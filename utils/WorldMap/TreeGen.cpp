#include "TreeGen.h"

#include "TreeData.h"
#include "biomes.h" // enum MCVersion

#include <QHash>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <cmath>

namespace
{
using namespace TreeData;

// ------------------------------------------------------------------ 随机数
struct Rng
{
    quint64 lo = 0;
    quint64 hi = 0;

    static inline quint64 rotl(quint64 x, int k) { return (x << k) | (x >> (64 - k)); }

    static inline quint64 mix(quint64 z)
    {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    void setSeed(quint64 s)
    {
        const quint64 l = s ^ 0x6A09E667F3BCC909ULL;
        const quint64 h = l + 0x9E3779B97F4A7C15ULL;
        lo = mix(l);
        hi = mix(h);
    }

    inline quint64 next()
    {
        const quint64 s0 = lo;
        const quint64 s1 = hi;
        const quint64 res = rotl(s0 + s1, 17) + s0;
        const quint64 t = s1 ^ s0;
        lo = rotl(s0, 49) ^ t ^ (t << 21);
        hi = rotl(t, 28);
        return res;
    }

    inline int bits(int n) { return int(quint64(next()) >> (64 - n)); }

    int nextIntBounded(int bound)
    {
        if (bound <= 0)
            return 0;
        if ((bound & -bound) == bound)
            return int((qint64(bound) * qint64(bits(31))) >> 31);
        int i = bits(31);
        int v = i % bound;
        while (i - v + (bound - 1) < 0)
        {
            i = bits(31);
            v = i % bound;
        }
        return v;
    }

    qint64 nextLong()
    {
        const qint64 hiBits = qint64(bits(32));
        const qint64 loBits = qint64(bits(32));
        return (hiBits << 32) + loBits;
    }

    float nextFloat() { return float(bits(24)) * 5.9604645e-8f; }
    bool nextBool() { return bits(1) != 0; }
    int nextInt(int bound) { return nextIntBounded(bound); }
};

quint64 decorationSeed(quint64 seed, int x, int z)
{
    Rng r;
    r.setSeed(seed);
    const qint64 i = r.nextLong() | 1;
    const qint64 j = r.nextLong() | 1;
    const quint64 k = quint64(qint64(x) * i + qint64(z) * j) ^ seed;
    return k;
}

inline int sampleRange(qint16 a, qint16 b, Rng &r)
{
    if (b <= a)
        return a;
    return a + r.nextIntBounded(b - a + 1);
}

// ------------------------------------------------------------------ 局部坐标
struct P3
{
    int x, y, z;
};

struct Attach
{
    int x, y, z;
    int roff;
    bool dbl;
};

const int kPad = 24;   // 局部占位表水平半径
const int kYLow = -24; // 局部占位表垂直范围
const int kYHigh = 64;

struct Ctx
{
    int ox = 0, oz = 0, grid = 0;
    const QVector<int> *biome = nullptr;
    QVector<float> ys;
    QVector<int> heights; // OCEAN_FLOOR（地面顶 + 1）
    QVector<qint16> *top = nullptr;
    QVector<quint8> *kind = nullptr;

    // 当前树的局部占位：0 空气 / 1 原木 / 2 树叶
    QHash<qint64, quint8> local;
    int lx = 0, ly = 0, lz = 0;
    quint8 curKind = 0;
    QVector<P3> logs;
    QVector<P3> leaves;

    inline int heightAt(int x, int z) const
    {
        int c = x - ox;
        int r = z - oz;
        if (c < 0)
            c = 0;
        else if (c >= grid)
            c = grid - 1;
        if (r < 0)
            r = 0;
        else if (r >= grid)
            r = grid - 1;
        return heights[r * grid + c];
    }

    inline void setHeight(int x, int z, int h)
    {
        const int c = x - ox;
        const int r = z - oz;
        if (c < 0 || c >= grid || r < 0 || r >= grid)
            return;
        int &cur = heights[r * grid + c];
        if (h > cur)
            cur = h + 0; // 已是「顶 + 1」语义
    }

