/**
 * @file   SeedMapPage.cpp
 * @brief  种子地图页实现：按种子预测群系地形 + 结构标记 + 拖动缩放 + 导出图片
 * @author BlockBox Team
 * @date   2026-11-26
 */

#include "SeedMapPage.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QHash>
#include <cmath>
#include <QtConcurrent/QtConcurrent>
#include <QtMath>

#include <functional>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/WorldMap/BiomeCatalog.h"
#include "utils/WorldMap/OreFinder.h"
#include "utils/WorldMap/SeedTerrainRenderer.h"
#include "utils/WorldMap/StructureFinder.h"
#include "utils/WorldMap/StructureIcons.h"
#include "utils/WorldMap/WorldMapRenderer.h"

namespace {

/** 每块 region 渲染成多少像素：512 方块 / 128 像素 = 1 像素 4 方块，正好是群系 1:4 粒度 */
constexpr int kTilePx = 128;
/** 画布覆盖多少个 region（取奇数，保证有中心块）：7 x 7 = 3584 x 3584 方块 */
constexpr int kRegions = 7;
/** 一个 region 的边长（方块） */
constexpr int kRegionBlocks = 512;
/** 画布像素 → 方块的比例 */
constexpr double kBlocksPerPx = double(kRegionBlocks) / double(kTilePx);
/** 群系叠加层采样步长（方块）：与存档地图一致 */
constexpr int kBiomeStep = 64;

/** 地图卡片圆角（与存档地图一致） */
constexpr qreal kMapCardRadius = 14.0;
/** 结构标记图标边长（与存档地图一致） */
constexpr int kMarkerSize = 18;
/** 网格颜色（与存档地图同款） */
const QColor kChunkGrid(255, 255, 255, 28);
const QColor kRegionGrid(255, 255, 255, 78);

/** 地图卡片底色（@BG_BASE@；不透，主题背景不会从地图区透出） */
QColor mapCardBackgroundColor()
{
    ThemeManager *tm = ThemeManager::instance();
    if (!tm)
        return QColor(0xf5, 0xf5, 0xf5);
    switch (tm->currentTheme())
    {
    case ThemeManager::LightTheme: return QColor(0xf5, 0xf5, 0xf5);
    case ThemeManager::DarkTheme:  return QColor(0x1a, 0x1a, 0x1a);
    default:                       return QColor(0x2a, 0x2a, 0x2a);
    }
}

/** 地图卡片描边（@BORDER@） */
QColor mapCardBorderColor()
{
    ThemeManager *tm = ThemeManager::instance();
    if (!tm)
        return QColor(0xe8, 0xea, 0xed);
    switch (tm->currentTheme())
    {
    case ThemeManager::LightTheme: return QColor(0xe8, 0xea, 0xed);
    case ThemeManager::DarkTheme:  return QColor(0x3a, 0x3a, 0x3a);
    default:                       return QColor(0x40, 0x40, 0x40);
    }
}

/** 离中心由近到远排好的 region 列表（按切比雪夫环一圈圈铺） */
QVector<QPoint> ringOrderedRegions(int centerX, int centerZ)
{
    QVector<QPoint> out;
    out.reserve(kRegions * kRegions);
    const int radius = kRegions / 2;
    for (int r = 0; r <= radius; ++r)
    {
        for (int dx = -r; dx <= r; ++dx)
        {
            for (int dz = -r; dz <= r; ++dz)
            {
                if (qMax(qAbs(dx), qAbs(dz)) != r)
                    continue;
                out.append(QPoint(centerX + dx, centerZ + dz));
            }
        }
    }
    return out;
}

} // namespace

/**
 * @brief 画布：地图 + 群系叠加层 + 结构标记，支持拖动平移、滚轮缩放、网格
 *
 * 外观与存档地图的画布一致：圆角不透卡片、同款网格配色、同款白色描边图标。
 * 不单独声明成带 Q_OBJECT 的类（避免额外 moc 文件），视角变化通过
 * std::function 回调通知页面刷新工具栏与状态文字。
 */
class SeedMapCanvas : public QWidget
{
public:
    explicit SeedMapCanvas(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setMinimumSize(240, 180);
        setCursor(Qt::OpenHandCursor);
    }

    /** 视角变化（拖动 / 缩放 / 适应 / 居中）后回调 */
    std::function<void()> onViewChanged;

    /** 重置画布：覆盖的方块范围 */
    void resetCanvas(const QRect &blockRect)
    {
        m_blockOrigin = blockRect.topLeft();
        m_img = QImage(kRegions * kTilePx, kRegions * kTilePx,
                       QImage::Format_ARGB32_Premultiplied);
        m_img.fill(Qt::transparent);
        m_zoom = 1.0;
        m_offset = QPointF(0, 0);
        m_hasTile = false;
        m_markers.clear();
        update();
    }

    /** 贴一块 region 预测图 */
    void putTile(int regionX, int regionZ, const QImage &tile)
    {
        if (m_img.isNull() || tile.isNull())
            return;
        const int col = regionX - m_blockOrigin.x() / kRegionBlocks;
        const int row = regionZ - m_blockOrigin.y() / kRegionBlocks;
        if (col < 0 || row < 0 || col >= kRegions || row >= kRegions)
            return;
        QPainter p(&m_img);
        p.drawImage(QPoint(col * kTilePx, row * kTilePx), tile);
        p.end();
        m_hasTile = true;
        update();
    }

    bool isEmpty() const { return !m_hasTile; }

    void setGridVisible(bool on)
    {
        m_gridOn = on;
        update();
    }

    /** 结构标记（方块坐标 + allStructures() 下标） */
    void setMarkers(const QVector<SeedStructureMarker> &markers)
    {
        m_markers = markers;
        update();
    }

    /** 群系叠加层：一格 = cellSize 方块；传空图关闭叠加 */
    void setBiomeOverlay(const QImage *img, qint64 minX, qint64 minZ, int cellSize)
    {
        m_bioImg = img ? *img : QImage();
        m_bioMinX = minX;
        m_bioMinZ = minZ;
        m_bioCell = qMax(1, cellSize);
        update();
    }

    /** 缩放 / 平移到底：整张地图刚好放进画布 */
    void fitToView()
    {
        if (m_img.isNull() || width() <= 0 || height() <= 0)
            return;
        const double s = qMin(double(width()) / m_img.width(),
                              double(height()) / m_img.height());
        m_zoom = qBound(0.1, s, 8.0);
        m_offset = QPointF((width() - m_img.width() * m_zoom) / 2.0,
                           (height() - m_img.height() * m_zoom) / 2.0);
        update();
        notifyViewChanged();
    }

    /** 按比例缩放（保持窗口中心不动） */
    void zoomBy(double factor)
    {
        if (m_img.isNull())
            return;
        const QPointF center(width() / 2.0, height() / 2.0);
        const QPointF before = (center - m_offset) / m_zoom;
        m_zoom = qBound(0.1, m_zoom * factor, 12.0);
        m_offset = center - before * m_zoom;
        update();
        notifyViewChanged();
    }

    /** 把某方块坐标移到画布正中（保持当前缩放） */
    void centerOnBlock(double blockX, double blockZ)
    {
        const double px = (blockX - m_blockOrigin.x()) / kBlocksPerPx;
        const double pz = (blockZ - m_blockOrigin.y()) / kBlocksPerPx;
        m_offset = QPointF(width() / 2.0 - px * m_zoom, height() / 2.0 - pz * m_zoom);
        update();
        notifyViewChanged();
    }

    /** 画布中心对应的方块坐标 */
    QPointF centerBlock() const
    {
        if (m_img.isNull())
            return QPointF(0, 0);
        const QPointF px = (QPointF(width() / 2.0, height() / 2.0) - m_offset) / m_zoom;
        return QPointF(m_blockOrigin.x() + px.x() * kBlocksPerPx,
                       m_blockOrigin.y() + px.y() * kBlocksPerPx);
    }

    /** 画布覆盖的方块范围（结构查询 / 群系采样用） */
    QRect blockRect() const
    {
        return QRect(m_blockOrigin.x(), m_blockOrigin.y(),
                     kRegions * kRegionBlocks, kRegions * kRegionBlocks);
    }

