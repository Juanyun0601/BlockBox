/**
 * @file   InstanceServerPage.cpp
 * @brief  实例助手 - 本地开服页面实现
 * @author BlockBox Team
 * @date   2026-11-25
 */
#include "InstanceServerPage.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

#include "components/AppMessageBox.h"
#include "components/CustomCheckBox.h"
#include "components/NotificationManager.h"
#include "components/OutlinedLabel.h"
#include "utils/McServerPinger.h"
#include "utils/Saves/LevelDatEditor.h"
#include "utils/server/ServerAdminStore.h"
#include "utils/ServersDatIO.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

namespace {

const char *kKeyPort = "local_server_port";
const char *kKeyPlayers = "local_server_players";
const char *kKeyMemory = "local_server_memory";
const char *kKeyOnlineMode = "local_server_online_mode";
const char *kKeyCore = "local_server_core";
const char *kKeyMotd = "local_server_motd";
const char *kKeyWorld = "local_server_world";

/** 内存下拉框的选项（MB） */
const int kMemoryOptions[] = {1024, 2048, 4096, 8192};

/** 服务器名称默认值（写进 server.properties 的 motd） */
const char *kDefaultMotd = "方块盒子本地服务器";

/** 在线人数探测间隔 */
constexpr int kPingIntervalMs = 5000;

/** 小字说明标签：内容可能很长，统一允许换行 */
QLabel *makeHintLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName("serverHint");
    label->setWordWrap(true);
    return label;
}

} // namespace

InstanceServerPage::InstanceServerPage(QWidget *parent)
    : QWidget(parent)
{
    m_manager = new LocalServerManager(this);
    m_pinger = new McServerPinger(this);

    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(kPingIntervalMs);
    connect(m_pingTimer, &QTimer::timeout, this, &InstanceServerPage::refreshOnlineInfo);

    initUI();
    connectManager();
    connectOnlineProbe();
    loadSettings();
    refreshStateVisuals();
    refreshInstanceHint();
    updateAdminHints();
    m_uiReady = true;
}

InstanceServerPage::~InstanceServerPage() = default;

void InstanceServerPage::initUI()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("serverScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget();
    content->setObjectName("serverContent");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto *title = new OutlinedLabel(tr("本地开服"), content);
    title->setObjectName("sectionTitle");
    auto *subtitle = new QLabel(
        tr("用当前实例的版本，在这台电脑上开一台服务器，朋友在游戏里输入地址就能进来。"), content);
    subtitle->setObjectName("sectionSubtitle");
    subtitle->setWordWrap(true);

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(buildStatusCard());
    layout->addWidget(buildSettingsCard());
    layout->addWidget(buildAdminCard());
    layout->addWidget(buildConsoleCard());

    auto *eulaHint = makeHintLabel(
        tr("点「一键开服」即表示同意 Minecraft 服务器最终用户许可协议（EULA）。"), content);
    eulaHint->setObjectName("serverEulaHint");
    layout->addWidget(eulaHint);
    layout->addStretch(1);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

QWidget *InstanceServerPage::buildStatusCard()
{
    auto *card = new QFrame(this);
    card->setObjectName("serverCard");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // ── 状态行：小圆点 + 状态文字 ──
    auto *statusRow = new QHBoxLayout();
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(7);

    m_statusDot = new QLabel(card);
    m_statusDot->setFixedSize(8, 8);
    m_statusText = new QLabel(tr("未开服"), card);
    m_statusText->setObjectName("serverStatusText");

    statusRow->addWidget(m_statusDot);
    statusRow->addWidget(m_statusText, 1);
    layout->addLayout(statusRow);

    m_instanceLabel = new QLabel(card);
    m_instanceLabel->setObjectName("serverHint");
    m_instanceLabel->setWordWrap(true);
    layout->addWidget(m_instanceLabel);

    // ── 地址行：本机玩家进游戏要填的地址 ──
    auto *addrRow = new QHBoxLayout();
    addrRow->setContentsMargins(0, 0, 0, 0);
    addrRow->setSpacing(6);
    m_addressLabel = new QLabel(displayAddress(), card);
    m_addressLabel->setObjectName("serverAddress");
    m_addressLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_copyBtn = new QPushButton(tr("复制地址"), card);
    m_copyBtn->setObjectName("serverGhostBtn");
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    m_copyBtn->setToolTip(tr("复制后到游戏「多人游戏 → 直接连接」里粘贴"));
    addrRow->addWidget(m_addressLabel, 1);
    addrRow->addWidget(m_copyBtn);
    layout->addLayout(addrRow);

    // ── 在线人数（运行中才有内容） ──
    m_playersLabel = makeHintLabel(QString(), card);
    m_playersLabel->hide();
    layout->addWidget(m_playersLabel);

    m_progressBar = new QProgressBar(card);
    m_progressBar->setObjectName("serverProgress");
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(5);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->hide();
    layout->addWidget(m_progressBar);

    m_primaryBtn = new QPushButton(tr("一键开服"), card);
    m_primaryBtn->setObjectName("serverPrimaryBtn");
    m_primaryBtn->setCursor(Qt::PointingHandCursor);
    m_primaryBtn->setMinimumHeight(36);
    connect(m_primaryBtn, &QPushButton::clicked, this, &InstanceServerPage::onPrimaryClicked);
    layout->addWidget(m_primaryBtn);

    // ── 开服之后的下一步：自己进游戏 / 喊朋友远程加入 ──
    m_runningActions = new QWidget(card);
    auto *actionsRow = new QHBoxLayout(m_runningActions);
    actionsRow->setContentsMargins(0, 0, 0, 0);
    actionsRow->setSpacing(6);

    m_joinGameBtn = new QPushButton(tr("用这个实例进游戏"), m_runningActions);
    m_joinGameBtn->setObjectName("serverGhostBtn");
    m_joinGameBtn->setCursor(Qt::PointingHandCursor);
    m_joinGameBtn->setToolTip(tr("启动当前实例的客户端，并直接连接到这台服务器"));
    connect(m_joinGameBtn, &QPushButton::clicked, this, &InstanceServerPage::onJoinGameClicked);

    m_tunnelBtn = new QPushButton(tr("让朋友远程加入"), m_runningActions);
    m_tunnelBtn->setObjectName("serverGhostBtn");
    m_tunnelBtn->setCursor(Qt::PointingHandCursor);
    m_tunnelBtn->setToolTip(tr("切到「联机」页做一条隧道，把本机端口映射成外网地址"));
    connect(m_tunnelBtn, &QPushButton::clicked, this, &InstanceServerPage::onTunnelClicked);

    actionsRow->addWidget(m_joinGameBtn, 1);
    actionsRow->addWidget(m_tunnelBtn, 1);
    m_runningActions->hide();
    layout->addWidget(m_runningActions);

    m_hintLabel = makeHintLabel(
        tr("点「一键开服」会自动下载对应版本的服务端并启动，第一次要多等一会儿。"), card);
    layout->addWidget(m_hintLabel);

    // ── 顺手写进游戏里的服务器列表 ──
    m_addToListBtn = new QPushButton(tr("加入游戏服务器列表"), card);
    m_addToListBtn->setObjectName("serverGhostBtn");
    m_addToListBtn->setCursor(Qt::PointingHandCursor);
    m_addToListBtn->setToolTip(tr("写进实例的 servers.dat，进游戏打开「多人游戏」就能看到这台服"));
    connect(m_addToListBtn, &QPushButton::clicked, this,
            &InstanceServerPage::onAddToServerListClicked);
    layout->addWidget(m_addToListBtn);

    // ── 发布到社区「服务器导航」：让别的玩家在社区里找到这台服 ──
    m_publishNavBtn = new QPushButton(tr("发布到社区服务器导航"), card);
    m_publishNavBtn->setObjectName("serverGhostBtn");
    m_publishNavBtn->setCursor(Qt::PointingHandCursor);
    m_publishNavBtn->setToolTip(
        tr("把这台服务器发到社区「服务器导航」板块，别人点开帖子就能看到地址和在线人数"));
    connect(m_publishNavBtn, &QPushButton::clicked, this,
            &InstanceServerPage::onPublishToServerNavClicked);
    layout->addWidget(m_publishNavBtn);

    auto *lanHint = makeHintLabel(
        tr("第一次开服时 Windows 可能弹出防火墙提示，允许「专用网络」后同一局域网的朋友才能连进来；"
           "要让外网朋友加入，可以用「联机」页的隧道把地址指向这里的端口。"),
        card);
    layout->addWidget(lanHint);

    connect(m_copyBtn, &QPushButton::clicked, this, &InstanceServerPage::onCopyAddressClicked);
    return card;
}

QWidget *InstanceServerPage::buildSettingsCard()
{
    auto *card = new QFrame(this);
    card->setObjectName("serverCard");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("开服设置"), card);
    title->setObjectName("serverCardTitle");
    layout->addWidget(title);

    // 服务端核心
    auto *coreRow = new QHBoxLayout();
    coreRow->setSpacing(8);
    auto *coreLabel = new QLabel(tr("服务端核心"), card);
    coreLabel->setObjectName("serverFieldLabel");
    coreLabel->setFixedWidth(64);
    m_coreCombo = new QComboBox(card);
    m_coreCombo->setObjectName("serverCombo");
    connect(m_coreCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &InstanceServerPage::onCoreChanged);
    coreRow->addWidget(coreLabel);
    coreRow->addWidget(m_coreCombo, 1);
    layout->addLayout(coreRow);

    m_coreHintLabel = makeHintLabel(QString(), card);
    layout->addWidget(m_coreHintLabel);

    // 世界：新建一个，或者拿实例里的存档开局
    auto *worldRow = new QHBoxLayout();
    worldRow->setSpacing(8);
    auto *worldLabel = new QLabel(tr("世界"), card);
    worldLabel->setObjectName("serverFieldLabel");
    worldLabel->setFixedWidth(64);
    m_worldCombo = new QComboBox(card);
    m_worldCombo->setObjectName("serverCombo");
    m_worldCombo->setToolTip(
        tr("选实例里的存档时，第一次开服会把它复制一份到服务端；之后服务端用自己的那份，"
           "客户端存档不会被改"));
    connect(m_worldCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &InstanceServerPage::onWorldChanged);
    worldRow->addWidget(worldLabel);
    worldRow->addWidget(m_worldCombo, 1);
    layout->addLayout(worldRow);

    // 服务器名称（motd）
    auto *motdRow = new QHBoxLayout();
    motdRow->setSpacing(8);
    auto *motdLabel = new QLabel(tr("服务器名称"), card);
    motdLabel->setObjectName("serverFieldLabel");
    motdLabel->setFixedWidth(64);
    m_motdEdit = new QLineEdit(card);
    m_motdEdit->setObjectName("serverMotdEdit");
    m_motdEdit->setMaxLength(60);
    m_motdEdit->setPlaceholderText(QString::fromUtf8(kDefaultMotd));
    m_motdEdit->setToolTip(tr("朋友在多人游戏列表里看到的服务器名字"));
    connect(m_motdEdit, &QLineEdit::textChanged, this, &InstanceServerPage::onSettingsChanged);
    motdRow->addWidget(motdLabel);
    motdRow->addWidget(m_motdEdit, 1);
    layout->addLayout(motdRow);

    // 端口 / 人数
    auto *portRow = new QHBoxLayout();
    portRow->setSpacing(8);
    auto *portLabel = new QLabel(tr("端口"), card);
    portLabel->setObjectName("serverFieldLabel");
    portLabel->setFixedWidth(64);
    m_portSpin = new QSpinBox(card);
    m_portSpin->setObjectName("serverSpin");
    m_portSpin->setRange(1024, 65535);
    m_portSpin->setValue(25565);
    // 上下按钮在深色主题下会画成灰色小方块（通用 QSpinBox 箭头样式的问题），
    // 这里直接不用按钮，改成点进输入框输入 / 滚轮微调
    m_portSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_portSpin->setToolTip(tr("点一下可以直接输入端口；被占用时会自动顺延一个空闲端口"));
    connect(m_portSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &InstanceServerPage::onSettingsChanged);

    auto *playersLabel = new QLabel(tr("人数"), card);
    playersLabel->setObjectName("serverFieldLabel");
    m_playersSpin = new QSpinBox(card);
    m_playersSpin->setObjectName("serverSpin");
    m_playersSpin->setRange(1, 100);
    m_playersSpin->setValue(10);
    m_playersSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(m_playersSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &InstanceServerPage::onSettingsChanged);

    portRow->addWidget(portLabel);
    portRow->addWidget(m_portSpin, 1);
    portRow->addWidget(playersLabel);
    portRow->addWidget(m_playersSpin, 1);
    layout->addLayout(portRow);

    // 内存
    auto *memRow = new QHBoxLayout();
    memRow->setSpacing(8);
    auto *memLabel = new QLabel(tr("内存"), card);
    memLabel->setObjectName("serverFieldLabel");
    memLabel->setFixedWidth(64);
    m_memoryCombo = new QComboBox(card);
    m_memoryCombo->setObjectName("serverCombo");
    for (int memory : kMemoryOptions)
        m_memoryCombo->addItem(tr("%1 MB").arg(memory), memory);
    connect(m_memoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &InstanceServerPage::onSettingsChanged);
    memRow->addWidget(memLabel);
    memRow->addWidget(m_memoryCombo, 1);
    layout->addLayout(memRow);

    m_onlineModeCheck = new CustomCheckBox(tr("开启正版验证"), card);
    m_onlineModeCheck->setToolTip(
        tr("开启后只有正版账号能加入；和朋友随便玩、或朋友用离线账号时请保持关闭。"));
    connect(m_onlineModeCheck, &CustomCheckBox::toggled, this,
            &InstanceServerPage::onSettingsChanged);
    layout->addWidget(m_onlineModeCheck);

    return card;
}

