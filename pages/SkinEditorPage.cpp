/**
 * @file   SkinEditorPage.cpp
 * @brief  Minecraft 皮肤制作器主页面类实现
 * @author BlockBox Team
 * @date   2026-07-22
 */

#include "SkinEditorPage.h"

// Qt 头文件
#include <QCheckBox>
#include <QClipboard>
#include "components/AppColorDialog.h"
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include "components/AppFileDialog.h"
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include "components/AppInputDialog.h"
#include <QLabel>
#include "components/AppMessageBox.h"
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSize>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <QPainter>

// 项目内部头文件
#include "components/Skin3DWidget.h"
#include "components/SkinTextureCanvas.h"
#include "utils/SkinDownloader.h"
#include "utils/SkinEditorDocument.h"
#include "utils/ThemeManager.h"

namespace {

/**
 * @brief 预设调色板（32 色，参考 Minecraft 皮肤常用颜色）
 */
const QVector<QColor>& presetColors()
{
  static const QVector<QColor> colors = {
    // 肤色
    QColor(245, 215, 175), QColor(225, 195, 155), QColor(195, 165, 125), QColor(155, 125, 85),
    // 发色
    QColor(50, 35, 20), QColor(90, 60, 35), QColor(140, 100, 60), QColor(200, 160, 100),
    QColor(180, 130, 90), QColor(60, 45, 30), QColor(240, 240, 240), QColor(120, 120, 120),
    // 衣服
    QColor(60, 90, 180), QColor(80, 130, 200), QColor(50, 150, 100), QColor(100, 180, 80),
    QColor(180, 80, 60), QColor(200, 120, 50), QColor(150, 60, 90), QColor(160, 100, 160),
    // 裤子
    QColor(40, 45, 55), QColor(70, 75, 85), QColor(90, 70, 50), QColor(120, 90, 60),
    // 鞋
    QColor(60, 40, 25), QColor(80, 55, 35), QColor(100, 75, 50), QColor(30, 25, 20),
    // 杂色
    QColor(255, 255, 255), QColor(0, 0, 0), QColor(255, 220, 60), QColor(200, 30, 30)
  };
  return colors;
}

/**
 * @brief 皮肤模板名称列表
 */
const QStringList& templateNames()
{
  static const QStringList names = {
    "Steve", "Alex", "Ari", "Efe", "Kai",
    "Makena", "Noor", "Sunny", "Zuri", QStringLiteral("空白")
  };
  return names;
}

/**
 * @brief 将 DrawTool 枚举转换为中文名称
 * @param tool 绘制工具枚举值
 * @return 中文名称字符串
 */
QString toolName(DrawTool tool)
{
  switch (tool)
  {
  case DrawTool::Brush:
    return QStringLiteral("画笔");
  case DrawTool::Eraser:
    return QStringLiteral("橡皮");
  case DrawTool::Fill:
    return QStringLiteral("填充");
  case DrawTool::Eyedropper:
    return QStringLiteral("吸管");
  case DrawTool::Line:
    return QStringLiteral("直线");
  }
  return {};
}

/**
 * @brief 生成色块按钮的样式表
 * @param color 目标颜色
 * @return 样式表字符串
 */
QString colorButtonStyle(const QColor& color)
{
  return QStringLiteral("background-color: %1; border: 1px solid #888;")
           .arg(color.name());
}

/**
 * @brief 从皮肤纹理中裁切头部正面作为预设缩略图
 * @param image 皮肤纹理（64x64 标准布局）
 * @param size 目标缩略图边长（像素）
 * @return 缩放后的头部正面缩略图；输入为空返回空 QPixmap
 *
 * 头部正面位于纹理坐标 (8,8) 起 8x8 区域（与 partFaceRect 头部/正面一致）。
 */
QPixmap makeHeadThumbnail(const QImage& image, int size = 48)
{
  if (image.isNull())
    return QPixmap();
  const QImage head = image.copy(8, 8, 8, 8);
  if (head.isNull())
    return QPixmap();
  return QPixmap::fromImage(
    head.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

/**
 * @brief 部件盒子规格（标准 Box UV 布局，参考 Blockbench player_preview_model）
 *
 * u0/v0 为 UV 偏移，w/h/d 为盒子的宽/高/深（单位：纹理像素）。
 * 经典模型手臂宽 4px，Slim 模型手臂宽 3px。
 */
struct PartBoxSpec
{
  int u0, v0;
  int w, h, d;
};

/**
 * @brief 获取指定部件的盒子规格
 * @param partIndex 部件索引（0=头部 1=身体 2=右臂 3=左臂 4=右腿 5=左腿）
 * @param slim 是否为 Slim 模型（影响手臂宽度）
 * @param overlay 是否为外层(overlay)
 */
PartBoxSpec partBoxSpec(int partIndex, bool slim, bool overlay)
{
  const int armW = slim ? 3 : 4;
  switch (partIndex)
  {
  case 0:  return overlay ? PartBoxSpec{32, 0, 8, 8, 8}   : PartBoxSpec{0, 0, 8, 8, 8};
  case 1:  return overlay ? PartBoxSpec{16, 32, 8, 12, 4} : PartBoxSpec{16, 16, 8, 12, 4};
  case 2:  return overlay ? PartBoxSpec{40, 32, armW, 12, 4} : PartBoxSpec{40, 16, armW, 12, 4};
  case 3:  return overlay ? PartBoxSpec{48, 48, armW, 12, 4} : PartBoxSpec{32, 48, armW, 12, 4};
  case 4:  return overlay ? PartBoxSpec{0, 32, 4, 12, 4}  : PartBoxSpec{0, 16, 4, 12, 4};
  case 5:  return overlay ? PartBoxSpec{0, 48, 4, 12, 4}  : PartBoxSpec{16, 48, 4, 12, 4};
  }
  return PartBoxSpec{0, 0, 0, 0, 0};
}

/**
 * @brief 计算指定部件某一面的 UV 矩形区域
 * @param partIndex 部件索引（0=头部 1=身体 2=右臂 3=左臂 4=右腿 5=左腿）
 * @param faceIndex 面索引（0=前 1=后 2=左 3=右 4=上 5=下）
 * @param overlay 是否为外层(overlay)
 * @param slim 是否为 Slim 模型
 * @param is64x64 是否为 64x64 格式（64x32 无外层）
 * @return 纹理像素坐标矩形；无效返回空 QRect
 *
 * 标准 Box UV 布局（与 3D 预览/SkinUVOverlay/Blockbench 一致）：
 *   右(east/+X)  : x=[u0, u0+d]      y=[v0+d, v0+d+h]  尺寸 d×h
 *   前(north/-Z) : x=[u0+d, u0+d+w]  y=[v0+d, v0+d+h]  尺寸 w×h
 *   左(west/-X)  : x=[u0+d+w, u0+2d+w] y=[v0+d, v0+d+h] 尺寸 d×h
 *   后(south/+Z) : x=[u0+2d+w, u0+2d+2w] y=[v0+d, v0+d+h] 尺寸 w×h
 *   上(up/+Y)    : x=[u0+d, u0+d+w]  y=[v0, v0+d]      尺寸 w×d
 *   下(down/-Y)  : x=[u0+d+w, u0+d+2w] y=[v0, v0+d]    尺寸 w×d
 */
QRect partFaceRect(int partIndex, int faceIndex, bool overlay, bool slim, bool is64x64)
{
  if (!is64x64 && overlay)
    return QRect();
  const PartBoxSpec s = partBoxSpec(partIndex, slim, overlay);
  if (s.w <= 0 || s.h <= 0 || s.d <= 0)
    return QRect();
  const int u0 = s.u0, v0 = s.v0, w = s.w, h = s.h, d = s.d;
  switch (faceIndex)
  {
  case 0: return QRect(u0 + d, v0 + d, w, h);           // 前
  case 1: return QRect(u0 + 2 * d + w, v0 + d, w, h);   // 后
  case 2: return QRect(u0 + d + w, v0 + d, d, h);       // 左
  case 3: return QRect(u0, v0 + d, d, h);               // 右
  case 4: return QRect(u0 + d, v0, w, d);               // 上
  case 5: return QRect(u0 + d + w, v0, w, d);           // 下
  }
  return QRect();
}

} // namespace

SkinEditorPage::SkinEditorPage(QWidget* parent)
  : QWidget(parent)
{
  m_fgColor = Qt::white;
  m_bgColor = Qt::black;

  m_document = new SkinEditorDocument(this);

  initUI();

  // 画布绑定文档
  m_canvas->setDocument(m_document);
  m_canvas->setForegroundColor(m_fgColor);
  m_canvas->setBackgroundColor(m_bgColor);
  // 同步网格/UV 显示状态（按钮在 initToolBar 中已设置初始状态）
  m_canvas->setShowGrid(m_gridBtn->isChecked());
  m_canvas->setShowUVOverlay(m_uvBtn->isChecked());

  // 信号连接：画布
  connect(m_canvas, &SkinTextureCanvas::colorPicked, this, &SkinEditorPage::onColorPicked);
  connect(m_canvas, &SkinTextureCanvas::pixelHovered, this, &SkinEditorPage::onPixelHovered);
  connect(m_canvas, &SkinTextureCanvas::zoomChanged, this, &SkinEditorPage::onZoomChanged);
  connect(m_canvas, &SkinTextureCanvas::imageEdited, this, [this]() {
    m_preview3D->setSkin(m_document->skinImage());
    m_editor3D->setSkin(m_document->skinImage());
  });
  connect(m_canvas, &SkinTextureCanvas::toolChanged, this, [this](DrawTool) {
    updateToolButtons();
  });

  // 信号连接：3D 编辑器（吸管拾取、坐标悬停）
  connect(m_editor3D, &Skin3DWidget::colorPicked, this, &SkinEditorPage::onColorPicked);
  connect(m_editor3D, &Skin3DWidget::pixelHovered, this, &SkinEditorPage::onPixelHovered);

  // 信号连接：文档
  connect(m_document, &SkinEditorDocument::skinChanged, this, [this](const QImage& image) {
    m_preview3D->setSkin(image);
    m_editor3D->setSkin(image);
    updateDirtyIndicator();
    updateCropSelection();
  });
  connect(m_document, &SkinEditorDocument::modelChanged, this, [this](bool slim) {
    QSignalBlocker b1(m_classicRadio);
    QSignalBlocker b2(m_slimRadio);
    if (slim)
      m_slimRadio->setChecked(true);
    else
      m_classicRadio->setChecked(true);
    // 文档模型变更时同步刷新 3D 预览/编辑器（Skin3DWidget 会自动检测模型）
    m_preview3D->setSkin(m_document->skinImage());
    m_editor3D->setSkin(m_document->skinImage());
    m_canvas->update();
    updateCropSelection();
  });
  connect(m_document, &SkinEditorDocument::formatChanged, this, [this](bool is64x64) {
    QSignalBlocker b1(m_format64Radio);
    QSignalBlocker b2(m_format32Radio);
    if (is64x64)
      m_format64Radio->setChecked(true);
    else
      m_format32Radio->setChecked(true);
    updateCropSelection();
  });
  connect(m_document, &SkinEditorDocument::dirtyChanged, this, [this](bool) {
    updateDirtyIndicator();
  });
  connect(m_document, &SkinEditorDocument::canUndoChanged, this, [this](bool) {
    updateUndoRedoButtons();
  });
  connect(m_document, &SkinEditorDocument::canRedoChanged, this, [this](bool) {
    updateUndoRedoButtons();
  });

  // 信号连接：模型/格式单选按钮
  connect(m_classicRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked)
      onModelChanged(false);
  });
  connect(m_slimRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked)
      onModelChanged(true);
  });
  connect(m_format64Radio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked)
      onFormatChanged(true);
  });
  connect(m_format32Radio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked)
      onFormatChanged(false);
  });

  // 信号连接：主题色
  if (ThemeManager::instance())
  {
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &SkinEditorPage::onThemeColorChanged);
    const bool dark = (ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme);
    m_preview3D->setThemeBackground(dark);
    m_editor3D->setThemeBackground(dark);
  }

  // 初始化状态栏
  m_fgColorPreview->setStyleSheet(colorButtonStyle(m_fgColor));
  m_bgColorPreview->setStyleSheet(colorButtonStyle(m_bgColor));
  onZoomChanged(m_canvas->zoom());
  updateUndoRedoButtons();
  updateDirtyIndicator();

  // 默认工具
  setTool(DrawTool::Brush);

  // 初始化截取选区（默认头部前面）
  updateCropSelection();
}

