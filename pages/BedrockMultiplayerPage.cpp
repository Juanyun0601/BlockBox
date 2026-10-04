/**
 * @file   BedrockMultiplayerPage.cpp
 * @brief  基岩版实例助手联机页面实现
 * @author BlockBox Team
 */

#include "BedrockMultiplayerPage.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSize>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "components/AppInputDialog.h"
#include "components/AppMessageBox.h"
#include "utils/GravityCone/GravityConeClient.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

BedrockMultiplayerPage::BedrockMultiplayerPage(QWidget *parent)
    : QWidget(parent)
    , m_client(GravityConeClient::instance())
    , m_stack(nullptr)
    , m_homePage(nullptr)
    , m_statusLabel(nullptr)
    , m_updateBtn(nullptr)
    , m_descLabel(nullptr)
    , m_joinBtn(nullptr)
    , m_createBtn(nullptr)
    , m_roomPage(nullptr)
    , m_latencyLabel(nullptr)
    , m_addressLabel(nullptr)
    , m_roomCodeLabel(nullptr)
    , m_copyCodeBtn(nullptr)
    , m_leaveBtn(nullptr)
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
        showInfoDialog(tr("联机错误"), err);
    });
    connect(m_client, &GravityConeClient::progressUpdated, this, [this](const QString &, const QString &message) {
        showInfoDialog(tr("正在加入"), message);
    });

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
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
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

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
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    // ---- 顶部区域：核心选择框 + 更新按钮 ----
    auto *topBar = new QHBoxLayout();
    topBar->setSpacing(6);

    auto *coreCombo = new QLabel(page);
    coreCombo->setObjectName(QStringLiteral("bedrockMpCoreLabel"));
    coreCombo->setText(tr("GravityCone"));
    coreCombo->setAlignment(Qt::AlignCenter);
    coreCombo->setFixedHeight(28);
    coreCombo->setMinimumWidth(140);
    coreCombo->setMaximumWidth(220);

    m_updateBtn = new QPushButton(tr("更新"), page);
    m_updateBtn->setObjectName(QStringLiteral("bedrockMpUpdateBtn"));
    m_updateBtn->setCursor(Qt::PointingHandCursor);
    m_updateBtn->setFixedHeight(28);
    m_updateBtn->setToolTip(tr("检查并下载最新版本联机核心"));

    topBar->addWidget(coreCombo, 1);
    topBar->addWidget(m_updateBtn);
    layout->addLayout(topBar);

    // 顶部状态文字
    m_statusLabel = new QLabel(page);
    m_statusLabel->setObjectName(QStringLiteral("bedrockMpStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);

    // ---- 中间区域：说明信息 ----
    m_descLabel = new QLabel(page);
    m_descLabel->setObjectName(QStringLiteral("bedrockMpDescLabel"));
    m_descLabel->setWordWrap(true);
    m_descLabel->setTextFormat(Qt::RichText);
    m_descLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_descLabel->setOpenExternalLinks(true);
    m_descLabel->setText(
        tr("基岩版实例助手可以使用 GravityCone 联机，房主创建房间后与朋友共享邀请码，"
           "即可跨越局域网限制一起游玩基岩版。<br><br>"
           "GravityCone 是第三方开源软件（<a href=\"https://github.com/Tianpao/GravityCone\">GitHub 仓库</a>），"
           "与本软件无关，如遇到问题可前往该仓库反馈。<br><br>"
           "联机使用 P2P 双端直连技术，无中转，最终联机体验由联机参与者决定。<br><br>"
           "在联机过程中，请严格遵守当地法律法规。<br><br>"
           "感谢了解与配合，祝你联机愉快！")
    );
    layout->addWidget(m_descLabel, 1);

    // ---- 加入房间 / 创建房间按钮 ----
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);

    m_joinBtn = new QPushButton(tr("加入房间"), page);
    m_joinBtn->setObjectName(QStringLiteral("bedrockMpJoinBtn"));
    m_joinBtn->setCursor(Qt::PointingHandCursor);
    m_joinBtn->setFixedHeight(34);

    m_createBtn = new QPushButton(tr("创建房间"), page);
    m_createBtn->setObjectName(QStringLiteral("bedrockMpCreateBtn"));
    m_createBtn->setCursor(Qt::PointingHandCursor);
    m_createBtn->setFixedHeight(34);

    btnRow->addWidget(m_joinBtn, 1);
    btnRow->addWidget(m_createBtn, 1);
    layout->addLayout(btnRow);

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
        if (!m_client->isProcessRunning() && m_client->installedVersion().isEmpty()) {
            showInfoDialog(tr("未安装联机核心"),
                tr("尚未安装 GravityCone 联机核心，请先点击「下载」按钮下载安装。"));
            return;
        }
        // Exception 状态下点击 = 重试：调用 leaveRoom 回到 Waiting
        if (m_client->state() == GravityConeClient::State::Exception) {
            m_client->leaveRoom();
            return;
        }
        // 已安装但进程未启动：自动启动
        if (m_client->state() == GravityConeClient::State::NotInstalled
            && !m_client->isProcessRunning()) {
            m_client->start();
            return;
        }
        if (m_client->state() == GravityConeClient::State::Launching) {
            showInfoDialog(tr("正在启动"), tr("联机核心正在启动，请稍候..."));
            return;
        }
        if (m_client->state() == GravityConeClient::State::Fatal) {
            showInfoDialog(tr("启动失败"), tr("联机核心启动失败，请重试或重新下载安装。"));
            return;
        }
        if (m_client->state() != GravityConeClient::State::Waiting) {
            showInfoDialog(tr("状态异常"), tr("当前已在房间中，请先离开当前房间。"));
            return;
        }
        bool ok = false;
        const QString code = AppInputDialog::getText(
            this, tr("加入房间"), tr("请输入邀请码："),
            QLineEdit::Normal, QString(), &ok);
        if (!ok || code.trimmed().isEmpty())
            return;

        if (!GravityConeClient::verifyRoomCode(code)) {
            showInfoDialog(tr("邀请码无效"),
                tr("基岩版邀请码格式错误，应为 P/XXXX-XXXX-XXXX-XXXX。"));
            return;
        }

        m_client->joinRoom(code, tr("BlockBox 玩家"));
    });

    connect(m_createBtn, &QPushButton::clicked, this, [this]() {
        if (!m_client->isProcessRunning() && m_client->installedVersion().isEmpty()) {
            showInfoDialog(tr("未安装联机核心"),
                tr("尚未安装 GravityCone 联机核心，请先点击「下载」按钮下载安装。"));
            return;
        }
        if (m_client->state() == GravityConeClient::State::Exception) {
            m_client->leaveRoom();
            return;
        }
        if (m_client->state() == GravityConeClient::State::NotInstalled
            && !m_client->isProcessRunning()) {
            m_client->start();
            return;
        }
        if (m_client->state() == GravityConeClient::State::Launching) {
            showInfoDialog(tr("正在启动"), tr("联机核心正在启动，请稍候..."));
            return;
        }
        if (m_client->state() == GravityConeClient::State::Fatal) {
            showInfoDialog(tr("启动失败"), tr("联机核心启动失败，请重试或重新下载安装。"));
            return;
        }
        if (m_client->state() != GravityConeClient::State::Waiting) {
            showInfoDialog(tr("状态异常"), tr("当前已在房间中，请先离开当前房间。"));
            return;
        }
        m_client->createRoom(tr("BlockBox 玩家"));
        showInfoDialog(tr("正在创建房间"),
            tr("正在创建基岩版联机房间，请稍候..."));
    });
}

