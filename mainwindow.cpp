/**
 * @file   mainwindow.cpp
 * @brief  涓荤獥鍙ｇ被瀹炵幇
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QCursor>
#include "components/AppMessageBox.h"
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QTimer>
#include <QFrame>
#include <QSplitter>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>

#include "components/BackgroundWidget.h"
#include "components/NotificationManager.h"
#include "components/SideBar.h"
#include "components/SubNavPanel.h"
#include "components/TaskBar.h"
#include "components/InstanceAssistantWindow.h"
#include "components/GameFloatingIcon.h"
#include "components/TopBar.h"
#include "utils/BackgroundManager.h"
#include "utils/BingWallpaperManager.h"
#include "utils/ThemeManager.h"
#include "pages/AccountManagePage.h"
#include "pages/ForgeVersionListPage.h"
#include "pages/HomePage.h"
#include "pages/InstallInstancePage.h"
#include "pages/InstanceSelectPage.h"
#include "pages/LaunchDetailsPage.h"
#include "pages/LoaderDetailPage.h"
#include "pages/ResourcesPage.h"
#include "pages/SettingsPage.h"
#include "pages/TaskDetailPage.h"
#include "pages/ModDownloadPage.h"
#include "pages/ModpackImportPage.h"
#include "pages/ModpackExportPage.h"
#include "pages/TaskListPage.h"
#include "pages/ContentDownloadPage.h"
#include "pages/ContentDetailPage.h"
#include "pages/ContentListPage.h"
#include "pages/InstanceManagePage.h"
#include "pages/InstanceLogPage.h"
#include "pages/SearchPage.h"
#include "pages/AiChatPage.h"
#include "pages/FavoritesPage.h"
#include "pages/SkinEditorPage.h"
#include "pages/PluginPage.h"
#include "components/CreatePluginDialog.h"
#include "utils/plugin/PluginManager.h"
#include "utils/plugin/PluginSafetyGuard.h"
#include "components/FavoriteFolderDialog.h"
#include "utils/GameLauncher.h"
#include "utils/LanguageManager.h"
#include "utils/PerformanceMonitor.h"
#include "platform.h"
#include "utils/SettingsManager.h"
#include "utils/SkinDownloader.h"
#include "utils/download/DownloadEngine.h"
#include "utils/content/ContentDownloader.h"
#include "utils/ClipboardMonitor.h"
#include "utils/PageTransitionAnimator.h"
#include "utils/ContentAnimator.h"

#include <QPainter>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_instanceSelectPage(nullptr)
    , m_instanceAssistantWindow(nullptr)
    , m_accountManagePage(nullptr)
    , m_launchDetailsPage(nullptr)
    , m_installInstancePage(nullptr)
    , m_loaderDetailPage(nullptr)
    , m_forgeVersionListPage(nullptr)
    , m_taskListPage(nullptr)
    , m_taskDetailPage(nullptr)
    , m_modDownloadPage(nullptr)
    , m_modDetailPage(nullptr)
    , m_localModDetailPage(nullptr)
    , m_modpackImportPage(nullptr)
    , m_modpackExportPage(nullptr)
    , m_searchPage(nullptr)
    , m_contentDownloadPage(nullptr)
    , m_contentDetailPage(nullptr)
    , m_contentListPage(nullptr)
    , m_resourcesPage(nullptr)
    , m_instanceManagePage(nullptr)
    , m_aiChatPage(nullptr)
    , m_javaDownloadPage(nullptr)
    , m_skinEditorPage(nullptr)
    , m_clipboardMonitor(nullptr)
    , m_backgroundWidget(nullptr)
    , m_gameLauncher(nullptr)
    , m_pageAnimator(nullptr)
    , m_contentAnimator(nullptr)
    , m_previousPage(PageIndex::HomePage)
    , m_hasPreviousPage(false)
    , m_lastSidebarIndex(0)
    , m_lastChildSidebarIndex{-1, -1, -1, -1, -1, -1, -1, -1}
{
    ui->setupUi(this);
    
    // 初始化页面初始化状态跟踪
    m_pageInitialized[PageIndex::InstanceSelectPage] = false;
    m_pageInitialized[PageIndex::AccountManagePage] = false;
    m_pageInitialized[PageIndex::LaunchDetailsPage] = false;
    m_pageInitialized[PageIndex::InstallInstancePage] = false;
    m_pageInitialized[PageIndex::LoaderDetailPage] = false;
    m_pageInitialized[PageIndex::ForgeVersionListPage] = false;
    m_pageInitialized[PageIndex::TaskListPage] = false;
    m_pageInitialized[PageIndex::TaskDetailPage] = false;
    m_pageInitialized[PageIndex::ModDownloadPage] = false;
    m_pageInitialized[PageIndex::ModDetailPage] = false;
    m_pageInitialized[PageIndex::ModpackImportPage] = false;
    m_pageInitialized[PageIndex::SearchPage] = false;
    m_pageInitialized[PageIndex::ContentDownloadPage] = false;
    m_pageInitialized[PageIndex::ContentDetailPage] = false;
    m_pageInitialized[PageIndex::ContentListPage] = false;
    m_pageInitialized[PageIndex::InstanceManagePage] = false;
    m_pageInitialized[PageIndex::ModpackExportPageIndex] = false;
    m_pageInitialized[PageIndex::AiChatPage] = false;
    m_pageInitialized[PageIndex::SkinEditorPage] = false;

    initUI();
    initPages();
    initGameLauncher();

    // Set initial page title
    m_topBar->setMainTitle();
    
    // Connect window control signals from top bar
    connect(m_topBar, &TopBar::minimizeWindowRequested, this, &MainWindow::showMinimized);
    connect(m_topBar, &TopBar::maximizeWindowRequested, this, &MainWindow::toggleMaximized);
    connect(m_topBar, &TopBar::closeWindowRequested, this, &MainWindow::close);
    // Connect back button signal
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onTopBarBackClicked);
    // Connect scroll to top and refresh signals
    connect(m_topBar, &TopBar::scrollToTopClicked, this, &MainWindow::onScrollToTop);
    connect(m_topBar, &TopBar::refreshClicked, this, &MainWindow::onRefreshClicked);
    // Connect instance select signal
    connect(m_topBar, &TopBar::instanceSelectClicked, this, &MainWindow::onInstanceSelectClicked);
    // Connect instance settings signal
    connect(m_topBar, &TopBar::instanceSettingsClicked, this, &MainWindow::onInstanceSettingsClicked);
    // Connect instance assistant signal
    connect(m_topBar, &TopBar::instanceAssistantClicked, this, [this]() {
        if (!m_instanceAssistantWindow) {
            m_instanceAssistantWindow = new InstanceAssistantWindow(this);
            // 转发资源管理页的存档快捷启动信号到主窗口处理
            connect(m_instanceAssistantWindow, &InstanceAssistantWindow::quickLaunchSaveRequested,
                    this, &MainWindow::onQuickLaunchSaveClicked);
        }
        // 注入当前实例上下文（路径、版本、loader）
        m_instanceAssistantWindow->setInstanceContext(
            m_currentInstancePath,
            m_currentInstanceVersion,
            m_currentInstanceLoader);
        m_instanceAssistantWindow->show();
        m_instanceAssistantWindow->raise();
        m_instanceAssistantWindow->activateWindow();
    });
    
    // Connect account manage signal
    connect(m_topBar, &TopBar::accountManageClicked, this, &MainWindow::onAccountManageClicked);
    connect(m_topBar, &TopBar::launchGameClicked, this, qOverload<>(&MainWindow::onLaunchGameClicked));
    connect(m_topBar, &TopBar::searchClicked, this, &MainWindow::onSearchClicked);
    // Connect game edition switch (Java版 / 基岩版)
    connect(m_topBar, &TopBar::editionSwitched, this, [this](bool javaEdition) {
        m_topBar->setJavaEdition(javaEdition);
        NotificationManager::showInfo(this, javaEdition ? tr("已切换到 Java 版") : tr("已切换到基岩版"));
    });

    connect(m_taskBar, &TaskBar::showTaskListPageRequested, this, &MainWindow::onShowTaskListPageRequested);
    connect(m_taskBar, &TaskBar::taskDetailRequested, this, &MainWindow::onTaskDetailRequested);

    connect(LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        ui->retranslateUi(this);
        m_topBar->setMainTitle();
    });

    connect(BackgroundManager::instance(), &BackgroundManager::backgroundChanged, this, [this]() {
        updateBackgroundWidget();
        ThemeManager::instance()->applyThemeColor();
    });

    updateBackgroundWidget();

    // 自动登录：从保存的账户文件中恢复默认账户
    restoreDefaultAccount();

    // 加载保存的实例路径
    loadCurrentInstancePath();

    // 初始化剪贴板监听
    m_clipboardMonitor = new ClipboardMonitor(this);
    connect(m_clipboardMonitor, &ClipboardMonitor::linkDetected,
            this, &MainWindow::onClipboardLinkDetected);

// 初始化快捷键
    reloadShortcuts();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::initUI()
{
    // 开始测量UI初始化时间
    PerformanceMonitor::instance()->startMeasurement("UI Initialization");

    // Set window properties
    setWindowTitle(tr(""));
    setMinimumSize(1200, 800);
    
    // Remove system title bar and use custom title bar
    setWindowFlags(Qt::FramelessWindowHint);

    // Create central widget and main layout
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Create top bar
    m_topBar = new TopBar(this);
    mainLayout->addWidget(m_topBar);

    // 顶部标题栏与内容区之间的分割线
    QFrame *topBarSeparator = new QFrame();
    topBarSeparator->setObjectName("topBarSeparator");
    topBarSeparator->setFrameShape(QFrame::HLine);
    topBarSeparator->setFrameShadow(QFrame::Plain);
    mainLayout->addWidget(topBarSeparator);

    // Create content area layout (sidebar + main content)
    QWidget *contentArea = new QWidget();
    QHBoxLayout *contentLayout = new QHBoxLayout(contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // Create sidebar
    m_sideBar = new SideBar(this);
    m_sideBar->setFixedWidth(76);
    contentLayout->addWidget(m_sideBar);

    // 子导航面板：悬浮气泡，不占用布局空间，贴靠侧边栏绘制
    m_subNavPanel = new SubNavPanel(contentArea);
    m_subNavPanel->setSideBar(m_sideBar);
    m_subNavPanel->setVisible(false);
    contentArea->installEventFilter(this);
    m_mainContentArea = contentArea;

    // Vertical separator between sidebar and content area
    m_sideSeparator = new QFrame();
    m_sideSeparator->setObjectName("sideSeparator");
    m_sideSeparator->setFrameShape(QFrame::VLine);
    m_sideSeparator->setFrameShadow(QFrame::Plain);
    contentLayout->addWidget(m_sideSeparator);

    // Create a wrapper to center the stacked widget with a max width
    QWidget *contentWrapper = new QWidget();
    contentWrapper->setObjectName("contentWrapper");
    contentWrapper->installEventFilter(this);
    QHBoxLayout *wrapperLayout = new QHBoxLayout(contentWrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    m_wrapperLayout = wrapperLayout;

    // Background widget behind the pages (not in layout, positioned manually)
    m_backgroundWidget = new BackgroundWidget(contentWrapper);
    m_backgroundWidget->lower();

    // Create stacked widget for pages
    m_stackedWidget = new QStackedWidget(this);
    wrapperLayout->addWidget(m_stackedWidget, 1);

    // Initialize page transition animator
    m_pageAnimator = new PageTransitionAnimator(m_stackedWidget, this);
    m_pageAnimator->setTransitionType(PageTransitionAnimator::FadeSlideUp);
    m_pageAnimator->setDuration(350);

    // Initialize content entrance animator
    m_contentAnimator = new ContentAnimator(this);

    // After transition, animate content entrance
    connect(m_pageAnimator, &PageTransitionAnimator::transitionFinished, this, [this]()
    {
        QWidget *current = m_stackedWidget->currentWidget();
        if (current)
        {
            animateContentEntrance(current);
            // 页面切换动画结束后，强制刷新含 Skin3DWidget 的页面的 3D 皮肤控件
            // 原因：PageTransitionAnimator 的 FadeSlideUp 动画期间会给页面应用 QGraphicsOpacityEffect，
            // 该 effect 会破坏其子 QOpenGLWidget (Skin3DWidget) 的 GL 渲染。
            // 动画结束后 effect 被移除，但 Skin3DWidget 不会自动重绘，需手动触发。
            if (current == m_accountManagePage && m_accountManagePage)
            {
                m_accountManagePage->refreshSkin3DWidget();
            }
            if (current == m_skinEditorPage && m_skinEditorPage)
            {
                m_skinEditorPage->refreshSkin3DPreview();
            }
        }
    });

    contentLayout->addWidget(contentWrapper, 1);

    // Add content area to main layout
    mainLayout->addWidget(contentArea, 1);

    // Create task bar at the bottom of the window (hidden by default)
    m_taskBar = new TaskBar(this);
    mainLayout->addWidget(m_taskBar);

#ifdef Q_OS_ANDROID
    // 安卓版：游戏内悬浮的方块盒子图标，点击弹出/收起实例助手侧栏
    m_gameFloatingIcon = new GameFloatingIcon(this);
    const bool floatingIconDark = (ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme);
    m_gameFloatingIcon->setIconPixmap(QPixmap(floatingIconDark
        ? QStringLiteral(":/Images/blockbox_icon_dark.png")
        : QStringLiteral(":/Images/logo.png")));
    m_gameFloatingIcon->hide();
    connect(m_gameFloatingIcon, &GameFloatingIcon::assistantToggleRequested,
            this, &MainWindow::toggleGameAssistantOverlay);
#endif

    // Set central widget
    setCentralWidget(centralWidget);

    // Connect signals and slots
    connect(m_sideBar, &SideBar::parentItemClicked, this, &MainWindow::onParentNavClicked);
    connect(m_sideBar, &SideBar::childItemClicked, this, &MainWindow::onChildNavClicked);
    connect(m_subNavPanel, &SubNavPanel::childItemClicked, this, &MainWindow::onChildNavClicked);

    // 气泡展开时主内容区左侧留出空隙，收起时恢复
    connect(m_subNavPanel, &SubNavPanel::panelVisibilityChanged, this, [this](bool visible)
    {
        if (!m_wrapperLayout) return;
        const int gap = visible ? SubNavPanel::kWidth + SubNavPanel::kMargin : 0;
        m_wrapperLayout->setContentsMargins(gap, 0, 0, 0);
    });

    // 收藏夹分组变化时刷新侧边栏
    connect(FavoritesManager::instance(), &FavoritesManager::foldersChanged,
            this, &MainWindow::onFavoritesChanged);

    // 结束测量UI初始化时间
    PerformanceMonitor::instance()->endMeasurement("UI Initialization");
}

void MainWindow::animatedSwitchToPage(PageIndex index)
{
    if (m_pageAnimator)
    {
        m_pageAnimator->animateToIndex(static_cast<int>(index));
        // Reset to default after transition
        QTimer::singleShot(400, this, [this]()
        {
            if (m_pageAnimator)
                m_pageAnimator->setTransitionType(PageTransitionAnimator::FadeSlideUp);
        });
    }
    else
    {
        m_stackedWidget->setCurrentIndex(static_cast<int>(index));
    }
}

void MainWindow::animatedSwitchToPageDirectional(PageIndex index, int fromSidebarIndex, int toSidebarIndex)
{
    if (!m_pageAnimator)
    {
        m_stackedWidget->setCurrentIndex(static_cast<int>(index));
        return;
    }

    if (fromSidebarIndex >= 0 && qAbs(toSidebarIndex - fromSidebarIndex) <= 3)
    {
        if (toSidebarIndex > fromSidebarIndex)
            m_pageAnimator->setTransitionType(PageTransitionAnimator::SlideUp);
        else if (toSidebarIndex < fromSidebarIndex)
            m_pageAnimator->setTransitionType(PageTransitionAnimator::SlideDown);
        else
            m_pageAnimator->setTransitionType(PageTransitionAnimator::FadeSlideUp);
    }
    else
    {
        m_pageAnimator->setTransitionType(PageTransitionAnimator::FadeSlideUp);
    }

    m_pageAnimator->animateToIndex(static_cast<int>(index));
}

void MainWindow::animateContentEntrance(QWidget *page)
{
    if (!page || !m_contentAnimator)
        return;

    // SearchPage dynamically manages its own result cards; skip entrance animation
    // to avoid applying opacity:0 to existing search results.
    if (page == m_searchPage)
        return;

    // 只对顶层容器（QGroupBox 和命名包含 Group/Card/Section 的 QFrame）做入场动画。
    // 避免对每个按钮/标签单独设置 QGraphicsOpacityEffect，原因有二：
    //   1. targets 过多时，顺序动画会让靠后的控件长时间保持 opacity=0，表现为"未显示"。
    //   2. QGraphicsOpacityEffect 会破坏 QSS 基于 objectName 的子控件样式渲染。
    QVector<QWidget*> targets;
    for (auto *child : page->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (!child->isWidgetType() || child->isHidden())
            continue;

        if (qobject_cast<QGroupBox*>(child))
        {
            targets.append(child);
            continue;
        }

        if (child->inherits("QFrame"))
        {
            QString objName = child->objectName();
            if (objName.contains("Card") || objName.contains("Section") ||
                objName.contains("Group"))
            {
                targets.append(child);
            }
        }
    }

    if (!targets.isEmpty())
    {
        m_contentAnimator->animateStaggered(targets, 60, 300);
    }
}

void MainWindow::setSideBarVisible(bool visible)
{
    m_sideBar->setVisible(visible);
    m_sideSeparator->setVisible(visible);
    if (!visible)
    {
        m_subNavPanel->hidePanel();
    }
}

void MainWindow::onSideBarItemClicked(int parentIndex)
{
    // Reset back button to main
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    
    // Deselect all top bar options
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setAccountSelected(false);
    
    if (parentIndex == 0) { // HomePage
        animatedSwitchToPageDirectional(PageIndex::HomePage, m_lastSidebarIndex, parentIndex);
        m_topBar->setMainTitle();
    } else if (parentIndex == 1) { // Resources
        m_topBar->setResourcesTitle();
        m_subNavPanel->showForParent(1);
        ensurePageInitialized(PageIndex::InstallInstancePage);
        onChildNavClicked(1, 0);
    } else if (parentIndex == 2) { // Settings
        m_topBar->setSettingsTitle();
        m_subNavPanel->showForParent(2);
        animatedSwitchToPageDirectional(PageIndex::SettingsPage, m_lastSidebarIndex, parentIndex);
        onChildNavClicked(2, 0);
    }

    m_lastSidebarIndex = parentIndex;
}

void MainWindow::onParentNavClicked(int parentIndex)
{
    // Reset back button to main
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    
    if (parentIndex == 0) { // HomePage
        m_subNavPanel->hidePanel();
        animatedSwitchToPageDirectional(PageIndex::HomePage, m_lastSidebarIndex, parentIndex);
        m_topBar->setMainTitle();
        m_topBar->setAccountCardVisible(true);
        m_topBar->setWindowControlCardVisible(true);
    } else if (parentIndex == 1) { // Resources
        // 进入资源主页（ResourcesPage），该页不对应任何子项，故不默认选中子导航
        m_subNavPanel->showForParent(1, false);
        m_topBar->setResourcesTitle();
        if (m_resourcesPage) {
            m_resourcesPage->setInstancePath(m_currentInstancePath);
        }
        animatedSwitchToPageDirectional(PageIndex::ResourcesPage, m_lastSidebarIndex, parentIndex);
    } else if (parentIndex == 2) { // Settings
        m_subNavPanel->showForParent(2);
        m_topBar->setSettingsTitle();
        animatedSwitchToPageDirectional(PageIndex::SettingsPage, m_lastSidebarIndex, parentIndex);
        // Trigger default child navigation
        onChildNavClicked(2, 0);
    } else if (parentIndex == 3) { // AI 助手
        m_subNavPanel->hidePanel();
        ensurePageInitialized(PageIndex::AiChatPage);
        animatedSwitchToPageDirectional(PageIndex::AiChatPage, m_lastSidebarIndex, parentIndex);
        m_topBar->setTitle(tr("AI 助手"));
        m_topBar->setAccountCardVisible(true);
        m_topBar->setWindowControlCardVisible(true);
    } else if (parentIndex == 5) { // 插件
        m_subNavPanel->showForParent(5, false);
        m_topBar->setTitle(tr("插件"));
        ensurePageInitialized(PageIndex::PluginPage);
        animatedSwitchToPageDirectional(PageIndex::PluginPage, m_lastSidebarIndex, parentIndex);
        if (m_pluginPage) {
            m_pluginPage->showOverview();
        }
    }

    m_lastSidebarIndex = parentIndex;
}

void MainWindow::onChildNavClicked(int parentIndex, int childIndex)
{
    // Direction-aware transition for same-level child navigation (Resources only)
    if (parentIndex == 1 && m_pageAnimator)
    {
        int &lastChild = m_lastChildSidebarIndex[parentIndex];
        if (lastChild >= 0 && childIndex != lastChild)
        {
            m_pageAnimator->setTransitionType(
                childIndex > lastChild ? PageTransitionAnimator::SlideUp
                                       : PageTransitionAnimator::SlideDown);
        }
        else if (lastChild < 0)
        {
            m_pageAnimator->setTransitionType(PageTransitionAnimator::FadeSlideUp);
        }
        lastChild = childIndex;
    }

    if (parentIndex == 1) { // Resources child
        if (childIndex == 0) {
            ensurePageInitialized(PageIndex::InstallInstancePage);
            setSideBarVisible(true);
            m_topBar->setTitle(tr("资源>安装新实例"));
            animatedSwitchToPage(PageIndex::InstallInstancePage);
        } else if (childIndex == 1) {
            showContentDownloadPage(ContentType::Modpack);
        } else if (childIndex == 2) {
            showModpackImportPage();
        } else if (childIndex == 3) {
            ensurePageInitialized(PageIndex::ModDownloadPage);
            setSideBarVisible(true);
            m_topBar->setTitle(tr("资源>模组"));
            animatedSwitchToPage(PageIndex::ModDownloadPage);
            if (m_modDownloadPage) {
                m_modDownloadPage->setCurrentInstancePath(m_currentInstancePath);
                m_modDownloadPage->loadInstances();
                m_modDownloadPage->loadInitialMods();
            }
        } else if (childIndex == 4) {
            showContentDownloadPage(ContentType::DataPack);
        } else if (childIndex == 5) {
            showContentDownloadPage(ContentType::ResourcePack);
        } else if (childIndex == 6) {
            showContentDownloadPage(ContentType::ShaderPack);
        } else if (childIndex == 7) {
            showContentDownloadPage(ContentType::World);
        } else if (childIndex == 8) {
            // 新建收藏夹
            onCreateFavoriteFolderRequested();
        } else {
            // childIndex >= 9: 收藏夹列表
            QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
            int folderIdx = childIndex - 9;
            if (folderIdx >= 0 && folderIdx < folders.size())
            {
                showFavoritesPage(folders[folderIdx].id);
            }
            else
            {
                // 标题兜底
                QStringList resourceTitles = {
                    tr("资源>安装新实例"), tr("资源>下载整合包"), tr("资源>导入整合包"),
                    tr("资源>模组"), tr("资源>数据包"), tr("资源>资源包"),
                    tr("资源>光影包"), tr("资源>世界"),
                    tr("资源>新建收藏夹")
                };
                if (childIndex >= 0 && childIndex < resourceTitles.size()) {
                    m_topBar->setTitle(resourceTitles[childIndex]);
                }
            }
        }
    } else if (parentIndex == 2) { // Settings child
        QScrollArea *scrollArea = qobject_cast<QScrollArea*>(m_stackedWidget->widget(2));
        if (scrollArea) {
            SettingsPage *settingsPage = qobject_cast<SettingsPage*>(scrollArea->widget());
            if (settingsPage) {
                settingsPage->setCurrentSettingsTab(childIndex);
            }
        }
        switch (childIndex) {
        case 0: m_topBar->setTitle(tr("设置>常规设置")); break;
        case 1: m_topBar->setTitle(tr("设置>界面设置")); break;
        case 2: m_topBar->setTitle(tr("设置>全局游戏设置")); break;
        case 3: m_topBar->setTitle(tr("设置>实例设置")); break;
        case 4: m_topBar->setTitle(tr("设置>Java管理")); break;
        case 5: m_topBar->setTitle(tr("设置>高级设置")); break;
        case 6: m_topBar->setTitle(tr("设置>按键绑定")); break;
        default: m_topBar->setSettingsTitle(); break;
        }
    } else if (parentIndex == 4) { // Instance Manage child
        // "修改"子项已改为直接前往安装新实例页进入修改模式，不再对应本页标签
        if (childIndex == 2) {
            if (m_currentInstancePath.isEmpty()) {
                NotificationManager::showInfo(this, tr("请先在顶部实例卡片中选择一个要修改的实例"));
                return;
            }
            ensurePageInitialized(PageIndex::InstallInstancePage);
            setSideBarVisible(true);
            m_topBar->setTitle(tr("实例管理>修改"));
            disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
            connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
            QString instanceName = QFileInfo(m_currentInstancePath).fileName();
            if (instanceName.isEmpty())
                instanceName = m_currentInstanceVersion;
            if (m_installInstancePage)
                m_installInstancePage->enterModifyMode(m_currentInstancePath, instanceName,
                                                        m_currentInstanceVersion, m_currentInstanceLoader);
            m_subNavPanel->setSelectedChild(2);
            animatedSwitchToPage(PageIndex::InstallInstancePage);
            return;
        }
        QStringList instanceTitles = {
            tr("实例管理>概览"),
            tr("实例管理>设置"),
            tr("实例管理>修改"),
            tr("实例管理>导出整合包"),
            tr("实例管理>文件传输"),
            tr("实例管理>存档"),
            tr("实例管理>服务器"),
            tr("实例管理>投影"),
            tr("实例管理>截图"),
            tr("实例管理>模组"),
            tr("实例管理>资源包"),
            tr("实例管理>光影包"),
            tr("实例管理>日志")
        };
        if (childIndex >= 0 && childIndex < instanceTitles.size()) {
            m_topBar->setTitle(instanceTitles[childIndex]);
        }
        if (m_instanceManagePage) {
            m_instanceManagePage->setCurrentTab(childIndex);
        }
        // 若当前正处于"修改"进入的安装新实例页，点击其他子项应切回实例管理页
        if (m_stackedWidget->currentIndex() != static_cast<int>(PageIndex::InstanceManagePage))
            animatedSwitchToPage(PageIndex::InstanceManagePage);
    } else if (parentIndex == 5) { // Plugin child
        const int pluginCount = PluginManager::instance()->plugins().size();
        ensurePageInitialized(PageIndex::PluginPage);

        // 0=插件列表总览，1..N=插件详情（无插件时 1=暂无插件占位），N+1（无插件时 +2）=制作，再+1=导入
        const int makeIdx = pluginCount == 0 ? 2 : pluginCount + 1;
        const int importIdx = makeIdx + 1;

        if (childIndex == 0) {
            m_topBar->setTitle(tr("插件"));
            if (m_pluginPage) m_pluginPage->showOverview();
        } else if (pluginCount > 0 && childIndex >= 1 && childIndex <= pluginCount) {
            const int pluginIdx = childIndex - 1;
            m_topBar->setTitle(tr("插件>%1").arg(
                PluginManager::instance()->plugins()[pluginIdx].name));
            if (m_pluginPage) m_pluginPage->showPluginDetail(pluginIdx);
        } else if (childIndex == makeIdx) {
            onCreatePluginRequested();
        } else if (childIndex == importIdx) {
            onImportPluginRequested();
        }
    }
}

void MainWindow::onImportPluginRequested()
{
    ensurePageInitialized(PageIndex::PluginPage);
    // 批量导入：支持多选 .BlockBox 文件
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("批量导入插件"), PluginManager::instance()->pluginsDir(),
        tr("BlockBox 插件 (*.BlockBox)"));
    if (files.isEmpty())
        return;

    PluginManager *pm = PluginManager::instance();
    int okCount = 0;
    int refuseCount = 0;
    QStringList errors;
    for (const QString &file : files) {
        // ===== 添加插件前信任确认：读取包内清单 + 扫描脚本，确认后才导入 =====
        const PluginInfo info = pm->inspectPluginFile(file);
        if (!info.loaded) {
            errors << QFileInfo(file).fileName() + QStringLiteral(": ") +
                      (info.loadError.isEmpty()
                           ? tr("无法读取插件清单")
                           : info.loadError);
            continue;
        }

        const QString scriptsContent = pm->collectScriptsContent(file);
        const bool trusted = PluginSafetyGuard::confirmBeforeInstall(
            this, info.name, info.author, info.version, info.permissions,
            PluginSafetyGuard::RiskMedium,
            QString(),   // 顶层清单暂无风险说明字段，风险由扫描/权限综合判定
            scriptsContent);
        if (!trusted) {
            ++refuseCount;
            continue;
        }

        QString error;
        if (pm->importPlugin(file, &error))
            ++okCount;
        else
            errors << QFileInfo(file).fileName() + QStringLiteral(": ") + error;
    }

    if (okCount > 0) {
        refreshPluginNav();
        if (m_pluginPage) m_pluginPage->showOverview();
    }

    QString summary;
    if (okCount > 0)
        summary += tr("成功导入 %1 个插件。").arg(okCount);
    if (refuseCount > 0)
        summary += tr("\n已拒绝 %1 个插件。").arg(refuseCount);
    if (!errors.isEmpty())
        summary += tr("\n失败项:\n%1").arg(errors.join(QStringLiteral("\n")));

    if (okCount == files.size() && refuseCount == 0) {
        AppMessageBox::information(this, tr("导入成功"), summary);
    } else if (okCount > 0 || refuseCount > 0) {
        AppMessageBox::information(this, tr("导入完成"), summary);
    } else {
        AppMessageBox::warning(this, tr("导入完成"), summary);
    }
    m_topBar->setTitle(tr("插件>批量导入"));
}

void MainWindow::onCreatePluginRequested()
{
    ensurePageInitialized(PageIndex::PluginPage);
    CreatePluginDialog dialog(this);
    dialog.exec();

    // 生成模板或打包后，插件列表可能已变化
    refreshPluginNav();
    if (m_pluginPage) m_pluginPage->showOverview();
}

void MainWindow::onRemovePluginRequested(const QString &id)
{
    PluginInfo target;
    const QList<PluginInfo> plugins = PluginManager::instance()->plugins();
    for (const PluginInfo &p : plugins) {
        if (p.id == id) { target = p; break; }
    }
    if (target.filePath.isEmpty())
        return;

    const AppMessageBox::StandardButton choice = AppMessageBox::question(
        this, tr("删除插件"),
        tr("确定要删除插件「%1」吗？\n\n文件将被删除：\n%2").arg(target.name, target.filePath),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (choice != AppMessageBox::Yes)
        return;

    QString error;
    if (!PluginManager::instance()->removePlugin(id, &error)) {
        AppMessageBox::warning(this, tr("删除失败"), error);
        return;
    }

    refreshPluginNav();
    if (m_pluginPage) m_pluginPage->showOverview();
    m_topBar->setTitle(tr("插件"));
}

void MainWindow::onOpenPluginsDirRequested()
{
    QDir dir(PluginManager::instance()->pluginsDir());
    if (!dir.exists())
        dir.mkpath(PluginManager::instance()->pluginsDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(PluginManager::instance()->pluginsDir()));
}

void MainWindow::refreshPluginNav()
{
    // 重建侧边栏导航结构（插件列表子项动态更新）
    m_sideBar->refreshNavStructure();
    // 若插件导航处于展开状态，同步刷新子导航面板
    const QVector<NavItem> &parents = m_sideBar->parentItems();
    if (m_subNavPanel->currentParentIndex() == 5 && parents.size() > 5) {
        m_subNavPanel->showForParent(5, false);
    }
}

void MainWindow::onSettingsNavItemClicked(int index)
{
    // Get the scroll area containing the settings page
    QScrollArea *scrollArea = qobject_cast<QScrollArea*>(m_stackedWidget->widget(2));
    if (scrollArea) {
        // Get the settings page from the scroll area
        SettingsPage *settingsPage = qobject_cast<SettingsPage*>(scrollArea->widget());
        if (settingsPage) {
            // Switch to the selected settings tab
            settingsPage->setCurrentSettingsTab(index);
        }
    }

    // Update top bar title based on settings tab
    switch (index) {
    case 0: m_topBar->setTitle(tr("设置>常规设置")); break;
    case 1: m_topBar->setTitle(tr("设置>界面设置")); break;
    case 2: m_topBar->setTitle(tr("设置>鍏ㄥ眬游戏设置")); break;
    case 3: m_topBar->setTitle(tr("设置>瀹炰緥设置")); break;
    case 4: m_topBar->setTitle(tr("设置>Java绠＄悊")); break;
    case 5: m_topBar->setTitle(tr("设置>高级设置")); break;
    case 6: m_topBar->setTitle(tr("设置>按键绑定")); break;
    default: m_topBar->setSettingsTitle(); break;
    }
}

void MainWindow::onResourcesNavItemClicked(int index)
{
    // Handle resources navigation item clicks
    qDebug() << "[MainWindow]" << "Resources nav item clicked:" << index;
    
    if (index == 0) { // Install new instance
        ensurePageInitialized(PageIndex::InstallInstancePage);
        setSideBarVisible(true);
        m_topBar->setTitle(tr("资源>安装新实例"));
        animatedSwitchToPage(PageIndex::InstallInstancePage);
    } else if (index == 1) { // Download modpack
        showContentDownloadPage(ContentType::Modpack);
    } else if (index == 2) { // Import modpack
        showModpackImportPage();
    } else if (index == 3) { // Mod download page
        ensurePageInitialized(PageIndex::ModDownloadPage);
        setSideBarVisible(true);
        m_topBar->setTitle(tr("资源>模组"));
        animatedSwitchToPage(PageIndex::ModDownloadPage);
        if (m_modDownloadPage) {
            m_modDownloadPage->setCurrentInstancePath(m_currentInstancePath);
            m_modDownloadPage->loadInstances();
            m_modDownloadPage->loadInitialMods();
        }
    } else if (index == 4) { // DataPack
        showContentDownloadPage(ContentType::DataPack);
    } else if (index == 5) { // ResourcePack
        showContentDownloadPage(ContentType::ResourcePack);
    } else if (index == 6) { // ShaderPack
        showContentDownloadPage(ContentType::ShaderPack);
    } else if (index == 7) { // World
        showContentDownloadPage(ContentType::World);
    } else {
        QStringList resourceTitles = {
            tr("资源>安装新实例"),     // 0
            tr("资源>下载整合包"),     // 1
            tr("资源>导入整合包"),     // 2
            tr("资源>模组"),           // 3
            tr("资源>数据包"),         // 4
            tr("资源>资源包"),         // 5
            tr("资源>光影包"),         // 6
            tr("资源>世界"),           // 7
            tr("资源>新建收藏夹"),     // 8
            tr("资源>默认收藏夹"),     // 9
            tr("资源>示例收藏夹")      // 10
        };
        if (index >= 0 && index < resourceTitles.size()) {
            m_topBar->setTitle(resourceTitles[index]);
        }
    }
}

void MainWindow::onBackToMain()
{
    // 放弃修改现有实例流程：回到主页时复位安装/加载器页的修改模式状态
    if (m_installInstancePage)
        m_installInstancePage->exitModifyMode();
    if (m_loaderDetailPage)
        m_loaderDetailPage->exitModifyMode();

    // Hide sub-navigation panel
    m_subNavPanel->hidePanel();
    
    // Select home in sidebar
    m_sideBar->setSelectedIndex(0);
    
    // Show sidebar again
    setSideBarVisible(true);
    
    // Change top bar title back
    m_topBar->setMainTitle();
    
    // Restore account card and window control card visibility
    m_topBar->setAccountCardVisible(true);
    m_topBar->setWindowControlCardVisible(true);
    
    // Deselect all top bar options
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setAccountSelected(false);
    
    // Switch back to homepage
    animatedSwitchToPage(PageIndex::HomePage);
}

void MainWindow::onInstanceSelectClicked()
{
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setInstanceSelectSelected(true);
    showInstanceSelectPage();
}

void MainWindow::onInstanceSettingsClicked()
{
    if (m_currentInstancePath.isEmpty()) {
        NotificationManager::showError(this, tr("请先选择实例"));
        return;
    }

    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);

    m_subNavPanel->showForParent(4, false);
    m_sideBar->clearSelection();
    setSideBarVisible(true);
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(true);
    m_topBar->setTitle(tr("实例管理"));
    ensurePageInitialized(PageIndex::InstanceManagePage);

    if (m_instanceManagePage)
    {
        QString instanceName = m_currentInstancePath.isEmpty()
            ? QString()
            : m_currentInstancePath.split("/").last();
        m_instanceManagePage->setCurrentInstancePath(m_currentInstancePath);
        m_instanceManagePage->setInstanceName(instanceName);
        m_instanceManagePage->setGameVersion(m_currentInstanceVersion);
        m_instanceManagePage->setLoaderInfo(m_currentInstanceLoader);
        m_instanceManagePage->setCurrentTab(0);
    }
    m_subNavPanel->setSelectedChild(0);

    animatedSwitchToPage(PageIndex::InstanceManagePage);
}

void MainWindow::onInstanceSelected(const QString &instancePath)
{
    updateCurrentInstance(instancePath);
}

void MainWindow::onInstanceInstalled(const QString &instancePath)
{
    updateCurrentInstance(instancePath);
    NotificationManager::showSuccess(this, tr("实例安装成功！"));
}

void MainWindow::updateCurrentInstance(const QString &instancePath)
{
    m_currentInstancePath = instancePath;
    QString instanceName = instancePath.split("/").last();

    m_currentInstanceVersion = instanceName;
    m_currentInstanceLoader.clear();

    QFile versionJsonFile(instancePath + "/" + instanceName + ".json");
    if (versionJsonFile.open(QIODevice::ReadOnly)) {
        QByteArray jsonData = versionJsonFile.readAll();
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parseError);
        if (parseError.error == QJsonParseError::NoError) {
            QJsonObject rootObj = jsonDoc.object();
            if (rootObj.contains("inheritsFrom"))
                m_currentInstanceVersion = rootObj["inheritsFrom"].toString();
            else if (rootObj.contains("clientVersion"))
                m_currentInstanceVersion = rootObj["clientVersion"].toString();
            else if (rootObj.contains("id"))
                m_currentInstanceVersion = rootObj["id"].toString();

            // Detect loader from JSON keys and values
            auto detectLoader = [](const QString &text) -> QString {
                QString lower = text.toLower();
                if (lower.contains("fabric")) return "Fabric";
                if (lower.contains("forge")) return "Forge";
                if (lower.contains("quilt")) return "Quilt";
                if (lower.contains("neoforge")) return "NeoForge";
                return QString();
            };
            for (auto it = rootObj.constBegin(); it != rootObj.constEnd(); ++it) {
                m_currentInstanceLoader = detectLoader(it.key());
                if (!m_currentInstanceLoader.isEmpty()) break;
                QJsonValue val = it.value();
                if (val.isString()) {
                    m_currentInstanceLoader = detectLoader(val.toString());
                    if (!m_currentInstanceLoader.isEmpty()) break;
                }
            }
            if (m_currentInstanceLoader.isEmpty())
                m_currentInstanceLoader = detectLoader(rootObj["inheritsFrom"].toString());
            if (m_currentInstanceLoader.isEmpty())
                m_currentInstanceLoader = detectLoader(rootObj["id"].toString());
        }
        versionJsonFile.close();
    }

    // Fallback: detect loader from instance folder name or version
    if (m_currentInstanceLoader.isEmpty()) {
        QString check = instanceName + " " + m_currentInstanceVersion;
        QString lower = check.toLower();
        if (lower.contains("neoforge")) m_currentInstanceLoader = "NeoForge";
        else if (lower.contains("forge")) m_currentInstanceLoader = "Forge";
        else if (lower.contains("fabric")) m_currentInstanceLoader = "Fabric";
        else if (lower.contains("quilt")) m_currentInstanceLoader = "Quilt";
    }

    m_topBar->setInstanceName(instanceName);

    saveCurrentInstancePath();

    if (m_resourcesPage) {
        m_resourcesPage->setInstancePath(m_currentInstancePath);
    }

    // 同步实例上下文到主侧边栏 AI 页（若已初始化），保持资源引用功能可用
    if (m_aiChatPage) {
        m_aiChatPage->setInstanceContext(m_currentInstancePath,
                                         m_currentInstanceVersion,
                                         m_currentInstanceLoader);
    }

    setSideBarVisible(true);

    animatedSwitchToPage(PageIndex::HomePage);

    m_topBar->setMainTitle();
}

QString MainWindow::findFirstVersionInGameRoot(const QString &gameRootPath) const
{
    // 在游戏根目录的 versions/ 下查找第一个有效版本目录
    QDir versionsDir(gameRootPath + "/versions");
    if (!versionsDir.exists())
    {
        return QString();
    }

    QDirIterator it(versionsDir.path(), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
    while (it.hasNext())
    {
        QString versionPath = it.next();
        QString versionName = QFileInfo(versionPath).fileName();
        // 检查是否存在版本 JSON 文件
        if (QFile::exists(versionPath + "/" + versionName + ".json"))
        {
            return versionPath;
        }
    }
    return QString();
}

void MainWindow::onShowDetailsPage()
{
    showLaunchDetailsPage();
}

void MainWindow::onDetailsPageBack()
{
    onTopBarBackClicked();
}

void MainWindow::toggleMaximized()
{
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void MainWindow::onAccountManageClicked()
{
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setAccountSelected(true);
    showAccountManagePage();
}

void MainWindow::onSkinEditorClicked()
{
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setAccountSelected(false);
    showSkinEditorPage();
}

void MainWindow::onSearchClicked()
{
    showSearchPage();
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    // Enable dragging of the window when clicking on the top bar
    if (event->button() == Qt::LeftButton && 
        event->position().y() <= m_topBar->height()) {
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    // Enable dragging of the window when clicking on the top bar
    if (event->buttons() & Qt::LeftButton && 
        event->position().y() <= m_topBar->height()) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void MainWindow::updateBackgroundWidget()
{
    BackgroundManager *bg = BackgroundManager::instance();
    m_backgroundWidget->setMode(static_cast<BackgroundWidget::Mode>(bg->currentMode()));
    m_backgroundWidget->setBlurRadius(bg->blurRadius());
    if (bg->currentMode() == BackgroundManager::SolidColor) {
        m_backgroundWidget->setSolidColor(QColor(bg->solidColor()));
    } else if (bg->currentMode() == BackgroundManager::Image) {
        m_backgroundWidget->setImage(bg->imagePath());
    } else if (bg->currentMode() == BackgroundManager::Bing) {
        // 先显示上次缓存的壁纸（可能为空），再由必应壁纸管理器异步补齐最新图片
        m_backgroundWidget->setImage(bg->bingImagePath());
        BingWallpaperManager::instance()->ensureLoaded();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_backgroundWidget && watched == m_backgroundWidget->parentWidget()
        && event->type() == QEvent::Resize) {
        m_backgroundWidget->setGeometry(m_backgroundWidget->parentWidget()->rect());
    }

    // 主内容区尺寸变化时，同步悬浮子导航气泡的位置与高度
    if (watched == m_mainContentArea && event->type() == QEvent::Resize
        && m_subNavPanel && m_subNavPanel->isVisible()) {
        m_subNavPanel->updateFloatGeometry();
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onAddAccountPageOpened()
{
    // 澶勭悊娣诲姞璐︽埛椤甸潰鎵撳紑鐨勯€昏緫
}

void MainWindow::onAccountManagePageOpened()
{
    // 澶勭悊璐︽埛绠＄悊椤甸潰鎵撳紑鐨勯€昏緫
}

void MainWindow::onTopBarBackClicked()
{
    if (m_hasPreviousPage) {
        PageIndex target = m_previousPage;
        m_hasPreviousPage = false;
        setSideBarVisible(true);
        m_topBar->setMainTitle();
        m_subNavPanel->hidePanel();
        m_sideBar->setSelectedIndex(0);
        m_topBar->setInstanceSelectSelected(false);
        m_topBar->setInstanceSettingsSelected(false);
        m_topBar->setAccountSelected(false);
        animatedSwitchToPage(target);
        return;
    }
    onBackToMain();
}

void MainWindow::animateScrollToTop(QScrollBar *scrollBar)
{
    int currentValue = scrollBar->value();
    if (currentValue == 0)
        return;

    auto *animation = new QPropertyAnimation(scrollBar, "value");
    animation->setDuration(300);
    animation->setStartValue(currentValue);
    animation->setEndValue(0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::onScrollToTop()
{
    QWidget *currentPage = m_stackedWidget->currentWidget();
    if (!currentPage)
        return;

    QScrollArea *scrollArea = qobject_cast<QScrollArea*>(currentPage);
    if (!scrollArea)
        scrollArea = currentPage->findChild<QScrollArea*>();
    if (scrollArea && scrollArea->isVisible() && scrollArea->verticalScrollBar()) {
        QScrollBar *bar = scrollArea->verticalScrollBar();
        if (bar->maximum() > 0) {
            animateScrollToTop(bar);
            return;
        }
    }

    // Fallback: find any visible scrollable vertical scrollbar in the page
    QList<QScrollBar*> allBars = currentPage->findChildren<QScrollBar*>();
    for (QScrollBar *bar : allBars) {
        if (bar->orientation() == Qt::Vertical && bar->maximum() > 0) {
            animateScrollToTop(bar);
            return;
        }
    }
}

void MainWindow::onRefreshClicked()
{
    QWidget *currentPage = m_stackedWidget->currentWidget();
    if (!currentPage)
        return;

    // Try to call common refresh methods on the current page
    if (!QMetaObject::invokeMethod(currentPage, "onRefreshVersions", Qt::DirectConnection))
        QMetaObject::invokeMethod(currentPage, "refresh", Qt::DirectConnection);
}

void MainWindow::saveCurrentInstancePath()
{
    QString filePath = Platform::getDataDirectory() + "/current_instance.txt";
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream stream(&file);
        stream << m_currentInstancePath;
        file.close();
    }
}

void MainWindow::loadCurrentInstancePath()
{
    QString filePath = Platform::getDataDirectory() + "/current_instance.txt";
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        QString instancePath = stream.readAll().trimmed();
        file.close();
        if (!instancePath.isEmpty()) {
            QDir dir(instancePath);
            if (dir.exists()) {
                updateCurrentInstance(instancePath);
            }
        }
    }
}

void MainWindow::restoreDefaultAccount()
{
    AccountInfo defaultAccount = SettingsManager::instance()->getDefaultAccount();
    if (!defaultAccount.username.isEmpty())
    {
        m_topBar->setAccountName(defaultAccount.username);
        // 顶栏头像：截取默认账户皮肤头部正面（圆角由 TopBar 内部处理）
        const QImage skin = SkinDownloader::loadSkinForAccount(defaultAccount.username, defaultAccount.uuid);
        const QImage head = SkinDownloader::cropHeadPortrait(skin);
        if (!head.isNull())
        {
            m_topBar->setAccountAvatar(QPixmap::fromImage(head));
        }
    }
}

void MainWindow::initGameLauncher()
{
    // Initialize game launcher
    m_gameLauncher = GameLauncher::instance();
    
    // Connect launcher signals
    connect(m_gameLauncher, &GameLauncher::launchStatusChanged, this, &MainWindow::onLaunchStatusChanged);
    connect(m_gameLauncher, &GameLauncher::gameStarted, this, &MainWindow::onGameStarted);
    connect(m_gameLauncher, &GameLauncher::gameStopped, this, &MainWindow::onGameStopped);
    connect(m_gameLauncher, &GameLauncher::gameCrashed, this, &MainWindow::onGameCrashed);
    connect(m_gameLauncher, &GameLauncher::launchProgressChanged, this, &MainWindow::onLaunchProgressChanged);
    connect(m_gameLauncher, &GameLauncher::launchDetailAdded, this, &MainWindow::onLaunchDetailAdded);
    connect(m_gameLauncher, &GameLauncher::errorReportGenerated, this, &MainWindow::onErrorReportGenerated);
    connect(m_gameLauncher, &GameLauncher::fileCompletionProgress, this, &MainWindow::onFileCompletionProgress);
    connect(m_gameLauncher, &GameLauncher::fileCompletionFinished, this, &MainWindow::onFileCompletionFinished);
    
    // Connect launch task card signals via task bar
    if (m_taskBar) {
        connect(m_taskBar, &TaskBar::launchTaskCardClicked, this, &MainWindow::onShowDetailsPage);
    }
}

void MainWindow::onLaunchStatusChanged(int status)
{
    // Update UI based on launch status if needed
    qDebug() << "[MainWindow]" << "Launch status changed:" << status;
    
    LaunchTaskCard::LaunchStatus ls = LaunchTaskCard::Idle;
    if (m_taskBar) {
        switch (static_cast<GameLauncher::LaunchStatus>(status)) {
        case GameLauncher::Idle:     ls = LaunchTaskCard::Idle; m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Idle); break;
        case GameLauncher::Launching: ls = LaunchTaskCard::Launching; m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Launching); break;
        case GameLauncher::Running:  ls = LaunchTaskCard::Running; m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Running); break;
        case GameLauncher::Failed:   ls = LaunchTaskCard::Failed; m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Failed); break;
        case GameLauncher::Stopped:  ls = LaunchTaskCard::Stopped; m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Stopped); break;
        }
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(tr("游戏"), ls, 0);
    }
    if (m_launchDetailsPage) {
        switch (static_cast<GameLauncher::LaunchStatus>(status)) {
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
}

void MainWindow::onLaunchProgressChanged(int progress, const QString& message)
{
    if (m_taskBar) {
        m_taskBar->updateLaunchTaskProgress(progress);
        m_taskBar->updateLaunchTaskMessage(message);
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Launching, progress, message);
    }
}

void MainWindow::onLaunchDetailAdded(const QString& detail)
{
    if (m_taskBar) {
        m_taskBar->addLaunchTaskDetail(detail);
    }
    if (m_launchDetailsPage) {
        m_launchDetailsPage->addDetail(detail);
    }
    if (m_instanceManagePage && m_instanceManagePage->instanceLogPage()) {
        m_instanceManagePage->instanceLogPage()->addLauncherLog(detail);
    }
}

void MainWindow::onFileCompletionProgress(int completed, int total, const QString &currentFile)
{
    // 文件补全进度统一显示在启动任务卡片上（35%~50% 区间）
    if (m_taskBar) {
        if (total > 0) {
            // 将文件补全进度映射到启动整体进度的 35%~50% 区间
            int filePercent = qBound(0, completed * 100 / total, 100);
            int overall = 35 + filePercent * 15 / 100;
            m_taskBar->updateLaunchTaskProgress(overall);
        }
        if (!currentFile.isEmpty()) {
            m_taskBar->updateLaunchTaskMessage(tr("补全文件: %1").arg(currentFile));
        }
    }
    if (m_taskListPage && total > 0) {
        int filePercent = qBound(0, completed * 100 / total, 100);
        int overall = 35 + filePercent * 15 / 100;
        QString msg = currentFile.isEmpty() ? tr("补全文件中...") : tr("补全文件: %1").arg(currentFile);
        m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Launching, overall, msg);
    }
}

void MainWindow::onFileCompletionFinished(bool success)
{
    // 文件补全只是启动流程的一个中间步骤，仅更新启动卡片消息
    // 任务由后续 onGameStarted/onGameCrashed 统一收尾
    if (m_taskBar) {
        if (success) {
            m_taskBar->updateLaunchTaskMessage(tr("文件补全完成，继续启动游戏..."));
        } else {
            m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Failed);
            m_taskBar->updateLaunchTaskMessage(tr("文件补全失败"));
        }
    }
    if (m_taskListPage) {
        if (success) {
            m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Launching, 50, tr("文件补全完成，继续启动游戏..."));
        } else {
            m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Failed, 50, tr("文件补全失败"));
        }
    }
}

void MainWindow::onErrorReportGenerated(const QString& report)
{
    qDebug() << "[MainWindow]" << "Error report generated:" << report;
    // Show error report in a message box or add to launch details
    if (m_taskBar) {
        m_taskBar->addLaunchTaskDetail("错误分析鎶ュ憡:");
        m_taskBar->addLaunchTaskDetail(report);
    }
    // Optionally show a message box with the error report
    AppMessageBox::information(this, tr("错误分析"), report);
}

#ifdef Q_OS_ANDROID
void MainWindow::toggleGameAssistantOverlay()
{
    if (!m_instanceAssistantWindow) {
        m_instanceAssistantWindow = new InstanceAssistantWindow(this);
        // 安卓游戏内使用侧栏形态：贴屏幕右侧四分之一、半透明
        m_instanceAssistantWindow->setOverlayMode(true);
        // 转发资源管理页的存档快捷启动信号到主窗口处理
        connect(m_instanceAssistantWindow, &InstanceAssistantWindow::quickLaunchSaveRequested,
                this, &MainWindow::onQuickLaunchSaveClicked);
    }

    // 再次点击悬浮图标收起侧栏
    if (m_instanceAssistantWindow->isVisible()) {
        m_instanceAssistantWindow->hide();
        return;
    }

    // 注入当前实例上下文（路径、版本、loader）
    m_instanceAssistantWindow->setInstanceContext(
        m_currentInstancePath,
        m_currentInstanceVersion,
        m_currentInstanceLoader);
    m_instanceAssistantWindow->show();
    m_instanceAssistantWindow->raise();
    m_instanceAssistantWindow->activateWindow();
}
#endif

void MainWindow::onGameStarted()
{
    qDebug() << "[MainWindow]" << "Game started successfully";
#ifdef Q_OS_ANDROID
    // 游戏启动成功：显示游戏内悬浮图标（实例助手入口）
    if (m_gameFloatingIcon) {
        m_gameFloatingIcon->show();
    }
#endif
    // 游戏窗口已出现，启动任务卡片切换为"运行中"状态
    if (m_taskBar) {
        m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Running);
        m_taskBar->updateLaunchTaskProgress(100);
        m_taskBar->updateLaunchTaskMessage(tr("游戏运行中"));
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Running, 100, tr("游戏运行中"));
    }
    if (m_launchDetailsPage) {
        m_launchDetailsPage->setLaunchStatus("游戏运行中", true);
    }

    onBackToMain();
}

void MainWindow::onGameStopped(int exitCode)
{
    qDebug() << "[MainWindow]" << "Game stopped with exit code:" << exitCode;

#ifdef Q_OS_ANDROID
    // 游戏退出：隐藏游戏内悬浮图标与实例助手侧栏
    if (m_gameFloatingIcon) {
        m_gameFloatingIcon->hide();
    }
    if (m_instanceAssistantWindow && m_instanceAssistantWindow->isVisible()) {
        m_instanceAssistantWindow->hide();
    }
#endif

    // 用户手动取消启动: 不显示错误信息，直接清除启动任务卡片
    if (m_gameLauncher->wasUserCancelled()) {
        if (m_taskBar) {
            m_taskBar->showLaunchTaskCard(false);
        }
        if (m_taskListPage) {
            m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Idle, 0);
        }
        if (m_launchDetailsPage) {
            m_launchDetailsPage->setLaunchStatus(tr("启动日志"), false);
        }
        return;
    }

    // 游戏正常退出（exitCode == 0）: 显示"游戏已正常退出"提示，并删除当前启动任务
    if (exitCode == 0) {
        NotificationManager::showSuccess(this, tr("游戏已正常退出"));
        if (m_taskBar) {
            m_taskBar->showLaunchTaskCard(false);
        }
        if (m_taskListPage) {
            m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Idle, 0);
        }
        if (m_launchDetailsPage) {
            m_launchDetailsPage->setLaunchStatus(tr("游戏已停止"), false);
        }
        // 刷新首页最近游玩区域（游戏退出后 level.dat 的 LastPlayed 已更新）
        if (m_homePage) {
            m_homePage->refreshRecentPlays();
        }
        return;
    }

    // 游戏异常退出（exitCode != 0 且非用户取消）: 保留任务卡片显示异常退出信息
    if (m_taskBar) {
        m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Stopped);
        m_taskBar->updateLaunchTaskMessage(tr("游戏异常退出: %1").arg(exitCode));
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Stopped, 100, tr("游戏异常退出: %1").arg(exitCode));
    }
    if (m_launchDetailsPage) {
        m_launchDetailsPage->setLaunchStatus(tr("游戏已停止"), false);
    }
}

void MainWindow::onGameCrashed(const QString &error)
{
    qDebug() << "[MainWindow]" << "Game crashed:" << error;

#ifdef Q_OS_ANDROID
    // 游戏崩溃：隐藏游戏内悬浮图标与实例助手侧栏
    if (m_gameFloatingIcon) {
        m_gameFloatingIcon->hide();
    }
    if (m_instanceAssistantWindow && m_instanceAssistantWindow->isVisible()) {
        m_instanceAssistantWindow->hide();
    }
#endif

    // 重试已调度但尚未执行时，忽略重复的 gameCrashed 信号
    // （QProcess 崩溃时 onProcessError 和 onProcessFinished 都会触发 gameCrashed）
    if (m_launchRetryPending) {
        qDebug() << "[MainWindow]" << "Retry already pending, ignoring duplicate gameCrashed signal";
        return;
    }

    // 仅在"启动期崩溃"（游戏窗口尚未出现）且尚未重试时，自动检查文件完整性并重新启动
    // 运行期崩溃（游戏已成功运行后崩溃）不触发自动重试，交由错误分析器处理
    if (!m_launchRetryAttempted && !m_gameLauncher->wasGameStartedSuccessfully()) {
        m_launchRetryAttempted = true;
        m_launchRetryPending = true;
        qDebug() << "[MainWindow]" << "Launch-time crash detected, retrying with file completion...";

        NotificationManager::showInfo(this, tr("游戏启动失败，正在检查文件完整性并重新启动..."));

        // 延迟到下一个事件循环执行重试，确保当前 onProcessFinished/onProcessError
        // 中的 cleanup() 已执行完毕，避免与新启动的进程冲突
        QTimer::singleShot(0, this, [this, error]() {
            m_launchRetryPending = false;

            if (m_currentInstancePath.isEmpty()) {
                return;
            }

            GameLauncher::LaunchConfig config;
            config.instancePath = m_currentInstancePath;
            config.accountName = m_topBar->accountName().isEmpty() ? "Player" : m_topBar->accountName();

            SettingsManager *settings = SettingsManager::instance();
            config.javaPath = settings->getJavaPath();
            config.maxMemory = SettingsManager::DEFAULT_MAX_MEMORY_MB;
            config.minMemory = SettingsManager::DEFAULT_MIN_MEMORY_MB;

            // 重试进度显示在启动任务卡片上，不创建下载任务
            if (m_taskBar) {
                m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Launching);
                m_taskBar->updateLaunchTaskProgress(35);
                m_taskBar->updateLaunchTaskMessage(tr("检查并补全文件..."));
            }
            if (m_taskListPage) {
                m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Launching, 35, tr("检查并补全文件..."));
            }

            // 重试: skipFileCompletion=false，执行文件完整性检查并补全缺失文件
            if (!m_gameLauncher->launchGame(config, false)) {
                QString failMsg = m_gameLauncher->errorMessage().isEmpty()
                    ? tr("重新启动失败") : m_gameLauncher->errorMessage();
                if (m_taskBar) {
                    m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Failed);
                    m_taskBar->updateLaunchTaskMessage(failMsg);
                }
                if (m_taskListPage) {
                    m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Failed, 0, failMsg);
                }
                NotificationManager::showError(this,
                    tr("重新启动失败: ") +
                    (m_gameLauncher->errorMessage().isEmpty() ? error : m_gameLauncher->errorMessage()));
            }
        });
        return;
    }

    // 非启动期崩溃，或已重试过: 更新启动任务卡片为失败状态（错误分析由 ErrorAnalyzer 处理）
    QString failMsg = error.isEmpty() ? tr("游戏崩溃") : error;
    if (m_taskBar) {
        m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Failed);
        m_taskBar->updateLaunchTaskMessage(failMsg);
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Failed, 0, failMsg);
    }
}

void MainWindow::onLaunchTaskCardClicked()
{
    qDebug() << "[MainWindow]" << "Launch task card clicked";
    // Here you can add additional functionality when the task card is clicked
    // For example, show more detailed information or open a separate window
}

void MainWindow::onLaunchGameClicked()
{
    if (m_currentInstancePath.isEmpty()) {
        showInstanceSelectPage();
        return;
    }

    if (m_gameLauncher->status() == GameLauncher::Running ||
        m_gameLauncher->status() == GameLauncher::Launching) {
        return;
    }

    // 每次用户主动点击启动都视为"首次启动": 重置重试标记
    // 首次启动跳过文件完整性检查与补全，若启动失败再自动重试时启用补全
    m_launchRetryAttempted = false;
    m_launchRetryPending = false;

    if (m_taskBar) {
        m_taskBar->showLaunchTaskCard(true);
        m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Idle);
        m_taskBar->updateLaunchTaskProgress(0);
        m_taskBar->updateLaunchTaskMessage("准备启动游戏...");
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Idle, 0);
    }

    GameLauncher::LaunchConfig config;
    config.instancePath = m_currentInstancePath;
    config.accountName = m_topBar->accountName().isEmpty() ? "Player" : m_topBar->accountName();

    SettingsManager *settings = SettingsManager::instance();
    // 统计该实例的启动次数（概览页「启动次数」卡片展示）
    const QString launchCountKey = "instance/" + m_currentInstancePath + "/launchCount";
    settings->setProperty(launchCountKey,
                          settings->getProperty(launchCountKey, 0).toInt() + 1);
    config.javaPath = settings->getJavaPath();
    config.maxMemory = SettingsManager::DEFAULT_MAX_MEMORY_MB;
    config.minMemory = SettingsManager::DEFAULT_MIN_MEMORY_MB;

    // 启动进度统一通过启动任务卡片显示，不创建下载任务
    // 首次启动: skipFileCompletion=true，跳过文件完整性检查与补全
    if (!m_gameLauncher->launchGame(config, true)) {
        if (m_taskBar) {
            m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Failed);
            m_taskBar->updateLaunchTaskMessage(
                m_gameLauncher->errorMessage().isEmpty() ? tr("启动失败") : m_gameLauncher->errorMessage());
        }
        if (m_taskListPage) {
            m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Failed, 0,
                m_gameLauncher->errorMessage().isEmpty() ? tr("启动失败") : m_gameLauncher->errorMessage());
        }
        if (!m_gameLauncher->errorMessage().isEmpty()) {
            NotificationManager::showError(this, m_gameLauncher->errorMessage());
        }
    }
}

void MainWindow::onLaunchGameClicked(const QString &instancePath)
{
    // 来自 InstanceSelectPage 的启动请求：使用指定的实例路径
    // 先更新当前实例，再复用无参重载的启动流程
    if (!instancePath.isEmpty()) {
        updateCurrentInstance(instancePath);
    }
    onLaunchGameClicked();
}

void MainWindow::launchGameWithExtraArgs(const QStringList &extraGameArgs)
{
    if (m_currentInstancePath.isEmpty()) {
        showInstanceSelectPage();
        return;
    }

    if (m_gameLauncher->status() == GameLauncher::Running ||
        m_gameLauncher->status() == GameLauncher::Launching) {
        return;
    }

    m_launchRetryAttempted = false;
    m_launchRetryPending = false;

    if (m_taskBar) {
        m_taskBar->showLaunchTaskCard(true);
        m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Idle);
        m_taskBar->updateLaunchTaskProgress(0);
        m_taskBar->updateLaunchTaskMessage("准备启动游戏...");
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Idle, 0);
    }

    GameLauncher::LaunchConfig config;
    config.instancePath = m_currentInstancePath;
    config.accountName = m_topBar->accountName().isEmpty() ? "Player" : m_topBar->accountName();

    SettingsManager *settings = SettingsManager::instance();
    // 统计该实例的启动次数（概览页「启动次数」卡片展示）
    const QString launchCountKey = "instance/" + m_currentInstancePath + "/launchCount";
    settings->setProperty(launchCountKey,
                          settings->getProperty(launchCountKey, 0).toInt() + 1);
    config.javaPath = settings->getJavaPath();
    config.maxMemory = SettingsManager::DEFAULT_MAX_MEMORY_MB;
    config.minMemory = SettingsManager::DEFAULT_MIN_MEMORY_MB;
    config.gameArgs = extraGameArgs;

    if (!m_gameLauncher->launchGame(config, true)) {
        if (m_taskBar) {
            m_taskBar->updateLaunchTaskStatus(LaunchTaskCard::Failed);
            m_taskBar->updateLaunchTaskMessage(
                m_gameLauncher->errorMessage().isEmpty() ? tr("启动失败") : m_gameLauncher->errorMessage());
        }
        if (m_taskListPage) {
            m_taskListPage->setLaunchTaskInfo(tr("游戏"), LaunchTaskCard::Failed, 0,
                m_gameLauncher->errorMessage().isEmpty() ? tr("启动失败") : m_gameLauncher->errorMessage());
        }
        if (!m_gameLauncher->errorMessage().isEmpty()) {
            NotificationManager::showError(this, m_gameLauncher->errorMessage());
        }
    }
}

void MainWindow::onQuickLaunchSaveClicked(const QString &saveName)
{
    // 使用 --quickPlaySingleplayer 直接进入指定存档（Minecraft 1.21+ 支持）
    // 旧版本会忽略此参数，游戏正常启动后由用户在菜单选择存档
    QStringList extraArgs;
    if (!saveName.isEmpty()) {
        extraArgs << "--quickPlaySingleplayer" << saveName;
    }
    launchGameWithExtraArgs(extraArgs);
}

void MainWindow::onQuickLaunchServerClicked(const QString &address, quint16 port)
{
    // 使用 --server 和 --port 启动游戏并自动连接到服务器
    QStringList extraArgs;
    extraArgs << "--server" << address << "--port" << QString::number(port);
    launchGameWithExtraArgs(extraArgs);
}

void MainWindow::onSaveSettingsOpened()
{
    // 进入存档设置子页面：复用 TopBar 的返回按钮与标题
    m_topBar->setTitle(tr("实例管理>存档设置"));

    // 断开所有 backClicked 信号连接
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);

    connect(m_topBar, &TopBar::backClicked, this, [this]() {
        // 返回存档列表
        if (m_instanceManagePage)
            m_instanceManagePage->setCurrentTab(5);
        m_topBar->setTitle(tr("实例管理>存档"));
        // 恢复主流程返回行为
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    });
}

void MainWindow::onShaderSettingsOpened()
{
    // 进入光影设置子页面：复用 TopBar 的返回按钮与标题
    m_topBar->setTitle(tr("实例管理>光影设置"));

    // 断开所有 backClicked 信号连接
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);

    connect(m_topBar, &TopBar::backClicked, this, [this]() {
        // 返回光影包列表
        if (m_instanceManagePage)
            m_instanceManagePage->setCurrentTab(11);
        m_topBar->setTitle(tr("实例管理>光影包"));
        // 恢复主流程返回行为
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    });
}

void MainWindow::onProjectionEditOpened()
{
    // 进入投影编辑子页面：复用 TopBar 的返回按钮与标题
    m_topBar->setTitle(tr("实例管理>投影编辑"));

    // 断开所有 backClicked 信号连接
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);

    connect(m_topBar, &TopBar::backClicked, this, [this]() {
        // 返回投影列表
        if (m_instanceManagePage)
            m_instanceManagePage->setCurrentTab(11);
        m_topBar->setTitle(tr("实例管理>蓝图"));
        // 恢复主流程返回行为
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    });
}

void MainWindow::onModifyVersionSelected(const QString &versionId, const QString &versionType,
                                         const QString &instancePath, const QString &instanceName)
{
    ensurePageInitialized(PageIndex::LoaderDetailPage);

    setSideBarVisible(false);
    m_currentVersionId = versionId;
    m_currentVersionType = versionType;
    m_topBar->setTitle(tr("资源>修改实例%1版本详情").arg(versionId));

    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToInstallPage);

    if (m_loaderDetailPage) {
        m_loaderDetailPage->exitModifyMode();
        m_loaderDetailPage->setVersionInfo(versionId);
        m_loaderDetailPage->setVersionType(versionType);
        m_loaderDetailPage->enterModifyMode(instancePath, instanceName,
                                             m_currentInstanceVersion, m_currentInstanceLoader);
    }

    animatedSwitchToPage(PageIndex::LoaderDetailPage);
}

void MainWindow::onInstanceModified(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return;
    updateCurrentInstance(instancePath);
    NotificationManager::showSuccess(this, tr("实例修改成功！"));

    if (m_installInstancePage) {
        m_installInstancePage->exitModifyMode();
    }
    if (m_loaderDetailPage) {
        m_loaderDetailPage->exitModifyMode();
    }

    // 返回实例选择页，刷新实例列表
    ensurePageInitialized(PageIndex::InstanceSelectPage);
    if (m_instanceSelectPage) {
        m_instanceSelectPage->loadInstances();
        m_instanceSelectPage->updateInstanceList();
    }
    setSideBarVisible(true);
    m_topBar->setInstanceSelectTitle();
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    animatedSwitchToPage(PageIndex::InstanceSelectPage);
}

void MainWindow::onVersionSelected(const QString &versionId, const QString &versionType)
{
    // 纭?繚椤甸潰宸插垵濮嬪寲
    ensurePageInitialized(PageIndex::LoaderDetailPage);
    
    setSideBarVisible(false);
    m_currentVersionId = versionId;
    m_currentVersionType = versionType;
    m_topBar->setTitle(tr("资源>安装新实例%1版本详情").arg(versionId));
    
    // 断开所有backClicked信号连接
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToInstallPage);
    
    if (m_loaderDetailPage) {
        m_loaderDetailPage->setVersionInfo(versionId);
        m_loaderDetailPage->setVersionType(versionType);
    }
    
    animatedSwitchToPage(PageIndex::LoaderDetailPage);
}

void MainWindow::onBackToInstallPage()
{
    qDebug() << "[MainWindow]" << "Back to install page";
    
    ensurePageInitialized(PageIndex::InstallInstancePage);
    
    setSideBarVisible(true);
    m_topBar->setResourcesTitle();
    animatedSwitchToPage(PageIndex::InstallInstancePage);
    
    // 重置返回按钮到主页面，避免再次点击时仍在同一页
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
}

void MainWindow::onViewAllVersions(const QString &loaderName, const QString &minecraftVersion)
{
    // 纭?繚椤甸潰宸插垵濮嬪寲
    ensurePageInitialized(PageIndex::ForgeVersionListPage);
    
    setSideBarVisible(false);
    QString prefix = (m_loaderDetailPage && m_loaderDetailPage->modifyMode())
                         ? tr("修改实例") : tr("安装新实例");
    m_topBar->setTitle(tr("资源>%1%2版本详情>%3版本详情").arg(prefix).arg(minecraftVersion, loaderName));
    
    // 断开所有backClicked信号连接
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToLoaderDetail);
    
    if (m_forgeVersionListPage) {
        m_forgeVersionListPage->setLoaderName(loaderName);
        m_forgeVersionListPage->setMinecraftVersion(minecraftVersion);
    }

    animatedSwitchToPage(PageIndex::ForgeVersionListPage);
}

void MainWindow::onBackToLoaderDetail()
{
    qDebug() << "[MainWindow]" << "Back to loader detail page";
    
    // 纭?繚椤甸潰宸插垵濮嬪寲
    ensurePageInitialized(PageIndex::LoaderDetailPage);
    
    // Hide sidebar
    setSideBarVisible(false);
    
    // Restore breadcrumb using stored version info
    QString prefix = (m_loaderDetailPage && m_loaderDetailPage->modifyMode())
                         ? tr("修改实例") : tr("安装新实例");
    m_topBar->setTitle(tr("资源>%1%2版本详情").arg(prefix).arg(m_currentVersionId));
    
    // 断开所有backClicked信号连接
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    
    // Connect top bar back signal to return to install page
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToInstallPage);
    
    // Show loader detail page
    animatedSwitchToPage(PageIndex::LoaderDetailPage);
}

void MainWindow::onForgeVersionSelected(const QString &loaderName, const QString & /* mcVersion */, const QString &forgeVersion)
{
    // 纭?繚椤甸潰宸插垵濮嬪寲
    ensurePageInitialized(PageIndex::LoaderDetailPage);
    
    if (m_loaderDetailPage) {
        m_loaderDetailPage->addSelectedLoader(loaderName, forgeVersion);
    }
    
    onBackToLoaderDetail();
}

