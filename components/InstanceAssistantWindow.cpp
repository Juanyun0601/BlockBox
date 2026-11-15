#include "InstanceAssistantWindow.h"

#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QEventLoop>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QParallelAnimationGroup>
#include <QScreen>
#include <QScrollBar>
#include <QWindow>
#include <QSequentialAnimationGroup>
#include <QShowEvent>
#include <QTimer>
#include <QtConcurrent>
#include <QFutureWatcher>

#include "platform.h"
#include "utils/ResourceManager.h"
#include "utils/SettingsManager.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"
#include "utils/CommandAssistant/CommandPack.h"
#include "utils/CommandAssistant/GameDetector.h"
#include "utils/CommandAssistant/GameRegistry.h"
#include "components/CommandPackManagerDialog.h"
#include "pages/AiChatPage.h"
#include "pages/InstanceHomePage.h"
#include "pages/InstanceJavaDownloadPage.h"
#include "pages/InstanceJavaPage.h"
#include "pages/InstanceResourcesPage.h"
#include "pages/MultiplayerPage.h"

// ============================================================================
// 后台上下文检测（工作线程执行，避免阻塞 UI）
// ============================================================================
namespace {

/**
 * @brief 后台检测汇总结果：运行中游戏信息 + 全量注册表数据 + level.dat 上下文
 */
struct BackgroundDetectionResult
{
    std::optional<GameProcessInfo> gameInfo;  ///< 运行中的游戏信息（可能未检测到）
    GameRegistry gameRegistry;                ///< 后台提取的方块/物品/实体/效果/附魔数据
    std::optional<LevelInfo> levelInfo;       ///< level.dat 解析结果
    QString scanPath;                         ///< 实际用于扫描的目录（游戏 gameDir 优先）
    QString detectedVersion;                  ///< 检测得到的游戏版本
};

/**
 * @brief 扫描指定实例路径下最新的 level.dat 并解析上下文（纯 IO，供后台线程调用）
 */
std::optional<LevelInfo> detectLevelDatFromPath(const QString &instancePath)
{
    if (instancePath.isEmpty())
    {
        return std::nullopt;
    }

    // 实例路径可能是 versions/{ver} 目录或 .minecraft 根目录，需要向上查找
    QDir savesDir;
    const QStringList candidatePaths = {
        QDir(instancePath).absoluteFilePath(QStringLiteral("saves")),
        QDir(instancePath).absoluteFilePath(QStringLiteral("../saves")),
        QDir(instancePath).absoluteFilePath(QStringLiteral("../../saves"))
    };
    for (const QString &candidate : candidatePaths)
    {
        QDir dir(candidate);
        if (dir.exists())
        {
            savesDir = dir;
            break;
        }
    }

    QString latestLevelDat;
    QDateTime latestTime;
    if (savesDir.exists())
    {
        const QStringList subDirs = savesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &sub : subDirs)
        {
            QString candidate = savesDir.absoluteFilePath(sub + QStringLiteral("/level.dat"));
            QFileInfo info(candidate);
            if (info.exists() && info.isFile())
            {
                if (!latestTime.isValid() || info.lastModified() > latestTime)
                {
                    latestTime = info.lastModified();
                    latestLevelDat = candidate;
                }
            }
        }
    }

    if (!latestLevelDat.isEmpty())
    {
        return LevelDatReader::readLevelDat(latestLevelDat);
    }
    return std::nullopt;
}

/**
 * @brief 后台执行完整的上下文检测：游戏进程 → 方块注册表 → level.dat
 *
 * 注意：本函数在 QtConcurrent 工作线程执行，内部会启动 PowerShell 查询
 * 进程命令行并解析游戏 JAR，耗时可能达数秒，禁止在 UI 线程直接调用。
 */
BackgroundDetectionResult runBackgroundDetection(const QString &instancePath, const QString &version)
{
    BackgroundDetectionResult result;
    QString scanPath = instancePath;
    QString effectiveVersion = version;

    // 1. 深度检测运行中的游戏（PowerShell 查询命令行，仅在此处调用）
    if (GameDetector::isJavaGameRunning())
    {
        auto gameInfo = GameDetector::detectRunningGame();
        if (gameInfo.has_value())
        {
            result.gameInfo = *gameInfo;
            if (!gameInfo->gameDir.isEmpty())
            {
                scanPath = gameInfo->gameDir;
            }
            if (!gameInfo->version.isEmpty())
            {
                effectiveVersion = gameInfo->version;
            }
        }
    }

    // 2. 全量注册表数据提取：方块 / 物品 / 实体 / 效果 / 附魔
    //    （解析版本 JAR + 全部 mods JAR 的 lang 翻译文件，IO 密集，后台执行）
    if (!scanPath.isEmpty() && !effectiveVersion.isEmpty())
    {
        if (result.gameInfo.has_value())
        {
            result.gameRegistry.extractFromGameDir(scanPath, effectiveVersion);
        }
        else
        {
            // 实例路径为 versions/{ver} 目录，JAR 在该目录下
            QString jarPath = scanPath + QStringLiteral("/") + effectiveVersion + QStringLiteral(".jar");
            if (QFileInfo::exists(jarPath))
            {
                result.gameRegistry.extractFromJar(jarPath);
            }
            // 模组目录在 .minecraft 根目录下的 mods/
            QString modsDir = QDir(scanPath).absoluteFilePath(QStringLiteral("../../mods"));
            if (QFileInfo::exists(modsDir))
            {
                QDir modFolder(modsDir);
                const QStringList modJars = modFolder.entryList({QStringLiteral("*.jar")}, QDir::Files);
                for (const QString &modJar : modJars)
                {
                    result.gameRegistry.extractFromJar(modFolder.absoluteFilePath(modJar));
                }
            }
        }
    }

    // 3. level.dat 上下文检测（纯 IO）
    result.levelInfo = detectLevelDatFromPath(scanPath);
    result.scanPath = scanPath;
    result.detectedVersion = effectiveVersion;
    return result;
}

} // namespace

namespace {

// 悬浮窗内容区尺寸范围（不含四周阴影边距）
constexpr int kMinContentWidth = 400;
constexpr int kMaxContentWidth = 620;
constexpr int kMinContentHeight = 480;
constexpr int kDefaultContentWidth = 450;
constexpr int kDefaultContentHeight = 760;

// 设置键：记住悬浮窗位置、大小与置顶状态
const QString kGeometryKey = QStringLiteral("instanceAssistant/windowGeometry");
const QString kPinnedKey = QStringLiteral("instanceAssistant/pinned");

} // namespace

InstanceAssistantWindow::InstanceAssistantWindow(QWidget *parent)
    : QWidget(parent)
    , m_outerLayout(nullptr)
    , m_frame(nullptr)
    , m_mainLayout(nullptr)
    , m_navBar(nullptr)
    , m_scrollArea(nullptr)
    , m_navContainer(nullptr)
    , m_navLayout(nullptr)
    , m_selectionHighlight(nullptr)
    , m_stackedWidget(nullptr)
    , m_highlightAnim(nullptr)
    , m_currentIndex(0)
    , m_highlightPos(0)
    , m_prevIndex(0)
    , m_titleBar(nullptr)
    , m_titleIcon(nullptr)
    , m_titleLabel(nullptr)
    , m_statsHost(nullptr)
    , m_statFps(nullptr)
    , m_statCpu(nullptr)
    , m_statGpu(nullptr)
    , m_statMem(nullptr)
    , m_pinBtn(nullptr)
    , m_collapseBtn(nullptr)
    , m_closeBtn(nullptr)
    , m_inputDebounceTimer(nullptr)
    , m_contextLabel(nullptr)
    , m_serverToggleBtn(nullptr)
    , m_opLevelCombo(nullptr)
    , m_redetectBtn(nullptr)
    , m_commandInput(nullptr)
    , m_completionList(nullptr)
    , m_englishOutput(nullptr)
    , m_copyBtn(nullptr)
    , m_injectBtn(nullptr)
    , m_historyBtn(nullptr)
    , m_packBtn(nullptr)
    , m_presetBtn(nullptr)
    , m_commandDb(nullptr)
    , m_translator(nullptr)
    , m_completer(nullptr)
    , m_blockRegistry(nullptr)
    , m_aiChatPage(nullptr)
    , m_homePage(nullptr)
    , m_multiplayerPage(nullptr)
    , m_resourcesPage(nullptr)
    , m_javaStack(nullptr)
    , m_javaPage(nullptr)
    , m_javaDownloadPage(nullptr)
{
    setWindowTitle(tr("实例助手"));

    // 桌面端：改成悬浮窗形态（无边框、圆角、可拖动、默认置顶、不占任务栏）；
    // 移动端保持原有独立窗口 / 游戏内覆盖侧栏，不套桌面悬浮窗外观。
    m_floating = !Platform::isMobile();

    // 窗口骨架：外层布局留出阴影边距，卡片容器里放标题栏 + 导航 + 页面
    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(0, 0, 0, 0);
    m_outerLayout->setSpacing(0);
    m_frame = new QWidget(this);
    m_frame->setObjectName(QStringLiteral("assistantFrame"));
    m_outerLayout->addWidget(m_frame);
    m_mainLayout = new QVBoxLayout(m_frame);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    if (m_floating) {
        applyFloatingWindow();
    } else {
        setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint);
        setFixedWidth(450);
        setMinimumHeight(500);
    }

    // 初始化快捷指令核心模块
    m_commandDb = new CommandDatabase();
    m_commandDb->load();
    m_translator = new CommandTranslator(m_commandDb);
    m_completer = new CommandCompleter(m_commandDb);
    m_blockRegistry = new BlockRegistry();
    m_completer->setBlockRegistry(m_blockRegistry);

    // 输入防抖：合并高频击键，停顿 60ms 后统一执行补全 + 翻译
    m_inputDebounceTimer = new QTimer(this);
    m_inputDebounceTimer->setSingleShot(true);
    m_inputDebounceTimer->setInterval(60);
    connect(m_inputDebounceTimer, &QTimer::timeout,
            this, &InstanceAssistantWindow::flushDebouncedInput);

    initUI();
    applyThemeStyles();

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &InstanceAssistantWindow::applyThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &InstanceAssistantWindow::applyThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::textColorChanged,
            this, &InstanceAssistantWindow::applyThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::borderColorChanged,
            this, &InstanceAssistantWindow::applyThemeStyles);

    if (m_floating) {
        restoreWindowGeometry();
    }
}

InstanceAssistantWindow::~InstanceAssistantWindow()
{
    if (m_floating) {
        saveWindowGeometry();
    }
    delete m_translator;
    delete m_completer;
    delete m_blockRegistry;
    delete m_commandDb;
}