// ════════════════════════════════════════════════════════════════════
// 管理卡：白名单 / 管理员 / 基础设置
// ════════════════════════════════════════════════════════════════════

QWidget *InstanceServerPage::buildAdminCard()
{
    auto *card = new QFrame(this);
    card->setObjectName("serverCard");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("服务器管理"), card);
    title->setObjectName("serverCardTitle");
    layout->addWidget(title);
    layout->addWidget(makeHintLabel(
        tr("白名单、管理员和常用规则都收在这里；服务器开着改的会直接同步给它。"), card));

    // ── 三个页签：分段按钮，窄窗口下也不占地方 ──
    auto *segBar = new QWidget(card);
    segBar->setObjectName("serverSegBar");
    auto *segLayout = new QHBoxLayout(segBar);
    segLayout->setContentsMargins(3, 3, 3, 3);
    segLayout->setSpacing(2);

    auto *group = new QButtonGroup(segBar);
    group->setExclusive(true);
    const QStringList tabNames = {tr("白名单"), tr("管理员"), tr("基础设置")};
    for (int i = 0; i < tabNames.size(); ++i) {
        auto *btn = new QPushButton(tabNames.at(i), segBar);
        btn->setObjectName("serverSegBtn");
        btn->setCheckable(true);
        btn->setChecked(i == 0);
        btn->setCursor(Qt::PointingHandCursor);
        group->addButton(btn, i);
        segLayout->addWidget(btn, 1);
        m_adminTabBtns.append(btn);
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int id) { setAdminTab(id); });
    layout->addWidget(segBar);

    // 三个页签是同一层的兄弟控件，只显示当前这张：
    // 用 QStackedWidget 的话卡片高度会永远按最高那页算，短页下面留一大片空白。
    auto *pagesHost = new QWidget(card);
    pagesHost->setObjectName("serverAdminPages");
    m_adminPagesLayout = new QVBoxLayout(pagesHost);
    m_adminPagesLayout->setContentsMargins(0, 0, 0, 0);
    m_adminPagesLayout->setSpacing(0);
    m_whitelistPage = buildWhitelistTab();
    m_opPage = buildOpTab();
    m_rulesPage = buildRulesTab();
    m_adminPagesLayout->addWidget(m_whitelistPage);
    m_adminPagesLayout->addWidget(m_opPage);
    m_adminPagesLayout->addWidget(m_rulesPage);
    layout->addWidget(pagesHost);
    setAdminTab(0);

    return card;
}

void InstanceServerPage::setAdminTab(int index)
{
    m_adminTab = qBound(0, index, 2);
    if (m_adminTabBtns.size() > m_adminTab && !m_adminTabBtns.at(m_adminTab)->isChecked())
        m_adminTabBtns.at(m_adminTab)->setChecked(true);
    if (m_whitelistPage)
        m_whitelistPage->setVisible(m_adminTab == 0);
    if (m_opPage)
        m_opPage->setVisible(m_adminTab == 1);
    if (m_rulesPage)
        m_rulesPage->setVisible(m_adminTab == 2);
}

QWidget *InstanceServerPage::buildWhitelistTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 2, 0, 0);
    layout->setSpacing(8);

    m_whitelistToggle = new CustomCheckBox(tr("开启白名单（只有名单里的玩家能进）"), page);
    m_whitelistToggle->setToolTip(
        tr("白名单关着时谁都能进服；开启后只有下面这些玩家能进来。"));
    connect(m_whitelistToggle, &CustomCheckBox::toggled, this,
            &InstanceServerPage::onWhitelistToggled);
    layout->addWidget(m_whitelistToggle);

    auto *addRow = new QHBoxLayout();
    addRow->setContentsMargins(0, 0, 0, 0);
    addRow->setSpacing(6);
    m_whitelistInput = new QLineEdit(page);
    m_whitelistInput->setPlaceholderText(tr("玩家名，如 Steve"));
    m_whitelistInput->setMaxLength(16);
    m_whitelistInput->setToolTip(tr("填对方在游戏里用的名字，敲回车或点「添加」"));
    connect(m_whitelistInput, &QLineEdit::returnPressed, this,
            &InstanceServerPage::onAddWhitelistClicked);
    m_whitelistAddBtn = new QPushButton(tr("添加"), page);
    m_whitelistAddBtn->setObjectName("serverGhostBtn");
    m_whitelistAddBtn->setCursor(Qt::PointingHandCursor);
    connect(m_whitelistAddBtn, &QPushButton::clicked, this,
            &InstanceServerPage::onAddWhitelistClicked);
    addRow->addWidget(m_whitelistInput, 1);
    addRow->addWidget(m_whitelistAddBtn);
    layout->addLayout(addRow);

    m_whitelistScroll = new QScrollArea(page);
    m_whitelistScroll->setObjectName("serverAdminScroll");
    m_whitelistScroll->setWidgetResizable(true);
    m_whitelistScroll->setFrameShape(QFrame::NoFrame);
    m_whitelistScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_whitelistScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto *listHost = new QWidget(m_whitelistScroll);
    listHost->setObjectName("serverAdminListHost");
    m_whitelistListLayout = new QVBoxLayout(listHost);
    m_whitelistListLayout->setContentsMargins(0, 0, 0, 0);
    m_whitelistListLayout->setSpacing(4);
    m_whitelistListLayout->addStretch(1);
    m_whitelistScroll->setWidget(listHost);
    layout->addWidget(m_whitelistScroll);

    m_whitelistEmpty = makeHintLabel(
        tr("白名单里还没有人。填一个玩家名加进来，就能开白名单了。"), page);
    layout->addWidget(m_whitelistEmpty);

    m_whitelistNotice = makeHintLabel(QString(), page);
    m_whitelistNotice->hide();
    layout->addWidget(m_whitelistNotice);

    m_whitelistHint = makeHintLabel(QString(), page);
    layout->addWidget(m_whitelistHint);
    // 三个页签共用一个高度，短页多出来的地方留在底部，别把中间的元素拉开
    layout->addStretch(1);
    return page;
}

QWidget *InstanceServerPage::buildOpTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 2, 0, 0);
    layout->setSpacing(8);

    auto *addRow = new QHBoxLayout();
    addRow->setContentsMargins(0, 0, 0, 0);
    addRow->setSpacing(6);
    m_opInput = new QLineEdit(page);
    m_opInput->setPlaceholderText(tr("玩家名"));
    m_opInput->setMaxLength(16);
    connect(m_opInput, &QLineEdit::returnPressed, this, &InstanceServerPage::onAddOpClicked);
    m_opLevelCombo = new QComboBox(page);
    m_opLevelCombo->setToolTip(tr("4 级能管所有事；3 级能封人、踢人和授管理员；2 级能用常用指令；1 级只能绕过出生点保护。"));
    m_opLevelCombo->addItem(tr("4 级 全部"), 4);
    m_opLevelCombo->addItem(tr("3 级 管人"), 3);
    m_opLevelCombo->addItem(tr("2 级 指令"), 2);
    m_opLevelCombo->addItem(tr("1 级 基础"), 1);
    m_opLevelCombo->setFixedWidth(124);
    m_opAddBtn = new QPushButton(tr("添加"), page);
    m_opAddBtn->setObjectName("serverGhostBtn");
    m_opAddBtn->setCursor(Qt::PointingHandCursor);
    connect(m_opAddBtn, &QPushButton::clicked, this, &InstanceServerPage::onAddOpClicked);
    addRow->addWidget(m_opInput, 1);
    addRow->addWidget(m_opLevelCombo);
    addRow->addWidget(m_opAddBtn);
    layout->addLayout(addRow);

    m_opScroll = new QScrollArea(page);
    m_opScroll->setObjectName("serverAdminScroll");
    m_opScroll->setWidgetResizable(true);
    m_opScroll->setFrameShape(QFrame::NoFrame);
    m_opScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_opScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto *listHost = new QWidget(m_opScroll);
    listHost->setObjectName("serverAdminListHost");
    m_opListLayout = new QVBoxLayout(listHost);
    m_opListLayout->setContentsMargins(0, 0, 0, 0);
    m_opListLayout->setSpacing(4);
    m_opListLayout->addStretch(1);
    m_opScroll->setWidget(listHost);
    layout->addWidget(m_opScroll);

    m_opEmpty = makeHintLabel(
        tr("还没有管理员。加一个人之后，他在游戏里就能用 /gamemode、/tp 这些指令。"), page);
    layout->addWidget(m_opEmpty);

    m_opNotice = makeHintLabel(QString(), page);
    m_opNotice->hide();
    layout->addWidget(m_opNotice);

    m_opHint = makeHintLabel(QString(), page);
    layout->addWidget(m_opHint);
    layout->addStretch(1);
    return page;
}

