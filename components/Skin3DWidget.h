/**
 * @file   Skin3DWidget.h
 * @brief  3D 皮肤预览控件（原生 OpenGL 实现，基于 QOpenGLWidget）
 * @author BlockBox Team
 * @date   2026-07-24
 *
 * 完全基于 Blockbench skin.ts 的标准坐标系（Y 上、Z 后）与 Box UV 算法，
 * 使用 C++17 + OpenGL 3.3 Core Profile 原生渲染，无任何浏览器依赖。
 *
 * 数据驱动的模型定义：
 *   - 每个 cube 由 (origin, size, uv, inflate) 描述
 *   - pivot 是部件旋转中心（关节位置）
 *   - 外层（overlay）通过 inflate 膨胀，比内层大 0.25~0.5
 *
 * 离线皮肤目录：Images/Skins/{username}.png（相对路径）
 * 兼容正版/第三方账户皮肤（通过 SkinDownloader）
 *
 * 接口与 SkinEditorPage / AccountManagePage 已有调用保持兼容：
 *   - setSkin(QImage)
 *   - setBackgroundColor(QColor)
 */
#pragma once

#include <QColor>
#include <QImage>
#include <QMatrix4x4>
#include <QObject>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QWidget>

class SkinEditorDocument;

/**
 * @brief 绘制工具枚举（与 SkinTextureCanvas 保持一致，避免重复定义）
 */
enum class DrawTool;

/**
 * @brief 模型类型枚举
 */
enum class SkinModelType
{
  Auto = 0,    // 自动检测（由 isSlimSkin 算法判定）
  Default = 1, // Classic (Steve)
  Slim = 2     // Slim (Alex)
};

/**
 * @brief 交互模式
 */
enum class InteractionMode
{
  Orbit = 0, // 轨道相机（仅旋转/缩放，预览用途，默认）
  Paint = 1  // 立体绘制（BlockBench 风格：左键在模型表面绘制，右键/中键旋转）
};

/**
 * @brief 模型动作/姿势（参考 skinview3d 的 walking/wave/riding 动画）
 *
 * 驱动各关节 rotation（绕 X 轴=前后摆，绕 Z 轴=侧抬臂）形成肢体动画。
 */
enum class SkinPose
{
  Idle = 0, // 待机（静止，默认）
  Wave,     // 挥手（右臂抬起左右摆动）
  Walk,     // 走路（双臂双腿交替前后摆动）
  Run,      // 跑步（幅度更大的走路 + 身体前倾）
  Ride      // 骑马/坐姿（双腿前屈，身体微前倾）
};

/**
 * @brief 3D 皮肤预览控件，基于 QOpenGLWidget 原生 OpenGL 渲染
 */
class Skin3DWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
  Q_OBJECT

