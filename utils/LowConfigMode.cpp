/**
 * @file   LowConfigMode.cpp
 * @brief  低配置模式检测与优化实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "LowConfigMode.h"

#include <cmath>

#include <QDebug>
#include <QtGlobal>
#include <QThread>
#include <QFile>
#include <QIODevice>
#include <QTextStream>
#include <QRegularExpression>
#include <QStringList>

// 静态成员初始化
LowConfigMode* LowConfigMode::m_instance = nullptr;
QMutex LowConfigMode::m_mutex;

// 跨平台内存检测
#if defined(Q_OS_WIN)
    #include <windows.h>
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    #include <sys/sysinfo.h>
    #include <unistd.h>
#elif defined(Q_OS_MACOS)
    #include <sys/sysctl.h>
    #include <mach/mach.h>
    #include <mach/vm_statistics.h>
#endif

LowConfigMode::LowConfigMode(QObject *parent)
    : QObject(parent)
    , m_lowConfigModeEnabled(false)
    , m_autoDetectEnabled(true)
    , m_memoryThresholdGB(4.0)
    , m_cpuCoreThreshold(2)
    , m_cachedTotalMemory(0)
    , m_cachedCpuCores(0)
    , m_settings(nullptr)
{
    // 初始化设置存储
    m_settings = new QSettings("BlockBox", "BlockBox");

    // 检测系统信息（只检测一次并缓存）
    m_cachedTotalMemory = detectTotalSystemMemory();
    m_cachedCpuCores = QThread::idealThreadCount();

    // 加载保存的设置
    loadSettings();

    // 根据设置更新低配置模式状态
    updateLowConfigModeState();

    qDebug() << "[LowConfigMode]" << "LowConfigMode initialized:";
    qDebug() << "[LowConfigMode]" << "  - Total Memory:" << getTotalMemoryGB() << "GB";
    qDebug() << "[LowConfigMode]" << "  - CPU Cores:" << m_cachedCpuCores;
    qDebug() << "[LowConfigMode]" << "  - Is Low Config Device:" << isLowConfigDevice();
    qDebug() << "[LowConfigMode]" << "  - Low Config Mode:" << m_lowConfigModeEnabled.load();
}

LowConfigMode::~LowConfigMode()
{
    saveSettings();
    delete m_settings;
    m_instance = nullptr;
}

LowConfigMode* LowConfigMode::instance()
{
    // 双检锁模式实现线程安全的单例
    if (!m_instance) {
        QMutexLocker locker(&m_mutex);
        if (!m_instance) {
            m_instance = new LowConfigMode();
        }
    }
    return m_instance;
}

qint64 LowConfigMode::detectTotalSystemMemory() const
{
    qint64 totalMemory = 0;

#if defined(Q_OS_WIN)
    // Windows平台：使用GlobalMemoryStatusEx
    MEMORYSTATUSEX memoryStatus;
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus)) {
        totalMemory = static_cast<qint64>(memoryStatus.ullTotalPhys);
    }
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // Linux/Android平台：优先使用/proc/meminfo（更可靠）
    QFile memFile("/proc/meminfo");
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&memFile);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            if (line.startsWith("MemTotal:")) {
                memFile.close();
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    // Value is in kB, convert to bytes
                    totalMemory = parts[1].toULongLong() * 1024;
                    return totalMemory;
                }
            }
        }
        memFile.close();
    }
    // Fallback to sysinfo
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        totalMemory = static_cast<qint64>(info.totalram) * info.mem_unit;
    }
#elif defined(Q_OS_MACOS)
    // macOS平台：使用sysctl
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    int64_t memSize = 0;
    size_t length = sizeof(memSize);
    if (sysctl(mib, 2, &memSize, &length, nullptr, 0) == 0) {
        totalMemory = memSize;
    }
#else
    // 其他平台：尝试从环境变量或默认值
    qWarning() << "[LowConfigMode]" << "LowConfigMode: Unknown platform, using default memory detection";
    totalMemory = 8LL * 1024 * 1024 * 1024; // 默认假设8GB
#endif

    return totalMemory;
}

qint64 LowConfigMode::getTotalSystemMemory() const
{
    return m_cachedTotalMemory;
}

double LowConfigMode::getTotalMemoryGB() const
{
    const qint64 bytesPerGB = 1024LL * 1024LL * 1024LL;
    return static_cast<double>(m_cachedTotalMemory) / static_cast<double>(bytesPerGB);
}

int LowConfigMode::getCpuCoreCount() const
{
    return m_cachedCpuCores;
}

bool LowConfigMode::isLowConfigMode() const
{
    return m_lowConfigModeEnabled.load();
}

void LowConfigMode::enableLowConfigMode()
{
    setLowConfigMode(true);
}

void LowConfigMode::disableLowConfigMode()
{
    setLowConfigMode(false);
}

void LowConfigMode::setLowConfigMode(bool enabled)
{
    bool oldValue = m_lowConfigModeEnabled.exchange(enabled);

    if (oldValue != enabled) {
        // 保存设置
        saveSettings();

        qDebug() << "[LowConfigMode]" << "LowConfigMode changed to:" << enabled;

        // 发送信号
        emit lowConfigModeChanged(enabled);
    }
}

bool LowConfigMode::isLowConfigDevice() const
{
    double memoryGB = getTotalMemoryGB();
    int cpuCores = m_cachedCpuCores;

    // 内存小于阈值或CPU核心数少于阈值
    bool isLowMemory = memoryGB < m_memoryThresholdGB.load();
    bool isLowCpuCores = cpuCores < m_cpuCoreThreshold.load();

    return isLowMemory || isLowCpuCores;
}

bool LowConfigMode::isAutoDetectEnabled() const
{
    return m_autoDetectEnabled.load();
}

void LowConfigMode::setAutoDetectEnabled(bool enabled)
{
    bool oldValue = m_autoDetectEnabled.exchange(enabled);

    if (oldValue != enabled) {
        saveSettings();
        updateLowConfigModeState();

        qDebug() << "[LowConfigMode]" << "AutoDetect changed to:" << enabled;

        emit autoDetectChanged(enabled);
    }
}

double LowConfigMode::getMemoryThresholdGB() const
{
    return m_memoryThresholdGB.load();
}

void LowConfigMode::setMemoryThresholdGB(double thresholdGB)
{
    double oldValue = m_memoryThresholdGB.exchange(thresholdGB);

    if (qAbs(oldValue - thresholdGB) > 0.001) {
        saveSettings();
        updateLowConfigModeState();

        qDebug() << "[LowConfigMode]" << "Memory threshold changed to:" << thresholdGB << "GB";
    }
}

int LowConfigMode::getCpuCoreThreshold() const
{
    return m_cpuCoreThreshold.load();
}

void LowConfigMode::setCpuCoreThreshold(int threshold)
{
    int oldValue = m_cpuCoreThreshold.exchange(threshold);

    if (oldValue != threshold) {
        saveSettings();
        updateLowConfigModeState();

        qDebug() << "[LowConfigMode]" << "CPU core threshold changed to:" << threshold;
    }
}

QString LowConfigMode::getSystemConfigInfo() const
{
    QString info;

    info += tr("系统配置信息:\n");
    info += tr("  操作系统: %1\n").arg(QSysInfo::prettyProductName());
    info += tr("  CPU架构: %1\n").arg(QSysInfo::currentCpuArchitecture());
    info += tr("  CPU核心数: %1\n").arg(m_cachedCpuCores);
    info += tr("  总内存: %1 GB\n").arg(getTotalMemoryGB(), 0, 'f', 2);
    info += tr("  低配置设备: %1\n").arg(isLowConfigDevice() ? tr("是") : tr("否"));
    info += tr("  低配置模式: %1\n").arg(m_lowConfigModeEnabled.load() ? tr("已启用") : tr("已禁用"));
    info += tr("  自动检测: %1\n").arg(m_autoDetectEnabled.load() ? tr("已启用") : tr("已禁用"));
    info += tr("  内存阈值: %1 GB\n").arg(m_memoryThresholdGB.load());
    info += tr("  CPU核心阈值: %1\n").arg(m_cpuCoreThreshold.load());

    return info;
}

void LowConfigMode::saveSettings()
{
    if (!m_settings) {
        return;
    }

    m_settings->beginGroup("LowConfigMode");
    m_settings->setValue("enabled", m_lowConfigModeEnabled.load());
    m_settings->setValue("autoDetect", m_autoDetectEnabled.load());
    m_settings->setValue("memoryThresholdGB", m_memoryThresholdGB.load());
    m_settings->setValue("cpuCoreThreshold", m_cpuCoreThreshold.load());
    m_settings->endGroup();

    m_settings->sync();
}

void LowConfigMode::loadSettings()
{
    if (!m_settings) {
        return;
    }

    m_settings->beginGroup("LowConfigMode");

    // 加载自动检测设置，默认启用
    m_autoDetectEnabled = m_settings->value("autoDetect", true).toBool();

    // 加载阈值设置
    m_memoryThresholdGB = m_settings->value("memoryThresholdGB", 4.0).toDouble();
    m_cpuCoreThreshold = m_settings->value("cpuCoreThreshold", 2).toInt();

    // 加载手动设置的低配置模式状态
    // 注意：如果启用了自动检测，这个值会被updateLowConfigModeState()覆盖
    m_lowConfigModeEnabled = m_settings->value("enabled", false).toBool();

    m_settings->endGroup();
}

void LowConfigMode::updateLowConfigModeState()
{
    // 如果启用了自动检测，则根据设备配置自动设置
    if (m_autoDetectEnabled.load()) {
        bool shouldBeLowConfig = isLowConfigDevice();
        bool currentState = m_lowConfigModeEnabled.load();

        if (currentState != shouldBeLowConfig) {
            m_lowConfigModeEnabled = shouldBeLowConfig;
            qDebug() << "[LowConfigMode]" << "Auto-detect: LowConfigMode set to" << shouldBeLowConfig;
            emit lowConfigModeChanged(shouldBeLowConfig);
        }
    }
}

bool LowConfigMode::areAnimationsEnabled() const
{
    // 在低配置模式下禁用动画以减少系统资源消耗
    return !m_lowConfigModeEnabled.load();
}
