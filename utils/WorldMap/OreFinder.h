/**
 * @file   OreFinder.h
 * @brief  存档矿物扫描：矿物表 + 按 region 统计矿物分布
 * @author BlockBox Team
 * @date   2026-10-18
 *
 * 存档地图「内容浏览」弹窗的矿物列表数据源：
 *  - allOres()：可扫描的矿物（主世界 / 下界），带中文名、Wiki 标题与标记色
 *    （图标复用 StructureIcons 的 Wiki 缓存，key 前缀 ore_）
 *  - scanRegion()：读一个 region 文件，输出其中所有矿物的方块坐标
 *
 * 扫描不引入第三方依赖：直接复用 WorldMapRenderer 的 Anvil / NBT 解析
 * （解析思路参考开源 MCA Selector，https://github.com/Querz/mcaselector）。
 * 逐区块取调色板，只有调色板里出现过目标矿物的 section 才解码方块数组，
 * 因此大范围扫描的开销主要集中在真正生成过矿物的区块上。
 *
 * 线程模型：与 StructureFinder / BiomeCatalog 一致，接口为无状态纯函数，
 * 内部只读存档文件，可直接放工作线程执行。
 */

#ifndef OREFINDER_H
#define OREFINDER_H

#include <QColor>
#include <QString>
#include <QVector>

#include <functional>

#include "WorldMapRenderer.h"

namespace OreFinder {

/** 一种可扫描的矿物（弹窗矿物列表的数据源） */
struct OreDef
{
    const char *key;            ///< 稳定标识（弹窗排序 / 图标缓存 key 用）
    const char *displayName;    ///< 中文显示名
    const char *wikiTitle;      ///< Wiki 条目标题（pageimages 主图，作列表图标）
    const char *blockName;      ///< 现代版本的方块名（不含 minecraft: 前缀）
    const char *deepslateName;  ///< 深层变体方块名；无则 nullptr
    int legacyId;               ///< 1.12 及以前的数字 ID；-1 = 无
    int dim;                    ///< 所属维度：0=主世界 / -1=下界 / +1=末地
    QColor color;               ///< 地图标记色
};

/** 可扫描矿物全集（运行时按当前维度过滤显示） */
const QVector<OreDef> &allOres();

/** 一处矿物命中；index = allOres() 下标（与扫描时传入的矿物集无关） */
using Hit = WorldMapRenderer::BlockHit;

/**
 * 扫描单个 region 文件里的全部矿物（一次读出所有种类，勾选集变化无需重扫）。
 * @param savePath 存档目录；dim 决定读哪个维度的 region 目录
 * @param regionX/regionZ region 坐标（对应 r.<x>.<z>.mca）
 * @param cancelled 可选中止回调（返回 true 则尽快返回已收集的结果）
 * @return 命中列表（无文件 / 无矿物时为空）
 */
QVector<Hit> scanRegion(const QString &savePath, WorldMapRenderer::Dimension dim,
                        int regionX, int regionZ,
                        const std::function<bool()> &cancelled = {});

} // namespace OreFinder

#endif // OREFINDER_H