    double zoom() const { return m_zoom; }
    QImage mapImage() const { return m_img; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 中间地图区：圆角不透卡片（与存档地图同款观感）
        const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(outer, kMapCardRadius, kMapCardRadius);
        p.fillPath(clip, mapCardBackgroundColor());
        p.setClipPath(clip);

        if (m_img.isNull() || !m_hasTile)
        {
            p.setPen(palette().color(QPalette::Mid));
            p.drawText(rect(), Qt::AlignCenter,
                       tr("填好种子点「生成」，或点「随机」看一个种子"));
            p.setClipping(false);
            p.setPen(QPen(mapCardBorderColor(), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(outer, kMapCardRadius, kMapCardRadius);
            return;
        }

        const double pxPerBlock = m_zoom / kBlocksPerPx;
        const QRectF target(m_offset.x(), m_offset.y(),
                            m_img.width() * m_zoom, m_img.height() * m_zoom);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(target, m_img, QRectF(m_img.rect()));

        // 群系高亮叠加层（选中群系上色，其余透明）
        if (!m_bioImg.isNull())
        {
            const QRectF bioRect(
                m_offset.x() + (m_bioMinX - m_blockOrigin.x()) * pxPerBlock,
                m_offset.y() + (m_bioMinZ - m_blockOrigin.y()) * pxPerBlock,
                m_bioImg.width() * m_bioCell * pxPerBlock,
                m_bioImg.height() * m_bioCell * pxPerBlock);
            p.drawImage(bioRect, m_bioImg);
        }

        if (m_gridOn)
            drawGrid(p, target);

        drawMarkers(p, pxPerBlock);

        p.setClipping(false);
        p.setPen(QPen(mapCardBorderColor(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(outer, kMapCardRadius, kMapCardRadius);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        // 首次布局完成后自动适应窗口
        if (!m_fittedOnce && m_img.width() > 0)
        {
            m_fittedOnce = true;
            fitToView();
        }
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_dragging = true;
            m_lastPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging)
        {
            const QPoint delta = event->pos() - m_lastPos;
            m_lastPos = event->pos();
            m_offset += QPointF(delta);
            update();
            notifyViewChanged();
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_dragging = false;
            setCursor(Qt::OpenHandCursor);
        }
        QWidget::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (m_img.isNull())
            return;
        const double factor = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
        const QPointF pos = event->position();
        const QPointF before = (pos - m_offset) / m_zoom;
        m_zoom = qBound(0.1, m_zoom * factor, 12.0);
        m_offset = pos - before * m_zoom;
        update();
        notifyViewChanged();
        event->accept();
    }

private:
    void notifyViewChanged()
    {
        if (onViewChanged)
            onViewChanged();
    }

    /** 区块 / 区域网格：与存档地图同一套配色与显示门槛 */
    void drawGrid(QPainter &p, const QRectF &target)
    {
        const double chunkWidgetPx = m_zoom * 16.0 / kBlocksPerPx;
        const double regionWidgetPx = m_zoom * 512.0 / kBlocksPerPx;
        const double pxPerBlock = m_zoom / kBlocksPerPx;

        const double leftBlock = m_blockOrigin.x() + (rect().left() - m_offset.x()) / pxPerBlock;
        const double topBlock = m_blockOrigin.y() + (rect().top() - m_offset.y()) / pxPerBlock;
        const double rightBlock = m_blockOrigin.x() + (rect().right() - m_offset.x()) / pxPerBlock;
        const double bottomBlock = m_blockOrigin.y() + (rect().bottom() - m_offset.y()) / pxPerBlock;

        if (chunkWidgetPx >= 7.0)
        {
            QPen chunkPen(kChunkGrid);
            chunkPen.setWidthF(1.0);
            p.setPen(chunkPen);
            for (qint64 bx = qint64(std::floor(leftBlock / 16.0)) * 16;
                 bx <= rightBlock; bx += 16)
            {
                const double x = m_offset.x() + (bx - m_blockOrigin.x()) * pxPerBlock;
                p.drawLine(QPointF(x, target.top()), QPointF(x, target.bottom()));
            }
            for (qint64 bz = qint64(std::floor(topBlock / 16.0)) * 16;
                 bz <= bottomBlock; bz += 16)
            {
                const double y = m_offset.y() + (bz - m_blockOrigin.y()) * pxPerBlock;
                p.drawLine(QPointF(target.left(), y), QPointF(target.right(), y));
            }
        }

        if (regionWidgetPx >= 18.0)
        {
            QPen regionPen(kRegionGrid);
            regionPen.setWidthF(1.0);
            p.setPen(regionPen);
            for (qint64 bx = qint64(std::floor(leftBlock / kRegionBlocks)) * kRegionBlocks;
                 bx <= rightBlock; bx += kRegionBlocks)
            {
                const double x = m_offset.x() + (bx - m_blockOrigin.x()) * pxPerBlock;
                p.drawLine(QPointF(x, target.top()), QPointF(x, target.bottom()));
            }
            for (qint64 bz = qint64(std::floor(topBlock / kRegionBlocks)) * kRegionBlocks;
                 bz <= bottomBlock; bz += kRegionBlocks)
            {
                const double y = m_offset.y() + (bz - m_blockOrigin.y()) * pxPerBlock;
                p.drawLine(QPointF(target.left(), y), QPointF(target.right(), y));
            }
        }
    }

    /** 结构标记：白色圆角底 + 类型图标；密集处合并成一个图标并标 ×N（与存档地图同款） */
    void drawMarkers(QPainter &p, double pxPerBlock)
    {
        if (m_markers.isEmpty())
            return;
        const auto &defs = StructureFinder::allStructures();
        const int half = kMarkerSize / 2 + 2;
        // 方块在屏幕上的大小：太小的时候只画稀有结构，否则一堆村庄糊满屏
        const bool tiny = (m_zoom / kBlocksPerPx) < 0.5;

        struct Cluster
        {
            QPointF center;
            int type = -1;
            int count = 0;
            bool rare = false;
        };
        QVector<Cluster> clusters;
        clusters.reserve(m_markers.size());

        const double mergePx = 20.0; // 屏幕距离小于它就算同一处
        for (const SeedStructureMarker &m : m_markers)
        {
            if (m.type < 0 || m.type >= defs.size())
                continue;
            const auto &d = defs[m.type];
            if (tiny && !d.rare)
                continue;
            const QPointF pos(m_offset.x() + (m.x - m_blockOrigin.x()) * pxPerBlock,
                              m_offset.y() + (m.z - m_blockOrigin.y()) * pxPerBlock);
            if (!QRectF(-40, -40, width() + 80, height() + 80).contains(pos))
                continue;

            bool merged = false;
            for (Cluster &c : clusters)
            {
                if (QLineF(c.center, pos).length() >= mergePx)
                    continue;
                c.center = (c.center + pos) / 2.0;
                ++c.count;
                // 合并时优先显示稀有结构
                if (d.rare && !c.rare)
                {
                    c.type = m.type;
                    c.rare = true;
                }
                merged = true;
                break;
            }
            if (!merged)
                clusters.append({pos, m.type, 1, d.rare});
        }

        for (const Cluster &c : clusters)
        {
            const auto &d = defs[c.type];
            const QPixmap pm = StructureIcons::pixmap(
                c.type, QLatin1String(d.key),
                QString(QChar::fromLatin1(d.displayName[0])),
                QLatin1String(d.wikiTitle), QLatin1String(d.itemTitle),
                QLatin1String(d.envSprite), kMarkerSize);
            const QRect bg(int(c.center.x()) - half, int(c.center.y()) - half,
                           half * 2, half * 2);
            p.setOpacity(c.count >= 3 ? 0.75 : 1.0);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawRoundedRect(bg, 6, 6);
            p.setBrush(Qt::NoBrush);
            p.drawPixmap(QPoint(int(c.center.x()) - pm.width() / 2,
                                int(c.center.y()) - pm.height() / 2),
                         pm);

            // 密集合并角标：×2 / ×3 …
            if (c.count >= 2)
            {
                QFont f = p.font();
                f.setPixelSize(10);
                f.setBold(true);
                p.setFont(f);
                p.setPen(QColor(0, 0, 0, 190));
                const QString badge = QStringLiteral("×%1").arg(c.count);
                const QFontMetrics fm(f);
                const int bw = fm.horizontalAdvance(badge) + 4;
                const QRect br(int(c.center.x()) + half - 2 - bw,
                               int(c.center.y()) + half - 14, bw, 13);
                p.setBrush(Qt::white);
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(br, 4, 4);
                p.setPen(QColor(0, 0, 0, 200));
                p.drawText(br, Qt::AlignCenter, badge);
                p.setBrush(Qt::NoBrush);
            }
            p.setOpacity(1.0);
        }
    }
    QImage m_img;
    QImage m_bioImg;
    QVector<SeedStructureMarker> m_markers;
    QPointF m_offset;
    QPoint m_blockOrigin;
    qint64 m_bioMinX = 0;
    qint64 m_bioMinZ = 0;
    int m_bioCell = kBiomeStep;
    double m_zoom = 1.0;
    bool m_dragging = false;
    bool m_fittedOnce = false;
    bool m_hasTile = false;
    bool m_gridOn = true;
    QPoint m_lastPos;
};

// ────────────────────────────────────────────────────────────── SeedMapPage

SeedMapPage::SeedMapPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

SeedMapPage::~SeedMapPage() = default;

void SeedMapPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 10, 16, 14);
    root->setSpacing(10);

    // 快捷键需要页面树内有焦点才生效（keyPressEvent 为统一入口）
    setFocusPolicy(Qt::StrongFocus);

    const QColor themeColor = ThemeManager::instance()->currentThemeColor();

    // ── 顶部操作区：标题块 │ 渲染组 │ 视图组 …… 输出动作 ──
    auto *bar = new QWidget(this);
    bar->setObjectName("worldMapToolbar");
    bar->setToolTip(
        tr("鼠标：左键拖拽平移 · 滚轮缩放\n"
           "快捷键：+ / - 缩放 · 0 适应窗口 · G 网格 · R 重新生成"));
    auto *barLay = new QHBoxLayout(bar);
    barLay->setContentsMargins(14, 9, 14, 9);
    barLay->setSpacing(10);
    barLay->setAlignment(Qt::AlignVCenter);

    // 组内扁平按钮；右侧输出动作用带框样式以示区别
    auto makeBtn = [&](const QString &text, const QString &tip,
                       const QString &icon = QString(),
                       const char *objName = "worldMapGroupBtn",
                       QWidget *parentWidget = nullptr) -> QPushButton * {
        auto *b = new QPushButton(text, parentWidget ? parentWidget : bar);
        b->setObjectName(QString::fromLatin1(objName));
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(26);
        if (!tip.isEmpty())
            b->setToolTip(tip);
        if (!icon.isEmpty())
        {
            b->setIcon(IconHelper::loadColoredIcon(icon, themeColor, 15));
            b->setIconSize(QSize(15, 15));
        }
        return b;
    };

    auto makeSep = [&]() -> QFrame * {
        auto *f = new QFrame(bar);
        f->setObjectName("worldMapToolSep");
        f->setFrameShape(QFrame::NoFrame);
        f->setAttribute(Qt::WA_StyledBackground, true);
        f->setFixedWidth(1);
        f->setFixedHeight(28);
        return f;
    };

    auto makeGroup = [&]() -> QWidget * {
        auto *g = new QWidget(bar);
        g->setObjectName("worldMapToolGroup");
        auto *lay = new QHBoxLayout(g);
        lay->setContentsMargins(5, 4, 5, 4);
        lay->setSpacing(4);
        lay->setAlignment(Qt::AlignVCenter);
        return g;
    };

    // —— 标题块：图标 + 名称 / 副标题 ——
    auto *titleBlock = new QWidget(bar);
    titleBlock->setObjectName("worldMapTitleBlock");
    auto *titleLay = new QHBoxLayout(titleBlock);
    titleLay->setContentsMargins(0, 0, 0, 0);
    titleLay->setSpacing(10);

    auto *titleIcon = new QLabel(titleBlock);
    titleIcon->setObjectName("worldMapTitleIcon");
    titleIcon->setAttribute(Qt::WA_StyledBackground, true);
    titleIcon->setFixedSize(34, 34);
    titleIcon->setAlignment(Qt::AlignCenter);
    titleIcon->setPixmap(IconHelper::loadColoredIcon(
                             QStringLiteral(":/Images/Icons/map.svg"), themeColor, 18)
                             .pixmap(QSize(18, 18)));

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(0);
    titleCol->setContentsMargins(0, 0, 0, 0);

    auto *titleLabel = new QLabel(tr("种子地图"), titleBlock);
    titleLabel->setObjectName("worldMapTitle");
    titleLabel->setMinimumWidth(40);

    m_subtitleLabel = new QLabel(tr("1.21.4 · 主世界 · 地表方块"), titleBlock);
    m_subtitleLabel->setObjectName("worldMapSubtitle");

    titleCol->addWidget(titleLabel);
    titleCol->addWidget(m_subtitleLabel);
    titleLay->addWidget(titleIcon);
    titleLay->addLayout(titleCol);
    barLay->addWidget(titleBlock);

    barLay->addWidget(makeSep());

    // —— 渲染组：种子 / 版本 / 维度 / 着色 / 高度 / 生成 ——
    auto *renderGroup = makeGroup();
    auto *rgLay = qobject_cast<QHBoxLayout *>(renderGroup->layout());

    m_seedEdit = new QLineEdit(renderGroup);
    m_seedEdit->setObjectName(QStringLiteral("worldMapSeedEdit"));
    m_seedEdit->setPlaceholderText(tr("数字或文字种子"));
    m_seedEdit->setText(QStringLiteral("0"));
    m_seedEdit->setClearButtonEnabled(true);
    m_seedEdit->setFixedSize(140, 26);
    m_seedEdit->setToolTip(tr("世界种子：填数字，或直接填一段文字（按游戏同款规则换算）"));
    rgLay->addWidget(m_seedEdit);

    auto *randomBtn = makeBtn(tr("随机"), tr("随机换一个种子看看地形"));
    connect(randomBtn, &QPushButton::clicked, this, [this]() {
        const quint32 v = QRandomGenerator::global()->generate();
        m_seedEdit->setText(QString::number(static_cast<qint32>(v)));
        startRender(true);
    });
    rgLay->addWidget(randomBtn);

    m_versionCombo = new QComboBox(renderGroup);
    m_versionCombo->setObjectName("worldMapToolCombo");
    m_versionCombo->setFixedHeight(26);
    // 顺序即下拉顺序；值交给 StructureFinder::versionToInt 转成 cubiomes 版本号
    const QStringList versions = {
        QStringLiteral("1.21.4"), QStringLiteral("1.21"), QStringLiteral("1.20.6"),
        QStringLiteral("1.20.4"), QStringLiteral("1.20.1"), QStringLiteral("1.19.4"),
        QStringLiteral("1.19"), QStringLiteral("1.18.2"), QStringLiteral("1.17.1"),
        QStringLiteral("1.16.5"), QStringLiteral("1.15.2"), QStringLiteral("1.14.4"),
        QStringLiteral("1.13.2"), QStringLiteral("1.12.2"), QStringLiteral("1.11.2"),
        QStringLiteral("1.10.2"), QStringLiteral("1.9.4"), QStringLiteral("1.8.9"),
        QStringLiteral("1.7.10")
    };
    for (const QString &v : versions)
        m_versionCombo->addItem(v, v);
    m_versionCombo->setToolTip(tr("版本：群系、结构与地形都按这个版本计算，换版本会自动重画"));
    rgLay->addWidget(m_versionCombo);

    m_dimCombo = new QComboBox(renderGroup);
    m_dimCombo->setObjectName("worldMapToolCombo");
    m_dimCombo->setFixedHeight(26);
    m_dimCombo->addItem(tr("主世界"), static_cast<int>(WorldMapRenderer::Dimension::Overworld));
    m_dimCombo->addItem(tr("下界"), static_cast<int>(WorldMapRenderer::Dimension::Nether));
    m_dimCombo->addItem(tr("末地"), static_cast<int>(WorldMapRenderer::Dimension::End));
    m_dimCombo->setToolTip(tr("维度：切换要查看的世界，切换后自动重新渲染（1 / 2 / 3）"));
    rgLay->addWidget(m_dimCombo);

    m_shadeCombo = new QComboBox(renderGroup);
    m_shadeCombo->setObjectName("worldMapToolCombo");
    m_shadeCombo->setFixedHeight(26);
    m_shadeCombo->addItem(tr("地表方块"), static_cast<int>(WorldMapRenderer::ShadeMode::BlockColor));
    m_shadeCombo->addItem(tr("高度"), static_cast<int>(WorldMapRenderer::ShadeMode::Height));
    m_shadeCombo->addItem(tr("地形"), static_cast<int>(WorldMapRenderer::ShadeMode::Terrain));
    m_shadeCombo->setToolTip(tr("着色：地表方块 = 按方块颜色；高度 = 按地表高低显示灰度；"
                                "地形 = 按海拔分层设色并加山体阴影"));
    rgLay->addWidget(m_shadeCombo);

    // 高度：可编辑下拉 —— 框内既是输入框，也能下拉选预设 Y
    m_heightCombo = new QComboBox(renderGroup);
    m_heightCombo->setObjectName("worldMapToolCombo");
    m_heightCombo->setFixedHeight(26);
    m_heightCombo->setFixedWidth(88);
    m_heightCombo->setEditable(true);
    m_heightCombo->setInsertPolicy(QComboBox::NoInsert);
    m_heightCombo->setPlaceholderText(tr("高度"));
    m_heightCombo->addItem(tr("全部"), -1);
    m_heightCombo->addItem(QStringLiteral("Y=64"), 64);
    m_heightCombo->addItem(QStringLiteral("Y=100"), 100);
    m_heightCombo->addItem(QStringLiteral("Y=128"), 128);
    m_heightCombo->setCurrentIndex(0);
    m_heightCombo->setToolTip(
        tr("高度：留空或选「全部」= 完整地表；填一个 Y 只显示该高度及以下的地表"
           "（例如填 64 可削掉树冠，看清地面）"));
    rgLay->addWidget(m_heightCombo);

    auto *generateBtn = makeBtn(tr("生成"), tr("按当前种子与参数重新生成地图"),
                                QStringLiteral(":/Images/Icons/refresh.svg"));
    connect(generateBtn, &QPushButton::clicked, this, [this]() { startRender(true); });
    rgLay->addWidget(generateBtn);

    barLay->addWidget(renderGroup);

    barLay->addWidget(makeSep());

    // —— 视图组：网格 / 缩放 / 适应窗口 ——
    auto *viewGroup = makeGroup();
    auto *vgLay = qobject_cast<QHBoxLayout *>(viewGroup->layout());

    m_gridBtn = makeBtn(tr("网格"), tr("显示 / 隐藏区块与区域网格（G）"),
                        QStringLiteral(":/Images/Icons/grid.svg"));
    m_gridBtn->setCheckable(true);
    m_gridBtn->setChecked(true);
    connect(m_gridBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_view)
            m_view->setGridVisible(on);
    });
    vgLay->addWidget(m_gridBtn);

