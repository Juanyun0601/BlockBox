/**
 * @file   VoxelViewWidget.cpp
 * @brief  3D 体素方块视图控件实现（OpenGL）
 * @author BlockBox Team
 * @date   2026-08-18
 */

#include "VoxelViewWidget.h"

#include <QDebug>
#include <QWheelEvent>
#include <QtMath>
#include <climits>

namespace {

// 单位立方体 8 顶点（min 角在原点）
// 0=(0,0,0) 1=(1,0,0) 2=(0,1,0) 3=(1,1,0) 4=(0,0,1) 5=(1,0,1) 6=(0,1,1) 7=(1,1,1)
static const float kCorner[8][3] = {
    {0,0,0},{1,0,0},{0,1,0},{1,1,0},
    {0,0,1},{1,0,1},{0,1,1},{1,1,1}
};

// 6 面，每面 4 个角点索引
// 顺序：+X, -X, +Y, -Y, +Z, -Z
static const int kFace[6][4] = {
    {1,5,7,3}, // +X (x=1)
    {0,4,6,2}, // -X (x=0)
    {3,7,6,2}, // +Y (y=1)
    {0,1,5,4}, // -Y (y=0)
    {4,5,7,6}, // +Z (z=1)
    {0,1,3,2}, // -Z (z=0)
};

// 每面光照（抵消背面不可见，故两侧一致）
static const float kFaceLight[6] = { 1.0f, 0.55f, 0.5f, 0.85f, 0.9f, 0.65f };

// slab 法射线-轴对齐盒相交；返回命中则 tmin/tmax 有效
bool slabHit(const QVector3D &ro, const QVector3D &rd, const QVector3D &invDir,
             const QVector3D &bmin, const QVector3D &bmax, float &tmin, float &tmax)
{
    tmin = -1e30f; tmax = 1e30f;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float o = ro[axis], d = rd[axis], mn = bmin[axis], mx = bmax[axis];
        if (qAbs(d) < 1e-8f)
        {
            if (o < mn || o > mx)
                return false;
        }
        else
        {
            const float inv = 1.0f / d;
            float t1 = (mn - o) * inv;
            float t2 = (mx - o) * inv;
            if (t1 > t2) { const float tmp = t1; t1 = t2; t2 = tmp; }
            tmin = qMax(tmin, t1);
            tmax = qMin(tmax, t2);
            if (tmin > tmax)
                return false;
        }
    }
    Q_UNUSED(invDir);
    return true;
}

int quadTriVertex(int face, int tri, int vert)
{
    // 每面 2 三角形，每三角形 3 顶点
    static const int triOrder[2][3] = { {0,1,2}, {0,2,3} };
    const int p = triOrder[tri][vert];
    return kFace[face][p];
}

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

// ShapeFace.dir -> 相应面板索引（0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z）
int shapeFaceToIndex(const QString &dir)
{
    if (dir == QLatin1String("east")) return 0;
    if (dir == QLatin1String("west")) return 1;
    if (dir == QLatin1String("up"))   return 2;
    if (dir == QLatin1String("down")) return 3;
    if (dir == QLatin1String("south")) return 4;
    return 5; // north
}

} // namespace

VoxelViewWidget::VoxelViewWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_doc(nullptr)
{
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
}

VoxelViewWidget::~VoxelViewWidget()
{
    destroyGL();
}

void VoxelViewWidget::setDocument(SchematicDocument *doc)
{
    m_doc = doc;
    m_dirty = true;
    rebuild();
    update();
}

void VoxelViewWidget::setTextureLoader(InstanceTextureLoader *loader)
{
    m_loader = loader;
    m_dirty = true;
    rebuild();   // 重算每方块贴图/UV（纯 CPU）
    update();
}

void VoxelViewWidget::setShapeLoader(BlockShapeLoader *loader)
{
    m_shapeLoader = loader;
    m_shapeCache.clear();
    m_dirty = true;
    rebuild();   // 重算每方块真实形状（纯 CPU）
    update();
}

