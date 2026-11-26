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
#include <QDir>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include "components/AppMessageBox.h"
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include "components/LobbyPublishDialog.h"
#include "components/CommunityAgreementDialog.h"
#include "components/NotificationManager.h"
#include "components/ShareToFriendDialog.h"
#include "pages/MultiplayerUiParts.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumChatService.h"
#include "utils/ForumImageStore.h"
#include "utils/ForumLobbyService.h"
#include "utils/CommandAssistant/LevelDatReader.h"
#include "utils/frp/FrpManager.h"
#include "utils/Hongshi/HongshiClient.h"
#include "utils/IconHelper.h"
#include "utils/Lobby/LobbyInstanceMatcher.h"
#include "utils/LocalCategoryManager.h"
#include "utils/Lobby/LobbyShareLink.h"
#include "utils/StyleKit.h"
#include "utils/Terracotta/TerracottaClient.h"
#include "utils/ThemeManager.h"
#include "utils/TouchScroll.h"

#include <QDesktopServices>
#include <QFrame>
#include <QIntValidator>
#include <QTabBar>
#include <QUrl>

// 版式零件（滚动页 / 卡片 / 状态条 / 状态圆点）与基岩版联机页共用
using MultiplayerUi::ScrollPage;
using MultiplayerUi::makeScrollPage;
using MultiplayerUi::makeCard;
using MultiplayerUi::makeCardTitle;
using MultiplayerUi::makeFieldLabel;
using MultiplayerUi::makeHintLabel;
using MultiplayerUi::makeDivider;
using MultiplayerUi::makeDot;
using MultiplayerUi::setDotTone;
using MultiplayerUi::makeStatusCard;
using MultiplayerUi::makeNoteCard;
using MultiplayerUi::makeLatencyRow;
using MultiplayerUi::letterAvatarPixmap;
using MultiplayerUi::roundAvatarPixmap;

namespace {

/// 玩家角色配色：房主橙 / 本机绿 / 房客蓝（头像底色、描边都用它）
QColor playerRoleColor(const QString &kind)
{
    if (kind == QStringLiteral("HOST"))
        return QColor(255, 152, 0);
    if (kind == QStringLiteral("LOCAL"))
        return QColor(76, 175, 80);
    return QColor(33, 150, 243);
}

} // namespace

#ifdef Q_OS_ANDROID
void MultiplayerPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 安卓：陶瓦核心以内置 so 随应用分发，首次进入联机页时做一次惰性初始化；
    // start() 自身幂等（已初始化/正在初始化时直接返回）
    if (!hongshiActive() && !frpActive())
        TerracottaClient::instance()->start();
}
#endif

