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
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QScrollArea>
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
#include "utils/WorldMap/SeedTerrainRenderer.h"

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
        {"@BG_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.42)"},
        {"@BORDER_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.55)"},
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
    // 最后一项是矿石：给"矿物列 + 扫描"用例埋一粒钻石（palette 18 项仍需 5 bit）
    const char *names[18] = {
        "minecraft:air", "minecraft:grass_block", "minecraft:water",
        "minecraft:sand", "minecraft:stone", "minecraft:dirt",
        "minecraft:cobblestone", "minecraft:gravel", "minecraft:oak_planks",
        "minecraft:oak_log", "minecraft:oak_leaves", "minecraft:andesite",
        "minecraft:diorite", "minecraft:granite", "minecraft:deepslate",
        "minecraft:bedrock", "minecraft:snow_block", "minecraft:diamond_ore"
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
                // 出生点旁边埋 32×32 粒钻石矿（世界 y=65，地表以下，不影响地表配色）：
                // 供矿物扫描用例在 1:1 下扫出点
                if (yy == 1 && wx >= 288 && wx < 320 && wz >= 192 && wz < 224)
                    v = 17;
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

    // 既有像素/布局断言建立在"无种子预测"的画面上；先关掉开关，
    // 流程末尾再单独开一段验证种子预测本身
    {
        QSettings bootSettings;
        bootSettings.setValue(QStringLiteral("worldMap/seedPreview"), false);
    }

    // ── 诊断入口：直接打开真实存档（PROBE_SAVE_DIR=存档目录，PROBE_SAVE_SEED=1 打开种子预测） ──
    const QString realSave = qEnvironmentVariable("PROBE_SAVE_DIR");
    if (!realSave.isEmpty())
    {
        if (qEnvironmentVariableIsSet("PROBE_SAVE_SEED"))
        {
            QSettings st;
            st.setValue(QStringLiteral("worldMap/seedPreview"), true);
        }
        WorldMapPage p;
        p.resize(1240, 720);
        p.show();
        QApplication::processEvents();
        p.setCurrentSave(realSave, QStringLiteral("RealSave"));
        expect(waitRenderDone(p,
                              p.findChild<QStackedWidget *>(QStringLiteral("worldMapStack")),
                              nullptr, 180000),
               "real save render finished");
        const QImage img = p.mapImage();
        const int bpp = p.liveBpp();
        const QPoint org = p.liveOriginRegion();
        const QList<QPoint> order = p.tileArrivalOrder();
        const QPoint vc = p.viewCenterBlock();
        const QPoint pri = p.renderPriorityRegion();
        std::fprintf(stderr,
                     "  real save: img=%dx%d bpp=%d origin=%d,%d tiles=%d priority=%d,%d viewCenter=%lld,%lld\n",
                     img.width(), img.height(), bpp, org.x(), org.y(),
                     int(order.size()), pri.x(), pri.y(),
                     (long long)vc.x(), (long long)vc.y());
        QStringList head;
        for (int i = 0; i < order.size() && i < 12; ++i)
            head.append(QStringLiteral("%1,%2").arg(order[i].x()).arg(order[i].y()));
        std::fprintf(stderr, "  arrival: [%s]\n", head.join(' ').toUtf8().constData());
        if (!img.isNull() && bpp > 0)
        {
            const int tile = 512 / bpp;
            const int cols = img.width() / tile;
            const int rows = img.height() / tile;
            auto opaqueIn = [&](int col, int row) {
                if (col < 0 || row < 0 || col >= cols || row >= rows)
                    return -1;
                int n = 0;
                for (int y = 0; y < tile; ++y)
                    for (int x = 0; x < tile; ++x)
                        if (qAlpha(img.pixel(col * tile + x, row * tile + y)) != 0)
                            ++n;
                return n;
            };
            const int cCol = pri.x() - org.x();
            const int cRow = pri.y() - org.y();
            int empty = 0;
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                    if (opaqueIn(c, r) == 0)
                        ++empty;
            std::fprintf(stderr,
                         "  tiles=%dx%d empty=%d | priority tile opaque=%d, up=%d down=%d left=%d right=%d\n",
                         cols, rows, empty, opaqueIn(cCol, cRow),
                         opaqueIn(cCol, cRow - 1), opaqueIn(cCol, cRow + 1),
                         opaqueIn(cCol - 1, cRow), opaqueIn(cCol + 1, cRow));
        }
        p.grab().save(QStringLiteral("D:/BlockBox/_out/real_save.png"));

        // 可选的"跳到指定方块再看一眼"：PROBE_SAVE_JUMP=x,z
        const QString jump = qEnvironmentVariable("PROBE_SAVE_JUMP");
        if (!jump.isEmpty())
        {
            const QStringList parts = jump.split(QLatin1Char(','));
            if (parts.size() == 2)
            {
                p.toggleContentPopup();
                QApplication::processEvents();
                const QList<QLineEdit *> edits =
                    p.findChildren<QLineEdit *>(QStringLiteral("worldMapCoordEdit"));
                if (edits.size() == 2)
                {
                    edits[0]->setText(parts[0].trimmed());
                    edits[1]->setText(parts[1].trimmed());
                }
                for (QPushButton *b : p.findChildren<QPushButton *>())
                {
                    if (b->text() != QStringLiteral("跳转") || !b->parentWidget())
                        continue;
                    if (b->parentWidget()->objectName()
                        != QStringLiteral("worldMapContentPopup"))
                        continue;
                    b->click();
                    break;
                }
                QApplication::processEvents();
                p.toggleContentPopup();
                QElapsedTimer jt;
                jt.start();
                while (jt.elapsed() < 4000)
                {
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(15);
                }
                const QPoint jc = p.viewCenterBlock();
                std::fprintf(stderr, "  jumped: viewCenter=%lld,%lld\n",
                             (long long)jc.x(), (long long)jc.y());
                p.grab().save(QStringLiteral("D:/BlockBox/_out/real_save_jump.png"));
                const QImage jimg = p.mapImage();
                jimg.save(QStringLiteral("D:/BlockBox/_out/real_save_jump_map.png"));
            }
        }

        // 缩到整图（scale<1）再刷新：抓三帧看渐进贴图在屏幕上是否真的刷新出来
        QPushButton *fitBtn = nullptr;
        for (QPushButton *b : p.findChildren<QPushButton *>())
        {
            if (b->text() == QStringLiteral("适应"))
            {
                fitBtn = b;
                break;
            }
        }
        if (fitBtn)
        {
            fitBtn->click();
            QApplication::processEvents();
            p.refresh();
            for (int i = 0; i < 3; ++i)
            {
                QElapsedTimer ft;
                ft.start();
                while (ft.elapsed() < 500)
                {
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(15);
                }
                const QImage shot = p.grab().toImage();
                shot.save(QStringLiteral("D:/BlockBox/_out/fit_growth_%1.png").arg(i));
                p.mapImage().save(QStringLiteral("D:/BlockBox/_out/fit_map_%1.png").arg(i));
                // 屏幕上画布区域里"有内容"的像素数（排除背景色），看渐进刷新
                int lit = 0;
                for (int y = 160; y < shot.height() - 40; y += 4)
                    for (int x = 20; x < shot.width() - 20; x += 4)
                    {
                        const QRgb c = shot.pixel(x, y);
                        const int r = qRed(c), g = qGreen(c), b2 = qBlue(c);
                        if (qAbs(r - g) > 6 || qAbs(g - b2) > 6)
                            ++lit;
                    }
                std::fprintf(stderr, "  fit frame %d: lit=%d tiles=%d\n", i, lit,
                             int(p.tileArrivalOrder().size()));
            }
        }
        // 诊断：直接导出若干 region 的种子预测贴图（PROBE_SEED_SNAP=1），
        // 用来判断"麻点"是概览采样（64）还是方块级（512）本身的问题
        if (qEnvironmentVariableIsSet("PROBE_SEED_SNAP"))
        {
            const quint64 snapSeed = 4562377579810721523ULL;
            const int snapMc = StructureFinder::versionToInt(QStringLiteral("1.21.11"));
            for (int rz = 0; rz < 2; ++rz)
                for (int rx = 0; rx < 2; ++rx)
                {
                    QElapsedTimer clock;
                    clock.start();
                    const QImage lo = SeedTerrainRenderer::renderRegionTile(
                        rx, rz, WorldMapRenderer::Dimension::Overworld,
                        WorldMapRenderer::ShadeMode::BlockColor, snapSeed, snapMc, 64);
                    const qint64 loMs = clock.elapsed();
                    clock.restart();
                    const QImage hi = SeedTerrainRenderer::renderRegionTile(
                        rx, rz, WorldMapRenderer::Dimension::Overworld,
                        WorldMapRenderer::ShadeMode::BlockColor, snapSeed, snapMc, 512);
                    const qint64 hiMs = clock.elapsed();
                    lo.save(QStringLiteral("D:/BlockBox/_out/seed_%1_%2_lo.png").arg(rx).arg(rz));
                    hi.save(QStringLiteral("D:/BlockBox/_out/seed_%1_%2_hi.png").arg(rx).arg(rz));
                    std::fprintf(stderr, "  seed snap %d,%d lo=%dx%d hi=%dx%d (%lld ms / %lld ms)\n",
                                 rx, rz, lo.width(), lo.height(), hi.width(), hi.height(),
                                 (long long)loMs, (long long)hiMs);
                }
        }

        std::fprintf(stderr, "failures: %d\n", g_fail);
        return g_fail == 0 ? 0 : 1;
    }

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

    // ── 渲染器层复现：多 region 世界的渐进顺序 + 中心是否留洞 ──
    {
        const QString bigSave = tmp.path() + QStringLiteral("/saves/RenderProbe");
        for (int rz = -4; rz <= 4; ++rz)
            for (int rx = -4; rx <= 4; ++rx)
                writeRegionFile(bigSave + QStringLiteral("/region/r.%1.%2.mca")
                                              .arg(rx).arg(rz),
                                rx, rz, false);
        auto rsink = std::make_shared<WorldMapRenderer::ProgressiveSink>();
        rsink->hasPriorityRegion = true;
        rsink->priorityRegion = QPoint(4, 4);
        const WorldMapRenderer::RenderResult rres = WorldMapRenderer::render(
            bigSave, WorldMapRenderer::Dimension::Overworld,
            WorldMapRenderer::ShadeMode::BlockColor, {}, {}, rsink, 4, -1,
            WorldMapRenderer::SeedPreview(), WorldMapRenderer::PriorCanvas());
        const int minRx = rres.minChunkX / 32;
        const int minRz = rres.minChunkZ / 32;
        const int rbpp = rres.blockPerPixel;
        const int tilePx = 512 / rbpp;
        const QPoint centerDest((4 - minRx) * tilePx, (4 - minRz) * tilePx);
        std::fprintf(stderr,
                     "  render: regions=%d failed=%d bpp=%d img=%dx%d tiles=%d\n",
                     rres.regionCount, rres.failedRegions, rbpp,
                     rres.image.width(), rres.image.height(),
                     int(rsink->pendingTiles.size()));
        expect(!rsink->pendingTiles.isEmpty(), "progressive sink got tiles");
        // 4 个渲染线程并发：只保证优先级 region 排在最前、落在首批完成里
        int centerTileIdx = -1;
        for (int i = 0; i < rsink->pendingTiles.size() && i < 8; ++i)
            if (rsink->pendingTiles.at(i).first == centerDest)
                centerTileIdx = i;
        std::fprintf(stderr, "  first tile dest=%d,%d center dest=%d,%d center idx=%d\n",
                     rsink->pendingTiles.first().first.x(),
                     rsink->pendingTiles.first().first.y(),
                     centerDest.x(), centerDest.y(), centerTileIdx);
        expect(centerTileIdx >= 0, "priority region is in the first batch of tiles");
        // 每 region 的 2×2 区块补丁落在 region 左上角；取补丁内一点看是否真有像素
        const int off = 16 / rbpp;
        const QRgb cc = rres.image.pixel(centerDest.x() + off,
                                         centerDest.y() + off);
        std::fprintf(stderr, "  center pixel a=%d rgb=%d,%d,%d\n",
                     qAlpha(cc), qRed(cc), qGreen(cc), qBlue(cc));
        expect(qAlpha(cc) == 255, "center region rendered (no hole)");

        // 同样的世界，但打开种子预测（应用里的常态）：走 merge 分支
        auto ssink = std::make_shared<WorldMapRenderer::ProgressiveSink>();
        ssink->hasPriorityRegion = true;
        ssink->priorityRegion = QPoint(4, 4);
        WorldMapRenderer::SeedPreview sp;
        sp.enabled = true;
        sp.seed = 4562377579810721523ULL;
        sp.mc = StructureFinder::versionToInt(QStringLiteral("1.21.11"));
        sp.hasCenter = true;
        sp.centerRegionX = 4;
        sp.centerRegionZ = 4;
        sp.radiusRegions = 20;
        const WorldMapRenderer::RenderResult sres = WorldMapRenderer::render(
            bigSave, WorldMapRenderer::Dimension::Overworld,
            WorldMapRenderer::ShadeMode::BlockColor, {}, {}, ssink, 4, -1, sp,
            WorldMapRenderer::PriorCanvas());
        const int sminRx = sres.minChunkX / 32;
        const int sminRz = sres.minChunkZ / 32;
        const int stile = 512 / sres.blockPerPixel;
        const QPoint sCenter((4 - sminRx) * stile, (4 - sminRz) * stile);
        const QPoint sFirst = ssink->pendingTiles.isEmpty()
            ? QPoint(-1, -1) : ssink->pendingTiles.first().first;
        std::fprintf(stderr,
                     "  seed render: regions=%d bpp=%d tiles=%d first=%d,%d center=%d,%d\n",
                     sres.regionCount, sres.blockPerPixel,
                     int(ssink->pendingTiles.size()),
                     sFirst.x(), sFirst.y(), sCenter.x(), sCenter.y());
        int sCenterIdx = -1;
        for (int i = 0; i < ssink->pendingTiles.size() && i < 8; ++i)
            if (ssink->pendingTiles[i].first == sCenter)
            {
                sCenterIdx = i;
                break;
            }
        expect(sCenterIdx >= 0, "seed branch: priority region is in the first batch");
        const int soff = 16 / sres.blockPerPixel;
        const QRgb sc = sres.image.pixel(sCenter.x() + soff, sCenter.y() + soff);
        std::fprintf(stderr, "  seed center pixel a=%d rgb=%d,%d,%d\n",
                     qAlpha(sc), qRed(sc), qGreen(sc), qBlue(sc));
        expect(qAlpha(sc) == 255, "seed branch: center region rendered (no hole)");
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

    // ── 底部「当前群系」：渲染 + 结构查询后应算出中文名（占位符 "—" 需被替换）──
    if (QLabel *biomeLabel = page.findChild<QLabel *>("worldMapBiome"))
    {
        QElapsedTimer biomeWait;
        biomeWait.start();
        while (biomeLabel->text().contains(QStringLiteral("—"))
               && biomeWait.elapsed() < 3000)
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        std::fprintf(stderr, "  biome: %s\n",
                     biomeLabel->text().toUtf8().constData());
        expect(!biomeLabel->text().contains(QStringLiteral("—")),
               "current biome shown in chinese");
    }
    else
    {
        expect(false, "biome label exists");
    }

    QProgressBar *progress = page.findChild<QProgressBar *>("worldMapProgress");
    expect(progress && !progress->isVisible(), "progress bar hidden after render");

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
    expect(buttons.size() >= 7, "toolbar buttons (grid/zoom/fit/export/expand...)");
    expect(page.findChildren<QLineEdit *>("worldMapCoordEdit").isEmpty(),
           "coordinate inputs moved into expand popup (not on toolbar)");

    // ── 着色下拉：新增第三项「地形」（海拔分层设色 + 山体阴影）──
    if (combos.size() == 3)
    {
        QComboBox *shade = combos[1];
        expect(shade->count() == 3, "shade combo has 3 modes");
        expect(shade->itemText(2) == QStringLiteral("地形"),
               "third shade mode is terrain");

        shade->setCurrentIndex(2);
        waitRenderDone(page, page.findChild<QStackedWidget *>("worldMapStack"),
                       nullptr, 20000);
        const QImage terr = page.mapImage();
        if (!terr.isNull())
        {
            const QRgb p = terr.pixel(0, 0);
            expect(qAlpha(p) == 255, "terrain mode renders the surface");
            expect(!(qRed(p) == qGreen(p) && qGreen(p) == qBlue(p)),
                   "terrain mode is colored (not height-gray)");
            QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));
            terr.save(QStringLiteral("D:/BlockBox/_out/wmp_terrain_mode.png"));
        }
        else
        {
            expect(false, "terrain mode map image not null");
        }

        shade->setCurrentIndex(0); // 收尾恢复「地表方块」，不干扰后续断言
        waitRenderDone(page, page.findChild<QStackedWidget *>("worldMapStack"),
                       nullptr, 20000);
    }

    // ── 第二行：结构开关条（图标 + 默认全选） ──
    const auto structBtns = page.findChildren<QPushButton *>("worldMapStructBtn");
    expect(structBtns.size() == StructureFinder::allStructures().size(),
           "structure bar has one button per structure type");
    expect(StructureFinder::allStructures().size() == 26,
           "structure set covers all 26 types");
    {
        // 本轮补全的结构必须在列（要塞走 StrongholdIter，不走区域网格）
        const char *requiredKeys[] = { "stronghold", "trail_ruins",
                                       "desert_well", "ruined_portal_nether",
                                       "end_gateway", "abandoned_camp",
                                       "nether_fossil", "end_ship" };
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
        expect(haveAll,
               "all structure keys present incl. gateway/camp/fossil/ship");
        // 不可计算的三（cubiomes 无算法）→ 按钮禁用但仍在列
        int unsupported = 0, unsupportedDisabled = 0;
        for (int i = 0; i < StructureFinder::allStructures().size(); ++i)
        {
            if (StructureFinder::allStructures()[i].type
                    != StructureFinder::kUnsupportedType)
                continue;
            ++unsupported;
            if (i < structBtns.size() && !structBtns[i]->isEnabled())
                ++unsupportedDisabled;
        }
        std::fprintf(stderr, "  unsupported=%d disabled=%d\n", unsupported,
                     unsupportedDisabled);
        expect(unsupported == 3 && unsupportedDisabled == 3,
               "unsupported structures listed but disabled");
        // 末地折跃门可计算 → 按钮可用
        for (int i = 0; i < StructureFinder::allStructures().size(); ++i)
            if (StructureFinder::allStructures()[i].type
                    == StructureFinder::kStrongholdType
                || std::strcmp(StructureFinder::allStructures()[i].key,
                               "end_gateway") == 0)
                expect(i < structBtns.size() && structBtns[i]->isEnabled(),
                       "computable structure button enabled");
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

    // ── 密集同类型图标合并：距 <30px 并成一个，右上角画 ×N 深色角标 ──
    {
        const int mcount = page.structureMarkerCount();
        QWidget *mapView0 = page.findChild<QWidget *>(QStringLiteral("worldMapView"));
        QVector<QPoint> denseMid;
        for (int i = 0; i < mcount && denseMid.size() < 5; ++i)
        {
            const QPoint a = page.markerScreenPos(i);
            if (a.isNull())
                continue;
            for (int j = i + 1; j < mcount; ++j)
            {
                if (page.markerType(j) != page.markerType(i))
                    continue;
                const QPoint b = page.markerScreenPos(j);
                if (b.isNull())
                    continue;
                const int dx = a.x() - b.x();
                const int dy = a.y() - b.y();
                if (dx * dx + dy * dy < 30 * 30)
                {
                    denseMid.append(QPoint((a.x() + b.x()) / 2,
                                           (a.y() + b.y()) / 2));
                    break;
                }
            }
        }
        std::fprintf(stderr, "  dense same-type groups sampled=%d\n",
                     int(denseMid.size()));
        expect(!denseMid.isEmpty(),
               "viewport has dense same-type marker pairs to merge");
        if (!denseMid.isEmpty() && mapView0)
        {
            QImage shot = page.grab().toImage();
            shot.setDevicePixelRatio(1.0);
            const QPoint origin = mapView0->mapTo(&page, QPoint(0, 0));
            int badged = 0;
            for (const QPoint &m : denseMid)
            {
                // 角标在合并图标右上角：中心附近偏右上扫一小块找深色像素
                int dark = 0;
                for (int y = m.y() - 22; y <= m.y() - 6; ++y)
                    for (int x = m.x() + 6; x <= m.x() + 24; ++x)
                    {
                        const QPoint q = origin + QPoint(x, y);
                        if (!shot.rect().contains(q))
                            continue;
                        const QRgb px = shot.pixel(q);
                        if (qRed(px) < 70 && qGreen(px) < 70 && qBlue(px) < 80)
                            ++dark;
                    }
                if (dark >= 6)
                    ++badged;
            }
            std::fprintf(stderr, "  merged groups with badge=%d/%d\n",
                         badged, int(denseMid.size()));
            expect(badged > 0, "dense merged icon shows dark ×N badge");
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
            // markerScreenPos 是 view 内坐标；页面截图要加上 view 在页面里的偏移
            const QPoint origin = mapView2->mapTo(&page, QPoint(0, 0));
            const QPoint cp = c + origin;
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
            const QRgb pxOut = before.rect().contains(QPoint(cp.x(), cp.y() - 19))
                                   ? before.pixel(cp.x(), cp.y() - 19)
                                   : 0;
            std::fprintf(stderr, "  stroke px(0,-16)=%d px(0,-19)=rgb(%d,%d,%d)\n",
                         nearWhite(before, cp.x(), cp.y() - 16),
                         qRed(pxOut), qGreen(pxOut), qBlue(pxOut));

            // ① 白描边上沿：图标 30px + 2px 描边 → cy-16 应为白，cy-19 不应为白
            const bool strokeTop = nearWhite(before, cp.x(), cp.y() - 16);
            const bool outsideTop = !nearWhite(before, cp.x(), cp.y() - 19);
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
                    const QPoint p0(cp.x(), cp.y());
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
                    const bool white =
                        nearWhite(tinyShot, c2.x() + origin.x(),
                                  c2.y() + origin.y() - 16);
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
    std::fprintf(stderr, "  page width after resize(1000) = %d\n",
                 page.width());
    // 结构条在横向滚动容器里：窄屏内容放不下时必须可横滚（而非撑爆布局）
    {
        QScrollArea *structScroll = page.findChild<QScrollArea *>(
            QStringLiteral("worldMapStructScroll"));
        expect(structScroll != nullptr, "structure bar has scroll container");
        if (structScroll)
        {
            const int maxH = structScroll->horizontalScrollBar()->maximum();
            std::fprintf(stderr, "  struct scroll max@1000px = %d\n", maxH);
            expect(maxH > 0, "narrow width: structure bar scrollable");
        }
    }
    page.grab().toImage().save(
        QStringLiteral("D:/BlockBox/_out/world_map_page_probe_narrow.png"));
    {
        QPushButton *actionBtn = nullptr;
        for (QPushButton *b : page.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("导出")) { actionBtn = b; break; }
        expect(actionBtn != nullptr, "export button exists (refresh moved to top bar)");
        if (actionBtn)
        {
            const int right = actionBtn->mapTo(&page,
                                               QPoint(actionBtn->width(), 0)).x();
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

    // ── 种子预测开关（fixture 有种子 + 版本 → 可用） ──
    {
        QPushButton *seedBtn =
            page.findChild<QPushButton *>(QStringLiteral("worldMapSeedBtn"));
        expect(seedBtn != nullptr, "seed preview toggle exists");
        if (seedBtn)
        {
            expect(seedBtn->isEnabled(), "seed toggle enabled (save has seed)");
            expect(!seedBtn->isChecked(),
                   "seed preview starts off (probe boot setting)");
            const QImage before = page.mapImage();
            expect(!before.isNull(), "map image before seed toggle");

            seedBtn->setChecked(true); // toggled → 重渲
            expect(waitRenderDone(page, stack, nullptr, 20000),
                   "render after enabling seed preview");
            QApplication::processEvents();

            const QImage after = page.mapImage();
            expect(!after.isNull(), "map image after seed toggle");
            // 开启后包围盒外扩 pad 圈：图像必然变大
            expect(after.width() > before.width()
                       && after.height() > before.height(),
                   "seed preview expands map bounds");
            if (statusLabel)
            {
                std::fprintf(stderr, "  seed-on status: %s\n",
                             statusLabel->text().toUtf8().constData());
                expect(statusLabel->text().contains(QStringLiteral("种子预测")),
                       "status reports seed region count");
            }
            page.grab().toImage().save(
                QStringLiteral("D:/BlockBox/_out/world_map_page_probe_seed.png"));

            // 关回去：画布缩回原范围，开关状态持久化
            seedBtn->setChecked(false);
            expect(waitRenderDone(page, stack, nullptr, 20000),
                   "render after disabling seed preview");
            const QImage back = page.mapImage();
            expect(!back.isNull() && back.width() == before.width()
                       && back.height() == before.height(),
                   "disabling seed preview restores bounds");
            QSettings st;
            expect(st.value(QStringLiteral("worldMap/seedPreview"), true)
                       .toBool() == false,
                   "seed preview state persisted");
        }
    }

    // ── 内容浏览弹窗：展开按钮 / 搜索 / 结构+群系双列 / 高亮跳转 ──
    {
        QPushButton *expandBtn = page.findChild<QPushButton *>(
            QStringLiteral("worldMapExpandBtn"));
        expect(expandBtn != nullptr, "second row has expand button");
        if (expandBtn)
        {
            expect(!page.isContentPopupOpen(), "popup starts closed");
            expandBtn->click();
            QApplication::processEvents();
            expect(page.isContentPopupOpen(), "expand opens content popup");

            QWidget *popup = page.findChild<QWidget *>(
                QStringLiteral("worldMapContentPopup"));
            QLineEdit *search = page.findChild<QLineEdit *>(
                QStringLiteral("worldMapPopupSearch"));
            QListWidget *sList = page.findChild<QListWidget *>(
                QStringLiteral("worldMapPopupList")); // 第一个（结构）
            expect(popup != nullptr, "popup widget exists");
            expect(search != nullptr, "popup has search box");
            expect(page.findChildren<QLineEdit *>(
                       QStringLiteral("worldMapCoordEdit")).size() == 2,
                   "popup has X and Z coordinate inputs at its top");
            expect(sList != nullptr && sList->count() > 0,
                   "popup structure list populated");
            // 样式截图：弹窗浮层 + 勾选态（部分结构默认勾选即可见）
            QApplication::processEvents();
            page.grab().toImage().save(
                QStringLiteral("D:/BlockBox/_out/world_map_page_probe_popup.png"));
            // 三列：结构列表 + 群系列表 + 矿物列表各一个
            const auto lists = page.findChildren<QListWidget *>(
                QStringLiteral("worldMapPopupList"));
            expect(lists.size() == 3, "popup has structure + biome + ore columns");
            if (lists.size() == 3)
            {
                std::fprintf(stderr, "  popup rows: struct=%d biome=%d ore=%d\n",
                             lists[0]->count(), lists[1]->count(), lists[2]->count());
                expect(lists[0]->count()
                           == StructureFinder::allStructures().size(),
                       "structure column lists all types");
                expect(lists[1]->count() > 0, "biome column lists biomes");
                expect(lists[2]->count() > 0, "ore column lists ores");
                // 每行 = 图标 + 名称 + 高亮按钮
                bool rowShapeOk = lists[0]->count() > 0;
                if (rowShapeOk)
                {
                    auto *w = lists[0]->itemWidget(lists[0]->item(0));
                    rowShapeOk = w && w->findChild<QPushButton *>(
                                          QStringLiteral("worldMapPopupToggle"))
                                      && w->findChild<QPushButton *>(
                                             QStringLiteral(
                                                 "worldMapPopupHighlight"));
                }
                expect(rowShapeOk,
                       "list row = toggle (icon+name) + highlight btn");
            }
            expect(page.biomeRowCount() > 0, "biome row count exposed");
            bool anyBiomeChecked = false;
            for (int i = 0; i < page.biomeRowCount(); ++i)
                if (page.biomeChecked(i))
                    anyBiomeChecked = true;
            expect(!anyBiomeChecked, "biomes default to all-deselected");

            // 两列各自的全选 / 清空按钮
            expect(page.findChild<QPushButton *>(
                       QStringLiteral("worldMapPopupStructAll"))
                       && page.findChild<QPushButton *>(
                              QStringLiteral("worldMapPopupStructClear")),
                   "structure column has select-all/clear");
            expect(page.findChild<QPushButton *>(
                       QStringLiteral("worldMapPopupBiomeAll"))
                       && page.findChild<QPushButton *>(
                              QStringLiteral("worldMapPopupBiomeClear")),
                   "biome column has select-all/clear");
            expect(page.findChild<QPushButton *>(
                       QStringLiteral("worldMapPopupOreAll"))
                       && page.findChild<QPushButton *>(
                              QStringLiteral("worldMapPopupOreClear")),
                   "ore column has select-all/clear");

            // 搜索过滤三列
            if (search)
            {
                search->setText(QStringLiteral("村庄"));
                QApplication::processEvents();
                const auto filtered = page.findChildren<QListWidget *>(
                    QStringLiteral("worldMapPopupList"));
                expect(filtered.size() == 3 && filtered[0]->count() == 1,
                       "search filters structure list");
                search->clear();
                QApplication::processEvents();
            }

            // 群系叠加：默认无选中 → 无格子；全选后 → 有格子；清空 → 归零
            expect(page.waitForStructs(10000), "struct/biome query converged");
            expect(page.biomeOverlayCellCount() == 0,
                   "no overlay when biomes all-deselected");
            if (auto *biomeAll = page.findChild<QPushButton *>(
                    QStringLiteral("worldMapPopupBiomeAll")))
            {
                biomeAll->click();
                expect(page.waitForStructs(10000),
                       "biome select-all query converged");
                const int cells = page.biomeOverlayCellCount();
                std::fprintf(stderr, "  biome overlay cells=%d\n", cells);
                expect(cells > 0, "biome select-all produces overlay cells");
                bool allChecked = true;
                for (int i = 0; i < page.biomeRowCount(); ++i)
                    if (!page.biomeChecked(i))
                        allChecked = false;
                expect(allChecked, "biome select-all checks all rows");
            }
            if (auto *biomeClear = page.findChild<QPushButton *>(
                    QStringLiteral("worldMapPopupBiomeClear")))
            {
                biomeClear->click();
                QApplication::processEvents();
                expect(page.biomeOverlayCellCount() == 0,
                       "biome clear empties overlay");
            }

            // ── 矿物列 + 扫描：缩得不够大只提示"要放大"，1:1 才真扫出矿物点 ──
            {
                QPushButton *oneOne = nullptr;
                QPushButton *zoomOut = nullptr;
                for (QPushButton *b : page.findChildren<QPushButton *>())
                {
                    if (b->text() == QStringLiteral("1:1"))
                        oneOne = b;
                    else if (b->text() == QString(QChar(0x2212))) // "−" 缩小
                        zoomOut = b;
                }
                expect(oneOne && zoomOut, "zoom controls found for ore scan");

                expect(page.oreRowCount() > 0, "ore row count exposed");
                bool anyOreChecked = false;
                for (int i = 0; i < page.oreRowCount(); ++i)
                    if (page.oreChecked(i))
                        anyOreChecked = true;
                expect(!anyOreChecked, "ores default to all-deselected");
                expect(page.oreMarkerCount() == 0, "no ore dots before selection");

                // 先回到 1:1 再退两档 → 每方块不到 1 像素
                if (oneOne)
                    oneOne->click();
                if (zoomOut)
                {
                    zoomOut->click();
                    QApplication::processEvents();
                    zoomOut->click();
                }
                QApplication::processEvents();

                // 主世界矿物列第 6 行 = 钻石（coal,iron,copper,gold,redstone,lapis,diamond,emerald）
                const int diamondRow = 6;
                expect(page.toggleOre(diamondRow), "click diamond ore row");
                expect(page.oreChecked(diamondRow), "ore row becomes checked");
                // 勾选后 320ms 去抖才调度；缩放不够时应当不扫、只提示
                QElapsedTimer smallTimer;
                smallTimer.start();
                while (smallTimer.elapsed() < 1200)
                    QApplication::processEvents(QEventLoop::AllEvents, 30);
                std::fprintf(stderr, "  ore prompt (zoomed out): %s\n",
                             page.orePromptText().toUtf8().constData());
                expect(page.oreMarkerCount() == 0, "no ore dots while zoomed out");
                expect(page.orePromptText().contains(QStringLiteral("放大")),
                       "prompt asks to zoom in before scanning");
                page.grab().toImage().save(
                    QStringLiteral("D:/BlockBox/_out/world_map_page_probe_ore_prompt.png"));

                // 放大回 1:1 → 扫描当前视野的 region，命中就在画布上打点
                // （先收起弹窗，让截图能看见地图上的矿物点）
                expandBtn->click();
                QApplication::processEvents();
                expect(!page.isContentPopupOpen(), "popup closed for ore dot screenshot");
                if (oneOne)
                    oneOne->click();
                QApplication::processEvents();
                QElapsedTimer scanTimer;
                scanTimer.start();
                while (scanTimer.elapsed() < 30000 && page.oreMarkerCount() == 0)
                    QApplication::processEvents(QEventLoop::AllEvents, 50);
                std::fprintf(stderr, "  ore dots=%d prompt=%s\n",
                             page.oreMarkerCount(),
                             page.orePromptText().toUtf8().constData());
                expect(page.oreMarkerCount() > 0, "1:1 scan marks ore dots");
                page.grab().toImage().save(
                    QStringLiteral("D:/BlockBox/_out/world_map_page_probe_ore_dots.png"));

                // 区块扫描要能收敛：扫完当前视野的区块后状态栏不再是"扫描中"
                QElapsedTimer idleTimer;
                idleTimer.start();
                while (idleTimer.elapsed() < 20000 && !page.oreScanIdle())
                    QApplication::processEvents(QEventLoop::AllEvents, 50);
                std::fprintf(stderr, "  ore scan idle=%d chunks=%d (%lld ms)\n",
                             int(page.oreScanIdle()), page.oreScannedChunkCount(),
                             static_cast<long long>(idleTimer.elapsed()));
                expect(page.oreScanIdle(), "ore scan finishes over visible chunks");
                const QByteArray chunkMsg =
                    QStringLiteral("chunk-level cache populated (%1 chunks)")
                        .arg(page.oreScannedChunkCount()).toUtf8();
                expect(page.oreScannedChunkCount() > 1, chunkMsg.constData());
                expect(!page.orePromptText().contains(QStringLiteral("扫描中")),
                       "status stops saying scanning after finishing");
                expandBtn->click(); // 弹窗打开着继续后面的用例
                QApplication::processEvents();
                expect(page.isContentPopupOpen(), "popup reopened after ore scan");

                // 点矿物点 → 与结构一致的信息窗（名称 / 坐标带 Y / 百科 / 完成标记）
                {
                    QWidget *mv = page.findChild<QWidget *>(
                        QStringLiteral("worldMapView"));
                    expect(mv != nullptr, "map view found for ore click");
                    // 找一个视野内的矿物点（密集矿脉里挑第一个）
                    int target = -1;
                    for (int i = 0; i < page.oreMarkerCount() && target < 0; ++i)
                        if (!page.oreMarkerScreenPos(i).isNull())
                            target = i;
                    expect(target >= 0, "found a visible ore dot");
                    if (mv && target >= 0)
                    {
                        const QPoint local = page.oreMarkerScreenPos(target);
                        const QPoint blk = page.oreMarkerBlockPos(target);
                        const int by = page.oreMarkerBlockY(target);
                        const int otype = page.oreMarkerType(target);
                        const QPointF lf(local.x(), local.y());
                        const QPointF gf = mv->mapToGlobal(local);
                        QMouseEvent press(QEvent::MouseButtonPress, lf, gf,
                                          Qt::LeftButton, Qt::LeftButton,
                                          Qt::NoModifier);
                        QApplication::sendEvent(mv, &press);
                        QMouseEvent release(QEvent::MouseButtonRelease, lf, gf,
                                            Qt::LeftButton, Qt::LeftButton,
                                            Qt::NoModifier);
                        QApplication::sendEvent(mv, &release);
                        QApplication::processEvents();

                        QWidget *popup = page.findChild<QWidget *>(
                            QStringLiteral("worldMapStructPopup"));
                        expect(popup != nullptr && popup->isVisible(),
                               "clicking ore dot opens info popup");
                        if (popup)
                        {
                            const auto &odef = OreFinder::allOres()[otype];
                            QLabel *title = popup->findChild<QLabel *>(
                                QStringLiteral("worldMapStructTitle"));
                            QLabel *coord = popup->findChild<QLabel *>(
                                QStringLiteral("worldMapStructCoord"));
                            const QString coordText = coord ? coord->text() : QString();
                            std::fprintf(stderr,
                                         "  ore popup: title=%s coord=%s\n",
                                         title ? title->text().toUtf8().constData() : "",
                                         coordText.toUtf8().constData());
                            expect(title && title->text()
                                       == QString::fromUtf8(odef.displayName),
                                   "ore popup shows ore name");
                            expect(coordText.contains(QStringLiteral("Y"))
                                       && coordText.contains(QString::number(by)),
                                   "ore popup shows ore Y coordinate");
                            expect(coordText.contains(QString::number(blk.x()))
                                       && coordText.contains(QString::number(blk.y())),
                                   "ore popup shows ore X/Z");
                            QPushButton *wikiBtn = nullptr;
                            QPushButton *copyBtn = nullptr;
                            QPushButton *doneBtn = nullptr;
                            for (QPushButton *b : popup->findChildren<QPushButton *>())
                            {
                                if (b->text() == QStringLiteral("复制坐标"))
                                    copyBtn = b;
                                else if (b->text() == QStringLiteral("查看百科"))
                                    wikiBtn = b;
                                else if (b->text().contains(QStringLiteral("完成")))
                                    doneBtn = b;
                            }
                            expect(copyBtn && wikiBtn && doneBtn,
                                   "ore popup has copy/wiki/done buttons");
                            if (wikiBtn)
                            {
                                const QUrl u = wikiBtn->property("wikiUrl").toUrl();
                                expect(u.isValid()
                                           && u.host() == QStringLiteral("zh.minecraft.wiki"),
                                       "ore wiki url points to zh.minecraft.wiki");
                            }
                            if (copyBtn)
                            {
                                copyBtn->click();
                                expect(QApplication::clipboard()->text()
                                           == QStringLiteral("%1, %2, %3")
                                                  .arg(blk.x()).arg(by).arg(blk.y()),
                                       "ore copy writes x,y,z");
                            }
                            if (doneBtn)
                            {
                                const int doneBefore = page.structureDoneCount();
                                doneBtn->click();
                                expect(page.structureDoneCount() == doneBefore + 1,
                                       "ore done button records a mark");
                                expect(doneBtn->text() == QStringLiteral("取消完成标记"),
                                       "ore done button text toggles");
                                doneBtn->click(); // 复原，免得影响后面的结构用例
                                expect(page.structureDoneCount() == doneBefore,
                                       "ore done toggle undone");
                            }
                            popup->grab().toImage().save(
                                QStringLiteral("D:/BlockBox/_out/"
                                               "world_map_page_probe_ore_popup.png"));
                            popup->close();
                            QApplication::processEvents();
                        }
                    }
                }

                // 清空 → 点全消失
                if (auto *oreClear = page.findChild<QPushButton *>(
                        QStringLiteral("worldMapPopupOreClear")))
                {
                    oreClear->click();
                    QApplication::processEvents();
                    expect(page.oreMarkerCount() == 0, "ore clear removes dots");
                }
            }

            // 高亮跳转：结构（村庄）→ 视角中心变化 + 高亮圈
            {
                const QPoint before = page.viewCenterBlock();
                int villageIdx = -1;
                for (int i = 0; i < StructureFinder::allStructures().size(); ++i)
                    if (std::strcmp(StructureFinder::allStructures()[i].key,
                                    "village") == 0)
                        villageIdx = i;
                expect(villageIdx >= 0, "village type present");
                if (villageIdx >= 0)
                {
                    expect(page.highlightStructureType(villageIdx),
                           "highlight village accepted");
                    // 视口内有村庄时立即跳转；没有则后台搜（等待收敛）
                    QElapsedTimer ht;
                    ht.start();
                    while (ht.elapsed() < 8000)
                    {
                        QApplication::processEvents(QEventLoop::AllEvents, 30);
                        // 视口内有村庄时立即跳转；没有则后台搜（等中心变化）
                        if (ht.elapsed() > 200
                            && page.viewCenterBlock() != before)
                            break;
                    }
                    page.grab().toImage().save(
                        QStringLiteral("D:/BlockBox/_out/"
                                       "world_map_page_probe_highlight.png"));
                }
            }

            // 关闭方式：× 按钮 → 关闭；Esc → 关闭；再点展开 → 开，点弹窗外 → 关
            if (auto *closeBtn = page.findChild<QPushButton *>(
                    QStringLiteral("worldMapPopupClose")))
            {
                closeBtn->click();
                QApplication::processEvents();
                expect(!page.isContentPopupOpen(), "close button closes popup");
            }
            expandBtn->click();
            QApplication::processEvents();
            expect(page.isContentPopupOpen(), "expand reopens popup");
            {
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape,
                              Qt::NoModifier);
                QApplication::sendEvent(popup, &esc);
                QApplication::processEvents();
                expect(!page.isContentPopupOpen(), "Esc closes popup");
            }
            expandBtn->click();
            QApplication::processEvents();
            expect(page.isContentPopupOpen(), "expand opens popup again");
            // 模拟点弹窗外（点地图左上角 —— 弹窗居中在画布中央，角落一定在外）
            if (mapView)
            {
                const QPointF outside(8, 8);
                QMouseEvent press(QEvent::MouseButtonPress, outside,
                                  mapView->mapToGlobal(outside),
                                  Qt::LeftButton, Qt::LeftButton,
                                  Qt::NoModifier);
                QApplication::sendEvent(mapView, &press);
                QApplication::processEvents();
                expect(!page.isContentPopupOpen(),
                       "click outside closes popup");
            }
            // 展开按钮 toggle：开 → 关
            expandBtn->click();
            QApplication::processEvents();
            expect(page.isContentPopupOpen(), "expand toggles open");
            expandBtn->click();
            QApplication::processEvents();
            expect(!page.isContentPopupOpen(), "expand toggles closed");
        }
    }

    // ── 概览重渲的渐进起步点 = 视野中心所在 region（不是出生点） ──
    {
        const QList<QLineEdit *> coords =
            page.findChildren<QLineEdit *>(QStringLiteral("worldMapCoordEdit"));
        expect(coords.size() == 2, "goto coord edits exist");
        QPushButton *gotoBtn = nullptr;
        const QList<QPushButton *> allBtns = page.findChildren<QPushButton *>();
        for (QPushButton *b : allBtns)
        {
            if (b->text() == QStringLiteral("跳转") && b->parentWidget()
                && b->parentWidget()->objectName() == QStringLiteral("worldMapContentPopup"))
            {
                gotoBtn = b;
                break;
            }
        }
        expect(gotoBtn != nullptr, "goto button found in content popup");
        if (gotoBtn && coords.size() == 2)
        {
            // 出生点 (300,200) 在 region (0,0)；跳到 (700,700) 即 region (1,1)
            coords[0]->setText(QStringLiteral("700"));
            coords[1]->setText(QStringLiteral("700"));
            gotoBtn->click();
            QApplication::processEvents();
            const QPoint c = page.viewCenterBlock();
            expect(std::abs(c.x() - 700) <= 64 && std::abs(c.y() - 700) <= 64,
                   "goto moved view center to (700,700)");

            page.refresh();
            const QPoint pri = page.renderPriorityRegion();
            expect(pri == QPoint(1, 1),
                   qPrintable(QStringLiteral("re-render starts from view center region, got %1,%2")
                                  .arg(pri.x()).arg(pri.y())));
            expect(waitRenderDone(page,
                                  page.findChild<QStackedWidget *>(QStringLiteral("worldMapStack")),
                                  nullptr, 20000),
                   "re-render from view center finished");

            // 收尾：回到出生点附近，后面的用例不受影响
            coords[0]->setText(QStringLiteral("300"));
            coords[1]->setText(QStringLiteral("200"));
            gotoBtn->click();
            QApplication::processEvents();
        }
    }

    // ── 页面层复现：开着种子预测重渲时，中心 region 到底有没有先显示 ──
    {
        const QString bigSave = tmp.path() + QStringLiteral("/saves/RenderProbe");
        QFile::copy(savePath + QStringLiteral("/level.dat"),
                    bigSave + QStringLiteral("/level.dat"));
        QPushButton *seedBtn = page.findChild<QPushButton *>(QStringLiteral("worldMapSeedBtn"));
        expect(seedBtn != nullptr, "seed preview button found");
        // 先落在一张 9×9 region 的画布上（种子关），再打开种子预测触发重渲
        page.reset();
        page.setCurrentSave(bigSave, QStringLiteral("RenderProbe"));
        expect(waitRenderDone(page,
                              page.findChild<QStackedWidget *>(QStringLiteral("worldMapStack")),
                              nullptr, 20000),
               "big world rendered before seed toggle");
        if (seedBtn)
        {
            const QPoint viewCenter = page.viewCenterBlock();
            const QPoint expectPri(int(std::floor(double(viewCenter.x()) / 512.0)),
                                   int(std::floor(double(viewCenter.y()) / 512.0)));
            seedBtn->click(); // 打开种子预测 → 以视野中心重渲
            // 抓一张生长途中的截图，再等渲染完成直接读图块到达顺序
            QElapsedTimer shotTimer;
            shotTimer.start();
            while (shotTimer.elapsed() < 800)
            {
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(20);
            }
            page.grab().save(QStringLiteral("D:/BlockBox/_out/probe_center_growth.png"));
            expect(waitRenderDone(page,
                                  page.findChild<QStackedWidget *>(QStringLiteral("worldMapStack")),
                                  nullptr, 30000),
                   "seed re-render finished");
            const QList<QPoint> order = page.tileArrivalOrder();
            std::fprintf(stderr, "  seed-on: view center region=%d,%d tiles=%d\n",
                         expectPri.x(), expectPri.y(), int(order.size()));
            QStringList head;
            for (int i = 0; i < order.size() && i < 12; ++i)
                head.append(QStringLiteral("%1,%2").arg(order[i].x()).arg(order[i].y()));
            std::fprintf(stderr, "  growth order: [%s]\n",
                         head.join(QStringLiteral(" ")).toUtf8().constData());
            expect(!order.isEmpty(), "progressive growth observed");
            // 16 个渲染线程并发 + 机器负载，完成顺序只保证"靠前"：
            // 排序正确性由渲染器层用例断言，这里只要求中心没被排到队尾。
            const int centerIdx = order.indexOf(expectPri);
            const int batch = qMax(16, order.size() / 4);
            std::fprintf(stderr, "  center tile arrival index=%d (batch %d)\n",
                         centerIdx, batch);
            expect(centerIdx >= 0 && centerIdx < batch,
                   qPrintable(QStringLiteral("view center region %1,%2 arrives early, index=%3")
                                  .arg(expectPri.x()).arg(expectPri.y()).arg(centerIdx)));
            const QImage finalImg = page.mapImage();
            const int fbpp = page.liveBpp();
            if (!finalImg.isNull() && fbpp > 0)
            {
                const QPoint org = page.liveOriginRegion();
                const int tile = 512 / fbpp;
                const int off = qMax(1, 16 / fbpp);
                const int px = (expectPri.x() - org.x()) * tile + off;
                const int py = (expectPri.y() - org.y()) * tile + off;
                const QRgb fc = finalImg.pixel(px, py);
                std::fprintf(stderr, "  final center pixel a=%d\n", qAlpha(fc));
                expect(qAlpha(fc) == 255, "center region present in final image");
            }
            seedBtn->click(); // 关回种子预测
            QApplication::processEvents();
        }
    }

    expect(g_parseWarnings == 0, "no QSS parse warnings");

    std::fprintf(stderr, "failures: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
