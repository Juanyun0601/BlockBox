/**
 * @file   neoforge_e2e_probe.cpp
 * @brief  NeoForge 安装端到端探针：真实 NeoForgeInstaller 全链路（无头）
 *
 * 走真实网络下载安装器 → 解析 install_profile → 下载库与处理器依赖
 * → 跑 processors → 生成版本 JSON → 补齐库，全部信号带时间戳打到 stderr。
 *
 * 用法：neoforge_e2e_probe.exe [mc版本] [neoforge版本] [游戏根目录] [official|bmcl]
 * 退出码：0=安装成功且产物校验通过 1=installFailed 2=超时 3=完成但产物缺失
 */
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>

#include "utils/DownloadTaskManager.h"
#include "utils/SettingsManager.h"
#include "utils/GameLauncher.h"
#include "utils/NeoForgeInstaller.h"

namespace {

qint64 g_startMs = 0;

QString nowStamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}

void logLine(const char *tag, const QString &msg)
{
    std::fprintf(stderr, "[%s][+%6.1fs][%s] %s\n",
                 qUtf8Printable(nowStamp()),
                 (QDateTime::currentMSecsSinceEpoch() - g_startMs) / 1000.0,
                 tag, qUtf8Printable(msg));
    std::fflush(stderr);
}

} // namespace

