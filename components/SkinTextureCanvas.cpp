/**
 * @file   SkinTextureCanvas.cpp
 * @brief  2D 像素编辑画布控件实现
 * @author BlockBox Team
 * @date   2026-07-22
 *
 * 渲染：棋盘格背景 + 放大皮肤纹理（最近邻）+ 像素网格 + UV 叠加层 + 笔画/直线预览。
 * 交互：画笔/橡皮/填充/吸管/直线工具，空格或中键平移，滚轮以鼠标为中心缩放。
 */

#include "SkinTextureCanvas.h"

#include "components/SkinUVOverlay.h"
#include "utils/SkinEditorDocument.h"

#include <QBrush>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>
#include <QtGlobal>
#include <cmath>

SkinTextureCanvas::SkinTextureCanvas(QWidget* parent)
  : QWidget(parent)
  , m_document(nullptr)
  , m_uvOverlay(new SkinUVOverlay(this))
  , m_tool(DrawTool::Brush)
  , m_brushSize(1)
  , m_fgColor(Qt::black)
  , m_bgColor(Qt::white)
  , m_strokeColor(Qt::black)
  , m_zoom(DEFAULT_ZOOM)
  , m_showGrid(true)
  , m_hasSelection(false)
  , m_spacePressed(false)
  , m_isPanning(false)
  , m_isDrawing(false)
  , m_leftButton(false)
{
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  updateCursor();
}

SkinTextureCanvas::~SkinTextureCanvas() = default;

void SkinTextureCanvas::setDocument(SkinEditorDocument* doc)
{
  if (m_document == doc)
  {
    return;
  }
  if (m_document != nullptr)
  {
    disconnect(m_document, &SkinEditorDocument::skinChanged,
               this, &SkinTextureCanvas::onSkinChanged);
  }
  m_document = doc;
  m_offset = QPoint(0, 0);
  m_pendingPoints.clear();
  m_isDrawing = false;
  if (m_document != nullptr)
  {
    connect(m_document, &SkinEditorDocument::skinChanged,
            this, &SkinTextureCanvas::onSkinChanged);
    updateOverlayFormat();
    rebuildChecker();
  }
  else
  {
    m_checkerImage = QImage();
  }
  update();
}

SkinEditorDocument* SkinTextureCanvas::document() const
{
  return m_document;
}

void SkinTextureCanvas::setTool(DrawTool tool)
{
  if (m_tool == tool)
  {
    return;
  }
  m_tool = tool;
  updateCursor();
  emit toolChanged(m_tool);
}

DrawTool SkinTextureCanvas::tool() const
{
  return m_tool;
}

void SkinTextureCanvas::setBrushSize(int size)
{
  if (size != 1 && size != 2 && size != 4)
  {
    return;
  }
  m_brushSize = size;
}

int SkinTextureCanvas::brushSize() const
{
  return m_brushSize;
}

void SkinTextureCanvas::setForegroundColor(const QColor& color)
{
  m_fgColor = color;
}

QColor SkinTextureCanvas::foregroundColor() const
{
  return m_fgColor;
}

void SkinTextureCanvas::setBackgroundColor(const QColor& color)
{
  m_bgColor = color;
}

QColor SkinTextureCanvas::backgroundColor() const
{
  return m_bgColor;
}

void SkinTextureCanvas::setShowGrid(bool show)
{
  if (m_showGrid == show)
  {
    return;
  }
  m_showGrid = show;
  update();
}

bool SkinTextureCanvas::showGrid() const
{
  return m_showGrid;
}

void SkinTextureCanvas::setShowUVOverlay(bool show)
{
  if (m_uvOverlay != nullptr)
  {
    m_uvOverlay->setVisible(show);
  }
  update();
}

bool SkinTextureCanvas::showUVOverlay() const
{
  return (m_uvOverlay != nullptr) ? m_uvOverlay->isVisible() : false;
}

void SkinTextureCanvas::setReadOnly(bool readOnly)
{
  if (m_readOnly == readOnly)
    return;
  m_readOnly = readOnly;
  if (m_readOnly)
  {
    m_pendingPoints.clear();
    m_isDrawing = false;
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
  }
  else
  {
    updateCursor();
  }
}

