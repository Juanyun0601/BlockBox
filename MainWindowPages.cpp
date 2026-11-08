/**
 * @file   MainWindowPages.cpp
 * @brief  涓荤獥鍙ｉ〉闈㈢鐞嗗疄鐜? * @author BlockBox Team
 * @date   2026-05-09
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QScrollArea>
#include <QUrl>

#include "utils/PageTransitionAnimator.h"
#include "utils/ContentAnimator.h"
#include "components/BackgroundWidget.h"
#include "components/InstanceAssistantWindow.h"
#include "components/NotificationManager.h"
#include "components/SideBar.h"
#include "components/SubNavPanel.h"
#include "components/TaskBar.h"
#include "components/TopBar.h"
#include "utils/GameLauncher.h"
#include "pages/AiChatPage.h"
#include "pages/JavaDownloadPage.h"
#include "pages/AddAccountDialog.h"
#include "pages/ForumAccountPage.h"
#include "pages/ForgeVersionListPage.h"
#include "pages/HomePage.h"
#include "pages/InstallInstancePage.h"
#include "pages/InstanceManagePage.h"
#include "pages/InstanceSelectPage.h"
#include "pages/LaunchDetailsPage.h"
#include "pages/LoaderDetailPage.h"
#include "pages/OnboardingWizard.h"
#include "pages/ResourcesPage.h"
#include "pages/SettingsPage.h"
#include "pages/TaskDetailPage.h"
#include "pages/ModDetailPage.h"
#include "pages/LocalModDetailPage.h"
#include "pages/ModDownloadPage.h"
#include "pages/ContentDownloadPage.h"
#include "pages/ForumPluginListPage.h"
#include "pages/PluginPage.h"
#include "pages/ModpackImportPage.h"
#include "pages/ModpackExportPage.h"
#include "pages/SearchPage.h"
#include "pages/SkinEditorPage.h"
#include "pages/TaskListPage.h"
#include "pages/NewsPage.h"
#include "pages/CommunityForumPage.h"
#include "pages/ForumThreadDetailPage.h"
#include "pages/ForumThreadListPage.h"
#include "pages/ForumComposerPage.h"
#include "pages/ForumUserProfilePage.h"
#include "pages/FeedbackPage.h"
#include "pages/FeedbackComposePage.h"
#include "pages/ForumReportPage.h"
#include "pages/ForumFriendsPage.h"
#include "pages/MultiplayerLobbyPage.h"
#include "pages/PublishPage.h"
#include "pages/PublishResourcePage.h"
#include "pages/PublishPlatformPage.h"
#include "pages/PublishBoardPage.h"
#include "utils/PerformanceMonitor.h"
#include "utils/SettingsManager.h"
#include "utils/TouchScroll.h"
#include "utils/VersionDownloader.h"
#include "utils/mod/ModData.h"

void MainWindow::initPages()
{
    PerformanceMonitor::instance()->startMeasurement("页面初始化");
    initEssentialPages();
    PerformanceMonitor::instance()->endMeasurement("页面初始化");
}

void MainWindow::refreshHomeRecentPlays()
{
    if (m_homePage) {
        m_homePage->refreshRecentPlays();
    }
}

void MainWindow::initEssentialPages()
{
    PerformanceMonitor::instance()->startMeasurement("主页初始化");
    QScrollArea *scrollArea1 = new QScrollArea(this);
    scrollArea1->setWidgetResizable(true);
    scrollArea1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scrollArea1->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *homePage = new HomePage(this);
    m_homePage = homePage;
    scrollArea1->setWidget(homePage);
    m_stackedWidget->addWidget(scrollArea1);

    // 连接最近游玩信号
    // 首页空态轮播卡片：点击后跳到「设置 → 界面设置」添加自己的图片
    connect(homePage, &HomePage::customizeCarouselRequested,
            this, &MainWindow::openInterfaceSettings);
    connect(homePage, &HomePage::recentPlayLaunchClicked, this, [this](const QString &gameRootPath) {
        // 根据游戏根路径查找第一个版本并启动
        QString versionPath = findFirstVersionInGameRoot(gameRootPath);
        if (!versionPath.isEmpty()) {
            onLaunchGameClicked(versionPath);
        }
    });
    connect(homePage, &HomePage::recentPlayQuickLaunchClicked, this, [this](const QString &saveName, const QString &instancePath) {
        // 快捷启动：切换到对应实例后启动并直接进入存档
        if (!instancePath.isEmpty()) {
            updateCurrentInstance(instancePath);
        }
        onQuickLaunchSaveClicked(saveName);
    });
    connect(homePage, &HomePage::recentPlayQuickLaunchServerClicked, this, [this](const QString &address, quint16 port, const QString &instancePath) {
        // 快捷启动：切换到对应实例后启动并自动连接服务器
        if (!instancePath.isEmpty()) {
            updateCurrentInstance(instancePath);
        }
        onQuickLaunchServerClicked(address, port);
    });
    connect(homePage, &HomePage::recentPlayServerSettingsClicked, this, [this](const QString &instancePath) {
        // 打开服务器管理（实例管理页服务器标签页）
        if (!instancePath.isEmpty()) {
            updateCurrentInstance(instancePath);
        }
        ensurePageInitialized(PageIndex::InstanceManagePage);
        m_subNavPanel->showForParent(4, false);
        m_sideBar->clearSelection();
        m_topBar->setTitle(tr("实例>服务器"));
        m_instanceManagePage->setCurrentTab(6);
        m_subNavPanel->setSelectedChild(6);
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstanceManagePage));
    });
    connect(homePage, &HomePage::recentPlaySettingsClicked, this, [this](const QString &gameRootPath) {
        // 打开存档管理（实例管理页存档标签页）
        QString versionPath = findFirstVersionInGameRoot(gameRootPath);
        if (!versionPath.isEmpty()) {
            updateCurrentInstance(versionPath);
            ensurePageInitialized(PageIndex::InstanceManagePage);
            m_subNavPanel->showForParent(4, false);
            m_sideBar->clearSelection();
            m_topBar->setTitle(tr("实例>存档"));
            m_instanceManagePage->setCurrentTab(5);
            m_subNavPanel->setSelectedChild(5);
            m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstanceManagePage));
        }
    });
    connect(homePage, &HomePage::recentPlayInstanceSettingsClicked, this, [this](const QString &gameRootPath) {
        // 打开实例设置
        QString versionPath = findFirstVersionInGameRoot(gameRootPath);
        if (!versionPath.isEmpty()) {
            updateCurrentInstance(versionPath);
            ensurePageInitialized(PageIndex::InstanceManagePage);
            m_subNavPanel->showForParent(4, false);
            m_sideBar->clearSelection();
            m_topBar->setTitle(tr("实例>实例设置"));
            m_instanceManagePage->setCurrentTab(1);
            m_subNavPanel->setSelectedChild(1);
            m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstanceManagePage));
        }
    });
    connect(homePage, &HomePage::recentPlayOpenFolderClicked, this, [this](const QString &savePath) {
        // 打开存档文件夹
        if (!savePath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(savePath));
        }
    });
    // 首页账号卡片 → 账户管理页（论坛账户界面）
    connect(homePage, &HomePage::accountManageRequested, this, &MainWindow::showAccountManagePage);
    // 首页轮播右侧「添加首个账户」→ 直接打开「添加角色」弹窗
    connect(homePage, &HomePage::addFirstAccountRequested, this,
            &MainWindow::openAddAccountDialog);
    // 首页「官方新闻 · 查看全部」→ 软件内置新闻列表页
    connect(homePage, &HomePage::newsPageRequested, this, &MainWindow::showNewsPage);
    // 经典模式皮肤面板：启动游戏 / 实例选择 / 实例设置 / 实例助手
    connect(homePage, &HomePage::launchRequested, this, qOverload<>(&MainWindow::onLaunchGameClicked));
    connect(homePage, &HomePage::instanceSelectRequested, this, &MainWindow::onInstanceSelectClicked);
    connect(homePage, &HomePage::instanceSettingsRequested, this, &MainWindow::onInstanceSettingsClicked);
    connect(homePage, &HomePage::instanceAssistantRequested, this, &MainWindow::openInstanceAssistant);
    connect(homePage, &HomePage::recentPlayCopyNameClicked, this, [this](const QString &name) {
        // 复制存档名到剪贴板
        if (!name.isEmpty()) {
            QGuiApplication::clipboard()->setText(name);
        }
    });

    // 首页左侧好友列表：点卡片聊天 / 好友开房「一起玩」/ 登录 / 查看全部
    connect(homePage, &HomePage::friendChatRequested, this, [this](int userId) {
        m_forumReturnAction = [this]() { onBackToMain(); };
        showForumFriends(userId);
    });
    connect(homePage, &HomePage::friendPanelOpenRequested, this, [this]() {
        m_forumReturnAction = [this]() { onBackToMain(); };
        showForumFriends();
    });
    connect(homePage, &HomePage::friendPanelLoginRequested,
            this, &MainWindow::openForumAccountLogin);
    connect(homePage, &HomePage::friendRoomJoinRequested,
            this, &MainWindow::onFriendRoomJoinRequested);

    PerformanceMonitor::instance()->endMeasurement("主页初始化");

    PerformanceMonitor::instance()->startMeasurement("资源页初始化");
    m_resourcesPage = new ResourcesPage(this);
    m_stackedWidget->addWidget(m_resourcesPage);
    m_resourcesPage->setInstancePath(m_currentInstancePath);
    m_resourcesPage->setBedrockMode(FavoritesManager::instance()->isBedrockMode());
    // 卡片点击直接转发到左侧导航项处理，行为与点击侧边栏完全一致
    connect(m_resourcesPage, &ResourcesPage::navItemClicked, this, [this](int childIndex) {
        onChildNavClicked(1, childIndex);
    });
    PerformanceMonitor::instance()->endMeasurement("资源页初始化");

    // 设置页改为首次进入时创建（见 initSettingsPage）：
    // 其控件数量较多，启动阶段提前构造会白白拖慢启动
    QWidget *placeholderSettings = new QWidget(this);
    m_stackedWidget->addWidget(placeholderSettings);

    QWidget *placeholderInstanceSelect = new QWidget(this);
    m_stackedWidget->addWidget(placeholderInstanceSelect);
    
    QWidget *placeholderAccountManage = new QWidget(this);
    m_stackedWidget->addWidget(placeholderAccountManage);
    
    QWidget *placeholderLaunchDetails = new QWidget(this);
    m_stackedWidget->addWidget(placeholderLaunchDetails);
    
    QWidget *placeholderInstallInstance = new QWidget(this);
    m_stackedWidget->addWidget(placeholderInstallInstance);
    
    QWidget *placeholderLoaderDetail = new QWidget(this);
    m_stackedWidget->addWidget(placeholderLoaderDetail);
    
    QWidget *placeholderForgeVersionList = new QWidget(this);
    m_stackedWidget->addWidget(placeholderForgeVersionList);
    
    QWidget *placeholderTaskList = new QWidget(this);
    m_stackedWidget->addWidget(placeholderTaskList);

    QWidget *placeholderTaskDetail = new QWidget(this);
    m_stackedWidget->addWidget(placeholderTaskDetail);

    QWidget *placeholderModDownload = new QWidget(this);
    m_stackedWidget->addWidget(placeholderModDownload);

    QWidget *placeholderModDetail = new QWidget(this);
    m_stackedWidget->addWidget(placeholderModDetail);

    QWidget *placeholderModpackImport = new QWidget(this);
    m_stackedWidget->addWidget(placeholderModpackImport);

    QWidget *placeholderSearch = new QWidget(this);
    m_stackedWidget->addWidget(placeholderSearch);

    // 内容下载/详情/列表页占位
    QWidget *placeholderContentDownload = new QWidget(this);
    m_stackedWidget->addWidget(placeholderContentDownload);

    QWidget *placeholderContentDetail = new QWidget(this);
    m_stackedWidget->addWidget(placeholderContentDetail);

    QWidget *placeholderContentList = new QWidget(this);
    m_stackedWidget->addWidget(placeholderContentList);

    // 实例管理页面
    m_instanceManagePage = new InstanceManagePage(this);
    m_stackedWidget->addWidget(m_instanceManagePage);
    connect(m_instanceManagePage, &InstanceManagePage::modDetailRequested,
            this, &MainWindow::showLocalModDetailPage);
    connect(m_instanceManagePage, &InstanceManagePage::modDownloadSearchRequested,
            this, [this](const QString &keyword) {
        showModDownloadPage();
        if (m_modDownloadPage)
            m_modDownloadPage->setSearchText(keyword);
    });
    connect(m_instanceManagePage, &InstanceManagePage::fileSearchRequested,
            this, [this](ContentType type, const QString &keyword) {
        showContentDownloadPage(type);
        if (m_contentDownloadPage)
            m_contentDownloadPage->setSearchText(keyword);
    });
    // 本地资源页空状态「快速安装对应模组」：光影加载器 / 投影 / 录像模组
    connect(m_instanceManagePage, &InstanceManagePage::quickInstallModRequested,
            this, &MainWindow::onQuickInstallModRequested);
    connect(m_instanceManagePage, &InstanceManagePage::launchGameRequested,
            this, qOverload<>(&MainWindow::onLaunchGameClicked));
    connect(m_instanceManagePage, &InstanceManagePage::quickLaunchSaveRequested,
            this, &MainWindow::onQuickLaunchSaveClicked);
    connect(m_instanceManagePage, &InstanceManagePage::quickLaunchServerRequested,
            this, &MainWindow::onQuickLaunchServerClicked);
    connect(m_instanceManagePage, &InstanceManagePage::saveSettingsOpened,
            this, &MainWindow::onSaveSettingsOpened);
    connect(m_instanceManagePage, &InstanceManagePage::saveMapOpened,
            this, &MainWindow::onSaveMapOpened);
    // 存档地图页在前台时把旋转背景降到省电档：地图拖动大图、
    // 后台还在并行渲染瓦片，背景不该再占 UI 线程
    connect(m_instanceManagePage, &InstanceManagePage::worldMapActiveChanged,
            this, [this](bool active) {
        if (m_backgroundWidget)
            m_backgroundWidget->setEcoMode(active);
    });
    connect(m_instanceManagePage, &InstanceManagePage::shaderSettingsOpened,
            this, &MainWindow::onShaderSettingsOpened);
    connect(m_instanceManagePage, &InstanceManagePage::projectionEditOpened,
            this, &MainWindow::onProjectionEditOpened);
    connect(m_instanceManagePage, &InstanceManagePage::instanceInfoChanged,
            this, [this](const QString &displayName, const QString &iconPath) {
        Q_UNUSED(iconPath);
        // 实例名称修改后同步顶栏显示（空值表示使用文件夹名）
        const QString newName = displayName.isEmpty()
            ? m_currentInstancePath.split("/").last()
            : displayName;
        m_topBar->setInstanceName(newName);
        // 刷新实例选择页卡片名称/图标
        if (m_instanceSelectPage)
            m_instanceSelectPage->updateInstanceList();
    });
    // 本地资源卡片「问问AI」：跳转 AI 助手并引用当前资源
    connect(m_instanceManagePage, &InstanceManagePage::askAiRequested,
            this, &MainWindow::openAiChatWithReference);

    // 整合包导出页面占位
    QWidget *placeholderModpackExport = new QWidget(this);
    m_stackedWidget->addWidget(placeholderModpackExport);

    // 本地模组详情页
    m_localModDetailPage = new LocalModDetailPage(this);
    connect(m_localModDetailPage, &LocalModDetailPage::backRequested, this, [this]() {
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstanceManagePage));
        // 返回"实例管理>模组"：恢复侧边栏同时恢复实例管理子导航
        restoreSideBarWithSubNav(4, 10);
        m_topBar->setTitle(tr("实例管理"));
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    });
    connect(m_localModDetailPage, &LocalModDetailPage::askAiRequested,
            this, &MainWindow::openAiChatWithReference);
    connect(m_localModDetailPage, &LocalModDetailPage::remoteDetailRequested,
            this, [this](const ModInfo &info) {
        // 跳转到内置在线详情页
        showModDetailPage(info, m_currentInstanceVersion, m_currentInstanceLoader);
        // 重设返回按钮：从本地详情跳转来的，应返回实例管理页
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, [this]() {
            m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::LocalModDetailPageIndex));
            setSideBarVisible(false);
            m_topBar->setTitle(tr("模组详情"));
            disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
            connect(m_topBar, &TopBar::backClicked, this, [this]() {
                m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstanceManagePage));
                // 返回"实例管理>模组"：恢复侧边栏同时恢复实例管理子导航
                restoreSideBarWithSubNav(4, 10);
                m_topBar->setTitle(tr("实例管理"));
                disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
                connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
            });
        });
    });
    m_stackedWidget->addWidget(m_localModDetailPage);
    m_pageInitialized[PageIndex::LocalModDetailPageIndex] = true;

    // AI 助手占位
    QWidget *placeholderAiChat = new QWidget(this);
    m_stackedWidget->addWidget(placeholderAiChat);

    // Java 下载页占位
    QWidget *placeholderJavaDownload = new QWidget(this);
    m_stackedWidget->addWidget(placeholderJavaDownload);

    // 收藏夹页占位（保持 PageIndex 枚举与 stackedWidget 索引对齐）
    QWidget *placeholderFavorites = new QWidget(this);
    m_stackedWidget->addWidget(placeholderFavorites);

    // 皮肤制作页占位
    QWidget *placeholderSkinEditor = new QWidget(this);
    m_stackedWidget->addWidget(placeholderSkinEditor);

    // 插件页占位
    QWidget *placeholderPlugin = new QWidget(this);
    m_stackedWidget->addWidget(placeholderPlugin);

    // 官方新闻列表页占位（保持 PageIndex 枚举与 stackedWidget 索引对齐）
    QWidget *placeholderNews = new QWidget(this);
    m_stackedWidget->addWidget(placeholderNews);

    // 社区（方块盒子论坛首页）占位
    QWidget *placeholderCommunity = new QWidget(this);
    m_stackedWidget->addWidget(placeholderCommunity);

    // 论坛深度集成四页占位（保持 PageIndex 枚举与 stackedWidget 索引对齐）
    QWidget *placeholderForumThreadDetail = new QWidget(this);
    m_stackedWidget->addWidget(placeholderForumThreadDetail);
    QWidget *placeholderForumThreadList = new QWidget(this);
    m_stackedWidget->addWidget(placeholderForumThreadList);
    QWidget *placeholderForumComposer = new QWidget(this);
    m_stackedWidget->addWidget(placeholderForumComposer);
    QWidget *placeholderForumUserProfile = new QWidget(this);
    m_stackedWidget->addWidget(placeholderForumUserProfile);

    // 软件内反馈页占位（左侧导航栏底部的反馈入口）
    QWidget *placeholderFeedback = new QWidget(this);
    m_stackedWidget->addWidget(placeholderFeedback);

    // 论坛好友与私信页占位（保持 PageIndex 枚举与 stackedWidget 索引对齐）
    QWidget *placeholderForumFriends = new QWidget(this);
    m_stackedWidget->addWidget(placeholderForumFriends);

    // 提交新反馈页占位（反馈页顶部「提交新反馈」按钮进入）
    QWidget *placeholderFeedbackCompose = new QWidget(this);
    m_stackedWidget->addWidget(placeholderFeedbackCompose);

    // 资源页「插件」列表页占位（论坛插件帖列表，按需创建）
    QWidget *placeholderForumPluginList = new QWidget(this);
    m_stackedWidget->addWidget(placeholderForumPluginList);

    // 发布中心四页占位（发布入口 / 资源类型 / 平台跳转 / 帖子板块）
    QWidget *placeholderPublish = new QWidget(this);
    m_stackedWidget->addWidget(placeholderPublish);
    QWidget *placeholderPublishResource = new QWidget(this);
    m_stackedWidget->addWidget(placeholderPublishResource);
    QWidget *placeholderPublishPlatform = new QWidget(this);
    m_stackedWidget->addWidget(placeholderPublishPlatform);
    QWidget *placeholderPublishBoard = new QWidget(this);
    m_stackedWidget->addWidget(placeholderPublishBoard);

    // 新手引导页占位（首次启动进入；设置里也可再次打开）
    QWidget *placeholderOnboarding = new QWidget(this);
    m_stackedWidget->addWidget(placeholderOnboarding);

    // 举报楼层页占位（帖子详情页「举报」进入；保持 PageIndex 枚举与 stackedWidget 索引对齐）
    QWidget *placeholderForumReport = new QWidget(this);
    m_stackedWidget->addWidget(placeholderForumReport);

    // 手机端：把启动时就存在的各页面内容滚动区统一开启手指拖动滚动
    // （懒加载页面在切换到时由页面切换钩子补挂，见 mainwindow.cpp）
    for (int i = 0; i < m_stackedWidget->count(); ++i)
        TouchScroll::enablePageDragScroll(m_stackedWidget->widget(i));

    m_pageAnimator->animateToIndex(0);
}

bool MainWindow::isPageInitialized(PageIndex pageIndex) const
{
    return m_pageInitialized.value(pageIndex, false);
}

void MainWindow::ensurePageInitialized(PageIndex pageIndex)
{
    QMutexLocker locker(&m_initMutex);
    
    if (isPageInitialized(pageIndex)) {
        return;
    }
    
    switch (pageIndex) {
    case PageIndex::InstanceSelectPage:
        initInstanceSelectPage();
        break;
    case PageIndex::AccountManagePage:
        initAccountManagePage();
        break;
    case PageIndex::LaunchDetailsPage:
        initLaunchDetailsPage();
        break;
    case PageIndex::InstallInstancePage:
        initInstallInstancePage();
        break;
    case PageIndex::LoaderDetailPage:
        initLoaderDetailPage();
        break;
    case PageIndex::ForgeVersionListPage:
        initForgeVersionListPage();
        break;
    case PageIndex::TaskListPage:
        initTaskListPage();
        break;
    case PageIndex::TaskDetailPage:
        initTaskDetailPage();
        break;
    case PageIndex::ModDownloadPage:
        initModDownloadPage();
        break;
    case PageIndex::ModDetailPage:
        initModDetailPage();
        break;
    case PageIndex::ModpackImportPage:
        initModpackImportPage();
        break;
    case PageIndex::ModpackExportPageIndex:
        initModpackExportPage();
        break;
    case PageIndex::SearchPage:
        initSearchPage();
        break;
    case PageIndex::AiChatPage:
        initAiChatPage();
        break;
    case PageIndex::JavaDownloadPage:
        initJavaDownloadPage();
        break;
    case PageIndex::SkinEditorPage:
        initSkinEditorPage();
        break;
    case PageIndex::PluginPage:
        initPluginPage();
        break;
    case PageIndex::NewsPage:
        initNewsPage();
        break;
    case PageIndex::CommunityPage:
        initCommunityPage();
        break;
    case PageIndex::ForumThreadDetailPage:
        initForumThreadDetailPage();
        break;
    case PageIndex::ForumThreadListPage:
        initForumThreadListPage();
        break;
    case PageIndex::ForumComposerPage:
        initForumComposerPage();
        break;
    case PageIndex::ForumUserProfilePage:
        initForumUserProfilePage();
        break;
    case PageIndex::FeedbackPage:
        initFeedbackPage();
        break;
    case PageIndex::FeedbackComposePage:
        initFeedbackComposePage();
        break;
    case PageIndex::ForumFriendsPage:
        initForumFriendsPage();
        break;
    case PageIndex::ForumPluginListPage:
        initForumPluginListPage();
        break;
    case PageIndex::PublishPage:
        initPublishPage();
        break;
    case PageIndex::PublishResourcePage:
        initPublishResourcePage();
        break;
    case PageIndex::PublishPlatformPage:
        initPublishPlatformPage();
        break;
    case PageIndex::PublishBoardPage:
        initPublishBoardPage();
        break;
    case PageIndex::OnboardingPage:
        initOnboardingPage();
        break;
    case PageIndex::ForumReportPage:
        initForumReportPage();
        break;
    case PageIndex::SettingsPage:
        initSettingsPage();
        break;
    default:
        break;
    }
}


void MainWindow::initModDownloadPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ModDownloadPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("模组下载页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ModDownloadPage));
    m_modDownloadPage = new ModDownloadPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ModDownloadPage), m_modDownloadPage);
    delete placeholder;
    connect(m_modDownloadPage, &ModDownloadPage::modClicked, this, [this](const ModInfo &info) {
        m_currentInstanceVersion = m_modDownloadPage->selectedGameVersion();
        QString loader = m_modDownloadPage->selectedLoader();
        if (!loader.isEmpty())
            m_currentInstanceLoader = loader;
        // 把列表页当前的实例筛选原样带到详情页（选“任意”时为空即不筛选）
        showModDetailPage(info, m_modDownloadPage->selectedGameVersion(), loader);
    });
    connect(m_modDownloadPage, &ModDownloadPage::instanceSelectionChanged,
            this, &MainWindow::applyInstanceContext);
    m_pageInitialized[PageIndex::ModDownloadPage] = true;
    PerformanceMonitor::instance()->endMeasurement("模组下载页初始化");
}

void MainWindow::initInstanceSelectPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::InstanceSelectPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("实例选择页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::InstanceSelectPage));
    m_instanceSelectPage = new InstanceSelectPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::InstanceSelectPage), m_instanceSelectPage);
    delete placeholder;
    connect(m_instanceSelectPage, &InstanceSelectPage::backToMainRequested, this, &MainWindow::onBackToMain);
    connect(m_instanceSelectPage, &InstanceSelectPage::instanceSelected, this, &MainWindow::onInstanceSelected);
    connect(m_instanceSelectPage, &InstanceSelectPage::launchGameRequested,
            this, qOverload<const QString&>(&MainWindow::onLaunchGameClicked));
    connect(m_instanceSelectPage, &InstanceSelectPage::installNewInstanceRequested, this, [this]() {
        ensurePageInitialized(PageIndex::InstallInstancePage);
        // 从实例页进入"安装新实例"时同样需要恢复资源子导航
        restoreSideBarWithSubNav(1, 0);
        m_topBar->setTitle(tr("资源>安装新实例"));
        animatedSwitchToPage(PageIndex::InstallInstancePage);
    });
    connect(m_instanceSelectPage, &InstanceSelectPage::downloadModpackRequested, this, [this]() {
        showContentDownloadPage(ContentType::Modpack);
    });
    connect(m_instanceSelectPage, &InstanceSelectPage::importModpackRequested, this, &MainWindow::showModpackImportPage);
    connect(m_instanceSelectPage, &InstanceSelectPage::exportModpackRequested, this, &MainWindow::showModpackExportPage);
    m_pageInitialized[PageIndex::InstanceSelectPage] = true;
    PerformanceMonitor::instance()->endMeasurement("实例选择页初始化");
}

void MainWindow::initAccountManagePage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::AccountManagePage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("账户管理页初始化");

    // 容器：只放论坛账户界面（保留这一层，页面切换动画的入场效果要作用在内层页面上）
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::AccountManagePage));
    m_accountUiStack = new QStackedWidget(this);
    m_accountUiStack->setObjectName(QStringLiteral("accountUiStack"));
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::AccountManagePage), m_accountUiStack);
    delete placeholder;

    buildForumAccountManagePage();
    if (m_forumAccountPage)
        m_accountUiStack->setCurrentWidget(m_forumAccountPage);

    m_pageInitialized[PageIndex::AccountManagePage] = true;
    PerformanceMonitor::instance()->endMeasurement("账户管理页初始化");
}

void MainWindow::openAddAccountDialog()
{
    // 「添加角色」：离线 / 微软正版 / 第三方三种方式都在一个弹窗里，加完直接刷新角色页
    auto *dialog = new AddAccountDialog(this);
    connect(dialog, &AddAccountDialog::accountAdded, this, [this](const QString &accountName) {
        Q_UNUSED(accountName);
        // 新账户加完就是默认账户：顶栏跟着换，角色页重新拉一遍卡片
        restoreDefaultAccount();
        if (m_forumAccountPage)
            m_forumAccountPage->refreshAccount();
    });
    dialog->exec();
    dialog->deleteLater();
}

void MainWindow::buildForumAccountManagePage()
{
    if (!m_accountUiStack || m_forumAccountPage) { return; }

    m_forumAccountPage = new ForumAccountPage(this);
    m_accountUiStack->addWidget(m_forumAccountPage);

    connect(m_forumAccountPage, &ForumAccountPage::backToMainRequested, this, &MainWindow::onBackToMain);
    connect(m_forumAccountPage, &ForumAccountPage::accountManagePageOpened,
            this, &MainWindow::onAccountManagePageOpened);

    // 「我的主题 / 回复 / 收藏 / 通知」点击：在启动器内打开帖子详情，不再跳浏览器
    connect(m_forumAccountPage, &ForumAccountPage::threadOpenRequested, this,
            [this](int threadId) {
                m_forumReturnAction = [this]() { showAccountManagePage(); };
                showForumThread(threadId);
            });

    // 「我的收藏 → 资源收藏」点击：打开在线资源详情页（与收藏夹页行为一致）
    connect(m_forumAccountPage, &ForumAccountPage::contentFavoriteOpenRequested, this,
            [this](const ModInfo &info, ContentType type) { showContentDetailPage(info, type); });

    // 「角色」页切换游戏账户：启动游戏跟着换账户；顶栏在登录方块盒子账户时仍显示社区身份
    connect(m_forumAccountPage, &ForumAccountPage::gameAccountSelected, this,
            [this](const QString &accountName, const QPixmap &avatar) {
                m_gameAccountName = accountName;
                m_gameAccountAvatar = avatar;
                syncTopBarAccount();
            });

    // 账户详情页里的「打开皮肤编辑器」：带上详情页当前账户，应用时不会落到别的账户上
    connect(m_forumAccountPage, &ForumAccountPage::skinEditorRequested, this,
            [this](const QString &accountName, const QString &accountType) {
                m_pendingSkinEditorAccount = accountName;
                m_pendingSkinEditorAccountType = accountType;
                onSkinEditorClicked();
            });

    // 皮肤编辑页可能先于论坛账户页创建：这里补建「应用后刷新」的信号连接
    if (m_skinEditorPage)
    {
        connect(m_skinEditorPage, &SkinEditorPage::skinAppliedToAccount,
                m_forumAccountPage, &ForumAccountPage::refreshAccount,
                Qt::UniqueConnection);
    }

    // 角色页「添加角色」/ 空态按钮：弹出添加账户弹窗
    connect(m_forumAccountPage, &ForumAccountPage::addGameAccountRequested,
            this, &MainWindow::openAddAccountDialog);
}

void MainWindow::initLaunchDetailsPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::LaunchDetailsPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("启动详情页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::LaunchDetailsPage));
    m_launchDetailsPage = new LaunchDetailsPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::LaunchDetailsPage), m_launchDetailsPage);
    delete placeholder;
    m_pageInitialized[PageIndex::LaunchDetailsPage] = true;
    PerformanceMonitor::instance()->endMeasurement("启动详情页初始化");
}

void MainWindow::initInstallInstancePage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::InstallInstancePage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("安装实例页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::InstallInstancePage));
    m_installInstancePage = new InstallInstancePage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::InstallInstancePage), m_installInstancePage);
    delete placeholder;
    connect(m_installInstancePage, &InstallInstancePage::backToMainRequested, this, &MainWindow::onBackToMain);
    connect(m_installInstancePage, &InstallInstancePage::instanceInstalled, this, &MainWindow::onInstanceInstalled);
    connect(m_installInstancePage, &InstallInstancePage::versionSelected, this, &MainWindow::onVersionSelected);
    // 修改现有实例：按钮点击后携带顶部实例卡片当前选中的实例信息进入修改模式
    connect(m_installInstancePage, &InstallInstancePage::modifyExistingInstanceRequested, this, [this]() {
        if (m_currentInstancePath.isEmpty()) {
            NotificationManager::showInfo(this, tr("请先在顶部实例卡片中选择一个要修改的实例"));
            return;
        }
        QString instanceName = QFileInfo(m_currentInstancePath).fileName();
        if (instanceName.isEmpty())
            instanceName = m_currentInstanceVersion;
        m_installInstancePage->enterModifyMode(m_currentInstancePath, instanceName,
                                                m_currentInstanceVersion, m_currentInstanceLoader);
    });
    connect(m_installInstancePage, &InstallInstancePage::modifyVersionSelected,
            this, &MainWindow::onModifyVersionSelected);
    m_pageInitialized[PageIndex::InstallInstancePage] = true;
    PerformanceMonitor::instance()->endMeasurement("安装实例页初始化");
}

void MainWindow::initLoaderDetailPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::LoaderDetailPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("加载器详情页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::LoaderDetailPage));
    m_loaderDetailPage = new LoaderDetailPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::LoaderDetailPage), m_loaderDetailPage);
    delete placeholder;
    connect(m_loaderDetailPage, &LoaderDetailPage::backToInstallPageRequested, this, &MainWindow::onBackToInstallPage);
    connect(m_loaderDetailPage, &LoaderDetailPage::viewAllVersionsRequested, this, &MainWindow::onViewAllVersions);
    connect(m_loaderDetailPage, &LoaderDetailPage::loaderCardClicked, this, &MainWindow::onViewAllVersions);
    connect(m_loaderDetailPage, &LoaderDetailPage::installRequested, this, [this](const QString &instancePath, const QString &instanceName, const QString &, const QMap<QString, SelectedLoader> &) {
        // 使用实际安装出来的实例目录（<游戏根>/versions/<实例名>），
        // 而不是「默认实例文件夹 + 实例名」这种拼错的路径
        QString path = QDir::fromNativeSeparators(instancePath.trimmed());
        if (path.isEmpty()) {
            QString defaultRoot = SettingsManager::instance()->getDefaultInstancePath();
            if (defaultRoot.isEmpty())
                return;
            path = QDir::fromNativeSeparators(defaultRoot) + "/versions/" + instanceName;
        }
        if (m_instanceSelectPage) {
            m_instanceSelectPage->loadInstances();
            m_instanceSelectPage->updateInstanceList();
        }
        updateCurrentInstance(path);
    });
    connect(m_loaderDetailPage, &LoaderDetailPage::instanceModified, this, &MainWindow::onInstanceModified);
    m_pageInitialized[PageIndex::LoaderDetailPage] = true;
    PerformanceMonitor::instance()->endMeasurement("加载器详情页初始化");
}

void MainWindow::initForgeVersionListPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ForgeVersionListPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Forge 版本列表页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ForgeVersionListPage));
    m_forgeVersionListPage = new ForgeVersionListPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ForgeVersionListPage), m_forgeVersionListPage);
    delete placeholder;
    connect(m_forgeVersionListPage, &ForgeVersionListPage::backToLoaderDetailRequested, this, &MainWindow::onBackToLoaderDetail);
    connect(m_forgeVersionListPage, &ForgeVersionListPage::forgeVersionSelected, this, &MainWindow::onForgeVersionSelected);
    m_pageInitialized[PageIndex::ForgeVersionListPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Forge 版本列表页初始化");
}

void MainWindow::initTaskListPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::TaskListPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("任务列表页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::TaskListPage));
    m_taskListPage = new TaskListPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::TaskListPage), m_taskListPage);
    delete placeholder;
    connect(m_taskListPage, &TaskListPage::backToMainRequested, this, &MainWindow::onTaskListPageBack);
    connect(m_taskListPage, &TaskListPage::taskDetailRequested, this, &MainWindow::onTaskDetailRequested);
    m_pageInitialized[PageIndex::TaskListPage] = true;
    PerformanceMonitor::instance()->endMeasurement("任务列表页初始化");
}

void MainWindow::initTaskDetailPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::TaskDetailPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("任务详情页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::TaskDetailPage));
    m_taskDetailPage = new TaskDetailPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::TaskDetailPage), m_taskDetailPage);
    delete placeholder;
    connect(m_taskDetailPage, &TaskDetailPage::backToTaskListRequested, this, &MainWindow::onTaskDetailPageBack);
    connect(m_taskDetailPage, &TaskDetailPage::taskCancelled, this, &MainWindow::onTaskCancelled);
    m_pageInitialized[PageIndex::TaskDetailPage] = true;
    PerformanceMonitor::instance()->endMeasurement("任务详情页初始化");
}


void MainWindow::initModDetailPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ModDetailPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("模组详情页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ModDetailPage));
    m_modDetailPage = new ModDetailPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ModDetailPage), m_modDetailPage);
    delete placeholder;
    connect(m_modDetailPage, &ModDetailPage::backToListRequested, this, [this]() {
        showModDownloadPage();
    });
    connect(m_modDetailPage, &ModDetailPage::downloadRequested,
            this, &MainWindow::onModDownloadRequested);
    // 左侧「一键安装」：安装与当前实例兼容的最新正式版，并自动补齐必备前置模组
    connect(m_modDetailPage, &ModDetailPage::installLatestRequested,
            this, [this](const ModInfo &modInfo) {
        const QString gameVersion = m_modDetailPage ? m_modDetailPage->instanceGameVersion() : QString();
        const QString loader = m_modDetailPage ? m_modDetailPage->instanceLoaderType() : QString();
        onCardDownloadRequested(modInfo, ContentType::Mod, 0, gameVersion, loader);
    });
    // Connect detail signals from APIs
    connect(m_modDownloadPage->curseforgeAPI(), &CurseForgeAPI::modDetailReceived,
            m_modDetailPage, &ModDetailPage::receiveDetail);
    connect(m_modDownloadPage->curseforgeAPI(), &CurseForgeAPI::modDetailFailed,
            this, [this](const QString &error) {
        if (m_modDetailPage) m_modDetailPage->hideLoading();
        NotificationManager::showError(m_modDetailPage, tr("获取模组详情失败: ") + error);
    });
    connect(m_modDownloadPage->modrinthAPI(), &ModrinthAPI::modDetailReceived,
            m_modDetailPage, &ModDetailPage::receiveDetail);
    connect(m_modDownloadPage->modrinthAPI(), &ModrinthAPI::modDetailFailed,
            this, [this](const QString &error) {
        if (m_modDetailPage) m_modDetailPage->hideLoading();
        NotificationManager::showError(m_modDetailPage, tr("获取模组详情失败: ") + error);
    });
    // 跨源合并卡片：顶部切换来源后按新源重新拉取详情（切换后 m_modInfo 已换到目标源）
    connect(m_modDetailPage, &ModDetailPage::sourceSwitchRequested,
            this, [this](const QString &source) {
        if (!m_modDetailPage)
            return;
        const ModInfo info = m_modDetailPage->currentModInfo();
        if (info.id.isEmpty() || info.source != source) {
            m_modDetailPage->hideLoading();
            return;
        }
        if (source == QStringLiteral("curseforge"))
            m_modDownloadPage->curseforgeAPI()->fetchModDetail(info.id);
        else if (source == QStringLiteral("modrinth"))
            m_modDownloadPage->modrinthAPI()->fetchModDetail(info.id);
        else
            m_modDetailPage->hideLoading();
    });
    // Connect MC百科 URL resolution to detail page
    connect(m_modDownloadPage, &ModDownloadPage::modMcmodUrlResolved,
            m_modDetailPage, [this](const QString & /*lookupKey*/, const QString &url) {
        // Check if the resolved mod matches the currently displayed one
        // The lookupKey format is "source:id"
        if (m_modDetailPage && !url.isEmpty()) {
            m_modDetailPage->setMcmodUrl(url);
        }
    });
    // 问问AI：跳转 AI 助手并引用当前网络模组
    connect(m_modDetailPage, &ModDetailPage::askAiRequested,
            this, &MainWindow::openAiChatWithReference);
    // 点击「前置模组」里的条目：直接打开该前置资源自己的详情页（沿用当前实例筛选）
    connect(m_modDetailPage, &ModDetailPage::contentClicked,
            this, [this](const ModInfo &depInfo) {
        if (!m_modDetailPage || depInfo.id.isEmpty())
            return;
        showModDetailPage(depInfo, m_modDetailPage->instanceGameVersion(),
                          m_modDetailPage->instanceLoaderType());
    });
    m_pageInitialized[PageIndex::ModDetailPage] = true;
    PerformanceMonitor::instance()->endMeasurement("模组详情页初始化");
}

