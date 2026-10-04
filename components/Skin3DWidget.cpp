/**
 * @file   Skin3DWidget.cpp
 * @brief  3D 皮肤预览控件（原生 OpenGL 实现）
 * @author BlockBox Team
 * @date   2026-07-24
 *
 * 完全基于 Blockbench skin.ts 的标准坐标系（Y 上、Z 后）与 Box UV 算法。
 * 模型数据、姿势表均来自 Blockbench skin.ts 的标准定义。
 *
 * 坐标系：
 *   - X 右、Y 上、Z 后（朝向观察者）
 *   - 单位：minecraft 像素 / 16 = OpenGL 单位
 *
 * UV：
 *   - 标准 Box UV，v = v_pixel / 64（QOpenGLTexture 已处理 Y 翻转）
 *
 * 离线皮肤目录：Images/Skins/{username}.png（相对路径，由调用方拼接）
 * 兼容正版/第三方账户皮肤（通过 SkinDownloader 提供 QImage）
 */
#include "Skin3DWidget.h"

#include "components/SkinTextureCanvas.h"
#include "utils/SkinEditorDocument.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QVector4D>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>

// Windows/MinGW 下确保 M_PI 可用
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// 着色器源码（OpenGL 3.3 Core Profile）
// 顶点着色器：传递位置/法线/UV，计算 gl_Position
// 片元着色器：纹理采样 + alphaTest 丢弃 + Blockbench 方向光照
//   AMBIENT=0.5, XFAC=-0.15, ZFAC=0.05
//   顶面最亮、底面最暗、左右中等、前后较亮 —— Minecraft 经典风格
// ============================================================
static const char* kVertexShaderSource = R"(
#version 330 core
in vec3 aPosition;
in vec3 aNormal;
in vec2 aUv;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vNormal;
out vec2 vUv;
void main() {
  vNormal = normalize(mat3(uModel) * aNormal);
  vUv = aUv;
  gl_Position = uProj * uView * uModel * vec4(aPosition, 1.0);
}
)";

static const char* kFragmentShaderSource = R"(
#version 330 core
in vec3 vNormal;
in vec2 vUv;
uniform sampler2D uTex;
uniform float uAlphaTest;
out vec4 fragColor;
void main() {
  vec4 c = texture(uTex, vUv);
  if (c.a < uAlphaTest) discard;
  float AMBIENT = 0.5;
  float XFAC = -0.15;
  float ZFAC = 0.05;
  vec3 N = normalize(vNormal);
  float yLight = (1.0 + N.y) * 0.5;
  float light = yLight * (1.0 - AMBIENT) + N.x * N.x * XFAC + N.z * N.z * ZFAC + AMBIENT;
  fragColor = vec4(c.rgb * light, c.a);
}
)";

// 背景着色器（全屏四边形，支持渐变顶点色与图片纹理混合）
static const char* kBgVertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUv;
out vec4 vColor;
out vec2 vUv;
void main() {
  vColor = aColor;
  vUv = aUv;
  gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* kBgFragmentShaderSource = R"(
#version 330 core
in vec4 vColor;
in vec2 vUv;
uniform sampler2D uImage;
uniform float uUseImage;
uniform vec2 uUvScale;
uniform vec2 uUvOffset;
out vec4 fragColor;
void main() {
  vec4 img = texture(uImage, vUv * uUvScale + uUvOffset);
  fragColor = mix(vColor, img, uUseImage);
}
)";

// 纹理尺寸常量
static constexpr float TEX_SIZE = 64.0f;

// ============================================================
// CubeMesh::destroy
// ============================================================
void Skin3DWidget::CubeMesh::destroy()
{
  vbo.destroy();
  ibo.destroy();
  vao.destroy();
}

// ============================================================
// 构造 / 析构
// ============================================================
Skin3DWidget::Skin3DWidget(QWidget* parent)
  : QOpenGLWidget(parent)
  , m_tool(DrawTool::Brush)
{
  // 请求 OpenGL 3.3 Core Profile
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  fmt.setSamples(4); // 4x MSAA 抗锯齿
  setFormat(fmt);

  // 动画定时器（~60fps）
  m_animTimer.setInterval(16);
  connect(&m_animTimer, &QTimer::timeout, this, &Skin3DWidget::onAnimTick);

  setMouseTracking(true);
  updateCursor();
}

Skin3DWidget::~Skin3DWidget()
{
  if (context() && context()->isValid())
  {
    makeCurrent();
    destroyGLResources();
    doneCurrent();
  }
}

// ============================================================
// 公共接口
// ============================================================
void Skin3DWidget::setSkin(const QImage& image)
{
  if (image.isNull()) return;

  // 64x32 格式不存在外层(overlay)：拾取与绘制时跳过外层盒子
  m_texIs64x64 = (image.height() > 32);

  // 检测 slim 模型（若模型类型为 Auto）
  if (m_modelType == SkinModelType::Auto)
  {
    m_skin.slim = isSlimSkin(image);
  }
  else
  {
    m_skin.slim = (m_modelType == SkinModelType::Slim);
  }

  m_pendingSkin = image;
  m_skinDirty = true;

  // 若已初始化 GL，立即上传纹理并重建几何体
  if (context() && context()->isValid() && m_program.isLinked())
  {
    makeCurrent();
    uploadTexture(image);
    buildSkinGeometry();
    m_skinDirty = false;
    doneCurrent();
  }
  // 若控件可见，立即重绘（repaint 同步触发 paintGL）；否则调度重绘
  if (isVisible())
  {
    repaint();
  }
  else
  {
    update();
  }
}

void Skin3DWidget::setBackgroundColor(const QColor& color)
{
  m_bgGradient = false;
  m_bgImageEnabled = false;
  m_background = color;
  update();
}

void Skin3DWidget::setBackgroundGradient(const QColor& top, const QColor& bottom)
{
  m_bgImageEnabled = false;
  m_gradientTop = top;
  m_gradientBottom = bottom;
  m_bgGradient = true;
  m_bgDirty = true;
  update();
}

void Skin3DWidget::setBackgroundImage(const QImage& image)
{
  if (image.isNull())
  {
    m_bgImageEnabled = false;
    update();
    return;
  }
  m_bgGradient = false;
  m_bgImage = image;
  m_bgImageEnabled = true;
  m_bgImageDirty = true;
  update();
}

void Skin3DWidget::setModelType(SkinModelType type)
{
  m_modelType = type;
  if (!m_pendingSkin.isNull())
  {
    setSkin(m_pendingSkin);
  }
}

void Skin3DWidget::setAutoRotate(bool enabled)
{
  m_autoRotate = enabled;
  if (enabled)
  {
    m_animTimer.start();
  }
  else
  {
    m_animTimer.stop();
  }
  update();
}

void Skin3DWidget::setInnerLayerVisible(bool visible)
{
  m_innerVisible = visible;
  update();
}

void Skin3DWidget::setOuterLayerVisible(bool visible)
{
  m_outerVisible = visible;
  update();
}

void Skin3DWidget::resetView()
{
  resetView(2.8f);
}

void Skin3DWidget::resetView(float radius)
{
  // 恢复默认视角：theta=0（正面）、phi=0.5（约 28.6° 俯视）。
  // 注意 phi 不能为 0：正上方视角会使 lookAt 退化、模型不可见。
  m_targetTheta = 0.0f;
  m_targetPhi = 0.5f;
  m_targetRadius = radius;
  m_velTheta = 0.0f;
  m_velPhi = 0.0f;
  m_inertiaActive = false;
  // 启动动画定时器以驱动平滑过渡
  if (!m_animTimer.isActive())
    m_animTimer.start();
  update();
}

void Skin3DWidget::rotateView(float dTheta, float dPhi)
{
  // 与鼠标拖拽同链路：增量叠加到目标角，由动画定时器平滑跟随
  m_targetTheta += dTheta;
  m_targetPhi += dPhi;
  m_targetPhi = qBound(-float(M_PI) / 3.0f, m_targetPhi, float(M_PI) / 3.0f);
  m_velTheta = 0.0f;
  m_velPhi = 0.0f;
  m_inertiaActive = false;
  if (!m_animTimer.isActive())
    m_animTimer.start();
  update();
}

void Skin3DWidget::zoomView(float factor)
{
  m_targetRadius *= factor;
  m_targetRadius = qBound(1.0f, m_targetRadius, 8.0f);
  if (!m_animTimer.isActive())
    m_animTimer.start();
  update();
}

void Skin3DWidget::setPose(SkinPose pose)
{
  m_pose = pose;
  m_poseTime = 0.0f;

  if (pose != SkinPose::Idle)
  {
    // 非待机：启动动画定时器驱动关节旋转
    if (!m_animTimer.isActive())
      m_animTimer.start();
  }
  else
  {
    // 待机：复位所有关节旋转
    applyPoseAnimation(0.0f);
    // 若没有其他动画源（自动旋转/惯性/脏纹理/Paint），停止定时器
    const bool needsTimer = m_autoRotate || m_inertiaActive || m_skinDirty ||
                            (m_interactionMode == InteractionMode::Paint && m_document);
    if (!needsTimer)
      m_animTimer.stop();
  }
  update();
}

