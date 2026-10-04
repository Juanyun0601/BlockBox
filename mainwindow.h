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

#include "utils/DownloadTaskManager.h"
#include "utils/content/ContentData.h"
#include "utils/FavoritesManager.h"

class BackgroundWidget;
class TopBar;
class TaskBar;
class SideBar;
class InstanceAssistantWindow;
class InstanceSelectPage;
class AccountManagePage;
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
class InstanceManagePage;
class AiChatPage;
class JavaDownloadPage;
class HomePage;
class FavoritesPage;
class SkinEditorPage;
class PluginPage;
struct ModInfo;
struct ModVersionFile;
class GameLauncher;
class QScrollBar;
class SubNavPanel;
class ClipboardMonitor;
struct ClipboardLinkInfo;

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
    PluginPage = 25
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onSideBarItemClicked(int parentIndex);
    void onParentNavClicked(int parentIndex);
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
    void onAddAccountPageOpened();
    void onAccountManagePageOpened();
    void onTopBarBackClicked();
    void onSearchClicked();
    void onScrollToTop();
    void onRefreshClicked();
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
    void onSaveSettingsOpened();
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
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
    AccountManagePage *m_accountManagePage;
    LaunchDetailsPage *m_launchDetailsPage;
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
    AiChatPage *m_aiChatPage = nullptr;
    JavaDownloadPage *m_javaDownloadPage = nullptr;
    HomePage *m_homePage = nullptr;
    FavoritesPage *m_favoritesPage = nullptr;
    SkinEditorPage *m_skinEditorPage = nullptr;
    PluginPage *m_pluginPage = nullptr;

    ClipboardMonitor *m_clipboardMonitor = nullptr;
    bool m_clipboardDialogOpen = false; ///< 剪贴板确认框是否已弹出（防剪贴板变化重入叠加弹窗）

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
    bool m_launchRetryAttempted = false; // 标记本次启动是否已执行过"补全文件后重试"，避免无限重试
    bool m_launchRetryPending = false;  // 标记重试已调度但尚未执行（防止重复 gameCrashed 信号清除任务）

    QMap<PageIndex, bool> m_pageInitialized;
    QRecursiveMutex m_initMutex;
    PageIndex m_previousPage;
    bool m_hasPreviousPage;
    int m_lastSidebarIndex;
    int m_lastChildSidebarIndex[8];

    void initUI();
    void initPages();
    void initGameLauncher();
    void updateBackgroundWidget();
    void setSideBarVisible(bool visible);

    void initEssentialPages();
    void ensurePageInitialized(PageIndex pageIndex);
    bool isPageInitialized(PageIndex pageIndex) const;

    void initInstanceSelectPage();
    void initAccountManagePage();
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
    void showJavaDownloadPage();
    void showFavoritesPage(const QString &folderId);
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
    void showSkinEditorPage();
    void showLaunchDetailsPage();
    void showTaskListPage();
    void showTaskDetailPage(const QString &taskId);
    void showModDownloadPage();
    void showModDetailPage(const ModInfo &modInfo);
    void showLocalModDetailPage(const ModInfo &modInfo);
    void showModpackImportPage();
    void showModpackExportPage(const QString& instancePath = QString());
    void showSearchPage();

    /** 带额外 gameArgs 启动游戏（用于存档/服务器快捷启动） */
    void launchGameWithExtraArgs(const QStringList &extraGameArgs);
    void showContentDownloadPage(ContentType type);
    void showContentDetailPage(const ModInfo &info, ContentType type);
    void showContentListPage(ContentType type);
    void updateCurrentInstance(const QString &instancePath);
    void saveCurrentInstancePath();
    void loadCurrentInstancePath();
    void restoreDefaultAccount();
    QString findFirstVersionInGameRoot(const QString &gameRootPath) const;
    void onModDownloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile);
    void onContentDownloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile, ContentType contentType);
    /**
     * @brief 处理资源卡片"一键下载"按钮点击
     * @param modInfo       资源信息（可能尚无 versionFiles，需拉取详情）
     * @param contentType   资源类型
     * @param depth         递归深度，0=用户直接点击的根资源，最大递归到 kMaxCardDownloadDepth
     *
     * 流程：拉取最新版本 → 下载主资源 → 遍历该版本的必备前置 → 递归下载
     */
    void onCardDownloadRequested(const ModInfo &modInfo, ContentType contentType, int depth = 0);
    void onClipboardLinkDetected(const ClipboardLinkInfo &info);

    static constexpr int kMaxCardDownloadDepth = 3; ///< 一键下载递归前置的最大深度，防止循环依赖
    QSet<QString> m_cardDownloadVisited;            ///< 一次"一键下载"流程中已处理的资源 ID（source:id）
};
