#include "WorldMapRenderer.h"

#include "SeedTerrainRenderer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QHash>
#include <QMutexLocker>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QThreadPool>
#include <QtConcurrentRun>

#ifdef Q_OS_WIN
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace WorldMapRenderer {

namespace {

// ═══════════════════════ 压缩解码 ═══════════════════════

/** 解压单个区块载荷：compType 1 = gzip、2 = zlib、3 = 未压缩 */
bool inflatePayload(const uchar *src, int srcLen, int compType, QByteArray &out)
{
    out.clear();
    if (compType == 3)
    {
        out.append(reinterpret_cast<const char *>(src), srcLen);
        return !out.isEmpty();
    }
    if (srcLen <= 0)
        return false;

    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    const int windowBits = (compType == 1) ? (16 + MAX_WBITS) : MAX_WBITS;
    if (inflateInit2(&strm, windowBits) != Z_OK)
        return false;

    strm.next_in  = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(src));
    strm.avail_in = uInt(srcLen);

    uchar buf[1 << 16];
    int ret = Z_OK;
    while (true)
    {
        strm.next_out  = buf;
        strm.avail_out = uInt(sizeof(buf));
        ret = inflate(&strm, Z_NO_FLUSH);
        out.append(reinterpret_cast<const char *>(buf), sizeof(buf) - strm.avail_out);
        if (ret == Z_STREAM_END)
            break;
        if (ret != Z_OK && ret != Z_BUF_ERROR)
            break;
        if (strm.avail_in == 0)
            break;
        if (out.size() > (64 << 20)) // 区块不可能这么大，防损坏文件撑爆内存
            break;
    }
    inflateEnd(&strm);
    return !out.isEmpty();
}

// ═══════════════════════ NBT 流式读取 ═══════════════════════

/**
 * 大端序字节游标。所有越界访问都会置 bad 并把游标推到结尾，
 * 后续读取直接返回 0，避免对损坏数据做额外的边界判断。
 */
struct Cursor
{
    const uchar *p   = nullptr;
    const uchar *end = nullptr;
    bool bad = false;

    Cursor() = default;
    Cursor(const uchar *base, qint64 len)
        : p(base), end(base + len)
    {
    }

    void need(qint64 n)
    {
        if (bad || n < 0 || p + n > end)
        {
            bad = true;
            p = end;
        }
    }

    quint8 u8()  { need(1); if (bad) return 0; return *p++; }
    qint8  i8()  { return qint8(u8()); }

    quint16 u16()
    {
        need(2);
        if (bad) return 0;
        const quint16 v = quint16((quint16(p[0]) << 8) | p[1]);
        p += 2;
        return v;
    }

    qint16 i16() { return qint16(u16()); }

    quint32 u32()
    {
        need(4);
        if (bad) return 0;
        const quint32 v = (quint32(p[0]) << 24) | (quint32(p[1]) << 16)
                        | (quint32(p[2]) << 8)  |  quint32(p[3]);
        p += 4;
        return v;
    }

    qint32 i32() { return qint32(u32()); }

    qint64 i64()
    {
        need(8);
        if (bad) return 0;
        quint64 v = 0;
        for (int i = 0; i < 8; ++i)
            v = (v << 8) | p[i];
        p += 8;
        return qint64(v);
    }

    QString str()
    {
        const quint16 n = u16();
        need(n);
        if (bad) return QString();
        QString s = QString::fromUtf8(reinterpret_cast<const char *>(p), n);
        p += n;
        return s;
    }

    void skip(qint64 n)
    {
        need(n);
        if (!bad)
            p += n;
    }

    /** 当前位置相对缓冲区首地址的偏移 */
    qint64 offsetFrom(const uchar *base) const { return qint64(p - base); }
};

/** 各 NBT 类型的载荷宽度；-1 表示变长（String / List / Compound） */
constexpr int payloadWidth(quint8 type)
{
    switch (type)
    {
    case 1:  return 1;
    case 2:  return 2;
    case 3: case 5: return 4;
    case 4: case 6: return 8;
    case 11: return 4;
    case 12: return 8;
    case 0:  return 0;
    default: return -1;
    }
}

/** NBT 结构最大嵌套深度（防损坏/恶意数据把 skipPayload 递归成爆栈） */
constexpr int kMaxNbtDepth = 48;

void skipPayload(Cursor &c, quint8 type, int depth = 0);

/** 跳过 List 的 n 个元素（元素载荷无标签名，按类型连续存放） */
void skipListElements(Cursor &c, quint8 elemType, qint32 n, int depth)
{
    if (c.bad || n <= 0)
        return;
    if (n > (1 << 22) || depth > kMaxNbtDepth)
    {
        c.bad = true;
        return;
    }
    const int w = payloadWidth(elemType);
    if (w > 0)
    {
        c.skip(qint64(n) * w);
        return;
    }
    if (w == 0)
        return;
    for (qint32 i = 0; i < n && !c.bad; ++i)
        skipPayload(c, elemType, depth + 1);
}

void skipPayload(Cursor &c, quint8 type, int depth)
{
    if (depth > kMaxNbtDepth)
    {
        c.bad = true;
        return;
    }
    switch (type)
    {
    case 0:
        break;
    case 1:
        c.skip(1);
        break;
    case 2:
        c.skip(2);
        break;
    case 3: case 5:
        c.skip(4);
        break;
    case 4: case 6:
        c.skip(8);
        break;
    case 7:
    {
        const qint32 n = c.i32();
        if (n > 0) c.skip(n);
        break;
    }
    case 8:
    {
        const quint16 n = c.u16();
        if (n > 0) c.skip(n);
        break;
    }
    case 9:
    {
        const quint8 et = c.u8();
        const qint32 n  = c.i32();
        skipListElements(c, et, n, depth);
        break;
    }
    case 10:
        while (!c.bad)
        {
            const quint8 t = c.u8();
            if (c.bad || t == 0)
                break;
            c.str();
            skipPayload(c, t, depth + 1);
        }
        break;
    case 11:
    {
        const qint32 n = c.i32();
        if (n > 0) c.skip(qint64(n) * 4);
        break;
    }
    case 12:
    {
        const qint32 n = c.i32();
        if (n > 0) c.skip(qint64(n) * 8);
        break;
    }
    default:
        c.bad = true;
        break;
    }
}

// ═══════════════════════ 区块解析 ═══════════════════════

/** 区块内一个 section 的位置索引（只记录偏移，按需再解析，避免无谓解码） */
struct SectionRef
{
    int y = 0;

    // 调色板：List<Compound>，paletteOff 指向第一个元素
    qint64 paletteOff = -1;
    qint32 paletteCount = 0;

    // 位打包数据：LongArray，dataOff 指向第一个 long
    qint64 dataOff = -1;
    qint32 dataCount = 0;

    // 旧格式（1.12-）：4096 字节方块 ID + 半字节 Data/Add
    qint64 blocksOff = -1;
    qint32 blocksCount = 0;
    qint64 nibbleOff = -1;
    qint32 nibbleCount = 0;
    qint64 addOff = -1;
    qint32 addCount = 0;

    bool hasPalette() const { return paletteOff >= 0 && paletteCount > 0; }
    bool hasStates()  const { return dataOff >= 0 && dataCount > 0; }
    bool hasLegacy()  const { return blocksOff >= 0 && blocksCount > 0; }
};

struct ChunkInfo
{
    bool ok = false;
    int dataVersion = 0;
    bool hasPos = false;
    int xPos = 0;
    int zPos = 0;
    QVector<SectionRef> sections;
};

/** 调色板条目上限：真实调色板最多几百项，异常大直接判坏（防 reserve 撑爆内存） */
constexpr qint32 kMaxPaletteEntries = 8192;

/** 记录 List 的元素区间并跳过 */
void readListTag(Cursor &c, const uchar *base, qint64 *off, qint32 *count)
{
    const quint8 et = c.u8();
    const qint32 n  = c.i32();
    if (n < 0 || n > kMaxPaletteEntries)
    {
        // 计数异常（多为数据错位/损坏），判坏让上层跳过该 section
        c.bad = true;
        if (off)   *off = -1;
        if (count) *count = 0;
        return;
    }
    if (off)    *off = c.offsetFrom(base);
    if (count)  *count = (et == 10) ? n : 0;
    skipListElements(c, et, n, 0);
}

/** 记录数组的元素区间并跳过 */
void readArrayTag(Cursor &c, const uchar *base, int elemSize,
                  qint64 *off, qint32 *count)
{
    const qint32 n = c.i32();
    if (count) *count = n;
    if (off)   *off = c.offsetFrom(base);
    if (n > 0) c.skip(qint64(n) * elemSize);
}

void walkSectionsList(Cursor &c, const uchar *base, QVector<SectionRef> &out)
{
    const quint8 elemType = c.u8();
    const qint32 count    = c.i32();
    if (c.bad)
        return;
    if (count < 0 || count > 512)
    {
        c.bad = true;
        return;
    }
    if (elemType != 10)
    {
        skipListElements(c, elemType, count, 0);
        return;
    }

    for (qint32 i = 0; i < count && !c.bad; ++i)
    {
        SectionRef s;
        while (!c.bad)
        {
            const quint8 t = c.u8();
            if (c.bad || t == 0)
                break;
            const QString name = c.str();
            if (c.bad)
                break;

            if (name == QLatin1String("Y"))
            {
                if (t == 1)      s.y = c.i8();
                else if (t == 2) s.y = c.i16();
                else if (t == 3) s.y = c.i32();
                else skipPayload(c, t);
            }
            else if (t == 10 && name == QLatin1String("block_states"))
            {
                while (!c.bad)
                {
                    const quint8 t2 = c.u8();
                    if (c.bad || t2 == 0)
                        break;
                    const QString n2 = c.str();
                    if (c.bad) break;
                    if (t2 == 9 && n2 == QLatin1String("palette"))
                        readListTag(c, base, &s.paletteOff, &s.paletteCount);
                    else if (t2 == 12 && n2 == QLatin1String("data"))
                        readArrayTag(c, base, 8, &s.dataOff, &s.dataCount);
                    else
                        skipPayload(c, t2);
                }
            }
            else if (t == 9 && name == QLatin1String("Palette"))
                readListTag(c, base, &s.paletteOff, &s.paletteCount);
            else if (t == 12 && name == QLatin1String("BlockStates"))
                readArrayTag(c, base, 8, &s.dataOff, &s.dataCount);
            else if (t == 7 && name == QLatin1String("Blocks"))
                readArrayTag(c, base, 1, &s.blocksOff, &s.blocksCount);
            else if (t == 7 && (name == QLatin1String("Data")
                             || name == QLatin1String("Add")))
            {
                qint64 off = -1;
                qint32 cnt = 0;
                readArrayTag(c, base, 1, &off, &cnt);
                if (name == QLatin1String("Data")) { s.nibbleOff = off; s.nibbleCount = cnt; }
                else                               { s.addOff = off;    s.addCount = cnt; }
            }
            else
                skipPayload(c, t);
        }
        out.append(s);
    }
}

ChunkInfo parseChunk(const uchar *data, qint64 len)
{
    ChunkInfo info;
    Cursor c(data, len);

    const quint8 rootType = c.u8();
    if (rootType != 10)
        return info;
    c.str(); // 根标签名

    while (!c.bad)
    {
        const quint8 t = c.u8();
        if (c.bad || t == 0)
            break;
        const QString name = c.str();
        if (c.bad)
            break;

        if (t == 3 && name == QLatin1String("DataVersion"))
            info.dataVersion = c.i32();
        else if (t == 3 && name == QLatin1String("xPos"))
        { info.xPos = c.i32(); info.hasPos = true; }
        else if (t == 3 && name == QLatin1String("zPos"))
        { info.zPos = c.i32(); info.hasPos = true; }
        else if (t == 9 && name == QLatin1String("sections"))
            walkSectionsList(c, data, info.sections);
        else if (t == 10 && name == QLatin1String("Level"))
        {
            // 1.17 及更早：坐标与区块都挂在 Level 下
            while (!c.bad)
            {
                const quint8 t2 = c.u8();
                if (c.bad || t2 == 0)
                    break;
                const QString n2 = c.str();
                if (c.bad) break;
                if (t2 == 3 && n2 == QLatin1String("xPos"))
                { info.xPos = c.i32(); info.hasPos = true; }
                else if (t2 == 3 && n2 == QLatin1String("zPos"))
                { info.zPos = c.i32(); info.hasPos = true; }
                else if (t2 == 9 && (n2 == QLatin1String("Sections")
                                  || n2 == QLatin1String("sections")))
                    walkSectionsList(c, data, info.sections);
                else
                    skipPayload(c, t2);
            }
        }
        else
            skipPayload(c, t);
    }

    info.ok = !c.bad;
    return info;
}

// ═══════════════════════ 调色板 / 位打包 ═══════════════════════

/** 解析 section 调色板，返回方块 ID 列表（顺序即索引） */
QStringList readPalette(const uchar *data, qint64 bufLen, const SectionRef &s)
{
    QStringList names;
    if (!s.hasPalette())
        return names;

    Cursor c(data + s.paletteOff, bufLen - s.paletteOff);
    names.reserve(s.paletteCount);
    for (qint32 i = 0; i < s.paletteCount && !c.bad; ++i)
    {
        QString name;
        // 列表元素 = 裸 Compound 载荷（没有类型字节与标签名）
        while (!c.bad)
        {
            const quint8 t = c.u8();
            if (c.bad || t == 0)
                break;
            const QString n = c.str();
            if (c.bad) break;
            if (t == 8 && n == QLatin1String("Name"))
                name = c.str();
            else
                skipPayload(c, t);
        }
        names.append(name);
    }
    if (c.bad)
        names.clear();
    return names;
}

int ceilLog2(int n)
{
    int bits = 0;
    while ((1 << bits) < n)
        ++bits;
    return bits;
}

/**
 * 解出 4096 个方块索引。
 *
 * 位宽与打包方式随版本变化（1.16 前条目可跨 long、位宽下限 4；
 * 1.16 起按 long 对齐），这里不依赖版本号硬猜，而是逐个候选
 * 方案试解并校验所有索引都落在调色板范围内 —— 错误方案几乎必然
 * 解出越界索引，因此判定很稳。
 */
bool decodeStates(const uchar *data, qint64 bufLen, const SectionRef &s,
                  int paletteSize, bool preferPacked, QVector<quint32> &out)
{
    out.fill(0, 4096);
    if (paletteSize <= 1)
        return true;
    if (!s.hasStates() || paletteSize <= 0)
        return false;

    const qint64 bytes = qint64(s.dataCount) * 8;
    if (s.dataOff + bytes > bufLen)
        return false;

    Cursor c(data + s.dataOff, bytes);
    QVector<qint64> longs(s.dataCount);
    for (qint32 i = 0; i < s.dataCount && !c.bad; ++i)
        longs[i] = c.i64();
    if (c.bad)
        return false;

    const int bits = std::max(1, ceilLog2(paletteSize));
    const qint64 mask = (bits >= 63) ? ~qint64(0) : ((qint64(1) << bits) - 1);

    QVector<int> widths;
    widths << bits;
    if (bits < 4) widths.prepend(4); // 旧版本位宽下限 4

    // 抽样步长（质数，避免与常见打包周期共振）：先用少量格子淘汰错误
    // 位宽/打包方式，再全量解码——错误方案几乎必然很快撞到越界索引
    constexpr int kSampleStride = 19;

    const auto tryPacked = [&](int w) -> bool {
        const int per = 64 / w;
        if (per <= 0)
            return false;
        const qint64 need = (qint64(4096) + per - 1) / per;
        if (need > longs.size())
            return false;
        for (int i = 0; i < 4096; i += kSampleStride)
        {
            const int li = i / per;
            const int off = (i % per) * w;
            if (quint32((longs[li] >> off) & mask) >= quint32(paletteSize))
                return false;
        }
        for (int i = 0; i < 4096; ++i)
        {
            const int li = i / per;
            const int off = (i % per) * w;
            const quint32 v = quint32((longs[li] >> off) & mask);
            if (v >= quint32(paletteSize))
                return false;
            out[i] = v;
        }
        return true;
    };

    const auto trySpanning = [&](int w) -> bool {
        const auto sampleAt = [&](int i) -> quint64 {
            const qint64 bitPos = qint64(i) * w;
            const qint64 li = bitPos >> 6;
            const int off = int(bitPos & 63);
            if (li >= longs.size())
                return quint64(paletteSize); // 越界 → 判失败
            quint64 v = quint64((quint64(longs[li]) >> off) & quint64(mask));
            if (off + w > 64)
            {
                if (li + 1 >= longs.size())
                    return quint64(paletteSize);
                v |= quint64((quint64(longs[li + 1]) << (64 - off)) & quint64(mask));
            }
            return v;
        };
        for (int i = 0; i < 4096; i += kSampleStride)
            if (sampleAt(i) >= quint64(paletteSize))
                return false;
        for (int i = 0; i < 4096; ++i)
        {
            const qint64 bitPos = qint64(i) * w;
            const qint64 li = bitPos >> 6;
            const int off = int(bitPos & 63);
            if (li >= longs.size())
                return false;
            quint64 v = quint64((quint64(longs[li]) >> off) & quint64(mask));
            if (off + w > 64)
            {
                if (li + 1 >= longs.size())
                    return false;
                v |= quint64((quint64(longs[li + 1]) << (64 - off)) & quint64(mask));
            }
            if (v >= quint64(paletteSize))
                return false;
            out[i] = quint32(v);
        }
        return true;
    };

    const int order[2] = { preferPacked ? 0 : 1, preferPacked ? 1 : 0 };
    for (int mode = 0; mode < 2; ++mode)
    {
        for (int w : widths)
        {
            out.fill(0, 4096);
            const bool ok = (order[mode] == 0) ? tryPacked(w) : trySpanning(w);
            if (ok)
                return true;
        }
    }
    return false;
}

/** 旧格式（1.12-）方块 ID → 颜色 */
QColor legacyBlockColor(int id)
{
    switch (id)
    {
    case 0:  return QColor(0, 0, 0, 0);        // 空气
    case 1:  return QColor("#7F7F7F");         // 石头
    case 2:  return QColor("#79A54D");         // 草方块
    case 3:  return QColor("#8B6A47");         // 泥土
    case 4:  return QColor("#7F7F7F");         // 圆石
    case 5:  return QColor("#9C7F4E");         // 木板
    case 7:  return QColor("#4C4C4C");         // 基岩
    case 8:  case 9:  return QColor("#3F76E4");// 水
    case 10: case 11: return QColor("#E06A12");// 岩浆
    case 12: return QColor("#DBD3A0");         // 沙子
    case 13: return QColor("#9D978C");         // 沙砾
    case 17: return QColor("#6B5233");         // 原木
    case 18: return QColor("#4E7F32");         // 树叶
    case 31: return QColor("#77BB55");         // 草
    case 56: return QColor("#4AEDD9");         // 钻石矿
    case 73: return QColor("#F01E00");         // 红石矿
    case 79: return QColor("#7DADE0");         // 冰
    case 80: return QColor("#FFFFFF");         // 雪块
    case 82: return QColor("#9FA6B4");         // 粘土
    case 87: case 88: return QColor("#6E3B3B");// 下界岩
    case 89: return QColor("#F5C86A");         // 萤石
    case 121: return QColor("#E4E3A0");        // 末地石
    case 155: return QColor("#E8E4DC");        // 石英块
    default:  break;
    }
    if (id == 35) return QColor("#D9D9D9");    // 羊毛
    if (id >= 159 && id <= 162) return QColor("#985E43"); // 陶瓦/混凝土类
    return QColor("#8A8A8A");
}

// ═══════════════════════ 方块着色 ═══════════════════════

bool isAirName(const QString &stripped)
{
    return stripped == QLatin1String("air")
        || stripped == QLatin1String("cave_air")
        || stripped == QLatin1String("void_air")
        || stripped == QLatin1String("structure_void");
}

bool isWaterName(const QString &stripped)
{
    return stripped == QLatin1String("water")
        || stripped == QLatin1String("flowing_water");
}

/**
 * 地表"可忽略覆盖物"：草丛、花、薄雪、火把、铁轨这类细碎装饰。
 * 扫描地表时穿透它们、取下面的实心地面 —— 否则草地上一朵花就会在地图上
 * 留一个杂色点，密林里也会被草丛搅出噪点。这与 MCA Selector 俯视渲染的
 * "忽略方块"思路一致。
 *
 * 边界：**树叶、原木、栏杆、玻璃板、农作物之外的实心方块一律不忽略**，
 * 树林必须显示成树冠；列表只收"贴地薄层 / 细杆 / 单花"这类遮不住地面的东西。
 */
bool isIgnorableCover(const QString &stripped)
{
    static const QSet<QString> kExact = {
        // 贴地与细碎植被
        QStringLiteral("short_grass"),   QStringLiteral("tall_grass"),
        QStringLiteral("fern"),          QStringLiteral("large_fern"),
        QStringLiteral("dead_bush"),     QStringLiteral("seagrass"),
        QStringLiteral("tall_seagrass"), QStringLiteral("wheat"),
        QStringLiteral("carrots"),       QStringLiteral("potatoes"),
        QStringLiteral("beetroots"),     QStringLiteral("beetroot"),
        QStringLiteral("sugar_cane"),    QStringLiteral("bamboo"),
        QStringLiteral("bamboo_sapling"), QStringLiteral("cocoa"),
        QStringLiteral("melon_stem"),    QStringLiteral("pumpkin_stem"),
        QStringLiteral("attached_melon_stem"),
        QStringLiteral("attached_pumpkin_stem"),
        QStringLiteral("nether_wart"),    QStringLiteral("sprouts"),
        // 薄雪 / 水面浮物
        QStringLiteral("snow"), QStringLiteral("powder_snow"),
        QStringLiteral("lily_pad"),
        // 火把 · 红石线 · 拉杆 · 轨道 · 细线
        QStringLiteral("torch"),            QStringLiteral("wall_torch"),
        QStringLiteral("soul_torch"),       QStringLiteral("soul_wall_torch"),
        QStringLiteral("redstone_torch"),   QStringLiteral("redstone_wall_torch"),
        QStringLiteral("redstone_wire"),    QStringLiteral("lever"),
        QStringLiteral("tripwire"),         QStringLiteral("tripwire_hook"),
        QStringLiteral("string"),           QStringLiteral("rail"),
        QStringLiteral("powered_rail"),     QStringLiteral("detector_rail"),
        QStringLiteral("activator_rail"),   QStringLiteral("cobweb"),
        QStringLiteral("fire"),             QStringLiteral("soul_fire"),
        // 洞穴 / 地下附着物（挡在岩壁前看不见地表）
        QStringLiteral("cave_vines"),       QStringLiteral("cave_vines_plant"),
        QStringLiteral("hanging_roots"),    QStringLiteral("pointed_dripstone"),
        QStringLiteral("small_dripleaf"),   QStringLiteral("big_dripleaf"),
        QStringLiteral("sculk_vein"),       QStringLiteral("twisting_vines"),
        QStringLiteral("nether_sprouts"),
        // 单朵花与两格花
        QStringLiteral("poppy"),            QStringLiteral("dandelion"),
        QStringLiteral("cornflower"),       QStringLiteral("azure_bluet"),
        QStringLiteral("lily_of_the_valley"),
        QStringLiteral("wither_rose"),      QStringLiteral("oxeye_daisy"),
        QStringLiteral("sunflower"),        QStringLiteral("lilac"),
        QStringLiteral("rose_bush"),        QStringLiteral("peony"),
        QStringLiteral("torchflower"),      QStringLiteral("pitcher_plant"),
        QStringLiteral("closed_eyeblossom"),
        QStringLiteral("open_eyeblossom"),
    };

    if (kExact.contains(stripped))
        return true;
    // 按后缀归类的装饰物（压板 / 按钮 / 各类郁金香与兰花 / 树苗）
    if (stripped.endsWith(QLatin1String("_pressure_plate"))
        || stripped.endsWith(QLatin1String("_button"))
        || stripped.endsWith(QLatin1String("_tulip"))
        || stripped.endsWith(QLatin1String("_orchid"))
        || stripped.endsWith(QLatin1String("_daisy"))
        || stripped.contains(QLatin1String("sapling")))
        return true;
    // torchflower / sunflower / flowering_azalea …
    return stripped.contains(QLatin1String("flower"));
}

} // namespace