void MainWindow::showModDetailPage(const ModInfo &modInfo, const QString &filterGameVersion,
                                   const QString &filterLoader)
{
    // ModDetailPage 初始化依赖 m_modDownloadPage，必须先初始化
    ensurePageInitialized(PageIndex::ModDownloadPage);
    ensurePageInitialized(PageIndex::ModDetailPage);
    setSideBarVisible(false);
    QString title = tr("资源>模组");
    if (!modInfo.chineseName.isEmpty())
        title += QString(">%1").arg(modInfo.chineseName);
    else
        title += QString(">%1").arg(modInfo.name);
    m_topBar->setTitle(title);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, [this]() {
        showModDownloadPage();
    });
    if (m_modDetailPage) {
        m_modDetailPage->setModInfo(modInfo);
        // 列表页选中的实例版本/加载器带入详情页左侧筛选
        if (!filterGameVersion.isEmpty() || !filterLoader.isEmpty())
            m_modDetailPage->setInstanceFilter(filterGameVersion, filterLoader);
        // Pass already-resolved MC百科 URL if available
        QString lookupKey = modInfo.source + ":" + modInfo.id;
        QString mcmodUrl = m_modDownloadPage->mcmodUrlForMod(lookupKey);
        if (!mcmodUrl.isEmpty())
            m_modDetailPage->setMcmodUrl(mcmodUrl);
    }
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ModDetailPage));
    // Fetch detailed info (screenshots, dependencies, etc.)
    if (modInfo.source == "curseforge" && !modInfo.id.isEmpty()) {
        m_modDownloadPage->curseforgeAPI()->fetchModDetail(modInfo.id);
    } else if (modInfo.source == "modrinth" && !modInfo.id.isEmpty()) {
        m_modDownloadPage->modrinthAPI()->fetchModDetail(modInfo.id);
    } else {
        // 无可用来源/ID，直接隐藏加载层
        if (m_modDetailPage)
            m_modDetailPage->hideLoading();
    }
}

