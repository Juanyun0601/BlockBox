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
#include <QScrollArea>
#include <QUrl>

#include "utils/PageTransitionAnimator.h"
#include "utils/ContentAnimator.h"
#include "components/NotificationManager.h"
#include "components/SideBar.h"
#include "components/SubNavPanel.h"
#include "components/TaskBar.h"
#include "components/TopBar.h"
#include "utils/GameLauncher.h"
#include "pages/AiChatPage.h"
#include "pages/JavaDownloadPage.h"
#include "pages/AccountManagePage.h"
#include "pages/ForgeVersionListPage.h"
#include "pages/HomePage.h"
#include "pages/InstallInstancePage.h"
#include "pages/InstanceManagePage.h"
#include "pages/InstanceSelectPage.h"
#include "pages/LaunchDetailsPage.h"
#include "pages/LoaderDetailPage.h"
#include "pages/ResourcesPage.h"
#include "pages/SettingsPage.h"
#include "pages/TaskDetailPage.h"
#include "pages/ModDetailPage.h"
#include "pages/LocalModDetailPage.h"
#include "pages/ModDownloadPage.h"
#include "pages/ContentDownloadPage.h"
#include "pages/PluginPage.h"
#include "pages/ModpackImportPage.h"
#include "pages/ModpackExportPage.h"
#include "pages/SearchPage.h"
#include "pages/SkinEditorPage.h"
#include "pages/TaskListPage.h"
#include "utils/PerformanceMonitor.h"
#include "utils/SettingsManager.h"
#include "utils/VersionDownloader.h"
#include "utils/mod/ModData.h"

void MainWindow::initPages()
{
    PerformanceMonitor::instance()->startMeasurement("Pages Initialization");
    initEssentialPages();
    PerformanceMonitor::instance()->endMeasurement("Pages Initialization");
}

void MainWindow::refreshHomeRecentPlays()
{
    if (m_homePage) {
        m_homePage->refreshRecentPlays();
    }
}

void MainWindow::initEssentialPages()
{
    PerformanceMonitor::instance()->startMeasurement("Home Page Initialization");
    QScrollArea *scrollArea1 = new QScrollArea(this);
    scrollArea1->setWidgetResizable(true);
    scrollArea1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scrollArea1->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *homePage = new HomePage(this);
    m_homePage = homePage;
    scrollArea1->setWidget(homePage);
    m_stackedWidget->addWidget(scrollArea1);

    // 连接最近游玩信号
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
        m_instanceManagePage->setCurrentTab(10);
        m_subNavPanel->setSelectedChild(10);
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
    // 首页账号卡片 / 轮播右侧皮肤区「添加首个账户」→ 账户管理页
    connect(homePage, &HomePage::accountManageRequested, this, &MainWindow::showAccountManagePage);
    connect(homePage, &HomePage::recentPlayCopyNameClicked, this, [this](const QString &name) {
        // 复制存档名到剪贴板
        if (!name.isEmpty()) {
            QGuiApplication::clipboard()->setText(name);
        }
    });

    PerformanceMonitor::instance()->endMeasurement("Home Page Initialization");

    PerformanceMonitor::instance()->startMeasurement("Resources Page Initialization");
    m_resourcesPage = new ResourcesPage(this);
    m_stackedWidget->addWidget(m_resourcesPage);
    m_resourcesPage->setInstancePath(m_currentInstancePath);
    // 卡片点击直接转发到左侧导航项处理，行为与点击侧边栏完全一致
    connect(m_resourcesPage, &ResourcesPage::navItemClicked, this, [this](int childIndex) {
        onChildNavClicked(1, childIndex);
    });
    PerformanceMonitor::instance()->endMeasurement("Resources Page Initialization");

    PerformanceMonitor::instance()->startMeasurement("Settings Page Initialization");
    QScrollArea *scrollArea3 = new QScrollArea(this);
    scrollArea3->setWidgetResizable(true);
    auto *settingsPage = new SettingsPage(this);
    scrollArea3->setWidget(settingsPage);
    m_stackedWidget->addWidget(scrollArea3);
    connect(settingsPage, &SettingsPage::downloadJavaRequested, this, &MainWindow::showJavaDownloadPage);
    PerformanceMonitor::instance()->endMeasurement("Settings Page Initialization");

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
    connect(m_instanceManagePage, &InstanceManagePage::launchGameRequested,
            this, qOverload<>(&MainWindow::onLaunchGameClicked));
    connect(m_instanceManagePage, &InstanceManagePage::quickLaunchSaveRequested,
            this, &MainWindow::onQuickLaunchSaveClicked);
    connect(m_instanceManagePage, &InstanceManagePage::quickLaunchServerRequested,
            this, &MainWindow::onQuickLaunchServerClicked);
    connect(m_instanceManagePage, &InstanceManagePage::saveSettingsOpened,
            this, &MainWindow::onSaveSettingsOpened);
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

    // 整合包导出页面占位
    QWidget *placeholderModpackExport = new QWidget(this);
    m_stackedWidget->addWidget(placeholderModpackExport);

    // 本地模组详情页
    m_localModDetailPage = new LocalModDetailPage(this);
    connect(m_localModDetailPage, &LocalModDetailPage::backRequested, this, [this]() {
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstanceManagePage));
        setSideBarVisible(true);
        m_topBar->setTitle(tr("实例管理"));
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    });
    connect(m_localModDetailPage, &LocalModDetailPage::remoteDetailRequested,
            this, [this](const ModInfo &info) {
        // 跳转到内置在线详情页
        showModDetailPage(info);
        // 重设返回按钮：从本地详情跳转来的，应返回实例管理页
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, [this]() {
            m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::LocalModDetailPageIndex));
            setSideBarVisible(false);
            m_topBar->setTitle(tr("模组详情"));
            disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
            connect(m_topBar, &TopBar::backClicked, this, [this]() {
                m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstanceManagePage));
                setSideBarVisible(false);
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
    default:
        break;
    }
}