void VoxelViewWidget::setShowAir(bool show)
{
    m_showAir = show;
    m_dirty = true;
    rebuild();
    update();
}

void VoxelViewWidget::setLayerFilter(int layer)
{
    m_layerFilter = layer;
    m_dirty = true;
    rebuild();
    update();
}

void VoxelViewWidget::setLayerMode(LayerMode mode)
{
    m_layerMode = mode;
    m_dirty = true;
    rebuild();
    update();
}

void VoxelViewWidget::setHover(int x, int y, int z)
{
    m_hoverX = x; m_hoverY = y; m_hoverZ = z;
    m_dirty = true;
    update();
    emit hoverChanged(x, y, z);
}

void VoxelViewWidget::resetView()
{
    m_theta = 0.6f;
    m_phi = 0.5f;
    if (m_doc)
        m_radius = static_cast<float>(qMax(6, m_doc->sizeX() / 2 + m_doc->sizeZ() / 2)) + 6.0f;
    else
        m_radius = 12.0f;
    update();
}

QColor VoxelViewWidget::colorForName(const QString &blockName)
{
    QString n = blockName;
    int slash = n.indexOf(QLatin1Char(':'));
    if (slash >= 0)
        n = n.mid(slash + 1);
    if (n == QLatin1String("air"))
        return QColor(0, 0, 0, 0);
    if (n.contains(QStringLiteral("water"), Qt::CaseInsensitive))
        return QColor(30, 90, 200);
    if (n.contains(QStringLiteral("glass")) || n.contains(QStringLiteral("sea_lantern"))
            || n.contains(QStringLiteral("lantern")))
        return QColor(190, 230, 255);
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

bool VoxelViewWidget::rayPick(const QPoint &pos, int &x, int &y, int &z) const
{
    return pickRay(pos, x, y, z);
}

void VoxelViewWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    // 关闭背面剔除（保证任意绕序均可见）
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.12f, 0.13f, 0.17f, 1.0f);

    const char *vsrc = R"(
#version 330 core
layout(location=0) in vec3 aCorner;
layout(location=1) in vec4 aColor;
layout(location=2) in float aLight;
layout(location=3) in float aHover;
layout(location=4) in float aDim;
layout(location=5) in vec2 aUV;
layout(location=6) in float aUseTex;
uniform mat4 uProj;
uniform mat4 uView;
out vec2 vUv;
out float vUseTex;
out float vLight;
out vec4 vColor;
out float vHover;
out float vDim;
void main() {
    gl_Position = uProj * uView * vec4(aCorner, 1.0);
    vUv = aUV;
    vUseTex = aUseTex;
    vLight = aLight;
    vColor = aColor;
    vHover = aHover;
    vDim = aDim;
}
)";
    const char *fsrc = R"(
#version 330 core
in vec2 vUv;
in float vUseTex;
in float vLight;
in vec4 vColor;
in float vHover;
in float vDim;
uniform sampler2D uAtlas;
out vec4 fragColor;
void main() {
    float blend = mix(1.0, 1.5, vHover);
    float darken = mix(1.0, 0.35, vDim);
    vec4 tex = texture(uAtlas, vUv);
    vec3 base = mix(vColor.rgb, tex.rgb, vUseTex);
    fragColor = vec4(base * vLight * blend * darken, vColor.a);
}
)";

    m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vsrc);
    m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fsrc);
    m_program.link();

    m_vao.create();
    m_vbo.create();
    m_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_glReady = true;

    // 若在显示前已构建好顶点数据（loadProjection 阶段），在此初传一次
    if (!m_vertexData.isEmpty())
        uploadGeometry();
    uploadAtlasTexture();
}

void VoxelViewWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void VoxelViewWidget::buildGeometry()
{
    m_cubes.clear();

    if (!m_doc)
    {
        m_vertexCount = 0;
        return;
    }

    const int sx = m_doc->sizeX(), sy = m_doc->sizeY(), sz = m_doc->sizeZ();
    const int layer = m_doc->layerSize();

    QVector<int> idxToCube;
    idxToCube.fill(-1, m_doc->volume());

    for (int y = 0; y < sy; ++y)
        for (int z = 0; z < sz; ++z)
            for (int x = 0; x < sx; ++x)
            {
                const int i = y * layer + z * sx + x;
                const SchematicBlockState st = m_doc->blockAt(i);

                // 图层过滤（模式相关）
                bool visible = true;
                float dim = 0.0f;
                if (m_layerMode == LayerMode::Single)
                {
                    visible = (y == m_layerFilter);
                }
                else if (m_layerMode == LayerMode::Below)
                {
                    if (y > m_layerFilter)
                        continue;             // 隐藏当前层以上的方块
                    dim = (y < m_layerFilter) ? 1.0f : 0.0f; // 当前层以下变暗
                }
                // LayerMode::All：全部可见
                if (!visible)
                    continue;

                const bool isAir = st.isAir();
                if (isAir && !m_showAir)
                    continue;
                const QColor col = colorForName(st.name);
                const float baseR = col.redF(), baseG = col.greenF(), baseB = col.blueF();

                // 真实形状盒列表（含属性状态字符串）；无形状时回退为整格立方体
                QVector<ShapeBox> boxes;
                if (!isAir)
                {
                    const BlockShapes *shapes = shapesFor(st.toString());
                    if (shapes && !shapes->boxes.isEmpty())
                        boxes = shapes->boxes;
                }
                if (boxes.isEmpty())
                    boxes.append(ShapeBox{});   // 默认全格

                const float cxp = static_cast<float>(x);
                const float cyp = static_cast<float>(y);
                const float czp = static_cast<float>(z);
                const bool texAvail = m_loader && m_loader->isValid();
                int fallbackTile = -1;
                if (texAvail)
                    m_loader->textureFor(st.name, fallbackTile);

                for (const ShapeBox &box : boxes)
                {
                    CubeVertex cv;
                    cv.x = cxp + box.from[0];
                    cv.y = cyp + box.from[1];
                    cv.z = czp + box.from[2];
                    cv.sx = qMax(0.0001f, box.to[0] - box.from[0]);
                    cv.sy = qMax(0.0001f, box.to[1] - box.from[1]);
                    cv.sz = qMax(0.0001f, box.to[2] - box.from[2]);
                    cv.r = baseR; cv.g = baseG; cv.b = baseB;
                    cv.a = isAir ? 0.2f : 1.0f;
                    cv.hoverR = cv.r; cv.hoverG = cv.g; cv.hoverB = cv.b;
                    cv.isHover = 0.0f;
                    cv.dim = dim;
                    cv.cellId = i;
                    cv.rotated = box.rotated;
                    cv.rotAxis = box.rotAxis;
                    cv.rotAngle = box.rotAngle;
                    cv.rotOx = cxp + box.rotOrigin[0];
                    cv.rotOy = cyp + box.rotOrigin[1];
                    cv.rotOz = czp + box.rotOrigin[2];

                    // 每面贴图/UV（优先该面模型纹理，其次方块基础贴图，最后颜色回退）
                    for (int f = 0; f < 6; ++f)
                        cv.faceUse[f] = 0.0f;
                    for (int f = 0; f < 6; ++f)
                    {
                        QString texKey;
                        ShapeFace sface;
                        bool hasSface = false;
                        for (const ShapeFace &sf : box.faces)
                        {
                            if (shapeFaceToIndex(sf.dir) == f)
                            {
                                hasSface = true;
                                sface = sf;
                                break;
                            }
                        }
                        if (hasSface)
                            texKey = sface.texture;
                        int tile = -1;
                        bool ok = false;
                        if (texAvail)
                        {
                            if (hasSface && !texKey.isEmpty())
                                ok = m_loader->tileForKey(texKey, tile);
                            if (!ok && fallbackTile >= 0)
                            {
                                tile = fallbackTile;
                                ok = true;
                            }
                        }
                        if (ok)
                        {
                            cv.faceUse[f] = 1.0f;
                            float tu0, tv0, tu1, tv1;
                            m_loader->tileUv(tile, tu0, tv0, tu1, tv1);
                            // 面内子区域（模型 uv 为 0..16 像素；缺省时整张贴图）
                            float u0s = 0.0f, v0s = 0.0f, u1s = 1.0f, v1s = 1.0f;
                            if (hasSface && sface.hasUv)
                            {
                                u0s = sface.uv[0] / 16.0f;
                                u1s = sface.uv[2] / 16.0f;
                                // 面顶边（fv=0）对应贴图上侧（较小的 v）
                                v0s = qMin(sface.uv[1], sface.uv[3]) / 16.0f;
                                v1s = qMax(sface.uv[1], sface.uv[3]) / 16.0f;
                            }
                            cv.uv[f][0] = tu0 + u0s * (tu1 - tu0);
                            cv.uv[f][1] = tv0 + v0s * (tv1 - tv0);
                            cv.uv[f][2] = tu0 + u1s * (tu1 - tu0);
                            cv.uv[f][3] = tv0 + v1s * (tv1 - tv0);
                        }
                        else
                        {
                            cv.uv[f][0] = cv.uv[f][1] = cv.uv[f][2] = cv.uv[f][3] = 0.0f;
                        }
                    }
                    m_cubes.append(cv);
                }
                idxToCube[i] = m_cubes.size() - static_cast<int>(boxes.size());
            }

    const int ox = m_doc->originX(), oy = m_doc->originY(), oz = m_doc->originZ();
    if (m_doc->contains(m_hoverX, m_hoverY, m_hoverZ))
    {
        const int hx = m_hoverX - ox, hy = m_hoverY - oy, hz = m_hoverZ - oz;
        const int hi = hy * layer + hz * sx + hx;
        if (hi >= 0 && hi < idxToCube.size())
        {
            const int start = idxToCube.at(hi);
            // 同格的多个形状盒是连续记录的，整格一起高亮
            for (int s = start; s < m_cubes.size() && m_cubes.at(s).cellId == hi; ++s)
            {
                m_cubes[s].isHover = 1.0f;
                m_cubes[s].hoverR = 1.0f;
                m_cubes[s].hoverG = 0.9f;
                m_cubes[s].hoverB = 0.3f;
            }
        }
    }

    m_vertexCount = 0;
}

