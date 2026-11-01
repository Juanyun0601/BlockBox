/**
 * @file   StrongholdModel.cpp
 * @brief  按原版要塞拼装规则（StrongholdPieces，1.21.4 校对）逐块重建要塞
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 对照的 Java 原版流程：
 *   - StrongholdStructure.generatePieces：循环换种子直到抽出末地传送门房间；
 *   - StrongholdPieces：按权重表选房间、碰撞检测、待处理子段随机出队；
 *   - StructurePiecesBuilder.moveBelowSeaLevel：整体下移到海平面以下；
 *   - ChunkGenerator.applyBiomeDecoration：逐区块用「装饰种子 + 结构步」
 *     重置随机源，石砖的裂纹/苔藓/虫蚀变体由此决定（逐区块不同）。
 *
 * 每个房间的 postProcess 都按原版坐标（局部坐标 → 朝向映射）逐格摆放，
 * 只在当前区块范围内落块，因此跨区块的段会和游戏里一样被区块边界切开。
 */
#include "StrongholdModel.h"

#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cstdlib>

namespace StrongholdModel {
namespace {

// ────────────────────────────────────────────────────────────────────────────
// 方块状态串
// ────────────────────────────────────────────────────────────────────────────

QString st(const char *s) { return QString::fromLatin1(s); }

const QString kStoneBricks = QStringLiteral("minecraft:stone_bricks");
const QString kCracked = QStringLiteral("minecraft:cracked_stone_bricks");
const QString kMossy = QStringLiteral("minecraft:mossy_stone_bricks");
const QString kInfested = QStringLiteral("minecraft:infested_stone_bricks");
const QString kCobble = QStringLiteral("minecraft:cobblestone");
const QString kPlanks = QStringLiteral("minecraft:oak_planks");
const QString kBookshelf = QStringLiteral("minecraft:bookshelf");
const QString kCaveAir = QStringLiteral("minecraft:cave_air");
const QString kIronBarsB = QStringLiteral("minecraft:iron_bars");

/** 方块状态：name[prop=value,...] */
QString prop(const QString &name, const QString &props)
{
    return props.isEmpty() ? name : name + QLatin1Char('[') + props + QLatin1Char(']');
}

/** 楼梯默认状态：下半、直形 */
QString stairs(const QString &name, const char *facing)
{
    return prop(name, QStringLiteral("facing=") + st(facing)
                          + QStringLiteral(",half=bottom,shape=straight"));
}

/** 平滑石台阶（下半） */
QString slab() { return QStringLiteral("minecraft:smooth_stone_slab[type=bottom]"); }
/** 平滑石台阶（双） */
QString doubleSlab() { return QStringLiteral("minecraft:smooth_stone_slab[type=double]"); }

/** 墙上的火把 */
QString wallTorch(const char *facing)
{
    return prop(QStringLiteral("minecraft:wall_torch"),
                QStringLiteral("facing=") + st(facing));
}

/** 铁栏杆：按原版 north/east/south/west 四向连接 */
QString bars(bool north, bool east, bool south, bool west)
{
    QStringList parts;
    parts << QStringLiteral("east=") + (east ? QStringLiteral("true") : QStringLiteral("false"));
    parts << QStringLiteral("north=") + (north ? QStringLiteral("true") : QStringLiteral("false"));
    parts << QStringLiteral("south=") + (south ? QStringLiteral("true") : QStringLiteral("false"));
    parts << QStringLiteral("west=") + (west ? QStringLiteral("true") : QStringLiteral("false"));
    return kIronBarsB + QLatin1Char('[') + parts.join(QLatin1Char(',')) + QLatin1Char(']');
}

/** 橡木栅栏：按原版四向连接 */
QString fence(bool north, bool east, bool south, bool west)
{
    QStringList parts;
    parts << QStringLiteral("east=") + (east ? QStringLiteral("true") : QStringLiteral("false"));
    parts << QStringLiteral("north=") + (north ? QStringLiteral("true") : QStringLiteral("false"));
    parts << QStringLiteral("south=") + (south ? QStringLiteral("true") : QStringLiteral("false"));
    parts << QStringLiteral("west=") + (west ? QStringLiteral("true") : QStringLiteral("false"));
    return QStringLiteral("minecraft:oak_fence[") + parts.join(QLatin1Char(','))
        + QLatin1Char(']');
}

QString doorState(bool iron, const char *facing, bool upper)
{
    QString props = QStringLiteral("facing=") + st(facing)
        + (upper ? QStringLiteral(",half=upper") : QStringLiteral(",half=lower"));
    return prop(iron ? QStringLiteral("minecraft:iron_door")
                     : QStringLiteral("minecraft:oak_door"),
                props);
}

QString ladderState(const char *facing)
{
    return prop(QStringLiteral("minecraft:ladder"),
                QStringLiteral("facing=") + st(facing));
}

QString frameState(const char *facing, bool eye)
{
    return prop(QStringLiteral("minecraft:end_portal_frame"),
                QStringLiteral("eye=") + (eye ? QStringLiteral("true") : QStringLiteral("false"))
                    + QStringLiteral(",facing=") + st(facing));
}

QString chestState(const char *facing)
{
    return prop(QStringLiteral("minecraft:chest"),
                QStringLiteral("facing=") + st(facing));
}

// ────────────────────────────────────────────────────────────────────────────
// 坐标与包围盒
// ────────────────────────────────────────────────────────────────────────────

enum Dir
{
    North = 0,
    East = 1,
    South = 2,
    West = 3
};

struct Box
{
    int x0 = 0, y0 = 0, z0 = 0;
    int x1 = -1, y1 = -1, z1 = -1;

    bool valid() const { return x1 >= x0 && y1 >= y0 && z1 >= z0; }
    int yspan() const { return y1 - y0 + 1; }
    bool contains(int x, int y, int z) const
    {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1;
    }
    bool intersects(const Box &o) const
    {
        return x1 >= o.x0 && x0 <= o.x1 && z1 >= o.z0 && z0 <= o.z1
            && y1 >= o.y0 && y0 <= o.y1;
    }
    void move(int dx, int dy, int dz)
    {
        x0 += dx; x1 += dx;
        y0 += dy; y1 += dy;
        z0 += dz; z1 += dz;
    }
};

/** BoundingBox.orientBox */
Box orientBox(int x, int y, int z, int ox, int oy, int oz,
              int sx, int sy, int sz, int dir)
{
    Box b;
    switch (dir)
    {
    case North:
        b.x0 = x + ox; b.y0 = y + oy; b.z0 = z - sz + 1 + oz;
        b.x1 = x + sx - 1 + ox; b.y1 = y + sy - 1 + oy; b.z1 = z + oz;
        break;
    case West:
        b.x0 = x - sz + 1 + oz; b.y0 = y + oy; b.z0 = z + ox;
        b.x1 = x + oz; b.y1 = y + sy - 1 + oy; b.z1 = z + sx - 1 + ox;
        break;
    case East:
        b.x0 = x + oz; b.y0 = y + oy; b.z0 = z + ox;
        b.x1 = x + sz - 1 + oz; b.y1 = y + sy - 1 + oy; b.z1 = z + sx - 1 + ox;
        break;
    case South:
    default:
        b.x0 = x + ox; b.y0 = y + oy; b.z0 = z + oz;
        b.x1 = x + sx - 1 + ox; b.y1 = y + sy - 1 + oy; b.z1 = z + sz - 1 + oz;
        break;
    }
    return b;
}

/** StructurePiece.makeBoundingBox */
Box makeBox(int x, int y, int z, int sx, int sy, int sz, int dir)
{
    Box b;
    b.x0 = x; b.y0 = y; b.z0 = z;
    if (dir == North || dir == South)
    {
        b.x1 = x + sx - 1; b.y1 = y + sy - 1; b.z1 = z + sz - 1;
    }
    else
    {
        b.x1 = x + sz - 1; b.y1 = y + sy - 1; b.z1 = z + sx - 1;
    }
    return b;
}

// ────────────────────────────────────────────────────────────────────────────
// 随机源
// ────────────────────────────────────────────────────────────────────────────

constexpr quint64 kModulusMask = (1ULL << 48) - 1;
constexpr quint64 kMultiplier = 0x5DEECE66DULL;
constexpr quint64 kIncrement = 0xBULL;

/** 原版 LegacyRandomSource（java.util.Random 的 48 位 LCG） */
struct LRng
{
    quint64 state = 0;

