/**
 * @file   CommunityLobbyPage.cpp
 * @brief  社区联机大厅页实现
 * @author BlockBox Team
 * @date   2026-08-30
 */

#include "CommunityLobbyPage.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpacerItem>
#include <QTimer>
#include <QVBoxLayout>

#include "layouts/FlowLayout.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/StyleKit.h"

// ============================
// 构造 / 析构
// ============================

CommunityLobbyPage::CommunityLobbyPage(QWidget *parent)
    : QWidget(parent)
    , m_searchEdit(nullptr)
    , m_versionFilter(nullptr)
    , m_statusFilter(nullptr)
    , m_refreshBtn(nullptr)
    , m_createRoomBtn(nullptr)
    , m_roomCountLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardFlowLayout(nullptr)
    , m_emptyWidget(nullptr)
    , m_emptyIcon(nullptr)
    , m_emptyText(nullptr)
{
    initUI();
    applyThemeStyles();

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &CommunityLobbyPage::applyThemeStyles);

    // 连接大厅客户端信号
    auto *lobby = HongshiLobbyClient::instance();
    connect(lobby, &HongshiLobbyClient::roomsRefreshed,
            this, &CommunityLobbyPage::onRoomsRefreshed);
    connect(lobby, &HongshiLobbyClient::refreshError,
            this, &CommunityLobbyPage::onRefreshError);
    connect(lobby, &HongshiLobbyClient::loadingChanged,
            this, &CommunityLobbyPage::onLoadingChanged);

    // 自动刷新：每 30 秒拉取一次房间列表
    m_autoRefreshTimer.setInterval(30000);
    connect(&m_autoRefreshTimer, &QTimer::timeout, this, [this]() {
        HongshiLobbyClient::instance()->refreshRooms();
    });
    m_autoRefreshTimer.start();

    // 启动时设置自动轮询间隔
    lobby->setAutoRefreshInterval(30000);

    // 首次加载
    lobby->refreshRooms();
}

CommunityLobbyPage::~CommunityLobbyPage() = default;

// ============================
// UI 构建
// ============================

