/**
 * @file   VoxelViewWidget.cpp
 * @brief  3D 体素方块视图控件实现（OpenGL）
 * @author BlockBox Team
 * @date   2026-08-18
 */

#include "VoxelViewWidget.h"

#include "components/GlslCompat.h"

#include "utils/Schematic/EntityModelLoader.h"

#include <QCursor>
#include <QDebug>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <QtMath>
#include <climits>
#include <cstring>
#include <new>

namespace {

// 6 面，每面 4 个角点（单位立方体坐标），顺序为「从面外看逆时针（CCW）」。
// 顺序：+X, -X, +Y, -Y, +Z, -Z；开启背面剔除后，只有朝向相机的面会被绘制。
static const float kFaceCorner[6][4][3] = {
    { {1,0,1}, {1,0,0}, {1,1,0}, {1,1,1} },   // +X
    { {0,0,0}, {0,0,1}, {0,1,1}, {0,1,0} },   // -X
    { {0,1,1}, {1,1,1}, {1,1,0}, {0,1,0} },   // +Y
    { {0,0,0}, {1,0,0}, {1,0,1}, {0,0,1} },   // -Y
    { {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1} },   // +Z
    { {0,0,0}, {0,1,0}, {1,1,0}, {1,0,0} },   // -Z
};

// 面朝向轴与符号（用于背面剔除与 AO 采样）
static const int kFaceAxis[6] = { 0, 0, 1, 1, 2, 2 };
static const int kFaceSign[6] = { 1, -1, 1, -1, 1, -1 };
// 面内两个切向轴
static const int kFaceTangent[6][2] = {
    {1, 2}, {1, 2}, {0, 2}, {0, 2}, {0, 1}, {0, 1}
};

// 每面基础光（略带方向性，让同样的方块也有体积感）
static const float kFaceLight[6] = { 0.94f, 0.80f, 1.0f, 0.58f, 0.88f, 0.72f };

void rotatePoint3(float &x, float &y, float &z,
                  float ox, float oy, float oz, float angDeg, int axis)
{
    if (qFuzzyIsNull(angDeg))
        return;
    const float a = qDegreesToRadians(angDeg);
    const float c = qCos(a), s = qSin(a);
    const float dx = x - ox, dy = y - oy, dz = z - oz;
    if (axis == 0)             // 绕 X
    {
        y = oy + dy * c - dz * s;
        z = oz + dy * s + dz * c;
    }
    else if (axis == 2)        // 绕 Z
    {
        x = ox + dx * c - dy * s;
        y = oy + dx * s + dy * c;
    }
    else                       // 绕 Y
    {
        x = ox + dx * c - dz * s;
        z = oz + dx * s + dz * c;
    }
}

// ShapeFace.dir -> 面索引（0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z）
int shapeFaceToIndex(const QString &dir)
{
    if (dir == QLatin1String("east")) return 0;
    if (dir == QLatin1String("west")) return 1;
    if (dir == QLatin1String("up"))   return 2;
    if (dir == QLatin1String("down")) return 3;
    if (dir == QLatin1String("south")) return 4;
    return 5; // north
}

// tintindex 面按方块种类取颜色（简化版生物群系着色：原版贴图是灰度的，
// 不乘色就会渲染成惨白的方块——树叶、草方块顶面、草、睡莲、水都属于这一类）
QColor tintColorFor(const QString &blockName)
{
    if (blockName.contains(QLatin1String("birch_leaves")))
        return QColor(0x80, 0xA7, 0x55);
    if (blockName.contains(QLatin1String("spruce_leaves")))
        return QColor(0x61, 0x99, 0x61);
    if (blockName.contains(QLatin1String("leaves")))
        return QColor(0x77, 0xAB, 0x2F);
    if (blockName.contains(QLatin1String("lily_pad")))
        return QColor(0x71, 0xC3, 0x5C);
    if (blockName.contains(QLatin1String("vine")))
        return QColor(0x48, 0xB5, 0x18);
    if (blockName.contains(QLatin1String("water")) || blockName.contains(QLatin1String("bubble")))
        return QColor(0x3F, 0x76, 0xE4);
    return QColor(0x91, 0xBD, 0x59);      // 草/蕨/苔藓等植被
}

// 液体种类：原版的水 / 岩浆没有方块模型，由专用渲染器按液面高度画，
// 贴图也另取名（water_still / lava_still），按普通方块「名字 → 贴图」是查不到的
enum FluidKind
{
    FluidNone = 0,
    FluidWater = 1,
    FluidLava = 2
};

FluidKind fluidKindOf(const QString &blockName)
{
    QString n = blockName;
    const int colon = n.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        n = n.mid(colon + 1);
    if (n == QLatin1String("water") || n == QLatin1String("flowing_water"))
        return FluidWater;
    if (n == QLatin1String("lava") || n == QLatin1String("flowing_lava"))
        return FluidLava;
    return FluidNone;
}

// 液面高度：level=0 是水源，原版可见液面在 14/16 处；level 1..7 是流动水，
// 每级再往下降 1/9 格；level>=8 表示正往下淌，按满高度处理，瀑布才不会一格一格断开
float fluidSurfaceHeight(const SchematicBlockState &state)
{
    bool ok = false;
    const int level = state.properties.value(QStringLiteral("level")).toInt(&ok);
    if (!ok || level <= 0 || level >= 8)
        return 0.875f;
    return float(8 - level) / 9.0f;
}

// ----------------------------------------------------------------- 红石线（红石粉）
//
// 红石线不是一块普通立方体：贴图是灰度的，颜色由信号强度决定；形状也不是固定模型，
// 而是按四周连接方向拼出「点 / 直线 / 拐角 / 爬坡」。直接按普通方块渲染时，纯色模式
// 会变成一整块红砖，材质模式又会因为 tintindex 被当成植物染成绿色。下面这几个小工具
// 负责把信号强度和连接关系翻译成实际要画的一层薄粉尘。

/** 去掉命名空间前缀、统一小写后的方块名 */
QString bareBlockName(const QString &blockName)
{
    QString n = blockName.trimmed();
    const int colon = n.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        n = n.mid(colon + 1);
    return n.toLower();
}

/** 是否是红石线（1.13+ 叫 redstone_wire，更老的映射会直接写 redstone） */
bool isRedstoneWireName(const QString &blockName)
{
    const QString n = bareBlockName(blockName);
    return n == QLatin1String("redstone_wire") || n == QLatin1String("redstone");
}

/**
 * @brief 原版 RedstoneWireBlock 的信号强度配色（0 → 暗红，15 → 亮橙红）
 *
 * 公式与游戏源码一致，投影里哪段线有电、哪段没电一眼就能看出来。
 */
QColor redstoneColorForPower(int power)
{
    const float p = float(qBound(0, power, 15)) / 15.0f;
    const float r = p * 0.6f + (p > 0.0f ? 0.4f : 0.3f);
    const float g = p * p * 0.7f - 0.5f;
    const float b = p * p * 0.6f - 0.7f;
    return QColor::fromRgbF(qBound(0.0f, r, 1.0f),
                            qBound(0.0f, g, 1.0f),
                            qBound(0.0f, b, 1.0f));
}

/** 红石线会连过去的方块：红石类元件 + 中继器 / 比较器 / 拉杆 / 按钮 / 压板等 */
bool isRedstoneConnector(const QString &blockName)
{
    const QString n = bareBlockName(blockName);
    if (n.isEmpty())
        return false;
    if (n == QLatin1String("redstone_wire") || n == QLatin1String("redstone")
        || (n.contains(QLatin1String("redstone")) && n.contains(QLatin1String("torch")))
        || n == QLatin1String("redstone_block"))
        return true;
    // 老格式里中继器 / 比较器带 powered_ 前缀，用后缀匹配把它们一起收进来
    if (n.endsWith(QLatin1String("repeater")) || n.endsWith(QLatin1String("comparator")))
        return true;
    if (n == QLatin1String("lever") || n == QLatin1String("observer")
        || n == QLatin1String("target") || n.contains(QLatin1String("daylight_detector"))
        || n == QLatin1String("tripwire_hook") || n == QLatin1String("detector_rail")
        || n == QLatin1String("lightning_rod"))
        return true;
    return n.endsWith(QLatin1String("_button"))
        || n.endsWith(QLatin1String("_pressure_plate"));
}

// 射线-轴对齐盒（区间法）；命中返回 true
bool slabHitF(const float ro[3], const float rd[3],
              const float bmin[3], const float bmax[3], float &tmin, float &tmax)
{
    tmin = -1e30f;
    tmax = 1e30f;
    for (int a = 0; a < 3; ++a)
    {
        const float o = ro[a], d = rd[a];
        if (qAbs(d) < 1e-8f)
        {
            if (o < bmin[a] || o > bmax[a])
                return false;
            continue;
        }
        const float inv = 1.0f / d;
        float t1 = (bmin[a] - o) * inv;
        float t2 = (bmax[a] - o) * inv;
        if (t1 > t2) { const float tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax)
            return false;
    }
    return true;
}

inline uchar toByte(float v)
{
    const float x = v <= 0.0f ? 0.0f : (v >= 1.0f ? 1.0f : v);
    return static_cast<uchar>(x * 255.0f + 0.5f);
}

} // namespace

VoxelViewWidget::VoxelViewWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_doc(nullptr)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // 4×MSAA：方块边缘不再锯齿（部分环境不支持时自动退回默认采样数）
    QSurfaceFormat fmt = format();
    if (fmt.samples() < 4)
    {
        fmt.setSamples(4);
        setFormat(fmt);
    }

    m_flyTimer.setInterval(16);
    m_flyTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_flyTimer, &QTimer::timeout, this, [this]() {
        applyFlyKeys(0.016f);
    });

    m_clock.start();
}

VoxelViewWidget::~VoxelViewWidget()
{
    destroyGL();
}

// ----------------------------------------------------------------- 数据注入

void VoxelViewWidget::setDocument(SchematicDocument *doc)
{
    m_doc = doc;
    m_highlightCells.clear();   // 换文档后旧格索引失效
    m_paletteCacheValid = false;
    m_paletteCache.clear();
    if (!m_eyeValid)
    {
        resetView();
        m_eyeValid = true;
    }
    rebuild();
}

void VoxelViewWidget::setTextureLoader(InstanceTextureLoader *loader)
{
    m_loader = loader;
    m_paletteCacheValid = false;
    m_atlasDirty = true;
    rebuild();   // 重算每方块贴图/UV（纯 CPU）
}

void VoxelViewWidget::setShapeLoader(BlockShapeLoader *loader)
{
    m_shapeLoader = loader;
    m_shapeCache.clear();
    m_paletteCacheValid = false;
    rebuild();   // 重算每方块真实形状（纯 CPU）
}

void VoxelViewWidget::setShowAir(bool show)
{
    if (m_showAir == show)
        return;
    m_showAir = show;
    rebuild();
}

void VoxelViewWidget::setLayerFilter(int layer)
{
    setLayerView(layer, m_layerMode);
}

void VoxelViewWidget::setLayerMode(LayerMode mode)
{
    setLayerView(m_layerFilter, mode);
}

void VoxelViewWidget::setLayerView(int layer, LayerMode mode)
{
    if (m_layerFilter == layer && m_layerMode == mode)
        return;
    m_layerFilter = layer;
    m_layerMode = mode;
    rebuild();
}

void VoxelViewWidget::setLayerAxis(LayerAxis axis)
{
    if (m_layerAxis == axis)
        return;
    m_layerAxis = axis;
    rebuild();
}

void VoxelViewWidget::setHover(int x, int y, int z)
{
    if (m_hoverX == x && m_hoverY == y && m_hoverZ == z)
        return;
    m_hoverX = x;
    m_hoverY = y;
    m_hoverZ = z;
    // 悬停只走叠加通道：不重建、不重传主几何
    rebuildHoverGeometry();
    update();
    emit hoverChanged(x, y, z);
}

void VoxelViewWidget::setSelectedCells(const QSet<int> &cells)
{
    if (m_selectedCells == cells)
        return;
    m_selectedCells = cells;
    rebuildSelectionGeometry();
    update();
}

void VoxelViewWidget::setHighlightCells(const QSet<int> &cells)
{
    if (m_highlightCells == cells)
        return;
    m_highlightCells = cells;
    rebuildHighlightGeometry();
    update();
}

void VoxelViewWidget::setLeftAction(LeftAction action)
{
    if (m_leftAction == action)
        return;
    m_leftAction = action;
    m_pendingEdit = EditNone;
    m_selectPending = false;
    if (action == LeftAction::Select)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
}

void VoxelViewWidget::rebuild()
{
    // 纯 CPU：重建方块几何 + 顶点数据，随后随 update() 在 paintGL 上传。
    // 绝不在控件显示前调用 GL（可安全于 loadProjection 阶段调用，避免闪退）。
    buildGeometry();
    buildEntityGeometry();
    rebuildSelectionGeometry();
    rebuildHoverGeometry();
    rebuildHighlightGeometry();
    update();
}

// ----------------------------------------------------------------- 相机

QVector3D VoxelViewWidget::lookForward() const
{
    const float cp = qCos(m_pitch);
    return QVector3D(qSin(m_yaw) * cp, qSin(m_pitch), qCos(m_yaw) * cp).normalized();
}

QVector3D VoxelViewWidget::eyePosition() const
{
    return m_eyePos;
}

QVector3D VoxelViewWidget::lookTarget() const
{
    return m_eyePos + lookForward() * qMax(1.0f, m_viewDist);
}

void VoxelViewWidget::resetView()
{
    m_yaw = 0.6f;
    m_pitch = -0.35f;

    QVector3D center(0.5f, 0.5f, 0.5f);
    float radius = 8.0f;
    if (m_doc && m_doc->isLoaded())
    {
        center = QVector3D(m_doc->sizeX() * 0.5f, m_doc->sizeY() * 0.5f, m_doc->sizeZ() * 0.5f);
        radius = 0.5f * qSqrt(float(m_doc->sizeX()) * m_doc->sizeX()
                              + float(m_doc->sizeY()) * m_doc->sizeY()
                              + float(m_doc->sizeZ()) * m_doc->sizeZ());
        radius = qMax(radius, 4.0f);
    }
    m_viewDist = qBound(4.0f, radius / qSin(qDegreesToRadians(22.5f)) * 0.95f, 6000.0f);
    m_eyePos = center - lookForward() * m_viewDist;
    m_eyeValid = true;
    update();
}

void VoxelViewWidget::focusOnCells(const QSet<int> &cells)
{
    if (!m_doc || !m_doc->isLoaded() || cells.isEmpty())
    {
        focusAll();
        return;
    }
    const int sx = m_doc->sizeX(), layer = m_doc->layerSize();
    int minX = INT_MAX, minY = INT_MAX, minZ = INT_MAX;
    int maxX = INT_MIN, maxY = INT_MIN, maxZ = INT_MIN;
    for (int idx : cells)
    {
        if (idx < 0 || idx >= m_doc->volume())
            continue;
        const int y = idx / layer;
        const int rem = idx % layer;
        const int z = rem / sx;
        const int x = rem % sx;
        minX = qMin(minX, x); maxX = qMax(maxX, x);
        minY = qMin(minY, y); maxY = qMax(maxY, y);
        minZ = qMin(minZ, z); maxZ = qMax(maxZ, z);
    }
    if (minX > maxX)
        return;
    const QVector3D center((minX + maxX + 1) * 0.5f, (minY + maxY + 1) * 0.5f,
                           (minZ + maxZ + 1) * 0.5f);
    const float radius = 0.5f * qSqrt(float(maxX - minX + 1) * (maxX - minX + 1)
                                      + float(maxY - minY + 1) * (maxY - minY + 1)
                                      + float(maxZ - minZ + 1) * (maxZ - minZ + 1));
    m_viewDist = qBound(2.0f, radius / qSin(qDegreesToRadians(22.5f)) * 1.05f, 6000.0f);
    m_eyePos = center - lookForward() * m_viewDist;
    m_eyeValid = true;
    update();
}

void VoxelViewWidget::focusAll()
{
    if (!m_doc || !m_doc->isLoaded())
        return;
    const float sx = float(m_doc->sizeX());
    const float sy = float(m_doc->sizeY());
    const float sz = float(m_doc->sizeZ());
    QVector3D center(sx * 0.5f, sy * 0.5f, sz * 0.5f);

    // 取景距离：把包围盒八个角按当前朝向投到画面的「横 / 竖」两轴上，逐个求
    // 「这个角刚好落进画面」需要的距离，取最大的那个。
    // 早先只拿 XZ 对角线当横向、拿高度当竖向，斜俯视一栋矮而宽的建筑时，
    // 屋顶在画面上占的高度远大于它的实际高度，四个角就被切掉了。
    const QVector3D forward = lookForward();
    QVector3D right = QVector3D::crossProduct(forward, QVector3D(0.0f, 1.0f, 0.0f));
    if (right.lengthSquared() < 1e-6f)   // 正上方俯视：forward 与世界上方平行
        right = QVector3D(1.0f, 0.0f, 0.0f);
    right.normalize();
    const QVector3D up =
        QVector3D::crossProduct(right, forward).normalized();

    const float aspect = float(qMax(1, width())) / float(qMax(1, height()));
    const float tanV = qTan(qDegreesToRadians(22.5f)); // 与 perspective(45) 对应
    const float tanH = qMax(0.05f, tanV * aspect);

    const float halfX = sx * 0.5f;
    const float halfY = qMax(sy, 1.0f) * 0.5f;
    const float halfZ = sz * 0.5f;
    float needDist = 3.0f;
    for (int i = 0; i < 8; ++i)
    {
        const QVector3D corner((i & 1) ? halfX : -halfX,
                               (i & 2) ? halfY : -halfY,
                               (i & 4) ? halfZ : -halfZ);
        const float depthOffset = QVector3D::dotProduct(corner, forward);
        // 横向 / 竖向都按「该角自身的深度」算，透视近大远小才不会把近处的角切掉
        const float byWidth = depthOffset + qAbs(QVector3D::dotProduct(corner, right)) / tanH;
        const float byHeight = depthOffset + qAbs(QVector3D::dotProduct(corner, up)) / tanV;
        needDist = qMax(needDist, qMax(byWidth, byHeight));
    }
    m_viewDist = qBound(3.0f, needDist * 1.08f, 6000.0f);
    m_eyePos = center - forward * m_viewDist;
    m_eyeValid = true;
    update();
}