void InstanceAssistantWindow::initUI()
{
    if (m_floating) {
        // 悬浮窗标题栏放在导航栏上方，既是拖动区域也提供置顶 / 收起 / 关闭
        buildTitleBar();
    }

    // Navigation bar (outer container)
    m_navBar = new QWidget();
    m_navBar->setObjectName("assistantNavBar");
    m_navBar->setFixedHeight(56);

    auto *navBarLayout = new QHBoxLayout(m_navBar);
    navBarLayout->setContentsMargins(0, 6, 0, 0);
    navBarLayout->setSpacing(0);

    // Scroll area for nav buttons
    m_scrollArea = new QScrollArea();
    m_scrollArea->setObjectName("assistantNavScroll");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet(
        "#assistantNavScroll {"
        "  background: transparent;"
        "  border: none;"
        "}"
    );

    // Container widget for buttons
    m_navContainer = new QWidget();
    m_navContainer->setObjectName("assistantNavContainer");
    m_navContainer->setStyleSheet(
        "#assistantNavContainer {"
        "  background: transparent;"
        "}"
    );

    m_navLayout = new QHBoxLayout(m_navContainer);
    m_navLayout->setContentsMargins(4, 0, 4, 0);
    m_navLayout->setSpacing(2);

    QStringList navItems = {tr("首页"), tr("AI助手"), tr("快捷指令"), tr("联机"), tr("资源管理"), tr("Java管理")};
    for (int i = 0; i < navItems.size(); ++i) {
        QPushButton *btn = createNavButton(navItems[i], i);
        m_navButtons.append(btn);
        m_navLayout->addWidget(btn);
    }

    m_scrollArea->setWidget(m_navContainer);
    navBarLayout->addWidget(m_scrollArea);

    // Selection highlight bar (child of container, scrolls with it)
    m_selectionHighlight = new QWidget(m_navContainer);
    m_selectionHighlight->setObjectName("assistantHighlight");
    m_selectionHighlight->setFixedHeight(3);
    m_selectionHighlight->raise();

    // Stacked widget for pages
    m_stackedWidget = new QStackedWidget();
    m_stackedWidget->setObjectName("assistantStackedWidget");


    for (int i = 0; i < navItems.size(); ++i) {
        QWidget *page = new QWidget();
        page->setObjectName(QString("assistantPage%1").arg(i));
        m_stackedWidget->addWidget(page);

        // index 0 为"首页"页面，复用 InstanceHomePage 组件
        if (i == 0) {
            buildHomePage(page);
        }
        // index 1 为"AI助手"页面，复用 AiChatPage 组件
        if (i == 1) {
            buildAiChatPage(page);
        }
        // index 2 为"快捷指令"页面，构建专用 UI
        if (i == 2) {
            buildCommandAssistantPage(page);
        }
        // index 3 为"联机"页面，复用 MultiplayerPage 组件
        if (i == 3) {
            buildMultiplayerPage(page);
        }
        // index 4 为"资源管理"页面，复用 InstanceResourcesPage 组件
        if (i == 4) {
            buildResourcesPage(page);
        }
        // index 5 为"Java管理"页面，内部用 QStackedWidget 切换管理/下载两个子页
        if (i == 5) {
            buildJavaPage(page);
        }
    }

    m_mainLayout->addWidget(m_navBar);
    m_mainLayout->addWidget(m_stackedWidget, 1);

    // Animation
    m_highlightAnim = new QPropertyAnimation(this, "highlightPos", this);
    m_highlightAnim->setDuration(250);
    m_highlightAnim->setEasingCurve(QEasingCurve::OutCubic);

    // Default state
    if (!m_navButtons.isEmpty()) {
        m_navButtons[0]->setChecked(true);
    }
    m_stackedWidget->setCurrentIndex(0);
}

void InstanceAssistantWindow::applyThemeStyles()
{
    QString themeColor = ThemeManager::instance()->currentThemeColor();
    QString textColor = ThemeManager::instance()->currentTextColor();
    QString borderColor = ThemeManager::instance()->currentBorderColor();

    // 悬浮窗的圆角卡片由 paintEvent 统一绘制，卡片内部各层保持透明，
    // 让四角真正露出圆角；移动端独立窗口 / 侧栏则仍用主题底色铺满。
    const QString surface = m_floating ? QStringLiteral("transparent")
                                       : QStringLiteral("@BG_BASE@");

    // 页面底色跟着主题走：深色主题下不要再露出系统浅色
    if (m_stackedWidget) {
        m_stackedWidget->setStyleSheet(StyleKit::resolve(QStringLiteral(
            "#assistantStackedWidget { background: %1; }").arg(surface)));
    }
    if (m_javaStack) {
        m_javaStack->setStyleSheet(StyleKit::resolve(QStringLiteral(
            "#assistantJavaStack { background: %1; }").arg(surface)));
    }

    QColor tc(themeColor);
    QString hoverBg = QString("rgba(%1, %2, %3, 0.12)")
        .arg(tc.red()).arg(tc.green()).arg(tc.blue());

    m_navBar->setStyleSheet(
        StyleKit::resolve(QStringLiteral(
            "#assistantNavBar {"
            "  background: %1;"
            "  border-bottom: 1px solid %2;"
            "}"
        ).arg(surface, borderColor))
    );

    // 标题栏：透明底，交给卡片本身统一绘制底色
    if (m_titleBar) {
        m_titleBar->setStyleSheet(StyleKit::resolve(QStringLiteral(
            "#assistantTitleBar { background: transparent; border: none; }")));
        m_titleLabel->setStyleSheet(
            QString("color: %1; font-size: 13px; font-weight: 600; background: transparent;")
                .arg(textColor));
        m_titleIcon->setStyleSheet(QStringLiteral("background: transparent;"));

        const QString headerBtnStyle = QString(
            "QPushButton {"
            "  border: none;"
            "  border-radius: 6px;"
            "  background: transparent;"
            "  padding: 0;"
            "}"
            "QPushButton:hover { background: %1; }"
            "QPushButton:checked { background: rgba(%2, %3, %4, 0.18); }")
            .arg(hoverBg).arg(tc.red()).arg(tc.green()).arg(tc.blue());

        for (QPushButton *btn : {m_pinBtn, m_collapseBtn, m_closeBtn}) {
            if (btn) {
                btn->setStyleSheet(headerBtnStyle);
            }
        }

        // 收起态实时数据：小号字、透明底，数值颜色跟随各自监控卡片
        for (QLabel *label : {m_statFps, m_statCpu, m_statGpu, m_statMem}) {
            if (label) {
                label->setStyleSheet(QStringLiteral("background: transparent; font-size: 11px;"));
            }
        }
        refreshQuickStatsTexts();
        updateHeaderButtons();
    }

    updateHighlightStyle(m_selectionHighlight ? m_selectionHighlight->width() : 0);

    QString btnStyle = QString(
        "QPushButton {"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 4px 12px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "  color: %1;"
        "  background: transparent;"
        ""
        "}"
        "QPushButton:hover {"
        "  background: %2;"
        "}"
        "QPushButton:checked {"
        "  color: %3;"
        "}"
    ).arg(textColor, hoverBg, themeColor);

    for (auto *btn : m_navButtons) {
        btn->setStyleSheet(btnStyle);
    }

    update();
}

QPushButton* InstanceAssistantWindow::createNavButton(const QString &text, int index)
{
    QPushButton *btn = new QPushButton(text);
    btn->setObjectName(QString("assistantNavBtn%1").arg(index));
    btn->setCheckable(true);
    btn->setFixedHeight(34);
    btn->setCursor(Qt::PointingHandCursor);

    connect(btn, &QPushButton::clicked, this, [this, index]() {
        if (index != m_currentIndex) {
            switchToTab(index);
        }
    });

    return btn;
}

void InstanceAssistantWindow::switchToTab(int index)
{
    m_prevIndex = m_currentIndex;
    m_currentIndex = index;

    for (int i = 0; i < m_navButtons.size(); ++i) {
        m_navButtons[i]->setChecked(i == index);
    }

    ensureTabVisible(index);

    if (m_highlightAnim->state() == QPropertyAnimation::Running) {
        m_highlightAnim->stop();
    }
    // 高亮条宽度跟随当前标签页宽度，位置从上一个标签平滑滑过去
    QPushButton *targetBtn = m_navButtons[index];
    int barWidth = qMax(20, targetBtn->width() / 2);
    updateHighlightStyle(barWidth);
    m_selectionHighlight->setFixedWidth(barWidth);
    m_highlightAnim->setStartValue(m_highlightPos);
    m_highlightAnim->setEndValue(targetBtn->x() + targetBtn->width() / 2 - barWidth / 2);
    m_highlightAnim->start();

    animatePageFade(m_prevIndex, index);
}

void InstanceAssistantWindow::updateHighlightGeometry()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_navButtons.size())
        return;

    QPushButton *btn = m_navButtons[m_currentIndex];
    int barWidth = btn->width() * 0.5;
    if (barWidth < 20) barWidth = 20;

    updateHighlightStyle(barWidth);
    m_selectionHighlight->setFixedWidth(barWidth);
    int x = btn->x() + btn->width() / 2 - barWidth / 2;
    m_selectionHighlight->move(x, m_navContainer->height() - 3);
    m_highlightPos = x;
}

void InstanceAssistantWindow::updateHighlightStyle(int width)
{
    if (!m_selectionHighlight) {
        return;
    }

    // 宽度也在控件自身样式里写死：全局样式表对 #assistantHighlight 的历史宽度规则
    // 若还留在资源里，会盖掉 setFixedWidth，导致高亮条缩成一个小方块。
    const int barWidth = qMax(3, width);
    m_selectionHighlight->setStyleSheet(
        QString(
            "#assistantHighlight {"
            "  background: %1;"
            "  border-radius: 2px;"
            "  min-width: %2px;"
            "  max-width: %2px;"
            "}"
        ).arg(ThemeManager::instance()->currentThemeColor()).arg(barWidth)
    );
}

void InstanceAssistantWindow::setHighlightPos(int pos)
{
    m_highlightPos = pos;
    m_selectionHighlight->move(pos, m_navContainer->height() - 3);
}

void InstanceAssistantWindow::ensureTabVisible(int index)
{
    if (index < 0 || index >= m_navButtons.size())
        return;

    QPushButton *btn = m_navButtons[index];
    int scrollViewWidth = m_scrollArea->viewport()->width();
    int scrollPos = m_scrollArea->horizontalScrollBar()->value();
    int btnLeft = btn->x();
    int btnRight = btn->x() + btn->width();

    if (btnLeft < scrollPos) {
        m_scrollArea->horizontalScrollBar()->setValue(btnLeft - 8);
    } else if (btnRight > scrollPos + scrollViewWidth) {
        m_scrollArea->horizontalScrollBar()->setValue(btnRight - scrollViewWidth + 8);
    }
}

