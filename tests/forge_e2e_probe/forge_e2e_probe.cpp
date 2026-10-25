/**
 * @file   forge_e2e_probe.cpp
 * @brief  Forge 安装端到端探针：真实 ForgeInstaller 全链路（无头）
 *
 * 走真实网络下载安装器 → 解析 install_profile → 下载处理器依赖 → 跑 processors
 * → 生成版本 JSON → 补齐库，全部信号带时间戳打到 stderr。
 *
 * 用法：forge_e2e_probe.exe [mc版本] [forge版本] [游戏根目录] [official|bmcl]
 * 退出码：0=安装成功且产物校验通过 1=installFailed 2=超时 3=完成但产物缺失
 */
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
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
#include "utils/forge/ForgeInstaller.h"

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

// 探针不链接整个 GameLauncher：用桩提供 getJavaPath 里引用的两个符号，
// 返回与主程序一致的选择结果（缓存清单里的 JDK 21）
GameLauncher *GameLauncher::m_instance = nullptr;
GameLauncher *GameLauncher::instance() { return nullptr; }
GameLauncher::JavaInfo GameLauncher::findBestJavaVersion(const QString &, int) { return {}; }

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    g_startMs = QDateTime::currentMSecsSinceEpoch();

    const QString mcVersion = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("26.3");
    const QString forgeVersion = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("66.1.0");
    const QString gameRoot = QDir::cleanPath(argc > 3 ? QString::fromLocal8Bit(argv[3])
                                                       : app.applicationDirPath() + QStringLiteral("/sandbox/.minecraft"));
    const bool useBmcl = !(argc > 4 && QByteArray(argv[4]) == "official");

    logLine("PROBE", QString("mc=%1 forge=%2 root=%3 source=%4")
                          .arg(mcVersion, forgeVersion, gameRoot,
                               useBmcl ? QStringLiteral("bmcl") : QStringLiteral("official")));

    QDir rootDir(gameRoot);
    if (!rootDir.exists())
        rootDir.mkpath(QStringLiteral("."));

    // 前置检查：原版客户端必须已就位（LoaderDetailPage 流程先装原版再装 Forge）
    const QString vanillaJar = gameRoot + QStringLiteral("/versions/%1/%1.jar").arg(mcVersion);
    if (!QFile::exists(vanillaJar))
    {
        logLine("FATAL", QString("原版 %1 不存在: %2").arg(mcVersion, vanillaJar));
        return 3;
    }
    logLine("PROBE", QString("vanilla jar OK: %1 (%2 bytes)")
                          .arg(vanillaJar).arg(QFileInfo(vanillaJar).size()));

    // 下载源与任务系统
    SettingsManager::instance()->setForgeDownloadSource(
        useBmcl ? ForgeDownloadSource::BMCL : ForgeDownloadSource::Official);
    // Java 用「自动选择」，专测 auto 解析路径（探针配置文件里默认就是 auto）
    logLine("PROBE", QString("java/path setting = %1").arg(SettingsManager::instance()->getJavaPath()));

    const QString taskId = DownloadTaskManager::instance()->addTask(
        QStringLiteral("ForgeE2E"), gameRoot, mcVersion,
        QStringList{QStringLiteral("Forge %1").arg(forgeVersion)});
    logLine("PROBE", QString("taskId=%1").arg(taskId));

    int exitCode = 2;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &watchdog, [&]() {
        logLine("FATAL", QStringLiteral("总超时（15 分钟），判卡死"));
        std::exit(2);
    });
    watchdog.start(15 * 60 * 1000);

    auto *installer = ForgeInstaller::instance();
    installer->setCurrentTaskId(taskId);
    QObject::connect(installer, &ForgeInstaller::installProgress,
                     &app, [&](const QString &, const QString &stage, int percent) {
        logLine("PROG ", QString("%1% %2").arg(percent).arg(stage));
    });
    QObject::connect(installer, &ForgeInstaller::installCompleted,
                     &app, [&](const QString &) {
        logLine("DONE ", QStringLiteral("installCompleted — 校验产物"));

        const QString versionId = QString(QStringLiteral("%1-forge-%2")).arg(mcVersion, forgeVersion);
        const QString vdir = gameRoot + QStringLiteral("/versions/") + versionId;
        const QString jsonPath = vdir + QStringLiteral("/") + versionId + QStringLiteral(".json");
        const QString jarPath = vdir + QStringLiteral("/") + versionId + QStringLiteral(".jar");

        bool ok = true;
        if (!QFile::exists(jsonPath)) { logLine("MISS", jsonPath); ok = false; }
        if (!QFile::exists(jarPath))  { logLine("MISS", jarPath); ok = false; }

        if (ok)
        {
            // JSON: id / inheritsFrom / mainClass 必须非空
            QFile jf(jsonPath);
            jf.open(QIODevice::ReadOnly);
            const QJsonObject obj = QJsonDocument::fromJson(jf.readAll()).object();
            jf.close();
            const QString id = obj.value(QStringLiteral("id")).toString();
            const QString inh = obj.value(QStringLiteral("inheritsFrom")).toString();
            const QString mc = obj.value(QStringLiteral("mainClass")).toString();
            logLine("JSON", QString("id=%1 inheritsFrom=%2 mainClass=%3 libraries=%4")
                       .arg(id, inh, mc).arg(obj.value(QStringLiteral("libraries")).toArray().size()));
            // 安装收尾会把原版目录并进来（自包含，inheritsFrom 被去掉），
            // 所以 inheritsFrom 为空时用 clientVersion 判定合并是否生效
            const bool selfContained =
                !obj.value(QStringLiteral("clientVersion")).toString().isEmpty();
            if (id.isEmpty() || mc.isEmpty() || (inh.isEmpty() && !selfContained)) ok = false;

            // patched client jar 必须存在（binarypatcher 产物）
            const QString patched = gameRoot + QStringLiteral("/libraries/net/minecraftforge/forge/%1-%2/forge-%1-%2-client.jar")
                                        .arg(mcVersion, forgeVersion);
            if (QFile::exists(patched))
                logLine("OK  ", QString("patched client jar: %1 bytes").arg(QFileInfo(patched).size()));
            else { logLine("MISS", patched); ok = false; }
        }

        logLine(ok ? "PASS" : "FAIL", ok ? QStringLiteral("端到端安装成功，产物齐全")
                                         : QStringLiteral("流程完成但产物缺失"));
        std::exit(ok ? 0 : 3);
    });
    QObject::connect(installer, &ForgeInstaller::installFailed,
                     &app, [&](const QString &, const QString &error) {
        logLine("FATAL", QStringLiteral("installFailed: %1").arg(error));
        std::exit(1);
    });

    logLine("PROBE", QStringLiteral("开始安装（游戏根目录作为实例根传入，模拟 LoaderDetailPage 调用）"));
    installer->downloadForgeInstaller(mcVersion, forgeVersion, gameRoot, QStringLiteral("probe-instance"));

    return app.exec();
}