MultiplayerPage::MultiplayerPage(QWidget *parent)
    : QWidget(parent)
    , m_client(TerracottaClient::instance())
    , m_hongshi(HongshiClient::instance())
    , m_stack(nullptr)
    , m_topBarContainer(nullptr)
    , m_coreCombo(nullptr)
    , m_updateBtn(nullptr)
    , m_homePage(nullptr)
    , m_statusDot(nullptr)
    , m_statusLabel(nullptr)
    , m_descLabel(nullptr)
    , m_joinBtn(nullptr)
    , m_createBtn(nullptr)
    , m_joinCard(nullptr)
    , m_joinCodeEdit(nullptr)
    , m_joinHint(nullptr)
    , m_joinConfirmBtn(nullptr)
    , m_joinCancelBtn(nullptr)
    , m_roomPage(nullptr)
    , m_latencyDot(nullptr)
    , m_latencyLabel(nullptr)
    , m_roomCodeLabel(nullptr)
    , m_copyCodeBtn(nullptr)
    , m_leaveBtn(nullptr)
    , m_playersTitle(nullptr)
    , m_playersScroll(nullptr)
    , m_playersContainer(nullptr)
    , m_playersLayout(nullptr)
    , m_hongshiHomePage(nullptr)
    , m_hongshiStatusDot(nullptr)
    , m_hongshiStatusLabel(nullptr)
    , m_hongshiNodeCombo(nullptr)
    , m_hongshiNodeRefreshBtn(nullptr)
    , m_hongshiNodeHint(nullptr)
    , m_hongshiPortSpin(nullptr)
    , m_hongshiStartBtn(nullptr)
    , m_hongshiTunnelPage(nullptr)
    , m_hongshiLatencyDot(nullptr)
    , m_hongshiLatencyLabel(nullptr)
    , m_tunnelAddrLabel(nullptr)
    , m_copyAddrBtn(nullptr)
    , m_hongshiStopBtn(nullptr)
    , m_frpHomePage(nullptr)
    , m_frpStatusDot(nullptr)
    , m_frpStatusLabel(nullptr)
    , m_frpDescLabel(nullptr)
    , m_frpKeyHint(nullptr)
    , m_frpKeyEdit(nullptr)
    , m_frpIdHint(nullptr)
    , m_frpIdEdit(nullptr)
    , m_frpHintLabel(nullptr)
    , m_frpLinkRow(nullptr)
    , m_frpPanelBtn(nullptr)
    , m_frpPickBtn(nullptr)
    , m_frpStartBtn(nullptr)
    , m_frpRoomPage(nullptr)
    , m_frpLatencyDot(nullptr)
    , m_frpLatencyLabel(nullptr)
    , m_frpAddrLabel(nullptr)
    , m_frpCopyBtn(nullptr)
    , m_frpRoomHint(nullptr)
    , m_frpStopBtn(nullptr)
{
    initUI();
    // 先把 frp 服务商的说明 / 已存密钥填好，切到樱花或 OpenFrp 时不会是空卡片
    updateFrpDesc();
    applyThemeStyles();

    // 发布到联机大厅：每 40 秒续一次期，服务端 2 分钟没收到心跳就会把房间下架
    if (m_publishHeartbeatTimer) {
        connect(m_publishHeartbeatTimer, &QTimer::timeout, this, [this]() {
            if (m_publishedRoomId <= 0)
                return;
            ForumLobbyService::instance()->sendHeartbeat(
                m_publishedRoomId, QStringLiteral("waiting"), nullptr,
                [this](const QString &message, int status) {
                    Q_UNUSED(message);
                    if (status != 404)
                        return;
                    // 房间已经在服务端过期：本地同步清掉发布状态
                    m_publishedRoomId = 0;
                    m_publishedCore.clear();
                    m_publishedJoin.clear();
                    if (m_publishHeartbeatTimer)
                        m_publishHeartbeatTimer->stop();
                    refreshPublishButtons();
                });
        });
    }

    // 分享短链：房间信息存在论坛上，联机期间每 40 秒续一次期；
    // 房主结束联机时本地会主动删掉，断线时服务端 2 分钟没收到心跳也会自动清掉
    if (m_shareHeartbeatTimer) {
        connect(m_shareHeartbeatTimer, &QTimer::timeout, this, [this]() {
            if (m_shareToken.isEmpty())
                return;
            ForumLobbyService::instance()->sendShareHeartbeat(
                m_shareToken, m_shareSecret, nullptr,
                [this](const QString &message, int status) {
                    Q_UNUSED(message);
                    if (status != 404)
                        return;
                    // 服务端已经没有这条分享（联机结束 / 过期）：本地同步作废
                    m_shareToken.clear();
                    m_shareSecret.clear();
                    m_shareCore.clear();
                    m_shareJoin.clear();
                    if (m_shareHeartbeatTimer)
                        m_shareHeartbeatTimer->stop();
                    refreshPublishButtons();
                });
        });
    }

    connect(m_client, &TerracottaClient::stateChanged, this, &MultiplayerPage::refreshPage);
    connect(m_client, &TerracottaClient::profilesChanged, this, &MultiplayerPage::refreshPlayerList);
    connect(m_client, &TerracottaClient::installStatusChanged, this, &MultiplayerPage::refreshInstallStatus);
    connect(m_client, &TerracottaClient::errorOccurred, this, [this](const QString &err) {
        // 联机出错不弹窗：左下角通知里说一句，页面状态条本身也会跟着变
        NotificationManager::showError(window(), err);
    });

    connect(m_hongshi, &HongshiClient::stateChanged, this, &MultiplayerPage::refreshPage);
    connect(m_hongshi, &HongshiClient::installStatusChanged, this, &MultiplayerPage::refreshInstallStatus);
    connect(m_hongshi, &HongshiClient::serverNodesChanged, this, &MultiplayerPage::populateNodeCombo);
    connect(m_hongshi, &HongshiClient::errorOccurred, this, [this](const QString &err) {
        NotificationManager::showError(window(), err);
    });

    auto *frp = FrpManager::instance();
    connect(frp, &FrpManager::stateChanged, this, &MultiplayerPage::refreshPage);
    connect(frp, &FrpManager::remoteAddressChanged, this, &MultiplayerPage::refreshFrpStatusBar);
    connect(frp, &FrpManager::downloadStateChanged, this, [this]() {
        refreshFrpInstallStatus();
        refreshFrpPage();
    });
    connect(frp, &FrpManager::downloadProgressChanged, this, [this](qreal) {
        refreshFrpInstallStatus();
        refreshFrpPage();
    });
    connect(frp, &FrpManager::downloadInfoChanged, this, [this](const QString &) {
        refreshFrpInstallStatus();
    });
    connect(frp, &FrpManager::installFinished, this,
            [this](bool success, const QString &message) {
                refreshFrpInstallStatus();
                refreshFrpPage();
                if (success)
                    NotificationManager::showSuccess(window(), message);
                else
                    NotificationManager::showError(window(), message);
            });
    connect(frp, &FrpManager::errorOccurred, this, [this](const QString &err) {
        if (frpActive())
            NotificationManager::showError(window(), err);
    });

    // 换主题色、或者亮色/暗色整体切换时都要重算样式
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &MultiplayerPage::applyThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MultiplayerPage::applyThemeStyles);

    // 刷新定时器（用于更新联机时间）
    QTimer *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (frpActive()) {
            refreshFrpStatusBar();
        } else if (hongshiActive()) {
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

MultiplayerPage::~MultiplayerPage()
{
    // 页面没了（关软件 / 关实例助手）：把分享的那份房间信息一并删掉，链接马上失效
    stopShareLink(true, false);
}

void MultiplayerPage::initUI()
{
    // 命名 + 允许样式表铺底：联机页自己铺主题底色，不靠外层窗口的调色板
    setObjectName(QStringLiteral("multiplayerRoot"));
    setAttribute(Qt::WA_StyledBackground, true);

    // 联机大厅已经独立成主窗口的一级页面，这里只保留联机核心本身
    buildCorePage(this);

    // 发布心跳：40 秒一次，服务端 2 分钟没收到就下架
    m_publishHeartbeatTimer = new QTimer(this);
    m_publishHeartbeatTimer->setInterval(40000);

    // 分享短链续期：房间信息存在论坛上，联机期间同样每 40 秒续一次
    m_shareHeartbeatTimer = new QTimer(this);
    m_shareHeartbeatTimer->setInterval(40000);
}

void MultiplayerPage::buildCorePage(QWidget *page)
{
    auto *mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(14, 12, 14, 0);
    mainLayout->setSpacing(0);

    // ---- 顶部：核心选择 + 更新按钮（跨核心共享） ----
    m_topBarContainer = new QWidget(page);
    m_topBarContainer->setObjectName(QStringLiteral("multiplayerTopBar"));
    auto *topLayout = new QHBoxLayout(m_topBarContainer);
    topLayout->setContentsMargins(0, 0, 0, 10);
    topLayout->setSpacing(8);

    auto *coreCaption = new QLabel(tr("核心"), m_topBarContainer);
    coreCaption->setObjectName(QStringLiteral("mpTopCaption"));
    topLayout->addWidget(coreCaption);

    m_coreCombo = new QComboBox(m_topBarContainer);
    m_coreCombo->setObjectName(QStringLiteral("multiplayerCoreCombo"));
    m_coreCombo->setCursor(Qt::PointingHandCursor);
    m_coreCombo->setFixedHeight(32);
    m_coreCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_coreCombo->addItem(tr("陶瓦联机"));
    m_coreCombo->addItem(tr("红石联机"));
    // frp 服务商加彩色圆点小图标便于区分
    const auto dotIcon = [](const QColor &color) {
        QPixmap pix(16, 16);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(2, 2, 12, 12);
        return QIcon(pix);
    };
    m_coreCombo->addItem(dotIcon(FrpManager::providerColor(FrpManager::Provider::Sakura)),
                         tr("樱花联机"));
    m_coreCombo->addItem(dotIcon(FrpManager::providerColor(FrpManager::Provider::OpenFrp)),
                         tr("OpenFrp 联机"));
    m_coreCombo->setToolTip(tr("选择联机核心"));

    m_updateBtn = new QPushButton(tr("更新"), m_topBarContainer);
    m_updateBtn->setObjectName(QStringLiteral("multiplayerUpdateBtn"));
    m_updateBtn->setCursor(Qt::PointingHandCursor);
    m_updateBtn->setFixedHeight(32);
    m_updateBtn->setMinimumWidth(88);
    m_updateBtn->setToolTip(tr("检查并下载最新版本联机核心"));

    topLayout->addWidget(m_coreCombo, 1);
    topLayout->addWidget(m_updateBtn);
    mainLayout->addWidget(m_topBarContainer);

    // 顶栏与页面内容之间的一条细线
    auto *topDivider = new QFrame(page);
    topDivider->setObjectName(QStringLiteral("mpTopDivider"));
    topDivider->setFixedHeight(1);
    mainLayout->addWidget(topDivider);

    // ---- 页面栈 ----
    m_stack = new QStackedWidget(page);
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

    m_frpHomePage = new QWidget();
    m_frpHomePage->setObjectName(QStringLiteral("multiplayerFrpHome"));
    buildFrpHomePage(m_frpHomePage);
    m_stack->addWidget(m_frpHomePage);     // 4: frp 服务商 首页

    m_frpRoomPage = new QWidget();
    m_frpRoomPage->setObjectName(QStringLiteral("multiplayerFrpRoom"));
    buildFrpRoomPage(m_frpRoomPage);
    m_stack->addWidget(m_frpRoomPage);     // 5: frp 服务商 房间页

    mainLayout->addWidget(m_stack, 1);

    connect(m_coreCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MultiplayerPage::onCoreChanged);

    m_stack->setCurrentIndex(0);
}

void MultiplayerPage::setInstanceContext(const QString &path, const QString &version,
                                         const QString &loader)
{
    m_instancePath = path;
    m_instanceVersion = version;
    m_instanceLoader = loader;
}

QString MultiplayerPage::currentTunnelAddress() const
{
    // 只认“看起来就是个地址”的文字：标签里也可能是「正在连接节点…」
    // 「已建立，可在面板查看地址」这类说明，不能当地址用
    const auto usable = [](const QString &text) {
        const QString value = text.trimmed();
        if (value.isEmpty() || value.contains(QLatin1Char(' ')))
            return QString();
        if (!value.contains(QLatin1Char(':')))
            return QString();
        if (!value.contains(QLatin1Char('.')) && !value.contains(QStringLiteral("localhost")))
            return QString();
        return value;
    };

    // frp 隧道（樱花 / OpenFrp）优先，其次是红石隧道
    if (m_frpAddrLabel) {
        const QString address = usable(m_frpAddrLabel->text());
        if (!address.isEmpty())
            return address;
    }
    if (m_tunnelAddrLabel) {
        const QString address = usable(m_tunnelAddrLabel->text());
        if (!address.isEmpty())
            return address;
    }
    return QString();
}

bool MultiplayerPage::hongshiActive() const
{
    return m_coreCombo && m_coreCombo->currentIndex() == 1;
}

bool MultiplayerPage::frpActive() const
{
    return m_coreCombo && m_coreCombo->currentIndex() >= 2;
}

FrpManager::Provider MultiplayerPage::frpProvider() const
{
    return (m_coreCombo && m_coreCombo->currentIndex() == 2)
        ? FrpManager::Provider::Sakura
        : FrpManager::Provider::OpenFrp;
}

// ============================
// 陶瓦联机页面
// ============================

void MultiplayerPage::buildHomePage(QWidget *page)
{
    const ScrollPage parts = makeScrollPage(page);

    // ---- 顶部状态条：核心装没装、跑没跑，一眼看到 ----
    parts.body->addWidget(makeStatusCard(page, m_statusDot, m_statusLabel,
                                         QStringLiteral("multiplayerStatusLabel")));

    // ---- 说明卡片 ----
    QFrame *noteCard = makeNoteCard(page, tr("关于陶瓦联机"),
                                    m_descLabel, QStringLiteral("multiplayerDescLabel"));
    m_descLabel->setText(
        tr("本软件可以使用陶瓦联机，可以与支持陶瓦联机的软件"
           "（如 HMCL、PCL社区版、VersePC、FCL、ZL2）进行联机。<br><br>"
           "点「创建房间」后请进入游戏，在游戏菜单里选「对局域网开放」，"
           "房间才算开好；邀请码会显示在下一页，发给朋友即可加入。<br><br>"
           "陶瓦联机是第三方开源软件，与我们无关，如遇到一些问题可前往"
           "<a href=\"https://github.com/burningtnt/Terracotta\">陶瓦联机GitHub仓库</a>"
           "反馈，无法访问可试着使用本软件合法合规的 GitHub 加速工具加速网络。<br><br>"
           "陶瓦联机使用 P2P 双端直连技术，无中转，最终联机体验由联机参与者有着较大的关系。<br><br>"
           "在联机过程中，请严格遵守当地法律法规。<br><br>"
           "感谢了解与配合，祝你联机愉快！")
    );
    parts.body->addWidget(noteCard);
    parts.body->addStretch(1);

    // ---- 加入房间 / 创建房间按钮（固定在底部） ----
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);

    m_joinBtn = new QPushButton(tr("加入房间"), page);
    m_joinBtn->setObjectName(QStringLiteral("multiplayerJoinBtn"));
    m_joinBtn->setCursor(Qt::PointingHandCursor);
    m_joinBtn->setFixedHeight(44);

    m_createBtn = new QPushButton(tr("创建房间"), page);
    m_createBtn->setObjectName(QStringLiteral("multiplayerCreateBtn"));
    m_createBtn->setCursor(Qt::PointingHandCursor);
    m_createBtn->setFixedHeight(44);

    btnRow->addWidget(m_joinBtn, 1);
    btnRow->addWidget(m_createBtn, 1);

    // ---- 邀请码输入卡：点「加入房间」就地展开，不再弹输入框 ----
    MultiplayerUi::JoinCardParts joinCard = MultiplayerUi::makeJoinCard(page);
    m_joinCard = joinCard.card;
    m_joinCodeEdit = joinCard.codeEdit;
    m_joinHint = joinCard.hint;
    m_joinConfirmBtn = joinCard.confirmBtn;
    m_joinCancelBtn = joinCard.cancelBtn;
    joinCard.title->setText(tr("邀请码"));
    m_joinConfirmBtn->setText(tr("加入"));
    m_joinCancelBtn->setText(tr("取消"));
    m_joinCodeEdit->setPlaceholderText(tr("粘贴好友发来的邀请码，如 U/XXXX-XXXX-XXXX-XXXX"));
    parts.outer->addWidget(m_joinCard);

    parts.outer->addLayout(btnRow);

// 信号连接
    connect(m_updateBtn, &QPushButton::clicked, this, [this]() {
        if (frpActive()) {
            auto *frp = FrpManager::instance();
            const auto provider = frpProvider();
            if (frp->isDownloading(provider))
                return; // 正在下载
            if (!frp->isFrpcPresent(provider))
                frp->downloadFrpc(provider);
            return;
        }
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
        // 核心出问题 / 还没启动时按钮本身就是「重试」：先把核心拉起来
        const auto state = m_client->state();
        if (state == TerracottaClient::State::Exception) {
            m_client->leaveRoom();
            return;
        }
        const bool coreStarting = state == TerracottaClient::State::Fatal
            || (state == TerracottaClient::State::NotInstalled && !m_client->isProcessRunning());
        if (coreStarting)
            m_client->start();
        // 邀请码就在页面里输，不等核心就绪——点了按钮却什么都不发生才是最奇怪的
        showJoinCard();
        if (coreStarting || state == TerracottaClient::State::Launching)
            setJoinHintText(tr("联机核心正在启动，稍等一下再点「加入」。"));
    });

    connect(m_joinConfirmBtn, &QPushButton::clicked, this, [this]() { submitJoinCode(); });
    connect(m_joinCodeEdit, &QLineEdit::returnPressed, this, [this]() { submitJoinCode(); });
    connect(m_joinCancelBtn, &QPushButton::clicked, this, [this]() { hideJoinCard(); });
    connect(m_joinCodeEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        if (m_joinHint && m_joinHint->property("error").toBool())
            setJoinHintText(QString());
    });

    connect(m_createBtn, &QPushButton::clicked, this, [this]() {
        // 建房：状态全在页面状态条里，不再弹「正在创建房间」的窗口
        const auto state = m_client->state();
        if (m_client->installedVersion().isEmpty() && !m_client->isProcessRunning()) {
            NotificationManager::showError(window(), tr("还没安装陶瓦联机核心，先点右上角「更新」下载。"));
            return;
        }
        if (state == TerracottaClient::State::Exception) {
            m_client->leaveRoom();
            return;
        }
        if (state == TerracottaClient::State::NotInstalled && !m_client->isProcessRunning()) {
            m_client->start();
            return;
        }
        if (state == TerracottaClient::State::Fatal) {
            m_client->start();
            return;
        }
        if (state == TerracottaClient::State::Launching) {
            NotificationManager::showInfo(window(), tr("联机核心正在启动，马上就好…"));
            return;
        }
        if (state != TerracottaClient::State::Waiting) {
            NotificationManager::showInfo(window(), tr("当前已在房间里，先离开当前房间再开新房间。"));
            return;
        }
        hideJoinCard();
        m_client->createRoom(localPlayerName());
    });
}

// ============================
// 加入房间：邀请码就地输入
// ============================

void MultiplayerPage::showJoinCard()
{
    if (!m_joinCard)
        return;
    setJoinHintText(QString());
    m_joinCard->setVisible(true);
    if (m_joinCodeEdit)
        m_joinCodeEdit->setFocus();
}

void MultiplayerPage::hideJoinCard()
{
    if (!m_joinCard)
        return;
    m_joinCard->setVisible(false);
    setJoinHintText(QString());
    if (m_joinCodeEdit)
        m_joinCodeEdit->clear();
}

void MultiplayerPage::submitJoinCode()
{
    const QString code = m_joinCodeEdit ? m_joinCodeEdit->text().trimmed() : QString();
    if (code.isEmpty()) {
        setJoinHintText(tr("先粘贴好友发来的邀请码。"), true);
        if (m_joinCodeEdit)
            m_joinCodeEdit->setFocus();
        return;
    }
    if (!TerracottaClient::verifyRoomCode(code)) {
        setJoinHintText(tr("邀请码格式不对，应为 U/XXXX-XXXX-XXXX-XXXX。"), true);
        return;
    }

    const auto state = m_client->state();
    if (m_client->installedVersion().isEmpty() && !m_client->isProcessRunning()) {
        setJoinHintText(tr("还没安装陶瓦联机核心，先点右上角「更新」下载。"), true);
        return;
    }
    if (state == TerracottaClient::State::Launching
        || (state == TerracottaClient::State::NotInstalled && !m_client->isProcessRunning())) {
        setJoinHintText(tr("联机核心正在启动，稍等一下再点「加入」。"), true);
        return;
    }
    if (state == TerracottaClient::State::Exception || state == TerracottaClient::State::Fatal) {
        setJoinHintText(tr("联机核心状态异常，先点一次「重试」恢复。"), true);
        return;
    }
    if (state != TerracottaClient::State::Waiting) {
        setJoinHintText(tr("已经在房间里了，先离开当前房间再加入。"), true);
        return;
    }
    if (!m_client->joinRoom(code, localPlayerName())) {
        setJoinHintText(tr("无法加入房间，请检查邀请码或稍后再试。"), true);
        return;
    }
    hideJoinCard();
}

