/**
 * @file   FavoritesManager.h
 * @brief  资源收藏夹管理器 - 单例，负责收藏夹分组与条目的持久化管理
 * @author BlockBox Team
 * @date   2026-07-18
 */
#ifndef FAVORITESMANAGER_H
#define FAVORITESMANAGER_H

#include <QObject>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QString>
#include <QDateTime>

#include "utils/content/ContentData.h"
#include "utils/mod/ModData.h"

/**
 * @brief 单个收藏条目（仅保留展示所需字段，详情查看时按 source+id 重新拉取）
 */
struct FavoriteItem
{
    QString id;            // 全局唯一 ID: source + ":" + remoteId
    QString remoteId;      // 远端项目 ID（CurseForge / Modrinth）
    QString source;        // "curseforge" / "modrinth"
    int contentType = 0;   // ContentType 枚举值
    QString name;
    QString chineseName;
    QString englishName;
    QString description;
    QString iconUrl;
    QString pageUrl;
    QString author;
    qint64 downloadCount = 0;
    qint64 addedAt = 0;    // 收藏时间（epoch ms）
    int bedrockClassId = 0;    // 基岩版 CurseForge 分类 classId（0 = 非基岩版资源）
    QString bedrockCategory;   // 基岩版分类显示名（资源包/皮肤/地图/脚本/附加包）

    QString displayName() const;
    QString displayType() const;

    /**
     * @brief 转换为 ModInfo，便于复用收藏/下载等接收 ModInfo 的接口
     */
    ModInfo toModInfo() const;
};

/**
 * @brief 收藏夹分组
 */
struct FavoriteFolder
{
    QString id;
    QString name;
    qint64 createdAt = 0;
    QList<FavoriteItem> items;

    bool isEmpty() const { return items.isEmpty(); }
};

/**
 * @brief 收藏夹管理器（线程安全的单例）
 *
 * 持久化文件: <应用数据目录>/favorites.json
 * 数据结构:
 * {
 *   "folders": [
 *     { "id": "...", "name": "...", "createdAt": ..., "items": [ ... ] }
 *   ]
 * }
 */
class FavoritesManager : public QObject
{
    Q_OBJECT

public:
    static FavoritesManager *instance();

    /** 设置当前版本模式（true=基岩版），收藏夹与 Java 版各自独立存储 */
    void setBedrockMode(bool bedrock);
    /** 当前是否为基岩版模式 */
    bool isBedrockMode() const { return m_bedrockMode; }

    /** 基岩版 CurseForge classId → ContentType 映射 */
    static ContentType bedrockClassIdToContentType(int classId);
    /** 基岩版 CurseForge classId → 分类显示名（资源包/皮肤/地图/脚本/附加包） */
    static QString bedrockClassIdToCategoryName(int classId);

    // ===== 收藏夹分组 =====
    QList<FavoriteFolder> folders() const;
    FavoriteFolder folder(const QString &folderId) const;
    QString createFolder(const QString &name);           // 返回新分组 ID
    bool renameFolder(const QString &folderId, const QString &newName);
    bool deleteFolder(const QString &folderId);

    // ===== 收藏条目 =====
    bool addFavorite(const QString &folderId, const ModInfo &info, ContentType type, const QString &source,
                     int bedrockClassId = 0, const QString &bedrockCategory = QString());
    bool removeFavorite(const QString &folderId, const QString &itemId);
    bool isFavorite(const QString &folderId, const QString &itemId) const;
    QList<QString> foldersContaining(const QString &itemId) const;  // 返回含此条目的分组 ID 列表
    QList<FavoriteItem> items(const QString &folderId) const;

signals:
    void foldersChanged();
    void folderChanged(const QString &folderId);

private:
    explicit FavoritesManager(QObject *parent = nullptr);

    void load();
    void save();
    QString filePath() const;
    static QString makeItemId(const QString &source, const QString &remoteId);

    QList<FavoriteFolder> m_folders;
    mutable QMutex m_mutex;
    bool m_bedrockMode = false;   // 当前版本模式（true=基岩版）

    static FavoritesManager *s_instance;
    static QMutex s_instanceMutex;
};

#endif // FAVORITESMANAGER_H
