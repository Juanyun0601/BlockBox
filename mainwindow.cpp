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
#include <QFileDialog>
#include "utils/Safety.h"
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QTimer>
#include <QFrame>
#include <QSplitter>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QFile>
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
#include <QInputMethod>
#include <QUrl>
#include <QUuid>

#include "components/BackgroundWidget.h"
#include "components/AppDialogBase.h"
#include "components/NotificationManager.h"
#include "components/SideBar.h"
#include "components/SubNavPanel.h"
#include "components/TaskBar.h"
#include "components/InstanceAssistantWindow.h"
#include "components/GameFloatingIcon.h"
#include "components/TouchControlOverlay.h"
#include "components/TopBar.h"
#include "components/UpdateProgressButton.h"
#include "components/FileDropOverlay.h"
#include "components/HumanCheckDialog.h"
#include "components/SystemTray.h"
#include "components/LobbyJoinPrepDialog.h"
#include "components/ResourceSyncListWindow.h"
#include "utils/ResourceVersionSyncer.h"
#include "utils/Schematic/SchematicDownloadTarget.h"
#include "utils/mcp/McpServer.h"
#include "utils/AiService.h"
#include "utils/Lobby/LobbyInstanceMatcher.h"
#include "utils/Lobby/LobbyShareLink.h"
#include "utils/BackgroundManager.h"
#include "utils/BingWallpaperManager.h"
#include "utils/CrashReporter.h"
#include "utils/PanoramaThemeManager.h"
#include "utils/Telemetry.h"
#include "utils/ThemeManager.h"
#include "pages/CreationCenterPage.h"
#include "pages/ForumAccountPage.h"
#include "pages/ForumThreadDetailPage.h"
#include "pages/AddAccountPage.h"
#include "pages/ForgeVersionListPage.h"
#include "pages/GameLaunchPage.h"
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
#include "pages/BedrockContentPage.h"
#include "pages/BedrockContentDetailPage.h"
#include "pages/BedrockDownloadPage.h"
#include "pages/BedrockVersionDetailPage.h"
#include "pages/BedrockInstanceSelectPage.h"
#include "components/BedrockInstanceAssistantWindow.h"
#include "utils/BedrockLauncher.h"
#include "utils/bedrock/BedrockInstanceManager.h"
#include "utils/bedrock/BedrockContentInstaller.h"
#include "utils/FileImportRouter.h"
#include "pages/InstanceManagePage.h"
#include "pages/InstanceLogPage.h"
#include "pages/SearchPage.h"
#include "pages/AiChatPage.h"
#include "pages/FavoritesPage.h"
#include "pages/SkinEditorPage.h"
#include "pages/CommunitySkinDetailPage.h"
#include "pages/PluginPage.h"
#include "pages/ForumSchematicListPage.h"
#include "pages/CommunityForumPage.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "components/CreatePluginDialog.h"
#include "utils/plugin/PluginManager.h"
#include "utils/plugin/PluginSafetyGuard.h"
#include "components/FavoriteFolderDialog.h"
#include "utils/GameLauncher.h"
#include "utils/GameAchievementService.h"
#include "utils/PlaytimeStats.h"
#include "utils/WorldBackupManager.h"
#include "utils/LanguageManager.h"
#include "utils/PerformanceMonitor.h"
#include "platform.h"
#include "utils/SettingsManager.h"
#include "utils/McimHelper.h"
#include "utils/SkinDownloader.h"
#include "utils/TouchScroll.h"
#include "utils/net/NetJob.h"
#include "utils/net/Download.h"
#include "utils/net/ChecksumValidator.h"
#include "utils/net/NetworkAccess.h"
#include "utils/content/ContentDownloader.h"
#include "utils/modpack/ModpackDetector.h"
#include "utils/modpack/ModpackImporter.h"
#include "components/ProgressDialog.h"
#include "utils/ClipboardMonitor.h"
#include "utils/links/LinkRouter.h"
#include "utils/PageTransitionAnimator.h"
#include "utils/ContentAnimator.h"
#include "utils/UpdateChecker.h"
#include "utils/trust/BuildChannel.h"
#include "utils/Terracotta/TerracottaClient.h"

#include <QPainter>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>

namespace {
// MultiMC net 层：单文件资源下载（镜像回退 + SHA1 校验 + .part 原子落盘）
void startResourceDownload(QObject *context, const QStringList &candidateUrls,
                           const QString &destPath, const QString &expectedSha1,
                           const std::function<void(bool)> &onDone,
                           const std::function<void(qint64, qint64)> &onProgress)
{
    auto dl = Net::Download::makeFile(QUrl(candidateUrls.first()), destPath);
    for (int i = 1; i < candidateUrls.size(); ++i)
        dl->addUrlFallback(QUrl(candidateUrls.at(i)));
    if (!expectedSha1.isEmpty())
    {
        dl->addValidator(new Net::ChecksumValidator(
            QCryptographicHash::Sha1, QByteArray::fromHex(expectedSha1.toLatin1())));
    }

    auto job = NetJob::Ptr(new NetJob(QFileInfo(destPath).fileName(),
                                      Net::networkAccessManager()));
    job->addNetAction(dl);

    if (onProgress)
    {
        QObject::connect(dl.get(), &NetAction::netActionProgress, context,
                         [onProgress](int, qint64 received, qint64 total) { onProgress(received, total); });
    }
    QObject::connect(job.get(), &Task::finished, context, [job, onDone]() {
        if (onDone)
            onDone(job->wasSuccessful());
    }, Qt::SingleShotConnection);
    job->start();
}

/**
 * @brief 为在线整合包挑一个没被占用的实例名
 *
 * 实例列表只认 <游戏根>/versions/<实例名>/<实例名>.json，重名会直接装不进去，
 * 所以重名时依次追加 (2)、(3)…，让同一个整合包装多份也能成功。
 */
QString uniqueInstanceName(const QString &gameRoot, const QString &preferred)
{
    QString base = Safety::safeFileName(preferred.trimmed(), QString());
    if (base.isEmpty())
        base = QStringLiteral("整合包");

    const QString versionsDir = gameRoot + QStringLiteral("/versions");
    QString candidate = base;
    int suffix = 2;
    while (QFile::exists(versionsDir + QLatin1Char('/') + candidate
                         + QLatin1Char('/') + candidate + QStringLiteral(".json"))) {
        candidate = QStringLiteral("%1 (%2)").arg(base).arg(suffix++);
        if (suffix > 999)
            break;
    }
    return candidate;
}

// 联机大厅地址（host:port / http(s)://host:port）→ host + 端口，默认 25565
bool parseLobbyAddress(const QString &text, QString *host, quint16 *port)
{
    QString value = text.trimmed();
    if (value.isEmpty())
        return false;
    if (value.startsWith(QStringLiteral("http://")))
        value.remove(0, 7);
    else if (value.startsWith(QStringLiteral("https://")))
        value.remove(0, 8);
    while (value.endsWith(QLatin1Char('/')))
        value.chop(1);

    quint16 parsedPort = 25565;
    QString parsedHost = value;
    const int colon = value.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && value.indexOf(QLatin1Char(':')) == colon)
    {
        bool ok = false;
        const int candidate = value.mid(colon + 1).toInt(&ok);
        if (ok && candidate > 0 && candidate <= 65535)
        {
            parsedPort = quint16(candidate);
            parsedHost = value.left(colon);
        }
    }

    if (parsedHost.isEmpty())
        return false;
    if (host)
        *host = parsedHost;
    if (port)
        *port = parsedPort;
    return true;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_instanceSelectPage(nullptr)
    , m_overlayAssistantWindow(nullptr)
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
    , m_lastChildSidebarIndex{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
{
    ui->setupUi(this);

    // 接受外部拖入的方块盒子更新安装包（setup/portable 等），松开即走安装流程
    setAcceptDrops(true);

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
    m_pageInitialized[PageIndex::CommunityPage] = false;

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
    connect(m_topBar, &TopBar::instanceAssistantClicked, this, &MainWindow::openInstanceAssistant);
    
    // Connect account manage signal
    connect(m_topBar, &TopBar::accountManageClicked, this, &MainWindow::onAccountManageClicked);
    connect(m_topBar, &TopBar::launchGameClicked, this, qOverload<>(&MainWindow::onLaunchGameClicked));
    connect(m_topBar, &TopBar::searchClicked, this, &MainWindow::onSearchClicked);
    // 手机端「扫描」：打开扫码登录电脑端的整页扫码界面（桌面端顶栏没有这枚按钮）
    connect(m_topBar, &TopBar::scanQrClicked, this, &MainWindow::showQrScanPage);
    // 顶栏「系统消息 / 聊天」快捷卡：社区为常驻功能，始终显示
    m_topBar->setForumCardVisible(true);
    connect(m_topBar, &TopBar::forumNotificationsClicked, this, &MainWindow::onForumNotificationsClicked);
    connect(m_topBar, &TopBar::forumChatClicked, this, &MainWindow::onForumChatClicked);
    // Connect game edition switch (Java版 / 基岩版)
    // 基岩版属于实验性内容：未在设置中开启时，顶栏不显示版本切换入口
    m_topBar->setEditionSwitchVisible(SettingsManager::instance()->isExperimentalBedrockEnabled());
    connect(m_topBar, &TopBar::editionSwitched, this, [this](bool javaEdition) {
        if (!SettingsManager::instance()->isExperimentalBedrockEnabled())
            return;
        m_topBar->setJavaEdition(javaEdition);
        // 版本模式同步到侧边栏子导航/首页快捷入口/收藏夹存储/资源主页
        applyBedrockEdition(!javaEdition);
        NotificationManager::showInfo(this, javaEdition ? tr("已切换到 Java 版") : tr("已切换到基岩版"));
    });
    connect(SettingsManager::instance(), &SettingsManager::experimentalBedrockChanged, this, [this](bool enabled) {
        m_topBar->setEditionSwitchVisible(enabled);
        if (!enabled && !m_topBar->isJavaEdition()) {
            m_topBar->setJavaEdition(true);
            applyBedrockEdition(false);
            NotificationManager::showInfo(this, tr("已关闭基岩版实验性功能，已切回 Java 版"));
        }
    });

    connect(m_taskBar, &TaskBar::showTaskListPageRequested, this, &MainWindow::onShowTaskListPageRequested);
    connect(m_taskBar, &TaskBar::taskDetailRequested, this, &MainWindow::onTaskDetailRequested);

    // 桌面端系统托盘：关窗收进托盘、托盘里也能打开设置或彻底退出
    setupSystemTray();

    connect(LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        ui->retranslateUi(this);
        m_topBar->setMainTitle();
    });

    connect(BackgroundManager::instance(), &BackgroundManager::backgroundChanged, this, [this]() {
        updateBackgroundWidget();
        ThemeManager::instance()->applyThemeColor();
    });

    // 深色主题下给壁纸叠暗纱，切换主题时立即跟随
    auto syncDarkScrim = [this]() {
        if (!m_backgroundWidget)
            return;
        m_backgroundWidget->setDarkScrimEnabled(
            ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme);
    };
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [syncDarkScrim](ThemeManager::ThemeType) { syncDarkScrim(); });
    syncDarkScrim();

    // 转速只是天空盒的内部参数，单独响应即可，不必重建整套样式表
    connect(BackgroundManager::instance(), &BackgroundManager::rotatingSpeedChanged, this,
            [this](int percent) {
                m_backgroundWidget->setRotatingSpeed(percent);
            });

    updateBackgroundWidget();

    // 旋转背景素材按需下载：当前主题六张图下完后立刻换成实景
    connect(PanoramaThemeManager::instance(), &PanoramaThemeManager::themeReady, this,
            [this](const QString &themeId) {
        BackgroundManager *bg = BackgroundManager::instance();
        if (!m_backgroundWidget || bg->currentMode() != BackgroundManager::Rotating
            || effectiveRotatingScene() != themeId) {
            return;
        }
        m_backgroundWidget->setPanoramaSource(themeId, PanoramaThemeManager::themeDir(themeId));
    });
    connect(PanoramaThemeManager::instance(), &PanoramaThemeManager::themeFailed, this,
            [this](const QString &themeId, const QString &reason) {
        BackgroundManager *bg = BackgroundManager::instance();
        if (bg->currentMode() == BackgroundManager::Rotating
            && effectiveRotatingScene() == themeId) {
            NotificationManager::showErrorWithNetworkCheck(this, tr("旋转背景下载失败：%1").arg(reason));
        }
    });

    // 方块盒子账户 ⇄ 顶栏账户卡片：登录后顶栏展示社区账户的头像与昵称
    auto *forumAccount = ForumAccountService::instance();
    connect(forumAccount, &ForumAccountService::userChanged, this,
            [this](const ForumUser &) { syncTopBarAccount(); });
    connect(forumAccount, &ForumAccountService::sessionExpired, this,
            [this]() { syncTopBarAccount(); });
    // 论坛开了人机验证：请求被拦下来就弹滑块，验完再继续
    connect(forumAccount, &ForumAccountService::humanCheckRequired, this,
            &MainWindow::onHumanCheckRequired);

    // 自动登录：从保存的账户文件中恢复默认账户
    restoreDefaultAccount();

    // 上次登录过方块盒子账户的话，启动后先用本地会话静默恢复登录：
    // 令牌还有效就继续用（顺带刷新昵称、头像和未读通知），
    // 已经失效交给 sessionExpired 把界面退回未登录；
    // 断网时保留上次的登录状态，不弹错误打扰用户。
    if (forumAccount->hasSession())
        forumAccount->refreshProfile();

    // 加载保存的实例路径
    loadCurrentInstancePath();

    // 基岩版实验功能：顶栏当前若停在基岩版模式，启动时恢复基岩版界面
    if (!m_topBar->isJavaEdition())
        applyBedrockEdition(true);

    // 初始化剪贴板监听
    m_clipboardMonitor = new ClipboardMonitor(this);
    connect(m_clipboardMonitor, &ClipboardMonitor::linkDetected,
            this, &MainWindow::onClipboardLinkDetected);

    // 详情页里点到方块盒子能解析的链接（帖子、一键加入、CF / Modrinth 资源页）
    // 都在软件内打开：具体跳哪个页面由这里统一登记
    setupLinkRouter();

// 初始化快捷键
    reloadShortcuts();

    // ── 本地 MCP 服务器（外部 AI 客户端接入）──
    // 默认关闭；设置页打开后才监听。工具复用 AI 助手页的 AiService，
    // 这样下载 / 改版本这类靠信号驱动的工具也能正常工作。
    m_mcpServer = new McpServer(this);
    m_mcpServer->setAiServiceProvider([this]() -> AiService * {
        ensurePageInitialized(PageIndex::AiChatPage);
        return m_aiChatPage ? m_aiChatPage->aiService() : nullptr;
    });
    connect(m_mcpServer, &McpServer::statusChanged, this,
            [this](bool running, quint16 port, const QString &detail) {
        m_mcpRunning = running;
        m_mcpRunningPort = running ? port : 0;
        m_mcpStatusDetail = detail;
        syncMcpStatusToSettingsPage();
    });
    applyMcpServerSettings();

    // 非官方构建：主窗口真正显示后提示一次风险（非阻塞，同一个进程只提示一次）。
    // 主窗口是在异步初始化结束后才 show() 的，固定延时可能早于显示，
    // 所以用短间隔定时器等 isVisible() 变真就停，提示条才会落在主窗口上。
    if (!BuildChannel::isOfficialBuild())
    {
        auto *noticeTimer = new QTimer(this);
        noticeTimer->setInterval(500);
        connect(noticeTimer, &QTimer::timeout, this, [this, noticeTimer]() {
            static bool shown = false; // 同一进程只弹一次
            if (shown || !isVisible())
                return;
            shown = true;
            noticeTimer->stop();
            noticeTimer->deleteLater();
            NotificationManager::showInfo(this, BuildChannel::riskNoticeText(), 12000);
        });
        noticeTimer->start();
    }

    // 语言包按需下载：上次选的是不随安装包内置的语言、本机又还没下好语言包时，
    // 启动后先在后台补一次，下好提示重启即可切回；失败只提示一句，不打断使用。
    // 软件升级后同一套逻辑也会把旧版本的语言包换成新版（缺的新文案才不会退回中文）。
    LanguageManager *languagePacks = LanguageManager::instance();
    LanguageManager::Language packToFetch = LanguageManager::Chinese;
    bool needPackFetch = false;
    bool isPackRefresh = false;
    if (languagePacks->hasPendingLanguagePack()) {
        packToFetch = languagePacks->pendingLanguagePack();
        needPackFetch = true;
    } else {
        const LanguageManager::Language current = languagePacks->currentLanguage();
        if (LanguageManager::needsLanguagePackRefresh(current)) {
            packToFetch = current;
            needPackFetch = true;
            isPackRefresh = true;
        }
    }
    if (needPackFetch)
    {
        const QString packName = LanguageManager::nativeName(packToFetch);

        connect(languagePacks, &LanguageManager::languagePackDownloadFinished, this,
                [this, packToFetch, packName](LanguageManager::Language lang) {
            if (lang != packToFetch)
                return;
            NotificationManager::showSuccess(this,
                tr("「%1」语言包已下载完成，重启方块盒子后即可切换。").arg(packName), 10000);
        });
        connect(languagePacks, &LanguageManager::languagePackDownloadFailed, this,
                [this, packToFetch](LanguageManager::Language lang, const QString &error) {
            if (lang != packToFetch)
                return;
            NotificationManager::showInfo(this,
                tr("语言包下载失败：%1，可在「设置 → 界面设置 → 语言」里重试。").arg(error), 10000);
        });
        languagePacks->downloadLanguagePack(packToFetch, isPackRefresh);
    }
}

void MainWindow::setupSystemTray()
{
    // 手机 / 平板没有系统托盘；个别精简 Linux 桌面也没有托盘服务，
    // 这两种情况不建托盘，关窗行为保持「直接退出」
    if (Platform::isMobile() || !SystemTray::isAvailable())
        return;

    m_systemTray = new SystemTray(this);
    connect(m_systemTray, &SystemTray::showWindowRequested, this, &MainWindow::showFromTray);
    connect(m_systemTray, &SystemTray::openSettingsRequested, this, &MainWindow::openSettingsFromTray);
    connect(m_systemTray, &SystemTray::quitRequested, this, &MainWindow::quitFromTray);

    // 托盘菜单是系统级菜单，不会收到 QWidget 的 LanguageChange，语言切换后单独刷一次文案
    connect(LanguageManager::instance(), &LanguageManager::languageChanged,
            m_systemTray, &SystemTray::retranslateUi);

    m_systemTray->show();
}

void MainWindow::showFromTray()
{
    // 最小化状态和「已经收进托盘」都要照顾到：先还原窗口状态，再提到前台
    if (isMinimized())
        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);

    show();
    raise();
    activateWindow();
}

void MainWindow::hideToTray()
{
    if (!m_systemTray)
        return;

    // 第一次收进托盘时说明一下，免得用户以为软件已经关掉、又去点图标开第二个
    SettingsManager *settings = SettingsManager::instance();
    if (!settings->getProperty(QStringLiteral("tray/hintShown"), false).toBool())
    {
        settings->setProperty(QStringLiteral("tray/hintShown"), true);
        m_systemTray->showBalloon(
            tr("方块盒子已最小化到系统托盘"),
            tr("软件仍在后台运行，点托盘图标可以重新打开窗口；右键托盘图标选「退出方块盒子」才是彻底退出。"),
            6000);
    }

    // 主窗口的弹窗 / 覆盖层都挂在它下面，跟着一起收起来
    hide();
}

void MainWindow::openSettingsFromTray()
{
    showFromTray();

    // 与点击侧边栏「设置」一致：恢复侧边栏与顶栏后切到设置页（默认落在「常规设置」，
    // 托盘开关就在这一页的「启动行为」里）
    m_topBar->setAccountCardVisible(true);
    m_topBar->setWindowControlCardVisible(true);
    setSideBarVisible(true);
    onSideBarItemClicked(2);
}

void MainWindow::quitFromTray()
{
    // 用户明确要退出：先撤托盘图标（避免进程结束后托盘里留个点不动的残影）
    m_quitConfirmed = true;
    if (m_systemTray)
        m_systemTray->hide();

    // 走正常关窗路径；窗口正收在托盘里时个别平台不会派发关闭事件，
    // 再用 quit() 兜底结束事件循环（析构清理照常执行）
    close();
    QTimer::singleShot(0, qApp, []() { QCoreApplication::quit(); });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 桌面端「关闭」只是把窗口收进托盘：进程继续在后台跑，真正退出走托盘菜单。
    // 用户明确点了退出、系统没有托盘，或者设置里关掉了这个行为时，按老规矩关窗退出。
    const bool closeToTray = !m_quitConfirmed
        && m_systemTray && m_systemTray->isVisible()
        && SettingsManager::instance()->isCloseToTrayEnabled();

    if (!closeToTray)
    {
        event->accept();
        return;
    }

    event->ignore();
    hideToTray();
}

void MainWindow::applyMcpServerSettings()
{
    if (!m_mcpServer)
    {
        return;
    }

    SettingsManager *settings = SettingsManager::instance();
    const bool enabled = settings->getProperty(QStringLiteral("mcp/enabled"), false).toBool();
    const int portValue = settings->getProperty(QStringLiteral("mcp/port"), 17890).toInt();
    const bool allowWrite = settings->getProperty(QStringLiteral("mcp/allowWrite"), false).toBool();

    QString token = settings->getProperty(QStringLiteral("mcp/token"), QString()).toString().trimmed();
    if (token.isEmpty())
    {
        token = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
        settings->setProperty(QStringLiteral("mcp/token"), token);
    }

    if (!enabled)
    {
        m_mcpServer->stop();
        m_mcpRunning = false;
        m_mcpRunningPort = 0;
        m_mcpStatusDetail.clear();
        syncMcpStatusToSettingsPage();
        return;
    }

    const quint16 port = static_cast<quint16>(qBound(1024, portValue, 65535));
    if (m_mcpServer->isRunning() && m_mcpServer->port() == port)
    {
        // 只改了令牌 / 写权限：原地生效，不用断开已连接的客户端
        m_mcpServer->setToken(token);
        m_mcpServer->setAllowWrite(allowWrite);
        m_mcpRunning = true;
        m_mcpRunningPort = port;
        m_mcpStatusDetail.clear();
        syncMcpStatusToSettingsPage();
        return;
    }

    QString error;
    if (!m_mcpServer->start(port, token, allowWrite, &error))
    {
        m_mcpRunning = false;
        m_mcpRunningPort = 0;
        m_mcpStatusDetail = error;
        syncMcpStatusToSettingsPage();
    }
}

void MainWindow::syncMcpStatusToSettingsPage()
{
    if (!m_stackedWidget)
    {
        return;
    }
    QScrollArea *area = qobject_cast<QScrollArea *>(
        m_stackedWidget->widget(static_cast<int>(PageIndex::SettingsPage)));
    if (!area)
    {
        return;
    }
    SettingsPage *page = qobject_cast<SettingsPage *>(area->widget());
    if (page)
    {
        page->setMcpServerStatus(m_mcpRunning, m_mcpRunningPort, m_mcpStatusDetail);
    }
}

void MainWindow::setupLinkRouter()
{
    LinkRouter::Handlers handlers;
    handlers.openForumThread = [this](int threadId) {
        if (threadId > 0)
            showForumThread(threadId);
    };
    handlers.openForumBoard = [this](const QString &slug, const QString &boardName) {
        if (!slug.isEmpty())
            showForumBoard(slug, boardName);
    };
    handlers.handleJoinLink = [this](const QString &url) { handleExternalJoinUrl(url); };
    handlers.openContentDetail = [this](const ModInfo &info, ContentType type) {
        if (type == ContentType::Mod)
            showModDetailPage(info, m_currentInstanceVersion, m_currentInstanceLoader);
        else
            showContentDetailPage(info, type);
    };
    LinkRouter::instance()->setHandlers(handlers);
}