void VoxelViewWidget::applyViewPreset(ViewPreset preset)
{
    // 只换观察方向：取景中心与距离保持不变，视点绕到该方向对应的一侧
    const QVector3D center = m_eyePos + lookForward() * m_viewDist;
    switch (preset)
    {
    case ViewPreset::Iso:    m_yaw = 0.6f;  m_pitch = -0.35f; break;
    case ViewPreset::Front:  m_yaw = 0.0f;  m_pitch = 0.0f;   break;
    case ViewPreset::Back:   m_yaw = float(M_PI); m_pitch = 0.0f; break;
    case ViewPreset::Left:   m_yaw = float(M_PI_2); m_pitch = 0.0f; break;
    case ViewPreset::Right:  m_yaw = float(-M_PI_2); m_pitch = 0.0f; break;
    case ViewPreset::Top:    m_yaw = 0.0f;  m_pitch = float(-M_PI_2); break;
    case ViewPreset::Bottom: m_yaw = 0.0f;  m_pitch = float(M_PI_2);  break;
    }
    m_eyePos = center - lookForward() * m_viewDist;
    m_eyeValid = true;
    update();
}

void VoxelViewWidget::orbitYaw(float degrees)
{
    if (!m_eyeValid)
    {
        // 还没取过景：只改朝向，下一步 resetView 再按尺寸摆好视点
        m_yaw += qDegreesToRadians(degrees);
        update();
        return;
    }
    const QVector3D center = m_eyePos + lookForward() * m_viewDist;
    m_yaw += qDegreesToRadians(degrees);
    m_eyePos = center - lookForward() * m_viewDist;
    update();
}

void VoxelViewWidget::lookBy(float dYaw, float dPitch)
{
    m_yaw += dYaw;
    m_pitch = qBound(float(-M_PI_2), m_pitch - dPitch, float(M_PI_2));
    m_eyeValid = true;
    update();
}

void VoxelViewWidget::moveEye(const QVector3D &delta)
{
    m_eyePos += delta;
    m_eyeValid = true;
    update();
}

void VoxelViewWidget::setLookSensitivity(float sensitivity)
{
    m_lookSensitivity = qBound(0.0f, sensitivity, 1.0f);
}

float VoxelViewWidget::mouseLookRadiansPerPixel() const
{
    // 《我的世界》原版的灵敏度曲线：滑块 0..1 先映射到 0.2..0.8，再取三次方。
    // 默认 0.5 时约 0.15°/像素，和游戏默认手感接近。
    const float f = 0.2f + m_lookSensitivity * 0.6f;
    return qDegreesToRadians(f * f * f * 1.2f);
}

void VoxelViewWidget::endLookCapture(bool restoreCursorToPress)
{
    if (restoreCursorToPress && m_lookActive && isVisible())
    {
        // 指针从视口中心还原到按下处，避免用户松手后找不到鼠标
        QCursor::setPos(mapToGlobal(m_pressPos));
    }
    m_lookActive = false;
    m_dragging = false;
    unsetCursor();
    if (m_leftAction == LeftAction::Select)
        setCursor(Qt::PointingHandCursor);
}

bool VoxelViewWidget::isFlyKey(int qtKey) const
{
    switch (qtKey)
    {
    case Qt::Key_W:
    case Qt::Key_A:
    case Qt::Key_S:
    case Qt::Key_D:
    case Qt::Key_Q:
    case Qt::Key_E:
    case Qt::Key_Space:
    case Qt::Key_Shift:
    case Qt::Key_Control:
        return true;
    default:
        return false;
    }
}

void VoxelViewWidget::applyFlyKeys(float dt)
{
    if (m_heldKeys.isEmpty())
    {
        m_flyTimer.stop();
        return;
    }

    // 《我的世界》式移动：W/S 沿视线方向飞（抬头就往上飞），A/D 水平横移，
    // 空格上升、Shift 下降；双击 W 或按住 Ctrl 疾跑（速度 ×2）。
    // 基础速度随取景距离放大，远景下拖动不会慢得难受。
    const float baseSpeed = 10.92f * qBound(1.0f, m_viewDist / 40.0f, 6.0f);
    const bool sprint = m_sprintLatch || m_heldKeys.contains(Qt::Key_Control);
    const float speed = baseSpeed * (sprint ? 2.0f : 1.0f) * dt;

    const QVector3D fwd = lookForward();
    QVector3D flat = QVector3D(fwd.x(), 0.0f, fwd.z());
    if (flat.lengthSquared() < 1e-8f)
        flat = QVector3D(qSin(m_yaw), 0.0f, qCos(m_yaw));
    flat.normalize();
    const QVector3D right = QVector3D::crossProduct(flat, QVector3D(0, 1, 0)).normalized();

    QVector3D move(0, 0, 0);
    if (m_heldKeys.contains(Qt::Key_W))
        move += fwd * speed;
    if (m_heldKeys.contains(Qt::Key_S))
        move -= fwd * speed;
    if (m_heldKeys.contains(Qt::Key_D))
        move += right * speed;
    if (m_heldKeys.contains(Qt::Key_A))
        move -= right * speed;
    if (m_heldKeys.contains(Qt::Key_Space) || m_heldKeys.contains(Qt::Key_E))
        move += QVector3D(0, speed, 0);
    if (m_heldKeys.contains(Qt::Key_Shift) || m_heldKeys.contains(Qt::Key_Q))
        move -= QVector3D(0, speed, 0);

    if (!move.isNull())
        moveEye(move);
}

// ----------------------------------------------------------------- 颜色

QColor VoxelViewWidget::colorForName(const QString &blockName)
{
    QString n = blockName;
    int slash = n.indexOf(QLatin1Char(':'));
    if (slash >= 0)
        n = n.mid(slash + 1);
    if (n.isEmpty())
        return QColor(120, 120, 120);

    // 染色系方块（羊毛 / 地毯 / 旗帜 / 染色玻璃 / 陶瓦 / 床 / 蜡烛…）按名称里的
    // 颜色词着色，否则白旗帜、红羊毛会被统一染成同一种颜色
    static const struct { const char *token; int r, g, b; } kDyeColors[] = {
        { "light_gray", 142, 142, 135 }, { "light_blue", 58, 175, 217 },
        { "white", 233, 236, 236 },      { "orange", 240, 118, 19 },
        { "magenta", 189, 68, 179 },     { "yellow", 248, 198, 40 },
        { "lime", 112, 185, 26 },        { "pink", 238, 141, 172 },
        { "gray", 62, 68, 71 },          { "cyan", 21, 119, 136 },
        { "purple", 121, 42, 172 },      { "blue", 53, 57, 157 },
        { "brown", 114, 71, 40 },        { "green", 84, 109, 27 },
        { "red", 160, 39, 34 },          { "black", 26, 26, 26 },
    };
    // 陶瓦是「灰扑扑的染色黏土」，比羊毛混凝土暗得多，单独一套色；
    // 带釉陶瓦仍是鲜艳色，继续用上面那套
    static const struct { const char *token; int r, g, b; } kTerracottaColors[] = {
        { "light_gray", 135, 106, 97 },  { "light_blue", 113, 108, 137 },
        { "white", 209, 178, 161 },      { "orange", 161, 83, 37 },
        { "magenta", 149, 88, 108 },     { "yellow", 186, 133, 35 },
        { "lime", 103, 117, 52 },        { "pink", 160, 77, 78 },
        { "gray", 57, 42, 35 },          { "cyan", 86, 91, 91 },
        { "purple", 118, 70, 86 },       { "blue", 74, 59, 91 },
        { "brown", 77, 51, 35 },         { "green", 76, 83, 42 },
        { "red", 143, 61, 46 },          { "black", 37, 22, 16 },
    };
    static const char *const kDyeFamilies[] = {
        "wool", "carpet", "banner", "concrete", "terracotta", "stained_glass",
        "bed", "candle", "shulker_box", "glazed_terracotta", "concrete_powder"
    };
    for (const char *family : kDyeFamilies)
    {
        if (!n.contains(QLatin1String(family)))
            continue;
        const bool plainTerracotta = n.contains(QLatin1String("terracotta"))
            && !n.contains(QLatin1String("glazed"));
        for (int ci = 0; ci < 16; ++ci)
        {
            const char *token = plainTerracotta ? kTerracottaColors[ci].token : kDyeColors[ci].token;
            if (!n.contains(QLatin1String(token)))
                continue;
            return plainTerracotta ? QColor(kTerracottaColors[ci].r, kTerracottaColors[ci].g,
                                            kTerracottaColors[ci].b)
                                   : QColor(kDyeColors[ci].r, kDyeColors[ci].g, kDyeColors[ci].b);
        }
        break;
    }

    // 楼梯 / 台阶 / 墙 / 栅栏 / 门 等派生方块直接沿用主体材质的颜色，
    // 否则「深板岩楼梯」「下界砖台阶」这类会被统一染成灰色，和游戏差很远
    static const char *const kShapeSuffixes[] = {
        "_hanging_sign", "_pressure_plate", "_fence_gate", "_trapdoor", "_stairs", "_button",
        "_carpet", "_fence", "_wall", "_door", "_pane", "_bars", "_sign", "_slab"
    };
    for (const char *suffix : kShapeSuffixes)
    {
        const QLatin1String sfx(suffix);
        if (n.endsWith(sfx) && n.size() > sfx.size())
        {
            n.chop(sfx.size());
            break;
        }
    }

    if (n == QLatin1String("air") || n == QLatin1String("cave_air")
            || n == QLatin1String("void_air") || n == QLatin1String("structure_void"))
        return QColor(0, 0, 0, 0);
    // 常见方块的平均贴图色（由细到粗排列，命中第一条即返回）。
    // 顺序是关键：含「砖」的要排在通用石头前，含「红石」的要排在通用石头前。
    struct Rule
    {
        const char *token;
        int r, g, b, a;
    };
    static const Rule kRules[] = {
        // 液体与半透明
        { "nether_portal", 120, 60, 190, 200 },
        { "lava", 240, 120, 38, 255 },
        { "water", 63, 118, 228, 235 },
        { "glass", 206, 233, 247, 225 },
        // 冰雪与晶体
        { "blue_ice", 120, 165, 240, 235 },
        { "packed_ice", 140, 180, 250, 235 },
        { "ice", 155, 195, 245, 220 },
        { "snow", 246, 248, 252, 255 },
        { "amethyst", 133, 97, 191, 220 },
        { "bone", 228, 224, 205, 255 },
        { "calcite", 223, 224, 220, 255 },
        { "dripstone", 152, 120, 96, 255 },
        { "tuff", 108, 109, 102, 255 },
        // 末地与深渊
        { "end_stone_brick", 218, 215, 158, 255 },
        { "end_stone", 219, 222, 158, 255 },
        { "end_rod", 240, 240, 225, 255 },
        { "purpur", 169, 125, 169, 255 },
        { "chorus", 110, 80, 105, 255 },
        { "sculk", 12, 32, 34, 255 },
        { "crying_obsidian", 40, 24, 62, 255 },
        { "obsidian", 22, 20, 32, 255 },
        { "bedrock", 85, 85, 85, 255 },
        { "deepslate_tiles", 58, 58, 62, 255 },
        { "deepslate_brick", 66, 66, 70, 255 },
        { "cobbled_deepslate", 77, 77, 80, 255 },
        { "deepslate", 74, 74, 79, 255 },
        // 下界
        { "netherrack", 97, 38, 38, 255 },
        { "nether_brick", 44, 21, 26, 255 },
        { "nether_wart", 118, 20, 20, 255 },
        { "magma", 142, 62, 31, 255 },
        { "soul_sand", 82, 62, 50, 255 },
        { "soul_soil", 78, 58, 46, 255 },
        { "soul_torch", 90, 200, 210, 255 },
        { "soul_lantern", 96, 205, 214, 255 },
        { "soul_campfire", 96, 190, 200, 255 },
        { "soul_fire", 80, 190, 205, 255 },
        { "gilded", 246, 208, 61, 255 },
        { "blackstone", 60, 50, 58, 255 },
        { "basalt", 82, 80, 86, 255 },
        // 石质
        { "prismarine", 100, 150, 140, 255 },
        { "mud_brick", 137, 103, 79, 255 },
        { "packed_mud", 142, 106, 79, 255 },
        { "stone_brick", 122, 121, 122, 255 },
        { "red_sandstone", 181, 97, 31, 255 },
        { "sandstone", 216, 203, 155, 255 },
        { "red_nether_brick", 88, 32, 32, 255 },
        { "redstone", 175, 24, 5, 255 },
        { "cobblestone", 127, 127, 127, 255 },
        { "granite", 149, 103, 85, 255 },
        { "diorite", 188, 188, 191, 255 },
        { "andesite", 136, 136, 137, 255 },
        { "smooth_stone", 158, 158, 158, 255 },
        { "brick", 150, 97, 83, 255 },
        { "stone", 125, 125, 125, 255 },
        { "quartz", 236, 229, 222, 255 },
        { "terracotta", 152, 94, 67, 255 },
        { "mud", 60, 57, 60, 255 },
        // 沙土
        { "red_sand", 188, 102, 33, 255 },
        { "suspicious_sand", 220, 208, 165, 255 },
        { "sand", 219, 207, 163, 255 },
        { "suspicious_gravel", 140, 136, 136, 255 },
        { "gravel", 132, 127, 127, 255 },
        { "clay", 160, 167, 178, 255 },
        { "coarse_dirt", 122, 90, 62, 255 },
        { "rooted_dirt", 130, 100, 78, 255 },
        { "podzol", 110, 80, 45, 255 },
        { "mycelium", 140, 120, 130, 255 },
        { "farmland", 110, 80, 55, 255 },
        { "dirt", 134, 96, 67, 255 },
        // 植被
        { "leaves", 70, 130, 50, 255 },
        { "sapling", 60, 110, 40, 255 },
        { "mangrove_roots", 90, 65, 50, 255 },
        { "azalea", 110, 150, 80, 255 },
        { "potted_", 148, 98, 68, 255 },
        { "flower_pot", 148, 98, 68, 255 },
        { "flower", 170, 60, 80, 255 },
        { "fern", 90, 140, 55, 255 },
        { "short_grass", 95, 159, 53, 255 },
        { "grass", 95, 159, 53, 255 },
        { "kelp", 70, 130, 60, 255 },
        { "seagrass", 60, 130, 70, 255 },
        { "vine", 70, 120, 45, 255 },
        { "moss_block", 60, 90, 40, 255 },
        { "moss_carpet", 60, 90, 40, 255 },
        { "lily_pad", 60, 120, 50, 255 },
        { "cactus", 70, 120, 50, 255 },
        { "dead_bush", 130, 100, 55, 255 },
        { "pumpkin", 200, 130, 50, 255 },
        { "melon", 110, 160, 60, 255 },
        { "hay", 166, 132, 26, 255 },
        { "wheat", 160, 170, 70, 255 },
        // 金属与矿物
        { "oxidized_copper", 82, 162, 132, 255 },
        { "weathered_copper", 108, 153, 110, 255 },
        { "exposed_copper", 161, 125, 103, 255 },
        { "copper", 192, 107, 79, 255 },
        { "gold", 246, 208, 61, 255 },
        { "iron", 220, 220, 220, 255 },
        { "diamond", 98, 219, 214, 255 },
        { "emerald", 41, 201, 100, 255 },
        { "lapis", 38, 97, 156, 255 },
        { "coal", 40, 40, 45, 255 },
        // 木质
        { "pale_oak", 200, 196, 186, 255 },
        { "dark_oak", 66, 43, 20, 255 },
        { "spruce", 114, 84, 48, 255 },
        { "birch", 193, 179, 124, 255 },
        { "jungle", 160, 115, 80, 255 },
        { "acacia", 168, 90, 50, 255 },
        { "mangrove", 117, 54, 48, 255 },
        { "cherry", 225, 181, 172, 255 },
        { "crimson", 127, 58, 86, 255 },
        { "warped", 43, 120, 110, 255 },
        { "bamboo", 192, 173, 80, 255 },
        { "oak", 162, 130, 78, 255 },
        { "planks", 150, 110, 60, 255 },
        { "log", 120, 92, 52, 255 },
        { "hyphae", 127, 58, 86, 255 },
        { "stem", 120, 92, 52, 255 },
        { "wood", 150, 110, 60, 255 },
        { "scaffolding", 170, 140, 80, 255 },
        { "ladder", 150, 120, 70, 255 },
        // 装饰与家具
        { "bookshelf", 150, 110, 70, 255 },
        { "chest", 120, 90, 50, 255 },
        { "barrel", 130, 100, 60, 255 },
        { "furnace", 110, 110, 110, 255 },
        { "crafting_table", 150, 110, 70, 255 },
        { "loom", 150, 120, 90, 255 },
        { "cartography", 150, 120, 90, 255 },
        { "smithing", 90, 90, 95, 255 },
        { "stonecutter", 160, 160, 160, 255 },
        { "cauldron", 70, 70, 78, 255 },
        { "anvil", 70, 70, 78, 255 },
        { "brewing", 150, 120, 90, 255 },
        { "enchanting", 60, 45, 80, 255 },
        { "bedrock", 85, 85, 85, 255 },
        { "bed", 200, 60, 60, 255 },
        { "wool", 225, 225, 225, 255 },
        { "carpet", 225, 225, 225, 255 },
        { "candle", 245, 225, 180, 255 },
        { "sea_lantern", 200, 230, 220, 255 },
        { "lantern", 255, 200, 110, 255 },
        { "glowstone", 255, 220, 150, 255 },
        { "shroomlight", 255, 170, 80, 255 },
        { "froglight", 230, 220, 180, 255 },
        { "redstone_lamp", 200, 170, 90, 255 },
        { "campfire", 120, 85, 55, 255 },
        { "torch", 255, 200, 90, 255 },
        { "fire", 255, 168, 62, 255 },
        { "chain", 110, 110, 120, 255 },
        { "rail", 150, 150, 155, 255 },
        { "cobweb", 235, 235, 235, 200 },
        { "tnt", 200, 50, 40, 255 },
        { "sponge", 200, 200, 90, 255 },
        { "conduit", 140, 210, 200, 255 },
        { "beacon", 120, 220, 230, 200 },
        { "bell", 230, 190, 70, 255 },
        { "banner", 200, 60, 60, 255 },
        { "pot", 170, 110, 90, 255 },
        { "jukebox", 110, 80, 55, 255 },
        { "note_block", 110, 80, 55, 255 },
        { "target", 230, 210, 180, 255 },
        { "honey", 240, 170, 50, 255 },
        { "slime", 120, 190, 110, 200 },
        { "trial_spawner", 70, 60, 90, 255 },
        { "vault", 90, 80, 70, 255 },
        { "candle", 245, 225, 180, 255 },
    };
    for (const Rule &rule : kRules)
    {
        if (n.contains(QLatin1String(rule.token)))
            return QColor(rule.r, rule.g, rule.b, rule.a);
    }

    // 未收录的方块：按名字散列出一个稳定色（不使用 qHash，保证跨进程一致），
    // 避免整片结构被染成同一种颜色
    quint32 hash = 2166136261u;
    for (const QChar ch : n)
        hash = (hash ^ quint32(ch.unicode())) * 16777619u;
    const int hue = int(hash % 360u);
    const int sat = 30 + int((hash >> 8) % 55u);
    const int val = 135 + int((hash >> 16) % 60u);
    return QColor::fromHsv(hue, sat, val);
}

