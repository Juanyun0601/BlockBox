/**
 * @file   seed_green_probe.cpp
 * @brief  种子预测配色探针：按群系统计未加载区块预测图的颜色分布
 *
 * 目的：确认"未加载区块的群系绿"与"已加载区块的草方块绿"一致 ——
 * 即树林类群系的底色应是草方块绿（草 #79A54D），树叶只由树冠叠加。
 */

#include <QCoreApplication>
#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cstdio>

#include "utils/WorldMap/BiomeCatalog.h"
#include "utils/WorldMap/SeedTerrainRenderer.h"
#include "utils/WorldMap/WorldMapRenderer.h"

// MC_1_21：包进命名空间，避免 biomes.h 的 Dimension 枚举污染 WorldMapRenderer
namespace cubiomes_mc {
#include "biomes.h"
}

namespace
{
QString biomeName(int id)
{
    const QVector<BiomeCatalog::BiomeDef> &all = BiomeCatalog::allBiomes();
    for (const BiomeCatalog::BiomeDef &b : all)
        if (b.id == id)
            return QString::fromUtf8(b.displayName);
    return QStringLiteral("id=%1").arg(id);
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const quint64 seed = 4562377579810721523ULL;
    const int mc = cubiomes_mc::MC_1_21;
    const int grid = 512;

    const QColor grass = WorldMapRenderer::blockColor(QStringLiteral("grass_block"));
    std::printf("grass_block base = (%d,%d,%d)\n", grass.red(), grass.green(), grass.blue());

    // 目视核对用：方块级（512）与概览级（64 = 每像素 8 格）各存一张
    SeedTerrainRenderer::renderRegionTile(
        0, 0, WorldMapRenderer::Dimension::Overworld,
        WorldMapRenderer::ShadeMode::BlockColor, seed, mc, 512)
        .save(QStringLiteral("D:/BlockBox/_out/seed_green_512.png"));
    SeedTerrainRenderer::renderRegionTile(
        0, 0, WorldMapRenderer::Dimension::Overworld,
        WorldMapRenderer::ShadeMode::BlockColor, seed, mc, 64)
        .save(QStringLiteral("D:/BlockBox/_out/seed_green_64.png"));

    for (int rz = -1; rz <= 1; ++rz)
    {
        for (int rx = -1; rx <= 1; ++rx)
        {
            const QImage img = SeedTerrainRenderer::renderRegionTile(
                rx, rz, WorldMapRenderer::Dimension::Overworld,
                WorldMapRenderer::ShadeMode::BlockColor, seed, mc, grid);
            if (img.isNull())
            {
                std::printf("region(%d,%d): null\n", rx, rz);
                continue;
            }

            const BiomeCatalog::SampleGrid sg = BiomeCatalog::sampleGrid(
                seed, mc, 0, qint64(rx) * 512, qint64(rz) * 512,
                qint64(rx) * 512 + 511, qint64(rz) * 512 + 511, 8);
            if (sg.nx <= 0 || sg.nz <= 0)
            {
                std::printf("region(%d,%d): sampleGrid failed\n", rx, rz);
                continue;
            }

            QHash<int, QHash<QRgb, int>> hist;
            for (int j = 0; j < sg.nz; ++j)
            {
                for (int i = 0; i < sg.nx; ++i)
                {
                    const int id = sg.ids.at(i + j * sg.nx);
                    if (id < 0)
                        continue;
                    const qint64 x = sg.minX + qint64(i) * sg.step;
                    const qint64 z = sg.minZ + qint64(j) * sg.step;
                    const int px = int(x - qint64(rx) * 512);
                    const int py = int(z - qint64(rz) * 512);
                    if (px < 0 || py < 0 || px >= grid || py >= grid)
                        continue;
                    const QRgb c = img.pixel(px, py);
                    if (qAlpha(c) != 255)
                        continue;
                    hist[id][c] += 1;
                }
            }

            std::printf("== region(%d,%d)\n", rx, rz);
            for (auto it = hist.cbegin(); it != hist.cend(); ++it)
            {
                const int id = it.key();
                QVector<QPair<int, QRgb>> top;
                for (auto c = it.value().cbegin(); c != it.value().cend(); ++c)
                    top.append(qMakePair(c.value(), c.key()));
                std::sort(top.begin(), top.end(),
                          [](const QPair<int, QRgb> &a, const QPair<int, QRgb> &b)
                          { return a.first > b.first; });
                std::printf("  %-16s", qUtf8Printable(biomeName(id)));
                const int shown = std::min<int>(3, int(top.size()));
                for (int k = 0; k < shown; ++k)
                {
                    const QRgb c = top.at(k).second;
                    std::printf(" (%3d,%3d,%3d)x%-4d", qRed(c), qGreen(c),
                                qBlue(c), top.at(k).first);
                }
                std::printf("\n");
            }
        }
    }

    return 0;
}
