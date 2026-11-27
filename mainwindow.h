#pragma once

#include <QMainWindow>
#include <QHBoxLayout>
#include <QColor>
#include <QFrame>
#include <QStackedWidget>
#include <QPoint>
#include <QMap>
#include <QMutex>
#include <QVector>
#include <QSet>
#include <QShortcut>
#include <QStringList>
#include <QPointer>
#include <QPixmap>

#include <functional>

#include "utils/DownloadTaskManager.h"
#include "utils/GameLauncher.h"
#include "utils/QuickLaunchManager.h"
#include "utils/content/ContentData.h"
#include "utils/FavoritesManager.h"
#include "utils/ForumLobbyService.h"
#include "components/PublishKit.h"

class BackgroundWidget;
class TopBar;
class TaskBar;
class SideBar;
class InstanceAssistantWindow;
class GameFloatingIcon;
class GameAchievementService;
class TouchControlOverlay;
class InstanceSelectPage;
class ForumAccountPage;
class CreationCenterPage;
class AddAccountPage;
class LaunchDetailsPage;
class GameLaunchPage;
class InstallInstancePage;
class LoaderDetailPage;
class ForgeVersionListPage;
class TaskListPage;
class TaskDetailPage;
class ModDownloadPage;
class ModDetailPage;
class LocalModDetailPage;
class ModpackImportPage;
class ModpackExportPage;
class SearchPage;
class ContentDownloadPage;
class ContentDetailPage;
class ContentListPage;
class ResourcesPage;
class BedrockContentPage;
class BedrockContentDetailPage;
class BedrockDownloadPage;
class BedrockVersionDetailPage;
class BedrockInstanceSelectPage;
class BedrockInstanceAssistantWindow;
class InstanceManagePage;
class AiChatPage;
class McpServer;
class ResourceVersionSyncer;
class ResourceSyncListWindow;
struct ResourceSyncItem;
class JavaDownloadPage;
class HomePage;
class FavoritesPage;
class SkinEditorPage;
class PluginPage;
class NewsPage;
class CommunityForumPage;
class ForumThreadDetailPage;
class ForumThreadListPage;
class ForumComposerPage;
class ForumUserProfilePage;
class ForumFriendsPage;
class ForumPluginListPage;
class ForumSchematicListPage;
class CommunitySkinPage;
class CommunitySkinDetailPage;
class SkinStationPage;
class LibraryPage;
class FeedbackPage;
class FeedbackComposePage;
class FeedbackDetailPage;
struct PublicFeedbackItem;
struct SkinLibraryItem;
class ForumReportPage;
class TranslationComposePage;
class PublishPage;
class PublishResourcePage;
class PublishPlatformPage;
class PublishBoardPage;
class MorePage;
class SeedMapPage;
class ProjectionEditPage;
class LanTransferPage;
class MemoryOptimizePage;
class MultiThreadDownloadPage;
class AnnualReportPage;
class QrScanPage;
class OnboardingWizard;
struct ModInfo;
struct ModVersionFile;
class GameLauncher;
class QTimer;
class QScrollBar;
class SubNavPanel;
class ClipboardMonitor;
struct ClipboardLinkInfo;
class FileDropOverlay;
class SystemTray;
class QCloseEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QMimeData;

class PageTransitionAnimator;
class ContentAnimator;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

enum class PageIndex {
    HomePage = 0,
    ResourcesPage = 1,
    SettingsPage = 2,
    InstanceSelectPage = 3,
    AccountManagePage = 4,
    LaunchDetailsPage = 5,
    InstallInstancePage = 6,
    LoaderDetailPage = 7,
    ForgeVersionListPage = 8,
    TaskListPage = 9,
    TaskDetailPage = 10,
    ModDownloadPage = 11,
    ModDetailPage = 12,
    ModpackImportPage = 13,
    SearchPage = 14,
    ContentDownloadPage = 15,
    ContentDetailPage = 16,
    ContentListPage = 17,
    InstanceManagePage = 18,
    ModpackExportPageIndex = 19,
    LocalModDetailPageIndex = 20,
    AiChatPage = 21,
    JavaDownloadPage = 22,
    FavoritesPage = 23,
    SkinEditorPage = 24,
    PluginPage = 25,
    NewsPage = 26,
    CommunityPage = 27,
    ForumThreadDetailPage = 28,
    ForumThreadListPage = 29,
    ForumComposerPage = 30,
    ForumUserProfilePage = 31,
    FeedbackPage = 32,
    ForumFriendsPage = 33,
    FeedbackComposePage = 34,
    ForumPluginListPage = 35,
    PublishPage = 36,
    PublishResourcePage = 37,
    PublishPlatformPage = 38,
    PublishBoardPage = 39,
    OnboardingPage = 40,
    ForumReportPage = 41,
    ForumSchematicListPage = 42,
    /** 安卓端整页启动界面（点「启动游戏」进入，替代底部任务卡形式） */
    GameLaunchPage = 43,
    /** 反馈详情页（反馈列表点卡片进入：正文 / 管理员回复 / 评论） */
    FeedbackDetailPage = 44,
    /** 创建 / 完善翻译页（在线详情页「创建翻译」进入：翻译简介与详细信息，提交后审核） */
    TranslationComposePage = 45,
    /** 资源页「皮肤」子页：方块皮肤站皮肤库（穿上 / 应用为离线皮肤 / 下载 PNG） */
    CommunitySkinPage = 46,
    /** 皮肤站整页（账户管理页「皮肤站」入口进入：衣柜 / 皮肤库 / 3D 预览都在软件内） */
    SkinStationPage = 47,
    /** 我的 MC 年度报告页（账户管理页「游玩」里的年度报告卡片进入，可导出分享海报） */
    AnnualReportPage = 48,
    /** 皮肤库作品详情页（资源>皮肤点卡片进入：3D 预览 / 穿上到皮肤站 / 离线皮肤 / 导出 PNG） */
    CommunitySkinDetailPage = 49,
    /** 手机端扫码登录电脑端的整页扫码界面（顶栏「扫描」按钮进入，桌面端入口不显示） */
    QrScanPage = 50,
    /** 方块图书馆整页（顶栏「文档」进入：使用教程 / 插件开发 / 开放平台 API / MCP，软件内阅读） */
    LibraryPageIndex = 51,
    /** 更多功能页（侧边栏「更多」分类的落地页：图书馆 / 皮肤站 / 种子地图 / 投影编辑器 / 文件传输 / 多线程下载 / 内存优化 / 迁移向导 / 新手引导） */
    MorePageIndex = 52,
    /** 种子地图页（「更多>种子地图」：输入种子即看群系地形，不依赖已有存档） */
    SeedMapPage = 53,
    /** 投影编辑器整页（「更多>投影编辑器」：选好投影文件后直接进编辑页） */
    ProjectionEditPageIndex = 54,
    /** 文件传输整页（「更多>文件传输」：局域网直传 / 远程组网互传，不依赖实例） */
    LanTransferPageIndex = 55,
    /** 内存优化整页（「更多>内存优化」：内存概况 + 一键把闲置内存还给系统） */
    MemoryOptimizePageIndex = 56,
    /** 多线程下载整页（「更多>多线程下载」：贴直链分几段同时下，可暂停继续） */
    MultiThreadDownloadPageIndex = 57
};

