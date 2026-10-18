/**
 * @file   BiomeCatalog.h
 * @brief  常见群系表 + 按种子采样 / 定位群系（基于 Cubiomes）
 * @author BlockBox Team
 * @date   2026-10-18
 *
 * 存档地图「内容浏览」弹窗的群系列表数据源：
 *  - allBiomes()：常见群系（主世界 / 下界 / 末地），带中文名与 Wiki 标题
 *    （图标复用 StructureIcons 的 Wiki 缓存，key 前缀 biome_）
 *  - sampleGrid()：在给定方块范围内按固定步长采样群系，供地图叠加层着色
 *  - findNearest()：从某点向外环形搜索最近的该群系，供「高亮跳转」定位
 *
 * 线程模型：与 StructureFinder / SeedTerrainRenderer 一致，接口为无状态
 * 纯函数（内部 Generator 按线程缓存），可直接放工作线程执行。
 */

#ifndef BIOMECATALOG_H
#define BIOMECATALOG_H

#include <QColor>
#include <QString>
#include <QVector>

namespace BiomeCatalog {

/** 一个可开关的群系（弹窗群系列表的数据源） */
struct BiomeDef
{
    int id;                 ///< cubiomes 的 BiomeID（基础 id，不含变异）
    const char *key;        ///< 稳定标识（图标缓存 / 设置用）
    const char *displayName;///< 中文显示名
    const char *wikiTitle;  ///< Wiki 条目标题（pageimages 主图，作列表图标）
    int dim;                ///< cubiomes 维度：0=主世界 / -1=下界 / +1=末地
    QColor color;           ///< 地图叠加层的代表色（半透明描边上色用 RGB）
};

/** 常见群系全集（运行时按当前维度过滤显示） */
const QVector<BiomeDef> &allBiomes();

/** 变异群系（id+128）折叠回基础群系；168+ 为独立群系原样返回 */
int baseId(int id);

/**
 * 版本串对应的 cubiomes 版本号（复用 StructureFinder::versionToInt 的结果）。
 * sampleGrid / findNearest 直接接收该 int，避免本模块依赖结构模块。
 */

/** 一次范围采样结果：nx×nz 个格子，每格一个基础群系 id（-1 = 无效） */
struct SampleGrid
{
    QVector<int> ids;  ///< 行主序（i + j * nx）
    int nx = 0;
    int nz = 0;
    qint64 minX = 0;   ///< 第一格左上角的方块坐标
    qint64 minZ = 0;
    int step = 64;     ///< 每格边长（方块）
};

/**
 * 在 [minX,minZ]~[maxX,maxZ] 内按 step 步长采样群系（含右/下边界对齐）。
 * @param dim cubiomes 维度值：0 / -1 / +1；mc 为 versionToInt 的结果
 */
SampleGrid sampleGrid(quint64 seed, int mc, int dim,
                      qint64 minX, qint64 minZ, qint64 maxX, qint64 maxZ,
                      int step = 64);

/**
 * 采样单点群系（基础 id；坐标为方块坐标）。
 * 无种子 / 版本不可用时返回 -1。用于「当前群系」等单点查询。
 */
int biomeAt(quint64 seed, int mc, int dim, qint64 x, qint64 z);

/**
 * 从 (fromX, fromZ) 向外环形搜索最近的该群系出现点。
 * @param maxRadius 搜索半径上限（方块）
 * @return 是否找到；找到时写出方块坐标
 */
bool findNearest(quint64 seed, int mc, int dim, int biomeId,
                 qint64 fromX, qint64 fromZ, qint64 maxRadius,
                 qint64 *outX, qint64 *outZ);

} // namespace BiomeCatalog

#endif // BIOMECATALOG_H