    void setSeed(qint64 seed)
    {
        state = (quint64(seed) ^ kMultiplier) & kModulusMask;
    }
    int next(int bits)
    {
        state = (state * kMultiplier + kIncrement) & kModulusMask;
        return int(state >> (48 - bits));
    }
    int nextInt(int bound)
    {
        if (bound <= 0)
            return 0;
        if ((bound & (bound - 1)) == 0)
            return int((qint64(bound) * qint64(next(31))) >> 31);
        for (;;)
        {
            const int bits = next(31);
            const int val = bits % bound;
            if (bits - val + (bound - 1) >= 0)
                return val;
        }
    }
    qint64 nextLong()
    {
        const qint64 hi = qint64(qint32(next(32)));
        const qint64 lo = qint64(qint32(next(32)));
        return qint64(quint64(hi) << 32) + lo;
    }
    bool nextBoolean() { return next(1) != 0; }
    float nextFloat() { return float(next(24)) * 5.9604645e-8f; }

    void setLargeFeatureSeed(qint64 seed, int cx, int cz)
    {
        setSeed(seed);
        const qint64 a = nextLong();
        const qint64 b = nextLong();
        setSeed(qint64(quint64(cx) * quint64(a) ^ quint64(cz) * quint64(b)
                       ^ quint64(seed)));
    }
};

/** 1.18+ XoroshiroRandomSource（逐区块装饰随机源） */
struct XRng
{
    quint64 s0 = 0;
    quint64 s1 = 0;

    static quint64 mix13(quint64 v)
    {
        // RandomSupport.mixStafford13
        v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
        v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
        return v ^ (v >> 31);
    }
    static quint64 rotl(quint64 x, int k) { return (x << k) | (x >> (64 - k)); }

    void setSeed(qint64 seed)
    {
        // XoroshiroRandomSource.setSeed → RandomSupport.c(seed)：
        // lo = seed ^ SILVER_RATIO_64, hi = lo + GOLDEN_RATIO_64，再各自 mixStafford13
        const quint64 lo = quint64(seed) ^ 0x6A09E667F3BCC909ULL;
        const quint64 hi = lo + 0x9E3779B97F4A7C15ULL;
        s0 = mix13(lo);
        s1 = mix13(hi);
    }
    quint64 nextLong()
    {
        // Xoroshiro128++（与 Minecraft Xoroshiro128PlusPlus.nextLong 一致）
        const quint64 a = s0;
        quint64 b = s1;
        const quint64 result = rotl(a + b, 17) + a;
        b ^= a;
        s0 = rotl(a, 49) ^ b ^ (b << 21);
        s1 = rotl(b, 28);
        return result;
    }
    /** WorldgenRandom.next(bits) */
    int next(int bits) { return int(quint32(nextLong() >> (64 - bits))); }
    /** WorldgenRandom.nextLong()：BitRandomSource 默认实现（两次 next(32)） */
    qint64 nextLongBits()
    {
        const quint64 hi = quint64(quint32(next(32)));
        const quint64 lo = quint64(quint32(next(32)));
        return qint64((hi << 32) | lo);
    }
    int nextInt(int bound)
    {
        if (bound <= 0)
            return 0;
        if ((bound & (bound - 1)) == 0)
            return int((qint64(bound) * qint64(next(31))) >> 31);
        for (;;)
        {
            const int bits = next(31);
            const int val = bits % bound;
            if (bits - val + (bound - 1) >= 0)
                return val;
        }
    }
    float nextFloat() { return float(next(24)) * 5.9604645e-8f; }

    /** WorldgenRandom.setDecorationSeed */
    qint64 setDecorationSeed(qint64 seed, int bx, int bz)
    {
        setSeed(seed);
        const qint64 a = nextLongBits() | 1;
        const qint64 b = nextLongBits() | 1;
        const qint64 k = qint64(quint64(qint64(bx) * quint64(a))
                                + quint64(qint64(bz) * quint64(b)))
            ^ seed;
        setSeed(k);
        return k;
    }
    void setFeatureSeed(qint64 decorationSeed, int index, int step)
    {
        setSeed(decorationSeed + qint64(index) + qint64(10000 * step));
    }
};

// ────────────────────────────────────────────────────────────────────────────
// 段
// ────────────────────────────────────────────────────────────────────────────

enum class PType
{
    Start,
    Straight,
    PrisonHall,
    LeftTurn,
    RightTurn,
    RoomCrossing,
    StraightStairsDown,
    StairsDown,
    FiveCrossing,
    ChestCorridor,
    Library,
    PortalRoom,
    FillerCorridor
};

const char *pieceName(PType t)
{
    switch (t)
    {
    case PType::Start: return "start";
    case PType::Straight: return "straight";
    case PType::PrisonHall: return "prison_hall";
    case PType::LeftTurn: return "left_turn";
    case PType::RightTurn: return "right_turn";
    case PType::RoomCrossing: return "room_crossing";
    case PType::StraightStairsDown: return "straight_stairs_down";
    case PType::StairsDown: return "stairs_down";
    case PType::FiveCrossing: return "five_crossing";
    case PType::ChestCorridor: return "chest_corridor";
    case PType::Library: return "library";
    case PType::PortalRoom: return "portal_room";
    case PType::FillerCorridor: return "filler_corridor";
    }
    return "?";
}

struct Piece
{
    PType type = PType::Straight;
    int depth = 0;
    Box box;
    int dir = South;
    int door = 0;   ///< 进口门类型（SmallDoorType）

    bool leftChild = false;
    bool rightChild = false;
    bool leftLow = false;
    bool leftHigh = false;
    bool rightLow = false;
    bool rightHigh = false;
    bool isTall = false;
    bool isSource = false;
    int roomType = 0;
    int steps = 0;
};

/** 房间权重（原版 StrongholdPieces.g） */
struct PieceWeight
{
    PType type;
    int weight;
    int maxPlace;
    int placeCount = 0;
};

/**
 * @brief 要塞生成器（布局 + 逐区块落块）
 */
class Gen
{
public:
    Gen(quint64 seed, int startChunkX, int startChunkZ, int seaLevel, int minY)
        : m_seed(seed), m_cx(startChunkX), m_cz(startChunkZ),
          m_seaLevel(seaLevel), m_minY(minY)
    {
        m_startX = startChunkX * 16 + 2;
        m_startZ = startChunkZ * 16 + 2;
    }

    bool run(Model &out);

private:
    // ── 布局 ──
    void resetPieces();
    bool updatePieceWeight();
    Piece *createPiece(int type, LRng &rng, int x, int y, int z, int dir, int depth);
    Piece *nextPiece(LRng &rng, int depth, int x, int y, int z, int dir);
    Piece *addChild(LRng &rng, int depth, int x, int y, int z, int dir);
    void addChildren(int pieceIndex, LRng &rng);
    Piece *forwardChild(const Piece &p, LRng &rng, int x, int y);
    Piece *leftChild(const Piece &p, LRng &rng, int x, int y);
    Piece *rightChild(const Piece &p, LRng &rng, int x, int y);
    bool findCollision(const Box &b) const;
    Box fillerBox(int x, int y, int z, int dir) const;