QColor blockColor(const QString &blockName)
{
    QString n = blockName;
    const int idx = n.indexOf(QLatin1Char(':'));
    if (idx >= 0)
        n = n.mid(idx + 1);
    n = n.toLower();

    static const QHash<QString, QColor> kTable = {
        // 地表常见方块
        { QStringLiteral("grass_block"),     QColor("#79A54D") },
        { QStringLiteral("dirt"),            QColor("#8B6A47") },
        { QStringLiteral("coarse_dirt"),     QColor("#7A5A3C") },
        { QStringLiteral("rooted_dirt"),     QColor("#8B6A47") },
        { QStringLiteral("podzol"),          QColor("#6B4F2A") },
        { QStringLiteral("mycelium"),        QColor("#75657B") },
        { QStringLiteral("stone"),           QColor("#7F7F7F") },
        { QStringLiteral("andesite"),        QColor("#8C8C8C") },
        { QStringLiteral("diorite"),         QColor("#DADADA") },
        { QStringLiteral("granite"),         QColor("#9B6B5C") },
        { QStringLiteral("deepslate"),       QColor("#4F4F52") },
        { QStringLiteral("tuff"),            QColor("#6C6C62") },
        { QStringLiteral("calcite"),         QColor("#DFDFDF") },
        { QStringLiteral("cobblestone"),     QColor("#7A7A7A") },
        { QStringLiteral("mossy_cobblestone"), QColor("#6E7A5E") },
        { QStringLiteral("bedrock"),         QColor("#4C4C4C") },
        { QStringLiteral("gravel"),          QColor("#837B73") },
        { QStringLiteral("clay"),            QColor("#9FA6B4") },
        { QStringLiteral("sand"),            QColor("#DBD3A0") },
        { QStringLiteral("red_sand"),        QColor("#BA6E28") },
        { QStringLiteral("sandstone"),       QColor("#DBD3A0") },
        { QStringLiteral("red_sandstone"),   QColor("#C0793C") },
        { QStringLiteral("snow"),            QColor("#FFFFFF") },
        { QStringLiteral("snow_block"),      QColor("#FFFFFF") },
        { QStringLiteral("powder_snow"),     QColor("#FFFFFF") },
        { QStringLiteral("ice"),             QColor("#7DADE0") },
        { QStringLiteral("packed_ice"),      QColor("#A0C8F0") },
        { QStringLiteral("blue_ice"),        QColor("#6A8FE0") },
        { QStringLiteral("water"),           QColor("#3F76E4") },
        { QStringLiteral("lava"),            QColor("#E06A12") },
        { QStringLiteral("magma_block"),     QColor("#C05416") },
        { QStringLiteral("farmland"),        QColor("#6B4A2E") },
        { QStringLiteral("dirt_path"),       QColor("#9A8352") },
        { QStringLiteral("grass_path"),      QColor("#9A8352") },
        { QStringLiteral("moss_block"),      QColor("#5B7C2E") },
        { QStringLiteral("moss_carpet"),     QColor("#5B7C2E") },
        { QStringLiteral("hay_block"),       QColor("#B3A12C") },
        { QStringLiteral("dried_kelp_block"), QColor("#4E5A2E") },
        { QStringLiteral("sponge"),          QColor("#C6C64C") },
        { QStringLiteral("cobweb"),          QColor("#E8E8E8") },
        { QStringLiteral("short_grass"),     QColor("#77BB55") },
        { QStringLiteral("tall_grass"),      QColor("#77BB55") },
        { QStringLiteral("fern"),            QColor("#6BA848") },
        { QStringLiteral("seagrass"),        QColor("#4C9E5A") },
        { QStringLiteral("kelp"),            QColor("#3E7A45") },
        { QStringLiteral("lily_pad"),        QColor("#4C8B3A") },
        { QStringLiteral("cactus"),          QColor("#5E8F3C") },
        { QStringLiteral("sugar_cane"),      QColor("#8FCF6A") },
        { QStringLiteral("bamboo"),          QColor("#7FAF48") },
        { QStringLiteral("vine"),            QColor("#4E7F32") },
        // 矿石
        { QStringLiteral("coal_ore"),        QColor("#5A5A5A") },
        { QStringLiteral("iron_ore"),        QColor("#C8A18A") },
        { QStringLiteral("copper_ore"),      QColor("#D77E4A") },
        { QStringLiteral("gold_ore"),        QColor("#D8B93C") },
        { QStringLiteral("redstone_ore"),    QColor("#B03028") },
        { QStringLiteral("lapis_ore"),       QColor("#3C64B4") },
        { QStringLiteral("diamond_ore"),     QColor("#5AC8C0") },
        { QStringLiteral("emerald_ore"),     QColor("#3CB05A") },
        { QStringLiteral("deepslate_diamond_ore"), QColor("#4AA9A2") },
        // 木质 / 叶子
        { QStringLiteral("oak_leaves"),      QColor("#4E7F32") },
        { QStringLiteral("spruce_leaves"),   QColor("#3E6B3E") },
        { QStringLiteral("birch_leaves"),    QColor("#6F9A45") },
        { QStringLiteral("jungle_leaves"),   QColor("#3F8A2E") },
        { QStringLiteral("acacia_leaves"),   QColor("#5E8A35") },
        { QStringLiteral("dark_oak_leaves"), QColor("#3A6425") },
        { QStringLiteral("mangrove_leaves"), QColor("#3F7A38") },
        { QStringLiteral("cherry_leaves"),   QColor("#E8AFC8") },
        { QStringLiteral("azalea_leaves"),   QColor("#59833A") },
        { QStringLiteral("nether_wart_block"), QColor("#7A1F1F") },
        { QStringLiteral("warped_wart_block"), QColor("#1E7A7A") },
        // 下界
        { QStringLiteral("netherrack"),      QColor("#6E3B3B") },
        { QStringLiteral("nether_bricks"),   QColor("#4C1E1E") },
        { QStringLiteral("soul_sand"),       QColor("#5B4635") },
        { QStringLiteral("soul_soil"),       QColor("#54402F") },
        { QStringLiteral("glowstone"),       QColor("#F5C86A") },
        { QStringLiteral("nether_quartz_ore"), QColor("#9C8C84") },
        { QStringLiteral("basalt"),          QColor("#4A4A4F") },
        { QStringLiteral("blackstone"),      QColor("#3B3436") },
        { QStringLiteral("crimson_nylium"),  QColor("#8A2E2E") },
        { QStringLiteral("warped_nylium"),   QColor("#2E6E6E") },
        // 末地
        { QStringLiteral("end_stone"),       QColor("#E4E3A0") },
        { QStringLiteral("purpur_block"),    QColor("#A5789C") },
        // 建筑常见
        { QStringLiteral("bricks"),          QColor("#96554A") },
        { QStringLiteral("stone_bricks"),    QColor("#7F7F7F") },
        { QStringLiteral("mossy_stone_bricks"), QColor("#6E7A5E") },
        { QStringLiteral("obsidian"),        QColor("#1B1329") },
        { QStringLiteral("terracotta"),      QColor("#985E43") },
        { QStringLiteral("white_wool"),      QColor("#EFEFEF") },
        { QStringLiteral("glass"),           QColor("#D5E8F0") },
        { QStringLiteral("glow_lichen"),     QColor("#7FA0A0") },
    };

    const auto it = kTable.constFind(n);
    if (it != kTable.constEnd())
        return *it;

    // 关键字兜底：调色板里大量装饰方块不逐个列举
    if (n.contains(QLatin1String("leaves")))       return QColor("#4E7F32");
    if (n.endsWith(QLatin1String("_log"))
     || n.endsWith(QLatin1String("_wood"))
     || n.contains(QLatin1String("stem")))         return QColor("#6B5233");
    if (n.contains(QLatin1String("planks")))       return QColor("#9C7F4E");
    if (n.contains(QLatin1String("wool")))         return QColor("#D9D9D9");
    if (n.contains(QLatin1String("carpet")))       return QColor("#C8C8C8");
    if (n.contains(QLatin1String("concrete")))     return QColor("#A0A0A0");
    if (n.contains(QLatin1String("terracotta")))   return QColor("#985E43");
    if (n.contains(QLatin1String("sand")))         return QColor("#DBD3A0");
    if (n.contains(QLatin1String("snow"))
     || n.contains(QLatin1String("ice")))          return QColor("#E4F0FF");
    if (n.contains(QLatin1String("water")))        return QColor("#3F76E4");
    if (n.contains(QLatin1String("lava")))         return QColor("#E06A12");
    if (n.contains(QLatin1String("grass")))        return QColor("#77BB55");
    if (n.contains(QLatin1String("coral")))        return QColor("#C96C8A");
    if (n.contains(QLatin1String("mushroom")))     return QColor("#9B6B4E");
    if (n.contains(QLatin1String("stone"))
     || n.contains(QLatin1String("brick"))
     || n.contains(QLatin1String("ore")))          return QColor("#7F7F7F");
    if (n.contains(QLatin1String("dirt")))         return QColor("#8B6A47");
    return QColor("#8A8A8A");
}

