/**
 * @file   WorldMapPage.cpp
 * @brief  存档地表地图页面实现
 * @author BlockBox Team
 * @date   2026-10-08
 */

#include "WorldMapPage.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QFuture>
#include <QFutureWatcher>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrentRun>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <utility>

#include "components/AppFileDialog.h"
#include "components/NotificationManager.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/WorldMap/BiomeCatalog.h"
#include "utils/WorldMap/BiomeName.h"
#include "utils/WorldMap/StructureFinder.h"
#include "utils/WorldMap/StructureIcons.h"
#include "utils/Saves/LevelDatEditor.h"
#include "utils/ThemeManager.h"

// ═══════════════════════ 地图画布 ═══════════════════════

namespace {

/** 未生成区块的底色（地图范围内的透明像素会露出它） */
const QColor kVoidColor(0x31, 0x34, 0x3A);
/** 区块网格 / 区域网格线颜色 */
const QColor kChunkGrid(255, 255, 255, 28);
const QColor kRegionGrid(255, 255, 255, 78);
/** 同时渲染的 1:1 细节瓦片数（放大后并行补细节） */
constexpr int kMaxConcurrentTiles = 3;
/** 每方块低于此屏幕像素数时不加载细节 —— 方块太小，只看概览就够 */
constexpr double kDetailMinBlockPx = 0.5;
/** 中间地图卡片圆角（与顶部工具栏 14px 对齐） */
constexpr qreal kMapCardRadius = 14.0;

/** 地图卡片底色（@BG_BASE@；不透，自定义背景不会从地图区透出） */
QColor mapCardBackgroundColor()
{
    if (!ThemeManager::instance())
        return QColor(0xf5, 0xf5, 0xf5);
    switch (ThemeManager::instance()->currentTheme())
    {
    case ThemeManager::LightTheme: return QColor(0xf5, 0xf5, 0xf5);
    case ThemeManager::DarkTheme:  return QColor(0x1a, 0x1a, 0x1a);
    default:                       return QColor(0x2a, 0x2a, 0x2a);
    }
}

/** 地图卡片描边（@BORDER@，与 QSS 卡片描边同值） */
QColor mapCardBorderColor()
{
    if (!ThemeManager::instance())
        return QColor(0xe8, 0xea, 0xed);
    switch (ThemeManager::instance()->currentTheme())
    {
    case ThemeManager::LightTheme: return QColor(0xe8, 0xea, 0xed);
    case ThemeManager::DarkTheme:  return QColor(0x3a, 0x3a, 0x3a);
    default:                       return QColor(0x40, 0x40, 0x40);
    }
}

} // namespace

/** 可拖拽 / 滚轮缩放的地表地图画布（仅本文件使用） */
class WorldMapView : public QWidget
{
public:
    explicit WorldMapView(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(240, 180);
    }

    /** 缩放比例变化（1.0 = 1 图像像素对应 1 屏幕像素） */
    std::function<void(double)> onZoomChanged;
    /** 鼠标指针所在的方块坐标变化 */
    std::function<void(qint64, qint64)> onCursorMoved;
    /** 右键请求菜单：全局坐标 + 该点方块坐标（由页面弹 QMenu） */
    std::function<void(const QPoint &globalPos, qint64 blockX, qint64 blockZ)> onContextMenu;
    /** 一个待绘制的结构标记 */
    struct Marker
    {
        qint64 x = 0;   ///< 方块坐标
        qint64 z = 0;
        int type = -1;  ///< allStructures() 下标
    };

    /** 替换画布上的结构标记（视口 / 开关 / 维度变化时调用） */
    void setMarkers(const QVector<Marker> &markers)
    {
        m_markers = markers;
        update();
    }

    /**
     * 群系叠加层：一张与采样网格对齐的小图（一格一像素），
     * 由页面按「选中群系 → 上色 / 未选中 → 透明」重建。
     * 传空图关闭叠加。坐标为方块级网格原点 + 格边长。
     */
    void setBiomeOverlay(const QImage *image, qint64 minBlockX,
                         qint64 minBlockZ, int cellSize)
    {
        m_bioOverlay = image;
        m_bioMinX = minBlockX;
        m_bioMinZ = minBlockZ;
        m_bioCell = qMax(1, cellSize);
        update();
    }

    /**
     * 高亮圈：在指定方块位置画一圈主题色脉冲光环（「跳转并高亮」用）。
     * 传 false 清除。
     */
    void setHighlight(qint64 x, qint64 z, bool on)
    {
        if (m_hlOn == on && m_hlX == x && m_hlZ == z)
            return;
        m_hlOn = on;
        m_hlX = x;
        m_hlZ = z;
        if (on && !m_hlTimer)
        {
            m_hlTimer = new QTimer(this);
            m_hlTimer->setInterval(50);
            connect(m_hlTimer, &QTimer::timeout, this,
                    [this]() { update(); });
        }
        if (m_hlTimer)
        {
            if (on)
            {
                m_hlPhase.start();
                m_hlTimer->start();
            }
            else
            {
                m_hlTimer->stop();
            }
        }
        update();
    }

    /** 当前高亮位置（探针断言用；未高亮返回 false） */
    bool highlightPos(qint64 *x, qint64 *z) const
    {
        if (!m_hlOn)
            return false;
        if (x) *x = m_hlX;
        if (z) *z = m_hlZ;
        return true;
    }

    /** 当前标记数（探针断言用） */
    int markerCount() const { return m_markers.size(); }

    /**
     * 已完成标记集合：key = "type_x_z"。命中的图标以半透明绘制。
     * （页面负责持久化，这里只管显示）
     */
    void setDoneStructures(const QSet<QString> &keys)
    {
        m_doneKeys = keys;
        update();
    }

    bool isStructureDone(int type, qint64 x, qint64 z) const
    {
        return m_doneKeys.contains(doneKey(type, x, z));
    }

    /** 第 idx 个标记的屏幕中心（视口外 / 无效返回 QPoint()；探针用） */
    QPoint markerScreenPos(int idx) const
    {
        if (idx < 0 || idx >= m_markers.size() || !valid())
            return QPoint();
        const Marker &m = m_markers[idx];
        const double ox = double(m_minChunkX) * 16.0;
        const double oz = double(m_minChunkZ) * 16.0;
        const QPointF c(m_offset.x() + (double(m.x) - ox) / m_bpp * m_scale,
                        m_offset.y() + (double(m.z) - oz) / m_bpp * m_scale);
        if (!QRectF(0, 0, width(), height()).contains(c))
            return QPoint();
        return c.toPoint();
    }

    /** 第 idx 个标记的方块坐标（打包 QPoint(x, z)；探针用） */
    QPoint markerBlockPos(int idx) const
    {
        if (idx < 0 || idx >= m_markers.size())
            return QPoint();
        return QPoint(int(m_markers[idx].x), int(m_markers[idx].z));
    }

    /** 第 idx 个标记的结构类型（探针用） */
    int markerType(int idx) const
    {
        if (idx < 0 || idx >= m_markers.size())
            return -1;
        return m_markers[idx].type;
    }

    /** 点击结构图标（未命中图标时走区块选中） */
    std::function<void(const QPoint &globalPos, int structIdx, qint64 x, qint64 z)>
        onStructureClicked;

    /** 完成标记的稳定 key（类型 + 坐标，与存档无关——存档维度由页面分文件保存） */
    static QString doneKey(int type, qint64 x, qint64 z)
    {
        return QString::number(type) + QLatin1Char('_')
             + QString::number(x) + QLatin1Char('_')
             + QString::number(z);
    }

    /** 选中区块变化（hasSelection, chunkX, chunkZ） */
    std::function<void(bool, qint64, qint64)> onSelectionChanged;
    /**
     * 视口（平移 / 缩放 / 图像尺寸）变化后触发 —— 页面据此调度
     * 视口内 region 的 1:1 细节瓦片加载。
     */
    std::function<void()> onViewportChanged;

    /**
     * 绑定渲染中的画布（不拷贝数据，避免渐进贴图时整图 detach）。
     * @param forceFit 是否强制重新自适应窗口
     */
    void setImageRef(const QImage *image, int minChunkX, int minChunkZ, int bpp,
                     bool forceFit)
    {
        const bool geometryChanged = !image || image->isNull()
            || m_image == nullptr
            || m_imageWidth != image->width()
            || m_imageHeight != image->height()
            || m_minChunkX != minChunkX
            || m_minChunkZ != minChunkZ
            || m_bpp != bpp;

        m_image = image;
        if (!image || image->isNull())
        {
            m_imageWidth = 0;
            m_imageHeight = 0;
            update();
            return;
        }

        m_imageWidth = image->width();
        m_imageHeight = image->height();
        m_minChunkX = minChunkX;
        m_minChunkZ = minChunkZ;
        m_bpp = std::max(1, bpp);

        if (geometryChanged || forceFit)
        {
            // 新画布：不整图自适应，按固定区块数把出生点放到中心
            resetInitialView(m_initialChunks);
            if (forceFit)
                scheduleInitialView();
        }
        else
            update();
        emitViewport();
    }

    /**
     * 视图刚从空态切过来时还没有真实几何（QStackedWidget 要等布局 pass 才 resize），
     * 先按旧尺寸设一次初始视图，再排一次延迟执行、用真实几何纠正。
     */
    void scheduleInitialView()
    {
        m_pendingFit = true;
        QTimer::singleShot(0, this, [this]() {
            if (!m_pendingFit)
                return;
            if (!valid())
            {
                m_pendingFit = false;
                return;
            }
            // 还没拿到真实几何：保持 pending，交给 resizeEvent 收尾
            if (width() < 64 || height() < 64)
                return;
            m_pendingFit = false;
            resetInitialView(m_initialChunks);
        });
    }

    /** 画布内容变化（渐进渲染贴了新图块）后刷新显示 */
    void refreshImage()
    {
        if (valid())
            update();
    }

    void clearImage()
    {
        m_image = nullptr;
        m_imageWidth = 0;
        m_imageHeight = 0;
        m_overviewPix = QPixmap();
        m_overviewPixScale = -1.0;
        m_tiles.clear();
        update();
        emitViewport();
    }

    bool hasImage() const { return valid(); }

    QImage image() const { return m_image ? *m_image : QImage(); }

    void setGridVisible(bool on)
    {
        m_grid = on;
        update();
    }

    double scale() const { return m_scale; }

    /** 一个方块占几个屏幕像素（= scale / bpp）。1.0 就是"1 像素 = 1 方块" */
    double blockScreenSize() const { return m_scale / double(qMax(1, m_bpp)); }

    /** 一键跳到 1 像素 = 1 方块（视口中心保持不动） */
    void zoomToOneOne()
    {
        if (!valid())
            return;
        const double target = double(qMax(1, m_bpp));
        if (std::abs(m_scale - target) < 1e-9)
            return;
        zoomAt(QPointF(rect().center()), target / m_scale);
    }

    void fitToWindow()
    {
        if (!valid() || width() <= 0 || height() <= 0)
            return;
        const double sx = double(width()) / double(m_imageWidth);
        const double sy = double(height()) / double(m_imageHeight);
        m_scale = std::clamp(std::min(sx, sy) * 0.94, kMinScale, kMaxScale);
        m_offset = QPointF((width() - m_imageWidth * m_scale) * 0.5,
                           (height() - m_imageHeight * m_scale) * 0.5);
        emitZoom();
        emitViewport();
        update();
    }

    /**
     * 初始视图：横向固定显示 visibleChunks 个区块，中心 = 出生点
     * （没读到出生点就用图像几何中心）。**不做整图自适应** —— 打开地图是
     * 从玩家所在处出发看局部，全览留给「适应窗口」/ 0。
     */
    void resetInitialView(int visibleChunks)
    {
        if (!valid() || width() <= 0 || visibleChunks <= 0)
            return;
        // 视口宽度 = visibleChunks × 16 方块；再按 bpp 换算成图像像素比例
        const double blockPx = double(width()) / double(visibleChunks * 16);
        m_scale = std::clamp(blockPx * double(qMax(1, m_bpp)),
                             kMinScale, kMaxScale);

        if (m_hasInitialCenter)
            centerOnBlock(m_initialCenter.x(), m_initialCenter.y());
        else
            centerOnBlock(qint64(m_minChunkX) * 16 + qint64(m_imageWidth) * m_bpp / 2,
                          qint64(m_minChunkZ) * 16 + qint64(m_imageHeight) * m_bpp / 2);
        emitZoom();
        emitViewport();
        update();
    }

    /** 由页面在换存档时喂入出生点（读不到就传 false，回退图像几何中心） */
    void setInitialCenter(qint64 blockX, qint64 blockZ, bool hasSpawn)
    {
        m_hasInitialCenter = hasSpawn;
        m_initialCenter = QPoint(int(blockX), int(blockZ));
    }

    void setInitialVisibleChunks(int chunks)
    {
        m_initialChunks = qMax(4, chunks);
    }

    /** 视口中心所在的方块坐标（探针用来验证初始中心是出生点） */
    QPoint centerBlock() const
    {
        qint64 bx = 0, bz = 0;
        blockAt(rect().center(), &bx, &bz);
        return QPoint(int(bx), int(bz));
    }

    /** 以某个屏幕点为锚缩放（鼠标滚轮用） */
    void zoomAt(const QPointF &anchor, double factor)
    {
        if (!valid())
            return;
        const QPointF imagePt = (anchor - m_offset) / m_scale;
        const double next = std::clamp(m_scale * factor, kMinScale, kMaxScale);
        m_scale = next;
        m_offset = anchor - imagePt * m_scale;
        emitZoom();
        emitViewport();
        update();
    }

    void zoomBy(double factor) { zoomAt(QPointF(rect().center()), factor); }

    void centerOnBlock(qint64 blockX, qint64 blockZ)
    {
        if (!valid())
            return;
        m_pendingFit = false; // 显式居中取消挂起的初始视图，免得回调又拉回出生点
        const double ix = double(blockX - qint64(m_minChunkX) * 16) / m_bpp;
        const double iz = double(blockZ - qint64(m_minChunkZ) * 16) / m_bpp;
        m_offset = QPointF(width() * 0.5 - ix * m_scale,
                           height() * 0.5 - iz * m_scale);
        update();
    }

    /** 屏幕点 → 方块坐标（图像范围外也返回真实坐标） */
    void blockAt(const QPoint &pos, qint64 *bx, qint64 *bz) const
    {
        const double ix = (pos.x() - m_offset.x()) / m_scale;
        const double iz = (pos.y() - m_offset.y()) / m_scale;
        *bx = qint64(m_minChunkX) * 16 + qRound(ix * m_bpp);
        *bz = qint64(m_minChunkZ) * 16 + qRound(iz * m_bpp);
    }

    /** 平移视图（快捷键 WASD / 方向键用，单位为屏幕像素） */
    void panBy(const QPoint &delta)
    {
        if (!valid())
            return;
        m_offset += delta;
        emitViewport();
        update();
    }

    bool hasSelection() const { return m_hasSelection; }

    void selectedChunk(qint64 *chunkX, qint64 *chunkZ) const
    {
        if (chunkX) *chunkX = m_selChunkX;
        if (chunkZ) *chunkZ = m_selChunkZ;
    }

    void clearSelection()
    {
        if (!m_hasSelection)
            return;
        m_hasSelection = false;
        update();
        notifySelection();
    }

    // ── 1:1 细节瓦片（region → 512×512，1 像素 = 1 方块） ──

    void setTile(int rx, int rz, const QImage &tile)
    {
        if (tile.isNull())
            return;
        m_tiles.insert(QPoint(rx, rz), tile);
        update();
    }

    void removeTile(const QPoint &key)
    {
        if (m_tiles.remove(key) > 0)
            update();
    }

    void clearTiles()
    {
        if (m_tiles.isEmpty())
            return;
        m_tiles.clear();
        update();
    }

    bool hasTile(const QPoint &key) const { return m_tiles.contains(key); }
    int tileCount() const { return m_tiles.size(); }

    /** 当前视口覆盖的 region 闭区间（横纵各自闭区间，可能很大） */
    QRect visibleRegionRange() const
    {
        if (!valid() || width() <= 0 || height() <= 0)
            return QRect();
        const double imgL = (0.0 - m_offset.x()) / m_scale;
        const double imgR = (double(width()) - m_offset.x()) / m_scale;
        const double imgT = (0.0 - m_offset.y()) / m_scale;
        const double imgB = (double(height()) - m_offset.y()) / m_scale;
        const double ox = double(m_minChunkX) * 16.0;
        const double oz = double(m_minChunkZ) * 16.0;
        const double bL = ox + imgL * m_bpp;
        const double bR = ox + imgR * m_bpp;
        const double bT = oz + imgT * m_bpp;
        const double bB = oz + imgB * m_bpp;

        auto toRegion = [](double block) {
            return int(std::floor(block / 512.0));
        };
        // 右/下是开区间，先减一个极小量再取整，避免贴边多算一行
        const double eps = 1e-6;
        return QRect(QPoint(toRegion(bL), toRegion(bT)),
                     QPoint(toRegion(bR - eps), toRegion(bB - eps)));
    }

    /**
     * 画布图像覆盖的 region 闭区间：细节瓦片调度必须裁剪在画布内 ——
     * 种子预测会让视口拖出图像后仍渲得出内容，画到边框外会很怪
     */
    QRect imageRegionRange() const
    {
        if (!valid())
            return QRect();
        const double ox = double(m_minChunkX) * 16.0;
        const double oz = double(m_minChunkZ) * 16.0;
        const double eps = 1e-6;
        auto toRegion = [](double block) {
            return int(std::floor(block / 512.0));
        };
        return QRect(QPoint(toRegion(ox), toRegion(oz)),
                     QPoint(toRegion(ox + double(m_imageWidth) * m_bpp - eps),
                            toRegion(oz + double(m_imageHeight) * m_bpp - eps)));
    }