bool SkinTextureCanvas::isReadOnly() const
{
  return m_readOnly;
}

void SkinTextureCanvas::setSelectionRect(const QRect& rect)
{
  m_selectionRect = rect.isValid() ? rect : QRect();
  m_hasSelection = m_selectionRect.isValid() && !m_selectionRect.isEmpty();
  update();
}

QRect SkinTextureCanvas::selectionRect() const
{
  return m_selectionRect;
}

bool SkinTextureCanvas::hasSelection() const
{
  return m_hasSelection;
}

float SkinTextureCanvas::zoom() const
{
  return m_zoom;
}

void SkinTextureCanvas::setZoom(float zoom)
{
  applyZoom(zoom, QPoint(width() / 2, height() / 2));
}

void SkinTextureCanvas::zoomIn()
{
  applyZoom(m_zoom * ZOOM_FACTOR, QPoint(width() / 2, height() / 2));
}

void SkinTextureCanvas::zoomOut()
{
  applyZoom(m_zoom / ZOOM_FACTOR, QPoint(width() / 2, height() / 2));
}

void SkinTextureCanvas::paintEvent(QPaintEvent* /*event*/)
{
  QPainter painter(this);
  // 关闭抗锯齿与平滑缩放，保证像素清晰
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  // 1. 基础背景（画布外区域）
  painter.fillRect(rect(), QColor(60, 60, 60));

  if (m_document == nullptr)
  {
    return;
  }

  // 2. 棋盘格背景（透明区域指示）
  drawCheckerboard(painter);

  // 3. 皮肤纹理（放大，最近邻；透明像素透出棋盘格）
  drawSkinImage(painter);

  // 4. 像素网格
  drawGrid(painter);

  // 5. UV 叠加层
  if (m_uvOverlay != nullptr && m_uvOverlay->isVisible())
  {
    painter.save();
    painter.translate(textureOriginF());
    m_uvOverlay->draw(painter, m_zoom);
    painter.restore();
  }

  // 6. 截取选区高亮
  drawSelection(painter);

  // 7. 画笔/橡皮笔画预览
  drawStrokePreview(painter);

  // 8. 直线工具预览
  drawLinePreview(painter);
}

void SkinTextureCanvas::mousePressEvent(QMouseEvent* event)
{
  if (m_readOnly)
  {
    event->ignore();
    return;
  }
  if (m_document == nullptr)
  {
    event->ignore();
    return;
  }

  // 已在平移中：任意按下都继续平移，避免平移中误触发工具
  if (m_isPanning)
  {
    m_panStart = event->position().toPoint();
    event->accept();
    return;
  }

  // 平移：空格按下或中键
  if (m_spacePressed || event->button() == Qt::MiddleButton)
  {
    m_isPanning = true;
    m_panStart = event->position().toPoint();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }

  if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
  {
    event->ignore();
    return;
  }

  QPoint tex = widgetToTexture(event->position().toPoint());
  bool isLeft = (event->button() == Qt::LeftButton);
  m_leftButton = isLeft;

  if (tex.x() < 0)
  {
    event->accept();
    return;
  }

  switch (m_tool)
  {
    case DrawTool::Brush:
    {
      m_isDrawing = true;
      m_strokeColor = isLeft ? m_fgColor : m_bgColor;
      m_lastDrawPos = tex;
      m_pendingPoints = clipPoints(brushStamp(tex.x(), tex.y()));
      update();
      break;
    }
    case DrawTool::Eraser:
    {
      m_isDrawing = true;
      m_strokeColor = QColor(0, 0, 0, 0);
      m_lastDrawPos = tex;
      m_pendingPoints = clipPoints(brushStamp(tex.x(), tex.y()));
      update();
      break;
    }
    case DrawTool::Fill:
    {
      QColor c = isLeft ? m_fgColor : m_bgColor;
      m_document->fillRegion(tex.x(), tex.y(), c);
      emit imageEdited();
      break;
    }
    case DrawTool::Eyedropper:
    {
      QColor picked = m_document->skinImage().pixelColor(tex.x(), tex.y());
      if (isLeft)
      {
        m_fgColor = picked;
      }
      else
      {
        m_bgColor = picked;
      }
      emit colorPicked(picked);
      setTool(DrawTool::Brush);  // 拾取后切回画笔
      break;
    }
    case DrawTool::Line:
    {
      m_isDrawing = true;
      m_strokeColor = isLeft ? m_fgColor : m_bgColor;
      m_lineStartPos = tex;
      m_lineCurrentPos = tex;
      update();
      break;
    }
  }
  event->accept();
}

