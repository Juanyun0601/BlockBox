/**
 * 探针用 SystemTools 桩：真实实现会连带 MultiThreadDownloader /
 * DownloadTaskManager / AppMessageBox 一整条依赖链，这里只保留
 * MemoryOptimizePage 用到的两个静态接口，返回固定数字方便截图。
 */
#ifndef SYSTEMTOOLS_H
#define SYSTEMTOOLS_H

#include <QtGlobal>

class SystemTools
{
public:
    /** 固定报「释放了 180 MB」 */
    static qint64 optimizeMemorySilently() { return qint64(180) * 1024 * 1024; }

    /** 固定报「启动器占用 420 MB」 */
    static qint64 currentProcessWorkingSetBytes() { return qint64(420) * 1024 * 1024; }
};

#endif // SYSTEMTOOLS_H