void CommunityLobbyPage::initUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ---- Tab标签页 ----
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("communityTabWidget"));

    // ---- Tab1: 联机大厅 ----
    auto *lobbyTab = new QWidget();
    auto *lobbyLayout = new QVBoxLayout(lobbyTab);
    lobbyLayout->setContentsMargins(16, 12, 16, 12);
    lobbyLayout->setSpacing(10);

    // 顶部：标题行
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);

    auto *titleLabel = new QLabel(tr("联机大厅"), lobbyTab);
    titleLabel->setObjectName(QStringLiteral("lobbyTitleLabel"));
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(14);
    titleLabel->setFont(titleFont);
    titleRow->addWidget(titleLabel);

    m_roomCountLabel = new QLabel(lobbyTab);
    m_roomCountLabel->setObjectName(QStringLiteral("lobbyRoomCountLabel"));
    titleRow->addWidget(m_roomCountLabel);

    titleRow->addStretch(1);

    m_refreshBtn = new QPushButton(tr("刷新"), lobbyTab);
    m_refreshBtn->setObjectName(QStringLiteral("lobbyRefreshBtn"));
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setFixedHeight(30);
    m_refreshBtn->setToolTip(tr("刷新房间列表"));
    m_refreshBtn->setVisible(false);
    titleRow->addWidget(m_refreshBtn);

    m_createRoomBtn = new QPushButton(tr("创建房间"), lobbyTab);
    m_createRoomBtn->setObjectName(QStringLiteral("lobbyCreateRoomBtn"));
    m_createRoomBtn->setCursor(Qt::PointingHandCursor);
    m_createRoomBtn->setFixedHeight(30);
    titleRow->addWidget(m_createRoomBtn);

    lobbyLayout->addLayout(titleRow);

    // 搜索与筛选栏
    auto *filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);

    m_searchEdit = new QLineEdit(lobbyTab);
    m_searchEdit->setObjectName(QStringLiteral("lobbySearchEdit"));
    m_searchEdit->setPlaceholderText(tr("搜索房间名、房主..."));
    m_searchEdit->setFixedHeight(32);
    m_searchEdit->setClearButtonEnabled(true);
    filterRow->addWidget(m_searchEdit, 2);

    m_versionFilter = new QComboBox(lobbyTab);
    m_versionFilter->setObjectName(QStringLiteral("lobbyFilterCombo"));
    m_versionFilter->setCursor(Qt::PointingHandCursor);
    m_versionFilter->setFixedHeight(32);
    m_versionFilter->addItem(tr("全部版本"));
    m_versionFilter->addItem(tr("1.21.x"));
    m_versionFilter->addItem(tr("1.20.x"));
    m_versionFilter->addItem(tr("1.19.x"));
    m_versionFilter->addItem(tr("1.18.x"));
    m_versionFilter->addItem(tr("其他"));
    filterRow->addWidget(m_versionFilter);

    m_statusFilter = new QComboBox(lobbyTab);
    m_statusFilter->setObjectName(QStringLiteral("lobbyFilterCombo"));
    m_statusFilter->setCursor(Qt::PointingHandCursor);
    m_statusFilter->setFixedHeight(32);
    m_statusFilter->addItem(tr("全部状态"));
    m_statusFilter->addItem(tr("等待中"));
    m_statusFilter->addItem(tr("游戏中"));
    m_statusFilter->addItem(tr("已满"));
    filterRow->addWidget(m_statusFilter);

    lobbyLayout->addLayout(filterRow);

    // 房间卡片滚动区域
    m_scrollArea = new QScrollArea(lobbyTab);
    m_scrollArea->setObjectName(QStringLiteral("lobbyScrollArea"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_cardContainer = new QWidget();
    m_cardContainer->setObjectName(QStringLiteral("lobbyCardContainer"));
    m_cardFlowLayout = new FlowLayout(m_cardContainer, 10, 10, 10);

    m_scrollArea->setWidget(m_cardContainer);
    lobbyLayout->addWidget(m_scrollArea, 1);

    // 空状态提示
    m_emptyWidget = new QWidget(lobbyTab);
    m_emptyWidget->setObjectName(QStringLiteral("lobbyEmptyWidget"));
    buildEmptyState(m_emptyWidget);
    lobbyLayout->addWidget(m_emptyWidget);
    m_emptyWidget->setVisible(false);

    m_tabWidget->addTab(lobbyTab, tr("联机大厅"));

    // ---- Tab2: 论坛（即将实现） ----
    QWidget *forumTab = createComingSoonTab(tr("论坛"));
    m_tabWidget->addTab(forumTab, tr("论坛"));

    // ---- Tab3: 服务器（即将实现） ----
    QWidget *serverTab = createComingSoonTab(tr("服务器"));
    m_tabWidget->addTab(serverTab, tr("服务器"));

    mainLayout->addWidget(m_tabWidget);

    // ---- 信号连接 ----
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &CommunityLobbyPage::onSearchTextChanged);
    connect(m_versionFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CommunityLobbyPage::onVersionFilterChanged);
    connect(m_statusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CommunityLobbyPage::onStatusFilterChanged);
    connect(m_refreshBtn, &QPushButton::clicked,
            this, &CommunityLobbyPage::onRefreshClicked);
    connect(m_createRoomBtn, &QPushButton::clicked,
            this, &CommunityLobbyPage::onCreateRoomClicked);
}