    /** 视口中心所在的 region（细节加载按到它的距离排序） */
    QPoint centerRegion() const
    {
        if (!valid())
            return QPoint();
        qint64 bx = 0, bz = 0;
        blockAt(rect().center(), &bx, &bz);
        // 负坐标要向下取整（int 除法向零截断会把 -1 归到 region 0）
        return QPoint(int(std::floor(double(bx) / 512.0)),
                      int(std::floor(double(bz) / 512.0)));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 中间地图区：圆角不透卡片 —— 底色固定主题底色，自定义背景（壁纸/纯色）
        // 不会从地图区透出；四角圆角外仍透出页面背景，形成悬浮卡片观感
        const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(outer, kMapCardRadius, kMapCardRadius);
        p.fillPath(clip, mapCardBackgroundColor());
        p.setClipPath(clip);

        if (!valid())
        {
            p.setPen(palette().color(QPalette::Mid));
            p.drawText(rect(), Qt::AlignCenter,
                       QStringLiteral("还没有可显示的地图"));
            p.setClipping(false);
            p.setPen(QPen(mapCardBorderColor(), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(outer, kMapCardRadius, kMapCardRadius);
            return;
        }

        const QRectF target(m_offset.x(), m_offset.y(),
                            m_imageWidth * m_scale,
                            m_imageHeight * m_scale);
        p.fillRect(target, kVoidColor);

        // 底层：概览（先大致） → 上层：已加载的 1:1 瓦片（再精细）
        drawOverview(p, target);
        drawTiles(p);
        drawBiomeOverlay(p);

        if (m_grid)
            drawGrid(p, target);

        drawMarkers(p);
        drawHighlight(p);

        if (m_hasSelection)
            drawSelection(p, target);

        // 显式清 brush：drawMarkers 的白描边若泄漏，这里会把整图画布刷白
        p.setBrush(Qt::NoBrush);
        p.setPen(QColor(255, 255, 255, 32));
        p.drawRect(target);

        p.setClipping(false);
        p.setPen(QPen(mapCardBorderColor(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(outer, kMapCardRadius, kMapCardRadius);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        // 布局刚把真实尺寸交给视图时，用它纠正此前的自适应
        if (m_pendingFit && valid())
        {
            m_pendingFit = false;
            resetInitialView(m_initialChunks);
        }
        emitViewport();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (!valid())
        {
            QWidget::mousePressEvent(event);
            return;
        }

        if (event->button() == Qt::LeftButton
            || event->button() == Qt::MiddleButton)
        {
            // 左键/中键都能拖拽平移；区分"单击"与"拖拽"靠移动距离阈值
            m_panning = true;
            m_moved = false;
            m_pressPos = event->pos();
            m_lastPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }

        if (event->button() == Qt::RightButton && onContextMenu)
        {
            qint64 bx = 0, bz = 0;
            blockAt(event->pos(), &bx, &bz);
            onContextMenu(event->globalPosition().toPoint(), bx, bz);
            event->accept();
            return;
        }

        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_panning)
        {
            const QPoint delta = event->pos() - m_lastPos;
            m_lastPos = event->pos();
            m_offset += delta;
            if ((event->pos() - m_pressPos).manhattanLength() >= kClickSlop)
                m_moved = true;
            update();
        }
        if (onCursorMoved && valid())
        {
            qint64 bx = 0, bz = 0;
            blockAt(event->pos(), &bx, &bz);
            onCursorMoved(bx, bz);
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_panning
            && (event->button() == Qt::LeftButton
                || event->button() == Qt::MiddleButton))
        {
            m_panning = false;
            unsetCursor();
            // 没有拖动 → 视为点击：先看是否点在结构图标上（弹信息窗），
            // 未命中才切换该区块的选中态
            if (!m_moved && event->button() == Qt::LeftButton)
            {
                const int hit = markerAt(event->pos());
                if (hit >= 0 && onStructureClicked)
                {
                    const Marker &m = m_markers[hit];
                    onStructureClicked(event->globalPosition().toPoint(),
                                       m.type, m.x, m.z);
                }
                else
                {
                    toggleSelectionAt(event->pos());
                }
            }
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && valid())
        {
            zoomAt(event->position(), 1.6);
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (!valid())
        {
            event->ignore();
            return;
        }
        // 滚轮缩放：以光标为锚（Shift+滚轮改为垂直平移）
        if (event->modifiers() & Qt::ShiftModifier)
        {
            const int step = (event->angleDelta().y() > 0) ? 60 : -60;
            panBy(QPoint(0, -step));
            event->accept();
            return;
        }
        const double factor = (event->angleDelta().y() > 0) ? 1.25 : 0.8;
        zoomAt(event->position(), factor);
        event->accept();
    }

private:
    static constexpr double kMinScale = 0.02;
    static constexpr double kMaxScale = 64.0;
    /** 单击 / 拖拽的判定阈值（像素） */
    static constexpr int kClickSlop = 4;
    /** 地图上结构图标的边长（像素） */
    static constexpr int kMarkerSize = 30;
    /** 图标白色描边宽度（白色圆角底比图标多出的边） */
    static constexpr int kMarkerStroke = 2;

    void emitZoom()
    {
        if (onZoomChanged)
            onZoomChanged(m_scale);
    }

    void emitViewport()
    {
        if (onViewportChanged)
            onViewportChanged();
    }

    /** region (rx,rz) 在屏幕上的矩形（统一走 overview 的方块坐标系） */
    QRectF regionRect(int rx, int rz) const
    {
        const double ox = double(m_minChunkX) * 16.0;
        const double oz = double(m_minChunkZ) * 16.0;
        const double x0 = (double(rx) * 512.0 - ox) / m_bpp;
        const double z0 = (double(rz) * 512.0 - oz) / m_bpp;
        const double size = 512.0 / m_bpp;
        return QRectF(m_offset.x() + x0 * m_scale,
                      m_offset.y() + z0 * m_scale,
                      size * m_scale, size * m_scale);
    }

    /**
     * 概览整图的缩放缓存（只在 scale<1 时启用）。
     * 没有它的话，平移每帧都要对整图做一次重采样（大世界 10ms+），拖起来必掉帧；
     * 有了它拖动只是 blit 一张位图。scale≥1 走裁剪子区域绘制（1:1 本身很快）。
     */
    void syncOverviewCache()
    {
        if (!valid() || m_scale >= 1.0)
        {
            m_overviewPix = QPixmap();
            m_overviewPixScale = -1.0;
            return;
        }
        if (!m_overviewPix.isNull()
            && std::abs(m_overviewPixScale - m_scale) < 1e-6)
            return;
        const QSize want(qMax(1, int(std::lround(double(m_imageWidth) * m_scale))),
                         qMax(1, int(std::lround(double(m_imageHeight) * m_scale))));
        m_overviewPix = QPixmap::fromImage(*m_image)
                            .scaled(want, Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
        m_overviewPixScale = m_scale;
    }

    void notifySelection()
    {
        if (onSelectionChanged)
            onSelectionChanged(m_hasSelection, m_selChunkX, m_selChunkZ);
    }

    /** 点击位置 → 区块选中切换（点在图外不选，点同一区块取消） */
    void toggleSelectionAt(const QPoint &pos)
    {
        qint64 bx = 0, bz = 0;
        blockAt(pos, &bx, &bz);

        const qint64 minX = qint64(m_minChunkX) * 16;
        const qint64 minZ = qint64(m_minChunkZ) * 16;
        if (bx < minX || bz < minZ
            || bx >= minX + qint64(m_imageWidth) * m_bpp
            || bz >= minZ + qint64(m_imageHeight) * m_bpp)
            return; // 点在地图范围外

        const qint64 cx = bx >> 4;
        const qint64 cz = bz >> 4;
        if (m_hasSelection && m_selChunkX == cx && m_selChunkZ == cz)
        {
            clearSelection();
            return;
        }
        m_hasSelection = true;
        m_selChunkX = cx;
        m_selChunkZ = cz;
        update();
        notifySelection();
    }

    /** 方块坐标 → 画布屏幕坐标 */
    QPointF screenPos(qint64 x, qint64 z) const
    {
        const double ox = double(m_minChunkX) * 16.0;
        const double oz = double(m_minChunkZ) * 16.0;
        return QPointF(m_offset.x() + (double(x) - ox) / m_bpp * m_scale,
                       m_offset.y() + (double(z) - oz) / m_bpp * m_scale);
    }

    /** 屏幕上合并后的一组同类型结构标记（count≥2 时画右上角 ×N 角标） */
    struct MarkerGroup
    {
        QPointF center;     ///< 成员屏幕坐标平均（绘制 / 命中共用）
        int type = -1;      ///< allStructures() 下标
        int count = 1;      ///< 合并进来的标记数
        int firstIdx = 0;   ///< 组内首个标记下标（点击弹窗取它的坐标）
        bool allDone = false; ///< 组内是否全部已标记完成（全完成才半透明）
    };

    /**
     * 把屏幕上挤在一起的同类型标记合并成组：中心距小于一个图标边长即视为
     * "太密集"（两个图标会叠住看不清），合并后只画一个并在右上角标 ×N。
     * 仅同类型合并，不同类型即使挨着也各画各的。
     */
    QVector<MarkerGroup> buildMarkerGroups() const
    {
        QVector<MarkerGroup> groups;
        const int n = m_markers.size();
        if (n == 0)
            return groups;
        groups.reserve(n);
        QVector<bool> used(n, false);
        const double mergeDist = double(kMarkerSize);
        const double mergeDist2 = mergeDist * mergeDist;

        for (int i = 0; i < n; ++i)
        {
            if (used[i] || m_markers[i].type < 0)
                continue;
            used[i] = true;
            MarkerGroup g;
            g.type = m_markers[i].type;
            g.firstIdx = i;
            QVector<QPointF> pts;
            pts.append(screenPos(m_markers[i].x, m_markers[i].z));
            const bool firstDone = m_doneKeys.contains(
                doneKey(m_markers[i].type, m_markers[i].x, m_markers[i].z));
            bool allDone = firstDone;

            // 单链接吸收：与组内任一成员够近的同类型标记并入
            for (int j = i + 1; j < n; ++j)
            {
                if (used[j] || m_markers[j].type != g.type)
                    continue;
                const QPointF cj = screenPos(m_markers[j].x, m_markers[j].z);
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
                allDone = allDone
                    && m_doneKeys.contains(doneKey(g.type, m_markers[j].x,
                                                   m_markers[j].z));
            }

            double sx = 0, sy = 0;
            for (const QPointF &q : pts)
            {
                sx += q.x();
                sy += q.y();
            }
            g.center = QPointF(sx / double(pts.size()),
                               sy / double(pts.size()));
            g.count = pts.size();
            g.allDone = allDone;
            groups.append(g);
        }
        return groups;
    }

    /** 结构标记：画在方块坐标上；缩到方块不可辨时只保留稀有结构 */
    void drawMarkers(QPainter &p)
    {
        if (m_markers.isEmpty() || !valid())
            return;
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        const bool tiny = blockScreenSize() < 0.5; // 方块太小：只画稀有结构
        const auto &defs = StructureFinder::allStructures();
        const int half = kMarkerSize / 2 + kMarkerStroke;
        for (const MarkerGroup &g : buildMarkerGroups())
        {
            if (g.type < 0 || g.type >= defs.size())
                continue;
            const auto &d = defs[g.type];
            if (tiny && !d.rare)
                continue;
            const QPointF &c = g.center;
            if (!QRectF(-40, -40, width() + 80, height() + 80).contains(c))
                continue; // 视口外
            const QPixmap pm = StructureIcons::pixmap(
                g.type, QLatin1String(d.key),
                QString(QChar::fromLatin1(d.displayName[0])),
                QLatin1String(d.wikiTitle), QLatin1String(d.itemTitle),
                QLatin1String(d.envSprite), kMarkerSize);
            const QRect bg(int(c.x()) - half, int(c.y()) - half,
                           half * 2, half * 2);
            p.setOpacity(g.allDone && g.count >= 1 ? 0.35 : 1.0);
            // 白色描边：比图标大一圈的白色圆角底，图标盖在上面
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawRoundedRect(bg, 7, 7);
            p.setBrush(Qt::NoBrush); // 不恢复的话 paintEvent 末尾的 drawRect 会填白整张画布
            p.setPen(Qt::NoPen);
            p.drawPixmap(QPoint(int(c.x()) - pm.width() / 2,
                                int(c.y()) - pm.height() / 2),
                         pm);

            // 密集合并角标：×2 / ×3 … 贴在图标右上角
            if (g.count >= 2)
            {
                QFont f = p.font();
                f.setPixelSize(10);
                f.setBold(true);
                p.setFont(f);
                const QString badge = QStringLiteral("×%1").arg(g.count);
                const QFontMetrics fm(f);
                const int padX = 4;
                const int bw = fm.horizontalAdvance(badge) + padX * 2;
                const int bh = fm.height() + 2;
                const QRect br(bg.right() - bw + 4, bg.top() - bh + 4,
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

    /** 屏幕点命中的结构标记下标（未命中返回 -1；命中半径含白边） */
    int markerAt(const QPoint &pos) const
    {
        if (m_markers.isEmpty() || !valid())
            return -1;
        const double hit = (kMarkerSize / 2.0 + kMarkerStroke) + 3.0;
        for (const MarkerGroup &g : buildMarkerGroups())
        {
            if (std::abs(g.center.x() - pos.x()) <= hit
                && std::abs(g.center.y() - pos.y()) <= hit)
                return g.firstIdx;
        }
        return -1;
    }

    /**
     * 群系叠加层：选中群系的采样格子按代表色半透明铺在地图上
     * （一格一像素的 QImage 最近邻拉伸 —— 边界呈方块状，与地图像素感一致）
     */
    void drawBiomeOverlay(QPainter &p)
    {
        if (!m_bioOverlay || m_bioOverlay->isNull() || !valid())
            return;
        const double ox = double(m_minChunkX) * 16.0;
        const double oz = double(m_minChunkZ) * 16.0;
        const double x0 = (double(m_bioMinX) - ox) / m_bpp;
        const double z0 = (double(m_bioMinZ) - oz) / m_bpp;
        const double sz = double(m_bioCell) / m_bpp;
        const QRectF target(m_offset.x() + x0 * m_scale,
                            m_offset.y() + z0 * m_scale,
                            sz * m_bioOverlay->width() * m_scale,
                            sz * m_bioOverlay->height() * m_scale);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.setOpacity(0.55);
        p.drawImage(target, *m_bioOverlay);
        p.setOpacity(1.0);
    }

    /** 高亮圈：主题色脉冲光环（sin 相位缩放），标记「刚跳转过来的内容」 */
    void drawHighlight(QPainter &p)
    {
        if (!m_hlOn || !valid())
            return;
        const QPointF c = screenPos(m_hlX, m_hlZ);
        if (!QRectF(-80, -80, width() + 160, height() + 160).contains(c))
            return;
        const double t = double(m_hlPhase.elapsed() % 1200) / 1200.0;
        const double pulse = 1.0 + 0.35 * std::sin(t * 2.0 * 3.14159265358979323846);
        const double r = 22.0 * pulse;
        const QColor accent(ThemeManager::instance()->currentThemeColor());
        // 外圈脉冲 + 内圈实线，保证静态截图也能看出高亮
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 70));
        p.drawEllipse(c, r + 8.0, r + 8.0);
        QPen pen(accent, 2.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r, r);
    }

    /** 选中区块高亮：主色半透明填充 + 描边（很小时保证可见的最小尺寸） */
    void drawSelection(QPainter &p, const QRectF &target)
    {
        const double x0 = (double(qint64(m_selChunkX) * 16 - qint64(m_minChunkX) * 16)) / m_bpp;
        const double z0 = (double(qint64(m_selChunkZ) * 16 - qint64(m_minChunkZ) * 16)) / m_bpp;
        const double sz = 16.0 / m_bpp;
        QRectF r(target.left() + x0 * m_scale,
                 target.top() + z0 * m_scale,
                 sz * m_scale, sz * m_scale);

        const QColor accent(ThemeManager::instance()->currentThemeColor());
        if (r.width() < 6.0)
        {
            const double grow = (6.0 - r.width()) * 0.5;
            r.adjust(-grow, -grow, grow, grow);
        }
        p.fillRect(r, QColor(accent.red(), accent.green(), accent.blue(), 46));
        QPen pen(accent, 2.0);
        p.setPen(pen);
        p.drawRect(r.adjusted(-1.5, -1.5, 1.5, 1.5));
    }

    void drawGrid(QPainter &p, const QRectF &target)
    {
        // 一个区块 = 16 方块 = 16 / bpp 图像像素
        const double chunkWidgetPx = m_scale * 16.0 / m_bpp;
        if (chunkWidgetPx < 7.0)
            return;

        const double imgLeft = (double(rect().left()) - target.left()) / m_scale;
        const double imgTop = (double(rect().top()) - target.top()) / m_scale;
        const double imgRight = (double(rect().right()) - target.left()) / m_scale;
        const double imgBottom = (double(rect().bottom()) - target.top()) / m_scale;

        const double chunkImgPx = 16.0 / m_bpp;
        const double regionImgPx = 512.0 / m_bpp;

        const int c0x = int(std::floor(imgLeft / chunkImgPx));
        const int c1x = int(std::ceil(imgRight / chunkImgPx));
        const int c0z = int(std::floor(imgTop / chunkImgPx));
        const int c1z = int(std::ceil(imgBottom / chunkImgPx));

        QPen chunkPen(kChunkGrid);
        chunkPen.setWidthF(1.0);
        p.setPen(chunkPen);
        for (int cx = c0x; cx <= c1x; ++cx)
        {
            const double x = target.left() + cx * chunkImgPx * m_scale;
            p.drawLine(QPointF(x, target.top()), QPointF(x, target.bottom()));
        }
        for (int cz = c0z; cz <= c1z; ++cz)
        {
            const double y = target.top() + cz * chunkImgPx * m_scale;
            p.drawLine(QPointF(target.left(), y), QPointF(target.right(), y));
        }

        if (m_scale * 512.0 / m_bpp < 18.0)
            return;
        QPen regionPen(kRegionGrid);
        regionPen.setWidthF(1.0);
        p.setPen(regionPen);
        const int r0x = int(std::floor(imgLeft / regionImgPx));
        const int r1x = int(std::ceil(imgRight / regionImgPx));
        const int r0z = int(std::floor(imgTop / regionImgPx));
        const int r1z = int(std::ceil(imgBottom / regionImgPx));
        for (int rx = r0x; rx <= r1x; ++rx)
        {
            const double x = target.left() + rx * regionImgPx * m_scale;
            p.drawLine(QPointF(x, target.top()), QPointF(x, target.bottom()));
        }
        for (int rz = r0z; rz <= r1z; ++rz)
        {
            const double y = target.top() + rz * regionImgPx * m_scale;
            p.drawLine(QPointF(target.left(), y), QPointF(target.right(), y));
        }
    }

    /** 画布是否绑定了有效图像 */
    bool valid() const { return m_image != nullptr && m_imageWidth > 0; }

    /** 底层概览：缩放缓存 + 视口裁剪，保证平移不重算整图缩放 */
    void drawOverview(QPainter &p, const QRectF &target)
    {
        if (m_scale < 1.0)
        {
            syncOverviewCache();
            if (!m_overviewPix.isNull())
            {
                p.setRenderHint(QPainter::SmoothPixmapTransform, false);
                p.drawPixmap(target, m_overviewPix,
                             QRectF(0, 0, m_overviewPix.width(),
                                    m_overviewPix.height()));
                return;
            }
        }

        const QRectF vis = QRectF((0.0 - m_offset.x()) / m_scale,
                                  (0.0 - m_offset.y()) / m_scale,
                                  double(width()) / m_scale,
                                  double(height()) / m_scale)
                               .intersected(
                                   QRectF(0, 0, m_imageWidth, m_imageHeight));
        if (vis.isEmpty())
            return;
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 1.0);
        p.drawImage(QRectF(target.left() + vis.left() * m_scale,
                           target.top() + vis.top() * m_scale,
                           vis.width() * m_scale, vis.height() * m_scale),
                    *m_image, vis);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    }

    /** 上层细节：视口相关的 1:1 瓦片（不可见的由 Qt 自动裁掉） */
    void drawTiles(QPainter &p)
    {
        if (m_tiles.isEmpty())
            return;
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it)
            p.drawImage(regionRect(it.key().x(), it.key().y()), it.value());
    }

    const QImage *m_image = nullptr;
    int m_imageWidth = 0;
    int m_imageHeight = 0;
    int m_minChunkX = 0;
    int m_minChunkZ = 0;
    int m_bpp = 1;
    double m_scale = 1.0;
    QPointF m_offset;
    bool m_grid = true;
    bool m_pendingFit = false;
    /** 初始视图横向显示的区块数（打开时不用整图自适应） */
    int m_initialChunks = 64;
    bool m_hasInitialCenter = false;
    QPoint m_initialCenter;
    /** 视口内的 1:1 细节瓦片：region 坐标 → 512×512 */
    QHash<QPoint, QImage> m_tiles;
    /** 概览缩放缓存（scale<1 时避免每帧重采样整图） */
    QPixmap m_overviewPix;
    qreal m_overviewPixScale = -1.0;
    bool m_panning = false;
    bool m_moved = false;      ///< 按下后是否发生了足以算"拖拽"的移动
    QPoint m_pressPos;
    QPoint m_lastPos;
    bool m_hasSelection = false;
    qint64 m_selChunkX = 0;
    qint64 m_selChunkZ = 0;
    /** 结构标记（按种子算出、按视口刷新） */
    QVector<Marker> m_markers;
    /** 已标记完成的结构（key = type_x_z），绘制时半透明 */
    QSet<QString> m_doneKeys;
    /** 群系叠加层（页面维护像素，这里只负责拉伸绘制） */
    const QImage *m_bioOverlay = nullptr;
    qint64 m_bioMinX = 0;
    qint64 m_bioMinZ = 0;
    int m_bioCell = 64;
    /** 高亮圈状态 */
    bool m_hlOn = false;
    qint64 m_hlX = 0;
    qint64 m_hlZ = 0;
    QElapsedTimer m_hlPhase;
    QTimer *m_hlTimer = nullptr;
};

// ═══════════════════════ 页面 ═══════════════════════

WorldMapPage::WorldMapPage(QWidget *parent)
    : QWidget(parent)
    , m_view(nullptr)
    , m_stack(nullptr)
    , m_saveNameLabel(nullptr)
    , m_emptyLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_zoomLabel(nullptr)
    , m_progress(nullptr)
    , m_dimCombo(nullptr)
    , m_shadeCombo(nullptr)
    , m_gridBtn(nullptr)
    , m_zoomInBtn(nullptr)
    , m_zoomOutBtn(nullptr)
    , m_fitBtn(nullptr)
    , m_gotoBtn(nullptr)
    , m_exportBtn(nullptr)
    , m_gotoX(nullptr)
    , m_gotoZ(nullptr)
    , m_watcher(nullptr)
    , m_pollTimer(nullptr)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 10, 16, 14);
    root->setSpacing(10);

    // 应用「地图结构图标」设置（文字旁小图标 / 结构概览）
    StructureIcons::setStyle(
        SettingsManager::instance()->getMapStructureIconStyle()
                == MapStructureIconStyle::Overview
            ? StructureIcons::Style::Overview
            : StructureIcons::Style::Icon);

    // 快捷键需要页面树内有焦点才生效（keyPressEvent 为统一入口）
    setFocusPolicy(Qt::StrongFocus);

    // ── 顶部地图操作区：标题块 │ 渲染组 │ 视图组 │ 定位组 …… 输出动作 ──
    auto *bar = new QWidget(this);
    bar->setObjectName("worldMapToolbar");
    // 鼠标 / 快捷键总览，放在工具栏上一处即可被发现
    bar->setToolTip(
        tr("鼠标：左键拖拽平移 · 左键单击选中区块 · 双击放大 · 中键拖拽平移 · "
           "右键菜单 · 滚轮缩放（Shift+滚轮纵向平移）\n"
           "快捷键：+ / - 缩放 · 0 适应窗口 · 方向键 / WASD 平移（Shift 加速）· "
           "G 网格 · R 刷新 · E 导出 · 1/2/3 切维度 · C 取消选择"));
    auto *barLay = new QHBoxLayout(bar);
    barLay->setContentsMargins(14, 9, 14, 9);
    barLay->setSpacing(10);
    barLay->setAlignment(Qt::AlignVCenter);

    const QColor themeColor = ThemeManager::instance()->currentThemeColor();

    // 组内扁平按钮；右侧输出动作用带框样式以示区别
    auto makeBtn = [&](const QString &text, const QString &tip,
                       const QString &icon = QString(),
                       const char *objName = "worldMapGroupBtn") -> QPushButton * {
        auto *b = new QPushButton(text, bar);
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

    // 竖分隔线（QSS 画 1px 实线，比文字 "|" 可见得多）
    auto makeSep = [&]() -> QFrame * {
        auto *f = new QFrame(bar);
        f->setObjectName("worldMapToolSep");
        f->setFrameShape(QFrame::NoFrame);
        f->setAttribute(Qt::WA_StyledBackground, true);
        f->setFixedWidth(1);
        f->setFixedHeight(28);
        return f;
    };

    // 功能分组容器：淡底圆角，把同类控件收在一起
    auto makeGroup = [&]() -> QWidget * {
        auto *g = new QWidget(bar);
        g->setObjectName("worldMapToolGroup");
        auto *lay = new QHBoxLayout(g);
        lay->setContentsMargins(5, 4, 5, 4);
        lay->setSpacing(4);
        lay->setAlignment(Qt::AlignVCenter);
        return g;
    };

    // —— 标题块：图标 + 存档名 / 副标题 ——
    auto *titleBlock = new QWidget(bar);
    titleBlock->setObjectName("worldMapTitleBlock");
    auto *titleLay = new QHBoxLayout(titleBlock);
    titleLay->setContentsMargins(0, 0, 0, 0);
    titleLay->setSpacing(10);

    m_titleIcon = new QLabel(titleBlock);
    m_titleIcon->setObjectName("worldMapTitleIcon");
    m_titleIcon->setAttribute(Qt::WA_StyledBackground, true);
    m_titleIcon->setFixedSize(34, 34);
    m_titleIcon->setAlignment(Qt::AlignCenter);
    m_titleIcon->setPixmap(IconHelper::loadColoredIcon(
                               QStringLiteral(":/Images/Icons/map.svg"), themeColor, 18)
                               .pixmap(QSize(18, 18)));

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(0);
    titleCol->setContentsMargins(0, 0, 0, 0);

    m_saveNameLabel = new QLabel(tr("地图"), titleBlock);
    m_saveNameLabel->setObjectName("worldMapTitle");
    m_saveNameLabel->setMinimumWidth(40);

    m_subtitleLabel = new QLabel(tr("地表地图"), titleBlock);
    m_subtitleLabel->setObjectName("worldMapSubtitle");

    titleCol->addWidget(m_saveNameLabel);
    titleCol->addWidget(m_subtitleLabel);
    titleLay->addWidget(m_titleIcon);
    titleLay->addLayout(titleCol);
    barLay->addWidget(titleBlock);

    barLay->addWidget(makeSep());

    // —— 渲染组：维度 / 着色 ——
    auto *renderGroup = makeGroup();
    auto *rgLay = qobject_cast<QHBoxLayout *>(renderGroup->layout());

    m_dimCombo = new QComboBox(renderGroup);
    m_dimCombo->setObjectName("worldMapToolCombo");
    m_dimCombo->setFixedHeight(26);
    m_dimCombo->addItem(tr("主世界"), int(WorldMapRenderer::Dimension::Overworld));
    m_dimCombo->addItem(tr("下界"), int(WorldMapRenderer::Dimension::Nether));
    m_dimCombo->addItem(tr("末地"), int(WorldMapRenderer::Dimension::End));
    m_dimCombo->setToolTip(tr("维度：切换要查看的世界，切换后自动重新渲染（1 / 2 / 3）"));
    rgLay->addWidget(m_dimCombo);

    m_shadeCombo = new QComboBox(renderGroup);
    m_shadeCombo->setObjectName("worldMapToolCombo");
    m_shadeCombo->setFixedHeight(26);
    m_shadeCombo->addItem(tr("地表方块"), int(WorldMapRenderer::ShadeMode::BlockColor));
    m_shadeCombo->addItem(tr("高度"), int(WorldMapRenderer::ShadeMode::Height));
    m_shadeCombo->addItem(tr("地形"), int(WorldMapRenderer::ShadeMode::Terrain));
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
        tr("高度：留空或选「全部」= 真实地表；填一个 Y 只显示该高度及以下的地表"
           "（例如填 64 可削掉树冠、屋顶，看清地面）"));
    rgLay->addWidget(m_heightCombo);
    // currentIndexChanged 覆盖"下拉选择"（含程序化切换）；输入文本走 editingFinished
    connect(m_heightCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &WorldMapPage::onHeightChanged);
    connect(m_heightCombo->lineEdit(), &QLineEdit::editingFinished,
            this, &WorldMapPage::onHeightChanged);

    barLay->addWidget(renderGroup);

    // —— 视图组：网格 / 缩放 / 适应窗口 ——
    auto *viewGroup = makeGroup();
    auto *vgLay = qobject_cast<QHBoxLayout *>(viewGroup->layout());

    m_gridBtn = makeBtn(tr("网格"), tr("显示 / 隐藏区块与区域网格（G）"),
                        QStringLiteral(":/Images/Icons/grid.svg"));
    m_gridBtn->setCheckable(true);
    m_gridBtn->setChecked(true);
    vgLay->addWidget(m_gridBtn);

    m_zoomOutBtn = makeBtn(QStringLiteral("−"), tr("缩小（-）"));
    m_zoomOutBtn->setFixedWidth(28);
    vgLay->addWidget(m_zoomOutBtn);

    m_zoomLabel = new QLabel(QStringLiteral("100%"), viewGroup);
    m_zoomLabel->setObjectName("worldMapToolLabel");
    m_zoomLabel->setFixedSize(42, 26);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    vgLay->addWidget(m_zoomLabel);

    m_zoomInBtn = makeBtn(QStringLiteral("+"), tr("放大（+）"));
    m_zoomInBtn->setFixedWidth(28);
    vgLay->addWidget(m_zoomInBtn);

    m_fitBtn = makeBtn(tr("适应"), tr("缩放到刚好容纳整张地图（0）"));
    vgLay->addWidget(m_fitBtn);

    m_oneOneBtn = makeBtn(QStringLiteral("1:1"),
                          tr("缩放到 1 像素 = 1 方块（方块级细节从此可见）"));
    vgLay->addWidget(m_oneOneBtn);

    barLay->addWidget(viewGroup);

    barLay->addStretch(1);

    // —— 右侧输出动作：带框按钮，与组内扁平控件区分 ——
    m_exportBtn = makeBtn(tr("导出"), tr("把当前地图导出为 PNG（E）"),
                          QStringLiteral(":/Images/Icons/export.svg"),
                          "worldMapActionBtn");
    barLay->addWidget(m_exportBtn);

    updateSubtitle();

    root->addWidget(bar);

    // ── 第二行：结构开关（按种子算位置，默认全选） ──
    auto *bar2 = new QWidget(this);
    bar2->setObjectName("worldMapToolbar");
    auto *bar2Lay = new QHBoxLayout(bar2);
    bar2Lay->setContentsMargins(14, 5, 14, 5);
    bar2Lay->setSpacing(6);

    auto *structLabel = new QLabel(tr("结构"), bar2);
    structLabel->setObjectName("worldMapToolLabel");
    bar2Lay->addWidget(structLabel);

    // 种子预测开关放第二行行首：第一行已接近窄屏极限，再加控件会把
    // 窗口最小宽度撑出探针断言的范围；第二行内容超出时本就走横向滚动
    {
        QSettings settings;
        m_seedPreviewOn =
            settings.value(QStringLiteral("worldMap/seedPreview"), true).toBool();
    }
    m_seedBtn = new QPushButton(tr("种子"), bar2);
    m_seedBtn->setObjectName("worldMapSeedBtn");
    m_seedBtn->setCheckable(true);
    m_seedBtn->setChecked(m_seedPreviewOn);
    m_seedBtn->setCursor(Qt::PointingHandCursor);
    m_seedBtn->setToolTip(tr("种子预测：未生成的区域按世界种子渲染近似地形"
                             "（仅供预览，实际地形以游戏为准）"));
    bar2Lay->addWidget(m_seedBtn);
    connect(m_seedBtn, &QPushButton::toggled,
            this, &WorldMapPage::onSeedToggled);

    // 按钮放进横向滚动容器：26+ 个按钮的行最小宽度不能把窗口撑大，
    // 窄屏时横向滚动（滚轮经 eventFilter 转横滚）
    m_structScroll = new QScrollArea(bar2);
    m_structScroll->setObjectName(QStringLiteral("worldMapStructScroll"));
    m_structScroll->setWidgetResizable(true);
    m_structScroll->setFrameShape(QFrame::NoFrame);
    m_structScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_structScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_structScroll->setFocusPolicy(Qt::NoFocus);
    // widgetResizable 下 QScrollArea 会把内容最小宽传给布局 —— Ignored 解除，
    // 窄屏才压得下去（内容靠横向滚动可达）
    m_structScroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *structInner = new QWidget(m_structScroll);
    auto *structLay = new QHBoxLayout(structInner);
    structLay->setContentsMargins(0, 0, 0, 0);
    structLay->setSpacing(6);
    m_structScroll->setWidget(structInner);
    m_structScroll->viewport()->installEventFilter(this);
    bar2Lay->addWidget(m_structScroll, 1);

    const auto &defs = StructureFinder::allStructures();
    m_structEnabled = QVector<bool>(defs.size(), true);
    // 群系默认全不选（叠加层默认关闭，需要时在弹窗里勾选 / 全选）
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
            // 生成结构表内、但当前无位置算法的条目：展示但禁用
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

        const int idx = i;
        connect(b, &QPushButton::toggled, this, [this, idx](bool on) {
            if (idx < m_structEnabled.size())
                m_structEnabled[idx] = on;
            // 弹窗打开时同步对应行的勾选（阻断信号避免回环触发重算）
            if (m_contentPopup && m_contentPopup->isVisible() && m_structList)
            {
                for (int r = 0; r < m_structList->count(); ++r)
                {
                    auto *item = m_structList->item(r);
                    if (item->data(Qt::UserRole).toInt() != idx)
                        continue;
                    auto *w = m_structList->itemWidget(item);
                    if (!w)
                        continue;
                    auto *btn = w->findChild<QPushButton *>(
                        QStringLiteral("worldMapPopupToggle"));
                    if (btn)
                    {
                        const QSignalBlocker blocker(btn);
                        btn->setChecked(on);
                    }
                    break;
                }
            }
            startStructRefresh();
        });
    }

    structLay->addStretch(1); // 宽屏时按钮靠左，滚动容器吸收富余宽度

    auto *selectAll = new QPushButton(tr("全选"), bar2);
    selectAll->setObjectName("worldMapGroupBtn");
    selectAll->setCursor(Qt::PointingHandCursor);
    selectAll->setToolTip(tr("显示全部结构标记"));
    connect(selectAll, &QPushButton::clicked, this, [this]() {
        setAllStructures(true);
    });
    bar2Lay->addWidget(selectAll);

    auto *clearAll = new QPushButton(tr("清空"), bar2);
    clearAll->setObjectName("worldMapGroupBtn");
    clearAll->setCursor(Qt::PointingHandCursor);
    clearAll->setToolTip(tr("隐藏全部结构标记"));
    connect(clearAll, &QPushButton::clicked, this, [this]() {
        setAllStructures(false);
    });
    bar2Lay->addWidget(clearAll);

    // —— 右侧「展开」：打开内容浏览弹窗（搜索 + 结构/群系列表 + 高亮跳转） ——
    m_expandBtn = new QPushButton(tr("展开"), bar2);
    m_expandBtn->setObjectName(QStringLiteral("worldMapExpandBtn"));
    m_expandBtn->setCheckable(true);
    m_expandBtn->setCursor(Qt::PointingHandCursor);
    m_expandBtn->setToolTip(tr("展开内容列表：按名称搜索结构与群系，"
                               "勾选决定是否在地图上显示，右侧按钮可高亮并跳转到最近的一处"));
    m_expandBtn->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/expand.svg"), themeColor, 14));
    m_expandBtn->setIconSize(QSize(14, 14));
    bar2Lay->addWidget(m_expandBtn);
    connect(m_expandBtn, &QPushButton::toggled,
            this, &WorldMapPage::onExpandToggled);

    root->addWidget(bar2);

    // ── 画布 ──
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("worldMapStack");

    auto *emptyPage = new QWidget(m_stack);
    emptyPage->setObjectName("worldMapEmptyPage");
    emptyPage->setAttribute(Qt::WA_StyledBackground, true);
    auto *emptyLay = new QVBoxLayout(emptyPage);
    emptyLay->addStretch(1);
    m_emptyLabel = new QLabel(emptyPage);
    m_emptyLabel->setObjectName("worldMapEmpty");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setText(tr("该存档还没有可渲染的地表地图"));
    emptyLay->addWidget(m_emptyLabel);
    emptyLay->addStretch(1);
    m_stack->addWidget(emptyPage);

    m_view = new WorldMapView(m_stack);
    m_view->setObjectName(QStringLiteral("worldMapView"));
    m_view->setInitialVisibleChunks(kInitialVisibleChunks);
    m_stack->addWidget(m_view);
    root->addWidget(m_stack, 1);

    // ── 底部状态区 ──
    auto *statusBar = new QWidget(this);
    statusBar->setObjectName("worldMapStatusBar");
    auto *statusLay = new QHBoxLayout(statusBar);
    statusLay->setContentsMargins(14, 6, 14, 6);
    statusLay->setSpacing(12);

    m_statusLabel = new QLabel(statusBar);
    m_statusLabel->setObjectName("worldMapStatus");
    m_statusLabel->setText(tr("尚未加载地图"));
    statusLay->addWidget(m_statusLabel, 1);

    // 底部「当前群系」：中文名来自当前实例 lang（后台加载），采样视口中心
    m_biomeLabel = new QLabel(statusBar);
    m_biomeLabel->setObjectName("worldMapBiome");
    m_biomeLabel->setText(tr("当前群系：—"));
    statusLay->addWidget(m_biomeLabel);

    m_progress = new QProgressBar(statusBar);
    m_progress->setObjectName("worldMapProgress");
    m_progress->setFixedWidth(220);
    m_progress->setFixedHeight(8);
    m_progress->setTextVisible(false);
    m_progress->hide();
    statusLay->addWidget(m_progress);

    root->addWidget(statusBar);

    // ── 接线 ──
    m_watcher = new QFutureWatcher<WorldMapRenderer::RenderResult>(this);
    connect(m_watcher, &QFutureWatcherBase::finished,
            this, &WorldMapPage::onRenderFinished);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(80);
    connect(m_pollTimer, &QTimer::timeout, this, &WorldMapPage::onPollProgress);

    // 专用线程池：概览跑 1 个外层任务（内部再按区域并行）；
    // 细节瓦片最多同时 3 路，共用此池
    m_renderPool.setMaxThreadCount(kMaxConcurrentTiles);
    // 结构查询独立成池：算标记时不会被大世界渲染堵住
    m_structPool.setMaxThreadCount(1);

    // 远程图标就位后刷新第二行按钮；页面析构前必须解绑（否则回调踩悬空指针）
    StructureIcons::setUpdatedHandler([this]() {
        refreshStructIcons();
        if (m_view)
            m_view->update();
    });

    // 1:1 细节瓦片：视口去抖调度；渲染结果由各自 watcher 回传（见 onTileFinished）
    m_tileTimer = new QTimer(this);
    m_tileTimer->setSingleShot(true);
    m_tileTimer->setInterval(150);
    connect(m_tileTimer, &QTimer::timeout, this, &WorldMapPage::scheduleTiles);

    // 视野补洞心跳：即使 continueTiling 链路断了（在途计数卡住、
    // 去抖被盖掉等），也会周期性按当前可见范围把缺的 region 补上，
    // 保证"中心铺开后，视野左右/上下空隙最终都会渲到"
    m_tileFillTimer = new QTimer(this);
    m_tileFillTimer->setInterval(400);
    connect(m_tileFillTimer, &QTimer::timeout, this, [this]() {
        if (m_busy || !m_view || !m_view->hasImage())
            return;
        if (m_view->blockScreenSize() < kDetailMinBlockPx)
            return;
        scheduleTiles();
    });

    // 种子画布重定心去抖：视角离开当前窗口后停手片刻再以新中心重渲
    m_seedRecenterTimer = new QTimer(this);
    m_seedRecenterTimer->setSingleShot(true);
    m_seedRecenterTimer->setInterval(600);
    connect(m_seedRecenterTimer, &QTimer::timeout,
            this, &WorldMapPage::onSeedRecenterTimeout);

    connect(m_dimCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &WorldMapPage::onDimensionChanged);
    connect(m_shadeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &WorldMapPage::onShadeChanged);
    connect(m_gridBtn, &QPushButton::toggled, this, &WorldMapPage::onGridToggled);
    connect(m_zoomInBtn, &QPushButton::clicked, this, &WorldMapPage::onZoomIn);
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &WorldMapPage::onZoomOut);
    connect(m_fitBtn, &QPushButton::clicked, this, &WorldMapPage::onFitClicked);
    connect(m_oneOneBtn, &QPushButton::clicked, this, &WorldMapPage::onZoomToOneOne);
    connect(m_exportBtn, &QPushButton::clicked, this, &WorldMapPage::onExportClicked);

    m_view->onZoomChanged = [this](double) { updateZoomLabel(); };
    m_view->onCursorMoved = [this](qint64 bx, qint64 bz) {
        updateStatusLabel(tr("指针 X %1  Z %2").arg(bx).arg(bz));
    };
    m_view->onSelectionChanged = [this](bool has, qint64 cx, qint64 cz) {
        m_selectionText = has ? tr("已选中区块 (%1, %2)").arg(cx).arg(cz)
                              : QString();
        updateStatusLabel();
    };
    m_view->onContextMenu = [this](const QPoint &globalPos, qint64 bx, qint64 bz) {
        showMapContextMenu(globalPos, bx, bz);
    };
    m_view->onStructureClicked =
        [this](const QPoint &globalPos, int type, qint64 x, qint64 z) {
            showStructurePopup(globalPos, type, x, z);
        };
    // 视口一变就去抖调度细节瓦片（概览渲染期间由 m_busy 挡住，完成后统一补）
    m_view->onViewportChanged = [this]() {
        if (!m_tileTimer)
            return;
        if (!m_busy)
            m_tileTimer->start();   // 细节瓦片：等概览渲染完再排
        maybeScheduleSeedRecenter(); // 种子画布：视角离开窗口 → 去抖重定心
        startStructRefresh();       // 结构标记与渲染进度无关，立即重算（内部去重）
        scheduleBiomeRefresh();     // 底部「当前群系」跟随视角中心
    };

    setBusy(false);
    updateZoomLabel();
}

WorldMapPage::~WorldMapPage()
{
    // 先解绑全局图标回调：否则 Wiki 图标下载完成后会回调到已销毁的页面
    StructureIcons::setUpdatedHandler(nullptr);
    // 内容弹窗创建过的话挂过应用级事件过滤（点弹窗外关闭），析构前摘掉
    if (m_contentPopup)
        QApplication::instance()->removeEventFilter(this);
    // 再撤信号与取消标志，最后由 m_renderPool 析构等在途任务收尾
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_job)
        m_job->cancel = true;
    if (m_sink)
        m_sink->cancel = true;
}

void WorldMapPage::setCurrentSave(const QString &saveFolderPath, const QString &saveName)
{
    const bool changed = (m_savePath != saveFolderPath);
    m_savePath = saveFolderPath;
    m_saveName = saveName;

    QFontMetrics fm(m_saveNameLabel->font());
    m_saveNameLabel->setText(fm.elidedText(saveName, Qt::ElideRight, 200));
    updateSubtitle();

    // 读出生点：初始视图中心 + 渐进渲染的优先区域（从这里往四周长）
    const auto saveInfo = LevelDatEditor::readSave(saveFolderPath);
    m_hasSpawn = saveInfo.has_value() && saveInfo->hasSpawn;
    m_spawnX = m_hasSpawn ? saveInfo->spawnX : 0;
    m_spawnZ = m_hasSpawn ? saveInfo->spawnZ : 0;
    m_view->setInitialCenter(m_spawnX, m_spawnZ, m_hasSpawn);

    // 种子与版本 → 结构位置预测 + 未加载地形的种子预测（cubiomes）
    m_hasSeed = saveInfo.has_value() && saveInfo->hasSeed;
    m_seed = m_hasSeed ? quint64(saveInfo->seed) : 0;
    m_mcVersion = StructureFinder::versionToInt(
        saveInfo.has_value() ? saveInfo->versionName : QString());
    if (m_seedBtn)
    {
        // 读不出种子或版本的存档没法预测，开关禁用并强制关闭
        const bool usable = m_hasSeed && m_mcVersion != 0;
        m_seedBtn->setEnabled(usable);
        const QSignalBlocker blocker(m_seedBtn);
        m_seedBtn->setChecked(usable && m_seedPreviewOn);
    }
    loadStructDone(); // 上次存档的完成标记（弹窗半透明显示用）
    // 换存档：丢掉上一个存档的群系采样 / 叠加层 / 高亮圈
    m_biomeGrid = BiomeCatalog::SampleGrid();
    m_biomeOverlay = QImage();
    if (m_view)
    {
        m_view->setBiomeOverlay(nullptr, 0, 0, 64);
        m_view->setHighlight(0, 0, false);
    }
    startStructRefresh();
    // 画布中心（出生点 / 恢复点）确定后即可报一次当前群系
    scheduleBiomeRefresh();

    if (changed)
    {
        if (m_job)
            m_job->cancel = true;
        m_cache.clear();
        m_cacheOrder.clear();
        m_priorLiveValid = false;
        m_priorLiveImage = QImage();
        m_view->clearImage();
        clearSelection();       // 换存档时丢掉上一个存档的选中区块
        m_cursorText.clear();
        m_selectionText.clear();
        m_statSuffix.clear();
        m_stack->setCurrentIndex(0);
        m_emptyLabel->setText(tr("正在解析 %1 的地表数据…").arg(saveName));
        m_statusLabel->setText(tr("准备渲染…"));
    }
    startRender();
    // 进入页面即接快捷键（要求焦点落在本页子树内）
    setFocus(Qt::OtherFocusReason);
}

void WorldMapPage::reset()
{
    if (m_job)
        m_job->cancel = true;
    m_priorLiveValid = false;
    m_priorLiveImage = QImage();
    m_savePath.clear();
    m_saveName.clear();
    m_cache.clear();
    m_cacheOrder.clear();
    m_statSuffix.clear();
    clearSelection();
    m_cursorText.clear();
    m_selectionText.clear();
    clearTileState(true);
    m_renderKey = -1;
    if (m_view)
    {
        m_view->clearImage();
        m_view->setInitialCenter(0, 0, false);
    }
    if (m_stack)
        m_stack->setCurrentIndex(0);
    if (m_emptyLabel)
        m_emptyLabel->setText(tr("该存档还没有可渲染的地表地图"));
    if (m_saveNameLabel)
        m_saveNameLabel->setText(tr("未选择存档"));
    if (m_subtitleLabel)
        m_subtitleLabel->setText(tr("地表地图"));
    if (m_statusLabel)
        m_statusLabel->setText(tr("尚未加载地图"));
    setBusy(false);
}

void WorldMapPage::refresh()
{
    if (m_savePath.isEmpty())
        return;
    m_cache.remove(renderCacheKey());
    m_cacheOrder.removeAll(renderCacheKey());
    startRender();
}

WorldMapRenderer::SeedPreview WorldMapPage::buildSeedPreview() const
{
    WorldMapRenderer::SeedPreview sp;
    sp.enabled = m_seedPreviewOn && m_hasSeed && m_mcVersion != 0
        && m_seedBtn && m_seedBtn->isChecked();
    sp.seed = m_seed;
    sp.mc = m_mcVersion;
    sp.padRegions = 3;
    sp.radiusRegions = m_seedCanvasRadius;
    // 中心 = 当前视角中心的 region（还没画布时退回出生点）——
    // 画布跟着人走，视角中心向四周约 radius×512 格都有预测地形
    sp.hasCenter = true;
    if (m_view && m_view->hasImage())
    {
        const QPoint c = m_view->centerRegion();
        sp.centerRegionX = c.x();
        sp.centerRegionZ = c.y();
    }
    else
    {
        sp.centerRegionX = int(std::floor(double(m_spawnX) / 512.0));
        sp.centerRegionZ = int(std::floor(double(m_spawnZ) / 512.0));
    }
    return sp;
}

void WorldMapPage::maybeScheduleSeedRecenter()
{
    if (!m_seedRecenterTimer || m_busy || !m_view || !m_view->hasImage()
        || !m_seedCanvasCenterSet)
        return;
    if (!(m_seedPreviewOn && m_hasSeed && m_mcVersion != 0
          && m_seedBtn && m_seedBtn->isChecked()))
        return;
    // 视角中心离开画布窗口（留 2 栈余量，避免贴边就重渲）→ 去抖后重定心
    const QPoint c = m_view->centerRegion();
    const int dist = std::max(std::abs(c.x() - m_seedCanvasCenter.x()),
                              std::abs(c.y() - m_seedCanvasCenter.y()));
    if (dist > m_seedCanvasRadius - 2)
        m_seedRecenterTimer->start();
}

void WorldMapPage::onSeedRecenterTimeout()
{
    if (!m_view || !m_view->hasImage() || m_busy)
        return;
    // 去抖期间视角可能又回窗口内了，再确认一次
    if (m_seedCanvasCenterSet)
    {
        const QPoint c = m_view->centerRegion();
        const int dist = std::max(std::abs(c.x() - m_seedCanvasCenter.x()),
                                  std::abs(c.y() - m_seedCanvasCenter.y()));
        if (dist <= m_seedCanvasRadius - 2)
            return;
    }
    // 记住当前视角中心，画布重建（几何变化会重置初始视图）后恢复
    const QPoint centerBlock = m_view->centerBlock();
    m_pendingRestoreBlock = centerBlock;
    m_pendingRestoreBlockSet = true;
    // 留下当前 live 画布：重定心时重叠 region 直接搬过去，不闪白、不重算
    if (!m_liveImage.isNull())
    {
        m_priorLiveImage = m_liveImage; // 隐式共享，不拷贝像素
        m_priorMinChunkX = m_liveMinChunkX;
        m_priorMinChunkZ = m_liveMinChunkZ;
        m_priorBpp = m_liveBpp;
        m_priorLiveValid = true;
    }
    startRender(); // 新画布以视角中心为圆心；完成后 applyResult 恢复视口
}

int WorldMapPage::renderCacheKey() const
{
    // key 里编入高度限制与种子预测开关：不同 Y / 不同开关是不同的渲染结果；
    // 种子开启时还要编入画布中心（同一设置在不同视角中心是不同画布）
    const int dim = m_dimCombo ? m_dimCombo->currentIndex() : 0;
    const int shade = m_shadeCombo ? m_shadeCombo->currentIndex() : 0;
    const WorldMapRenderer::SeedPreview sp = buildSeedPreview();
    int key = ((dim * 3 + shade) * 2 + (sp.enabled ? 1 : 0)) * 1000000
        + (m_maxSurfaceY + 500000);
    if (sp.enabled)
        key = key * 31 + qHash(QPoint(sp.centerRegionX, sp.centerRegionZ));
    return key;
}

void WorldMapPage::startRender()
{
    if (m_savePath.isEmpty())
    {
        m_stack->setCurrentIndex(0);
        m_emptyLabel->setText(tr("该存档还没有可渲染的地表地图"));
        return;
    }

    const WorldMapRenderer::SeedPreview seedPrev = buildSeedPreview();
    const int key = renderCacheKey();
    m_renderKey = key;
    // 记下这张画布的种子中心，视角离开窗口时据此触发重定心
    m_seedCanvasCenterSet = seedPrev.enabled;
    if (seedPrev.enabled)
        m_seedCanvasCenter = QPoint(seedPrev.centerRegionX, seedPrev.centerRegionZ);

    // 取消上一次渲染（job + sink 双通道），旧任务迟到的结果会被序号检查丢弃
    if (m_job)
        m_job->cancel = true;
    if (m_sink)
        m_sink->cancel = true;
    m_sink.reset();
    m_layoutApplied = false;
    // 旧概览还在跑时，细节瓦片调度停住；已发出的在途瓦片一并作废，
    // 免得它们占着并发位，新画布就绪后左右两侧补不上。
    // 种子重定心且 bpp 未变：1:1 瓦片按 region 键仍有效，只作废在途、不拆画布
    if (m_priorLiveValid && m_priorBpp > 0 && m_priorBpp == m_liveBpp
        && !m_priorLiveImage.isNull())
    {
        ++m_tileSeq;
        for (QFutureWatcher<TileResult> *w : m_tileWatchers)
        {
            if (!w)
                continue;
            w->disconnect(this);
            w->deleteLater();
        }
        m_tileWatchers.clear();
        m_tileInFlightKeys.clear();
        m_tileInFlight = 0;
        m_tilesPending.clear();
    }
    else
    {
        clearTileState(true);
    }

    WorldMapRenderer::RenderResult cached;
    if (takeCached(key, &cached))
    {
        m_priorLiveValid = false;
        m_priorLiveImage = QImage();
        applyResult(cached);
        return;
    }

    auto job = std::make_shared<RenderJob>();
    m_job = job;
    auto sink = std::make_shared<WorldMapRenderer::ProgressiveSink>();
    m_sink = sink;
    // 概览的渐进渲染也从出生点所在 region 起步，往四周一圈圈长
    if (m_hasSpawn)
    {
        sink->hasPriorityRegion = true;
        sink->priorityRegion = QPoint(
            int(std::floor(double(m_spawnX) / 512.0)),
            int(std::floor(double(m_spawnZ) / 512.0)));
    }
    const int seq = ++m_renderSeq;

    const QString path = m_savePath;
    const auto dim = static_cast<WorldMapRenderer::Dimension>(m_dimCombo->currentIndex());
    const auto mode = static_cast<WorldMapRenderer::ShadeMode>(m_shadeCombo->currentIndex());

    setBusy(true);
    m_progress->setRange(0, 0);
    m_stack->setCurrentIndex(m_view->hasImage() ? 1 : 0);
    if (!m_view->hasImage())
        m_emptyLabel->setText(tr("正在解析地表数据…"));
    m_statusLabel->setText(tr("正在渲染…"));

    const int maxSurfaceY = m_maxSurfaceY;

    // 种子重定心：把上一张 live 画布交给渲染器，重叠 region 原样搬过去。
    // 这里只拷一份给 worker，prior 本体留给 applyLiveLayout 贴到新画布并判断
    // 是否保留 1:1 瓦片（applyLiveFromPrior 消费）。
    WorldMapRenderer::PriorCanvas prior;
    if (m_priorLiveValid && !m_priorLiveImage.isNull())
    {
        prior.image = m_priorLiveImage;
        prior.minChunkX = m_priorMinChunkX;
        prior.minChunkZ = m_priorMinChunkZ;
        prior.blockPerPixel = m_priorBpp;
    }

    // 区域之间并行解码；每完成一个就通过 sink 推给 UI 贴图，实现边渲染边显示
    auto future = QtConcurrent::run(&m_renderPool,
        [path, dim, mode, job, sink, seq, maxSurfaceY, seedPrev, prior]() {
            WorldMapRenderer::RenderResult result = WorldMapRenderer::render(
                path, dim, mode,
                [job]() { return job->cancel.load(); },
                [job](int done, int total) {
                    job->done.store(done);
                    job->total.store(total);
                },
                sink, 0, maxSurfaceY, seedPrev, prior);
            result.key = seq;
            return result;
        });
    m_watcher->setFuture(future);
    m_pollTimer->start();
}

void WorldMapPage::onRenderFinished()
{
    if (!m_watcher || !m_watcher->isFinished())
        return;

    const WorldMapRenderer::RenderResult result = m_watcher->result();
    // 过期任务（已被后来的渲染顶掉）：不动任何 UI 状态
    if (result.key != m_renderSeq)
        return;

    m_pollTimer->stop();
    if (m_job)
        m_job->cancel = true;
    if (m_sink)
        m_sink->cancel = true;
    setBusy(false);

    applyResult(result);
}

void WorldMapPage::onPollProgress()
{
    // 后台任务结果先收（与渲染进度无关）
    drainStructResult();
    drainLocateResult();
    drainBiomeResult();
    if (m_biomeNameJob && m_biomeNameJob->done.load(std::memory_order_acquire))
    {
        const auto job = m_biomeNameJob;
        m_biomeNameJob.reset();
        m_biomeNames = job->names;
    }

    if (!m_busy || !m_sink)
    {
        if (!m_structJob && !m_locateJob && !m_biomeJob && !m_biomeNameJob
            && m_pollTimer)
            m_pollTimer->stop(); // 渲染与后台任务都收完了：停表
        return;
    }

    // 先建画布、再贴新到的图块，最后刷新进度 —— 用户看到地图逐区域长出来
    applyLiveLayout();
    drainPendingTiles();

    const int total = m_sink->total.load();
    if (total > 0)
    {
        if (m_progress->maximum() != total)
            m_progress->setRange(0, total);
        m_progress->setValue(m_sink->done.load());
    }
    else if (m_job && m_job->total.load() > 0)
    {
        const int jobTotal = m_job->total.load();
        if (m_progress->maximum() != jobTotal)
            m_progress->setRange(0, jobTotal);
        m_progress->setValue(m_job->done.load());
    }
}

/** 结构查询（后台线程算，结果由 onPollProgress 轮询收回 UI，无悬空风险） */
void WorldMapPage::startStructRefresh()
{
    if (!m_view)
        return;
    if (!m_hasSeed || m_mcVersion == 0)
    {
        m_view->setMarkers({});
        return;
    }
    if (m_structJob)
    {
        m_structDirty = true; // 正在算：这轮完成后按最新状态再来一轮
        return;
    }

    const QRect rr = m_view->visibleRegionRange();
    if (rr.isNull() || rr.width() <= 0 || rr.height() <= 0)
        return;

    // 覆盖范围 = 可见 region 外扩一圈，再按上限封顶（缩到全图时网格数不会爆炸）
    qint64 minX = qint64(rr.left()) * 512 - 512;
    qint64 maxX = (qint64(rr.right()) + 1) * 512;
    qint64 minZ = qint64(rr.top()) * 512 - 512;
    qint64 maxZ = (qint64(rr.bottom()) + 1) * 512;
    const qint64 cx = (minX + maxX) / 2;
    const qint64 cz = (minZ + maxZ) / 2;
    constexpr qint64 kMaxSpan = 16384;
    if (maxX - minX > kMaxSpan)
    {
        minX = cx - kMaxSpan / 2;
        maxX = cx + kMaxSpan / 2;
    }
    if (maxZ - minZ > kMaxSpan)
    {
        minZ = cz - kMaxSpan / 2;
        maxZ = cz + kMaxSpan / 2;
    }

    const auto dim = static_cast<WorldMapRenderer::Dimension>(
        m_dimCombo ? m_dimCombo->currentIndex() : 0);
    const int cdim = (dim == WorldMapRenderer::Dimension::Nether) ? -1
                   : (dim == WorldMapRenderer::Dimension::End)    ? 1
                                                                  : 0;

    auto job = std::make_shared<StructJob>();
    m_structJob = job;
    const quint64 seed = m_seed;
    const int mc = m_mcVersion;
    const QVector<bool> enabled = m_structEnabled;
    // 群系叠加：有任一勾选才采样（默认不选；全清空时跳过省算力）
    const bool anyBiome = std::any_of(m_biomeEnabled.cbegin(), m_biomeEnabled.cend(),
                                      [](bool b) { return b; });

    // 独立线程池：结构计算与地图渲染各跑各的，谁也不堵谁
    QtConcurrent::run(&m_structPool, [seed, mc, cdim, minX, minZ, maxX, maxZ,
                                      enabled, anyBiome, job]() {
        const auto hits = StructureFinder::find(seed, mc, cdim,
                                                minX, minZ, maxX, maxZ, enabled);
        job->coords.reserve(hits.size());
        job->types.reserve(hits.size());
        for (const auto &h : hits)
        {
            job->coords.append(qMakePair(h.x, h.z));
            job->types.append(h.structIdx);
        }
        if (anyBiome)
        {
            job->biomeGrid = BiomeCatalog::sampleGrid(seed, mc, cdim,
                                                      minX, minZ, maxX, maxZ);
        }
        job->done.store(true, std::memory_order_release);
    });

    if (m_pollTimer && !m_pollTimer->isActive())
        m_pollTimer->start();
}

void WorldMapPage::drainStructResult()
{
    if (!m_structJob)
        return;
    if (!m_structJob->done.load(std::memory_order_acquire))
        return;

    const auto job = m_structJob;
    m_structJob.reset();
    applyStructHits(job->coords, job->types);

    // 群系采样网格随结构查询一起回来：保存并按当前勾选重建叠加层
    m_biomeGrid = job->biomeGrid;
    rebuildBiomeOverlay();

    if (m_structDirty)
    {
        m_structDirty = false;
        startStructRefresh(); // 拿最新状态（开关 / 视口）再算一轮
    }
}

void WorldMapPage::applyStructHits(const QVector<QPair<int, int>> &coords,
                                   const QVector<int> &types)
{
    if (!m_view)
        return;
    QVector<WorldMapView::Marker> markers;
    markers.reserve(coords.size());
    for (int i = 0; i < coords.size() && i < types.size(); ++i)
    {
        WorldMapView::Marker m;
        m.x = coords[i].first;
        m.z = coords[i].second;
        m.type = types[i];
        markers.append(m);
    }
    m_view->setMarkers(markers);
}

void WorldMapPage::refreshStructIcons()
{
    const auto &defs = StructureFinder::allStructures();
    for (int i = 0; i < m_structBtns.size() && i < defs.size(); ++i)
    {
        const auto &d = defs[i];
        m_structBtns[i]->setIcon(StructureIcons::pixmap(
            i, QLatin1String(d.key),
            QString(QChar::fromLatin1(d.displayName[0])),
            QLatin1String(d.wikiTitle), QLatin1String(d.itemTitle),
            QLatin1String(d.envSprite), 18));
    }
    // 弹窗打开时，两列列表的图标也跟着远程下载完成刷新
    if (m_contentPopup && m_contentPopup->isVisible())
        rebuildPopupLists();
}

/** 渲染线程发布布局后，在 UI 线程建画布并切到地图视图 */
void WorldMapPage::applyLiveLayout()
{
    if (m_layoutApplied || !m_sink)
        return;
    if (!m_sink->layoutReady.load(std::memory_order_acquire))
        return;

    const int w = m_sink->imageWidth;
    const int h = m_sink->imageHeight;
    const int minX = m_sink->minChunkX;
    const int minZ = m_sink->minChunkZ;
    const int bpp = m_sink->blockPerPixel;
    if (w <= 0 || h <= 0)
        return;

    const QString key = QStringLiteral("%1x%2:%3:%4:%5")
                            .arg(w).arg(h).arg(minX).arg(minZ).arg(bpp);
    const bool sameCanvas = (m_liveKey == key && !m_liveImage.isNull());
    // 种子重定心：prior bpp 与新画布一致 → 搬重叠像素，并保留 1:1 瓦片
    const bool seedReuse = m_priorLiveValid && m_priorBpp == bpp
        && !m_priorLiveImage.isNull();
    if (!sameCanvas)
    {
        m_liveImage = QImage(w, h, QImage::Format_ARGB32);
        m_liveImage.fill(qRgba(0, 0, 0, 0));
        // 种子重定心：先把上一张画布的重叠 region 搬进来，不闪白
        primeLiveFromPrior();
        m_liveKey = key;
    }
    m_liveMinChunkX = minX;
    m_liveMinChunkZ = minZ;
    m_liveBpp = bpp;

    // 先切画布再绑定，布局 pass 才会把真实几何交给视图
    m_stack->setCurrentIndex(1);
    // 同布局的重新渲染（如切换着色方式）保留旧画面，新区域逐块覆盖
    m_view->setImageRef(&m_liveImage, minX, minZ, bpp, !sameCanvas);
    if (m_pendingRestoreBlockSet)
    {
        // 种子画布重定心：渐进建画布时几何变化会重置视口，立即恢复视角中心，
        // 避免渲染过程中视口先跳回出生点再跳回来
        m_view->centerOnBlock(m_pendingRestoreBlock.x(),
                              m_pendingRestoreBlock.y());
    }
    m_layoutApplied = true;
    // 画布换了/着色变了：上一批 1:1 瓦片全部作废；
    // 种子重定心且 bpp 未变时瓦片键仍有效，保留以免放大后又从零加载
    if (!sameCanvas && !seedReuse)
        clearTileState(true);
}

/** 把渲染线程推来的区域图块贴到画布（写入只发生在 UI 线程） */
void WorldMapPage::drainPendingTiles()
{
    if (!m_sink || !m_layoutApplied)
        return;

    QList<QPair<QPoint, QImage>> batch;
    {
        QMutexLocker locker(&m_sink->tileMutex);
        if (m_sink->pendingTiles.isEmpty())
            return;
        batch.swap(m_sink->pendingTiles);
    }

    QPainter painter(&m_liveImage);
    for (const auto &item : batch)
        painter.drawImage(item.first, item.second);
    painter.end();

    m_view->refreshImage();
}

void WorldMapPage::primeLiveFromPrior()
{
    if (!m_priorLiveValid || m_priorLiveImage.isNull() || m_liveImage.isNull())
        return;
    if (m_priorBpp <= 0 || m_priorBpp != m_liveBpp)
    {
        m_priorLiveValid = false;
        m_priorLiveImage = QImage();
        return;
    }
    const int tilePx = 512 / m_liveBpp;
    const int priorMinRx = m_priorMinChunkX / 32;
    const int priorMinRz = m_priorMinChunkZ / 32;
    const int liveMinRx = m_liveMinChunkX / 32;
    const int liveMinRz = m_liveMinChunkZ / 32;
    const int priorW = m_priorLiveImage.width();
    const int priorH = m_priorLiveImage.height();
    QPainter p(&m_liveImage);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    for (int rz = liveMinRz; rz * tilePx < m_liveImage.height(); ++rz)
    {
        for (int rx = liveMinRx; rx * tilePx < m_liveImage.width(); ++rx)
        {
            const int srcX = (rx - priorMinRx) * tilePx;
            const int srcY = (rz - priorMinRz) * tilePx;
            if (srcX < 0 || srcY < 0
                || srcX + tilePx > priorW || srcY + tilePx > priorH)
                continue;
            p.drawImage(QPoint((rx - liveMinRx) * tilePx,
                               (rz - liveMinRz) * tilePx),
                        m_priorLiveImage,
                        QRect(srcX, srcY, tilePx, tilePx));
        }
    }
    p.end();
    m_priorLiveValid = false;
    m_priorLiveImage = QImage();
}

void WorldMapPage::applyResult(const WorldMapRenderer::RenderResult &result)
{
    if (m_renderKey >= 0 && result.error.isEmpty() && !result.image.isNull()
        && result.renderedRegions > 0)
        putCache(m_renderKey, result);

    if (result.renderedRegions == 0)
    {
        m_stack->setCurrentIndex(0);
        const QString message = result.error.isEmpty()
            ? (result.failedRegions > 0
                   ? tr("区域文件读取失败，请确认游戏已退出、存档未损坏。")
                   : tr("该维度还没有生成的区块。"))
            : result.error;
        m_emptyLabel->setText(message);
        m_statusLabel->setText(tr("没有可显示的地图"));
        m_statSuffix.clear();
        m_liveKey.clear();
        clearTileState(true);
        return;
    }

    // 权威结果覆盖画布：与缓存共享同一块像素数据（QImage 隐式共享，不拷贝）
    m_liveImage = result.image;
    m_liveKey = QStringLiteral("%1x%2:%3:%4:%5")
                    .arg(result.image.width()).arg(result.image.height())
                    .arg(result.minChunkX).arg(result.minChunkZ)
                    .arg(result.blockPerPixel);
    m_liveMinChunkX = result.minChunkX;
    m_liveMinChunkZ = result.minChunkZ;
    m_liveBpp = result.blockPerPixel;
    m_layoutApplied = true;

    const bool wasVisible = m_view && m_view->isVisible();
    m_stack->setCurrentIndex(1);
    m_view->setImageRef(&m_liveImage, result.minChunkX, result.minChunkZ,
                        result.blockPerPixel, false);
    if (m_pendingRestoreBlockSet)
    {
        // 种子画布重定心：几何变化会把视口重置到出生点，恢复成重渲前的视角中心
        m_view->centerOnBlock(m_pendingRestoreBlock.x(),
                              m_pendingRestoreBlock.y());
        m_pendingRestoreBlockSet = false;
    }
    else if (!wasVisible)
    {
        m_view->scheduleInitialView(); // 首次显示：等布局给出真实几何后设初始视图
    }

    m_statSuffix = tr("区域 %1 · 已渲染 %2 · 每像素 %3 格")
                       .arg(result.regionCount)
                       .arg(result.renderedRegions)
                       .arg(result.blockPerPixel);
    if (result.seedRegions > 0)
        m_statSuffix += tr(" · 种子预测 %1").arg(result.seedRegions);
    if (result.reusedRegions > 0)
        m_statSuffix += tr(" · 复用 %1").arg(result.reusedRegions);
    if (result.failedRegions > 0)
        m_statSuffix += tr(" · 失败 %1").arg(result.failedRegions);
    if (result.elapsedMs > 0)
        m_statSuffix += QStringLiteral(" · %1 ms").arg(result.elapsedMs);
    updateStatusLabel();
    updateZoomLabel();

    // 概览就绪：作废上一批 1:1 瓦片，再按当前视口补细节（先大致、后精细）
    clearTileState(true);
    if (m_tileTimer)
        m_tileTimer->start();
    if (m_tileFillTimer && !m_busy)
        m_tileFillTimer->start();
    startStructRefresh(); // 视口最终确定后重算结构标记
}

// ── 视口 1:1 细节瓦片调度 ──

namespace {
/** 细节瓦片缓存上限：512×512×4B = 1MB/块，96 块 ≈ 96MB */
constexpr int kMaxTiles = 96;
/** 单轮最多排队的 region 数（渲染完成后再补，避免一次排满） */
constexpr int kMaxTilePending = 24;
/** 视口巨大时展开 region 的上限，防止遍历卡 UI */
constexpr int kMaxRegionScan = 4096;

/**
 * 从高度选择框的文本里解析自定义 Y：能取到数字就用它，
 * 取不到（"全部"/空）返回 -1 = 不限制，显示真实地表。
 */
int parseSurfaceY(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("(-?\\d+)"));
    const QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch())
        return -1;
    return std::clamp(m.captured(1).toInt(), -64000, 32000);
}
} // namespace

