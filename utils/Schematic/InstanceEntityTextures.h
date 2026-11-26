/**
 * @file   InstanceEntityTextures.h
 * @brief  从游戏实例的客户端 jar 加载生物 / 实体贴图（打包成一张图集）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 与 InstanceTextureLoader（方块贴图，16×16 一格）分开：
 * 实体贴图是「展开的皮肤」，尺寸各异（64×32、64×64、128×128…），
 * 压到 16×16 会糊成一团，所以这里按原始像素尺寸单独拼一张图集，
 * 每张贴图的矩形原样返回，渲染时按像素换算 UV 即可。
 *
 * 素材来源：实例的客户端 jar（assets/minecraft/textures/entity 目录下），
 * 所以换实例 / 换版本后生物外观跟着变（材质跟随当前所选实例）。
 *
 * 村民 / 僵尸村民在 jar 里是「底图 + 生物群系外观 + 职业外观」三层，后两层
 * 大部分像素透明，这里合成成一张再进图集，渲染端照旧按职业那层的键取图。
 *
 * 同一 jar 进程内只解析一次，供三维预览与三视图渲染器共用。
 */
#pragma once

#include <QHash>
#include <QImage>
#include <QRect>
#include <QString>

/**
 * @brief 实例实体贴图图集
 */
class InstanceEntityTextures
{
public:
    /**
     * @brief 取进程内共享图集（同一 jar 只加载一次；无缓存时同步构建）
     * @param instancePath 实例的 versions/<版本> 目录
     * @return 共享实例；失败返回空指针
     */
    static InstanceEntityTextures *sharedForInstance(const QString &instancePath);

    bool isValid() const { return !m_atlas.isNull() && m_textureCount > 0; }

    /** 图集图像（RGBA8888） */
    QImage atlasImage() const { return m_atlas; }
    int atlasSize() const { return m_atlasSize; }
    int textureCount() const { return m_textureCount; }

    /**
     * @brief 按模型里的贴图键取图集矩形（像素坐标）
     * @param key     如 "entity/pig/pig"、"textures/entity/steve"、"zombie"
     * @param rect    输出矩形（图集内位置）
     * @param sourceSize 可选：该贴图在图集里的实际像素尺寸（用于把模型里的
     *               像素级 uv 换算进图集；通常等于原始贴图尺寸）
     * @return 找到返回 true
     */
    bool rectForKey(const QString &key, QRect &rect, QSize *sourceSize = nullptr) const;

    /** 规范化贴图键（去 "textures/" 前缀、去扩展名、小写） */
    static QString normalizeKey(const QString &key);

private:
    InstanceEntityTextures() = default;
    bool load(const QString &jarPath);

    /** 图集里一条贴图：位置 + 实际像素尺寸 */
    struct TexEntry
    {
        QRect rect;
        QSize size;
    };

    QImage m_atlas;
    int m_atlasSize = 0;
    int m_textureCount = 0;
    QHash<QString, TexEntry> m_entries;      ///< 完整键 -> 贴图
    QHash<QString, QString> m_leafToKey;     ///< 末段名（pig / steve） -> 完整键
};