void MultiplayerPage::setJoinHintText(const QString &text, bool error)
{
    if (!m_joinHint)
        return;
    m_joinHint->setText(text.trimmed().isEmpty()
                            ? tr("邀请码由房主创建房间后生成，形如 U/XXXX-XXXX-XXXX-XXXX。")
                            : text);
    MultiplayerUi::setJoinHintError(m_joinHint, error);
}

// ============================
// 陶瓦联机：房间页
// ============================

void MultiplayerPage::buildRoomPage(QWidget *page)
{
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 12, 0, 12);
    outer->setSpacing(12);

    // ---- 顶部：状态小圆点 + 联机时间 ----
    outer->addLayout(makeLatencyRow(page, m_latencyDot, m_latencyLabel));
    m_latencyLabel->setAlignment(Qt::AlignCenter);

    // ---- 邀请码卡片：大字邀请码 + 一键复制 ----
    QFrame *codeCard = makeCard(QStringLiteral("mpCodeCard"), page);
    m_codeCard = codeCard;
    auto *codeRow = new QHBoxLayout(codeCard);
    codeRow->setContentsMargins(16, 12, 12, 12);
    codeRow->setSpacing(10);

    auto *codeColumn = new QVBoxLayout();
    codeColumn->setSpacing(2);
    auto *codeCaption = new QLabel(tr("房间邀请码"), codeCard);
    codeCaption->setObjectName(QStringLiteral("mpCodeCaption"));
    codeColumn->addWidget(codeCaption);

    m_roomCodeLabel = new QLabel(codeCard);
    m_roomCodeLabel->setObjectName(QStringLiteral("multiplayerRoomCodeLabel"));
    m_roomCodeLabel->setWordWrap(true);
    m_roomCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeColumn->addWidget(m_roomCodeLabel);
    codeRow->addLayout(codeColumn, 1);

    m_copyCodeBtn = new QPushButton(codeCard);
    m_copyCodeBtn->setObjectName(QStringLiteral("multiplayerCopyCodeBtn"));
    m_copyCodeBtn->setCursor(Qt::PointingHandCursor);
    m_copyCodeBtn->setFixedSize(36, 36);
    m_copyCodeBtn->setToolTip(tr("复制邀请码"));
    m_copyCodeBtn->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/copy.svg"),
        ThemeManager::instance()->currentThemeColor(), 16));
    m_copyCodeBtn->setIconSize(QSize(16, 16));
    codeRow->addWidget(m_copyCodeBtn, 0, Qt::AlignVCenter);
    outer->addWidget(codeCard);

    // ---- 玩家列表 ----
    auto *playersHeader = new QHBoxLayout();
    playersHeader->setContentsMargins(2, 2, 2, 0);
    m_playersTitle = new QLabel(tr("玩家"), page);
    m_playersTitle->setObjectName(QStringLiteral("mpSectionTitle"));
    playersHeader->addWidget(m_playersTitle);
    playersHeader->addStretch(1);
    outer->addLayout(playersHeader);

    m_playersScroll = new QScrollArea(page);
    m_playersScroll->setObjectName(QStringLiteral("multiplayerPlayersScroll"));
    m_playersScroll->setWidgetResizable(true);
    m_playersScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_playersScroll->setFrameShape(QFrame::NoFrame);
    m_playersScroll->viewport()->setAutoFillBackground(false);
    TouchScroll::enableDragScroll(m_playersScroll);

    m_playersContainer = new QWidget();
    m_playersContainer->setObjectName(QStringLiteral("multiplayerPlayersContainer"));
    m_playersLayout = new QVBoxLayout(m_playersContainer);
    m_playersLayout->setContentsMargins(0, 0, 2, 0);
    m_playersLayout->setSpacing(8);
    m_playersLayout->addStretch();

    m_playersScroll->setWidget(m_playersContainer);
    outer->addWidget(m_playersScroll, 1);

    // ---- 发布到联机大厅：把邀请码挂到论坛，朋友在软件里能一键进房 ----
    m_pubTerracottaBtn = new QPushButton(tr("发布到联机大厅"), page);
    m_pubTerracottaBtn->setObjectName(QStringLiteral("multiplayerPublishBtn"));
    m_pubTerracottaBtn->setCursor(Qt::PointingHandCursor);
    m_pubTerracottaBtn->setFixedHeight(40);
    m_pubTerracottaBtn->setToolTip(tr("把当前房间发到论坛联机大厅，网页端只能看邀请码，软件端可一键加入"));
    connect(m_pubTerracottaBtn, &QPushButton::clicked,
            this, &MultiplayerPage::onPublishToLobbyClicked);

    // 创建分享链接：房间信息传到论坛后只留房间号，朋友点开看邀请码，装了方块盒子可一键加入
    m_shareTerracottaBtn = new QPushButton(tr("创建分享链接"), page);
    m_shareTerracottaBtn->setObjectName(QStringLiteral("multiplayerShareBtn"));
    m_shareTerracottaBtn->setCursor(Qt::PointingHandCursor);
    m_shareTerracottaBtn->setFixedHeight(40);
    m_shareTerracottaBtn->setToolTip(tr("生成一条很短的分享链接，创建时可以写一句简要介绍，朋友打开就能看到邀请码并一键加入"));
    connect(m_shareTerracottaBtn, &QPushButton::clicked,
            this, &MultiplayerPage::onCreateShareLinkClicked);

    auto *publishRow = new QHBoxLayout();
    publishRow->setSpacing(8);
    publishRow->addWidget(m_pubTerracottaBtn, 1);
    publishRow->addWidget(m_shareTerracottaBtn);
    outer->addLayout(publishRow);

    // ---- 离开房间按钮 ----
    m_leaveBtn = new QPushButton(tr("离开房间"), page);
    m_leaveBtn->setObjectName(QStringLiteral("multiplayerLeaveBtn"));
    m_leaveBtn->setCursor(Qt::PointingHandCursor);
    m_leaveBtn->setFixedHeight(44);
    outer->addWidget(m_leaveBtn);

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
    const ScrollPage parts = makeScrollPage(page);

    // 顶部状态条
    parts.body->addWidget(makeStatusCard(page, m_hongshiStatusDot, m_hongshiStatusLabel,
                                         QStringLiteral("multiplayerStatusLabel")));

    // ---- 连接设置卡片 ----
    QFrame *formCard = makeCard(QStringLiteral("mpFormCard"), page);
    auto *form = new QVBoxLayout(formCard);
    form->setContentsMargins(14, 12, 14, 14);
    form->setSpacing(8);

    // 中转服务器节点
    auto *nodeHeaderRow = new QHBoxLayout();
    nodeHeaderRow->setSpacing(8);
    nodeHeaderRow->addWidget(makeFieldLabel(tr("中转服务器节点"), formCard), 1);

    m_hongshiNodeRefreshBtn = new QPushButton(tr("刷新"), formCard);
    m_hongshiNodeRefreshBtn->setObjectName(QStringLiteral("hongshiNodeRefreshBtn"));
    m_hongshiNodeRefreshBtn->setCursor(Qt::PointingHandCursor);
    m_hongshiNodeRefreshBtn->setFixedHeight(28);
    m_hongshiNodeRefreshBtn->setMinimumWidth(58);
    nodeHeaderRow->addWidget(m_hongshiNodeRefreshBtn);
    form->addLayout(nodeHeaderRow);

    m_hongshiNodeCombo = new QComboBox(formCard);
    m_hongshiNodeCombo->setObjectName(QStringLiteral("hongshiNodeCombo"));
    m_hongshiNodeCombo->setCursor(Qt::PointingHandCursor);
    m_hongshiNodeCombo->setFixedHeight(36);
    m_hongshiNodeCombo->addItem(tr("加载中..."));
    form->addWidget(m_hongshiNodeCombo);

    m_hongshiNodeHint = makeHintLabel(
        tr("正在从主站获取节点列表..."), formCard);
    form->addWidget(m_hongshiNodeHint);

    form->addWidget(makeDivider(formCard));

    // 本地 Minecraft 端口
    form->addWidget(makeFieldLabel(tr("本地 Minecraft 端口"), formCard));

    m_hongshiPortSpin = new QSpinBox(formCard);
    m_hongshiPortSpin->setObjectName(QStringLiteral("hongshiPortSpin"));
    m_hongshiPortSpin->setRange(1, 65535);
    m_hongshiPortSpin->setValue(25565);
    m_hongshiPortSpin->setFixedHeight(36);
    // 上下调节箭头在窄卡片里很挤，直接当输入框用（手机端也只能敲键盘）
    m_hongshiPortSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    form->addWidget(m_hongshiPortSpin);

    form->addWidget(makeHintLabel(
        tr("请先启动游戏并选择「对局域网开放」，然后填入游戏显示的端口。"), formCard));

    form->addSpacing(2);

    // 启动内核按钮
    m_hongshiStartBtn = new QPushButton(tr("启动内核"), formCard);
    m_hongshiStartBtn->setObjectName(QStringLiteral("hongshiStartBtn"));
    m_hongshiStartBtn->setCursor(Qt::PointingHandCursor);
    m_hongshiStartBtn->setFixedHeight(44);
    form->addWidget(m_hongshiStartBtn);

    parts.body->addWidget(formCard);

    // 说明卡片
    QLabel *descLabel = nullptr;
    auto *descCard = makeNoteCard(page, tr("关于红石联机"), descLabel,
                                  QStringLiteral("multiplayerDescLabel"));
    descLabel->setText(
        tr("红石联机是第三方开源联机服务，与本软件无关。选择中转节点并填入本地端口后，"
           "点击启动即可获得一个联机地址，将地址分享给好友，参与者在 Minecraft 中连接该地址"
           "即可共同游玩。<br><br>"
           "参考项目：<a href=\"https://github.com/hongshionline/hongshi2\">红石联机 2 开源项目</a><br><br>"
           "在联机过程中，请严格遵守当地法律法规。")
    );
    parts.body->addWidget(descCard);
    parts.body->addStretch(1);

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
            NotificationManager::showError(
                window(), tr("还没安装红石联机核心，先点右上角「更新」下载。"));
            return;
        }
        if (m_hongshi->isProcessRunning()) {
            NotificationManager::showInfo(window(), tr("红石联机内核正在启动，稍等一下…"));
            return;
        }
        QString server = m_hongshiNodeCombo->currentData().toString();
        if (server.isEmpty()) {
            NotificationManager::showError(window(), tr("先选一个中转服务器节点。"));
            return;
        }
        m_hongshi->startKernel(server, quint16(m_hongshiPortSpin->value()));
    });
}

