/**
 * @file   WorldMapPage.h
 * @brief  存档地表地图页面（由存档列表的「地图」按钮进入）
 * @author BlockBox Team
 * @date   2026-10-08
 *
 * 顶部为地图操作区（维度 / 着色 / 网格 / 缩放 / 坐标跳转 / 导出 / 刷新），
 * 下方是可拖拽缩放的画布，渲染由 WorldMapRenderer 在工作线程完成。
 *
 * 区域文件解析与俯视渲染思路参考开源项目
 * MCA Selector（https://github.com/Querz/mcaselector）。
 */

#ifndef WORLDMAPPAGE_H
#define WORLDMAPPAGE_H

#include <QFutureWatcher>
#include <QHash>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QImage>
#include <QList>
#include <QThreadPool>
#include <QWidget>
#include <atomic>
#include <memory>

#include "utils/WorldMap/WorldMapRenderer.h"

class QComboBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class WorldMapView;

class WorldMapPage : public QWidget
{
    Q_OBJECT

public:
    explicit WorldMapPage(QWidget *parent = nullptr);
    ~WorldMapPage() override;

    /** 载入指定存档（路径指向存档目录）并开始渲染 */
    void setCurrentSave(const QString &saveFolderPath, const QString &saveName);
    /** 清空（切换实例时调用，避免残留上一个存档的地图） */
    void reset();

    /** 顶栏刷新按钮 / F5：重新渲染当前维度 */
    Q_INVOKABLE void refresh();

    /** 后台是否正在渲染（探针/外部用于等待渐进渲染结束） */
    bool isRendering() const { return m_busy; }

    /** 已加载的 1:1 细节瓦片数（探针 / 状态显示用） */
    int loadedDetailTiles() const;

    /** 当前地图图像（与画布共享同一块像素，不拷贝）——探针取样用 */
    QImage mapImage() const { return m_liveImage; }

    /**
     * 打开地图时横向固定的区块数：不做整图自适应，
     * 以出生点为中心显示这么大一片（全览交给「适应窗口」/ 0）
     */
    static constexpr int kInitialVisibleChunks = 64;

    /** 视口中心所在的方块坐标（探针用来验证初始中心 = 出生点） */
    QPoint viewCenterBlock() const;

    /** 已加载细节瓦片的顺序（LRU 序 = 加载序，探针用来验证"由中心向外"） */
    QList<QPoint> loadedTileOrder() const;

    /** 取消当前选中的区块（右键菜单也会调用） */
    Q_INVOKABLE void clearSelection();

    // ── 结构标记（第二行开关 + 按种子计算）：探针断言用 ──
    /** 结构开关按钮数（= allStructures() 全集） */
    int structureButtonCount() const { return m_structBtns.size(); }
    /** 第 i 个结构开关是否勾选（默认全选） */
    bool structureChecked(int i) const;
    /** 当前画布上的结构标记数（后台查询完成后才有值） */
    int structureMarkerCount() const;
    /** 等待结构后台查询收敛（探针用；返回是否在超时前收敛） */
    bool waitForStructs(int timeoutMs = 5000);

