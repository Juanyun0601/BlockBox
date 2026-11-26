/**
 * @file   MapExportDialog.cpp
 * @brief  存档地图「高级导出」弹窗与导出图合成器实现
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "MapExportDialog.h"

#include "utils/StyleKit.h"
#include "utils/WorldMap/StructureFinder.h"

#include <QCheckBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QPolygonF>
#include <QVBoxLayout>

#include <utility>

// ═══════════════════════ 导出图合成器 ═══════════════════════
//
// 与 WorldMapView 的画布绘制同一套视觉（结构图标白底、彩色图钉、区块网格），
// 但坐标基准换成了"导出图像素"：一个方块占 1/bpp 像素，不再有视口和缩放。
// 只用 QImage / QPainter / QFont，不碰任何控件 —— 可以放在工作线程执行。

namespace {

/** 未生成区块的底色兜底（正常应与画布的 kVoidColor 同值传入） */
const QColor kFallbackVoid(0x31, 0x34, 0x3A);
/** 导出图上的网格线（与画布同色） */
const QColor kChunkGrid(255, 255, 255, 28);
const QColor kRegionGrid(255, 255, 255, 78);
/** 信息栏 / 指北针的半透明深底与文字 */
const QColor kBadgeBg(20, 22, 26, 185);
/** 结构图标边长与白底描边（与画布 kMarkerSize / kMarkerStroke 一致） */
constexpr int kMarkerSize = 30;
constexpr int kMarkerStroke = 2;
/** 用户图钉圆头直径（与画布 kUserPinSize 一致） */
constexpr int kUserPinSize = 24;

/** 一个待合并绘制的结构标记（导出空间） */
struct ComposeMarkerGroup
{
    QPointF center;
    int type = -1;
    int count = 1;
    bool allDone = false;
};

} // namespace