void WorldMapPage::clearTileState(bool clearViewTiles)
{
    ++m_tileSeq; // 在途结果回来后按批次号丢弃

    // 主动作废在途 watcher：断开回调并清空计数。
    // 若只靠"结束后自己减回去"，旧任务在途时会占满并发位，
    // 新一批（含视野左右两侧）一直发不出去 —— 表现为"中心铺完后边缘不再渲染"
    for (QFutureWatcher<TileResult> *w : m_tileWatchers)
    {
        if (!w)
            continue;
        w->disconnect(this);
        w->deleteLater();
    }
    m_tileWatchers.clear();
    m_tileInFlightKeys.clear();
    m_tileInFlight = 0;

    m_tilesPending.clear();
    m_tilesEmpty.clear();
    m_tileOrder.clear();
    m_detailText.clear();
    if (clearViewTiles && m_view)
        m_view->clearTiles();
    updateStatusLabel();
}

void WorldMapPage::rememberTile(const QPoint &key)
{
    m_tileOrder.removeAll(key);
    m_tileOrder.append(key);
    while (m_tileOrder.size() > kMaxTiles)
    {
        const QPoint oldest = m_tileOrder.takeFirst();
        if (m_view)
            m_view->removeTile(oldest);
    }
    m_detailText = tr("细节 %1").arg(m_view ? m_view->tileCount() : 0);
    updateStatusLabel();
}