SkinEditorPage::~SkinEditorPage()
{
}

void SkinEditorPage::initializeDefault()
{
  if (!m_document->skinImage().isNull())
    return;

  m_document->loadFromTemplate("Steve");

  m_fgColor = Qt::white;
  m_bgColor = Qt::black;
  m_canvas->setForegroundColor(m_fgColor);
  m_canvas->setBackgroundColor(m_bgColor);
  m_fgColorPreview->setStyleSheet(colorButtonStyle(m_fgColor));
  m_bgColorPreview->setStyleSheet(colorButtonStyle(m_bgColor));

  setTool(DrawTool::Brush);
}

void SkinEditorPage::applyToAccount(const QString& username, const QString& accountType)
{
  m_currentAccount = username;
  m_currentAccountType = accountType;
}

void SkinEditorPage::refreshSkin3DPreview()
{
  // 同步重绘：立即触发 paintGL，确保 GL 渲染在 QGraphicsEffect 移除后立即恢复
  if (m_preview3D)
    m_preview3D->repaint();
  if (m_editor3D)
    m_editor3D->repaint();
}

void SkinEditorPage::onThemeColorChanged(const QString& color)
{
  Q_UNUSED(color);
  if (ThemeManager::instance())
  {
    const bool dark = (ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme);
    m_preview3D->setThemeBackground(dark);
    m_editor3D->setThemeBackground(dark);
  }
}

void SkinEditorPage::initUI()
{
  auto* pageLayout = new QVBoxLayout(this);
  pageLayout->setContentsMargins(0, 0, 0, 0);
  pageLayout->setSpacing(0);

  m_mainLayout = new QHBoxLayout;
  m_mainLayout->setContentsMargins(0, 0, 0, 0);
  m_mainLayout->setSpacing(0);
  pageLayout->addLayout(m_mainLayout);

  initToolBar();
  initCanvasArea();
  initPreviewPanel();
  initStatusBar();
  initShortcuts();
}