namespace MapExportComposer {

QImage compose(const MapExportComposeInput &in)
{
    const QImage &base = in.base;
    if (base.isNull())
        return QImage();

    QImage out;
    if (in.png)
    {
        // PNG 保留未生成区块的透明像素
        out = base.convertToFormat(QImage::Format_ARGB32);
    }
    else
    {
        // JPG 没有透明通道：先铺底色再把底图压上去
        out = QImage(base.size(), QImage::Format_RGB32);
        out.fill(in.voidColor.isValid() ? in.voidColor : kFallbackVoid);
        QPainter flatten(&out);
        flatten.drawImage(0, 0, base);
        flatten.end();
    }

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    const int W = out.width();
    const int H = out.height();
    const double bpp = qMax(1, in.bpp);
    const double bpx = 1.0 / bpp; // 一个方块占几个导出像素（画布的 blockScreenSize）

    /** 方块坐标 → 导出图像素坐标 */
    auto pxX = [&](qint64 block) { return double(block - in.minBlockX) / bpp; };
    auto pxZ = [&](qint64 block) { return double(block - in.minBlockZ) / bpp; };

    // ── 区块 / 区域网格（与画布同款：区块细线，区域粗一档） ──
    if (in.grid)
    {
        const double chunkPx = 16.0 / bpp;
        if (chunkPx >= 7.0)
        {
            p.setPen(QPen(kChunkGrid, 1.0));
            const qint64 c0x = qint64(std::floor(in.minBlockX / 16.0));
            const qint64 c1x = qint64(std::ceil((in.minBlockX + W * bpp) / 16.0));
            for (qint64 cx = c0x; cx <= c1x; ++cx)
            {
                const double x = pxX(cx * 16);
                p.drawLine(QPointF(x, 0), QPointF(x, H));
            }
            const qint64 c0z = qint64(std::floor(in.minBlockZ / 16.0));
            const qint64 c1z = qint64(std::ceil((in.minBlockZ + H * bpp) / 16.0));
            for (qint64 cz = c0z; cz <= c1z; ++cz)
            {
                const double y = pxZ(cz * 16);
                p.drawLine(QPointF(0, y), QPointF(W, y));
            }
        }
        const double regionPx = 512.0 / bpp;
        if (regionPx >= 18.0)
        {
            p.setPen(QPen(kRegionGrid, 1.0));
            const qint64 r0x = qint64(std::floor(in.minBlockX / 512.0));
            const qint64 r1x = qint64(std::ceil((in.minBlockX + W * bpp) / 512.0));
            for (qint64 rx = r0x; rx <= r1x; ++rx)
            {
                const double x = pxX(rx * 512);
                p.drawLine(QPointF(x, 0), QPointF(x, H));
            }
            const qint64 r0z = qint64(std::floor(in.minBlockZ / 512.0));
            const qint64 r1z = qint64(std::ceil((in.minBlockZ + H * bpp) / 512.0));
            for (qint64 rz = r0z; rz <= r1z; ++rz)
            {
                const double y = pxZ(rz * 512);
                p.drawLine(QPointF(0, y), QPointF(W, y));
            }
        }
    }

    // ── 矿物点：小方块（密集时不描边，与画布同款公式） ──
    if (in.ores && !in.ann.ores.isEmpty())
    {
        const bool dense = bpx < 2.0;
        const double sz = dense ? qBound(2.0, 1.6 * bpx, 3.0)
                                : qBound(4.0, 1.2 * bpx, 10.0);
        const double half = sz / 2.0;
        p.setPen(dense ? Qt::NoPen : QPen(QColor(0, 0, 0, 130), 1.0));
        for (const auto &m : in.ann.ores)
        {
            const double x = pxX(m.x);
            const double y = pxZ(m.z);
            if (x < -sz || y < -sz || x > W + sz || y > H + sz)
                continue;
            p.setBrush(m.ore >= 0 && m.ore < in.ann.oreColors.size()
                           ? QColor(in.ann.oreColors.at(m.ore))
                           : QColor(0x9e, 0x9e, 0x9e));
            p.drawRect(QRectF(x - half, y - half, sz, sz));
        }
        p.setBrush(Qt::NoBrush);
    }

    // ── 结构标记：同类型挤在一起的合并成一个 + ×N 角标（与画布同款） ──
    if (in.structures && !in.ann.structures.isEmpty())
    {
        const auto &defs = StructureFinder::allStructures();
        const bool tiny = bpx < 0.5; // 方块小到不可辨：只画稀有结构
        // 单链接合并：中心距小于一个图标边长即并入同组
        QVector<ComposeMarkerGroup> groups;
        QVector<bool> used(in.ann.structures.size(), false);
        const double mergeDist2 = double(kMarkerSize) * double(kMarkerSize);
        for (int i = 0; i < in.ann.structures.size(); ++i)
        {
            if (used[i] || in.ann.structures[i].type < 0)
                continue;
            used[i] = true;
            ComposeMarkerGroup g;
            g.type = in.ann.structures[i].type;
            QVector<QPointF> pts;
            pts.append(QPointF(pxX(in.ann.structures[i].x),
                               pxZ(in.ann.structures[i].z)));
            bool allDone = in.ann.structures[i].done;
            for (int j = i + 1; j < in.ann.structures.size(); ++j)
            {
                if (used[j] || in.ann.structures[j].type != g.type)
                    continue;
                const QPointF cj(pxX(in.ann.structures[j].x),
                                 pxZ(in.ann.structures[j].z));
                bool near = false;
                for (const QPointF &pk : pts)
                {
                    const double dx = cj.x() - pk.x();
                    const double dy = cj.y() - pk.y();
                    if (dx * dx + dy * dy < mergeDist2)
                    {
                        near = true;
                        break;
                    }
                }
                if (!near)
                    continue;
                used[j] = true;
                pts.append(cj);
                allDone = allDone && in.ann.structures[j].done;
            }
            double sx = 0, sy = 0;
            for (const QPointF &q : pts)
            {
                sx += q.x();
                sy += q.y();
            }
            g.center = QPointF(sx / pts.size(), sy / pts.size());
            g.count = pts.size();
            g.allDone = allDone;
            groups.append(g);
        }

        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        const int half = kMarkerSize / 2 + kMarkerStroke;
        for (const ComposeMarkerGroup &g : groups)
        {
            if (g.type < 0 || g.type >= defs.size() || g.type >= in.ann.structureIcons.size())
                continue;
            const auto &d = defs.at(g.type);
            if (tiny && !d.rare)
                continue;
            const QPointF &c = g.center;
            if (c.x() < -40 || c.y() < -40 || c.x() > W + 40 || c.y() > H + 40)
                continue;
            const QImage &icon = in.ann.structureIcons.at(g.type);
            p.setOpacity(g.allDone ? 0.35 : 1.0);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawRoundedRect(QRectF(c.x() - half, c.y() - half,
                                     half * 2.0, half * 2.0), 7, 7);
            if (!icon.isNull())
                p.drawImage(QPointF(std::round(c.x() - icon.width() / 2.0),
                                    std::round(c.y() - icon.height() / 2.0)),
                            icon);

            if (g.count >= 2)
            {
                QFont f = p.font();
                f.setPixelSize(10);
                f.setBold(true);
                p.setFont(f);
                const QString badge = QStringLiteral("×%1").arg(g.count);
                const QFontMetrics fm(f);
                const int bw = fm.horizontalAdvance(badge) + 8;
                const int bh = fm.height() + 2;
                const QRectF br(c.x() + half - bw + 4, c.y() - half - bh + 4,
                                bw, bh);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(28, 30, 34, 230));
                p.drawRoundedRect(br, bh / 2.0, bh / 2.0);
                p.setPen(Qt::white);
                p.drawText(br, Qt::AlignCenter, badge);
            }
            p.setOpacity(1.0);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(Qt::NoPen);
    }

    // ── 用户标记：白描边彩色图钉 + 名称标签（与画布同款） ──
    if (in.userMarkers && !in.ann.userMarkers.isEmpty())
    {
        const double r = kUserPinSize / 2.0;
        QFont f = p.font();
        f.setPixelSize(11);
        f.setBold(true);
        const QFontMetrics fm(f);
        for (const auto &m : in.ann.userMarkers)
        {
            const double ax = pxX(m.x);
            const double ay = pxZ(m.z);
            if (ax < -60 || ay < -60 || ax > W + 60 || ay > H + 60)
                continue;
            const QColor col(m.icon >= 0 && m.icon < in.ann.userColors.size()
                                 ? QColor(in.ann.userColors.at(m.icon))
                                 : QColor(0x70, 0x70, 0x70));
            const QPointF cc(ax, ay - r - 4.0);
            p.setPen(QPen(QColor(0, 0, 0, 70), 1.0));
            p.setBrush(col);
            p.drawPolygon(QPolygonF() << cc + QPointF(-4.0, r - 5.0)
                                      << cc + QPointF(4.0, r - 5.0)
                                      << QPointF(ax, ay));
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(cc, r + 2.5, r + 2.5);
            p.setPen(QPen(QColor(0, 0, 0, 60), 1.0));
            p.setBrush(col);
            p.drawEllipse(cc, r + 1.5, r + 1.5);
            p.setPen(Qt::NoPen);
            if (m.icon >= 0 && m.icon < in.ann.userIcons.size()
                && !in.ann.userIcons.at(m.icon).isNull())
            {
                const QImage &glyph = in.ann.userIcons.at(m.icon);
                p.drawImage(QPointF(std::round(cc.x() - glyph.width() / 2.0),
                                    std::round(cc.y() - glyph.height() / 2.0)),
                            glyph);
            }
            if (!m.name.isEmpty())
            {
                p.setFont(f);
                const int tw = fm.horizontalAdvance(m.name);
                const QRectF lb(ax - tw / 2.0 - 5.0, ay + 3.0,
                                tw + 10.0, fm.height() + 4);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(20, 22, 26, 180));
                p.drawRoundedRect(lb, 4, 4);
                p.setPen(Qt::white);
                p.drawText(lb, Qt::AlignCenter, m.name);
            }
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(Qt::NoPen);
    }

    // ── 玩家位置：主题色箭头 + 圆点（与画布同款） ──
    if (in.player && in.ann.hasPlayer)
    {
        const double cx = pxX(qint64(std::llround(in.ann.playerX)));
        const double cy = pxZ(qint64(std::llround(in.ann.playerZ)));
        if (cx > -40 && cy > -40 && cx < W + 40 && cy < H + 40)
        {
            const QColor accent(in.ann.accent);
            const double yawRad = qDegreesToRadians(in.ann.playerYaw);
            const double thetaDeg = qRadiansToDegrees(
                std::atan2(std::cos(yawRad), -std::sin(yawRad)));
            p.save();
            p.translate(cx, cy);
            p.rotate(thetaDeg);
            QPolygonF arrow;
            arrow << QPointF(16.0, 0.0) << QPointF(3.0, 6.0)
                  << QPointF(3.0, -6.0);
            p.setPen(QPen(Qt::white, 2.0));
            p.setBrush(accent);
            p.drawPolygon(arrow);
            p.restore();
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(cx, cy), 7.5, 7.5);
            p.setPen(QPen(QColor(0, 0, 0, 60), 1.0));
            p.setBrush(accent);
            p.drawEllipse(QPointF(cx, cy), 6.0, 6.0);
            p.setBrush(Qt::NoBrush);
            p.setPen(Qt::NoPen);
        }
    }