QWidget *InstanceServerPage::buildRulesTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 2, 0, 0);
    layout->setSpacing(8);

    auto makeComboRow = [this, page, layout](const QString &labelText, const QString &tip) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        auto *label = new QLabel(labelText, page);
        label->setObjectName("serverFieldLabel");
        label->setFixedWidth(64);
        auto *combo = new QComboBox(page);
        combo->setToolTip(tip);
        row->addWidget(label);
        row->addWidget(combo, 1);
        layout->addLayout(row);
        return combo;
    };

    m_gamemodeCombo = makeComboRow(
        tr("游戏模式"),
        tr("新玩家进服时的默认模式；服务器开着时改动会直接同步给服务端。"));
    m_gamemodeCombo->addItem(tr("生存"), QStringLiteral("survival"));
    m_gamemodeCombo->addItem(tr("创造"), QStringLiteral("creative"));
    m_gamemodeCombo->addItem(tr("冒险"), QStringLiteral("adventure"));
    m_gamemodeCombo->addItem(tr("旁观"), QStringLiteral("spectator"));
    connect(m_gamemodeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onAdminRuleChanged(); });

    m_difficultyCombo = makeComboRow(
        tr("难度"),
        tr("和平不会刷怪，困难怪物最凶；服务器开着时改动立刻生效。"));
    m_difficultyCombo->addItem(tr("和平"), QStringLiteral("peaceful"));
    m_difficultyCombo->addItem(tr("简单"), QStringLiteral("easy"));
    m_difficultyCombo->addItem(tr("普通"), QStringLiteral("normal"));
    m_difficultyCombo->addItem(tr("困难"), QStringLiteral("hard"));
    connect(m_difficultyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onAdminRuleChanged(); });

    m_pvpCheck = new CustomCheckBox(tr("允许玩家互相攻击（PVP）"), page);
    connect(m_pvpCheck, &CustomCheckBox::toggled, this, [this](bool) { onAdminRuleChanged(); });
    layout->addWidget(m_pvpCheck);

    m_flightCheck = new CustomCheckBox(tr("允许飞行"), page);
    m_flightCheck->setToolTip(
        tr("原版服务端默认把飞行当成作弊：装了飞行 / 鞘翅类模组时要打开，不然会被踢。"));
    connect(m_flightCheck, &CustomCheckBox::toggled, this, [this](bool) { onAdminRuleChanged(); });
    layout->addWidget(m_flightCheck);

    auto makeSpinRow = [this, page, layout](const QString &labelText, int minimum, int maximum,
                                            const QString &tip) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        auto *label = new QLabel(labelText, page);
        label->setObjectName("serverFieldLabel");
        label->setFixedWidth(64);
        auto *spin = new QSpinBox(page);
        spin->setObjectName("serverSpin");
        spin->setRange(minimum, maximum);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setToolTip(tip);
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int) { onAdminRuleChanged(); });
        row->addWidget(label);
        row->addWidget(spin, 1);
        layout->addLayout(row);
        return spin;
    };

    m_spawnProtectSpin = makeSpinRow(
        tr("出生点保护"), 0, 64,
        tr("出生点周围多少格内只有管理员能放 / 拆方块；0 表示不保护。"));
    m_viewDistanceSpin = makeSpinRow(
        tr("视距"), 3, 32, tr("玩家能看到多远（区块数）；调小更省服务器资源。"));

    m_rulesNotice = makeHintLabel(QString(), page);
    m_rulesNotice->hide();
    layout->addWidget(m_rulesNotice);

    m_rulesHint = makeHintLabel(
        tr("这些规则写在服务端的 server.properties 里；标了「重启后生效」的项要下次开服才变。"),
        page);
    layout->addWidget(m_rulesHint);
    layout->addStretch(1);
    return page;
}