void InstanceAssistantWindow::animatePageFade(int from, int to)
{
    if (from == to)
        return;

    QWidget *fromWidget = m_stackedWidget->widget(from);
    QWidget *toWidget = m_stackedWidget->widget(to);

    if (!fromWidget || !toWidget)
        return;

    auto *fromEffect = new QGraphicsOpacityEffect(fromWidget);
    fromEffect->setOpacity(1.0);
    fromWidget->setGraphicsEffect(fromEffect);

    auto *toEffect = new QGraphicsOpacityEffect(toWidget);
    toEffect->setOpacity(0.0);
    toWidget->setGraphicsEffect(toEffect);

    m_stackedWidget->setCurrentWidget(toWidget);

    auto *fromAnim = new QPropertyAnimation(fromEffect, "opacity", this);
    fromAnim->setDuration(200);
    fromAnim->setStartValue(1.0);
    fromAnim->setEndValue(0.0);
    fromAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto *toAnim = new QPropertyAnimation(toEffect, "opacity", this);
    toAnim->setDuration(200);
    toAnim->setStartValue(0.0);
    toAnim->setEndValue(1.0);
    toAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto *group = new QParallelAnimationGroup(this);
    group->addAnimation(fromAnim);
    group->addAnimation(toAnim);

    connect(group, &QParallelAnimationGroup::finished, this, [fromWidget]() {
        fromWidget->setGraphicsEffect(nullptr);
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void InstanceAssistantWindow::setOverlayMode(bool enabled)
{
    if (m_overlayMode == enabled) {
        return;
    }
    m_overlayMode = enabled;

    if (m_overlayMode) {
        // 侧栏模式：解除构造时的固定尺寸限制，几何由 applyOverlayGeometry 按屏幕计算；
        // 整窗 80% 不透明度呈现半透明效果（悬浮于游戏画面之上）
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setWindowOpacity(0.80);

        // 侧栏形态不套悬浮窗外观：不画圆角 / 阴影，也不显示标题栏
        m_floating = false;
        setAttribute(Qt::WA_TranslucentBackground, false);
        if (m_titleBar) {
            m_titleBar->hide();
        }
        if (m_outerLayout) {
            m_outerLayout->setContentsMargins(0, 0, 0, 0);
        }
        applyThemeStyles();
        applyOverlayGeometry();
    } else {
        m_floating = !Platform::isMobile();
        setWindowOpacity(1.0);
        if (m_floating) {
            applyFloatingWindow();
            if (m_titleBar) {
                m_titleBar->show();
            }
            restoreWindowGeometry();
        } else {
            setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint);
            setFixedWidth(450);
            setMinimumHeight(500);
        }
        applyThemeStyles();
    }
}

void InstanceAssistantWindow::applyOverlayGeometry()
{
    if (!m_overlayMode) {
        return;
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (const QWindow *handle = windowHandle()) {
        if (QScreen *ownScreen = handle->screen()) {
            screen = ownScreen;
        }
    }
    if (!screen) {
        return;
    }

    const QRect available = screen->availableGeometry();
    // 右侧四分之一屏宽；竖屏等极窄屏幕保底 240px，保证内容可用
    const int width = qMax(available.width() / 4, 240);
    setGeometry(available.x() + available.width() - width,
                available.y(), width, available.height());
}

// ============================
// 桌面悬浮窗（标题栏 / 圆角阴影 / 拖动缩放 / 位置记忆）
// ============================

void InstanceAssistantWindow::buildTitleBar()
{
    m_titleBar = new QWidget(m_frame);
    m_titleBar->setObjectName(QStringLiteral("assistantTitleBar"));
    m_titleBar->setFixedHeight(40);
    // 标题栏同时是拖动区：空白处按下即可移动整个悬浮窗
    m_titleBar->installEventFilter(this);

    auto *layout = new QHBoxLayout(m_titleBar);
    layout->setContentsMargins(12, 4, 6, 4);
    layout->setSpacing(4);

    m_titleIcon = new QLabel(m_titleBar);
    m_titleIcon->setFixedSize(18, 18);
    m_titleIcon->setAlignment(Qt::AlignCenter);
    // 图标与文字都不接收鼠标事件，按下时交给标题栏处理拖动
    m_titleIcon->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_titleLabel = new QLabel(tr("实例助手"), m_titleBar);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // 收起态实时数据：帧率 / CPU / GPU / 内存，默认隐藏，收起后显示在标题文字右侧
    m_statsHost = new QWidget(m_titleBar);
    m_statsHost->setObjectName(QStringLiteral("assistantStatsHost"));
    auto *statsLayout = new QHBoxLayout(m_statsHost);
    // 左侧留一点空，和标题文字分开，不贴着「实例助手」
    statsLayout->setContentsMargins(6, 0, 0, 0);
    statsLayout->setSpacing(10);

    auto makeStatLabel = [this, statsLayout]() -> QLabel * {
        auto *label = new QLabel(m_statsHost);
        label->setTextFormat(Qt::RichText);
        label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        statsLayout->addWidget(label);
        return label;
    };
    m_statFps = makeStatLabel();
    m_statCpu = makeStatLabel();
    m_statGpu = makeStatLabel();
    m_statMem = makeStatLabel();
    m_statsHost->hide();

    m_pinBtn = new QPushButton(m_titleBar);
    m_pinBtn->setObjectName(QStringLiteral("assistantPinBtn"));
    m_pinBtn->setFixedSize(24, 24);
    m_pinBtn->setIconSize(QSize(14, 14));
    m_pinBtn->setCheckable(true);
    m_pinBtn->setCursor(Qt::PointingHandCursor);
    connect(m_pinBtn, &QPushButton::clicked, this, &InstanceAssistantWindow::togglePinned);

    m_collapseBtn = new QPushButton(m_titleBar);
    m_collapseBtn->setObjectName(QStringLiteral("assistantCollapseBtn"));
    m_collapseBtn->setFixedSize(24, 24);
    m_collapseBtn->setIconSize(QSize(14, 14));
    m_collapseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_collapseBtn, &QPushButton::clicked, this, [this]() {
        setCollapsed(!m_collapsed);
    });

    m_closeBtn = new QPushButton(m_titleBar);
    m_closeBtn->setObjectName(QStringLiteral("assistantCloseBtn"));
    m_closeBtn->setFixedSize(24, 24);
    m_closeBtn->setIconSize(QSize(14, 14));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        hide();
    });

    layout->addWidget(m_titleIcon);
    layout->addSpacing(2);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_statsHost);
    layout->addStretch(1);
    layout->addWidget(m_pinBtn);
    layout->addWidget(m_collapseBtn);
    layout->addWidget(m_closeBtn);

    m_mainLayout->addWidget(m_titleBar);
    refreshQuickStatsTexts();
    updateHeaderButtons();
}

void InstanceAssistantWindow::applyFloatingWindow()
{
    // 与启动器主窗口彻底分开：撤掉父子/所有者关系，实例助手就是任务栏里的
    // 独立窗口。主窗口最小化、隐藏、关闭都不会把它一起带走，两个窗口也能
    // 分别激活、切换层级。
    if (QWidget *host = parentWidget()) {
        m_launcherRect = QRect(host->mapToGlobal(QPoint(0, 0)), host->size());
        setParent(nullptr);
    }
    // 主窗口关闭时照常退出程序，不要让独立窗口把进程留住
    setAttribute(Qt::WA_QuitOnClose, false);

    // 默认不再置顶：作为普通窗口先露在打开它的那一刻，需要悬在游戏上时再点图钉
    m_pinned = SettingsManager::instance()->getProperty(kPinnedKey, false).toBool();

    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint;
    if (m_pinned) {
        flags |= Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground, true);
    // 边缘悬浮光标提示：需要在没有按键时也能收到 mouseMoveEvent
    setMouseTracking(true);

    setMinimumSize(kMinContentWidth + 2 * m_shadowMargin,
                   kMinContentHeight + 2 * m_shadowMargin);
    setMaximumSize(kMaxContentWidth + 2 * m_shadowMargin, QWIDGETSIZE_MAX);

    if (m_outerLayout) {
        m_outerLayout->setContentsMargins(m_shadowMargin, m_shadowMargin,
                                          m_shadowMargin, m_shadowMargin);
    }
    if (m_frame) {
        // 卡片底色与圆角由窗口 paintEvent 绘制，容器和页面都保持透明
        m_frame->setStyleSheet(QStringLiteral(
            "#assistantFrame { background: transparent; border: none; }"));
    }
}

void InstanceAssistantWindow::updateHeaderButtons()
{
    if (!m_pinBtn) {
        return;
    }

    auto *rm = ResourceManager::instance();

    if (m_titleIcon) {
        m_titleIcon->setPixmap(rm->loadThemedIcon(
            QStringLiteral(":/Images/Icons/instance_assistant.svg")).pixmap(16, 16));
    }

    m_pinBtn->setChecked(m_pinned);
    m_pinBtn->setIcon(rm->loadThemedIcon(m_pinned
        ? QStringLiteral(":/Images/Icons/pin_filled.svg")
        : QStringLiteral(":/Images/Icons/pin.svg")));
    m_pinBtn->setToolTip(m_pinned
        ? tr("已置顶，点击取消置顶")
        : tr("置顶显示，悬浮在其他窗口之上"));

    if (m_collapseBtn) {
        m_collapseBtn->setIcon(rm->loadThemedIcon(m_collapsed
            ? QStringLiteral(":/Images/Icons/expand.svg")
            : QStringLiteral(":/Images/Icons/chevron_down.svg")));
        m_collapseBtn->setToolTip(m_collapsed ? tr("展开实例助手") : tr("收起成悬浮小条"));
    }
    if (m_closeBtn) {
        m_closeBtn->setIcon(rm->loadThemedIcon(
            QStringLiteral(":/Images/Icons/close.svg")));
        m_closeBtn->setToolTip(tr("关闭悬浮窗（可从顶栏重新打开）"));
    }
}

void InstanceAssistantWindow::togglePinned()
{
    if (!m_floating) {
        return;
    }

    m_pinned = !m_pinned;
    SettingsManager::instance()->setProperty(kPinnedKey, m_pinned);

    // 切换置顶标志会重建原生窗口，显示状态下需要重新 show 一次
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, m_pinned);
    if (wasVisible) {
        show();
        raise();
    }
    updateHeaderButtons();
}