void VoxelViewWidget::computeVertexData()
{
    // 仅 CPU：把方块几何摊平成顶点数据，供 paintGL 时上传
    if (m_cubes.isEmpty())
    {
        m_vertexData.clear();
        m_vertexCount = 0;
        return;
    }

    struct VAttr { float corner[3]; float color[4]; float light; float hover; float dim; float uv[2]; float useTex; };
    const int vertsPerCube = 6 * 2 * 3;
    QVector<VAttr> data(m_cubes.size() * vertsPerCube);
    int out = 0;
    for (int c = 0; c < m_cubes.size(); ++c)
    {
        const CubeVertex &cv = m_cubes.at(c);
        for (int f = 0; f < 6; ++f)
            for (int t = 0; t < 2; ++t)
                for (int v = 0; v < 3; ++v)
                {
                    const int ci = quadTriVertex(f, t, v);
                    VAttr &a = data[out++];
                    float px = cv.x + kCorner[ci][0] * cv.sx;
                    float py = cv.y + kCorner[ci][1] * cv.sy;
                    float pz = cv.z + kCorner[ci][2] * cv.sz;
                    if (cv.rotated)
                        rotatePoint3(px, py, pz, cv.rotOx, cv.rotOy, cv.rotOz,
                                     cv.rotAngle, cv.rotAxis);
                    a.corner[0] = px;
                    a.corner[1] = py;
                    a.corner[2] = pz;
                    a.color[0] = cv.r; a.color[1] = cv.g; a.color[2] = cv.b; a.color[3] = cv.a;
                    a.light = kFaceLight[f];
                    a.hover = cv.isHover;
                    a.dim = cv.dim;
                    a.useTex = cv.faceUse[f];
                    if (cv.faceUse[f] > 0.5f)
                    {
                        // 依面的坐标轴确定面上的 (u,v)，保证子区域（如活板门 3px 边条）裁切正确
                        float fu, fv;
                        const float kx = kCorner[ci][0], ky = kCorner[ci][1], kz = kCorner[ci][2];
                        if (f == 0 || f == 1)
                        {
                            fu = kz; fv = 1.0f - ky;      // ±X
                        }
                        else if (f == 2)
                        {
                            fu = 1.0f - kx; fv = kz;      // +Y
                        }
                        else if (f == 3)
                        {
                            fu = kx; fv = 1.0f - kz;      // -Y
                        }
                        else
                        {
                            fu = kx; fv = 1.0f - ky;      // ±Z
                        }
                        a.uv[0] = cv.uv[f][0] + fu * (cv.uv[f][2] - cv.uv[f][0]);
                        a.uv[1] = cv.uv[f][1] + fv * (cv.uv[f][3] - cv.uv[f][1]);
                    }
                    else
                    {
                        a.uv[0] = 0.0f;
                        a.uv[1] = 0.0f;
                    }
                }
    }
    m_vertexCount = data.size();
    m_vertexData = QByteArray(reinterpret_cast<const char *>(data.constData()),
                              data.size() * sizeof(VAttr));
}

