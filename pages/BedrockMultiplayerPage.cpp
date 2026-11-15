/**
 * @file   BedrockMultiplayerPage.cpp
 * @brief  基岩版实例助手联机页面实现
 * @author BlockBox Team
 */

#include "BedrockMultiplayerPage.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSize>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/NotificationManager.h"
#include "pages/MultiplayerUiParts.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/GravityCone/GravityConeClient.h"
#include "utils/IconHelper.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

// 版式零件与整页令牌样式与 Java 版联机页共用（见 MultiplayerUiParts.h）
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

BedrockMultiplayerPage::BedrockMultiplayerPage(QWidget *parent)
    : QWidget(parent)
    , m_client(GravityConeClient::instance())
    , m_stack(nullptr)
    , m_topBar(nullptr)
    , m_coreLabel(nullptr)
    , m_updateBtn(nullptr)
    , m_homePage(nullptr)
    , m_statusDot(nullptr)
    , m_statusLabel(nullptr)
    , m_progressHint(nullptr)
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
    , m_addressCard(nullptr)
    , m_addressLabel(nullptr)
    , m_roomCodeLabel(nullptr)
    , m_copyCodeBtn(nullptr)
    , m_leaveBtn(nullptr)
    , m_playersTitle(nullptr)
    , m_playersScroll(nullptr)
    , m_playersContainer(nullptr)
    , m_playersLayout(nullptr)
{
    initUI();
    applyThemeStyles();

    connect(m_client, &GravityConeClient::stateChanged, this, &BedrockMultiplayerPage::refreshPage);
    connect(m_client, &GravityConeClient::playersChanged, this, &BedrockMultiplayerPage::refreshPlayerList);
    connect(m_client, &GravityConeClient::installStatusChanged, this, &BedrockMultiplayerPage::refreshInstallStatus);
    connect(m_client, &GravityConeClient::errorOccurred, this, [this](const QString &err) {
        // 出错不弹窗：左下角通知说一句，顶部状态条也会跟着变成异常说明
        NotificationManager::showError(window(), err);
    });
    // 核心在加入/创建过程中会推送多条进度：就地显示在状态条下面一行，
    // 不再开窗口，也不会出现「每条进度一个弹窗」的轰炸
    connect(m_client, &GravityConeClient::progressUpdated, this,
            [this](const QString &, const QString &message) {
                if (!m_progressHint)
                    return;
                m_progressHint->setText(message);
                m_progressHint->setVisible(!message.trimmed().isEmpty());
            });

    // 换主题色、或者亮色/暗色整体切换时都要重算样式
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &BedrockMultiplayerPage::applyThemeStyles);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &BedrockMultiplayerPage::applyThemeStyles);

    // 刷新定时器（用于更新联机时长）
    QTimer *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, &BedrockMultiplayerPage::refreshStatusBar);
    timer->start();

    refreshPage();
    refreshInstallStatus();
}

BedrockMultiplayerPage::~BedrockMultiplayerPage() = default;

void BedrockMultiplayerPage::initUI()
{
    // 命名 + 允许样式表铺底：联机页自己铺主题底色，不靠外层窗口的调色板
    setObjectName(QStringLiteral("bedrockMultiplayerRoot"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 12, 14, 0);
    mainLayout->setSpacing(0);

    // ---- 顶部：核心名 + 更新按钮（基岩版核心固定为 GravityCone） ----
    m_topBar = new QWidget(this);
    m_topBar->setObjectName(QStringLiteral("multiplayerTopBar"));
    auto *topLayout = new QHBoxLayout(m_topBar);
    topLayout->setContentsMargins(0, 0, 0, 10);
    topLayout->setSpacing(8);

    auto *coreCaption = new QLabel(tr("核心"), m_topBar);
    coreCaption->setObjectName(QStringLiteral("mpTopCaption"));
    topLayout->addWidget(coreCaption);

    m_coreLabel = new QLabel(tr("GravityCone"), m_topBar);
    m_coreLabel->setObjectName(QStringLiteral("multiplayerCoreBadge"));
    m_coreLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_coreLabel->setFixedHeight(32);
    m_coreLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_updateBtn = new QPushButton(tr("更新"), m_topBar);
    m_updateBtn->setObjectName(QStringLiteral("multiplayerUpdateBtn"));
    m_updateBtn->setCursor(Qt::PointingHandCursor);
    m_updateBtn->setFixedHeight(32);
    m_updateBtn->setMinimumWidth(88);
    m_updateBtn->setToolTip(tr("检查并下载最新版本联机核心"));

    topLayout->addWidget(m_coreLabel, 1);
    topLayout->addWidget(m_updateBtn);
    mainLayout->addWidget(m_topBar);

    auto *topDivider = new QFrame(this);
    topDivider->setObjectName(QStringLiteral("mpTopDivider"));
    topDivider->setFixedHeight(1);
    mainLayout->addWidget(topDivider);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("bedrockMultiplayerStack"));

    m_homePage = new QWidget();
    m_homePage->setObjectName(QStringLiteral("bedrockMultiplayerHome"));
    buildHomePage(m_homePage);
    m_stack->addWidget(m_homePage);

    m_roomPage = new QWidget();
    m_roomPage->setObjectName(QStringLiteral("bedrockMultiplayerRoom"));
    buildRoomPage(m_roomPage);
    m_stack->addWidget(m_roomPage);

    mainLayout->addWidget(m_stack, 1);

    m_stack->setCurrentIndex(0);
}