    auto *zoomOutBtn = makeBtn(QStringLiteral("−"), tr("缩小（-）"));
    zoomOutBtn->setFixedWidth(28);
    connect(zoomOutBtn, &QPushButton::clicked, this, [this]() {
        if (m_view)
            m_view->zoomBy(1.0 / 1.25);
    });
    vgLay->addWidget(zoomOutBtn);

    m_zoomLabel = new QLabel(QStringLiteral("100%"), viewGroup);
    m_zoomLabel->setObjectName("worldMapToolLabel");
    m_zoomLabel->setFixedSize(42, 26);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    vgLay->addWidget(m_zoomLabel);

    auto *zoomInBtn = makeBtn(QStringLiteral("+"), tr("放大（+）"));
    zoomInBtn->setFixedWidth(28);
    connect(zoomInBtn, &QPushButton::clicked, this, [this]() {
        if (m_view)
            m_view->zoomBy(1.25);
    });
    vgLay->addWidget(zoomInBtn);

    auto *fitBtn = makeBtn(tr("适应"), tr("缩放到刚好容纳整张地图（0）"));
    connect(fitBtn, &QPushButton::clicked, this, [this]() {
        if (m_view)
            m_view->fitToView();
    });
    vgLay->addWidget(fitBtn);

    barLay->addWidget(viewGroup);

