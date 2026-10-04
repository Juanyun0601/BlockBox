/**
 * @file   InstanceTextureLoader.h
 * @brief  从游戏实例的客户端 jar 加载方块材质（texture atlas）
 * @author BlockBox Team
 * @date   2026-08-18
 *
 * 从实例的版本 jar 中提取 `assets/minecraft/textures/block` 下的方块贴图（.png），
 * 打包成一张 OpenGL 纹理图集（每格 16×16），并提供"方块名 -> 图集 UV 矩形"映射。
 *
 * 实例路径约定（与启动器一致）：
 *   <实例根>/.minecraft/versions/<版本>/<版本>.jar   （共享 .minecraft）
 *   或版本隔离时 <实例根>/versions/<版本>/<版本>.jar
 * 传入的 instancePath 即该 versions/<版本> 目录，本类在该目录下查找 .jar。
 *
 * 由宿主持有，供投影方块编辑器（原生插件界面）渲染真实材质使用；
 * 未映射到贴图的方块由调用方以颜色回退。
 */
#pragma once

#include <QImage>
#include <QMap>
#include <QString>
#include <QStringList>

/**
 * @brief 实例方块材质加载器（生成纹理图集）
 */
class InstanceTextureLoader
{
public:
    /**
     * @brief 从指定实例目录加载方块贴图并构建图集
     * @param instancePath 实例的 versions/<版本> 目录（内含客户端 .jar）
     * @param errorOut     可选失败原因
     * @return 成功返回 true；找不到 jar/无贴图返回 false
     */
    bool load(const QString &instancePath, QString *errorOut = nullptr);

    bool isValid() const { return !m_atlas.isNull() && m_textureCount > 0; }

    /** 构建好的图集图像（32 位 RGB） */
    QImage atlasImage() const { return m_atlas; }

    /** 图集每行贴图数量 */
    int tilesPerRow() const { return m_tilesPerRow; }

    /** 已加载的贴图数量 */
    int textureCount() const { return m_textureCount; }

    /** 图集尺寸（正方形边，像素） */
    int atlasSize() const { return m_atlasSize; }

    /**
     * @brief 根据方块名返回对应贴图格，是否成功
     * @param blockName  方块状态名，如 "minecraft:stone" 或带属性 "minecraft:oak_log[axis=y]"
     * @param tile       输出：贴图格下标（如图集中位置；调用方按 tilesPerRow 计算 UV）
     * @return true 表示有对应贴图；false 表示回退到颜色
     */
    bool textureFor(const QString &blockName, int &tile) const;

    /**
     * @brief 图集 UV 矩形（u0,v0,u1,v1），供渲染取样
     */
    void tileUv(int tile, float &u0, float &v0, float &u1, float &v1) const;

    /**
     * @brief 根据模型贴图 key（如 "block/oak_planks"、"minecraft:block/stone"）返回对应贴图格
     * @param modelKey 模型 face 的贴图引用（解析 #var 之后的最终值）
     * @param tile     输出贴图格下标
     * @return true 表示有对应贴图；false 表示回退到颜色
     */
    bool tileForKey(const QString &modelKey, int &tile) const;

private:
    // 方块名规范化：去 "minecraft:" 前缀与属性 [..]
    static QString normalizeName(const QString &blockName);

    /** 按基础名（去掉路径/前缀）查找贴图格，带 _top/_side/_planks 变体回退 */
    bool resolveByName(const QString &base, int &tile) const;

    bool findClientJar(const QString &instancePath, QString &jarPath) const;
    void buildAtlas(const QMap<QString, QImage> &textures);

    QImage   m_atlas;
    int      m_tilesPerRow = 0;
    int      m_atlasSize = 0;
    int      m_textureCount = 0;
    QMap<QString, int> m_nameToTile;   // 规范化方块名 -> 贴图格下标
};