void MainWindow::onShowTaskListPageRequested()
{
    showTaskListPage();
}

void MainWindow::onTaskListPageBack()
{
    onBackToMain();
}

void MainWindow::onTaskDetailRequested(const QString &taskId)
{
    showTaskDetailPage(taskId);
}

void MainWindow::onTaskDetailPageBack()
{
    showTaskListPage();
}

void MainWindow::onTaskCancelled(const QString &taskId)
{
    Q_UNUSED(taskId);
    showTaskListPage();
}

void MainWindow::onModDownloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile)
{
    QString instancePath = m_currentInstancePath;
    if (instancePath.isEmpty()) {
        NotificationManager::showError(this, tr("请先选择一个实例"));
        return;
    }

    QString fileName = versionFile.fileName;
    if (fileName.isEmpty()) {
        QUrl url(versionFile.downloadUrl);
        fileName = url.fileName();
        if (fileName.isEmpty() || fileName.contains('?')) {
            QString safeName = modInfo.name;
            safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
            QString ver = versionFile.version.isEmpty() ? modInfo.latestVersion : versionFile.version;
            if (!ver.isEmpty())
                safeName += "-" + ver;
            fileName = safeName + ".jar";
        }
    }

    QString destDir = instancePath + "/mods";
    QString destPath = destDir + "/" + fileName;

    QDir().mkpath(destDir);

    QString instanceName = QFileInfo(instancePath).fileName();
    QString taskId = DownloadTaskManager::instance()->addTask(
        instanceName, instancePath, QString(), QStringList());
    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading, tr("下载模组: ") + modInfo.name);
    DownloadTaskManager::instance()->updateTaskCurrentFile(taskId, fileName);
    DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::ClientJar, 0);

    DownloadEngine *engine = new DownloadEngine(this);
    connect(engine, &DownloadEngine::downloadBytesProgress, this,
        [this, taskId, fileName](qint64 received, qint64 total) {
            DownloadTaskManager::instance()->updateTaskFileProgress(taskId, fileName, received, total);
            if (total > 0) {
                int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
            }
        });

    engine->downloadFile({versionFile.downloadUrl}, destPath,
        [this, engine, taskId, fileName, modInfo](bool success) {
            if (success) {
                DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::Completed, 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Completed, tr("下载完成"));
                NotificationManager::showSuccess(this, tr("模组 \"%1\" 下载完成").arg(modInfo.name));
            } else {
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Failed, tr("下载失败"));
                NotificationManager::showError(this, tr("模组 \"%1\" 下载失败").arg(modInfo.name));
            }
            engine->deleteLater();
        });
}

