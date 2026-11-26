/**
 * @file   SchematicOrthoViewRenderer.cpp
 * @brief  投影三视图（正视图 / 侧视图 / 俯视图）渲染器实现
 * @author BlockBox Team
 * @date   2026-11-14
 */

#include "SchematicOrthoViewRenderer.h"

#include "BlockShapeLoader.h"
#include "EntityModelLoader.h"
#include "InstanceEntityTextures.h"
#include "InstanceTextureLoader.h"
#include "LitematicEditor.h"
#include "SchematicDocument.h"
#include "utils/MapBlockPalette.h"

#include <QAtomicInt>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QPainter>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace {

/** 体积守卫：与封面渲染器同限，超出直接拒绝（耗时/内存失控保护） */
constexpr qint64 kMaxVolume = 16000000;

/** 每格放大上限（像素）：小投影放大到这个精度就够，再大只是糊 */
constexpr qreal kMaxScalePerBlock = 32.0;

/** 空气类方块（SchematicBlockState::isAir 只认 minecraft:air，这里补全） */
bool isAirLike(const QString &name)
{
    return name == QLatin1String("minecraft:air")
        || name == QLatin1String("minecraft:cave_air")
        || name == QLatin1String("minecraft:void_air")
        || name == QLatin1String("minecraft:light")
        || name == QLatin1String("minecraft:barrier")
        || name == QLatin1String("minecraft:structure_void");
}

/** 透光方块：满格也不截断列扫描，让后面的方块透出来（与 3D 编辑器的不遮挡名单一致） */
bool isSeeThrough(const QString &name)
{
    return name.contains(QLatin1String("glass"))
        || name.contains(QLatin1String("leaves"))
        || name.contains(QLatin1String("ice"))
        || name.contains(QLatin1String("water"));
}

/** 无模型方块（整格六面保底路径）按名字判断要不要染色（原版贴图是灰度的，不乘色会发白） */
bool isVegetationLike(const QString &name)
{
    return name.contains(QLatin1String("water"))
        || name.contains(QLatin1String("bubble"))
        || name.contains(QLatin1String("leaves"))
        || name.contains(QLatin1String("grass"))
        || name.contains(QLatin1String("vine"))
        || name.contains(QLatin1String("lily"))
        || name.contains(QLatin1String("fern"));
}

/** tintindex 面的简化染色表（与 VoxelViewWidget::tintColorFor 一致） */
QColor orthoTintColorFor(const QString &blockName)
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

