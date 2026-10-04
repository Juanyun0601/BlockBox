/**
 * @file   HomeDataVisuals.h
 * @brief  首页可视化小组件：环形仪表（RingGauge）、模拟时钟（AnalogClock）、分段分布条（SegmentedBar）
 * @author BlockBox Team
 * @date   2026-08-18
 */

#pragma once

#include <QColor>
#include <QList>
#include <QTime>
#include <QWidget>

class QTimer;

// ============================================================================
// RingGauge — 环形仪表（带动画），用于磁盘/内存/下载等百分比展示
// ============================================================================
class RingGauge : public QWidget
{
  Q_OBJECT

public:
  explicit RingGauge(QWidget *parent = nullptr);

  void setValue(int percent);                    // 0~100
  void setColor(const QColor &color);
  void setTrackColor(const QColor &color);
  void setCenterText(const QString &text);       // 空则不显示
  void setThickness(int thickness);
  void setAnimated(bool animated);
  int value() const { return m_target; }

  QSize sizeHint() const override { return QSize(96, 96); }

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  int m_target = 0;
  int m_current = 0;
  QColor m_color = QColor("#10B981");
  QColor m_trackColor = QColor(226, 232, 240, 120);
  QString m_centerText;
  int m_thickness = 10;
  bool m_animated = true;
  QTimer *m_timer = nullptr;
};

// ============================================================================
// AnalogClock — 模拟时钟表盘（指针随时间走动）
// ============================================================================
class AnalogClock : public QWidget
{
  Q_OBJECT

public:
  explicit AnalogClock(QWidget *parent = nullptr);

  void setTime(const QTime &time);
  void setForeground(const QColor &color);
  void setHandColor(const QColor &color);

  QSize sizeHint() const override { return QSize(110, 110); }

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QTime m_time;
  QColor m_foreground = QColor("#334155");
  QColor m_handColor = QColor("#334155");
  QTimer *m_timer = nullptr;
};

// ============================================================================
// SegmentedBar — 横向分段分布条（如加载器占比、磁盘占用）
// ============================================================================
struct HomeSegment
{
  QColor color;
  double fraction = 0.0;   // 0~1，总和接近 1
  QString label;           // 图例文本（可选）
};

class SegmentedBar : public QWidget
{
  Q_OBJECT

public:
  explicit SegmentedBar(QWidget *parent = nullptr);

  void setSegments(const QList<HomeSegment> &segments);
  void setTrackColor(const QColor &color);
  void setBarHeight(int height);
  void setShowPercent(bool show);

  QSize sizeHint() const override { return QSize(200, 18); }

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QList<HomeSegment> m_segments;
  QColor m_trackColor = QColor(226, 232, 240, 120);
  int m_height = 12;
  bool m_showPercent = false;
};
