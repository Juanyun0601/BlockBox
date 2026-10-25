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
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <cstdio>

#define private public
#include "utils/GameLauncher.h"
#undef private

#include "utils/SettingsManager.h"

// 运行依赖管理器只影响「未检测到 Java」时的提示文案，探针不链接它的实现
#include "utils/RuntimeDependencyManager.h"
bool RuntimeDependencyManager::isSupportedPlatform() { return false; }

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0); // 崩溃时也能看到已打印的进度
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

    // 2b) 缓存为空时先播种一个已知 Java，避免 findBestJavaVersion 触发全盘扫描
    //     （扫描路径与本探针要验证的版本号规则无关，且在无 GUI 环境下会崩）
    const auto savedInstallations = installations;
    bool seeded = false;
    if (installations.isEmpty()) {
        const QString knownJava = QStringLiteral("C:/Program Files/Microsoft/jdk-21.0.8.9-hotspot/bin/java.exe");
        if (QFile::exists(knownJava)) {
            SettingsManager::instance()->setJavaInstallations({ qMakePair(knownJava, QStringLiteral("21.0.8")) });
            seeded = true;
            std::printf("\n[2b] 缓存为空，已播种 Java 21.0.8 用于后续用例（结束时会还原）\n");
        }
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

    // 4b) 版本号规则 → Java 要求（旧规则 1.x / 新规则 26.x / 快照 / 官方 javaVersion 提示）
    std::printf("\n[4b] javaRequirementFor 版本号规则:\n");
    {
        struct Case { const char *ver; int hint; int minJ; int maxJ; };
        static const Case cases[] = {
            { "1.5.2",  0,  0,   8 },
            { "1.7.10", 0,  8,   8 },
            { "1.12.2", 0,  8,   8 },
            { "1.16.5", 0,  8, 999 },
            { "1.17.1", 0, 16, 999 },
            { "1.20.4", 0, 17, 999 },
            { "1.20.5", 0, 21, 999 },
            { "1.21.11", 0, 21, 999 },
            { "26.1",   0, 25, 999 },
            { "26.1.1", 0, 25, 999 },   // 曾被 "1.1" 误判成 1.1 → 压到 Java 8
            { "26.1.2", 0, 25, 999 },
            { "26.2",   0, 25, 999 },
            { "26.4-snapshot-5", 0, 25, 999 },
            { "26.3-rc-1",       0, 25, 999 },
            { "23w14a", 0, 17, 999 },
            { "1.12.2", 8,  8,   8 },
            { "1.21.11", 21, 21, 999 },
            { "26.1.1", 25, 25, 999 },
            { "自定义整合包", 0, 8, 999 },
        };
        int pass = 0, fail = 0;
        for (const Case &c : cases) {
            const GameLauncher::JavaRequirement req =
                GameLauncher::javaRequirementFor(QString::fromUtf8(c.ver), c.hint);
            const bool ok = (req.minJava == c.minJ && req.maxJava == c.maxJ);
            std::printf("    %-18s hint=%-2d => %-9s %s\n", c.ver, c.hint,
                        qPrintable(req.rangeDesc()), ok ? "OK" : "MISMATCH");
            if (ok) ++pass; else ++fail;
        }
        std::printf("    %s (pass=%d fail=%d)\n",
                    fail == 0 ? "[PASS] 全部通过" : "[FAIL] 存在不一致", pass, fail);
    }

    // 5) 兜底检测
    std::printf("\n[5] detectJava():\n");
    {
        QElapsedTimer t; t.start();
        GameLauncher::JavaInfo info = GameLauncher::instance()->detectJava();
        std::printf("    => valid=%d version='%s' path='%s'  耗时=%lldms\n",
                    int(info.valid), qPrintable(info.version), qPrintable(info.path), t.elapsed());
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

    // 7b) 新版本号规则（26.x）的 resolveVersionId 解析（合成版本 JSON，不依赖真实实例）
    std::printf("\n[7b] resolveVersionId 新版本号规则:\n");
    {
        const QString root = QDir::tempPath() + QStringLiteral("/blockbox_ver_probe");
        QDir(root).removeRecursively();
        QDir().mkpath(root);
        struct Case { const char *dirName; const char *json; const char *expect; };
        static const Case cases[] = {
            { "26.1", "{\"id\":\"26.1\",\"javaVersion\":{\"component\":\"java-runtime-epsilon\",\"majorVersion\":25},\"assetIndex\":{\"id\":\"30\"}}", "26.1" },
            { "26.1.1", "{\"id\":\"26.1.1\",\"javaVersion\":{\"majorVersion\":25}}", "26.1.1" },
            { "26.4-snapshot-5", "{\"id\":\"26.4-snapshot-5\",\"javaVersion\":{\"majorVersion\":25}}", "26.4-snapshot-5" },
            { "fabric-26.1", "{\"id\":\"fabric-loader-0.16.9-26.1\",\"inheritsFrom\":\"26.1\"}", "26.1" },
            { "fabric-noloader", "{\"id\":\"26.1-Fabric 0.16.9\"}", "26.1" },
            { "forge-1.21.11", "{\"id\":\"1.21.11-forge-54.1.0\"}", "1.21.11" },
            { "1.21.11", "{\"id\":\"1.21.11\"}", "1.21.11" },
        };
        int pass = 0, fail = 0;
        GameLauncher *gl = GameLauncher::instance();
        for (const Case &c : cases) {
            const QString dir = root + QLatin1Char('/') + QString::fromUtf8(c.dirName);
            QDir().mkpath(dir);
            QFile f(dir + QLatin1Char('/') + QString::fromUtf8(c.dirName) + QStringLiteral(".json"));
            if (f.open(QIODevice::WriteOnly)) { f.write(c.json); f.close(); }
            const QString got = gl->resolveVersionId(dir);
            const bool ok = (got == QString::fromUtf8(c.expect));
            std::printf("    %-16s => %-16s %s\n", c.dirName, qPrintable(got), ok ? "OK" : "MISMATCH");
            if (ok) ++pass; else ++fail;
        }
        QDir(root).removeRecursively();
        std::printf("    %s (pass=%d fail=%d)\n",
                    fail == 0 ? "[PASS] 全部通过" : "[FAIL] 存在不一致", pass, fail);
    }

    // 5b) findAllJavaInstallations — 设置页 Java 下拉框的数据源
    //     （放在最后：无 GUI 环境下这一步的扫描路径可能崩，别挡住前面的用例）
    std::printf("\n[5b] findAllJavaInstallations()（设置页下拉框数据源）:\n");
    {
        QElapsedTimer t; t.start();
        const auto list = GameLauncher::instance()->findAllJavaInstallations();
        std::printf("    共 %d 个 Java (耗时 %lldms):\n", list.size(), t.elapsed());
        for (const auto &j : list) {
            std::printf("    - Java %s  %s\n", qPrintable(j.version), qPrintable(j.path));
        }
    }

    // 还原 2b 播种的 Java 缓存
    if (seeded) {
        SettingsManager::instance()->setJavaInstallations(savedInstallations);
        std::printf("\n[2b] 已还原原 Java 缓存（%d 项）\n", int(savedInstallations.size()));
    }

    std::printf("\nDONE\n");
    return 0;
}
