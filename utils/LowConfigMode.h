/**
 * @file   LowConfigMode.h
 * @brief  低配置模式检测与优化类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef LOWCONFIGMODE_H
#define LOWCONFIGMODE_H

#include <atomic>

#include <QMutex>
#include <QObject>
#include <QSettings>
#include <QSysInfo>

/**
 * @brief 低配置模式管理类
 *
 * 用于检测设备配置并管理低配置模式。
 * 当系统内存小于4GB或CPU核心数少于2核时自动启用低配置模式。
 * 支持用户手动启用/禁用低配置模式。
 * 线程安全的单例模式实现。
 */
class LowConfigMode : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     * @return LowConfigMode单例指针
     */
    static LowConfigMode* instance();

    /**
     * @brief 检测系统总内存大小
     * @return 系统总内存大小（字节）
     */
    qint64 getTotalSystemMemory() const;

    /**
     * @brief 获取系统内存大小（GB）
     * @return 系统内存大小（GB）
     */
    double getTotalMemoryGB() const;

    /**
     * @brief 获取CPU核心数
     * @return CPU逻辑核心数
     */
    int getCpuCoreCount() const;

    /**
     * @brief 检查当前是否处于低配置模式
     * @return true表示处于低配置模式
     */
    bool isLowConfigMode() const;

    /**
     * @brief 启用低配置模式
     */
    void enableLowConfigMode();

    /**
     * @brief 禁用低配置模式
     */
    void disableLowConfigMode();

    /**
     * @brief 设置低配置模式状态
     * @param enabled 是否启用
     */
    void setLowConfigMode(bool enabled);

    /**
     * @brief 检查设备是否为低配置设备（自动检测）
     * @return true表示设备被判定为低配置设备
     */
    bool isLowConfigDevice() const;

    /**
     * @brief 获取低配置模式是否为自动检测模式
     * @return true表示使用自动检测
     */
    bool isAutoDetectEnabled() const;

    /**
     * @brief 设置是否启用自动检测
     * @param enabled 是否启用自动检测
     */
    void setAutoDetectEnabled(bool enabled);

    /**
     * @brief 获取低配置阈值：内存阈值（GB）
     * @return 内存阈值（GB），低于此值视为低配置
     */
    double getMemoryThresholdGB() const;

    /**
     * @brief 设置内存阈值
     * @param thresholdGB 内存阈值（GB）
     */
    void setMemoryThresholdGB(double thresholdGB);

    /**
     * @brief 获取CPU核心数阈值
     * @return CPU核心数阈值，低于此值视为低配置
     */
    int getCpuCoreThreshold() const;

    /**
     * @brief 设置CPU核心数阈值
     * @param threshold CPU核心数阈值
     */
    void setCpuCoreThreshold(int threshold);

    /**
     * @brief 获取系统配置信息描述
     * @return 系统配置信息字符串
     */
    QString getSystemConfigInfo() const;

    /**
     * @brief 检查是否启用动画效果
     * @return true表示启用动画，false表示禁用动画
     *
     * 在低配置模式下返回false以减少系统资源消耗
     */
    bool areAnimationsEnabled() const;

signals:
    /**
     * @brief 低配置模式状态改变信号
     * @param enabled 新的状态
     */
    void lowConfigModeChanged(bool enabled);

    /**
     * @brief 自动检测设置改变信号
     * @param enabled 新的状态
     */
    void autoDetectChanged(bool enabled);

    /**
     * @brief 动画启用状态改变信号
     * @param enabled 新的状态
     */
    void animationsEnabledChanged(bool enabled);

private:
    explicit LowConfigMode(QObject *parent = nullptr);
    ~LowConfigMode();

    // 禁止拷贝和赋值
    LowConfigMode(const LowConfigMode&) = delete;
    LowConfigMode& operator=(const LowConfigMode&) = delete;

    /**
     * @brief 检测系统内存（内部实现）
     * @return 系统总内存（字节）
     */
    qint64 detectTotalSystemMemory() const;

    /**
     * @brief 保存设置到配置文件
     */
    void saveSettings();

    /**
     * @brief 从配置文件加载设置
     */
    void loadSettings();

    /**
     * @brief 更新低配置模式状态（根据自动检测或手动设置）
     */
    void updateLowConfigModeState();

    // 静态单例实例
    static LowConfigMode* m_instance;
    static QMutex m_mutex;

    // 配置数据
    std::atomic<bool> m_lowConfigModeEnabled;
    std::atomic<bool> m_autoDetectEnabled;
    std::atomic<double> m_memoryThresholdGB;
    std::atomic<int> m_cpuCoreThreshold;

    // 缓存的系统信息
    mutable qint64 m_cachedTotalMemory;
    mutable int m_cachedCpuCores;

    // 设置存储
    QSettings *m_settings;
};

#endif // LOWCONFIGMODE_H