    barLay->addStretch(1);

    // —— 右侧输出动作：带框按钮，与组内扁平控件区分 ——
    auto *exportBtn = makeBtn(tr("导出"), tr("把当前这张种子地图导出成 PNG 图片（E）"),
                              QStringLiteral(":/Images/Icons/export.svg"),
                              "worldMapActionBtn");
    connect(exportBtn, &QPushButton::clicked, this, &SeedMapPage::exportImage);
    barLay->addWidget(exportBtn);

    root->addWidget(bar);

    // ── 第二行：结构开关 + 全选 / 清空 + 展开（与存档地图一致） ──
    auto *bar2 = new QWidget(this);
    bar2->setObjectName("worldMapToolbar");
    auto *bar2Lay = new QHBoxLayout(bar2);
    bar2Lay->setContentsMargins(14, 5, 14, 5);
    bar2Lay->setSpacing(6);
    buildOverlayRow(bar2, bar2Lay);
    root->addWidget(bar2);

    // ── 画布 ──
    m_view = new SeedMapCanvas(this);
    m_view->setObjectName(QStringLiteral("worldMapView"));
    m_view->onViewChanged = [this]() {
        syncZoomLabel();
        updateViewInfo();
    };
    root->addWidget(m_view, 1);

    // ── 底部状态区 ──
    auto *statusBar = new QWidget(this);
    statusBar->setObjectName("worldMapStatusBar");
    auto *statusLay = new QHBoxLayout(statusBar);
    statusLay->setContentsMargins(14, 6, 14, 6);
    statusLay->setSpacing(12);

    m_statusLabel = new QLabel(statusBar);
    m_statusLabel->setObjectName("worldMapStatus");
    m_statusLabel->setText(tr("填好种子点「生成」，或直接点「随机」看一个种子"));
    statusLay->addWidget(m_statusLabel, 1);

    m_progress = new QProgressBar(statusBar);
    m_progress->setObjectName("worldMapProgress");
    m_progress->setFixedWidth(220);
    m_progress->setFixedHeight(8);
    m_progress->setTextVisible(false);
    m_progress->setRange(0, kRegions * kRegions);
    m_progress->hide();
    statusLay->addWidget(m_progress);

    m_infoLabel = new QLabel(statusBar);
    m_infoLabel->setObjectName("worldMapBiome");
    m_infoLabel->setText(tr("中心：—"));
    statusLay->addWidget(m_infoLabel);

    root->addWidget(statusBar);

    // 渲染节拍：每触发一次画一块 region，界面不会被一次重活卡住
    m_timer = new QTimer(this);
    m_timer->setInterval(0);
    connect(m_timer, &QTimer::timeout, this, &SeedMapPage::renderStep);

    // 参数一改就重画（已经有图的前提下）
    connect(m_versionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]() {
                updateSubtitle();
                if (m_hasRendered)
                    startRender(true);
            });
    connect(m_dimCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]() {
                updateSubtitle();
                if (m_hasRendered)
                    startRender(true);
            });
    connect(m_shadeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]() {
                updateSubtitle();
                if (m_hasRendered)
                    startRender(true);
            });
    connect(m_heightCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]() {
                if (m_hasRendered)
                    startRender(true);
            });
    connect(m_heightCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        if (m_hasRendered)
            startRender(true);
    });
    connect(m_seedEdit, &QLineEdit::returnPressed, this, [this]() { startRender(true); });

    updateSubtitle();
    syncZoomLabel();
}