    // ── 结构信息弹窗与完成标记：探针断言用 ──
    /** 第 idx 个标记的屏幕中心（无效 / 视口外返回 QPoint()） */
    QPoint markerScreenPos(int idx) const;
    /** 第 idx 个标记的方块坐标（打包为 QPoint(x, z)） */
    QPoint markerBlockPos(int idx) const;
    /** 第 idx 个标记的结构类型（allStructures() 下标；无效 -1） */
    int markerType(int idx) const;
    /** 已标记完成的结构数（当前存档） */
    int structureDoneCount() const { return m_structDone.size(); }
    /** 该结构是否已标记完成 */
    bool isStructureDone(int type, qint64 x, qint64 z) const;

protected:
    /**
     * 地图快捷键统一入口。
     * 焦点在坐标输入框时按键被输入框自己消费、不会冒泡到这里，
     * 因此不会和"输入负号/数字"冲突；Esc / F5 / Home 等留给主窗口的全局快捷键。
     */
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onRenderFinished();
    void onDimensionChanged(int index);
    void onShadeChanged(int index);
    /** 高度下拉 / 输入框变化（下拉选或直接键入 Y） */
    void onHeightChanged();
    /** 按种子 + 当前视口重算结构标记（后台执行） */
    void startStructRefresh();
    /** 收取已完成的结构查询结果 */
    void drainStructResult();
    /** 把结构命中应用到画布 */
    void applyStructHits(const QVector<QPair<int, int>> &coords,
                         const QVector<int> &types);
    /** 远程图标就位后刷新第二行按钮图标 */
    void refreshStructIcons();
    void onGridToggled(bool on);
    void onZoomIn();
    void onZoomOut();
    void onFitClicked();
    /** 一键 1 像素 = 1 方块 */
    void onZoomToOneOne();
    void onGotoClicked();
    void onExportClicked();
    void onRefreshClicked();
    void onPollProgress();
    /** 视口去抖到期：挑选需要补的 1:1 细节 region */
    void scheduleTiles();
    /** 取队列里离视口中心最近的一块交给工作线程 */
    void pumpTileQueue();
    /** 单块细节渲染完成 */
    void onTileFinished();
    /** 队列还有就接着渲，空了就补下一批（由中心向四周扩散） */
    void continueTiling();

private:
    struct RenderJob
    {
        std::atomic<bool> cancel{ false };
        std::atomic<int> done{ 0 };
        std::atomic<int> total{ 0 };
    };

    /** 结构查询任务：worker 写入结果后置 done，UI 轮询收取（无悬空风险） */
    struct StructJob
    {
        std::atomic<bool> done{ false };
        QVector<QPair<int, int>> coords;   ///< (x, z) 方块坐标
        QVector<int> types;                ///< 结构下标，与 coords 对齐
    };

    /** 单区域 1:1 瓦片的渲染结果（跨线程传回 UI） */
    struct TileResult
    {
        QPoint key;    ///< region 坐标
        QImage image;  ///< 512×512；空图 = 该 region 无内容
        int seq = 0;   ///< 批次号，用来丢弃换存档/换维度前的过期结果
    };

    void startRender();
    /** 顶部标题副行：跟随当前维度变化（地表地图 · 主世界） */
    void updateSubtitle();
    void applyResult(const WorldMapRenderer::RenderResult &result);
    /** 渲染线程发布布局后建画布（定时器里调用） */
    void applyLiveLayout();
    /** 把 sink 里的区域图块贴到画布（定时器里调用） */
    void drainPendingTiles();
    void setBusy(bool busy);
    void updateZoomLabel();
    /** 拼状态栏文本：指针坐标 + 选中区块 + 渲染统计（cursor 为空则保留上次指针） */
    void updateStatusLabel(const QString &cursor = QString());
    /** 画布右键：弹地图上下文菜单（复制坐标 / 居中 / 填入跳转 / 取消选择） */
    void showMapContextMenu(const QPoint &globalPos, qint64 blockX, qint64 blockZ);
    /** 点击地图结构图标：弹信息窗（名称 / 坐标 / 百科 / 完成标记） */
    void showStructurePopup(const QPoint &globalPos, int structIdx,
                            qint64 x, qint64 z);
    /** 完成标记写入 QSettings（按存档路径分桶） */
    void persistStructDone() const;
    /** 读当前存档的完成标记到内存并同步到画布 */
    void loadStructDone();
    /** 清掉细节瓦片的调度状态（换存档 / 换维度 / 概览换图时） */
    void clearTileState(bool clearViewTiles);
    /** 记一塊已加载的瓦片并做 LRU 淘汰，同时刷新状态栏"细节 N" */
    void rememberTile(const QPoint &key);
    /** 取缓存的渲染结果，命中返回 true */
    bool takeCached(int key, WorldMapRenderer::RenderResult *out);
    void putCache(int key, const WorldMapRenderer::RenderResult &result);

    QString m_savePath;
    QString m_saveName;
    /** 自定义地表高度上限（-1 = 不限制，显示真实地表） */
    int m_maxSurfaceY = -1;
    /** 种子与版本（cubiomes 版本号；0 = 读不出，不算结构） */
    quint64 m_seed = 0;
    int m_mcVersion = 0;
    bool m_hasSeed = false;
    /** 第二行结构开关：默认全选 */
    QVector<bool> m_structEnabled;
    QList<QPushButton *> m_structBtns;
    /** 出生点：初始视图中心 + 渐进渲染的优先区域 */
    qint64 m_spawnX = 0;
    qint64 m_spawnZ = 0;
    bool m_hasSpawn = false;
    /** 当前存档已标记完成的结构（key = type_x_z，持久化于 QSettings） */
    QSet<QString> m_structDone;
    /** 打开中的结构信息弹窗（Qt::Popup，点外部自动关） */
    QPointer<QWidget> m_structPopup;

