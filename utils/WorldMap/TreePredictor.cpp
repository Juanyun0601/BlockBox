/**
 * @file   TreePredictor.cpp
 * @brief  未生成区块的树木/树冠位置预测实现
 * @author BlockBox Team
 * @date   2026-10-28
 *
 * 流程：区块 → 装饰种子（setDecorationSeed 同构）→ 每棵树在 16×16 区块里
 * 抽散点 → 按群系密度表挑树种 → 按树种画俯视树冠（顶面高度 + 轮廓）。
 * 全程只依赖世界种子，同一区块每次结果一致，相邻 region 接缝处也不会断树。
 */

#include "TreePredictor.h"

#include <algorithm>
#include <cmath>

#include "McRandom.h"
#include "TreeGen.h"
#include "biomes.h"

namespace TreePredictor {

namespace {

constexpr int kRegionBlocks = 512;   // 一个 region 的方块边长
constexpr int kSeaLevel = 63;

/** 树种权重 */
struct Mix
{
    quint8 kind;
    quint8 weight;
};

/** 一个群系的树木规则：每区块几棵、树种配比、零星孤树概率 */
struct Rule
{
    quint8 count = 0;            // 每区块树木数量（近似原版）
    quint8 mixN = 0;
    Mix mix[4] = {};
    quint8 rareKind = 0xFF;      // 0xFF = 没有零星孤树
    quint8 rareOneIn = 0;        // 孤树概率 1/N
};

/** 树冠外观：半径、高度、轮廓、顶面剖面 */
struct Shape
{
    quint8 radius;
    quint8 hMin;                 // 树冠顶面高出地表的最少格数
    quint8 hVar;
    quint8 profile;              // 0 平顶 / 1 圆顶 / 2 锥形
    quint8 mask;                 // 0 圆 / 1 菱形 / 2 椭圆 / 3 十字
};

enum Profile { Flat = 0, Dome = 1, Cone = 2 };
enum Mask { Round = 0, Diamond = 1, Ellipse = 2, Cross = 3 };

const Shape kShapes[KindCount] = {
    /* Oak     */ { 2, 5, 2, Dome, Round },
    /* Birch   */ { 2, 6, 2, Dome, Diamond },
    /* Spruce  */ { 2, 8, 4, Cone, Diamond },
    /* Jungle  */ { 3, 8, 5, Dome, Round },
    /* Acacia  */ { 2, 6, 2, Flat, Ellipse },
    /* DarkOak */ { 3, 6, 3, Flat, Round },
    /* Cherry  */ { 3, 7, 3, Dome, Round },
    /* PaleOak */ { 3, 9, 4, Dome, Round },
    /* Mangrove*/ { 2, 5, 3, Flat, Diamond },
    /* SwampOak*/ { 2, 5, 2, Dome, Diamond },
};

quint32 hash32(quint64 v)
{
    v ^= v >> 33;
    v *= 0xFF51AFD7ED558CCDULL;
    v ^= v >> 33;
    v *= 0xC4CEB9FE1A85EC53ULL;
    v ^= v >> 33;
    return quint32(v);
}

/** 群系 → 树木规则（1.13 之前的变异体 id-128 折叠，1.16+ 新群系单独列出） */
Rule ruleFor(int biome)
{
    // 1.16+ / 1.18+ 的新群系落在 128..199 段，不能按旧版变异体折叠
    switch (biome)
    {
    case giant_spruce_taiga:
    case giant_spruce_taiga_hills:
        return Rule{ 10, 1, { { Spruce, 100 } }, 0xFF, 0 };
    case bamboo_jungle:
    case bamboo_jungle_hills:
        return Rule{ 12, 1, { { Jungle, 100 } }, 0xFF, 0 };
    case mangrove_swamp:
        return Rule{ 3, 1, { { Mangrove, 100 } }, 0xFF, 0 };
    case cherry_grove:
        return Rule{ 10, 1, { { Cherry, 100 } }, 0xFF, 0 };
    case pale_garden:
        return Rule{ 10, 1, { { PaleOak, 100 } }, 0xFF, 0 };
    case meadow:
        return Rule{ 0, 0, {}, Birch, 8 };
    case grove:
        return Rule{ 10, 1, { { Spruce, 100 } }, 0xFF, 0 };
    case snowy_slopes:
        return Rule{ 3, 1, { { Spruce, 100 } }, 0xFF, 0 };
    case shattered_savanna:
    case shattered_savanna_plateau:
        return Rule{ 2, 2, { { Acacia, 85 }, { Oak, 15 } }, 0xFF, 0 };
    case modified_jungle:
    case modified_jungle_edge:
        return Rule{ 36, 2, { { Jungle, 92 }, { Oak, 8 } }, 0xFF, 0 };
    case tall_birch_forest:
    case tall_birch_hills:
        return Rule{ 10, 2, { { Birch, 90 }, { Oak, 10 } }, 0xFF, 0 };
    case dark_forest_hills:
        return Rule{ 14, 2, { { DarkOak, 78 }, { Oak, 22 } }, 0xFF, 0 };
    case taiga_mountains:
    case snowy_taiga_mountains:
        return Rule{ 10, 1, { { Spruce, 100 } }, 0xFF, 0 };
    case modified_wooded_badlands_plateau:
        return Rule{ 6, 1, { { Oak, 100 } }, 0xFF, 0 };
    default:
        break;
    }

    if (biome >= 128 && biome < 168)
        biome -= 128;

    switch (biome)
    {
    case forest: case wooded_hills:          // flower_forest 折叠到 forest
        return Rule{ 10, 2, { { Oak, 76 }, { Birch, 24 } }, 0xFF, 0 };
    case birch_forest: case birch_forest_hills:
        return Rule{ 10, 2, { { Birch, 85 }, { Oak, 15 } }, 0xFF, 0 };
    case dark_forest:
        return Rule{ 14, 2, { { DarkOak, 78 }, { Oak, 22 } }, 0xFF, 0 };
    case taiga: case taiga_hills:
        return Rule{ 10, 2, { { Spruce, 85 }, { Oak, 15 } }, 0xFF, 0 };
    case snowy_taiga: case snowy_taiga_hills:
        // 雪针叶林是纯云杉（原版树木特征里没有橡树），混橡树会画出阔叶圆头
        return Rule{ 10, 1, { { Spruce, 100 } }, 0xFF, 0 };
    case giant_tree_taiga: case giant_tree_taiga_hills:
        return Rule{ 10, 2, { { Spruce, 55 }, { Oak, 45 } }, 0xFF, 0 };
    case jungle: case jungle_hills:
        return Rule{ 36, 2, { { Jungle, 92 }, { Oak, 8 } }, 0xFF, 0 };
    case jungle_edge:
        return Rule{ 4, 2, { { Jungle, 70 }, { Oak, 30 } }, 0xFF, 0 };
    case swamp: case swamp_hills:
        return Rule{ 3, 1, { { SwampOak, 100 } }, 0xFF, 0 };
    case savanna: case savanna_plateau:
        return Rule{ 2, 2, { { Acacia, 78 }, { Oak, 22 } }, 0xFF, 0 };
    case wooded_badlands_plateau:
        return Rule{ 6, 1, { { Oak, 100 } }, 0xFF, 0 };
    case wooded_mountains:                    // windswept_forest
        return Rule{ 8, 2, { { Spruce, 60 }, { Oak, 40 } }, 0xFF, 0 };
    case mountains:                           // windswept_hills
        return Rule{ 1, 1, { { Spruce, 100 } }, 0xFF, 0 };
    case plains:                              // sunflower_plains 折叠到 plains
        return Rule{ 0, 0, {}, Oak, 9 };
    default:
        return Rule{};
    }
}

bool mayHaveTrees(int biome)
{
    const Rule r = ruleFor(biome);
    return r.count > 0 || r.rareKind != 0xFF;
}

/** 原版装饰种子：setSeed(levelSeed) → i=nextLong()|1, j=nextLong()|1 → k=x*i+z*j^levelSeed */
quint64 decorationSeed(quint64 levelSeed, qint64 blockX, qint64 blockZ)
{
    McRandom::Legacy rng(levelSeed);
    const quint64 i = rng.nextLong() | 1ULL;
    const quint64 j = rng.nextLong() | 1ULL;
    const quint64 k = (quint64(blockX) * i + quint64(blockZ) * j) ^ levelSeed;
    return k;
}

quint8 pickKind(const Rule &rule, McRandom::Legacy &rng)
{
    int total = 0;
    for (int i = 0; i < rule.mixN; ++i)
        total += rule.mix[i].weight;
    if (total <= 0)
        return 0xFF;
    int pick = rng.nextInt(total);
    for (int i = 0; i < rule.mixN; ++i)
    {
        pick -= rule.mix[i].weight;
        if (pick < 0)
            return rule.mix[i].kind;
    }
    return rule.mix[0].kind;
}

bool inMask(int mask, int radius, int dx, int dz, quint32 hsh)
{
    const int ax = qAbs(dx), az = qAbs(dz);
    switch (mask)
    {
    case Diamond:
        return ax + az <= radius + 1;
    case Ellipse:
    {
        // 金合欢的伞冠：东西宽、南北窄
        const qreal rx = radius + 0.7;
        const qreal rz = radius * 0.8 + 0.3;
        return (dx * dx) / (rx * rx) + (dz * dz) / (rz * rz) <= 1.0;
    }
    case Cross:
        return ax + az <= radius;
    default:
    {
        const int d2 = dx * dx + dz * dz;
        if (d2 > radius * radius)
            return false;
        if (d2 == radius * radius)
            return (hsh & 3) != 0;   // 轮廓边缘随机缺一格，别太规整
        return true;
    }
    }
}

} // namespace

bool supported(int mc)
{
    return mc >= MC_B1_7 && mc != MC_UNDEF;
}

quint32 cellNoise(int blockX, int blockZ, quint64 seed)
{
    return hash32((quint64(quint32(blockX)) << 32)
                  ^ quint64(quint32(blockZ)) ^ (seed * 0x9E3779B97F4A7C15ULL));
}

void overlayCanopy(QVector<qint16> &top, QVector<quint8> &kind,
                   int originX, int originZ, int grid,
                   quint64 seed, int mc,
                   const QVector<int> &ids, const QVector<float> &ys)
{
    if (grid <= 0 || ids.size() < grid * grid || ys.size() < grid * grid)
        return;
    if (top.size() != grid * grid)
        top.fill(kNoCanopy, grid * grid);
    if (kind.size() != grid * grid)
        kind.fill(quint8(0), grid * grid);

    // 1.21.4：按原版装饰管线逐棵复刻（官方 worldgen 数据生成的特征表）
    if (TreeGen::supported(mc) && grid == kRegionBlocks)
    {
        TreeGen::generate(top, kind, originX, originZ, grid, seed, ids, ys);
        return;
    }

    // 方块 → 栅格下标（栅格恒铺满 512 格，grid 可能小于 512）
    const auto cellAt = [&](int bx, int bz) {
        const int ci = std::clamp((bx - originX) * grid / kRegionBlocks, 0, grid - 1);
        const int cj = std::clamp((bz - originZ) * grid / kRegionBlocks, 0, grid - 1);
        return cj * grid + ci;
    };

    const int cx0 = originX >> 4;   // region 左上角所在区块
    const int cz0 = originZ >> 4;
    // 多算一圈相邻区块：邻区块的树冠会压进本 region 边界，少这一圈接缝处断树
    for (int cz = cz0 - 1; cz <= cz0 + 32; ++cz)
    {
        for (int cx = cx0 - 1; cx <= cx0 + 32; ++cx)
        {
            const int center = cellAt(cx * 16 + 8, cz * 16 + 8);
            const Rule rule = ruleFor(ids[center]);
            const bool rare = (rule.rareKind != 0xFF && rule.rareOneIn > 0);
            if (rule.count == 0 && !rare)
                continue;

            const quint64 deco = decorationSeed(seed, qint64(cx) * 16,
                                                qint64(cz) * 16);
            const int total = rule.count + (rare ? 1 : 0);
            for (int t = 0; t < total; ++t)
            {
                // 每棵树单独加盐：与原版"每个特征重新播种"同构
                McRandom::Legacy rng(deco ^ (quint64(t + 1) * 0x9E3779B97F4A7C15ULL));
                const int ox = rng.nextInt(16);
                const int oz = rng.nextInt(16);

                quint8 treeKind = 0xFF;
                if (t == rule.count)
                {
                    if (rng.nextInt(rule.rareOneIn) != 0)
                        continue;
                    treeKind = rule.rareKind;
                }
                else
                {
                    treeKind = pickKind(rule, rng);
                }
                if (treeKind >= KindCount)
                    continue;

                const int bx = cx * 16 + ox;
                const int bz = cz * 16 + oz;
                const int idx = cellAt(bx, bz);
                const int localBiome = ids[idx];
                if (!mayHaveTrees(localBiome))
                    continue;   // 落在河/海/沙漠里就不长树
                if (isOceanic(localBiome) || localBiome == river
                    || localBiome == frozen_river)
                    continue;
                const float groundY = ys[idx];
                if (groundY < float(kSeaLevel + 1))
                    continue;   // 水位以下不长树

                const Shape &shape = kShapes[treeKind];
                const int height = int(shape.hMin)
                    + (shape.hVar ? int(rng.nextInt(int(shape.hVar) + 1)) : 0);
                const int topY = int(std::lround(groundY)) + height;

                for (int dz = -int(shape.radius); dz <= int(shape.radius); ++dz)
                {
                    for (int dx = -int(shape.radius); dx <= int(shape.radius); ++dx)
                    {
                        const int px = bx + dx;
                        const int pz = bz + dz;
                        if (px < originX || px >= originX + kRegionBlocks
                            || pz < originZ || pz >= originZ + kRegionBlocks)
                            continue;
                        const quint32 hsh = cellNoise(px, pz, seed);
                        if (!inMask(shape.mask, int(shape.radius), dx, dz, hsh))
                            continue;

                        const int dist = std::max(qAbs(dx), qAbs(dz));
                        int drop = 0;
                        if (shape.profile == Dome)
                            drop = (dist == int(shape.radius)) ? 1 : 0;
                        else if (shape.profile == Cone)
                            drop = dist;

                        const int cy = topY - drop;
                        const int pi = std::clamp(
                            (px - originX) * grid / kRegionBlocks, 0, grid - 1);
                        const int pj = std::clamp(
                            (pz - originZ) * grid / kRegionBlocks, 0, grid - 1);
                        const int cell = pj * grid + pi;
                        if (cy > int(top[cell]))
                        {
                            top[cell] = qint16(cy);
                            kind[cell] = treeKind;
                        }
                    }
                }
            }
        }
    }
}

QString leafBlockName(int kind)
{
    switch (kind)
    {
    case Birch:    return QStringLiteral("birch_leaves");
    case Spruce:   return QStringLiteral("spruce_leaves");
    case Jungle:   return QStringLiteral("jungle_leaves");
    case Acacia:   return QStringLiteral("acacia_leaves");
    case DarkOak:  return QStringLiteral("dark_oak_leaves");
    case Cherry:   return QStringLiteral("cherry_leaves");
    case PaleOak:  return QStringLiteral("azalea_leaves");
    case Mangrove: return QStringLiteral("mangrove_leaves");
    case SwampOak: return QStringLiteral("oak_leaves");
    default:       return QStringLiteral("oak_leaves");
    }
}

} // namespace TreePredictor
