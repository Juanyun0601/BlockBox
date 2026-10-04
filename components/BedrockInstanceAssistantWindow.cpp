#include "BedrockInstanceAssistantWindow.h"

#include <QDesktopServices>
#include <QDir>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QScrollBar>
#include <QShowEvent>
#include <QUrl>

#include "pages/AiChatPage.h"
#include "pages/BedrockCommandAssistantPage.h"
#include "pages/BedrockInstanceHomePage.h"
#include "pages/BedrockMultiplayerPage.h"
#include "pages/BedrockResourcesPage.h"
#include "pages/settings/BedrockInstanceSettingsPage.h"
#include "utils/BedrockLauncher.h"
#include "utils/ThemeManager.h"
#include "utils/bedrock/BedrockInstanceManager.h"

BedrockInstanceAssistantWindow::BedrockInstanceAssistantWindow(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_navBar(nullptr)
    , m_scrollArea(nullptr)
    , m_navContainer(nullptr)
    , m_navLayout(nullptr)
    , m_selectionHighlight(nullptr)
    , m_stackedWidget(nullptr)
    , m_highlightAnim(nullptr)
{
    setWindowTitle(tr("基岩版实例助手"));
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint);
    setFixedWidth(450);
    setMinimumHeight(500);

    initUI();
    applyThemeStyles();

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &BedrockInstanceAssistantWindow::applyThemeStyles);
}

BedrockInstanceAssistantWindow::~BedrockInstanceAssistantWindow() = default;

void BedrockInstanceAssistantWindow::setInstanceId(const QString &instanceId)
{
    m_instanceId = instanceId;

    // 同步实例 ID 到各子页面
    if (m_homePage)
    {
        m_homePage->setInstanceId(m_instanceId);
    }

    if (m_aiChatPage)
    {
        // 基岩版实例路径：dataDir + "/com.mojang"
        BedrockInstance inst = BedrockInstanceManager::instance()->instanceById(m_instanceId);
        if (!inst.dataDir.isEmpty())
        {
            m_aiChatPage->setInstanceContext(inst.dataDir + QStringLiteral("/com.mojang"),
                                             inst.version, QStringLiteral("Bedrock"));
        }
    }

    if (m_commandPage)
    {
        BedrockInstance inst = BedrockInstanceManager::instance()->instanceById(m_instanceId);
        if (!inst.dataDir.isEmpty())
        {
            m_commandPage->setInstancePath(inst.dataDir + QStringLiteral("/com.mojang"));
        }
    }

    if (m_resourcesPage)
    {
        BedrockInstance inst = BedrockInstanceManager::instance()->instanceById(m_instanceId);
        if (!inst.dataDir.isEmpty())
        {
            m_resourcesPage->setInstancePath(inst.dataDir + QStringLiteral("/com.mojang"));
        }
    }

    if (m_settingsPage)
    {
        m_settingsPage->setInstanceId(m_instanceId);
    }
}

