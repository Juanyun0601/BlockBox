/**
 * @file   SkinUVOverlay.cpp
 * @brief  Minecraft 皮肤 UV 分区叠加层组件实现
 * @author BlockBox Team
 * @date   2026-07-22
 */
#include "SkinUVOverlay.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPen>

SkinUVOverlay::SkinUVOverlay(QObject* parent)
  : QObject(parent)
  , m_visible(false)
  , m_is64x64(true)
  , m_isSlim(false)
{
  rebuildRegions();
}

SkinUVOverlay::~SkinUVOverlay() = default;

void SkinUVOverlay::setVisible(bool visible)
{
  m_visible = visible;
}

bool SkinUVOverlay::isVisible() const
{
  return m_visible;
}

void SkinUVOverlay::setFormat(bool is64x64)
{
  if (m_is64x64 != is64x64)
  {
    m_is64x64 = is64x64;
    rebuildRegions();
  }
}

void SkinUVOverlay::setSlim(bool slim)
{
  if (m_isSlim != slim)
  {
    m_isSlim = slim;
    rebuildRegions();
  }
}

void SkinUVOverlay::draw(QPainter& painter, float scale) const
{
  if (!m_visible)
  {
    return;
  }

  painter.save();

  // 字体大小随 scale 调整，限制在 8-14pt 之间
  QFont font = painter.font();
  font.setPointSize(qBound(8, static_cast<int>(scale), 14));
  painter.setFont(font);
  const QFontMetrics fontMetrics(font);

  // 内层边框画笔（青色半透明虚线）
  QPen innerPen(QColor(0, 200, 200, 100));
  innerPen.setWidth(1);
  innerPen.setStyle(Qt::DashLine);

  // 外层边框画笔（橙色半透明虚线）
  QPen outerPen(QColor(255, 165, 0, 100));
  outerPen.setWidth(1);
  outerPen.setStyle(Qt::DashLine);

  for (const UvRegion& region : m_regions)
  {
    // 将纹理坐标乘以 scale 得到画布坐标
    const QRectF scaledRect(
      static_cast<qreal>(region.rect.x()) * scale,
      static_cast<qreal>(region.rect.y()) * scale,
      static_cast<qreal>(region.rect.width()) * scale,
      static_cast<qreal>(region.rect.height()) * scale);

    // 绘制区域边框（内层青色，外层橙色）
    painter.setPen(region.isOverlay ? outerPen : innerPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(scaledRect);

    // 绘制标签背景（半透明黑色，便于阅读）
    const int textWidth = fontMetrics.horizontalAdvance(region.name);
    const int textHeight = fontMetrics.height();
    const QRectF labelRect(
      scaledRect.left(),
      scaledRect.top(),
      static_cast<qreal>(textWidth) + 4.0,
      static_cast<qreal>(textHeight) + 2.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRect(labelRect);

    // 绘制标签文字
    painter.setPen(QColor(255, 255, 255, 220));
    painter.drawText(labelRect, Qt::AlignCenter, region.name);
  }

  painter.restore();
}

QVector<UvRegion> SkinUVOverlay::regions() const
{
  return m_regions;
}

void SkinUVOverlay::rebuildRegions()
{
  m_regions.clear();

  // 手臂宽度：Slim 模型为 15px（视觉提示手臂区域收窄），Classic 模型为 16px
  const int armWidth = m_isSlim ? 15 : 16;

  if (m_is64x64)
  {
    // === 内层（Base Layer）===
    m_regions.append({QRect(0, 0, 32, 16), QStringLiteral("头部"), false});
    m_regions.append({QRect(16, 16, 24, 16), QStringLiteral("身体"), false});
    m_regions.append({QRect(40, 16, armWidth, 16), QStringLiteral("右臂"), false});
    m_regions.append({QRect(0, 16, 16, 16), QStringLiteral("右腿"), false});
    m_regions.append({QRect(32, 48, armWidth, 16), QStringLiteral("左臂"), false});
    m_regions.append({QRect(16, 48, 16, 16), QStringLiteral("左腿"), false});

    // === 外层（Overlay Layer）===
    m_regions.append({QRect(32, 0, 32, 16), QStringLiteral("头部外层"), true});
    m_regions.append({QRect(16, 32, 24, 16), QStringLiteral("身体外层"), true});
    m_regions.append({QRect(40, 32, armWidth, 16), QStringLiteral("右臂外层"), true});
    m_regions.append({QRect(0, 32, 16, 16), QStringLiteral("右腿外层"), true});
    m_regions.append({QRect(48, 48, armWidth, 16), QStringLiteral("左臂外层"), true});
    m_regions.append({QRect(0, 48, 16, 16), QStringLiteral("左腿外层"), true});
  }
  else
  {
    // === 64x32 格式（仅内层）===
    m_regions.append({QRect(0, 0, 32, 16), QStringLiteral("头部"), false});
    m_regions.append({QRect(16, 16, 24, 16), QStringLiteral("身体"), false});
    m_regions.append({QRect(40, 16, armWidth, 16), QStringLiteral("右臂"), false});
    m_regions.append({QRect(0, 16, 16, 16), QStringLiteral("右腿"), false});
    // 左臂/左腿复用右臂/右腿区域
    m_regions.append({QRect(40, 16, armWidth, 16), QStringLiteral("左臂(同右臂)"), false});
    m_regions.append({QRect(0, 16, 16, 16), QStringLiteral("左腿(同右腿)"), false});
  }
}