    // ── 指北针 / 比例尺 / 信息栏（深色半透明小件，压在地图右下等角落） ──
    if (in.infoBar && W >= 220 && H >= 120)
    {
        const QColor accent(in.ann.accent);

        // 指北针：右上角圆形徽章，N + 向上箭头（地图上方 = 北 = -Z）
        {
            const QPointF cc(W - 44.0, 44.0);
            p.setPen(QPen(QColor(255, 255, 255, 70), 1.0));
            p.setBrush(kBadgeBg);
            p.drawEllipse(cc, 17.0, 17.0);
            QFont f = p.font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(QRectF(cc.x() - 17.0, cc.y() - 14.0, 34.0, 12.0),
                       Qt::AlignCenter, QStringLiteral("N"));
            p.setPen(QPen(QColor(255, 255, 255, 160), 1.0));
            p.setBrush(accent);
            p.drawPolygon(QPolygonF() << QPointF(cc.x(), cc.y() - 1.0)
                                      << QPointF(cc.x() - 4.5, cc.y() + 10.0)
                                      << QPointF(cc.x() + 4.5, cc.y() + 10.0));
        }

        // 信息栏：右下角深色圆角条 = 存档 · 维度 + 种子 · 日期
        QFont titleF = p.font();
        titleF.setPixelSize(13);
        titleF.setBold(true);
        QFont metaF = p.font();
        metaF.setPixelSize(12);
        const QFontMetrics tFm(titleF);
        const QFontMetrics mFm(metaF);
        const int titleW = tFm.horizontalAdvance(in.title);
        const int metaW = mFm.horizontalAdvance(in.meta);
        const int gap = 12;
        const int pillW = 14 + titleW + (metaW > 0 ? gap + metaW : 0) + 14;
        const int pillH = 30;
        QRectF info(W - 12.0 - pillW, H - 10.0 - pillH, pillW, pillH);

        // 比例尺：左下角，取最短能画到 ~56px 的整段方块数
        bool drawScale = false;
        qint64 scaleBlocks = 0;
        double scalePx = 0;
        QRectF scalePill;
        for (qint64 L : { qint64(64), qint64(128), qint64(256), qint64(512),
                          qint64(1024), qint64(2048), qint64(4096),
                          qint64(8192), qint64(16384) })
        {
            const double px = L / bpp;
            if (px >= 56.0)
            {
                scaleBlocks = L;
                scalePx = px;
                break;
            }
        }
        if (scaleBlocks > 0)
        {
            QFont f = p.font();
            f.setPixelSize(12);
            const QFontMetrics fm(f);
            const QString label = QStringLiteral("%1 方块").arg(scaleBlocks);
            const int w = int(scalePx) + 8 + fm.horizontalAdvance(label) + 22;
            scalePill = QRectF(12.0, H - 10.0 - pillH, w, pillH);
            // 与信息栏挤不下时放弃比例尺（信息栏优先）
            drawScale = scalePill.right() + 12.0 < info.left();
        }
        if (drawScale)
        {
            p.setPen(Qt::NoPen);
            p.setBrush(kBadgeBg);
            p.drawRoundedRect(scalePill, 8, 8);
            QFont f = p.font();
            f.setPixelSize(12);
            p.setFont(f);
            const QFontMetrics fm(f);
            const double lineY = scalePill.center().y();
            const double x0 = scalePill.left() + 12.0;
            const double x1 = x0 + scalePx;
            p.setPen(QPen(QColor(255, 255, 255, 220), 1.5));
            p.drawLine(QPointF(x0, lineY - 4.0), QPointF(x0, lineY + 4.0));
            p.drawLine(QPointF(x0, lineY), QPointF(x1, lineY));
            p.drawLine(QPointF(x1, lineY - 4.0), QPointF(x1, lineY + 4.0));
            p.setPen(Qt::white);
            p.drawText(QRectF(x1 + 8.0, scalePill.top(), scalePill.right() - x1 - 8.0,
                              pillH),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral("%1 方块").arg(scaleBlocks));
        }

        p.setPen(Qt::NoPen);
        p.setBrush(kBadgeBg);
        p.drawRoundedRect(info, 8, 8);
        double tx = info.left() + 14.0;
        if (titleW > 0)
        {
            p.setFont(titleF);
            p.setPen(Qt::white);
            p.drawText(QRectF(tx, info.top(), titleW + 4, pillH),
                       Qt::AlignVCenter | Qt::AlignLeft, in.title);
            tx += titleW + gap;
        }
        if (metaW > 0 && tx + metaW < info.right())
        {
            p.setFont(metaF);
            p.setPen(QColor(255, 255, 255, 200));
            p.drawText(QRectF(tx, info.top(), metaW + 4, pillH),
                       Qt::AlignVCenter | Qt::AlignLeft, in.meta);
        }
    }

