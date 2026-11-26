#include "LayerBuildPlanWidget.h"

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSet>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "components/VoxelViewWidget.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/SchematicDocument.h"
#include "utils/ThemeManager.h"

namespace {

/** 四周留给刻度尺的宽度（左 / 上 / 右 / 下） */
constexpr int kRulerLeft = 56;
constexpr int kRulerTop = 56;
constexpr int kRulerRight = 48;
constexpr int kRulerBottom = 48;

/** 高亮当前步骤的琥珀色：不跟主题色走，保证任何主题下都醒目 */
const QColor kTargetAccent(255, 193, 87);

QColor mixColor(const QColor &a, const QColor &b, double t)
{
    const double u = qBound(0.0, t, 1.0);
    return QColor(int(a.red() + (b.red() - a.red()) * u + 0.5),
                  int(a.green() + (b.green() - a.green()) * u + 0.5),
                  int(a.blue() + (b.blue() - a.blue()) * u + 0.5),
                  int(a.alpha() + (b.alpha() - a.alpha()) * u + 0.5));
}

/** 主题色太暗时提亮一点，刻度数字才看得清 */
QColor readableColor(QColor color)
{
    if (color.lightness() < 100)
        color = color.lighter(150);
    return color;
}

} // namespace

LayerBuildPlanWidget::LayerBuildPlanWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("buildPlanView"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(360, 320);

    if (ThemeManager *tm = ThemeManager::instance())
    {
        connect(tm, &ThemeManager::themeChanged, this, [this]() { update(); });
        connect(tm, &ThemeManager::themeColorChanged, this, [this]() { update(); });
    }
}

void LayerBuildPlanWidget::setDocument(SchematicDocument *doc)
{
    m_doc = doc;
    m_layerY = -1;
    m_targetCells.clear();
    m_targetBoundsCells = QRect();
    m_paletteColors.clear();
    m_paletteTextures.clear();
    m_fitted = false;
    m_hoverX = -1;
    m_hoverZ = -1;
    m_placeholder.clear();
    m_overlayMessage.clear();

    if (m_doc && m_doc->isLoaded())
    {
        rebuildPaletteColors();
        rebuildPaletteTextures();
        fitToView();
    }
    update();
}

void LayerBuildPlanWidget::setTextureLoader(InstanceTextureLoader *loader)
{
    if (m_loader == loader)
        return;
    m_loader = loader;
    rebuildPaletteTextures();
    update();
}

void LayerBuildPlanWidget::setStep(int layerY, const QSet<int> &targetCells)
{
    if (!m_doc || !m_doc->isLoaded())
        return;

    const bool layerChanged = (layerY != m_layerY);
    m_layerY = layerY;
    m_targetCells = targetCells;
    m_overlayMessage.clear();

    // 先算高亮格的包围矩形：缩得很小时不再逐格描边，直接概括高亮
    m_targetBoundsCells = QRect();
    if (m_doc && m_doc->sizeX() > 0 && m_doc->layerSize() > 0)
    {
        const int sx = m_doc->sizeX();
        const int layerSize = m_doc->layerSize();
        for (int index : m_targetCells)
        {
            const int rem = index % layerSize;
            const int z = rem / sx;
            const int x = rem % sx;
            if (m_targetBoundsCells.isNull())
                m_targetBoundsCells = QRect(x, z, 1, 1);
            else
                m_targetBoundsCells |= QRect(x, z, 1, 1);
        }
    }

    if (layerChanged || !m_fitted)
    {
        m_fitted = false;
        fitToView();
    }
    else
    {
        clampOffset();
        update();
    }
}

void LayerBuildPlanWidget::setPlaceholder(const QString &text)
{
    m_placeholder = text;
    update();
}

void LayerBuildPlanWidget::setOverlayMessage(const QString &text)
{
    m_overlayMessage = text;
    update();
}

void LayerBuildPlanWidget::fitToView()
{
    if (!m_doc || !m_doc->isLoaded() || m_doc->sizeX() <= 0 || m_doc->sizeZ() <= 0)
        return;

    const QRectF area(contentRect());
    const double sx = m_doc->sizeX();
    const double sz = m_doc->sizeZ();
    const double fitScale = qMin((area.width() - 2.0) / sx, (area.height() - 2.0) / sz);
    m_scale = qBound(0.25, fitScale, 64.0);

    m_offset = QPointF((area.width() - sx * m_scale) * 0.5,
                       (area.height() - sz * m_scale) * 0.5);
    m_fitted = true;
    update();
}

