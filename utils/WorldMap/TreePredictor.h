/**
 * @file   TreePredictor.h
 * @brief  未生成区块的树木/树冠位置预测
 * @author BlockBox Team
 * @date   2026-10-28
 *
 * 原版的树不是噪声直接长出来的，而是"装饰阶段"撒出来的：每个区块按群系的
 * BiomeGenerationSettings 掷若干棵树，位置用 LegacyRandomSource 以
 * setDecorationSeed(levelSeed, blockX, blockZ) 定出，再在 16×16 区块内
 * InSquarePlacement 散点。这里复刻同一套种子派生 + 散点思路，配合群系密度表
 * 和树冠形状，算出俯视看到树冠顶面高度，供种子地形预测叠加。
 *
 * 说明：真实世界的树位置还取决于特征序号、装饰步骤与放置时的随机串，
 * 这里只保证"密度、树种、位置分布"与原版同构，不逐格对齐——预测图也
 * 一直标着"以游戏内实际地形为准"。
 */

#ifndef TREEPREDICTOR_H
#define TREEPREDICTOR_H

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace TreePredictor {

/** 树种（只区分树冠外观，不区分方块细节） */
enum Kind : quint8 {
    Oak = 0,
    Birch,
    Spruce,
    Jungle,
    Acacia,
    DarkOak,
    Cherry,
    PaleOak,
    Mangrove,
    SwampOak,
    KindCount
};

/** 该格没有树冠 */
constexpr qint16 kNoCanopy = -32000;

/** 版本是否支持预测（主世界树木各版本都有，只要版本可识别） */
bool supported(int mc);

/**
 * 在 region 范围的采样栅格上叠加树冠。
 *
 * @param top    树冠顶面 Y，尺寸 grid×grid；无树为 kNoCanopy。进入时须已 resize
 * @param kind   树冠树种（Kind），尺寸 grid×grid，与 top 同步写入
 * @param originX/originZ 栅格左上角对应的方块坐标（region 起点）
 * @param grid   栅格边长，覆盖 512×512 方块
 * @param ids    每格群系 id（cubiomes BiomeID），尺寸 grid×grid
 * @param ys     每格地表高度（方块），尺寸 grid×grid
 */
void overlayCanopy(QVector<qint16> &top, QVector<quint8> &kind,
                   int originX, int originZ, int grid,
                   quint64 seed, int mc,
                   const QVector<int> &ids, const QVector<float> &ys);

/** 树冠叶块名（WorldMapRenderer::blockColor 的键） */
QString leafBlockName(int kind);

/** 稳定的方块级噪声（0..2^32-1），做叶面颗粒与树冠轮廓抖动 */
quint32 cellNoise(int blockX, int blockZ, quint64 seed);

} // namespace TreePredictor

#endif // TREEPREDICTOR_H