void MainWindow::initModDownloadPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ModDownloadPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Mod Download Page Initialization");
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
        showModDetailPage(info);
    });
    connect(m_modDownloadPage, &ModDownloadPage::instanceSelectionChanged,
            this, &MainWindow::applyInstanceContext);
    m_pageInitialized[PageIndex::ModDownloadPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Mod Download Page Initialization");
}

void MainWindow::initInstanceSelectPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::InstanceSelectPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Instance Select Page Initialization");
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
        setSideBarVisible(true);
        m_topBar->setTitle(tr("资源>安装新实例"));
        animatedSwitchToPage(PageIndex::InstallInstancePage);
    });
    connect(m_instanceSelectPage, &InstanceSelectPage::downloadModpackRequested, this, [this]() {
        showContentDownloadPage(ContentType::Modpack);
    });
    connect(m_instanceSelectPage, &InstanceSelectPage::importModpackRequested, this, &MainWindow::showModpackImportPage);
    connect(m_instanceSelectPage, &InstanceSelectPage::exportModpackRequested, this, &MainWindow::showModpackExportPage);
    m_pageInitialized[PageIndex::InstanceSelectPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Instance Select Page Initialization");
}

void MainWindow::initAccountManagePage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::AccountManagePage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Account Manage Page Initialization");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::AccountManagePage));
    m_accountManagePage = new AccountManagePage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::AccountManagePage), m_accountManagePage);
    delete placeholder;
    connect(m_accountManagePage, &AccountManagePage::backToMainRequested, this, &MainWindow::onBackToMain);
    connect(m_accountManagePage, &AccountManagePage::accountSelected, this, [this](const QString &accountName) {
        m_topBar->setAccountName(accountName);
    });
    // 账户皮肤加载后，将皮肤头部裁剪头像同步到顶栏账户头像
    connect(m_accountManagePage, &AccountManagePage::accountSkinUpdated, this, [this](const QPixmap &avatar) {
        m_topBar->setAccountAvatar(avatar);
    });
    // 账户管理页左侧"皮肤制作"按钮 → 进入皮肤编辑页
    connect(m_accountManagePage, &AccountManagePage::skinEditorRequested, this, &MainWindow::onSkinEditorClicked);
    // 面包屑跟随页面：账户管理 / 添加账户
    connect(m_accountManagePage, &AccountManagePage::accountManagePageOpened,
            this, &MainWindow::onAccountManagePageOpened);
    connect(m_accountManagePage, &AccountManagePage::addAccountPageOpened,
            this, &MainWindow::onAddAccountPageOpened);
    // 若皮肤编辑页已先于账户页初始化，补建皮肤应用 → 账户刷新的信号连接
    if (m_skinEditorPage)
    {
        connect(m_skinEditorPage, &SkinEditorPage::skinAppliedToAccount,
                m_accountManagePage, &AccountManagePage::refreshSkinForAccount);
    }
    m_pageInitialized[PageIndex::AccountManagePage] = true;
    PerformanceMonitor::instance()->endMeasurement("Account Manage Page Initialization");
}

