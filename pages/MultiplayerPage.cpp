/**
 * @file   MultiplayerPage.cpp
 * @brief  联机页面实现
 * @author BlockBox Team
 * @date   2026-07-07
 */

#include "MultiplayerPage.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include "components/AppInputDialog.h"
#include <QLabel>
#include <QLineEdit>
#include "components/AppMessageBox.h"
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "utils/Hongshi/HongshiClient.h"
#include "utils/IconHelper.h"
#include "utils/Terracotta/TerracottaClient.h"
#include "utils/ThemeManager.h"

MultiplayerPage::MultiplayerPage(QWidget *parent)
    : QWidget(parent)
    , m_client(TerracottaClient::instance())
    , m_hongshi(HongshiClient::instance())
    , m_stack(nullptr)
    , m_topBarContainer(nullptr)
    , m_coreCombo(nullptr)
    , m_updateBtn(nullptr)
    , m_homePage(nullptr)
    , m_statusLabel(nullptr)
    , m_descLabel(nullptr)
    , m_joinBtn(nullptr)
    , m_createBtn(nullptr)
    , m_roomPage(nullptr)
    , m_latencyLabel(nullptr)
    , m_roomCodeLabel(nullptr)
    , m_copyCodeBtn(nullptr)
    , m_leaveBtn(nullptr)
    , m_playersScroll(nullptr)
    , m_playersContainer(nullptr)
    , m_playersLayout(nullptr)
    , m_hongshiHomePage(nullptr)
    , m_hongshiStatusLabel(nullptr)
    , m_hongshiNodeCombo(nullptr)
    , m_hongshiNodeRefreshBtn(nullptr)
    , m_hongshiNodeHint(nullptr)
    , m_hongshiPortSpin(nullptr)
    , m_hongshiStartBtn(nullptr)
    , m_hongshiTunnelPage(nullptr)
    , m_hongshiLatencyLabel(nullptr)
    , m_tunnelAddrLabel(nullptr)
    , m_copyAddrBtn(nullptr)
    , m_hongshiStopBtn(nullptr)
{
    initUI();
    applyThemeStyles();

    connect(m_client, &TerracottaClient::stateChanged, this, &MultiplayerPage::refreshPage);
    connect(m_client, &TerracottaClient::profilesChanged, this, &MultiplayerPage::refreshPlayerList);
    connect(m_client, &TerracottaClient::installStatusChanged, this, &MultiplayerPage::refreshInstallStatus);
    connect(m_client, &TerracottaClient::errorOccurred, this, [this](const QString &err) {
        showInfoDialog(tr("联机错误"), err);
    });

    connect(m_hongshi, &HongshiClient::stateChanged, this, &MultiplayerPage::refreshPage);
    connect(m_hongshi, &HongshiClient::installStatusChanged, this, &MultiplayerPage::refreshInstallStatus);
    connect(m_hongshi, &HongshiClient::serverNodesChanged, this, &MultiplayerPage::populateNodeCombo);
    connect(m_hongshi, &HongshiClient::errorOccurred, this, [this](const QString &err) {
        showInfoDialog(tr("联机错误"), err);
    });

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &MultiplayerPage::applyThemeStyles);

    // 刷新定时器（用于更新联机时间）
    QTimer *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (hongshiActive()) {
            refreshTunnelStatusBar();
        } else {
            refreshStatusBar();
        }
    });
    timer->start();

    refreshPage();
    refreshInstallStatus();

    // 若已将核心默认设为红石联机（后续扩展可读配置），这里仅确保节点列表被拉取
    if (m_hongshi->serverNodes().isEmpty()) {
        m_hongshi->refreshServerNodes();
    }
}

MultiplayerPage::~MultiplayerPage() = default;

void MultiplayerPage::initUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ---- 顶部：核心选择 + 更新按钮（跨核心共享） ----
    m_topBarContainer = new QWidget(this);
    m_topBarContainer->setObjectName(QStringLiteral("multiplayerTopBar"));
    auto *topLayout = new QHBoxLayout(m_topBarContainer);
    topLayout->setContentsMargins(8, 8, 8, 0);
    topLayout->setSpacing(6);

    m_coreCombo = new QComboBox(m_topBarContainer);
    m_coreCombo->setObjectName(QStringLiteral("multiplayerCoreCombo"));
    m_coreCombo->setCursor(Qt::PointingHandCursor);
    m_coreCombo->setFixedHeight(28);
    m_coreCombo->addItem(tr("陶瓦联机"));
    m_coreCombo->addItem(tr("红石联机"));
    m_coreCombo->setToolTip(tr("选择联机核心"));

    m_updateBtn = new QPushButton(tr("更新"), m_topBarContainer);
    m_updateBtn->setObjectName(QStringLiteral("multiplayerUpdateBtn"));
    m_updateBtn->setCursor(Qt::PointingHandCursor);
    m_updateBtn->setFixedHeight(28);
    m_updateBtn->setToolTip(tr("检查并下载最新版本联机核心"));

    topLayout->addWidget(m_coreCombo, 1);
    topLayout->addWidget(m_updateBtn);
    mainLayout->addWidget(m_topBarContainer);

    // ---- 页面栈 ----
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("multiplayerStack"));

    m_homePage = new QWidget();
    m_homePage->setObjectName(QStringLiteral("multiplayerHome"));
    buildHomePage(m_homePage);
    m_stack->addWidget(m_homePage);      // 0: 陶瓦联机 首页

    m_roomPage = new QWidget();
    m_roomPage->setObjectName(QStringLiteral("multiplayerRoom"));
    buildRoomPage(m_roomPage);
    m_stack->addWidget(m_roomPage);      // 1: 陶瓦联机 房间页

    m_hongshiHomePage = new QWidget();
    m_hongshiHomePage->setObjectName(QStringLiteral("multiplayerHongshiHome"));
    buildHongshiHomePage(m_hongshiHomePage);
    m_stack->addWidget(m_hongshiHomePage); // 2: 红石联机 首页

    m_hongshiTunnelPage = new QWidget();
    m_hongshiTunnelPage->setObjectName(QStringLiteral("multiplayerHongshiTunnel"));
    buildHongshiTunnelPage(m_hongshiTunnelPage);
    m_stack->addWidget(m_hongshiTunnelPage); // 3: 红石联机 隧道页

    mainLayout->addWidget(m_stack, 1);

    connect(m_coreCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MultiplayerPage::onCoreChanged);

    m_stack->setCurrentIndex(0);
}

bool MultiplayerPage::hongshiActive() const
{
    return m_coreCombo && m_coreCombo->currentIndex() == 1;
}

// ============================
// 陶瓦联机页面
// ============================

