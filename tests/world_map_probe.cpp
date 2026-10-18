/**
 * @file   world_map_probe.cpp
 * @brief  地表地图渲染探针：合成 region 文件 → 真实渲染 → 像素断言
 * @author BlockBox Team
 *
 * 覆盖两条解析路径：
 *   - 1.18+（DataVersion 3465，root.sections / block_states，位打包按 long 对齐）
 *   - 1.14  （DataVersion 1976，Level.Sections / Palette+BlockStates，位流跨 long）
 * 两者都用 18 项调色板（5 bit/格），打包方式不同，正好验证解码器的候选方案校验；
 * 调色板末位是草丛覆盖物（short_grass），地表扫描必须穿透它才算对着色逻辑的验证。
 *
 * 通过返回 0，并把渲染结果写到 D:/BlockBox/_out/world_map_probe.png。
 */
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QMutexLocker>
#include <QPainter>
#include <QTemporaryDir>
#include <QThreadPool>

#include <cstdio>
#include <cstring>

#include <zlib.h>

#include "utils/Schematic/NbtCodec.h"
#include "utils/WorldMap/SeedTerrainRenderer.h"
#include "utils/WorldMap/WorldMapRenderer.h"

// MC_1_21（种子预测测试用的版本号）。包进命名空间避免其 enum Dimension
// 与 WorldMapRenderer::Dimension 在探针里互相污染
namespace cubiomes_mc {
#include "biomes.h"
}

namespace {

int g_failures = 0;

void check(bool cond, const char *what)
{
    std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond)
        ++g_failures;
}

QList<qint64> encodeStates(const QVector<int> &indices, int bits, bool packedPerLong)
{
    QList<qint64> longs;
    if (packedPerLong)
    {
        const int per = 64 / bits;
        const int count = (4096 + per - 1) / per;
        longs.fill(0, count);
        for (int i = 0; i < 4096; ++i)
        {
            const int li = i / per;
            const int off = (i % per) * bits;
            longs[li] |= (qint64(indices[i]) << off);
        }
    }
    else
    {
        // 位流跨 long 连续排列：一个条目可能被切成两段
        qint64 cur = 0;
        int used = 0;
        for (int i = 0; i < 4096; ++i)
        {
            quint64 v = quint64(indices[i]);
            int low = bits;
            if (used + bits > 64)
                low = 64 - used;
            cur |= qint64((v & ((quint64(1) << low) - 1)) << used);
            used += low;
            if (used == 64)
            {
                longs.append(cur);
                cur = 0;
                used = 0;
            }
            if (low < bits)
            {
                const int rest = bits - low;
                cur |= qint64(v >> low) & ((qint64(1) << rest) - 1);
                used = rest;
            }
        }
        if (used > 0)
            longs.append(cur);
    }
    return longs;
}

/** 18 项调色板：0=air 1=stone 2=地表方块 3..16=填充 17=草丛覆盖物（验证穿透） */
NbtTag makePalette()
{
    const char *names[18] = {
        "minecraft:air", "minecraft:stone", "minecraft:grass_block",
        "minecraft:dirt", "minecraft:cobblestone", "minecraft:gravel",
        "minecraft:sand", "minecraft:oak_planks", "minecraft:oak_log",
        "minecraft:oak_leaves", "minecraft:water", "minecraft:lava",
        "minecraft:bedrock", "minecraft:andesite", "minecraft:diorite",
        "minecraft:granite", "minecraft:deepslate", "minecraft:short_grass"
    };
    QList<NbtTag> items;
    for (int i = 0; i < 18; ++i)
    {
        NbtTag item = NbtCodec::makeCompound();
        item.children.append(NbtCodec::makeString(QStringLiteral("Name"),
                                                  QString::fromLatin1(names[i])));
        items.append(item);
    }
    return NbtCodec::makeList(QStringLiteral("palette"), NbtType::Compound, items);
}

/**
 * 每列地表：左 8 列草方块，右 8 列石头；y64 以下全是石头，y72 是地表，
 * y73 再铺一层草丛（short_grass）——渲染必须穿透这层覆盖物，
 * 否则右半区会变绿、"right is stone (gray)" 断言就会失败。
 */
QVector<int> buildIndices()
{
    QVector<int> indices(4096, 0);
    for (int lz = 0; lz < 16; ++lz)
    {
        for (int lx = 0; lx < 16; ++lx)
        {
            for (int yy = 0; yy <= 6; ++yy)
                indices[yy * 256 + lz * 16 + lx] = 1; // stone
            indices[7 * 256 + lz * 16 + lx] = (lx < 8) ? 2 : 1;
            indices[8 * 256 + lz * 16 + lx] = 17;      // 草丛覆盖层
        }
    }
    return indices;
}

NbtTag makeModernChunk(int cx, int cz)
{
    NbtTag section = NbtCodec::makeCompound();
    section.children.append(NbtCodec::makeByte(QStringLiteral("Y"), 4));

    NbtTag states = NbtCodec::makeCompound();
    states.name = QStringLiteral("block_states");
    states.children.append(makePalette());
    states.children.append(NbtCodec::makeLongArray(
        QStringLiteral("data"),
        encodeStates(buildIndices(), 5, true))); // 1.16+：按 long 对齐
    section.children.append(states);

    NbtTag root = NbtCodec::makeCompound();
    root.children.append(NbtCodec::makeInt(QStringLiteral("DataVersion"), 3465));
    root.children.append(NbtCodec::makeInt(QStringLiteral("xPos"), cx));
    root.children.append(NbtCodec::makeInt(QStringLiteral("zPos"), cz));
    root.children.append(NbtCodec::makeList(QStringLiteral("sections"),
                                            NbtType::Compound, { section }));
    return root;
}