QColor VoxelViewWidget::blockColorOf(const QString &blockName)
{
    return colorForName(blockName);
}

QColor VoxelViewWidget::blockColor(const SchematicBlockState &state) const
{
    return colorForName(state.name);
}

// ----------------------------------------------------------------- 调色板缓存

const BlockShapes *VoxelViewWidget::shapesFor(const QString &stateName) const
{
    if (!m_shapeLoader || !m_shapeLoader->isValid())
        return nullptr;
    auto it = m_shapeCache.constFind(stateName);
    if (it != m_shapeCache.constEnd())
        return &it.value();
    if (m_shapeCache.size() > 4096)
        m_shapeCache.clear();
    BlockShapes shapes;
    if (!m_shapeLoader->shapeFor(stateName, shapes) || shapes.empty())
        return nullptr;
    m_shapeCache.insert(stateName, shapes);
    return &m_shapeCache.find(stateName).value();
}

void VoxelViewWidget::ensurePaletteCache()
{
    if (!m_doc || !m_doc->isLoaded())
    {
        m_paletteCache.clear();
        m_paletteCacheValid = true;
        return;
    }

    const int ps = m_doc->paletteSize();
    // 调色板只会追加（setBlock 追加新状态），已解析过的条目可长期复用；
    // 但换材质 / 换形状加载器时必须把缓存整体作废重算——否则旧条目还带着
    // 「用贴图」的标记，纯色块模式下会拿着已经卸掉的图集（1×1 白图）把整份模型
    // 渲染成白色，切到纯色块反而比材质模式还白。
    if (m_paletteCacheValid && m_paletteCache.size() == ps)
        return;

    const bool texAvail = m_loader && m_loader->isValid();
    const int from = m_paletteCacheValid ? qMin(m_paletteCache.size(), ps) : 0;
    m_paletteCache.resize(ps);

    for (int p = from; p < ps; ++p)
    {
        const SchematicBlockState &st = m_doc->paletteEntry(p);
        PaletteRender pr;
        pr.isAir = st.isAir();
        pr.color = colorForName(st.name);
        // 红石线：颜色只看信号强度（贴图本身是灰度的），形状和连接单独处理
        const bool redstoneWire = !pr.isAir && isRedstoneWireName(st.name);
        if (redstoneWire)
        {
            pr.redstoneWire = true;
            pr.redstonePower = qBound(0,
                st.properties.value(QStringLiteral("power")).toInt(), 15);
            pr.color = redstoneColorForPower(pr.redstonePower);
        }

        if (pr.isAir)
        {
            pr.opaque = false;
            pr.color.setAlpha(51);          // 空气轮廓：淡淡一层
            RenderBox box;                  // 默认整格
            box.fullCube = true;
            for (int f = 0; f < 6; ++f)
                box.face[f].present = true;  // 空气轮廓六个面都要画
            pr.boxes.append(box);
            pr.fullCube = true;
            pr.opaque = false;
            m_paletteCache[p] = pr;
            continue;
        }

        // 真实形状盒（含属性状态字符串）；无形状时回退为整格立方体。
        // 红石线也先尝试原版模型（保留贴图细节），没有模型时再自己拼粉尘。
        // 注意：fromModel 只表示「调色板里真有这个方块的模型」，补出来的整格保底盒
        // 不算模型——水 / 岩浆（专用渲染器画）和红石线都靠这一点区分「有模型 / 没模型」。
        QVector<ShapeBox> shapes;
        bool fromModel = false;
        {
            const BlockShapes *bs = shapesFor(st.toString());
            if (bs && !bs->boxes.isEmpty())
            {
                shapes = bs->boxes;
                fromModel = true;
            }
            if (shapes.isEmpty() && !redstoneWire)
                shapes.append(ShapeBox{});      // 默认全格
        }
        // 水 / 岩浆没有模型（原版用专用渲染器画）：这里按满格建盒，
        // 真正的液面高度留到生成几何时逐格决定——一整片水要按「上面还连着水」拉满整格，
        // 只有最上面那层才收口到 14/16，否则每层之间会露出一条缝，层层叠起来像一摞蓝玻璃箱。
        const FluidKind fluid = fromModel ? FluidNone : fluidKindOf(st.name);
        if (fluid != FluidNone)
        {
            pr.fluid = true;
            pr.fluidLava = (fluid == FluidLava);
            pr.fluidTop = fluidSurfaceHeight(st);
        }

        int fallbackTile = -1;
        if (texAvail)
        {
            if (fluid != FluidNone)
            {
                // 液体贴图叫 water_still / lava_still，直接按方块名查 water / lava 查不到，
                // 整片水就会退回纯色，看着像一块塑料蓝板
                const QString texKey = (fluid == FluidLava)
                    ? QStringLiteral("block/lava_still")
                    : QStringLiteral("block/water_still");
                if (!m_loader->tileForKey(texKey, fallbackTile))
                    fallbackTile = -1;
            }
            else
            {
                m_loader->textureFor(st.name, fallbackTile);
            }
        }

        bool allFull = true;
        for (const ShapeBox &sb : shapes)
        {
            RenderBox rb;
            for (int a = 0; a < 3; ++a)
            {
                rb.from[a] = sb.from[a];
                rb.to[a] = sb.to[a];
                rb.rotOrigin[a] = sb.rotOrigin[a];
            }
            rb.rotated = sb.rotated;
            rb.rotAxis = sb.rotAxis;
            rb.rotAngle = sb.rotAngle;
            rb.yaw = sb.yaw;
            rb.fullCube = !sb.rotated && sb.yaw == 0
                && sb.from[0] <= 1e-3f && sb.from[1] <= 1e-3f && sb.from[2] <= 1e-3f
                && sb.to[0] >= 1.0f - 1e-3f && sb.to[1] >= 1.0f - 1e-3f
                && sb.to[2] >= 1.0f - 1e-3f;
            if (!rb.fullCube)
                allFull = false;

            // 每面贴图/UV（优先该面模型纹理，其次方块基础贴图，最后颜色回退）
            // 模型只渲染它显式列出的面（与游戏一致）：灯笼这种「顶盖只写了 5 个面、
            // 顶部十字贴片只写了 2 个面」的模型，补出来的面不但白占顶点，
            // 还会和真正相邻的面重叠在一起（顶盖底面 vs 灯身顶面）而闪面。
            const bool modelHasFaces = !sb.faces.isEmpty();
            for (int f = 0; f < 6; ++f)
            {
                QString texKey;
                const ShapeFace *sface = nullptr;
                for (const ShapeFace &sf : sb.faces)
                {
                    if (shapeFaceToIndex(sf.dir) == f)
                    {
                        sface = &sf;
                        break;
                    }
                }
                if (!sface && modelHasFaces)
                    continue;
                RenderFace &rf = rb.face[f];
                rf.present = true;   // 模型没写 faces 的元素才走「整格六面」保底
                if (sface)
                {
                    texKey = sface->texture;
                    rf.cull = sface->canCull;         // 只有模型声明 cullface 的面才会被邻居剔除
                    rf.shadeOff = !sface->shaded;     // 火把/植物/梯子：不吃方向光
                    rf.noAo = !sface->aoOn;           // 十字模型关掉角落阴影
                    if (sface->tintIndex >= 0)        // 草/树叶/水按种类着色
                    {
                        // 红石线的贴图也是灰度的，但颜色由信号强度决定；不单独乘色的话
                        // 会落到植被的默认绿色，预览里就成了「绿线」
                        const QColor tc = redstoneWire
                            ? redstoneColorForPower(pr.redstonePower)
                            : tintColorFor(st.name);
                        rf.tint[0] = uchar(tc.red());
                        rf.tint[1] = uchar(tc.green());
                        rf.tint[2] = uchar(tc.blue());
                    }
                }
                // 液体贴图乘色：水的贴图是灰度的，不乘群系色就是一片惨白；
                // 岩浆贴图本身有色，乘白即可（原版岩浆的橙黄是画在贴图里的）
                if (fluid != FluidNone)
                {
                    if (fluid == FluidLava)
                    {
                        rf.tint[0] = rf.tint[1] = rf.tint[2] = 255;
                    }
                    else
                    {
                        rf.tint[0] = 63;
                        rf.tint[1] = 118;
                        rf.tint[2] = 228;
                    }
                }
                int tile = -1;
                bool ok = false;
                if (texAvail)
                {
                    if (sface && !texKey.isEmpty())
                        ok = m_loader->tileForKey(texKey, tile);
                    if (!ok && fallbackTile >= 0)
                    {
                        tile = fallbackTile;
                        ok = true;
                    }
                }
                if (!ok)
                    continue;
                float tu0, tv0, tu1, tv1;
                m_loader->tileUv(tile, tu0, tv0, tu1, tv1);
                // 面内子区域（模型 uv 为 0..16 像素；缺省时整张贴图）
                float u0s = 0.0f, v0s = 0.0f, u1s = 1.0f, v1s = 1.0f;
                if (sface && sface->hasUv)
                {
                    u0s = sface->uv[0] / 16.0f;
                    u1s = sface->uv[2] / 16.0f;
                    v0s = qMin(sface->uv[1], sface->uv[3]) / 16.0f;
                    v1s = qMax(sface->uv[1], sface->uv[3]) / 16.0f;
                }
                rb.face[f].useTex = true;
                rb.face[f].uv[0] = tu0 + u0s * (tu1 - tu0);
                rb.face[f].uv[1] = tv0 + v0s * (tv1 - tv0);
                rb.face[f].uv[2] = tu0 + u1s * (tu1 - tu0);
                rb.face[f].uv[3] = tv0 + v1s * (tv1 - tv0);
            }
            pr.boxes.append(rb);
        }

        pr.fullCube = allFull && pr.boxes.size() == 1;
        pr.opaque = !pr.isAir && pr.color.alpha() >= 250;
        if (fluid != FluidNone)
        {
            // 液体只填 14/16（流动时更低），不能当整格方块：否则邻居会以为被整块挡住，
            // 把紧挨液面的那圈面剔除，液面那一小截就会透出背景
            pr.fullCube = false;
            if (fluid == FluidWater)
            {
                // 水要能透出水下的结构：按半透明画（原版的水也是能看穿的）
                pr.color.setAlpha(150);
                pr.opaque = false;
            }
        }
        // 实心占比：台阶/楼梯/栅栏这类「不完整方块」虽然挡住视线，但只占一部分体积，
        // 用它给邻居打柔和的接触阴影（整块=1，台阶=0.5，十字植物/铁轨=0 不留阴影）
        {
            float solid = 0.0f;
            for (const RenderBox &rb : pr.boxes)
            {
                if (rb.rotated || rb.yaw != 0)
                    continue;   // 旋转/斜放的形状按平面处理，不做遮挡体
                const float dx = rb.to[0] - rb.from[0];
                const float dy = rb.to[1] - rb.from[1];
                const float dz = rb.to[2] - rb.from[2];
                if (dx > 0.0f && dy > 0.0f && dz > 0.0f)
                    solid += dx * dy * dz;
            }
            pr.solidity = qMin(1.0f, solid);
        }
        // 液体不留接触阴影：水面 / 岩浆面边上压一圈阴影会显得脏
        if (fluid != FluidNone)
            pr.solidity = 0.0f;
        if (redstoneWire)
        {
            // 有原版模型时沿用模型（贴图细节保留）；没有模型（纯色块模式、缺材质、
            // 或状态里只写了名字没有连接属性）时按连接关系自己拼一层粉尘
            pr.redstoneProcedural = !fromModel;
            if (pr.redstoneProcedural)
            {
                // 占位盒只用于拾取 / 悬停描边（原版红石线的选取框就是 1/16 高），
                // 真正画出来的粉尘由 buildGeometry 按邻居逐格生成
                RenderBox ph;
                ph.from[1] = 0.0f;
                ph.to[1] = 0.0625f;
                ph.fullCube = false;
                pr.boxes.append(ph);
            }
            pr.fullCube = false;      // 不能挡住邻居的面，也不能当整格 AO 遮挡体
            pr.solidity = 0.0f;
            pr.opaque = true;         // 粉尘本身不透明，放到第一遍画
            pr.tinted = true;         // 颜色按信号强度统一，不叠加逐格亮度扰动
        }
        // 玻璃/树叶/冰/水这类「能透出来」的方块不做遮挡体：
        // 否则邻居的面会被剔除，透过它们就会看到空洞
        const QString &bn = st.name;
        if (bn.contains(QLatin1String("glass")) || bn.contains(QLatin1String("leaves"))
            || bn.contains(QLatin1String("ice")) || bn.contains(QLatin1String("water")))
            pr.opaque = false;
        // 只要有任意一面用上了真实材质，就不做逐格亮度扰动，避免材质+扰动显得脏
        for (const RenderBox &rb : pr.boxes)
            for (int f = 0; f < 6; ++f)
                if (rb.face[f].useTex)
                {
                    pr.tinted = true;
                    break;
                }
        m_paletteCache[p] = pr;
    }
    m_paletteCacheValid = true;
}