// ============================================================
// 模型动作动画（参考 skinview3d 的 walking/wave/riding）
//
// 坐标系：X 右、Y 上、Z 后（minecraft 像素，pivot 局部坐标）
//   - 绕 X 轴旋转 → 前后摆（手臂/腿走路、腿前屈）
//   - 绕 Z 轴旋转 → 侧抬（挥手）
// ============================================================
void Skin3DWidget::applyPoseAnimation(float t)
{
  // 复位所有关节
  auto resetPart = [](BodyPart& p) { p.rotation.setToIdentity(); };
  resetPart(m_skin.head);
  resetPart(m_skin.body);
  resetPart(m_skin.rightArm);
  resetPart(m_skin.leftArm);
  resetPart(m_skin.rightLeg);
  resetPart(m_skin.leftLeg);

  if (m_pose == SkinPose::Idle)
    return;

  // 弧度 → 角度
  auto deg = [](float rad) { return rad * 180.0f / float(M_PI); };

  if (m_pose == SkinPose::Walk)
  {
    // 走路：双臂双腿交替前后摆动（左右反相）
    const float amp = 0.45f; // 弧度
    const float s = std::sin(t * 4.0f);
    m_skin.rightArm.rotation.rotate(deg(s * amp), 1, 0, 0);
    m_skin.leftArm.rotation.rotate(deg(-s * amp), 1, 0, 0);
    m_skin.rightLeg.rotation.rotate(deg(-s * amp), 1, 0, 0);
    m_skin.leftLeg.rotation.rotate(deg(s * amp), 1, 0, 0);
  }
  else if (m_pose == SkinPose::Run)
  {
    // 跑步：幅度更大的走路 + 身体前倾
    const float amp = 0.9f;
    const float s = std::sin(t * 6.0f);
    m_skin.rightArm.rotation.rotate(deg(s * amp), 1, 0, 0);
    m_skin.leftArm.rotation.rotate(deg(-s * amp), 1, 0, 0);
    m_skin.rightLeg.rotation.rotate(deg(-s * amp), 1, 0, 0);
    m_skin.leftLeg.rotation.rotate(deg(s * amp), 1, 0, 0);
    m_skin.body.rotation.rotate(15.0f, 1, 0, 0); // 前倾约 15°
  }
  else if (m_pose == SkinPose::Wave)
  {
    // 挥手：右臂侧抬（绕 Z 轴向外），叠加小幅前后摆
    // 右臂在 X 负侧，绕 Z 负角抬起（-Y 端向 +Y 摆）
    m_skin.rightArm.rotation.rotate(-115.0f, 0, 0, 1);
    m_skin.rightArm.rotation.rotate(deg(std::sin(t * 5.0f) * 0.35f), 1, 0, 0);
  }
  else if (m_pose == SkinPose::Ride)
  {
    // 骑马/坐姿：双腿前屈，身体微前倾，手臂略前伸
    m_skin.rightLeg.rotation.rotate(55.0f, 1, 0, 0);
    m_skin.leftLeg.rotation.rotate(55.0f, 1, 0, 0);
    m_skin.body.rotation.rotate(8.0f, 1, 0, 0);
    m_skin.rightArm.rotation.rotate(20.0f, 1, 0, 0);
    m_skin.leftArm.rotation.rotate(20.0f, 1, 0, 0);
  }
}

// ============================================================
// 3D 编辑接口（BlockBench 风格立体绘制）
// ============================================================
void Skin3DWidget::setInteractionMode(InteractionMode mode)
{
  if (m_interactionMode == mode)
    return;
  m_interactionMode = mode;
  if (mode == InteractionMode::Paint)
  {
    // 进入绘制模式时暂停自动旋转，避免绘制过程中模型转动
    m_autoRotateSaved = m_autoRotate;
    setAutoRotate(false);
    // 参考开源皮肤渲染库（skinview3d / Blockbench）的持续渲染循环：
    // 编辑器不依赖自动旋转驱动重绘，而动画定时器是唯一保证 paintGL 被
    // 持续调用的机制——即使相机静止也保持渲染，避免 QStackedWidget 页面
    // 切换、GL 上下文初始化等场景下模型漏画（空白）。
    if (isVisible() && m_document && !m_animTimer.isActive())
      m_animTimer.start();
  }
  else
  {
    // 恢复进入绘制模式前的自动旋转状态
    setAutoRotate(m_autoRotateSaved);
  }
  updateCursor();
  update();
}

InteractionMode Skin3DWidget::interactionMode() const
{
  return m_interactionMode;
}

void Skin3DWidget::setDocument(SkinEditorDocument* doc)
{
  m_document = doc;
  if (m_document == nullptr)
  {
    m_pendingPoints.clear();
    m_painting = false;
  }
}

void Skin3DWidget::setForegroundColor(const QColor& color)
{
  m_fgColor = color;
}

QColor Skin3DWidget::foregroundColor() const
{
  return m_fgColor;
}

void Skin3DWidget::setTool(DrawTool tool)
{
  if (m_tool == tool)
    return;
  m_tool = tool;
  updateCursor();
}

DrawTool Skin3DWidget::tool() const
{
  return m_tool;
}

void Skin3DWidget::setBrushSize(int size)
{
  if (size != 1 && size != 2 && size != 4)
    return;
  m_brushSize = size;
}

int Skin3DWidget::brushSize() const
{
  return m_brushSize;
}

void Skin3DWidget::setPaintOuter(bool enabled)
{
  m_paintOuter = enabled;
}

bool Skin3DWidget::paintOuter() const
{
  return m_paintOuter;
}

void Skin3DWidget::setVisiblePart(int partIndex)
{
  m_visiblePart = qBound(-1, partIndex, 5);
  // 更新各部位可见性：-1=全部显示，否则只显示选中部位
  m_skin.head.visible      = (m_visiblePart < 0 || m_visiblePart == 0);
  m_skin.body.visible      = (m_visiblePart < 0 || m_visiblePart == 1);
  m_skin.rightArm.visible  = (m_visiblePart < 0 || m_visiblePart == 2);
  m_skin.leftArm.visible   = (m_visiblePart < 0 || m_visiblePart == 3);
  m_skin.rightLeg.visible  = (m_visiblePart < 0 || m_visiblePart == 4);
  m_skin.leftLeg.visible   = (m_visiblePart < 0 || m_visiblePart == 5);
  update();
}

int Skin3DWidget::visiblePart() const
{
  return m_visiblePart;
}

void Skin3DWidget::setThemeBackground(bool dark)
{
  // 深色主题：深蓝灰；浅色主题：浅灰
  m_bgGradient = false;
  m_bgImageEnabled = false;
  m_background = dark ? QColor(0x1e, 0x1e, 0x2e) : QColor(0xd9, 0xd9, 0xe0);
  update();
}

void Skin3DWidget::buildBackgroundResources()
{
  // 编译渐变背景着色器（仅在未链接时执行一次）
  if (!m_bgProgram.isLinked())
  {
    if (!m_bgProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, kBgVertexShaderSource))
    {
      qWarning("[Skin3DWidget] Background vertex shader compilation failed: %s", qPrintable(m_bgProgram.log()));
    }
    if (!m_bgProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, kBgFragmentShaderSource))
    {
      qWarning("[Skin3DWidget] Background fragment shader compilation failed: %s", qPrintable(m_bgProgram.log()));
    }
    m_bgProgram.bindAttributeLocation("aPos", 0);
    m_bgProgram.bindAttributeLocation("aColor", 1);
    if (!m_bgProgram.link())
    {
      qWarning("[Skin3DWidget] Background shader linking failed: %s", qPrintable(m_bgProgram.log()));
      return;
    }
  }

  if (!m_bgVao.isCreated())
    m_bgVao.create();
  if (!m_bgVbo.isCreated())
    m_bgVbo.create();

  // 绑定 VAO 的 attribute 布局：位置(vec2) + 颜色(vec4) + UV(vec2)，跨距 8 个 float
  m_bgVao.bind();
  m_bgVbo.bind();
  const int stride = 8 * static_cast<int>(sizeof(float));
  m_bgProgram.enableAttributeArray(0);
  m_bgProgram.setAttributeBuffer(0, GL_FLOAT, 0, 2, stride);
  m_bgProgram.enableAttributeArray(1);
  m_bgProgram.setAttributeBuffer(1, GL_FLOAT, 2 * static_cast<int>(sizeof(float)), 4, stride);
  m_bgProgram.enableAttributeArray(2);
  m_bgProgram.setAttributeBuffer(2, GL_FLOAT, 6 * static_cast<int>(sizeof(float)), 2, stride);
  m_bgVao.release();
  m_bgVbo.release();

  // 1x1 白色纹理：非图片背景时绑定，保证 sampler 采样有效
  if (!m_bgWhiteTexture)
  {
    QImage white(1, 1, QImage::Format_RGB32);
    white.fill(Qt::white);
    m_bgWhiteTexture = new QOpenGLTexture(white);
    m_bgWhiteTexture->setMinificationFilter(QOpenGLTexture::Linear);
    m_bgWhiteTexture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_bgWhiteTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
  }

  m_bgDirty = true;
}

void Skin3DWidget::uploadBackgroundImage()
{
  if (m_bgImage.isNull())
    return;

  if (m_bgImageTexture)
  {
    m_bgImageTexture->destroy();
    delete m_bgImageTexture;
    m_bgImageTexture = nullptr;
  }

  m_bgImageTexture = new QOpenGLTexture(m_bgImage, QOpenGLTexture::DontGenerateMipMaps);
  m_bgImageTexture->setMinificationFilter(QOpenGLTexture::Linear);
  m_bgImageTexture->setMagnificationFilter(QOpenGLTexture::Linear);
  m_bgImageTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
  m_bgImageDirty = false;
}