// ── 第二行：结构开关（对齐存档地图） ──

void SeedMapPage::buildOverlayRow(QWidget *bar2, QHBoxLayout *bar2Lay)
{
    auto *structLabel = new QLabel(tr("结构"), bar2);
    structLabel->setObjectName("worldMapToolLabel");
    bar2Lay->addWidget(structLabel);

    // 按钮放进横向滚动容器：26 个按钮的行最小宽度不能把窗口撑大，窄屏横向滚动
    auto *structScroll = new QScrollArea(bar2);
    structScroll->setObjectName(QStringLiteral("worldMapStructScroll"));
    structScroll->setWidgetResizable(true);
    structScroll->setFrameShape(QFrame::NoFrame);
    structScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    structScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    structScroll->setFocusPolicy(Qt::NoFocus);
    structScroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *structInner = new QWidget(structScroll);
    auto *structLay = new QHBoxLayout(structInner);
    structLay->setContentsMargins(0, 0, 0, 0);
    structLay->setSpacing(6);
    structScroll->setWidget(structInner);
    bar2Lay->addWidget(structScroll, 1);

    const auto &defs = StructureFinder::allStructures();
    m_structEnabled = QVector<bool>(defs.size(), true);
    m_biomeEnabled = QVector<bool>(BiomeCatalog::allBiomes().size(), false);
    m_structBtns.reserve(defs.size());
    for (int i = 0; i < defs.size(); ++i)
    {
        const auto &d = defs[i];
        auto *b = new QPushButton(bar2);
        b->setObjectName("worldMapStructBtn");
        b->setCheckable(true);
        b->setChecked(true);
        b->setFixedSize(30, 30);
        b->setCursor(Qt::PointingHandCursor);
        if (d.type == StructureFinder::kUnsupportedType)
        {
            b->setEnabled(false);
            b->setToolTip(QStringLiteral("%1（暂不支持定位）")
                              .arg(QString::fromUtf8(d.displayName)));
        }
        else
        {
            b->setToolTip(QString::fromUtf8(d.displayName));
        }
        b->setIcon(StructureIcons::pixmap(
            i, QLatin1String(d.key),
            QString(QChar::fromLatin1(d.displayName[0])),
            QLatin1String(d.wikiTitle), QLatin1String(d.itemTitle),
            QLatin1String(d.envSprite), 18));
        b->setIconSize(QSize(18, 18));
        structLay->addWidget(b);
        m_structBtns.append(b);

        connect(b, &QPushButton::toggled, this, [this, i](bool on) {
            if (i >= 0 && i < m_structEnabled.size())
                m_structEnabled[i] = on;
            requestStructures();
        });
    }

    auto *selectAll = new QPushButton(tr("全选"), bar2);
    selectAll->setObjectName("worldMapGroupBtn");
    selectAll->setCursor(Qt::PointingHandCursor);
    selectAll->setFixedHeight(26);
    selectAll->setToolTip(tr("显示全部结构标记"));
    connect(selectAll, &QPushButton::clicked, this, [this]() { setAllStructures(true); });
    bar2Lay->addWidget(selectAll);

    auto *clearAll = new QPushButton(tr("清空"), bar2);
    clearAll->setObjectName("worldMapGroupBtn");
    clearAll->setCursor(Qt::PointingHandCursor);
    clearAll->setFixedHeight(26);
    clearAll->setToolTip(tr("隐藏全部结构标记"));
    connect(clearAll, &QPushButton::clicked, this, [this]() { setAllStructures(false); });
    bar2Lay->addWidget(clearAll);

    // ── 右侧「展开」：内容浏览弹窗（搜索 + 结构 / 群系 / 矿物 + 坐标跳转） ──
    m_expandBtn = new QPushButton(tr("展开"), bar2);
    m_expandBtn->setObjectName(QStringLiteral("worldMapExpandBtn"));
    m_expandBtn->setCheckable(true);
    m_expandBtn->setCursor(Qt::PointingHandCursor);
    m_expandBtn->setToolTip(tr("展开内容列表：按名称搜索结构或群系，勾选决定是否在地图上显示"));
    m_expandBtn->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/expand.svg"),
        ThemeManager::instance()->currentThemeColor(), 14));
    m_expandBtn->setIconSize(QSize(14, 14));
    connect(m_expandBtn, &QPushButton::toggled, this, &SeedMapPage::showContentPopup);
    bar2Lay->addWidget(m_expandBtn);
}

// ── 内容弹窗：搜索 + 坐标跳转 + 结构 / 群系 / 矿物三列 ──

