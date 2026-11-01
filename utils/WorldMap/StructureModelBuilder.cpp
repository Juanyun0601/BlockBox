/**
 * @file   StructureModelBuilder.cpp
 * @brief  按世界种子程序化构建结构三维模型（结构详情「预览」页）
 * @author BlockBox Team
 * @date   2026-10-26
 */

#include "utils/WorldMap/StructureModelBuilder.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <algorithm>
#include <cmath>

#include "utils/Schematic/SchematicDocument.h"
#include "utils/WorldMap/McRandom.h"
#include "utils/WorldMap/BiomeCatalog.h"
#include "utils/WorldMap/StructureFinder.h"
#include "utils/WorldMap/StructureTemplates.h"
#include "utils/WorldMap/StrongholdModel.h"
#include "utils/WorldMap/FortressModel.h"
#include "utils/WorldMap/MineshaftModel.h"
#include "util.h"  // biome2str（cubiomes 群系 id → 资源名）
#include "biomes.h"  // MC_1_14 等版本枚举

namespace {

// ────────────────────────────────────────────────────────────────────────────
// 小工具
// ────────────────────────────────────────────────────────────────────────────

QString st(const char *s) { return QString::fromLatin1(s); }

/** 方块状态串（含属性），用于台阶 / 栏杆等需要朝向的方块 */
QString stair(const char *name, const char *facing)
{
    return st(name) + QStringLiteral("[facing=") + st(facing)
        + QStringLiteral(",half=bottom,shape=straight]");
}

/** 结构模型内的确定性随机源（种子 + 坐标混合） */
struct Rng
{
    McRandom::Xoroshiro r;
    explicit Rng(quint64 seed) : r(seed) {}
    int range(int lo, int hi) { return r.nextIntBetween(lo, hi); }
    int next(int n) { return r.nextInt(n); }
    bool chance(int percent) { return r.nextInt(100) < percent; }
};

/** 体积键：10 位一轴，支持 -256..255 */
inline quint64 packed(int x, int y, int z)
{
    return quint64(x + 256) | (quint64(y + 256) << 10) | (quint64(z + 256) << 20);
}
inline void unpack(quint64 k, int &x, int &y, int &z)
{
    x = int(k & 1023) - 256;
    y = int((k >> 10) & 1023) - 256;
    z = int((k >> 20) & 1023) - 256;
}

/** 方块状态随模型整体旋转时的朝向映射 */
QString rotateState(const QString &state, int turns)
{
    turns = ((turns % 4) + 4) % 4;
    if (turns == 0)
        return state;
    SchematicBlockState s = SchematicBlockState::fromString(state);
    static const char *dirs[] = {"north", "east", "south", "west"};
    auto turnDir = [turns](QString d) -> QString {
        for (int i = 0; i < 4; ++i)
            if (d == QLatin1String(dirs[i]))
                return QLatin1String(dirs[(i + turns) % 4]);
        return d;
    };
    if (s.properties.contains(QStringLiteral("facing")))
        s.properties[QStringLiteral("facing")] =
            turnDir(s.properties.value(QStringLiteral("facing")));
    if (s.properties.contains(QStringLiteral("axis")))
    {
        const QString axis = s.properties.value(QStringLiteral("axis"));
        if (axis != QLatin1String("y") && turns % 2 == 1)
            s.properties[QStringLiteral("axis")] =
                axis == QLatin1String("x") ? QStringLiteral("z")
                                           : QStringLiteral("x");
    }
    // 铁轨形状：南北 ↔ 东西（楼梯的 inner/outer 形状只跟朝向有关，转 90° 不变）
    if (s.properties.contains(QStringLiteral("shape")))
    {
        static const struct { const char *a; const char *b; } pairs[] = {
            {"north_south", "east_west"},
            {"ascending_north", "ascending_east"},
            {"ascending_south", "ascending_west"},
        };
        QString shape = s.properties.value(QStringLiteral("shape"));
        if (turns % 2 == 1)
        {
            for (const auto &p : pairs)
            {
                if (shape == QLatin1String(p.a))
                {
                    shape = QLatin1String(p.b);
                    break;
                }
                if (shape == QLatin1String(p.b))
                {
                    shape = QLatin1String(p.a);
                    break;
                }
            }
        }
        s.properties[QStringLiteral("shape")] = shape;
    }
    // 栅栏 / 墙 / 藤蔓等四向连接属性：整体转 90° 时连接方向跟着转
    {
        static const char *const conn[] = {"north", "east", "south", "west"};
        bool hasConn = false;
        for (const char *key : conn)
        {
            if (s.properties.contains(QLatin1String(key)))
            {
                hasConn = true;
                break;
            }
        }
        if (hasConn)
        {
            const QMap<QString, QString> before = s.properties;
            for (int i = 0; i < 4; ++i)
            {
                const QLatin1String from(conn[i]);
                const QLatin1String to(conn[(i + turns) % 4]);
                if (before.contains(from))
                    s.properties[to] = before.value(from);
            }
        }
    }
    // 告示牌 / 头颅等的 16 向 rotation
    if (s.properties.contains(QStringLiteral("rotation")))
    {
        bool ok = false;
        int rot = s.properties.value(QStringLiteral("rotation")).toInt(&ok);
        if (ok)
            s.properties[QStringLiteral("rotation")] =
                QString::number((rot + turns * 4) % 16);
    }
    return s.toString();
}

/**
 * @brief 结构模型的方块画布（稀疏存储 + 整体旋转 + 裁剪成文档）
 */
class Canvas
{
public:
    QHash<quint64, QString> cells;

    void put(int x, int y, int z, const QString &s)
    {
        if (s.isEmpty() || s == QLatin1String("minecraft:air"))
            cells.remove(packed(x, y, z));
        else
            cells.insert(packed(x, y, z), s);
    }
    void carve(int x, int y, int z) { cells.remove(packed(x, y, z)); }
    QString at(int x, int y, int z) const { return cells.value(packed(x, y, z)); }

    void fill(int x0, int y0, int z0, int x1, int y1, int z1, const QString &s)
    {
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);
        if (z0 > z1) std::swap(z0, z1);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z)
                    put(x, y, z, s);
    }
    void carveBox(int x0, int y0, int z0, int x1, int y1, int z1)
    {
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);
        if (z0 > z1) std::swap(z0, z1);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z)
                    carve(x, y, z);
    }

    /** 只砌四面墙（含四角） */
    void walls(int x0, int y0, int z0, int x1, int y1, int z1,
               const QString &s)
    {
        if (x0 > x1) std::swap(x0, x1);
        if (z0 > z1) std::swap(z0, z1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (int z = z0; z <= z1; ++z)
                    if (x == x0 || x == x1 || z == z0 || z == z1)
                        put(x, y, z, s);
    }

    /** 墙 + 地板 + 天花板 */
    void hollowBox(int x0, int y0, int z0, int x1, int y1, int z1,
                   const QString &wall, const QString &floorS,
                   const QString &ceilS)
    {
        walls(x0, y0, z0, x1, y1, z1, wall);
        if (!floorS.isEmpty())
            fill(x0, y0, z0, x1, y0, z1, floorS);
        if (!ceilS.isEmpty())
            fill(x0, y1, z0, x1, y1, z1, ceilS);
    }

    void pillar(int x, int z, int y0, int y1, const QString &s)
    {
        fill(x, y0, z, x, y1, z, s);
    }

    /** 方形环（不含内部） */
    void ring(int x0, int z0, int x1, int z1, int y, const QString &s)
    {
        if (x0 > x1) std::swap(x0, x1);
        if (z0 > z1) std::swap(z0, z1);
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
                if (x == x0 || x == x1 || z == z0 || z == z1)
                    put(x, y, z, s);
    }

    void bounds(int &x0, int &y0, int &z0, int &x1, int &y1, int &z1) const
    {
        x0 = y0 = z0 = 1 << 30;
        x1 = y1 = z1 = -(1 << 30);
        for (auto it = cells.constBegin(); it != cells.constEnd(); ++it)
        {
            int x, y, z;
            unpack(it.key(), x, y, z);
            x0 = std::min(x0, x); x1 = std::max(x1, x);
            y0 = std::min(y0, y); y1 = std::max(y1, y);
            z0 = std::min(z0, z); z1 = std::max(z1, z);
        }
    }

    void rotate(int turns)
    {
        turns = ((turns % 4) + 4) % 4;
        if (turns == 0)
            return;
        QHash<quint64, QString> out;
        out.reserve(cells.size());
        QHash<QString, QString> cache;
        for (auto it = cells.constBegin(); it != cells.constEnd(); ++it)
        {
            int x, y, z;
            unpack(it.key(), x, y, z);
            int nx = x, nz = z;
            switch (turns)
            {
            case 1: nx = -z; nz = x; break;
            case 2: nx = -x; nz = -z; break;
            default: nx = z; nz = -x; break;
            }
            QString rs = cache.value(it.value());
            if (rs.isEmpty())
            {
                rs = rotateState(it.value(), turns);
                cache.insert(it.value(), rs);
            }
            out.insert(packed(nx, y, nz), rs);
        }
        cells.swap(out);
    }

    /** 地表圆盘：顶面 + 若干层底，边缘按种子轻微参差 */
    void ground(Rng &rng, int rx, int rz, const QString &top,
                const QString &under, int depth = 2)
    {
        for (int x = -rx; x <= rx; ++x)
            for (int z = -rz; z <= rz; ++z)
            {
                const double nx = double(x) / double(std::max(1, rx));
                const double nz = double(z) / double(std::max(1, rz));
                const double jitter = (rng.next(101) - 50) * 0.0022;
                if (nx * nx + nz * nz > 1.0 + jitter)
                    continue;
                put(x, 0, z, top);
                if (depth > 1)
                    for (int d = 1; d < depth; ++d)
                        put(x, -d, z, under);
            }
    }

    /** 地表水层（放在 ground 之上） */
    void water(Rng &rng, int rx, int rz, int fromY = 1, int depth = 2)
    {
        for (int x = -rx; x <= rx; ++x)
            for (int z = -rz; z <= rz; ++z)
            {
                const double nx = double(x) / double(std::max(1, rx));
                const double nz = double(z) / double(std::max(1, rz));
                if (nx * nx + nz * nz > 1.02)
                    continue;
                for (int d = 0; d < depth; ++d)
                    put(x, fromY + d, z, st("minecraft:water[level=0]"));
            }
    }
};

/** 四坡屋顶：每层向内收一圈台阶，顶上压一块盖板 */
void hipRoof(Canvas &c, int x0, int z0, int x1, int z1, int y,
             const QString &stairName, const QString &cap)
{
    int level = 0;
    while (x0 <= x1 && z0 <= z1)
    {
        if (x1 - x0 <= 1 || z1 - z0 <= 1)
        {
            c.fill(x0, y + level, z0, x1, y + level, z1, cap);
            return;
        }
        const QString north = stair(stairName.toLatin1().constData(), "north");
        const QString south = stair(stairName.toLatin1().constData(), "south");
        const QString west = stair(stairName.toLatin1().constData(), "west");
        const QString east = stair(stairName.toLatin1().constData(), "east");
        for (int x = x0; x <= x1; ++x)
        {
            c.put(x, y + level, z0, north);
            c.put(x, y + level, z1, south);
        }
        for (int z = z0 + 1; z < z1; ++z)
        {
            c.put(x0, y + level, z, west);
            c.put(x1, y + level, z, east);
        }
        ++level;
        ++x0; --x1; ++z0; --z1;
    }
}

/** 坡屋顶（屋脊沿 X）：两面台阶 + 屋脊盖板 */
void gableRoof(Canvas &c, int x0, int z0, int x1, int z1, int y,
               const QString &stairName, const QString &cap,
               const QString &under)
{
    int zl = z0, zr = z1, level = 0;
    while (zl <= zr)
    {
        const QString north = stair(stairName.toLatin1().constData(), "north");
        const QString south = stair(stairName.toLatin1().constData(), "south");
        for (int x = x0; x <= x1; ++x)
        {
            if (zl == zr)
                c.put(x, y + level, zl, cap);
            else
            {
                c.put(x, y + level, zl, north);
                c.put(x, y + level, zr, south);
            }
            if (level > 0 && !under.isEmpty())
            {
                // 屋顶下的空气在斜面上会露缝，补一层屋面板
                c.put(x, y + level - 1, zl, under);
                if (zl != zr)
                    c.put(x, y + level - 1, zr, under);
            }
        }
        ++level;
        ++zl;
        --zr;
    }
}

/** 小树：原木树干 + 叶团 */
void tree(Canvas &c, Rng &rng, int x, int z, const QString &log,
          const QString &leaves, int height = 4)
{
    c.pillar(x, z, 1, height, log);
    const int r = rng.chance(50) ? 2 : 1;
    for (int dx = -r; dx <= r; ++dx)
        for (int dz = -r; dz <= r; ++dz)
            for (int dy = 0; dy <= r; ++dy)
            {
                if (dx * dx + dz * dz + dy * dy > r * r + 1)
                    continue;
                if (c.at(x + dx, height + dy, z + dz).isEmpty())
                    c.put(x + dx, height + dy, z + dz, leaves);
            }
}

/** 带窗带与转角柱的塔楼（不含屋顶） */
void towerBody(Canvas &c, int x0, int z0, int w, int d, int y0, int y1,
               const QString &wall, const QString &corner,
               const QString &glass, bool windows)
{
    c.walls(x0, y0, z0, x0 + w - 1, y1, z0 + d - 1, wall);
    const int x1 = x0 + w - 1, z1 = z0 + d - 1;
    for (int y = y0; y <= y1; ++y)
    {
        c.put(x0, y, z0, corner);
        c.put(x1, y, z0, corner);
        c.put(x0, y, z1, corner);
        c.put(x1, y, z1, corner);
    }
    if (!windows)
        return;
    for (int y = y0 + 1; y <= y1; y += 3)
        for (int x = x0 + 2; x <= x1 - 2; x += 3)
        {
            c.put(x, y, z0, glass);
            c.put(x, y, z1, glass);
        }
    for (int y = y0 + 1; y <= y1; y += 3)
        for (int z = z0 + 2; z <= z1 - 2; z += 3)
        {
            c.put(x0, y, z, glass);
            c.put(x1, y, z, glass);
        }
}

// ────────────────────────────────────────────────────────────────────────────
// 村庄
// ────────────────────────────────────────────────────────────────────────────

struct VillageStyle
{
    const char *wall;
    const char *trim;
    const char *roof;
    const char *cap;
    const char *ground;
    const char *name;
};

VillageStyle villageStyleFor(Rng &rng)
{
    static const VillageStyle styles[] = {
        {"minecraft:oak_planks", "minecraft:oak_log",
         "minecraft:dark_oak_stairs", "minecraft:dark_oak_slab",
         "minecraft:grass_block", "平原村庄"},
        {"minecraft:spruce_planks", "minecraft:spruce_log",
         "minecraft:spruce_stairs", "minecraft:spruce_slab",
         "minecraft:grass_block", "针叶林村庄"},
        {"minecraft:acacia_planks", "minecraft:acacia_log",
         "minecraft:acacia_stairs", "minecraft:acacia_slab",
         "minecraft:coarse_dirt", "热带草原村庄"},
        {"minecraft:sandstone", "minecraft:chiseled_sandstone",
         "minecraft:smooth_sandstone_stairs", "minecraft:smooth_sandstone",
         "minecraft:sand", "沙漠村庄"},
        {"minecraft:spruce_planks", "minecraft:stripped_spruce_log",
         "minecraft:spruce_stairs", "minecraft:spruce_slab",
         "minecraft:snow_block", "雪原村庄"},
    };
    return styles[rng.next(int(sizeof(styles) / sizeof(styles[0])))];
}

void villageHouse(Canvas &c, Rng &rng, const VillageStyle &vs,
                  int x0, int z0, int w, int d, int wallH)
{
    const QString wall = st(vs.wall);
    const QString trim = st(vs.trim);
    const QString glass = st("minecraft:glass");
    const QString floorS = st("minecraft:oak_planks");
    const int x1 = x0 + w - 1, z1 = z0 + d - 1;

    // 地基
    c.fill(x0, 0, z0, x1, 1, z1, st("minecraft:cobblestone"));
    // 地板与墙
    c.fill(x0, 1, z0, x1, 1, z1, floorS);
    towerBody(c, x0, z0, w, d, 2, 1 + wallH, wall, trim, glass, true);
    // 门：南面正中开 2 高
    const int dx = (x0 + x1) / 2;
    c.carveBox(dx, 2, z1, dx, 3, z1);
    c.put(dx, 4, z1, st("minecraft:oak_slab[type=bottom]"));
    // 门口台阶
    if (!c.at(dx, 0, z1 + 1).isEmpty())
        c.put(dx, 1, z1 + 1, st("minecraft:cobblestone_stairs[facing=south]"));
    // 屋顶
    hipRoof(c, x0 - 1, z0 - 1, x1 + 1, z1 + 1, 2 + wallH,
            st(vs.roof), st(vs.cap));
    // 屋内：床、工作台、火把
    if (w >= 7)
    {
        c.fill(x0 + 2, 2, z0 + 2, x0 + 3, 2, z0 + 2, st("minecraft:red_wool"));
        c.put(x0 + 2, 2, z0 + 3, st("minecraft:crafting_table"));
        c.put(x1 - 2, 2, z1 - 2, st("minecraft:torch"));
    }
    (void)rng;
}