void VoxelViewWidget::uploadGeometry()
{
    // 须在 GL 上下文有效时调用（initializeGL / paintGL）
    if (!m_glReady || m_vertexData.isEmpty())
    {
        m_vertexCount = 0;
        return;
    }

    const int stride = static_cast<int>(sizeof(float) * (3 + 4 + 1 + 1 + 1 + 2 + 1));
    m_vbo.bind();
    m_vbo.allocate(m_vertexData.constData(), m_vertexData.size());

    m_vao.bind();
    int off = 0;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(off)); off += 3*4;
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(off)); off += 4*4;
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(off)); off += 4;
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(off)); off += 4;
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(off)); off += 4;
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(off)); off += 2*4;
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(off)); off += 4;
    m_vao.release();
}

void VoxelViewWidget::uploadAtlasTexture()
{
    // 须在 GL 上下文有效时调用
    if (!m_glReady)
        return;

    if (!m_loader || !m_loader->isValid())
    {
        // 无贴图：清掉图集纹理，回退到纯色
        if (m_atlasTex)
        {
            delete m_atlasTex;
            m_atlasTex = nullptr;
        }
        return;
    }

    const QImage atlas = m_loader->atlasImage();
    if (atlas.isNull())
        return;

    if (!m_atlasTex)
        m_atlasTex = new QOpenGLTexture(atlas, QOpenGLTexture::DontGenerateMipMaps);
    else
        m_atlasTex->setData(atlas, QOpenGLTexture::DontGenerateMipMaps);

    m_atlasTex->setMinificationFilter(QOpenGLTexture::Nearest);
    m_atlasTex->setMagnificationFilter(QOpenGLTexture::Nearest);
    m_atlasTex->setWrapMode(QOpenGLTexture::ClampToEdge);
}

void VoxelViewWidget::rebuild()
{
    // 纯 CPU：重建方块几何 + 顶点数据，随后随 update() 在 paintGL 上传。
    // 绝不在控件显示前调用 GL（可安全于 loadProjection 阶段调用，避免闪退）。
    buildGeometry();
    computeVertexData();
    m_dirty = true;
    update();
}