QWidget *CommunityLobbyPage::createComingSoonTab(const QString &title)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setAlignment(Qt::AlignCenter);

    auto *iconLabel = new QLabel(tab);
    iconLabel->setObjectName(QStringLiteral("comingSoonIcon"));
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/nav_multiplayer.svg"),
        ThemeManager::instance()->currentThemeColor(), 64).pixmap(64, 64));
    iconLabel->setFixedSize(80, 80);
    layout->addWidget(iconLabel, 0, Qt::AlignCenter);

    layout->addSpacing(16);

    auto *titleLabel = new QLabel(tr("%1 即将实现").arg(title), tab);
    titleLabel->setObjectName(QStringLiteral("comingSoonTitle"));
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(16);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel, 0, Qt::AlignCenter);

    layout->addSpacing(8);

    auto *descLabel = new QLabel(tr("此功能正在开发中，敬请期待"), tab);
    descLabel->setObjectName(QStringLiteral("comingSoonDesc"));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel, 0, Qt::AlignCenter);

    return tab;
}

void CommunityLobbyPage::buildEmptyState(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 40, 0, 40);
    layout->setAlignment(Qt::AlignCenter);

    m_emptyIcon = new QLabel(parent);
    m_emptyIcon->setObjectName(QStringLiteral("lobbyEmptyIcon"));
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    m_emptyIcon->setPixmap(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/nav_multiplayer.svg"),
        ThemeManager::instance()->currentThemeColor(), 48).pixmap(48, 48));
    m_emptyIcon->setFixedSize(64, 64);
    layout->addWidget(m_emptyIcon, 0, Qt::AlignCenter);

    layout->addSpacing(12);

    m_emptyText = new QLabel(tr("暂无开放的联机房间"), parent);
    m_emptyText->setObjectName(QStringLiteral("lobbyEmptyText"));
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    layout->addWidget(m_emptyText, 0, Qt::AlignCenter);

    layout->addSpacing(8);

    auto *hint = new QLabel(tr("你可以点击「创建房间」来开设自己的联机房间"), parent);
    hint->setObjectName(QStringLiteral("lobbyEmptyHint"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    layout->addWidget(hint, 0, Qt::AlignCenter);
}

// ============================
// 房间卡片
// ============================

QWidget *CommunityLobbyPage::createRoomCard(const HongshiRoomInfo &room)
{
    auto *card = new QWidget(m_cardContainer);
    card->setObjectName(QStringLiteral("lobbyRoomCard"));
    card->setMinimumWidth(260);
    card->setMaximumWidth(320);
    card->setFixedHeight(160);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("roomId", room.roomId);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 10);
    layout->setSpacing(6);

    // ---- 第一行：房间名 + 状态标签 ----
    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(6);

    auto *nameLabel = new QLabel(room.roomName, card);
    nameLabel->setObjectName(QStringLiteral("lobbyCardRoomName"));
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(12);
    nameLabel->setFont(nameFont);
    nameLabel->setMinimumWidth(120);
    topRow->addWidget(nameLabel, 1);

    auto *statusLabel = new QLabel(statusDisplayText(room.status), card);
    statusLabel->setObjectName(QStringLiteral("lobbyCardStatus"));
    statusLabel->setProperty("status", statusProperty(room.status));
    statusLabel->setAlignment(Qt::AlignCenter);
    topRow->addWidget(statusLabel);

    layout->addLayout(topRow);

    // ---- 第二行：房主信息 ----
    auto *hostRow = new QHBoxLayout();
    hostRow->setSpacing(4);

    auto *hostIcon = new QLabel(card);
    hostIcon->setPixmap(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/nav_ai.svg"),
        ThemeManager::instance()->currentThemeColor(), 12).pixmap(12, 12));
    hostIcon->setFixedSize(14, 14);
    hostRow->addWidget(hostIcon);

    auto *hostLabel = new QLabel(room.hostName, card);
    hostLabel->setObjectName(QStringLiteral("lobbyCardHostLabel"));
    hostRow->addWidget(hostLabel);

    hostRow->addStretch(1);
    layout->addLayout(hostRow);

    // ---- 第三行：版本 + 加载器 + 区域 ----
    auto *infoRow = new QHBoxLayout();
    infoRow->setSpacing(6);

    if (!room.gameVersion.isEmpty()) {
        auto *versionTag = new QLabel(room.gameVersion, card);
        versionTag->setObjectName(QStringLiteral("lobbyCardTag"));
        infoRow->addWidget(versionTag);
    }

    if (!room.loaderType.isEmpty()) {
        auto *loaderTag = new QLabel(room.loaderType, card);
        loaderTag->setObjectName(QStringLiteral("lobbyCardTag"));
        infoRow->addWidget(loaderTag);
    }

    if (!room.nodeRegion.isEmpty()) {
        auto *regionTag = new QLabel(room.nodeRegion, card);
        regionTag->setObjectName(QStringLiteral("lobbyCardTag"));
        infoRow->addWidget(regionTag);
    }

    infoRow->addStretch(1);
    layout->addLayout(infoRow);

    // ---- 弹性空间 ----
    layout->addStretch(1);

    // ---- 底部：人数 + 延迟 + 加入按钮 ----
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(8);

    auto *playerLabel = new QLabel(formatPlayerCount(room.currentPlayers, room.maxPlayers), card);
    playerLabel->setObjectName(QStringLiteral("lobbyCardPlayerLabel"));
    bottomRow->addWidget(playerLabel);

    if (room.latencyMs >= 0) {
        auto *latencyLabel = new QLabel(QStringLiteral("%1ms").arg(room.latencyMs), card);
        latencyLabel->setObjectName(QStringLiteral("lobbyCardLatencyLabel"));
        bottomRow->addWidget(latencyLabel);
    }

    bottomRow->addStretch(1);

    auto *joinBtn = new QPushButton(tr("加入"), card);
    joinBtn->setObjectName(QStringLiteral("lobbyCardJoinBtn"));
    joinBtn->setCursor(Qt::PointingHandCursor);
    joinBtn->setFixedSize(56, 26);
    joinBtn->setEnabled(room.status != "full");
    connect(joinBtn, &QPushButton::clicked, this, [this, room]() {
        onRoomCardJoinClicked(room.roomId);
    });
    bottomRow->addWidget(joinBtn);

    layout->addLayout(bottomRow);

    return card;
}