    WorldMapView *m_view;
    QStackedWidget *m_stack;
    QLabel *m_titleIcon;
    QLabel *m_saveNameLabel;
    QLabel *m_subtitleLabel;
    QLabel *m_emptyLabel;
    QLabel *m_statusLabel;
    QLabel *m_zoomLabel;
    QProgressBar *m_progress;
    QString m_statSuffix;   ///< 状态栏右侧的渲染统计
    QString m_cursorText;   ///< 状态栏"指针 X.. Z.."片段
    QString m_selectionText;///< 离栏"已选中区块(..)"片段

    QComboBox *m_dimCombo;
    QComboBox *m_shadeCombo;
    /** 可编辑的高度选择框：下拉选预设，也能直接键入自定义 Y */
    QComboBox *m_heightCombo;
    QPushButton *m_gridBtn;
    QPushButton *m_zoomInBtn;
    QPushButton *m_zoomOutBtn;
    QPushButton *m_fitBtn;
    QPushButton *m_oneOneBtn;
    QPushButton *m_gotoBtn;
    QPushButton *m_exportBtn;
    QPushButton *m_refreshBtn;
    QLineEdit *m_gotoX;
    QLineEdit *m_gotoZ;

    QFutureWatcher<WorldMapRenderer::RenderResult> *m_watcher;
    QTimer *m_pollTimer;
    /** 专用渲染线程池：概览与 1:1 细节瓦片共用（串行），不占全局池，
     *  免得和 AI 助手的 QtConcurrent 抢线程 */
    QThreadPool m_renderPool;
    /** 结构查询专用池：与渲染池分离，算标记时不会被大世界渲染堵住 */
    QThreadPool m_structPool;
    std::shared_ptr<StructJob> m_structJob;
    bool m_structDirty = false;   ///< 任务进行中又改了开关 / 视口 → 完成后再算一轮
    /** 单区域 1:1 瓦片渲染结果的回传 */
    QFutureWatcher<TileResult> *m_tileWatcher;
    /** 视口变化去抖：拖动中不反复重算要加载哪些 region */
    QTimer *m_tileTimer;
    std::shared_ptr<RenderJob> m_job;
    /** 每次启动渲染自增；finished 时对不上说明是过期任务的结果，直接丢弃 */
    int m_renderSeq = 0;
    /** 细节瓦片批次号：换存档 / 换维度时递增，过期的瓦片结果直接丢弃 */
    int m_tileSeq = 0;
    /** 排队 + 在途的 region（在途也在里面，避免重复排队） */
    QSet<QPoint> m_tilesPending;
    /** 渲染过但没有内容的 region（空文件 / 未生成），不再重试 */
    QSet<QPoint> m_tilesEmpty;
    /** 细节瓦片 LRU 顺序（最旧在前），超出上限从 view 里剔除 */
    QList<QPoint> m_tileOrder;
    bool m_tileBusy = false;
    QString m_detailText;   ///< 状态栏"细节 N"片段
    /** 渐进渲染上下文：渲染线程推图块，定时器在 UI 线程 drain 贴图 */
    WorldMapRenderer::ProgressiveSinkPtr m_sink;
    /** 渐进渲染画布，WorldMapView 直接引用它（不拷贝，避免逐块贴图时整图 detach） */
    QImage m_liveImage;
    /** 当前画布的布局指纹（宽x高:起点区块:bpp），同布局的重渲染保留旧画面 */
    QString m_liveKey;
    bool m_layoutApplied = false;

    /** 已渲染结果缓存：key = dim * 2 + shadeMode（最多 4 条） */
    QHash<int, WorldMapRenderer::RenderResult> m_cache;
    QList<int> m_cacheOrder;
    int m_renderKey = -1;
    bool m_busy = false;
};

#endif // WORLDMAPPAGE_H