void WorldMapPage::scheduleTiles()
{
    if (!m_view || !m_view->hasImage() || m_savePath.isEmpty() || m_busy)
        return;
    if (m_view->blockScreenSize() < kDetailMinBlockPx)
        return; // 方块在屏幕上太小：概览足够，放大到能看清方块再补细节

    QRect range = m_view->visibleRegionRange();
    if (range.isNull() || range.width() <= 0 || range.height() <= 0)
        return;
    // 裁剪到画布覆盖的 region：画布外的瓦片画出来也没底图，白白浪费渲染
    range &= m_view->imageRegionRange();
    if (range.isNull() || range.width() <= 0 || range.height() <= 0)
        return;
    const QPoint center = m_view->centerRegion();

    // 队列里只留可见的：视口外的既白渲、又会把"中心向外"的队位占掉
    QList<QPoint> stale;
    for (const QPoint &key : m_tilesPending)
        if (!range.contains(key))
            stale.append(key);
    for (const QPoint &key : stale)
        m_tilesPending.remove(key);

    QList<QPoint> candidates;
    int scanned = 0;
    for (int rz = range.top(); rz <= range.bottom() && scanned < kMaxRegionScan; ++rz)
    {
        for (int rx = range.left(); rx <= range.right() && scanned < kMaxRegionScan;
             ++rx, ++scanned)
        {
            const QPoint key(rx, rz);
            if (m_view->hasTile(key) || m_tilesPending.contains(key)
                || m_tilesEmpty.contains(key)
                || m_tileInFlightKeys.contains(key))
                continue;
            candidates.append(key);
        }
    }
    if (candidates.isEmpty())
        return;

    // 由中心向外：切比雪夫距离（方环一圈圈），同环内再按曼哈顿距离靠近中心
    std::sort(candidates.begin(), candidates.end(),
              [&center](const QPoint &a, const QPoint &b) {
                  const int da = std::max(std::abs(a.x() - center.x()),
                                          std::abs(a.y() - center.y()));
                  const int db = std::max(std::abs(b.x() - center.x()),
                                          std::abs(b.y() - center.y()));
                  if (da != db)
                      return da < db;
                  const int sa = std::abs(a.x() - center.x())
                               + std::abs(a.y() - center.y());
                  const int sb = std::abs(b.x() - center.x())
                               + std::abs(b.y() - center.y());
                  if (sa != sb)
                      return sa < sb;
                  return a.x() != b.x() ? a.x() < b.x() : a.y() < b.y();
              });

    // 队列总量受限：里面始终是"离中心最近的一批"，渲完再续下一批
    for (const QPoint &key : candidates)
    {
        if (m_tilesPending.size() >= kMaxTilePending)
            break;
        m_tilesPending.insert(key);
    }
    pumpTileQueue();
}