void SkinEditorPage::initToolBar()
{
  m_toolBar = new QWidget(this);
  m_toolBar->setFixedWidth(60);
  m_toolBarLayout = new QVBoxLayout(m_toolBar);
  m_toolBarLayout->setContentsMargins(0, 6, 0, 6);
  m_toolBarLayout->setSpacing(4);
  m_mainLayout->addWidget(m_toolBar);

  // 按钮创建辅助
  auto makeBtn = [this](const QString& icon, const QString& tip) -> QPushButton* {
    auto* btn = new QPushButton(m_toolBar);
    btn->setIcon(QIcon(icon));
    btn->setIconSize(QSize(24, 24));
    btn->setFixedSize(40, 40);
    btn->setToolTip(tip);
    m_toolBarLayout->addWidget(btn, 0, Qt::AlignHCenter);
    return btn;
  };

  auto addSep = [this]() {
    auto* sep = new QFrame(m_toolBar);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setFixedHeight(2);
    m_toolBarLayout->addWidget(sep);
  };

  // 分组 1：绘制工具（可选中，互斥）
  m_brushBtn = makeBtn(":/Images/Icons/brush.svg", QStringLiteral("画笔 (B)"));
  m_eraserBtn = makeBtn(":/Images/Icons/eraser.svg", QStringLiteral("橡皮 (E)"));
  m_fillBtn = makeBtn(":/Images/Icons/fill.svg", QStringLiteral("填充 (G)"));
  m_eyedropperBtn = makeBtn(":/Images/Icons/eyedropper.svg", QStringLiteral("吸管 (I)"));
  m_lineBtn = makeBtn(":/Images/Icons/line.svg", QStringLiteral("直线 (L)"));

  for (auto* btn : {m_brushBtn, m_eraserBtn, m_fillBtn, m_eyedropperBtn, m_lineBtn})
  {
    btn->setCheckable(true);
    btn->setAutoExclusive(true);
  }

  addSep();

  // 分组 2：编辑
  m_undoBtn = makeBtn(":/Images/Icons/undo.svg", QStringLiteral("撤销 (Ctrl+Z)"));
  m_redoBtn = makeBtn(":/Images/Icons/redo.svg", QStringLiteral("重做 (Ctrl+Y)"));
  m_clearBtn = makeBtn(":/Images/Icons/clear.svg", QStringLiteral("清除"));

  addSep();

  // 分组 3：显示
  m_gridBtn = makeBtn(":/Images/Icons/grid.svg", QStringLiteral("网格"));
  m_uvBtn = makeBtn(":/Images/Icons/uv_overlay.svg", QStringLiteral("UV 叠加"));
  m_gridBtn->setCheckable(true);
  m_uvBtn->setCheckable(true);
  m_gridBtn->setChecked(true);

  addSep();

  // 分组 4：文件
  m_newBtn = makeBtn(":/Images/Icons/install.svg", QStringLiteral("新建"));
  m_openBtn = makeBtn(":/Images/Icons/folder.svg", QStringLiteral("打开"));
  m_saveBtn = makeBtn(":/Images/Icons/download.svg", QStringLiteral("保存"));
  m_saveAsBtn = makeBtn(":/Images/Icons/download.svg", QStringLiteral("另存为"));

  addSep();

  // 分组 5：账户
  m_loadAccountBtn = makeBtn(":/Images/Icons/folder.svg", QStringLiteral("从账户加载"));
  m_applyAccountBtn = makeBtn(":/Images/Icons/download.svg", QStringLiteral("应用到账户"));

  m_toolBarLayout->addStretch();

  // 信号连接
  connect(m_brushBtn, &QPushButton::clicked, this, [this]() { setTool(DrawTool::Brush); });
  connect(m_eraserBtn, &QPushButton::clicked, this, [this]() { setTool(DrawTool::Eraser); });
  connect(m_fillBtn, &QPushButton::clicked, this, [this]() { setTool(DrawTool::Fill); });
  connect(m_eyedropperBtn, &QPushButton::clicked, this, [this]() { setTool(DrawTool::Eyedropper); });
  connect(m_lineBtn, &QPushButton::clicked, this, [this]() { setTool(DrawTool::Line); });

  connect(m_undoBtn, &QPushButton::clicked, this, &SkinEditorPage::onUndo);
  connect(m_redoBtn, &QPushButton::clicked, this, &SkinEditorPage::onRedo);
  connect(m_clearBtn, &QPushButton::clicked, m_document, &SkinEditorDocument::clearAll);

  // 网格/UV 连接使用 lambda（m_canvas 在 initCanvasArea 中才创建）
  connect(m_gridBtn, &QPushButton::toggled, this, [this](bool checked) {
    if (m_canvas)
      m_canvas->setShowGrid(checked);
  });
  connect(m_uvBtn, &QPushButton::toggled, this, [this](bool checked) {
    if (m_canvas)
      m_canvas->setShowUVOverlay(checked);
  });

  connect(m_newBtn, &QPushButton::clicked, this, &SkinEditorPage::onNewFromTemplate);
  connect(m_openBtn, &QPushButton::clicked, this, &SkinEditorPage::onOpenFile);
  connect(m_saveBtn, &QPushButton::clicked, this, &SkinEditorPage::onSaveFile);
  connect(m_saveAsBtn, &QPushButton::clicked, this, &SkinEditorPage::onSaveAsFile);
  connect(m_loadAccountBtn, &QPushButton::clicked, this, &SkinEditorPage::onLoadFromAccount);
  connect(m_applyAccountBtn, &QPushButton::clicked, this, &SkinEditorPage::onApplyToAccount);
}

void SkinEditorPage::initCanvasArea()
{
  // 中央区域：QStackedWidget 在 2D 画布与 3D 编辑器之间切换
  m_centerStack = new QStackedWidget(this);
  m_mainLayout->addWidget(m_centerStack, 1);

  // ---- 页 0：2D 像素画布（现有方式）----
  auto* scroll2D = new QScrollArea(m_centerStack);
  scroll2D->setWidgetResizable(true);
  scroll2D->setAlignment(Qt::AlignCenter);
  scroll2D->setFrameShape(QFrame::NoFrame);
  m_canvas = new SkinTextureCanvas;
  m_canvas->setMinimumSize(512, 512);
  scroll2D->setWidget(m_canvas);
  m_canvasPage = scroll2D;
  m_centerStack->addWidget(m_canvasPage);

  // ---- 页 1：3D 编辑器（BlockBench 风格，直接立体绘制）----
  // 注意：不用 QScrollArea 包裹 QOpenGLWidget——QOpenGLWidget 在滚动容器中
  // 的渲染在部分 Qt 版本/平台不可靠（漏画/空白）。参考 Blockbench 的 3D 视图
  // （直接用 Canvas 布局，无滚动包裹），让编辑器直接填满页面并随布局缩放。
  auto* editorPage = new QWidget;
  auto* editorLayout = new QVBoxLayout(editorPage);
  editorLayout->setContentsMargins(0, 0, 0, 0);
  editorLayout->setSpacing(0);
  m_editor3D = new Skin3DWidget;
  m_editor3D->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_editor3D->setMinimumSize(320, 400);
  m_editor3D->setInteractionMode(InteractionMode::Paint);
  m_editor3D->setDocument(m_document);
  m_editor3D->setAutoRotate(false);  // 绘制模式下不需要自动旋转（渲染循环另行驱动）
  // 编辑器"画哪层显示哪层"：默认只显示内层（勾选"绘制外层"时才会同步显示外层）。
  // 同时把相机拉近一点，让手臂/腿等窄部位在屏幕上更大、更易精确点击。
  m_editor3D->setOuterLayerVisible(false);
  m_editor3D->resetView(2.2f);  // 拉近相机，便于点击小部位（手臂/腿等 4px 宽部件）
  editorLayout->addWidget(m_editor3D);
  m_editor3DPage = editorPage;
  m_centerStack->addWidget(m_editor3DPage);
}

