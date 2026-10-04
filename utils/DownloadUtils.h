/**
 * @file   DownloadUtils.h
 * @brief  下载工具类，提供下载相关的格式化方法
 * @author BlockBox Team
 * @date   2026-05-29
 */

#pragma once

#include <QString>

/**
 * @class DownloadUtils
 * @brief 下载工具类，提供文件大小、速度和剩余时间的格式化
 */
class DownloadUtils
{
public:
    /**
     * @brief 格式化文件大小
     * @param bytes 字节数
     * @return 自适应单位的字符串，如 "1.23 GB"、"456.78 MB"、"12.34 KB"、"789 B"
     */
    static QString formatSize(qint64 bytes);

    /**
     * @brief 格式化下载速度
     * @param bytesPerSecond 每秒字节数
     * @return 带速度单位的字符串，如 "1.23 MB/s"、"456 KB/s"
     */
    static QString formatSpeed(qint64 bytesPerSecond);

    /**
     * @brief 格式化剩余时间
     * @param seconds 秒数
     * @return 中文时间单位字符串，如 "12 秒"、"3 分 45 秒"、"1 小时 23 分"
     */
    static QString formatEta(int seconds);

private:
    DownloadUtils() = delete;
};