MainWindow::~MainWindow()
{
    // 本地 MCP 服务器：先停掉再断开状态信号，避免子对象析构时回调到已析构的成员
    if (m_mcpServer)
    {
        m_mcpServer->disconnect(this);
        m_mcpServer->stop();
    }

    // 关软件时把「正在游玩」的记录撤掉（尽力而为，断网时服务端 2 分钟后自动清）
    stopLobbyPlayReport();

    // 关软件时如果游戏还在跑，也把这段游玩时长结算掉（下次启动不会少算）
    PlaytimeStatsService::instance()->endSession();

    // 实例助手是独立顶层窗口，不挂在主窗口下，退出时这里手动释放（多开：逐个释放）
    qDeleteAll(m_assistantWindows);
    m_assistantWindows.clear();
    delete m_overlayAssistantWindow;
    m_overlayAssistantWindow = nullptr;
    delete m_bedrockInstanceAssistantWindow;
    m_bedrockInstanceAssistantWindow = nullptr;
    delete ui;
}

void MainWindow::initUI()
{
    // 开始测量UI初始化时间
    PerformanceMonitor::instance()->startMeasurement("界面初始化");

    // Set window properties
    setWindowTitle(tr(""));
    // 非官方构建（打包脚本没传 BLOCKBOX_OFFICIAL_BUILD）：标题追加显式水印，
    // 任务栏 / Alt-Tab 里也能一眼看出这不是官方发布。官方构建标题保持不变
    // （自绘标题栏原本就是空标题，这里不额外加应用名）。
    if (!BuildChannel::isOfficialBuild())
        setWindowTitle(tr("方块盒子") + BuildChannel::windowTitleSuffix());
#if defined(Q_OS_ANDROID) || defined(HARMONY_OS)
    if (Platform::isMobile()) {
        // 移动端：不限制最小尺寸——任何最小高度都可能比屏幕高，把底部内容挤出屏幕。
        // 窗口直接按当前屏幕可用区域给尺寸，之后完全跟随系统给的窗口大小。
        // （此前只有安卓生效，鸿蒙手机同样存在该问题；鸿蒙电脑仍走桌面分支）
        setMinimumSize(1, 1);
        if (QScreen *screen = QGuiApplication::primaryScreen())
            resize(screen->availableSize());
    } else
#endif
    {
        setMinimumSize(1200, 800);
    }
    
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

    // 应用内更新：下载开始后侧边栏底部出现圆形进度按钮（完成变绿，点击重启安装）
    UpdateProgressButton *updateProgressButton = new UpdateProgressButton(m_sideBar);
    m_sideBar->setUpdateButton(updateProgressButton);

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

    // 背景层铺满整个主窗口（含顶栏与左侧导航栏，不止 contentWrapper）：
    // 必应壁纸/旋转/流光等模式由 BackgroundWidget 绘制背景，若只覆盖内容区，
    // 透明顶栏/侧边栏会透出 QMainWindow 的深色底色（#0F172A），导致对应区域变黑。
    centralWidget->installEventFilter(this);
    m_backgroundWidget = new BackgroundWidget(centralWidget);
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
            // 账户管理页是论坛账户 / 经典两个界面的容器，入场动画要作用在内层页面上
            QWidget *contentPage = current;
            if (m_accountUiStack && current == m_accountUiStack && m_accountUiStack->currentWidget())
                contentPage = m_accountUiStack->currentWidget();

            animateContentEntrance(contentPage);
            // 页面切换动画结束后，强制刷新含 Skin3DWidget 的页面的 3D 皮肤控件
            // 原因：PageTransitionAnimator 的 FadeSlideUp 动画期间会给页面应用 QGraphicsOpacityEffect，
            // 该 effect 会破坏其子 QOpenGLWidget (Skin3DWidget) 的 GL 渲染。
            // 动画结束后 effect 被移除，但 Skin3DWidget 不会自动重绘，需手动触发。
            if (current == m_skinEditorPage && m_skinEditorPage)
            {
                m_skinEditorPage->refreshSkin3DPreview();
            }
            // 皮肤库作品详情页同样带 3D 预览：动画结束后重绘一次
            if (current == m_communitySkinDetailPage && m_communitySkinDetailPage)
            {
                m_communitySkinDetailPage->refreshPreviewAfterTransition();
            }
        }
    });

    contentLayout->addWidget(contentWrapper, 1);

    // Add content area to main layout
    mainLayout->addWidget(contentArea, 1);

    // 任务栏：悬浮叠在内容区底部（不占布局空间），有任务时滑入
    m_taskBar = new TaskBar(contentArea);
    m_taskBar->updateFloatGeometry();

#ifdef Q_OS_ANDROID
    // 安卓版：游戏内触屏键位悬浮层（布局取「操作设置」保存的触屏布局）
    m_touchControlOverlay = new TouchControlOverlay(this);
    m_touchControlOverlay->hide();
    connect(m_touchControlOverlay, &TouchControlOverlay::moveVectorChanged, this,
            [this](qreal x, qreal y) { onTouchControlInput(QStringLiteral("move"), x, y, true); });
    connect(m_touchControlOverlay, &TouchControlOverlay::lookDelta, this,
            [this](int dx, int dy) { onTouchControlInput(QStringLiteral("look"), dx, dy, true); });
    connect(m_touchControlOverlay, &TouchControlOverlay::actionPressed, this,
            [this](const QString &action) { onTouchControlInput(action, 0, 0, true); });
    connect(m_touchControlOverlay, &TouchControlOverlay::actionReleased, this,
            [this](const QString &action) { onTouchControlInput(action, 0, 0, false); });
    connect(m_touchControlOverlay, &TouchControlOverlay::keysPressed, this,
            [this](const QStringList &keys) {
                if (m_gameLauncher)
                    m_gameLauncher->sendTouchInputKeys(keys, true);
            });
    connect(m_touchControlOverlay, &TouchControlOverlay::keysReleased, this,
            [this](const QStringList &keys) {
                if (m_gameLauncher)
                    m_gameLauncher->sendTouchInputKeys(keys, false);
            });

    // 安卓版：游戏内悬浮的方块盒子图标，点击弹出/收起实例助手侧栏；
    // 图标 showEvent 里会自动 raise，盖在键位悬浮层之上
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

    // 子导航顶部搜索框：200ms 防抖后过滤当前页面主内容区
    connect(m_subNavPanel, &SubNavPanel::searchQueryChanged, this, &MainWindow::onSubNavSearchQueryChanged);
    // 页面切换后：把当前查询重新应用到新页面（新页实现 applyPageFilter 时生效）
    connect(m_stackedWidget, &QStackedWidget::currentChanged, this, [this](int)
    {
        // 手机端：新页面的内容滚动区统一支持手指拖动滚动（懒加载页在切换时补挂）
        if (m_stackedWidget->currentWidget())
            TouchScroll::enablePageDragScroll(m_stackedWidget->currentWidget());
        if (m_subNavPanel)
            onSubNavSearchQueryChanged(m_subNavPanel->searchQuery());
    });

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
    PerformanceMonitor::instance()->endMeasurement("界面初始化");
}

namespace {

/// 页面 → 匿名统计 / 崩溃现场用的短标识（只描述页面，不带实例名、账号等内容）
QString pageDiagnosticKey(PageIndex index)
{
    switch (index) {
    case PageIndex::HomePage: return QStringLiteral("home");
    case PageIndex::ResourcesPage: return QStringLiteral("resources");
    case PageIndex::SettingsPage: return QStringLiteral("settings");
    case PageIndex::InstanceSelectPage: return QStringLiteral("instances");
    case PageIndex::AccountManagePage: return QStringLiteral("account");
    case PageIndex::LaunchDetailsPage: return QStringLiteral("launch_details");
    case PageIndex::InstallInstancePage: return QStringLiteral("install_instance");
    case PageIndex::LoaderDetailPage: return QStringLiteral("loader_detail");
    case PageIndex::ForgeVersionListPage: return QStringLiteral("version_list");
    case PageIndex::TaskListPage: return QStringLiteral("tasks");
    case PageIndex::TaskDetailPage: return QStringLiteral("task_detail");
    case PageIndex::ModDownloadPage: return QStringLiteral("mod_download");
    case PageIndex::ModDetailPage: return QStringLiteral("mod_detail");
    case PageIndex::ModpackImportPage: return QStringLiteral("modpack_import");
    case PageIndex::SearchPage: return QStringLiteral("search");
    case PageIndex::ContentDownloadPage: return QStringLiteral("content_download");
    case PageIndex::ContentDetailPage: return QStringLiteral("content_detail");
    case PageIndex::ContentListPage: return QStringLiteral("content_list");
    case PageIndex::InstanceManagePage: return QStringLiteral("instance_manage");
    case PageIndex::ModpackExportPageIndex: return QStringLiteral("modpack_export");
    case PageIndex::LocalModDetailPageIndex: return QStringLiteral("local_mod_detail");
    case PageIndex::AiChatPage: return QStringLiteral("ai_chat");
    case PageIndex::JavaDownloadPage: return QStringLiteral("java_download");
    case PageIndex::FavoritesPage: return QStringLiteral("favorites");
    case PageIndex::SkinEditorPage: return QStringLiteral("skin_editor");
    case PageIndex::PluginPage: return QStringLiteral("plugins");
    case PageIndex::NewsPage: return QStringLiteral("news");
    case PageIndex::CommunityPage: return QStringLiteral("community");
    case PageIndex::ForumThreadDetailPage: return QStringLiteral("forum_thread");
    case PageIndex::ForumThreadListPage: return QStringLiteral("forum_threads");
    case PageIndex::ForumComposerPage: return QStringLiteral("forum_composer");
    case PageIndex::ForumUserProfilePage: return QStringLiteral("forum_profile");
    case PageIndex::FeedbackPage: return QStringLiteral("feedback");
    case PageIndex::ForumFriendsPage: return QStringLiteral("forum_friends");
    case PageIndex::FeedbackComposePage: return QStringLiteral("feedback_compose");
    case PageIndex::ForumPluginListPage: return QStringLiteral("forum_plugins");
    case PageIndex::PublishPage: return QStringLiteral("publish");
    case PageIndex::PublishResourcePage: return QStringLiteral("publish_resource");
    case PageIndex::PublishPlatformPage: return QStringLiteral("publish_platform");
    case PageIndex::PublishBoardPage: return QStringLiteral("publish_board");
    case PageIndex::OnboardingPage: return QStringLiteral("onboarding");
    case PageIndex::ForumReportPage: return QStringLiteral("forum_report");
    case PageIndex::ForumSchematicListPage: return QStringLiteral("forum_schematics");
    case PageIndex::GameLaunchPage: return QStringLiteral("game_launch");
    case PageIndex::FeedbackDetailPage: return QStringLiteral("feedback_detail");
    case PageIndex::TranslationComposePage: return QStringLiteral("translation_compose");
    case PageIndex::CommunitySkinPage: return QStringLiteral("community_skins");
    case PageIndex::SkinStationPage: return QStringLiteral("skin_station");
    case PageIndex::AnnualReportPage: return QStringLiteral("annual_report");
    case PageIndex::CommunitySkinDetailPage: return QStringLiteral("community_skin_detail");
    case PageIndex::QrScanPage: return QStringLiteral("qr_scan");
    case PageIndex::LibraryPageIndex: return QStringLiteral("library");
    case PageIndex::MorePageIndex: return QStringLiteral("more");
    case PageIndex::SeedMapPage: return QStringLiteral("seed_map");
    case PageIndex::ProjectionEditPageIndex: return QStringLiteral("projection_editor");
    case PageIndex::LanTransferPageIndex: return QStringLiteral("lan_transfer");
    case PageIndex::MemoryOptimizePageIndex: return QStringLiteral("memory_optimize");
    case PageIndex::MultiThreadDownloadPageIndex: return QStringLiteral("multi_thread_download");
    }
    return QStringLiteral("page_%1").arg(static_cast<int>(index));
}

} // namespace

void MainWindow::trackPageForDiagnostics(PageIndex index)
{
    const QString key = pageDiagnosticKey(index);
    CrashReporter::instance()->setCurrentPage(key);
    Telemetry::pageView(key);
}

void MainWindow::animatedSwitchToPage(PageIndex index)
{
    trackPageForDiagnostics(index);
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
    trackPageForDiagnostics(index);
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

void MainWindow::restoreSideBarWithSubNav(int parentIndex, int childIndex)
{
    setSideBarVisible(true);

    if (!m_sideBar || !m_subNavPanel || parentIndex < 0)
        return;

    const QVector<NavItem> &parents = m_sideBar->parentItems();
    if (parentIndex >= parents.size())
        return;

    // "实例管理"等不在侧边栏渲染的分组保持无选中高亮，避免出现指向空处的选中态
    if (parents[parentIndex].hideInSidebar)
        m_sideBar->clearSelection();
    else
        m_sideBar->setSelectedIndex(parentIndex);
    m_lastSidebarIndex = parentIndex;

    m_subNavPanel->showForParent(parentIndex, false);
    if (childIndex >= 0)
        m_subNavPanel->setSelectedChild(childIndex);
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
        ensurePageInitialized(PageIndex::SettingsPage);
        animatedSwitchToPageDirectional(PageIndex::SettingsPage, m_lastSidebarIndex, parentIndex);
        onChildNavClicked(2, 0);
    } else if (parentIndex == 5) { // 社区（方块盒子论坛）
        m_subNavPanel->hidePanel();
        ensurePageInitialized(PageIndex::CommunityPage);
        animatedSwitchToPageDirectional(PageIndex::CommunityPage, m_lastSidebarIndex, parentIndex);
        m_topBar->setTitle(tr("社区"));
        m_topBar->setAccountCardVisible(true);
        m_topBar->setWindowControlCardVisible(true);
        // 人机验证交给社区页自己的请求去触发（被 403 拦下 → 弹滑块 → 验完重发），
        // 不在这里提前弹：点侧边栏的这一下会被刚弹出的弹窗当成「点遮罩」关掉
        if (m_communityPage) {
            m_communityPage->closeLobbyIfVisible();
            m_communityPage->refresh();
        }
    } else if (parentIndex == FeedbackNavIndex) { // 反馈（侧边栏底部入口）
        showFeedbackPage();
    }

    m_lastSidebarIndex = parentIndex;
}

void MainWindow::openInterfaceSettings()
{
    // 与点击侧边栏「设置」保持一致：切换页面、展开设置子导航，再定位到「界面设置」
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
    onChildNavClicked(2, 1);
    m_subNavPanel->setSelectedChild(1);
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
        ensurePageInitialized(PageIndex::SettingsPage);
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
    } else if (parentIndex == 5) { // 社区（方块盒子论坛）
        m_subNavPanel->hidePanel();
        ensurePageInitialized(PageIndex::CommunityPage);
        animatedSwitchToPageDirectional(PageIndex::CommunityPage, m_lastSidebarIndex, parentIndex);
        m_topBar->setTitle(tr("社区"));
        m_topBar->setAccountCardVisible(true);
        m_topBar->setWindowControlCardVisible(true);
        // 论坛开了人机验证的话，交给社区页自己的请求去触发滑块：被 403 拦下时
        // 会弹验证框、验完把请求原样重发。不在这里提前弹——点侧边栏的这一下
        // 会被刚弹出来的弹窗当成「点遮罩」，滑块一闪就没了
        if (m_communityPage) {
            m_communityPage->closeLobbyIfVisible();
            m_communityPage->refresh();
        }
    } else if (parentIndex == PublishNavIndex) { // 发布中心（资源 / 帖子）
        showPublishPage();
    } else if (parentIndex == MoreNavIndex) { // 更多（图书馆 / 皮肤站 / 种子地图 / 投影编辑器 / 文件传输 / 内存优化 / 迁移向导 / 新手引导）
        showMorePage();
    } else if (parentIndex == FeedbackNavIndex) { // 反馈（侧边栏底部入口）
        showFeedbackPage();
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
        // ── 基岩版子导航：0安装新实例 / 1资源包 / 2材质包 / 3地图 / 4脚本 / 5附加包 / 6新建收藏夹 / 7+收藏夹
        //    （childIndex 10 兼容资源主页「附加包」分组卡入口） ──
        if (FavoritesManager::instance()->isBedrockMode()) {
            if (childIndex == 0) {
                // 基岩版安装入口 → 基岩版本下载页（mcappx/mcapks/bbk 多源列表）
                showBedrockDownloadPage();
            } else if (childIndex >= 1 && childIndex <= 4) {
                restoreSideBarWithSubNav(1, childIndex);
                showBedrockContentPage(bedrockClassIdForChildIndex(childIndex));
                // 子导航标题：资源包/材质包/地图/脚本（材质包与资源包同库但标题区分入口）
                const QStringList bedrockTitles = {
                    tr("资源>资源包"), tr("资源>材质包"), tr("资源>地图"), tr("资源>脚本")
                };
                if (childIndex - 1 >= 0 && childIndex - 1 < bedrockTitles.size())
                    m_topBar->setTitle(bedrockTitles[childIndex - 1]);
            } else if (childIndex == 5 || childIndex == 10) {
                // 「附加包」：侧边栏第 5 项，或资源主页「附加包」分组卡（childIndex 10）
                restoreSideBarWithSubNav(1, childIndex == 5 ? 5 : -1);
                showBedrockContentPage(bedrockClassIdForChildIndex(10));
                m_topBar->setTitle(tr("资源>附加包"));
            } else if (childIndex == 6) {
                onCreateFavoriteFolderRequested();
            } else {
                // childIndex >= 7: 收藏夹列表
                QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
                int folderIdx = childIndex - 7;
                if (folderIdx >= 0 && folderIdx < folders.size())
                    showFavoritesPage(folders[folderIdx].id);
            }
        }
        else if (childIndex == 0) {
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
            // 方块皮肤站皮肤库（资源页「皮肤」子项）
            showCommunitySkinPage();
        } else if (childIndex == 9) {
            // 论坛投影分享列表（资源页「投影」子项）
            showForumSchematicListPage();
        } else if (childIndex == 10) {
            // 论坛插件列表（资源页「插件」子项）
            showForumPluginListPage();
        } else if (childIndex == 11) {
            // 新建收藏夹
            onCreateFavoriteFolderRequested();
        } else {
            // childIndex >= 12: 收藏夹列表
            QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
            int folderIdx = childIndex - 12;
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
                    tr("资源>光影包"), tr("资源>世界"), tr("资源>皮肤"),
                    tr("资源>投影"), tr("资源>插件"), tr("资源>新建收藏夹")
                };
                if (childIndex >= 0 && childIndex < resourceTitles.size()) {
                    m_topBar->setTitle(resourceTitles[childIndex]);
                }
            }
        }
    } else if (parentIndex == 2) { // Settings child
        // 设置页为按需创建，切到设置子项前先确保已初始化
        ensurePageInitialized(PageIndex::SettingsPage);
        SettingsPage *settingsPage = nullptr;
        QScrollArea *scrollArea = qobject_cast<QScrollArea*>(m_stackedWidget->widget(2));
        if (scrollArea)
            settingsPage = qobject_cast<SettingsPage*>(scrollArea->widget());

        // 「插件管理」是挂在设置子导航最后的插件整页（设置内容页里没有这一页）
        if (settingsPage && settingsPage->isPluginManageTab(childIndex)) {
            showPluginManagePage(childIndex);
            return;
        }

        // 从插件管理整页切回设置内容页：把侧边栏、顶栏和页面状态恢复成「设置」
        if (m_stackedWidget->currentIndex() != static_cast<int>(PageIndex::SettingsPage)) {
            setSideBarVisible(true);
            if (m_sideBar) {
                m_sideBar->setSelectedIndex(2);
                m_sideBar->setExpandButtonVisible(false);
            }
            if (m_subNavPanel && m_subNavPanel->currentParentIndex() != 2)
                m_subNavPanel->showForParent(2, false);
            m_lastSidebarIndex = 2;
            m_topBar->setAccountCardVisible(true);
            m_topBar->setWindowControlCardVisible(true);
            m_topBar->setAccountSelected(false);
            m_topBar->setInstanceSelectSelected(false);
            m_topBar->setInstanceSettingsSelected(false);
            disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
            connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
            animatedSwitchToPage(PageIndex::SettingsPage);
        }

        if (settingsPage) {
            settingsPage->setCurrentSettingsTab(childIndex);
            // 标题由设置页按实际子页给出：Java / 基岩版与移动端多出的子页不会错位
            const QString title = settingsPage->tabTitle(childIndex);
            if (!title.isEmpty())
                m_topBar->setTitle(title);
            else
                m_topBar->setSettingsTitle();
        } else {
            m_topBar->setSettingsTitle();
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
            tr("实例管理>录像"),
            tr("实例管理>模组"),
            tr("实例管理>资源包"),
            tr("实例管理>光影包"),
            tr("实例管理>数据包"),
            tr("实例管理>日志")
        };
        if (childIndex >= 0 && childIndex < instanceTitles.size()) {
            m_topBar->setTitle(instanceTitles[childIndex]);
        }
        if (m_instanceManagePage) {
            // 直连侧边栏进入时也要先把当前实例同步过去，否则子页拿到的是旧/空实例
            syncInstanceManagePageContext();
            m_instanceManagePage->setCurrentTab(childIndex);
        }
        // 若当前正处于"修改"进入的安装新实例页，点击其他子项应切回实例管理页
        if (m_stackedWidget->currentIndex() != static_cast<int>(PageIndex::InstanceManagePage))
            animatedSwitchToPage(PageIndex::InstanceManagePage);
    } else if (parentIndex == MoreNavIndex) { // 更多分类（子导航 / 落地页卡片共用）
        openMoreFeature(childIndex);
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
    m_topBar->setTitle(tr("设置>插件管理"));
}

void MainWindow::onCreatePluginRequested()
{
    ensurePageInitialized(PageIndex::PluginPage);
    CreatePluginDialog dialog(this);

    // 弹窗里的「开发文档」：记下来，关掉弹窗后跳到内置图书馆的插件开发卷
    bool docsRequested = false;
    connect(&dialog, &CreatePluginDialog::openDocsRequested, this, [&dialog, &docsRequested]() {
        docsRequested = true;
        dialog.reject();
    });

    dialog.exec();

    // 生成模板或打包后，插件列表可能已变化
    refreshPluginNav();
    if (m_pluginPage) m_pluginPage->showOverview();

    if (docsRequested)
        openLibraryDoc(QStringLiteral("plugin"), QStringLiteral("plugin-manifest"));
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
    m_topBar->setTitle(tr("设置>插件管理"));
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
    // 插件管理的入口在「设置」子导航里，入口数量不随插件增减变化：
    // 这里只把侧边栏结构按最新状态重建，插件页自己会监听 PluginManager::pluginsChanged 刷新列表
    m_sideBar->refreshNavStructure();
}

void MainWindow::onSettingsNavItemClicked(int index)
{
    // 设置页为按需创建，切设置子页前先确保已初始化
    ensurePageInitialized(PageIndex::SettingsPage);

    // Get the scroll area containing the settings page
    SettingsPage *settingsPage = nullptr;
    QScrollArea *scrollArea = qobject_cast<QScrollArea*>(m_stackedWidget->widget(2));
    if (scrollArea)
        settingsPage = qobject_cast<SettingsPage*>(scrollArea->widget());

    // 「插件管理」是设置子导航最后一项，内容仍是插件整页
    if (settingsPage && settingsPage->isPluginManageTab(index)) {
        showPluginManagePage(index);
        return;
    }

    if (settingsPage) {
        // Switch to the selected settings tab
        settingsPage->setCurrentSettingsTab(index);
        const QString title = settingsPage->tabTitle(index);
        if (!title.isEmpty())
            m_topBar->setTitle(title);
        else
            m_topBar->setSettingsTitle();
    } else {
        m_topBar->setSettingsTitle();
    }
}

void MainWindow::onResourcesNavItemClicked(int index)
{
    // Handle resources navigation item clicks
    qDebug() << "[MainWindow]" << "资源导航项已点击：" << index;
    
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
            tr("资源>投影"),           // 8
            tr("资源>插件"),           // 9
            tr("资源>新建收藏夹"),     // 10
            tr("资源>默认收藏夹"),     // 11
            tr("资源>示例收藏夹")      // 12
        };
        if (index >= 0 && index < resourceTitles.size()) {
            m_topBar->setTitle(resourceTitles[index]);
        }
    }
}

