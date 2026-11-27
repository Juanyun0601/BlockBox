/**
 * @file   LocalFavoritesManager.h
 * @brief  本地资源收藏管理器 - 单例，跨实例收藏实例内的模组/存档/截图/资源包/光影包/投影
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 与在线资源收藏（FavoritesManager）互不相干：本地收藏只存本地、不上云、
 * 不进侧边栏收藏夹，统一在账户管理页「我的收藏 → 本地收藏」里查看。
 *
 * 持久化文件: <数据目录>/local_favorites.json
 * 数据结构:
 * {
 *   "favorites": [
 *     { "instancePath": "...", "resourceType": "mods", "fileName": "a.jar",
 *       "displayName": "某模组", "filePath": "...", "fileSize": 123, "addedAt": 456 }
 *   ]
 * }
 *
 * 条目键为 实例路径 + 资源类型 + 归一化文件名（复用 LocalCategoryManager::normalizeFileName，
 * 忽略 .disabled 后缀），这样模组被禁用（文件重命名）后收藏仍能保持。
 */
#ifndef LOCALFAVORITESMANAGER_H
#define LOCALFAVORITESMANAGER_H

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

/**
 * @brief 单个本地资源收藏条目
 */
struct LocalFavoriteEntry
{
    QString instancePath;    // 所属实例根目录
    QString resourceType;    // 资源目录名：mods/saves/screenshots/resourcepacks/shaderpacks/schematics
    QString fileName;        // 归一化文件名（资源目录内，无 .disabled 后缀）
    QString displayName;     // 收藏时的展示名（模组中文名 > 英文名 > 文件名）
    QString filePath;        // 收藏时的绝对路径（打开/检测文件丢失用）
    qint64 fileSize = 0;
    qint64 addedAt = 0;      // 收藏时间（epoch ms）
    // 模组快照字段（收藏瞬间的元数据，账户页详情直接展示，其余类型为空）
    QString loaderType;      // fabric/forge/...
    QString version;         // 模组版本
    QString description;     // 模组简介

    /** 唯一键：实例路径 + 资源类型 + 归一化文件名 */
    QString key() const;
};

/**
 * @brief 本地资源收藏管理器（线程安全的单例）
 */
class LocalFavoritesManager : public QObject
{
    Q_OBJECT

public:
    static LocalFavoritesManager *instance();

    /** 是否已收藏指定本地资源（fileName 会自动归一化，传完整路径也行） */
    bool isFavorite(const QString &instancePath, const QString &resourceType, const QString &fileName) const;

    /** 收藏 / 取消收藏（重复设置无副作用；fileName 自动归一化） */
    void setFavorite(const LocalFavoriteEntry &entry, bool fav);

    /** 资源文件改名后同步收藏记录（保留收藏顺序与展示信息）；未收藏则忽略 */
    void renameFavorite(const QString &instancePath, const QString &resourceType,
                        const QString &oldFileName, const QString &newFileName,
                        const QString &newFilePath);

    /** 全部收藏（按收藏时间新 → 旧） */
    QVector<LocalFavoriteEntry> favorites() const;
    int count() const;

    /** 资源类型 → 中文显示名（模组/存档/截图/资源包/光影包/投影），未知返回原名 */
    static QString typeDisplayName(const QString &resourceType);

signals:
    /** 收藏新增 */
    void favoriteAdded(const LocalFavoriteEntry &entry);
    /** 收藏移除（key 为被移除条目的唯一键） */
    void favoriteRemoved(const QString &key);
    /** 任一增删后发出（列表/按钮批量刷新用） */
    void favoritesChanged();

private:
    explicit LocalFavoritesManager(QObject *parent = nullptr);

    void load();
    void save();
    QString filePath() const;
    /** 归一化后的条目键（任一部分为空返回空） */
    static QString normalizeKey(const QString &instancePath, const QString &resourceType, const QString &fileName);

    QVector<LocalFavoriteEntry> m_favorites;
    mutable QMutex m_mutex;

    static LocalFavoritesManager *s_instance;
    static QMutex s_instanceMutex;
};

#endif // LOCALFAVORITESMANAGER_H
