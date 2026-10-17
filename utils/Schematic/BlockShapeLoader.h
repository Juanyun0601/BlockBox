/**
 * @file   BlockShapeLoader.h
 * @brief  从实例客户端 jar 解析方块真实形状（blockstate + model）
 * @author BlockBox Team
 * @date   2026-08-18
 *
 * 从对应游戏实例的客户端 jar 中读取：
 *   - assets/minecraft/blockstates/<block>.json   方块状态 -> 模型选择
 *   - assets/minecraft/models/block/<name>.json      模型几何（parent 继承、elements、textures）
 * 并据此重建每个"方块状态"的真实形状（一组 AABB + 每面贴图），用于不完整方块的渲染
 * （台阶/楼梯/栅栏/按钮/压力板/花/火把/门/活板门/玻璃板等）。
 *
 * JSON 解析深度：完整 parent 继承链、variants / multipart 属性匹配、elements(from/to/rotation/faces)、
 * 纹理变量(#xxx)解析、面级 uv 子区域，以及 blockstate 级 x/y 旋转（活板门等按朝向正确显示）。
 *
 * 启动提速：不再一次性解压 jar 里全部 blockstate/model（数千文件），而是先扫一次中心目录
 * 建索引，再在真正需要某个方块形状时才按需读取；同一 jar 进程内共享一份。
 *
 * 由宿主持有，供投影方块编辑器渲染与拾取使用；无可解析形状时由调用方回退为完整立方体。
 */
#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>

/**
 * @brief 单个面的贴图信息（模型元素的一个面）
 */
struct ShapeFace
{
    QString dir;        // up / down / north / south / east / west
    QString texture;    // 解析后的贴图 key，如 "block/stone"；空表示颜色回退
    float   uv[4] = {0.0f, 0.0f, 16.0f, 16.0f}; // 模型面在贴图上的子区域（0..16 像素，MC 约定）
    bool    hasUv = false;   // 模型是否显式给出 uv（否则整张贴图铺满该面）
};

/**
 * @brief 一个立方体元素（对应 model 的一个 element / AABB）
 */
struct ShapeBox
{
    float from[3] = {0,0,0};
    float to[3] = {1,1,1};
    bool  rotated = false;      // 是否带旋转
    int   rotAxis  = 1;         // 0=x 1=y 2=z
    float rotAngle = 0.0f;      // 度
    float rotOrigin[3] = {0.5f,0.5f,0.5f};
    QList<ShapeFace> faces;     // 各面的贴图
    bool isEmpty() const { return to[0]-from[0] <= 1e-4f || to[1]-from[1] <= 1e-4f || to[2]-from[2] <= 1e-4f; }
};

/**
 * @brief 方块状态对应的形状（若干元素叠加）
 */
struct BlockShapes
{
    QList<ShapeBox> boxes;
    bool empty() const { return boxes.isEmpty(); }
};

/**
 * @brief 方块形状加载器
 */
class BlockShapeLoader
{
public:
    /**
     * @brief 从实例目录建立 jar 索引（不解压内容）
     * @param instancePath 实例的 versions/<版本> 目录（内含客户端 .jar）
     * @return 找到 jar 并成功建立索引返回 true
     */
    bool load(const QString &instancePath, QString *errorOut = nullptr);

    /**
     * @brief 取进程内共享加载器（同一 jar 只建一次索引）
     */
    static BlockShapeLoader *sharedForInstance(const QString &instancePath);

    bool isValid() const { return m_loaded; }

    /**
     * @brief 解析方块状态名（含属性，如 minecraft:oak_stairs[facing=...]）的真实形状
     * @return true 表示解析到非空形状；false 表示无形状（调用方回退）
     */
    bool shapeFor(const QString &blockStateName, BlockShapes &out) const;

    /** 预热一批方块状态形状（打开编辑器时按调色板调用，避免首帧大量随机 IO） */
    void prewarm(const QStringList &blockStateNames) const;

private:
    // ---- 内部数据结构 ----
    struct ZipEntryMeta
    {
        quint32 offset = 0;
        quint16 method = 0;
        quint32 csize = 0;
        quint32 usize = 0;
    };

    struct ModelJson
    {
        QString parent;
        QMap<QString, QString> textures;   // 纹理变量名 -> 引用(var 或直接贴图 key)
        bool hasElements = false;
        QJsonObject elemObj;               // 保留原始 elements 数组（延迟解析）
    };

    // 解析一个模型(含 parent 链)得到元素
    void collectElements(const QString &modelKey, const QMap<QString, QString> *texOverride,
                         QList<ShapeBox> &boxes, QStringList &visited) const;

    bool ensureBlockstate(const QString &type) const;
    bool ensureModel(const QString &key) const;
    bool readEntry(const QString &entryPath, QByteArray &data) const;

    QString m_jarPath;
    mutable QMap<QString, ZipEntryMeta> m_index; // 相对 jar 路径 -> 元数据
    mutable QMap<QString, ModelJson> m_models;     // 模型名(不带 assets/minecraft/) -> 模型
    mutable QMap<QString, QJsonObject> m_blockstates; // 方块名 -> blockstate JSON
    mutable QSet<QString> m_missingBlockstates;
    mutable QSet<QString> m_missingModels;

    bool m_loaded = false;
};
