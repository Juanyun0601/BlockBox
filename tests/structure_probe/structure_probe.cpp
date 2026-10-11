/**
 * @file   structure_probe.cpp
 * @brief  按种子算结构位置：读真实存档种子/版本 → cubiomes 查询 → 输出坐标
 * @author BlockBox Team
 *
 * 用法：structure_probe.exe <存档目录> [查询半径，默认4096 方块]
 *
 * 断言：版本映射有效、主世界 ±4096 内能查到村庄、下界结构（下界要塞）
 * 在主世界维度查询结果为 0（维度过滤生效）、查询耗时在 2s 内。
 */

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QVector>

#include <cstdio>
#include <cstring>

#include "utils/Saves/LevelDatEditor.h"
#include "utils/WorldMap/StructureFinder.h"

namespace {

int g_failures = 0;

void check(bool cond, const char *what)
{
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond)
        ++g_failures;
}

int indexOfKey(const char *key)
{
    const auto &defs = StructureFinder::allStructures();
    for (int i = 0; i < defs.size(); ++i)
        if (std::strcmp(defs[i].key, key) == 0)
            return i;
    return -1;
}

} // namespace

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);

    if (argc < 2)
    {
        std::fprintf(stderr, "usage: structure_probe <saveDir> [radiusBlocks]\n");
        return 2;
    }
    const QString savePath = QString::fromLocal8Bit(argv[1]);
    const int radius = (argc >= 3) ? std::atoi(argv[2]) : 4096;

    const auto info = LevelDatEditor::readSave(savePath);
    check(info.has_value(), "read level.dat");
    if (!info.has_value())
        return 1;
    check(info->hasSeed, "level.dat has seed");

    const int mc = StructureFinder::versionToInt(info->versionName);
    std::printf("seed=%lld version=%s mc=%d radius=%d\n",
                static_cast<long long>(info->seed),
                info->versionName.toUtf8().constData(), mc, radius);
    check(mc != 0, "version maps to cubiomes MCVersion");

    const auto &defs = StructureFinder::allStructures();
    QVector<bool> enabled(defs.size(), true);

    // 主世界维度（cubiomes dim = 0）
    QElapsedTimer timer;
    timer.start();
    const auto hits = StructureFinder::find(
        quint64(info->seed), mc, 0,
        -qint64(radius), -qint64(radius), qint64(radius), qint64(radius), enabled);
    const qint64 ms = timer.elapsed();
    std::printf("hits=%d in %lld ms\n", int(hits.size()), static_cast<long long>(ms));

    QVector<int> counts(defs.size(), 0);
    for (const auto &h : hits)
        if (h.structIdx >= 0 && h.structIdx < counts.size())
            ++counts[h.structIdx];

    for (int i = 0; i < defs.size(); ++i)
    {
        if (counts[i] == 0)
            continue;
        std::printf("  %-18s %-8s hits=%d\n", defs[i].key, defs[i].displayName,
                    counts[i]);
    }
    if (!hits.isEmpty())
    {
        std::printf("  sample:");
        for (int i = 0; i < hits.size() && i < 6; ++i)
            std::printf(" (%d,%d)", hits[i].x, hits[i].z);
        std::printf("\n");
    }

    // 主世界 ±4096 内应该能算出村庄
    const int village = indexOfKey("village");
    check(village >= 0 && counts[village] > 0, "village found in overworld radius");

    // 下界要塞不该出现在主世界维度（维度过滤生效）
    const int fortress = indexOfKey("fortress");
    check(fortress >= 0 && counts[fortress] == 0,
          "nether-only structures filtered out in overworld");

    check(ms < 2000, "structure query under 2s");

    // 所有命中都必须落在查询范围内
    bool inRange = true;
    for (const auto &h : hits)
        if (h.x < -radius || h.x > radius || h.z < -radius || h.z > radius)
            inRange = false;
    check(inRange, "all hits inside query range");

    std::printf("failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