void SkinEditorPage::initPreviewPanel()
{
  auto* scrollArea = new QScrollArea(this);
  scrollArea->setFixedWidth(320);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  m_mainLayout->addWidget(scrollArea);

  auto* container = new QWidget;
  m_panelLayout = new QVBoxLayout(container);
  m_panelLayout->setContentsMargins(8, 8, 8, 8);
  m_panelLayout->setSpacing(6);
  scrollArea->setWidget(container);

  QFont boldFont = font();
  boldFont.setBold(true);

  // ===== 编辑方式选择（2D 像素 / 3D 立体）=====
  auto* modeLabel = new QLabel(QStringLiteral("编辑方式"));
  modeLabel->setFont(boldFont);
  m_panelLayout->addWidget(modeLabel);

  // 独立容器隔离自动互斥，避免与下方模型/格式单选互相抢占
  auto* modeBox = new QWidget;
  auto* modeLayout = new QHBoxLayout(modeBox);
  modeLayout->setContentsMargins(0, 0, 0, 0);
  modeLayout->setSpacing(6);
  m_mode2DRadio = new QRadioButton(QStringLiteral("2D 像素"), modeBox);
  m_mode3DRadio = new QRadioButton(QStringLiteral("3D 立体"), modeBox);
  m_mode2DRadio->setChecked(true);
  m_mode2DRadio->setToolTip(QStringLiteral("在平面纹理上逐像素绘制（当前方式）"));
  m_mode3DRadio->setToolTip(QStringLiteral("像 BlockBench 一样直接在 3D 模型表面绘制，左键绘制、右键旋转、滚轮缩放"));
  modeLayout->addWidget(m_mode2DRadio);
  modeLayout->addWidget(m_mode3DRadio);
  modeLayout->addStretch();
  m_panelLayout->addWidget(modeBox);

  connect(m_mode2DRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked)
      onEditModeChanged(false);
  });
  connect(m_mode3DRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked)
      onEditModeChanged(true);
  });

  // 笔刷大小（2D / 3D 共用）
  auto* brushRow = new QHBoxLayout;
  brushRow->setSpacing(6);
  auto* brushCaption = new QLabel(QStringLiteral("笔刷:"));
  m_brushSizeCombo = new QComboBox;
  m_brushSizeCombo->addItems({QStringLiteral("1x1"), QStringLiteral("2x2"), QStringLiteral("4x4")});
  m_brushSizeCombo->setCurrentIndex(0);
  m_brushSizeCombo->setToolTip(QStringLiteral("笔刷大小，两种编辑方式共用"));
  brushRow->addWidget(brushCaption);
  brushRow->addWidget(m_brushSizeCombo, 1);
  m_panelLayout->addLayout(brushRow);

  connect(m_brushSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SkinEditorPage::onBrushSizeChanged);

  // ===== 3D 预览块（2D 模式下显示）=====
  m_preview3DBlock = new QWidget;
  auto* previewV = new QVBoxLayout(m_preview3DBlock);
  previewV->setContentsMargins(0, 0, 0, 0);
  previewV->setSpacing(6);

  auto* previewLabel = new QLabel(QStringLiteral("3D 预览"));
  previewLabel->setFont(boldFont);
  previewV->addWidget(previewLabel);

  m_preview3D = new Skin3DWidget;
  m_preview3D->setFixedSize(300, 400);
  previewV->addWidget(m_preview3D, 0, Qt::AlignHCenter);
  m_panelLayout->addWidget(m_preview3DBlock);

  // ===== 迷你 2D 纹理块（3D 模式下显示，BlockBench 的纹理窗口定位）=====
  m_miniTextureBlock = new QWidget;
  auto* miniV = new QVBoxLayout(m_miniTextureBlock);
  miniV->setContentsMargins(0, 0, 0, 0);
  miniV->setSpacing(6);

  auto* miniLabel = new QLabel(QStringLiteral("2D 纹理"));
  miniLabel->setFont(boldFont);
  miniV->addWidget(miniLabel);

  m_miniTexture = new SkinTextureCanvas;
  m_miniTexture->setReadOnly(true);
  m_miniTexture->setDocument(m_document);
  m_miniTexture->setShowGrid(false);
  m_miniTexture->setShowUVOverlay(false);
  m_miniTexture->setFixedSize(140, 140);
  m_miniTexture->setZoom(2.0f);
  miniV->addWidget(m_miniTexture, 0, Qt::AlignHCenter);

  auto* hintLabel = new QLabel(QStringLiteral("左键绘制 · 右键/中键旋转 · 滚轮缩放"));
  hintLabel->setAlignment(Qt::AlignCenter);
  hintLabel->setStyleSheet("color: #888888;");
  miniV->addWidget(hintLabel);
  m_panelLayout->addWidget(m_miniTextureBlock);
  m_miniTextureBlock->setVisible(false);

  // 模型设置区（独立容器隔离自动互斥）
  auto* modelLabel = new QLabel(QStringLiteral("模型"));
  modelLabel->setFont(boldFont);
  m_panelLayout->addWidget(modelLabel);

  auto* modelBox = new QWidget;
  auto* modelLayout = new QHBoxLayout(modelBox);
  modelLayout->setContentsMargins(0, 0, 0, 0);
  modelLayout->setSpacing(6);
  m_classicRadio = new QRadioButton(QStringLiteral("Classic"), modelBox);
  m_slimRadio = new QRadioButton(QStringLiteral("Slim"), modelBox);
  m_classicRadio->setChecked(true);
  modelLayout->addWidget(m_classicRadio);
  modelLayout->addWidget(m_slimRadio);
  modelLayout->addStretch();
  m_panelLayout->addWidget(modelBox);

  // 绘制外层开关（参考 Blockbench 的图层设计：默认只画内层基础皮肤，
  // 勾选后才把画笔落到帽子/外套等外层 overlay 区域，避免"没勾选却在涂外层"）
  m_paintOuterCheck = new QCheckBox(QStringLiteral("绘制外层 (Overlay)"));
  m_paintOuterCheck->setToolTip(QStringLiteral("3D 模式下将画笔落到外层叠加区域（帽子/外套等，纹理 y>=32 部分）。仅 64x64 格式可用"));
  m_panelLayout->addWidget(m_paintOuterCheck);
  connect(m_paintOuterCheck, &QCheckBox::toggled,
          this, &SkinEditorPage::onPaintOuterToggled);

  // 选取部分（参考 Blockbench 的部件选择：3D 模式只显示选中的部位，
  // 便于单独查看/绘制某个部位，其余部位隐藏且不参与拾取）
  auto* visiblePartLabel = new QLabel(QStringLiteral("选取部分"));
  visiblePartLabel->setFont(boldFont);
  m_panelLayout->addWidget(visiblePartLabel);

  m_visiblePartCombo = new QComboBox;
  m_visiblePartCombo->addItem(QStringLiteral("全部（显示整个模型）"));
  m_visiblePartCombo->addItem(QStringLiteral("头部"));
  m_visiblePartCombo->addItem(QStringLiteral("身体"));
  m_visiblePartCombo->addItem(QStringLiteral("右臂"));
  m_visiblePartCombo->addItem(QStringLiteral("左臂"));
  m_visiblePartCombo->addItem(QStringLiteral("右腿"));
  m_visiblePartCombo->addItem(QStringLiteral("左腿"));
  m_visiblePartCombo->setToolTip(QStringLiteral("3D 模式下启用后只显示选中的部位，其余部位隐藏且不可绘制"));
  m_panelLayout->addWidget(m_visiblePartCombo);
  connect(m_visiblePartCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SkinEditorPage::onVisiblePartChanged);

  // 格式设置区（独立容器隔离自动互斥）
  auto* formatLabel = new QLabel(QStringLiteral("格式"));
  formatLabel->setFont(boldFont);
  m_panelLayout->addWidget(formatLabel);

  auto* formatBox = new QWidget;
  auto* formatLayout = new QHBoxLayout(formatBox);
  formatLayout->setContentsMargins(0, 0, 0, 0);
  formatLayout->setSpacing(6);
  m_format64Radio = new QRadioButton(QStringLiteral("64x64"), formatBox);
  m_format32Radio = new QRadioButton(QStringLiteral("64x32"), formatBox);
  m_format64Radio->setChecked(true);
  formatLayout->addWidget(m_format64Radio);
  formatLayout->addWidget(m_format32Radio);
  formatLayout->addStretch();
  m_panelLayout->addWidget(formatBox);

  // 预设皮肤导入区
  initPresetSkins();

  // 截取部分区
  initCropSection();

  // 调色板区
  initPalette();

  m_panelLayout->addStretch();
}

void SkinEditorPage::initStatusBar()
{
  auto* bar = new QWidget(this);
  bar->setFixedHeight(24);
  bar->setStyleSheet("background-color: #f0f0f0;");

  auto* layout = new QHBoxLayout(bar);
  layout->setContentsMargins(8, 0, 8, 0);
  layout->setSpacing(8);

  m_toolLabel = new QLabel(QStringLiteral("画笔"));
  layout->addWidget(m_toolLabel);

  auto* sep1 = new QFrame;
  sep1->setFrameShape(QFrame::VLine);
  sep1->setFixedHeight(14);
  layout->addWidget(sep1);

  m_coordLabel = new QLabel("--");
  layout->addWidget(m_coordLabel);

  auto* sep2 = new QFrame;
  sep2->setFrameShape(QFrame::VLine);
  sep2->setFixedHeight(14);
  layout->addWidget(sep2);

  m_zoomLabel = new QLabel("800%");
  layout->addWidget(m_zoomLabel);

  layout->addStretch();

  m_dirtyLabel = new QLabel("");
  layout->addWidget(m_dirtyLabel);

  if (auto* pageLayout = qobject_cast<QVBoxLayout*>(this->layout()))
    pageLayout->addWidget(bar);
}

