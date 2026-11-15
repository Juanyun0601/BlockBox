/**
 * @file   lobby_matcher_probe.cpp
 * @brief  联机大厅「挑实例」逻辑探针：纯内存构造实例，核对打分与缺失内容识别
 *
 * 不读盘、不联网：直接用 LobbyInstanceInfo 手工拼出几种典型情况，核对
 *   1. 必装模组齐全的实例排在只有版本匹配的前面；
 *   2. 加载器不一致的实例不会被选；
 *   3. 只缺「辅助」模组时仍算模组匹配（辅助不是门槛），资源包 / 光影包只加分；
 *   4. 缺的模组 / 资源包会逐项列出来（界面据此做勾选清单）；
 *   5. 完全没匹配时给出安装提示。
 *
 * 退出码 0 = 全部断言通过。
 */
#include <QCoreApplication>
#include <QDateTime>

#include <cstdio>

#include "utils/Lobby/LobbyInstanceMatcher.h"

namespace
{
int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-58s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

LobbyInstanceInfo makeInstance(const QString &id, const QString &version, const QString &loader,
                               const QStringList &mods,
                               const QStringList &packs = QStringList(),
                               const QStringList &shaders = QStringList(),
                               int daysOld = 10)
{
    LobbyInstanceInfo info;
    info.id = id;
    info.path = QStringLiteral("/fake/") + id;
    info.version = version;
    info.loader = loader;
    info.mods = mods;
    info.resourcePacks = packs;
    info.shaderPacks = shaders;
    info.modifiedAt = QDateTime::currentDateTime().addDays(-daysOld);
    return info;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList roomMods{QStringLiteral("sodium.jar"), QStringLiteral("lithium.jar"),
                               QStringLiteral("fabric-api.jar")};
    const QStringList roomAux{QStringLiteral("minimap.jar")};
    const QStringList roomPacks{QStringLiteral("Faithful-32x.zip")};
    const QStringList roomShaders{QStringLiteral("Complementary.zip")};

    const LobbyInstanceMatcher::Requirement requirement = LobbyInstanceMatcher::requirementFor(
        QStringLiteral("1.20.1"), QStringLiteral("Fabric"), roomMods, roomAux, roomPacks,
        roomShaders);
    expect("aux mods are stripped from required list", requirement.requiredMods.size() == 3);
    expect("aux list kept separate", requirement.auxMods.size() == 1);

    // A：必装齐全 + 辅助 / 资源包 / 光影都齐
    LobbyInstanceInfo perfect = makeInstance(
        QStringLiteral("a-complete"), QStringLiteral("1.20.1"), QStringLiteral("Fabric"),
        {QStringLiteral("sodium.jar"), QStringLiteral("lithium.jar"),
         QStringLiteral("fabric-api.jar"), QStringLiteral("minimap.jar")},
        roomPacks, roomShaders, 40);
    // B：缺一个必装模组，但比 A 新
    LobbyInstanceInfo almost = makeInstance(
        QStringLiteral("b-almost"), QStringLiteral("1.20.1"), QStringLiteral("Fabric"),
        {QStringLiteral("sodium.jar"), QStringLiteral("fabric-api.jar")}, roomPacks, {}, 1);
    // C：加载器不对
    LobbyInstanceInfo wrongLoader = makeInstance(
        QStringLiteral("c-forge"), QStringLiteral("1.20.1"), QStringLiteral("Forge"),
        {QStringLiteral("sodium.jar"), QStringLiteral("lithium.jar"),
         QStringLiteral("fabric-api.jar")});
    // D：版本不对
    LobbyInstanceInfo wrongVersion = makeInstance(
        QStringLiteral("d-118"), QStringLiteral("1.18.2"), QStringLiteral("Fabric"),
        {QStringLiteral("sodium.jar")});

    const QList<LobbyInstanceInfo> pool{perfect, almost, wrongLoader, wrongVersion};
    const LobbyInstanceMatcher::JoinPlan plan = LobbyInstanceMatcher::makePlan(requirement, pool);

    expect("plan found", plan.found);
    expect("complete instance wins over newer incomplete one",
           plan.found && plan.best.instance.id == QLatin1String("a-complete"));
    expect("complete instance counts as full mod match",
           plan.best.level == LobbyInstanceMatcher::MatchLevel::Mods);
    expect("wrong loader excluded from candidates", plan.candidates.size() == 2);
    expect("candidates sorted by score",
           plan.candidates.size() == 2 && plan.candidates.at(0).score > plan.candidates.at(1).score);
    expect("complete instance has nothing to fetch", !plan.best.hasAnythingToFetch());

    // 缺辅助模组 / 资源包 / 光影时：等级仍是模组匹配，但会列出要补的东西
    LobbyInstanceInfo noExtras = makeInstance(
        QStringLiteral("e-noextras"), QStringLiteral("1.20.1"), QStringLiteral("Fabric"),
        {QStringLiteral("sodium.jar"), QStringLiteral("lithium.jar"),
         QStringLiteral("fabric-api.jar")});
    const LobbyInstanceMatcher::JoinPlan plan2 =
        LobbyInstanceMatcher::makePlan(requirement, {noExtras});
    expect("missing aux mod still a mod-level match",
           plan2.found && plan2.best.level == LobbyInstanceMatcher::MatchLevel::Mods);
    expect("missing aux mod reported", plan2.best.missingAuxMods.size() == 1);
    expect("missing resource pack reported", plan2.best.missingResourcePacks.size() == 1);
    expect("missing shader pack reported", plan2.best.missingShaderPacks.size() == 1);
    expect("missing required mods empty", plan2.best.missingMods.isEmpty());
    expect("extras counted as something to fetch", plan2.best.hasAnythingToFetch());

    // 两个实例都缺必装模组：等级降到「加载器匹配」，缺的模组逐个列出来
    LobbyInstanceInfo missingAll = makeInstance(
        QStringLiteral("f-missing"), QStringLiteral("1.20.1"), QStringLiteral("Fabric"),
        {QStringLiteral("sodium.jar")});
    const LobbyInstanceMatcher::JoinPlan plan3 =
        LobbyInstanceMatcher::makePlan(requirement, {missingAll});
    expect("missing required mods -> loader level",
           plan3.best.level == LobbyInstanceMatcher::MatchLevel::Loader);
    expect("missing required mod list has 2 items", plan3.best.missingMods.size() == 2);

    // 完全没匹配：给出安装提示
    const LobbyInstanceMatcher::JoinPlan plan4 =
        LobbyInstanceMatcher::makePlan(requirement, {wrongLoader, wrongVersion});
    expect("no match -> found false", !plan4.found);
    expect("no match -> install hint mentions version", plan4.installHint.contains(QStringLiteral("1.20.1")));

    // 同分时最近改过的实例优先
    LobbyInstanceInfo older = makeInstance(QStringLiteral("g-old"), QStringLiteral("1.20.1"),
                                           QStringLiteral("Fabric"),
                                           {QStringLiteral("sodium.jar")}, {}, {}, 30);
    LobbyInstanceInfo newer = makeInstance(QStringLiteral("h-new"), QStringLiteral("1.20.1"),
                                           QStringLiteral("Fabric"),
                                           {QStringLiteral("sodium.jar")}, {}, {}, 1);
    const LobbyInstanceMatcher::JoinPlan plan5 =
        LobbyInstanceMatcher::makePlan(requirement, {older, newer});
    expect("tie on score -> newest instance wins",
           plan5.found && plan5.best.instance.id == QLatin1String("h-new"));

    fprintf(stderr, "[probe] failures = %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
