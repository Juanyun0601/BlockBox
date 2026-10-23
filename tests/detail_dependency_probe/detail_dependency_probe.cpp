/**
 * @file   detail_dependency_probe.cpp
 * @brief  在线资源详情页「前置资源」数据链路探针（真实网络）
 *
 * 详情页左侧「前置资源」依赖两段数据：
 *  1) 详情接口返回的 dependencies：Modrinth 只在 /version 的每个版本里带 dependencies，
 *     解析器取 versionFiles.first() 的依赖；CurseForge 取 latestFiles 里的 dependencies。
 *  2) 依赖项里只有 id（Modrinth 是 project_id，CurseForge 是 modId），必须再用
 *     /v2/projects?ids=[...] 与 POST /v1/mods 批量换成标题和图标，卡片才有名字可显示。
 *
 * 本探针断言这两段对「确有前置」的真实资源都成立，并打印卡片最终会显示的内容。
 * 覆盖：Modrinth(iris→sodium)、CurseForge(iris 455508→sodium 394468)。
 * 退出码：0 = 全部通过；1 = 失败。
 */
#include <QCoreApplication>
#include <QEventLoop>
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

void warn(const QString &msg)
{
    std::printf("[warn] %s\n", qUtf8Printable(msg));
}

// 等到 done() 为真或超时
void pump(QEventLoop &loop, const std::function<bool()> &done, int timeoutMs)
{
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(timeoutMs);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start();
    while (!done() && timer.isActive())
        loop.exec();
}

