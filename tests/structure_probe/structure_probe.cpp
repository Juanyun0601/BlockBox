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

    // 要塞走 StrongholdIter 环形迭代（不走区域网格）：中心附近必有一个
    const int stronghold = indexOfKey("stronghold");
    check(stronghold >= 0 && counts[stronghold] > 0,
          "stronghold found via stronghold iterator");

    // 古迹废墟等本轮补全的结构类型不应被版本配置漏掉
    const int trailRuins = indexOfKey("trail_ruins");
    check(trailRuins >= 0 && counts[trailRuins] > 0,
          "trail_ruins found (new type wired)");

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

    // ── 末地维度：末地折跃门（regionSize=1 每 16 方块一格 + rarity 1/700，
    // 依赖放宽后的扫描预算；biome 只认 end_highlands，即外岛区域） ──
    {
        const int gateway = indexOfKey("end_gateway");
        check(gateway >= 0, "end_gateway type registered");
        QVector<bool> endEnabled(defs.size(), false);
        if (gateway >= 0)
            endEnabled[gateway] = true;
        QElapsedTimer et;
        et.start();
        const auto endHits = StructureFinder::find(
            quint64(info->seed), mc, 1, -16000, -16000, 16000, 16000,
            endEnabled);
        const qint64 endMs = et.elapsed();
        std::printf("end_gateway hits=%d in %lld ms\n", int(endHits.size()),
                    static_cast<long long>(endMs));
        check(!endHits.isEmpty(), "end gateways found in The End");
        check(endMs < 5000, "end gateway query under 5s");
        // 全部落在查询范围内
        bool endInRange = true;
        for (const auto &h : endHits)
            if (h.x < -16000 || h.x > 16000 || h.z < -16000 || h.z > 16000)
                endInRange = false;
        check(endInRange, "end gateway hits inside range");
        // 折跃门只在末地：主世界维度查询必须为 0
        const auto overworldGw = StructureFinder::find(
            quint64(info->seed), mc, 0, -16000, -16000, 16000, 16000,
            endEnabled);
        check(overworldGw.isEmpty(), "end gateways filtered out in overworld");
    }

    std::printf("failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
