/**
 * @file   install_vanilla_e2e_probe.cpp
 * @brief  安装新实例端到端探针：真实 VersionDownloader::downloadVanillaTo 全链路
 *
 * 走真实网络（官方源/BMCL 源）拉清单 → 版本 JSON → 客户端 JAR → 依赖库 → 资源，
 * 全部信号带时间戳打到 stderr，用于定位"安装新实例装不上"卡在哪一环。
 *
 * 用法：install_vanilla_e2e_probe.exe [版本id] [official|bmcl] [目标实例目录]
 * 退出码：0=安装成功 1=downloadFailed 2=超时卡死 3=完成但产物缺失
 */
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <cstdio>

#include "utils/DownloadTaskManager.h"
#include "utils/VersionDownloader.h"

namespace {

qint64 g_startMs = 0;

QString nowStamp()
{
    return QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
}

QString humanBytes(qint64 b)
{
    if (b >= 1024 * 1024)
        return QString("%1 MB").arg(b / (1024.0 * 1024.0), 0, 'f', 1);
    if (b >= 1024)
        return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    return QString("%1 B").arg(b);
}

void logLine(const char *tag, const QString &msg)
{
    std::fprintf(stderr, "[%s][+%5.1fs][%s] %s\n",
                 qUtf8Printable(nowStamp()),
                 (QDateTime::currentMSecsSinceEpoch() - g_startMs) / 1000.0,
                 tag, qUtf8Printable(msg));
    std::fflush(stderr);
}

void dumpTree(const QString &rootPath, int maxEntries = 40)
{
    int n = 0;
    QDirIterator it(rootPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && n < maxEntries) {
        QFileInfo fi(it.next());
        logLine("TREE", QString("%1 %2")
                    .arg(fi.isDir() ? QStringLiteral("<DIR>") : humanBytes(fi.size()), 10)
                    .arg(fi.absoluteFilePath().mid(rootPath.size())));
        ++n;
    }
    if (n == maxEntries)
        logLine("TREE", QStringLiteral("...(截断)"));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("BlockBox"));

    const QString versionId = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("1.21.1");
    const QString sourceArg = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                       : QStringLiteral("bmcl");
    QString target = argc > 3 ? QString::fromLocal8Bit(argv[3])
                              : QStringLiteral("D:/BlockBox/_out/install_probe/target/1.21.1");
    target = QDir::cleanPath(target);

    QElapsedTimer total;
    total.start();
    g_startMs = QDateTime::currentMSecsSinceEpoch();

    logLine("PROBE", QString("版本=%1 源=%2 目标=%3").arg(versionId, sourceArg, target));

    auto *dl = VersionDownloader::instance();
    dl->setDownloadSource(sourceArg == QLatin1String("official")
                              ? VersionDownloader::Official
                              : VersionDownloader::BMCL);

    int exitCode = -1;

    QObject::connect(dl, &VersionDownloader::downloadStarted, [](const QString &taskId) {
        logLine("SIG", QString("downloadStarted taskId=%1").arg(taskId));
    });
    QObject::connect(dl, &VersionDownloader::statusChanged, [](const QString &s) {
        logLine("SIG", QString("statusChanged: %1").arg(s));
    });
    QObject::connect(dl, &VersionDownloader::downloadProgressUpdated,
                     [](qint64 received, qint64 totalBytes) {
        static qint64 lastPrint = 0;
        if (QDateTime::currentMSecsSinceEpoch() - lastPrint < 2000)
            return;
        lastPrint = QDateTime::currentMSecsSinceEpoch();
        logLine("SIG", QString("progress %1 / %2")
                    .arg(humanBytes(received), humanBytes(totalBytes)));
    });
    QObject::connect(dl, &VersionDownloader::downloadCompleted,
                     [&](const QString &vid, const QString &path) {
        logLine("SIG", QString("downloadCompleted vid=%1 path=%2").arg(vid, path));
        // 校验产物：版本 json + 客户端 jar
        const QString jsonPath = path + "/" + vid + ".json";
        const QString jarPath = path + "/" + vid + ".jar";
        const bool jsonOk = QFileInfo(jsonPath).isFile();
        const bool jarOk = QFileInfo(jarPath).isFile() && QFileInfo(jarPath).size() > 0;
        logLine("CHECK", QString("%1 size=%2")
                    .arg(jsonPath, humanBytes(QFileInfo(jsonPath).size())));
        logLine("CHECK", QString("%1 size=%2")
                    .arg(jarPath, humanBytes(QFileInfo(jarPath).size())));
        dumpTree(target, 60);
        if (!jsonOk || !jarOk) {
            logLine("FAIL", "下载完成信号已发但产物缺失");
            exitCode = 3;
        } else {
            logLine("OK", QString("安装全链路成功，用时 %1 s")
                        .arg(total.elapsed() / 1000.0, 0, 'f', 1));
            exitCode = 0;
        }
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });
    QObject::connect(dl, &VersionDownloader::downloadFailed, [&](const QString &err) {
        logLine("SIG", QString("downloadFailed: %1").arg(err));
        exitCode = 1;
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });
    QObject::connect(dl, &VersionDownloader::downloadCancelled, [&]() {
        logLine("SIG", "downloadCancelled");
        exitCode = 1;
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });

    auto *tasks = DownloadTaskManager::instance();
    QObject::connect(tasks, &DownloadTaskManager::taskStatusChanged,
                     [](const QString &taskId, DownloadTaskStatus status) {
        logLine("TASK", QString("%1 status=%2").arg(taskId).arg(int(status)));
    });
    QObject::connect(tasks, &DownloadTaskManager::taskStageChanged,
                     [](const QString &taskId, DownloadStage stage, int stageProgress) {
        logLine("TASK", QString("%1 stage=%2 progress=%3")
                    .arg(taskId).arg(int(stage)).arg(stageProgress));
    });

    // 看门狗：每 10s 心跳打印内部状态；600s 仍无结果判为卡死
    const int kTimeoutSec = 600;
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        const int elapsed = total.elapsed() / 1000;
        logLine("WATCH", QString("elapsed=%1s isDownloading=%2 isPaused=%3 phase任务=%4")
                    .arg(elapsed)
                    .arg(dl->isDownloading())
                    .arg(dl->isPaused())
                    .arg(dl->currentTaskId()));
        if (elapsed >= kTimeoutSec) {
            logLine("FAIL", QString("%1 秒无结果 → 判定安装流程卡死").arg(kTimeoutSec));
            dumpTree(target, 60);
            exitCode = 2;
            dl->cancelDownload();
            QTimer::singleShot(0, &app, &QCoreApplication::quit);
        }
    });
    watchdog.start(10000);

    QDir().mkpath(target);
    dl->downloadVanillaTo(versionId, target, QStringLiteral("ProbeInstance"));

    app.exec();

    logLine("PROBE", QString("退出码=%1").arg(exitCode));
    return exitCode < 0 ? 1 : exitCode;
}
