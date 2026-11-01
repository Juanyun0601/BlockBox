// 渲染 region 15,8（种子 12345）的方块级地表图，检查蘑菇岛蘑菇伞盖是否画出来
#include <QGuiApplication>
#include <QImage>
#include <QColor>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "SeedTerrainRenderer.h"
#include "biomes.h"
#include "generator.h"
#include "TreePredictor.h"
#include "MushroomPredictor.h"

extern "C" float getSpline(const Spline *sp, const float *vals);

// 复刻 SeedTerrainRenderer 的 1.18 近似高度，比较真实蘑菇点与误报点的近似高度
static float approxH118(const Generator *g, double bx, double bz)
{
    const BiomeNoise *bn = &g->bn;
    const double x4 = bx / 4.0, z4 = bz / 4.0;
    double px = x4, pz = z4;
    px += sampleDoublePerlin(&bn->climate[NP_SHIFT], x4, 0, z4) * 4.0;
    pz += sampleDoublePerlin(&bn->climate[NP_SHIFT], z4, x4, 0) * 4.0;
    const float c = float(sampleDoublePerlin(&bn->climate[NP_CONTINENTALNESS], px, 0, pz));
    const float e = float(sampleDoublePerlin(&bn->climate[NP_EROSION], px, 0, pz));
    const float w = float(sampleDoublePerlin(&bn->climate[NP_WEIRDNESS], px, 0, pz));
    const float npParam[] = { c, e, -3.0F * (std::fabs(std::fabs(w) - 0.6666667F) - 0.33333334F), w };
    const double off = getSpline(bn->sp, npParam) + 0.015F;
    const float d = 1.0F - 83.0F / 160.0F + float(off);
    return float(int64_t(10000.0F * d)) / 76.0F;
}
static int mcFromName(const char *s)
{
    if (!std::strcmp(s, "MC_1_18")) return MC_1_18;
    if (!std::strcmp(s, "MC_1_19")) return MC_1_19;
    if (!std::strcmp(s, "MC_1_20")) return MC_1_20;
    if (!std::strcmp(s, "MC_1_21_1")) return MC_1_21_1;
    if (!std::strcmp(s, "MC_1_21_3")) return MC_1_21_3;
    if (!std::strcmp(s, "MC_1_21")) return MC_1_21;
    return std::atoi(s);
}

static int heightMode(int mc, unsigned long long seed, const char *path)
{
    FILE *fp = std::fopen(path, "r");
    if (!fp) { std::printf("open fail\n"); return 1; }
    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, seed);
    int cx, cz, lx, lz;
    while (std::fscanf(fp, "%d %d %d %d", &cx, &cz, &lx, &lz) == 4)
    {
        const double wx = cx * 16 + lx, wz = cz * 16 + lz;
        std::printf("%d %d %d %.2f\n", cx, cz, lx, approxH118(&g, wx, wz));
    }
    std::fclose(fp);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 4)
        return heightMode(mcFromName(argv[2]), std::strtoull(argv[3], nullptr, 10), argv[1]);
    QGuiApplication app(argc, argv);

    const quint64 seed = 12345;
    const int mc = MC_1_21; // 1.21.4
    const int rx = 7, rz = 4;

    QImage img = SeedTerrainRenderer::renderRegionTile(
        rx, rz, WorldMapRenderer::Dimension::Overworld,
        WorldMapRenderer::ShadeMode::Terrain, seed, mc, 512, -1);

    if (img.isNull())
    {
        std::printf("render failed\n");
        return 1;
    }
    img.save(QStringLiteral("D:/BlockBox/.tmp_chk/mc/mush_render.png"));
    std::printf("rendered %dx%d\n", img.width(), img.height());

    // chunk (250,142) local (4,13) 红 -> 世界 (4004, 2277)；区域原点 (240*16, 128*16)
    const int px = 4004 - rx * 512;
    const int py = 2285 - rz * 512;
    const QColor c = img.pixelColor(px, py);
    std::printf("pixel (%d,%d) = #%02X%02X%02X\n", px, py, c.red(), c.green(), c.blue());

    const QColor redCap(0xC4, 0x46, 0x3C);
    std::printf("red cap? %s\n", c == redCap ? "YES" : "no");

    // chunk (250,143) local (8,3) 棕 -> 世界 (4008, 2275)
    const QColor b = img.pixelColor(4008 - rx * 512, 2291 - rz * 512);
    std::printf("brown pixel = #%02X%02X%02X\n", b.red(), b.green(), b.blue());

    // 直接验证 overlayCaps：全部当作蘑菇岛群系，看是否写入伞盖
    {
        const int grid = 512;
        QVector<qint16> top(grid * grid, TreePredictor::kNoCanopy);
        QVector<quint8> kind(grid * grid, 0);
        QVector<int> ids(grid * grid, mushroom_fields);
        QVector<float> ys(grid * grid, 100.0f);
        MushroomPredictor::overlayCaps(top, kind, rx * 512, rz * 512, grid,
                                       seed, mc, ids, ys);
        int painted = 0;
        for (int i = 0; i < grid * grid; ++i)
            if (top[i] != TreePredictor::kNoCanopy)
                ++painted;
        const int wantIdx = 237 * grid + 420;
        std::printf("overlayCaps painted %d cells, target top=%d kind=%d\n",
                    painted, int(top[wantIdx]), int(kind[wantIdx]));
        const QVector<MushroomPredictor::Mushroom> pm =
            MushroomPredictor::predictChunk(seed, mc, 250, 142);
        if (!pm.isEmpty())
            std::printf("predict(250,142) = (%d,%d,k%d,h%d)\n", pm[0].localX,
                        pm[0].localZ, int(pm[0].kind), pm[0].height);
        else
            std::printf("predict(250,142) empty (supported=%d)\n",
                        int(MushroomPredictor::supported(mc)));

        // 用 cubiomes 采样真实群系，确认该点是否 mushroom_fields
        Generator g;
        setupGenerator(&g, mc, 0);
        applySeed(&g, DIM_OVERWORLD, seed);
        Range rr;
        rr.scale = 1;
        rr.x = 4004;
        rr.z = 2285;
        rr.sx = 1;
        rr.sz = 1;
        rr.y = 64;
        rr.sy = 1;
        int *cache = allocCache(&g, rr);
        if (cache && genBiomes(&g, cache, rr) == 0)
            std::printf("cubiomes biome at (4004,2285) = %d (mushroom_fields=%d)\n",
                        cache[0], int(mushroom_fields));
        free(cache);

        const int pts[][2] = {
            { 4004, 2285 }, { 3999, 2291 }, { 4008, 2291 }, { 4009, 2311 },
            { 4022, 2301 }, { 4029, 2319 },
        };
        const char *tags[] = { "true1", "true2", "true3", "true4", "FP1", "FP2" };
        for (int i = 0; i < 6; ++i)
            std::printf("%s (%d,%d) approxH=%.2f\n", tags[i], pts[i][0], pts[i][1],
                        approxH118(&g, pts[i][0], pts[i][1]));
    }
    return 0;
}













