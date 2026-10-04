/**
 * @file   placeholder_logic_test.cpp
 * @brief  快捷指令方块填写功能的独立逻辑测试（非 QtTest，控制台自校验）
 * @author BlockBox Team
 * @date   2026-09-13
 *
 * 验证两条核心链路（用户输入不含 <> 符号，如 "给予 @自己 钻石块 2"）：
 *   1. 补全候选：在方块/物品参数位置输入中文时，补全列表给出实例注册表
 *      中的方块候选词（中文名 + 英文 ID），省略选择器时同样给出
 *   2. 指令翻译：中文方块名经实例注册表解析为可执行的英文指令
 * 方块数据用 addBlock 手工注入，模拟从实例 JAR 提取的 BlockRegistry 内容。
 */

#include <QCoreApplication>
#include <QDebug>

#include "utils/CommandAssistant/BlockRegistry.h"
#include "utils/CommandAssistant/CommandCompleter.h"
#include "utils/CommandAssistant/CommandDatabase.h"
#include "utils/CommandAssistant/CommandTranslator.h"
#include "utils/CommandAssistant/PlaceholderHelper.h"

namespace {

int g_failures = 0;

void expectEqual(const QString &actual, const QString &expected, const char *caseName)
{
    const bool ok = actual == expected;
    if (!ok)
    {
        ++g_failures;
    }
    qInfo() << (ok ? "[PASS]" : "[FAIL]") << caseName
            << "| expected:" << expected << "| actual:" << actual;
}

void expectTrue(bool cond, const char *caseName)
{
    if (!cond)
    {
        ++g_failures;
    }
    qInfo() << (cond ? "[PASS]" : "[FAIL]") << caseName;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // ---------- PlaceholderHelper ----------
    const QString input = QStringLiteral("给予 @自己 <钻石块> <2>");
    const QList<PlaceholderToken> tokens = PlaceholderHelper::parse(input);
    expectTrue(tokens.size() == 2, "parse finds two tokens");
    expectEqual(tokens.at(0).inner, QStringLiteral("钻石块"), "token0 inner");
    expectEqual(tokens.at(1).inner, QStringLiteral("2"), "token1 inner");
    expectTrue(PlaceholderHelper::isPlaceholder(QStringLiteral("<2>")), "isPlaceholder true");
    expectTrue(!PlaceholderHelper::isPlaceholder(QStringLiteral("2>")), "isPlaceholder false");
    expectEqual(PlaceholderHelper::replaceToken(input, tokens.at(0).start, tokens.at(0).end,
                                                QStringLiteral("金块")),
                QStringLiteral("给予 @自己 <金块> <2>"), "replaceToken");

    // ---------- 方块注册表（模拟实例 JAR 提取结果） ----------
    BlockRegistry registry;
    BlockInfo diamond;
    diamond.english = QStringLiteral("minecraft:diamond_block");
    diamond.chinese.append(QStringLiteral("钻石块"));
    registry.addBlock(diamond);
    BlockInfo stone;
    stone.english = QStringLiteral("minecraft:stone");
    stone.chinese.append(QStringLiteral("石头"));
    registry.addBlock(stone);
    registry.rebuildIndexes();

    expectTrue(registry.findBlockExact(QStringLiteral("钻石块")).has_value(),
               "findBlockExact by chinese");
    expectTrue(registry.findBlockExact(QStringLiteral("diamond_block")).has_value(),
               "findBlockExact by bare english");
    expectTrue(registry.findBlockExact(QStringLiteral("不存在的方块")) == std::nullopt,
               "findBlockExact miss");

    // ---------- 指令翻译 ----------
    CommandDatabase db;
    const QString dbPath = QStringLiteral("resources/command_database.json");
    if (!db.loadFromFile(dbPath))
    {
        qWarning() << "failed to load database from" << dbPath
                   << "（请在本测试于 C-BlockBox 目录下运行）";
        return 2;
    }

    CommandTranslator translator(&db);
    translator.setBlockRegistry(&registry);
    const CommandContext ctx{QStringLiteral("1.20.4"), 4, false};

    // 核心用例：给予 @自己 <钻石块> <2> → /give @s minecraft:diamond_block 2
    const auto r1 = translator.translate(QStringLiteral("给予 @自己 <钻石块> <2>"), ctx);
    expectTrue(r1.success, "give placeholder success");
    expectEqual(r1.englishCommand, QStringLiteral("/give @s minecraft:diamond_block 2"),
                "give placeholder output");

    // 选择器占位符：<自己> 应按选择器消费而非被默认值顶替
    const auto r2 = translator.translate(QStringLiteral("给予 <自己> <钻石块> <2>"), ctx);
    expectEqual(r2.englishCommand, QStringLiteral("/give @s minecraft:diamond_block 2"),
                "selector placeholder output");

    // 无占位符的中文方块名同样可翻译（注册表兜底）
    const auto r3 = translator.translate(QStringLiteral("给予 钻石块 64"), ctx);
    expectEqual(r3.englishCommand, QStringLiteral("/give @p minecraft:diamond_block 64"),
                "bare chinese block output");

    // block 类型参数（setblock）：坐标三位在前，方块占位符在后
    const auto r4 = translator.translate(QStringLiteral("放置方块 1 2 3 <石头>"), ctx);
    expectEqual(r4.englishCommand, QStringLiteral("/setblock 1 2 3 minecraft:stone replace"),
                "setblock block placeholder");

    // pos 类型参数支持坐标占位符（<100> <64> <100>）
    const auto r4b = translator.translate(QStringLiteral("放置方块 <100> <64> <100> <石头>"), ctx);
    expectEqual(r4b.englishCommand, QStringLiteral("/setblock 100 64 100 minecraft:stone replace"),
                "setblock coord placeholders");

    // 数字占位符非法内容 → 保留并给出警告
    const auto r5 = translator.translate(QStringLiteral("给予 钻石剑 <abc>"), ctx);
    expectTrue(!r5.warnings.isEmpty(), "non-numeric placeholder warns");
    expectEqual(r5.englishCommand, QStringLiteral("/give @p diamond_sword abc"),
                "non-numeric placeholder passes value through");

    // 原有行为回归：无占位符的普通物品指令不受影响
    const auto r6 = translator.translate(QStringLiteral("给予 钻石剑 1"), ctx);
    expectEqual(r6.englishCommand, QStringLiteral("/give @p diamond_sword 1"),
                "regression: plain item command");

    // ---------- 补全候选（方块填写的主要入口） ----------
    CommandCompleter completer(&db);
    completer.setBlockRegistry(&registry);

    // 在物品参数位置输入 "钻"：应出现实例注册表的方块候选（钻石块）
    const auto c1 = completer.complete(QStringLiteral("给予 @自己 钻"), ctx);
    bool hasBlockCandidate = false;
    for (const CompletionCandidate &c : c1)
    {
        if (c.insertText == QStringLiteral("钻石块") && c.detail.contains(QStringLiteral("方块")))
        {
            hasBlockCandidate = true;
            break;
        }
    }
    expectTrue(hasBlockCandidate, "item param candidates include block 钻石块");

    // 省略选择器直接输入 "给予 钻"：选择器智能跳过后同样给出方块候选
    const auto c2 = completer.complete(QStringLiteral("给予 钻"), ctx);
    hasBlockCandidate = false;
    for (const CompletionCandidate &c : c2)
    {
        if (c.insertText == QStringLiteral("钻石块") && c.detail.contains(QStringLiteral("方块")))
        {
            hasBlockCandidate = true;
            break;
        }
    }
    expectTrue(hasBlockCandidate, "selector smart-skip still offers block candidates");

    // 仅输入 "给予 "（空词）：仍给出选择器候选，引导用户
    const auto c3 = completer.complete(QStringLiteral("给予 "), ctx);
    bool hasSelectorCandidate = false;
    for (const CompletionCandidate &c : c3)
    {
        if (c.insertText == QStringLiteral("@自己"))
        {
            hasSelectorCandidate = true;
            break;
        }
    }
    expectTrue(hasSelectorCandidate, "empty word keeps selector guidance");

    // 英文 ID 前缀也能补全到方块（如 "diamond_bl"）
    const auto c4 = completer.complete(QStringLiteral("给予 @自己 diamond_bl"), ctx);
    hasBlockCandidate = false;
    for (const CompletionCandidate &c : c4)
    {
        if (c.insertText == QStringLiteral("钻石块"))
        {
            hasBlockCandidate = true;
            break;
        }
    }
    expectTrue(hasBlockCandidate, "english id prefix matches block candidate");

    // ---------- 参数引导提示 ----------
    // 物品参数位置：首行提示"请输入物品或方块名称"（灰显、不可插入）
    const auto h1 = completer.complete(QStringLiteral("给予 @自己 钻"), ctx);
    expectTrue(!h1.isEmpty()
                   && h1.first().displayText.contains(QStringLiteral("请输入物品或方块名称"))
                   && h1.first().insertText.isEmpty(),
               "item param shows guidance hint at top");

    // 数量参数位置（int）：提示"请输入数量"
    const auto h2 = completer.complete(QStringLiteral("给予 @自己 钻石剑 2"), ctx);
    expectTrue(!h2.isEmpty()
                   && h2.first().displayText == QStringLiteral("请输入数量"),
               "int param shows 请输入数量 hint");

    // block 参数位置（setblock 方块）：提示"请输入方块名称"
    const auto h3 = completer.complete(QStringLiteral("放置方块 1 2 3 石"), ctx);
    expectTrue(!h3.isEmpty()
                   && h3.first().displayText == QStringLiteral("请输入方块名称"),
               "block param shows 请输入方块名称 hint");

    // 选择器打完后补全前进到下一参数：传送 @自己 → 提示"请输入坐标"
    const auto h4 = completer.complete(QStringLiteral("传送 @自己"), ctx);
    expectTrue(!h4.isEmpty()
                   && h4.first().displayText.contains(QStringLiteral("请输入坐标")),
               "complete selector advances to pos hint");

    // 选择器未打完（传送 @自）：仍给出选择器候选而非坐标提示
    const auto h5 = completer.complete(QStringLiteral("传送 @自"), ctx);
    bool hasSelectorMid = false;
    for (const CompletionCandidate &c : h5)
    {
        if (c.insertText == QStringLiteral("@自己"))
        {
            hasSelectorMid = true;
            break;
        }
    }
    expectTrue(hasSelectorMid, "incomplete selector keeps selector candidates");

    // ---------- 传送目的地坐标（pos 类型三联消费） ----------
    const auto t1 = translator.translate(QStringLiteral("传送 @自己 100 64 200"), ctx);
    expectEqual(t1.englishCommand, QStringLiteral("/teleport @s 100 64 200"),
                "teleport with coordinates");
    const auto t2 = translator.translate(QStringLiteral("传送 @自己 Steve"), ctx);
    expectEqual(t2.englishCommand, QStringLiteral("/teleport @s Steve"),
                "teleport with entity destination");
    const auto t3 = translator.translate(QStringLiteral("传送 100 64 200"), ctx);
    expectEqual(t3.englishCommand, QStringLiteral("/teleport @s 100 64 200"),
                "teleport without selector uses default @s");

    // ---------- 时间相关指令（中文枚举值与候选） ----------
    // 设置白天：操作枚举"设置"→set，时间值"白天"→day
    const auto tm1 = translator.translate(QStringLiteral("时间 设置 白天"), ctx);
    expectEqual(tm1.englishCommand, QStringLiteral("/time set day"),
                "time set day via chinese aliases");
    const auto tm2 = translator.translate(QStringLiteral("时间 设置 午夜"), ctx);
    expectEqual(tm2.englishCommand, QStringLiteral("/time set midnight"),
                "time set midnight");
    const auto tm3 = translator.translate(QStringLiteral("时间 设置 6000"), ctx);
    expectEqual(tm3.englishCommand, QStringLiteral("/time set 6000"),
                "time set with tick number");
    const auto tm4 = translator.translate(QStringLiteral("时间 增加 1000"), ctx);
    expectEqual(tm4.englishCommand, QStringLiteral("/time add 1000"),
                "time add ticks");
    const auto tm5 = translator.translate(QStringLiteral("时间 查询 游戏时间"), ctx);
    expectEqual(tm5.englishCommand, QStringLiteral("/time query gametime"),
                "time query gametime");
    // 天气 / 游戏模式 / 开关共用同一别名表
    const auto tm6 = translator.translate(QStringLiteral("天气 晴"), ctx);
    expectEqual(tm6.englishCommand, QStringLiteral("/weather clear"),
                "weather via chinese alias");
    const auto tm7 = translator.translate(QStringLiteral("模式 创造"), ctx);
    expectEqual(tm7.englishCommand, QStringLiteral("/gamemode creative @s"),
                "gamemode via chinese alias");
    const auto tm8 = translator.translate(QStringLiteral("锁定昼夜 开"), ctx);
    expectEqual(tm8.englishCommand, QStringLiteral("/daylock true"),
                "daylock via chinese alias");

    // 补全：时间操作枚举给出中文别名行（设置（set））
    const auto h6 = completer.complete(QStringLiteral("时间 "), ctx);
    bool hasSetAlias = false;
    for (const CompletionCandidate &c : h6)
    {
        if (c.insertText == QStringLiteral("设置"))
        {
            hasSetAlias = true;
            break;
        }
    }
    expectTrue(hasSetAlias, "time operation enum offers chinese alias");
    // 时间值参数给出候选（白天（day））+ 引导提示
    const auto h7 = completer.complete(QStringLiteral("时间 设置 白"), ctx);
    bool hasDayAlias = false;
    for (const CompletionCandidate &c : h7)
    {
        if (c.insertText == QStringLiteral("白天"))
        {
            hasDayAlias = true;
            break;
        }
    }
    expectTrue(hasDayAlias, "time value offers chinese candidate 白天（day）");

    // ---------- 生物群系（定位生物群系候选 + 翻译，数据来自实例注入） ----------
    QList<ItemInfo> biomes;
    ItemInfo plains;
    plains.english = QStringLiteral("minecraft:plains");
    plains.chinese.append(QStringLiteral("平原"));
    biomes.append(plains);
    ItemInfo flowerForest;
    flowerForest.english = QStringLiteral("minecraft:flower_forest");
    flowerForest.chinese.append(QStringLiteral("繁花森林"));
    biomes.append(flowerForest);
    db.mergeDynamicData({}, {}, {}, {}, biomes);

    // 翻译：中文名与省略命名空间的英文 ID 均可解析
    const auto b1 = translator.translate(QStringLiteral("定位生物群系 平原"), ctx);
    expectEqual(b1.englishCommand, QStringLiteral("/locatebiome minecraft:plains"),
                "locatebiome via chinese biome name");
    const auto b2 = translator.translate(QStringLiteral("定位生物群系 plains"), ctx);
    expectEqual(b2.englishCommand, QStringLiteral("/locatebiome minecraft:plains"),
                "locatebiome via bare english id");
    const auto b3 = translator.translate(QStringLiteral("填充生物群系 1 2 3 4 5 6 平原"), ctx);
    expectEqual(b3.englishCommand, QStringLiteral("/fillbiome 1 2 3 4 5 6 minecraft:plains"),
                "fillbiome consumes two pos then biome");

    // 补全：中文前缀出群系候选；英文省略命名空间前缀同样命中
    const auto h8 = completer.complete(QStringLiteral("定位生物群系 平"), ctx);
    bool hasPlainsCandidate = false;
    for (const CompletionCandidate &c : h8)
    {
        if (c.insertText == QStringLiteral("平原") && c.detail.contains(QStringLiteral("群系")))
        {
            hasPlainsCandidate = true;
            break;
        }
    }
    expectTrue(hasPlainsCandidate, "locatebiome offers biome candidate 平原");
    const auto h9 = completer.complete(QStringLiteral("定位生物群系 flow"), ctx);
    bool hasFlowerCandidate = false;
    for (const CompletionCandidate &c : h9)
    {
        if (c.insertText == QStringLiteral("繁花森林"))
        {
            hasFlowerCandidate = true;
            break;
        }
    }
    expectTrue(hasFlowerCandidate, "locatebiome matches bare english prefix");

    // ---------- 结构（定位结构候选 + 翻译，数据来自实例注入） ----------
    QList<ItemInfo> structures;
    ItemInfo bastion;
    bastion.english = QStringLiteral("minecraft:bastion_remnant");
    bastion.chinese.append(QStringLiteral("下界堡垒"));
    structures.append(bastion);
    ItemInfo villagePlains;
    villagePlains.english = QStringLiteral("minecraft:village_plains");
    villagePlains.chinese.append(QStringLiteral("平原村庄"));
    structures.append(villagePlains);
    db.mergeDynamicData({}, {}, {}, {}, {}, structures);

    const auto s1 = translator.translate(QStringLiteral("定位结构 下界堡垒"), ctx);
    expectEqual(s1.englishCommand, QStringLiteral("/locate minecraft:bastion_remnant"),
                "locate via chinese structure name");
    const auto s2 = translator.translate(QStringLiteral("定位结构 bastion_remnant"), ctx);
    expectEqual(s2.englishCommand, QStringLiteral("/locate minecraft:bastion_remnant"),
                "locate via bare english id");
    const auto s3 = translator.translate(QStringLiteral("放置结构 平原村庄 1 2 3"), ctx);
    expectEqual(s3.englishCommand, QStringLiteral("/place structure minecraft:village_plains 1 2 3"),
                "place structure with optional pos");

    const auto h10 = completer.complete(QStringLiteral("定位结构 下"), ctx);
    bool hasBastionCandidate = false;
    for (const CompletionCandidate &c : h10)
    {
        if (c.insertText == QStringLiteral("下界堡垒") && c.detail.contains(QStringLiteral("结构")))
        {
            hasBastionCandidate = true;
            break;
        }
    }
    expectTrue(hasBastionCandidate, "locate offers structure candidate");
    const auto h11 = completer.complete(QStringLiteral("定位结构 village"), ctx);
    bool hasVillageCandidate = false;
    for (const CompletionCandidate &c : h11)
    {
        if (c.insertText == QStringLiteral("平原村庄"))
        {
            hasVillageCandidate = true;
            break;
        }
    }
    expectTrue(hasVillageCandidate, "locate matches bare english structure prefix");

    // ---------- 天气指令完整输入 ----------
    // 翻译：天气 + 可选持续秒数
    const auto w1 = translator.translate(QStringLiteral("天气 晴 600"), ctx);
    expectEqual(w1.englishCommand, QStringLiteral("/weather clear 600"),
                "weather with duration");
    const auto w2 = translator.translate(QStringLiteral("天气 下雨"), ctx);
    expectEqual(w2.englishCommand, QStringLiteral("/weather rain"),
                "weather via alias 下雨");
    // 补全：天气词打完后前进到持续秒数提示，而不是重复显示已完成的候选
    const auto w3 = completer.complete(QStringLiteral("天气 晴"), ctx);
    expectTrue(!w3.isEmpty()
                   && w3.first().displayText.contains(QStringLiteral("请输入持续秒数")),
               "completed weather word advances to duration hint");
    // 输入中途（"下"）仍给出天气候选（下雨（rain）），不提前前进
    const auto w4 = completer.complete(QStringLiteral("天气 下"), ctx);
    bool hasRainAlias = false;
    for (const CompletionCandidate &c : w4)
    {
        if (c.insertText == QStringLiteral("下雨"))
        {
            hasRainAlias = true;
            break;
        }
    }
    expectTrue(hasRainAlias, "mid-typing weather keeps alias candidates");
    // 时间操作枚举同理：设置打完后前进到时间值候选
    const auto w5 = completer.complete(QStringLiteral("时间 设置"), ctx);
    expectTrue(!w5.isEmpty()
                   && w5.first().displayText.contains(QStringLiteral("请输入值")),
               "completed enum advances to time value candidates");

    // ---------- 候选去重：每个值只出一行 ----------
    const auto d1 = completer.complete(QStringLiteral("天气 "), ctx);
    int clearFamily = 0, rainFamily = 0, thunderFamily = 0;
    for (const CompletionCandidate &c : d1)
    {
        const QString &t = c.insertText;
        if (t == QStringLiteral("clear") || t == QStringLiteral("晴")
            || t == QStringLiteral("晴天"))
        {
            ++clearFamily;
        }
        if (t == QStringLiteral("rain") || t == QStringLiteral("雨")
            || t == QStringLiteral("下雨"))
        {
            ++rainFamily;
        }
        if (t == QStringLiteral("thunder") || t == QStringLiteral("雷")
            || t == QStringLiteral("雷雨") || t == QStringLiteral("雷暴"))
        {
            ++thunderFamily;
        }
    }
    expectTrue(clearFamily == 1 && rainFamily == 1 && thunderFamily == 1,
               "weather candidates deduped to one row per value");

    // 英文前缀命中 → 插入英文形式；别名精确输入 → 插入别名
    const auto d2 = completer.complete(QStringLiteral("天气 cle"), ctx);
    expectTrue(!d2.isEmpty() && d2.first().insertText == QStringLiteral("clear"),
               "english prefix inserts english form");
    const auto d3 = completer.complete(QStringLiteral("难度 和平"), ctx);
    expectTrue(!d3.isEmpty() && d3.first().insertText == QStringLiteral("和平"),
               "alias input inserts alias form");
    // 天气 晴天：完整枚举值 → 前进到持续秒数提示（与 天气 晴 行为一致）
    const auto d5 = completer.complete(QStringLiteral("天气 晴天"), ctx);
    expectTrue(!d5.isEmpty()
                   && d5.first().displayText.contains(QStringLiteral("请输入持续秒数")),
               "complete alias word advances to next param");

    // 时间操作枚举同样去重（add 只有 增加（add） 一行）
    const auto d4 = completer.complete(QStringLiteral("时间 "), ctx);
    int addFamily = 0;
    for (const CompletionCandidate &c : d4)
    {
        const QString &t = c.insertText;
        if (t == QStringLiteral("add") || t == QStringLiteral("增加")
            || t == QStringLiteral("添加"))
        {
            ++addFamily;
        }
    }
    expectTrue(addFamily == 1, "time operation candidates deduped");

    qInfo() << (g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED")
            << "(failures:" << g_failures << ")";
    return g_failures == 0 ? 0 : 1;
}