void VoxelViewWidget::buildOccupancy()
{
    m_occupancy.clear();
    if (!m_doc || !m_doc->isLoaded())
        return;

    const int vol = m_doc->volume();
    const int layer = m_doc->layerSize();
    if (vol <= 0 || layer <= 0)
        return;

    try
    {
        m_occupancy.resize(vol);
    }
    catch (const std::bad_alloc &)
    {
        return;
    }
    // QByteArray::resize() 新扩出来的字节是未初始化的，必须先清零：
    // 后面只给「可见且有材质缓存」的格子写位，其余格子（空气、隐藏层、未知方块）
    // 全依赖这里的 0 表示「不参与绘制、不可拾取」。少了这一步，那些格子会带着
    // 随机字节当上「可见非空气」，拾取就会命中空气格，选中/悬停看起来就是没反应。
    std::memset(m_occupancy.data(), 0, size_t(vol));
    char *occ = m_occupancy.data();

    const LayerMode mode = m_layerMode;
    const int filter = m_layerFilter;
    const LayerAxis axis = m_layerAxis;
    const int sx = m_doc->sizeX();
    const bool showAir = m_showAir;
    const bool allowAll = (mode == LayerMode::All) || (filter < 0);

    for (int i = 0; i < vol; ++i)
    {
        // 剖切轴决定「第几层」取哪个坐标：Y 是水平层，X / Z 是竖向剖开
        const int y = i / layer;
        int axisCoord = y;
        if (axis != LayerAxis::Y)
        {
            const int rem = i - y * layer;
            axisCoord = (axis == LayerAxis::X) ? (rem % sx) : (rem / sx);
        }
        bool visible = true;
        if (!allowAll)
            visible = (mode == LayerMode::Single) ? (axisCoord == filter)
                                                  : (axisCoord <= filter);
        if (!visible)
            continue;

        const int p = m_doc->paletteIndexAt(i);
        if (p < 0 || p >= m_paletteCache.size())
            continue;
        const PaletteRender &pr = m_paletteCache.at(p);
        if (pr.isAir)
        {
            if (showAir)
                occ[i] = 2;              // bit1：可见空气（可点选，但不遮挡）
            continue;
        }
        occ[i] = 1;                      // bit0：可见非空气
        if (pr.opaque && pr.fullCube)
            occ[i] |= 4;                 // bit2：不透明整方块（可剔除邻居、可做 AO）
        else if (pr.opaque && pr.solidity >= 0.18f)
            occ[i] |= 8;                 // bit3：占了一部分体积的实心方块（台阶/楼梯/墙）
    }
}

// ----------------------------------------------------------------- 几何构建

