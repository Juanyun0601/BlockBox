#include "MultiplayerLobbyPage.h"

#include "components/AppMessageBox.h"
#include "components/ForumKit.h"
#include "utils/ForumAccountService.h"
#include "utils/Lobby/LobbyInstanceMatcher.h"
#include "utils/Terracotta/TerracottaClient.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
QString elide(const QString &text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars - 1) + QStringLiteral("…");
}
}

MultiplayerLobbyPage::MultiplayerLobbyPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("multiplayerLobbyPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &MultiplayerLobbyPage::applyStyle);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(30000);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (isVisible())
            requestRooms();
    });
    m_refreshTimer->start();

    connect(TerracottaClient::instance(), &TerracottaClient::stateChanged,
            this, &MultiplayerLobbyPage::onTerracottaStateChanged);

    applyStyle();
}

MultiplayerLobbyPage::~MultiplayerLobbyPage() = default;

void MultiplayerLobbyPage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 22, 28, 18);
    layout->setSpacing(14);

    // ---- 页头 ----
    auto *header = new QHBoxLayout();
    header->setSpacing(12);

    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(4);
    m_titleLabel = new QLabel(tr("联机大厅"), this);
    m_titleLabel->setObjectName(QStringLiteral("mlLobbyTitle"));
    auto *subtitle = new QLabel(
        tr("挑一个房间就能直接进游戏：自动选版本、加载器和模组都合适的本地实例。"),
        this);
    subtitle->setObjectName(QStringLiteral("mlLobbySubtitle"));
    subtitle->setWordWrap(true);
    titleBox->addWidget(m_titleLabel);
    titleBox->addWidget(subtitle);
    header->addLayout(titleBox, 1);

    auto *webBtn = new QPushButton(tr("打开网页大厅"), this);
    webBtn->setObjectName(QStringLiteral("mlGhostBtn"));
    webBtn->setCursor(Qt::PointingHandCursor);
    webBtn->setFixedHeight(30);
    connect(webBtn, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://forum.mcblockbox.top/lobby.html")));
    });
    header->addWidget(webBtn);

    auto *hostBtn = new QPushButton(tr("我要开房"), this);
    hostBtn->setObjectName(QStringLiteral("mlPrimaryBtn"));
    hostBtn->setCursor(Qt::PointingHandCursor);
    hostBtn->setFixedHeight(30);
    connect(hostBtn, &QPushButton::clicked, this,
            &MultiplayerLobbyPage::openInstanceAssistantRequested);
    header->addWidget(hostBtn);

    layout->addLayout(header);

    // ---- 筛选栏 ----
    auto *filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("mlSearch"));
    m_searchEdit->setPlaceholderText(tr("搜房间名、简介或房主"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(30);
    m_searchEdit->setMaximumWidth(380);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MultiplayerLobbyPage::requestRooms);
    filterRow->addWidget(m_searchEdit, 1);

    m_coreCombo = new QComboBox(this);
    m_coreCombo->setObjectName(QStringLiteral("mlCoreCombo"));
    m_coreCombo->setFixedHeight(30);
    m_coreCombo->addItem(tr("全部核心"), QString());
    m_coreCombo->addItem(tr("陶瓦联机"), QStringLiteral("terracotta"));
    m_coreCombo->addItem(tr("红石联机"), QStringLiteral("hongshi"));
    m_coreCombo->addItem(tr("内网穿透"), QStringLiteral("frp"));
    connect(m_coreCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { requestRooms(); });
    filterRow->addWidget(m_coreCombo);

    filterRow->addStretch();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("mlStatus"));
    filterRow->addWidget(m_statusLabel);

    layout->addLayout(filterRow);

    // ---- 房间列表 ----
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("mlScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_container = new QWidget(m_scroll);
    m_container->setObjectName(QStringLiteral("mlContainer"));
    m_roomsLayout = new QVBoxLayout(m_container);
    m_roomsLayout->setContentsMargins(0, 0, 4, 8);
    m_roomsLayout->setSpacing(10);
    m_roomsLayout->addStretch();
    m_scroll->setWidget(m_container);
    layout->addWidget(m_scroll, 1);

    setStatus(tr("加载中…"));
}