void WorldMapPage::pumpTileQueue()
{
    if (!m_view || m_tilesPending.isEmpty())
        return;

    while (m_tileInFlight < kMaxConcurrentTiles && !m_tilesPending.isEmpty())
    {
        const QPoint center = m_view->centerRegion();
        const auto chessOf = [&center](const QPoint &p) {
            return std::max(std::abs(p.x() - center.x()),
                            std::abs(p.y() - center.y()));
        };

        // 加载顺序仍是"由中心向外"：更近的环还在飞时，不放更远的进来并行，
        // 否则邻居可能先渲完，顺序断言/观感都乱
        int minInFlight = std::numeric_limits<int>::max();
        for (const QPoint &k : m_tileInFlightKeys)
            minInFlight = std::min(minInFlight, chessOf(k));
        int minPending = std::numeric_limits<int>::max();
        for (const QPoint &k : m_tilesPending)
            minPending = std::min(minPending, chessOf(k));
        if (minPending > minInFlight)
            break;

        // 只取当前最内圈（切比雪夫距离最小）的 region，同环可并行
        QPoint best;
        bool found = false;
        int bestManh = std::numeric_limits<int>::max();
        for (const QPoint &key : m_tilesPending)
        {
            if (chessOf(key) != minPending)
                continue;
            const int manh = std::abs(key.x() - center.x())
                           + std::abs(key.y() - center.y());
            if (manh < bestManh)
            {
                bestManh = manh;
                best = key;
                found = true;
            }
        }
        if (!found)
            return;

        m_tilesPending.remove(best);

        const QString path = m_savePath;
        const auto dim = static_cast<WorldMapRenderer::Dimension>(
            m_dimCombo ? m_dimCombo->currentIndex() : 0);
        const auto mode = static_cast<WorldMapRenderer::ShadeMode>(
            m_shadeCombo ? m_shadeCombo->currentIndex() : 0);
        const int seq = m_tileSeq;
        const QPoint key = best;
        const int maxSurfaceY = m_maxSurfaceY;
        const WorldMapRenderer::SeedPreview seedPrev = buildSeedPreview();

        // 与概览共用专用线程池：概览跑着时这里会排队，不会抢 UI 的 CPU；
        // 概览结束后最多 kMaxConcurrentTiles 路并行补细节
        auto *watcher = new QFutureWatcher<TileResult>(this);
        m_tileWatchers.append(watcher);
        m_tileInFlightKeys.append(key);
        ++m_tileInFlight;
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
            onTileFinished(watcher);
        });
        watcher->setFuture(QtConcurrent::run(&m_renderPool,
            [path, dim, mode, key, seq, maxSurfaceY, seedPrev]() {
                TileResult r;
                r.key = key;
                r.seq = seq;
                r.image = WorldMapRenderer::renderRegionTile(
                    path, dim, key.x(), key.y(), mode, maxSurfaceY, seedPrev);
                return r;
            }));
    }
}