void MainWindow::initContentDownloadPage(ContentType type)
{
    Q_UNUSED(type);
    // ContentDownloadPage is created fresh each time in showContentDownloadPage
    m_pageInitialized[PageIndex::ContentDownloadPage] = true;
}

void MainWindow::initContentDetailPage(ContentType type)
{
    Q_UNUSED(type);
    // ContentDetailPage is created fresh each time in showContentDetailPage
    m_pageInitialized[PageIndex::ContentDetailPage] = true;
}

void MainWindow::initContentListPage(ContentType type)
{
    Q_UNUSED(type);
    // ContentListPage is created fresh each time in showContentListPage
    m_pageInitialized[PageIndex::ContentListPage] = true;
}

void MainWindow::showContentDownloadPage(ContentType type)
{
    ContentTypeConfig config = ContentTypeConfig::getConfig(type);

    // Create or reuse the content download page
    if (m_contentDownloadPage)
    {
        m_stackedWidget->removeWidget(m_contentDownloadPage);
        m_contentDownloadPage->deleteLater();
        m_contentDownloadPage = nullptr;
    }

    m_contentDownloadPage = new ContentDownloadPage(type, this);
    m_stackedWidget->addWidget(m_contentDownloadPage);
    if (m_pageAnimator)
        m_pageAnimator->animateToWidget(m_contentDownloadPage);
    else
        m_stackedWidget->setCurrentWidget(m_contentDownloadPage);

    setSideBarVisible(true);
    m_topBar->setTitle(tr("资源>%1").arg(config.displayName));
    m_topBar->setTranslateButtonVisible(true);

    m_contentDownloadPage->setCurrentInstancePath(m_currentInstancePath);
    m_contentDownloadPage->loadInstances();
    m_contentDownloadPage->loadInitialMods();

    connect(m_contentDownloadPage, &ContentDownloadPage::contentClicked, this, [this, type](const ModInfo &info)
    {
        showContentDetailPage(info, type);
    });

    connect(m_contentDownloadPage, &ContentDownloadPage::cardDownloadRequested, this, [this, type](const ModInfo &info)
    {
        onCardDownloadRequested(info, type, 0);
    });
}