void MultiplayerLobbyPage::applyStyle()
{
    const QString themeColor = ThemeManager::instance()->currentThemeColor();
    const QString textColor = ThemeManager::instance()->currentTextColor();
    const QString borderColor = ThemeManager::instance()->currentBorderColor();
    QColor tc(themeColor);
    const QString hoverBg = QStringLiteral("rgba(%1, %2, %3, 0.10)")
                                .arg(tc.red()).arg(tc.green()).arg(tc.blue());

    setStyleSheet(QStringLiteral(
        "QWidget#multiplayerLobbyPage { background: palette(base); }"
        "QLabel#mlLobbyTitle {"
        "  color: %1; font-size: 22px; font-weight: 700; background: transparent; border: none;"
        "}"
        "QLabel#mlLobbySubtitle {"
        "  color: %2; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLabel#mlStatus { color: %2; font-size: 11px; background: transparent; border: none; }"
        "QPushButton#mlPrimaryBtn {"
        "  background: %1; color: white; border: none; border-radius: 6px;"
        "  padding: 0 16px; font-size: 12px; font-weight: 600;"
        "}"
        "QPushButton#mlPrimaryBtn:hover { background: %4; }"
        "QPushButton#mlGhostBtn {"
        "  background: transparent; color: %1; border: 1px solid %3; border-radius: 6px;"
        "  padding: 0 12px; font-size: 12px;"
        "}"
        "QPushButton#mlGhostBtn:hover { border-color: %1; background: %5; }"
        "QLineEdit#mlSearch, QComboBox#mlCoreCombo {"
        "  background: palette(base); border: 1px solid %3; border-radius: 6px;"
        "  color: %2; font-size: 12px; padding: 0 9px;"
        "}"
        "QLineEdit#mlSearch:focus, QComboBox#mlCoreCombo:focus { border-color: %1; }"
        "QComboBox#mlCoreCombo QAbstractItemView {"
        "  background: palette(base); color: %2; border: 1px solid %3;"
        "  selection-background-color: %5; selection-color: %2;"
        "}"
        "QFrame#mlRoomCard {"
        "  background: palette(base); border: 1px solid %3; border-radius: 10px;"
        "}"
        "QFrame#mlRoomCard:hover { border-color: %1; }"
        "QLabel#mlCardTitle { color: %2; font-size: 14px; font-weight: 600;"
        "  background: transparent; border: none; }"
        "QLabel#mlCardMeta { color: %2; font-size: 11px; background: transparent; border: none; }"
        "QLabel#mlCardDesc { color: %2; font-size: 11px; background: transparent; border: none; }"
        "QLabel#mlCardMatch { color: %1; font-size: 11px; background: transparent; border: none; }"
        "QLabel#mlChip {"
        "  color: %1; border: 1px solid %3; border-radius: 9px;"
        "  padding: 1px 7px; font-size: 10px; background: transparent;"
        "}"
        "QLabel#mlChipOnline {"
        "  color: #2e9e5b; border: 1px solid rgba(46, 158, 91, 0.45); border-radius: 9px;"
        "  padding: 1px 7px; font-size: 10px; background: rgba(46, 158, 91, 0.10);"
        "}"
        "QLabel#mlCode {"
        "  color: %2; font-family: monospace; font-size: 13px; font-weight: 700;"
        "  border: 1px dashed %3; border-radius: 6px; padding: 5px 9px; background: %5;"
        "}"
        "QPushButton#mlJoinBtn {"
        "  background: %1; color: white; border: none; border-radius: 6px;"
        "  padding: 0 14px; font-size: 12px; font-weight: 600;"
        "}"
        "QPushButton#mlJoinBtn:hover { background: %4; }"
        "QPushButton#mlCopyBtn {"
        "  background: transparent; color: %2; border: 1px solid %3; border-radius: 6px;"
        "  padding: 0 10px; font-size: 11px;"
        "}"
        "QPushButton#mlCopyBtn:hover { border-color: %1; color: %1; }"
        "QPushButton#mlLinkBtn {"
        "  background: transparent; border: none; color: %1; font-size: 12px;"
        "  text-align: left; padding: 0;"
        "}"
        "QPushButton#mlLinkBtn:hover { text-decoration: underline; }"
        "QScrollArea#mlScroll, QWidget#mlContainer { background: transparent; border: none; }"
    ).arg(themeColor, textColor, borderColor, tc.darker(110).name(), hoverBg));
}