    // ── 逐块放置 ──
    void postProcess(Piece &p, const Box &chunk);
    void sel(Piece &p, int x0, int y0, int z0, int x1, int y1, int z1, bool checkAir);
    void box(Piece &p, int x0, int y0, int z0, int x1, int y1, int z1,
             const QString &edge, const QString &fill, bool checkAir);
    void airBox(Piece &p, int x0, int y0, int z0, int x1, int y1, int z1);
    void maybeBox(Piece &p, float chance, int x0, int y0, int z0, int x1, int y1,
                  int z1, const QString &edge, const QString &fill, bool checkAir);
    void maybeBlock(Piece &p, float chance, int x, int y, int z, const QString &state);
    void smallDoor(Piece &p, int type, int x, int y, int z);
    void chest(Piece &p, int x, int y, int z);
    void put(Piece &p, int x, int y, int z, const QString &state);
    bool occupied(Piece &p, int x, int y, int z) const;

    int wx(const Piece &p, int x, int z) const
    {
        switch (p.dir)
        {
        case West: return p.box.x1 - z;
        case East: return p.box.x0 + z;
        default: return p.box.x0 + x;
        }
    }
    int wz(const Piece &p, int x, int z) const
    {
        switch (p.dir)
        {
        case North: return p.box.z1 - z;
        case West:
        case East: return p.box.z0 + x;
        default: return p.box.z0 + z;
        }
    }
    int wy(const Piece &p, int y) const { return p.box.y0 + y; }

    static qint64 key(int relX, int y, int relZ)
    {
        return qint64((quint64(relX + 2048) & 0xFFF)
                      | ((quint64(relZ + 2048) & 0xFFF) << 12)
                      | ((quint64(y + 2048) & 0xFFF) << 24));
    }

    quint64 m_seed;
    int m_cx, m_cz;
    int m_seaLevel;
    int m_minY;
    int m_startX = 0;
    int m_startZ = 0;

    QVector<Piece> m_pieces;      ///< 创建顺序（原版 PiecesContainer 顺序）
    QVector<int> m_pending;       ///< 待处理子段（存 m_pieces 下标）
    QVector<PieceWeight> m_weights;
    QVector<PieceWeight *> m_current;
    PieceWeight *m_previous = nullptr;
    PType m_imposed = PType::Straight;
    bool m_hasImposed = false;
    bool m_portalRoom = false;
    int m_totalWeight = 0;

    // 逐区块落块状态
    Box m_chunk;
    XRng m_rng;
    QHash<qint64, QString> m_blocks;  ///< 相对坐标 → 方块状态
    QSet<qint64> m_air;               ///< 被结构挖成空气的格子