void BedrockInstanceAssistantWindow::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // ---- 顶部导航栏 ----
    m_navBar = new QWidget(this);
    m_navBar->setObjectName(QStringLiteral("assistantNavBar"));

    auto *navBarLayout = new QVBoxLayout(m_navBar);
    navBarLayout->setContentsMargins(0, 0, 0, 0);
    navBarLayout->setSpacing(0);

    m_scrollArea = new QScrollArea(m_navBar);
    m_scrollArea->setObjectName(QStringLiteral("assistantNavScroll"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(
        "#assistantNavScroll {"
        "  background: transparent;"
        "  border: none;"
        "}"
    );

    m_navContainer = new QWidget();
    m_navContainer->setObjectName(QStringLiteral("assistantNavContainer"));
    m_navContainer->setStyleSheet(
        "#assistantNavContainer {"
        "  background: transparent;"
        "}"
    );

    m_navLayout = new QHBoxLayout(m_navContainer);
    m_navLayout->setContentsMargins(4, 0, 4, 0);
    m_navLayout->setSpacing(2);

    // 对齐 Java 版 6 个标签页，将 Java 管理替换为实例设置（基岩版不需要 Java 管理）
    QStringList navItems = {tr("首页"), tr("AI助手"), tr("快捷指令"), tr("联机"), tr("资源管理"), tr("设置")};
    for (int i = 0; i < navItems.size(); ++i) {
        QPushButton *btn = createNavButton(navItems[i], i);
        m_navButtons.append(btn);
        m_navLayout->addWidget(btn);
    }

    m_scrollArea->setWidget(m_navContainer);
    navBarLayout->addWidget(m_scrollArea);

    m_selectionHighlight = new QWidget(m_navContainer);
    m_selectionHighlight->setObjectName(QStringLiteral("assistantHighlight"));
    m_selectionHighlight->setFixedHeight(3);
    m_selectionHighlight->raise();

    m_mainLayout->addWidget(m_navBar);

    // ---- Tab 页面 ----
    m_stackedWidget = new QStackedWidget();
    m_stackedWidget->setObjectName(QStringLiteral("assistantStackedWidget"));
    m_stackedWidget->setStyleSheet(
        "#assistantStackedWidget {"
        "  background: palette(window);"
        "}"
    );

    for (int i = 0; i < navItems.size(); ++i) {
        QWidget *page = new QWidget();
        page->setObjectName(QString("assistantPage%1").arg(i));
        m_stackedWidget->addWidget(page);

        if (i == 0)
            buildHomePage(page);
        else if (i == 1)
            buildAiChatPage(page);
        else if (i == 2)
            buildCommandAssistantPage(page);
        else if (i == 3)
            buildMultiplayerPage(page);
        else if (i == 4)
            buildResourcesPage(page);
        else if (i == 5)
            buildSettingsPage(page);
    }

    m_mainLayout->addWidget(m_stackedWidget, 1);

    m_highlightAnim = new QPropertyAnimation(this, "highlightPos", this);
    m_highlightAnim->setDuration(250);
    m_highlightAnim->setEasingCurve(QEasingCurve::OutCubic);

    if (!m_navButtons.isEmpty())
        m_navButtons[0]->setChecked(true);
    m_stackedWidget->setCurrentIndex(0);
}

void BedrockInstanceAssistantWindow::applyThemeStyles()
{
    const QString themeColor = ThemeManager::instance()->currentThemeColor();
    const QString textColor = ThemeManager::instance()->currentTextColor();
    const QString borderColor = ThemeManager::instance()->currentBorderColor();
    QColor tc(themeColor);
    const QString hoverBg = QString("rgba(%1, %2, %3, 0.12)")
        .arg(tc.red()).arg(tc.green()).arg(tc.blue());

    if (m_navBar) {
        m_navBar->setStyleSheet(
            QString("#assistantNavBar {"
                    "  background: palette(window);"
                    "  border-bottom: 1px solid %1;"
                    "}").arg(borderColor));
    }
    if (m_selectionHighlight) {
        m_selectionHighlight->setStyleSheet(
            QString("#assistantHighlight {"
                    "  background: %1;"
                    "  border-radius: 2px;"
                    "}").arg(themeColor));
    }

    const QString btnStyle = QString(
        "QPushButton {"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 4px 12px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "  color: %1;"
        "  background: transparent;"
        "}"
        "QPushButton:hover {"
        "  background: %2;"
        "}"
        "QPushButton:checked {"
        "  color: %3;"
        "}"
    ).arg(textColor, hoverBg, themeColor);

    for (auto *btn : m_navButtons)
        btn->setStyleSheet(btnStyle);
}

QPushButton *BedrockInstanceAssistantWindow::createNavButton(const QString &text, int index)
{
    QPushButton *btn = new QPushButton(text);
    btn->setObjectName(QString("assistantNavBtn%1").arg(index));
    btn->setCheckable(true);
    btn->setFixedHeight(34);
    btn->setCursor(Qt::PointingHandCursor);

    connect(btn, &QPushButton::clicked, this, [this, index]() {
        if (index != m_currentIndex)
            switchToTab(index);
    });

    return btn;
}

void BedrockInstanceAssistantWindow::switchToTab(int index)
{
    m_prevIndex = m_currentIndex;
    m_currentIndex = index;

    for (int i = 0; i < m_navButtons.size(); ++i)
        m_navButtons[i]->setChecked(i == index);

    ensureTabVisible(index);

    if (m_highlightAnim->state() == QPropertyAnimation::Running)
        m_highlightAnim->stop();
    m_highlightAnim->setStartValue(m_highlightPos);
    m_highlightAnim->setEndValue(m_navButtons[index]->x() + m_navButtons[index]->width() / 2
                                 - m_selectionHighlight->width() / 2);
    m_highlightAnim->start();

    m_stackedWidget->setCurrentIndex(index);
}

void BedrockInstanceAssistantWindow::updateHighlightGeometry()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_navButtons.size())
        return;

    QPushButton *btn = m_navButtons[m_currentIndex];
    int barWidth = btn->width() * 0.5;
    if (barWidth < 20)
        barWidth = 20;

    m_selectionHighlight->setFixedWidth(barWidth);
    const int x = btn->x() + btn->width() / 2 - barWidth / 2;
    m_selectionHighlight->move(x, m_navContainer->height() - 3);
    m_highlightPos = x;
}