namespace {

/** 去掉 minecraft: 前缀并转小写（配色与空气判断共用） */
QString strippedName(const QString &raw)
{
    const int idx = raw.indexOf(QLatin1Char(':'));
    return (idx >= 0 ? raw.mid(idx + 1) : raw).toLower();
}

/** 高度明暗系数（0.72 ~ 1.16），让地表起伏可读 */
qreal heightFactor(int y, int minY, int maxY)
{
    if (maxY <= minY)
        return 1.0;
    const qreal t = std::clamp((qreal(y) - minY) / qreal(maxY - minY), 0.0, 1.0);
    return 0.72 + 0.44 * t;
}

/** 水体按深度调暗：浅滩亮、深潭沉，河海层次立刻出来 */
QColor deepenWater(QColor c, int depth)
{
    const qreal k = 1.0 - std::min(depth, 20) * 0.027;
    return QColor(std::max(0, int(c.red() * k)),
                  std::max(0, int(c.green() * k)),
                  std::max(0, int(c.blue() * k)));
}

/** 同 deepenWater，但走 QRgb——地表扫描热路径用（免 QColor 构造） */
QRgb deepenRgb(QRgb c, int depth)
{
    const qreal k = 1.0 - std::min(depth, 20) * 0.027;
    return qRgba(std::max(0, int(qRed(c) * k)),
                 std::max(0, int(qGreen(c) * k)),
                 std::max(0, int(qBlue(c) * k)), 255);
}

/** 调色板预计算标志（每项只解析一次字符串，扫地表时纯整数比较） */
enum PalFlag : quint8
{
    PalAir   = 1 << 0,  ///< 空气：穿透
    PalWater = 1 << 1,  ///< 水：参与水深调暗
    PalCover = 1 << 2,  ///< 可忽略覆盖物（草丛/花/火把…）：穿透
};

struct PalEntry
{
    QRgb color = 0;     ///< 地表基础色（alpha=255）
    quint8 flags = 0;   ///< PalFlag 组合
};

/** 按方块名预计算一项调色板条目 */
PalEntry makePalEntry(const QString &raw)
{
    const QString name = strippedName(raw);
    const QColor c = blockColor(raw);
    PalEntry e;
    e.color = qRgba(c.red(), c.green(), c.blue(), 255);
    quint8 f = 0;
    if (isAirName(name)) f |= PalAir;
    if (isIgnorableCover(name)) f |= PalCover;
    if (isWaterName(name)) f |= PalWater;
    e.flags = f;
    return e;
}

/** 该像素无地表（区块未生成 / 全空气） */
constexpr qint16 kNoHeight = std::numeric_limits<qint16>::min();

/**
 * 区域收尾着色：基础色 + 高度 → 最终像素。
 *
 * BlockColor / Height 叠两层明暗——① 高度明暗（0.72~1.16）；② 坡度阴影
 * （与右/下邻居的高差，山脊受光、背光坡压暗）。坡度只在区域内部计算，
 * 边缘邻居缺失时用自身高度兜底，因此跨区域不会出现接缝。
 *
 * Terrain 走地形图：海拔分层设色 + 西北定向山体阴影；water 标出的水面
 * 像素保持水色、不叠阴影（海面保持干净的平色）。
 */
void shadeRegion(const QVector<QRgb> &baseColors, const QVector<qint16> &heights,
                 ShadeMode mode, int minY, int maxY, QImage &img,
                 const QVector<quint8> *water)
{
    constexpr qreal kSlope = 0.010;
    for (int r = 0; r < 512; ++r)
    {
        QRgb *dst = reinterpret_cast<QRgb *>(img.scanLine(r));
        for (int c = 0; c < 512; ++c)
        {
            const int i = r * 512 + c;
            if (heights[i] == kNoHeight)
                continue;

            const qint16 h = heights[i];

            if (mode == ShadeMode::Terrain)
            {
                // 中心差分梯度（缺邻居用自身高度兜底，与坡度阴影同一约定）
                const qreal hl = (c > 0 && heights[i - 1] != kNoHeight)
                    ? qreal(heights[i - 1]) : qreal(h);
                const qreal hrr = (c + 1 < 512 && heights[i + 1] != kNoHeight)
                    ? qreal(heights[i + 1]) : qreal(h);
                const qreal hu = (r > 0 && heights[i - 512] != kNoHeight)
                    ? qreal(heights[i - 512]) : qreal(h);
                const qreal hd = (r + 1 < 512 && heights[i + 512] != kNoHeight)
                    ? qreal(heights[i + 512]) : qreal(h);
                const qreal hs = terrainHillshade((hrr - hl) * 0.5,
                                                  (hd - hu) * 0.5);

                const QRgb base = baseColors[i];
                if (water && water->at(i))
                {
                    dst[c] = base; // 水面平色：不叠阴影
                    continue;
                }
                const QRgb col = terrainKeepsBase(base)
                    ? base : terrainTint(h, minY, maxY);
                dst[c] = qRgba(std::clamp(int(qRed(col) * hs), 0, 255),
                               std::clamp(int(qGreen(col) * hs), 0, 255),
                               std::clamp(int(qBlue(col) * hs), 0, 255), 255);
                continue;
            }

            qint16 hr = h, hd = h;
            if (c + 1 < 512 && heights[i + 1] != kNoHeight)
                hr = heights[i + 1];
            if (r + 1 < 512 && heights[i + 512] != kNoHeight)
                hd = heights[i + 512];
            const qreal slope = std::clamp(
                1.0 + kSlope * ((qreal(h) - hr) + (qreal(h) - hd)), 0.55, 1.45);

            if (mode == ShadeMode::Height)
            {
                const qreal t = std::clamp(
                    (qreal(h) - minY) / qreal(std::max(1, maxY - minY)), 0.0, 1.0);
                const int v = std::clamp(int((56 + 180 * t) * slope), 0, 255);
                dst[c] = qRgba(v, v, v, 255);
            }
            else
            {
                const QRgb b = baseColors[i];
                const qreal f = heightFactor(h, minY, maxY) * slope;
                dst[c] = qRgba(std::clamp(int(qRed(b) * f), 0, 255),
                               std::clamp(int(qGreen(b) * f), 0, 255),
                               std::clamp(int(qBlue(b) * f), 0, 255), 255);
            }
        }
    }
}

/**
 * 把一个区块的地表提取到区域缓冲（基础色 + 高度），明暗由 shadeRegion 统一做。
 * @param originCol 图像列（= 区块内偏移 0）
 * @param originRow 图像行
 * @param water     地形模式的水面标记缓冲（可空）：该像素地表是水 → 1
 * @return 256 列是否全部解析出地表（false = 该区块有空缺，上层据此判断补种子图）
 */
bool renderChunk(const ChunkInfo &info, const uchar *data, qint64 bufLen,
                 int originCol, int originRow,
                 QVector<QRgb> &baseColors, QVector<qint16> &heights,
                 int maxSurfaceY, QVector<quint8> *water)
{
    if (info.sections.isEmpty())
        return false;

    QVector<SectionRef> secs = info.sections;
    std::sort(secs.begin(), secs.end(),
              [](const SectionRef &a, const SectionRef &b) { return a.y > b.y; });

    const bool preferPacked = info.dataVersion >= 2566; // 1.16+

    std::array<bool, 256> resolved{};
    int remaining = 256;
    QVector<QRgb> colors(256, 0);          // 地表基础色（方块色 + 水深，未做明暗）
    QVector<qint16> colHeights(256, kNoHeight);
    std::array<bool, 256> colWater{};      // 该列地表是否为水面（地形模式用）

    for (const SectionRef &s : secs)
    {
        if (remaining == 0)
            break;

        QStringList palette;
        QVector<quint32> indices;

        if (s.hasLegacy())
        {
            // 1.12 及更早：4096 字节的数字方块 ID
            const qint64 bytes = s.blocksCount;
            if (s.blocksOff + bytes > bufLen)
                continue;
            Cursor c(data + s.blocksOff, bytes);
            indices.resize(4096);
            for (int i = 0; i < 4096 && !c.bad; ++i)
                indices[i] = quint32(c.u8());
            if (c.bad)
                continue;
            if (s.addOff >= 0 && s.addCount > 0 && s.addOff + s.addCount <= bufLen)
            {
                Cursor add(data + s.addOff, s.addCount);
                for (int i = 0; i < 2048 && !add.bad; ++i)
                {
                    const int b = add.u8();
                    const quint32 hi = quint32(b & 0x0F) << 8;
                    const quint32 lo = quint32(b >> 4);
                    if (i * 2 < 4096)     indices[i * 2]     |= lo << 8;
                    if (i * 2 + 1 < 4096) indices[i * 2 + 1] |= hi;
                }
            }
        }
        else
        {
            palette = readPalette(data, bufLen, s);
            if (palette.isEmpty())
            {
                qWarning() << "[WorldMap] palette empty, sec y=" << s.y
                           << "off=" << s.paletteOff << "cnt=" << s.paletteCount;
                continue;
            }
            // 纯空气层（地表之上的空 section）：整层跳过，省掉 4096 项解码
            if (palette.size() == 1 && isAirName(strippedName(palette.first())))
                continue;
            if (!decodeStates(data, bufLen, s, palette.size(), preferPacked, indices))
            {
                qWarning() << "[WorldMap] decode failed, sec y=" << s.y
                           << "palette=" << palette.size() << "data=" << s.dataCount;
                continue;
            }
        }

        const int paletteSize = palette.size();

        // 调色板 → 预计算条目：每个 section 只解析一次字符串/哈希，
        // 下面 4096 格的内层扫描全部退化成整数比较（地表渲染的主要热点）
        QVector<PalEntry> lut(paletteSize);
        if (paletteSize > 0)
        {
            if (s.hasLegacy())
            {
                // 旧格式数字 ID 走固定色表，flags 按 ID 判定
                for (int p = 0; p < paletteSize; ++p)
                {
                    const int id = p;
                    const QColor c = legacyBlockColor(id);
                    quint8 f = 0;
                    if (id == 0) f |= PalAir;
                    if (id == 8 || id == 9) f |= PalWater;
                    lut[p].color = qRgba(c.red(), c.green(), c.blue(), 255);
                    lut[p].flags = f;
                }
            }
            else
            {
                for (int p = 0; p < paletteSize; ++p)
                    lut[p] = makePalEntry(palette.at(p));
            }
        }

        for (int col = 0; col < 256 && remaining > 0; ++col)
        {
            if (resolved[uint(col)])
                continue;
            for (int yy = 15; yy >= 0; --yy)
            {
                // 自定义高度：削掉高于设定 Y 的方块（树冠 / 屋顶…），
                // 只取该高度及以下的地表
                if (maxSurfaceY >= 0 && s.y * 16 + yy > maxSurfaceY)
                    continue;

                const quint32 idx = indices[yy * 256 + col];
                if (paletteSize > 0)
                {
                    if (idx >= quint32(paletteSize))
                        break;
                    const PalEntry &e = lut[int(idx)];
                    // 穿透空气与可忽略覆盖物（草丛/花/薄雪/火把…），取真正的地面
                    if (e.flags & (PalAir | PalCover))
                        continue;

                    QRgb c = e.color;
                    if (e.flags & PalWater)
                    {
                        colWater[uint(col)] = true;
                        // 数水层厚度：浅滩亮、深潭沉
                        int depth = 1;
                        for (int y2 = yy - 1; y2 >= 0; --y2)
                        {
                            const quint32 i2 =
                                indices[quint32(y2) * 256 + quint32(col)];
                            if (i2 >= quint32(paletteSize)
                                || !(lut[int(i2)].flags & PalWater))
                                break;
                            if (++depth >= 24)
                                break;
                        }
                        c = deepenRgb(c, depth);
                    }
                    colors[uint(col)] = c;
                }
                else
                {
                    // 旧格式数字 ID（8 / 9 = 水）
                    const int id = int(idx);
                    if (id == 0)
                        continue;
                    QColor c = legacyBlockColor(id);
                    if (id == 8 || id == 9)
                    {
                        colWater[uint(col)] = true;
                        int depth = 1;
                        for (int y2 = yy - 1; y2 >= 0; --y2)
                        {
                            const int id2 =
                                int(indices[quint32(y2) * 256 + quint32(col)]);
                            if (id2 != 8 && id2 != 9)
                                break;
                            if (++depth >= 24)
                                break;
                        }
                        c = deepenWater(c, depth);
                    }
                    colors[uint(col)] = qRgba(c.red(), c.green(), c.blue(), 255);
                }
                colHeights[uint(col)] = qint16(s.y * 16 + yy);
                resolved[uint(col)] = true;
                --remaining;
                break;
            }
        }
    }

    for (int col = 0; col < 256; ++col)
    {
        if (!resolved[uint(col)])
            continue;
        const int lx = col & 15;
        const int lz = col >> 4;
        const int i = (originRow + lz) * 512 + (originCol + lx);
        baseColors[uint(i)] = colors[uint(col)];
        heights[uint(i)] = colHeights[uint(col)];
        if (water && colWater[uint(col)])
            water->operator[](uint(i)) = 1;
    }
    return remaining == 0;
}

} // namespace

