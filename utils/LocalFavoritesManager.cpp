/**
 * @file   LocalFavoritesManager.cpp
 * @brief  本地资源收藏管理器实现
 * @author BlockBox Team
 * @date   2026-11-14
 */
#include "LocalFavoritesManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>

#include "LocalCategoryManager.h"
#include "../platform.h"

LocalFavoritesManager *LocalFavoritesManager::s_instance = nullptr;
QMutex LocalFavoritesManager::s_instanceMutex;

LocalFavoritesManager::LocalFavoritesManager(QObject *parent)
    : QObject(parent)
{
    load();
}

LocalFavoritesManager *LocalFavoritesManager::instance()
{
    if (!s_instance)
    {
        QMutexLocker locker(&s_instanceMutex);
        if (!s_instance)
        {
            s_instance = new LocalFavoritesManager();
        }
    }
    return s_instance;
}

QString LocalFavoriteEntry::key() const
{
    return instancePath + "|" + resourceType + "|" + fileName;
}

QString LocalFavoritesManager::typeDisplayName(const QString &resourceType)
{
    if (resourceType == QLatin1String("mods"))
        return QStringLiteral("模组");
    if (resourceType == QLatin1String("saves"))
        return QStringLiteral("存档");
    if (resourceType == QLatin1String("screenshots"))
        return QStringLiteral("截图");
    if (resourceType == QLatin1String("resourcepacks"))
        return QStringLiteral("资源包");
    if (resourceType == QLatin1String("shaderpacks"))
        return QStringLiteral("光影包");
    if (resourceType == QLatin1String("datapacks"))
        return QStringLiteral("数据包");
    if (resourceType == QLatin1String("schematics"))
        return QStringLiteral("投影");
    return resourceType;
}

QString LocalFavoritesManager::filePath() const
{
    return Platform::getDataDirectory() + "/local_favorites.json";
}

QString LocalFavoritesManager::normalizeKey(const QString &instancePath, const QString &resourceType,
                                            const QString &fileName)
{
    const QString file = LocalCategoryManager::normalizeFileName(fileName);
    const QString path = QDir::fromNativeSeparators(instancePath.trimmed());
    if (path.isEmpty() || resourceType.isEmpty() || file.isEmpty())
        return QString();
    return path + "|" + resourceType + "|" + file;
}

bool LocalFavoritesManager::isFavorite(const QString &instancePath, const QString &resourceType,
                                       const QString &fileName) const
{
    const QString key = normalizeKey(instancePath, resourceType, fileName);
    if (key.isEmpty())
        return false;

    QMutexLocker locker(&m_mutex);
    for (const LocalFavoriteEntry &entry : m_favorites)
    {
        if (entry.key() == key)
            return true;
    }
    return false;
}

void LocalFavoritesManager::setFavorite(const LocalFavoriteEntry &entry, bool fav)
{
    LocalFavoriteEntry normalized = entry;
    normalized.fileName = LocalCategoryManager::normalizeFileName(normalized.fileName);
    normalized.instancePath = QDir::fromNativeSeparators(normalized.instancePath.trimmed());
    normalized.filePath = QDir::fromNativeSeparators(normalized.filePath);
    if (normalized.fileName.isEmpty() || normalized.instancePath.isEmpty() || normalized.resourceType.isEmpty())
        return;

    if (normalized.addedAt <= 0)
        normalized.addedAt = QDateTime::currentMSecsSinceEpoch();

    const QString key = normalized.key();

    QMutexLocker locker(&m_mutex);
    int index = -1;
    for (int i = 0; i < m_favorites.size(); ++i)
    {
        if (m_favorites.at(i).key() == key)
        {
            index = i;
            break;
        }
    }

    if (fav)
    {
        if (index >= 0)
        {
            // 已收藏：仅刷新展示信息（文件改名/模组更新等场景），不重复发信号
            const LocalFavoriteEntry &old = m_favorites.at(index);
            if (old.displayName == normalized.displayName && old.filePath == normalized.filePath
                && old.loaderType == normalized.loaderType && old.version == normalized.version
                && old.description == normalized.description && old.fileSize == normalized.fileSize)
                return;
            normalized.addedAt = old.addedAt;
            m_favorites.replace(index, normalized);
        }
        else
        {
            m_favorites.prepend(normalized);   // 新收藏排最前
        }
        save();
        emit favoriteAdded(normalized);
    }
    else
    {
        if (index < 0)
            return;
        m_favorites.removeAt(index);
        save();
        emit favoriteRemoved(key);
    }
    emit favoritesChanged();
}

QVector<LocalFavoriteEntry> LocalFavoritesManager::favorites() const
{
    QMutexLocker locker(&m_mutex);
    return m_favorites;
}

int LocalFavoritesManager::count() const
{
    QMutexLocker locker(&m_mutex);
    return m_favorites.size();
}

void LocalFavoritesManager::load()
{
    m_favorites.clear();

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return;

    const QJsonArray arr = doc.object().value("favorites").toArray();
    for (const QJsonValue &v : arr)
    {
        if (!v.isObject())
            continue;
        const QJsonObject obj = v.toObject();
        LocalFavoriteEntry entry;
        entry.instancePath = QDir::fromNativeSeparators(obj["instancePath"].toString());
        entry.resourceType = obj["resourceType"].toString();
        entry.fileName = LocalCategoryManager::normalizeFileName(obj["fileName"].toString());
        entry.displayName = obj["displayName"].toString();
        entry.filePath = QDir::fromNativeSeparators(obj["filePath"].toString());
        entry.fileSize = static_cast<qint64>(obj["fileSize"].toDouble());
        entry.addedAt = static_cast<qint64>(obj["addedAt"].toDouble());
        entry.loaderType = obj["loaderType"].toString();
        entry.version = obj["version"].toString();
        entry.description = obj["description"].toString();
        if (!entry.instancePath.isEmpty() && !entry.resourceType.isEmpty() && !entry.fileName.isEmpty())
            m_favorites.append(entry);
    }
}

void LocalFavoritesManager::save()
{
    QJsonObject root;
    QJsonArray arr;
    for (const LocalFavoriteEntry &entry : m_favorites)
    {
        QJsonObject obj;
        obj["instancePath"] = entry.instancePath;
        obj["resourceType"] = entry.resourceType;
        obj["fileName"] = entry.fileName;
        obj["displayName"] = entry.displayName;
        obj["filePath"] = entry.filePath;
        obj["fileSize"] = static_cast<double>(entry.fileSize);
        obj["addedAt"] = static_cast<double>(entry.addedAt);
        if (!entry.loaderType.isEmpty())
            obj["loaderType"] = entry.loaderType;
        if (!entry.version.isEmpty())
            obj["version"] = entry.version;
        if (!entry.description.isEmpty())
            obj["description"] = entry.description;
        arr.append(obj);
    }
    root["favorites"] = arr;

    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}