void MainWindow::showContentDetailPage(const ModInfo &info, ContentType type)
{
    ContentTypeConfig config = ContentTypeConfig::getConfig(type);

    if (m_contentDetailPage)
    {
        m_stackedWidget->removeWidget(m_contentDetailPage);
        m_contentDetailPage->deleteLater();
        m_contentDetailPage = nullptr;
    }

    m_contentDetailPage = new ContentDetailPage(type, this);
    m_stackedWidget->addWidget(m_contentDetailPage);
    m_stackedWidget->setCurrentWidget(m_contentDetailPage);

    setSideBarVisible(true);
    m_topBar->setTitle(tr("资源>%1>%2").arg(config.displayName, info.name));
    m_topBar->setTranslateButtonVisible(true);

    m_contentDetailPage->setModInfo(info);

    connect(m_contentDetailPage, &ContentDetailPage::downloadRequested, this, [this, type](const ModInfo &modInfo, const ModVersionFile &versionFile)
    {
        onContentDownloadRequested(modInfo, versionFile, type);
    });

    connect(m_contentDetailPage, &ContentDetailPage::backToListRequested, this, [this, type]()
    {
        showContentDownloadPage(type);
    });

    connect(m_contentDetailPage, &ContentDetailPage::contentClicked, this, [this, type](const ModInfo &modInfo)
    {
        showContentDetailPage(modInfo, type);
    });

    // 用户在详情页点击“新建收藏夹...”时，弹出对话框并直接将当前资源收藏到新分组
    connect(m_contentDetailPage, &ContentDetailPage::addToFavoritesRequested,
            this, [this](const ModInfo &modInfo, ContentType type, const QString &source)
    {
        FavoriteFolderDialog dlg(FavoriteFolderDialog::CreateMode, this);
        if (dlg.exec() != QDialog::Accepted)
        {
            return;
        }
        QString newId = FavoritesManager::instance()->createFolder(dlg.folderName());
        if (!newId.isEmpty())
        {
            FavoritesManager::instance()->addFavorite(newId, modInfo, type, source);
            NotificationManager::showSuccess(this, tr("已添加到新收藏夹“%1”").arg(dlg.folderName()));
            if (m_contentDetailPage)
            {
                m_contentDetailPage->refreshFavoriteState();
            }
        }
    });
}