NbtTag makeLegacyChunk(int cx, int cz)
{
    NbtTag section = NbtCodec::makeCompound();
    section.children.append(NbtCodec::makeByte(QStringLiteral("Y"), 4));
    section.children.append(makePalette()); // 标签名同为 "palette" 不匹配旧路径，下面改名
    section.children.last().name = QStringLiteral("Palette");
    section.children.append(NbtCodec::makeLongArray(
        QStringLiteral("BlockStates"),
        encodeStates(buildIndices(), 5, false))); // 1.16 前：位流跨 long

    NbtTag level = NbtCodec::makeCompound();
    level.name = QStringLiteral("Level");
    level.children.append(NbtCodec::makeInt(QStringLiteral("xPos"), cx));
    level.children.append(NbtCodec::makeInt(QStringLiteral("zPos"), cz));
    level.children.append(NbtCodec::makeList(QStringLiteral("Sections"),
                                             NbtType::Compound, { section }));

    NbtTag root = NbtCodec::makeCompound();
    root.children.append(NbtCodec::makeInt(QStringLiteral("DataVersion"), 1976));
    root.children.append(level);
    return root;
}

QByteArray zlibCompress(const QByteArray &raw)
{
    uLongf bound = compressBound(uLong(raw.size()));
    QByteArray out;
    out.resize(int(bound));
    if (compress2(reinterpret_cast<Bytef *>(out.data()), &bound,
                  reinterpret_cast<const Bytef *>(raw.constData()),
                  uLong(raw.size()), Z_DEFAULT_COMPRESSION) != Z_OK)
        return QByteArray();
    out.resize(int(bound));
    return out;
}

/** 写一个只有若干区块的 r.0.0.mca */
bool writeRegion(const QString &path, const QList<QPair<int, int>> &coords,
                 const QList<NbtTag> &chunks)
{
    QByteArray file;
    file.append(QByteArray(8192, '\0')); // 位置表 + 时间戳表

    int sector = 2;
    for (int i = 0; i < coords.size(); ++i)
    {
        const QByteArray nbt = NbtCodec::serialize(chunks[i]);
        const QByteArray comp = zlibCompress(nbt);
        if (comp.isEmpty())
            return false;

        QByteArray entry;
        const quint32 len = quint32(comp.size()) + 1; // 长度含压缩类型字节
        entry.append(char((len >> 24) & 0xFF));
        entry.append(char((len >> 16) & 0xFF));
        entry.append(char((len >> 8) & 0xFF));
        entry.append(char(len & 0xFF));
        entry.append(char(2)); // zlib
        entry.append(comp);
        const int sectors = (entry.size() + 4095) / 4096;
        entry.append(QByteArray(sectors * 4096 - entry.size(), '\0'));

        const int lx = coords[i].first & 31;
        const int lz = coords[i].second & 31;
        const int idx = lx + lz * 32;
        file[idx * 4 + 0] = char((sector >> 16) & 0xFF);
        file[idx * 4 + 1] = char((sector >> 8) & 0xFF);
        file[idx * 4 + 2] = char(sector & 0xFF);
        file[idx * 4 + 3] = char(sectors);

        if (file.size() < sector * 4096 + entry.size())
            file.append(QByteArray(sector * 4096 + entry.size() - file.size(), '\0'));
        std::memcpy(file.data() + sector * 4096, entry.constData(), size_t(entry.size()));
        sector += sectors;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(file);
    return true;
}

} // namespace

/**
 * 真实存档模式：world_map_probe --real <存档目录>
 * 反复渲染真实 region 文件（含三维度、多轮），统计耗时/失败数并落图，
 * 期间任何 qWarning（位宽解码失败、调色板异常等）都会打到 stderr。
 */