/** 绕指定轴转角旋转一个点（与 VoxelViewWidget::rotatePoint3 同一套公式） */
void rotatePoint(float &x, float &y, float &z,
                 float ox, float oy, float oz, float angDeg, int axis)
{
    if (qFuzzyIsNull(angDeg))
        return;
    const float a = qDegreesToRadians(angDeg);
    const float c = std::cos(a), s = std::sin(a);
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

/** 绕格心转 yaw（blockstate 的 y 旋转，90° 倍数，轴对齐保持） */
void rotateYaw(float &x, float &z, float angDeg)
{
    if (qFuzzyIsNull(angDeg))
        return;
    const float a = qDegreesToRadians(angDeg);
    const float c = std::cos(a), s = std::sin(a);
    const float dx = x - 0.5f, dz = z - 0.5f;
    x = 0.5f + dx * c - dz * s;
    z = 0.5f + dx * s + dz * c;
}

void dirToAxis(const QString &dir, int &axis, int &sign)
{
    if (dir == QLatin1String("up"))        { axis = 1; sign = +1; }
    else if (dir == QLatin1String("down")) { axis = 1; sign = -1; }
    else if (dir == QLatin1String("east")) { axis = 0; sign = +1; }
    else if (dir == QLatin1String("west")) { axis = 0; sign = -1; }
    else if (dir == QLatin1String("south")){ axis = 2; sign = +1; }
    else                                   { axis = 2; sign = -1; }   // north
}

/** 旋转后的法线吸附到最近的轴对齐方向（任意角旋转的近似） */
void snapNormal(const float n[3], int &axis, int &sign)
{
    axis = 0;
    sign = 1;
    float best = -1.0f;
    for (int a = 0; a < 3; ++a)
    {
        if (qAbs(n[a]) > best)
        {
            best = qAbs(n[a]);
            axis = a;
            sign = n[a] >= 0 ? 1 : -1;
        }
    }
}

/** 从图集裁出一个面的贴图（含 uv 子区域与染色）；拿不到贴图返回空图 */
QImage cropFaceTexture(InstanceTextureLoader *texLoader, const QString &modelKey,
                       const QString &blockName, const float uv[4], bool hasUv, bool tinted)
{
    if (!texLoader)
        return QImage();

    int tile = -1;
    if (!modelKey.isEmpty())
        texLoader->tileForKey(modelKey, tile);
    if (tile < 0)
        texLoader->textureFor(blockName, tile);
    if (tile < 0)
        return QImage();

    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    texLoader->tileUv(tile, u0, v0, u1, v1);
    const QImage atlas = texLoader->atlasImage();
    if (atlas.isNull())
        return QImage();

    // 面内子区域（模型 uv 为 0..16 像素；缺省时整张贴图）
    float su0 = 0.0f, sv0 = 0.0f, su1 = 1.0f, sv1 = 1.0f;
    if (hasUv)
    {
        su0 = uv[0] / 16.0f;
        su1 = uv[2] / 16.0f;
        sv0 = qMin(uv[1], uv[3]) / 16.0f;
        sv1 = qMax(uv[1], uv[3]) / 16.0f;
    }
    const int tw = qMax(1, qRound((u1 - u0) * atlas.width()));
    const int th = qMax(1, qRound((v1 - v0) * atlas.height()));
    const int x = qRound(u0 * atlas.width() + su0 * tw);
    const int y = qRound(v0 * atlas.height() + sv0 * th);
    const int w = qMax(1, qRound((su1 - su0) * tw));
    const int h = qMax(1, qRound((sv1 - sv0) * th));
    QImage img = atlas.copy(x, y, w, h);
    if (img.isNull())
        return QImage();

    if (tinted)
    {
        // 逐像素乘色，保留 alpha（草/树叶/水的贴图本体是灰度图）
        const QColor tc = orthoTintColorFor(blockName);
        img = img.convertToFormat(QImage::Format_ARGB32);
        const int tr = tc.red(), tg = tc.green(), tb = tc.blue();
        for (int yy = 0; yy < img.height(); ++yy)
        {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(yy));
            for (int xx = 0; xx < img.width(); ++xx)
            {
                const QRgb c = line[xx];
                line[xx] = qRgba(qRed(c) * tr / 255, qGreen(c) * tg / 255,
                                 qBlue(c) * tb / 255, qAlpha(c));
            }
        }
    }
    return img;
}

/** 调色板条目：变换后的所有面 + 可见性信息 */
struct OrthoFace
{
    int axis = 0;              ///< 面法线轴（0=x 1=y 2=z，已含 yaw/元素旋转）
    int sign = 1;              ///< 面法线符号
    QImage tex;                ///< 已裁好的面贴图（空 = 用色块）
    QColor color;              ///< tex 为空时的兜底色
    float from[3] = {0, 0, 0}; ///< 所属盒子（变换后）在块内的范围
    float to[3] = {1, 1, 1};
};

struct OrthoEntry
{
    bool air = true;
    bool occludes = false;     ///< 满格不透明：所在列的扫描到此截断
    QList<OrthoFace> faces;
};

