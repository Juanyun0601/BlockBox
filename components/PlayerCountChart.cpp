/**
 * @file   PlayerCountChart.cpp
 * @brief  人数统计折线图实现
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "PlayerCountChart.h"

#include <QDateTime>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>

#include <cmath>
#include <cstdlib>

#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

namespace {

/** 纵轴刻度取整：把 raw 抬到 1 / 2 / 5 × 10ⁿ */
double niceStep(double raw)
{
    if (raw <= 0.0)
        return 1.0;
    const double exponent = std::floor(std::log10(raw));
    const double base = std::pow(10.0, exponent);
    const double normalized = raw / base;
    double nice = 10.0;
    if (normalized <= 1.0)
        nice = 1.0;
    else if (normalized <= 2.0)
        nice = 2.0;
    else if (normalized <= 5.0)
        nice = 5.0;
    return nice * base;
}

/** 时间轴刻度：跨度超过一天半就只标日期，否则标到分钟 */
QString formatTick(qint64 second, qint64 spanSeconds)
{
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(second).toLocalTime();
    return spanSeconds > 36 * 3600 ? dt.toString(QStringLiteral("MM-dd"))
                                   : dt.toString(QStringLiteral("HH:mm"));
}

} // namespace

PlayerCountChart::PlayerCountChart(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground, false);
    m_emptyText = tr("暂时还没有人数记录");

    // 主题切换后颜色要跟着变：颜色是在 paintEvent 里按当前令牌解析的，重绘即可
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this,
            [this]() { update(); });
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](ThemeManager::ThemeType) { update(); });
}

void PlayerCountChart::setPoints(const QVector<Point> &points, qint64 fromSec, qint64 toSec)
{
    m_points = points;
    m_from = fromSec;
    m_to = toSec;
    if (m_points.isEmpty())
    {
        m_from = 0;
        m_to = 0;
    }
    else
    {
        if (m_from <= 0)
            m_from = m_points.first().t;
        if (m_to <= 0)
            m_to = m_points.last().t;
        if (m_to <= m_from)
            m_to = m_from + 60;
    }
    m_hover = -1;
    update();
}

void PlayerCountChart::clear()
{
    m_points.clear();
    m_from = 0;
    m_to = 0;
    m_hover = -1;
    update();
}

void PlayerCountChart::setEmptyText(const QString &text)
{
    m_emptyText = text;
    update();
}

void PlayerCountChart::setCapacity(int capacity)
{
    m_capacity = qMax(0, capacity);
    update();
}

QRectF PlayerCountChart::plotRect() const
{
    const double left = 40.0;
    const double right = 12.0;
    const double top = 14.0;
    const double bottom = 22.0;
    return QRectF(QPointF(left, top),
                  QPointF(qMax(left + 20.0, width() - right),
                          qMax(top + 20.0, height() - bottom)));
}

void PlayerCountChart::updateHover(const QPoint &pos)
{
    if (m_points.size() < 2)
    {
        if (m_hover != -1)
        {
            m_hover = -1;
            update();
        }
        return;
    }

    const QRectF plot = plotRect();
    if (!plot.contains(pos))
    {
        if (m_hover != -1)
        {
            m_hover = -1;
            update();
        }
        return;
    }

    const double ratio = qBound(0.0, (pos.x() - plot.left()) / qMax(1.0, plot.width()), 1.0);
    const qint64 target = m_from + qRound64(ratio * double(m_to - m_from));

    int best = 0;
    qint64 bestDelta = std::llabs(m_points.first().t - target);
    for (int i = 1; i < m_points.size(); ++i)
    {
        const qint64 delta = std::llabs(m_points.at(i).t - target);
        if (delta < bestDelta)
        {
            bestDelta = delta;
            best = i;
        }
    }
    if (m_hover != best)
    {
        m_hover = best;
        update();
    }
}

void PlayerCountChart::mouseMoveEvent(QMouseEvent *event)
{
    updateHover(event->pos());
    QWidget::mouseMoveEvent(event);
}