    p.end();
    return out;
}

} // namespace MapExportComposer

// ═══════════════════════ 高级导出弹窗 ═══════════════════════

MapExportDialog::MapExportDialog(QWidget *parent, const SizeEstimates &estimates,
                                 const QRect &canvasBlocks, int canvasBpp,
                                 const Options &initial,
                                 const QRect &initialCustomRect)
    : AppDialogBase(parent)
    , m_est(estimates)
    , m_canvasBlocks(canvasBlocks)
    , m_canvasBpp(qMax(1, canvasBpp))
{
    setObjectName(QStringLiteral("mapExportDialog"));
    setWindowTitle(tr("高级导出地图"));
    m_opt = initial;
    initUI();

    // 上一次弹窗的选项原样带回（「重新框选」重开时选项不丢）
    if (initial.rangeMode == RangeView)
        m_viewBtn->setChecked(true);
    else if (initial.rangeMode == RangeCustom)
        m_customBtn->setChecked(true);
    m_hiBtn->setChecked(initial.hiRes);
    m_structChk->setChecked(initial.structures);
    m_userChk->setChecked(initial.userMarkers);
    m_oreChk->setChecked(initial.ores);
    m_playerChk->setChecked(initial.player);
    m_gridChk->setChecked(initial.grid);
    m_infoChk->setChecked(initial.infoBar);
    m_jpgBtn->setChecked(!initial.png);
    if (initialCustomRect.isValid())
    {
        m_cX0->setText(QString::number(initialCustomRect.left()));
        m_cX1->setText(QString::number(initialCustomRect.right()));
        m_cZ0->setText(QString::number(initialCustomRect.top()));
        m_cZ1->setText(QString::number(initialCustomRect.bottom()));
    }
    updateEstimate();
}

