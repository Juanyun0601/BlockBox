/**
 * @file   SkinEditorPage.h
 * @brief  Minecraft 皮肤制作器主页面类声明
 * @author BlockBox Team
 * @date   2026-07-22
 *
 * 整合文档模型、2D 像素画布、UV 叠加层与 3D 预览控件，提供完整的皮肤编辑体验。
 * 左侧为工具栏，中央为可缩放的像素画布，右侧为 3D 预览与模型/格式/调色板设置面板，
 * 底部为状态栏。
 */

#pragma once

#include <QColor>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

#include <QGridLayout>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QVBoxLayout;

class Skin3DWidget;
class SkinEditorDocument;
class SkinTextureCanvas;

enum class DrawTool;

/**
 * @brief Minecraft 皮肤制作器主页面
 *
 * 继承 QWidget，作为启动器中的独立页面。持有 SkinEditorDocument 所有权，
 * 绑定 SkinTextureCanvas 进行像素级编辑，并通过 Skin3DWidget 实时预览效果。
 * 支持画笔/橡皮/填充/吸管/直线五种工具、撤销重做、调色板、模型与格式切换，
 * 以及从账户加载/应用到账户等功能。
 */
class SkinEditorPage : public QWidget
{
  Q_OBJECT

public:
  explicit SkinEditorPage(QWidget* parent = nullptr);
  ~SkinEditorPage() override;

  /**
   * @brief 由 MainWindow 在显示页面时调用，懒加载默认 Steve 皮肤
   *
   * 若文档已有内容则不重复加载。
   */
  void initializeDefault();

  /**
   * @brief 设置当前选中账户信息（由 MainWindow 传入）
   * @param username 当前账户用户名
   * @param accountType 当前账户类型（"离线"、"微软正版"、"第三方"）
   *
   * 账户类型决定皮肤保存位置：
   *   - 离线账户：保存到 Images/Skins/{username}.png（离线皮肤目录）
   *   - 正版/第三方账户：保存到 SkinDownloader 缓存目录
   */
  void applyToAccount(const QString& username, const QString& accountType = QString());

  /**
   * @brief 强制刷新 3D 皮肤预览控件（同步重绘）
   *
   * 用于页面切换动画结束后触发 Skin3DWidget 重绘，
   * 解决 QGraphicsEffect 移除后 QOpenGLWidget 不自动重绘的问题。
   */
  void refreshSkin3DPreview();

signals:
  /**
   * @brief 请求返回主页面（如点击返回按钮时发出）
   */
  void backToMainRequested();

  /**
   * @brief 皮肤成功应用到账户后发出
   * @param username 应用到的账户名
   */
  void skinAppliedToAccount(const QString& username);

public slots:
  /**
   * @brief 主题色变更槽
   * @param color 新的主题色（十六进制字符串）
   */
  void onThemeColorChanged(const QString& color);

private:
  // 布局初始化
  void initUI();
  void initToolBar();       ///< 左侧工具栏
  void initCanvasArea();    ///< 中央画布区
  void initPreviewPanel();  ///< 右侧预览与设置面板
  void initStatusBar();     ///< 底部状态栏
  void initPalette();       ///< 调色板
  void initPresetSkins();   ///< 预设皮肤导入
  void initShortcuts();     ///< 快捷键

  // 工具切换
  void setTool(DrawTool tool);  ///< 设置当前工具，更新按钮 checked 状态
  void updateToolButtons();     ///< 更新工具按钮选中状态

  // 文件操作
  void onNewFromTemplate();  ///< 新建：弹出模板选择对话框
  void onOpenFile();         ///< 打开 PNG
  void onSaveFile();         ///< 保存
  void onSaveAsFile();       ///< 另存为
  void onLoadFromAccount();  ///< 从当前账户加载
  void onApplyToAccount();   ///< 应用到当前账户

  // 撤销/重做
  void onUndo();
  void onRedo();
  void updateUndoRedoButtons();

  // 颜色
  void onColorPicked(const QColor& color);          ///< 吸管拾取
  void onPresetColorClicked(const QColor& color);   ///< 预设色（左键设前景）
  void onCustomColorClicked();                      ///< 自定义颜色
  void addRecentColor(const QColor& color);

  // 模型/格式
  void onModelChanged(bool slim);
  void onFormatChanged(bool is64x64);

  // 编辑方式（2D 像素 / 3D 立体）
  void onEditModeChanged(bool use3D);
  void onBrushSizeChanged(int index);
  void onPaintOuterToggled(bool checked);
  void onVisiblePartChanged(int index);  ///< "选取部分"下拉：3D 模式只显示选中部位

  // 预设皮肤
  void onPresetSkinClicked(const QString& name);  ///< 点击预设皮肤缩略图，直接加载该模板

  // 截取部分
  void initCropSection();                  ///< 初始化"截取部分"面板
  QRect currentCropRect() const;           ///< 根据当前选择计算 UV 选区
  void updateCropSelection();              ///< 选区变更时同步画布高亮与缩略图
  void onCropSelected();                   ///< 截取按钮：弹窗预览并复制/保存