void MainWindow::showContentListPage(ContentType type)
{
    ContentTypeConfig config = ContentTypeConfig::getConfig(type);

    if (m_contentListPage)
    {
        m_stackedWidget->removeWidget(m_contentListPage);
        m_contentListPage->deleteLater();
        m_contentListPage = nullptr;
    }

    m_contentListPage = new ContentListPage(type, this);
    m_stackedWidget->addWidget(m_contentListPage);
    m_stackedWidget->setCurrentWidget(m_contentListPage);

    setSideBarVisible(true);
    m_topBar->setTitle(tr("资源>%1>本地管理").arg(config.displayName));

    m_contentListPage->loadInstances();

    connect(m_contentListPage, &ContentListPage::contentClicked, this, [this, type](const ModInfo &info)
    {
        showContentDetailPage(info, type);
    });
}

void MainWindow::showFavoritesPage(const QString &folderId)
{
    FavoriteFolder folder = FavoritesManager::instance()->folder(folderId);
    if (folder.id.isEmpty())
    {
        NotificationManager::showError(this, tr("收藏夹不存在或已被删除"));
        return;
    }

    if (m_favoritesPage)
    {
        m_stackedWidget->removeWidget(m_favoritesPage);
        m_favoritesPage->deleteLater();
        m_favoritesPage = nullptr;
    }

    m_favoritesPage = new FavoritesPage(this);
    m_stackedWidget->addWidget(m_favoritesPage);
    if (m_pageAnimator)
    {
        m_pageAnimator->animateToWidget(m_favoritesPage);
    }
    else
    {
        m_stackedWidget->setCurrentWidget(m_favoritesPage);
    }

    setSideBarVisible(true);
    m_topBar->setTitle(tr("资源>%1").arg(folder.name));

    m_favoritesPage->setFolder(folderId);

    connect(m_favoritesPage, &FavoritesPage::favoriteClicked,
            this, &MainWindow::showContentDetailFromFavorite);
    connect(m_favoritesPage, &FavoritesPage::manageFoldersRequested,
            this, &MainWindow::onManageFavoriteFoldersRequested);

    // 收藏夹内容变化时，刷新当前页
    // 注意：必须捕获 page 指针而非使用 m_favoritesPage，否则在页面切换期间
    // 旧页面的 deleteLater() 尚未执行时收到信号，会错误地操作新页面
    FavoritesPage *page = m_favoritesPage;
    connect(FavoritesManager::instance(), &FavoritesManager::folderChanged,
            page, [page, folderId](const QString &changedId)
    {
        if (changedId == folderId)
        {
            page->setFolder(folderId);
        }
    });
}