/** 一次解析整个调色板（每方块状态只算一次，扫描时纯查表） */
QVector<OrthoEntry> buildPalette(const SchematicDocument &doc,
                                 BlockShapeLoader *shapeLoader,
                                 InstanceTextureLoader *texLoader)
{
    // 先把调色板里的状态名汇总，交给形状加载器预热（避免扫描时逐格随机 IO）
    if (shapeLoader && shapeLoader->isValid())
    {
        QStringList states;
        for (int p = 0; p < doc.paletteSize(); ++p)
        {
            const SchematicBlockState &st = doc.paletteEntry(p);
            if (!st.isAir())
                states.append(st.toString());
        }
        if (!states.isEmpty())
            shapeLoader->prewarm(states);
    }

    QVector<OrthoEntry> palette(qMax(1, doc.paletteSize()));
    for (int p = 0; p < doc.paletteSize(); ++p)
    {
        const SchematicBlockState &st = doc.paletteEntry(p);
        OrthoEntry entry;
        if (isAirLike(st.name))
        {
            palette[p] = entry;
            continue;
        }
        entry.air = false;

        // 真实形状（含属性状态）；无形状时回退整格立方体，液体补 14/16 液面高度
        BlockShapes shapes;
        bool fromModel = false;
        if (shapeLoader && shapeLoader->isValid())
            fromModel = shapeLoader->shapeFor(st.toString(), shapes) && !shapes.empty();
        if (!fromModel)
        {
            ShapeBox fallback;
            if (st.name.contains(QLatin1String("water"))
                || st.name.contains(QLatin1String("lava")))
                fallback.to[1] = 0.875f;
            shapes.boxes.append(fallback);
        }

        // 满格判定（与 VoxelViewWidget 一致）：所有盒子都是未旋转、占满整格
        bool allFull = true;
        for (const ShapeBox &sb : shapes.boxes)
        {
            if (sb.isEmpty())
                continue;
            const bool fullCube = !sb.rotated && sb.yaw == 0
                && sb.from[0] <= 1e-3f && sb.from[1] <= 1e-3f && sb.from[2] <= 1e-3f
                && sb.to[0] >= 1.0f - 1e-3f && sb.to[1] >= 1.0f - 1e-3f
                && sb.to[2] >= 1.0f - 1e-3f;
            if (!fullCube)
                allFull = false;
        }
        entry.occludes = allFull && !isSeeThrough(st.name);

        for (const ShapeBox &sb : shapes.boxes)
        {
            if (sb.isEmpty())
                continue;

            // 变换盒子：元素级任意角旋转取旋转后包围盒，再套 blockstate yaw
            float mn[3] = {1e9f, 1e9f, 1e9f};
            float mx[3] = {-1e9f, -1e9f, -1e9f};
            for (int ci = 0; ci < 8; ++ci)
            {
                float pt[3] = {(ci & 1) ? sb.to[0] : sb.from[0],
                               (ci & 2) ? sb.to[1] : sb.from[1],
                               (ci & 4) ? sb.to[2] : sb.from[2]};
                if (sb.rotated)
                    rotatePoint(pt[0], pt[1], pt[2], sb.rotOrigin[0], sb.rotOrigin[1],
                                sb.rotOrigin[2], sb.rotAngle, sb.rotAxis);
                if (sb.yaw != 0)
                    rotateYaw(pt[0], pt[2], float(sb.yaw));
                for (int a = 0; a < 3; ++a)
                {
                    mn[a] = qMin(mn[a], pt[a]);
                    mx[a] = qMax(mx[a], pt[a]);
                }
            }

            static const float kAxes[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

            // 模型显式列出的面（与游戏 / 3D 编辑器一致：不补未声明的面，避免闪面）
            for (const ShapeFace &sf : sb.faces)
            {
                OrthoFace face;
                dirToAxis(sf.dir, face.axis, face.sign);
                float normal[3] = {kAxes[face.axis][0] * face.sign,
                                   kAxes[face.axis][1] * face.sign,
                                   kAxes[face.axis][2] * face.sign};
                if (sb.rotated)
                {
                    rotatePoint(normal[0], normal[1], normal[2], 0, 0, 0,
                                sb.rotAngle, sb.rotAxis);
                    snapNormal(normal, face.axis, face.sign);
                }
                if (sb.yaw != 0)
                {
                    float nx = normal[0], nz = normal[2];
                    rotateYaw(nx, nz, float(sb.yaw));
                    normal[0] = nx;
                    normal[2] = nz;
                    snapNormal(normal, face.axis, face.sign);
                }
                for (int a = 0; a < 3; ++a)
                {
                    face.from[a] = mn[a];
                    face.to[a] = mx[a];
                }
                face.tex = cropFaceTexture(texLoader, sf.texture, st.name,
                                           sf.uv, sf.hasUv, sf.tintIndex >= 0);
                face.color = MapBlockPalette::blockColor(st.name);
                entry.faces.append(face);
            }

            // 模型没写 faces 的元素按整格六面保底（与 VoxelViewWidget 一致）
            if (sb.faces.isEmpty())
            {
                static const char *const kDirs[6] = {
                    "east", "west", "up", "down", "south", "north"};
                for (const char *dirName : kDirs)
                {
                    OrthoFace face;
                    dirToAxis(QLatin1String(dirName), face.axis, face.sign);
                    for (int a = 0; a < 3; ++a)
                    {
                        face.from[a] = mn[a];
                        face.to[a] = mx[a];
                    }
                    face.tex = cropFaceTexture(texLoader, QString(), st.name,
                                               nullptr, false, isVegetationLike(st.name));
                    face.color = MapBlockPalette::blockColor(st.name);
                    entry.faces.append(face);
                }
            }
        }
        palette[p] = entry;
    }
    return palette;
}

/** 单个视图的相机配置：屏幕两轴、深度轴与正对的模型面 */
struct ViewConfig
{
    int uAxis;        ///< 列对应的世界轴
    bool uFlip;       ///< 列是否反向（编辑器同款朝向：正视图中东在左）
    int vAxis;        ///< 行对应的世界轴
    bool vFlip;       ///< 行是否反向
    int dAxis;        ///< 深度轴
    bool dNearAtMax;  ///< 近端是否在轴最大值（俯视图从上往下看）
    int faceAxis;     ///< 正对面的法线轴
    int faceSign;     ///< 正对面的法线符号
};

int axisSize(const SchematicDocument &doc, int axis)
{
    return axis == 0 ? doc.sizeX() : (axis == 1 ? doc.sizeY() : doc.sizeZ());
}

// ============================================================================
// 生物 / 实体：三视图里的合成
// ============================================================================
/** 三视图共用的一份实体资源（模型三角面 + 贴图矩形） */
struct OrthoEntitySet
{
    struct Item
    {
        const SchematicEntity *entity = nullptr;
        QRect rect;                             ///< 图集里的贴图矩形
        QSize sourceSize;                       ///< 贴图在图集里的实际尺寸
        QVector<EntityMeshTriangle> mesh;       ///< 模型三角面（模型空间）
    };
    QVector<Item> items;
    QImage atlas;

    bool empty() const { return items.isEmpty() || atlas.isNull(); }
};

/** 把投影里的生物准备好（没有实体贴图 / 没有模型的实体直接跳过） */
OrthoEntitySet prepareEntities(const SchematicDocument &doc, InstanceEntityTextures *textures)
{
    OrthoEntitySet set;
    if (textures == nullptr || !textures->isValid() || doc.entities().isEmpty())
        return set;
    set.atlas = textures->atlasImage();
    if (set.atlas.isNull())
        return set;

    QHash<QString, QVector<EntityMeshTriangle>> meshCache;
    QHash<QString, QPair<QRect, QSize>> rectCache;
    for (const SchematicEntity &entity : doc.entities())
    {
        EntityModel model;
        if (!EntityModelLoader::instance()->modelFor(entity.id, model))
            continue;

        auto rectIt = rectCache.constFind(model.textureKey);
        if (rectIt == rectCache.constEnd())
        {
            QRect rect;
            QSize sourceSize;
            if (!textures->rectForKey(model.textureKey, rect, &sourceSize))
            {
                rect = QRect();
                sourceSize = QSize();
            }
            rectIt = rectCache.insert(model.textureKey, QPair<QRect, QSize>(rect, sourceSize));
        }
        if (rectIt.value().first.isEmpty() || rectIt.value().second.isEmpty())
            continue;
        // 模型布局和实例贴图对不上就跳过（详见 EntityModel::uvLimit）
        if (model.uvLimitU > float(rectIt.value().second.width()) + 2.0f
            || model.uvLimitV > float(rectIt.value().second.height()) + 2.0f)
        {
            continue;
        }

        auto meshIt = meshCache.constFind(model.id);
        if (meshIt == meshCache.constEnd())
            meshIt = meshCache.insert(model.id, buildEntityMesh(model));
        if (meshIt.value().isEmpty())
            continue;

        OrthoEntitySet::Item item;
        item.entity = &entity;
        item.rect = rectIt.value().first;
        item.sourceSize = rectIt.value().second;
        item.mesh = meshIt.value();
        set.items.append(item);
    }
    return set;
}

/** 世界坐标按轴取分量 */
inline float axisComponent(float x, float y, float z, int axis)
{
    return axis == 0 ? x : (axis == 1 ? y : z);
}

/**
 * @brief 把实体画进正交视图
 *
 * 方块已经按列扫描画完了，这里再做一遍实体三角形光栅化：
 *   1) 顶点变换到屏幕坐标（正交投影，天然无透视，贴图仿射即可）；
 *   2) 每像素与「该屏幕格里最近的方块面」比深度，被墙挡住的部分自动不画；
 *   3) 实体之间用像素级 z-buffer，前后关系正确。
 */
void compositeEntities(QImage &out, const QVector<float> &cellDepth, int uSize, int vSize,
                        int dSize, qreal scale, const ViewConfig &vc,
                        const OrthoEntitySet &entities)
{
    if (entities.empty() || scale <= 0.0 || uSize <= 0 || vSize <= 0)
        return;

    const int w = out.width();
    const int h = out.height();
    if (w <= 0 || h <= 0)
        return;

    QImage layer(w, h, QImage::Format_ARGB32_Premultiplied);
    if (layer.isNull())
        return;
    layer.fill(Qt::transparent);
    QVector<float> zbuf(w * h, 1e30f);

    const QImage atlas = entities.atlas;
    const QVector3D lightDir = QVector3D(0.32f, 0.86f, 0.38f).normalized();

    for (const OrthoEntitySet::Item &item : entities.items)
    {
        const SchematicEntity &entity = *item.entity;
        // 模型面朝 -Z，原版 yaw 0 面朝 +Z，所以绕 Y 转 180 - yaw
        const float yawRad = qDegreesToRadians(180.0f - entity.yaw);
        const float cy = qCos(yawRad);
        const float sy = qSin(yawRad);

        for (const EntityMeshTriangle &tri : item.mesh)
        {
            float px[3], py[3], pd[3], pu[3], pv[3];
            for (int k = 0; k < 3; ++k)
            {
                const QVector3D &p = tri.pos[k];
                const float wx = p.x() * cy + p.z() * sy + float(entity.x);
                const float wy = p.y() + float(entity.y);
                const float wz = -p.x() * sy + p.z() * cy + float(entity.z);

                const float cu = axisComponent(wx, wy, wz, vc.uAxis);
                const float cv = axisComponent(wx, wy, wz, vc.vAxis);
                const float cd = axisComponent(wx, wy, wz, vc.dAxis);

                const float su = vc.uFlip ? (uSize - cu) : cu;
                const float sv = vc.vFlip ? (vSize - cv) : cv;
                const float sd = vc.dNearAtMax ? (dSize - cd) : cd;

                px[k] = float(su * scale);
                py[k] = float(sv * scale);
                pd[k] = sd;
                pu[k] = tri.uv[k].x();
                pv[k] = tri.uv[k].y();
            }

            const float area = (px[1] - px[0]) * (py[2] - py[0])
                               - (px[2] - px[0]) * (py[1] - py[0]);
            if (qAbs(area) < 1e-5f)
                continue;
            const float invArea = 1.0f / area;

            // 面光：与三维视图的实体一致（法线绕 Y 转后按方向光取绝对值）
            const float nx = tri.normal.x() * cy + tri.normal.z() * sy;
            const float nz = -tri.normal.x() * sy + tri.normal.z() * cy;
            const float nd = qAbs(nx * lightDir.x() + tri.normal.y() * lightDir.y()
                                  + nz * lightDir.z());
            const float light = 0.56f + 0.44f * qBound(0.0f, nd, 1.0f);

            const int minX = qBound(0, int(qFloor(qMin(px[0], qMin(px[1], px[2])))), w - 1);
            const int maxX = qBound(0, int(qCeil(qMax(px[0], qMax(px[1], px[2])))), w - 1);
            const int minY = qBound(0, int(qFloor(qMin(py[0], qMin(py[1], py[2])))), h - 1);
            const int maxY = qBound(0, int(qCeil(qMax(py[0], qMax(py[1], py[2])))), h - 1);

            for (int iy = minY; iy <= maxY; ++iy)
            {
                uchar *line = layer.scanLine(iy);
                const float fy = float(iy) + 0.5f;
                for (int ix = minX; ix <= maxX; ++ix)
                {
                    const float fx = float(ix) + 0.5f;
                    const float b0 = ((px[1] - fx) * (py[2] - fy)
                                      - (px[2] - fx) * (py[1] - fy)) * invArea;
                    const float b1 = ((px[2] - fx) * (py[0] - fy)
                                      - (px[0] - fx) * (py[2] - fy)) * invArea;
                    const float b2 = 1.0f - b0 - b1;
                    if (b0 < -0.0002f || b1 < -0.0002f || b2 < -0.0002f)
                        continue;

                    const float depth = b0 * pd[0] + b1 * pd[1] + b2 * pd[2];

                    // 和该屏幕格里最近的方块面比深度：被墙/地板挡住就不画
                    const int cellC = qBound(0, int(fx / float(scale)), uSize - 1);
                    const int cellR = qBound(0, int(fy / float(scale)), vSize - 1);
                    const float cellD = cellDepth.at(cellR * uSize + cellC);
                    if (depth >= cellD - 0.02f)
                        continue;

                    const int idx = iy * w + ix;
                    if (depth >= zbuf.at(idx))
                        continue;

                    const float u = b0 * pu[0] + b1 * pu[1] + b2 * pu[2];
                    const float v = b0 * pv[0] + b1 * pv[1] + b2 * pv[2];
                    // 模型 uv 是像素；贴图在图集里可能被等比缩过，按实际尺寸换算
                    const float texelU = float(item.rect.width())
                                         / float(qMax(1, item.sourceSize.width()));
                    const float texelV = float(item.rect.height())
                                         / float(qMax(1, item.sourceSize.height()));
                    const int tx = item.rect.x()
                                   + qBound(0, int(u * texelU), item.rect.width() - 1);
                    const int ty = item.rect.y()
                                   + qBound(0, int(v * texelV), item.rect.height() - 1);
                    if (tx < 0 || ty < 0 || tx >= atlas.width() || ty >= atlas.height())
                        continue;
                    // 图集是 RGBA8888：按字节读，别当 QRgb（内存序是 BGRA，红蓝会颠倒）
                    const uchar *src = atlas.constScanLine(ty) + tx * 4;
                    const int alpha = src[3];
                    if (alpha < 40)
                        continue;   // 贴图透明区（四肢缝隙等）不占像素

                    const int rr = qBound(0, int(src[0] * light), 255);
                    const int gg = qBound(0, int(src[1] * light), 255);
                    const int bb = qBound(0, int(src[2] * light), 255);
                    uchar *dst = line + ix * 4;
                    // 层是 ARGB32_Premultiplied（内存序 B, G, R, A）：颜色先乘 alpha
                    const int a8 = qBound(0, alpha, 255);
                    dst[0] = uchar(bb * a8 / 255);
                    dst[1] = uchar(gg * a8 / 255);
                    dst[2] = uchar(rr * a8 / 255);
                    dst[3] = uchar(a8);
                    zbuf[idx] = depth;
                }
            }
        }
    }

    QPainter painter(&out);
    painter.drawImage(0, 0, layer);
}


/** 渲染一张视图：逐列近→远扫描可见块，再远→近画正对相机的那一类面 */
QImage renderView(const SchematicDocument &doc, const QVector<OrthoEntry> &palette,
                  const ViewConfig &vc, qreal scale, const OrthoEntitySet &entities)
{
    const int sizeX = doc.sizeX(), sizeZ = doc.sizeZ();
    const int layer = sizeX * sizeZ;
    const int uSize = axisSize(doc, vc.uAxis);
    const int vSize = axisSize(doc, vc.vAxis);
    const int dSize = axisSize(doc, vc.dAxis);

    QImage out(qMax(1, qRound(uSize * scale)), qMax(1, qRound(vSize * scale)),
               QImage::Format_ARGB32_Premultiplied);
    out.fill(QColor(0xFF, 0xFF, 0xFF));   // 白底：蓝图观感，发帖可直接用
    if (uSize <= 0 || vSize <= 0 || dSize <= 0)
        return out;

    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // 每个屏幕格里「最近的方块面」深度（单位：格，越小越近），实体合成时用来判断遮挡
    QVector<float> cellDepth(uSize * vSize, 1e30f);

    for (int c = 0; c < uSize; ++c)
    {
        const int ua = vc.uFlip ? (uSize - 1 - c) : c;
        for (int r = 0; r < vSize; ++r)
        {
            const int va = vc.vFlip ? (vSize - 1 - r) : r;

            // ── 近→远收集本列可见块（满格不透明处截断） ──
            QList<int> visible;
            float nearestDepth = 1e30f;
            for (int di = 0; di < dSize; ++di)
            {
                const int d = vc.dNearAtMax ? (dSize - 1 - di) : di;
                int xyz[3] = {0, 0, 0};
                xyz[vc.uAxis] = ua;
                xyz[vc.vAxis] = va;
                xyz[vc.dAxis] = d;
                const int idx = xyz[1] * layer + xyz[2] * sizeX + xyz[0];
                const int pal = doc.paletteIndexAt(idx);
                const OrthoEntry &e = palette[pal];
                if (e.air)
                    continue;
                if (visible.isEmpty())
                {
                    // 本列最近的可见方块：它的「朝向相机的那一面」就是遮挡深度
                    nearestDepth = vc.dNearAtMax ? float(dSize - (d + 1)) : float(d);
                }
                visible.append(pal);
                if (e.occludes)
                    break;
            }
            cellDepth[r * uSize + c] = nearestDepth;

            // ── 远→近绘制：每个可见块只画正对相机的那一类面 ──
            for (int k = visible.size() - 1; k >= 0; --k)
            {
                const OrthoEntry &e = palette[visible[k]];
                QList<const OrthoFace *> faces;
                for (const OrthoFace &f : e.faces)
                {
                    if (f.axis == vc.faceAxis && f.sign == vc.faceSign)
                        faces.append(&f);
                }
                if (faces.isEmpty())
                    continue;
                // 块内多面按面平面深度排：远在前（同深度保持模型书写顺序）
                std::stable_sort(faces.begin(), faces.end(),
                                 [vc](const OrthoFace *a, const OrthoFace *b) {
                                     // 面平面位置：法线朝正方向的面在盒子的 to，反方向在 from
                                     const float pa = vc.faceSign > 0 ? a->to[vc.dAxis]
                                                                      : a->from[vc.dAxis];
                                     const float pb = vc.faceSign > 0 ? b->to[vc.dAxis]
                                                                      : b->from[vc.dAxis];
                                     return vc.dNearAtMax ? pa < pb : pa > pb;
                                 });
                for (const OrthoFace *f : faces)
                {
                    // 面的 from/to 是「格内」的 0..1 坐标，先平移到这一格在投影里的位置，
                    // 之后翻面/缩放才是正确的屏幕范围（漏了这一步所有方块会叠在同一格里）
                    float u0 = f->from[vc.uAxis] + float(ua);
                    float u1 = f->to[vc.uAxis] + float(ua);
                    float v0 = f->from[vc.vAxis] + float(va);
                    float v1 = f->to[vc.vAxis] + float(va);
                    if (vc.uFlip)
                    {
                        const float t0 = u0;
                        u0 = uSize - u1;
                        u1 = uSize - t0;
                    }
                    if (vc.vFlip)
                    {
                        const float t0 = v0;
                        v0 = vSize - v1;
                        v1 = vSize - t0;
                    }
                    // 各边外扩一点，避免相邻同材质面之间出现白缝；
                    // 缩小时按比例收缩外扩量，防止邻面大面积互相盖住
                    const qreal expand = 0.5 * qMin(scale, 1.0);
                    const QRectF target(u0 * scale - expand, v0 * scale - expand,
                                        (u1 - u0) * scale + 2.0 * expand,
                                        (v1 - v0) * scale + 2.0 * expand);
                    if (f->tex.isNull())
                        p.fillRect(target, f->color);
                    else
                        p.drawImage(target, f->tex);
                }
            }
        }
    }
    p.end();

    // 生物 / 实体：方块画完后再合成（被方块挡住的部分按深度丢弃）
    compositeEntities(out, cellDepth, uSize, vSize, dSize, scale, vc, entities);
    return out;
}

/** .schematic / .schem 先转成临时 .litematic（与封面渲染器同款做法），返回空串表示转换失败 */
QString ensureLitematic(const QString &path, QString *errorOut)
{
    const QString lowered = path.toLower();
    if (lowered.endsWith(QLatin1String(".litematic"))
        || lowered.endsWith(QLatin1String(".nbt")))
        return path;

    static QAtomicInt s_tmpSeq{0};
    const QString tmpDir = QDir::tempPath() + QStringLiteral("/blockbox_ortho");
    QDir().mkpath(tmpDir);
    const QString tmp = QString("%1/%2_%3.litematic")
                            .arg(tmpDir)
                            .arg(QFileInfo(path).completeBaseName())
                            .arg(s_tmpSeq.fetchAndAddRelaxed(1));

    bool ok = false;
    if (lowered.endsWith(QLatin1String(".schematic")))
        ok = LitematicEditor::convertFromSchematica(path, tmp, errorOut);
    else if (lowered.endsWith(QLatin1String(".schem")))
        ok = LitematicEditor::convertFromSponge(path, tmp, errorOut);
    else
        return path;

    if (!ok)
    {
        QFile::remove(tmp);
        return QString();
    }
    return tmp;
}

} // namespace

