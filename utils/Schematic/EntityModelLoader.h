/**
 * @file   EntityModelLoader.h
 * @brief  投影里的生物 / 实体模型（基岩版几何格式，取自 prismarine-viewer）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 原版 Java 客户端只把生物贴图放在 jar 里（textures/entity），生物模型是写死在
 * 游戏代码里的，没有可解析的文件。所以这里用 PrismarineJS/prismarine-viewer
 * （MIT）整理好的基岩版几何模型表（resources/projection_entity_models.json），
 * 再配实例 jar 里的实体贴图渲染，这样投影里保存的生物也能在启动器里看到。
 *
 * 模型单位是 1/16 格，骨骼（bone）给出 pivot 与 bind_pose_rotation，立方体
 * （cube）给出 origin / size / uv / rotation / inflate。
 *
 * 渲染约定（与 prismarine-viewer 一致）：
 *   - 骨骼旋转取反（原版是 y 向下的模型空间，这里统一成 y 向上）；
 *   - 立方体绕自身中心（或显式 pivot）旋转；
 *   - 整模型 x 镜像，让左右肢体与原版一致；
 *   - 生成结果脚底在 y = 0，面朝 -Z，单位换算成「格」。
 *
 * 无模型的实体（掉落物、末地水晶等）直接跳过，不影响方块渲染。
 */
#pragma once

#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

/**
 * @brief 一个立方体（模型里的一个盒子）
 */
struct EntityCube
{
    QVector3D origin;              ///< 最小角（1/16 格）
    QVector3D size;                ///< 三边长度（1/16 格，可为 0：十字贴片）
    QVector3D rot;                 ///< 立方体自身旋转（度，已取反）
    QVector3D pivot;               ///< 旋转中心（1/16 格）
    bool hasPivot = false;         ///< 是否给了显式 pivot（否则绕盒子中心转）
    bool mirror = false;
    float inflate = 0.0f;          ///< 外扩（盔甲层 / 外套）
    QVector2D uv;                  ///< 贴图左上角偏移（像素）
    bool perFaceUv = false;        ///< 少数扁平模型按面给 uv
    QMap<QString, QVector2D> faceUv;      ///< 面 -> uv 偏移
    QMap<QString, QVector2D> faceUvSize;  ///< 面 -> uv 尺寸覆盖（可负）
};

/**
 * @brief 一根骨骼（可挂若干立方体，可带父骨骼）
 */
struct EntityBone
{
    QString name;
    QString parent;
    QVector3D pivot;               ///< 旋转中心（1/16 格，绝对坐标）
    QVector3D rot;                 ///< bind_pose_rotation（度，已取反）
    bool hasRot = false;
    bool mirror = false;
    QVector<EntityCube> cubes;
};

/**
 * @brief 一个实体的模型
 */
struct EntityModel
{
    QString id;                    ///< 实体 ID，如 "minecraft:pig"
    QString textureKey;            ///< 贴图键，如 "entity/pig/pig"（对应 jar 里的 .png）
    QString chineseName;           ///< 中文名（有则用于悬停提示）
    int texWidth = 64;
    int texHeight = 64;
    /** 模型用到的贴图区最大边界（像素），用来判断模型与实例贴图是否对得上 */
    float uvLimitU = 0.0f;
    float uvLimitV = 0.0f;
    QVector<EntityBone> bones;

    bool isValid() const { return !bones.isEmpty(); }
};

/**
 * @brief 模型里生成出来的一个三角面（模型空间，单位：格）
 */
struct EntityMeshTriangle
{
    QVector3D pos[3];
    /**
     * 贴图坐标：**像素**（原版模型就是这么写 uv 的，如 16,16）。
     * 不在这里按模型声明的贴图尺寸归一化——实例里的贴图实际尺寸可能和模型
     * 声明的不一致（僵尸这类模型声明 64×32，现代贴图是 64×64，上半部分布局
     * 相同），按像素交给渲染端除以真实贴图尺寸才不会错位。
     */
    QVector2D uv[3];
    QVector3D normal;              ///< 模型空间法线（已归一化）
};

/**
 * @brief 实体模型表（进程内只解析一次，线程安全）
 */
class EntityModelLoader
{
public:
    /** 进程内单例 */
    static EntityModelLoader *instance();

    /** 模型表是否加载成功 */
    bool isValid() const;

    /** 模型种类数 */
    int modelCount() const;

    /**
     * @brief 取某个实体的模型
     * @param entityId 实体 ID（"minecraft:pig" / "pig" / 带命名空间或大写都行）
     * @param out      输出模型
     * @return 有对应模型返回 true（调用方的兜底：不画）
     */
    bool modelFor(const QString &entityId, EntityModel &out) const;

    /** 去掉命名空间并转小写："minecraft:Zombie" -> "zombie" */
    static QString shortName(const QString &entityId);

private:
    EntityModelLoader() = default;
    bool ensureLoaded() const;

    mutable bool m_loaded = false;
    mutable QHash<QString, EntityModel> m_models;
};

/**
 * @brief 把模型展开成三角面（脚底在 y = 0，面朝 -Z，单位格）
 *
 * 直接给 OpenGL / 软渲染两条管线复用；贴图坐标还是模型自身的 0..1，
 * 由调用方按图集矩形再换算，这样同一份几何可以被不同图集复用。
 */
QVector<EntityMeshTriangle> buildEntityMesh(const EntityModel &model);