void BedrockMultiplayerPage::buildHomePage(QWidget *page)
{
    const ScrollPage parts = makeScrollPage(page);

    // ---- 顶部状态条：核心装没装、跑没跑，一眼看到 ----
    parts.body->addWidget(makeStatusCard(page, m_statusDot, m_statusLabel,
                                         QStringLiteral("multiplayerStatusLabel")));

    // ---- 说明卡片 ----
    QFrame *noteCard = makeNoteCard(page, tr("关于 GravityCone"),
                                    m_descLabel, QStringLiteral("multiplayerDescLabel"));
    m_descLabel->setText(
        tr("基岩版实例助手可以使用 GravityCone 联机，房主创建房间后与朋友共享邀请码，"
           "即可跨越局域网限制一起游玩基岩版。<br><br>"
           "点「创建房间」后请在游戏里进入世界并允许好友加入，房间开好后邀请码会显示在下一页。<br><br>"
           "GravityCone 是第三方开源软件（<a href=\"https://github.com/Tianpao/GravityCone\">GitHub 仓库</a>），"
           "与本软件无关，如遇到问题可前往该仓库反馈。<br><br>"
           "联机使用 P2P 双端直连技术，无中转，最终联机体验由联机参与者决定。<br><br>"
           "在联机过程中，请严格遵守当地法律法规。<br><br>"
           "感谢了解与配合，祝你联机愉快！")
    );
    parts.body->addWidget(noteCard);

    // 核心推送的建房/进房进度：就地一行小字，不弹窗
    m_progressHint = makeHintLabel(QString(), page);
    m_progressHint->setVisible(false);
    parts.body->addWidget(m_progressHint);
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
    m_joinCodeEdit->setPlaceholderText(tr("粘贴好友发来的邀请码，如 P/XXXX-XXXX-XXXX-XXXX"));
    parts.outer->addWidget(m_joinCard);

    parts.outer->addLayout(btnRow);

    // 信号连接
    connect(m_updateBtn, &QPushButton::clicked, this, [this]() {
        const auto status = m_client->installStatus();
        if (status == GravityConeClient::InstallStatus::Downloading
            || status == GravityConeClient::InstallStatus::CheckingUpdate)
            return;
        if (status == GravityConeClient::InstallStatus::UpToDate
            || status == GravityConeClient::InstallStatus::Ready) {
            m_client->checkUpdate();
            return;
        }
        m_client->downloadLatest();
    });

    connect(m_joinBtn, &QPushButton::clicked, this, [this]() {
        // 核心出问题 / 还没启动时按钮本身就是「重试」：先把核心拉起来
        const auto state = m_client->state();
        if (state == GravityConeClient::State::Exception) {
            m_client->leaveRoom();
            return;
        }
        const bool coreStarting = state == GravityConeClient::State::Fatal
            || (state == GravityConeClient::State::NotInstalled && !m_client->isProcessRunning());
        if (coreStarting)
            m_client->start();
        // 邀请码就在页面里输，不等核心就绪
        showJoinCard();
        if (coreStarting || state == GravityConeClient::State::Launching)
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
        // 建房：进度就地显示在状态条下面，不再弹「正在创建房间」的窗口
        const auto state = m_client->state();
        if (m_client->installedVersion().isEmpty() && !m_client->isProcessRunning()) {
            NotificationManager::showError(window(), tr("还没安装 GravityCone 联机核心，先点右上角「更新」下载。"));
            return;
        }
        if (state == GravityConeClient::State::Exception
            || state == GravityConeClient::State::Fatal) {
            m_client->leaveRoom();
            return;
        }
        if (state == GravityConeClient::State::NotInstalled && !m_client->isProcessRunning()) {
            m_client->start();
            return;
        }
        if (state == GravityConeClient::State::Launching) {
            NotificationManager::showInfo(window(), tr("联机核心正在启动，马上就好…"));
            return;
        }
        if (state != GravityConeClient::State::Waiting) {
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

void BedrockMultiplayerPage::showJoinCard()
{
    if (!m_joinCard)
        return;
    setJoinHintText(QString());
    m_joinCard->setVisible(true);
    if (m_joinCodeEdit)
        m_joinCodeEdit->setFocus();
}

void BedrockMultiplayerPage::hideJoinCard()
{
    if (!m_joinCard)
        return;
    m_joinCard->setVisible(false);
    setJoinHintText(QString());
    if (m_joinCodeEdit)
        m_joinCodeEdit->clear();
}

void BedrockMultiplayerPage::submitJoinCode()
{
    const QString code = m_joinCodeEdit ? m_joinCodeEdit->text().trimmed() : QString();
    if (code.isEmpty()) {
        setJoinHintText(tr("先粘贴好友发来的邀请码。"), true);
        if (m_joinCodeEdit)
            m_joinCodeEdit->setFocus();
        return;
    }
    if (!GravityConeClient::verifyRoomCode(code)) {
        setJoinHintText(tr("邀请码格式不对，应为 P/XXXX-XXXX-XXXX-XXXX。"), true);
        return;
    }

    const auto state = m_client->state();
    if (m_client->installedVersion().isEmpty() && !m_client->isProcessRunning()) {
        setJoinHintText(tr("还没安装 GravityCone 联机核心，先点右上角「更新」下载。"), true);
        return;
    }
    if (state == GravityConeClient::State::Launching
        || (state == GravityConeClient::State::NotInstalled && !m_client->isProcessRunning())) {
        setJoinHintText(tr("联机核心正在启动，稍等一下再点「加入」。"), true);
        return;
    }
    if (state == GravityConeClient::State::Exception || state == GravityConeClient::State::Fatal) {
        setJoinHintText(tr("联机核心状态异常，先点一次「重试」恢复。"), true);
        return;
    }
    if (state != GravityConeClient::State::Waiting) {
        setJoinHintText(tr("已经在房间里了，先离开当前房间再加入。"), true);
        return;
    }
    m_client->joinRoom(code, localPlayerName());
    hideJoinCard();
}

void BedrockMultiplayerPage::setJoinHintText(const QString &text, bool error)
{
    if (!m_joinHint)
        return;
    m_joinHint->setText(text.trimmed().isEmpty()
                            ? tr("邀请码由房主创建房间后生成，形如 P/XXXX-XXXX-XXXX-XXXX。")
                            : text);
    MultiplayerUi::setJoinHintError(m_joinHint, error);
}

void BedrockMultiplayerPage::buildRoomPage(QWidget *page)
{
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 12, 0, 12);
    outer->setSpacing(12);

    // ---- 顶部：状态小圆点 + 联机时长 ----
    outer->addLayout(makeLatencyRow(page, m_latencyDot, m_latencyLabel));
    m_latencyLabel->setAlignment(Qt::AlignCenter);

    // ---- 服务器地址（房客模式）：在游戏中输入此地址即可进入 ----
    m_addressCard = makeCard(QStringLiteral("mpCodeCard"), page);
    auto *addressRow = new QVBoxLayout(m_addressCard);
    addressRow->setContentsMargins(16, 12, 16, 12);
    addressRow->setSpacing(2);

    auto *addressCaption = new QLabel(tr("游戏内服务器地址"), m_addressCard);
    addressCaption->setObjectName(QStringLiteral("mpCodeCaption"));
    addressRow->addWidget(addressCaption);

    m_addressLabel = new QLabel(m_addressCard);
    m_addressLabel->setObjectName(QStringLiteral("multiplayerAddressLabel"));
    m_addressLabel->setWordWrap(true);
    m_addressLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addressRow->addWidget(m_addressLabel);
    m_addressCard->setVisible(false);
    outer->addWidget(m_addressCard);

    // ---- 房间邀请码卡片 ----
    QFrame *codeCard = makeCard(QStringLiteral("mpCodeCard"), page);
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

    // ---- 离开房间按钮 ----
    m_leaveBtn = new QPushButton(tr("离开房间"), page);
    m_leaveBtn->setObjectName(QStringLiteral("multiplayerLeaveBtn"));
    m_leaveBtn->setCursor(Qt::PointingHandCursor);
    m_leaveBtn->setFixedHeight(44);
    outer->addWidget(m_leaveBtn);

    connect(m_copyCodeBtn, &QPushButton::clicked, this, [this]() {
        const QString code = m_client->roomCode();
        if (code.isEmpty())
            return;
        QApplication::clipboard()->setText(code);
        m_copyCodeBtn->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/copy.svg"), QColor(76, 175, 80), 16));
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

void BedrockMultiplayerPage::applyThemeStyles()
{
    // 与 Java 版联机页共用同一份令牌样式（对象名一致），见 MultiplayerUiParts.h
    setStyleSheet(StyleKit::resolve(MultiplayerUi::pageStyleSheet()));

    // 说明卡片里的超链接：暗色主题下默认的深蓝几乎看不见，按主题重新上色
    const bool darkTheme = ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
    const QColor linkColor(StyleKit::resolve(darkTheme ? QStringLiteral("@LINK_HOVER@")
                                                       : QStringLiteral("@LINK@")));
    for (QLabel *desc : findChildren<QLabel *>(QStringLiteral("multiplayerDescLabel")))
        MultiplayerUi::applyDescriptionLinkColor(desc, linkColor);
}
void BedrockMultiplayerPage::refreshPage()
{
    const auto state = m_client->state();
    const bool inRoom = m_client->isInRoom();

    // 加入/创建已有结果（进房、失败或回到空闲）时清掉那行进度小字
    if (m_progressHint
        && (inRoom || state == GravityConeClient::State::Waiting
            || state == GravityConeClient::State::Exception
            || state == GravityConeClient::State::Fatal
            || state == GravityConeClient::State::NotInstalled)) {
        m_progressHint->clear();
        m_progressHint->setVisible(false);
    }

    if (inRoom) {
        // 已经进房：邀请码输入卡自动收起
        hideJoinCard();
        if (m_stack->currentIndex() != 1) {
            m_stack->setCurrentIndex(1);
            m_roomEnterTime = QDateTime::currentDateTime();
        }
    } else if (m_stack->currentIndex() != 0) {
        m_stack->setCurrentIndex(0);
    }

    if (m_statusLabel) {
        QString text;
        switch (state) {
        case GravityConeClient::State::NotInstalled:
            text = m_client->installedVersion().isEmpty()
                ? tr("未安装联机核心")
                : tr("联机核心未运行（已安装 v%1）").arg(m_client->installedVersion());
            break;
        case GravityConeClient::State::Launching:
            text = tr("正在启动联机核心...");
            break;
        case GravityConeClient::State::Waiting:
            text = m_client->isProcessRunning()
                ? (m_client->gravityConeVersion().isEmpty()
                    ? tr("联机核心已就绪")
                    : tr("联机核心已就绪（v%1）").arg(m_client->gravityConeVersion()))
                : tr("联机核心未运行");
            break;
        case GravityConeClient::State::HostStarting:
            text = tr("正在创建基岩版联机房间...");
            break;
        case GravityConeClient::State::HostOk:
            text = tr("房间已就绪，分享邀请码给朋友加入");
            break;
        case GravityConeClient::State::GuestStarting:
            text = tr("正在加入房间...");
            break;
        case GravityConeClient::State::GuestOk:
            text = tr("已加入房间，在游戏中输入下方地址即可进入");
            break;
        case GravityConeClient::State::Exception:
            text = tr("联机异常：%1").arg(m_client->errorMessage());
            break;
        case GravityConeClient::State::Fatal:
            text = tr("联机核心启动失败");
            break;
        }
        m_statusLabel->setText(text);

        // 状态圆点跟着状态走：绿=可用，黄=进行中/待操作，红=出错
        QString tone;
        switch (state) {
        case GravityConeClient::State::Waiting:
            tone = m_client->isProcessRunning() ? QStringLiteral("ok") : QStringLiteral("idle");
            break;
        case GravityConeClient::State::HostOk:
        case GravityConeClient::State::GuestOk:
            tone = QStringLiteral("ok");
            break;
        case GravityConeClient::State::Exception:
        case GravityConeClient::State::Fatal:
            tone = QStringLiteral("err");
            break;
        default:
            tone = QStringLiteral("warn");
            break;
        }
        setDotTone(m_statusDot, tone);
    }

    // 首页按钮可用性
    if (m_joinBtn && m_createBtn) {
        const bool canAct = (state == GravityConeClient::State::Waiting)
            || (state == GravityConeClient::State::NotInstalled
                && !m_client->installedVersion().isEmpty())
            || (state == GravityConeClient::State::Exception
                && m_client->isProcessRunning())
            || (state == GravityConeClient::State::Fatal); // 启动失败允许点「重试」再拉起核心
        m_joinBtn->setEnabled(canAct);
        m_createBtn->setEnabled(canAct);

        if (state == GravityConeClient::State::Exception
            || state == GravityConeClient::State::Fatal) {
            m_createBtn->setText(tr("重试"));
            m_joinBtn->setText(tr("重试"));
        } else {
            m_createBtn->setText(tr("创建房间"));
            m_joinBtn->setText(tr("加入房间"));
        }
    }

    // 房间邀请码
    if (m_roomCodeLabel) {
        const QString code = m_client->roomCode();
        m_roomCodeLabel->setText(code.isEmpty() ? tr("正在生成邀请码...") : code);
    }

    refreshStatusBar();
    refreshPlayerList();
}

void BedrockMultiplayerPage::refreshPlayerList()
{
    if (!m_playersLayout)
        return;

    while (m_playersLayout->count() > 1) {
        QLayoutItem *item = m_playersLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const QList<GravityConeClient::Player> players = m_client->players();

    // 玩家列表标题带上人数
    if (m_playersTitle) {
        m_playersTitle->setText(players.isEmpty() ? tr("玩家")
                                                  : tr("玩家 · %1").arg(players.size()));
    }

    // 一个人都没进来时给一句提示，列表不至于空得莫名
    if (players.isEmpty()) {
        auto *emptyLabel = new QLabel(tr("还没有人加入，把邀请码发给好友吧"));
        emptyLabel->setObjectName(QStringLiteral("mpEmptyPlayers"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);
        m_playersLayout->insertWidget(m_playersLayout->count() - 1, emptyLabel);
        return;
    }

    // 本机的方块盒子身份：登录后自己的头像 / 昵称覆盖进房间时报的游戏名
    ForumAccountService *forum = ForumAccountService::instance();
    const bool loggedIn = forum->isLoggedIn();
    const QString reportedName = m_client->localPlayerName().trimmed();
    QString selfName;
    QString selfAvatar;
    if (loggedIn) {
        selfName = forum->playerName();
        if (!selfName.isEmpty())
            selfAvatar = forum->user().avatar;
    }

    for (const auto &p : players) {
        // 和进房间时报给核心的名字比对，认出哪张卡片是自己
        const bool isSelf = !reportedName.isEmpty()
                            && p.name.trimmed().compare(reportedName, Qt::CaseInsensitive) == 0;

        // 自己在卡片上显示账户名（没登录时就是进房间报的名字）
        const QString shownName =
            (isSelf && !selfName.isEmpty()) ? selfName : p.name.trimmed();

        QWidget *card = new QWidget();
        card->setProperty("cardRole", "mpContainer");
        card->setFixedHeight(64);

        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(10, 8, 10, 8);
        cardLayout->setSpacing(10);

        // 头像底板：首字母圆；自己的账户有头像时再换成账户头像
        QLabel *avatarLabel = new QLabel();
        avatarLabel->setFixedSize(36, 36);
        avatarLabel->setAlignment(Qt::AlignCenter);
        const QColor avatarColor = isSelf
            ? QColor(76, 175, 80)
            : (p.isRoomHost ? QColor(255, 152, 0) : QColor(33, 150, 243));
        avatarLabel->setPixmap(letterAvatarPixmap(avatarColor, shownName, 36,
                                                  avatarLabel->devicePixelRatioF()));
        if (isSelf && !selfAvatar.trimmed().isEmpty()) {
            const QString avatarUrl = forum->absoluteUrl(selfAvatar.trimmed());
            if (!avatarUrl.isEmpty()) {
                QPointer<QLabel> guard(avatarLabel);
                ForumImageStore::instance()->requestImage(
                    avatarUrl, [guard, avatarColor](const QImage &image) {
                        if (guard.isNull() || image.isNull())
                            return;
                        guard->setPixmap(roundAvatarPixmap(image, avatarColor, 36,
                                                           guard->devicePixelRatioF()));
                    });
            }
        }
        cardLayout->addWidget(avatarLabel);

        auto *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(4);
        infoLayout->setContentsMargins(0, 0, 0, 0);

        QLabel *nameLabel = new QLabel(shownName);
        nameLabel->setProperty("cardRole", "mpName");
        if (shownName != p.name)
            nameLabel->setToolTip(tr("%1\n游戏内名字：%2").arg(shownName, p.name));
        else
            nameLabel->setToolTip(shownName);
        QFontMetrics fm(nameLabel->font());
        nameLabel->setText(fm.elidedText(shownName, Qt::ElideRight, 180));
        infoLayout->addWidget(nameLabel);

        auto *chipsLayout = new QHBoxLayout();
        chipsLayout->setSpacing(4);
        chipsLayout->setContentsMargins(0, 0, 0, 0);

        QLabel *roleChip = new QLabel(p.isRoomHost ? tr("房主") : tr("玩家"));
        roleChip->setProperty("cardRole", "mpChip");
        if (isSelf)
            roleChip->setProperty("accent", "self");
        chipsLayout->addWidget(roleChip);

        if (isSelf) {
            QLabel *selfChip = new QLabel(tr("本机"));
            selfChip->setProperty("cardRole", "mpChip");
            selfChip->setProperty("accent", "self");
            chipsLayout->addWidget(selfChip);
        }

        if (isSelf && loggedIn) {
            QLabel *accountChip = new QLabel(tr("方块盒子"));
            accountChip->setProperty("cardRole", "mpChip");
            accountChip->setProperty("accent", "account");
            accountChip->setToolTip(tr("头像和昵称取自当前登录的方块盒子账户"));
            chipsLayout->addWidget(accountChip);
        }

        QLabel *onlineChip = new QLabel(tr("在线"));
        onlineChip->setProperty("cardRole", "mpChip");
        chipsLayout->addWidget(onlineChip);

        chipsLayout->addStretch();
        infoLayout->addLayout(chipsLayout);

        cardLayout->addLayout(infoLayout, 1);

        // 复制玩家名按钮
        QPushButton *copyNameBtn = new QPushButton();
        copyNameBtn->setProperty("cardRole", "mpActionBtn");
        copyNameBtn->setFixedSize(32, 32);
        copyNameBtn->setCursor(Qt::PointingHandCursor);
        copyNameBtn->setToolTip(tr("复制玩家名"));
        copyNameBtn->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/copy.svg"),
            ThemeManager::instance()->currentThemeColor(), 15));
        copyNameBtn->setIconSize(QSize(15, 15));

        const QString playerName = shownName;
        connect(copyNameBtn, &QPushButton::clicked, this, [playerName]() {
            QApplication::clipboard()->setText(playerName);
        });

        cardLayout->addWidget(copyNameBtn);

        m_playersLayout->insertWidget(m_playersLayout->count() - 1, card);
    }
}

QString BedrockMultiplayerPage::localPlayerName() const
{
    const QString name = ForumAccountService::instance()->playerName();
    if (!name.isEmpty())
        return name;
    return tr("BlockBox 玩家");
}

void BedrockMultiplayerPage::refreshStatusBar()
{
    if (!m_latencyLabel || !m_addressLabel)
        return;

    if (!m_client->isInRoom()) {
        m_latencyLabel->clear();
        m_addressLabel->clear();
        if (m_addressCard)
            m_addressCard->setVisible(false);
        return;
    }

    QString text;
    if (m_roomEnterTime.isValid()) {
        const qint64 seconds = m_roomEnterTime.secsTo(QDateTime::currentDateTime());
        const int h = int(seconds / 3600);
        const int m = int((seconds % 3600) / 60);
        const int s = int(seconds % 60);
        const QString timeStr = (h > 0)
            ? QStringLiteral("%1:%2:%3").arg(h, 2, 10, QLatin1Char('0'))
                .arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
            : QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0'))
                .arg(s, 2, 10, QLatin1Char('0'));

        QString statusText;
        switch (m_client->state()) {
        case GravityConeClient::State::HostStarting:
            statusText = tr("创建房间中");
            break;
        case GravityConeClient::State::HostOk:
            statusText = tr("房主就绪");
            break;
        case GravityConeClient::State::GuestStarting:
            statusText = tr("加入中");
            break;
        case GravityConeClient::State::GuestOk:
            statusText = tr("已连接");
            break;
        default:
            statusText = tr("联机中");
            break;
        }
        text = tr("%1 | 联机时间 %2").arg(statusText, timeStr);
    }
    m_latencyLabel->setText(text);

    // 服务器地址：房主显示提示，房客显示游戏内要输入的地址；没有内容就整块收起
    if (m_client->state() == GravityConeClient::State::GuestOk) {
        const QString addr = m_client->serverAddress();
        const quint16 port = m_client->gamePort();
        if (!addr.isEmpty()) {
            m_addressLabel->setText(QStringLiteral("%1:%2").arg(addr).arg(port));
        } else {
            m_addressLabel->clear();
        }
    } else if (m_client->state() == GravityConeClient::State::HostOk) {
        m_addressLabel->setText(tr("请在你的世界里开启「对局域网开放」，并把邀请码分享给朋友"));
    } else {
        m_addressLabel->clear();
    }
    if (m_addressCard)
        m_addressCard->setVisible(!m_addressLabel->text().isEmpty());

    // 根据连接状态切换 status 属性
    QString latencyStatus;
    switch (m_client->state()) {
    case GravityConeClient::State::HostOk:
    case GravityConeClient::State::GuestOk:
        latencyStatus = QStringLiteral("good");
        break;
    case GravityConeClient::State::HostStarting:
    case GravityConeClient::State::GuestStarting:
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

void BedrockMultiplayerPage::refreshInstallStatus()
{
    if (!m_updateBtn)
        return;
    const auto status = m_client->installStatus();
    const QString installedVer = m_client->installedVersion();
    const QString latestVer = m_client->latestVersion();

    switch (status) {
    case GravityConeClient::InstallStatus::NotInstalled:
        m_updateBtn->setText(latestVer.isEmpty() ? tr("下载") : tr("下载 v%1").arg(latestVer));
        m_updateBtn->setToolTip(tr("下载并安装 GravityCone 联机核心"));
        m_updateBtn->setEnabled(true);
        break;
    case GravityConeClient::InstallStatus::Ready:
        m_updateBtn->setText(tr("检查更新"));
        m_updateBtn->setToolTip(installedVer.isEmpty()
            ? tr("检查并下载最新版本联机核心")
            : tr("已安装 v%1，点击检查是否有新版本").arg(installedVer));
        m_updateBtn->setEnabled(true);
        break;
    case GravityConeClient::InstallStatus::CheckingUpdate:
        m_updateBtn->setText(tr("检查中"));
        m_updateBtn->setEnabled(false);
        break;
    case GravityConeClient::InstallStatus::UpdateAvailable:
        m_updateBtn->setText(tr("更新到 v%1").arg(latestVer));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(tr("当前 v%1，最新 v%2，点击更新").arg(installedVer, latestVer));
        break;
    case GravityConeClient::InstallStatus::UpToDate:
        m_updateBtn->setText(tr("已是最新"));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(tr("当前 v%1 已是最新版本，点击重新检查").arg(installedVer));
        break;
    case GravityConeClient::InstallStatus::Downloading:
        m_updateBtn->setText(tr("下载中"));
        m_updateBtn->setEnabled(false);
        break;
    case GravityConeClient::InstallStatus::DownloadFailed:
        m_updateBtn->setText(tr("重试"));
        m_updateBtn->setEnabled(true);
        m_updateBtn->setToolTip(tr("下载失败，点击重试"));
        break;
    }
}