void SkinEditorPage::initPalette()
{
  QFont boldFont = font();
  boldFont.setBold(true);

  // 前景/背景色预览
  auto* fbLabel = new QLabel(QStringLiteral("前景色 / 背景色"));
  fbLabel->setFont(boldFont);
  m_panelLayout->addWidget(fbLabel);

  auto* fbLayout = new QHBoxLayout;
  fbLayout->setSpacing(4);
  m_fgColorPreview = new QLabel;
  m_fgColorPreview->setFixedSize(80, 24);
  m_fgColorPreview->setToolTip(QStringLiteral("前景色（左键绘制）"));
  m_bgColorPreview = new QLabel;
  m_bgColorPreview->setFixedSize(80, 24);
  m_bgColorPreview->setToolTip(QStringLiteral("背景色（右键绘制）"));
  fbLayout->addWidget(m_fgColorPreview);
  fbLayout->addWidget(m_bgColorPreview);
  fbLayout->addStretch();
  m_panelLayout->addLayout(fbLayout);

  // 预设颜色网格（8x4 = 32 色）
  auto* presetLabel = new QLabel(QStringLiteral("预设颜色"));
  presetLabel->setFont(boldFont);
  m_panelLayout->addWidget(presetLabel);

  auto* gridLayout = new QGridLayout;
  gridLayout->setSpacing(2);
  const QVector<QColor>& colors = presetColors();
  for (int i = 0; i < colors.size(); ++i)
  {
    const QColor& color = colors[i];
    auto* btn = new QPushButton;
    btn->setFixedSize(24, 24);
    btn->setStyleSheet(colorButtonStyle(color));
    btn->setToolTip(color.name());
    m_presetColorBtns.append(btn);
    gridLayout->addWidget(btn, i / 8, i % 8);

    // 左键设前景色
    connect(btn, &QPushButton::clicked, this, [this, color]() {
      onPresetColorClicked(color);
    });

    // 右键设背景色
    btn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btn, &QWidget::customContextMenuRequested, this, [this, color](const QPoint&) {
      m_bgColor = color;
      m_canvas->setBackgroundColor(color);
      m_bgColorPreview->setStyleSheet(colorButtonStyle(color));
      addRecentColor(color);
    });
  }
  m_panelLayout->addLayout(gridLayout);

  // 最近使用颜色（1x8）
  auto* recentLabel = new QLabel(QStringLiteral("最近使用"));
  recentLabel->setFont(boldFont);
  m_panelLayout->addWidget(recentLabel);

  auto* recentLayout = new QHBoxLayout;
  recentLayout->setSpacing(2);
  for (int i = 0; i < 8; ++i)
  {
    auto* btn = new QPushButton;
    btn->setFixedSize(24, 24);
    btn->setStyleSheet(colorButtonStyle(Qt::white));
    m_recentColorBtns.append(btn);
    m_recentColors.append(Qt::white);
    recentLayout->addWidget(btn);

    // 左键设前景色
    connect(btn, &QPushButton::clicked, this, [this, i]() {
      if (i < m_recentColors.size())
        onPresetColorClicked(m_recentColors[i]);
    });

    // 右键设背景色
    btn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btn, &QWidget::customContextMenuRequested, this, [this, i](const QPoint&) {
      if (i < m_recentColors.size())
      {
        QColor color = m_recentColors[i];
        m_bgColor = color;
        m_canvas->setBackgroundColor(color);
        m_bgColorPreview->setStyleSheet(colorButtonStyle(color));
      }
    });
  }
  recentLayout->addStretch();
  m_panelLayout->addLayout(recentLayout);

  // 自定义颜色按钮
  m_customColorBtn = new QPushButton(QStringLiteral("自定义颜色..."));
  m_panelLayout->addWidget(m_customColorBtn);
  connect(m_customColorBtn, &QPushButton::clicked, this, &SkinEditorPage::onCustomColorClicked);
}

void SkinEditorPage::initPresetSkins()
{
  QFont boldFont = font();
  boldFont.setBold(true);

  auto* presetLabel = new QLabel(QStringLiteral("预设皮肤"));
  presetLabel->setFont(boldFont);
  presetLabel->setToolTip(QStringLiteral("点击缩略图直接导入官方皮肤模板（Steve/Alex 等），导入后会替换当前画布内容"));
  m_panelLayout->addWidget(presetLabel);

  m_presetSkinBlock = new QWidget;
  auto* blockLayout = new QVBoxLayout(m_presetSkinBlock);
  blockLayout->setContentsMargins(0, 0, 0, 0);
  blockLayout->setSpacing(6);

  m_presetSkinGrid = new QGridLayout;
  m_presetSkinGrid->setContentsMargins(0, 0, 0, 0);
  m_presetSkinGrid->setSpacing(6);

  // 官方皮肤模板（与模板资源一一对应，点击即导入）
  static const QStringList presetNames = {
    "Steve", "Alex", "Ari", "Efe", "Kai",
    "Makena", "Noor", "Sunny", "Zuri"
  };

  for (int i = 0; i < presetNames.size(); ++i)
  {
    const QString& name = presetNames[i];
    QImage skin(QStringLiteral(":/Images/Skins/%1.png").arg(name));
    QPixmap thumb = makeHeadThumbnail(skin, 44);

    auto* btn = new QPushButton;
    btn->setFixedSize(52, 52);
    btn->setIcon(QIcon(thumb));
    btn->setIconSize(QSize(44, 44));
    btn->setToolTip(QStringLiteral("导入 %1 皮肤").arg(name));

    m_presetSkinGrid->addWidget(btn, i / 3, i % 3);

    connect(btn, &QPushButton::clicked, this, [this, name]() {
      onPresetSkinClicked(name);
    });
  }

  blockLayout->addLayout(m_presetSkinGrid);
  m_panelLayout->addWidget(m_presetSkinBlock);
}

void SkinEditorPage::onPresetSkinClicked(const QString& name)
{
  if (!m_document->loadFromTemplate(name))
  {
    AppMessageBox::warning(this, QStringLiteral("导入失败"),
                           QStringLiteral("无法加载预设皮肤 %1").arg(name));
    return;
  }
}

void SkinEditorPage::initShortcuts()
{
  auto* sBrush = new QShortcut(QKeySequence(Qt::Key_B), this);
  connect(sBrush, &QShortcut::activated, this, [this]() { setTool(DrawTool::Brush); });

  auto* sEraser = new QShortcut(QKeySequence(Qt::Key_E), this);
  connect(sEraser, &QShortcut::activated, this, [this]() { setTool(DrawTool::Eraser); });

  auto* sFill = new QShortcut(QKeySequence(Qt::Key_G), this);
  connect(sFill, &QShortcut::activated, this, [this]() { setTool(DrawTool::Fill); });

  auto* sEyedropper = new QShortcut(QKeySequence(Qt::Key_I), this);
  connect(sEyedropper, &QShortcut::activated, this, [this]() { setTool(DrawTool::Eyedropper); });

  auto* sLine = new QShortcut(QKeySequence(Qt::Key_L), this);
  connect(sLine, &QShortcut::activated, this, [this]() { setTool(DrawTool::Line); });

  auto* sUndo = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Z")), this);
  connect(sUndo, &QShortcut::activated, this, &SkinEditorPage::onUndo);

  auto* sRedo = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Y")), this);
  connect(sRedo, &QShortcut::activated, this, &SkinEditorPage::onRedo);

  auto* sRedo2 = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")), this);
  connect(sRedo2, &QShortcut::activated, this, &SkinEditorPage::onRedo);

  auto* sCopy = new QShortcut(QKeySequence(QStringLiteral("Ctrl+C")), this);
  connect(sCopy, &QShortcut::activated, this, &SkinEditorPage::onCopySelected);

  auto* sPaste = new QShortcut(QKeySequence(QStringLiteral("Ctrl+V")), this);
  connect(sPaste, &QShortcut::activated, this, &SkinEditorPage::onPasteFromClipboard);

  auto* sCut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+X")), this);
  connect(sCut, &QShortcut::activated, this, &SkinEditorPage::onCutSelected);

  auto* sDelete = new QShortcut(QKeySequence(Qt::Key_Delete), this);
  connect(sDelete, &QShortcut::activated, this, &SkinEditorPage::onDeleteSelected);

  auto* sSelectAll = new QShortcut(QKeySequence(QStringLiteral("Ctrl+A")), this);
  connect(sSelectAll, &QShortcut::activated, this, &SkinEditorPage::onSelectAll);
}

