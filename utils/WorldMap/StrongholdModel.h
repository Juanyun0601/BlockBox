/**
 * @file   StrongholdModel.h
 * @brief  按原版要塞拼装规则（StrongholdPieces）逐块重建要塞
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 原版要塞没有结构模板 NBT，房间布局、朝向、楼梯/门/铁栏以及石砖的
 * 裂纹/苔藓变体全部由 Java 代码在生成区块时算出。本模块把 1.18+
 * （1.21.4 校对）的 StrongholdStructure / StrongholdPieces 逻辑完整移植：
 *
 *  - 布局随机源：WorldgenRandom(LegacyRandomSource) +
 *    setLargeFeatureSeed(seed + i, 起始区块)，与游戏内完全一致；
 *  - 每段的重选与占地由原版权重表 / 碰撞检测决定；
 *  - 石砖的裂纹、苔藓、虫蚀变体由「逐区块」的装饰随机源决定
 *    （世界种子 → 装饰种子 → 结构步种子），因此相邻区块里同样的石砖
 *    会像游戏里那样出现不同的花纹；
 *  - 房间整体高度按原版 moveBelowSeaLevel 下移到海平面以下。
 *
 * 生成结果为世界坐标下的方块表，坐标以起始区块原点为参考，交给
 * StructureModelBuilder 包装成可预览的模型。
 */
#ifndef STRONGHOLDMODEL_H
#define STRONGHOLDMODEL_H

#include <QHash>
#include <QString>
#include <QVector>

namespace StrongholdModel {

/** 一段拼装结果的包围盒（供对照调试） */
struct PieceBox
{
    QString type;             ///< 段类型（straight / library / portal_room 等）
    int x0 = 0, y0 = 0, z0 = 0;
    int x1 = -1, y1 = -1, z1 = -1;
};

/** 生成结果 */
struct Model
{
    bool ok = false;
    QString error;                 ///< 失败原因
    QString variant;               ///< 变体说明（如「要塞 · 末地传送门」）
    /** 方块表：键为相对起始区块原点的坐标（见 packKey），值为方块状态串 */
    QHash<qint64, QString> blocks;
    int x0 = 0, y0 = 0, z0 = 0;    ///< 相对坐标范围（含）
    int x1 = -1, y1 = -1, z1 = -1;
    int startX = 0, startZ = 0;    ///< 相对坐标参考原点（世界方块坐标）
    int blockCount = 0;
    bool hasPortalRoom = false;    ///< 是否找到末地传送门房间
    QVector<PieceBox> pieces;      ///< 各段的包围盒（世界坐标基准见 startX）

    /** blocks 的键解码（相对起始区块原点） */
    static int keyX(qint64 key) { return int(key & 0xFFF) - 2048; }
    static int keyZ(qint64 key) { return int((key >> 12) & 0xFFF) - 2048; }
    static int keyY(qint64 key) { return int((key >> 24) & 0xFFF) - 2048; }
};

/**
 * @brief 按种子与起始区块重建要塞
 * @param seed        世界种子
 * @param startChunkX 要塞起始区块 X（cubiomes 的 StrongholdIter.pos 为
 *                    该区块内 (4,4) 方块，换算方式：chunkX = (pos.x - 4) / 16）
 * @param startChunkZ 要塞起始区块 Z
 * @param seaLevel    海平面（主世界 63）
 * @param minYLevel   世界最低 Y（1.18+ 为 -64）
 */
Model build(quint64 seed, int startChunkX, int startChunkZ,
            int seaLevel = 63, int minYLevel = -64);

} // namespace StrongholdModel

#endif // STRONGHOLDMODEL_H
