/**
 * @file   TaskProgressWidgets.cpp
 * @brief  任务详情页现代化控件实现
 * @author BlockBox Team
 * @date   2026-08-15
 */
#include "TaskProgressWidgets.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

/* ============================================================
 * RingProgressWidget
 * ============================================================ */
RingProgressWidget::RingProgressWidget(QWidget *parent)
    : QWidget(parent)
    , m_progress(0.0)
    , m_accentColor(0x10, 0xB9, 0x81)
    , m_textColor(0x0F, 0x17, 0x2A)
    , m_caption(QStringLiteral("进度"))
{
    setMinimumWidth(110);
    setMinimumHeight(110);
}

void RingProgressWidget::setProgress(double progress)
{
    m_progress = qBound(0.0, progress, 100.0);
    update();
}

double RingProgressWidget::progress() const
{
    return m_progress;
}

void RingProgressWidget::setAccentColor(const QColor &color)
{
    m_accentColor = color;
    update();
}

void RingProgressWidget::setCaption(const QString &caption)
{
    m_caption = caption;
    update();
}

void RingProgressWidget::setTextColor(const QColor &color)
{
    m_textColor = color;
    update();
}

void RingProgressWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int side = qMin(width(), height());
    const QRectF base(0, 0, side, side);
    QRectF rect = base.adjusted(side * 0.08, side * 0.08, -side * 0.08, -side * 0.08);
    rect.translate((width() - side) / 2.0, (height() - side) / 2.0);

    const double penWidth = side * 0.085;
    const double radius = rect.width() / 2.0;
    const QPointF center = rect.center();

    // 轨道
    QPen trackPen(QColor(15, 23, 42, 24), penWidth);
    trackPen.setCapStyle(Qt::RoundCap);
    painter.setPen(trackPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(rect);

    // 渐变填充弧
    if (m_progress > 0.0)
    {
        QConicalGradient grad(center, -90.0);
        grad.setColorAt(0.0, m_accentColor.lighter(125));
        grad.setColorAt(0.55, m_accentColor);
        grad.setColorAt(1.0, QColor(0x06, 0xB6, 0xD4));

        QPen fillPen(grad, penWidth);
        fillPen.setCapStyle(Qt::RoundCap);
        painter.setPen(fillPen);
        painter.drawArc(rect, 90 * 16, static_cast<int>(-m_progress * 3.6 * 16));
    }

    // 中心文字
    QFont pctFont = font();
    pctFont.setPixelSize(static_cast<int>(side * 0.21));
    pctFont.setBold(true);
    painter.setFont(pctFont);
    painter.setPen(m_textColor);
    painter.drawText(rect.adjusted(0, -side * 0.03, 0, -side * 0.03), Qt::AlignCenter,
                     QString::number(qRound(m_progress)) + QStringLiteral("%"));

    QFont capFont = font();
    capFont.setPixelSize(static_cast<int>(side * 0.075));
    QColor capColor = m_textColor;
    capColor.setAlpha(150);
    painter.setFont(capFont);
    painter.setPen(capColor);
    painter.drawText(rect.adjusted(0, side * 0.16, 0, side * 0.16), Qt::AlignCenter, m_caption);
}

/* ============================================================
 * SpeedChartWidget
 * ============================================================ */
SpeedChartWidget::SpeedChartWidget(QWidget *parent)
    : QWidget(parent)
    , m_accentColor(0x10, 0xB9, 0x81)
    , m_textColor(0x0F, 0x17, 0x2A)
{
    setMinimumHeight(120);
}

void SpeedChartWidget::addSample(double mbps)
{
    m_samples.append(qMax(0.0, mbps));
    if (m_samples.size() > 60)
    {
        m_samples.remove(0, m_samples.size() - 60);
    }
    update();
}

void SpeedChartWidget::clearSamples()
{
    m_samples.clear();
    update();
}

void SpeedChartWidget::setAccentColor(const QColor &color)
{
    m_accentColor = color;
    update();
}

