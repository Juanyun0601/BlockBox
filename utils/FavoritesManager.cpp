/**
 * @file   FavoritesManager.cpp
 * @brief  资源收藏夹管理器实现
 * @author BlockBox Team
 * @date   2026-07-18
 */
#include "FavoritesManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include "../platform.h"

FavoritesManager *FavoritesManager::s_instance = nullptr;
QMutex FavoritesManager::s_instanceMutex;

FavoritesManager::FavoritesManager(QObject *parent)
    : QObject(parent)
{
    load();
}

FavoritesManager *FavoritesManager::instance()
{
    if (!s_instance)
    {
        QMutexLocker locker(&s_instanceMutex);
        if (!s_instance)
        {
            s_instance = new FavoritesManager();
        }
    }
    return s_instance;
}

QString FavoritesManager::filePath() const
{
    // 基岩版与 Java 版收藏夹独立存储
    return m_bedrockMode
        ? Platform::getDataDirectory() + "/favorites_bedrock.json"
        : Platform::getDataDirectory() + "/favorites.json";
}

void FavoritesManager::setBedrockMode(bool bedrock)
{
    if (m_bedrockMode == bedrock)
        return;
    m_bedrockMode = bedrock;
    load();
    emit foldersChanged();
}

ContentType FavoritesManager::bedrockClassIdToContentType(int classId)
{
    switch (classId) {
    case 4984: return ContentType::Mod;        // 附加包 Addons
    case 6913: return ContentType::World;      // 地图 Maps
    case 6940: return ContentType::DataPack;   // 脚本 Scripts
    case 6925:                                 // 皮肤 Skins
    case 6929:                                 // 资源包 Texture Packs
    default:   return ContentType::ResourcePack;
    }
}

QString FavoritesManager::bedrockClassIdToCategoryName(int classId)
{
    switch (classId) {
    case 4984: return QStringLiteral("附加包");
    case 6929: return QStringLiteral("资源包");
    case 6913: return QStringLiteral("地图");
    case 6940: return QStringLiteral("脚本");
    case 6925: return QStringLiteral("皮肤");
    default:   return QStringLiteral("基岩版资源");
    }
}

QString FavoritesManager::makeItemId(const QString &source, const QString &remoteId)
{
    return source + ":" + remoteId;
}

QString FavoriteItem::displayName() const
{
    if (!chineseName.isEmpty())
    {
        return chineseName;
    }
    if (!englishName.isEmpty())
    {
        return englishName;
    }
    return name;
}

QString FavoriteItem::displayType() const
{
    // 基岩版资源优先显示其真实分类名（资源包/皮肤/地图/脚本/附加包）
    if (!bedrockCategory.isEmpty())
    {
        return bedrockCategory;
    }
    return ContentTypeConfig::getConfig(static_cast<ContentType>(contentType)).displayName;
}

ModInfo FavoriteItem::toModInfo() const
{
    ModInfo info;
    info.id = remoteId;
    info.source = source;
    info.name = name;
    info.chineseName = chineseName;
    info.englishName = englishName.isEmpty() ? name : englishName;
    info.description = description;
    info.iconUrl = iconUrl;
    info.pageUrl = pageUrl;
    info.author = author;
    info.downloadCount = downloadCount;
    return info;
}

static FavoriteItem itemFromJson(const QJsonObject &obj)
{
    FavoriteItem item;
    item.id = obj["id"].toString();
    item.remoteId = obj["remoteId"].toString();
    item.source = obj["source"].toString();
    item.contentType = obj["contentType"].toInt();
    item.name = obj["name"].toString();
    item.chineseName = obj["chineseName"].toString();
    item.englishName = obj["englishName"].toString();
    item.description = obj["description"].toString();
    item.iconUrl = obj["iconUrl"].toString();
    item.pageUrl = obj["pageUrl"].toString();
    item.author = obj["author"].toString();
    item.downloadCount = static_cast<qint64>(obj["downloadCount"].toDouble());
    item.addedAt = static_cast<qint64>(obj["addedAt"].toDouble());
    item.bedrockClassId = obj["bedrockClassId"].toInt();
    item.bedrockCategory = obj["bedrockCategory"].toString();
    return item;
}