void SeedMapPage::buildContentPopup()
{
    if (m_popup)
        return;

    m_popup = new QWidget(this);
    m_popup->setObjectName(QStringLiteral("worldMapContentPopup"));
    m_popup->setAttribute(Qt::WA_StyledBackground, true);
    auto *shadow = new QGraphicsDropShadowEffect(m_popup);
    shadow->setBlurRadius(36);
    shadow->setOffset(0, 10);
    const bool dark = ThemeManager::instance()
                      && ThemeManager::instance()->currentTheme() != ThemeManager::LightTheme;
    shadow->setColor(QColor(0, 0, 0, dark ? 140 : 55));
    m_popup->setGraphicsEffect(shadow);
    m_popup->hide();

    auto *lay = new QVBoxLayout(m_popup);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(12);

    // —— 顶行：搜索框 + 坐标跳转 + 关闭 ——
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(6);

    m_popupSearch = new QLineEdit(m_popup);
    m_popupSearch->setObjectName(QStringLiteral("worldMapPopupSearch"));
    m_popupSearch->setPlaceholderText(tr("搜索结构或群系…"));
    m_popupSearch->setClearButtonEnabled(true);
    m_popupSearch->addAction(IconHelper::loadColoredIcon(
                                 QStringLiteral(":/Images/Icons/search.svg"),
                                 ThemeManager::instance()->currentThemeColor(), 14),
                             QLineEdit::LeadingPosition);
    topRow->addWidget(m_popupSearch, 1);
    connect(m_popupSearch, &QLineEdit::textChanged,
            this, &SeedMapPage::rebuildPopupLists);

    auto *gotoLabel = new QLabel(tr("坐标跳转"), m_popup);
    gotoLabel->setObjectName(QStringLiteral("worldMapPopupColTitle"));
    topRow->addWidget(gotoLabel);

    m_xEdit = new QLineEdit(m_popup);
    m_xEdit->setObjectName("worldMapCoordEdit");
    m_xEdit->setPlaceholderText(tr("X"));
    m_xEdit->setToolTip(tr("要跳转到的方块坐标"));
    m_xEdit->setValidator(new QIntValidator(-30000000, 30000000, m_xEdit));
    m_xEdit->setFixedSize(58, 26);
    topRow->addWidget(m_xEdit);

    m_zEdit = new QLineEdit(m_popup);
    m_zEdit->setObjectName("worldMapCoordEdit");
    m_zEdit->setPlaceholderText(tr("Z"));
    m_zEdit->setToolTip(tr("要跳转到的方块坐标"));
    m_zEdit->setValidator(new QIntValidator(-30000000, 30000000, m_zEdit));
    m_zEdit->setFixedSize(58, 26);
    topRow->addWidget(m_zEdit);

    auto *jumpBtn = new QPushButton(tr("跳转"), m_popup);
    jumpBtn->setObjectName(QStringLiteral("worldMapGroupBtn"));
    jumpBtn->setCursor(Qt::PointingHandCursor);
    jumpBtn->setFixedHeight(26);
    jumpBtn->setToolTip(tr("把视图中心移动到该坐标，并重新生成这一带的地形（回车）"));
    connect(jumpBtn, &QPushButton::clicked, this, &SeedMapPage::jumpToCoordinate);
    connect(m_xEdit, &QLineEdit::returnPressed, this, &SeedMapPage::jumpToCoordinate);
    connect(m_zEdit, &QLineEdit::returnPressed, this, &SeedMapPage::jumpToCoordinate);
    topRow->addWidget(jumpBtn);

    auto *closeBtn = new QPushButton(QStringLiteral("×"), m_popup);
    closeBtn->setObjectName(QStringLiteral("worldMapPopupClose"));
    closeBtn->setFixedSize(28, 28);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip(tr("关闭（Esc）"));
    connect(closeBtn, &QPushButton::clicked, this, [this]() { m_expandBtn->setChecked(false); });
    topRow->addWidget(closeBtn);
    lay->addLayout(topRow);

    // —— 三列：结构 / 群系 / 矿物 ——
    auto *cols = new QHBoxLayout;
    cols->setSpacing(12);

    auto makeColumn = [&](const QString &title, QListWidget **listOut,
                          QPushButton **allOut, QPushButton **clearOut) -> QVBoxLayout * {
        auto *col = new QVBoxLayout;
        col->setSpacing(6);
        auto *head = new QHBoxLayout;
        head->setSpacing(6);
        auto *t = new QLabel(title, m_popup);
        t->setObjectName(QStringLiteral("worldMapPopupColTitle"));
        head->addWidget(t);
        head->addStretch(1);
        auto *all = new QPushButton(tr("全选"), m_popup);
        all->setObjectName(QStringLiteral("worldMapGroupBtn"));
        all->setCursor(Qt::PointingHandCursor);
        all->setFixedHeight(22);
        head->addWidget(all);
        auto *clr = new QPushButton(tr("清空"), m_popup);
        clr->setObjectName(QStringLiteral("worldMapGroupBtn"));
        clr->setCursor(Qt::PointingHandCursor);
        clr->setFixedHeight(22);
        head->addWidget(clr);
        col->addLayout(head);

        auto *list = new QListWidget(m_popup);
        list->setObjectName(QStringLiteral("worldMapPopupList"));
        list->setSelectionMode(QAbstractItemView::NoSelection);
        list->setFocusPolicy(Qt::NoFocus);
        col->addWidget(list, 1);
        *listOut = list;
        *allOut = all;
        *clearOut = clr;
        return col;
    };

    auto *structAll = static_cast<QPushButton *>(nullptr);
    auto *structClear = static_cast<QPushButton *>(nullptr);
    auto *biomeAll = static_cast<QPushButton *>(nullptr);
    auto *biomeClear = static_cast<QPushButton *>(nullptr);
    auto *oreAll = static_cast<QPushButton *>(nullptr);
    auto *oreClear = static_cast<QPushButton *>(nullptr);
    cols->addLayout(makeColumn(tr("结构"), &m_structList, &structAll, &structClear));
    cols->addLayout(makeColumn(tr("群系"), &m_biomeList, &biomeAll, &biomeClear));
    cols->addLayout(makeColumn(tr("矿物"), &m_oreList, &oreAll, &oreClear));
    lay->addLayout(cols, 1);

    auto *hint = new QLabel(
        tr("结构 / 群系都按种子现算，勾选即在地图上显示。矿石要读存档里的区块数据，"
           "种子地图没有存档，只能看群系与结构 —— 想找矿请到实例的「存档地图」里扫描。"),
        m_popup);
    hint->setObjectName(QStringLiteral("worldMapPopupHint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    // 结构列：全选 / 清空 + 勾选变化
    connect(structAll, &QPushButton::clicked, this, [this]() { setAllStructures(true); });
    connect(structClear, &QPushButton::clicked, this, [this]() { setAllStructures(false); });
    connect(m_structList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_structEnabled.size())
            return;
        const bool on = item->checkState() == Qt::Checked;
        if (m_structEnabled[idx] == on)
            return;
        m_structEnabled[idx] = on;
        if (idx < m_structBtns.size() && m_structBtns[idx])
        {
            const QSignalBlocker blocker(m_structBtns[idx]);
            m_structBtns[idx]->setChecked(on);
        }
        requestStructures();
    });

    // 群系列：全选 / 清空 + 勾选变化
    auto setAllBiomes = [this](bool on) {
        for (int i = 0; i < m_biomeEnabled.size(); ++i)
            m_biomeEnabled[i] = on;
        if (m_biomeList)
        {
            const QSignalBlocker blocker(m_biomeList);
            for (int r = 0; r < m_biomeList->count(); ++r)
            {
                QListWidgetItem *item = m_biomeList->item(r);
                const int idx = item->data(Qt::UserRole).toInt();
                item->setCheckState(idx >= 0 && idx < m_biomeEnabled.size()
                                            && m_biomeEnabled[idx] ? Qt::Checked
                                                                   : Qt::Unchecked);
            }
        }
        if (!anyBiomeEnabled())
        {
            m_biomeGrid = BiomeCatalog::SampleGrid();
            m_biomeOverlay = QImage();
            if (m_view)
                m_view->setBiomeOverlay(nullptr, 0, 0, kBiomeStep);
        }
        else
        {
            refreshBiomeGrid();
        }
    };
    connect(biomeAll, &QPushButton::clicked, this, [setAllBiomes]() { setAllBiomes(true); });
    connect(biomeClear, &QPushButton::clicked, this, [setAllBiomes]() { setAllBiomes(false); });
    connect(m_biomeList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_biomeEnabled.size())
            return;
        m_biomeEnabled[idx] = item->checkState() == Qt::Checked;
        if (!anyBiomeEnabled())
        {
            m_biomeGrid = BiomeCatalog::SampleGrid();
            m_biomeOverlay = QImage();
            if (m_view)
                m_view->setBiomeOverlay(nullptr, 0, 0, kBiomeStep);
            return;
        }
        if (m_biomeGrid.ids.isEmpty())
            refreshBiomeGrid(); // 从全清空重新勾选：需要重采网格
        else
            rebuildBiomeOverlay();
    });

    // 矿物列：列表照常显示，但种子地图没有区块数据，全部置灰并说明
    connect(oreAll, &QPushButton::clicked, this, [this]() {
        updateStatus(tr("矿石要读存档区块：种子地图只能预测结构与群系，"
                        "找矿请用实例里的「存档地图」"));
    });
    connect(oreClear, &QPushButton::clicked, this, [this]() {
        updateStatus(tr("矿石要读存档区块：种子地图只能预测结构与群系，"
                        "找矿请用实例里的「存档地图」"));
    });
}

void SeedMapPage::showContentPopup(bool on)
{
    if (on)
    {
        buildContentPopup();
        rebuildPopupLists(m_popupSearch ? m_popupSearch->text() : QString());
        positionContentPopup();
        m_popup->show();
        m_popup->raise();
        if (m_popupSearch)
            m_popupSearch->setFocus();
    }
    else if (m_popup)
    {
        m_popup->hide();
    }
}

void SeedMapPage::positionContentPopup()
{
    if (!m_popup)
        return;
    QSize want(760, 520);
    want.setWidth(qMin(want.width(), qMax(360, width() - 60)));
    want.setHeight(qMin(want.height(), qMax(280, height() - 60)));
    m_popup->resize(want);
    m_popup->move((width() - want.width()) / 2, (height() - want.height()) / 2);
}