void Skin3DWidget::drawBackground()
{
  if (!m_bgProgram.isLinked())
    return;

  // 颜色变化时重建顶点数据（全屏四边形：顶部两个顶点用顶部色，底部用底部色）
  if (m_bgDirty)
  {
    const float top[4] = {
      m_gradientTop.redF(), m_gradientTop.greenF(), m_gradientTop.blueF(), 1.0f
    };
    const float bottom[4] = {
      m_gradientBottom.redF(), m_gradientBottom.greenF(), m_gradientBottom.blueF(), 1.0f
    };
    const float quad[48] = {
      -1.0f,  1.0f,  top[0],    top[1],    top[2],    top[3],    0.0f, 1.0f,
       1.0f,  1.0f,  top[0],    top[1],    top[2],    top[3],    1.0f, 1.0f,
       1.0f, -1.0f,  bottom[0], bottom[1], bottom[2], bottom[3], 1.0f, 0.0f,
      -1.0f,  1.0f,  top[0],    top[1],    top[2],    top[3],    0.0f, 1.0f,
       1.0f, -1.0f,  bottom[0], bottom[1], bottom[2], bottom[3], 1.0f, 0.0f,
      -1.0f, -1.0f,  bottom[0], bottom[1], bottom[2], bottom[3], 0.0f, 0.0f
    };
    m_bgVbo.bind();
    m_bgVbo.allocate(quad, sizeof(quad));
    m_bgVbo.release();
    m_bgDirty = false;
  }

  // 图片背景：延迟上传纹理（需当前 GL 上下文）
  if (m_bgImageEnabled && m_bgImageDirty)
  {
    uploadBackgroundImage();
  }

  // 背景在 NDC 全屏绘制，无需深度测试/背面剔除，且不写入深度（避免污染模型深度）
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);

  m_bgProgram.bind();

  if (m_bgImageEnabled && m_bgImageTexture)
  {
    // 铺满视口并保持比例（cover）：根据视口与图片宽高比计算采样范围
    const float aspect = (height() > 0) ? float(width()) / float(height()) : 1.0f;
    const float imgAspect = (m_bgImage.height() > 0)
                              ? float(m_bgImage.width()) / float(m_bgImage.height())
                              : aspect;
    float scaleU = 1.0f, scaleV = 1.0f, offsetU = 0.0f, offsetV = 0.0f;
    if (imgAspect > aspect)
    {
      scaleU = aspect / imgAspect;
      offsetU = (1.0f - scaleU) * 0.5f;
    }
    else
    {
      scaleV = imgAspect / aspect;
      offsetV = (1.0f - scaleV) * 0.5f;
    }
    m_bgProgram.setUniformValue("uUseImage", 1.0f);
    m_bgProgram.setUniformValue("uUvScale", scaleU, scaleV);
    m_bgProgram.setUniformValue("uUvOffset", offsetU, offsetV);
    m_bgImageTexture->bind(0);
    m_bgProgram.setUniformValue("uImage", 0);
  }
  else
  {
    m_bgProgram.setUniformValue("uUseImage", 0.0f);
    m_bgProgram.setUniformValue("uUvScale", 1.0f, 1.0f);
    m_bgProgram.setUniformValue("uUvOffset", 0.0f, 0.0f);
    if (m_bgWhiteTexture)
    {
      m_bgWhiteTexture->bind(0);
      m_bgProgram.setUniformValue("uImage", 0);
    }
  }

  m_bgVao.bind();
  glDrawArrays(GL_TRIANGLES, 0, 6);
  m_bgVao.release();

  m_bgProgram.release();
  if (m_bgImageTexture)
    m_bgImageTexture->release();
  if (m_bgWhiteTexture)
    m_bgWhiteTexture->release();

  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
}

void Skin3DWidget::simulateDrag()
{
  if (!isVisible())
    return;

  // 按下 → 拖动（轻微位移）→ 松开，等效于用户手动拖拽
  // 注意：Paint 模式下左键用于绘制，必须用右键（旋转相机）模拟拖拽，
  //       否则会误在模型表面落笔。
  const Qt::MouseButton dragBtn = (m_interactionMode == InteractionMode::Paint && m_document)
                                    ? Qt::RightButton
                                    : Qt::LeftButton;

  const QPoint center(width() / 2, height() / 2);
  const QPoint end = center + QPoint(10, 0);

  QMouseEvent press(QEvent::MouseButtonPress, center, center,
                    dragBtn, dragBtn, Qt::NoModifier);
  QMouseEvent move(QEvent::MouseMove, end, end,
                   Qt::NoButton, dragBtn, Qt::NoModifier);
  QMouseEvent release(QEvent::MouseButtonRelease, end, end,
                      dragBtn, Qt::NoButton, Qt::NoModifier);

  QCoreApplication::sendEvent(this, &press);
  QCoreApplication::sendEvent(this, &move);
  QCoreApplication::sendEvent(this, &release);

  // 立即触发一次渲染，确保 GL 内容在进入页面时即被绘制
  update();
}

// ============================================================
// OpenGL 初始化
// ============================================================
void Skin3DWidget::initializeGL()
{
  initializeOpenGLFunctions();

  // 编译着色器
  if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSource))
  {
    qWarning("[Skin3DWidget] Vertex shader compilation failed: %s", qPrintable(m_program.log()));
  }
  if (!m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSource))
  {
    qWarning("[Skin3DWidget] Fragment shader compilation failed: %s", qPrintable(m_program.log()));
  }
  // 显式绑定 attribute location（必须在 link() 之前）
  m_program.bindAttributeLocation("aPosition", 0);
  m_program.bindAttributeLocation("aNormal", 1);
  m_program.bindAttributeLocation("aUv", 2);
  if (!m_program.link())
  {
    qWarning("[Skin3DWidget] Shader linking failed: %s", qPrintable(m_program.log()));
  }

  // 启用深度测试与背面剔除
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  // 构建渐变背景资源（着色器 + 全屏四边形）
  buildBackgroundResources();

  // 构建皮肤几何体
  buildSkinGeometry();

  // 上传待处理的皮肤纹理
  if (!m_pendingSkin.isNull())
  {
    uploadTexture(m_pendingSkin);
    m_skinDirty = false;
  }

  emit ready();

  // 启动动画定时器（自动旋转或非待机动作时）
  if (m_autoRotate || m_pose != SkinPose::Idle)
  {
    m_animTimer.start();
  }
}

void Skin3DWidget::resizeGL(int w, int h)
{
  glViewport(0, 0, w, h);
}

void Skin3DWidget::showEvent(QShowEvent* e)
{
  QOpenGLWidget::showEvent(e);
  // 页面显示时确保动画定时器运行，并触发重绘
  // 解决 QOpenGLWidget 在 QStackedWidget 中切换页面后不自动 paintGL 的问题
  // 注意：m_skinDirty 也需要启动定时器，确保 paintGL 被调用以上传待处理纹理
  const bool needsTimer = m_autoRotate ||
                          m_inertiaActive ||
                          m_skinDirty ||
                          (m_pose != SkinPose::Idle) ||
                          // Paint 模式（3D 编辑器）：持续渲染循环（参考 skinview3d），
                          // 进入显示状态即启动，保证模型一定被 paintGL 绘制
                          (m_interactionMode == InteractionMode::Paint && m_document);
  if (needsTimer && !m_animTimer.isActive())
    m_animTimer.start();
  // 强制请求重绘（若 GL 已初始化会立即渲染，否则触发 initializeGL）
  update();
  // 延迟一次重绘请求，确保 QStackedWidget 页面切换后 GL 上下文完全就绪
  // QTimer::singleShot(0) 在事件循环回到后触发，此时窗口已完全显示
  QTimer::singleShot(0, this, [this]() { update(); });
  // 进入页面时模拟一次自动拖拽，强制走鼠标拖拽渲染路径，恢复被 QGraphicsEffect 破坏的 GL 渲染
  QTimer::singleShot(50, this, [this]() { simulateDrag(); });
  // 动画后延迟兜底重绘：覆盖 PageTransitionAnimator 的 FadeSlideUp 动画时长（350ms）+ 100ms 余量
  // 原因：QGraphicsOpacityEffect 在动画期间破坏 QOpenGLWidget 的 GL 渲染，
  //       动画结束后 effect 被移除，但 Skin3DWidget 不知情，需延迟触发重绘以恢复 GL 内容。
  // 此为兜底机制：正常情况下 MainWindow 的 transitionFinished 信号会调用 refreshSkin3DWidget()
  // 触发同步重绘；此延迟重绘用于应对动画被禁用、transitionFinished 未触发等边缘情况。
  QTimer::singleShot(450, this, [this]() { update(); });
}