void InstanceAssistantWindow::setCollapsed(bool collapsed)
{
    if (!m_floating || m_collapsed == collapsed) {
        return;
    }
    m_collapsed = collapsed;

    if (m_collapsed) {
        m_expandedSize = size();
        m_navBar->hide();
        m_stackedWidget->hide();
        // 收起后窗口只剩一条标题栏，临时把最小高度放宽
        setMinimumHeight(2 * m_shadowMargin + m_titleBar->height());
        resize(width(), minimumHeight());
        // 标题栏右侧直接露出帧率 / CPU / GPU / 内存
        refreshQuickStatsTexts();
        if (m_statsHost) {
            m_statsHost->show();
        }
        fitQuickStatsToWidth();
    } else {
        m_navBar->show();
        m_stackedWidget->show();
        if (m_statsHost) {
            m_statsHost->hide();
        }
        setMinimumHeight(kMinContentHeight + 2 * m_shadowMargin);
        const QSize expanded = m_expandedSize.isValid()
            ? m_expandedSize
            : QSize(kDefaultContentWidth + 2 * m_shadowMargin,
                    kDefaultContentHeight + 2 * m_shadowMargin);
        resize(expanded.expandedTo(minimumSize()).boundedTo(maximumSize()));
    }

    updateHeaderButtons();
    update();
}

void InstanceAssistantWindow::updateLiveStats(int fps, int cpuPercent, int memoryPercent, int gpuPercent,
                                              bool cpuAvailable, bool memoryAvailable, bool gpuAvailable)
{
    m_liveFps = qMax(0, fps);
    m_liveCpuPercent = cpuPercent;
    m_liveMemoryPercent = memoryPercent;
    m_liveGpuPercent = gpuPercent;
    m_liveCpuAvailable = cpuAvailable;
    m_liveMemoryAvailable = memoryAvailable;
    m_liveGpuAvailable = gpuAvailable;

    refreshQuickStatsTexts();
    if (m_collapsed) {
        fitQuickStatsToWidth();
    }
}

void InstanceAssistantWindow::refreshQuickStatsTexts()
{
    if (!m_statFps) {
        return;
    }

    const QString tertiary = StyleKit::resolve(QStringLiteral("@TEXT_TERTIARY@"));
    const QString themeColor = ThemeManager::instance()->currentThemeColor();
    const QString memoryColor = ThemeManager::instance()->currentInfoAccentColor();

    auto format = [](const QString &name, const QString &value,
                     const QString &nameColor, const QString &valueColor) {
        return QStringLiteral(
            "<span style=\"color:%1\">%2</span> <span style=\"color:%3; font-weight:600\">%4</span>")
            .arg(nameColor, name, valueColor, value);
    };
    auto percent = [](bool available, int value) {
        return available ? QString::number(value) + QLatin1Char('%') : QStringLiteral("--");
    };

    m_statFps->setText(format(tr("帧率"),
        m_liveFps > 0 ? QString::number(m_liveFps) : QStringLiteral("--"),
        tertiary, themeColor));
    m_statCpu->setText(format(QStringLiteral("CPU"),
        percent(m_liveCpuAvailable, m_liveCpuPercent), tertiary, QStringLiteral("#4CAF50")));
    m_statGpu->setText(format(QStringLiteral("GPU"),
        percent(m_liveGpuAvailable, m_liveGpuPercent), tertiary, QStringLiteral("#9C27B0")));
    m_statMem->setText(format(tr("内存"),
        percent(m_liveMemoryAvailable, m_liveMemoryPercent), tertiary, memoryColor));
}

void InstanceAssistantWindow::fitQuickStatsToWidth()
{
    if (!m_floating || !m_collapsed || !m_statsHost || !m_titleBar) {
        return;
    }

    QLabel *labels[] = {m_statFps, m_statCpu, m_statGpu, m_statMem};
    if (!labels[0] || !labels[1] || !labels[2] || !labels[3]) {
        return;
    }

    const int spacing = m_titleBar->layout() ? m_titleBar->layout()->spacing() : 4;
    const int barWidth = width() - 2 * m_shadowMargin;

    // 除数据之外占掉的宽度：左右内边距 + 图标 + 间距 + 标题 + 三个按钮 + 控件间隔
    const int fixed = 12 + 6
        + m_titleIcon->sizeHint().width() + 2
        + m_titleLabel->sizeHint().width()
        + 3 * 24
        + 7 * spacing;
    const int available = barWidth - fixed;

    int total = 0;
    for (QLabel *label : labels) {
        total += label->sizeHint().width();
    }
    total += spacing * 3 + 6; // 3 个间隔 + 容器左内边距

    // 窄窗口下从后面开始少显示：先丢内存，再丢 GPU……
    int visible = 4;
    while (visible > 0 && total > available) {
        --visible;
        total -= labels[visible]->sizeHint().width() + spacing;
    }

    for (int i = 0; i < 4; ++i) {
        labels[i]->setVisible(i < visible);
    }
    m_statsHost->setVisible(visible > 0);
}

QRect InstanceAssistantWindow::cardRect() const
{
    return rect().adjusted(m_shadowMargin, m_shadowMargin,
                           -m_shadowMargin, -m_shadowMargin);
}