void MultiplayerPage::buildHomePage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    // 顶部状态文字（显示当前状态/版本）
    m_statusLabel = new QLabel(page);
    m_statusLabel->setObjectName(QStringLiteral("multiplayerStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);

    // ---- 中间区域：说明信息 ----
    m_descLabel = new QLabel(page);
    m_descLabel->setObjectName(QStringLiteral("multiplayerDescLabel"));
    m_descLabel->setWordWrap(true);
    m_descLabel->setTextFormat(Qt::RichText);
    m_descLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_descLabel->setOpenExternalLinks(true);
    m_descLabel->setText(
        tr("本软件可以使用陶瓦联机，可以与支持陶瓦联机的软件"
           "（如 HMCL、PCL社区版、VersePC、FCL、ZL2）进行联机。<br><br>"
           "陶瓦联机是第三方开源软件，与我们无关，如遇到一些问题可前往"
           "<a href=\"https://github.com/burningtnt/Terracotta\">陶瓦联机GitHub仓库</a>"
           "反馈，无法访问可试着使用本软件合法合规的 GitHub 加速工具加速网络。<br><br>"
           "陶瓦联机使用 P2P 双端直连技术，无中转，最终联机体验由联机参与者有着较大的关系。<br><br>"
           "在联机过程中，请严格遵守当地法律法规。<br><br>"
           "感谢了解与配合，祝你联机愉快！")
    );
    layout->addWidget(m_descLabel, 1);

    // ---- 加入房间 / 创建房间按钮 ----
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);

    m_joinBtn = new QPushButton(tr("加入房间"), page);
    m_joinBtn->setObjectName(QStringLiteral("multiplayerJoinBtn"));
    m_joinBtn->setCursor(Qt::PointingHandCursor);
    m_joinBtn->setFixedHeight(34);

    m_createBtn = new QPushButton(tr("创建房间"), page);
    m_createBtn->setObjectName(QStringLiteral("multiplayerCreateBtn"));
    m_createBtn->setCursor(Qt::PointingHandCursor);
    m_createBtn->setFixedHeight(34);

    btnRow->addWidget(m_joinBtn, 1);
    btnRow->addWidget(m_createBtn, 1);
    layout->addLayout(btnRow);

// 信号连接
    connect(m_updateBtn, &QPushButton::clicked, this, [this]() {
        if (hongshiActive()) {
            auto st = m_hongshi->installStatus();
            if (st == HongshiClient::InstallStatus::Downloading
                || st == HongshiClient::InstallStatus::CheckingUpdate) {
                return; // 按钮已禁用，此处不应触发
            }
            if (st == HongshiClient::InstallStatus::UpToDate
                || st == HongshiClient::InstallStatus::Ready) {
                m_hongshi->checkUpdate();
                return;
            }
            // NotInstalled / UpdateAvailable / DownloadFailed → 直接下载
            m_hongshi->downloadLatest();
            return;
        }
        auto status = m_client->installStatus();
        if (status == TerracottaClient::InstallStatus::Downloading
            || status == TerracottaClient::InstallStatus::CheckingUpdate) {
            return; // 按钮已禁用，此处不应触发
        }
        if (status == TerracottaClient::InstallStatus::UpToDate) {
            // 已是最新，允许手动重新检查
            m_client->checkUpdate();
            return;
        }
        if (status == TerracottaClient::InstallStatus::Ready) {
            // 已安装但未知远程版本，先检查更新
            m_client->checkUpdate();
            return;
        }
        // NotInstalled / UpdateAvailable / DownloadFailed → 直接下载
        m_client->downloadLatest();
    });

    connect(m_joinBtn, &QPushButton::clicked, this, [this]() {
        if (m_client->installedVersion().isEmpty()) {
            showInfoDialog(tr("未安装联机核心"),
                tr("尚未安装陶瓦联机核心，请先点击「下载」按钮下载安装。"));
            return;
        }
        // Exception 状态下点击 = 重试：调用 leaveRoom 回到 Waiting
        if (m_client->state() == TerracottaClient::State::Exception) {
            m_client->leaveRoom();
            return;
        }
        // 若进程未运行（NotInstalled 状态但已安装），自动启动（无需弹窗）
        if (m_client->state() == TerracottaClient::State::NotInstalled
            && m_client->isProcessRunning() == false) {
            m_client->start();
            return;
        }
        if (m_client->state() == TerracottaClient::State::Launching) {
            showInfoDialog(tr("正在启动"),
                tr("联机核心正在启动，请稍候..."));
            return;
        }
        if (m_client->state() == TerracottaClient::State::Fatal) {
            showInfoDialog(tr("启动失败"),
                tr("联机核心启动失败，请重试或重新下载安装。"));
            return;
        }
        if (m_client->state() != TerracottaClient::State::Waiting) {
            showInfoDialog(tr("状态异常"),
                tr("当前已在房间中，请先离开当前房间。"));
            return;
        }
        // 弹出输入对话框
        bool ok = false;
        QString code = AppInputDialog::getText(
            this, tr("加入房间"), tr("请输入邀请码："),
            QLineEdit::Normal, QString(), &ok);
        if (!ok || code.trimmed().isEmpty()) return;

        if (!TerracottaClient::verifyRoomCode(code)) {
            showInfoDialog(tr("邀请码无效"),
                tr("邀请码格式错误，应为 U/XXXX-XXXX-XXXX-XXXX。"));
            return;
        }

        // 获取当前账户玩家名（简化：使用默认名）
        QString playerName = tr("BlockBox 玩家");
        if (!m_client->joinRoom(code, playerName)) {
            showInfoDialog(tr("加入失败"),
                tr("无法加入房间，请检查邀请码或当前状态。"));
        } else {
            showInfoDialog(tr("正在加入"),
                tr("正在连接房主，请稍候..."));
        }
    });

    connect(m_createBtn, &QPushButton::clicked, this, [this]() {
        if (m_client->installedVersion().isEmpty()) {
            showInfoDialog(tr("未安装联机核心"),
                tr("尚未安装陶瓦联机核心，请先点击「下载」按钮下载安装。"));
            return;
        }
        // Exception 状态下点击 = 重试：调用 leaveRoom 回到 Waiting
        if (m_client->state() == TerracottaClient::State::Exception) {
            m_client->leaveRoom();
            return;
        }
        // 若进程未运行（NotInstalled 状态但已安装），自动启动（无需弹窗）
        if (m_client->state() == TerracottaClient::State::NotInstalled
            && m_client->isProcessRunning() == false) {
            m_client->start();
            return;
        }
        if (m_client->state() == TerracottaClient::State::Launching) {
            showInfoDialog(tr("正在启动"),
                tr("联机核心正在启动，请稍候..."));
            return;
        }
        if (m_client->state() == TerracottaClient::State::Fatal) {
            showInfoDialog(tr("启动失败"),
                tr("联机核心启动失败，请重试或重新下载安装。"));
            return;
        }
        if (m_client->state() != TerracottaClient::State::Waiting) {
            showInfoDialog(tr("状态异常"),
                tr("当前已在房间中，请先离开当前房间。"));
            return;
        }
        QString playerName = tr("BlockBox 玩家");
        m_client->createRoom(playerName);
        showInfoDialog(tr("正在扫描"),
            tr("正在扫描，请进入游戏，在游戏菜单选择对局域网开放。"));
    });
}

// ============================
// 陶瓦联机：房间页
// ============================

