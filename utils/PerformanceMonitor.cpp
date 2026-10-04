/**
 * @file   PerformanceMonitor.cpp
 * @brief  性能监控类实现
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "PerformanceMonitor.h"

#include <QDebug>
#include <QMutexLocker>
#include <QtMath>
#include <QTextStream>

#include "LowConfigMode.h"

// ========== 静态成员初始化 ==========

// 预定义的测量点名称常量
const QString PerformanceMonitor::MEASUREMENT_APP_STARTUP = QStringLiteral("ApplicationStartup");
const QString PerformanceMonitor::MEASUREMENT_DATA_DIR_INIT = QStringLiteral("DataDirectoryInit");
const QString PerformanceMonitor::MEASUREMENT_RESOURCE_MANAGER_INIT = QStringLiteral("ResourceManagerInit");
const QString PerformanceMonitor::MEASUREMENT_CONFIG_LOAD = QStringLiteral("ConfigLoad");
const QString PerformanceMonitor::MEASUREMENT_PLUGIN_LOAD = QStringLiteral("PluginLoad");
const QString PerformanceMonitor::MEASUREMENT_UI_INIT = QStringLiteral("UIInit");
const QString PerformanceMonitor::MEASUREMENT_THEME_LOAD = QStringLiteral("ThemeLoad");
const QString PerformanceMonitor::MEASUREMENT_LANGUAGE_LOAD = QStringLiteral("LanguageLoad");
const QString PerformanceMonitor::MEASUREMENT_NETWORK_INIT = QStringLiteral("NetworkInit");
const QString PerformanceMonitor::MEASUREMENT_DATABASE_INIT = QStringLiteral("DatabaseInit");
const QString PerformanceMonitor::MEASUREMENT_CACHE_WARMUP = QStringLiteral("CacheWarmup");
const QString PerformanceMonitor::MEASUREMENT_FINAL_SETUP = QStringLiteral("FinalSetup");

// 单例实例和互斥锁
PerformanceMonitor* PerformanceMonitor::m_instance = nullptr;
QMutex PerformanceMonitor::m_instanceMutex;

// ========== 构造函数和析构函数 ==========

PerformanceMonitor::PerformanceMonitor(QObject *parent)
    : QObject(parent)
    , m_startupMeasuring(false)
    , m_startupCompleted(false)
    , m_lowConfigModeEnabled(false)
    , m_totalStartupThreshold(3000)  // 默认总启动时间阈值3秒
{
    // 初始化默认阈值
    initializeDefaultThresholds();
    
    // 尝试连接LowConfigMode信号（如果已初始化）
    // 注意：这里使用QueuedConnection确保线程安全
    if (LowConfigMode::instance()) {
        m_lowConfigModeEnabled = LowConfigMode::instance()->isLowConfigMode();
        connect(LowConfigMode::instance(), &LowConfigMode::lowConfigModeChanged,
                this, &PerformanceMonitor::setLowConfigModeEnabled,
                Qt::QueuedConnection);
    }
    
    qDebug() << "[PerformanceMonitor]" << "PerformanceMonitor initialized";
    qDebug() << "[PerformanceMonitor]" << "  - Low Config Mode:" << m_lowConfigModeEnabled.load();
    qDebug() << "[PerformanceMonitor]" << "  - Total Startup Threshold:" << m_totalStartupThreshold.load() << "ms";
}

PerformanceMonitor::~PerformanceMonitor()
{
    // 如果启动测量未完成，先结束
    if (m_startupMeasuring.load()) {
        endStartupMeasurement();
    }
    
    // 打印启动性能报告
    if (m_startupCompleted.load()) {
        printStartupReport();
        checkPerformanceThresholds();
    }
    
    // 打印所有测量结果
    printMeasurements();
}

// ========== 单例获取 ==========

PerformanceMonitor* PerformanceMonitor::instance()
{
    // 双检锁模式实现线程安全的单例
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new PerformanceMonitor();
        }
    }
    return m_instance;
}

// ========== 基本测量方法 ==========

void PerformanceMonitor::startMeasurement(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    
    // 开始测量
    QElapsedTimer timer;
    timer.start();
    m_timers[name] = timer;
    
    // 如果正在测量启动阶段，记录阶段顺序
    if (m_startupMeasuring.load() && !m_startupPhaseOrder.contains(name)) {
        m_startupPhaseOrder.append(name);
    }
    
    qDebug() << "[PerformanceMonitor] Started measurement:" << name;
}

qint64 PerformanceMonitor::endMeasurement(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    
    // 结束测量
    if (m_timers.contains(name)) {
        QElapsedTimer timer = m_timers[name];
        qint64 elapsed = timer.elapsed();
        m_measurements[name] = elapsed;
        m_timers.remove(name);
        
        qDebug() << "[PerformanceMonitor] Measurement completed -" << name << ":" << elapsed << "ms";
        
        return elapsed;
    }
    
    qWarning() << "[PerformanceMonitor] Measurement not found:" << name;
    return -1;
}

qint64 PerformanceMonitor::getMeasurement(const QString& name) const
{
    QMutexLocker locker(&m_mutex);
    return m_measurements.value(name, -1);
}

QMap<QString, qint64> PerformanceMonitor::getAllMeasurements() const
{
    QMutexLocker locker(&m_mutex);
    return m_measurements;
}

void PerformanceMonitor::clearMeasurements()
{
    QMutexLocker locker(&m_mutex);
    m_timers.clear();
    m_measurements.clear();
    m_startupPhaseOrder.clear();
    m_startupMeasuring = false;
    m_startupCompleted = false;
}

void PerformanceMonitor::printMeasurements() const
{
    QMutexLocker locker(&m_mutex);
    
    if (m_measurements.isEmpty()) {
        qDebug() << "[PerformanceMonitor]" << "\n===== No Performance Measurements =====\n";
        return;
    }
    
    qDebug() << "[PerformanceMonitor]" << "\n===== Performance Measurements =====";
    for (auto it = m_measurements.constBegin(); it != m_measurements.constEnd(); ++it) {
        qDebug() << "[PerformanceMonitor]" << QString("  %1: %2 ms")
                    .arg(it.key(), -30)
                    .arg(it.value());
    }
    qDebug() << "[PerformanceMonitor]" << "====================================\n";
}

// ========== 启动性能测量便捷方法 ==========

void PerformanceMonitor::startStartupMeasurement()
{
    QMutexLocker locker(&m_mutex);
    
    // 标记启动测量开始
    m_startupMeasuring = true;
    m_startupCompleted = false;
    m_startupStartTime = QDateTime::currentDateTime();
    m_startupPhaseOrder.clear();
    
    // 开始总启动时间测量
    QElapsedTimer timer;
    timer.start();
    m_timers[MEASUREMENT_APP_STARTUP] = timer;
    
    qDebug() << "[PerformanceMonitor]" << "\n========================================";
    qDebug() << "[PerformanceMonitor] Startup measurement started at" 
             << m_startupStartTime.toString("yyyy-MM-dd hh:mm:ss.zzz");
    qDebug() << "[PerformanceMonitor]" << "========================================\n";
}

void PerformanceMonitor::endStartupMeasurement()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_startupMeasuring.load()) {
        qWarning() << "[PerformanceMonitor] No startup measurement in progress";
        return;
    }
    
    // 结束总启动时间测量
    if (m_timers.contains(MEASUREMENT_APP_STARTUP)) {
        QElapsedTimer timer = m_timers[MEASUREMENT_APP_STARTUP];
        qint64 elapsed = timer.elapsed();
        m_measurements[MEASUREMENT_APP_STARTUP] = elapsed;
        m_timers.remove(MEASUREMENT_APP_STARTUP);
        
        // 标记启动测量完成
        m_startupMeasuring = false;
        m_startupCompleted = true;
        
        qDebug() << "[PerformanceMonitor]" << "\n========================================";
        qDebug() << "[PerformanceMonitor] Startup measurement completed";
        qDebug() << "[PerformanceMonitor]" << "  Total startup time:" << elapsed << "ms";
        qDebug() << "[PerformanceMonitor]" << "  Phases measured:" << m_startupPhaseOrder.size();
        qDebug() << "[PerformanceMonitor]" << "========================================\n";
        
        // 发送信号
        emit startupMeasurementCompleted(elapsed);
    }
}

bool PerformanceMonitor::isStartupMeasuring() const
{
    return m_startupMeasuring.load();
}

qint64 PerformanceMonitor::getTotalStartupTime() const
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_startupCompleted.load()) {
        return -1;
    }
    
    return m_measurements.value(MEASUREMENT_APP_STARTUP, -1);
}

QStringList PerformanceMonitor::getStartupPhases() const
{
    QMutexLocker locker(&m_mutex);
    return m_startupPhaseOrder;
}

// ========== 启动性能报告 ==========

QString PerformanceMonitor::generateStartupReport() const
{
    QMutexLocker locker(&m_mutex);
    
    QString report;
    QTextStream stream(&report);
    
    // 获取阈值倍数（原子操作，不需要锁）
    double multiplier = getThresholdMultiplier();
    
    stream << "\n";
    stream << "╔══════════════════════════════════════════════════════════════╗\n";
    stream << "║              启动性能报告 (Startup Performance Report)              ║\n";
    stream << "╠══════════════════════════════════════════════════════════════╣\n";
    
    // 总启动时间
    qint64 totalTime = m_measurements.value(MEASUREMENT_APP_STARTUP, 0);
    stream << QString("║ 总启动时间: %1\n").arg(formatTime(totalTime));
    stream << QString("║ 启动开始时间: %1\n").arg(m_startupStartTime.toString("yyyy-MM-dd hh:mm:ss.zzz"));
    stream << "╠══════════════════════════════════════════════════════════════╣\n";
    
    // 各阶段详情
    stream << "║ 各阶段耗时详情:\n";
    stream << "╠══════════════════════════════════════════════════════════════╣\n";
    
    // 按顺序显示各阶段
    int phaseNumber = 1;
    for (const QString& phase : m_startupPhaseOrder) {
        if (phase == MEASUREMENT_APP_STARTUP) {
            continue;  // 跳过总启动时间
        }
        
        qint64 phaseTime = m_measurements.value(phase, 0);
        QString percentage = calculatePercentage(phaseTime, totalTime);
        QString thresholdStr;
        
        // 检查是否超过阈值（直接访问内部数据，避免死锁）
        qint64 threshold = m_phaseThresholds.value(phase, 500);
        qint64 adjustedThreshold = static_cast<qint64>(threshold * multiplier);
        
        if (phaseTime > adjustedThreshold && adjustedThreshold > 0) {
            thresholdStr = QString(" [警告: 超过阈值 %1 ms]").arg(adjustedThreshold);
        }
        
        stream << QString("║ %1. %2\n")
                  .arg(phaseNumber, 2)
                  .arg(phase);
        stream << QString("║    耗时: %1 (%2)%3\n")
                  .arg(formatTime(phaseTime))
                  .arg(percentage)
                  .arg(thresholdStr);
        
        phaseNumber++;
    }
    
    stream << "╠══════════════════════════════════════════════════════════════╣\n";
    
    // 性能警告摘要（内部计算，避免死锁）
    QStringList warnings;
    
    // 检查总启动时间
    qint64 totalThreshold = static_cast<qint64>(m_totalStartupThreshold.load() * multiplier);
    if (totalTime > totalThreshold) {
        warnings << QString("总启动时间 %1 ms 超过阈值 %2 ms")
                    .arg(totalTime)
                    .arg(totalThreshold);
    }
    
    // 检查各阶段
    for (const QString& phase : m_startupPhaseOrder) {
        if (phase == MEASUREMENT_APP_STARTUP) {
            continue;
        }
        
        qint64 phaseTime = m_measurements.value(phase, 0);
        qint64 phaseThreshold = static_cast<qint64>(m_phaseThresholds.value(phase, 500) * multiplier);
        
        if (phaseTime > phaseThreshold && phaseThreshold > 0) {
            warnings << QString("%1: %2 ms 超过阈值 %3 ms")
                        .arg(phase)
                        .arg(phaseTime)
                        .arg(phaseThreshold);
        }
    }
    
    if (!warnings.isEmpty()) {
        stream << "║ 性能警告:\n";
        for (const QString& warning : warnings) {
            stream << QString("║   - %1\n").arg(warning);
        }
        stream << "╠══════════════════════════════════════════════════════════════╣\n";
    }
    
    // 系统信息
    stream << "║ 系统信息:\n";
    stream << QString("║   低配置模式: %1\n").arg(m_lowConfigModeEnabled.load() ? "已启用" : "已禁用");
    stream << QString("║   阈值倍数: %1x\n").arg(multiplier);
    
    stream << "╚══════════════════════════════════════════════════════════════╝\n";
    
    stream.flush();
    return report;
}

void PerformanceMonitor::printStartupReport() const
{
    QString report = generateStartupReport();
    qDebug().noquote() << "[PerformanceMonitor]" << report;
}

// ========== 性能阈值检查 ==========

bool PerformanceMonitor::checkPerformanceThresholds()
{
    QMutexLocker locker(&m_mutex);
    
    bool allWithinThreshold = true;
    double multiplier = getThresholdMultiplier();
    
    // 检查总启动时间
    qint64 totalTime = m_measurements.value(MEASUREMENT_APP_STARTUP, 0);
    qint64 totalThreshold = static_cast<qint64>(m_totalStartupThreshold.load() * multiplier);
    
    if (totalTime > totalThreshold) {
        allWithinThreshold = false;
        qWarning() << QString("[PerformanceMonitor] WARNING: Total startup time (%1 ms) exceeds threshold (%2 ms)")
                      .arg(totalTime)
                      .arg(totalThreshold);
        
        emit performanceWarningEmitted(MEASUREMENT_APP_STARTUP, totalTime, totalThreshold);
    }
    
    // 检查各阶段（直接访问内部数据，避免死锁）
    for (const QString& phase : m_startupPhaseOrder) {
        if (phase == MEASUREMENT_APP_STARTUP) {
            continue;
        }
        
        qint64 phaseTime = m_measurements.value(phase, 0);
        qint64 phaseThreshold = static_cast<qint64>(m_phaseThresholds.value(phase, 500) * multiplier);
        
        if (phaseTime > phaseThreshold && phaseThreshold > 0) {
            allWithinThreshold = false;
            qWarning() << QString("[PerformanceMonitor] WARNING: Phase '%1' (%2 ms) exceeds threshold (%3 ms)")
                          .arg(phase)
                          .arg(phaseTime)
                          .arg(phaseThreshold);
            
            emit performanceWarningEmitted(phase, phaseTime, phaseThreshold);
        }
    }
    
    return allWithinThreshold;
}

QStringList PerformanceMonitor::getPerformanceWarnings() const
{
    QMutexLocker locker(&m_mutex);
    
    QStringList warnings;
    double multiplier = getThresholdMultiplier();
    
    // 检查总启动时间
    qint64 totalTime = m_measurements.value(MEASUREMENT_APP_STARTUP, 0);
    qint64 totalThreshold = static_cast<qint64>(m_totalStartupThreshold.load() * multiplier);
    
    if (totalTime > totalThreshold) {
        warnings << QString("总启动时间 %1 ms 超过阈值 %2 ms")
                    .arg(totalTime)
                    .arg(totalThreshold);
    }
    
    // 检查各阶段（直接访问内部数据，避免死锁）
    for (const QString& phase : m_startupPhaseOrder) {
        if (phase == MEASUREMENT_APP_STARTUP) {
            continue;
        }
        
        qint64 phaseTime = m_measurements.value(phase, 0);
        qint64 phaseThreshold = static_cast<qint64>(m_phaseThresholds.value(phase, 500) * multiplier);
        
        if (phaseTime > phaseThreshold && phaseThreshold > 0) {
            warnings << QString("%1: %2 ms 超过阈值 %3 ms")
                        .arg(phase)
                        .arg(phaseTime)
                        .arg(phaseThreshold);
        }
    }
    
    return warnings;
}

// ========== 低配置模式集成 ==========

void PerformanceMonitor::setLowConfigModeEnabled(bool enabled)
{
    bool oldValue = m_lowConfigModeEnabled.exchange(enabled);
    
    if (oldValue != enabled) {
        qDebug() << "[PerformanceMonitor] Low config mode changed to:" << enabled;
        qDebug() << "[PerformanceMonitor]" << "  - Threshold multiplier:" << getThresholdMultiplier();
        
        emit lowConfigModeChanged(enabled);
    }
}

bool PerformanceMonitor::isLowConfigModeEnabled() const
{
    return m_lowConfigModeEnabled.load();
}

double PerformanceMonitor::getThresholdMultiplier() const
{
    return m_lowConfigModeEnabled.load() ? LOW_CONFIG_THRESHOLD_MULTIPLIER : 1.0;
}

// ========== 阈值配置 ==========

void PerformanceMonitor::setTotalStartupThreshold(qint64 thresholdMs)
{
    m_totalStartupThreshold = thresholdMs;
    qDebug() << "[PerformanceMonitor] Total startup threshold set to:" << thresholdMs << "ms";
}

qint64 PerformanceMonitor::getTotalStartupThreshold() const
{
    return m_totalStartupThreshold.load();
}

void PerformanceMonitor::setPhaseThreshold(const QString& phaseName, qint64 thresholdMs)
{
    QMutexLocker locker(&m_mutex);
    m_phaseThresholds[phaseName] = thresholdMs;
    qDebug() << "[PerformanceMonitor] Phase threshold set:" << phaseName << "->" << thresholdMs << "ms";
}

qint64 PerformanceMonitor::getPhaseThreshold(const QString& phaseName) const
{
    QMutexLocker locker(&m_mutex);
    return m_phaseThresholds.value(phaseName, 500);  // 默认阈值500ms
}

// ========== 私有方法 ==========

void PerformanceMonitor::initializeDefaultThresholds()
{
    // 设置各阶段的默认阈值（毫秒）
    m_phaseThresholds[MEASUREMENT_DATA_DIR_INIT] = 200;
    m_phaseThresholds[MEASUREMENT_RESOURCE_MANAGER_INIT] = 500;
    m_phaseThresholds[MEASUREMENT_CONFIG_LOAD] = 300;
    m_phaseThresholds[MEASUREMENT_PLUGIN_LOAD] = 800;
    m_phaseThresholds[MEASUREMENT_UI_INIT] = 500;
    m_phaseThresholds[MEASUREMENT_THEME_LOAD] = 200;
    m_phaseThresholds[MEASUREMENT_LANGUAGE_LOAD] = 100;
    m_phaseThresholds[MEASUREMENT_NETWORK_INIT] = 300;
    m_phaseThresholds[MEASUREMENT_DATABASE_INIT] = 400;
    m_phaseThresholds[MEASUREMENT_CACHE_WARMUP] = 300;
    m_phaseThresholds[MEASUREMENT_FINAL_SETUP] = 200;
}

QString PerformanceMonitor::formatTime(qint64 milliseconds) const
{
    if (milliseconds < 1000) {
        return QString("%1 ms").arg(milliseconds);
    } else if (milliseconds < 60000) {
        double seconds = milliseconds / 1000.0;
        return QString("%1 s (%2 ms)").arg(seconds, 0, 'f', 2).arg(milliseconds);
    } else {
        int minutes = milliseconds / 60000;
        int seconds = (milliseconds % 60000) / 1000;
        return QString("%1 m %2 s (%3 ms)").arg(minutes).arg(seconds).arg(milliseconds);
    }
}

QString PerformanceMonitor::calculatePercentage(qint64 partTime, qint64 totalTime) const
{
    if (totalTime <= 0) {
        return "0.00%";
    }
    
    double percentage = (static_cast<double>(partTime) / static_cast<double>(totalTime)) * 100.0;
    return QString("%1%").arg(percentage, 0, 'f', 2);
}