// ============================
// 数据加载与筛选
// ============================

void CommunityLobbyPage::fetchRoomList()
{
    // 委托给 HongshiLobbyClient，通过信号回调结果
    HongshiLobbyClient::instance()->refreshRooms();
}

void CommunityLobbyPage::onRoomsRefreshed(const QList<HongshiRoomInfo> &rooms)
{
    m_allRooms = rooms;
    populateRoomCards();
}

void CommunityLobbyPage::onRefreshError(const QString &error)
{
    Q_UNUSED(error);
    // 错误时保持已有数据，仅恢复刷新按钮
    m_refreshBtn->setEnabled(true);
    m_refreshBtn->setText(tr("刷新"));
}

void CommunityLobbyPage::onLoadingChanged(bool loading)
{
    if (loading) {
        m_refreshBtn->setEnabled(false);
        m_refreshBtn->setText(tr("加载中..."));
    } else {
        m_refreshBtn->setEnabled(true);
        m_refreshBtn->setText(tr("刷新"));
    }
}

void CommunityLobbyPage::populateRoomCards()
{
    clearRoomCards();

    // 应用筛选
    QString searchText = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : QString();
    int versionIdx = m_versionFilter ? m_versionFilter->currentIndex() : 0;
    int statusIdx = m_statusFilter ? m_statusFilter->currentIndex() : 0;

    m_filteredRooms.clear();
    for (const auto &room : m_allRooms) {
        // 搜索筛选
        if (!searchText.isEmpty()) {
            if (!room.roomName.toLower().contains(searchText)
                && !room.hostName.toLower().contains(searchText)) {
                continue;
            }
        }

        // 版本筛选
        if (versionIdx > 0) {
            QString ver = room.gameVersion;
            bool versionMatch = false;
            switch (versionIdx) {
            case 1: versionMatch = ver.startsWith(QStringLiteral("1.21")); break;
            case 2: versionMatch = ver.startsWith(QStringLiteral("1.20")); break;
            case 3: versionMatch = ver.startsWith(QStringLiteral("1.19")); break;
            case 4: versionMatch = ver.startsWith(QStringLiteral("1.18")); break;
            case 5: versionMatch = !ver.startsWith(QStringLiteral("1.21"))
                    && !ver.startsWith(QStringLiteral("1.20"))
                    && !ver.startsWith(QStringLiteral("1.19"))
                    && !ver.startsWith(QStringLiteral("1.18")); break;
            }
            if (!versionMatch) continue;
        }

        // 状态筛选
        if (statusIdx > 0) {
            QString targetStatus;
            switch (statusIdx) {
            case 1: targetStatus = QStringLiteral("waiting"); break;
            case 2: targetStatus = QStringLiteral("playing"); break;
            case 3: targetStatus = QStringLiteral("full"); break;
            }
            if (room.status != targetStatus) continue;
        }

        m_filteredRooms.append(room);
    }

    // 创建卡片
    for (const auto &room : m_filteredRooms) {
        QWidget *card = createRoomCard(room);
        m_cardFlowLayout->addWidget(card);
        m_cardWidgets.append(card);
    }

    // 更新房间计数
    if (m_roomCountLabel) {
        m_roomCountLabel->setText(tr("(%1 个房间)").arg(m_filteredRooms.size()));
    }

    updateEmptyState();
}