void buildVillage(Canvas &c, Rng &rng, const QString & /*mc*/,
                  StructureModelInfo *out)
{
    const VillageStyle vs = villageStyleFor(rng);
    out->variant = QString::fromUtf8(vs.name);
    c.ground(rng, 23, 23, st(vs.ground), st("minecraft:dirt"), 2);

    // 井
    c.ring(-2, -2, 2, 2, 1, st("minecraft:cobblestone"));
    c.fill(-1, 1, -1, 1, 1, 1, st("minecraft:water[level=0]"));
    for (int px : {-2, 2})
        for (int pz : {-2, 2})
            c.pillar(px, pz, 2, 3, st("minecraft:oak_fence"));
    c.fill(-2, 4, -2, 2, 4, 2, st("minecraft:oak_slab[type=top]"));
    c.put(0, 5, 0, st("minecraft:lantern"));

    // 房屋：围绕中心一圈
    struct HouseSlot { int x, z, w, d, h; };
    const HouseSlot houses[] = {
        {-17, -14, 9, 7, 5}, {7, -17, 7, 9, 5}, {16, -5, 9, 7, 4},
        {-18, 4, 7, 9, 5},   {5, 16, 9, 7, 5},   {-8, 17, 7, 7, 4},
    };
    for (const HouseSlot &s : houses)
        villageHouse(c, rng, vs, s.x, s.z, s.w, s.d, s.h);

    // 农田：耕地 + 水渠 + 作物
    const int fx = 13, fz = 10;
    c.fill(fx, 0, fz, fx + 8, 0, fz + 6, st("minecraft:farmland[moisture=7]"));
    for (int x = fx; x <= fx + 8; ++x)
        for (int z = fz; z <= fz + 6; ++z)
        {
            if (z == fz + 3)
            {
                c.put(x, 0, z, st("minecraft:water[level=0]"));
                c.put(x, 1, z, st("minecraft:water[level=0]"));
            }
            else if ((x + z) % 2 == 0)
                c.put(x, 1, z, st("minecraft:wheat[age=7]"));
        }
    for (int x = fx - 1; x <= fx + 9; ++x)
    {
        c.put(x, 1, fz - 1, st("minecraft:oak_fence"));
        c.put(x, 1, fz + 7, st("minecraft:oak_fence"));
    }

    // 土路：把井与房屋连起来
    const QString path = st("minecraft:dirt_path");
    for (const HouseSlot &s : houses)
    {
        const int cx = s.x + s.w / 2, cz = s.z + s.d / 2;
        for (int x = 0; x != cx; x += (cx > 0 ? 1 : -1))
            c.put(x, 0, 0, path);
        for (int z = 0; z != cz; z += (cz > 0 ? 1 : -1))
            c.put(cx, 0, z, path);
    }

    // 路灯与树
    for (int i = 0; i < 6; ++i)
    {
        const int x = rng.range(-20, 20), z = rng.range(-20, 20);
        if (!c.at(x, 0, z).isEmpty() && c.at(x, 1, z).isEmpty()
            && c.at(x, 2, z).isEmpty())
        {
            c.put(x, 1, z, st("minecraft:oak_fence"));
            c.put(x, 2, z, st("minecraft:lantern"));
        }
    }
    for (int i = 0; i < 5; ++i)
    {
        const int x = rng.range(-21, 21), z = rng.range(-21, 21);
        if (c.at(x, 1, z).isEmpty() && c.at(x, 0, z) == st(vs.ground))
            tree(c, rng, x, z, st("minecraft:oak_log"), st("minecraft:oak_leaves"),
                 rng.range(3, 5));
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 沙漠神殿
//
// 原版的沙漠神殿没有结构模板 NBT，而是 Java 代码按固定步骤搭出来的。
// 这里照原版顺序逐块重放：21×21 的砂岩底座 → 9 层逐层内缩的空心台阶
// 外壳 → 前侧两座角塔和门厅 → 陶瓦花纹 → 地下宝藏室（TNT 陷阱、压力板、
// 4 个宝箱）→ 侧面的台阶与镶沙的坑。方块种类、数量和位置都与游戏内实际
// 生成一致；只有「可疑的沙」选哪几格这类纯随机装饰用普通沙子代替。
// ────────────────────────────────────────────────────────────────────────────

void buildDesertPyramid(Canvas &c, Rng &rng, const QString &mcVersion,
                        StructureModelInfo *out)
{
    out->variant = QStringLiteral("沙漠神殿");
    out->fromVanillaTemplate = true;

    const int W = 21;
    const QString ss = st("minecraft:sandstone");
    // 切制砂岩是 1.14 才有的方块，更早的版本用平滑砂岩顶替，免得缺材质
    const QString cut = StructureFinder::versionToInt(mcVersion) >= MC_1_14
        ? st("minecraft:cut_sandstone") : st("minecraft:smooth_sandstone");
    const QString chis = st("minecraft:chiseled_sandstone");
    const QString orange = st("minecraft:orange_terracotta");
    const QString blue = st("minecraft:blue_terracotta");
    const QString sand = st("minecraft:sand");
    const QString air = st("minecraft:air");

    // 原版 StructurePiece.generateBox：边框砌 edge、内部砌 fill；
    // skipAir 为真时只替换已有方块（不往空气里放）
    auto box = [&c](int x0, int y0, int z0, int x1, int y1, int z1,
                    const QString &edge, const QString &fill,
                    bool skipAir = false)
    {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (int z = z0; z <= z1; ++z)
                {
                    if (skipAir && c.at(x, y, z).isEmpty())
                        continue;
                    const bool edgeCell = x == x0 || x == x1 || y == y0
                        || y == y1 || z == z0 || z == z1;
                    c.put(x, y, z, edgeCell ? edge : fill);
                }
    };
    auto place = [&c](const QString &s, int x, int y, int z)
    {
        c.put(x, y, z, s);
    };

    // 底座：5 层实心，再往上 9 层空心台阶外壳（每层向外收一格）
    box(0, -4, 0, W - 1, 0, W - 1, ss, ss);
    for (int i = 1; i <= 9; ++i)
    {
        box(i, i, i, W - 1 - i, i, W - 1 - i, ss, ss);
        box(i + 1, i, i + 1, W - 2 - i, i, W - 2 - i, air, air);
    }

    // 前侧两座角塔（塔顶四向台阶封口）
    box(0, 0, 0, 4, 9, 4, ss, air);
    box(1, 10, 1, 3, 10, 3, ss, ss);
    place(stair("minecraft:sandstone_stairs", "north"), 2, 10, 0);
    place(stair("minecraft:sandstone_stairs", "south"), 2, 10, 4);
    place(stair("minecraft:sandstone_stairs", "west"), 0, 10, 2);
    place(stair("minecraft:sandstone_stairs", "east"), 4, 10, 2);
    box(16, 0, 0, 20, 9, 4, ss, air);
    box(17, 10, 1, 19, 10, 3, ss, ss);
    place(stair("minecraft:sandstone_stairs", "north"), 18, 10, 0);
    place(stair("minecraft:sandstone_stairs", "south"), 18, 10, 4);
    place(stair("minecraft:sandstone_stairs", "west"), 16, 10, 2);
    place(stair("minecraft:sandstone_stairs", "east"), 20, 10, 2);

    // 正门门厅
    box(8, 0, 0, 12, 4, 4, ss, air);
    box(9, 1, 0, 11, 3, 4, air, air);
    place(cut, 9, 1, 1);
    place(cut, 9, 2, 1);
    place(cut, 9, 3, 1);
    place(cut, 10, 3, 1);
    place(cut, 11, 3, 1);
    place(cut, 11, 2, 1);
    place(cut, 11, 1, 1);
    box(4, 1, 1, 8, 3, 3, ss, air);
    box(4, 1, 2, 8, 2, 2, air, air);
    box(12, 1, 1, 16, 3, 3, ss, air);
    box(12, 1, 2, 16, 2, 2, air, air);

    // 塔基之间的平台、柱子和壁柱
    box(5, 4, 5, 15, 4, 15, ss, ss);
    box(9, 4, 9, 11, 4, 11, air, air);
    box(8, 1, 8, 8, 3, 8, cut, cut);
    box(12, 1, 8, 12, 3, 8, cut, cut);
    box(8, 1, 12, 8, 3, 12, cut, cut);
    box(12, 1, 12, 12, 3, 12, cut, cut);
    box(1, 1, 5, 4, 4, 11, ss, ss);
    box(16, 1, 5, 19, 4, 11, ss, ss);
    box(6, 7, 9, 6, 7, 11, ss, ss);
    box(14, 7, 9, 14, 7, 11, ss, ss);
    box(5, 5, 9, 5, 7, 11, cut, cut);
    box(15, 5, 9, 15, 7, 11, cut, cut);
    place(air, 5, 5, 10);
    place(air, 5, 6, 10);
    place(air, 6, 6, 10);
    place(air, 15, 5, 10);
    place(air, 15, 6, 10);
    place(air, 14, 6, 10);
    box(2, 4, 4, 2, 6, 4, air, air);
    box(18, 4, 4, 18, 6, 4, air, air);
    place(stair("minecraft:sandstone_stairs", "north"), 2, 4, 5);
    place(stair("minecraft:sandstone_stairs", "north"), 2, 3, 4);
    place(stair("minecraft:sandstone_stairs", "north"), 18, 4, 5);
    place(stair("minecraft:sandstone_stairs", "north"), 18, 3, 4);
    box(1, 1, 3, 2, 2, 3, ss, ss);
    box(18, 1, 3, 19, 2, 3, ss, ss);
    place(ss, 1, 1, 2);
    place(ss, 19, 1, 2);
    place(st("minecraft:sandstone_slab"), 1, 2, 2);
    place(st("minecraft:sandstone_slab"), 19, 2, 2);
    place(stair("minecraft:sandstone_stairs", "east"), 2, 1, 2);
    place(stair("minecraft:sandstone_stairs", "west"), 18, 1, 2);

    // 两侧的壁柱带（下面留空，形成门洞）
    box(4, 3, 5, 4, 3, 17, ss, ss);
    box(16, 3, 5, 16, 3, 17, ss, ss);
    box(3, 1, 5, 4, 2, 16, air, air);
    box(15, 1, 5, 16, 2, 16, air, air);
    for (int y = 5; y <= 17; y += 2)
    {
        place(cut, 4, 1, y);
        place(chis, 4, 2, y);
        place(cut, 16, 1, y);
        place(chis, 16, 2, y);
    }

    // 地面上的陶瓦花纹（中心蓝陶瓦，向外一圈橙陶瓦）
    static const int pattern[][2] = {
        {10, 7}, {10, 8}, {9, 9}, {11, 9}, {8, 10}, {12, 10},
        {7, 10}, {13, 10}, {9, 11}, {11, 11}, {10, 12}, {10, 13},
    };
    for (const auto &p : pattern)
        place(orange, p[0], 0, p[1]);
    place(blue, 10, 0, 10);

    // 东西两侧外墙上的花纹
    for (int x : {0, 20})
    {
        place(cut, x, 2, 1);
        place(orange, x, 2, 2);
        place(cut, x, 2, 3);
        place(cut, x, 3, 1);
        place(orange, x, 3, 2);
        place(cut, x, 3, 3);
        place(orange, x, 4, 1);
        place(chis, x, 4, 2);
        place(orange, x, 4, 3);
        place(cut, x, 5, 1);
        place(orange, x, 5, 2);
        place(cut, x, 5, 3);
        place(orange, x, 6, 1);
        place(chis, x, 6, 2);
        place(orange, x, 6, 3);
        place(orange, x, 7, 1);
        place(orange, x, 7, 2);
        place(orange, x, 7, 3);
        place(cut, x, 8, 1);
        place(cut, x, 8, 2);
        place(cut, x, 8, 3);
    }

    // 正门两侧外墙上的花纹
    for (int x : {2, 18})
    {
        place(cut, x - 1, 2, 0);
        place(orange, x, 2, 0);
        place(cut, x + 1, 2, 0);
        place(cut, x - 1, 3, 0);
        place(orange, x, 3, 0);
        place(cut, x + 1, 3, 0);
        place(orange, x - 1, 4, 0);
        place(chis, x, 4, 0);
        place(orange, x + 1, 4, 0);
        place(cut, x - 1, 5, 0);
        place(orange, x, 5, 0);
        place(cut, x + 1, 5, 0);
        place(orange, x - 1, 6, 0);
        place(chis, x, 6, 0);
        place(orange, x + 1, 6, 0);
        place(orange, x - 1, 7, 0);
        place(orange, x, 7, 0);
        place(orange, x + 1, 7, 0);
        place(cut, x - 1, 8, 0);
        place(cut, x, 8, 0);
        place(cut, x + 1, 8, 0);
    }

    // 门厅上方的檐口
    box(8, 4, 0, 12, 6, 0, cut, cut);
    place(air, 8, 6, 0);
    place(air, 12, 6, 0);
    place(orange, 9, 5, 0);
    place(chis, 10, 5, 0);
    place(orange, 11, 5, 0);

    // 地下宝藏室：外壁 + 内部空腔 + TNT 陷阱与压力板
    box(8, -14, 8, 12, -11, 12, cut, cut);
    box(8, -10, 8, 12, -10, 12, chis, chis);
    box(8, -9, 8, 12, -9, 12, cut, cut);
    box(8, -8, 8, 12, -1, 12, ss, ss);
    box(9, -11, 9, 11, -1, 11, air, air);
    place(st("minecraft:stone_pressure_plate"), 10, -11, 10);
    box(9, -13, 9, 11, -13, 11, st("minecraft:tnt"), air);
    // 四壁上的壁龛
    place(air, 8, -11, 10);
    place(air, 8, -10, 10);
    place(chis, 7, -10, 10);
    place(cut, 7, -11, 10);
    place(air, 12, -11, 10);
    place(air, 12, -10, 10);
    place(chis, 13, -10, 10);
    place(cut, 13, -11, 10);
    place(air, 10, -11, 8);
    place(air, 10, -10, 8);
    place(chis, 10, -10, 7);
    place(cut, 10, -11, 7);
    place(air, 10, -11, 12);
    place(air, 10, -10, 12);
    place(chis, 10, -10, 13);
    place(cut, 10, -11, 13);
    // 四个宝箱分别嵌在四壁，开口朝向房间中央
    place(st("minecraft:chest[facing=east,type=single,waterlogged=false]"),
          8, -11, 10);
    place(st("minecraft:chest[facing=west,type=single,waterlogged=false]"),
          12, -11, 10);
    place(st("minecraft:chest[facing=north,type=single,waterlogged=false]"),
          10, -11, 8);
    place(st("minecraft:chest[facing=south,type=single,waterlogged=false]"),
          10, -11, 12);

    // 前侧地下的侧向台阶，通向宝藏室
    place(stair("minecraft:sandstone_stairs", "west"), 13, -1, 17);
    place(stair("minecraft:sandstone_stairs", "west"), 14, -2, 17);
    place(stair("minecraft:sandstone_stairs", "west"), 15, -3, 17);
    for (int x = 12; x <= 16; ++x)
        place(sand, x, 0, 17);
    place(sand, 14, -1, 17);
    place(rng.chance(50) ? sand : ss, 15, -1, 17);
    place(rng.chance(50) ? ss : sand, 16, -1, 17);
    place(sand, 15, -2, 17);
    place(ss, 16, -2, 17);
    place(sand, 16, -3, 17);

    // 前侧地下的挖掘厅（切制砂岩壁 + 沙子填芯）
    box(13, -3, 10, 13, -3, 15, cut, cut, true);
    box(19, -3, 10, 19, -3, 15, cut, cut, true);
    box(13, -3, 10, 19, -3, 10, cut, cut, true);
    box(13, -3, 16, 19, -3, 16, cut, cut, true);
    box(13, -2, 10, 13, -2, 15, chis, chis, true);
    box(19, -2, 10, 19, -2, 15, chis, chis, true);
    box(13, -2, 10, 19, -2, 10, chis, chis, true);
    box(13, -2, 16, 19, -2, 16, chis, chis, true);
    box(13, -1, 10, 13, -1, 15, cut, cut, true);
    box(19, -1, 10, 19, -1, 15, cut, cut, true);
    box(13, -1, 10, 19, -1, 10, cut, cut, true);
    box(13, -1, 16, 19, -1, 16, cut, cut, true);
    // 挖掘面：原版逐格按 1/3 概率砌砂岩、其余铺沙
    for (int x = 14; x <= 18; ++x)
        for (int z = 11; z <= 15; ++z)
            place(rng.chance(33) ? ss : sand, x, 0, z);
    // 厅内的候选格最终大多铺成沙子（原版其余格子留给可疑的沙）
    for (int x = 14; x <= 18; ++x)
        for (int y = -3; y <= -1; ++y)
            for (int z = 11; z <= 15; ++z)
                place(sand, x, y, z);
    static const int digCells[][3] = {
        {19, -3, 13}, {19, -2, 13}, {13, -3, 13}, {13, -2, 13},
        {16, -3, 16}, {16, -2, 16}, {16, -3, 10}, {16, -2, 10},
    };
    for (const auto &p : digCells)
        place(sand, p[0], p[1], p[2]);

    // 挖掘厅中央的蓝陶瓦与四周橙陶瓦标记
    place(blue, 16, -4, 13);
    static const int markCells[][3] = {
        {17, -4, 12}, {17, -4, 14}, {15, -4, 12}, {15, -4, 14},
        {18, -4, 13}, {14, -4, 13}, {16, -4, 15}, {16, -4, 11},
        {19, -4, 13}, {13, -4, 13}, {16, -4, 16}, {16, -4, 10},
    };
    for (const auto &p : markCells)
        place(orange, p[0], p[1], p[2]);
    place(cut, 20, -3, 13);
    place(chis, 20, -2, 13);
    place(cut, 12, -3, 13);
    place(chis, 12, -2, 13);
    place(cut, 16, -3, 9);
    place(chis, 16, -2, 9);
}

// ────────────────────────────────────────────────────────────────────────────
// 丛林神庙
// ────────────────────────────────────────────────────────────────────────────

void buildJungleTemple(Canvas &c, Rng &rng, const QString &,
                       StructureModelInfo *out)
{
    out->variant = QStringLiteral("丛林神庙");
    c.ground(rng, 18, 18, st("minecraft:grass_block"), st("minecraft:dirt"), 2);

    const QString cobble = st("minecraft:cobblestone");
    const QString mossy = st("minecraft:mossy_cobblestone");
    const QString chisel = st("minecraft:chiseled_stone_bricks");

    // 三级台基
    c.fill(-7, 1, -7, 7, 3, 7, cobble);
    c.fill(-6, 4, -6, 6, 5, 6, mossy);
    c.fill(-5, 6, -5, 5, 7, 5, mossy);

    // 中央塔（中空，顶部开口，能看到里面的厅堂）
    c.fill(-3, 8, -3, 3, 15, 3, cobble);
    c.carveBox(-2, 9, -2, 2, 15, 2);
    c.fill(-3, 16, -3, 3, 16, 3, mossy);
    c.carveBox(-1, 16, -1, 1, 16, 1);
    c.fill(-2, 17, -2, 2, 18, 2, chisel);
    c.carveBox(-1, 17, -1, 1, 18, 1);
    c.fill(-4, 19, -4, 4, 19, 4, mossy);
    c.carveBox(-1, 19, -1, 1, 19, 1);
    // 四面各一段台阶（原版丛林神庙四个方向都有阶梯）
    for (int dir = 0; dir < 4; ++dir)
    {
        static const char *const facing[4] = {"south", "north", "west", "east"};
        for (int step = 0; step < 3; ++step)
        {
            const int y = 1 + step;
            const int d = 8 + step;
            const QString stairState =
                QStringLiteral("minecraft:cobblestone_stairs[facing=%1]")
                    .arg(QLatin1String(facing[dir]));
            for (int w = -1; w <= 1; ++w)
            {
                switch (dir)
                {
                case 0: c.put(w, y, d, stairState); break;
                case 1: c.put(w, y, -d, stairState); break;
                case 2: c.put(-d, y, w, stairState); break;
                default: c.put(d, y, w, stairState); break;
                }
            }
        }
    }
    // 四角火盆
    for (int sx : {-1, 1})
        for (int sz : {-1, 1})
        {
            c.fill(sx * 4, 8, sz * 4, sx * 4, 10, sz * 4, mossy);
            c.put(sx * 4, 11, sz * 4, st("minecraft:fire"));
        }
    // 藤蔓：挂在塔身四面
    static const char *const vineFaces[4] = {"south", "north", "east", "west"};
    for (int i = 0; i < 44; ++i)
    {
        const int y = rng.range(9, 15);
        const int side = rng.next(4);
        const QString vine = QStringLiteral("minecraft:vine[facing=%1]")
                                 .arg(QLatin1String(vineFaces[side]));
        if (side == 0)
            c.put(rng.range(-2, 2), y, 3, vine);
        else if (side == 1)
            c.put(rng.range(-2, 2), y, -3, vine);
        else if (side == 2)
            c.put(3, y, rng.range(-2, 2), vine);
        else
            c.put(-3, y, rng.range(-2, 2), vine);
    }
    // 正门与阶梯
    c.carveBox(-1, 4, 7, 1, 7, 7);
    c.fill(-1, 4, 8, 1, 4, 8, st("minecraft:cobblestone_stairs[facing=south]"));
    c.fill(-1, 3, 9, 1, 3, 9, st("minecraft:cobblestone_stairs[facing=south]"));
    c.put(0, 8, 7, st("minecraft:torch"));
    // 藤蔓与苔藓
    for (int i = 0; i < 26; ++i)
    {
        const int x = rng.range(-6, 6);
        const int z = rng.range(-6, 6);
        if (c.at(x, 8, z).isEmpty() && !c.at(x, 7, z).isEmpty()
            && rng.chance(55))
            c.put(x, 8, z, st("minecraft:jungle_leaves"));
    }
    for (int i = 0; i < 12; ++i)
    {
        const int x = rng.range(-7, 7), z = rng.range(-7, 7);
        if (c.at(x, 1, z) == cobble || c.at(x, 4, z) == mossy)
            c.put(x, rng.range(5, 7), z, mossy);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 沼泽小屋
//
// 原版没有沼泽小屋的结构模板：小屋是在 SwampHutStructure 里直接逐块搭出来的
// （云杉木板地板、四角橡木立柱、两层墙与栅栏窗、一圈云杉台阶屋顶、屋里的炼药锅 /
// 工作台 / 红色蘑菇盆栽）。这里按同一套坐标还原：地板 8×5、墙 6×5、屋顶 8×7，
// 四角立柱从吊脚一路通到墙顶，方块种类与位置和游戏内生成的逐个一致。
// ────────────────────────────────────────────────────────────────────────────

void buildSwampHut(Canvas &c, Rng &rng, const QString &,
                   StructureModelInfo *out)
{
    out->variant = QStringLiteral("沼泽小屋");
    out->fromVanillaTemplate = true;

    const QString planks = st("minecraft:spruce_planks");
    const QString log = st("minecraft:oak_log");
    const QString table = st("minecraft:crafting_table");
    const QString cauldron = st("minecraft:cauldron");
    const QString flowerPot = st("minecraft:potted_red_mushroom");

    // 沼泽地表：草岸 + 浅水塘（只铺一圈够吊脚落脚的水面，避免把小屋衬得太小）
    c.ground(rng, 9, 9, st("minecraft:grass_block"), st("minecraft:dirt"), 3);
    c.water(rng, 8, 8, 1, 1);

    // 小屋地板高度：水面（y=1）之上两层
    const int fy = 3;
    // 小屋按水面中心摆放（下面的表格坐标是 0..8 × 0..6 的局部坐标）
    const int hx = -4, hz = -3;

    // 四角立柱：地板以下是两格吊脚，往上一直顶到墙顶
    static const int posts[4][2] = {{2, 1}, {7, 1}, {2, 5}, {7, 5}};
    for (const auto &p : posts)
        c.pillar(p[0] + hx, p[1] + hz, 1, fy + 2, log);

    // 地板（8×5；门廊外沿与四角立柱位置留空）
    static const char *const floorRows[7] = {
        ".........",
        ".P.PPPP..",
        "PPPPPPPP.",
        "PPPPPPPP.",
        "PPPPPPPP.",
        ".P.PPPP..",
        ".........",
    };
    for (int z = 0; z < 7; ++z)
        for (int x = 0; floorRows[z][x]; ++x)
            if (floorRows[z][x] == 'P')
                c.put(x + hx, fy, z + hz, planks);

    // 两层墙：云杉木板 + 栅栏窗，屋里摆炼药锅、工作台与红色蘑菇盆栽
    static const char *const wallRows[2][7] = {
        {
            ".........",
            ".F.PPPP..",
            "..P....P.",
            "..P...TP.",
            "......CP.",
            ".F.PPPP..",
            ".........",
        },
        {
            ".........",
            "...P.RP..",
            "..F....P.",
            "..P....F.",
            ".......P.",
            "...P..P..",
            ".........",
        },
    };
    for (int layer = 0; layer < 2; ++layer)
    {
        for (int z = 0; z < 7; ++z)
        {
            for (int x = 0; wallRows[layer][z][x]; ++x)
            {
                const char cell = wallRows[layer][z][x];
                if (cell == 'P')
                    c.put(x + hx, fy + 1 + layer, z + hz, planks);
                else if (cell == 'F')
                    c.put(x + hx, fy + 1 + layer, z + hz,
                          layer == 0
                              ? QStringLiteral("minecraft:oak_fence[east=true,"
                                               "north=false,south=false,west=false]")
                              : QStringLiteral("minecraft:oak_fence[east=false,"
                                               "north=true,south=true,west=false]"));
                else if (cell == 'T')
                    c.put(x + hx, fy + 1 + layer, z + hz, table);
                else if (cell == 'C')
                    c.put(x + hx, fy + 1 + layer, z + hz, cauldron);
                else if (cell == 'R')
                    c.put(x + hx, fy + 1 + layer, z + hz, flowerPot);
            }
        }
    }

    // 屋顶：外圈一圈云杉台阶（四角是转角台阶），中间铺一层木板
    for (int z = 0; z <= 6; ++z)
    {
        for (int x = 1; x <= 8; ++x)
        {
            if (z != 0 && z != 6 && x != 1 && x != 8)
            {
                c.put(x + hx, fy + 3, z + hz, planks);
                continue;
            }
            const char *facing = x == 1 ? "east" : x == 8 ? "west"
                                  : z == 0 ? "south" : "north";
            const char *shape = "straight";
            if (x == 1 && z == 0)
                shape = "outer_right";
            else if (x == 8 && z == 0)
                shape = "outer_left";
            else if (x == 1 && z == 6)
                shape = "outer_left";
            else if (x == 8 && z == 6)
                shape = "outer_right";
            c.put(x + hx, fy + 3, z + hz,
                  QStringLiteral("minecraft:spruce_stairs[facing=%1,half=bottom,"
                                 "shape=%2]")
                      .arg(QLatin1String(facing), QLatin1String(shape)));
        }
    }

    // 睡莲与蘑菇：沼泽常见的点缀，不算结构主体
    for (int i = 0; i < 14; ++i)
    {
        const int x = rng.range(-6, 6), z = rng.range(-6, 6);
        if (c.at(x, 1, z) == st("minecraft:water[level=0]")
            && c.at(x, 2, z).isEmpty())
            c.put(x, 2, z, st("minecraft:lily_pad"));
    }
    for (int i = 0; i < 6; ++i)
    {
        const int x = rng.range(-7, 7), z = rng.range(-7, 7);
        if (c.at(x, 0, z) == st("minecraft:grass_block")
            && c.at(x, 1, z).isEmpty())
            c.put(x, 1, z, rng.chance(50) ? st("minecraft:red_mushroom")
                                          : st("minecraft:brown_mushroom"));
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 雪屋
// ────────────────────────────────────────────────────────────────────────────

void buildIgloo(Canvas &c, Rng &rng, const QString &,
                StructureModelInfo *out)
{
    out->variant = QStringLiteral("雪屋");
    c.ground(rng, 14, 14, st("minecraft:snow_block"),
             st("minecraft:snow_block"), 2);

    const QString snow = st("minecraft:snow_block");
    const int r = 5;
    for (int x = -r; x <= r; ++x)
        for (int y = 0; y <= r; ++y)
            for (int z = -r; z <= r; ++z)
            {
                const int d2 = x * x + y * y + z * z;
                if (d2 > r * r + 1)
                    continue;
                if (y == 0 && (x * x + z * z) <= 9)
                    continue; // 冰面地板
                c.put(x, y, z, snow);
            }
    // 内部掏空 + 冰地板
    for (int x = -3; x <= 3; ++x)
        for (int z = -3; z <= 3; ++z)
            for (int y = 0; y <= 3; ++y)
                c.carve(x, y, z);
    c.fill(-3, 0, -3, 3, 0, 3, st("minecraft:ice"));
    // 入口隧道（南侧）
    c.fill(-1, 0, 4, 1, 3, 4, snow);
    c.fill(-1, 1, 5, 1, 3, 6, snow);
    c.fill(-1, 0, 5, 1, 0, 6, st("minecraft:ice"));
    c.carveBox(0, 1, 4, 0, 2, 6);
    // 家具
    c.fill(-2, 1, -2, -1, 1, -2, st("minecraft:red_wool"));
    c.put(-2, 1, -1, st("minecraft:oak_slab[type=top]"));
    c.put(2, 1, -2, st("minecraft:furnace[facing=south]"));
    c.put(2, 1, 2, st("minecraft:crafting_table"));
    c.put(-2, 2, 2, st("minecraft:torch"));
    // 外面：几只雪块与营火
    c.fill(6, 1, 2, 7, 2, 3, snow);
    c.put(8, 1, -6, st("minecraft:campfire[lit=true,facing=north]"));
    (void)rng;
}

// ────────────────────────────────────────────────────────────────────────────
// 海底废墟
// ────────────────────────────────────────────────────────────────────────────

void buildOceanRuin(Canvas &c, Rng &rng, const QString &,
                    StructureModelInfo *out)
{
    const bool warm = rng.chance(50);
    const QString wall = st(warm ? "minecraft:sandstone" : "minecraft:stone_bricks");
    const QString trim = st(warm ? "minecraft:cut_sandstone" : "minecraft:mossy_stone_bricks");
    const QString bed = st(warm ? "minecraft:sand" : "minecraft:gravel");
    const QString coral = st(warm ? "minecraft:tube_coral_block" : "minecraft:brain_coral_block");
    c.ground(rng, 14, 14, bed, st("minecraft:sand"), 2);
    c.water(rng, 15, 15, 1, 4);

    // 主厅：石砖/砂岩残墙，留出坍塌缺口
    c.fill(-5, 0, -4, 5, 0, 4, trim);
    c.walls(-5, 1, -4, 5, 4, 4, wall);
    c.carveBox(-2, 1, -4, 2, 3, -4);
    c.carveBox(5, 1, -2, 5, 3, 2);
    c.carveBox(-1, 1, 4, 1, 2, 4);
    for (int i = 0; i < 16; ++i)
    {
        const int x = rng.range(-5, 5), y = rng.range(1, 4), z = rng.range(-4, 4);
        if (c.at(x, y, z) == wall)
            c.carve(x, y, z);
    }
    // 四根残柱与一段拱门
    for (int sx : {-4, 4})
        for (int sz : {-3, 3})
            c.pillar(sx, sz, 1, rng.range(2, 4), trim);
    c.fill(0, 4, -4, 0, 4, 4, trim);
    c.pillar(-4, 0, 1, 4, trim);
    c.pillar(4, 0, 1, 4, trim);

    // 海沙掩埋与珊瑚
    for (int i = 0; i < 10; ++i)
    {
        const int x = rng.range(-9, 9), z = rng.range(-9, 9);
        if (c.at(x, 0, z) == bed)
            c.put(x, 1, z, coral);
    }
    for (int i = 0; i < 8; ++i)
        c.put(rng.range(-8, 8), rng.range(1, 2), rng.range(-8, 8),
              st("minecraft:sea_pickle[pickles=2]"));
    c.put(3, 1, -5, st("minecraft:chest[facing=south]"));
    out->variant = warm ? QStringLiteral("暖水海底废墟")
                        : QStringLiteral("冷水海底废墟");
}

// ────────────────────────────────────────────────────────────────────────────
// 沉船
// ────────────────────────────────────────────────────────────────────────────

void buildShipwreck(Canvas &c, Rng &rng, const QString &,
                    StructureModelInfo *out)
{
    const bool beached = rng.chance(35);
    const QString plank = st(rng.chance(50) ? "minecraft:oak_planks" : "minecraft:spruce_planks");
    const QString hull = st(rng.chance(50) ? "minecraft:spruce_planks" : "minecraft:dark_oak_planks");
    const QString log = st(rng.chance(50) ? "minecraft:oak_log" : "minecraft:spruce_log");

    c.ground(rng, beached ? 14 : 16, beached ? 14 : 16,
             st("minecraft:sand"), st("minecraft:sandstone"), 2);
    if (!beached)
        c.water(rng, 17, 17, 1, 1);

    // 船体：底窄上宽，首尾收窄
    for (int y = 1; y <= 5; ++y)
    {
        const int half = y == 1 ? 1 : (y == 2 ? 2 : 3);
        for (int x = -9; x <= 9; ++x)
        {
            int h = half;
            const int ax = qAbs(x);
            if (ax >= 8) --h;
            if (ax >= 9) --h;
            if (h < 0)
                continue;
            for (int z = -h; z <= h; ++z)
                c.put(x, y, z, (z == -h || z == h || y == 1) ? hull : plank);
        }
    }
    // 掏空船舱，保留甲板
    for (int x = -8; x <= 8; ++x)
        for (int z = -2; z <= 2; ++z)
            for (int y = 2; y <= 4; ++y)
                c.carve(x, y, z);
    // 甲板破口
    c.carveBox(5, 5, -1, 6, 5, 1);
    c.carveBox(-7, 5, 1, -5, 5, 2);

    // 桅杆与风帆
    c.pillar(2, 0, 5, 12, log);
    c.fill(2, 9, -3, 2, 12, -3, st("minecraft:white_wool"));
    c.fill(2, 9, 3, 2, 12, 3, st("minecraft:white_wool"));
    c.put(2, 13, 0, st("minecraft:oak_fence"));
    // 船尾小舱
    c.fill(-8, 5, -2, -4, 5, 2, plank);
    c.walls(-8, 6, -2, -4, 7, 2, hull);
    c.fill(-8, 8, -2, -4, 8, 2, plank);
    c.carveBox(-7, 6, 0, -5, 7, 0);
    // 货箱与木桶
    c.put(-2, 2, 0, st("minecraft:chest[facing=north]"));
    c.put(0, 2, -1, st("minecraft:barrel[facing=up]"));
    c.put(0, 2, 1, st("minecraft:barrel[facing=up]"));
    c.put(-4, 2, -1, st("minecraft:chest[facing=south]"));
    // 海滩散木
    if (beached)
        for (int i = 0; i < 5; ++i)
            c.put(rng.range(-10, 10), 1, rng.range(-12, 12),
                  st("minecraft:oak_slab[type=bottom]"));
    out->variant = beached ? QStringLiteral("搁浅沉船") : QStringLiteral("沉没沉船");
}

// ────────────────────────────────────────────────────────────────────────────
// 废弃传送门
// ────────────────────────────────────────────────────────────────────────────

void buildRuinedPortal(Canvas &c, Rng &rng, const QString &,
                       StructureModelInfo *out)
{
    const QString obsidian = st("minecraft:obsidian");
    const QString crying = st("minecraft:crying_obsidian");
    const QString netherrack = st("minecraft:netherrack");

    c.ground(rng, 10, 10, netherrack, st("minecraft:magma_block"), 2);
    for (int i = 0; i < 6; ++i)
        c.put(rng.range(-9, 9), 1, rng.range(-9, 9), netherrack);
    // 岩浆坑
    c.fill(-5, 1, 4, -3, 1, 6, st("minecraft:lava[level=0]"));

    // 4×5 黑曜石门框（内径 2×3）
    c.fill(-2, 1, 0, 1, 1, 0, obsidian);
    c.fill(-2, 5, 0, 1, 5, 0, obsidian);
    c.pillar(-2, 0, 2, 4, obsidian);
    c.pillar(1, 0, 2, 4, obsidian);
    c.fill(-1, 2, 0, 0, 4, 0, st("minecraft:nether_portal[axis=x]"));
    // 破损：抽掉几块，混入哭泣黑曜石
    const int broken[][2] = {{-2, 5}, {1, 3}, {-2, 3}, {1, 5}};
    for (const auto &b : broken)
        if (rng.chance(55))
            c.carve(b[0], b[1], 0);
    for (int i = 0; i < 4; ++i)
    {
        const int x = rng.range(-2, 1), y = rng.range(2, 5);
        if (c.at(x, y, 0) == obsidian)
            c.put(x, y, 0, crying);
    }
    // 金块与箱子
    c.put(-2, 1, -2, st("minecraft:gold_block"));
    c.put(2, 1, 1, st("minecraft:chest[facing=west]"));
    c.put(3, 1, -2, st("minecraft:magma_block"));
    c.put(0, 1, 4, st("minecraft:gold_block"));
    // 散落黑曜石
    for (int i = 0; i < 6; ++i)
        c.put(rng.range(-6, 6), 1, rng.range(-6, 6),
              rng.chance(60) ? obsidian : netherrack);
    out->variant = QStringLiteral("废弃传送门");
}

// ────────────────────────────────────────────────────────────────────────────
// 掠夺者前哨站
// ────────────────────────────────────────────────────────────────────────────

void buildOutpost(Canvas &c, Rng &rng, const QString &,
                  StructureModelInfo *out)
{
    c.ground(rng, 16, 16, st("minecraft:grass_block"), st("minecraft:dirt"), 2);
    const QString wood = st("minecraft:dark_oak_planks");
    const QString log = st("minecraft:dark_oak_log");
    const QString cobble = st("minecraft:cobblestone");
    const QString glass = st("minecraft:glass_pane");

    // 地基与四层塔身
    c.fill(-5, 1, -5, 5, 1, 5, cobble);
    c.fill(-4, 1, -4, 4, 1, 4, wood);
    int floorY[] = {2, 6, 10, 14};
    for (int f = 0; f < 4; ++f)
    {
        const int y0 = floorY[f], y1 = y0 + 3;
        c.walls(-4, y0, -4, 4, y1, 4, wood);
        for (int y = y0; y <= y1; ++y)
        {
            c.put(-4, y, -4, log); c.put(4, y, -4, log);
            c.put(-4, y, 4, log);  c.put(4, y, 4, log);
        }
        c.fill(-4, y1, -4, 4, y1, 4, wood);
        // 窗
        for (int x = -2; x <= 2; x += 2)
        {
            c.put(x, y0 + 2, -4, glass);
            c.put(x, y0 + 2, 4, glass);
        }
        for (int z = -2; z <= 2; z += 2)
        {
            c.put(-4, y0 + 2, z, glass);
            c.put(4, y0 + 2, z, glass);
        }
        // 每层外侧挑台
        if (f > 0)
        {
            c.ring(-6, -6, 6, 5, y1 + 1,
                   st("minecraft:dark_oak_fence"));
            c.ring(-5, -5, 5, 5, y1 + 1, wood);
        }
        // 门
        c.carveBox(0, y0, -4, 0, y0 + 1, -4);
    }
    // 顶层屋顶
    hipRoof(c, -5, -5, 5, 5, 18, st("minecraft:dark_oak_stairs"),
            st("minecraft:dark_oak_slab"));
    c.put(0, 19, 0, st("minecraft:lantern"));
    // 内部梯子与火把
    for (int y = 2; y <= 16; ++y)
        c.put(0, y, 0, st("minecraft:ladder[facing=south]"));
    for (int y = 2; y <= 14; y += 4)
        c.put(-3, y + 1, 3, st("minecraft:torch"));

    // 帐篷与栅栏
    c.fill(7, 1, -3, 11, 1, 2, st("minecraft:white_wool"));
    gableRoof(c, 7, -3, 11, 2, 2, st("minecraft:white_wool"),
              st("minecraft:white_wool"), QString());
    c.put(9, 2, 0, st("minecraft:campfire[lit=true,facing=south]"));
    for (int x = -8; x <= 8; ++x)
        if (qAbs(x) > 2)
            c.put(x, 1, 7, st("minecraft:dark_oak_fence"));
    out->variant = QStringLiteral("掠夺者前哨站");
}

// ────────────────────────────────────────────────────────────────────────────
// 林地府邸
// ────────────────────────────────────────────────────────────────────────────

void buildMansion(Canvas &c, Rng &rng, const QString &,
                  StructureModelInfo *out)
{
    c.ground(rng, 26, 26, st("minecraft:grass_block"), st("minecraft:dirt"), 2);
    const QString wood = st("minecraft:dark_oak_planks");
    const QString log = st("minecraft:dark_oak_log");
    const QString birch = st("minecraft:birch_planks");
    const QString cobble = st("minecraft:cobblestone");
    const QString glass = st("minecraft:glass_pane");
    const QString carpet = st("minecraft:red_carpet");

    // 石基
    c.fill(-15, 1, -9, 15, 2, 9, cobble);
    c.fill(-14, 2, -8, 14, 2, 8, wood);

    // 主体三层
    const int floors[][2] = {{3, 7}, {8, 12}, {13, 16}};
    for (const auto &f : floors)
    {
        c.walls(-9, f[0], -6, 9, f[1], 6, wood);
        for (int y = f[0]; y <= f[1]; ++y)
        {
            c.put(-9, y, -6, log); c.put(9, y, -6, log);
            c.put(-9, y, 6, log);  c.put(9, y, 6, log);
            c.put(-9, y, 0, birch); c.put(9, y, 0, birch);
        }
        c.fill(-9, f[1], -6, 9, f[1], 6, wood);
        for (int x = -7; x <= 7; x += 3)
        {
            c.put(x, f[0] + 2, -6, glass);
            c.put(x, f[0] + 2, 6, glass);
        }
        for (int z = -4; z <= 4; z += 3)
        {
            c.put(-9, f[0] + 2, z, glass);
            c.put(9, f[0] + 2, z, glass);
        }
    }
    // 双坡大屋顶（屋脊沿 X）
    gableRoof(c, -11, -8, 11, 8, 17, st("minecraft:dark_oak_stairs"),
              st("minecraft:dark_oak_slab"), wood);
    // 左右副楼
    c.walls(-20, 3, -3, -11, 10, 3, wood);
    c.fill(-20, 10, -3, -11, 10, 3, wood);
    hipRoof(c, -21, -4, -10, 4, 11, st("minecraft:dark_oak_stairs"),
            st("minecraft:dark_oak_slab"));
    c.walls(11, 3, -3, 19, 9, 3, wood);
    c.fill(11, 9, -3, 19, 9, 3, wood);
    hipRoof(c, 10, -4, 20, 4, 10, st("minecraft:dark_oak_stairs"),
            st("minecraft:dark_oak_slab"));
    // 门廊与红毯
    c.carveBox(-2, 3, 6, 2, 5, 6);
    c.fill(-2, 3, 7, 2, 3, 10, wood);
    c.fill(-2, 3, 7, 2, 3, 7, carpet);
    c.fill(-2, 3, 8, 2, 3, 8, carpet);
    c.fill(-2, 2, 10, 2, 2, 10, st("minecraft:dark_oak_stairs[facing=south]"));
    c.fill(-2, 6, 7, 2, 6, 9, st("minecraft:dark_oak_slab[type=top]"));
    // 烟囱与回廊
    c.pillar(-6, 4, 17, 20, cobble);
    c.pillar(6, -4, 17, 20, cobble);
    c.pillar(-6, 4, 21, 21, st("minecraft:campfire[lit=false]"));
    c.pillar(6, -4, 21, 21, st("minecraft:campfire[lit=false]"));
    // 庭院树
    for (int i = 0; i < 3; ++i)
        tree(c, rng, rng.range(-24, 24), rng.range(-18, 18),
             st("minecraft:dark_oak_log"), st("minecraft:dark_oak_leaves"), 5);
    out->variant = QStringLiteral("林地府邸");
}

// ────────────────────────────────────────────────────────────────────────────
// 要塞
// ────────────────────────────────────────────────────────────────────────────

void buildStronghold(Canvas &c, Rng &rng, const QString &,
                     StructureModelInfo *out)
{
    const QString brick = st("minecraft:stone_bricks");
    const QString cracked = st("minecraft:cracked_stone_bricks");
    const QString mossy = st("minecraft:mossy_stone_bricks");
    const QString frame = st("minecraft:end_portal_frame[eye=true]");
    const QString portal = st("minecraft:end_portal");

    // 地基与主厅（去掉屋顶，做成剖面，能直接看到末地传送门）
    c.fill(-13, 0, -11, 13, 0, 11, brick);
    c.walls(-7, 1, -6, 7, 6, 6, brick);
    c.fill(-7, 1, -6, 7, 1, 6, mossy);
    c.carveBox(-6, 1, -5, 6, 6, 5);
    c.fill(-6, 1, -5, 6, 1, 5, mossy);
    for (int sx : {-7, 7})
        for (int sz : {-6, 6})
            c.pillar(sx, sz, 1, 7, cracked);
    // 末地传送门：3×3 框架 + 中间传送门方块
    for (int x = -2; x <= 2; ++x)
        for (int z = -2; z <= 2; ++z)
            if (qAbs(x) == 2 || qAbs(z) == 2)
                c.put(x, 2, z, frame);
            else
                c.put(x, 2, z, portal);
    c.put(0, 2, 2, st("minecraft:end_portal_frame[eye=false]"));
    c.fill(-1, 1, -1, 1, 1, 1, st("minecraft:lava[level=0]"));
    // 四条走廊
    const int dirs[][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    for (const auto &d : dirs)
    {
        const int len = rng.range(7, 12);
        for (int i = 7; i <= 7 + len; ++i)
        {
            const int x = d[0] * i, z = d[1] * i;
            const int ax = d[0] ? (d[0] > 0 ? 1 : -1) : 0;
            const int az = d[1] ? (d[1] > 0 ? 1 : -1) : 0;
            const int px = az, pz = ax;
            c.fill(x - 1, 1, z - 1, x + 1, 1, z + 1, mossy);
            c.walls(x - 1, 1, z - 1, x + 1, 4, z + 1,
                    rng.chance(30) ? (px ? cracked : mossy) : brick);
            c.carveBox(x - 1, 1, z - 1, x + 1, 3, z + 1);
            // 走廊里每隔两格点一支火把（挂在顶上，不挡路）
            if (i % 2 == 0)
                c.put(x, 3, z, st("minecraft:torch"));
        }
    }
    // 图书馆侧翼（同样掀掉屋顶）
    c.walls(-12, 1, 1, -8, 6, 9, brick);
    c.fill(-12, 1, 1, -8, 1, 9, mossy);
    c.carveBox(-11, 1, 2, -9, 6, 8);
    for (int y = 2; y <= 5; ++y)
        for (int z = 2; z <= 8; ++z)
            if (y < 5 || z % 2 == 0)
                c.put(-11, y, z, st("minecraft:bookshelf"));
    c.fill(-10, 2, 3, -10, 2, 4, st("minecraft:cobweb"));
    c.carveBox(-8, 2, 4, -8, 3, 6);
    // 螺旋楼梯塔（方形回旋台阶）
    c.fill(9, 1, -8, 13, 1, -4, brick);
    for (int i = 0; i < 10; ++i)
    {
        const int x = 9 + (i % 4), z = -8 + ((i / 4) % 4);
        c.put(x, 2 + i, z, st("minecraft:stone_brick_stairs[facing=east]"));
        c.pillar(9, -8, 2, 2 + i, brick);
        c.pillar(13, -8, 2, 2 + i, brick);
        c.pillar(9, -4, 2, 2 + i, brick);
        c.pillar(13, -4, 2, 2 + i, brick);
    }
    c.fill(9, 12, -8, 13, 12, -4, brick);
    out->variant = QStringLiteral("要塞");
}

// ────────────────────────────────────────────────────────────────────────────
// 废弃矿井
// ────────────────────────────────────────────────────────────────────────────

void buildMineshaft(Canvas &c, Rng &rng, const QString &,
                    StructureModelInfo *out)
{
    const QString stone = st("minecraft:stone");
    const QString cobble = st("minecraft:cobblestone");
    const QString plank = st("minecraft:oak_planks");
    const QString fence = st("minecraft:oak_fence");
    const QString rail = st("minecraft:rail[shape=north_south]");

    // 露天剖面：只留一层石头台面，巷道敞开，木支架与轨道直接露在外面
    c.fill(-14, 0, -4, 14, 0, 4, stone);
    c.fill(-13, 1, -1, 13, 1, 1, plank);
    c.fill(-13, 2, 0, 13, 2, 0, rail);
    // 侧向支巷（地板 + 轨道）
    c.fill(4, 1, 2, 6, 1, 6, plank);
    c.fill(-6, 1, 2, -4, 1, 4, plank);
    c.fill(5, 2, 2, 5, 2, 6, st("minecraft:rail[shape=north_south]"));
    c.fill(-5, 2, 2, -5, 2, 4, st("minecraft:rail[shape=north_south]"));

    // 木支架：立柱 + 横梁
    for (int x = -12; x <= 12; x += 4)
    {
        if (x == 4)
            continue;
        c.pillar(x, -1, 2, 5, fence);
        c.pillar(x, 1, 2, 5, fence);
        c.fill(x - 1, 5, -1, x + 1, 5, 1, plank);
        c.put(x, 3, -2, st("minecraft:torch"));
    }
    // 巷道壁上的木板
    for (int x = -12; x <= 12; x += 3)
        c.put(x, 3, -2, plank);
    // 蜘蛛网、箱子与矿车轨道尽头
    for (int i = 0; i < 6; ++i)
        c.put(rng.range(-12, 12), rng.range(3, 4), rng.range(-1, 1),
              st("minecraft:cobweb"));
    c.put(-10, 2, 1, st("minecraft:chest[facing=south]"));
    c.put(9, 2, -1, st("minecraft:chest[facing=west]"));
    c.put(11, 2, 0, st("minecraft:rail[shape=east_west]"));
    for (int x = 5; x <= 13; ++x)
        if (rng.chance(35))
            c.put(x, 2, 0, st("minecraft:rail[shape=east_west]"));
    out->variant = QStringLiteral("废弃矿井");
}

// ────────────────────────────────────────────────────────────────────────────
// 远古城市
// ────────────────────────────────────────────────────────────────────────────

void buildAncientCity(Canvas &c, Rng &rng, const QString &,
                      StructureModelInfo *out)
{
    const QString brick = st("minecraft:deepslate_bricks");
    const QString tile = st("minecraft:deepslate_tiles");
    const QString dark = st("minecraft:polished_deepslate");
    const QString sculk = st("minecraft:sculk");

    c.fill(-15, 0, -9, 15, 0, 9, tile);
    for (int i = 0; i < 18; ++i)
        c.put(rng.range(-14, 14), 1, rng.range(-8, 8), sculk);

    // 大厅矮墙与立柱
    c.walls(-14, 1, -8, 14, 5, 8, brick);
    c.fill(-14, 1, -8, 14, 1, 8, dark);
    for (int x = -12; x <= 12; x += 6)
        for (int z = -6; z <= 6; z += 6)
        {
            c.pillar(x, z, 2, 8, dark);
            c.put(x, 9, z, st("minecraft:chiseled_deepslate"));
        }
    // 中央巨型传送门框架
    c.fill(-3, 1, -6, 3, 6, -5, st("minecraft:reinforced_deepslate"));
    c.carveBox(-2, 2, -5, 2, 5, -5);
    c.fill(-2, 2, -5, 2, 5, -5, sculk);
    c.put(0, 2, -5, st("minecraft:sculk_catalyst"));
    c.put(-1, 3, -5, st("minecraft:sculk_sensor"));
    c.put(1, 4, -5, st("minecraft:sculk_shrieker"));
    // 灯与地毡
    for (int x : {-10, -4, 4, 10})
    {
        c.pillar(x, 7, 2, 3, st("minecraft:deepslate_brick_wall"));
        c.put(x, 4, 7, st("minecraft:soul_lantern"));
    }
    c.fill(-8, 1, 2, -6, 1, 5, st("minecraft:light_blue_wool"));
    c.fill(6, 1, -4, 8, 1, -1, st("minecraft:gray_wool"));
    // 入口坡道
    c.fill(-2, 1, 9, 2, 1, 13, tile);
    c.walls(-2, 1, 9, 2, 4, 13, brick);
    c.carveBox(-1, 1, 9, 1, 3, 13);
    out->variant = QStringLiteral("远古城市");
}

// ────────────────────────────────────────────────────────────────────────────
// 试炼密室
// ────────────────────────────────────────────────────────────────────────────

void buildTrialChambers(Canvas &c, Rng &rng, const QString &,
                        StructureModelInfo *out)
{
    const QString tuff = st("minecraft:tuff");
    const QString brick = st("minecraft:tuff_bricks");
    const QString copper = st("minecraft:copper_block");
    const QString grate = st("minecraft:copper_grate");
    const QString bulb = st("minecraft:copper_bulb[lit=true]");

    c.fill(-11, 0, -7, 11, 0, 7, tuff);
    c.fill(-11, 0, -1, 11, 0, 1, brick);
    c.walls(-11, 1, -7, 11, 6, 7, brick);
    c.fill(-11, 1, -7, 11, 1, 7, tuff);
    // 铜格栅窗与灯
    for (int x = -9; x <= 9; x += 4)
        c.fill(x, 3, -7, x + 1, 4, -7, grate);
    for (int z = -5; z <= 5; z += 4)
        c.fill(-11, 3, z, -11, 4, z + 1, grate);
    for (int x : {-8, 0, 8})
        c.put(x, 4, 6, bulb);
    // 立柱与拱顶
    for (int z : {-4, 4})
        for (int x : {-6, 6})
        {
            c.pillar(x, z, 2, 5, st("minecraft:chiseled_tuff"));
            c.put(x, 6, z, copper);
        }
    // 尽头宝库与试炼刷怪箱
    c.fill(-2, 2, -6, 2, 3, -6, st("minecraft:vault[facing=south]"));
    c.put(0, 2, 0, st("minecraft:trial_spawner"));
    c.fill(-3, 1, 3, 3, 1, 3, st("minecraft:waxed_copper_block"));
    // 入口铜门与台阶
    c.carveBox(-1, 1, 7, 1, 3, 7);
    c.put(-1, 2, 7, st("minecraft:copper_door[facing=south,half=lower]"));
    c.put(1, 2, 7, st("minecraft:copper_door[facing=south,half=lower]"));
    c.fill(-1, 1, 8, 1, 1, 10, tuff);
    for (int i = 0; i < 3; ++i)
        c.put(rng.range(-8, 8), 1, rng.range(-5, 5),
              st("minecraft:copper_grate"));
    out->variant = QStringLiteral("试炼密室");
}

// ────────────────────────────────────────────────────────────────────────────
// 古迹废墟
// ────────────────────────────────────────────────────────────────────────────

void buildTrailRuins(Canvas &c, Rng &rng, const QString &,
                     StructureModelInfo *out)
{
    const QString mud = st("minecraft:mud_bricks");
    const QString terr = st("minecraft:terracotta");
    const QString orange = st("minecraft:orange_terracotta");
    const QString yellow = st("minecraft:yellow_terracotta");
    const QString gravel = st("minecraft:gravel");

    c.ground(rng, 13, 13, st("minecraft:coarse_dirt"), st("minecraft:dirt"), 2);
    for (int i = 0; i < 12; ++i)
        c.put(rng.range(-9, 9), 1, rng.range(-9, 9), gravel);

    // 残塔：泥砖 + 彩色陶瓦层
    c.fill(-3, 1, -3, 3, 7, 3, mud);
    c.fill(-3, 4, -3, 3, 4, 3, orange);
    c.fill(-3, 7, -3, 3, 7, 3, yellow);
    c.carveBox(-2, 1, -2, 2, 6, 2);
    c.carveBox(0, 1, 3, 0, 3, 3);
    // 坍塌：抽掉半面墙与顶部
    for (int i = 0; i < 10; ++i)
    {
        const int x = rng.range(-3, 3), y = rng.range(2, 7), z = rng.range(-3, 3);
        if (x == -3 || x == 3 || z == -3 || z == 3)
            c.carve(x, y, z);
    }
    // 断墙与可疑沙砾
    c.walls(-8, 1, -6, -4, 4, -2, terr);
    c.fill(-8, 4, -6, -4, 4, -2, orange);
    for (int i = 0; i < 6; ++i)
        c.put(rng.range(-8, 8), 1, rng.range(-8, 8),
              st("minecraft:suspicious_gravel"));
    c.put(-6, 1, -4, st("minecraft:decorated_pot"));
    c.put(5, 1, 4, st("minecraft:campfire[lit=false,facing=south]"));
    out->variant = QStringLiteral("古迹废墟");
}

// ────────────────────────────────────────────────────────────────────────────
// 沙漠水井
//
// 原版的沙漠水井没有结构模板 NBT，是 Java 代码（DesertWellFeature）按固定
// 步骤搭出来的。这里照原版顺序逐块重放：地面以下三层 5×5 砂岩井体 → 井口
// 正中和四邻共五格水 → 1.20 起水下一层换成一模一样的十字沙子 → 井沿一圈
// 砂岩（四条边的正中换成砂岩台阶）→ 四角砂岩柱 → 4 格高处 3×3 的台阶顶盖
// （中心一块砂岩）→ 最后从井口五格里各抽一格沙子、一格砂岩换成「可疑的沙」。
// 方块种类、数量和位置都与游戏内一致，只有可疑的沙落在哪一格按种子随机。
// ────────────────────────────────────────────────────────────────────────────

void buildDesertWell(Canvas &c, Rng &rng, const QString &mcVersion,
                     StructureModelInfo *out)
{
    out->variant = QStringLiteral("沙漠水井");
    out->fromVanillaTemplate = true;

    const bool hasSusSand =
        StructureFinder::versionToInt(mcVersion) >= MC_1_20;
    const QString sandstone = st("minecraft:sandstone");
    const QString slab = st("minecraft:sandstone_slab");
    const QString water = st("minecraft:water[level=0]");
    const QString sand = st("minecraft:sand");

    // 井口十字：中心 + 四邻（原版 Direction.Plane.HORIZONTAL 的四个方向）
    static const int plus[5][2] = {
        {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    };

    // 井体：地面以下三层 5×5 实心砂岩
    c.fill(-2, -2, -2, 2, 0, 2, sandstone);

    // 井口水层，1.20 起水底这一层改成沙子
    for (const auto &p : plus)
    {
        c.put(p[0], 0, p[1], water);
        if (hasSusSand)
            c.put(p[0], -1, p[1], sand);
    }

    // 井沿：一圈砂岩，四条边的正中换成砂岩台阶
    c.ring(-2, -2, 2, 2, 1, sandstone);
    c.put(2, 1, 0, slab);
    c.put(-2, 1, 0, slab);
    c.put(0, 1, 2, slab);
    c.put(0, 1, -2, slab);

    // 顶盖：4 格高处 3×3，中心砂岩、周围八块台阶
    c.fill(-1, 4, -1, 1, 4, 1, slab);
    c.put(0, 4, 0, sandstone);

    // 四角立柱：从井沿到顶盖下方
    for (int sx : {-1, 1})
        for (int sz : {-1, 1})
            c.pillar(sx, sz, 1, 3, sandstone);

    // 1.20 起：井口五格里各抽一格，把水下的沙子、再下面的砂岩
    // 换成可刷扫的「可疑的沙」
    if (hasSusSand)
    {
        const int i = rng.next(5);
        c.put(plus[i][0], -1, plus[i][1], st("minecraft:suspicious_sand"));
        const int j = rng.next(5);
        c.put(plus[j][0], -2, plus[j][1], st("minecraft:suspicious_sand"));
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 埋藏的宝藏
// ────────────────────────────────────────────────────────────────────────────

void buildBuriedTreasure(Canvas &c, Rng &rng, const QString &,
                         StructureModelInfo *out)
{
    c.ground(rng, 6, 5, st("minecraft:sand"), st("minecraft:sandstone"), 2);
    // 挖开的沙坑，露出箱顶
    c.carveBox(-2, 1, -1, 2, 3, 1);
    c.fill(-2, 0, -1, 2, 0, 1, st("minecraft:sandstone"));
    c.put(0, 1, 0, st("minecraft:chest[facing=north]"));
    c.put(-1, 1, 1, st("minecraft:suspicious_sand"));
    c.put(1, 1, -1, st("minecraft:suspicious_sand"));
    c.put(2, 1, 1, st("minecraft:gravel"));
    // 半掩的石板与散落沙堆
    c.put(-2, 4, -1, st("minecraft:sandstone_slab[type=bottom]"));
    c.put(1, 4, 2, st("minecraft:sandstone_slab[type=bottom]"));
    for (int i = 0; i < 6; ++i)
        c.put(rng.range(-7, 7), 1, rng.range(-5, 5), st("minecraft:sand"));
    out->variant = QStringLiteral("埋藏的宝藏");
}

// ────────────────────────────────────────────────────────────────────────────
// 下界要塞
// ────────────────────────────────────────────────────────────────────────────

void buildFortress(Canvas &c, Rng &rng, const QString &,
                   StructureModelInfo *out)
{
    const QString brick = st("minecraft:nether_bricks");
    const QString fence = st("minecraft:nether_brick_fence");
    const QString lava = st("minecraft:lava[level=0]");

    // 岩浆海（薄薄一层当底座，方便看桥）
    c.fill(-26, 0, -26, 26, 0, 26, lava);
    for (int i = 0; i < 8; ++i)
        c.put(rng.range(-24, 24), 1, rng.range(-24, 24),
              st("minecraft:netherrack"));

    // 主桥：沿 z 方向，桥面 5 格宽、两侧矮墙，桥墩一直插进岩浆
    c.fill(-2, 8, -22, 2, 8, 22, brick);
    c.fill(-2, 9, -22, -2, 9, 22, brick);
    c.fill(2, 9, -22, 2, 9, 22, brick);
    for (int z = -20; z <= 20; z += 8)
    {
        c.pillar(-2, z, 1, 7, brick);
        c.pillar(2, z, 1, 7, brick);
    }
    // 叉桥：沿 x 方向，在 z=0 处与主桥交叉
    c.fill(-20, 8, -2, 20, 8, 2, brick);
    c.fill(-20, 9, -2, 20, 9, -2, brick);
    c.fill(-20, 9, 2, 20, 9, 2, brick);
    for (int x = -18; x <= 18; x += 8)
    {
        c.pillar(x, -2, 1, 7, brick);
        c.pillar(x, 2, 1, 7, brick);
    }
    // 交叉处的桥顶（原版 bridge_crossing 是带拱顶的跨段）
    c.fill(-2, 10, -2, 2, 10, 2, brick);
    c.fill(-2, 11, -2, 2, 11, 2, brick);
    c.fill(-1, 12, -1, 1, 12, 1, brick);
    c.put(0, 9, 0, st("minecraft:lantern"));

    // 烈焰人房间（桥东端的小厅）
    c.fill(-4, 8, 12, 4, 8, 20, brick);
    c.walls(-4, 9, 12, 4, 13, 20, brick);
    c.fill(-4, 14, 12, 4, 14, 20, brick);
    c.carveBox(-3, 9, 13, 3, 13, 19);
    c.carveBox(-1, 9, 12, 1, 11, 12);
    c.put(0, 9, 16, st("minecraft:spawner"));
    c.put(-2, 9, 18, st("minecraft:chest[facing=south]"));
    c.put(2, 9, 18, st("minecraft:chest[facing=south]"));
    c.fill(-3, 9, 13, 3, 9, 13, fence);

    // 下界疣楼梯与农田
    c.fill(4, 8, -6, 12, 8, 2, brick);
    for (int i = 0; i < 4; ++i)
        c.fill(5 + i, 9 + i, -5, 5 + i, 9 + i, 1, brick);
    c.fill(9, 13, -5, 12, 13, 1, st("minecraft:soul_sand"));
    for (int x = 9; x <= 12; ++x)
        for (int z = -4; z <= 0; z += 2)
            c.put(x, 14, z, st("minecraft:nether_wart[age=3]"));
    c.pillar(9, -5, 14, 16, brick);
    c.pillar(12, 1, 14, 16, brick);
    c.put(10, 15, -2, st("minecraft:lantern"));

    // 桥边的零星岩浆块与火把
    for (int i = 0; i < 6; ++i)
    {
        const int z = rng.range(-18, 18) / 6 * 6;
        c.put(-2, 10, z, st("minecraft:torch"));
        c.put(2, 10, z + 6, st("minecraft:torch"));
    }
    out->variant = QStringLiteral("下界要塞");
}

// ────────────────────────────────────────────────────────────────────────────
// 堡垒遗迹
// ────────────────────────────────────────────────────────────────────────────

void buildBastion(Canvas &c, Rng &rng, const QString &,
                  StructureModelInfo *out)
{
    const QString black = st("minecraft:blackstone");
    const QString brick = st("minecraft:polished_blackstone_bricks");
    const QString gilded = st("minecraft:gilded_blackstone");
    const QString gold = st("minecraft:gold_block");
    const QString lava = st("minecraft:lava[level=0]");
    const QString chain = st("minecraft:chain");

    // 黑石台地与岩浆壕沟
    c.fill(-18, 0, -18, 18, 0, 18, black);
    c.ring(-14, -14, 14, 14, 0, lava);
    c.ring(-13, -13, 13, 13, 0, black);
    c.fill(-2, 0, 14, 2, 0, 18, brick);
    // 主堡
    c.fill(-7, 1, -7, 7, 10, 7, black);
    c.carveBox(-6, 1, -6, 6, 9, 6);
    c.fill(-7, 1, -7, 7, 1, 7, brick);
    c.walls(-7, 1, -7, 7, 10, 7, brick);
    // 城垛
    for (int x = -7; x <= 7; x += 2)
    {
        c.put(x, 11, -7, brick);
        c.put(x, 11, 7, brick);
    }
    for (int z = -7; z <= 7; z += 2)
    {
        c.put(-7, 11, z, brick);
        c.put(7, 11, z, brick);
    }
    // 门洞与吊桥
    c.carveBox(-2, 1, 7, 2, 4, 7);
    c.carveBox(-2, 1, 8, 2, 4, 13);
    c.fill(-2, 0, 8, 2, 0, 13, st("minecraft:polished_blackstone"));
    // 内部：金块堆、锁链、宝箱
    c.fill(-3, 2, -4, -1, 3, -2, gold);
    c.fill(2, 2, 2, 3, 2, 3, gold);
    c.pillar(0, 0, 4, 9, chain);
    c.put(0, 4, -5, st("minecraft:chest[facing=south]"));
    c.put(4, 2, -4, gilded);
    c.put(-4, 2, 4, gilded);
    // 侧墙与掠夺者塔楼
    c.walls(-12, 1, 8, -8, 6, 12, black);
    c.fill(-12, 7, 8, -8, 7, 12, brick);
    c.fill(-8, 1, 8, -8, 5, 12, brick);
    c.put(-10, 6, 10, st("minecraft:lantern"));
    // 金块散落
    for (int i = 0; i < 6; ++i)
        c.put(rng.range(-11, 11), 1, rng.range(-11, 11), gold);
    out->variant = QStringLiteral("堡垒遗迹");
}

// ────────────────────────────────────────────────────────────────────────────
// 末地城
// ────────────────────────────────────────────────────────────────────────────

void buildEndCity(Canvas &c, Rng &rng, const QString &,
                  StructureModelInfo *out)
{
    const QString purpur = st("minecraft:purpur_block");
    const QString pillarS = st("minecraft:purpur_pillar");
    const QString stone = st("minecraft:end_stone_bricks");
    const QString rod = st("minecraft:end_rod");

    c.ground(rng, 18, 18, st("minecraft:end_stone"), st("minecraft:end_stone"), 2);

    // 主塔
    c.fill(-4, 1, -4, 4, 22, 4, purpur);
    c.carveBox(-3, 2, -3, 3, 21, 3);
    c.fill(-4, 1, -4, 4, 1, 4, stone);
    for (int y = 2; y <= 21; ++y)
    {
        c.put(-4, y, -4, pillarS); c.put(4, y, -4, pillarS);
        c.put(-4, y, 4, pillarS);  c.put(4, y, 4, pillarS);
    }
    for (int y = 6; y <= 18; y += 6)
    {
        c.fill(-4, y, -4, 4, y, 4, stone);
        for (int x = -2; x <= 2; x += 2)
        {
            c.put(x, y - 2, -4, st("minecraft:magenta_stained_glass"));
            c.put(x, y - 2, 4, st("minecraft:magenta_stained_glass"));
        }
    }
    c.fill(-5, 23, -5, 5, 23, 5, stone);
    c.fill(-4, 24, -4, 4, 24, 4, purpur);
    c.pillar(0, 0, 25, 26, rod);
    for (int sx : {-1, 1})
        for (int sz : {-1, 1})
            c.put(sx * 4, 25, sz * 4, rod);

    // 侧塔
    c.fill(7, 1, -3, 12, 12, 2, purpur);
    c.carveBox(8, 2, -2, 11, 11, 1);
    c.fill(7, 13, -4, 12, 13, 3, stone);
    c.put(9, 14, 0, rod);
    // 连桥
    c.fill(4, 6, -1, 7, 6, 1, purpur);
    c.fill(4, 7, -1, 4, 7, 1, st("minecraft:end_stone_brick_wall"));
    c.fill(7, 7, -1, 7, 7, 1, st("minecraft:end_stone_brick_wall"));
    // 紫颂植物与末地烛
    for (int i = 0; i < 8; ++i)
    {
        const int x = rng.range(-15, 15), z = rng.range(-15, 15);
        if (c.at(x, 1, z).isEmpty() && !c.at(x, 0, z).isEmpty())
        {
            c.pillar(x, z, 1, rng.range(2, 4), st("minecraft:chorus_plant"));
            c.put(x, rng.range(3, 5), z, st("minecraft:chorus_flower"));
        }
    }
    c.put(-8, 2, 6, rod);
    c.put(7, 2, 8, rod);
    out->variant = QStringLiteral("末地城");
}

// ────────────────────────────────────────────────────────────────────────────
// 末地折跃门
// ────────────────────────────────────────────────────────────────────────────

void buildEndGateway(Canvas &c, Rng &rng, const QString &,
                     StructureModelInfo *out)
{
    const QString bedrock = st("minecraft:bedrock");
    const QString stone = st("minecraft:end_stone");
    const QString portal = st("minecraft:end_gateway");

    c.ground(rng, 6, 6, stone, stone, 2);
    // 原版折跃门是悬在空中的基岩方块阵：上下各一片 5×5 的基岩板（中间留空），
    // 四角立基岩柱把两片连起来，折跃门方块独自悬在正中。
    c.ring(-2, -2, 2, 2, 1, bedrock);
    c.ring(-2, -2, 2, 2, 4, bedrock);
    for (int sx : {-2, 2})
        for (int sz : {-2, 2})
            c.pillar(sx, sz, 2, 3, bedrock);
    c.put(0, 2, 0, portal);
    out->variant = QStringLiteral("末地折跃门");
}

// ────────────────────────────────────────────────────────────────────────────
// 废弃营地
// ────────────────────────────────────────────────────────────────────────────

void buildAbandonedCamp(Canvas &c, Rng &rng, const QString &,
                        StructureModelInfo *out)
{
    c.ground(rng, 13, 13, st("minecraft:grass_block"), st("minecraft:dirt"), 2);
    const QString wool = st("minecraft:white_wool");
    const QString log = st("minecraft:oak_log");
    const QString plank = st("minecraft:oak_planks");

    // 两顶帐篷
    c.fill(-7, 1, -4, -2, 1, 2, wool);
    gableRoof(c, -7, -4, -2, 2, 2, wool, wool, QString());
    c.put(-5, 1, -3, st("minecraft:campfire[lit=true,facing=south]"));
    c.fill(3, 1, 1, 7, 1, 5, st("minecraft:brown_wool"));
    gableRoof(c, 3, 1, 7, 5, 2, st("minecraft:brown_wool"),
              st("minecraft:brown_wool"), QString());

    // 篝火、原木凳与补给箱
    c.fill(0, 1, -1, 1, 1, 0, st("minecraft:campfire[lit=true,facing=west]"));
    c.fill(-1, 1, 3, 1, 1, 3, log);
    c.put(2, 1, -3, st("minecraft:chest[facing=west]"));
    c.put(4, 1, -2, st("minecraft:barrel[facing=up]"));
    c.put(-3, 1, 5, st("minecraft:crafting_table"));
    // 栅栏与路
    for (int x = -9; x <= 9; x += 3)
        c.put(x, 1, -7, st("minecraft:oak_fence"));
    c.fill(-9, 0, 9, 9, 0, 9, st("minecraft:dirt_path"));
    c.put(0, 1, 7, st("minecraft:torch"));
    // 树与草丛
    for (int i = 0; i < 4; ++i)
    {
        const int x = rng.range(-11, 11), z = rng.range(-11, 11);
        if (c.at(x, 1, z).isEmpty() && !c.at(x, 0, z).isEmpty())
            tree(c, rng, x, z, log, st("minecraft:oak_leaves"), 4);
    }
    for (int i = 0; i < 10; ++i)
        c.put(rng.range(-11, 11), 1, rng.range(-11, 11),
              st("minecraft:short_grass"));
    out->variant = QStringLiteral("废弃营地");
}

// ────────────────────────────────────────────────────────────────────────────
// 下界化石
// ────────────────────────────────────────────────────────────────────────────

void buildNetherFossil(Canvas &c, Rng &rng, const QString &,
                       StructureModelInfo *out)
{
    const QString bone = st("minecraft:bone_block[axis=x]");
    const QString boneY = st("minecraft:bone_block[axis=y]");
    c.ground(rng, 13, 9, st("minecraft:soul_sand"), st("minecraft:soul_soil"), 2);

    // 脊椎
    c.fill(-9, 1, 0, 8, 1, 0, bone);
    // 头骨
    c.fill(8, 1, -2, 10, 4, 2, st("minecraft:bone_block[axis=y]"));
    c.carveBox(9, 2, -1, 9, 3, 1);
    c.put(10, 3, 0, st("minecraft:coal_block"));
    // 肋骨：两侧拱起
    for (int x = -7; x <= 5; x += 2)
    {
        const int h = rng.range(3, 5);
        for (int y = 2; y <= h; ++y)
        {
            c.put(x, y, -1, boneY);
            c.put(x, y, 1, boneY);
        }
        c.put(x, h + 1, -1, st("minecraft:bone_block[axis=x]"));
        c.put(x, h + 1, 1, st("minecraft:bone_block[axis=x]"));
    }
    // 尾椎与半埋
    for (int x = -11; x <= -9; ++x)
        c.put(x, 1, 0, bone);
    for (int i = 0; i < 8; ++i)
        c.put(rng.range(-10, 10), 1, rng.range(-6, 6),
              st("minecraft:soul_sand"));
    c.put(-8, 1, 2, st("minecraft:soul_soil"));
    out->variant = QStringLiteral("下界化石");
}

// ────────────────────────────────────────────────────────────────────────────
// 末地船
// ────────────────────────────────────────────────────────────────────────────

void buildEndShip(Canvas &c, Rng &rng, const QString &,
                  StructureModelInfo *out)
{
    const QString purpur = st("minecraft:purpur_block");
    const QString stone = st("minecraft:end_stone_bricks");
    const QString pillarS = st("minecraft:purpur_pillar");
    const QString rod = st("minecraft:end_rod");

    // 下方浮岛与紫色传送门框
    c.ground(rng, 8, 6, st("minecraft:end_stone"), st("minecraft:end_stone"), 2);
    c.fill(-1, 1, -1, 1, 3, 1, st("minecraft:end_portal_frame[eye=true]"));
    c.fill(0, 2, 0, 0, 2, 0, st("minecraft:end_portal"));

    // 悬空的末地船
    for (int y = 5; y <= 9; ++y)
    {
        const int half = y == 5 ? 1 : (y == 6 ? 2 : 2);
        for (int x = -6; x <= 6; ++x)
        {
            int h = half;
            if (qAbs(x) >= 5)
                --h;
            for (int z = -h; z <= h; ++z)
                c.put(x, y, z,
                      (z == -h || z == h || y == 5) ? purpur : stone);
        }
    }
    for (int x = -5; x <= 5; ++x)
        for (int z = -1; z <= 1; ++z)
            for (int y = 6; y <= 8; ++y)
                c.carve(x, y, z);
    c.fill(-5, 9, -1, 5, 9, 1, purpur);
    // 船首与船尾
    c.pillar(6, 0, 5, 10, pillarS);
    c.pillar(-6, 0, 6, 11, purpur);
    // 桅杆与帆
    c.pillar(1, 0, 9, 18, pillarS);
    c.fill(1, 13, -2, 1, 17, -2, st("minecraft:white_wool"));
    c.fill(1, 13, 2, 1, 17, 2, st("minecraft:white_wool"));
    c.fill(1, 14, -2, 1, 16, 2, st("minecraft:white_wool"));
    // 船舱：潜影盒与末地烛
    c.put(-3, 6, 0, st("minecraft:shulker_box"));
    c.put(-2, 6, 0, st("minecraft:end_rod"));
    c.put(4, 10, 0, rod);
    c.put(-5, 10, 0, rod);
    out->variant = QStringLiteral("末地船");
}

// ────────────────────────────────────────────────────────────────────────────
// 原版结构模板逐块还原
// 数据取自官方客户端 jar 内 data/minecraft/structure/**.nbt（结构方块同款模板）
// ────────────────────────────────────────────────────────────────────────────

/**
 * @brief 模板内坐标的水平旋转（以模板自身包围盒为基准，旋转后仍落在原点一侧）
 *
 * 与原版 Blocks 放置的旋转一致：turns=1 对应顺时针 90°，y 不变。
 */
void rotateTemplatePoint(int turns, int x, int y, int z,
                         int sizeX, int sizeZ, int &outX, int &outZ)
{
    switch (((turns % 4) + 4) % 4)
    {
    case 1: outX = sizeZ - 1 - z; outZ = x; break;
    case 2: outX = sizeX - 1 - x; outZ = sizeZ - 1 - z; break;
    case 3: outX = z; outZ = sizeX - 1 - x; break;
    default: outX = x; outZ = z; break;
    }
    Q_UNUSED(y);
}

/**
 * @brief 把一段原版模板按自身旋转与偏移放进画布
 * @param turns 该模板自身的水平旋转（整座模型最后还会再整体旋转一次）
 * @param openTop 去掉模板顶部若干层（用于试炼密室这类完全封闭的地下结构，
 *                屋顶切开后才能看到里面的房间）
 * @return 实际放入的方块数（0 表示模板缺失）
 */
int placeTemplate(Canvas &c, const QString &templateId,
                  int ox, int oy, int oz, int turns = 0, int openTop = 0)
{
    const auto tpl = StructureTemplate::load(templateId);
    if (!tpl)
        return 0;

    int placed = 0;
    for (const StructureTemplateBlock &b : tpl->blocks())
    {
        if (openTop > 0 && b.y >= tpl->sizeY() - openTop)
            continue;
        QString s = b.stateOverride.isEmpty() ? tpl->stateOf(b.state)
                                              : b.stateOverride;
        if (s.isEmpty() || s == QLatin1String("minecraft:air"))
            continue;
        int px = b.x, pz = b.z;
        rotateTemplatePoint(turns, b.x, b.y, b.z, tpl->sizeX(), tpl->sizeZ(), px, pz);
        if (turns != 0)
            s = rotateState(s, turns);
        c.put(ox + px, oy + b.y, oz + pz, s);
        ++placed;
    }
    return placed;
}

/** 从候选模板里按种子挑一个 */
QString pickTemplate(Rng &rng, const char *const *ids, int count)
{
    return st(ids[rng.next(count)]);
}

// ────────────────────────────────────────────────────────────────────────────
// 拼图（jigsaw）组装
//
// 村庄、远古城市这类结构在原版里并不是“一张大模板”，而是由数据包里的模板池
// （data/minecraft/worldgen/template_pool/**.json）驱动：每段模板带若干拼图方块，
// 父拼图点按自己的 pool 选子模板、按 target 找子拼图点，让两个拼图点正面贴合，
// 然后各自递归展开。这里实现同一套规则（忽略 processor 的随机替换与 rollable
// 的三维翻滚），使预览页能给出与原版同级的街区布局。
// ────────────────────────────────────────────────────────────────────────────

/** 模板池里的一个候选项（list 元素会展开成多个子项） */
struct PoolElement
{
    QString location;              ///< 模板 id（已去掉 minecraft: 前缀）
    int weight = 1;
    QVector<PoolElement> children;
    bool usable = false;
};

QString poolFilePath(const QString &pool)
{
    QString id = pool;
    if (id.startsWith(QLatin1String("minecraft:")))
        id.remove(0, 10);
    return QStringLiteral(":/utils/WorldMap/worldgen/template_pool/") + id
        + QStringLiteral(".json");
}

void parsePoolElement(const QJsonObject &obj, PoolElement &out)
{
    const QString type = obj.value(QStringLiteral("element_type")).toString();
    if (type.endsWith(QLatin1String("list_pool_element")))
    {
        const QJsonArray children = obj.value(QStringLiteral("elements")).toArray();
        for (const QJsonValue &value : children)
        {
            PoolElement child;
            parsePoolElement(value.toObject(), child);
            if (child.usable)
                out.children.append(child);
        }
        out.usable = !out.children.isEmpty();
        return;
    }

    QString location = obj.value(QStringLiteral("location")).toString();
    if (location.isEmpty())
        return; // empty / feature 元素：预览里不产生方块
    if (location.startsWith(QLatin1String("minecraft:")))
        location.remove(0, 10);
    out.location = location;
    out.usable = true;
}

QVector<PoolElement> loadPool(const QString &pool)
{
    static QHash<QString, QVector<PoolElement>> cache;
    const auto cached = cache.constFind(pool);
    if (cached != cache.constEnd())
        return cached.value();

    QVector<PoolElement> elements;
    QFile file(poolFilePath(pool));
    if (file.open(QIODevice::ReadOnly))
    {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonArray array =
            doc.object().value(QStringLiteral("elements")).toArray();
        for (const QJsonValue &value : array)
        {
            const QJsonObject entry = value.toObject();
            PoolElement element;
            element.weight = qMax(1, entry.value(QStringLiteral("weight")).toInt(1));
            parsePoolElement(entry.value(QStringLiteral("element")).toObject(), element);
            if (element.usable)
                elements.append(element);
        }
    }
    // 僵尸村庄的模板没有随包内置，预览里统一走正常版本
    QVector<PoolElement> filtered;
    for (const PoolElement &element : elements)
        if (!element.location.contains(QLatin1String("/zombie/")))
            filtered.append(element);
    if (!filtered.isEmpty())
        elements = filtered;
    cache.insert(pool, elements);
    return elements;
}

/** 按权重从模板池里抽一个候选模板 */
bool pickPoolElement(Rng &rng, const QVector<PoolElement> &pool, PoolElement &out)
{
    QVector<const PoolElement *> flat;
    QVector<int> weights;
    int total = 0;
    for (const PoolElement &element : pool)
    {
        if (element.children.isEmpty())
        {
            flat.append(&element);
            weights.append(element.weight);
            total += element.weight;
        }
        else
        {
            for (const PoolElement &child : element.children)
            {
                flat.append(&child);
                weights.append(element.weight);
                total += element.weight;
            }
        }
    }
    if (flat.isEmpty() || total <= 0)
        return false;

    int roll = rng.next(total);
    for (int i = 0; i < flat.size(); ++i)
    {
        roll -= weights.at(i);
        if (roll < 0)
        {
            out = *flat.at(i);
            return true;
        }
    }
    out = *flat.last();
    return true;
}

/** 从模板池里抽一个能真正载入的模板（跳过僵尸村庄等未随包内置的变体） */
bool pickUsableTemplate(Rng &rng, const QString &pool, QString &id)
{
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        PoolElement element;
        if (!pickPoolElement(rng, loadPool(pool), element))
            return false;
        if (element.location.isEmpty())
            continue;
        const auto tpl = StructureTemplate::load(element.location);
        if (!tpl || tpl->sizeX() <= 0 || tpl->sizeZ() <= 0)
            continue;
        id = element.location;
        return true;
    }
    return false;
}

/** 拼图朝向的水平分量（north=0 east=1 south=2 west=3，与 rotateState 一致） */
int facingIndex(const QString &orientation)
{
    if (orientation.isEmpty())
        return -1;
    const QChar head = orientation.at(0);
    if (head == QLatin1Char('n'))
        return 0;
    if (head == QLatin1Char('e'))
        return 1;
    if (head == QLatin1Char('s'))
        return 2;
    if (head == QLatin1Char('w'))
        return 3;
    return -1; // up / down
}

/** 竖直朝向：+1 朝上，-1 朝下，0 非竖直 */
int verticalFacing(const QString &orientation)
{
    if (orientation.startsWith(QLatin1String("up")))
        return 1;
    if (orientation.startsWith(QLatin1String("down")))
        return -1;
    return 0;
}

void facingDirection(int facing, int &dx, int &dz)
{
    static const int kDx[] = {0, 1, 0, -1};
    static const int kDz[] = {-1, 0, 1, 0};
    dx = (facing >= 0 && facing < 4) ? kDx[facing] : 0;
    dz = (facing >= 0 && facing < 4) ? kDz[facing] : 0;
}


/** 已放置的一段拼图（旧版近似拼装用） */
struct JigsawPiece
{
    QString id;
    int ox = 0, oy = 0, oz = 0;
    int turns = 0;
    int depth = 0;
    int bx0 = 0, by0 = 0, bz0 = 0, bx1 = -1, by1 = -1, bz1 = -1;
};

bool boxesOverlap(const JigsawPiece &a, const JigsawPiece &b)
{
    return a.bx0 <= b.bx1 && b.bx0 <= a.bx1
        && a.by0 <= b.by1 && b.by0 <= a.by1
        && a.bz0 <= b.bz1 && b.bz0 <= a.bz1;
}

/**
 * @brief 按原版拼图规则拼装一座结构
 * @param startPool  起始模板池（对应结构 json 的 start_pool）
 * @param maxDepth   展开层数（对应结构 json 的 size）
 * @param maxPieces  最多放置的段数（限制预览规模）
 * @param maxRadius  离原点最远距离（对应 max_distance_from_center）
 * @return 实际放置的段数；0 表示该结构池不可用
 */
int assembleJigsaw(Canvas &c, Rng &rng, const QString &startPool,
                   int maxDepth, int maxPieces, int maxRadius, int openTop = 0)
{
    QVector<JigsawPiece> pieces;

    auto addPiece = [&](const QString &id, int ox, int oy, int oz,
                        int turns, int depth) -> bool {
        const auto tpl = StructureTemplate::load(id);
        if (!tpl || tpl->sizeX() <= 0 || tpl->sizeZ() <= 0)
            return false;
        JigsawPiece piece;
        piece.id = id;
        piece.ox = ox;
        piece.oy = oy;
        piece.oz = oz;
        piece.turns = ((turns % 4) + 4) % 4;
        piece.depth = depth;
        const int sx = (piece.turns & 1) ? tpl->sizeZ() : tpl->sizeX();
        const int sz = (piece.turns & 1) ? tpl->sizeX() : tpl->sizeZ();
        piece.bx0 = ox;
        piece.by0 = oy;
        piece.bz0 = oz;
        piece.bx1 = ox + sx - 1;
        piece.by1 = oy + tpl->sizeY() - 1;
        piece.bz1 = oz + sz - 1;

        if (maxRadius > 0)
        {
            const int reach = qMax(qMax(qAbs(piece.bx0), qAbs(piece.bx1)),
                                   qMax(qAbs(piece.bz0), qAbs(piece.bz1)));
            if (reach > maxRadius)
                return false;
        }
        if (!pieces.isEmpty())
        {
            if (pieces.size() >= maxPieces)
                return false;
            // 道路、街道这类贴地薄片在原版里本来就互相叠压，也有铺在房屋地基下的；
            // 真正不能重叠的是“有高度的建筑”，所以只有双方都是建筑时才做碰撞检查
            const bool solid = piece.by1 - piece.by0 >= 3;
            for (const JigsawPiece &other : pieces)
            {
                if (other.id == piece.id && other.ox == piece.ox
                    && other.oy == piece.oy && other.oz == piece.oz
                    && other.turns == piece.turns)
                    return false;
                if (solid && other.by1 - other.by0 >= 3
                    && boxesOverlap(piece, other))
                    return false;
            }
        }
        pieces.append(piece);
        return true;
    };

    QString startId;
    if (!pickUsableTemplate(rng, startPool, startId))
        return 0;
    if (!addPiece(startId, 0, 0, 0, rng.next(4), 0))
        return 0;

    // 深度优先：沿一条街道一直延伸，碰到岔路和房屋入口再折回，
    // 这样不会像广度优先那样被一堆直路占满配额
    QVector<int> pending;
    pending.append(0);
    while (!pending.isEmpty())
    {
        const int cursor = pending.takeLast();
        if (cursor >= pieces.size())
            continue;
        const JigsawPiece parent = pieces.at(cursor); // 拷贝：后面会继续 append
        if (parent.depth >= maxDepth)
            continue;
        const auto parentTpl = StructureTemplate::load(parent.id);
        if (!parentTpl)
            continue;

        for (const StructureTemplateJigsaw &pj : parentTpl->jigsaws())
        {
            if (pieces.size() >= maxPieces)
                break;
            if (pj.pool.isEmpty())
                continue;

            const int localFacing = facingIndex(pj.orientation);
            const int parentFacing =
                localFacing < 0 ? -1 : (localFacing + parent.turns) % 4;
            const int parentVertical = verticalFacing(pj.orientation);
            if (parentFacing < 0 && parentVertical == 0)
                continue;

            // 父拼图点在世界里的位置
            int wx = 0, wz = 0;
            rotateTemplatePoint(parent.turns, pj.x, pj.y, pj.z,
                                parentTpl->sizeX(), parentTpl->sizeZ(), wx, wz);
            const int worldX = parent.ox + wx;
            const int worldY = parent.oy + pj.y;
            const int worldZ = parent.oz + wz;

            // 同一拼图点最多试几个候选：放不下就换一个（原版也是抽一个、放不下就断开）
            for (int attempt = 0; attempt < 6; ++attempt)
            {
                QString candidateId;
                if (!pickUsableTemplate(rng, pj.pool, candidateId))
                    break;
                const auto childTplPtr = StructureTemplate::load(candidateId);
                if (!childTplPtr)
                    continue;

                // 子模板里与本拼图点对接的那一格
                const StructureTemplateJigsaw *childJigsaw = nullptr;
                for (const StructureTemplateJigsaw &cj : childTplPtr->jigsaws())
                {
                    if (!pj.target.isEmpty() && cj.name != pj.target)
                        continue;
                    if (parentFacing >= 0)
                    {
                        if (facingIndex(cj.orientation) < 0)
                            continue;
                    }
                    else if (verticalFacing(cj.orientation) != -parentVertical)
                    {
                        continue;
                    }
                    childJigsaw = &cj;
                    break;
                }
                if (!childJigsaw)
                    continue;

                int childTurns = 0;
                if (parentFacing >= 0)
                {
                    const int childFacing = facingIndex(childJigsaw->orientation);
                    childTurns = ((parentFacing + 2 - childFacing) % 4 + 4) % 4;
                }
                else
                {
                    childTurns = rng.next(4);
                }

                int cx = 0, cz = 0;
                rotateTemplatePoint(childTurns, childJigsaw->x, childJigsaw->y,
                                    childJigsaw->z, childTplPtr->sizeX(),
                                    childTplPtr->sizeZ(), cx, cz);

                int dx = 0, dz = 0;
                facingDirection(parentFacing, dx, dz);
                const int dy = (parentFacing >= 0) ? 0 : parentVertical;
                const int ox = worldX + dx - cx;
                const int oy = worldY + dy - childJigsaw->y;
                const int oz = worldZ + dz - cz;

                if (addPiece(candidateId, ox, oy, oz, childTurns,
                             parent.depth + 1))
                {
                    pending.append(pieces.size() - 1);
                    break;
                }
            }
        }
    }

    for (const JigsawPiece &piece : pieces)
        placeTemplate(c, piece.id, piece.ox, piece.oy, piece.oz, piece.turns,
                      openTop);
    return pieces.size();
}

/** 雪屋：圆顶 + 3 格梯子通道 + 地下室（三段均为原版模板，梯子列对齐） */
bool buildIglooFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    const bool basement = rng.chance(50);
    if (placeTemplate(c, QStringLiteral("igloo/top"), 0, 0, 0) <= 0)
        return false;
    if (basement)
    {
        // 相对偏移取自原版拼图位置：三段梯子都落在同一列 (3, *, 5)
        placeTemplate(c, QStringLiteral("igloo/middle"), 2, -3, 4);
        placeTemplate(c, QStringLiteral("igloo/bottom"), 0, -9, -2);
    }
    out->variant = basement ? QStringLiteral("雪屋（地下室已生成）")
                            : QStringLiteral("雪屋");
    out->fromVanillaTemplate = true;
    return true;
}

/** 沉船：整船模板（含桅杆 / 正立 / 倒扣 / 侧翻，按种子决定破损程度） */
bool buildShipwreckFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    static const char *const ids[] = {
        "shipwreck/with_mast",
        "shipwreck/rightsideup_full",
        "shipwreck/upsidedown_full",
        "shipwreck/sideways_full",
    };
    QString id = pickTemplate(rng, ids, 4);
    const bool degraded = rng.chance(35);
    if (degraded)
        id += QStringLiteral("_degraded");
    if (placeTemplate(c, id, 0, 0, 0) <= 0)
        return false;

    QString variant = QStringLiteral("沉船");
    if (id.contains(QLatin1String("with_mast")))
        variant = QStringLiteral("带桅杆的沉船");
    else if (id.contains(QLatin1String("upsidedown")))
        variant = QStringLiteral("底朝上的沉船");
    else if (id.contains(QLatin1String("sideways")))
        variant = QStringLiteral("侧翻的沉船");
    if (degraded)
        variant += QStringLiteral("（残破）");
    out->variant = variant;
    out->fromVanillaTemplate = true;
    return true;
}

/** 水下遗迹：暖 / 寒海域 + 大 / 小 + 序号，均取自原版模板池 */
bool buildOceanRuinFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    const bool warm = rng.chance(50);
    const bool large = rng.chance(30);
    QString id;
    if (warm)
    {
        if (large)
        {
            static const char *const ids[] = {
                "underwater_ruin/big_warm_4", "underwater_ruin/big_warm_5",
                "underwater_ruin/big_warm_6", "underwater_ruin/big_warm_7",
            };
            id = pickTemplate(rng, ids, 4);
        }
        else
        {
            id = QStringLiteral("underwater_ruin/warm_%1").arg(rng.range(1, 8));
        }
    }
    else if (large)
    {
        static const char *const ids[] = {
            "underwater_ruin/big_brick_1",   "underwater_ruin/big_brick_2",
            "underwater_ruin/big_brick_3",   "underwater_ruin/big_brick_8",
            "underwater_ruin/big_cracked_1", "underwater_ruin/big_cracked_2",
            "underwater_ruin/big_cracked_3", "underwater_ruin/big_cracked_8",
            "underwater_ruin/big_mossy_1",   "underwater_ruin/big_mossy_2",
            "underwater_ruin/big_mossy_3",   "underwater_ruin/big_mossy_8",
        };
        id = pickTemplate(rng, ids, 12);
    }
    else
    {
        static const char *const kinds[] = {"brick", "cracked", "mossy"};
        id = QStringLiteral("underwater_ruin/%1_%2")
                 .arg(st(kinds[rng.next(3)]))
                 .arg(rng.range(1, 8));
    }
    if (placeTemplate(c, id, 0, 0, 0) <= 0)
        return false;

    out->variant = warm ? QStringLiteral("温暖海域的水下遗迹")
                        : QStringLiteral("寒冷海域的水下遗迹");
    out->fromVanillaTemplate = true;
    return true;
}

/** 废弃传送门：普通 / 巨型，均取自原版模板 */
bool buildRuinedPortalFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    const bool giant = rng.chance(15);
    const QString id = giant
        ? QStringLiteral("ruined_portal/giant_portal_%1").arg(rng.range(1, 3))
        : QStringLiteral("ruined_portal/portal_%1").arg(rng.range(1, 10));
    if (placeTemplate(c, id, 0, 0, 0) <= 0)
        return false;

    out->variant = giant ? QStringLiteral("巨型废弃传送门")
                         : QStringLiteral("废弃传送门");
    out->fromVanillaTemplate = true;
    return true;
}

/** 下界化石：14 种骨架之一 */
bool buildNetherFossilFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    const QString id = QStringLiteral("nether_fossils/fossil_%1").arg(rng.range(1, 14));
    if (placeTemplate(c, id, 0, 0, 0) <= 0)
        return false;

    out->variant = QStringLiteral("下界化石");
    out->fromVanillaTemplate = true;
    return true;
}

/** 末地船：末地城船体模板 */
bool buildEndShipFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    Q_UNUSED(rng);
    if (placeTemplate(c, QStringLiteral("end_city/ship"), 0, 0, 0) <= 0)
        return false;

    out->variant = QStringLiteral("末地船");
    out->fromVanillaTemplate = true;
    return true;
}

/** 掠夺者哨塔：主塔模板（塔基为原版空模板，仅含拼图点，不产生方块） */
bool buildOutpostFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    const bool overgrown = rng.chance(30);
    const QString id = overgrown ? QStringLiteral("pillager_outpost/watchtower_overgrown")
                                 : QStringLiteral("pillager_outpost/watchtower");
    if (placeTemplate(c, id, 0, 0, 0) <= 0)
        return false;

    out->variant = overgrown ? QStringLiteral("掠夺者哨塔（爬满藤蔓）")
                             : QStringLiteral("掠夺者哨塔");
    out->fromVanillaTemplate = true;
    return true;
}

/** 古迹废墟：塔体模板（遗迹主体） */
bool buildTrailRuinsFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    const QString id = QStringLiteral("trail_ruins/tower/tower_%1").arg(rng.range(1, 5));
    if (placeTemplate(c, id, 0, 0, 0) <= 0)
        return false;

    out->variant = QStringLiteral("古迹废墟塔体");
    out->fromVanillaTemplate = true;
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// 原版拼图（JigsawPlacement）精确复刻
//
// 对照 1.21.4 反编译代码逐行还原：
//   JigsawStructure.findGenerationPoint
//   JigsawPlacement.addPieces / Placer.a
//   StructureTemplatePool.getRandomTemplate / getShuffledTemplates
//   SinglePoolElement.getShuffledJigsawBlocks / getBoundingBox
//   StructureTemplate.getJigsaws / transform / getBoundingBox
//   JigsawBlock.canAttach
//
// 随机源是 LegacyRandomSource（48 位 Java LCG），种子按
// WorldgenRandom.setLargeFeatureSeed(seed, chunkX, chunkZ) 混合，
// 因此同一个世界种子、同一座村庄算出的段序、朝向、位置与游戏内一致。
// ────────────────────────────────────────────────────────────────────────────

/** 原版 LegacyRandomSource（java.util.Random 的 48 位 LCG） */
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
    bool nextBoolean() { return next(1) != 0; }
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
            // Java int 溢出成负数时重抽
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
// 海底神殿：按原版 OceanMonumentPieces（1.21.4）逐块还原
//
// 对照 1.21.4 反编译源码逐行还原：75 格房间网格、按种子抽取房间拼装、
// 外墙 / 入口 / 屋顶 / 四座翼楼 / 顶层，以及每个房间内部的金块室、海晶灯、
// 湿海绵与门洞。随机源是原版 LegacyRandomSource，种子按
// WorldgenRandom.setLargeFeatureSeed(seed, chunkX, chunkZ) 混合，因此同一个
// 世界种子算出的神殿与游戏内实际生成的一致。
// ────────────────────────────────────────────────────────────────────────────

namespace monument {

/** 方块种类（与世界地图预览用的方块字符串一一对应） */
enum State : quint8
{
    S_AIR = 0,
    S_WATER,
    S_GRAY,   ///< 海晶石
    S_LIGHT,  ///< 海晶石砖
    S_BLACK,  ///< 暗海晶石
    S_LAMP,   ///< 海晶灯
    S_GOLD,   ///< 金块
    S_SPONGE  ///< 湿海绵
};

/** 与原版 Direction.get3DDataValue() 一致 */
enum Dir { D_DOWN = 0, D_UP = 1, D_NORTH = 2, D_SOUTH = 3, D_WEST = 4, D_EAST = 5 };

inline int dirDx(int d) { return d == D_EAST ? 1 : (d == D_WEST ? -1 : 0); }
inline int dirDy(int d) { return d == D_DOWN ? -1 : (d == D_UP ? 1 : 0); }
inline int dirDz(int d) { return d == D_NORTH ? -1 : (d == D_SOUTH ? 1 : 0); }
inline int dirOpp(int d) { return d ^ 1; }
inline bool dirAxisZ(int d) { return d == D_NORTH || d == D_SOUTH; }

QString monumentBlockName(quint8 s)
{
    switch (s)
    {
    case S_GRAY: return st("minecraft:prismarine");
    case S_LIGHT: return st("minecraft:prismarine_bricks");
    case S_BLACK: return st("minecraft:dark_prismarine");
    case S_LAMP: return st("minecraft:sea_lantern");
    case S_GOLD: return st("minecraft:gold_block");
    case S_SPONGE: return st("minecraft:wet_sponge");
    case S_WATER: return st("minecraft:water[level=0]");
    default: return QString();
    }
}

/** 闭区间包围盒 */
struct Box
{
    int x0 = 0, y0 = 0, z0 = 0, x1 = -1, y1 = -1, z1 = -1;

    bool has(int x, int y, int z) const
    {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1;
    }
    bool hits(const Box &o) const
    {
        return x0 <= o.x1 && o.x0 <= x1 && y0 <= o.y1 && o.y0 <= y1
            && z0 <= o.z1 && o.z0 <= z1;
    }
    void move(int dx, int dy, int dz)
    {
        x0 += dx; x1 += dx;
        y0 += dy; y1 += dy;
        z0 += dz; z1 += dz;
    }
    static Box corners(int ax, int ay, int az, int bx, int by, int bz)
    {
        Box b;
        b.x0 = std::min(ax, bx); b.x1 = std::max(ax, bx);
        b.y0 = std::min(ay, by); b.y1 = std::max(ay, by);
        b.z0 = std::min(az, bz); b.z1 = std::max(az, bz);
        return b;
    }
    /** StructurePiece.makeBoundingBox */
    static Box make(int x, int y, int z, int dir, int sx, int sy, int sz)
    {
        if (dirAxisZ(dir))
            return Box{x, y, z, x + sx - 1, y + sy - 1, z + sz - 1};
        return Box{x, y, z, x + sz - 1, y + sy - 1, z + sx - 1};
    }
};

/** 世界方块表：未写入的格子按海平面补成水 / 空气 */
struct Grid
{
    QHash<quint64, quint8> cells;
    int seaLevel = 63;

    static quint64 key(int x, int y, int z)
    {
        return quint64(x + 1024) | (quint64(y + 1024) << 12)
             | (quint64(z + 1024) << 24);
    }
    quint8 at(int x, int y, int z) const
    {
        const auto it = cells.constFind(key(x, y, z));
        if (it != cells.constEnd())
            return it.value();
        return (y < seaLevel) ? S_WATER : S_AIR;
    }
    void put(int x, int y, int z, quint8 s) { cells.insert(key(x, y, z), s); }

    static void unkey(quint64 k, int &x, int &y, int &z)
    {
        x = int(k & 0xFFF) - 1024;
        y = int((k >> 12) & 0xFFF) - 1024;
        z = int((k >> 24) & 0xFFF) - 1024;
    }
};

/** 房间节点（原版 RoomDefinition） */
struct Room
{
    int index = 0;
    Room *con[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    bool open[6] = {false, false, false, false, false, false};
    bool claimed = false;
    bool isSource = false;
    int scanIndex = 0;

    explicit Room(int idx) : index(idx) {}

    void setConnection(int d, Room *o)
    {
        con[d] = o;
        o->con[dirOpp(d)] = this;
    }
    void updateOpenings()
    {
        for (int i = 0; i < 6; ++i)
            open[i] = (con[i] != nullptr);
    }
    bool findSource(int id)
    {
        if (isSource)
            return true;
        scanIndex = id;
        for (int i = 0; i < 6; ++i)
        {
            if (con[i] && open[i] && con[i]->scanIndex != id
                && con[i]->findSource(id))
                return true;
        }
        return false;
    }
    int countOpenings() const
    {
        int n = 0;
        for (int i = 0; i < 6; ++i)
            if (open[i])
                ++n;
        return n;
    }
};

/** 与 OceanMonumentPiece.getRoomIndex 一致 */
inline int roomIndex(int x, int y, int z) { return y * 25 + z * 5 + x; }

/**
 * @brief 一个结构段（房间 / 翼楼 / 顶层 / 主体）：坐标换算与批量放块
 *
 * 所有局部坐标与原版一致：x/z 相对段自身包围盒，y 再叠加包围盒 minY；
 * 世界 Y = baseY(39) + 局部 Y。方块朝向在海晶石系方块上无影响，故只换算坐标。
 */
struct Piece
{
    int kind = 0;
    Box box;
    int orient = -1;
    Room *room = nullptr;
    int design = 0;
    Grid *grid = nullptr;
    int baseY = 39;

    int px(int lx, int lz) const
    {
        if (orient < 0)
            return lx;
        if (dirAxisZ(orient))
            return box.x0 + lx;
        return (orient == D_WEST) ? box.x1 - lz : box.x0 + lz;
    }
    int pz(int lx, int lz) const
    {
        if (orient < 0)
            return lz;
        switch (orient)
        {
        case D_NORTH: return box.z1 - lz;
        case D_SOUTH: return box.z0 + lz;
        default: return box.z0 + lx; // 东 / 西
        }
    }
    int py(int ly) const { return baseY + (orient < 0 ? ly : box.y0 + ly); }

    void put(const Box &clip, quint8 s, int lx, int ly, int lz)
    {
        const int x = px(lx, lz), y = py(ly), z = pz(lx, lz);
        if (clip.has(x, y, z))
            grid->put(x, y, z, s);
    }
    quint8 at(const Box &clip, int lx, int ly, int lz) const
    {
        const int x = px(lx, lz), y = py(ly), z = pz(lx, lz);
        if (!clip.has(x, y, z))
            return S_AIR;
        return grid->at(x, y, z);
    }

    /** StructurePiece.generateBox：外圈 edge、内部 fill */
    void boxFill(const Box &clip, int x0, int y0, int z0, int x1, int y1, int z1,
                 quint8 edge, quint8 fill, bool skipAir)
    {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (int z = z0; z <= z1; ++z)
                {
                    if (skipAir && at(clip, x, y, z) == S_AIR)
                        continue;
                    const bool shell = (x == x0 || x == x1 || y == y0 || y == y1
                                        || z == z0 || z == z1);
                    put(clip, shell ? edge : fill, x, y, z);
                }
    }

    /** OceanMonumentPiece.generateBoxOnFillOnly：只替换水 */
    void boxOnWater(const Box &clip, int x0, int y0, int z0, int x1, int y1, int z1,
                    quint8 s)
    {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (int z = z0; z <= z1; ++z)
                    if (at(clip, x, y, z) == S_WATER)
                        put(clip, s, x, y, z);
    }

    /** OceanMonumentPiece.generateWaterBox：海平面以上清空、以下注水 */
    void waterBox(const Box &clip, int x0, int y0, int z0, int x1, int y1, int z1)
    {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (int z = z0; z <= z1; ++z)
                {
                    const quint8 s = at(clip, x, y, z);
                    if (s == S_WATER)
                        continue;
                    if (py(y) >= grid->seaLevel)
                        put(clip, S_AIR, x, y, z);
                    else
                        put(clip, S_WATER, x, y, z);
                }
    }

    /** StructurePiece.fillColumnDown：向下补柱子直到碰到固体（预览止于基座之下若干格） */
    void columnDown(const Box &clip, quint8 s, int lx, int ly, int lz)
    {
        const int x = px(lx, lz), z = pz(lx, lz);
        int y = py(ly);
        if (!clip.has(x, y, z))
            return;
        while (y > baseY - 5)
        {
            const quint8 cur = grid->at(x, y, z);
            if (cur != S_AIR && cur != S_WATER)
                break;
            grid->put(x, y, z, s);
            --y;
        }
    }

    /** OceanMonumentPiece.generateDefaultFloor */
    void defaultFloor(const Box &clip, int x, int z, bool detailed)
    {
        if (detailed)
        {
            boxFill(clip, x + 0, 0, z + 0, x + 2, 0, z + 7, S_GRAY, S_GRAY, false);
            boxFill(clip, x + 5, 0, z + 0, x + 7, 0, z + 7, S_GRAY, S_GRAY, false);
            boxFill(clip, x + 3, 0, z + 0, x + 4, 0, z + 2, S_GRAY, S_GRAY, false);
            boxFill(clip, x + 3, 0, z + 5, x + 4, 0, z + 7, S_GRAY, S_GRAY, false);
            boxFill(clip, x + 3, 0, z + 2, x + 4, 0, z + 2, S_LIGHT, S_LIGHT, false);
            boxFill(clip, x + 3, 0, z + 5, x + 4, 0, z + 5, S_LIGHT, S_LIGHT, false);
            boxFill(clip, x + 2, 0, z + 3, x + 2, 0, z + 4, S_LIGHT, S_LIGHT, false);
            boxFill(clip, x + 5, 0, z + 3, x + 5, 0, z + 4, S_LIGHT, S_LIGHT, false);
        }
        else
        {
            boxFill(clip, x + 0, 0, z + 0, x + 7, 0, z + 7, S_GRAY, S_GRAY, false);
        }
    }

    /** OceanMonumentPiece.chunkIntersects（只看 x/z） */
    bool chunkHits(const Box &clip, int x0, int z0, int x1, int z1) const
    {
        const int ax = px(x0, z0), az = pz(x0, z0);
        const int bx = px(x1, z1), bz = pz(x1, z1);
        const int minX = std::min(ax, bx), maxX = std::max(ax, bx);
        const int minZ = std::min(az, bz), maxZ = std::max(az, bz);
        return clip.x0 <= maxX && minX <= clip.x1 && clip.z0 <= maxZ
            && minZ <= clip.z1;
    }
};

/** 房间段类型 */
enum Kind
{
    K_ENTRY = 0,
    K_CORE,
    K_DOUBLE_X,
    K_DOUBLE_XY,
    K_DOUBLE_Y,
    K_DOUBLE_YZ,
    K_DOUBLE_Z,
    K_SIMPLE,
    K_SIMPLE_TOP,
    K_WING,
    K_PENTHOUSE
};

// ── 房间内部 ────────────────────────────────────────────────────────────────

void roomEntry(Piece &p, const Box &clip)
{
    Room *r = p.room;
    p.boxFill(clip, 0, 3, 0, 2, 3, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 3, 0, 7, 3, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 0, 2, 0, 1, 2, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, 2, 0, 7, 2, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 0, 1, 0, 0, 1, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 7, 1, 0, 7, 1, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 0, 1, 7, 7, 3, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 1, 0, 2, 3, 0, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 1, 0, 6, 3, 0, S_LIGHT, S_LIGHT, false);
    if (r->open[D_NORTH])
        p.waterBox(clip, 3, 1, 7, 4, 2, 7);
    if (r->open[D_WEST])
        p.waterBox(clip, 0, 1, 3, 1, 2, 4);
    if (r->open[D_EAST])
        p.waterBox(clip, 6, 1, 3, 7, 2, 4);
}

void roomCore(Piece &p, const Box &clip)
{
    p.boxOnWater(clip, 1, 8, 0, 14, 8, 14, S_GRAY);
    p.boxFill(clip, 0, 7, 0, 0, 7, 15, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 15, 7, 0, 15, 7, 15, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 7, 0, 15, 7, 0, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 7, 15, 14, 7, 15, S_LIGHT, S_LIGHT, false);

    for (int y = 1; y <= 6; ++y)
    {
        quint8 s = S_LIGHT;
        if (y == 2 || y == 6)
            s = S_GRAY;
        for (int x = 0; x <= 15; x += 15)
        {
            p.boxFill(clip, x, y, 0, x, y, 1, s, s, false);
            p.boxFill(clip, x, y, 6, x, y, 9, s, s, false);
            p.boxFill(clip, x, y, 14, x, y, 15, s, s, false);
        }
        p.boxFill(clip, 1, y, 0, 1, y, 0, s, s, false);
        p.boxFill(clip, 6, y, 0, 9, y, 0, s, s, false);
        p.boxFill(clip, 14, y, 0, 14, y, 0, s, s, false);
        p.boxFill(clip, 1, y, 15, 14, y, 15, s, s, false);
    }

    p.boxFill(clip, 6, 3, 6, 9, 6, 9, S_BLACK, S_BLACK, false);
    p.boxFill(clip, 7, 4, 7, 8, 5, 8, S_GOLD, S_GOLD, false);

    for (int y = 3; y <= 6; y += 3)
        for (int z = 6; z <= 9; z += 3)
        {
            p.put(clip, S_LAMP, z, y, 6);
            p.put(clip, S_LAMP, z, y, 9);
        }

    p.boxFill(clip, 5, 1, 6, 5, 2, 6, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 1, 9, 5, 2, 9, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 10, 1, 6, 10, 2, 6, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 10, 1, 9, 10, 2, 9, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, 1, 5, 6, 2, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 9, 1, 5, 9, 2, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, 1, 10, 6, 2, 10, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 9, 1, 10, 9, 2, 10, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 2, 5, 5, 6, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 2, 10, 5, 6, 10, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 10, 2, 5, 10, 6, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 10, 2, 10, 10, 6, 10, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 7, 1, 5, 7, 6, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 10, 7, 1, 10, 7, 6, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 7, 9, 5, 7, 14, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 10, 7, 9, 10, 7, 14, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 7, 5, 6, 7, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 7, 10, 6, 7, 10, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 9, 7, 5, 14, 7, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 9, 7, 10, 14, 7, 10, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 2, 1, 2, 2, 1, 3, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 3, 1, 2, 3, 1, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 13, 1, 2, 13, 1, 3, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 12, 1, 2, 12, 1, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 2, 1, 12, 2, 1, 13, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 3, 1, 13, 3, 1, 13, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 13, 1, 12, 13, 1, 13, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 12, 1, 13, 12, 1, 13, S_LIGHT, S_LIGHT, false);
}

void roomDoubleX(Piece &p, const Box &clip)
{
    Room *room = p.room;
    Room *east = room->con[D_EAST];
    if (room->index / 25 > 0)
    {
        p.defaultFloor(clip, 8, 0, east->open[D_DOWN]);
        p.defaultFloor(clip, 0, 0, room->open[D_DOWN]);
    }
    if (room->con[D_UP] == nullptr)
        p.boxOnWater(clip, 1, 4, 1, 7, 4, 6, S_GRAY);
    if (east->con[D_UP] == nullptr)
        p.boxOnWater(clip, 8, 4, 1, 14, 4, 6, S_GRAY);

    p.boxFill(clip, 0, 3, 0, 0, 3, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 15, 3, 0, 15, 3, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 3, 0, 15, 3, 0, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 3, 7, 14, 3, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 0, 2, 0, 0, 2, 7, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 15, 2, 0, 15, 2, 7, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 1, 2, 0, 15, 2, 0, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 1, 2, 7, 14, 2, 7, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 0, 1, 0, 0, 1, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 15, 1, 0, 15, 1, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 1, 0, 15, 1, 0, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 1, 7, 14, 1, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 1, 0, 10, 1, 4, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, 2, 0, 9, 2, 3, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 5, 3, 0, 10, 3, 4, S_LIGHT, S_LIGHT, false);
    p.put(clip, S_LAMP, 6, 2, 3);
    p.put(clip, S_LAMP, 9, 2, 3);
    if (room->open[D_SOUTH])
        p.waterBox(clip, 3, 1, 0, 4, 2, 0);
    if (room->open[D_NORTH])
        p.waterBox(clip, 3, 1, 7, 4, 2, 7);
    if (room->open[D_WEST])
        p.waterBox(clip, 0, 1, 3, 0, 2, 4);
    if (east->open[D_SOUTH])
        p.waterBox(clip, 11, 1, 0, 12, 2, 0);
    if (east->open[D_NORTH])
        p.waterBox(clip, 11, 1, 7, 12, 2, 7);
    if (east->open[D_EAST])
        p.waterBox(clip, 15, 1, 3, 15, 2, 4);
}

void roomDoubleXY(Piece &p, const Box &clip)
{
    Room *room = p.room;
    Room *east = room->con[D_EAST];
    Room *up = room->con[D_UP];
    Room *eastUp = east->con[D_UP];
    if (room->index / 25 > 0)
    {
        p.defaultFloor(clip, 8, 0, east->open[D_DOWN]);
        p.defaultFloor(clip, 0, 0, room->open[D_DOWN]);
    }
    if (up->con[D_UP] == nullptr)
        p.boxOnWater(clip, 1, 8, 1, 7, 8, 6, S_GRAY);
    if (eastUp->con[D_UP] == nullptr)
        p.boxOnWater(clip, 8, 8, 1, 14, 8, 6, S_GRAY);

    for (int y = 1; y <= 7; ++y)
    {
        quint8 s = S_LIGHT;
        if (y == 2 || y == 6)
            s = S_GRAY;
        p.boxFill(clip, 0, y, 0, 0, y, 7, s, s, false);
        p.boxFill(clip, 15, y, 0, 15, y, 7, s, s, false);
        p.boxFill(clip, 1, y, 0, 15, y, 0, s, s, false);
        p.boxFill(clip, 1, y, 7, 14, y, 7, s, s, false);
    }

    p.boxFill(clip, 2, 1, 3, 2, 7, 4, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 3, 1, 2, 4, 7, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 3, 1, 5, 4, 7, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 13, 1, 3, 13, 7, 4, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 11, 1, 2, 12, 7, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 11, 1, 5, 12, 7, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 1, 3, 5, 3, 4, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 10, 1, 3, 10, 3, 4, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 7, 2, 10, 7, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 5, 2, 5, 7, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 10, 5, 2, 10, 7, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 5, 5, 5, 7, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 10, 5, 5, 10, 7, 5, S_LIGHT, S_LIGHT, false);
    p.put(clip, S_LIGHT, 6, 6, 2);
    p.put(clip, S_LIGHT, 9, 6, 2);
    p.put(clip, S_LIGHT, 6, 6, 5);
    p.put(clip, S_LIGHT, 9, 6, 5);
    p.boxFill(clip, 5, 4, 3, 6, 4, 4, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 9, 4, 3, 10, 4, 4, S_LIGHT, S_LIGHT, false);
    p.put(clip, S_LAMP, 5, 4, 2);
    p.put(clip, S_LAMP, 5, 4, 5);
    p.put(clip, S_LAMP, 10, 4, 2);
    p.put(clip, S_LAMP, 10, 4, 5);

    if (room->open[D_SOUTH])
        p.waterBox(clip, 3, 1, 0, 4, 2, 0);
    if (room->open[D_NORTH])
        p.waterBox(clip, 3, 1, 7, 4, 2, 7);
    if (room->open[D_WEST])
        p.waterBox(clip, 0, 1, 3, 0, 2, 4);
    if (east->open[D_SOUTH])
        p.waterBox(clip, 11, 1, 0, 12, 2, 0);
    if (east->open[D_NORTH])
        p.waterBox(clip, 11, 1, 7, 12, 2, 7);
    if (east->open[D_EAST])
        p.waterBox(clip, 15, 1, 3, 15, 2, 4);
    if (up->open[D_SOUTH])
        p.waterBox(clip, 3, 5, 0, 4, 6, 0);
    if (up->open[D_NORTH])
        p.waterBox(clip, 3, 5, 7, 4, 6, 7);
    if (up->open[D_WEST])
        p.waterBox(clip, 0, 5, 3, 0, 6, 4);
    if (eastUp->open[D_SOUTH])
        p.waterBox(clip, 11, 5, 0, 12, 6, 0);
    if (eastUp->open[D_NORTH])
        p.waterBox(clip, 11, 5, 7, 12, 6, 7);
    if (eastUp->open[D_EAST])
        p.waterBox(clip, 15, 5, 3, 15, 6, 4);
}

void roomDoubleY(Piece &p, const Box &clip)
{
    Room *room = p.room;
    if (room->index / 25 > 0)
        p.defaultFloor(clip, 0, 0, room->open[D_DOWN]);
    Room *up = room->con[D_UP];
    if (up->con[D_UP] == nullptr)
        p.boxOnWater(clip, 1, 8, 1, 6, 8, 6, S_GRAY);

    p.boxFill(clip, 0, 4, 0, 0, 4, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 7, 4, 0, 7, 4, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 4, 0, 6, 4, 0, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 4, 7, 6, 4, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 2, 4, 1, 2, 4, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 4, 2, 1, 4, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 4, 1, 5, 4, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, 4, 2, 6, 4, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 2, 4, 5, 2, 4, 6, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 4, 5, 1, 4, 5, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 4, 5, 5, 4, 6, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, 4, 5, 6, 4, 5, S_LIGHT, S_LIGHT, false);

    Room *cur = room;
    for (int y = 1; y <= 5; y += 4)
    {
        if (cur->open[D_SOUTH])
        {
            p.boxFill(clip, 2, y, 0, 2, y + 2, 0, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 5, y, 0, 5, y + 2, 0, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 3, y + 2, 0, 4, y + 2, 0, S_LIGHT, S_LIGHT, false);
        }
        else
        {
            p.boxFill(clip, 0, y, 0, 7, y + 2, 0, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 0, y + 1, 0, 7, y + 1, 0, S_GRAY, S_GRAY, false);
        }

        if (cur->open[D_NORTH])
        {
            p.boxFill(clip, 2, y, 7, 2, y + 2, 7, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 5, y, 7, 5, y + 2, 7, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 3, y + 2, 7, 4, y + 2, 7, S_LIGHT, S_LIGHT, false);
        }
        else
        {
            p.boxFill(clip, 0, y, 7, 7, y + 2, 7, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 0, y + 1, 7, 7, y + 1, 7, S_GRAY, S_GRAY, false);
        }

        if (cur->open[D_WEST])
        {
            p.boxFill(clip, 0, y, 2, 0, y + 2, 2, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 0, y, 5, 0, y + 2, 5, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 0, y + 2, 3, 0, y + 2, 4, S_LIGHT, S_LIGHT, false);
        }
        else
        {
            p.boxFill(clip, 0, y, 0, 0, y + 2, 7, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 0, y + 1, 0, 0, y + 1, 7, S_GRAY, S_GRAY, false);
        }

        if (cur->open[D_EAST])
        {
            p.boxFill(clip, 7, y, 2, 7, y + 2, 2, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 7, y, 5, 7, y + 2, 5, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 7, y + 2, 3, 7, y + 2, 4, S_LIGHT, S_LIGHT, false);
        }
        else
        {
            p.boxFill(clip, 7, y, 0, 7, y + 2, 7, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 7, y + 1, 0, 7, y + 1, 7, S_GRAY, S_GRAY, false);
        }

        cur = up;
    }
}

void roomDoubleYZ(Piece &p, const Box &clip)
{
    Room *room = p.room;
    Room *north = room->con[D_NORTH];
    Room *northUp = north->con[D_UP];
    Room *up = room->con[D_UP];
    if (room->index / 25 > 0)
    {
        p.defaultFloor(clip, 0, 8, north->open[D_DOWN]);
        p.defaultFloor(clip, 0, 0, room->open[D_DOWN]);
    }
    if (up->con[D_UP] == nullptr)
        p.boxOnWater(clip, 1, 8, 1, 6, 8, 7, S_GRAY);
    if (northUp->con[D_UP] == nullptr)
        p.boxOnWater(clip, 1, 8, 8, 6, 8, 14, S_GRAY);

    for (int y = 1; y <= 7; ++y)
    {
        quint8 s = S_LIGHT;
        if (y == 2 || y == 6)
            s = S_GRAY;
        p.boxFill(clip, 0, y, 0, 0, y, 15, s, s, false);
        p.boxFill(clip, 7, y, 0, 7, y, 15, s, s, false);
        p.boxFill(clip, 1, y, 0, 6, y, 0, s, s, false);
        p.boxFill(clip, 1, y, 15, 6, y, 15, s, s, false);
    }

    for (int y = 1; y <= 7; ++y)
    {
        quint8 s = S_BLACK;
        if (y == 2 || y == 6)
            s = S_LAMP;
        p.boxFill(clip, 3, y, 7, 4, y, 8, s, s, false);
    }

    if (room->open[D_SOUTH])
        p.waterBox(clip, 3, 1, 0, 4, 2, 0);
    if (room->open[D_EAST])
        p.waterBox(clip, 7, 1, 3, 7, 2, 4);
    if (room->open[D_WEST])
        p.waterBox(clip, 0, 1, 3, 0, 2, 4);
    if (north->open[D_NORTH])
        p.waterBox(clip, 3, 1, 15, 4, 2, 15);
    if (north->open[D_WEST])
        p.waterBox(clip, 0, 1, 11, 0, 2, 12);
    if (north->open[D_EAST])
        p.waterBox(clip, 7, 1, 11, 7, 2, 12);
    if (up->open[D_SOUTH])
        p.waterBox(clip, 3, 5, 0, 4, 6, 0);
    if (up->open[D_EAST])
    {
        p.waterBox(clip, 7, 5, 3, 7, 6, 4);
        p.boxFill(clip, 5, 4, 2, 6, 4, 5, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 6, 1, 2, 6, 3, 2, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 6, 1, 5, 6, 3, 5, S_LIGHT, S_LIGHT, false);
    }
    if (up->open[D_WEST])
    {
        p.waterBox(clip, 0, 5, 3, 0, 6, 4);
        p.boxFill(clip, 1, 4, 2, 2, 4, 5, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 1, 1, 2, 1, 3, 2, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 1, 1, 5, 1, 3, 5, S_LIGHT, S_LIGHT, false);
    }
    if (northUp->open[D_NORTH])
        p.waterBox(clip, 3, 5, 15, 4, 6, 15);
    if (northUp->open[D_WEST])
    {
        p.waterBox(clip, 0, 5, 11, 0, 6, 12);
        p.boxFill(clip, 1, 4, 10, 2, 4, 13, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 1, 1, 10, 1, 3, 10, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 1, 1, 13, 1, 3, 13, S_LIGHT, S_LIGHT, false);
    }
    if (northUp->open[D_EAST])
    {
        p.waterBox(clip, 7, 5, 11, 7, 6, 12);
        p.boxFill(clip, 5, 4, 10, 6, 4, 13, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 6, 1, 10, 6, 3, 10, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 6, 1, 13, 6, 3, 13, S_LIGHT, S_LIGHT, false);
    }
}

void roomDoubleZ(Piece &p, const Box &clip)
{
    Room *room = p.room;
    Room *north = room->con[D_NORTH];
    if (room->index / 25 > 0)
    {
        p.defaultFloor(clip, 0, 8, north->open[D_DOWN]);
        p.defaultFloor(clip, 0, 0, room->open[D_DOWN]);
    }
    if (room->con[D_UP] == nullptr)
        p.boxOnWater(clip, 1, 4, 1, 6, 4, 7, S_GRAY);
    if (north->con[D_UP] == nullptr)
        p.boxOnWater(clip, 1, 4, 8, 6, 4, 14, S_GRAY);

    p.boxFill(clip, 0, 3, 0, 0, 3, 15, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 7, 3, 0, 7, 3, 15, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 3, 0, 7, 3, 0, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 3, 15, 6, 3, 15, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 0, 2, 0, 0, 2, 15, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 7, 2, 0, 7, 2, 15, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 1, 2, 0, 7, 2, 0, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 1, 2, 15, 6, 2, 15, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 0, 1, 0, 0, 1, 15, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 7, 1, 0, 7, 1, 15, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 1, 0, 7, 1, 0, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 1, 15, 6, 1, 15, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 1, 1, 1, 1, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, 1, 1, 6, 1, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 3, 1, 1, 3, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, 3, 1, 6, 3, 2, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 1, 13, 1, 1, 14, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, 1, 13, 6, 1, 14, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 3, 13, 1, 3, 14, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, 3, 13, 6, 3, 14, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 2, 1, 6, 2, 3, 6, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 1, 6, 5, 3, 6, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 2, 1, 9, 2, 3, 9, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 1, 9, 5, 3, 9, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 3, 2, 6, 4, 2, 6, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 3, 2, 9, 4, 2, 9, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 2, 2, 7, 2, 2, 8, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 5, 2, 7, 5, 2, 8, S_LIGHT, S_LIGHT, false);
    p.put(clip, S_LAMP, 2, 2, 5);
    p.put(clip, S_LAMP, 5, 2, 5);
    p.put(clip, S_LAMP, 2, 2, 10);
    p.put(clip, S_LAMP, 5, 2, 10);
    p.put(clip, S_LIGHT, 2, 3, 5);
    p.put(clip, S_LIGHT, 5, 3, 5);
    p.put(clip, S_LIGHT, 2, 3, 10);
    p.put(clip, S_LIGHT, 5, 3, 10);

    if (room->open[D_SOUTH])
        p.waterBox(clip, 3, 1, 0, 4, 2, 0);
    if (room->open[D_EAST])
        p.waterBox(clip, 7, 1, 3, 7, 2, 4);
    if (room->open[D_WEST])
        p.waterBox(clip, 0, 1, 3, 0, 2, 4);
    if (north->open[D_NORTH])
        p.waterBox(clip, 3, 1, 15, 4, 2, 15);
    if (north->open[D_WEST])
        p.waterBox(clip, 0, 1, 11, 0, 2, 12);
    if (north->open[D_EAST])
        p.waterBox(clip, 7, 1, 11, 7, 2, 12);
}

void roomSimple(Piece &p, const Box &clip, LegacyRng &rng)
{
    Room *room = p.room;
    if (room->index / 25 > 0)
        p.defaultFloor(clip, 0, 0, room->open[D_DOWN]);
    if (room->con[D_UP] == nullptr)
        p.boxOnWater(clip, 1, 4, 1, 6, 4, 6, S_GRAY);

    const bool pillar = p.design != 0 && rng.nextBoolean()
        && !room->open[D_DOWN] && !room->open[D_UP]
        && room->countOpenings() > 1;

    if (p.design == 0)
    {
        p.boxFill(clip, 0, 1, 0, 2, 1, 2, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 0, 3, 0, 2, 3, 2, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 0, 2, 0, 0, 2, 2, S_GRAY, S_GRAY, false);
        p.boxFill(clip, 1, 2, 0, 2, 2, 0, S_GRAY, S_GRAY, false);
        p.put(clip, S_LAMP, 1, 2, 1);
        p.boxFill(clip, 5, 1, 0, 7, 1, 2, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 5, 3, 0, 7, 3, 2, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 7, 2, 0, 7, 2, 2, S_GRAY, S_GRAY, false);
        p.boxFill(clip, 5, 2, 0, 6, 2, 0, S_GRAY, S_GRAY, false);
        p.put(clip, S_LAMP, 6, 2, 1);
        p.boxFill(clip, 0, 1, 5, 2, 1, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 0, 3, 5, 2, 3, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 0, 2, 5, 0, 2, 7, S_GRAY, S_GRAY, false);
        p.boxFill(clip, 1, 2, 7, 2, 2, 7, S_GRAY, S_GRAY, false);
        p.put(clip, S_LAMP, 1, 2, 6);
        p.boxFill(clip, 5, 1, 5, 7, 1, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 5, 3, 5, 7, 3, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 7, 2, 5, 7, 2, 7, S_GRAY, S_GRAY, false);
        p.boxFill(clip, 5, 2, 7, 6, 2, 7, S_GRAY, S_GRAY, false);
        p.put(clip, S_LAMP, 6, 2, 6);

        if (room->open[D_SOUTH])
            p.boxFill(clip, 3, 3, 0, 4, 3, 0, S_LIGHT, S_LIGHT, false);
        else
        {
            p.boxFill(clip, 3, 3, 0, 4, 3, 1, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 3, 2, 0, 4, 2, 0, S_GRAY, S_GRAY, false);
            p.boxFill(clip, 3, 1, 0, 4, 1, 1, S_LIGHT, S_LIGHT, false);
        }
        if (room->open[D_NORTH])
            p.boxFill(clip, 3, 3, 7, 4, 3, 7, S_LIGHT, S_LIGHT, false);
        else
        {
            p.boxFill(clip, 3, 3, 6, 4, 3, 7, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 3, 2, 7, 4, 2, 7, S_GRAY, S_GRAY, false);
            p.boxFill(clip, 3, 1, 6, 4, 1, 7, S_LIGHT, S_LIGHT, false);
        }
        if (room->open[D_WEST])
            p.boxFill(clip, 0, 3, 3, 0, 3, 4, S_LIGHT, S_LIGHT, false);
        else
        {
            p.boxFill(clip, 0, 3, 3, 1, 3, 4, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 0, 2, 3, 0, 2, 4, S_GRAY, S_GRAY, false);
            p.boxFill(clip, 0, 1, 3, 1, 1, 4, S_LIGHT, S_LIGHT, false);
        }
        if (room->open[D_EAST])
            p.boxFill(clip, 7, 3, 3, 7, 3, 4, S_LIGHT, S_LIGHT, false);
        else
        {
            p.boxFill(clip, 6, 3, 3, 7, 3, 4, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 7, 2, 3, 7, 2, 4, S_GRAY, S_GRAY, false);
            p.boxFill(clip, 6, 1, 3, 7, 1, 4, S_LIGHT, S_LIGHT, false);
        }
    }
    else if (p.design == 1)
    {
        p.boxFill(clip, 2, 1, 2, 2, 3, 2, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 2, 1, 5, 2, 3, 5, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 5, 1, 5, 5, 3, 5, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 5, 1, 2, 5, 3, 2, S_LIGHT, S_LIGHT, false);
        p.put(clip, S_LAMP, 2, 2, 2);
        p.put(clip, S_LAMP, 2, 2, 5);
        p.put(clip, S_LAMP, 5, 2, 5);
        p.put(clip, S_LAMP, 5, 2, 2);
        p.boxFill(clip, 0, 1, 0, 1, 3, 0, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 0, 1, 1, 0, 3, 1, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 0, 1, 7, 1, 3, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 0, 1, 6, 0, 3, 6, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 6, 1, 7, 7, 3, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 7, 1, 6, 7, 3, 6, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 6, 1, 0, 7, 3, 0, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 7, 1, 1, 7, 3, 1, S_LIGHT, S_LIGHT, false);
        p.put(clip, S_GRAY, 1, 2, 0);
        p.put(clip, S_GRAY, 0, 2, 1);
        p.put(clip, S_GRAY, 1, 2, 7);
        p.put(clip, S_GRAY, 0, 2, 6);
        p.put(clip, S_GRAY, 6, 2, 7);
        p.put(clip, S_GRAY, 7, 2, 6);
        p.put(clip, S_GRAY, 6, 2, 0);
        p.put(clip, S_GRAY, 7, 2, 1);
        if (!room->open[D_SOUTH])
        {
            p.boxFill(clip, 1, 3, 0, 6, 3, 0, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 1, 2, 0, 6, 2, 0, S_GRAY, S_GRAY, false);
            p.boxFill(clip, 1, 1, 0, 6, 1, 0, S_LIGHT, S_LIGHT, false);
        }
        if (!room->open[D_NORTH])
        {
            p.boxFill(clip, 1, 3, 7, 6, 3, 7, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 1, 2, 7, 6, 2, 7, S_GRAY, S_GRAY, false);
            p.boxFill(clip, 1, 1, 7, 6, 1, 7, S_LIGHT, S_LIGHT, false);
        }
        if (!room->open[D_WEST])
        {
            p.boxFill(clip, 0, 3, 1, 0, 3, 6, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 0, 2, 1, 0, 2, 6, S_GRAY, S_GRAY, false);
            p.boxFill(clip, 0, 1, 1, 0, 1, 6, S_LIGHT, S_LIGHT, false);
        }
        if (!room->open[D_EAST])
        {
            p.boxFill(clip, 7, 3, 1, 7, 3, 6, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, 7, 2, 1, 7, 2, 6, S_GRAY, S_GRAY, false);
            p.boxFill(clip, 7, 1, 1, 7, 1, 6, S_LIGHT, S_LIGHT, false);
        }
    }
    else if (p.design == 2)
    {
        p.boxFill(clip, 0, 1, 0, 0, 1, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 7, 1, 0, 7, 1, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 1, 1, 0, 6, 1, 0, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 1, 1, 7, 6, 1, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 0, 2, 0, 0, 2, 7, S_BLACK, S_BLACK, false);
        p.boxFill(clip, 7, 2, 0, 7, 2, 7, S_BLACK, S_BLACK, false);
        p.boxFill(clip, 1, 2, 0, 6, 2, 0, S_BLACK, S_BLACK, false);
        p.boxFill(clip, 1, 2, 7, 6, 2, 7, S_BLACK, S_BLACK, false);
        p.boxFill(clip, 0, 3, 0, 0, 3, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 7, 3, 0, 7, 3, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 1, 3, 0, 6, 3, 0, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 1, 3, 7, 6, 3, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 0, 1, 3, 0, 2, 4, S_BLACK, S_BLACK, false);
        p.boxFill(clip, 7, 1, 3, 7, 2, 4, S_BLACK, S_BLACK, false);
        p.boxFill(clip, 3, 1, 0, 4, 2, 0, S_BLACK, S_BLACK, false);
        p.boxFill(clip, 3, 1, 7, 4, 2, 7, S_BLACK, S_BLACK, false);
        if (room->open[D_SOUTH])
            p.waterBox(clip, 3, 1, 0, 4, 2, 0);
        if (room->open[D_NORTH])
            p.waterBox(clip, 3, 1, 7, 4, 2, 7);
        if (room->open[D_WEST])
            p.waterBox(clip, 0, 1, 3, 0, 2, 4);
        if (room->open[D_EAST])
            p.waterBox(clip, 7, 1, 3, 7, 2, 4);
    }

    if (pillar)
    {
        p.boxFill(clip, 3, 1, 3, 4, 1, 4, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 3, 2, 3, 4, 2, 4, S_GRAY, S_GRAY, false);
        p.boxFill(clip, 3, 3, 3, 4, 3, 4, S_LIGHT, S_LIGHT, false);
    }
}

void roomSimpleTop(Piece &p, const Box &clip, LegacyRng &rng)
{
    Room *room = p.room;
    if (room->index / 25 > 0)
        p.defaultFloor(clip, 0, 0, room->open[D_DOWN]);
    if (room->con[D_UP] == nullptr)
        p.boxOnWater(clip, 1, 4, 1, 6, 4, 6, S_GRAY);

    for (int x = 1; x <= 6; ++x)
        for (int z = 1; z <= 6; ++z)
        {
            if (rng.nextInt(3) != 0)
            {
                const int y = 2 + (rng.nextInt(4) == 0 ? 0 : 1);
                p.boxFill(clip, x, y, z, x, 3, z, S_SPONGE, S_SPONGE, false);
            }
        }

    p.boxFill(clip, 0, 1, 0, 0, 1, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 7, 1, 0, 7, 1, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 1, 0, 6, 1, 0, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 1, 7, 6, 1, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 0, 2, 0, 0, 2, 7, S_BLACK, S_BLACK, false);
    p.boxFill(clip, 7, 2, 0, 7, 2, 7, S_BLACK, S_BLACK, false);
    p.boxFill(clip, 1, 2, 0, 6, 2, 0, S_BLACK, S_BLACK, false);
    p.boxFill(clip, 1, 2, 7, 6, 2, 7, S_BLACK, S_BLACK, false);
    p.boxFill(clip, 0, 3, 0, 0, 3, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 7, 3, 0, 7, 3, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 3, 0, 6, 3, 0, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 3, 7, 6, 3, 7, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 0, 1, 3, 0, 2, 4, S_BLACK, S_BLACK, false);
    p.boxFill(clip, 7, 1, 3, 7, 2, 4, S_BLACK, S_BLACK, false);
    p.boxFill(clip, 3, 1, 0, 4, 2, 0, S_BLACK, S_BLACK, false);
    p.boxFill(clip, 3, 1, 7, 4, 2, 7, S_BLACK, S_BLACK, false);
    if (room->open[D_SOUTH])
        p.waterBox(clip, 3, 1, 0, 4, 2, 0);
}

void roomWing(Piece &p, const Box &clip)
{
    if (p.design == 0)
    {
        for (int i = 0; i < 4; ++i)
            p.boxFill(clip, 10 - i, 3 - i, 20 - i, 12 + i, 3 - i, 20,
                      S_LIGHT, S_LIGHT, false);

        p.boxFill(clip, 7, 0, 6, 15, 0, 16, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 6, 0, 6, 6, 3, 20, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 16, 0, 6, 16, 3, 20, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 7, 1, 7, 7, 1, 20, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 15, 1, 7, 15, 1, 20, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 7, 1, 6, 9, 3, 6, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 13, 1, 6, 15, 3, 6, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 8, 1, 7, 9, 1, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 13, 1, 7, 14, 1, 7, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 9, 0, 5, 13, 0, 5, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 10, 0, 7, 12, 0, 7, S_BLACK, S_BLACK, false);
        p.boxFill(clip, 8, 0, 10, 8, 0, 12, S_BLACK, S_BLACK, false);
        p.boxFill(clip, 14, 0, 10, 14, 0, 12, S_BLACK, S_BLACK, false);

        for (int z = 18; z >= 7; z -= 3)
        {
            p.put(clip, S_LAMP, 6, 3, z);
            p.put(clip, S_LAMP, 16, 3, z);
        }

        p.put(clip, S_LAMP, 10, 0, 10);
        p.put(clip, S_LAMP, 12, 0, 10);
        p.put(clip, S_LAMP, 10, 0, 12);
        p.put(clip, S_LAMP, 12, 0, 12);
        p.put(clip, S_LAMP, 8, 3, 6);
        p.put(clip, S_LAMP, 14, 3, 6);
        p.put(clip, S_LIGHT, 4, 2, 4);
        p.put(clip, S_LAMP, 4, 1, 4);
        p.put(clip, S_LIGHT, 4, 0, 4);
        p.put(clip, S_LIGHT, 18, 2, 4);
        p.put(clip, S_LAMP, 18, 1, 4);
        p.put(clip, S_LIGHT, 18, 0, 4);
        p.put(clip, S_LIGHT, 4, 2, 18);
        p.put(clip, S_LAMP, 4, 1, 18);
        p.put(clip, S_LIGHT, 4, 0, 18);
        p.put(clip, S_LIGHT, 18, 2, 18);
        p.put(clip, S_LAMP, 18, 1, 18);
        p.put(clip, S_LIGHT, 18, 0, 18);
        p.put(clip, S_LIGHT, 9, 7, 20);
        p.put(clip, S_LIGHT, 13, 7, 20);
        p.boxFill(clip, 6, 0, 21, 7, 4, 21, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 15, 0, 21, 16, 4, 21, S_LIGHT, S_LIGHT, false);
    }
    else if (p.design == 1)
    {
        p.boxFill(clip, 9, 3, 18, 13, 3, 20, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 9, 0, 18, 9, 2, 18, S_LIGHT, S_LIGHT, false);
        p.boxFill(clip, 13, 0, 18, 13, 2, 18, S_LIGHT, S_LIGHT, false);
        int x = 9;
        for (int i = 0; i < 2; ++i)
        {
            p.put(clip, S_LIGHT, x, 6, 20);
            p.put(clip, S_LAMP, x, 5, 20);
            p.put(clip, S_LIGHT, x, 4, 20);
            x = 13;
        }
        p.boxFill(clip, 7, 3, 7, 15, 3, 14, S_LIGHT, S_LIGHT, false);
        int xx = 10;
        for (int i = 0; i < 2; ++i)
        {
            p.boxFill(clip, xx, 0, 10, xx, 6, 10, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, xx, 0, 12, xx, 6, 12, S_LIGHT, S_LIGHT, false);
            p.put(clip, S_LAMP, xx, 0, 10);
            p.put(clip, S_LAMP, xx, 0, 12);
            p.put(clip, S_LAMP, xx, 4, 10);
            p.put(clip, S_LAMP, xx, 4, 12);
            xx = 12;
        }
        int zz = 8;
        for (int i = 0; i < 2; ++i)
        {
            p.boxFill(clip, zz, 0, 7, zz, 2, 7, S_LIGHT, S_LIGHT, false);
            p.boxFill(clip, zz, 0, 14, zz, 2, 14, S_LIGHT, S_LIGHT, false);
            zz = 14;
        }
        p.boxFill(clip, 8, 3, 8, 8, 3, 13, S_BLACK, S_BLACK, false);
        p.boxFill(clip, 14, 3, 8, 14, 3, 13, S_BLACK, S_BLACK, false);
    }
}

void roomPenthouse(Piece &p, const Box &clip)
{
    p.boxFill(clip, 2, -1, 2, 11, -1, 11, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 0, -1, 0, 1, -1, 11, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 12, -1, 0, 13, -1, 11, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 2, -1, 0, 11, -1, 1, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 2, -1, 12, 11, -1, 13, S_GRAY, S_GRAY, false);
    p.boxFill(clip, 0, 0, 0, 0, 0, 13, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 13, 0, 0, 13, 0, 13, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 0, 0, 12, 0, 0, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 1, 0, 13, 12, 0, 13, S_LIGHT, S_LIGHT, false);
    for (int x = 2; x <= 11; x += 3)
    {
        p.put(clip, S_LAMP, 0, 0, x);
        p.put(clip, S_LAMP, 13, 0, x);
        p.put(clip, S_LAMP, x, 0, 0);
    }
    p.boxFill(clip, 2, 0, 3, 4, 0, 9, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 9, 0, 3, 11, 0, 9, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 4, 0, 9, 9, 0, 11, S_LIGHT, S_LIGHT, false);
    p.put(clip, S_LIGHT, 5, 0, 8);
    p.put(clip, S_LIGHT, 8, 0, 8);
    p.put(clip, S_LIGHT, 10, 0, 10);
    p.put(clip, S_LIGHT, 3, 0, 10);
    p.boxFill(clip, 3, 0, 3, 3, 0, 7, S_BLACK, S_BLACK, false);
    p.boxFill(clip, 10, 0, 3, 10, 0, 7, S_BLACK, S_BLACK, false);
    p.boxFill(clip, 6, 0, 10, 7, 0, 10, S_BLACK, S_BLACK, false);
    int x = 3;
    for (int i = 0; i < 2; ++i)
    {
        for (int z = 2; z <= 8; z += 3)
            p.boxFill(clip, x, 0, z, x, 2, z, S_LIGHT, S_LIGHT, false);
        x = 10;
    }
    p.boxFill(clip, 5, 0, 10, 5, 2, 10, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 8, 0, 10, 8, 2, 10, S_LIGHT, S_LIGHT, false);
    p.boxFill(clip, 6, -1, 7, 7, -1, 8, S_BLACK, S_BLACK, false);
    p.waterBox(clip, 6, -1, 3, 7, -1, 4);
}

void pieceProcess(Piece &p, const Box &clip, LegacyRng &rng)
{
    switch (p.kind)
    {
    case K_ENTRY: roomEntry(p, clip); break;
    case K_CORE: roomCore(p, clip); break;
    case K_DOUBLE_X: roomDoubleX(p, clip); break;
    case K_DOUBLE_XY: roomDoubleXY(p, clip); break;
    case K_DOUBLE_Y: roomDoubleY(p, clip); break;
    case K_DOUBLE_YZ: roomDoubleYZ(p, clip); break;
    case K_DOUBLE_Z: roomDoubleZ(p, clip); break;
    case K_SIMPLE: roomSimple(p, clip, rng); break;
    case K_SIMPLE_TOP: roomSimpleTop(p, clip, rng); break;
    case K_WING: roomWing(p, clip); break;
    case K_PENTHOUSE: roomPenthouse(p, clip); break;
    default: break;
    }
}

// ── 主体：外墙、入口、翼楼、屋顶 ────────────────────────────────────────────

void buildingWing(Piece &b, const Box &clip, bool left, int x)
{
    if (!b.chunkHits(clip, x, 0, x + 23, 20))
        return;

    b.boxFill(clip, x + 0, 0, 0, x + 24, 0, 20, S_GRAY, S_GRAY, false);
    b.waterBox(clip, x + 0, 1, 0, x + 24, 10, 20);

    for (int i = 0; i < 4; ++i)
    {
        b.boxFill(clip, x + i, i + 1, i, x + i, i + 1, 20, S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, x + i + 7, i + 5, i + 7, x + i + 7, i + 5, 20,
                  S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, x + 17 - i, i + 5, i + 7, x + 17 - i, i + 5, 20,
                  S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, x + 24 - i, i + 1, i, x + 24 - i, i + 1, 20,
                  S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, x + i + 1, i + 1, i, x + 23 - i, i + 1, i,
                  S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, x + i + 8, i + 5, i + 7, x + 16 - i, i + 5, i + 7,
                  S_LIGHT, S_LIGHT, false);
    }

    b.boxFill(clip, x + 4, 4, 4, x + 6, 4, 20, S_GRAY, S_GRAY, false);
    b.boxFill(clip, x + 7, 4, 4, x + 17, 4, 6, S_GRAY, S_GRAY, false);
    b.boxFill(clip, x + 18, 4, 4, x + 20, 4, 20, S_GRAY, S_GRAY, false);
    b.boxFill(clip, x + 11, 8, 11, x + 13, 8, 20, S_GRAY, S_GRAY, false);
    b.put(clip, S_LIGHT, x + 12, 9, 12);
    b.put(clip, S_LIGHT, x + 12, 9, 15);
    b.put(clip, S_LIGHT, x + 12, 9, 18);

    const int near = x + (left ? 19 : 5);
    const int far = x + (left ? 5 : 19);
    for (int z = 20; z >= 5; z -= 3)
        b.put(clip, S_LIGHT, near, 5, z);
    for (int z = 19; z >= 7; z -= 3)
        b.put(clip, S_LIGHT, far, 5, z);
    for (int i = 0; i < 4; ++i)
    {
        const int xx = left ? x + 24 - (17 - i * 3) : x + 17 - i * 3;
        b.put(clip, S_LIGHT, xx, 5, 5);
    }
    b.put(clip, S_LIGHT, far, 5, 5);

    b.boxFill(clip, x + 11, 1, 12, x + 13, 7, 12, S_GRAY, S_GRAY, false);
    b.boxFill(clip, x + 12, 1, 11, x + 12, 7, 13, S_GRAY, S_GRAY, false);
}

void buildingEntranceArchs(Piece &b, const Box &clip)
{
    if (!b.chunkHits(clip, 22, 5, 35, 17))
        return;
    b.waterBox(clip, 25, 0, 0, 32, 8, 20);
    for (int i = 0; i < 4; ++i)
    {
        const int z = 5 + i * 4;
        b.boxFill(clip, 24, 2, z, 24, 4, z, S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, 22, 4, z, 23, 4, z, S_LIGHT, S_LIGHT, false);
        b.put(clip, S_LIGHT, 25, 5, z);
        b.put(clip, S_LIGHT, 26, 6, z);
        b.put(clip, S_LAMP, 26, 5, z);
        b.boxFill(clip, 33, 2, z, 33, 4, z, S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, 34, 4, z, 35, 4, z, S_LIGHT, S_LIGHT, false);
        b.put(clip, S_LIGHT, 32, 5, z);
        b.put(clip, S_LIGHT, 31, 6, z);
        b.put(clip, S_LAMP, 31, 5, z);
        b.boxFill(clip, 27, 6, z, 30, 6, z, S_GRAY, S_GRAY, false);
    }
}

void buildingEntranceWall(Piece &b, const Box &clip)
{
    if (!b.chunkHits(clip, 15, 20, 42, 21))
        return;
    b.boxFill(clip, 15, 0, 21, 42, 0, 21, S_GRAY, S_GRAY, false);
    b.waterBox(clip, 26, 1, 21, 31, 3, 21);
    b.boxFill(clip, 21, 12, 21, 36, 12, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 17, 11, 21, 40, 11, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 16, 10, 21, 41, 10, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 15, 7, 21, 42, 9, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 16, 6, 21, 41, 6, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 17, 5, 21, 40, 5, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 21, 4, 21, 36, 4, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 22, 3, 21, 26, 3, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 31, 3, 21, 35, 3, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 23, 2, 21, 25, 2, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 32, 2, 21, 34, 2, 21, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 28, 4, 20, 29, 4, 21, S_LIGHT, S_LIGHT, false);
    b.put(clip, S_LIGHT, 27, 3, 21);
    b.put(clip, S_LIGHT, 30, 3, 21);
    b.put(clip, S_LIGHT, 26, 2, 21);
    b.put(clip, S_LIGHT, 31, 2, 21);
    b.put(clip, S_LIGHT, 25, 1, 21);
    b.put(clip, S_LIGHT, 32, 1, 21);

    for (int i = 0; i < 7; ++i)
    {
        b.put(clip, S_BLACK, 28 - i, 6 + i, 21);
        b.put(clip, S_BLACK, 29 + i, 6 + i, 21);
    }
    for (int i = 0; i < 4; ++i)
    {
        b.put(clip, S_BLACK, 28 - i, 9 + i, 21);
        b.put(clip, S_BLACK, 29 + i, 9 + i, 21);
    }
    b.put(clip, S_BLACK, 28, 12, 21);
    b.put(clip, S_BLACK, 29, 12, 21);
    for (int i = 0; i < 3; ++i)
    {
        b.put(clip, S_BLACK, 22 - i * 2, 8, 21);
        b.put(clip, S_BLACK, 22 - i * 2, 9, 21);
        b.put(clip, S_BLACK, 35 + i * 2, 8, 21);
        b.put(clip, S_BLACK, 35 + i * 2, 9, 21);
    }

    b.waterBox(clip, 15, 13, 21, 42, 15, 21);
    b.waterBox(clip, 15, 1, 21, 15, 6, 21);
    b.waterBox(clip, 16, 1, 21, 16, 5, 21);
    b.waterBox(clip, 17, 1, 21, 20, 4, 21);
    b.waterBox(clip, 21, 1, 21, 21, 3, 21);
    b.waterBox(clip, 22, 1, 21, 22, 2, 21);
    b.waterBox(clip, 23, 1, 21, 24, 1, 21);
    b.waterBox(clip, 42, 1, 21, 42, 6, 21);
    b.waterBox(clip, 41, 1, 21, 41, 5, 21);
    b.waterBox(clip, 37, 1, 21, 40, 4, 21);
    b.waterBox(clip, 36, 1, 21, 36, 3, 21);
    b.waterBox(clip, 33, 1, 21, 34, 1, 21);
    b.waterBox(clip, 35, 1, 21, 35, 2, 21);
}

void buildingRoof(Piece &b, const Box &clip)
{
    if (!b.chunkHits(clip, 21, 21, 36, 36))
        return;
    b.boxFill(clip, 21, 0, 22, 36, 0, 36, S_GRAY, S_GRAY, false);
    b.waterBox(clip, 21, 1, 22, 36, 23, 36);

    for (int i = 0; i < 4; ++i)
    {
        b.boxFill(clip, 21 + i, 13 + i, 21 + i, 36 - i, 13 + i, 21 + i,
                  S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, 21 + i, 13 + i, 36 - i, 36 - i, 13 + i, 36 - i,
                  S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, 21 + i, 13 + i, 22 + i, 21 + i, 13 + i, 35 - i,
                  S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, 36 - i, 13 + i, 22 + i, 36 - i, 13 + i, 35 - i,
                  S_LIGHT, S_LIGHT, false);
    }

    b.boxFill(clip, 25, 16, 25, 32, 16, 32, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 25, 17, 25, 25, 19, 25, S_LIGHT, S_LIGHT, false);
    b.boxFill(clip, 32, 17, 25, 32, 19, 25, S_LIGHT, S_LIGHT, false);
    b.boxFill(clip, 25, 17, 32, 25, 19, 32, S_LIGHT, S_LIGHT, false);
    b.boxFill(clip, 32, 17, 32, 32, 19, 32, S_LIGHT, S_LIGHT, false);
    b.put(clip, S_LIGHT, 26, 20, 26);
    b.put(clip, S_LIGHT, 27, 21, 27);
    b.put(clip, S_LAMP, 27, 20, 27);
    b.put(clip, S_LIGHT, 26, 20, 31);
    b.put(clip, S_LIGHT, 27, 21, 30);
    b.put(clip, S_LAMP, 27, 20, 30);
    b.put(clip, S_LIGHT, 31, 20, 31);
    b.put(clip, S_LIGHT, 30, 21, 30);
    b.put(clip, S_LAMP, 30, 20, 30);
    b.put(clip, S_LIGHT, 31, 20, 26);
    b.put(clip, S_LIGHT, 30, 21, 27);
    b.put(clip, S_LAMP, 30, 20, 27);
    b.boxFill(clip, 28, 21, 27, 29, 21, 27, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 27, 21, 28, 27, 21, 29, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 28, 21, 30, 29, 21, 30, S_GRAY, S_GRAY, false);
    b.boxFill(clip, 30, 21, 28, 30, 21, 29, S_GRAY, S_GRAY, false);
}

void buildingLowerWall(Piece &b, const Box &clip)
{
    if (b.chunkHits(clip, 0, 21, 6, 58))
    {
        b.boxFill(clip, 0, 0, 21, 6, 0, 57, S_GRAY, S_GRAY, false);
        b.waterBox(clip, 0, 1, 21, 6, 7, 57);
        b.boxFill(clip, 4, 4, 21, 6, 4, 53, S_GRAY, S_GRAY, false);
        for (int i = 0; i < 4; ++i)
            b.boxFill(clip, i, i + 1, 21, i, i + 1, 57 - i, S_LIGHT, S_LIGHT, false);
        for (int z = 23; z < 53; z += 3)
            b.put(clip, S_LIGHT, 5, 5, z);
        b.put(clip, S_LIGHT, 5, 5, 52);
        for (int i = 0; i < 4; ++i)
            b.boxFill(clip, i, i + 1, 21, i, i + 1, 57 - i, S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, 4, 1, 52, 6, 3, 52, S_GRAY, S_GRAY, false);
        b.boxFill(clip, 5, 1, 51, 5, 3, 53, S_GRAY, S_GRAY, false);
    }

    if (b.chunkHits(clip, 51, 21, 58, 58))
    {
        b.boxFill(clip, 51, 0, 21, 57, 0, 57, S_GRAY, S_GRAY, false);
        b.waterBox(clip, 51, 1, 21, 57, 7, 57);
        b.boxFill(clip, 51, 4, 21, 53, 4, 53, S_GRAY, S_GRAY, false);
        for (int i = 0; i < 4; ++i)
            b.boxFill(clip, 57 - i, i + 1, 21, 57 - i, i + 1, 57 - i,
                      S_LIGHT, S_LIGHT, false);
        for (int z = 23; z < 53; z += 3)
            b.put(clip, S_LIGHT, 52, 5, z);
        b.put(clip, S_LIGHT, 52, 5, 52);
        b.boxFill(clip, 51, 1, 52, 53, 3, 52, S_GRAY, S_GRAY, false);
        b.boxFill(clip, 52, 1, 51, 52, 3, 53, S_GRAY, S_GRAY, false);
    }

    if (b.chunkHits(clip, 0, 51, 57, 57))
    {
        b.boxFill(clip, 7, 0, 51, 50, 0, 57, S_GRAY, S_GRAY, false);
        b.waterBox(clip, 7, 1, 51, 50, 10, 57);
        for (int i = 0; i < 4; ++i)
            b.boxFill(clip, i + 1, i + 1, 57 - i, 56 - i, i + 1, 57 - i,
                      S_LIGHT, S_LIGHT, false);
    }
}

void buildingMiddleWall(Piece &b, const Box &clip)
{
    if (b.chunkHits(clip, 7, 21, 13, 50))
    {
        b.boxFill(clip, 7, 0, 21, 13, 0, 50, S_GRAY, S_GRAY, false);
        b.waterBox(clip, 7, 1, 21, 13, 10, 50);
        b.boxFill(clip, 11, 8, 21, 13, 8, 53, S_GRAY, S_GRAY, false);
        for (int i = 0; i < 4; ++i)
            b.boxFill(clip, i + 7, i + 5, 21, i + 7, i + 5, 54, S_LIGHT, S_LIGHT, false);
        for (int z = 21; z <= 45; z += 3)
            b.put(clip, S_LIGHT, 12, 9, z);
    }

    if (b.chunkHits(clip, 44, 21, 50, 54))
    {
        b.boxFill(clip, 44, 0, 21, 50, 0, 50, S_GRAY, S_GRAY, false);
        b.waterBox(clip, 44, 1, 21, 50, 10, 50);
        b.boxFill(clip, 44, 8, 21, 46, 8, 53, S_GRAY, S_GRAY, false);
        for (int i = 0; i < 4; ++i)
            b.boxFill(clip, 50 - i, i + 5, 21, 50 - i, i + 5, 54, S_LIGHT, S_LIGHT, false);
        for (int z = 21; z <= 45; z += 3)
            b.put(clip, S_LIGHT, 45, 9, z);
    }

    if (b.chunkHits(clip, 8, 44, 49, 54))
    {
        b.boxFill(clip, 14, 0, 44, 43, 0, 50, S_GRAY, S_GRAY, false);
        b.waterBox(clip, 14, 1, 44, 43, 10, 50);
        for (int x = 12; x <= 45; x += 3)
        {
            b.put(clip, S_LIGHT, x, 9, 45);
            b.put(clip, S_LIGHT, x, 9, 52);
            if (x == 12 || x == 18 || x == 24 || x == 33 || x == 39 || x == 45)
            {
                b.put(clip, S_LIGHT, x, 9, 47);
                b.put(clip, S_LIGHT, x, 9, 50);
                b.put(clip, S_LIGHT, x, 10, 45);
                b.put(clip, S_LIGHT, x, 10, 46);
                b.put(clip, S_LIGHT, x, 10, 51);
                b.put(clip, S_LIGHT, x, 10, 52);
                b.put(clip, S_LIGHT, x, 11, 47);
                b.put(clip, S_LIGHT, x, 11, 50);
                b.put(clip, S_LIGHT, x, 12, 48);
                b.put(clip, S_LIGHT, x, 12, 49);
            }
        }
        for (int i = 0; i < 3; ++i)
            b.boxFill(clip, 8 + i, 5 + i, 54, 49 - i, 5 + i, 54, S_GRAY, S_GRAY, false);
        b.boxFill(clip, 11, 8, 54, 46, 8, 54, S_LIGHT, S_LIGHT, false);
        b.boxFill(clip, 14, 8, 44, 43, 8, 53, S_GRAY, S_GRAY, false);
    }
}

void buildingUpperWall(Piece &b, const Box &clip)
{
    if (b.chunkHits(clip, 14, 21, 20, 43))
    {
        b.boxFill(clip, 14, 0, 21, 20, 0, 43, S_GRAY, S_GRAY, false);
        b.waterBox(clip, 14, 1, 22, 20, 14, 43);
        b.boxFill(clip, 18, 12, 22, 20, 12, 39, S_GRAY, S_GRAY, false);
        b.boxFill(clip, 18, 12, 21, 20, 12, 21, S_LIGHT, S_LIGHT, false);
        for (int i = 0; i < 4; ++i)
            b.boxFill(clip, i + 14, i + 9, 21, i + 14, i + 9, 43 - i,
                      S_LIGHT, S_LIGHT, false);
        for (int z = 23; z <= 39; z += 3)
            b.put(clip, S_LIGHT, 19, 13, z);
    }

    if (b.chunkHits(clip, 37, 21, 43, 43))
    {
        b.boxFill(clip, 37, 0, 21, 43, 0, 43, S_GRAY, S_GRAY, false);
        b.waterBox(clip, 37, 1, 22, 43, 14, 43);
        b.boxFill(clip, 37, 12, 22, 39, 12, 39, S_GRAY, S_GRAY, false);
        b.boxFill(clip, 37, 12, 21, 39, 12, 21, S_LIGHT, S_LIGHT, false);
        for (int i = 0; i < 4; ++i)
            b.boxFill(clip, 43 - i, i + 9, 21, 43 - i, i + 9, 43 - i,
                      S_LIGHT, S_LIGHT, false);
        for (int z = 23; z <= 39; z += 3)
            b.put(clip, S_LIGHT, 38, 13, z);
    }

    if (b.chunkHits(clip, 15, 37, 42, 43))
    {
        b.boxFill(clip, 21, 0, 37, 36, 0, 43, S_GRAY, S_GRAY, false);
        b.waterBox(clip, 21, 1, 37, 36, 14, 43);
        b.boxFill(clip, 21, 12, 37, 36, 12, 39, S_GRAY, S_GRAY, false);
        for (int i = 0; i < 4; ++i)
            b.boxFill(clip, 15 + i, i + 9, 43 - i, 42 - i, i + 9, 43 - i,
                      S_LIGHT, S_LIGHT, false);
        for (int x = 21; x <= 36; x += 3)
            b.put(clip, S_LIGHT, x, 13, 38);
    }
}

/** 生成整座海底神殿到 grid */
void generate(quint64 seed, qint64 blockX, qint64 blockZ, Grid &grid)
{
    const int chunkX = int(blockX >> 4);
    const int chunkZ = int(blockZ >> 4);

    LegacyRng rng;
    rng.setLargeFeatureSeed(qint64(seed), chunkX, chunkZ);

    // Direction.Plane.HORIZONTAL = {NORTH, EAST, SOUTH, WEST}
    static const int kHorizontal[4] = {D_NORTH, D_EAST, D_SOUTH, D_WEST};
    const int dir = kHorizontal[rng.nextInt(4)];

    QVector<Room *> owned;
    auto newRoom = [&owned](int index) {
        Room *r = new Room(index);
        owned.append(r);
        return r;
    };

    Room *grid9[75] = {};
    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 4; ++z)
            grid9[roomIndex(x, 0, z)] = newRoom(roomIndex(x, 0, z));
    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 4; ++z)
            grid9[roomIndex(x, 1, z)] = newRoom(roomIndex(x, 1, z));
    for (int x = 1; x < 4; ++x)
        for (int z = 0; z < 2; ++z)
            grid9[roomIndex(x, 2, z)] = newRoom(roomIndex(x, 2, z));

    Room *source = grid9[roomIndex(2, 0, 0)];
    for (int x = 0; x < 5; ++x)
        for (int y = 0; y < 3; ++y)
            for (int z = 0; z < 5; ++z)
            {
                Room *r = grid9[roomIndex(x, y, z)];
                if (!r)
                    continue;
                for (int d = 0; d < 6; ++d)
                {
                    const int nx = x + dirDx(d), ny = y + dirDy(d), nz = z + dirDz(d);
                    if (nx < 0 || nx >= 5 || nz < 0 || nz >= 5 || ny < 0 || ny >= 3)
                        continue;
                    Room *n = grid9[roomIndex(nx, ny, nz)];
                    if (!n)
                        continue;
                    if (nz == z)
                        r->setConnection(d, n);
                    else
                        r->setConnection(dirOpp(d), n);
                }
            }

    Room *penthouse = newRoom(1003);
    Room *leftWing = newRoom(1001);
    Room *rightWing = newRoom(1002);
    grid9[roomIndex(2, 2, 0)]->setConnection(D_UP, penthouse);
    grid9[roomIndex(0, 1, 0)]->setConnection(D_SOUTH, leftWing);
    grid9[roomIndex(4, 1, 0)]->setConnection(D_SOUTH, rightWing);
    penthouse->claimed = true;
    leftWing->claimed = true;
    rightWing->claimed = true;
    source->isSource = true;

    Room *core = grid9[roomIndex(rng.nextInt(4), 0, 2)];
    core->claimed = true;
    core->con[D_EAST]->claimed = true;
    core->con[D_NORTH]->claimed = true;
    core->con[D_EAST]->con[D_NORTH]->claimed = true;
    core->con[D_UP]->claimed = true;
    core->con[D_EAST]->con[D_UP]->claimed = true;
    core->con[D_NORTH]->con[D_UP]->claimed = true;
    core->con[D_EAST]->con[D_NORTH]->con[D_UP]->claimed = true;

    QVector<Room *> list;
    for (int i = 0; i < 75; ++i)
    {
        if (!grid9[i])
            continue;
        grid9[i]->updateOpenings();
        list.append(grid9[i]);
    }
    penthouse->updateOpenings();

    for (int i = list.size(); i > 1; --i)
    {
        const int j = rng.nextInt(i);
        std::swap(list[i - 1], list[j]);
    }

    int sourceId = 1;
    for (Room *r : list)
    {
        int done = 0;
        int tries = 0;
        while (done < 2 && tries < 5)
        {
            ++tries;
            const int d = rng.nextInt(6);
            if (!r->open[d])
                continue;
            const int od = dirOpp(d);
            r->open[d] = false;
            r->con[d]->open[od] = false;
            const bool ok1 = r->findSource(sourceId++);
            bool ok2 = false;
            if (ok1)
                ok2 = r->con[d]->findSource(sourceId++);
            if (ok1 && ok2)
                ++done;
            else
            {
                r->open[d] = true;
                r->con[d]->open[od] = true;
            }
        }
    }

    list.append(penthouse);
    list.append(leftWing);
    list.append(rightWing);

    // ── 组装房间段 ──
    QVector<Piece> children;

    Piece building;
    building.kind = -1;
    building.orient = dir;
    building.box = Box::make(0, 0, 0, dir, 58, 23, 58);

    auto addPiece = [&](int kind, Room *room, int sx, int sy, int sz) {
        Piece p;
        p.kind = kind;
        p.orient = dir;
        p.room = room;
        p.design = (kind == K_SIMPLE) ? rng.nextInt(3) : 0;
        p.box = Box::make(0, 0, 0, 0, 0, 0, 0); // 占位，下面按房间计算
        const int idx = room->index;
        const int rx = idx % 5;
        const int rz = (idx / 5) % 5;
        const int ry = idx / 25;
        Box b = Box::make(0, 0, 0, dir, sx * 8, sy * 4, sz * 8);
        switch (dir)
        {
        case D_NORTH: b.move(rx * 8, ry * 4, -(rz + sz) * 8 + 1); break;
        case D_SOUTH: b.move(rx * 8, ry * 4, rz * 8); break;
        case D_WEST: b.move(-(rz + sz) * 8 + 1, ry * 4, rx * 8); break;
        default: b.move(rz * 8, ry * 4, rx * 8); break;
        }
        p.box = b;
        children.append(p);
    };

    addPiece(K_ENTRY, source, 1, 1, 1);
    addPiece(K_CORE, core, 2, 2, 2);

    for (Room *r : list)
    {
        if (r->claimed || r->index >= 75)
            continue;

        // FitDoubleXYRoom
        if (r->open[D_EAST] && !r->con[D_EAST]->claimed
            && r->open[D_UP] && !r->con[D_UP]->claimed)
        {
            Room *east = r->con[D_EAST];
            if (east->open[D_UP] && !east->con[D_UP]->claimed)
            {
                r->claimed = true;
                east->claimed = true;
                r->con[D_UP]->claimed = true;
                east->con[D_UP]->claimed = true;
                addPiece(K_DOUBLE_XY, r, 2, 2, 1);
                continue;
            }
        }
        // FitDoubleYZRoom
        if (r->open[D_NORTH] && !r->con[D_NORTH]->claimed
            && r->open[D_UP] && !r->con[D_UP]->claimed)
        {
            Room *north = r->con[D_NORTH];
            if (north->open[D_UP] && !north->con[D_UP]->claimed)
            {
                r->claimed = true;
                north->claimed = true;
                r->con[D_UP]->claimed = true;
                north->con[D_UP]->claimed = true;
                addPiece(K_DOUBLE_YZ, r, 1, 2, 2);
                continue;
            }
        }
        // FitDoubleZRoom
        if (r->open[D_NORTH] && !r->con[D_NORTH]->claimed)
        {
            Room *zr = r;
            if (!r->open[D_NORTH] || r->con[D_NORTH]->claimed)
                zr = r->con[D_SOUTH];
            zr->claimed = true;
            zr->con[D_NORTH]->claimed = true;
            addPiece(K_DOUBLE_Z, zr, 1, 1, 2);
            continue;
        }
        // FitDoubleXRoom
        if (r->open[D_EAST] && !r->con[D_EAST]->claimed)
        {
            r->claimed = true;
            r->con[D_EAST]->claimed = true;
            addPiece(K_DOUBLE_X, r, 2, 1, 1);
            continue;
        }
        // FitDoubleYRoom
        if (r->open[D_UP] && !r->con[D_UP]->claimed)
        {
            r->claimed = true;
            r->con[D_UP]->claimed = true;
            addPiece(K_DOUBLE_Y, r, 1, 2, 1);
            continue;
        }
        // FitSimpleTopRoom
        if (!r->open[D_WEST] && !r->open[D_EAST] && !r->open[D_NORTH]
            && !r->open[D_SOUTH] && !r->open[D_UP])
        {
            r->claimed = true;
            addPiece(K_SIMPLE_TOP, r, 1, 1, 1);
            continue;
        }
        // FitSimpleRoom
        r->claimed = true;
        addPiece(K_SIMPLE, r, 1, 1, 1);
    }

    // 房间段整体平移到主体坐标（原版 b(9, 0, 22)）
    const int offX = building.px(9, 22);
    const int offY = (dir < 0) ? 0 : building.box.y0 + 0;
    const int offZ = building.pz(9, 22);
    for (Piece &p : children)
        p.box.move(offX, offY, offZ);

    // 两座翼楼 + 顶层：原版只抽一次随机数，左右两座翼楼取相反的设计号
    //（一座带高塔、一座是平台款式），具体哪座配哪款由这次随机数决定
    int wingDesign = 0;
    {
        Piece w;
        w.kind = K_WING;
        w.orient = dir;
        const int ax = building.px(1, 1), az = building.pz(1, 1);
        const int bx = building.px(23, 21), bz = building.pz(23, 21);
        w.box = Box::corners(ax, building.box.y0 + 1, az, bx, building.box.y0 + 8, bz);
        wingDesign = rng.next(32);
        w.design = (wingDesign + 1) & 1;
        children.append(w);
    }
    {
        Piece w;
        w.kind = K_WING;
        w.orient = dir;
        const int ax = building.px(34, 1), az = building.pz(34, 1);
        const int bx = building.px(56, 21), bz = building.pz(56, 21);
        w.box = Box::corners(ax, building.box.y0 + 1, az, bx, building.box.y0 + 8, bz);
        w.design = wingDesign & 1;
        children.append(w);
    }
    {
        Piece w;
        w.kind = K_PENTHOUSE;
        w.orient = dir;
        const int ax = building.px(22, 22), az = building.pz(22, 22);
        const int bx = building.px(35, 35), bz = building.pz(35, 35);
        w.box = Box::corners(ax, building.box.y0 + 13, az, bx,
                             building.box.y0 + 17, bz);
        children.append(w);
    }

    // ── 摆方块 ──
    const Box clip{-40, -40, -40, 120, 60, 120};
    building.grid = &grid;
    building.baseY = 39;
    for (Piece &p : children)
    {
        p.grid = &grid;
        p.baseY = 39;
    }

    LegacyRng detail;
    detail.setLargeFeatureSeed(qint64(seed) ^ 0x5DEECE66DLL, chunkX, chunkZ);

    const int fillTop = std::max(grid.seaLevel, 64) - (39 + building.box.y0);
    building.waterBox(clip, 0, 0, 0, 58, fillTop, 58);
    buildingWing(building, clip, false, 0);
    buildingWing(building, clip, true, 33);
    buildingEntranceArchs(building, clip);
    buildingEntranceWall(building, clip);
    buildingRoof(building, clip);
    buildingLowerWall(building, clip);
    buildingMiddleWall(building, clip);
    buildingUpperWall(building, clip);

    for (int i = 0; i < 7; ++i)
    {
        int j = 0;
        while (j < 7)
        {
            if (j == 0 && i == 3)
                j = 6;
            const int bx = i * 9;
            const int bz = j * 9;
            for (int m = 0; m < 4; ++m)
                for (int n = 0; n < 4; ++n)
                {
                    building.put(clip, S_LIGHT, bx + m, 0, bz + n);
                    building.columnDown(clip, S_LIGHT, bx + m, -1, bz + n);
                }
            if (i != 0 && i != 6)
                j += 6;
            else
                ++j;
        }
    }

    for (int i = 0; i < 5; ++i)
    {
        building.waterBox(clip, -1 - i, 0 + i * 2, -1 - i, -1 - i, 23, 58 + i);
        building.waterBox(clip, 58 + i, 0 + i * 2, -1 - i, 58 + i, 23, 58 + i);
        building.waterBox(clip, 0 - i, 0 + i * 2, -1 - i, 57 + i, 23, -1 - i);
        building.waterBox(clip, 0 - i, 0 + i * 2, 58 + i, 57 + i, 23, 58 + i);
    }

    for (Piece &p : children)
        if (p.box.hits(clip))
            pieceProcess(p, clip, detail);

    qDeleteAll(owned);
}

} // namespace monument

/** 由原版算法生成的海底神殿导出到画布（世界坐标平移回神殿局部坐标） */
bool buildMonumentFromTemplate(Canvas &c, quint64 seed, qint64 x, qint64 z,
                               StructureModelInfo *out)
{
    monument::Grid grid;
    monument::generate(seed, x, z, grid);
    if (grid.cells.isEmpty())
        return false;

    for (auto it = grid.cells.constBegin(); it != grid.cells.constEnd(); ++it)
    {
        int wx = 0, wy = 0, wz = 0;
        monument::Grid::unkey(it.key(), wx, wy, wz);
        const quint8 s = it.value();
        if (s == monument::S_AIR || s == monument::S_WATER)
            continue;
        c.put(wx, wy - 39, wz, monument::monumentBlockName(s));
    }

    out->variant = QStringLiteral("海底神殿");
    out->fromVanillaTemplate = true;
    out->rotation = 0;
    return true;
}


/** 拼图用的轴对齐包围盒（闭区间） */
struct ExactBox
{
    int x0 = 0, y0 = 0, z0 = 0;
    int x1 = -1, y1 = -1, z1 = -1;

    bool valid() const { return x1 >= x0 && y1 >= y0 && z1 >= z0; }
    int height() const { return y1 - y0 + 1; }
};

inline bool boxInside(const ExactBox &outer, const ExactBox &inner)
{
    return outer.x0 <= inner.x0 && outer.y0 <= inner.y0 && outer.z0 <= inner.z0
        && inner.x1 <= outer.x1 && inner.y1 <= outer.y1 && inner.z1 <= outer.z1;
}

inline bool boxOverlap(const ExactBox &a, const ExactBox &b)
{
    return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1
        && a.z0 <= b.z1 && b.z0 <= a.z1;
}

/** StructureTemplate.transform（pivot 为原点，仅水平旋转） */
inline void exactTransform(int x, int y, int z, int turns, int &ox, int &oz)
{
    Q_UNUSED(y);
    switch (((turns % 4) + 4) % 4)
    {
    case 1: ox = -z; oz = x; break;
    case 2: ox = -x; oz = -z; break;
    case 3: ox = z; oz = -x; break;
    default: ox = x; oz = z; break;
    }
}

/** 水平方向名旋转 90°×turns */
QString rotateDirName(const QString &dir, int turns)
{
    static const char *const names[] = {"north", "east", "south", "west"};
    for (int i = 0; i < 4; ++i)
        if (dir == QLatin1String(names[i]))
            return QLatin1String(names[(i + turns) % 4]);
    return dir; // up / down 不变
}

/** 拼图方块朝向（front_top）随旋转变化 */
QString rotateOrientation(const QString &orientation, int turns)
{
    turns = ((turns % 4) + 4) % 4;
    if (turns == 0 || orientation.isEmpty())
        return orientation;
    const int underscore = orientation.indexOf(QLatin1Char('_'));
    if (underscore < 0)
        return orientation;
    return rotateDirName(orientation.left(underscore), turns)
        + QLatin1Char('_')
        + rotateDirName(orientation.mid(underscore + 1), turns);
}

/** 一个拼图点（世界坐标 + 旋转后的朝向） */
struct ExactJigsaw
{
    int x = 0, y = 0, z = 0;
    QString front, top;
    QString name, target, pool, joint, finalState;
    int placementPriority = 0;
    int selectionPriority = 0;
};

/** 模板池里的一个元素（list 元素展开成子元素） */
struct ExactElement
{
    QString loc;                                  ///< 模板 id（去掉 minecraft:）
    QString projection = QStringLiteral("rigid"); ///< rigid / terrain_matching
    bool feature = false;                         ///< 特征元素（村庄的树）
    bool empty = false;                           ///< 空元素
    QVector<ExactElement> children;               ///< list 元素的子项
};

/** 一个模板池（weight 已展开成重复项，与原版一致） */
struct ExactPool
{
    QVector<ExactElement> templates;
    QString fallback;
    int maxY = -1;
    bool loaded = false;
};

QString exactPoolPath(const QString &pool)
{
    QString id = pool;
    if (id.startsWith(QLatin1String("minecraft:")))
        id.remove(0, 10);
    return QStringLiteral(":/utils/WorldMap/worldgen/template_pool/") + id
        + QStringLiteral(".json");
}

ExactElement parseExactElement(const QJsonObject &obj)
{
    ExactElement element;
    const QString type = obj.value(QStringLiteral("element_type")).toString();
    element.projection = obj.value(QStringLiteral("projection")).toString(
        QStringLiteral("rigid"));
    if (type.endsWith(QLatin1String("empty_pool_element")))
    {
        element.empty = true;
        return element;
    }
    if (type.endsWith(QLatin1String("feature_pool_element")))
    {
        element.feature = true;
        return element;
    }
    if (type.endsWith(QLatin1String("list_pool_element")))
    {
        const QJsonArray kids = obj.value(QStringLiteral("elements")).toArray();
        for (const QJsonValue &value : kids)
            element.children.append(parseExactElement(value.toObject()));
        return element;
    }
    QString loc = obj.value(QStringLiteral("location")).toString();
    if (loc.startsWith(QLatin1String("minecraft:")))
        loc.remove(0, 10);
    element.loc = loc;
    return element;
}

/** 读取模板池（按 weight 展开；结果缓存） */
const ExactPool &loadExactPool(const QString &pool)
{
    static QMap<QString, ExactPool> cache;
    QString id = pool;
    if (id.startsWith(QLatin1String("minecraft:")))
        id.remove(0, 10);

    // 试炼密室的 spawner 内容池在原版里由 pool_alias 映射（ranged / melee 等），
    // 没有对应的 JSON；映射到同类真实模板池，保证拼图随机序列与占格一致。
    static const QHash<QString, QString> trialAliases = {
        {QStringLiteral("trial_chambers/spawner/contents/ranged"),
         QStringLiteral("trial_chambers/spawner/ranged/skeleton")},
        {QStringLiteral("trial_chambers/spawner/contents/slow_ranged"),
         QStringLiteral("trial_chambers/spawner/slow_ranged/skeleton")},
        {QStringLiteral("trial_chambers/spawner/contents/melee"),
         QStringLiteral("trial_chambers/spawner/melee/zombie")},
        {QStringLiteral("trial_chambers/spawner/contents/small_melee"),
         QStringLiteral("trial_chambers/spawner/small_melee/slime")},
    };
    id = trialAliases.value(id, id);

    const auto cached = cache.constFind(id);
    if (cached != cache.constEnd())
        return cached.value();

    ExactPool loaded;
    loaded.loaded = true;
    loaded.fallback = QStringLiteral("empty");
    if (id != QLatin1String("empty"))
    {
        QFile file(exactPoolPath(id));
        if (file.open(QIODevice::ReadOnly))
        {
            const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
            const QJsonArray elements = root.value(QStringLiteral("elements")).toArray();
            for (const QJsonValue &value : elements)
            {
                const QJsonObject entry = value.toObject();
                const ExactElement element = parseExactElement(
                    entry.value(QStringLiteral("element")).toObject());
                const int weight = qMax(1, entry.value(QStringLiteral("weight")).toInt(1));
                for (int i = 0; i < weight; ++i)
                    loaded.templates.append(element);
            }
            QString fallback = root.value(QStringLiteral("fallback")).toString();
            if (fallback.startsWith(QLatin1String("minecraft:")))
                fallback.remove(0, 10);
            if (!fallback.isEmpty())
                loaded.fallback = fallback;
        }
    }
    // QMap 为节点存储，返回的引用在后续插入时仍然有效
    return *cache.insert(id, loaded);
}

/** 元素在世界里的包围盒 */
ExactBox exactElementBox(const ExactElement &element, int ox, int oy, int oz, int turns)
{
    if (element.feature)
    {
        ExactBox box;
        box.x0 = box.x1 = ox;
        box.y0 = box.y1 = oy;
        box.z0 = box.z1 = oz;
        return box;
    }
    if (!element.children.isEmpty())
    {
        ExactBox merged;
        for (const ExactElement &child : element.children)
        {
            const ExactBox box = exactElementBox(child, ox, oy, oz, turns);
            if (!box.valid())
                continue;
            if (!merged.valid())
            {
                merged = box;
                continue;
            }
            merged.x0 = qMin(merged.x0, box.x0);
            merged.y0 = qMin(merged.y0, box.y0);
            merged.z0 = qMin(merged.z0, box.z0);
            merged.x1 = qMax(merged.x1, box.x1);
            merged.y1 = qMax(merged.y1, box.y1);
            merged.z1 = qMax(merged.z1, box.z1);
        }
        return merged;
    }

    const auto tpl = StructureTemplate::load(element.loc);
    if (!tpl)
        return ExactBox();

    int ax = 0, az = 0, bx = 0, bz = 0;
    exactTransform(0, 0, 0, turns, ax, az);
    exactTransform(tpl->sizeX() - 1, tpl->sizeY() - 1, tpl->sizeZ() - 1, turns, bx, bz);
    ExactBox box;
    box.x0 = ox + qMin(ax, bx);
    box.x1 = ox + qMax(ax, bx);
    box.y0 = oy;
    box.y1 = oy + tpl->sizeY() - 1;
    box.z0 = oz + qMin(az, bz);
    box.z1 = oz + qMax(az, bz);
    return box;
}

/** 模板池里最高模板的 Y 跨度（拼图扩展用的「预留空间」） */
int exactPoolMaxY(const QString &pool)
{
    ExactPool &p = const_cast<ExactPool &>(loadExactPool(pool));
    if (p.maxY >= 0)
        return p.maxY;
    int best = 0;
    for (const ExactElement &element : p.templates)
    {
        if (element.empty)
            continue;
        if (element.feature)
        {
            best = qMax(best, 1);
            continue;
        }
        if (!element.children.isEmpty())
        {
            for (const ExactElement &child : element.children)
                if (!child.loc.isEmpty())
                {
                    const ExactBox box = exactElementBox(child, 0, 0, 0, 0);
                    if (box.valid())
                        best = qMax(best, box.height());
                }
            continue;
        }
        const ExactBox box = exactElementBox(element, 0, 0, 0, 0);
        if (box.valid())
            best = qMax(best, box.height());
    }
    p.maxY = best;
    return best;
}

/** 元素的所有拼图点：变换位置/朝向 → 洗牌 → 按 selection_priority 稳定排序 */
QVector<ExactJigsaw> exactElementJigsaws(const ExactElement &element,
                                         int ox, int oy, int oz, int turns,
                                         LegacyRng &rng)
{
    QVector<ExactJigsaw> list;

    if (element.feature)
    {
        // FeaturePoolElement 自带的合成拼图点：朝下、名称 bottom
        ExactJigsaw jigsaw;
        jigsaw.x = ox;
        jigsaw.y = oy;
        jigsaw.z = oz;
        jigsaw.front = QStringLiteral("down");
        jigsaw.top = QStringLiteral("south");
        jigsaw.name = QStringLiteral("minecraft:bottom");
        jigsaw.target = QStringLiteral("minecraft:empty");
        jigsaw.pool = QStringLiteral("minecraft:empty");
        jigsaw.joint = QStringLiteral("rollable");
        jigsaw.finalState = QStringLiteral("minecraft:air");
        return QVector<ExactJigsaw>{jigsaw};
    }

    if (!element.children.isEmpty())
    {
        for (const ExactElement &child : element.children)
            list += exactElementJigsaws(child, ox, oy, oz, turns, rng);
    }
    else
    {
        const auto tpl = StructureTemplate::load(element.loc);
        if (!tpl)
            return list;
        for (const StructureTemplateJigsaw &jigsaw : tpl->jigsaws())
        {
            int rx = 0, rz = 0;
            exactTransform(jigsaw.x, jigsaw.y, jigsaw.z, turns, rx, rz);
            ExactJigsaw out;
            out.x = ox + rx;
            out.y = oy + jigsaw.y;
            out.z = oz + rz;
            const QString orientation = rotateOrientation(jigsaw.orientation, turns);
            const int underscore = orientation.indexOf(QLatin1Char('_'));
            out.front = underscore < 0 ? orientation : orientation.left(underscore);
            out.top = underscore < 0 ? QString() : orientation.mid(underscore + 1);
            out.name = jigsaw.name;
            out.target = jigsaw.target;
            out.pool = jigsaw.pool;
            out.joint = jigsaw.joint.isEmpty() ? QStringLiteral("aligned")
                                               : jigsaw.joint;
            out.finalState = jigsaw.finalState;
            out.placementPriority = jigsaw.placementPriority;
            out.selectionPriority = jigsaw.selectionPriority;
            list.append(out);
        }
    }

    // Util.shuffledCopy：Collections.shuffle 的 Fisher-Yates
    for (int i = list.size(); i > 1; --i)
        list.swapItemsAt(i - 1, rng.nextInt(i));
    std::stable_sort(list.begin(), list.end(),
                     [](const ExactJigsaw &a, const ExactJigsaw &b) {
                         return a.selectionPriority > b.selectionPriority;
                     });
    return list;
}

/** JigsawBlock.canAttach */
bool canAttach(const ExactJigsaw &parent, const ExactJigsaw &child)
{
    static const QHash<QString, QString> opposite = {
        {QStringLiteral("north"), QStringLiteral("south")},
        {QStringLiteral("south"), QStringLiteral("north")},
        {QStringLiteral("east"), QStringLiteral("west")},
        {QStringLiteral("west"), QStringLiteral("east")},
        {QStringLiteral("up"), QStringLiteral("down")},
        {QStringLiteral("down"), QStringLiteral("up")},
    };
    if (opposite.value(child.front) != parent.front)
        return false;
    if (parent.joint != QLatin1String("rollable") && parent.top != child.top)
        return false;
    return parent.target == child.name;
}

void facingOffset(const QString &front, int &dx, int &dy, int &dz)
{
    dx = dy = dz = 0;
    if (front == QLatin1String("north")) dz = -1;
    else if (front == QLatin1String("south")) dz = 1;
    else if (front == QLatin1String("east")) dx = 1;
    else if (front == QLatin1String("west")) dx = -1;
    else if (front == QLatin1String("up")) dy = 1;
    else if (front == QLatin1String("down")) dy = -1;
}

/** 村庄风格的显示名 */
QString villageStyleName(const QString &style)
{
    if (style == QLatin1String("desert"))
        return QStringLiteral("沙漠村庄");
    if (style == QLatin1String("savanna"))
        return QStringLiteral("热带草原村庄");
    if (style == QLatin1String("taiga"))
        return QStringLiteral("针叶林村庄");
    if (style == QLatin1String("snowy"))
        return QStringLiteral("雪原村庄");
    return QStringLiteral("平原村庄");
}

/** 群系名是否属于该村庄风格的 has_structure/village_* 标签 */
bool villageStyleMatchesBiome(const QString &style, const QString &biome)
{
    if (style == QLatin1String("plains"))
        return biome == QLatin1String("plains") || biome == QLatin1String("meadow");
    if (style == QLatin1String("desert"))
        return biome == QLatin1String("desert");
    if (style == QLatin1String("savanna"))
        return biome == QLatin1String("savanna");
    if (style == QLatin1String("taiga"))
        return biome == QLatin1String("taiga");
    if (style == QLatin1String("snowy"))
        // 1.18 起叫 snowy_plains，1.16/1.17 的资源名是 snowy_tundra
        return biome == QLatin1String("snowy_plains")
            || biome == QLatin1String("snowy_tundra");
    return false;
}

/**
 * @brief 该风格村庄「起始段包围盒中心」（原版用这个点采样群系）
 *
 * JigsawPlacement 返回的 GenerationStub 位置是起始段包围盒中心，
 * Structure.generate 再拿它去查群系来判断该结构是否成立。
 */
bool villageStartCenter(const QString &style, quint64 seed, int chunkX, int chunkZ,
                        int &centerX, int &centerZ)
{
    LegacyRng rng;
    rng.setLargeFeatureSeed(qint64(seed), chunkX, chunkZ);
    const int turns = rng.nextInt(4);
    const ExactPool &pool = loadExactPool(
        QStringLiteral("village/") + style + QStringLiteral("/town_centers"));
    if (pool.templates.isEmpty())
        return false;
    const ExactElement element = pool.templates.at(rng.nextInt(pool.templates.size()));
    if (element.empty || element.loc.isEmpty())
        return false;
    const ExactBox box = exactElementBox(element, chunkX * 16, 0, chunkZ * 16, turns);
    if (!box.valid())
        return false;
    centerX = (box.x0 + box.x1) / 2;
    centerZ = (box.z0 + box.z1) / 2;
    return true;
}

/**
 * @brief 按原版规则确定村庄风格
 *
 * 原版 villages.json 里 5 种村庄同属一个结构集（各权重 1，顺序为
 * 平原 / 沙漠 / 热带草原 / 雪原 / 针叶林）：先用 setLargeFeatureSeed
 * 抽一个，生成它的起始段，再拿起始段包围盒中心采样群系；群系不在该
 * 结构的 has_structure 标签里就剔掉重抽。这里按同样的顺序与规则实现。
 */
QString villageStyleFor(quint64 seed, const QString &mcVersion, qint64 x, qint64 z)
{
    const int mc = StructureFinder::versionToInt(mcVersion);
    if (mc == 0)
        return QStringLiteral("plains");

    const int chunkX = int(x >> 4);
    const int chunkZ = int(z >> 4);

    QStringList styles;
    styles << QStringLiteral("plains") << QStringLiteral("desert")
           << QStringLiteral("savanna") << QStringLiteral("snowy")
           << QStringLiteral("taiga");
    QVector<int> weights(styles.size(), 1);
    int total = styles.size();

    LegacyRng rng;
    rng.setLargeFeatureSeed(qint64(seed), chunkX, chunkZ);

    while (!styles.isEmpty())
    {
        const int roll = rng.nextInt(total);
        int index = 0;
        int accumulated = 0;
        for (; index < styles.size(); ++index)
        {
            accumulated += weights.at(index);
            if (roll < accumulated)
                break;
        }
        if (index >= styles.size())
            index = styles.size() - 1;

        const QString style = styles.at(index);
        int centerX = 0, centerZ = 0;
        if (villageStartCenter(style, seed, chunkX, chunkZ, centerX, centerZ))
        {
            const int biome = BiomeCatalog::biomeAt(seed, mc, 0, centerX, centerZ);
            if (biome >= 0
                && villageStyleMatchesBiome(
                    style, QString::fromLatin1(biome2str(mc, biome))))
                return style;
        }

        total -= weights.at(index);
        styles.removeAt(index);
        weights.removeAt(index);
    }
    return QStringLiteral("plains");
}

/** 已放置的一段拼图 */
struct ExactPiece
{
    ExactElement element;
    int ox = 0, oy = 0, oz = 0;
    int turns = 0;
    int depth = 0;
    int groundDelta = 1;
    ExactBox box;
    int freeIndex = 0;
};

/** 自由区域：一个外包盒减去已占用盒 */
struct ExactFree
{
    ExactBox box;
    QVector<ExactBox> used;
};

/**
 * @brief 按原版拼图规则拼出一个拼图结构，返回段数
 *
 * 村庄与废弃营地共用这套流程：起点段从 startPool 按权重抽、整体朝向由
 * 结构随机源决定，之后每段的拼图点按原版顺序（洗牌模板池 → 洗牌四个朝向
 * → canAttach 对齐）接下一段，直到 maxDepth 层或接不上为止。
 *
 * @param startPoolId 起点模板池（如 village/plains/town_centers）
 * @param maxDepth    原版结构 JSON 的 size（村庄 6、废弃营地 2）
 * @param maxDistance 原版结构 JSON 的 max_distance_from_center（方块）
 */
int assembleExactJigsaw(Canvas &c, quint64 seed, qint64 blockX, qint64 blockZ,
                        const QString &startPoolId, int maxDepth,
                        int maxDistance, int startHeightBound = 0,
                        bool useExpansionHack = true)
{
    LegacyRng rng;
    rng.setLargeFeatureSeed(qint64(seed), int(blockX >> 4), int(blockZ >> 4));

    // 原版先由 start_height 取一个高度（UniformHeight 用同一个随机源抽一次），
    // 再抽起点朝向、最后抽起点模板。村庄用 constant 高度，不消耗随机数。
    if (startHeightBound > 0)
        rng.nextInt(startHeightBound);

    const ExactPool &startPool = loadExactPool(startPoolId);
    if (startPool.templates.isEmpty())
        return 0;

    const int startTurns = rng.nextInt(4);
    const ExactElement startElement =
        startPool.templates.at(rng.nextInt(startPool.templates.size()));
    if (startElement.empty
        || (startElement.loc.isEmpty() && !startElement.feature))
        return 0;

    // 起点放在区块最小角，地面高度记为 0（整体平移不影响布局）
    ExactBox startBox = exactElementBox(startElement, 0, 0, 0, startTurns);
    if (!startBox.valid())
        return 0;
    const int shiftY = -(startBox.y0 + 1); // groundLevelDelta = 1
    startBox.y0 += shiftY;
    startBox.y1 += shiftY;

    QVector<ExactPiece> pieces;
    QVector<ExactFree> frees;

    ExactFree globalFree;
    const int centerX = (startBox.x0 + startBox.x1) / 2;
    const int centerZ = (startBox.z0 + startBox.z1) / 2;
    globalFree.box.x0 = centerX - maxDistance;
    globalFree.box.x1 = centerX + maxDistance;
    globalFree.box.y0 = -320;
    globalFree.box.y1 = 320;
    globalFree.box.z0 = centerZ - maxDistance;
    globalFree.box.z1 = centerZ + maxDistance;
    globalFree.used.append(startBox);
    frees.append(globalFree);

    ExactPiece startPiece;
    startPiece.element = startElement;
    startPiece.ox = 0;
    startPiece.oy = shiftY;
    startPiece.oz = 0;
    startPiece.turns = startTurns;
    startPiece.depth = 0;
    startPiece.groundDelta = 1;
    startPiece.box = startBox;
    startPiece.freeIndex = 0;
    pieces.append(startPiece);

    // SequencedPriorityIterator：优先级高者先出，同优先级先进先出
    QMap<int, QVector<int>> queue;

    // 按值接收：拼装过程中 pieces 会扩容，引用会失效（原版也是值语义）
    auto placePiece = [&](ExactPiece piece) {
        const bool rigidParent = piece.element.projection == QLatin1String("rigid");
        const int parentMinY = piece.box.y0;
        int localFreeIndex = -1;

        const QVector<ExactJigsaw> jigsaws =
            exactElementJigsaws(piece.element, piece.ox, piece.oy, piece.oz,
                                piece.turns, rng);
        for (const ExactJigsaw &jigsaw : jigsaws)
        {
            int dx = 0, dy = 0, dz = 0;
            facingOffset(jigsaw.front, dx, dy, dz);
            const int frontX = jigsaw.x + dx;
            const int frontY = jigsaw.y + dy;
            const int frontZ = jigsaw.z + dz;

            const int relY = jigsaw.y - parentMinY;
            const int stepY = dy;

            const ExactPool &pool = loadExactPool(jigsaw.pool);
            const ExactPool &fallback = loadExactPool(pool.fallback);
            if (pool.templates.isEmpty() && fallback.templates.isEmpty())
                continue;

            // 拼图点正面落在本段包围盒内 → 使用本段自己的自由区域
            const ExactBox frontPoint{frontX, frontY, frontZ, frontX, frontY, frontZ};
            int freeIndex = piece.freeIndex;
            if (boxInside(piece.box, frontPoint))
            {
                if (localFreeIndex < 0)
                {
                    ExactFree local;
                    local.box = piece.box;
                    frees.append(local);
                    localFreeIndex = frees.size() - 1;
                }
                freeIndex = localFreeIndex;
            }
            ExactFree &free = frees[freeIndex];

            // 原版分别洗牌主池与 fallback，再拼接（两次独立的 Fisher-Yates）
            QVector<ExactElement> candidates;
            if (piece.depth != maxDepth)
            {
                candidates = pool.templates;
                for (int i = candidates.size(); i > 1; --i)
                    candidates.swapItemsAt(i - 1, rng.nextInt(i));
            }
            QVector<ExactElement> fallbackCandidates = fallback.templates;
            for (int i = fallbackCandidates.size(); i > 1; --i)
                fallbackCandidates.swapItemsAt(i - 1, rng.nextInt(i));
            candidates += fallbackCandidates;

            bool placed = false;
            for (const ExactElement &candidate : candidates)
            {
                if (candidate.empty)
                    break;
                if (candidate.loc.isEmpty() && !candidate.feature
                    && candidate.children.isEmpty())
                    continue;

                QVector<int> rotations{0, 1, 2, 3};
                for (int i = rotations.size(); i > 1; --i)
                    rotations.swapItemsAt(i - 1, rng.nextInt(i));

                for (int candidateTurns : rotations)
                {
                    const QVector<ExactJigsaw> childJigsaws =
                        exactElementJigsaws(candidate, 0, 0, 0, candidateTurns, rng);
                    const ExactBox candidateBox0 =
                        exactElementBox(candidate, 0, 0, 0, candidateTurns);

                    int expand = 0;
                    if (useExpansionHack && candidateBox0.valid()
                        && candidateBox0.height() <= 16)
                    {
                        for (const ExactJigsaw &childJigsaw : childJigsaws)
                        {
                            int cdx = 0, cdy = 0, cdz = 0;
                            facingOffset(childJigsaw.front, cdx, cdy, cdz);
                            const ExactBox frontPoint2{
                                childJigsaw.x + cdx, childJigsaw.y + cdy,
                                childJigsaw.z + cdz, childJigsaw.x + cdx,
                                childJigsaw.y + cdy, childJigsaw.z + cdz};
                            if (!boxInside(candidateBox0, frontPoint2))
                                continue;
                            const ExactPool &childPool =
                                loadExactPool(childJigsaw.pool);
                            expand = qMax(expand, exactPoolMaxY(childJigsaw.pool));
                            expand = qMax(expand, exactPoolMaxY(childPool.fallback));
                        }
                    }

                    for (const ExactJigsaw &childJigsaw : childJigsaws)
                    {
                        if (!canAttach(jigsaw, childJigsaw))
                            continue;

                        const int ox = frontX - childJigsaw.x;
                        const int oy = frontY - childJigsaw.y;
                        const int oz = frontZ - childJigsaw.z;
                        ExactBox childBox =
                            exactElementBox(candidate, ox, oy, oz, candidateTurns);
                        if (!childBox.valid())
                            continue;

                        const bool childRigid =
                            candidate.projection == QLatin1String("rigid");
                        const int off = relY - childJigsaw.y + stepY;
                        int wantMinY = 0;
                        if (rigidParent && childRigid)
                            wantMinY = parentMinY + off;
                        else
                            wantMinY = 0 - childJigsaw.y; // 地表高度按 0 计算

                        const int shift = wantMinY - childBox.y0;
                        childBox.y0 += shift;
                        childBox.y1 += shift;

                        if (expand > 0)
                        {
                            const int extent = qMax(expand + 1, childBox.height());
                            childBox.y1 = qMax(childBox.y1, childBox.y0 + extent - 1);
                        }

                        if (!boxInside(free.box, childBox))
                            continue;
                        bool collides = false;
                        for (const ExactBox &used : free.used)
                            if (boxOverlap(childBox, used))
                            {
                                collides = true;
                                break;
                            }
                        if (collides)
                            continue;

                        free.used.append(childBox);

                        ExactPiece child;
                        child.element = candidate;
                        child.ox = ox;
                        child.oy = oy + shift;
                        child.oz = oz;
                        child.turns = candidateTurns;
                        child.depth = piece.depth + 1;
                        child.groundDelta = childRigid ? piece.groundDelta - off : 1;
                        child.box = childBox;
                        child.freeIndex = freeIndex;
                        pieces.append(child);

                        if (piece.depth + 1 <= maxDepth)
                            queue[jigsaw.placementPriority].append(pieces.size() - 1);
                        placed = true;
                        break;
                    }
                    if (placed)
                        break;
                }
                if (placed)
                    break;
            }
        }
    };

    placePiece(pieces.first());

    while (!queue.isEmpty())
    {
        const int priority = queue.lastKey();
        QVector<int> &bucket = queue[priority];
        if (bucket.isEmpty())
        {
            queue.remove(priority);
            continue;
        }
        const int index = bucket.takeFirst();
        if (index >= 0 && index < pieces.size())
            placePiece(pieces.at(index));
    }

    // 逐段砌进画布
    for (const ExactPiece &piece : pieces)
    {
        if (piece.element.feature || piece.element.loc.isEmpty())
            continue;
        const auto tpl = StructureTemplate::load(piece.element.loc);
        if (!tpl)
            continue;
        for (const StructureTemplateBlock &block : tpl->blocks())
        {
            QString state = block.stateOverride.isEmpty()
                ? tpl->stateOf(block.state)
                : block.stateOverride;
            if (state.isEmpty() || state == QLatin1String("minecraft:air")
                || state == QLatin1String("minecraft:cave_air")
                || state == QLatin1String("minecraft:void_air"))
                continue;
            int rx = 0, rz = 0;
            exactTransform(block.x, block.y, block.z, piece.turns, rx, rz);
            if (((piece.turns % 4) + 4) % 4 != 0)
                state = rotateState(state, piece.turns);
            c.put(piece.ox + rx, piece.oy + block.y, piece.oz + rz, state);
        }
    }

    return pieces.size();
}

/** 村庄：中心广场 + 街道 + 房屋（从原版模板池按拼图规则拼装） */
bool buildVillageFromTemplate(Canvas &c, quint64 seed, qint64 x, qint64 z,
                              const QString &mcVersion, StructureModelInfo *out)
{
    // 原版 village_*.json：start_pool = town_centers、size = 6、
    // max_distance_from_center = 80；风格由村庄所在群系决定。
    const QString style = villageStyleFor(seed, mcVersion, x, z);
    const int count = assembleExactJigsaw(
        c, seed, x, z, QStringLiteral("village/") + style
            + QStringLiteral("/town_centers"), 6, 80);
    if (count <= 0)
        return false;
    if (out)
    {
        out->fromVanillaTemplate = true;
        out->variant = villageStyleName(style);
    }
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// 废弃营地（26.3 起）：帐篷段 + 营地段
// ────────────────────────────────────────────────────────────────────────────

/** 原版 18 种废弃营地变体（结构 id 后缀 = 帐篷/营地模板池所在群系） */
const char *const kCampVariants[] = {
    "bamboo_jungle", "birch_forest", "cherry_grove", "dappled_forest",
    "flower_forest", "forest", "meadow", "old_growth_birch_forest",
    "old_growth_pine_taiga", "old_growth_spruce_taiga", "pale_garden",
    "savanna", "snowy_taiga", "sparse_jungle", "swamp", "taiga",
    "windswept_forest", "wooded_badlands",
};

/** 变体的中文名（预览页「变体」一行） */
QString campVariantName(const QString &variant)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("bamboo_jungle"), QStringLiteral("竹林废弃营地")},
        {QStringLiteral("birch_forest"), QStringLiteral("白桦森林废弃营地")},
        {QStringLiteral("cherry_grove"), QStringLiteral("樱花树林废弃营地")},
        {QStringLiteral("dappled_forest"), QStringLiteral("斑驳森林废弃营地")},
        {QStringLiteral("flower_forest"), QStringLiteral("繁花森林废弃营地")},
        {QStringLiteral("forest"), QStringLiteral("森林废弃营地")},
        {QStringLiteral("meadow"), QStringLiteral("草甸废弃营地")},
        {QStringLiteral("old_growth_birch_forest"), QStringLiteral("原始白桦森林废弃营地")},
        {QStringLiteral("old_growth_pine_taiga"), QStringLiteral("原始松林废弃营地")},
        {QStringLiteral("old_growth_spruce_taiga"), QStringLiteral("原始云杉林废弃营地")},
        {QStringLiteral("pale_garden"), QStringLiteral("苍白之园废弃营地")},
        {QStringLiteral("savanna"), QStringLiteral("热带草原废弃营地")},
        {QStringLiteral("snowy_taiga"), QStringLiteral("积雪针叶林废弃营地")},
        {QStringLiteral("sparse_jungle"), QStringLiteral("稀疏丛林废弃营地")},
        {QStringLiteral("swamp"), QStringLiteral("沼泽废弃营地")},
        {QStringLiteral("taiga"), QStringLiteral("针叶林废弃营地")},
        {QStringLiteral("windswept_forest"), QStringLiteral("风袭森林废弃营地")},
        {QStringLiteral("wooded_badlands"), QStringLiteral("疏林恶地废弃营地")},
    };
    return names.value(variant, QStringLiteral("废弃营地"));
}