public:
  explicit Skin3DWidget(QWidget* parent = nullptr);
  ~Skin3DWidget() override;

  // 兼容原接口 --------------------------------------------------
  void setSkin(const QImage& image);
  void setBackgroundColor(const QColor& color);

  /**
   * @brief 设置垂直线性渐变背景（顶部颜色 → 底部颜色）
   *
   * 开启后会替代纯色背景（含主题背景）；调用 setBackgroundColor /
   * setThemeBackground 会重新回到纯色背景。
   */
  void setBackgroundGradient(const QColor& top, const QColor& bottom);

  /**
   * @brief 设置图片背景（铺满视口，保持比例裁剪）
   *
   * 空图片表示关闭图片背景；开启后替代纯色/渐变/主题背景。
   * 调用 setBackgroundColor / setThemeBackground / setBackgroundGradient
   * 会重新回到对应背景类型。
   */
  void setBackgroundImage(const QImage& image);

  /**
   * @brief 设置背景圆角半径（像素，0=方形，默认）
   *
   * 启用后纯色背景以带 alpha 的圆角矩形绘制、清屏为全透明：
   * 配合 WA_AlwaysStackOnTop 使用时，圆角外区域完全透明、
   * 圆角内按背景色 alpha 与窗口背景合成（如首页 50% 透明皮肤预览）。
   * 仅对纯色背景生效，渐变/图片背景忽略此设置。
   */
  void setBackgroundCornerRadius(qreal radius);

  // 新增接口 --------------------------------------------------
  void setModelType(SkinModelType type);
  void setAutoRotate(bool enabled);
  void setInnerLayerVisible(bool visible);
  void setOuterLayerVisible(bool visible);
  void resetView();
  void resetView(float radius);  // 重置视角到默认（正面 3/4 视角），并设置相机距离

  /**
   * @brief 旋转视角（弧度），增量叠加到目标相机角
   * @param dTheta 水平角增量（正=右转，负=左转）
   * @param dPhi   垂直角增量（正=抬头，负=低头），会自动限制在 ±60°
   */
  void rotateView(float dTheta, float dPhi = 0.0f);

  /**
   * @brief 缩放视角（倍率），>1 拉近、<1 拉远，限制在 [1.0, 8.0]
   */
  void zoomView(float factor);

  /**
   * @brief 设置模型动作/姿势（参考 skinview3d 的 walking/wave/riding）
   *
   * 非 Idle 动作会启动动画定时器驱动关节旋转；Idle 恢复静止。
   * Paint（3D 编辑）模式下不应用动作，避免干扰绘制。
   */
  void setPose(SkinPose pose);
  SkinPose pose() const { return m_pose; }

  // 3D 编辑接口（BlockBench 风格立体绘制）---------------------
  void setInteractionMode(InteractionMode mode);
  InteractionMode interactionMode() const;

  /**
   * @brief 绑定编辑文档（Paint 模式下绘制写入该文档；nullptr 时为纯预览）
   * @param doc 皮肤文档指针（不持有所有权）
   */
  void setDocument(SkinEditorDocument* doc);

  void setForegroundColor(const QColor& color);
  QColor foregroundColor() const;
  void setTool(DrawTool tool);
  DrawTool tool() const;
  void setBrushSize(int size);
  int brushSize() const;

  /**
   * @brief 设置是否绘制外层(overlay)（参考 Blockbench 的图层设计）
   * @param enabled 为 true 时点击模型命中膨胀的外层盒子（帽子/外套等，64x64 纹理的 y>=32 区域）；
   *                默认 false：只绘制内层（基础皮肤层）
   *
   * 注意与 setOuterLayerVisible（渲染显示开关）分离：
   *   显示外层 = 预览/编辑时都可见叠加层；绘制外层 = 是否把画笔落到外层 UV 区域。
   */
  void setPaintOuter(bool enabled);
  bool paintOuter() const;

  /**
   * @brief 设置只显示某个部位（参考 Blockbench 的部件选择）
   * @param partIndex -1=全部显示（默认）；0=头部 1=身体 2=右臂 3=左臂 4=右腿 5=左腿
   *
   * 选中部位后，其余部位的几何体不渲染（renderPart 跳过），
   * 拾取（pick）也只命中选中部位——便于单独绘制/查看细节。
   */
  void setVisiblePart(int partIndex);
  int visiblePart() const;

  /**
   * @brief 根据当前主题自动设置背景色
   * @param dark 是否深色主题
   *
   * 深色主题使用 #1e1e2e，浅色主题使用 #d9d9e0
   */
  void setThemeBackground(bool dark);

  /**
   * @brief 模拟一次轻量拖拽（按下-拖动-松开）
   *
   * 用于进入含本控件的页面时自动触发渲染刷新：等效于用户手动拖拽，
   * 走鼠标事件处理链路（mousePressEvent/mouseMoveEvent/mouseReleaseEvent），
   * 确保被 QGraphicsEffect 破坏的 GL 渲染恢复显示。
   */
  void simulateDrag();

