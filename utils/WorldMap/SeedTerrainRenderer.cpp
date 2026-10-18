/**
 * @file   SeedTerrainRenderer.cpp
 * @brief  未加载区块的种子预测地形渲染实现
 * @author BlockBox Team
 * @date   2026-10-15
 *
 * 采样策略：采样栅格恒定铺满整个 region（512 格）。输出分辨率低时用
 * 跨步抽稀（第 i 个样本落在 region 内 i*格宽/N 处），而不是只采左上角
 * 一块再拉伸 —— 后者会让相邻 region 的地形对不齐，边界处错位剪切。
 * 配色/明暗算完后再最近邻映射到输出尺寸，与真实地表图同风格。
 *
 * 方块级精度（outSize > 128，即放大细节瓦片）：
 *  - 群系走 scale=1 的 voronoi（与游戏内一致，边界精确到方块）；
 *  - 1.18+ 高度按方块连续采样气候噪声（不再停在 1:4 粒度）；
 *  - 旧版本高度在 1:4 近似场上做双线性插值，过渡比最近邻平滑。
 */

#include "SeedTerrainRenderer.h"

#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "biomes.h"      // BiomeID / isOceanic / isSnowy / isMesa
#include "generator.h"   // setupGenerator / applySeed / genBiomes / mapApproxHeight
#include "biomenoise.h"  // initSurfaceNoise / Range / sampleBiomeNoise
#include "noise.h"       // sampleDoublePerlin（方块级高度用）

// getSpline 在 biomenoise.c 里是非 static 的，头文件未导出；本地声明即可链接
extern "C" float getSpline(const Spline *sp, const float *vals);

namespace SeedTerrainRenderer {

namespace {

/** 维度纵坐标范围（与 WorldMapRenderer 的高度着色一致） */
void dimensionHeightRange(WorldMapRenderer::Dimension dim, int *minY, int *maxY)
{
    switch (dim)
    {
    case WorldMapRenderer::Dimension::Nether: *minY = 0;   *maxY = 128; break;
    case WorldMapRenderer::Dimension::End:    *minY = 0;   *maxY = 256; break;
    default:                                  *minY = -64; *maxY = 320; break;
    }
}

struct GenBundle
{
    Generator g;
    SurfaceNoise sn;
    int mc = 0;
    quint64 seed = 0;
    int dim = DIM_UNDEF;
    bool inited = false;
};

/**
 * Generator 按线程缓存：cubiomes 的 biome 层/噪声初始化很重，但概览渲染
 * 最多 8 个线程并行，每线程一份互不干扰（thread_local，无锁）。
 */
Generator *generatorFor(int mc, quint64 seed, int dim, SurfaceNoise **snOut)
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
        initSurfaceNoise(&b.sn, dim, seed);
        b.dim = dim;
    }
    if (snOut)
        *snOut = &b.sn;
    return &b.g;
}

/**
 * 群系 id → 地表代表方块颜色（复用 blockColor，保证与真实地图同调色板）。
 * 变异群系（id+128）先折叠回基础群系。返回无效 QColor 表示该处无地表（虚空）。
 */
