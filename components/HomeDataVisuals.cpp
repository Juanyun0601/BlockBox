/**
 * @file   HomeDataVisuals.cpp
 * @brief  首页可视化小组件实现
 * @author BlockBox Team
 * @date   2026-08-18
 */

#include "HomeDataVisuals.h"

#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

// ============================================================================
// RingGauge
// ============================================================================

RingGauge::RingGauge(QWidget *parent)
    : QWidget(parent)
{
  setAttribute(Qt::WA_StyledBackground, false);
  m_timer = new QTimer(this);
  m_timer->setInterval(16);
  connect(m_timer, &QTimer::timeout, this, [this]() {
    const int step = qMax(1, qAbs(m_target - m_current) / 8);
    if (m_current < m_target)
      m_current = qMin(m_target, m_current + step);
    else if (m_current > m_target)
      m_current = qMax(m_target, m_current - step);
    if (m_current == m_target)
      m_timer->stop();
    update();
  });
}

void RingGauge::setValue(int percent)
{
  m_target = qBound(0, percent, 100);
  if (m_animated)
  {
    if (!m_timer->isActive())
      m_timer->start();
  }
  else
  {
    m_current = m_target;
    update();
  }
}

void RingGauge::setColor(const QColor &color)
{
  m_color = color;
  update();
}

void RingGauge::setTrackColor(const QColor &color)
{
  m_trackColor = color;
  update();
}

void RingGauge::setCenterText(const QString &text)
{
  m_centerText = text;
  update();
}

void RingGauge::setThickness(int thickness)
{
  m_thickness = qMax(2, thickness);
  update();
}

void RingGauge::setAnimated(bool animated)
{
  m_animated = animated;
  if (!m_animated)
  {
    m_timer->stop();
    m_current = m_target;
    update();
  }
}

void RingGauge::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event)
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const int side = qMin(width(), height());
  const QRectF r = QRectF((width() - side) / 2.0 + m_thickness / 2.0,
                          (height() - side) / 2.0 + m_thickness / 2.0,
                          side - m_thickness,
                          side - m_thickness);

  // 轨道
  p.setPen(QPen(m_trackColor, m_thickness, Qt::SolidLine, Qt::RoundCap));
  p.drawArc(r, 0, 360 * 16);

  // 进度弧（从 12 点方向顺时针）
  if (m_current > 0)
  {
    p.setPen(QPen(m_color, m_thickness, Qt::SolidLine, Qt::RoundCap));
    const int span = -360 * 16 * m_current / 100;
    p.drawArc(r, 90 * 16, span);
  }

  // 中心文字
  if (!m_centerText.isEmpty())
  {
    QFont f = p.font();
    f.setPixelSize(qMax(10, side / 5));
    f.setBold(true);
    p.setFont(f);
    p.setPen(m_color);
    p.drawText(QRect(0, 0, width(), height()), Qt::AlignCenter, m_centerText);
  }
}

// ============================================================================
// AnalogClock
// ============================================================================

AnalogClock::AnalogClock(QWidget *parent)
    : QWidget(parent)
{
  setAttribute(Qt::WA_StyledBackground, false);
  m_time = QTime::currentTime();
  m_timer = new QTimer(this);
  m_timer->setInterval(1000);
  connect(m_timer, &QTimer::timeout, this, [this]() {
    m_time = QTime::currentTime();
    update();
  });
  m_timer->start();
}

void AnalogClock::setTime(const QTime &time)
{
  m_time = time;
  update();
}

void AnalogClock::setForeground(const QColor &color)
{
  m_foreground = color;
  update();
}

void AnalogClock::setHandColor(const QColor &color)
{
  m_handColor = color;
  update();
}