    int biomeAt(int x, int z) const
    {
        int c = x - ox;
        int r = z - oz;
        if (c < 0)
            c = 0;
        else if (c >= grid)
            c = grid - 1;
        if (r < 0)
            r = 0;
        else if (r >= grid)
            r = grid - 1;
        return (*biome)[r * grid + c];
    }

    inline qint64 key(int x, int y, int z) const
    {
        const qint64 ax = x - lx + kPad;
        const qint64 ay = y - ly - kYLow;
        const qint64 az = z - lz + kPad;
        return ax | (az << 8) | (ay << 16);
    }

    inline bool inLocal(int x, int y, int z) const
    {
        const int ax = x - lx;
        const int az = z - lz;
        const int ay = y - ly;
        return ax >= -kPad && ax <= kPad && az >= -kPad && az <= kPad && ay >= kYLow
               && ay <= kYHigh;
    }

    inline quint8 get(int x, int y, int z) const
    {
        if (inLocal(x, y, z))
        {
            const auto it = local.constFind(key(x, y, z));
            if (it != local.constEnd())
                return it.value();
        }
        return (y >= heightAt(x, z)) ? quint8(0) : quint8(3);
    }

    inline bool isAir(int x, int y, int z) const { return get(x, y, z) == 0; }
    inline bool isLog(int x, int y, int z) const { return get(x, y, z) == 1; }
    inline bool isLeaf(int x, int y, int z) const { return get(x, y, z) == 2; }

    void put(int x, int y, int z, quint8 val)
    {
        if (!inLocal(x, y, z))
            return;
        local.insert(key(x, y, z), val);
        // 注意：不把树冠身高写回 heights —— 原版 OCEAN_FLOOR 高度图不把树叶算作
        // 实体地形。若这里抬高，后放的树会把前一棵的树冠当成“地面”而被判没空间，
        // 针叶林就会散成一棵一棵的孤树（原版是一棵挨一棵、树冠互相穿插）。
        if (val == 1)
            logs.append(P3{x, y, z});
        else if (val == 2)
            leaves.append(P3{x, y, z});
    }

