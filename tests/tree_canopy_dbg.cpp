// 树冠生成诊断：复刻 SeedTerrainRenderer 的方块级取样（scale-1 群系 + 1.18 近似高度），
// 直接调 TreeGen::generate，统计每个“树冠连通块”的尺寸，判断树冠是否退化成一格。
#include "../utils/WorldMap/TreeGen.cpp"
#include "../utils/WorldMap/SeedTerrainRenderer.cpp"

#include <QGuiApplication>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const quint64 seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 12345;
    const int rx = argc > 2 ? std::atoi(argv[2]) : 18;
    const int rz = argc > 3 ? std::atoi(argv[3]) : -8;
    const int mc = MC_1_21;

    Generator *g = SeedTerrainRenderer::generatorFor(mc, seed, DIM_OVERWORLD, nullptr);
    if (!g) { std::printf("no generator\n"); return 1; }

    const int grid = 512;
    const int rx1 = rx * 512, rz1 = rz * 512;
    Range r;
    r.scale = 1;
    r.x = rx1;
    r.z = rz1;
    r.sx = 512;
    r.sz = 512;
    r.y = 64;
    r.sy = 1;
    int *cache = allocCache(g, r);
    if (!cache) { std::printf("no cache\n"); return 1; }
    genBiomes(g, cache, r);

    QVector<int> ids(grid * grid, 0);
    QVector<float> ys(grid * grid, 64.0f);
    for (int j = 0; j < grid; ++j) {
        for (int i = 0; i < grid; ++i) {
            ids[j * grid + i] = cache[j * 512 + i];
            ys[j * grid + i] = SeedTerrainRenderer::sampleHeight118(g, (rx1 + i) / 4.0,
                                                                    (rz1 + j) / 4.0);
        }
    }

    int all[256] = {0};
    for (int v : ids) if (v >= 0 && v < 256) ++all[v];
    std::printf("biome histogram (all): ");
    for (int v = 0; v < 256; ++v) if (all[v] > 200) std::printf("%d:%d ", v, all[v]);
    std::printf("\n");
    int histo[64] = {0};
    for (int v : ids) if (v >= 0 && v < 64) ++histo[v];
    std::printf("biome histogram (top): ");
    for (int v = 0; v < 64; ++v) if (histo[v] > 100) std::printf("%d:%d ", v, histo[v]);
    std::printf("\n");

    QVector<qint16> top(grid * grid, TreeGen::kNoCanopy);
    QVector<quint8> kind(grid * grid, 0);

    {
        // 打印瓦片内出现的群系在 kBiomeFeats 里对应的特征行
        std::printf("kBiomeFeatCount=%d kFeatCount=%d\n", TreeData::kBiomeFeatCount,
                    TreeData::kFeatCount);
        for (int b = 0; b < 64; ++b) {
            if (!histo[b]) continue;
            int rows = 0;
            for (int i = 0; i < TreeData::kBiomeFeatCount; ++i) {
                if (TreeData::kBiomeFeats[i].biome != b) continue;
                ++rows;
            }
            std::printf("biome %d: %d rows\n", b, rows);
        }
        for (int i = 0; i < TreeData::kBiomeFeatCount; ++i) {
            const int b = TreeData::kBiomeFeats[i].biome;
            const int f = TreeData::kBiomeFeats[i].feat;
            if (b < 0) continue;
            std::printf("  row biome=%d featIdx=%d idx=%d countMode=%d cA=%d rarity=%.3f selFirst=%d selCount=%d\n",
                        b, f, TreeData::kFeats[f].idx, TreeData::kFeats[f].countMode,
                        TreeData::kFeats[f].cA, double(TreeData::kFeats[f].rarity),
                        TreeData::kFeats[f].selFirst, TreeData::kFeats[f].selCount);
        }
    }

    TreeGen::generate(top, kind, rx1, rz1, grid, seed, ids, ys);

    // 连通块统计
    std::vector<char> seen(grid * grid, 0);
    std::vector<int> sizes;
    std::vector<int> cellsX, cellsZ;
    int total = 0;
    for (int j = 0; j < grid; ++j) {
        for (int i = 0; i < grid; ++i) {
            const int idx = j * grid + i;
            if (top[idx] == TreeGen::kNoCanopy || seen[idx]) continue;
            // flood fill 4-neighbour
            std::vector<int> stack{idx};
            seen[idx] = 1;
            int n = 0, w = 0, h = 0;
            int minx = 9999, maxx = -1, minz = 9999, maxz = -1;
            while (!stack.empty()) {
                const int c = stack.back(); stack.pop_back();
                ++n;
                const int cx = c % grid, cz = c / grid;
                minx = std::min(minx, cx); maxx = std::max(maxx, cx);
                minz = std::min(minz, cz); maxz = std::max(maxz, cz);
                const int nb[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (auto &d : nb) {
                    const int nx = cx + d[0], nz = cz + d[1];
                    if (nx < 0 || nx >= grid || nz < 0 || nz >= grid) continue;
                    const int ni = nz * grid + nx;
                    if (top[ni] == TreeGen::kNoCanopy || seen[ni]) continue;
                    seen[ni] = 1;
                    stack.push_back(ni);
                }
            }
            sizes.push_back(n);
            total += n;
            w = maxx - minx + 1; h = maxz - minz + 1;
            if (n <= 2) cellsX.push_back(n);
        }
    }
    std::sort(sizes.begin(), sizes.end());
    std::printf("canopy cells=%d  components=%d\n", total, int(sizes.size()));
    if (!sizes.empty()) {
        const int q1 = sizes[sizes.size() / 4];
        const int med = sizes[sizes.size() / 2];
        const int q3 = sizes[sizes.size() * 3 / 4];
        std::printf("component size: min=%d p25=%d median=%d p75=%d max=%d\n",
                    sizes.front(), q1, med, q3, sizes.back());
    }
    int single = 0;
    for (int s : sizes) if (s <= 2) ++single;
    std::printf("components <=2 cells: %d (%.1f%%)\n", single,
                sizes.empty() ? 0.0 : 100.0 * single / sizes.size());
#ifdef BB_TREE_STATS
    std::printf("attempts=%d placed=%d noSpace=%d selectorMiss=%d\n", g_attempts, g_placed,
                g_noSpace, g_selectorMiss);
#endif
    return 0;
}