void MultiplayerPage::buildHongshiTunnelPage(QWidget *page)
{
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 12, 0, 12);
    outer->setSpacing(12);

    // ---- 顶部：状态小圆点 + 联机时间 ----
    outer->addLayout(makeLatencyRow(page, m_hongshiLatencyDot, m_hongshiLatencyLabel));
    m_hongshiLatencyLabel->setAlignment(Qt::AlignCenter);

    // ---- 隧道地址卡片 ----
    QFrame *addrCard = makeCard(QStringLiteral("mpCodeCard"), page);
    auto *addrRow = new QHBoxLayout(addrCard);
    addrRow->setContentsMargins(16, 12, 12, 12);
    addrRow->setSpacing(10);

    auto *addrColumn = new QVBoxLayout();
    addrColumn->setSpacing(2);
    auto *addrCaption = new QLabel(tr("联机地址"), addrCard);
    addrCaption->setObjectName(QStringLiteral("mpCodeCaption"));
    addrColumn->addWidget(addrCaption);

    m_tunnelAddrLabel = new QLabel(addrCard);
    m_tunnelAddrLabel->setObjectName(QStringLiteral("multiplayerRoomCodeLabel"));
    m_tunnelAddrLabel->setWordWrap(true);
    m_tunnelAddrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_tunnelAddrLabel->setText(tr("正在建立隧道..."));
    addrColumn->addWidget(m_tunnelAddrLabel);
    addrRow->addLayout(addrColumn, 1);

    m_copyAddrBtn = new QPushButton(addrCard);
    m_copyAddrBtn->setObjectName(QStringLiteral("multiplayerCopyCodeBtn"));
    m_copyAddrBtn->setCursor(Qt::PointingHandCursor);
    m_copyAddrBtn->setFixedSize(36, 36);
    m_copyAddrBtn->setToolTip(tr("复制联机地址"));
    m_copyAddrBtn->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/copy.svg"),
        ThemeManager::instance()->currentThemeColor(), 16));
    m_copyAddrBtn->setIconSize(QSize(16, 16));
    addrRow->addWidget(m_copyAddrBtn, 0, Qt::AlignVCenter);
    outer->addWidget(addrCard);

    auto *hint = makeHintLabel(
        tr("在 Minecraft 多人游戏中连接上方地址即可联机，请将地址分享给好友。"), page);
    hint->setAlignment(Qt::AlignCenter);
    outer->addWidget(hint);

    // 发布 / 停止两个操作固定在底部
    outer->addStretch(1);

    // ---- 发布到联机大厅 ----
    m_pubHongshiBtn = new QPushButton(tr("发布到联机大厅"), page);
    m_pubHongshiBtn->setObjectName(QStringLiteral("multiplayerPublishBtn"));
    m_pubHongshiBtn->setCursor(Qt::PointingHandCursor);
    m_pubHongshiBtn->setFixedHeight(40);
    m_pubHongshiBtn->setToolTip(tr("把隧道地址发到论坛联机大厅，网页端只能查看地址，软件端可一键加入"));
    connect(m_pubHongshiBtn, &QPushButton::clicked,
            this, &MultiplayerPage::onPublishToLobbyClicked);

    // 创建分享链接
    m_shareHongshiBtn = new QPushButton(tr("创建分享链接"), page);
    m_shareHongshiBtn->setObjectName(QStringLiteral("multiplayerShareBtn"));
    m_shareHongshiBtn->setCursor(Qt::PointingHandCursor);
    m_shareHongshiBtn->setFixedHeight(40);
    m_shareHongshiBtn->setToolTip(tr("生成一条很短的分享链接，创建时可以写一句简要介绍，朋友打开就能看到连接地址并一键加入"));
    connect(m_shareHongshiBtn, &QPushButton::clicked,
            this, &MultiplayerPage::onCreateShareLinkClicked);

    auto *hongshiPublishRow = new QHBoxLayout();
    hongshiPublishRow->setSpacing(8);
    hongshiPublishRow->addWidget(m_pubHongshiBtn, 1);
    hongshiPublishRow->addWidget(m_shareHongshiBtn);
    outer->addLayout(hongshiPublishRow);

    // ---- 停止联机按钮 ----
    m_hongshiStopBtn = new QPushButton(tr("停止联机"), page);
    m_hongshiStopBtn->setObjectName(QStringLiteral("multiplayerLeaveBtn"));
    m_hongshiStopBtn->setCursor(Qt::PointingHandCursor);
    m_hongshiStopBtn->setFixedHeight(44);
    outer->addWidget(m_hongshiStopBtn);

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

// ============================
// frp 服务商联机（樱花 / OpenFrp）：首页
// ============================

void MultiplayerPage::buildFrpHomePage(QWidget *page)
{
    const ScrollPage parts = makeScrollPage(page);

    // 顶部状态条（frpc 安装/下载状态）
    parts.body->addWidget(makeStatusCard(page, m_frpStatusDot, m_frpStatusLabel,
                                         QStringLiteral("multiplayerStatusLabel")));

    // ---- 连接设置卡片 ----
    QFrame *formCard = makeCard(QStringLiteral("mpFormCard"), page);
    auto *form = new QVBoxLayout(formCard);
    form->setContentsMargins(14, 12, 14, 14);
    form->setSpacing(8);

    // 访问密钥
    m_frpKeyHint = makeFieldLabel(tr("访问密钥"), formCard);
    form->addWidget(m_frpKeyHint);

    m_frpKeyEdit = new QLineEdit(formCard);
    m_frpKeyEdit->setObjectName(QStringLiteral("frpKeyEdit"));
    m_frpKeyEdit->setFixedHeight(36);
    m_frpKeyEdit->setPlaceholderText(tr("在官网面板「访问密钥」页面获取"));
    form->addWidget(m_frpKeyEdit);

    form->addSpacing(2);

    // 隧道 ID
    m_frpIdHint = makeFieldLabel(tr("隧道 ID"), formCard);
    form->addWidget(m_frpIdHint);

    m_frpIdEdit = new QLineEdit(formCard);
    m_frpIdEdit->setObjectName(QStringLiteral("frpIdEdit"));
    m_frpIdEdit->setFixedHeight(36);
    m_frpIdEdit->setPlaceholderText(tr("官网面板隧道列表中的数字 ID"));
    m_frpIdEdit->setValidator(new QIntValidator(1, 999999999, m_frpIdEdit));
    form->addWidget(m_frpIdEdit);

    // 打开面板 / 手动选择 frpc
    m_frpLinkRow = new QWidget(formCard);
    m_frpLinkRow->setObjectName(QStringLiteral("frpLinkRow"));
    auto *linkLayout = new QHBoxLayout(m_frpLinkRow);
    linkLayout->setContentsMargins(0, 0, 0, 0);
    linkLayout->setSpacing(8);

    m_frpPanelBtn = new QPushButton(tr("打开面板"), m_frpLinkRow);
    m_frpPanelBtn->setObjectName(QStringLiteral("hongshiNodeRefreshBtn"));
    m_frpPanelBtn->setCursor(Qt::PointingHandCursor);
    m_frpPanelBtn->setFixedHeight(30);
    m_frpPanelBtn->setToolTip(tr("注册账号、创建 TCP 隧道、查看访问密钥与隧道 ID"));
    linkLayout->addWidget(m_frpPanelBtn);

    m_frpPickBtn = new QPushButton(tr("手动选择 frpc"), m_frpLinkRow);
    m_frpPickBtn->setObjectName(QStringLiteral("hongshiNodeRefreshBtn"));
    m_frpPickBtn->setCursor(Qt::PointingHandCursor);
    m_frpPickBtn->setFixedHeight(30);
    m_frpPickBtn->setToolTip(tr("自动下载失败时，可手动下载后选择 frpc 程序导入"));
    linkLayout->addWidget(m_frpPickBtn);
    linkLayout->addStretch(1);
    form->addWidget(m_frpLinkRow);

    // 获取指引提示
    m_frpHintLabel = makeHintLabel(QString(), formCard);
    form->addWidget(m_frpHintLabel);

    form->addWidget(makeDivider(formCard));

    // 启动隧道按钮
    m_frpStartBtn = new QPushButton(tr("启动隧道"), formCard);
    m_frpStartBtn->setObjectName(QStringLiteral("multiplayerCreateBtn"));
    m_frpStartBtn->setCursor(Qt::PointingHandCursor);
    m_frpStartBtn->setFixedHeight(44);
    form->addWidget(m_frpStartBtn);

    parts.body->addWidget(formCard);

    // ---- 说明卡片（内容随核心切换，见 updateFrpDesc） ----
    auto *descCard = makeNoteCard(page, tr("关于这个联机服务"),
                                  m_frpDescLabel, QStringLiteral("multiplayerDescLabel"));
    parts.body->addWidget(descCard);
    parts.body->addStretch(1);

    connect(m_frpStartBtn, &QPushButton::clicked, this, &MultiplayerPage::startFrpTunnel);
    connect(m_frpPanelBtn, &QPushButton::clicked, this, &MultiplayerPage::openFrpPanel);
    connect(m_frpPickBtn, &QPushButton::clicked, this, &MultiplayerPage::pickFrpcManually);

    // 输入变化即保存（按服务商分别记住）
    connect(m_frpKeyEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        FrpManager::instance()->setAccessKey(frpProvider(), text);
    });
    connect(m_frpIdEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        FrpManager::instance()->setTunnelId(frpProvider(), text.toInt());
    });
}

// ============================
// frp 服务商联机：房间页
// ============================

void MultiplayerPage::buildFrpRoomPage(QWidget *page)
{
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 12, 0, 12);
    outer->setSpacing(12);

    // ---- 顶部：状态小圆点 + 联机时间 ----
    outer->addLayout(makeLatencyRow(page, m_frpLatencyDot, m_frpLatencyLabel));
    m_frpLatencyLabel->setAlignment(Qt::AlignCenter);

    // ---- 远程连接地址卡片 ----
    QFrame *addrCard = makeCard(QStringLiteral("mpCodeCard"), page);
    auto *addrRow = new QHBoxLayout(addrCard);
    addrRow->setContentsMargins(16, 12, 12, 12);
    addrRow->setSpacing(10);

    auto *addrColumn = new QVBoxLayout();
    addrColumn->setSpacing(2);
    auto *addrCaption = new QLabel(tr("远程连接地址"), addrCard);
    addrCaption->setObjectName(QStringLiteral("mpCodeCaption"));
    addrColumn->addWidget(addrCaption);

    m_frpAddrLabel = new QLabel(addrCard);
    m_frpAddrLabel->setObjectName(QStringLiteral("multiplayerRoomCodeLabel"));
    m_frpAddrLabel->setWordWrap(true);
    m_frpAddrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_frpAddrLabel->setText(tr("正在连接节点..."));
    addrColumn->addWidget(m_frpAddrLabel);
    addrRow->addLayout(addrColumn, 1);

    m_frpCopyBtn = new QPushButton(addrCard);
    m_frpCopyBtn->setObjectName(QStringLiteral("multiplayerCopyCodeBtn"));
    m_frpCopyBtn->setCursor(Qt::PointingHandCursor);
    m_frpCopyBtn->setFixedSize(36, 36);
    m_frpCopyBtn->setToolTip(tr("复制联机地址"));
    m_frpCopyBtn->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/copy.svg"),
        ThemeManager::instance()->currentThemeColor(), 16));
    m_frpCopyBtn->setIconSize(QSize(16, 16));
    addrRow->addWidget(m_frpCopyBtn, 0, Qt::AlignVCenter);
    outer->addWidget(addrCard);

    m_frpRoomHint = makeHintLabel(QString(), page);
    m_frpRoomHint->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_frpRoomHint);

    // 发布 / 停止两个操作固定在底部
    outer->addStretch(1);

    // ---- 发布到联机大厅 ----
    m_pubFrpBtn = new QPushButton(tr("发布到联机大厅"), page);
    m_pubFrpBtn->setObjectName(QStringLiteral("multiplayerPublishBtn"));
    m_pubFrpBtn->setCursor(Qt::PointingHandCursor);
    m_pubFrpBtn->setFixedHeight(40);
    m_pubFrpBtn->setToolTip(tr("把远程地址发到论坛联机大厅，网页端只能查看地址，软件端可一键加入"));
    connect(m_pubFrpBtn, &QPushButton::clicked,
            this, &MultiplayerPage::onPublishToLobbyClicked);

    // 创建分享链接
    m_shareFrpBtn = new QPushButton(tr("创建分享链接"), page);
    m_shareFrpBtn->setObjectName(QStringLiteral("multiplayerShareBtn"));
    m_shareFrpBtn->setCursor(Qt::PointingHandCursor);
    m_shareFrpBtn->setFixedHeight(40);
    m_shareFrpBtn->setToolTip(tr("生成一条很短的分享链接，创建时可以写一句简要介绍，朋友打开就能看到连接地址并一键加入"));
    connect(m_shareFrpBtn, &QPushButton::clicked,
            this, &MultiplayerPage::onCreateShareLinkClicked);

    auto *frpPublishRow = new QHBoxLayout();
    frpPublishRow->setSpacing(8);
    frpPublishRow->addWidget(m_pubFrpBtn, 1);
    frpPublishRow->addWidget(m_shareFrpBtn);
    outer->addLayout(frpPublishRow);

    // ---- 停止联机按钮 ----
    m_frpStopBtn = new QPushButton(tr("停止联机"), page);
    m_frpStopBtn->setObjectName(QStringLiteral("multiplayerLeaveBtn"));
    m_frpStopBtn->setCursor(Qt::PointingHandCursor);
    m_frpStopBtn->setFixedHeight(44);
    outer->addWidget(m_frpStopBtn);

    connect(m_frpCopyBtn, &QPushButton::clicked, this, [this]() {
        const QString addr = FrpManager::instance()->remoteAddress();
        if (addr.isEmpty()) return;
        QApplication::clipboard()->setText(addr);
        m_frpCopyBtn->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/copy.svg"),
            QColor(76, 175, 80), 16));
        QTimer::singleShot(800, this, [this]() {
            m_frpCopyBtn->setIcon(IconHelper::loadColoredIcon(
                QStringLiteral(":/Images/Icons/copy.svg"),
                ThemeManager::instance()->currentThemeColor(), 16));
        });
    });

    connect(m_frpStopBtn, &QPushButton::clicked, this, [this]() {
        FrpManager::instance()->stop();
    });
}