  // 编辑快捷键
  void onCopySelected();                   ///< Ctrl+C：复制选区像素到剪贴板
  void onPasteFromClipboard();             ///< Ctrl+V：从剪贴板粘贴到画布
  void onCutSelected();                    ///< Ctrl+X：剪切选区像素
  void onDeleteSelected();                 ///< Delete：删除选区像素
  void onSelectAll();                      ///< Ctrl+A：全选整个纹理

  // 状态栏更新
  void onPixelHovered(int x, int y);
  void onZoomChanged(float zoom);
  void updateDirtyIndicator();

  // 主布局
  QHBoxLayout* m_mainLayout = nullptr;
  QWidget* m_toolBar = nullptr;
  QVBoxLayout* m_toolBarLayout = nullptr;
  QVBoxLayout* m_panelLayout = nullptr;  ///< 右侧面板内容布局（供 initPalette 使用）

  // 绘制工具按钮
  QPushButton* m_brushBtn = nullptr;
  QPushButton* m_eraserBtn = nullptr;
  QPushButton* m_fillBtn = nullptr;
  QPushButton* m_eyedropperBtn = nullptr;
  QPushButton* m_lineBtn = nullptr;

  // 编辑按钮
  QPushButton* m_undoBtn = nullptr;
  QPushButton* m_redoBtn = nullptr;
  QPushButton* m_clearBtn = nullptr;

  // 显示按钮
  QPushButton* m_gridBtn = nullptr;
  QPushButton* m_uvBtn = nullptr;

  // 文件操作按钮
  QPushButton* m_newBtn = nullptr;
  QPushButton* m_openBtn = nullptr;
  QPushButton* m_saveBtn = nullptr;
  QPushButton* m_saveAsBtn = nullptr;
  QPushButton* m_loadAccountBtn = nullptr;
  QPushButton* m_applyAccountBtn = nullptr;

  // 中央画布与文档
  SkinTextureCanvas* m_canvas = nullptr;
  SkinEditorDocument* m_document = nullptr;
  Skin3DWidget* m_preview3D = nullptr;

  // 编辑方式切换（2D / 3D）
  QStackedWidget* m_centerStack = nullptr;  ///< 中央区域：页0=2D 画布，页1=3D 编辑器
  QWidget* m_canvasPage = nullptr;          ///< 2D 画布所在滚动区域
  QWidget* m_editor3DPage = nullptr;        ///< 3D 编辑器所在滚动区域
  Skin3DWidget* m_editor3D = nullptr;       ///< 3D 编辑器（Paint 交互模式）
  SkinTextureCanvas* m_miniTexture = nullptr; ///< 3D 模式下的只读 2D 纹理小视图
  QWidget* m_preview3DBlock = nullptr;      ///< 右侧 3D 预览块（2D 模式显示）
  QWidget* m_miniTextureBlock = nullptr;    ///< 右侧迷你纹理块（3D 模式显示）
  QRadioButton* m_mode2DRadio = nullptr;
  QRadioButton* m_mode3DRadio = nullptr;
  QComboBox* m_brushSizeCombo = nullptr;
  QCheckBox* m_paintOuterCheck = nullptr;  ///< "绘制外层 (Overlay)"勾选（3D 模式图层切换）
  QComboBox* m_visiblePartCombo = nullptr; ///< "选取部分"下拉（-1 全部 / 0~5 对应部位，3D 模式生效）
  bool m_editMode3D = false;                ///< 当前是否 3D 编辑模式

  // 右侧面板控件
  QRadioButton* m_classicRadio = nullptr;
  QRadioButton* m_slimRadio = nullptr;
  QRadioButton* m_format64Radio = nullptr;
  QRadioButton* m_format32Radio = nullptr;

  // 截取部分控件
  QComboBox* m_cropPartCombo = nullptr;   ///< 部件（头部/身体/右臂/左臂/右腿/左腿）
  QComboBox* m_cropFaceCombo = nullptr;   ///< 面（前/后/左/右/上/下）
  QCheckBox* m_cropOuterCheck = nullptr;  ///< 是否勾选外层(overlay)
  QLabel* m_cropPreview = nullptr;        ///< 选区缩略图
  QLabel* m_cropCoordLabel = nullptr;     ///< 选区坐标信息
  QPushButton* m_cropBtn = nullptr;       ///< 截取按钮

  // 调色板
  QVector<QPushButton*> m_presetColorBtns;
  QPushButton* m_customColorBtn = nullptr;
  QVector<QPushButton*> m_recentColorBtns;
  QVector<QColor> m_recentColors;
  QLabel* m_fgColorPreview = nullptr;
  QLabel* m_bgColorPreview = nullptr;

  // 预设皮肤
  QWidget* m_presetSkinBlock = nullptr;  ///< 预设皮肤导入区域（右侧面板）
  QGridLayout* m_presetSkinGrid = nullptr; ///< 预设皮肤缩略图网格

  // 状态栏
  QLabel* m_toolLabel = nullptr;
  QLabel* m_coordLabel = nullptr;
  QLabel* m_zoomLabel = nullptr;
  QLabel* m_dirtyLabel = nullptr;

  // 颜色状态
  QColor m_fgColor;  ///< 前景色（默认白色）
  QColor m_bgColor;  ///< 背景色（默认黑色）

  // 当前账户
  QString m_currentAccount;
  QString m_currentAccountType;  ///< 账户类型（"离线"、"微软正版"、"第三方"）
};