void VoxelViewWidget::buildGeometry()
{
    QElapsedTimer timer;
    timer.start();

    m_quadCount = 0;
    m_vertexCount = 0;
    m_opaqueVertexCount = 0;
    m_uploadDirty = true;
    ensurePaletteCache();

    if (!m_doc || !m_doc->isLoaded() || m_paletteCache.isEmpty())
    {
        m_vertexData.clear();
        m_occupancy.clear();
        update();
        emit geometryRebuilt(0, int(timer.elapsed()));
        return;
    }

    buildOccupancy();

    const int sx = m_doc->sizeX();
    const int sy = m_doc->sizeY();
    const int sz = m_doc->sizeZ();
    const int layer = m_doc->layerSize();
    if (sx <= 0 || sy <= 0 || sz <= 0)
    {
        m_vertexData.clear();
        update();
        emit geometryRebuilt(0, int(timer.elapsed()));
        return;
    }

    const char *occ = m_occupancy.isEmpty() ? nullptr : m_occupancy.constData();
    const int vol = m_doc->volume();

    // 顶点缓冲：按上一轮面数（或体积）预留，边写边扩容，避免「先 QVector 再拷贝」
    int capQuads = m_quadCount > 0 ? m_quadCount + m_quadCount / 2
                                   : int(qBound<qint64>(qint64(64), qint64(vol) / 2, qint64(2000000)));
    if (capQuads < 64)
        capQuads = 64;
    qint64 capVerts = qint64(capQuads) * 6;
    try
    {
        m_vertexData.resize(static_cast<int>(capVerts * qint64(sizeof(Vtx))));
    }
    catch (const std::bad_alloc &)
    {
        m_vertexData.clear();
        update();
        emit geometryRebuilt(0, int(timer.elapsed()));
        return;
    }
    Vtx *out = reinterpret_cast<Vtx *>(m_vertexData.data());
    qint64 usedVerts = 0;

    const LayerMode mode = m_layerMode;
    const int filter = m_layerFilter;
    const LayerAxis axis = m_layerAxis;
    const bool allowAll = (mode == LayerMode::All) || (filter < 0);
    const bool showAir = m_showAir;

    // 预留失败时优雅退化：只画已写下的部分
    bool overflow = false;

    auto grow = [&](int need) -> bool {
        if (usedVerts + need <= capVerts)
            return true;
        qint64 next = qMax<qint64>(capVerts * 3 / 2, usedVerts + need);
        try
        {
            m_vertexData.resize(static_cast<int>(next * qint64(sizeof(Vtx))));
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        capVerts = next;
        out = reinterpret_cast<Vtx *>(m_vertexData.data());
        return true;
    };

    // AO 采样：整方块算全遮挡，台阶/楼梯这类「不完整方块」算部分遮挡（接触阴影更柔和）
    auto aoSample = [&](int x, int y, int z) -> float {
        if (!occ || x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz)
            return 0.0f;
        const char o = occ[y * layer + z * sx + x];
        if (o & 4)
            return 1.0f;
        if (o & 8)
            return 0.55f;
        return 0.0f;
    };

    // 相邻格的「有效液面高度」（局部 0..1）：不是同类液体返回 -1。
    // 上面还连着同类液体的格子按满格算（整片水连成一体）；只认本轮真正画出来的格子，
    // 被层数条切掉的那一层不能参与连片，否则切面上会缺一层水面，看着是个洞。
    auto fluidTopAt = [&](int x, int y, int z, bool lava) -> float {
        if (!occ || x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz)
            return -1.0f;
        const int ni = y * layer + z * sx + x;
        if (!(occ[ni] & 1))
            return -1.0f;
        const int np = m_doc->paletteIndexAt(ni);
        if (np < 0 || np >= m_paletteCache.size())
            return -1.0f;
        const PaletteRender &npr = m_paletteCache.at(np);
        if (!npr.fluid || npr.fluidLava != lava)
            return -1.0f;
        // 这一格上面还有同类液体 → 它被上面的水/岩浆填满，液面就是格子顶
        if (y + 1 < sy)
        {
            const int ai = (y + 1) * layer + z * sx + x;
            if (occ[ai] & 1)
            {
                const int ap = m_doc->paletteIndexAt(ai);
                if (ap >= 0 && ap < m_paletteCache.size())
                {
                    const PaletteRender &apr = m_paletteCache.at(ap);
                    if (apr.fluid && apr.fluidLava == lava)
                        return 1.0f;
                }
            }
        }
        return npr.fluidTop;
    };

    // ---- 红石线粉尘：没有可用模型时自己拼（纯色块模式 / 缺材质 / 状态里没有连接属性）----
    // 原版红石线是贴在方块顶上的一层薄片：从中心向连接方向各伸一条线，孤立时是一个小点，
    // 拐角处补一点；遇到高一格的相邻线路还会沿侧面爬上去。不这么做的话，纯色模式下
    // 每格红石线都会画成一整块红砖，把整片电路糊成红色方块。
    auto emitRedstoneDust = [&](int x, int y, int z, const SchematicBlockState &st,
                                int power, uchar dimByte) -> bool {
        const float dustH = 0.25f / 16.0f;     // 1/64：与原版粉尘模型同高
        const float lineHalf = 0.0625f;        // 线宽 2/16 的一半
        const QColor color = redstoneColorForPower(power);
        const uchar cr = toByte(color.redF());
        const uchar cg = toByte(color.greenF());
        const uchar cb = toByte(color.blueF());

        auto emitQuad = [&](const float p0[3], const float p1[3],
                            const float p2[3], const float p3[3], float light) -> bool {
            if (!grow(6))
                return false;
            const float *corners[4] = { p0, p1, p2, p3 };
            Vtx q[4];
            for (int c = 0; c < 4; ++c)
            {
                Vtx &v = q[c];
                v.x = float(x) + corners[c][0];
                v.y = float(y) + corners[c][1];
                v.z = float(z) + corners[c][2];
                v.r = cr; v.g = cg; v.b = cb; v.a = 255;
                v.u = 0; v.v = 0;
                v.light = toByte(light);
                v.tex = 0;
                v.dim = dimByte;
                v.pad = 0;
                v.tr = 255; v.tg = 255; v.tb = 255;
                v.tinted = 1;
            }
            Vtx *dst = out + usedVerts;
            dst[0] = q[0]; dst[1] = q[1]; dst[2] = q[2];
            dst[3] = q[0]; dst[4] = q[2]; dst[5] = q[3];
            usedVerts += 6;
            return true;
        };

        // 顶面横片（原版粉尘就是一层几乎贴地的薄片）
        auto emitTop = [&](float x0, float z0, float x1, float z1) -> bool {
            const float a[3] = { x0, dustH, z1 };
            const float c0[3] = { x1, dustH, z1 };
            const float c1[3] = { x1, dustH, z0 };
            const float c2[3] = { x0, dustH, z0 };
            return emitQuad(a, c0, c1, c2, kFaceLight[2]);
        };

        // 方向顺序：北 / 东 / 南 / 西（-Z, +X, +Z, -X）
        static const int dxs[4] = { 0, 1, 0, -1 };
        static const int dzs[4] = { -1, 0, 1, 0 };
        static const char *const dirKeys[4] = { "north", "east", "south", "west" };
        bool side[4] = { false, false, false, false };
        bool up[4] = { false, false, false, false };
        bool hasProp = false;
        for (int d = 0; d < 4; ++d)
            if (st.properties.contains(QLatin1String(dirKeys[d])))
                hasProp = true;

        if (hasProp)
        {
            // 投影里保存了连接属性（Litematica 等格式）：直接照抄，最忠实
            for (int d = 0; d < 4; ++d)
            {
                const QString v = st.properties.value(QLatin1String(dirKeys[d]));
                side[d] = (v == QLatin1String("side") || v == QLatin1String("up"));
                up[d] = (v == QLatin1String("up"));
            }
        }
        else
        {
            // 只有方块名（旧格式转换过来的投影）：按四周邻居推断该怎么连
            for (int d = 0; d < 4; ++d)
            {
                const int nx = x + dxs[d];
                const int nz = z + dzs[d];
                if (nx < 0 || nz < 0 || nx >= sx || nz >= sz || !occ)
                    continue;
                const int ni = y * layer + nz * sx + nx;
                if ((occ[ni] & 1))
                {
                    const int np = m_doc->paletteIndexAt(ni);
                    if (np < 0 || np >= m_paletteCache.size())
                        continue;
                    if (isRedstoneConnector(m_doc->paletteEntry(np).name))
                        side[d] = true;
                    // 高一格：邻格是整块不透明方块、它上面还有红石线 → 沿它侧面爬上去
                    if (!side[d] && (occ[ni] & 4) && y + 1 < sy)
                    {
                        const int ai = (y + 1) * layer + nz * sx + nx;
                        if ((occ[ai] & 1))
                        {
                            const int ap = m_doc->paletteIndexAt(ai);
                            if (ap >= 0 && ap < m_paletteCache.size()
                                && isRedstoneWireName(m_doc->paletteEntry(ap).name))
                            {
                                side[d] = true;
                                up[d] = true;
                            }
                        }
                    }
                }
                else if (!(occ[ni] & 4) && y > 0)
                {
                    // 原版还有一条「斜下方」规则：本格旁边是空的、下面一格是红石线时也连一条线，
                    // 上坡爬到方块顶以后才接得上
                    const int di = (y - 1) * layer + nz * sx + nx;
                    if ((occ[di] & 1))
                    {
                        const int dp = m_doc->paletteIndexAt(di);
                        if (dp >= 0 && dp < m_paletteCache.size()
                            && isRedstoneConnector(m_doc->paletteEntry(dp).name))
                            side[d] = true;
                    }
                }
            }
        }

        const bool n = side[0], e = side[1], s = side[2], w = side[3];
        const bool any = n || e || s || w;
        if (!any)
        {
            // 孤立一格：画个小点（原版是一个小圆斑）
            if (!emitTop(0.5f - 0.125f, 0.5f - 0.125f, 0.5f + 0.125f, 0.5f + 0.125f))
                return false;
        }
        else
        {
            if (n && !emitTop(0.5f - lineHalf, 0.0f, 0.5f + lineHalf, 0.5f))
                return false;
            if (s && !emitTop(0.5f - lineHalf, 0.5f, 0.5f + lineHalf, 1.0f))
                return false;
            if (e && !emitTop(0.5f, 0.5f - lineHalf, 1.0f, 0.5f + lineHalf))
                return false;
            if (w && !emitTop(0.0f, 0.5f - lineHalf, 0.5f, 0.5f + lineHalf))
                return false;
            // 拐角 / 四向交汇时中心补一点（与原版 dot 模型的出现条件一致）
            if ((e && n) || (e && s) || (s && w) || (n && w))
            {
                if (!emitTop(0.5f - 0.125f, 0.5f - 0.125f,
                             0.5f + 0.125f, 0.5f + 0.125f))
                    return false;
            }
        }

        // 爬坡：沿相邻整块方块的侧面竖一条薄线（原版 up 模型）
        for (int d = 0; d < 4; ++d)
        {
            if (!up[d])
                continue;
            const float xz0 = 0.5f - lineHalf;
            const float xz1 = 0.5f + lineHalf;
            const float edge = dustH;
            if (d == 0)        // 北：贴在 z=0 内侧，面朝 +Z
            {
                const float a[3] = { xz0, 0.0f, edge };
                const float b[3] = { xz1, 0.0f, edge };
                const float c[3] = { xz1, 1.0f, edge };
                const float e2[3] = { xz0, 1.0f, edge };
                if (!emitQuad(a, b, c, e2, kFaceLight[4]))
                    return false;
            }
            else if (d == 1)   // 东：贴在 x=1 内侧，面朝 -X
            {
                const float a[3] = { 1.0f - edge, 0.0f, xz0 };
                const float b[3] = { 1.0f - edge, 0.0f, xz1 };
                const float c[3] = { 1.0f - edge, 1.0f, xz1 };
                const float e2[3] = { 1.0f - edge, 1.0f, xz0 };
                if (!emitQuad(a, b, c, e2, kFaceLight[1]))
                    return false;
            }
            else if (d == 2)   // 南：贴在 z=1 内侧，面朝 -Z
            {
                const float a[3] = { xz0, 0.0f, 1.0f - edge };
                const float b[3] = { xz0, 1.0f, 1.0f - edge };
                const float c[3] = { xz1, 1.0f, 1.0f - edge };
                const float e2[3] = { xz1, 0.0f, 1.0f - edge };
                if (!emitQuad(a, b, c, e2, kFaceLight[5]))
                    return false;
            }
            else               // 西：贴在 x=0 内侧，面朝 +X
            {
                const float a[3] = { edge, 0.0f, xz1 };
                const float b[3] = { edge, 0.0f, xz0 };
                const float c[3] = { edge, 1.0f, xz0 };
                const float e2[3] = { edge, 1.0f, xz1 };
                if (!emitQuad(a, b, c, e2, kFaceLight[0]))
                    return false;
            }
        }
        return true;
    };

    // 两遍生成：第一遍只写不透明方块，第二遍写半透明方块（玻璃/水/树叶/空气轮廓）。
    // 绘制时前段正常写深度、后段关掉深度写入，透过玻璃就不会把后面画的方块「挖空」。
    for (int pass = 0; pass < 2; ++pass)
    {
    const bool wantTranslucent = (pass == 1);
    if (pass == 1)
        m_opaqueVertexCount = int(usedVerts);

    for (int y = 0; y < sy; ++y)
    {
        for (int z = 0; z < sz; ++z)
        {
            for (int x = 0; x < sx; ++x)
            {
                const int i = y * layer + z * sx + x;
                // 剖切轴坐标（Y 水平 / X 左右 / Z 前后）
                const int axisCoord = (axis == LayerAxis::Y) ? y
                                     : (axis == LayerAxis::X) ? x
                                                              : z;
                if (!allowAll)
                {
                    if (mode == LayerMode::Single && axisCoord != filter)
                        continue;
                    if (mode == LayerMode::Below && axisCoord > filter)
                        continue;
                }
                const uchar dimByte =
                    (mode == LayerMode::Below && axisCoord < filter) ? 255 : 0;
                const int p = m_doc->paletteIndexAt(i);
                if (p < 0 || p >= m_paletteCache.size())
                    continue;
                const PaletteRender &pr = m_paletteCache.at(p);
                if (pr.isAir && !showAir)
                    continue;
                // 半透明方块放到第二遍：否则它先画出来会挡住后面才画的实心方块
                if ((!pr.opaque) != wantTranslucent)
                    continue;

                // 液体：上面还连着同类液体就把这一格拉满整格（一整片水连成一体），
                // 只有最上面那层才收口到液面高度；被同类液体挡住的内部面在下面一律不画
                bool lavaCell = false;
                float fluidTop = 1.0f;
                if (pr.fluid)
                {
                    lavaCell = pr.fluidLava;
                    fluidTop = (fluidTopAt(x, y + 1, z, lavaCell) >= 0.0f) ? 1.0f : pr.fluidTop;
                }

                // 逐格微小明度扰动：让纯色（无材质）渲染也有方块质感，避免大面积塑料感。
                // 用坐标哈希保证同一格子每次重建结果一致，不会闪烁。
                quint32 hsh = quint32(i) * 2654435761u;
                hsh ^= hsh >> 13;
                const float jitter = 0.94f + float(hsh & 15) * 0.008f;   // 0.94 ~ 1.06

                uchar cr = toByte(pr.color.redF());
                uchar cg = toByte(pr.color.greenF());
                uchar cb = toByte(pr.color.blueF());
                const uchar ca = uchar(pr.color.alpha());
                // 液体不参与逐格扰动：水面 / 岩浆面是一整片，格子之间有色差就会
                // 浮出一格一格的补丁和横条，看着像贴了瓷砖
                if (!pr.tinted && !pr.fluid)
                {
                    // 注意：必须先在浮点域夹到 0..255 再转 uchar，
                    // 否则像雪块(246) 乘 1.05 得 258 会溢出回卷成 2，渲染出怪色薄片
                    cr = uchar(qBound(0.0f, float(cr) * jitter, 255.0f));
                    cg = uchar(qBound(0.0f, float(cg) * jitter, 255.0f));
                    cb = uchar(qBound(0.0f, float(cb) * jitter, 255.0f));
                }

                // 红石线：没有可用模型时自己拼一层薄粉尘（有模型时继续走普通模型路径）
                if (pr.redstoneWire && pr.redstoneProcedural)
                {
                    if (!emitRedstoneDust(x, y, z, m_doc->paletteEntry(p),
                                          pr.redstonePower, dimByte))
                    {
                        overflow = true;
                        break;
                    }
                    continue;
                }

                for (const RenderBox &box : pr.boxes)
                {
                    // 液面高度逐格计算（其余方块仍用形状盒自己的高度）
                    const float boxTop = pr.fluid ? fluidTop : box.to[1];
                    // 零厚度元素的退化面（灯笼顶部十字贴片、链子、平面植物）没有面积，
                    // 画出来只是白占顶点，这里按面积提前丢掉
                    const float ex = box.to[0] - box.from[0];
                    const float ez = box.to[2] - box.from[2];
                    for (int f = 0; f < 6; ++f)
                    {
                        const int axis = kFaceAxis[f];
                        const int sign = kFaceSign[f];
                        const RenderFace &rf = box.face[f];

                        // 模型没声明这一面就整个不画（原版也只渲染 elements 里列出的面）
                        if (!rf.present)
                            continue;

                        // ---- 液体面：同类液体之间是内部面，画出来只会在水里留下一格一格的隔墙；
                        // 邻居液面更低时只补画露出来的那一截「台阶」，水面上才不会留下一条透光的缝 ----
                        float faceY0 = box.from[1];
                        float faceY1 = boxTop;
                        if (pr.fluid)
                        {
                            int fx = x, fy = y, fz = z;
                            if (axis == 0) fx += sign;
                            else if (axis == 1) fy += sign;
                            else fz += sign;
                            const float nTop = fluidTopAt(fx, fy, fz, lavaCell);
                            if (nTop >= 0.0f)
                            {
                                if (axis == 1 || nTop >= fluidTop - 1e-3f)
                                    continue;       // 被同类液体整个盖住
                                faceY0 = nTop;      // 邻居液面更低：只画上面那一截
                            }
                        }

                        const float ey = faceY1 - faceY0;
                        const float faceArea = (axis == 0) ? ey * ez : (axis == 1 ? ex * ez : ex * ey);
                        if (faceArea < 1e-6f)
                            continue;

                        // ---- 背面剔除：面贴格子边界、模型声明了 cullface、且邻居是不透明整方块 ----
                        bool onBoundary = false;
                        if (sign > 0)
                            onBoundary = box.to[axis] >= 1.0f - 1e-3f;
                        else
                            onBoundary = box.from[axis] <= 1e-3f;
                        if (rf.cull && onBoundary && occ)
                        {
                            int nx = x, ny = y, nz = z;
                            if (axis == 0) nx += sign;
                            else if (axis == 1) ny += sign;
                            else nz += sign;
                            if (nx >= 0 && ny >= 0 && nz >= 0 && nx < sx && ny < sy && nz < sz)
                            {
                                if (occ[ny * layer + nz * sx + nx] & 4)
                                    continue;       // 被邻居整块挡住，直接丢弃
                            }
                        }

                        if (!grow(6))
                        {
                            overflow = true;
                            break;
                        }

                        float faceLight = rf.shadeOff ? 1.0f : kFaceLight[f];
                        if (lavaCell)
                            faceLight = 1.0f;   // 岩浆自发光：六面都是满亮，不吃方向光
                        const float *uvr = rf.uv;
                        const int ta = kFaceTangent[f][0];
                        const int tb = kFaceTangent[f][1];

                        Vtx q[4];
                        float ao[4];
                        for (int c = 0; c < 4; ++c)
                        {
                            const float *corner = kFaceCorner[f][c];
                            float lx = box.from[0] + (box.to[0] - box.from[0]) * corner[0];
                            float ly = faceY0 + (faceY1 - faceY0) * corner[1];
                            float lz = box.from[2] + (box.to[2] - box.from[2]) * corner[2];
                            if (box.rotated)
                                rotatePoint3(lx, ly, lz,
                                             box.rotOrigin[0], box.rotOrigin[1], box.rotOrigin[2],
                                             box.rotAngle, box.rotAxis);
                            // blockstate 的 y 旋转：绕格心转（正角度即 MC 的 y 方向，
                            // 例如 facing=south 就是 y=90、模型东侧转到南侧），
                            // 几何与贴图一起转，所以楼梯/栅栏/玻璃板的朝向与贴图都正确
                            if (box.yaw)
                                rotatePoint3(lx, ly, lz, 0.5f, 0.5f, 0.5f, float(box.yaw), 1);

                            Vtx &v = q[c];
                            v.x = float(x) + lx;
                            v.y = float(y) + ly;
                            v.z = float(z) + lz;

                            // ---- 逐顶点 AO ----
                            float aoSum = 0.0f;
                            if (occ && onBoundary && !rf.noAo)
                            {
                                const int sxk = sign;
                                const int sa = corner[ta] > 0.5f ? 1 : -1;
                                const int sb = corner[tb] > 0.5f ? 1 : -1;
                                int bx = x, by = y, bz = z;
                                if (axis == 0) bx += sxk;
                                else if (axis == 1) by += sxk;
                                else bz += sxk;
                                int o1[3] = { bx, by, bz };
                                int o2[3] = { bx, by, bz };
                                int o3[3] = { bx, by, bz };
                                if (ta == 0) { o1[0] += sa; o3[0] += sa; } else if (ta == 1) { o1[1] += sa; o3[1] += sa; } else { o1[2] += sa; o3[2] += sa; }
                                if (tb == 0) { o2[0] += sb; o3[0] += sb; } else if (tb == 1) { o2[1] += sb; o3[1] += sb; } else { o2[2] += sb; o3[2] += sb; }
                                const float s1 = aoSample(o1[0], o1[1], o1[2]);
                                const float s2 = aoSample(o2[0], o2[1], o2[2]);
                                const float s3 = aoSample(o3[0], o3[1], o3[2]);
                                aoSum = (s1 > 0.9f && s2 > 0.9f) ? qMax(2.0f, s1 + s2 + s3)
                                                                 : (s1 + s2 + s3);
                                aoSum = qMin(3.0f, aoSum);
                            }
                            // 1.0 → 0.82 → 0.62 → 0.46：整块相邻时与旧观感一致，
                            // 台阶/栅栏这类只占半格的邻居给出 0.9 左右的柔和接触阴影
                            const float aoF = lavaCell ? 1.0f : (1.0f - 0.18f * aoSum);
                            ao[c] = aoF;
                            v.light = toByte(faceLight * aoF);
                            v.r = cr; v.g = cg; v.b = cb; v.a = ca;
                            v.tex = rf.useTex ? 255 : 0;
                            v.dim = dimByte;
                            v.pad = 0;
                            v.tr = rf.tint[0];
                            v.tg = rf.tint[1];
                            v.tb = rf.tint[2];
                            v.tinted = 1;

                            // 面内参数 -> 图集坐标（与旧版朝向规则保持一致）
                            const float kx = corner[0], ky = corner[1], kz = corner[2];
                            float fu, fv;
                            if (f == 0 || f == 1)      { fu = kz;          fv = 1.0f - ky; }
                            // 顶面：原版是「U 随 +X、V 随 +Z」（贴图按从上方看的样子平铺，
                            // 左上角对西北）。这里以前把 U 反了，凡是左右不对称的顶面贴图
                            // 都会镜像——弯曲铁轨最明显：shape=south_east 的贴图弧线其实从
                            // 南边西侧进、东边北侧出，U 一反就成了从南边东侧进、西边出，
                            // 弯道看着就是「斜着拧过去」的一团，和游戏里对不上。
                            else if (f == 2)           { fu = kx;          fv = kz; }
                            else if (f == 3)           { fu = kx;          fv = 1.0f - kz; }
                            else                       { fu = kx;          fv = 1.0f - ky; }
                            if (rf.useTex)
                            {
                                const float u = uvr[0] + fu * (uvr[2] - uvr[0]);
                                const float vv = uvr[1] + fv * (uvr[3] - uvr[1]);
                                v.u = static_cast<ushort>(qBound(0.0f, u, 1.0f) * 65535.0f);
                                v.v = static_cast<ushort>(qBound(0.0f, vv, 1.0f) * 65535.0f);
                            }
                            else
                            {
                                v.u = 0;
                                v.v = 0;
                            }
                        }

                        // 模型声明 shade:false 的面（十字花草、树叶、铁轨、藤蔓…）原版按
                        // 满亮渲染；这里按旋转后的实际法线补一点点方向光，交叉贴片和斜置
                        // 平面才不会糊成一块平板，同时不破坏"该亮的地方仍然亮"的观感
                        if (rf.shadeOff && !lavaCell)
                        {
                            const float ax = q[1].x - q[0].x, ay = q[1].y - q[0].y, az = q[1].z - q[0].z;
                            const float bx = q[3].x - q[0].x, by = q[3].y - q[0].y, bz = q[3].z - q[0].z;
                            const float nx = ay * bz - az * by;
                            const float ny = az * bx - ax * bz;
                            const float nz = ax * by - ay * bx;
                            const float nl = qSqrt(nx * nx + ny * ny + nz * nz);
                            float d = 0.0f;
                            if (nl > 1e-6f)
                                d = qAbs((nx * 0.36f + ny * 0.86f + nz * 0.36f) / nl);
                            faceLight = 0.88f + 0.12f * d;
                            for (int c = 0; c < 4; ++c)
                                q[c].light = toByte(faceLight * ao[c]);
                        }

                        // AO 各向异性修正：沿较亮的一对顶点切分，避免出现斜向暗条纹
                        const bool flip = (ao[0] + ao[2]) > (ao[1] + ao[3]);
                        Vtx *dst = out + usedVerts;
                        if (!flip)
                        {
                            dst[0] = q[0]; dst[1] = q[1]; dst[2] = q[2];
                            dst[3] = q[0]; dst[4] = q[2]; dst[5] = q[3];
                        }
                        else
                        {
                            dst[0] = q[1]; dst[1] = q[2]; dst[2] = q[3];
                            dst[3] = q[1]; dst[4] = q[3]; dst[5] = q[0];
                        }
                        usedVerts += 6;
                        ++m_quadCount;
                    }
                    if (overflow)
                        break;
                }
                if (overflow)
                    break;
            }
            if (overflow)
                break;
        }
        if (overflow)
            break;
    }
    if (overflow)
        break;
    }   // pass

    m_vertexCount = int(usedVerts);
    m_quadCount = int(usedVerts / 6);
    try
    {
        m_vertexData.resize(static_cast<int>(usedVerts * qint64(sizeof(Vtx))));
    }
    catch (...)
    {
        m_vertexData.clear();
        m_vertexCount = 0;
    }

    update();
    emit geometryRebuilt(m_quadCount, int(timer.elapsed()));
}

// ----------------------------------------------------------------- 生物 / 实体

void VoxelViewWidget::setEntityTextures(InstanceEntityTextures *textures)
{
    if (m_entityTextures == textures)
        return;
    m_entityTextures = textures;
    m_entAtlasDirty = true;
    rebuild();   // 实体三角面按新图集的 UV 重算（纯 CPU）
}

void VoxelViewWidget::setShowEntities(bool show)
{
    if (m_showEntities == show)
        return;
    m_showEntities = show;
    rebuild();
}

/**
 * @brief 把投影里的生物展开成三角面
 *
 * 与方块共用同一套顶点格式与着色器，只是换一张「实体图集」贴图；
 * 实体按脚底坐标摆放，朝向按原版 yaw（0 = 南 / +Z）绕 Y 旋转。
 * 层数过滤同样生效：只看某一层时，本层以外的生物一并隐藏/变暗。
 */
void VoxelViewWidget::buildEntityGeometry()
{
    m_entityData.clear();
    m_entityVertexCount = 0;
    m_drawnEntities = 0;
    m_entityUploadDirty = true;

    if (!m_showEntities || m_doc == nullptr || !m_doc->isLoaded()
        || m_entityTextures == nullptr || !m_entityTextures->isValid()
        || m_doc->entities().isEmpty())
    {
        return;
    }
    const float atlasSide = float(m_entityTextures->atlasSize());
    if (atlasSide <= 0.0f)
        return;

    const LayerMode mode = m_layerMode;
    const int filter = m_layerFilter;
    const LayerAxis axis = m_layerAxis;
    const bool allowAll = (mode == LayerMode::All) || (filter < 0);

    // 同一类实体只展开一次模型（一份猪的三角面可以给满场的猪复用）
    QHash<QString, QVector<EntityMeshTriangle>> meshCache;
    QHash<QString, QPair<QRect, QSize>> rectCache;

    // 光源方向（略偏上前方，与方块的面向光观感对齐）
    const QVector3D lightDir = QVector3D(0.32f, 0.86f, 0.38f).normalized();

    const auto pushVertex = [this](float x, float y, float z, float u, float v,
                                   float light, uchar dim) {
        Vtx vtx;
        vtx.x = x;
        vtx.y = y;
        vtx.z = z;
        vtx.r = 255;
        vtx.g = 255;
        vtx.b = 255;
        vtx.a = 255;
        vtx.u = static_cast<ushort>(qBound(0.0f, u, 1.0f) * 65535.0f);
        vtx.v = static_cast<ushort>(qBound(0.0f, v, 1.0f) * 65535.0f);
        vtx.light = toByte(light);
        vtx.tex = 255;
        vtx.dim = dim;
        vtx.pad = 0;
        vtx.tr = 255;
        vtx.tg = 255;
        vtx.tb = 255;
        vtx.tinted = 1;
        m_entityData.append(reinterpret_cast<const char *>(&vtx), int(sizeof(Vtx)));
        ++m_entityVertexCount;
    };

    for (const SchematicEntity &entity : m_doc->entities())
    {
        EntityModel model;
        if (!EntityModelLoader::instance()->modelFor(entity.id, model))
            continue;

        auto rectIt = rectCache.constFind(model.textureKey);
        if (rectIt == rectCache.constEnd())
        {
            QRect rect;
            QSize sourceSize;
            if (!m_entityTextures->rectForKey(model.textureKey, rect, &sourceSize)
                || rect.isEmpty())
            {
                rect = QRect();   // 实例里没这张贴图：这个实体跳过（不画成黑块）
                sourceSize = QSize();
            }
            rectIt = rectCache.insert(model.textureKey, QPair<QRect, QSize>(rect, sourceSize));
        }
        const QRect rect = rectIt.value().first;
        const QSize sourceSize = rectIt.value().second;
        if (rect.isEmpty() || sourceSize.isEmpty())
            continue;

        // 模型贴图布局和实例贴图对不上（羊、蝙蝠这类）就不画，免得贴图错位糊成一片
        if (model.uvLimitU > float(sourceSize.width()) + 2.0f
            || model.uvLimitV > float(sourceSize.height()) + 2.0f)
        {
            continue;
        }

        // 层过滤：按实体脚底所在的格判断（与方块的剖切同步，掀屋顶时生物一起消失）
        const int cellX = int(qFloor(entity.x));
        const int cellY = int(qFloor(entity.y));
        const int cellZ = int(qFloor(entity.z));
        const int axisCoord = (axis == LayerAxis::Y) ? cellY
                             : (axis == LayerAxis::X) ? cellX
                                                      : cellZ;
        if (!allowAll)
        {
            if (mode == LayerMode::Single && axisCoord != filter)
                continue;
            if (mode == LayerMode::Below && axisCoord > filter)
                continue;
        }
        const uchar dim = (mode == LayerMode::Below && axisCoord < filter) ? 255 : 0;

        auto meshIt = meshCache.constFind(model.id);
        if (meshIt == meshCache.constEnd())
            meshIt = meshCache.insert(model.id, buildEntityMesh(model));
        const QVector<EntityMeshTriangle> &mesh = meshIt.value();
        if (mesh.isEmpty())
            continue;

        // 模型面朝 -Z，原版 yaw 0 面朝 +Z，所以绕 Y 转 180 - yaw
        const float yawRad = qDegreesToRadians(180.0f - entity.yaw);
        const float cy = qCos(yawRad), sy = qSin(yawRad);

        const float uScale = 1.0f / atlasSide;
        // 模型 uv 是「像素」，按贴图在图集里的实际尺寸换算（贴图可能被等比缩过）
        const float texelU = float(rect.width()) / float(sourceSize.width()) * uScale;
        const float texelV = float(rect.height()) / float(sourceSize.height()) * uScale;
        const float rectU = float(rect.x()) * uScale;
        const float rectV = float(rect.y()) * uScale;

        for (const EntityMeshTriangle &tri : mesh)
        {
            // 法线跟着绕 Y 转，再做面向光
            const float nx = tri.normal.x() * cy + tri.normal.z() * sy;
            const float nz = -tri.normal.x() * sy + tri.normal.z() * cy;
            const float nd = qAbs(nx * lightDir.x() + tri.normal.y() * lightDir.y()
                                  + nz * lightDir.z());
            const float light = 0.56f + 0.44f * qBound(0.0f, nd, 1.0f);

            for (int k = 0; k < 3; ++k)
            {
                const QVector3D &p = tri.pos[k];
                const float px = p.x() * cy + p.z() * sy;
                const float pz = -p.x() * sy + p.z() * cy;
                const QVector2D &uv = tri.uv[k];
                pushVertex(float(entity.x) + px,
                           float(entity.y) + p.y(),
                           float(entity.z) + pz,
                           rectU + uv.x() * texelU,
                           rectV + uv.y() * texelV,
                           light, dim);
            }
        }
        ++m_drawnEntities;
    }
}

// ----------------------------------------------------------------- GL 初始化

void VoxelViewWidget::initializeGL(){
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);          // 几何按「面外 CCW」生成，背面无需绘制
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.055f, 0.065f, 0.09f, 1.0f);

    const char *vsrc = BLOCKBOX_GLSL_VS_HEADER R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
