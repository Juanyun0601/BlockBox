/**
 * @file   java_detect_quick_probe.cpp
 * @brief  实测 JavaScanWorker::runQuickScan 统一快速检测能否在本机发现 Java
 * @author BlockBox Team
 *
 * 覆盖「Java检测」按钮的第一阶段（环境变量 → 注册表 → 厂商目录 → 启动器运行时），
 * 不进入全盘深度搜索。发现任何 Java 返回 0，否则返回 1。
 */
#include <QCoreApplication>
#include <QDebug>

#include <cstdio>

#include "utils/JavaScanWorker.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    JavaScanWorker worker;
    const QList<QPair<QString, QString>> found = worker.runQuickScan();

    for (const auto &p : found)
    {
        std::printf("%s  @%s\n", p.second.toUtf8().constData(),
                    p.first.toUtf8().constData());
    }
    std::printf("total: %d\n", static_cast<int>(found.size()));
    std::fflush(stdout);

    return found.isEmpty() ? 1 : 0;
}