void SpeedChartWidget::setTextColor(const QColor &color)
{
    m_textColor = color;
    update();
}

void SpeedChartWidget::setPeakHintText(const QString &text)
{
    m_peakHint = text;
    update();
}

void SpeedChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int W = width();
    const int H = height();
    const double padX = 4.0;
    const double padTop = 16.0;
    const double padBottom = 6.0;

    // 基线
    painter.setPen(QPen(QColor(15, 23, 42, 14), 1));
    painter.drawLine(QPointF(padX, H - padBottom), QPointF(W - padX, H - padBottom));

    if (m_samples.size() < 2)
    {
        // 空态提示
        QFont f = font();
        f.setPixelSize(11);
        painter.setFont(f);
        QColor c = m_textColor;
        c.setAlpha(120);
        painter.setPen(c);
        painter.drawText(rect(), Qt::AlignCenter, tr("等待数据传输…"));
        return;
    }

    // 动态 Y 轴范围
    double maxV = 0.0;
    for (double v : m_samples) maxV = qMax(maxV, v);
    maxV = qMax(1.0, maxV * 1.15);

    const double plotH = H - padTop - padBottom;
    const int n = m_samples.size();
    QPolygonF poly;
    poly.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        double x = padX + (static_cast<double>(i) / (60.0 - 1.0)) * (W - padX * 2.0);
        double y = padTop + plotH - (m_samples.at(i) / maxV) * plotH;
        poly << QPointF(x, y);
    }

    // 面积填充
    QPainterPath areaPath;
    areaPath.moveTo(poly.first());
    for (int i = 1; i < poly.size(); ++i) areaPath.lineTo(poly.at(i));
    areaPath.lineTo(poly.last().x(), H - padBottom);
    areaPath.lineTo(poly.first().x(), H - padBottom);
    areaPath.closeSubpath();

    QLinearGradient fillGrad(0, padTop, 0, H - padBottom);
    QColor fillStart = m_accentColor;
    fillStart.setAlpha(70);
    QColor fillEnd = m_accentColor;
    fillEnd.setAlpha(0);
    fillGrad.setColorAt(0.0, fillStart);
    fillGrad.setColorAt(1.0, fillEnd);
    painter.setPen(Qt::NoPen);
    painter.fillPath(areaPath, fillGrad);

    // 折线
    QPen linePen(m_accentColor, 2.2);
    linePen.setCapStyle(Qt::RoundCap);
    linePen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(linePen);
    QPainterPath linePath;
    linePath.moveTo(poly.first());
    for (int i = 1; i < poly.size(); ++i) linePath.lineTo(poly.at(i));
    painter.drawPath(linePath);

    // 峰值提示
    if (!m_peakHint.isEmpty())
    {
        QFont hf = font();
        hf.setPixelSize(11);
        painter.setFont(hf);
        QColor hc = m_textColor;
        hc.setAlpha(150);
        painter.setPen(hc);
        painter.drawText(QRectF(padX, 2, W - padX * 2.0, 14), Qt::AlignRight | Qt::AlignVCenter, m_peakHint);
    }
}

/* ============================================================
 * StageStepperWidget
 * ============================================================ */
StageStepperWidget::StageStepperWidget(QWidget *parent)
    : QWidget(parent)
    , m_currentIndex(-1)
    , m_progress(0.0)
    , m_accentColor(0x10, 0xB9, 0x81)
    , m_textColor(0x0F, 0x17, 0x2A)
    , m_mutedColor(0x94, 0xA3, 0xB8)
{
    setMinimumHeight(56);
}

void StageStepperWidget::setStages(const QStringList &stages)
{
    m_stages = stages;
    update();
}

void StageStepperWidget::setCurrentStage(int index)
{
    m_currentIndex = index;
    update();
}

void StageStepperWidget::setProgress(double progress)
{
    m_progress = qBound(0.0, progress, 100.0);
    update();
}

void StageStepperWidget::setAccentColor(const QColor &color)
{
    m_accentColor = color;
    update();
}

void StageStepperWidget::setTextColor(const QColor &color)
{
    m_textColor = color;
    update();
}

