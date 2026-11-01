// 树木生成引擎内部诊断（直接 include TreeGen.cpp 以访问匿名命名空间）
// 目的：核对单棵树到了“画树冠”这一步时，树叶到底铺开几格、高几格。
#include "../utils/WorldMap/TreeGen.cpp"

#include <QGuiApplication>
#include <QImage>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace TreeData;

static Rng makeRng(quint64 s)
{
    Rng r;
    r.setSeed(s);
    return r;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    const int cartKind = argc > 1 ? std::atoi(argv[1]) : 2; // 2 = Spruce
    std::printf("tree defs: %d\n", kTreeCount);
    for (int t = 0; t < kTreeCount; ++t)
    {
        const TreeDef &def = kTrees[t];
        Ctx w;
        w.ox = 0;
        w.oz = 0;
        w.grid = 512;
        QVector<int> biome(512 * 512, 30); // snowy_taiga
        QVector<float> ys(512 * 512, 70.0f);
        w.biome = &biome;
        QVector<qint16> top(512 * 512, -32000);
        QVector<quint8> kind(512 * 512, 0);
        w.top = &top;
        w.kind = &kind;
        w.heights.resize(512 * 512);
        for (int i = 0; i < 512 * 512; ++i)
            w.heights[i] = 71;

        Rng r = makeRng(12345 + t);
        const int h = trunkHeight(def, r);
        const int fh = foliageHeight(def, r, h);
        const int rad = foliageRadius(def, r, h, h - fh);
        std::printf("[%2d] %-22s log=%-28s kind=%d trunk=%d fol=%d h=%d fh=%d rad=%d tp={%d,%d,%d}\n",
                    t, "", def.log, def.kind, def.trunk, def.fol, h, fh, rad, def.tp[0], def.tp[1],
                    def.tp[2]);

        Rng r2 = makeRng(12345 + t);
        const bool ok = placeTree(w, def, r2, 256, 71, 256);
        int x0 = 9999, x1 = -9999, y0 = 9999, y1 = -9999, z0 = 9999, z1 = -9999;
        for (const P3 &p : w.leaves) {
            x0 = qMin(x0, p.x); x1 = qMax(x1, p.x);
            z0 = qMin(z0, p.z); z1 = qMax(z1, p.z);
            y0 = qMin(y0, p.y); y1 = qMax(y1, p.y);
        }
        int lx0 = 9999, lx1 = -9999;
        for (const P3 &p : w.logs) {
            lx0 = qMin(lx0, p.x); lx1 = qMax(lx1, p.x);
        }
        std::printf("     placed=%d logs=%d leaves=%d  leafX=[%d,%d] leafZ=[%d,%d] leafY=[%d,%d]  logX=[%d,%d]\n",
                    int(ok), int(w.logs.size()), int(w.leaves.size()), x0, x1, z0, z1, y0, y1, lx0, lx1);
        qint16 topv = -32000;
        int cells = 0;
        for (int i = 0; i < top.size(); ++i) {
            if (top[i] != -32000) { ++cells; topv = qMax(topv, top[i]); }
        }
        std::printf("     canopyCells=%d maxTopY=%d\n", cells, int(topv));
    }
    Q_UNUSED(cartKind);
    return 0;
}
