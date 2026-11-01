/**
 * @file   FortressModel.h
 * @brief  按原版下界要塞拼装规则（NetherFortressPieces）逐块重建要塞
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 原版下界要塞没有结构模板 NBT，桥段、走廊、烈焰人房间、下界疣楼梯塔
 * 全部由 Java 代码在生成区块时算出。本模块把 1.21.4 的
 * NetherFortressStructure / NetherFortressPieces 完整移植：
 *
 *  - 布局随机源：WorldgenRandom(LegacyRandomSource) +
 *    setLargeFeatureSeed(seed, 起始区块)，与游戏内完全一致；
 *  - 起段朝向、桥段/城堡段的权重抽取、重复上限、碰撞检测、
 *    队列随机取段，都按原版顺序消耗随机数；
 *  - 整座要塞最后按原版 moveBelowSeaLevel 下移到 48~70 的高度；
 *  - 每一段的方块摆放（含桥墩向下填充、栏杆朝向、楼梯朝向、
 *    箱子、刷怪笼、岩浆池、下界疣农田）按原版逐行还原。
 *
 * 生成结果为世界坐标下的方块表，坐标以起始区块原点为参考，交给
 * StructureModelBuilder 包装成可预览的模型。
 */
#ifndef FORTRESSMODEL_H
#define FORTRESSMODEL_H

#include <QHash>
#include <QString>
#include <QVector>

namespace FortressModel {

/** 一段拼装结果的包围盒（供对照调试） */
struct PieceBox
{
    QString type;             ///< 段类型（bridge_crossing / corridor / throne 等）
    int x0 = 0, y0 = 0, z0 = 0;
    int x1 = -1, y1 = -1, z1 = -1;
};

/** 生成结果 */
struct Model
{
    bool ok = false;
    QString error;                 ///< 失败原因
    QString variant;               ///< 变体说明
    /** 方块表：键为相对起始区块原点的坐标（见 packKey），值为方块状态串 */
    QHash<qint64, QString> blocks;
    int x0 = 0, y0 = 0, z0 = 0;    ///< 相对坐标范围（含）
    int x1 = -1, y1 = -1, z1 = -1;
    int startX = 0, startZ = 0;    ///< 相对坐标参考原点（世界方块坐标）
    int blockCount = 0;
    QVector<PieceBox> pieces;      ///< 各段的包围盒
};

/**
 * @brief 按键打包相对坐标（供块表使用）
 * @param x 相对 startX 的 X（±511）
 * @param y 世界 Y
 * @param z 相对 startZ 的 Z（±511）
 */
inline qint64 packKey(int x, int y, int z)
{
    return (qint64(x + 512) & 1023)
         | ((qint64(z + 512) & 1023) << 10)
         | ((qint64(y + 512) & 1023) << 20);
}

/** 解开 packKey（相对坐标 / 世界 Y / 相对坐标） */
inline void unpackKey(qint64 k, int &x, int &y, int &z)
{
    x = int(k & 1023) - 512;
    z = int((k >> 10) & 1023) - 512;
    y = int((k >> 20) & 1023) - 512;
}

/**
 * @brief 按种子与起始区块重建下界要塞
 * @param seed         世界种子
 * @param startChunkX  要塞起始区块 X（StructureFinder 返回的 x >> 4）
 * @param startChunkZ  要塞起始区块 Z
 * @param mcVersion    游戏版本（如 "1.21.4"；1.16 以前的要塞布局不同，
 *                     低于 1.16 时返回错误，由调用方回退到示意模型）
 */
Model build(quint64 seed, int startChunkX, int startChunkZ,
            const QString &mcVersion = QStringLiteral("1.21.4"));

} // namespace FortressModel

#endif // FORTRESSMODEL_H