void Skin3DWidget::paintGL()
{
  // 如果皮肤纹理待更新（setSkin 在 GL 未就绪时被调用），在此处补上
  if (m_skinDirty && !m_pendingSkin.isNull())
  {
    uploadTexture(m_pendingSkin);
    buildSkinGeometry();
    m_skinDirty = false;
  }
  // 安全兜底：若纹理为空但有待处理皮肤（initializeGL 在 setSkin 之前调用、
  // 或 GL 上下文重建后纹理丢失等边缘情况），立即上传
  if (!m_texture && !m_pendingSkin.isNull())
  {
    uploadTexture(m_pendingSkin);
    m_skinDirty = false;
  }

  // 清屏
  glClearColor(m_background.redF(), m_background.greenF(), m_background.blueF(), 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // 渐变背景（绘制在全屏四边形上，替代纯色背景）
  if (m_bgGradient)
  {
    drawBackground();
  }

  // ---- 相机平滑插值（当前值 → 目标值）----
  // 拖拽中：目标值由 mouseMoveEvent 直接更新
  // 惯性中：目标值由 onAnimTick 持续更新，当前值跟随
  // 静止：当前值缓慢趋近目标值
  const float lerpFactor = 0.25f; // 插值速度，越大越快
  m_theta += (m_targetTheta - m_theta) * lerpFactor;
  m_phi += (m_targetPhi - m_phi) * lerpFactor;
  m_radius += (m_targetRadius - m_radius) * lerpFactor;

  // 自动旋转（仅在非拖拽、非惯性时）
  if (m_autoRotate && !m_dragging && !m_inertiaActive)
  {
    m_targetTheta += 0.5f * 0.0166f; // ~0.5 rad/s
    if (m_targetTheta > float(M_PI * 2)) m_targetTheta -= float(M_PI * 2);
  }

  // 绑定着色器
  m_program.bind();

  // 投影矩阵
  QMatrix4x4 proj;
  const float aspect = float(width()) / float(height() ? height() : 1);
  proj.perspective(40.0f, aspect, 0.1f, 1000.0f);

  // 视图矩阵（球面坐标相机）
  QMatrix4x4 view;
  const float phi = qBound(-float(M_PI) / 3.0f, m_phi, float(M_PI) / 3.0f);
  const float sinP = std::sin(phi), cosP = std::cos(phi);
  // 以身体（模型整体）为中心：模型经 rootTransform 平移后世界 Y 范围为 [-1, 1]，
  // 中心在 y=0，相机看向此处使模型在视口内垂直居中
  const QVector3D target(0.0f, 0.0f, 0.0f);
  const QVector3D eye(
    target.x() + m_radius * sinP * std::sin(m_theta),
    target.y() + m_radius * cosP,
    target.z() + m_radius * sinP * std::cos(m_theta)
  );
  view.lookAt(eye, target, QVector3D(0, 1, 0));

  m_program.setUniformValue("uProj", proj);
  m_program.setUniformValue("uView", view);
  m_program.setUniformValue("uTex", 0);

  // 绑定纹理
  if (m_texture)
  {
    m_texture->bind(0);
  }

  // 渲染所有身体部件
  // 顺序：内层先（不透明），外层后（透明测试）
  auto renderPart = [&](BodyPart& part)
  {
    if (!part.visible) return;

    // 模型矩阵 = 根变换 * 部件变换(pivot) * 关节旋转
    QMatrix4x4 model = m_skin.rootTransform;
    model *= part.partTransform * part.rotation;

    // 内层
    if (m_innerVisible && part.inner.indexCount > 0)
    {
      m_program.setUniformValue("uModel", model);
      m_program.setUniformValue("uAlphaTest", 0.5f);
      part.inner.vao.bind();
      glDrawElements(GL_TRIANGLES, part.inner.indexCount, GL_UNSIGNED_INT, 0);
      part.inner.vao.release();
    }
    // 外层（overlay）
    if (m_outerVisible && part.outer.indexCount > 0)
    {
      m_program.setUniformValue("uModel", model);
      m_program.setUniformValue("uAlphaTest", 0.5f);
      // 外层禁用背面剔除以支持双面
      glDisable(GL_CULL_FACE);
      part.outer.vao.bind();
      glDrawElements(GL_TRIANGLES, part.outer.indexCount, GL_UNSIGNED_INT, 0);
      part.outer.vao.release();
      glEnable(GL_CULL_FACE);
    }
  };

  renderPart(m_skin.head);
  renderPart(m_skin.body);
  renderPart(m_skin.rightArm);
  renderPart(m_skin.leftArm);
  renderPart(m_skin.rightLeg);
  renderPart(m_skin.leftLeg);

  if (m_texture) m_texture->release();
  m_program.release();
}

// ============================================================
// 模型规格（数据来自 Blockbench skin.ts）
// 坐标系：X 右, Y 上, Z 后（minecraft 像素）
// pivot 是旋转中心，origin 是 cube 最小角
// ============================================================
Skin3DWidget::SkinSpec Skin3DWidget::getModelSpec(bool slim) const
{
  SkinSpec spec;
  spec.slim = slim;

  // 手臂宽度：Classic=4, Slim=3
  const float armW = slim ? 3.0f : 4.0f;
  // 右臂 X 起点：Classic=-8, Slim=-7
  const float rArmX = slim ? -7.0f : -8.0f;

  // ---- 头部 ----
  // pivot=[0, 24, 0], inner: origin=[-4,24,-4] size=[8,8,8] uv=[0,0]
  // outer(hat): origin=[-4,24,-4] size=[8,8,8] uv=[32,0] inflate=0.5
  spec.head.px = 0.0f;  spec.head.py = 24.0f; spec.head.pz = 0.0f;
  spec.head.inner = {-4, 24, -4, 8, 8, 8, 0, 0, 0.0f};
  spec.head.outer = {-4, 24, -4, 8, 8, 8, 32, 0, 0.5f};

  // ---- 身体 ----
  // pivot=[0, 24, 0], inner: origin=[-4,12,-2] size=[8,12,4] uv=[16,16]
  // outer(jacket): uv=[16,32] inflate=0.25
  spec.body.px = 0.0f;  spec.body.py = 24.0f; spec.body.pz = 0.0f;
  spec.body.inner = {-4, 12, -2, 8, 12, 4, 16, 16, 0.0f};
  spec.body.outer = {-4, 12, -2, 8, 12, 4, 16, 32, 0.25f};

  // ---- 右臂 ----
  // pivot=[-5, 22, 0], inner: origin=[rArmX,12,-2] size=[armW,12,4] uv=[40,16]
  // outer(sleeve): uv=[40,32] inflate=0.25
  spec.rightArm.px = -5.0f; spec.rightArm.py = 22.0f; spec.rightArm.pz = 0.0f;
  spec.rightArm.inner = {rArmX, 12, -2, armW, 12, 4, 40, 16, 0.0f};
  spec.rightArm.outer = {rArmX, 12, -2, armW, 12, 4, 40, 32, 0.25f};

  // ---- 左臂 ----
  // pivot=[5, 22, 0], inner: origin=[4,12,-2] size=[armW,12,4] uv=[32,48]
  // outer(sleeve): uv=[48,48] inflate=0.25
  spec.leftArm.px = 5.0f; spec.leftArm.py = 22.0f; spec.leftArm.pz = 0.0f;
  spec.leftArm.inner = {4, 12, -2, armW, 12, 4, 32, 48, 0.0f};
  spec.leftArm.outer = {4, 12, -2, armW, 12, 4, 48, 48, 0.25f};

  // ---- 右腿 ----
  // pivot=[-1.9, 12, 0], inner: origin=[-3.9,0,-2] size=[4,12,4] uv=[0,16]
  // outer(pants): uv=[0,32] inflate=0.25
  spec.rightLeg.px = -1.9f; spec.rightLeg.py = 12.0f; spec.rightLeg.pz = 0.0f;
  spec.rightLeg.inner = {-3.9f, 0, -2, 4, 12, 4, 0, 16, 0.0f};
  spec.rightLeg.outer = {-3.9f, 0, -2, 4, 12, 4, 0, 32, 0.25f};

  // ---- 左腿 ----
  // pivot=[1.9, 12, 0], inner: origin=[-0.1,0,-2] size=[4,12,4] uv=[16,48]
  // outer(pants): uv=[0,48] inflate=0.25
  spec.leftLeg.px = 1.9f; spec.leftLeg.py = 12.0f; spec.leftLeg.pz = 0.0f;
  spec.leftLeg.inner = {-0.1f, 0, -2, 4, 12, 4, 16, 48, 0.0f};
  spec.leftLeg.outer = {-0.1f, 0, -2, 4, 12, 4, 0, 48, 0.25f};

  return spec;
}

// ============================================================
// 面 UV 矩形（像素坐标）
// 与 buildCubeMesh 的 faceUV 表严格一致，供拾取/绘制复用：
//   0=Top(+Y) 1=Bottom(-Y) 2=Front(+Z) 3=Back(-Z) 4=Right(+X) 5=Left(-X)
// ============================================================
Skin3DWidget::UvRect Skin3DWidget::faceUvRect(const CubeSpec& spec, int faceIndex) const
{
  const float w = spec.sx;
  const float h = spec.sy;
  const float d = spec.sz;
  const float u0 = spec.u;
  const float v0 = spec.v;
  switch (faceIndex)
  {
  case 0: return {u0 + d, u0 + d + w, v0, v0 + d};                    // Top
  case 1: return {u0 + d + w, u0 + d + 2 * w, v0, v0 + d};            // Bottom
  case 2: return {u0 + 2 * d + w, u0 + 2 * d + 2 * w, v0 + d, v0 + d + h}; // Front
  case 3: return {u0 + d, u0 + d + w, v0 + d, v0 + d + h};            // Back
  case 4: return {u0, u0 + d, v0 + d, v0 + d + h};                    // Right
  case 5: return {u0 + d + w, u0 + 2 * d + w, v0 + d, v0 + d + h};    // Left
  }
  return {0.0f, 0.0f, 0.0f, 0.0f};
}

// ============================================================
// Cube mesh 构建（标准 Box UV 算法）
//
// 顶点存储为相对于 pivot 的坐标（origin - pivot），缩放后再加上 inflate 偏移。
// 这样旋转矩阵直接绕原点（即 pivot 位置）旋转。
//
// 面顺序（与 UV 布局对应）：
//   Top(+Y), Bottom(-Y), Front(+Z), Back(-Z), Right(+X), Left(-X)
// 每面 4 顶点，逆时针排列（OpenGL CCW 正面）
// ============================================================
void Skin3DWidget::buildCubeMesh(CubeMesh& mesh, const CubeSpec& spec, float scale)
{
  // 应用 inflate：origin 减去 inflate，size 加上 2*inflate
  const float inf = spec.inflate;
  const float x0 = (spec.ox - inf) * scale;
  const float x1 = (spec.ox + spec.sx + inf) * scale;
  const float y0 = (spec.oy - inf) * scale;
  const float y1 = (spec.oy + spec.sy + inf) * scale;
  const float z0 = (spec.oz - inf) * scale;
  const float z1 = (spec.oz + spec.sz + inf) * scale;

  const float INV = 1.0f / TEX_SIZE;

  // 6 个面的 UV 区域（像素坐标，严格遵循 Blockbench face_data 标准布局）
  // Blockbench face_data 布局（posx=u0, posy=v0, t.x=w, t.y=h, t.z=d）：
  //   up(Top)     : x=[u0+d,    u0+d+w]    y=[v0,    v0+d]   尺寸 w×d
  //   down(Bottom): x=[u0+d+w,  u0+d+2w]   y=[v0,    v0+d]   尺寸 w×d
  //   east(Right) : x=[u0,      u0+d]      y=[v0+d,  v0+d+h] 尺寸 d×h
  //   north(Back) : x=[u0+d,    u0+d+w]    y=[v0+d,  v0+d+h] 尺寸 w×h
  //   west(Left)  : x=[u0+d+w,  u0+2d+w]   y=[v0+d,  v0+d+h] 尺寸 d×h
  //   south(Front): x=[u0+2d+w, u0+2d+2w]  y=[v0+d,  v0+d+h] 尺寸 w×h
  // 顺序：Top, Bottom, Front, Back, Right, Left
  const UvRect faceUV[6] = {
    faceUvRect(spec, 0),
    faceUvRect(spec, 1),
    faceUvRect(spec, 2),
    faceUvRect(spec, 3),
    faceUvRect(spec, 4),
    faceUvRect(spec, 5)
  };

  // 8 个顶点（相对于 pivot 的坐标，已缩放）
  // 命名：v0..v7，x0<x1, y0<y1, z0<z1
  // v0=(x0,y1,z0) v1=(x1,y1,z0) v2=(x0,y1,z1) v3=(x1,y1,z1)  顶部
  // v4=(x0,y0,z0) v5=(x1,y0,z0) v6=(x0,y0,z1) v7=(x1,y0,z1)  底部
  const QVector3D verts[8] = {
    {x0, y1, z0}, // 0 顶左前
    {x1, y1, z0}, // 1 顶右前
    {x0, y1, z1}, // 2 顶左后
    {x1, y1, z1}, // 3 顶右后
    {x0, y0, z0}, // 4 底左前
    {x1, y0, z0}, // 5 底右前
    {x0, y0, z1}, // 6 底左后
    {x1, y0, z1}  // 7 底右后
  };

  // 6 个面的顶点索引（每面 4 顶点，逆时针 CCW，从外部看）
  // 三角形 (0,1,2) 和 (0,2,3) 在 OpenGL 默认 CCW=正面 下可见
  // 已通过叉积验证每个面的三角形法线与该面法线方向一致
  // 面顺序：Top, Bottom, Front, Back, Right, Left
  static const int FACE_VERTS[6][4] = {
    // Top (+Y): 从 +Y 看（right=+X, up=-Z），CCW 顺序
    {2, 3, 1, 0},
    // Bottom (-Y): 从 -Y 看（right=-X, up=+Z），CCW 顺序
    {5, 7, 6, 4},
    // Front (+Z): 从 +Z 看（right=+X, up=+Y）
    {6, 7, 3, 2},
    // Back (-Z): 从 -Z 看（right=-X, up=+Y）
    {5, 4, 0, 1},
    // Right (+X): 从 +X 看（right=-Z, up=+Y）
    {1, 3, 7, 5},
    // Left (-X): 从 -X 看（right=+Z, up=+Y）
    {4, 6, 2, 0}
  };

  // 每个面 4 个顶点对应的 UV 角索引
  // UV 角定义：0=TL(u0,v0), 1=TR(u1,v0), 2=BR(u1,v1), 3=BL(u0,v1)
  // 根据每个面从外部看的顶点位置（TL/TR/BR/BL）分配 UV
  static const int FACE_UV_ORDER[6][4] = {
    // Top: v2=BL, v3=BR, v1=TR, v0=TL
    {3, 2, 1, 0},
    // Bottom: v5=BL, v7=TL, v6=TR, v4=BR
    {3, 0, 1, 2},
    // Front: v6=BL, v7=BR, v3=TR, v2=TL
    {3, 2, 1, 0},
    // Back: v5=BL, v4=BR, v0=TR, v1=TL
    {3, 2, 1, 0},
    // Right: v1=TR, v3=TL, v7=BL, v5=BR
    {1, 0, 3, 2},
    // Left: v4=BL, v6=BR, v2=TR, v0=TL
    {3, 2, 1, 0}
  };

  // 6 个面的法线
  static const QVector3D FACE_NORMALS[6] = {
    {0, 1, 0},   // Top
    {0, -1, 0},  // Bottom
    {0, 0, 1},   // Front
    {0, 0, -1},  // Back
    {1, 0, 0},   // Right
    {-1, 0, 0}   // Left
  };

  // 构建顶点数据（24 顶点 = 6 面 × 4 顶点）
  struct Vertex { QVector3D pos; QVector3D normal; QVector2D uv; };
  std::array<Vertex, 24> vertices;

  for (int face = 0; face < 6; ++face)
  {
    const UvRect& uv = faceUV[face];
    // UV 四角：0=TL, 1=TR, 2=BR, 3=BL
    const float uvCorners[4][2] = {
      {uv.u0 * INV, uv.v0 * INV},  // TL
      {uv.u1 * INV, uv.v0 * INV},  // TR
      {uv.u1 * INV, uv.v1 * INV},  // BR
      {uv.u0 * INV, uv.v1 * INV}   // BL
    };

    for (int i = 0; i < 4; ++i)
    {
      const int vi = face * 4 + i;
      const int uvIdx = FACE_UV_ORDER[face][i];
      vertices[vi].pos = verts[FACE_VERTS[face][i]];
      vertices[vi].normal = FACE_NORMALS[face];
      vertices[vi].uv = QVector2D(uvCorners[uvIdx][0], uvCorners[uvIdx][1]);
    }
  }

  // 索引（6 面 × 2 三角形 × 3 索引 = 36）
  // 标准 quad 三角化：(0,1,2), (0,2,3)，CCW 朝外
  std::array<GLuint, 36> indices;
  for (int face = 0; face < 6; ++face)
  {
    const int base = face * 4;
    const int idx = face * 6;
    // 三角形 1: v0, v1, v2
    indices[idx + 0] = base + 0;
    indices[idx + 1] = base + 1;
    indices[idx + 2] = base + 2;
    // 三角形 2: v0, v2, v3
    indices[idx + 3] = base + 0;
    indices[idx + 4] = base + 2;
    indices[idx + 5] = base + 3;
  }
  mesh.indexCount = 36;

  // 上传到 GPU
  mesh.vao.create();
  mesh.vao.bind();

  mesh.vbo.create();
  mesh.vbo.bind();
  mesh.vbo.allocate(vertices.data(), 24 * sizeof(Vertex));

  mesh.ibo.create();
  mesh.ibo.bind();
  mesh.ibo.allocate(indices.data(), 36 * sizeof(GLuint));

  // 属性绑定
  // aPosition (location 0): vec3
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
  // aNormal (location 1): vec3
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(sizeof(QVector3D)));
  // aUv (location 2): vec2
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void*>(2 * sizeof(QVector3D)));

  mesh.vao.release();
}

