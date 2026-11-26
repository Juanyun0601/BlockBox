/**
 * @file   HomeFriendsPanel.cpp
 * @brief  首页好友栏实现
 * @author BlockBox Team
 * @date   2026-11-07
 */

#include "HomeFriendsPanel.h"

#include "ForumKit.h"
#include "OutlinedLabel.h"
#include "layouts/FlowLayout.h"
#include "utils/ForumAccountService.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// 好友与联机大厅同步间隔：好友开房后最多半分钟内出现在首页
constexpr int kPollIntervalMs = 30000;
// 卡片宽度 / 间距 / 列数来自 UiMetrics::kHomeStrip*：与首页最近游玩、热门帖卡片同宽，
// 且一栏只摆一行（一行能放几张就显示几张），其余的去好友页看

QString friendDisplayName(const ForumFriendInfo &info)
{
    return info.displayName.trimmed().isEmpty() ? info.username : info.displayName;
}

} // namespace

HomeFriendsPanel::HomeFriendsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("homeFriendsPanel"));
    setAttribute(Qt::WA_StyledBackground, false);

    buildUi();

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &HomeFriendsPanel::refresh);

    // 登录 / 退出 / 资料变化后立刻换一份好友列表
    connect(ForumAccountService::instance(), &ForumAccountService::userChanged,
            this, [this]() { refresh(); });
    connect(ForumAccountService::instance(), &ForumAccountService::sessionExpired,
            this, [this]() {
                m_friends.clear();
                m_rooms.clear();
                m_friendsLoading = false;
                m_roomsLoading = false;
                m_friendsFailed = false;
                m_roomsFailed = false;
                m_loadedOnce = false;
                m_builtSignature.clear();
                render();
            });
    // 主题切换：头像的首字母底色是内联样式，需要重建卡片
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        m_builtSignature.clear();
        render();
    });
}

HomeFriendsPanel::~HomeFriendsPanel() = default;

void HomeFriendsPanel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active)
    {
        m_pollTimer->start();
        refresh();
    }
    else
    {
        m_pollTimer->stop();
    }
}

void HomeFriendsPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    // ── 标题行：好友 · 在线数 · 查看全部 ──
    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    m_titleLabel = new OutlinedLabel(tr("好友"), this);
    m_titleLabel->setObjectName(QStringLiteral("homeSectionTitle"));
    header->addWidget(m_titleLabel);

    m_countLabel = new QLabel(this);
    m_countLabel->setObjectName(QStringLiteral("homeFriendsCount"));
    header->addWidget(m_countLabel);
    header->addStretch(1);

    m_openButton = new QPushButton(tr("查看全部"), this);
    m_openButton->setObjectName(QStringLiteral("homeLinkBtn"));
    m_openButton->setCursor(Qt::PointingHandCursor);
    m_openButton->setToolTip(tr("打开好友与私信页"));
    connect(m_openButton, &QPushButton::clicked, this,
            &HomeFriendsPanel::openFriendsPageRequested);
    header->addWidget(m_openButton);

    root->addLayout(header);

    // ── 好友卡片流：按宽度算出一行摆几张，只摆一行 ──
    m_listHost = new QWidget(this);
    m_listHost->setObjectName(QStringLiteral("homeFriendsListHost"));
    m_listLayout = new FlowLayout(m_listHost, 0,
                                  UiMetrics::kHomeStripSpacing,
                                  UiMetrics::kHomeStripSpacing);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listHost->installEventFilter(this);
    root->addWidget(m_listHost);

    // ── 空态行：未登录 / 加载中 / 加载失败 / 还没有好友时占住卡片流的位置 ──
    m_stateBar = new QWidget(this);
    m_stateBar->setObjectName(QStringLiteral("homeFriendsState"));
    auto *stateRow = new QHBoxLayout(m_stateBar);
    stateRow->setContentsMargins(0, 8, 0, 8);
    stateRow->setSpacing(12);
    stateRow->addStretch(1);

    m_stateLabel = new QLabel(m_stateBar);
    m_stateLabel->setObjectName(QStringLiteral("homeFriendsStateText"));
    stateRow->addWidget(m_stateLabel);

    m_stateBtn = new QPushButton(m_stateBar);
    m_stateBtn->setObjectName(QStringLiteral("homeFriendsStateBtn"));
    m_stateBtn->setCursor(Qt::PointingHandCursor);
    m_stateBtn->setFixedHeight(30);
    connect(m_stateBtn, &QPushButton::clicked, this, [this]() {
        if (m_stateAction)
            m_stateAction();
    });
    stateRow->addWidget(m_stateBtn);
    stateRow->addStretch(1);

    m_stateBar->hide();
    root->addWidget(m_stateBar);

    // 首次显示先占一行提示，等好友数据回来再由 render() 换成卡片
    if (ForumAccountService::instance()->isLoggedIn())
    {
        showState(tr("正在加载好友…"));
    }
    else
    {
        showState(tr("登录方块盒子账号，看看好友都在玩什么"),
                  tr("去登录"), [this]() { emit loginRequested(); });
    }
}