    void writeCanopy(int x, int y, int z)
    {
        const int c = x - ox;
        const int r = z - oz;
        if (c < 0 || c >= grid || r < 0 || r >= grid)
            return;
        const int idx = r * grid + c;
        qint16 &t = (*top)[idx];
        if (y > t)
        {
            t = qint16(y);
            (*kind)[idx] = curKind;
        }
    }
};

// ------------------------------------------------------------------ 空间
// 原版 Feature.isFree（compute_tree_height）：空气 / 原木 / 树叶都算“空”。
// 树叶必须算空，否则相邻的树会因为别人的树冠被判为没空间而集体被拒
// （原版针叶林是一棵挨一棵的，树冠互相穿插）。
bool isFreeHeight(const Ctx &w, int x, int y, int z)
{
    const quint8 v = w.get(x, y, z);
    return v == 0 || v == 1 || v == 2;
}

// 原版放置方块前的占位判定（TrunkPlacer.isFree）：空气或可替换（这里把树叶也当可替换）
bool isFreePlace(const Ctx &w, int x, int y, int z)
{
    const quint8 v = w.get(x, y, z);
    return v == 0 || v == 2;
}

// ------------------------------------------------------------------ 尺寸
int sizeAt(const TreeDef &def, int h, int y)
{
    if (def.sizeType == 1)
    {
        if (y < def.sA)
            return def.sC;
        if (y >= h - def.sB)
            return def.sE;
        return def.sD;
    }
    return (y < def.sA) ? def.sB : def.sC;
}

int computeTreeHeight(const Ctx &w, const TreeDef &def, int h, int x, int y, int z)
{
    const int reach = def.ignoreVines ? 1 : 1;
    Q_UNUSED(reach);
    for (int dy = 0; dy <= h + 1; ++dy)
    {
        const int r = sizeAt(def, h, dy);
        for (int dx = -r; dx <= r; ++dx)
        {
            for (int dz = -r; dz <= r; ++dz)
            {
                const quint8 v = w.get(x + dx, y + dy, z + dz);
                if (v == 0 || v == 1)
                    continue;
                return dy - 2;
            }
        }
    }
    return h;
}

// ------------------------------------------------------------------ 树干
int trunkHeight(const TreeDef &def, Rng &r)
{
    return def.tp[0] + r.nextIntBounded(def.tp[1] + 1) + r.nextIntBounded(def.tp[2] + 1);
}

bool placeLog(Ctx &w, const TreeDef &def, int x, int y, int z)
{
    Q_UNUSED(def);
    if (!isFreePlace(w, x, y, z))
        return false;
    w.put(x, y, z, 1);
    w.writeCanopy(x, y, z);
    return true;
}

void trunkStraight(Ctx &w, const TreeDef &def, Rng &r, int x, int y, int z, int h,
                   QVector<Attach> &out)
{
    Q_UNUSED(r);
    for (int i = 0; i < h; ++i)
        placeLog(w, def, x, y + i, z);
    out.append(Attach{x, y + h, z, 0, false});
}

void trunkDarkOak(Ctx &w, const TreeDef &def, Rng &r, int x, int y, int z, int h,
                  QVector<Attach> &out)
{
    static const int DX[4] = {0, -1, 0, 1};
    static const int DZ[4] = {-1, 0, 1, 0};
    const int di = r.nextIntBounded(4);
    const int dx = DX[di];
    const int dz = DZ[di];
    const int topOff = h - r.nextIntBounded(4);
    int branch = 2 - r.nextIntBounded(3);
    int bx = x;
    int bz = z;
    for (int i = 0; i < h; ++i)
    {
        if (i >= topOff && branch > 0)
        {
            bx += dx;
            bz += dz;
            --branch;
        }
        const quint8 v = w.get(bx, y + i, bz);
        if (!(v == 0 || v == 2))
            continue;
        for (int k = 0; k < 4; ++k)
        {
            const int ox2 = (k == 1 || k == 3) ? 1 : 0;
            const int oz2 = (k >= 2) ? 1 : 0;
            placeLog(w, def, bx + ox2, y + i, bz + oz2);
        }
    }
    const int topY = y + h - 1;
    out.append(Attach{bx, topY, bz, 0, true});
    for (int ox2 = -1; ox2 <= 2; ++ox2)
    {
        for (int oz2 = -1; oz2 <= 2; ++oz2)
        {
            if (ox2 >= 0 && ox2 <= 1 && oz2 >= 0 && oz2 <= 1)
                continue;
            if (r.nextIntBounded(3) > 0)
                continue;
            const int n = r.nextIntBounded(3) + 2;
            for (int k = 0; k < n; ++k)
                placeLog(w, def, x + ox2, topY - k - 1, z + oz2);
            out.append(Attach{x + ox2, topY, z + oz2, 0, false});
        }
    }
}

void trunkGiant(Ctx &w, const TreeDef &def, Rng &r, int x, int y, int z, int h,
                QVector<Attach> &out)
{
    Q_UNUSED(r);
    for (int i = 0; i < h; ++i)
    {
        for (int k = 0; k < 4; ++k)
        {
            const int ox2 = (k == 1 || k == 3) ? 1 : 0;
            const int oz2 = (k >= 2) ? 1 : 0;
            if ((ox2 || oz2) && i >= h - 1)
                continue;
            if (isFreePlace(w, x + ox2, y + i, z + oz2))
            {
                w.put(x + ox2, y + i, z + oz2, 1);
                w.writeCanopy(x + ox2, y + i, z + oz2);
            }
        }
    }
    out.append(Attach{x, y + h, z, 0, true});
}

void trunkMegaJungle(Ctx &w, const TreeDef &def, Rng &r, int x, int y, int z, int h,
                     QVector<Attach> &out)
{
    trunkGiant(w, def, r, x, y, z, h, out);
    int i = h - 2 - r.nextIntBounded(4);
    while (i > h / 2)
    {
        const double ang = double(r.nextFloat()) * 2.0 * 3.14159265358979;
        for (int v = 0; v < 5; ++v)
        {
            const int bx = int(1.5 + std::sin(ang) * v);
            const int bz = int(1.5 + std::cos(ang) * v);
            placeLog(w, def, x + bx, y + i - 3 + v / 2, z + bz);
        }
        out.append(Attach{x + int(1.5 + std::sin(ang) * 4), y + i,
                          z + int(1.5 + std::cos(ang) * 4), -2, false});
        i -= 2 + r.nextIntBounded(4);
    }
}

void trunkForking(Ctx &w, const TreeDef &def, Rng &r, int x, int y, int z, int h,
                  QVector<Attach> &out)
{
    static const int DX[4] = {0, -1, 0, 1};
    static const int DZ[4] = {-1, 0, 1, 0};
    const int di = r.nextIntBounded(4);
    const int dx = DX[di];
    const int dz = DZ[di];
    const int topOff = h - r.nextIntBounded(4) - 1;
    int branch = 3 - r.nextIntBounded(3);
    int bx = x;
    int bz = z;
    int best = y;
    for (int i = 0; i < h; ++i)
    {
        if (i >= topOff && branch > 0)
        {
            bx += dx;
            bz += dz;
            --branch;
        }
        if (placeLog(w, def, bx, y + i, bz))
            best = y + i + 1;
    }
    out.append(Attach{bx, best, bz, 1, false});
    const int di2 = r.nextIntBounded(4);
    if (di2 != di)
    {
        const int dx2 = DX[di2];
        const int dz2 = DZ[di2];
        const int topOff2 = topOff - r.nextIntBounded(2) - 1;
        int branch2 = 1 + r.nextIntBounded(3);
        int bx2 = x;
        int bz2 = z;
        int best2 = y;
        for (int i = qMax(topOff2, 1); i < h; ++i)
        {
            if (branch2 <= 0)
                break;
            bx2 += dx2;
            bz2 += dz2;
            --branch2;
            if (placeLog(w, def, bx2, y + i, bz2))
                best2 = y + i + 1;
        }
        out.append(Attach{bx2, best2, bz2, 0, false});
    }
}

double fancyShape(int hh, int level)
{
    const double d = hh / 2.0;
    if (level < hh * 0.3)
        return -1.0;
    const double dy = d - level;
    if (dy * dy > d * d)
        return 0.0;
    return std::sqrt(d * d - dy * dy) * 0.5;
}

bool fancyLimb(Ctx &w, const TreeDef &def, const P3 &start, const P3 &end, bool place)
{
    const int dx = end.x - start.x;
    const int dy = end.y - start.y;
    const int dz = end.z - start.z;
    const int steps = qMax(qMax(qAbs(dx), qAbs(dy)), qAbs(dz));
    if (steps == 0)
        return place ? placeLog(w, def, end.x, end.y, end.z) : true;
    for (int i = 0; i <= steps; ++i)
    {
        const double s = double(i) / double(steps);
        const int px = start.x + int(std::floor(dx * s + 0.5));
        const int py = start.y + int(std::floor(dy * s + 0.5));
        const int pz = start.z + int(std::floor(dz * s + 0.5));
        if (place)
            placeLog(w, def, px, py, pz);
        else if (!(w.get(px, py, pz) == 0 || w.get(px, py, pz) == 1))
            return false;
    }
    return true;
}

void trunkFancy(Ctx &w, const TreeDef &def, Rng &r, int x, int y, int z, int h,
                QVector<Attach> &out)
{
    const int hh = h + 2;
    const int top = int(hh * 0.618);
    QVector<P3> branches;
    branches.append(P3{x, y, z});
    const int count = qMin(1, int(std::floor(1.382 + std::pow(hh / 13.0, 2.0))));
    for (int level = hh - 5; level >= 0; --level)
    {
        const double f = fancyShape(hh, level);
        if (f < 0)
            continue;
        for (int c = 0; c < count; ++c)
        {
            const double sc = f * (double(r.nextFloat()) + 0.328);
            const double ang = double(r.nextFloat()) * 2.0 * 3.14159265358979;
            const int bx = x + int(std::floor(sc * std::sin(ang) + 0.5));
            const int bz = z + int(std::floor(sc * std::cos(ang) + 0.5));
            const int by = y + level - 1;
            if (fancyLimb(w, def, P3{x, by + 5, z}, P3{bx, by, bz}, false))
            {
                int ty = by - int(std::sqrt(double((bx - x) * (bx - x)
                                                   + (bz - z) * (bz - z)))
                                   * 0.381);
                ty = qMin(top, ty);
                if (fancyLimb(w, def, P3{x, ty, z}, P3{bx, by, bz}, false))
                    branches.append(P3{bx, by, bz});
            }
        }
    }
    fancyLimb(w, def, P3{x, y + top, z}, P3{x, y + top, z}, true);
    for (int i = 0; i < branches.size(); ++i)
    {
        const P3 &b = branches[i];
        fancyLimb(w, def, P3{x, y, z}, b, true);
        out.append(Attach{b.x, b.y, b.z, 0, (b.y - y) >= hh * 0.2});
    }
}

// ------------------------------------------------------------------ 树冠
int foliageHeight(const TreeDef &def, Rng &r, int h)
{
    Q_UNUSED(h);
    switch (def.fol)
    {
    case 0: // blob
    case 1: // fancy
        return sampleRange(def.fp[4], def.fp[5], r);
    case 2: // dark oak
        return 4;
    case 3: // acacia
        return 0;
    case 4: // pine
        return sampleRange(def.fp[4], def.fp[5], r);
    case 5: // spruce
    {
        const int th = sampleRange(def.fp[6], def.fp[7], r);
        return qMax(4, h - th);
    }
    case 6: // mega pine：crown_height 提供器
        return sampleRange(def.fp[8], def.fp[9], r);
    case 7: // mega jungle
        return sampleRange(def.fp[4], def.fp[5], r);
    case 9: // bush
        return sampleRange(def.fp[4], def.fp[5], r);
    default:
        return 0;
    }
}

int foliageRadius(const TreeDef &def, Rng &r, int h, int fh)
{
    int base = sampleRange(def.fp[0], def.fp[1], r);
    if (def.fol == 4)
        base += r.nextIntBounded(qMax(h + 1, 1));
    Q_UNUSED(fh);
    return base;
}

bool foliageSkip(int fol, Rng &r, int ax, int dy, int az, int rad, bool dbl)
{
    switch (fol)
    {
    case 0: // blob
        return ax == rad && az == rad && (r.nextIntBounded(2) == 0 || dy == 0);
    case 1: // fancy
        return int(std::floor(ax + 0.5)) * int(std::floor(ax + 0.5))
                   + int(std::floor(az + 0.5)) * int(std::floor(az + 0.5))
               > rad * rad;
    case 2: // dark oak
        if (dy == 0 && dbl && ax >= rad && az >= rad && rad > 0)
            return true;
        if (dy == -1 && !dbl)
            return ax == rad && az == rad;
        if (dy == 1)
            return ax + az > rad * 2 - 2;
        return false;
    case 3: // acacia
        if (dy == 0)
            return (ax > 1 || az > 1) && ax != 0 && az != 0;
        return ax == rad && az == rad && rad > 0;
    case 4: // pine
    case 5: // spruce
        return ax == rad && az == rad && rad > 0;
    case 6: // mega pine
    case 7: // mega jungle
        if (ax + az >= 7)
            return true;
        return ax * ax + az * az > rad * rad;
    case 9: // bush
        return ax == rad && az == rad && r.nextIntBounded(2) == 0;
    default:
        return false;
    }
}

void foliageRow(Ctx &w, const TreeDef &def, Rng &r, int x, int y, int z, int rad, int dy,
                bool dbl)
{
    const int extra = dbl ? 1 : 0;
    for (int dx = -rad; dx <= rad + extra; ++dx)
    {
        for (int dz = -rad; dz <= rad + extra; ++dz)
        {
            int ax = qAbs(dx);
            int az = qAbs(dz);
            if (dbl)
            {
                ax = qMin(qAbs(dx), qAbs(dx - 1));
                az = qMin(qAbs(dz), qAbs(dz - 1));
            }
            if (foliageSkip(def.fol, r, ax, dy, az, rad, dbl))
                continue;
            const int px = x + dx;
            const int py = y + dy;
            const int pz = z + dz;
            const quint8 v = w.get(px, py, pz);
            if (v == 0 || v == 2) // 空气或已有树叶：原版允许树叶互相覆盖
            {
                w.put(px, py, pz, 2);
                w.writeCanopy(px, py, pz);
            }
        }
    }
}

void foliageCreate(Ctx &w, const TreeDef &def, Rng &r, const Attach &att, int h, int fh,
                   int rad)
{
    const int offset = sampleRange(def.fp[2], def.fp[3], r);
    Q_UNUSED(h);
    switch (def.fol)
    {
    case 0: // blob
        for (int dy = offset; dy >= offset - fh; --dy)
        {
            const int rr = qMax(rad + att.roff - 1 - int(dy / 2), 0);
            foliageRow(w, def, r, att.x, att.y, att.z, rr, dy, att.dbl);
        }
        break;
    case 1: // fancy
        for (int dy = offset; dy >= offset - fh; --dy)
        {
            const int rr = rad + ((dy == offset || dy == offset - fh) ? 0 : 1);
            foliageRow(w, def, r, att.x, att.y, att.z, rr, dy, att.dbl);
        }
        break;
    case 2: // dark oak
    {
        const int yy = att.y + offset;
        if (att.dbl)
        {
            foliageRow(w, def, r, att.x, yy, att.z, rad + 2, -1, true);
            foliageRow(w, def, r, att.x, yy, att.z, rad + 3, 0, true);
            foliageRow(w, def, r, att.x, yy, att.z, rad + 2, 1, true);
            if (r.nextBool())
                foliageRow(w, def, r, att.x, yy, att.z, rad, 2, true);
        }
        else
        {
            foliageRow(w, def, r, att.x, yy, att.z, rad + 2, -1, false);
            foliageRow(w, def, r, att.x, yy, att.z, rad + 1, 0, false);
        }
    }
    break;
    case 3: // acacia
    {
        const int yy = att.y + offset;
        foliageRow(w, def, r, att.x, yy, att.z, rad + att.roff, -1, att.dbl);
        foliageRow(w, def, r, att.x, yy, att.z, rad - 1, 0, att.dbl);
        foliageRow(w, def, r, att.x, yy, att.z, rad + att.roff - 1, 1, att.dbl);
    }
    break;
    case 4: // pine
    {
        int acc = 0;
        for (int dy = offset; dy >= offset - fh; --dy)
        {
            foliageRow(w, def, r, att.x, att.y, att.z, acc, dy, att.dbl);
            if (acc >= 1 && dy == offset - fh + 1)
                --acc;
            else if (acc < rad + att.roff)
                ++acc;
        }
    }
    break;
    case 5: // spruce
    {
        int rr = r.nextIntBounded(2);
        int i = 1;
        int j = 0;
        for (int dy = offset; dy >= offset - fh; --dy)
        {
            foliageRow(w, def, r, att.x, att.y, att.z, rr, dy, att.dbl);
            if (rr >= i)
            {
                rr = j;
                j = 1;
                i = qMin(i + 1, rad + att.roff);
            }
            else
            {
                ++rr;
            }
        }
    }
    break;
    case 6: // mega pine
    {
        int prev = 0;
        for (int y = att.y + offset - fh; y <= att.y + offset; ++y)
        {
            const int d = att.y - y;
            int rr = rad + att.roff + int(std::floor(double(d) / double(fh) * 3.5));
            if (d > 0 && rr == prev && (y & 1) == 0)
                ++rr;
            foliageRow(w, def, r, att.x, y, att.z, qMax(rr, 0), 0, att.dbl);
            prev = rr;
        }
    }
    break;
    case 7: // mega jungle
    {
        const int n = att.dbl ? fh : 1 + r.nextIntBounded(2);
        for (int dy = offset; dy >= offset - n; --dy)
        {
            const int rr = rad + att.roff + 1 - dy;
            foliageRow(w, def, r, att.x, att.y, att.z, qMax(rr, 0), dy, att.dbl);
        }
    }
    break;
    case 9: // bush
        for (int dy = offset; dy >= offset - fh; --dy)
        {
            const int rr = qMax(rad + att.roff - 1 - dy, 0);
            foliageRow(w, def, r, att.x, att.y, att.z, rr, dy, att.dbl);
        }
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------------ 装饰器抽签
void runDecorators(Ctx &w, const TreeDef &def, Rng &r)
{
    if (!def.deco || !*def.deco)
        return;
    const QStringList items = QString::fromLatin1(def.deco).split(QLatin1Char(';'),
                                                                  Qt::SkipEmptyParts);
    for (const QString &item : items)
    {
        const QStringList kv = item.split(QLatin1Char('|'));
        if (kv.isEmpty())
            continue;
        const QString name = kv.at(0);
        const double prob = kv.size() > 1 ? kv.at(1).toDouble() : 0.0;
        if (name == QLatin1String("leave_vine"))
        {
            for (int i = 0; i < w.leaves.size(); ++i)
                for (int d = 0; d < 4; ++d)
                    r.nextFloat();
        }
        else if (name == QLatin1String("trunk_vine"))
        {
            for (int i = 0; i < w.logs.size(); ++i)
                for (int d = 0; d < 4; ++d)
                    r.nextIntBounded(3);
        }
        else if (name == QLatin1String("cocoa"))
        {
            for (int i = 0; i < w.logs.size(); ++i)
                for (int d = 0; d < 4; ++d)
                    r.nextFloat();
        }
        else if (name == QLatin1String("alter_ground"))
        {
            for (int i = 0; i < w.logs.size(); ++i)
                for (int k = 0; k < 5; ++k)
                    r.nextIntBounded(64);
        }
        else if (name == QLatin1String("beehive") || name == QLatin1String("pale_moss")
                 || name == QLatin1String("creaking_heart")
                 || name == QLatin1String("attached_to_leaves"))
        {
            Q_UNUSED(prob);
            r.nextFloat();
        }
    }
}

// ------------------------------------------------------------------ 放树
#ifdef BB_TREE_STATS
int g_attempts = 0, g_placed = 0, g_noSpace = 0, g_selectorMiss = 0;
#endif

bool placeTree(Ctx &w, const TreeDef &defIn, Rng &r, int x, int y, int z)
{
#ifdef BB_TREE_STATS
    ++g_attempts;
#endif
    // 未复刻的树形（弯干 / 樱花干 / 向上分枝 / 随机散布树冠）退回最接近的已实现
    // 树形，而不是整棵丢掉 —— 丢掉会让该树种在 selector 里的概率份额消失，
    // 整片林子的密度都会随之变稀。
    TreeDef def = defIn;
    if (def.trunk > 5)
        def.trunk = (def.trunk == 7) ? 0 : 5;
    if (def.fol > 7 && def.fol != 9)
        def.fol = 0;

    const int h = trunkHeight(def, r);
    const int fh = foliageHeight(def, r, h);
    const int rad = foliageRadius(def, r, h, h - fh);

    if (y < -63 || y + h + 1 > 320)
        return false;

    const int treeH = computeTreeHeight(w, def, h, x, y, z);
    if (treeH < h && (def.clipped < 0 || treeH < def.clipped))
    {
#ifdef BB_TREE_STATS
        ++g_noSpace;
#endif
        return false;
    }

    w.local.clear();
    w.logs.clear();
    w.leaves.clear();
    w.lx = x;
    w.ly = y;
    w.lz = z;
    w.curKind = def.kind;

    QVector<Attach> atts;
    switch (def.trunk)
    {
    case 0:
        trunkStraight(w, def, r, x, y, z, treeH, atts);
        break;
    case 1:
        trunkDarkOak(w, def, r, x, y, z, treeH, atts);
        break;
    case 2:
        trunkFancy(w, def, r, x, y, z, treeH, atts);
        break;
    case 3:
        trunkGiant(w, def, r, x, y, z, treeH, atts);
        break;
    case 4:
        trunkMegaJungle(w, def, r, x, y, z, treeH, atts);
        break;
    case 5:
        trunkForking(w, def, r, x, y, z, treeH, atts);
        break;
    default:
        break;
    }
    for (int i = 0; i < atts.size(); ++i)
        foliageCreate(w, def, r, atts[i], treeH, fh, rad);

    runDecorators(w, def, r);
#ifdef BB_TREE_STATS
    ++g_placed;
#endif
    return true;
}

// ------------------------------------------------------------------ 特征
bool biomeHasTrees(int biome)
{
    for (int i = 0; i < kBiomeFeatCount; ++i)
        if (kBiomeFeats[i].biome == biome)
            return true;
    return false;
}

int sampleCount(const FeatDef &f, Rng &r)
{
    switch (f.countMode)
    {
    case 2:
        return sampleRange(f.cA, f.cB, r);
    case 3:
    {
        const int total = f.wA + f.wB;
        if (total <= 0)
            return f.cA;
        const int pick = r.nextIntBounded(total);
        return (pick < f.wA) ? f.cA : f.cB;
    }
    default:
        return f.cA;
    }
}

void runFeature(Ctx &w, const FeatDef &f, Rng &rng, int chunkX, int chunkZ)
{
    const int n = sampleCount(f, rng);
    for (int i = 0; i < n; ++i)
    {
        // 原版修饰链顺序：rarity_filter 在 in_square 之前
        if (f.rarity > 0.0f && rng.nextFloat() >= f.rarity)
            continue;

        const int px = chunkX * 16 + rng.nextIntBounded(16);
        const int pz = chunkZ * 16 + rng.nextIntBounded(16);
        const int py = w.heightAt(px, pz);

        if (f.maxWater == 0 && py < 62)
            continue; // 水下（近似 surface_water_depth_filter）
        // 落点群系完全不产树（海、河、沙漠、雪滩…）就不长，别把树摆进水里/沙里
        if (!biomeHasTrees(w.biomeAt(px, pz)))
            continue;

        // random_selector：按顺序抽 chance，命中即尝试，失败不再回退
        bool picked = false;
        for (int s = 0; s < f.selCount; ++s)
        {
            const SelEntry &e = kSel[f.selFirst + s];
            if (double(rng.nextFloat()) * 1000.0 >= e.chance)
                continue;
            placeTree(w, kTrees[e.tree], rng, px, py, pz);
            picked = true;
            break;
        }
#ifdef BB_TREE_STATS
        if (!picked)
            ++g_selectorMiss;
#endif
    }
}

} // namespace

namespace TreeGen
{
bool supported(int mc)
{
    // 数据表由 1.21.4（DataVersion 4189）的官方 worldgen 数据生成；
    // 本工程捆绑的 cubiomes 把 1.21.4 归在 MC_1_21（Winter Drop）下。
    return mc == MC_1_21 || mc == MC_1_21_WD;
}

void generate(QVector<qint16> &top, QVector<quint8> &kind, int originX, int originZ,
              int grid, quint64 seed, const QVector<int> &biomeIds, const QVector<float> &ys)
{
    if (grid <= 0 || top.size() < grid * grid || kind.size() < grid * grid)
        return;

    Ctx w;
    w.ox = originX;
    w.oz = originZ;
    w.grid = grid;
    w.biome = &biomeIds;
    w.top = &top;
    w.kind = &kind;
    w.heights.resize(grid * grid);
    for (int i = 0; i < grid * grid; ++i)
    {
        float y = ys.size() > i ? ys.at(i) : 64.0f;
        if (y < -30000.0f)
            y = 64.0f;
        w.heights[i] = int(std::floor(y + 0.5f)) + 1;
    }

    const int cx0 = originX / 16;
    const int cz0 = originZ / 16;
    const int chunks = grid / 16 + 2;
    for (int cz = cz0 - 1; cz < cz0 + chunks; ++cz)
    {
        for (int cx = cx0 - 1; cx < cx0 + chunks; ++cx)
        {
            const quint64 deco = decorationSeed(seed, cx * 16, cz * 16);
            // 原版按区块（更准确地说是按 4×4×4 群系段）跑该群系 features[9] 的列表：
            // 用区块中心的群系取它自己那份树特征，按列表顺序执行，
            // 每棵特征用 setFeatureSeed(deco, 列表下标, step=9) 重播种。
            const int biome = w.biomeAt(cx * 16 + 8, cz * 16 + 8);
            for (int i = 0; i < kBiomeFeatCount; ++i)
            {
                if (kBiomeFeats[i].biome != biome)
                    continue;
                const FeatDef &f = kFeats[kBiomeFeats[i].feat];
                Rng rng;
                rng.setSeed(deco + quint64(kBiomeFeats[i].listIndex) + 90000ULL);
                runFeature(w, f, rng, cx, cz);
            }
        }
    }
}
} // namespace TreeGen