SchematicOrthoResult SchematicOrthoViewRenderer::render(const QString &filePath,
                                                        const QString &instancePath,
                                                        int maxPixels)
{
    SchematicOrthoResult result;
    if (filePath.isEmpty())
    {
        result.error = QStringLiteral("没有指定投影文件");
        return result;
    }
    if (maxPixels < 64)
        maxPixels = 64;

    // ── 格式归一化 + 读文档 ──
    QString error;
    const QString loadPath = ensureLitematic(filePath, &error);
    if (loadPath.isEmpty())
    {
        result.error = QStringLiteral("投影格式转换失败：%1").arg(error);
        return result;
    }

    SchematicDocument doc;
    QString loadError;
    const bool loaded = doc.load(loadPath, &loadError);
    if (loadPath != filePath)
        QFile::remove(loadPath);   // 临时文件用完即删（数据已在内存里）
    if (!loaded)
    {
        result.error = QStringLiteral("投影读不出来：%1").arg(
            loadError.isEmpty() ? QStringLiteral("文件解析失败") : loadError);
        return result;
    }
    if (doc.sizeX() <= 0 || doc.sizeY() <= 0 || doc.sizeZ() <= 0)
    {
        result.error = QStringLiteral("投影尺寸不完整，无法生成三视图");
        return result;
    }
    if (qint64(doc.volume()) > kMaxVolume)
    {
        result.error = QStringLiteral("投影太大了（超过 1600 万格），生成三视图会拖慢电脑，先拆分一下");
        return result;
    }

    // ── 贴图 / 形状来源（无实例时全走色块兜底，照样能出图） ──
    InstanceTextureLoader *texLoader = nullptr;
    BlockShapeLoader *shapeLoader = nullptr;
    if (!instancePath.isEmpty())
    {
        texLoader = InstanceTextureLoader::sharedForInstance(instancePath);
        shapeLoader = BlockShapeLoader::sharedForInstance(instancePath);
    }

    // ── 快速判定是否全空气 ──
    const int volume = doc.volume();
    bool hasBlock = false;
    for (int i = 0; i < volume; ++i)
    {
        if (doc.paletteIndexAt(i) != 0)
        {
            hasBlock = true;
            break;
        }
    }
    if (!hasBlock)
    {
        result.error = QStringLiteral("投影是空的，没有方块可画");
        return result;
    }

    const QVector<OrthoEntry> palette = buildPalette(doc, shapeLoader, texLoader);

    // ── 生物 / 实体（贴图同样取自当前实例；没有生物就不必解实体贴图） ──
    InstanceEntityTextures *entityTextures = nullptr;
    if (!instancePath.isEmpty() && doc.entities().isEmpty() == false)
        entityTextures = InstanceEntityTextures::sharedForInstance(instancePath);
    const OrthoEntitySet entities = prepareEntities(doc, entityTextures);

    // ── 三视图共用一个比例，保证预览时相对大小一致 ──
    const int dims[3] = {doc.sizeX(), doc.sizeY(), doc.sizeZ()};
    const ViewConfig configs[3] = {
        // 正视图：编辑器「前」视角，相机在 -Z 往 +Z 看 → 列=世界 X（镜像，东在左）、行=Y 翻转
        {0, true, 1, true, 2, false, 2, -1},
        // 侧视图：编辑器「左」视角，相机在 -X 往 +X 看 → 列=世界 Z、行=Y 翻转
        {2, false, 1, true, 0, false, 0, -1},
        // 俯视图：编辑器「顶」视角，相机在上往下看 → 列=世界 X、行=世界 Z（北在上）
        {0, false, 2, false, 1, true, 1, +1},
    };
    qreal scale = kMaxScalePerBlock;
    for (const ViewConfig &vc : configs)
    {
        scale = qMin(scale, qMin(qreal(maxPixels) / qMax(1, dims[vc.uAxis]),
                                 qreal(maxPixels) / qMax(1, dims[vc.vAxis])));
    }
    scale = qBound(0.01, scale, kMaxScalePerBlock);

    result.front = renderView(doc, palette, configs[0], scale, entities);
    result.left = renderView(doc, palette, configs[1], scale, entities);
    result.top = renderView(doc, palette, configs[2], scale, entities);

    if (!result.isValid())
        result.error = QStringLiteral("三视图渲染失败，请稍后再试");
    return result;
}