void MultiplayerPage::applyThemeStyles()
{
    // 两套联机页（Java / 基岩）共用同一份令牌样式，见 MultiplayerUiParts.h
    setStyleSheet(StyleKit::resolve(MultiplayerUi::pageStyleSheet()));
    const bool darkTheme = ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
    const QColor linkColor(StyleKit::resolve(darkTheme ? QStringLiteral("@LINK_HOVER@")
                                                       : QStringLiteral("@LINK@")));
    for (QLabel *desc : findChildren<QLabel *>(QStringLiteral("multiplayerDescLabel")))
        MultiplayerUi::applyDescriptionLinkColor(desc, linkColor);
}

void MultiplayerPage::refreshPage()
{
    // frp 服务商核心：走独立刷新逻辑
    if (frpActive()) {
        refreshFrpPage();
        return;
    }

    // 红石联机核心：走独立刷新逻辑
    if (hongshiActive()) {
        refreshHongshiPage();
        return;
    }

    auto state = m_client->state();
    bool inRoom = m_client->isInRoom();

    // 切换页面
    if (inRoom) {
        // 已经进房：邀请码输入卡自动收起，回到首页时不会停在上次输到一半的状态
        hideJoinCard();
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
#ifdef Q_OS_ANDROID
            // 安卓：核心随应用内置，这里只是「原生层还没初始化」
            text = m_client->installedVersion().isEmpty()
                ? tr("联机核心待启动")
                : tr("联机核心待启动（内置 v%1）").arg(m_client->installedVersion());
#else
            if (m_client->installedVersion().isEmpty()) {
                text = tr("未安装联机核心");
            } else {
                text = tr("联机核心未运行（已安装 v%1）").arg(m_client->installedVersion());
            }
#endif
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

        // 状态圆点跟着状态走：绿=可用，黄=进行中/待操作，红=出错
        QString tone;
        switch (state) {
        case TerracottaClient::State::Waiting:
            tone = m_client->isProcessRunning() ? QStringLiteral("ok") : QStringLiteral("idle");
            break;
        case TerracottaClient::State::HostOk:
        case TerracottaClient::State::GuestOk:
            tone = QStringLiteral("ok");
            break;
        case TerracottaClient::State::Exception:
        case TerracottaClient::State::Fatal:
            tone = QStringLiteral("err");
            break;
        default:
            tone = QStringLiteral("warn");
            break;
        }
        setDotTone(m_statusDot, tone);
    }

    // 更新首页按钮可用性
    if (m_joinBtn && m_createBtn) {
        bool canAct = (state == TerracottaClient::State::Waiting)
            || (state == TerracottaClient::State::NotInstalled
                && !m_client->installedVersion().isEmpty())
            || (state == TerracottaClient::State::Exception
                && m_client->isProcessRunning())
            || (state == TerracottaClient::State::Fatal); // 启动失败允许点「重试」再拉起核心
        m_joinBtn->setEnabled(canAct);
        m_createBtn->setEnabled(canAct);

        if (state == TerracottaClient::State::Exception
            || state == TerracottaClient::State::Fatal) {
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

    // 邀请码只对房主有用：客人进房后不必再看到别人的邀请码，整块卡片直接收起
    if (m_codeCard) {
        const bool hosting = state == TerracottaClient::State::HostScanning
                          || state == TerracottaClient::State::HostStarting
                          || state == TerracottaClient::State::HostOk;
        m_codeCard->setVisible(hosting);
    }

    refreshStatusBar();
    refreshPlayerList();
    refreshPublishButtons();
    ensurePublishStillValid();
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
            text = hs->latestVersion().isEmpty()
                ? tr("红石联机有新版本可更新")
                : tr("红石联机有新版 v%1 可更新").arg(hs->latestVersion());
            break;
        case HongshiClient::InstallStatus::UpToDate:
            text = hs->installedVersion().isEmpty()
                ? tr("红石联机内核已是最新")
                : tr("红石联机内核已是最新（v%1）").arg(hs->installedVersion());
            break;
        case HongshiClient::InstallStatus::Downloading:
            text = tr("正在下载红石联机内核...");
            break;
        case HongshiClient::InstallStatus::DownloadFailed:
            text = tr("红石联机内核下载失败，请重试");
            break;
        case HongshiClient::InstallStatus::Unsupported:
            text = tr("红石联机暂不支持当前系统");
            break;
        }
        m_hongshiStatusLabel->setText(text);

        // 状态圆点：已就绪绿点，需要下载/更新或正在下载黄点，下载失败红点
        QString tone;
        switch (hs->installStatus()) {
        case HongshiClient::InstallStatus::Ready:
        case HongshiClient::InstallStatus::UpToDate:
            tone = QStringLiteral("ok");
            break;
        case HongshiClient::InstallStatus::DownloadFailed:
            tone = QStringLiteral("err");
            break;
        default:
            tone = QStringLiteral("warn");
            break;
        }
        setDotTone(m_hongshiStatusDot, tone);
    }

    if (m_hongshiStartBtn) {
        if (hs->installStatus() == HongshiClient::InstallStatus::Unsupported) {
            m_hongshiStartBtn->setText(tr("暂不支持"));
            m_hongshiStartBtn->setEnabled(false);
        } else if (active) {
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

    refreshPublishButtons();
    ensurePublishStillValid();
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
    setDotTone(m_hongshiLatencyDot, latencyStatus == QLatin1String("good")
                                        ? QStringLiteral("ok")
                                        : (latencyStatus == QLatin1String("medium")
                                               ? QStringLiteral("warn")
                                               : QStringLiteral("err")));

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
    if (frpActive()) {
        refreshFrpInstallStatus();
        return;
    }

    if (hongshiActive()) {
        refreshHongshiInstallStatus();
        return;
    }

#ifdef Q_OS_ANDROID
    // 安卓：陶瓦核心随 APK 内置（JNI），没有独立下载/更新入口
    if (!m_updateBtn) return;
    const QString builtinVer = m_client->installedVersion();
    m_updateBtn->setText(builtinVer.isEmpty()
                             ? tr("内置核心")
                             : tr("内置核心 v%1").arg(builtinVer));
    m_updateBtn->setToolTip(tr("陶瓦联机核心随应用内置，更新版本请升级方块盒子"));
    m_updateBtn->setEnabled(false);
    return;
#endif

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
        if (latestVer.isEmpty()) {
            m_updateBtn->setText(tr("更新"));
            m_updateBtn->setToolTip(tr("发现新版本，点击下载更新"));
        } else {
            m_updateBtn->setText(tr("更新到 v%1").arg(latestVer));
            m_updateBtn->setToolTip(tr("当前 v%1，最新 v%2，点击更新").arg(installedVer, latestVer));
        }
        m_updateBtn->setEnabled(true);
        break;
    case HongshiClient::InstallStatus::UpToDate:
        m_updateBtn->setText(tr("已是最新"));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(installedVer.isEmpty()
                                    ? tr("已是最新版本，点击重新检查")
                                    : tr("当前 v%1 已是最新版本，点击重新检查").arg(installedVer));
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
    case HongshiClient::InstallStatus::Unsupported:
        m_updateBtn->setText(tr("暂不支持"));
        m_updateBtn->setEnabled(false);
        m_updateBtn->setToolTip(tr("红石联机官方暂未提供适用于当前系统的内核"));
        break;
    }
}

// ============================
// frp 服务商联机（樱花 / OpenFrp）
// ============================

void MultiplayerPage::updateFrpDesc()
{
    const auto provider = frpProvider();
    auto *frp = FrpManager::instance();

    if (m_frpDescLabel) {
        if (provider == FrpManager::Provider::Sakura) {
            m_frpDescLabel->setText(
                tr("樱花联机基于樱花内网穿透（SakuraFrp）：在官网注册并创建 TCP 隧道后，"
                   "把「访问密钥」和「隧道 ID」填到下面，点「启动隧道」即可获得一个联机地址，"
                   "把地址分享给好友，参与者在 Minecraft 中连接该地址即可共同游玩。<br><br>"
                   "创建隧道时类型选 TCP，本地地址填 127.0.0.1，本地端口填游戏"
                   "「对局域网开放」显示的端口。<br><br>"
                   "SakuraFrp 是第三方服务（需实名认证），与本软件无关，如遇问题可前往"
                   "<a href=\"https://doc.natfrp.com/\">SakuraFrp 帮助文档</a>反馈。<br><br>"
                   "在联机过程中，请严格遵守当地法律法规。"));
        } else {
            m_frpDescLabel->setText(
                tr("OpenFrp 联机基于 OpenFrp 公益内网穿透：在官网注册并创建 TCP 隧道后，"
                   "把「访问密钥」和「隧道 ID」填到下面，点「启动隧道」即可获得一个联机地址，"
                   "把地址分享给好友，参与者在 Minecraft 中连接该地址即可共同游玩。<br><br>"
                   "创建隧道时类型选 TCP，本地地址填 127.0.0.1，本地端口填游戏"
                   "「对局域网开放」显示的端口。<br><br>"
                   "OpenFrp 是第三方服务，与本软件无关，如遇问题可前往"
                   "<a href=\"https://www.openfrp.net/\">OpenFrp 官网</a>反馈。<br><br>"
                   "在联机过程中，请严格遵守当地法律法规。"));
        }
    }

    if (m_frpKeyEdit)
        m_frpKeyEdit->setText(frp->accessKey(provider));
    if (m_frpIdEdit) {
        const int id = frp->tunnelId(provider);
        m_frpIdEdit->setText(id > 0 ? QString::number(id) : QString());
    }
    if (m_frpHintLabel) {
        m_frpHintLabel->setText(
            tr("没有账号？点「打开面板」注册并创建 TCP 隧道，再把密钥和 ID 填在上面。"));
    }

    // 说明文字是重建过的，链接要按当前主题重新上色
    if (m_frpDescLabel) {
        const bool darkTheme =
            ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
        MultiplayerUi::applyDescriptionLinkColor(
            m_frpDescLabel,
            QColor(StyleKit::resolve(darkTheme ? QStringLiteral("@LINK_HOVER@")
                                               : QStringLiteral("@LINK@"))));
    }
}

void MultiplayerPage::refreshFrpPage()
{
    if (!frpActive())
        return;

    auto *frp = FrpManager::instance();
    const auto provider = frpProvider();
    const bool running = frp->isRunning();

    // 页面切换：隧道运行中 → 房间页(index 5)，否则首页(index 4)
    const int target = running ? 5 : 4;
    if (m_stack->currentIndex() != target) {
        m_stack->setCurrentIndex(target);
        if (target == 5)
            m_frpEnterTime = QDateTime::currentDateTime();
    }

    // 首页状态文字（frpc 安装/下载状态）
    if (m_frpStatusLabel) {
        QString text;
        if (frp->isDownloading(provider)) {
            const qreal progress = frp->downloadProgress();
            text = progress >= 0
                ? tr("正在下载 frpc… %1%").arg(qRound(progress * 100))
                : tr("正在下载 frpc…");
            if (!frp->downloadInfo().isEmpty())
                text += QStringLiteral("\n") + frp->downloadInfo();
        } else if (!frp->isFrpcPresent(provider)) {
            text = tr("未安装 frpc 客户端，请点击顶部「下载」按钮安装");
        } else {
            const QString ver = frp->installedVersion(provider);
            text = ver.isEmpty() ? tr("frpc 客户端已就绪")
                                 : tr("frpc 客户端已就绪（v%1）").arg(ver);
        }
        m_frpStatusLabel->setText(text);

        // 状态圆点：frpc 就绪绿点，未装 / 下载中黄点
        setDotTone(m_frpStatusDot,
                   frp->isFrpcPresent(provider) ? QStringLiteral("ok") : QStringLiteral("warn"));
    }

    // 首页按钮：frpc 未就绪时不可启动
    if (m_frpStartBtn)
        m_frpStartBtn->setEnabled(frp->isFrpcPresent(provider));

    if (target == 5)
        refreshFrpStatusBar();

    refreshPublishButtons();
    ensurePublishStillValid();
}

void MultiplayerPage::refreshFrpInstallStatus()
{
    if (!frpActive() || !m_updateBtn) return;
    auto *frp = FrpManager::instance();
    const auto provider = frpProvider();

    if (frp->isDownloading(provider)) {
        const qreal progress = frp->downloadProgress();
        m_updateBtn->setText(progress >= 0
            ? tr("下载中 %1%").arg(qRound(progress * 100)) : tr("下载中"));
        m_updateBtn->setEnabled(false);
        m_updateBtn->setToolTip(frp->downloadInfo().isEmpty()
            ? tr("正在下载 frpc 客户端…") : frp->downloadInfo());
        return;
    }

    if (!frp->isFrpcPresent(provider)) {
        m_updateBtn->setText(tr("下载"));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(tr("自动下载并安装 %1 的 frpc 客户端")
                                    .arg(FrpManager::providerName(provider)));
        return;
    }

    m_updateBtn->setText(tr("已是最新"));
    m_updateBtn->setEnabled(false);
    m_updateBtn->setToolTip(tr("frpc 客户端已就绪（%1）")
                                .arg(frp->installedVersion(provider)));
}

void MultiplayerPage::refreshFrpStatusBar()
{
    if (!m_frpLatencyLabel) return;
    auto *frp = FrpManager::instance();
    if (!frp->isRunning()) {
        m_frpLatencyLabel->clear();
        return;
    }

    const bool established = frp->isTunnelEstablished();

    // 顶部状态与联机时间
    QString text;
    if (m_frpEnterTime.isValid()) {
        qint64 seconds = m_frpEnterTime.secsTo(QDateTime::currentDateTime());
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
        text = tr("%1 | 联机时间 %2").arg(frp->statusText(), timeStr);
    }
    m_frpLatencyLabel->setText(text);

    // 状态颜色驱动 [status="..."] 选择器
    m_frpLatencyLabel->setProperty("status",
        established ? QStringLiteral("good") : QStringLiteral("medium"));
    m_frpLatencyLabel->style()->polish(m_frpLatencyLabel);
    setDotTone(m_frpLatencyDot, established ? QStringLiteral("ok") : QStringLiteral("warn"));

    // 大字：远程连接地址
    const QString addr = frp->remoteAddress();
    if (m_frpAddrLabel) {
        if (!addr.isEmpty())
            m_frpAddrLabel->setText(addr);
        else if (established)
            m_frpAddrLabel->setText(tr("已建立，可在面板查看地址"));
        else
            m_frpAddrLabel->setText(tr("正在连接节点..."));
    }

    // 提示
    if (m_frpRoomHint) {
        m_frpRoomHint->setText(addr.isEmpty()
            ? tr("连接建立后这里会显示远程地址；也可以到官网面板查看隧道的连接地址。")
            : tr("在 Minecraft 多人游戏中连接上方地址即可联机，请将地址分享给好友。"));
    }
}

void MultiplayerPage::startFrpTunnel()
{
    auto *frp = FrpManager::instance();
    const auto provider = frpProvider();
    if (frp->isRunning())
        return; // 隧道运行中，房间页有「停止联机」

    if (!frp->isFrpcPresent(provider)) {
        NotificationManager::showError(
            window(), tr("还没装 frpc 客户端，先点右上角「更新」下载。"));
        return;
    }

    const QString key = m_frpKeyEdit ? m_frpKeyEdit->text().trimmed() : QString();
    const QString idStr = m_frpIdEdit ? m_frpIdEdit->text().trimmed() : QString();
    if (key.isEmpty()) {
        NotificationManager::showError(
            window(), tr("先填访问密钥：在官网面板「访问密钥」页面获取。"));
        return;
    }
    bool ok = false;
    const int tunnelId = idStr.toInt(&ok);
    if (!ok || tunnelId <= 0) {
        NotificationManager::showError(
            window(), tr("隧道 ID 要填官网面板隧道列表里的数字 ID。"));
        return;
    }

    frp->setAccessKey(provider, key);
    frp->setTunnelId(provider, tunnelId);
    frp->start(provider);
}

void MultiplayerPage::openFrpPanel()
{
    QDesktopServices::openUrl(QUrl(FrpManager::panelUrl(frpProvider())));
}

void MultiplayerPage::pickFrpcManually()
{
    const auto provider = frpProvider();
    const QString file = QFileDialog::getOpenFileName(
        this, tr("选择 frpc 程序"), QString(),
        tr("frpc 程序 (frpc*.exe);;所有文件 (*.*)"));
    if (file.isEmpty())
        return;

    if (FrpManager::instance()->importFrpc(provider, file)) {
        NotificationManager::showSuccess(window(), tr("frpc 导入成功"));
        refreshFrpInstallStatus();
        refreshFrpPage();
    } else {
        NotificationManager::showError(
            window(), tr("导入失败：请确认选的是 frpc 程序文件。"));
    }
}

QString MultiplayerPage::localPlayerName() const
{
    const QString name = ForumAccountService::instance()->playerName();
    if (!name.isEmpty())
        return name;
    return tr("BlockBox 玩家");
}

bool MultiplayerPage::isSelfProfile(const QString &playerName, const QString &kind,
                                    bool hosting) const
{
    if (kind == QStringLiteral("LOCAL"))
        return true;
    if (hosting && kind == QStringLiteral("HOST"))
        return true;

    // 房客端不一定拿得到稳定的"本机"标记，退而按进房间时报的昵称认领自己
    if (!hosting && ForumAccountService::instance()->isLoggedIn())
    {
        const QString selfName = localPlayerName();
        return !selfName.isEmpty() && selfName.compare(playerName.trimmed(), Qt::CaseInsensitive) == 0;
    }
    return false;
}

void MultiplayerPage::lookupPlayerAccount(const QString &playerName)
{
    const QString key = playerName.trimmed();
    if (key.size() < 2 || m_playerAccounts.contains(key)
        || m_playerLookupPending.contains(key) || m_playerLookupMissed.contains(key))
        return;

    m_playerLookupPending.insert(key);
    ForumChatService::instance()->searchUsers(
        key,
        [this, key](const QVector<ForumFriendInfo> &users) {
            m_playerLookupPending.remove(key);
            for (const ForumFriendInfo &user : users)
            {
                if (!user.isValid())
                    continue;
                // 只认用户名 / 昵称完全一致的结果，避免搜出半个名字就套错头像
                if (user.username.compare(key, Qt::CaseInsensitive) != 0
                    && user.displayName.compare(key, Qt::CaseInsensitive) != 0)
                    continue;

                PlayerAccount account;
                account.id = user.id;
                account.username = user.username;
                account.displayName = user.displayName;
                account.avatar = user.avatar;
                m_playerAccounts.insert(key, account);
                schedulePlayerListRefresh();
                return;
            }
            m_playerLookupMissed.insert(key);
        },
        [this, key](const QString &, int) { m_playerLookupPending.remove(key); });
}

void MultiplayerPage::schedulePlayerListRefresh()
{
    if (m_playerListRefreshQueued)
        return;
    m_playerListRefreshQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_playerListRefreshQueued = false;
        if (m_client->isInRoom())
            refreshPlayerList();
    });
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

    // 玩家列表标题带上人数
    if (m_playersTitle) {
        m_playersTitle->setText(profiles.isEmpty() ? tr("玩家")
                                                   : tr("玩家 · %1").arg(profiles.size()));
    }

    // 一个人都没进来时给一句提示，列表不至于空得莫名
    if (profiles.isEmpty()) {
        auto *emptyLabel = new QLabel(tr("还没有人加入，把邀请码发给好友吧"));
        emptyLabel->setObjectName(QStringLiteral("mpEmptyPlayers"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);
        m_playersLayout->insertWidget(m_playersLayout->count() - 1, emptyLabel);
        return;
    }

    // 本机的方块盒子身份：登录后用自己的账户头像 / 昵称覆盖游戏名
    ForumAccountService *forum = ForumAccountService::instance();
    const bool loggedIn = forum->isLoggedIn();
    QString selfName;
    QString selfAvatar;
    if (loggedIn)
    {
        const ForumUser user = forum->user();
        selfName = user.displayName.trimmed().isEmpty() ? user.username.trimmed()
                                                        : user.displayName.trimmed();
        if (!selfName.isEmpty())
            selfAvatar = user.avatar;
    }
    // 反查别人的账户要动论坛接口：没过人机验证就别去打扰人家，免得在房间里莫名弹滑块
    const bool canLookupAccounts = loggedIn && forum->humanCheckSatisfied();
    const bool hosting = m_client->state() == TerracottaClient::State::HostScanning
                         || m_client->state() == TerracottaClient::State::HostStarting
                         || m_client->state() == TerracottaClient::State::HostOk;

    for (const auto &p : profiles) {
        const bool isSelf = isSelfProfile(p.name, p.kind, hosting);

        // 能认出来的方块盒子账户（自己的，或按名字反查到的）就用账户资料覆盖游戏名
        QString shownName = p.name;
        QString avatarPath;
        bool accountLinked = false;
        if (isSelf && !selfName.isEmpty())
        {
            shownName = selfName;
            avatarPath = selfAvatar;
            accountLinked = true;
        }
        else if (!isSelf)
        {
            const PlayerAccount account = m_playerAccounts.value(p.name.trimmed());
            if (account.isValid())
            {
                shownName = account.displayName.trimmed().isEmpty() ? account.username
                                                                    : account.displayName.trimmed();
                avatarPath = account.avatar;
                accountLinked = true;
            }
            else if (canLookupAccounts)
            {
                // 第一次见到就查一次，查到后会自动重绘列表
                lookupPlayerAccount(p.name);
            }
        }

        const QColor roleColor = playerRoleColor(p.kind);

        QWidget *card = new QWidget();
        card->setProperty("cardRole", "mpContainer");
        card->setFixedHeight(64);

        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(10, 8, 10, 8);
        cardLayout->setSpacing(10);

        auto *avatarLabel = new QLabel(card);
        avatarLabel->setFixedSize(40, 40);
        avatarLabel->setAlignment(Qt::AlignCenter);
        avatarLabel->setPixmap(letterAvatarPixmap(roleColor, shownName, 40,
                                                  avatarLabel->devicePixelRatioF()));
        if (!avatarPath.trimmed().isEmpty())
        {
            const QString avatarUrl =
                ForumAccountService::instance()->absoluteUrl(avatarPath.trimmed());
            if (!avatarUrl.isEmpty())
            {
                QPointer<QLabel> guard(avatarLabel);
                ForumImageStore::instance()->requestImage(
                    avatarUrl, [guard, roleColor](const QImage &image) {
                        if (guard.isNull() || image.isNull())
                            return;
                        guard->setPixmap(roundAvatarPixmap(image, roleColor, 40,
                                                           guard->devicePixelRatioF()));
                    });
            }
        }
        cardLayout->addWidget(avatarLabel, 0, Qt::AlignVCenter);

        auto *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(4);
        infoLayout->setContentsMargins(0, 0, 0, 0);

        auto *nameLabel = new QLabel(card);
        nameLabel->setProperty("cardRole", "mpName");
        nameLabel->setText(QFontMetrics(nameLabel->font())
                               .elidedText(shownName, Qt::ElideRight, 180));
        if (shownName != p.name)
            nameLabel->setToolTip(tr("%1\n游戏内名字：%2").arg(shownName, p.name));
        else
            nameLabel->setToolTip(shownName);
        infoLayout->addWidget(nameLabel);

        auto *chipsLayout = new QHBoxLayout();
        chipsLayout->setSpacing(4);
        chipsLayout->setContentsMargins(0, 0, 0, 0);

        QString roleText;
        if (p.kind == QStringLiteral("HOST")) roleText = tr("房主");
        else if (p.kind == QStringLiteral("LOCAL")) roleText = tr("本机");
        else roleText = tr("房客");

        auto *roleChip = new QLabel(roleText, card);
        roleChip->setProperty("cardRole", "mpChip");
        if (p.kind == QStringLiteral("HOST"))
            roleChip->setProperty("accent", "host");
        else if (isSelf)
            roleChip->setProperty("accent", "self");
        chipsLayout->addWidget(roleChip);

        if (accountLinked)
        {
            auto *accountChip = new QLabel(tr("方块盒子"), card);
            accountChip->setProperty("cardRole", "mpChip");
            accountChip->setProperty("accent", "account");
            accountChip->setToolTip(tr("这位玩家关联了方块盒子账户，头像和昵称取自账户资料"));
            chipsLayout->addWidget(accountChip);
        }

        if (p.kind == QStringLiteral("LOCAL") || p.kind == QStringLiteral("HOST")) {
            auto *timeChip = new QLabel(tr("在线"), card);
            timeChip->setProperty("cardRole", "mpChip");
            chipsLayout->addWidget(timeChip);
        }

        if (!p.vendor.isEmpty()) {
            QString vendorShort = p.vendor.section(QLatin1Char(' '), 0, 0);
            auto *vendorChip = new QLabel(vendorShort, card);
            vendorChip->setProperty("cardRole", "mpChip");
            chipsLayout->addWidget(vendorChip);
        }

        chipsLayout->addStretch();
        infoLayout->addLayout(chipsLayout);

        cardLayout->addLayout(infoLayout, 1);

        auto *copyNameBtn = new QPushButton(card);
        copyNameBtn->setProperty("cardRole", "mpActionBtn");
        copyNameBtn->setFixedSize(32, 32);
        copyNameBtn->setCursor(Qt::PointingHandCursor);
        copyNameBtn->setToolTip(tr("复制名称"));
        copyNameBtn->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/copy.svg"),
            ThemeManager::instance()->currentThemeColor(), 15));
        copyNameBtn->setIconSize(QSize(15, 15));

        connect(copyNameBtn, &QPushButton::clicked, this, [shownName]() {
            QApplication::clipboard()->setText(shownName);
        });

        cardLayout->addWidget(copyNameBtn, 0, Qt::AlignVCenter);

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
    setDotTone(m_latencyDot, latencyStatus == QLatin1String("good")
                                 ? QStringLiteral("ok")
                                 : (latencyStatus == QLatin1String("medium")
                                        ? QStringLiteral("warn")
                                        : QStringLiteral("err")));
}

