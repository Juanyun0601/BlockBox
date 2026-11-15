/**
 * @file   lobby_fetch_probe.cpp
 * @brief  联机大厅「缺什么自动补什么」联网探针：真的去下载源找同名文件并下载
 *
 * 用真实的 Modrinth / CurseForge 接口跑一遍 LobbyContentFetcher，核对：
 *   1. 按房主给的文件名能找到对应项目，并按版本 / 加载器挑对文件；
 *   2. 下载真的落到实例的 mods / resourcepacks 目录里；
 *   3. 找不到的文件不会把整队卡死，会带一句提示继续下一个。
 *
 * 需要联网。输出目录：_tmp_fetch_probe/instance
 * 退出码 0 = 模组那一项下载成功。
 */
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <cstdio>

#include "utils/Lobby/LobbyContentFetcher.h"

namespace
{
int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-56s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString instancePath = QStringLiteral("_tmp_fetch_probe/instance");
    QDir().mkpath(instancePath + QStringLiteral("/mods"));
    QDir().mkpath(instancePath + QStringLiteral("/resourcepacks"));

    QList<LobbyFetchRequest> requests;
    LobbyFetchRequest mod;
    mod.type = ContentType::Mod;
    mod.fileName = QStringLiteral("sodium-fabric-0.5.11+mc1.20.1.jar");
    requests.append(mod);

    LobbyFetchRequest pack;
    pack.type = ContentType::ResourcePack;
    pack.optional = true;
    pack.fileName = QStringLiteral("Faithful-32x.zip");
    requests.append(pack);

    LobbyContentFetcher fetcher;
    int done = 0;
    QObject::connect(&fetcher, &LobbyContentFetcher::itemFinished, &app,
                     [&done](int, const LobbyFetchResult &result) {
                         ++done;
                         fprintf(stderr, "[probe] item %d %s <- %s (%s) %s\n", done,
                                 qPrintable(result.request.fileName),
                                 qPrintable(result.savedName),
                                 qPrintable(result.source), qPrintable(result.message));
                     });
    QObject::connect(&fetcher, &LobbyContentFetcher::progressChanged, &app,
                     [](int finished, int total, const QString &message) {
                         fprintf(stderr, "[probe] progress %d/%d %s\n", finished, total,
                                 qPrintable(message));
                     });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &app, [&app]() {
        fprintf(stderr, "[probe] TIMEOUT\n");
        app.exit(2);
    });

    QObject::connect(&fetcher, &LobbyContentFetcher::finished, &app,
                     [&app, &fetcher, instancePath](int ok, int fail) {
                         const QList<LobbyFetchResult> results = fetcher.results();
                         const bool modOk = !results.isEmpty() && results.at(0).ok;
                         expect("at least one file downloaded", ok >= 1);
                         expect("mod resolved and saved", modOk);
                         const bool modFileExists =
                             modOk
                             && QFileInfo::exists(instancePath + QStringLiteral("/mods/")
                                                  + results.at(0).savedName);
                         expect("mod file present in mods/", modFileExists);
                         if (results.size() > 1 && results.at(1).ok)
                         {
                             expect("resource pack present in resourcepacks/",
                                    QFileInfo::exists(instancePath
                                                      + QStringLiteral("/resourcepacks/")
                                                      + results.at(1).savedName));
                         }
                         fprintf(stderr, "[probe] finished ok=%d fail=%d\n", ok, fail);
                         app.exit(g_failures == 0 ? 0 : 1);
                     });

    guard.start(120000);
    fetcher.start(instancePath, QStringLiteral("1.20.1"), QStringLiteral("Fabric"), requests);
    if (!fetcher.isRunning())
    {
        fprintf(stderr, "[probe] fetcher did not start\n");
        return 1;
    }

    const int code = app.exec();
    return code;
}
