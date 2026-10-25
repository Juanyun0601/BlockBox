/**
 * @file   WorldMapPage.h
 * @brief  存档地表地图页面（由存档列表的「地图」按钮进入）
 * @author BlockBox Team
 * @date   2026-10-08
 *
 * 顶部为地图操作区（维度 / 着色 / 网格 / 缩放 / 导出，
 * 坐标跳转位于「展开」内容弹窗搜索框右侧；
 * 刷新复用主窗口顶栏刷新按钮），
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
#include <limits>
#include <memory>

#include "utils/WorldMap/BiomeName.h"
#include "utils/WorldMap/OreFinder.h"
#include "utils/WorldMap/WorldMapRenderer.h"
#include "utils/WorldMap/BiomeCatalog.h"

class QComboBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPixmap;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QScrollArea;
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
    /**
     * 设置当前实例（versions/<id> 目录）：从它的客户端 JAR 读群系中文名，
     * 供底部「当前群系」显示使用。实例切换时调用（可在 setCurrentSave 前后）。
     */
    void setInstancePath(const QString &instancePath);
    /** 清空（切换实例时调用，避免残留上一个存档的地图） */
    void reset();

    /** 顶栏刷新按钮 / F5 / R：重新渲染当前维度 */
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

    /**
     * 最近一次概览渲染选定的渐进起步 region（探针用来验证"从视野中心开始渲染"）。
     * 有画布时 = 视野中心所在 region；首次打开存档 = 出生点所在 region；
     * 两者都没有时返回 QPoint(INT_MIN, INT_MIN)。
     */
    QPoint renderPriorityRegion() const { return m_renderPriorityRegion; }

    /** 当前 live 画布左上角所在 region（探针把像素换算成区域坐标用） */
    QPoint liveOriginRegion() const { return QPoint(m_liveMinChunkX / 32, m_liveMinChunkZ / 32); }

    /** 当前 live 画布的每像素方块数（0 = 还没有画布） */
    int liveBpp() const { return m_liveBpp; }

    /** 本次渲染中图块到达画布的顺序（region 坐标，探针验证「中心优先」） */
    QList<QPoint> tileArrivalOrder() const { return m_tileArrivalOrder; }

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

    // ── 内容浏览弹窗（第二行右侧「展开」）：探针断言用 ──
    /** 打开/关闭内容浏览弹窗；返回操作后是否处于打开状态 */
    Q_INVOKABLE bool toggleContentPopup();
    /** 弹窗是否打开 */
    bool isContentPopupOpen() const { return m_contentPopup && m_contentPopup->isVisible(); }
    /** 弹窗群系列表行数（= 当前维度可选群系数） */
    int biomeRowCount() const;
    /** 第 i 行群系是否勾选（默认全不选） */
    bool biomeChecked(int i) const;
    /** 触发「高亮并跳转到最近的该内容」——探针用来验证跳转行为 */
    Q_INVOKABLE bool highlightStructureType(int idx);
    Q_INVOKABLE bool highlightBiome(int idx);
    /** 当前画布上的群系叠加层格子数（有选中群系且算完采样后才有值） */
    int biomeOverlayCellCount() const;

    // ── 内容浏览弹窗·矿物列（扫描按 region 后台进行）：探针断言用 ──
    /** 弹窗矿物列表行数（= 当前维度可选矿物数） */
    int oreRowCount() const;
    /** 第 i 行矿物是否勾选（默认全不选） */
    bool oreChecked(int i) const;
    /** 画布上的矿物点数（放大到扫描门槛并扫完可见区域后才有值） */
    int oreMarkerCount() const;
    /** 勾选 / 取消第 i 行矿物；返回操作后该行是否勾选 */
    Q_INVOKABLE bool toggleOre(int i);
    /** 当前矿物提示文案（「需放大到 100% 以上才会扫描」等；无提示返回空串） */
    QString orePromptText() const { return m_oreText; }
    /** 可见区块是否都扫完了（false = 还在扫，状态栏显示「扫描中」） */
    bool oreScanIdle() const { return !m_oreBusy; }
    /** 已扫过的区块数（按区块缓存，扫过的不会重复读盘） */
    int oreScannedChunkCount() const { return m_oreScanned.size(); }
    /** 第 i 个矿物点的屏幕中心（视口外 / 无效返回 QPoint()） */
    QPoint oreMarkerScreenPos(int i) const;
    /** 第 i 个矿物点的方块坐标（打包 QPoint(x, z)） */
    QPoint oreMarkerBlockPos(int i) const;
    /** 第 i 个矿物点的 Y */
    int oreMarkerBlockY(int i) const;
    /** 第 i 个矿物点的矿物下标（OreFinder::allOres() 下标；无效 -1） */
    int oreMarkerType(int i) const;