void InstanceServerPage::onAdminRuleChanged()
{
    if (m_syncingAdmin || !m_adminLoaded || !m_gamemodeCombo)
        return;

    ServerAdminStore store = adminStore();
    if (!store.isValid())
        return;

    QMap<QString, QString> values;
    values.insert(QStringLiteral("gamemode"), m_gamemodeCombo->currentData().toString());
    values.insert(QStringLiteral("difficulty"), m_difficultyCombo->currentData().toString());
    values.insert(QStringLiteral("pvp"), m_pvpCheck->isChecked() ? QStringLiteral("true")
                                                                : QStringLiteral("false"));
    values.insert(QStringLiteral("allow-flight"),
                  m_flightCheck->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    values.insert(QStringLiteral("spawn-protection"),
                  QString::number(m_spawnProtectSpin->value()));
    values.insert(QStringLiteral("view-distance"), QString::number(m_viewDistanceSpin->value()));

    QString error;
    if (!store.setProperties(values, &error)) {
        setAdminNotice(m_rulesNotice, error, true);
        return;
    }

    if (m_manager->isRunning()) {
        // 难度和默认模式有对应指令，可以先让服务端立刻按新值走；
        // PVP / 飞行 / 出生点保护 / 视距 只能等下次开服读配置。
        m_manager->sendCommand(QStringLiteral("difficulty %1").arg(values.value(
            QStringLiteral("difficulty"))));
        m_manager->sendCommand(QStringLiteral("defaultgamemode %1").arg(values.value(
            QStringLiteral("gamemode"))));
        setAdminNotice(m_rulesNotice,
                       tr("游戏模式和难度已经同步给服务器；其它几项下次开服生效。"), false);
    } else {
        setAdminNotice(m_rulesNotice, tr("规则已保存，下次开服按这里的设置走。"), false);
    }
}

ServerAdminStore InstanceServerPage::adminStore() const
{
    return ServerAdminStore(m_manager ? m_manager->serverDir() : QString());
}

bool InstanceServerPage::canEditLists(QString *reason) const
{
    // 服务器开着时走指令，由服务端自己写名单，UUID 由它去认；
    // 只有「停服 + 正版验证」这一种情况算不出正版 UUID，只能劝用户先开服。
    if (m_manager && m_manager->isRunning())
        return true;
    if (m_onlineModeCheck && m_onlineModeCheck->isChecked()) {
        if (reason) {
            *reason = tr("这台服开着正版验证：名单要按正版账号的 UUID 记，停服时算不准。"
                         "先把服务器开起来再加，或者先把正版验证关掉。");
        }
        return false;
    }
    return true;
}

void InstanceServerPage::onWhitelistToggled(bool enabled)
{
    if (m_syncingAdmin || !m_adminLoaded)
        return;
    ServerAdminStore store = adminStore();
    if (!store.isValid())
        return;

    QString error;
    if (!store.setProperty(QStringLiteral("white-list"),
                           enabled ? QStringLiteral("true") : QStringLiteral("false"), &error)) {
        setAdminNotice(m_whitelistNotice, error, true);
        // 写不进去就把开关拨回去，免得界面上显示的和实际不一致
        m_syncingAdmin = true;
        m_whitelistToggle->setChecked(!enabled);
        m_syncingAdmin = false;
        return;
    }

    if (m_manager->isRunning())
        m_manager->sendCommand(enabled ? QStringLiteral("whitelist on")
                                       : QStringLiteral("whitelist off"));

    setAdminNotice(m_whitelistNotice,
                   enabled ? tr("白名单已开启：只有名单里的玩家能进服。")
                           : tr("白名单已关闭：谁都能进服。"),
                   false);
}

void InstanceServerPage::onAddWhitelistClicked()
{
    const QString name = m_whitelistInput ? m_whitelistInput->text().trimmed() : QString();
    if (name.isEmpty()) {
        setAdminNotice(m_whitelistNotice, tr("先填一个玩家名。"), true);
        return;
    }
    addWhitelistEntry(name);
}

bool InstanceServerPage::addWhitelistEntry(const QString &name)
{
    if (!ServerAdminStore::isValidPlayerName(name)) {
        setAdminNotice(m_whitelistNotice,
                       tr("玩家名只能是 3~16 位字母、数字或下划线，要和游戏里用的名字完全一致。"),
                       true);
        return false;
    }
    for (const auto &entry : m_whitelistEntries) {
        if (entry.name.compare(name, Qt::CaseInsensitive) == 0) {
            setAdminNotice(m_whitelistNotice, tr("%1 已经在白名单里了。").arg(name), false);
            return false;
        }
    }

    QString reason;
    if (!canEditLists(&reason)) {
        setAdminNotice(m_whitelistNotice, reason, true);
        return false;
    }

    if (m_manager->isRunning()) {
        if (!m_manager->sendCommand(QStringLiteral("whitelist add %1").arg(name))) {
            setAdminNotice(m_whitelistNotice, tr("指令没发出去，服务器可能刚退出。"), true);
            return false;
        }
        setAdminNotice(m_whitelistNotice,
                       tr("已经让服务器把 %1 加进白名单，稍等一下列表就会更新。").arg(name),
                       false);
        if (m_whitelistInput)
            m_whitelistInput->clear();
        m_pendingWhitelistAdds.append(name);
        scheduleAdminReload(900);
        return true;
    }

    ServerAdminStore store = adminStore();
    QString readError;
    QList<ServerWhitelistEntry> entries = store.readWhitelist(&readError);
    if (!readError.isEmpty()) {
        setAdminNotice(m_whitelistNotice, readError, true);
        return false;
    }
    ServerWhitelistEntry entry;
    entry.name = name;
    entry.uuid = ServerAdminStore::offlineUuid(name);
    entries.append(entry);

    QString error;
    if (!store.writeWhitelist(entries, &error)) {
        setAdminNotice(m_whitelistNotice, error, true);
        return false;
    }
    m_whitelistInput->clear();
    reloadAdminData();
    setAdminNotice(m_whitelistNotice, tr("已加进白名单：%1").arg(name), false);
    return true;
}

bool InstanceServerPage::removeWhitelistEntry(ServerWhitelistEntry entry)
{
    if (entry.name.isEmpty()) {
        setAdminNotice(m_whitelistNotice,
                       tr("这条只记了 UUID，没记名字：停服后删掉 whitelist.json 里的这条，"
                          "或者开服后敲 whitelist remove 指令。"),
                       true);
        return false;
    }

    if (m_manager->isRunning()) {
        if (!m_manager->sendCommand(QStringLiteral("whitelist remove %1").arg(entry.name))) {
            setAdminNotice(m_whitelistNotice, tr("指令没发出去，服务器可能刚退出。"), true);
            return false;
        }
        setAdminNotice(m_whitelistNotice,
                       tr("已经让服务器把 %1 移出白名单。").arg(entry.name), false);
        m_pendingWhitelistRemoves.append(entry.name);
        scheduleAdminReload(900);
        return true;
    }

    ServerAdminStore store = adminStore();
    QString readError;
    QList<ServerWhitelistEntry> entries = store.readWhitelist(&readError);
    if (!readError.isEmpty()) {
        setAdminNotice(m_whitelistNotice, readError, true);
        return false;
    }
    QList<ServerWhitelistEntry> kept;
    for (const auto &item : entries) {
        const bool sameUuid = !entry.uuid.isEmpty() && item.uuid.compare(entry.uuid) == 0;
        const bool sameName = !entry.name.isEmpty()
                              && item.name.compare(entry.name, Qt::CaseInsensitive) == 0;
        if (!sameUuid && !sameName)
            kept.append(item);
    }
    QString error;
    if (!store.writeWhitelist(kept, &error)) {
        setAdminNotice(m_whitelistNotice, error, true);
        return false;
    }
    reloadAdminData();
    setAdminNotice(m_whitelistNotice, tr("已把 %1 移出白名单。").arg(entry.name), false);
    return true;
}

void InstanceServerPage::onAddOpClicked()
{
    const QString name = m_opInput ? m_opInput->text().trimmed() : QString();
    if (name.isEmpty()) {
        setAdminNotice(m_opNotice, tr("先填一个玩家名。"), true);
        return;
    }
    const int level = m_opLevelCombo ? m_opLevelCombo->currentData().toInt() : 4;
    addOpEntry(name, level);
}

bool InstanceServerPage::addOpEntry(const QString &name, int level)
{
    if (!ServerAdminStore::isValidPlayerName(name)) {
        setAdminNotice(m_opNotice,
                       tr("玩家名只能是 3~16 位字母、数字或下划线，要和游戏里用的名字完全一致。"),
                       true);
        return false;
    }
    level = qBound(1, level, 4);

    QString reason;
    if (!canEditLists(&reason)) {
        setAdminNotice(m_opNotice, reason, true);
        return false;
    }

    const bool running = m_manager->isRunning();

    // 运行中的 4 级直接交给服务端：它会拿正版 UUID 写进 ops.json，比我们算得准
    if (running && level == 4) {
        if (!m_manager->sendCommand(QStringLiteral("op %1").arg(name))) {
            setAdminNotice(m_opNotice, tr("指令没发出去，服务器可能刚退出。"), true);
            return false;
        }
        setAdminNotice(m_opNotice, tr("已经让服务器把 %1 设为管理员（4 级）。").arg(name), false);
        if (m_opInput)
            m_opInput->clear();
        m_pendingOpAdds.append(name);
        scheduleAdminReload(900);
        return true;
    }

    ServerAdminStore store = adminStore();
    QString readError;
    QList<ServerOpEntry> entries = store.readOps(&readError);
    if (!readError.isEmpty()) {
        setAdminNotice(m_opNotice, readError, true);
        return false;
    }

    QList<ServerOpEntry> kept;
    for (const auto &item : entries) {
        if (item.name.compare(name, Qt::CaseInsensitive) != 0)
            kept.append(item);
    }
    ServerOpEntry entry;
    entry.name = name;
    entry.uuid = ServerAdminStore::offlineUuid(name);
    entry.level = level;
    kept.append(entry);

    QString error;
    if (!store.writeOps(kept, &error)) {
        setAdminNotice(m_opNotice, error, true);
        return false;
    }
    m_opInput->clear();

    if (running) {
        // 服务端的 op 指令只能给 4 级，1~3 级得等它下次启动读 ops.json
        setAdminNotice(m_opNotice,
                       tr("已记下 %1 的 %2 权限，重启服务器后生效"
                          "（服务器运行中只能直接授予 4 级）。")
                           .arg(name, opLevelText(level)),
                       false);
    } else {
        reloadAdminData();
        setAdminNotice(m_opNotice, tr("已把 %1 设为管理员（%2）。").arg(name, opLevelText(level)),
                       false);
    }
    return true;
}

bool InstanceServerPage::removeOpEntry(ServerOpEntry entry)
{
    if (entry.name.isEmpty()) {
        setAdminNotice(m_opNotice, tr("这条只记了 UUID，没记名字，停服后到 ops.json 里删掉它。"),
                       true);
        return false;
    }

    if (m_manager->isRunning()) {
        if (!m_manager->sendCommand(QStringLiteral("deop %1").arg(entry.name))) {
            setAdminNotice(m_opNotice, tr("指令没发出去，服务器可能刚退出。"), true);
            return false;
        }
        setAdminNotice(m_opNotice, tr("已经让服务器取消 %1 的管理员权限。").arg(entry.name), false);
        m_pendingOpRemoves.append(entry.name);
        scheduleAdminReload(900);
        return true;
    }

    ServerAdminStore store = adminStore();
    QString readError;
    QList<ServerOpEntry> entries = store.readOps(&readError);
    if (!readError.isEmpty()) {
        setAdminNotice(m_opNotice, readError, true);
        return false;
    }
    QList<ServerOpEntry> kept;
    for (const auto &item : entries) {
        const bool sameUuid = !entry.uuid.isEmpty() && item.uuid.compare(entry.uuid) == 0;
        const bool sameName = !entry.name.isEmpty()
                              && item.name.compare(entry.name, Qt::CaseInsensitive) == 0;
        if (!sameUuid && !sameName)
            kept.append(item);
    }
    QString error;
    if (!store.writeOps(kept, &error)) {
        setAdminNotice(m_opNotice, error, true);
        return false;
    }
    reloadAdminData();
    setAdminNotice(m_opNotice, tr("已取消 %1 的管理员权限。").arg(entry.name), false);
    return true;
}

QString InstanceServerPage::opLevelText(int level) const
{
    switch (level) {
    case 1:
        return tr("1 级 · 基础");
    case 2:
        return tr("2 级 · 常用指令");
    case 3:
        return tr("3 级 · 管理玩家");
    default:
        return tr("4 级 · 完全权限");
    }
}

void InstanceServerPage::setAdminNotice(QLabel *label, const QString &text, bool error)
{
    if (!label)
        return;
    label->setText(text);
    label->setVisible(!text.isEmpty());
    label->setStyleSheet(error ? QStringLiteral("color: #e5484d;") : QString());
}

void InstanceServerPage::reloadAdminData()
{
    if (!m_whitelistPage || !m_manager || m_instancePath.isEmpty())
        return;

    ServerAdminStore store = adminStore();
    if (!store.isValid())
        return;

    const bool previousSync = m_syncingAdmin;
    m_syncingAdmin = true;
    m_adminLoaded = false;
    QString whitelistError;
    m_whitelistEntries = store.readWhitelist(&whitelistError);
    const QString whiteListValue = store.property(QStringLiteral("white-list"));
    m_whitelistToggle->setChecked(whiteListValue.compare(QStringLiteral("true"),
                                                         Qt::CaseInsensitive) == 0);
    rebuildWhitelistList();
    if (!whitelistError.isEmpty())
        setAdminNotice(m_whitelistNotice, whitelistError, true);

    QString opsError;
    m_opEntries = store.readOps(&opsError);
    rebuildOpList();
    if (!opsError.isEmpty())
        setAdminNotice(m_opNotice, opsError, true);

    loadRulesFromProperties();
    updateAdminHints();

    m_syncingAdmin = previousSync;
    m_adminLoaded = true;
}

void InstanceServerPage::scheduleAdminReload(int delayMs)
{
    if (m_adminReloadScheduled)
        return;
    m_adminReloadScheduled = true;
    QTimer::singleShot(delayMs, this, [this]() {
        m_adminReloadScheduled = false;
        reloadAdminData();
        verifyPendingAdminChanges();
    });
}

void InstanceServerPage::verifyPendingAdminChanges()
{
    const bool hasPending = !m_pendingWhitelistAdds.isEmpty()
                            || !m_pendingWhitelistRemoves.isEmpty()
                            || !m_pendingOpAdds.isEmpty() || !m_pendingOpRemoves.isEmpty();
    if (!hasPending)
        return;

    const QString separator = QStringLiteral("、");
    auto whitelistHas = [this](const QString &name) {
        for (const auto &entry : m_whitelistEntries) {
            if (entry.name.compare(name, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    };
    auto opsHave = [this](const QString &name) {
        for (const auto &entry : m_opEntries) {
            if (entry.name.compare(name, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    };

    QStringList whitelistAdded;
    QStringList whitelistAddFailed;
    QStringList whitelistRemoved;
    QStringList whitelistRemoveFailed;
    for (const QString &name : m_pendingWhitelistAdds)
        (whitelistHas(name) ? whitelistAdded : whitelistAddFailed).append(name);
    for (const QString &name : m_pendingWhitelistRemoves)
        (whitelistHas(name) ? whitelistRemoveFailed : whitelistRemoved).append(name);

    QStringList opAdded;
    QStringList opAddFailed;
    QStringList opRemoved;
    QStringList opRemoveFailed;
    for (const QString &name : m_pendingOpAdds)
        (opsHave(name) ? opAdded : opAddFailed).append(name);
    for (const QString &name : m_pendingOpRemoves)
        (opsHave(name) ? opRemoveFailed : opRemoved).append(name);

    // 服务器写盘有时比 900ms 慢一点：第一次没对上先再等一轮，别急着下结论
    const bool anyFailure = !whitelistAddFailed.isEmpty() || !whitelistRemoveFailed.isEmpty()
                            || !opAddFailed.isEmpty() || !opRemoveFailed.isEmpty();
    if (anyFailure && !m_adminVerifyRetry) {
        m_adminVerifyRetry = true;
        scheduleAdminReload(1400);
        return;
    }
    m_adminVerifyRetry = false;

    m_pendingWhitelistAdds.clear();
    m_pendingWhitelistRemoves.clear();
    m_pendingOpAdds.clear();
    m_pendingOpRemoves.clear();

    if (!whitelistAdded.isEmpty() || !whitelistAddFailed.isEmpty()) {
        if (!whitelistAddFailed.isEmpty()) {
            setAdminNotice(m_whitelistNotice,
                           tr("服务器没把 %1 加进白名单：名字要和游戏里一致"
                              "（正版服填正版账号名，离线服填离线名字）。")
                               .arg(whitelistAddFailed.join(separator)),
                           true);
        } else {
            setAdminNotice(m_whitelistNotice,
                           tr("已加进白名单：%1").arg(whitelistAdded.join(separator)), false);
        }
    }

    if (!whitelistRemoved.isEmpty() || !whitelistRemoveFailed.isEmpty()) {
        if (!whitelistRemoveFailed.isEmpty()) {
            setAdminNotice(m_whitelistNotice,
                           tr("服务器还没把 %1 移出白名单，稍后再点一次「移除」。")
                               .arg(whitelistRemoveFailed.join(separator)),
                           true);
        } else {
            setAdminNotice(m_whitelistNotice, tr("已经移出白名单。"), false);
        }
    }

    if (!opAdded.isEmpty() || !opAddFailed.isEmpty()) {
        if (!opAddFailed.isEmpty()) {
            setAdminNotice(m_opNotice,
                           tr("服务器没把 %1 设为管理员：名字要和游戏里一致"
                              "（正版服填正版账号名，离线服填离线名字）。")
                               .arg(opAddFailed.join(separator)),
                           true);
        } else {
            setAdminNotice(m_opNotice, tr("已设为管理员：%1").arg(opAdded.join(separator)),
                           false);
        }
    }

    if (!opRemoved.isEmpty() || !opRemoveFailed.isEmpty()) {
        if (!opRemoveFailed.isEmpty()) {
            setAdminNotice(m_opNotice,
                           tr("服务器还没取消 %1 的管理员权限，稍后再点一次「移除」。")
                               .arg(opRemoveFailed.join(separator)),
                           true);
        } else {
            setAdminNotice(m_opNotice, tr("已取消管理员权限。"), false);
        }
    }
}

void InstanceServerPage::rebuildWhitelistList()
{
    if (!m_whitelistListLayout)
        return;

    // 清掉旧行（末尾留着的 stretch 不动）
    while (m_whitelistListLayout->count() > 1) {
        QLayoutItem *item = m_whitelistListLayout->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    auto makeRow = [this](const QString &name, const QString &meta, const QString &tip,
                          const std::function<void()> &onRemove) {
        auto *row = new QFrame(this);
        row->setObjectName("serverPlayerRow");
        row->setToolTip(tip);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 4, 6, 4);
        rowLayout->setSpacing(6);
        auto *nameLabel = new QLabel(name, row);
        nameLabel->setObjectName("serverPlayerName");
        auto *metaLabel = new QLabel(meta, row);
        metaLabel->setObjectName("serverPlayerMeta");
        auto *removeBtn = new QPushButton(tr("移除"), row);
        removeBtn->setObjectName("serverRowBtn");
        removeBtn->setCursor(Qt::PointingHandCursor);
        connect(removeBtn, &QPushButton::clicked, row, [onRemove]() { onRemove(); });
        rowLayout->addWidget(nameLabel);
        rowLayout->addStretch(1);
        if (!meta.isEmpty())
            rowLayout->addWidget(metaLabel);
        rowLayout->addWidget(removeBtn);
        return row;
    };

    for (const auto &entry : m_whitelistEntries) {
        const QString displayName = entry.name.isEmpty() ? tr("未知玩家") : entry.name;
        const QString tip = entry.uuid.isEmpty()
                                ? tr("这条没有记 UUID，可能进不来")
                                : tr("UUID：%1").arg(entry.uuid);
        const ServerWhitelistEntry copy = entry;
        m_whitelistListLayout->insertWidget(
            m_whitelistListLayout->count() - 1,
            makeRow(displayName, QString(), tip,
                    [this, copy]() { removeWhitelistEntry(copy); }));
    }

    const bool empty = m_whitelistEntries.isEmpty();
    m_whitelistEmpty->setVisible(empty);
    m_whitelistScroll->setVisible(!empty);
    if (!empty) {
        const int height = qMin(150, m_whitelistEntries.size() * 32 + 6);
        m_whitelistScroll->setFixedHeight(qMax(38, height));
    }
}

void InstanceServerPage::rebuildOpList()
{
    if (!m_opListLayout)
        return;

    while (m_opListLayout->count() > 1) {
        QLayoutItem *item = m_opListLayout->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    auto makeRow = [this](const QString &name, const QString &meta, const QString &tip,
                          const std::function<void()> &onRemove) {
        auto *row = new QFrame(this);
        row->setObjectName("serverPlayerRow");
        row->setToolTip(tip);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 4, 6, 4);
        rowLayout->setSpacing(6);
        auto *nameLabel = new QLabel(name, row);
        nameLabel->setObjectName("serverPlayerName");
        auto *metaLabel = new QLabel(meta, row);
        metaLabel->setObjectName("serverPlayerMeta");
        auto *removeBtn = new QPushButton(tr("移除"), row);
        removeBtn->setObjectName("serverRowBtn");
        removeBtn->setCursor(Qt::PointingHandCursor);
        connect(removeBtn, &QPushButton::clicked, row, [onRemove]() { onRemove(); });
        rowLayout->addWidget(nameLabel);
        rowLayout->addStretch(1);
        rowLayout->addWidget(metaLabel);
        rowLayout->addWidget(removeBtn);
        return row;
    };

    for (const auto &entry : m_opEntries) {
        const QString displayName = entry.name.isEmpty() ? tr("未知玩家") : entry.name;
        const QString tip = entry.uuid.isEmpty()
                                ? tr("这条没有记 UUID，可能不生效")
                                : tr("UUID：%1").arg(entry.uuid);
        const ServerOpEntry copy = entry;
        m_opListLayout->insertWidget(
            m_opListLayout->count() - 1,
            makeRow(displayName, tr("%1 级").arg(qBound(1, entry.level, 4)), tip,
                    [this, copy]() { removeOpEntry(copy); }));
    }

    const bool empty = m_opEntries.isEmpty();
    m_opEmpty->setVisible(empty);
    m_opScroll->setVisible(!empty);
    if (!empty) {
        const int height = qMin(150, m_opEntries.size() * 32 + 6);
        m_opScroll->setFixedHeight(qMax(38, height));
    }
}

void InstanceServerPage::loadRulesFromProperties()
{
    ServerAdminStore store = adminStore();
    const bool previousSync = m_syncingAdmin;
    m_syncingAdmin = true;

    auto selectByData = [](QComboBox *combo, const QString &value, const QString &fallback) {
        if (!combo)
            return;
        int index = combo->findData(value);
        if (index < 0)
            index = combo->findData(fallback);
        if (index >= 0)
            combo->setCurrentIndex(index);
    };

    selectByData(m_gamemodeCombo, store.property(QStringLiteral("gamemode")),
                 QStringLiteral("survival"));
    selectByData(m_difficultyCombo, store.property(QStringLiteral("difficulty")),
                 QStringLiteral("normal"));

    const QString pvp = store.property(QStringLiteral("pvp"), QStringLiteral("true"));
    m_pvpCheck->setChecked(pvp.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
    const QString flight = store.property(QStringLiteral("allow-flight"), QStringLiteral("false"));
    m_flightCheck->setChecked(flight.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);

    m_spawnProtectSpin->setValue(
        qBound(0, store.property(QStringLiteral("spawn-protection"),
                                 QStringLiteral("0")).toInt(), 64));
    m_viewDistanceSpin->setValue(
        qBound(3, store.property(QStringLiteral("view-distance"),
                                 QStringLiteral("10")).toInt(), 32));

    m_syncingAdmin = previousSync;
}

void InstanceServerPage::updateAdminHints()
{
    if (!m_whitelistHint || !m_opHint)
        return;

    if (m_onlineModeCheck && m_onlineModeCheck->isChecked()) {
        m_whitelistHint->setText(
            tr("这台服开着正版验证：名单按正版账号记。服务器没开的时候加不了人，"
               "先开服再加（也可以直接在控制台敲 whitelist add）。"));
        m_opHint->setText(
            tr("开着正版验证时，停服状态下加不了管理员：先把服务器开起来，"
               "运行中加的会由服务端按正版账号记。"));
    } else {
        m_whitelistHint->setText(
            tr("离线模式按玩家名记名单：填游戏里用的名字就行，停服时也能加；"
               "开着服加会由服务器写完名单，UUID 更准。"));
        m_opHint->setText(
            tr("离线模式按玩家名记管理员，停服时也能加。"
               "4 级能管所有事；3 级能封人、踢人；2 级能用常用指令；1 级只能绕过出生点保护。"));
    }
}

QWidget *InstanceServerPage::buildConsoleCard()
{
    auto *card = new QFrame(this);
    card->setObjectName("serverCard");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *headRow = new QHBoxLayout();
    headRow->setSpacing(6);
    auto *title = new QLabel(tr("服务器控制台"), card);
    title->setObjectName("serverCardTitle");
    m_copyLogBtn = new QPushButton(tr("复制"), card);
    m_copyLogBtn->setObjectName("serverGhostBtn");
    m_copyLogBtn->setCursor(Qt::PointingHandCursor);
    m_copyLogBtn->setToolTip(tr("复制控制台里的全部日志"));
    m_openDirBtn = new QPushButton(tr("打开目录"), card);
    m_openDirBtn->setObjectName("serverGhostBtn");
    m_openDirBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn = new QPushButton(tr("清空"), card);
    m_clearBtn->setObjectName("serverGhostBtn");
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    headRow->addWidget(title, 1);
    headRow->addWidget(m_copyLogBtn);
    headRow->addWidget(m_openDirBtn);
    headRow->addWidget(m_clearBtn);
    layout->addLayout(headRow);

    m_console = new QPlainTextEdit(card);
    m_console->setObjectName("serverConsole");
    m_console->setReadOnly(true);
    m_console->setMinimumHeight(132);
    m_console->setMaximumBlockCount(800);
    QFont consoleFont(QStringLiteral("Consolas"));
    consoleFont.setStyleHint(QFont::Monospace);
    consoleFont.setPointSizeF(9.0);
    m_console->setFont(consoleFont);
    m_console->setPlaceholderText(tr("点「一键开服」后，服务端日志会显示在这里。"));
    layout->addWidget(m_console);

    // ── 指令输入行：等同在服务端窗口里打字 ──
    auto *cmdRow = new QHBoxLayout();
    cmdRow->setSpacing(6);
    m_commandEdit = new QLineEdit(card);
    m_commandEdit->setObjectName("serverCommandEdit");
    m_commandEdit->setPlaceholderText(tr("输入服务端指令，如 op 玩家名、whitelist add 玩家名"));
    m_commandEdit->setToolTip(tr("支持 op / deop / whitelist / ban / say / time set 等服务端指令"));
    m_commandEdit->installEventFilter(this);
    connect(m_commandEdit, &QLineEdit::returnPressed, this,
            &InstanceServerPage::onCommandEntered);

    m_commandSendBtn = new QPushButton(tr("发送"), card);
    m_commandSendBtn->setObjectName("serverGhostBtn");
    m_commandSendBtn->setCursor(Qt::PointingHandCursor);
    connect(m_commandSendBtn, &QPushButton::clicked, this, &InstanceServerPage::onCommandEntered);

    cmdRow->addWidget(m_commandEdit, 1);
    cmdRow->addWidget(m_commandSendBtn);
    layout->addLayout(cmdRow);

    connect(m_copyLogBtn, &QPushButton::clicked, this, &InstanceServerPage::onCopyConsoleClicked);
    connect(m_openDirBtn, &QPushButton::clicked, this, &InstanceServerPage::onOpenServerDirClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &InstanceServerPage::onClearConsoleClicked);
    return card;
}

void InstanceServerPage::connectManager()
{
    connect(m_manager, &LocalServerManager::stateChanged, this,
            [this](LocalServerManager::State state) {
        if (state != LocalServerManager::State::Running) {
            m_pingTimer->stop();
            m_onlineText.clear();
        }
        refreshStateVisuals();
        if (state == LocalServerManager::State::Idle) {
            // 服务端退出时可能刚写过白名单 / OP，重新对一遍磁盘
            reloadAdminData();
            emit serverStopped();
        }
    });

    connect(m_manager, &LocalServerManager::logLine, this, &InstanceServerPage::appendConsole);

    connect(m_manager, &LocalServerManager::progressChanged, this,
            [this](const QString &text, int percent) {
        if (!text.isEmpty())
            m_progressHint = text;
        if (percent >= 0) {
            m_progressBar->setRange(0, 100);
            m_progressBar->setValue(qBound(0, percent, 100));
        } else if (m_manager->state() == LocalServerManager::State::Preparing) {
            m_progressBar->setRange(0, 0);   // 不确定进度：滚动条
        }
        m_progressBar->setVisible(m_manager->state() == LocalServerManager::State::Preparing);
        if (m_manager->state() == LocalServerManager::State::Preparing) {
            setStatusDot(QStringLiteral("#e0a33f"));
            m_statusText->setText(m_progressHint.isEmpty() ? tr("正在准备服务端…")
                                                           : m_progressHint);
        }
    });

    connect(m_manager, &LocalServerManager::ready, this, [this](const QString &address) {
        m_addressLabel->setText(address);
        appendConsole(tr("服务器已就绪，本机地址：%1").arg(address));
        // 顺手写进游戏服务器列表，进游戏打开「多人游戏」就能直接点
        if (writeServerListEntry(true))
            appendConsole(tr("已把地址写进游戏服务器列表（多人游戏里能看到这台服）"));
        m_pingTimer->start();
        QTimer::singleShot(1200, this, &InstanceServerPage::refreshOnlineInfo);
        refreshStateVisuals();
        NotificationManager::showSuccess(this, tr("服务器已开好：%1").arg(address));
        emit serverStarted(address);
    });

    connect(m_manager, &LocalServerManager::failed, this, [this](const QString &reason) {
        appendConsole(tr("开服失败：%1").arg(reason));
        refreshStateVisuals();
        NotificationManager::showError(this, reason);
    });

    connect(m_manager, &LocalServerManager::crashed, this, [this](const QString &reason) {
        appendConsole(reason);
        refreshStateVisuals();
        NotificationManager::showError(this, reason);
    });
}

void InstanceServerPage::connectOnlineProbe()
{
    connect(m_pinger, &McServerPinger::pingFinished, this,
            [this](const QString &key, const McServerPinger::Result &result) {
        if (key != QStringLiteral("local_server") || !m_manager->isRunning())
            return;
        if (result.state == McServerPinger::Result::Online) {
            m_onlineText = result.playersMax > 0
                ? tr("在线 %1/%2 人 · 延迟 %3 ms")
                      .arg(result.playersOnline)
                      .arg(result.playersMax)
                      .arg(result.latencyMs)
                : tr("在线 %1 人 · 延迟 %2 ms").arg(result.playersOnline).arg(result.latencyMs);
        } else {
            m_onlineText = tr("在线人数暂时读不到（服务端可能还在生成世界）");
        }
        m_playersLabel->setText(m_onlineText);
    });
}

// ════════════════════════════════════════════════════════════════════
// 交互
// ════════════════════════════════════════════════════════════════════

void InstanceServerPage::onPrimaryClicked()
{
    if (m_manager->isRunning()) {
        m_manager->stop();
        return;
    }
    if (m_manager->isBusy())
        return;

    if (m_instancePath.isEmpty()) {
        NotificationManager::showInfo(this, tr("先在启动器里选一个实例，再回来开服。"));
        return;
    }
    if (!selectedCoreAvailable()) {
        NotificationManager::showInfo(
            this, tr("这个服务端核心不支持当前实例的版本，换一个核心再开服。"));
        return;
    }

    // 用存档开局：服务端已经有一份世界时要先问清楚要不要覆盖
    LocalServerManager::Config config = configFromUi();
    const QString worldSource = config.worldSource;
    if (!worldSource.isEmpty() && QDir(m_manager->serverWorldDir()).exists()) {
        const QString existing = m_manager->serverWorldSource();
        const bool sameSource = !existing.isEmpty()
            && QDir::cleanPath(existing).compare(QDir::cleanPath(worldSource),
                                                 Qt::CaseInsensitive) == 0;
        if (!sameSource) {
            const QString existingName = existing.isEmpty()
                ? tr("服务端自己生成的")
                : QFileInfo(existing).fileName();
            const auto answer = AppMessageBox::question(
                this, tr("覆盖服务端现有的世界？"),
                tr("服务端里已经有一份世界（%1）。用「%2」覆盖之后，服务端那边的进度会删掉；"
                   "实例里的存档不受影响。\n\n也可以先选「新建世界」，去服务端目录把 world 改名留个备份。")
                    .arg(existingName, QFileInfo(worldSource).fileName()),
                AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
            if (answer != AppMessageBox::Yes)
                return;
            QString resetError;
            if (!m_manager->resetServerWorld(&resetError)) {
                NotificationManager::showError(this, resetError);
                return;
            }
        }
    }

    m_console->clear();
    m_progressHint.clear();
    m_onlineText.clear();
    m_manager->setConfig(config);
    m_progressBar->hide();
    m_manager->start();
}

void InstanceServerPage::onCopyAddressClicked()
{
    QApplication::clipboard()->setText(displayAddress());
    NotificationManager::showSuccess(this, tr("地址已复制：%1").arg(displayAddress()));
}

void InstanceServerPage::onAddToServerListClicked()
{
    writeServerListEntry(false);
}

bool InstanceServerPage::writeServerListEntry(bool silent)
{
    if (m_instancePath.isEmpty()) {
        if (!silent)
            NotificationManager::showInfo(this, tr("先在启动器里选一个实例。"));
        return false;
    }

    const QStringList candidates = ServersDatIO::candidatePaths(m_instancePath);
    QString target = ServersDatIO::existingPath(candidates);
    if (target.isEmpty() && !candidates.isEmpty())
        target = candidates.first();
    if (target.isEmpty()) {
        if (!silent)
            NotificationManager::showError(this, tr("没找到这个实例的 servers.dat 位置。"));
        return false;
    }

    ServersDatIO::Entry entry;
    entry.name = m_motdEdit ? m_motdEdit->text().trimmed() : QString();
    if (entry.name.isEmpty())
        entry.name = QString::fromUtf8(kDefaultMotd);
    entry.host = QStringLiteral("127.0.0.1");
    entry.port = static_cast<quint16>(qBound(1024, m_manager->port(), 65535));

    const bool ok = ServersDatIO::upsert(target, entry);
    if (!silent) {
        if (ok)
            NotificationManager::showSuccess(
                this, tr("已写进游戏服务器列表，进游戏打开「多人游戏」就能看到。"));
        else
            NotificationManager::showError(this,
                                           tr("写入服务器列表失败，可能是文件被游戏占用。"));
    }
    return ok;
}

void InstanceServerPage::onOpenServerDirClicked()
{
    const QString dir = m_manager->serverDir();
    if (dir.isEmpty()) {
        NotificationManager::showInfo(this, tr("先在启动器里选一个实例。"));
        return;
    }
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void InstanceServerPage::onClearConsoleClicked()
{
    m_console->clear();
}

void InstanceServerPage::onCopyConsoleClicked()
{
    const QString text = m_console->toPlainText().trimmed();
    if (text.isEmpty()) {
        NotificationManager::showInfo(this, tr("控制台还没有内容。"));
        return;
    }
    QApplication::clipboard()->setText(text);
    NotificationManager::showSuccess(this, tr("已复制 %1 行日志").arg(m_console->blockCount()));
}

void InstanceServerPage::onCoreChanged(int index)
{
    Q_UNUSED(index);
    if (!m_uiReady)
        return;
    const auto core = static_cast<LocalServerManager::Core>(m_coreCombo->currentData().toInt());
    LocalServerManager::Config config = m_manager->config();
    config.core = core;
    m_manager->setConfig(config);
    refreshStateVisuals();
    updateCoreHint();
    saveSettings();
}

void InstanceServerPage::onWorldChanged(int index)
{
    Q_UNUSED(index);
    if (!m_uiReady)
        return;
    m_manager->setConfig(configFromUi());
    saveSettings();
}

void InstanceServerPage::updateCoreHint()
{
    const auto core = static_cast<LocalServerManager::Core>(m_coreCombo->currentData().toInt());
    const bool available = LocalServerManager::coreAvailableForVersion(core, m_instanceVersion);

    if (!available) {
        m_coreHintLabel->setText(
            tr("%1 不支持 Minecraft %2：Forge 要 1.13 之后、NeoForge 要 1.20.2 之后。")
                .arg(LocalServerManager::coreDisplayName(core),
                     m_instanceVersion.isEmpty() ? tr("这个版本") : m_instanceVersion));
        return;
    }

    switch (core) {
    case LocalServerManager::Core::Forge:
    case LocalServerManager::Core::NeoForge:
        m_coreHintLabel->setText(
            tr("第一次开服会跑官方安装器（要下载服务端和一堆库，可能等几分钟），"
               "装好后直接用实例里的 mods 目录；只装在客户端的模组可能让服务端起不来。"));
        break;
    case LocalServerManager::Core::Fabric:
        m_coreHintLabel->setText(
            tr("Fabric 服务端会直接用实例里的 mods 目录，双端模组保持一致；"
               "只装在客户端的模组可能让服务端起不来。"));
        break;
    case LocalServerManager::Core::Paper:
        m_coreHintLabel->setText(
            tr("Paper 优化了 tick 与区块加载，多人更省资源，但不加载模组。"));
        break;
    case LocalServerManager::Core::Vanilla:
    default:
        m_coreHintLabel->setText(tr("原版与 Paper 不加载模组；Paper 更省资源、适合多人。"));
        break;
    }
}

void InstanceServerPage::rebuildCoreCombo()
{
    const int previousData = m_coreCombo->currentData().toInt();
    const bool hadSelection = m_coreCombo->count() > 0;

    QSignalBlocker blocker(m_coreCombo);
    m_coreCombo->clear();
    const QVector<LocalServerManager::Core> cores = {
        LocalServerManager::Core::Vanilla, LocalServerManager::Core::Paper,
        LocalServerManager::Core::Fabric, LocalServerManager::Core::Forge,
        LocalServerManager::Core::NeoForge};
    auto *model = qobject_cast<QStandardItemModel *>(m_coreCombo->model());
    int firstAvailable = -1;
    for (const auto core : cores) {
        const bool available = LocalServerManager::coreAvailableForVersion(core, m_instanceVersion);
        m_coreCombo->addItem(LocalServerManager::coreDisplayName(core), static_cast<int>(core));
        if (available) {
            if (firstAvailable < 0)
                firstAvailable = m_coreCombo->count() - 1;
        } else if (model) {
            // 版本不支持的核心留在列表里置灰：用户能看到「为什么没得选」，
            // 而不是莫名其妙少了一项
            if (QStandardItem *item = model->item(m_coreCombo->count() - 1)) {
                item->setEnabled(false);
                item->setToolTip(tr("这个服务端核心不支持当前游戏版本"));
            }
        }
    }

    // 以管理器里当前的核心为准（它已经按加载器挑过默认值），
    // 只有它在这个版本上不可用时才退回界面原来的选择
    const auto desired = m_manager->config().core;
    int target = m_coreCombo->findData(static_cast<int>(desired));
    if (target < 0 && hadSelection)
        target = m_coreCombo->findData(previousData);
    if (target < 0)
        target = firstAvailable;
    m_coreCombo->setCurrentIndex(target >= 0 ? target : 0);

    LocalServerManager::Config config = m_manager->config();
    config.core = static_cast<LocalServerManager::Core>(m_coreCombo->currentData().toInt());
    m_manager->setConfig(config);
    updateCoreHint();
}

void InstanceServerPage::rebuildWorldCombo()
{
    const QString previous = chosenWorldSource();

    QSignalBlocker blocker(m_worldCombo);
    m_worldCombo->clear();
    m_worldCombo->addItem(tr("新建世界（服务端自己生成）"), QString());

    const QDir savesDir(QDir(m_instancePath).absoluteFilePath(QStringLiteral("saves")));
    if (savesDir.exists()) {
        const auto entries =
            savesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        for (const QFileInfo &entry : entries) {
            QString label = entry.fileName();
            const auto info = LevelDatEditor::readSave(entry.absoluteFilePath());
            if (info && !info->levelName.trimmed().isEmpty())
                label = info->levelName.trimmed();
            m_worldCombo->addItem(tr("%1（存档）").arg(label), entry.absoluteFilePath());
        }
    }

    // 恢复选择：先看界面上原来选的，再看设置里存的
    QString wanted = previous;
    if (wanted.isEmpty())
        wanted = SettingsManager::instance()->getProperty(QString::fromLatin1(kKeyWorld)).toString();
    int index = 0;
    if (!wanted.isEmpty()) {
        const int found = m_worldCombo->findData(wanted);
        if (found >= 0)
            index = found;
    }
    m_worldCombo->setCurrentIndex(index);
    m_worldCombo->setEnabled(m_worldCombo->count() > 1);
}

bool InstanceServerPage::selectedCoreAvailable() const
{
    const auto core = static_cast<LocalServerManager::Core>(m_coreCombo->currentData().toInt());
    return LocalServerManager::coreAvailableForVersion(core, m_instanceVersion);
}

QString InstanceServerPage::chosenWorldSource() const
{
    if (!m_worldCombo || m_worldCombo->currentIndex() < 0)
        return QString();
    return m_worldCombo->currentData().toString();
}

void InstanceServerPage::onSettingsChanged()
{
    if (!m_uiReady)
        return;
    m_manager->setConfig(configFromUi());
    m_addressLabel->setText(displayAddress());
    saveSettings();
    updateAdminHints();
}

void InstanceServerPage::onCommandEntered()
{
    const QString command = m_commandEdit->text().trimmed();
    if (command.isEmpty())
        return;
    if (!m_manager->isRunning()) {
        NotificationManager::showInfo(this, tr("服务器还没开起来，指令要等开服后才能发。"));
        return;
    }
    if (!m_manager->sendCommand(command)) {
        NotificationManager::showError(this, tr("指令没发出去，服务器可能刚退出。"));
        return;
    }
    rememberCommand(command);
    m_commandEdit->clear();
}

void InstanceServerPage::onJoinGameClicked()
{
    if (!m_manager->isRunning())
        return;
    emit launchGameRequested(QStringLiteral("127.0.0.1"), m_manager->port());
}

void InstanceServerPage::onTunnelClicked()
{
    emit multiplayerTabRequested();
}

void InstanceServerPage::onPublishToServerNavClicked()
{
    if (!m_manager->isRunning()) {
        NotificationManager::showInfo(this, tr("先把服务器开起来，再发布到服务器导航。"));
        return;
    }

    QString motd = m_motdEdit->text().trimmed();
    if (motd.isEmpty())
        motd = QString::fromUtf8(kDefaultMotd);
    const QString version = m_instanceVersion.isEmpty() ? tr("未知版本") : m_instanceVersion;
    const QString loader = m_instanceLoader.isEmpty() ? tr("原版") : m_instanceLoader;
    const QString core = LocalServerManager::coreDisplayName(m_manager->config().core);

    // 标题 / 正文 / 标签先按服务器信息生成好，发布页里还能改
    const QString title = tr("%1（Minecraft %2）").arg(motd, version);

    QStringList infoLines;
    infoLines << tr("- 版本：%1").arg(version);
    infoLines << tr("- 客户端：%1").arg(loader);
    infoLines << tr("- 服务端：%1").arg(core);
    infoLines << tr("- 容纳人数：%1 人").arg(m_playersSpin->value());
    infoLines << tr("- 正版验证：%1").arg(m_onlineModeCheck->isChecked()
                                             ? tr("开启")
                                             : tr("关闭（离线账号也能进）"));

    const QString content =
        tr("## 服务器信息\n%1\n\n"
           "## 怎么进服\n"
           "在游戏里打开「多人游戏 → 直接连接」，填上面那个地址就能进；"
           "用方块盒子的朋友也可以在社区「服务器导航」里点进来。\n\n"
           "想一起玩的直接加进来就行，遇到连不上之类的问题回帖说一声。")
            .arg(infoLines.join(QLatin1Char('\n')));

    QStringList tags;
    tags << tr("服务器");
    if (!m_instanceVersion.isEmpty())
        tags << m_instanceVersion;
    if (!tags.contains(loader))
        tags << loader;

    appendConsole(tr("正在打开发布页：地址先填的是本机地址，记得改成别人能连的那个再发布。"));
    emit publishToServerNavigationRequested(title, content, tags, displayAddress());
}

void InstanceServerPage::refreshOnlineInfo()
{
    if (!m_manager->isRunning()) {
        m_onlineText.clear();
        m_playersLabel->hide();
        return;
    }
    m_playersLabel->show();
    if (m_onlineText.isEmpty())
        m_playersLabel->setText(tr("正在获取在线人数…"));
    m_pinger->ping(QStringLiteral("local_server"), QStringLiteral("127.0.0.1"),
                   static_cast<quint16>(qBound(1, m_manager->port(), 65535)));
}

void InstanceServerPage::rememberCommand(const QString &command)
{
    if (!m_commandHistory.isEmpty() && m_commandHistory.last() == command) {
        m_historyCursor = m_commandHistory.size();
        return;
    }
    m_commandHistory.append(command);
    while (m_commandHistory.size() > 50)
        m_commandHistory.removeFirst();
    m_historyCursor = m_commandHistory.size();
}

bool InstanceServerPage::eventFilter(QObject *watched, QEvent *event)
{
    // 指令输入框：上下键翻历史，跟终端里一样
    if (watched == m_commandEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Up && !m_commandHistory.isEmpty()) {
            if (m_historyCursor > 0)
                --m_historyCursor;
            m_commandEdit->setText(m_commandHistory.value(m_historyCursor));
            return true;
        }
        if (keyEvent->key() == Qt::Key_Down && !m_commandHistory.isEmpty()) {
            if (m_historyCursor < m_commandHistory.size())
                ++m_historyCursor;
            m_commandEdit->setText(m_historyCursor >= m_commandHistory.size()
                                       ? QString()
                                       : m_commandHistory.value(m_historyCursor));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void InstanceServerPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 切回这个页时名单可能已经被改过（比如在游戏里用指令加人 / 服务端刚退出）
    if (m_uiReady && !m_instancePath.isEmpty())
        reloadAdminData();
}

// ════════════════════════════════════════════════════════════════════
// 状态刷新
// ════════════════════════════════════════════════════════════════════

void InstanceServerPage::refreshStateVisuals()
{
    const auto state = m_manager->state();
    const bool coreOk = selectedCoreAvailable();
    const bool hasInstance = !m_instancePath.isEmpty();

    auto setPrimary = [this](const QString &text, bool danger, bool enabled) {
        m_primaryBtn->setText(text);
        m_primaryBtn->setProperty("danger", danger);
        m_primaryBtn->setEnabled(enabled);
        m_primaryBtn->style()->unpolish(m_primaryBtn);
        m_primaryBtn->style()->polish(m_primaryBtn);
    };

    switch (state) {
    case LocalServerManager::State::Preparing:
        setStatusDot(QStringLiteral("#e0a33f"));
        m_statusText->setText(m_progressHint.isEmpty() ? tr("正在准备服务端…") : m_progressHint);
        m_hintLabel->setText(tr("正在下载服务端文件与写开服配置，第一次会慢一些。"));
        setPrimary(tr("正在开服…"), false, false);
        break;

    case LocalServerManager::State::Starting:
        setStatusDot(QStringLiteral("#e0a33f"));
        m_statusText->setText(tr("正在启动服务器…"));
        m_hintLabel->setText(tr("服务端正在生成世界，等它打出「Done」就可以进游戏了。"));
        setPrimary(tr("正在启动…"), false, false);
        break;

    case LocalServerManager::State::Running:
        setStatusDot(ThemeManager::instance()->currentThemeColor());
        m_statusText->setText(tr("服务器运行中"));
        m_hintLabel->setText(
            tr("在游戏里打开「多人游戏 → 直接连接」，输入上面的地址就能进服；"
               "下面还能直接给服务端发指令。"));
        setPrimary(tr("停止服务器"), true, true);
        break;

    case LocalServerManager::State::Stopping:
        setStatusDot(QStringLiteral("#e0a33f"));
        m_statusText->setText(tr("正在停止…"));
        m_hintLabel->setText(tr("等服务器把存档写完就会自己退出。"));
        setPrimary(tr("正在停止…"), false, false);
        break;

    case LocalServerManager::State::Idle:
    default:
        if (!coreOk) {
            setStatusDot(QStringLiteral("#e5484d"));
            m_statusText->setText(tr("这个服务端核心不支持当前版本"));
        } else {
            setStatusDot(QStringLiteral("#8a8f98"));
            m_statusText->setText(m_manager->lastError().isEmpty() ? tr("未开服")
                                                                   : tr("已停止"));
        }
        m_hintLabel->setText(
            tr("点「一键开服」会自动下载对应版本的服务端并启动，第一次要多等一会儿。"));
        setPrimary(tr("一键开服"), false, hasInstance && coreOk);
        break;
    }

    const bool running = (state == LocalServerManager::State::Running);
    const bool idle = (state == LocalServerManager::State::Idle);

    m_addressLabel->setText(running ? m_manager->address() : displayAddress());
    m_copyBtn->setEnabled(running);
    m_addToListBtn->setEnabled(hasInstance);
    // 发布到服务器导航要服务器真的在跑，不然发出去的地址是死的
    m_publishNavBtn->setEnabled(running);
    m_progressBar->setVisible(state == LocalServerManager::State::Preparing);

    // 运行中才给「进游戏 / 远程加入」和在线人数
    m_runningActions->setVisible(running);
    m_playersLabel->setVisible(running);
    if (running) {
        if (m_onlineText.isEmpty())
            m_playersLabel->setText(tr("正在获取在线人数…"));
        else
            m_playersLabel->setText(m_onlineText);
    }

    // 控制台指令只在运行时能发
    m_commandEdit->setEnabled(running);
    m_commandSendBtn->setEnabled(running);

    // 开服期间不让改设置（改了也不会生效，容易误会）
    m_coreCombo->setEnabled(idle);
    m_worldCombo->setEnabled(idle && m_worldCombo->count() > 1);
    m_motdEdit->setEnabled(idle);
    m_portSpin->setEnabled(idle);
    m_playersSpin->setEnabled(idle);
    m_memoryCombo->setEnabled(idle);
    m_onlineModeCheck->setEnabled(idle);

    // 管理卡：选好实例后随时能改；开服准备 / 停止过程中先锁住，避免和写配置打架
    const bool adminUsable = hasInstance
                             && (state == LocalServerManager::State::Idle
                                 || state == LocalServerManager::State::Running);
    if (m_whitelistToggle)
        m_whitelistToggle->setEnabled(adminUsable);
    if (m_whitelistInput) {
        m_whitelistInput->setEnabled(adminUsable);
        m_whitelistAddBtn->setEnabled(adminUsable);
    }
    if (m_opInput) {
        m_opInput->setEnabled(adminUsable);
        m_opLevelCombo->setEnabled(adminUsable);
        m_opAddBtn->setEnabled(adminUsable);
    }
    if (m_gamemodeCombo) {
        m_gamemodeCombo->setEnabled(adminUsable);
        m_difficultyCombo->setEnabled(adminUsable);
        m_pvpCheck->setEnabled(adminUsable);
        m_flightCheck->setEnabled(adminUsable);
        m_spawnProtectSpin->setEnabled(adminUsable);
        m_viewDistanceSpin->setEnabled(adminUsable);
    }
}

void InstanceServerPage::refreshInstanceHint()
{
    if (m_instancePath.isEmpty()) {
        m_instanceLabel->setText(tr("还没选到实例：先在启动器里选一个实例。"));
        return;
    }
    QString loaderText = m_instanceLoader.isEmpty() ? tr("原版") : m_instanceLoader;
    m_instanceLabel->setText(
        tr("当前实例：%1 · %2").arg(m_instanceVersion.isEmpty() ? tr("未知版本") : m_instanceVersion,
                                    loaderText));
}

void InstanceServerPage::appendConsole(const QString &line)
{
    if (!m_console || line.isEmpty())
        return;
    m_console->appendPlainText(line);
    auto *bar = m_console->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void InstanceServerPage::setStatusDot(const QString &color)
{
    m_statusDot->setStyleSheet(
        QStringLiteral("background:%1; border-radius:4px;").arg(color));
}

QString InstanceServerPage::displayAddress() const
{
    if (m_manager && m_manager->state() != LocalServerManager::State::Idle)
        return m_manager->address();
    const int port = m_portSpin ? m_portSpin->value() : 25565;
    return QStringLiteral("127.0.0.1:%1").arg(port);
}

// ════════════════════════════════════════════════════════════════════
// 实例上下文 / 设置持久化
// ════════════════════════════════════════════════════════════════════

void InstanceServerPage::setInstanceContext(const QString &path, const QString &version,
                                           const QString &loader)
{
    m_instancePath = path;
    m_instanceVersion = version;
    m_instanceLoader = loader;

    m_manager->setInstance(path, version, loader);

    if (m_uiReady) {
        LocalServerManager::Config config = m_manager->config();
        const QString lowerLoader = loader.toLower();
        // 加载器决定默认核心：Fabric / Quilt → Fabric，Forge → Forge，NeoForge → NeoForge；
        // 其它实例沿用上次选的核心（没选过就用管理器给的默认值）
        const bool mustUseFabric = lowerLoader.contains(QStringLiteral("fabric"))
                                   || lowerLoader.contains(QStringLiteral("quilt"));
        const bool mustUseNeoForge = lowerLoader.contains(QStringLiteral("neoforge"));
        const bool mustUseForge =
            !mustUseNeoForge && lowerLoader.contains(QStringLiteral("forge"));
        if (mustUseNeoForge) {
            config.core = LocalServerManager::Core::NeoForge;
        } else if (mustUseForge) {
            config.core = LocalServerManager::Core::Forge;
        } else if (mustUseFabric) {
            config.core = LocalServerManager::Core::Fabric;
        } else {
            const QString savedCore = SettingsManager::instance()
                                          ->getProperty(QString::fromLatin1(kKeyCore))
                                          .toString();
            if (!savedCore.isEmpty())
                config.core = LocalServerManager::coreFromKey(savedCore, config.core);
        }
        m_manager->setConfig(config);
    }

    rebuildCoreCombo();
    rebuildWorldCombo();
    updateCoreHint();

    refreshInstanceHint();
    refreshStateVisuals();
    // 换了实例：白名单 / OP / 规则都按新实例的服务端目录重新读
    reloadAdminData();
}

bool InstanceServerPage::serverRunning() const
{
    return m_manager && m_manager->isRunning();
}

LocalServerManager::Config InstanceServerPage::configFromUi() const
{
    LocalServerManager::Config config = m_manager->config();
    config.instancePath = m_instancePath;
    config.version = m_instanceVersion;
    config.loader = m_instanceLoader;
    config.core = static_cast<LocalServerManager::Core>(m_coreCombo->currentData().toInt());
    config.port = m_portSpin->value();
    config.maxPlayers = m_playersSpin->value();
    config.memoryMb = m_memoryCombo->currentData().toInt();
    config.onlineMode = m_onlineModeCheck->isChecked();
    config.worldSource = chosenWorldSource();

    QString motd = m_motdEdit->text().trimmed();
    if (motd.isEmpty())
        motd = QString::fromUtf8(kDefaultMotd);
    config.motd = motd;
    return config;
}

void InstanceServerPage::loadSettings()
{
    auto *settings = SettingsManager::instance();

    const int port = settings->getProperty(QString::fromLatin1(kKeyPort), 25565).toInt();
    m_portSpin->setValue(qBound(1024, port, 65535));
    const int players = settings->getProperty(QString::fromLatin1(kKeyPlayers), 10).toInt();
    m_playersSpin->setValue(qBound(1, players, 100));

    const int memory = settings->getProperty(QString::fromLatin1(kKeyMemory), 2048).toInt();
    int memoryIndex = m_memoryCombo->findData(memory);
    if (memoryIndex < 0)
        memoryIndex = m_memoryCombo->findData(2048);
    m_memoryCombo->setCurrentIndex(qMax(0, memoryIndex));

    m_onlineModeCheck->setChecked(
        settings->getProperty(QString::fromLatin1(kKeyOnlineMode), false).toBool());

    const QString motd = settings->getProperty(QString::fromLatin1(kKeyMotd)).toString();
    m_motdEdit->setText(motd.isEmpty() ? QString::fromUtf8(kDefaultMotd) : motd);

    // 核心：没存过就按实例加载器给默认值（下拉里只会有这个版本能用的核心）
    const QString savedCore = settings->getProperty(QString::fromLatin1(kKeyCore)).toString();
    LocalServerManager::Core core = LocalServerManager::defaultCoreForLoader(m_instanceLoader);
    if (!savedCore.isEmpty())
        core = LocalServerManager::coreFromKey(savedCore, core);

    rebuildCoreCombo();
    const int coreIndex = m_coreCombo->findData(static_cast<int>(core));
    if (coreIndex >= 0)
        m_coreCombo->setCurrentIndex(coreIndex);

    LocalServerManager::Config config = m_manager->config();
    config.core = core;
    config.port = m_portSpin->value();
    config.maxPlayers = m_playersSpin->value();
    config.memoryMb = m_memoryCombo->currentData().toInt();
    config.onlineMode = m_onlineModeCheck->isChecked();
    config.motd = m_motdEdit->text().trimmed();
    m_manager->setConfig(config);
}

void InstanceServerPage::saveSettings()
{
    if (!m_uiReady)
        return;
    auto *settings = SettingsManager::instance();
    settings->setProperty(QString::fromLatin1(kKeyPort), m_portSpin->value());
    settings->setProperty(QString::fromLatin1(kKeyPlayers), m_playersSpin->value());
    settings->setProperty(QString::fromLatin1(kKeyMemory), m_memoryCombo->currentData().toInt());
    settings->setProperty(QString::fromLatin1(kKeyOnlineMode), m_onlineModeCheck->isChecked());
    settings->setProperty(QString::fromLatin1(kKeyMotd), m_motdEdit->text().trimmed());
    settings->setProperty(QString::fromLatin1(kKeyWorld), chosenWorldSource());
    settings->setProperty(QString::fromLatin1(kKeyCore),
                          LocalServerManager::coreKey(m_manager->config().core));
}