void SkinEditorPage::setTool(DrawTool tool)
{
  m_canvas->setTool(tool);
  m_editor3D->setTool(tool);
  updateToolButtons();
}

void SkinEditorPage::updateToolButtons()
{
  DrawTool tool = m_canvas->tool();
  m_brushBtn->setChecked(tool == DrawTool::Brush);
  m_eraserBtn->setChecked(tool == DrawTool::Eraser);
  m_fillBtn->setChecked(tool == DrawTool::Fill);
  m_eyedropperBtn->setChecked(tool == DrawTool::Eyedropper);
  m_lineBtn->setChecked(tool == DrawTool::Line);
  m_toolLabel->setText(toolName(tool));
}

void SkinEditorPage::onNewFromTemplate()
{
  bool ok = false;
  QString choice = AppInputDialog::getItem(this, QStringLiteral("新建皮肤"),
                                          QStringLiteral("选择模板:"),
                                          templateNames(), 0, false, &ok);
  if (!ok || choice.isEmpty())
    return;

  if (choice == QStringLiteral("空白"))
    m_document->loadBlank(true);
  else
    m_document->loadFromTemplate(choice);
}

void SkinEditorPage::onOpenFile()
{
  QString path = AppFileDialog::getOpenFileName(this, QStringLiteral("打开皮肤"),
                                              QString(),
                                              QStringLiteral("PNG 图片 (*.png)"));
  if (!path.isEmpty())
    m_document->loadFromFile(path);
}

void SkinEditorPage::onSaveFile()
{
  if (m_document->filePath().isEmpty())
    onSaveAsFile();
  else
    m_document->save();
}

void SkinEditorPage::onSaveAsFile()
{
  QString path = AppFileDialog::getSaveFileName(this, QStringLiteral("保存皮肤"),
                                              QString(),
                                              QStringLiteral("PNG 图片 (*.png)"));
  if (!path.isEmpty())
    m_document->saveToFile(path);
}

void SkinEditorPage::onLoadFromAccount()
{
  if (m_currentAccount.isEmpty())
  {
    AppMessageBox::warning(this, QStringLiteral("提示"),
                         QStringLiteral("请先在账户管理页选择一个账户"));
    return;
  }

  // 优先级 1：离线账户从 Images/Skins/{用户名}.png 加载
  QImage offlineSkin;
  if (SkinDownloader::loadOfflineSkin(m_currentAccount, offlineSkin))
  {
    m_document->loadFromImage(offlineSkin);
    return;
  }

  // 优先级 2：从 SkinDownloader 缓存加载（正版/第三方账户下载的皮肤）
  QString path = SkinDownloader::getCachePath(m_currentAccount);
  QFileInfo info(path);
  if (info.exists())
  {
    m_document->loadFromFile(path);
    return;
  }

  // 优先级 3：回退到默认皮肤
  QImage img = SkinDownloader::getDefaultSkinForUser(m_currentAccount);
  if (!img.isNull())
    m_document->loadFromImage(img);
  else
    AppMessageBox::warning(this, QStringLiteral("提示"),
                         QStringLiteral("无法获取默认皮肤"));
}

void SkinEditorPage::onApplyToAccount()
{
  if (m_currentAccount.isEmpty())
  {
    AppMessageBox::warning(this, QStringLiteral("提示"),
                         QStringLiteral("请先在账户管理页选择一个账户"));
    return;
  }

  auto result = AppMessageBox::question(this, QStringLiteral("确认"),
                                      QStringLiteral("确定要将此皮肤应用到账户 %1 吗？")
                                        .arg(m_currentAccount));
  if (result != AppMessageBox::Yes)
    return;

  // 根据账户类型决定保存位置：
  //   - 离线账户：保存到 Images/Skins/{用户名}.png（离线皮肤目录）
  //   - 正版/第三方账户：保存到 SkinDownloader 缓存目录
  QString savePath;
  const bool isOffline = (m_currentAccountType == QStringLiteral("离线") ||
                          m_currentAccountType.isEmpty());
  if (isOffline)
  {
    savePath = SkinDownloader::getOfflineSkinPath(m_currentAccount);
    // 确保离线皮肤目录存在
    QDir().mkpath(SkinDownloader::offlineSkinsDir());
  }
  else
  {
    savePath = SkinDownloader::getCachePath(m_currentAccount);
    QDir().mkpath(QFileInfo(savePath).absolutePath());
  }

  if (m_document->saveToFile(savePath))
  {
    emit skinAppliedToAccount(m_currentAccount);
    AppMessageBox::information(this, QStringLiteral("成功"),
                             QStringLiteral("皮肤已成功应用到账户"));
  }
  else
    AppMessageBox::warning(this, QStringLiteral("失败"),
                         QStringLiteral("皮肤保存失败，请重试"));
}

void SkinEditorPage::onUndo()
{
  m_document->undo();
}

void SkinEditorPage::onRedo()
{
  m_document->redo();
}

void SkinEditorPage::updateUndoRedoButtons()
{
  m_undoBtn->setEnabled(m_document->canUndo());
  m_redoBtn->setEnabled(m_document->canRedo());
}

void SkinEditorPage::onColorPicked(const QColor& color)
{
  m_fgColor = color;
  m_canvas->setForegroundColor(color);
  m_editor3D->setForegroundColor(color);
  m_fgColorPreview->setStyleSheet(colorButtonStyle(color));
  addRecentColor(color);
}

void SkinEditorPage::onPresetColorClicked(const QColor& color)
{
  m_fgColor = color;
  m_canvas->setForegroundColor(color);
  m_editor3D->setForegroundColor(color);
  m_fgColorPreview->setStyleSheet(colorButtonStyle(color));
  addRecentColor(color);
}

void SkinEditorPage::onCustomColorClicked()
{
  QColor color = AppColorDialog::getColor(m_fgColor, this, QStringLiteral("选择自定义颜色"));
  if (color.isValid())
  {
    m_fgColor = color;
    m_canvas->setForegroundColor(color);
    m_editor3D->setForegroundColor(color);
    m_fgColorPreview->setStyleSheet(colorButtonStyle(color));
    addRecentColor(color);
  }
}

void SkinEditorPage::addRecentColor(const QColor& color)
{
  m_recentColors.removeAll(color);
  m_recentColors.prepend(color);
  while (m_recentColors.size() > 8)
    m_recentColors.removeLast();

  for (int i = 0; i < m_recentColorBtns.size() && i < m_recentColors.size(); ++i)
  {
    m_recentColorBtns[i]->setStyleSheet(colorButtonStyle(m_recentColors[i]));
  }
}

void SkinEditorPage::onModelChanged(bool slim)
{
  m_document->setSlim(slim);
  // 切换 Classic/Slim 模型后刷新 3D 预览（Skin3DWidget 会通过 detectSkinModel 自动检测）
  m_preview3D->setSkin(m_document->skinImage());
}

void SkinEditorPage::onFormatChanged(bool is64x64)
{
  // 从 64x64 切换到 64x32 会丢弃外层(overlay)内容，需用户确认
  if (!is64x64 && m_document->is64x64())
  {
    auto result = AppMessageBox::question(this, QStringLiteral("确认"),
                                        QStringLiteral("切换到 64x32 格式将丢弃外层(overlay)内容，确定继续吗？"));
    if (result != AppMessageBox::Yes)
    {
      // 恢复单选按钮状态
      QSignalBlocker b(m_format64Radio);
      m_format64Radio->setChecked(true);
      return;
    }
  }
  m_document->setFormat64x64(is64x64);
}