void VoxelViewWidget::paintGL()
{
    glClearColor(0.12f, 0.13f, 0.17f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_glReady || !m_doc || !m_program.isLinked())
        return;

    // 顶点数据在 loadProjection 阶段即已算出，若尚未上传则上传
    if (m_dirty || m_vertexCount == 0)
    {
        uploadGeometry();
        m_dirty = false;
    }
    if (m_vertexCount <= 0)
        return;

    const float aspect = height() > 0 ? static_cast<float>(width()) / height() : 1.0f;
    QMatrix4x4 proj;
    proj.perspective(45.0f, aspect, 0.1f, 1000.0f);

    const QVector3D center(m_doc->sizeX()/2.0f, m_doc->sizeY()/2.0f, m_doc->sizeZ()/2.0f);
    QMatrix4x4 view;
    const QVector3D eye = center + m_radius * QVector3D(
        qCos(m_phi)*qSin(m_theta), qSin(m_phi), qCos(m_phi)*qCos(m_theta));
    view.lookAt(eye, center, QVector3D(0, 1, 0));

    m_program.bind();

    // 绑定材质图集（无贴图时用 1×1 白色兜底）
    uploadAtlasTexture();
    if (!m_atlasTex)
    {
        static QImage white(1, 1, QImage::Format_RGBA8888);
        if (white.isNull() || white.pixelColor(0, 0) != Qt::white)
            white.fill(Qt::white);
        m_atlasTex = new QOpenGLTexture(white, QOpenGLTexture::DontGenerateMipMaps);
    }
    m_atlasTex->bind(0);
    m_program.setUniformValue("uAtlas", 0);

    m_program.setUniformValue("uProj", proj);
    m_program.setUniformValue("uView", view);

    m_vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    m_vao.release();
    m_atlasTex->release(0);
    m_program.release();
    m_dirty = false;
}

void VoxelViewWidget::orbitBy(float dTheta, float dPhi)
{
    m_theta += dTheta;
    m_phi = qBound(-1.2f, m_phi + dPhi, 1.2f);
    update();
}

