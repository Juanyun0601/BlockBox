/**
 * @file   java_full_scan_probe.cpp
 * @brief  实测 JavaScanWorker::doScan（快速检测 + 全盘深扫）能否在本机发现所有 Java
 * @author BlockBox Team
 * @date   2026-10-24
 */
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#include "utils/JavaScanWorker.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qputenv("QT_LOGGING_RULES", "*.debug=false");

    JavaScanWorker worker;

    QObject::connect(&worker, &JavaScanWorker::scanProgress,
                     &app, [](const QString& cur, int percent, int, int, int, int found) {
        static qint64 last = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - last < 2000) return;  // 每 2 秒采样一条，避免刷屏
        last = now;
        fprintf(stderr, "[%6.1fs] %3d%% found=%d :: %s\n",
                now / 1000.0, percent, found, cur.toLocal8Bit().constData());
    });

    QList<QPair<QString, QString>> results;
    QObject::connect(&worker, &JavaScanWorker::scanFinished, &app,
                     [&](const QList<QPair<QString, QString>>& r) { results = r; });

    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    worker.doScan();  // 同步执行（内部已按 100ms 轮询取消标志）
    const qint64 ms = QDateTime::currentMSecsSinceEpoch() - t0;

    printf("total: %d  (%lld ms)\n", results.size(), ms);
    for (const auto& r : results) {
        printf("%-12s @%s\n", r.second.toLocal8Bit().constData(),
               r.first.toLocal8Bit().constData());
    }
    return 0;
}