/** 侧边栏导航索引（SideBar::buildNavStructure 的顺序）：0首页 1资源 2设置 3AI助手 4实例管理(隐藏) 5社区 6发布 7更多 8反馈 */
constexpr int PublishNavIndex = 6;
constexpr int MoreNavIndex = 7;
constexpr int FeedbackNavIndex = 8;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// 单个启动会话的 UI 状态（重试标记/待执行启动/大厅待上报房间号）。
    /// moc 不处理 slots 区里的类型声明，故放在类体最前面
    struct LaunchSessionUiState {
        bool retryAttempted = false;           // 已执行过「补全文件后重试」，防无限重试
        bool retryPending = false;             // 重试已调度未执行（防重复 gameCrashed）
        bool backupPending = false;            // 启动前自动备份进行中
        std::function<void()> launchAfterBackup;
        int pendingLobbyRoomId = 0;            // 大厅一键加入：等游戏起来再上报的房间号
        int lobbyRoomId = 0;                   // 本会话游戏正在上报「正在游玩」的房间号
    };

    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    /// 刷新首页「最近游玩」区域（实例文件夹在启动后被添加时调用）
    void refreshHomeRecentPlays();

    /// 打开账号页并切到论坛登录表单（答题弹窗「去登录」等外部入口用）
    Q_INVOKABLE void openForumAccountLogin();

    /** 当前是否为基岩版模式（顶栏「Java版/基岩版」切换驱动，导航/收藏夹/资源主页随版本重建） */
    bool isBedrockEdition() const { return m_bedrockEdition; }

    /**
     * @brief 打开新手引导页（整页，隐藏侧边栏）
     * @param firstRun true=首次启动自动进入；走完后发出 onboardingFinished()
     *
     * 设置里的「打开新手引导」入口同样调用本方法，只是不做首次启动的后续动作。
     */
    void showOnboardingPage(bool firstRun = false);

    /**
     * @brief 处理外部一键加入链接（blockbox://join?r=房间号）：按房间号回论坛取房间信息，
     *        再走大厅同款加入流程（陶瓦自动连核心，进服前按版本/加载器挑实例）。
     *        房间已经查不到（房主结束联机）时提示「联机可能已经结束」。
     *        老式的自包含长链接（blockbox://join?d=...）仍然照旧解开并加入。
     *        主程序在窗口显示后、或已运行实例收到转发时调用。
     */
    void handleExternalJoinUrl(const QString &url);

    /**
     * @brief 处理双击桌面 .blockbox 快捷启动文件（也可能是别的程序转发过来的路径）
     *
     * 文件里记录的目标有三种：整个实例、某个存档、某台服务器。这里按目标切到对应
     * 实例并直接启动游戏（进存档 / 连服务器），不再要求用户先打开软件点一遍。
     * 如果文件其实是整合包（.blockbox 也是导出格式），自动转到整合包导入页。
     */
    void handleExternalQuickLaunchFile(const QString &filePath);

    /**
     * @brief 安卓系统返回键分级处理：应用级事件过滤器拦截 Key_Back 后调用。
     *        按"关弹窗 → 收覆盖层 → 收子导航 → 收键盘 → 返回上页 → 确认退出"
     *        的优先级消化一次返回操作。
     * @return true 表示本次返回已被消化（事件不应再传给系统默认行为）；
     *         false 表示当前没有可处理的内容（如窗口尚未显示），交还系统处理。
     */
    bool handleAndroidBack();

    /**
     * @brief 把主窗口从系统托盘叫回前台（点托盘图标 / 托盘通知时调用）
     *
     * 窗口可能正收在托盘里、被最小化，或本来就在前台，这里统一还原成前台可交互状态。
     */
    void showFromTray();

signals:
    /** 新手引导页走完（首次启动流程用：主程序据此弹入站答题提醒 / 检查更新） */
    void onboardingFinished(bool firstRun);

