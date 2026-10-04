/**
 * @file  main.cpp
 * @brief VersionDownloader 独立控制台测试驱动
 *
 * 把真实的 VersionDownloader 状态机放进最小事件循环里跑一次真实下载，
 * 打印全部信号与阶段轮询，用于定位“下载客户端”阶段死等问题。
 *
 * 用法: vdtest [版本ID] [official|bmcl]
 */
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTimer>

#include <cstdio>

#include "VersionDownloader.h"
#include "DownloadTaskManager.h"

static QString now()
{
    return QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vdtest"));
    QCoreApplication::setOrganizationName(QStringLiteral("vdtest"));

    QString version = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("26.2");
    QString sourceArg = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("official");
    VersionDownloader::DownloadSource src = (sourceArg == QStringLiteral("bmcl"))
                                                ? VersionDownloader::BMCL
                                                : VersionDownloader::Official;

    // 工作目录：构建目录下独立沙盒，避免污染真实实例
    const QString workDir = QCoreApplication::applicationDirPath() + "/vdtest-instance";
    QDir(workDir).removeRecursively();
    QDir().mkpath(workDir);

    qInfo().noquote() << QString("[%1] === vdtest start: version=%2 source=%3 workDir=%4")
                             .arg(now(), version, sourceArg, workDir);

    // 复现模式：剥离 reply 的 isClientJar 属性，模拟分段重试/降级链路属性丢失
    // （修复前会导致 JAR 写盘成功后永久卡死在“下载客户端”）
    const bool stripProps = qEnvironmentVariableIsSet("VDTEST_STRIP_PROPS");
    VersionDownloader::s_testStripReplyProps = stripProps;
    if (stripProps)
        qInfo() << "[vdtest] STRIP_PROPS mode ON";

    VersionDownloader *vd = VersionDownloader::instance();
    vd->setDownloadSource(src);

    // ── 全部信号镜像打印 ──
    QObject::connect(vd, &VersionDownloader::downloadStarted, vd, [](const QString &taskId) {
        qInfo().noquote() << QString("[%1] SIG downloadStarted task=%2").arg(now(), taskId);
    });
    QObject::connect(vd, &VersionDownloader::downloadProgressUpdated, vd,
                     [](qint64 rec, qint64 total) {
                         static qint64 lastRec = -1;
                         if (rec / (1024 * 1024) != lastRec / (1024 * 1024)) { // 每MB打一条
                             qInfo().noquote() << QString("[%1] SIG progress %2/%3 B")
                                                      .arg(now()).arg(rec).arg(total);
                             lastRec = rec;
                         }
                     });
    QObject::connect(vd, &VersionDownloader::statusChanged, vd, [](const QString &s) {
        qInfo().noquote() << QString("[%1] SIG status: %2").arg(now(), s);
    });
    QObject::connect(vd, &VersionDownloader::downloadCompleted, vd,
                     [&app](const QString &ver, const QString &path) {
                         qInfo().noquote() << QString("[%1] *** SIG downloadCompleted ver=%2 path=%3")
                                                  .arg(now(), ver, path);
                         QTimer::singleShot(500, &app, [&app]() { app.exit(0); });
                     });
    QObject::connect(vd, &VersionDownloader::downloadFailed, vd,
                     [&app](const QString &err) {
                         qInfo().noquote() << QString("[%1] *** SIG downloadFailed: %2")
                                                  .arg(now(), err);
                         QTimer::singleShot(500, &app, [&app]() { app.exit(1); });
                     });
    QObject::connect(vd, &VersionDownloader::downloadCancelled, vd, [&app]() {
        qInfo().noquote() << QString("[%1] *** SIG downloadCancelled").arg(now());
        QTimer::singleShot(500, &app, [&app]() { app.exit(1); });
    });

    // ── 启动下载 ──
    QTimer::singleShot(0, &app, [vd, version, workDir]() {
        qInfo().noquote() << QString("[%1] calling downloadVanillaTo...").arg(now());
        vd->downloadVanillaTo(version, workDir, QStringLiteral("vdtest"));
    });

    // ── 每秒轮询任务状态 ──
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&app, workDir]() {
        static int elapsed = 0;
        elapsed += 1;

        QList<DownloadTask> tasks = DownloadTaskManager::instance()->getAllTasks();
        if (!tasks.isEmpty()) {
            const DownloadTask &t = tasks.last();
            qInfo().noquote() << QString("[%1] poll %2s: stage=%3 progress=%4% status=%5 speed=%6 KB/s bytes=%7/%8 files=%9 step=%10")
                .arg(now()).arg(elapsed)
                .arg(int(t.stage)).arg(t.progressDouble, 0, 'f', 1)
                .arg(int(t.status))
                .arg(t.downloadSpeed / 1024.0, 0, 'f', 0)
                .arg(t.bytesReceived).arg(t.bytesTotal)
                .arg(t.fileQueue.size())
                .arg(t.currentStep);
        } else {
            qInfo().noquote() << QString("[%1] poll %2s: no task yet").arg(now()).arg(elapsed);
        }

        // 工作目录内容快照（每10秒）
        if (elapsed % 10 == 0) {
            QDir dir(workDir);
            qInfo().noquote() << QString("[%1] workdir entries: %2")
                .arg(now(), dir.entryList(QDir::AllEntries | QDir::Hidden).join(", "));
        }

        // 看门狗：判定死等并退出（VDTEST_TIMEOUT 可配置，默认 180 秒）
        bool okTO = false;
        const int timeoutSec = qEnvironmentVariableIntValue("VDTEST_TIMEOUT", &okTO);
        const int limit = (okTO && timeoutSec > 0) ? timeoutSec : 180;
        if (elapsed >= limit) {
            qWarning().noquote() << QString("[%1] !!! WATCHDOG: no completion in %2s — HANG CONFIRMED")
                                        .arg(now()).arg(elapsed);
            app.exit(2);
        }
    });
    poll.start(1000);

    int rc = app.exec();
    qInfo().noquote() << QString("[%1] === vdtest exit code=%2").arg(now()).arg(rc);
    return rc;
}