void WorldMapPage::onTileFinished(QFutureWatcher<TileResult> *watcher)
{
    if (!watcher || !watcher->isFinished())
        return;

    const int idx = m_tileWatchers.indexOf(watcher);
    if (idx < 0)
    {
        // clearTileState 已作废的在途任务：不要再动 m_tileInFlight（可能已是新一批）
        watcher->deleteLater();
        return;
    }
    m_tileWatchers.removeAt(idx);
    if (idx < m_tileInFlightKeys.size())
        m_tileInFlightKeys.removeAt(idx);
    if (m_tileInFlight > 0)
        --m_tileInFlight;

    const TileResult r = watcher->result();
    watcher->deleteLater();

    if (r.seq != m_tileSeq)
    {
        // 换存档 / 换维度之前排队的：丢弃，新批次会重新调度
        continueTiling();
        return;
    }

    if (!r.image.isNull())
    {
        if (m_view)
            m_view->setTile(r.key.x(), r.key.y(), r.image);
        rememberTile(r.key);
    }
    else
    {
        m_tilesEmpty.insert(r.key); // 空文件 / 未生成，别再反复渲它
    }

    continueTiling();
}

/**
 * 队列还有就接着渲；空了就按当前视口补下一批 —— 于是加载表现为
 * 从视口中心一圈圈向四周扩散，直到可见区域被细节瓦片填满。
 */
void WorldMapPage::continueTiling()
{
    if (!m_tilesPending.isEmpty())
        pumpTileQueue();
    else
        scheduleTiles();
}

QList<QPoint> WorldMapPage::loadedTileOrder() const
{
    return m_tileOrder; // LRU 序即加载序（最早加载的在队首）
}

void WorldMapPage::setBusy(bool busy)
{
    m_busy = busy;
    if (m_progress)
        m_progress->setVisible(busy);
    if (m_exportBtn)
        m_exportBtn->setEnabled(!busy);
    if (m_gotoBtn)
        m_gotoBtn->setEnabled(!busy);
    // 概览结束后立刻开始"视野补洞"；渲染中停掉，避免和 m_busy 门闩打架
    if (m_tileFillTimer)
    {
        if (busy || !m_view || !m_view->hasImage())
            m_tileFillTimer->stop();
        else
            m_tileFillTimer->start();
    }
}

void WorldMapPage::updateZoomLabel()
{
    if (!m_zoomLabel || !m_view)
        return;
    //按"每方块的屏幕像素"报数：100% 就是 1 像素 = 1 方块
    m_zoomLabel->setText(
        QStringLiteral("%1%").arg(qRound(m_view->blockScreenSize() * 100.0)));
}

void WorldMapPage::updateStatusLabel(const QString &cursor)
{
    if (!m_statusLabel)
        return;
    if (!cursor.isEmpty())
        m_cursorText = cursor;

    QStringList parts;
    if (!m_cursorText.isEmpty())
        parts << m_cursorText;
    if (!m_selectionText.isEmpty())
        parts << m_selectionText;
    if (!m_detailText.isEmpty())
        parts << m_detailText;
    if (!m_statSuffix.isEmpty())
        parts << m_statSuffix;

    m_statusLabel->setText(parts.isEmpty() ? tr("已就绪")
                                           : parts.join(QStringLiteral("   ·   ")));
}

void WorldMapPage::clearSelection()
{
    if (m_view)
        m_view->clearSelection(); // 选中态文本由 onSelectionChanged 回调清掉
}

// ═══════════════════ 结构信息弹窗与完成标记 ═══════════════════

void WorldMapPage::loadStructDone()
{
    QSettings s;
    const QString sk = QStringLiteral("worldMap/structDone/")
                     + QString::number(qHash(m_savePath));
    m_structDone.clear();
    const QStringList list = s.value(sk).toStringList();
    for (const QString &v : list)
        m_structDone.insert(v);
    if (m_view)
        m_view->setDoneStructures(m_structDone);
}

void WorldMapPage::persistStructDone() const
{
    QSettings s;
    const QString sk = QStringLiteral("worldMap/structDone/")
                     + QString::number(qHash(m_savePath));
    const QStringList list(m_structDone.begin(), m_structDone.end());
    s.setValue(sk, list);
}

bool WorldMapPage::isStructureDone(int type, qint64 x, qint64 z) const
{
    return m_structDone.contains(WorldMapView::doneKey(type, x, z));
}

QPoint WorldMapPage::markerScreenPos(int idx) const
{
    return m_view ? m_view->markerScreenPos(idx) : QPoint();
}

QPoint WorldMapPage::markerBlockPos(int idx) const
{
    if (!m_view || idx < 0 || idx >= m_view->markerCount())
        return QPoint();
    // Marker 列表访问经 setMarkers 后不再变化；页面不重复存坐标，
    // 通过 doneKey 反查不可行 —— 这里从 view 内部拿（同 cpp 可见）
    return m_view->markerBlockPos(idx);
}

int WorldMapPage::markerType(int idx) const
{
    return m_view ? m_view->markerType(idx) : -1;
}