void MultiplayerPage::buildRoomPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // ---- 顶部：延迟和联机时间 ----
    m_latencyLabel = new QLabel(page);
    m_latencyLabel->setObjectName(QStringLiteral("multiplayerLatencyLabel"));
    m_latencyLabel->setAlignment(Qt::AlignCenter);
    m_latencyLabel->setWordWrap(true);
    layout->addWidget(m_latencyLabel);

    // ---- 中间：大大的房间邀请码 + 复制按钮 ----
    auto *codeLayout = new QHBoxLayout();
    codeLayout->setSpacing(6);

    m_roomCodeLabel = new QLabel(page);
    m_roomCodeLabel->setObjectName(QStringLiteral("multiplayerRoomCodeLabel"));
    m_roomCodeLabel->setAlignment(Qt::AlignCenter);
    m_roomCodeLabel->setWordWrap(true);
    m_roomCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QFont codeFont = m_roomCodeLabel->font();
    codeFont.setBold(true);
    codeFont.setPointSize(12);
    m_roomCodeLabel->setFont(codeFont);

    m_copyCodeBtn = new QPushButton(page);
    m_copyCodeBtn->setObjectName(QStringLiteral("multiplayerCopyCodeBtn"));
    m_copyCodeBtn->setCursor(Qt::PointingHandCursor);
    m_copyCodeBtn->setFixedSize(28, 28);
    m_copyCodeBtn->setToolTip(tr("复制邀请码"));
    m_copyCodeBtn->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/copy.svg"),
        ThemeManager::instance()->currentThemeColor(), 16));
    m_copyCodeBtn->setIconSize(QSize(16, 16));

    codeLayout->addStretch(1);
    codeLayout->addWidget(m_roomCodeLabel, 4);
    codeLayout->addWidget(m_copyCodeBtn);
    codeLayout->addStretch(1);
    layout->addLayout(codeLayout);

    // ---- 玩家列表区域 ----
    m_playersScroll = new QScrollArea(page);
    m_playersScroll->setObjectName(QStringLiteral("multiplayerPlayersScroll"));
    m_playersScroll->setWidgetResizable(true);
    m_playersScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_playersScroll->setFrameShape(QFrame::NoFrame);

    m_playersContainer = new QWidget();
    m_playersContainer->setObjectName(QStringLiteral("multiplayerPlayersContainer"));
    m_playersLayout = new QVBoxLayout(m_playersContainer);
    m_playersLayout->setContentsMargins(0, 0, 0, 0);
    m_playersLayout->setSpacing(4);
    m_playersLayout->addStretch();

    m_playersScroll->setWidget(m_playersContainer);
    layout->addWidget(m_playersScroll, 1);

    // ---- 离开房间按钮 ----
    m_leaveBtn = new QPushButton(tr("离开房间"), page);
    m_leaveBtn->setObjectName(QStringLiteral("multiplayerLeaveBtn"));
    m_leaveBtn->setCursor(Qt::PointingHandCursor);
    m_leaveBtn->setFixedHeight(32);
    layout->addWidget(m_leaveBtn);

    connect(m_copyCodeBtn, &QPushButton::clicked, this, [this]() {
        QString code = m_client->roomCode();
        if (code.isEmpty()) return;
        QApplication::clipboard()->setText(code);
        m_copyCodeBtn->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/copy.svg"),
            QColor(76, 175, 80), 16));
        QTimer::singleShot(800, this, [this]() {
            m_copyCodeBtn->setIcon(IconHelper::loadColoredIcon(
                QStringLiteral(":/Images/Icons/copy.svg"),
                ThemeManager::instance()->currentThemeColor(), 16));
        });
    });

    connect(m_leaveBtn, &QPushButton::clicked, this, [this]() {
        m_client->leaveRoom();
    });
}

void MultiplayerPage::buildHongshiHomePage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    // 顶部状态文字
    m_hongshiStatusLabel = new QLabel(page);
    m_hongshiStatusLabel->setObjectName(QStringLiteral("multiplayerStatusLabel"));
    m_hongshiStatusLabel->setWordWrap(true);
    m_hongshiStatusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_hongshiStatusLabel);

    // 中转服务器节点
    auto *nodeHeaderRow = new QHBoxLayout();
    nodeHeaderRow->setSpacing(6);

    auto *nodeLabel = new QLabel(tr("中转服务器节点"), page);
    nodeLabel->setObjectName(QStringLiteral("hongshiFieldLabel"));
    nodeHeaderRow->addWidget(nodeLabel, 1);

    m_hongshiNodeRefreshBtn = new QPushButton(tr("刷新"), page);
    m_hongshiNodeRefreshBtn->setObjectName(QStringLiteral("hongshiNodeRefreshBtn"));
    m_hongshiNodeRefreshBtn->setCursor(Qt::PointingHandCursor);
    m_hongshiNodeRefreshBtn->setFixedHeight(26);
    nodeHeaderRow->addWidget(m_hongshiNodeRefreshBtn);
    layout->addLayout(nodeHeaderRow);

    m_hongshiNodeCombo = new QComboBox(page);
    m_hongshiNodeCombo->setObjectName(QStringLiteral("hongshiNodeCombo"));
    m_hongshiNodeCombo->setCursor(Qt::PointingHandCursor);
    m_hongshiNodeCombo->addItem(tr("加载中..."));
    layout->addWidget(m_hongshiNodeCombo);

    m_hongshiNodeHint = new QLabel(page);
    m_hongshiNodeHint->setObjectName(QStringLiteral("hongshiFieldHint"));
    m_hongshiNodeHint->setWordWrap(true);
    m_hongshiNodeHint->setText(tr("正在从主站获取节点列表，失败自动回退国内镜像..."));
    layout->addWidget(m_hongshiNodeHint);

    // 本地 Minecraft 端口
    auto *portLabel = new QLabel(tr("本地 Minecraft 端口"), page);
    portLabel->setObjectName(QStringLiteral("hongshiFieldLabel"));
    layout->addWidget(portLabel);

    m_hongshiPortSpin = new QSpinBox(page);
    m_hongshiPortSpin->setObjectName(QStringLiteral("hongshiPortSpin"));
    m_hongshiPortSpin->setRange(1, 65535);
    m_hongshiPortSpin->setValue(25565);
    m_hongshiPortSpin->setFixedHeight(34);
    layout->addWidget(m_hongshiPortSpin);

    auto *portHint = new QLabel(tr("请先启动游戏并选择「对局域网开放」，然后填入游戏显示的端口。"), page);
    portHint->setObjectName(QStringLiteral("hongshiFieldHint"));
    portHint->setWordWrap(true);
    layout->addWidget(portHint);

    // 启动内核按钮
    m_hongshiStartBtn = new QPushButton(tr("启动内核"), page);
    m_hongshiStartBtn->setObjectName(QStringLiteral("hongshiStartBtn"));
    m_hongshiStartBtn->setCursor(Qt::PointingHandCursor);
    m_hongshiStartBtn->setFixedHeight(34);
    layout->addWidget(m_hongshiStartBtn);

    // 说明文字
    auto *desc = new QLabel(page);
    desc->setObjectName(QStringLiteral("multiplayerDescLabel"));
    desc->setWordWrap(true);
    desc->setTextFormat(Qt::RichText);
    desc->setTextInteractionFlags(Qt::TextBrowserInteraction);
    desc->setOpenExternalLinks(true);
    desc->setText(
        tr("红石联机是第三方开源联机服务，与本软件无关。选择中转节点并填入本地端口后，"
           "点击启动即可获得一个联机地址，将地址分享给好友，参与者在 Minecraft 中连接该地址"
           "即可共同游玩。<br><br>"
           "参考项目：<a href=\"https://github.com/hongshionline/hongshi-shell\">红石联机开源外壳</a><br><br>"
           "在联机过程中，请严格遵守当地法律法规。")
    );
    layout->addWidget(desc, 1);

    // 信号连接
    connect(m_hongshiNodeRefreshBtn, &QPushButton::clicked, this, [this]() {
        m_hongshi->refreshServerNodes();
    });

    connect(m_hongshiStartBtn, &QPushButton::clicked, this, [this]() {
        // 已建立连接时点击 = 停止
        if (m_hongshi->isTunnelActive()) {
            m_hongshi->stopKernel();
            return;
        }
        if (!m_hongshi->isKernelPresent()) {
            showInfoDialog(tr("未安装联机核心"),
                tr("尚未安装红石联机核心，请先点击「更新」按钮下载安装。"));
            return;
        }
        if (m_hongshi->isProcessRunning()) {
            showInfoDialog(tr("正在启动"),
                tr("红石联机内核正在启动，请稍候..."));
            return;
        }
        QString server = m_hongshiNodeCombo->currentData().toString();
        if (server.isEmpty()) {
            showInfoDialog(tr("未选择节点"),
                tr("请先选择一个中转服务器节点。"));
            return;
        }
        m_hongshi->startKernel(server, quint16(m_hongshiPortSpin->value()));
    });
}