static QJsonObject itemToJson(const FavoriteItem &item)
{
    QJsonObject obj;
    obj["id"] = item.id;
    obj["remoteId"] = item.remoteId;
    obj["source"] = item.source;
    obj["contentType"] = item.contentType;
    obj["name"] = item.name;
    obj["chineseName"] = item.chineseName;
    obj["englishName"] = item.englishName;
    obj["description"] = item.description;
    obj["iconUrl"] = item.iconUrl;
    obj["pageUrl"] = item.pageUrl;
    obj["author"] = item.author;
    obj["downloadCount"] = static_cast<double>(item.downloadCount);
    obj["addedAt"] = static_cast<double>(item.addedAt);
    obj["bedrockClassId"] = item.bedrockClassId;
    obj["bedrockCategory"] = item.bedrockCategory;
    return obj;
}

static FavoriteFolder folderFromJson(const QJsonObject &obj)
{
    FavoriteFolder folder;
    folder.id = obj["id"].toString();
    folder.name = obj["name"].toString();
    folder.createdAt = static_cast<qint64>(obj["createdAt"].toDouble());

    const QJsonArray items = obj["items"].toArray();
    for (const QJsonValue &v : items)
    {
        if (v.isObject())
        {
            folder.items.append(itemFromJson(v.toObject()));
        }
    }
    return folder;
}

static QJsonObject folderToJson(const FavoriteFolder &folder)
{
    QJsonObject obj;
    obj["id"] = folder.id;
    obj["name"] = folder.name;
    obj["createdAt"] = static_cast<double>(folder.createdAt);

    QJsonArray items;
    for (const FavoriteItem &item : folder.items)
    {
        items.append(itemToJson(item));
    }
    obj["items"] = items;
    return obj;
}

void FavoritesManager::load()
{
    QMutexLocker locker(&m_mutex);

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
    {
        // 首次使用：自动创建一个"默认收藏夹"并持久化，避免每次启动 UUID 变化
        FavoriteFolder defaultFolder;
        defaultFolder.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        defaultFolder.name = tr("默认收藏夹");
        defaultFolder.createdAt = QDateTime::currentMSecsSinceEpoch();
        m_folders.append(defaultFolder);
        save();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    m_folders.clear();
    const QJsonArray arr = doc.object().value("folders").toArray();
    for (const QJsonValue &v : arr)
    {
        if (v.isObject())
        {
            m_folders.append(folderFromJson(v.toObject()));
        }
    }

    if (m_folders.isEmpty())
    {
        FavoriteFolder defaultFolder;
        defaultFolder.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        defaultFolder.name = tr("默认收藏夹");
        defaultFolder.createdAt = QDateTime::currentMSecsSinceEpoch();
        m_folders.append(defaultFolder);
        save();
    }
}

void FavoritesManager::save()
{
    QJsonObject root;
    QJsonArray arr;
    for (const FavoriteFolder &folder : m_folders)
    {
        arr.append(folderToJson(folder));
    }
    root["folders"] = arr;

    QJsonDocument doc(root);

    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile file(filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qWarning() << "[FavoritesManager] 收藏夹文件写入失败:" << filePath()
                   << file.errorString();
        return;
    }
    file.write(doc.toJson());
    file.close();
}

QList<FavoriteFolder> FavoritesManager::folders() const
{
    QMutexLocker locker(&m_mutex);
    return m_folders;
}

FavoriteFolder FavoritesManager::folder(const QString &folderId) const
{
    QMutexLocker locker(&m_mutex);
    for (const FavoriteFolder &f : m_folders)
    {
        if (f.id == folderId)
        {
            return f;
        }
    }
    return FavoriteFolder();
}

QString FavoritesManager::createFolder(const QString &name)
{
    QString finalName = name.trimmed();
    if (finalName.isEmpty())
    {
        finalName = tr("新建收藏夹");
    }

    QMutexLocker locker(&m_mutex);
    FavoriteFolder folder;
    folder.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    folder.name = finalName;
    folder.createdAt = QDateTime::currentMSecsSinceEpoch();
    m_folders.append(folder);
    save();

    QString newId = folder.id;
    locker.unlock();
    emit foldersChanged();
    return newId;
}

bool FavoritesManager::renameFolder(const QString &folderId, const QString &newName)
{
    QString finalName = newName.trimmed();
    if (finalName.isEmpty())
    {
        return false;
    }

    QMutexLocker locker(&m_mutex);
    for (FavoriteFolder &f : m_folders)
    {
        if (f.id == folderId)
        {
            f.name = finalName;
            save();
            locker.unlock();
            emit foldersChanged();
            emit folderChanged(folderId);
            return true;
        }
    }
    return false;
}

bool FavoritesManager::deleteFolder(const QString &folderId)
{
    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < m_folders.size(); ++i)
    {
        if (m_folders[i].id == folderId)
        {
            // 不允许删除最后一个收藏夹
            if (m_folders.size() <= 1)
            {
                return false;
            }
            m_folders.removeAt(i);
            save();
            locker.unlock();
            emit foldersChanged();
            return true;
        }
    }
    return false;
}

