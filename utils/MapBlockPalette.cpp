/**
 * @file   MapBlockPalette.cpp
 * @brief  方块 → 地表代表色表实现
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "MapBlockPalette.h"

#include <QHash>
#include <QLatin1String>

namespace MapBlockPalette {

QColor blockColor(const QString &blockName)
{
    QString n = blockName;
    const int idx = n.indexOf(QLatin1Char(':'));
    if (idx >= 0)
        n = n.mid(idx + 1);
    n = n.toLower();

    static const QHash<QString, QColor> kTable = {
        // 地表常见方块
        { QStringLiteral("grass_block"),     QColor("#79A54D") },
        { QStringLiteral("dirt"),            QColor("#8B6A47") },
        { QStringLiteral("coarse_dirt"),     QColor("#7A5A3C") },
        { QStringLiteral("rooted_dirt"),     QColor("#8B6A47") },
        { QStringLiteral("podzol"),          QColor("#6B4F2A") },
        { QStringLiteral("mycelium"),        QColor("#75657B") },
        { QStringLiteral("stone"),           QColor("#7F7F7F") },
        { QStringLiteral("andesite"),        QColor("#8C8C8C") },
        { QStringLiteral("diorite"),         QColor("#DADADA") },
        { QStringLiteral("granite"),         QColor("#9B6B5C") },
        { QStringLiteral("deepslate"),       QColor("#4F4F52") },
        { QStringLiteral("tuff"),            QColor("#6C6C62") },
        { QStringLiteral("calcite"),         QColor("#DFDFDF") },
        { QStringLiteral("cobblestone"),     QColor("#7A7A7A") },
        { QStringLiteral("mossy_cobblestone"), QColor("#6E7A5E") },
        { QStringLiteral("bedrock"),         QColor("#4C4C4C") },
        { QStringLiteral("gravel"),          QColor("#837B73") },
        { QStringLiteral("clay"),            QColor("#9FA6B4") },
        { QStringLiteral("sand"),            QColor("#DBD3A0") },
        { QStringLiteral("red_sand"),        QColor("#BA6E28") },
        { QStringLiteral("sandstone"),       QColor("#DBD3A0") },
        { QStringLiteral("red_sandstone"),   QColor("#C0793C") },
        { QStringLiteral("snow"),            QColor("#FFFFFF") },
        { QStringLiteral("snow_block"),      QColor("#FFFFFF") },
        { QStringLiteral("powder_snow"),     QColor("#FFFFFF") },
        { QStringLiteral("ice"),             QColor("#7DADE0") },
        { QStringLiteral("packed_ice"),      QColor("#A0C8F0") },
        { QStringLiteral("blue_ice"),        QColor("#6A8FE0") },
        { QStringLiteral("water"),           QColor("#3F76E4") },
        { QStringLiteral("lava"),            QColor("#E06A12") },
        { QStringLiteral("magma_block"),     QColor("#C05416") },
        { QStringLiteral("farmland"),        QColor("#6B4A2E") },
        { QStringLiteral("dirt_path"),       QColor("#9A8352") },
        { QStringLiteral("grass_path"),      QColor("#9A8352") },
        { QStringLiteral("moss_block"),      QColor("#5B7C2E") },
        { QStringLiteral("moss_carpet"),     QColor("#5B7C2E") },
        { QStringLiteral("hay_block"),       QColor("#B3A12C") },
        { QStringLiteral("dried_kelp_block"), QColor("#4E5A2E") },
        { QStringLiteral("sponge"),          QColor("#C6C64C") },
        { QStringLiteral("cobweb"),          QColor("#E8E8E8") },
        { QStringLiteral("short_grass"),     QColor("#77BB55") },
        { QStringLiteral("tall_grass"),      QColor("#77BB55") },
        { QStringLiteral("fern"),            QColor("#6BA848") },
        { QStringLiteral("seagrass"),        QColor("#4C9E5A") },
        { QStringLiteral("kelp"),            QColor("#3E7A45") },
        { QStringLiteral("lily_pad"),        QColor("#4C8B3A") },
        { QStringLiteral("cactus"),          QColor("#5E8F3C") },
        { QStringLiteral("sugar_cane"),      QColor("#8FCF6A") },
        { QStringLiteral("bamboo"),          QColor("#7FAF48") },
        { QStringLiteral("vine"),            QColor("#4E7F32") },
        // 矿石
        { QStringLiteral("coal_ore"),        QColor("#5A5A5A") },
        { QStringLiteral("iron_ore"),        QColor("#C8A18A") },
        { QStringLiteral("copper_ore"),      QColor("#D77E4A") },
        { QStringLiteral("gold_ore"),        QColor("#D8B93C") },
        { QStringLiteral("redstone_ore"),    QColor("#B03028") },
        { QStringLiteral("lapis_ore"),       QColor("#3C64B4") },
        { QStringLiteral("diamond_ore"),     QColor("#5AC8C0") },
        { QStringLiteral("emerald_ore"),     QColor("#3CB05A") },
        { QStringLiteral("deepslate_diamond_ore"), QColor("#4AA9A2") },
        // 木质 / 叶子
        { QStringLiteral("oak_leaves"),      QColor("#4E7F32") },
        { QStringLiteral("spruce_leaves"),   QColor("#3E6B3E") },
        { QStringLiteral("birch_leaves"),    QColor("#6F9A45") },
        { QStringLiteral("jungle_leaves"),   QColor("#3F8A2E") },
        { QStringLiteral("acacia_leaves"),   QColor("#5E8A35") },
        { QStringLiteral("dark_oak_leaves"), QColor("#3A6425") },
        { QStringLiteral("mangrove_leaves"), QColor("#3F7A38") },
        { QStringLiteral("cherry_leaves"),   QColor("#E8AFC8") },
        { QStringLiteral("azalea_leaves"),   QColor("#59833A") },
        { QStringLiteral("nether_wart_block"), QColor("#7A1F1F") },
        { QStringLiteral("warped_wart_block"), QColor("#1E7A7A") },
        // 下界
        { QStringLiteral("netherrack"),      QColor("#6E3B3B") },
        { QStringLiteral("nether_bricks"),   QColor("#4C1E1E") },
        { QStringLiteral("soul_sand"),       QColor("#5B4635") },
        { QStringLiteral("soul_soil"),       QColor("#54402F") },
        { QStringLiteral("glowstone"),       QColor("#F5C86A") },
        { QStringLiteral("nether_quartz_ore"), QColor("#9C8C84") },
        { QStringLiteral("basalt"),          QColor("#4A4A4F") },
        { QStringLiteral("blackstone"),      QColor("#3B3436") },
        { QStringLiteral("crimson_nylium"),  QColor("#8A2E2E") },
        { QStringLiteral("warped_nylium"),   QColor("#2E6E6E") },
        // 末地
        { QStringLiteral("end_stone"),       QColor("#E4E3A0") },
        { QStringLiteral("purpur_block"),    QColor("#A5789C") },
        // 建筑常见
        { QStringLiteral("bricks"),          QColor("#96554A") },
        { QStringLiteral("stone_bricks"),    QColor("#7F7F7F") },
        { QStringLiteral("mossy_stone_bricks"), QColor("#6E7A5E") },
        { QStringLiteral("obsidian"),        QColor("#1B1329") },
        { QStringLiteral("terracotta"),      QColor("#985E43") },
        { QStringLiteral("white_wool"),      QColor("#EFEFEF") },
        { QStringLiteral("glass"),           QColor("#D5E8F0") },
        { QStringLiteral("glow_lichen"),     QColor("#7FA0A0") },
    };

    const auto it = kTable.constFind(n);
    if (it != kTable.constEnd())
        return *it;

    // 关键字兜底：调色板里大量装饰方块不逐个列举
    if (n.contains(QLatin1String("leaves")))       return QColor("#4E7F32");
    if (n.endsWith(QLatin1String("_log"))
     || n.endsWith(QLatin1String("_wood"))
     || n.contains(QLatin1String("stem")))         return QColor("#6B5233");
    if (n.contains(QLatin1String("planks")))       return QColor("#9C7F4E");
    if (n.contains(QLatin1String("wool")))         return QColor("#D9D9D9");
    if (n.contains(QLatin1String("carpet")))       return QColor("#C8C8C8");
    if (n.contains(QLatin1String("concrete")))     return QColor("#A0A0A0");
    if (n.contains(QLatin1String("terracotta")))   return QColor("#985E43");
    if (n.contains(QLatin1String("sand")))         return QColor("#DBD3A0");
    if (n.contains(QLatin1String("snow"))
     || n.contains(QLatin1String("ice")))          return QColor("#E4F0FF");
    if (n.contains(QLatin1String("water")))        return QColor("#3F76E4");
    if (n.contains(QLatin1String("lava")))         return QColor("#E06A12");
    if (n.contains(QLatin1String("grass")))        return QColor("#77BB55");
    if (n.contains(QLatin1String("coral")))        return QColor("#C96C8A");
    if (n.contains(QLatin1String("mushroom")))     return QColor("#9B6B4E");
    if (n.contains(QLatin1String("stone"))
     || n.contains(QLatin1String("brick"))
     || n.contains(QLatin1String("ore")))          return QColor("#7F7F7F");
    if (n.contains(QLatin1String("dirt")))         return QColor("#8B6A47");
    return QColor("#8A8A8A");
}

} // namespace MapBlockPalette