void MainWindow::onBackToMain()
{
    // 社区页里还开着联机大厅子页面：顶栏返回先退回房间列表 / 社区，而不是直接回主页
    if (m_communityPage && m_stackedWidget
        && m_stackedWidget->currentWidget() == m_communityPage)
    {
        if (m_communityPage->closeLobbyDetailIfVisible())
            return;
        if (m_communityPage->closeLobbyIfVisible())
            return;
    }

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

void MainWindow::onGameLaunchPageBackRequested()
{
    if (!m_gameLaunchPage)
        return;

    // 启动中离开 = 取消启动：先弹确认，防止误触把启动悄悄杀掉
    if (m_gameLauncher && m_gameLauncher->status() == GameLauncher::Launching) {
        const AppMessageBox::StandardButton answer = AppMessageBox::question(
            this, tr("取消启动"),
            tr("游戏正在启动，确定要取消本次启动吗？"),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
        if (answer != AppMessageBox::Yes)
            return;
        m_gameLauncher->stopGame();
    }

    // 清掉"来路页面"记录，避免回主页后按返回键又跳回启动页来路
    m_hasPreviousPage = false;
    onBackToMain();
}

void MainWindow::onInstanceSelectClicked()
{
    m_topBar->setAccountSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setInstanceSelectSelected(true);
    // 基岩版模式：进基岩版实例管理页（Java 实例列表对基岩版无意义）
    if (m_bedrockEdition) {
        showBedrockInstanceSelectPage();
        return;
    }
    showInstanceSelectPage();
}

void MainWindow::openInstanceAssistant()
{
    // 基岩版模式：打开基岩版实例助手（首页/AI/快捷指令/联机/资源管理/设置）
    if (m_bedrockEdition) {
        BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
        mgr->ensureInitialized();
        if (mgr->activeInstanceId().isEmpty()) {
            NotificationManager::showInfo(this,
                tr("还没有基岩版实例，请先在「资源>安装新实例」中安装基岩版并创建实例。"));
            showBedrockDownloadPage();
            return;
        }
        if (!m_bedrockInstanceAssistantWindow)
            m_bedrockInstanceAssistantWindow = new BedrockInstanceAssistantWindow(this);
        m_bedrockInstanceAssistantWindow->setInstanceId(mgr->activeInstanceId());
        m_bedrockInstanceAssistantWindow->show();
        m_bedrockInstanceAssistantWindow->raise();
        m_bedrockInstanceAssistantWindow->activateWindow();
        return;
    }

    // Java 版：打开当前实例的助手窗口（多开：每个实例一个独立窗口）
    openInstanceAssistantFor(m_currentInstancePath);
}

void MainWindow::openInstanceAssistantFor(const QString &instancePath)
{
    if (instancePath.isEmpty()) {
        NotificationManager::showInfo(this, tr("还没有选择实例，请先在「实例」页选择或安装一个实例。"));
        showInstanceSelectPage();
        return;
    }

    InstanceAssistantWindow *window = m_assistantWindows.value(instancePath, nullptr);
    if (!window) {
        window = new InstanceAssistantWindow(this);
        // 助手窗口的快捷进存档启动的是「这个窗口所属实例」，
        // 多开时各窗口互不干扰；论坛登录是全局入口，共用即可
        connect(window, &InstanceAssistantWindow::quickLaunchSaveRequested,
                this, [this, instancePath](const QString &saveName) {
                    onQuickLaunchSaveClickedFor(instancePath, saveName);
                });
        connect(window, &InstanceAssistantWindow::forumLoginRequested,
                this, &MainWindow::openForumAccountLogin);
        // 「开服」页的「用这个实例进游戏」：带 --server/--port 启动该实例，进游戏直接连本地服
        connect(window, &InstanceAssistantWindow::launchLocalServerGameRequested,
                this, [this, instancePath](const QString &address, int port) {
                    onQuickLaunchServerClickedFor(instancePath, address,
                                                  static_cast<quint16>(qBound(1, port, 65535)));
                });
        // 「开服」页的「发布到社区服务器导航」：打开发帖页并预填好，地址优先用隧道地址
        connect(window, &InstanceAssistantWindow::publishServerAdRequested, this,
                [this](const QString &title, const QString &content, const QStringList &tags,
                       const QString &address) {
                    // 本机 / 局域网地址别人连不上，提醒一句免得白发一帖
                    const auto localOnlyAddress = [](const QString &value) {
                        const QString host =
                            value.section(QLatin1Char(':'), 0, 0).trimmed().toLower();
                        if (host.isEmpty() || host == QStringLiteral("localhost")
                            || host == QStringLiteral("0.0.0.0"))
                            return true;
                        if (host.startsWith(QStringLiteral("127."))
                            || host.startsWith(QStringLiteral("192.168."))
                            || host.startsWith(QStringLiteral("10.")))
                            return true;
                        if (host.startsWith(QStringLiteral("172."))) {
                            const int second = host.section(QLatin1Char('.'), 1, 1).toInt();
                            return second >= 16 && second <= 31;
                        }
                        return false;
                    };

                    if (!ForumAccountService::instance()->isLoggedIn()) {
                        NotificationManager::showInfo(
                            this, tr("发布到社区服务器导航要先登录方块盒子账号。"));
                        openForumAccountLogin();
                    } else {
                        // 没过入站答题也没关系：发帖页自己会显示答题入口
                        showPublishServerNavigationCompose(title, content, tags, address);
                        if (localOnlyAddress(address)) {
                            NotificationManager::showInfo(
                                this, tr("这地址是本机/局域网的，别人连不上：先在「联机」页做隧道，"
                                         "或把地址改成公网 IP / 域名再发布。"));
                        }
                    }
                    if (isMinimized())
                        showNormal();
                    else if (!isVisible())
                        show();   // 缩到托盘时也要把主窗口叫回来，不然发布页看不到
                    raise();
                    activateWindow();
                });
        m_assistantWindows.insert(instancePath, window);
    }
    // 注入该实例上下文（路径、版本、loader）
    window->setInstanceContext(instancePath,
                               instanceVersionOf(instancePath),
                               instanceLoaderOf(instancePath));
    window->setWindowTitle(tr("实例助手 · %1").arg(instanceDisplayName(instancePath)));
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::syncInstanceManagePageContext()
{
    if (!m_instanceManagePage)
        return;
    if (m_instanceManagePage->currentInstancePath() == m_currentInstancePath)
        return;

    m_instanceManagePage->setCurrentInstancePath(m_currentInstancePath);

    QString instanceName;
    if (!m_currentInstancePath.isEmpty())
        instanceName = QFileInfo(m_currentInstancePath).fileName();
    m_instanceManagePage->setInstanceName(instanceName);
    m_instanceManagePage->setGameVersion(m_currentInstanceVersion);
    m_instanceManagePage->setLoaderInfo(m_currentInstanceLoader);
}

void MainWindow::onInstanceSettingsClicked()
{
    // 基岩版模式：进基岩版实例助手的「设置」标签（实例专属启动设置与数据统计）
    if (m_bedrockEdition) {
        BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
        mgr->ensureInitialized();
        if (mgr->activeInstanceId().isEmpty()) {
            NotificationManager::showInfo(this,
                tr("还没有基岩版实例，请先在「资源>安装新实例」中安装基岩版并创建实例。"));
            return;
        }
        if (!m_bedrockInstanceAssistantWindow)
            m_bedrockInstanceAssistantWindow = new BedrockInstanceAssistantWindow(this);
        m_bedrockInstanceAssistantWindow->setInstanceId(mgr->activeInstanceId());
        m_bedrockInstanceAssistantWindow->switchToTab(5);
        m_bedrockInstanceAssistantWindow->show();
        m_bedrockInstanceAssistantWindow->raise();
        m_bedrockInstanceAssistantWindow->activateWindow();
        return;
    }

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
        syncInstanceManagePageContext();
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
    applyInstanceContext(instancePath);

    setSideBarVisible(true);

    animatedSwitchToPage(PageIndex::HomePage);

    m_topBar->setMainTitle();
}

void MainWindow::applyInstanceContext(const QString &instancePath)
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

    // 同步到首页经典模式皮肤面板
    if (m_homePage) {
        m_homePage->setCurrentInstanceName(instanceName);
    }

    saveCurrentInstancePath();

    if (m_resourcesPage) {
        m_resourcesPage->setInstancePath(m_currentInstancePath);
    }

    // 资源页「投影」子页：下载落点跟着当前实例走
    if (m_forumSchematicListPage) {
        m_forumSchematicListPage->setInstancePath(m_currentInstancePath);
    }

    // 投影帖详情页的「投影预览」也按当前实例取材质
    if (m_forumThreadDetailPage) {
        m_forumThreadDetailPage->setInstancePath(m_currentInstancePath);
    }

    // 同步实例上下文到主侧边栏 AI 页（若已初始化），保持资源引用功能可用
    if (m_aiChatPage) {
        m_aiChatPage->setInstanceContext(m_currentInstancePath,
                                         m_currentInstanceVersion,
                                         m_currentInstanceLoader);
    }
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

void MainWindow::onShowDetailsPage(const QString &instancePath)
{
    // 启动卡片点击：详情页聚焦到该卡片所属实例的启动会话
    if (!instancePath.isEmpty())
        m_focusedLaunchSession = instancePath;
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

void MainWindow::onForumNotificationsClicked()
{
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setAccountSelected(false);
    showAccountManagePage();
    if (m_forumAccountPage)
        m_forumAccountPage->openNotificationsTab();
}

void MainWindow::onForumChatClicked()
{
    m_topBar->setInstanceSelectSelected(false);
    m_topBar->setInstanceSettingsSelected(false);
    m_topBar->setAccountSelected(false);
    // 从顶栏进来没有「上一个论坛页」可回，返回键回主页
    m_forumReturnAction = [this]() { onBackToMain(); };
    showForumFriends();
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
    m_backgroundWidget->setRotatingSpeed(bg->rotatingSpeed());
    if (bg->currentMode() == BackgroundManager::Rotating) {
        applyRotatingPanorama(effectiveRotatingScene());
    } else {
        // 非旋转模式只记住场景名；切回旋转模式时会重新解析本地目录
        m_backgroundWidget->setPanoramaScene(bg->panoramaScene());
    }
    if (bg->currentMode() == BackgroundManager::SolidColor) {
        m_backgroundWidget->setSolidColor(QColor(bg->solidColor()));
    } else if (bg->currentMode() == BackgroundManager::Image
               || bg->currentMode() == BackgroundManager::Rotating) {
        m_backgroundWidget->setImage(bg->imagePath());
    } else if (bg->currentMode() == BackgroundManager::Bing) {
        // 先显示上次缓存的壁纸（可能为空），再由必应壁纸管理器异步补齐最新图片
        m_backgroundWidget->setImage(bg->bingImagePath());
        BingWallpaperManager::instance()->ensureLoaded();
    }
}

void MainWindow::applyRotatingPanorama(const QString &scene)
{
    if (!m_backgroundWidget)
        return;

    if (scene.isEmpty()) {
        m_backgroundWidget->setPanoramaSource(QString(), QString());
        return;
    }

    PanoramaThemeManager *themes = PanoramaThemeManager::instance();
    if (PanoramaThemeManager::isThemeReady(scene)) {
        m_backgroundWidget->setPanoramaSource(scene, PanoramaThemeManager::themeDir(scene));
    } else {
        // 素材尚未下载：先不切换画面（深色/自定义图兜底），下载完成后由 themeReady 换上
        m_backgroundWidget->setPanoramaSource(scene, QString());
        themes->ensureTheme(scene);
    }
}

QString MainWindow::effectiveRotatingScene() const
{
    BackgroundManager *bg = BackgroundManager::instance();
    QString scene = bg->panoramaScene();
    if (scene.isEmpty()
        && (bg->imagePath().isEmpty() || !QFileInfo::exists(bg->imagePath()))) {
        // 选「自定义图片」但没指定可用图片：沿用旧版的「主世界」兜底，避免整屏黑底
        scene = PanoramaThemeManager::defaultThemeId();
    }
    return scene;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_backgroundWidget && watched == m_backgroundWidget->parentWidget()
        && event->type() == QEvent::Resize) {
        m_backgroundWidget->setGeometry(m_backgroundWidget->parentWidget()->rect());
    }

    // 主内容区尺寸变化时，同步悬浮子导航气泡与底部任务栏的位置
    if (watched == m_mainContentArea && event->type() == QEvent::Resize) {
        if (m_subNavPanel && m_subNavPanel->isVisible()) {
            m_subNavPanel->updateFloatGeometry();
        }
        if (m_taskBar && m_taskBar->isVisible()) {
            m_taskBar->updateFloatGeometry();
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

QStringList MainWindow::extractUpdatePackages(const QMimeData *mime) const
{
    QStringList packages;
    if (!mime || !mime->hasUrls())
        return packages;
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (UpdateChecker::isUpdatePackageFile(path))
            packages.append(path);
    }
    return packages;
}

QStringList MainWindow::extractBedrockPacks(const QMimeData *mime) const
{
    QStringList packs;
    if (!mime || !mime->hasUrls())
        return packs;
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (FileImportRouter::detectFileType(path) == ImportFileType::BedrockResource)
            packs.append(path);
    }
    return packs;
}

void MainWindow::installDroppedBedrockPacks(const QStringList &files)
{
    BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
    mgr->ensureInitialized();
    const BedrockInstance inst = mgr->activeInstance();
    const QString comMojangDir = inst.dataDir.isEmpty()
                                     ? QString()
                                     : inst.dataDir + QStringLiteral("/com.mojang");
    if (comMojangDir.isEmpty()) {
        NotificationManager::showInfo(this,
            tr("还没有可安装的基岩版实例，请先在「资源>安装新实例」中安装基岩版并创建实例。"));
        return;
    }

    int okCount = 0;
    QString firstErr;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString &file : files) {
        QString err;
        if (BedrockContentInstaller::installPack(file, comMojangDir, &err)) {
            ++okCount;
        } else if (firstErr.isEmpty()) {
            firstErr = err;
        }
    }
    QApplication::restoreOverrideCursor();

    if (okCount == files.size()) {
        NotificationManager::showSuccess(this,
            files.size() == 1
                ? tr("%1 安装成功，启动基岩版后即可在游戏中使用。")
                      .arg(QFileInfo(files.first()).fileName())
                : tr("已成功安装 %1 个基岩版资源。").arg(okCount),
            7000);
    } else if (okCount > 0) {
        NotificationManager::showInfo(this,
            tr("已安装 %1 个资源，其余安装失败：%2").arg(okCount).arg(firstErr), 7000);
    } else {
        NotificationManager::showError(this,
            tr("基岩版资源安装失败: %1").arg(firstErr));
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        QMainWindow::dragEnterEvent(event);
        return;
    }

    m_pendingDropPackages = extractUpdatePackages(event->mimeData());
    if (m_pendingDropPackages.isEmpty()) {
        // 不是更新包：再看是不是基岩版资源包/地图，都不是则不接受，
        // 避免干扰其他页面自身的拖放逻辑
        m_pendingDropBedrockPacks = extractBedrockPacks(event->mimeData());
        if (m_pendingDropBedrockPacks.isEmpty()) {
            event->ignore();
            return;
        }

        event->acceptProposedAction();
        if (!m_dropOverlay) {
            m_dropOverlay = new FileDropOverlay(this);
            m_dropOverlay->setGeometry(rect());
        }
        m_dropOverlay->setGeometry(rect());
        m_dropOverlay->activate(m_pendingDropBedrockPacks.size());
        m_dropOverlay->updatePreview(tr("松开以安装到基岩版实例：%1")
                                         .arg(QFileInfo(m_pendingDropBedrockPacks.first()).fileName()));
        return;
    }
    m_pendingDropBedrockPacks.clear();

    event->acceptProposedAction();
    if (!m_dropOverlay) {
        m_dropOverlay = new FileDropOverlay(this);
        m_dropOverlay->setGeometry(rect());
    }
    m_dropOverlay->setGeometry(rect());
    m_dropOverlay->activate(m_pendingDropPackages.size());
    m_dropOverlay->updatePreview(tr("松开以安装新版本：%1")
                                     .arg(QFileInfo(m_pendingDropPackages.first()).fileName()));
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (!m_pendingDropPackages.isEmpty() && m_dropOverlay && m_dropOverlay->isActive()) {
        event->acceptProposedAction();
        return;
    }
    if (!m_pendingDropBedrockPacks.isEmpty() && m_dropOverlay && m_dropOverlay->isActive()) {
        event->acceptProposedAction();
        return;
    }
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        const QStringList packages = extractUpdatePackages(event->mimeData());
        if (!packages.isEmpty()) {
            m_pendingDropPackages = packages;
            m_pendingDropBedrockPacks.clear();
            event->acceptProposedAction();
            if (!m_dropOverlay) {
                m_dropOverlay = new FileDropOverlay(this);
            }
            m_dropOverlay->setGeometry(rect());
            if (!m_dropOverlay->isActive()) {
                m_dropOverlay->activate(packages.size());
            }
            m_dropOverlay->updatePreview(tr("松开以安装新版本：%1")
                                             .arg(QFileInfo(packages.first()).fileName()));
            return;
        }
        const QStringList bedrockPacks = extractBedrockPacks(event->mimeData());
        if (!bedrockPacks.isEmpty()) {
            m_pendingDropBedrockPacks = bedrockPacks;
            m_pendingDropPackages.clear();
            event->acceptProposedAction();
            if (!m_dropOverlay) {
                m_dropOverlay = new FileDropOverlay(this);
            }
            m_dropOverlay->setGeometry(rect());
            if (!m_dropOverlay->isActive()) {
                m_dropOverlay->activate(bedrockPacks.size());
            }
            m_dropOverlay->updatePreview(tr("松开以安装到基岩版实例：%1")
                                             .arg(QFileInfo(bedrockPacks.first()).fileName()));
            return;
        }
    }
    m_pendingDropPackages.clear();
    m_pendingDropBedrockPacks.clear();
    if (m_dropOverlay && m_dropOverlay->isActive())
        m_dropOverlay->deactivate();
    event->ignore();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    // 拖出窗口：收起覆盖层并丢弃待装列表（若已 drop，dropEvent 会先清空）
    if (m_dropOverlay && m_dropOverlay->isActive())
        m_dropOverlay->deactivate();
    m_pendingDropPackages.clear();
    m_pendingDropBedrockPacks.clear();
    QMainWindow::dragLeaveEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList packages = extractUpdatePackages(event->mimeData());
    if (packages.isEmpty())
        packages = m_pendingDropPackages;
    m_pendingDropPackages.clear();

    QStringList bedrockPacks = extractBedrockPacks(event->mimeData());
    if (bedrockPacks.isEmpty())
        bedrockPacks = m_pendingDropBedrockPacks;
    m_pendingDropBedrockPacks.clear();

    if (m_dropOverlay && m_dropOverlay->isActive())
        m_dropOverlay->deactivate();

    if (!packages.isEmpty()) {
        event->acceptProposedAction();
        // 多文件时只安装第一个可识别更新包
        UpdateChecker::instance()->installExternalPackage(this, packages.first());
        return;
    }

    if (!bedrockPacks.isEmpty()) {
        event->acceptProposedAction();
        installDroppedBedrockPacks(bedrockPacks);
        return;
    }

    event->ignore();
}

void MainWindow::onAccountManagePageOpened()
{
    if (m_topBar)
    {
        m_topBar->setAccountManageTitle();
    }
}

void MainWindow::onTopBarBackClicked()
{
    // 安卓整页启动界面：返回键/顶栏返回走启动页自己的逻辑（启动中先确认是否取消）
    if (m_stackedWidget && m_gameLaunchPage &&
        m_stackedWidget->currentWidget() == m_gameLaunchPage)
    {
        onGameLaunchPageBackRequested();
        return;
    }

    // 创作中心内部的作品详情子页：返回先退回作品列表，不离开创作中心
    if (m_creationCenterPage && m_accountUiStack && m_stackedWidget &&
        m_accountUiStack->currentWidget() == m_creationCenterPage &&
        m_stackedWidget->currentIndex() == static_cast<int>(PageIndex::AccountManagePage) &&
        m_creationCenterPage->returnToWorkList())
    {
        return;
    }

    // 添加角色页是账户页内层的一页：返回先退回角色页，不离开页面
    if (m_addAccountPage && m_forumAccountPage && m_accountUiStack && m_stackedWidget &&
        m_accountUiStack->currentWidget() == m_addAccountPage &&
        m_stackedWidget->currentIndex() == static_cast<int>(PageIndex::AccountManagePage))
    {
        m_accountUiStack->setCurrentWidget(m_forumAccountPage);
        m_topBar->setAccountManageTitle();
        return;
    }

    // 创作中心是账户页内层的第二页：返回先退回账户总览，不离开页面
    if (m_creationCenterPage && m_forumAccountPage && m_accountUiStack &&
        m_accountUiStack->currentWidget() == m_creationCenterPage && m_stackedWidget &&
        m_stackedWidget->currentIndex() == static_cast<int>(PageIndex::AccountManagePage))
    {
        m_accountUiStack->setCurrentWidget(m_forumAccountPage);
        m_topBar->setAccountManageTitle();
        return;
    }

    // 论坛账户界面的登录 / 注册子视图：先退回账户总览，不离开页面
    if (m_forumAccountPage && m_accountUiStack &&
        m_accountUiStack->currentWidget() == m_forumAccountPage && m_stackedWidget &&
        m_stackedWidget->currentIndex() == static_cast<int>(PageIndex::AccountManagePage) &&
        m_forumAccountPage->returnToOverview())
    {
        return;
    }

    // 皮肤库作品详情页：返回先回皮肤库列表，不离开资源页
    if (m_stackedWidget &&
        m_stackedWidget->currentIndex() == static_cast<int>(PageIndex::CommunitySkinDetailPage))
    {
        returnToCommunitySkinList();
        return;
    }

    // 我的 MC 年度报告页：返回先回账户管理页的「游玩」页签
    if (m_stackedWidget &&
        m_stackedWidget->currentIndex() == static_cast<int>(PageIndex::AnnualReportPage))
    {
        showAccountManagePage();
        if (m_forumAccountPage)
            m_forumAccountPage->openPlaytimeTab();
        return;
    }

    // 皮肤站整页：返回先回账户管理页，不离开软件
    if (m_stackedWidget &&
        m_stackedWidget->currentIndex() == static_cast<int>(PageIndex::SkinStationPage))
    {
        showAccountManagePage();
        return;
    }

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

bool MainWindow::handleAndroidBack()
{
    // 主窗口还没显示（启动阶段）：返回键交给系统，按默认行为处理
    if (!isVisible())
        return false;

    // 1. 应用内弹窗（AppDialogBase 覆盖层，含 AppMessageBox）：关最上层一个
    if (AppDialogBase *dialog = AppDialogBase::topMostVisibleDialog()) {
        dialog->reject();
        return true;
    }

    // 2. 游戏内实例助手侧栏（安卓 overlay 模式专用窗口）
    if (m_overlayAssistantWindow && m_overlayAssistantWindow->isVisible()) {
        m_overlayAssistantWindow->hide();
        return true;
    }

    // 3. 子导航气泡面板
    if (m_subNavPanel && m_subNavPanel->isVisible()) {
        m_subNavPanel->hidePanel();
        return true;
    }

    // 4. 软键盘开着：先收键盘
    QInputMethod *inputMethod = QGuiApplication::inputMethod();
    if (inputMethod && inputMethod->isVisible()) {
        inputMethod->hide();
        return true;
    }

    // 5. 有可返回的页面层级（历史上一页 / 论坛账户登录注册子视图 / 创作中心 / 添加角色页 / 皮肤站整页）
    const bool forumSubviewOpen = m_forumAccountPage && m_accountUiStack && m_stackedWidget &&
        m_stackedWidget->currentIndex() == static_cast<int>(PageIndex::AccountManagePage) &&
        (m_accountUiStack->currentWidget() == m_forumAccountPage ||
         m_accountUiStack->currentWidget() == m_creationCenterPage ||
         m_accountUiStack->currentWidget() == m_addAccountPage);
    const bool skinStationOpen = m_stackedWidget &&
        m_stackedWidget->currentIndex() == static_cast<int>(PageIndex::SkinStationPage);
    const bool skinDetailOpen = m_stackedWidget &&
        m_stackedWidget->currentIndex() == static_cast<int>(PageIndex::CommunitySkinDetailPage);
    if (m_hasPreviousPage || forumSubviewOpen || skinStationOpen || skinDetailOpen) {
        onTopBarBackClicked();
        return true;
    }

    // 6. 主界面：弹窗确认后才退出，避免误触返回直接丢掉整个应用
    const AppMessageBox::StandardButton answer = AppMessageBox::question(
        this, tr("退出 BlockBox"),
        tr("确定要退出 BlockBox 吗？"),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (answer == AppMessageBox::Yes)
        QCoreApplication::quit();
    return true;
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
    QWidget *currentPage = m_stackedWidget ? m_stackedWidget->currentWidget() : nullptr;
    if (!currentPage)
        return;

    // 页面外层可能还包着容器：账户管理页在 QStackedWidget 里（论坛账户界面），
    // 首页 / 设置页在 QScrollArea 里。一路往下收集候选，先试页面本身再试里面那层，
    // 让整页级的刷新槽（refreshAccount 等）优先于内层内容列表
    QList<QWidget *> candidates;
    candidates.append(currentPage);
    for (int guard = 0; guard < 4 && currentPage; ++guard)
    {
        QWidget *inner = nullptr;
        if (auto *stack = qobject_cast<QStackedWidget *>(currentPage))
            inner = stack->currentWidget();
        else if (auto *scrollArea = qobject_cast<QScrollArea *>(currentPage))
            inner = scrollArea->widget();
        else if (auto *stack = currentPage->findChild<QStackedWidget *>())
            inner = stack->currentWidget();

        if (!inner || candidates.contains(inner))
            break;
        candidates.append(inner);
        currentPage = inner;
    }

    // 各页的刷新槽名字不统一，按「越通用越靠前」逐个试，命中一个就停
    static const char *const refreshMethods[] = {
        "onRefreshVersions",   // 版本列表页
        "refresh",             // 实例管理 / 世界地图 / 社区等
        "refreshAccount",      // 账户管理（论坛资料 + 角色卡片）
        "onRefreshClicked",    // 内容列表 / 收藏 / 联机大厅
        "refreshPage",         // 联机页
        "refreshList",         // 插件 / 资源 / 实例资源
        "refreshContentList",  // 资源列表
        "refreshModList",      // 模组列表
        "refreshFileList",     // 实例文件 / 录像
        "refreshRecentPlays",  // 首页最近游玩
        "reloadCharacters",    // 角色页兜底
        "reload"               // 好友 / 帖子详情
    };

    for (QWidget *candidate : candidates)
    {
        for (const char *method : refreshMethods)
        {
            // 先查一次元对象，避免对没有该槽的页面刷一堆「No such method」警告
            const QByteArray signature = QByteArray(method) + "()";
            if (candidate->metaObject()->indexOfMethod(signature.constData()) < 0)
                continue;

            QMetaObject::invokeMethod(candidate, method, Qt::DirectConnection);
            NotificationManager::showSuccess(this, tr("已刷新"));
            return;
        }
    }

    NotificationManager::showInfo(this, tr("当前页面没有可刷新的内容"));
}

// ── 子导航搜索框 → 当前页内容过滤 ─────────────────────────────────────────

QWidget *MainWindow::resolveCurrentFilterPage() const
{
    if (!m_stackedWidget)
        return nullptr;
    QWidget *page = m_stackedWidget->currentWidget();
    // HomePage / SettingsPage 外层包了 QScrollArea
    if (auto *scrollArea = qobject_cast<QScrollArea *>(page))
        page = scrollArea->widget();
    return page;
}

void MainWindow::dispatchPageFilter(const QString &query)
{
    m_subNavFilterQuery = query;

    // 先恢复上一次被过滤的页面（页面可能已切换，不能再找「当前页」）
    if (m_lastFilteredPage)
    {
        QMetaObject::invokeMethod(m_lastFilteredPage, "applyPageFilter",
                                  Q_ARG(QString, QString()));
        m_lastFilteredPage = nullptr;
    }

    if (query.isEmpty())
        return;

    QWidget *page = resolveCurrentFilterPage();
    if (!page)
        return;

    // 仅当页面实现了 Q_INVOKABLE applyPageFilter(const QString &) 时分发
    if (page->metaObject()->indexOfMethod("applyPageFilter(QString)") < 0)
        return;

    QMetaObject::invokeMethod(page, "applyPageFilter", Q_ARG(QString, query));
    m_lastFilteredPage = page;
}

void MainWindow::onSubNavSearchQueryChanged(const QString &query)
{
    dispatchPageFilter(query);
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

bool MainWindow::ensureHumanCheck(bool force)
{
    ForumAccountService *service = ForumAccountService::instance();
    // force：服务端已经明确拦下来了，本地那份令牌可能早被它作废，
    // 这时候再拿本地到期时间当托词跳过滑块，被拦的请求就会一直挂着
    if (!force && service->humanCheckSatisfied())
        return true;

    // 已经在弹了就别叠第二个（页面刷新时会并发好几个请求，每个都会触发一次；
    // AI 助手工作模式也可能提前弹了一道，同样按「已在验证」处理）
    if (m_humanCheckRunning || HumanCheckDialog::isChecking())
        return false;

    m_humanCheckRunning = true;
    const bool passed = HumanCheckDialog::verify(this);
    m_humanCheckRunning = false;
    return passed;
}

void MainWindow::onHumanCheckRequired()
{
    // 服务端不认令牌时才会走到这里：本地那份已经被服务当作废了
    // AI 助手工作模式可能已经提前弹着验证框，这时直接等它验完，别叠窗也别放掉排队请求
    if (m_humanCheckRunning || HumanCheckDialog::isChecking())
        return;

    if (!ensureHumanCheck(true))
    {
        // 用户没验就关掉了滑块：把被拦下的请求放掉，页面给出「需要人机验证」
        // 的提示，而不是永远停在「正在连接论坛…」
        ForumAccountService::instance()->abortHumanCheck();
    }
}

void MainWindow::restoreDefaultAccount()
{
    m_gameAccountName.clear();
    m_gameAccountAvatar = QPixmap();

    AccountInfo defaultAccount = SettingsManager::instance()->getDefaultAccount();
    if (!defaultAccount.username.isEmpty())
    {
        m_gameAccountName = defaultAccount.username;
        // 顶栏头像：截取默认账户皮肤头部正面（圆角由 TopBar 内部处理）
        const QImage skin = SkinDownloader::loadSkinForAccount(defaultAccount.username, defaultAccount.uuid);
        const QImage head = SkinDownloader::cropHeadPortrait(skin);
        if (!head.isNull())
        {
            m_gameAccountAvatar = QPixmap::fromImage(head);
        }
    }

    syncTopBarAccount();
}

void MainWindow::syncTopBarAccount()
{
    if (!m_topBar)
        return;

    ForumAccountService *forum = ForumAccountService::instance();

    // 已登录方块盒子账户：顶栏账户卡片展示社区身份（昵称 + 头像）
    QString forumName;
    if (forum->isLoggedIn())
    {
        const ForumUser user = forum->user();
        forumName = user.displayName.trimmed();
        if (forumName.isEmpty())
            forumName = user.username.trimmed();
    }

    if (!forumName.isEmpty())
    {
        m_topBar->setAccountName(forumName);
        m_topBar->setAccountToolTip(tr("方块盒子账户"));
        // 头像下载完成前先用首字母占位，避免卡片空一块
        m_topBar->setAccountAvatarPlaceholder(forumName);
        loadTopBarForumAvatar(forum->user().avatar);
        return;
    }

    // 未登录方块盒子账户：保持原来的游戏账户展示
    m_topBar->setAccountName(m_gameAccountName.isEmpty() ? tr("未登录") : m_gameAccountName);
    m_topBar->setAccountToolTip(tr("账户管理"));
    if (m_gameAccountAvatar.isNull())
        m_topBar->clearAccountAvatar();
    else
        m_topBar->setAccountAvatar(m_gameAccountAvatar);
}

void MainWindow::loadTopBarForumAvatar(const QString &avatarPath)
{
    if (avatarPath.trimmed().isEmpty())
        return;

    ForumAccountService *service = ForumAccountService::instance();
    const int userId = service->user().id;
    const QString url = service->absoluteUrl(avatarPath);
    if (url.isEmpty())
        return;

    // 上次可能因为断网失败被记成“不再重试”，登录 / 刷新时重新给一次机会
    ForumImageStore::instance()->clearFailures();
    ForumImageStore::instance()->requestImage(url, [this, userId](const QImage &image) {
        ForumAccountService *current = ForumAccountService::instance();
        // 下载期间换了账户或退出了，别把旧头像盖到新账户上
        if (!current->isLoggedIn() || current->user().id != userId)
            return;
        if (image.isNull())
            return; // 失败时保留首字母占位
        m_topBar->setAccountAvatar(QPixmap::fromImage(image));
    });
}

void MainWindow::initGameLauncher()
{
    // Initialize game launcher
    m_gameLauncher = GameLauncher::instance();

    // 主单例作为「第一个启动会话」复用：单例会被反复用于不同实例的启动，
    // 路径不能在连接时捕获死，改为信号触发时从会话自身解析当前实例路径。
    // 并行启动的额外会话（GameLauncher::createSession）由 connectSessionSignals
    // 按固定路径逐个接线。
    connect(m_gameLauncher, &GameLauncher::launchStatusChanged, this, [this](GameLauncher::LaunchStatus status) {
        onSessionLaunchStatusChanged(m_gameLauncher->currentInstancePath(), status);
    });
    connect(m_gameLauncher, &GameLauncher::gameStarted, this, [this]() {
        onSessionGameStarted(m_gameLauncher->currentInstancePath());
    });
    connect(m_gameLauncher, &GameLauncher::gameStopped, this, [this](int exitCode) {
        onSessionGameStopped(m_gameLauncher->currentInstancePath(), exitCode);
    });
    connect(m_gameLauncher, &GameLauncher::gameCrashed, this, [this](const QString &error) {
        onSessionGameCrashed(m_gameLauncher->currentInstancePath(), error);
    });
    connect(m_gameLauncher, &GameLauncher::launchProgressChanged, this, [this](int progress, const QString &message) {
        onSessionLaunchProgressChanged(m_gameLauncher->currentInstancePath(), progress, message);
    });
    connect(m_gameLauncher, &GameLauncher::launchDetailAdded, this, [this](const QString &detail) {
        onSessionLaunchDetailAdded(m_gameLauncher->currentInstancePath(), detail);
    });
    connect(m_gameLauncher, &GameLauncher::errorReportGenerated, this, [this](const QString &report) {
        onSessionErrorReportGenerated(m_gameLauncher->currentInstancePath(), report);
    });
    connect(m_gameLauncher, &GameLauncher::fileCompletionProgress, this, [this](int completed, int total, const QString &currentFile) {
        onSessionFileCompletionProgress(m_gameLauncher->currentInstancePath(), completed, total, currentFile);
    });
    connect(m_gameLauncher, &GameLauncher::fileCompletionFinished, this, [this](bool success) {
        onSessionFileCompletionFinished(m_gameLauncher->currentInstancePath(), success);
    });
    // 第三方账户注入 authlib-injector 失败：只提示，不打断启动
    connect(m_gameLauncher, &GameLauncher::thirdPartyInjectWarning, this,
            [this](const QString &message) { NotificationManager::showInfo(this, message, 9000); });

    // 启动前自动备份存档：备份期间把进度显示在对应实例的启动任务卡 / 安卓启动页上，
    // 备份结束（含未启用、无需备份）后才真正启动游戏。
    // 备份管理器是全局单例，同一时间只有一份备份在进行，按 m_backupPendingPath 路由。
    connect(WorldBackupManager::instance(), &WorldBackupManager::backupProgress, this,
            [this](int percent, const QString &message) {
                const QString path = m_backupPendingPath;
                if (path.isEmpty())
                    return;
                if (m_taskBar) {
                    if (percent >= 0)
                        m_taskBar->updateLaunchCardProgress(path, qBound(0, percent, 100));
                    if (!message.isEmpty())
                        m_taskBar->updateLaunchCardMessage(path, message);
                }
                if (m_taskListPage) {
                    m_taskListPage->setLaunchTaskInfo(instanceDisplayName(path), LaunchTaskCard::Launching,
                                                      qMax(0, percent), message);
                }
                if (m_gameLaunchPage) {
                    m_gameLaunchPage->appendLog(message);
                    m_gameLaunchPage->setProgress(qMax(0, percent), message);
                }
            });
    connect(WorldBackupManager::instance(), &WorldBackupManager::autoBackupBatchFinished, this,
            [this](int backed, int failed) {
                const QString path = m_backupPendingPath;
                m_backupPendingPath.clear();
                if (path.isEmpty())
                    return;
                LaunchSessionUiState *state = sessionState(path);
                if (!state)
                    return;
                state->backupPending = false;
                std::function<void()> action = std::move(state->launchAfterBackup);
                state->launchAfterBackup = nullptr;
                // 备份失败不拦启动：玩家进游戏比留备份更重要
                if (failed > 0) {
                    NotificationManager::showError(
                        this, tr("自动备份存档失败 %1 份，本次直接启动游戏（不影响游玩）").arg(failed));
                } else if (backed > 0) {
                    NotificationManager::showInfo(this, tr("已自动备份 %1 个存档").arg(backed));
                }
                if (action)
                    action();
            });

    // 游戏成就同步：不依赖实例助手，游戏启动后一直在后台跑
    m_gameAchievements = new GameAchievementService(this, this);

    // Connect launch task card signals via task bar
    if (m_taskBar) {
        connect(m_taskBar, &TaskBar::launchTaskCardClicked, this, &MainWindow::onShowDetailsPage);
    }

    // 联机大厅「正在游玩」上报：进游戏后每 40 秒续一次，2 分钟没上报服务端就当人已经走了
    m_lobbyPresenceTimer = new QTimer(this);
    m_lobbyPresenceTimer->setInterval(40000);
    connect(m_lobbyPresenceTimer, &QTimer::timeout, this, [this]() {
        if (m_lobbyPlayRoomId <= 0)
            return;
        ForumLobbyService::instance()->sendPresence(
            m_lobbyPlayRoomId, nullptr, [this](const QString &, int status) {
                // 房间已经下架 / 过期：本地也不用再报了
                if (status == 404)
                    stopLobbyPlayReport();
            });
    });
}

void MainWindow::startLobbyPlayReport(int roomId)
{
    if (roomId <= 0)
        return;
    // 没登录论坛账号就没有可显示的身份，服务端也会拒收，干脆不报
    if (!ForumAccountService::instance()->isLoggedIn())
        return;

    // 从大厅换到另一个房间：先把旧房间的记录撤掉，不要一个人挂在两个房间里
    if (m_lobbyPlayRoomId > 0 && m_lobbyPlayRoomId != roomId)
        ForumLobbyService::instance()->clearPresence(m_lobbyPlayRoomId, nullptr, nullptr);

    m_lobbyPlayRoomId = roomId;
    ForumLobbyService::instance()->sendPresence(roomId, nullptr, nullptr);
    if (m_lobbyPresenceTimer && !m_lobbyPresenceTimer->isActive())
        m_lobbyPresenceTimer->start();
}

void MainWindow::stopLobbyPlayReport()
{
    if (m_lobbyPresenceTimer)
        m_lobbyPresenceTimer->stop();

    const int roomId = m_lobbyPlayRoomId;
    m_lobbyPlayRoomId = 0;
    if (roomId > 0)
        ForumLobbyService::instance()->clearPresence(roomId, nullptr, nullptr);
}

void MainWindow::onSessionLaunchStatusChanged(const QString &instancePath, GameLauncher::LaunchStatus status)
{
    qDebug() << "[MainWindow]" << "启动状态已变更：" << instancePath << int(status);

    // 启动就没起来：这次大厅房间其实没进去，别把人算进「正在游玩」
    if (status == GameLauncher::Failed) {
        if (LaunchSessionUiState *state = sessionState(instancePath)) {
            if (state->lobbyRoomId > 0) {
                state->lobbyRoomId = 0;
                stopLobbyPlayReport();
            }
            state->pendingLobbyRoomId = 0;
        }
    }

    // 安卓整页启动界面同步状态（桌面端 m_gameLaunchPage 恒为空）
    if (m_gameLaunchPage) {
        m_gameLaunchPage->setLaunchStatus(status);
    }

    LaunchTaskCard::LaunchStatus ls = LaunchTaskCard::Idle;
    switch (status) {
    case GameLauncher::Idle:     ls = LaunchTaskCard::Idle; break;
    case GameLauncher::Launching: ls = LaunchTaskCard::Launching; break;
    case GameLauncher::Running:  ls = LaunchTaskCard::Running; break;
    case GameLauncher::Failed:   ls = LaunchTaskCard::Failed; break;
    case GameLauncher::Stopped:  ls = LaunchTaskCard::Stopped; break;
    }
    if (m_taskBar) {
        m_taskBar->updateLaunchCardStatus(instancePath, ls);
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(instanceDisplayName(instancePath), ls, 0);
    }
    if (m_launchDetailsPage && instancePath == m_focusedLaunchSession) {
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
}

void MainWindow::onSessionLaunchProgressChanged(const QString &instancePath, int progress, const QString& message)
{
    // 安卓整页启动界面同步进度（桌面端 m_gameLaunchPage 恒为空；安卓单会话）
    if (m_gameLaunchPage) {
        m_gameLaunchPage->setProgress(progress, message);
    }
    if (m_taskBar) {
        m_taskBar->updateLaunchCardProgress(instancePath, progress);
        m_taskBar->updateLaunchCardMessage(instancePath, message);
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(instanceDisplayName(instancePath), LaunchTaskCard::Launching, progress, message);
    }
}

void MainWindow::onSessionLaunchDetailAdded(const QString &instancePath, const QString& detail)
{
    // 安卓整页启动界面缓存日志（供「查看日志」弹窗展示）
    if (m_gameLaunchPage) {
        m_gameLaunchPage->appendLog(detail);
    }
    if (m_taskBar) {
        m_taskBar->addLaunchCardDetail(instancePath, detail);
    }
    // 详情页只显示当前聚焦会话（点哪张启动卡片就看谁的日志）
    if (m_launchDetailsPage && instancePath == m_focusedLaunchSession) {
        m_launchDetailsPage->addDetail(detail);
    }
    // 实例日志页跟随当前实例
    if (m_instanceManagePage && instancePath == m_currentInstancePath
        && m_instanceManagePage->instanceLogPage()) {
        m_instanceManagePage->instanceLogPage()->addLauncherLog(detail);
    }
    // 成就日志解析跟随最新启动的会话，多开时不把两个游戏的日志混在一起
    if (m_gameAchievements && instancePath == m_focusedLaunchSession) {
        m_gameAchievements->onGameLogLine(detail);
    }
}

void MainWindow::onSessionFileCompletionProgress(const QString &instancePath, int completed, int total, const QString &currentFile)
{
    // 文件补全进度统一显示在该实例的启动任务卡片上（35%~50% 区间）
    if (m_taskBar) {
        if (total > 0) {
            // 将文件补全进度映射到启动整体进度的 35%~50% 区间
            int filePercent = qBound(0, completed * 100 / total, 100);
            int overall = 35 + filePercent * 15 / 100;
            m_taskBar->updateLaunchCardProgress(instancePath, overall);
        }
        if (!currentFile.isEmpty()) {
            m_taskBar->updateLaunchCardMessage(instancePath, tr("补全文件: %1").arg(currentFile));
        }
    }
    if (m_taskListPage && total > 0) {
        int filePercent = qBound(0, completed * 100 / total, 100);
        int overall = 35 + filePercent * 15 / 100;
        QString msg = currentFile.isEmpty() ? tr("补全文件中...") : tr("补全文件: %1").arg(currentFile);
        m_taskListPage->setLaunchTaskInfo(instanceDisplayName(instancePath), LaunchTaskCard::Launching, overall, msg);
    }
    // 安卓整页启动界面：同样映射到 35%~50% 区间
    if (m_gameLaunchPage && total > 0) {
        int filePercent = qBound(0, completed * 100 / total, 100);
        int overall = 35 + filePercent * 15 / 100;
        QString msg = currentFile.isEmpty() ? tr("补全文件中...") : tr("补全文件: %1").arg(currentFile);
        m_gameLaunchPage->setProgress(overall, msg);
    }
}

void MainWindow::onSessionFileCompletionFinished(const QString &instancePath, bool success)
{
    // 文件补全只是启动流程的一个中间步骤，仅更新启动卡片消息
    // 任务由后续 onSessionGameStarted/onSessionGameCrashed 统一收尾
    if (m_gameLaunchPage) {
        if (success) {
            m_gameLaunchPage->setProgress(50, tr("文件补全完成，继续启动游戏..."));
        } else {
            m_gameLaunchPage->setProgress(50, tr("文件补全失败"));
        }
    }
    if (m_taskBar) {
        if (success) {
            m_taskBar->updateLaunchCardMessage(instancePath, tr("文件补全完成，继续启动游戏..."));
        } else {
            m_taskBar->updateLaunchCardStatus(instancePath, LaunchTaskCard::Failed);
            m_taskBar->updateLaunchCardMessage(instancePath, tr("文件补全失败"));
        }
    }
    if (m_taskListPage) {
        if (success) {
            m_taskListPage->setLaunchTaskInfo(instanceDisplayName(instancePath), LaunchTaskCard::Launching, 50, tr("文件补全完成，继续启动游戏..."));
        } else {
            m_taskListPage->setLaunchTaskInfo(instanceDisplayName(instancePath), LaunchTaskCard::Failed, 50, tr("文件补全失败"));
        }
    }
}

void MainWindow::onSessionErrorReportGenerated(const QString &instancePath, const QString& report)
{
    qDebug() << "[MainWindow]" << "已生成错误报告：" << report;
    // Show error report in a message box or add to launch details
    if (m_taskBar) {
        m_taskBar->addLaunchCardDetail(instancePath, tr("错误分析报告:"));
        m_taskBar->addLaunchCardDetail(instancePath, report);
    }
    // Optionally show a message box with the error report
    AppMessageBox::information(this, tr("错误分析"), report);
}

#ifdef Q_OS_ANDROID
void MainWindow::toggleGameAssistantOverlay()
{
    // 安卓游戏内侧栏助手是独立于桌面多开助手窗口的单例窗口
    if (!m_overlayAssistantWindow) {
        m_overlayAssistantWindow = new InstanceAssistantWindow(this);
        // 安卓游戏内使用侧栏形态：贴屏幕右侧四分之一、半透明
        m_overlayAssistantWindow->setOverlayMode(true);
        // 转发资源管理页的存档快捷启动信号到主窗口处理
        connect(m_overlayAssistantWindow, &InstanceAssistantWindow::quickLaunchSaveRequested,
                this, &MainWindow::onQuickLaunchSaveClicked);
        connect(m_overlayAssistantWindow, &InstanceAssistantWindow::forumLoginRequested,
                this, &MainWindow::openForumAccountLogin);
    }
    // 再次点击悬浮图标收起侧栏
    if (m_overlayAssistantWindow->isVisible()) {
        m_overlayAssistantWindow->hide();
        return;
    }

    // 注入当前实例上下文（路径、版本、loader）
    m_overlayAssistantWindow->setInstanceContext(
        m_currentInstancePath,
        m_currentInstanceVersion,
        m_currentInstanceLoader);
    m_overlayAssistantWindow->show();
    m_overlayAssistantWindow->raise();
    m_overlayAssistantWindow->activateWindow();
}
#endif

void MainWindow::onTouchControlInput(const QString &action, qreal x, qreal y, bool pressed)
{
    // 游戏内触屏键位 → 游戏输入桥：统一走 GameLauncher 写进游戏进程 stdin，
    // 没开游戏时 GameLauncher 内部直接忽略，无需在这里判状态
    if (m_gameLauncher)
        m_gameLauncher->sendTouchInput(action, x, y, pressed);
}

void MainWindow::onSessionGameStarted(const QString &instancePath)
{
    qDebug() << "[MainWindow]" << "游戏已成功启动：" << instancePath;
    // 匿名统计：只记「启动了一次游戏」，不带实例名、路径或版本
    Telemetry::recordEvent(QStringLiteral("game_start"));

    GameLauncher *session = m_launchSessions.value(instancePath, m_gameLauncher);
    const bool secondarySession = session && session != m_gameLauncher;
    LaunchSessionUiState *state = sessionState(instancePath);

    // 从联机大厅「一键加入」进来的：游戏真跑起来了，开始在大厅里显示「正在游玩」
    const bool fromLobby = (state && (state->pendingLobbyRoomId > 0 || state->lobbyRoomId > 0))
        || m_lobbyPlayRoomId > 0
        || (m_gameAchievements && m_gameAchievements->hasLobbyContext());
    if (state && state->pendingLobbyRoomId > 0) {
        startLobbyPlayReport(state->pendingLobbyRoomId);
        state->lobbyRoomId = state->pendingLobbyRoomId;
        state->pendingLobbyRoomId = 0;
    }

    // 游戏成就同步：单机读存档；方块盒子联机读游戏日志（房间上下文在加入时已记下）。
    // 多开时成就跟随最新启动的会话，日志路由（onSessionLaunchDetailAdded）会按
    // m_focusedLaunchSession 过滤，不会把两个游戏的日志混在一起
    if (m_gameAchievements) {
        if (!fromLobby)
            m_gameAchievements->setLobbyContext(0, QString());
        QString playerName = m_gameAccountName;
        if (playerName.isEmpty())
            playerName = SettingsManager::instance()->getDefaultAccount().username;
        m_gameAchievements->setSessionContext(instancePath, instanceVersionOf(instancePath),
                                             playerName);
        m_gameAchievements->startSession();
    }

    // 游玩时长统计：服务只支持一次记一个会话，由第一个跑起来的游戏「认领」；
    // 认领的游戏退出时结算，其余并行游戏的时长统计到下一次认领为止
    if (m_playtimeSessionPath.isEmpty()) {
        PlaytimeStatsService::instance()->beginSession(
            instancePath, QFileInfo(instancePath).fileName(),
            instanceVersionOf(instancePath), instanceLoaderOf(instancePath),
            (state && state->lobbyRoomId > 0) ? state->lobbyRoomId : m_lobbyPlayRoomId);
        m_playtimeSessionPath = instancePath;
    }
#ifdef Q_OS_ANDROID
    Q_UNUSED(secondarySession);
    // 游戏启动成功：先铺触屏键位悬浮层（读「操作设置」布局），再亮出
    // 方块盒子悬浮图标（showEvent 自动 raise，保持在键位层之上）
    if (m_touchControlOverlay) {
        m_touchControlOverlay->show();
    }
    if (m_gameFloatingIcon) {
        m_gameFloatingIcon->show();
    }
    // 整页启动界面同步为运行状态（随后 onBackToMain 切回主页，启动页随之消失，
    // 游戏画面 + 键位 + 悬浮图标成为用户看到的内容）
    if (m_gameLaunchPage) {
        m_gameLaunchPage->setLaunchStatus(GameLauncher::Running);
        // 清掉启动页记录的"来路页面"，避免回主页后返回键行为错乱
        m_hasPreviousPage = false;
    }
#endif
    // 游戏窗口已出现，该实例的启动任务卡片切换为"运行中"状态
    if (m_taskBar) {
        m_taskBar->updateLaunchCardStatus(instancePath, LaunchTaskCard::Running);
        m_taskBar->updateLaunchCardProgress(instancePath, 100);
        m_taskBar->updateLaunchCardMessage(instancePath, tr("游戏运行中"));
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(instanceDisplayName(instancePath), LaunchTaskCard::Running, 100, tr("游戏运行中"));
    }
    if (m_launchDetailsPage && instancePath == m_focusedLaunchSession) {
        m_launchDetailsPage->setLaunchStatus("游戏运行中", true);
    }

    // 并行启动的会话：提示一声即可，不打断用户当前所在的页面
    if (secondarySession) {
        NotificationManager::showSuccess(this,
            tr("%1 已成功启动").arg(instanceDisplayName(instancePath)));
        return;
    }

    onBackToMain();
}

void MainWindow::onSessionGameStopped(const QString &instancePath, int exitCode)
{
    qDebug() << "[MainWindow]" << "游戏已停止，退出码：" << exitCode << instancePath;

    GameLauncher *session = m_launchSessions.value(instancePath, m_gameLauncher);
    const bool secondarySession = session && session != m_gameLauncher;
    LaunchSessionUiState *state = sessionState(instancePath);
    const QString instanceName = instanceDisplayName(instancePath);

    // 成就：还有别的游戏在跑就继续同步（跟随最新启动的会话），全停了才结束
    if (m_gameAchievements && !GameLauncher::anySessionBusy())
        m_gameAchievements->stopSession();

    // 游玩时长统计：认领本会话统计的游戏退出时结算
    if (m_playtimeSessionPath == instancePath) {
        PlaytimeStatsService::instance()->endSession();
        m_playtimeSessionPath.clear();
    }

    // 游戏退出 = 不在这个房间里玩了：大厅里的头像马上撤掉
    if (state) {
        state->pendingLobbyRoomId = 0;
        if (state->lobbyRoomId > 0) {
            state->lobbyRoomId = 0;
            stopLobbyPlayReport();
        }
    }

#ifdef Q_OS_ANDROID
    // 游戏退出：隐藏游戏内触屏键位、悬浮图标与实例助手侧栏
    if (m_touchControlOverlay) {
        m_touchControlOverlay->hide();
    }
    if (m_gameFloatingIcon) {
        m_gameFloatingIcon->hide();
    }
    if (m_overlayAssistantWindow && m_overlayAssistantWindow->isVisible()) {
        m_overlayAssistantWindow->hide();
    }
#endif

    // 用户手动取消启动: 不显示错误信息，直接清除启动任务卡片
    if (session && session->wasUserCancelled()) {
        if (m_taskBar) {
            m_taskBar->removeLaunchCard(instancePath);
        }
        if (m_taskListPage) {
            m_taskListPage->setLaunchTaskInfo(instanceName, LaunchTaskCard::Idle, 0);
        }
        if (m_launchDetailsPage && instancePath == m_focusedLaunchSession) {
            m_launchDetailsPage->setLaunchStatus(tr("启动日志"), false);
        }
        finishLaunchSession(instancePath);
        return;
    }

    // 游戏正常退出（exitCode == 0）: 显示"游戏已正常退出"提示，并删除当前启动任务
    if (exitCode == 0) {
        NotificationManager::showSuccess(this, secondarySession
            ? tr("%1 已正常退出").arg(instanceName) : tr("游戏已正常退出"));
        if (m_taskBar) {
            m_taskBar->removeLaunchCard(instancePath);
        }
        if (m_taskListPage) {
            m_taskListPage->setLaunchTaskInfo(instanceName, LaunchTaskCard::Idle, 0);
        }
        if (m_launchDetailsPage && instancePath == m_focusedLaunchSession) {
            m_launchDetailsPage->setLaunchStatus(tr("游戏已停止"), false);
        }
        // 刷新首页最近游玩区域（游戏退出后 level.dat 的 LastPlayed 已更新）
        if (m_homePage) {
            m_homePage->refreshRecentPlays();
        }
        finishLaunchSession(instancePath);
        return;
    }

    // 游戏异常退出（exitCode != 0 且非用户取消）: 保留任务卡片显示异常退出信息
    if (m_taskBar) {
        m_taskBar->updateLaunchCardStatus(instancePath, LaunchTaskCard::Stopped);
        m_taskBar->updateLaunchCardMessage(instancePath, tr("游戏异常退出: %1").arg(exitCode));
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(instanceName, LaunchTaskCard::Stopped, 100, tr("游戏异常退出: %1").arg(exitCode));
    }
    if (m_launchDetailsPage && instancePath == m_focusedLaunchSession) {
        m_launchDetailsPage->setLaunchStatus(tr("游戏已停止"), false);
    }
    if (secondarySession) {
        NotificationManager::showError(this,
            tr("%1 异常退出（代码 %2）").arg(instanceName).arg(exitCode));
    }
    finishLaunchSession(instancePath);
}

void MainWindow::onSessionGameCrashed(const QString &instancePath, const QString &error)
{
    qDebug() << "[MainWindow]" << "游戏崩溃：" << error << instancePath;
    // 匿名统计：只记「游戏崩了一次」，不上传任何日志、报错原文或实例信息
    Telemetry::recordEvent(QStringLiteral("game_crash"));

    GameLauncher *session = m_launchSessions.value(instancePath, m_gameLauncher);
    LaunchSessionUiState *state = sessionState(instancePath);
    const QString instanceName = instanceDisplayName(instancePath);

    // 崩溃也算这次进房结束：先把「正在游玩」撤掉；启动期崩溃会自动重启，起来后接着报
    int crashedLobbyRoomId = 0;
    if (state) {
        crashedLobbyRoomId = state->pendingLobbyRoomId > 0 ? state->pendingLobbyRoomId
                                                           : state->lobbyRoomId;
        state->pendingLobbyRoomId = 0;
        state->lobbyRoomId = 0;
        if (crashedLobbyRoomId > 0)
            stopLobbyPlayReport();
    }

#ifdef Q_OS_ANDROID
    // 游戏崩溃：隐藏游戏内触屏键位、悬浮图标与实例助手侧栏
    if (m_touchControlOverlay) {
        m_touchControlOverlay->hide();
    }
    if (m_gameFloatingIcon) {
        m_gameFloatingIcon->hide();
    }
    if (m_overlayAssistantWindow && m_overlayAssistantWindow->isVisible()) {
        m_overlayAssistantWindow->hide();
    }
#endif

    // 重试已调度但尚未执行时，忽略重复的 gameCrashed 信号
    // （QProcess 崩溃时 onProcessError 和 onProcessFinished 都会触发 gameCrashed）
    if (state && state->retryPending) {
        qDebug() << "[MainWindow]" << "重试已在等待中，忽略重复的 gameCrashed 信号";
        return;
    }

    // 仅在"启动期崩溃"（游戏窗口尚未出现）且尚未重试时，自动检查文件完整性并重新启动
    // 运行期崩溃（游戏已成功运行后崩溃）不触发自动重试，交由错误分析器处理；
    // Java 缺失自救失败也不触发——自救已经查找并尝试过安装，补文件没有意义
    if (session && state && !state->retryAttempted && !session->wasGameStartedSuccessfully()
        && !session->wasJavaRescueAttempted()) {
        state->retryAttempted = true;
        state->retryPending = true;
        qDebug() << "[MainWindow]" << "检测到启动期崩溃，正在补全文件并重试...";

        NotificationManager::showInfo(this, tr("游戏启动失败，正在检查文件完整性并重新启动..."));

        // 延迟到下一个事件循环执行重试，确保当前 onProcessFinished/onProcessError
        // 中的 cleanup() 已执行完毕，避免与新启动的进程冲突
        QTimer::singleShot(0, this, [this, instancePath, error, crashedLobbyRoomId, session]() {
            LaunchSessionUiState *st = sessionState(instancePath);
            if (!st)
                return;
            st->retryPending = false;
            // 上次是从联机大厅「一键加入」进来的：重启的进程起来后接着在大厅里显示「正在游玩」
            if (crashedLobbyRoomId > 0)
                st->pendingLobbyRoomId = crashedLobbyRoomId;

            GameLauncher::LaunchConfig config;
            config.instancePath = instancePath;
            config.accountName = m_gameAccountName.isEmpty() ? "Player" : m_gameAccountName;

            SettingsManager *settings = SettingsManager::instance();
            config.javaPath = settings->getJavaPath();
            config.maxMemory = SettingsManager::DEFAULT_MAX_MEMORY_MB;
            config.minMemory = SettingsManager::DEFAULT_MIN_MEMORY_MB;

            // 重试进度显示在该实例的启动任务卡片上，不创建下载任务
            if (m_taskBar) {
                m_taskBar->updateLaunchCardStatus(instancePath, LaunchTaskCard::Launching);
                m_taskBar->updateLaunchCardProgress(instancePath, 35);
                m_taskBar->updateLaunchCardMessage(instancePath, tr("检查并补全文件..."));
            }
            if (m_taskListPage) {
                m_taskListPage->setLaunchTaskInfo(instanceDisplayName(instancePath), LaunchTaskCard::Launching, 35, tr("检查并补全文件..."));
            }

            // 重试: skipFileCompletion=false，执行文件完整性检查并补全缺失文件
            if (!session->launchGame(config, false)) {
                QString failMsg = session->errorMessage().isEmpty()
                    ? tr("重新启动失败") : session->errorMessage();
                if (m_taskBar) {
                    m_taskBar->updateLaunchCardStatus(instancePath, LaunchTaskCard::Failed);
                    m_taskBar->updateLaunchCardMessage(instancePath, failMsg);
                }
                if (m_taskListPage) {
                    m_taskListPage->setLaunchTaskInfo(instanceDisplayName(instancePath), LaunchTaskCard::Failed, 0, failMsg);
                }
                NotificationManager::showError(this,
                    tr("重新启动失败: ") +
                    (session->errorMessage().isEmpty() ? error : session->errorMessage()));
            }
        });
        return;
    }

    // 非启动期崩溃，或已重试过: 更新启动任务卡片为失败状态（错误分析由 ErrorAnalyzer 处理）
    QString failMsg = error.isEmpty() ? tr("游戏崩溃") : error;
    if (m_taskBar) {
        m_taskBar->updateLaunchCardStatus(instancePath, LaunchTaskCard::Failed);
        m_taskBar->updateLaunchCardMessage(instancePath, failMsg);
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(instanceName, LaunchTaskCard::Failed, 0, failMsg);
    }
    GameLauncher *crashedSession = m_launchSessions.value(instancePath, nullptr);
    if (crashedSession && crashedSession != m_gameLauncher) {
        NotificationManager::showError(this,
            tr("%1 启动失败：%2").arg(instanceName, failMsg));
        finishLaunchSession(instancePath);
    }
}

void MainWindow::onLaunchTaskCardClicked()
{
    qDebug() << "[MainWindow]" << "启动任务卡片已点击";
    // Here you can add additional functionality when the task card is clicked
    // For example, show more detailed information or open a separate window
}

bool MainWindow::ensureLaunchPrerequisites()
{
    // 启动前置校验：账户没就绪就直说并带用户去补，而不是进启动流程后
    // 让预检失败（旧流程会把配置错误当崩溃处理，连发多条错误通知）。
    if (SettingsManager::instance()->getDefaultAccount().username.isEmpty()) {
        NotificationManager::showInfo(this, tr("尚未登录账户，请先添加账户后再启动游戏"));
        // 没有账户就是来加账户的：直接进「添加角色」页，加好再回来启动
        openAddAccountPage();
        return false;
    }
    return true;
}

void MainWindow::onLaunchGameClicked()
{
    if (m_currentInstancePath.isEmpty()) {
        showInstanceSelectPage();
        return;
    }

    // 普通启动：清掉上一次可能残留的大厅房间上下文，单机成就走存档监听
    if (m_gameAchievements)
        m_gameAchievements->setLobbyContext(0, QString());

    startInstanceLaunch(m_currentInstancePath);
}

void MainWindow::onLaunchGameClicked(const QString &instancePath)
{
    // 来自 InstanceSelectPage 的启动请求：使用指定的实例路径
    // 先更新当前实例，再复用统一启动入口
    if (!instancePath.isEmpty()) {
        updateCurrentInstance(instancePath);
    }
    onLaunchGameClicked();
}

QString MainWindow::instanceDisplayName(const QString &instancePath)
{
    const QString name = QFileInfo(instancePath).fileName();
    return name.isEmpty() ? QObject::tr("游戏") : name;
}

QString MainWindow::instanceVersionOf(const QString &instancePath)
{
    // 与 applyInstanceContext 的版本解析规则一致：优先版本 JSON 里声明的
    // inheritsFrom / clientVersion / id，读不到就用目录名兜底
    const QString instanceName = QFileInfo(instancePath).fileName();
    QFile versionJsonFile(instancePath + "/" + instanceName + ".json");
    if (versionJsonFile.open(QIODevice::ReadOnly)) {
        const QJsonObject rootObj = QJsonDocument::fromJson(versionJsonFile.readAll()).object();
        for (const QString &key : { QStringLiteral("inheritsFrom"),
                                    QStringLiteral("clientVersion"),
                                    QStringLiteral("id") }) {
            const QString v = rootObj.value(key).toString();
            if (!v.isEmpty())
                return v;
        }
    }
    return instanceName;
}

QString MainWindow::instanceLoaderOf(const QString &instancePath)
{
    // 与 applyInstanceContext 的加载器检测规则一致：扫版本 JSON 的键值文本
    const QString instanceName = QFileInfo(instancePath).fileName();
    QFile versionJsonFile(instancePath + "/" + instanceName + ".json");
    if (versionJsonFile.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromUtf8(versionJsonFile.readAll()).toLower();
        if (text.contains("neoforge")) return QStringLiteral("NeoForge");
        if (text.contains("quilt"))    return QStringLiteral("Quilt");
        if (text.contains("fabric"))   return QStringLiteral("Fabric");
        if (text.contains("forge"))    return QStringLiteral("Forge");
    }
    return QString();
}

bool MainWindow::startInstanceLaunch(const QString &instancePath, const QStringList &extraGameArgs)
{
    if (instancePath.isEmpty()) {
        showInstanceSelectPage();
        return false;
    }

#ifdef Q_OS_ANDROID
    // 安卓：整页启动界面与触屏键位/悬浮图标都按单游戏设计，保持一次只启动一个
    if (GameLauncher::anySessionBusy()) {
        NotificationManager::showError(this, tr("游戏已经在运行或正在启动中"));
        return false;
    }
#else
    // 同一实例不允许并行开两个（存档/配置会互相踩）；不同实例桌面端可以多开
    if (GameLauncher::sessionForInstance(instancePath)) {
        NotificationManager::showError(this,
            tr("%1 已经在运行或正在启动中").arg(instanceDisplayName(instancePath)));
        return false;
    }
#endif

    if (!ensureLaunchPrerequisites())
        return false;

    // 选会话对象：主单例空闲则复用（既有信号接线与行为保持不变），
    // 否则为并行启动新造一个会话对象，按实例路径接线
    GameLauncher *session = nullptr;
    const bool reusePrimary = m_gameLauncher
        && m_gameLauncher->status() != GameLauncher::Running
        && m_gameLauncher->status() != GameLauncher::Launching;
    if (reusePrimary) {
        session = m_gameLauncher;
    } else {
        session = GameLauncher::createSession();
        connectSessionSignals(session, instancePath);
    }

    // 该实例旧的已结束会话对象：直接回收，避免反复启动堆积
    if (GameLauncher *stale = m_launchSessions.value(instancePath, nullptr)) {
        if (stale != m_gameLauncher && stale != session
            && stale->status() != GameLauncher::Running
            && stale->status() != GameLauncher::Launching) {
            m_launchSessions.remove(instancePath);
            stale->deleteLater();
        }
    }
    m_launchSessions.insert(instancePath, session);

    // 重置该实例本次启动的会话状态（重试/备份/大厅待上报）
    m_launchSessionStates.insert(instancePath, LaunchSessionUiState());
    // 大厅/好友房间「一键加入」：房间号跟着这次启动走进会话状态
    if (LaunchSessionUiState *st = sessionState(instancePath); st && m_nextLaunchLobbyRoomId > 0) {
        st->pendingLobbyRoomId = m_nextLaunchLobbyRoomId;
        m_nextLaunchLobbyRoomId = 0;
    }
    // 启动详情页/成就日志跟随最新启动的会话
    m_focusedLaunchSession = instancePath;

    SettingsManager *settings = SettingsManager::instance();
    // 统计该实例的启动次数（概览页「启动次数」卡片展示）
    const QString launchCountKey = "instance/" + instancePath + "/launchCount";
    settings->setProperty(launchCountKey,
                          settings->getProperty(launchCountKey, 0).toInt() + 1);

    GameLauncher::LaunchConfig config;
    config.instancePath = instancePath;
    config.accountName = m_gameAccountName.isEmpty() ? "Player" : m_gameAccountName;
    config.javaPath = settings->getJavaPath();
    config.maxMemory = SettingsManager::DEFAULT_MAX_MEMORY_MB;
    config.minMemory = SettingsManager::DEFAULT_MIN_MEMORY_MB;
    if (!extraGameArgs.isEmpty())
        config.gameArgs = extraGameArgs;

    const QString instanceName = instanceDisplayName(instancePath);

#ifdef Q_OS_ANDROID
    // 安卓：不用底部任务卡形式，进入整页启动界面展示进度
    showGameLaunchPage();
    if (m_gameLaunchPage)
        m_gameLaunchPage->beginLaunch();
#else
    // 启动进度统一通过该实例自己的启动任务卡片显示，不创建下载任务
    if (m_taskBar) {
        m_taskBar->ensureLaunchCard(instancePath, instanceName);
        m_taskBar->showLaunchCard(instancePath, true);
        m_taskBar->updateLaunchCardStatus(instancePath, LaunchTaskCard::Idle);
        m_taskBar->updateLaunchCardProgress(instancePath, 0);
        m_taskBar->updateLaunchCardMessage(instancePath, tr("准备启动游戏..."));
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(instanceName, LaunchTaskCard::Idle, 0);
    }
#endif

    // 首次启动: skipFileCompletion=true，跳过文件完整性检查与补全，
    // 若启动崩溃再自动重试时启用补全（见 onSessionGameCrashed）
    // 开了「启动前自动备份」时先等备份跑完再启动（备份失败也照常启动）
    launchWithAutoBackup(instancePath, [this, session, config, instancePath]() {
        if (!session->launchGame(config, true)) {
            const QString failMsg = session->errorMessage().isEmpty()
                ? tr("启动失败") : session->errorMessage();
            if (m_taskBar) {
                m_taskBar->updateLaunchCardStatus(instancePath, LaunchTaskCard::Failed);
                m_taskBar->updateLaunchCardMessage(instancePath, failMsg);
            }
            if (m_taskListPage) {
                m_taskListPage->setLaunchTaskInfo(instanceDisplayName(instancePath),
                    LaunchTaskCard::Failed, 0, failMsg);
            }
            if (!session->errorMessage().isEmpty()) {
                NotificationManager::showError(this, session->errorMessage());
            }
        }
    });
    return true;
}

void MainWindow::connectSessionSignals(GameLauncher *session, const QString &instancePath)
{
    // 并行启动会话的信号接线：捕获会话所属实例路径，路由到会话感知处理器。
    // 主单例的接线在 initGameLauncher 里（路径在信号触发时动态解析），不走这里
    connect(session, &GameLauncher::launchStatusChanged, this, [this, instancePath](GameLauncher::LaunchStatus status) {
        onSessionLaunchStatusChanged(instancePath, status);
    });
    connect(session, &GameLauncher::gameStarted, this, [this, instancePath]() {
        onSessionGameStarted(instancePath);
    });
    connect(session, &GameLauncher::gameStopped, this, [this, instancePath](int exitCode) {
        onSessionGameStopped(instancePath, exitCode);
    });
    connect(session, &GameLauncher::gameCrashed, this, [this, instancePath](const QString &error) {
        onSessionGameCrashed(instancePath, error);
    });
    connect(session, &GameLauncher::launchProgressChanged, this, [this, instancePath](int progress, const QString &message) {
        onSessionLaunchProgressChanged(instancePath, progress, message);
    });
    connect(session, &GameLauncher::launchDetailAdded, this, [this, instancePath](const QString &detail) {
        onSessionLaunchDetailAdded(instancePath, detail);
    });
    connect(session, &GameLauncher::errorReportGenerated, this, [this, instancePath](const QString &report) {
        onSessionErrorReportGenerated(instancePath, report);
    });
    connect(session, &GameLauncher::fileCompletionProgress, this, [this, instancePath](int completed, int total, const QString &currentFile) {
        onSessionFileCompletionProgress(instancePath, completed, total, currentFile);
    });
    connect(session, &GameLauncher::fileCompletionFinished, this, [this, instancePath](bool success) {
        onSessionFileCompletionFinished(instancePath, success);
    });
    connect(session, &GameLauncher::thirdPartyInjectWarning, this, [this](const QString &message) {
        NotificationManager::showInfo(this, message, 9000);
    });
}

void MainWindow::finishLaunchSession(const QString &instancePath)
{
    // 会话结束：移出会话表；主单例是长期复用的对象不销毁，
    // 并行会话对象此时游戏进程已退出，安全回收
    GameLauncher *session = m_launchSessions.take(instancePath);
    if (session && session != m_gameLauncher)
        session->deleteLater();
    m_launchSessionStates.remove(instancePath);
}

MainWindow::LaunchSessionUiState *MainWindow::sessionState(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return nullptr;
    return &m_launchSessionStates[instancePath];
}

void MainWindow::launchGameWithExtraArgs(const QStringList &extraGameArgs)
{
    if (m_currentInstancePath.isEmpty()) {
        showInstanceSelectPage();
        return;
    }
    // 快捷启动（存档 / 服务器 / 大厅）走统一启动入口
    startInstanceLaunch(m_currentInstancePath, extraGameArgs);
}

void MainWindow::launchWithAutoBackup(const QString &instancePath, const std::function<void()> &launchAction)
{
    if (!launchAction)
        return;

    LaunchSessionUiState *state = sessionState(instancePath);
    if (!state)
        return;

    // 该实例上一轮自动备份还没结束（连点启动）：忽略新的请求
    if (state->backupPending)
        return;

    // 另一个实例的启动前备份还在跑：备份管理器一次只处理一份，请稍后再启动
    if (!m_backupPendingPath.isEmpty() && m_backupPendingPath != instancePath) {
        NotificationManager::showInfo(this, tr("另一个实例的启动前备份还在进行，请稍后再启动"));
        return;
    }

    // 没开自动备份：零延迟直接启动，不打断原来的启动节奏
    if (!WorldBackupManager::autoBackupEnabled()) {
        launchAction();
        return;
    }

    state->backupPending = true;
    state->launchAfterBackup = launchAction;
    m_backupPendingPath = instancePath;

    // 备份期间先把该实例的启动任务卡切成「启动中」，告诉玩家为什么还没进游戏
    const QString instanceName = instanceDisplayName(instancePath);
    if (m_taskBar) {
        m_taskBar->ensureLaunchCard(instancePath, instanceName);
        m_taskBar->showLaunchCard(instancePath, true);
        m_taskBar->updateLaunchCardStatus(instancePath, LaunchTaskCard::Launching);
        m_taskBar->updateLaunchCardProgress(instancePath, 0);
        m_taskBar->updateLaunchCardMessage(instancePath, tr("正在自动备份存档..."));
    }
    if (m_taskListPage) {
        m_taskListPage->setLaunchTaskInfo(instanceName, LaunchTaskCard::Launching, 0,
                                          tr("正在自动备份存档..."));
    }
    if (m_gameLaunchPage) {
        m_gameLaunchPage->appendLog(tr("正在自动备份存档..."));
        m_gameLaunchPage->setProgress(0, tr("正在自动备份存档..."));
    }

    WorldBackupManager::instance()->startAutoBackupBeforeLaunch(instancePath);
}

void MainWindow::onQuickLaunchSaveClicked(const QString &saveName)
{
    // 实例助手（当前实例）触发的快捷进存档
    onQuickLaunchSaveClickedFor(m_currentInstancePath, saveName);
}

void MainWindow::onQuickLaunchSaveClickedFor(const QString &instancePath, const QString &saveName)
{
    if (instancePath.isEmpty()) {
        showInstanceSelectPage();
        return;
    }
    // 使用 --quickPlaySingleplayer 直接进入指定存档（Minecraft 1.21+ 支持）
    // 旧版本会忽略此参数，游戏正常启动后由用户在菜单选择存档
    QStringList extraArgs;
    if (!saveName.isEmpty()) {
        extraArgs << "--quickPlaySingleplayer" << saveName;
    }
    startInstanceLaunch(instancePath, extraArgs);
}

void MainWindow::onQuickLaunchServerClicked(const QString &address, quint16 port)
{
    // 目标实例已经在跑时明确提示，避免「一键启动 / 一键加入」点了没反应；
    // 其他实例在运行不影响本次加入（多实例并行启动）
    if (GameLauncher::sessionForInstance(m_currentInstancePath)) {
        NotificationManager::showError(this,
            tr("%1 已经在运行了，先关掉它再加入服务器。").arg(instanceDisplayName(m_currentInstancePath)));
        return;
    }

    // 使用 --server 和 --port 启动游戏并自动连接到服务器
    QStringList extraArgs;
    extraArgs << "--server" << address << "--port" << QString::number(port);
    startInstanceLaunch(m_currentInstancePath, extraArgs);
}

void MainWindow::onQuickLaunchServerClickedFor(const QString &instancePath, const QString &address,
                                               quint16 port)
{
    // 实例助手「开服」页用：窗口可能属于非当前实例，按窗口所属实例启动
    if (instancePath.isEmpty()) {
        showInstanceSelectPage();
        return;
    }
    if (GameLauncher::sessionForInstance(instancePath)) {
        NotificationManager::showError(
            this, tr("%1 已经在运行了，先关掉它再加入服务器。").arg(instanceDisplayName(instancePath)));
        return;
    }

    QStringList extraArgs;
    extraArgs << "--server" << address << "--port" << QString::number(port);
    startInstanceLaunch(instancePath, extraArgs);
}

void MainWindow::onLobbyJoinRequested(const ForumLobbyRoom &room, const QString &address,
                                      quint16 port)
{
    if (m_gameLauncher->status() == GameLauncher::Running
        || m_gameLauncher->status() == GameLauncher::Launching) {
        NotificationManager::showError(this, tr("游戏已经在运行了，先关掉游戏再加入大厅房间。"));
        return;
    }

    if (m_currentInstancePath.isEmpty() && LobbyInstanceMatcher::scanInstances().isEmpty()) {
        NotificationManager::showError(this, tr("本机还没有安装任何实例，请先安装一个游戏实例。"));
        return;
    }

    // 房间要求：带「辅助」标签的模组不算门槛，资源包 / 光影包只做可选补装
    const LobbyInstanceMatcher::Requirement requirement = LobbyInstanceMatcher::requirementFor(
        room.gameVersion, room.loader, room.requiredMods(), room.auxMods, room.resourcePacks,
        room.shaderPacks);

    // 匹配器带扫描缓存：房间列表已经预热过，这里基本不读盘
    const LobbyInstanceMatcher::JoinPlan plan = LobbyInstanceMatcher::makePlan(requirement);

    QString instancePath = plan.best.instance.path;
    int failedFetches = 0;

    // 什么都不缺时一步进房；缺实例或缺内容才弹准备对话框
    if (!plan.found || plan.best.hasAnythingToFetch()) {
        LobbyJoinPrepDialog dlg(this);
        dlg.setPlan(plan, room.title, room.joinTarget(), room.description);
        const int result = dlg.exec();
        if (result == LobbyJoinPrepDialog::kResultInstall) {
            openLobbyInstallFlow(room.gameVersion, room.loader);
            return;
        }
        if (result != QDialog::Accepted)
            return;
        instancePath = dlg.selectedInstancePath();
        failedFetches = dlg.failedCount();
        if (instancePath.isEmpty())
            return;
    }

    if (instancePath.isEmpty()) {
        NotificationManager::showError(
            this, plan.installHint.trimmed().isEmpty()
                      ? tr("没有找到合适的实例，装一个对应版本后再加入大厅房间。")
                      : plan.installHint);
        return;
    }

    if (instancePath != m_currentInstancePath) {
        // 切到匹配的实例（同时刷新顶栏、AI 页等上下文）
        updateCurrentInstance(instancePath);
    }

    // 刚才可能刚补装过模组 / 资源包，让匹配器的实例缓存重扫一遍
    LobbyInstanceMatcher::invalidateCache();

    QString tip = tr("正在用实例「%1」加入联机房间").arg(QDir(instancePath).dirName());
    if (failedFetches > 0)
        tip += tr("（还有 %1 个文件没装上，进房后可能和房主不完全一样）").arg(failedFetches);
    NotificationManager::showSuccess(this, tip);

    // 记下这次进的是哪个大厅房间：游戏真正跑起来（onSessionGameStarted）才开始上报「正在游玩」；
    // 房间号由统一启动入口 startInstanceLaunch 消费进对应会话的状态
    m_nextLaunchLobbyRoomId = room.id > 0 ? room.id : 0;
    if (m_gameAchievements)
    {
        // 分享链接进来的房间可能没有 id，用标题 + 地址生成一个稳定的房间标识
        int achievementRoomId = room.id;
        if (achievementRoomId <= 0)
        {
            achievementRoomId =
                static_cast<int>(qHash(room.title + QLatin1Char('|') + address) & 0x7fffffff) | 1;
        }
        m_gameAchievements->setLobbyContext(achievementRoomId, room.title);
    }
    onQuickLaunchServerClicked(address, port);
}

void MainWindow::openLobbyInstallFlow(const QString &gameVersion, const QString &loader)
{
    const QString version = gameVersion.trimmed();
    if (version.isEmpty()) {
        NotificationManager::showInfo(
            this, tr("这个房间没限定版本，先在「资源 > 安装新实例」里装一个实例再回来加入。"));
        ensurePageInitialized(PageIndex::InstallInstancePage);
        setSideBarVisible(true);
        m_topBar->setTitle(tr("资源>安装新实例"));
        animatedSwitchToPage(PageIndex::InstallInstancePage);
        return;
    }

    // 直接进这个版本的详情页：选加载器、点安装，装好回大厅再点一次「一键加入」
    m_currentVersionId = version;
    onVersionSelected(version, QStringLiteral("release"));
    NotificationManager::showInfo(
        this, tr("先装好 %1%2 的实例，再回大厅点「一键加入」就会自动进房。")
                  .arg(version, loader.trimmed().isEmpty() ? QString() 
                                                           : QStringLiteral(" + ") + loader));
}

void MainWindow::handleExternalJoinUrl(const QString &url)
{
    LobbyShareLink::LinkTarget target;
    QString error;
    if (!LobbyShareLink::parseLink(url, &target, &error))
    {
        NotificationManager::showError(this, error);
        return;
    }

    // 老式链接：房间信息就压缩在链接里，直接走加入流程
    if (target.legacy)
    {
        onFriendRoomJoinRequested(target.room);
        return;
    }

    // 新式短链接：链接里只有房间号，房间信息得回论坛取；
    // 房主结束联机后服务端已经把它删掉了，这时提示「联机可能已经结束」
    NotificationManager::showInfo(this, tr("正在打开联机分享链接…"));
    ForumLobbyService::instance()->fetchShare(
        target.token,
        [this](const ForumLobbyRoom &room) { onFriendRoomJoinRequested(room); },
        [this](const QString &message, int status) {
            if (status == 404)
            {
                AppMessageBox::information(
                    this, tr("联机可能已经结束"),
                    tr("这个分享链接对应的房间已经不在了：房主可能已经结束联机，"
                       "或者这条链接放太久失效了。\n请让房主重新创建一条分享链接。"));
                return;
            }
            NotificationManager::showError(this, message);
        });
}

void MainWindow::handleExternalQuickLaunchFile(const QString &filePath)
{
    showFromTray();

    QuickLaunchManager::Target target;
    QString error;
    if (!QuickLaunchManager::parseQuickLaunchFile(filePath, &target, &error))
    {
        // .blockbox 同时也是整合包的导出格式：内容不是快捷启动载荷时按整合包导入，
        // 让双击整合包也能直接进导入页，而不是只弹一句「不是有效文件」
        bool isZip = false;
        QFile probe(filePath);
        if (probe.open(QIODevice::ReadOnly))
        {
            isZip = probe.read(2) == QByteArrayLiteral("PK");
            probe.close();
        }
        if (isZip)
        {
            showModpackImportPage();
            if (m_modpackImportPage)
                m_modpackImportPage->loadModpackFile(filePath);
            return;
        }
        NotificationManager::showError(this, error);
        return;
    }

    if (!QDir(target.instancePath).exists())
    {
        NotificationManager::showError(
            this, tr("快捷启动文件指向的实例不存在：%1")
                      .arg(QDir::toNativeSeparators(target.instancePath)));
        return;
    }

    // 存档可能已经被删掉或改名：先确认还在，再启动游戏，避免进游戏后空等
    if (target.kind == QuickLaunchManager::Kind::Save)
    {
        const QString savePath = target.instancePath + QStringLiteral("/saves/") + target.saveName;
        if (!QDir(savePath).exists())
        {
            NotificationManager::showError(
                this, tr("存档「%1」已经不在了，可能被删除或改名了。").arg(target.saveName));
            return;
        }
    }

    if (target.instancePath != m_currentInstancePath)
    {
        updateCurrentInstance(target.instancePath);
        // 换实例带有页面过渡动画：稍等一下再启动，避免启动卡片和切页动画互相抢焦点
        QTimer::singleShot(150, this, [this, target]() {
            launchQuickLaunchTarget(target.instancePath, target);
        });
        return;
    }

    launchQuickLaunchTarget(target.instancePath, target);
}

void MainWindow::launchQuickLaunchTarget(const QString &instancePath,
                                         const QuickLaunchManager::Target &target)
{
    switch (target.kind)
    {
    case QuickLaunchManager::Kind::Save:
        // 与存档卡片上的「快捷启动」一致：启动游戏并直接进入该存档
        onQuickLaunchSaveClickedFor(instancePath, target.saveName);
        break;
    case QuickLaunchManager::Kind::Server:
        // 与服务器卡片上的「快捷启动并连接」一致：启动游戏并自动进服
        onQuickLaunchServerClickedFor(instancePath, target.serverAddress, target.serverPort);
        break;
    case QuickLaunchManager::Kind::Instance:
    default:
        startInstanceLaunch(instancePath);
        break;
    }
}

void MainWindow::onFriendRoomJoinRequested(const ForumLobbyRoom &room)
{
    if (!room.isOnline())
    {
        // 加入房间只走左下角通知，不弹窗挡住详情页
        NotificationManager::showInfo(this, tr("这个好友的房间已经下架，首页好友列表稍后会自动刷新。"));
        return;
    }

    if (m_friendRoomJoinPending)
    {
        NotificationManager::showInfo(this, tr("正在加入好友的联机房间，请稍候…"));
        return;
    }

    if (m_gameLauncher->status() == GameLauncher::Running
        || m_gameLauncher->status() == GameLauncher::Launching)
    {
        NotificationManager::showError(this, tr("游戏已经在运行了，先关掉游戏再加入好友的房间。"));
        return;
    }

    if (m_currentInstancePath.isEmpty() && LobbyInstanceMatcher::scanInstances().isEmpty())
    {
        NotificationManager::showError(this, tr("本机还没有安装任何实例，请先安装一个游戏实例。"));
        return;
    }

    // 名字依次兜底：好友昵称 → 房间名（分享链接进来的房间没有 host 信息）→ 「好友」
    QString friendName = room.host.displayName.trimmed();
    if (friendName.isEmpty())
        friendName = room.title.trimmed();
    if (friendName.isEmpty())
        friendName = tr("好友");

    if (room.isTerracotta())
    {
        const QString code = room.inviteCode.trimmed();
        if (code.isEmpty())
        {
            NotificationManager::showError(
                this, tr("这个房间没有可用的邀请码，请让房主重新发布。"));
            return;
        }

        TerracottaClient *terracotta = TerracottaClient::instance();
        // 只在真的要点「一起玩」时才拉起联机核心；首次使用时补上状态回调
        connect(terracotta, &TerracottaClient::stateChanged, this,
                &MainWindow::onFriendRoomJoinStateChanged, Qt::UniqueConnection);
        if (terracotta->installedVersion().isEmpty())
        {
            NotificationManager::showError(
                this, tr("这是陶瓦联机的房间，需要先在实例助手的联机页安装陶瓦联机核心。"));
            openInstanceAssistant();
            return;
        }
        if (terracotta->isInRoom())
        {
            NotificationManager::showInfo(
                this, tr("你已经在陶瓦联机的房间里了，先离开当前房间再加入好友的房间。"));
            return;
        }
        if (terracotta->state() == TerracottaClient::State::Exception
            || terracotta->state() == TerracottaClient::State::Fatal)
        {
            NotificationManager::showInfo(
                this, tr("联机核心状态异常，先到实例助手的联机页点一次「重试」再来加入。"));
            openInstanceAssistant();
            return;
        }

        m_friendRoomJoinPending = true;
        m_friendRoomJoinWaitingCore = false;
        m_friendRoomJoinCode = code;
        m_friendRoomJoinRoom = room;
        ForumLobbyService::instance()->recordJoin(room.id, nullptr, nullptr);

        QString playerName = ForumAccountService::instance()->playerName();
        if (playerName.isEmpty())
            playerName = tr("BlockBox 玩家");

        if (terracotta->state() == TerracottaClient::State::Waiting)
        {
            if (!terracotta->joinRoom(code, playerName))
            {
                m_friendRoomJoinPending = false;
                NotificationManager::showError(this, tr("无法加入好友的房间，请稍后再试。"));
                return;
            }
            NotificationManager::showInfo(
                this, tr("正在连接好友「%1」的联机房间…").arg(friendName));
        }
        else
        {
            m_friendRoomJoinWaitingCore = true;
            terracotta->start();
            NotificationManager::showInfo(
                this, tr("正在启动联机核心，连上后会自动进入好友的房间…"));
        }
        return;
    }

    // 红石 / frp / 直连：地址本身就能启动游戏
    QString host;
    quint16 port = 0;
    if (!parseLobbyAddress(room.address, &host, &port))
    {
        NotificationManager::showError(
            this, tr("这个房间没有可用的连接地址，请让房主重新发布。"));
        return;
    }

    ForumLobbyService::instance()->recordJoin(room.id, nullptr, nullptr);
    onLobbyJoinRequested(room, host, port);
}

void MainWindow::onFriendRoomJoinStateChanged()
{
    if (!m_friendRoomJoinPending)
        return;

    TerracottaClient *terracotta = TerracottaClient::instance();
    const auto state = terracotta->state();

    if (state == TerracottaClient::State::Exception
        || state == TerracottaClient::State::Fatal)
    {
        m_friendRoomJoinPending = false;
        m_friendRoomJoinWaitingCore = false;
        const QString detail = terracotta->errorMessage().trimmed().isEmpty()
                                   ? tr("请稍后再试。")
                                   : terracotta->errorMessage();
        NotificationManager::showError(this, tr("连不上好友的房间：%1").arg(detail));
        return;
    }

    if (m_friendRoomJoinWaitingCore && state == TerracottaClient::State::Waiting)
    {
        m_friendRoomJoinWaitingCore = false;
        QString playerName = ForumAccountService::instance()->playerName();
        if (playerName.isEmpty())
            playerName = tr("BlockBox 玩家");

        if (!terracotta->joinRoom(m_friendRoomJoinCode, playerName))
        {
            m_friendRoomJoinPending = false;
            NotificationManager::showError(this, tr("无法加入好友的房间，请稍后再试。"));
            return;
        }
        NotificationManager::showInfo(this, tr("正在连接好友的联机房间…"));
        return;
    }

    if (state != TerracottaClient::State::GuestOk)
        return;

    QString host;
    quint16 port = 0;
    if (!parseLobbyAddress(terracotta->serverUrl(), &host, &port))
    {
        m_friendRoomJoinPending = false;
        NotificationManager::showError(this, tr("联机核心没有返回可用的服务器地址。"));
        return;
    }

    const ForumLobbyRoom room = m_friendRoomJoinRoom;
    m_friendRoomJoinPending = false;
    m_friendRoomJoinWaitingCore = false;
    onLobbyJoinRequested(room, host, port);
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

void MainWindow::onSaveMapOpened()
{
    // 进入存档地表地图子页面：复用 TopBar 的返回按钮与标题
    m_topBar->setTitle(tr("实例管理>存档地图"));

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
            m_instanceManagePage->setCurrentTab(12);
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
            m_instanceManagePage->setCurrentTab(7);
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
    // 修改前的版本：LoaderDetailPage 进入修改模式时记录的版本（此处尚未退出修改模式）
    const QString oldVersion = m_loaderDetailPage && m_loaderDetailPage->modifyMode()
        ? m_loaderDetailPage->modifyOldVersion() : m_currentInstanceVersion;

    updateCurrentInstance(instancePath);
    const QString newVersion = m_currentInstanceVersion;
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

    // 版本有变化：自动把模组 / 资源包 / 光影包同步到新版本
    if (!newVersion.isEmpty() && (oldVersion.isEmpty() || oldVersion != newVersion))
        startResourceVersionSync(instancePath, oldVersion, newVersion);
}

void MainWindow::startResourceVersionSync(const QString &instancePath,
                                          const QString &oldVersion, const QString &newVersion)
{
    if (!m_resourceVersionSyncer)
    {
        m_resourceVersionSyncer = new ResourceVersionSyncer(this);
        connect(m_resourceVersionSyncer, &ResourceVersionSyncer::finished,
                this, &MainWindow::onResourceSyncFinished);
        connect(m_resourceVersionSyncer, &ResourceVersionSyncer::checkFailed,
                this, &MainWindow::onResourceSyncCheckFailed);
    }
    if (m_resourceVersionSyncer->isRunning())
        return;

    m_syncOldVersion = oldVersion;
    m_syncNewVersion = newVersion;
    NotificationManager::showInfo(this,
        tr("正在自动同步模组、资源包与光影包到 %1，完成后会提示处理结果").arg(newVersion));
    m_resourceVersionSyncer->start(instancePath, oldVersion, newVersion);
}

void MainWindow::onResourceSyncFinished(const QList<ResourceSyncItem> &converted,
                                        const QList<ResourceSyncItem> &pending)
{
    if (!converted.isEmpty())
    {
        NotificationManager::showSuccess(this,
            tr("已把 %1 个资源的版本同步到 %2").arg(converted.size()).arg(m_syncNewVersion));
    }
    if (!pending.isEmpty())
        showResourceSyncPendingDialog(pending);
}

void MainWindow::onResourceSyncCheckFailed(const QString &error)
{
    NotificationManager::showError(this,
        tr("检查资源版本失败，所有资源保持原样：%1").arg(error));
}

void MainWindow::showResourceSyncPendingDialog(const QList<ResourceSyncItem> &pending)
{
    if (pending.isEmpty())
        return;

    // 弹窗正文只点名前几个资源，完整清单交给悬浮窗展示
    QStringList names;
    for (const ResourceSyncItem &item : pending)
        names << item.displayName;
    constexpr int kPreviewCount = 6;
    QString preview = names.mid(0, kPreviewCount).join(QStringLiteral("、"));
    if (names.size() > kPreviewCount)
        preview += tr(" 等 %1 个资源").arg(names.size());

    AppMessageBox box(this);
    box.setWindowTitle(tr("部分资源未适配新版本"));
    box.setText(tr("以下 %1 个资源在资源站上没有支持 %2 的版本：")
                    .arg(pending.size()).arg(m_syncNewVersion));
    box.setInformativeText(preview);
    QPushButton *deleteBtn = box.addButton(tr("删除这些资源"), AppMessageBox::DestructiveRole);
    QPushButton *keepBtn = box.addButton(tr("保留"), AppMessageBox::AcceptRole);
    QPushButton *aiBtn = box.addButton(tr("让 AI 找替代"), AppMessageBox::ActionRole);
    QPushButton *listBtn = box.addButton(tr("悬浮窗查看清单"), AppMessageBox::ActionRole);
    box.setDefaultButton(keepBtn);
    box.exec();

    if (box.clickedButton() == deleteBtn)
    {
        // 删除属不可逆操作，与本地资源页一致再做一次确认
        if (AppMessageBox::question(this, tr("批量删除"),
                tr("确定要删除这 %1 个未适配新版本的资源吗？此操作不可撤销。")
                    .arg(pending.size()),
                AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No) == AppMessageBox::Yes)
        {
            for (const ResourceSyncItem &item : pending)
                QFile::remove(item.filePath);
            NotificationManager::showInfo(this,
                tr("已删除 %1 个未适配新版本的资源").arg(pending.size()));
        }
        else
        {
            // 用户临时反悔：别丢掉清单，转到悬浮窗继续处理
            showResourceSyncListWindow(pending);
        }
    }
    else if (box.clickedButton() == aiBtn)
    {
        openAiFindReplacement(pending);
    }
    else if (box.clickedButton() == listBtn)
    {
        showResourceSyncListWindow(pending);
    }
    // 保留 / 关闭：什么都不动
}

void MainWindow::showResourceSyncListWindow(const QList<ResourceSyncItem> &items)
{
    if (items.isEmpty())
        return;
    if (!m_resourceSyncListWindow)
    {
        m_resourceSyncListWindow = new ResourceSyncListWindow(this);
        connect(m_resourceSyncListWindow, &ResourceSyncListWindow::findReplacementRequested,
                this, [this](const QList<ResourceSyncItem> &items)
        {
            openAiFindReplacement(items);
        });
    }
    m_resourceSyncListWindow->setItems(items, m_syncNewVersion);
    m_resourceSyncListWindow->show();
    m_resourceSyncListWindow->raise();
    m_resourceSyncListWindow->activateWindow();
}

void MainWindow::openAiFindReplacement(const QList<ResourceSyncItem> &items)
{
    if (items.isEmpty())
        return;
    ensurePageInitialized(PageIndex::AiChatPage);

    // 每个未适配资源一条引用，AI 能看到文件本身与它原来支持的版本
    QList<ResourceReference> refs;
    for (const ResourceSyncItem &item : items)
    {
        ResourceReference ref;
        ref.category = ContentTypeConfig::getConfig(item.type).displayName;
        ref.name = item.displayName;
        ref.path = item.filePath;
        ref.size = QFileInfo(item.filePath).size();
        QStringList gv = item.gameVersions;
        if (gv.size() > 6)
        {
            gv = gv.mid(0, 6);
            gv << QStringLiteral("...");
        }
        ref.extra = tr("当前版本: %1；支持的游戏版本: %2")
            .arg(item.installedVersion.isEmpty() ? tr("未知") : item.installedVersion,
                 gv.isEmpty() ? tr("未知") : gv.join(QStringLiteral(", ")));
        refs.append(ref);
    }

    const QString prompt = tr(
        "我的实例刚从 %1 升级到 %2，附上的 %3 个资源都没有支持 %2 的版本。"
        "请逐个了解它们的用途，推荐功能相近、支持 %2 的替代资源（给出来源），"
        "我确认后再帮我下载替换。")
        .arg(m_syncOldVersion.isEmpty() ? tr("旧版本") : m_syncOldVersion,
             m_syncNewVersion, QString::number(items.size()));

    setSideBarVisible(false);
    m_topBar->setTitle(tr("AI 助手"));
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    if (m_aiChatPage)
        m_aiChatPage->addExternalPrompt(refs, prompt);
    m_pageAnimator->animateToIndex(static_cast<int>(PageIndex::AiChatPage));
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
    qDebug() << "[MainWindow]" << "返回安装页面";
    
    ensurePageInitialized(PageIndex::InstallInstancePage);
    
    // 从版本详情页返回时补回子导航：该页可由"资源>安装新实例"进入，
    // 也可由"实例管理>修改"进入，按当前是否为修改模式恢复对应分组
    if (m_loaderDetailPage && m_loaderDetailPage->modifyMode())
        restoreSideBarWithSubNav(4, 2);
    else
        restoreSideBarWithSubNav(1, 0);
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
    qDebug() << "[MainWindow]" << "返回加载器详情页";
    
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

    startResourceDownload(this, {versionFile.downloadUrl}, destPath, QString(),
        [this, taskId, fileName, modInfo](bool success) {
            if (success) {
                DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::Completed, 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Completed, tr("下载完成"));
                NotificationManager::showSuccess(this, tr("模组 \"%1\" 下载完成").arg(modInfo.name));
            } else {
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Failed, tr("下载失败"));
                NotificationManager::showErrorWithNetworkCheck(this, tr("模组 \"%1\" 下载失败").arg(modInfo.name));
            }
        },
        [this, taskId, fileName](qint64 received, qint64 total) {
            DownloadTaskManager::instance()->updateTaskFileProgress(taskId, fileName, received, total);
            if (total > 0) {
                int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
            }
        });
}

void MainWindow::onModConvertRequested(const ModInfo &modInfo, const ModVersionFile &versionFile,
                                       const QStringList &oldFilePaths)
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
        taskId, DownloadTaskStatus::Downloading, tr("转换模组: ") + modInfo.name);
    DownloadTaskManager::instance()->updateTaskCurrentFile(taskId, fileName);
    DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::ClientJar, 0);

    // 下载完成后删掉旧版本文件（含 .disabled 残留），完成版本转换
    auto removeOldFiles = [destPath](const QStringList &oldFiles) {
        const QString newAbs = QFileInfo(destPath).absoluteFilePath();
        for (const QString &p : oldFiles) {
            if (p.isEmpty())
                continue;
            const QString abs = QFileInfo(p).absoluteFilePath();
            if (abs == newAbs)
                continue;
            if (QFile::exists(abs))
                QFile::remove(abs);
            if (QFile::exists(abs + ".disabled"))
                QFile::remove(abs + ".disabled");
        }
    };

    // 镜像优先下载，失败自动回退官方源
    const QString requestUrl = McimHelper::rewriteFileUrl(versionFile.downloadUrl);
    QStringList candidateUrls;
    if (requestUrl != versionFile.downloadUrl)
        candidateUrls << requestUrl;
    candidateUrls << versionFile.downloadUrl;

    startResourceDownload(this, candidateUrls, destPath, versionFile.sha1,
        [this, taskId, fileName, modInfo, oldFilePaths, removeOldFiles](bool success) {
            if (success) {
                removeOldFiles(oldFilePaths);
                DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::Completed, 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Completed, tr("转换完成"));
                NotificationManager::showSuccess(this, tr("模组 \"%1\" 已转换到所选版本").arg(modInfo.name));
            } else {
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Failed, tr("转换失败"));
                NotificationManager::showError(this, tr("模组 \"%1\" 转换失败").arg(modInfo.name));
            }
        },
        [this, taskId, fileName](qint64 received, qint64 total) {
            DownloadTaskManager::instance()->updateTaskFileProgress(taskId, fileName, received, total);
            if (total > 0) {
                int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
            }
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

/** 资源分组子项索引：0安装新实例/1下载整合包/2导入整合包/3模组/4数据包/5资源包/6光影包/7世界/8投影/9插件/10新建收藏夹/11+收藏夹 */
static int resourcesChildIndexForContentType(ContentType type)
{
    switch (type)
    {
    case ContentType::Modpack:      return 1;
    case ContentType::Mod:          return 3;
    case ContentType::DataPack:     return 4;
    case ContentType::ResourcePack: return 5;
    case ContentType::ShaderPack:   return 6;
    case ContentType::World:        return 7;
    }
    return -1;
}

/** 基岩版子导航 childIndex → CurseForge classId：1资源包/2材质包/3地图/4脚本/10附加包，0=分类不限 */
int MainWindow::bedrockClassIdForChildIndex(int childIndex)
{
    switch (childIndex)
    {
    case 1:  return 6929;   // 资源包 Texture Packs
    case 2:  return 6929;   // 材质包（与资源包同库，CurseForge 混用两个入口词）
    case 3:  return 6913;   // 地图 Maps
    case 4:  return 6940;   // 脚本 Scripts
    case 10: return 4984;   // 附加包 Addons
    }
    return 0;
}

void MainWindow::applyBedrockEdition(bool bedrock)
{
    m_bedrockEdition = bedrock;
    const bool oldMode = FavoritesManager::instance()->isBedrockMode();
    if (oldMode == bedrock)
        return;

    // 收藏夹先切（favorites.json / favorites_bedrock.json 独立存储），
    // SideBar 与 ResourcesPage 重建时读到的就是新版本的收藏夹列表
    FavoritesManager::instance()->setBedrockMode(bedrock);
    m_sideBar->setBedrockMode(bedrock);
    if (m_homePage)
        m_homePage->setBedrockMode(bedrock);
    if (m_resourcesPage)
        m_resourcesPage->setBedrockMode(bedrock);
    // 设置页在首次进入时才创建，这里跳过（initSettingsPage 时按当前模式初始化）
}

void MainWindow::showBedrockContentPage(int classId)
{
    if (!m_bedrockContentPage)
    {
        m_bedrockContentPage = new BedrockContentPage(this);
        m_stackedWidget->addWidget(m_bedrockContentPage);
        // 卡片点击进入基岩版详情页
        connect(m_bedrockContentPage, &BedrockContentPage::contentDetailRequested, this,
                [this](const ModInfo &info, int bedrockClassId) {
            showBedrockContentDetailPage(info, bedrockClassId);
        });
        // 记住「下载到实例」选择，进详情页时透传给安装目标
        connect(m_bedrockContentPage, &BedrockContentPage::targetInstanceChanged, this,
                [this](const QString &instanceId) {
            m_bedrockDownloadTargetInstanceId = instanceId;
        });
    }

    if (m_pageAnimator)
        m_pageAnimator->animateToWidget(m_bedrockContentPage);
    else
        m_stackedWidget->setCurrentWidget(m_bedrockContentPage);

    setSideBarVisible(true);
    // 顶栏返回键回资源主页
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    if (classId > 0)
        m_bedrockContentPage->setCategory(classId);
}

void MainWindow::showBedrockContentDetailPage(const ModInfo &info, int bedrockClassId)
{
    if (m_bedrockContentDetailPage)
    {
        m_stackedWidget->removeWidget(m_bedrockContentDetailPage);
        m_bedrockContentDetailPage->deleteLater();
        m_bedrockContentDetailPage = nullptr;
    }

    m_bedrockContentDetailPage = new BedrockContentDetailPage(this);
    m_stackedWidget->addWidget(m_bedrockContentDetailPage);
    m_stackedWidget->setCurrentWidget(m_bedrockContentDetailPage);

    // 从浏览页进入详情页时保持资源子导航可见
    setSideBarVisible(true);
    const QString category = FavoritesManager::bedrockClassIdToCategoryName(bedrockClassId);
    m_topBar->setTitle(tr("资源>%1>%2").arg(category, info.name));

    m_bedrockContentDetailPage->setBedrockClassId(bedrockClassId);
    m_bedrockContentDetailPage->setBedrockCategory(category);
    m_bedrockContentDetailPage->setTargetInstanceId(m_bedrockDownloadTargetInstanceId);
    m_bedrockContentDetailPage->setModInfo(info);

    // 详情页返回键回到资源主页
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, [this]() {
        // 不带子项高亮（入口可能是子导航 1~4，也可能是主页「附加包」卡）
        restoreSideBarWithSubNav(1, -1);
        m_topBar->setTitle(tr("资源>社区资源"));
        if (m_bedrockContentPage)
        {
            if (m_pageAnimator)
                m_pageAnimator->animateToWidget(m_bedrockContentPage);
            else
                m_stackedWidget->setCurrentWidget(m_bedrockContentPage);
        }
        else
        {
            onBackToMain();
        }
    });

    // 用户在详情页点「新建收藏夹...」时，弹出对话框并直接收藏到新分组
    connect(m_bedrockContentDetailPage, &BedrockContentDetailPage::addToFavoritesRequested,
            this, [this](const ModInfo &modInfo, ContentType type, const QString &source,
                         int classId, const QString &categoryName)
    {
        FavoriteFolderDialog dlg(FavoriteFolderDialog::CreateMode, this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        QString newId = FavoritesManager::instance()->createFolder(dlg.folderName());
        if (!newId.isEmpty())
        {
            // 收藏条目带上基岩分类（classId/分类名随详情页传入）
            FavoritesManager::instance()->addFavorite(newId, modInfo, type, source,
                                                      classId, categoryName);
            NotificationManager::showSuccess(this, tr("已添加到新收藏夹“%1”").arg(dlg.folderName()));
            if (m_bedrockContentDetailPage)
                m_bedrockContentDetailPage->refreshFavoriteState();
        }
    });
}

void MainWindow::showBedrockDownloadPage()
{
    if (!m_bedrockDownloadPage)
    {
        // 注册页面（懒创建）：versionDetailRequested 由列表页翻卡时发出
        m_bedrockDownloadPage = new BedrockDownloadPage(this);
        m_stackedWidget->addWidget(m_bedrockDownloadPage);
        connect(m_bedrockDownloadPage, &BedrockDownloadPage::versionDetailRequested,
                this, &MainWindow::showBedrockVersionDetailPage);
    }
    else
    {
        if (m_stackedWidget->indexOf(m_bedrockDownloadPage) < 0)
            m_stackedWidget->addWidget(m_bedrockDownloadPage);
        connect(m_bedrockDownloadPage, &BedrockDownloadPage::versionDetailRequested,
                this, &MainWindow::showBedrockVersionDetailPage, Qt::UniqueConnection);
    }

    setSideBarVisible(true);
    m_topBar->setTitle(tr("资源>安装新实例"));
    // 顶栏返回键回资源主页
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    // 首次/每次进入刷新一次版本列表
    if (m_pageAnimator)
        m_pageAnimator->animateToWidget(m_bedrockDownloadPage);
    else
        m_stackedWidget->setCurrentWidget(m_bedrockDownloadPage);
    m_bedrockDownloadPage->refresh();
}

void MainWindow::showBedrockVersionDetailPage(const BedrockVersionEntry &entry)
{
    if (!m_bedrockVersionDetailPage)
    {
        m_bedrockVersionDetailPage = new BedrockVersionDetailPage(this);
        m_stackedWidget->addWidget(m_bedrockVersionDetailPage);
    }

    setSideBarVisible(true);
    m_topBar->setTitle(tr("资源>安装新实例>%1").arg(entry.version));
    // 顶栏返回键回版本列表
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, [this]() {
        if (m_bedrockDownloadPage)
            showBedrockDownloadPage();
        else
            onBackToMain();
    });

    m_bedrockVersionDetailPage->setVersionEntry(entry);
    if (m_pageAnimator)
        m_pageAnimator->animateToWidget(m_bedrockVersionDetailPage);
    else
        m_stackedWidget->setCurrentWidget(m_bedrockVersionDetailPage);
}

void MainWindow::showBedrockInstanceSelectPage()
{
    if (!m_bedrockInstanceSelectPage) {
        m_bedrockInstanceSelectPage = new BedrockInstanceSelectPage(this);
        m_stackedWidget->addWidget(m_bedrockInstanceSelectPage);
        connect(m_bedrockInstanceSelectPage, &BedrockInstanceSelectPage::backRequested,
                this, &MainWindow::onBackToMain);
        connect(m_bedrockInstanceSelectPage, &BedrockInstanceSelectPage::launchRequested,
                this, [this](const QString &instanceId) {
            QString err;
            if (!BedrockInstanceManager::instance()->activateInstance(instanceId, &err)) {
                NotificationManager::showError(this, tr("切换实例失败: %1").arg(err));
                return;
            }
            m_bedrockInstanceSelectPage->refresh();
            if (!BedrockLauncher::instance()->launchGame())
                NotificationManager::showError(this, tr("基岩版启动失败，请确认游戏已从商店安装。"));
        });
    }
    else if (m_stackedWidget->indexOf(m_bedrockInstanceSelectPage) < 0) {
        m_stackedWidget->addWidget(m_bedrockInstanceSelectPage);
    }

    setSideBarVisible(true);
    m_topBar->setTitle(tr("实例管理"));
    disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
    connect(m_topBar, &TopBar::backClicked, this, &MainWindow::onBackToMain);
    if (m_pageAnimator)
        m_pageAnimator->animateToWidget(m_bedrockInstanceSelectPage);
    else
        m_stackedWidget->setCurrentWidget(m_bedrockInstanceSelectPage);
    m_bedrockInstanceSelectPage->refresh();
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

    // 恢复资源子导航并选中对应的内容类型（从搜索页/实例页等入口进入时子导航此前会缺失）
    restoreSideBarWithSubNav(1, resourcesChildIndexForContentType(type));
    m_topBar->setTitle(tr("资源>%1").arg(config.displayName));

    m_contentDownloadPage->setCurrentInstancePath(m_currentInstancePath);
    m_contentDownloadPage->loadInstances();
    m_contentDownloadPage->loadInitialMods();

    connect(m_contentDownloadPage, &ContentDownloadPage::contentClicked, this, [this, type](const ModInfo &info)
    {
        // 列表页选中的实例版本/加载器带入详情页左侧筛选，保证详情页结果与列表一致
        showContentDetailPage(info, type,
                              m_contentDownloadPage ? m_contentDownloadPage->selectedGameVersion() : QString(),
                              m_contentDownloadPage ? m_contentDownloadPage->selectedLoader() : QString());
    });

    // 页面内"下载实例"下拉：切换后资源（含前置）应下载到该实例，
    // 同步顶栏与各页上下文，避免下载落到别的实例
    connect(m_contentDownloadPage, &ContentDownloadPage::instanceSelectionChanged,
            this, &MainWindow::applyInstanceContext);

    connect(m_contentDownloadPage, &ContentDownloadPage::cardDownloadRequested, this, [this, type](const ModInfo &info)
    {
        // 卡片下载图标与详情页「一键安装」同一套流程：按列表选中的实例版本/加载器挑正式版
        const QString gameVersion = m_contentDownloadPage ? m_contentDownloadPage->selectedGameVersion() : QString();
        const QString loader = m_contentDownloadPage ? m_contentDownloadPage->selectedLoader() : QString();
        onCardDownloadRequested(info, type, 0, gameVersion, loader);
    });
}

void MainWindow::showContentDetailPage(const ModInfo &info, ContentType type,
                                       const QString &filterGameVersion, const QString &filterLoader)
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

    // 从搜索页等入口进入详情页时补回资源子导航；若已展开收藏夹等其他分组则原样保留
    if (m_subNavPanel->currentParentIndex() < 0)
        restoreSideBarWithSubNav(1, resourcesChildIndexForContentType(type));
    else
        setSideBarVisible(true);
    m_topBar->setTitle(tr("资源>%1>%2").arg(config.displayName, info.name));

    m_contentDetailPage->setModInfo(info);

    // 按列表页带入的实例筛选左侧版本/加载器并刷新可下载版本
    if (!filterGameVersion.isEmpty() || !filterLoader.isEmpty())
        m_contentDetailPage->setInstanceFilter(filterGameVersion, filterLoader);

    // 带入当前实例：详情页据此识别下载列表里各版本的安装状态
    m_contentDetailPage->setInstancePath(m_currentInstancePath);

    connect(m_contentDetailPage, &ContentDetailPage::downloadRequested, this, [this, type](const ModInfo &modInfo, const ModVersionFile &versionFile)
    {
        onContentDownloadRequested(modInfo, versionFile, type);
    });

    connect(m_contentDetailPage, &ContentDetailPage::convertRequested, this,
            [this, type](const ModInfo &modInfo, const ModVersionFile &versionFile, const QStringList &oldFilePaths)
    {
        onContentConvertRequested(modInfo, versionFile, type, oldFilePaths);
    });

    // 左侧「一键安装」：安装与当前实例兼容的最新正式版，并自动补齐必备前置
    connect(m_contentDetailPage, &ContentDetailPage::installLatestRequested, this, [this, type](const ModInfo &modInfo)
    {
        const QString gameVersion = m_contentDetailPage ? m_contentDetailPage->instanceGameVersion() : QString();
        const QString loader = m_contentDetailPage ? m_contentDetailPage->instanceLoaderType() : QString();
        onCardDownloadRequested(modInfo, type, 0, gameVersion, loader);
    });

    connect(m_contentDetailPage, &ContentDetailPage::backToListRequested, this, [this, type]()
    {
        showContentDownloadPage(type);
    });

    connect(m_contentDetailPage, &ContentDetailPage::contentClicked, this, [this, type](const ModInfo &modInfo)
    {
        // 详情页内继续跳转其他资源时，沿用当前的实例筛选
        showContentDetailPage(modInfo, type,
                              m_contentDetailPage ? m_contentDetailPage->instanceGameVersion() : QString(),
                              m_contentDetailPage ? m_contentDetailPage->instanceLoaderType() : QString());
    });

    // 问问AI：跳转 AI 助手并引用当前网络资源
    connect(m_contentDetailPage, &ContentDetailPage::askAiRequested,
            this, &MainWindow::openAiChatWithReference);

    // 点击「创建翻译 / 完善翻译」：带原文进翻译编辑页，返回落点仍是本详情页
    connect(m_contentDetailPage, &ContentDetailPage::translationComposeRequested,
            this, [this, type](const ModInfo &info) {
        if (!m_contentDetailPage)
            return;
        m_translationComposeInfo = info;
        // 预填原文用资源本身的简介/正文（详情回包后 m_modInfo 已合并完整原文）
        m_translationSourceSummary = info.description;
        m_translationSourceDetailHtml = info.detailedDescription.isEmpty()
            ? info.description : info.detailedDescription;
        showTranslationComposePage([this, type]() {
            // 返回内容详情页：页面实例还在栈里，直接切回去（不重新拉详情）
            if (!m_contentDetailPage)
                return;
            setSideBarVisible(false);
            const ContentTypeConfig config = ContentTypeConfig::getConfig(type);
            m_topBar->setTitle(tr("资源>%1>%2").arg(config.displayName,
                                                    m_translationComposeInfo.name));
            disconnect(m_topBar, &TopBar::backClicked, this, nullptr);
            connect(m_topBar, &TopBar::backClicked, this, [this, type]() {
                showContentDownloadPage(type);
            });
            m_pageAnimator->animateToIndex(m_stackedWidget->indexOf(m_contentDetailPage));
        });
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

    // 恢复资源子导航并选中对应的内容类型
    restoreSideBarWithSubNav(1, resourcesChildIndexForContentType(type));
    m_topBar->setTitle(tr("资源>%1>本地管理").arg(config.displayName));

    m_contentListPage->loadInstances();

    connect(m_contentListPage, &ContentListPage::contentClicked, this, [this, type](const ModInfo &info)
    {
        showContentDetailPage(info, type);
    });

    // 问问AI：跳转 AI 助手并引用当前本地资源
    connect(m_contentListPage, &ContentListPage::askAiRequested,
            this, &MainWindow::openAiChatWithReference);
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

    // 基岩版收藏条目带 bedrockClassId：走基岩详情页（CurseForge 游戏 ID 78022），
    // Java 详情页按 432 拉 数据会张冠李戴
    if (item.bedrockClassId > 0)
    {
        showBedrockContentDetailPage(info, item.bedrockClassId);
        return;
    }

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

void MainWindow::installOnlineModpack(const ModInfo &modInfo, const ModVersionFile &versionFile)
{
    if (versionFile.downloadUrl.isEmpty())
    {
        NotificationManager::showError(this,
            tr("%1 这个版本没有可用的下载地址，装不了").arg(modInfo.name));
        return;
    }

    // 在线整合包装的是"新实例"，与当前选中的实例无关
    const QString gameRoot = SettingsManager::instance()->getDefaultInstancePath();

    // 压缩包文件名：服务器给的最准（.zip / .mrpack），没有再按 URL / 名称拼
    QString fileName = versionFile.fileName.trimmed();
    if (fileName.isEmpty())
        fileName = QUrl(versionFile.downloadUrl).fileName();
    if (fileName.isEmpty() || fileName.contains(QLatin1Char('?')))
    {
        const QString ver = versionFile.version.isEmpty() ? modInfo.latestVersion : versionFile.version;
        fileName = Safety::safeFileName(modInfo.name, QStringLiteral("modpack"));
        if (!ver.isEmpty())
            fileName += QStringLiteral("-") + Safety::safeFileName(ver, QStringLiteral("version"));
        fileName += QStringLiteral(".zip");
    }
    fileName = Safety::safeFileName(fileName, QStringLiteral("modpack.zip"));
    if (!fileName.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)
        && !fileName.endsWith(QLatin1String(".mrpack"), Qt::CaseInsensitive))
    {
        fileName += QStringLiteral(".zip");
    }

    // 临时落在游戏根目录下的隐藏文件夹：安装完就删，系统临时盘不够时也能下
    const QString tempDir = QDir(gameRoot).filePath(QStringLiteral(".blockbox-temp/modpacks"));
    QDir().mkpath(tempDir);
    const QString zipPath = QDir(tempDir).filePath(fileName);
    if (QFile::exists(zipPath))
        QFile::remove(zipPath);

    auto *taskManager = DownloadTaskManager::instance();
    const QString taskId = taskManager->addTaskWithType(
        modInfo.name, QString(), QString(), QStringList(), TaskType::ContentDownload);
    taskManager->updateTaskStatus(taskId, DownloadTaskStatus::Downloading,
                                  tr("下载整合包: ") + modInfo.name);
    taskManager->updateTaskCurrentFile(taskId, fileName);
    taskManager->updateTaskStage(taskId, DownloadStage::ClientJar, 0);

    // 镜像优先、官方兜底（大文件镜像能明显提速，镜像坏了自动回官方）
    QStringList candidates;
    const QString mirrored = McimHelper::rewriteFileUrl(versionFile.downloadUrl);
    if (!mirrored.isEmpty() && mirrored != versionFile.downloadUrl)
        candidates << mirrored;
    candidates << versionFile.downloadUrl;

    startResourceDownload(this, candidates, zipPath, versionFile.sha1,
        [this, taskId, zipPath, fileName, modInfo, gameRoot](bool success)
        {
            auto *manager = DownloadTaskManager::instance();

            if (!success)
            {
                manager->updateTaskStatus(taskId, DownloadTaskStatus::Failed,
                                          tr("整合包下载失败"));
                NotificationManager::showError(this,
                    tr("整合包 \"%1\" 下载失败，请检查网络后重试").arg(modInfo.name));
                QFile::remove(zipPath);
                return;
            }

            manager->updateTaskFileProgress(taskId, fileName, 1, 1);
            manager->updateTaskProgressPercent(taskId, 100);

            // 下载回来的是压缩包，先认格式并读出游戏版本 / 加载器 / 模组清单
            const modpack::ModpackInfo info = modpack::ModpackDetector::parse(zipPath);
            if (info.gameVersion.trimmed().isEmpty())
            {
                manager->updateTaskStatus(taskId, DownloadTaskStatus::Failed,
                                          tr("无法识别整合包"));
                NotificationManager::showError(this,
                    tr("下载回来的文件不是可识别的整合包（缺少游戏版本等信息），无法安装"));
                QFile::remove(zipPath);
                return;
            }

            const QString preferredName = modInfo.name.trimmed().isEmpty() ? info.name
                                                                           : modInfo.name;
            const QString instanceName = uniqueInstanceName(gameRoot, preferredName);

            auto *importer = new modpack::ModpackImporter(this);
            importer->setModpackInfo(info);
            importer->setZipFilePath(zipPath);
            importer->setInstanceName(instanceName);

            manager->updateTaskStatus(taskId, DownloadTaskStatus::Downloading,
                                      tr("正在安装整合包: ") + instanceName);

            // 安装各阶段（下载版本 / 装加载器 / 下模组 / 解压）同步到任务栏。
            // 只同步阶段文案：安装进度由弹出的进度窗按 0~100 展示，
            // 若也写回任务进度条，会把已经走完的下载进度拉回 0。
            connect(importer, &modpack::ModpackImporter::installProgressChanged, this,
                    [taskId](int percent, const QString &message)
            {
                Q_UNUSED(percent);
                if (!message.isEmpty())
                    DownloadTaskManager::instance()->updateTaskStep(taskId, message);
            });

            connect(importer, &Task::finished, this,
                    [this, importer, taskId, zipPath, instanceName]()
            {
                auto *tasks = DownloadTaskManager::instance();
                if (importer->wasSuccessful())
                {
                    tasks->updateTaskStage(taskId, DownloadStage::Completed, 100);
                    tasks->updateTaskProgressPercent(taskId, 100);
                    tasks->updateTaskStatus(taskId, DownloadTaskStatus::Completed,
                                            tr("安装完成"));
                    NotificationManager::showSuccess(this,
                        tr("整合包已装成新实例「%1」，去实例列表就能启动").arg(instanceName));
                    // 实例列表已初始化时立即刷新，新实例马上出现
                    if (m_instanceSelectPage)
                        m_instanceSelectPage->loadInstances();
                }
                else if (importer->wasAborted())
                {
                    tasks->updateTaskStatus(taskId, DownloadTaskStatus::Cancelled, tr("已取消"));
                    NotificationManager::showInfo(this, tr("整合包安装已取消"));
                }
                else
                {
                    const QString reason = importer->failReason();
                    tasks->updateTaskStatus(taskId, DownloadTaskStatus::Failed, reason);
                    AppMessageBox::warning(this, tr("安装失败"),
                        reason.isEmpty() ? tr("整合包安装失败，请检查日志。") : reason);
                }

                QFile::remove(zipPath);
                importer->deleteLater();
            });

            auto *progressDlg = new ProgressDialog(this);
            progressDlg->setAttribute(Qt::WA_DeleteOnClose);
            progressDlg->setSkipButton(true, tr("取消"));
            progressDlg->showWithTask(importer);
        },
        [taskId, fileName](qint64 received, qint64 total)
        {
            auto *manager = DownloadTaskManager::instance();
            manager->updateTaskFileProgress(taskId, fileName, received, total);
            if (total > 0)
            {
                const int percent = qBound(0, static_cast<int>(received * 100 / total), 100);
                manager->updateTaskProgressPercent(taskId, percent);
            }
        });
}

void MainWindow::onContentDownloadRequested(const ModInfo &modInfo, const ModVersionFile &versionFile, ContentType contentType)
{
    // 整合包要装成新实例，不是往当前实例里塞文件
    if (contentType == ContentType::Modpack)
    {
        installOnlineModpack(modInfo, versionFile);
        return;
    }

    QString instancePath = m_currentInstancePath;
    if (instancePath.isEmpty())
    {
        NotificationManager::showError(this,
            tr("请先选择下载实例：用顶部实例按钮或资源页的「下载实例」下拉选择"));
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

    // 文件名来自 Modrinth/CurseForge 返回的上传者可控字段，必须清洗成单层文件名，
    // 否则形如 "../../Startup/x.jar" 的内容会被写到实例目录之外
    fileName = Safety::safeFileName(fileName, QStringLiteral("download.jar"));

    QString destDir = instancePath + "/" + config.folderName;
    QString destPath = QDir::cleanPath(destDir + "/" + fileName);
    if (!Safety::isPathInside(destDir, destPath))
    {
        NotificationManager::showError(this, tr("文件名非法，已取消下载"));
        return;
    }

    QDir().mkpath(destDir);

    QString instanceName = QFileInfo(instancePath).fileName();
    QString taskId = DownloadTaskManager::instance()->addTask(
        instanceName, instancePath, QString(), QStringList());
    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading, tr("下载%1: ").arg(config.displayName) + modInfo.name);
    DownloadTaskManager::instance()->updateTaskCurrentFile(taskId, fileName);
    DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::ClientJar, 0);

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
                NotificationManager::showErrorWithNetworkCheck(this, tr("世界 \"%1\" 下载失败: %2").arg(modInfo.name, error));
                downloader->deleteLater();
            });
        downloader->downloadToInstance(versionFile.downloadUrl, fileName, instancePath, contentType);
        return;
    }

    startResourceDownload(this, {versionFile.downloadUrl}, destPath, versionFile.sha1,
        [this, taskId, fileName, modInfo, config](bool success)
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
                NotificationManager::showErrorWithNetworkCheck(this, tr("%1 \"%2\" 下载失败").arg(config.displayName, modInfo.name));
            }
        },
        [this, taskId, fileName](qint64 received, qint64 total)
        {
            DownloadTaskManager::instance()->updateTaskFileProgress(taskId, fileName, received, total);
            if (total > 0)
            {
                int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
            }
        });
}