/**
 * @brief 按营地所在群系选原版变体
 *
 * 原版每个群系一种废弃营地结构（abandoned_camp_<biome>），各自有帐篷池与
 * 营地池。群系由 cubiomes 按种子采样；cubiomes 尚未收录的新群系
 * （26.x 的斑驳森林）落到森林变体，丛林类统一按稀疏丛林处理。
 */
QString campVariantFor(quint64 seed, const QString &mcVersion, qint64 x, qint64 z)
{
    const int mc = StructureFinder::versionToInt(mcVersion);
    if (mc == 0)
        return QStringLiteral("forest");
    const int biome = BiomeCatalog::biomeAt(seed, mc, 0, x, z);
    if (biome < 0)
        return QStringLiteral("forest");
    const QString name = QString::fromLatin1(biome2str(mc, biome));
    for (const char *variant : kCampVariants)
        if (name == QLatin1String(variant))
            return name;
    if (name == QLatin1String("jungle"))
        return QStringLiteral("sparse_jungle");
    if (name.contains(QLatin1String("taiga")))
        return QStringLiteral("taiga");
    if (name.contains(QLatin1String("birch")))
        return QStringLiteral("birch_forest");
    if (name.contains(QLatin1String("savanna")))
        return QStringLiteral("savanna");
    return QStringLiteral("forest");
}

