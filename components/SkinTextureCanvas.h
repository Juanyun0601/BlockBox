/**
 * @file   SkinTextureCanvas.h
 * @brief  2D 像素编辑画布控件声明，用于 Minecraft 皮肤纹理的像素级绘制
 * @author BlockBox Team
 * @date   2026-07-22
 *
 * 以可缩放、可平移的方式显示 SkinEditorDocument 中的皮肤纹理（64x64 或 64x32），
 * 支持画笔/橡皮/填充/吸管/直线五种工具进行像素级编辑。
 * 一次鼠标拖拽笔画在松开时一次性提交给文档，对应一次撤销步。
 */

#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QWidget>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;
class QPainter;

class SkinEditorDocument;
class SkinUVOverlay;

/**
 * @brief 绘制工具枚举
 */
enum class DrawTool
{
  Brush,       ///< 画笔
  Eraser,      ///< 橡皮
  Fill,        ///< 填充
  Eyedropper,  ///< 吸管
  Line         ///< 直线
};

/**
 * @brief 2D 像素编辑画布控件
 *
 * 继承 QWidget，将皮肤纹理按 m_zoom 倍放大显示（默认 8x，范围 2~32），
 * 内部维护棋盘格背景、网格、UV 叠加层与笔画预览。
 * 绘制操作在鼠标松开时通过 setPixels() 一次性提交给文档，
 * 保证一次笔画对应一次撤销步。
 */
class SkinTextureCanvas : public QWidget
{
  Q_OBJECT

public:
  explicit SkinTextureCanvas(QWidget* parent = nullptr);
  ~SkinTextureCanvas() override;

  // 文档绑定
  void setDocument(SkinEditorDocument* doc);
  SkinEditorDocument* document() const;

  // 工具控制
  void setTool(DrawTool tool);
  DrawTool tool() const;
  void setBrushSize(int size);  ///< 笔刷大小仅接受 1/2/4
  int brushSize() const;

  // 颜色控制
  void setForegroundColor(const QColor& color);
  QColor foregroundColor() const;
  void setBackgroundColor(const QColor& color);
  QColor backgroundColor() const;

  // 显示控制
  void setShowGrid(bool show);
  bool showGrid() const;
  void setShowUVOverlay(bool show);
  bool showUVOverlay() const;

  // 只读模式（3D 编辑模式下的小尺寸纹理视图用，禁止任何编辑/缩放/平移）
  void setReadOnly(bool readOnly);
  bool isReadOnly() const;

  // 区域选择（截取部分辅助）
  void setSelectionRect(const QRect& rect);  ///< 设置选中区域（纹理坐标，空矩形清除）
  QRect selectionRect() const;
  bool hasSelection() const;

  // 缩放
  float zoom() const;
  void setZoom(float zoom);
  void zoomIn();
  void zoomOut();

signals:
  void toolChanged(DrawTool tool);        ///< 工具切换
  void colorPicked(const QColor& color);  ///< 吸管拾取后发出
  void pixelHovered(int x, int y);        ///< 鼠标移动时发出像素坐标（-1,-1 表示离开画布）
  void zoomChanged(float zoom);           ///< 缩放倍数变更
  void imageEdited();                     ///< 绘制操作完成后发出，用于触发 3D 预览更新

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  // 坐标转换
  QPointF textureOriginF() const;               ///< 纹理左上角在控件中的浮点坐标
  QPoint widgetToTexture(QPoint widgetPos) const; ///< 控件坐标转纹理像素坐标，越界返回 (-1,-1)
  QPoint textureToWidget(QPoint texPos) const;   ///< 纹理像素坐标转控件坐标

  // 笔刷与裁剪
  QVector<QPoint> brushStamp(int x, int y) const; ///< 根据笔刷大小生成像素块坐标
  QVector<QPoint> clipPoints(const QVector<QPoint>& points) const; ///< 裁剪到有效纹理范围

  // Bresenham 直线
  static QVector<QPoint> bresenhamLine(int x0, int y0, int x1, int y1);

  // 绘制辅助
  void drawCheckerboard(QPainter& painter) const; ///< 棋盘格背景（透明区域指示）
  void drawSkinImage(QPainter& painter) const;    ///< 放大绘制皮肤纹理（最近邻）
  void drawGrid(QPainter& painter) const;         ///< 像素网格线
  void drawSelection(QPainter& painter) const;    ///< 截取选区高亮
  void drawStrokePreview(QPainter& painter) const; ///< 画笔/橡皮笔画预览
  void drawLinePreview(QPainter& painter) const;  ///< 直线工具预览
  void rebuildChecker();                          ///< 重建棋盘格缓存（纹理尺寸变更时）
  static QColor checkerColor(int texX, int texY); ///< 棋盘格颜色（按像素奇偶）

  // 交互与文档辅助
  void updateCursor();                            ///< 根据工具/平移状态设置光标
  void updateOverlayFormat();                     ///< 同步 UV 叠加层的 format/slim
  void onSkinChanged(const QImage& image);        ///< 文档皮肤变更响应
  void applyZoom(float newZoom, const QPoint& anchor); ///< 以锚点为中心缩放

  // 文档与叠加层
  SkinEditorDocument* m_document;  ///< 文档指针（不持有所有权）
  SkinUVOverlay* m_uvOverlay;      ///< UV 叠加层（持有所有权，父对象为本控件）

  // 工具与颜色
  DrawTool m_tool;        ///< 当前工具
  int m_brushSize;        ///< 笔刷大小（1/2/4）
  QColor m_fgColor;       ///< 前景色
  QColor m_bgColor;       ///< 背景色
  QColor m_strokeColor;   ///< 当前笔画使用的颜色（左键=前景，右键=背景，橡皮=透明）

  // 显示
  float m_zoom;           ///< 缩放倍数
  bool m_showGrid;        ///< 是否显示网格
  bool m_readOnly = false; ///< 只读模式（禁止编辑）
  QImage m_checkerImage;  ///< 棋盘格缓存（纹理像素尺寸，每像素一格）

  // 截取选区
  QRect m_selectionRect;  ///< 选中区域（纹理坐标）
  bool m_hasSelection;    ///< 是否有选中区域

  // 平移
  bool m_spacePressed;    ///< 空格键按下
  bool m_isPanning;       ///< 正在平移
  QPoint m_panStart;      ///< 平移起点
  QPoint m_offset;        ///< 画布偏移（平移量）

  // 绘制状态
  QPoint m_lastDrawPos;   ///< 上次绘制位置（连线绘制用）
  QPoint m_lineStartPos;  ///< 直线工具起点
  QPoint m_lineCurrentPos; ///< 直线工具当前终点（预览用）
  bool m_isDrawing;       ///< 正在绘制
  bool m_leftButton;      ///< 左键按下（标记当前笔画由左键发起）
  QVector<QPoint> m_pendingPoints; ///< 当前笔画累积的点（鼠标松开时一次性提交）

  // 缩放范围常量
  static constexpr float MIN_ZOOM = 2.0f;
  static constexpr float MAX_ZOOM = 32.0f;
  static constexpr float DEFAULT_ZOOM = 8.0f;
  static constexpr float ZOOM_FACTOR = 1.25f;
  static constexpr float GRID_MIN_ZOOM = 4.0f; ///< 网格显示的最小缩放倍数
};
