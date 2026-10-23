/**
 * @file   OreFinder.cpp
 * @brief  存档矿物扫描实现（矿物表 + region 扫描）
 * @author BlockBox Team
 * @date   2026-10-18
 */

#include "OreFinder.h"

#include <QPoint>
#include <QStringList>

namespace OreFinder {

namespace {

/**
 * 矿物表：主世界按稀有度从常见到稀有排，下界另起一段。
 * 颜色取自地表配色表里的同款矿石色（地表图与矿物点看图一致）。
 */
const QVector<OreDef> &oreTable()
{
    static const QVector<OreDef> kDefs = {
        // ── 主世界 ──
        { "coal",         "煤矿石",     "Coal Ore",          "coal_ore",         "deepslate_coal_ore",         16, 0, QColor("#5A5A5A") },
        { "iron",         "铁矿石",     "Iron Ore",          "iron_ore",         "deepslate_iron_ore",         15, 0, QColor("#C8A18A") },
        { "copper",       "铜矿石",     "Copper Ore",        "copper_ore",       "deepslate_copper_ore",       -1, 0, QColor("#D77E4A") },
        { "gold",         "金矿石",     "Gold Ore",          "gold_ore",         "deepslate_gold_ore",         14, 0, QColor("#D8B93C") },
        { "redstone",     "红石矿石",   "Redstone Ore",      "redstone_ore",     "deepslate_redstone_ore",     73, 0, QColor("#B03028") },
        { "lapis",        "青金石矿石", "Lapis Lazuli Ore",  "lapis_ore",        "deepslate_lapis_ore",        21, 0, QColor("#3C64B4") },
        { "diamond",      "钻石矿石",   "Diamond Ore",       "diamond_ore",      "deepslate_diamond_ore",      56, 0, QColor("#5AC8C0") },
        { "emerald",      "绿宝石矿石", "Emerald Ore",       "emerald_ore",      "deepslate_emerald_ore",     129, 0, QColor("#3CB05A") },
        // ── 下界 ──
        { "nether_quartz", "下界石英矿石", "Nether Quartz Ore", "nether_quartz_ore", nullptr,               153, -1, QColor("#9C8C84") },
        { "nether_gold",   "下界金矿石",   "Nether Gold Ore",   "nether_gold_ore",   nullptr,                -1, -1, QColor("#D9A03C") },
        { "ancient_debris", "远古残骸",    "Ancient Debris",    "ancient_debris",    nullptr,                -1, -1, QColor("#6B4A46") },
    };
    return kDefs;
}

} // namespace

const QVector<OreDef> &allOres()
{
    return oreTable();
}

namespace {

/** 扫描用的方块名表：blockName / deepslateName 展开成扁平表 + 下标映射 */
struct ScanTables
{
    QStringList names;
    QVector<int> nameOre;   ///< names 下标 → 矿物下标
    QVector<int> legacyIds; ///< names 下标 → 旧版数字 ID
};

const ScanTables &scanTables()
{
    static const ScanTables kTables = []() {
        ScanTables t;
        const QVector<OreDef> &defs = oreTable();
        t.names.reserve(defs.size() * 2);
        for (int i = 0; i < defs.size(); ++i)
        {
            t.names << QString::fromLatin1(defs.at(i).blockName);
            t.nameOre << i;
            t.legacyIds << defs.at(i).legacyId;
            if (defs.at(i).deepslateName && *defs.at(i).deepslateName)
            {
                t.names << QString::fromLatin1(defs.at(i).deepslateName);
                t.nameOre << i;
                t.legacyIds << -1; // 深层变体只存在于 1.17+，没有旧版 ID
            }
        }
        return t;
    }();
    return kTables;
}

/** 方块名下标 → 矿物下标（一次读出全部矿物，勾选集变化无需重扫） */
void remapOres(QVector<Hit> &hits)
{
    const QVector<int> &nameOre = scanTables().nameOre;
    for (Hit &h : hits)
    {
        h.index = (h.index >= 0 && h.index < nameOre.size())
                      ? nameOre.at(h.index) : -1;
    }
}

} // namespace

QVector<Hit> scanRegion(const QString &savePath, WorldMapRenderer::Dimension dim,
                        int regionX, int regionZ,
                        const std::function<bool()> &cancelled)
{
    const ScanTables &t = scanTables();
    QVector<Hit> hits = WorldMapRenderer::scanRegionBlocks(savePath, dim, regionX,
                                                           regionZ, t.names,
                                                           t.legacyIds, cancelled);
    remapOres(hits);
    return hits;
}

QVector<Hit> scanChunks(const QString &savePath, WorldMapRenderer::Dimension dim,
                        const QVector<QPoint> &chunks,
                        const std::function<bool()> &cancelled)
{
    const ScanTables &t = scanTables();
    QVector<Hit> hits = WorldMapRenderer::scanChunksBlocks(savePath, dim, chunks,
                                                           t.names, t.legacyIds,
                                                           cancelled);
    remapOres(hits);
    return hits;
}

QVector<Hit> scanChunk(const QString &savePath, WorldMapRenderer::Dimension dim,
                       int chunkX, int chunkZ,
                       const std::function<bool()> &cancelled)
{
    return scanChunks(savePath, dim, {QPoint(chunkX, chunkZ)}, cancelled);
}

} // namespace OreFinder