void MainWindow::onContentConvertRequested(const ModInfo &modInfo, const ModVersionFile &versionFile,
                                           ContentType contentType, const QStringList &oldFilePaths)
{
    QString instancePath = m_currentInstancePath;
    if (instancePath.isEmpty())
    {
        NotificationManager::showError(this,
            tr("请先选择下载实例：用顶部实例按钮或资源页的「下载实例」下拉选择"));
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

    // 文件名来自 Modrinth/CurseForge 返回的上传者可控字段，必须清洗成单层文件名，
    // 否则形如 "../../Startup/x.jar" 的内容会被写到实例目录之外
    fileName = Safety::safeFileName(fileName, QStringLiteral("download.jar"));

    QString destDir = instancePath + "/" + config.folderName;
    QString destPath = QDir::cleanPath(destDir + "/" + fileName);
    if (!Safety::isPathInside(destDir, destPath))
    {
        NotificationManager::showError(this, tr("文件名非法，已取消转换"));
        return;
    }

    QDir().mkpath(destDir);

    QString instanceName = QFileInfo(instancePath).fileName();
    QString taskId = DownloadTaskManager::instance()->addTask(
        instanceName, instancePath, QString(), QStringList());
    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading, tr("转换%1: ").arg(config.displayName) + modInfo.name);
    DownloadTaskManager::instance()->updateTaskCurrentFile(taskId, fileName);
    DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::ClientJar, 0);

    // 下载完成后删掉旧版本文件（含 .disabled 残留），完成版本转换
    auto removeOldFiles = [destPath](const QStringList &oldFiles) {
        const QString newAbs = QFileInfo(destPath).absoluteFilePath();
        for (const QString &p : oldFiles) {
            if (p.isEmpty())
                continue;
            const QString abs = QFileInfo(p).absoluteFilePath();
            if (abs == newAbs)
                continue;
            if (QFile::exists(abs))
                QFile::remove(abs);
            if (QFile::exists(abs + ".disabled"))
                QFile::remove(abs + ".disabled");
        }
    };

    // 镜像优先下载，失败自动回退官方源
    const QString requestUrl = McimHelper::rewriteFileUrl(versionFile.downloadUrl);
    QStringList candidateUrls;
    if (requestUrl != versionFile.downloadUrl)
        candidateUrls << requestUrl;
    candidateUrls << versionFile.downloadUrl;

    startResourceDownload(this, candidateUrls, destPath, versionFile.sha1,
        [this, taskId, fileName, modInfo, config, oldFilePaths, removeOldFiles](bool success)
        {
            if (success)
            {
                removeOldFiles(oldFilePaths);
                DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::Completed, 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Completed, tr("转换完成"));
                NotificationManager::showSuccess(this, tr("%1 \"%2\" 已转换到所选版本").arg(config.displayName, modInfo.name));
            }
            else
            {
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Failed, tr("转换失败"));
                NotificationManager::showError(this, tr("%1 \"%2\" 转换失败").arg(config.displayName, modInfo.name));
            }
        },
        [this, taskId, fileName](qint64 received, qint64 total)
        {
            DownloadTaskManager::instance()->updateTaskFileProgress(taskId, fileName, received, total);
            if (total > 0)
            {
                int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
            }
        });
}

