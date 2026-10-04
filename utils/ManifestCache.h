/**
 * @file   ManifestCache.h
 * @brief  版本清单缓存类声明
 * @author BlockBox Team
 * @date   2026-05-29
 */

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>

class ManifestCache : public QObject
{
    Q_OBJECT

public:
    static ManifestCache* instance();

    bool isManifestValid() const;
    QJsonObject getManifest() const;
    QString getVersionUrl(const QString &versionId) const;
    void setManifest(const QJsonObject &manifest);

    QJsonObject getVersionJson(const QString &versionId) const;
    void setVersionJson(const QString &versionId, const QJsonObject &json);

    QJsonObject getAssetIndex(const QString &assetIndexId) const;
    void setAssetIndex(const QString &assetIndexId, const QJsonObject &json);

private:
    ManifestCache();
    ~ManifestCache();

    void loadFromDisk();
    void saveManifestToDisk() const;
    void saveVersionJsonToDisk(const QString &versionId) const;
    void saveAssetIndexToDisk(const QString &assetIndexId) const;
    QString cacheDirPath() const;

    static ManifestCache *m_instance;
    static QMutex m_instanceMutex;
    mutable QMutex m_dataMutex;

    QJsonObject m_manifestJson;
    QHash<QString, QString> m_versionUrlIndex;
    mutable QHash<QString, QJsonObject> m_versionJsonCache;
    mutable QHash<QString, QJsonObject> m_assetIndexCache;
    qint64 m_lastFetchTime;
    static constexpr qint64 CACHE_TTL_MS = 3600000;
};