void MultiplayerPage::buildHongshiTunnelPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // ---- 顶部：隧道状态与联机时间 ----
    m_hongshiLatencyLabel = new QLabel(page);
    m_hongshiLatencyLabel->setObjectName(QStringLiteral("multiplayerLatencyLabel"));
    m_hongshiLatencyLabel->setAlignment(Qt::AlignCenter);
    m_hongshiLatencyLabel->setWordWrap(true);
    layout->addWidget(m_hongshiLatencyLabel);

    // ---- 中间：大大的隧道地址 + 复制按钮 ----
    auto *addrLayout = new QHBoxLayout();
    addrLayout->setSpacing(6);

    m_tunnelAddrLabel = new QLabel(page);
    m_tunnelAddrLabel->setObjectName(QStringLiteral("multiplayerRoomCodeLabel"));
    m_tunnelAddrLabel->setAlignment(Qt::AlignCenter);
    m_tunnelAddrLabel->setWordWrap(true);
    m_tunnelAddrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_tunnelAddrLabel->setText(tr("正在建立隧道..."));

    QFont addrFont = m_tunnelAddrLabel->font();
    addrFont.setBold(true);
    addrFont.setPointSize(12);
    m_tunnelAddrLabel->setFont(addrFont);

    m_copyAddrBtn = new QPushButton(page);
    m_copyAddrBtn->setObjectName(QStringLiteral("multiplayerCopyCodeBtn"));
    m_copyAddrBtn->setCursor(Qt::PointingHandCursor);
    m_copyAddrBtn->setFixedSize(28, 28);
    m_copyAddrBtn->setToolTip(tr("复制联机地址"));
    m_copyAddrBtn->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/copy.svg"),
        ThemeManager::instance()->currentThemeColor(), 16));
    m_copyAddrBtn->setIconSize(QSize(16, 16));

    addrLayout->addStretch(1);
    addrLayout->addWidget(m_tunnelAddrLabel, 4);
    addrLayout->addWidget(m_copyAddrBtn);
    addrLayout->addStretch(1);
    layout->addLayout(addrLayout);

    auto *hint = new QLabel(tr("在 Minecraft 多人游戏中连接上方地址即可联机，请将地址分享给好友。"), page);
    hint->setObjectName(QStringLiteral("hongshiFieldHint"));
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);

    layout->addStretch(1);

    // ---- 停止联机按钮 ----
    m_hongshiStopBtn = new QPushButton(tr("停止联机"), page);
    m_hongshiStopBtn->setObjectName(QStringLiteral("multiplayerLeaveBtn"));
    m_hongshiStopBtn->setCursor(Qt::PointingHandCursor);
    m_hongshiStopBtn->setFixedHeight(32);
    layout->addWidget(m_hongshiStopBtn);

    connect(m_copyAddrBtn, &QPushButton::clicked, this, [this]() {
        QString addr = m_hongshi->tunnelAddress();
        if (addr.isEmpty()) return;
        QApplication::clipboard()->setText(addr);
        m_copyAddrBtn->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/copy.svg"),
            QColor(76, 175, 80), 16));
        QTimer::singleShot(800, this, [this]() {
            m_copyAddrBtn->setIcon(IconHelper::loadColoredIcon(
                QStringLiteral(":/Images/Icons/copy.svg"),
                ThemeManager::instance()->currentThemeColor(), 16));
        });
    });

    connect(m_hongshiStopBtn, &QPushButton::clicked, this, [this]() {
        m_hongshi->stopKernel();
    });
}

