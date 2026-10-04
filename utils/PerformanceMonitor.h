/**
 * @file   PerformanceMonitor.h
 * @brief  性能监控类定义
 * @author BlockBox Team
 * @date   2026-05-09
 */

#pragma once

#include <atomic>

#include <QDateTime>
#include <QElapsedTimer>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief 性能监控类
 *
 * 用于测量和监控应用程序性能，特别是启动阶段的性能。
 * 线程安全的单例模式实现。
 */
class PerformanceMonitor : public QObject
{
    Q_OBJECT

public:
    // ========== 预定义的启动测量点名称常量 ==========
    
    // 应用程序启动相关
    static const QString MEASUREMENT_APP_STARTUP;
    static const QString MEASUREMENT_DATA_DIR_INIT;
    static const QString MEASUREMENT_RESOURCE_MANAGER_INIT;
    static const QString MEASUREMENT_CONFIG_LOAD;
    static const QString MEASUREMENT_PLUGIN_LOAD;
    static const QString MEASUREMENT_UI_INIT;
    static const QString MEASUREMENT_THEME_LOAD;
    static const QString MEASUREMENT_LANGUAGE_LOAD;
    static const QString MEASUREMENT_NETWORK_INIT;
    static const QString MEASUREMENT_DATABASE_INIT;
    static const QString MEASUREMENT_CACHE_WARMUP;
    static const QString MEASUREMENT_FINAL_SETUP;

    /**
     * @brief 获取单例实例
     * @return PerformanceMonitor单例指针
     */
    static PerformanceMonitor* instance();
    
    ~PerformanceMonitor();

    // ========== 基本测量方法 ==========

    /**
     * @brief 开始测量
     * @param name 测量点名称
     */
    void startMeasurement(const QString& name);

    /**
     * @brief 结束测量并返回时间（毫秒）
     * @param name 测量点名称
     * @return 测量时间（毫秒），如果测量点不存在返回-1
     */
    qint64 endMeasurement(const QString& name);

    /**
     * @brief 获取指定测量点的结果
     * @param name 测量点名称
     * @return 测量时间（毫秒），如果不存在返回-1
     */
    qint64 getMeasurement(const QString& name) const;

    /**
     * @brief 获取所有测量结果
     * @return 测量结果映射表
     */
    QMap<QString, qint64> getAllMeasurements() const;

    /**
     * @brief 清除所有测量结果
     */
    void clearMeasurements();

    /**
     * @brief 打印所有测量结果
     */
    void printMeasurements() const;

    // ========== 启动性能测量便捷方法 ==========

    /**
     * @brief 开始启动阶段测量
     * 
     * 这是一个便捷方法，用于标记启动阶段的开始。
     * 会自动记录启动开始时间戳。
     */
    void startStartupMeasurement();

    /**
     * @brief 结束启动阶段测量
     * 
     * 这是一个便捷方法，用于标记启动阶段的结束。
     * 会自动计算总启动时间并记录。
     */
    void endStartupMeasurement();

    /**
     * @brief 检查是否正在测量启动阶段
     * @return true表示正在测量启动阶段
     */
    bool isStartupMeasuring() const;

    /**
     * @brief 获取总启动时间
     * @return 总启动时间（毫秒），如果未完成启动测量返回-1
     */
    qint64 getTotalStartupTime() const;

    /**
     * @brief 获取启动阶段列表（按测量顺序）
     * @return 启动阶段名称列表
     */
    QStringList getStartupPhases() const;

    // ========== 启动性能报告 ==========

    /**
     * @brief 生成启动性能报告
     * @return 格式化的启动性能报告字符串
     */
    QString generateStartupReport() const;

    /**
     * @brief 打印启动性能报告到调试输出
     */
    void printStartupReport() const;

    // ========== 性能阈值检查 ==========

    /**
     * @brief 检查性能阈值
     * 
     * 检查各阶段的性能是否超过阈值，超过则输出警告。
     * @return true表示所有阶段都在阈值范围内
     */
    bool checkPerformanceThresholds();

