/**
 * @file   EntityModelLoader.cpp
 * @brief  投影生物 / 实体模型表实现（模型数据来自 prismarine-viewer，MIT）
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "EntityModelLoader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QtMath>

#include <memory>

namespace {

/** 模型数据随程序打包在资源里（由 tools 脚本从 prismarine-viewer 的 entities.json 裁剪生成） */
const char kModelResource[] = ":/resources/projection_entity_models.json";

/** 1/16 格 */
constexpr float kUnit = 1.0f / 16.0f;

/** 少数实体的 Java ID 与模型表里的键不同名 */
struct AliasEntry
{
    const char *from;
    const char *to;
};

/** 基岩版几何和 Java 贴图布局不一致时的 UV 修补（单位：贴图像素） */
struct UvFix
{
    const char *id;
    float du;
    float dv;
};

const UvFix kUvFixes[] = {
    // 羊的基岩版几何按 64×64 贴图写好（v 从 32 起），Java 的 sheep_fur.png 是 64×32，
    // 整体上移 32 后正好是 Java 的布局（头 0 / 身 8 / 腿 16）
    { "sheep", 0.0f, -32.0f },
    { nullptr, 0.0f, 0.0f }
};

const AliasEntry kAliases[] = {
    { "zombie_pigman", "zombified_piglin" },
    { "snowman", "snow_golem" },
    { "xp_orb", "experience_orb" },
    { "oak_boat", "boat" },
    { "spruce_boat", "boat" },
    { "birch_boat", "boat" },
    { "jungle_boat", "boat" },
    { "acacia_boat", "boat" },
    { "dark_oak_boat", "boat" },
    { "mangrove_boat", "boat" },
    { "cherry_boat", "boat" },
    { "bamboo_raft", "boat" },
    { "cell_horse", "horse" },
    { nullptr, nullptr }
};

/** 一个面：uv 起点由 u0/u1、v0/v1 与立方体尺寸点乘得到（原版盒子展开规则） */
struct FaceDef
{
    const char *name;
    int u0[3];
    int u1[3];
    int v0[3];
    int v1[3];
    /** 四个角的 {x, y, z, 取 u1 还是 u0, 取 v1 还是 v0} */
    float corner[4][5];
};

/** 顺序与 prismarine-viewer 的 elemFaces 一致（贴图朝向已对好，别随意改） */
const FaceDef kFaces[6] = {
    { "up",
      { 0, 0, 1 }, { 1, 0, 1 }, { 0, 0, 0 }, { 0, 0, 1 },
      { { 0, 1, 1, 0, 0 }, { 1, 1, 1, 1, 0 }, { 0, 1, 0, 0, 1 }, { 1, 1, 0, 1, 1 } } },
    { "down",
      { 1, 0, 1 }, { 2, 0, 1 }, { 0, 0, 0 }, { 0, 0, 1 },
      { { 1, 0, 1, 1, 0 }, { 0, 0, 1, 0, 0 }, { 1, 0, 0, 1, 1 }, { 0, 0, 0, 0, 1 } } },
    { "east",
      { 1, 0, 1 }, { 1, 0, 2 }, { 0, 0, 1 }, { 0, 1, 1 },
      { { 1, 1, 1, 1, 0 }, { 1, 0, 1, 1, 1 }, { 1, 1, 0, 0, 0 }, { 1, 0, 0, 0, 1 } } },
    { "west",
      { 0, 0, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 1, 1 },
      { { 0, 1, 0, 1, 0 }, { 0, 0, 0, 1, 1 }, { 0, 1, 1, 0, 0 }, { 0, 0, 1, 0, 1 } } },
    { "north",
      { 0, 0, 1 }, { 1, 0, 1 }, { 0, 0, 1 }, { 0, 1, 1 },
      { { 1, 0, 0, 1, 1 }, { 0, 0, 0, 0, 1 }, { 1, 1, 0, 1, 0 }, { 0, 1, 0, 0, 0 } } },
    { "south",
      { 1, 0, 2 }, { 2, 0, 2 }, { 0, 0, 1 }, { 0, 1, 1 },
      { { 0, 0, 1, 1, 1 }, { 1, 0, 1, 0, 1 }, { 0, 1, 1, 1, 0 }, { 1, 1, 1, 0, 0 } } },
};