void MultiplayerPage::applyThemeStyles()
{
QString themeColor = ThemeManager::instance()->currentThemeColor();
    QString textColor = ThemeManager::instance()->currentTextColor();
    QString borderColor = ThemeManager::instance()->currentBorderColor();
    QColor tc(themeColor);
    QString hoverBg = QString("rgba(%1, %2, %3, 0.12)")
        .arg(tc.red()).arg(tc.green()).arg(tc.blue());
    const bool isMpDark = (ThemeManager::instance()->currentTheme()
                           == ThemeManager::DarkTheme);
    const QString mpDisabledBg = isMpDark ? "#3a3a3a" : "#f5f5f5";
    const QString mpDisabledText = isMpDark ? "#8a8a8a" : "#999999";

    // 顶部核心选择栏
    if (m_topBarContainer) {
        m_topBarContainer->setStyleSheet(
            QString(
                "QComboBox#multiplayerCoreCombo {"
                "  background: palette(base);"
                "  border: 1px solid %1;"
                "  border-radius: 4px;"
                "  color: %2;"
                "  font-size: 12px;"
                "  padding: 0 6px;"
                "}"
                "QComboBox#multiplayerCoreCombo:hover {"
                "  border-color: %3;"
                "}"
                "QComboBox#multiplayerCoreCombo::drop-down {"
                "  border: none;"
                "  width: 22px;"
                "}"
                "QComboBox#multiplayerCoreCombo::down-arrow {"
                "  image: none;"
                "  border-left: 4px solid transparent;"
                "  border-right: 4px solid transparent;"
                "  border-top: 5px solid %2;"
                "}"
                "QComboBox#multiplayerCoreCombo QAbstractItemView {"
                "  background: palette(base);"
                "  color: %2;"
                "  border: 1px solid %1;"
                "  selection-background-color: %4;"
                "  selection-color: %3;"
                "}"
                "QPushButton#multiplayerUpdateBtn {"
                "  background: palette(base);"
                "  border: 1px solid %1;"
                "  border-radius: 4px;"
                "  color: %2;"
                "  font-size: 12px;"
                "  padding: 0 10px;"
                "}"
                "QPushButton#multiplayerUpdateBtn:hover {"
                "  background: %4;"
                "  border-color: %3;"
                "  color: %3;"
                "}"
"QPushButton#multiplayerUpdateBtn:disabled {"
                "  color: %5;"
                "  background: %6;"
                "}"
            ).arg(borderColor, textColor, themeColor, hoverBg,
                  mpDisabledText, mpDisabledBg));
    }

    // 陶瓦联机首页
    if (m_homePage) {
        m_homePage->setStyleSheet(
            QString(
                "QLabel#multiplayerStatusLabel {"
                "  color: %1;"
                "  font-size: 11px;"
                "  padding: 4px;"
                "  background: transparent;"
                "}"
                "QLabel#multiplayerDescLabel {"
                "  color: %1;"
                "  font-size: 12px;"
                "  padding: 8px;"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "}"
                "QPushButton#multiplayerJoinBtn, QPushButton#multiplayerCreateBtn {"
                "  background: %3;"
                "  color: white;"
                "  border: none;"
                "  border-radius: 6px;"
                "  font-size: 13px;"
                "  font-weight: 500;"
                "}"
                "QPushButton#multiplayerJoinBtn:hover, QPushButton#multiplayerCreateBtn:hover {"
                "  background: %4;"
                "}"
"QPushButton#multiplayerJoinBtn:disabled, QPushButton#multiplayerCreateBtn:disabled {"
                "  background: %5;"
                "  color: %6;"
                "}"
            ).arg(textColor, borderColor, themeColor, tc.darker(110).name(),
                  mpDisabledBg, mpDisabledText));
    }

    // 房间页样式
    if (m_roomPage) {
        m_roomPage->setStyleSheet(
            QString(
                "QLabel#multiplayerRoomCodeLabel {"
                "  color: %3;"
                "  font-size: 14px;"
                "  font-weight: bold;"
                "  padding: 12px 8px;"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "}"
                "QPushButton#multiplayerCopyCodeBtn {"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 4px;"
                "}"
                "QPushButton#multiplayerCopyCodeBtn:hover {"
                "  background: %4;"
                "  border-color: %3;"
                "}"
                "QPushButton#multiplayerLeaveBtn {"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "  color: #d32f2f;"
                "  font-size: 12px;"
                "}"
                "QPushButton#multiplayerLeaveBtn:hover {"
                "  background: rgba(211, 47, 47, 0.12);"
                "  border-color: #d32f2f;"
                "}"
                "QScrollArea#multiplayerPlayersScroll {"
                "  background: transparent;"
                "  border: none;"
                "}"
                "QWidget#multiplayerPlayersContainer {"
                "  background: transparent;"
                "}"
            ).arg(textColor, borderColor, themeColor, hoverBg));
    }

    // 红石联机首页
    if (m_hongshiHomePage) {
        m_hongshiHomePage->setStyleSheet(
            QString(
                "QLabel#multiplayerStatusLabel {"
                "  color: %1;"
                "  font-size: 11px;"
                "  padding: 4px;"
                "  background: transparent;"
                "}"
                "QLabel#multiplayerDescLabel {"
                "  color: %1;"
                "  font-size: 12px;"
                "  padding: 8px;"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "}"
                "QLabel#hongshiFieldLabel {"
                "  color: %1;"
                "  font-size: 12px;"
                "  font-weight: 600;"
                "  background: transparent;"
                "  border: none;"
                "}"
                "QLabel#hongshiFieldHint {"
                "  color: %1;"
                "  font-size: 11px;"
                "  background: transparent;"
                "  border: none;"
                "}"
                "QComboBox#hongshiNodeCombo {"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "  color: %1;"
                "  font-size: 12px;"
                "  padding: 4px 8px;"
                "}"
                "QComboBox#hongshiNodeCombo:hover {"
                "  border-color: %3;"
                "}"
                "QComboBox#hongshiNodeCombo QAbstractItemView {"
                "  background: palette(base);"
                "  color: %1;"
                "  border: 1px solid %2;"
                "  selection-background-color: %4;"
                "  selection-color: %3;"
                "}"
                "QPushButton#hongshiNodeRefreshBtn {"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "  color: %1;"
                "  font-size: 11px;"
                "  padding: 0 10px;"
                "}"
                "QPushButton#hongshiNodeRefreshBtn:hover {"
                "  border-color: %3;"
                "  color: %3;"
                "}"
                "QSpinBox#hongshiPortSpin {"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "  color: %1;"
                "  font-size: 12px;"
                "  padding: 2px 6px;"
                "}"
                "QPushButton#hongshiStartBtn {"
                "  background: %3;"
                "  color: white;"
                "  border: none;"
                "  border-radius: 6px;"
                "  font-size: 13px;"
                "  font-weight: 500;"
                "}"
                "QPushButton#hongshiStartBtn:hover {"
                "  background: %5;"
                "}"
"QPushButton#hongshiStartBtn:disabled {"
                "  background: %6;"
                "  color: %7;"
                "}"
            ).arg(textColor, borderColor, themeColor, hoverBg,
                  tc.darker(110).name(), mpDisabledBg, mpDisabledText));
    }

    // 红石联机隧道页
    if (m_hongshiTunnelPage) {
        m_hongshiTunnelPage->setStyleSheet(
            QString(
                "QLabel#multiplayerRoomCodeLabel {"
                "  color: %3;"
                "  font-size: 16px;"
                "  font-weight: bold;"
                "  padding: 12px 8px;"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "}"
                "QPushButton#multiplayerCopyCodeBtn {"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 4px;"
                "}"
                "QPushButton#multiplayerCopyCodeBtn:hover {"
                "  background: %4;"
                "  border-color: %3;"
                "}"
                "QPushButton#multiplayerLeaveBtn {"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "  color: #d32f2f;"
                "  font-size: 12px;"
                "}"
                "QPushButton#multiplayerLeaveBtn:hover {"
                "  background: rgba(211, 47, 47, 0.12);"
                "  border-color: #d32f2f;"
                "}"
                "QLabel#hongshiFieldHint {"
                "  color: %1;"
                "  font-size: 11px;"
                "  background: transparent;"
                "  border: none;"
                "}"
            ).arg(textColor, borderColor, themeColor, hoverBg));
    }

    // 玩家卡片样式（与模组卡片一致的列表项样式）
    QString playerCardStyle = QString(
        "QWidget[cardRole=\"mpContainer\"] {"
        "  background: palette(base);"
        "  border: 1px solid %1;"
        "  border-radius: 6px;"
        "}"
        "QWidget[cardRole=\"mpContainer\"]:hover {"
        "  border-color: %2;"
        "}"
        "QLabel[cardRole=\"mpName\"] {"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  color: %3;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel[cardRole=\"mpChip\"] {"
        "  background-color: rgba(%4, %5, %6, 26);"
        "  color: %2;"
        "  border: 1px solid %1;"
        "  border-radius: 8px;"
        "  padding: 2px 6px;"
        "  font-size: 10px;"
        "}"
        "QPushButton[cardRole=\"mpActionBtn\"] {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QPushButton[cardRole=\"mpActionBtn\"]:hover {"
        "  background: rgba(%4, %5, %6, 26);"
        "}"
    ).arg(borderColor, themeColor, textColor)
     .arg(tc.red()).arg(tc.green()).arg(tc.blue());

    if (m_playersContainer) {
        m_playersContainer->setStyleSheet(playerCardStyle);
    }
}
void MultiplayerPage::refreshPage()
{
    // 红石联机核心：走独立刷新逻辑
    if (hongshiActive()) {
        refreshHongshiPage();
        return;
    }

    auto state = m_client->state();
    bool inRoom = m_client->isInRoom();

    // 切换页面
    if (inRoom) {
        if (m_stack->currentIndex() != 1) {
            m_stack->setCurrentIndex(1);
            m_roomEnterTime = QDateTime::currentDateTime();
        }
    } else {
        if (m_stack->currentIndex() != 0) {
            m_stack->setCurrentIndex(0);
        }
    }

    // 更新首页状态文字
    if (m_statusLabel) {
        QString text;
        switch (state) {
        case TerracottaClient::State::NotInstalled:
            if (m_client->installedVersion().isEmpty()) {
                text = tr("未安装联机核心");
            } else {
                text = tr("联机核心未运行（已安装 v%1）").arg(m_client->installedVersion());
            }
            break;
        case TerracottaClient::State::Launching:
            text = tr("正在启动联机核心...");
            break;
        case TerracottaClient::State::Waiting:
            if (m_client->isProcessRunning()) {
                text = m_client->terracottaVersion().isEmpty()
                    ? tr("联机核心已就绪")
                    : tr("联机核心已就绪（v%1）").arg(m_client->terracottaVersion());
            } else {
                text = tr("联机核心未运行");
            }
            break;
        case TerracottaClient::State::HostScanning:
            text = tr("正在扫描本地 Minecraft 端口...");
            break;
        case TerracottaClient::State::HostStarting:
            text = tr("正在创建房间...");
            break;
        case TerracottaClient::State::HostOk:
            text = tr("房间已就绪");
            break;
        case TerracottaClient::State::GuestConnecting:
            text = tr("正在连接房主...");
            break;
        case TerracottaClient::State::GuestStarting:
            text = tr("正在建立端口转发...");
            break;
        case TerracottaClient::State::GuestOk:
            text = tr("已连接到房间");
            break;
        case TerracottaClient::State::Exception:
            switch (m_client->exceptionType()) {
            case TerracottaClient::ExceptionType::PingHostFail:
                text = tr("联机异常：无法连接房主，请重试");
                break;
            case TerracottaClient::ExceptionType::PingHostRst:
                text = tr("联机异常：连接被房主重置，请重试");
                break;
            case TerracottaClient::ExceptionType::GuestEasytierCrash:
                text = tr("联机异常：房客网络组件崩溃，请重试");
                break;
            case TerracottaClient::ExceptionType::HostEasytierCrash:
                text = tr("联机异常：房主网络组件崩溃，请重试");
                break;
            case TerracottaClient::ExceptionType::PingServerRst:
                text = tr("联机异常：Minecraft 服务器无响应，请重试");
                break;
            case TerracottaClient::ExceptionType::ScaffoldingInvalidResponse:
                text = tr("联机异常：联机协议响应无效，请重试");
                break;
            }
            break;
        case TerracottaClient::State::Fatal:
            text = tr("联机核心启动失败");
            break;
        }
        m_statusLabel->setText(text);
    }

    // 更新首页按钮可用性
    if (m_joinBtn && m_createBtn) {
        bool canAct = (state == TerracottaClient::State::Waiting)
            || (state == TerracottaClient::State::NotInstalled
                && !m_client->installedVersion().isEmpty())
            || (state == TerracottaClient::State::Exception
                && m_client->isProcessRunning());
        m_joinBtn->setEnabled(canAct);
        m_createBtn->setEnabled(canAct);

        if (state == TerracottaClient::State::Exception) {
            m_createBtn->setText(tr("重试"));
            m_joinBtn->setText(tr("重试"));
        } else {
            m_createBtn->setText(tr("创建房间"));
            m_joinBtn->setText(tr("加入房间"));
        }
    }

    // 更新房间页
    if (m_roomCodeLabel) {
        QString code = m_client->roomCode();
        if (code.isEmpty()) {
            m_roomCodeLabel->setText(tr("正在生成邀请码..."));
        } else {
            m_roomCodeLabel->setText(code);
        }
    }

    refreshStatusBar();
    refreshPlayerList();
}

void MultiplayerPage::refreshHongshiPage()
{
    auto *hs = m_hongshi;
    bool active = hs->isTunnelActive();
    bool open = (hs->state() == HongshiClient::State::Open);

    // 页面切换：active → 隧道页(index 3)，否则首页(index 2)
    int target = active ? 3 : 2;
    if (m_stack->currentIndex() != target) {
        m_stack->setCurrentIndex(target);
        if (target == 3) {
            m_hongshiEnterTime = QDateTime::currentDateTime();
        }
    }

    // 首页状态文字与按钮
    if (m_hongshiStatusLabel) {
        QString text;
        switch (hs->installStatus()) {
        case HongshiClient::InstallStatus::NotInstalled:
            text = (hs->isKernelPresent() && hs->installedVersion().isEmpty())
                ? tr("红石联机内核已安装（版本未知）")
                : tr("未安装红石联机核心");
            break;
        case HongshiClient::InstallStatus::Ready:
            if (hs->isProcessRunning()) {
                text = tr("红石联机内核运行中...");
            } else if (hs->installedVersion().isEmpty()) {
                text = tr("红石联机内核已就绪");
            } else {
                text = tr("红石联机内核已就绪（v%1）").arg(hs->installedVersion());
            }
            break;
        case HongshiClient::InstallStatus::CheckingUpdate:
            text = tr("正在检查红石联机更新...");
            break;
        case HongshiClient::InstallStatus::UpdateAvailable:
            text = tr("红石联机有新版 v%1 可更新").arg(hs->latestVersion());
            break;
        case HongshiClient::InstallStatus::UpToDate:
            text = tr("红石联机内核已是最新（v%1）").arg(hs->installedVersion());
            break;
        case HongshiClient::InstallStatus::Downloading:
            text = tr("正在下载红石联机内核...");
            break;
        case HongshiClient::InstallStatus::DownloadFailed:
            text = tr("红石联机内核下载失败，请重试");
            break;
        }
        m_hongshiStatusLabel->setText(text);
    }

    if (m_hongshiStartBtn) {
        if (active) {
            m_hongshiStartBtn->setText(open ? tr("停止联机") : tr("连接中..."));
            m_hongshiStartBtn->setEnabled(!open);
        } else {
            m_hongshiStartBtn->setText(tr("启动内核"));
            m_hongshiStartBtn->setEnabled(true);
        }
    }

    // 隧道页刷新
    if (target == 3) {
        refreshTunnelStatusBar();
    }
}

void MultiplayerPage::refreshTunnelStatusBar()
{
    if (!m_hongshiLatencyLabel) return;
    if (!m_hongshi->isTunnelActive()) {
        m_hongshiLatencyLabel->clear();
        return;
    }

    QString text;
    if (m_hongshiEnterTime.isValid()) {
        qint64 seconds = m_hongshiEnterTime.secsTo(QDateTime::currentDateTime());
        int h = int(seconds / 3600);
        int m = int((seconds % 3600) / 60);
        int s = int(seconds % 60);
        QString timeStr = (h > 0)
            ? QStringLiteral("%1:%2:%3")
                  .arg(h, 2, 10, QLatin1Char('0'))
                  .arg(m, 2, 10, QLatin1Char('0'))
                  .arg(s, 2, 10, QLatin1Char('0'))
            : QStringLiteral("%1:%2")
                  .arg(m, 2, 10, QLatin1Char('0'))
                  .arg(s, 2, 10, QLatin1Char('0'));

        QString statusText;
        switch (m_hongshi->state()) {
        case HongshiClient::State::Launching:
            statusText = tr("启动中");
            break;
        case HongshiClient::State::Connecting:
            statusText = tr("连接中转服务器");
            break;
        case HongshiClient::State::Open:
            statusText = tr("隧道已建立");
            break;
        default:
            statusText = tr("联机中");
            break;
        }
        text = tr("%1 | 联机时间 %2").arg(statusText, timeStr);
    }
    m_hongshiLatencyLabel->setText(text);

    // 状态颜色驱动 [status="..."] 选择器
    QString latencyStatus;
    switch (m_hongshi->state()) {
    case HongshiClient::State::Open:
        latencyStatus = QStringLiteral("good");
        break;
    case HongshiClient::State::Launching:
    case HongshiClient::State::Connecting:
        latencyStatus = QStringLiteral("medium");
        break;
    default:
        latencyStatus = QStringLiteral("bad");
        break;
    }
    m_hongshiLatencyLabel->setProperty("status", latencyStatus);
    m_hongshiLatencyLabel->style()->polish(m_hongshiLatencyLabel);

    // 隧道地址
    if (m_tunnelAddrLabel) {
        QString addr = m_hongshi->tunnelAddress();
        if (addr.isEmpty()) {
            m_tunnelAddrLabel->setText(tr("正在建立隧道..."));
        } else {
            m_tunnelAddrLabel->setText(addr);
        }
    }
}

void MultiplayerPage::populateNodeCombo()
{
    if (!m_hongshiNodeCombo) return;

    QString current = m_hongshiNodeCombo->currentData().toString();
    m_hongshiNodeCombo->clear();

    const QList<HongshiClient::Node> nodes = m_hongshi->serverNodes();
    for (const auto &n : nodes) {
        m_hongshiNodeCombo->addItem(
            QStringLiteral("%1 · %2").arg(n.name, n.host), n.host);
    }

    if (m_hongshiNodeCombo->count() > 0) {
        int idx = m_hongshiNodeCombo->findData(current);
        m_hongshiNodeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        if (m_hongshiNodeHint) {
            m_hongshiNodeHint->setText(tr("已从主站获取 %1 个中转节点").arg(nodes.size()));
        }
    } else {
        m_hongshiNodeCombo->addItem(tr("无可用节点"));
        if (m_hongshiNodeHint) {
            m_hongshiNodeHint->setText(tr("无法获取节点列表，请点击「刷新」重试或检查网络连接。"));
        }
    }
}

void MultiplayerPage::refreshInstallStatus()
{
    if (hongshiActive()) {
        refreshHongshiInstallStatus();
        return;
    }

    if (!m_updateBtn) return;
    auto status = m_client->installStatus();
    QString installedVer = m_client->installedVersion();
    QString latestVer = m_client->latestVersion();

    switch (status) {
    case TerracottaClient::InstallStatus::NotInstalled:
        if (!latestVer.isEmpty()) {
            m_updateBtn->setText(tr("下载 v%1").arg(latestVer));
            m_updateBtn->setToolTip(tr("下载并安装陶瓦联机核心 v%1").arg(latestVer));
        } else {
            m_updateBtn->setText(tr("下载"));
            m_updateBtn->setToolTip(tr("下载并安装陶瓦联机核心"));
        }
        m_updateBtn->setEnabled(true);
        break;
    case TerracottaClient::InstallStatus::Ready:
        if (!installedVer.isEmpty()) {
            m_updateBtn->setText(tr("检查更新"));
            m_updateBtn->setToolTip(tr("已安装 v%1，点击检查是否有新版本").arg(installedVer));
        } else {
            m_updateBtn->setText(tr("更新"));
            m_updateBtn->setToolTip(tr("检查并下载最新版本联机核心"));
        }
        m_updateBtn->setEnabled(true);
        break;
    case TerracottaClient::InstallStatus::CheckingUpdate:
        m_updateBtn->setText(tr("检查中"));
        m_updateBtn->setEnabled(false);
        m_updateBtn->setToolTip(tr("正在检查远程版本..."));
        break;
    case TerracottaClient::InstallStatus::UpdateAvailable:
        m_updateBtn->setText(tr("更新到 v%1").arg(latestVer));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(tr("当前 v%1，最新 v%2，点击更新").arg(installedVer, latestVer));
        break;
    case TerracottaClient::InstallStatus::UpToDate:
        m_updateBtn->setText(tr("已是最新"));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(tr("当前 v%1 已是最新版本，点击重新检查").arg(installedVer));
        break;
    case TerracottaClient::InstallStatus::Downloading:
        m_updateBtn->setText(tr("下载中"));
        m_updateBtn->setEnabled(false);
        m_updateBtn->setToolTip(tr("正在下载联机核心..."));
        break;
    case TerracottaClient::InstallStatus::DownloadFailed:
        m_updateBtn->setText(tr("重试"));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(tr("下载失败，点击重试"));
        break;
    }
}

void MultiplayerPage::refreshHongshiInstallStatus()
{
    if (!m_updateBtn) return;
    auto *hs = m_hongshi;
    auto status = hs->installStatus();
    QString installedVer = hs->installedVersion();
    QString latestVer = hs->latestVersion();

    switch (status) {
    case HongshiClient::InstallStatus::NotInstalled:
        if (!latestVer.isEmpty()) {
            m_updateBtn->setText(tr("下载 v%1").arg(latestVer));
            m_updateBtn->setToolTip(tr("下载并安装红石联机核心 v%1").arg(latestVer));
        } else {
            m_updateBtn->setText(tr("下载"));
            m_updateBtn->setToolTip(tr("下载并安装红石联机核心"));
        }
        m_updateBtn->setEnabled(true);
        break;
    case HongshiClient::InstallStatus::Ready:
        if (!installedVer.isEmpty()) {
            m_updateBtn->setText(tr("检查更新"));
            m_updateBtn->setToolTip(tr("已安装 v%1，点击检查是否有新版本").arg(installedVer));
        } else {
            m_updateBtn->setText(tr("更新"));
            m_updateBtn->setToolTip(tr("检查并下载最新版本联机核心"));
        }
        m_updateBtn->setEnabled(true);
        break;
    case HongshiClient::InstallStatus::CheckingUpdate:
        m_updateBtn->setText(tr("检查中"));
        m_updateBtn->setEnabled(false);
        m_updateBtn->setToolTip(tr("正在检查远程版本..."));
        break;
    case HongshiClient::InstallStatus::UpdateAvailable:
        m_updateBtn->setText(tr("更新到 v%1").arg(latestVer));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(tr("当前 v%1，最新 v%2，点击更新").arg(installedVer, latestVer));
        break;
    case HongshiClient::InstallStatus::UpToDate:
        m_updateBtn->setText(tr("已是最新"));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(tr("当前 v%1 已是最新版本，点击重新检查").arg(installedVer));
        break;
    case HongshiClient::InstallStatus::Downloading:
        m_updateBtn->setText(tr("下载中"));
        m_updateBtn->setEnabled(false);
        m_updateBtn->setToolTip(tr("正在下载红石联机核心..."));
        break;
    case HongshiClient::InstallStatus::DownloadFailed:
        m_updateBtn->setText(tr("重试"));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(tr("下载失败，点击重试"));
        break;
    }
}

void MultiplayerPage::refreshPlayerList()
{
    if (!m_playersLayout) return;

    // 清空旧卡片（保留末尾的 addStretch）
    while (m_playersLayout->count() > 1) {
        QLayoutItem *item = m_playersLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    QList<TerracottaClient::Profile> profiles = m_client->profiles();
    for (const auto &p : profiles) {
        QWidget *card = new QWidget();
        card->setProperty("cardRole", "mpContainer");
        card->setFixedHeight(60);

        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(8, 6, 8, 6);
        cardLayout->setSpacing(8);

        QLabel *avatarLabel = new QLabel();
        avatarLabel->setFixedSize(36, 36);
        avatarLabel->setAlignment(Qt::AlignCenter);
        QColor avatarColor = (p.kind == QStringLiteral("HOST"))
            ? QColor(255, 152, 0)   // 房主：橙色
            : (p.kind == QStringLiteral("LOCAL"))
                ? QColor(76, 175, 80) // 本地：绿色
                : QColor(33, 150, 243); // 房客：蓝色
        QPixmap avatar(36, 36);
        avatar.fill(Qt::transparent);
        {
            QPainter painter(&avatar);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(avatarColor);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(0, 0, 36, 36);
            painter.setPen(Qt::white);
            QFont font = painter.font();
            font.setBold(true);
            font.setPixelSize(18);
            painter.setFont(font);
            QString letter = p.name.isEmpty() ? QStringLiteral("?")
                : QString(p.name.at(0)).toUpper();
            painter.drawText(avatar.rect(), Qt::AlignCenter, letter);
        }
        avatarLabel->setPixmap(avatar);
        cardLayout->addWidget(avatarLabel);

        auto *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(4);
        infoLayout->setContentsMargins(0, 0, 0, 0);

        QLabel *nameLabel = new QLabel(p.name);
        nameLabel->setProperty("cardRole", "mpName");
        nameLabel->setToolTip(p.name);
        QFontMetrics fm(nameLabel->font());
        QString elidedName = fm.elidedText(p.name, Qt::ElideRight, 180);
        nameLabel->setText(elidedName);
        infoLayout->addWidget(nameLabel);

        auto *chipsLayout = new QHBoxLayout();
        chipsLayout->setSpacing(4);
        chipsLayout->setContentsMargins(0, 0, 0, 0);

        QString roleText;
        if (p.kind == QStringLiteral("HOST")) roleText = tr("房主");
        else if (p.kind == QStringLiteral("LOCAL")) roleText = tr("本机");
        else roleText = tr("房客");

        QLabel *roleChip = new QLabel(roleText);
        roleChip->setProperty("cardRole", "mpChip");
        chipsLayout->addWidget(roleChip);

        if (p.kind == QStringLiteral("LOCAL") || p.kind == QStringLiteral("HOST")) {
            QLabel *timeChip = new QLabel(tr("在线"));
            timeChip->setProperty("cardRole", "mpChip");
            chipsLayout->addWidget(timeChip);
        }

        if (!p.vendor.isEmpty()) {
            QString vendorShort = p.vendor.section(QLatin1Char(' '), 0, 0);
            QLabel *vendorChip = new QLabel(vendorShort);
            vendorChip->setProperty("cardRole", "mpChip");
            chipsLayout->addWidget(vendorChip);
        }

        chipsLayout->addStretch();
        infoLayout->addLayout(chipsLayout);

        cardLayout->addLayout(infoLayout, 1);

        QPushButton *copyNameBtn = new QPushButton();
        copyNameBtn->setProperty("cardRole", "mpActionBtn");
        copyNameBtn->setFixedSize(28, 28);
        copyNameBtn->setCursor(Qt::PointingHandCursor);
        copyNameBtn->setToolTip(tr("复制玩家名"));
        copyNameBtn->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/copy.svg"),
            ThemeManager::instance()->currentThemeColor(), 14));
        copyNameBtn->setIconSize(QSize(14, 14));

        QString playerName = p.name;
        connect(copyNameBtn, &QPushButton::clicked, this, [playerName]() {
            QApplication::clipboard()->setText(playerName);
        });

        cardLayout->addWidget(copyNameBtn);

        m_playersLayout->insertWidget(m_playersLayout->count() - 1, card);
    }
}

