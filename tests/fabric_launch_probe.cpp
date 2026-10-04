/**
 * @file   fabric_launch_probe.cpp
 * @brief  Fabric 启动链路端到端探针
 *
 * 用真实代码路径验证:
 *   1) FabricInstaller::generateVersionJson 从 launcherMeta 生成的版本 JSON
 *      包含全部必需库（ASM/mixin/intermediary/loader），且为 Mojang 标准 downloads 格式
 *   2) FabricInstaller::repairIncompleteVersionJson 能修复旧版安装器生成的
 *      残缺 JSON（联网从 Fabric Meta 拉取 profile 并重写）
 *   3) GameLauncher::mergeInheritsFromJson + buildLaunchCommand 对 Fabric 版本
 *      产出可启动的完整命令（KnotClient 主类 / 完整 classpath / 原版参数继承）
 *
 * 链接真实的 GameLauncher / FabricInstaller 目标文件（private 改 public 的白盒访问）。
 */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>
#include <functional>

// ── 白盒访问: Qt 头已在上方以正常声明包含，此处临时放开访问控制 ──
#define private public
#include "utils/GameLauncher.h"
#include "utils/fabric/FabricInstaller.h"
#undef private

#include "utils/SettingsManager.h"

static int g_failures = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { std::printf("  [PASS] %s\n", msg); } \
        else { std::printf("  [FAIL] %s\n", msg); ++g_failures; } \
    } while (0)

static QJsonObject jsonFromFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
    return QJsonDocument::fromJson(f.readAll()).object();
}

static bool writeJson(const QString &path, const QJsonObject &obj)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

static bool argsContain(const QStringList &args, const QString &v)
{
    return args.contains(v);
}

// 构造最小原版 1.20.1 父版本 JSON（结构与 Mojang 官方一致的字段子集）
static QJsonObject makeVanillaParentJson()
{
    QJsonObject json;
    json["id"] = "1.20.1";
    json["type"] = "release";
    json["mainClass"] = "net.minecraft.client.main.Main";
    json["assets"] = "5";
    json["assetIndex"] = QJsonObject{ {"id", "5"}, {"url", "https://piston-meta.mojang.com/x.json"}, {"sha1", ""} };

    QJsonObject arguments;
    arguments["game"] = QJsonArray{
        "--username", "${auth_player_name}", "--version", "${version_name}",
        "--gameDir", "${game_directory}", "--assetsDir", "${assets_root}",
        "--assetIndex", "${assets_index_name}", "--uuid", "${auth_uuid}",
        "--accessToken", "${auth_access_token}", "--userType", "${user_type}",
        "--versionType", "${version_type}",
        QJsonObject{
            {"rules", QJsonArray{ QJsonObject{ {"action","allow"}, {"features", QJsonObject{ {"has_custom_resolution", true} }} }}},
            {"value", QJsonArray{"--width", "${resolution_width}", "--height", "${resolution_height}"}}
        }
    };
    arguments["jvm"] = QJsonArray{
        QJsonObject{
            {"rules", QJsonArray{ QJsonObject{ {"action","allow"}, {"os", QJsonObject{ {"name","windows"} } } }}},
            {"value", "-XX:HeapDumpPath=dump"}
        },
        QJsonObject{
            {"rules", QJsonArray{ QJsonObject{ {"action","allow"}, {"os", QJsonObject{ {"name","unix"} } } }}},
            {"value", "-Xss1M"}
        },
        "-Djava.library.path=${natives_directory}",
        "-Djna.tmpdir=${natives_directory}",
        "-Dminecraft.launcher.brand=${launcher_name}",
        "-cp", "${classpath}"
    };
    json["arguments"] = arguments;

    QJsonArray libs;
    libs.append(QJsonObject{
        {"name", "com.mojang:brigadier:1.1.8"},
        {"downloads", QJsonObject{ {"artifact", QJsonObject{
            {"path", "com/mojang/brigadier/1.1.8/brigadier-1.1.8.jar"},
            {"url", "https://libraries.minecraft.net/com/mojang/brigadier/1.1.8/brigadier-1.1.8.jar"},
            {"sha1", ""}, {"size", 100}} } }}
    });
    libs.append(QJsonObject{
        {"name", "org.lwjgl:lwjgl:3.3.1"},
        {"natives", QJsonObject{ {"windows", "natives-windows"} }},
        {"downloads", QJsonObject{
            {"artifact", QJsonObject{
                {"path", "org/lwjgl/lwjgl/3.3.1/lwjgl-3.3.1.jar"},
                {"url", "u"}, {"sha1", ""}, {"size", 1} }},
            {"classifiers", QJsonObject{ {"natives-windows", QJsonObject{
                {"path", "org/lwjgl/lwjgl/3.3.1/lwjgl-3.3.1-natives-windows.jar"},
                {"url", "u"}, {"sha1", ""}, {"size", 1} }} }} }}
    });
    json["libraries"] = libs;

    json["downloads"] = QJsonObject{ {"client", QJsonObject{ {"url",""}, {"sha1",""}, {"size",0} }} };
    return json;
}

