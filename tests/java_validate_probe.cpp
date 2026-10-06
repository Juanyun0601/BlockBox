/**
 * @file   java_validate_probe.cpp
 * @brief  Java 检测链路探针 — 复现启动器的 Java 验证/选择流程
 *
 * 依次执行并计时:
 *   1) SettingsManager::getJavaPath()          — 全局 Java 设置
 *   2) SettingsManager::getJavaInstallations() — Java 缓存
 *   3) GameLauncher::validateJavaPath()        — 逐个验证缓存 Java（含耗时）
 *   4) GameLauncher::findBestJavaVersion()     — 按游戏版本选 Java
 *   5) GameLauncher::detectJava()              — PATH/注册表兜底检测
 *   6) preCheck 同款路径校验 — 复现「无效的Java路径」判定
 */

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <cstdio>

#define private public
#include "utils/GameLauncher.h"
#undef private

#include "utils/SettingsManager.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    std::printf("appDir: %s\n", qPrintable(QCoreApplication::applicationDirPath()));

    // 1) 全局 Java 设置
    const QString javaPath = SettingsManager::instance()->getJavaPath();
    std::printf("\n[1] getJavaPath() = '%s'\n", qPrintable(javaPath));

    // 2) Java 缓存
    const auto installations = SettingsManager::instance()->getJavaInstallations();
    std::printf("\n[2] getJavaInstallations() = %d 项\n", installations.size());
    for (const auto &inst : installations) {
        std::printf("    path=%s\n    ver =%s  exists=%d\n",
                    qPrintable(inst.first), qPrintable(inst.second),
                    int(QFile::exists(inst.first)));
    }

    // 3) 逐个验证（含耗时 — 验证 2 秒超时是否是问题根源）
    std::printf("\n[3] validateJavaPath 逐个验证:\n");
    {
        GameLauncher *gl = GameLauncher::instance();
        for (const auto &inst : installations) {
            QElapsedTimer t;
            t.start();
            GameLauncher::JavaInfo info;
            const bool ok = gl->validateJavaPath(inst.first, info);
            std::printf("    %s  valid=%d version='%s'  耗时=%lldms\n",
                        qPrintable(QFileInfo(inst.first).fileName()),
                        int(ok), qPrintable(info.version), t.elapsed());
        }

        // 3b) PATH 上的 java（preCheck 之外 validateJavaPath("java") 的场景）
        QElapsedTimer t2; t2.start();
        GameLauncher::JavaInfo info2;
        const bool ok2 = gl->validateJavaPath(QStringLiteral("java"), info2);
        std::printf("    PATH 'java'  valid=%d version='%s'  耗时=%lldms\n",
                    int(ok2), qPrintable(info2.version), t2.elapsed());
    }

    // 4) 按游戏版本自动选择（当前实例: 长梦镇 → 1.21.11）
    std::printf("\n[4] findBestJavaVersion(\"1.21.11\"):\n");
    {
        GameLauncher *gl = GameLauncher::instance();
        QObject::connect(gl, &GameLauncher::launchDetailAdded, [](const QString &m) {
            std::printf("    [detail] %s\n", qPrintable(m));
        });
        GameLauncher::JavaInfo best = gl->findBestJavaVersion(QStringLiteral("1.21.11"));
        std::printf("    => valid=%d version='%s' path='%s'\n",
                    int(best.valid), qPrintable(best.version), qPrintable(best.path));
    }

    // 5) 兜底检测
    std::printf("\n[5] detectJava():\n");
    {
        QElapsedTimer t; t.start();
        GameLauncher::JavaInfo info = GameLauncher::instance()->detectJava();
        std::printf("    => valid=%d version='%s' path='%s'  耗时=%lldms\n",
                    int(info.valid), qPrintable(info.version), qPrintable(info.path), t.elapsed());
    }

    // 5b) findAllJavaInstallations — 设置页 Java 下拉框的数据源
    std::printf("\n[5b] findAllJavaInstallations()（设置页下拉框数据源）:\n");
    {
        QElapsedTimer t; t.start();
        const auto list = GameLauncher::instance()->findAllJavaInstallations();
        std::printf("    共 %d 个 Java (耗时 %lldms):\n", list.size(), t.elapsed());
        for (const auto &j : list) {
            std::printf("    - Java %s  %s\n", qPrintable(j.version), qPrintable(j.path));
        }
    }

    // 6) preCheck 同款校验（指定的 javaPath 非空时）
    if (javaPath != QStringLiteral("auto") && !javaPath.isEmpty()) {
        std::printf("\n[6] preCheck 同款校验 '%s':\n", qPrintable(javaPath));
        GameLauncher::JavaInfo info;
        const bool ok = GameLauncher::instance()->validateJavaPath(javaPath, info);
        std::printf("    => valid=%d version='%s'\n", int(ok), qPrintable(info.version));
        std::printf("    %s\n", ok ? "    preCheck 将通过" : "    preCheck 将报「无效的Java路径」");
    } else {
        std::printf("\n[6] 全局设置为 auto，跳过 preCheck 指定路径校验\n");
    }

    // 7) 真实实例启动命令校验: fabric_launch_probe.exe <实例路径>
    if (argc > 1) {
        const QString instancePath = QString::fromLocal8Bit(argv[1]);
        std::printf("\n[7] 真实实例启动命令: %s\n", qPrintable(instancePath));
        GameLauncher *gl = GameLauncher::instance();
        const QString gameVersion = gl->resolveVersionId(instancePath);
        std::printf("    resolveVersionId => %s\n", qPrintable(gameVersion));
        GameLauncher::JavaInfo best = gl->findBestJavaVersion(gameVersion);
        std::printf("    findBestJavaVersion => %s (%s)\n",
                    qPrintable(best.version), qPrintable(best.path));

        GameLauncher::LaunchConfig cfg;
        cfg.instancePath = instancePath;
        cfg.accountName = "ProbePlayer";
        QStringList args = gl->buildLaunchCommand(cfg);
        const int cpIdx = args.indexOf("-cp");
        const QString cp = (cpIdx >= 0 && cpIdx + 1 < args.size()) ? args[cpIdx + 1] : QString();
        const QStringList cpEntries = cp.split(';', Qt::SkipEmptyParts);
        std::printf("    classpath 条目数: %d\n", cpEntries.size());
        // 客户端 JAR 必须在 classpath 上（版本 id 命名 / 原版版本号命名均可）
        const QString dirName = QFileInfo(instancePath).fileName();
        bool hasClientJar = false;
        for (const QString &e : cpEntries) {
            if (e.endsWith(QStringLiteral(".jar")) && !e.contains(QStringLiteral("/libraries/"))
                && (e.contains(dirName) || e.contains(gameVersion))) {
                std::printf("    客户端 JAR: %s\n", qPrintable(e));
                hasClientJar = true;
            }
        }
        std::printf("    %s\n", hasClientJar ? "    [PASS] classpath 包含客户端 JAR"
                                            : "    [FAIL] classpath 缺失客户端 JAR");
        const int mcIdx = args.indexOf(QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient"));
        std::printf("    mainClass: %s\n", mcIdx >= 0 ? "KnotClient (Fabric)" : "其他");
    }

    std::printf("\nDONE\n");
    return 0;
}