void BedrockInstanceAssistantWindow::setHighlightPos(int pos)
{
    m_highlightPos = pos;
    m_selectionHighlight->move(pos, m_navContainer->height() - 3);
}

void BedrockInstanceAssistantWindow::ensureTabVisible(int index)
{
    if (index < 0 || index >= m_navButtons.size())
        return;

    QPushButton *btn = m_navButtons[index];
    const int scrollViewWidth = m_scrollArea->viewport()->width();
    const int scrollPos = m_scrollArea->horizontalScrollBar()->value();
    const int btnLeft = btn->x();
    const int btnRight = btn->x() + btn->width();

    if (btnLeft < scrollPos)
        m_scrollArea->horizontalScrollBar()->setValue(btnLeft - 8);
    else if (btnRight > scrollPos + scrollViewWidth)
        m_scrollArea->horizontalScrollBar()->setValue(btnRight - scrollViewWidth + 8);
}

void BedrockInstanceAssistantWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateHighlightGeometry();
}

void BedrockInstanceAssistantWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateHighlightGeometry();
}

// ============================================================================
// Tab 页面构建
// ============================================================================

void BedrockInstanceAssistantWindow::buildHomePage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_homePage = new BedrockInstanceHomePage(page);
    layout->addWidget(m_homePage, 1);

    // 连接首页信号
    connect(m_homePage, &BedrockInstanceHomePage::launchGameRequested,
            this, []() {
        BedrockLauncher::instance()->launchGame();
    });

    connect(m_homePage, &BedrockInstanceHomePage::openDataDirRequested,
            this, [this]() {
        BedrockInstance inst = BedrockInstanceManager::instance()->instanceById(m_instanceId);
        if (!inst.dataDir.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(inst.dataDir));
    });

    connect(m_homePage, &BedrockInstanceHomePage::openMojangDirRequested,
            this, [this]() {
        BedrockInstance inst = BedrockInstanceManager::instance()->instanceById(m_instanceId);
        QString mojangDir = inst.dataDir + QStringLiteral("/com.mojang");
        if (QDir(mojangDir).exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(mojangDir));
    });
}

void BedrockInstanceAssistantWindow::buildAiChatPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_aiChatPage = new AiChatPage(page);
    m_aiChatPage->setCompactMode(true);
    layout->addWidget(m_aiChatPage, 1);
}

void BedrockInstanceAssistantWindow::buildCommandAssistantPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_commandPage = new BedrockCommandAssistantPage(page);
    layout->addWidget(m_commandPage, 1);
}

void BedrockInstanceAssistantWindow::buildMultiplayerPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_multiplayerPage = new BedrockMultiplayerPage(page);
    layout->addWidget(m_multiplayerPage, 1);
}

void BedrockInstanceAssistantWindow::buildResourcesPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_resourcesPage = new BedrockResourcesPage(page);
    layout->addWidget(m_resourcesPage, 1);
}

void BedrockInstanceAssistantWindow::buildSettingsPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_settingsPage = new BedrockInstanceSettingsPage(page);
    layout->addWidget(m_settingsPage, 1);

    // 将实例激活为当前实例，激活成功后刷新设置页
    connect(m_settingsPage, &BedrockInstanceSettingsPage::activateInstanceRequested,
            this, [this](const QString &instanceId) {
        QString err;
        if (BedrockInstanceManager::instance()->activateInstance(instanceId, &err)) {
            m_settingsPage->setInstanceId(instanceId);
        } else {
            qWarning() << "[BedrockInstanceAssistantWindow] 激活实例失败:" << err;
        }
    });
}