// 探针不链接整个 GameLauncher：用桩提供安装任务 getJavaPath 里引用的两个符号
GameLauncher *GameLauncher::m_instance = nullptr;
GameLauncher *GameLauncher::instance() { return nullptr; }
GameLauncher::JavaInfo GameLauncher::findBestJavaVersion(const QString &, int) { return {}; }

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    g_startMs = QDateTime::currentMSecsSinceEpoch();

    const QString mcVersion = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("1.21.1");
    const QString nfVersion = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("21.1.9");
    const QString gameRoot = QDir::cleanPath(argc > 3 ? QString::fromLocal8Bit(argv[3])
                                                       : app.applicationDirPath() + QStringLiteral("/sandbox/.minecraft"));
    const bool useBmcl = !(argc > 4 && QByteArray(argv[4]) == "official");

    logLine("PROBE", QString("mc=%1 neoforge=%2 root=%3 source=%4")
                          .arg(mcVersion, nfVersion, gameRoot,
                               useBmcl ? QStringLiteral("bmcl") : QStringLiteral("official")));

    QDir rootDir(gameRoot);
    if (!rootDir.exists())
        rootDir.mkpath(QStringLiteral("."));

    const QString vanillaJar = gameRoot + QStringLiteral("/versions/%1/%1.jar").arg(mcVersion);
    if (!QFile::exists(vanillaJar))
    {
        logLine("FATAL", QString("原版 %1 不存在: %2").arg(mcVersion, vanillaJar));
        return 3;
    }
    logLine("PROBE", QString("vanilla jar OK: %1 (%2 bytes)")
                          .arg(vanillaJar).arg(QFileInfo(vanillaJar).size()));

    SettingsManager::instance()->setNeoForgeDownloadSource(
        useBmcl ? NeoForgeDownloadSource::BMCL : NeoForgeDownloadSource::Official);

    const QString taskId = DownloadTaskManager::instance()->addTask(
        QStringLiteral("NeoForgeE2E"), gameRoot, mcVersion,
        QStringList{QStringLiteral("NeoForge %1").arg(nfVersion)});
    logLine("PROBE", QString("taskId=%1").arg(taskId));

    int exitCode = 2;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &watchdog, [&]() {
        logLine("FATAL", QStringLiteral("总超时（20 分钟），判卡死"));
        std::exit(2);
    });
    watchdog.start(20 * 60 * 1000);

    auto *installer = NeoForgeInstaller::instance();
    installer->setCurrentTaskId(taskId);
    QObject::connect(installer, &NeoForgeInstaller::statusChanged, &app, [&](const QString &status) {
        logLine("STAT ", status);
    });
    QObject::connect(installer, &NeoForgeInstaller::installProgressUpdated, &app,
                     [&](int percent, const QString &status) {
        logLine("PROG ", QString("%1% %2").arg(percent).arg(status));
    });
    QObject::connect(installer, &NeoForgeInstaller::installCompleted, &app,
                     [&](const QString &versionId) {
        logLine("DONE ", QString("installCompleted id=%1 — 校验产物").arg(versionId));

        const QString vdir = gameRoot + QStringLiteral("/versions/") + versionId;
        const QString jsonPath = vdir + QStringLiteral("/") + versionId + QStringLiteral(".json");

        bool ok = true;
        if (!QFile::exists(jsonPath)) { logLine("MISS", jsonPath); ok = false; }

        if (ok)
        {
            QFile jf(jsonPath);
            jf.open(QIODevice::ReadOnly);
            const QJsonObject obj = QJsonDocument::fromJson(jf.readAll()).object();
            jf.close();
            const QJsonArray libs = obj.value(QStringLiteral("libraries")).toArray();
            logLine("JSON", QString("id=%1 inheritsFrom=%2 mainClass=%3 libraries=%4")
                       .arg(obj.value(QStringLiteral("id")).toString(),
                            obj.value(QStringLiteral("inheritsFrom")).toString(),
                            obj.value(QStringLiteral("mainClass")).toString())
                       .arg(libs.size()));

            // 官方安装器布局 + 安装收尾的单实例目录合并：
            // 合并后 inheritsFrom 会被去掉、clientVersion 会被写上
            const bool selfContained =
                !obj.value(QStringLiteral("clientVersion")).toString().isEmpty();
            if (obj.value(QStringLiteral("id")).toString().isEmpty()
                || obj.value(QStringLiteral("mainClass")).toString().isEmpty()
                || (obj.value(QStringLiteral("inheritsFrom")).toString().isEmpty() && !selfContained))
                ok = false;

            if (!QFile::exists(vdir + QStringLiteral("/") + versionId + QStringLiteral(".jar")))
            {
                logLine("MISS", QStringLiteral("版本目录缺少客户端 JAR"));
                ok = false;
            }

            for (const QJsonValue &v : libs)
            {
                const QString name = v.toObject().value(QStringLiteral("name")).toString();
                if (name.contains(QStringLiteral("minecraft-client-patched"))
                    || name.endsWith(QStringLiteral(":client"))
                    || name.endsWith(QStringLiteral(":universal")))
                {
                    logLine("FAIL", QStringLiteral("版本 JSON 里不应出现加载器/补丁客户端库: ") + name);
                    ok = false;
                }
            }

            // 补丁后的客户端与加载器 universal 必须落在库目录里（FML 按坐标找）
            const QString patchedModern = gameRoot
                + QStringLiteral("/libraries/net/neoforged/minecraft-client-patched/%1/minecraft-client-patched-%1.jar")
                      .arg(nfVersion);
            const QString patchedNeoForge = gameRoot
                + QStringLiteral("/libraries/net/neoforged/neoforge/%1/neoforge-%1-client.jar").arg(nfVersion);
            const QString patchedLegacy = gameRoot
                + QStringLiteral("/libraries/net/neoforged/forge/%1/forge-%1-client.jar").arg(nfVersion);

            QString patched;
            for (const QString &candidate : {patchedModern, patchedNeoForge, patchedLegacy})
            {
                if (QFile::exists(candidate))
                {
                    patched = candidate;
                    break;
                }
            }
            if (patched.isEmpty())
            {
                logLine("MISS", QStringLiteral("补丁后的客户端: ") + patchedModern);
                ok = false;
            }
            else
            {
                logLine("OK  ", QString("patched client jar: %1 bytes").arg(QFileInfo(patched).size()));
            }

            const QStringList universalCandidates{
                gameRoot + QStringLiteral("/libraries/net/neoforged/neoforge/%1/neoforge-%1-universal.jar")
                               .arg(nfVersion),
                gameRoot + QStringLiteral("/libraries/net/neoforged/forge/%1/forge-%1-universal.jar")
                               .arg(nfVersion)};
            bool universalFound = false;
            for (const QString &candidate : universalCandidates)
            {
                if (QFile::exists(candidate))
                {
                    universalFound = true;
                    break;
                }
            }
            if (!universalFound)
            {
                logLine("MISS", universalCandidates.first());
                ok = false;
            }
        }

        logLine(ok ? "PASS" : "FAIL", ok ? QStringLiteral("端到端安装成功，产物符合官方布局")
                                         : QStringLiteral("流程完成但产物缺失"));
        std::exit(ok ? 0 : 3);
    });
    QObject::connect(installer, &NeoForgeInstaller::installFailed, &app,
                     [&](const QString &error) {
        logLine("FATAL", QStringLiteral("installFailed: %1").arg(error));
        std::exit(1);
    });

    logLine("PROBE", QStringLiteral("开始安装（游戏根目录作为实例根传入，模拟安装页调用）"));
    installer->downloadNeoForgeInstaller(mcVersion, nfVersion, gameRoot,
                                         QStringLiteral("probe-instance"));

    return app.exec();
}