void MainWindow::showLocalModDetailPage(const ModInfo &modInfo)
{
    ensurePageInitialized(PageIndex::LocalModDetailPageIndex);
    setSideBarVisible(false);
    QString title = tr("模组详情");
    if (!modInfo.chineseName.isEmpty())
        title = modInfo.chineseName;
    else if (!modInfo.englishName.isEmpty())
        title = modInfo.englishName;
    else
        title = modInfo.name;
    m_topBar->setTitle(title);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, [this]() {
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstanceManagePage));
        // 返回"实例管理>模组"：恢复侧边栏同时恢复实例管理子导航
        restoreSideBarWithSubNav(4, 10);
        m_topBar->setTitle(tr("实例管理"));
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    });
    if (m_localModDetailPage) {
        m_localModDetailPage->setModInfo(modInfo);
    }
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::LocalModDetailPageIndex));
}

void MainWindow::openAiChatWithReference(const ResourceReference &ref)
{
    ensurePageInitialized(PageIndex::AiChatPage);
    setSideBarVisible(false);
    m_topBar->setTitle(tr("AI 助手"));
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    if (m_aiChatPage) {
        m_aiChatPage->addExternalReference(ref);
    }
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::AiChatPage));
}

void MainWindow::showModDownloadPage()
{
    ensurePageInitialized(PageIndex::ModDownloadPage);
    // 恢复"资源>模组"的子导航：从模组详情返回时子导航此前会整条消失
    restoreSideBarWithSubNav(1, 3);
    m_topBar->setTitle(tr("资源>模组"));
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    if (m_modDownloadPage) {
        m_modDownloadPage->setCurrentInstancePath(m_currentInstancePath);
        m_modDownloadPage->loadInstances();
        // 选中当前实例后重新拉取列表，保证版本/加载器筛选立即生效
        m_modDownloadPage->loadInitialMods();
    }
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ModDownloadPage));
}

