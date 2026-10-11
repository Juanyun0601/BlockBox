/**
 * @file   world_map_page_probe.cpp
 * @brief  WorldMapPage（存档地表地图页）视觉探针
 *
 * 加载真实 style.qss（token 解析后），合成一个含两个 region 的存档，
 * 渲染真实 WorldMapPage 并等待后台渲染完成：
 *  - 顶部操作区控件齐全（维度 / 着色 / 网格 / 缩放 / 坐标 / 导出 / 刷新）
 *  - 画布切到地图视图（worldMapStack 索引 1），进度条收起
 *  - 样式表无 "Could not parse" 告警
 *  - 截图后统计中央区域颜色数，确认真的画出了地形而不是空底
 * 输出：D:/BlockBox/_out/world_map_page_probe_{map,empty}.png
 */
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QVector>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <zlib.h>

#include "pages/WorldMapPage.h"
#include "platform.h"
#include "utils/Schematic/NbtCodec.h"
#include "utils/Saves/LevelDatEditor.h"
#include "utils/WorldMap/StructureFinder.h"
#include "utils/WorldMap/StructureIcons.h"

namespace {

int g_parseWarnings = 0;
int g_fail = 0;

void msgHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    if (msg.contains(QLatin1String("Could not parse")))
        ++g_parseWarnings;
    std::fprintf(stderr, "[qt ] %s\n", msg.toLocal8Bit().constData());
}

void expect(bool ok, const char *what)
{
    std::fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++g_fail;
}