void SkinTextureCanvas::mouseMoveEvent(QMouseEvent* event)
{
  if (m_document == nullptr)
  {
    event->ignore();
    return;
  }

  if (m_isPanning)
  {
    QPoint pos = event->position().toPoint();
    QPoint delta = pos - m_panStart;
    m_offset += delta;
    m_panStart = pos;
    update();
    event->accept();
    return;
  }

  QPoint tex = widgetToTexture(event->position().toPoint());
  emit pixelHovered(tex.x(), tex.y());

  if (m_isDrawing)
  {
    switch (m_tool)
    {
      case DrawTool::Brush:
      case DrawTool::Eraser:
      {
        if (tex.x() >= 0 && tex != m_lastDrawPos)
        {
          // 用 Bresenham 连线填充相邻两次移动间的间隔像素，避免断点
          QVector<QPoint> seg = bresenhamLine(m_lastDrawPos.x(), m_lastDrawPos.y(),
                                              tex.x(), tex.y());
          for (const QPoint& p : seg)
          {
            m_pendingPoints += clipPoints(brushStamp(p.x(), p.y()));
          }
          m_lastDrawPos = tex;
          update();
        }
        break;
      }
      case DrawTool::Line:
      {
        m_lineCurrentPos = tex;  // 越界也记录，预览与提交时裁剪
        update();
        break;
      }
      default:
        break;
    }
  }
  event->accept();
}

void SkinTextureCanvas::mouseReleaseEvent(QMouseEvent* event)
{
  if (m_document == nullptr)
  {
    event->ignore();
    return;
  }

  if (m_isPanning)
  {
    // 平移由任意按键释放结束（支持空格+左键或中键发起）
    m_isPanning = false;
    updateCursor();
    event->accept();
    return;
  }

  if (!m_isDrawing)
  {
    event->accept();
    return;
  }

  // 仅响应发起当前笔画的那个按键
  if (m_leftButton && event->button() != Qt::LeftButton)
  {
    event->accept();
    return;
  }
  if (!m_leftButton && event->button() != Qt::RightButton)
  {
    event->accept();
    return;
  }

  switch (m_tool)
  {
    case DrawTool::Brush:
    {
      QVector<QPoint> pts = clipPoints(m_pendingPoints);
      if (!pts.isEmpty())
      {
        m_document->setPixels(pts, m_strokeColor);
      }
      m_pendingPoints.clear();
      emit imageEdited();
      break;
    }
    case DrawTool::Eraser:
    {
      QVector<QPoint> pts = clipPoints(m_pendingPoints);
      if (!pts.isEmpty())
      {
        m_document->setPixels(pts, QColor(0, 0, 0, 0));
      }
      m_pendingPoints.clear();
      emit imageEdited();
      break;
    }
    case DrawTool::Line:
    {
      QPoint endTex = widgetToTexture(event->position().toPoint());
      if (endTex.x() < 0)
      {
        endTex = m_lineCurrentPos;
      }
      QVector<QPoint> line = bresenhamLine(m_lineStartPos.x(), m_lineStartPos.y(),
                                           endTex.x(), endTex.y());
      QVector<QPoint> pts = clipPoints(line);
      if (!pts.isEmpty())
      {
        m_document->setPixels(pts, m_strokeColor);
        emit imageEdited();
      }
      break;
    }
    default:
      // Fill/Eyedropper 已在 press 时处理
      break;
  }

  m_isDrawing = false;
  m_leftButton = false;
  update();
  event->accept();
}

void SkinTextureCanvas::wheelEvent(QWheelEvent* event)
{
  if (m_readOnly || m_document == nullptr)
  {
    event->ignore();
    return;
  }
  QPoint angle = event->angleDelta();
  if (angle.y() == 0)
  {
    event->ignore();
    return;
  }
  QPoint anchor = event->position().toPoint();
  if (angle.y() > 0)
  {
    applyZoom(m_zoom * ZOOM_FACTOR, anchor);
  }
  else
  {
    applyZoom(m_zoom / ZOOM_FACTOR, anchor);
  }
  event->accept();
}

