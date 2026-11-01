/**
 * @file   MushroomPredictor.h
 * @brief  蘑菇岛巨型蘑菇位置的精确预测（1.18+）
 * @author BlockBox Team
 * @date   2026-10-29
 *
 * 蘑菇岛（mushroom_fields）的巨型蘑菇来自群系装饰阶段的
 * mushroom_island_vegetation 特征：每个区块恰好掷一次，
 * 位置、红/棕、伞盖高度都由世界种子与区块坐标唯一确定。
 *
 * 原版算法（1.18+）：
 *   1. decorationSeed = setDecorationSeed(worldSeed, chunkOriginX, chunkOriginZ)
 *   2. featureSeed    = decorationSeed + featureIndex + 10000 * 9（装饰步骤 9）
 *   3. 依次取 x=nextInt(16)、z=nextInt(16)、kind=nextBoolean、
 *      h=nextInt(3)+4（nextInt(12)==0 时翻倍）
 *   4. 蘑菇基座取该位置 MOTION_BLOCKING 地表高度
 *
 * featureIndex 是原版特征的全局排序序号（由数据包注册顺序决定，各版本
 * 可能不同），这里按版本查表：1.18.2/1.19.4 = 66、
 * 1.20.6/1.21.1/1.21.3 = 68、1.21.4 = 72；未核对过的版本不给出预测。
 */

#ifndef MUSHROOMPREDICTOR_H
#define MUSHROOMPREDICTOR_H

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace MushroomPredictor {

/** 巨型蘑菇种类 */
enum Kind : quint8 {
    Brown = 0, // 棕色巨型蘑菇
    Red = 1,   // 红色巨型蘑菇
    KindCount
};

/** 一个区块里的蘑菇预测结果 */
struct Mushroom
{
    int localX = 0; ///< 区块内 0..15
    int localZ = 0; ///< 区块内 0..15
    Kind kind = Red;
    int height = 0; ///< 菌柄高度（方块），伞盖在基座上方 height 处
};

/** 版本是否支持（1.18+ 才换成 Xoroshiro 随机源） */
bool supported(int mc);

/** 该版本 mushroom_island_vegetation 的特征序号（原版特征排序） */
int featureIndex(int mc);

/**
 * @brief 预测一个区块的巨型蘑菇
 * @param seed  世界种子
 * @param mc    cubiomes 版本号（StructureFinder::versionToInt）
 * @param chunkX,chunkZ 区块坐标
 * @return 该区块的蘑菇（原版每区块至多一个，但保留列表便于扩展）
 */
QVector<Mushroom> predictChunk(quint64 seed, int mc, int chunkX, int chunkZ);

/**
 * @brief 在 region 采样栅格上叠加蘑菇伞盖（俯视看到的是伞盖顶面）
 *
 * 仅对群系为蘑菇岛的格子生效；伞盖尺寸按原版：棕蘑菇 7×7（去四角）、
 * 红蘑菇 5×5。写入 top 的是伞盖顶面 Y，kind 用 KindCount+kind 编码，
 * 与树冠图例区分。
 *
 * @param top    伞盖顶面 Y，尺寸 grid×grid，无蘑菇的格子保持不变
 * @param kind   伞盖种类编码，与 top 同步写入
 * @param originX/originZ 栅格左上角对应的方块坐标
 * @param grid   栅格边长，覆盖 512×512 方块
 * @param ids    每格群系 id（cubiomes BiomeID）
 * @param ys     每格地表高度（方块）
 */
void overlayCaps(QVector<qint16> &top, QVector<quint8> &kind,
                 int originX, int originZ, int grid,
                 quint64 seed, int mc,
                 const QVector<int> &ids, const QVector<float> &ys);

/** 伞盖方块（伞顶材质）名称，供色表取色 */
QString blockName(Kind kind);

constexpr quint8 kKindBase = 100; ///< canopyKind 里蘑菇的编码偏移
constexpr qint16 kNoCap = -32000;
/**
 * 落点近似地表高度的下限（海平面 63 往下留一点余量）。
 * 巨型蘑菇只会长在水面以上的陆地：落在水下时原版会因脚印格是水而放弃，
 * 种子预览不画这一株，免得在浅水处凭空多出蘑菇。
 */
constexpr int kMinSurfaceY = 61;

} // namespace MushroomPredictor

#endif // MUSHROOMPREDICTOR_H