QColor biomeBaseColor(int id)
{
    using namespace WorldMapRenderer;

    if (id >= 128 && id < 168)
        id -= 128;

    switch (id)
    {
    // ── 海洋 / 河流 ──
    case ocean: case deep_ocean: case warm_ocean: case lukewarm_ocean:
    case cold_ocean: case deep_warm_ocean: case deep_lukewarm_ocean:
    case deep_cold_ocean:
        return blockColor(QStringLiteral("water"));
    case frozen_ocean: case deep_frozen_ocean:
        return blockColor(QStringLiteral("ice"));
    case river:
        return blockColor(QStringLiteral("water"));
    case frozen_river:
        return blockColor(QStringLiteral("ice"));
    // ── 温带陆地 ──
    case plains: case sunflower_plains: case meadow:
        return blockColor(QStringLiteral("grass_block"));
    case forest: case wooded_hills: case flower_forest:
        return blockColor(QStringLiteral("oak_leaves"));
    case birch_forest: case birch_forest_hills:
    case tall_birch_forest: case tall_birch_hills:   // 含别名 old_growth_birch_forest/hills
        return blockColor(QStringLiteral("birch_leaves"));
    case dark_forest: case dark_forest_hills: case pale_garden:
        return blockColor(QStringLiteral("dark_oak_leaves"));
    case swamp: case swamp_hills: case mangrove_swamp:
        return blockColor(QStringLiteral("moss_block"));
    // ── 针叶林 ──
    case taiga: case taiga_hills: case taiga_mountains:
        return blockColor(QStringLiteral("spruce_leaves"));
    case snowy_taiga: case snowy_taiga_hills: case snowy_taiga_mountains:
        return blockColor(QStringLiteral("snow_block"));
    case giant_tree_taiga: case giant_tree_taiga_hills:   // 含别名 old_growth_pine_taiga
    case giant_spruce_taiga: case giant_spruce_taiga_hills:   // 含别名 old_growth_spruce_taiga
        return blockColor(QStringLiteral("podzol"));
    // ── 丛林 ──
    case jungle: case jungle_hills: case jungle_edge:
    case modified_jungle: case modified_jungle_edge:
        return blockColor(QStringLiteral("jungle_leaves"));
    case bamboo_jungle: case bamboo_jungle_hills:
        return blockColor(QStringLiteral("bamboo"));
    // sparse_jungle 是 jungle_edge 的别名（同值），已并入上方 jungle_edge 分支
    // ── 沙漠 / 恶地 ──
    case desert: case desert_hills: case desert_lakes:
        return blockColor(QStringLiteral("sand"));
    case beach: case snowy_beach:
        return blockColor(QStringLiteral("sand"));
    case badlands: case eroded_badlands:
        return blockColor(QStringLiteral("red_sand"));
    case wooded_badlands_plateau: case badlands_plateau:
    case modified_wooded_badlands_plateau: case modified_badlands_plateau:
        return blockColor(QStringLiteral("terracotta"));
    // ── 山地 / 岸边 ──
    case mountains: case mountain_edge: case gravelly_mountains:
    case modified_gravelly_mountains: case stone_shore: case stony_peaks:
        return blockColor(QStringLiteral("stone"));
    case wooded_mountains:
        return blockColor(QStringLiteral("spruce_leaves"));
    case grove: case snowy_slopes: case snowy_mountains:
    case ice_spikes: case snowy_tundra:
        return blockColor(QStringLiteral("snow_block"));
    case jagged_peaks: case frozen_peaks:
        return blockColor(QStringLiteral("snow_block"));
    // ── 草原类 ──
    case savanna: case savanna_plateau:
    case shattered_savanna: case shattered_savanna_plateau:
        return blockColor(QStringLiteral("grass_block"));
    // ── 蘑菇岛 ──
    case mushroom_fields: case mushroom_field_shore:
        return blockColor(QStringLiteral("mycelium"));
    // ── 下界 ──
    case nether_wastes:
        return blockColor(QStringLiteral("netherrack"));
    case soul_sand_valley:
        return blockColor(QStringLiteral("soul_sand"));
    case crimson_forest:
        return blockColor(QStringLiteral("crimson_nylium"));
    case warped_forest:
        return blockColor(QStringLiteral("warped_nylium"));
    case basalt_deltas:
        return blockColor(QStringLiteral("basalt"));
    // ── 末地 ──
    case the_end: case small_end_islands: case end_midlands:
    case end_highlands: case end_barrens:
        return blockColor(QStringLiteral("end_stone"));
    // ── 洞穴类（1.18 地表采样偶发命中，给贴近的兜底色）──
    case lush_caves:
        return blockColor(QStringLiteral("moss_block"));
    case dripstone_caves: case deep_dark:
        return blockColor(QStringLiteral("stone"));
    default:
        break;
    }

    if (id == the_void)
        return QColor(); // 虚空：透明
    if (isOceanic(id))
        return blockColor(QStringLiteral("water"));
    if (isSnowy(id))
        return blockColor(QStringLiteral("snow_block"));
    if (isMesa(id))
        return blockColor(QStringLiteral("red_sand"));
    return blockColor(QStringLiteral("grass_block"));
}

/** 高度明暗系数（0.72 ~ 1.16），与真实地表图同一公式 */
qreal heightFactor(int y, int minY, int maxY)
{
    if (maxY <= minY)
        return 1.0;
    const qreal t = std::clamp((qreal(y) - minY) / qreal(maxY - minY), 0.0, 1.0);
    return 0.72 + 0.44 * t;
}

