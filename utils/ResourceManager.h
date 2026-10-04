/**
 * @file   ResourceManager.h
 * @brief  资源管理器类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef RESOURCEMANAGER_H
#define RESOURCEMANAGER_H

#include <QCache>
#include <QColor>
#include <QFuture>
#include <QIcon>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QTimer>
#include <QtConcurrent>

/**
 * @brief 资源优先级枚举
 */
enum class ResourcePriority {
    High,       // 高优先级：关键资源，始终预加载，不自动清理
    Medium,     // 中优先级：常用资源，按需加载，可选择性清理
    Low         // 低优先级：次要资源，按需加载，优先清理
};

/**
 * @brief 资源管理器类
 *
 * 负责管理应用程序中的图标和图片资源加载与缓存。
 * 支持按需加载、优先级管理和低配置模式优化。
 * 线程安全的单例模式实现。
 */
class ResourceManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 图标类型枚举
     */
    enum class IconType {
        Back,
        InstanceSelect,
        InstanceSettings,
        ScrollToTop,
        Refresh,
        Share,
        Star,
        Download,
        Search,
        Play,
        Install,
        Delete,
        Copy,
        ViewAll,
        Server,
        Translate
    };

    static ResourceManager* instance();
    ~ResourceManager();

    // 禁止拷贝和赋值
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // 加载图标
    QIcon loadIcon(IconType type);
    QIcon loadIcon(const QString& path, ResourcePriority priority = ResourcePriority::Medium);

    // 加载主题色图标（自动适配当前文字颜色）
    QIcon loadThemedIcon(IconType type);
    QIcon loadThemedIcon(const QString& svgPath);

    // 加载图片
    QPixmap loadPixmap(const QString& path, ResourcePriority priority = ResourcePriority::Medium);

    // 预加载资源（同步版本）- 只预加载高优先级资源
    void preloadResources();

    // 异步预加载资源
    QFuture<void> preloadResourcesAsync();

    // 检查资源是否已加载完成
    bool isResourcesLoaded() const;

    // 缓存清理方法
    void clearCache();                    // 清理所有缓存
    void clearLowPriorityCache();         // 清理低优先级资源
    void clearMediumPriorityCache();      // 清理中优先级资源
    void clearUnusedResources();          // 清理未使用的资源（保留高优先级）

    // 获取缓存统计信息
    int getIconCacheSize() const;
    int getPixmapCacheSize() const;
    qint64 getEstimatedMemoryUsage() const;

    // 设置缓存大小限制
    void setMaxIconCacheSize(int maxItems);
    void setMaxPixmapCacheSize(int maxItems);

    // 低配置模式支持
    void setLowConfigModeEnabled(bool enabled);
    bool isLowConfigModeEnabled() const;

signals:
    // 资源加载完成信号
    void resourcesLoaded();

    // 缓存清理信号
    void cacheCleared();

    // 低配置模式改变信号
    void lowConfigModeChanged(bool enabled);

private slots:
    // 定期清理缓存（低配置模式下使用）
    void onPeriodicCacheCleanup();

private:
    explicit ResourceManager(QObject *parent = nullptr);
    static ResourceManager* m_instance;
    static QMutex m_instanceMutex;

    // 缓存
    QCache<QString, QIcon> m_iconCache;
    QCache<QString, QPixmap> m_pixmapCache;

    // 资源优先级映射
    QMap<QString, ResourcePriority> m_iconPriorityMap;
    QMap<QString, ResourcePriority> m_pixmapPriorityMap;

    // 高优先级资源集合（用于快速查找）
    QSet<QString> m_highPriorityIcons;
    QSet<QString> m_highPriorityPixmaps;

    // 线程安全
    mutable QMutex m_mutex;
    bool m_resourcesLoaded;

    // 低配置模式
    bool m_lowConfigModeEnabled;
    QTimer* m_cacheCleanupTimer;

    // 获取图标路径
    QString getIconPath(IconType type) const;

    // 创建适配主题色的图标（将SVG中的currentColor替换为实际颜色）
    QIcon createColoredIcon(const QString& svgPath, const QColor& color);
    QString readSvgContent(const QString& svgPath) const;
    QString replaceCurrentColor(const QString& svgContent, const QColor& color) const;

    // 获取资源优先级
    ResourcePriority getIconPriority(IconType type) const;
    ResourcePriority getIconPriority(const QString& path) const;

    // 内部预加载实现（在后台线程执行）
    void preloadResourcesInternal();

    // 内部加载方法
    QIcon loadIconInternal(const QString& path, ResourcePriority priority);
    QPixmap loadPixmapInternal(const QString& path, ResourcePriority priority);

    // 初始化高优先级资源列表
    void initializeHighPriorityResources();

    // 更新缓存清理定时器
    void updateCacheCleanupTimer();
};

#endif // RESOURCEMANAGER_H
