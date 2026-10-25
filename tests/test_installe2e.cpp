/**
 * @file   test_installe2e.cpp
 * @brief  安装新实例端到端冒烟测试（离屏 + 真实网络）
 *
 * 走完整的 MultiMC 下载管线：清单缓存下载 → 版本 JSON → 客户端 JAR →
 * 依赖库阶段作业（NetJob 聚合）→ 资源阶段作业 → 完成。
 * 断言：进度全程 0~100、最终下载完成、关键文件落盘。
 */

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QDebug>

#include "utils/VersionDownloader.h"
#include "utils/DownloadTaskManager.h"

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);

    const QString versionId = QStringLiteral("b1.7.3");
    const QString root = QStringLiteral("D:/BlockBox/.tmp/e2e-inst");

    VersionDownloader *vd = VersionDownloader::instance();
    vd->setDownloadSource(VersionDownloader::BMCL);

    int g_failures = 0;
    double lastPercent = -1.0;

    QObject::connect(DownloadTaskManager::instance(),
                     &DownloadTaskManager::taskProgressUpdated,
                     [&lastPercent, &g_failures](const QString &, double progress) {
        if (progress < 0 || progress > 100.0)
        {
            qWarning() << "[FAIL] 进度越界:" << progress;
            ++g_failures;
        }
        if (progress - lastPercent >= 5.0)
        {
            qWarning() << "[进度]" << progress << "%";
            lastPercent = progress;
        }
    });

    QEventLoop loop;
    bool completed = false;
    QString failReason;

    QObject::connect(vd, &VersionDownloader::downloadCompleted,
                     [&](const QString &verId, const QString &path) {
        qWarning() << "[完成]" << verId << path;
        completed = true;
        loop.quit();
    });
    QObject::connect(vd, &VersionDownloader::downloadFailed,
                     [&](const QString &error) {
        qWarning() << "[FAIL] 下载失败:" << error;
        failReason = error;
        loop.quit();
    });

    vd->downloadVanillaTo(versionId, root + "/versions/" + versionId,
                          QStringLiteral("e2e"), QStringList());

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [&]() {
        qWarning() << "[FAIL] 超时（180 秒）";
        ++g_failures;
        loop.quit();
    });
    timeout.start(180 * 1000);

    loop.exec();

    if (!completed)
    {
        qWarning() << "[FAIL] 未完成:" << (failReason.isEmpty() ? QStringLiteral("超时") : failReason);
        ++g_failures;
    }
    else
    {
        const QString jar = root + "/versions/" + versionId + "/" + versionId + ".jar";
        const QString json = root + "/versions/" + versionId + "/" + versionId + ".json";
        const bool jarOk = QFile::exists(jar);
        const bool jsonOk = QFile::exists(json);
        qWarning() << "[CHECK] 客户端 JAR 落盘:" << jarOk;
        qWarning() << "[CHECK] 版本 JSON 落盘:" << jsonOk;
        if (!jarOk || !jsonOk)
            ++g_failures;
    }

    vd->cancelDownload();
    qWarning() << (g_failures == 0 ? "E2E: ALL PASS" : "E2E: FAILURES PRESENT");
    return g_failures == 0 ? 0 : 1;
}
