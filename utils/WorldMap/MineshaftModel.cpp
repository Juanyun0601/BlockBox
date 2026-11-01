/**
 * @file   MineshaftModel.cpp
 * @brief  废弃矿井逐块重建（移植 MineshaftStructure / MineshaftPieces）
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 对照 1.21.4 反编译代码逐行还原：
 *   MineshaftStructure.findGenerationPoint / addPieces（含 shiftInto 下移）
 *   MineshaftPieces.createRandomShaftPiece / generateAndAddPiece
 *   MineShaftRoom / MineShaftCorridor / MineShaftCrossing / MineShaftStairs
 *   StructurePiece.placeBlock / generateBox / generateMaybeBox /
 *   generateBoxEllipsoid / createChest，以及各段的 postProcess
 *
 * 随机源有两套，和游戏内完全一致：
 *   - 布局：WorldgenRandom(LegacyRandomSource) +
 *     setLargeFeatureSeed(worldSeed, 起始区块)，先空掷一次 nextDouble()
 *     再开始拼装；
 *   - 落块：按区块逐个来，随机源是 WorldgenRandom(XoroshiroRandomSource)：
 *     先用 setDecorationSeed(worldSeed, 区块原点) 得到基准值，再
 *     setFeatureSeed(基准值, 结构序号, 装饰阶段)（地下结构阶段 = 3、矿井
 *     序号 = 1）—— 因此同一座矿井跨多个区块时，各区块里的铁轨、蜘蛛网、
 *     火把都是各自区块的随机结果。
 *
 * 预览没有地形数据，矿井周围按「实心石头」处理（矿井自己挖空的位置记成
 * 空气），这与游戏里埋在地下时的判断一致；地形石头不会进入结果块表。
 * 游戏在洞穴、含水层里会跳过贴着液体的段，也不给「头顶是空气」的巷道搭
 * 木支架，这部分差异预览复现不了（按完整矿井展示）。
 */

#include "MineshaftModel.h"

#include "StructureFinder.h"

#include <algorithm>
#include <cstdlib>
#include <deque>

#include "biomes.h"   // enum MCVersion
#include <vector>

namespace MineshaftModel {
namespace {

// ────────────────────────────────────────────────────────────────────────────
// 原版随机源（LegacyRandomSource / WorldgenRandom）
// ────────────────────────────────────────────────────────────────────────────

struct LegacyRng
{
    quint64 state = 0;

    void setSeed(qint64 seed)
    {
        state = (quint64(seed) ^ 0x5DEECE66DULL) & 0xFFFFFFFFFFFFULL;
    }
    int next(int bits)
    {
        state = (state * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
        return int(state >> (48 - bits));
    }
    int nextInt()
    {
        return int(qint32(next(32)));
    }
    int nextInt(int bound)
    {
        if (bound <= 0)
            return 0;
        if ((bound & (bound - 1)) == 0)
            return int((qint64(bound) * qint64(next(31))) >> 31);
        for (;;)
        {
            const qint64 bits = next(31);
            const qint64 val = bits % bound;
            const qint64 r = bits - val + (bound - 1);
            if (r >= 0 && r <= 0x7FFFFFFFLL)
                return int(val);
        }
    }
    qint64 nextLong()
    {
        const quint64 hi = quint64(qint64(qint32(next(32)))) << 32;
        const quint64 lo = quint64(qint64(qint32(next(32))));
        return qint64(hi + lo);
    }
    float nextFloat()
    {
        return float(next(24)) / float(1 << 24);
    }
    double nextDouble()
    {
        const qint64 hi = qint64(next(26));
        const qint64 lo = qint64(next(27));
        return double((hi << 27) + lo) / double(0x1p53);
    }
    bool nextBoolean()
    {
        return next(1) != 0;
    }

    /** WorldgenRandom.setLargeFeatureSeed（结构布局用） */
    void setLargeFeatureSeed(qint64 seed, int chunkX, int chunkZ)
    {
        setSeed(seed);
        const qint64 a = nextLong();
        const qint64 b = nextLong();
        setSeed(qint64(quint64(chunkX) * quint64(a) ^ quint64(chunkZ) * quint64(b)
                       ^ quint64(seed)));
    }

    /** WorldgenRandom.setDecorationSeed（落块基准种子，返回同一值） */
    qint64 setDecorationSeed(qint64 seed, int blockX, int blockZ)
    {
        setSeed(seed);
        const qint64 a = nextLong() | 1LL;
        const qint64 b = nextLong() | 1LL;
        const qint64 k = qint64(quint64(qint64(blockX) * a) + quint64(qint64(blockZ) * b))
                       ^ seed;
        setSeed(k);
        return k;
    }

    /** WorldgenRandom.setFeatureSeed */
    void setFeatureSeed(qint64 seed, int index, int step)
    {
        setSeed(qint64(quint64(seed) + quint64(index) + quint64(10000) * quint64(step)));
    }
};

// ────────────────────────────────────────────────────────────────────────────
// 落块随机源：原版区块装饰阶段用的是 Xoroshiro128++
// （XoroshiroRandomSource + setDecorationSeed / setFeatureSeed）
// ────────────────────────────────────────────────────────────────────────────

struct XoroshiroRng
{
    quint64 s0 = 0, s1 = 0;
    int count = 0;

    static quint64 mix(quint64 v)
    {
        v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
        v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
        return v ^ (v >> 31);
    }
    static quint64 rotl(quint64 x, int k)
    {
        return (x << k) | (x >> (64 - k));
    }
    void setSeed(qint64 seed)
    {
        const quint64 a = quint64(seed) ^ 0x6A09E667F3BCC909ULL;
        const quint64 b = a - 0x61C8864680B583EBULL;
        s0 = mix(a);
        s1 = mix(b);
        if ((s0 | s1) == 0)
        {
            s0 = 0x9E3779B97F4A7C15ULL;
            s1 = 0x6A09E667F3BCC909ULL;
        }
        count = 0;
    }
    /** Xoroshiro128++ 原生抽样（XoroshiroRandomSource.nextLong） */
    quint64 rawLong()
    {
        const quint64 a = s0;
        quint64 b = s1;
        const quint64 out = rotl(a + b, 17) + a;
        b ^= a;
        s0 = rotl(a, 49) ^ b ^ (b << 21);
        s1 = rotl(b, 28);
        return out;
    }
    /**
     * WorldgenRandom 包装 Xoroshiro 时走的是 BitRandomSource：
     * 每次 next(bits) 取原生抽样的高 bits 位，与 XoroshiroRandomSource 自己的
     * nextInt/nextLong 并不相同（这一点直接决定落块随机序列）。
     */
    int next(int bits)
    {
        ++count;
        return int(quint32(rawLong() >> (64 - bits)));
    }
    int nextInt() { return next(32); }
    int nextInt(int bound)
    {
        if (bound <= 0)
            return 0;
        if ((bound & (bound - 1)) == 0)
            return int((qint64(bound) * qint64(next(31))) >> 31);
        for (;;)
        {
            const qint64 i = next(31);
            const qint64 j = i % bound;
            if (i - j + (bound - 1) >= 0)
                return int(j);
        }
    }
    qint64 nextLong()
    {
        const qint64 hi = qint64(qint32(quint32(next(32))));
        const qint64 lo = qint64(qint32(quint32(next(32))));
        return (hi << 32) + lo;
    }
    float nextFloat() { return float(next(24)) * 5.9604645e-8f; }
    bool nextBoolean() { return next(1) != 0; }