void InstanceAssistantWindow::paintEvent(QPaintEvent *event)
{
    if (!m_floating) {
        QWidget::paintEvent(event);
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card(cardRect());
    const qreal radius = m_cornerRadius;

    // 柔和投影：由外到内叠加一圈圈半透明圆角描边。
    // 相比 QGraphicsDropShadowEffect 不做整窗离屏渲染，聊天流式刷新时不掉帧。
    for (int i = 1; i <= m_shadowMargin; ++i) {
        const int alpha = qMax(2, 24 - i * 2 - (i > m_shadowMargin / 2 ? 2 : 0));
        painter.setPen(QPen(QColor(0, 0, 0, alpha), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(card.adjusted(-i, -i + 2, i, i + 2),
                                radius + i, radius + i);
    }

    // 圆角卡片本体
    QPainterPath path;
    path.addRoundedRect(card, radius, radius);
    painter.fillPath(path, QColor(StyleKit::resolve(QStringLiteral("@BG_BASE@"))));

    // 1px 描边，让卡片和桌面背景有清晰边界
    painter.setPen(QPen(QColor(ThemeManager::instance()->currentBorderColor()), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

Qt::Edges InstanceAssistantWindow::resizeEdgesAt(const QPoint &pos) const
{
    if (!m_floating || m_collapsed) {
        return Qt::Edges();
    }

    Qt::Edges edges;
    // 只认窗口四周的阴影边距（透明区），卡片内部的点击不会被当成缩放
    const int band = m_shadowMargin;
    if (pos.x() < band) {
        edges |= Qt::LeftEdge;
    }
    if (pos.x() >= width() - band) {
        edges |= Qt::RightEdge;
    }
    if (pos.y() < band) {
        edges |= Qt::TopEdge;
    }
    if (pos.y() >= height() - band) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void InstanceAssistantWindow::mousePressEvent(QMouseEvent *event)
{
    if (m_floating && event->button() == Qt::LeftButton) {
        const Qt::Edges edges = resizeEdgesAt(event->position().toPoint());
        if (edges) {
            m_resizing = true;
            m_resizeEdges = edges;
            m_resizeStartGlobal = event->globalPosition().toPoint();
            m_resizeStartGeometry = geometry();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void InstanceAssistantWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_resizing && (event->buttons() & Qt::LeftButton)) {
        const QPoint delta = event->globalPosition().toPoint() - m_resizeStartGlobal;
        QRect target = m_resizeStartGeometry;

        if (m_resizeEdges & Qt::LeftEdge) {
            target.setLeft(target.left() + delta.x());
        } else if (m_resizeEdges & Qt::RightEdge) {
            target.setRight(target.right() + delta.x());
        }
        if (m_resizeEdges & Qt::TopEdge) {
            target.setTop(target.top() + delta.y());
        } else if (m_resizeEdges & Qt::BottomEdge) {
            target.setBottom(target.bottom() + delta.y());
        }

        // 四周都要留出阴影边距，且不小于内容区最小尺寸
        const int minW = minimumWidth();
        const int minH = minimumHeight();
        if (target.width() < minW) {
            if (m_resizeEdges & Qt::LeftEdge) {
                target.setLeft(target.right() - minW + 1);
            } else {
                target.setRight(target.left() + minW - 1);
            }
        }
        if (target.height() < minH) {
            if (m_resizeEdges & Qt::TopEdge) {
                target.setTop(target.bottom() - minH + 1);
            } else {
                target.setBottom(target.top() + minH - 1);
            }
        }

        setGeometry(target);
        event->accept();
        return;
    }

    if (m_floating) {
        // 悬停在窗口边缘时给出缩放光标（阴影边距内的事件才会落到窗口本身）
        const Qt::Edges edges = resizeEdgesAt(event->position().toPoint());
        if ((edges & Qt::LeftEdge) && (edges & Qt::TopEdge)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if ((edges & Qt::RightEdge) && (edges & Qt::TopEdge)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if ((edges & Qt::LeftEdge) && (edges & Qt::BottomEdge)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if ((edges & Qt::RightEdge) && (edges & Qt::BottomEdge)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
            setCursor(Qt::SizeVerCursor);
        } else {
            unsetCursor();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void InstanceAssistantWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_resizing) {
        m_resizing = false;
        m_resizeEdges = Qt::Edges();
        unsetCursor();
        saveWindowGeometry();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void InstanceAssistantWindow::keyPressEvent(QKeyEvent *event)
{
    // Esc 收起悬浮窗，不退出程序；安卓侧栏仍由悬浮图标控制
    if (m_floating && !m_overlayMode && event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void InstanceAssistantWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
}

void InstanceAssistantWindow::hideEvent(QHideEvent *event)
{
    if (m_floating) {
        // 收起状态下隐藏：先还原成完整窗口再记录几何，下次打开仍是完整形态
        if (m_collapsed) {
            m_navBar->show();
            m_stackedWidget->show();
            if (m_statsHost) {
                m_statsHost->hide();
            }
            setMinimumHeight(kMinContentHeight + 2 * m_shadowMargin);
            const QSize expanded = m_expandedSize.isValid()
                ? m_expandedSize
                : QSize(kDefaultContentWidth + 2 * m_shadowMargin,
                        kDefaultContentHeight + 2 * m_shadowMargin);
            m_collapsed = false;
            resize(expanded.expandedTo(minimumSize()).boundedTo(maximumSize()));
            updateHeaderButtons();
        }
        saveWindowGeometry();
    }
    QWidget::hideEvent(event);
}

void InstanceAssistantWindow::restoreWindowGeometry()
{
    const QByteArray saved = SettingsManager::instance()->getProperty(kGeometryKey).toByteArray();
    bool restored = false;
    if (!saved.isEmpty()) {
        restored = restoreGeometry(saved);
    }
    if (restored) {
        keepOnScreen();
        return;
    }
    placeDefaultGeometry();
}

void InstanceAssistantWindow::placeDefaultGeometry()
{
    QScreen *screen = QGuiApplication::primaryScreen();

    QSize targetSize(kDefaultContentWidth + 2 * m_shadowMargin,
                     kDefaultContentHeight + 2 * m_shadowMargin);

    // 首次打开贴着启动器主窗口右侧摆放（窗口已与主窗口脱开，用构造时记下的位置）
    QRect hostRect = m_launcherRect;
    if (!hostRect.isValid() && parentWidget()) {
        QWidget *host = parentWidget()->window();
        hostRect = QRect(host->mapToGlobal(QPoint(0, 0)), host->size());
    }
    if (QScreen *hostScreen = QGuiApplication::screenAt(hostRect.center())) {
        screen = hostScreen;
    }
    if (hostRect.isValid() && hostRect.width() > 0 && hostRect.height() > 0) {
        if (hostRect.height() > 2 * m_shadowMargin) {
            targetSize.setHeight(qMin(targetSize.height(),
                                      hostRect.height() - 2 * m_shadowMargin));
        }
        QRect target(QPoint(0, 0), targetSize);
        target.moveRight(hostRect.right() - 24);
        target.moveTop(hostRect.top() + qMax(0, (hostRect.height() - targetSize.height()) / 2));
        setGeometry(target);
        keepOnScreen();
        return;
    }

    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    targetSize = targetSize.boundedTo(available.size());
    QRect target(available.topLeft(), targetSize);
    target.moveCenter(available.center());
    setGeometry(target);
}

void InstanceAssistantWindow::keepOnScreen()
{
    const QRect current = frameGeometry();
    QScreen *screen = QGuiApplication::screenAt(current.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect available = screen->availableGeometry();
    QRect target = current;
    target.setSize(target.size().expandedTo(minimumSize()).boundedTo(
        QSize(qMin(available.width(), maximumWidth()),
              qMin(available.height(), QWIDGETSIZE_MAX))));

    if (target.right() > available.right()) {
        target.moveRight(available.right());
    }
    if (target.bottom() > available.bottom()) {
        target.moveBottom(available.bottom());
    }
    if (target.left() < available.left()) {
        target.moveLeft(available.left());
    }
    if (target.top() < available.top()) {
        target.moveTop(available.top());
    }

    if (target != current) {
        setGeometry(target);
    }
}

void InstanceAssistantWindow::saveWindowGeometry()
{
    if (!m_floating) {
        return;
    }
    SettingsManager::instance()->setProperty(kGeometryKey, saveGeometry());
}

void InstanceAssistantWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_overlayMode) {
        applyOverlayGeometry();
    } else if (m_floating) {
#ifdef Q_OS_WIN
        // 独立窗口：显式声明为应用窗口，任务栏里才有自己的按钮
        //（无边框窗口默认是弹出层样式，部分系统环境下不会出现在任务栏）
        if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
            LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
            if (!(exStyle & WS_EX_APPWINDOW)) {
                SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_APPWINDOW);
            }
        }
#endif
        // 换分辨率 / 拔掉显示器后，恢复回来的位置可能已经落在屏幕外
        keepOnScreen();
        if (m_collapsed) {
            m_navBar->hide();
            m_stackedWidget->hide();
        }
    }
    updateHighlightGeometry();
}

void InstanceAssistantWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateHighlightGeometry();
    if (m_collapsed) {
        fitQuickStatsToWidth();
    }
}

// ============================
// 首页实现（复用 InstanceHomePage）
// ============================

void InstanceAssistantWindow::buildHomePage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 复用 InstanceHomePage 组件，提供当前时间/游玩时间/帧率显示与 CPU/内存/GPU 性能监控
    m_homePage = new InstanceHomePage(page);
    // 收起成小条时，用首页每秒刷新的数据在标题栏右侧显示实时占用
    connect(m_homePage, &InstanceHomePage::liveStatsChanged,
            this, &InstanceAssistantWindow::updateLiveStats);
    layout->addWidget(m_homePage, 1);
}

// ============================
// AI 助手页面实现（复用 AiChatPage）
// ============================

void InstanceAssistantWindow::buildAiChatPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 直接复用主程序侧边栏 AI 助手页的完整组件，
    // 与 AiChatPage 共享同一套实现（模型配置、对话持久化、流式接收等）。
    m_aiChatPage = new AiChatPage(page);
    // 实例助手窗口较窄（450px），启用紧凑模式适配样式：
    // - 欢迎页快捷按钮由 3 列改为 2 列
    // - 欢迎页/消息区/输入区内边距收紧
    // - 通过 QSS 动态属性 compactMode 触发字号、按钮宽度等覆盖
    m_aiChatPage->setCompactMode(true);
    layout->addWidget(m_aiChatPage, 1);
}

// ============================
// 联机页面实现（复用 MultiplayerPage）
// ============================

void InstanceAssistantWindow::buildMultiplayerPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 复用独立的 MultiplayerPage 组件，与主程序共享同一套实现
    // （Terracotta 进程管理、HTTP 轮询、房间创建/加入、玩家列表等）
    m_multiplayerPage = new MultiplayerPage(page);
    layout->addWidget(m_multiplayerPage, 1);

    // 发布到联机大厅前未登录 → 打开账户页
    connect(m_multiplayerPage, &MultiplayerPage::forumLoginRequested,
            this, &InstanceAssistantWindow::forumLoginRequested);
}

// ============================
// 资源管理页面实现（复用 InstanceResourcesPage）
// ============================

void InstanceAssistantWindow::buildResourcesPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 复用 InstanceResourcesPage 组件，提供模组/存档/截图/资源包/光影包的统一管理
    // （扫描、启用/禁用、删除、粘贴、搜索、Tab 切换、存档快捷启动等）
    m_resourcesPage = new InstanceResourcesPage(page);
    layout->addWidget(m_resourcesPage, 1);

    // 转发存档快捷启动信号到顶层窗口，由 MainWindow 处理实际启动逻辑
    connect(m_resourcesPage, &InstanceResourcesPage::quickLaunchSaveRequested,
            this, &InstanceAssistantWindow::quickLaunchSaveRequested);

    // 转发模组详情请求信号，附带实例上下文，由 MainWindow 打开替换资源详情页
    connect(m_resourcesPage, &InstanceResourcesPage::modDetailRequested,
            this, [this](const ModInfo &info) {
        emit resourceDetailForReplaceRequested(info, m_instancePath, info);
    });

    // 问问AI：切换到本窗口的 AI 助手页并引用当前资源
    connect(m_resourcesPage, &InstanceResourcesPage::askAiRequested,
            this, [this](const ResourceReference &ref) {
        switchToTab(1);
        if (m_aiChatPage)
            m_aiChatPage->addExternalReference(ref);
    });
}

// ============================
// Java 管理页面实现（管理 + 下载子栈切换）
// ============================

void InstanceAssistantWindow::buildJavaPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 内部用 QStackedWidget 在「管理页」与「下载页」之间切换：
    //  - index 0：InstanceJavaPage（列表 + 工具栏 + 全局设置）
    //  - index 1：InstanceJavaDownloadPage（发行版/版本/镜像源/进度）
    m_javaStack = new QStackedWidget(page);
    m_javaStack->setObjectName(QStringLiteral("assistantJavaStack"));


    m_javaPage = new InstanceJavaPage(m_javaStack);
    m_javaDownloadPage = new InstanceJavaDownloadPage(m_javaStack);

    m_javaStack->addWidget(m_javaPage);            // index 0
    m_javaStack->addWidget(m_javaDownloadPage);    // index 1
    m_javaStack->setCurrentIndex(0);

    // 管理页 → 下载页：用户点击「下载Java」按钮
    connect(m_javaPage, &InstanceJavaPage::downloadJavaRequested,
            this, [this]() {
                if (m_javaStack)
                    m_javaStack->setCurrentIndex(1);
            });

    // 下载页 → 管理页：用户点击返回按钮
    connect(m_javaDownloadPage, &InstanceJavaDownloadPage::backRequested,
            this, [this]() {
                if (m_javaStack)
                    m_javaStack->setCurrentIndex(0);
            });

    // 下载完成：自动切回管理页并刷新列表
    connect(m_javaDownloadPage, &InstanceJavaDownloadPage::javaInstalled,
            this, [this](const QString &javaPath, const QString &versionName) {
                Q_UNUSED(javaPath);
                Q_UNUSED(versionName);
                // InstanceJavaDownloadPage 已将新 Java 写入 SettingsManager，
                // 这里只需切回管理页并触发列表刷新即可
                if (m_javaPage)
                    m_javaPage->refreshJavaList();
                if (m_javaStack)
                    m_javaStack->setCurrentIndex(0);
            });

    layout->addWidget(m_javaStack, 1);
}

// ============================
// 快捷指令页面实现
// ============================

void InstanceAssistantWindow::buildCommandAssistantPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // 1. 上下文状态栏
    auto *contextBar = new QHBoxLayout();
    contextBar->setSpacing(6);

    m_contextLabel = new QLabel(page);
    m_contextLabel->setObjectName(QStringLiteral("contextLabel"));
    m_contextLabel->setText(tr("版本: 未知 | 单机 | OP: 0"));

    m_serverToggleBtn = new QPushButton(tr("切换服务端"), page);
    m_serverToggleBtn->setObjectName(QStringLiteral("serverToggleBtn"));
    m_serverToggleBtn->setCheckable(true);
    m_serverToggleBtn->setCursor(Qt::PointingHandCursor);

    m_opLevelCombo = new QComboBox(page);
    m_opLevelCombo->setObjectName(QStringLiteral("opLevelCombo"));
    for (int lvl = 0; lvl <= 4; ++lvl) {
        m_opLevelCombo->addItem(QString::number(lvl), lvl);
    }
    m_opLevelCombo->setEnabled(false); // 默认单机模式禁用

    m_redetectBtn = new QPushButton(tr("重新检测"), page);
    m_redetectBtn->setObjectName(QStringLiteral("redetectBtn"));
    m_redetectBtn->setCursor(Qt::PointingHandCursor);

    contextBar->addWidget(m_contextLabel, 1);
    contextBar->addWidget(m_serverToggleBtn);
    contextBar->addWidget(m_opLevelCombo);
    contextBar->addWidget(m_redetectBtn);

    layout->addLayout(contextBar);

    // 2. 中文输入框
    m_commandInput = new QLineEdit(page);
    m_commandInput->setObjectName(QStringLiteral("commandInput"));
    m_commandInput->setPlaceholderText(tr("输入中文指令，如：给予 钻石剑 1"));
    m_commandInput->installEventFilter(this);

    layout->addWidget(m_commandInput);

    // 3. 候选列表（占据剩余空间，窗口拉高时同步增高，可显示更多候选）
    m_completionList = new QListWidget(page);
    m_completionList->setObjectName(QStringLiteral("completionList"));
    m_completionList->setMinimumHeight(180);
    m_completionList->setMouseTracking(true);
    m_completionList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    layout->addWidget(m_completionList, 1);

    // 4. 英文输出框 + 复制按钮 + 历史记录按钮
    auto *outputBar = new QHBoxLayout();
    outputBar->setSpacing(6);

    m_englishOutput = new QLineEdit(page);
    m_englishOutput->setObjectName(QStringLiteral("englishOutput"));
    m_englishOutput->setReadOnly(true);
    m_englishOutput->setPlaceholderText(tr("英文指令输出"));

    m_copyBtn = new QPushButton(tr("复制"), page);
    m_copyBtn->setObjectName(QStringLiteral("copyBtn"));
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    m_copyBtn->setEnabled(false);

    m_injectBtn = new QPushButton(tr("一键注入"), page);
    m_injectBtn->setObjectName(QStringLiteral("injectBtn"));
    m_injectBtn->setCursor(Qt::PointingHandCursor);
    m_injectBtn->setEnabled(false);
    m_injectBtn->setToolTip(tr("将指令注入到运行中的 Minecraft 游戏窗口"));

    m_historyBtn = new QPushButton(tr("历史"), page);
    m_historyBtn->setObjectName(QStringLiteral("historyBtn"));
    m_historyBtn->setCursor(Qt::PointingHandCursor);

    m_packBtn = new QPushButton(tr("指令包"), page);
    m_packBtn->setObjectName(QStringLiteral("packBtn"));
    m_packBtn->setCursor(Qt::PointingHandCursor);
    m_packBtn->setToolTip(tr("管理自定义指令包：添加、移除、制作"));

    m_presetBtn = new QPushButton(tr("预设"), page);
    m_presetBtn->setObjectName(QStringLiteral("presetBtn"));
    m_presetBtn->setCursor(Qt::PointingHandCursor);
    m_presetBtn->setToolTip(tr("常用指令句子预设：按分类一键填入"));

    outputBar->addWidget(m_englishOutput, 1);
    outputBar->addWidget(m_copyBtn);
    outputBar->addWidget(m_injectBtn);
    outputBar->addWidget(m_presetBtn);
    outputBar->addWidget(m_historyBtn);
    outputBar->addWidget(m_packBtn);

    layout->addLayout(outputBar);

    // ---- 信号连接 ----
    // 每次击键只重启防抖定时器，停顿 60ms 后统一执行补全 + 翻译，
    // 避免输入过程中同步重建候选列表（clear + new 20 个 item）造成卡顿
    connect(m_commandInput, &QLineEdit::textChanged, this, [this](const QString &) {
        m_inputDebounceTimer->start();
    });

    connect(m_completionList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        QString insertText = item->data(Qt::UserRole).toString();
        if (insertText.isEmpty()) {
            return;
        }
        // 定位当前正在输入的词（光标位置往前到最近空白）
        QString text = m_commandInput->text();
        int cursor = m_commandInput->cursorPosition();
        int wordStart = cursor;
        while (wordStart > 0 && !text.at(wordStart - 1).isSpace()) {
            --wordStart;
        }
        QString currentWord = text.mid(wordStart, cursor - wordStart);

        // 判断是补全当前词还是追加新参数：
        // - 若 insertText 以当前词为前缀（或当前词为空），说明在补全当前词 → 替换当前词
        // - 否则当前词已是完整命令名，候选是下一个参数 → 直接写下去（追加）
        if (currentWord.isEmpty() || insertText.startsWith(currentWord, Qt::CaseInsensitive))
        {
            // 替换当前正在输入的词
            QString newText = text.left(wordStart) + insertText + text.mid(cursor);
            m_commandInput->setText(newText);
            m_commandInput->setCursorPosition(wordStart + insertText.length());
        }
        else
        {
            // 追加新参数：在光标位置插入 " " + insertText + " "
            QString insertion = QStringLiteral(" ") + insertText + QStringLiteral(" ");
            QString newText = text.left(cursor) + insertion + text.mid(cursor);
            m_commandInput->setText(newText);
            m_commandInput->setCursorPosition(cursor + insertion.length());
        }
        m_commandInput->setFocus();
    });

    connect(m_copyBtn, &QPushButton::clicked, this, &InstanceAssistantWindow::onCopyClicked);
    connect(m_injectBtn, &QPushButton::clicked, this, &InstanceAssistantWindow::onInjectClicked);

    connect(m_serverToggleBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_commandContext.isServer = checked;
        m_opLevelCombo->setEnabled(checked);
        m_serverToggleBtn->setText(checked ? tr("切换单机") : tr("切换服务端"));
        updateContextDisplay();
        refreshCompletion();
        refreshTranslation();
    });

    connect(m_opLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        Q_UNUSED(idx);
        m_commandContext.opLevel = m_opLevelCombo->currentData().toInt();
        updateContextDisplay();
        refreshCompletion();
        refreshTranslation();
    });

    connect(m_redetectBtn, &QPushButton::clicked, this, &InstanceAssistantWindow::detectContextFromLevelDat);

    connect(m_historyBtn, &QPushButton::clicked, this, &InstanceAssistantWindow::showHistoryDialog);

    connect(m_packBtn, &QPushButton::clicked, this, &InstanceAssistantWindow::showPackManagerDialog);

    connect(m_presetBtn, &QPushButton::clicked, this, &InstanceAssistantWindow::showPresetsMenu);

    // 初始化上下文显示
    updateContextDisplay();

    // 加载已保存的自定义指令包
    reloadCommandDatabase();
}

void InstanceAssistantWindow::refreshCompletion()
{
    if (!m_completionList || !m_completer) {
        return;
    }

    m_completionList->clear();

    QString input = m_commandInput ? m_commandInput->text() : QString();
    QList<CompletionCandidate> candidates = m_completer->complete(input, m_commandContext);

    const int maxItems = 20;
    int count = qMin(candidates.size(), maxItems);
    for (int i = 0; i < count; ++i) {
        const auto &c = candidates.at(i);
        auto *item = new QListWidgetItem(c.displayText, m_completionList);
        item->setData(Qt::UserRole, c.insertText);
        item->setData(Qt::UserRole + 1, c.unavailableReason);

        // 构造 tooltip
        QStringList tipParts;
        if (!c.detail.isEmpty()) {
            tipParts << c.detail;
        }
        if (!c.available && !c.unavailableReason.isEmpty()) {
            tipParts << tr("不可用: %1").arg(c.unavailableReason);
        }
        if (!tipParts.isEmpty()) {
            item->setToolTip(tipParts.join(QStringLiteral("\n")));
        }

        // 不可用项标灰
        if (!c.available) {
            item->setData(Qt::ForegroundRole, QVariant::fromValue(QBrush(QColor(150, 150, 150))));
        }

        m_completionList->addItem(item);
    }
}

void InstanceAssistantWindow::refreshTranslation()
{
    if (!m_englishOutput || !m_translator || !m_commandInput) {
        return;
    }

    QString input = m_commandInput->text();
    if (input.trimmed().isEmpty()) {
        m_englishOutput->clear();
        m_englishOutput->setToolTip(QString());
        m_englishOutput->setStyleSheet(QString());
        if (m_copyBtn) {
            m_copyBtn->setEnabled(false);
        }
        if (m_injectBtn) {
            m_injectBtn->setEnabled(false);
        }
        return;
    }

    TranslateResult result = m_translator->translate(input, m_commandContext);

    if (result.success) {
        m_englishOutput->setText(result.englishCommand);
        m_englishOutput->setStyleSheet(QString());
        if (m_copyBtn) {
            m_copyBtn->setEnabled(true);
        }
        if (m_injectBtn) {
            m_injectBtn->setEnabled(true);
        }
    } else {
        m_englishOutput->clear();
        if (m_copyBtn) {
            m_copyBtn->setEnabled(false);
        }
        if (m_injectBtn) {
            m_injectBtn->setEnabled(false);
        }
    }

    // 警告/不可用原因通过 tooltip 提示
    QStringList tipParts;
    if (!result.unavailableReason.isEmpty()) {
        tipParts << tr("不可用: %1").arg(result.unavailableReason);
        m_englishOutput->setStyleSheet(QStringLiteral("color: #d32f2f;"));
    } else {
        m_englishOutput->setStyleSheet(QString());
    }
    if (!result.warnings.isEmpty()) {
        tipParts << tr("警告: %1").arg(result.warnings.join(QStringLiteral("; ")));
    }
    m_englishOutput->setToolTip(tipParts.join(QStringLiteral("\n")));
}

void InstanceAssistantWindow::updateContextDisplay()
{
    if (!m_contextLabel) {
        return;
    }

    QString version = m_commandContext.version.isEmpty() ? tr("未知") : m_commandContext.version;
    QString mode = m_commandContext.isServer ? tr("服务端") : tr("单机");
    QString text = tr("版本: %1 | %2 | OP: %3")
                       .arg(version, mode, QString::number(m_commandContext.opLevel));
    m_contextLabel->setText(text);
}

void InstanceAssistantWindow::detectContextFromLevelDat()
{
    if (m_instancePath.isEmpty()) {
        // 无实例路径，默认服务端 OP 4
        m_commandContext.isServer = true;
        m_commandContext.opLevel = 4;
        if (m_serverToggleBtn) {
            QSignalBlocker blocker(m_serverToggleBtn);
            m_serverToggleBtn->setChecked(true);
            m_serverToggleBtn->setText(tr("切换单机"));
        }
        if (m_opLevelCombo) {
            QSignalBlocker blocker(m_opLevelCombo);
            m_opLevelCombo->setEnabled(true);
            m_opLevelCombo->setCurrentIndex(4);
        }
        updateContextDisplay();
        refreshCompletion();
        refreshTranslation();
        return;
    }

    // 扫描 saves/ 目录下的 level.dat
    // 实例路径可能是 versions/{ver} 目录或 .minecraft 根目录，需要向上查找
    QDir savesDir;
    QStringList candidatePaths = {
        QDir(m_instancePath).absoluteFilePath(QStringLiteral("saves")),
        QDir(m_instancePath).absoluteFilePath(QStringLiteral("../saves")),
        QDir(m_instancePath).absoluteFilePath(QStringLiteral("../../saves"))
    };
    for (const QString &candidate : candidatePaths)
    {
        QDir dir(candidate);
        if (dir.exists())
        {
            savesDir = dir;
            break;
        }
    }

    QString latestLevelDat;
    QDateTime latestTime;
    if (savesDir.exists()) {
        const QStringList subDirs = savesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &sub : subDirs) {
            QString candidate = savesDir.absoluteFilePath(sub + QStringLiteral("/level.dat"));
            QFileInfo info(candidate);
            if (info.exists() && info.isFile()) {
                if (!latestTime.isValid() || info.lastModified() > latestTime) {
                    latestTime = info.lastModified();
                    latestLevelDat = candidate;
                }
            }
        }
    }

    if (!latestLevelDat.isEmpty()) {
        auto info = LevelDatReader::readLevelDat(latestLevelDat);
        if (info.has_value()) {
            m_commandContext.isServer = false;
            m_commandContext.opLevel = info->allowCommands ? 4 : 0;
            if (!info->versionName.isEmpty() && m_commandContext.version.isEmpty()) {
                m_commandContext.version = info->versionName;
            }
            if (m_serverToggleBtn) {
                QSignalBlocker blocker(m_serverToggleBtn);
                m_serverToggleBtn->setChecked(false);
                m_serverToggleBtn->setText(tr("切换服务端"));
            }
            if (m_opLevelCombo) {
                QSignalBlocker blocker(m_opLevelCombo);
                m_opLevelCombo->setEnabled(false);
                m_opLevelCombo->setCurrentIndex(m_commandContext.opLevel);
            }
            updateContextDisplay();
            refreshCompletion();
            refreshTranslation();
            return;
        }
    }

    // 解析失败或无 level.dat：默认服务端 OP 4
    m_commandContext.isServer = true;
    m_commandContext.opLevel = 4;
    if (m_serverToggleBtn) {
        QSignalBlocker blocker(m_serverToggleBtn);
        m_serverToggleBtn->setChecked(true);
        m_serverToggleBtn->setText(tr("切换单机"));
    }
    if (m_opLevelCombo) {
        QSignalBlocker blocker(m_opLevelCombo);
        m_opLevelCombo->setEnabled(true);
        m_opLevelCombo->setCurrentIndex(4);
    }
    updateContextDisplay();
    refreshCompletion();
    refreshTranslation();
}

void InstanceAssistantWindow::setInstanceContext(const QString &path, const QString &version, const QString &loader)
{
    m_instancePath = path;
    m_commandContext.version = version;

    // 联机页发布到联机大厅时要带上当前实例的版本 / 加载器 / 模组
    if (m_multiplayerPage)
    {
        m_multiplayerPage->setInstanceContext(path, version, loader);
    }

    // 后台执行耗时检测（进程检测 / JAR 解析 / level.dat 扫描），完成后回主线程应用。
    // 避免在 UI 线程同步启动 PowerShell 或解析游戏 JAR 造成窗口卡顿。
    runContextDetectionInBackground();

    // 同步实例上下文到 AI 助手页面，启用资源引用功能
    if (m_aiChatPage)
    {
        m_aiChatPage->setInstanceContext(m_instancePath, m_commandContext.version, loader);
    }

    // 同步实例路径到资源管理页面，触发模组/资源包/光影包列表扫描
    if (m_resourcesPage)
    {
        m_resourcesPage->setInstancePath(m_instancePath);
    }

    // 同步实例路径到首页，便于后续日志/资源定位
    if (m_homePage)
    {
        m_homePage->setInstancePath(m_instancePath);
    }
}

void InstanceAssistantWindow::runContextDetectionInBackground()
{
    // 捕获当前上下文快照（QString 隐式共享，工作线程只读拷贝安全）
    const QString instancePath = m_instancePath;
    const QString version = m_commandContext.version;

    // 后台线程执行全部耗时检测（PowerShell 查询 + 游戏 JAR/mod JAR 解析 + level.dat 扫描）
    QFuture<BackgroundDetectionResult> future = QtConcurrent::run([instancePath, version]() {
        return runBackgroundDetection(instancePath, version);
    });

    auto *watcher = new QFutureWatcher<BackgroundDetectionResult>(this);
    connect(watcher, &QFutureWatcher<BackgroundDetectionResult>::finished, this,
            [this, watcher]() {
                const BackgroundDetectionResult result = watcher->result();

                // 应用检测结果到成员（主线程）
                m_instancePath = result.scanPath;
                if (!result.detectedVersion.isEmpty())
                {
                    m_commandContext.version = result.detectedVersion;
                }

                // 方块注册表：合并 GameRegistry 提取的方块（lang 数据，含模组）
                if (m_blockRegistry)
                {
                    const QList<BlockInfo> &blocks = result.gameRegistry.blocks();
                    for (const BlockInfo &block : blocks)
                    {
                        m_blockRegistry->addBlock(block);
                    }
                    m_blockRegistry->rebuildIndexes();
                    qDebug() << "GameRegistry: 方块" << m_blockRegistry->allBlocks().size()
                             << "| 物品" << result.gameRegistry.items().size()
                             << "| 实体" << result.gameRegistry.entities().size()
                             << "| 效果" << result.gameRegistry.effects().size()
                             << "| 附魔" << result.gameRegistry.enchantments().size();
                }

                // 物品/实体/效果/附魔：合并到指令数据库（模组数据优先，自动重建索引）
                if (m_commandDb)
                {
                    m_commandDb->mergeDynamicData(result.gameRegistry.items(),
                                                  result.gameRegistry.entities(),
                                                  result.gameRegistry.effects(),
                                                  result.gameRegistry.enchantments());
                }

                // 上下文：有 level.dat 则按存档设置，否则默认服务端 OP 4
                if (result.levelInfo.has_value())
                {
                    m_commandContext.isServer = false;
                    m_commandContext.opLevel = result.levelInfo->allowCommands ? 4 : 0;
                    if (!result.levelInfo->versionName.isEmpty() && m_commandContext.version.isEmpty())
                    {
                        m_commandContext.version = result.levelInfo->versionName;
                    }
                }
                else
                {
                    m_commandContext.isServer = true;
                    m_commandContext.opLevel = 4;
                }

                // 同步 UI 控件状态（QSignalBlocker 避免信号递归触发补全）
                if (m_serverToggleBtn)
                {
                    QSignalBlocker blocker(m_serverToggleBtn);
                    m_serverToggleBtn->setChecked(m_commandContext.isServer);
                    m_serverToggleBtn->setText(m_commandContext.isServer ? tr("切换单机") : tr("切换服务端"));
                }
                if (m_opLevelCombo)
                {
                    QSignalBlocker blocker(m_opLevelCombo);
                    m_opLevelCombo->setEnabled(m_commandContext.isServer);
                    m_opLevelCombo->setCurrentIndex(qBound(0, m_commandContext.opLevel,
                                                           m_opLevelCombo->count() - 1));
                }

                updateContextDisplay();
                refreshCompletion();
                refreshTranslation();

                watcher->deleteLater();
            });
    watcher->setFuture(future);
}

void InstanceAssistantWindow::flushDebouncedInput()
{
    if (!m_commandInput)
    {
        return;
    }
    const QString input = m_commandInput->text();
    // 输入无变化（如 applyPreset 已预置英文输出）跳过，避免覆盖预设结果
    if (input == m_lastCompletionInput)
    {
        return;
    }
    m_lastCompletionInput = input;
    refreshCompletion();
    refreshTranslation();
}

void InstanceAssistantWindow::detectRunningGame()
{
    // 同步检测已废弃：耗时操作统一走后台线程，此处委托以保持接口兼容
    runContextDetectionInBackground();
}

void InstanceAssistantWindow::onCopyClicked()
{
    if (!m_englishOutput || !m_commandInput) {
        return;
    }
    QString text = m_englishOutput->text();
    if (text.isEmpty()) {
        return;
    }
    QApplication::clipboard()->setText(text);

    // 临时样式提示"已复制"
    m_copyBtn->setText(tr("已复制"));
    QPalette pal = m_copyBtn->palette();
    QColor oldBtnColor = pal.color(QPalette::Button);
    pal.setColor(QPalette::Button, QColor(76, 175, 80));
    m_copyBtn->setPalette(pal);
    m_copyBtn->setAutoFillBackground(true);

    QTimer::singleShot(800, this, [this, oldBtnColor]() {
        if (m_copyBtn) {
            m_copyBtn->setText(tr("复制"));
            QPalette p = m_copyBtn->palette();
            p.setColor(QPalette::Button, oldBtnColor);
            m_copyBtn->setPalette(p);
            m_copyBtn->setAutoFillBackground(false);
        }
    });

    addHistory(m_commandInput->text(), text);
}

#ifdef Q_OS_WIN
namespace {
/**
 * @brief 枚举所有顶级窗口，查找属于指定进程 ID 的可见窗口
 * @param pid 目标进程 ID
 * @return 找到的窗口句柄（未找到返回 nullptr）
 */
HWND findWindowByPid(DWORD pid)
{
    struct EnumData
    {
        DWORD targetPid;
        HWND result;
    };
    EnumData data{pid, nullptr};

    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        auto *d = reinterpret_cast<EnumData *>(lParam);
        DWORD windowPid = 0;
        GetWindowThreadProcessId(hWnd, &windowPid);
        if (windowPid != d->targetPid)
        {
            return TRUE;
        }
        if (!IsWindowVisible(hWnd))
        {
            return TRUE;
        }
        // 优先选择有标题的窗口
        wchar_t title[256] = {0};
        GetWindowTextW(hWnd, title, 256);
        if (wcslen(title) > 0)
        {
            d->result = hWnd;
            return FALSE;
        }
        if (d->result == nullptr)
        {
            d->result = hWnd;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return data.result;
}

/**
 * @brief 查找运行中的 javaw.exe 进程 ID
 * @return 第一个 javaw.exe 的 PID（未找到返回 0）
 */
DWORD findJavaProcessId()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, L"javaw.exe") == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return pid;
}

/**
 * @brief 模拟按键按下与释放
 * @param vk 虚拟键码
 */
void sendKeyPress(WORD vk)
{
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[0].ki.wScan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.wScan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

// 在 UI 线程上延时等待但不完全冻结界面。
// 相比裸 Sleep()，会周期性处理事件，使窗口可重绘、响应部分点击，
// 同时保持按键注入步骤的顺序时序。
static void uiDelayMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    // 每 20ms 处理一次事件，避免长时间完全无响应
    QTimer ticker;
    ticker.setInterval(20);
    QObject::connect(&ticker, &QTimer::timeout, []() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    });
    ticker.start();
    loop.exec();
}
} // namespace
#endif

