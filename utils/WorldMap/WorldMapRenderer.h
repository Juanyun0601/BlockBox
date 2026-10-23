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
#include <QStringList>
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

/** 地表着色方式（与存档地图着色下拉的选项顺序一致） */
enum class ShadeMode
{
    BlockColor = 0, ///< 按地表方块自身颜色（叠加高度明暗）
    Height     = 1, ///< 按地表高度灰度
    Terrain    = 2  ///< 地形图：按海拔分层设色 + 山体阴影（水面/雪冰保持本色）
};

/**
 * @brief 种子预测参数：对未生成的区块按种子渲染近似地形预览
 *
 * 画布 = 已探索范围外扩 padRegions ∪（中心 ± radiusRegions 的方形窗口）——
 * 视角中心向四周约一万格都有预测地形，不只贴着已探索边界；
 * 真实数据永远优先。mc 为 StructureFinder::versionToInt() 的结果。
 */
struct SeedPreview
{
    bool enabled = false;
    quint64 seed = 0;
    int mc = 0;                 ///< cubiomes 版本号；0 = 读不出 → 不预测
    int padRegions = 3;         ///< 已探索边界向外扩的 region 圈数
    int radiusRegions = 20;     ///< 以中心无条件铺开的半径（region 数；20×512 = 10240 格）
    bool hasCenter = false;     ///< 中心 region（视角中心；无画布时退回出生点）
    int centerRegionX = 0;
    int centerRegionZ = 0;
};