void MultiplayerPage::onCoreChanged(int index)
{
    // 非本核心即停，保证同一时刻只有一个核心在运行
    if (index != 0)
        m_client->leaveRoom();
    if (index != 1)
        m_hongshi->stopKernel();
    if (!frpActive())
        FrpManager::instance()->stop(); // 离开 frp 核心即停止隧道

    if (frpActive())
        updateFrpDesc(); // 按服务商刷新说明与表单
    if (index == 1 && m_hongshi->serverNodes().isEmpty()) {
        m_hongshi->refreshServerNodes();
    }
    refreshPage();
    refreshInstallStatus();
    refreshPlayerList();
}


// ============================
// 发布到联机大厅
// ============================

bool MultiplayerPage::currentPublishTarget(QString *core, QString *joinInfo,
                                           QString *address, QString *error) const
{
    if (core)
        core->clear();
    if (joinInfo)
        joinInfo->clear();
    if (address)
        address->clear();
    if (error)
        error->clear();

    if (frpActive())
    {
        auto *frp = FrpManager::instance();
        const QString addr = frp->remoteAddress().trimmed();
        if (!frp->isRunning() || addr.isEmpty())
        {
            if (error)
                *error = tr("隧道还没建立，等房间页显示连接地址后再发布。");
            return false;
        }
        if (core)
            *core = QStringLiteral("frp");
        if (joinInfo)
            *joinInfo = addr;
        if (address)
            *address = addr;
        return true;
    }

    if (hongshiActive())
    {
        const QString addr = m_hongshi->tunnelAddress().trimmed();
        if (!m_hongshi->isTunnelActive() || addr.isEmpty())
        {
            if (error)
                *error = tr("隧道还没建立，等房间页显示连接地址后再发布。");
            return false;
        }
        if (core)
            *core = QStringLiteral("hongshi");
        if (joinInfo)
            *joinInfo = addr;
        if (address)
            *address = addr;
        return true;
    }

    if (m_client->state() != TerracottaClient::State::HostOk || m_client->roomCode().isEmpty())
    {
        if (error)
            *error = tr("只有房主创建好房间后才能发布：先在「联机」页创建房间并进入游戏开服。");
        return false;
    }
    if (core)
        *core = QStringLiteral("terracotta");
    if (joinInfo)
        *joinInfo = m_client->roomCode();
    return true;
}

