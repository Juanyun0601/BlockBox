/**
 * @file   FortressModel.cpp
 * @brief  下界要塞逐块重建（移植 NetherFortressStructure / NetherFortressPieces）
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 对照 1.21.4 反编译代码逐行还原：
 *   NetherFortressStructure.findGenerationPoint / addPieces
 *   NetherFortressPieces.StartPiece / NetherBridgePiece.addChildren
 *   NetherFortressPieces.<各段>.createPiece / postProcess
 *   StructurePiece.placeBlock / generateBox / fillColumnDown
 *   StructurePiecesBuilder.moveBelowSeaLevel
 *
 * 随机源是 LegacyRandomSource（48 位 Java LCG），种子按
 * WorldgenRandom.setLargeFeatureSeed(seed, 起始区块) 混合，因此同一个
 * 世界种子、同一座要塞算出的段序、朝向、位置与游戏内一致。
 */

#include "FortressModel.h"

#include <QStringList>

#include <algorithm>
#include <vector>

namespace FortressModel {
namespace {

// ────────────────────────────────────────────────────────────────────────────
// 原版 LegacyRandomSource（java.util.Random 的 48 位 LCG）
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
    void setLargeFeatureSeed(qint64 seed, int cx, int cz)
    {
        setSeed(seed);
        const qint64 a = nextLong();
        const qint64 b = nextLong();
        setSeed(qint64(quint64(cx) * quint64(a) ^ quint64(cz) * quint64(b)
                       ^ quint64(seed)));
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
    DIR_EAST = 5
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

/**
 * 原版 StructurePiece.setOrientation 推出的「代码方位 → 世界方位」映射。
 *
 * 各段的方块状态都按同一套代码方位书写（前向 = NORTH、右向 = EAST），
 * placeBlock 再按段朝向做 mirror / rotate：
 *   NORTH 不转；SOUTH 前后翻转；WEST 翻转后顺时针 90°；EAST 顺时针 90°。
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
    default: // EAST
        return dirCw90(codeDir);
    }
}

inline int dirDx(int d) { return d == DIR_EAST ? 1 : (d == DIR_WEST ? -1 : 0); }
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

// ────────────────────────────────────────────────────────────────────────────
// 方块状态
// ────────────────────────────────────────────────────────────────────────────

const QString kAir = QStringLiteral("minecraft:air");
const QString kBrick = QStringLiteral("minecraft:nether_bricks");
const QString kLava = QStringLiteral("minecraft:lava[level=0]");
const QString kSoulSand = QStringLiteral("minecraft:soul_sand");
const QString kSpawner = QStringLiteral("minecraft:spawner");

QString wartState()
{
    return QStringLiteral("minecraft:nether_wart[age=0]");
}

/** 栏杆：按代码方位给出四边连接，再映射到世界方位 */
QString fenceState(bool n, bool e, bool s, bool w, int orient)
{
    bool wn = false, we = false, ws = false, ww = false;
    struct Tmp
    {
        static void set(bool &wn_, bool &we_, bool &ws_, bool &ww_, int d)
        {
            if (d == DIR_NORTH) wn_ = true;
            else if (d == DIR_SOUTH) ws_ = true;
            else if (d == DIR_WEST) ww_ = true;
            else we_ = true;
        }
    };
    if (n) Tmp::set(wn, we, ws, ww, worldDirOfState(DIR_NORTH, orient));
    if (e) Tmp::set(wn, we, ws, ww, worldDirOfState(DIR_EAST, orient));
    if (s) Tmp::set(wn, we, ws, ww, worldDirOfState(DIR_SOUTH, orient));
    if (w) Tmp::set(wn, we, ws, ww, worldDirOfState(DIR_WEST, orient));
    return QStringLiteral("minecraft:nether_brick_fence[east=%1,north=%2,south=%3,waterlogged=false,west=%4]")
        .arg(we ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(wn ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(ws ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(ww ? QStringLiteral("true") : QStringLiteral("false"));
}

/** 楼梯：facing 按代码方位给出，再映射到世界方位 */
QString stairState(int codeFacing, int orient)
{
    const int f = worldDirOfState(codeFacing, orient);
    return QStringLiteral("minecraft:nether_brick_stairs[facing=%1,half=bottom,shape=straight,waterlogged=false]")
        .arg(QString::fromLatin1(dirName(f)));
}

QString chestState(int facing)
{
    return QStringLiteral("minecraft:chest[facing=%1]")
        .arg(QString::fromLatin1(dirName(facing)));
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
    void move(int dx, int dy, int dz)
    {
        x0 += dx; x1 += dx; y0 += dy; y1 += dy; z0 += dz; z1 += dz;
    }
};

/**
 * 原版 BoundingBox.orientBox(x, y, z, offX, offY, offZ, sizeX, sizeY, sizeZ, dir)
 *
 * (x, y, z) 是段与父段相接的锚点，(offX/offY/offZ) 是相对锚点的偏移，
 * 段体沿 -dir 方向展开（NORTH 往 -Z 长、WEST 往 -X 长……）。
 */
Box orientBox(int x, int y, int z, int ox, int oy, int oz,
              int sx, int sy, int sz, int dir)
{
    Box b;
    switch (dir)
    {
    case DIR_NORTH:
        b.x0 = x + ox; b.y0 = y + oy; b.z0 = z - sz + 1 + oz;
        b.x1 = x + ox + sx - 1; b.y1 = y + oy + sy - 1; b.z1 = z + oz;
        break;
    case DIR_WEST:
        b.x0 = x - sz + 1 + oz; b.y0 = y + oy; b.z0 = z + ox;
        b.x1 = x + oz; b.y1 = y + oy + sy - 1; b.z1 = z + ox + sx - 1;
        break;
    case DIR_EAST:
        b.x0 = x + oz; b.y0 = y + oy; b.z0 = z + ox;
        b.x1 = x + oz + sz - 1; b.y1 = y + oy + sy - 1; b.z1 = z + ox + sx - 1;
        break;
    case DIR_SOUTH:
    default:
        b.x0 = x + ox; b.y0 = y + oy; b.z0 = z + oz;
        b.x1 = x + ox + sx - 1; b.y1 = y + oy + sy - 1; b.z1 = z + oz + sz - 1;
        break;
    }
    return b;
}

/** 原版 StructurePiece.createBox（起段用，不带偏移） */
Box createBoxAt(int x, int y, int z, int dir, int w, int h, int d)
{
    Box b;
    if (dir == DIR_NORTH || dir == DIR_SOUTH)
        b = {x, y, z, x + w - 1, y + h - 1, z + d - 1};
    else
        b = {x, y, z, x + d - 1, y + h - 1, z + w - 1};
    return b;
}

// ────────────────────────────────────────────────────────────────────────────
// 段类型
// ────────────────────────────────────────────────────────────────────────────

enum PType
{
    T_BRIDGE_CROSSING = 0,
    T_BRIDGE_END_FILLER,
    T_BRIDGE_STRAIGHT,
    T_CASTLE_STAIRS,
    T_CASTLE_BALCONY,
    T_CASTLE_ENTRANCE,
    T_CASTLE_SMALL_CROSSING,
    T_CASTLE_SMALL_LEFT,
    T_CASTLE_SMALL,
    T_CASTLE_SMALL_RIGHT,
    T_CASTLE_STALK_ROOM,
    T_MONSTER_THRONE,
    T_ROOM_CROSSING,
    T_STAIRS_ROOM
};

const char *typeName(int t)
{
    switch (t)
    {
    case T_BRIDGE_CROSSING:       return "bridge_crossing";
    case T_BRIDGE_END_FILLER:     return "bridge_end_filler";
    case T_BRIDGE_STRAIGHT:       return "bridge_straight";
    case T_CASTLE_STAIRS:         return "castle_stairs";
    case T_CASTLE_BALCONY:        return "castle_balcony";
    case T_CASTLE_ENTRANCE:       return "castle_entrance";
    case T_CASTLE_SMALL_CROSSING: return "castle_crossing";
    case T_CASTLE_SMALL_LEFT:     return "castle_left";
    case T_CASTLE_SMALL:          return "castle_corridor";
    case T_CASTLE_SMALL_RIGHT:    return "castle_right";
    case T_CASTLE_STALK_ROOM:     return "castle_stalk_room";
    case T_MONSTER_THRONE:        return "monster_throne";
    case T_ROOM_CROSSING:         return "room_crossing";
    default:                      return "stairs_room";
    }
}

/** 各段 createPiece 的包围盒参数（offX, offY, offZ, sizeX, sizeY, sizeZ） */
bool pieceBoxParams(int type, int &ox, int &oy, int &oz,
                    int &sx, int &sy, int &sz)
{
    switch (type)
    {
    case T_BRIDGE_CROSSING:       ox = -8; oy = -3; oz = 0; sx = 19; sy = 10; sz = 19; return true;
    case T_BRIDGE_END_FILLER:     ox = -1; oy = -3; oz = 0; sx = 5;  sy = 10; sz = 8;  return true;
    case T_BRIDGE_STRAIGHT:       ox = -1; oy = -3; oz = 0; sx = 5;  sy = 10; sz = 19; return true;
    case T_CASTLE_STAIRS:         ox = -1; oy = -7; oz = 0; sx = 5;  sy = 14; sz = 10; return true;
    case T_CASTLE_BALCONY:        ox = -3; oy = 0;  oz = 0; sx = 9;  sy = 7;  sz = 9;  return true;
    case T_CASTLE_ENTRANCE:       ox = -5; oy = -3; oz = 0; sx = 13; sy = 14; sz = 13; return true;
    case T_CASTLE_SMALL_CROSSING: ox = -1; oy = 0;  oz = 0; sx = 5;  sy = 7;  sz = 5;  return true;
    case T_CASTLE_SMALL_LEFT:     ox = -1; oy = 0;  oz = 0; sx = 5;  sy = 7;  sz = 5;  return true;
    case T_CASTLE_SMALL:          ox = -1; oy = 0;  oz = 0; sx = 5;  sy = 7;  sz = 5;  return true;
    case T_CASTLE_SMALL_RIGHT:    ox = -1; oy = 0;  oz = 0; sx = 5;  sy = 7;  sz = 5;  return true;
    case T_CASTLE_STALK_ROOM:     ox = -5; oy = -3; oz = 0; sx = 13; sy = 14; sz = 13; return true;
    case T_MONSTER_THRONE:        ox = -2; oy = 0;  oz = 0; sx = 7;  sy = 8;  sz = 9;  return true;
    case T_ROOM_CROSSING:         ox = -2; oy = 0;  oz = 0; sx = 7;  sy = 9;  sz = 7;  return true;
    case T_STAIRS_ROOM:           ox = -2; oy = 0;  oz = 0; sx = 7;  sy = 11; sz = 7;  return true;
    default: return false;
    }
}

struct Piece
{
    int type = T_BRIDGE_CROSSING;
    int depth = 0;
    int dir = DIR_NORTH;
    Box box;
    qint32 seed = 0;      ///< BridgeEndFiller：末段随机种子
    bool flag = false;    ///< 箱子 / 刷怪笼是否已放
};

struct Weight
{
    int type = 0;
    int weight = 0;
    int maxCount = 0;     ///< 0 = 不限
    bool allowInRow = false;
    int placed = 0;

    bool canPlace(int /*depth*/) const { return maxCount == 0 || placed < maxCount; }
    bool isFull() const { return maxCount != 0 && placed >= maxCount; }
};

struct Grid
{
    QHash<qint64, QString> cells;

    void put(int relX, int y, int relZ, const QString &s)
    {
        const qint64 k = packKey(relX, y, relZ);
        if (s.isEmpty() || s == kAir)
            cells.remove(k);
        else
            cells.insert(k, s);
    }
    QString at(int relX, int y, int relZ) const
    {
        return cells.value(packKey(relX, y, relZ));
    }
    bool isSolid(int relX, int y, int relZ) const
    {
        const QString s = at(relX, y, relZ);
        return !s.isEmpty() && s != kLava;
    }
    bool isReplaceable(int relX, int y, int relZ) const
    {
        const QString s = at(relX, y, relZ);
        return s.isEmpty() || s == kLava;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// 摆放器：把段的局部坐标换算成世界坐标，并按段朝向旋转方块状态
// ────────────────────────────────────────────────────────────────────────────

struct Placer
{
    Grid &grid;
    const Piece &piece;
    int startX = 0;
    int startZ = 0;

    int wx(int lx, int lz) const
    {
        switch (piece.dir)
        {
        case DIR_NORTH:
        case DIR_SOUTH:
            return piece.box.x0 + lx;
        case DIR_WEST:
            return piece.box.x1 - lz;
        default: // EAST
            return piece.box.x0 + lz;
        }
    }
    int wz(int lx, int lz) const
    {
        switch (piece.dir)
        {
        case DIR_NORTH:
            return piece.box.z1 - lz;
        case DIR_SOUTH:
            return piece.box.z0 + lz;
        default:
            return piece.box.z0 + lx;
        }
    }
    int wy(int ly) const { return piece.box.y0 + ly; }

    void put(const QString &s, int lx, int ly, int lz)
    {
        grid.put(wx(lx, lz) - startX, wy(ly), wz(lx, lz) - startZ, s);
    }

    /** 原版 StructurePiece.generateBox（首末圆角方块与内部一致，直接整块填） */
    void fill(int x1, int y1, int z1, int x2, int y2, int z2, const QString &s)
    {
        if (x1 > x2) std::swap(x1, x2);
        if (y1 > y2) std::swap(y1, y2);
        if (z1 > z2) std::swap(z1, z2);
        for (int x = x1; x <= x2; ++x)
            for (int y = y1; y <= y2; ++y)
                for (int z = z1; z <= z2; ++z)
                    put(s, x, y, z);
    }

    /** 原版 StructurePiece.fillColumnDown：从 y 起往下填到实心（预览按岩浆海面截止） */
    void fillDown(const QString &s, int lx, int ly, int lz)
    {
        const int x = wx(lx, lz) - startX;
        const int z = wz(lx, lz) - startZ;
        int y = wy(ly);
        while (y >= 32 && grid.isReplaceable(x, y, z))
        {
            grid.put(x, y, z, s);
            --y;
        }
    }

    /** 原版 StructurePiece.generateChest：朝向取四面实心方块的反向 */
    void chest(int lx, int ly, int lz)
    {
        const int x = wx(lx, lz) - startX;
        const int z = wz(lx, lz) - startZ;
        const int y = wy(ly);
        if (grid.at(x, y, z).startsWith(QLatin1String("minecraft:chest")))
            return;

        const int dirs[4] = {DIR_NORTH, DIR_EAST, DIR_SOUTH, DIR_WEST};
        int solid = -1;
        for (int i = 0; i < 4; ++i)
        {
            if (!grid.isSolid(x + dirDx(dirs[i]), y, z + dirDz(dirs[i])))
                continue;
            if (solid >= 0) { solid = -2; break; }  // 两面都有实心：不判定
            solid = dirs[i];
        }

        int facing = DIR_NORTH;
        if (solid >= 0)
            facing = dirOpposite(solid);
        else
        {
            auto solidAt = [&](int d) {
                return grid.isSolid(x + dirDx(d), y, z + dirDz(d));
            };
            if (solidAt(facing))
                facing = dirOpposite(facing);
            if (solidAt(facing))
                facing = dirCw90(facing);
            if (solidAt(facing))
                facing = dirOpposite(facing);
        }
        grid.put(x, y, z, chestState(facing));
    }
};

// ────────────────────────────────────────────────────────────────────────────
// 生成器
// ────────────────────────────────────────────────────────────────────────────

struct Gen
{
    LegacyRng rng;
    std::vector<Piece *> pieces;     ///< 已放置的段（顺序即摆放顺序）
    std::vector<Weight *> bridgeWeights;
    std::vector<Weight *> castleWeights;
    std::vector<Weight *> weightPool;  ///< 权重表本体（保持指针稳定）
    const Weight *lastWeight = nullptr;
    std::vector<Piece *> pending;    ///< 待展开的段（StartPiece.pendingChildren）
    Piece *start = nullptr;
    quint64 seedValue = 0;

    ~Gen()
    {
        for (Piece *p : pieces)
            delete p;
        for (Weight *w : weightPool)
            delete w;
    }

    Weight *addWeight(std::vector<Weight *> &list, int type, int weight,
                      int maxCount, bool allowInRow)
    {
        auto *w = new Weight{type, weight, maxCount, allowInRow, 0};
        weightPool.push_back(w);
        list.push_back(w);
        return w;
    }

    bool collides(const Box &b) const
    {
        for (const Piece *p : pieces)
            if (p->box.intersects(b))
                return true;
        return false;
    }

    Piece *newPiece(int type, int depth, int dir, const Box &box)
    {
        auto *p = new Piece;
        p->type = type;
        p->depth = depth;
        p->dir = dir;
        p->box = box;
        return p;
    }

    /** 原版各段的 createPiece：包围盒合法 + 不碰撞才创建（BridgeEndFiller 取种子） */
    Piece *createPiece(int type, int x, int y, int z, int dir, int depth)
    {
        int ox, oy, oz, sx, sy, sz;
        if (!pieceBoxParams(type, ox, oy, oz, sx, sy, sz))
            return nullptr;
        const Box box = orientBox(x, y, z, ox, oy, oz, sx, sy, sz, dir);
        if (!(box.valid() && box.y0 > 10))
            return nullptr;
        if (collides(box))
            return nullptr;

        Piece *p = newPiece(type, depth, dir, box);
        if (type == T_BRIDGE_END_FILLER)
            p->seed = qint32(rng.nextInt());
        else if (type == T_CASTLE_SMALL_LEFT || type == T_CASTLE_SMALL_RIGHT)
            p->flag = rng.nextInt(3) == 0;
        return p;
    }

    int totalWeight(const std::vector<Weight *> &weights) const
    {
        bool any = false;
        int total = 0;
        for (const Weight *w : weights)
        {
            if (w->maxCount > 0 && w->placed < w->maxCount)
                any = true;
            total += w->weight;
        }
        return any ? total : -1;
    }

    /** 原版 NetherBridgePiece.generatePiece（权重抽取 + 最多重试 5 次） */
    Piece *generatePiece(std::vector<Weight *> &weights, int x, int y, int z,
                         int dir, int depth)
    {
        const int total = totalWeight(weights);
        const bool run = total > 0 && depth <= 30;
        int tries = 0;
        while (tries < 5 && run)
        {
            ++tries;
            int r = rng.nextInt(total);
            for (std::size_t i = 0; i < weights.size(); ++i)
            {
                Weight *w = weights[i];
                r -= w->weight;
                if (r >= 0)
                    continue;
                if (!w->canPlace(depth) || (w == lastWeight && !w->allowInRow))
                    break;

                Piece *p = createPiece(w->type, x, y, z, dir, depth);
                if (p)
                {
                    ++w->placed;
                    lastWeight = w;
                    if (w->isFull())
                        weights.erase(weights.begin() + std::ptrdiff_t(i));
                    return p;
                }
                // 原版在这里不 break：继续试列表里的下一个权重
            }
        }
        return createPiece(T_BRIDGE_END_FILLER, x, y, z, dir, depth);
    }

    /** 原版 NetherBridgePiece.generateAndAddPiece */
    Piece *generateAndAddPiece(int x, int y, int z, int dir, int depth,
                               std::vector<Weight *> &weights)
    {
        if (std::abs(x - start->box.x0) <= 112 && std::abs(z - start->box.z0) <= 112)
        {
            Piece *p = generatePiece(weights, x, y, z, dir, depth);
            if (p)
            {
                pieces.push_back(p);
                pending.push_back(p);
            }
            return p;
        }
        return createPiece(T_BRIDGE_END_FILLER, x, y, z, dir, depth);
    }

    /** 朝前扩展（原版 NetherBridgePiece.generateForward） */
    void addForward(Piece *p, int xOff, int yOff, bool castle)
    {
        const int d = p->dir;
        int nx = 0, ny = p->box.y0 + yOff, nz = 0;
        switch (d)
        {
        case DIR_NORTH: nx = p->box.x0 + xOff; nz = p->box.z0 - 1; break;
        case DIR_SOUTH: nx = p->box.x0 + xOff; nz = p->box.z1 + 1; break;
        case DIR_WEST:  nx = p->box.x0 - 1; nz = p->box.z0 + xOff; break;
        default:        nx = p->box.x1 + 1; nz = p->box.z0 + xOff; break;
        }
        generateAndAddPiece(nx, ny, nz, d, p->depth + 1,
                            castle ? castleWeights : bridgeWeights);
    }

    /** 左侧扩展（原版 generateLeft） */
    void addLeft(Piece *p, int yOff, int zOff, bool castle)
    {
        const int d = p->dir;
        int nx = 0, ny = p->box.y0 + yOff, nz = 0, nd = DIR_WEST;
        if (d == DIR_NORTH || d == DIR_SOUTH)
        {
            nx = p->box.x0 - 1;
            nz = p->box.z0 + zOff;
            nd = DIR_WEST;
        }
        else
        {
            nx = p->box.x0 + zOff;
            nz = p->box.z0 - 1;
            nd = DIR_NORTH;
        }
        generateAndAddPiece(nx, ny, nz, nd, p->depth + 1,
                            castle ? castleWeights : bridgeWeights);
    }

    /** 右侧扩展（原版 generateRight） */
    void addRight(Piece *p, int yOff, int zOff, bool castle)
    {
        const int d = p->dir;
        int nx = 0, ny = p->box.y0 + yOff, nz = 0, nd = DIR_EAST;
        if (d == DIR_NORTH || d == DIR_SOUTH)
        {
            nx = p->box.x1 + 1;
            nz = p->box.z0 + zOff;
            nd = DIR_EAST;
        }
        else
        {
            nx = p->box.x0 + zOff;
            nz = p->box.z1 + 1;
            nd = DIR_SOUTH;
        }
        generateAndAddPiece(nx, ny, nz, nd, p->depth + 1,
                            castle ? castleWeights : bridgeWeights);
    }

    /** 原版各段 addChildren */
    void addChildren(Piece *p)
    {
        switch (p->type)
        {
        case T_BRIDGE_CROSSING:
            addForward(p, 8, 3, false);
            addLeft(p, 3, 8, false);
            addRight(p, 3, 8, false);
            break;
        case T_BRIDGE_STRAIGHT:
            addForward(p, 1, 3, false);
            break;
        case T_CASTLE_STAIRS:
            addForward(p, 1, 0, true);
            break;
        case T_CASTLE_BALCONY:
        {
            const int y = (p->dir == DIR_WEST || p->dir == DIR_NORTH) ? 5 : 1;
            // 原版两侧子段各自抽一次 nextInt(8)：结果为 0 时改用桥段权重表
            const bool leftCastle = rng.nextInt(8) > 0;
            addLeft(p, 0, y, leftCastle);
            const bool rightCastle = rng.nextInt(8) > 0;
            addRight(p, 0, y, rightCastle);
            break;
        }
        case T_CASTLE_ENTRANCE:
            addForward(p, 5, 3, true);
            break;
        case T_CASTLE_SMALL_CROSSING:
            addForward(p, 1, 0, true);
            addLeft(p, 0, 1, true);
            addRight(p, 0, 1, true);
            break;
        case T_CASTLE_SMALL_LEFT:
            addLeft(p, 0, 1, true);
            break;
        case T_CASTLE_SMALL:
            addForward(p, 1, 0, true);
            break;
        case T_CASTLE_SMALL_RIGHT:
            addRight(p, 0, 1, true);
            break;
        case T_CASTLE_STALK_ROOM:
            addForward(p, 5, 3, true);
            addForward(p, 5, 11, true);
            break;
        case T_ROOM_CROSSING:
            addForward(p, 2, 0, false);
            addLeft(p, 0, 2, false);
            addRight(p, 0, 2, false);
            break;
        case T_STAIRS_ROOM:
            addRight(p, 6, 2, false);
            break;
        default:
            break; // 末段 / 王座厅没有子段
        }
    }

    bool run(int chunkX, int chunkZ)
    {
        rng.setLargeFeatureSeed(qint64(seedValue), chunkX, chunkZ);

        bridgeWeights.clear();
        castleWeights.clear();
        addWeight(bridgeWeights, T_BRIDGE_STRAIGHT, 30, 0, true);
        addWeight(bridgeWeights, T_BRIDGE_CROSSING, 10, 4, false);
        addWeight(bridgeWeights, T_ROOM_CROSSING, 10, 4, false);
        addWeight(bridgeWeights, T_STAIRS_ROOM, 10, 3, false);
        addWeight(bridgeWeights, T_MONSTER_THRONE, 5, 2, false);
        addWeight(bridgeWeights, T_CASTLE_ENTRANCE, 5, 1, false);

        addWeight(castleWeights, T_CASTLE_SMALL, 25, 0, true);
        addWeight(castleWeights, T_CASTLE_SMALL_CROSSING, 15, 5, false);
        addWeight(castleWeights, T_CASTLE_SMALL_RIGHT, 5, 10, false);
        addWeight(castleWeights, T_CASTLE_SMALL_LEFT, 5, 10, false);
        addWeight(castleWeights, T_CASTLE_STAIRS, 10, 3, true);
        addWeight(castleWeights, T_CASTLE_BALCONY, 7, 2, false);
        addWeight(castleWeights, T_CASTLE_STALK_ROOM, 5, 2, false);
        lastWeight = nullptr;
        pending.clear();

        // NetherFortressPieces.StartPiece(random, chunkMinX + 2, chunkMinZ + 2)
        static const int horizontal[4] = {DIR_NORTH, DIR_EAST, DIR_SOUTH, DIR_WEST};
        const int dir = horizontal[rng.nextInt(4)];
        start = newPiece(T_BRIDGE_CROSSING, 0, dir,
                         createBoxAt(chunkX * 16 + 2, 64, chunkZ * 16 + 2,
                                     dir, 19, 10, 19));
        pieces.push_back(start);

        addChildren(start);
        while (!pending.empty())
        {
            const int index = rng.nextInt(int(pending.size()));
            Piece *p = pending[std::size_t(index)];
            pending.erase(pending.begin() + index);
            addChildren(p);
        }

        // StructurePiecesBuilder.moveBelowSeaLevel(random, 48, 70)
        Box all = pieces.front()->box;
        for (const Piece *p : pieces)
        {
            all.x0 = std::min(all.x0, p->box.x0);
            all.y0 = std::min(all.y0, p->box.y0);
            all.z0 = std::min(all.z0, p->box.z0);
            all.x1 = std::max(all.x1, p->box.x1);
            all.y1 = std::max(all.y1, p->box.y1);
            all.z1 = std::max(all.z1, p->box.z1);
        }
        const int span = 70 - 48 + 1 - (all.y1 - all.y0 + 1);
        const int target = span > 1 ? 48 + rng.nextInt(span) : 48;
        const int offset = target - all.y0;
        if (offset != 0)
        if (offset != 0)
            for (Piece *p : pieces)
                p->box.move(0, offset, 0);
        return !pieces.empty();
    }
};

// ────────────────────────────────────────────────────────────────────────────
// 逐块摆放（原版各段 postProcess）
// ────────────────────────────────────────────────────────────────────────────

void placeBridgeCrossing(Placer &pl)
{
    pl.fill(7, 3, 0, 11, 4, 18, kBrick);
    pl.fill(0, 3, 7, 18, 4, 11, kBrick);
    pl.fill(8, 5, 0, 10, 7, 18, kAir);
    pl.fill(0, 5, 8, 18, 7, 10, kAir);
    pl.fill(7, 5, 0, 7, 5, 7, kBrick);
    pl.fill(7, 5, 11, 7, 5, 18, kBrick);
    pl.fill(11, 5, 0, 11, 5, 7, kBrick);
    pl.fill(11, 5, 11, 11, 5, 18, kBrick);
    pl.fill(0, 5, 7, 7, 5, 7, kBrick);
    pl.fill(11, 5, 7, 18, 5, 7, kBrick);
    pl.fill(0, 5, 11, 7, 5, 11, kBrick);
    pl.fill(11, 5, 11, 18, 5, 11, kBrick);
    pl.fill(7, 2, 0, 11, 2, 5, kBrick);
    pl.fill(7, 2, 13, 11, 2, 18, kBrick);
    pl.fill(7, 0, 0, 11, 1, 3, kBrick);
    pl.fill(7, 0, 15, 11, 1, 18, kBrick);

    for (int x = 7; x <= 11; ++x)
    {
        for (int z = 0; z <= 2; ++z)
        {
            pl.fillDown(kBrick, x, -1, z);
            pl.fillDown(kBrick, x, -1, 18 - z);
        }
    }

    pl.fill(0, 2, 7, 5, 2, 11, kBrick);
    pl.fill(13, 2, 7, 18, 2, 11, kBrick);
    pl.fill(0, 0, 7, 3, 1, 11, kBrick);
    pl.fill(15, 0, 7, 18, 1, 11, kBrick);

    for (int x = 0; x <= 2; ++x)
    {
        for (int z = 7; z <= 11; ++z)
        {
            pl.fillDown(kBrick, x, -1, z);
            pl.fillDown(kBrick, 18 - x, -1, z);
        }
    }
}

void placeBridgeEndFiller(Placer &pl)
{
    LegacyRng r;
    r.setSeed(qint64(pl.piece.seed));

    for (int x = 0; x <= 4; ++x)
        for (int y = 3; y <= 4; ++y)
            pl.fill(x, y, 0, x, y, r.nextInt(8), kBrick);

    pl.fill(0, 5, 0, 0, 5, r.nextInt(8), kBrick);
    pl.fill(4, 5, 0, 4, 5, r.nextInt(8), kBrick);

    for (int x = 0; x <= 4; ++x)
        pl.fill(x, 2, 0, x, 2, r.nextInt(5), kBrick);

    for (int x = 0; x <= 4; ++x)
        for (int y = 0; y <= 1; ++y)
            pl.fill(x, y, 0, x, y, r.nextInt(3), kBrick);
}

void placeBridgeStraight(Placer &pl)
{
    const int o = pl.piece.dir;

    pl.fill(0, 3, 0, 4, 4, 18, kBrick);
    pl.fill(1, 5, 0, 3, 7, 18, kAir);
    pl.fill(0, 5, 0, 0, 5, 18, kBrick);
    pl.fill(4, 5, 0, 4, 5, 18, kBrick);
    pl.fill(0, 2, 0, 4, 2, 5, kBrick);
    pl.fill(0, 2, 13, 4, 2, 18, kBrick);
    pl.fill(0, 0, 0, 4, 1, 3, kBrick);
    pl.fill(0, 0, 15, 4, 1, 18, kBrick);

    for (int x = 0; x <= 4; ++x)
    {
        for (int z = 0; z <= 2; ++z)
        {
            pl.fillDown(kBrick, x, -1, z);
            pl.fillDown(kBrick, x, -1, 18 - z);
        }
    }

    // 两侧墙上的栏杆窗（代码方位 N+S，再按朝向补 E / W）
    const QString fEast = fenceState(true, true, true, false, o);
    const QString fWest = fenceState(true, false, true, true, o);
    pl.fill(0, 1, 1, 0, 4, 1, fEast);
    pl.fill(0, 3, 4, 0, 4, 4, fEast);
    pl.fill(0, 3, 14, 0, 4, 14, fEast);
    pl.fill(0, 1, 17, 0, 4, 17, fEast);
    pl.fill(4, 1, 1, 4, 4, 1, fWest);
    pl.fill(4, 3, 4, 4, 4, 4, fWest);
    pl.fill(4, 3, 14, 4, 4, 14, fWest);
    pl.fill(4, 1, 17, 4, 4, 17, fWest);
}

void placeCastleStairs(Placer &pl)
{
    const int o = pl.piece.dir;
    const QString stairs = stairState(DIR_SOUTH, o);
    const QString fenceNS = fenceState(true, false, true, false, o);

    for (int k = 0; k <= 9; ++k)
    {
        const int h = std::max(1, 7 - k);
        const int top = std::min(std::max(h + 5, 14 - k), 13);
        pl.fill(0, 0, k, 4, h, k, kBrick);
        pl.fill(1, h + 1, k, 3, top - 1, k, kAir);
        if (k <= 6)
        {
            pl.put(stairs, 1, h + 1, k);
            pl.put(stairs, 2, h + 1, k);
            pl.put(stairs, 3, h + 1, k);
        }
        pl.fill(0, top, k, 4, top, k, kBrick);
        pl.fill(0, h + 1, k, 0, top - 1, k, kBrick);
        pl.fill(4, h + 1, k, 4, top - 1, k, kBrick);
        if ((k & 1) == 0)
        {
            pl.fill(0, h + 2, k, 0, h + 3, k, fenceNS);
            pl.fill(4, h + 2, k, 4, h + 3, k, fenceNS);
        }
        for (int x = 0; x <= 4; ++x)
            pl.fillDown(kBrick, x, -1, k);
    }
}

void placeCastleBalcony(Placer &pl)
{
    const int o = pl.piece.dir;
    const QString fenceNS = fenceState(true, false, true, false, o);
    const QString fenceSE = fenceState(false, true, true, false, o);

    pl.fill(0, 0, 0, 8, 1, 8, kBrick);
    pl.fill(0, 2, 0, 8, 5, 8, kAir);
    pl.fill(0, 6, 0, 8, 6, 5, kBrick);
    pl.fill(0, 2, 0, 2, 5, 0, kBrick);
    pl.fill(6, 2, 0, 8, 5, 0, kBrick);
    pl.fill(1, 3, 0, 1, 4, 0, fenceSE);
    pl.fill(7, 3, 0, 7, 4, 0, fenceSE);
    pl.fill(0, 2, 4, 8, 2, 8, kBrick);
    pl.fill(1, 1, 4, 2, 2, 4, kAir);
    pl.fill(6, 1, 4, 7, 2, 4, kAir);
    pl.fill(1, 3, 8, 7, 3, 8, fenceSE);
    pl.put(fenceState(false, true, true, false, o), 0, 3, 8);
    pl.put(fenceState(false, false, true, true, o), 8, 3, 8);
    pl.fill(0, 3, 6, 0, 3, 7, fenceNS);
    pl.fill(8, 3, 6, 8, 3, 7, fenceNS);
    pl.fill(0, 3, 4, 0, 5, 5, kBrick);
    pl.fill(8, 3, 4, 8, 5, 5, kBrick);
    pl.fill(1, 3, 5, 2, 5, 5, kBrick);
    pl.fill(6, 3, 5, 7, 5, 5, kBrick);
    pl.fill(1, 4, 5, 1, 5, 5, fenceSE);
    pl.fill(7, 4, 5, 7, 5, 5, fenceSE);

    for (int z = 0; z <= 5; ++z)
        for (int x = 0; x <= 8; ++x)
            pl.fillDown(kBrick, x, -1, z);
}

void placeCastleEntrance(Placer &pl)
{
    const int o = pl.piece.dir;

    pl.fill(0, 3, 0, 12, 4, 12, kBrick);
    pl.fill(0, 5, 0, 12, 13, 12, kAir);
    pl.fill(0, 5, 0, 1, 12, 12, kBrick);
    pl.fill(11, 5, 0, 12, 12, 12, kBrick);
    pl.fill(2, 5, 11, 4, 12, 12, kBrick);
    pl.fill(8, 5, 11, 10, 12, 12, kBrick);
    pl.fill(5, 9, 11, 7, 12, 12, kBrick);
    pl.fill(2, 5, 0, 4, 12, 1, kBrick);
    pl.fill(8, 5, 0, 10, 12, 1, kBrick);
    pl.fill(5, 9, 0, 7, 12, 1, kBrick);
    pl.fill(2, 11, 2, 10, 12, 10, kBrick);
    // 原版这里放的是「默认状态」栏杆（四边都不连）
    pl.fill(5, 8, 0, 7, 8, 0, fenceState(false, false, false, false, o));

    const QString fenceSE = fenceState(false, true, true, false, o);
    const QString fenceNS = fenceState(true, false, true, false, o);

    for (int i = 1; i <= 11; i += 2)
    {
        pl.fill(i, 10, 0, i, 11, 0, fenceSE);
        pl.fill(i, 10, 12, i, 11, 12, fenceSE);
        pl.fill(0, 10, i, 0, 11, i, fenceNS);
        pl.fill(12, 10, i, 12, 11, i, fenceNS);
        pl.put(kBrick, i, 13, 0);
        pl.put(kBrick, i, 13, 12);
        pl.put(kBrick, 0, 13, i);
        pl.put(kBrick, 12, 13, i);
        if (i != 11)
        {
            pl.put(fenceSE, i + 1, 13, 0);
            pl.put(fenceSE, i + 1, 13, 12);
            pl.put(fenceNS, 0, 13, i + 1);
            pl.put(fenceNS, 12, 13, i + 1);
        }
    }

    pl.put(fenceState(true, true, false, false, o), 0, 13, 0);
    pl.put(fenceState(false, true, true, false, o), 0, 13, 12);
    pl.put(fenceState(false, false, true, true, o), 12, 13, 12);
    pl.put(fenceState(true, false, false, true, o), 12, 13, 0);

    for (int i = 3; i <= 9; i += 2)
    {
        pl.fill(1, 7, i, 1, 8, i, fenceState(true, false, true, true, o));
        pl.fill(11, 7, i, 11, 8, i, fenceState(true, true, true, false, o));
    }

    pl.fill(4, 2, 0, 8, 2, 12, kBrick);
    pl.fill(0, 2, 4, 12, 2, 8, kBrick);
    pl.fill(4, 0, 0, 8, 1, 3, kBrick);
    pl.fill(4, 0, 9, 8, 1, 12, kBrick);
    pl.fill(0, 0, 4, 3, 1, 8, kBrick);
    pl.fill(9, 0, 4, 12, 1, 8, kBrick);

    for (int x = 4; x <= 8; ++x)
    {
        for (int z = 0; z <= 2; ++z)
        {
            pl.fillDown(kBrick, x, -1, z);
            pl.fillDown(kBrick, x, -1, 12 - z);
        }
    }
    for (int x = 0; x <= 2; ++x)
    {
        for (int z = 4; z <= 8; ++z)
        {
            pl.fillDown(kBrick, x, -1, z);
            pl.fillDown(kBrick, 12 - x, -1, z);
        }
    }

    pl.fill(5, 5, 5, 7, 5, 7, kBrick);
    pl.fill(6, 1, 6, 6, 4, 6, kAir);
    pl.put(kBrick, 6, 0, 6);
    pl.put(kLava, 6, 5, 6);
}

void placeCastleSmallCrossing(Placer &pl)
{
    pl.fill(0, 0, 0, 4, 1, 4, kBrick);
    pl.fill(0, 2, 0, 4, 5, 4, kAir);
    pl.fill(0, 2, 0, 0, 5, 0, kBrick);
    pl.fill(4, 2, 0, 4, 5, 0, kBrick);
    pl.fill(0, 2, 4, 0, 5, 4, kBrick);
    pl.fill(4, 2, 4, 4, 5, 4, kBrick);
    pl.fill(0, 6, 0, 4, 6, 4, kBrick);
    for (int x = 0; x <= 4; ++x)
        for (int z = 0; z <= 4; ++z)
            pl.fillDown(kBrick, x, -1, z);
}

void placeCastleSmallLeft(Placer &pl)
{
    const int o = pl.piece.dir;
    const QString fenceSE = fenceState(false, true, true, false, o);
    const QString fenceNS = fenceState(true, false, true, false, o);

    pl.fill(0, 0, 0, 4, 1, 4, kBrick);
    pl.fill(0, 2, 0, 4, 5, 4, kAir);
    pl.fill(4, 2, 0, 4, 5, 4, kBrick);
    pl.fill(4, 3, 1, 4, 4, 1, fenceNS);
    pl.fill(4, 3, 3, 4, 4, 3, fenceNS);
    pl.fill(0, 2, 0, 0, 5, 0, kBrick);
    pl.fill(0, 2, 4, 3, 5, 4, kBrick);
    pl.fill(1, 3, 4, 1, 4, 4, fenceSE);
    pl.fill(3, 3, 4, 3, 4, 4, fenceSE);
    if (pl.piece.flag)
        pl.chest(3, 2, 3);
    pl.fill(0, 6, 0, 4, 6, 4, kBrick);
    for (int x = 0; x <= 4; ++x)
        for (int z = 0; z <= 4; ++z)
            pl.fillDown(kBrick, x, -1, z);
}

void placeCastleSmall(Placer &pl)
{
    const int o = pl.piece.dir;
    const QString fenceNS = fenceState(true, false, true, false, o);

    pl.fill(0, 0, 0, 4, 1, 4, kBrick);
    pl.fill(0, 2, 0, 4, 5, 4, kAir);
    pl.fill(0, 2, 0, 0, 5, 4, kBrick);
    pl.fill(4, 2, 0, 4, 5, 4, kBrick);
    pl.fill(0, 3, 1, 0, 4, 1, fenceNS);
    pl.fill(0, 3, 3, 0, 4, 3, fenceNS);
    pl.fill(4, 3, 1, 4, 4, 1, fenceNS);
    pl.fill(4, 3, 3, 4, 4, 3, fenceNS);
    pl.fill(0, 6, 0, 4, 6, 4, kBrick);
    for (int x = 0; x <= 4; ++x)
        for (int z = 0; z <= 4; ++z)
            pl.fillDown(kBrick, x, -1, z);
}

void placeCastleSmallRight(Placer &pl)
{
    const int o = pl.piece.dir;
    const QString fenceSE = fenceState(false, true, true, false, o);
    const QString fenceNS = fenceState(true, false, true, false, o);

    pl.fill(0, 0, 0, 4, 1, 4, kBrick);
    pl.fill(0, 2, 0, 4, 5, 4, kAir);
    pl.fill(0, 2, 0, 0, 5, 4, kBrick);
    pl.fill(0, 3, 1, 0, 4, 1, fenceNS);
    pl.fill(0, 3, 3, 0, 4, 3, fenceNS);
    pl.fill(4, 2, 0, 4, 5, 0, kBrick);
    pl.fill(1, 2, 4, 4, 5, 4, kBrick);
    pl.fill(1, 3, 4, 1, 4, 4, fenceSE);
    pl.fill(3, 3, 4, 3, 4, 4, fenceSE);
    if (pl.piece.flag)
        pl.chest(1, 2, 3);
    pl.fill(0, 6, 0, 4, 6, 4, kBrick);
    for (int x = 0; x <= 4; ++x)
        for (int z = 0; z <= 4; ++z)
            pl.fillDown(kBrick, x, -1, z);
}

void placeCastleStalkRoom(Placer &pl)
{
    const int o = pl.piece.dir;

    pl.fill(0, 3, 0, 12, 4, 12, kBrick);
    pl.fill(0, 5, 0, 12, 13, 12, kAir);
    pl.fill(0, 5, 0, 1, 12, 12, kBrick);
    pl.fill(11, 5, 0, 12, 12, 12, kBrick);
    pl.fill(2, 5, 11, 4, 12, 12, kBrick);
    pl.fill(8, 5, 11, 10, 12, 12, kBrick);
    pl.fill(5, 9, 11, 7, 12, 12, kBrick);
    pl.fill(2, 5, 0, 4, 12, 1, kBrick);
    pl.fill(8, 5, 0, 10, 12, 1, kBrick);
    pl.fill(5, 9, 0, 7, 12, 1, kBrick);
    pl.fill(2, 11, 2, 10, 12, 10, kBrick);

    const QString fenceSE = fenceState(false, true, true, false, o);
    const QString fenceNS = fenceState(true, false, true, false, o);

    for (int i = 1; i <= 11; i += 2)
    {
        pl.fill(i, 10, 0, i, 11, 0, fenceSE);
        pl.fill(i, 10, 12, i, 11, 12, fenceSE);
        pl.fill(0, 10, i, 0, 11, i, fenceNS);
        pl.fill(12, 10, i, 12, 11, i, fenceNS);
        pl.put(kBrick, i, 13, 0);
        pl.put(kBrick, i, 13, 12);
        pl.put(kBrick, 0, 13, i);
        pl.put(kBrick, 12, 13, i);
        if (i != 11)
        {
            pl.put(fenceSE, i + 1, 13, 0);
            pl.put(fenceSE, i + 1, 13, 12);
            pl.put(fenceNS, 0, 13, i + 1);
            pl.put(fenceNS, 12, 13, i + 1);
        }
    }

    pl.put(fenceState(true, true, false, false, o), 0, 13, 0);
    pl.put(fenceState(false, true, true, false, o), 0, 13, 12);
    pl.put(fenceState(false, false, true, true, o), 12, 13, 12);
    pl.put(fenceState(true, false, false, true, o), 12, 13, 0);

    for (int i = 3; i <= 9; i += 2)
    {
        pl.fill(1, 7, i, 1, 8, i, fenceState(true, false, true, true, o));
        pl.fill(11, 7, i, 11, 8, i, fenceState(true, true, true, false, o));
    }

    // 下界疣楼梯塔（通往塔顶的两段楼梯）
    const QString stairsNorth = stairState(DIR_NORTH, o);
    for (int i = 0; i <= 6; ++i)
    {
        const int z = i + 4;
        for (int x = 5; x <= 7; ++x)
            pl.put(stairsNorth, x, 5 + i, z);
        if (z >= 5 && z <= 8)
            pl.fill(5, 5, z, 7, i + 4, z, kBrick);
        else if (z >= 9 && z <= 10)
            pl.fill(5, 8, z, 7, i + 4, z, kBrick);
        if (i >= 1)
            pl.fill(5, 6 + i, z, 7, 9 + i, z, kAir);
    }

    for (int x = 5; x <= 7; ++x)
        pl.put(stairsNorth, x, 12, 11);

    // 瘤塔两侧的楼梯（东西向）
    const QString stairsWest = stairState(DIR_WEST, o);
    const QString stairsEast = stairState(DIR_EAST, o);
    pl.fill(5, 6, 7, 5, 7, 7, fenceState(true, true, true, false, o));
    pl.fill(7, 6, 7, 7, 7, 7, fenceState(true, false, true, true, o));
    pl.fill(5, 13, 12, 7, 13, 12, kAir);
    pl.fill(2, 5, 2, 3, 5, 3, kBrick);
    pl.fill(2, 5, 9, 3, 5, 10, kBrick);
    pl.fill(2, 5, 4, 2, 5, 8, kBrick);
    pl.fill(9, 5, 2, 10, 5, 3, kBrick);
    pl.fill(9, 5, 9, 10, 5, 10, kBrick);
    pl.fill(10, 5, 4, 10, 5, 8, kBrick);
    pl.put(stairsWest, 4, 5, 2);
    pl.put(stairsWest, 4, 5, 3);
    pl.put(stairsWest, 4, 5, 9);
    pl.put(stairsWest, 4, 5, 10);
    pl.put(stairsEast, 8, 5, 2);
    pl.put(stairsEast, 8, 5, 3);
    pl.put(stairsEast, 8, 5, 9);
    pl.put(stairsEast, 8, 5, 10);

    // 下界疣农田：灵魂沙 + 下界疣
    pl.fill(3, 4, 4, 4, 4, 8, kSoulSand);
    pl.fill(8, 4, 4, 9, 4, 8, kSoulSand);
    pl.fill(3, 5, 4, 4, 5, 8, wartState());
    pl.fill(8, 5, 4, 9, 5, 8, wartState());

    pl.fill(4, 2, 0, 8, 2, 12, kBrick);
    pl.fill(0, 2, 4, 12, 2, 8, kBrick);
    pl.fill(4, 0, 0, 8, 1, 3, kBrick);
    pl.fill(4, 0, 9, 8, 1, 12, kBrick);
    pl.fill(0, 0, 4, 3, 1, 8, kBrick);
    pl.fill(9, 0, 4, 12, 1, 8, kBrick);

    for (int x = 4; x <= 8; ++x)
    {
        for (int z = 0; z <= 2; ++z)
        {
            pl.fillDown(kBrick, x, -1, z);
            pl.fillDown(kBrick, x, -1, 12 - z);
        }
    }
    for (int x = 0; x <= 2; ++x)
    {
        for (int z = 4; z <= 8; ++z)
        {
            pl.fillDown(kBrick, x, -1, z);
            pl.fillDown(kBrick, 12 - x, -1, z);
        }
    }
}

void placeMonsterThrone(Placer &pl)
{
    const int o = pl.piece.dir;

    pl.fill(0, 2, 0, 6, 7, 7, kAir);
    pl.fill(1, 0, 0, 5, 1, 7, kBrick);
    pl.fill(1, 2, 1, 5, 2, 7, kBrick);
    pl.fill(1, 3, 2, 5, 3, 7, kBrick);
    pl.fill(1, 4, 3, 5, 4, 7, kBrick);
    pl.fill(1, 2, 0, 1, 4, 2, kBrick);
    pl.fill(5, 2, 0, 5, 4, 2, kBrick);
    pl.fill(1, 5, 2, 1, 5, 3, kBrick);
    pl.fill(5, 5, 2, 5, 5, 3, kBrick);
    pl.fill(0, 5, 3, 0, 5, 8, kBrick);
    pl.fill(6, 5, 3, 6, 5, 8, kBrick);
    pl.fill(1, 5, 8, 5, 5, 8, kBrick);

    const QString fenceSE = fenceState(false, true, true, false, o);
    const QString fenceS = fenceState(false, false, true, false, o);
    const QString fenceE = fenceState(false, true, false, false, o);
    const QString fenceNE = fenceState(true, true, false, false, o);
    const QString fenceSN = fenceState(true, false, true, false, o);
    const QString fenceSW = fenceState(false, false, true, true, o);

    pl.put(fenceS, 1, 6, 3);
    pl.put(fenceE, 5, 6, 3);
    pl.put(fenceNE, 0, 6, 3);
    pl.put(fenceSN, 6, 6, 3);
    pl.fill(0, 6, 4, 0, 6, 7, fenceSN);
    pl.fill(6, 6, 4, 6, 6, 7, fenceSN);
    pl.put(fenceSE, 0, 6, 8);
    pl.put(fenceSW, 6, 6, 8);
    pl.fill(1, 6, 8, 5, 6, 8, fenceSE);
    pl.put(fenceE, 1, 7, 8);
    pl.fill(2, 7, 8, 4, 7, 8, fenceSE);
    pl.put(fenceS, 5, 7, 8);
    pl.put(fenceE, 2, 8, 8);
    pl.put(fenceSE, 3, 8, 8);
    pl.put(fenceS, 4, 8, 8);

    if (!pl.piece.flag)
        pl.put(kSpawner, 3, 5, 5);

    for (int x = 0; x <= 6; ++x)
        for (int z = 0; z <= 6; ++z)
            pl.fillDown(kBrick, x, -1, z);
}

void placeRoomCrossing(Placer &pl)
{
    const int o = pl.piece.dir;

    pl.fill(0, 0, 0, 6, 1, 6, kBrick);
    pl.fill(0, 2, 0, 6, 7, 6, kAir);
    pl.fill(0, 2, 0, 1, 6, 0, kBrick);
    pl.fill(0, 2, 6, 1, 6, 6, kBrick);
    pl.fill(5, 2, 0, 6, 6, 0, kBrick);
    pl.fill(5, 2, 6, 6, 6, 6, kBrick);
    pl.fill(0, 2, 0, 0, 6, 1, kBrick);
    pl.fill(0, 2, 5, 0, 6, 6, kBrick);
    pl.fill(6, 2, 0, 6, 6, 1, kBrick);
    pl.fill(6, 2, 5, 6, 6, 6, kBrick);

    const QString fenceSE = fenceState(false, true, true, false, o);
    const QString fenceNS = fenceState(true, false, true, false, o);

    pl.fill(2, 6, 0, 4, 6, 0, kBrick);
    pl.fill(2, 5, 0, 4, 5, 0, fenceSE);
    pl.fill(2, 6, 6, 4, 6, 6, kBrick);
    pl.fill(2, 5, 6, 4, 5, 6, fenceSE);
    pl.fill(0, 6, 2, 0, 6, 4, kBrick);
    pl.fill(0, 5, 2, 0, 5, 4, fenceNS);
    pl.fill(6, 6, 2, 6, 6, 4, kBrick);
    pl.fill(6, 5, 2, 6, 5, 4, fenceNS);

    for (int x = 0; x <= 6; ++x)
        for (int z = 0; z <= 6; ++z)
            pl.fillDown(kBrick, x, -1, z);
}

void placeStairsRoom(Placer &pl)
{
    const int o = pl.piece.dir;

    pl.fill(0, 0, 0, 6, 1, 6, kBrick);
    pl.fill(0, 2, 0, 6, 10, 6, kAir);
    pl.fill(0, 2, 0, 1, 8, 0, kBrick);
    pl.fill(5, 2, 0, 6, 8, 0, kBrick);
    pl.fill(0, 2, 1, 0, 8, 6, kBrick);
    pl.fill(6, 2, 1, 6, 8, 6, kBrick);
    pl.fill(1, 2, 6, 5, 8, 6, kBrick);

    const QString fenceSE = fenceState(false, true, true, false, o);
    const QString fenceNS = fenceState(true, false, true, false, o);

    pl.fill(0, 3, 2, 0, 5, 4, fenceNS);
    pl.fill(6, 3, 2, 6, 5, 2, fenceNS);
    pl.fill(6, 3, 4, 6, 5, 4, fenceNS);
    pl.put(kBrick, 5, 2, 5);
    pl.fill(4, 2, 5, 4, 3, 5, kBrick);
    pl.fill(3, 2, 5, 3, 4, 5, kBrick);
    pl.fill(2, 2, 5, 2, 5, 5, kBrick);
    pl.fill(1, 2, 5, 1, 6, 5, kBrick);
    pl.fill(1, 7, 1, 5, 7, 4, kBrick);
    pl.fill(6, 8, 2, 6, 8, 4, kAir);
    pl.fill(2, 6, 0, 4, 8, 0, kBrick);
    pl.fill(2, 5, 0, 4, 5, 0, fenceSE);

    for (int x = 0; x <= 6; ++x)
        for (int z = 0; z <= 6; ++z)
            pl.fillDown(kBrick, x, -1, z);
}

void placePiece(const Piece &p, Grid &grid, int startX, int startZ)
{
    Placer pl{grid, p, startX, startZ};
    switch (p.type)
    {
    case T_BRIDGE_CROSSING:       placeBridgeCrossing(pl); break;
    case T_BRIDGE_END_FILLER:     placeBridgeEndFiller(pl); break;
    case T_BRIDGE_STRAIGHT:       placeBridgeStraight(pl); break;
    case T_CASTLE_STAIRS:         placeCastleStairs(pl); break;
    case T_CASTLE_BALCONY:        placeCastleBalcony(pl); break;
    case T_CASTLE_ENTRANCE:       placeCastleEntrance(pl); break;
    case T_CASTLE_SMALL_CROSSING: placeCastleSmallCrossing(pl); break;
    case T_CASTLE_SMALL_LEFT:     placeCastleSmallLeft(pl); break;
    case T_CASTLE_SMALL:          placeCastleSmall(pl); break;
    case T_CASTLE_SMALL_RIGHT:    placeCastleSmallRight(pl); break;
    case T_CASTLE_STALK_ROOM:     placeCastleStalkRoom(pl); break;
    case T_MONSTER_THRONE:        placeMonsterThrone(pl); break;
    case T_ROOM_CROSSING:         placeRoomCrossing(pl); break;
    default:                      placeStairsRoom(pl); break;
    }
}

} // namespace

// ────────────────────────────────────────────────────────────────────────────
// 对外接口
// ────────────────────────────────────────────────────────────────────────────

Model build(quint64 seed, int startChunkX, int startChunkZ, const QString &mcVersion)
{
    Model out;
    out.startX = startChunkX * 16;
    out.startZ = startChunkZ * 16;

    // 1.16 之前要塞布局完全不同，这里只还原 1.16+ 的规则
    bool okVer = true;
    const QStringList parts = mcVersion.split(QLatin1Char('.'));
    if (parts.size() >= 2)
    {
        const int minor = parts[1].toInt(&okVer);
        const int major = parts[0].toInt();
        okVer = okVer && (major > 1 || (major == 1 && minor >= 16));
    }
    if (!okVer)
    {
        out.error = QStringLiteral("该版本的下界要塞布局不同，暂不预览");
        return out;
    }

    Gen gen;
    gen.seedValue = seed;
    if (!gen.run(startChunkX, startChunkZ) || gen.pieces.empty())
    {
        out.error = QStringLiteral("下界要塞拼装失败");
        return out;
    }

    Grid grid;
    for (const Piece *p : gen.pieces)
        placePiece(*p, grid, out.startX, out.startZ);

    // 栏杆落地后原版会按邻居重算四边连接：这里做同样的收尾，
    // 让墙角、墙头与相邻下界砖接上，不至于出现断开的小缝
    {
        auto solidState = [](const QString &s) {
            return s.startsWith(QLatin1String("minecraft:nether_bricks"))
                || s.startsWith(QLatin1String("minecraft:nether_brick_stairs"))
                || s.startsWith(QLatin1String("minecraft:soul_sand"))
                || s.startsWith(QLatin1String("minecraft:spawner"))
                || s.startsWith(QLatin1String("minecraft:chest"));
        };
        auto connectTo = [&](int x, int y, int z) {
            const QString s = grid.at(x, y, z);
            return s.startsWith(QLatin1String("minecraft:nether_brick_fence"))
                || solidState(s);
        };

        QVector<qint64> fences;
        for (auto it = grid.cells.constBegin(); it != grid.cells.constEnd(); ++it)
            if (it.value().startsWith(QLatin1String("minecraft:nether_brick_fence")))
                fences.append(it.key());
        for (qint64 k : fences)
        {
            int x, y, z;
            unpackKey(k, x, y, z);
            const bool n = connectTo(x, y, z - 1);
            const bool s = connectTo(x, y, z + 1);
            const bool w = connectTo(x - 1, y, z);
            const bool e = connectTo(x + 1, y, z);
            grid.cells.insert(k,
                QStringLiteral("minecraft:nether_brick_fence[east=%1,north=%2,south=%3,waterlogged=false,west=%4]")
                    .arg(e ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(n ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(s ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(w ? QStringLiteral("true") : QStringLiteral("false")));
        }
    }

    out.blocks = grid.cells;
    out.blockCount = grid.cells.size();

    bool first = true;
    for (auto it = grid.cells.constBegin(); it != grid.cells.constEnd(); ++it)
    {
        int x, y, z;
        unpackKey(it.key(), x, y, z);
        if (first)
        {
            out.x0 = out.x1 = x;
            out.y0 = out.y1 = y;
            out.z0 = out.z1 = z;
            first = false;
        }
        else
        {
            out.x0 = std::min(out.x0, x);
            out.x1 = std::max(out.x1, x);
            out.y0 = std::min(out.y0, y);
            out.y1 = std::max(out.y1, y);
            out.z0 = std::min(out.z0, z);
            out.z1 = std::max(out.z1, z);
        }
    }

    for (const Piece *p : gen.pieces)
    {
        PieceBox pb;
        pb.type = QString::fromLatin1(typeName(p->type));
        pb.x0 = p->box.x0; pb.y0 = p->box.y0; pb.z0 = p->box.z0;
        pb.x1 = p->box.x1; pb.y1 = p->box.y1; pb.z1 = p->box.z1;
        out.pieces.append(pb);
    }

    out.ok = true;
    out.variant = QStringLiteral("下界要塞");
    return out;
}

} // namespace FortressModel
