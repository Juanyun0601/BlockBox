/**
 * @file   live_merge_probe.cpp
 * @brief  跨源合并端到端联调探针（真实网络）：Modrinth/CurseForge API 搜索 → mergeCrossSourceResults
 *
 * 用同一关键词分别请求两源（MCIM 镜像，免 key），等待双方返回后调用
 * mergeCrossSourceResults 合并，打印合并对与未合并项，断言：
 *  - 至少合并出 1 对同名资源（以 sodium 等通用词为关键词时必然存在）
 *  - 合并卡片主源为 modrinth、下载量为两源之和、alt* 字段完整
 *  - switchModInfoSource 往返一致
 * 用法：qmake live_merge_probe.pro && mingw32-make release && release/live_merge_probe.exe
 */
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cstdio>

#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModrinthAPI.h"
#include "utils/mod/ModData.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("live_merge_probe"));

    const QString query = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                   : QStringLiteral("sodium");

    CurseForgeAPI cf;
    cf.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/curseforge"));
    ModrinthAPI mr;
    mr.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/modrinth"));

    ModSearchResult cfRes, mrRes;
    bool cfDone = false, mrDone = false, cfFail = false, mrFail = false;

    QObject::connect(&cf, &CurseForgeAPI::searchCompleted,
                     [&](const ModSearchResult &r) { cfRes = r; cfDone = true; });
    QObject::connect(&cf, &CurseForgeAPI::searchFailed,
                     [&](const QString &e) { qWarning("cf failed: %s", qUtf8Printable(e)); cfFail = true; cfDone = true; });
    QObject::connect(&mr, &ModrinthAPI::searchCompleted,
                     [&](const ModSearchResult &r) { mrRes = r; mrDone = true; });
    QObject::connect(&mr, &ModrinthAPI::searchFailed,
                     [&](const QString &e) { qWarning("mr failed: %s", qUtf8Printable(e)); mrFail = true; mrDone = true; });

    cf.searchMods(query, QString(), QString(), 0, 20, QStringLiteral("popularity"));
    mr.searchMods(query, QString(), QString(), 0, 20, QStringLiteral("relevance"));

    QEventLoop wait;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &wait, &QEventLoop::quit);
    auto check = [&]() {
        if ((cfDone && mrDone) || (cfDone && mrFail) || (mrDone && cfFail))
            wait.quit();
    };
    QObject::connect(&cf, &CurseForgeAPI::searchCompleted, check);
    QObject::connect(&cf, &CurseForgeAPI::searchFailed, check);
    QObject::connect(&mr, &ModrinthAPI::searchCompleted, check);
    QObject::connect(&mr, &ModrinthAPI::searchFailed, check);
    timeout.start(30000);
    wait.exec();

    if (cfFail || mrFail) {
        std::fprintf(stderr, "[FAIL] 网络检索失败 cfFail=%d mrFail=%d\n", cfFail, mrFail);
        return 2;
    }

    std::fprintf(stderr, "[info] cf=%d 项, mr=%d 项\n",
                 cfRes.mods.size(), mrRes.mods.size());

    const QList<ModInfo> merged = mergeCrossSourceResults(mrRes.mods, cfRes.mods);
    std::fprintf(stderr, "[info] merged=%d 项\n", merged.size());

    int pairs = 0;
    for (const ModInfo &m : merged) {
        if (!m.isCrossSourceMerged())
            continue;
        ++pairs;
        std::fprintf(stderr, "[pair] %s | mr=%s(%lld) + cf=%s(%lld) => %lld | alt=%s/%s\n",
                     qUtf8Printable(m.name),
                     qUtf8Printable(m.id), m.downloadCount - m.altDownloadCount,
                     qUtf8Printable(m.altId), m.altDownloadCount,
                     m.downloadCount,
                     qUtf8Printable(m.altSource), qUtf8Printable(m.altName));
        if (m.source != QLatin1String("modrinth") || m.altSource != QLatin1String("curseforge")
            || m.altId.isEmpty() || m.downloadCount <= m.altDownloadCount) {
            std::fprintf(stderr, "[FAIL] 合并对字段异常: %s\n", qUtf8Printable(m.name));
            return 1;
        }
        const ModInfo swapped = switchModInfoSource(m);
        if (swapped.source != QLatin1String("curseforge") || swapped.id != m.altId
            || switchModInfoSource(swapped).id != m.id) {
            std::fprintf(stderr, "[FAIL] 换源往返异常: %s\n", qUtf8Printable(m.name));
            return 1;
        }
    }

    std::fprintf(stderr, "[done] pairs=%d\n", pairs);
    if (pairs == 0) {
        std::fprintf(stderr, "[FAIL] 未合并出任何同名对（关键词 %s）\n", qUtf8Printable(query));
        return 1;
    }
    std::fprintf(stderr, "LIVE MERGE PROBE PASSED\n");
    return 0;
}