void MultiplayerPage::onPublishToLobbyClicked()
{
    if (m_publishedRoomId > 0)
    {
        const AppMessageBox::StandardButton answer = AppMessageBox::question(
            this, tr("取消发布"), tr("要把当前房间从联机大厅下架吗？"),
            AppMessageBox::StandardButtons(AppMessageBox::Yes | AppMessageBox::No),
            AppMessageBox::No);
        if (answer == AppMessageBox::Yes)
            stopPublishHeartbeat(true, true);
        return;
    }

    if (!ForumAccountService::instance()->isLoggedIn())
    {
        showInfoDialog(tr("需要登录论坛"),
                       tr("发布到联机大厅需要先登录方块盒子论坛账号。登录后再回来点一次即可。"));
        emit forumLoginRequested();
        return;
    }

    publishRoomWithDialog();
}

void MultiplayerPage::onCreateShareLinkClicked()
{
    QString core;
    QString joinInfo;
    QString address;
    QString error;
    if (!currentPublishTarget(&core, &joinInfo, &address, &error))
    {
        showInfoDialog(tr("现在还不能分享"), error);
        return;
    }

    // 同一个房间已经生成过：直接把那条链接再弹出来，不用重复上传
    if (!m_shareToken.isEmpty() && core == m_shareCore && joinInfo == m_shareJoin)
    {
        showShareLinkDialog(tr("%1 的联机房间").arg(latestSaveDisplayName()),
                            LobbyShareLink::webShareUrl(m_shareToken), m_shareDescription);
        return;
    }

    // 生成前先问一句介绍：朋友打开邀请页时能看到，留空也能创建
    QString description;
    if (!promptShareIntro(&description))
        return;

    ForumLobbyRoom room;
    room.core = core;
    room.coreLabel = ForumLobbyService::coreDisplayName(core);
    if (core == QStringLiteral("terracotta"))
        room.inviteCode = joinInfo;
    else
        room.address = address;
    // 房间名用存档名：朋友看到的是「新世界 的联机房间」，而不是一串实例名
    room.title = tr("%1 的联机房间").arg(latestSaveDisplayName());
    room.description = description;
    // 分享的房间房主就是自己，加入方的提示会显示这个名字（没登录才退回存档名）
    room.host.displayName = ForumAccountService::instance()->playerName().trimmed().isEmpty()
                                ? latestSaveDisplayName()
                                : ForumAccountService::instance()->playerName().trimmed();
    room.gameVersion = m_instanceVersion;
    room.loader = m_instanceLoader;
    room.mods = instanceMods();
    // 辅助模组 / 资源包 / 光影包也一起传上去：对方加入时能按需勾选安装
    room.auxMods = instanceAuxMods();
    room.resourcePacks = LobbyInstanceMatcher::scanResourcePacks(m_instancePath);
    room.shaderPacks = LobbyInstanceMatcher::scanShaderPacks(m_instancePath);

    // 先把房间信息传到论坛，链接里只放回一小段房间号，链接就不会再长得像一串乱码；
    // 结束联机时服务端那份会被删掉，别人再打开链接就会看到「联机可能已经结束」
    if (!m_shareToken.isEmpty())
        stopShareLink(true); // 换了房间 / 核心：上一条分享先删掉，免得在服务器上留着
    m_shareBusy = true;
    refreshPublishButtons();

    const QString title = room.title;
    ForumLobbyService::instance()->createShare(
        room,
        [this, title, core, joinInfo, description](const QString &token, const QString &secret) {
            m_shareBusy = false;
            m_shareToken = token;
            m_shareSecret = secret;
            m_shareCore = core;
            m_shareJoin = joinInfo;
            m_shareDescription = description;
            startShareHeartbeat();
            refreshPublishButtons();
            showShareLinkDialog(title, LobbyShareLink::webShareUrl(token), description);
        },
        [this](const QString &message, int status) {
            Q_UNUSED(status);
            m_shareBusy = false;
            refreshPublishButtons();
            showInfoDialog(tr("分享链接没生成出来"), message);
        });
}

void MultiplayerPage::startShareHeartbeat()
{
    if (m_shareHeartbeatTimer)
        m_shareHeartbeatTimer->start();
}

void MultiplayerPage::stopShareLink(bool deleteRemote, bool refreshUi)
{
    const QString token = m_shareToken;
    const QString secret = m_shareSecret;
    m_shareToken.clear();
    m_shareSecret.clear();
    m_shareCore.clear();
    m_shareJoin.clear();
    m_shareDescription.clear();
    m_shareBusy = false;
    if (m_shareHeartbeatTimer)
        m_shareHeartbeatTimer->stop();

    if (refreshUi)
        refreshPublishButtons();

    if (!deleteRemote || token.isEmpty())
        return;

    // 联机结束就把服务端那份删掉：之后谁再打开这条链接都会看到「联机可能已经结束」
    ForumLobbyService::instance()->deleteShare(token, secret, nullptr, nullptr);
}

bool MultiplayerPage::promptShareIntro(QString *description)
{
    if (description)
        description->clear();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("创建分享链接"));
    dlg.setMinimumWidth(440);
    auto *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *hint = new QLabel(
        tr("写一句简要介绍，朋友打开邀请链接时会看到；不写也可以直接生成。"), &dlg);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *caption = new QLabel(tr("简要介绍（选填）"), &dlg);
    layout->addWidget(caption);

    auto *edit = new QLineEdit(&dlg);
    edit->setMaxLength(LobbyShareLink::kMaxDescriptionInPayload);
    edit->setPlaceholderText(tr("例如：纯生存，装了光影和优化模组，晚上八点开荒"));
    layout->addWidget(edit);

    auto *counter = new QLabel(tr("0/%1").arg(LobbyShareLink::kMaxDescriptionInPayload), &dlg);
    counter->setAlignment(Qt::AlignRight);
    layout->addWidget(counter);
    connect(edit, &QLineEdit::textChanged, &dlg, [counter](const QString &text) {
        counter->setText(tr("%1/%2").arg(text.size()).arg(LobbyShareLink::kMaxDescriptionInPayload));
    });

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto *cancelBtn = new QPushButton(tr("取消"), &dlg);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setFixedHeight(32);
    btnRow->addWidget(cancelBtn);
    auto *createBtn = new QPushButton(tr("生成链接"), &dlg);
    createBtn->setCursor(Qt::PointingHandCursor);
    createBtn->setFixedHeight(32);
    createBtn->setDefault(true);
    btnRow->addWidget(createBtn);
    layout->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(createBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted)
        return false;
    if (description)
        *description = edit->text().trimmed();
    return true;
}