// ============================================================
// 构建整个皮肤几何体
// ============================================================
void Skin3DWidget::buildSkinGeometry()
{
  // 先销毁旧资源
  auto destroyPart = [](BodyPart& p)
  {
    p.inner.destroy();
    p.outer.destroy();
  };
  destroyPart(m_skin.head);
  destroyPart(m_skin.body);
  destroyPart(m_skin.rightArm);
  destroyPart(m_skin.leftArm);
  destroyPart(m_skin.rightLeg);
  destroyPart(m_skin.leftLeg);

  // 获取模型规格
  const SkinSpec spec = getModelSpec(m_skin.slim);

  // 坐标缩放：1 minecraft 像素 = 1/16 OpenGL 单位
  // 顶点存储为相对于 pivot 的坐标，所以 buildCubeMesh 内部会减去 pivot
  const float S = 1.0f / 16.0f;

  // 构建各部件（顶点 = (origin - pivot) * scale）
  auto buildPart = [&](BodyPart& part, const PartSpec& ps)
  {
    // 内层：顶点 = (origin - pivot) * scale
    CubeSpec inner = ps.inner;
    inner.ox -= ps.px;
    inner.oy -= ps.py;
    inner.oz -= ps.pz;
    buildCubeMesh(part.inner, inner, S);

    // 外层：同样减去 pivot
    CubeSpec outer = ps.outer;
    outer.ox -= ps.px;
    outer.oy -= ps.py;
    outer.oz -= ps.pz;
    buildCubeMesh(part.outer, outer, S);

    // partTransform = translate(pivot * scale)
    // 这样旋转后模型回到正确位置
    part.partTransform.setToIdentity();
    part.partTransform.translate(ps.px * S, ps.py * S, ps.pz * S);
  };

  buildPart(m_skin.head, spec.head);
  buildPart(m_skin.body, spec.body);
  buildPart(m_skin.rightArm, spec.rightArm);
  buildPart(m_skin.leftArm, spec.leftArm);
  buildPart(m_skin.rightLeg, spec.rightLeg);
  buildPart(m_skin.leftLeg, spec.leftLeg);

  // 根变换：将模型中心移到 OpenGL 原点附近
  // 模型 y 范围 [0, 32]（脚底到头顶），缩放后 [0, 2]
  // 将 y=0 移到 y=-1，使模型中心在原点
  m_skin.rootTransform.setToIdentity();
  m_skin.rootTransform.translate(0.0f, -1.0f, 0.0f);

  // 重建几何体后重新应用"只显示选中部位"状态（buildPart 不触碰 visible）
  if (m_visiblePart >= 0)
    setVisiblePart(m_visiblePart);
}

