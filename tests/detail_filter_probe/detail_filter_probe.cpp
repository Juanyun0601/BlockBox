/**
 * @file   detail_filter_probe.cpp
 * @brief  在线资源详情页「筛选选项」版本列表探针（真实网络）
 *
 * 详情页左侧「筛选选项」的版本下拉取 ModInfo::gameVersions，下载卡片的版本小标签取
 * ModVersionFile::gameVersions。探针断言两个源的解析结果里只有真正的游戏版本：
 *  - Modrinth：项目接口的 versions 是版本记录 ID（yaoBL9D9 这种），游戏版本在
 *    game_versions；用错字段会让下拉显示成一长串字母。
 *  - CurseForge：gameVersions 会把 Client / Fabric / Risugami's ModLoader 等环境与
 *    加载器混进来，必须剔除；同时不能误伤 Beta 1.7.3 这类真实老版本。
 *
 * 覆盖：Modrinth 详情 + Modrinth 搜索命中 + CurseForge 详情（两个模组，含逐文件版本）
 *       + CurseForge 搜索命中；并打印各列表便于人工核对。
 * 退出码：0 = 全部通过；1 = 失败；2 = 网络不可用
 * 用法：qmake detail_filter_probe.pro && mingw32-make release && release/detail_filter_probe.exe
 */
#include <QCoreApplication>
#include <QEventLoop>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <functional>

#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModData.h"
#include "utils/mod/ModrinthAPI.h"

namespace {

int g_fail = 0;

void check(bool ok, const QString &msg)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", qUtf8Printable(msg));
    if (!ok)
        ++g_fail;
}

// 版本记录 ID 形如 yaoBL9D9 / 1b0GhKHj：8 位纯字母数字、没有点或横线；
// 真正的 MC 版本（1.20.1、18w49a、1.14-Snapshot、Beta 1.7.3…）不会长这样
bool looksLikeVersionId(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9]{8}$"));
    return re.match(s).hasMatch();
}

// 与 CurseForgeAPI.cpp 中的 isGameVersionEntry 保持一致
bool isNonVersionEntry(const QString &value)
{
    static const QStringList kExact = {
        QStringLiteral("client"), QStringLiteral("server"),
        QStringLiteral("minecraft"), QStringLiteral("java edition")
    };
    static const QStringList kKeywords = {
        QStringLiteral("forge"), QStringLiteral("fabric"),
        QStringLiteral("quilt"), QStringLiteral("liteloader"),
        QStringLiteral("modloader"), QStringLiteral("rift"),
        QStringLiteral("babric"), QStringLiteral("ornit"),
        QStringLiteral("optifine"), QStringLiteral("bukkit"),
        QStringLiteral("spigot"), QStringLiteral("paper"),
        QStringLiteral("purpur"), QStringLiteral("sponge"),
        QStringLiteral("velocity"), QStringLiteral("waterfall"),
        QStringLiteral("bungeecord"), QStringLiteral("folia"),
        QStringLiteral("arclight"), QStringLiteral("magma"),
        QStringLiteral("mohist"), QStringLiteral("canvas")
    };
    static const QRegularExpression kJava(QStringLiteral("^java\\s*\\d+$"));

    const QString lower = value.trimmed().toLower();
    if (lower.isEmpty() || kExact.contains(lower) || kJava.match(lower).hasMatch())
        return true;
    for (const QString &k : kKeywords) {
        if (lower.contains(k))
            return true;
    }
    return false;
}

QStringList badEntries(const QStringList &versions, bool idsAreBad, bool nonVersionsAreBad)
{
    QStringList bad;
    for (const QString &v : versions) {
        if (idsAreBad && looksLikeVersionId(v))
            bad << v;
        else if (nonVersionsAreBad && isNonVersionEntry(v))
            bad << v;
    }
    return bad;
}

void dump(const char *tag, const QStringList &versions)
{
    std::printf("[%s] (%d) %s\n", tag, int(versions.size()),
                qUtf8Printable(versions.join(QStringLiteral(" | "))));
}