signals:
  void ready();
  void colorPicked(const QColor& color);  ///< 3D 吸管拾取后发出
  void pixelHovered(int x, int y);        ///< Paint 模式悬停/绘制时发出纹理像素坐标（-1,-1 表示未命中模型）

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  void showEvent(QShowEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;

private:
  // ============================================================
  // 数据驱动的模型定义（参考 Blockbench skin.ts 标准数据）
  // ============================================================

  /// Cube 规格：origin 是最小角，size 是各维度长度，uv 是像素偏移，inflate 是膨胀量
  struct CubeSpec
  {
    float ox, oy, oz;   // origin (minecraft 像素坐标)
    float sx, sy, sz;   // size [width, height, depth] (像素)
    float u, v;         // UV 偏移 (像素)
    float inflate;       // 膨胀量 (0=内层, 0.25/0.5=外层)
  };

  /// 部件规格：pivot 旋转中心 + 内层/外层 cube
  struct PartSpec
  {
    float px, py, pz;   // pivot (minecraft 像素坐标)
    CubeSpec inner;
    CubeSpec outer;
  };

  /// 完整皮肤规格
  struct SkinSpec
  {
    PartSpec head;
    PartSpec body;
    PartSpec rightArm;
    PartSpec leftArm;
    PartSpec rightLeg;
    PartSpec leftLeg;
    bool slim = false;
  };

  // ============================================================
  // 运行时 GL 资源
  // ============================================================

  /// 单个 cube 的 GL mesh：24 顶点 + 36 索引
  struct CubeMesh
  {
    QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer ibo{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject vao;
    int indexCount = 0;
    void destroy();
  };

  /// 身体部件：内层 mesh + 外层 mesh + 变换矩阵
  struct BodyPart
  {
    CubeMesh inner;
    CubeMesh outer;
    QMatrix4x4 partTransform;  // pivot 平移（将 pivot 移到原点以便旋转）
    QMatrix4x4 rotation;       // 运行时关节旋转
    bool visible = true;
  };

  /// 运行时皮肤对象
  struct SkinObject
  {
    BodyPart head;
    BodyPart body;
    BodyPart rightArm;
    BodyPart leftArm;
    BodyPart rightLeg;
    BodyPart leftLeg;
    QMatrix4x4 rootTransform;  // 整体变换（居中 + 缩放）
    bool slim = false;
  };

  // ---- OpenGL 资源 ----
  QOpenGLShaderProgram m_program;
  QOpenGLShaderProgram m_bgProgram;  ///< 渐变背景全屏四边形着色器
  QOpenGLBuffer m_bgVbo{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject m_bgVao;
  QOpenGLTexture* m_texture = nullptr;
  SkinObject m_skin;

  // ---- 状态 ----
  QColor m_background{0x1e, 0x1e, 0x2e};
  QColor m_gradientTop{0x1e, 0x1e, 0x2e};   ///< 渐变背景顶部颜色
  QColor m_gradientBottom{0x1e, 0x1e, 0x2e}; ///< 渐变背景底部颜色
  bool m_bgGradient = false;                ///< 是否使用渐变背景
  bool m_bgDirty = true;                    ///< 渐变背景顶点数据是否需要重建
  QImage m_bgImage;                         ///< 图片背景源图
  bool m_bgImageEnabled = false;            ///< 是否使用图片背景
  bool m_bgImageDirty = true;               ///< 图片背景纹理是否需要上传
  QOpenGLTexture* m_bgImageTexture = nullptr; ///< 图片背景纹理
  QOpenGLTexture* m_bgWhiteTexture = nullptr; ///< 1x1 白色纹理（非图片背景时绑定）
  qreal m_bgCornerRadius = 0.0;             ///< 纯色背景圆角半径（0=方形；>0 时清屏透明、绘制圆角背景）
  QImage m_pendingSkin;
  bool m_skinDirty = false;
  bool m_innerVisible = true;
  bool m_outerVisible = true;
  bool m_autoRotate = true;
  SkinModelType m_modelType = SkinModelType::Auto;

  // 相机（球面坐标）—— 使用目标值 + 当前值实现平滑插值
  // 注意：m_phi / m_targetPhi 默认必须为非零俯视角！
  // 若 phi=0，相机位于模型正上方 (0, radius, 0)，视线与 up=(0,1,0) 平行，
  // QMatrix4x4::lookAt 退化（side/upVector 为零向量），模型投影到一个点不可见。
  // 参考开源皮肤渲染库 skinview3d 的默认 3/4 视角（俯视约 28.6°）。
  float m_theta = 0.0f;        // 当前水平角
  float m_phi = 0.5f;          // 当前垂直角（默认 0.5 rad ≈ 28.6° 俯视，避免 lookAt 退化）
  float m_radius = 2.8f;       // 当前相机距离
  float m_targetTheta = 0.0f;  // 目标水平角（拖拽/惯性更新）
  float m_targetPhi = 0.5f;    // 目标垂直角
  float m_targetRadius = 2.8f; // 目标相机距离（滚轮更新）
  QPoint m_lastPos;
  bool m_dragging = false;
  Qt::MouseButton m_dragButton = Qt::NoButton;

  // 惯性拖拽
  float m_velTheta = 0.0f;  // 水平角速度（rad/帧）
  float m_velPhi = 0.0f;    // 垂直角速度（rad/帧）
  bool m_inertiaActive = false;

  // ---- 动画 ----
  QTimer m_animTimer;
  SkinPose m_pose = SkinPose::Idle;  // 当前模型动作/姿势
  float m_poseTime = 0.0f;           // 动作动画时钟（秒，驱动关节旋转）

  // 动作动画：按当前 pose 更新各关节 rotation（onAnimTick 调用）
  void applyPoseAnimation(float t);

  // ---- 3D 绘制（Paint 模式）状态 ----
  InteractionMode m_interactionMode = InteractionMode::Orbit;
  SkinEditorDocument* m_document = nullptr;  ///< 编辑文档（不持有所有权）
  QColor m_fgColor{Qt::black};               ///< 绘制前景色
  DrawTool m_tool;                           ///< 当前工具（构造函数初始化）
  int m_brushSize = 1;                       ///< 笔刷大小（1/2/4）
  bool m_texIs64x64 = true;                  ///< 纹理是否为 64x64（决定外层是否参与拾取/绘制）
  bool m_paintOuter = false;                 ///< 是否绘制外层(overlay)（默认 false：只画内层）
  int m_visiblePart = -1;                    ///< 只显示的部件（-1=全部；0~5=对应部位，参考 PickResult::partIndex）
  bool m_autoRotateSaved = false;            ///< 进入 Paint 模式前的自动旋转状态

  /// 拾取结果：命中的部件/内外层/面与纹理像素坐标
  struct PickResult
  {
    bool hit = false;
    int partIndex = -1;   ///< 0=头部 1=身体 2=右臂 3=左臂 4=右腿 5=左腿
    bool outer = false;   ///< 是否命中外层(overlay)
    int faceIndex = -1;   ///< 0=Top 1=Bottom 2=Front 3=Back 4=Right 5=Left
    float u = 0.0f, v = 0.0f;      ///< 命中点纹理像素坐标（浮点）
    int px = -1, py = -1;          ///< 命中点纹理像素坐标（取整）
    int fu0 = 0, fv0 = 0, fw = 0, fh = 0;  ///< 所在面的整数 UV 矩形（用于笔刷裁剪）
  };

  /// 笔画状态
  bool m_painting = false;
  int m_strokePart = -1;      ///< 当前笔画所在部件
  bool m_strokeOuter = false; ///< 当前笔画所在层
  int m_strokeFace = -1;      ///< 当前笔画所在面
  QColor m_strokeColor;       ///< 当前笔画颜色（橡皮=透明）
  QVector<QPoint> m_pendingPoints;  ///< 当前笔画累积像素（松开时一次性提交）
  PickResult m_lastPick;      ///< 上次拾取结果（笔画插值用）
  QPointF m_lineStartUV;      ///< 直线工具起点 UV

  // ---- 几何体构建 ----

  /// 根据 cube 规格构建 GL mesh（标准 Box UV 算法）
  void buildCubeMesh(CubeMesh& mesh, const CubeSpec& spec, float scale);

  /// 构建整个皮肤几何体
  void buildSkinGeometry();

  /// 获取当前模型规格（Classic 或 Slim）
  SkinSpec getModelSpec(bool slim) const;

  // ---- Slim 检测（复刻 skinview-utils）----
  static bool isSlimSkin(const QImage& image);

  // ---- 背景渲染 ----
  /// 编译背景着色器并创建全屏四边形 VAO/VBO（需当前 GL 上下文）
  void buildBackgroundResources();
  /// 上传图片背景纹理（需当前 GL 上下文）
  void uploadBackgroundImage();
  /// 绘制渐变/图片背景（在 paintGL 中调用）
  void drawBackground();

  // ---- 纹理上传 ----
  void uploadTexture(const QImage& image);

  // ---- 资源释放 ----
  void destroyGLResources();

  // ---- 3D 拾取与绘制 ----

  /// 面 UV 矩形（像素坐标，与 buildCubeMesh 中 faceUV 布局严格一致）
  struct UvRect { float u0, u1, v0, v1; };
  UvRect faceUvRect(const CubeSpec& spec, int faceIndex) const;

  /// 射线与轴对齐盒（slab 法）相交测试，返回进入 t 与命中的面索引
  static bool intersectLocalBox(const QVector3D& ro, const QVector3D& rd,
                                const QVector3D& bmin, const QVector3D& bmax,
                                float& tEnter, int& faceIndex);

  /// 屏幕坐标拾取模型表面，未命中返回 hit=false
  PickResult pick(const QPoint& widgetPos) const;

  /// 由拾取结果生成笔刷块（裁剪到所在面 UV 矩形内）
  void stampForPick(const PickResult& r, QVector<QPoint>& out) const;

  /// 将像素点裁剪到纹理有效范围
  QVector<QPoint> clipPointsToImage(const QVector<QPoint>& points) const;

  /// 笔画预览：把当前累积像素画到临时纹理并上传（释放时由文档提交覆盖）
  void updateStrokePreviewTexture();

  /// 尝试开始笔画；未命中模型返回 false（调用方降级为旋转相机）
  bool startStroke(const QPoint& pos);
  void continueStroke(const QPoint& pos);
  void endStroke(const QPoint& pos);

  /// 根据交互模式/工具更新光标
  void updateCursor();

  // 轨道相机辅助（从既有鼠标事件拆出，Paint/Orbit 共用）
  void beginOrbit(QMouseEvent* e);
  void orbitMove(QMouseEvent* e);
  void endOrbit(QMouseEvent* e);

private slots:
  void onAnimTick();
};