layout(location=2) in vec2 aUV;
layout(location=3) in float aLight;
layout(location=4) in float aTex;
layout(location=5) in float aDim;
layout(location=6) in vec4 aTint;
uniform mat4 uProj;
uniform mat4 uView;
out vec2 vUV;
out vec4 vColor;
out float vLight;
out float vTex;
out float vDim;
out float vDepth;
out vec4 vTint;
void main() {
    vec4 vp = uView * vec4(aPos, 1.0);
    vDepth = -vp.z;
    gl_Position = uProj * vp;
    vUV = aUV;
    vColor = aColor;
    vLight = aLight;
    vTex = aTex;
    vDim = aDim;
    vTint = aTint;
}
)";
    const char *fsrc = BLOCKBOX_GLSL_FS_HEADER R"(
in vec2 vUV;
in vec4 vColor;
in float vLight;
in float vTex;
in float vDim;
in float vDepth;
in vec4 vTint;
uniform sampler2D uAtlas;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec4 tex = texture(uAtlas, vUV);
    // 贴图颜色 × 种类着色（草/树叶/水）：原版这些贴图是灰度的，不乘色就是惨白方块
    vec3 texRgb = tex.rgb * vTint.rgb;
    vec3 base = mix(vColor.rgb, texRgb, vTex);
    vec3 c = base * vLight;
    c *= mix(1.0, 0.35, vDim);
    // 贴图 alpha 参与混合；完全透明的像素直接丢弃，
    // 否则花草/玻璃/梯子这些「不完整方块」的空白区域会写进深度缓冲，挡住后面的方块
    float alpha = vColor.a * mix(1.0, tex.a, vTex);
    if (alpha < 0.02)
        discard;
    // 远景轻微雾化，大场景不会在边界处「断层」
    float f = clamp((vDepth - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);
    c = mix(c, uFogColor, f * 0.55);
    fragColor = vec4(c, alpha);
}
)";

    m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vsrc);
    m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fsrc);
    m_program.link();

    // 叠加通道：选中线框 / 悬停高亮 / 藏宝箱高亮（自带颜色，线段与三角形共用）
    // uAlpha：整体透明度倍率，高亮通道用它把「被墙挡住的轮廓」画成半透明
    const char *ovVsrc = BLOCKBOX_GLSL_VS_HEADER R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uProj;
uniform mat4 uView;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";
    const char *ovFsrc = BLOCKBOX_GLSL_FS_HEADER R"(
in vec4 vColor;
uniform float uAlpha;
out vec4 fragColor;
void main() { fragColor = vec4(vColor.rgb, vColor.a * uAlpha); }
)";
    m_ovProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, ovVsrc);
    m_ovProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, ovFsrc);
    m_ovProgram.link();

    // 背景：竖向渐变 + 柔和暗角（不需要任何顶点缓冲）
    const char *bgVsrc = BLOCKBOX_GLSL_VS_HEADER R"(
out vec2 vUv;
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    vUv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 1.0, 1.0);
}
)";
    const char *bgFsrc = BLOCKBOX_GLSL_FS_HEADER R"(
in vec2 vUv;
uniform vec3 uTop;
uniform vec3 uBottom;
out vec4 fragColor;
void main() {
    vec3 c = mix(uBottom, uTop, smoothstep(0.0, 1.0, vUv.y));
    vec2 q = vUv - vec2(0.5);
    c *= 1.0 - 0.45 * dot(q, q);
    fragColor = vec4(c, 1.0);
}
)";
    m_bgProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, bgVsrc);
    m_bgProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, bgFsrc);
    m_bgProgram.link();
    m_bgVao.create();
    m_bgReady = true;

    m_vao.create();
    m_vbo.create();
    m_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    m_selVao.create();
    m_selVbo.create();
    m_selVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    m_hoverVao.create();
    m_hoverVbo.create();
    m_hoverVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    m_hiVao.create();
    m_hiVbo.create();
    m_hiVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    m_entVao.create();
    m_entVbo.create();
    m_entVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    m_glReady = true;
    m_uploadDirty = true;
    m_selUploadDirty = true;
    m_hoverUploadDirty = true;
    m_hiUploadDirty = true;
    m_entityUploadDirty = true;
    m_entAtlasDirty = true;
    m_atlasDirty = true;
}

void VoxelViewWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void VoxelViewWidget::uploadGeometry()
{
    m_uploadDirty = false;
    if (!m_glReady)
        return;
    if (m_vertexData.isEmpty() || m_vertexCount <= 0)
    {
        m_vertexCount = 0;
        return;
    }

    const int stride = int(sizeof(Vtx));
    m_vbo.bind();
    m_vbo.allocate(m_vertexData.constData(), int(m_vertexData.size()));
    if (m_vbo.size() < m_vertexData.size())
    {
        m_vertexCount = 0;   // 显存不足：宁可不画，也不按越界长度读顶点
        return;
    }

    m_vao.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(12));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_TRUE, stride, reinterpret_cast<void *>(16));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(20));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(21));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(22));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(24));
    m_vao.release();
}

void VoxelViewWidget::uploadAtlasTexture()
{
    m_atlasDirty = false;
    if (!m_glReady)
        return;

    if (!m_loader || !m_loader->isValid())
    {
        if (m_atlasTex && !m_atlasFallback)
        {
            delete m_atlasTex;
            m_atlasTex = nullptr;
        }
        if (!m_atlasTex)
        {
            m_atlasTex = new QOpenGLTexture(QOpenGLTexture::Target2D);
            m_atlasTex->create();
            m_atlasTex->setSize(1, 1);
            m_atlasTex->setFormat(QOpenGLTexture::RGBA8_UNorm);
            m_atlasTex->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
            const uchar white[4] = { 255, 255, 255, 255 };
            m_atlasTex->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, white);
            m_atlasTex->setMinificationFilter(QOpenGLTexture::Nearest);
            m_atlasTex->setMagnificationFilter(QOpenGLTexture::Nearest);
            m_atlasFallback = true;
        }
        return;
    }

    const QImage atlas = m_loader->atlasImage();
    if (atlas.isNull())
        return;

    if (!m_atlasTex || m_atlasFallback)
    {
        delete m_atlasTex;
        m_atlasTex = new QOpenGLTexture(atlas, QOpenGLTexture::DontGenerateMipMaps);
        m_atlasFallback = false;
    }
    else
    {
        m_atlasTex->setData(atlas, QOpenGLTexture::DontGenerateMipMaps);
    }
    m_atlasTex->setMinificationFilter(QOpenGLTexture::Nearest);
    m_atlasTex->setMagnificationFilter(QOpenGLTexture::Nearest);
    m_atlasTex->setWrapMode(QOpenGLTexture::ClampToEdge);
}

void VoxelViewWidget::uploadEntityGeometry()
{
    m_entityUploadDirty = false;
    if (!m_glReady)
        return;
    if (m_entityData.isEmpty() || m_entityVertexCount <= 0 || !m_entVao.isCreated()
        || !m_entVbo.isCreated())
    {
        m_entityVertexCount = 0;
        return;
    }

    const int stride = int(sizeof(Vtx));
    m_entVbo.bind();
    m_entVbo.allocate(m_entityData.constData(), int(m_entityData.size()));
    if (m_entVbo.size() < m_entityData.size())
    {
        m_entityVertexCount = 0;   // 显存不足：宁可不画
        return;
    }

    m_entVao.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(12));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_TRUE, stride, reinterpret_cast<void *>(16));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(20));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(21));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(22));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void *>(24));
    m_entVao.release();
}

void VoxelViewWidget::uploadEntityAtlasTexture()
{
    m_entAtlasDirty = false;
    if (!m_glReady)
        return;

    if (m_entityTextures == nullptr || !m_entityTextures->isValid())
    {
        if (m_entAtlasTex)
        {
            delete m_entAtlasTex;
            m_entAtlasTex = nullptr;
        }
        return;
    }

    const QImage atlas = m_entityTextures->atlasImage();
    if (atlas.isNull())
        return;

    if (!m_entAtlasTex)
        m_entAtlasTex = new QOpenGLTexture(atlas, QOpenGLTexture::DontGenerateMipMaps);
    else
        m_entAtlasTex->setData(atlas, QOpenGLTexture::DontGenerateMipMaps);
    m_entAtlasTex->setMinificationFilter(QOpenGLTexture::Nearest);
    m_entAtlasTex->setMagnificationFilter(QOpenGLTexture::Nearest);
    m_entAtlasTex->setWrapMode(QOpenGLTexture::ClampToEdge);
}

// ----------------------------------------------------------------- 叠加通道

void VoxelViewWidget::appendCellLines(int cellIndex, const QVector<RenderBox> &boxes,
                                      uchar r, uchar g, uchar b, QByteArray *outData) const
{
    const int layer = m_doc->layerSize();
    const int sx = m_doc->sizeX();
    const int y = cellIndex / layer;
    const int rem = cellIndex % layer;
    const int z = rem / sx;
    const int x = rem % sx;

    static const int kEdge[12][2] = {
        {0,1},{2,3},{4,5},{6,7},
        {0,2},{1,3},{4,6},{5,7},
        {0,4},{1,5},{2,6},{3,7}
    };
    static const float kCorner[8][3] = {
        {0,0,0},{1,0,0},{0,1,0},{1,1,0},
        {0,0,1},{1,0,1},{0,1,1},{1,1,1}
    };

    const float eps = 0.004f;
    if (boxes.isEmpty())
        return;
    qsizetype at = outData->size();
    // 每个盒子 12 条边 × 2 个端点 = 24 个顶点；一个方块可能有多个盒子（台阶、栅栏等），
    // 必须按盒子数量一次预留够，否则多盒子方块会写穿缓冲区（表现为堆损坏闪退）
    const qsizetype needBytes = qsizetype(boxes.size()) * 24 * 16;
    try
    {
        outData->resize(int(at + needBytes));
    }
    catch (const std::bad_alloc &)
    {
        return;
    }
    uchar *p = reinterpret_cast<uchar *>(outData->data()) + at;

    for (const RenderBox &box : boxes)
    {
        const float cx = (box.from[0] + box.to[0]) * 0.5f;
        const float cy = (box.from[1] + box.to[1]) * 0.5f;
        const float cz = (box.from[2] + box.to[2]) * 0.5f;
        float w[8][3];
        for (int c = 0; c < 8; ++c)
        {
            float lx = box.from[0] + (box.to[0] - box.from[0]) * kCorner[c][0];
            float ly = box.from[1] + (box.to[1] - box.from[1]) * kCorner[c][1];
            float lz = box.from[2] + (box.to[2] - box.from[2]) * kCorner[c][2];
            if (box.rotated)
                rotatePoint3(lx, ly, lz, box.rotOrigin[0], box.rotOrigin[1], box.rotOrigin[2],
                             box.rotAngle, box.rotAxis);
            if (box.yaw)
                rotatePoint3(lx, ly, lz, 0.5f, 0.5f, 0.5f, float(box.yaw), 1);
            w[c][0] = float(x) + lx + (lx >= cx ? eps : -eps);
            w[c][1] = float(y) + ly + (ly >= cy ? eps : -eps);
            w[c][2] = float(z) + lz + (lz >= cz ? eps : -eps);
        }
        for (int e = 0; e < 12; ++e)
        {
            for (int k = 0; k < 2; ++k)
            {
                const float *v = w[kEdge[e][k]];
                std::memcpy(p, v, 12);
                p[12] = r; p[13] = g; p[14] = b; p[15] = 255;
                p += 16;
            }
        }
    }
}

void VoxelViewWidget::appendCellFaces(int cellIndex, const QVector<RenderBox> &boxes,
                                      uchar r, uchar g, uchar b, uchar a,
                                      QByteArray *outData) const
{
    if (!m_doc || boxes.isEmpty())
        return;

    const int layer = m_doc->layerSize();
    const int sx = m_doc->sizeX();
    const int y = cellIndex / layer;
    const int rem = cellIndex % layer;
    const int z = rem / sx;
    const int x = rem % sx;

    static const float kCorner[8][3] = {
        {0,0,0},{1,0,0},{0,1,0},{1,1,0},
        {0,0,1},{1,0,1},{0,1,1},{1,1,1}
    };
    static const int kQuad[6][4] = {
        {1,5,7,3},{0,4,6,2},{3,7,6,2},{0,1,5,4},{4,5,7,6},{0,1,3,2}
    };

    const float eps = 0.003f;
    const qsizetype at = outData->size();
    // 每个盒子 6 个面 × 6 个顶点 × 16 字节
    const qsizetype needBytes = qsizetype(boxes.size()) * 6 * 6 * 16;
    try
    {
        outData->resize(int(at + needBytes));
    }
    catch (const std::bad_alloc &)
    {
        return;
    }
    uchar *p = reinterpret_cast<uchar *>(outData->data()) + at;

    for (const RenderBox &box : boxes)
    {
        const float cx = (box.from[0] + box.to[0]) * 0.5f;
        const float cy = (box.from[1] + box.to[1]) * 0.5f;
        const float cz = (box.from[2] + box.to[2]) * 0.5f;
        float w[8][3];
        for (int c = 0; c < 8; ++c)
        {
            float lx = box.from[0] + (box.to[0] - box.from[0]) * kCorner[c][0];
            float ly = box.from[1] + (box.to[1] - box.from[1]) * kCorner[c][1];
            float lz = box.from[2] + (box.to[2] - box.from[2]) * kCorner[c][2];
            // 先按盒心微微外扩，再套用模型旋转：外壳比方块本体大一圈才不会 z-fighting
            lx += (lx >= cx ? eps : -eps);
            ly += (ly >= cy ? eps : -eps);
            lz += (lz >= cz ? eps : -eps);
            if (box.rotated)
                rotatePoint3(lx, ly, lz, box.rotOrigin[0], box.rotOrigin[1], box.rotOrigin[2],
                             box.rotAngle, box.rotAxis);
            if (box.yaw)
                rotatePoint3(lx, ly, lz, 0.5f, 0.5f, 0.5f, float(box.yaw), 1);
            w[c][0] = float(x) + lx;
            w[c][1] = float(y) + ly;
            w[c][2] = float(z) + lz;
        }
        for (int f = 0; f < 6; ++f)
        {
            const int *idx = kQuad[f];
            static const int order[6] = { 0, 1, 2, 0, 2, 3 };
            for (int v = 0; v < 6; ++v)
            {
                const float *pos = w[idx[order[v]]];
                std::memcpy(p, pos, 12);
                p[12] = r; p[13] = g; p[14] = b; p[15] = a;
                p += 16;
            }
        }
    }
}

void VoxelViewWidget::rebuildSelectionGeometry()
{    m_selData.clear();
    m_selVertexCount = 0;
    m_selUploadDirty = true;

    if (!m_doc || !m_doc->isLoaded() || m_selectedCells.isEmpty())
        return;
    ensurePaletteCache();

    const int kMaxWireCells = 20000;   // 整份选中时不做无意义的百万格描边
    int wireCells = 0;
    try
    {
        for (int cell : m_selectedCells)
        {
            if (wireCells >= kMaxWireCells)
                break;
            if (cell < 0 || cell >= m_doc->volume())
                continue;
            const int p = m_doc->paletteIndexAt(cell);
            if (p < 0 || p >= m_paletteCache.size())
                continue;
            const PaletteRender &pr = m_paletteCache.at(p);
            if (pr.isAir)
                continue;
            appendCellLines(cell, pr.boxes, 60, 255, 130, &m_selData);
            ++wireCells;
        }
    }
    catch (const std::bad_alloc &)
    {
        m_selData.clear();
    }
    m_selVertexCount = int(m_selData.size() / 16);
    update();
}