QSize LayerBuildPlanWidget::minimumSizeHint() const
{
    return QSize(400, 340);
}

QRect LayerBuildPlanWidget::contentRect() const
{
    QRect r = rect();
    r.adjust(kRulerLeft, kRulerTop, -kRulerRight, -kRulerBottom);
    if (r.width() < 40)
        r.setWidth(40);
    if (r.height() < 40)
        r.setHeight(40);
    return r;
}

QRectF LayerBuildPlanWidget::cellRect(int x, int z) const
{
    const QRectF area(contentRect());
    const QPointF origin = area.topLeft() + m_offset;
    return QRectF(origin.x() + x * m_scale, origin.y() + z * m_scale,
                  m_scale, m_scale);
}

bool LayerBuildPlanWidget::cellAt(const QPoint &pos, int &x, int &z) const
{
    if (!m_doc || !m_doc->isLoaded())
        return false;

    const QRectF area(contentRect());
    if (!area.contains(pos))
        return false;

    const QPointF origin = area.topLeft() + m_offset;
    const double gx = (pos.x() - origin.x()) / m_scale;
    const double gz = (pos.y() - origin.y()) / m_scale;
    if (gx < 0.0 || gz < 0.0)
        return false;

    x = int(std::floor(gx));
    z = int(std::floor(gz));
    return x >= 0 && x < m_doc->sizeX() && z >= 0 && z < m_doc->sizeZ();
}

void LayerBuildPlanWidget::clampOffset()
{
    if (!m_doc || !m_doc->isLoaded())
        return;

    const QRectF area(contentRect());
    const double gw = m_doc->sizeX() * m_scale;
    const double gh = m_doc->sizeZ() * m_scale;

    if (gw <= area.width())
        m_offset.setX((area.width() - gw) * 0.5);
    else
        m_offset.setX(qBound(area.width() - gw - 28.0, m_offset.x(), 28.0));

    if (gh <= area.height())
        m_offset.setY((area.height() - gh) * 0.5);
    else
        m_offset.setY(qBound(area.height() - gh - 28.0, m_offset.y(), 28.0));
}

void LayerBuildPlanWidget::rebuildPaletteColors()
{
    m_paletteColors.clear();
    if (!m_doc || !m_doc->isLoaded())
        return;

    for (int i = 1; i < m_doc->paletteSize(); ++i)
    {
        const SchematicBlockState &state = m_doc->paletteEntry(i);
        if (state.isAir())
            continue;
        QColor color = VoxelViewWidget::blockColorOf(state.name);
        if (!color.isValid())
            color = QColor(150, 150, 150);
        m_paletteColors.insert(i, color);
    }
}

QColor LayerBuildPlanWidget::colorForPalette(int paletteIndex) const
{
    const auto it = m_paletteColors.constFind(paletteIndex);
    if (it != m_paletteColors.constEnd())
        return it.value();
    return QColor(150, 150, 150);
}

void LayerBuildPlanWidget::rebuildPaletteTextures()
{
    m_paletteTextures.clear();
    if (!m_loader || !m_loader->isValid() || !m_doc || !m_doc->isLoaded())
        return;

    for (int i = 1; i < m_doc->paletteSize(); ++i)
    {
        const SchematicBlockState &state = m_doc->paletteEntry(i);
        if (state.isAir())
            continue;

        // 32px 小图足够看清材质；平面图放大时再平滑铺开
        const QIcon icon = m_loader->blockIcon(state.name, 32);
        if (icon.isNull())
            continue;
        const QPixmap pixmap = icon.pixmap(32, 32);
        if (!pixmap.isNull())
            m_paletteTextures.insert(i, pixmap);
    }
}