void BedrockMultiplayerPage::buildRoomPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // ---- 顶部：联机状态和联机时长 ----
    m_latencyLabel = new QLabel(page);
    m_latencyLabel->setObjectName(QStringLiteral("bedrockMpLatencyLabel"));
    m_latencyLabel->setAlignment(Qt::AlignCenter);
    m_latencyLabel->setWordWrap(true);
    layout->addWidget(m_latencyLabel);

    // 服务器地址（房客模式）：在游戏中输入此地址即可进入
    m_addressLabel = new QLabel(page);
    m_addressLabel->setObjectName(QStringLiteral("bedrockMpAddressLabel"));
    m_addressLabel->setAlignment(Qt::AlignCenter);
    m_addressLabel->setWordWrap(true);
    m_addressLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_addressLabel);

    // ---- 中间：大大的房间邀请码 + 复制按钮 ----
    auto *codeLayout = new QHBoxLayout();
    codeLayout->setSpacing(6);

    m_roomCodeLabel = new QLabel(page);
    m_roomCodeLabel->setObjectName(QStringLiteral("bedrockMpRoomCodeLabel"));
    m_roomCodeLabel->setAlignment(Qt::AlignCenter);
    m_roomCodeLabel->setWordWrap(true);
    m_roomCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QFont codeFont = m_roomCodeLabel->font();
    codeFont.setBold(true);
    codeFont.setPointSize(12);
    m_roomCodeLabel->setFont(codeFont);

    m_copyCodeBtn = new QPushButton(page);
    m_copyCodeBtn->setObjectName(QStringLiteral("bedrockMpCopyCodeBtn"));
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
    m_playersScroll->setObjectName(QStringLiteral("bedrockMpPlayersScroll"));
    m_playersScroll->setWidgetResizable(true);
    m_playersScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_playersScroll->setFrameShape(QFrame::NoFrame);

    m_playersContainer = new QWidget();
    m_playersContainer->setObjectName(QStringLiteral("bedrockMpPlayersContainer"));
    m_playersLayout = new QVBoxLayout(m_playersContainer);
    m_playersLayout->setContentsMargins(0, 0, 0, 0);
    m_playersLayout->setSpacing(4);
    m_playersLayout->addStretch();

    m_playersScroll->setWidget(m_playersContainer);
    layout->addWidget(m_playersScroll, 1);

    // ---- 离开房间按钮 ----
    m_leaveBtn = new QPushButton(tr("离开房间"), page);
    m_leaveBtn->setObjectName(QStringLiteral("bedrockMpLeaveBtn"));
    m_leaveBtn->setCursor(Qt::PointingHandCursor);
    m_leaveBtn->setFixedHeight(32);
    layout->addWidget(m_leaveBtn);

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
    const QString themeColor = ThemeManager::instance()->currentThemeColor();
    const QString textColor = ThemeManager::instance()->currentTextColor();
    const QString borderColor = ThemeManager::instance()->currentBorderColor();
    QColor tc(themeColor);
    const QString hoverBg = QString("rgba(%1, %2, %3, 0.12)")
        .arg(tc.red()).arg(tc.green()).arg(tc.blue());
    const bool isBmpDark = (ThemeManager::instance()->currentTheme()
                            == ThemeManager::DarkTheme);
    const QString bmpDisabledBg = isBmpDark ? "#3a3a3a" : "#f5f5f5";
    const QString bmpDisabledText = isBmpDark ? "#8a8a8a" : "#999999";

    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(
            QString("QLabel#bedrockMpStatusLabel {"
                    "  color: %1;"
                    "  font-size: 11px;"
                    "  padding: 4px;"
                    "  background: transparent;"
                    "}").arg(textColor));
    }

    if (m_descLabel) {
        m_descLabel->setStyleSheet(
            QString("QLabel#bedrockMpDescLabel {"
                    "  color: %1;"
                    "  font-size: 12px;"
                    "  padding: 8px;"
                    "  background: palette(base);"
                    "  border: 1px solid %2;"
                    "  border-radius: 6px;"
                    "}").arg(textColor, borderColor));
    }

    if (m_homePage) {
        m_homePage->setStyleSheet(
            QString(
                "QLabel#bedrockMpCoreLabel {"
                "  background: palette(base);"
                "  border: 1px solid %1;"
                "  border-radius: 4px;"
                "  color: %2;"
                "  font-size: 12px;"
                "  padding: 0 6px;"
                "}"
                "QPushButton#bedrockMpUpdateBtn {"
                "  background: palette(base);"
                "  border: 1px solid %1;"
                "  border-radius: 4px;"
                "  color: %2;"
                "  font-size: 12px;"
                "  padding: 0 10px;"
                "}"
                "QPushButton#bedrockMpUpdateBtn:hover {"
                "  background: %3;"
                "  border-color: %4;"
                "  color: %4;"
                "}"
                "QPushButton#bedrockMpUpdateBtn:disabled {"
                "  color: %6;"
                "  background: %7;"
                "}"
                "QPushButton#bedrockMpJoinBtn, QPushButton#bedrockMpCreateBtn {"
                "  background: %4;"
                "  color: white;"
                "  border: none;"
                "  border-radius: 6px;"
                "  font-size: 13px;"
                "  font-weight: 500;"
                "}"
                "QPushButton#bedrockMpJoinBtn:hover, QPushButton#bedrockMpCreateBtn:hover {"
                "  background: %5;"
                "}"
                "QPushButton#bedrockMpJoinBtn:disabled, QPushButton#bedrockMpCreateBtn:disabled {"
                "  background: %6;"
                "  color: %7;"
                "}"
            ).arg(borderColor, textColor, hoverBg, themeColor,
                  tc.darker(110).name(), bmpDisabledBg, bmpDisabledText));
    }

    if (m_roomPage) {
        m_roomPage->setStyleSheet(
            QString(
                "QLabel#bedrockMpRoomCodeLabel {"
                "  color: %3;"
                "  font-size: 14px;"
                "  font-weight: bold;"
                "  padding: 12px 8px;"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "}"
                "QLabel#bedrockMpAddressLabel {"
                "  color: %1;"
                "  font-size: 12px;"
                "  padding: 6px;"
                "  background: rgba(31, 167, 143, 0.10);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "}"
                "QPushButton#bedrockMpCopyCodeBtn {"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 4px;"
                "}"
                "QPushButton#bedrockMpCopyCodeBtn:hover {"
                "  background: %4;"
                "  border-color: %3;"
                "}"
                "QPushButton#bedrockMpLeaveBtn {"
                "  background: palette(base);"
                "  border: 1px solid %2;"
                "  border-radius: 6px;"
                "  color: #d32f2f;"
                "  font-size: 12px;"
                "}"
                "QPushButton#bedrockMpLeaveBtn:hover {"
                "  background: rgba(211, 47, 47, 0.12);"
                "  border-color: #d32f2f;"
                "}"
                "QScrollArea#bedrockMpPlayersScroll {"
                "  background: transparent;"
                "  border: none;"
                "}"
                "QWidget#bedrockMpPlayersContainer {"
                "  background: transparent;"
                "}"
            ).arg(textColor, borderColor, themeColor, hoverBg));
    }

    // 玩家卡片样式
    const QString playerCardStyle = QString(
        "QWidget[cardRole=\"bmpContainer\"] {"
        "  background: palette(base);"
        "  border: 1px solid %1;"
        "  border-radius: 6px;"
        "}"
        "QWidget[cardRole=\"bmpContainer\"]:hover {"
        "  border-color: %2;"
        "}"
        "QLabel[cardRole=\"bmpName\"] {"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  color: %3;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel[cardRole=\"bmpChip\"] {"
        "  background-color: rgba(%4, %5, %6, 26);"
        "  color: %2;"
        "  border: 1px solid %1;"
        "  border-radius: 8px;"
        "  padding: 2px 6px;"
        "  font-size: 10px;"
        "}"
        "QPushButton[cardRole=\"bmpActionBtn\"] {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QPushButton[cardRole=\"bmpActionBtn\"]:hover {"
        "  background: rgba(%4, %5, %6, 26);"
        "}"
    ).arg(borderColor, themeColor, textColor)
     .arg(tc.red()).arg(tc.green()).arg(tc.blue());

    if (m_playersContainer)
        m_playersContainer->setStyleSheet(playerCardStyle);
}