private slots:
    void onSideBarItemClicked(int parentIndex);
    void onParentNavClicked(int parentIndex);
    /** 跳到「设置 → 界面设置」（首页空态轮播卡片被点击时调用） */
    void openInterfaceSettings();
    void onChildNavClicked(int parentIndex, int childIndex);
    void onSettingsNavItemClicked(int index);
    void onResourcesNavItemClicked(int index);
    void onBackToMain();
    void toggleMaximized();
    void onInstanceSelectClicked();
    void onInstanceSettingsClicked();
    /**
     * 把当前实例同步给实例管理页。
     * 实例管理页持有一份实例路径，只有它和主窗口一致时各子页（存档/模组/服务器…）
     * 才会加载正确的实例；从首页快捷入口或侧边栏直接进入时以前漏了这一步，
     * 页面会显示上一个实例（或空）的内容。
     */
    void syncInstanceManagePageContext();
    void onInstanceSelected(const QString &instancePath);
    void onInstanceInstalled(const QString &instancePath);
    void onAccountManageClicked();
    void onSkinEditorClicked();
    /** 顶栏「系统消息」：跳到论坛账户页的通知页签 */
    void onForumNotificationsClicked();
    /** 顶栏「聊天」：打开论坛好友与私信页 */
    void onForumChatClicked();
    void onAccountManagePageOpened();
    void onTopBarBackClicked();
    void onSearchClicked();
    void onScrollToTop();
    void onRefreshClicked();
    /** 子导航顶部搜索框内容变化（200ms 防抖后）：过滤当前页面主内容区 */
    void onSubNavSearchQueryChanged(const QString &query);
    void animateScrollToTop(QScrollBar *scrollBar);
    void animatedSwitchToPage(PageIndex index);
    void animatedSwitchToPageDirectional(PageIndex index, int fromSidebarIndex, int toSidebarIndex);

    /// 记录页面访问（只记页面标识，供崩溃现场与匿名统计使用）
    void trackPageForDiagnostics(PageIndex index);
    void animateContentEntrance(QWidget *page);

    // ── 多实例并行启动：按实例路径路由的启动会话处理 ──
    // 每个并行启动会话（GameLauncher 对象）的信号在连接时捕获实例路径，
    // 处理器按路径把进度/状态/日志写进各自的启动卡片与任务条目
    void onSessionLaunchStatusChanged(const QString &instancePath, GameLauncher::LaunchStatus status);
    void onSessionGameStarted(const QString &instancePath);
    void onSessionGameStopped(const QString &instancePath, int exitCode);
    void onSessionGameCrashed(const QString &instancePath, const QString &error);
    void onSessionLaunchProgressChanged(const QString &instancePath, int progress, const QString& message);
    void onSessionLaunchDetailAdded(const QString &instancePath, const QString& detail);
    void onSessionErrorReportGenerated(const QString &instancePath, const QString& report);
    void onSessionFileCompletionProgress(const QString &instancePath, int completed, int total, const QString &currentFile);
    void onSessionFileCompletionFinished(const QString &instancePath, bool success);
    /** 游戏内触屏键位的输入转发：摇杆向量 / 视角拖动 / 动作键按下抬起（仅安卓接线） */
    void onTouchControlInput(const QString &action, qreal x, qreal y, bool pressed);

    // ── 多实例并行启动：会话管理 ──
    /// 统一启动入口：同实例已在启动/运行则提示并拒绝，桌面端允许多实例并行
    bool startInstanceLaunch(const QString &instancePath, const QStringList &extraGameArgs = {});
    /// 给会话对象接上带实例路径的信号路由（新会话调用一次）
    void connectSessionSignals(GameLauncher *session, const QString &instancePath);
    /// 会话结束（自然退出/取消/异常退出收尾）后的清理：移出会话表并回收对象
    void finishLaunchSession(const QString &instancePath);
    /// 会话的单次启动 UI 状态（结构体定义见类体开头）
    LaunchSessionUiState *sessionState(const QString &instancePath);
    /// 实例显示名（目录名；空路径兜底叫「游戏」）
    static QString instanceDisplayName(const QString &instancePath);
    /// 实例游戏版本（读实例版本 JSON 的 inheritsFrom/clientVersion/id，兜底目录名）
    static QString instanceVersionOf(const QString &instancePath);
    /// 实例加载器名（扫描实例版本 JSON 文本，Vanilla 返回空）
    static QString instanceLoaderOf(const QString &instancePath);

    void onLaunchGameClicked();
    void onLaunchGameClicked(const QString &instancePath);
    /** 启动前置校验：账户未就绪时给出引导并返回 false，不进入启动流程 */
    bool ensureLaunchPrerequisites();
    void onQuickLaunchSaveClicked(const QString &saveName);
    void onQuickLaunchSaveClickedFor(const QString &instancePath, const QString &saveName);
    void onQuickLaunchServerClicked(const QString &address, quint16 port);
    /// 指定实例一键进服（实例助手「开服」页用）：带 --server/--port 启动
    void onQuickLaunchServerClickedFor(const QString &instancePath, const QString &address,
                                       quint16 port);
    /** 执行 .blockbox 快捷启动文件记录的目标（实例 / 存档 / 服务器） */
    void launchQuickLaunchTarget(const QString &instancePath,
                                 const QuickLaunchManager::Target &target);
    /** 联机大厅 / 好友房间「一键加入」：按房间要求挑最合适的实例，缺东西时先问用户再补装 */
    void onLobbyJoinRequested(const ForumLobbyRoom &room, const QString &address, quint16 port);
    /** 开始「正在游玩」上报：立刻报一次，之后每 40 秒续一次（游戏已在运行时调用） */
    void startLobbyPlayReport(int roomId);
    /** 结束上报：游戏退出 / 关软件 / 换房间时调用，服务端马上把这个人从房间里摘掉 */
    void stopLobbyPlayReport();
    /** 本机没有合适实例时，把用户带到「安装新实例」的版本详情页 */
    void openLobbyInstallFlow(const QString &gameVersion, const QString &loader);
    /** 首页好友卡片「一起玩」：直接加入好友正在开放的联机房间 */
    void onFriendRoomJoinRequested(const ForumLobbyRoom &room);
    /** 首页好友房间的陶瓦联机状态变化：核心就绪后自动连进房间 */
    void onFriendRoomJoinStateChanged();
    void onSaveSettingsOpened();
    void onSaveMapOpened();
    void onShaderSettingsOpened();
    void onProjectionEditOpened();
    void onLaunchTaskCardClicked();
    /** 启动卡片点击：详情页聚焦到该实例的启动会话 */
    void onShowDetailsPage(const QString &instancePath = QString());
    void onDetailsPageBack();

    void onVersionSelected(const QString &versionId, const QString &versionType);
    void onModifyVersionSelected(const QString &versionId, const QString &versionType,
                                 const QString &instancePath, const QString &instanceName);
    void onInstanceModified(const QString &instancePath);
    void onBackToInstallPage();
    void onViewAllVersions(const QString &loaderName, const QString &minecraftVersion);
    void onBackToLoaderDetail();
    void onForgeVersionSelected(const QString &loaderName, const QString &mcVersion, const QString &forgeVersion);

    void onShowTaskListPageRequested();
    void onTaskListPageBack();
    void onTaskDetailRequested(const QString &taskId);
    void onTaskDetailPageBack();
    void onTaskCancelled(const QString &taskId);

    /// 论坛请求被人机验证拦下时弹滑块
    void onHumanCheckRequired();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    /** 创建桌面端系统托盘（手机 / 平板或系统没有托盘服务时跳过） */
    void setupSystemTray();

    /** 把主窗口收进系统托盘继续后台运行（第一次会弹一条系统通知说明） */
    void hideToTray();

    /** 托盘菜单「打开设置」：先把窗口叫回来，再跳到设置页 */
    void openSettingsFromTray();

    /** 托盘菜单「退出方块盒子」：标记为明确退出后走正常退出流程 */
    void quitFromTray();

    /// 进社区前先过一道人机验证，已经验过就直接返回 true
    /// @param force 服务端已经明确要求验证时传 true，忽略本地令牌的有效期
    bool ensureHumanCheck(bool force = false);

    /** 修改版本完成后：自动同步模组 / 资源包 / 光影包到新版本 */
    void startResourceVersionSync(const QString &instancePath,
                                  const QString &oldVersion, const QString &newVersion);
    void onResourceSyncFinished(const QList<ResourceSyncItem> &converted,
                                const QList<ResourceSyncItem> &pending);
    void onResourceSyncCheckFailed(const QString &error);
    /** 未适配新版本的资源询问弹窗：删除 / 保留 / AI 找替代 / 悬浮窗查看清单 */
    void showResourceSyncPendingDialog(const QList<ResourceSyncItem> &pending);
    void showResourceSyncListWindow(const QList<ResourceSyncItem> &items);
    /** 跳转 AI 助手，注入未适配资源引用与「找替代」提示词 */
    void openAiFindReplacement(const QList<ResourceSyncItem> &items);

    Ui::MainWindow *ui;
    TopBar *m_topBar;
    TaskBar *m_taskBar;
    SideBar *m_sideBar;
    SubNavPanel *m_subNavPanel;
    QFrame *m_sideSeparator;
    QWidget *m_mainContentArea = nullptr;
    QHBoxLayout *m_wrapperLayout = nullptr;
    QStackedWidget *m_stackedWidget;

    InstanceSelectPage *m_instanceSelectPage;
    /**
     * 实例助手窗口（实例助手窗口多开：每个实例一个独立窗口，键=实例路径）。
     * 安卓游戏内侧栏模式仍使用 m_overlayAssistantWindow 单窗口。
     */
    QMap<QString, InstanceAssistantWindow *> m_assistantWindows;
    /** 安卓游戏内侧栏助手窗口（overlay 模式专用，桌面端不用） */
    InstanceAssistantWindow *m_overlayAssistantWindow = nullptr;
    /** 基岩版实例助手窗口（首页/AI/快捷指令/联机/资源管理/设置 六标签） */
    BedrockInstanceAssistantWindow *m_bedrockInstanceAssistantWindow = nullptr;
    /** 账户管理页界面（与方块盒子论坛账号打通，桌面端唯一的账户页） */
    ForumAccountPage *m_forumAccountPage = nullptr;
    /** 创作中心（账户管理页内层第二页：自己发布作品的管理与数据） */
    CreationCenterPage *m_creationCenterPage = nullptr;
    /** 添加角色页（账户管理页内层页：离线 / 微软正版 / 第三方添加游戏账户） */
    AddAccountPage *m_addAccountPage = nullptr;
    /** 账户管理页容器（入场动画作用在内层页面，故保留一层 QStackedWidget） */
    QStackedWidget *m_accountUiStack = nullptr;    LaunchDetailsPage *m_launchDetailsPage;
    /** 安卓端整页启动界面（按需创建；桌面端恒为空，启动进度仍走任务卡） */
    GameLaunchPage *m_gameLaunchPage = nullptr;
    InstallInstancePage *m_installInstancePage;
    LoaderDetailPage *m_loaderDetailPage;
    ForgeVersionListPage *m_forgeVersionListPage;
    TaskListPage *m_taskListPage;
    TaskDetailPage *m_taskDetailPage;
    ModDownloadPage *m_modDownloadPage;
    ModDetailPage *m_modDetailPage;
    LocalModDetailPage *m_localModDetailPage = nullptr;
    ModpackImportPage *m_modpackImportPage;
    ModpackExportPage *m_modpackExportPage;
    SearchPage *m_searchPage;
    ContentDownloadPage *m_contentDownloadPage;
    ContentDetailPage *m_contentDetailPage;
    ContentListPage *m_contentListPage;
    ResourcesPage *m_resourcesPage;
    InstanceManagePage *m_instanceManagePage;
    /** 基岩版社区资源浏览页/详情页（CurseForge 源，基岩版模式下使用） */
    BedrockContentPage *m_bedrockContentPage = nullptr;
    BedrockContentDetailPage *m_bedrockContentDetailPage = nullptr;
    /** 基岩版版本下载页（多源 mcappx/mcapks/bbk 版本列表）与版本详情下载页 */
    BedrockDownloadPage *m_bedrockDownloadPage = nullptr;
    BedrockVersionDetailPage *m_bedrockVersionDetailPage = nullptr;
    /** 基岩版实例管理页（多实例选择/新建/导入/启动） */
    BedrockInstanceSelectPage *m_bedrockInstanceSelectPage = nullptr;
    /** 基岩版社区资源下载目标实例 ID（空 = 跟随当前激活实例） */
    QString m_bedrockDownloadTargetInstanceId;
    AiChatPage *m_aiChatPage = nullptr;
    /** 本地 MCP 服务器（外部 AI 客户端接入，默认关闭） */
    McpServer *m_mcpServer = nullptr;
    /** 最近一次 MCP 服务状态（设置页晚创建时用来补显示） */
    bool m_mcpRunning = false;
    quint16 m_mcpRunningPort = 0;
    QString m_mcpStatusDetail;
    /** 桌面端系统托盘（平台不支持托盘时为 nullptr，关窗行为保持原样） */
    SystemTray *m_systemTray = nullptr;
    /** 用户在托盘菜单里明确点了退出：closeEvent 直接放行，不再收进托盘 */
    bool m_quitConfirmed = false;
    JavaDownloadPage *m_javaDownloadPage = nullptr;
    /** 资源版本同步器（修改实例版本后自动转化模组/资源包/光影包） */
    ResourceVersionSyncer *m_resourceVersionSyncer = nullptr;
    /** 未适配资源清单悬浮窗（WA_DeleteOnClose，关闭后自动置空） */
    QPointer<ResourceSyncListWindow> m_resourceSyncListWindow;
    /** 最近一次版本同步的新旧版本（弹窗文案与 AI 提示词用） */
    QString m_syncOldVersion;
    QString m_syncNewVersion;
    HomePage *m_homePage = nullptr;
    FavoritesPage *m_favoritesPage = nullptr;
    SkinEditorPage *m_skinEditorPage = nullptr;
    PluginPage *m_pluginPage = nullptr;
    NewsPage *m_newsPage = nullptr;
    CommunityForumPage *m_communityPage = nullptr;
    /// 人机验证弹窗是否正在显示（防止并发请求弹出多个）
    bool m_humanCheckRunning = false;
    /** 论坛深度集成四页：帖子详情 / 主题列表（板块·搜索·标签）/ 发帖 / 用户主页 */
    ForumThreadDetailPage *m_forumThreadDetailPage = nullptr;
    ForumThreadListPage *m_forumThreadListPage = nullptr;
    ForumComposerPage *m_forumComposerPage = nullptr;
    ForumUserProfilePage *m_forumUserProfilePage = nullptr;
    /** 论坛好友与私信页 */
    ForumFriendsPage *m_forumFriendsPage = nullptr;
    /** 资源页「插件」子页：论坛插件帖聚合成可安装列表 */
    ForumPluginListPage *m_forumPluginListPage = nullptr;
    /** 资源页「投影」子页：论坛投影分享板块的投影列表（一键下载到当前实例） */
    ForumSchematicListPage *m_forumSchematicListPage = nullptr;
    /** 资源页「皮肤」子页：方块皮肤站皮肤库（穿上 / 离线皮肤 / 导出 PNG） */
    CommunitySkinPage *m_communitySkinPage = nullptr;
    /** 皮肤库作品详情页（资源>皮肤点卡片进入，整页展示 3D 预览与动作） */
    CommunitySkinDetailPage *m_communitySkinDetailPage = nullptr;
    /** 皮肤站整页（账户管理页「皮肤站」进入，软件内打开而不是独立窗口） */
    SkinStationPage *m_skinStationPage = nullptr;
    /** 我的 MC 年度报告页（统计页进入，返回落点是统计页） */
    AnnualReportPage *m_annualReportPage = nullptr;
    /** 手机端扫码登录电脑端的扫码页（顶栏「扫描」进入，按需创建后常驻） */
    QrScanPage *m_qrScanPage = nullptr;
    /** 方块图书馆整页（顶栏「文档」/ 设置-关于 进入，软件内直接看四卷文档，不开浏览器） */
    LibraryPage *m_libraryPage = nullptr;
    /** 软件内反馈页（左侧导航栏底部入口），只显示反馈列表 */
    FeedbackPage *m_feedbackPage = nullptr;
    /** 提交新反馈页（反馈页顶部「提交新反馈」进入），提交走论坛账号 */
    FeedbackComposePage *m_feedbackComposePage = nullptr;
    /** 反馈详情页（反馈列表点卡片进入：正文 / 管理员回复 / 评论区） */
    FeedbackDetailPage *m_feedbackDetailPage = nullptr;
    /** 创建 / 完善翻译页（在线详情页「创建翻译」进入），提交走论坛账号 */
    TranslationComposePage *m_translationComposePage = nullptr;
    /** 翻译编辑页的上下文：正在翻译的资源与详情页当前显示的原文 */
    ModInfo m_translationComposeInfo;
    QString m_translationSourceSummary;
    QString m_translationSourceDetailHtml;
    /** 举报楼层页（帖子详情页「举报」进入），分类 + 补充说明走论坛账号 */
    ForumReportPage *m_forumReportPage = nullptr;
    /** 发布中心：入口页 / 资源类型页 / 平台跳转页 / 帖子板块页 */
    PublishPage *m_publishPage = nullptr;
    PublishResourcePage *m_publishResourcePage = nullptr;
    PublishPlatformPage *m_publishPlatformPage = nullptr;
    PublishBoardPage *m_publishBoardPage = nullptr;
    /** 更多功能页（侧边栏「更多」类别的落地页，按需创建后常驻） */
    MorePage *m_morePage = nullptr;
    /** 更多页插件入口是否已同步过（首次进入更多页时扫描一次插件目录） */
    bool m_moreEntriesLoaded = false;
    /** 更多页插件入口签名（插件ID|入口ID|标题），插件变化时才重建侧边栏与卡片 */
    QString m_moreEntrySignature;
    /** 种子地图页（「更多>种子地图」，按需创建后常驻） */
    SeedMapPage *m_seedMapPage = nullptr;
    /** 投影编辑器整页（「更多>投影编辑器」，按需创建后常驻） */
    ProjectionEditPage *m_projectionEditPage = nullptr;
    /** 文件传输整页（「更多>文件传输」，按需创建后常驻；与实例管理里的文件传输各自独立） */
    LanTransferPage *m_lanTransferPage = nullptr;
    /** 内存优化页（「更多>内存优化」，按需创建后常驻） */
    MemoryOptimizePage *m_memoryOptimizePage = nullptr;
    /** 多线程下载页（「更多>多线程下载」，按需创建后常驻） */
    MultiThreadDownloadPage *m_multiThreadDownloadPage = nullptr;
    /** 新手引导页（独立整页，按需创建后常驻；不随侧边栏导航出现） */
    OnboardingWizard *m_onboardingPage = nullptr;
    /** 本次进入引导页是否为首次启动自动进入（决定走完后是否触发首次启动后续动作） */
    bool m_onboardingFirstRun = false;
    /** 论坛内层页（详情 / 发帖 / 用户主页）的返回动作，由进入入口决定 */
    std::function<void()> m_forumReturnAction;
    /** 从发帖页去登录前的返回落点，登录完回到发帖页后恢复 */
    std::function<void()> m_pendingComposerReturn;
    /** 发帖页顶栏标题（发布中心进入时用「发布>插件 / 发布>帖子」，空则用默认） */
    QString m_forumComposerTopTitle;

    ClipboardMonitor *m_clipboardMonitor = nullptr;
    bool m_clipboardDialogOpen = false; ///< 剪贴板确认框是否已弹出（防剪贴板变化重入叠加弹窗）

    GameFloatingIcon *m_gameFloatingIcon = nullptr; ///< 游戏内悬浮图标（仅安卓：实例助手入口）
    TouchControlOverlay *m_touchControlOverlay = nullptr; ///< 游戏内触屏键位悬浮层（仅安卓：按操作设置布局）

    QList<QShortcut *> m_shortcuts;

    BackgroundWidget *m_backgroundWidget;
    GameLauncher *m_gameLauncher;
    /** 全局游戏成就同步（不依赖实例助手；单机读存档，方块盒子联机读日志） */
    GameAchievementService *m_gameAchievements = nullptr;
    PageTransitionAnimator *m_pageAnimator;
    ContentAnimator *m_contentAnimator;
    QPoint m_dragPosition;

    QString m_currentVersionId;
    QString m_currentVersionType;
    QString m_currentInstancePath;
    QString m_currentInstanceVersion;
    QString m_currentInstanceLoader;
    /**
     * 当前用于启动游戏的账户名 / 头像。
     * 顶栏账户卡片登录方块盒子账户后会改成显示社区账户，
     * 所以启动参数不能再从顶栏取名字，统一用这里保存的游戏账户。
     */
    QString m_gameAccountName;
    QPixmap m_gameAccountAvatar;
    /** 从论坛账户详情进入皮肤编辑器时要应用的账户（一次性，用后清空） */
    QString m_pendingSkinEditorAccount;
    QString m_pendingSkinEditorAccountType;
    /** 首页好友房间：陶瓦联机加入流程的待启动参数（GuestOk 后启动游戏） */
    bool m_friendRoomJoinPending = false;
    bool m_friendRoomJoinWaitingCore = false;
    QString m_friendRoomJoinCode;
    ForumLobbyRoom m_friendRoomJoinRoom;   ///< 待加入的好友房间（版本 / 加载器 / 模组 / 资源包）
    /**
     * 联机大厅「正在游玩」上报：
     * 一键加入时先记下房间号，等游戏真正跑起来（onGameStarted）再每 40 秒上报一次，
     * 游戏退出 / 关软件时把记录删掉，大厅详情页里就能看到谁在这个房间里。
     */
    QTimer *m_lobbyPresenceTimer = nullptr;
    int m_lobbyPlayRoomId = 0;      ///< 正在上报的房间号（0 = 没在玩）

    // ── 多实例并行启动 ──
    /// 活动的启动会话（键=实例路径，含主单例复用的会话）；会话结束即移除。
    /// 用 QMap：sessionState() 返回值指针要求节点地址稳定
    QMap<QString, GameLauncher *> m_launchSessions;
    /// 各会话的单次启动 UI 状态（重试/备份/大厅待上报），键=实例路径
    QMap<QString, LaunchSessionUiState> m_launchSessionStates;
    /// 启动详情页当前展示的会话（卡片点击切换；空=最新会话）
    QString m_focusedLaunchSession;
    /// 当前正在进行「启动前自动备份」的会话（备份管理器全局单例，同一时间只跑一份）
    QString m_backupPendingPath;
    /// 游玩时长统计当前认领会话的实例路径（服务一次只记一个会话）
    QString m_playtimeSessionPath;
    /// 大厅/好友房间一键加入：随下一次 startInstanceLaunch 消费进会话状态的房间号
    int m_nextLaunchLobbyRoomId = 0;

    QMap<PageIndex, bool> m_pageInitialized;
    QRecursiveMutex m_initMutex;
    PageIndex m_previousPage;
    bool m_hasPreviousPage;
    int m_lastSidebarIndex;
    int m_lastChildSidebarIndex[10];
    /** 当前版本模式（true=基岩版），由顶栏「Java版/基岩版」切换驱动 */
    bool m_bedrockEdition = false;

    void initUI();
    void initPages();
    void initGameLauncher();
    /** 登记详情页链接的分发方式：站内帖子、一键加入、CF / Modrinth 资源页都在软件内打开 */
    void setupLinkRouter();
    void updateBackgroundWidget();
    /** 按当前主题应用旋转全景：素材已下载则直接用本地目录，否则先触发下载 */
    void applyRotatingPanorama(const QString &scene);
    /** 旋转模式实际使用的场景：自定义图片为空/失效时回退到默认全景主题 */
    QString effectiveRotatingScene() const;
    void setSideBarVisible(bool visible);
    /**
     * 显示侧边栏并恢复该页所属分组的子导航面板。
     * 详情页会整体隐藏侧边栏（同时收起子导航），返回上一页时只调用 setSideBarVisible(true)
     * 会漏掉子导航，故统一走本方法。
     * @param parentIndex 目标页所属主导航项（<0 表示仅显示侧边栏，不展开子导航）
     * @param childIndex  需要选中的子项（<0 表示不选中）
     */
    void restoreSideBarWithSubNav(int parentIndex, int childIndex);

    /**
     * 解析「当前可过滤页」：解包 HomePage/SettingsPage 外层 QScrollArea；
     * 实例管理页返回其自身（由它转发给内层当前子页）。
     * 无对应页时返回 nullptr。
     */
    QWidget *resolveCurrentFilterPage() const;
    /** 把查询分发到当前可过滤页（Q_INVOKABLE applyPageFilter）；空查询恢复上一被过滤页 */
    void dispatchPageFilter(const QString &query);

    /** 最近一次被子导航搜索过滤过的页面（切换/清空时先恢复它） */
    QPointer<QWidget> m_lastFilteredPage;
    /** 子导航当前查询词（页面/子页切换后需重新应用） */
    QString m_subNavFilterQuery;

