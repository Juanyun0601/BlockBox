/**
 * @file   WorldMapRenderer.h
 * @brief  存档地表地图渲染器（Anvil region → 俯视地表图）
 * @author BlockBox Team
 * @date   2026-10-08
 *
 * 读取存档目录下 region 目录里的 r.X.Z.mca（Anvil 区域文件），逐区块解出
 * 地表最上层可见方块，按方块颜色 + 高度明暗绘制俯视图。
 *
 * 区域文件结构（文件头位置表 / 分段存储 / zlib 压缩）、区块 NBT 的
 * sections + block_states 调色板位打包解析，参考开源项目
 * MCA Selector（https://github.com/Querz/mcaselector）的实现思路。
 *
 * 线程模型：所有接口都是无状态的纯函数，可直接放到工作线程执行，
 * 由调用方负责把结果带回 UI 线程。
 */

#ifndef WORLDMAPRENDERER_H
#define WORLDMAPRENDERER_H

#include <QColor>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

namespace WorldMapRenderer {

/** 支持的维度（对应存档目录下的 region 位置） */
enum class Dimension
{
    Overworld = 0,  ///< 主世界 <save>/region
    Nether    = 1,  ///< 下界   <save>/DIM-1/region
    End       = 2   ///< 末地   <save>/DIM1/region
};

/** 地表着色方式 */
enum class ShadeMode
{
    BlockColor = 0, ///< 按地表方块自身颜色（叠加高度明暗）
    Height     = 1  ///< 按地表高度灰度
};

/** 渲染结果 */
struct RenderResult
{
    QImage image;             ///< 俯视地表图；未生成的区块为透明
    int minChunkX = 0;        ///< 图像左上角对应的区块坐标 X（世界方块 X = minChunkX * 16）
    int minChunkZ = 0;        ///< 图像左上角对应的区块坐标 Z
    int blockPerPixel = 1;    ///< 每像素代表的方块数（世界过大时 > 1）
    int regionCount = 0;      ///< 扫描到的区域文件数
    int renderedRegions = 0;  ///< 实际渲染成功的区域数
    int failedRegions = 0;    ///< 读取失败的区域数
    int elapsedMs = 0;        ///< 渲染耗时（毫秒）
    int key = -1;             ///< 调用方打的标记（如 维度×着色），用于丢弃过期渲染结果
    QString error;            ///< 非空表示无法渲染（如该维度还没有区块）
};

/**
 * @brief 渐进渲染上下文：渲染线程写、UI 线程定期取，实现"边渲染边显示"
 *
 * 约定：
 *  - 图像布局字段（imageWidth/…）在 layoutReady 置位前写入、置位后只读；
 *  - pendingTiles 由渲染线程 append、UI 线程 drain，必须持 tileMutex；
 *  - 整个对象用 shared_ptr 持有，渲染线程在页面析构后仍可安全写入。
 */
struct ProgressiveSink
{
    std::atomic<bool> cancel{ false };     ///< 置 true 中止渲染
    std::atomic<int> done{ 0 };            ///< 已完成区域数
    std::atomic<int> total{ 0 };           ///< 总区域数
    std::atomic<bool> layoutReady{ false };///< 布局字段已就绪

    int imageWidth = 0;
    int imageHeight = 0;
    int blockPerPixel = 1;
    int minChunkX = 0;
    int minChunkZ = 0;

    QMutex tileMutex;
    /** 待贴图区块：first = 最终图上的像素坐标，second = 已按 blockPerPixel 缩放的图块 */
    QList<QPair<QPoint, QImage>> pendingTiles;

    /**
     * 渐进渲染的优先区域：region 渲染顺序按到它的切比雪夫距离由近及远
     * （即"从视角中心往四周一圈圈长"）。未设置时保持文件名顺序。
     */
    bool hasPriorityRegion = false;
    QPoint priorityRegion;
};

using ProgressiveSinkPtr = std::shared_ptr<ProgressiveSink>;

/** 维度对应的 region 目录（绝对路径），存档目录为空时返回空串 */
QString regionDir(const QString &savePath, Dimension dim);

/** 该维度是否已有生成的区块文件 */
bool hasRegions(const QString &savePath, Dimension dim);

/**
 * @brief 渲染单个 region 为 1:1 图块（512×512，1 像素 = 1 方块）
 *
 * 视口细节层用：缩小先看概览（render() 的整图），放大后再按需补这块的精细图。
 * 须在工作线程调用；该 region 不存在 / 空文件 / 损坏时返回空图 ——
 * 调用方应把空结果记为"无内容"，别反复重试同一块。
 */
QImage renderRegionTile(const QString &savePath, Dimension dim,
                        int regionX, int regionZ, ShadeMode mode,
                        int maxSurfaceY = -1);

/**
 * @brief 阻塞渲染整张地表地图（须在工作线程调用）
 *
 * 区域文件之间多线程并行（参考 MCA Selector 的多线程渲染管线），每完成一个
 * 区域就：① 写进返回值用的主图；② 有 sink 时挂进 pendingTiles 供 UI 实时贴图。
 *
 * @param savePath    存档目录（含 level.dat 的目录）
 * @param dim         维度
 * @param mode        着色方式
 * @param cancelled   返回 true 时中止渲染（已渲染部分仍会返回）；与 sink->cancel 是或的关系
 * @param progress    进度回调（已完成区域数, 总区域数），sink 存在时同样会写 sink->done/total
 * @param sink        渐进渲染上下文；非空时先写布局再逐区域推图块
 * @param maxThreads  并行区域数上限；<=0 用 QThreadPool 默认线程数，1 = 单线程
 */
RenderResult render(const QString &savePath, Dimension dim, ShadeMode mode,
                    const std::function<bool()> &cancelled,
                    const std::function<void(int, int)> &progress,
                    const ProgressiveSinkPtr &sink = nullptr,
                    int maxThreads = 0,
                    int maxSurfaceY = -1);

/** 方块 ID（含或不含 minecraft: 前缀）→ 地表颜色 */
QColor blockColor(const QString &blockName);

} // namespace WorldMapRenderer

#endif // WORLDMAPRENDERER_H