void MainWindow::showContentDetailFromFavorite(const FavoriteItem &item)
{
    ContentType type = static_cast<ContentType>(item.contentType);

    // 用最少的展示字段构造 ModInfo，详情页加载时会按 id/source 重新拉取完整信息
    ModInfo info = item.toModInfo();

    showContentDetailPage(info, type);
}

void MainWindow::onCreateFavoriteFolderRequested()
{
    FavoriteFolderDialog dlg(FavoriteFolderDialog::CreateMode, this);
    if (dlg.exec() == QDialog::Accepted)
    {
        QString name = dlg.folderName();
        QString newId = FavoritesManager::instance()->createFolder(name);
        if (!newId.isEmpty())
        {
            NotificationManager::showSuccess(this, tr("已创建收藏夹“%1”").arg(name));
            // foldersChanged 信号会触发 onFavoritesChanged，刷新侧边栏
            // 直接展示新建的收藏夹
            showFavoritesPage(newId);
        }
        else
        {
            NotificationManager::showError(this, tr("创建收藏夹失败"));
        }
    }
}

void MainWindow::onManageFavoriteFoldersRequested()
{
    // 提供重命名 / 删除入口：弹出一个简单的菜单
    QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();

    QMenu menu(this);
    menu.setWindowTitle(tr("管理收藏夹"));

    // 记录各类动作，使用指针比较避免依赖文本（更健壮，不受翻译/助记符影响）
    QList<QAction *> renameActions;
    QList<QAction *> deleteActions;

    if (folders.isEmpty())
    {
        menu.addAction(tr("暂无收藏夹"))->setEnabled(false);
    }
    else
    {
        for (const FavoriteFolder &f : folders)
        {
            QMenu *sub = menu.addMenu(f.name);
            QAction *renameAct = sub->addAction(tr("重命名"));
            QAction *deleteAct = sub->addAction(tr("删除"));
            renameAct->setData(f.id);
            deleteAct->setData(f.id);
            renameActions.append(renameAct);
            deleteActions.append(deleteAct);
        }
    }

    menu.addSeparator();
    QAction *newAct = menu.addAction(tr("新建收藏夹"));

    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen)
    {
        return;
    }

    if (chosen == newAct)
    {
        onCreateFavoriteFolderRequested();
        return;
    }

    QString folderId = chosen->data().toString();
    if (folderId.isEmpty())
    {
        return;
    }

    if (renameActions.contains(chosen))
    {
        FavoriteFolder folder = FavoritesManager::instance()->folder(folderId);
        FavoriteFolderDialog dlg(FavoriteFolderDialog::RenameMode, this);
        dlg.setInitialName(folder.name);
        if (dlg.exec() == QDialog::Accepted)
        {
            if (FavoritesManager::instance()->renameFolder(folderId, dlg.folderName()))
            {
                NotificationManager::showSuccess(this, tr("已重命名"));
            }
        }
    }
    else if (deleteActions.contains(chosen))
    {
        FavoriteFolder folder = FavoritesManager::instance()->folder(folderId);
        AppMessageBox::StandardButton btn = AppMessageBox::question(
            this, tr("删除收藏夹"),
            tr("确定要删除收藏夹“%1”吗？其中的 %2 个收藏条目也会被移除。")
                .arg(folder.name).arg(folder.items.size()),
            AppMessageBox::Yes | AppMessageBox::No);
        if (btn == AppMessageBox::Yes)
        {
            if (FavoritesManager::instance()->deleteFolder(folderId))
            {
                NotificationManager::showSuccess(this, tr("已删除收藏夹"));
                // 切换到第一个收藏夹
                QList<FavoriteFolder> remaining = FavoritesManager::instance()->folders();
                if (!remaining.isEmpty())
                {
                    showFavoritesPage(remaining.first().id);
                }
            }
            else
            {
                NotificationManager::showError(this, tr("至少需要保留一个收藏夹"));
            }
        }
    }
}