namespace {

/** 是否正式版（releaseType 为空的历史数据按正式版处理） */
bool isReleaseFile(const ModVersionFile &vf)
{
    return vf.releaseType.isEmpty()
        || vf.releaseType.compare(QStringLiteral("release"), Qt::CaseInsensitive) == 0;
}

/** 从实例版本串中取出纯游戏版本号（如 "1.21.1-fabric0.16.5" → "1.21.1"） */
QString normalizeGameVersion(const QString &raw)
{
    static const QRegularExpression versionRe(QStringLiteral("\\d+\\.\\d+(?:\\.\\d+)?"));
    const QRegularExpressionMatch match = versionRe.match(raw);
    return match.hasMatch() ? match.captured(0) : raw;
}

/** 目标游戏版本是否被该版本文件支持（文件未列出视为不限制） */
bool matchesGameVersion(const ModVersionFile &vf, const QString &gameVersion)
{
    if (gameVersion.isEmpty() || vf.gameVersions.isEmpty())
        return true;
    const QString target = normalizeGameVersion(gameVersion);
    for (const QString &v : vf.gameVersions) {
        if (v.compare(target, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

/** 目标加载器是否被该版本文件支持（文件未列出视为不限制，如资源包/光影） */
bool matchesLoader(const ModVersionFile &vf, const QString &loader)
{
    if (loader.isEmpty() || vf.loaders.isEmpty())
        return true;
    for (const QString &l : vf.loaders) {
        if (l.compare(loader, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

/**
 * @brief 挑选"最新正式版"：先求与目标实例兼容，再逐级放宽
 * @param files       按发布时间倒序的版本列表
 * @param gameVersion 目标游戏版本（可空）
 * @param loader      目标加载器（可空）
 * @param fallbackNonRelease 输出：是否因没有任何正式版而退回测试版本
 */
ModVersionFile pickLatestRelease(const QList<ModVersionFile> &files,
                                 const QString &gameVersion,
                                 const QString &loader,
                                 bool *fallbackNonRelease)
{
    if (fallbackNonRelease)
        *fallbackNonRelease = false;
    if (files.isEmpty())
        return ModVersionFile();

    // 1) 正式版，且游戏版本与加载器都匹配
    for (const ModVersionFile &vf : files) {
        if (isReleaseFile(vf) && matchesGameVersion(vf, gameVersion) && matchesLoader(vf, loader))
            return vf;
    }
    // 2) 正式版，至少游戏版本匹配（加载器标记可能缺失，避免选不出来）
    if (!gameVersion.isEmpty()) {
        for (const ModVersionFile &vf : files) {
            if (isReleaseFile(vf) && matchesGameVersion(vf, gameVersion))
                return vf;
        }
    }
    // 3) 任意正式版
    for (const ModVersionFile &vf : files) {
        if (isReleaseFile(vf))
            return vf;
    }
    // 4) 只有测试版：退回最新版本，由调用方提示
    if (fallbackNonRelease)
        *fallbackNonRelease = true;
    return files.first();
}

} // namespace

void MainWindow::onCardDownloadRequested(const ModInfo &modInfo, ContentType contentType, int depth,
                                         const QString &gameVersion, const QString &loader)
{
    // 根节点：重置已访问集合，开启一次全新的"一键安装"流程
    QString targetGameVersion = gameVersion;
    QString targetLoader = loader;
    const bool isModpack = (contentType == ContentType::Modpack);
    if (depth == 0)
    {
        m_cardDownloadVisited.clear();
        m_cardDownloadDetailTried.clear();
        // 调用方未指定目标时，按当前实例的游戏版本/加载器挑选兼容版本
        if (targetGameVersion.isEmpty())
            targetGameVersion = m_currentInstanceVersion;
        if (targetLoader.isEmpty())
            targetLoader = m_currentInstanceLoader;
        // 整合包自带版本与加载器、装出来是独立新实例：实例的版本/加载器不该参与筛选，
        // 否则会挑成"和当前实例同版本"的旧包，而不是最新版
        if (isModpack)
        {
            targetGameVersion.clear();
            targetLoader.clear();
        }
    }

    if (depth > kMaxCardDownloadDepth)
        return;

    // 整合包装成新实例，不需要先选下载实例
    if (!isModpack && m_currentInstancePath.isEmpty())
    {
        NotificationManager::showError(this,
            tr("请先选择下载实例：用顶部实例按钮或资源页的「下载实例」下拉选择"));
        return;
    }

    const QString visitKey = modInfo.source + ":" + modInfo.id;

    // 卡片搜索结果不含 versionFiles：需要先拉取详情才能拿到最新版本和前置列表
    if (modInfo.versionFiles.isEmpty())
    {
        if (modInfo.source.isEmpty() || modInfo.id.isEmpty())
        {
            NotificationManager::showError(this,
                tr("资源 %1 信息不完整，无法获取版本").arg(modInfo.name));
            return;
        }

        // 每个资源只补全一次详情：拉回来的版本列表仍为空时必须明确提示，
        // 否则会「补全→再进本函数→再补全」无限循环
        if (!visitKey.isEmpty() && m_cardDownloadDetailTried.contains(visitKey))
        {
            NotificationManager::showError(this,
                tr("%1 暂无可安装的版本（可能已被作者删除或只提供站外下载）").arg(modInfo.name));
            return;
        }
        if (!visitKey.isEmpty())
            m_cardDownloadDetailTried.insert(visitKey);

        if (modInfo.source == QStringLiteral("modrinth"))
        {
            ModrinthAPI *api = new ModrinthAPI(this);
            QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
            bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
            api->setBaseUrl(useMcim
                ? QStringLiteral("https://mod.mcimirror.top/modrinth")
                : QStringLiteral("https://api.modrinth.com"));

            connect(api, &ModrinthAPI::modDetailReceived, this,
                [this, api, contentType, depth, modInfo, targetGameVersion, targetLoader](const ModInfo &detail)
            {
                ModInfo merged = detail;
                if (merged.name.isEmpty())
                    merged.name = modInfo.name;
                api->deleteLater();
                // 复用同深度（详情拉取属于"补全当前节点信息"）
                onCardDownloadRequested(merged, contentType, depth, targetGameVersion, targetLoader);
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
                [this, api, contentType, depth, modInfo, targetGameVersion, targetLoader](const ModInfo &detail)
            {
                ModInfo merged = detail;
                if (merged.name.isEmpty())
                    merged.name = modInfo.name;
                api->deleteLater();
                onCardDownloadRequested(merged, contentType, depth, targetGameVersion, targetLoader);
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

    // 循环依赖防护：只有真正进入下载阶段才记账。
    // 卡片搜索结果里没有 versionFiles，需要先拉详情再回到本函数继续；如果提前记账，
    // 第二次进入会被自己挡掉，整条「一键下载」就会毫无提示地中断（点了没反应）。
    if (!visitKey.isEmpty())
    {
        if (m_cardDownloadVisited.contains(visitKey))
        {
            if (depth > 0)
                NotificationManager::showInfo(this,
                    tr("前置 %1 已在本次安装中处理，跳过").arg(modInfo.name));
            return;
        }
        m_cardDownloadVisited.insert(visitKey);
    }

    // 已具备 versionFiles：挑选最新正式版（优先与目标实例的游戏版本/加载器兼容）
    bool fallbackNonRelease = false;
    ModVersionFile latest = pickLatestRelease(modInfo.versionFiles, targetGameVersion, targetLoader,
                                             &fallbackNonRelease);

    // 校验下载链接
    if (latest.downloadUrl.isEmpty())
    {
        if (latest.version.isEmpty())
            NotificationManager::showError(this,
                tr("%1 暂无可下载的版本，安装未能开始").arg(modInfo.name));
        else
            NotificationManager::showError(this,
                tr("%1 %2 没有可用的下载链接，请到详情页换一个版本").arg(modInfo.name, latest.version));
        return;
    }

    // 整合包/存档自身已包含全部内容，不存在"必备前置"概念，
    // 否则会把前置文件塞进 modpacks/、saves/ 这类目录
    const bool supportsDependencies =
        (contentType != ContentType::Modpack && contentType != ContentType::World);

    if (depth == 0)
    {
        int depCount = 0;
        if (supportsDependencies) {
            for (const ModDependency &d : latest.dependencies)
                if (d.isRequired) ++depCount;
        }
        if (isModpack)
            NotificationManager::showInfo(this,
                tr("开始安装整合包 %1 %2，装好后会是一个新实例").arg(modInfo.name, latest.version));
        else
            NotificationManager::showInfo(this,
                tr("开始安装 %1 %2（含 %3 个必备前置）").arg(modInfo.name, latest.version).arg(depCount));
        if (fallbackNonRelease)
            NotificationManager::showInfo(this,
                tr("%1 暂无正式版，已按最新测试版 %2 安装").arg(modInfo.name, latest.version));
    }
    else
    {
        NotificationManager::showInfo(this,
            tr("安装前置：%1 %2").arg(modInfo.name, latest.version));
    }

    // 触发主资源下载（复用现有完整流程：任务栏、进度、通知等）
    onContentDownloadRequested(modInfo, latest, contentType);

    if (!supportsDependencies)
        return;

    // 递归安装该正式版要求的必备前置（同样按目标实例版本/加载器挑选正式版）
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
        onCardDownloadRequested(depInfo, contentType, depth + 1, targetGameVersion, targetLoader);
    }
}

namespace {

/** 实例加载器名（Fabric / Forge / NeoForge / Quilt）→ Modrinth 加载器标识 */
QString loaderFacetName(const QString &loader)
{
    const QString lower = loader.toLower();
    if (lower == QStringLiteral("fabric") || lower == QStringLiteral("quilt")
        || lower == QStringLiteral("forge") || lower == QStringLiteral("neoforge"))
        return lower;
    return QString();
}

} // namespace

void MainWindow::onQuickInstallModRequested(const QString &modKey)
{
    installQuickMod(modKey, false);
}

void MainWindow::installQuickMod(const QString &modKey, bool quiet)
{
    if (m_currentInstancePath.isEmpty())
    {
        if (!quiet)
            NotificationManager::showError(this, tr("请先选择一个实例，再安装模组"));
        return;
    }

    const QString loader = loaderFacetName(m_currentInstanceLoader);
    if (loader.isEmpty())
    {
        if (quiet)
            NotificationManager::showInfo(this,
                tr("当前实例是原版（未安装模组加载器），模组没法自动安装"));
        else
            NotificationManager::showError(this,
                tr("当前实例是原版（未安装模组加载器），无法加载模组。"
                   "请先给实例安装 Fabric / Forge / NeoForge 再使用该功能"));
        return;
    }

    // 按页面的需求选定要装的模组（Modrinth 项目 slug）
    QString slug;
    QString displayName;
    if (modKey == QLatin1String("shader_loader"))
    {
        // Iris 支持 Fabric / Quilt / NeoForge，Forge 上对应的是 Oculus
        const bool isForge = (loader == QLatin1String("forge"));
        slug = isForge ? QStringLiteral("oculus") : QStringLiteral("iris");
        displayName = isForge ? QStringLiteral("Oculus") : QStringLiteral("Iris Shaders");
    }
    else if (modKey == QLatin1String("projection"))
    {
        slug = QStringLiteral("litematica");
        displayName = QStringLiteral("Litematica");
    }
    else if (modKey == QLatin1String("replaymod"))
    {
        slug = QStringLiteral("replaymod");
        displayName = QStringLiteral("Replay Mod");
    }
    else if (modKey == QLatin1String("flashback"))
    {
        slug = QStringLiteral("flashback");
        displayName = QStringLiteral("Flashback");
    }
    else
    {
        return;
    }

    auto *api = new ModrinthAPI(this);
    api->setBaseUrl(McimHelper::modrinthApiBaseUrl());

    const QString gameVersion = m_currentInstanceVersion;
    const QString loaderLabel = m_currentInstanceLoader;
    // 兜底：装不成时带用户去下载页按名称自己挑（例如 NeoForge 上的第三方移植版本）
    const QString fallbackKeyword = displayName;

    connect(api, &ModrinthAPI::modDetailReceived, this,
        [this, api, loader, gameVersion, loaderLabel, displayName, fallbackKeyword, quiet](const ModInfo &detail)
    {
        api->deleteLater();

        // 装之前先确认真有适配当前实例的版本，避免把不兼容的 jar 塞进 mods 目录
        bool compatible = false;
        for (const ModVersionFile &vf : detail.versionFiles)
        {
            if (matchesLoader(vf, loader) && matchesGameVersion(vf, gameVersion))
            {
                compatible = true;
                break;
            }
        }
        if (!compatible)
        {
            if (quiet)
            {
                NotificationManager::showInfo(this,
                    tr("%1 暂无适配当前实例（%2 · %3）的版本，已跳过自动安装")
                        .arg(displayName, gameVersion, loaderLabel));
                return;
            }
            NotificationManager::showInfo(this,
                tr("%1 暂无适配当前实例（%2 · %3）的版本，已打开下载页，可挑选其它同类模组")
                    .arg(displayName, gameVersion, loaderLabel));
            showModDownloadPage();
            if (m_modDownloadPage)
                m_modDownloadPage->setSearchText(fallbackKeyword);
            return;
        }

        ModInfo target = detail;
        if (target.name.isEmpty())
            target.name = displayName;
        // 后续下载/前置递归由 onCardDownloadRequested 统一走「一键安装」流程（自带提示）
        onCardDownloadRequested(target, ContentType::Mod);
    });

    connect(api, &ModrinthAPI::modDetailFailed, this,
        [this, api, displayName, quiet](const QString &error)
    {
        api->deleteLater();
        if (quiet)
            NotificationManager::showInfo(this,
                tr("自动安装 %1 失败：%2").arg(displayName, error));
        else
            NotificationManager::showError(this,
                tr("获取 %1 信息失败：%2").arg(displayName, error));
    });

    api->fetchModDetail(slug);
}

void MainWindow::ensureProjectionModForDownload()
{
    if (m_currentInstancePath.isEmpty())
        return;
    // 实例里已经有投影模组（哪怕目录被手动删了）就不重复装
    if (SchematicDownloadTarget::hasProjectionMod(m_currentInstancePath))
        return;
    // 原版实例装不了模组：说明一声就好，别白跑一次安装
    if (loaderFacetName(m_currentInstanceLoader).isEmpty())
    {
        NotificationManager::showInfo(this,
            tr("当前实例是原版，投影模组装不上；投影文件仍会存进 schematics 文件夹，"
               "装好模组加载器后就能用"));
        return;
    }
    // 同一实例只自动尝试一次；装不上时手动入口还在「实例管理 → 投影」页
    if (m_projectionModAutoTried.contains(m_currentInstancePath))
        return;
    m_projectionModAutoTried.insert(m_currentInstancePath);
    NotificationManager::showInfo(this,
        tr("实例里还没有投影目录，正在自动安装投影模组（Litematica）…"));
    installQuickMod(QStringLiteral("projection"), true);
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
            showModDetailPage(modInfo, m_currentInstanceVersion, m_currentInstanceLoader);
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
        restoreSideBarWithSubNav(1, 0);
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


