// 种子树冠探针：方块级渲染指定 region 的种子预测地表，扫描雪地群系区域。
// 用途：核对未加载区块里预测出来的树木（形状/密度/配色）是否与原版观感一致。
//
// 用法：
//   seed_tree_probe.exe scan  <seed> [mc]            扫描 region（半径 20）里雪地占比高的瓦片
//   seed_tree_probe.exe render <seed> <rx> <rz> [mc] 渲染该 region 并输出 PNG（512 与 3x 放大）
//   seed_tree_probe.exe crop  <seed> <rx> <rz> <x> <y> <w> [mc]  渲染后裁剪窗口并 6x 放大
#include <QGuiApplication>
#include <QImage>
#include <QColor>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "SeedTerrainRenderer.h"
#include "WorldMapRenderer.h"
#include "biomes.h"
#include "generator.h"
#include "TreeGen.h"
#include "TreePredictor.h"

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

static bool snowBiome(int id)
{
    switch (id) {
    case snowy_tundra: case snowy_mountains: case snowy_taiga:
    case snowy_taiga_hills: case ice_spikes: case snowy_beach:
    case grove: case snowy_slopes: case frozen_peaks: case jagged_peaks:
        return true;
    default:
        return false;
    }
}

static QImage upscale(const QImage &src, int n)
{
    return src.scaled(src.width() * n, src.height() * n,
                      Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    if (argc >= 3 && !std::strcmp(argv[1], "scan")) {
        const quint64 seed = std::strtoull(argv[2], nullptr, 10);
        const int mc = argc >= 4 ? mcFromName(argv[3]) : MC_1_21;
        Generator g;
        setupGenerator(&g, mc, 0);
        applySeed(&g, DIM_OVERWORLD, seed);
        const int radius = 20;
        for (int rz = -radius; rz <= radius; ++rz) {
            for (int rx = -radius; rx <= radius; ++rx) {
                Range r = {16, rx * 32, rz * 32, 32, 32};
                int *cache = allocCache(&g, r);
                if (!cache) continue;
                genBiomes(&g, cache, r);
                int snow = 0, total = 0;
                for (int i = 0; i < 32 * 32; ++i) {
                    ++total;
                    if (snowBiome(cache[i])) ++snow;
                }
                std::free(cache);
                if (snow > total / 5)
                    std::printf("region %d %d snow=%.2f\n", rx, rz, double(snow) / total);
            }
        }
        return 0;
    }

    if (argc >= 5 && (!std::strcmp(argv[1], "render") || !std::strcmp(argv[1], "crop"))) {
        const quint64 seed = std::strtoull(argv[2], nullptr, 10);
        const int rx = std::atoi(argv[3]);
        const int rz = std::atoi(argv[4]);
        int argi = 5;
        int cx = -1, cy = -1, cw = 128;
        if (!std::strcmp(argv[1], "crop") && argc >= 8) {
            cx = std::atoi(argv[5]);
            cy = std::atoi(argv[6]);
            cw = std::atoi(argv[7]);
            argi = 8;
        }
        const int mc = argc > argi ? mcFromName(argv[argi]) : MC_1_21;

        QImage img = SeedTerrainRenderer::renderRegionTile(
            rx, rz, WorldMapRenderer::Dimension::Overworld,
            WorldMapRenderer::ShadeMode::Terrain, seed, mc, 512, -1);
        if (img.isNull()) {
            std::printf("render failed (mc=%d)\n", mc);
            return 1;
        }
        char path[512];
        std::snprintf(path, sizeof(path), "D:/BlockBox/_out/tree_%llu_%d_%d.png",
                      (unsigned long long)seed, rx, rz);
        img.save(QString::fromUtf8(path));
        std::printf("saved %s  %dx%d\n", path, img.width(), img.height());

        if (cx >= 0) {
            QImage sub = img.copy(cx, cy, cw, cw);
            QImage big = upscale(sub, 6);
            std::snprintf(path, sizeof(path), "D:/BlockBox/_out/tree_%llu_%d_%d_crop_%d_%d.png",
                          (unsigned long long)seed, rx, rz, cx, cy);
            big.save(QString::fromUtf8(path));
            std::printf("saved %s  %dx%d\n", path, big.width(), big.height());
        } else {
            QImage big = upscale(img, 2);
            std::snprintf(path, sizeof(path), "D:/BlockBox/_out/tree_%llu_%d_%d_2x.png",
                          (unsigned long long)seed, rx, rz);
            big.save(QString::fromUtf8(path));
            std::printf("saved %s\n", path);
        }
        return 0;
    }

    std::printf("usage: scan|render|crop ...\n");
    return 2;
}