void MultiplayerLobbyPage::refresh()
{
    requestRooms();
}

void MultiplayerLobbyPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_loading)
        requestRooms();
}

void MultiplayerLobbyPage::requestRooms()
{
    if (m_loading)
        return;
    m_loading = true;
    setStatus(tr("正在刷新…"));

    const QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    const QString core = m_coreCombo ? m_coreCombo->currentData().toString() : QString();

    ForumLobbyService::instance()->fetchRooms(
        keyword, core,
        [this](const QVector<ForumLobbyRoom> &rooms, int total) {
            m_loading = false;
            m_rooms = rooms;
            m_total = total;
            renderRooms();
        },
        [this](const QString &message, int) {
            m_loading = false;
            setStatus(tr("刷新失败：%1").arg(message), true);
        });
}

void MultiplayerLobbyPage::renderRooms()
{
    if (!m_roomsLayout)
        return;

    while (m_roomsLayout->count() > 0)
    {
        QLayoutItem *item = m_roomsLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (m_rooms.isEmpty())
    {
        auto *empty = new QLabel(tr("大厅里暂时没有房间。\n开好房后，在实例助手的联机页点「发布到联机大厅」就能让所有人看到。"), m_container);
        empty->setObjectName(QStringLiteral("mlCardMeta"));
        empty->setWordWrap(true);
        empty->setAlignment(Qt::Alignment(Qt::AlignHCenter | Qt::AlignTop));
        empty->setContentsMargins(0, 40, 0, 0);
        m_roomsLayout->addWidget(empty);

        auto *openAssistant = new QPushButton(tr("去实例助手开房 →"), m_container);
        openAssistant->setObjectName(QStringLiteral("mlLinkBtn"));
        openAssistant->setCursor(Qt::PointingHandCursor);
        connect(openAssistant, &QPushButton::clicked, this,
                &MultiplayerLobbyPage::openInstanceAssistantRequested);
        auto *btnRow = new QHBoxLayout();
        btnRow->addStretch();
        btnRow->addWidget(openAssistant);
        btnRow->addStretch();
        m_roomsLayout->addLayout(btnRow);
        m_roomsLayout->addStretch();
        setStatus(tr("共 0 个房间"));
        return;
    }

    for (const ForumLobbyRoom &room : m_rooms)
        m_roomsLayout->addWidget(createRoomCard(room));
    m_roomsLayout->addStretch();

    const int online = m_rooms.count();
    setStatus(m_total > static_cast<int>(m_rooms.size())
                  ? tr("显示 %1 个 · 在线 %2 个").arg(m_rooms.size()).arg(m_total)
                  : tr("共 %1 个房间").arg(online));
}

QWidget *MultiplayerLobbyPage::createRoomCard(const ForumLobbyRoom &room)
{
    auto *card = new QFrame(m_container);
    card->setObjectName(QStringLiteral("mlRoomCard"));
    card->setMinimumHeight(96);

    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(16, 12, 14, 12);
    row->setSpacing(12);

    // 封面缩略（有才显示，没有就保持原来的纯文字卡片）
    if (!room.cover.isEmpty())
    {
        auto *cover = ForumKit::makeAvatar(card, room.cover, QString(), 96);
        cover->setObjectName(QStringLiteral("mlCoverThumb"));
        cover->setStyleSheet(QStringLiteral(
            "QLabel#mlCoverThumb { border: 1px solid palette(midlight);"
            "  border-radius: 8px; background: palette(window); }"));
        row->addWidget(cover);
    }

    auto *left = new QVBoxLayout();
    left->setSpacing(5);

    auto *chipRow = new QHBoxLayout();
    chipRow->setSpacing(6);

    auto *coreChip = new QLabel(room.coreLabel.isEmpty()
                                    ? ForumLobbyService::coreDisplayName(room.core)
                                    : room.coreLabel,
                                card);
    coreChip->setObjectName(QStringLiteral("mlChip"));
    chipRow->addWidget(coreChip);

    auto *onlineChip = new QLabel(room.status == QStringLiteral("playing") ? tr("游戏中") : tr("在线"),
                                  card);
    onlineChip->setObjectName(QStringLiteral("mlChipOnline"));
    chipRow->addWidget(onlineChip);

    if (room.hasModpack())
    {
        auto *packChip = new QLabel(tr("前置整合包：%1").arg(room.modpackName), card);
        packChip->setObjectName(QStringLiteral("mlChip"));
        packChip->setToolTip(room.modpackUrl.isEmpty()
                                 ? tr("加入前可以先把这个整合包装上")
                                 : tr("加入前可以先把这个整合包装上：%1").arg(room.modpackUrl));
        chipRow->addWidget(packChip);
    }

    QString missing;
    auto *match = new QLabel(matchText(room, &missing), card);
    match->setObjectName(QStringLiteral("mlCardMatch"));
    match->setToolTip(missing.isEmpty() ? match->text() : tr("缺少模组：%1").arg(missing));
    chipRow->addWidget(match, 1);
    left->addLayout(chipRow);

    auto *title = new QLabel(elide(room.title, 40), card);
    title->setObjectName(QStringLiteral("mlCardTitle"));
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    left->addWidget(title);

    QStringList meta;
    if (!room.gameVersion.isEmpty())
        meta << tr("MC %1").arg(room.gameVersion);
    if (!room.loader.isEmpty())
        meta << room.loader;
    if (!room.host.displayName.isEmpty())
        meta << tr("房主 %1").arg(room.host.displayName);
    meta << tr("%1 人").arg(room.maxPlayers);
    auto *metaLabel = new QLabel(meta.join(QStringLiteral(" · ")), card);
    metaLabel->setObjectName(QStringLiteral("mlCardMeta"));
    left->addWidget(metaLabel);

    if (!room.description.trimmed().isEmpty())
    {
        auto *desc = new QLabel(elide(room.description.trimmed(), 80), card);
        desc->setObjectName(QStringLiteral("mlCardDesc"));
        left->addWidget(desc);
    }

    row->addLayout(left, 1);

    auto *right = new QVBoxLayout();
    right->setSpacing(8);
    right->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *code = new QLabel(room.joinTarget().isEmpty() ? tr("未填写邀请码") : room.joinTarget(),
                            card);
    code->setObjectName(QStringLiteral("mlCode"));
    code->setAlignment(Qt::AlignCenter);
    code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    right->addWidget(code);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);

    auto *copyBtn = new QPushButton(tr("复制"), card);
    copyBtn->setObjectName(QStringLiteral("mlCopyBtn"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setFixedHeight(30);
    connect(copyBtn, &QPushButton::clicked, this, [room]() {
        QApplication::clipboard()->setText(room.joinTarget());
    });
    btnRow->addWidget(copyBtn);

    auto *joinBtn = new QPushButton(tr("一键加入"), card);
    joinBtn->setObjectName(QStringLiteral("mlJoinBtn"));
    joinBtn->setCursor(Qt::PointingHandCursor);
    joinBtn->setFixedHeight(30);
    connect(joinBtn, &QPushButton::clicked, this, [this, room]() { joinRoom(room); });
    btnRow->addWidget(joinBtn);

    right->addLayout(btnRow);
    row->addLayout(right);

    return card;
}

QString MultiplayerLobbyPage::matchText(const ForumLobbyRoom &room, QString *missingText) const
{
    LobbyInstanceInfo best;
    LobbyInstanceMatcher::MatchLevel level = LobbyInstanceMatcher::MatchLevel::None;
    QStringList missing;
    const bool found = LobbyInstanceMatcher::pickBest(room.gameVersion, room.loader, room.mods,
                                                      &best, &level, &missing);
    if (missingText)
        *missingText = missing.join(QStringLiteral("、"));

    if (!found)
    {
        return room.gameVersion.isEmpty() ? tr("任意版本都能加入")
                                          : tr("本机没有 %1 的实例，需要先装一个").arg(room.gameVersion);
    }

    if (level == LobbyInstanceMatcher::MatchLevel::Mods)
        return tr("一键用「%1」加入 · 模组齐全").arg(best.id);
    if (level == LobbyInstanceMatcher::MatchLevel::Loader)
        return tr("一键用「%1」加入 · 版本和加载器匹配").arg(best.id);
    return tr("一键用「%1」加入 · 仅版本匹配").arg(best.id);
}

void MultiplayerLobbyPage::joinRoom(const ForumLobbyRoom &room)
{
    if (!room.isOnline())
    {
        AppMessageBox::information(this, tr("房间已过期"), tr("这个房间已经下架，刷新后再看看别的吧。"));
        return;
    }

    // 房间标了前置整合包：加入前先过一次提醒（在线的告诉玩家可以从这装，链接的给地址）
    if (room.hasModpack())
        showModpackNotice(room);

    if (room.isTerracotta())
    {
        const QString code = room.inviteCode.trimmed();
        if (code.isEmpty())
        {
            AppMessageBox::information(this, tr("没有邀请码"), tr("这个房间没有可用的邀请码，请让房主重新发布。"));
            return;
        }

        auto *terracotta = TerracottaClient::instance();
        if (terracotta->installedVersion().isEmpty())
        {
            AppMessageBox::information(
                this, tr("还没安装陶瓦联机核心"),
                tr("这是陶瓦联机的房间，需要先在实例助手的联机页安装陶瓦联机核心。"));
            emit openInstanceAssistantRequested();
            return;
        }
        if (terracotta->isInRoom())
        {
            AppMessageBox::information(this, tr("已在房间中"),
                                       tr("你已经在陶瓦联机的房间里了，先离开当前房间再加入其他房间。"));
            return;
        }

        m_pendingJoin = true;
        m_pendingWaitingCore = false;
        m_pendingRoomId = room.id;
        m_pendingCode = code;
        m_pendingVersion = room.gameVersion;
        m_pendingLoader = room.loader;
        m_pendingMods = room.mods;
        ForumLobbyService::instance()->recordJoin(room.id, nullptr, nullptr);

        QString playerName;
        if (ForumAccountService::instance()->isLoggedIn())
            playerName = ForumAccountService::instance()->user().displayName;
        if (playerName.trimmed().isEmpty())
            playerName = tr("BlockBox 玩家");

        if (terracotta->state() == TerracottaClient::State::Waiting)
        {
            if (!terracotta->joinRoom(code, playerName))
            {
                handlePendingJoin(false, tr("无法加入房间，请稍后再试。"));
                return;
            }
            setStatus(tr("正在连接房主…"));
        }
        else
        {
            m_pendingWaitingCore = true;
            terracotta->start();
            setStatus(tr("正在启动陶瓦联机核心，启动后会自动加入…"));
        }
        return;
    }

    // 红石 / frp / 直连：地址本身就够用
    QString host;
    quint16 port = 0;
    if (!parseHostPort(room.address, &host, &port))
    {
        AppMessageBox::information(this, tr("地址无效"),
                                   tr("这个房间没有可用的连接地址，请让房主重新发布。"));
        return;
    }

    ForumLobbyService::instance()->recordJoin(room.id, nullptr, nullptr);
    emit joinGameRequested(host, port, room.gameVersion, room.loader, room.mods);
}

void MultiplayerLobbyPage::showModpackNotice(const ForumLobbyRoom &room)
{
    if (room.modpackMode == QLatin1String("online") && !room.modpackUrl.isEmpty())
    {
        AppMessageBox::information(
            this, tr("需要前置整合包"),
            tr("房主给这个房间标了前置整合包「%1」。\n\n"
               "建议先在「资源 > 导入整合包」里下载安装它：\n%2\n\n装好再回来加入。")
                .arg(room.modpackName, room.modpackUrl));
    }
    else if (room.modpackMode == QLatin1String("link") && !room.modpackUrl.isEmpty())
    {
        AppMessageBox::information(
            this, tr("需要前置整合包"),
            tr("房主给这个房间标了前置整合包「%1」。\n\n下载地址：\n%2\n\n自己装好再回来加入。")
                .arg(room.modpackName, room.modpackUrl));
    }
    else
    {
        AppMessageBox::information(
            this, tr("需要前置整合包"),
            tr("房主给这个房间标了前置整合包「%1」，加入前先确认你装了它。")
                .arg(room.modpackName));
    }
}

void MultiplayerLobbyPage::onTerracottaStateChanged()
{
    if (!m_pendingJoin)
        return;

    auto *terracotta = TerracottaClient::instance();
    const auto state = terracotta->state();

    if (state == TerracottaClient::State::Exception
        || state == TerracottaClient::State::Fatal)
    {
        handlePendingJoin(false, tr("连不上房主，请稍后再试。"));
        return;
    }

    if (m_pendingWaitingCore && state == TerracottaClient::State::Waiting)
    {
        m_pendingWaitingCore = false;
        QString playerName;
        if (ForumAccountService::instance()->isLoggedIn())
            playerName = ForumAccountService::instance()->user().displayName;
        if (playerName.trimmed().isEmpty())
            playerName = tr("BlockBox 玩家");

        if (!terracotta->joinRoom(m_pendingCode, playerName))
        {
            handlePendingJoin(false, tr("无法加入房间，请稍后再试。"));
            return;
        }
        setStatus(tr("正在连接房主…"));
        return;
    }

    if (state == TerracottaClient::State::GuestOk)
    {
        QString host;
        quint16 port = 0;
        if (!parseHostPort(terracotta->serverUrl(), &host, &port))
        {
            handlePendingJoin(false, tr("联机核心没有返回可用的服务器地址。"));
            return;
        }
        const QString version = m_pendingVersion;
        const QString loader = m_pendingLoader;
        const QStringList mods = m_pendingMods;
        m_pendingJoin = false;
        m_pendingWaitingCore = false;
        setStatus(tr("连接成功，正在启动游戏…"));
        emit joinGameRequested(host, port, version, loader, mods);
    }
}

void MultiplayerLobbyPage::handlePendingJoin(bool success, const QString &message)
{
    m_pendingJoin = false;
    m_pendingWaitingCore = false;
    m_pendingRoomId = 0;
    m_pendingCode.clear();
    m_pendingVersion.clear();
    m_pendingLoader.clear();
    m_pendingMods.clear();

    if (!success && !message.isEmpty())
    {
        setStatus(message, true);
        AppMessageBox::information(this, tr("加入失败"), message);
    }
}

void MultiplayerLobbyPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#d9534f;")
                                       : QString());
}

bool MultiplayerLobbyPage::parseHostPort(const QString &text, QString *host, quint16 *port)
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