/** 水体按深度调暗（与真实地表图同一系数） */
QColor deepenWater(QColor c, int depth)
{
    const qreal k = 1.0 - std::min(depth, 20) * 0.027;
    return QColor(std::max(0, int(c.red() * k)),
                  std::max(0, int(c.green() * k)),
                  std::max(0, int(c.blue() * k)));
}

/**
 * 1.18+ 主世界：在 scale-4 连续坐标 (x4, z4) 上采样气候噪声，得到地表近似高度。
 * 与 sampleBiomeNoise / mapApproxHeight 的 1.18 分支同一套公式（shift + 大陆
 * 度/侵蚀/怪异度样条）；传入可为方块坐标/4 的连续值，因此相邻方块之间高度
 * 会平滑变化，不再停在 4 格台阶上。
 */
float sampleHeight118(const Generator *g, double x4, double z4)
{
    const BiomeNoise *bn = &g->bn;
    double px = x4, pz = z4;
    px += sampleDoublePerlin(&bn->climate[NP_SHIFT], x4, 0, z4) * 4.0;
    pz += sampleDoublePerlin(&bn->climate[NP_SHIFT], z4, x4, 0) * 4.0;

    const float c = float(sampleDoublePerlin(&bn->climate[NP_CONTINENTALNESS], px, 0, pz));
    const float e = float(sampleDoublePerlin(&bn->climate[NP_EROSION], px, 0, pz));
    const float w = float(sampleDoublePerlin(&bn->climate[NP_WEIRDNESS], px, 0, pz));

    const float npParam[] = {
        c, e, -3.0F * (std::fabs(std::fabs(w) - 0.6666667F) - 0.33333334F), w,
    };
    const double off = getSpline(bn->sp, npParam) + 0.015F;
    // y=0 的深度，与 sampleBiomeNoise 里 NP_DEPTH 的算法一致
    const float d = 1.0F - 83.0F / 160.0F + float(off);
    return float(int64_t(10000.0F * d)) / 76.0F;
}

/** 双线性插值 1:4 高度场 → 方块级（src 为 n×n 的 scale-4 网格） */
float bilinearHeight(const QVector<float> &src, int n, double fx, double fz)
{
    fx = std::clamp(fx, 0.0, double(n - 1));
    fz = std::clamp(fz, 0.0, double(n - 1));
    const int x0 = int(fx);
    const int z0 = int(fz);
    const int x1 = std::min(x0 + 1, n - 1);
    const int z1 = std::min(z0 + 1, n - 1);
    const double tx = fx - x0;
    const double tz = fz - z0;
    const float h00 = src[z0 * n + x0];
    const float h10 = src[z0 * n + x1];
    const float h01 = src[z1 * n + x0];
    const float h11 = src[z1 * n + x1];
    return float((h00 * (1.0 - tx) + h10 * tx) * (1.0 - tz)
               + (h01 * (1.0 - tx) + h11 * tx) * tz);
}

} // namespace

bool supported(int mc, WorldMapRenderer::Dimension dim)
{
    if (mc == MC_UNDEF)
        return false;
    // cubiomes 对三个维度都有群系生成；版本可识别即可预测
    Q_UNUSED(dim);
    return true;
}