void MainWindow::onFavoritesChanged()
{
    if (m_sideBar)
    {
        m_sideBar->refreshNavStructure();
    }

    // 刷新侧边栏父级按钮后，若 SubNavPanel 当前正展开"资源"分组（parentIndex==1），
    // 需要重新调用 showForParent 以读取最新的收藏夹列表，否则子面板仍显示旧数据
    if (m_subNavPanel && m_subNavPanel->isVisible())
    {
        int curParent = m_subNavPanel->currentParentIndex();
        if (curParent >= 0)
        {
            // 保留当前选中状态，避免展开"资源"时跳回首个子项
            m_subNavPanel->showForParent(curParent, false);
        }
    }
}

void MainWindow::onContentDownloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile, ContentType contentType)
{
    QString instancePath = m_currentInstancePath;
    if (instancePath.isEmpty())
    {
        NotificationManager::showError(this, tr("请先选择一个实例"));
        return;
    }

    ContentTypeConfig config = ContentTypeConfig::getConfig(contentType);

    QString fileName = versionFile.fileName;
    if (fileName.isEmpty())
    {
        QUrl url(versionFile.downloadUrl);
        fileName = url.fileName();
        if (fileName.isEmpty() || fileName.contains('?'))
        {
            QString safeName = modInfo.name;
            safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
            QString ver = versionFile.version.isEmpty() ? modInfo.latestVersion : versionFile.version;
            if (!ver.isEmpty())
            {
                safeName += "-" + ver;
            }
            fileName = safeName + ".jar";
        }
    }

    QString destDir = instancePath + "/" + config.folderName;
    QString destPath = destDir + "/" + fileName;

    QDir().mkpath(destDir);

    QString instanceName = QFileInfo(instancePath).fileName();
    QString taskId = DownloadTaskManager::instance()->addTask(
        instanceName, instancePath, QString(), QStringList());
    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading, tr("下载%1: ").arg(config.displayName) + modInfo.name);
    DownloadTaskManager::instance()->updateTaskCurrentFile(taskId, fileName);
    DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::ClientJar, 0);

    DownloadEngine *engine = new DownloadEngine(this);
    connect(engine, &DownloadEngine::downloadBytesProgress, this,
        [this, taskId, fileName](qint64 received, qint64 total)
        {
            DownloadTaskManager::instance()->updateTaskFileProgress(taskId, fileName, received, total);
            if (total > 0)
            {
                int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
            }
        });

    // For worlds, use ContentDownloader to handle extraction
    if (contentType == ContentType::World)
    {
        auto *downloader = new ContentDownloader(this);
        connect(downloader, &ContentDownloader::downloadProgress, this,
            [this, taskId, fileName](qint64 received, qint64 total)
            {
                DownloadTaskManager::instance()->updateTaskFileProgress(taskId, fileName, received, total);
                if (total > 0)
                {
                    int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
                    DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
                }
            });
        connect(downloader, &ContentDownloader::downloadFinished, this,
            [this, taskId, modInfo, downloader](const QString &)
            {
                DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::Completed, 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Completed, tr("下载完成"));
                NotificationManager::showSuccess(this, tr("世界 \"%1\" 下载并解压完成").arg(modInfo.name));
                downloader->deleteLater();
            });
        connect(downloader, &ContentDownloader::downloadFailed, this,
            [this, taskId, modInfo, downloader](const QString &error)
            {
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Failed, error);
                NotificationManager::showError(this, tr("世界 \"%1\" 下载失败: %2").arg(modInfo.name, error));
                downloader->deleteLater();
            });
        downloader->downloadToInstance(versionFile.downloadUrl, fileName, instancePath, contentType);
        engine->deleteLater();
        return;
    }

    engine->downloadFile({versionFile.downloadUrl}, destPath,
        [this, engine, taskId, fileName, modInfo, config](bool success)
        {
            if (success)
            {
                DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::Completed, 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Completed, tr("下载完成"));
                NotificationManager::showSuccess(this, tr("%1 \"%2\" 下载完成").arg(config.displayName, modInfo.name));
            }
            else
            {
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Failed, tr("下载失败"));
                NotificationManager::showError(this, tr("%1 \"%2\" 下载失败").arg(config.displayName, modInfo.name));
            }
            engine->deleteLater();
        });
}