// ============================================================
// Slim 检测（复刻 skinview-utils isSlimSkin）
// 检测 64x64 皮肤上 4 个 slim 空白区域
// ============================================================
bool Skin3DWidget::isSlimSkin(const QImage& image)
{
  if (image.width() < 64 || image.height() < 64) return false;

  QImage img = image;
  if (img.format() != QImage::Format_RGBA8888)
  {
    img = img.convertToFormat(QImage::Format_RGBA8888);
  }
  if (img.width() != 64 || img.height() != 64)
  {
    img = img.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::FastTransformation);
  }

  // 检测区域是否透明
  auto checkTransparency = [&](int x, int y, int w, int h) -> bool
  {
    for (int i = 0; i < w; ++i)
      for (int j = 0; j < h; ++j)
        if (qAlpha(img.pixel(x + i, y + j)) != 255) return true;
    return false;
  };
  // 检测区域是否为纯色
  auto checkSolid = [&](int x, int y, int w, int h, int target) -> bool
  {
    for (int i = 0; i < w; ++i)
      for (int j = 0; j < h; ++j)
      {
        QRgb p = img.pixel(x + i, y + j);
        if (qRed(p) != target || qGreen(p) != target || qBlue(p) != target) return false;
      }
    return true;
  };
  auto checkBlack = [&](int x, int y, int w, int h) { return checkSolid(x, y, w, h, 0); };
  auto checkWhite = [&](int x, int y, int w, int h) { return checkSolid(x, y, w, h, 255); };

  // 4 个 slim 空白区域（右臂和左臂的腋下区域）
  return checkTransparency(50, 16, 2, 4) ||
         checkTransparency(54, 20, 2, 12) ||
         checkTransparency(42, 48, 2, 4) ||
         checkTransparency(46, 52, 2, 12) ||
         (checkBlack(50, 16, 2, 4) && checkBlack(54, 20, 2, 12) &&
          checkBlack(42, 48, 2, 4) && checkBlack(46, 52, 2, 12)) ||
         (checkWhite(50, 16, 2, 4) && checkWhite(54, 20, 2, 12) &&
          checkWhite(42, 48, 2, 4) && checkWhite(46, 52, 2, 12));
}

// ============================================================
// 纹理上传
// ============================================================
void Skin3DWidget::uploadTexture(const QImage& image)
{
  if (m_texture)
  {
    m_texture->destroy();
    delete m_texture;
    m_texture = nullptr;
  }

  // 规范化到 64x64
  QImage tex = image;
  if (tex.width() != 64 || tex.height() != 64)
  {
    int targetH = (image.height() >= 64) ? 64 : 32;
    tex = image.scaled(64, targetH, Qt::IgnoreAspectRatio, Qt::FastTransformation);
  }
  if (tex.format() != QImage::Format_RGBA8888)
  {
    tex = tex.convertToFormat(QImage::Format_RGBA8888);
  }

  // QOpenGLTexture(QImage) 会自动处理 Y 翻转，
  // 使得 QImage 顶部对应纹理 t=0，与 Blockbench UV v_pixel/64 一致
  m_texture = new QOpenGLTexture(tex);
  m_texture->setMinificationFilter(QOpenGLTexture::Nearest); // 像素风
  m_texture->setMagnificationFilter(QOpenGLTexture::Nearest);
  m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
}

// ============================================================
// 鼠标交互（轨道相机 + 惯性；Paint 模式下左键绘制）
// ============================================================
void Skin3DWidget::mousePressEvent(QMouseEvent* e)
{
  if (m_interactionMode == InteractionMode::Paint && m_document)
  {
    // 左键：优先尝试开始绘制；若按下位置未命中模型（空白处），
    // 降级为旋转相机——参考 Blockbench 的交互习惯（模型上绘制、空白处旋转），
    // 同时兼容用户在预览模式下"左键拖拽旋转"的既有习惯。
    if (e->button() == Qt::LeftButton)
    {
      if (!startStroke(e->pos()))
        beginOrbit(e);
      e->accept();
      return;
    }
    // 右键/中键：旋转相机
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton)
    {
      beginOrbit(e);
      e->accept();
      return;
    }
    e->accept();
    return;
  }
  beginOrbit(e);
}

void Skin3DWidget::mouseMoveEvent(QMouseEvent* e)
{
  if (m_interactionMode == InteractionMode::Paint && m_document)
  {
    if (m_painting && (e->buttons() & Qt::LeftButton))
    {
      continueStroke(e->pos());
      e->accept();
      return;
    }
    if (m_dragging)
    {
      orbitMove(e);
      e->accept();
      return;
    }
    // 悬停拾取：更新状态栏像素坐标（画笔/橡皮时）
    if (m_tool == DrawTool::Brush || m_tool == DrawTool::Eraser)
    {
      const PickResult r = pick(e->pos());
      emit pixelHovered(r.hit ? r.px : -1, r.hit ? r.py : -1);
    }
    e->accept();
    return;
  }
  if (m_dragging)
    orbitMove(e);
}

void Skin3DWidget::mouseReleaseEvent(QMouseEvent* e)
{
  if (m_interactionMode == InteractionMode::Paint && m_document)
  {
    if (m_painting && e->button() == Qt::LeftButton)
    {
      endStroke(e->pos());
      e->accept();
      return;
    }
    if (m_dragging && e->button() == m_dragButton)
    {
      endOrbit(e);
      e->accept();
      return;
    }
    e->accept();
    return;
  }
  if (m_dragging && e->button() == m_dragButton)
    endOrbit(e);
}

void Skin3DWidget::beginOrbit(QMouseEvent* e)
{
  m_dragging = true;
  m_dragButton = e->button();
  m_lastPos = e->pos();
  // 停止惯性
  m_inertiaActive = false;
  m_velTheta = 0.0f;
  m_velPhi = 0.0f;
}

void Skin3DWidget::orbitMove(QMouseEvent* e)
{
  if (!m_dragging) return;
  const QPoint delta = e->pos() - m_lastPos;
  m_lastPos = e->pos();
  const float sensitivity = 0.008f;
  const float dTheta = -delta.x() * sensitivity;
  const float dPhi = -delta.y() * sensitivity;

  // 更新目标角度
  m_targetTheta += dTheta;
  m_targetPhi += dPhi;
  m_targetPhi = qBound(-float(M_PI) / 3.0f, m_targetPhi, float(M_PI) / 3.0f);

  // 记录速度（用于松手后的惯性）
  m_velTheta = dTheta;
  m_velPhi = dPhi;

  // 确保动画定时器运行以驱动平滑插值
  if (!m_animTimer.isActive())
    m_animTimer.start();
}

void Skin3DWidget::endOrbit(QMouseEvent* e)
{
  Q_UNUSED(e);
  if (m_dragging)
  {
    // 速度足够大时启动惯性
    const float speedSq = m_velTheta * m_velTheta + m_velPhi * m_velPhi;
    if (speedSq > 0.0001f)
    {
      m_inertiaActive = true;
      if (!m_animTimer.isActive())
        m_animTimer.start();
    }
  }
  m_dragging = false;
  m_dragButton = Qt::NoButton;
}

void Skin3DWidget::wheelEvent(QWheelEvent* e)
{
  const int delta = e->angleDelta().y();
  m_targetRadius -= delta * 0.005f;
  m_targetRadius = qBound(1.0f, m_targetRadius, 8.0f);
  // 确保动画定时器运行以驱动平滑缩放
  if (!m_animTimer.isActive())
    m_animTimer.start();
}

// ============================================================
// 动画定时器（惯性衰减 + 自动旋转）
// ============================================================
void Skin3DWidget::onAnimTick()
{
  // 模型动作/姿势动画：非待机（且非 Paint 编辑模式）时驱动关节旋转
  if (m_pose != SkinPose::Idle && !(m_interactionMode == InteractionMode::Paint && m_document))
  {
    m_poseTime += 0.016f; // ~60fps
    applyPoseAnimation(m_poseTime);
  }

  // 惯性衰减
  if (m_inertiaActive && !m_dragging)
  {
    m_targetTheta += m_velTheta;
    m_targetPhi += m_velPhi;
    m_targetPhi = qBound(-float(M_PI) / 3.0f, m_targetPhi, float(M_PI) / 3.0f);

    // 速度衰减（摩擦系数）
    const float friction = 0.92f;
    m_velTheta *= friction;
    m_velPhi *= friction;

    // 速度足够小时停止惯性
    const float speedSq = m_velTheta * m_velTheta + m_velPhi * m_velPhi;
    if (speedSq < 0.000001f)
    {
      m_inertiaActive = false;
      m_velTheta = 0.0f;
      m_velPhi = 0.0f;
    }
  }

  // 判断是否需要继续定时器
  const bool needsAnimation = m_inertiaActive ||
                              m_autoRotate ||
                              m_dragging ||
                              m_skinDirty ||
                              // 非待机动作需要持续驱动关节旋转
                              (m_pose != SkinPose::Idle) ||
                              // Paint 模式（3D 编辑器）：参考 skinview3d 的持续渲染，
                              // 只要可见就保持渲染循环，确保模型总是被绘制
                              (m_interactionMode == InteractionMode::Paint &&
                               m_document && isVisible());
  // 检查插值是否还在进行（当前值与目标值差距大）
  const float thetaDiff = std::abs(m_targetTheta - m_theta);
  const float phiDiff = std::abs(m_targetPhi - m_phi);
  const float radiusDiff = std::abs(m_targetRadius - m_radius);
  const bool interpolating = (thetaDiff > 0.0001f || phiDiff > 0.0001f || radiusDiff > 0.0001f);

  if (!needsAnimation && !interpolating)
  {
    m_animTimer.stop();
  }

  update();
}