void MainWindow::initLaunchDetailsPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::LaunchDetailsPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Launch Details Page Initialization");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::LaunchDetailsPage));
    m_launchDetailsPage = new LaunchDetailsPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::LaunchDetailsPage), m_launchDetailsPage);
    delete placeholder;
    m_pageInitialized[PageIndex::LaunchDetailsPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Launch Details Page Initialization");
}

void MainWindow::initInstallInstancePage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::InstallInstancePage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Install Instance Page Initialization");
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
    PerformanceMonitor::instance()->endMeasurement("Install Instance Page Initialization");
}

void MainWindow::initLoaderDetailPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::LoaderDetailPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Loader Detail Page Initialization");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::LoaderDetailPage));
    m_loaderDetailPage = new LoaderDetailPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::LoaderDetailPage), m_loaderDetailPage);
    delete placeholder;
    connect(m_loaderDetailPage, &LoaderDetailPage::backToInstallPageRequested, this, &MainWindow::onBackToInstallPage);
    connect(m_loaderDetailPage, &LoaderDetailPage::viewAllVersionsRequested, this, &MainWindow::onViewAllVersions);
    connect(m_loaderDetailPage, &LoaderDetailPage::loaderCardClicked, this, &MainWindow::onViewAllVersions);
    connect(m_loaderDetailPage, &LoaderDetailPage::installRequested, this, [this](const QString &instanceName, const QString &, const QMap<QString, SelectedLoader> &) {
        QString instancePath = SettingsManager::instance()->getDefaultInstancePath();
        if (!instancePath.isEmpty()) {
            instancePath += "/" + instanceName;
            updateCurrentInstance(instancePath);
        }
    });
    connect(m_loaderDetailPage, &LoaderDetailPage::instanceModified, this, &MainWindow::onInstanceModified);
    m_pageInitialized[PageIndex::LoaderDetailPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Loader Detail Page Initialization");
}

void MainWindow::initForgeVersionListPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ForgeVersionListPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Forge Version List Page Initialization");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::ForgeVersionListPage));
    m_forgeVersionListPage = new ForgeVersionListPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::ForgeVersionListPage), m_forgeVersionListPage);
    delete placeholder;
    connect(m_forgeVersionListPage, &ForgeVersionListPage::backToLoaderDetailRequested, this, &MainWindow::onBackToLoaderDetail);
    connect(m_forgeVersionListPage, &ForgeVersionListPage::forgeVersionSelected, this, &MainWindow::onForgeVersionSelected);
    m_pageInitialized[PageIndex::ForgeVersionListPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Forge Version List Page Initialization");
}

void MainWindow::initTaskListPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::TaskListPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Task List Page Initialization");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::TaskListPage));
    m_taskListPage = new TaskListPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::TaskListPage), m_taskListPage);
    delete placeholder;
    connect(m_taskListPage, &TaskListPage::backToMainRequested, this, &MainWindow::onTaskListPageBack);
    connect(m_taskListPage, &TaskListPage::taskDetailRequested, this, &MainWindow::onTaskDetailRequested);
    m_pageInitialized[PageIndex::TaskListPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Task List Page Initialization");
}

void MainWindow::initTaskDetailPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::TaskDetailPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Task Detail Page Initialization");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::TaskDetailPage));
    m_taskDetailPage = new TaskDetailPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::TaskDetailPage), m_taskDetailPage);
    delete placeholder;
    connect(m_taskDetailPage, &TaskDetailPage::backToTaskListRequested, this, &MainWindow::onTaskDetailPageBack);
    connect(m_taskDetailPage, &TaskDetailPage::taskCancelled, this, &MainWindow::onTaskCancelled);
    m_pageInitialized[PageIndex::TaskDetailPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Task Detail Page Initialization");
}