void WorldMapPage::showStructurePopup(const QPoint &globalPos, int structIdx,
                                      qint64 x, qint64 z)
{
    const auto &defs = StructureFinder::allStructures();
    if (structIdx < 0 || structIdx >= defs.size() || !m_view)
        return;
    const auto &d = defs[structIdx];

    if (m_structPopup)
        m_structPopup->close(); // 同时只留一个信息窗

    // parent = 页面：生命周期跟随页面，且探针可 findChild 找到
    auto *pop = new QWidget(this, Qt::Popup | Qt::FramelessWindowHint);
    m_structPopup = pop;
    pop->setObjectName(QStringLiteral("worldMapStructPopup"));
    pop->setAttribute(Qt::WA_DeleteOnClose);

    auto *lay = new QVBoxLayout(pop);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(8);

    // 标题行：图标 + 结构名称
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(8);
    auto *iconLabel = new QLabel(pop);
    iconLabel->setPixmap(StructureIcons::pixmap(
        structIdx, QLatin1String(d.key),
        QString(QChar::fromLatin1(d.displayName[0])),
        QLatin1String(d.wikiTitle), QLatin1String(d.itemTitle),
        QLatin1String(d.envSprite), 20));
    titleRow->addWidget(iconLabel);
    auto *titleLabel = new QLabel(QString::fromUtf8(d.displayName), pop);
    titleLabel->setObjectName(QStringLiteral("worldMapStructTitle"));
    titleRow->addWidget(titleLabel);
    titleRow->addStretch(1);
    lay->addLayout(titleRow);

    // 坐标（只读展示，复制走按钮）
    auto *coordLabel = new QLabel(tr("X %1   Z %2").arg(x).arg(z), pop);
    coordLabel->setObjectName(QStringLiteral("worldMapStructCoord"));
    lay->addWidget(coordLabel);

    // 操作行：复制坐标 / 查看百科 / 标记为已完成
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);

    auto *copyBtn = new QPushButton(tr("复制坐标"), pop);
    copyBtn->setObjectName(QStringLiteral("worldMapStructAction"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    QObject::connect(copyBtn, &QPushButton::clicked, copyBtn,
                     [this, x, z]() {
        QApplication::clipboard()->setText(
            QStringLiteral("%1, %2").arg(x).arg(z));
        NotificationManager::showSuccess(
            this, tr("已复制坐标：%1, %2").arg(x).arg(z));
    });
    btnRow->addWidget(copyBtn);

    auto *wikiBtn = new QPushButton(tr("查看百科"), pop);
    wikiBtn->setObjectName(QStringLiteral("worldMapStructAction"));
    wikiBtn->setCursor(Qt::PointingHandCursor);
    const QUrl wikiUrl(
        QStringLiteral("https://zh.minecraft.wiki/w/")
        + QString::fromLatin1(
              QUrl::toPercentEncoding(QLatin1String(d.wikiTitle))));
    wikiBtn->setProperty("wikiUrl", wikiUrl); // 探针读取，不真开浏览器
    QObject::connect(wikiBtn, &QPushButton::clicked, wikiBtn,
                     [wikiUrl]() { QDesktopServices::openUrl(wikiUrl); });
    btnRow->addWidget(wikiBtn);

    auto *doneBtn = new QPushButton(pop);
    doneBtn->setObjectName(QStringLiteral("worldMapStructAction"));
    doneBtn->setCursor(Qt::PointingHandCursor);
    const QString dkey = WorldMapView::doneKey(structIdx, x, z);
    const auto syncText = [doneBtn, this, dkey]() {
        doneBtn->setText(m_structDone.contains(dkey)
                             ? tr("取消完成标记")
                             : tr("标记为已完成"));
    };
    syncText();
    QObject::connect(doneBtn, &QPushButton::clicked, doneBtn,
                     [this, dkey, syncText]() {
        if (m_structDone.contains(dkey))
            m_structDone.remove(dkey);
        else
            m_structDone.insert(dkey);
        persistStructDone();
        if (m_view)
        {
            m_view->setDoneStructures(m_structDone);
            m_view->update();
        }
        syncText();
    });
    btnRow->addWidget(doneBtn);

    lay->addLayout(btnRow);
    pop->adjustSize();

    // 贴着点击位置弹出，但不出屏幕右/下边缘
    QPoint pos = globalPos + QPoint(10, 10);
    const QRect screen = pop->screen()->availableGeometry();
    if (pos.x() + pop->width() > screen.right())
        pos.setX(globalPos.x() - pop->width() - 10);
    if (pos.y() + pop->height() > screen.bottom())
        pos.setY(globalPos.y() - pop->height() - 10);
    pop->move(pos);
    pop->show();
}

bool WorldMapPage::structureChecked(int i) const
{
    return i >= 0 && i < m_structEnabled.size() && m_structEnabled[i];
}

int WorldMapPage::structureMarkerCount() const
{
    return m_view ? m_view->markerCount() : 0;
}

bool WorldMapPage::waitForStructs(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (m_structJob || m_structDirty)
    {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

void WorldMapPage::showMapContextMenu(const QPoint &globalPos, qint64 blockX,
                                      qint64 blockZ)
{
    const qint64 chunkX = blockX >> 4;
    const qint64 chunkZ = blockZ >> 4;

    // 非模态弹出：probe / 自动化里点不到菜单也不会卡死
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    menu->addAction(tr("复制方块坐标  %1  %2").arg(blockX).arg(blockZ), this,
                    [this, blockX, blockZ]() {
                        QApplication::clipboard()->setText(
                            QStringLiteral("%1 %2").arg(blockX).arg(blockZ));
                        NotificationManager::showSuccess(
                            this, tr("已复制方块坐标：%1 %2").arg(blockX).arg(blockZ));
                    });
    menu->addAction(tr("复制区块坐标  %1  %2").arg(chunkX).arg(chunkZ), this,
                    [this, chunkX, chunkZ]() {
                        QApplication::clipboard()->setText(
                            QStringLiteral("%1 %2").arg(chunkX).arg(chunkZ));
                        NotificationManager::showSuccess(
                            this, tr("已复制区块坐标：%1 %2").arg(chunkX).arg(chunkZ));
                    });

    menu->addSeparator();
    menu->addAction(tr("居中到这里"), this, [this, blockX, blockZ]() {
        if (m_view)
            m_view->centerOnBlock(blockX, blockZ);
    });
    menu->addAction(tr("填入跳转坐标"), this, [this, blockX, blockZ]() {
        if (m_expandBtn && !m_expandBtn->isChecked())
            m_expandBtn->setChecked(true);
        if (m_gotoX && m_gotoZ)
        {
            m_gotoX->setText(QString::number(blockX));
            m_gotoZ->setText(QString::number(blockZ));
            m_gotoX->setFocus();
        }
    });

    if (m_view && m_view->hasSelection())
    {
        menu->addSeparator();
        menu->addAction(tr("取消选择"), this, &WorldMapPage::clearSelection);
    }

    menu->popup(globalPos);
}

bool WorldMapPage::eventFilter(QObject *watched, QEvent *event)
{
    // 内容弹窗打开时：Esc 关闭；点弹窗外任意处关闭
    //（点「展开」按钮本身留给按钮 toggle 处理，避免"关了又开"）
    if (m_contentPopup && m_contentPopup->isVisible())
    {
        if (event->type() == QEvent::KeyPress)
        {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape)
            {
                closeContentPopup();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonPress)
        {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            const QPoint gp = mouse->globalPosition().toPoint();
            if (!m_contentPopup->rect().contains(
                    m_contentPopup->mapFromGlobal(gp)))
            {
                const bool onExpand = m_expandBtn
                    && m_expandBtn->rect().contains(
                           m_expandBtn->mapFromGlobal(gp));
                if (!onExpand)
                {
                    closeContentPopup();
                    return true; // 吞掉这次按下：只关弹窗，不误触地图
                }
            }
        }
    }
    // 结构条横向滚动：滚轮纵向转横滚（QScrollArea 纵向无行程时滚轮不生效）
    if (event->type() == QEvent::Wheel && m_structScroll
        && watched == m_structScroll->viewport())
    {
        const auto *wheel = static_cast<QWheelEvent *>(event);
        const int delta = wheel->angleDelta().y() != 0 ? wheel->angleDelta().y()
                                                       : wheel->angleDelta().x();
        if (delta != 0)
        {
            QScrollBar *sb = m_structScroll->horizontalScrollBar();
            sb->setValue(sb->value() - delta / 2);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void WorldMapPage::keyPressEvent(QKeyEvent *event)
{
    // 已有地图时才接管按键；否则交给父类（避免空白页吃掉全局键）
    const bool haveMap = m_view && m_view->hasImage();
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const int step = shift ? 240 : 60;

    if (haveMap)
    {
        switch (event->key())
        {
        // —— 缩放 ——
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            onZoomIn();
            event->accept();
            return;
        case Qt::Key_Minus:
            onZoomOut();
            event->accept();
            return;
        case Qt::Key_0:
        case Qt::Key_F:
            onFitClicked();
            event->accept();
            return;

        // —— 平移（方向键 + WASD，Shift 加速）——
        case Qt::Key_Left:
        case Qt::Key_A:
            m_view->panBy(QPoint(step, 0));
            event->accept();
            return;
        case Qt::Key_Right:
        case Qt::Key_D:
            m_view->panBy(QPoint(-step, 0));
            event->accept();
            return;
        case Qt::Key_Up:
        case Qt::Key_W:
            m_view->panBy(QPoint(0, step));
            event->accept();
            return;
        case Qt::Key_Down:
        case Qt::Key_S:
            m_view->panBy(QPoint(0, -step));
            event->accept();
            return;

        // —— 视图开关 / 输出 ——
        case Qt::Key_G:
            m_gridBtn->toggle(); // toggled 信号会同步画布
            event->accept();
            return;
        case Qt::Key_R:
            refresh();
            event->accept();
            return;
        case Qt::Key_E:
            onExportClicked();
            event->accept();
            return;
        case Qt::Key_C:
            clearSelection();
            event->accept();
            return;

        // —— 维度直达 ——
        case Qt::Key_1:
            m_dimCombo->setCurrentIndex(0);
            event->accept();
            return;
        case Qt::Key_2:
            m_dimCombo->setCurrentIndex(1);
            event->accept();
            return;
        case Qt::Key_3:
            m_dimCombo->setCurrentIndex(2);
            event->accept();
            return;

        default:
            break;
        }
    }

    // Esc / F5 / Home 等留给主窗口的全局快捷键，这里不拦截
    QWidget::keyPressEvent(event);
}

bool WorldMapPage::takeCached(int key, WorldMapRenderer::RenderResult *out)
{
    const auto it = m_cache.constFind(key);
    if (it == m_cache.constEnd())
        return false;
    *out = it.value();
    m_cacheOrder.removeAll(key);
    m_cacheOrder.append(key);
    return true;
}

void WorldMapPage::putCache(int key, const WorldMapRenderer::RenderResult &result)
{
    // 只缓存完整结果：取消/中断渲染出来的半张图不能当缓存用
    if (result.image.isNull() || result.renderedRegions < result.regionCount)
        return;

    m_cacheOrder.removeAll(key);
    m_cache.insert(key, result);
    m_cacheOrder.append(key);

    // 条数与总字节双上限：大世界单张可达 64MB，缓存不能把内存吃光
    const qint64 budget = qint64(128) * 1024 * 1024;
    while (m_cacheOrder.size() > 2)
        m_cache.remove(m_cacheOrder.takeFirst());
    while (m_cacheOrder.size() > 1)
    {
        qint64 total = 0;
        for (int k : m_cacheOrder)
        {
            const auto it = m_cache.constFind(k);
            if (it != m_cache.constEnd() && !it->image.isNull())
                total += qint64(it->image.sizeInBytes());
        }
        if (total <= budget)
            break;
        m_cache.remove(m_cacheOrder.takeFirst());
    }
}

// ── 顶部操作区响应 ──

void WorldMapPage::updateSubtitle()
{
    if (!m_subtitleLabel || !m_dimCombo)
        return;
    m_subtitleLabel->setText(tr("地表地图 · %1").arg(m_dimCombo->currentText()));
}

void WorldMapPage::onDimensionChanged(int)
{
    updateSubtitle();
    // 群系列按维度过滤：切维度后弹窗里的群系列表要换一批
    if (m_contentPopup && m_contentPopup->isVisible())
        rebuildPopupLists();
    m_priorLiveValid = false;
    m_priorLiveImage = QImage();
    startRender();
    startStructRefresh(); // 换维度要重算（结构的维度过滤不同）
    scheduleBiomeRefresh(); // 当前群系跟随维度
}

void WorldMapPage::onShadeChanged(int)
{
    m_priorLiveValid = false;
    m_priorLiveImage = QImage();
    startRender();
}

void WorldMapPage::onSeedToggled(bool on)
{
    m_seedPreviewOn = on;
    QSettings settings;
    settings.setValue(QStringLiteral("worldMap/seedPreview"), on);
    // 开关影响渲染结果与缓存 key，直接重渲（同 key 有缓存则秒切）
    m_priorLiveValid = false;
    m_priorLiveImage = QImage();
    startRender();
}

void WorldMapPage::onHeightChanged()
{
    if (!m_heightCombo)
        return;
    const int y = parseSurfaceY(m_heightCombo->currentText());
    if (y == m_maxSurfaceY)
        return; // 下拉项与 editingFinished 可能连发，值没变就不重渲
    m_maxSurfaceY = y;
    m_priorLiveValid = false;
    m_priorLiveImage = QImage();
    startRender();
}

void WorldMapPage::onGridToggled(bool on)
{
    if (m_view)
        m_view->setGridVisible(on);
}

void WorldMapPage::onZoomIn()
{
    if (!m_view)
        return;
    m_view->zoomBy(1.35);
    updateZoomLabel();
}

void WorldMapPage::onZoomOut()
{
    if (!m_view)
        return;
    m_view->zoomBy(1.0 / 1.35);
    updateZoomLabel();
}

void WorldMapPage::onFitClicked()
{
    if (!m_view)
        return;
    m_view->fitToWindow();
    updateZoomLabel();
}

int WorldMapPage::loadedDetailTiles() const
{
    return m_view ? m_view->tileCount() : 0;
}

QPoint WorldMapPage::viewCenterBlock() const
{
    return m_view ? m_view->centerBlock() : QPoint();
}

void WorldMapPage::onZoomToOneOne()
{
    if (!m_view)
        return;
    m_view->zoomToOneOne();
    updateZoomLabel();
}

void WorldMapPage::onGotoClicked()
{
    if (!m_view || !m_view->hasImage())
        return;
    bool okX = false, okZ = false;
    const qint64 x = m_gotoX->text().trimmed().toLongLong(&okX);
    const qint64 z = m_gotoZ->text().trimmed().toLongLong(&okZ);
    if (!okX || !okZ)
        return;
    m_view->centerOnBlock(x, z);
    updateZoomLabel();
}

void WorldMapPage::onExportClicked()
{
    if (!m_view || !m_view->hasImage())
        return;
    const QString suggested = m_saveName.isEmpty()
        ? QStringLiteral("worldmap.png")
        : m_saveName + QStringLiteral("_") + m_dimCombo->currentText()
              + QStringLiteral(".png");
    const QString path = AppFileDialog::getSaveFileName(
        this, tr("导出地图图片"), suggested,
        tr("PNG 图片 (*.png);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    if (m_view->image().save(path, "PNG"))
        m_statusLabel->setText(tr("已导出到 %1").arg(path));
    else
        m_statusLabel->setText(tr("导出失败：%1").arg(path));
}

// ═══════════════ 底部「当前群系」（中文名 + 视口中心采样） ═══════════════

void WorldMapPage::setInstancePath(const QString &instancePath)
{
    if (m_instancePath == instancePath)
        return;
    m_instancePath = instancePath;
    // 换实例：丢弃上个实例的中文群系表，后台重新解析当前实例 lang
    m_biomeNames.zh.clear();
    m_biomeNames.en.clear();
    loadBiomeNamesAsync();
}

/** 后台解析当前实例的群系中文名表（读客户端 JAR lang，IO 密集） */
void WorldMapPage::loadBiomeNamesAsync()
{
    const QString instance = m_instancePath;
    if (instance.isEmpty())
    {
        if (m_biomeLabel)
            m_biomeLabel->setText(tr("当前群系：—"));
        return;
    }
    auto job = std::make_shared<BiomeNameJob>();
    m_biomeNameJob = job;
    QtConcurrent::run(&m_structPool, [instance, job]() {
        job->names = BiomeName::loadFromInstance(instance);
        job->done.store(true, std::memory_order_release);
    });
    if (m_pollTimer && !m_pollTimer->isActive())
        m_pollTimer->start();
}

/** 后台采样视口中心的当前群系（去重：任务在途则标记待刷新） */
void WorldMapPage::scheduleBiomeRefresh()
{
    // 读不出种子 / 版本的存档无法按种子采样群系 → 显示占位
    if (!m_hasSeed || m_mcVersion == 0 || !m_view)
    {
        if (m_biomeLabel)
            m_biomeLabel->setText(tr("当前群系：—"));
        return;
    }
    if (m_biomeJob)
    {
        m_biomeDirty = true; // 正在算：这轮完成后按最新视口再来一轮
        return;
    }

    const QPoint c = m_view->centerBlock();
    auto job = std::make_shared<BiomeJob>();
    m_biomeJob = job;
    const quint64 seed = m_seed;
    const int mc = m_mcVersion;
    const auto dim = static_cast<WorldMapRenderer::Dimension>(
        m_dimCombo ? m_dimCombo->currentIndex() : 0);
    const int cdim = (dim == WorldMapRenderer::Dimension::Nether) ? -1
                   : (dim == WorldMapRenderer::Dimension::End)    ? 1
                                                                  : 0;
    QtConcurrent::run(&m_structPool, [seed, mc, cdim, c, job]() {
        // 与叠加层采样同一入口：单点网格（step=16 取中心）
        const auto grid = BiomeCatalog::sampleGrid(
            seed, mc, cdim, c.x() - 8, c.y() - 8, c.x() + 8, c.y() + 8, 16);
        if (!grid.ids.isEmpty())
        {
            job->biomeId = grid.ids[grid.ids.size() / 2];
            job->x = c.x();
            job->z = c.y();
        }
        job->done.store(true, std::memory_order_release);
    });
    if (m_pollTimer && !m_pollTimer->isActive())
        m_pollTimer->start();
}

/** 收取当前群系查询结果并刷新底部群系标签 */
void WorldMapPage::drainBiomeResult()
{
    if (!m_biomeJob)
        return;
    if (!m_biomeJob->done.load(std::memory_order_acquire))
        return;
    const auto job = m_biomeJob;
    m_biomeJob.reset();

    if (m_biomeLabel)
    {
        if (job->biomeId < 0)
        {
            m_biomeLabel->setText(tr("当前群系：—"));
        }
        else
        {
            const QString name = BiomeName::nameForId(
                m_mcVersion, job->biomeId, m_biomeNames);
            m_biomeLabel->setText(tr("当前群系：%1")
                                      .arg(name.isEmpty()
                                               ? QString::number(job->biomeId)
                                               : name));
        }
    }
    if (m_biomeDirty)
    {
        m_biomeDirty = false;
        scheduleBiomeRefresh(); // 拿最新视口再算一轮
    }
}

// ═══════════════ 内容浏览弹窗（结构 + 群系列表 / 搜索 / 高亮跳转） ═══════════════

namespace {

/** 弹窗列表行：左侧图标 + 名称 + 右侧「高亮跳转」小按钮 */
struct PopupRow
{
    QWidget *row = nullptr;   ///< 行容器：只负责行间分隔线
    QWidget *box = nullptr;   ///< 选中胶囊：同时包住名称与右侧定位按钮
    QPushButton *toggle = nullptr;  ///< 图标 + 名称，可勾选（决定是否在地图上显示）
    QPushButton *highlight = nullptr; ///< 高亮并跳转到最近一处
};

/**
 * 造一行内容条目。icon 点击 / 名称点击 = 勾选切换；右侧高亮按钮独立。
 * parent 为列表容器（ QListWidget 的 viewport 或 itemWidget 宿主）。
 */
PopupRow makePopupRow(QWidget *parent, const QPixmap &icon, const QString &name,
                      bool checked, bool enabled,
                      const QString &highlightTip)
{
    PopupRow r;
    r.row = new QWidget(parent);
    r.row->setObjectName(QStringLiteral("worldMapPopupRow"));
    // QSS 的 border-bottom 要生效：开样式背景
    r.row->setAttribute(Qt::WA_StyledBackground, true);
    auto *rowLay = new QHBoxLayout(r.row);
    // 选中胶囊与行边界只留很小距离，让勾选态看着占满整行
    rowLay->setContentsMargins(3, 3, 3, 3);
    rowLay->setSpacing(0);

    // 选中胶囊：勾选 / 悬停底色画在这一层，右侧定位按钮也被它包住
    r.box = new QWidget(r.row);
    r.box->setObjectName(QStringLiteral("worldMapPopupRowBox"));
    r.box->setAttribute(Qt::WA_StyledBackground, true);
    r.box->setAttribute(Qt::WA_Hover, true);
    r.box->setProperty("checked", checked);
    r.box->setProperty("supported", enabled);
    auto *lay = new QHBoxLayout(r.box);
    lay->setContentsMargins(6, 0, 4, 0);
    lay->setSpacing(6);
    rowLay->addWidget(r.box);

    r.toggle = new QPushButton(r.box);
    r.toggle->setObjectName(QStringLiteral("worldMapPopupToggle"));
    r.toggle->setCheckable(true);
    r.toggle->setChecked(checked);
    r.toggle->setEnabled(enabled);
    r.toggle->setCursor(Qt::PointingHandCursor);
    r.toggle->setIcon(icon);
    r.toggle->setIconSize(QSize(18, 18));
    r.toggle->setText(name);
    r.toggle->setToolTip(name);
    lay->addWidget(r.toggle, 1);

    r.highlight = new QPushButton(r.box);
    r.highlight->setObjectName(QStringLiteral("worldMapPopupHighlight"));
    r.highlight->setFixedSize(28, 28);
    r.highlight->setCursor(Qt::PointingHandCursor);
    r.highlight->setEnabled(enabled);
    r.highlight->setToolTip(highlightTip);
    r.highlight->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/pin_filled.svg"),
        ThemeManager::instance()->currentThemeColor(), 14));
    r.highlight->setIconSize(QSize(14, 14));
    lay->addWidget(r.highlight);

    // 勾选态由行内胶囊承载：切换时刷新动态属性，样式表据此换底色
    QObject::connect(r.toggle, &QPushButton::toggled, r.box,
                     [box = r.box](bool on) {
                         box->setProperty("checked", on);
                         box->style()->unpolish(box);
                         box->style()->polish(box);
                         box->update();
                     });

    return r;
}

} // namespace

void WorldMapPage::onExpandToggled(bool on)
{
    if (!on)
    {
        if (m_contentPopup)
            m_contentPopup->hide();
        return;
    }
    if (!m_contentPopup)
    {
        // 居中弹窗：跟随页面生命周期，非模态（点地图仍可拖拽 / 缩放）
        m_contentPopup = new QWidget(this);
        m_contentPopup->setObjectName(QStringLiteral("worldMapContentPopup"));
        m_contentPopup->setAttribute(Qt::WA_StyledBackground, true);
        m_contentPopup->setMinimumSize(560, 480);
        m_contentPopup->resize(680, 560);
        // 柔和投影：让弹窗浮在地图上方，层次一眼可辨
        auto *shadow = new QGraphicsDropShadowEffect(m_contentPopup);
        shadow->setBlurRadius(36);
        shadow->setOffset(0, 10);
        const bool darkPopup = ThemeManager::instance()
            && ThemeManager::instance()->currentTheme() != ThemeManager::LightTheme;
        shadow->setColor(QColor(0, 0, 0, darkPopup ? 140 : 55));
        m_contentPopup->setGraphicsEffect(shadow);

        // 点弹窗外关闭：过滤应用级鼠标按下事件（弹窗可见时才生效）
        QApplication::instance()->installEventFilter(this);

        auto *lay = new QVBoxLayout(m_contentPopup);
        lay->setContentsMargins(18, 16, 18, 16);
        lay->setSpacing(12);

        // —— 顶行：搜索框 + 坐标跳转 + 关闭按钮 ——
        auto *topRow = new QHBoxLayout;
        topRow->setSpacing(6);
        m_popupSearch = new QLineEdit(m_contentPopup);
        m_popupSearch->setObjectName(QStringLiteral("worldMapPopupSearch"));
        m_popupSearch->setPlaceholderText(tr("搜索结构或群系…"));
        m_popupSearch->setClearButtonEnabled(true);
        // 左侧放大镜图标
        m_popupSearch->addAction(IconHelper::loadColoredIcon(
                                     QStringLiteral(":/Images/Icons/search.svg"),
                                     ThemeManager::instance()->currentThemeColor(),
                                     14),
                                 QLineEdit::LeadingPosition);
        topRow->addWidget(m_popupSearch, 1);
        connect(m_popupSearch, &QLineEdit::textChanged,
                this, &WorldMapPage::onPopupSearchChanged);

        // —— 坐标跳转（原顶部工具栏定位组，已移至搜索框右侧） ——
        auto *gotoLabel = new QLabel(tr("坐标跳转"), m_contentPopup);
        gotoLabel->setObjectName(QStringLiteral("worldMapPopupColTitle"));
        topRow->addWidget(gotoLabel);

        m_gotoX = new QLineEdit(m_contentPopup);
        m_gotoX->setObjectName("worldMapCoordEdit");
        m_gotoX->setPlaceholderText(tr("X"));
        m_gotoX->setToolTip(tr("要跳转到的方块坐标"));
        m_gotoX->setValidator(new QIntValidator(-30000000, 30000000, m_gotoX));
        m_gotoX->setFixedSize(52, 26);
        topRow->addWidget(m_gotoX);

        m_gotoZ = new QLineEdit(m_contentPopup);
        m_gotoZ->setObjectName("worldMapCoordEdit");
        m_gotoZ->setPlaceholderText(tr("Z"));
        m_gotoZ->setToolTip(tr("要跳转到的方块坐标"));
        m_gotoZ->setValidator(new QIntValidator(-30000000, 30000000, m_gotoZ));
        m_gotoZ->setFixedSize(52, 26);
        topRow->addWidget(m_gotoZ);

        m_gotoBtn = new QPushButton(tr("跳转"), m_contentPopup);
        m_gotoBtn->setObjectName(QStringLiteral("worldMapGroupBtn"));
        m_gotoBtn->setCursor(Qt::PointingHandCursor);
        m_gotoBtn->setFixedHeight(26);
        m_gotoBtn->setToolTip(tr("把视图中心移动到该坐标（回车）"));
        topRow->addWidget(m_gotoBtn);

        connect(m_gotoBtn, &QPushButton::clicked, this, &WorldMapPage::onGotoClicked);
        connect(m_gotoX, &QLineEdit::returnPressed, this, &WorldMapPage::onGotoClicked);
        connect(m_gotoZ, &QLineEdit::returnPressed, this, &WorldMapPage::onGotoClicked);

        auto *closeBtn = new QPushButton(QStringLiteral("×"), m_contentPopup);
        closeBtn->setObjectName(QStringLiteral("worldMapPopupClose"));
        closeBtn->setFixedSize(28, 28);
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setToolTip(tr("关闭（Esc）"));
        connect(closeBtn, &QPushButton::clicked,
                this, &WorldMapPage::closeContentPopup);
        topRow->addWidget(closeBtn);
        lay->addLayout(topRow);

        // —— 下方双列：左结构、右群系（各带全选 / 清空） ——
        auto *cols = new QHBoxLayout;
        cols->setSpacing(12);

        auto makeListColumn = [&](const QString &title, QListWidget **listOut,
                                  const char *allName, const char *clearName,
                                  const QString &allTip,
                                  const QString &clearTip,
                                  std::function<void(bool)> bulk) {
            auto *col = new QWidget(m_contentPopup);
            auto *colLay = new QVBoxLayout(col);
            colLay->setContentsMargins(0, 0, 0, 0);
            colLay->setSpacing(6);
            auto *label = new QLabel(title, col);
            label->setObjectName(QStringLiteral("worldMapPopupColTitle"));
            colLay->addWidget(label);
            auto *list = new QListWidget(col);
            list->setObjectName(QStringLiteral("worldMapPopupList"));
            list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            list->setSelectionMode(QListWidget::NoSelection);
            list->setFocusPolicy(Qt::NoFocus);
            colLay->addWidget(list, 1);
            *listOut = list;

            // 列表下方：全选 / 清空
            auto *btnRow = new QHBoxLayout;
            btnRow->setSpacing(6);
            auto *allBtn = new QPushButton(tr("全选"), col);
            allBtn->setObjectName(QString::fromLatin1(allName));
            allBtn->setCursor(Qt::PointingHandCursor);
            allBtn->setToolTip(allTip);
            connect(allBtn, &QPushButton::clicked, this, [bulk]() {
                bulk(true);
            });
            btnRow->addWidget(allBtn);
            auto *clrBtn = new QPushButton(tr("清空"), col);
            clrBtn->setObjectName(QString::fromLatin1(clearName));
            clrBtn->setCursor(Qt::PointingHandCursor);
            clrBtn->setToolTip(clearTip);
            connect(clrBtn, &QPushButton::clicked, this, [bulk]() {
                bulk(false);
            });
            btnRow->addWidget(clrBtn);
            btnRow->addStretch(1);
            colLay->addLayout(btnRow);

            return col;
        };

        QWidget *structCol = makeListColumn(
            tr("结构"), &m_structList,
            "worldMapPopupStructAll", "worldMapPopupStructClear",
            tr("勾选全部结构（与第二行开关同步）"),
            tr("取消勾选全部结构"),
            [this](bool on2) { setAllStructures(on2); });
        cols->addWidget(structCol, 1);
        QWidget *biomeCol = makeListColumn(
            tr("群系"), &m_biomeList,
            "worldMapPopupBiomeAll", "worldMapPopupBiomeClear",
            tr("勾选当前列表中的全部群系"),
            tr("取消勾选全部群系"),
            [this](bool on2) { setAllBiomes(on2); });
        cols->addWidget(biomeCol, 1);
        lay->addLayout(cols, 1);

        auto *hint = new QLabel(
            tr("勾选 = 在地图上显示（群系默认不显示）；右侧按钮 = 高亮并跳转到离视角最近的一处"),
            m_contentPopup);
        hint->setObjectName(QStringLiteral("worldMapPopupHint"));
        lay->addWidget(hint);
    }

    rebuildPopupLists();

    // 居中于地图画布（不是整个窗口 —— 顶部工具栏不遮挡弹窗）
    if (m_stack)
    {
        const QPoint center = m_stack->mapTo(this, m_stack->rect().center());
        m_contentPopup->move(center - QPoint(m_contentPopup->width() / 2,
                                             m_contentPopup->height() / 2));
    }
    m_contentPopup->show();
    m_contentPopup->raise();
    m_popupSearch->setFocus();
}

void WorldMapPage::closeContentPopup()
{
    if (m_contentPopup)
        m_contentPopup->hide();
    if (m_expandBtn && m_expandBtn->isChecked())
    {
        const QSignalBlocker blocker(m_expandBtn);
        m_expandBtn->setChecked(false);
    }
}

void WorldMapPage::setAllStructures(bool on)
{
    for (int i = 0; i < m_structBtns.size(); ++i)
    {
        const QSignalBlocker b(m_structBtns[i]); // 阻止逐个 toggled 反复重算
        m_structBtns[i]->setChecked(on);
        if (i < m_structEnabled.size())
            m_structEnabled[i] = on;
    }
    if (m_contentPopup && m_contentPopup->isVisible())
        rebuildPopupLists();
    startStructRefresh();
}

void WorldMapPage::setAllBiomes(bool on)
{
    // 作用于弹窗当前列表的可见行（按维度过滤后的群系）
    for (int idx : std::as_const(m_biomeVisible))
    {
        if (idx >= 0 && idx < m_biomeEnabled.size())
            m_biomeEnabled[idx] = on;
    }
    if (m_contentPopup && m_contentPopup->isVisible())
        rebuildPopupLists();
    // 与 onPopupBiomeToggled 收尾逻辑一致
    if (!std::any_of(m_biomeEnabled.cbegin(), m_biomeEnabled.cend(),
                     [](bool b) { return b; }))
    {
        m_biomeGrid = BiomeCatalog::SampleGrid();
        m_biomeOverlay = QImage();
        if (m_view)
            m_view->setBiomeOverlay(nullptr, 0, 0, 64);
    }
    else if (m_biomeGrid.ids.isEmpty())
    {
        startStructRefresh(); // 从全清空重新勾选：需要重采网格
    }
    else
    {
        rebuildBiomeOverlay();
    }
}

void WorldMapPage::rebuildPopupLists()
{
    if (!m_structList || !m_biomeList)
        return;
    const QString filter = m_popupSearch ? m_popupSearch->text().trimmed()
                                         : QString();

    // —— 结构列 ——
    const auto &sdefs = StructureFinder::allStructures();
    m_structList->clear();
    for (int i = 0; i < sdefs.size(); ++i)
    {
        const auto &d = sdefs[i];
        const QString name = QString::fromUtf8(d.displayName);
        if (!filter.isEmpty()
            && !name.contains(filter, Qt::CaseInsensitive)
            && !QLatin1String(d.key).contains(filter, Qt::CaseInsensitive))
            continue;
        const bool supported = d.type != StructureFinder::kUnsupportedType;
        const bool checked = i < m_structEnabled.size() && m_structEnabled[i];
        auto *item = new QListWidgetItem(m_structList);
        item->setData(Qt::UserRole, i);
        item->setSizeHint(QSize(0, 48));
        PopupRow row = makePopupRow(
            m_structList->viewport(),
            StructureIcons::pixmap(
                i, QLatin1String(d.key),
                QString(QChar::fromLatin1(d.displayName[0])),
                QLatin1String(d.wikiTitle), QLatin1String(d.itemTitle),
                QLatin1String(d.envSprite), 16),
            name, checked, supported,
            supported ? tr("高亮并跳转到离视角最近的%1").arg(name)
                      : tr("%1 暂不支持定位").arg(name));
        m_structList->setItemWidget(item, row.row);
        connect(row.toggle, &QPushButton::toggled, this, [this, i](bool on) {
            onPopupStructToggled(i, on);
        });
        connect(row.highlight, &QPushButton::clicked, this, [this, i]() {
            highlightStructureType(i);
        });
    }

    // —— 群系列（按当前维度过滤） ——
    const auto dim = static_cast<WorldMapRenderer::Dimension>(
        m_dimCombo ? m_dimCombo->currentIndex() : 0);
    const int cdim = (dim == WorldMapRenderer::Dimension::Nether) ? -1
                   : (dim == WorldMapRenderer::Dimension::End)    ? 1
                                                                  : 0;
    const auto &bdefs = BiomeCatalog::allBiomes();
    m_biomeVisible.clear();
    m_biomeList->clear();
    for (int i = 0; i < bdefs.size(); ++i)
    {
        const auto &d = bdefs[i];
        if (d.dim != cdim)
            continue;
        const QString name = QString::fromUtf8(d.displayName);
        if (!filter.isEmpty()
            && !name.contains(filter, Qt::CaseInsensitive)
            && !QLatin1String(d.key).contains(filter, Qt::CaseInsensitive))
            continue;
        m_biomeVisible.append(i);
        auto *item = new QListWidgetItem(m_biomeList);
        item->setData(Qt::UserRole, m_biomeVisible.size() - 1);
        item->setSizeHint(QSize(0, 48));
        // 群系图标：Wiki pageimages 主图（key 前缀 biome_ 与结构缓存隔离）
        const bool checked = i < m_biomeEnabled.size() && m_biomeEnabled[i];
        PopupRow row = makePopupRow(
            m_biomeList->viewport(),
            StructureIcons::pixmap(
                2000 + i, QStringLiteral("biome_") + QLatin1String(d.key),
                QString(QChar::fromLatin1(d.displayName[0])),
                QLatin1String(d.wikiTitle), QString(), QString(), 16),
            name, checked, true,
            tr("高亮并跳转到离视角最近的%1").arg(name));
        m_biomeList->setItemWidget(item, row.row);
        const int biomeIdx = i;
        connect(row.toggle, &QPushButton::toggled, this,
                [this, biomeIdx](bool on) { onPopupBiomeToggled(biomeIdx, on); });
        connect(row.highlight, &QPushButton::clicked, this, [this, biomeIdx]() {
            highlightBiome(biomeIdx);
        });
    }
}

void WorldMapPage::onPopupSearchChanged(const QString &)
{
    rebuildPopupLists();
}

void WorldMapPage::onPopupStructToggled(int idx, bool on)
{
    if (idx < 0 || idx >= m_structEnabled.size())
        return;
    if (m_structEnabled[idx] == on)
        return;
    m_structEnabled[idx] = on;
    // 与第二行开关按钮双向同步（阻断信号避免回环）
    if (idx < m_structBtns.size())
    {
        const QSignalBlocker blocker(m_structBtns[idx]);
        m_structBtns[idx]->setChecked(on);
    }
    startStructRefresh();
}

void WorldMapPage::onPopupBiomeToggled(int idx, bool on)
{
    if (idx < 0 || idx >= m_biomeEnabled.size())
        return;
    if (m_biomeEnabled[idx] == on)
        return;
    m_biomeEnabled[idx] = on;
    // 勾选变化只需按已采样的网格重建叠加层（不用重算群系）
    rebuildBiomeOverlay();
    // 一个都没选时顺手把网格丢掉，下次全选再采
    if (!std::any_of(m_biomeEnabled.cbegin(), m_biomeEnabled.cend(),
                     [](bool b) { return b; }))
    {
        m_biomeGrid = BiomeCatalog::SampleGrid();
        m_biomeOverlay = QImage();
        if (m_view)
            m_view->setBiomeOverlay(nullptr, 0, 0, 64);
    }
    else if (m_biomeGrid.ids.isEmpty())
    {
        startStructRefresh(); // 从全清空重新勾选：需要重采网格
    }
}

void WorldMapPage::rebuildBiomeOverlay()
{
    if (!m_view)
        return;
    if (m_biomeGrid.ids.isEmpty() || m_biomeGrid.nx <= 0 || m_biomeGrid.nz <= 0)
    {
        m_biomeOverlay = QImage();
        m_view->setBiomeOverlay(nullptr, 0, 0, 64);
        return;
    }

    const auto &bdefs = BiomeCatalog::allBiomes();
    // id → 颜色 / 是否选中 的快速查表
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

int WorldMapPage::biomeRowCount() const
{
    return m_biomeVisible.size();
}

bool WorldMapPage::biomeChecked(int i) const
{
    if (i < 0 || i >= m_biomeVisible.size())
        return false;
    const int idx = m_biomeVisible[i];
    return idx >= 0 && idx < m_biomeEnabled.size() && m_biomeEnabled[idx];
}

int WorldMapPage::biomeOverlayCellCount() const
{
    if (m_biomeOverlay.isNull())
        return 0;
    int n = 0;
    for (int y = 0; y < m_biomeOverlay.height(); ++y)
        for (int x = 0; x < m_biomeOverlay.width(); ++x)
            if (qAlpha(m_biomeOverlay.pixel(x, y)) > 0)
                ++n;
    return n;
}

bool WorldMapPage::toggleContentPopup()
{
    if (!m_expandBtn)
        return false;
    m_expandBtn->toggle(); // toggled 信号统一走 onExpandToggled
    return isContentPopupOpen();
}

bool WorldMapPage::highlightStructureType(int idx)
{
    if (idx < 0 || idx >= m_structEnabled.size())
        return false;
    if (!m_hasSeed || m_mcVersion == 0 || !m_view)
        return false;
    const auto &defs = StructureFinder::allStructures();
    if (idx >= defs.size() || defs[idx].type == StructureFinder::kUnsupportedType)
        return false;

    // 确保它在地图上显示（未勾选则代为勾选）
    if (!m_structEnabled[idx])
    {
        m_structEnabled[idx] = true;
        if (idx < m_structBtns.size())
        {
            const QSignalBlocker blocker(m_structBtns[idx]);
            m_structBtns[idx]->setChecked(true);
        }
        startStructRefresh();
    }

    // 先在当前视口的标记里找最近的；找不到再后台扩范围搜
    const QPoint center = m_view->centerBlock();
    qint64 best = 0;
    qint64 bx = 0, bz = 0;
    bool found = false;
    const int n = m_view->markerCount();
    for (int i = 0; i < n; ++i)
    {
        if (m_view->markerType(i) != idx)
            continue;
        const QPoint p = m_view->markerBlockPos(i);
        const qint64 d = qint64(p.x() - center.x()) * (p.x() - center.x())
                       + qint64(p.y() - center.y()) * (p.y() - center.y());
        if (!found || d < best)
        {
            found = true;
            best = d;
            bx = p.x();
            bz = p.y();
        }
    }
    if (found)
    {
        m_view->centerOnBlock(bx, bz);
        m_view->setHighlight(bx, bz, true);
        updateStatusLabel(tr("已高亮：%1（X %2  Z %3）")
                              .arg(QString::fromUtf8(defs[idx].displayName))
                              .arg(bx).arg(bz));
        return true;
    }

    // 视口内没有 → 后台在更大范围里找最近的一处
    if (m_locateJob && m_locateJob->done.load(std::memory_order_acquire))
        m_locateJob.reset();
    if (m_locateJob)
        return false; // 已有一个定位在跑：不叠加

    auto job = std::make_shared<LocateJob>();
    job->isBiome = false;
    job->idx = idx;
    m_locateJob = job;
    const quint64 seed = m_seed;
    const int mc = m_mcVersion;
    const auto dim = static_cast<WorldMapRenderer::Dimension>(
        m_dimCombo ? m_dimCombo->currentIndex() : 0);
    const int cdim = (dim == WorldMapRenderer::Dimension::Nether) ? -1
                   : (dim == WorldMapRenderer::Dimension::End)    ? 1
                                                                  : 0;
    QtConcurrent::run(&m_structPool, [seed, mc, cdim, center, idx, job]() {
        // 从视口中心向外扩一倍范围搜索（结构网格间距有限，一定能覆盖最近的一处）
        constexpr qint64 kR = 16384;
        QVector<bool> only(idx, false);
        only.append(true);
        const auto hits = StructureFinder::find(
            seed, mc, cdim, center.x() - kR, center.y() - kR,
            center.x() + kR, center.y() + kR, only);
        qint64 bestD = 0;
        for (const auto &h : hits)
        {
            const qint64 d = qint64(h.x - center.x()) * (h.x - center.x())
                           + qint64(h.z - center.y()) * (h.z - center.y());
            if (!job->found || d < bestD)
            {
                job->found = true;
                bestD = d;
                job->x = h.x;
                job->z = h.z;
            }
        }
        job->done.store(true, std::memory_order_release);
    });
    if (m_pollTimer && !m_pollTimer->isActive())
        m_pollTimer->start();
    return true;
}

bool WorldMapPage::highlightBiome(int idx)
{
    if (idx < 0 || idx >= m_biomeEnabled.size())
        return false;
    if (!m_hasSeed || m_mcVersion == 0 || !m_view)
        return false;

    // 确保它在叠加层里显示
    if (!m_biomeEnabled[idx])
    {
        m_biomeEnabled[idx] = true;
        if (m_contentPopup && m_contentPopup->isVisible())
            rebuildPopupLists();
        rebuildBiomeOverlay();
    }

    const auto &d = BiomeCatalog::allBiomes()[idx];
    const QPoint center = m_view->centerBlock();

    // 先在当前采样网格里找最近的该群系格子
    qint64 bx = 0, bz = 0;
    bool found = false;
    qint64 best = 0;
    if (!m_biomeGrid.ids.isEmpty())
    {
        for (int j = 0; j < m_biomeGrid.nz; ++j)
        {
            for (int i = 0; i < m_biomeGrid.nx; ++i)
            {
                if (m_biomeGrid.ids[j * m_biomeGrid.nx + i] != d.id)
                    continue;
                const qint64 x = m_biomeGrid.minX
                               + qint64(i) * m_biomeGrid.step
                               + m_biomeGrid.step / 2;
                const qint64 z = m_biomeGrid.minZ
                               + qint64(j) * m_biomeGrid.step
                               + m_biomeGrid.step / 2;
                const qint64 dist = qint64(x - center.x()) * (x - center.x())
                                  + qint64(z - center.y()) * (z - center.y());
                if (!found || dist < best)
                {
                    found = true;
                    best = dist;
                    bx = x;
                    bz = z;
                }
            }
        }
    }
    if (found)
    {
        m_view->centerOnBlock(bx, bz);
        m_view->setHighlight(bx, bz, true);
        updateStatusLabel(tr("已高亮：%1（X %2  Z %3）")
                              .arg(QString::fromUtf8(d.displayName))
                              .arg(bx).arg(bz));
        return true;
    }

    // 网格里没有（该群系不在当前采样范围）→ 后台环形搜索
    if (m_locateJob && m_locateJob->done.load(std::memory_order_acquire))
        m_locateJob.reset();
    if (m_locateJob)
        return false;

    auto job = std::make_shared<LocateJob>();
    job->isBiome = true;
    job->idx = idx;
    m_locateJob = job;
    const quint64 seed = m_seed;
    const int mc = m_mcVersion;
    const auto dim = static_cast<WorldMapRenderer::Dimension>(
        m_dimCombo ? m_dimCombo->currentIndex() : 0);
    const int cdim = (dim == WorldMapRenderer::Dimension::Nether) ? -1
                   : (dim == WorldMapRenderer::Dimension::End)    ? 1
                                                                  : 0;
    QtConcurrent::run(&m_structPool, [seed, mc, cdim, center, idx, job]() {
        qint64 x = 0, z = 0;
        job->found = BiomeCatalog::findNearest(
            seed, mc, cdim, idx, center.x(), center.y(), 16384, &x, &z);
        job->x = x;
        job->z = z;
        job->done.store(true, std::memory_order_release);
    });
    if (m_pollTimer && !m_pollTimer->isActive())
        m_pollTimer->start();
    return true;
}

void WorldMapPage::drainLocateResult()
{
    if (!m_locateJob)
        return;
    if (!m_locateJob->done.load(std::memory_order_acquire))
        return;
    const auto job = m_locateJob;
    m_locateJob.reset();
    if (!job->found || !m_view)
        return;

    m_view->centerOnBlock(job->x, job->z);
    m_view->setHighlight(job->x, job->z, true);
    const auto &name = job->isBiome
        ? QString::fromUtf8(BiomeCatalog::allBiomes()[job->idx].displayName)
        : QString::fromUtf8(StructureFinder::allStructures()[job->idx].displayName);
    updateStatusLabel(tr("已高亮：%1（X %2  Z %3）")
                          .arg(name).arg(job->x).arg(job->z));
}