// ═══════════════════ 地形模式共用（ShadeMode::Terrain） ═══════════════════

QRgb terrainTint(int y, int minY, int maxY)
{
    // 分层设色色带：低地绿 → 海平面亮绿 → 黄绿 → 黄 → 棕 → 灰 → 雪顶白
    static const QRgb kStops[] = {
        qRgb(0x3E, 0x7C, 0x4F), // 0.00 深绿（最低）
        qRgb(0x6B, 0xAF, 0x5F), // 0.33 主世界海平面附近亮绿
        qRgb(0xA8, 0xC4, 0x6A), // 0.48 黄绿（丘陵）
        qRgb(0xD9, 0xC1, 0x72), // 0.60 黄（高原）
        qRgb(0xC9, 0x9A, 0x5E), // 0.72 棕黄（山地）
        qRgb(0xA8, 0x75, 0x52), // 0.84 棕（高山）
        qRgb(0x9C, 0x8F, 0x86), // 0.93 灰（岩峰）
        qRgb(0xF7, 0xF7, 0xF5), // 1.00 白（雪顶）
    };
    constexpr int kSegs = int(sizeof(kStops) / sizeof(kStops[0])) - 1;
    qreal t = (maxY > minY) ? (qreal(y) - minY) / qreal(maxY - minY) : 0.5;
    t = std::clamp(t, 0.0, 1.0);
    const qreal pos = t * kSegs;
    const int s = std::min(int(pos), kSegs - 1);
    const qreal f = pos - s;
    const QRgb a = kStops[s];
    const QRgb b = kStops[s + 1];
    return qRgba(int(qRed(a) + (qRed(b) - qRed(a)) * f),
                 int(qGreen(a) + (qGreen(b) - qGreen(a)) * f),
                 int(qBlue(a) + (qBlue(b) - qBlue(a)) * f), 255);
}