void MainWindow::showInstanceSelectPage()
{
    ensurePageInitialized(PageIndex::InstanceSelectPage);
    m_subNavPanel->hidePanel();
    m_sideBar->clearSelection();
    setSideBarVisible(false);
    m_topBar->setInstanceSelectTitle();
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstanceSelectPage));
}

void MainWindow::showAccountManagePage()
{
    ensurePageInitialized(PageIndex::AccountManagePage);
    setSideBarVisible(false);
    m_topBar->setAccountManageTitle();
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::AccountManagePage));
}

void MainWindow::openForumAccountLogin()
{
    showAccountManagePage();
    if (m_forumAccountPage)
        m_forumAccountPage->openLoginForm();
}

void MainWindow::showLaunchDetailsPage()
{
    ensurePageInitialized(PageIndex::LaunchDetailsPage);
    int currentIdx = m_stackedWidget->currentIndex();
    m_previousPage = static_cast<PageIndex>(currentIdx);
    m_hasPreviousPage = true;
    setSideBarVisible(false);
    m_topBar->setLaunchDetailsTitle();
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::LaunchDetailsPage));
    if (m_taskBar && m_taskBar->launchTaskCard()) {
        m_launchDetailsPage->setDetails(m_taskBar->launchTaskCard()->details());
    }
    // 根据当前游戏状态更新详情页状态
    auto status = GameLauncher::instance()->status();
    switch (status) {
    case GameLauncher::Idle:
        m_launchDetailsPage->setLaunchStatus("启动日志", false);
        break;
    case GameLauncher::Launching:
        m_launchDetailsPage->setLaunchStatus("正在启动...", true);
        break;
    case GameLauncher::Running:
        m_launchDetailsPage->setLaunchStatus("游戏运行中", true);
        break;
    case GameLauncher::Failed:
        m_launchDetailsPage->setLaunchStatus("启动失败", false);
        break;
    case GameLauncher::Stopped:
        m_launchDetailsPage->setLaunchStatus("游戏已停止", false);
        break;
    }
}