void MainWindow::initModDetailPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ModDetailPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Mod Detail Page Initialization");
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
    // Connect MC百科 URL resolution to detail page
    connect(m_modDownloadPage, &ModDownloadPage::modMcmodUrlResolved,
            m_modDetailPage, [this](const QString & /*lookupKey*/, const QString &url) {
        // Check if the resolved mod matches the currently displayed one
        // The lookupKey format is "source:id"
        if (m_modDetailPage && !url.isEmpty()) {
            m_modDetailPage->setMcmodUrl(url);
        }
    });
    m_pageInitialized[PageIndex::ModDetailPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Mod Detail Page Initialization");
}

void MainWindow::showModDetailPage(const ModInfo &modInfo)
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
    m_topBar->setTranslateButtonVisible(true);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, [this]() {
        showModDownloadPage();
    });
    if (m_modDetailPage) {
        m_modDetailPage->setModInfo(modInfo);
        m_modDetailPage->setInstanceFilter(m_currentInstanceVersion, m_currentInstanceLoader);
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
        setSideBarVisible(true);
        m_topBar->setTitle(tr("实例管理"));
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    });
    if (m_localModDetailPage) {
        m_localModDetailPage->setModInfo(modInfo);
    }
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::LocalModDetailPageIndex));
}

void MainWindow::showModDownloadPage()
{
    ensurePageInitialized(PageIndex::ModDownloadPage);
    setSideBarVisible(true);
    m_topBar->setTitle(tr("资源>模组"));
    m_topBar->setTranslateButtonVisible(true);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    if (m_modDownloadPage) {
        m_modDownloadPage->setCurrentInstancePath(m_currentInstancePath);
        m_modDownloadPage->loadInstances();
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
    PerformanceMonitor::instance()->startMeasurement("Modpack Import Page Initialization");
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
    PerformanceMonitor::instance()->endMeasurement("Modpack Import Page Initialization");
}

void MainWindow::showModpackImportPage()
{
    ensurePageInitialized(PageIndex::ModpackImportPage);
    setSideBarVisible(true);
    m_topBar->setTitle(tr("资源>导入整合包"));
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::ModpackImportPage));
}

void MainWindow::initSearchPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::SearchPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Search Page Initialization");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::SearchPage));
    m_searchPage = new SearchPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::SearchPage), m_searchPage);
    delete placeholder;

    connect(m_searchPage, &SearchPage::navigateToInstance, this, [this](const QString &path) {
        onInstanceSelected(path);
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstanceSelectPage));
    });
    connect(m_searchPage, &SearchPage::navigateToSettings, this, [this](int tabIndex) {
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::SettingsPage));
        m_subNavPanel->showForParent(2);
        onSettingsNavItemClicked(tabIndex);
    });
    connect(m_searchPage, &SearchPage::navigateToResource, this, [this](ContentType type) {
        showContentDownloadPage(type);
    });
    connect(m_searchPage, &SearchPage::navigateToAiChat, this, [this]() {
        ensurePageInitialized(PageIndex::AiChatPage);
        setSideBarVisible(false);
        m_topBar->setTitle(tr("AI 助手"));
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::AiChatPage));
    });
    connect(m_searchPage, &SearchPage::navigateToResourceDetail, this, [this](const ModInfo &info, ContentType contentType) {
        showContentDetailPage(info, contentType);
    });
    connect(m_searchPage, &SearchPage::navigateToFavorites, this, [this]() {
        showFavoritesPage(QString());
    });
    connect(m_searchPage, &SearchPage::navigateToTaskList, this, [this]() {
        showTaskListPage();
    });
    connect(m_searchPage, &SearchPage::navigateToInstallInstance, this, [this]() {
        // 进入资源页安装新实例（等价于点击资源页左侧导航"安装新实例"）
        m_sideBar->setSelectedIndex(1);
        m_subNavPanel->showForParent(1);
        m_subNavPanel->setSelectedChild(0);
        ensurePageInitialized(PageIndex::InstallInstancePage);
        setSideBarVisible(true);
        m_topBar->setTitle(tr("资源>安装新实例"));
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::InstallInstancePage));
    });
    connect(m_searchPage, &SearchPage::navigateToModpackImport, this, [this]() {
        // 进入资源页导入整合包
        m_sideBar->setSelectedIndex(1);
        m_subNavPanel->showForParent(1);
        m_subNavPanel->setSelectedChild(2);
        showModpackImportPage();
    });

    m_pageInitialized[PageIndex::SearchPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Search Page Initialization");
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

    // 根据来源页面自动选中对应的搜索筛选器：
    // 资源页(1)→资源筛选(2)，设置页(2)→设置筛选(3)，AI助手页(3)→AI助手筛选(4)，首页(0)→全部(0)
    int targetFilter = 0;
    switch (m_lastSidebarIndex)
    {
    case 0: targetFilter = 0; break; // 首页 → 全部
    case 1: targetFilter = 2; break; // 资源 → 资源
    case 2: targetFilter = 3; break; // 设置 → 设置
    case 3: targetFilter = 4; break; // AI 助手 → AI 助手
    default: targetFilter = 0; break;
    }
    m_searchPage->setFilter(targetFilter);

    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::SearchPage));
    m_searchPage->setSearchFocus();
}