void MainWindow::onCardDownloadRequested(const ModInfo &modInfo, ContentType contentType, int depth)
{
    // 根节点：重置已访问集合，开启一次全新的"一键下载"流程
    if (depth == 0)
        m_cardDownloadVisited.clear();

    // 防止循环依赖导致无限递归
    QString visitKey = modInfo.source + ":" + modInfo.id;
    if (visitKey.isEmpty() || m_cardDownloadVisited.contains(visitKey))
        return;
    m_cardDownloadVisited.insert(visitKey);

    if (depth > kMaxCardDownloadDepth)
        return;

    if (m_currentInstancePath.isEmpty())
    {
        NotificationManager::showError(this, tr("请先选择一个实例"));
        return;
    }

    // 卡片搜索结果不含 versionFiles：需要先拉取详情才能拿到最新版本和前置列表
    if (modInfo.versionFiles.isEmpty())
    {
        if (modInfo.source.isEmpty() || modInfo.id.isEmpty())
        {
            NotificationManager::showError(this,
                tr("资源 %1 信息不完整，无法获取版本").arg(modInfo.name));
            return;
        }

        if (modInfo.source == QStringLiteral("modrinth"))
        {
            ModrinthAPI *api = new ModrinthAPI(this);
            QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
            bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
            api->setBaseUrl(useMcim
                ? QStringLiteral("https://mod.mcimirror.top/modrinth")
                : QStringLiteral("https://api.modrinth.com"));

            connect(api, &ModrinthAPI::modDetailReceived, this,
                [this, api, contentType, depth, modInfo](const ModInfo &detail)
            {
                ModInfo merged = detail;
                if (merged.name.isEmpty())
                    merged.name = modInfo.name;
                api->deleteLater();
                // 复用同深度（详情拉取属于"补全当前节点信息"）
                onCardDownloadRequested(merged, contentType, depth);
            });
            connect(api, &ModrinthAPI::modDetailFailed, this,
                [this, api, modInfo](const QString &err)
            {
                NotificationManager::showError(this,
                    tr("获取 %1 详情失败: %2").arg(modInfo.name, err));
                api->deleteLater();
            });
            api->fetchModDetail(modInfo.id);
        }
        else if (modInfo.source == QStringLiteral("curseforge"))
        {
            CurseForgeAPI *api = new CurseForgeAPI(this);
            QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
            if (!apiKey.isEmpty())
                api->setApiKey(apiKey);
            QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
            bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
            api->setBaseUrl(useMcim
                ? QStringLiteral("https://mod.mcimirror.top/curseforge")
                : QStringLiteral("https://api.curseforge.com"));

            connect(api, &CurseForgeAPI::modDetailReceived, this,
                [this, api, contentType, depth, modInfo](const ModInfo &detail)
            {
                ModInfo merged = detail;
                if (merged.name.isEmpty())
                    merged.name = modInfo.name;
                api->deleteLater();
                onCardDownloadRequested(merged, contentType, depth);
            });
            connect(api, &CurseForgeAPI::modDetailFailed, this,
                [this, api, modInfo](const QString &err)
            {
                NotificationManager::showError(this,
                    tr("获取 %1 详情失败: %2").arg(modInfo.name, err));
                api->deleteLater();
            });
            api->fetchModDetail(modInfo.id);
        }
        else
        {
            NotificationManager::showError(this,
                tr("不支持的资源来源：%1").arg(modInfo.source));
        }
        return;
    }

    // 已具备 versionFiles：取首个（按发布时间倒序）即最新版本
    ModVersionFile latest = modInfo.versionFiles.first();

    // 校验下载链接
    if (latest.downloadUrl.isEmpty())
    {
        NotificationManager::showError(this,
            tr("%1 最新版本没有可用的下载链接").arg(modInfo.name));
        return;
    }

    if (depth == 0)
    {
        int depCount = 0;
        for (const ModDependency &d : latest.dependencies)
            if (d.isRequired) ++depCount;
        NotificationManager::showInfo(this,
            tr("开始下载 %1（含 %2 个必备前置）").arg(modInfo.name).arg(depCount));
    }
    else
    {
        NotificationManager::showInfo(this,
            tr("下载前置：%1").arg(modInfo.name));
    }

    // 触发主资源下载（复用现有完整流程：任务栏、进度、通知等）
    onContentDownloadRequested(modInfo, latest, contentType);

    // 递归下载必备前置
    for (const ModDependency &dep : latest.dependencies)
    {
        if (!dep.isRequired)
            continue;
        if (dep.name.isEmpty())
            continue;

        ModInfo depInfo;
        depInfo.id = dep.name;
        depInfo.source = modInfo.source;
        depInfo.name = tr("前置: %1").arg(dep.name);
        onCardDownloadRequested(depInfo, contentType, depth + 1);
    }
}

void MainWindow::onClipboardLinkDetected(const ClipboardLinkInfo &info)
{
    // 防重入：确认框 exec() 的嵌套事件循环期间，剪贴板若再次变化会
    // 递归叠加确认框导致点击结果异常，直接忽略重复触发。
    if (m_clipboardDialogOpen)
        return;
    m_clipboardDialogOpen = true;

    // 剪贴板变化通常发生在其它应用（浏览器等）中，软件此时可能在后台
    // 甚至最小化。先把主窗口带到前台，否则弹出框无法获得焦点而不交互。
    if (isMinimized())
        showNormal();
    show();
    raise();
    activateWindow();

    ContentTypeConfig config = ContentTypeConfig::getConfig(info.contentType);
    QString platformLabel = info.displayName;
    QString typeLabel = config.displayName;

    QString message = tr("检测到 %1 %2 链接，是否使用方块盒子打开？")
        .arg(platformLabel, typeLabel);

    AppMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("方块盒子"));
    msgBox.setText(message);
    msgBox.setStandardButtons(AppMessageBox::Yes | AppMessageBox::No);
    msgBox.setDefaultButton(AppMessageBox::Yes);

    const int result = msgBox.exec();
    m_clipboardDialogOpen = false;

    if (result == AppMessageBox::Yes)
    {
        ModInfo modInfo;
        modInfo.source = info.source;
        modInfo.id = info.slug;
        modInfo.name = info.slug;

        if (info.contentType == ContentType::Mod)
        {
            showModDetailPage(modInfo);
        }
        else
        {
            showContentDetailPage(modInfo, info.contentType);
        }
    }
    else
    {
        // 记录已拒绝的链接，避免重复弹窗
        m_clipboardMonitor->addRejected(QApplication::clipboard()->text().trimmed());
    }
}

void MainWindow::reloadShortcuts()
{
  // 清除旧快捷键
  for (QShortcut *sc : m_shortcuts)
  {
    delete sc;
  }
  m_shortcuts.clear();

  // 默认按键绑定
  struct DefaultBinding
  {
    QString id;
    QString defaultKey;
  };

  QVector<DefaultBinding> defaults = {
    {"nav_home",        "Ctrl+1"},
    {"nav_resources",   "Ctrl+2"},
    {"nav_settings",    "Ctrl+3"},
    {"nav_ai",          "Ctrl+4"},
    {"search",          "Ctrl+F"},
    {"tasks",           "Ctrl+T"},
    {"instance_select", "Ctrl+E"},
    {"back",            "Esc"},
    {"home",            "Home"},
    {"toggle_maximize", "F11"},
    {"quit",            "Ctrl+Q"},
    {"launch",          "Ctrl+L"},
    {"mod_download",    "Ctrl+D"},
    {"install_instance","Ctrl+I"},
    {"instance_manage", "Ctrl+M"},
    {"refresh",         "F5"},
  };

  // 从设置加载自定义绑定
  QMap<QString, QString> customBindings;
  QString jsonStr = SettingsManager::instance()->getProperty("keyBindings").toString();
  if (!jsonStr.isEmpty())
  {
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (doc.isArray())
    {
      QJsonArray arr = doc.array();
      for (const auto &val : arr)
      {
        QJsonObject obj = val.toObject();
        customBindings[obj["id"].toString()] = obj["key"].toString();
      }
    }
  }

  // 为每个 action 创建快捷键
  for (const auto &d : defaults)
  {
    QString key = customBindings.value(d.id, d.defaultKey);
    if (key.isEmpty())
    {
      continue;
    }

    QShortcut *sc = new QShortcut(QKeySequence(key), this);

    if (d.id == "nav_home")
    {
      connect(sc, &QShortcut::activated, this, [this]() { onParentNavClicked(0); });
    }
    else if (d.id == "nav_resources")
    {
      connect(sc, &QShortcut::activated, this, [this]() { onParentNavClicked(1); });
    }
    else if (d.id == "nav_settings")
    {
      connect(sc, &QShortcut::activated, this, [this]() { onParentNavClicked(2); });
    }
    else if (d.id == "nav_ai")
    {
      connect(sc, &QShortcut::activated, this, [this]() { onParentNavClicked(3); });
    }
    else if (d.id == "search")
    {
      connect(sc, &QShortcut::activated, this, &MainWindow::onSearchClicked);
    }
    else if (d.id == "tasks")
    {
      connect(sc, &QShortcut::activated, this, &MainWindow::onShowTaskListPageRequested);
    }
    else if (d.id == "instance_select")
    {
      connect(sc, &QShortcut::activated, this, &MainWindow::onInstanceSelectClicked);
    }
    else if (d.id == "back")
    {
      connect(sc, &QShortcut::activated, this, &MainWindow::onTopBarBackClicked);
    }
    else if (d.id == "home")
    {
      connect(sc, &QShortcut::activated, this, &MainWindow::onBackToMain);
    }
    else if (d.id == "toggle_maximize")
    {
      connect(sc, &QShortcut::activated, this, &MainWindow::toggleMaximized);
    }
    else if (d.id == "quit")
    {
      connect(sc, &QShortcut::activated, this, &MainWindow::close);
    }
    else if (d.id == "launch")
    {
      connect(sc, &QShortcut::activated, this, qOverload<>(&MainWindow::onLaunchGameClicked));
    }
    else if (d.id == "mod_download")
    {
      connect(sc, &QShortcut::activated, this, [this]() { showModDownloadPage(); });
    }
    else if (d.id == "install_instance")
    {
      connect(sc, &QShortcut::activated, this, [this]() {
        ensurePageInitialized(PageIndex::InstallInstancePage);
        setSideBarVisible(true);
        m_topBar->setTitle(tr("资源>安装新实例"));
        animatedSwitchToPage(PageIndex::InstallInstancePage);
      });
    }
    else if (d.id == "instance_manage")
    {
      connect(sc, &QShortcut::activated, this, &MainWindow::onInstanceSettingsClicked);
    }
    else if (d.id == "refresh")
    {
      connect(sc, &QShortcut::activated, this, &MainWindow::onRefreshClicked);
    }

    m_shortcuts.append(sc);
  }
}

void MainWindow::setShortcutsEnabled(bool enabled)
{
  for (QShortcut *sc : m_shortcuts)
  {
    sc->setEnabled(enabled);
  }
}