    Model *m_out = nullptr;
};

// ── 布局 ────────────────────────────────────────────────────────────────────

void Gen::resetPieces()
{
    m_weights.clear();
    m_current.clear();
    const struct { PType t; int w; int m; } defs[] = {
        {PType::Straight, 40, 0},
        {PType::PrisonHall, 5, 5},
        {PType::LeftTurn, 20, 0},
        {PType::RightTurn, 20, 0},
        {PType::RoomCrossing, 10, 6},
        {PType::StraightStairsDown, 5, 5},
        {PType::StairsDown, 5, 5},
        {PType::FiveCrossing, 5, 4},
        {PType::ChestCorridor, 5, 4},
        {PType::Library, 10, 2},
        {PType::PortalRoom, 20, 1},
    };
    for (const auto &d : defs)
        m_weights.append(PieceWeight{d.t, d.w, d.m, 0});
    for (PieceWeight &w : m_weights)
        m_current.append(&w);
    m_previous = nullptr;
    m_hasImposed = false;
    m_portalRoom = false;
}

bool Gen::updatePieceWeight()
{
    bool any = false;
    m_totalWeight = 0;
    for (const PieceWeight *w : m_current)
    {
        if (w->maxPlace > 0 && w->placeCount < w->maxPlace)
            any = true;
        m_totalWeight += w->weight;
    }
    return any;
}

/** PieceWeight.doPlace（含 Library / PortalRoom 的深度限制） */
bool canPlace(const PieceWeight *w, int depth)
{
    if (w->type == PType::Library && !(depth > 4))
        return false;
    if (w->type == PType::PortalRoom && !(depth > 5))
        return false;
    return w->maxPlace == 0 || w->placeCount < w->maxPlace;
}

/** 门类型：0 OPENING / 1 WOOD_DOOR / 2 GRATES / 3 IRON_DOOR */
int randomDoorType(LRng &rng)
{
    switch (rng.nextInt(5))
    {
    case 2: return 1;
    case 3: return 2;
    case 4: return 3;
    default: return 0;
    }
}

bool okBox(const Box &b) { return b.valid() && b.y0 > 10; }

Piece *Gen::createPiece(int type, LRng &rng, int x, int y, int z, int dir, int depth)
{
    Box b;
    Piece p;
    p.type = PType(type);
    p.depth = depth;
    p.dir = dir;

    switch (p.type)
    {
    case PType::Straight:
        b = orientBox(x, y, z, -1, -1, 0, 5, 5, 7, dir);
        if (!okBox(b) || findCollision(b))
            return nullptr;
        p.door = randomDoorType(rng);
        p.leftChild = rng.nextInt(2) == 0;
        p.rightChild = rng.nextInt(2) == 0;
        break;
    case PType::ChestCorridor:
        b = orientBox(x, y, z, -1, -1, 0, 5, 5, 7, dir);
        if (!okBox(b) || findCollision(b))
            return nullptr;
        p.door = randomDoorType(rng);
        break;
    case PType::PrisonHall:
        b = orientBox(x, y, z, -1, -1, 0, 9, 5, 11, dir);
        if (!okBox(b) || findCollision(b))
            return nullptr;
        p.door = randomDoorType(rng);
        break;
    case PType::LeftTurn:
    case PType::RightTurn:
        b = orientBox(x, y, z, -1, -1, 0, 5, 5, 5, dir);
        if (!okBox(b) || findCollision(b))
            return nullptr;
        p.door = randomDoorType(rng);
        break;
    case PType::RoomCrossing:
        b = orientBox(x, y, z, -4, -1, 0, 11, 7, 11, dir);
        if (!okBox(b) || findCollision(b))
            return nullptr;
        p.door = randomDoorType(rng);
        p.roomType = rng.nextInt(5);
        break;
    case PType::StraightStairsDown:
        b = orientBox(x, y, z, -1, -7, 0, 5, 11, 8, dir);
        if (!okBox(b) || findCollision(b))
            return nullptr;
        p.door = randomDoorType(rng);
        break;
    case PType::StairsDown:
        b = orientBox(x, y, z, -1, -7, 0, 5, 11, 5, dir);
        if (!okBox(b) || findCollision(b))
            return nullptr;
        p.door = randomDoorType(rng);
        break;
    case PType::FiveCrossing:
        b = orientBox(x, y, z, -4, -3, 0, 10, 9, 11, dir);
        if (!okBox(b) || findCollision(b))
            return nullptr;
        p.door = randomDoorType(rng);
        p.leftLow = rng.nextBoolean();
        p.leftHigh = rng.nextBoolean();
        p.rightLow = rng.nextBoolean();
        p.rightHigh = rng.nextInt(3) > 0;
        break;
    case PType::Library:
    {
        b = orientBox(x, y, z, -4, -1, 0, 14, 11, 15, dir);
        if (!okBox(b) || findCollision(b))
        {
            b = orientBox(x, y, z, -4, -1, 0, 14, 6, 15, dir);
            if (!okBox(b) || findCollision(b))
                return nullptr;
        }
        p.door = randomDoorType(rng);
        p.isTall = b.yspan() > 6;
        break;
    }
    case PType::PortalRoom:
        b = orientBox(x, y, z, -4, -1, 0, 11, 8, 16, dir);
        if (!okBox(b) || findCollision(b))
            return nullptr;
        break;
    case PType::Start:
    case PType::FillerCorridor:
        return nullptr;
    }

    p.box = b;
    m_pieces.append(p);
    return &m_pieces.last();
}

bool Gen::findCollision(const Box &b) const
{
    for (const Piece &p : m_pieces)
        if (p.box.intersects(b))
            return true;
    return false;
}

Box Gen::fillerBox(int x, int y, int z, int dir) const
{
    Box b = orientBox(x, y, z, -1, -1, 0, 5, 5, 4, dir);
    const Piece *hit = nullptr;
    for (const Piece &p : m_pieces)
    {
        if (p.box.intersects(b))
        {
            hit = &p;
            break;
        }
    }
    if (!hit)
        return Box();
    if (hit->box.y0 == b.y0)
    {
        for (int i = 2; i >= 1; --i)
        {
            b = orientBox(x, y, z, -1, -1, 0, 5, 5, i, dir);
            if (!hit->box.intersects(b))
                return orientBox(x, y, z, -1, -1, 0, 5, 5, i + 1, dir);
        }
    }
    return Box();
}

Piece *Gen::addChild(LRng &rng, int depth, int x, int y, int z, int dir)
{
    if (depth > 50)
        return nullptr;
    const Box &startBox = m_pieces.first().box;
    if (std::abs(x - startBox.x0) > 112 || std::abs(z - startBox.z0) > 112)
        return nullptr;
    Piece *p = nextPiece(rng, depth + 1, x, y, z, dir);
    if (p)
        m_pending.append(int(p - m_pieces.constData()));
    return p;
}

Piece *Gen::nextPiece(LRng &rng, int depth, int x, int y, int z, int dir)
{
    if (!updatePieceWeight())
        return nullptr;

    if (m_hasImposed)
    {
        Piece *p = createPiece(int(m_imposed), rng, x, y, z, dir, depth);
        m_hasImposed = false;
        if (p)
            return p;
    }

    int tries = 0;
    while (tries < 5)
    {
        ++tries;
        int k = rng.nextInt(m_totalWeight);
        for (int i = 0; i < m_current.size(); ++i)
        {
            PieceWeight *w = m_current[i];
            k -= w->weight;
            if (k >= 0)
                continue;
            if (!canPlace(w, depth) || w == m_previous)
                break;
            Piece *p = createPiece(int(w->type), rng, x, y, z, dir, depth);
            if (p)
            {
                ++w->placeCount;
                m_previous = w;
                if (w->maxPlace > 0 && w->placeCount >= w->maxPlace)
                    m_current.removeAt(i);
                return p;
            }
        }
    }

    const Box b = fillerBox(x, y, z, dir);
    if (b.valid() && b.y0 > 1)
    {
        Piece p;
        p.type = PType::FillerCorridor;
        p.depth = depth;
        p.box = b;
        p.dir = dir;
        p.steps = (dir == North || dir == South) ? (b.z1 - b.z0 + 1) : (b.x1 - b.x0 + 1);
        m_pieces.append(p);
        return &m_pieces.last();
    }
    return nullptr;
}

Piece *Gen::forwardChild(const Piece &p, LRng &rng, int x, int y)
{
    switch (p.dir)
    {
    case North: return addChild(rng, p.depth, p.box.x0 + x, p.box.y0 + y, p.box.z0 - 1, North);
    case East: return addChild(rng, p.depth, p.box.x1 + 1, p.box.y0 + y, p.box.z0 + x, East);
    case West: return addChild(rng, p.depth, p.box.x0 - 1, p.box.y0 + y, p.box.z0 + x, West);
    default: return addChild(rng, p.depth, p.box.x0 + x, p.box.y0 + y, p.box.z1 + 1, South);
    }
}

Piece *Gen::leftChild(const Piece &p, LRng &rng, int x, int y)
{
    switch (p.dir)
    {
    case North:
    case South:
        return addChild(rng, p.depth, p.box.x0 - 1, p.box.y0 + x, p.box.z0 + y, West);
    default:
        return addChild(rng, p.depth, p.box.x0 + y, p.box.y0 + x, p.box.z0 - 1, North);
    }
}

Piece *Gen::rightChild(const Piece &p, LRng &rng, int x, int y)
{
    switch (p.dir)
    {
    case North:
    case South:
        return addChild(rng, p.depth, p.box.x1 + 1, p.box.y0 + x, p.box.z0 + y, East);
    default:
        return addChild(rng, p.depth, p.box.x0 + y, p.box.y0 + x, p.box.z1 + 1, South);
    }
}

void Gen::addChildren(int pieceIndex, LRng &rng)
{
    const Piece p = m_pieces.at(pieceIndex);
    switch (p.type)
    {
    case PType::Start:
    case PType::StairsDown:
        if (p.isSource)
            m_imposed = PType::FiveCrossing, m_hasImposed = true;
        forwardChild(p, rng, 1, 1);
        break;
    case PType::Straight:
        forwardChild(p, rng, 1, 1);
        if (p.leftChild)
            leftChild(p, rng, 1, 2);
        if (p.rightChild)
            rightChild(p, rng, 1, 2);
        break;
    case PType::PrisonHall:
    case PType::ChestCorridor:
    case PType::StraightStairsDown:
        forwardChild(p, rng, 1, 1);
        break;
    case PType::LeftTurn:
        if (p.dir != North && p.dir != East)
            rightChild(p, rng, 1, 1);
        else
            leftChild(p, rng, 1, 1);
        break;
    case PType::RightTurn:
        if (p.dir != North && p.dir != East)
            leftChild(p, rng, 1, 1);
        else
            rightChild(p, rng, 1, 1);
        break;
    case PType::RoomCrossing:
        forwardChild(p, rng, 4, 1);
        leftChild(p, rng, 1, 4);
        rightChild(p, rng, 1, 4);
        break;
    case PType::FiveCrossing:
    {
        int a = 3;
        int b = 5;
        if (p.dir == West || p.dir == North)
        {
            a = 8 - a;
            b = 8 - b;
        }
        forwardChild(p, rng, 5, 1);
        if (p.leftLow)
            leftChild(p, rng, a, 1);
        if (p.leftHigh)
            leftChild(p, rng, b, 7);
        if (p.rightLow)
            rightChild(p, rng, a, 1);
        if (p.rightHigh)
            rightChild(p, rng, b, 7);
        break;
    }
    case PType::Library:
        // 原版 Library 没有子段（不重写 addChildren），这里不能继续铺路
        break;
    case PType::PortalRoom:
        m_portalRoom = true;
        m_out->hasPortalRoom = true;
        break;
    case PType::FillerCorridor:
        break;
    }
}

// ── 逐块放置：原版 StructurePiece 的几种摆放原语 ───────────────────────────

bool Gen::occupied(Piece &p, int x, int y, int z) const
{
    // getBlock：区块外的位置视为空气；其余位置只有被结构挖空的才算空气
    const int gx = wx(p, x, z);
    const int gz = wz(p, x, z);
    const int gy = wy(p, y);
    if (!m_chunk.contains(gx, gy, gz))
        return false;
    return !m_air.contains(key(gx - m_startX, gy, gz - m_startZ));
}

void Gen::put(Piece &p, int x, int y, int z, const QString &state)
{
    const int gx = wx(p, x, z);
    const int gz = wz(p, x, z);
    const int gy = wy(p, y);
    if (!m_chunk.contains(gx, gy, gz))
        return;
    const qint64 k = key(gx - m_startX, gy, gz - m_startZ);
    if (state.isEmpty() || state == kCaveAir || state == QLatin1String("minecraft:air"))
    {
        m_blocks.remove(k);
        m_air.insert(k);
    }
    else
    {
        m_air.remove(k);
        m_blocks.insert(k, state);
    }
}

void Gen::sel(Piece &p, int x0, int y0, int z0, int x1, int y1, int z1, bool checkAir)
{
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
            {
                if (checkAir && !occupied(p, x, y, z))
                    continue;
                const bool edge = (y == y0 || y == y1 || x == x0 || x == x1
                                   || z == z0 || z == z1);
                QString state;
                if (edge)
                {
                    const float f = m_rng.nextFloat();
                    if (f < 0.2f)
                        state = kCracked;
                    else if (f < 0.5f)
                        state = kMossy;
                    else if (f < 0.55f)
                        state = kInfested;
                    else
                        state = kStoneBricks;
                }
                put(p, x, y, z, state);
            }
}

void Gen::box(Piece &p, int x0, int y0, int z0, int x1, int y1, int z1,
              const QString &edge, const QString &fill, bool checkAir)
{
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
            {
                if (checkAir && !occupied(p, x, y, z))
                    continue;
                const bool onEdge = (y == y0 || y == y1 || x == x0 || x == x1
                                     || z == z0 || z == z1);
                put(p, x, y, z, onEdge ? edge : fill);
            }
}

void Gen::airBox(Piece &p, int x0, int y0, int z0, int x1, int y1, int z1)
{
    box(p, x0, y0, z0, x1, y1, z1, kCaveAir, kCaveAir, false);
}

void Gen::maybeBox(Piece &p, float chance, int x0, int y0, int z0, int x1, int y1,
                   int z1, const QString &edge, const QString &fill, bool checkAir)
{
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
            {
                const float f = m_rng.nextFloat();
                if (f > chance)
                    continue;
                if (checkAir && !occupied(p, x, y, z))
                    continue;
                const bool onEdge = (y == y0 || y == y1 || x == x0 || x == x1
                                     || z == z0 || z == z1);
                put(p, x, y, z, onEdge ? edge : fill);
            }
}

void Gen::maybeBlock(Piece &p, float chance, int x, int y, int z, const QString &state)
{
    if (m_rng.nextFloat() < chance)
        put(p, x, y, z, state);
}

void Gen::smallDoor(Piece &p, int type, int x, int y, int z)
{
    switch (type)
    {
    case 1:  // WOOD_DOOR
        put(p, x, y, z, kStoneBricks);
        put(p, x, y + 1, z, kStoneBricks);
        put(p, x, y + 2, z, kStoneBricks);
        put(p, x + 1, y + 2, z, kStoneBricks);
        put(p, x + 2, y + 2, z, kStoneBricks);
        put(p, x + 2, y + 1, z, kStoneBricks);
        put(p, x + 2, y, z, kStoneBricks);
        put(p, x + 1, y, z, doorState(false, "north", false));
        put(p, x + 1, y + 1, z, doorState(false, "north", true));
        break;
    case 2:  // GRATES
        put(p, x + 1, y, z, kCaveAir);
        put(p, x + 1, y + 1, z, kCaveAir);
        put(p, x, y, z, bars(false, false, false, true));
        put(p, x, y + 1, z, bars(false, false, false, true));
        put(p, x, y + 2, z, bars(false, true, false, true));
        put(p, x + 1, y + 2, z, bars(false, true, false, true));
        put(p, x + 2, y + 2, z, bars(false, true, false, true));
        put(p, x + 2, y + 1, z, bars(false, true, false, false));
        put(p, x + 2, y, z, bars(false, true, false, false));
        break;
    case 3:  // IRON_DOOR
        put(p, x, y, z, kStoneBricks);
        put(p, x, y + 1, z, kStoneBricks);
        put(p, x, y + 2, z, kStoneBricks);
        put(p, x + 1, y + 2, z, kStoneBricks);
        put(p, x + 2, y + 2, z, kStoneBricks);
        put(p, x + 2, y + 1, z, kStoneBricks);
        put(p, x + 2, y, z, kStoneBricks);
        put(p, x + 1, y, z, doorState(true, "north", false));
        put(p, x + 1, y + 1, z, doorState(true, "north", true));
        put(p, x + 2, y + 1, z + 1,
            prop(QStringLiteral("minecraft:stone_button"), QStringLiteral("facing=north")));
        put(p, x + 2, y + 1, z - 1,
            prop(QStringLiteral("minecraft:stone_button"), QStringLiteral("facing=south")));
        break;
    default:  // OPENING
        box(p, x, y, z, x + 2, y + 2, z, kCaveAir, kCaveAir, false);
        break;
    }
}

void Gen::chest(Piece &p, int x, int y, int z)
{
    const int gx = wx(p, x, z);
    const int gz = wz(p, x, z);
    const int gy = wy(p, y);
    if (!m_chunk.contains(gx, gy, gz))
        return;
    const qint64 k = key(gx - m_startX, gy, gz - m_startZ);
    if (m_blocks.value(k).startsWith(QLatin1String("minecraft:chest")))
        return;
    m_air.remove(k);
    m_blocks.insert(k, chestState("north"));
    m_rng.nextLongBits();  // 原版为箱子掷战利品种子
}

// ── 各房间的 postProcess（逐句对照原版） ────────────────────────────────────

void Gen::postProcess(Piece &p, const Box &chunk)
{
    m_chunk = chunk;
    switch (p.type)
    {
    case PType::Start:
    case PType::StairsDown:
    {
        sel(p, 0, 0, 0, 4, 10, 4, true);
        smallDoor(p, p.door, 1, 7, 0);
        smallDoor(p, 0, 1, 1, 4);
        put(p, 2, 6, 1, kStoneBricks);
        put(p, 1, 5, 1, kStoneBricks);
        put(p, 1, 6, 1, slab());
        put(p, 1, 5, 2, kStoneBricks);
        put(p, 1, 4, 3, kStoneBricks);
        put(p, 1, 5, 3, slab());
        put(p, 2, 4, 3, kStoneBricks);
        put(p, 3, 3, 3, kStoneBricks);
        put(p, 3, 4, 3, slab());
        put(p, 3, 3, 2, kStoneBricks);
        put(p, 3, 2, 1, kStoneBricks);
        put(p, 3, 3, 1, slab());
        put(p, 2, 2, 1, kStoneBricks);
        put(p, 1, 1, 1, kStoneBricks);
        put(p, 1, 2, 1, slab());
        put(p, 1, 1, 2, kStoneBricks);
        put(p, 1, 1, 3, slab());
        break;
    }
    case PType::Straight:
    {
        sel(p, 0, 0, 0, 4, 4, 6, true);
        smallDoor(p, p.door, 1, 1, 0);
        smallDoor(p, 0, 1, 1, 6);
        maybeBlock(p, 0.1f, 1, 2, 1, wallTorch("east"));
        maybeBlock(p, 0.1f, 3, 2, 1, wallTorch("west"));
        maybeBlock(p, 0.1f, 1, 2, 5, wallTorch("east"));
        maybeBlock(p, 0.1f, 3, 2, 5, wallTorch("west"));
        if (p.leftChild)
            airBox(p, 0, 1, 2, 0, 3, 4);
        if (p.rightChild)
            airBox(p, 4, 1, 2, 4, 3, 4);
        break;
    }
    case PType::ChestCorridor:
    {
        sel(p, 0, 0, 0, 4, 4, 6, true);
        smallDoor(p, p.door, 1, 1, 0);
        smallDoor(p, 0, 1, 1, 6);
        box(p, 3, 1, 2, 3, 1, 4, kStoneBricks, kStoneBricks, false);
        put(p, 3, 1, 1, slab());
        put(p, 3, 1, 5, slab());
        put(p, 3, 2, 2, slab());
        put(p, 3, 2, 4, slab());
        for (int z = 2; z <= 4; ++z)
            put(p, 2, 1, z, slab());
        chest(p, 3, 2, 3);
        break;
    }
    case PType::FillerCorridor:
    {
        for (int step = 0; step < p.steps; ++step)
        {
            for (int x = 0; x <= 4; ++x)
                put(p, x, 0, step, kStoneBricks);
            for (int y = 1; y <= 3; ++y)
            {
                put(p, 0, y, step, kStoneBricks);
                put(p, 1, y, step, kCaveAir);
                put(p, 2, y, step, kCaveAir);
                put(p, 3, y, step, kCaveAir);
                put(p, 4, y, step, kStoneBricks);
            }
            for (int x = 0; x <= 4; ++x)
                put(p, x, 4, step, kStoneBricks);
        }
        break;
    }
    case PType::FiveCrossing:
    {
        sel(p, 0, 0, 0, 9, 8, 10, true);
        smallDoor(p, p.door, 4, 3, 0);
        if (p.leftLow)
            airBox(p, 0, 3, 1, 0, 5, 3);
        if (p.rightLow)
            airBox(p, 9, 3, 1, 9, 5, 3);
        if (p.leftHigh)
            airBox(p, 0, 5, 7, 0, 7, 9);
        if (p.rightHigh)
            airBox(p, 9, 5, 7, 9, 7, 9);
        airBox(p, 5, 1, 10, 7, 3, 10);
        sel(p, 1, 2, 1, 8, 2, 6, false);
        sel(p, 4, 1, 5, 4, 4, 9, false);
        sel(p, 8, 1, 5, 8, 4, 9, false);
        sel(p, 1, 4, 7, 3, 4, 9, false);
        sel(p, 1, 3, 5, 3, 3, 6, false);
        box(p, 1, 3, 4, 3, 3, 4, slab(), slab(), false);
        box(p, 1, 4, 6, 3, 4, 6, slab(), slab(), false);
        sel(p, 5, 1, 7, 7, 1, 8, false);
        box(p, 5, 1, 9, 7, 1, 9, slab(), slab(), false);
        box(p, 5, 2, 7, 7, 2, 7, slab(), slab(), false);
        box(p, 4, 5, 7, 4, 5, 9, slab(), slab(), false);
        box(p, 8, 5, 7, 8, 5, 9, slab(), slab(), false);
        box(p, 5, 5, 7, 7, 5, 9, doubleSlab(), doubleSlab(), false);
        put(p, 6, 5, 6, wallTorch("south"));
        break;
    }
    case PType::LeftTurn:
    case PType::RightTurn:
    {
        sel(p, 0, 0, 0, 4, 4, 4, true);
        smallDoor(p, p.door, 1, 1, 0);
        const bool leftSide = (p.type == PType::LeftTurn)
            ? (p.dir != North && p.dir != East)
            : (p.dir == North || p.dir == East);
        if (leftSide)
            airBox(p, 4, 1, 1, 4, 3, 3);
        else
            airBox(p, 0, 1, 1, 0, 3, 3);
        break;
    }
    case PType::PrisonHall:
    {
        sel(p, 0, 0, 0, 8, 4, 10, true);
        smallDoor(p, p.door, 1, 1, 0);
        airBox(p, 1, 1, 10, 3, 3, 10);
        sel(p, 4, 1, 1, 4, 3, 1, false);
        sel(p, 4, 1, 3, 4, 3, 3, false);
        sel(p, 4, 1, 7, 4, 3, 7, false);
        sel(p, 4, 1, 9, 4, 3, 9, false);
        for (int y = 1; y <= 3; ++y)
        {
            put(p, 4, y, 4, bars(true, false, true, false));
            put(p, 4, y, 5, bars(true, true, true, false));
            put(p, 4, y, 6, bars(true, false, true, false));
            put(p, 5, y, 5, bars(false, true, false, true));
            put(p, 6, y, 5, bars(false, true, false, true));
            put(p, 7, y, 5, bars(false, true, false, true));
        }
        put(p, 4, 3, 2, bars(true, false, true, false));
        put(p, 4, 3, 8, bars(true, false, true, false));
        put(p, 4, 1, 2, doorState(true, "west", false));
        put(p, 4, 2, 2, doorState(true, "west", true));
        put(p, 4, 1, 8, doorState(true, "west", false));
        put(p, 4, 2, 8, doorState(true, "west", true));
        break;
    }
    case PType::RoomCrossing:
    {
        sel(p, 0, 0, 0, 10, 6, 10, true);
        smallDoor(p, p.door, 4, 1, 0);
        airBox(p, 4, 1, 10, 6, 3, 10);
        airBox(p, 0, 1, 4, 0, 3, 6);
        airBox(p, 10, 1, 4, 10, 3, 6);
        if (p.roomType == 0)
        {
            put(p, 5, 1, 5, kStoneBricks);
            put(p, 5, 2, 5, kStoneBricks);
            put(p, 5, 3, 5, kStoneBricks);
            put(p, 4, 3, 5, wallTorch("west"));
            put(p, 6, 3, 5, wallTorch("east"));
            put(p, 5, 3, 4, wallTorch("south"));
            put(p, 5, 3, 6, wallTorch("north"));
            put(p, 4, 1, 4, slab());
            put(p, 4, 1, 5, slab());
            put(p, 4, 1, 6, slab());
            put(p, 6, 1, 4, slab());
            put(p, 6, 1, 5, slab());
            put(p, 6, 1, 6, slab());
            put(p, 5, 1, 4, slab());
            put(p, 5, 1, 6, slab());
        }
        else if (p.roomType == 1)
        {
            for (int i = 0; i < 5; ++i)
            {
                put(p, 3, 1, 3 + i, kStoneBricks);
                put(p, 7, 1, 3 + i, kStoneBricks);
                put(p, 3 + i, 1, 3, kStoneBricks);
                put(p, 3 + i, 1, 7, kStoneBricks);
            }
            put(p, 5, 1, 5, kStoneBricks);
            put(p, 5, 2, 5, kStoneBricks);
            put(p, 5, 3, 5, kStoneBricks);
            put(p, 5, 4, 5, QStringLiteral("minecraft:water[level=0]"));
        }
        else if (p.roomType == 2)
        {
            for (int i = 1; i <= 9; ++i)
            {
                put(p, 1, 3, i, kCobble);
                put(p, 9, 3, i, kCobble);
            }
            for (int i = 1; i <= 9; ++i)
            {
                put(p, i, 3, 1, kCobble);
                put(p, i, 3, 9, kCobble);
            }
            put(p, 5, 1, 4, kCobble);
            put(p, 5, 1, 6, kCobble);
            put(p, 5, 3, 4, kCobble);
            put(p, 5, 3, 6, kCobble);
            put(p, 4, 1, 5, kCobble);
            put(p, 6, 1, 5, kCobble);
            put(p, 4, 3, 5, kCobble);
            put(p, 6, 3, 5, kCobble);
            for (int i = 1; i <= 3; ++i)
            {
                put(p, 4, i, 4, kCobble);
                put(p, 6, i, 4, kCobble);
                put(p, 4, i, 6, kCobble);
                put(p, 6, i, 6, kCobble);
            }
            put(p, 5, 3, 5, QStringLiteral("minecraft:wall_torch[facing=north]"));
            for (int i = 2; i <= 8; ++i)
            {
                put(p, 2, 3, i, kPlanks);
                put(p, 3, 3, i, kPlanks);
                if (i <= 3 || i >= 7)
                {
                    put(p, 4, 3, i, kPlanks);
                    put(p, 5, 3, i, kPlanks);
                    put(p, 6, 3, i, kPlanks);
                }
                put(p, 7, 3, i, kPlanks);
                put(p, 8, 3, i, kPlanks);
            }
            put(p, 9, 1, 3, ladderState("west"));
            put(p, 9, 2, 3, ladderState("west"));
            put(p, 9, 3, 3, ladderState("west"));
            chest(p, 3, 4, 8);
        }
        break;
    }
    case PType::StraightStairsDown:
    {
        sel(p, 0, 0, 0, 4, 10, 7, true);
        smallDoor(p, p.door, 1, 7, 0);
        smallDoor(p, 0, 1, 1, 7);
        const QString cs = stairs(QStringLiteral("minecraft:cobblestone_stairs"), "south");
        for (int i = 0; i < 6; ++i)
        {
            put(p, 1, 6 - i, 1 + i, cs);
            put(p, 2, 6 - i, 1 + i, cs);
            put(p, 3, 6 - i, 1 + i, cs);
            if (i < 5)
            {
                put(p, 1, 5 - i, 1 + i, kStoneBricks);
                put(p, 2, 5 - i, 1 + i, kStoneBricks);
                put(p, 3, 5 - i, 1 + i, kStoneBricks);
            }
        }
        break;
    }
    case PType::Library:
    {
        const int h = p.isTall ? 11 : 6;
        sel(p, 0, 0, 0, 13, h - 1, 14, true);
        smallDoor(p, p.door, 4, 1, 0);
        maybeBox(p, 0.07f, 2, 1, 1, 11, 4, 13,
                 QStringLiteral("minecraft:cobweb"), QStringLiteral("minecraft:cobweb"),
                 false);
        for (int i = 1; i <= 13; ++i)
        {
            if ((i - 1) % 4 == 0)
            {
                box(p, 1, 1, i, 1, 4, i, kPlanks, kPlanks, false);
                box(p, 12, 1, i, 12, 4, i, kPlanks, kPlanks, false);
                put(p, 2, 3, i, wallTorch("east"));
                put(p, 11, 3, i, wallTorch("west"));
                if (h == 11)
                {
                    box(p, 1, 6, i, 1, 9, i, kPlanks, kPlanks, false);
                    box(p, 12, 6, i, 12, 9, i, kPlanks, kPlanks, false);
                }
            }
            else
            {
                box(p, 1, 1, i, 1, 4, i, kBookshelf, kBookshelf, false);
                box(p, 12, 1, i, 12, 4, i, kBookshelf, kBookshelf, false);
                if (h == 11)
                {
                    box(p, 1, 6, i, 1, 9, i, kBookshelf, kBookshelf, false);
                    box(p, 12, 6, i, 12, 9, i, kBookshelf, kBookshelf, false);
                }
            }
        }
        for (int i = 3; i < 12; i += 2)
        {
            box(p, 3, 1, i, 4, 3, i, kBookshelf, kBookshelf, false);
            box(p, 6, 1, i, 7, 3, i, kBookshelf, kBookshelf, false);
            box(p, 9, 1, i, 10, 3, i, kBookshelf, kBookshelf, false);
        }
        if (h == 11)
        {
            box(p, 1, 5, 1, 3, 5, 13, kPlanks, kPlanks, false);
            box(p, 10, 5, 1, 12, 5, 13, kPlanks, kPlanks, false);
            box(p, 4, 5, 1, 9, 5, 2, kPlanks, kPlanks, false);
            box(p, 4, 5, 12, 9, 5, 13, kPlanks, kPlanks, false);
            put(p, 9, 5, 11, kPlanks);
            put(p, 8, 5, 11, kPlanks);
            put(p, 9, 5, 10, kPlanks);
            const QString fEW = fence(false, true, false, true);          // 东西向
            const QString fNS = fence(true, false, true, false);          // 南北向
            const QString fE = fence(false, true, false, false);
            const QString fW = fence(false, false, false, true);
            const QString fAll = fence(true, true, true, true);
            box(p, 3, 6, 3, 3, 6, 11, fNS, fNS, false);
            box(p, 10, 6, 3, 10, 6, 9, fNS, fNS, false);
            box(p, 4, 6, 2, 9, 6, 2, fEW, fEW, false);
            box(p, 4, 6, 12, 7, 6, 12, fEW, fEW, false);
            put(p, 3, 6, 2, fence(true, true, false, false));
            put(p, 3, 6, 12, fence(false, true, true, false));
            put(p, 10, 6, 2, fence(true, false, false, true));
            for (int i = 0; i <= 2; ++i)
            {
                put(p, 8 + i, 6, 12 - i, fence(false, false, true, true));
                if (i != 2)
                    put(p, 8 + i, 6, 11 - i, fence(true, true, false, false));
            }
            for (int y = 1; y <= 7; ++y)
                put(p, 10, y, 13, ladderState("south"));
            put(p, 6, 9, 7, fE);
            put(p, 7, 9, 7, fW);
            put(p, 6, 8, 7, fE);
            put(p, 7, 8, 7, fW);
            put(p, 6, 7, 7, fAll);
            put(p, 7, 7, 7, fAll);
            put(p, 5, 7, 7, fE);
            put(p, 8, 7, 7, fW);
            put(p, 6, 7, 6, fence(true, true, false, false));
            put(p, 6, 7, 8, fence(false, true, true, false));
            put(p, 7, 7, 6, fence(true, false, false, true));
            put(p, 7, 7, 8, fence(false, false, true, true));
            put(p, 5, 8, 7, QStringLiteral("minecraft:torch"));
            put(p, 8, 8, 7, QStringLiteral("minecraft:torch"));
            put(p, 6, 8, 6, QStringLiteral("minecraft:torch"));
            put(p, 6, 8, 8, QStringLiteral("minecraft:torch"));
            put(p, 7, 8, 6, QStringLiteral("minecraft:torch"));
            put(p, 7, 8, 8, QStringLiteral("minecraft:torch"));
        }
        chest(p, 3, 3, 5);
        if (h == 11)
        {
            put(p, 12, 9, 1, kCaveAir);
            chest(p, 12, 8, 1);
        }
        break;
    }
    case PType::PortalRoom:
    {
        sel(p, 0, 0, 0, 10, 7, 15, false);
        smallDoor(p, 2, 4, 1, 0);
        sel(p, 1, 6, 1, 1, 6, 14, false);
        sel(p, 9, 6, 1, 9, 6, 14, false);
        sel(p, 2, 6, 1, 8, 6, 2, false);
        sel(p, 2, 6, 14, 8, 6, 14, false);
        sel(p, 1, 1, 1, 2, 1, 4, false);
        sel(p, 8, 1, 1, 9, 1, 4, false);
        box(p, 1, 1, 1, 1, 1, 3, QStringLiteral("minecraft:lava[level=0]"),
            QStringLiteral("minecraft:lava[level=0]"), false);
        box(p, 9, 1, 1, 9, 1, 3, QStringLiteral("minecraft:lava[level=0]"),
            QStringLiteral("minecraft:lava[level=0]"), false);
        sel(p, 3, 1, 8, 7, 1, 12, false);
        box(p, 4, 1, 9, 6, 1, 11, QStringLiteral("minecraft:lava[level=0]"),
            QStringLiteral("minecraft:lava[level=0]"), false);
        const QString bNS = bars(true, false, true, false);
        const QString bEW = bars(false, true, false, true);
        for (int k = 3; k < 14; k += 2)
        {
            box(p, 0, 3, k, 0, 4, k, bNS, bNS, false);
            box(p, 10, 3, k, 10, 4, k, bNS, bNS, false);
        }
        for (int k = 2; k < 9; k += 2)
            box(p, k, 3, 15, k, 4, 15, bEW, bEW, false);
        const QString brickStairs = stairs(QStringLiteral("minecraft:stone_brick_stairs"),
                                           "north");
        box(p, 4, 1, 5, 6, 1, 7, kCaveAir, kCaveAir, false);
        box(p, 4, 2, 6, 6, 2, 7, kCaveAir, kCaveAir, false);
        box(p, 4, 3, 7, 6, 3, 7, kCaveAir, kCaveAir, false);
        for (int i = 4; i <= 6; ++i)
        {
            put(p, i, 1, 4, brickStairs);
            put(p, i, 2, 5, brickStairs);
            put(p, i, 3, 6, brickStairs);
        }
        bool eyes[12];
        bool all = true;
        for (int i = 0; i < 12; ++i)
        {
            eyes[i] = m_rng.nextFloat() > 0.9f;
            all = all && eyes[i];
        }
        put(p, 4, 3, 8, frameState("north", eyes[0]));
        put(p, 5, 3, 8, frameState("north", eyes[1]));
        put(p, 6, 3, 8, frameState("north", eyes[2]));
        put(p, 4, 3, 12, frameState("south", eyes[3]));
        put(p, 5, 3, 12, frameState("south", eyes[4]));
        put(p, 6, 3, 12, frameState("south", eyes[5]));
        put(p, 3, 3, 9, frameState("east", eyes[6]));
        put(p, 3, 3, 10, frameState("east", eyes[7]));
        put(p, 3, 3, 11, frameState("east", eyes[8]));
        put(p, 7, 3, 9, frameState("west", eyes[9]));
        put(p, 7, 3, 10, frameState("west", eyes[10]));
        put(p, 7, 3, 11, frameState("west", eyes[11]));
        if (all)
        {
            for (int x = 4; x <= 6; ++x)
                for (int z = 9; z <= 11; ++z)
                    put(p, x, 3, z, QStringLiteral("minecraft:end_portal"));
        }
        // 末地传送门房间的刷怪笼（银鱼）
        put(p, 5, 3, 6, QStringLiteral("minecraft:spawner"));
        break;
    }
    }
}

// ── 主流程 ──────────────────────────────────────────────────────────────────

bool Gen::run(Model &out)
{
    m_out = &out;

    LRng rng;
    int attempt = 0;
    for (;;)
    {
        m_pieces.clear();
        m_pending.clear();
        resetPieces();
        m_portalRoom = false;
        out.hasPortalRoom = false;

        rng.setLargeFeatureSeed(qint64(m_seed) + attempt, m_cx, m_cz);
        ++attempt;

        Piece start;
        start.type = PType::Start;
        start.depth = 0;
        start.isSource = true;
        start.dir = rng.nextInt(4);  // Direction.Plane.HORIZONTAL：N/E/S/W
        start.box = makeBox(m_startX, 64, m_startZ, 5, 11, 5, start.dir);
        start.door = 0;
        m_pieces.append(start);
        addChildren(0, rng);

        while (!m_pending.isEmpty())
        {
            const int idx = rng.nextInt(m_pending.size());
            const int pieceIdx = m_pending[idx];
            m_pending.removeAt(idx);
            addChildren(pieceIdx, rng);
        }

        // ── moveBelowSeaLevel(63, minY, rng, 10) ──
        Box all;
        all = m_pieces.first().box;
        for (const Piece &p : m_pieces)
        {
            all.x0 = std::min(all.x0, p.box.x0);
            all.y0 = std::min(all.y0, p.box.y0);
            all.z0 = std::min(all.z0, p.box.z0);
            all.x1 = std::max(all.x1, p.box.x1);
            all.y1 = std::max(all.y1, p.box.y1);
            all.z1 = std::max(all.z1, p.box.z1);
        }
        int j = all.yspan() + m_minY + 1;
        const int limit = m_seaLevel - 10;
        if (j < limit)
            j += rng.nextInt(limit - j);
        const int offset = j - all.y1;
        if (offset != 0)
            for (Piece &p : m_pieces)
                p.box.move(0, offset, 0);

        if (!m_pieces.isEmpty() && m_portalRoom)
            break;
        if (attempt > 40)
        {
            out.error = QStringLiteral("要塞拼装失败");
            return false;
        }
    }

    // ── 逐区块落块 ──
    Box all = m_pieces.first().box;
    for (const Piece &p : m_pieces)
    {
        all.x0 = std::min(all.x0, p.box.x0);
        all.y0 = std::min(all.y0, p.box.y0);
        all.z0 = std::min(all.z0, p.box.z0);
        all.x1 = std::max(all.x1, p.box.x1);
        all.y1 = std::max(all.y1, p.box.y1);
        all.z1 = std::max(all.z1, p.box.z1);
    }
    const int cx0 = (all.x0 >> 4);
    const int cx1 = (all.x1 >> 4);
    const int cz0 = (all.z0 >> 4);
    const int cz1 = (all.z1 >> 4);
    for (int cz = cz0; cz <= cz1; ++cz)
    {
        for (int cx = cx0; cx <= cx1; ++cx)
        {
            Box chunk;
            chunk.x0 = cx * 16;
            chunk.x1 = cx * 16 + 15;
            chunk.z0 = cz * 16;
            chunk.z1 = cz * 16 + 15;
            chunk.y0 = m_minY + 1;
            chunk.y1 = 319;

            // 装饰种子 → 结构步种子：与原版 ChunkGenerator.applyBiomeDecoration 一致
            // （strongholds 是第 5 个生成步骤，该步骤里只有要塞一种结构，下标为 0）
            const qint64 deco = m_rng.setDecorationSeed(qint64(m_seed), cx * 16, cz * 16);
            m_rng.setFeatureSeed(deco, 0, 5);

            for (int i = 0; i < m_pieces.size(); ++i)
            {
                Piece &p = m_pieces[i];
                if (!p.box.intersects(chunk))
                    continue;
                postProcess(p, chunk);
            }
        }
    }

    if (m_blocks.isEmpty())
    {
        out.error = QStringLiteral("要塞模型为空");
        return false;
    }

    // ── 导出 ──
    int minX = 1 << 30, minY = 1 << 30, minZ = 1 << 30;
    int maxX = -(1 << 30), maxY = -(1 << 30), maxZ = -(1 << 30);
    for (auto it = m_blocks.constBegin(); it != m_blocks.constEnd(); ++it)
    {
        const qint64 k = it.key();
        const int relX = int(k & 0xFFF) - 2048;
        const int relZ = int((k >> 12) & 0xFFF) - 2048;
        const int y = int((k >> 24) & 0xFFF) - 2048;
        minX = std::min(minX, relX); maxX = std::max(maxX, relX);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
        minZ = std::min(minZ, relZ); maxZ = std::max(maxZ, relZ);
    }

    out.blocks = m_blocks;
    out.x0 = minX; out.x1 = maxX;
    out.y0 = minY; out.y1 = maxY;
    out.z0 = minZ; out.z1 = maxZ;
    out.blockCount = m_blocks.size();
    out.ok = true;
    out.startX = m_startX;
    out.startZ = m_startZ;
    out.hasPortalRoom = m_portalRoom;
    out.variant = m_portalRoom ? QStringLiteral("要塞 · 末地传送门")
                               : QStringLiteral("要塞");

    for (const Piece &p : m_pieces)
    {
        PieceBox pb;
        pb.type = QString::fromLatin1(pieceName(p.type));
        pb.x0 = p.box.x0; pb.y0 = p.box.y0; pb.z0 = p.box.z0;
        pb.x1 = p.box.x1; pb.y1 = p.box.y1; pb.z1 = p.box.z1;
        out.pieces.append(pb);
    }
    return true;
}

} // namespace

Model build(quint64 seed, int startChunkX, int startChunkZ, int seaLevel, int minYLevel)
{
    Model m;
    Gen gen(seed, startChunkX, startChunkZ, seaLevel, minYLevel);
    if (!gen.run(m))
    {
        m.ok = false;
        return m;
    }
    return m;
}

} // namespace StrongholdModel
