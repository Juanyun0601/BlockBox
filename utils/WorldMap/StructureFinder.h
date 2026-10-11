/**
 * @file   StructureFinder.h
 * @brief  按种子计算结构位置（基于 Cubiomes，MIT）
 * @author BlockBox Team
 * @date   2026-10-11
 *
 * 种子 → 结构坐标全图预测：封装 third_party/cubiomes 的 `getStructurePos()`，
 * 按「结构区域网格」遍历（每种结构的 spacing/separation/salt 都由 cubiomes 的
 * 版本配置表给出），只保留当前维度命中的结果。
 *
 * 注意：`find()` 应传**视口级范围**（视口 + 边距）。cubiomes 的计算很快，
 * 但区域网格数随范围平方增长，调用方负责把范围控制在看得到的区域。
 */

#ifndef STRUCTUREFINDER_H
#define STRUCTUREFINDER_H

#include <QString>
#include <QVector>

namespace StructureFinder {

/**
 * 要塞（Stronghold）不在 cubiomes 的 StructureType 枚举里，
 * 用这个哨兵 type 表示 —— `find()` 内部走 StrongholdIter 环形迭代。
 */
constexpr int kStrongholdType = -1;

/** 一个可开关的结构类型（第二行开关的数据源） */
struct StructureDef
{
    int type;                ///< cubiomes 的 StructureType 枚举值（或 kStrongholdType）
    const char *key;         ///< 稳定标识（缓存 / 设置用）
    const char *displayName; ///< 中文显示名
    const char *wikiTitle;   ///< Wiki 条目标题（页面主图，作"结构概览"图标来源）
    const char *itemTitle;   ///< Wiki 物品条目标题（更清晰的小图标来源）
    const char *envSprite;   ///< 生成结构表文字旁的 16px 小图标文件名（EnvSprite_*）
    bool rare;               ///< 稀有/重要结构：地图缩小时也保留显示
};

/** 全部结构类型（含主世界 / 下界 / 末地，运行时按维度过滤） */
const QVector<StructureDef> &allStructures();

/**
 * 版本串（如 "1.21.11" / "1.16.5"）→ cubiomes 的 MCVersion 枚举值。
 * 解析不出合法版本时返回 MC_UNDEF(0)（此时 `find()` 会直接返回空）。
 */
int versionToInt(const QString &mcVersion);

/** 一次命中的结构 */
struct Hit
{
    int x = 0;        ///< 方块坐标 X
    int z = 0;        ///< 方块坐标 Z
    int structIdx = -1; ///< allStructures() 下标
};

/**
 * 按种子在方块范围 [minX,minZ] ~ [maxX,maxZ] 内查找结构。
 *
 * @param seed     世界种子（只需低 48 位）
 * @param mc       versionToInt() 的结果
 * @param dim      cubiomes 维度值：0=主世界 / -1=下界 / +1=末地
 * @param enabled  结构下标 → 是否参与计算
 * @return 命中列表（坐标为方块坐标）
 */
QVector<Hit> find(quint64 seed, int mc, int dim,
                  qint64 minX, qint64 minZ, qint64 maxX, qint64 maxZ,
                  const QVector<bool> &enabled);

} // namespace StructureFinder

#endif // STRUCTUREFINDER_H