int runRealSave(const QString &savePath, int maxThreads)
{
    using namespace WorldMapRenderer;

    std::printf("real save: %s\n", savePath.toUtf8().constData());

    struct Case { Dimension dim; const char *name; int repeats; };
    const Case cases[] = {
        { Dimension::Overworld, "overworld", 3 },
        { Dimension::Nether,    "nether",    1 },
        { Dimension::End,       "end",       1 },
    };

    for (const Case &c : cases)
    {
        const QString dir = regionDir(savePath, c.dim);
        QDir d(dir);
        const auto files = d.entryList({ QStringLiteral("*.mca") }, QDir::Files);
        qint64 bytes = 0;
        for (const QString &f : files)
            bytes += QFileInfo(d.absoluteFilePath(f)).size();
        std::printf("  [%s] %d regions, %.1f MB\n", c.name, int(files.size()),
                    double(bytes) / (1024.0 * 1024.0));
        if (files.isEmpty())
            continue;

        for (int round = 0; round < c.repeats; ++round)
        {
            auto sink = std::make_shared<ProgressiveSink>();
            QElapsedTimer timer;
            timer.start();
            const RenderResult res = render(savePath, c.dim, ShadeMode::BlockColor,
                                            {}, {}, sink, maxThreads);
            const qint64 ms = timer.elapsed();

            std::printf("    round %d: rendered=%d/%d failed=%d  %dx%d bpp=%d  "
                        "%lld ms (sink tiles=%d done=%d)\n",
                        round, res.renderedRegions, res.regionCount, res.failedRegions,
                        res.image.width(), res.image.height(), res.blockPerPixel,
                        static_cast<long long>(ms),
                        int(sink->pendingTiles.size()), sink->done.load());

            check(res.error.isEmpty(), "real render no error");
            check(res.renderedRegions == res.regionCount,
                  "real render covered every region");
            check(!res.image.isNull(), "real render image not null");
            check(sink->layoutReady.load(), "real sink layout published");
            check(sink->done.load() == res.renderedRegions, "real sink progress matches");

            // 渐进图块拼接 == 最终图（真实数据下逐字节比对）
            if (!res.image.isNull())
            {
                QImage tiled(sink->imageWidth, sink->imageHeight, QImage::Format_ARGB32);
                tiled.fill(qRgba(0, 0, 0, 0));
                QList<QPair<QPoint, QImage>> tiles;
                {
                    QMutexLocker locker(&sink->tileMutex);
                    tiles = sink->pendingTiles;
                }
                QPainter painter(&tiled);
                for (const auto &item : tiles)
                    painter.drawImage(item.first, item.second);
                painter.end();
                const bool same = tiled.sizeInBytes() == res.image.sizeInBytes()
                    && std::memcmp(tiled.constBits(), res.image.constBits(),
                                   size_t(tiled.sizeInBytes())) == 0;
                check(same, "real progressive tiles == final image");
            }

            if (round == 0 && c.dim == Dimension::Overworld && !res.image.isNull())
                res.image.save(QStringLiteral("D:/BlockBox/_out/world_map_real_overworld.png"));
        }
    }
    return g_failures == 0 ? 0 : 1;
}

