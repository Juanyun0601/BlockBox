/**
 * @file   BingWallpaperManager.h
 * @brief  必应每日壁纸管理器：拉取必应官方壁纸列表，下载并缓存图片供背景模式使用
 * @author BlockBox Team
 * @date   2026-10-05
 */

#ifndef BINGWALLPAPERMANAGER_H
#define BINGWALLPAPERMANAGER_H

#include <functional>

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief 必应每日壁纸管理器（单例）
 *
 * 通过必应官方 HPImageArchive 接口获取最近 8 天的每日壁纸元数据，
 * 缩略图与原图按需下载至 {数据目录}/cache/bing/ 并永久缓存。
 * 选中某张壁纸后，将本地缓存路径写入 BackgroundManager::setBingImagePath，
 * 由既有的 backgroundChanged 信号驱动主窗口刷新背景。
 */
class BingWallpaperManager : public QObject
{
    Q_OBJECT

public:
    /// 单张壁纸的元数据（来自 HPImageArchive 接口的 images 数组）
    struct WallpaperInfo {
        QString date;       ///< 壁纸日期，如 20261004
        QString urlbase;    ///< 无尺寸后缀的图片地址，如 /th?id=OHR.Xxx_EN-US123
        QString fullUrl;    ///< 原图完整 URL（1920x1080）
        QString title;      ///< 壁纸标题
        QString copyright;  ///< 版权描述
        QString hsh;        ///< 必应提供的哈希，用于缓存命名兜底
    };

    static BingWallpaperManager* instance();

    /// 已加载的壁纸列表（未加载完成时为空）
    QList<WallpaperInfo> wallpapers() const { return m_wallpapers; }

    /// 当前选中的壁纸在列表中的下标
    int currentIndex() const { return m_currentIndex; }

    /// 当前选中壁纸的本地缓存路径（未下载完成或未加载时为空）
    QString currentImagePath() const;

    /// 指定下标壁纸缩略图的本地路径（可能尚未下载完成，用 QFile::exists 判断）
    QString thumbPath(int index) const;

    /**
     * @brief 确保壁纸列表与当前壁纸可用（幂等，可安全重复调用）
     *
     * 列表未加载时发起异步请求；已加载时仅检查当前壁纸是否已缓存，
     * 未缓存则下载。命中缓存立即通过 wallpaperImageReady 上报本地路径。
     */
    void ensureLoaded();

    /**
     * @brief 强制重新拉取壁纸列表，成功后将选中项重置为最新一天
     */
    void refresh();

    /**
     * @brief 选择指定下标的壁纸，必要时异步下载
     */
    void selectWallpaper(int index);

    /**
     * @brief 壁纸缓存目录：{数据目录}/cache/bing
     */
    static QString cacheDir();

signals:
    /// 壁纸列表加载（或刷新）完成，设置页据此重建缩略图栏
    void wallpaperListUpdated();

    /// 缩略图下载完成：index 为列表下标，path 为本地缩略图路径
    void wallpaperThumbReady(int index, const QString &path);

    /// 当前选中壁纸原图就绪（已缓存或下载完成），path 为本地文件路径
    void wallpaperImageReady(const QString &path);

    /// 列表或图片获取失败，reason 为给用户的提示文本
    void fetchFailed(const QString &reason);

private:
    explicit BingWallpaperManager(QObject *parent = nullptr);

    void fetchList();
    void downloadMissingThumbs();
    void downloadImage(const QString &url, const QString &savePath,
                       const std::function<void(const QString &)> &onSuccess,
                       const std::function<void(const QString &)> &onFailure);
    void ensureCurrentImage();
    QString cacheFileName(const WallpaperInfo &info, const QString &suffix) const;
    static QString normalizeUrlbase(const QString &urlbase);
    static QString buildImageUrl(const QString &urlbase, int width, int height);

    QNetworkAccessManager *m_networkManager;
    QList<WallpaperInfo> m_wallpapers;
    int m_currentIndex = 0;
    bool m_listLoading = false;
    bool m_imageDownloading = false;
    static BingWallpaperManager* m_instance;
    static QMutex m_instanceMutex;
};

#endif // BINGWALLPAPERMANAGER_H
