/**
 * @file   SpeedCalculator.h
 * @brief  下载速度计算器，基于滑动窗口的实时速度计算
 * @author BlockBox Team
 * @date   2026-05-29
 */

#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPair>

/**
 * @class SpeedCalculator
 * @brief 下载速度计算器类
 *
 * 采用滑动窗口机制计算实时下载速度，使用加权平均算法，
 * 优先考虑较新的采样点。同时提供平均速度、剩余时间预估和进度百分比功能。
 */
class SpeedCalculator : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit SpeedCalculator(QObject *parent = nullptr);

    /**
     * @brief 添加采样点
     * @param totalBytesDownloaded 当前已下载的总字节数
     */
    void addSample(qint64 totalBytesDownloaded);

    /**
     * @brief 重置所有采样数据
     */
    void reset();

    /**
     * @brief 获取当前实时速度（3秒滑动窗口内的加权平均）
     * @return 每秒字节数
     */
    qint64 currentSpeed() const;

    /**
     * @brief 获取自上次重置以来的平均速度
     * @return 每秒字节数
     */
    qint64 averageSpeed() const;

    /**
     * @brief 预估剩余时间
     * @param totalBytes 总字节数
     * @param downloadedBytes 已下载字节数
     * @return 预估剩余秒数
     */
    int etaSeconds(qint64 totalBytes, qint64 downloadedBytes) const;

    /**
     * @brief 计算进度百分比
     * @param totalBytes 总字节数
     * @param downloadedBytes 已下载字节数
     * @return 进度百分比（0-100）
     */
    double progressPercent(qint64 totalBytes, qint64 downloadedBytes) const;

signals:
    /**
     * @brief 速度更新信号
     * @param speed 当前速度（字节/秒）
     */
    void speedUpdated(qint64 speed);

private:
    /**
     * @struct Sample
     * @brief 采样点数据结构
     */
    struct Sample
    {
        qint64 bytes;       /**< 累积字节数 */
        qint64 timestampMs; /**< 时间戳（毫秒） */
    };

    QList<Sample> m_samples;     /**< 采样点列表 */
    QElapsedTimer m_timer;       /**< 高精度计时器 */
    qint64 m_lastBytes;          /**< 上次记录的字节数 */

    static const int WINDOW_MS = 3000; /**< 滑动窗口大小3秒 */
};