void CommunityLobbyPage::clearRoomCards()
{
    for (QWidget *w : m_cardWidgets) {
        m_cardFlowLayout->removeWidget(w);
        w->deleteLater();
    }
    m_cardWidgets.clear();
}

void CommunityLobbyPage::updateEmptyState()
{
    bool empty = m_filteredRooms.isEmpty();
    m_emptyWidget->setVisible(empty);
    m_scrollArea->setVisible(!empty);
}

// ============================
// 格式化辅助
// ============================

QString CommunityLobbyPage::formatPlayerCount(int current, int max) const
{
    return QStringLiteral("%1/%2").arg(current).arg(max);
}

QString CommunityLobbyPage::formatRelativeTime(qint64 timestamp) const
{
    qint64 diff = QDateTime::currentDateTime().toSecsSinceEpoch() - timestamp;
    if (diff < 60) return tr("刚刚");
    if (diff < 3600) return tr("%1 分钟前").arg(diff / 60);
    if (diff < 86400) return tr("%1 小时前").arg(diff / 3600);
    return tr("%1 天前").arg(diff / 86400);
}

QString CommunityLobbyPage::statusDisplayText(const QString &status) const
{
    if (status == QStringLiteral("waiting")) return tr("等待中");
    if (status == QStringLiteral("playing")) return tr("游戏中");
    if (status == QStringLiteral("full")) return tr("已满");
    return status;
}

QString CommunityLobbyPage::statusProperty(const QString &status) const
{
    if (status == QStringLiteral("waiting")) return QStringLiteral("good");
    if (status == QStringLiteral("playing")) return QStringLiteral("medium");
    if (status == QStringLiteral("full")) return QStringLiteral("bad");
    return QStringLiteral("good");
}

// ============================
// 事件处理
// ============================

void CommunityLobbyPage::onSearchTextChanged(const QString &text)
{
    Q_UNUSED(text);
    populateRoomCards();
}

void CommunityLobbyPage::onVersionFilterChanged(int index)
{
    Q_UNUSED(index);
    populateRoomCards();
}

void CommunityLobbyPage::onStatusFilterChanged(int index)
{
    Q_UNUSED(index);
    populateRoomCards();
}

void CommunityLobbyPage::onRefreshClicked()
{
    HongshiLobbyClient::instance()->refreshRooms();
}

void CommunityLobbyPage::onCreateRoomClicked()
{
    emit createRoomRequested();
}

void CommunityLobbyPage::onRoomCardJoinClicked(const QString &roomId)
{
    for (const auto &room : m_allRooms) {
        if (room.roomId == roomId) {
            emit joinRoomRequested(room);
            return;
        }
    }
}