bool VoxelViewWidget::pickRay(const QPoint &pos, int &bx, int &by, int &bz) const
{
    if (!m_doc || height() <= 0)
        return false;

    const float aspect = static_cast<float>(width()) / height();
    QMatrix4x4 proj;
    proj.perspective(45.0f, aspect, 0.1f, 1000.0f);

    const QVector3D center(m_doc->sizeX()/2.0f, m_doc->sizeY()/2.0f, m_doc->sizeZ()/2.0f);
    QVector3D eye = center + m_radius * QVector3D(
        qCos(m_phi)*qSin(m_theta), qSin(m_phi), qCos(m_phi)*qCos(m_theta));
    QMatrix4x4 view;
    view.lookAt(eye, center, QVector3D(0, 1, 0));

    QMatrix4x4 invVP = (proj * view).inverted();
    if (invVP.isIdentity())
        return false;

    const float ndcX = (2.0f * pos.x()) / width() - 1.0f;
    const float ndcY = 1.0f - (2.0f * pos.y()) / height();

    QVector4D near4 = invVP * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D far4  = invVP * QVector4D(ndcX, ndcY, 1.0f, 1.0f);
    if (qFuzzyIsNull(near4.w()) || qFuzzyIsNull(far4.w()))
        return false;
    const QVector3D ro(near4.x()/near4.w(), near4.y()/near4.w(), near4.z()/near4.w());
    const QVector3D rf(far4.x()/far4.w(), far4.y()/far4.w(), far4.z()/far4.w());
    QVector3D rd = rf - ro;
    if (rd.isNull())
        return false;
    rd.normalize();

    const int sx = m_doc->sizeX(), sy = m_doc->sizeY(), sz = m_doc->sizeZ();
    const int layer = m_doc->layerSize();
    const int ox = m_doc->originX(), oy = m_doc->originY(), oz = m_doc->originZ();

    float bestT = 1e30f;
    int bestX = -1, bestY = -1, bestZ = -1;
    for (int y = 0; y < sy; ++y)
        for (int z = 0; z < sz; ++z)
            for (int x = 0; x < sx; ++x)
            {
                const int i = y * layer + z * sx + x;
                const SchematicBlockState st = m_doc->blockAt(i);
                if (st.isAir() && !m_showAir)
                    continue;

                float tmin, tmax;
                bool hit = false;
                QVector<ShapeBox> boxes;
                if (!st.isAir())
                {
                    const BlockShapes *shapes = shapesFor(st.toString());
                    if (shapes && !shapes->boxes.isEmpty())
                        boxes = shapes->boxes;
                }
                if (boxes.isEmpty())
                {
                    // 整格
                    hit = slabHit(ro, rd, QVector3D(), QVector3D(x,y,z),
                                  QVector3D(x+1,y+1,z+1), tmin, tmax);
                }
                else
                {
                    // 真实形状盒（含旋转后的包围盒）
                    for (const ShapeBox &b : boxes)
                    {
                        float bx0, by0, bz0, bx1, by1, bz1;
                        boxWorldBounds(b, x, y, z, bx0, by0, bz0, bx1, by1, bz1);
                        float t0, t1;
                        if (slabHit(ro, rd, QVector3D(),
                                    QVector3D(bx0, by0, bz0), QVector3D(bx1, by1, bz1), t0, t1))
                        {
                            const float ht = qMax(0.0f, t0);
                            if (!hit || ht < tmin)
                                tmin = ht;
                            hit = true;
                        }
                    }
                }
                if (hit)
                {
                    const float t = qMax(0.0f, tmin);
                    if (t < bestT && t < 1e29f)
                    {
                        bestT = t;
                        bestX = x + ox; bestY = y + oy; bestZ = z + oz;
                    }
                }
            }

    if (bestX < 0)
        return false;
    bx = bestX; by = bestY; bz = bestZ;
    return true;
}

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

void VoxelViewWidget::boxWorldBounds(const ShapeBox &box, int x, int y, int z,
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
        px += x; py += y; pz += z;
        x0 = qMin(x0, px); y0 = qMin(y0, py); z0 = qMin(z0, pz);
        x1 = qMax(x1, px); y1 = qMax(y1, py); z1 = qMax(z1, pz);
    }
}

void VoxelViewWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton)
    {
        int x,y,z;
        if (pickRay(e->pos(), x, y, z))
        {
            emit breakRequested(x, y, z);
            return;
        }
    }
    else if ((e->button() == Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier))
    {
        int x,y,z;
        if (pickRay(e->pos(), x, y, z))
        {
            emit placeRequested(x, y, z);
            return;
        }
    }
    m_dragging = true;
    m_lastPos = e->pos();
    setCursor(Qt::ClosedHandCursor);
}

void VoxelViewWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging)
    {
        const int dx = e->pos().x() - m_lastPos.x();
        const int dy = e->pos().y() - m_lastPos.y();
        orbitBy(dx * 0.008f, dy * 0.008f);
        m_lastPos = e->pos();
    }
    else
    {
        int x,y,z;
        if (pickRay(e->pos(), x, y, z))
        {
            setHover(x,y,z);
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
    Q_UNUSED(e);
    m_dragging = false;
    unsetCursor();
}

void VoxelViewWidget::wheelEvent(QWheelEvent *e)
{
    const float delta = e->angleDelta().y() > 0 ? -0.9f : 0.9f;
    m_radius = qBound(3.0f, m_radius + delta, 300.0f);
    update();
}

void VoxelViewWidget::leaveEvent(QEvent *e)
{
    Q_UNUSED(e);
    if (m_dragging)
        return;
    setHover(INT_MIN, INT_MIN, INT_MIN);
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
    m_glReady = false;
    doneCurrent();
}