// ============================================================
// 资源释放
// ============================================================
void Skin3DWidget::destroyGLResources()
{
  if (m_texture)
  {
    m_texture->destroy();
    delete m_texture;
    m_texture = nullptr;
  }
  auto destroyPart = [](BodyPart& p)
  {
    p.inner.destroy();
    p.outer.destroy();
  };
  destroyPart(m_skin.head);
  destroyPart(m_skin.body);
  destroyPart(m_skin.rightArm);
  destroyPart(m_skin.leftArm);
  destroyPart(m_skin.rightLeg);
  destroyPart(m_skin.leftLeg);

  // 渐变背景资源
  if (m_bgVbo.isCreated())
    m_bgVbo.destroy();
  if (m_bgVao.isCreated())
    m_bgVao.destroy();
  m_bgProgram.removeAllShaders();
  m_bgDirty = true;

  // 图片背景纹理
  if (m_bgImageTexture)
  {
    m_bgImageTexture->destroy();
    delete m_bgImageTexture;
    m_bgImageTexture = nullptr;
  }
  if (m_bgWhiteTexture)
  {
    m_bgWhiteTexture->destroy();
    delete m_bgWhiteTexture;
    m_bgWhiteTexture = nullptr;
  }
  m_bgImageDirty = true;
}

// ============================================================
// 3D 拾取（屏幕 → 模型表面 → 纹理像素）
//
// 算法：用与 paintGL 完全一致的投影/视图矩阵构造视线射线，
// 对 6 个部件 × {内层, 外层} 共 12 个轴对齐盒做 slab 相交测试，
// 取最近命中；命中点通过 Box UV 布局反算纹理像素坐标。
// ============================================================
bool Skin3DWidget::intersectLocalBox(const QVector3D& ro, const QVector3D& rd,
                                     const QVector3D& bmin, const QVector3D& bmax,
                                     float& tEnter, int& faceIndex)
{
  float tmin = 0.0f;
  float tmax = 1e9f;
  int side = -1;
  const float eps = 1e-9f;
  for (int a = 0; a < 3; ++a)
  {
    const float o = ro[a];
    const float d = rd[a];
    const float lo = bmin[a];
    const float hi = bmax[a];
    if (std::abs(d) < eps)
    {
      if (o < lo || o > hi)
        return false;
      continue;
    }
    const float inv = 1.0f / d;
    float t0 = (lo - o) * inv;
    float t1 = (hi - o) * inv;
    // side 编码：0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
    // s0 绑定 lo（-轴）面、s1 绑定 hi（+轴）面，swap 时跟随 t0。
    // 这样 t0 取最小（进入盒子的时刻）时，s0 一定是"先碰到的那个面"：
    //   rd 向 +轴 走 → 先碰 -轴 面（lo）；rd 向 -轴 走 → 先碰 +轴 面（hi）。
    // （此前 s0/s1 按"方向先碰面"赋值，与 t0/t1 的 lo/hi 语义在 swap 时不匹配，
    //   导致返回的面与实际进入面相反——正面点击拾取到背面，画与显示不一致。）
    int s0 = a * 2 + 1;  // lo → -轴
    int s1 = a * 2;      // hi → +轴
    if (t0 > t1)
    {
      std::swap(t0, t1);
      std::swap(s0, s1);
    }
    if (t0 > tmin)
    {
      tmin = t0;
      side = s0;
    }
    if (t1 < tmax)
      tmax = t1;
    if (tmin > tmax)
      return false;
  }
  if (side < 0)
    return false;

  tEnter = tmin;
  // side → faceIndex（0=Top 1=Bottom 2=Front 3=Back 4=Right 5=Left）
  switch (side)
  {
  case 0:  faceIndex = 4; break; // +X → Right
  case 1:  faceIndex = 5; break; // -X → Left
  case 2:  faceIndex = 0; break; // +Y → Top
  case 3:  faceIndex = 1; break; // -Y → Bottom
  case 4:  faceIndex = 2; break; // +Z → Front
  default: faceIndex = 3; break; // -Z → Back
  }
  return true;
}

Skin3DWidget::PickResult Skin3DWidget::pick(const QPoint& widgetPos) const
{
  PickResult res;
  // GL 未初始化（控件首次显示前）不做拾取
  if (!(context() && context()->isValid() && m_program.isLinked()))
    return res;

  // 与 paintGL 完全一致的投影/视图矩阵
  QMatrix4x4 proj;
  const float aspect = float(width()) / float(height() ? height() : 1);
  proj.perspective(40.0f, aspect, 0.1f, 1000.0f);
  QMatrix4x4 view;
  const float phi = qBound(-float(M_PI) / 3.0f, m_phi, float(M_PI) / 3.0f);
  const float sinP = std::sin(phi), cosP = std::cos(phi);
  const QVector3D target(0.0f, 0.0f, 0.0f);
  const QVector3D eye(target.x() + m_radius * sinP * std::sin(m_theta),
                      target.y() + m_radius * cosP,
                      target.z() + m_radius * sinP * std::cos(m_theta));
  view.lookAt(eye, target, QVector3D(0, 1, 0));

  // 屏幕坐标 → 世界空间射线（map 自动做透视除法）
  const QMatrix4x4 invVP = (proj * view).inverted();
  const float ndcX = (2.0f * widgetPos.x() / float(width()) - 1.0f);
  const float ndcY = (1.0f - 2.0f * widgetPos.y() / float(height()));
  // 逆透视投影必须用 QVector4D 变换后再除以 w！
  // 注意：QMatrix4x4::map(QVector3D) 会忽略 w 分量、不做透视除法，
  // 对投影矩阵的逆（含透视）使用它会导致反投影偏移、拾取命中率极低。
  const QVector4D vNear = invVP.map(QVector4D(ndcX, ndcY, -1.0f, 1.0f));
  const QVector4D vFar  = invVP.map(QVector4D(ndcX, ndcY, 1.0f, 1.0f));
  const QVector3D pNear(vNear.x() / vNear.w(), vNear.y() / vNear.w(), vNear.z() / vNear.w());
  const QVector3D pFar(vFar.x() / vFar.w(), vFar.y() / vFar.w(), vFar.z() / vFar.w());
  const QVector3D dir = (pFar - pNear).normalized();

  const SkinSpec spec = getModelSpec(m_skin.slim);
  const float S = 1.0f / 16.0f;
  const PartSpec* parts[6] = { &spec.head, &spec.body, &spec.rightArm,
                               &spec.leftArm, &spec.rightLeg, &spec.leftLeg };

  auto partRotation = [this](int pi) -> const QMatrix4x4&
  {
    switch (pi)
    {
    case 0: return m_skin.head.rotation;
    case 1: return m_skin.body.rotation;
    case 2: return m_skin.rightArm.rotation;
    case 3: return m_skin.leftArm.rotation;
    case 4: return m_skin.rightLeg.rotation;
    default: return m_skin.leftLeg.rotation;
    }
  };

  // 世界射线 → 部件局部（pivot 居中、缩放后）空间
  auto localRay = [&](const PartSpec& ps, int pi, QVector3D& roOut, QVector3D& rdOut)
  {
    QMatrix4x4 M = m_skin.rootTransform;
    M.translate(ps.px * S, ps.py * S, ps.pz * S);
    M *= partRotation(pi);
    const QMatrix4x4 Minv = M.inverted();
    roOut = Minv.map(pNear);
    rdOut = Minv.mapVector(dir);
  };

  float bestT = 1e30f;
  for (int pi = 0; pi < 6; ++pi)
  {
    // 只显示选中部位时，隐藏的部件不参与拾取
    if (m_visiblePart >= 0 && pi != m_visiblePart)
      continue;

    const PartSpec& ps = *parts[pi];
    QVector3D ro, rd;
    localRay(ps, pi, ro, rd);

    // 对单个 cube 做相交测试（cube 需先减去 pivot 得到局部盒）
    auto testBox = [&](const CubeSpec& raw, float& t, int& face) -> bool
    {
      CubeSpec c = raw;
      c.ox -= ps.px;
      c.oy -= ps.py;
      c.oz -= ps.pz;
      const float inf = c.inflate;
      const QVector3D bmin((c.ox - inf) * S, (c.oy - inf) * S, (c.oz - inf) * S);
      const QVector3D bmax((c.ox + c.sx + inf) * S,
                           (c.oy + c.sy + inf) * S,
                           (c.oz + c.sz + inf) * S);
      return intersectLocalBox(ro, rd, bmin, bmax, t, face);
    };

    float t = 0.0f;
    int face = -1;
    if (testBox(ps.inner, t, face) && t > 0.0f && t < bestT)
    {
      bestT = t;
      res.hit = true;
      res.partIndex = pi;
      res.outer = false;
      res.faceIndex = face;
    }
    // 外层参与拾取需满足：64x64 格式 + 用户显式开启"绘制外层"。
    // 外层盒子是膨胀的（inflate 0.25~0.5），包裹内层，若默认参与拾取，
    // 点击模型表面会优先命中外层 → 画到 overlay（y>=32）区域，
    // 用户会看到"没勾选外层却在涂"、"画的位置反了"。
    // 参考 Blockbench 的图层设计：默认只画内层（基础层），显式开启才画外层。
    if (m_paintOuter && m_texIs64x64)
    {
      if (testBox(ps.outer, t, face) && t > 0.0f && t < bestT)
      {
        bestT = t;
        res.hit = true;
        res.partIndex = pi;
        res.outer = true;
        res.faceIndex = face;
      }
    }
  }

  if (!res.hit)
    return res;

  // 命中点局部坐标 → 面内 UV（Box UV 反算）
  const PartSpec& ps = *parts[res.partIndex];
  QVector3D ro, rd;
  localRay(ps, res.partIndex, ro, rd);
  const QVector3D p = ro + rd * bestT;

  const CubeSpec raw = res.outer ? ps.outer : ps.inner;
  CubeSpec c = raw;
  c.ox -= ps.px;
  c.oy -= ps.py;
  c.oz -= ps.pz;
  const UvRect uv = faceUvRect(c, res.faceIndex);

  // 命中点 p 是缩放后的局部坐标（1 像素 = S = 1/16 单位），
  // 而 x0/y0/z0 与 UV 偏移是"像素单位"——必须先换算成像素单位再反算 UV，
  // 否则 UV 会被压缩 S 倍（同面上拖拽几乎不动，画不出连续笔画）。
  const float px = p.x() / S;
  const float py = p.y() / S;
  const float pz = p.z() / S;

  const float inf = c.inflate;
  const float x0 = c.ox - inf, x1 = c.ox + c.sx + inf;
  const float y1 = c.oy + c.sy + inf;
  const float z0 = c.oz - inf, z1 = c.oz + c.sz + inf;
  const float w = c.sx, d = c.sz;
  const float u0 = c.u, v0 = c.v;

  // Box UV 反算：命中点像素坐标 → 面内 UV（与 faceUvRect 布局一一对应）
  // 参考：buildCubeMesh 中每面 4 角的 TL/TR/BR/BL 顶点与 UV 分配
  float u = 0.0f, v = 0.0f;
  switch (res.faceIndex)
  {
  case 0:  // Top(+Y)：u 沿 +X，v 沿 +Z
    u = u0 + d + (px - x0);
    v = v0 + (pz - z0);
    break;
  case 1:  // Bottom(-Y)：u 沿 -X，v 沿 +Z
    u = u0 + d + 2 * w - (px - x0);
    v = v0 + (z1 - pz);
    break;
  case 2:  // Front(+Z)：u 沿 +X，v 沿 -Y
    u = u0 + 2 * d + w + (px - x0);
    v = v0 + d + (y1 - py);
    break;
  case 3:  // Back(-Z)：u 沿 -X，v 沿 -Y
    u = u0 + d + (x1 - px);
    v = v0 + d + (y1 - py);
    break;
  case 4:  // Right(+X)：u 沿 -Z，v 沿 -Y
    u = u0 + (z1 - pz);
    v = v0 + d + (y1 - py);
    break;
  case 5:  // Left(-X)：u 沿 +Z，v 沿 -Y
    u = u0 + d + w + (pz - z0);
    v = v0 + d + (y1 - py);
    break;
  }

  // 限制在面矩形内（膨胀边缘越界时收敛到面边缘，避免画到相邻面区域）
  u = qBound(uv.u0, u, uv.u1 - 1e-4f);
  v = qBound(uv.v0, v, uv.v1 - 1e-4f);

  res.u = u;
  res.v = v;
  res.px = static_cast<int>(std::floor(u));
  res.py = static_cast<int>(std::floor(v));
  res.fu0 = static_cast<int>(std::floor(uv.u0));
  res.fv0 = static_cast<int>(std::floor(uv.v0));
  res.fw = static_cast<int>(std::ceil(uv.u1)) - res.fu0;
  res.fh = static_cast<int>(std::ceil(uv.v1)) - res.fv0;
  return res;
}