void SkinEditorPage::onEditModeChanged(bool use3D)
{
  m_editMode3D = use3D;

  // 中央区域：2D 画布 / 3D 编辑器
  m_centerStack->setCurrentIndex(use3D ? 1 : 0);

  // 右侧面板：3D 预览块（2D 模式）↔ 迷你纹理块（3D 模式）
  m_preview3DBlock->setVisible(!use3D);
  m_miniTextureBlock->setVisible(use3D);

  // "绘制外层"是 3D 编辑器专属，2D 模式禁用
  if (m_paintOuterCheck)
  {
    // 禁用条件：非 3D 模式 或 64x32 格式（updateCropSelection 已处理 64x32 联动）
    if (!use3D)
    {
      m_paintOuterCheck->setEnabled(false);
    }
    else
    {
      m_paintOuterCheck->setEnabled(m_document ? m_document->is64x64() : true);
    }
  }

  // "选取部分"同样是 3D 编辑器专属，2D 模式禁用
  if (m_visiblePartCombo)
    m_visiblePartCombo->setEnabled(use3D);

  // 切换后强制刷新 3D 渲染（QOpenGLWidget 在隐藏/显示切换后需手动重绘）
  if (use3D)
  {
    // 3D 编辑器首次可见（位于 centerStack 页1）→ GL 上下文初始化与纹理上传
    // 是异步/分阶段的，单次 repaint 不够；多次延迟重绘覆盖布局激活、showEvent、
    // initializeGL、纹理上传完成等关键时点，确保 paintGL 真正画出模型。
    m_editor3D->repaint();
    m_miniTexture->update();
    QTimer::singleShot(0, this, [this]() {
      if (m_editor3D && m_editMode3D) { m_editor3D->repaint(); }
    });
    QTimer::singleShot(50, this, [this]() {
      if (m_editor3D && m_editMode3D) { m_editor3D->repaint(); }
    });
    QTimer::singleShot(200, this, [this]() {
      if (m_editor3D && m_editMode3D) { m_editor3D->repaint(); }
    });
    QTimer::singleShot(500, this, [this]() {
      if (m_editor3D && m_editMode3D) { m_editor3D->repaint(); }
    });
  }
  else
  {
    m_preview3D->repaint();
  }
}

void SkinEditorPage::onBrushSizeChanged(int index)
{
  static const int sizes[3] = {1, 2, 4};
  const int size = sizes[qBound(0, index, 2)];
  m_canvas->setBrushSize(size);
  m_editor3D->setBrushSize(size);
}

void SkinEditorPage::onPaintOuterToggled(bool checked)
{
  // 编辑器采用 Blockbench 的"画哪层显示哪层"原则：
  // 勾选绘制外层时同步显示外层（否则外层 UV 画上去看不到，困惑"画与显示不一致"）；
  // 不勾选时隐藏外层（避免看到外层却画到内层的反向困惑）。
  m_editor3D->setPaintOuter(checked);
  m_editor3D->setOuterLayerVisible(checked);
}

void SkinEditorPage::onVisiblePartChanged(int index)
{
  // combo 索引 0=全部；1~6 对应 0~5 部位（与 PickResult::partIndex 一致）
  m_editor3D->setVisiblePart(index - 1);
}

void SkinEditorPage::initCropSection()
{
  QFont boldFont = font();
  boldFont.setBold(true);

  auto* cropLabel = new QLabel(QStringLiteral("截取部分"));
  cropLabel->setFont(boldFont);
  m_panelLayout->addWidget(cropLabel);

  // 部件选择
  auto* partRow = new QHBoxLayout;
  partRow->setSpacing(6);
  auto* partCaption = new QLabel(QStringLiteral("部件:"));
  m_cropPartCombo = new QComboBox;
  m_cropPartCombo->addItems({QStringLiteral("头部"), QStringLiteral("身体"),
                             QStringLiteral("右臂"), QStringLiteral("左臂"),
                             QStringLiteral("右腿"), QStringLiteral("左腿")});
  m_cropPartCombo->setToolTip(QStringLiteral("选择要截取的皮肤部件"));
  partRow->addWidget(partCaption);
  partRow->addWidget(m_cropPartCombo, 1);
  m_panelLayout->addLayout(partRow);

  // 面选择
  auto* faceRow = new QHBoxLayout;
  faceRow->setSpacing(6);
  auto* faceCaption = new QLabel(QStringLiteral("面:"));
  m_cropFaceCombo = new QComboBox;
  m_cropFaceCombo->addItems({QStringLiteral("前"), QStringLiteral("后"),
                             QStringLiteral("左"), QStringLiteral("右"),
                             QStringLiteral("上"), QStringLiteral("下")});
  m_cropFaceCombo->setToolTip(QStringLiteral("选择要截取的面，如头部的前面"));
  faceRow->addWidget(faceCaption);
  faceRow->addWidget(m_cropFaceCombo, 1);
  m_panelLayout->addLayout(faceRow);

  // 外层勾选
  m_cropOuterCheck = new QCheckBox(QStringLiteral("外层 (Overlay)"));
  m_cropOuterCheck->setToolTip(QStringLiteral("截取对应部件的外层(overlay)区域，仅 64x64 格式可用"));
  m_panelLayout->addWidget(m_cropOuterCheck);

  // 选区缩略图
  m_cropPreview = new QLabel(QStringLiteral("请选择部位"));
  m_cropPreview->setFixedSize(100, 100);
  m_cropPreview->setAlignment(Qt::AlignCenter);
  m_cropPreview->setStyleSheet("border: 1px solid #888; background-color: #2a2a2a;");
  m_panelLayout->addWidget(m_cropPreview, 0, Qt::AlignHCenter);

  m_cropCoordLabel = new QLabel("--");
  m_cropCoordLabel->setAlignment(Qt::AlignCenter);
  m_panelLayout->addWidget(m_cropCoordLabel);

  // 截取按钮
  m_cropBtn = new QPushButton(QStringLiteral("截取所选区域"));
  m_cropBtn->setToolTip(QStringLiteral("将所选部位区域截取出来，可复制到剪贴板或保存为 PNG"));
  m_panelLayout->addWidget(m_cropBtn);

  // 信号连接
  connect(m_cropPartCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { updateCropSelection(); });
  connect(m_cropFaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { updateCropSelection(); });
  connect(m_cropOuterCheck, &QCheckBox::toggled, this, [this](bool) { updateCropSelection(); });
  connect(m_cropBtn, &QPushButton::clicked, this, &SkinEditorPage::onCropSelected);
}

QRect SkinEditorPage::currentCropRect() const
{
  if (m_document == nullptr || m_cropPartCombo == nullptr || m_cropFaceCombo == nullptr)
    return QRect();
  const int part = m_cropPartCombo->currentIndex();
  const int face = m_cropFaceCombo->currentIndex();
  const bool overlay = (m_cropOuterCheck != nullptr) && m_cropOuterCheck->isChecked();
  return partFaceRect(part, face, overlay, m_document->isSlim(), m_document->is64x64());
}