void MultiplayerPage::showShareLinkDialog(const QString &title, const QString &link,
                                          const QString &description)
{
    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle(tr("分享链接已生成"));
    dlg->setMinimumWidth(460);
    auto *layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *hint = new QLabel(
        description.trimmed().isEmpty()
            ? tr("把这条链接发给朋友：打开就能看到邀请码，点「通过方块盒子一键加入」直接进房；没装方块盒子会引导去下载页。\n"
                 "房间信息存在方块盒子论坛上，这条链接在你结束联机后就会失效。")
            : tr("把这条链接发给朋友：打开就能看到房间名、你写的介绍和邀请码，点「通过方块盒子一键加入」直接进房；没装方块盒子会引导去下载页。\n"
                 "房间信息存在方块盒子论坛上，这条链接在你结束联机后就会失效。"),
        dlg);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    if (!description.trimmed().isEmpty())
    {
        auto *descLabel = new QLabel(tr("简要介绍：%1").arg(description.trimmed()), dlg);
        descLabel->setWordWrap(true);
        descLabel->setTextFormat(Qt::PlainText);
        descLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(descLabel);
    }

    auto *linkEdit = new QLineEdit(link, dlg);
    linkEdit->setReadOnly(true);
    layout->addWidget(linkEdit);

    auto *btnRow = new QHBoxLayout();
    auto *friendBtn = new QPushButton(tr("分享给论坛好友"), dlg);
    friendBtn->setCursor(Qt::PointingHandCursor);
    friendBtn->setFixedHeight(32);
    btnRow->addWidget(friendBtn);
    btnRow->addStretch();
    auto *copyBtn = new QPushButton(tr("复制链接"), dlg);
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setFixedHeight(32);
    copyBtn->setDefault(true);
    btnRow->addWidget(copyBtn);
    layout->addLayout(btnRow);

    connect(copyBtn, &QPushButton::clicked, dlg, [copyBtn, link]() {
        QApplication::clipboard()->setText(link);
        copyBtn->setText(tr("已复制"));
        QTimer::singleShot(1200, copyBtn, [copyBtn]() {
            copyBtn->setText(tr("复制链接"));
        });
    });
    connect(friendBtn, &QPushButton::clicked, dlg, [dlg, title, link]() {
        ShareToFriendDialog::share(dlg, title, link, tr("联机房间"));
    });
    connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);

    dlg->setModal(false);
    dlg->show();
}

void MultiplayerPage::publishRoomWithDialog()
{
    // 首次把房间发布到联机大厅前，同样要先同意社区规范，同意一次长期生效
    if (!CommunityAgreementDialog::ensureAccepted(this))
        return;

    QString core;
    QString joinInfo;
    QString address;
    QString error;
    if (!currentPublishTarget(&core, &joinInfo, &address, &error))
    {
        showInfoDialog(tr("现在还不能发布"), error);
        return;
    }

    const QStringList mods = instanceMods();
    const QStringList auxMods = instanceAuxMods();
    const QStringList resourcePacks = LobbyInstanceMatcher::scanResourcePacks(m_instancePath);
    const QStringList shaderPacks = LobbyInstanceMatcher::scanShaderPacks(m_instancePath);

    // 新版发布对话框：房间名 / 简介 / 人数 + 封面图 + Markdown 介绍
    // + 前置整合包（在线挑选或名称+网盘链接），可勾「存为模板」下次一键填回
    LobbyPublishDialog dlg(this);
    if (!ForumLobbyService::loadPublishTemplate().isEmpty())
        dlg.setWindowTitle(tr("发布到联机大厅（已套用上次的模板）"));
    else
        dlg.setWindowTitle(tr("发布到联机大厅"));
    dlg.setDefaultTitle(tr("%1 的联机房间").arg(latestSaveDisplayName()));

    if (dlg.exec() != QDialog::Accepted)
        return;

    ForumLobbyRoom room;
    if (!dlg.validateAndFill(&room))
        return;
    room.core = core;
    room.coreLabel = ForumLobbyService::coreDisplayName(core);
    if (core == QStringLiteral("terracotta"))
        room.inviteCode = joinInfo;
    else
        room.address = address;
    room.gameVersion = m_instanceVersion;
    room.loader = m_instanceLoader;
    room.mods = mods;
    room.auxMods = auxMods;
    room.resourcePacks = resourcePacks;
    room.shaderPacks = shaderPacks;
    room.status = QStringLiteral("waiting");

    refreshPublishButtons();
    if (m_pubTerracottaBtn)
        m_pubTerracottaBtn->setText(tr("发布中…"));
    if (m_pubHongshiBtn)
        m_pubHongshiBtn->setText(tr("发布中…"));
    if (m_pubFrpBtn)
        m_pubFrpBtn->setText(tr("发布中…"));

    // 模板在对话框还活着时就抽出来（发布回调是异步的，不能引用栈上的对话框）
    const bool saveTemplate = dlg.shouldSaveTemplate();
    const ForumLobbyRoom templateRoom = room;

    ForumLobbyService::instance()->publishRoom(
        room,
        [this, templateRoom, saveTemplate](const ForumLobbyRoom &published) {
            m_publishedRoomId = published.id;
            m_publishedCore = published.core;
            m_publishedJoin = published.inviteCode.isEmpty() ? published.address
                                                             : published.inviteCode;
            startPublishHeartbeat();
            refreshPublishButtons();
            // 发布成功才算「模板定型」：把确认时抽出的这套设置写回本机
            if (saveTemplate)
            {
                ForumLobbyService::PublishTemplate tpl;
                tpl.title = templateRoom.title;
                tpl.description = templateRoom.description;
                tpl.maxPlayers = templateRoom.maxPlayers;
                tpl.cover = templateRoom.cover;
                tpl.md = templateRoom.md;
                tpl.modpackMode = templateRoom.modpackMode;
                tpl.modpackName = templateRoom.modpackName;
                tpl.modpackUrl = templateRoom.modpackUrl;
                tpl.modpackMeta = templateRoom.modpackMeta;
                ForumLobbyService::savePublishTemplate(tpl);
            }
            NotificationManager::showSuccess(
                window(), tr("已发布到联机大厅，朋友在软件里可以一键加入"));
        },
        [this](const QString &message, int status) {
            refreshPublishButtons();
            if (status == 401)
            {
                showInfoDialog(tr("需要登录论坛"),
                               tr("登录方块盒子论坛账号后才能发布到联机大厅。"));
                emit forumLoginRequested();
                return;
            }
            showInfoDialog(tr("发布失败"), message);
        });
}

void MultiplayerPage::startPublishHeartbeat()
{
    if (m_publishHeartbeatTimer)
        m_publishHeartbeatTimer->start();
}

void MultiplayerPage::stopPublishHeartbeat(bool closeRemote, bool notify)
{
    const int roomId = m_publishedRoomId;
    m_publishedRoomId = 0;
    m_publishedCore.clear();
    m_publishedJoin.clear();
    if (m_publishHeartbeatTimer)
        m_publishHeartbeatTimer->stop();
    refreshPublishButtons();

    if (!closeRemote || roomId <= 0)
        return;

    ForumLobbyService::instance()->closeRoom(
        roomId,
        [this, notify]() {
            if (notify)
                NotificationManager::showSuccess(window(), tr("已从联机大厅下架"));
        },
        nullptr);
}

void MultiplayerPage::refreshPublishButtons()
{
    const bool published = m_publishedRoomId > 0;
    QString core;
    QString joinInfo;
    QString address;
    QString error;
    const bool canPublish = currentPublishTarget(&core, &joinInfo, &address, &error);
    const bool sameRoom = published && core == m_publishedCore;

    const QString text = sameRoom ? tr("取消发布") : tr("发布到联机大厅");
    const QString tip = sameRoom
        ? tr("房间正在联机大厅展示，点这里下架")
        : (canPublish ? tr("把当前房间发到论坛联机大厅，网页端只能看邀请码，软件端可一键加入")
                      : error);

    QPushButton *buttons[] = {m_pubTerracottaBtn, m_pubHongshiBtn, m_pubFrpBtn};
    for (QPushButton *button : buttons)
    {
        if (!button)
            continue;
        button->setText(text);
        button->setEnabled(published ? (sameRoom || canPublish) : canPublish);
        button->setToolTip(tip);
        // 已在大厅展示时按钮切成主色浅底，一眼能看出「再点一下是下架」
        if (button->property("published").toBool() != sameRoom)
        {
            button->setProperty("published", sameRoom);
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
    }

    // 分享按钮跟发布一样，需要房间就绪才能点；提示也沿用同一套
    QPushButton *shareButtons[] = {m_shareTerracottaBtn, m_shareHongshiBtn, m_shareFrpBtn};
    const bool shareReady = canPublish && !m_shareToken.isEmpty() && core == m_shareCore
        && joinInfo == m_shareJoin;
    const QString shareText = m_shareBusy ? tr("生成中…") : tr("创建分享链接");
    const QString shareTip = !canPublish ? error
        : (shareReady ? tr("分享链接已经生成，再点一次可以复制；联机结束后这条链接会自动失效")
                      : tr("生成一条很短的分享链接，创建时可以写一句简要介绍，朋友打开就能看到并一键加入"));
    for (QPushButton *button : shareButtons)
    {
        if (!button)
            continue;
        button->setText(shareText);
        button->setEnabled(canPublish && !m_shareBusy);
        button->setToolTip(shareTip);
    }
}

void MultiplayerPage::ensurePublishStillValid()
{
    QString core;
    QString joinInfo;
    QString address;
    QString error;
    const bool targetOk = currentPublishTarget(&core, &joinInfo, &address, &error);

    if (m_publishedRoomId > 0
        && (!targetOk || core != m_publishedCore || joinInfo != m_publishedJoin))
    {
        stopPublishHeartbeat(true, false);
    }

    // 分享短链同理：房间没了、换了核心或换了邀请码，就让这条链接立刻失效
    if (!m_shareToken.isEmpty()
        && (!targetOk || core != m_shareCore || joinInfo != m_shareJoin))
    {
        stopShareLink(true);
    }
}

QStringList MultiplayerPage::instanceMods() const
{
    return LobbyInstanceMatcher::scanMods(m_instancePath);
}

QStringList MultiplayerPage::instanceAuxMods() const
{
    const QStringList mods = instanceMods();
    if (mods.isEmpty() || m_instancePath.isEmpty())
        return QStringList();

    // 用户在本机把模组归进了「辅助类」这类分类：带这个标签的模组算可选，
    // 加入的人不装也能进房，发布时一起告诉大厅，加入前会单独列成可选清单。
    LocalCategoryManager categories;
    categories.setInstancePath(m_instancePath);
    categories.setResourceType(QStringLiteral("mods"));

    QStringList aux;
    for (const QString &mod : mods)
    {
        const QString category = categories.categoryNameOf(mod).trimmed();
        if (category.isEmpty())
            continue;
        if (category.contains(QStringLiteral("辅助")) || category.contains(QStringLiteral("工具"))
            || category.compare(QStringLiteral("Utility"), Qt::CaseInsensitive) == 0)
        {
            aux.append(mod);
        }
    }
    return aux;
}

QString MultiplayerPage::instanceDisplayName() const
{
    if (!m_instancePath.isEmpty())
    {
        const QString name = QDir(m_instancePath).dirName();
        if (!name.isEmpty())
            return name;
    }
    return tr("本机");
}

QString MultiplayerPage::latestSaveDisplayName() const
{
    // 联机分享 / 发布到大厅时的房间名按「存档名」显示，不再用实例名：
    // 房主开的通常是自己最近玩过的那个世界，取存档目录里最后游玩的那一个，
    // 名字优先读 level.dat 里的 LevelName（游戏里显示的名字），读不到就用目录名。
    if (!m_instancePath.isEmpty())
    {
        const QDir savesDir(QDir(m_instancePath).filePath(QStringLiteral("saves")));
        if (savesDir.exists())
        {
            QString bestName;
            qint64 bestPlayed = -1;
            const QStringList dirs =
                savesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
            for (const QString &dirName : dirs)
            {
                // 被禁用的存档不会被打开到局域网，跳过
                if (dirName.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
                    continue;

                const QString levelDat =
                    savesDir.filePath(dirName + QStringLiteral("/level.dat"));
                if (!QFile::exists(levelDat))
                    continue;

                // 进入世界 / 对局域网开放都会重写 level.dat，先用它的修改时间兜底
                qint64 played = QFileInfo(levelDat).lastModified().toMSecsSinceEpoch();
                QString name = dirName;
                const auto info = LevelDatReader::readLevelDat(levelDat);
                if (info)
                {
                    if (info->lastPlayed > 0)
                        played = info->lastPlayed;
                    const QString levelName = info->levelName.trimmed();
                    if (!levelName.isEmpty())
                        name = levelName;
                }

                if (bestName.isEmpty() || played > bestPlayed)
                {
                    bestName = name;
                    bestPlayed = played;
                }
            }
            if (!bestName.isEmpty())
                return bestName;
        }
    }
    // 一个存档都没有（还没进过世界）：只能退回实例名
    return instanceDisplayName();
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