void InstanceAssistantWindow::onInjectClicked()
{
    if (!m_englishOutput || !m_commandInput)
    {
        return;
    }
    QString text = m_englishOutput->text();
    if (text.isEmpty())
    {
        return;
    }

#ifdef Q_OS_WIN
    // 1. 查找运行中的 Minecraft 进程
    DWORD pid = findJavaProcessId();
    if (pid == 0)
    {
        m_injectBtn->setText(tr("未找到游戏"));
        QTimer::singleShot(1500, this, [this]() {
            if (m_injectBtn)
            {
                m_injectBtn->setText(tr("一键注入"));
            }
        });
        return;
    }

    // 2. 查找游戏窗口
    HWND gameWnd = findWindowByPid(pid);
    if (gameWnd == nullptr)
    {
        m_injectBtn->setText(tr("未找到窗口"));
        QTimer::singleShot(1500, this, [this]() {
            if (m_injectBtn)
            {
                m_injectBtn->setText(tr("一键注入"));
            }
        });
        return;
    }

    // 3. 将窗口置前
    if (IsIconic(gameWnd))
    {
        ShowWindow(gameWnd, SW_RESTORE);
    }
    SetForegroundWindow(gameWnd);
    uiDelayMs(150); // 等待窗口切换完成

    // 4. 复制指令到剪贴板（不含前导 '/'，稍后通过按键注入 '/'）
    QString commandBody = text;
    if (commandBody.startsWith(QStringLiteral("/")))
    {
        commandBody = commandBody.mid(1);
    }
    QApplication::clipboard()->setText(commandBody);
    uiDelayMs(50);

    // 5. 发送 '/' 打开聊天框（Minecraft 收到 '/' 会打开聊天框并预填 '/'）
    sendKeyPress(VK_OEM_2); // '/' 键
    uiDelayMs(100);

    // 6. Ctrl+V 粘贴指令内容
    INPUT ctrlDown = {};
    ctrlDown.type = INPUT_KEYBOARD;
    ctrlDown.ki.wVk = VK_CONTROL;
    SendInput(1, &ctrlDown, sizeof(INPUT));

    sendKeyPress('V');

    INPUT ctrlUp = {};
    ctrlUp.type = INPUT_KEYBOARD;
    ctrlUp.ki.wVk = VK_CONTROL;
    ctrlUp.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &ctrlUp, sizeof(INPUT));

    uiDelayMs(100);

    // 7. 发送 Enter 执行指令
    sendKeyPress(VK_RETURN);

    // 8. UI 反馈
    m_injectBtn->setText(tr("已注入"));
    QPalette pal = m_injectBtn->palette();
    QColor oldBtnColor = pal.color(QPalette::Button);
    pal.setColor(QPalette::Button, QColor(76, 175, 80));
    m_injectBtn->setPalette(pal);
    m_injectBtn->setAutoFillBackground(true);

    QTimer::singleShot(1000, this, [this, oldBtnColor]() {
        if (m_injectBtn)
        {
            m_injectBtn->setText(tr("一键注入"));
            QPalette p = m_injectBtn->palette();
            p.setColor(QPalette::Button, oldBtnColor);
            m_injectBtn->setPalette(p);
            m_injectBtn->setAutoFillBackground(false);
        }
    });

    addHistory(m_commandInput->text(), text);