/**
 * @brief 废弃营地：帐篷段 + 营地段（从原版模板池按拼图规则拼装）
 *
 * 原版 abandoned_camp_*.json：start_pool = abandoned_camp/tent/<biome>、
 * size = 2、max_distance_from_center = 80。帐篷段底部有一个 connector
 * 拼图点，营地段的 end_connector 接上它；帐篷段另外几个点接树木特征
 * （原版由特征系统单独生成，预览里不画）。
 */
bool buildAbandonedCampFromTemplate(Canvas &c, quint64 seed, qint64 x, qint64 z,
                                    const QString &mcVersion,
                                    StructureModelInfo *out)
{
    const QString variant = campVariantFor(seed, mcVersion, x, z);
    const int count = assembleExactJigsaw(
        c, seed, x, z, QStringLiteral("abandoned_camp/tent/") + variant, 2, 80);
    if (count <= 0)
        return false;
    if (out)
    {
        out->fromVanillaTemplate = true;
        out->variant = campVariantName(variant);
    }
    return true;
}

/** 远古城市：中心区 + 外围街区（从原版模板池按拼图规则拼装） */
bool buildAncientCityFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    // 原版 ancient_city.json：start_pool = city_center、size = 7；
    // 远古城市单体很大，预览只展开两层，避免体积失控。
    const int count = assembleJigsaw(c, rng,
                                     QStringLiteral("minecraft:ancient_city/city_center"),
                                     2, 7, 96);
    if (count <= 0)
        return false;

    out->variant = count > 1 ? QStringLiteral("远古城市")
                             : QStringLiteral("远古城市中心");
    out->fromVanillaTemplate = true;
    return true;
}