#ifdef Q_OS_ANDROID
    /** 安卓游戏内：弹出/收起右侧实例助手侧栏（由 GameFloatingIcon 点击触发） */
    void toggleGameAssistantOverlay();
#endif

    void initEssentialPages();
    void ensurePageInitialized(PageIndex pageIndex);
    bool isPageInitialized(PageIndex pageIndex) const;

    /** 按设置启停本地 MCP 服务器，并把状态回显到设置页 */
    void applyMcpServerSettings();
    /** 把 MCP 服务状态同步到已创建的设置页（未创建时忽略，创建时再补一次） */
    void syncMcpStatusToSettingsPage();

    /** 从拖拽 mime 中取出方块盒子更新安装包的本地路径列表 */
    QStringList extractUpdatePackages(const QMimeData *mime) const;
    /** 从拖拽 mime 中取出基岩版资源包/地图（.mcpack/.mcaddon/.mcworld/.mctemplate）路径列表 */
    QStringList extractBedrockPacks(const QMimeData *mime) const;
    /** 把拖入的基岩版资源安装进当前激活实例的 com.mojang */
    void installDroppedBedrockPacks(const QStringList &files);
    /** 更新包拖入覆盖层（悬停/松开时显示提示） */
    FileDropOverlay *m_dropOverlay = nullptr;
    QStringList m_pendingDropPackages;
    QStringList m_pendingDropBedrockPacks;

    void initInstanceSelectPage();
    void initAccountManagePage();
    /** 论坛账户管理页：账户管理页的唯一界面（按需创建，创建后常驻） */
    void buildForumAccountManagePage();
    /** 「添加角色」页：离线 / 微软正版 / 第三方添加游戏账户（账户管理页内层页） */
    void openAddAccountPage();
    void initLaunchDetailsPage();
    /** 安卓端整页启动界面（按需创建） */
    void initGameLaunchPage();
    void initInstallInstancePage();
    void initLoaderDetailPage();
    void initForgeVersionListPage();
    void initTaskListPage();
    void initTaskDetailPage();
    void initModDownloadPage();
    void initModDetailPage();
    void initModpackImportPage();
    void initModpackExportPage();
    void initSearchPage();
    void initContentDownloadPage(ContentType type);
    void initContentDetailPage(ContentType type);
    void initContentListPage(ContentType type);
    void initAiChatPage();
    void initJavaDownloadPage();
    void initSkinEditorPage();
    void initPluginPage();
    void initNewsPage();
    void initCommunityPage();
    void initForumThreadDetailPage();
    void initForumThreadListPage();
    void initForumComposerPage();
    void initForumUserProfilePage();
    void initFeedbackPage();
    void initFeedbackComposePage();
    void initFeedbackDetailPage();
    void initTranslationComposePage();
    void initForumReportPage();
    void initForumFriendsPage();
    void initForumPluginListPage();
    void initForumSchematicListPage();
    void initCommunitySkinPage();
    /** 皮肤库作品详情页（资源>皮肤点卡片进入，按需创建，创建后常驻） */
    void initCommunitySkinDetailPage();
    /** 皮肤站整页（按需创建，创建后常驻） */
    void initSkinStationPage();
    /** 我的 MC 年度报告页（按需创建，创建后常驻） */
    void initAnnualReportPage();
    /** 手机端扫码登录电脑端的扫码页（按需创建，创建后常驻） */
    void initQrScanPage();
    /** 方块图书馆整页（按需创建，创建后常驻） */
    void initLibraryPage();
    /** 打开扫码登录页（手机端顶栏「扫描」进入，返回落点是进入前的页面） */
    void showQrScanPage();
    /** 扫码页自己的返回请求：仍停在扫码页时才按「返回上一页」处理 */
    void onQrScanPageBackRequested();
    void initPublishPage();
    void initPublishResourcePage();
    void initPublishPlatformPage();
    void initPublishBoardPage();
    /** 更多功能页（侧边栏「更多」入口，按需创建后常驻） */
    void initMorePage();
    /** 打开「更多」落地页：展开更多分类的子导航并选中侧边栏「更多」 */
    void showMorePage();
    /** 「更多」子导航 / 卡片选了第 childIndex 个分类：转到对应整页 */
    void openMoreFeature(int childIndex);
    /**
     * @brief 打开插件管理整页（「设置」子导航最后一项）
     * @param settingsChildIndex 设置子导航里「插件管理」的序号；-1 表示按设置页自动定位
     *
     * 只切换页面、侧边栏选中「设置」、展开设置子导航并高亮该项，
     * 设置内容页本身不动（插件页不放进设置内容堆栈）。
     */
    void showPluginManagePage(int settingsChildIndex = -1);
    /** 同步「更多」页里插件提供的入口（插件列表 / 启用状态变化后调用），
     *  返回侧边栏与卡片是否真的重建（入口没变时只刷新卡片） */
    bool syncMorePluginEntries(bool rescan);
    /** 执行「更多」里插件提供的第 entryIndex 个入口（内置入口之后依次排） */
    void openPluginMoreEntry(int entryIndex);
    /** 种子地图页（按需创建后常驻） */
    void initSeedMapPage();
    /** 打开种子地图页（「更多>种子地图」） */
    void showSeedMapPage();
    /** 投影编辑器整页（按需创建后常驻） */
    void initProjectionEditPage();
    /** 选一个投影文件后打开投影编辑器（「更多>投影编辑器」） */
    void showProjectionEditorPage();
    /** 文件传输整页（按需创建后常驻） */
    void initLanTransferPage();
    /** 打开文件传输页（「更多>文件传输」：局域网直传 / 远程组网互传） */
    void showLanTransferPage();
    /** 内存优化页（按需创建后常驻） */
    void initMemoryOptimizePage();
    /** 打开内存优化页（「更多>内存优化」：内存概况 + 一键优化） */
    void showMemoryOptimizePage();
    /** 多线程下载页（按需创建后常驻） */
    void initMultiThreadDownloadPage();
    /** 打开多线程下载页（「更多>多线程下载」：直链分段并发下载） */
    void showMultiThreadDownloadPage();
    /** 新手引导页（首次启动时懒加载创建） */
    void initOnboardingPage();
    /** 引导页走完：回到首页，必要时通知主程序做首次启动后续动作 */
    void onOnboardingFinished();
    /** 引导页「返回设置」/ 顶栏返回 */
    void onOnboardingBackRequested();
    /** 引导页切换语言：重启进程以干净加载新语言 */
    void onOnboardingRestartRequested();
    // ── 安卓端整页启动界面 ──
    /** 启动页顶栏返回 / 安卓返回键：启动中先确认取消，其余直接回主页 */
    void onGameLaunchPageBackRequested();
    // ── 发布中心：入口 / 资源类型 / 平台跳转 / 帖子板块 ──
    void showPublishPage();
    void showPublishResourcePage();
    void showPublishPlatformPage(PublishKit::ResourceKind kind);
    void showPublishBoardPage();
    /** 插件发布页（复用论坛发帖页，预选「插件与开发」板块） */
    void showPublishPluginCompose();
    /** 投影发布页（复用论坛发帖页，预选「投影」资源分类） */
    void showPublishSchematicCompose();
    /** 帖子发帖详情页（复用论坛发帖页，预选所选板块） */
    void showPublishPostCompose(const QString &boardSlug);
    /** 教程发布页（复用论坛发帖页，切到「教程」模式，固定发到教程分享板块） */
    void showPublishTutorialCompose();
    /** 有奖问答发布页（复用论坛发帖页，切到「问答」模式，固定发到有奖问答板块） */
    void showPublishQuestionCompose();
    /** 导航发布页（复用论坛发帖页，切到「导航」模式，按类型发到网站/服务器导航板块） */
    void showPublishNavigationCompose();
    /**
     * @brief 把一台开好的服务器发布到社区「服务器导航」（「开服」页按钮走这里）
     *
     * 标题 / 正文 / 标签由开服页按服务器信息生成，地址优先用隧道地址；
     * 未登录或没过入站答题时，发帖页自己会显示登录 / 答题入口。
     */
    void showPublishServerNavigationCompose(const QString &title, const QString &content,
                                            const QStringList &tags, const QString &address);
    // ── 论坛深度集成：内层页的打开与返回 ──
    void showForumThread(int threadId);
    void showForumBoard(const QString &slug, const QString &boardName);
    void showForumSearch(const QString &query);
    void showForumTag(const QString &tag);
    void showForumComposer(const QString &preselectSlug = QString(),
                           const QString &topTitle = QString());
    void showForumUserProfile(int userId);
    /** 打开资源页「插件」子页（论坛插件帖列表） */
    void showForumPluginListPage();
    /** 打开资源页「投影」子页（论坛投影分享板块的投影列表） */
    void showForumSchematicListPage();
    /** 打开资源页「皮肤」子页（方块皮肤站皮肤库） */
    void showCommunitySkinPage();
    /** 打开皮肤库作品详情页（资源>皮肤点卡片进入，返回落点是皮肤库列表页） */
    void showCommunitySkinDetail(const SkinLibraryItem &item);
    /** 回到皮肤库列表页（详情页的返回落点，不重新拉列表） */
    void returnToCommunitySkinList();
    /** 打开皮肤站整页（账户管理页「皮肤站」入口，返回落点是账户管理页） */
    void showSkinStationPage();
    /** 打开「我的 MC 年度报告」页（账户管理页「游玩」里的年度报告卡片入口，返回落点也是「游玩」页签） */
    void showAnnualReportPage(int year = 0);
    void showFeedbackPage();
    /** 打开方块图书馆整页（顶栏「文档」与设置-关于的「文档中心」都走这里），返回落点是主界面 */
    void showLibraryPage();
    /** 打开图书馆并直接跳到指定卷 / 章（插件页、AI 设置里的文档入口都走这里） */
    void openLibraryDoc(const QString &volume, const QString &chapter = QString(),
                        const QString &anchor = QString());
    /** 提交新反馈页（从反馈列表页进入，返回落点也是反馈列表页） */
    void showFeedbackComposePage();
    /**
     * 进入创建 / 完善翻译页（上下文在 m_translationComposeInfo 等成员里，由详情页入口填好）。
     * returnToDetail 为返回落点（从哪个详情页进来的就回哪个），提交成功后也走它回详情页。
     */
    void showTranslationComposePage(std::function<void()> returnToDetail);
    /** 反馈详情页（从反馈列表页点卡片进入，返回落点也是反馈列表页） */
    void showFeedbackDetail(const PublicFeedbackItem &item);
    /** 举报楼层页（从帖子详情页进入，返回落点是帖子详情页） */
    void showForumReportPage(int postId, int floor, const QString &authorName,
                             const QString &excerpt);
    /** 打开好友与私信页；userId > 0 时直接和这个人开聊天 */
    void showForumFriends(int userId = 0);
    void onForumPageBack();
    /** 内层页的纯展示（不重新加载数据），互跳返回时用 */
    void presentForumThreadDetail();
    void presentForumThreadList();
    void presentForumComposer();
    void presentForumUserProfile();
    void presentFeedbackPage();
    void presentFeedbackComposePage();
    void presentTranslationComposePage();
    void presentFeedbackDetailPage();
    void presentForumReportPage();
    void presentForumFriends();
    /** 回到社区页（恢复侧边栏选中与顶栏标题） */
    void returnToCommunityPage();
    /** 回到「设置 > 关于」（设置里反馈入口的返回落点） */
    void returnToSettingsAbout();
    /** 回到反馈列表页（提交新反馈页的返回落点） */
    void returnToFeedbackList();
    void initSettingsPage();
    /** 当前设置页中「关于」子页的序号；设置页还没创建时返回 -1 */
    int settingsTabIndexForAbout() const;
    void showJavaDownloadPage();
    /** 弹出网络连通性自检对话框（设置页 / 启动加载失败诊断入口） */
    void showNetworkCheckDialog();
    void showFavoritesPage(const QString &folderId);
    /** 打开内置「官方新闻」列表页 */
    void showNewsPage();
    void showContentDetailFromFavorite(const FavoriteItem &item);
    void onCreateFavoriteFolderRequested();
    void onManageFavoriteFoldersRequested();
    void onFavoritesChanged();

    // ===== 插件功能 =====
    void onImportPluginRequested();
    void onCreatePluginRequested();
    void onRemovePluginRequested(const QString &id);
    void onOpenPluginsDirRequested();
    /** 插件列表变化后刷新侧边栏子导航与插件页 */
    void refreshPluginNav();

    void reloadShortcuts();
    Q_INVOKABLE void setShortcutsEnabled(bool enabled);

    void showInstanceSelectPage();
    void showAccountManagePage();
    /** 切到账户管理页内层的创作中心（自己发布作品的管理与数据） */
    void showCreationCenterPage();
    /** 打开实例助手窗口（顶栏与首页经典模式面板共用），注入当前实例上下文 */
    void openInstanceAssistant();
    /** 打开指定实例的助手窗口（窗口多开：每个实例一个独立窗口；已有则置前） */
    void openInstanceAssistantFor(const QString &instancePath);
    void showSkinEditorPage();
    void showLaunchDetailsPage();
    /** 安卓端：切到整页启动界面（记录来路页面，返回键可离开） */
    void showGameLaunchPage();
    void showTaskListPage();
    void showTaskDetailPage(const QString &taskId);
    void showModDownloadPage();
    void showModDetailPage(const ModInfo &modInfo,
                           const QString &filterGameVersion = QString(),
                           const QString &filterLoader = QString());
    void showLocalModDetailPage(const ModInfo &modInfo);
    void showModpackImportPage();
    void showModpackExportPage(const QString& instancePath = QString());
    void showSearchPage();
    /**
     * @brief 跳转到 AI 助手页并注入一条资源引用（问问AI 入口）
     * @param ref 资源引用（本地文件路径或网络 URL）
     */
    void openAiChatWithReference(const ResourceReference &ref);

    /** 带额外 gameArgs 启动游戏（用于存档/服务器快捷启动） */
    void launchGameWithExtraArgs(const QStringList &extraGameArgs);
    /**
     * @brief 启动游戏前的自动存档备份：备份完成后（未启用/无需备份时立即）执行 launchAction
     *
     * 备份在工作线程里跑，启动任务卡显示进度；备份失败不会拦住启动，
     * 只是本次没有新的备份点。备份进度按 instancePath 路由到对应会话的 UI。
     */
    void launchWithAutoBackup(const QString &instancePath, const std::function<void()> &launchAction);
    void showContentDownloadPage(ContentType type);
    void showContentDetailPage(const ModInfo &info, ContentType type,
                               const QString &filterGameVersion = QString(),
                               const QString &filterLoader = QString());
    void showContentListPage(ContentType type);
    void updateCurrentInstance(const QString &instancePath);
    /**
     * @brief 同步实例上下文（路径/版本/加载器/顶栏/资源页/AI 页）但不切换页面
     * @param instancePath 目标实例路径
     *
     * 用于下载页内"下载实例"下拉这类就地切换场景：下载目标随之改变，
     * 但不应把用户踢回主页（updateCurrentInstance 会跳转 HomePage）。
     */
    void applyInstanceContext(const QString &instancePath);
    void saveCurrentInstancePath();
    void loadCurrentInstancePath();
    void restoreDefaultAccount();
    /**
     * @brief 刷新顶栏账户卡片：已登录方块盒子账户时显示该账户的昵称与头像，
     *        未登录时回落到当前游戏账户（启动游戏始终使用游戏账户，与顶栏显示无关）
     */
    void syncTopBarAccount();
    /** 下载方块盒子账户头像并更新顶栏（下载失败时保留首字母占位图） */
    void loadTopBarForumAvatar(const QString &avatarPath);
    QString findFirstVersionInGameRoot(const QString &gameRootPath) const;
    void onModDownloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile);
    /** 把实例里已装的旧版本模组替换成所选版本（下载新文件后删除 oldFilePaths 里的旧文件） */
    void onModConvertRequested(const ModInfo &modInfo, const ModVersionFile &versionFile,
                               const QStringList &oldFilePaths);
    void onContentDownloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile, ContentType contentType);
    /**
     * @brief 在线安装整合包：把网络整合包下载下来后按清单装成一个完整的新实例
     *
     * 整合包不是"塞进某个实例文件夹"的资源，装它等于新建实例（原版版本 + 加载器 +
     * 模组 + 覆盖文件），所以不走普通资源下载落地流程。
     */
    void installOnlineModpack(const ModInfo &modInfo, const ModVersionFile &versionFile);
    /** 内容详情页（资源包/光影包）的版本转换：下载新文件后删除 oldFilePaths 里的旧文件 */
    void onContentConvertRequested(const ModInfo &modInfo, const ModVersionFile &versionFile,
                                   ContentType contentType, const QStringList &oldFilePaths);
    /**
     * @brief 处理"一键安装/下载"请求（详情页按钮、列表卡片下载图标共用）
     * @param modInfo       资源信息（可能尚无 versionFiles，需拉取详情）
     * @param contentType   资源类型
     * @param depth         递归深度，0=用户直接点击的根资源，最大递归到 kMaxCardDownloadDepth
     * @param gameVersion   目标游戏版本（空=使用当前实例版本），用于挑选兼容的最新正式版
     * @param loader        目标加载器（空=使用当前实例加载器）
     *
     * 流程：挑出兼容的最新正式版 → 下载主资源 → 遍历该版本的必备前置 → 递归下载
     */
    void onCardDownloadRequested(const ModInfo &modInfo, ContentType contentType, int depth = 0,
                                 const QString &gameVersion = QString(),
                                 const QString &loader = QString());
    /**
     * @brief 本地资源页空状态「快速安装对应模组」：按实例加载器挑选并安装推荐模组
     * @param modKey 模组标识：shader_loader（光影加载器）/ projection（投影）/ replaymod / flashback
     *
     * 光影按加载器分流（Fabric / Quilt / NeoForge 装 Iris，Forge 装 Oculus）；
     * 安装前先确认存在适配当前实例版本与加载器的文件，没有就说明原因并引导到下载页。
     */
    void onQuickInstallModRequested(const QString &modKey);
    /**
     * @brief 下载投影时实例没有 schematics 目录：自动装投影模组并准备目录
     *
     * 同一实例只自动尝试一次；已经有 Litematica 或装不成时只提示，不打断下载。
     */
    void ensureProjectionModForDownload();
    /** 快速装模组的具体实现；quiet = true 时走后台自动安装，失败只提示不跳页 */
    void installQuickMod(const QString &modKey, bool quiet);
    void onClipboardLinkDetected(const ClipboardLinkInfo &info);

    // ===== 基岩版社区资源（CurseForge 游戏 ID 78022） =====
    /**
     * @brief 应用版本模式（Java/基岩）并同步到侧边栏/首页/收藏夹/资源主页
     *
     * 顶栏「Java版/基岩版」切换与启动时初始化共用；只在模式真正变化时重建。
     */
    void applyBedrockEdition(bool bedrock);
    /** 打开基岩版社区资源浏览页（childIndex 按基岩子导航：1=资源包 2=材质包 3=地图 4=脚本 10=附加包） */
    void showBedrockContentPage(int classId);
    /** 打开基岩版社区资源详情页（item 的 bedrockClassId 决定分类语义） */
    void showBedrockContentDetailPage(const ModInfo &info, int bedrockClassId);
    /** 基岩版子导航 childIndex → CurseForge classId（1资源包/2材质包/3地图/4脚本/10附加包，其余 0=全部） */
    static int bedrockClassIdForChildIndex(int childIndex);
    /** 基岩版版本下载页（mcappx/mcapks/bbk 多源列表；顶栏返回回资源主页） */
    void showBedrockDownloadPage();
    /** 打开基岩版版本详情下载页 */
    void showBedrockVersionDetailPage(const struct BedrockVersionEntry &entry);
    /** 基岩版实例管理页（设置→基岩版游戏→管理实例 进入；选择/新建/导入/启动实例） */
    Q_INVOKABLE void showBedrockInstanceSelectPage();

    static constexpr int kMaxCardDownloadDepth = 3; ///< 一键下载递归前置的最大深度，防止循环依赖
    QSet<QString> m_cardDownloadVisited;            ///< 一次"一键下载"流程中已处理的资源 ID（source:id）
    QSet<QString> m_cardDownloadDetailTried;        ///< 一次"一键下载"流程中已请求过详情的资源 ID，避免重复补全与死循环
    QSet<QString> m_projectionModAutoTried;         ///< 已自动尝试装过投影模组的实例路径（失败也不反复重试）
};
