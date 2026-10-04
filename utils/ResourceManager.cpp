/**
 * @file   ResourceManager.cpp
 * @brief  资源管理器类实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "ResourceManager.h"

#include <QDebug>
#include <QFile>
#include <QPainter>
#include <QMutexLocker>
#include <QSvgRenderer>
#include <QThread>

#include "LowConfigMode.h"
#include "ThemeManager.h"

// 初始化静态成员变量
ResourceManager* ResourceManager::m_instance = nullptr;
QMutex ResourceManager::m_instanceMutex;

ResourceManager::ResourceManager(QObject *parent)
    : QObject(parent)
    , m_resourcesLoaded(false)
    , m_lowConfigModeEnabled(false)
    , m_cacheCleanupTimer(nullptr)
{
    // 设置缓存大小
    m_iconCache.setMaxCost(100);   // 最多缓存100个图标
    m_pixmapCache.setMaxCost(50);  // 最多缓存50个图片

    // 初始化高优先级资源列表
    initializeHighPriorityResources();

    // 创建定期清理定时器（低配置模式下使用）
    m_cacheCleanupTimer = new QTimer(this);
    connect(m_cacheCleanupTimer, &QTimer::timeout, this, &ResourceManager::onPeriodicCacheCleanup);

    // 连接 LowConfigMode 信号
    connect(LowConfigMode::instance(), &LowConfigMode::lowConfigModeChanged,
            this, &ResourceManager::setLowConfigModeEnabled);

    // 初始化时检查低配置模式状态
    m_lowConfigModeEnabled = LowConfigMode::instance()->isLowConfigMode();
    updateCacheCleanupTimer();

    qDebug() << "[ResourceManager]" << "ResourceManager initialized:";
    qDebug() << "[ResourceManager]" << "  - Low Config Mode:" << m_lowConfigModeEnabled;
    qDebug() << "[ResourceManager]" << "  - High Priority Icons:" << m_highPriorityIcons.size();
}

ResourceManager::~ResourceManager()
{
    if (m_cacheCleanupTimer) {
        m_cacheCleanupTimer->stop();
    }

    QMutexLocker locker(&m_mutex);
    m_iconCache.clear();
    m_pixmapCache.clear();
    m_iconPriorityMap.clear();
    m_pixmapPriorityMap.clear();
    m_highPriorityIcons.clear();
    m_highPriorityPixmaps.clear();
}

ResourceManager* ResourceManager::instance()
{
    // 双检锁模式实现线程安全的单例
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new ResourceManager();
        }
    }
    return m_instance;
}

void ResourceManager::initializeHighPriorityResources()
{
    // 初始化高优先级图标路径集合
    // 这些是关键资源，始终预加载，不会被自动清理
    m_highPriorityIcons.insert(":/Images/Icons/back.svg");
    m_highPriorityIcons.insert(":/Images/Icons/instance_select.svg");
    m_highPriorityIcons.insert(":/Images/Icons/instance_settings.svg");
    m_highPriorityIcons.insert(":/Images/Icons/back_to_top.svg");
    m_highPriorityIcons.insert(":/Images/Icons/refresh.svg");
    m_highPriorityIcons.insert(":/Images/Icons/share.svg");
    m_highPriorityIcons.insert(":/Images/Icons/star.svg");
    m_highPriorityIcons.insert(":/Images/Icons/download.svg");
    m_highPriorityIcons.insert(":/Images/Icons/search.svg");
    m_highPriorityIcons.insert(":/Images/Icons/play.svg");
    m_highPriorityIcons.insert(":/Images/Icons/install.svg");
    m_highPriorityIcons.insert(":/Images/Icons/delete.svg");
    m_highPriorityIcons.insert(":/Images/Icons/copy.svg");

    // 为高优先级资源设置优先级映射
    for (const QString& path : m_highPriorityIcons) {
        m_iconPriorityMap[path] = ResourcePriority::High;
    }
}

ResourcePriority ResourceManager::getIconPriority(IconType type) const
{
    switch (type) {
    case IconType::Back:
    case IconType::InstanceSelect:
    case IconType::InstanceSettings:
    case IconType::ScrollToTop:
    case IconType::Refresh:
    case IconType::Share:
    case IconType::Star:
    case IconType::Download:
    case IconType::Search:
    case IconType::Play:
    case IconType::Install:
    case IconType::Delete:
    case IconType::Copy:
    case IconType::ViewAll:
    case IconType::Server:
    case IconType::Translate:
        return ResourcePriority::High;
    default:
        return ResourcePriority::Medium;
    }
}

ResourcePriority ResourceManager::getIconPriority(const QString& path) const
{
    // 先检查映射表
    if (m_iconPriorityMap.contains(path)) {
        return m_iconPriorityMap[path];
    }

    // 默认返回中优先级
    return ResourcePriority::Medium;
}

QIcon ResourceManager::loadIcon(IconType type)
{
    QString path = getIconPath(type);
    ResourcePriority priority = getIconPriority(type);
    return loadIcon(path, priority);
}

QIcon ResourceManager::loadIcon(const QString& path, ResourcePriority priority)
{
    return loadIconInternal(path, priority);
}

QIcon ResourceManager::loadIconInternal(const QString& path, ResourcePriority priority)
{
    if (path.isEmpty()) {
        return QIcon();
    }

    // 线程安全：使用互斥锁保护缓存访问
    QMutexLocker locker(&m_mutex);

    // 检查缓存
    if (m_iconCache.contains(path)) {
        return *m_iconCache[path];
    }

    // 解锁后再从文件加载（避免在IO操作时持有锁）
    locker.unlock();

    // 从文件加载
    QIcon icon(path);

    // 重新加锁以更新缓存
    locker.relock();

    if (!icon.isNull()) {
        // 记录优先级
        m_iconPriorityMap[path] = priority;

        // 高优先级资源使用更高的缓存成本，防止被自动淘汰
        int cacheCost = (priority == ResourcePriority::High) ? 1 : 10;
        m_iconCache.insert(path, new QIcon(icon), cacheCost);

        // 更新高优先级集合
        if (priority == ResourcePriority::High) {
            m_highPriorityIcons.insert(path);
        }
    }

    return icon;
}

QIcon ResourceManager::loadThemedIcon(IconType type)
{
    QString path = getIconPath(type);
    return loadThemedIcon(path);
}

QIcon ResourceManager::loadThemedIcon(const QString& svgPath)
{
    QColor textColor(ThemeManager::instance()->currentTextColor());
    return createColoredIcon(svgPath, textColor);
}

QIcon ResourceManager::createColoredIcon(const QString& svgPath, const QColor& color)
{
    QString svgContent = readSvgContent(svgPath);
    if (svgContent.isEmpty()) {
        return QIcon(svgPath);
    }

    svgContent = replaceCurrentColor(svgContent, color);

    QSvgRenderer renderer(svgContent.toUtf8());
    if (!renderer.isValid()) {
        return QIcon(svgPath);
    }

    QPixmap pixmap(renderer.defaultSize());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();

    return QIcon(pixmap);
}

QString ResourceManager::readSvgContent(const QString& svgPath) const
{
    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString ResourceManager::replaceCurrentColor(const QString& svgContent, const QColor& color) const
{
    QString result = svgContent;
    QString colorStr = color.name();
    result.replace("currentColor", colorStr);
    return result;
}

QPixmap ResourceManager::loadPixmap(const QString& path, ResourcePriority priority)
{
    return loadPixmapInternal(path, priority);
}

QPixmap ResourceManager::loadPixmapInternal(const QString& path, ResourcePriority priority)
{
    if (path.isEmpty()) {
        return QPixmap();
    }

    // 线程安全：使用互斥锁保护缓存访问
    QMutexLocker locker(&m_mutex);

    // 检查缓存
    if (m_pixmapCache.contains(path)) {
        return *m_pixmapCache[path];
    }

    // 解锁后再从文件加载（避免在IO操作时持有锁）
    locker.unlock();

    // 从文件加载
    QPixmap pixmap(path);

    // 重新加锁以更新缓存
    locker.relock();

    if (!pixmap.isNull()) {
        // 记录优先级
        m_pixmapPriorityMap[path] = priority;

        // 高优先级资源使用更高的缓存成本，防止被自动淘汰
        int cacheCost = (priority == ResourcePriority::High) ? 1 : 100;
        m_pixmapCache.insert(path, new QPixmap(pixmap), cacheCost);

        // 更新高优先级集合
        if (priority == ResourcePriority::High) {
            m_highPriorityPixmaps.insert(path);
        }
    }

    return pixmap;
}

void ResourceManager::preloadResources()
{
    // 同步预加载资源
    preloadResourcesInternal();
}

QFuture<void> ResourceManager::preloadResourcesAsync()
{
    // 使用 QtConcurrent::run 在后台线程中执行预加载
    return QtConcurrent::run([this]() {
        preloadResourcesInternal();

        // 加载完成后，在主线程中发出信号
        QMetaObject::invokeMethod(this, [this]() {
            emit resourcesLoaded();
        }, Qt::QueuedConnection);
    });
}

bool ResourceManager::isResourcesLoaded() const
{
    QMutexLocker locker(&m_mutex);
    return m_resourcesLoaded;
}

void ResourceManager::preloadResourcesInternal()
{
    qDebug() << "[ResourceManager]" << "Preloading high-priority resources...";

    // 只预加载高优先级图标（关键资源）
    QList<IconType> highPriorityIcons = {
        IconType::Back,
        IconType::InstanceSelect,
        IconType::InstanceSettings,
        IconType::ScrollToTop,
        IconType::Refresh
    };

    for (IconType type : highPriorityIcons) {
        loadIcon(type);
    }

    // 更新加载状态
    {
        QMutexLocker locker(&m_mutex);
        m_resourcesLoaded = true;

        qDebug() << "[ResourceManager]" << "Resources preloaded:"
                 << "Icons:" << m_iconCache.size()
                 << "Pixmaps:" << m_pixmapCache.size();
    }
}

void ResourceManager::clearCache()
{
    QMutexLocker locker(&m_mutex);

    m_iconCache.clear();
    m_pixmapCache.clear();
    m_iconPriorityMap.clear();
    m_pixmapPriorityMap.clear();
    m_highPriorityIcons.clear();
    m_highPriorityPixmaps.clear();

    // 重新初始化高优先级资源列表
    locker.unlock();
    initializeHighPriorityResources();

    qDebug() << "[ResourceManager]" << "ResourceManager: All caches cleared";

    emit cacheCleared();
}

void ResourceManager::clearLowPriorityCache()
{
    QMutexLocker locker(&m_mutex);

    // 收集需要移除的低优先级资源
    QList<QString> iconsToRemove;
    QList<QString> pixmapsToRemove;

    for (auto it = m_iconPriorityMap.begin(); it != m_iconPriorityMap.end(); ++it) {
        if (it.value() == ResourcePriority::Low) {
            iconsToRemove.append(it.key());
        }
    }

    for (auto it = m_pixmapPriorityMap.begin(); it != m_pixmapPriorityMap.end(); ++it) {
        if (it.value() == ResourcePriority::Low) {
            pixmapsToRemove.append(it.key());
        }
    }

    // 移除低优先级资源
    for (const QString& path : iconsToRemove) {
        m_iconCache.remove(path);
        m_iconPriorityMap.remove(path);
    }

    for (const QString& path : pixmapsToRemove) {
        m_pixmapCache.remove(path);
        m_pixmapPriorityMap.remove(path);
    }

    qDebug() << "[ResourceManager]" << "ResourceManager: Cleared low priority cache -"
             << "Icons removed:" << iconsToRemove.size()
             << "Pixmaps removed:" << pixmapsToRemove.size();
}

void ResourceManager::clearMediumPriorityCache()
{
    QMutexLocker locker(&m_mutex);

    // 收集需要移除的中优先级资源
    QList<QString> iconsToRemove;
    QList<QString> pixmapsToRemove;

    for (auto it = m_iconPriorityMap.begin(); it != m_iconPriorityMap.end(); ++it) {
        if (it.value() == ResourcePriority::Medium) {
            iconsToRemove.append(it.key());
        }
    }

    for (auto it = m_pixmapPriorityMap.begin(); it != m_pixmapPriorityMap.end(); ++it) {
        if (it.value() == ResourcePriority::Medium) {
            pixmapsToRemove.append(it.key());
        }
    }

    // 移除中优先级资源
    for (const QString& path : iconsToRemove) {
        m_iconCache.remove(path);
        m_iconPriorityMap.remove(path);
    }

    for (const QString& path : pixmapsToRemove) {
        m_pixmapCache.remove(path);
        m_pixmapPriorityMap.remove(path);
    }

    qDebug() << "[ResourceManager]" << "ResourceManager: Cleared medium priority cache -"
             << "Icons removed:" << iconsToRemove.size()
             << "Pixmaps removed:" << pixmapsToRemove.size();
}

void ResourceManager::clearUnusedResources()
{
    QMutexLocker locker(&m_mutex);

    // 清理所有非高优先级资源
    QList<QString> iconsToRemove;
    QList<QString> pixmapsToRemove;

    for (auto it = m_iconPriorityMap.begin(); it != m_iconPriorityMap.end(); ++it) {
        if (it.value() != ResourcePriority::High) {
            iconsToRemove.append(it.key());
        }
    }

    for (auto it = m_pixmapPriorityMap.begin(); it != m_pixmapPriorityMap.end(); ++it) {
        if (it.value() != ResourcePriority::High) {
            pixmapsToRemove.append(it.key());
        }
    }

    // 移除非高优先级资源
    for (const QString& path : iconsToRemove) {
        m_iconCache.remove(path);
        m_iconPriorityMap.remove(path);
    }

    for (const QString& path : pixmapsToRemove) {
        m_pixmapCache.remove(path);
        m_pixmapPriorityMap.remove(path);
    }

    qDebug() << "[ResourceManager]" << "ResourceManager: Cleared unused resources (kept high priority) -"
             << "Icons removed:" << iconsToRemove.size()
             << "Pixmaps removed:" << pixmapsToRemove.size();
}

int ResourceManager::getIconCacheSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_iconCache.size();
}

int ResourceManager::getPixmapCacheSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_pixmapCache.size();
}

qint64 ResourceManager::getEstimatedMemoryUsage() const
{
    QMutexLocker locker(&m_mutex);

    // 估算内存使用量（粗略估计）
    // 每个图标约 10KB，每个图片约 100KB
    qint64 iconMemory = m_iconCache.size() * 10 * 1024;
    qint64 pixmapMemory = m_pixmapCache.size() * 100 * 1024;

    return iconMemory + pixmapMemory;
}

void ResourceManager::setMaxIconCacheSize(int maxItems)
{
    QMutexLocker locker(&m_mutex);
    m_iconCache.setMaxCost(maxItems);
    qDebug() << "[ResourceManager]" << "ResourceManager: Icon cache max size set to" << maxItems;
}

void ResourceManager::setMaxPixmapCacheSize(int maxItems)
{
    QMutexLocker locker(&m_mutex);
    m_pixmapCache.setMaxCost(maxItems);
    qDebug() << "[ResourceManager]" << "ResourceManager: Pixmap cache max size set to" << maxItems;
}

void ResourceManager::setLowConfigModeEnabled(bool enabled)
{
    bool oldValue = m_lowConfigModeEnabled;
    m_lowConfigModeEnabled = enabled;

    if (oldValue != enabled) {
        qDebug() << "[ResourceManager]" << "ResourceManager: Low config mode changed to" << enabled;

        // 更新缓存清理定时器
        updateCacheCleanupTimer();

        // 如果启用低配置模式，立即清理低优先级资源
        if (enabled) {
            clearLowPriorityCache();

            // 减少缓存大小限制
            setMaxIconCacheSize(50);
            setMaxPixmapCacheSize(25);
        } else {
            // 恢复正常缓存大小限制
            setMaxIconCacheSize(100);
            setMaxPixmapCacheSize(50);
        }

        emit lowConfigModeChanged(enabled);
    }
}

bool ResourceManager::isLowConfigModeEnabled() const
{
    return m_lowConfigModeEnabled;
}

void ResourceManager::updateCacheCleanupTimer()
{
    if (m_lowConfigModeEnabled) {
        // 低配置模式下，每30秒清理一次缓存
        m_cacheCleanupTimer->start(30000);
        qDebug() << "[ResourceManager]" << "ResourceManager: Cache cleanup timer started (30s interval)";
    } else {
        m_cacheCleanupTimer->stop();
        qDebug() << "[ResourceManager]" << "ResourceManager: Cache cleanup timer stopped";
    }
}

void ResourceManager::onPeriodicCacheCleanup()
{
    // 低配置模式下的定期缓存清理
    if (m_lowConfigModeEnabled) {
        qDebug() << "[ResourceManager]" << "ResourceManager: Performing periodic cache cleanup (low config mode)";
        clearLowPriorityCache();

        // 如果内存压力较大，也清理中优先级资源
        qint64 memoryUsage = getEstimatedMemoryUsage();
        if (memoryUsage > 5 * 1024 * 1024) { // 超过5MB
            qDebug() << "[ResourceManager]" << "ResourceManager: Memory pressure detected, clearing medium priority cache";
            clearMediumPriorityCache();
        }
    }
}

QString ResourceManager::getIconPath(IconType type) const
{
    switch (type) {
    case IconType::Back:
        return ":/Images/Icons/back.svg";
    case IconType::InstanceSelect:
        return ":/Images/Icons/instance_select.svg";
    case IconType::InstanceSettings:
        return ":/Images/Icons/instance_settings.svg";
    case IconType::ScrollToTop:
        return ":/Images/Icons/back_to_top.svg";
    case IconType::Refresh:
        return ":/Images/Icons/refresh.svg";
    case IconType::Share:
        return ":/Images/Icons/share.svg";
    case IconType::Star:
        return ":/Images/Icons/star.svg";
    case IconType::Download:
        return ":/Images/Icons/download.svg";
    case IconType::Search:
        return ":/Images/Icons/search.svg";
    case IconType::Play:
        return ":/Images/Icons/play.svg";
    case IconType::Install:
        return ":/Images/Icons/install.svg";
    case IconType::Delete:
        return ":/Images/Icons/delete.svg";
    case IconType::Copy:
        return ":/Images/Icons/copy.svg";
    case IconType::ViewAll:
        return ":/Images/Icons/list_view.svg";
    case IconType::Server:
        return ":/Images/Icons/server.svg";
    case IconType::Translate:
        return ":/Images/Icons/translate.svg";
    default:
        return "";
    }
}