/** 末地城：塔基 + 若干层塔身 + 塔顶（原版 EndCityPieces 的分层堆叠） */
bool buildEndCityFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    if (placeTemplate(c, QStringLiteral("end_city/tower_base"), 0, 0, 0) <= 0)
        return false;

    // 塔身每层高 4 格，塔顶（9×5×9）比塔身（7×7）宽一圈，居中放置
    const int floors = rng.range(2, 4);
    int y = 7;
    for (int i = 0; i < floors; ++i)
    {
        placeTemplate(c, QStringLiteral("end_city/tower_floor"), 0, y, 0);
        y += 4;
    }
    placeTemplate(c, QStringLiteral("end_city/tower_top"), -1, y, -1);

    out->variant = (floors >= 4) ? QStringLiteral("末地城塔（高塔）")
                                 : QStringLiteral("末地城塔");
    out->fromVanillaTemplate = true;
    return true;
}

/** 试炼密室：从原版模板池按拼图规则向四周与上下展开 */
/** 试炼密室：按原版 JigsawPlacement 从起始房间拼出整座地下迷宫 */
bool buildTrialChambersFromTemplate(Canvas &c, quint64 seed, qint64 x, qint64 z,
                                    StructureModelInfo *out)
{
    // 原版 trial_chambers.json：start_pool = trial_chambers/chamber/end、
    // size = 20、max_distance_from_center = 116、use_expansion_hack = false、
    // start_height = uniform(-40..-20)。起始高度会先消耗一次随机数，
    // 之后才是起点朝向与起点模板——少这一步整座迷宫都会拼错。
    const int count = assembleExactJigsaw(
        c, seed, x, z, QStringLiteral("trial_chambers/chamber/end"), 20, 116, 21,
        false);
    if (count <= 0)
        return false;

    out->variant = QStringLiteral("试炼密室");
    out->fromVanillaTemplate = true;
    return true;
}