void VoxelViewWidget::rebuildHoverGeometry()
{
    m_hoverData.clear();
    m_hoverTriVerts = 0;
    m_hoverLineVerts = 0;
    m_hoverUploadDirty = true;

    if (!m_doc || !m_doc->isLoaded() || m_hoverX == INT_MIN)
        return;
    if (!m_doc->contains(m_hoverX, m_hoverY, m_hoverZ))
        return;

    const int lx = m_hoverX - m_doc->originX();
    const int ly = m_hoverY - m_doc->originY();
    const int lz = m_hoverZ - m_doc->originZ();
    const int sx = m_doc->sizeX(), sy = m_doc->sizeY(), sz = m_doc->sizeZ();
    if (lx < 0 || ly < 0 || lz < 0 || lx >= sx || ly >= sy || lz >= sz)
        return;
    const int cell = ly * m_doc->layerSize() + lz * sx + lx;
    const int p = m_doc->paletteIndexAt(cell);
    ensurePaletteCache();
    if (p < 0 || p >= m_paletteCache.size())
        return;
    const PaletteRender &pr = m_paletteCache.at(p);
    if (pr.isAir && !m_showAir)
        return;

    const int layer = m_doc->layerSize();
    const int yy = cell / layer;
    const int rem = cell % layer;
    const int zz = rem / sx;
    const int xx = rem % sx;

    // 半透明填充（略微外扩，避免与方块表面 z-fighting）
    const float eps = 0.003f;
    try
    {
        for (const RenderBox &box : pr.boxes)
        {
            float w[8][3];
            for (int c = 0; c < 8; ++c)
            {
                static const float kCorner[8][3] = {
                    {0,0,0},{1,0,0},{0,1,0},{1,1,0},
                    {0,0,1},{1,0,1},{0,1,1},{1,1,1}
                };
                float bx = box.from[0] + (box.to[0] - box.from[0]) * kCorner[c][0];
                float by = box.from[1] + (box.to[1] - box.from[1]) * kCorner[c][1];
                float bz = box.from[2] + (box.to[2] - box.from[2]) * kCorner[c][2];
                if (box.rotated)
                    rotatePoint3(bx, by, bz, box.rotOrigin[0], box.rotOrigin[1], box.rotOrigin[2],
                                 box.rotAngle, box.rotAxis);
                if (box.yaw)
                    rotatePoint3(bx, by, bz, 0.5f, 0.5f, 0.5f, float(box.yaw), 1);
                w[c][0] = float(xx) + bx + (bx >= box.from[0] ? eps : -eps);
                w[c][1] = float(yy) + by + (by >= box.from[1] ? eps : -eps);
                w[c][2] = float(zz) + bz + (bz >= box.from[2] ? eps : -eps);
            }
            static const int kQuad[6][4] = {
                {1,5,7,3},{0,4,6,2},{3,7,6,2},{0,1,5,4},{4,5,7,6},{0,1,3,2}
            };
            for (int f = 0; f < 6; ++f)
            {
                const int *idx = kQuad[f];
                const int order[6] = { 0, 1, 2, 0, 2, 3 };
                for (int v = 0; v < 6; ++v)
                {
                    const float *pos = w[idx[order[v]]];
                    const qsizetype at = m_hoverData.size();
                    m_hoverData.resize(int(at + 16));
                    uchar *q = reinterpret_cast<uchar *>(m_hoverData.data()) + at;
                    std::memcpy(q, pos, 12);
                    q[12] = 255; q[13] = 214; q[14] = 90; q[15] = 90;
                }
            }
        }
        m_hoverTriVerts = int(m_hoverData.size() / 16);

        // 描边
        appendCellLines(cell, pr.boxes, 255, 226, 120, &m_hoverData);
        m_hoverLineVerts = int(m_hoverData.size() / 16) - m_hoverTriVerts;
    }
    catch (const std::bad_alloc &)
    {
        m_hoverData.clear();
        m_hoverTriVerts = 0;
        m_hoverLineVerts = 0;
    }
    update();
}

void VoxelViewWidget::rebuildHighlightGeometry()
{
    m_hiData.clear();
    m_hiTriVerts = 0;
    m_hiLineVerts = 0;
    m_hiUploadDirty = true;

    if (!m_doc || !m_doc->isLoaded() || m_highlightCells.isEmpty())
        return;
    ensurePaletteCache();

    const int vol = m_doc->volume();
    const int layer = m_doc->layerSize();
    const int sx = m_doc->sizeX();
    const LayerMode mode = m_layerMode;
    const int filter = m_layerFilter;
    const LayerAxis axis = m_layerAxis;
    const bool allowAll = (mode == LayerMode::All) || (filter < 0);

    // 只看某一层 / 此层以下时，高亮跟着同样的可见范围走：
    // 否则被层过滤藏起来的宝箱会以「透视轮廓」浮在图里，反而看不懂
    const auto cellVisible = [&](int cell) -> bool {
        if (cell < 0 || cell >= vol)
            return false;
        if (allowAll)
            return true;
        const int y = cell / layer;
        int axisCoord = y;
        if (axis != LayerAxis::Y)
        {
            const int rem = cell - y * layer;
            axisCoord = (axis == LayerAxis::X) ? (rem % sx) : (rem / sx);
        }
        return mode == LayerMode::Single ? (axisCoord == filter)
                                         : (axisCoord <= filter);
    };

    const int kMaxHighlightCells = 8192;
    int used = 0;
    try
    {
        // 第一段：淡金填充（只在能看到的地方叠一层暖色，让宝箱从同色调木头里跳出来）
        for (int cell : m_highlightCells)
        {
            if (used >= kMaxHighlightCells)
                break;
            if (!cellVisible(cell))
                continue;
            const int p = m_doc->paletteIndexAt(cell);
            if (p < 0 || p >= m_paletteCache.size())
                continue;
            const PaletteRender &pr = m_paletteCache.at(p);
            if (pr.isAir)
                continue;
            appendCellFaces(cell, pr.boxes, 255, 184, 54, 56, &m_hiData);
            ++used;
        }
        m_hiTriVerts = int(m_hiData.size() / 16);

        // 第二段：琥珀金描边；绘制时会再以半透明画一遍「透视」轮廓
        used = 0;
        for (int cell : m_highlightCells)
        {
            if (used >= kMaxHighlightCells)
                break;
            if (!cellVisible(cell))
                continue;
            const int p = m_doc->paletteIndexAt(cell);
            if (p < 0 || p >= m_paletteCache.size())
                continue;
            const PaletteRender &pr = m_paletteCache.at(p);
            if (pr.isAir)
                continue;
            appendCellLines(cell, pr.boxes, 255, 196, 72, &m_hiData);
            ++used;
        }
        m_hiLineVerts = int(m_hiData.size() / 16) - m_hiTriVerts;
    }
    catch (const std::bad_alloc &)
    {
        m_hiData.clear();
        m_hiTriVerts = 0;
        m_hiLineVerts = 0;
    }
    update();
}

void VoxelViewWidget::uploadSelectionGeometry()
{
    m_selUploadDirty = false;
    if (!m_glReady)
        return;
    if (m_selData.isEmpty())
    {
        m_selVertexCount = 0;
        return;
    }
    m_selVbo.bind();
    m_selVbo.allocate(m_selData.constData(), int(m_selData.size()));
    m_selVao.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16, reinterpret_cast<void *>(12));
    m_selVao.release();
}

void VoxelViewWidget::uploadHoverGeometry()
{
    m_hoverUploadDirty = false;
    if (!m_glReady)
        return;
    if (m_hoverData.isEmpty())
    {
        m_hoverTriVerts = 0;
        m_hoverLineVerts = 0;
        return;
    }
    m_hoverVbo.bind();
    m_hoverVbo.allocate(m_hoverData.constData(), int(m_hoverData.size()));
    m_hoverVao.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16, reinterpret_cast<void *>(12));
    m_hoverVao.release();
}

void VoxelViewWidget::uploadHighlightGeometry()
{
    m_hiUploadDirty = false;
    if (!m_glReady)
        return;
    if (m_hiData.isEmpty())
    {
        m_hiTriVerts = 0;
        m_hiLineVerts = 0;
        return;
    }
    m_hiVbo.bind();
    m_hiVbo.allocate(m_hiData.constData(), int(m_hiData.size()));
    m_hiVao.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16, reinterpret_cast<void *>(12));
    m_hiVao.release();
}

void VoxelViewWidget::ensureBackgroundResources()
{
    if (!m_bgReady)
        return;
}

// ----------------------------------------------------------------- 绘制

