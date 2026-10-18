/**
 * @file   BiomeCatalog.cpp
 * @brief  常见群系表 + 按种子采样 / 定位群系 —— Cubiomes 封装
 * @author BlockBox Team
 * @date   2026-10-18
 */

#include "BiomeCatalog.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

#include "biomes.h"     // enum BiomeID / MCVersion / DIM_*
#include "generator.h"  // Generator / getBiomeAt（自带 extern "C" 保护）

namespace BiomeCatalog {

namespace {

struct GenBundle
{
    Generator g;
    int mc = MC_UNDEF;
    quint64 seed = 0;
    int dim = DIM_UNDEF;
    bool inited = false;
};

/**
 * Generator 按线程缓存：cubiomes 的 biome 层状态很重，采样又发生在
 * 结构查询同一个工作线程里 —— thread_local 互不干扰、无锁。
 */
Generator *generatorFor(int mc, quint64 seed, int dim)
{
    static thread_local GenBundle b;
    if (!b.inited || b.mc != mc || b.seed != seed)
    {
        setupGenerator(&b.g, mc, 0);
        b.mc = mc;
        b.seed = seed;
        b.dim = DIM_UNDEF;
        b.inited = true;
    }
    if (b.dim != dim)
    {
        applySeed(&b.g, dim, seed);
        b.dim = dim;
    }
    return &b.g;
}

/** 采样单点群系（基础 id）；坐标为方块坐标，内部换算 scale-4（y 同步 >>2） */
int sampleAt(Generator *g, int dim, qint64 x, qint64 z)
{
    const int yBlocks = (dim == DIM_NETHER) ? 64 : (dim == DIM_END ? 0 : 64);
    const int id = getBiomeAt(g, 4, int(x >> 2), yBlocks >> 2, int(z >> 2));
    return baseId(id);
}

} // namespace

const QVector<BiomeDef> &allBiomes()
{
    // 主世界（现代名；颜色为地图叠加层代表色，取自原版地图调色近似值）
    static const QVector<BiomeDef> kDefs = {
        // ── 主世界：海洋 / 河流 ──
        { ocean,                 "ocean",                 "海洋",         "Ocean",                 0,  QColor(0x3F, 0x76, 0xE4) },
        { deep_ocean,            "deep_ocean",            "深海",         "Deep Ocean",            0,  QColor(0x34, 0x5B, 0x9C) },
        { frozen_ocean,          "frozen_ocean",          "冻结海洋",     "Frozen Ocean",          0,  QColor(0x8C, 0x9C, 0xB8) },
        { deep_frozen_ocean,     "deep_frozen_ocean",     "深冻结海洋",   "Deep Frozen Ocean",     0,  QColor(0x74, 0x88, 0xA8) },
        { cold_ocean,            "cold_ocean",            "冷水海洋",     "Cold Ocean",            0,  QColor(0x3D, 0x6A, 0xA8) },
        { deep_cold_ocean,       "deep_cold_ocean",       "深冷水海洋",   "Deep Cold Ocean",       0,  QColor(0x2F, 0x55, 0x8C) },
        { lukewarm_ocean,        "lukewarm_ocean",        "温水海洋",     "Lukewarm Ocean",        0,  QColor(0x45, 0xA2, 0xC4) },
        { deep_lukewarm_ocean,   "deep_lukewarm_ocean",   "深温水海洋",   "Deep Lukewarm Ocean",   0,  QColor(0x37, 0x84, 0xA8) },
        { warm_ocean,            "warm_ocean",            "暖水海洋",     "Warm Ocean",            0,  QColor(0x43, 0xC5, 0xCA) },
        { river,                 "river",                 "河流",         "River",                 0,  QColor(0x40, 0x7F, 0xFF) },
        { frozen_river,          "frozen_river",          "冻结河流",     "Frozen River",          0,  QColor(0xA0, 0xA8, 0xD0) },
        // ── 主世界：平原 / 森林 ──
        { plains,                "plains",                "平原",         "Plains",                0,  QColor(0x8D, 0xB3, 0x60) },
        { sunflower_plains,      "sunflower_plains",      "向日葵平原",   "Sunflower Plains",      0,  QColor(0xB1, 0xB7, 0x5D) },
        { forest,                "forest",                "森林",         "Forest",                0,  QColor(0x55, 0xA0, 0x4A) },
        { flower_forest,         "flower_forest",         "繁花森林",     "Flower Forest",         0,  QColor(0x77, 0xBB, 0x5E) },
        { birch_forest,          "birch_forest",          "白桦林",       "Birch Forest",          0,  QColor(0x80, 0xB4, 0x97) },
        { old_growth_birch_forest, "old_growth_birch_forest", "高大白桦林", "Old Growth Birch Forest", 0, QColor(0x6F, 0xA8, 0x6C) },
        { dark_forest,           "dark_forest",           "黑森林",       "Dark Forest",           0,  QColor(0x40, 0x51, 0x3A) },
        { pale_garden,           "pale_garden",           "苍白之园",     "Pale Garden",           0,  QColor(0x8A, 0x8F, 0x7E) },
        { taiga,                 "taiga",                 "针叶林",       "Taiga",                 0,  QColor(0x5B, 0x9C, 0x89) },
        { snowy_taiga,           "snowy_taiga",           "雪原针叶林",   "Snowy Taiga",           0,  QColor(0xA0, 0xC8, 0xB0) },
        { old_growth_pine_taiga, "old_growth_pine_taiga", "高大松林",     "Old Growth Pine Taiga", 0,  QColor(0x68, 0x8C, 0x64) },
        { old_growth_spruce_taiga, "old_growth_spruce_taiga", "高大云杉林", "Old Growth Spruce Taiga", 0, QColor(0x6A, 0x8E, 0x6E) },
        // ── 主世界：丛林 / 沼泽 ──
        { jungle,                "jungle",                "丛林",         "Jungle",                0,  QColor(0x59, 0xC9, 0x3C) },
        { bamboo_jungle,         "bamboo_jungle",         "竹林",         "Bamboo Jungle",         0,  QColor(0x77, 0xBB, 0x2E) },
        { sparse_jungle,         "sparse_jungle",         "稀疏丛林",     "Sparse Jungle",         0,  QColor(0x6A, 0xA8, 0x4A) },
        { swamp,                 "swamp",                 "沼泽",         "Swamp",                 0,  QColor(0x61, 0x7B, 0x64) },
        { mangrove_swamp,        "mangrove_swamp",        "红树沼泽",     "Mangrove Swamp",        0,  QColor(0x5A, 0x7A, 0x5E) },
        // ── 主世界：沙漠 / 恶地 ──
        { desert,                "desert",                "沙漠",         "Desert",                0,  QColor(0xD9, 0xD0, 0x8E) },
        { badlands,              "badlands",              "恶地",         "Badlands",              0,  QColor(0xC4, 0x6A, 0x3C) },
        { eroded_badlands,       "eroded_badlands",       "侵蚀恶地",     "Eroded Badlands",       0,  QColor(0xB8, 0x5C, 0x38) },
        { wooded_badlands,       "wooded_badlands",       "疏林恶地",     "Wooded Badlands",       0,  QColor(0xA0, 0x70, 0x48) },
        // ── 主世界：热带草原 / 丘陵 ──
        { savanna,               "savanna",               "热带草原",     "Savanna",               0,  QColor(0xBF, 0xB7, 0x55) },
        { savanna_plateau,       "savanna_plateau",       "热带高原",     "Savanna Plateau",       0,  QColor(0xB5, 0xA8, 0x4A) },
        { windswept_savanna,     "windswept_savanna",     "风袭热带草原", "Windswept Savanna",     0,  QColor(0xA8, 0xA8, 0x50) },
        { windswept_hills,       "windswept_hills",       "风袭丘陵",     "Windswept Hills",       0,  QColor(0x8A, 0x9A, 0x7A) },
        { windswept_gravelly_hills, "windswept_gravelly_hills", "风袭砂砾丘陵", "Windswept Gravelly Hills", 0, QColor(0x8A, 0x8A, 0x80) },
        { windswept_forest,      "windswept_forest",      "风袭森林",     "Windswept Forest",      0,  QColor(0x4F, 0x7A, 0x4A) },
        // ── 主世界：雪原 / 山地 ──
        { snowy_plains,          "snowy_plains",          "雪原",         "Snowy Plains",          0,  QColor(0xFF, 0xFF, 0xFF) },
        { ice_spikes,            "ice_spikes",            "冰刺之地",     "Ice Spikes",            0,  QColor(0xE8, 0xF0, 0xF8) },
        { snowy_beach,           "snowy_beach",           "雪滩",         "Snowy Beach",           0,  QColor(0xD0, 0xE0, 0xE8) },
        { beach,                 "beach",                 "海滩",         "Beach",                 0,  QColor(0xFA, 0xDE, 0x55) },
        { stony_shore,           "stony_shore",           "石岸",         "Stony Shore",           0,  QColor(0x8A, 0x8A, 0x8A) },
        { meadow,                "meadow",                "草甸",         "Meadow",                0,  QColor(0x83, 0xBB, 0x6F) },
        { grove,                 "grove",                 "雪原树林",     "Grove",                 0,  QColor(0x7B, 0xBA, 0x9C) },
        { snowy_slopes,          "snowy_slopes",          "雪坡",         "Snowy Slopes",          0,  QColor(0xD8, 0xE8, 0xF0) },
        { jagged_peaks,          "jagged_peaks",          "尖锐山峰",     "Jagged Peaks",          0,  QColor(0xC8, 0xD0, 0xD8) },
        { frozen_peaks,          "frozen_peaks",          "冰封山峰",     "Frozen Peaks",          0,  QColor(0xB0, 0xC8, 0xD8) },
        { stony_peaks,           "stony_peaks",           "石质山峰",     "Stony Peaks",           0,  QColor(0x9A, 0x9A, 0x92) },
        { cherry_grove,          "cherry_grove",          "樱花树林",     "Cherry Grove",          0,  QColor(0xE0, 0xA0, 0xC0) },
        // ── 主世界：洞穴 / 特殊 ──
        { mushroom_fields,       "mushroom_fields",       "蘑菇岛",       "Mushroom Fields",       0,  QColor(0x8F, 0x6B, 0x8F) },
        { mushroom_field_shore,  "mushroom_field_shore",  "蘑菇岛岸",     "Mushroom Field Shore",  0,  QColor(0x8A, 0x68, 0x88) },
        { deep_dark,             "deep_dark",             "深暗之域",     "Deep Dark",             0,  QColor(0x2A, 0x2A, 0x32) },
        { lush_caves,            "lush_caves",            "繁茂洞穴",     "Lush Caves",            0,  QColor(0x5A, 0x8A, 0x4A) },
        { dripstone_caves,       "dripstone_caves",       "溶洞",         "Dripstone Caves",       0,  QColor(0x8A, 0x7A, 0x6A) },
        // ── 下界 ──
        { nether_wastes,         "nether_wastes",         "下界荒地",     "Nether Wastes",         -1, QColor(0x8A, 0x3C, 0x32) },
        { soul_sand_valley,      "soul_sand_valley",      "灵魂沙谷",     "Soul Sand Valley",      -1, QColor(0x5A, 0x40, 0x38) },
        { crimson_forest,        "crimson_forest",        "绯红森林",     "Crimson Forest",        -1, QColor(0x8A, 0x30, 0x38) },
        { warped_forest,         "warped_forest",         "诡异森林",     "Warped Forest",         -1, QColor(0x2A, 0x7A, 0x72) },
        { basalt_deltas,         "basalt_deltas",         "玄武岩三角洲", "Basalt Deltas",         -1, QColor(0x4A, 0x4A, 0x4A) },
        // ── 末地 ──
        { the_end,               "the_end",               "末地",         "The End",               1,  QColor(0xE0, 0xE0, 0xD8) },
        { small_end_islands,     "small_end_islands",     "小型末地岛",   "Small End Islands",     1,  QColor(0xC8, 0xC8, 0xB8) },
        { end_midlands,          "end_midlands",          "末地中地",     "End Midlands",          1,  QColor(0xD0, 0xD0, 0xC0) },
        { end_highlands,         "end_highlands",         "末地高地",     "End Highlands",         1,  QColor(0xD8, 0xD8, 0xC8) },
        { end_barrens,           "end_barrens",           "末地荒地",     "End Barrens",           1,  QColor(0xB0, 0xB0, 0xA0) },
    };
    return kDefs;
}

int baseId(int id)
{
    // 变异群系在 1.13~1.17 为 id+128（与 SeedTerrainRenderer 的折叠一致）；
    // 168+ 为竹林 / 下界群系等独立 id，不能折叠
    if (id >= 128 && id < 168)
        return id - 128;
    return id;
}

SampleGrid sampleGrid(quint64 seed, int mc, int dim,
                      qint64 minX, qint64 minZ, qint64 maxX, qint64 maxZ,
                      int step)
{
    SampleGrid out;
    if (mc == MC_UNDEF || maxX < minX || maxZ < minZ)
        return out;
    step = qMax(16, step);

    const qint64 spanX = maxX - minX + 1;
    const qint64 spanZ = maxZ - minZ + 1;
    // 格子数封顶：叠加层是预览，不需要方块级精度；过大时加大步长
    constexpr int kMaxCells = 160;
    if (spanX / step > kMaxCells)
        step = qMax(step, int(spanX / kMaxCells));
    if (spanZ / step > kMaxCells)
        step = qMax(step, int(spanZ / kMaxCells));
    step = (step + 15) & ~15; // 对齐 16，与 scale-4 采样坐标整除友好

    out.nx = int((spanX + step - 1) / step);
    out.nz = int((spanZ + step - 1) / step);
    if (out.nx <= 0 || out.nz <= 0)
        return SampleGrid();
    out.minX = minX;
    out.minZ = minZ;
    out.step = step;
    out.ids.resize(out.nx * out.nz);

    Generator *g = generatorFor(mc, seed, dim);
    for (int j = 0; j < out.nz; ++j)
    {
        const qint64 z = minZ + qint64(j) * step + step / 2;
        for (int i = 0; i < out.nx; ++i)
        {
            const qint64 x = minX + qint64(i) * step + step / 2;
            out.ids[j * out.nx + i] = sampleAt(g, dim, x, z);
        }
    }
    return out;
}

int biomeAt(quint64 seed, int mc, int dim, qint64 x, qint64 z)
{
    if (mc == MC_UNDEF)
        return -1;
    Generator *g = generatorFor(mc, seed, dim);
    return sampleAt(g, dim, x, z);
}

bool findNearest(quint64 seed, int mc, int dim, int biomeId,
                 qint64 fromX, qint64 fromZ, qint64 maxRadius,
                 qint64 *outX, qint64 *outZ)
{
    if (mc == MC_UNDEF)
        return false;
    const int target = baseId(biomeId);
    Generator *g = generatorFor(mc, seed, dim);

    // 先看脚下，再按环带向外扩（环距 64 → 与群系斑块尺度相当）
    if (sampleAt(g, dim, fromX, fromZ) == target)
    {
        if (outX) *outX = fromX;
        if (outZ) *outZ = fromZ;
        return true;
    }

    const qint64 rMax = qMax<qint64>(64, maxRadius);
    constexpr int kMaxSamples = 6000; // 防极端范围把线程拖死
    int samples = 0;
    for (qint64 r = 64; r <= rMax; r += 64)
    {
        // 每环采样点数随周长增长，但限制在 24~192 之间
        const int n = std::clamp(int(r * 8 / 64), 24, 192);
        for (int k = 0; k < n; ++k)
        {
            const double a = 2.0 * 3.14159265358979323846 * k / n;
            const qint64 x = fromX + qint64(std::llround(std::cos(a) * double(r)));
            const qint64 z = fromZ + qint64(std::llround(std::sin(a) * double(r)));
            if (++samples > kMaxSamples)
                return false;
            if (sampleAt(g, dim, x, z) == target)
            {
                if (outX) *outX = x;
                if (outZ) *outZ = z;
                return true;
            }
        }
    }
    return false;
}

} // namespace BiomeCatalog
