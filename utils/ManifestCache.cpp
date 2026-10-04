/**
 * @file   ManifestCache.cpp
 * @brief  版本清单缓存类实现
 * @author BlockBox Team
 * @date   2026-05-29
 */

#include "ManifestCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

ManifestCache *ManifestCache::m_instance = nullptr;
QMutex ManifestCache::m_instanceMutex;

ManifestCache* ManifestCache::instance()
{
    if (m_instance == nullptr)
    {
        QMutexLocker locker(&m_instanceMutex);
        if (m_instance == nullptr)
        {
            m_instance = new ManifestCache();
        }
    }
    return m_instance;
}

ManifestCache::ManifestCache()
    : m_lastFetchTime(0)
{
    loadFromDisk();
}

ManifestCache::~ManifestCache()
{
}

bool ManifestCache::isManifestValid() const
{
    QMutexLocker locker(&m_dataMutex);
    if (m_manifestJson.isEmpty())
    {
        return false;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    return (now - m_lastFetchTime) < CACHE_TTL_MS;
}

QJsonObject ManifestCache::getManifest() const
{
    QMutexLocker locker(&m_dataMutex);
    return m_manifestJson;
}

QString ManifestCache::getVersionUrl(const QString &versionId) const
{
    QMutexLocker locker(&m_dataMutex);
    return m_versionUrlIndex.value(versionId);
}

void ManifestCache::setManifest(const QJsonObject &manifest)
{
    QMutexLocker locker(&m_dataMutex);
    m_manifestJson = manifest;
    m_lastFetchTime = QDateTime::currentMSecsSinceEpoch();

    m_versionUrlIndex.clear();
    QJsonArray versions = manifest.value("versions").toArray();
    for (const QJsonValue &val : versions)
    {
        QJsonObject obj = val.toObject();
        QString id = obj.value("id").toString();
        QString url = obj.value("url").toString();
        if (!id.isEmpty() && !url.isEmpty())
        {
            m_versionUrlIndex.insert(id, url);
        }
    }

    saveManifestToDisk();
}

QJsonObject ManifestCache::getVersionJson(const QString &versionId) const
{
    QMutexLocker locker(&m_dataMutex);
    if (m_versionJsonCache.contains(versionId))
    {
        return m_versionJsonCache.value(versionId);
    }

    QString dir = cacheDirPath() + "/versions/";
    QFile file(dir + versionId + ".json");
    if (file.open(QIODevice::ReadOnly))
    {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject())
        {
            QJsonObject json = doc.object();
            m_versionJsonCache.insert(versionId, json);
            return json;
        }
    }

    return QJsonObject();
}

void ManifestCache::setVersionJson(const QString &versionId, const QJsonObject &json)
{
    QMutexLocker locker(&m_dataMutex);
    m_versionJsonCache.insert(versionId, json);
    saveVersionJsonToDisk(versionId);
}

QJsonObject ManifestCache::getAssetIndex(const QString &assetIndexId) const
{
    QMutexLocker locker(&m_dataMutex);
    if (m_assetIndexCache.contains(assetIndexId))
    {
        return m_assetIndexCache.value(assetIndexId);
    }

    QString dir = cacheDirPath() + "/assets/";
    QFile file(dir + assetIndexId + ".json");
    if (file.open(QIODevice::ReadOnly))
    {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject())
        {
            QJsonObject json = doc.object();
            m_assetIndexCache.insert(assetIndexId, json);
            return json;
        }
    }

    return QJsonObject();
}

void ManifestCache::setAssetIndex(const QString &assetIndexId, const QJsonObject &json)
{
    QMutexLocker locker(&m_dataMutex);
    m_assetIndexCache.insert(assetIndexId, json);
    saveAssetIndexToDisk(assetIndexId);
}

void ManifestCache::loadFromDisk()
{
    QString cacheDir = cacheDirPath();
    QDir manifestDir(cacheDir);
    QFile manifestFile(cacheDir + "/manifest_v2.json");
    if (manifestFile.open(QIODevice::ReadOnly))
    {
        QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll());
        manifestFile.close();
        if (doc.isObject())
        {
            m_manifestJson = doc.object();
            QJsonArray versions = m_manifestJson.value("versions").toArray();
            for (const QJsonValue &val : versions)
            {
                QJsonObject obj = val.toObject();
                QString id = obj.value("id").toString();
                QString url = obj.value("url").toString();
                if (!id.isEmpty() && !url.isEmpty())
                {
                    m_versionUrlIndex.insert(id, url);
                }
            }
        }
    }

    QDir versionsDir(cacheDir + "/versions/");
    if (versionsDir.exists())
    {
        QStringList versionFiles = versionsDir.entryList({"*.json"}, QDir::Files);
        for (const QString &fileName : versionFiles)
        {
            QString versionId = fileName.chopped(5);
            QFile vf(versionsDir.filePath(fileName));
            if (vf.open(QIODevice::ReadOnly))
            {
                QJsonDocument doc = QJsonDocument::fromJson(vf.readAll());
                vf.close();
                if (doc.isObject())
                {
                    m_versionJsonCache.insert(versionId, doc.object());
                }
            }
        }
    }

    QDir assetsDir(cacheDir + "/assets/");
    if (assetsDir.exists())
    {
        QStringList assetFiles = assetsDir.entryList({"*.json"}, QDir::Files);
        for (const QString &fileName : assetFiles)
        {
            QString assetIndexId = fileName.chopped(5);
            QFile af(assetsDir.filePath(fileName));
            if (af.open(QIODevice::ReadOnly))
            {
                QJsonDocument doc = QJsonDocument::fromJson(af.readAll());
                af.close();
                if (doc.isObject())
                {
                    m_assetIndexCache.insert(assetIndexId, doc.object());
                }
            }
        }
    }
}

void ManifestCache::saveManifestToDisk() const
{
    QString dir = cacheDirPath();
    QDir().mkpath(dir);
    QFile file(dir + "/manifest_v2.json");
    if (file.open(QIODevice::WriteOnly))
    {
        QJsonDocument doc(m_manifestJson);
        file.write(doc.toJson());
        file.close();
    }
}

void ManifestCache::saveVersionJsonToDisk(const QString &versionId) const
{
    if (!m_versionJsonCache.contains(versionId))
    {
        return;
    }
    QString dir = cacheDirPath() + "/versions/";
    QDir().mkpath(dir);
    QFile file(dir + versionId + ".json");
    if (file.open(QIODevice::WriteOnly))
    {
        QJsonDocument doc(m_versionJsonCache.value(versionId));
        file.write(doc.toJson());
        file.close();
    }
}

void ManifestCache::saveAssetIndexToDisk(const QString &assetIndexId) const
{
    if (!m_assetIndexCache.contains(assetIndexId))
    {
        return;
    }
    QString dir = cacheDirPath() + "/assets/";
    QDir().mkpath(dir);
    QFile file(dir + assetIndexId + ".json");
    if (file.open(QIODevice::WriteOnly))
    {
        QJsonDocument doc(m_assetIndexCache.value(assetIndexId));
        file.write(doc.toJson());
        file.close();
    }
}

QString ManifestCache::cacheDirPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cache";
}