void MapExportDialog::initUI()
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("mapExportCard"));
    card->setMinimumWidth(420);

    QVBoxLayout *lay = new QVBoxLayout(card);
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(10);

    auto *title = new QLabel(tr("高级导出地图"), card);
    title->setStyleSheet(StyleKit::cardTitle());
    lay->addWidget(title);

    auto *hint = new QLabel(
        tr("把地图连同标注一起导出成一张图片；PNG 保留未探索区域的透明底，JPG 会铺上底色。"),
        card);
    hint->setStyleSheet(StyleKit::mutedLabel());
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto rowLabel = [&](const QString &text) {
        auto *l = new QLabel(text, card);
        l->setStyleSheet(StyleKit::secondaryLabel());
        return l;
    };

    // —— 范围 ——
    {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        row->addWidget(rowLabel(tr("范围")));
        m_wholeBtn = new QRadioButton(tr("整张地图"), card);
        m_wholeBtn->setChecked(true);
        m_viewBtn = new QRadioButton(tr("当前视野"), card);
        m_customBtn = new QRadioButton(tr("自定义区域"), card);
        row->addWidget(m_wholeBtn);
        row->addWidget(m_viewBtn);
        row->addWidget(m_customBtn);
        row->addStretch(1);
        lay->addLayout(row);
    }

    // —— 自定义区域：方块坐标输入 + 重新框选 ——
    {
        m_customRow = new QWidget(card);
        auto *row = new QHBoxLayout(m_customRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        auto mkCoord = [&](const QString &tip, int lo, int hi) {
            auto *e = new QLineEdit(m_customRow);
            e->setAlignment(Qt::AlignCenter);
            e->setFixedSize(64, 24);
            e->setToolTip(tip);
            auto *v = new QIntValidator(lo, hi, e);
            e->setValidator(v);
            return e;
        };
        row->addWidget(new QLabel(QStringLiteral("X"), m_customRow));
        row->addWidget(m_cX0 = mkCoord(
            tr("区域左边缘（X 最小值）"),
            m_canvasBlocks.left(), m_canvasBlocks.right()));
        row->addWidget(new QLabel(QStringLiteral("~"), m_customRow));
        row->addWidget(m_cX1 = mkCoord(
            tr("区域右边缘（X 最大值）"),
            m_canvasBlocks.left(), m_canvasBlocks.right()));
        row->addSpacing(8);
        row->addWidget(new QLabel(QStringLiteral("Z"), m_customRow));
        row->addWidget(m_cZ0 = mkCoord(
            tr("区域上边缘（Z 最小值）"),
            m_canvasBlocks.top(), m_canvasBlocks.bottom()));
        row->addWidget(new QLabel(QStringLiteral("~"), m_customRow));
        row->addWidget(m_cZ1 = mkCoord(
            tr("区域下边缘（Z 最大值）"),
            m_canvasBlocks.top(), m_canvasBlocks.bottom()));
        row->addSpacing(4);
        m_reselectBtn = new QPushButton(tr("重新框选…"), m_customRow);
        m_reselectBtn->setCursor(Qt::PointingHandCursor);
        m_reselectBtn->setToolTip(tr("收起弹窗，在地图上拖拽框选一个矩形范围"));
        connect(m_reselectBtn, &QPushButton::clicked, this, [this]() {
            collectOptions(); // 已改的勾选先收好，框选回来原样带回
            done(ReselectResult);
        });
        row->addWidget(m_reselectBtn);
        row->addStretch(1);
        m_customRow->setVisible(false);
        lay->addWidget(m_customRow);
        for (QLineEdit *e : { m_cX0, m_cX1, m_cZ0, m_cZ1 })
            connect(e, &QLineEdit::textEdited, this,
                    [this]() { updateEstimate(); });
    }

    // —— 清晰度 ——
    {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        row->addWidget(rowLabel(tr("清晰度")));
        m_loBtn = new QRadioButton(tr("当前画布（快）"), card);
        m_loBtn->setChecked(true);
        m_hiBtn = new QRadioButton(tr("高清重渲（重新读存档，更清晰）"), card);
        row->addWidget(m_loBtn);
        row->addWidget(m_hiBtn);
        row->addStretch(1);
        lay->addLayout(row);
    }

    // —— 标注 ——
    {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        row->addWidget(rowLabel(tr("标注")));
        m_structChk = new QCheckBox(tr("结构"), card);
        m_structChk->setChecked(true);
        m_userChk = new QCheckBox(tr("我的标记"), card);
        m_userChk->setChecked(true);
        m_oreChk = new QCheckBox(tr("矿物点"), card);
        m_playerChk = new QCheckBox(tr("玩家位置"), card);
        m_gridChk = new QCheckBox(tr("区块网格"), card);
        row->addWidget(m_structChk);
        row->addWidget(m_userChk);
        row->addWidget(m_oreChk);
        row->addWidget(m_playerChk);
        row->addWidget(m_gridChk);
        row->addStretch(1);
        lay->addLayout(row);
    }

    // —— 信息与格式 ——
    {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        row->addWidget(rowLabel(tr("信息")));
        m_infoChk = new QCheckBox(tr("指北针 · 比例尺 · 信息栏"), card);
        m_infoChk->setChecked(true);
        row->addWidget(m_infoChk);
        row->addStretch(1);
        row->addWidget(rowLabel(tr("格式")));
        m_pngBtn = new QRadioButton(QStringLiteral("PNG"), card);
        m_pngBtn->setChecked(true);
        m_jpgBtn = new QRadioButton(QStringLiteral("JPG"), card);
        row->addWidget(m_pngBtn);
        row->addWidget(m_jpgBtn);
        lay->addLayout(row);
    }

    // —— 尺寸预估 + 动作 ——
    m_estimateLabel = new QLabel(card);
    m_estimateLabel->setStyleSheet(StyleKit::captionLabel());
    lay->addWidget(m_estimateLabel);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(8);
    actions->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), card);
    cancel->setObjectName(QStringLiteral("mapExportCancel"));
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, [this]() { reject(); });
    actions->addWidget(cancel);
    m_okBtn = new QPushButton(tr("导出"), card);
    m_okBtn->setObjectName(QStringLiteral("mapExportOk"));
    m_okBtn->setCursor(Qt::PointingHandCursor);
    m_okBtn->setStyleSheet(StyleKit::primaryButton());
    connect(m_okBtn, &QPushButton::clicked, this, [this]() {
        collectOptions();
        accept();
    });
    actions->addWidget(m_okBtn);
    lay->addLayout(actions);

    connect(m_wholeBtn, &QRadioButton::toggled, this,
            [this]() { updateEstimate(); });
    connect(m_viewBtn, &QRadioButton::toggled, this,
            [this]() { updateEstimate(); });
    connect(m_customBtn, &QRadioButton::toggled, this, [this]() {
        m_customRow->setVisible(m_customBtn->isChecked());
        updateEstimate();
    });
    connect(m_hiBtn, &QRadioButton::toggled, this,
            [this]() { updateEstimate(); });
    connect(m_loBtn, &QRadioButton::toggled, this,
            [this]() { updateEstimate(); });

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(card, 0, 0);

    setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#mapExportCard {"
        "    background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 16px;"
        "}")));
}