void MainWindow::showTaskListPage()
{
    ensurePageInitialized(PageIndex::TaskListPage);
    setSideBarVisible(false);
    m_topBar->setTaskListTitle();
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onTaskListPageBack);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::TaskListPage));
}

void MainWindow::showTaskDetailPage(const QString &taskId)
{
    ensurePageInitialized(PageIndex::TaskDetailPage);
    setSideBarVisible(false);
    m_topBar->setTitle(tr("任务列表>下载详情"));
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onTaskDetailPageBack);
    if (m_taskDetailPage) {
        m_taskDetailPage->setTaskId(taskId);
    }
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::TaskDetailPage));
}

void MainWindow::initModpackImportPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ModpackImportPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("整合包导入页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ModpackImportPage));
    m_modpackImportPage = new ModpackImportPage(this);
    connect(m_modpackImportPage, &ModpackImportPage::backRequested, this, &MainWindow::onBackToMain);
    connect(m_modpackImportPage, &ModpackImportPage::modpackInstalled, this, [this]()
    {
      // Refresh instance list if it's already initialized
      if (m_instanceSelectPage)
        m_instanceSelectPage->loadInstances();
    });
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ModpackImportPage), m_modpackImportPage);
    delete placeholder;
    m_pageInitialized[PageIndex::ModpackImportPage] = true;
    PerformanceMonitor::instance()->endMeasurement("整合包导入页初始化");
}

void MainWindow::showModpackImportPage()
{
    ensurePageInitialized(PageIndex::ModpackImportPage);
    // 恢复"资源>导入整合包"的子导航（从实例页等入口进入时子导航此前会缺失）
    restoreSideBarWithSubNav(1, 2);
    m_topBar->setTitle(tr("资源>导入整合包"));
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ModpackImportPage));
}

void MainWindow::initSearchPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::SearchPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("搜索页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::SearchPage));
    m_searchPage = new SearchPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::SearchPage), m_searchPage);
    delete placeholder;

    connect(m_searchPage, &SearchPage::navigateToResourceDetail, this, [this](const ModInfo &info, ContentType contentType) {
        showContentDetailPage(info, contentType);
    });
    // 搜索页里的社区帖子直接在启动器内打开帖子详情；返回键回到搜索页（搜索状态保留）
    connect(m_searchPage, &SearchPage::navigateToForumThread, this, [this](int threadId) {
        m_forumReturnAction = [this]() { showSearchPage(); };
        showForumThread(threadId);
    });

    m_pageInitialized[PageIndex::SearchPage] = true;
    PerformanceMonitor::instance()->endMeasurement("搜索页初始化");
}

void MainWindow::showSearchPage()
{
    ensurePageInitialized(PageIndex::SearchPage);
    // 搜索页不属于主导航项，需隐藏子导航面板并清除主导航选中状态，
    // 避免从资源页/设置页/实例管理页进入时子导航栏滞留、主导航选中残留
    m_subNavPanel->hidePanel();
    m_sideBar->clearSelection();
    setSideBarVisible(false);
    m_topBar->setTitle(tr("搜索"));
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);

    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::SearchPage));
    m_searchPage->setSearchFocus();
}

void MainWindow::initModpackExportPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ModpackExportPageIndex]) { return; }
    PerformanceMonitor::instance()->startMeasurement("整合包导出页初始化");
    QWidget* placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ModpackExportPageIndex));
    m_modpackExportPage = new ModpackExportPage(this);
    connect(m_modpackExportPage, &ModpackExportPage::backRequested, this, &MainWindow::onBackToMain);
    connect(m_modpackExportPage, &ModpackExportPage::exportFinished, this, [this]()
    {
      if (m_instanceSelectPage)
      {
        m_instanceSelectPage->loadInstances();
        m_instanceSelectPage->updateInstanceList();
      }
    });
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ModpackExportPageIndex), m_modpackExportPage);
    delete placeholder;
    m_pageInitialized[PageIndex::ModpackExportPageIndex] = true;
    PerformanceMonitor::instance()->endMeasurement("整合包导出页初始化");
}

void MainWindow::showModpackExportPage(const QString& instancePath)
{
    ensurePageInitialized(PageIndex::ModpackExportPageIndex);
    // 恢复"实例管理>导出整合包"的子导航
    restoreSideBarWithSubNav(4, 3);
    m_topBar->setTitle(tr("实例>导出整合包"));
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ModpackExportPageIndex));
    if (!instancePath.isEmpty() && m_modpackExportPage)
    {
        m_modpackExportPage->setInstancePath(instancePath);
    }
}

void MainWindow::initAiChatPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::AiChatPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("AI 聊天页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::AiChatPage));
    m_aiChatPage = new AiChatPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::AiChatPage), m_aiChatPage);
    delete placeholder;
    // 注入当前实例上下文，启用主侧边栏 AI 页的资源引用功能
    m_aiChatPage->setInstanceContext(m_currentInstancePath,
                                     m_currentInstanceVersion,
                                     m_currentInstanceLoader);
    m_pageInitialized[PageIndex::AiChatPage] = true;
    PerformanceMonitor::instance()->endMeasurement("AI 聊天页初始化");
}

void MainWindow::initJavaDownloadPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::JavaDownloadPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Java 下载页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::JavaDownloadPage));
    m_javaDownloadPage = new JavaDownloadPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::JavaDownloadPage), m_javaDownloadPage);
    delete placeholder;
    connect(m_javaDownloadPage, &JavaDownloadPage::backToSettingsRequested, this, [this]()
    {
        ensurePageInitialized(PageIndex::SettingsPage);
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::SettingsPage));
        // 返回"设置>Java管理"：恢复设置的子导航
        restoreSideBarWithSubNav(2, 4);
        m_topBar->setTitle(tr("设置"));
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    });
    m_pageInitialized[PageIndex::JavaDownloadPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Java 下载页初始化");
}

void MainWindow::showJavaDownloadPage()
{
    ensurePageInitialized(PageIndex::JavaDownloadPage);
    setSideBarVisible(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, [this]()
    {
        ensurePageInitialized(PageIndex::SettingsPage);
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::SettingsPage));
        // 返回"设置>Java管理"：恢复设置的子导航
        restoreSideBarWithSubNav(2, 4);
        m_topBar->setTitle(tr("设置"));
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
        m_topBar->setTitle(tr("设置>Java管理"));
    });
    m_topBar->setTitle(tr("Java 下载"));
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::JavaDownloadPage));
}

void MainWindow::initSkinEditorPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::SkinEditorPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("皮肤编辑器页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::SkinEditorPage));
    m_skinEditorPage = new SkinEditorPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::SkinEditorPage), m_skinEditorPage);
    delete placeholder;
    connect(m_skinEditorPage, &SkinEditorPage::backToMainRequested, this, &MainWindow::onBackToMain);
    // 应用皮肤到账户后，刷新账户管理页的皮肤显示（若账户页已初始化）
    if (m_forumAccountPage)
    {
        connect(m_skinEditorPage, &SkinEditorPage::skinAppliedToAccount,
                m_forumAccountPage, &ForumAccountPage::refreshAccount,
                Qt::UniqueConnection);
    }
    m_pageInitialized[PageIndex::SkinEditorPage] = true;
    PerformanceMonitor::instance()->endMeasurement("皮肤编辑器页初始化");
}

void MainWindow::showSkinEditorPage()
{
    bool firstShow = !isPageInitialized(PageIndex::SkinEditorPage);
    ensurePageInitialized(PageIndex::SkinEditorPage);
    setSideBarVisible(false);
    m_topBar->setTitle(tr("皮肤制作"));
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::SkinEditorPage));
    if (firstShow)
    {
        m_skinEditorPage->initializeDefault();
    }
    // 目标账户：从角色页 / 账户详情进来时带上那个账户
    const QString targetName = m_pendingSkinEditorAccount;
    const QString targetType = m_pendingSkinEditorAccountType;
    m_pendingSkinEditorAccount.clear();
    m_pendingSkinEditorAccountType.clear();
    if (!targetName.isEmpty())
        m_skinEditorPage->applyToAccount(targetName, targetType);
}