void BedrockMultiplayerPage::refreshPage()
{
    const auto state = m_client->state();
    const bool inRoom = m_client->isInRoom();

    if (inRoom) {
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
    }

    // 首页按钮可用性
    if (m_joinBtn && m_createBtn) {
        const bool canAct = (state == GravityConeClient::State::Waiting)
            || (state == GravityConeClient::State::NotInstalled
                && !m_client->installedVersion().isEmpty())
            || (state == GravityConeClient::State::Exception
                && m_client->isProcessRunning());
        m_joinBtn->setEnabled(canAct);
        m_createBtn->setEnabled(canAct);

        if (state == GravityConeClient::State::Exception) {
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
    for (const auto &p : players) {
        QWidget *card = new QWidget();
        card->setProperty("cardRole", "bmpContainer");
        card->setFixedHeight(60);

        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(8, 6, 8, 6);
        cardLayout->setSpacing(8);

        // 头像占位（首字母圆形）
        QLabel *avatarLabel = new QLabel();
        avatarLabel->setFixedSize(36, 36);
        avatarLabel->setAlignment(Qt::AlignCenter);
        QColor avatarColor = p.isRoomHost
            ? QColor(255, 152, 0)
            : QColor(76, 175, 80);
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
            const QString letter = p.name.isEmpty() ? QStringLiteral("?")
                : QString(p.name.at(0)).toUpper();
            painter.drawText(avatar.rect(), Qt::AlignCenter, letter);
        }
        avatarLabel->setPixmap(avatar);
        cardLayout->addWidget(avatarLabel);

        auto *infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(4);
        infoLayout->setContentsMargins(0, 0, 0, 0);

        QLabel *nameLabel = new QLabel(p.name);
        nameLabel->setProperty("cardRole", "bmpName");
        nameLabel->setToolTip(p.name);
        QFontMetrics fm(nameLabel->font());
        nameLabel->setText(fm.elidedText(p.name, Qt::ElideRight, 180));
        infoLayout->addWidget(nameLabel);

        auto *chipsLayout = new QHBoxLayout();
        chipsLayout->setSpacing(4);
        chipsLayout->setContentsMargins(0, 0, 0, 0);

        QLabel *roleChip = new QLabel(p.isRoomHost ? tr("房主") : tr("玩家"));
        roleChip->setProperty("cardRole", "bmpChip");
        chipsLayout->addWidget(roleChip);

        QLabel *onlineChip = new QLabel(tr("在线"));
        onlineChip->setProperty("cardRole", "bmpChip");
        chipsLayout->addWidget(onlineChip);

        chipsLayout->addStretch();
        infoLayout->addLayout(chipsLayout);

        cardLayout->addLayout(infoLayout, 1);

        // 复制玩家名按钮
        QPushButton *copyNameBtn = new QPushButton();
        copyNameBtn->setProperty("cardRole", "bmpActionBtn");
        copyNameBtn->setFixedSize(28, 28);
        copyNameBtn->setCursor(Qt::PointingHandCursor);
        copyNameBtn->setToolTip(tr("复制玩家名"));
        copyNameBtn->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/copy.svg"),
            ThemeManager::instance()->currentThemeColor(), 14));
        copyNameBtn->setIconSize(QSize(14, 14));

        const QString playerName = p.name;
        connect(copyNameBtn, &QPushButton::clicked, this, [playerName]() {
            QApplication::clipboard()->setText(playerName);
        });

        cardLayout->addWidget(copyNameBtn);

        m_playersLayout->insertWidget(m_playersLayout->count() - 1, card);
    }
}

void BedrockMultiplayerPage::refreshStatusBar()
{
    if (!m_latencyLabel || !m_addressLabel)
        return;

    if (!m_client->isInRoom()) {
        m_latencyLabel->clear();
        m_addressLabel->clear();
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

    // 服务器地址：房主显示提示，房客显示游戏内要输入的地址
    if (m_client->state() == GravityConeClient::State::GuestOk) {
        const QString addr = m_client->serverAddress();
        const quint16 port = m_client->gamePort();
        if (!addr.isEmpty()) {
            m_addressLabel->setText(tr("游戏内服务器地址：%1:%2")
                .arg(addr).arg(port));
        } else {
            m_addressLabel->clear();
        }
    } else if (m_client->state() == GravityConeClient::State::HostOk) {
        m_addressLabel->setText(tr("请在你的世界里开启「对局域网开放」，并把邀请码分享给朋友"));
    } else {
        m_addressLabel->clear();
    }

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

void BedrockMultiplayerPage::showInfoDialog(const QString &title, const QString &text)
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