void SkinEditorPage::updateCropSelection()
{
  if (m_document == nullptr)
    return;

  // 64x32 格式不支持外层：禁用并取消勾选
  const bool is64x64 = m_document->is64x64();
  if (m_cropOuterCheck != nullptr)
  {
    m_cropOuterCheck->setEnabled(is64x64);
    if (!is64x64 && m_cropOuterCheck->isChecked())
    {
      QSignalBlocker b(m_cropOuterCheck);
      m_cropOuterCheck->setChecked(false);
    }
  }
  // "绘制外层"开关同样仅在 64x64 格式可用
  if (m_paintOuterCheck != nullptr)
  {
    m_paintOuterCheck->setEnabled(is64x64);
    if (!is64x64 && m_paintOuterCheck->isChecked())
    {
      QSignalBlocker b(m_paintOuterCheck);
      m_paintOuterCheck->setChecked(false);
      // 信号被 block，需手动同步编辑器状态
      m_editor3D->setPaintOuter(false);
    }
  }

  const QRect rect = currentCropRect();
  if (m_canvas != nullptr)
    m_canvas->setSelectionRect(rect);
  if (m_miniTexture != nullptr)
    m_miniTexture->setSelectionRect(rect);

  if (rect.isEmpty())
  {
    if (m_cropPreview != nullptr)
    {
      m_cropPreview->setPixmap(QPixmap());
      m_cropPreview->setText(QStringLiteral("请选择部位"));
    }
    if (m_cropCoordLabel != nullptr)
      m_cropCoordLabel->setText("--");
    return;
  }

  if (m_cropPreview != nullptr)
  {
    QImage crop = m_document->skinImage().copy(rect);
    if (!crop.isNull())
    {
      const int maxSide = 96;
      const int scale = qMax(1, maxSide / qMax(crop.width(), crop.height()));
      QImage scaled = crop.scaled(crop.width() * scale, crop.height() * scale,
                                  Qt::IgnoreAspectRatio, Qt::FastTransformation);
      m_cropPreview->setText(QString());
      m_cropPreview->setPixmap(QPixmap::fromImage(scaled));
    }
  }

  if (m_cropCoordLabel != nullptr)
  {
    m_cropCoordLabel->setText(
      QStringLiteral("X:%1-%2  Y:%3-%4  %5x%6")
        .arg(rect.x()).arg(rect.x() + rect.width())
        .arg(rect.y()).arg(rect.y() + rect.height())
        .arg(rect.width()).arg(rect.height()));
  }
}

void SkinEditorPage::onCropSelected()
{
  if (m_document == nullptr || m_document->skinImage().isNull())
  {
    AppMessageBox::warning(this, QStringLiteral("提示"),
                         QStringLiteral("当前没有可截取的皮肤内容"));
    return;
  }

  const QRect rect = currentCropRect();
  if (rect.isEmpty())
  {
    AppMessageBox::warning(this, QStringLiteral("提示"),
                         QStringLiteral("所选区域无效，64x32 格式不支持外层(overlay)"));
    return;
  }

  QImage crop = m_document->skinImage().copy(rect);
  if (crop.isNull())
    return;

  // 截取结果预览对话框
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("截取结果"));
  dialog.setMinimumWidth(300);

  auto* layout = new QVBoxLayout(&dialog);
  layout->setSpacing(8);

  const int maxSide = 256;
  const int scale = qMax(1, maxSide / qMax(crop.width(), crop.height()));
  QImage preview = crop.scaled(crop.width() * scale, crop.height() * scale,
                               Qt::IgnoreAspectRatio, Qt::FastTransformation);
  auto* imgLabel = new QLabel;
  imgLabel->setAlignment(Qt::AlignCenter);
  imgLabel->setStyleSheet("border: 1px solid #888; background-color: #2a2a2a;");
  imgLabel->setPixmap(QPixmap::fromImage(preview));
  layout->addWidget(imgLabel, 0, Qt::AlignHCenter);

  auto* infoLabel = new QLabel(
    QStringLiteral("%1 x %2 像素\n坐标 X:%3-%4  Y:%5-%6")
      .arg(crop.width()).arg(crop.height())
      .arg(rect.x()).arg(rect.x() + rect.width())
      .arg(rect.y()).arg(rect.y() + rect.height()));
  infoLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(infoLabel);

  auto* copyBtn = new QPushButton(QStringLiteral("复制到剪贴板"));
  copyBtn->setToolTip(QStringLiteral("将截取区域以透明 PNG 复制到剪贴板"));
  auto* saveBtn = new QPushButton(QStringLiteral("另存为 PNG..."));
  auto* closeBtn = new QPushButton(QStringLiteral("关闭"));

  auto* btnLayout = new QHBoxLayout;
  btnLayout->addWidget(copyBtn);
  btnLayout->addWidget(saveBtn);
  btnLayout->addWidget(closeBtn);
  layout->addLayout(btnLayout);

  int result = 0;  // 0=关闭 1=复制 2=保存
  connect(copyBtn, &QPushButton::clicked, &dialog, [&result, &dialog]() {
    result = 1;
    dialog.accept();
  });
  connect(saveBtn, &QPushButton::clicked, &dialog, [&result, &dialog]() {
    result = 2;
    dialog.accept();
  });
  connect(closeBtn, &QPushButton::clicked, &dialog, [&dialog]() { dialog.reject(); });

  if (dialog.exec() != QDialog::Accepted)
    return;

  if (result == 1)
  {
    QGuiApplication::clipboard()->setImage(crop);
    AppMessageBox::information(this, QStringLiteral("已复制"),
                             QStringLiteral("选区已以透明 PNG 复制到剪贴板"));
  }
  else if (result == 2)
  {
    QString path = AppFileDialog::getSaveFileName(this, QStringLiteral("保存截取区域"),
                                                QString(),
                                                QStringLiteral("PNG 图片 (*.png)"));
    if (!path.isEmpty())
    {
      if (crop.save(path))
      {
        AppMessageBox::information(this, QStringLiteral("已保存"),
                                 QStringLiteral("截取区域已保存到:\n%1").arg(path));
      }
      else
      {
        AppMessageBox::warning(this, QStringLiteral("失败"), QStringLiteral("保存失败"));
      }
    }
  }
}

void SkinEditorPage::onPixelHovered(int x, int y)
{
  if (x < 0 || y < 0)
    m_coordLabel->setText("--");
  else
    m_coordLabel->setText(QStringLiteral("X: %1, Y: %2").arg(x).arg(y));
}

void SkinEditorPage::onZoomChanged(float zoom)
{
  m_zoomLabel->setText(QStringLiteral("%1%").arg(static_cast<int>(zoom * 100)));
}

void SkinEditorPage::updateDirtyIndicator()
{
  m_dirtyLabel->setText(m_document->isDirty() ? QStringLiteral("*") : QStringLiteral(""));
}

void SkinEditorPage::onCopySelected()
{
  if (m_document == nullptr || m_document->skinImage().isNull())
    return;

  const QRect rect = m_canvas->selectionRect();
  if (!m_canvas->hasSelection() || rect.isEmpty())
    return;

  QImage crop = m_document->skinImage().copy(rect);
  if (!crop.isNull())
    QGuiApplication::clipboard()->setImage(crop);
}

void SkinEditorPage::onPasteFromClipboard()
{
  if (m_document == nullptr || m_document->skinImage().isNull())
    return;

  const QImage clipImg = QGuiApplication::clipboard()->image();
  if (clipImg.isNull())
    return;

  const QRect rect = m_canvas->selectionRect();
  const QRect target = rect.isValid() ? rect : QRect(0, 0, m_document->skinImage().width(),
                                                     m_document->skinImage().height());

  QImage skin = m_document->skinImage();
  QPainter painter(&skin);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  painter.drawImage(target.topLeft(), clipImg, QRect(0, 0, qMin(clipImg.width(), target.width()),
                                                     qMin(clipImg.height(), target.height())));
  painter.end();

  m_document->loadFromImage(skin);
}

void SkinEditorPage::onCutSelected()
{
  onCopySelected();
  onDeleteSelected();
}

void SkinEditorPage::onDeleteSelected()
{
  if (m_document == nullptr || m_document->skinImage().isNull())
    return;

  const QRect rect = m_canvas->selectionRect();
  if (!m_canvas->hasSelection() || rect.isEmpty())
    return;

  QVector<QPoint> points;
  for (int y = rect.top(); y < rect.bottom(); ++y)
    for (int x = rect.left(); x < rect.right(); ++x)
      points.append(QPoint(x, y));

  m_document->setPixels(points, Qt::transparent);
}

void SkinEditorPage::onSelectAll()
{
  if (m_document == nullptr || m_document->skinImage().isNull())
    return;

  const QRect fullRect(0, 0, m_document->skinImage().width(), m_document->skinImage().height());
  m_canvas->setSelectionRect(fullRect);
  if (m_miniTexture != nullptr)
    m_miniTexture->setSelectionRect(fullRect);
}