void MainWindow::initPluginPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::PluginPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("插件页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::PluginPage));
    m_pluginPage = new PluginPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::PluginPage), m_pluginPage);
    delete placeholder;

    // 插件页操作 → 主窗口处理
    connect(m_pluginPage, &PluginPage::importPluginRequested,
            this, &MainWindow::onImportPluginRequested);
    connect(m_pluginPage, &PluginPage::createPluginRequested,
            this, &MainWindow::onCreatePluginRequested);
    connect(m_pluginPage, &PluginPage::removePluginRequested,
            this, &MainWindow::onRemovePluginRequested);
    connect(m_pluginPage, &PluginPage::openPluginsDirRequested,
            this, &MainWindow::onOpenPluginsDirRequested);

    // 插件内服务器列表：使用当前实例一键进服（复用首页快速进服链路）
    connect(m_pluginPage, &PluginPage::serverJoinRequested,
            this, &MainWindow::onQuickLaunchServerClicked);

    m_pageInitialized[PageIndex::PluginPage] = true;
    PerformanceMonitor::instance()->endMeasurement("插件页初始化");
}

void MainWindow::initNewsPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::NewsPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("资讯页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::NewsPage));
    m_newsPage = new NewsPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::NewsPage), m_newsPage);
    delete placeholder;
    m_pageInitialized[PageIndex::NewsPage] = true;
    PerformanceMonitor::instance()->endMeasurement("资讯页初始化");
}

void MainWindow::showNewsPage()
{
    ensurePageInitialized(PageIndex::NewsPage);
    setSideBarVisible(false);
    m_topBar->setTitle(tr("官方新闻"));
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::NewsPage));
}

void MainWindow::initSettingsPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::SettingsPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("设置页初始化");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::SettingsPage));
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    auto *settingsPage = new SettingsPage(this);
    // 设置页按当前版本模式裁剪（基岩版模式下隐藏 Java 专属设置项）
    settingsPage->setBedrockMode(FavoritesManager::instance()->isBedrockMode());
    scrollArea->setWidget(settingsPage);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::SettingsPage), scrollArea);
    delete placeholder;
    connect(settingsPage, &SettingsPage::downloadJavaRequested, this, &MainWindow::showJavaDownloadPage);
    // 关于页点了「意见反馈」：跳到软件内反馈页
    connect(settingsPage, &SettingsPage::openFeedbackRequested, this, &MainWindow::showFeedbackPage);
    // 通用设置里点了「打开新手引导」：跳到新手引导页（独立整页，不是弹窗）
    connect(settingsPage, &SettingsPage::openOnboardingRequested, this,
            [this]() { showOnboardingPage(false); });
    // 界面设置里改了首页轮播图：立即刷新首页轮播（无需重启）
    connect(settingsPage, &SettingsPage::carouselImagesChanged, this, [this]() {
        if (m_homePage) {
            m_homePage->reloadCarouselImages();
        }
    });
    m_pageInitialized[PageIndex::SettingsPage] = true;
    PerformanceMonitor::instance()->endMeasurement("设置页初始化");
}

void MainWindow::initCommunityPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::CommunityPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("社区页初始化");

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::CommunityPage));
    m_communityPage = new CommunityForumPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::CommunityPage), m_communityPage);
    delete placeholder;

    // ── 论坛深度集成：社区页所有点击都在启动器内消化，不再跳浏览器 ──
    connect(m_communityPage, &CommunityForumPage::threadOpenRequested, this, [this](int threadId) {
        m_forumReturnAction = [this]() { returnToCommunityPage(); };
        showForumThread(threadId);
    });
    connect(m_communityPage, &CommunityForumPage::boardOpenRequested, this,
            [this](const QString &slug, const QString &boardName) {
                m_forumReturnAction = [this]() { returnToCommunityPage(); };
                showForumBoard(slug, boardName);
            });
    connect(m_communityPage, &CommunityForumPage::tagOpenRequested, this, [this](const QString &tag) {
        m_forumReturnAction = [this]() { returnToCommunityPage(); };
        showForumTag(tag);
    });
    connect(m_communityPage, &CommunityForumPage::composeRequested, this, [this]() {
        m_forumReturnAction = [this]() { returnToCommunityPage(); };
        showForumComposer();
    });
    connect(m_communityPage, &CommunityForumPage::friendsOpenRequested, this, [this]() {
        m_forumReturnAction = [this]() { returnToCommunityPage(); };
        showForumFriends();
    });
    connect(m_communityPage, &CommunityForumPage::notificationsRequested, this, [this]() {
        m_forumReturnAction = [this]() { returnToCommunityPage(); };
        showAccountManagePage();
        if (m_forumAccountPage)
            m_forumAccountPage->openNotificationsTab();
    });
    connect(m_communityPage, &CommunityForumPage::loginRequested, this, [this]() {
        m_forumReturnAction = [this]() { returnToCommunityPage(); };
        showAccountManagePage();
        if (m_forumAccountPage)
            m_forumAccountPage->openLoginForm();
    });
    // 社区页内的「联机大厅」子页面：加入 / 开房 / 登录都交给主窗口
    connect(m_communityPage, &CommunityForumPage::joinGameRequested,
            this, &MainWindow::onLobbyJoinRequested);
    connect(m_communityPage, &CommunityForumPage::openInstanceAssistantRequested,
            this, &MainWindow::openInstanceAssistant);

    m_pageInitialized[PageIndex::CommunityPage] = true;
    PerformanceMonitor::instance()->endMeasurement("社区页初始化");
}

// ────────────────────────────────────────────────────────────── 论坛深度集成
// 帖子详情 / 主题列表（板块·搜索·标签）/ 发帖 / 用户主页 四个内层页，
// 统一走「隐藏侧边栏 + 顶栏返回键」的详情页模式，返回动作由进入入口决定。

void MainWindow::initForumThreadDetailPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ForumThreadDetailPage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ForumThreadDetailPage));
    m_forumThreadDetailPage = new ForumThreadDetailPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ForumThreadDetailPage),
                                  m_forumThreadDetailPage);
    delete placeholder;

    connect(m_forumThreadDetailPage, &ForumThreadDetailPage::boardOpenRequested, this,
            [this](const QString &slug, const QString &boardName) {
                m_forumReturnAction = [this]() { presentForumThreadDetail(); };
                showForumBoard(slug, boardName);
            });
    connect(m_forumThreadDetailPage, &ForumThreadDetailPage::tagOpenRequested, this,
            [this](const QString &tag) {
                m_forumReturnAction = [this]() { presentForumThreadDetail(); };
                showForumTag(tag);
            });
    connect(m_forumThreadDetailPage, &ForumThreadDetailPage::userOpenRequested, this,
            [this](int userId) {
                m_forumReturnAction = [this]() { presentForumThreadDetail(); };
                showForumUserProfile(userId);
            });
    connect(m_forumThreadDetailPage, &ForumThreadDetailPage::loginRequested, this, [this]() {
        m_forumReturnAction = [this]() { presentForumThreadDetail(); };
        showAccountManagePage();
        if (m_forumAccountPage)
            m_forumAccountPage->openLoginForm();
    });
    connect(m_forumThreadDetailPage, &ForumThreadDetailPage::threadDeleted, this, [this]() {
        // 主题没了，回社区页并刷新
        returnToCommunityPage();
    });
    connect(m_forumThreadDetailPage, &ForumThreadDetailPage::pluginInstalled, this, [this]() {
        // 帖子里装好了插件，侧边栏「插件」分组跟着亮出来
        refreshPluginNav();
    });
    connect(m_forumThreadDetailPage, &ForumThreadDetailPage::backRequested, this, [this]() {
        returnToCommunityPage();
    });
    connect(m_forumThreadDetailPage, &ForumThreadDetailPage::reportRequested, this,
            [this](int postId, int floor, const QString &authorName, const QString &excerpt) {
                // 举报走整页：返回落点是当前帖子
                m_forumReturnAction = [this]() { presentForumThreadDetail(); };
                showForumReportPage(postId, floor, authorName, excerpt);
            });
    connect(m_forumThreadDetailPage, &ForumThreadDetailPage::threadOpenRequested, this,
            [this](int threadId) {
                // 正文里点了别的帖子：把当前帖子记成返回落点，返回键效果同网页版的浏览器后退
                const int previous = m_forumThreadDetailPage ? m_forumThreadDetailPage->threadId() : 0;
                if (previous > 0 && previous != threadId)
                    m_forumReturnAction = [this, previous]() { showForumThread(previous); };
                showForumThread(threadId);
            });

    m_pageInitialized[PageIndex::ForumThreadDetailPage] = true;
}

void MainWindow::initForumThreadListPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ForumThreadListPage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ForumThreadListPage));
    m_forumThreadListPage = new ForumThreadListPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ForumThreadListPage),
                                  m_forumThreadListPage);
    delete placeholder;

    connect(m_forumThreadListPage, &ForumThreadListPage::threadOpenRequested, this,
            [this](int threadId) {
                m_forumReturnAction = [this]() { presentForumThreadList(); };
                showForumThread(threadId);
            });
    connect(m_forumThreadListPage, &ForumThreadListPage::composeRequested, this,
            [this](const QString &boardSlug) {
                m_forumReturnAction = [this]() { presentForumThreadList(); };
                showForumComposer(boardSlug);
            });
    connect(m_forumThreadListPage, &ForumThreadListPage::boardOpenRequested, this,
            [this](const QString &slug, const QString &boardName) {
                m_forumReturnAction = [this]() { presentForumThreadList(); };
                showForumBoard(slug, boardName);
            });

    m_pageInitialized[PageIndex::ForumThreadListPage] = true;
}

void MainWindow::initForumComposerPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ForumComposerPage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ForumComposerPage));
    m_forumComposerPage = new ForumComposerPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ForumComposerPage),
                                  m_forumComposerPage);
    delete placeholder;

    connect(m_forumComposerPage, &ForumComposerPage::threadCreated, this, [this](int threadId) {
        // 发完直接看新帖；从新帖返回时回社区页
        m_forumReturnAction = [this]() { returnToCommunityPage(); };
        showForumThread(threadId);
    });
    connect(m_forumComposerPage, &ForumComposerPage::loginRequested, this, [this]() {
        // 记住进发帖页时的返回落点：登录完回到发帖页后，再点返回还能回到原来的入口
        m_pendingComposerReturn = m_forumReturnAction;
        m_forumReturnAction = [this]() {
            presentForumComposer();
            m_forumReturnAction = m_pendingComposerReturn;
            m_pendingComposerReturn = nullptr;
        };
        showAccountManagePage();
        if (m_forumAccountPage)
            m_forumAccountPage->openLoginForm();
    });

    m_pageInitialized[PageIndex::ForumComposerPage] = true;
}