bool FavoritesManager::addFavorite(const QString &folderId, const ModInfo &info, ContentType type, const QString &source,
                                   int bedrockClassId, const QString &bedrockCategory)
{
    if (info.id.isEmpty())
    {
        return false;
    }

    FavoriteItem item;
    item.remoteId = info.id;
    item.source = source;
    item.id = makeItemId(source, info.id);
    item.contentType = static_cast<int>(type);
    item.name = info.name;
    item.chineseName = info.chineseName;
    item.englishName = info.englishName.isEmpty() ? info.name : info.englishName;
    item.description = info.description;
    item.iconUrl = info.iconUrl;
    item.pageUrl = info.pageUrl;
    item.author = info.author;
    item.downloadCount = info.downloadCount;
    item.addedAt = QDateTime::currentMSecsSinceEpoch();
    item.bedrockClassId = bedrockClassId;
    item.bedrockCategory = bedrockCategory;

    QMutexLocker locker(&m_mutex);
    for (FavoriteFolder &f : m_folders)
    {
        if (f.id == folderId)
        {
            // 去重
            for (const FavoriteItem &existing : f.items)
            {
                if (existing.id == item.id)
                {
                    return false;
                }
            }
            f.items.append(item);
            save();
            locker.unlock();
            emit folderChanged(folderId);
            return true;
        }
    }
    return false;
}

bool FavoritesManager::removeFavorite(const QString &folderId, const QString &itemId)
{
    QMutexLocker locker(&m_mutex);
    for (FavoriteFolder &f : m_folders)
    {
        if (f.id == folderId)
        {
            for (int i = 0; i < f.items.size(); ++i)
            {
                if (f.items[i].id == itemId)
                {
                    f.items.removeAt(i);
                    save();
                    locker.unlock();
                    emit folderChanged(folderId);
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

bool FavoritesManager::isFavorite(const QString &folderId, const QString &itemId) const
{
    QMutexLocker locker(&m_mutex);
    for (const FavoriteFolder &f : m_folders)
    {
        if (f.id == folderId)
        {
            for (const FavoriteItem &item : f.items)
            {
                if (item.id == itemId)
                {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

QList<QString> FavoritesManager::foldersContaining(const QString &itemId) const
{
    QMutexLocker locker(&m_mutex);
    QList<QString> result;
    for (const FavoriteFolder &f : m_folders)
    {
        for (const FavoriteItem &item : f.items)
        {
            if (item.id == itemId)
            {
                result.append(f.id);
                break;
            }
        }
    }
    return result;
}

QList<FavoriteItem> FavoritesManager::items(const QString &folderId) const
{
    QMutexLocker locker(&m_mutex);
    for (const FavoriteFolder &f : m_folders)
    {
        if (f.id == folderId)
        {
            return f.items;
        }
    }
    return QList<FavoriteItem>();
}