bool HomeFriendsPanel::eventFilter(QObject *watched, QEvent *event)
{
    // 容器宽度变化后重新分列：延迟一拍执行，避免在布局激活过程中增删子项
    if (watched == m_listHost && event->type() == QEvent::Resize && m_listHost->isVisible())
        scheduleRebuild();
    return QWidget::eventFilter(watched, event);
}

void HomeFriendsPanel::refresh()
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        m_friends.clear();
        m_rooms.clear();
        m_friendsLoading = false;
        m_roomsLoading = false;
        m_friendsFailed = false;
        m_roomsFailed = false;
        m_loadedOnce = false;
        m_builtSignature.clear();
        render();
        return;
    }
    loadFriends();
    loadRooms();
    // 还没有任何数据时先显示「正在加载」，避免登录回来后短时间停在旧空态上
    if (m_friends.isEmpty() && !m_loadedOnce)
    {
        m_builtSignature.clear();
        render();
    }
}

void HomeFriendsPanel::loadFriends()
{
    if (m_friendsLoading)
        return;
    m_friendsLoading = true;
    const QString session = ForumAccountService::instance()->token();

    QPointer<HomeFriendsPanel> guard(this);
    ForumChatService::instance()->fetchFriends(
        [guard, session](const ForumFriendSnapshot &snapshot) {
            if (!guard)
                return;
            guard->m_friendsLoading = false;
            if (!ForumAccountService::instance()->isLoggedIn()
                || session != ForumAccountService::instance()->token())
                return;
            guard->m_friends = snapshot.friends;
            guard->m_friendsFailed = false;
            guard->m_loadedOnce = true;
            guard->render();
        },
        [guard, session](const QString &, int) {
            if (!guard)
                return;
            guard->m_friendsLoading = false;
            if (session != ForumAccountService::instance()->token())
                return;
            guard->m_friendsFailed = true;
            guard->m_loadedOnce = true;
            guard->render();
        });
}

void HomeFriendsPanel::loadRooms()
{
    if (m_roomsLoading)
        return;
    m_roomsLoading = true;
    const QString session = ForumAccountService::instance()->token();

    QPointer<HomeFriendsPanel> guard(this);
    ForumLobbyService::instance()->fetchRooms(
        QString(), QString(),
        [guard, session](const QVector<ForumLobbyRoom> &rooms, int) {
            if (!guard)
                return;
            guard->m_roomsLoading = false;
            if (!ForumAccountService::instance()->isLoggedIn()
                || session != ForumAccountService::instance()->token())
                return;
            guard->m_rooms = rooms;
            guard->m_roomsFailed = false;
            guard->render();
        },
        [guard, session](const QString &, int) {
            if (!guard)
                return;
            guard->m_roomsLoading = false;
            if (session != ForumAccountService::instance()->token())
                return;
            guard->m_roomsFailed = true;
            guard->render();
        });
}

QHash<int, ForumLobbyRoom> HomeFriendsPanel::activeRoomsByHost() const
{
    QHash<int, ForumLobbyRoom> rooms;
    for (const ForumLobbyRoom &room : m_rooms)
    {
        if (room.mine || !room.isOnline() || room.host.id <= 0)
            continue;
        auto it = rooms.find(room.host.id);
        if (it == rooms.end())
        {
            rooms.insert(room.host.id, room);
            continue;
        }
        // 同一位好友发布过多个房间时，用最新的那条
        const qint64 currentStamp = qMax(it->updatedAt, it->createdAt);
        const qint64 incomingStamp = qMax(room.updatedAt, room.createdAt);
        if (incomingStamp >= currentStamp)
            *it = room;
    }
    return rooms;
}

int HomeFriendsPanel::onlineFriendCount() const
{
    const QHash<int, ForumLobbyRoom> rooms = activeRoomsByHost();
    int count = 0;
    for (const ForumFriendInfo &info : m_friends)
    {
        if (info.online || rooms.contains(info.id))
            ++count;
    }
    return count;
}