void SkinTextureCanvas::keyPressEvent(QKeyEvent* event)
{
  if (m_readOnly)
  {
    QWidget::keyPressEvent(event);
    return;
  }
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
  {
    m_spacePressed = true;
    updateCursor();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void SkinTextureCanvas::keyReleaseEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
  {
    m_spacePressed = false;
    if (!m_isPanning)
    {
      updateCursor();
    }
    event->accept();
    return;
  }
  QWidget::keyReleaseEvent(event);
}

void SkinTextureCanvas::leaveEvent(QEvent* event)
{
  emit pixelHovered(-1, -1);
  event->accept();
}

QPointF SkinTextureCanvas::textureOriginF() const
{
  if (m_document == nullptr)
  {
    return QPointF(m_offset);
  }
  const QImage& img = m_document->skinImage();
  qreal cx = (width() - img.width() * m_zoom) * 0.5;
  qreal cy = (height() - img.height() * m_zoom) * 0.5;
  return QPointF(m_offset) + QPointF(cx, cy);
}

QPoint SkinTextureCanvas::widgetToTexture(QPoint widgetPos) const
{
  if (m_document == nullptr)
  {
    return QPoint(-1, -1);
  }
  QPointF origin = textureOriginF();
  int x = static_cast<int>(std::floor((widgetPos.x() - origin.x()) / m_zoom));
  int y = static_cast<int>(std::floor((widgetPos.y() - origin.y()) / m_zoom));
  const QImage& img = m_document->skinImage();
  if (x < 0 || x >= img.width() || y < 0 || y >= img.height())
  {
    return QPoint(-1, -1);
  }
  return QPoint(x, y);
}

QPoint SkinTextureCanvas::textureToWidget(QPoint texPos) const
{
  QPointF origin = textureOriginF();
  return QPoint(static_cast<int>(origin.x() + texPos.x() * m_zoom),
                static_cast<int>(origin.y() + texPos.y() * m_zoom));
}

QVector<QPoint> SkinTextureCanvas::brushStamp(int x, int y) const
{
  QVector<QPoint> points;
  int half = m_brushSize / 2;
  points.reserve(m_brushSize * m_brushSize);
  for (int dy = 0; dy < m_brushSize; ++dy)
  {
    for (int dx = 0; dx < m_brushSize; ++dx)
    {
      points.append(QPoint(x - half + dx, y - half + dy));
    }
  }
  return points;
}

QVector<QPoint> SkinTextureCanvas::clipPoints(const QVector<QPoint>& points) const
{
  if (m_document == nullptr)
  {
    return {};
  }
  const QImage& img = m_document->skinImage();
  QVector<QPoint> clipped;
  clipped.reserve(points.size());
  for (const QPoint& p : points)
  {
    if (p.x() >= 0 && p.x() < img.width() && p.y() >= 0 && p.y() < img.height())
    {
      clipped.append(p);
    }
  }
  return clipped;
}

QVector<QPoint> SkinTextureCanvas::bresenhamLine(int x0, int y0, int x1, int y1)
{
  QVector<QPoint> points;
  int dx = qAbs(x1 - x0);
  int dy = qAbs(y1 - y0);
  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx - dy;
  int x = x0;
  int y = y0;
  while (true)
  {
    points.append(QPoint(x, y));
    if (x == x1 && y == y1)
    {
      break;
    }
    int e2 = 2 * err;
    if (e2 > -dy)
    {
      err -= dy;
      x += sx;
    }
    if (e2 < dx)
    {
      err += dx;
      y += sy;
    }
  }
  return points;
}

QColor SkinTextureCanvas::checkerColor(int texX, int texY)
{
  const QColor LIGHT(204, 204, 204);
  const QColor DARK(170, 170, 170);
  return ((texX + texY) % 2 == 0) ? LIGHT : DARK;
}

void SkinTextureCanvas::drawCheckerboard(QPainter& painter) const
{
  if (m_checkerImage.isNull())
  {
    return;
  }
  QPointF origin = textureOriginF();
  QRectF target(origin, QSizeF(m_checkerImage.width() * m_zoom,
                                m_checkerImage.height() * m_zoom));
  painter.drawImage(target, m_checkerImage, QRectF(m_checkerImage.rect()));
}

void SkinTextureCanvas::drawSkinImage(QPainter& painter) const
{
  if (m_document == nullptr)
  {
    return;
  }
  const QImage& img = m_document->skinImage();
  if (img.isNull())
  {
    return;
  }
  QPointF origin = textureOriginF();
  QRectF target(origin, QSizeF(img.width() * m_zoom, img.height() * m_zoom));
  painter.drawImage(target, img, QRectF(0, 0, img.width(), img.height()));
}

void SkinTextureCanvas::drawSelection(QPainter& painter) const
{
  if (!m_hasSelection || m_document == nullptr || m_selectionRect.isEmpty())
  {
    return;
  }
  const QImage& img = m_document->skinImage();
  if (img.isNull())
  {
    return;
  }
  QPointF origin = textureOriginF();
  QRectF texRect(origin, QSizeF(img.width() * m_zoom, img.height() * m_zoom));

  QRectF sel(
    origin.x() + m_selectionRect.x() * m_zoom,
    origin.y() + m_selectionRect.y() * m_zoom,
    m_selectionRect.width() * m_zoom,
    m_selectionRect.height() * m_zoom);

  painter.save();
  painter.setClipRect(texRect);
  // 半透明高亮
  painter.fillRect(sel, QColor(0, 140, 255, 70));
  // 高亮边框
  QPen pen(QColor(0, 120, 255));
  pen.setWidth(2);
  pen.setStyle(Qt::SolidLine);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(sel);
  painter.restore();
}

void SkinTextureCanvas::drawGrid(QPainter& painter) const
{
  if (!m_showGrid || m_document == nullptr || m_zoom < GRID_MIN_ZOOM)
  {
    return;
  }
  const QImage& img = m_document->skinImage();
  QPointF origin = textureOriginF();
  qreal w = img.width() * m_zoom;
  qreal h = img.height() * m_zoom;
  QPen pen(QColor(0, 0, 0, 80));
  pen.setWidthF(1.0);
  painter.setPen(pen);
  for (int x = 0; x <= img.width(); ++x)
  {
    qreal xp = origin.x() + x * m_zoom;
    painter.drawLine(QPointF(xp, origin.y()), QPointF(xp, origin.y() + h));
  }
  for (int y = 0; y <= img.height(); ++y)
  {
    qreal yp = origin.y() + y * m_zoom;
    painter.drawLine(QPointF(origin.x(), yp), QPointF(origin.x() + w, yp));
  }
}

void SkinTextureCanvas::drawStrokePreview(QPainter& painter) const
{
  if (!m_isDrawing || m_pendingPoints.isEmpty())
  {
    return;
  }
  if (m_tool != DrawTool::Brush && m_tool != DrawTool::Eraser)
  {
    return;
  }
  if (m_document == nullptr)
  {
    return;
  }
  const QImage& img = m_document->skinImage();
  QPointF origin = textureOriginF();
  QRectF texRect(origin, QSizeF(img.width() * m_zoom, img.height() * m_zoom));

  painter.save();
  painter.setClipRect(texRect);
  for (const QPoint& p : m_pendingPoints)
  {
    QRectF r(origin.x() + p.x() * m_zoom, origin.y() + p.y() * m_zoom, m_zoom, m_zoom);
    if (m_tool == DrawTool::Eraser)
    {
      // 用棋盘色显示将被擦除为透明的像素
      painter.fillRect(r, checkerColor(p.x(), p.y()));
    }
    else
    {
      painter.fillRect(r, m_strokeColor);
    }
  }
  painter.restore();
}

void SkinTextureCanvas::drawLinePreview(QPainter& painter) const
{
  if (!m_isDrawing || m_tool != DrawTool::Line || m_document == nullptr)
  {
    return;
  }
  QVector<QPoint> line = bresenhamLine(m_lineStartPos.x(), m_lineStartPos.y(),
                                       m_lineCurrentPos.x(), m_lineCurrentPos.y());
  const QImage& img = m_document->skinImage();
  QPointF origin = textureOriginF();
  QColor preview = m_strokeColor;
  preview.setAlpha(160);

  painter.save();
  painter.setClipRect(QRectF(origin, QSizeF(img.width() * m_zoom, img.height() * m_zoom)));
  for (const QPoint& p : line)
  {
    if (p.x() < 0 || p.x() >= img.width() || p.y() < 0 || p.y() >= img.height())
    {
      continue;
    }
    QRectF r(origin.x() + p.x() * m_zoom, origin.y() + p.y() * m_zoom, m_zoom, m_zoom);
    painter.fillRect(r, preview);
  }
  painter.restore();
}

void SkinTextureCanvas::rebuildChecker()
{
  if (m_document == nullptr)
  {
    m_checkerImage = QImage();
    return;
  }
  const QImage& img = m_document->skinImage();
  int w = img.width();
  int h = img.height();
  if (w <= 0 || h <= 0)
  {
    m_checkerImage = QImage();
    return;
  }
  if (m_checkerImage.width() != w || m_checkerImage.height() != h)
  {
    m_checkerImage = QImage(w, h, QImage::Format_ARGB32);
  }
  for (int y = 0; y < h; ++y)
  {
    QRgb* scan = reinterpret_cast<QRgb*>(m_checkerImage.scanLine(y));
    for (int x = 0; x < w; ++x)
    {
      scan[x] = ((x + y) % 2 == 0) ? qRgb(204, 204, 204) : qRgb(170, 170, 170);
    }
  }
}

void SkinTextureCanvas::updateCursor()
{
  if (m_readOnly)
  {
    setCursor(Qt::ArrowCursor);
    return;
  }
  if (m_spacePressed)
  {
    setCursor(m_isPanning ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    return;
  }
  switch (m_tool)
  {
    case DrawTool::Brush:
    case DrawTool::Eraser:
    case DrawTool::Line:
      setCursor(Qt::CrossCursor);
      break;
    case DrawTool::Fill:
      setCursor(Qt::ArrowCursor);
      break;
    case DrawTool::Eyedropper:
      setCursor(Qt::PointingHandCursor);
      break;
  }
}

void SkinTextureCanvas::updateOverlayFormat()
{
  if (m_document == nullptr || m_uvOverlay == nullptr)
  {
    return;
  }
  m_uvOverlay->setFormat(m_document->is64x64());
  m_uvOverlay->setSlim(m_document->isSlim());
}

void SkinTextureCanvas::onSkinChanged(const QImage& /*image*/)
{
  updateOverlayFormat();
  rebuildChecker();
  update();
}

void SkinTextureCanvas::applyZoom(float newZoom, const QPoint& anchor)
{
  // 手动裁剪到 [MIN_ZOOM, MAX_ZOOM]，避免对 static constexpr 成员的 odr-use
  // （qBound 按常量引用取参，在 C++11 下需要类外定义）
  float clamped = newZoom;
  if (clamped < MIN_ZOOM)
  {
    clamped = MIN_ZOOM;
  }
  if (clamped > MAX_ZOOM)
  {
    clamped = MAX_ZOOM;
  }
  if (clamped == m_zoom)
  {
    return;
  }

  if (m_document == nullptr)
  {
    m_zoom = clamped;
    emit zoomChanged(m_zoom);
    update();
    return;
  }

  const QImage& img = m_document->skinImage();
  float oldZoom = m_zoom;

  // 锚点在纹理像素空间的浮点坐标（旧缩放下）
  QPointF originOld = textureOriginF();
  QPointF texF((anchor.x() - originOld.x()) / oldZoom,
               (anchor.y() - originOld.y()) / oldZoom);

  // 新缩放下的居中偏移
  QPointF centerNew((width() - img.width() * clamped) * 0.5,
                    (height() - img.height() * clamped) * 0.5);
  // 使锚点仍指向同一纹理像素：anchor = originNew + texF * clamped
  QPointF originNew = QPointF(anchor) - QPointF(texF.x() * clamped, texF.y() * clamped);
  QPointF newOffset = originNew - centerNew;

  m_zoom = clamped;
  m_offset = newOffset.toPoint();
  emit zoomChanged(m_zoom);
  update();
}