// 旧版 FabricInstaller 生成的残缺 JSON（按旧实现复刻的关键字段）
static QJsonObject makeOldBrokenFabricJson()
{
    QJsonObject json;
    json["id"] = "fabric-loader-0.14.21-1.20.1";
    json["type"] = "release";
    json["mainClass"] = "net.fabricmc.loader.impl.launch.knot.KnotClient";
    json["arguments"] = QJsonObject{ {"game", QJsonArray{
        "--username", "${auth_player_name}", "--version", "${version_name}",
        "--gameDir", "${game_directory}", "--assetsDir", "${assets_root}",
        "--assetIndex", "${assets_index_name}", "--uuid", "${auth_uuid}",
        "--accessToken", "${auth_access_token}", "--userType", "${user_type}",
        "--versionType", "${version_type}", "--launchTarget", "fabric-client"}} };
    QJsonArray libs;
    libs.append(QJsonObject{
        {"name", "net.fabricmc:fabric-loader:0.14.21"},
        {"downloads", QJsonObject{ {"artifact", QJsonObject{
            {"url", "https://bmclapi2.bangbang93.com/v2/versions/loader/1.20.1/0.14.21/loader//fabric-loader-0.14.21-1.20.1.jar"},
            {"path", "net/fabricmc/fabric-loader/0.14.21/fabric-loader-0.14.21-1.20.1.jar"},
            {"sha1", ""}, {"size", 0} }} }}
    });
    libs.append(QJsonObject{
        {"name", "net.fabricmc:fabric-api:0.76.0+1.19"},
        {"url", "https://maven.fabricmc.net/"},
        {"downloads", QJsonObject{ {"artifact", QJsonObject{
            {"url", "https://maven.fabricmc.net/net/fabricmc/fabric-api/fabric-api-0.76.0+1.19.jar"},
            {"path", "net/fabricmc/fabric-api/fabric-api-0.76.0+1.19.jar"},
            {"sha1", ""}, {"size", 0} }} }}
    });
    libs.append(QJsonObject{ {"name", "com.mojang:minecraft:1.20.1"} });
    json["libraries"] = libs;
    json["clientVersion"] = "1.20.1";
    json["inheritsFrom"] = "1.20.1";
    return json;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) { std::printf("[FATAL] temp dir\n"); return 2; }
    const QString root = tempDir.path();           // .minecraft 根
    const QString versionsDir = root + "/versions";

    // ── 搭建实例树 ──
    QDir().mkpath(versionsDir + "/1.20.1");
    QDir().mkpath(root + "/libraries/com/mojang/brigadier/1.1.8");
    const QString parentDir = versionsDir + "/1.20.1";
    writeJson(parentDir + "/1.20.1.json", makeVanillaParentJson());
    { QFile jar(parentDir + "/1.20.1.jar"); jar.open(QIODevice::WriteOnly); jar.write("PK\x03\x04fake", 7); }
    { QFile lib(root + "/libraries/com/mojang/brigadier/1.1.8/brigadier-1.1.8.jar");
      lib.open(QIODevice::WriteOnly); lib.write("fake", 4); }

    const QString fabricDir = versionsDir + "/fabric-loader-0.15.11-1.20.1";
    QDir().mkpath(fabricDir);

    std::printf("=== 探针根目录: %s ===\n", qPrintable(root));

    // ── 1) 新版 generateVersionJson: 用真实 launcherMeta 结构生成版本 JSON ──
    std::printf("\n[1] FabricInstaller::generateVersionJson (launcherMeta → 版本 JSON)\n");
    {
        // fixture: 取自 meta.fabricmc.net /v2/versions/loader/1.20.1 的真实返回结构
        QJsonObject launcherMeta;
        launcherMeta["mainClass"] = QJsonObject{
            {"client", "net.fabricmc.loader.impl.launch.knot.KnotClient"},
            {"server", "net.fabricmc.loader.impl.launch.knot.KnotServer"} };
        QJsonObject libsMeta;
        auto asmLib = [](const char *a) {
            return QJsonObject{ {"name", QString("org.ow2.asm:%1:9.10.1").arg(a)},
                                {"url", "https://maven.fabricmc.net/"} };
        };
        libsMeta["client"] = QJsonArray{};
        libsMeta["common"] = QJsonArray{ asmLib("asm"), asmLib("asm-analysis"), asmLib("asm-commons"),
                                         asmLib("asm-tree"), asmLib("asm-util"),
                                         QJsonObject{ {"name","net.fabricmc:sponge-mixin:0.13.3+mixin.0.8.5"},
                                                      {"url","https://maven.fabricmc.net/"} },
                                         QJsonObject{ {"name","net.fabricmc:intermediary:1.20.1"},
                                                      {"url","https://maven.fabricmc.net/"} } };
        libsMeta["main"] = QJsonArray{ QJsonObject{ {"name","net.fabricmc:fabric-loader:0.15.11"},
                                                    {"url","https://maven.fabricmc.net/"} } };
        libsMeta["server"] = QJsonArray{};
        launcherMeta["libraries"] = libsMeta;

        FabricVersionInfo info;
        info.fabricVersion = "0.15.11";
        info.minecraftVersion = "1.20.1";
        info.intermediaryMaven = "net.fabricmc:intermediary:1.20.1";
        info.launcherMeta = launcherMeta;
        // 强制用官方 maven 路径生成，避免镜像源差异影响断言
        FabricInstaller::instance()->m_downloadSource = FabricDownloadSource::Official;

        QJsonObject generated = FabricInstaller::instance()->generateVersionJson(info);

        writeJson(fabricDir + "/fabric-loader-0.15.11-1.20.1.json", generated);

        CHECK(generated["mainClass"].toString() == "net.fabricmc.loader.impl.launch.knot.KnotClient",
              "mainClass = KnotClient");
        CHECK(generated["inheritsFrom"].toString() == "1.20.1", "inheritsFrom = 1.20.1");
        CHECK(!generated.contains("arguments"), "不写 arguments（继承原版参数）");

        QStringList names;
        for (const QJsonValue &v : generated["libraries"].toArray())
            names << v.toObject()["name"].toString();
        CHECK(names.contains("net.fabricmc:intermediary:1.20.1"), "包含 intermediary 映射库");
        CHECK(names.contains("net.fabricmc:sponge-mixin:0.13.3+mixin.0.8.5"), "包含 sponge-mixin");
        CHECK(names.contains("net.fabricmc:fabric-loader:0.15.11"), "包含 fabric-loader");
        CHECK(names.count(QString("org.ow2.asm:asm:9.10.1")) == 1, "包含 asm (无重复)");

        // 全部条目应为 Mojang 标准 downloads.artifact 格式且 URL 指向官方 maven
        bool allMojangFormat = true;
        for (const QJsonValue &v : generated["libraries"].toArray()) {
            QJsonObject lib = v.toObject();
            QString url = lib["downloads"].toObject()["artifact"].toObject()["url"].toString();
            QString path = lib["downloads"].toObject()["artifact"].toObject()["path"].toString();
            if (url.isEmpty() || path.isEmpty() || !url.startsWith("https://maven.fabricmc.net/"))
                allMojangFormat = false;
        }
        CHECK(allMojangFormat, "全部库条目为标准 downloads.artifact 格式");

        // 安装阶段真实下载运行库（首次启动跳过通用补全，库文件必须就位）
        QStringList dlFailed;
        const bool dlOk = FabricInstaller::instance()->downloadLibraries(
            generated, root + "/libraries", &dlFailed);
        CHECK(dlOk, "downloadLibraries 下载全部运行库成功");
    }

    // ── 2) 新版 JSON → merge → 启动命令 ──
    std::printf("\n[2] GameLauncher::mergeInheritsFromJson + buildLaunchCommand (新格式)\n");
    {
        GameLauncher *gl = GameLauncher::instance();
        QJsonObject raw = gl->readVersionJson(fabricDir);
        QJsonObject merged = gl->mergeInheritsFromJson(fabricDir, raw);

        CHECK(merged["mainClass"].toString() == "net.fabricmc.loader.impl.launch.knot.KnotClient",
              "合并后 mainClass 保留 KnotClient");
        CHECK(merged["assetIndex"].toObject()["id"].toString() == "5", "继承原版 assetIndex");
        CHECK(merged["arguments"].toObject()["jvm"].toArray().size() > 0, "继承原版 jvm 参数");
        CHECK(merged["arguments"].toObject()["game"].toArray().size() > 0, "继承原版 game 参数");

        GameLauncher::LaunchConfig cfg;
        cfg.instancePath = fabricDir;
        cfg.accountName = "ProbePlayer";
        QStringList args = gl->buildLaunchCommand(cfg);

        const int cpIdx = args.indexOf("-cp");
        CHECK(cpIdx >= 0 && cpIdx + 1 < args.size(), "存在 -cp 参数");
        const QString cp = (cpIdx >= 0 && cpIdx + 1 < args.size()) ? args[cpIdx + 1] : QString();
        CHECK(cp.contains("net/fabricmc/fabric-loader/0.15.11/fabric-loader-0.15.11.jar"),
              "classpath 含 fabric-loader");
        CHECK(cp.contains("net/fabricmc/intermediary/1.20.1/intermediary-1.20.1.jar"),
              "classpath 含 intermediary");
        CHECK(cp.contains("net/fabricmc/sponge-mixin/"), "classpath 含 sponge-mixin");
        CHECK(cp.contains("org/ow2/asm/asm/9.10.1/asm-9.10.1.jar"), "classpath 含 asm");
        CHECK(cp.contains("com/mojang/brigadier/1.1.8/brigadier-1.1.8.jar"), "classpath 含原版库 brigadier");
        CHECK(cp.contains("1.20.1/1.20.1.jar"), "classpath 含原版客户端 JAR");
        CHECK(!cp.contains("fabric-loader-0.15.11-1.20.1.jar"), "classpath 不含旧版错误路径的 loader JAR");

        CHECK(argsContain(args, "net.fabricmc.loader.impl.launch.knot.KnotClient"), "主类 = KnotClient");
        CHECK(argsContain(args, "--username"), "game 参数含 --username");
        CHECK(argsContain(args, "--assetsDir"), "game 参数含 --assetsDir");
        CHECK(argsContain(args, "--assetIndex"), "game 参数含 --assetIndex");
        const int assetIdxIdx = args.indexOf("--assetIndex");
        CHECK(assetIdxIdx >= 0 && assetIdxIdx + 1 < args.size() && args[assetIdxIdx + 1] == "5",
              "--assetIndex 值继承原版 (5)");
        bool hasLibraryPath = false;
        for (const QString &a : args)
            if (a.startsWith("-Djava.library.path=")) hasLibraryPath = true;
        CHECK(hasLibraryPath, "jvm 参数含 -Djava.library.path");
        CHECK(!argsContain(args, "--launchTarget"), "不含多余的 --launchTarget");

        std::printf("\n--- 启动命令预览 ---\n  java");
        for (const QString &a : args) {
            std::printf(" %s", qPrintable(a.size() > 160 ? a.left(160) + "..." : a));
        }
        std::printf("\n");
    }

    // ── 3) 旧版残缺 JSON 自愈: repairIncompleteVersionJson (联网) ──
    std::printf("\n[3] FabricInstaller::repairIncompleteVersionJson (修复旧版残缺 JSON)\n");
    {
        const QString brokenDir = versionsDir + "/fabric-loader-0.14.21-1.20.1";
        QDir().mkpath(brokenDir);
        QJsonObject broken = makeOldBrokenFabricJson();
        broken["id"] = "fabric-loader-0.14.21-1.20.1";
        writeJson(brokenDir + "/fabric-loader-0.14.21-1.20.1.json", broken);

        const bool ok = FabricInstaller::repairIncompleteVersionJson(brokenDir,
            [](const QString &m) { std::printf("    [log] %s\n", qPrintable(m)); });
        CHECK(ok, "修复流程执行完成");

        QJsonObject fixed = jsonFromFile(brokenDir + "/fabric-loader-0.14.21-1.20.1.json");
        QStringList names;
        for (const QJsonValue &v : fixed["libraries"].toArray())
            names << v.toObject()["name"].toString();

        CHECK(names.contains("net.fabricmc:intermediary:1.20.1"), "修复后包含 intermediary");
        CHECK(!names.contains("net.fabricmc:fabric-api:0.76.0+1.19"), "移除了无效的 fabric-api 库");
        CHECK(!names.contains("com.mojang:minecraft:1.20.1"), "移除了无效的 minecraft 库条目");

        // 修复后的库文件应已下载到 <root>/libraries（首次启动跳过补全，必须就位）
        const QString loaderJar = root + "/libraries/net/fabricmc/fabric-loader/0.14.21/fabric-loader-0.14.21.jar";
        CHECK(QFile::exists(loaderJar), "fabric-loader 库文件已下载");

        // ── 4) 修复后的 JSON → 启动命令 ──
        std::printf("\n[4] 修复后的版本 → 启动命令\n");
        GameLauncher *gl = GameLauncher::instance();
        GameLauncher::LaunchConfig cfg;
        cfg.instancePath = brokenDir;
        QStringList args = gl->buildLaunchCommand(cfg);
        const int cpIdx = args.indexOf("-cp");
        const QString cp = (cpIdx >= 0 && cpIdx + 1 < args.size()) ? args[cpIdx + 1] : QString();
        CHECK(cp.contains("net/fabricmc/fabric-loader/0.14.21/fabric-loader-0.14.21.jar"),
              "classpath 含正确路径的 fabric-loader");
        CHECK(cp.contains("net/fabricmc/intermediary/1.20.1/intermediary-1.20.1.jar"),
              "classpath 含 intermediary");
        CHECK(argsContain(args, "net.fabricmc.loader.impl.launch.knot.KnotClient"), "主类 = KnotClient");
        CHECK(argsContain(args, "--username"), "game 参数含 --username");
    }

    std::printf("\n=== 结果: %s (失败 %d 项) ===\n", g_failures == 0 ? "全部通过" : "存在失败", g_failures);
    return g_failures == 0 ? 0 : 1;
}
