/**
 * 探针：验证 InstanceBlockNames 的中文名 / 中文输入解析（纯函数，不依赖游戏实例）
 * 结果写入 .syntax_check/probe_block_names_out.txt（UTF-8），控制台只打印 ASCII 统计。
 */

#include "utils/Schematic/InstanceBlockNames.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <cstdio>

using namespace InstanceBlockNames;

static int g_total = 0;
static int g_fail = 0;
static QStringList g_log;

static void checkEq(const QString &what, const QString &got, const QString &want)
{
    ++g_total;
    const bool ok = (got == want);
    if (!ok)
        ++g_fail;
    g_log << QStringLiteral("%1 | %2 | got=[%3] | want=[%4]")
                 .arg(ok ? QStringLiteral("OK  ") : QStringLiteral("FAIL"), what, got, want);
}

int main()
{
    const QHash<QString, QString> empty;

    // 一个「假实例」译名表：验证实例优先 + 模组方块
    QHash<QString, QString> cnMap;
    cnMap.insert(QStringLiteral("minecraft:oak_trapdoor"), QStringLiteral("橡木活板门（实例译名）"));
    cnMap.insert(QStringLiteral("mymod:custom_block"), QStringLiteral("自定义方块"));

    // ---- lookup：内置兜底 ----
    checkEq(QStringLiteral("lookup/内置-石头"), lookup(empty, QStringLiteral("minecraft:stone")),
            QStringLiteral("石头"));
    checkEq(QStringLiteral("lookup/无命名空间"), lookup(empty, QStringLiteral("stone")),
            QStringLiteral("石头"));
    checkEq(QStringLiteral("lookup/带属性"), lookup(empty, QStringLiteral("minecraft:oak_trapdoor[open=true]")),
            QStringLiteral("橡木活板门"));
    checkEq(QStringLiteral("lookup/实例优先"), lookup(cnMap, QStringLiteral("minecraft:oak_trapdoor")),
            QStringLiteral("橡木活板门（实例译名）"));
    checkEq(QStringLiteral("lookup/模组方块"), lookup(cnMap, QStringLiteral("mymod:custom_block")),
            QStringLiteral("自定义方块"));
    checkEq(QStringLiteral("lookup/未知方块"), lookup(empty, QStringLiteral("mymod:whatever")),
            QString());

    // ---- displayName：中文名 + 状态摘要 ----
    checkEq(QStringLiteral("displayName/活板门全状态"),
            displayName(empty, QStringLiteral("minecraft:oak_trapdoor[facing=north,half=bottom,open=false]")),
            QStringLiteral("橡木活板门（北面·下半·关闭）"));
    checkEq(QStringLiteral("displayName/无属性"),
            displayName(empty, QStringLiteral("minecraft:air")), QStringLiteral("空气"));
    checkEq(QStringLiteral("displayName/未收录属性原样"),
            displayName(empty, QStringLiteral("minecraft:oak_log[axis=y]")),
            QStringLiteral("橡木原木（Y轴）"));
    checkEq(QStringLiteral("displayName/无中文回退英文ID"),
            displayName(empty, QStringLiteral("mymod:weird_block[waterlogged=false]")),
            QStringLiteral("weird_block（无水）"));
    checkEq(QStringLiteral("displayName/实例译名生效"),
            displayName(cnMap, QStringLiteral("minecraft:oak_trapdoor[open=true]")),
            QStringLiteral("橡木活板门（实例译名）（开启）"));

    // ---- resolveId：中文/英文输入 ----
    checkEq(QStringLiteral("resolveId/纯中文"), resolveId(empty, QStringLiteral("橡木活板门")),
            QStringLiteral("minecraft:oak_trapdoor"));
    checkEq(QStringLiteral("resolveId/中文带状态后缀"),
            resolveId(empty, QStringLiteral("橡木活板门（北面·下半·关闭）")),
            QStringLiteral("minecraft:oak_trapdoor"));
    checkEq(QStringLiteral("resolveId/短ID补前缀"),
            resolveId(empty, QStringLiteral("oak_trapdoor")), QStringLiteral("minecraft:oak_trapdoor"));
    checkEq(QStringLiteral("resolveId/短ID带属性"),
            resolveId(empty, QStringLiteral("oak_trapdoor[facing=north]")),
            QStringLiteral("minecraft:oak_trapdoor[facing=north]"));
    checkEq(QStringLiteral("resolveId/完整ID原样"),
            resolveId(empty, QStringLiteral("minecraft:stone")), QStringLiteral("minecraft:stone"));
    checkEq(QStringLiteral("resolveId/调色板文案"),
            resolveId(empty, QStringLiteral("石头 (stone)")), QStringLiteral("minecraft:stone"));
    checkEq(QStringLiteral("resolveId/调色板文案带状态"),
            resolveId(empty, QStringLiteral("橡木活板门 (oak_trapdoor[facing=north])")),
            QStringLiteral("minecraft:oak_trapdoor[facing=north]"));
    checkEq(QStringLiteral("resolveId/模组调色板文案"),
            resolveId(cnMap, QStringLiteral("自定义方块 (mymod:custom_block)")),
            QStringLiteral("mymod:custom_block"));
    checkEq(QStringLiteral("resolveId/实例中文名"),
            resolveId(cnMap, QStringLiteral("自定义方块")), QStringLiteral("mymod:custom_block"));
    checkEq(QStringLiteral("resolveId/无法识别原样返回"),
            resolveId(empty, QStringLiteral("这不是方块")), QStringLiteral("这不是方块"));

    // ---- 往返：内置方块显示名再解析回来，方块本体不变 ----
    static const char *ids[] = {
        "minecraft:air",
        "minecraft:stone",
        "minecraft:oak_trapdoor[facing=north,half=bottom,open=false]",
        "minecraft:iron_trapdoor[facing=north,half=bottom,open=false]",
        "minecraft:dark_oak_trapdoor[facing=west,half=bottom,open=true]",
        "minecraft:redstone_wire[east=none,north=side,south=none,west=none]",
        "minecraft:lever[face=wall,facing=north,powered=false]",
        "minecraft:oak_door[half=lower,hinge=left,open=false,facing=north]",
        "minecraft:oak_fence[east=true,north=false,south=false,west=false]",
    };
    for (const char *raw : ids)
    {
        const QString id = QString::fromLatin1(raw);
        const QString label = displayName(empty, id);
        const QString back = resolveId(empty, label);
        // 允许状态属性书写顺序不同，比较方块本体 + 属性集合
        const QString wantBase = baseBlockId(id);
        const bool ok = (baseBlockId(back) == wantBase) && !label.isEmpty();
        ++g_total;
        if (!ok)
            ++g_fail;
        g_log << QStringLiteral("%1 | roundtrip %2 | label=[%3] | back=[%4]")
                     .arg(ok ? QStringLiteral("OK  ") : QStringLiteral("FAIL"), id, label, back);
    }

    QFile out(QStringLiteral(".syntax_check/probe_block_names_out.txt"));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QTextStream ts(&out);
        ts.setEncoding(QStringConverter::Utf8);
        ts << g_log.join(QLatin1Char('\n')) << Qt::endl;
    }
    std::printf("TOTAL=%d FAIL=%d\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