float dotUnit(const int unit[3], const QVector3D &size)
{
    return unit[0] * size.x() + unit[1] * size.y() + unit[2] * size.z();
}

QVector3D rotateX(const QVector3D &p, float deg)
{
    if (qFuzzyIsNull(deg))
        return p;
    const float a = qDegreesToRadians(deg);
    const float c = qCos(a), s = qSin(a);
    return QVector3D(p.x(), p.y() * c - p.z() * s, p.y() * s + p.z() * c);
}

QVector3D rotateY(const QVector3D &p, float deg)
{
    if (qFuzzyIsNull(deg))
        return p;
    const float a = qDegreesToRadians(deg);
    const float c = qCos(a), s = qSin(a);
    return QVector3D(p.x() * c + p.z() * s, p.y(), -p.x() * s + p.z() * c);
}

QVector3D rotateZ(const QVector3D &p, float deg)
{
    if (qFuzzyIsNull(deg))
        return p;
    const float a = qDegreesToRadians(deg);
    const float c = qCos(a), s = qSin(a);
    return QVector3D(p.x() * c - p.y() * s, p.x() * s + p.y() * c, p.z());
}

/** 与 three.js 的 Euler('XYZ') 一致：先绕 Z、再绕 Y、最后绕 X */
QVector3D rotateEuler(const QVector3D &p, const QVector3D &deg)
{
    if (deg.isNull())
        return p;
    return rotateX(rotateY(rotateZ(p, deg.z()), deg.y()), deg.x());
}

QVector3D readVec3(const QJsonValue &value)
{
    const QJsonArray arr = value.toArray();
    if (arr.size() < 3)
        return QVector3D();
    return QVector3D(float(arr.at(0).toDouble()), float(arr.at(1).toDouble()),
                     float(arr.at(2).toDouble()));
}

QVector2D readVec2(const QJsonValue &value)
{
    const QJsonArray arr = value.toArray();
    if (arr.size() < 2)
        return QVector2D();
    return QVector2D(float(arr.at(0).toDouble()), float(arr.at(1).toDouble()));
}

} // namespace

// ============================================================================
// 模型表
// ============================================================================
EntityModelLoader *EntityModelLoader::instance()
{
    static EntityModelLoader loader;
    return &loader;
}

QString EntityModelLoader::shortName(const QString &entityId)
{
    QString n = entityId.trimmed().toLower();
    const int colon = n.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        n = n.mid(colon + 1);
    return n.trimmed();
}

bool EntityModelLoader::isValid() const
{
    ensureLoaded();
    return !m_models.isEmpty();
}

int EntityModelLoader::modelCount() const
{
    ensureLoaded();
    return m_models.size();
}

bool EntityModelLoader::modelFor(const QString &entityId, EntityModel &out) const
{
    ensureLoaded();
    const QString name = shortName(entityId);
    if (name.isEmpty())
        return false;

    auto it = m_models.constFind(name);
    if (it != m_models.constEnd())
    {
        out = it.value();
        return out.isValid();
    }
    for (int i = 0; kAliases[i].from; ++i)
    {
        if (name != QLatin1String(kAliases[i].from))
            continue;
        it = m_models.constFind(QLatin1String(kAliases[i].to));
        if (it != m_models.constEnd())
        {
            out = it.value();
            return out.isValid();
        }
    }
    return false;
}

