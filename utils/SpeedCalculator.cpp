/**
 * @file   SpeedCalculator.cpp
 * @brief  下载速度计算器实现
 * @author BlockBox Team
 * @date   2026-05-29
 */
#include "SpeedCalculator.h"

#include <QtMath>

SpeedCalculator::SpeedCalculator(QObject *parent)
    : QObject(parent)
    , m_lastBytes(0)
{
}

void SpeedCalculator::addSample(qint64 totalBytesDownloaded)
{
    qint64 now = m_timer.elapsed();
    Sample sample;
    sample.bytes = totalBytesDownloaded;
    sample.timestampMs = now;

    m_samples.append(sample);

    qint64 cutoff = now - WINDOW_MS;
    while (!m_samples.isEmpty() && m_samples.first().timestampMs < cutoff)
    {
        m_samples.removeFirst();
    }

    qint64 speed = currentSpeed();
    if (speed != m_lastBytes)
    {
        m_lastBytes = speed;
        emit speedUpdated(speed);
    }
}

void SpeedCalculator::reset()
{
    m_samples.clear();
    m_timer.restart();
    m_lastBytes = 0;
}

qint64 SpeedCalculator::currentSpeed() const
{
    if (m_samples.size() < 2)
    {
        return 0;
    }

    qint64 now = m_samples.last().timestampMs;
    qint64 cutoff = now - WINDOW_MS;

    // 取窗口内的第一个有效采样点
    int firstIdx = 0;
    while (firstIdx < m_samples.size() && m_samples[firstIdx].timestampMs < cutoff)
    {
        firstIdx++;
    }

    // 需要至少 2 个采样点计算速度
    if (firstIdx >= m_samples.size() - 1)
    {
        return 0;
    }

    const Sample &first = m_samples[firstIdx];
    const Sample &last = m_samples.last();

    qint64 deltaBytes = last.bytes - first.bytes;
    qint64 deltaMs = last.timestampMs - first.timestampMs;

    if (deltaMs <= 0)
    {
        return 0;
    }

    return static_cast<qint64>(static_cast<double>(deltaBytes) * 1000.0 / deltaMs);
}

qint64 SpeedCalculator::averageSpeed() const
{
    if (m_samples.size() < 2)
    {
        return 0;
    }

    qint64 totalBytes = m_samples.last().bytes - m_samples.first().bytes;
    qint64 totalMs = m_samples.last().timestampMs - m_samples.first().timestampMs;

    if (totalMs <= 0)
    {
        return 0;
    }

    return totalBytes * 1000 / totalMs;
}

int SpeedCalculator::etaSeconds(qint64 totalBytes, qint64 downloadedBytes) const
{
    if (totalBytes <= 0 || downloadedBytes >= totalBytes)
    {
        return 0;
    }

    qint64 remainingBytes = totalBytes - downloadedBytes;
    qint64 speed = currentSpeed();

    if (speed <= 0)
    {
        return -1;
    }

    return static_cast<int>(remainingBytes / speed);
}

double SpeedCalculator::progressPercent(qint64 totalBytes, qint64 downloadedBytes) const
{
    if (totalBytes <= 0)
    {
        return 0.0;
    }

    return static_cast<double>(downloadedBytes) * 100.0 / totalBytes;
}