/** 渲染结果 */
struct RenderResult
{
    QImage image;             ///< 俯视地表图；未生成的区块为透明
    int minChunkX = 0;        ///< 图像左上角对应的区块坐标 X（世界方块 X = minChunkX * 16）
    int minChunkZ = 0;        ///< 图像左上角对应的区块坐标 Z
    int blockPerPixel = 1;    ///< 每像素代表的方块数（世界过大时 > 1）
    int regionCount = 0;      ///< 需渲染的区域总数（文件区域 + 种子预测区域）
    int renderedRegions = 0;  ///< 实际渲染成功的区域数（含种子预测）
    int seedRegions = 0;      ///< 其中按种子预测渲染的区域数
    int reusedRegions = 0;    ///< 从上一张画布直接搬像素、未重渲的区域数
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

/**
 * @brief 上一张同设置画布：种子重定心时把重叠 region 原样搬过来
 *
 * 视角离开种子窗口后只需补新露出来的环，不必整图重读 .mca / 重算群系。
 * bpp 必须与本次渲染一致，否则像素对不齐。
 */
struct PriorCanvas
{
    QImage image;
    int minChunkX = 0;
    int minChunkZ = 0;
    int blockPerPixel = 0; ///< ≤0 = 不可用
    bool valid() const
    {
        return !image.isNull() && blockPerPixel > 0;
    }
};

/** 维度对应的 region 目录（绝对路径），存档目录为空时返回空串 */
QString regionDir(const QString &savePath, Dimension dim);

/** 该维度是否已有生成的区块文件 */
bool hasRegions(const QString &savePath, Dimension dim);

/**
 * @brief 渲染单个 region 为 1:1 图块（512×512，1 像素 = 1 方块）
 *
 * 视口细节层用：缩小先看概览（render() 的整图），放大后再按需补这块的精细图。
 * 须在工作线程调用；该 region 不存在 / 空文件 / 损坏时 —— 若传入了可用的
 * seed 预览参数则改为返回种子预测图，否则返回空图。
 * 调用方应把空结果记为"无内容"，别反复重试同一块。
 */
QImage renderRegionTile(const QString &savePath, Dimension dim,
                        int regionX, int regionZ, ShadeMode mode,
                        int maxSurfaceY = -1,
                        const SeedPreview &seed = SeedPreview());

/**
 * @brief 一处方块命中（矿物扫描等按名查方块的用途）
 *
 * index 由调用方定义：scanRegionBlocks() 回传 blockNames 的下标。
 * y 为方块世界高度，x/z 为方块水平坐标（region 内偏移已换算成世界坐标）。
 */
struct BlockHit
{
    int x = 0;
    int y = 0;
    int z = 0;
    int index = -1; ///< blockNames 下标；<0 = 无效
};

/**
 * @brief 扫描单个 region 里指定方块出现的方块坐标（须在工作线程调用）
 *
 * 与 renderRegionTile 共用同一套 Anvil / NBT 解析：逐区块读调色板，只有调色板里
 * 出现过目标方块名的 section 才解码方块数组，因此"扫矿物"不会比渲染这张图更贵。
 * 旧格式（1.12 及以前）按 legacyIds 里的数字 ID 匹配。
 *
 * @param blockNames 目标方块名（含或不含 minecraft: 前缀；状态属性会被忽略）
 * @param legacyIds  与 blockNames 对齐的旧版数字 ID（-1 = 该方块无旧版 ID）
 * @param cancelled  返回 true 时中止（已扫描部分仍会返回）
 * @return 命中列表；单 region 命中数有上限保护（异常存档不会吃爆内存）
 */
QVector<BlockHit> scanRegionBlocks(const QString &savePath, Dimension dim,
                                   int regionX, int regionZ,
                                   const QStringList &blockNames,
                                   const QVector<int> &legacyIds = QVector<int>(),
                                   const std::function<bool()> &cancelled = {});

/**
 * @brief 以区块为单位扫描指定方块（须在工作线程调用）
 *
 * 只解压、解析被点名的区块，不碰同 region 里的其他区块 —— 放大看局部时
 * 不必为一个区块去读整块 512×512 的地图数据。chunks 里的区块按 region 分组，
 * 同一个 .mca 只打开一次，所以成批传入相邻区块不会额外读盘。
 *
 * @param chunks 区块坐标（chunkX/chunkZ，即方块坐标 >> 4）
 * @return 命中列表；index 为 blockNames 下标
 */
QVector<BlockHit> scanChunksBlocks(const QString &savePath, Dimension dim,
                                   const QVector<QPoint> &chunks,
                                   const QStringList &blockNames,
                                   const QVector<int> &legacyIds = QVector<int>(),
                                   const std::function<bool()> &cancelled = {});

/** @brief 单个区块的 scanChunksBlocks() 便捷重载 */
QVector<BlockHit> scanChunkBlocks(const QString &savePath, Dimension dim,
                                  int chunkX, int chunkZ,
                                  const QStringList &blockNames,
                                  const QVector<int> &legacyIds = QVector<int>(),
                                  const std::function<bool()> &cancelled = {});

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
 * @param seed        种子预测：无文件的区域按种子渲染近似地形（enabled=false 时行为不变）
 * @param prior       上一张画布（可选）：bpp 一致时重叠 region 直接搬像素，跳过重渲
 */
RenderResult render(const QString &savePath, Dimension dim, ShadeMode mode,
                    const std::function<bool()> &cancelled,
                    const std::function<void(int, int)> &progress,
                    const ProgressiveSinkPtr &sink = nullptr,
                    int maxThreads = 0,
                    int maxSurfaceY = -1,
                    const SeedPreview &seed = SeedPreview(),
                    const PriorCanvas &prior = PriorCanvas());

/** 方块 ID（含或不含 minecraft: 前缀）→ 地表颜色 */
QColor blockColor(const QString &blockName);

// ── 地形模式（ShadeMode::Terrain）共用纯函数：真实地表与种子预测同款 ──

/** 海拔 → 分层设色（低地绿 → 山地棕 → 雪顶白），返回不透明像素色 */
QRgb terrainTint(int y, int minY, int maxY);

/**
 * 山体阴影系数：西北 45° 定向光照在高度梯度上，平地 = 1。
 * dzDx/dzDy 为相邻采样之间的高度差（格 / 采样），已按采样步长折算。
 */
qreal terrainHillshade(qreal dzDx, qreal dzDy);

/** 雪 / 冰等浅色地表保持本色（地形模式不按海拔给它们改色，雪顶恒白） */
bool terrainKeepsBase(QRgb c);

} // namespace WorldMapRenderer

#endif // WORLDMAPRENDERER_H