void PlayerCountChart::leaveEvent(QEvent *event)
{
    if (m_hover != -1)
    {
        m_hover = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void PlayerCountChart::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor textColor(StyleKit::resolve(QStringLiteral("@TEXT_TERTIARY@")));
    const QColor gridColor(StyleKit::resolve(QStringLiteral("@BORDER@")));
    const QColor accentColor(StyleKit::resolve(QStringLiteral("@TEXT_ACCENT@")));
    const QRectF plot = plotRect();

    QFont tickFont = font();
    tickFont.setPixelSize(10);
    const QFontMetricsF tickMetrics(tickFont);

    if (m_points.isEmpty())
    {
        painter.setFont(tickFont);
        QColor muted = textColor;
        muted.setAlpha(170);
        painter.setPen(muted);
        painter.drawText(QRectF(rect()), Qt::AlignCenter, m_emptyText);
        return;
    }

    // ── 纵轴范围：峰值再留一点余量，上限线在合理范围内时也带上 ──
    double peak = 0.0;
    for (const Point &point : m_points)
        peak = qMax(peak, point.online);
    double target = qMax(2.0, peak * 1.12);
    if (m_capacity > 0 && m_capacity <= peak * 3.0 + 8.0)
        target = qMax(target, double(m_capacity) * 1.04);
    const double step = niceStep(target / 3.0);
    const double yMax = qMax(step, std::ceil(target / step) * step);

    const qint64 span = qMax<qint64>(1, m_to - m_from);
    auto xOf = [&plot, this](qint64 t) {
        const double ratio =
            qBound(0.0, double(t - m_from) / double(qMax<qint64>(1, m_to - m_from)), 1.0);
        return plot.left() + ratio * plot.width();
    };
    auto yOf = [&plot, yMax](double value) {
        const double ratio = qBound(0.0, value / yMax, 1.0);
        return plot.bottom() - ratio * plot.height();
    };

    // ── 横向网格 + 纵轴刻度 ──
    painter.setFont(tickFont);
    const int gridLines = 3;
    for (int i = 0; i < gridLines; ++i)
    {
        const double value = yMax * double(i) / double(gridLines - 1);
        const double y = yOf(value);
        QPen gridPen(gridColor);
        gridPen.setWidthF(1.0);
        gridPen.setStyle(i == 0 ? Qt::SolidLine : Qt::DotLine);
        QColor gridAlpha = gridColor;
        gridAlpha.setAlpha(i == 0 ? 120 : 70);
        gridPen.setColor(gridAlpha);
        painter.setPen(gridPen);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));

        const QString label = QString::number(qRound(value));
        painter.setPen(textColor);
        painter.drawText(QRectF(0, y - 7, plot.left() - 8, 14),
                         Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // ── 人数上限参考线（虚线，只在和实际人数同一量级时画） ──
    if (m_capacity > 0 && m_capacity <= yMax)
    {
        const double y = yOf(double(m_capacity));
        QPen capPen(accentColor);
        capPen.setWidthF(1.0);
        capPen.setStyle(Qt::DashLine);
        QColor capColor = accentColor;
        capColor.setAlpha(120);
        capPen.setColor(capColor);
        painter.setPen(capPen);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));

        const QString capText = tr("上限 %1").arg(m_capacity);
        painter.setPen(textColor);
        painter.drawText(QRectF(plot.right() - tickMetrics.horizontalAdvance(capText) - 4,
                                y - 15, tickMetrics.horizontalAdvance(capText) + 4, 13),
                         Qt::AlignRight | Qt::AlignVCenter, capText);
    }

    // ── 折线 + 面积 ──
    QPolygonF line;
    line.reserve(m_points.size());
    for (const Point &point : m_points)
        line.append(QPointF(xOf(point.t), yOf(point.online)));

    if (line.size() >= 2)
    {
        QPainterPath area;
        area.moveTo(line.first());
        for (int i = 1; i < line.size(); ++i)
            area.lineTo(line.at(i));
        area.lineTo(line.last().x(), plot.bottom());
        area.lineTo(line.first().x(), plot.bottom());
        area.closeSubpath();

        QLinearGradient fill(0, plot.top(), 0, plot.bottom());
        QColor start = accentColor;
        start.setAlpha(64);
        QColor end = accentColor;
        end.setAlpha(0);
        fill.setColorAt(0.0, start);
        fill.setColorAt(1.0, end);
        painter.setPen(Qt::NoPen);
        painter.fillPath(area, fill);
    }

    QPen linePen(accentColor);
    linePen.setWidthF(2.2);
    linePen.setCapStyle(Qt::RoundCap);
    linePen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(linePen);
    if (line.size() == 1)
    {
        painter.setBrush(accentColor);
        painter.drawEllipse(line.first(), 3.0, 3.0);
    }
    else
    {
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(line);
    }

    // 最后一个点画一个实心圆，表示「现在」
    if (!line.isEmpty())
    {
        painter.setPen(QPen(QColor(StyleKit::resolve(QStringLiteral("@BG_CARD@"))), 2.0));
        painter.setBrush(accentColor);
        painter.drawEllipse(line.last(), 3.6, 3.6);
    }

    // ── 时间轴刻度：起点 / 中点 / 终点 ──
    if (span >= 60)
    {
        painter.setPen(textColor);
        struct Tick { qint64 t; Qt::Alignment align; };
        const Tick ticks[] = {
            { m_from, Qt::AlignLeft },
            { m_from + span / 2, Qt::AlignHCenter },
            { m_to, Qt::AlignRight },
        };
        for (const Tick &tick : ticks)
        {
            const QString label = formatTick(tick.t, span);
            const double advance = tickMetrics.horizontalAdvance(label);
            double x = xOf(tick.t);
            if (tick.align == Qt::AlignHCenter)
                x -= advance / 2.0;
            else if (tick.align == Qt::AlignRight)
                x -= advance;
            // 窄窗口下标签可能比绘图区还宽：夹在绘图区内，避免画到卡片外面
            x = qBound(plot.left(), x, qMax(plot.left(), plot.right() - advance - 2.0));
            const QRectF box(x, plot.bottom() + 5, advance + 2.0, 13);
            painter.drawText(box, tick.align | Qt::AlignVCenter, label);
        }
    }

    // ── 悬停：竖线 + 高亮点 + 数据气泡 ──
    if (m_hover >= 0 && m_hover < m_points.size())
    {
        const Point &point = m_points.at(m_hover);
        const double x = xOf(point.t);
        const double y = yOf(point.online);

        QPen cursorPen(accentColor);
        cursorPen.setWidthF(1.0);
        cursorPen.setStyle(Qt::DashLine);
        QColor cursorColor = accentColor;
        cursorColor.setAlpha(140);
        cursorPen.setColor(cursorColor);
        painter.setPen(cursorPen);
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));

        painter.setPen(QPen(QColor(StyleKit::resolve(QStringLiteral("@BG_CARD@"))), 2.0));
        painter.setBrush(accentColor);
        painter.drawEllipse(QPointF(x, y), 4.0, 4.0);

        const QDateTime dt = QDateTime::fromSecsSinceEpoch(point.t).toLocalTime();
        const QString text = QStringLiteral("%1 · %2 人")
                                 .arg(dt.toString(QStringLiteral("MM-dd HH:mm")),
                                      QString::number(qRound(point.online)));
        const double textWidth = tickMetrics.horizontalAdvance(text) + 14.0;
        double boxX = x + 8.0;
        if (boxX + textWidth > plot.right())
            boxX = x - 8.0 - textWidth;
        boxX = qBound(plot.left(), boxX, qMax(plot.left(), plot.right() - textWidth));
        const double boxY = qBound(plot.top(), y - 30.0, plot.bottom() - 22.0);

        const QRectF bubble(boxX, boxY, textWidth, 20.0);
        painter.setPen(QPen(QColor(StyleKit::resolve(QStringLiteral("@BORDER@"))), 1.0));
        painter.setBrush(QColor(StyleKit::resolve(QStringLiteral("@BG_CARD@"))));
        painter.drawRoundedRect(bubble, 5.0, 5.0);
        painter.setPen(textColor);
        painter.drawText(bubble, Qt::AlignCenter, text);
    }
}