    /**
     * @brief 获取性能警告列表
     * @return 性能警告消息列表
     */
    QStringList getPerformanceWarnings() const;

    // ========== 低配置模式集成 ==========

    /**
     * @brief 设置低配置模式是否启用
     * @param enabled 是否启用低配置模式
     */
    void setLowConfigModeEnabled(bool enabled);

    /**
     * @brief 检查低配置模式是否启用
     * @return true表示低配置模式已启用
     */
    bool isLowConfigModeEnabled() const;

    /**
     * @brief 获取当前使用的阈值倍数
     * 
     * 低配置模式下阈值会放宽（默认1.5倍）
     * @return 阈值倍数
     */
    double getThresholdMultiplier() const;

    // ========== 阈值配置 ==========

    /**
     * @brief 设置总启动时间警告阈值（毫秒）
     * @param thresholdMs 阈值（毫秒）
     */
    void setTotalStartupThreshold(qint64 thresholdMs);

    /**
     * @brief 获取总启动时间警告阈值
     * @return 阈值（毫秒）
     */
    qint64 getTotalStartupThreshold() const;

    /**
     * @brief 设置阶段启动时间警告阈值
     * @param phaseName 阶段名称
     * @param thresholdMs 阈值（毫秒）
     */
    void setPhaseThreshold(const QString& phaseName, qint64 thresholdMs);

    /**
     * @brief 获取阶段启动时间警告阈值
     * @param phaseName 阶段名称
     * @return 阈值（毫秒），如果未设置返回默认值
     */
    qint64 getPhaseThreshold(const QString& phaseName) const;

signals:
    /**
     * @brief 启动测量完成信号
     * @param totalTimeMs 总启动时间（毫秒）
     */
    void startupMeasurementCompleted(qint64 totalTimeMs);

    /**
     * @brief 性能警告信号
     * @param phaseName 阶段名称
     * @param actualTime 实际时间（毫秒）
     * @param threshold 阈值（毫秒）
     */
    void performanceWarningEmitted(const QString& phaseName, qint64 actualTime, qint64 threshold);

    /**
     * @brief 低配置模式改变信号
     * @param enabled 是否启用
     */
    void lowConfigModeChanged(bool enabled);

private:
    explicit PerformanceMonitor(QObject *parent = nullptr);

    // 禁止拷贝和赋值
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

    /**
     * @brief 初始化默认阈值
     */
    void initializeDefaultThresholds();

    /**
     * @brief 格式化时间输出
     * @param milliseconds 毫秒数
     * @return 格式化的时间字符串
     */
    QString formatTime(qint64 milliseconds) const;

    /**
     * @brief 计算时间占比
     * @param partTime 部分时间
     * @param totalTime 总时间
     * @return 百分比字符串
     */
    QString calculatePercentage(qint64 partTime, qint64 totalTime) const;

    // 静态单例实例
    static PerformanceMonitor* m_instance;
    static QMutex m_instanceMutex;

    // 互斥锁（用于线程安全）
    mutable QMutex m_mutex;

    // 测量数据
    QMap<QString, QElapsedTimer> m_timers;
    QMap<QString, qint64> m_measurements;
    
    // 启动阶段顺序记录
    QStringList m_startupPhaseOrder;
    
    // 启动测量状态
    std::atomic<bool> m_startupMeasuring;
    std::atomic<bool> m_startupCompleted;
    QDateTime m_startupStartTime;
    
    // 低配置模式
    std::atomic<bool> m_lowConfigModeEnabled;
    
    // 性能阈值（毫秒）
    QMap<QString, qint64> m_phaseThresholds;
    std::atomic<qint64> m_totalStartupThreshold;
    
    // 低配置模式下的阈值倍数
    static constexpr double LOW_CONFIG_THRESHOLD_MULTIPLIER = 1.5;
};