QString HomeFriendsPanel::dataSignature() const
{
    QStringList parts;
    const bool loggedIn = ForumAccountService::instance()->isLoggedIn();
    parts << QStringLiteral("%1|%2|%3|%4|%5|%6")
                 .arg(loggedIn ? 1 : 0)
                 .arg(m_friendsLoading ? 1 : 0)
                 .arg(m_roomsLoading ? 1 : 0)
                 .arg(m_friendsFailed ? 1 : 0)
                 .arg(m_roomsFailed ? 1 : 0)
                 .arg(m_loadedOnce ? 1 : 0);

    for (const ForumFriendInfo &info : m_friends)
    {
        parts << QStringLiteral("F%1|%2|%3|%4|%5|%6")
                     .arg(info.id)
                     .arg(info.displayName, info.avatar)
                     .arg(info.online ? 1 : 0)
                     .arg(info.unread)
                     .arg(info.official ? 1 : 0);
    }
    for (const ForumLobbyRoom &room : m_rooms)
    {
        parts << QStringLiteral("R%1|%2|%3|%4|%5|%6|%7|%8|%9")
                     .arg(room.id)
                     .arg(room.host.id)
                     .arg(room.status, room.core, room.title)
                     .arg(room.updatedAt)
                     .arg(room.expiresIn)
                     .arg(room.inviteCode, room.address);
    }
    return parts.join(QLatin1Char('\n'));
}

void HomeFriendsPanel::rebuildHeader()
{
    const bool loggedIn = ForumAccountService::instance()->isLoggedIn();
    m_openButton->setVisible(loggedIn);

    if (!loggedIn || m_friends.isEmpty())
    {
        m_countLabel->clear();
        return;
    }

    const int online = onlineFriendCount();
    if (online > 0)
        m_countLabel->setText(tr("%1 在线").arg(online));
    else
        m_countLabel->setText(tr("共 %1 位").arg(m_friends.size()));
    m_countLabel->setToolTip(tr("共 %1 位好友").arg(m_friends.size()));
}

int HomeFriendsPanel::calculateCardWidth() const
{
    return UiMetrics::homeStripCardWidth(calculateColumns());
}

int HomeFriendsPanel::calculateColumns() const
{
    return UiMetrics::homeStripColumns(m_listHost ? m_listHost->width() : 0);
}

void HomeFriendsPanel::clearCards()
{
    if (!m_listLayout)
        return;
    while (QLayoutItem *item = m_listLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

void HomeFriendsPanel::showState(const QString &text, const QString &actionText,
                                 const std::function<void()> &onAction)
{
    if (!m_stateBar)
        return;
    m_stateLabel->setText(text);
    m_stateAction = onAction;
    m_stateBtn->setText(actionText);
    m_stateBtn->setVisible(!actionText.isEmpty() && static_cast<bool>(onAction));
    m_listHost->hide();
    m_stateBar->show();
}

void HomeFriendsPanel::hideState()
{
    if (!m_stateBar)
        return;
    m_stateAction = nullptr;
    m_stateBar->hide();
    m_listHost->show();
}

void HomeFriendsPanel::scheduleRebuild()
{
    if (m_rebuildPending)
        return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_rebuildPending = false;
        render();
    });
}