QImage renderRegionTile(int regionX, int regionZ,
                        WorldMapRenderer::Dimension dim,
                        WorldMapRenderer::ShadeMode mode,
                        quint64 seed, int mc, int outSize,
                        int maxSurfaceY)
{
    if (mc == MC_UNDEF || outSize < 4 || outSize > 512)
        return QImage();

    const int cdim = (dim == WorldMapRenderer::Dimension::Nether) ? DIM_NETHER
                   : (dim == WorldMapRenderer::Dimension::End)    ? DIM_END
                                                                  : DIM_OVERWORLD;

    // 采样栅格：样本数跟着输出走。outSize>128 时升到方块级（最多 512=1 格/像素），
    // 概览仍封顶 128（群系 1:4 粒度已足够）。坐标跨步铺满整个 region ——
    // 绝不只采左上角一块再拉伸
    const bool blockLevel = (outSize > 128);
    const int N = blockLevel ? std::clamp(outSize, 129, 512)
                             : std::clamp(outSize, 16, 128);
    const int rx4 = regionX * 128;  // scale-4 坐标：128 格/region
    const int rz4 = regionZ * 128;
    const int rx16 = regionX * 32;  // scale-16 坐标：32 格/region
    const int rz16 = regionZ * 32;
    const int rx1 = regionX * 512;  // 方块坐标：512 格/region
    const int rz1 = regionZ * 512;

    SurfaceNoise *sn = nullptr;
    Generator *g = generatorFor(mc, seed, cdim, &sn);

    // grid = 实际采样栅格边长（最终按 grid → outSize 最近邻映射）
    const bool fullRes = blockLevel || (N >= 128);
    const int grid = N;
    QVector<int> ids(grid * grid, 0);
    QVector<float> ys(grid * grid, 64.0f);

    if (cdim == DIM_OVERWORLD && mc >= MC_1_18)
    {
        // 1.18+ 方块级：群系 scale=1 voronoi（边界精确到方块，恒铺满整 region），
        // 高度按方块连续采样气候噪声（与 mapApproxHeight 同公式）
        if (blockLevel)
        {
            Range r;
            r.scale = 1;
            r.x = rx1;
            r.z = rz1;
            r.sx = 512;
            r.sz = 512;
            r.y = 64; // 地表附近采 3D 群系
            r.sy = 1;
            int *cache = allocCache(g, r);
            if (!cache)
                return QImage();
            const int rc = genBiomes(g, cache, r);
            if (rc == 0)
            {
                for (int j = 0; j < grid; ++j)
                {
                    // 方块坐标 → scale-4 连续坐标
                    const double z4 = (rz1 + j * 512.0 / grid) / 4.0;
                    for (int i = 0; i < grid; ++i)
                    {
                        const double x4 = (rx1 + i * 512.0 / grid) / 4.0;
                        const int k = j * grid + i;
                        ids[k] = cache[(j * 512 / grid) * 512 + i * 512 / grid];
                        ys[k] = sampleHeight118(g, x4, z4);
                    }
                }
            }
            free(cache);
            if (rc != 0)
                return QImage();
        }
        else
        {
            // 概览：与 1.17- 同路径 —— mapApproxHeight 整段 128×128 采完再
            // 跨步抽稀到 grid。此前逐点 sampleBiomeNoise 在 radius 20 画布
            // （~1700 region）上会卡到拖不动；同一公式但整图一次算完快得多
            QVector<int> fullIds(128 * 128, 0);
            QVector<float> fullYs(128 * 128, 64.0f);
            if (mapApproxHeight(fullYs.data(), fullIds.data(), g, sn,
                                rx4, rz4, 128, 128) != 0)
                return QImage();
            if (grid < 128)
            {
                for (int j = 0; j < grid; ++j)
                    for (int i = 0; i < grid; ++i)
                    {
                        const int src = (j * 128 / grid) * 128 + i * 128 / grid;
                        ids[j * grid + i] = fullIds[src];
                        ys[j * grid + i] = fullYs[src];
                    }
            }
            else
            {
                ids = fullIds;
                ys = fullYs;
            }
        }
    }
    else if (cdim == DIM_OVERWORLD)
    {
        // 1.17-：mapApproxHeight 固定 1:4 连续采样整 region（128×128）。
        // 方块级时群系走 scale=1 voronoi，高度在 1:4 场上双线性插值到 grid
        QVector<int> fullIds(128 * 128, 0);
        QVector<float> fullYs(128 * 128, 64.0f);
        if (mapApproxHeight(fullYs.data(), fullIds.data(), g, sn,
                            rx4, rz4, 128, 128) != 0)
            return QImage();

        if (blockLevel)
        {
            Range r;
            r.scale = 1;
            r.x = rx1;
            r.z = rz1;
            r.sx = 512;
            r.sz = 512;
            r.y = 64;
            r.sy = 1;
            int *cache = allocCache(g, r);
            if (!cache)
                return QImage();
            const int rc = genBiomes(g, cache, r);
            if (rc == 0)
            {
                for (int j = 0; j < grid; ++j)
                    for (int i = 0; i < grid; ++i)
                    {
                        const int k = j * grid + i;
                        ids[k] = cache[(j * 512 / grid) * 512 + i * 512 / grid];
                        // 方块在 region 内的 scale-4 连续坐标 → 双线性高度
                        const double fx = i * 128.0 / grid;
                        const double fz = j * 128.0 / grid;
                        ys[k] = bilinearHeight(fullYs, 128, fx, fz);
                    }
            }
            free(cache);
            if (rc != 0)
                return QImage();
        }
        else if (grid < 128)
        {
            for (int j = 0; j < grid; ++j)
                for (int i = 0; i < grid; ++i)
                {
                    const int src = (j * 128 / grid) * 128 + i * 128 / grid;
                    ids[j * grid + i] = fullIds[src];
                    ys[j * grid + i] = fullYs[src];
                }
        }
        else
        {
            ids = fullIds;
            ys = fullYs;
        }
    }
    else
    {
        // 下界 / 末地：概览用 scale 16（32×32），细节用 scale 4（128×128），
        // 方块级用 scale 1 voronoi（恒铺满整 region）；末地另取近似高度
        Range r;
        if (blockLevel)
        {
            r.scale = 1;
            r.x = rx1;
            r.z = rz1;
            r.sx = 512;
            r.sz = 512;
            r.y = (cdim == DIM_NETHER) ? 64 : 0; // 下界群系在近地表采样
        }
        else if (fullRes)
        {
            r.scale = 4;
            r.x = rx4;
            r.z = rz4;
            r.sx = 128;
            r.sz = 128;
            r.y = (cdim == DIM_NETHER) ? 16 : 0;
        }
        else
        {
            r.scale = 16;
            r.x = rx16;
            r.z = rz16;
            r.sx = 32;
            r.sz = 32;
            r.y = (cdim == DIM_NETHER) ? 4 : 0;
        }
        r.sy = 1;
        int *cache = allocCache(g, r);
        if (!cache)
            return QImage();
        const int rc = genBiomes(g, cache, r);
        if (rc == 0)
        {
            for (int j = 0; j < grid; ++j)
                for (int i = 0; i < grid; ++i)
                    ids[j * grid + i] =
                        cache[(j * r.sz / grid) * r.sx + i * r.sx / grid];
        }
        free(cache);
        if (rc != 0)
            return QImage();
        if (cdim == DIM_END && fullRes)
        {
            QVector<int> fullIds(128 * 128, 0);
            QVector<float> fullYs(128 * 128, 64.0f);
            if (mapApproxHeight(fullYs.data(), fullIds.data(), g, sn,
                                rx4, rz4, 128, 128) == 0)
            {
                if (blockLevel)
                {
                    for (int j = 0; j < grid; ++j)
                        for (int i = 0; i < grid; ++i)
                            ys[j * grid + i] = bilinearHeight(
                                fullYs, 128, i * 128.0 / grid, j * 128.0 / grid);
                }
                else
                {
                    for (int j = 0; j < grid; ++j)
                        for (int i = 0; i < grid; ++i)
                            ys[j * grid + i] =
                                fullYs[(j * 128 / grid) * 128 + i * 128 / grid];
                }
            }
        }
    }

    int minY = 0, maxY = 255;
    dimensionHeightRange(dim, &minY, &maxY);
    const int seaLevel = (cdim == DIM_OVERWORLD) ? 63 : 0;
    // 采样步长（方块）：坡度系数按步长折算，跨分辨率观感一致
    const qreal stepBlocks = 512.0 / grid;
    const qreal kSlope = 0.010 / stepBlocks;
    // 高度限制：截断面之上的预测地表削掉，与真实地表同一套语义
    const bool yLimited = (maxSurfaceY >= 0);
    // 切进水柱时可见水面下沉到截断 Y；未限制或截断在海平面之上则仍是海平面
    const int waterTop =
        yLimited ? std::min(seaLevel, maxSurfaceY) : seaLevel;
    // 预测只有近似地表高度、没有方块柱：截断落入固体内部时按材质兜底
    const QString cutSolid = (cdim == DIM_NETHER) ? QStringLiteral("netherrack")
                           : (cdim == DIM_END)    ? QStringLiteral("end_stone")
                                                  : QStringLiteral("stone");

    // ┌─ 采样栅格上着色（基础色 + 高度明暗 + 坡度阴影 / 地形图）──
    QVector<QRgb> cell(grid * grid, 0);
    for (int r = 0; r < grid; ++r)
    {
        for (int c = 0; c < grid; ++c)
        {
            const int i = r * grid + c;
            QColor base = biomeBaseColor(ids[i]);
            if (!base.isValid())
                continue; // 虚空保持透明

            const float yRaw = ys[i];
            // 邻居高度也按上限截断：截断平台上山体阴影变平，边缘才有小台阶
            const auto clampY = [yLimited, maxSurfaceY](float y) {
                return (yLimited && y > float(maxSurfaceY)) ? float(maxSurfaceY)
                                                            : y;
            };
            const int h = int(std::lround(clampY(yRaw)));

            // 预测地表高于设定 Y：截断落入固体内部（含海床），按材质兜底；
            // 水底低于截断面时走 waterTop，水面同样下沉到截断 Y
            bool solidCut = false;
            if (yLimited && yRaw > float(maxSurfaceY))
            {
                solidCut = true;
                base = WorldMapRenderer::blockColor(cutSolid);
            }
            const bool waterPix = !solidCut && (cdim == DIM_OVERWORLD)
                && isOceanic(ids[i]) && yRaw < float(seaLevel)
                && h < seaLevel;

            // ── 地形图：分层设色 + 山体阴影（与真实地表同一套公式）──
            if (mode == WorldMapRenderer::ShadeMode::Terrain)
            {
                const qreal hl = (c > 0) ? clampY(ys[i - 1]) : yRaw;
                const qreal hrr = (c + 1 < grid) ? clampY(ys[i + 1]) : yRaw;
                const qreal hu = (r > 0) ? clampY(ys[i - grid]) : yRaw;
                const qreal hdn = (r + 1 < grid) ? clampY(ys[i + grid]) : yRaw;
                const qreal hs = WorldMapRenderer::terrainHillshade(
                    (hrr - hl) * 0.5 / stepBlocks, (hdn - hu) * 0.5 / stepBlocks);

                // 水面保持水色不叠阴影；雪冰保持本色；其余按海拔分层设色
                QColor col = base;
                if (waterPix)
                    col = deepenWater(col, waterTop - h);
                else if (!WorldMapRenderer::terrainKeepsBase(col.rgb()))
                    col = QColor::fromRgb(
                        WorldMapRenderer::terrainTint(h, minY, maxY));

                cell[i] = qRgba(
                    waterPix ? col.red()
                             : std::clamp(int(col.red() * hs), 0, 255),
                    waterPix ? col.green()
                             : std::clamp(int(col.green() * hs), 0, 255),
                    waterPix ? col.blue()
                             : std::clamp(int(col.blue() * hs), 0, 255),
                    255);
                continue;
            }

            qint16 hr = qint16(h), hd = qint16(h);
            if (c + 1 < grid) hr = qint16(int(std::lround(clampY(ys[i + 1]))));
            if (r + 1 < grid) hd = qint16(int(std::lround(clampY(ys[i + grid]))));
            const qreal slope = std::clamp(
                1.0 + kSlope * ((qreal(h) - hr) + (qreal(h) - hd)), 0.55, 1.45);

            if (mode == WorldMapRenderer::ShadeMode::Height)
            {
                const qreal t = std::clamp(
                    (qreal(h) - minY) / qreal(std::max(1, maxY - minY)), 0.0, 1.0);
                const int v = std::clamp(int((56 + 180 * t) * slope), 0, 255);
                cell[i] = qRgba(v, v, v, 255);
                continue;
            }

            QColor col = base;
            // 海洋按近似深度调暗，海沟/浅滩层次与真实地图接近
            if (waterPix)
                col = deepenWater(col, waterTop - h);

            const qreal f = heightFactor(h, minY, maxY) * slope;
            cell[i] = qRgba(std::clamp(int(col.red() * f), 0, 255),
                            std::clamp(int(col.green() * f), 0, 255),
                            std::clamp(int(col.blue() * f), 0, 255), 255);
        }
    }

    // └─ 栅格 → 输出尺寸（最近邻；栅格已铺满 region，映射后 region 之间连续）
    QImage img(outSize, outSize, QImage::Format_ARGB32);
    img.fill(qRgba(0, 0, 0, 0));
    for (int y = 0; y < outSize; ++y)
    {
        const int sr = std::min(y * grid / outSize, grid - 1);
        QRgb *dst = reinterpret_cast<QRgb *>(img.scanLine(y));
        const QRgb *src = cell.constData() + sr * grid;
        for (int x = 0; x < outSize; ++x)
            dst[x] = src[std::min(x * grid / outSize, grid - 1)];
    }
    return img;
}

} // namespace SeedTerrainRenderer