void MainWindow::initForumUserProfilePage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ForumUserProfilePage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ForumUserProfilePage));
    m_forumUserProfilePage = new ForumUserProfilePage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ForumUserProfilePage),
                                  m_forumUserProfilePage);
    delete placeholder;

    connect(m_forumUserProfilePage, &ForumUserProfilePage::threadOpenRequested, this,
            [this](int threadId) {
                m_forumReturnAction = [this]() { presentForumUserProfile(); };
                showForumThread(threadId);
            });
    connect(m_forumUserProfilePage, &ForumUserProfilePage::chatOpenRequested, this,
            [this](int userId) {
                m_forumReturnAction = [this]() { presentForumUserProfile(); };
                showForumFriends(userId);
            });
    connect(m_forumUserProfilePage, &ForumUserProfilePage::friendsOpenRequested, this, [this]() {
        m_forumReturnAction = [this]() { presentForumUserProfile(); };
        showForumFriends();
    });

    m_pageInitialized[PageIndex::ForumUserProfilePage] = true;
}

void MainWindow::initFeedbackPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::FeedbackPage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::FeedbackPage));
    m_feedbackPage = new FeedbackPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::FeedbackPage),
                                  m_feedbackPage);
    delete placeholder;

    // 点了「提交新反馈」：跳到提交新反馈页，返回落点是反馈列表
    connect(m_feedbackPage, &FeedbackPage::composeRequested, this,
            &MainWindow::showFeedbackComposePage);

    m_pageInitialized[PageIndex::FeedbackPage] = true;
}

void MainWindow::initFeedbackComposePage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::FeedbackComposePage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::FeedbackComposePage));
    m_feedbackComposePage = new FeedbackComposePage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::FeedbackComposePage),
                                  m_feedbackComposePage);
    delete placeholder;

    // 未登录点了「去登录」：先记下返回动作再跳账户页弹登录表单
    connect(m_feedbackComposePage, &FeedbackComposePage::loginRequested, this, [this]() {
        m_forumReturnAction = [this]() { presentFeedbackComposePage(); };
        showAccountManagePage();
        if (m_forumAccountPage)
            m_forumAccountPage->openLoginForm();
    });
    // 提交成功：回到反馈列表页刷新
    connect(m_feedbackComposePage, &FeedbackComposePage::submitted, this,
            &MainWindow::showFeedbackPage);

    m_pageInitialized[PageIndex::FeedbackComposePage] = true;
}

void MainWindow::initForumReportPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ForumReportPage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ForumReportPage));
    m_forumReportPage = new ForumReportPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ForumReportPage),
                                  m_forumReportPage);
    delete placeholder;

    // 未登录点了「去登录」：先记下返回动作再跳账户页弹登录表单
    connect(m_forumReportPage, &ForumReportPage::loginRequested, this, [this]() {
        m_forumReturnAction = [this]() { presentForumReportPage(); };
        showAccountManagePage();
        if (m_forumAccountPage)
            m_forumAccountPage->openLoginForm();
    });
    // 提交成功：回到帖子详情页
    connect(m_forumReportPage, &ForumReportPage::submitted, this, [this]() {
        presentForumThreadDetail();
    });

    m_pageInitialized[PageIndex::ForumReportPage] = true;
}

void MainWindow::initForumFriendsPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ForumFriendsPage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ForumFriendsPage));
    m_forumFriendsPage = new ForumFriendsPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ForumFriendsPage),
                                  m_forumFriendsPage);
    delete placeholder;

    connect(m_forumFriendsPage, &ForumFriendsPage::loginRequested, this, [this]() {
        m_forumReturnAction = [this]() { presentForumFriends(); };
        showAccountManagePage();
        if (m_forumAccountPage)
            m_forumAccountPage->openLoginForm();
    });
    connect(m_forumFriendsPage, &ForumFriendsPage::userOpenRequested, this, [this](int userId) {
        m_forumReturnAction = [this]() { presentForumFriends(); };
        showForumUserProfile(userId);
    });

    m_pageInitialized[PageIndex::ForumFriendsPage] = true;
}

void MainWindow::initForumPluginListPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ForumPluginListPage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ForumPluginListPage));
    m_forumPluginListPage = new ForumPluginListPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ForumPluginListPage),
                                  m_forumPluginListPage);
    delete placeholder;

    // 点插件卡跳到原帖（返回键回到插件列表）
    connect(m_forumPluginListPage, &ForumPluginListPage::threadOpenRequested, this,
            [this](int threadId) {
                m_forumReturnAction = [this]() { showForumPluginListPage(); };
                showForumThread(threadId);
            });
    // 安装成功刷新侧边栏插件分组（与导入插件的刷新一致）
    connect(m_forumPluginListPage, &ForumPluginListPage::pluginInstalled, this, [this]() {
        refreshPluginNav();
    });
    // 未登录拉列表 401：跳账户页登录
    connect(m_forumPluginListPage, &ForumPluginListPage::loginRequested, this, [this]() {
        m_forumReturnAction = [this]() { showForumPluginListPage(); };
        showAccountManagePage();
        if (m_forumAccountPage)
            m_forumAccountPage->openLoginForm();
    });

    m_pageInitialized[PageIndex::ForumPluginListPage] = true;
}

void MainWindow::showForumPluginListPage()
{
    ensurePageInitialized(PageIndex::ForumPluginListPage);
    if (m_sideBar)
        m_sideBar->setSelectedIndex(1);
    m_topBar->setTitle(tr("资源>插件"));
    animatedSwitchToPage(PageIndex::ForumPluginListPage);
    if (m_forumPluginListPage)
        m_forumPluginListPage->openPage();
}

// ── 发布中心：入口 → 资源类型 / 帖子板块 → 平台跳转 / 发帖详情 ──────────────

void MainWindow::initPublishPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::PublishPage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::PublishPage));
    m_publishPage = new PublishPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::PublishPage), m_publishPage);
    delete placeholder;

    connect(m_publishPage, &PublishPage::resourcesRequested,
            this, &MainWindow::showPublishResourcePage);
    connect(m_publishPage, &PublishPage::postsRequested,
            this, &MainWindow::showPublishBoardPage);

    m_pageInitialized[PageIndex::PublishPage] = true;
}

void MainWindow::initPublishResourcePage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::PublishResourcePage]) { return; }

    QWidget *placeholder =
        m_stackedWidget->widget(static_cast<int>(PageIndex::PublishResourcePage));
    m_publishResourcePage = new PublishResourcePage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::PublishResourcePage),
                                  m_publishResourcePage);
    delete placeholder;

    connect(m_publishResourcePage, &PublishResourcePage::kindSelected, this,
            [this](PublishKit::ResourceKind kind) {
                if (kind == PublishKit::ResourceKind::Plugin)
                    showPublishPluginCompose();
                else
                    showPublishPlatformPage(kind);
            });

    m_pageInitialized[PageIndex::PublishResourcePage] = true;
}

void MainWindow::initPublishPlatformPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::PublishPlatformPage]) { return; }

    QWidget *placeholder =
        m_stackedWidget->widget(static_cast<int>(PageIndex::PublishPlatformPage));
    m_publishPlatformPage = new PublishPlatformPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::PublishPlatformPage),
                                  m_publishPlatformPage);
    delete placeholder;

    connect(m_publishPlatformPage, &PublishPlatformPage::jumpRequested, this,
            [this](const QString &url) {
                if (!QDesktopServices::openUrl(QUrl(url)))
                {
                    NotificationManager::showInfo(
                        this, tr("没能打开浏览器，请手动访问：%1").arg(url));
                    return;
                }
                NotificationManager::showInfo(
                    this, tr("已在浏览器打开平台发布页，发布完成后回到方块盒子即可"));
            });

    m_pageInitialized[PageIndex::PublishPlatformPage] = true;
}

void MainWindow::initPublishBoardPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::PublishBoardPage]) { return; }

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::PublishBoardPage));
    m_publishBoardPage = new PublishBoardPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::PublishBoardPage),
                                  m_publishBoardPage);
    delete placeholder;

    connect(m_publishBoardPage, &PublishBoardPage::boardSelected, this,
            [this](const QString &slug, const QString &boardName) {
                Q_UNUSED(boardName);
                showPublishPostCompose(slug);
            });

    m_pageInitialized[PageIndex::PublishBoardPage] = true;
}

void MainWindow::showPublishPage()
{
    ensurePageInitialized(PageIndex::PublishPage);

    setSideBarVisible(true);
    if (m_sideBar)
    {
        m_sideBar->setSelectedIndex(PublishNavIndex);
        m_sideBar->setExpandButtonVisible(false);
    }
    if (m_subNavPanel)
        m_subNavPanel->hidePanel();
    m_lastSidebarIndex = PublishNavIndex;

    m_topBar->setTitle(tr("发布"));
    m_topBar->setAccountCardVisible(true);
    m_topBar->setWindowControlCardVisible(true);
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);

    animatedSwitchToPage(PageIndex::PublishPage);
}

void MainWindow::showPublishResourcePage()
{
    ensurePageInitialized(PageIndex::PublishResourcePage);

    setSideBarVisible(true);
    if (m_sideBar)
    {
        m_sideBar->setSelectedIndex(PublishNavIndex);
        m_sideBar->setExpandButtonVisible(false);
    }
    if (m_subNavPanel)
        m_subNavPanel->hidePanel();
    m_lastSidebarIndex = PublishNavIndex;

    m_topBar->setTitle(tr("发布>资源"));
    m_topBar->setAccountCardVisible(true);
    m_topBar->setWindowControlCardVisible(true);
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::showPublishPage);

    animatedSwitchToPage(PageIndex::PublishResourcePage);
}

void MainWindow::showPublishPlatformPage(PublishKit::ResourceKind kind)
{
    ensurePageInitialized(PageIndex::PublishPlatformPage);
    if (m_publishPlatformPage)
        m_publishPlatformPage->setKind(kind);

    setSideBarVisible(true);
    if (m_sideBar)
    {
        m_sideBar->setSelectedIndex(PublishNavIndex);
        m_sideBar->setExpandButtonVisible(false);
    }
    if (m_subNavPanel)
        m_subNavPanel->hidePanel();
    m_lastSidebarIndex = PublishNavIndex;

    m_topBar->setTitle(tr("发布>%1").arg(PublishKit::kindName(kind)));
    m_topBar->setAccountCardVisible(true);
    m_topBar->setWindowControlCardVisible(true);
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::showPublishResourcePage);

    animatedSwitchToPage(PageIndex::PublishPlatformPage);
}

void MainWindow::showPublishBoardPage()
{
    ensurePageInitialized(PageIndex::PublishBoardPage);

    setSideBarVisible(true);
    if (m_sideBar)
    {
        m_sideBar->setSelectedIndex(PublishNavIndex);
        m_sideBar->setExpandButtonVisible(false);
    }
    if (m_subNavPanel)
        m_subNavPanel->hidePanel();
    m_lastSidebarIndex = PublishNavIndex;

    m_topBar->setTitle(tr("发布>帖子"));
    m_topBar->setAccountCardVisible(true);
    m_topBar->setWindowControlCardVisible(true);
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::showPublishPage);

    animatedSwitchToPage(PageIndex::PublishBoardPage);
    if (m_publishBoardPage)
        m_publishBoardPage->open();
}