int LayerBuildPlanWidget::tickStepFor(double scale)
{
    static const int kSteps[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
    for (int step : kSteps)
    {
        if (step * scale >= 26.0)
            return step;
    }
    return 10000;
}

void LayerBuildPlanWidget::clearHover()
{
    if (m_hoverX >= 0 || m_hoverZ >= 0)
    {
        m_hoverX = -1;
        m_hoverZ = -1;
        emit cellHovered(-1, -1, QString());
        update();
    }
}

void LayerBuildPlanWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_fitted)
        fitToView();
    else
    {
        clampOffset();
        update();
    }
}

void LayerBuildPlanWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool dark = ThemeManager::instance()
                      && ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
    const QColor panel = dark ? QColor(17, 19, 24) : QColor(250, 251, 253);
    const QColor panelBorder = dark ? QColor(255, 255, 255, 28) : QColor(15, 23, 42, 34);
    const QColor gridLine = dark ? QColor(255, 255, 255, 36) : QColor(15, 23, 42, 40);
    const QColor gridMajor = dark ? QColor(255, 255, 255, 78) : QColor(15, 23, 42, 80);
    const QColor textColor = readableColor(ThemeManager::instance()
                                               ? QColor(ThemeManager::instance()->currentTextColor())
                                               : QColor(220, 225, 235));

    // 卡片底
    const QRectF panelRect(rect());
    QPainterPath panelPath;
    panelPath.addRoundedRect(panelRect.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    painter.fillPath(panelPath, panel);
    painter.setPen(QPen(panelBorder, 1));
    painter.drawPath(panelPath);

    const QRectF area(contentRect());
    if (!m_doc || !m_doc->isLoaded() || m_doc->sizeX() <= 0 || m_doc->sizeZ() <= 0)
    {
        painter.setPen(textColor);
        QFont f = painter.font();
        f.setPointSizeF(10.5);
        painter.setFont(f);
        painter.drawText(area, Qt::AlignCenter | Qt::TextWordWrap,
                         m_placeholder.isEmpty() ? tr("暂时没有可显示的投影层") : m_placeholder);
        return;
    }

    const int sx = m_doc->sizeX();
    const int sz = m_doc->sizeZ();
    const int layerSize = m_doc->layerSize();
    const QPointF origin = area.topLeft() + m_offset;
    const QRectF gridRect(origin, QSizeF(sx * m_scale, sz * m_scale));

    // 可见格范围（含边缘半格）
    const int x0 = qBound(0, int(std::floor((area.left() - origin.x()) / m_scale)), sx - 1);
    const int x1 = qBound(0, int(std::floor((area.right() - origin.x()) / m_scale)), sx - 1);
    const int z0 = qBound(0, int(std::floor((area.top() - origin.y()) / m_scale)), sz - 1);
    const int z1 = qBound(0, int(std::floor((area.bottom() - origin.y()) / m_scale)), sz - 1);

    painter.save();
    painter.setClipRect(area);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // 整层底板：即使这一层有空位，也能看出范围
    painter.fillRect(gridRect, dark ? QColor(255, 255, 255, 8) : QColor(15, 23, 42, 7));

    // 缩得太小或可见格太多时不再逐格上色：一像素一格既看不清，
    // 又会在超大投影上把 UI 卡住。此时只保留网格和当前步骤的概括高亮。
    const qint64 visibleCells = qint64(x1 - x0 + 1) * qint64(z1 - z0 + 1);
    const bool hasTextures = !m_paletteTextures.isEmpty();
    const qint64 detailLimit = hasTextures ? 60000 : 180000;
    const bool detailed = (m_scale >= 2.0 && visibleCells <= detailLimit);

    // 方块格：有实例材质时铺真实贴图，没有对应贴图的方块仍回退到代表色
    if (detailed)
    {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const int index = m_layerY * layerSize + z * sx + x;
                const int paletteIndex = m_doc->paletteIndexAt(index);
                if (paletteIndex <= 0)
                    continue;

                const bool target = m_targetCells.contains(index);
                QRectF r = cellRect(x, z);
                if (m_scale >= 2.0)
                    r.adjust(0.25, 0.25, -0.25, -0.25);

                const auto texIt = m_paletteTextures.constFind(paletteIndex);
                if (texIt != m_paletteTextures.constEnd())
                {
                    // 非当前步骤的材质压暗，当前步骤保持原色，便于把注意力放在这一步
                    painter.setOpacity(target ? 1.0 : 0.62);
                    painter.drawPixmap(r, texIt.value(), QRectF(texIt.value().rect()));
                    painter.setOpacity(1.0);
                }
                else
                {
                    QColor color = colorForPalette(paletteIndex);
                    if (target)
                    {
                        color.setAlpha(245);
                    }
                    else
                    {
                        color = mixColor(color, panel, 0.52);
                        color.setAlpha(182);
                    }
                    painter.fillRect(r, color);
                }

                if (target)
                {
                    // 琥珀色薄纱让高亮不依赖方块本身的颜色
                    painter.fillRect(r, QColor(255, 193, 87, 44));
                }
            }
        }

        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    }

    // 每格都有网格线；缩得太小时按倍数抽稀，避免糊成一片
    const int lineStep = qMax(1, int(std::ceil(3.2 / m_scale)));
    const double startX = std::floor(x0 / double(lineStep)) * lineStep;
    const double startZ = std::floor(z0 / double(lineStep)) * lineStep;

    for (double x = startX; x <= x1 + 1.0; x += lineStep)
    {
        if (x < 0.0 || x > sx)
            continue;
        const bool major = (qFuzzyIsNull(std::fmod(x, 8.0)));
        painter.setPen(QPen(major ? gridMajor : gridLine, major ? 1.2 : 1.0));
        const double px = origin.x() + x * m_scale;
        painter.drawLine(QPointF(px, area.top()), QPointF(px, area.bottom()));
    }
    for (double z = startZ; z <= z1 + 1.0; z += lineStep)
    {
        if (z < 0.0 || z > sz)
            continue;
        const bool major = (qFuzzyIsNull(std::fmod(z, 8.0)));
        painter.setPen(QPen(major ? gridMajor : gridLine, major ? 1.2 : 1.0));
        const double pz = origin.y() + z * m_scale;
        painter.drawLine(QPointF(area.left(), pz), QPointF(area.right(), pz));
    }

    // 当前步骤的方块描边（画在网格之上）；格数太多时改画概括范围
    const double borderWidth = qBound(1.2, m_scale * 0.09, 2.4);
    painter.setRenderHint(QPainter::Antialiasing, false);
    if (detailed && m_targetCells.size() <= 40000)
    {
        for (int index : m_targetCells)
        {
            if (index < 0 || index >= m_doc->volume())
                continue;
            const int y = index / layerSize;
            if (y != m_layerY)
                continue;
            const int rem = index % layerSize;
            const int z = rem / sx;
            const int x = rem % sx;
            if (x < x0 || x > x1 || z < z0 || z > z1)
                continue;

            QRectF r = cellRect(x, z);
            r.adjust(0.9, 0.9, -0.9, -0.9);
            painter.setPen(QPen(kTargetAccent, borderWidth));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(r);
        }
    }
    else if (!m_targetBoundsCells.isNull())
    {
        const QRectF r(origin.x() + m_targetBoundsCells.left() * m_scale,
                       origin.y() + m_targetBoundsCells.top() * m_scale,
                       m_targetBoundsCells.width() * m_scale,
                       m_targetBoundsCells.height() * m_scale);
        painter.fillRect(r, QColor(255, 193, 87, 48));
        painter.setPen(QPen(kTargetAccent, qMax(1.4, borderWidth)));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r);
    }

    // 悬停格反馈
    if (m_hoverX >= 0 && m_hoverZ >= 0)
    {
        QRectF r = cellRect(m_hoverX, m_hoverZ);
        r.adjust(0.8, 0.8, -0.8, -0.8);
        QPen pen(dark ? QColor(255, 255, 255, 190) : QColor(20, 30, 45, 170), 1.4);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r);
    }

    painter.restore();

    // 四边刻度
    const int tickStep = tickStepFor(m_scale);
    drawRulers(painter, gridRect, x0, x1, z0, z1, tickStep);

    // 网格外框
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(gridMajor, 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(gridRect);

    // 完成等提示卡片
    if (!m_overlayMessage.isEmpty())
    {
        painter.setRenderHint(QPainter::Antialiasing, true);
        QFont f = painter.font();
        f.setPointSizeF(13.0);
        f.setBold(true);
        painter.setFont(f);
        const QFontMetricsF fm(f);
        const QString text = m_overlayMessage;
        const QRectF textRect = fm.boundingRect(QRectF(0, 0, area.width() - 48, 400),
                                                Qt::AlignCenter | Qt::TextWordWrap, text);
        QRectF card(0, 0, qMin(area.width() - 48.0, textRect.width() + 56.0),
                    textRect.height() + 40.0);
        card.moveCenter(area.center());
        QPainterPath path;
        path.addRoundedRect(card, 12, 12);
        painter.fillPath(path, dark ? QColor(20, 24, 30, 232) : QColor(255, 255, 255, 238));
        painter.setPen(QPen(kTargetAccent, 1.6));
        painter.drawPath(path);
        painter.setPen(kTargetAccent);
        painter.drawText(card.adjusted(18, 10, -18, -10),
                         Qt::AlignCenter | Qt::TextWordWrap, text);
    }
}