void SeedMapPage::rebuildPopupLists(const QString &filter)
{
    if (!m_popup || !m_structList || !m_biomeList || !m_oreList)
        return;

    const QString key = filter.trimmed();

    // —— 结构列 ——
    {
        const QSignalBlocker blocker(m_structList);
        m_structList->clear();
        const auto &defs = StructureFinder::allStructures();
        for (int i = 0; i < defs.size(); ++i)
        {
            const auto &d = defs[i];
            const QString name = QString::fromUtf8(d.displayName);
            if (!key.isEmpty() && !name.contains(key, Qt::CaseInsensitive)
                && !QLatin1String(d.key).contains(key, Qt::CaseInsensitive))
                continue;
            auto *item = new QListWidgetItem(m_structList);
            item->setText(name);
            item->setData(Qt::UserRole, i);
            item->setIcon(QIcon(StructureIcons::pixmap(
                i, QLatin1String(d.key),
                QString(QChar::fromLatin1(d.displayName[0])),
                QLatin1String(d.wikiTitle), QLatin1String(d.itemTitle),
                QLatin1String(d.envSprite), 16)));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(i < m_structEnabled.size() && m_structEnabled[i]
                                    ? Qt::Checked : Qt::Unchecked);
            if (d.type == StructureFinder::kUnsupportedType)
            {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setToolTip(tr("暂不支持定位"));
            }
        }
    }

    // —— 群系列（按当前维度过滤） ——
    {
        const QSignalBlocker blocker(m_biomeList);
        m_biomeList->clear();
        const int cdim = cubiomesDim();
        const auto &bdefs = BiomeCatalog::allBiomes();
        for (int i = 0; i < bdefs.size(); ++i)
        {
            const auto &d = bdefs[i];
            if (d.dim != cdim)
                continue;
            const QString name = QString::fromUtf8(d.displayName);
            if (!key.isEmpty() && !name.contains(key, Qt::CaseInsensitive)
                && !QLatin1String(d.key).contains(key, Qt::CaseInsensitive))
                continue;
            auto *item = new QListWidgetItem(m_biomeList);
            item->setText(name);
            item->setData(Qt::UserRole, i);
            item->setIcon(QIcon(StructureIcons::pixmap(
                2000 + i, QStringLiteral("biome_") + QLatin1String(d.key),
                QString(QChar::fromLatin1(d.displayName[0])),
                QLatin1String(d.wikiTitle), QString(), QString(), 16)));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(i < m_biomeEnabled.size() && m_biomeEnabled[i]
                                    ? Qt::Checked : Qt::Unchecked);
        }
    }

    // —— 矿物列（同样按维度过滤；种子地图没有区块数据，全部置灰） ——
    {
        const QSignalBlocker blocker(m_oreList);
        m_oreList->clear();
        const int cdim = cubiomesDim();
        const auto &odes = OreFinder::allOres();
        for (int i = 0; i < odes.size(); ++i)
        {
            const auto &d = odes[i];
            if (d.dim != cdim)
                continue;
            const QString name = QString::fromUtf8(d.displayName);
            if (!key.isEmpty() && !name.contains(key, Qt::CaseInsensitive)
                && !QLatin1String(d.key).contains(key, Qt::CaseInsensitive))
                continue;
            auto *item = new QListWidgetItem(m_oreList);
            item->setText(name);
            item->setData(Qt::UserRole, i);
            item->setIcon(QIcon(StructureIcons::pixmap(
                3000 + i, QStringLiteral("ore_") + QLatin1String(d.key),
                QString(QChar::fromLatin1(d.displayName[0])),
                QLatin1String(d.wikiTitle), QString(), QString(), 16)));
            item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEnabled);
            item->setCheckState(Qt::Unchecked);
            item->setToolTip(tr("种子地图没有存档区块，矿石无法预测"));
        }
    }
}

void SeedMapPage::setAllStructures(bool on)
{
    for (int i = 0; i < m_structEnabled.size(); ++i)
        m_structEnabled[i] = on;
    for (int i = 0; i < m_structBtns.size(); ++i)
    {
        if (!m_structBtns[i])
            continue;
        const QSignalBlocker blocker(m_structBtns[i]);
        m_structBtns[i]->setChecked(on);
    }
    if (m_structList)
    {
        const QSignalBlocker blocker(m_structList);
        for (int r = 0; r < m_structList->count(); ++r)
        {
            QListWidgetItem *item = m_structList->item(r);
            const int idx = item->data(Qt::UserRole).toInt();
            if (item->flags() & Qt::ItemIsEnabled)
                item->setCheckState(idx >= 0 && idx < m_structEnabled.size()
                                            && m_structEnabled[idx] ? Qt::Checked
                                                                    : Qt::Unchecked);
        }
    }
    requestStructures();
}

bool SeedMapPage::anyStructureEnabled() const
{
    for (bool b : m_structEnabled)
        if (b)
            return true;
    return false;
}

bool SeedMapPage::anyBiomeEnabled() const
{
    for (bool b : m_biomeEnabled)
        if (b)
            return true;
    return false;
}

void SeedMapPage::requestStructures()
{
    if (!m_view)
        return;
    if (!m_structWatcher)
    {
        m_structWatcher = new QFutureWatcher<QVector<SeedStructureMarker>>(this);
        connect(m_structWatcher, &QFutureWatcher<QVector<SeedStructureMarker>>::finished,
                this, [this]() {
                    if (!m_view || !m_structWatcher)
                        return;
                    m_view->setMarkers(m_structWatcher->result());
                });
    }

    bool seedOk = false;
    const quint64 seed = seedValue(&seedOk);
    const int mc = mcValue();
    if (!seedOk || mc == 0 || !anyStructureEnabled())
    {
        m_view->setMarkers({});
        return;
    }

    const QRect rect = m_view->blockRect();
    const int cdim = cubiomesDim();
    const QVector<bool> enabled = m_structEnabled;

    m_structWatcher->setFuture(QtConcurrent::run([seed, mc, cdim, rect, enabled]() {
        QVector<SeedStructureMarker> out;
        const auto hits = StructureFinder::find(seed, mc, cdim,
                                                rect.left(), rect.top(),
                                                rect.right(), rect.bottom(),
                                                enabled);
        out.reserve(hits.size());
        for (const auto &h : hits)
        {
            SeedStructureMarker m;
            m.x = h.x;
            m.z = h.z;
            m.type = h.structIdx;
            out.append(m);
        }
        return out;
    }));
}

void SeedMapPage::refreshBiomeGrid()
{
    if (!m_view || !anyBiomeEnabled())
        return;
    bool seedOk = false;
    const quint64 seed = seedValue(&seedOk);
    const int mc = mcValue();
    if (!seedOk || mc == 0)
        return;

    const QRect rect = m_view->blockRect();
    m_biomeGrid = BiomeCatalog::sampleGrid(seed, mc, cubiomesDim(),
                                           rect.left(), rect.top(),
                                           rect.right(), rect.bottom(), kBiomeStep);
    rebuildBiomeOverlay();
}

void SeedMapPage::rebuildBiomeOverlay()
{
    if (!m_view)
        return;
    if (m_biomeGrid.ids.isEmpty() || m_biomeGrid.nx <= 0 || m_biomeGrid.nz <= 0)
    {
        m_biomeOverlay = QImage();
        m_view->setBiomeOverlay(nullptr, 0, 0, kBiomeStep);
        return;
    }

    const auto &bdefs = BiomeCatalog::allBiomes();
    QHash<int, QColor> colorOf;
    for (int i = 0; i < bdefs.size() && i < m_biomeEnabled.size(); ++i)
    {
        if (!m_biomeEnabled[i])
            continue;
        colorOf.insert(bdefs[i].id, bdefs[i].color);
    }

    QImage img(m_biomeGrid.nx, m_biomeGrid.nz, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    for (int j = 0; j < m_biomeGrid.nz; ++j)
    {
        for (int i = 0; i < m_biomeGrid.nx; ++i)
        {
            const int id = m_biomeGrid.ids[j * m_biomeGrid.nx + i];
            const auto it = colorOf.constFind(id);
            if (it == colorOf.constEnd())
                continue;
            QColor c = it.value();
            c.setAlpha(160);
            img.setPixelColor(i, j, c);
        }
    }
    m_biomeOverlay = img;
    m_view->setBiomeOverlay(&m_biomeOverlay, m_biomeGrid.minX,
                            m_biomeGrid.minZ, m_biomeGrid.step);
}

// ── 页面生命周期 / 渲染 ──

void SeedMapPage::openPage()
{
    if (!m_hasRendered)
        startRender();
    setFocus(Qt::OtherFocusReason);
}

void SeedMapPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_hasRendered && isVisible())
        startRender();
}

void SeedMapPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_popup && m_popup->isVisible())
        positionContentPopup();
}

void SeedMapPage::refresh()
{
    startRender();
}