protected:
    /**
     * 地图快捷键统一入口。
     * 焦点在坐标输入框时按键被输入框自己消费、不会冒泡到这里，
     * 因此不会和"输入负号/数字"冲突；Esc / F5 / Home 等留给主窗口的全局快捷键。
     */
    void keyPressEvent(QKeyEvent *event) override;

    /** 结构条滚动容器：滚轮在横向滚动条上时转为横滚（QScrollArea 默认只滚纵向） */
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    /** 后台解析当前实例的群系中文名表（lang 文件） */
    void loadBiomeNamesAsync();
    /** 后台采样视口中心的当前群系（去重：任务在途则标记待刷新） */
    void scheduleBiomeRefresh();
    /** 收取当前群系查询结果并刷新底部群系标签 */
    void drainBiomeResult();
    /** 远程图标就位后刷新第二行按钮图标 */
    void refreshStructIcons();
    void onGridToggled(bool on);
    void onSeedToggled(bool on);
    /** 种子画布去抖到期：视角中心离开当前画布窗口 → 以新中心重渲 */
    void onSeedRecenterTimeout();
    void onZoomIn();
    void onZoomOut();
    void onFitClicked();
    /** 一键 1 像素 = 1 方块 */
    void onZoomToOneOne();
    void onGotoClicked();
    void onExportClicked();
    void onPollProgress();
    /** 视口去抖到期：挑选需要补的 1:1 细节 region */
    void scheduleTiles();
    /** 队列还有就接着渲，空了就补下一批（由中心向四周扩散） */
    void continueTiling();
    /** 第二行右侧「展开」按钮：切换内容浏览弹窗 */
    void onExpandToggled(bool on);
    /** 关闭内容弹窗（关闭按钮 / Esc / 点弹窗外） */
    void closeContentPopup();
    /** 结构列「全选 / 清空」（弹窗内按钮与第二行按钮共用） */
    void setAllStructures(bool on);
    /** 群系列「全选 / 清空」（作用于当前列表可见行） */
    void setAllBiomes(bool on);
    /** 弹窗搜索框：过滤结构 / 群系两列列表 */
    void onPopupSearchChanged(const QString &text);
    /** 弹窗结构行勾选变化（与第二行开关双向同步） */
    void onPopupStructToggled(int idx, bool on);
    /** 弹窗群系行勾选变化（驱动群系叠加层） */
    void onPopupBiomeToggled(int idx, bool on);
    /** 弹窗矿物行勾选变化（驱动矿物扫描调度） */
    void onPopupOreToggled(int idx, bool on);
    /** 矿物列「全选 / 清空」（作用于当前维度可见行） */
    void setAllOres(bool on);
    /** 视口 / 缩放去抖到期：安排可见区域里还没扫过的 region */
    void scheduleOreScan();
    /** 收取已完成的 region 扫描结果并刷新画布上的矿物点 */
    void drainOreScan();
    /** 按当前勾选与缓存重建画布矿物点（顺带刷新状态栏提示） */
    void rebuildOreMarkers();
    /** 换存档 / 换维度 / 复位：清空矿物扫描缓存与画布点位 */
    void clearOreState();
    /** 高亮定位任务完成：跳转视角并高亮 */
    void drainLocateResult();
    /** 依据选中群系 + 最新采样网格重建叠加层并刷新画布 */
    void rebuildBiomeOverlay();
    /** 按搜索词 / 维度重建弹窗两列列表 */
    void rebuildPopupLists();

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
        BiomeCatalog::SampleGrid biomeGrid;///< 群系采样网格（叠加层数据源）
    };

    /** 高亮定位任务：找离视口中心最近的该内容并输出坐标 */
    struct LocateJob
    {
        std::atomic<bool> done{ false };
        bool isBiome = false;
        int idx = -1;      ///< 结构 / 群系下标
        bool found = false;
        qint64 x = 0;
        qint64 z = 0;
    };

    /** 当前群系查询任务：采样视口中心单点群系（后台执行，UI 轮询收取） */
    struct BiomeJob
    {
        std::atomic<bool> done{ false };
        int biomeId = -1;  ///< 基础群系 id；-1 = 算不出来
        qint64 x = 0;      ///< 采样点方块坐标
        qint64 z = 0;
    };

    /** 实例群系中文名表解析任务（后台读 jar lang，UI 轮询收取） */
    struct BiomeNameJob
    {
        std::atomic<bool> done{ false };
        BiomeName::InstanceNames names;
    };

    /** 矿物扫描任务：一批区块（同一 region 内），worker 写入命中后置 done，UI 轮询收取 */
    struct OreScanJob
    {
        std::atomic<bool> done{ false };
        QVector<QPoint> chunks;       ///< 该任务扫的区块坐标
        QVector<OreFinder::Hit> hits; ///< 命中的矿物方块坐标
    };

    /** 单区域 1:1 瓦片的渲染结果（跨线程传回 UI） */
    struct TileResult
    {
        QPoint key;    ///< region 坐标
        QImage image;  ///< 512×512；空图 = 该 region 无内容
        int seq = 0;   ///< 批次号，用来丢弃换存档/换维度前的过期结果
    };

    /** 取队列里离视口中心最近的若干块交给工作线程（并行） */
    void pumpTileQueue();
    /** 单块细节渲染完成（watcher 完成信号触发） */
    void onTileFinished(QFutureWatcher<TileResult> *watcher);

    void startRender();
    /** 组装当前的种子预测参数（开关 + 种子 + 版本 + 视角中心） */
    WorldMapRenderer::SeedPreview buildSeedPreview() const;
    /** 视角中心是否已离开种子画布窗口（离开则安排重定心渲染） */
    void maybeScheduleSeedRecenter();
    /** 当前 维度×着色×种子开关×高度 的渲染缓存 key（与 startRender 一致） */
    int renderCacheKey() const;
    /** 顶部标题副行：跟随当前维度变化（地表地图 · 主世界） */
    void updateSubtitle();
    void applyResult(const WorldMapRenderer::RenderResult &result);
    /** 渲染线程发布布局后建画布（定时器里调用） */
    void applyLiveLayout();
    /** 把 sink 里的区域图块贴到画布（定时器里调用） */
    void drainPendingTiles();
    /** 把上一张同 bpp 画布的重叠 region 搬进新 live 画布（种子重定心用） */
    void primeLiveFromPrior();
    void setBusy(bool busy);
    void updateZoomLabel();
    /** 拼状态栏文本：指针坐标 + 选中区块 + 渲染统计（cursor 为空则保留上次指针） */
    void updateStatusLabel(const QString &cursor = QString());
    /** 画布右键：弹地图上下文菜单（复制坐标 / 居中 / 填入跳转 / 取消选择） */
    void showMapContextMenu(const QPoint &globalPos, qint64 blockX, qint64 blockZ);
    /** 点击地图结构图标：弹信息窗（名称 / 坐标 / 百科 / 完成标记） */
    void showStructurePopup(const QPoint &globalPos, int structIdx,
                            qint64 x, qint64 z);
    /** 点击地图矿物点：弹同一个信息窗（多带一个 Y，矿物类型不同） */
    void showOrePopup(const QPoint &globalPos, int ore, qint64 x, qint64 y, qint64 z);
    /** 小窗「详情」：弹居中的结构详情弹窗（简介 / 预览 / 战利品） */
    void showStructureDetail(int structIdx, qint64 x, qint64 z);
    /**
     * 地图信息小窗（结构与矿物共用）：图标 + 名称 + 坐标 + 复制 / 百科 / 完成标记。
     * @param y 为 kNoY（INT_MIN）时坐标只显示 X / Z（结构不知道具体高度）
     * @param structIdx allStructures() 下标；≥0 时多给一个「详情」按钮（仅结构）
     */
    void showMapPointPopup(const QPoint &globalPos, const QPixmap &icon,
                           const QString &title, qint64 x, qint64 y, qint64 z,
                           const QString &wikiTitle, const QString &doneKey,
                           int structIdx = -1);
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
    int m_mcMinor = 21; ///< MC 次版本（16/18/20/21…），战利品随机源代际用
    bool m_hasSeed = false;
    /** 未加载区域的种子预测开关（QSettings 持久化；无种子时禁用） */
    bool m_seedPreviewOn = true;
    QPushButton *m_seedBtn = nullptr;
    /** 当前种子画布的中心 region（视角中心）与半径，用于"离开窗口就重定心" */
    QPoint m_seedCanvasCenter;
    bool m_seedCanvasCenterSet = false;
    int m_seedCanvasRadius = 20;
    /** 重定心重渲期间保留视角中心（避免画布重建时跳回出生点） */
    QPoint m_pendingRestoreBlock;
    bool m_pendingRestoreBlockSet = false;
    /** 上一张 live 画布：种子重定心时把重叠 region 搬进新画布，避免闪白 */
    QImage m_priorLiveImage;
    int m_priorMinChunkX = 0;
    int m_priorMinChunkZ = 0;
    int m_priorBpp = 0;
    bool m_priorLiveValid = false;
    /** 当前 live 画布的布局（重定心搬像素用） */
    int m_liveMinChunkX = 0;
    int m_liveMinChunkZ = 0;
    int m_liveBpp = 1;
    /** 视角离开画布窗口后的去抖定时器（停手 600ms 才重渲，拖动中不打断） */
    QTimer *m_seedRecenterTimer = nullptr;
    /** 第二行结构开关：默认全选 */
    QVector<bool> m_structEnabled;
    QList<QPushButton *> m_structBtns;
    /** 结构条的横向滚动容器（窄屏不撑大窗口） */
    QScrollArea *m_structScroll = nullptr;
    /** 出生点：初始视图中心 + 渐进渲染的优先区域 */
    qint64 m_spawnX = 0;
    qint64 m_spawnZ = 0;
    bool m_hasSpawn = false;
    /** 当前存档已标记完成的结构（key = type_x_z，持久化于 QSettings） */
    QSet<QString> m_structDone;
    /** 打开中的结构信息弹窗（Qt::Popup，点外部自动关） */
    QPointer<QWidget> m_structPopup;

    // ── 内容浏览弹窗（结构 + 群系列表 / 搜索 / 高亮跳转） ──
    /** 第二行右侧「展开」按钮 */
    QPushButton *m_expandBtn = nullptr;
    /** 居中内容弹窗（非模态，跟随页面生命周期） */
    QWidget *m_contentPopup = nullptr;
    QLineEdit *m_popupSearch = nullptr;
    QListWidget *m_structList = nullptr;
    QListWidget *m_biomeList = nullptr;
    /** 群系勾选状态（与 BiomeCatalog::allBiomes() 对齐，默认全不选） */
    QVector<bool> m_biomeEnabled;
    /** 当前行维度下的群系列表下标（列表行 → allBiomes 下标） */
    QVector<int> m_biomeVisible;
    /** 群系采样网格（最近一次结构查询带回） */
    BiomeCatalog::SampleGrid m_biomeGrid;
    /** 选中群系的叠加层（一格一像素，与网格对齐；空图 = 关闭） */
    QImage m_biomeOverlay;
    /** 高亮定位任务（与 m_structPool 共用，轮询收取） */
    std::shared_ptr<LocateJob> m_locateJob;
    /** 待应用的高亮（定位完成后跳转 + 画布高亮） */
    bool m_pendingHighlightIsBiome = false;
    int m_pendingHighlightIdx = -1;

    // ── 内容浏览弹窗·矿物列（列表 + 扫描调度 + 命中缓存） ──
    /** 弹窗矿物列表 */
    QListWidget *m_oreList = nullptr;
    /** 矿物列下方提示（「需放大到 100% 以上才会扫描」） */
    QLabel *m_oreHint = nullptr;
    /** 矿物勾选状态（与 OreFinder::allOres() 对齐，默认全不选） */
    QVector<bool> m_oreEnabled;
    /** 当前维度下的矿物列表下标（列表行 → allOres 下标） */
    QVector<int> m_oreVisible;
    /** 已扫描区块的命中缓存（key = 区块坐标；空结果也记在 m_oreScanned） */
    QHash<QPoint, QVector<OreFinder::Hit>> m_oreHits;
    /** 命中缓存的 LRU 顺序（超上限先丢最旧的区块） */
    QList<QPoint> m_oreHitOrder;
    /** 已扫过的区块（含无命中），避免反复读盘 */
    QSet<QPoint> m_oreScanned;
    /** 在途的区块扫描任务（同一时刻只跑一批，读盘不抢渲染线程） */
    std::shared_ptr<OreScanJob> m_oreJob;
    /** 扫描线程池（与结构查询分开：要读 region 文件，别堵结构刷新） */
    QThreadPool m_orePool;
    /** 视口 / 缩放去抖定时器（停手后才扫，拖图中不读盘） */
    QTimer *m_oreTimer = nullptr;
    /** 状态栏矿物片段（提示或命中数；空则不占位） */
    QString m_oreText;
    /** 还有区块没扫完（状态栏显示「扫描中」，扫完自动转成结果数） */
    bool m_oreBusy = false;
    // ── 底部「当前群系」显示（中文名来自当前实例 lang 文件） ──
    /** 当前实例路径（versions/<id> 目录；群系中文名来源） */
    QString m_instancePath;
    /** 从实例客户端 JAR 解析出的 biome.* 翻译表（后台加载） */
    BiomeName::InstanceNames m_biomeNames;
    /** 当前群系查询任务（后台执行，onPollProgress 轮询收取） */
    std::shared_ptr<BiomeJob> m_biomeJob;
    /** 查询任务进行中又改了视口 / 维度 → 完成后再算一轮 */
    bool m_biomeDirty = false;
    /** 实例群系中文名表解析任务（后台读 lang，onPollProgress 轮询收取） */
    std::shared_ptr<BiomeNameJob> m_biomeNameJob;

    WorldMapView *m_view;
    QStackedWidget *m_stack;
    QLabel *m_titleIcon;
    QLabel *m_saveNameLabel;
    QLabel *m_subtitleLabel;
    QLabel *m_emptyLabel;
    QLabel *m_statusLabel;
    QLabel *m_biomeLabel;
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
    /** 单区域 1:1 瓦片渲染结果的回传（多路并行，各自一个 watcher） */
    QList<QFutureWatcher<TileResult> *> m_tileWatchers;
    /** 在途瓦片的 region 坐标（与 m_tileWatchers 对齐，控制"由中心向外"并行节奏） */
    QList<QPoint> m_tileInFlightKeys;
    /** 同时在渲的细节瓦片数（≤ kMaxConcurrentTiles） */
    int m_tileInFlight = 0;
    /** 视口变化去抖：拖动中不反复重算要加载哪些 region */
    QTimer *m_tileTimer;
    /** 视野补洞心跳：周期性重扫可见范围，防止调度链断掉后左右/四角停在概览 */
    QTimer *m_tileFillTimer = nullptr;
    std::shared_ptr<RenderJob> m_job;
    /** 每次启动渲染自增；finished 时对不上说明是过期任务的结果，直接丢弃 */
    int m_renderSeq = 0;
    /** 最近一次渲染选定的渐进起步 region（无则 INT_MIN） */
    QPoint m_renderPriorityRegion{std::numeric_limits<int>::min(),
                                  std::numeric_limits<int>::min()};
    /** 本次渲染图块到达顺序（探针验证中心优先；每次 startRender 清空） */
    QList<QPoint> m_tileArrivalOrder;
    /** 细节瓦片批次号：换存档 / 换维度时递增，过期的瓦片结果直接丢弃 */
    int m_tileSeq = 0;
    /** 排队 + 在途的 region（在途也在里面，避免重复排队） */
    QSet<QPoint> m_tilesPending;
    /** 渲染过但没有内容的 region（空文件 / 未生成），不再重试 */
    QSet<QPoint> m_tilesEmpty;
    /** 细节瓦片 LRU 顺序（最旧在前），超出上限从 view 里剔除 */
    QList<QPoint> m_tileOrder;
    QString m_detailText;   ///< 状态栏"细节 N"片段
    /** 渐进渲染上下文：渲染线程推图块，定时器在 UI 线程 drain 贴图 */
    WorldMapRenderer::ProgressiveSinkPtr m_sink;
    /** 渐进渲染画布，WorldMapView 直接引用它（不拷贝，避免逐块贴图时整图 detach） */
    QImage m_liveImage;
    /** 当前画布的布局指纹（宽x高:起点区块:bpp），同布局的重渲染保留旧画面 */
    QString m_liveKey;
    bool m_layoutApplied = false;

    /** 已渲染结果缓存：key 由 renderCacheKey() 编入 维度/着色/种子/高度 */
    QHash<int, WorldMapRenderer::RenderResult> m_cache;
    QList<int> m_cacheOrder;
    int m_renderKey = -1;
    bool m_busy = false;
};

#endif // WORLDMAPPAGE_H
