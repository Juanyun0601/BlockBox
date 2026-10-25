/**
 * @file   StructureLoot.h
 * @brief  结构战利品：解析原版战利品表并按世界种子掷取
 * @author BlockBox Team
 * @date   2026-10-26
 *
 * 对齐 Minecraft 战利品表算法（池 / 条目权重 / 轮数 / 数量区间 / empty），
 * 随机源用 1.18+ Xoroshiro128++；1.20.2+ 的 random_sequence 按原版派生。
 * 箱子盐用结构坐标混合，保证同一种子同一位置结果稳定。
 * 数据来自原版 data/minecraft/loot_table/chests（内嵌 + 可选客户端 JAR）。
 */

#ifndef STRUCTURELOOT_H
#define STRUCTURELOOT_H

#include <QList>
#include <QString>
#include <QStringList>

namespace StructureLoot {

/** 一组堆叠（物品名 + 数量） */
struct Stack
{
    QString name;
    QString itemId; ///< minecraft:xxx（调试 / 后续图标用）
    int count = 1;
};

/** 一次掷取结果 */
struct Result
{
    QList<Stack> stacks;   ///< 合并后堆叠（按数量降序）
    int chests = 0;        ///< 模拟箱子数
    bool fromSeed = false; ///< true = 按种子掷取
    QString tableIds;      ///< 使用的战利品表 id（逗号分隔，调试用）
};

/**
 * 按种子模拟该结构实例战利品（对齐原版权重 / 轮数）。
 * @param structKey StructureFinder::StructureDef::key
 * @param worldSeed 世界种子
 * @param hasSeed   无种子时回退概览
 * @param mcMinor   MC 次版本号（16/18/20/21…），用于随机源代际；0=默认最新
 */
Result rollLoot(const QString &structKey, quint64 worldSeed, bool hasSeed,
                qint64 x = 0, qint64 z = 0, int mcMinor = 21);

/** 常见物品概览（无种子回退） */
QStringList lootFor(const QString &structKey);

/** 设置客户端 JAR 路径：优先从 JAR 读 data/minecraft/loot_table/chests */
void setClientJar(const QString &jarPath);

} // namespace StructureLoot

#endif // STRUCTURELOOT_H