void LayerBuildPlanWidget::drawRulers(QPainter &painter, const QRectF &grid,
                                      int x0, int x1, int z0, int z1,
                                      int tickStep) const
{
    if (!m_doc || !m_doc->isLoaded())
        return;

    const QRectF area(contentRect());
    const bool dark = ThemeManager::instance()
                      && ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
    const QColor muted = dark ? QColor(148, 158, 174) : QColor(92, 103, 120);
    const QColor line = dark ? QColor(255, 255, 255, 52) : QColor(15, 23, 42, 58);

    QFont numberFont = painter.font();
    numberFont.setPointSizeF(8.2);
    numberFont.setBold(false);
    painter.setFont(numberFont);
    painter.setPen(muted);

    const double step = qMax(1, tickStep);

    // 顶部 / 底部：X 方向序号
    double lastTopX = -1e9;
    for (int x = x0; x <= x1; ++x)
    {
        if (!qFuzzyIsNull(std::fmod(double(x), step)) && x != x1)
            continue;
        const double cx = grid.left() + (x + 0.5) * m_scale;
        if (cx < area.left() - 2.0 || cx > area.right() + 2.0)
            continue;
        if (cx - lastTopX < 18.0)
            continue;
        lastTopX = cx;

        const QString label = QString::number(x + 1);
        painter.drawText(QRectF(cx - 22, area.top() - 22, 44, 16),
                         Qt::AlignHCenter | Qt::AlignVCenter, label);
        painter.drawText(QRectF(cx - 22, area.bottom() + 5, 44, 16),
                         Qt::AlignHCenter | Qt::AlignVCenter, label);

        painter.setPen(QPen(line, 1));
        painter.drawLine(QPointF(cx, area.top() - 4), QPointF(cx, area.top()));
        painter.drawLine(QPointF(cx, area.bottom()), QPointF(cx, area.bottom() + 4));
        painter.setPen(muted);
    }

    // 左侧 / 右侧：Z 方向序号
    double lastLeftZ = -1e9;
    for (int z = z0; z <= z1; ++z)
    {
        if (!qFuzzyIsNull(std::fmod(double(z), step)) && z != z1)
            continue;
        const double cz = grid.top() + (z + 0.5) * m_scale;
        if (cz < area.top() - 2.0 || cz > area.bottom() + 2.0)
            continue;
        if (cz - lastLeftZ < 18.0)
            continue;
        lastLeftZ = cz;

        const QString label = QString::number(z + 1);
        painter.drawText(QRectF(area.left() - 42, cz - 9, 36, 18),
                         Qt::AlignRight | Qt::AlignVCenter, label);
        painter.drawText(QRectF(area.right() + 6, cz - 9, 40, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, label);

        painter.setPen(QPen(line, 1));
        painter.drawLine(QPointF(area.left() - 4, cz), QPointF(area.left(), cz));
        painter.drawLine(QPointF(area.right(), cz), QPointF(area.right() + 4, cz));
        painter.setPen(muted);
    }

    // 四边的总长度：一眼知道这一层有多大
    QFont totalFont = painter.font();
    totalFont.setPointSizeF(9.0);
    totalFont.setBold(true);
    painter.setFont(totalFont);
    painter.setPen(dark ? QColor(206, 214, 226) : QColor(48, 58, 74));

    const QString xTotal = tr("X 轴 · %1 格").arg(m_doc->sizeX());
    const QString zTotal = tr("Z 轴 · %1 格").arg(m_doc->sizeZ());

    painter.drawText(QRectF(area.left(), 2, area.width(), 20),
                     Qt::AlignLeft | Qt::AlignVCenter, xTotal);
    painter.drawText(QRectF(area.left(), height() - 23, area.width(), 20),
                     Qt::AlignRight | Qt::AlignVCenter, xTotal);

    painter.save();
    painter.translate(13, area.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-area.height() * 0.5, -11, area.height(), 22),
                     Qt::AlignCenter, zTotal);
    painter.restore();

    painter.save();
    painter.translate(width() - 13, area.center().y());
    painter.rotate(90);
    painter.drawText(QRectF(-area.height() * 0.5, -11, area.height(), 22),
                     Qt::AlignCenter, zTotal);
    painter.restore();
}

void LayerBuildPlanWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_doc || !m_doc->isLoaded())
    {
        QWidget::wheelEvent(event);
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0)
        return;

    const QRectF area(contentRect());
    const QPointF pos = event->position();
    const QPointF origin = area.topLeft() + m_offset;
    const double gx = (pos.x() - origin.x()) / m_scale;
    const double gz = (pos.y() - origin.y()) / m_scale;

    const double factor = delta > 0 ? 1.18 : 1.0 / 1.18;
    m_scale = qBound(0.2, m_scale * factor, 96.0);
    m_offset = QPointF(pos.x() - area.left() - gx * m_scale,
                       pos.y() - area.top() - gz * m_scale);
    m_fitted = false;
    clampOffset();
    update();
    event->accept();
}

void LayerBuildPlanWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && contentRect().contains(event->pos()))
    {
        m_panning = true;
        m_pressPos = event->pos();
        m_offsetAtPress = m_offset;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void LayerBuildPlanWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning)
    {
        m_offset = m_offsetAtPress + QPointF(event->pos() - m_pressPos);
        clampOffset();
        update();
        return;
    }

    int x = -1;
    int z = -1;
    if (!m_doc || !m_doc->isLoaded() || !cellAt(event->pos(), x, z))
    {
        clearHover();
        return;
    }

    if (x == m_hoverX && z == m_hoverZ)
        return;

    m_hoverX = x;
    m_hoverZ = z;
    const int index = m_layerY * m_doc->layerSize() + z * m_doc->sizeX() + x;
    const int paletteIndex = m_doc->paletteIndexAt(index);
    const QString blockId = paletteIndex > 0 ? m_doc->paletteEntry(paletteIndex).name : QString();
    emit cellHovered(x, z, blockId);
    update();
}

void LayerBuildPlanWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && event->button() == Qt::LeftButton)
    {
        m_panning = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void LayerBuildPlanWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        fitToView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void LayerBuildPlanWidget::leaveEvent(QEvent *event)
{
    clearHover();
    QWidget::leaveEvent(event);
}

void LayerBuildPlanWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_0:
    case Qt::Key_Home:
        fitToView();
        event->accept();
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
    {
        const QRectF area(contentRect());
        const QPointF center = area.center();
        const double gx = (center.x() - area.left() - m_offset.x()) / m_scale;
        const double gz = (center.y() - area.top() - m_offset.y()) / m_scale;
        m_scale = qBound(0.2, m_scale * 1.2, 96.0);
        m_offset = QPointF(center.x() - area.left() - gx * m_scale,
                           center.y() - area.top() - gz * m_scale);
        m_fitted = false;
        clampOffset();
        update();
        event->accept();
        return;
    }
    case Qt::Key_Minus:
    {
        const QRectF area(contentRect());
        const QPointF center = area.center();
        const double gx = (center.x() - area.left() - m_offset.x()) / m_scale;
        const double gz = (center.y() - area.top() - m_offset.y()) / m_scale;
        m_scale = qBound(0.2, m_scale / 1.2, 96.0);
        m_offset = QPointF(center.x() - area.left() - gx * m_scale,
                           center.y() - area.top() - gz * m_scale);
        m_fitted = false;
        clampOffset();
        update();
        event->accept();
        return;
    }
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}
