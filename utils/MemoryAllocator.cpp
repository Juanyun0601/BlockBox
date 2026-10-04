/**
 * @file   MemoryAllocator.cpp
 * @brief  自动内存分配器实现
 * @author BlockBox Team
 * @date   2026-06-19
 */

#include "MemoryAllocator.h"

#include <algorithm>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#include <unistd.h>
#endif

#ifdef Q_OS_MAC
#include <sys/sysctl.h>
#include <unistd.h>
#endif

MemoryAllocator* MemoryAllocator::m_instance = nullptr;
static QMutex s_memInstanceMutex;

MemoryAllocator* MemoryAllocator::instance()
{
    if (!m_instance)
    {
        QMutexLocker locker(&s_memInstanceMutex);
        if (!m_instance)
        {
            m_instance = new MemoryAllocator();
        }
    }
    return m_instance;
}

int MemoryAllocator::detectTotalSystemMemory()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memStatus))
    {
        return static_cast<int>(memStatus.ullTotalPhys / (1024 * 1024));
    }
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // Try /proc/meminfo first (works on Android)
    QFile memFile("/proc/meminfo");
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream stream(&memFile);
        while (!stream.atEnd())
        {
            QString line = stream.readLine();
            if (line.startsWith("MemTotal:"))
            {
                memFile.close();
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2)
                {
                    // Value is in kB, convert to MB
                    return static_cast<int>(parts[1].toULongLong() / 1024);
                }
            }
        }
        memFile.close();
    }
    // Fallback to sysconf
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0)
    {
        return static_cast<int>((pages * pageSize) / (1024 * 1024));
    }
#elif defined(Q_OS_MAC)
    int mib[2] = { CTL_HW, HW_MEMSIZE };
    int64_t memSize = 0;
    size_t len = sizeof(memSize);
    if (sysctl(mib, 2, &memSize, &len, nullptr, 0) == 0)
    {
        return static_cast<int>(memSize / (1024 * 1024));
    }
#endif
    // 默认返回 4GB
    return 4096;
}

int MemoryAllocator::detectAvailableMemory()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memStatus))
    {
        return static_cast<int>(memStatus.ullAvailPhys / (1024 * 1024));
    }
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // Try /proc/meminfo first (works on Android)
    QFile memFile("/proc/meminfo");
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream stream(&memFile);
        quint64 totalMem = 0;
        quint64 availableMem = 0;

        while (!stream.atEnd())
        {
            QString line = stream.readLine();
            if (line.startsWith("MemTotal:"))
            {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2)
                    totalMem = parts[1].toULongLong() / 1024;
            }
            else if (line.startsWith("MemAvailable:"))
            {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2)
                    availableMem = parts[1].toULongLong() / 1024;
            }
        }
        memFile.close();

        if (availableMem > 0)
            return static_cast<int>(availableMem);
        if (totalMem > 0)
            return static_cast<int>(totalMem / 2);
    }
    // Fallback to sysconf
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0)
    {
        return static_cast<int>((pages * pageSize) / (1024 * 1024));
    }
#endif
    // 默认返回总内存的一半
    return detectTotalSystemMemory() / 2;
}

MemoryAllocation MemoryAllocator::calculateRecommendedAllocation(MemoryAllocationMode mode)
{
    MemoryAllocation result;
    result.mode = mode;

    result.totalSystemMemoryMb = detectTotalSystemMemory();
    result.availableMemoryMb = detectAvailableMemory();

    const int totalMem = result.totalSystemMemoryMb;

    // 根据模式确定分配比例和安全上限
    double ratio;       // 总内存分配比例
    double safetyCap;   // 安全上限 (总内存的百分比)
    int smallMaxLimit;  // < 4GB 时的上限
    int mediumMaxLimit; // 4~8GB 时的上限
    int largeMaxLimit;  // 8~16GB 时的上限
    int hugeMaxLimit;   // > 16GB 时的上限

    switch (mode)
    {
    case MemoryAllocationMode::Normal:
        ratio = 0.4;
        safetyCap = 0.7;
        smallMaxLimit = 1536;
        mediumMaxLimit = 3072;
        largeMaxLimit = 6144;
        hugeMaxLimit = 8192;
        break;
    case MemoryAllocationMode::Optimized:
        ratio = 0.5;
        safetyCap = 0.75;
        smallMaxLimit = 2048;
        mediumMaxLimit = 4096;
        largeMaxLimit = 8192;
        hugeMaxLimit = 12288;
        break;
    case MemoryAllocationMode::Extreme:
        ratio = 0.6;
        safetyCap = 0.8;
        smallMaxLimit = 2048;
        mediumMaxLimit = 4096;
        largeMaxLimit = 8192;
        hugeMaxLimit = 16384;
        break;
    default:
        ratio = 0.5;
        safetyCap = 0.75;
        smallMaxLimit = 2048;
        mediumMaxLimit = 4096;
        largeMaxLimit = 8192;
        hugeMaxLimit = 12288;
        break;
    }

    int recommendedMax;
    int recommendedMin;

    if (totalMem < 4096)
    {
        recommendedMin = 512;
        recommendedMax = std::min(smallMaxLimit, static_cast<int>(totalMem * ratio));
    }
    else if (totalMem < 8192)
    {
        recommendedMin = 1024;
        recommendedMax = std::min(mediumMaxLimit, static_cast<int>(totalMem * ratio));
    }
    else if (totalMem < 16384)
    {
        recommendedMin = 1024;
        recommendedMax = std::min(largeMaxLimit, static_cast<int>(totalMem * ratio));
    }
    else
    {
        recommendedMin = 1024;
        recommendedMax = std::min(hugeMaxLimit, static_cast<int>(totalMem * ratio));
    }

    // 安全限制: 最大内存不超过总内存的安全上限
    int safeMax = static_cast<int>(totalMem * safetyCap);
    recommendedMax = std::min(recommendedMax, safeMax);

    // 确保最小内存不小于 512MB
    recommendedMin = std::max(512, recommendedMin);

    // 确保最大内存不小于最小内存
    recommendedMax = std::max(recommendedMax, recommendedMin);

    // 对齐到 512MB 步长
    result.minMemoryMb = alignToStep(recommendedMin);
    result.maxMemoryMb = alignToStep(recommendedMax);

    return result;
}

int MemoryAllocator::alignToStep(int valueMb, int stepMb)
{
    if (stepMb <= 0)
    {
        return valueMb;
    }
    // 向上取整到步长边界
    return ((valueMb + stepMb - 1) / stepMb) * stepMb;
}

const char* MemoryAllocator::modeDisplayName(MemoryAllocationMode mode)
{
    switch (mode)
    {
    case MemoryAllocationMode::Normal:    return "普通";
    case MemoryAllocationMode::Optimized: return "优化";
    case MemoryAllocationMode::Extreme:   return "极致";
    default:                              return "未知";
    }
}