/**
 * @file   MineshaftModel.h
 * @brief  按原版废弃矿井拼装规则（MineshaftStructure / MineshaftPieces）逐块重建
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 原版废弃矿井没有结构模板 NBT：主厅、巷道、路口、楼梯全部由 Java 代码在
 * 生成区块时算出，方块摆放还要看周围地形（哪里是空气、顶上是实心还是空）。
 * 本模块把 1.21.4 的 MineshaftStructure / MineshaftPieces 完整移植：
 *
 *  - 布局随机源：WorldgenRandom(LegacyRandomSource) +
 *    setLargeFeatureSeed(seed, 起始区块)，与游戏内一致；
 *  - 主厅尺寸、四个方向的延伸口、巷道/路口/楼梯的类型抽取（100 面骰、
 *    碰撞检测、8 段上限、±80 格范围）都按原版顺序消耗随机数；
 *  - 整座矿井最后按原版 StructurePiecesBuilder.shiftInto 下移到地下
 *    （坐标与游戏内一致，可精确到 y）；
 *  - 逐块摆放时原版的每条分支（巷道清空、木支架、栏杆、铁轨、蜘蛛网、
 *    火把、箱子矿车、刷怪笼、上下贯通的立柱/铁链）都照抄，随机抽取与
 *    游戏内同种子、同区块一致。
 *
 * 预览没有地形数据：矿井周围的石头按「实心」处理（矿井本身挖空的地方
 * 记为空气），这与游戏里埋在地下时的判断一致；地形留下的普通石头不会
 * 导出到模型里，只保留矿井本身的木料、铁轨、蜘蛛网等方块。
 */
#ifndef MINESHAFTMODEL_H
#define MINESHAFTMODEL_H

#include <QHash>
#include <QString>
#include <QVector>

namespace MineshaftModel {

/** 一段拼装结果的包围盒（供对照调试） */
struct PieceBox
{
    QString type;             ///< 段类型（room / corridor / crossing / stairs）
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
    int x0 = 0, y0 = 0, z0 = 0;    ///< 相对坐标范围（含，y 为世界坐标）
    int x1 = -1, y1 = -1, z1 = -1;
    int startX = 0, startZ = 0;    ///< 参考原点（起始区块最小方块坐标）
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
 * @brief 按种子与起始区块重建废弃矿井
 * @param seed         世界种子
 * @param startChunkX  矿井起始区块 X（StructureFinder 返回的 x >> 4）
 * @param startChunkZ  矿井起始区块 Z
 * @param mcVersion    游戏版本（如 "1.21.4"；1.13 以前的矿井地台不同，
 *                     低于 1.13 时返回错误，由调用方回退到示意模型）
 * @param placementStep  落块随机源用的装饰阶段序号（内部参数，校验用）
 * @param placementIndex 落块随机源用的结构序号（内部参数，校验用）
 * @param worldMinY    世界最低 y（默认主世界 -64，内部参数）
 * @param seaLevel     海平面（默认 63，内部参数）
 * @param mesa         是否使用恶地变体（深色橡木 + 按地表下移）
 */
Model build(quint64 seed, int startChunkX, int startChunkZ,
            const QString &mcVersion = QStringLiteral("1.21.4"),
            int placementStep = 3, int placementIndex = 1,
            int worldMinY = -64, int seaLevel = 63, bool mesa = false);

} // namespace MineshaftModel

#endif // MINESHAFTMODEL_H