#else
    m_injectBtn->setText(tr("不支持"));
    QTimer::singleShot(1500, this, [this]() {
        if (m_injectBtn)
        {
            m_injectBtn->setText(tr("一键注入"));
        }
    });
#endif
}

void InstanceAssistantWindow::showHistoryDialog()
{
    if (!m_commandInput) {
        return;
    }

    // 弹出模态对话框显示历史记录
    QDialog dialog(this);
    dialog.setWindowTitle(tr("历史记录"));
    dialog.setMinimumSize(380, 320);

    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->setContentsMargins(8, 8, 8, 8);
    dialogLayout->setSpacing(6);

    auto *listWidget = new QListWidget(&dialog);
    listWidget->setObjectName(QStringLiteral("historyDialogList"));
    dialogLayout->addWidget(listWidget, 1);

    QList<QPair<QString, QString>> history = SettingsManager::instance()->commandHistory();
    for (const auto &pair : history) {
        QString display = QStringLiteral("%1 → %2").arg(pair.first, pair.second);
        auto *item = new QListWidgetItem(display, listWidget);
        item->setData(Qt::UserRole, pair.first); // 中文输入
        item->setToolTip(pair.second);
    }

    // 双击或点击历史项回填输入框并关闭对话框
    connect(listWidget, &QListWidget::itemClicked, &dialog, [this, &dialog, listWidget](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        QString fullText = item->data(Qt::UserRole).toString();
        if (fullText.isEmpty()) {
            return;
        }
        m_commandInput->setText(fullText);
        m_commandInput->setFocus();
        dialog.accept();
    });

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    auto *closeBtn = new QPushButton(tr("关闭"), &dialog);
    btnRow->addWidget(closeBtn);
    dialogLayout->addLayout(btnRow);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
}

