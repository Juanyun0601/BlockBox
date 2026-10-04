/**
 * @file   DownloadUtils.cpp
 * @brief  下载工具类实现
 * @author BlockBox Team
 * @date   2026-05-29
 */
#include "DownloadUtils.h"

#include <QObject>

static const qint64 KB = 1024LL;
static const qint64 MB = 1024LL * KB;
static const qint64 GB = 1024LL * MB;

QString DownloadUtils::formatSize(qint64 bytes)
{
    if (bytes >= GB)
    {
        double value = static_cast<double>(bytes) / GB;
        return QString("%1 GB").arg(value, 0, 'f', 2);
    }
    if (bytes >= MB)
    {
        double value = static_cast<double>(bytes) / MB;
        return QString("%1 MB").arg(value, 0, 'f', 2);
    }
    if (bytes >= KB)
    {
        double value = static_cast<double>(bytes) / KB;
        return QString("%1 KB").arg(value, 0, 'f', 2);
    }
    return QString("%1 B").arg(bytes);
}

QString DownloadUtils::formatSpeed(qint64 bytesPerSecond)
{
    if (bytesPerSecond >= GB)
    {
        double value = static_cast<double>(bytesPerSecond) / GB;
        return QString("%1 GB/s").arg(value, 0, 'f', 2);
    }
    if (bytesPerSecond >= MB)
    {
        double value = static_cast<double>(bytesPerSecond) / MB;
        return QString("%1 MB/s").arg(value, 0, 'f', 2);
    }
    if (bytesPerSecond >= KB)
    {
        double value = static_cast<double>(bytesPerSecond) / KB;
        return QString("%1 KB/s").arg(value, 0, 'f', 2);
    }
    return QString("%1 B/s").arg(bytesPerSecond);
}

QString DownloadUtils::formatEta(int seconds)
{
    if (seconds < 0)
    {
        return QObject::tr("未知");
    }

    if (seconds < 60)
    {
        return QObject::tr("%1 秒").arg(seconds);
    }

    int minutes = seconds / 60;
    int remainingSeconds = seconds % 60;

    if (minutes < 60)
    {
        if (remainingSeconds > 0)
        {
            return QObject::tr("%1 分 %2 秒").arg(minutes).arg(remainingSeconds);
        }
        return QObject::tr("%1 分").arg(minutes);
    }

    int hours = minutes / 60;
    int remainingMinutes = minutes % 60;

    if (hours < 24)
    {
        if (remainingMinutes > 0)
        {
            return QObject::tr("%1 小时 %2 分").arg(hours).arg(remainingMinutes);
        }
        return QObject::tr("%1 小时").arg(hours);
    }

    int days = hours / 24;
    int remainingHours = hours % 24;

    if (remainingHours > 0)
    {
        return QObject::tr("%1 天 %2 小时").arg(days).arg(remainingHours);
    }
    return QObject::tr("%1 天").arg(days);
}