int main(int argc, char *argv[])
{
    // 无缓冲：崩溃时也要能看到最后一条日志，方便定位
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    QCoreApplication app(argc, argv);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() >= 3 && args.at(1) == QLatin1String("--real"))
        return runRealSave(args.at(2),
                             args.size() >= 4 ? args.at(3).toInt() : 0);

    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::printf("[FAIL] temp dir\n");
        return 1;
    }

    const QList<QPair<int, int>> coords = {
        { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }
    };

    // ── 1. 1.18+ 格式 ──
    const QString modernSave = tmp.path() + QStringLiteral("/modern");
    QList<NbtTag> modernChunks;
    for (const auto &c : coords)
        modernChunks.append(makeModernChunk(c.first, c.second));
    check(writeRegion(modernSave + "/region/r.0.0.mca", coords, modernChunks),
          "write modern region");

    // 自检：序列化 → 再解析，确认 List 元素确实写成裸 Compound 载荷
    {
        const QByteArray nbt = NbtCodec::serialize(modernChunks.first());
        const auto back = NbtCodec::parse(nbt);
        check(back.has_value(), "nbt round-trip parse");
        if (back)
        {
            std::printf("  root children=%d\n", int(back->children.size()));
            for (const auto &ch : back->children)
                std::printf("    - %s type=%d\n", ch.name.toUtf8().constData(), int(ch.type));
            const NbtTag *secs = NbtCodec::findChild(*back, QStringLiteral("sections"));
            std::printf("  sections=%s count=%d\n",
                        secs ? "found" : "MISSING",
                        secs ? int(secs->children.size()) : -1);
            if (secs && !secs->children.isEmpty())
            {
                const NbtTag &sec0 = secs->children.first();
                for (const auto &ch : sec0.children)
                    std::printf("      * %s type=%d\n",
                                ch.name.toUtf8().constData(), int(ch.type));
            }
        }
    }

    auto res = WorldMapRenderer::render(modernSave, WorldMapRenderer::Dimension::Overworld,
                                        WorldMapRenderer::ShadeMode::BlockColor, {}, {});
    check(res.error.isEmpty(), "modern render no error");
    check(res.renderedRegions == 1, "modern renderedRegions == 1");
    check(!res.image.isNull(), "modern image not null");
    check(res.image.width() == 512 && res.image.height() == 512, "modern image 512x512");

    if (!res.image.isNull())
    {
        int opaque = 0;
        for (int y = 0; y < res.image.height(); ++y)
            for (int x = 0; x < res.image.width(); ++x)
                if (qAlpha(res.image.pixel(x, y)) != 0) ++opaque;
        std::printf("  opaque pixels=%d (expect 1024), failedRegions=%d regionCount=%d\n",
                    opaque, res.failedRegions, res.regionCount);

        const QRgb left = res.image.pixel(0, 0);      // lx=0 → 草方块
        const QRgb right = res.image.pixel(15, 0);    // lx=15 → 石头
        const QRgb empty = res.image.pixel(400, 400); // 区块 (25,25) 未生成

        check(qAlpha(left) == 255, "surface pixel opaque");
        check(left != right, "left half != right half");
        // 草方块：绿通道明显高于红通道；石头：三通道接近
        check(qGreen(left) > qRed(left) + 10, "left is grass (green dominant)");
        check(qAbs(qRed(right) - qGreen(right)) < 24
              && qAbs(qGreen(right) - qBlue(right)) < 24, "right is stone (gray)");
        check(qAlpha(empty) == 0, "ungenerated chunk transparent");

        QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));
        res.image.save(QStringLiteral("D:/BlockBox/_out/world_map_probe.png"));
        std::printf("  image saved, grass=(%d,%d,%d) stone=(%d,%d,%d)\n",
                    qRed(left), qGreen(left), qBlue(left),
                    qRed(right), qGreen(right), qBlue(right));
    }

    // ── 2. 高度着色模式 ──
    auto resH = WorldMapRenderer::render(modernSave, WorldMapRenderer::Dimension::Overworld,
                                         WorldMapRenderer::ShadeMode::Height, {}, {});
    if (!resH.image.isNull())
    {
        const QRgb p = resH.image.pixel(0, 0);
        check(qAlpha(p) == 255 && qRed(p) == qGreen(p) && qGreen(p) == qBlue(p),
              "height mode is grayscale");
    }
    else
    {
        check(false, "height mode render");
    }

    // ── 2b. 地形着色模式（海拔分层设色 + 山体阴影）──
    auto resT = WorldMapRenderer::render(modernSave, WorldMapRenderer::Dimension::Overworld,
                                         WorldMapRenderer::ShadeMode::Terrain, {}, {});
    if (!resT.image.isNull())
    {
        const QRgb leftT = resT.image.pixel(0, 0);    // 草方块 y71
        const QRgb rightT = resT.image.pixel(15, 0);  // 石头 y71
        check(qAlpha(leftT) == 255, "terrain pixel opaque");
        // 不是灰度（与 Height 模式区分），且低地为绿系（蓝通道明显更低）
        check(!(qRed(leftT) == qGreen(leftT) && qGreen(leftT) == qBlue(leftT)),
              "terrain mode is colored");
        check(qBlue(leftT) + 40 < qGreen(leftT), "terrain tint lowland is greenish");
        // 同一海拔 → 同一色（分层设色只看高度，与地表是草还是石无关）
        check(leftT == rightT, "terrain tint depends on elevation only");
        resT.image.save(QStringLiteral("D:/BlockBox/_out/world_map_probe_terrain.png"));
        std::printf("  terrain grass col=(%d,%d,%d) stone col=(%d,%d,%d)\n",
                    qRed(leftT), qGreen(leftT), qBlue(leftT),
                    qRed(rightT), qGreen(rightT), qBlue(rightT));
    }
    else
    {
        check(false, "terrain mode render");
    }

    // ── 3. 1.14 旧格式 ──
    const QString legacySave = tmp.path() + QStringLiteral("/legacy");
    QList<NbtTag> legacyChunks;
    for (const auto &c : coords)
        legacyChunks.append(makeLegacyChunk(c.first, c.second));
    check(writeRegion(legacySave + "/region/r.0.0.mca", coords, legacyChunks),
          "write legacy region");

    auto resL = WorldMapRenderer::render(legacySave, WorldMapRenderer::Dimension::Overworld,
                                         WorldMapRenderer::ShadeMode::BlockColor, {}, {});
    check(resL.error.isEmpty(), "legacy render no error");
    if (!resL.image.isNull())
    {
        const QRgb left = resL.image.pixel(0, 0);
        const QRgb right = resL.image.pixel(15, 0);
        check(qAlpha(left) == 255, "legacy surface pixel opaque");
        check(left != right, "legacy left != right");
        check(qGreen(left) > qRed(left) + 10, "legacy left is grass");
        resL.image.save(QStringLiteral("D:/BlockBox/_out/world_map_probe_legacy.png"));
    }
    else
    {
        check(false, "legacy image not null");
    }

    // ── 4. 没有区块时给出错误提示 ──
    const QString emptySave = tmp.path() + QStringLiteral("/empty");
    QDir().mkpath(emptySave);
    auto resE = WorldMapRenderer::render(emptySave, WorldMapRenderer::Dimension::Overworld,
                                         WorldMapRenderer::ShadeMode::BlockColor, {}, {});
    check(!resE.error.isEmpty(), "empty save reports error");

    // ── 4b. 种子预测：未加载区域按种子渲染近似地形 ──
    {
        const quint64 seedVal = 4562377579810721523ULL;
        const int mc = cubiomes_mc::MC_1_21;
        using WorldMapRenderer::Dimension;
        using WorldMapRenderer::ShadeMode;
        using WorldMapRenderer::SeedPreview;
        using WorldMapRenderer::render;
        using WorldMapRenderer::renderRegionTile;

        // 单区域接口：直渲一张预测图，确定性 + 有内容 + 不同种子结果不同
        const QImage s0 = SeedTerrainRenderer::renderRegionTile(
            0, 0, Dimension::Overworld, ShadeMode::BlockColor, seedVal, mc, 128);
        check(!s0.isNull() && s0.width() == 128 && s0.height() == 128,
              "seed tile renders 128x128");
        int opaque = 0;
        if (!s0.isNull())
        {
            for (int y = 0; y < s0.height(); ++y)
            {
                const QRgb *line = reinterpret_cast<const QRgb *>(s0.constScanLine(y));
                for (int x = 0; x < s0.width(); ++x)
                    if (qAlpha(line[x]) == 255) ++opaque;
            }
        }
        check(opaque > s0.width() * s0.height() / 2,
              "seed tile has mostly opaque surface");
        const QImage s1 = SeedTerrainRenderer::renderRegionTile(
            0, 0, Dimension::Overworld, ShadeMode::BlockColor, seedVal, mc, 128);
        check(!s0.isNull() && !s1.isNull()
              && std::memcmp(s0.constBits(), s1.constBits(),
                             size_t(s0.sizeInBytes())) == 0,
              "seed tile deterministic for same seed");
        const QImage s2 = SeedTerrainRenderer::renderRegionTile(
            0, 0, Dimension::Overworld, ShadeMode::BlockColor,
            seedVal + 1, mc, 128);
        check(!s0.isNull() && !s2.isNull()
              && std::memcmp(s0.constBits(), s2.constBits(),
                             size_t(s0.sizeInBytes())) != 0,
              "seed tile differs for different seed");
        check(SeedTerrainRenderer::renderRegionTile(
                  0, 0, Dimension::Overworld, ShadeMode::BlockColor,
                  seedVal, 0, 128).isNull(),
              "seed tile empty for unknown version");
        // 低分辨率（概览 bpp=8 → outSize=64）下相邻 region 必须连续：
        // 采样栅格铺满整个 region，左右边缘采样的是相邻格，色差应很小
        {
            const QImage ta = SeedTerrainRenderer::renderRegionTile(
                0, 0, Dimension::Overworld, ShadeMode::BlockColor, seedVal, mc, 64);
            const QImage tb = SeedTerrainRenderer::renderRegionTile(
                1, 0, Dimension::Overworld, ShadeMode::BlockColor, seedVal, mc, 64);
            check(!ta.isNull() && !tb.isNull(), "coarse seed tiles render");
            if (!ta.isNull() && !tb.isNull())
            {
                int diff = 0;
                for (int y = 0; y < 64; ++y)
                {
                    const QRgb a = ta.pixel(63, y);
                    const QRgb b = tb.pixel(0, y);
                    diff += qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b))
                           + qAbs(qBlue(a) - qBlue(b));
                }
                diff /= 64 * 3;
                std::printf("  coarse neighbor edge avg diff = %d\n", diff);
                check(diff < 24, "coarse seed tiles continuous across region edge");
            }
        }
        // 方块级精度（outSize=512）：群系 scale=1 + 连续高度，
        // 相邻像素不应整块 4×4 复制（那是旧 128 采样的特征）
        {
            const QImage sB = SeedTerrainRenderer::renderRegionTile(
                0, 0, Dimension::Overworld, ShadeMode::BlockColor, seedVal, mc, 512);
            check(!sB.isNull() && sB.width() == 512 && sB.height() == 512,
                  "block-level seed tile is 512x512");
            if (!sB.isNull())
            {
                // 4×4 邻域内高度（用红通道近似）若全相同 → 还在 4 格台阶上
                int flat4 = 0, total4 = 0;
                for (int y = 0; y + 4 <= 512; y += 8)
                {
                    for (int x = 0; x + 4 <= 512; x += 8)
                    {
                        const QRgb p00 = sB.pixel(x, y);
                        if (qAlpha(p00) == 0)
                            continue;
                        bool same = true;
                        for (int dy = 0; dy < 4 && same; ++dy)
                            for (int dx = 0; dx < 4 && same; ++dx)
                            {
                                const QRgb p = sB.pixel(x + dx, y + dy);
                                if (qRed(p) != qRed(p00) || qGreen(p) != qGreen(p00)
                                    || qBlue(p) != qBlue(p00))
                                    same = false;
                            }
                        if (same)
                            ++flat4;
                        ++total4;
                    }
                }
                check(total4 > 100, "block-level tile has enough samples");
                // 若仍是 4×4 色块，flat4/total4 ≈ 1；方块级应显著更少
                const double flatRatio = total4 ? double(flat4) / total4 : 1.0;
                std::printf("  block-level flat 4x4 ratio = %.3f\n", flatRatio);
                check(flatRatio < 0.85,
                      "block-level tile not stuck at 4-block steps");
                QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));
                sB.save(QStringLiteral("D:/BlockBox/_out/world_map_seed_block512.png"));
            }
            // 确定性：同种子两次渲染一致
            const QImage sB2 = SeedTerrainRenderer::renderRegionTile(
                0, 0, Dimension::Overworld, ShadeMode::BlockColor, seedVal, mc, 512);
            check(!sB.isNull() && !sB2.isNull()
                  && std::memcmp(sB.constBits(), sB2.constBits(),
                                 size_t(sB.sizeInBytes())) == 0,
                  "block-level tile deterministic");
        }
        // 高度着色模式：灰度输出
        const QImage sH = SeedTerrainRenderer::renderRegionTile(
            5, -3, Dimension::Overworld, ShadeMode::Height, seedVal, mc, 64);
        bool gray = !sH.isNull();
        if (gray)
        {
            for (int y = 0; y < sH.height() && gray; ++y)
            {
                const QRgb *line = reinterpret_cast<const QRgb *>(sH.constScanLine(y));
                for (int x = 0; x < sH.width(); ++x)
                {
                    if (qAlpha(line[x]) == 0)
                        continue;
                    if (qRed(line[x]) != qGreen(line[x])
                        || qGreen(line[x]) != qBlue(line[x]))
                    {
                        gray = false;
                        break;
                    }
                }
            }
        }
        check(gray, "seed tile height mode is grayscale");

        // 地形模式（种子预测）：彩色输出 + 能看出山体阴影明暗变化
        const QImage sT = SeedTerrainRenderer::renderRegionTile(
            5, -3, Dimension::Overworld, ShadeMode::Terrain, seedVal, mc, 128);
        check(!sT.isNull(), "seed tile terrain mode renders");
        if (!sT.isNull())
        {
            bool colored = false, hasVariation = false;
            QRgb first = 0;
            for (int y = 0; y < sT.height() && !(colored && hasVariation); ++y)
            {
                const QRgb *line = reinterpret_cast<const QRgb *>(sT.constScanLine(y));
                for (int x = 0; x < sT.width(); ++x)
                {
                    if (qAlpha(line[x]) == 0)
                        continue;
                    if (qRed(line[x]) != qGreen(line[x])
                        || qGreen(line[x]) != qBlue(line[x]))
                        colored = true;
                    if (first == 0)
                        first = line[x];
                    else if (line[x] != first)
                        hasVariation = true; // 山体阴影 / 海拔带来明暗层次
                    if (colored && hasVariation)
                        break;
                }
            }
            check(colored, "seed terrain mode is colored");
            check(hasVariation, "seed terrain mode has hillshade/height variation");
            QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));
            sT.save(QStringLiteral("D:/BlockBox/_out/world_map_seed_terrain.png"));
        }

        // 高度限制：种子预测同样按 Y 截断（与真实地表同一套语义）
        {
            using WorldMapRenderer::ShadeMode;
            const QImage sAll = SeedTerrainRenderer::renderRegionTile(
                5, -3, Dimension::Overworld, ShadeMode::Terrain, seedVal, mc,
                128, -1);
            const QImage sY64 = SeedTerrainRenderer::renderRegionTile(
                5, -3, Dimension::Overworld, ShadeMode::Terrain, seedVal, mc,
                128, 64);
            check(!sAll.isNull() && !sY64.isNull()
                  && sAll.size() == sY64.size(),
                  "seed tile with height limit renders same size");
            check(std::memcmp(sAll.constBits(), sY64.constBits(),
                              size_t(sAll.sizeInBytes())) != 0,
                  "height limit changes seed tile (mountains truncated)");
            // 默认 -1 与显式不限制一致（向后兼容）
            const QImage sDef = SeedTerrainRenderer::renderRegionTile(
                5, -3, Dimension::Overworld, ShadeMode::Terrain, seedVal, mc,
                128);
            check(std::memcmp(sAll.constBits(), sDef.constBits(),
                              size_t(sAll.sizeInBytes())) == 0,
                  "default maxSurfaceY == unlimited");
            // 全图截断后高度着色整体变暗（截断面低于原地表）
            const QImage hAll = SeedTerrainRenderer::renderRegionTile(
                5, -3, Dimension::Overworld, ShadeMode::Height, seedVal, mc,
                64, -1);
            const QImage hY50 = SeedTerrainRenderer::renderRegionTile(
                5, -3, Dimension::Overworld, ShadeMode::Height, seedVal, mc,
                64, 50);
            long long sumAll = 0, sumY50 = 0;
            int nAll = 0, nY50 = 0;
            for (int y = 0; y < hAll.height(); ++y)
            {
                const QRgb *la = reinterpret_cast<const QRgb *>(hAll.constScanLine(y));
                const QRgb *lb = reinterpret_cast<const QRgb *>(hY50.constScanLine(y));
                for (int x = 0; x < hAll.width(); ++x)
                {
                    if (qAlpha(la[x]) == 255) { sumAll += qRed(la[x]); ++nAll; }
                    if (qAlpha(lb[x]) == 255) { sumY50 += qRed(lb[x]); ++nY50; }
                }
            }
            check(nAll > 0 && nY50 > 0, "height-limited tiles have opaque pixels");
            if (nAll > 0 && nY50 > 0)
            {
                std::printf("  height-limit avg gray: all=%.1f y50=%.1f\n",
                            double(sumAll) / nAll, double(sumY50) / nY50);
                check(sumY50 * nAll <= sumAll * nY50,
                      "Y=50 cut darker than unlimited height shading");
            }
            QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));
            sY64.save(QStringLiteral("D:/BlockBox/_out/world_map_seed_y64.png"));
        }

        // 空维度 + 种子预测：不再报错，围绕中心渲染出预览图
        SeedPreview sp;
        sp.enabled = true;
        sp.seed = seedVal;
        sp.mc = mc;
        sp.padRegions = 2;
        sp.radiusRegions = 2; // 探针用小窗口，断言槽位数好算
        sp.hasCenter = true;
        sp.centerRegionX = 1;
        sp.centerRegionZ = -1;
        auto resSE = render(emptySave, Dimension::Overworld,
                            ShadeMode::BlockColor, {}, {}, nullptr, 0, -1, sp);
        check(resSE.error.isEmpty(), "seed preview renders empty dimension");
        check(!resSE.image.isNull(), "seed preview image not null");
        check(resSE.seedRegions == 25 && resSE.renderedRegions == 25,
              "seed preview fills (2*pad+1)^2 regions");
        check(resSE.regionCount == 25, "seed preview regionCount == slots");
        check(resSE.minChunkX == (1 - 2) * 32 && resSE.minChunkZ == (-1 - 2) * 32,
              "seed preview centered on requested region");
        if (!resSE.image.isNull())
        {
            std::printf("  seed-only: %dx%d seedRegions=%d (%lld ms)\n",
                        resSE.image.width(), resSE.image.height(),
                        resSE.seedRegions,
                        static_cast<long long>(resSE.elapsedMs));
            QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));
            resSE.image.save(QStringLiteral("D:/BlockBox/_out/world_map_seed_empty.png"));
        }

        // 已有文件 + 种子预测：真实数据优先，包围盒外扩 pad 圈
        auto resSW = render(modernSave, Dimension::Overworld,
                            ShadeMode::BlockColor, {}, {}, nullptr, 0, -1, sp);
        check(resSW.error.isEmpty(), "seed preview + real files no error");
        check(resSW.renderedRegions == resSW.regionCount,
              "seed preview renders every slot");
        check(resSW.seedRegions > 0, "seed preview renders seed slots");
        check(resSW.failedRegions == 0, "seed preview no failures");
        // 文件区域 (0,0) 的真实草地还在：外扩 2 圈后 min = -2 → 图上 (2*512, 2*512)
        if (!resSW.image.isNull())
        {
            // 偏移按实际画布原点逐轴换算（X/Z 窗口可能不对称）
            const int offX = (0 - resSW.minChunkX / 32) * 512;
            const int offZ = (0 - resSW.minChunkZ / 32) * 512;
            const QRgb grass = resSW.image.pixel(offX + 4, offZ + 4); // 区块左半 = 草
            check(qAlpha(grass) == 255 && qGreen(grass) > qRed(grass) + 10,
                  "real region content preserved under seed padding");
            // 外扩出来的种子区域也有内容（画布左上角附近）
            check(qAlpha(resSW.image.pixel(2, 2)) == 255,
                  "padded ring filled by seed terrain");
            // 半生成 region（只写了 2×2 区块）内部的空洞也被种子垫底补齐，
            // 已加载与未加载之间不留空缺
            check(qAlpha(resSW.image.pixel(offX + 400, offZ + 400)) == 255,
                  "holes inside partial region filled by seed underlay");
            resSW.image.save(QStringLiteral("D:/BlockBox/_out/world_map_seed_padded.png"));
        }

        // 细节瓦片接口：缺失区域 + 种子预测 → 有图；关掉预测 → 空图
        const QImage st = renderRegionTile(emptySave, Dimension::Overworld,
                                           1, -1, ShadeMode::BlockColor, -1, sp);
        check(!st.isNull() && st.width() == 512 && st.height() == 512,
              "renderRegionTile falls back to seed tile");
        check(renderRegionTile(emptySave, Dimension::Overworld, 1, -1,
                               ShadeMode::BlockColor).isNull(),
              "renderRegionTile still empty without seed preview");

        // 下界种子预测：群系配色可用
        auto resSN = render(emptySave, Dimension::Nether,
                            ShadeMode::BlockColor, {}, {}, nullptr, 0, -1, sp);
        check(resSN.error.isEmpty() && !resSN.image.isNull(),
              "seed preview works for nether");

        // 半径窗口：radius 远大于 pad 时，画布按 radius 铺开（视角中心向外）
        SeedPreview spW = sp;
        spW.padRegions = 1;
        spW.radiusRegions = 6;
        auto resW = render(emptySave, Dimension::Overworld,
                           ShadeMode::BlockColor, {}, {}, nullptr, 0, -1, spW);
        check(resW.seedRegions == 13 * 13,
              "radius window covers (2*radius+1)^2 regions");
        check(resW.minChunkX == (1 - 6) * 32 && resW.minChunkZ == (-1 - 6) * 32,
              "radius window centered on view center");
    }

    // ── 5. 下界路径 ──
    const QString netherSave = tmp.path() + QStringLiteral("/nether");
    check(writeRegion(netherSave + "/DIM-1/region/r.0.0.mca", coords, modernChunks),
          "write nether region");
    check(WorldMapRenderer::hasRegions(netherSave, WorldMapRenderer::Dimension::Nether),
          "hasRegions(nether)");
    auto resN = WorldMapRenderer::render(netherSave, WorldMapRenderer::Dimension::Nether,
                                         WorldMapRenderer::ShadeMode::BlockColor, {}, {});
    check(resN.renderedRegions == 1, "nether dimension path works");

    // ── 6. 性能与渐进渲染：4 个满区域（4096 区块）单线程 vs 并行 ──
    {
        const QString bench = tmp.path() + QStringLiteral("/bench");
        // 2×2 邻接区域 + 一个远处区域 r.8.8 → 跨度 9 个 region（4608 格），
        // 触发 bpp=2 降采样路径，用来验证盒式降采样后的 alpha 二值化
        const QList<QPair<int, int>> benchRegions = {
            qMakePair(0, 0), qMakePair(1, 0), qMakePair(0, 1),
            qMakePair(1, 1), qMakePair(8, 8)
        };
        for (const auto &rc : benchRegions)
        {
            const int span = (rc.first == 8) ? 4 : 32;
            QList<QPair<int, int>> cs;
            QList<NbtTag> chs;
            cs.reserve(span * span);
            for (int dz = 0; dz < span; ++dz)
            {
                for (int dx = 0; dx < span; ++dx)
                {
                    const int cx = rc.first * 32 + dx;
                    const int cz = rc.second * 32 + dz;
                    cs.append(qMakePair(cx, cz));
                    chs.append(makeModernChunk(cx, cz));
                }
            }
            const QString path = bench + QStringLiteral("/region/r.%1.%2.mca")
                                                 .arg(rc.first).arg(rc.second);
            check(writeRegion(path, cs, chs), "write bench region");
        }

        QElapsedTimer timer;
        timer.start();
        const auto single = WorldMapRenderer::render(
            bench, WorldMapRenderer::Dimension::Overworld,
            WorldMapRenderer::ShadeMode::BlockColor, {}, {}, nullptr, 1);
        const qint64 singleMs = timer.elapsed();

        timer.restart();
        auto sink = std::make_shared<WorldMapRenderer::ProgressiveSink>();
        const auto parallel = WorldMapRenderer::render(
            bench, WorldMapRenderer::Dimension::Overworld,
            WorldMapRenderer::ShadeMode::BlockColor, {}, {}, sink);
        const qint64 parallelMs = timer.elapsed();

        check(single.renderedRegions == 5 && parallel.renderedRegions == 5,
              "bench renders 5 regions (single + parallel)");
        check(single.elapsedMs > 0 && parallel.elapsedMs > 0, "elapsedMs reported");
        std::printf("  4096+ chunks: single=%lldms parallel=%lldms (x%.2f), pool=%d, bpp=%d\n",
                    static_cast<long long>(singleMs),
                    static_cast<long long>(parallelMs),
                    parallelMs > 0 ? double(singleMs) / double(parallelMs) : 0.0,
                    QThreadPool::globalInstance()->maxThreadCount(),
                    parallel.blockPerPixel);

        if (QThreadPool::globalInstance()->maxThreadCount() >= 2)
            check(parallelMs <= qint64(double(singleMs) * 0.85),
                  "parallel render faster than single-thread");

        // 远处区域把跨度顶到 9 个 region → 必须走 2x 降采样
        check(parallel.blockPerPixel == 2, "far region forces 2x downsample");
        // 盒式降采样后 alpha 必须二值：否则未生成区域的边缘会发虚
        {
            bool halfAlpha = false;
            const QImage &im = parallel.image;
            for (int y = 0; y < im.height() && !halfAlpha; ++y)
            {
                const QRgb *line = reinterpret_cast<const QRgb *>(im.constScanLine(y));
                for (int x = 0; x < im.width() && !halfAlpha; ++x)
                {
                    const int a = qAlpha(line[x]);
                    if (a != 0 && a != 255)
                        halfAlpha = true;
                }
            }
            check(!halfAlpha, "box downsample keeps binary alpha");
        }

        // 单区域 1:1 接口（视口细节层用）：存在的区域给 512×512，不存在的给空图
        {
            const QImage tile = WorldMapRenderer::renderRegionTile(
                bench, WorldMapRenderer::Dimension::Overworld, 0, 0,
                WorldMapRenderer::ShadeMode::BlockColor);
            check(!tile.isNull() && tile.width() == 512 && tile.height() == 512,
                  "renderRegionTile returns 512x512");
            int opaque = 0;
            if (!tile.isNull())
            {
                for (int y = 0; y < tile.height(); ++y)
                {
                    const QRgb *line =
                        reinterpret_cast<const QRgb *>(tile.constScanLine(y));
                    for (int x = 0; x < tile.width(); ++x)
                        if (qAlpha(line[x]) == 255) ++opaque;
                }
            }
            check(opaque > 1000, "renderRegionTile has surface content");
            check(WorldMapRenderer::renderRegionTile(
                          bench, WorldMapRenderer::Dimension::Overworld, 99, 99,
                          WorldMapRenderer::ShadeMode::BlockColor).isNull(),
                  "renderRegionTile empty for missing region");
        }

        // 渐进渲染上下文：布局先发布，每个区域推一块图
        check(sink->layoutReady.load(), "sink layout published");
        check(sink->imageWidth == parallel.image.width()
              && sink->imageHeight == parallel.image.height(),
              "sink layout matches final image");
        check(sink->done.load() == parallel.renderedRegions, "sink progress == rendered");
        check(sink->pendingTiles.size() == parallel.renderedRegions,
              "sink queued one tile per region");

        // 把 sink 里的图块贴出来，应与最终结果逐字节一致
        QImage tiled(sink->imageWidth, sink->imageHeight, QImage::Format_ARGB32);
        tiled.fill(qRgba(0, 0, 0, 0));
        {
            QList<QPair<QPoint, QImage>> tiles;
            {
                QMutexLocker locker(&sink->tileMutex);
                tiles = sink->pendingTiles;
            }
            QPainter painter(&tiled);
            for (const auto &item : tiles)
                painter.drawImage(item.first, item.second);
            painter.end();
        }
        const bool same = tiled.sizeInBytes() == parallel.image.sizeInBytes()
            && std::memcmp(tiled.constBits(), parallel.image.constBits(),
                           size_t(tiled.sizeInBytes())) == 0;
        check(same, "progressive tiles == final image (byte exact)");
    }

    std::printf("failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