void AnalogClock::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event)
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const int side = qMin(width(), height());
  const QPointF center(width() / 2.0, height() / 2.0);
  const qreal radius = side / 2.0 - 4;

  // 表盘
  p.setPen(QPen(m_foreground, 1.4));
  p.setBrush(QColor(255, 255, 255, 26));
  p.drawEllipse(center, radius, radius);

  // 刻度（12/3/6/9 加粗）
  for (int i = 0; i < 12; ++i)
  {
    const qreal ang = i * 30.0 - 90.0;
    const qreal rad = qDegreesToRadians(ang);
    const bool major = (i % 3 == 0);
    const qreal r1 = radius - (major ? 8 : 4);
    const qreal r2 = radius - 2;
    const QPointF a = center + QPointF(qCos(rad) * r1, qSin(rad) * r1);
    const QPointF b = center + QPointF(qCos(rad) * r2, qSin(rad) * r2);
    p.setPen(QPen(m_foreground, major ? 2.4 : 1.2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(a, b);
  }

  // 时针
  const qreal hourAng = (m_time.hour() % 12 + m_time.minute() / 60.0) * 30.0 - 90.0;
  const qreal hourRad = qDegreesToRadians(hourAng);
  p.setPen(QPen(m_handColor, 4.0, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(center, center + QPointF(qCos(hourRad) * radius * 0.5, qSin(hourRad) * radius * 0.5));

  // 分针
  const qreal minAng = (m_time.minute() + m_time.second() / 60.0) * 6.0 - 90.0;
  const qreal minRad = qDegreesToRadians(minAng);
  p.setPen(QPen(m_handColor, 2.6, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(center, center + QPointF(qCos(minRad) * radius * 0.72, qSin(minRad) * radius * 0.72));

  // 秒针
  const qreal secAng = m_time.second() * 6.0 - 90.0;
  const qreal secRad = qDegreesToRadians(secAng);
  p.setPen(QPen(QColor("#EF4444"), 1.4, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(center, center + QPointF(qCos(secRad) * radius * 0.82, qSin(secRad) * radius * 0.82));

  // 中心点
  p.setBrush(m_handColor);
  p.setPen(Qt::NoPen);
  p.drawEllipse(center, 3.2, 3.2);
}

// ============================================================================
// SegmentedBar
// ============================================================================

SegmentedBar::SegmentedBar(QWidget *parent)
    : QWidget(parent)
{
  setAttribute(Qt::WA_StyledBackground, false);
}

void SegmentedBar::setSegments(const QList<HomeSegment> &segments)
{
  m_segments = segments;
  update();
}

void SegmentedBar::setTrackColor(const QColor &color)
{
  m_trackColor = color;
  update();
}

void SegmentedBar::setBarHeight(int height)
{
  m_height = qMax(4, height);
  update();
}

void SegmentedBar::setShowPercent(bool show)
{
  m_showPercent = show;
  update();
}

void SegmentedBar::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event)
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const QRectF barRect(0, (height() - m_height) / 2.0, width(), m_height);
  const qreal radius = m_height / 2.0;

  QPainterPath clip;
  clip.addRoundedRect(barRect, radius, radius);
  p.save();
  p.setClipPath(clip);

  // 轨道
  p.setPen(Qt::NoPen);
  p.setBrush(m_trackColor);
  p.drawRect(barRect);

  // 分段
  qreal x = barRect.left();
  for (const HomeSegment &seg : m_segments)
  {
    if (seg.fraction <= 0.0)
      continue;
    const qreal w = barRect.width() * qMin(1.0, seg.fraction);
    p.setBrush(seg.color);
    p.drawRect(QRectF(x, barRect.top(), w, barRect.height()));
    x += w;
  }
  p.restore();

  // 外框
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(m_trackColor, 1));
  p.drawRoundedRect(barRect, radius, radius);

  // 百分比文字
  if (m_showPercent && m_segments.size() == 1)
  {
    QFont f = p.font();
    f.setPixelSize(qMax(9, m_height));
    p.setFont(f);
    p.setPen(m_segments.first().color);
    p.drawText(barRect, Qt::AlignCenter,
               QString::number(qRound(m_segments.first().fraction * 100.0)) + "%");
  }
}