void MainWindow::initModpackExportPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::ModpackExportPageIndex]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Modpack Export Page Initialization");
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
    PerformanceMonitor::instance()->endMeasurement("Modpack Export Page Initialization");
}

void MainWindow::showModpackExportPage(const QString& instancePath)
{
    ensurePageInitialized(PageIndex::ModpackExportPageIndex);
    setSideBarVisible(true);
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
    PerformanceMonitor::instance()->startMeasurement("AI Chat Page Initialization");
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
    PerformanceMonitor::instance()->endMeasurement("AI Chat Page Initialization");
}

void MainWindow::initJavaDownloadPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::JavaDownloadPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Java Download Page Initialization");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::JavaDownloadPage));
    m_javaDownloadPage = new JavaDownloadPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::JavaDownloadPage), m_javaDownloadPage);
    delete placeholder;
    connect(m_javaDownloadPage, &JavaDownloadPage::backToSettingsRequested, this, [this]()
    {
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::SettingsPage));
        setSideBarVisible(true);
        m_topBar->setTitle(tr("设置"));
        disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
        connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    });
    m_pageInitialized[PageIndex::JavaDownloadPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Java Download Page Initialization");
}

void MainWindow::showJavaDownloadPage()
{
    ensurePageInitialized(PageIndex::JavaDownloadPage);
    setSideBarVisible(false);
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, [this]()
    {
        m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::SettingsPage));
        setSideBarVisible(true);
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
    PerformanceMonitor::instance()->startMeasurement("Skin Editor Page Initialization");
    QWidget *placeholder = m_stackedWidget->widget(static_cast<int>(PageIndex::SkinEditorPage));
    m_skinEditorPage = new SkinEditorPage(this);
    m_stackedWidget->removeWidget(placeholder);
    m_stackedWidget->insertWidget(static_cast<int>(PageIndex::SkinEditorPage), m_skinEditorPage);
    delete placeholder;
    connect(m_skinEditorPage, &SkinEditorPage::backToMainRequested, this, &MainWindow::onBackToMain);
    // 应用皮肤到账户后，刷新账户管理页的皮肤显示（若账户页已初始化）
    if (m_accountManagePage)
    {
        connect(m_skinEditorPage, &SkinEditorPage::skinAppliedToAccount,
                m_accountManagePage, &AccountManagePage::refreshSkinForAccount);
    }
    m_pageInitialized[PageIndex::SkinEditorPage] = true;
    PerformanceMonitor::instance()->endMeasurement("Skin Editor Page Initialization");
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
    // 将当前选中账户信息传入皮肤编辑页，支持"应用到账户"功能
    if (m_accountManagePage)
        m_skinEditorPage->applyToAccount(m_accountManagePage->currentAccountName(),
                                         m_accountManagePage->currentAccountType());
}

void MainWindow::initPluginPage()
{
    QMutexLocker locker(&m_initMutex);
    if (m_pageInitialized[PageIndex::PluginPage]) { return; }
    PerformanceMonitor::instance()->startMeasurement("Plugin Page Initialization");
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
    PerformanceMonitor::instance()->endMeasurement("Plugin Page Initialization");
}