bool EntityModelLoader::ensureLoaded() const
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (m_loaded)
        return !m_models.isEmpty();
    m_loaded = true;

    QFile file(QString::fromLatin1(kModelResource));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        const QJsonObject obj = it.value().toObject();
        EntityModel model;
        model.id = obj.value(QStringLiteral("id")).toString(it.key());
        model.chineseName = obj.value(QStringLiteral("cn")).toString();
        model.texWidth = qMax(1, obj.value(QStringLiteral("tw")).toInt(64));
        model.texHeight = qMax(1, obj.value(QStringLiteral("th")).toInt(64));

        // 贴图：优先 default / base_xxx（马的毛色、猫的毛色这类），
        // 别挑到 armor_*、beam、angry 这种特效用图
        const QJsonObject textures = obj.value(QStringLiteral("tex")).toObject();
        QString textureRaw;
        int bestScore = -1000;
        for (auto texIt = textures.constBegin(); texIt != textures.constEnd(); ++texIt)
        {
            const QString key = texIt.key().toLower();
            int score = 0;
            if (key == QLatin1String("default"))
                score = 100;
            else if (key.startsWith(QLatin1String("base")))
                score = 90;
            else if (key == QLatin1String("normal") || key == QLatin1String("main"))
                score = 85;
            else
                score = 60;
            if (key.contains(QLatin1String("armor")))
                score -= 30;
            if (key.contains(QLatin1String("marking")))
                score -= 25;
            if (key.contains(QLatin1String("beam")) || key.contains(QLatin1String("exploding"))
                || key.contains(QLatin1String("charged")) || key.contains(QLatin1String("shooting"))
                || key.contains(QLatin1String("angry")) || key.contains(QLatin1String("nectar"))
                || key.contains(QLatin1String("ghost")) || key.contains(QLatin1String("overlay"))
                || key.contains(QLatin1String("invulnerable"))
                || key.contains(QLatin1String("suffocated")))
            {
                score -= 40;
            }
            if (score > bestScore)
            {
                bestScore = score;
                textureRaw = texIt.value().toString();
            }
        }
        if (textureRaw.startsWith(QLatin1String("textures/")))
            textureRaw = textureRaw.mid(9);
        model.textureKey = textureRaw;

        const QJsonArray bones = obj.value(QStringLiteral("bones")).toArray();
        model.bones.reserve(bones.size());
        for (const QJsonValue &boneValue : bones)
        {
            const QJsonObject boneObj = boneValue.toObject();
            EntityBone bone;
            bone.name = boneObj.value(QStringLiteral("n")).toString();
            bone.parent = boneObj.value(QStringLiteral("p")).toString();
            if (boneObj.contains(QStringLiteral("v")))
                bone.pivot = readVec3(boneObj.value(QStringLiteral("v")));
            if (boneObj.contains(QStringLiteral("r")))
            {
                // 原版是 y 向下的模型空间，这里整体取反搬到 y 向上
                bone.rot = -readVec3(boneObj.value(QStringLiteral("r")));
                bone.hasRot = true;
            }
            bone.mirror = boneObj.value(QStringLiteral("m")).toInt() != 0;

            const QJsonArray cubes = boneObj.value(QStringLiteral("c")).toArray();
            bone.cubes.reserve(cubes.size());
            for (const QJsonValue &cubeValue : cubes)
            {
                const QJsonObject cubeObj = cubeValue.toObject();
                EntityCube cube;
                cube.origin = readVec3(cubeObj.value(QStringLiteral("o")));
                cube.size = readVec3(cubeObj.value(QStringLiteral("s")));                if (cubeObj.contains(QStringLiteral("r")))
                    cube.rot = -readVec3(cubeObj.value(QStringLiteral("r")));
                if (cubeObj.contains(QStringLiteral("p")))
                {
                    cube.pivot = readVec3(cubeObj.value(QStringLiteral("p")));
                    cube.hasPivot = true;
                }
                cube.mirror = cubeObj.value(QStringLiteral("m")).toInt() != 0;
                cube.inflate = float(cubeObj.value(QStringLiteral("i")).toDouble());

                const QJsonValue uvValue = cubeObj.value(QStringLiteral("uv"));
                if (uvValue.isArray())
                {
                    cube.uv = readVec2(uvValue);
                }
                else if (uvValue.isObject())
                {
                    cube.perFaceUv = true;
                    const QJsonObject uvObj = uvValue.toObject();
                    for (auto faceIt = uvObj.constBegin(); faceIt != uvObj.constEnd(); ++faceIt)
                    {
                        const QJsonObject faceObj = faceIt.value().toObject();
                        if (faceObj.contains(QStringLiteral("u")))
                            cube.faceUv.insert(faceIt.key(),
                                               readVec2(faceObj.value(QStringLiteral("u"))));
                        if (faceObj.contains(QStringLiteral("s")))
                            cube.faceUvSize.insert(faceIt.key(),
                                                   readVec2(faceObj.value(QStringLiteral("s"))));
                    }
                }
                bone.cubes.append(cube);
            }
            model.bones.append(bone);
        }

        if (!model.bones.isEmpty())
        {
            // 先应用 UV 修补（按模型短名匹配）
            const QString shortId = shortName(model.id);
            for (int i = 0; kUvFixes[i].id; ++i)
            {
                if (shortId != QLatin1String(kUvFixes[i].id))
                    continue;
                for (EntityBone &bone : model.bones)
                {
                    for (EntityCube &cube : bone.cubes)
                    {
                        cube.uv += QVector2D(kUvFixes[i].du, kUvFixes[i].dv);
                        for (auto it = cube.faceUv.begin(); it != cube.faceUv.end(); ++it)
                            it.value() += QVector2D(kUvFixes[i].du, kUvFixes[i].dv);
                    }
                }
                break;
            }
            // 记录模型用到的贴图边界：实例贴图比这个还小的时候（羊、蝙蝠这类模型的
            // 布局和 Java 贴图对不上）就不画，免得贴图错位糊成一片
            float maxU = 0.0f, maxV = 0.0f;
            for (const EntityBone &bone : model.bones)
            {
                for (const EntityCube &cube : bone.cubes)
                {
                    if (cube.perFaceUv)
                    {
                        for (auto faceIt = cube.faceUv.constBegin();
                             faceIt != cube.faceUv.constEnd(); ++faceIt)
                        {
                            const QVector2D sizeOverride = cube.faceUvSize.value(faceIt.key());
                            maxU = qMax(maxU, faceIt.value().x() + sizeOverride.x());
                            maxV = qMax(maxV, faceIt.value().y() + sizeOverride.y());
                        }
                        continue;
                    }
                    // 盒子展开的贴图范围：u 最远到 u + 2w + 2d，v 最远到 v + h + d（含外扩）
                    const float inflate = 2.0f * qMax(0.0f, cube.inflate);
                    maxU = qMax(maxU, cube.uv.x() + 2.0f * cube.size.x()
                                        + 2.0f * cube.size.z() + inflate);
                    maxV = qMax(maxV, cube.uv.y() + cube.size.y() + cube.size.z() + inflate);
                }
            }
            model.uvLimitU = maxU;
            model.uvLimitV = maxV;
            m_models.insert(shortName(model.id), model);
        }
    }
    return !m_models.isEmpty();
}