qreal terrainHillshade(qreal dzDx, qreal dzDy)
{
    // 光源：方位西北、仰角 45° → L = (-0.5, -0.5, √2/2)，|L| = 1
    constexpr qreal kLz = 0.7071067811865476;
    // 法线 N = (-dzDx, -dzDy, 1)，Lambert 照度归一化到"平地 = 1"
    const qreal len = std::sqrt(dzDx * dzDx + dzDy * dzDy + 1.0);
    const qreal dotNL = (dzDx * 0.5 + dzDy * 0.5 + kLz) / len;
    return std::clamp(dotNL / kLz, 0.35, 1.55);
}

bool terrainKeepsBase(QRgb c)
{
    // 雪 / 冰 / 白色方块保持本色：雪顶在任何海拔都显示为白
    return qRed(c) >= 200 && qGreen(c) >= 200 && qBlue(c) >= 200;
}

// ═══════════════════════ 区域文件 ═══════════════════════

namespace {

quint32 be32(const uchar *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16)
         | (quint32(p[2]) << 8)  |  quint32(p[3]);
}

/** 维度纵坐标范围（用于高度着色） */
void dimensionHeightRange(Dimension dim, int *minY, int *maxY)
{
    switch (dim)
    {
    case Dimension::Nether: *minY = 0;   *maxY = 128; break;
    case Dimension::End:    *minY = 0;   *maxY = 256; break;
    default:                *minY = -64; *maxY = 320; break;
    }
}

