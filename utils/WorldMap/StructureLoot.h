/**
 * @file   StructureLoot.h
 * @brief  结构战利品预测：按原版战利品表推算每件物品的出现概率
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 做法对齐原版 1.21 的战利品表算法（池 / 轮数 rolls / 权重 / 空条目 /
 * 嵌套表 / 条件 / 物品函数），数据取自官方客户端 data/minecraft/loot_table
 * （宝箱 56 张 + 考古 3 张）。箱子内容取决于生成时掷出的 LootTableSeed，
 * 该种子无法从世界种子反推，因此这里给出的是「该种子下这处结构每个箱子
 * 里出现某物品的概率」：
 *  - 出现概率 ≥ 95% 的物品标记为「必出」（必出清单的实测命中率 ≥ 95%）；
 *  - 其余物品按概率从高到低排列，并给出期望数量。
 * 概率为精确解析解（含嵌套表的表用大样本蒙特卡洛 + Wilson 置信下界）。
 */

#ifndef STRUCTURELOOT_H
#define STRUCTURELOOT_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace StructureLoot {

/** 一组堆叠（物品名 + 期望数量 + 出现概率） */
struct Stack
{
    QString name;         ///< 显示名（含附魔 / 药水等后缀）
    QString itemId;       ///< minecraft:xxx（图标 / 调试用）
    int count = 1;        ///< 期望数量（四舍五入，至少 1）
    double chance = 1.0;  ///< 该箱出现这件物品的概率（0~1）
    int minCount = 0;     ///< 出现时的最少数量
    int maxCount = 1;     ///< 出现时的最多数量
    bool guaranteed = false; ///< 概率 ≥ 95%，标记为「必出」
    bool enchanted = false;  ///< 掷出时带附魔（附魔书 / 附魔装备）
    QString note;            ///< 附魔 / 药水效果等说明
};

/** 一类箱子的预测结果 */
struct ChestLoot
{
    QString tableId;     ///< 战利品表 id（调试 / 悬停提示用）
    QString label;       ///< 人类可读名称，如「铁匠铺」
    int count = 1;       ///< 该结构里这类箱子有几处
    QList<Stack> stacks; ///< 单个箱子内的物品概率（按概率降序）
};

/** 一次预测结果 */
struct Result
{
    QList<Stack> stacks;         ///< 「任一宝箱」混合概率（按概率降序）
    QList<Stack> totals;         ///< 该结构全部宝箱合计：至少出现一次的概率
    QList<ChestLoot> chestLoots; ///< 按箱子种类拆开的明细
    int chests = 0;              ///< 该结构的宝箱总数
    bool fromSeed = false;       ///< true = 已按种子定位到该结构
    QString tableIds;            ///< 使用的战利品表 id（逗号分隔，调试用）
};

/**
 * 预测该结构实例的战利品。
 * @param structKey StructureFinder::StructureDef::key
 * @param worldSeed 世界种子（当前概率模型不依赖它，保留用于结构定位语义）
 * @param hasSeed   是否读到世界种子（false 时只给常见物品概览）
 * @param x/z       结构坐标（保留）
 * @param mcMinor   MC 次版本号（保留）
 */
Result rollLoot(const QString &structKey, quint64 worldSeed, bool hasSeed,
                qint64 x = 0, qint64 z = 0, int mcMinor = 21);

/** 常见物品名列表（无种子回退，供旧接口使用） */
QStringList lootFor(const QString &structKey);

/**
 * 注入当前实例的物品中文名（来自客户端 JAR lang 的 item.*）。
 * 键为完整资源名 "minecraft:diamond"，值为显示名。空表 = 清空。
 * 应在后台线程调用 load 后、GUI 线程写入；内部有锁。
 */
void setInstanceItemNames(const QHash<QString, QString> &zh,
                          const QHash<QString, QString> &en);

/** 清空实例物品名（换存档 / 无实例时） */
void clearInstanceItemNames();

/** 设置客户端 JAR 路径（预留；当前战利品表只读内嵌 qrc） */
void setClientJar(const QString &jarPath);

} // namespace StructureLoot

#endif // STRUCTURELOOT_H