    qint64 setDecorationSeed(qint64 seed, int blockX, int blockZ)
    {
        setSeed(seed);
        const qint64 a = nextLong() | 1LL;
        const qint64 b = nextLong() | 1LL;
        const qint64 k = qint64(quint64(qint64(blockX) * a)
                              + quint64(qint64(blockZ) * b)) ^ seed;
        setSeed(k);
        return k;
    }
    void setFeatureSeed(qint64 seed, int index, int step)
    {
        setSeed(seed + qint64(index) + qint64(10000) * qint64(step));
    }
};

// ────────────────────────────────────────────────────────────────────────────
// 方向（与原版 Direction 的 get3DDataValue() 一致）
// ────────────────────────────────────────────────────────────────────────────

enum : int
{
    DIR_DOWN = 0,
    DIR_UP = 1,
    DIR_NORTH = 2,
    DIR_SOUTH = 3,
    DIR_WEST = 4,
    DIR_EAST = 5,
    DIR_NONE = -1
};

inline int dirOpposite(int d) { return d ^ 1; }

/** Rotation.CLOCKWISE_90.rotate(Direction)（水平方向） */
inline int dirCw90(int d)
{
    switch (d)
    {
    case DIR_NORTH: return DIR_EAST;
    case DIR_EAST:  return DIR_SOUTH;
    case DIR_SOUTH: return DIR_WEST;
    default:        return DIR_NORTH;
    }
}

inline int dirDx(int d) { return d == DIR_EAST ? 1 : (d == DIR_WEST ? -1 : 0); }
inline int dirDy(int d) { return d == DIR_UP ? 1 : (d == DIR_DOWN ? -1 : 0); }
inline int dirDz(int d) { return d == DIR_SOUTH ? 1 : (d == DIR_NORTH ? -1 : 0); }

inline const char *dirName(int d)
{
    switch (d)
    {
    case DIR_NORTH: return "north";
    case DIR_SOUTH: return "south";
    case DIR_WEST:  return "west";
    default:        return "east";
    }
}

/**
 * 原版 StructurePiece.setOrientation 推出的「代码方位 → 世界方位」映射：
 *   NORTH 不转；SOUTH 左右镜像；WEST 镜像后顺时针 90°；EAST 顺时针 90°。
 */
inline int worldDirOfState(int codeDir, int orient)
{
    switch (orient)
    {
    case DIR_NORTH:
        return codeDir;
    case DIR_SOUTH:
        return (codeDir == DIR_NORTH) ? DIR_SOUTH
             : (codeDir == DIR_SOUTH) ? DIR_NORTH : codeDir;
    case DIR_WEST:
    {
        const int mirrored = (codeDir == DIR_NORTH) ? DIR_SOUTH
                           : (codeDir == DIR_SOUTH) ? DIR_NORTH : codeDir;
        return dirCw90(mirrored);
    }
    case DIR_EAST:
        return dirCw90(codeDir);
    default:
        return codeDir;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 方块状态
// ────────────────────────────────────────────────────────────────────────────

const QString kStone = QStringLiteral("minecraft:stone");          // 内部：地形默认
const QString kAir = QStringLiteral("minecraft:cave_air");        // 挖空标记
/** 矿井变体用到的木料（普通 = 橡木，恶地 = 深色橡木） */
struct Style
{
    QString planks = QStringLiteral("minecraft:oak_planks");
    QString wood = QStringLiteral("minecraft:oak_log[axis=y]");
    QString fence = QStringLiteral("minecraft:oak_fence");
};

const Style kStyleNormal;
const Style kStyleMesa{QStringLiteral("minecraft:dark_oak_planks"),
                        QStringLiteral("minecraft:dark_oak_log[axis=y]"),
                        QStringLiteral("minecraft:dark_oak_fence")};
const QString kCobweb = QStringLiteral("minecraft:cobweb");
const QString kSpawner = QStringLiteral("minecraft:spawner");
const QString kRailNS = QStringLiteral("minecraft:rail[shape=north_south]");
const QString kRailEW = QStringLiteral("minecraft:rail[shape=east_west]");
const QString kChain = QStringLiteral("minecraft:iron_chain[axis=y,waterlogged=false]");

QString fenceState(const Style &style, bool n, bool e, bool s, bool w)
{
    return style.fence + QStringLiteral("[east=%1,north=%2,south=%3,waterlogged=false,west=%4]")
        .arg(e ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(n ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(s ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(w ? QStringLiteral("true") : QStringLiteral("false"));
}

/** 铁轨：代码里固定写南北向，段朝东西时旋转成东西向 */
QString railState(bool northSouth, int orient)
{
    const bool flip = (orient == DIR_WEST || orient == DIR_EAST);
    const bool ns = flip ? !northSouth : northSouth;
    return ns ? kRailNS : kRailEW;
}

QString torchState(int codeFacing, int orient)
{
    return QStringLiteral("minecraft:wall_torch[facing=%1]")
        .arg(QString::fromLatin1(dirName(worldDirOfState(codeFacing, orient))));
}

inline bool isAirState(const QString &s)
{
    return s.isEmpty() || s == kAir || s == QLatin1String("minecraft:air");
}

inline bool isLavaState(const QString &s)
{
    return s.startsWith(QLatin1String("minecraft:lava"));
}

/** 原版 isReplaceableByStructures：空气 / 水 / 岩浆等可被结构替换 */
inline bool isReplaceable(const QString &s)
{
    if (s.isEmpty())
        return true;
    static const char *kNames[] = {"air", "water", "lava", "glow_lichen", "seagrass",
                                   "tall_seagrass", "vine", "snow", "short_grass",
                                   "fern", "large_fern", "dead_bush", "cobweb"};
    const int bracket = s.indexOf(QLatin1Char('['));
    const QString name = bracket >= 0 ? s.left(bracket) : s;
    for (const char *n : kNames)
        if (name == QLatin1String("minecraft:") + QLatin1String(n))
            return true;
    return false;
}

/** 面是否实心（预览用的近似：整块方块的朝向面都算实心） */
inline bool isFaceSturdy(const QString &s, int /*dir*/)
{
    if (isAirState(s) || s == kStone)
        return s == kStone;
    if (s.startsWith(QLatin1String("minecraft:cobweb"))
        || s.startsWith(QLatin1String("minecraft:rail"))
        || s.startsWith(QLatin1String("minecraft:wall_torch"))
        || s.startsWith(QLatin1String("minecraft:torch"))
        || s.startsWith(QLatin1String("minecraft:iron_chain"))
        || s.startsWith(QLatin1String("minecraft:spawner")))
        return false;
    if (s.contains(QLatin1String("fence")))
        return false;
    return true;
}

inline bool isFallingBlock(const QString &s)
{
    return s.startsWith(QLatin1String("minecraft:sand"))
        || s.startsWith(QLatin1String("minecraft:gravel"))
        || s.startsWith(QLatin1String("minecraft:anvil"));
}

// ────────────────────────────────────────────────────────────────────────────
// 包围盒（原版 BoundingBox）
// ────────────────────────────────────────────────────────────────────────────

struct Box
{
    int x0 = 0, y0 = 0, z0 = 0, x1 = -1, y1 = -1, z1 = -1;

    bool valid() const { return x1 >= x0 && y1 >= y0 && z1 >= z0; }
    bool intersects(const Box &o) const
    {
        return o.x1 >= x0 && o.x0 <= x1 && o.y1 >= y0 && o.y0 <= y1
            && o.z1 >= z0 && o.z0 <= z1;
    }
    bool contains(int x, int y, int z) const
    {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1;
    }
    int xSpan() const { return x1 - x0 + 1; }
    int ySpan() const { return y1 - y0 + 1; }
    int zSpan() const { return z1 - z0 + 1; }
    void move(int dx, int dy, int dz)
    {
        x0 += dx; x1 += dx; y0 += dy; y1 += dy; z0 += dz; z1 += dz;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// 段类型
// ────────────────────────────────────────────────────────────────────────────

enum PType
{
    T_ROOM = 0,
    T_CORRIDOR,
    T_CROSSING,
    T_STAIRS
};

const char *typeName(int t)
{
    switch (t)
    {
    case T_ROOM:      return "room";
    case T_CORRIDOR:  return "corridor";
    case T_CROSSING:  return "crossing";
    default:          return "stairs";
    }
}

struct Piece
{
    int type = T_ROOM;
    int depth = 0;
    int dir = DIR_NONE;         ///< 段朝向（主厅 / 路口没有朝向，落块按绝对坐标走）
    int faceDir = DIR_NONE;     ///< 生成子段用的方向（路口也有）
    Box box;
    bool hasRails = false;      ///< 巷道：是否铺铁轨
    bool spider = false;        ///< 巷道：是否蜘蛛刷怪笼走廊
    bool spiderPlaced = false;  ///< 刷怪笼是否已放
    int sections = 0;           ///< 巷道分节数
    bool twoFloors = false;     ///< 路口：两层
    std::vector<Box> entrances; ///< 主厅：延伸口
};

// ────────────────────────────────────────────────────────────────────────────
// 世界状态（相对起始区块原点存储；未记录的位置按实心石头处理）
// ────────────────────────────────────────────────────────────────────────────

struct Grid
{
    QHash<qint64, QString> cells;
    int originX = 0;
    int originZ = 0;

    QString at(int wx, int y, int wz) const
    {
        return cells.value(packKey(wx - originX, y, wz - originZ), kStone);
    }
    void set(int wx, int y, int wz, const QString &s)
    {
        cells.insert(packKey(wx - originX, y, wz - originZ), s);
    }
    bool has(int wx, int y, int wz) const
    {
        return cells.contains(packKey(wx - originX, y, wz - originZ));
    }
};

// ────────────────────────────────────────────────────────────────────────────
// 摆放器：把段的局部坐标换算成世界坐标，并做结构落块的各种判断
// ────────────────────────────────────────────────────────────────────────────

struct Placer
{
    Grid &grid;
    Piece &pc;
    XoroshiroRng &rng;
    Box clip;
    const Style &style;
    int minY = -64;
    int maxY = 320;

    int wx(int lx, int lz) const
    {
        switch (pc.dir)
        {
        case DIR_NONE:
            return lx;
        case DIR_NORTH:
        case DIR_SOUTH:
            return pc.box.x0 + lx;
        case DIR_WEST:
            return pc.box.x1 - lz;
        default: // EAST
            return pc.box.x0 + lz;
        }
    }
    int wz(int lx, int lz) const
    {
        switch (pc.dir)
        {
        case DIR_NONE:
            return lz;
        case DIR_NORTH:
            return pc.box.z1 - lz;
        case DIR_SOUTH:
            return pc.box.z0 + lz;
        case DIR_WEST:
            return pc.box.z0 + lx;
        default: // EAST
            return pc.box.z0 + lx;
        }
    }
    int wy(int ly) const
    {
        return pc.dir == DIR_NONE ? ly : pc.box.y0 + ly;
    }

    /** 原版 StructurePiece.getBlock：区块框外的位置一律当空气 */
    QString getBlock(int lx, int ly, int lz) const
    {
        const int x = wx(lx, lz), y = wy(ly), z = wz(lx, lz);
        if (!clip.contains(x, y, z))
            return QStringLiteral("minecraft:air");
        return grid.at(x, y, z);
    }

    /** 原版 MineShaftPiece.canBePlaced：木料 / 栏杆 / 岩浆不被覆盖 */
    bool canBePlaced(int lx, int ly, int lz) const
    {
        const QString s = getBlock(lx, ly, lz);
        if (s == style.planks || s == style.wood || isLavaState(s))
            return false;
        return !s.contains(QLatin1String("fence"));
    }

    void placeBlock(const QString &state, int lx, int ly, int lz)
    {
        const int x = wx(lx, lz), y = wy(ly), z = wz(lx, lz);
        if (!clip.contains(x, y, z))
            return;
        if (!canBePlaced(lx, ly, lz))
            return;
        grid.set(x, y, z, state);
    }

    /** 原版 generateBox：边框用 state1、内部用 state2 */
    void generateBox(int x1, int y1, int z1, int x2, int y2, int z2,
                     const QString &state1, const QString &state2, bool requireNonAir)
    {
        if (x1 > x2) std::swap(x1, x2);
        if (y1 > y2) std::swap(y1, y2);
        if (z1 > z2) std::swap(z1, z2);
        for (int y = y1; y <= y2; ++y)
            for (int x = x1; x <= x2; ++x)
                for (int z = z1; z <= z2; ++z)
                {
                    if (requireNonAir && isAirState(getBlock(x, y, z)))
                        continue;
                    const bool edge = (y == y1 || y == y2 || x == x1 || x == x2
                                       || z == z1 || z == z2);
                    placeBlock(edge ? state1 : state2, x, y, z);
                }
    }

    /** 原版 generateMaybeBox：按概率铺，可选只铺非空气 / 只铺内部 */
    void generateMaybeBox(float prob, int x1, int y1, int z1, int x2, int y2, int z2,
                          const QString &state1, const QString &state2,
                          bool requireNonAir, bool onlyInterior)
    {
        if (x1 > x2) std::swap(x1, x2);
        if (y1 > y2) std::swap(y1, y2);
        if (z1 > z2) std::swap(z1, z2);
        for (int y = y1; y <= y2; ++y)
            for (int x = x1; x <= x2; ++x)
                for (int z = z1; z <= z2; ++z)
                {
                    const float r = rng.nextFloat();
                    if (r > prob)
                        continue;
                    if (requireNonAir && isAirState(getBlock(x, y, z)))
                        continue;
                    if (onlyInterior && !isInterior(x, y, z))
                        continue;
                    const bool edge = (y == y1 || y == y2 || x == x1 || x == x2
                                       || z == z1 || z == z2);
                    placeBlock(edge ? state1 : state2, x, y, z);
                }
    }

    /** 原版 generateBoxEllipsoid */
    void generateEllipsoid(int x1, int y1, int z1, int x2, int y2, int z2,
                           const QString &state, bool requireNonAir)
    {
        if (x1 > x2) std::swap(x1, x2);
        if (y1 > y2) std::swap(y1, y2);
        if (z1 > z2) std::swap(z1, z2);
        const float w = float(x2 - x1 + 1);
        const float h = float(y2 - y1 + 1);
        const float d = float(z2 - z1 + 1);
        const float cx = float(x1) + w / 2.0f;
        const float cz = float(z1) + d / 2.0f;
        for (int y = y1; y <= y2; ++y)
        {
            const float ry = float(y - y1) / h;
            for (int x = x1; x <= x2; ++x)
            {
                const float rx = (float(x) - cx) / (w * 0.5f);
                for (int z = z1; z <= z2; ++z)
                {
                    const float rz = (float(z) - cz) / (d * 0.5f);
                    if (requireNonAir && isAirState(getBlock(x, y, z)))
                        continue;
                    if (rx * rx + ry * ry + rz * rz <= 1.05f)
                        placeBlock(state, x, y, z);
                }
            }
        }
    }

    /** 原版 StructurePiece.isInterior：区块内且低于海平面高度图（预览按地下处理） */
    bool isInterior(int lx, int ly, int lz) const
    {
        const int x = wx(lx, lz), y = wy(ly) + 1, z = wz(lx, lz);
        if (!clip.contains(x, y, z))
            return false;
        return y < 320;   // 预览假定矿井埋在地下
    }

    /** 原版 isSupportingBox：横梁上方一整排都不是空气才算有支撑 */
    bool isSupportingBox(int x0, int x1, int y1, int z) const
    {
        for (int x = x0; x <= x1; ++x)
        {
            const int bx = wx(x, z), by = wy(y1 + 1), bz = wz(x, z);
            if (!clip.contains(bx, by, bz))
                return false;
            if (isAirState(grid.at(bx, by, bz)))
                return false;
        }
        return true;
    }

    bool hasSturdyNeighbours(int lx, int ly, int lz, int count) const
    {
        const int x = wx(lx, lz), y = wy(ly), z = wz(lx, lz);
        static const int dirs[] = {DIR_DOWN, DIR_UP, DIR_NORTH, DIR_SOUTH, DIR_WEST, DIR_EAST};
        int n = 0;
        for (int d : dirs)
        {
            const int nx = x + dirDx(d), ny = y + dirDy(d), nz = z + dirDz(d);
            if (!clip.contains(nx, ny, nz))
                continue;
            if (isFaceSturdy(grid.at(nx, ny, nz), dirOpposite(d)))
            {
                if (++n >= count)
                    return true;
            }
        }
        return false;
    }

    void maybePlaceCobweb(float prob, int lx, int ly, int lz)
    {
        if (isInterior(lx, ly, lz) && rng.nextFloat() < prob
            && hasSturdyNeighbours(lx, ly, lz, 2))
            placeBlock(kCobweb, lx, ly, lz);
    }

    /** 原版 placeSupport：两根立柱 + 横梁（少数情况只铺两块木板+火把） */
    void placeSupport(int x0, int y0, int z, int y1, int x1)
    {
        if (!isSupportingBox(x0, x1, y1, z))
            return;

        generateBox(x0, y0, z, x0, y1 - 1, z, fenceState(style, false, false, false, true), kAir, false);
        generateBox(x1, y0, z, x1, y1 - 1, z, fenceState(style, false, true, false, false), kAir, false);

        if (rng.nextInt(4) == 0)
        {
            generateBox(x0, y1, z, x0, y1, z, style.planks, kAir, false);
            generateBox(x1, y1, z, x1, y1, z, style.planks, kAir, false);
        }
        else
        {
            generateBox(x0, y1, z, x1, y1, z, style.planks, kAir, false);
            if (rng.nextFloat() < 0.05f)
                placeBlock(torchState(DIR_SOUTH, pc.dir), x0 + 1, y1, z - 1);
            if (rng.nextFloat() < 0.05f)
                placeBlock(torchState(DIR_NORTH, pc.dir), x0 + 1, y1, z + 1);
        }
    }

    /** 原版 createChest：铺一格铁轨并生成箱子矿车（预览只留铁轨）。返回是否放成功 */
    bool createChest(int lx, int ly, int lz)
    {
        const int x = wx(lx, lz), y = wy(ly), z = wz(lx, lz);
        if (!clip.contains(x, y, z))
            return false;
        if (!isAirState(grid.at(x, y, z)))
            return false;
        if (isAirState(grid.at(x, y - 1, z)))
            return false;

        const bool northSouth = rng.nextBoolean();
        placeBlock(railState(northSouth, pc.dir), lx, ly, lz);
        rng.nextLong();   // 箱子矿车的战利品种子
        return true;
    }

    /** 原版 placePlanksBlock：地面不实心的地方补木板 */
    void placePlanksBlock(int lx, int ly, int lz)
    {
        if (!isInterior(lx, ly, lz))
            return;
        const int x = wx(lx, lz), y = wy(ly), z = wz(lx, lz);
        if (!isFaceSturdy(grid.at(x, y, z), DIR_UP))
            grid.set(x, y, z, style.planks);
    }

    /** 原版 fillPillarDownOrChainUp：往下补原木立柱，往上挂栅栏 + 铁链 */
    void fillPillarDownOrChainUp(int lx, int ly, int lz)
    {
        const int x = wx(lx, lz), y0 = wy(ly), z = wz(lx, lz);
        if (!clip.contains(x, y0, z))
            return;

        bool below = true, above = true;
        for (int dist = 1; below || above; ++dist)
        {
            if (below)
            {
                const int by = y0 - dist;
                const QString s = grid.at(x, by, z);
                const bool repl = isReplaceable(s) && !isLavaState(s);
                if (!repl && isFaceSturdy(s, DIR_UP))
                {
                    for (int py = by + 1; py < y0; ++py)
                        grid.set(x, py, z, style.wood);
                    return;
                }
                below = dist <= 20 && repl && by > minY + 1;
            }
            if (above)
            {
                const int ay = y0 + dist;
                const QString s = grid.at(x, ay, z);
                const bool repl = isReplaceable(s);
                if (!repl && isFaceSturdy(s, DIR_DOWN) && !isFallingBlock(s))
                {
                    grid.set(x, y0 + 1, z, fenceState(style, false, false, false, false));
                    for (int py = y0 + 2; py < ay; ++py)
                        grid.set(x, py, z, kChain);
                    return;
                }
                above = dist <= 50 && repl && ay < maxY;
            }
        }
    }

    void placeDoubleLowerOrUpperSupport(int lx, int ly, int lz)
    {
        if (getBlock(lx, ly, lz) == style.planks)
            fillPillarDownOrChainUp(lx, ly, lz);
        if (getBlock(lx + 2, ly, lz) == style.planks)
            fillPillarDownOrChainUp(lx + 2, ly, lz);
    }
};

// ────────────────────────────────────────────────────────────────────────────
// 拼装（布局）
// ────────────────────────────────────────────────────────────────────────────

struct Gen
{
    LegacyRng rng;
    std::deque<Piece> pieces;   ///< 段列表（顺序即摆放顺序；deque 保证引用稳定）
    int startX = 0, startZ = 0;  ///< 起始区块最小方块坐标
    int minY = -64;              ///< 世界最低 y（原版 chunkGenerator.getMinY()）
    Style style;
    int seaLevel = 63;           ///< 海平面（原版 getSeaLevel()）
    Box roomBox;                 ///< 主厅包围盒（±80 范围判定用）

    Piece &addPiece(const Piece &p)
    {
        pieces.push_back(p);
        return pieces.back();
    }

    bool collides(const Box &b) const
    {
        for (const Piece &p : pieces)
            if (p.box.intersects(b))
                return true;
        return false;
    }

    /** 原版 findCorridorSize */
    Box findCorridorSize(int footX, int footY, int footZ, int dir)
    {
        const int maxLen = rng.nextInt(3) + 2;   // 每次只掷一次
        for (int len = maxLen; len > 0; --len)
        {
            const int blockLen = len * 5;
            Box b;
            switch (dir)
            {
            case DIR_SOUTH: b = {0, 0, 0, 2, 2, blockLen - 1}; break;
            case DIR_WEST:  b = {-(blockLen - 1), 0, 0, 0, 2, 2}; break;
            case DIR_EAST:  b = {0, 0, 0, blockLen - 1, 2, 2}; break;
            default:        b = {0, 0, -(blockLen - 1), 2, 2, 0}; break;
            }
            b.move(footX, footY, footZ);
            if (!collides(b))
                return b;
        }
        return Box{};   // 无效
    }

    /** 原版 MineShaftCrossing.findCrossing */
    Box findCrossing(int footX, int footY, int footZ, int dir)
    {
        const int h = (rng.nextInt(4) == 0) ? 6 : 2;
        Box b;
        switch (dir)
        {
        case DIR_SOUTH: b = {-1, 0, 0, 3, h, 4}; break;
        case DIR_WEST:  b = {-4, 0, -1, 0, h, 3}; break;
        case DIR_EAST:  b = {0, 0, -1, 4, h, 3}; break;
        default:        b = {-1, 0, -4, 3, h, 0}; break;
        }
        b.move(footX, footY, footZ);
        if (collides(b))
            return Box{};
        return b;
    }

    /** 原版 MineShaftStairs.findStairs */
    Box findStairs(int footX, int footY, int footZ, int dir)
    {
        Box b;
        switch (dir)
        {
        case DIR_SOUTH: b = {0, -5, 0, 2, 2, 8}; break;
        case DIR_WEST:  b = {-8, -5, 0, 0, 2, 2}; break;
        case DIR_EAST:  b = {0, -5, 0, 8, 2, 2}; break;
        default:        b = {0, -5, -8, 2, 2, 0}; break;
        }
        b.move(footX, footY, footZ);
        if (collides(b))
            return Box{};
        return b;
    }

    /** 原版 createRandomShaftPiece：返回 nullptr 表示空间不够 */
    bool createRandomShaftPiece(int footX, int footY, int footZ, int dir, int depth,
                                Piece &out)
    {
        const int roll = rng.nextInt(100);
        if (roll >= 80)
        {
            const Box b = findCrossing(footX, footY, footZ, dir);
            if (!b.valid())
                return false;
            out.type = T_CROSSING;
            out.depth = depth;
            out.dir = DIR_NONE;   // 路口不设朝向，落块用绝对坐标
            out.faceDir = dir;
            out.box = b;
            out.twoFloors = b.ySpan() > 3;
            return true;
        }
        if (roll >= 70)
        {
            const Box b = findStairs(footX, footY, footZ, dir);
            if (!b.valid())
                return false;
            out.type = T_STAIRS;
            out.depth = depth;
            out.dir = dir;
            out.faceDir = dir;
            out.box = b;
            return true;
        }
        const Box b = findCorridorSize(footX, footY, footZ, dir);
        if (!b.valid())
            return false;
        out.type = T_CORRIDOR;
        out.depth = depth;
        out.dir = dir;
        out.faceDir = dir;
        out.box = b;
        out.hasRails = rng.nextInt(3) == 0;
        out.spider = !out.hasRails && rng.nextInt(23) == 0;
        out.sections = (dir == DIR_NORTH || dir == DIR_SOUTH) ? b.zSpan() / 5
                                                             : b.xSpan() / 5;
        return true;
    }

    /** 原版 generateAndAddPiece */
    void generateAndAddPiece(int footX, int footY, int footZ, int dir, int depth)
    {
        if (depth > 8)
            return;
        if (std::abs(footX - roomBox.x0) > 80 || std::abs(footZ - roomBox.z0) > 80)
            return;
        Piece p;
        if (!createRandomShaftPiece(footX, footY, footZ, dir, depth + 1, p))
            return;
        Piece &added = addPiece(p);
        addChildren(added);
    }

    void addChildren(Piece &p)
    {
        switch (p.type)
        {
        case T_ROOM:      roomChildren(p); break;
        case T_CORRIDOR:  corridorChildren(p); break;
        case T_CROSSING:  crossingChildren(p); break;
        default:          stairsChildren(p); break;
        }
    }

    void roomChildren(Piece &room)
    {
        const int depth = room.depth;
        int space = room.box.ySpan() - 4;
        if (space <= 0)
            space = 1;
        const int xSpan = room.box.xSpan();
        const int zSpan = room.box.zSpan();

        int pos = 0;
        while (pos < xSpan)
        {
            pos += rng.nextInt(xSpan);
            if (pos + 3 > xSpan)
                break;
            const int x = room.box.x0 + pos;
            const int y = room.box.y0 + rng.nextInt(space) + 1;
            const int z = room.box.z0 - 1;
            if (childBox(x, y, z, DIR_NORTH, depth))
            {
                const Box &cb = pieces.back().box;
                room.entrances.push_back(
                    {cb.x0, cb.y0, room.box.z0, cb.x1, cb.y1, room.box.z0 + 1});
            }
            pos += 4;
        }

        pos = 0;
        while (pos < xSpan)
        {
            pos += rng.nextInt(xSpan);
            if (pos + 3 > xSpan)
                break;
            const int x = room.box.x0 + pos;
            const int y = room.box.y0 + rng.nextInt(space) + 1;
            const int z = room.box.z1 + 1;
            if (childBox(x, y, z, DIR_SOUTH, depth))
            {
                const Box &cb = pieces.back().box;
                room.entrances.push_back(
                    {cb.x0, cb.y0, room.box.z1 - 1, cb.x1, cb.y1, room.box.z1});
            }
            pos += 4;
        }

        pos = 0;
        while (pos < zSpan)
        {
            pos += rng.nextInt(zSpan);
            if (pos + 3 > zSpan)
                break;
            const int x = room.box.x0 - 1;
            const int y = room.box.y0 + rng.nextInt(space) + 1;
            const int z = room.box.z0 + pos;
            if (childBox(x, y, z, DIR_WEST, depth))
            {
                const Box &cb = pieces.back().box;
                room.entrances.push_back(
                    {room.box.x0, cb.y0, cb.z0, room.box.x0 + 1, cb.y1, cb.z1});
            }
            pos += 4;
        }

        pos = 0;
        while (pos < zSpan)
        {
            pos += rng.nextInt(zSpan);
            if (pos + 3 > zSpan)
                break;
            const int x = room.box.x1 + 1;
            const int y = room.box.y0 + rng.nextInt(space) + 1;
            const int z = room.box.z0 + pos;
            if (childBox(x, y, z, DIR_EAST, depth))
            {
                const Box &cb = pieces.back().box;
                room.entrances.push_back(
                    {room.box.x1 - 1, cb.y0, cb.z0, room.box.x1, cb.y1, cb.z1});
            }
            pos += 4;
        }
    }

    /** 生成并加入一个子段，成功返回 true（对应原版 generateAndAddPiece != null） */
    bool childBox(int x, int y, int z, int dir, int depth)
    {
        const size_t before = pieces.size();
        generateAndAddPiece(x, y, z, dir, depth);
        return pieces.size() > before;
    }

    void corridorChildren(Piece &pc)
    {
        const int depth = pc.depth;
        const int endSel = rng.nextInt(4);
        const int randY = pc.box.y0 - 1 + rng.nextInt(3);

        switch (pc.dir)
        {
        case DIR_NORTH:
            if (endSel <= 1)
                generateAndAddPiece(pc.box.x0, randY, pc.box.z0 - 1, pc.dir, depth);
            else if (endSel == 2)
                generateAndAddPiece(pc.box.x0 - 1, randY, pc.box.z0, DIR_WEST, depth);
            else
                generateAndAddPiece(pc.box.x1 + 1, randY, pc.box.z0, DIR_EAST, depth);
            break;
        case DIR_SOUTH:
            if (endSel <= 1)
                generateAndAddPiece(pc.box.x0, randY, pc.box.z1 + 1, pc.dir, depth);
            else if (endSel == 2)
                generateAndAddPiece(pc.box.x0 - 1, randY, pc.box.z1 - 3, DIR_WEST, depth);
            else
                generateAndAddPiece(pc.box.x1 + 1, randY, pc.box.z1 - 3, DIR_EAST, depth);
            break;
        case DIR_WEST:
            if (endSel <= 1)
                generateAndAddPiece(pc.box.x0 - 1, randY, pc.box.z0, pc.dir, depth);
            else if (endSel == 2)
                generateAndAddPiece(pc.box.x0, randY, pc.box.z0 - 1, DIR_NORTH, depth);
            else
                generateAndAddPiece(pc.box.x0, randY, pc.box.z1 + 1, DIR_SOUTH, depth);
            break;
        default: // EAST
            if (endSel <= 1)
                generateAndAddPiece(pc.box.x1 + 1, randY, pc.box.z0, pc.dir, depth);
            else if (endSel == 2)
                generateAndAddPiece(pc.box.x1 - 3, randY, pc.box.z0 - 1, DIR_NORTH, depth);
            else
                generateAndAddPiece(pc.box.x1 - 3, randY, pc.box.z1 + 1, DIR_SOUTH, depth);
            break;
        }

        if (depth < 8)
        {
            if (pc.dir != DIR_NORTH && pc.dir != DIR_SOUTH)
            {
                for (int x = pc.box.x0 + 3; x + 3 <= pc.box.x1; x += 5)
                {
                    const int sel = rng.nextInt(5);
                    if (sel == 0)
                        generateAndAddPiece(x, pc.box.y0, pc.box.z0 - 1, DIR_NORTH, depth + 1);
                    else if (sel == 1)
                        generateAndAddPiece(x, pc.box.y0, pc.box.z1 + 1, DIR_SOUTH, depth + 1);
                }
            }
            else
            {
                for (int z = pc.box.z0 + 3; z + 3 <= pc.box.z1; z += 5)
                {
                    const int sel = rng.nextInt(5);
                    if (sel == 0)
                        generateAndAddPiece(pc.box.x0 - 1, pc.box.y0, z, DIR_WEST, depth + 1);
                    else if (sel == 1)
                        generateAndAddPiece(pc.box.x1 + 1, pc.box.y0, z, DIR_EAST, depth + 1);
                }
            }
        }
    }

    void crossingChildren(Piece &pc)
    {
        const int depth = pc.depth;
        switch (pc.faceDir)
        {
        case DIR_NORTH:
            generateAndAddPiece(pc.box.x0 + 1, pc.box.y0, pc.box.z0 - 1, DIR_NORTH, depth);
            generateAndAddPiece(pc.box.x0 - 1, pc.box.y0, pc.box.z0 + 1, DIR_WEST, depth);
            generateAndAddPiece(pc.box.x1 + 1, pc.box.y0, pc.box.z0 + 1, DIR_EAST, depth);
            break;
        case DIR_SOUTH:
            generateAndAddPiece(pc.box.x0 + 1, pc.box.y0, pc.box.z1 + 1, DIR_SOUTH, depth);
            generateAndAddPiece(pc.box.x0 - 1, pc.box.y0, pc.box.z0 + 1, DIR_WEST, depth);
            generateAndAddPiece(pc.box.x1 + 1, pc.box.y0, pc.box.z0 + 1, DIR_EAST, depth);
            break;
        case DIR_WEST:
            generateAndAddPiece(pc.box.x0 + 1, pc.box.y0, pc.box.z0 - 1, DIR_NORTH, depth);
            generateAndAddPiece(pc.box.x0 + 1, pc.box.y0, pc.box.z1 + 1, DIR_SOUTH, depth);
            generateAndAddPiece(pc.box.x0 - 1, pc.box.y0, pc.box.z0 + 1, DIR_WEST, depth);
            break;
        case DIR_EAST:
            generateAndAddPiece(pc.box.x0 + 1, pc.box.y0, pc.box.z0 - 1, DIR_NORTH, depth);
            generateAndAddPiece(pc.box.x0 + 1, pc.box.y0, pc.box.z1 + 1, DIR_SOUTH, depth);
            generateAndAddPiece(pc.box.x1 + 1, pc.box.y0, pc.box.z0 + 1, DIR_EAST, depth);
            break;
        default:
            break;
        }

        if (pc.twoFloors)
        {
            if (rng.nextBoolean())
                generateAndAddPiece(pc.box.x0 + 1, pc.box.y0 + 4, pc.box.z0 - 1, DIR_NORTH, depth);
            if (rng.nextBoolean())
                generateAndAddPiece(pc.box.x0 - 1, pc.box.y0 + 4, pc.box.z0 + 1, DIR_WEST, depth);
            if (rng.nextBoolean())
                generateAndAddPiece(pc.box.x1 + 1, pc.box.y0 + 4, pc.box.z0 + 1, DIR_EAST, depth);
            if (rng.nextBoolean())
                generateAndAddPiece(pc.box.x0 + 1, pc.box.y0 + 4, pc.box.z1 + 1, DIR_SOUTH, depth);
        }
    }

    void stairsChildren(Piece &pc)
    {
        const int depth = pc.depth;
        switch (pc.dir)
        {
        case DIR_NORTH:
            generateAndAddPiece(pc.box.x0, pc.box.y0, pc.box.z0 - 1, DIR_NORTH, depth);
            break;
        case DIR_SOUTH:
            generateAndAddPiece(pc.box.x0, pc.box.y0, pc.box.z1 + 1, DIR_SOUTH, depth);
            break;
        case DIR_WEST:
            generateAndAddPiece(pc.box.x0 - 1, pc.box.y0, pc.box.z0, DIR_WEST, depth);
            break;
        case DIR_EAST:
            generateAndAddPiece(pc.box.x1 + 1, pc.box.y0, pc.box.z0, DIR_EAST, depth);
            break;
        default:
            break;
        }
    }

    /** 原版 StructurePiecesBuilder.shiftInto */
    int shiftInto(int seaLevel, int minY, int range)
    {
        const int target = seaLevel - range;
        const Box box = boundingBox();
        int h = box.ySpan() + minY + 1;
        if (h < target)
            h += rng.nextInt(target - h);
        return h - box.y1;
    }

    /** 原版 StructurePiecesBuilder.shift */
    void shift(int dy)
    {
        if (dy == 0)
            return;
        for (Piece &p : pieces)
        {
            p.box.move(0, dy, 0);
            for (Box &e : p.entrances)
                e.move(0, dy, 0);
        }
        roomBox.move(0, dy, 0);
    }

    Box boundingBox() const
    {
        Box b;
        int x0 = 1 << 30, y0 = 1 << 30, z0 = 1 << 30;
        int x1 = -(1 << 30), y1 = -(1 << 30), z1 = -(1 << 30);
        for (const Piece &p : pieces)
        {
            x0 = std::min(x0, p.box.x0); y0 = std::min(y0, p.box.y0); z0 = std::min(z0, p.box.z0);
            x1 = std::max(x1, p.box.x1); y1 = std::max(y1, p.box.y1); z1 = std::max(z1, p.box.z1);
        }
        b = {x0, y0, z0, x1, y1, z1};
        return b;
    }

    /** 布局总入口：返回整体下移量 */
    bool generate(int chunkX, int chunkZ, bool mesa)
    {
        rng.setLargeFeatureSeed(qint64(seedValue), chunkX, chunkZ);
        rng.nextDouble();   // 原版 findGenerationPoint 开头空掷的几率判定

        startX = chunkX * 16;
        startZ = chunkZ * 16;

        Piece room;
        room.type = T_ROOM;
        room.depth = 0;
        room.dir = DIR_NONE;
        room.box.x0 = startX + 2;
        room.box.y0 = 50;
        room.box.z0 = startZ + 2;
        room.box.x1 = room.box.x0 + 7 + rng.nextInt(6);
        room.box.y1 = 54 + rng.nextInt(6);
        room.box.z1 = room.box.z0 + 7 + rng.nextInt(6);
        roomBox = room.box;
        Piece &added = addPiece(room);
        roomChildren(added);

        if (mesa)
        {
            // 恶地变体的下移量取决于当地地表高度；预览没有地形数据，
            // 按「地表就在海平面附近」取整（只影响整座矿井的绝对高度）。
            const Box box = boundingBox();
            const int centerY = (box.y0 + box.y1) / 2;
            shift(seaLevel - centerY);
            return true;
        }

        const int dy = shiftInto(seaLevel, minY, 10);
        shift(dy);
        return true;
    }

    quint64 seedValue = 0;
};

// ────────────────────────────────────────────────────────────────────────────
// 逐块摆放（原版各段 postProcess）
// ────────────────────────────────────────────────────────────────────────────

void placeRoom(Placer &pl)
{
    const Box bb = pl.pc.box;

    pl.generateBox(bb.x0, bb.y0 + 1, bb.z0, bb.x1, std::min(bb.y0 + 3, bb.y1), bb.z1,
                   kAir, kAir, false);

    for (const Box &e : pl.pc.entrances)
    {
        pl.generateBox(e.x0, e.y1 - 2, e.z0, e.x1, e.y1, e.z1, kAir, kAir, false);
    }

    pl.generateEllipsoid(bb.x0, bb.y0 + 4, bb.z0, bb.x1, bb.y1, bb.z1, kAir, false);
}

void placeCorridor(Placer &pl)
{
    Piece &pc = pl.pc;
    const int len = pc.sections * 5 - 1;

    pl.generateBox(0, 0, 0, 2, 1, len, kAir, kAir, false);
    pl.generateMaybeBox(0.8f, 0, 2, 0, 2, 2, len, kAir, kAir, false, false);
    if (pc.spider)
        pl.generateMaybeBox(0.6f, 0, 0, 0, 2, 1, len, kCobweb, kAir, false, true);

    for (int s = 0; s < pc.sections; ++s)
    {
        const int z = 2 + s * 5;
        pl.placeSupport(0, 0, z, 2, 2);
        pl.maybePlaceCobweb(0.1f, 0, 2, z - 1);
        pl.maybePlaceCobweb(0.1f, 2, 2, z - 1);
        pl.maybePlaceCobweb(0.1f, 0, 2, z + 1);
        pl.maybePlaceCobweb(0.1f, 2, 2, z + 1);
        pl.maybePlaceCobweb(0.05f, 0, 2, z - 2);
        pl.maybePlaceCobweb(0.05f, 2, 2, z - 2);
        pl.maybePlaceCobweb(0.05f, 0, 2, z + 2);
        pl.maybePlaceCobweb(0.05f, 2, 2, z + 2);

        if (pl.rng.nextInt(100) == 0)
            pl.createChest(2, 0, z - 1);
        if (pl.rng.nextInt(100) == 0)
            pl.createChest(0, 0, z + 1);

        if (pc.spider && !pc.spiderPlaced)
        {
            const int spawnerZ = z - 1 + pl.rng.nextInt(3);
            const int sx = pl.wx(1, spawnerZ);
            const int sy = pl.wy(0);
            const int sz = pl.wz(1, spawnerZ);
            if (pl.clip.contains(sx, sy, sz) && pl.isInterior(1, 0, spawnerZ))
            {
                pc.spiderPlaced = true;
                pl.grid.set(sx, sy, sz, kSpawner);
            }
        }
    }

    for (int x = 0; x <= 2; ++x)
        for (int z = 0; z <= len; ++z)
            pl.placePlanksBlock(x, -1, z);

    pl.placeDoubleLowerOrUpperSupport(0, -1, 2);
    if (pc.sections > 1)
        pl.placeDoubleLowerOrUpperSupport(0, -1, len - 2);

    if (pc.hasRails)
    {
        for (int z = 0; z <= len; ++z)
        {
            const QString floor = pl.getBlock(1, -1, z);
            if (!isAirState(floor) && isFaceSturdy(floor, DIR_UP))
            {
                const float prob = pl.isInterior(1, 0, z) ? 0.7f : 0.9f;
                if (pl.rng.nextFloat() < prob)
                    pl.placeBlock(railState(true, pc.dir), 1, 0, z);
            }
        }
    }
}

void placeCrossing(Placer &pl)
{
    const Box bb = pl.pc.box;

    if (pl.pc.twoFloors)
    {
        pl.generateBox(bb.x0 + 1, bb.y0, bb.z0, bb.x1 - 1, bb.y0 + 2, bb.z1, kAir, kAir, false);
        pl.generateBox(bb.x0, bb.y0, bb.z0 + 1, bb.x1, bb.y0 + 2, bb.z1 - 1, kAir, kAir, false);
        pl.generateBox(bb.x0 + 1, bb.y1 - 2, bb.z0, bb.x1 - 1, bb.y1, bb.z1, kAir, kAir, false);
        pl.generateBox(bb.x0, bb.y1 - 2, bb.z0 + 1, bb.x1, bb.y1, bb.z1 - 1, kAir, kAir, false);
        pl.generateBox(bb.x0 + 1, bb.y0 + 3, bb.z0 + 1, bb.x1 - 1, bb.y0 + 3, bb.z1 - 1,
                       kAir, kAir, false);
    }
    else
    {
        pl.generateBox(bb.x0 + 1, bb.y0, bb.z0, bb.x1 - 1, bb.y1, bb.z1, kAir, kAir, false);
        pl.generateBox(bb.x0, bb.y0, bb.z0 + 1, bb.x1, bb.y1, bb.z1 - 1, kAir, kAir, false);
    }

    const int corners[4][2] = {
        {bb.x0 + 1, bb.z0 + 1}, {bb.x0 + 1, bb.z1 - 1},
        {bb.x1 - 1, bb.z0 + 1}, {bb.x1 - 1, bb.z1 - 1}
    };
    for (const auto &c : corners)
    {
        const int x = c[0], z = c[1];
        if (!isAirState(pl.grid.at(x, bb.y1 + 1, z)))
            pl.generateBox(x, bb.y0, z, x, bb.y1, z, pl.style.planks, kAir, false);
    }

    for (int x = bb.x0; x <= bb.x1; ++x)
        for (int z = bb.z0; z <= bb.z1; ++z)
            pl.placePlanksBlock(x, bb.y0 - 1, z);
}

void placeStairs(Placer &pl)
{
    pl.generateBox(0, 5, 0, 2, 7, 1, kAir, kAir, false);
    pl.generateBox(0, 0, 7, 2, 2, 8, kAir, kAir, false);
    for (int i = 0; i < 5; ++i)
    {
        const int y0 = 5 - i - (i < 4 ? 1 : 0);
        const int y1 = 7 - i;
        const int z = 2 + i;
        pl.generateBox(0, y0, z, 2, y1, z, kAir, kAir, false);
    }
}

} // namespace

// ────────────────────────────────────────────────────────────────────────────
// 对外入口
// ────────────────────────────────────────────────────────────────────────────

Model build(quint64 seed, int startChunkX, int startChunkZ,
            const QString &mcVersion, int placementStep, int placementIndex,
            int worldMinY, int seaLevel, bool mesa)
{
    Model model;
    model.startX = startChunkX * 16;
    model.startZ = startChunkZ * 16;

    if (StructureFinder::versionToInt(mcVersion) < MC_1_13)
    {
        model.error = QStringLiteral("该版本的矿井生成规则不同");
        return model;
    }

    Gen gen;
    gen.seedValue = seed;
    gen.minY = worldMinY;
    gen.seaLevel = seaLevel;
    gen.style = mesa ? kStyleMesa : kStyleNormal;
    if (!gen.generate(startChunkX, startChunkZ, mesa))
    {
        model.error = QStringLiteral("矿井布局生成失败");
        return model;
    }
    if (gen.pieces.empty())
    {
        model.error = QStringLiteral("矿井没有生成任何段");
        return model;
    }

    Grid grid;
    grid.originX = model.startX;
    grid.originZ = model.startZ;

    // 整体范围（决定要遍历哪些区块）
    const Box total = gen.boundingBox();
    const int cx0 = (total.x0) >> 4, cx1 = (total.x1) >> 4;
    const int cz0 = (total.z0) >> 4, cz1 = (total.z1) >> 4;

    for (int cz = cz0; cz <= cz1; ++cz)
    {
        for (int cx = cx0; cx <= cx1; ++cx)
        {
            const int minBlockX = cx * 16;
            const int minBlockZ = cz * 16;

            XoroshiroRng seedRng;
            const qint64 base = seedRng.setDecorationSeed(qint64(seed), minBlockX, minBlockZ);
            XoroshiroRng rng;
            rng.setFeatureSeed(base, placementIndex, placementStep);

            Box clip;
            clip.x0 = minBlockX;
            clip.y0 = worldMinY + 1;
            clip.z0 = minBlockZ;
            clip.x1 = minBlockX + 15;
            clip.y1 = 320;
            clip.z1 = minBlockZ + 15;

            for (Piece &pc : gen.pieces)
            {
                if (!pc.box.intersects(clip))
                    continue;
                Placer pl{grid, pc, rng, clip, gen.style, worldMinY, 320};
                switch (pc.type)
                {
                case T_ROOM:     placeRoom(pl); break;
                case T_CORRIDOR: placeCorridor(pl); break;
                case T_CROSSING: placeCrossing(pl); break;
                default:         placeStairs(pl); break;
                }
            }
        }
    }

    // 只导出矿井本身的方块（地形默认石头与挖空标记不进模型）
    // 栏杆放好后原版会跑一次方块更新：相邻的栏杆与实心方块会把连接边打开。
    // 这里补同一步，让栏杆四向连接与游戏内一致。
    for (auto it = grid.cells.begin(); it != grid.cells.end(); ++it)
    {
        if (!it.value().startsWith(QLatin1String("minecraft:oak_fence["))
            && !it.value().startsWith(QLatin1String("minecraft:dark_oak_fence[")))
            continue;
        int rx, y, rz;
        unpackKey(it.key(), rx, y, rz);
        const int wx = rx + model.startX;
        const int wz = rz + model.startZ;
        auto linked = [&](int dx, int dz) {
            const QString n = grid.at(wx + dx, y, wz + dz);
            return n.contains(QLatin1String("fence")) || isFaceSturdy(n, DIR_UP);
        };
        const Style &fs = it.value().startsWith(QLatin1String("minecraft:dark_oak"))
                             ? kStyleMesa : kStyleNormal;
        it.value() = fenceState(fs, linked(0, -1), linked(1, 0),
                                linked(0, 1), linked(-1, 0));
    }

    int x0 = 1 << 30, y0 = 1 << 30, z0 = 1 << 30;
    int x1 = -(1 << 30), y1 = -(1 << 30), z1 = -(1 << 30);
    for (auto it = grid.cells.constBegin(); it != grid.cells.constEnd(); ++it)
    {
        if (isAirState(it.value()) || it.value() == kStone)
            continue;
        int rx, y, rz;
        unpackKey(it.key(), rx, y, rz);
        model.blocks.insert(it.key(), it.value());
        x0 = std::min(x0, rx); y0 = std::min(y0, y);
        z0 = std::min(z0, rz);
        x1 = std::max(x1, rx); y1 = std::max(y1, y);
        z1 = std::max(z1, rz);
    }
    if (model.blocks.isEmpty())
    {
        model.error = QStringLiteral("矿井模型为空");
        return model;
    }

    model.x0 = x0; model.y0 = y0; model.z0 = z0;
    model.x1 = x1; model.y1 = y1; model.z1 = z1;
    model.blockCount = model.blocks.size();
    model.variant = mesa ? QStringLiteral("废弃矿井（恶地）") : QStringLiteral("废弃矿井");

    for (const Piece &p : gen.pieces)
    {
        PieceBox pb;
        pb.type = QString::fromLatin1(typeName(p.type));
        pb.x0 = p.box.x0 - model.startX;
        pb.y0 = p.box.y0;
        pb.z0 = p.box.z0 - model.startZ;
        pb.x1 = p.box.x1 - model.startX;
        pb.y1 = p.box.y1;
        pb.z1 = p.box.z1 - model.startZ;
        model.pieces.push_back(pb);
    }

    model.ok = true;
    return model;
}

} // namespace MineshaftModel