/**
 * 用种子预测地形垫住真实图里的透明洞（半生成 region）。
 * 真实像素画在上层、种子只填空缺 —— 已加载与未加载之间不再露出空缺。
 * @param seedRes 种子渲染边长：细节瓦片传 512（方块级），概览传 512/bpp。
 */
QImage seedUnderlay(QImage &real, int regionX, int regionZ, Dimension dim,
                    ShadeMode mode, const SeedPreview &seed, int seedRes)
{
    if (!seed.enabled || !SeedTerrainRenderer::supported(seed.mc, dim))
        return real;
    QImage under = SeedTerrainRenderer::renderRegionTile(regionX, regionZ, dim,
                                                         mode, seed.seed,
                                                         seed.mc, seedRes);
    if (under.isNull())
        return real;
    if (under.size() != real.size())
        under = under.scaled(real.size(), Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    QPainter p(&under);
    p.drawImage(0, 0, real);
    p.end();
    return under;
}

/**
 * region 文件数据：优先内存映射（省一次整文件拷贝，大区域文件收益明显），
 * 映射失败或平台不支持时回退成一次性读入内存。
 */
struct RegionData
{
    QFile file;
    QByteArray owned;
    uchar *mapped = nullptr;
    const uchar *data = nullptr;
    qint64 size = 0;

    ~RegionData()
    {
        if (mapped)
            file.unmap(mapped);
    }

    bool load(const QString &path)
    {
        file.setFileName(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        size = file.size();
        if (size <= 0)
            return false;
        mapped = file.map(0, size);
        if (mapped)
        {
            data = mapped;
            return true;
        }
        owned = file.readAll();
        data = reinterpret_cast<const uchar *>(owned.constData());
        size = owned.size();
        return data != nullptr && size > 0;
    }
};

/**
 * 渲染单个 region 文件为 512×512 图块（1 像素 = 1 方块）。
 * 解析思路参考 MCA Selector：文件头固定 32KB（位置表 4096×4B + 时间戳表 4096×4B），
 * 数据从第 8 扇区开始，每个区块按 (offset << 12) 定位，长度字段含 1 字节压缩类型。
 * @param hasHoles 非空时回传"图里是否有透明空缺"（半生成 region），省掉整图 alpha 扫描
 */
QImage renderRegionFile(const QString &path, ShadeMode mode,
                        int minY, int maxY, int *failedChunks, int maxSurfaceY,
                        bool *hasHoles = nullptr)
{
    QImage img;
    if (hasHoles)
        *hasHoles = false;

    // Anvil 头部固定 8KB：位置表 1024×4B + 时间戳表 1024×4B（一个 region 是 32×32 = 1024 个区块）。
    // 空 .mca（游戏运行中会创建 0 字节文件）与被截断的文件直接跳过。
    // 注意位置表只有 1024 项：按 4096 项读会读进时间戳表与数据区，
    // 把时间戳当偏移 → 大量假跳过，还会把伪造槽位画上去盖掉真实像素。
    constexpr qint64 kHeaderSize = 8192;

    RegionData region;
    if (!region.load(path))
        return img;
    const uchar *data = region.data;
    const qint64 size = region.size;
    if (size <= 0)
        return img; // 空文件：游戏运行中很常见，静默跳过
    if (size < kHeaderSize)
    {
        if (failedChunks)
            ++*failedChunks; // 截断文件
        return img;
    }

    // 区域缓冲：基础色 + 每像素地表高度（kNoHeight = 该处没有地表）
    QVector<QRgb> baseColors(512 * 512, 0);
    QVector<qint16> heights(512 * 512, kNoHeight);
    // 地形模式的水面标记：水面保持水色、不叠山体阴影（其他模式不分配）
    QVector<quint8> water;
    if (mode == ShadeMode::Terrain)
        water.resize(512 * 512);

    bool holes = false;

    // 位置表只有 1024 项（32×32）；按 4096 项读会读进时间戳表与数据区，
    // 把时间戳当成偏移导致大量假跳过、还会把伪造槽位画上去盖掉真实像素。
    for (int i = 0; i < 1024; ++i)
    {
        const quint32 off = (quint32(data[i * 4]) << 16)
                          | (quint32(data[i * 4 + 1]) << 8)
                          |  quint32(data[i * 4 + 2]);
        if (off == 0)
        {
            holes = true; // 该区块槽位没有数据 → 图上是洞
            continue;
        }

        const qint64 pos = qint64(off) * 4096;
        if (pos < kHeaderSize || pos + 5 > size)
        {
            holes = true;
            continue;
        }

        const quint32 len = be32(data + pos);
        if (len <= 1 || pos + 4 + qint64(len) > size)
        {
            holes = true;
            continue;
        }

        const int compType = int(data[pos + 4]);
        QByteArray chunk;
        if (!inflatePayload(data + pos + 5, int(len - 1), compType, chunk))
        {
            qWarning() << "[WorldMap] inflate failed" << path << i << compType << int(len);
            if (failedChunks) ++*failedChunks;
            holes = true;
            continue;
        }

        const uchar *cdata = reinterpret_cast<const uchar *>(chunk.constData());
        const ChunkInfo info = parseChunk(cdata, chunk.size());
        if (!info.ok)
        {
            qWarning() << "[WorldMap] parse failed" << path << i << chunk.size();
            if (failedChunks) ++*failedChunks;
            holes = true;
            continue;
        }
        if (info.sections.isEmpty())
            qWarning() << "[WorldMap] no sections" << path << i;

        // 位置表索引 i 就是该区块在 region 内的权威槽位（x = i & 31, z = i >> 5）。
        // 早先优先取 NBT 的 xPos/zPos，遇到重复或错位坐标会把别的槽位盖掉。
        const int localX = i & 31;
        const int localZ = i >> 5;
        if (!renderChunk(info, cdata, chunk.size(), localX * 16, localZ * 16,
                         baseColors, heights, maxSurfaceY,
                         water.isEmpty() ? nullptr : &water))
            holes = true; // 区块里有列没解析出地表
    }

    if (hasHoles)
        *hasHoles = holes;

    img = QImage(512, 512, QImage::Format_ARGB32);
    if (img.isNull())
        return img;
    img.fill(qRgba(0, 0, 0, 0));
    // 统一收尾：高度明暗 + 坡度阴影（Height 转灰度再叠坡度；Terrain 走分层设色 + 山体阴影）
    shadeRegion(baseColors, heights, mode, minY, maxY, img,
                water.isEmpty() ? nullptr : &water);

    return img;
}

} // namespace

QString regionDir(const QString &savePath, Dimension dim)
{
    if (savePath.isEmpty())
        return QString();
    switch (dim)
    {
    case Dimension::Nether: return savePath + QStringLiteral("/DIM-1/region");
    case Dimension::End:    return savePath + QStringLiteral("/DIM1/region");
    default:                return savePath + QStringLiteral("/region");
    }
}

bool hasRegions(const QString &savePath, Dimension dim)
{
    const QString dir = regionDir(savePath, dim);
    if (dir.isEmpty())
        return false;
    const QDir d(dir);
    if (!d.exists())
        return false;
    return !d.entryList({ QStringLiteral("*.mca") }, QDir::Files).isEmpty();
}

QImage renderRegionTile(const QString &savePath, Dimension dim,
                        int regionX, int regionZ, ShadeMode mode,
                        int maxSurfaceY, const SeedPreview &seed)
{
    const QString dir = regionDir(savePath, dim);
    QImage img;

    if (!dir.isEmpty())
    {
        const QString path = dir + QStringLiteral("/r.%1.%2.mca")
                                           .arg(regionX).arg(regionZ);
        if (QFileInfo::exists(path))
        {
            int minY = 0, maxY = 255;
            dimensionHeightRange(dim, &minY, &maxY);

            int failedChunks = 0;
            bool holes = false;
            img = renderRegionFile(path, mode, minY, maxY, &failedChunks,
                                   maxSurfaceY, &holes);
            if (!img.isNull() && holes)
                img = seedUnderlay(img, regionX, regionZ, dim, mode, seed, 512);
        }
    }

    // 文件缺失 / 空文件 / 损坏 → 有种子预测参数就补一张近似地形
    if (img.isNull() && seed.enabled
        && SeedTerrainRenderer::supported(seed.mc, dim))
    {
        img = SeedTerrainRenderer::renderRegionTile(regionX, regionZ, dim, mode,
                                                    seed.seed, seed.mc, 512);
    }
    return img;
}

namespace {

/** 扫描出 r.<x>.<z>.mca 对应的区域坐标 */
void scanRegionFiles(const QString &dir, QVector<QPair<int, int>> *coords,
                     QStringList *paths)
{
    static const QRegularExpression re(
        QStringLiteral("^r\\.(-?\\d+)\\.(-?\\d+)\\.mca$"));
    const QDir d(dir);
    const QStringList names = d.entryList({ QStringLiteral("*.mca") },
                                          QDir::Files, QDir::Name);
    for (const QString &name : names)
    {
        const QRegularExpressionMatch m = re.match(name);
        if (!m.hasMatch())
            continue;
        const int rx = m.captured(1).toInt();
        const int rz = m.captured(2).toInt();
        // 合法世界边界约 ±30000000/512 ≈ ±58593；异常坐标多为伪造/损坏文件名
        if (qAbs(rx) > 60000 || qAbs(rz) > 60000)
            continue;
        // 0 字节（游戏运行中很常见）与不足 32KB 文件头的截断残件直接不纳入：
        // 它们本来就渲染不出内容，计进"失败"只会吓用户
        const QString absPath = d.absoluteFilePath(name);
        if (QFileInfo(absPath).size() < 8192)
            continue;
        coords->append(qMakePair(rx, rz));
        paths->append(absPath);
    }
}

/** 单个区域的渲染任务结果：已缩放到最终分辨率、可直接贴图 */
struct TileTaskResult
{
    QImage tile;
    QPoint dest;   ///< 最终图上的像素坐标
    bool ok = false;
    bool isSeed = false; ///< 该区域是种子预测（预览性质，失败不计入 failedRegions）
};

/**
 * 盒式降采样（factor 为 2 的幂，512 能被它整除）。
 * 相比 `QImage::scaled` 的双线性，bpp>2 时它会真正平均到全部源像素而不是只采
 * 几个点，方块轮廓更锐；同时**跳过透明像素**，"未生成区域"的边缘保持实边，
 * 不会出现半透明渐隐带。
 */
QImage boxDownscale(const QImage &src, int factor)
{
    if (src.isNull() || factor <= 1)
        return src;
    const int w = src.width() / factor;
    const int h = src.height() / factor;
    if (w <= 0 || h <= 0)
        return QImage();

    QImage dst(w, h, QImage::Format_ARGB32);
    dst.fill(qRgba(0, 0, 0, 0));
    for (int y = 0; y < h; ++y)
    {
        QRgb *out = reinterpret_cast<QRgb *>(dst.scanLine(y));
        for (int x = 0; x < w; ++x)
        {
            qint64 r = 0, g = 0, b = 0;
            int n = 0;
            for (int dy = 0; dy < factor; ++dy)
            {
                const QRgb *in = reinterpret_cast<const QRgb *>(
                    src.constScanLine(y * factor + dy));
                for (int dx = 0; dx < factor; ++dx)
                {
                    const QRgb p = in[x * factor + dx];
                    if (qAlpha(p) == 0)
                        continue;
                    r += qRed(p);
                    g += qGreen(p);
                    b += qBlue(p);
                    ++n;
                }
            }
            out[x] = n ? qRgba(int(r / n), int(g / n), int(b / n), 255)
                       : qRgba(0, 0, 0, 0);
        }
    }
    return dst;
}

/** 工作线程里跑：解一个 region → 按 blockPerPixel 降采样；无文件时走种子预测 */
TileTaskResult renderRegionTask(const QString &path, int regionX, int regionZ,
                                int destX, int destY, int bpp,
                                ShadeMode mode, int minY, int maxY,
                                int maxSurfaceY, const SeedPreview &seed,
                                Dimension dim)
{
    TileTaskResult r;
    r.dest = QPoint(destX, destY);

    QImage img;
    if (!path.isEmpty())
    {
        int failedChunks = 0;
        bool holes = false;
        img = renderRegionFile(path, mode, minY, maxY, &failedChunks,
                               maxSurfaceY, &holes);
        // 半生成 region：真实图带透明洞，用种子地形垫底补齐接缝，
        // 已加载与未加载之间不再露出一条空缺
        if (!img.isNull() && holes)
            img = seedUnderlay(img, regionX, regionZ, dim, mode, seed,
                               std::clamp(512 / bpp, 4, 128));
    }
    else if (seed.enabled && SeedTerrainRenderer::supported(seed.mc, dim))
    {
        // 无 region 文件（已探索范围的空洞 / 边界外扩）→ 种子预测近似地形。
        // 直接按输出尺寸渲染；概览封顶 128 采样（群系 1:4 粒度已够），
        // 方块级 512 采样只留给放大后的细节瓦片路径
        r.isSeed = true;
        img = SeedTerrainRenderer::renderRegionTile(regionX, regionZ, dim, mode,
                                                    seed.seed, seed.mc,
                                                    std::clamp(512 / bpp, 4, 128));
    }

    if (img.isNull())
        return r;

    r.ok = true;
    if (r.isSeed)
        r.tile = img; // 种子预测已按输出尺寸渲染，无需再降采样
    else
        r.tile = (bpp == 1) ? img : boxDownscale(img, bpp);
    return r;
}

} // namespace

/** 种子预测槽位上限：保证中心窗口（radius 20 → 41×41 = 1681 槽）完整铺开，
 *  超出部分按到视角中心的距离由近及远裁剪 */
constexpr int kMaxSeedSlots = 4096;

RenderResult render(const QString &savePath, Dimension dim, ShadeMode mode,
                    const std::function<bool()> &cancelled,
                    const std::function<void(int, int)> &progress,
                    const ProgressiveSinkPtr &sink, int maxThreads,
                    int maxSurfaceY, const SeedPreview &seed)
{
    QElapsedTimer timer;
    timer.start();
    RenderResult res;

    const QString dir = regionDir(savePath, dim);
    QVector<QPair<int, int>> coords;
    QStringList paths;
    if (!dir.isEmpty() && QDir(dir).exists())
        scanRegionFiles(dir, &coords, &paths);

    const bool seedOn = seed.enabled
        && SeedTerrainRenderer::supported(seed.mc, dim);
    if (coords.isEmpty() && !seedOn)
    {
        res.error = QObject::tr("该维度还没有生成的区块");
        return res;
    }

    // 渐进渲染优先点：出生点 region 没有就退回种子预览中心
    const QPoint priority = (sink && sink->hasPriorityRegion)
        ? sink->priorityRegion
        : (seed.hasCenter ? QPoint(seed.centerRegionX, seed.centerRegionZ)
                          : QPoint(0, 0));

    // ── 种子预测：包围盒 = 已探索范围外扩 pad ∪（中心 ± radius 的方形窗口）——
    //    视角中心向四周约一万格都有预测地形；盒内无文件的槽位交给种子渲染 ──
    if (seedOn)
    {
        QHash<QPoint, QString> fileMap;
        fileMap.reserve(coords.size());
        for (int i = 0; i < coords.size(); ++i)
            fileMap.insert(QPoint(coords[i].first, coords[i].second), paths[i]);

        // 半径窗口的中心：优先用调用方传来的视角中心，没有就退回渐进优先点
        const QPoint seedCenter = seed.hasCenter
            ? QPoint(seed.centerRegionX, seed.centerRegionZ)
            : priority;

        int minRx = 0, maxRx = 0, minRz = 0, maxRz = 0;
        if (!coords.isEmpty())
        {
            minRx = maxRx = coords.first().first;
            minRz = maxRz = coords.first().second;
            for (const auto &c : coords)
            {
                minRx = std::min(minRx, c.first);
                maxRx = std::max(maxRx, c.first);
                minRz = std::min(minRz, c.second);
                maxRz = std::max(maxRz, c.second);
            }
            minRx -= seed.padRegions; maxRx += seed.padRegions;
            minRz -= seed.padRegions; maxRz += seed.padRegions;
        }
        else
        {
            // 该维度还没有任何区块：先按中心占位，下面统一并入半径窗口
            minRx = maxRx = seedCenter.x();
            minRz = maxRz = seedCenter.y();
        }
        // 并入中心 ± radius 的方形窗口（视角中心向外约 radius×512 格）
        minRx = std::min(minRx, seedCenter.x() - seed.radiusRegions);
        maxRx = std::max(maxRx, seedCenter.x() + seed.radiusRegions);
        minRz = std::min(minRz, seedCenter.y() - seed.radiusRegions);
        maxRz = std::max(maxRz, seedCenter.y() + seed.radiusRegions);

        // 槽位由近及远排序（切比雪夫距离），种子槽位超额时裁掉最远的。
        // 包围盒过大（远处孤岛文件把跨度撑大）时不逐格枚举，只枚举
        // 「中心窗口 + 文件槽位」—— 视角附近始终完整，远处真实文件也不丢
        const qint64 bboxCells = qint64(maxRx - minRx + 1) * qint64(maxRz - minRz + 1);
        constexpr qint64 kMaxEnumerateCells = 8192;
        QVector<QPair<int, int>> regionSlots;
        if (bboxCells <= kMaxEnumerateCells)
        {
            regionSlots.reserve(int(bboxCells));
            for (int rz = minRz; rz <= maxRz; ++rz)
                for (int rx = minRx; rx <= maxRx; ++rx)
                    regionSlots.append(qMakePair(rx, rz));
        }
        else
        {
            QSet<QPoint> seen;
            seen.reserve(coords.size() + 4096);
            const int wx0 = seedCenter.x() - seed.radiusRegions;
            const int wx1 = seedCenter.x() + seed.radiusRegions;
            const int wz0 = seedCenter.y() - seed.radiusRegions;
            const int wz1 = seedCenter.y() + seed.radiusRegions;
            for (int rz = wz0; rz <= wz1; ++rz)
                for (int rx = wx0; rx <= wx1; ++rx)
                {
                    regionSlots.append(qMakePair(rx, rz));
                    seen.insert(QPoint(rx, rz));
                }
            for (const auto &c : coords)
                if (!seen.contains(QPoint(c.first, c.second)))
                    regionSlots.append(c);
        }
        std::sort(regionSlots.begin(), regionSlots.end(), [&seedCenter](const auto &a, const auto &b) {
            const int da = std::max(std::abs(a.first - seedCenter.x()),
                                    std::abs(a.second - seedCenter.y()));
            const int db = std::max(std::abs(b.first - seedCenter.x()),
                                    std::abs(b.second - seedCenter.y()));
            if (da != db)
                return da < db;
            if (a.second != b.second)
                return a.second < b.second;
            return a.first < b.first;
        });

        QVector<QPair<int, int>> mergedCoords;
        QStringList mergedPaths;
        mergedCoords.reserve(regionSlots.size());
        mergedPaths.reserve(regionSlots.size());
        int seedSlots = 0;
        for (const auto &s : regionSlots)
        {
            const auto it = fileMap.constFind(QPoint(s.first, s.second));
            if (it != fileMap.constEnd())
            {
                mergedCoords.append(s);
                mergedPaths.append(it.value());
            }
            else if (seedSlots < kMaxSeedSlots)
            {
                ++seedSlots;
                mergedCoords.append(s);
                mergedPaths.append(QString()); // 空路径 = 种子预测
            }
        }
        coords = mergedCoords;
        paths = mergedPaths;
        // mergedCoords 已按到优先点的切比雪夫距离排好序，无需再排
    }
    else
    {
        // 从优先区域（出生点所在 region）往四周一圈圈渲染：切比雪夫距离
        // max(|dx|,|dz|) 正好是方环由内向外的顺序 —— 渐进画面从中心长出来
        if (sink && sink->hasPriorityRegion)
        {
            QVector<int> order(paths.size());
            for (int i = 0; i < order.size(); ++i)
                order[i] = i;
            const QPoint p = sink->priorityRegion;
            std::sort(order.begin(), order.end(), [&coords, p](int a, int b) {
                const int da = std::max(std::abs(coords[a].first - p.x()),
                                        std::abs(coords[a].second - p.y()));
                const int db = std::max(std::abs(coords[b].first - p.x()),
                                        std::abs(coords[b].second - p.y()));
                if (da != db)
                    return da < db;
                return a < b; // 同环内保持稳定，免得每次刷新顺序都变
            });
            QVector<QPair<int, int>> sortedCoords;
            QStringList sortedPaths;
            sortedCoords.reserve(coords.size());
            sortedPaths.reserve(paths.size());
            for (int i : order)
            {
                sortedCoords.append(coords[i]);
                sortedPaths.append(paths[i]);
            }
            coords = sortedCoords;
            paths = sortedPaths;
        }
    }

    // 包围盒统一按最终槽位计算（种子预测分支里可能已裁剪缩小）
    int minRx = coords.first().first, maxRx = minRx;
    int minRz = coords.first().second, maxRz = minRz;
    for (const auto &c : coords)
    {
        minRx = std::min(minRx, c.first);
        maxRx = std::max(maxRx, c.first);
        minRz = std::min(minRz, c.second);
        maxRz = std::max(maxRz, c.second);
    }

    // 世界过大时按 2 的幂降采样，避免主图超过 4096px（≈64MB）。
    // 用 64 位算跨度：region 文件名坐标是 int，直接相减/相乘会溢出
    const qint64 blocksX = (qint64(maxRx) - qint64(minRx) + 1) * 512;
    const qint64 blocksZ = (qint64(maxRz) - qint64(minRz) + 1) * 512;
    int bpp = 1;
    while (blocksX / bpp > 4096 || blocksZ / bpp > 4096)
        bpp *= 2;

    const int imgW = int(blocksX / bpp);
    const int imgH = int(blocksZ / bpp);
    QImage master(imgW, imgH, QImage::Format_ARGB32);
    master.fill(qRgba(0, 0, 0, 0));

    int minY = 0, maxY = 255;
    dimensionHeightRange(dim, &minY, &maxY);

    res.minChunkX = minRx * 32;
    res.minChunkZ = minRz * 32;
    res.blockPerPixel = bpp;
    res.regionCount = paths.size();

    // ① 先发布布局，UI 收到就能建画布开始"边渲染边显示"
    if (sink)
    {
        sink->imageWidth = imgW;
        sink->imageHeight = imgH;
        sink->blockPerPixel = bpp;
        sink->minChunkX = res.minChunkX;
        sink->minChunkZ = res.minChunkZ;
        sink->total.store(paths.size());
        sink->layoutReady.store(true, std::memory_order_release);
    }

    const auto isCancelled = [&]() {
        if (cancelled && cancelled())
            return true;
        return sink && sink->cancel.load();
    };

    // ② 区域之间并行：工作线程自取队列（不再"一批等一批"，慢 region 不拖累同批邻居）。
    //    并行度封顶 16：同时 map/read 大 region 时提交内存会明显上涨，留些给 UI
    int threads = (maxThreads > 0)
        ? maxThreads
        : qMax(1, QThreadPool::globalInstance()->maxThreadCount());
    threads = qBound(1, threads, qMin(paths.size(), 16));

    QThreadPool pool;
    pool.setMaxThreadCount(threads);

    const int total = paths.size();
    std::atomic<int> nextIdx{ 0 };
    std::atomic<int> doneCount{ 0 };
    std::atomic<int> failedCount{ 0 };
    std::atomic<int> renderedCount{ 0 };
    std::atomic<int> seedCount{ 0 };
    QMutex blitMutex;

    // 主图写入：QPainter Source 合成（与 sink 贴图一致），互斥保证只一线程画
    const auto blitTile = [&](const TileTaskResult &task) {
        const int tw = task.tile.width();
        const int th = task.tile.height();
        if (tw <= 0 || th <= 0)
            return;
        if (task.dest.x() < 0 || task.dest.y() < 0
            || task.dest.x() + tw > imgW || task.dest.y() + th > imgH)
            return;
        QMutexLocker locker(&blitMutex);
        QPainter painter(&master);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(task.dest, task.tile);
        painter.end();
    };

    auto worker = [&]() {
        while (!isCancelled())
        {
            const int i = nextIdx.fetch_add(1, std::memory_order_relaxed);
            if (i >= total)
                break;

            const QString path = paths[i];
            const int regionX = coords[i].first;
            const int regionZ = coords[i].second;
            const int destX = (regionX - minRx) * 512 / bpp;
            const int destY = (regionZ - minRz) * 512 / bpp;

            TileTaskResult task = renderRegionTask(path, regionX, regionZ,
                                                   destX, destY, bpp, mode,
                                                   minY, maxY, maxSurfaceY,
                                                   seed, dim);
            if (sink && task.ok)
            {
                // 一解完就入队，UI 定时 drain 就能看到新区域冒出来
                QMutexLocker locker(&sink->tileMutex);
                sink->pendingTiles.append(qMakePair(task.dest, task.tile));
            }
            if (!task.ok)
            {
                // 种子预测失败只是预览缺失，不算文件读取失败
                if (!task.isSeed)
                    failedCount.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                blitTile(task);
                renderedCount.fetch_add(1, std::memory_order_relaxed);
                if (task.isSeed)
                    seedCount.fetch_add(1, std::memory_order_relaxed);
            }

            const int d = doneCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (sink)
                sink->done.store(d);
            if (progress)
                progress(d, total);
        }
    };

    QList<QFuture<void>> workers;
    workers.reserve(threads);
    for (int w = 0; w < threads; ++w)
        workers.append(QtConcurrent::run(&pool, worker));
    for (QFuture<void> &f : workers)
        f.waitForFinished();

    res.failedRegions = failedCount.load();
    res.renderedRegions = renderedCount.load();
    res.seedRegions = seedCount.load();

    res.image = master;
    res.elapsedMs = int(timer.elapsed());
    return res;
}

} // namespace WorldMapRenderer