// ============================================================
// 3D 绘制（Paint 模式）
// ============================================================
void Skin3DWidget::stampForPick(const PickResult& r, QVector<QPoint>& out) const
{
  const int half = m_brushSize / 2;
  for (int dy = 0; dy < m_brushSize; ++dy)
  {
    for (int dx = 0; dx < m_brushSize; ++dx)
    {
      const int sx = r.px - half + dx;
      const int sy = r.py - half + dy;
      // 裁剪到所在面的整数 UV 矩形，避免笔画溢出到相邻面
      if (sx >= r.fu0 && sx < r.fu0 + r.fw && sy >= r.fv0 && sy < r.fv0 + r.fh)
        out.append(QPoint(sx, sy));
    }
  }
}

QVector<QPoint> Skin3DWidget::clipPointsToImage(const QVector<QPoint>& points) const
{
  if (m_document == nullptr)
    return {};
  const QImage& img = m_document->skinImage();
  QVector<QPoint> clipped;
  clipped.reserve(points.size());
  for (const QPoint& p : points)
  {
    if (p.x() >= 0 && p.x() < img.width() && p.y() >= 0 && p.y() < img.height())
      clipped.append(p);
  }
  return clipped;
}

void Skin3DWidget::updateStrokePreviewTexture()
{
  if (m_document == nullptr || m_pendingPoints.isEmpty())
    return;
  if (!(context() && context()->isValid() && m_program.isLinked()))
    return;

  QImage preview = m_document->skinImage();
  if (preview.format() != QImage::Format_ARGB32)
    preview = preview.convertToFormat(QImage::Format_ARGB32);
  for (const QPoint& p : m_pendingPoints)
  {
    if (p.x() >= 0 && p.x() < preview.width() && p.y() >= 0 && p.y() < preview.height())
      preview.setPixelColor(p.x(), p.y(), m_strokeColor);
  }
  makeCurrent();
  uploadTexture(preview);
  doneCurrent();
  update();
}

bool Skin3DWidget::startStroke(const QPoint& pos)
{
  if (m_document == nullptr || m_document->skinImage().isNull())
    return false;
  const PickResult r = pick(pos);
  if (!r.hit)
    return false;

  // 即时工具：填充 / 吸管
  if (m_tool == DrawTool::Fill)
  {
    m_document->fillRegion(r.px, r.py, m_fgColor);
    return true;
  }
  if (m_tool == DrawTool::Eyedropper)
  {
    const QColor c = m_document->skinImage().pixelColor(r.px, r.py);
    m_fgColor = c;
    m_tool = DrawTool::Brush;  // 拾取后切回画笔（与 2D 画布一致）
    updateCursor();
    emit colorPicked(c);
    return true;
  }

  // 画笔 / 橡皮 / 直线：进入笔画状态
  m_painting = true;
  m_strokePart = r.partIndex;
  m_strokeOuter = r.outer;
  m_strokeFace = r.faceIndex;
  m_lastPick = r;
  m_strokeColor = (m_tool == DrawTool::Eraser) ? QColor(0, 0, 0, 0) : m_fgColor;
  m_pendingPoints.clear();

  if (m_tool == DrawTool::Line)
  {
    m_lineStartUV = QPointF(r.u, r.v);
    return true;  // 直线在松开时一次性提交
  }

  stampForPick(r, m_pendingPoints);
  updateStrokePreviewTexture();
  return true;
}

void Skin3DWidget::continueStroke(const QPoint& pos)
{
  if (!m_painting)
    return;
  const PickResult r = pick(pos);
  if (!r.hit)
    return;
  emit pixelHovered(r.px, r.py);

  if (m_tool == DrawTool::Line)
    return;  // 直线在松开时提交

  // 与上次命中在同一部件/层/面：UV 空间线性插值，避免移动过快产生断点
  if (r.partIndex == m_strokePart && r.outer == m_strokeOuter && r.faceIndex == m_strokeFace)
  {
    const float du = r.u - m_lastPick.u;
    const float dv = r.v - m_lastPick.v;
    const float dist = std::sqrt(du * du + dv * dv);
    const int steps = qMax(1, static_cast<int>(std::ceil(dist)));
    for (int i = 0; i <= steps; ++i)
    {
      const float t = float(i) / float(steps);
      PickResult sr = r;
      sr.u = m_lastPick.u + du * t;
      sr.v = m_lastPick.v + dv * t;
      sr.px = static_cast<int>(std::floor(sr.u));
      sr.py = static_cast<int>(std::floor(sr.v));
      stampForPick(sr, m_pendingPoints);
    }
  }
  else
  {
    stampForPick(r, m_pendingPoints);
    m_strokePart = r.partIndex;
    m_strokeOuter = r.outer;
    m_strokeFace = r.faceIndex;
  }
  m_lastPick = r;
  updateStrokePreviewTexture();
}

void Skin3DWidget::endStroke(const QPoint& pos)
{
  if (!m_painting)
    return;

  if (m_tool == DrawTool::Line)
  {
    const PickResult r = pick(pos);
    if (r.hit)
    {
      QVector<QPoint> pts;
      if (r.partIndex == m_strokePart && r.outer == m_strokeOuter && r.faceIndex == m_strokeFace)
      {
        // 同面直线：UV 空间 Bresenham 插值
        const int x0 = static_cast<int>(std::floor(m_lineStartUV.x()));
        const int y0 = static_cast<int>(std::floor(m_lineStartUV.y()));
        const int x1 = r.px;
        const int y1 = r.py;
        int dx = qAbs(x1 - x0), dy = qAbs(y1 - y0);
        const int sx = (x0 < x1) ? 1 : -1;
        const int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;
        int x = x0, y = y0;
        while (true)
        {
          if (x >= r.fu0 && x < r.fu0 + r.fw && y >= r.fv0 && y < r.fv0 + r.fh)
            pts.append(QPoint(x, y));
          if (x == x1 && y == y1)
            break;
          const int e2 = 2 * err;
          if (e2 > -dy) { err -= dy; x += sx; }
          if (e2 < dx) { err += dx; y += sy; }
        }
      }
      else
      {
        stampForPick(r, pts);  // 跨面直线退化为单点
      }
      const QVector<QPoint> clipped = clipPointsToImage(pts);
      if (!clipped.isEmpty())
        m_document->setPixels(clipped, m_strokeColor);
    }
  }
  else
  {
    const QVector<QPoint> clipped = clipPointsToImage(m_pendingPoints);
    if (!clipped.isEmpty())
      m_document->setPixels(clipped, m_strokeColor);
  }

  m_pendingPoints.clear();
  m_painting = false;
  m_strokePart = -1;
  m_strokeFace = -1;
}

// ============================================================
// 光标
// ============================================================
void Skin3DWidget::updateCursor()
{
  if (m_interactionMode == InteractionMode::Paint)
  {
    switch (m_tool)
    {
    case DrawTool::Brush:
    case DrawTool::Eraser:
    case DrawTool::Line:
      setCursor(Qt::CrossCursor);
      break;
    case DrawTool::Eyedropper:
      setCursor(Qt::PointingHandCursor);
      break;
    default:
      setCursor(Qt::ArrowCursor);
      break;
    }
    return;
  }
  setCursor(Qt::ArrowCursor);
}