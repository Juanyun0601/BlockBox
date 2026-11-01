#pragma once

#include <QVector>
#include <QtGlobal>

/**
 * 原版（1.21.4）树木特征复刻引擎。
 *
 * 复刻 ChunkGenerator.applyBiomeDecoration 的 step9 植被阶段里所有产树
 * placed_feature：装饰种子 -> 逐特征重播种 -> 放置修饰链 -> 树木特征。
 * 只在只有高度图（无方块数据）的离线预测场景下工作：地形高度用作
 * OCEAN_FLOOR，树木自身用一个稀疏的局部占位表判断空间。
 */
namespace TreeGen
{
constexpr qint16 kNoCanopy = -32000;

/** 该版本是否有可用的树木数据表。 */
bool supported(int mc);

/**
 * 把区域内的树冠写入 top/kind。
 * @param top     尺寸 grid×grid，行优先（r = z, c = x），无树处写 kNoCanopy
 * @param kind    树叶颜色索引（TreePredictor::Kind），无树处 0
 * @param originX 区域左上角方块 X（= regionX*512）
 * @param originZ 区域左上角方块 Z
 * @param seed    世界种子
 * @param biomeIds 尺寸 grid×grid 的群系 id（cubiomes，行优先）
 * @param ys      尺寸 grid×grid 的地表高度（OCEAN_FLOOR - 1 或地表 y）
 */
void generate(QVector<qint16> &top, QVector<quint8> &kind,
              int originX, int originZ, int grid, quint64 seed,
              const QVector<int> &biomeIds, const QVector<float> &ys);
} // namespace TreeGen
