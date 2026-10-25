/**
 * @file   VoxelViewWidget.cpp
 * @brief  3D 体素方块视图控件实现（OpenGL）
 * @author BlockBox Team
 * @date   2026-08-18
 */

#include "VoxelViewWidget.h"

#include "components/GlslCompat.h"

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
}

VoxelViewWidget::~VoxelViewWidget()
{
    destroyGL();
}

// ----------------------------------------------------------------- 数据注入

void VoxelViewWidget::setDocument(SchematicDocument *doc)
{
    m_doc = doc;
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
    if (m_layerFilter == layer)
        return;
    m_layerFilter = layer;
    rebuild();
}

void VoxelViewWidget::setLayerMode(LayerMode mode)
{
    if (m_layerMode == mode)
        return;
    m_layerMode = mode;
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
    rebuildSelectionGeometry();
    rebuildHoverGeometry();
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
    return m_orbitCenter - lookForward() * m_orbitDist;
}

QVector3D VoxelViewWidget::lookTarget() const
{
    return m_orbitCenter;
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
    m_orbitCenter = center;
    m_orbitDist = radius / qSin(qDegreesToRadians(22.5f)) * 0.95f;
    m_orbitDist = qBound(4.0f, m_orbitDist, 6000.0f);
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
    m_orbitCenter = QVector3D((minX + maxX + 1) * 0.5f, (minY + maxY + 1) * 0.5f,
                              (minZ + maxZ + 1) * 0.5f);
    const float radius = 0.5f * qSqrt(float(maxX - minX + 1) * (maxX - minX + 1)
                                      + float(maxY - minY + 1) * (maxY - minY + 1)
                                      + float(maxZ - minZ + 1) * (maxZ - minZ + 1));
    m_orbitDist = qBound(2.0f, radius / qSin(qDegreesToRadians(22.5f)) * 1.05f, 6000.0f);
    m_eyeValid = true;
    update();
}

void VoxelViewWidget::focusAll()
{
    if (!m_doc || !m_doc->isLoaded())
        return;
    QVector3D center(m_doc->sizeX() * 0.5f, m_doc->sizeY() * 0.5f, m_doc->sizeZ() * 0.5f);
    const float radius = qMax(4.0f, 0.5f * qSqrt(float(m_doc->sizeX()) * m_doc->sizeX()
                                                 + float(m_doc->sizeY()) * m_doc->sizeY()
                                                 + float(m_doc->sizeZ()) * m_doc->sizeZ()));
    m_orbitCenter = center;
    m_orbitDist = qBound(3.0f, radius / qSin(qDegreesToRadians(22.5f)) * 0.95f, 6000.0f);
    m_eyeValid = true;
    update();
}