// 与 ThemeManager::buildTokenTable 同族的 token 表（探针精简版，同 source_switch_probe）
QString resolveTokens(QString style)
{
    struct KV { const char *k; const char *v; };
    const QVector<KV> t = {
        {"@BG_BASE@", "#f5f5f5"}, {"@BG_CARD@", "#ffffff"},
        {"@BG_CONTENT@", "#fafafa"}, {"@BG_MUTED@", "#f8f9fa"},
        {"@BG_HOVER@", "#eaeaea"}, {"@BG_PRESSED@", "#e0e0e0"},
        {"@BG_TRACK@", "#e8e8e8"}, {"@BG_TAB@", "#f0f0f0"},
        {"@BG_DISABLED@", "#d5d7db"}, {"@BG_MSG@", "#edf0f4"},
        {"@BG_GLASS@", "rgba(255, 255, 255, 0.72)"},
        {"@BORDER@", "#e8eaed"}, {"@BORDER_STRONG@", "#dadce0"},
        {"@BORDER_HOVER@", "#bdbdbd"}, {"@BORDER_DASHED@", "#c4c7c5"},
        {"@BORDER_DISABLED@", "#b0b0b0"}, {"@BORDER_LIGHT@", "#f0f1f3"},
        {"@TEXT_SECONDARY@", "#666666"}, {"@TEXT_TERTIARY@", "#888888"},
        {"@TEXT_DISABLED@", "#bbbbbb"}, {"@TEXT_PRIMARY@", "#333333"},
        {"@TEXT_BORDER@", "#e8eaed"}, {"@TEXT_ACCENT@", "#1b5e20"},
        {"@PRIMARY_BG@", "#e8f5e9"},
        {"@BLACK_RGBA_0.06@", "rgba(0, 0, 0, 0.06)"},
        {"@BLACK_RGBA_0.08@", "rgba(0, 0, 0, 0.08)"},
        {"@BLACK_RGBA_0.10@", "rgba(0, 0, 0, 0.10)"},
        {"@BLACK_RGBA_0.13@", "rgba(0, 0, 0, 0.13)"},
        {"@BLACK_RGBA_0.25@", "rgba(0, 0, 0, 0.25)"},
        {"@PRIMARY@", "#2E7D32"},
        {"@PRIMARY_HOVER@", "#256428"},
        {"@PRIMARY_PRESSED@", "#1B5E20"},
        {"@PRIMARY_DARK@", "#1B5E20"},
        {"@PRIMARY_RGBA_12@", "rgba(46, 125, 50, 0.12)"},
        {"@PRIMARY_RGBA_20@", "rgba(46, 125, 50, 0.2)"},
        {"@PRIMARY_RGBA_38@", "rgba(46, 125, 50, 0.38)"},
        {"@PRIMARY_BG@", "#e8f5e9"},
        {"@TEXT_ON_PRIMARY@", "#ffffff"},
        {"@SUCCESS@", "#10B981"}, {"@INFO@", "#2196F3"},
        {"@WARNING@", "#F57C00"}, {"@DANGER@", "#D32F2F"},
    };
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    static const QRegularExpression re(QStringLiteral("@([A-Za-z0-9_.\\-]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

// ── 合成存档：5 bit 调色板 + 按 long 对齐的位打包（1.16+ 真实格式） ──

QList<qint64> packStates(const QVector<int> &indices, int bits)
{
    const int per = 64 / bits;
    const int count = (4096 + per - 1) / per;
    QList<qint64> longs;
    longs.fill(0, count);
    for (int i = 0; i < 4096; ++i)
        longs[i / per] |= (qint64(indices[i]) << ((i % per) * bits));
    return longs;
}

NbtTag makePalette()
{
    const char *names[17] = {
        "minecraft:air", "minecraft:grass_block", "minecraft:water",
        "minecraft:sand", "minecraft:stone", "minecraft:dirt",
        "minecraft:cobblestone", "minecraft:gravel", "minecraft:oak_planks",
        "minecraft:oak_log", "minecraft:oak_leaves", "minecraft:andesite",
        "minecraft:diorite", "minecraft:granite", "minecraft:deepslate",
        "minecraft:bedrock", "minecraft:snow_block"
    };
    QList<NbtTag> items;
    for (int i = 0; i < 17; ++i)
    {
        NbtTag item = NbtCodec::makeCompound();
        item.children.append(NbtCodec::makeString(QStringLiteral("Name"),
                                                  QString::fromLatin1(names[i])));
        items.append(item);
    }
    return NbtCodec::makeList(QStringLiteral("palette"), NbtType::Compound, items);
}

/** 起伏地表 + 斜穿的河 + 分块沙地/石山，让截图看得出是地形 */
QVector<int> buildIndices(int cx, int cz)
{
    QVector<int> indices(4096, 0);
    for (int lz = 0; lz < 16; ++lz)
    {
        for (int lx = 0; lx < 16; ++lx)
        {
            const int wx = cx * 16 + lx;
            const int wz = cz * 16 + lz;

            int surf = 4 + ((wx * 7 + wz * 3) % 8); // y68 ~ y75
            int top = 1;                            // grass
            if (qAbs(wx - wz) <= 2)
            {
                surf = 4;
                top = 2;                            // water 河道更低
            }
            else if ((wx / 8 + wz / 8) % 3 == 0)
                top = 3;                            // sand
            else if (((wx * wz) % 13) == 0)
                top = 4;                            // stone 山

            for (int yy = 0; yy < 16; ++yy)
            {
                int v = 0;
                if (yy < surf)      v = 5;          // 地下 dirt
                else if (yy == surf) v = top;
                indices[yy * 256 + lz * 16 + lx] = v;
            }
        }
    }
    return indices;
}

NbtTag makeChunk(int cx, int cz)
{
    NbtTag states = NbtCodec::makeCompound();
    states.name = QStringLiteral("block_states");
    states.children.append(makePalette());
    states.children.append(NbtCodec::makeLongArray(
        QStringLiteral("data"), packStates(buildIndices(cx, cz), 5)));

    NbtTag section = NbtCodec::makeCompound();
    section.children.append(NbtCodec::makeByte(QStringLiteral("Y"), 4));
    section.children.append(states);

    NbtTag root = NbtCodec::makeCompound();
    root.children.append(NbtCodec::makeInt(QStringLiteral("DataVersion"), 3465));
    root.children.append(NbtCodec::makeInt(QStringLiteral("xPos"), cx));
    root.children.append(NbtCodec::makeInt(QStringLiteral("zPos"), cz));
    root.children.append(NbtCodec::makeList(QStringLiteral("sections"),
                                            NbtType::Compound, { section }));
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

bool writeRegion(const QString &path, const QList<QPair<int, int>> &coords,
                 const QList<NbtTag> &chunks)
{
    QByteArray file;
    file.append(QByteArray(8192, '\0'));

    int sector = 2;
    for (int i = 0; i < coords.size(); ++i)
    {
        const QByteArray comp = zlibCompress(NbtCodec::serialize(chunks[i]));
        if (comp.isEmpty())
            return false;

        QByteArray entry;
        const quint32 len = quint32(comp.size()) + 1;
        entry.append(char((len >> 24) & 0xFF));
        entry.append(char((len >> 16) & 0xFF));
        entry.append(char((len >> 8) & 0xFF));
        entry.append(char(len & 0xFF));
        entry.append(char(2));
        entry.append(comp);
        const int sectors = (entry.size() + 4095) / 4096;
        entry.append(QByteArray(sectors * 4096 - entry.size(), '\0'));

        const int idx = (coords[i].first & 31) + (coords[i].second & 31) * 32;
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

/** 写一个区域文件：full = 铺满 32×32 区块，否则只写局部 2×2 */
bool writeRegionFile(const QString &path, int regionX, int regionZ, bool full)
{
    QList<QPair<int, int>> coords;
    QList<NbtTag> chunks;
    const int span = full ? 32 : 2;
    for (int dz = 0; dz < span; ++dz)
    {
        for (int dx = 0; dx < span; ++dx)
        {
            const int cx = regionX * 32 + dx;
            const int cz = regionZ * 32 + dz;
            coords.append(qMakePair(cx, cz));
            chunks.append(makeChunk(cx, cz));
        }
    }
    return writeRegion(path, coords, chunks);
}

int countColors(const QImage &img, const QRect &area)
{
    QSet<QRgb> colors;
    const QImage sub = img.copy(area);
    for (int y = 0; y < sub.height(); y += 2)
        for (int x = 0; x < sub.width(); x += 2)
            colors.insert(sub.pixel(x, y));
    return colors.size();
}

/**
 * 等后台渲染结束；同时记录"布局发布即切画布"是否先于渲染完成发生 ——
 * 这正是实时（边渲染边显示）渲染的可观测特征。
 */
bool waitRenderDone(WorldMapPage &page, QStackedWidget *stack, bool *sawLiveBeforeDone,
                    int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    if (sawLiveBeforeDone)
        *sawLiveBeforeDone = false;
    while (page.isRendering() && timer.elapsed() < timeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (sawLiveBeforeDone && stack && stack->currentIndex() == 1)
            *sawLiveBeforeDone = true;
    }
    // 收尾：把最后一轮定时器贴图跑完
    for (int i = 0; i < 5; ++i)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    return !page.isRendering();
}

} // namespace

int main(int argc, char **argv)
{
    qInstallMessageHandler(msgHandler);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("world_map_page_probe"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly))
    {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    f.close();
    qApp->setStyleSheet(resolveTokens(raw));

    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    QTemporaryDir tmp;
    const QString savePath = tmp.path() + QStringLiteral("/saves/ProbeWorld");
    expect(writeRegionFile(savePath + "/region/r.0.0.mca", 0, 0, true), "write r.0.0 (full)");
    expect(writeRegionFile(savePath + "/region/r.1.1.mca", 1, 1, false), "write r.1.1");
    // 视口（64 区块、中心 300,200）会盖到 region (1,0)：用来验证"中心先、外围后"
    expect(writeRegionFile(savePath + "/region/r.1.0.mca", 1, 0, false), "write r.1.0");

    // level.dat：出生点 (300, 200)，初始视图应该以它为中心（而不是图像几何中心 512,512）
    {
        NbtTag data = NbtCodec::makeCompound();
        data.name = QStringLiteral("Data");
        data.children.append(NbtCodec::makeInt(QStringLiteral("SpawnX"), 300));
        data.children.append(NbtCodec::makeInt(QStringLiteral("SpawnY"), 64));
        data.children.append(NbtCodec::makeInt(QStringLiteral("SpawnZ"), 200));
        data.children.append(NbtCodec::makeString(QStringLiteral("LevelName"),
                                                  QStringLiteral("ProbeWorld")));
        // 版本 + 种子：结构标记按种子预测（cubiomes），两者缺一都不会算
        {
            NbtTag ver = NbtCodec::makeCompound();
            ver.name = QStringLiteral("Version");
            ver.children.append(NbtCodec::makeString(QStringLiteral("Name"),
                                                     QStringLiteral("1.21.11")));
            data.children.append(ver);
            NbtTag wgs = NbtCodec::makeCompound();
            wgs.name = QStringLiteral("WorldGenSettings");
            wgs.children.append(
                NbtCodec::makeLong(QStringLiteral("seed"), 4562377579810721523LL));
            data.children.append(wgs);
        }
        NbtTag root = NbtCodec::makeCompound();
        root.children.append(data);
        QDir().mkpath(savePath);
        QFile lf(savePath + QStringLiteral("/level.dat"));
        expect(lf.open(QIODevice::WriteOnly | QIODevice::Truncate),
               "write fixture level.dat");
        if (lf.isOpen())
        {
            lf.write(NbtCodec::gzipCompress(NbtCodec::serialize(root)));
            lf.close();
        }
    }

    WorldMapPage page;
    page.resize(1240, 720);
    page.show();
    QApplication::processEvents();

    page.setCurrentSave(savePath, QStringLiteral("ProbeWorld"));
    QStackedWidget *stack = page.findChild<QStackedWidget *>("worldMapStack");
    bool sawLiveBeforeDone = false;
    expect(waitRenderDone(page, stack, &sawLiveBeforeDone, 20000),
           "render finished");
    expect(stack && stack->currentIndex() == 1, "stack -> map view");
    expect(sawLiveBeforeDone, "canvas switched while still rendering (real-time)");
    if (QLabel *status = page.findChild<QLabel *>("worldMapStatus"))
        std::fprintf(stderr, "  status: %s\n", status->text().toUtf8().constData());

    QProgressBar *progress = page.findChild<QProgressBar *>("worldMapProgress");
    expect(progress && !progress->isVisible(), "progress bar hidden after render");

    // 调试：渲染刚完成时的画布状态（定位白屏起始点）
    {
        QWidget *mv = page.findChild<QWidget *>(QStringLiteral("worldMapView"));
        std::fprintf(stderr, "  dbg view vis=%d geo=(%d,%d %dx%d)\n",
                     mv ? int(mv->isVisible()) : -1,
                     mv ? mv->x() : 0, mv ? mv->y() : 0,
                     mv ? mv->width() : 0, mv ? mv->height() : 0);
        if (mv)
            mv->repaint();
        if (mv)
        {
            QImage vimg = mv->grab().toImage();
            vimg.setDevicePixelRatio(1.0);
            vimg.save(QStringLiteral("D:/BlockBox/_out/wmp_dbg_viewonly.png"));
            std::fprintf(stderr, "  dbg view grab %dx%d px(600,270)=%08x\n",
                         vimg.width(), vimg.height(),
                         vimg.rect().contains(QPoint(600, 270))
                             ? vimg.pixel(600, 270) : 0u);
        }
        QImage dbg = page.grab().toImage();
        dbg.setDevicePixelRatio(1.0);
        dbg.save(QStringLiteral("D:/BlockBox/_out/wmp_dbg_renderdone.png"));
        int white = 0, total = 0;
        for (int y = 160; y < 580; y += 8)
            for (int x = 240; x < 940; x += 8)
            {
                ++total;
                const QRgb px = dbg.pixel(x, y);
                if (qRed(px) > 250 && qGreen(px) > 250 && qBlue(px) > 250)
                    ++white;
            }
        std::fprintf(stderr, "  dbg@renderdone white=%d/%d\n", white, total);
    }

    // ── 结构标记：按种子在后台算，等收敛后应有结果 ──
    expect(page.waitForStructs(8000), "structure query converged");
    {
        const int markers = page.structureMarkerCount();
        std::fprintf(stderr, "  structure markers=%d\n", markers);
        expect(markers > 0, "seed-based structure markers present");
    }

    // 顶部操作区控件
    const auto combos = page.findChildren<QComboBox *>("worldMapToolCombo");
    expect(combos.size() == 3, "dimension + shade + height combos");
    const auto buttons = page.findChildren<QPushButton *>();
    expect(buttons.size() >= 7, "toolbar buttons (grid/zoom/fit/goto/export/refresh...)");
    expect(page.findChildren<QLineEdit *>("worldMapCoordEdit").size() == 2,
           "coordinate inputs");

    // ── 第二行：结构开关条（图标 + 默认全选） ──
    const auto structBtns = page.findChildren<QPushButton *>("worldMapStructBtn");
    expect(structBtns.size() == StructureFinder::allStructures().size(),
           "structure bar has one button per structure type");
    expect(StructureFinder::allStructures().size() == 22,
           "structure set covers all 22 types");
    {
        // 本轮补全的结构必须在列（要塞走 StrongholdIter，不走区域网格）
        const char *requiredKeys[] = { "stronghold", "trail_ruins",
                                       "desert_well", "ruined_portal_nether" };
        bool haveAll = true;
        for (const char *k : requiredKeys)
        {
            bool found = false;
            for (const auto &d : StructureFinder::allStructures())
                if (std::strcmp(d.key, k) == 0)
                    found = true;
            if (!found)
                haveAll = false;
        }
        expect(haveAll, "new structure keys present (stronghold/trail/well/nether portal)");
        int rareCount = 0;
        for (const auto &d : StructureFinder::allStructures())
            if (d.rare)
                ++rareCount;
        std::fprintf(stderr, "  rare structures=%d\n", rareCount);
        expect(rareCount >= 6, "rare flags assigned");
    }
    expect(page.structureButtonCount() == structBtns.size(),
           "page tracks structure buttons");
    {
        bool allChecked = true;
        for (QPushButton *b : structBtns)
            if (!b->isChecked())
                allChecked = false;
        expect(allChecked, "all structure toggles checked by default");
        bool pageAllChecked = true;
        for (int i = 0; i < page.structureButtonCount(); ++i)
            if (!page.structureChecked(i))
                pageAllChecked = false;
        expect(pageAllChecked, "page state defaults to all-enabled");
        // 图标应是真实像素（内置徽章或已下载的 Wiki 图），不能是空 pixmap
        bool iconsOk = true;
        for (QPushButton *b : structBtns)
            if (b->icon().pixmap(18, 18).isNull())
                iconsOk = false;
        expect(iconsOk, "structure buttons have icons");

        // 清空 → 标记归零；全选 → 标记恢复（开关真的驱动重算）
        QPushButton *clearBtn = nullptr;
        QPushButton *selectAllBtn = nullptr;
        for (QPushButton *b : page.findChildren<QPushButton *>())
        {
            if (b->text() == QStringLiteral("清空"))
                clearBtn = b;
            else if (b->text() == QStringLiteral("全选"))
                selectAllBtn = b;
        }
        expect(clearBtn && selectAllBtn, "structure bar has 全选/清空 buttons");
        if (clearBtn && selectAllBtn)
        {
            clearBtn->click();
            expect(page.waitForStructs(8000), "clear-all query converged");
            std::fprintf(stderr, "  markers after clear=%d\n",
                         page.structureMarkerCount());
            expect(page.structureMarkerCount() == 0,
                   "clear-all removes all markers");
            bool noneChecked = true;
            for (int i = 0; i < page.structureButtonCount(); ++i)
                if (page.structureChecked(i))
                    noneChecked = false;
            expect(noneChecked, "clear-all unchecks all state");

            selectAllBtn->click();
            expect(page.waitForStructs(8000), "select-all query converged");
            std::fprintf(stderr, "  markers after select-all=%d\n",
                         page.structureMarkerCount());
            expect(page.structureMarkerCount() > 0,
                   "select-all restores markers");
        }
    }

    // ── 图标风格：默认 Icon（文字旁小图标），可切 Overview（结构概览） ──
    expect(StructureIcons::style() == StructureIcons::Style::Icon,
           "icon style defaults to Icon");
    {
        const QString iconDir =
            Platform::getDataDirectory()
            + QStringLiteral("/cache/structure_icons");
        // Icon 风格：远程 EnvSprite 小图标落盘为 <key>.icon.png
        QElapsedTimer dl;
        dl.start();
        int iconFiles = 0;
        while (dl.elapsed() < 15000)
        {
            iconFiles = QDir(iconDir)
                            .entryList(QStringList() << QStringLiteral("*.icon.png"),
                                       QDir::Files)
                            .size();
            if (iconFiles >= StructureFinder::allStructures().size())
                break;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        std::fprintf(stderr, "  icon-style files on disk=%d\n", iconFiles);
        expect(iconFiles >= StructureFinder::allStructures().size(),
               "all EnvSprite icons downloaded (*.icon.png)");

        // 切 Overview：风格生效 + 取图标走 Overview 磁盘缓存（与 Icon 缓存独立）
        StructureIcons::setStyle(StructureIcons::Style::Overview);
        expect(StructureIcons::style() == StructureIcons::Style::Overview,
               "switch to Overview style");
        {
            const auto &defs = StructureFinder::allStructures();
            const auto &d = defs[0];
            const QPixmap ov = StructureIcons::pixmap(
                0, QLatin1String(d.key),
                QString(QChar::fromLatin1(d.displayName[0])),
                QLatin1String(d.wikiTitle), QLatin1String(d.itemTitle),
                QLatin1String(d.envSprite), 18);
            expect(!ov.isNull(), "overview-style pixmap resolvable");
        }

        // Overview 缓存（<key>.png，无 .icon 后缀）与 Icon 缓存并存
        const int overviewFiles =
            QDir(iconDir)
                .entryList(QStringList() << QStringLiteral("*.png"),
                           QDir::Files)
                .size();
        std::fprintf(stderr, "  overview-style files on disk=%d\n",
                     overviewFiles - iconFiles);
        expect(overviewFiles > iconFiles,
               "overview cache stored separately from icon cache");

        // 切回默认 Icon（风格值恢复即可；磁盘两套缓存互不覆盖）
        StructureIcons::setStyle(StructureIcons::Style::Icon);
        expect(StructureIcons::style() == StructureIcons::Style::Icon,
               "switch back to Icon style");
    }

    // ── 初始视图：固定区块数 + 以出生点为中心（打开时不做整图自适应） ──
    QString initialZoomText;
    {
        const auto info = LevelDatEditor::readSave(savePath);
        expect(info.has_value() && info->hasSpawn, "fixture level.dat has spawn");
        if (info.has_value())
            expect(info->spawnX == 300 && info->spawnZ == 200,
                   "spawn read as (300, 200)");

        for (int i = 0; i < 5; ++i)
            QApplication::processEvents(QEventLoop::AllEvents, 20);

        QLabel *initZoomLabel =
            page.findChild<QLabel *>(QStringLiteral("worldMapToolLabel"));
        initialZoomText = initZoomLabel ? initZoomLabel->text() : QString();
        expect(!initialZoomText.isEmpty(), "initial zoom readout exists");
        std::fprintf(stderr, "  initial zoom=%s\n",
                     initialZoomText.toUtf8().constData());

        const QPoint center = page.viewCenterBlock();
        std::fprintf(stderr, "  initial center=(%d, %d)\n", center.x(), center.y());
        expect(std::abs(center.x() - 300) <= 16 && std::abs(center.y() - 200) <= 16,
               "initial view centered on spawn");
        // 64 个区块的视野应比整图自适应更"近"
        expect(WorldMapPage::kInitialVisibleChunks == 64,
               "initial view uses fixed 64-chunk window");
    }

    QApplication::processEvents();
    QImage liveImg = page.mapImage();
    std::fprintf(stderr, "  liveImage null=%d size=(%d,%d)\n",
                 liveImg.isNull(), liveImg.width(), liveImg.height());
    if (!liveImg.isNull())
    {
        const QImage::Format fmt = liveImg.format();
        const int w = liveImg.width(), h = liveImg.height();
        int alphaOdd = 0, opaque = 0;
        QRgb sample[4] = { 0, 0, 0, 0 };
        const QPoint pts[4] = { QPoint(w / 2, h / 2), QPoint(w / 4, h / 4),
                                QPoint(w / 2, h / 4), QPoint(10, 10) };
        for (int i = 0; i < 4; ++i)
            if (liveImg.rect().contains(pts[i]))
                sample[i] = liveImg.pixel(pts[i]);
        for (int y = 0; y < h; y += 16)
            for (int x = 0; x < w; x += 16)
            {
                const int a = qAlpha(liveImg.pixel(x, y));
                if (a != 0 && a != 255)
                    ++alphaOdd;
                if (a == 255)
                    ++opaque;
            }
        std::fprintf(stderr,
                     "  liveImage fmt=%d opaque=%d alphaOdd=%d px=%08x,%08x,%08x,%08x\n",
                     int(fmt), opaque, alphaOdd, sample[0], sample[1],
                     sample[2], sample[3]);
    }
    QImage shot = page.grab().toImage();
    shot.setDevicePixelRatio(1.0);
    const QString out = QStringLiteral("D:/BlockBox/_out/world_map_page_probe.png");
    shot.save(out);

    // 中央画布区域颜色数：确认画出了地形而非空底
    const int colors = countColors(shot, QRect(240, 160, 700, 420));
    std::fprintf(stderr, "  central colors=%d\n", colors);
    expect(colors > 20, "canvas shows terrain (many colors)");

    // ── 结构图标：白描边 / 30px 尺寸 / 点击弹窗 / 完成标记 ──
    {
        QWidget *mapView2 = page.findChild<QWidget *>(QStringLiteral("worldMapView"));
        // 找一个「屏幕可见、且周围 40px 内没有其他标记」的干净标记
        int tgt = -1;
        const int mcount = page.structureMarkerCount();
        for (int i = 0; i < mcount && tgt < 0; ++i)
        {
            const QPoint c = page.markerScreenPos(i);
            if (c.isNull())
                continue;
            bool clean = true;
            for (int j = 0; j < mcount; ++j)
            {
                if (j == i)
                    continue;
                const QPoint o = page.markerScreenPos(j);
                if (o.isNull())
                    continue;
                if (std::abs(o.x() - c.x()) < 44 && std::abs(o.y() - c.y()) < 44)
                {
                    clean = false;
                    break;
                }
            }
            if (clean)
                tgt = i;
        }
        expect(tgt >= 0, "found an isolated visible marker");
        if (tgt >= 0 && mapView2)
        {
            const QPoint c = page.markerScreenPos(tgt);
            const QPoint blk = page.markerBlockPos(tgt);
            const int mtype = page.markerType(tgt);
            std::fprintf(stderr, "  marker[%d] type=%d block=(%d,%d) screen=(%d,%d)\n",
                         tgt, mtype, blk.x(), blk.y(), c.x(), c.y());

            auto snap = [&page]() {
                QImage im = page.grab().toImage();
                im.setDevicePixelRatio(1.0);
                return im;
            };
            auto nearWhite = [](const QImage &im, int x, int y) {
                if (!im.rect().contains(QPoint(x, y)))
                    return false;
                const QRgb px = im.pixel(x, y);
                return qRed(px) >= 240 && qGreen(px) >= 240 && qBlue(px) >= 240;
            };
            auto avg = [](const QImage &im, const QPoint &ctr, int r) {
                qint64 rr = 0, gg = 0, bb = 0;
                int n = 0;
                for (int y = ctr.y() - r; y <= ctr.y() + r; ++y)
                    for (int x = ctr.x() - r; x <= ctr.x() + r; ++x)
                    {
                        if (!im.rect().contains(QPoint(x, y)))
                            continue;
                        const QRgb px = im.pixel(x, y);
                        rr += qRed(px); gg += qGreen(px); bb += qBlue(px);
                        ++n;
                    }
                return n ? QPoint(int(rr / n), int(gg / n)) : QPoint();
            };

            QImage before = snap();
            const QRgb pxOut = before.rect().contains(QPoint(c.x(), c.y() - 19))
                                   ? before.pixel(c.x(), c.y() - 19)
                                   : 0;
            std::fprintf(stderr, "  stroke px(0,-16)=%d px(0,-19)=rgb(%d,%d,%d)\n",
                         nearWhite(before, c.x(), c.y() - 16),
                         qRed(pxOut), qGreen(pxOut), qBlue(pxOut));

            // ① 白描边上沿：图标 30px + 2px 描边 → cy-16 应为白，cy-19 不应为白
            const bool strokeTop = nearWhite(before, c.x(), c.y() - 16);
            const bool outsideTop = !nearWhite(before, c.x(), c.y() - 19);
            std::fprintf(stderr, "  white stroke top=%d outside=%d\n",
                         strokeTop, outsideTop);
            expect(strokeTop, "marker has white stroke at top edge (30px icon)");
            expect(outsideTop, "no white stroke outside the border");

            // ② 点击图标 → 信息弹窗
            {
                const QPointF local(c.x(), c.y());
                const QPointF global = mapView2->mapToGlobal(local.toPoint());
                QMouseEvent press(QEvent::MouseButtonPress, local, global,
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(mapView2, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(mapView2, &release);
            }
            QWidget *popup =
                page.findChild<QWidget *>(QStringLiteral("worldMapStructPopup"));
            expect(popup != nullptr, "clicking marker opens structure popup");
            if (popup)
            {
                const auto &def = StructureFinder::allStructures()[mtype];
                QLabel *title = popup->findChild<QLabel *>(
                    QStringLiteral("worldMapStructTitle"));
                QLabel *coord = popup->findChild<QLabel *>(
                    QStringLiteral("worldMapStructCoord"));
                expect(title && title->text()
                           == QString::fromUtf8(def.displayName),
                       "popup shows structure name");
                const QString coordText = coord ? coord->text() : QString();
                std::fprintf(stderr, "  popup coord: %s\n",
                             coordText.toUtf8().constData());
                expect(coordText.contains(QString::number(blk.x()))
                           && coordText.contains(QString::number(blk.y())),
                       "popup shows structure coordinates");

                // 查看百科：URL 指向 zh.minecraft.wiki（不真点击，只验属性）
                QPushButton *wikiBtn = nullptr;
                QPushButton *copyBtn = nullptr;
                QPushButton *doneBtn = nullptr;
                for (QPushButton *b : popup->findChildren<QPushButton *>())
                {
                    if (b->text() == QStringLiteral("复制坐标"))
                        copyBtn = b;
                    else if (b->text() == QStringLiteral("查看百科"))
                        wikiBtn = b;
                    else if (b->text() == QStringLiteral("标记为已完成")
                             || b->text() == QStringLiteral("取消完成标记"))
                        doneBtn = b;
                }
                expect(copyBtn && wikiBtn && doneBtn,
                       "popup has copy/wiki/done buttons");
                if (wikiBtn)
                {
                    const QUrl u = wikiBtn->property("wikiUrl").toUrl();
                    std::fprintf(stderr, "  wiki url: %s\n",
                                 u.toString().toUtf8().constData());
                    expect(u.isValid()
                               && u.host() == QStringLiteral("zh.minecraft.wiki")
                               && u.path().startsWith(QStringLiteral("/w/")),
                           "wiki url points to zh.minecraft.wiki");
                }

                // 复制坐标 → 剪贴板
                if (copyBtn)
                {
                    copyBtn->click();
                    const QString clip = QApplication::clipboard()->text();
                    const QString want = QStringLiteral("%1, %2")
                                             .arg(blk.x()).arg(blk.y());
                    std::fprintf(stderr, "  clipboard: %s\n",
                                 clip.toUtf8().constData());
                    expect(clip == want, "copy button writes clipboard");
                }

                // 标记为已完成 → 内存状态 + QSettings 持久化 + 按钮文字切换
                const QString dkey = QString::number(mtype) + QLatin1Char('_')
                                   + QString::number(blk.x()) + QLatin1Char('_')
                                   + QString::number(blk.y());
                if (doneBtn)
                {
                    expect(!page.isStructureDone(mtype, blk.x(), blk.y()),
                           "not marked done initially");
                    doneBtn->click();
                    expect(page.isStructureDone(mtype, blk.x(), blk.y()),
                           "done button marks structure done");
                    expect(page.structureDoneCount() == 1, "done count is 1");
                    expect(doneBtn->text() == QStringLiteral("取消完成标记"),
                           "done button text toggles to cancel");
                    QSettings st;
                    const QString skey =
                        QStringLiteral("worldMap/structDone/")
                        + QString::number(qHash(savePath));
                    const QStringList persisted = st.value(skey).toStringList();
                    std::fprintf(stderr, "  persisted: %s\n",
                                 persisted.join(QLatin1Char('|'))
                                     .toUtf8().constData());
                    expect(persisted.contains(dkey),
                           "done flag persisted to QSettings");

                    // 半透明：标记后同区域颜色应明显变化
                    QApplication::processEvents();
                    QImage after = snap();
                    const QPoint p0(c.x(), c.y());
                    auto av0 = avg(before, p0, 6);
                    auto av1 = avg(after, p0, 6);
                    std::fprintf(stderr, "  avg before=(%d,%d) after=(%d,%d)\n",
                                 av0.x(), av0.y(), av1.x(), av1.y());
                    const int diff = std::abs(av0.x() - av1.x())
                                   + std::abs(av0.y() - av1.y());
                    std::fprintf(stderr, "  done pixel diff=%d\n", diff);
                    expect(diff > 10,
                           "marked-done marker drawn semi-transparent");

                    // 取消标记恢复
                    doneBtn->click();
                    expect(!page.isStructureDone(mtype, blk.x(), blk.y()),
                           "cancel removes done flag");
                    expect(page.structureDoneCount() == 0, "done count back to 0");
                }

                popup->close();
                QApplication::processEvents();
            }

            // ③ 缩小到方块不可辨 → 只画稀有结构（不许一个都不画）
            QPushButton *zoomOutBtn = nullptr;
            for (QPushButton *b : page.findChildren<QPushButton *>())
                if (b->text() == QStringLiteral("−"))
                {
                    zoomOutBtn = b;
                    break;
                }
            expect(zoomOutBtn != nullptr, "zoom-out button exists");
            if (zoomOutBtn)
            {
                for (int i = 0; i < 5; ++i)
                {
                    zoomOutBtn->click();
                    for (int k = 0; k < 3; ++k)
                        QApplication::processEvents(QEventLoop::AllEvents, 20);
                }
                QImage tinyShot = snap();
                int rareVisible = 0, rareWithStroke = 0;
                int commonNoStroke = 0;
                const auto &defs2 = StructureFinder::allStructures();
                for (int i = 0; i < mcount; ++i)
                {
                    const QPoint c2 = page.markerScreenPos(i);
                    if (c2.isNull())
                        continue;
                    const int t2 = page.markerType(i);
                    if (t2 < 0 || t2 >= defs2.size())
                        continue;
                    const bool white = nearWhite(tinyShot, c2.x(), c2.y() - 16);
                    if (defs2[t2].rare)
                    {
                        ++rareVisible;
                        if (white)
                            ++rareWithStroke;
                    }
                    else if (!white)
                    {
                        ++commonNoStroke;
                    }
                }
                std::fprintf(stderr,
                             "  zoomed-out markers: rare=%d rareDrawn=%d commonHidden=%d\n",
                             rareVisible, rareWithStroke, commonNoStroke);
                expect(rareVisible == 0 || rareWithStroke > 0,
                       "rare structures still drawn when zoomed out");
                expect(commonNoStroke > 0,
                       "common structures hidden when zoomed out");
                if (zoomOutBtn->text() == QStringLiteral("−"))
                {
                    // 恢复视野，避免影响后续断言
                    for (int i = 0; i < 5; ++i)
                    {
                        QPushButton *zin = nullptr;
                        for (QPushButton *b : page.findChildren<QPushButton *>())
                            if (b->text() == QStringLiteral("+"))
                            {
                                zin = b;
                                break;
                            }
                        if (!zin)
                            break;
                        zin->click();
                        for (int k = 0; k < 3; ++k)
                            QApplication::processEvents(QEventLoop::AllEvents, 20);
                    }
                }
            }
        }
    }

    // 网格开关
    QPushButton *gridBtn = nullptr;
    for (QPushButton *b : buttons)
        if (b->text() == QStringLiteral("网格")) { gridBtn = b; break; }
    expect(gridBtn != nullptr, "grid toggle exists");
    if (gridBtn)
    {
        gridBtn->setChecked(false);
        QApplication::processEvents();
        page.grab().toImage().save(
            QStringLiteral("D:/BlockBox/_out/world_map_page_probe_nogrid.png"));
        gridBtn->setChecked(true);
    }

    // 窄窗口（1000px）下顶部工具栏不应把最右侧按钮挤出去
    page.resize(1000, 640);
    for (int i = 0; i < 4; ++i)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    page.grab().toImage().save(
        QStringLiteral("D:/BlockBox/_out/world_map_page_probe_narrow.png"));
    {
        QPushButton *refreshBtn = nullptr;
        for (QPushButton *b : page.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("刷新")) { refreshBtn = b; break; }
        expect(refreshBtn != nullptr, "refresh button exists");
        if (refreshBtn)
        {
            const int right = refreshBtn->mapTo(&page,
                                                QPoint(refreshBtn->width(), 0)).x();
            std::fprintf(stderr, "  toolbar right edge @1000px = %d (page %d)\n",
                         right, page.width());
            expect(right <= page.width() - 8, "toolbar fits in 1000px width");
        }
    }

    // ── 鼠标交互：单击选中 / 中键平移 / 右键菜单 / 双击放大 ──
    page.resize(1240, 720);
    page.setFocus(Qt::OtherFocusReason);
    for (int i = 0; i < 3; ++i)
        QApplication::processEvents(QEventLoop::AllEvents, 20);

    QWidget *mapView = page.findChild<QWidget *>(QStringLiteral("worldMapView"));
    QLabel *statusLabel = page.findChild<QLabel *>(QStringLiteral("worldMapStatus"));
    QLabel *zoomLabel = page.findChild<QLabel *>(QStringLiteral("worldMapToolLabel"));
    expect(mapView != nullptr, "map view exists");
    expect(statusLabel != nullptr, "status label exists");

    if (mapView)
    {
        const QPointF center(mapView->width() / 2.0, mapView->height() / 2.0);
        const QPointF global = mapView->mapToGlobal(center);

        // 单击（按下-释放、无位移）→ 选中该区块
        {
            QMouseEvent press(QEvent::MouseButtonPress, center, global,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(mapView, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, center, global,
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(mapView, &release);
        }
        expect(statusLabel
                   && statusLabel->text().contains(QStringLiteral("已选中区块")),
               "click selects chunk (status shows selection)");
        QApplication::processEvents(); // 让 pending 的 label 重绘先落地再截图
        page.grab().toImage().save(
            QStringLiteral("D:/BlockBox/_out/world_map_page_probe_selected.png"));

        // 中键按下 → 进入抓手平移态；松开退出
        {
            QMouseEvent press(QEvent::MouseButtonPress, center, global,
                              Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
            QApplication::sendEvent(mapView, &press);
            const bool grabbed =
                mapView->cursor().shape() == Qt::ClosedHandCursor;
            QMouseEvent release(QEvent::MouseButtonRelease, center, global,
                                Qt::MiddleButton, Qt::MiddleButton,
                                Qt::NoModifier);
            QApplication::sendEvent(mapView, &release);
            expect(grabbed, "middle button enters pan mode");
            expect(mapView->cursor().shape() != Qt::ClosedHandCursor,
                   "middle release leaves pan mode");
        }

        // 右键 → 上下文菜单（非模态 popup，点不到也不会卡死）
        {
            QMouseEvent press(QEvent::MouseButtonPress, center, global,
                              Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            QApplication::sendEvent(mapView, &press);
            QMenu *menu = nullptr;
            for (QMenu *m : page.findChildren<QMenu *>())
                if (m->isVisible()) { menu = m; break; }
            expect(menu != nullptr, "right click opens context menu");
            if (menu)
                menu->close(); // WA_DeleteOnClose → 随后自删
            QApplication::processEvents();
        }

        // 双击 → 以点击点为锚放大
        if (zoomLabel)
        {
            const QString before = zoomLabel->text();
            QMouseEvent dbl(QEvent::MouseButtonDblClick, center, global,
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(mapView, &dbl);
            QApplication::processEvents();
            expect(zoomLabel->text() != before, "double click zooms in");
        }
    }

    // ── 1 像素 = 1 方块：自动补视口细节瓦片 + 1:1 按钮 ──
    {
        QElapsedTimer t;
        t.start();
        while (page.loadedDetailTiles() == 0 && t.elapsed() < 8000)
            QApplication::processEvents(QEventLoop::AllEvents, 30);
        std::fprintf(stderr, "  detail tiles loaded=%d\n",
                     page.loadedDetailTiles());
        expect(page.loadedDetailTiles() > 0,
               "1:1 detail tiles load for visible region");

        // 第一块证明"从中心开始"；再等外围补完，才能验证"向外扩散"
        {
            QElapsedTimer t2;
            t2.start();
            while (page.loadedDetailTiles() < 2 && t2.elapsed() < 3000)
                QApplication::processEvents(QEventLoop::AllEvents, 30);
        }

        // 从视口中心往四周：第一块必须是中心所在的 region，且距离单调不减
        const QList<QPoint> tileOrder = page.loadedTileOrder();
        std::fprintf(stderr, "  tile order:");
        for (const QPoint &k : tileOrder)
            std::fprintf(stderr, " (%d,%d)", k.x(), k.y());
        std::fprintf(stderr, "\n");
        expect(!tileOrder.isEmpty(), "tile load order recorded");
        if (!tileOrder.isEmpty())
        {
            const QPoint cb = page.viewCenterBlock();
            const QPoint cRegion(int(std::floor(double(cb.x()) / 512.0)),
                                 int(std::floor(double(cb.y()) / 512.0)));
            std::fprintf(stderr, "  center region=(%d,%d)\n",
                         cRegion.x(), cRegion.y());
            expect(tileOrder.first() == cRegion,
                   "first detail tile is the viewport-center region");

            auto chess = [&cRegion](const QPoint &p) {
                return std::max(std::abs(p.x() - cRegion.x()),
                                std::abs(p.y() - cRegion.y()));
            };
            bool monotonic = true;
            for (int i = 1; i < tileOrder.size(); ++i)
                if (chess(tileOrder[i]) < chess(tileOrder[i - 1]))
                    monotonic = false;
            expect(monotonic, "tiles load outward (distance non-decreasing)");
        }

        QPushButton *oneOne = nullptr;
        for (QPushButton *b : page.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("1:1")) { oneOne = b; break; }
        expect(oneOne != nullptr, "1:1 button exists");
        if (oneOne && zoomLabel)
        {
            const QString before = zoomLabel->text();
            oneOne->click();
            QApplication::processEvents();
            expect(zoomLabel->text() == QStringLiteral("100%"),
                   "1:1 button reports 100% (1px = 1 block)");
            expect(zoomLabel->text() != before, "1:1 changes zoom readout");
            std::fprintf(stderr, "  zoom readout: %s -> %s\n",
                         before.toUtf8().constData(),
                         zoomLabel->text().toUtf8().constData());
        }
    }

    // ── 高度设置：可编辑选择框（下拉选预设 / 直接键入自定义 Y） ──
    {
        QComboBox *height = nullptr;
        for (QComboBox *c : page.findChildren<QComboBox *>())
            if (c->isEditable()) { height = c; break; }
        expect(height != nullptr, "height combo exists");
        if (height)
        {
            expect(height->lineEdit() != nullptr,
                   "height combo has an inline input box");
            expect(height->currentText().contains(QStringLiteral("全部")),
                   "height default = unlimited");

            const QImage before = page.mapImage();
            expect(!before.isNull(), "map image available");

            // 下拉选 Y=64：fixture 地表在 y=71，被削到 y≤64 → 只剩下面的石头
            height->setCurrentIndex(1);
            waitRenderDone(page, page.findChild<QStackedWidget *>("worldMapStack"),
                           nullptr, 8000);
            const QImage cut = page.mapImage();
            if (!before.isNull() && !cut.isNull())
            {
                const QRgb b = before.pixel(0, 0);
                const QRgb c = cut.pixel(0, 0);
                std::fprintf(stderr, "  Y=64 (0,0): rgb(%d,%d,%d) -> rgb(%d,%d,%d)\n",
                             qRed(b), qGreen(b), qBlue(b),
                             qRed(c), qGreen(c), qBlue(c));
                expect(c != b, "Y=64 changes surface (cuts blocks above)");
                expect(qAlpha(c) == 255, "Y=64 still shows the surface below");
                expect(qBlue(c) <= qRed(c) + 40,
                       "cut surface is not the water that sat above");
            }

            // 直接在输入框里键入自定义 Y（回车 → editingFinished）
            height->lineEdit()->setText(QStringLiteral("100"));
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(height->lineEdit(), &enter);
            waitRenderDone(page, page.findChild<QStackedWidget *>("worldMapStack"),
                           nullptr, 8000);
            const QImage typed = page.mapImage();
            if (!before.isNull() && !typed.isNull())
                expect(typed.pixel(0, 0) == before.pixel(0, 0),
                       "typed Y=100 restores the true surface");

            height->setCurrentIndex(0); // 收尾恢复「全部」
            waitRenderDone(page, page.findChild<QStackedWidget *>("worldMapStack"),
                           nullptr, 8000);
        }
    }

    // ── 快捷键（焦点在页面子树内才生效；焦点落在坐标框时由输入框消费） ──
    auto sendKey = [&](int key) {
        QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(&page, &ev);
        QApplication::processEvents();
    };

    {
        QPushButton *grid = nullptr;
        for (QPushButton *b : page.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("网格")) { grid = b; break; }
        if (grid)
        {
            const bool before = grid->isChecked();
            sendKey(Qt::Key_G);
            expect(grid->isChecked() != before, "G toggles grid");
        }
    }
    if (zoomLabel)
    {
        QString before = zoomLabel->text();
        sendKey(Qt::Key_Plus);
        expect(zoomLabel->text() != before, "+ zooms in");

        before = zoomLabel->text();
        sendKey(Qt::Key_Minus);
        expect(zoomLabel->text() != before, "- zooms out");

        // 先放大离开自适应态，再按 0 拉回 —— 这样文本必然变化
        sendKey(Qt::Key_Plus);
        QApplication::processEvents();
        before = zoomLabel->text();
        sendKey(Qt::Key_0);
        expect(zoomLabel->text() != before, "0 fits to window");
        // 打开时是 64 区块视野，按 0 适应整图后读数必然不同 → 验证默认不是全览
        expect(zoomLabel->text() != initialZoomText,
               "default view is not fit-to-overview");
    }
    {
        sendKey(Qt::Key_C);
        expect(statusLabel
                   && !statusLabel->text().contains(QStringLiteral("已选中区块")),
               "C clears selection");
    }
    {
        QComboBox *dim = page.findChild<QComboBox *>("worldMapToolCombo");
        QLabel *subtitle = page.findChild<QLabel *>("worldMapSubtitle");
        if (dim)
        {
            sendKey(Qt::Key_2);
            expect(dim->currentIndex() == 1, "2 switches to nether");
            expect(subtitle && subtitle->text().contains(QStringLiteral("下界")),
                   "subtitle follows dimension");
            waitRenderDone(page, page.findChild<QStackedWidget *>("worldMapStack"),
                           nullptr, 8000);
            sendKey(Qt::Key_1);
            expect(dim->currentIndex() == 0, "1 switches back to overworld");
            waitRenderDone(page, page.findChild<QStackedWidget *>("worldMapStack"),
                           nullptr, 8000);
        }
    }

    // 空存档 → 空状态提示
    WorldMapPage emptyPage;
    emptyPage.resize(1240, 720);
    emptyPage.show();
    QApplication::processEvents();
    const QString emptyDir = tmp.path() + QStringLiteral("/saves/EmptyWorld");
    QDir().mkpath(emptyDir);
    emptyPage.setCurrentSave(emptyDir, QStringLiteral("EmptyWorld"));
    waitRenderDone(emptyPage, emptyPage.findChild<QStackedWidget *>("worldMapStack"),
                   nullptr, 4000);
    QApplication::processEvents();
    QLabel *emptyLabel = emptyPage.findChild<QLabel *>("worldMapEmpty");
    expect(emptyLabel && !emptyLabel->text().isEmpty(), "empty save shows hint");
    emptyPage.grab().toImage().save(
        QStringLiteral("D:/BlockBox/_out/world_map_page_probe_empty.png"));

    expect(g_parseWarnings == 0, "no QSS parse warnings");

    std::fprintf(stderr, "failures: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