void MainWindow::showPublishPluginCompose()
{
    // 资源→插件：发布详情页切到「资源」模式（分类只有插件），返回落点是发布资源页
    m_forumReturnAction = [this]() { showPublishResourcePage(); };
    m_forumComposerTopTitle = tr("发布>插件");
    ensurePageInitialized(PageIndex::ForumComposerPage);
    presentForumComposer();
    if (m_forumComposerPage)
        m_forumComposerPage->startNewResource(QStringLiteral("plugin"));
}

void MainWindow::showPublishPostCompose(const QString &boardSlug)
{
    // 帖子发布详情页就是发布详情页的「帖子」模式，返回落点是发布帖子（板块选择）页
    m_forumReturnAction = [this]() { showPublishBoardPage(); };
    showForumComposer(boardSlug, tr("发布>帖子"));
}


void MainWindow::showForumThread(int threadId)
{
    ensurePageInitialized(PageIndex::ForumThreadDetailPage);
    presentForumThreadDetail();
    if (m_forumThreadDetailPage)
        m_forumThreadDetailPage->openThread(threadId);
}

void MainWindow::showForumBoard(const QString &slug, const QString &boardName)
{
    ensurePageInitialized(PageIndex::ForumThreadListPage);
    presentForumThreadList();
    if (m_forumThreadListPage)
        m_forumThreadListPage->openBoard(slug, boardName);
}

void MainWindow::showForumSearch(const QString &query)
{
    ensurePageInitialized(PageIndex::ForumThreadListPage);
    presentForumThreadList();
    if (m_forumThreadListPage)
        m_forumThreadListPage->openSearch(query);
}

void MainWindow::showForumTag(const QString &tag)
{
    ensurePageInitialized(PageIndex::ForumThreadListPage);
    presentForumThreadList();
    if (m_forumThreadListPage)
        m_forumThreadListPage->openTag(tag);
}

void MainWindow::showForumComposer(const QString &preselectSlug, const QString &topTitle)
{
    // 记住本页顶栏标题：发布中心进来时显示「发布>插件 / 发布>帖子」，
    // 登录完回到发帖页也不会掉回默认标题
    m_forumComposerTopTitle = topTitle;
    ensurePageInitialized(PageIndex::ForumComposerPage);
    presentForumComposer();
    if (m_forumComposerPage)
        m_forumComposerPage->startNewThread(preselectSlug);
}

void MainWindow::showFeedbackPage()
{
    ensurePageInitialized(PageIndex::FeedbackPage);
    // 反馈页现在是左侧导航一级页：顶栏返回键直接回主页
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    presentFeedbackPage();
    if (m_feedbackPage)
        m_feedbackPage->open();
}

void MainWindow::showFeedbackComposePage()
{
    ensurePageInitialized(PageIndex::FeedbackComposePage);
    // 提交新反馈页的返回落点是反馈列表页
    m_forumReturnAction = [this]() { returnToFeedbackList(); };
    presentFeedbackComposePage();
    if (m_feedbackComposePage)
        m_feedbackComposePage->open();
}

void MainWindow::showForumFriends(int userId)
{
    ensurePageInitialized(PageIndex::ForumFriendsPage);
    presentForumFriends();
    if (m_forumFriendsPage)
        m_forumFriendsPage->openChat(userId);
}

void MainWindow::showForumUserProfile(int userId)
{
    ensurePageInitialized(PageIndex::ForumUserProfilePage);
    presentForumUserProfile();
    if (m_forumUserProfilePage)
        m_forumUserProfilePage->openUser(userId);
}

void MainWindow::presentForumThreadDetail()
{
    setSideBarVisible(false);
    m_topBar->setTitle(tr("社区 > 帖子"));
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onForumPageBack);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ForumThreadDetailPage));
}

void MainWindow::presentForumThreadList()
{
    setSideBarVisible(false);
    m_topBar->setTitle(tr("社区 > 主题列表"));
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onForumPageBack);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ForumThreadListPage));
}

void MainWindow::presentForumComposer()
{
    setSideBarVisible(false);
    m_topBar->setTitle(m_forumComposerTopTitle.isEmpty() ? tr("社区 > 发帖")
                                                        : m_forumComposerTopTitle);
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onForumPageBack);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ForumComposerPage));
}

void MainWindow::presentForumUserProfile()
{
    setSideBarVisible(false);
    m_topBar->setTitle(tr("社区 > 用户主页"));
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onForumPageBack);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ForumUserProfilePage));
}

void MainWindow::presentFeedbackPage()
{
    m_sideBar->setSelectedIndex(FeedbackNavIndex);
    m_sideBar->setExpandButtonVisible(false);
    m_subNavPanel->hidePanel();
    m_topBar->setTitle(tr("反馈"));
    m_topBar->setAccountCardVisible(true);
    m_topBar->setWindowControlCardVisible(true);
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::FeedbackPage));
}

void MainWindow::presentFeedbackComposePage()
{
    setSideBarVisible(false);
    m_topBar->setTitle(tr("反馈 > 提交新反馈"));
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onForumPageBack);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::FeedbackComposePage));
}

void MainWindow::showForumReportPage(int postId, int floor, const QString &authorName,
                                     const QString &excerpt)
{
    ensurePageInitialized(PageIndex::ForumReportPage);
    presentForumReportPage();
    if (m_forumReportPage)
        m_forumReportPage->open(postId, floor, authorName, excerpt);
}

void MainWindow::presentForumReportPage()
{
    setSideBarVisible(false);
    m_topBar->setTitle(tr("社区 > 举报"));
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onForumPageBack);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ForumReportPage));
}

void MainWindow::presentForumFriends()
{
    setSideBarVisible(false);
    m_topBar->setTitle(tr("社区 > 好友与私信"));
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onForumPageBack);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ForumFriendsPage));
}

void MainWindow::onForumPageBack()
{
    // 返回动作由进入时的入口决定（社区页 / 列表页 / 账户页 / 论坛内层页之间互转）
    if (m_forumReturnAction)
    {
        const auto action = m_forumReturnAction;
        m_forumReturnAction = nullptr;
        action();
        return;
    }
    onBackToMain();
}

void MainWindow::returnToSettingsAbout()
{
    // 与点击侧边栏「设置 > 关于」保持一致：恢复侧边栏选中、子导航定位与顶栏标题
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);

    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setAccountSelected(false);
    m_topBar->setSettingsTitle();

    m_sideBar->setSelectedIndex(2);
    m_subNavPanel->showForParent(2, false);
    ensurePageInitialized(PageIndex::SettingsPage);
    animatedSwitchToPageDirectional(PageIndex::SettingsPage, m_lastSidebarIndex, 2);
    m_lastSidebarIndex = 2;
    onChildNavClicked(2, 9);
    m_subNavPanel->setSelectedChild(9);
}

void MainWindow::returnToFeedbackList()
{
    // 从提交新反馈页回到反馈列表页：恢复侧边栏选中与顶栏
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    presentFeedbackPage();
    if (m_feedbackPage)
        m_feedbackPage->open();
}

void MainWindow::returnToCommunityPage()
{
    ensurePageInitialized(PageIndex::CommunityPage);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);

    m_topBar->setTitle(tr("社区"));
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setAccountCardVisible(true);
    m_topBar->setWindowControlCardVisible(true);

    setSideBarVisible(true);
    if (m_sideBar)
        m_sideBar->setSelectedIndex(6);
    if (m_subNavPanel)
        m_subNavPanel->hidePanel();
    m_lastSidebarIndex = 6;

    animatedSwitchToPage(PageIndex::CommunityPage);
    if (m_communityPage)
        m_communityPage->refresh();
}

/* ============================================================
 * 新手引导页（独立整页：首次启动自动进入，设置里也可再次打开）
 * ============================================================ */
void MainWindow::initOnboardingPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::OnboardingPage])
        return;

    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::OnboardingPage));
    m_onboardingPage = new OnboardingWizard(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::OnboardingPage), m_onboardingPage);
    delete placeholder;

    // 页面自己不发退出：由主窗口决定回到首页还是设置页（以及是否重启）
    connect(m_onboardingPage, &OnboardingWizard::finished, this, &MainWindow::onOnboardingFinished);
    connect(m_onboardingPage, &OnboardingWizard::backRequested, this,
            &MainWindow::onOnboardingBackRequested);
    connect(m_onboardingPage, &OnboardingWizard::restartRequested, this,
            &MainWindow::onOnboardingRestartRequested);

    m_pageInitialized[PageIndex::OnboardingPage] = true;
}

void MainWindow::showOnboardingPage(bool firstRun)
{
    ensurePageInitialized(PageIndex::OnboardingPage);
    if (!m_onboardingPage)
        return;

    m_onboardingFirstRun = firstRun;

    // 整页配置流程：隐藏侧边栏与子导航，顶栏只留标题（窗口控制按钮保留）
    m_subNavPanel->hidePanel();
    m_sideBar->clearSelection();
    setSideBarVisible(false);
    m_topBar->setTitle(tr("新手引导"));
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setAccountCardVisible(false);
    m_topBar->setWindowControlCardVisible(true);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onOnboardingBackRequested);

    // 每次进入都从第一步开始（上一次可能停在完成页）
    m_onboardingPage->resetToFirstStep();
    // 首次启动没有「设置」这个来路：完成页只给「开始使用方块盒子」
    m_onboardingPage->setReturnToSettingsAvailable(!firstRun);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::OnboardingPage));
}

void MainWindow::onOnboardingFinished()
{
    const bool firstRun = m_onboardingFirstRun;
    m_onboardingFirstRun = false;

    // 顶栏返回按钮交回「回主页」（进入引导时换成了引导页自己的返回逻辑）
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);

    // 引导里可能新添加了实例文件夹，刷新首页「最近游玩」卡片
    refreshHomeRecentPlays();
    onBackToMain();

    // 首次启动：通知主程序做引导之后的动作（入站答题提醒 / 检查更新）
    if (firstRun)
        emit onboardingFinished(true);
}

void MainWindow::onOnboardingBackRequested()
{
    const bool firstRun = m_onboardingFirstRun;
    m_onboardingFirstRun = false;

    refreshHomeRecentPlays();

    if (firstRun) {
        // 首次启动没有「设置」这个来路，返回即回首页；
        // onboarding/completed 未置位，下次启动会再次进入引导。
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
        onBackToMain();
        // 引导之外的启动后续动作（入站答题提醒 / 检查更新）照常进行
        emit onboardingFinished(true);
        return;
    }

    // 从设置里打开的：恢复侧边栏与顶栏后回到设置页（与点击侧边栏「设置」一致）
    m_topBar->setAccountCardVisible(true);
    m_topBar->setWindowControlCardVisible(true);
    setSideBarVisible(true);
    m_sideBar->setSelectedIndex(2);
    onSideBarItemClicked(2);
}

void MainWindow::onOnboardingRestartRequested()
{
    // 语言切换：启动新进程并退出当前进程。新进程启动时 loadLanguageSync() 干净加载所选语言，
    // 且因 onboarding 未完成会再次进入引导。
    hide();
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList());
    QCoreApplication::quit();
}