/** 堡垒遗迹：原版起始段是空框架 + 若干拼图接口，建筑由子模板展开 */
bool buildBastionFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    // 原版 bastion_remnant.json：start_pool = bastion/starts、size = 6。
    const int count = assembleJigsaw(c, rng,
                                     QStringLiteral("minecraft:bastion/starts"),
                                     6, 40, 88);
    if (count <= 0)
        return false;

    out->variant = count > 1 ? QStringLiteral("堡垒遗迹")
                             : QStringLiteral("堡垒遗迹入口");
    out->fromVanillaTemplate = true;
    return true;
}

/** 林地府邸：原版房间模板铺满 8 格一格的网格，再围外墙、逐格盖屋顶 */
bool buildMansionFromTemplate(Canvas &c, Rng &rng, StructureModelInfo *out)
{
    // 原版宅邸的房间就是这些 7×8×7 的模板，一格 8 格（房间 7 宽 + 1 格隔墙），
    // 外墙 2 格厚包一圈。原版的房间排布由游戏代码随机决定，这里按同样的
    // 网格规则铺房间、围墙、盖屋顶，房间内部（书架、箱子、床、蜘蛛网）都是原版模板。
    static const char *const roomsShort[] = {
        "woodland_mansion/1x1_a1", "woodland_mansion/1x1_a2",
        "woodland_mansion/1x1_a3", "woodland_mansion/1x1_a4",
        "woodland_mansion/1x1_a5", "woodland_mansion/1x1_as1",
        "woodland_mansion/1x1_as2", "woodland_mansion/1x1_as3",
        "woodland_mansion/1x1_as4",
    };
    static const char *const roomsTall[] = {
        "woodland_mansion/1x1_b1", "woodland_mansion/1x1_b2",
        "woodland_mansion/1x1_b3", "woodland_mansion/1x1_b4",
        "woodland_mansion/1x1_b5",
    };
    static const char *const wallIds[] = {
        "woodland_mansion/wall_window", "woodland_mansion/wall_flat",
    };

    const int cols = rng.range(4, 5);
    const int rows = rng.range(3, 4);
    const int spanX = cols * 8 - 1;
    const int spanZ = rows * 8 - 1;

    // 内部房间有 1/3 概率用 11 格高的款式，屋顶就会高出一截
    QVector<bool> tall(cols * rows, false);
    for (int i = 0; i < cols; ++i)
    {
        for (int j = 0; j < rows; ++j)
        {
            const bool edge = (i == 0 || i == cols - 1 || j == 0 || j == rows - 1);
            tall[i * rows + j] = !edge && rng.chance(45);
        }
    }

    int placed = 0;
    for (int i = 0; i < cols; ++i)
    {
        for (int j = 0; j < rows; ++j)
        {
            const bool isTall = tall[i * rows + j];
            const char *const *ids = isTall ? roomsTall : roomsShort;
            const int count = isTall ? 5 : 9;
            placed += placeTemplate(c, st(ids[rng.next(count)]),
                                    i * 8, 0, j * 8, rng.next(4));
        }
    }
    if (placed <= 0)
        return false;

    // 房间之间 1 格厚的隔墙
    for (int i = 0; i < cols - 1; ++i)
        for (int j = 0; j < rows; ++j)
            placeTemplate(c, QStringLiteral("woodland_mansion/indoors_wall_1"),
                          i * 8 + 7, 0, j * 8, 0);
    for (int j = 0; j < rows - 1; ++j)
        for (int i = 0; i < cols; ++i)
            placeTemplate(c, QStringLiteral("woodland_mansion/indoors_wall_1"),
                          i * 8, 0, j * 8 + 7, 1);

    // 一圈外墙（圆石台基 + 深色橡木墙）：窗只开在两侧短墙上，
    // 正面长墙用实墙，免得从外面直接看见屋内亮着的木板和花盆
    for (int j = 0; j < rows; ++j)
    {
        placeTemplate(c, QStringLiteral("woodland_mansion/wall_flat"),
                      -2, 0, j * 8, 0);
        placeTemplate(c, QStringLiteral("woodland_mansion/wall_flat"),
                      spanX + 1, 0, j * 8, 2);
    }
    for (int i = 0; i < cols; ++i)
    {
        placeTemplate(c, st(wallIds[(i + 0) % 2]), i * 8, 0, -2, 1);
        placeTemplate(c, st(wallIds[(i + 1) % 2]), i * 8, 0, spanZ + 1, 3);
    }
    // 四角补墙
    placeTemplate(c, QStringLiteral("woodland_mansion/wall_flat"), -2, 0, -2, 0);
    placeTemplate(c, QStringLiteral("woodland_mansion/wall_flat"), spanX + 1, 0, -2, 2);
    placeTemplate(c, QStringLiteral("woodland_mansion/wall_flat"), -2, 0, spanZ - 6, 0);
    placeTemplate(c, QStringLiteral("woodland_mansion/wall_flat"), spanX + 1, 0, spanZ - 6, 2);

    // 逐格盖屋顶，高房间的屋顶高 3 格，形成错落的屋顶线
    for (int i = 0; i < cols; ++i)
    {
        for (int j = 0; j < rows; ++j)
        {
            const int y = tall[i * rows + j] ? 11 : 8;
            placeTemplate(c, QStringLiteral("woodland_mansion/roof"), i * 8, y, j * 8, 0);
        }
    }

    out->variant = QStringLiteral("林地府邸");
    out->fromVanillaTemplate = true;
    return true;
}

