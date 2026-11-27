/**
 * @file   memory_optimizer_probe.cpp
 * @brief  内存优化引擎自测探针：在真实机器上跑一次 MemoryOptimizer::optimize() 并打印明细
 *
 * 用法（在临时构建目录里）：
 *   qmake ../../C-BlockBox/tests/memory_optimizer_probe/memory_optimizer_probe.pro
 *   mingw32-make && release/memory_optimizer_probe.exe
 *
 * 关注三点：耗时是否在可接受范围（启动前是同步调用）、收了多少个进程、
 * 系统可用内存有没有真的涨（管理员运行时 should 涨得更多）。
 */

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#include "utils/MemoryOptimizer.h"

namespace {

QString sizeText(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("读取不到");
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qInfo().noquote() << "[probe] 管理员权限:" << MemoryOptimizer::isElevated();

    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    const MemoryOptimizeReport report = MemoryOptimizer::optimize(true, true);
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - started;

    qInfo().noquote() << "[probe] 耗时:" << elapsed << "ms";
    qInfo().noquote() << "[probe] 系统可用内存:" << sizeText(report.availableBeforeBytes)
                      << "->" << sizeText(report.availableAfterBytes)
                      << "，增量" << sizeText(report.systemFreedBytes());
    qInfo().noquote() << "[probe] 启动器工作集:" << sizeText(report.launcherBeforeBytes)
                      << "->" << sizeText(report.launcherAfterBytes);
    qInfo().noquote() << "[probe] 收回进程数:" << report.processesTrimmed
                      << "，跳过:" << report.processesSkipped
                      << "，系统缓存已清:" << report.systemCachePurged;
    qInfo().noquote() << "[probe] headline:" << sizeText(report.headlineFreedBytes());
    return 0;
}