void MultiplayerPage::refreshStatusBar()
{
    if (!m_latencyLabel) return;
    if (!m_client->isInRoom()) {
        m_latencyLabel->clear();
        return;
    }

    QString text;
    if (m_roomEnterTime.isValid()) {
        qint64 seconds = m_roomEnterTime.secsTo(QDateTime::currentDateTime());
        int h = int(seconds / 3600);
        int m = int((seconds % 3600) / 60);
        int s = int(seconds % 60);
        QString timeStr = (h > 0)
            ? QStringLiteral("%1:%2:%3")
                  .arg(h, 2, 10, QLatin1Char('0'))
                  .arg(m, 2, 10, QLatin1Char('0'))
                  .arg(s, 2, 10, QLatin1Char('0'))
            : QStringLiteral("%1:%2")
                  .arg(m, 2, 10, QLatin1Char('0'))
                  .arg(s, 2, 10, QLatin1Char('0'));

        QString statusText;
        switch (m_client->state()) {
        case TerracottaClient::State::HostScanning:
            statusText = tr("扫描端口中");
            break;
        case TerracottaClient::State::HostStarting:
            statusText = tr("创建房间中");
            break;
        case TerracottaClient::State::HostOk:
            statusText = tr("房主就绪");
            break;
        case TerracottaClient::State::GuestConnecting:
            statusText = tr("连接房主中");
            break;
        case TerracottaClient::State::GuestStarting:
            statusText = tr("建立转发中");
            break;
        case TerracottaClient::State::GuestOk:
            statusText = tr("已连接");
            break;
        default:
            statusText = tr("联机中");
            break;
        }

        text = tr("%1 | 联机时间 %2").arg(statusText, timeStr);

        if (m_client->state() == TerracottaClient::State::GuestOk
            && !m_client->serverUrl().isEmpty()) {
            text += QStringLiteral("\n") + tr("服务器地址：%1").arg(m_client->serverUrl());
        }
    }
    m_latencyLabel->setText(text);

    QString latencyStatus;
    switch (m_client->state()) {
    case TerracottaClient::State::HostOk:
    case TerracottaClient::State::GuestOk:
        latencyStatus = QStringLiteral("good");
        break;
    case TerracottaClient::State::HostScanning:
    case TerracottaClient::State::HostStarting:
    case TerracottaClient::State::GuestConnecting:
    case TerracottaClient::State::GuestStarting:
        latencyStatus = QStringLiteral("medium");
        break;
    default:
        latencyStatus = QStringLiteral("bad");
        break;
    }
    m_latencyLabel->setProperty("status", latencyStatus);
    m_latencyLabel->style()->polish(m_latencyLabel);
}

void MultiplayerPage::onCoreChanged(int index)
{
    if (index == 1) {
        // 切到红石联机：离开陶瓦房间/停止陶瓦轮询不启停 daemon
        m_client->leaveRoom();
        if (m_hongshi->serverNodes().isEmpty()) {
            m_hongshi->refreshServerNodes();
        }
    } else {
        // 切到陶瓦联机：停止红石内核
        m_hongshi->stopKernel();
    }
    refreshPage();
    refreshInstallStatus();
    refreshPlayerList();
}

void MultiplayerPage::showInfoDialog(const QString &title, const QString &text)
{
    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle(title);
    dlg->setMinimumWidth(280);
    auto *layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto *label = new QLabel(text, dlg);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(label);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto *okBtn = new QPushButton(tr("确定"), dlg);
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setFixedHeight(30);
    okBtn->setMinimumWidth(80);
    btnLayout->addWidget(okBtn);
    layout->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);

    dlg->setModal(false);
    dlg->show();
}