void SeedMapPage::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_popup && m_popup->isVisible())
    {
        if (m_expandBtn)
            m_expandBtn->setChecked(false);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)
    {
        if (m_view)
            m_view->zoomBy(1.25);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Minus)
    {
        if (m_view)
            m_view->zoomBy(1.0 / 1.25);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_0)
    {
        if (m_view)
            m_view->fitToView();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_G)
    {
        if (m_gridBtn)
            m_gridBtn->toggle();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_R)
    {
        startRender(true);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

quint64 SeedMapPage::seedValue(bool *ok) const
{
    const QString text = m_seedEdit ? m_seedEdit->text().trimmed() : QString();
    if (text.isEmpty())
    {
        if (ok)
            *ok = false;
        return 0;
    }

    bool numeric = false;
    const qlonglong value = text.toLongLong(&numeric);
    if (numeric)
    {
        if (ok)
            *ok = true;
        return static_cast<quint64>(value);
    }

    // 与游戏一致：文字种子走 Java String.hashCode()（32 位有符号）再当长整型种子
    quint32 h = 0;
    for (const QChar c : text)
        h = h * 31u + static_cast<quint32>(c.unicode());
    if (ok)
        *ok = true;
    return static_cast<quint64>(static_cast<qint64>(static_cast<qint32>(h)));
}

int SeedMapPage::mcValue() const
{
    if (!m_versionCombo)
        return 0;
    return StructureFinder::versionToInt(m_versionCombo->currentData().toString());
}

int SeedMapPage::dimensionValue() const
{
    return m_dimCombo ? m_dimCombo->currentData().toInt() : 0;
}

int SeedMapPage::shadeValue() const
{
    return m_shadeCombo ? m_shadeCombo->currentData().toInt() : 0;
}

int SeedMapPage::heightValue() const
{
    if (!m_heightCombo)
        return -1;
    // 手动输入的数字优先，其次看下拉项的数据
    bool ok = false;
    const QString text = m_heightCombo->currentText().trimmed();
    const int typed = text.toInt(&ok);
    if (ok)
        return typed;
    return m_heightCombo->currentData().toInt();
}

int SeedMapPage::cubiomesDim() const
{
    switch (static_cast<WorldMapRenderer::Dimension>(dimensionValue()))
    {
    case WorldMapRenderer::Dimension::Nether: return -1;
    case WorldMapRenderer::Dimension::End:    return 1;
    default:                                  return 0;
    }
}

void SeedMapPage::startRender(bool keepCenter)
{
    bool seedOk = false;
    const quint64 seed = seedValue(&seedOk);
    if (!seedOk)
    {
        updateStatus(tr("先填一个种子（数字或文字都行）"));
        return;
    }

    const int mc = mcValue();
    if (mc == 0)
    {
        updateStatus(tr("这个版本还预测不了，换一个版本试试"));
        return;
    }

    stopRender();

    // 中心：沿用当前视角中心，或用生成 / 跳转时指定的坐标
    QPointF center(m_centerX, m_centerZ);
    if (keepCenter && m_hasRendered && m_view)
        center = m_view->centerBlock();
    m_centerX = center.x();
    m_centerZ = center.y();

    const int centerRegionX = qRound(center.x() / kRegionBlocks);
    const int centerRegionZ = qRound(center.y() / kRegionBlocks);
    m_originRegionX = centerRegionX - kRegions / 2;
    m_originRegionZ = centerRegionZ - kRegions / 2;

    if (m_view)
    {
        m_view->resetCanvas(QRect(m_originRegionX * kRegionBlocks,
                                  m_originRegionZ * kRegionBlocks,
                                  kRegions * kRegionBlocks, kRegions * kRegionBlocks));
    }

    m_pendingRegions = ringOrderedRegions(centerRegionX, centerRegionZ);
    m_renderedCount = 0;

    if (m_progress)
    {
        m_progress->setRange(0, kRegions * kRegions);
        m_progress->setValue(0);
        m_progress->show();
    }

    updateStatus(tr("正在渲染… 0/%1（版本 %2 · %3）")
                     .arg(m_pendingRegions.size())
                     .arg(m_versionCombo->currentData().toString(),
                          m_dimCombo->currentText()));

    m_timer->start();
}

void SeedMapPage::stopRender()
{
    if (m_timer)
        m_timer->stop();
    m_pendingRegions.clear();
}

void SeedMapPage::renderStep()
{
    if (m_pendingRegions.isEmpty())
    {
        m_timer->stop();
        if (m_view)
        {
            if (!m_hasRendered)
                m_view->fitToView(); // 第一次出图：整张先放进窗口
            else
                m_view->centerOnBlock(m_centerX, m_centerZ);
        }
        m_hasRendered = true;
        if (m_progress)
            m_progress->hide();
        syncZoomLabel();
        updateViewInfo();
        updateStatus(tr("渲染完成 · %1 个区域 · 种子 %2")
                         .arg(kRegions * kRegions)
                         .arg(m_seedEdit->text().trimmed()));

        // 画布范围定了：结构标记 + 群系叠加层一起刷新
        requestStructures();
        if (anyBiomeEnabled())
            refreshBiomeGrid();
        return;
    }

    const QPoint region = m_pendingRegions.takeFirst();

    bool seedOk = false;
    const quint64 seed = seedValue(&seedOk);
    const QImage tile = SeedTerrainRenderer::renderRegionTile(
        region.x(), region.y(),
        static_cast<WorldMapRenderer::Dimension>(dimensionValue()),
        static_cast<WorldMapRenderer::ShadeMode>(shadeValue()),
        seed, mcValue(), kTilePx, heightValue());

    if (m_view && !tile.isNull())
        m_view->putTile(region.x(), region.y(), tile);

    ++m_renderedCount;
    if (m_progress)
        m_progress->setValue(m_renderedCount);
    updateStatus(tr("正在渲染… %1/%2（版本 %3 · %4）")
                     .arg(m_renderedCount)
                     .arg(m_renderedCount + m_pendingRegions.size())
                     .arg(m_versionCombo->currentData().toString(),
                          m_dimCombo->currentText()));
}

void SeedMapPage::jumpToCoordinate()
{
    bool okX = false, okZ = false;
    const double x = m_xEdit->text().trimmed().toDouble(&okX);
    const double z = m_zEdit->text().trimmed().toDouble(&okZ);
    if (!okX || !okZ)
    {
        updateStatus(tr("坐标要填数字，例如 X=100、Z=-200"));
        return;
    }

    m_centerX = x;
    m_centerZ = z;
    startRender(false);
}

void SeedMapPage::exportImage()
{
    const QImage img = m_view ? m_view->mapImage() : QImage();
    if (img.isNull() || m_view->isEmpty())
    {
        updateStatus(tr("还没有可导出的地图，先生成一张"));
        return;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    // 文件名里不能出现 : / \ 这类字符（文字种子可能带）
    QString seedName = m_seedEdit->text().trimmed();
    for (const QChar bad : QStringLiteral("\\/:*?\"<>|"))
        seedName.replace(bad, QLatin1Char('_'));
    if (seedName.isEmpty())
        seedName = QStringLiteral("map");
    const QString suggested = QStringLiteral("%1/种子地图_%2.png")
                                  .arg(dir.isEmpty() ? QStringLiteral(".") : dir, seedName);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出种子地图"), suggested, tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;

    if (img.save(path))
        updateStatus(tr("已导出：%1").arg(path));
    else
        updateStatus(tr("导出失败，换个位置再试"));
}

void SeedMapPage::updateSubtitle()
{
    if (!m_subtitleLabel)
        return;
    m_subtitleLabel->setText(tr("%1 · %2 · %3")
                                 .arg(m_versionCombo->currentData().toString(),
                                      m_dimCombo->currentText(),
                                      m_shadeCombo->currentText()));
}

void SeedMapPage::syncZoomLabel()
{
    if (!m_zoomLabel || !m_view)
        return;
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_view->zoom() * 100)));
}

void SeedMapPage::updateViewInfo()
{
    if (!m_infoLabel || !m_view || !m_hasRendered)
        return;
    const QPointF c = m_view->centerBlock();
    m_infoLabel->setText(tr("中心 X %1 · Z %2 · 缩放 %3%")
                             .arg(qRound(c.x()))
                             .arg(qRound(c.y()))
                             .arg(qRound(m_view->zoom() * 100)));
}

void SeedMapPage::updateStatus(const QString &text)
{
    if (m_statusLabel)
        m_statusLabel->setText(text);
}
