/**
 * @file   StructureTemplates.h
 * @brief  读取原版（Java 版客户端自带）结构模板 NBT，供结构预览逐块还原
 * @author BlockBox Team
 * @date   2026-10-28
 *
 * 数据来源：官方客户端 jar 内的 `data/minecraft/structure/**.nbt`
 * （即数据包结构与结构方块导出的模板格式：size / palette / blocks）。
 * 模板为 gzip 压缩的 NBT，解析复用 NbtCodec。
 *
 * 模板里的辅助方块按原版放置规则处理：
 *  - `minecraft:structure_void`：跳过（保持原有方块）；
 *  - `minecraft:structure_block`：跳过（仅作标记）；
 *  - `minecraft:jigsaw`：按方块实体里的 `final_state` 出方块（原版放置时即以
 *    final_state 替换拼图方块），没有 final_state 时跳过。
 */

#ifndef STRUCTURETEMPLATES_H
#define STRUCTURETEMPLATES_H

#include <QString>
#include <QVector>
#include <memory>

struct NbtTag;

/** 模板内的单个方块（坐标相对模板原点） */
struct StructureTemplateBlock
{
    int x = 0;
    int y = 0;
    int z = 0;
    int state = -1;         ///< palette 索引
    QString stateOverride;  ///< 非空时替代 palette 结果（拼图方块的 final_state）
};

/** 模板内的拼图方块（多段结构拼装时用） */
struct StructureTemplateJigsaw
{
    int x = 0;
    int y = 0;
    int z = 0;
    QString name;         ///< 拼图点自身名称
    QString target;       ///< 需要连接的拼图点名称
    QString pool;         ///< 目标模板池
    QString finalState;   ///< 放置后实际显示的方块状态
    QString joint;        ///< aligned / rollable
    QString orientation;  ///< north_up 等朝向（取自方块属性）
    int placementPriority = 0; ///< 子段在队列中的优先级（原版 placement_priority）
    int selectionPriority = 0; ///< 排序优先级（原版 selection_priority）
};

/**
 * @brief 一个原版结构模板（不可变，按 id 缓存）
 */
class StructureTemplate
{
public:
    /**
     * @brief 按 id 载入模板（如 "igloo/top"、"shipwreck/with_mast"）
     * @return 载入失败返回 nullptr；结果全局缓存，重复调用不会重复解析
     */
    static std::shared_ptr<const StructureTemplate> load(const QString &id);

    QString id() const { return m_id; }
    int sizeX() const { return m_sizeX; }
    int sizeY() const { return m_sizeY; }
    int sizeZ() const { return m_sizeZ; }

    /** 单个方块的可显示方块状态；空表示该格跳过（结构空位 / 标记方块） */
    QString stateOf(int paletteIndex) const
    {
        return (paletteIndex >= 0 && paletteIndex < m_palette.size())
            ? m_palette.at(paletteIndex)
            : QString();
    }

    const QVector<StructureTemplateBlock> &blocks() const { return m_blocks; }
    const QVector<StructureTemplateJigsaw> &jigsaws() const { return m_jigsaws; }

private:
    bool parseFromNbt(const NbtTag &root);

    QString m_id;
    int m_sizeX = 0;
    int m_sizeY = 0;
    int m_sizeZ = 0;
    QVector<QString> m_palette;
    QVector<StructureTemplateBlock> m_blocks;
    QVector<StructureTemplateJigsaw> m_jigsaws;
};

#endif // STRUCTURETEMPLATES_H