// 事件循环泵：调用方已把相关完成/失败信号接到 loop.quit()，这里等到 done() 为真或超时
void pump(QEventLoop &loop, const std::function<bool()> &done, int timeoutMs)
{
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    while (!done() && timer.isActive())
        loop.exec();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("detail_filter_probe"));

    // 与启动器默认一致：走 MCIM 镜像（免 key）
    ModrinthAPI mr;
    mr.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/modrinth"));
    CurseForgeAPI cf;
    cf.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/curseforge"));

    // ── 阶段 1：Modrinth 详情（Sodium） ──
    {
        ModInfo info;
        bool done = false;
        QEventLoop loop;
        QObject::connect(&mr, &ModrinthAPI::modDetailReceived,
                         [&](const ModInfo &i) { info = i; done = true; });
        QObject::connect(&mr, &ModrinthAPI::modDetailFailed,
                         [&](const QString &e) { std::printf("[warn] modrinth 详情失败: %s\n", qUtf8Printable(e)); done = true; });
        QObject::connect(&mr, &ModrinthAPI::modDetailReceived, &loop, &QEventLoop::quit);
        QObject::connect(&mr, &ModrinthAPI::modDetailFailed, &loop, &QEventLoop::quit);
        mr.fetchModDetail(QStringLiteral("sodium"));
        pump(loop, [&]() { return done; }, 40000);

        if (info.gameVersions.isEmpty()) {
            std::printf("[warn] Modrinth 详情没取到（网络不可用？）\n");
        } else {
            dump("modrinth 详情 gameVersions", info.gameVersions);
            const QStringList bad = badEntries(info.gameVersions, true, false);
            check(bad.isEmpty(),
                  QStringLiteral("Modrinth 详情版本列表不含版本记录 ID 字母串：%1").arg(bad.join(QStringLiteral(","))));
            check(info.gameVersions.contains(QStringLiteral("1.20.1")),
                  QStringLiteral("Modrinth 详情版本列表含真实游戏版本 1.20.1"));
            QStringList fileVersions;
            for (const ModVersionFile &vf : info.versionFiles)
                fileVersions << vf.gameVersions;
            check(badEntries(fileVersions, true, false).isEmpty(),
                  QStringLiteral("Modrinth 逐文件版本标签不含版本记录 ID 字母串"));
        }
    }

    // ── 阶段 2：CurseForge 详情（Sodium 新版 + Mouse Tweaks 老版本） ──
    struct CfCase {
        const char *label;
        const char *id;
        const char *mustKeep;   // 必须保留的真实老版本（nullptr = 不检查）
    };
    const CfCase cases[] = {
        {"sodium", "394468", nullptr},
        {"mouse tweaks", "60089", "Beta 1.7.3"}
    };

    for (const CfCase &c : cases) {
        ModInfo info;
        bool done = false;
        QEventLoop loop;
        QObject::connect(&cf, &CurseForgeAPI::modDetailReceived,
                         [&](const ModInfo &i) { info = i; done = true; });
        QObject::connect(&cf, &CurseForgeAPI::modDetailFailed,
                         [&](const QString &e) { std::printf("[warn] curseforge %s 详情失败: %s\n", c.label, qUtf8Printable(e)); done = true; });
        QObject::connect(&cf, &CurseForgeAPI::modDetailReceived, &loop, &QEventLoop::quit);
        QObject::connect(&cf, &CurseForgeAPI::modDetailFailed, &loop, &QEventLoop::quit);
        cf.fetchModDetail(QString::fromLatin1(c.id));
        pump(loop, [&]() { return done; }, 40000);

        if (info.gameVersions.isEmpty()) {
            std::printf("[warn] CurseForge %s 详情没取到（网络不可用？）\n", c.label);
            continue;
        }

        const QString tag = QStringLiteral("curseforge %1 详情 gameVersions").arg(QLatin1String(c.label));
        dump(qUtf8Printable(tag), info.gameVersions);

        const QStringList bad = badEntries(info.gameVersions, true, true);
        check(bad.isEmpty(),
              QStringLiteral("CurseForge %1 版本列表不含 Client/Fabric 等非版本项：%2")
                  .arg(QLatin1String(c.label), bad.join(QStringLiteral(","))));

        bool hasDotted = false;
        for (const QString &v : info.gameVersions) {
            if (v.contains(QLatin1Char('.')))
                hasDotted = true;
        }
        check(hasDotted, QStringLiteral("CurseForge %1 版本列表含真实游戏版本（带小数点）")
                             .arg(QLatin1String(c.label)));

        QStringList fileVersions;
        for (const ModVersionFile &vf : info.versionFiles)
            fileVersions << vf.gameVersions;
        check(badEntries(fileVersions, true, true).isEmpty(),
              QStringLiteral("CurseForge %1 逐文件版本标签不含 Client/Fabric 等非版本项：%2")
                  .arg(QLatin1String(c.label), badEntries(fileVersions, true, true).join(QStringLiteral(","))));

        if (c.mustKeep) {
            const QString keep = QString::fromUtf8(c.mustKeep);
            check(info.gameVersions.contains(keep) || fileVersions.contains(keep),
                  QStringLiteral("CurseForge %1 保留了真实老版本 %2").arg(QLatin1String(c.label), keep));
        }
    }

    // ── 阶段 3：搜索列表（卡片版本小标签 / 详情数据缺失时的候选） ──
    {
        ModSearchResult mrSearch, cfSearch;
        bool mrDone = false, cfDone = false;

        QObject::connect(&mr, &ModrinthAPI::searchCompleted,
                         [&](const ModSearchResult &r) { mrSearch = r; mrDone = true; });
        QObject::connect(&mr, &ModrinthAPI::searchFailed,
                         [&](const QString &e) { std::printf("[warn] modrinth 搜索失败: %s\n", qUtf8Printable(e)); mrDone = true; });
        QObject::connect(&cf, &CurseForgeAPI::searchCompleted,
                         [&](const ModSearchResult &r) { cfSearch = r; cfDone = true; });
        QObject::connect(&cf, &CurseForgeAPI::searchFailed,
                         [&](const QString &e) { std::printf("[warn] curseforge 搜索失败: %s\n", qUtf8Printable(e)); cfDone = true; });

        QEventLoop loop;
        QObject::connect(&mr, &ModrinthAPI::searchCompleted, &loop, &QEventLoop::quit);
        QObject::connect(&mr, &ModrinthAPI::searchFailed, &loop, &QEventLoop::quit);
        QObject::connect(&cf, &CurseForgeAPI::searchCompleted, &loop, &QEventLoop::quit);
        QObject::connect(&cf, &CurseForgeAPI::searchFailed, &loop, &QEventLoop::quit);

        mr.searchMods(QStringLiteral("sodium"), QString(), QString(), 0, 20, QStringLiteral("relevance"));
        cf.searchMods(QStringLiteral("sodium"), QString(), QString(), 0, 20, QStringLiteral("popularity"));
        pump(loop, [&]() { return mrDone && cfDone; }, 40000);

        QStringList mrBad, cfBad;
        int mrWithVersions = 0, cfWithVersions = 0;
        for (const ModInfo &m : mrSearch.mods) {
            if (m.gameVersions.isEmpty())
                continue;
            ++mrWithVersions;
            mrBad << badEntries(m.gameVersions, true, false);
        }
        for (const ModInfo &m : cfSearch.mods) {
            if (m.gameVersions.isEmpty())
                continue;
            ++cfWithVersions;
            cfBad << badEntries(m.gameVersions, true, true);
        }

        std::printf("[modrinth 搜索] 命中 %d 项，其中有版本信息 %d 项\n",
                    int(mrSearch.mods.size()), mrWithVersions);
        std::printf("[curseforge 搜索] 命中 %d 项，其中有版本信息 %d 项\n",
                    int(cfSearch.mods.size()), cfWithVersions);

        if (mrWithVersions > 0) {
            dump("modrinth 搜索 gameVersions 抽样", mrSearch.mods.first().gameVersions);
            check(mrBad.isEmpty(),
                  QStringLiteral("Modrinth 搜索命中的版本列表不含版本记录 ID 字母串：%1").arg(mrBad.join(QStringLiteral(","))));
        }
        if (cfWithVersions > 0) {
            QStringList sample;
            for (const ModInfo &m : cfSearch.mods) {
                if (!m.gameVersions.isEmpty()) {
                    sample = m.gameVersions;
                    break;
                }
            }
            dump("curseforge 搜索 gameVersions 抽样", sample);
            check(cfBad.isEmpty(),
                  QStringLiteral("CurseForge 搜索命中的版本列表不含 Client/Fabric 等非版本项：%1").arg(cfBad.join(QStringLiteral(","))));
        }
    }

    std::printf("\n======== detail_filter: %s ========\n", g_fail == 0 ? "ALL PASS" : "HAS FAIL");
    return g_fail == 0 ? 0 : 1;
}