void CommunityLobbyPage::refreshRooms()
{
    HongshiLobbyClient::instance()->refreshRooms();
}

void CommunityLobbyPage::refresh()
{
    onRefreshClicked();
}

// ============================
// 主题样式
// ============================

void CommunityLobbyPage::applyThemeStyles()
{
    QString themeColor = ThemeManager::instance()->currentThemeColor();
    QString textColor = ThemeManager::instance()->currentTextColor();
    QString borderColor = ThemeManager::instance()->currentBorderColor();
    QString baseColor = ThemeManager::instance()->currentThemeColor();
    QColor tc(themeColor);
    QString hoverBg = QString("rgba(%1, %2, %3, 0.12)")
        .arg(tc.red()).arg(tc.green()).arg(tc.blue());
    const bool isDark = (ThemeManager::instance()->currentTheme()
                         == ThemeManager::DarkTheme);
    const QString disabledBg = isDark ? "#3a3a3a" : "#f5f5f5";
    const QString disabledText = isDark ? "#8a8a8a" : "#999999";
    const QString subtleText = isDark ? "#aaaaaa" : "#888888";

    setStyleSheet(StyleKit::resolve(QString(
        // Tab标签页
        "QTabWidget#communityTabWidget {"
        "  background: transparent;"
        "}"
        "QTabWidget#communityTabWidget::pane {"
        "  border: none;"
        "  background: transparent;"
        "}"
        "QTabBar#communityTabWidget::tab {"
        "  background: transparent;"
        "  color: %7;"
        "  border: none;"
        "  border-bottom: 2px solid transparent;"
        "  padding: 8px 20px;"
        "  font-size: 13px;"
        "  font-weight: 500;"
        "}"
        "QTabBar#communityTabWidget::tab:selected {"
        "  color: %3;"
        "  border-bottom: 2px solid %3;"
        "}"
        "QTabBar#communityTabWidget::tab:hover {"
        "  color: %1;"
        "}"
        // 即将实现页面
        "QLabel#comingSoonIcon {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#comingSoonTitle {"
        "  color: %1;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#comingSoonDesc {"
        "  color: %7;"
        "  font-size: 13px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        // 标题
        "QLabel#lobbyTitleLabel {"
        "  color: %1;"
        "  background: transparent;"
        "  border: none;"
        "}"
        // 房间计数
        "QLabel#lobbyRoomCountLabel {"
        "  color: %7;"
        "  font-size: 12px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        // 搜索框
        "QLineEdit#lobbySearchEdit {"
        "  background: palette(base);"
        "  border: 1px solid %2;"
        "  border-radius: 6px;"
        "  color: %1;"
        "  font-size: 12px;"
        "  padding: 0 10px;"
        "}"
        "QLineEdit#lobbySearchEdit:focus {"
        "  border-color: %3;"
        "}"
        // 筛选下拉框
        "QComboBox#lobbyFilterCombo {"
        "  background: palette(base);"
        "  border: 1px solid %2;"
        "  border-radius: 6px;"
        "  color: %1;"
        "  font-size: 12px;"
        "  padding: 0 8px;"
        "}"
        "QComboBox#lobbyFilterCombo:hover {"
        "  border-color: %3;"
        "}"
        "QComboBox#lobbyFilterCombo::drop-down {"
        "  border: none;"
        "  width: 22px;"
        "}"
        "QComboBox#lobbyFilterCombo::down-arrow {"
        "  image: none;"
        "  border-left: 4px solid transparent;"
        "  border-right: 4px solid transparent;"
        "  border-top: 5px solid %1;"
        "}"
        "QComboBox#lobbyFilterCombo QAbstractItemView {"
        "  background: palette(base);"
        "  color: %1;"
        "  border: 1px solid %2;"
        "  selection-background-color: %4;"
        "  selection-color: %3;"
        "}"
        // 刷新按钮
        "QPushButton#lobbyRefreshBtn {"
        "  background: palette(base);"
        "  border: 1px solid %2;"
        "  border-radius: 6px;"
        "  color: %1;"
        "  font-size: 12px;"
        "  padding: 0 14px;"
        "}"
        "QPushButton#lobbyRefreshBtn:hover {"
        "  border-color: %3;"
        "  color: %3;"
        "}"
        "QPushButton#lobbyRefreshBtn:disabled {"
        "  color: %8;"
        "  background: %9;"
        "}"
        // 创建房间按钮
        "QPushButton#lobbyCreateRoomBtn {"
        "  background: %3;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "  padding: 0 16px;"
        "}"
        "QPushButton#lobbyCreateRoomBtn:hover {"
        "  background: %5;"
        "}"
        // 滚动区域
        "QScrollArea#lobbyScrollArea {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QWidget#lobbyCardContainer {"
        "  background: transparent;"
        "}"
        // 房间卡片
        "QWidget#lobbyRoomCard {"
        "  background: palette(base);"
        "  border: 1px solid %2;"
        "  border-radius: 8px;"
        "}"
        "QWidget#lobbyRoomCard:hover {"
        "  border-color: %3;"
        "}"
        // 房间名
        "QLabel#lobbyCardRoomName {"
        "  color: %1;"
        "  background: transparent;"
        "  border: none;"
        "}"
        // 状态标签
        "QLabel#lobbyCardStatus[status=\"good\"] {"
        "  background-color: rgba(76, 175, 80, 30);"
        "  color: #4caf50;"
        "  border: 1px solid rgba(76, 175, 80, 80);"
        "  border-radius: 8px;"
        "  padding: 2px 8px;"
        "  font-size: 10px;"
        "}"
        "QLabel#lobbyCardStatus[status=\"medium\"] {"
        "  background-color: rgba(255, 193, 7, 30);"
        "  color: @WARNING@;"
        "  border: 1px solid rgba(255, 193, 7, 80);"
        "  border-radius: 8px;"
        "  padding: 2px 8px;"
        "  font-size: 10px;"
        "}"
        "QLabel#lobbyCardStatus[status=\"bad\"] {"
        "  background-color: rgba(244, 67, 54, 30);"
        "  color: @DANGER@;"
        "  border: 1px solid rgba(244, 67, 54, 80);"
        "  border-radius: 8px;"
        "  padding: 2px 8px;"
        "  font-size: 10px;"
        "}"
        // 房主标签
        "QLabel#lobbyCardHostLabel {"
        "  color: %7;"
        "  font-size: 12px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        // 标签（版本/加载器/区域）
        "QLabel#lobbyCardTag {"
        "  background-color: rgba(%4, %5, %6, 26);"
        "  color: %3;"
        "  border: 1px solid %2;"
        "  border-radius: 8px;"
        "  padding: 1px 6px;"
        "  font-size: 10px;"
        "}"
        // 玩家人数
        "QLabel#lobbyCardPlayerLabel {"
        "  color: %1;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "}"
        // 延迟
        "QLabel#lobbyCardLatencyLabel {"
        "  color: %7;"
        "  font-size: 11px;"
        "  background: transparent;"
        "  border: none;"
        "}"
        // 加入按钮
        "QPushButton#lobbyCardJoinBtn {"
        "  background: %3;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 4px;"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "}"
        "QPushButton#lobbyCardJoinBtn:hover {"
        "  background: %5;"
        "}"
        "QPushButton#lobbyCardJoinBtn:disabled {"
        "  background: %9;"
        "  color: %8;"
        "}"
        // 空状态
        "QWidget#lobbyEmptyWidget {"
        "  background: transparent;"
        "}"
        "QLabel#lobbyEmptyText {"
        "  color: %1;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#lobbyEmptyHint {"
        "  color: %7;"
        "  font-size: 12px;"
        "  background: transparent;"
        "  border: none;"
        "}"
    )).arg(textColor, borderColor, themeColor, hoverBg,
          tc.darker(110).name(), baseColor, subtleText,
          disabledText, disabledBg));
}