/** 把当前界面勾选读进 m_opt（导出与「重新框选」两个出口共用） */
void MapExportDialog::collectOptions()
{
    m_opt.rangeMode = m_customBtn->isChecked() ? RangeCustom
        : m_viewBtn->isChecked() ? RangeView : RangeWhole;
    m_opt.customRect = m_opt.rangeMode == RangeCustom
        ? customRectFromInputs() : QRect();
    m_opt.hiRes = m_hiBtn->isChecked();
    m_opt.structures = m_structChk->isChecked();
    m_opt.userMarkers = m_userChk->isChecked();
    m_opt.ores = m_oreChk->isChecked();
    m_opt.player = m_playerChk->isChecked();
    m_opt.grid = m_gridChk->isChecked();
    m_opt.infoBar = m_infoChk->isChecked();
    m_opt.png = m_pngBtn->isChecked();
}

QRect MapExportDialog::customRectFromInputs() const
{
    bool okX0 = false, okX1 = false, okZ0 = false, okZ1 = false;
    int x0 = m_cX0->text().toInt(&okX0);
    int x1 = m_cX1->text().toInt(&okX1);
    int z0 = m_cZ0->text().toInt(&okZ0);
    int z1 = m_cZ1->text().toInt(&okZ1);
    if (!okX0 || !okX1 || !okZ0 || !okZ1)
        return QRect();
    if (x0 > x1)
        std::swap(x0, x1);
    if (z0 > z1)
        std::swap(z0, z1);
    return QRect(QPoint(x0, z0), QPoint(x1, z1)).intersected(m_canvasBlocks);
}