// ============================================================================
// 三角面生成
// ============================================================================
QVector<EntityMeshTriangle> buildEntityMesh(const EntityModel &model)
{
    QVector<EntityMeshTriangle> out;
    if (model.bones.isEmpty())
        return out;

    // 说明：模型声明的贴图尺寸（texWidth/texHeight）只用于判断模型布局是否
    // 和实例贴图匹配（uvLimit），这里不再拿它归一化 UV，见 EntityMeshTriangle::uv

    for (int bi = 0; bi < model.bones.size(); ++bi)
    {
        const EntityBone &bone = model.bones.at(bi);
        for (const EntityCube &cube : bone.cubes)
        {
            // 退化盒子（三个方向都为 0）没有面积，直接跳过
            if (cube.size.lengthSquared() <= 1e-8f)
                continue;

            const QVector3D cubeCenter = cube.origin + cube.size * 0.5f;
            const QVector3D cubePivot = cube.hasPivot ? cube.pivot : cubeCenter;

            for (const FaceDef &face : kFaces)
            {
                QVector2D uvBase = cube.uv;
                bool hasSizeOverride = false;
                QVector2D uvSizeOverride;
                if (cube.perFaceUv)
                {
                    auto fit = cube.faceUv.constFind(QLatin1String(face.name));
                    if (fit == cube.faceUv.constEnd())
                        continue;   // 这类扁平模型只声明了几个面，其余面原版也没画
                    uvBase = fit.value();
                    auto sit = cube.faceUvSize.constFind(QLatin1String(face.name));
                    if (sit != cube.faceUvSize.constEnd())
                    {
                        hasSizeOverride = true;
                        uvSizeOverride = sit.value();
                    }
                }

                float uStart = 0.0f, uEnd = 0.0f, vStart = 0.0f, vEnd = 0.0f;
                if (hasSizeOverride)
                {
                    uStart = uvBase.x();
                    uEnd = uvBase.x() + uvSizeOverride.x();
                    vStart = uvBase.y();
                    vEnd = uvBase.y() + uvSizeOverride.y();
                }
                else
                {
                    uStart = uvBase.x() + dotUnit(face.u0, cube.size);
                    uEnd = uvBase.x() + dotUnit(face.u1, cube.size);
                    vStart = uvBase.y() + dotUnit(face.v0, cube.size);
                    vEnd = uvBase.y() + dotUnit(face.v1, cube.size);
                }
                if (qFuzzyIsNull(uEnd - uStart) || qFuzzyIsNull(vEnd - vStart))
                    continue;   // 零厚度面的贴图区也是零面积，画出来只会是一条线

                QVector3D cornerPos[4];
                QVector2D cornerUv[4];
                for (int c = 0; c < 4; ++c)
                {
                    const float *corner = face.corner[c];
                    QVector3D p = cube.origin
                                  + QVector3D(corner[0] ? cube.size.x() : 0.0f,
                                              corner[1] ? cube.size.y() : 0.0f,
                                              corner[2] ? cube.size.z() : 0.0f);
                    if (!qFuzzyIsNull(cube.inflate))
                    {
                        p += QVector3D(corner[0] ? cube.inflate : -cube.inflate,
                                       corner[1] ? cube.inflate : -cube.inflate,
                                       corner[2] ? cube.inflate : -cube.inflate);
                    }
                    // 立方体自身旋转（绕 pivot / 盒子中心）
                    if (!cube.rot.isNull())
                        p = rotateEuler(p - cubePivot, cube.rot) + cubePivot;
                    // 骨骼旋转：模型里的 pivot 是「绝对坐标」，静态姿势下每根骨骼只绕
                    // 自己的 pivot 转，不向父骨骼累积（父骨骼的摆位已经写进子骨骼的
                    // pivot 与盒子里了）。所以这里不往上走父链，否则腿会被身体的
                    // 90° 旋转带跑，整只猪会陷到地面以下。
                    const EntityBone &boneRef = bone;
                    if (boneRef.hasRot)
                        p = rotateEuler(p - boneRef.pivot, boneRef.rot) + boneRef.pivot;
                    cornerPos[c] = p;
                    const float u = corner[3] > 0.5f ? uEnd : uStart;
                    const float v = corner[4] > 0.5f ? vEnd : vStart;
                    // 保留像素坐标：渲染端按实例贴图的真实尺寸归一化
                    cornerUv[c] = QVector2D(u, v);
                }

                // 缩放到「格」，并按原版约定把模型 x 镜像（左右肢体才对得上）
                for (int c = 0; c < 4; ++c)
                    cornerPos[c] = QVector3D(-cornerPos[c].x() * kUnit,
                                             cornerPos[c].y() * kUnit,
                                             cornerPos[c].z() * kUnit);

                const QVector3D n = QVector3D::crossProduct(cornerPos[1] - cornerPos[0],
                                                            cornerPos[3] - cornerPos[0]);
                const float nlen = n.length();
                const QVector3D normal = (nlen > 1e-6f) ? (n / nlen) : QVector3D(0.0f, 1.0f, 0.0f);

                // 四边形拆两个三角形（保持与 prismarine-viewer 相同的顶点顺序）
                const int order[2][3] = { { 0, 1, 2 }, { 2, 1, 3 } };
                for (const auto &tri : order)
                {
                    EntityMeshTriangle t;
                    for (int k = 0; k < 3; ++k)
                    {
                        t.pos[k] = cornerPos[tri[k]];
                        t.uv[k] = cornerUv[tri[k]];
                    }
                    t.normal = normal;
                    out.append(t);
                }
            }
        }
    }
    // 落地对齐：各生物模型写的高度基准并不统一（羊、船这类整体偏高一截），
    // 统一把最低点压到 y = 0，实体按脚底坐标摆进投影时就不会悬空
    if (!out.isEmpty())
    {
        float minY = out.first().pos[0].y();
        for (const EntityMeshTriangle &tri : out)
        {
            for (const QVector3D &p : tri.pos)
                minY = qMin(minY, p.y());
        }
        if (!qFuzzyIsNull(minY))
        {
            for (EntityMeshTriangle &tri : out)
            {
                for (QVector3D &p : tri.pos)
                    p.setY(p.y() - minY);
            }
        }
    }

    return out;
}