void VoxelViewWidget::applyViewPreset(ViewPreset preset)
{
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
    m_eyeValid = true;
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
    m_orbitCenter += delta;
    m_eyeValid = true;
    update();
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

    // 速度随观察距离缩放：无论放大到方块级还是退到全景，手感一致
    const float speed = qMax(2.0f, m_orbitDist * 0.55f) * dt;
    const QVector3D fwd = lookForward();
    QVector3D flat = QVector3D(fwd.x(), 0.0f, fwd.z());
    if (flat.lengthSquared() < 1e-8f)
        flat = QVector3D(qSin(m_yaw), 0.0f, qCos(m_yaw));
    flat.normalize();
    const QVector3D right = QVector3D::crossProduct(flat, QVector3D(0, 1, 0)).normalized();

    QVector3D move(0, 0, 0);
    if (m_heldKeys.contains(Qt::Key_W))
        move += flat * speed;
    if (m_heldKeys.contains(Qt::Key_S))
        move -= flat * speed;
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
    if (n == QLatin1String("air"))
        return QColor(0, 0, 0, 0);
    if (n.contains(QStringLiteral("water"), Qt::CaseInsensitive))
        return QColor(38, 96, 200, 235);
    if (n.contains(QStringLiteral("lava")))
        return QColor(230, 110, 30);
    if (n.contains(QStringLiteral("glass")) || n.contains(QStringLiteral("sea_lantern"))
            || n.contains(QStringLiteral("lantern")))
        return QColor(190, 230, 255, 200);
    if (n.contains(QStringLiteral("wood")) || n.contains(QStringLiteral("log")) ||
        n.contains(QStringLiteral("planks")) || n.contains(QStringLiteral("bamboo")) ||
        n.contains(QStringLiteral("door")) || n.contains(QStringLiteral("trapdoor")))
        return QColor(150, 110, 60);
    if (n.contains(QStringLiteral("fence_gate")))
        return QColor(140, 100, 55);
    if (n.contains(QStringLiteral("fence")))
        return QColor(160, 130, 80);
    if (n.contains(QStringLiteral("cobblestone_wall")) || n.contains(QStringLiteral("mossy_cobblestone_wall")))
        return QColor(130, 130, 130);
    if (n.contains(QStringLiteral("brick_wall")))
        return QColor(170, 100, 80);
    if (n.contains(QStringLiteral("stone_brick_wall")) || n.contains(QStringLiteral("mossy_stone_brick_wall")))
        return QColor(140, 140, 140);
    if (n.contains(QStringLiteral("nether_brick_wall")))
        return QColor(80, 40, 50);
    if (n.contains(QStringLiteral("prismarine_wall")))
        return QColor(100, 140, 130);
    if (n.contains(QStringLiteral("red_sandstone_wall")))
        return QColor(190, 120, 70);
    if (n.contains(QStringLiteral("sandstone_wall")))
        return QColor(210, 190, 140);
    if (n.contains(QStringLiteral("end_stone_brick_wall")))
        return QColor(220, 210, 150);
    if (n.contains(QStringLiteral("blackstone_wall")))
        return QColor(50, 45, 50);
    if (n.contains(QStringLiteral("polished_blackstone_wall")) || n.contains(QStringLiteral("polished_blackstone_brick_wall")))
        return QColor(45, 40, 45);
    if (n.contains(QStringLiteral("wall")))
        return QColor(150, 150, 150);
    if (n.contains(QStringLiteral("stone")) || n.contains(QStringLiteral("brick")) ||
        n.contains(QStringLiteral("smooth")) || n.contains(QStringLiteral("andesite")) ||
        n.contains(QStringLiteral("diorite")) || n.contains(QStringLiteral("granite")) ||
        n.contains(QStringLiteral("cobble")))
        return QColor(150, 150, 150);
    if (n.contains(QStringLiteral("dirt")) || n.contains(QStringLiteral("sand")) ||
        n.contains(QStringLiteral("gravel")) || n.contains(QStringLiteral("clay")))
        return QColor(160, 130, 70);
    if (n.contains(QStringLiteral("grass")) || n.contains(QStringLiteral("leaf")))
        return QColor(90, 160, 70);
    if (n.contains(QStringLiteral("redstone")) || n.contains(QStringLiteral("torch")) ||
        n.contains(QStringLiteral("lamp")) || n.contains(QStringLiteral("lever")) ||
        n.contains(QStringLiteral("button")))
        return QColor(220, 60, 40);
    if (n.contains(QStringLiteral("chest")) || n.contains(QStringLiteral("barrel")) ||
        n.contains(QStringLiteral("furnace")) || n.contains(QStringLiteral("cart")))
        return QColor(120, 90, 50);
    if (n.contains(QStringLiteral("bed")))
        return QColor(240, 230, 210);
    if (n.contains(QStringLiteral("carpet")) || n.contains(QStringLiteral("wool")))
        return QColor(220, 180, 200);
    if (n.contains(QStringLiteral("slab")) || n.contains(QStringLiteral("stairs")))
        return QColor(180, 180, 190);
    if (n.contains(QStringLiteral("candle")))
        return QColor(245, 225, 180);
    if (n.contains(QStringLiteral("gilded")) || n.contains(QStringLiteral("gold")))
        return QColor(230, 200, 60);
    if (n.contains(QStringLiteral("iron")) || n.contains(QStringLiteral("copper")))
        return QColor(170, 170, 180);
    if (n.contains(QStringLiteral("coal")) || n.contains(QStringLiteral("obsidian")))
        return QColor(40, 40, 45);
    if (n.contains(QStringLiteral("diamond")))
        return QColor(80, 220, 200);
    return QColor(90, 130, 220);
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
    // 调色板只会追加（setBlock 追加新状态），已解析过的条目可长期复用
    if (m_paletteCacheValid && m_paletteCache.size() == ps)
        return;

    const bool texAvail = m_loader && m_loader->isValid();
    const int from = qMin(m_paletteCache.size(), ps);
    m_paletteCache.resize(ps);

    for (int p = from; p < ps; ++p)
    {
        const SchematicBlockState &st = m_doc->paletteEntry(p);
        PaletteRender pr;
        pr.isAir = st.isAir();
        pr.color = colorForName(st.name);

        if (pr.isAir)
        {
            pr.opaque = false;
            pr.color.setAlpha(51);          // 空气轮廓：淡淡一层
            RenderBox box;                  // 默认整格
            box.fullCube = true;
            pr.boxes.append(box);
            pr.fullCube = true;
            pr.opaque = false;
            m_paletteCache[p] = pr;
            continue;
        }

        // 真实形状盒（含属性状态字符串）；无形状时回退为整格立方体
        QVector<ShapeBox> shapes;
        const BlockShapes *bs = shapesFor(st.toString());
        if (bs && !bs->boxes.isEmpty())
            shapes = bs->boxes;
        const bool fromModel = !shapes.isEmpty();
        if (shapes.isEmpty())
            shapes.append(ShapeBox{});      // 默认全格
        // 液体没有模型（原版用专用渲染器画）：按 14/16 的液面高度显示，
        // 否则水面会顶到整格高，和岸边/水下的方块拼起来像一堵蓝墙
        if (!fromModel
            && (st.name.contains(QLatin1String("water")) || st.name.contains(QLatin1String("lava"))))
            shapes[0].to[1] = 0.875f;

        int fallbackTile = -1;
        if (texAvail)
            m_loader->textureFor(st.name, fallbackTile);

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
                RenderFace &rf = rb.face[f];
                if (sface)
                {
                    texKey = sface->texture;
                    rf.cull = sface->canCull;         // 只有模型声明 cullface 的面才会被邻居剔除
                    rf.shadeOff = !sface->shaded;     // 火把/植物/梯子：不吃方向光
                    rf.noAo = !sface->aoOn;           // 十字模型关掉角落阴影
                    if (sface->tintIndex >= 0)        // 草/树叶/水按种类着色
                    {
                        const QColor tc = tintColorFor(st.name);
                        rf.tint[0] = uchar(tc.red());
                        rf.tint[1] = uchar(tc.green());
                        rf.tint[2] = uchar(tc.blue());
                    }
                }
                if (sface)
                    texKey = sface->texture;
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
    const bool showAir = m_showAir;
    const bool allowAll = (mode == LayerMode::All) || (filter < 0);

    for (int i = 0; i < vol; ++i)
    {
        const int y = i / layer;
        bool visible = true;
        if (!allowAll)
            visible = (mode == LayerMode::Single) ? (y == filter) : (y <= filter);
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

    // 两遍生成：第一遍只写不透明方块，第二遍写半透明方块（玻璃/水/树叶/空气轮廓）。
    // 绘制时前段正常写深度、后段关掉深度写入，透过玻璃就不会把后面画的方块「挖空」。
    for (int pass = 0; pass < 2; ++pass)
    {
    const bool wantTranslucent = (pass == 1);
    if (pass == 1)
        m_opaqueVertexCount = int(usedVerts);

    for (int y = 0; y < sy; ++y)
    {
        if (!allowAll)
        {
            if (mode == LayerMode::Single && y != filter)
                continue;
            if (mode == LayerMode::Below && y > filter)
                continue;
        }
        const uchar dimByte = (mode == LayerMode::Below && y < filter) ? 255 : 0;

        for (int z = 0; z < sz; ++z)
        {
            for (int x = 0; x < sx; ++x)
            {
                const int i = y * layer + z * sx + x;
                const int p = m_doc->paletteIndexAt(i);
                if (p < 0 || p >= m_paletteCache.size())
                    continue;
                const PaletteRender &pr = m_paletteCache.at(p);
                if (pr.isAir && !showAir)
                    continue;
                // 半透明方块放到第二遍：否则它先画出来会挡住后面才画的实心方块
                if ((!pr.opaque) != wantTranslucent)
                    continue;

                // 逐格微小明度扰动：让纯色（无材质）渲染也有方块质感，避免大面积塑料感。
                // 用坐标哈希保证同一格子每次重建结果一致，不会闪烁。
                quint32 hsh = quint32(i) * 2654435761u;
                hsh ^= hsh >> 13;
                const float jitter = 0.94f + float(hsh & 15) * 0.008f;   // 0.94 ~ 1.06

                uchar cr = toByte(pr.color.redF());
                uchar cg = toByte(pr.color.greenF());
                uchar cb = toByte(pr.color.blueF());
                const uchar ca = uchar(pr.color.alpha());
                if (!pr.tinted)
                {
                    cr = qBound<uchar>(0, uchar(cr * jitter), 255);
                    cg = qBound<uchar>(0, uchar(cg * jitter), 255);
                    cb = qBound<uchar>(0, uchar(cb * jitter), 255);
                }

                for (const RenderBox &box : pr.boxes)
                {
                    for (int f = 0; f < 6; ++f)
                    {
                        const int axis = kFaceAxis[f];
                        const int sign = kFaceSign[f];
                        const RenderFace &rf = box.face[f];

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
                        const float *uvr = rf.uv;
                        const int ta = kFaceTangent[f][0];
                        const int tb = kFaceTangent[f][1];

                        Vtx q[4];
                        float ao[4];
                        for (int c = 0; c < 4; ++c)
                        {
                            const float *corner = kFaceCorner[f][c];
                            float lx = box.from[0] + (box.to[0] - box.from[0]) * corner[0];
                            float ly = box.from[1] + (box.to[1] - box.from[1]) * corner[1];
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
                            const float aoF = 1.0f - 0.18f * aoSum;
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
                            else if (f == 2)           { fu = 1.0f - kx;   fv = kz; }
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
                        if (rf.shadeOff)
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

// ----------------------------------------------------------------- GL 初始化

void VoxelViewWidget::initializeGL()
{
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

    // 叠加通道：选中线框 / 悬停高亮（自带颜色，线段与三角形共用）
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
out vec4 fragColor;
void main() { fragColor = vColor; }
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

    m_glReady = true;
    m_uploadDirty = true;
    m_selUploadDirty = true;
    m_hoverUploadDirty = true;
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

void VoxelViewWidget::rebuildSelectionGeometry()
{
    m_selData.clear();
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
        const float fogEnd = qMax(64.0f, m_orbitDist * 2.2f);
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

    // ---- 叠加通道 ----
    if (m_ovProgram.isLinked())
    {
        if (m_hoverUploadDirty)
            uploadHoverGeometry();
        if (m_selUploadDirty)
            uploadSelectionGeometry();

        if (m_hoverData.size() > 0 && m_hoverVao.isCreated())
        {
            m_ovProgram.bind();
            m_ovProgram.setUniformValue("uProj", proj);
            m_ovProgram.setUniformValue("uView", view);
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
            glDepthFunc(GL_LEQUAL);
            glLineWidth(1.8f);
            m_selVao.bind();
            glDrawArrays(GL_LINES, 0, m_selVertexCount);
            m_selVao.release();
            glLineWidth(1.0f);
            glDepthFunc(GL_LESS);
            m_ovProgram.release();
        }
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
    m_lastPos = e->pos();
    m_pressPos = e->pos();
    m_dragMoved = false;
    m_selectPending = (m_leftAction == LeftAction::Select) && left;
    setCursor(Qt::ClosedHandCursor);
}

void VoxelViewWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging)
    {
        const int dx = e->pos().x() - m_lastPos.x();
        const int dy = e->pos().y() - m_lastPos.y();
        if ((e->pos() - m_pressPos).manhattanLength() > 4)
            m_dragMoved = true;
        lookBy(dx * 0.008f, dy * 0.008f);
        m_lastPos = e->pos();
    }
    else
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
    }
}

void VoxelViewWidget::mouseReleaseEvent(QMouseEvent *e)
{
    const bool selecting = m_selectPending && !m_dragMoved && e->button() == Qt::LeftButton;
    // 拖动过 = 转向视角，不执行编辑；否则执行按下时记下的破坏 / 放置
    const int pendingEdit = (m_dragMoved || selecting) ? EditNone : m_pendingEdit;
    const int px = m_pendingX, py = m_pendingY, pz = m_pendingZ;

    m_dragging = false;
    m_selectPending = false;
    m_pendingEdit = EditNone;

    unsetCursor();
    if (m_leftAction == LeftAction::Select)
        setCursor(Qt::PointingHandCursor);

    if (selecting)
    {
        int x, y, z;
        if (pickRay(e->pos(), x, y, z, /*skipAir=*/true))
            emit selectRequested(x, y, z);
        return;
    }
    if (pendingEdit == EditBreak)
        emit breakRequested(px, py, pz);
    else if (pendingEdit == EditPlace)
        emit placeRequested(px, py, pz);
}

void VoxelViewWidget::wheelEvent(QWheelEvent *e)
{
    // 围绕观察中心缩放（比「沿视线平移」更容易控制远近）
    const float steps = e->angleDelta().y() / 120.0f;
    m_orbitDist = qBound(1.2f, m_orbitDist * qPow(0.86f, steps), 6000.0f);
    m_eyeValid = true;
    update();
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
            m_heldKeys.insert(e->key());
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
            m_heldKeys.remove(e->key());
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
    if (m_bgProgram.isLinked())
        m_bgProgram.removeAllShaders();
    if (m_bgVao.isCreated())
        m_bgVao.destroy();
    m_selVertexCount = 0;
    m_hoverTriVerts = 0;
    m_hoverLineVerts = 0;
    m_bgReady = false;
    m_glReady = false;
    doneCurrent();
}
