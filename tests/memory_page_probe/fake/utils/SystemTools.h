/**
 * 探针用 SystemTools 桩：真实实现会连带 MultiThreadDownloader /
 * DownloadTaskManager / AppMessageBox 一整条依赖链，这里只保留
 * MemoryOptimizePage 用到的静态接口，返回固定数字方便截图。
 */
#ifndef SYSTEMTOOLS_H
#define SYSTEMTOOLS_H

#include <QtGlobal>

#include "utils/MemoryOptimizer.h"

class SystemTools
{
public:
    /** 固定报「启动器占用 420 MB」 */
    static qint64 currentProcessWorkingSetBytes() { return qint64(420) * 1024 * 1024; }

    /** 探针里不清任何缓存 */
    static void clearLauncherImageCaches() {}

    /** 固定造一份「释放了 1.8 GB、收了 37 个后台程序、系统缓存已清理」的结果 */
    static MemoryOptimizeReport optimizeMemoryDetailed(bool = true, bool = true)
    {
        MemoryOptimizeReport report;
        report.supported = true;
        report.elevated = true;
        report.systemCachePurged = true;
        report.processesTrimmed = 37;
        report.processesSkipped = 12;
        report.availableBeforeBytes = qint64(2150) * 1024 * 1024;
        report.availableAfterBytes = qint64(3990) * 1024 * 1024;
        report.launcherBeforeBytes = qint64(420) * 1024 * 1024;
        report.launcherAfterBytes = qint64(210) * 1024 * 1024;
        return report;
    }

    static bool isRunningElevated() { return true; }
};

#endif // SYSTEMTOOLS_H