void InstanceAssistantWindow::showPresetsMenu()
{
    if (!m_commandDb || !m_presetBtn) {
        return;
    }

    const QStringList categories = m_commandDb->presetCategories();
    if (categories.isEmpty()) {
        return;
    }

    QMenu menu(this);
    menu.setObjectName(QStringLiteral("presetMenu"));

    for (const QString &category : categories) {
        const QList<CommandPreset> presets = m_commandDb->findPresetsByCategory(category);
        if (presets.isEmpty()) {
            continue;
        }
        QMenu *subMenu = menu.addMenu(category);
        for (const CommandPreset &preset : presets) {
            QAction *action = subMenu->addAction(preset.chinese);
            action->setToolTip(preset.english);
            action->setStatusTip(preset.english);
            connect(action, &QAction::triggered, this, [this, preset]() {
                applyPreset(preset);
            });
        }
    }

    menu.exec(m_presetBtn->mapToGlobal(QPoint(0, m_presetBtn->height())));
}

void InstanceAssistantWindow::applyPreset(const CommandPreset &preset)
{
    if (!m_commandInput || !m_englishOutput) {
        return;
    }

    // 预置防抖缓存：使 setText 触发的防抖刷新检测到输入未变而跳过，
    // 避免 60ms 后翻译结果覆盖下方写入的预设英文指令
    m_lastCompletionInput = preset.chinese;

    // 中文句子填入输入框（触发实时翻译，便于用户理解与微调）
    m_commandInput->setText(preset.chinese);

    // 英文指令写入输出框（预设为验证过的完整指令，避免翻译偏差）。
    // 防御性规范化：连续空白压缩为单个空格，确保指令各 token 之间以
    // 单一空格分隔，避免数据异常时出现 "give@s" 之类粘连导致游戏内无效。
    QString english = preset.english.trimmed();
    QString normalized;
    normalized.reserve(english.size());
    bool prevSpace = false;
    for (const QChar &ch : english)
    {
        if (ch.isSpace())
        {
            if (!prevSpace)
            {
                normalized.append(QLatin1Char(' '));
                prevSpace = true;
            }
        }
        else
        {
            normalized.append(ch);
            prevSpace = false;
        }
    }
    m_englishOutput->setText(normalized);
    m_englishOutput->setStyleSheet(QString());
    m_englishOutput->setToolTip(preset.description.isEmpty()
                                    ? normalized
                                    : QStringLiteral("%1\n%2").arg(preset.description, normalized));

    if (m_copyBtn) {
        m_copyBtn->setEnabled(true);
    }
    if (m_injectBtn) {
        m_injectBtn->setEnabled(true);
    }

    m_commandInput->setFocus();
}

void InstanceAssistantWindow::addHistory(const QString &input, const QString &output)
{
    if (input.trimmed().isEmpty() || output.trimmed().isEmpty()) {
        return;
    }

    QList<QPair<QString, QString>> history = SettingsManager::instance()->commandHistory();

    // 去重：若已存在相同 (input, output)，先移除
    for (int i = history.size() - 1; i >= 0; --i) {
        if (history.at(i).first == input && history.at(i).second == output) {
            history.removeAt(i);
        }
    }

    history.prepend(qMakePair(input, output));

    // 截断超过 20 条
    const int maxHistory = 20;
    while (history.size() > maxHistory) {
        history.removeLast();
    }

    SettingsManager::instance()->saveCommandHistory(history);
}

void InstanceAssistantWindow::showPackManagerDialog()
{
    CommandPackManagerDialog dialog(this);
    dialog.reloadPackList();
    connect(&dialog, &CommandPackManagerDialog::packsChanged, this, [this]() {
        reloadCommandDatabase();
        // 重建后立即刷新当前候选与翻译
        refreshCompletion();
        refreshTranslation();
    });
    dialog.exec();
}

void InstanceAssistantWindow::reloadCommandDatabase()
{
    if (!m_commandDb) {
        return;
    }

    // 清空当前数据并重新加载内置数据库
    m_commandDb->clearAll();
    m_commandDb->load();

    // 依次合并已启用的指令包
    QList<QPair<QString, bool>> packs = SettingsManager::instance()->commandPackList();
    for (const auto &entry : packs)
    {
        if (!entry.second) {
            continue; // 已禁用，跳过
        }
        CommandPack pack;
        if (!pack.loadFromFile(entry.first)) {
            qWarning() << "InstanceAssistantWindow: 加载包失败" << entry.first;
            continue;
        }
        m_commandDb->mergePack(pack);
        qDebug() << "InstanceAssistantWindow: 已合并包" << pack.name()
                 << "来自" << entry.first
                 << "(指令数:" << pack.info().commandCount << ")";
    }
}

bool InstanceAssistantWindow::eventFilter(QObject *obj, QEvent *event)
{
    // 悬浮窗标题栏：空白处按住拖动整个窗口，双击收起 / 展开
    if (m_floating && obj == m_titleBar) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragOrigin = mouseEvent->globalPosition().toPoint()
                    - frameGeometry().topLeft();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (m_dragging && (mouseEvent->buttons() & Qt::LeftButton)) {
                move(mouseEvent->globalPosition().toPoint() - m_dragOrigin);
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            if (m_dragging) {
                m_dragging = false;
                keepOnScreen();
                saveWindowGeometry();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonDblClick: {
            setCollapsed(!m_collapsed);
            return true;
        }
        default:
            break;
        }
    }

    // Tab 键补全：在 m_commandInput 中按 Tab 时，用候选列表第一项补全当前词
    if (obj == m_commandInput && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Tab) {
            if (m_completionList && m_completionList->count() > 0) {
                QListWidgetItem *firstItem = m_completionList->item(0);
                if (firstItem) {
                    QString insertText = firstItem->data(Qt::UserRole).toString();
                    if (!insertText.isEmpty()) {
                        QString text = m_commandInput->text();
                        int cursor = m_commandInput->cursorPosition();
                        int wordStart = cursor;
                        while (wordStart > 0 && !text.at(wordStart - 1).isSpace()) {
                            --wordStart;
                        }
                        QString newText = text.left(wordStart) + insertText + text.mid(cursor);
                        m_commandInput->setText(newText);
                        int newCursor = wordStart + insertText.length();
                        m_commandInput->setCursorPosition(newCursor);
                        return true; // 阻止默认 Tab 行为
                    }
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