void MapExportDialog::updateEstimate()
{
    const bool hi = m_hiBtn->isChecked();
    QSize size;
    QString problem;
    if (m_customBtn->isChecked())
    {
        const QRect rect = customRectFromInputs();
        if (rect.isNull() || rect.isEmpty())
            problem = tr("自定义区域无效：填画布范围内的 X / Z 坐标，或点「重新框选」");
        else if (hi && (rect.width() > 8192 || rect.height() > 8192))
            problem = tr("自定义区域过大：高清导出每边最大 8192 方块");
        else if (hi)
            size = rect.size(); // 高清 = 1 像素 1 方块
        else
            size = QSize(int(std::ceil(rect.width() / double(m_canvasBpp))),
                         int(std::ceil(rect.height() / double(m_canvasBpp))));
    }
    else
    {
        const bool whole = m_wholeBtn->isChecked();
        size = hi ? (whole ? m_est.wholeHi : m_est.viewHi)
                  : (whole ? m_est.wholeLo : m_est.viewLo);
    }
    m_hiBtn->setEnabled(m_customBtn->isChecked()
        || !m_est.wholeHi.isNull() || !m_est.viewHi.isNull());
    if (size.isEmpty() || !problem.isEmpty())
    {
        m_okBtn->setEnabled(false);
        m_estimateLabel->setText(problem.isEmpty()
            ? tr("预估导出尺寸：暂不可用（先渲染出地图再导出）")
            : problem);
        return;
    }
    m_okBtn->setEnabled(true);
    m_estimateLabel->setText(tr("预估导出尺寸：约 %1 × %2 像素")
                                 .arg(size.width())
                                 .arg(size.height()));
}