/**
 * @brief 要塞：按原版 StrongholdPieces 逐段拼装、逐区块落块
 *
 * 要塞没有结构模板 NBT，房间类型、朝向、段间拼接、石砖的裂纹/苔藓变体
 * 全部由 Java 代码在生成区块时算出；这里把 1.18+ 的拼装流程完整移植，
 * 同一个世界种子与要塞坐标能得到与游戏内一致的房间布局和方块。
 */
bool buildStrongholdFromLayout(Canvas &c, quint64 seed, qint64 x, qint64 z,
                               const QString &mcVersion, StructureModelInfo *out)
{
    if (StructureFinder::versionToInt(mcVersion) < MC_1_18)
        return false;  // 1.18 之前的要塞分布/落块规则不同，仍用程序化模型
    // StructureFinder 给出的是起始区块内 (4,4) 处的方块坐标
    const int cx = int((x - 4) / 16);
    const int cz = int((z - 4) / 16);
    const StrongholdModel::Model model = StrongholdModel::build(seed, cx, cz);
    if (!model.ok)
        return false;
    for (auto it = model.blocks.constBegin(); it != model.blocks.constEnd(); ++it)
    {
        c.put(StrongholdModel::Model::keyX(it.key()),
              StrongholdModel::Model::keyY(it.key()),
              StrongholdModel::Model::keyZ(it.key()), it.value());
    }
    if (c.cells.isEmpty())
        return false;
    out->variant = model.variant;
    out->fromVanillaTemplate = true;
    return true;
}