void StageStepperWidget::setMutedColor(const QColor &color)
{
    m_mutedColor = color;
    update();
}

void StageStepperWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int count = m_stages.size();
    if (count <= 0) return;

    const int H = height();
    const int dotY = static_cast<int>(H * 0.30);
    const int dotR = 7;

    // 连接线（轨道）
    const double leftX = 18.0;
    const double rightX = width() - 18.0;
    painter.setPen(QPen(QColor(15, 23, 42, 18), 3));
    painter.drawLine(QPointF(leftX, dotY), QPointF(rightX, dotY));

    // 连接线（已填充）
    if (m_progress > 0.0)
    {
        double fillEnd = leftX + (rightX - leftX) * (m_progress / 100.0);
        QPen fillPen(m_accentColor, 3);
        fillPen.setCapStyle(Qt::RoundCap);
        painter.setPen(fillPen);
        painter.drawLine(QPointF(leftX, dotY), QPointF(fillEnd, dotY));
    }

    // 圆点 + 名称
    const double stepX = count > 1 ? (rightX - leftX) / (count - 1) : 0.0;
    QFont nameFont = font();
    nameFont.setPixelSize(11);
    nameFont.setBold(false);

    for (int i = 0; i < count; ++i)
    {
        const double cx = leftX + stepX * i;
        const QPointF c(cx, dotY);

        QColor dotColor = QColor(255, 255, 255);
        QColor borderColor = QColor(15, 23, 42, 26);
        bool filled = (m_currentIndex >= 0 && i < m_currentIndex);
        bool current = (i == m_currentIndex);

        // 圆点
        QPen dotPen(borderColor, 1.5);
        painter.setPen(dotPen);
        painter.setBrush(dotColor);
        painter.drawEllipse(c, dotR, dotR);

        if (filled)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_accentColor);
            painter.drawEllipse(c, dotR, dotR);
            // 对勾
            QPen checkPen(QColor(255, 255, 255), 1.6);
            checkPen.setCapStyle(Qt::RoundCap);
            checkPen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(checkPen);
            painter.drawLine(QPointF(cx - 3, dotY), QPointF(cx - 0.5, dotY + 2.6));
            painter.drawLine(QPointF(cx - 0.5, dotY + 2.6), QPointF(cx + 3.5, dotY - 2.4));
        }
        else if (current)
        {
            // 当前阶段：彩色描边 + 外圈光晕
            QPen glowPen(m_accentColor, 1.5);
            painter.setPen(glowPen);
            painter.setBrush(QColor(255, 255, 255));
            painter.drawEllipse(c, dotR, dotR);
            painter.setPen(QPen(m_accentColor, 1));
            QColor halo = m_accentColor;
            halo.setAlpha(70);
            painter.setBrush(halo);
            painter.drawEllipse(c, dotR + 5, dotR + 5);
            // 阶段序号
            QFont idxFont = font();
            idxFont.setPixelSize(10);
            idxFont.setBold(true);
            painter.setFont(idxFont);
            painter.setPen(m_accentColor);
            painter.drawText(QRectF(cx - dotR, dotY - dotR, dotR * 2, dotR * 2), Qt::AlignCenter,
                             QString::number(i + 1));
        }
        else
        {
            // 阶段序号
            QFont idxFont = font();
            idxFont.setPixelSize(10);
            painter.setFont(idxFont);
            QColor idxColor = m_mutedColor;
            painter.setPen(idxColor);
            painter.drawText(QRectF(cx - dotR, dotY - dotR, dotR * 2, dotR * 2), Qt::AlignCenter,
                             QString::number(i + 1));
        }

        // 名称
        painter.setFont(nameFont);
        QColor nameColor = m_textColor;
        nameColor.setAlpha(current ? 255 : 160);
        painter.setPen(nameColor);
        painter.drawText(QRectF(cx - 44, dotY + 10, 88, 18), Qt::AlignHCenter | Qt::AlignTop,
                         m_stages.at(i));
    }
}