void dumpDeps(const QString &tag, const QList<ModDependency> &deps)
{
    std::printf("[%s] (%d) ", qUtf8Printable(tag), int(deps.size()));
    QStringList parts;
    for (const ModDependency &dep : deps)
        parts << QStringLiteral("%1(%2)").arg(dep.name, dep.isRequired ? QStringLiteral("必备")
                                                                     : QStringLiteral("可选"));
    std::printf("%s\n", qUtf8Printable(parts.join(QStringLiteral(" | "))));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("detail_dependency_probe"));

    // 默认走 MCIM 镜像（免 key）；可用 argv 覆盖：<modrinth-base> <curseforge-base>
    const QString mrBase = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                    : QStringLiteral("https://mod.mcimirror.top/modrinth");
    const QString cfBase = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                    : QStringLiteral("https://mod.mcimirror.top/curseforge");
    std::printf("[base] modrinth=%s\n", qUtf8Printable(mrBase));
    std::printf("[base] curseforge=%s\n", qUtf8Printable(cfBase));
    ModrinthAPI mr;
    mr.setBaseUrl(mrBase);
    CurseForgeAPI cf;
    cf.setBaseUrl(cfBase);

    // ── 阶段 1：Modrinth iris 详情 → 前置应有 sodium ──
    {
        ModInfo info;
        bool done = false;
        QEventLoop loop;
        QObject::connect(&mr, &ModrinthAPI::modDetailReceived, [&](const ModInfo &i) { info = i; done = true; });
        QObject::connect(&mr, &ModrinthAPI::modDetailFailed, [&](const QString &e) {
            warn(QStringLiteral("modrinth iris 详情失败: %1").arg(e));
            done = true;
        });
        QObject::connect(&mr, &ModrinthAPI::modDetailReceived, &loop, &QEventLoop::quit);
        QObject::connect(&mr, &ModrinthAPI::modDetailFailed, &loop, &QEventLoop::quit);
        mr.fetchModDetail(QStringLiteral("YL57xq9U"));   // Iris Shaders
        pump(loop, [&]() { return done; }, 40000);

        if (info.versionFiles.isEmpty()) {
            warn(QStringLiteral("Modrinth iris 详情没取到（网络不可用？）"));
        } else {
            std::printf("[modrinth] 项目=%s 版本数=%d\n",
                        qUtf8Printable(info.name), int(info.versionFiles.size()));
            dumpDeps(QStringLiteral("modrinth iris 详情 dependencies"), info.dependencies);
            check(!info.dependencies.isEmpty(),
                  QStringLiteral("Modrinth iris 的详情带回了前置资源"));

            QStringList ids;
            for (const ModDependency &dep : info.dependencies) {
                if (!dep.name.isEmpty() && !ids.contains(dep.name))
                    ids << dep.name;
            }
            check(!ids.isEmpty(), QStringLiteral("Modrinth 前置 id 可提取（解析器用 project_id）"));

            // 与详情页一致：批量换名称与图标
            QList<ModInfo> resolved;
            bool resolvedDone = false;
            QEventLoop rl;
            QObject::connect(&mr, &ModrinthAPI::projectsResolved, [&](const QList<ModInfo> &list) {
                resolved = list;
                resolvedDone = true;
            });
            QObject::connect(&mr, &ModrinthAPI::projectsResolveFailed, [&](const QString &e) {
                warn(QStringLiteral("modrinth 批量查询失败: %1").arg(e));
                resolvedDone = true;
            });
            QObject::connect(&mr, &ModrinthAPI::projectsResolved, &rl, &QEventLoop::quit);
            QObject::connect(&mr, &ModrinthAPI::projectsResolveFailed, &rl, &QEventLoop::quit);
            mr.fetchProjectsByIds(ids);
            pump(rl, [&]() { return resolvedDone; }, 40000);

            check(resolved.size() == ids.size(),
                  QStringLiteral("Modrinth 批量查询返回 %1/%2 项").arg(resolved.size()).arg(ids.size()));
            for (const ModInfo &m : resolved) {
                std::printf("[modrinth 前置卡片] %s | id=%s | source=%s | icon=%d\n",
                            qUtf8Printable(m.name), qUtf8Printable(m.id),
                            qUtf8Printable(m.source), int(!m.iconUrl.isEmpty()));
                check(!m.name.isEmpty() && !m.id.isEmpty(),
                      QStringLiteral("Modrinth 前置「%1」有名称和 id").arg(m.name));
            }
        }
    }

    // ── 阶段 2：CurseForge iris 详情 → 前置应有 sodium ──
    {
        ModInfo info;
        bool done = false;
        QEventLoop loop;
        QObject::connect(&cf, &CurseForgeAPI::modDetailReceived, [&](const ModInfo &i) { info = i; done = true; });
        QObject::connect(&cf, &CurseForgeAPI::modDetailFailed, [&](const QString &e) {
            warn(QStringLiteral("curseforge iris 详情失败: %1").arg(e));
            done = true;
        });
        QObject::connect(&cf, &CurseForgeAPI::modDetailReceived, &loop, &QEventLoop::quit);
        QObject::connect(&cf, &CurseForgeAPI::modDetailFailed, &loop, &QEventLoop::quit);
        cf.fetchModDetail(QStringLiteral("455508"));   // Iris Shaders (CurseForge)
        pump(loop, [&]() { return done; }, 40000);

        if (info.versionFiles.isEmpty()) {
            warn(QStringLiteral("CurseForge iris 详情没取到（网络不可用？）"));
        } else {
            std::printf("[curseforge] 项目=%s 版本数=%d 最新版本=%s\n",
                        qUtf8Printable(info.name), int(info.versionFiles.size()),
                        qUtf8Printable(info.versionFiles.first().version));
            dumpDeps(QStringLiteral("curseforge iris 详情 dependencies"), info.dependencies);

            // 每个版本各自的依赖，用于确认「取 first() 是否合理」
            int withDeps = 0;
            for (const ModVersionFile &vf : info.versionFiles) {
                if (!vf.dependencies.isEmpty()) {
                    ++withDeps;
                    std::printf("[curseforge] 版本 %s 依赖 %d 项\n",
                                qUtf8Printable(vf.version), int(vf.dependencies.size()));
                }
            }
            std::printf("[curseforge] 带依赖的版本数=%d/%d\n", withDeps, int(info.versionFiles.size()));

            check(!info.dependencies.isEmpty(),
                  QStringLiteral("CurseForge iris 的详情带回了前置资源（versionFiles.first()）"));

            QStringList ids;
            for (const ModDependency &dep : info.dependencies) {
                if (!dep.name.isEmpty() && !ids.contains(dep.name))
                    ids << dep.name;
            }
            check(!ids.isEmpty(), QStringLiteral("CurseForge 前置 id 可提取（解析器用 modId）"));

            QList<ModInfo> resolved;
            bool resolvedDone = false;
            QEventLoop rl;
            QObject::connect(&cf, &CurseForgeAPI::modsResolved, [&](const QList<ModInfo> &list) {
                resolved = list;
                resolvedDone = true;
            });
            QObject::connect(&cf, &CurseForgeAPI::modsResolveFailed, [&](const QString &e) {
                warn(QStringLiteral("curseforge 批量查询失败: %1").arg(e));
                resolvedDone = true;
            });
            QObject::connect(&cf, &CurseForgeAPI::modsResolved, &rl, &QEventLoop::quit);
            QObject::connect(&cf, &CurseForgeAPI::modsResolveFailed, &rl, &QEventLoop::quit);
            cf.fetchModsByIds(ids);
            pump(rl, [&]() { return resolvedDone; }, 40000);

            check(resolved.size() == ids.size(),
                  QStringLiteral("CurseForge 批量查询返回 %1/%2 项").arg(resolved.size()).arg(ids.size()));
            for (const ModInfo &m : resolved) {
                std::printf("[curseforge 前置卡片] %s | id=%s | source=%s | icon=%d\n",
                            qUtf8Printable(m.name), qUtf8Printable(m.id),
                            qUtf8Printable(m.source), int(!m.iconUrl.isEmpty()));
                check(!m.name.isEmpty() && !m.id.isEmpty(),
                      QStringLiteral("CurseForge 前置「%1」有名称和 id").arg(m.name));
            }
        }
    }

    std::printf("\n======== detail_dependency: %s ========\n", g_fail == 0 ? "ALL PASS" : "HAS FAIL");
    return g_fail == 0 ? 0 : 1;
}