/**
 * @brief 下界要塞：按原版 NetherFortressPieces 逐段拼装、逐块落块
 *
 * 下界要塞没有结构模板 NBT，桥段/走廊/烈焰人房间/下界疣楼梯塔全部由
 * Java 代码在生成区块时算出；这里把 1.21.4 的拼装流程完整移植，
 * 同一个世界种子与要塞坐标能得到与游戏内一致的段序、朝向和方块。
 */
bool buildFortressFromLayout(Canvas &c, quint64 seed, qint64 x, qint64 z,
                             const QString &mcVersion, StructureModelInfo *out)
{
    if (StructureFinder::versionToInt(mcVersion) < MC_1_16)
        return false;  // 1.16 之前的要塞布局不同，仍用程序化模型
    // StructureFinder 给出的是起始区块原点的方块坐标
    const int cx = int(x) >> 4;
    const int cz = int(z) >> 4;
    const FortressModel::Model model = FortressModel::build(seed, cx, cz, mcVersion);
    if (!model.ok)
        return false;
    for (auto it = model.blocks.constBegin(); it != model.blocks.constEnd(); ++it)
    {
        int rx, y, rz;
        FortressModel::unpackKey(it.key(), rx, y, rz);
        c.put(rx, y, rz, it.value());
    }
    if (c.cells.isEmpty())
        return false;
    out->variant = model.variant;
    out->fromVanillaTemplate = true;
    return true;
}

/**
 * @brief 废弃矿井：按原版 MineshaftStructure / MineshaftPieces 逐段拼装、逐块落块
 *
 * 矿井没有结构模板 NBT，主厅、巷道、路口、楼梯和木支架、铁轨、火蜘蛛网
 * 全部由 Java 代码在生成区块时算出；这里把 1.21.4 的拼装流程完整移植，
 * 同一个世界种子与起始区块能得到与游戏内一致的段序、朝向、高度和方块。
 */
bool buildMineshaftFromLayout(Canvas &c, quint64 seed, qint64 x, qint64 z,
                              const QString &mcVersion, StructureModelInfo *out)
{
    if (StructureFinder::versionToInt(mcVersion) < MC_1_13)
        return false;   // 1.13 以前的矿井地台规则不同，仍用程序化模型
    // StructureFinder 给出的是起始区块原点的方块坐标
    const int cx = int(x) >> 4;
    const int cz = int(z) >> 4;
    // 恶地群系用原版恶地变体（深色橡木 + 按地表下移）
    const int mc = StructureFinder::versionToInt(mcVersion);
    const int biome = BiomeCatalog::biomeAt(seed, mc, 0, x + 8, z + 8);
    const bool mesa = biome >= 0
        && QString::fromLatin1(biome2str(mc, biome)).contains(QLatin1String("badlands"));
    const MineshaftModel::Model model =
        MineshaftModel::build(seed, cx, cz, mcVersion, 3, 1, -64, 63, mesa);
    if (!model.ok)
        return false;
    for (auto it = model.blocks.constBegin(); it != model.blocks.constEnd(); ++it)
    {
        int rx, y, rz;
        MineshaftModel::unpackKey(it.key(), rx, y, rz);
        c.put(rx, y, rz, it.value());
    }
    if (c.cells.isEmpty())
        return false;
    out->variant = model.variant;
    out->fromVanillaTemplate = true;
    return true;
}

/** 有原版模板的结构用模板逐块还原；其余仍走程序化近似 */
bool buildFromVanillaTemplate(const QString &key, Canvas &c, Rng &rng,
                              StructureModelInfo *out, quint64 seed,
                              qint64 x, qint64 z, const QString &mcVersion)
{
    if (key == QLatin1String("stronghold"))
        return buildStrongholdFromLayout(c, seed, x, z, mcVersion, out);
    if (key == QLatin1String("fortress"))
        return buildFortressFromLayout(c, seed, x, z, mcVersion, out);
    if (key == QLatin1String("mineshaft"))
        return buildMineshaftFromLayout(c, seed, x, z, mcVersion, out);
    if (key == QLatin1String("igloo"))
        return buildIglooFromTemplate(c, rng, out);
    if (key == QLatin1String("shipwreck"))
        return buildShipwreckFromTemplate(c, rng, out);
    if (key == QLatin1String("ocean_ruin"))
        return buildOceanRuinFromTemplate(c, rng, out);
    if (key == QLatin1String("ruined_portal"))
        return buildRuinedPortalFromTemplate(c, rng, out);
    if (key == QLatin1String("nether_fossil"))
        return buildNetherFossilFromTemplate(c, rng, out);
    if (key == QLatin1String("end_ship"))
        return buildEndShipFromTemplate(c, rng, out);
    if (key == QLatin1String("outpost"))
        return buildOutpostFromTemplate(c, rng, out);
    if (key == QLatin1String("trail_ruins"))
        return buildTrailRuinsFromTemplate(c, rng, out);
    if (key == QLatin1String("village"))
        return buildVillageFromTemplate(c, seed, x, z, mcVersion, out);
    if (key == QLatin1String("abandoned_camp"))
        return buildAbandonedCampFromTemplate(c, seed, x, z, mcVersion, out);
    if (key == QLatin1String("ancient_city"))
        return buildAncientCityFromTemplate(c, rng, out);
    if (key == QLatin1String("end_city"))
        return buildEndCityFromTemplate(c, rng, out);
    if (key == QLatin1String("trial_chambers"))
        return buildTrialChambersFromTemplate(c, seed, x, z, out);
    if (key == QLatin1String("bastion"))
        return buildBastionFromTemplate(c, rng, out);
    if (key == QLatin1String("mansion"))
        return buildMansionFromTemplate(c, rng, out);
    if (key == QLatin1String("monument"))
        return buildMonumentFromTemplate(c, seed, x, z, out);
    return false;
}

} // namespace

bool StructureModelBuilder::supports(const QString &structKey)
{
    static const char *keys[] = {
        "village", "desert_pyramid", "jungle_temple", "swamp_hut", "igloo",
        "ocean_ruin", "shipwreck", "ruined_portal", "outpost", "monument",
        "mansion", "stronghold", "mineshaft", "ancient_city", "trial_chambers",
        "trail_ruins", "desert_well", "buried_treasure", "fortress", "bastion",
        "end_city", "end_gateway", "abandoned_camp", "nether_fossil", "end_ship",
    };
    for (const char *k : keys)
        if (structKey == QLatin1String(k))
            return true;
    return false;
}

SchematicDocument *StructureModelBuilder::build(const QString &structKey,
                                                qint64 x, qint64 z,
                                                quint64 seed, bool hasSeed,
                                                const QString &mcVersion,
                                                StructureModelInfo *info)
{
    StructureModelInfo localInfo;
    StructureModelInfo *out = info ? info : &localInfo;
    out->error.clear();
    if (!hasSeed)
    {
        out->error = QStringLiteral("尚未读取到世界种子");
        return nullptr;
    }
    if (!supports(structKey))
    {
        out->error = QStringLiteral("该结构暂无三维预览");
        return nullptr;
    }

    Canvas canvas;
    const quint64 mixed = McRandom::mixPosition(seed, x, z, 64, 0x5354727563ULL);
    Rng rng(mixed);
    int turns = rng.next(4);
    out->rotation = turns;

    // 有原版结构模板的结构：逐块还原；其余结构仍走程序化近似
    if (!buildFromVanillaTemplate(structKey, canvas, rng, out, seed, x, z, mcVersion))
    {
        if (structKey == QLatin1String("village"))
            buildVillage(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("desert_pyramid"))
            buildDesertPyramid(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("jungle_temple"))
            buildJungleTemple(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("swamp_hut"))
            buildSwampHut(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("igloo"))
            buildIgloo(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("ocean_ruin"))
            buildOceanRuin(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("shipwreck"))
            buildShipwreck(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("ruined_portal"))
            buildRuinedPortal(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("outpost"))
            buildOutpost(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("mansion"))
            buildMansion(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("stronghold"))
            buildStronghold(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("mineshaft"))
            buildMineshaft(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("ancient_city"))
            buildAncientCity(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("trial_chambers"))
            buildTrialChambers(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("trail_ruins"))
            buildTrailRuins(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("desert_well"))
            buildDesertWell(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("buried_treasure"))
            buildBuriedTreasure(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("fortress"))
            buildFortress(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("bastion"))
            buildBastion(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("end_city"))
            buildEndCity(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("end_gateway"))
            buildEndGateway(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("abandoned_camp"))
            buildAbandonedCamp(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("nether_fossil"))
            buildNetherFossil(canvas, rng, mcVersion, out);
        else if (structKey == QLatin1String("end_ship"))
            buildEndShip(canvas, rng, mcVersion, out);
        // @@MORE_MODELS@@
    }

    if (canvas.cells.isEmpty())
    {
        out->error = QStringLiteral("结构模型生成失败");
        return nullptr;
    }

    // 村庄、废弃营地、要塞的朝向已由原版拼装随机决定，不再整体旋转
    if (structKey == QLatin1String("village")
        || structKey == QLatin1String("abandoned_camp")
        || structKey == QLatin1String("monument")
        || structKey == QLatin1String("stronghold")
        || structKey == QLatin1String("fortress")
        || structKey == QLatin1String("mineshaft"))
    {
        turns = 0;
        out->rotation = 0;
    }
    canvas.rotate(turns);

    int x0, y0, z0, x1, y1, z1;
    canvas.bounds(x0, y0, z0, x1, y1, z1);
    const int sx = x1 - x0 + 1, sy = y1 - y0 + 1, sz = z1 - z0 + 1;
    if (sx <= 0 || sy <= 0 || sz <= 0)
    {
        out->error = QStringLiteral("结构模型为空");
        return nullptr;
    }

    auto *doc = new SchematicDocument;
    if (!doc->createEmpty(sx, sy, sz))
    {
        delete doc;
        out->error = QStringLiteral("结构模型尺寸无效");
        return nullptr;
    }
    int count = 0;
    for (auto it = canvas.cells.constBegin(); it != canvas.cells.constEnd(); ++it)
    {
        int cx, cy, cz;
        unpack(it.key(), cx, cy, cz);
        if (doc->setBlock(cx - x0, cy - y0, cz - z0,
                          SchematicBlockState::fromString(it.value())))
            ++count;
    }
    out->sizeX = sx;
    out->sizeY = sy;
    out->sizeZ = sz;
    out->blockCount = count;

    return doc;
}