void VoxelViewWidget::paintGL()
{
    glClearColor(0.055f, 0.065f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_glReady)
        return;

    const float aspect = height() > 0 ? static_cast<float>(width()) / height() : 1.0f;
    QMatrix4x4 proj;
    proj.perspective(45.0f, aspect, 0.08f, 8000.0f);

    QMatrix4x4 view;
    QVector3D up(0, 1, 0);
    if (m_pitch < -1.45f)
        up = QVector3D(0, 0, -1);
    else if (m_pitch > 1.45f)
        up = QVector3D(0, 0, 1);
    view.lookAt(eyePosition(), lookTarget(), up);

    // ---- 背景 ----
    if (m_bgProgram.isLinked() && m_bgVao.isCreated())
    {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        m_bgProgram.bind();
        m_bgProgram.setUniformValue("uTop", QVector3D(0.115f, 0.135f, 0.19f));
        m_bgProgram.setUniformValue("uBottom", QVector3D(0.035f, 0.04f, 0.055f));
        m_bgVao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        m_bgVao.release();
        m_bgProgram.release();
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    if (!m_doc || !m_program.isLinked())
        return;

    if (m_uploadDirty)
        uploadGeometry();
    if (m_atlasDirty || !m_atlasTex)
        uploadAtlasTexture();

    if (m_vertexCount > 0 && m_atlasTex)
    {
        m_program.bind();
        m_atlasTex->bind(0);
        m_program.setUniformValue("uAtlas", 0);
        m_program.setUniformValue("uProj", proj);
        m_program.setUniformValue("uView", view);
        const float fogEnd = qMax(64.0f, m_viewDist * 2.2f);
        m_program.setUniformValue("uFogColor", QVector3D(0.06f, 0.07f, 0.10f));
        m_program.setUniformValue("uFogStart", fogEnd * 0.35f);
        m_program.setUniformValue("uFogEnd", fogEnd);

        m_vao.bind();
        // LEQUAL：草方块侧面这种「叠加层」与底层面完全共面，
        // 严格 LESS 会让叠加层被自己挡住（表现为草方块侧面缺一层草）
        glDepthFunc(GL_LEQUAL);
        const int opaqueVerts = qBound(0, m_opaqueVertexCount, m_vertexCount);
        if (opaqueVerts > 0)
            glDrawArrays(GL_TRIANGLES, 0, opaqueVerts);
        if (m_vertexCount > opaqueVerts)
        {
            // 半透明段：关掉深度写入，只做深度测试。
            // 这样玻璃/水/树叶不会把还没画出来的方块「挖」掉，叠在一起也不会互相剔穿。
            glDepthMask(GL_FALSE);
            glDrawArrays(GL_TRIANGLES, opaqueVerts, m_vertexCount - opaqueVerts);
            glDepthMask(GL_TRUE);
        }
        glDepthFunc(GL_LESS);
        m_vao.release();
        m_atlasTex->release(0);
        m_program.release();
    }

    // ---- 生物 / 实体：同一套着色器，换成实体贴图图集 ----
    if (m_showEntities && m_entityVertexCount > 0 && m_program.isLinked())
    {
        if (m_entityUploadDirty)
            uploadEntityGeometry();
        if (m_entAtlasDirty || !m_entAtlasTex)
            uploadEntityAtlasTexture();

        if (m_entityVertexCount > 0 && m_entAtlasTex)
        {
            m_program.bind();
            m_entAtlasTex->bind(0);
            m_program.setUniformValue("uAtlas", 0);
            m_program.setUniformValue("uProj", proj);
            m_program.setUniformValue("uView", view);
            const float fogEnd = qMax(64.0f, m_viewDist * 2.2f);
            m_program.setUniformValue("uFogColor", QVector3D(0.06f, 0.07f, 0.10f));
            m_program.setUniformValue("uFogStart", fogEnd * 0.35f);
            m_program.setUniformValue("uFogEnd", fogEnd);

            m_entVao.bind();
            glDepthFunc(GL_LEQUAL);
            // 实体模型是展开的盒子，正反两面都要能看见（贴图是单面的，不做剔除）
            glDisable(GL_CULL_FACE);
            glDrawArrays(GL_TRIANGLES, 0, m_entityVertexCount);
            glEnable(GL_CULL_FACE);
            glDepthFunc(GL_LESS);
            m_entVao.release();
            m_entAtlasTex->release(0);
            m_program.release();
        }
    }

    // ---- 叠加通道 ----
    if (m_ovProgram.isLinked())
    {
        // 只混合颜色、不写 alpha：QOpenGLWidget 的帧缓冲 alpha 会参与和下层
        // 页面的合成，半透明线框若把 alpha 拉低，线头处会透出下面地图的绿/蓝
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

        if (m_hoverUploadDirty)
            uploadHoverGeometry();
        if (m_selUploadDirty)
            uploadSelectionGeometry();
        if (m_hiUploadDirty)
            uploadHighlightGeometry();

        // 高亮格（藏宝箱）：先以无深度测试的半透明轮廓「透」出来，
        // 再按正常深度画淡金填充与亮边；被方块包住的宝箱只剩透视轮廓
        if (m_hiData.size() > 0 && m_hiVao.isCreated())
        {
            m_ovProgram.bind();
            m_ovProgram.setUniformValue("uProj", proj);
            m_ovProgram.setUniformValue("uView", view);
            m_hiVao.bind();

            if (m_hiLineVerts > 0)
            {
                m_ovProgram.setUniformValue("uAlpha", 0.36f);
                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glLineWidth(1.3f);
                glDrawArrays(GL_LINES, m_hiTriVerts, m_hiLineVerts);
                glLineWidth(1.0f);
                glDepthMask(GL_TRUE);
                glEnable(GL_DEPTH_TEST);
            }

            m_ovProgram.setUniformValue("uAlpha", 1.0f);
            glDisable(GL_CULL_FACE);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -1.0f);
            glDepthMask(GL_FALSE);
            if (m_hiTriVerts > 0)
                glDrawArrays(GL_TRIANGLES, 0, m_hiTriVerts);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDepthMask(GL_TRUE);
            if (m_hiLineVerts > 0)
            {
                glDepthFunc(GL_LEQUAL);
                glLineWidth(2.2f);
                glDrawArrays(GL_LINES, m_hiTriVerts, m_hiLineVerts);
                glLineWidth(1.0f);
                glDepthFunc(GL_LESS);
            }
            glEnable(GL_CULL_FACE);
            m_hiVao.release();
            m_ovProgram.release();
        }

        if (m_hoverData.size() > 0 && m_hoverVao.isCreated())
        {
            m_ovProgram.bind();
            m_ovProgram.setUniformValue("uProj", proj);
            m_ovProgram.setUniformValue("uView", view);
            m_ovProgram.setUniformValue("uAlpha", 1.0f);
            m_hoverVao.bind();
            glDisable(GL_CULL_FACE);        // 悬停填充是半透明外壳，两面都画
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -1.0f);
            glDepthMask(GL_FALSE);
            if (m_hoverTriVerts > 0)
                glDrawArrays(GL_TRIANGLES, 0, m_hoverTriVerts);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDepthMask(GL_TRUE);
            if (m_hoverLineVerts > 0)
            {
                glDepthFunc(GL_LEQUAL);
                glLineWidth(1.6f);
                glDrawArrays(GL_LINES, m_hoverTriVerts, m_hoverLineVerts);
                glLineWidth(1.0f);
                glDepthFunc(GL_LESS);
            }
            glEnable(GL_CULL_FACE);
            m_hoverVao.release();
            m_ovProgram.release();
        }

        if (m_selVertexCount > 0 && m_selVao.isCreated())
        {
            m_ovProgram.bind();
            m_ovProgram.setUniformValue("uProj", proj);
            m_ovProgram.setUniformValue("uView", view);
            m_ovProgram.setUniformValue("uAlpha", 1.0f);
            glDepthFunc(GL_LEQUAL);
            glLineWidth(1.8f);
            m_selVao.bind();
            glDrawArrays(GL_LINES, 0, m_selVertexCount);
            m_selVao.release();
            glLineWidth(1.0f);
            glDepthFunc(GL_LESS);
            m_ovProgram.release();
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    m_needsRepaint = false;
}

// ----------------------------------------------------------------- 射线拾取

void VoxelViewWidget::boxWorldBounds(const RenderBox &box, int x, int y, int z,
                                     float &x0, float &y0, float &z0,
                                     float &x1, float &y1, float &z1)
{
    x0 = y0 = z0 = 1e9f;
    x1 = y1 = z1 = -1e9f;
    for (int bit = 0; bit < 8; ++bit)
    {
        float px = (bit & 1) ? box.to[0] : box.from[0];
        float py = (bit & 2) ? box.to[1] : box.from[1];
        float pz = (bit & 4) ? box.to[2] : box.from[2];
        if (box.rotated)
            rotatePoint3(px, py, pz, box.rotOrigin[0], box.rotOrigin[1], box.rotOrigin[2],
                         box.rotAngle, box.rotAxis);
        if (box.yaw)
            rotatePoint3(px, py, pz, 0.5f, 0.5f, 0.5f, float(box.yaw), 1);
        px += float(x);
        py += float(y);
        pz += float(z);
        x0 = qMin(x0, px); y0 = qMin(y0, py); z0 = qMin(z0, pz);
        x1 = qMax(x1, px); y1 = qMax(y1, py); z1 = qMax(z1, pz);
    }
}

bool VoxelViewWidget::pickRay(const QPoint &pos, int &x, int &y, int &z, bool skipAir,
                              int *outNormal) const
{
    if (!m_doc || !m_doc->isLoaded() || height() <= 0 || m_occupancy.isEmpty())
        return false;

    const int sx = m_doc->sizeX(), sy = m_doc->sizeY(), sz = m_doc->sizeZ();
    const int layer = m_doc->layerSize();
    if (sx <= 0 || sy <= 0 || sz <= 0)
        return false;

    const float aspect = static_cast<float>(width()) / height();
    QMatrix4x4 proj;
    proj.perspective(45.0f, aspect, 0.08f, 8000.0f);
    QMatrix4x4 view;
    QVector3D up(0, 1, 0);
    if (m_pitch < -1.45f)
        up = QVector3D(0, 0, -1);
    else if (m_pitch > 1.45f)
        up = QVector3D(0, 0, 1);
    view.lookAt(eyePosition(), lookTarget(), up);

    QMatrix4x4 invVP = (proj * view).inverted();
    if (invVP.isIdentity())
        return false;

    const float ndcX = (2.0f * pos.x()) / width() - 1.0f;
    const float ndcY = 1.0f - (2.0f * pos.y()) / height();
    QVector4D near4 = invVP * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D far4 = invVP * QVector4D(ndcX, ndcY, 1.0f, 1.0f);
    if (qFuzzyIsNull(near4.w()) || qFuzzyIsNull(far4.w()))
        return false;
    float ro0[3] = { near4.x() / near4.w(), near4.y() / near4.w(), near4.z() / near4.w() };
    float rf[3] = { far4.x() / far4.w(), far4.y() / far4.w(), far4.z() / far4.w() };
    float rd[3] = { rf[0] - ro0[0], rf[1] - ro0[1], rf[2] - ro0[2] };
    const float len = qSqrt(rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
    if (len < 1e-8f)
        return false;
    rd[0] /= len; rd[1] /= len; rd[2] /= len;

    // 先与包围盒求交，缩短 DDA 起点
    const float bmin[3] = { 0.0f, 0.0f, 0.0f };
    const float bmax[3] = { float(sx), float(sy), float(sz) };
    float t0, t1;
    if (!slabHitF(ro0, rd, bmin, bmax, t0, t1))
        return false;
    const float tStart = qMax(t0, 0.0f);
    if (t1 < tStart)
        return false;

    // DDA 步进（Amanatides & Woo）
    const float eps = 1e-4f;
    float p[3] = {
        ro0[0] + rd[0] * (tStart + eps),
        ro0[1] + rd[1] * (tStart + eps),
        ro0[2] + rd[2] * (tStart + eps)
    };
    int cell[3] = {
        qBound(0, int(qFloor(p[0])), sx - 1),
        qBound(0, int(qFloor(p[1])), sy - 1),
        qBound(0, int(qFloor(p[2])), sz - 1)
    };
    int step[3];
    float tMax[3], tDelta[3];
    for (int a = 0; a < 3; ++a)
    {
        const int n = (a == 0) ? sx : ((a == 1) ? sy : sz);
        if (rd[a] > 0.0f)
        {
            step[a] = 1;
            tMax[a] = (float(cell[a] + 1) - ro0[a]) / rd[a];
            tDelta[a] = 1.0f / rd[a];
        }
        else if (rd[a] < 0.0f)
        {
            step[a] = -1;
            tMax[a] = (float(cell[a]) - ro0[a]) / rd[a];
            tDelta[a] = -1.0f / rd[a];
        }
        else
        {
            step[a] = 0;
            tMax[a] = 1e30f;
            tDelta[a] = 1e30f;
        }
        Q_UNUSED(n);
    }

    const char *occ = m_occupancy.constData();
    const int maxSteps = sx + sy + sz + 4;

    for (int stepIdx = 0; stepIdx < maxSteps; ++stepIdx)
    {
        if (cell[0] < 0 || cell[1] < 0 || cell[2] < 0
                || cell[0] >= sx || cell[1] >= sy || cell[2] >= sz)
            break;
        const int cIdx = cell[1] * layer + cell[2] * sx + cell[0];
        const uchar bits = uchar(occ[cIdx] & 3);
        const bool isAirCell = (bits & 1) == 0;
        const bool pickable = (bits & 1) != 0 || (!skipAir && (bits & 2) != 0);

        if (pickable && !(isAirCell && skipAir))
        {
            // 命中格：用真实形状做精确测试（整格方块直接判定命中）
            const int pp = m_doc->paletteIndexAt(cIdx);
            bool hit = false;
            float hitT = 1e30f;
            int hitAxis = -1;
            if (pp >= 0 && pp < m_paletteCache.size())
            {
                const PaletteRender &pr = m_paletteCache.at(pp);
                if (pr.isAir)
                {
                    const float lx = float(cell[0]), ly = float(cell[1]), lz = float(cell[2]);
                    const float bbmin[3] = { lx, ly, lz };
                    const float bbmax[3] = { lx + 1.0f, ly + 1.0f, lz + 1.0f };
                    hit = slabHitF(ro0, rd, bbmin, bbmax, t0, t1);
                    hitT = qMax(0.0f, t0);
                }
                else
                {
                    for (const RenderBox &box : pr.boxes)
                    {
                        float bx0, by0, bz0, bx1, by1, bz1;
                        boxWorldBounds(box, cell[0], cell[1], cell[2],
                                       bx0, by0, bz0, bx1, by1, bz1);
                        const float bbmin[3] = { bx0, by0, bz0 };
                        const float bbmax[3] = { bx1, by1, bz1 };
                        if (slabHitF(ro0, rd, bbmin, bbmax, t0, t1))
                        {
                            const float ht = qMax(0.0f, t0);
                            if (ht < hitT)
                            {
                                hitT = ht;
                                // 由命中点与盒面距离推断朝向轴（供贴面放置用）
                                const float px = ro0[0] + rd[0] * hitT;
                                const float py = ro0[1] + rd[1] * hitT;
                                const float pz = ro0[2] + rd[2] * hitT;
                                const float d[3] = {
                                    qMin(qAbs(px - bx0), qAbs(px - bx1)),
                                    qMin(qAbs(py - by0), qAbs(py - by1)),
                                    qMin(qAbs(pz - bz0), qAbs(pz - bz1))
                                };
                                hitAxis = 0;
                                if (d[1] < d[hitAxis]) hitAxis = 1;
                                if (d[2] < d[hitAxis]) hitAxis = 2;
                                if (outNormal)
                                {
                                    const float mid = 0.5f * (hitAxis == 0 ? (bx0 + bx1)
                                                     : (hitAxis == 1 ? (by0 + by1) : (bz0 + bz1)));
                                    const float value = hitAxis == 0 ? px : (hitAxis == 1 ? py : pz);
                                    outNormal[0] = outNormal[1] = outNormal[2] = 0;
                                    outNormal[hitAxis] = (value >= mid) ? 1 : -1;
                                }
                            }
                            hit = true;
                        }
                    }
                }
            }
            if (hit)
            {
                x = cell[0] + m_doc->originX();
                y = cell[1] + m_doc->originY();
                z = cell[2] + m_doc->originZ();
                return true;
            }
        }

        // 前进到下一格
        int axis = 0;
        if (tMax[1] < tMax[axis]) axis = 1;
        if (tMax[2] < tMax[axis]) axis = 2;
        if (tMax[axis] > 1e29f)
            break;
        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];
        if (step[axis] == 0)
            break;
    }
    return false;
}

bool VoxelViewWidget::rayPick(const QPoint &pos, int &x, int &y, int &z) const
{
    return pickRay(pos, x, y, z);
}

// 放置目标：命中方块本身；开启「贴面放置」时改用它朝外的相邻格
bool VoxelViewWidget::pickPlaceTarget(const QPoint &pos, int &x, int &y, int &z) const
{
    int n[3] = { 0, 0, 0 };
    if (!pickRay(pos, x, y, z, /*skipAir=*/false, n))
        return false;
    // 点到空气格（「显示空气」打开时的线框）就直接填进这一格，不再往旁边偏移
    if (m_doc && m_doc->block(x, y, z).isAir())
        return true;
    if (m_placeAdjacent)
    {
        const int tx = x + n[0], ty = y + n[1], tz = z + n[2];
        if (m_doc && m_doc->contains(tx, ty, tz))
        {
            x = tx;
            y = ty;
            z = tz;
        }
    }
    return true;
}

// ----------------------------------------------------------------- 交互

void VoxelViewWidget::mousePressEvent(QMouseEvent *e)
{
    m_pendingEdit = EditNone;

    const bool left = (e->button() == Qt::LeftButton);
    const bool right = (e->button() == Qt::RightButton);

    // Ctrl+左键：立即放置（保留的快捷方式，按下即生效）
    if (left && (e->modifiers() & Qt::ControlModifier))
    {
        int x, y, z;
        if (pickPlaceTarget(e->pos(), x, y, z))
        {
            emit placeRequested(x, y, z);
            return;
        }
    }
    // 左键单击：按当前工具破坏 / 放置 / 选取（拖动仍是转向视角，松开时才生效）
    else if (left)
    {
        int x, y, z;
        if (m_leftAction == LeftAction::Break)
        {
            // 只破坏真正能看到的方块（空气线框点了不产生空操作）
            if (pickRay(e->pos(), x, y, z, /*skipAir=*/true))
            {
                m_pendingEdit = EditBreak;
                m_pendingX = x;
                m_pendingY = y;
                m_pendingZ = z;
            }
        }
        else if (m_leftAction == LeftAction::Place)
        {
            if (pickPlaceTarget(e->pos(), x, y, z))
            {
                m_pendingEdit = EditPlace;
                m_pendingX = x;
                m_pendingY = y;
                m_pendingZ = z;
            }
        }
    }
    // 右键单击：放置当前方块（所有工具通用）
    else if (right)
    {
        int x, y, z;
        if (pickPlaceTarget(e->pos(), x, y, z))
        {
            m_pendingEdit = EditPlace;
            m_pendingX = x;
            m_pendingY = y;
            m_pendingZ = z;
        }
    }

    setFocus(Qt::MouseFocusReason);
    m_dragging = true;
    m_pressPos = e->pos();
    m_dragMoved = false;
    m_lookActive = false;
    m_selectPending = (m_leftAction == LeftAction::Select) && left;
    setCursor(Qt::ClosedHandCursor);
}

void VoxelViewWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
    {
        int x, y, z;
        if (pickRay(e->pos(), x, y, z))
        {
            setHover(x, y, z);
            setCursor(Qt::PointingHandCursor);
        }
        else
        {
            setHover(INT_MIN, INT_MIN, INT_MIN);
            setCursor(Qt::ArrowCursor);
        }
        return;
    }

    if (!m_lookActive)
    {
        // 按下后移动超过阈值才进入「转向视角」，轻点不会误转
        if ((e->pos() - m_pressPos).manhattanLength() <= 4)
            return;
        m_lookActive = true;
        m_dragMoved = true;
        setCursor(Qt::BlankCursor);
        m_lookAnchor = rect().center();
        QCursor::setPos(mapToGlobal(m_lookAnchor));
        return;
    }

    // 指针锁在视口中心：每帧按相对位移转身，再拉回中心，可以无限转
    const QPoint delta = e->pos() - m_lookAnchor;
    // 高 DPI 下回中后读回的坐标可能差 1 像素，忽略这种回声，避免视角自转
    if (qAbs(delta.x()) <= 1 && qAbs(delta.y()) <= 1)
        return;

    const float radPerPx = mouseLookRadiansPerPixel();
    const float dy = m_invertLookY ? -delta.y() : delta.y();
    lookBy(delta.x() * radPerPx, dy * radPerPx);
    QCursor::setPos(mapToGlobal(m_lookAnchor));
}

void VoxelViewWidget::mouseReleaseEvent(QMouseEvent *e)
{
    const bool selecting = m_selectPending && !m_dragMoved && !m_lookActive
                           && e->button() == Qt::LeftButton;
    // 拖动过 = 转向视角，不执行编辑；否则执行按下时记下的破坏 / 放置
    const int pendingEdit = (m_dragMoved || m_lookActive || selecting) ? EditNone : m_pendingEdit;
    const int px = m_pendingX, py = m_pendingY, pz = m_pendingZ;

    if (m_lookActive)
    {
        endLookCapture(true);
    }
    else
    {
        m_dragging = false;
        unsetCursor();
        if (m_leftAction == LeftAction::Select)
            setCursor(Qt::PointingHandCursor);
    }
    m_selectPending = false;
    m_pendingEdit = EditNone;

    if (selecting)
    {
        int x, y, z;
        if (pickRay(e->pos(), x, y, z, /*skipAir=*/true))
            emit selectRequested(x, y, z);
        else
            emit selectRequested(INT_MIN, INT_MIN, INT_MIN);   // 点到空白/天空：交给编辑器取消选取
        return;
    }
    if (pendingEdit == EditBreak)
        emit breakRequested(px, py, pz);
    else if (pendingEdit == EditPlace)
        emit placeRequested(px, py, pz);
}

void VoxelViewWidget::wheelEvent(QWheelEvent *e)
{
    // 沿视线前后推进 / 后退（第一人称下比绕中心缩放更直观）
    const float steps = e->angleDelta().y() / 120.0f;
    if (qFuzzyIsNull(steps))
    {
        e->ignore();
        return;
    }
    const float distance = qBound(0.6f, m_viewDist * 0.12f, 16.0f) * steps;
    m_eyePos += lookForward() * distance;
    m_viewDist = qBound(1.2f, m_viewDist - distance, 6000.0f);
    m_eyeValid = true;
    update();
    e->accept();
}

void VoxelViewWidget::leaveEvent(QEvent *e)
{
    Q_UNUSED(e);
    if (m_dragging)
        return;
    setHover(INT_MIN, INT_MIN, INT_MIN);
}

void VoxelViewWidget::keyPressEvent(QKeyEvent *e)
{
    if (isFlyKey(e->key()))
    {
        if (!e->isAutoRepeat())
        {
            // 双击 W 疾跑（与《我的世界》一致）；按住 Ctrl 同样疾跑
            if (e->key() == Qt::Key_W)
            {
                const qint64 now = m_clock.elapsed();
                if (m_lastWTapMs > 0 && now - m_lastWTapMs <= 350)
                    m_sprintLatch = true;
                m_lastWTapMs = now;
            }
            m_heldKeys.insert(e->key());
        }
        if (!m_flyTimer.isActive())
            m_flyTimer.start();
        e->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}

void VoxelViewWidget::keyReleaseEvent(QKeyEvent *e)
{
    if (isFlyKey(e->key()))
    {
        if (!e->isAutoRepeat())
        {
            if (e->key() == Qt::Key_W)
            {
                m_sprintLatch = false;
                m_lastWTapMs = 0;
            }
            m_heldKeys.remove(e->key());
        }
        if (m_heldKeys.isEmpty())
            m_flyTimer.stop();
        e->accept();
        return;
    }
    QOpenGLWidget::keyReleaseEvent(e);
}

void VoxelViewWidget::focusOutEvent(QFocusEvent *e)
{
    m_heldKeys.clear();
    m_flyTimer.stop();
    m_sprintLatch = false;
    m_lastWTapMs = 0;
    if (m_lookActive)
        endLookCapture(false);
    QOpenGLWidget::focusOutEvent(e);
}

void VoxelViewWidget::destroyGL()
{
    makeCurrent();
    if (m_vbo.isCreated())
        m_vbo.destroy();
    if (m_vao.isCreated())
        m_vao.destroy();
    if (m_program.isLinked())
        m_program.removeAllShaders();
    if (m_atlasTex)
    {
        delete m_atlasTex;
        m_atlasTex = nullptr;
    }
    if (m_ovProgram.isLinked())
        m_ovProgram.removeAllShaders();
    if (m_selVbo.isCreated())
        m_selVbo.destroy();
    if (m_selVao.isCreated())
        m_selVao.destroy();
    if (m_hoverVbo.isCreated())
        m_hoverVbo.destroy();
    if (m_hoverVao.isCreated())
        m_hoverVao.destroy();
    if (m_hiVbo.isCreated())
        m_hiVbo.destroy();
    if (m_hiVao.isCreated())
        m_hiVao.destroy();
    if (m_bgProgram.isLinked())
        m_bgProgram.removeAllShaders();
    if (m_bgVao.isCreated())
        m_bgVao.destroy();
    if (m_entVbo.isCreated())
        m_entVbo.destroy();
    if (m_entVao.isCreated())
        m_entVao.destroy();
    if (m_entAtlasTex)
    {
        delete m_entAtlasTex;
        m_entAtlasTex = nullptr;
    }
    m_entityVertexCount = 0;
    m_drawnEntities = 0;
    m_selVertexCount = 0;
    m_hoverTriVerts = 0;
    m_hoverLineVerts = 0;
    m_hiTriVerts = 0;
    m_hiLineVerts = 0;
    m_bgReady = false;
    m_glReady = false;
    doneCurrent();
}