QWidget *HomeFriendsPanel::createFriendCard(const ForumFriendInfo &info,
                                            const ForumLobbyRoom *room, int cardWidth)
{
    auto *card = new ForumKit::ClickableCard(m_listHost);
    card->setObjectName(QStringLiteral("homeFriendCard"));
    card->setProperty("roomActive", room != nullptr);
    card->setCursor(Qt::PointingHandCursor);
    card->setFixedWidth(cardWidth);
    // 同一排卡片统一高度：有「一起玩」按钮的卡片也按同一高度排，避免行内参差
    card->setFixedHeight(58);

    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(9, 7, 9, 7);
    row->setSpacing(8);

    const QString name = friendDisplayName(info);
    // 官方客服用方块盒子图标当头像，普通好友照旧
    auto *avatar = info.official ? ForumKit::makeOfficialAvatar(card, 34)
                                 : ForumKit::makeAvatar(card, info.avatar, name, 34);
    row->addWidget(avatar, 0, Qt::AlignVCenter);

    auto *textColumn = new QVBoxLayout();
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(1);

    auto *nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(5);

    auto *dot = new QLabel(card);
    dot->setObjectName(QStringLiteral("homeFriendOnlineDot"));
    dot->setFixedSize(6, 6);
    dot->setAttribute(Qt::WA_StyledBackground, true);
    dot->setProperty("online", info.online || room != nullptr);
    nameRow->addWidget(dot, 0, Qt::AlignVCenter);

    auto *nameLabel = new ForumKit::ElidedLabel(card);
    nameLabel->setObjectName(QStringLiteral("homeFriendName"));
    nameLabel->setFullText(name);
    nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nameRow->addWidget(nameLabel, 1);
    if (info.official)
    {
        auto *badge = new QLabel(tr("官方"), card);
        badge->setObjectName(QStringLiteral("homeFriendOfficialBadge"));
        badge->setToolTip(tr("方块盒子官方客服，遇到问题可以随时提问"));
        nameRow->addWidget(badge, 0, Qt::AlignVCenter);
    }
    textColumn->addLayout(nameRow);

    auto *status = new QLabel(card);
    status->setObjectName(QStringLiteral("homeFriendStatus"));
    status->setProperty("roomActive", room != nullptr);
    if (room)
    {
        status->setText(tr("在联机房间"));
        const QString title = room->title.trimmed();
        status->setToolTip(title.isEmpty() ? tr("房间正在开放") : tr("房间：%1").arg(title));
    }
    else if (info.official)
    {
        status->setText(tr("官方客服 · 随时提问"));
    }
    else
    {
        status->setText(info.online ? tr("在线") : tr("离线"));
    }
    textColumn->addWidget(status);

    row->addLayout(textColumn, 1);

    if (room)
    {
        auto *playButton = new QPushButton(tr("一起玩"), card);
        playButton->setObjectName(QStringLiteral("homeFriendPlayBtn"));
        playButton->setCursor(Qt::PointingHandCursor);
        playButton->setFixedHeight(26);
        const ForumLobbyRoom roomCopy = *room;
        connect(playButton, &QPushButton::clicked, this, [this, roomCopy]() {
            emit joinRoomRequested(roomCopy);
        });
        row->addWidget(playButton, 0, Qt::AlignVCenter);
    }

    card->onClick = [this, userId = info.id]() { emit friendChatRequested(userId); };

    if (room)
    {
        const QString title = room->title.trimmed();
        card->setToolTip(title.isEmpty() ? tr("点击和 %1 聊天").arg(name)
                                         : tr("%1 正在「%2」，点击卡片聊天").arg(name, title));
    }
    else
    {
        card->setToolTip(tr("点击和 %1 聊天").arg(name));
    }

    return card;
}

void HomeFriendsPanel::render()
{
    if (!m_listLayout || !m_listHost || !m_stateBar)
        return;

    rebuildHeader();

    const int cardWidth = calculateCardWidth();
    const QString signature = dataSignature();
    if (signature == m_builtSignature && cardWidth == m_builtCardWidth)
        return;
    m_builtSignature = signature;
    m_builtCardWidth = cardWidth;

    clearCards();

    const bool loggedIn = ForumAccountService::instance()->isLoggedIn();

    if (!loggedIn)
    {
        showState(tr("登录方块盒子账号，看看好友都在玩什么"),
                  tr("去登录"), [this]() { emit loginRequested(); });
        return;
    }
    if (m_friends.isEmpty())
    {
        if (m_friendsFailed)
        {
            showState(tr("好友列表没加载出来，稍后再试试"),
                      tr("重试"), [this]() { refresh(); });
        }
        else if (!m_loadedOnce || m_friendsLoading)
        {
            showState(tr("正在加载好友…"));
        }
        else
        {
            showState(tr("还没有好友，先去认识几位吧"),
                      tr("去添加好友"), [this]() { emit openFriendsPageRequested(); });
        }
        return;
    }

    hideState();

    const QHash<int, ForumLobbyRoom> rooms = activeRoomsByHost();
    QVector<ForumFriendInfo> ordered = m_friends;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&rooms](const ForumFriendInfo &a, const ForumFriendInfo &b)
                     {
                         // 官方客服固定在第一位，其余好友里联机中的优先
                         if (a.official != b.official)
                             return a.official;
                         const auto roomA = rooms.constFind(a.id);
                         const auto roomB = rooms.constFind(b.id);
                         const bool aPlaying = roomA != rooms.constEnd();
                         const bool bPlaying = roomB != rooms.constEnd();
                         if (aPlaying != bPlaying)
                             return aPlaying;
                         if (aPlaying && bPlaying)
                         {
                             const qint64 stampA = qMax(roomA->updatedAt, roomA->createdAt);
                             const qint64 stampB = qMax(roomB->updatedAt, roomB->createdAt);
                             return stampA > stampB;
                         }
                         return false;
                     });

    const int count = qMin(ordered.size(), calculateColumns());
    for (int i = 0; i < count; ++i)
    {
        const ForumFriendInfo &info = ordered[i];
        const auto roomIt = rooms.constFind(info.id);
        const ForumLobbyRoom *room = roomIt == rooms.constEnd() ? nullptr : &roomIt.value();
        m_listLayout->addWidget(createFriendCard(info, room, cardWidth));
    }

    // 让父布局重新查询本栏的 sizeHint（分列变化导致高度变化时才能正确传导）
    m_listHost->updateGeometry();
}
