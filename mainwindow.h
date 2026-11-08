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
class InstanceSelectPage;
class ForumAccountPage;
class LaunchDetailsPage;
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
class InstanceManagePage;
class AiChatPage;
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
class FeedbackPage;
class FeedbackComposePage;
class ForumReportPage;
class PublishPage;
class PublishResourcePage;
class PublishPlatformPage;
class PublishBoardPage;
class OnboardingWizard;
struct ModInfo;
struct ModVersionFile;
class GameLauncher;
class QScrollBar;
class SubNavPanel;
class ClipboardMonitor;
struct ClipboardLinkInfo;
class FileDropOverlay;
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
    ForumReportPage = 41
};

/** 侧边栏导航索引（SideBar::buildNavStructure 的顺序）：0首页 1资源 2设置 3AI助手 4实例管理(隐藏) 5插件 6社区 7发布 8反馈 */
constexpr int PublishNavIndex = 7;
constexpr int FeedbackNavIndex = 8;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
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
    void animateContentEntrance(QWidget *page);

    void onLaunchStatusChanged(int status);
    void onGameStarted();
    void onGameStopped(int exitCode);
    void onGameCrashed(const QString &error);
    void onLaunchProgressChanged(int progress, const QString& message);
    void onLaunchDetailAdded(const QString& detail);
    void onErrorReportGenerated(const QString& report);
    void onFileCompletionProgress(int completed, int total, const QString& currentFile);
    void onFileCompletionFinished(bool success);

    void onLaunchGameClicked();
    void onLaunchGameClicked(const QString &instancePath);
    void onQuickLaunchSaveClicked(const QString &saveName);
    void onQuickLaunchServerClicked(const QString &address, quint16 port);
    /** 联机大厅「一键加入」：按房间要求挑最合适的实例，启动游戏并连进房间 */
    void onLobbyJoinRequested(const QString &address, quint16 port, const QString &gameVersion,
                              const QString &loader, const QStringList &mods);
    /** 首页好友卡片「一起玩」：直接加入好友正在开放的联机房间 */
    void onFriendRoomJoinRequested(const ForumLobbyRoom &room);
    /** 首页好友房间的陶瓦联机状态变化：核心就绪后自动连进房间 */
    void onFriendRoomJoinStateChanged();
    void onSaveSettingsOpened();
    void onSaveMapOpened();
    void onShaderSettingsOpened();
    void onProjectionEditOpened();
    void onLaunchTaskCardClicked();
    void onShowDetailsPage();
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
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    /// 进社区前先过一道人机验证，已经验过就直接返回 true
    bool ensureHumanCheck();

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
    InstanceAssistantWindow *m_instanceAssistantWindow = nullptr;
    /** 账户管理页界面（与方块盒子论坛账号打通，桌面端唯一的账户页） */
    ForumAccountPage *m_forumAccountPage = nullptr;
    /** 账户管理页容器（入场动画作用在内层页面，故保留一层 QStackedWidget） */
    QStackedWidget *m_accountUiStack = nullptr;    LaunchDetailsPage *m_launchDetailsPage;
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
    AiChatPage *m_aiChatPage = nullptr;
    JavaDownloadPage *m_javaDownloadPage = nullptr;
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
    /** 软件内反馈页（左侧导航栏底部入口），只显示反馈列表 */
    FeedbackPage *m_feedbackPage = nullptr;
    /** 提交新反馈页（反馈页顶部「提交新反馈」进入），提交走论坛账号 */
    FeedbackComposePage *m_feedbackComposePage = nullptr;
    /** 举报楼层页（帖子详情页「举报」进入），分类 + 补充说明走论坛账号 */
    ForumReportPage *m_forumReportPage = nullptr;
    /** 发布中心：入口页 / 资源类型页 / 平台跳转页 / 帖子板块页 */
    PublishPage *m_publishPage = nullptr;
    PublishResourcePage *m_publishResourcePage = nullptr;
    PublishPlatformPage *m_publishPlatformPage = nullptr;
    PublishBoardPage *m_publishBoardPage = nullptr;
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

    QList<QShortcut *> m_shortcuts;

    BackgroundWidget *m_backgroundWidget;
    GameLauncher *m_gameLauncher;
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
    QString m_friendRoomJoinVersion;
    QString m_friendRoomJoinLoader;
    QStringList m_friendRoomJoinMods;
    bool m_launchRetryAttempted = false; // 标记本次启动是否已执行过"补全文件后重试"，避免无限重试
    bool m_launchRetryPending = false;  // 标记重试已调度但尚未执行（防止重复 gameCrashed 信号清除任务）

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
    void updateBackgroundWidget();
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

    /** 从拖拽 mime 中取出方块盒子更新安装包的本地路径列表 */
    QStringList extractUpdatePackages(const QMimeData *mime) const;
    /** 更新包拖入覆盖层（悬停/松开时显示提示） */
    FileDropOverlay *m_dropOverlay = nullptr;
    QStringList m_pendingDropPackages;

    void initInstanceSelectPage();
    void initAccountManagePage();
    /** 论坛账户管理页：账户管理页的唯一界面（按需创建，创建后常驻） */
    void buildForumAccountManagePage();
    /** 「添加角色」弹窗：离线 / 微软正版 / 第三方添加游戏账户 */
    void openAddAccountDialog();
    void initLaunchDetailsPage();
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
    void initForumReportPage();
    void initForumFriendsPage();
    void initForumPluginListPage();
    void initPublishPage();
    void initPublishResourcePage();
    void initPublishPlatformPage();
    void initPublishBoardPage();
    /** 新手引导页（首次启动时懒加载创建） */
    void initOnboardingPage();
    /** 引导页走完：回到首页，必要时通知主程序做首次启动后续动作 */
    void onOnboardingFinished();
    /** 引导页「返回设置」/ 顶栏返回 */
    void onOnboardingBackRequested();
    /** 引导页切换语言：重启进程以干净加载新语言 */
    void onOnboardingRestartRequested();
    // ── 发布中心：入口 / 资源类型 / 平台跳转 / 帖子板块 ──
    void showPublishPage();
    void showPublishResourcePage();
    void showPublishPlatformPage(PublishKit::ResourceKind kind);
    void showPublishBoardPage();
    /** 插件发布页（复用论坛发帖页，预选「插件与开发」板块） */
    void showPublishPluginCompose();
    /** 帖子发帖详情页（复用论坛发帖页，预选所选板块） */
    void showPublishPostCompose(const QString &boardSlug);
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
    void showFeedbackPage();
    /** 提交新反馈页（从反馈列表页进入，返回落点也是反馈列表页） */
    void showFeedbackComposePage();
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
    void presentForumReportPage();
    void presentForumFriends();
    /** 回到社区页（恢复侧边栏选中与顶栏标题） */
    void returnToCommunityPage();
    /** 回到「设置 > 关于」（设置里反馈入口的返回落点） */
    void returnToSettingsAbout();
    /** 回到反馈列表页（提交新反馈页的返回落点） */
    void returnToFeedbackList();
    void initSettingsPage();
    void showJavaDownloadPage();
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
    /** 打开实例助手窗口（顶栏与首页经典模式面板共用），注入当前实例上下文 */
    void openInstanceAssistant();
    void showSkinEditorPage();
    void showLaunchDetailsPage();
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
    void onContentDownloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile, ContentType contentType);
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

    static constexpr int kMaxCardDownloadDepth = 3; ///< 一键下载递归前置的最大深度，防止循环依赖
    QSet<QString> m_cardDownloadVisited;            ///< 一次"一键下载"流程中已处理的资源 ID（source:id）
    QSet<QString> m_cardDownloadDetailTried;        ///< 一次"一键下载"流程中已请求过详情的资源 ID，避免重复补全与死循环
};
