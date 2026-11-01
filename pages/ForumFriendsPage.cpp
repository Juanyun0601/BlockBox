/**
 * @file   ForumFriendsPage.cpp
 * @brief  论坛好友与私信页实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumFriendsPage.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

#include "components/ForumKit.h"
#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/StyleKit.h"

namespace {

/** 消息气泡时间：今天显示 HH:mm，更早显示 MM-dd HH:mm */
QString messageTime(qint64 seconds)
{
    if (seconds <= 0)
        return QString();
    const QDateTime moment = QDateTime::fromSecsSinceEpoch(seconds);
    const QDateTime today = QDateTime::currentDateTime();
    const QString time = moment.toString(QStringLiteral("HH:mm"));
    if (moment.date() == today.date())
        return time;
    return moment.toString(QStringLiteral("MM-dd HH:mm"));
}

/** 在线状态一句话 */
QString onlineText(const ForumFriendInfo &peer)
{
    if (peer.online)
        return QObject::tr("在线");
    if (peer.lastSeenAt > 0)
        return QObject::tr("最近活跃 %1").arg(ForumKit::relativeTime(peer.lastSeenAt));
    return QObject::tr("很久没来啦");
}

} // namespace

ForumFriendsPage::ForumFriendsPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    m_pollTimer.setInterval(4000);
    connect(&m_pollTimer, &QTimer::timeout, this, &ForumFriendsPage::pollOnce);

    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(300);
    connect(&m_searchTimer, &QTimer::timeout, this, [this]() {
        if (m_searchEdit)
            runSearch(m_searchEdit->text());
    });

    // 令牌失效（在别处退出登录 / 被踢）：回到未登录的空状态
    connect(ForumAccountService::instance(), &ForumAccountService::sessionExpired, this, [this]() {
        m_snapshot = {};
        m_peer = {};
        m_messages.clear();
        m_lastMessageId = 0;
        m_pollTimer.stop();
        renderSide();
        closeConversation();
        setStatus(tr("登录已失效，请重新登录"), true);
    });
}

void ForumFriendsPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 18, 28, 28);
    outer->setSpacing(12);

    // ── 状态行 ──
    auto *statusRow = new QHBoxLayout();
    statusRow->setSpacing(10);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("forumStatus"));
    statusRow->addWidget(m_statusLabel);
    statusRow->addStretch(1);
    m_refreshButton = new QPushButton(tr("刷新"), this);
    m_refreshButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setFixedHeight(28);
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() { reload(); });
    statusRow->addWidget(m_refreshButton);
    outer->addLayout(statusRow);

    // ── 左右两栏 ──
    auto *columns = new QHBoxLayout();
    columns->setSpacing(12);
    outer->addLayout(columns, 1);

    // 左栏：页签 + 列表
    auto *sideColumn = new QWidget(this);
    auto *sideLayout = new QVBoxLayout(sideColumn);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(8);

    auto *tabRow = new QHBoxLayout();
    tabRow->setSpacing(6);
    m_friendsTabButton = new QPushButton(tr("好友"), sideColumn);
    m_requestsTabButton = new QPushButton(tr("申请"), sideColumn);
    m_addTabButton = new QPushButton(tr("添加好友"), sideColumn);
    for (QPushButton *button : {m_friendsTabButton, m_requestsTabButton, m_addTabButton})
    {
        button->setObjectName(QStringLiteral("forumSortButton"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(28);
        tabRow->addWidget(button);
    }
    m_friendsTabButton->setChecked(true);
    connect(m_friendsTabButton, &QPushButton::clicked, this,
            [this]() { setTab(FriendsTab); });
    connect(m_requestsTabButton, &QPushButton::clicked, this,
            [this]() { setTab(RequestsTab); });
    connect(m_addTabButton, &QPushButton::clicked, this, [this]() { setTab(AddTab); });
    sideLayout->addLayout(tabRow);

    m_sideScroll = new QScrollArea(sideColumn);
    m_sideScroll->setWidgetResizable(true);
    m_sideScroll->setFrameShape(QFrame::NoFrame);
    m_sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sideScroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    m_sideContainer = new QWidget(m_sideScroll);
    m_sideContainer->setAttribute(Qt::WA_StyledBackground, false);
    m_sideListLayout = new QVBoxLayout(m_sideContainer);
    m_sideListLayout->setContentsMargins(0, 0, 0, 0);
    m_sideListLayout->setSpacing(8);
    m_sideListLayout->addStretch(1);
    m_sideScroll->setWidget(m_sideContainer);
    sideLayout->addWidget(m_sideScroll, 1);
    sideColumn->setFixedWidth(320);
    columns->addWidget(sideColumn);

    // 右栏：聊天面板
    m_chatCard = ForumKit::makeCard(this, QStringLiteral("forumCard"));
    auto *chatLayout = new QVBoxLayout(m_chatCard);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

    m_chatEmpty = ForumKit::makeMuted(m_chatCard, tr("在左侧选择一个好友，开始聊天"));
    m_chatEmpty->setAlignment(Qt::AlignCenter);
    chatLayout->addWidget(m_chatEmpty, 1);

    m_chatMain = new QWidget(m_chatCard);
    m_chatMain->hide();
    auto *chatMainLayout = new QVBoxLayout(m_chatMain);
    chatMainLayout->setContentsMargins(0, 0, 0, 0);
    chatMainLayout->setSpacing(0);

    // 会话头：头像 / 名字 / 状态 / 主页 / 删除好友
    auto *head = new QWidget(m_chatMain);
    head->setObjectName(QStringLiteral("forumChatHead"));
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(16, 12, 16, 12);
    headLayout->setSpacing(10);
    m_chatAvatarSlot = new QWidget(head);
    m_chatAvatarSlot->setFixedSize(36, 36);
    auto *avatarSlotLayout = new QHBoxLayout(m_chatAvatarSlot);
    avatarSlotLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->addWidget(m_chatAvatarSlot);
    auto *nameColumn = new QVBoxLayout();
    nameColumn->setSpacing(1);
    m_chatName = new QLabel(head);
    m_chatName->setObjectName(QStringLiteral("forumRowTitle"));
    nameColumn->addWidget(m_chatName);
    m_chatStatus = new QLabel(head);
    m_chatStatus->setObjectName(QStringLiteral("forumMuted"));
    m_chatStatus->setStyleSheet(StyleKit::resolve(QStringLiteral("font-size: 11px; color: @TEXT_TERTIARY@;")));
    nameColumn->addWidget(m_chatStatus);
    headLayout->addLayout(nameColumn, 1);
    m_chatProfileButton = new QPushButton(tr("主页"), head);
    m_chatProfileButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_chatProfileButton->setCursor(Qt::PointingHandCursor);
    m_chatProfileButton->setFixedHeight(26);
    headLayout->addWidget(m_chatProfileButton);
    m_chatRemoveButton = new QPushButton(tr("删除好友"), head);
    m_chatRemoveButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_chatRemoveButton->setCursor(Qt::PointingHandCursor);
    m_chatRemoveButton->setFixedHeight(26);
    headLayout->addWidget(m_chatRemoveButton);
    chatMainLayout->addWidget(head);

    // 加载更早
    auto *moreRow = new QHBoxLayout();
    moreRow->setContentsMargins(0, 8, 0, 0);
    moreRow->addStretch(1);
    m_moreButton = new QPushButton(tr("查看更早的消息"), m_chatMain);
    m_moreButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_moreButton->setCursor(Qt::PointingHandCursor);
    m_moreButton->setFixedHeight(24);
    m_moreButton->hide();
    connect(m_moreButton, &QPushButton::clicked, this, &ForumFriendsPage::loadOlderMessages);
    moreRow->addWidget(m_moreButton);
    moreRow->addStretch(1);
    chatMainLayout->addLayout(moreRow);

    // 消息列表
    m_msgsScroll = new QScrollArea(m_chatMain);
    m_msgsScroll->setWidgetResizable(true);
    m_msgsScroll->setFrameShape(QFrame::NoFrame);
    m_msgsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_msgsScroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    m_msgsContainer = new QWidget(m_msgsScroll);
    m_msgsContainer->setAttribute(Qt::WA_StyledBackground, false);
    m_msgsLayout = new QVBoxLayout(m_msgsContainer);
    m_msgsLayout->setContentsMargins(16, 12, 16, 12);
    m_msgsLayout->setSpacing(10);
    m_msgsLayout->addStretch(1);
    m_msgsScroll->setWidget(m_msgsContainer);
    chatMainLayout->addWidget(m_msgsScroll, 1);

    // 输入区
    auto *composer = new QWidget(m_chatMain);
    composer->setObjectName(QStringLiteral("forumChatComposer"));
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(16, 10, 16, 10);
    composerLayout->setSpacing(10);
    m_chatInput = new QTextEdit(composer);
    m_chatInput->setObjectName(QStringLiteral("forumChatInput"));
    m_chatInput->setPlaceholderText(tr("输入消息，Enter 发送，Shift+Enter 换行"));
    m_chatInput->setFixedHeight(64);
    m_chatInput->installEventFilter(this);
    composerLayout->addWidget(m_chatInput, 1);
    m_sendButton = new QPushButton(tr("发送"), composer);
    m_sendButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setFixedHeight(36);
    m_sendButton->setFixedWidth(76);
    connect(m_sendButton, &QPushButton::clicked, this, &ForumFriendsPage::sendCurrentMessage);
    composerLayout->addWidget(m_sendButton);
    chatMainLayout->addWidget(composer);

    chatLayout->addWidget(m_chatMain, 1);
    columns->addWidget(m_chatCard, 1);

    connect(m_chatProfileButton, &QPushButton::clicked, this,
            [this]() { emit userOpenRequested(m_peer.id); });
    connect(m_chatRemoveButton, &QPushButton::clicked, this, [this]() {
        if (m_peer.id <= 0)
            return;
        const int peerId = m_peer.id;
        const QString peerName = m_peer.displayName;
        if (QMessageBox::question(this, tr("删除好友"),
                                  tr("确定删除好友「%1」吗？聊天记录将不再可见。").arg(peerName)) !=
            QMessageBox::Yes)
            return;
        ForumChatService::instance()->removeFriend(
            peerId,
            [this, peerId]() {
                NotificationManager::showSuccess(this, tr("已删除好友"));
                if (m_peer.id == peerId)
                    closeConversation();
                reload();
            },
            [this](const QString &message, int) {
                NotificationManager::showError(this, message);
            });
    });
}

// ────────────────────────────────────────────────────────────── 进入 / 轮询

void ForumFriendsPage::open()
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        m_snapshot = {};
        closeConversation();
        ForumKit::clearLayout(m_sideListLayout);
        auto *hint = ForumKit::makeMuted(this, tr("登录后才能查看好友和私信"));
        hint->setAlignment(Qt::AlignCenter);
        auto *loginButton = new QPushButton(tr("去登录"), this);
        loginButton->setObjectName(QStringLiteral("forumPrimaryButton"));
        loginButton->setCursor(Qt::PointingHandCursor);
        loginButton->setFixedHeight(32);
        connect(loginButton, &QPushButton::clicked, this, &ForumFriendsPage::loginRequested);
        auto *row = new QWidget(this);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 20, 0, 20);
        rowLayout->setSpacing(10);
        rowLayout->addWidget(hint);
        rowLayout->addWidget(loginButton, 0, Qt::AlignHCenter);
        m_sideListLayout->insertWidget(0, row);
        setStatus(QString());
        return;
    }
    reload();
}

void ForumFriendsPage::openChat(int userId)
{
    if (userId <= 0)
    {
        open();
        return;
    }
    open();
    if (!ForumAccountService::instance()->isLoggedIn())
        return;
    // 好友列表里找到就直接开会话；找不到（可能还不是好友）也先开，
    // 让论坛返回的中文错误告诉用户「先加为好友才能私信」。
    for (const ForumFriendInfo &friendInfo : m_snapshot.friends)
    {
        if (friendInfo.id == userId)
        {
            openConversation(friendInfo);
            return;
        }
    }
    ForumFriendInfo peer;
    peer.id = userId;
    openConversation(peer);
}

void ForumFriendsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (ForumAccountService::instance()->isLoggedIn() && m_snapshot.friends.isEmpty() &&
        m_snapshot.incoming.isEmpty() && m_snapshot.outgoing.isEmpty())
        open();
    if (ForumAccountService::instance()->isLoggedIn())
        m_pollTimer.start();
}

void ForumFriendsPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_pollTimer.stop();
}

bool ForumFriendsPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_chatInput && event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
        {
            if (keyEvent->modifiers() & Qt::ShiftModifier)
                return QWidget::eventFilter(watched, event);
            sendCurrentMessage();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ForumFriendsPage::reload()
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        open();
        return;
    }
    m_refreshButton->setEnabled(false);
    setStatus(tr("正在加载…"));
    ForumChatService::instance()->fetchFriends(
        [this](const ForumFriendSnapshot &snapshot) {
            m_refreshButton->setEnabled(true);
            setStatus(QString());
            m_snapshot = snapshot;
            if (m_tab == AddTab)
                renderAddTab();
            else if (m_tab == RequestsTab)
                renderRequestRows();
            else
                renderFriendRows();
        },
        [this](const QString &message, int) {
            m_refreshButton->setEnabled(true);
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void ForumFriendsPage::pollOnce()
{
    if (!isVisible() || m_pollBusy || !ForumAccountService::instance()->isLoggedIn())
        return;
    m_pollBusy = true;
    const int peerId = m_peer.id;
    const int afterId = m_lastMessageId;
    ForumChatService::instance()->poll(
        peerId, afterId,
        [this](const ForumPollResult &result) {
            m_pollBusy = false;
            m_snapshot.friends = result.friends;
            m_snapshot.totalUnread = result.totalUnread;
            if (m_tab == FriendsTab)
                renderFriendRows();
            if (m_peer.id > 0 && !result.messages.isEmpty())
                appendMessages(result.messages);
        },
        [this](const QString &, int) { m_pollBusy = false; });
}

// ────────────────────────────────────────────────────────────── 左栏

void ForumFriendsPage::setTab(Tab tab)
{
    if (m_tab == tab)
        return;
    m_tab = tab;
    if (tab != FriendsTab)
        reload();
    else
        renderSide();
}

void ForumFriendsPage::renderSide()
{
    if (m_tab == FriendsTab)
        renderFriendRows();
    else if (m_tab == RequestsTab)
        renderRequestRows();
    else
        renderAddTab();
}

void ForumFriendsPage::renderFriendRows()
{
    const int scrollValue = m_sideScroll->verticalScrollBar()->value();
    ForumKit::clearLayout(m_sideListLayout);

    if (m_snapshot.friends.isEmpty())
    {
        auto *empty = ForumKit::makeMuted(this, tr("还没有好友，去「添加好友」找一个吧"));
        empty->setAlignment(Qt::AlignCenter);
        m_sideListLayout->addSpacing(20);
        m_sideListLayout->addWidget(empty);
        m_sideListLayout->addSpacing(20);
    }

    for (const ForumFriendInfo &friendInfo : m_snapshot.friends)
    {
        auto *row = new ForumKit::ClickableCard(this);
        row->setObjectName(QStringLiteral("forumThreadRow"));
        const int peerId = friendInfo.id;
        row->onClick = [this, peerId, friendInfo]() {
            for (const ForumFriendInfo &item : m_snapshot.friends)
            {
                if (item.id == peerId)
                {
                    openConversation(item);
                    return;
                }
            }
        };

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 10, 12, 10);
        rowLayout->setSpacing(10);

        rowLayout->addWidget(ForumKit::makeAvatar(row, friendInfo.avatar,
                                                  friendInfo.displayName, 36));

        auto *textColumn = new QVBoxLayout();
        textColumn->setSpacing(3);

        auto *nameRow = new QHBoxLayout();
        nameRow->setSpacing(6);
        auto *name = new ForumKit::ElidedLabel(row);
        name->setObjectName(QStringLiteral("forumRowTitle"));
        name->setFullText(friendInfo.displayName);
        nameRow->addWidget(name, 1);
        auto *online = new QLabel(row);
        online->setText(friendInfo.online ? QStringLiteral("●") : QStringLiteral("○"));
        online->setToolTip(onlineText(friendInfo));
        online->setStyleSheet(StyleKit::resolve(friendInfo.online
                                                    ? QStringLiteral("color: @SUCCESS@; font-size: 11px;")
                                                    : QStringLiteral("color: @TEXT_TERTIARY@; font-size: 11px;")));
        nameRow->addWidget(online);
        if (friendInfo.unread > 0)
        {
            auto *badge = new QLabel(row);
            badge->setObjectName(QStringLiteral("forumUnreadBadge"));
            badge->setText(friendInfo.unread > 99 ? QStringLiteral("99+") : QString::number(friendInfo.unread));
            nameRow->addWidget(badge);
        }
        textColumn->addLayout(nameRow);

        QString preview = tr("还没有聊过天");
        if (!friendInfo.lastMessageBody.isEmpty())
        {
            preview = friendInfo.lastMessageBody;
            if (friendInfo.lastMessageMine)
                preview = tr("我：%1").arg(preview);
        }
        auto *previewLabel = new ForumKit::ElidedLabel(row);
        previewLabel->setObjectName(QStringLiteral("forumMuted"));
        previewLabel->setFullText(preview);
        textColumn->addWidget(previewLabel);

        rowLayout->addLayout(textColumn, 1);

        auto *time = new QLabel(messageTime(friendInfo.lastMessageAt), row);
        time->setObjectName(QStringLiteral("forumMuted"));
        time->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: 11px;")));
        rowLayout->addWidget(time, 0, Qt::AlignTop);

        m_sideListLayout->addWidget(row);
    }
    m_sideListLayout->addStretch(1);
    m_sideScroll->verticalScrollBar()->setValue(scrollValue);
}

void ForumFriendsPage::renderRequestRows()
{
    ForumKit::clearLayout(m_sideListLayout);

    if (m_snapshot.incoming.isEmpty() && m_snapshot.outgoing.isEmpty())
    {
        auto *empty = ForumKit::makeMuted(this, tr("没有待处理的好友申请"));
        empty->setAlignment(Qt::AlignCenter);
        m_sideListLayout->addSpacing(20);
        m_sideListLayout->addWidget(empty);
        m_sideListLayout->addSpacing(20);
    }

    // 收到的申请：同意 / 拒绝
    for (const ForumFriendRequest &request : m_snapshot.incoming)
    {
        auto *row = new QFrame(this);
        row->setObjectName(QStringLiteral("forumThreadRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 10, 12, 10);
        rowLayout->setSpacing(10);

        rowLayout->addWidget(ForumKit::makeAvatar(row, request.user.avatar,
                                                  request.user.displayName, 36));
        auto *textColumn = new QVBoxLayout();
        textColumn->setSpacing(3);
        auto *name = new ForumKit::ElidedLabel(row);
        name->setObjectName(QStringLiteral("forumRowTitle"));
        name->setFullText(request.user.displayName);
        textColumn->addWidget(name);
        textColumn->addWidget(ForumKit::makeMuted(
            row, tr("%1 申请加你为好友").arg(ForumKit::relativeTime(request.createdAt))));
        rowLayout->addLayout(textColumn, 1);

        const int requestId = request.id;
        auto *acceptButton = new QPushButton(tr("同意"), row);
        acceptButton->setObjectName(QStringLiteral("forumPrimaryButton"));
        acceptButton->setCursor(Qt::PointingHandCursor);
        acceptButton->setFixedHeight(26);
        connect(acceptButton, &QPushButton::clicked, this, [this, requestId]() {
            ForumChatService::instance()->acceptFriendRequest(
                requestId,
                [this]() {
                    NotificationManager::showSuccess(this, tr("已同意好友申请"));
                    reload();
                },
                [this](const QString &message, int) {
                    NotificationManager::showError(this, message);
                    reload();
                });
        });
        rowLayout->addWidget(acceptButton);
        auto *declineButton = new QPushButton(tr("拒绝"), row);
        declineButton->setObjectName(QStringLiteral("forumGhostButton"));
        declineButton->setCursor(Qt::PointingHandCursor);
        declineButton->setFixedHeight(26);
        connect(declineButton, &QPushButton::clicked, this, [this, requestId]() {
            ForumChatService::instance()->declineFriendRequest(
                requestId, [this]() { reload(); },
                [this](const QString &message, int) {
                    NotificationManager::showError(this, message);
                    reload();
                });
        });
        rowLayout->addWidget(declineButton);

        m_sideListLayout->addWidget(row);
    }

    // 发出的申请：撤回
    for (const ForumFriendRequest &request : m_snapshot.outgoing)
    {
        auto *row = new QFrame(this);
        row->setObjectName(QStringLiteral("forumThreadRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 10, 12, 10);
        rowLayout->setSpacing(10);

        rowLayout->addWidget(ForumKit::makeAvatar(row, request.user.avatar,
                                                  request.user.displayName, 36));
        auto *textColumn = new QVBoxLayout();
        textColumn->setSpacing(3);
        auto *name = new ForumKit::ElidedLabel(row);
        name->setObjectName(QStringLiteral("forumRowTitle"));
        name->setFullText(request.user.displayName);
        textColumn->addWidget(name);
        textColumn->addWidget(ForumKit::makeMuted(row, tr("等待对方同意")));
        rowLayout->addLayout(textColumn, 1);

        const int requestId = request.id;
        auto *withdrawButton = new QPushButton(tr("撤回"), row);
        withdrawButton->setObjectName(QStringLiteral("forumGhostButton"));
        withdrawButton->setCursor(Qt::PointingHandCursor);
        withdrawButton->setFixedHeight(26);
        connect(withdrawButton, &QPushButton::clicked, this, [this, requestId]() {
            ForumChatService::instance()->withdrawFriendRequest(
                requestId, [this]() { reload(); },
                [this](const QString &message, int) {
                    NotificationManager::showError(this, message);
                    reload();
                });
        });
        rowLayout->addWidget(withdrawButton);

        m_sideListLayout->addWidget(row);
    }
    m_sideListLayout->addStretch(1);
}

void ForumFriendsPage::renderAddTab()
{
    // 清空列表重建；搜索框单独保留焦点状态
    detachSearchEdit();
    ForumKit::clearLayout(m_sideListLayout);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("forumSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("输入用户名或昵称搜索"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(30);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { m_searchTimer.start(); });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        m_searchTimer.stop();
        runSearch(m_searchEdit->text());
    });
    m_sideListLayout->addWidget(m_searchEdit);

    auto *hint = ForumKit::makeMuted(this, tr("搜到玩家后点「加好友」，对方同意就能聊天了"));
    hint->setWordWrap(true);
    m_sideListLayout->addWidget(hint);
    m_sideListLayout->addSpacing(4);
    m_sideListLayout->addStretch(1);

    m_searchEdit->setFocus();
}

/** 把搜索框从列表布局里摘下来（clearLayout 会销毁还在布局里的控件） */
void ForumFriendsPage::detachSearchEdit()
{
    if (!m_searchEdit)
        return;
    m_sideListLayout->removeWidget(m_searchEdit);
    m_searchEdit->setParent(nullptr);
    m_searchEdit->hide();
}

void ForumFriendsPage::runSearch(const QString &keyword)
{
    const QString text = keyword.trimmed();
    if (m_tab != AddTab)
        return;

    QLineEdit *searchEdit = m_searchEdit;
    detachSearchEdit();
    ForumKit::clearLayout(m_sideListLayout);
    m_searchEdit = searchEdit;
    if (m_searchEdit)
    {
        m_sideListLayout->addWidget(m_searchEdit);
        m_searchEdit->show();
        m_searchEdit->setText(text);
        m_searchEdit->setFocus();
    }

    if (text.isEmpty())
    {
        auto *hint = ForumKit::makeMuted(this, tr("输入用户名或昵称开始搜索"));
        m_sideListLayout->addWidget(hint);
        m_sideListLayout->addStretch(1);
        return;
    }

    auto *loading = ForumKit::makeMuted(this, tr("正在搜索…"));
    m_sideListLayout->addWidget(loading);
    m_sideListLayout->addStretch(1);

    ForumChatService::instance()->searchUsers(
        text,
        [this, text](const QVector<ForumFriendInfo> &users) {
            if (m_tab != AddTab || !m_searchEdit || m_searchEdit->text().trimmed() != text)
                return; // 已经切走或输入变了，丢弃过期结果
            QLineEdit *searchEdit = m_searchEdit;
            detachSearchEdit();
            ForumKit::clearLayout(m_sideListLayout);
            m_searchEdit = searchEdit;
            m_sideListLayout->addWidget(m_searchEdit);
            m_searchEdit->show();
            m_searchEdit->setFocus();

            if (users.isEmpty())
            {
                auto *empty = ForumKit::makeMuted(this, tr("没有找到这个用户"));
                m_sideListLayout->addWidget(empty);
                m_sideListLayout->addStretch(1);
                return;
            }

            for (const ForumFriendInfo &user : users)
            {
                auto *row = new QFrame(this);
                row->setObjectName(QStringLiteral("forumThreadRow"));
                auto *rowLayout = new QHBoxLayout(row);
                rowLayout->setContentsMargins(12, 10, 12, 10);
                rowLayout->setSpacing(10);

                rowLayout->addWidget(ForumKit::makeAvatar(row, user.avatar, user.displayName, 36));
                auto *textColumn = new QVBoxLayout();
                textColumn->setSpacing(3);
                auto *name = new ForumKit::ElidedLabel(row);
                name->setObjectName(QStringLiteral("forumRowTitle"));
                name->setFullText(user.displayName);
                textColumn->addWidget(name);
                textColumn->addWidget(
                    ForumKit::makeMuted(row, QStringLiteral("@%1").arg(user.username)));
                rowLayout->addLayout(textColumn, 1);

                const int userId = user.id;
                const bool alreadyFriend = [this, userId]() {
                    for (const ForumFriendInfo &item : m_snapshot.friends)
                        if (item.id == userId)
                            return true;
                    return false;
                }();
                const bool incoming = [this, userId]() {
                    for (const ForumFriendRequest &item : m_snapshot.incoming)
                        if (item.user.id == userId)
                            return true;
                    return false;
                }();
                const bool outgoing = [this, userId]() {
                    for (const ForumFriendRequest &item : m_snapshot.outgoing)
                        if (item.user.id == userId)
                            return true;
                    return false;
                }();

                if (alreadyFriend)
                {
                    auto *state = new QPushButton(tr("已是好友"), row);
                    state->setObjectName(QStringLiteral("forumGhostButton"));
                    state->setEnabled(false);
                    state->setFixedHeight(26);
                    rowLayout->addWidget(state);
                }
                else if (incoming)
                {
                    int requestId = 0;
                    for (const ForumFriendRequest &item : m_snapshot.incoming)
                        if (item.user.id == userId)
                            requestId = item.id;
                    auto *acceptButton = new QPushButton(tr("同意申请"), row);
                    acceptButton->setObjectName(QStringLiteral("forumPrimaryButton"));
                    acceptButton->setCursor(Qt::PointingHandCursor);
                    acceptButton->setFixedHeight(26);
                    connect(acceptButton, &QPushButton::clicked, this, [this, requestId]() {
                        ForumChatService::instance()->acceptFriendRequest(
                            requestId,
                            [this]() {
                                NotificationManager::showSuccess(this, tr("已同意好友申请"));
                                reload();
                            },
                            [this](const QString &message, int) {
                                NotificationManager::showError(this, message);
                                reload();
                            });
                    });
                    rowLayout->addWidget(acceptButton);
                }
                else if (outgoing)
                {
                    int requestId = 0;
                    for (const ForumFriendRequest &item : m_snapshot.outgoing)
                        if (item.user.id == userId)
                            requestId = item.id;
                    auto *withdrawButton = new QPushButton(tr("撤回申请"), row);
                    withdrawButton->setObjectName(QStringLiteral("forumGhostButton"));
                    withdrawButton->setCursor(Qt::PointingHandCursor);
                    withdrawButton->setFixedHeight(26);
                    connect(withdrawButton, &QPushButton::clicked, this, [this, requestId]() {
                        ForumChatService::instance()->withdrawFriendRequest(
                            requestId, [this]() { reload(); },
                            [this](const QString &message, int) {
                                NotificationManager::showError(this, message);
                                reload();
                            });
                    });
                    rowLayout->addWidget(withdrawButton);
                }
                else
                {
                    auto *addButton = new QPushButton(tr("＋ 加好友"), row);
                    addButton->setObjectName(QStringLiteral("forumPrimaryButton"));
                    addButton->setCursor(Qt::PointingHandCursor);
                    addButton->setFixedHeight(26);
                    connect(addButton, &QPushButton::clicked, this, [this, userId, addButton]() {
                        addButton->setEnabled(false);
                        ForumChatService::instance()->sendFriendRequest(
                            userId,
                            [this, addButton](const QString &status) {
                                NotificationManager::showSuccess(
                                    this, status == QStringLiteral("friends")
                                              ? tr("对方也申请了你，已经成为好友")
                                              : tr("申请已发出，等对方同意"));
                                if (addButton)
                                    addButton->setText(tr("已申请"));
                                reload();
                            },
                            [this, addButton](const QString &message, int) {
                                if (addButton)
                                    addButton->setEnabled(true);
                                NotificationManager::showError(this, message);
                            });
                    });
                    rowLayout->addWidget(addButton);
                }

                m_sideListLayout->addWidget(row);
            }
            m_sideListLayout->addStretch(1);
        },
        [this](const QString &message, int) {
            setStatus(tr("搜索失败：%1").arg(message), true);
        });
}

// ────────────────────────────────────────────────────────────── 右栏聊天

void ForumFriendsPage::openConversation(const ForumFriendInfo &peer)
{
    m_peer = peer;
    m_messages.clear();
    m_lastMessageId = 0;
    m_hasMore = false;
    m_chatEmpty->hide();
    m_chatMain->show();
    m_moreButton->hide();
    m_chatInput->clear();
    m_chatName->setText(peer.isValid() ? peer.displayName : tr("…"));
    m_chatStatus->setText(peer.isValid() ? onlineText(peer) : QString());
    renderChatAvatar();
    ForumKit::clearLayout(m_msgsLayout);
    m_msgsLayout->addStretch(1);
    auto *loading = ForumKit::makeMuted(this, tr("正在加载消息…"));
    m_msgsLayout->insertWidget(0, loading);
    renderFriendRows(); // 高亮当前会话行

    ForumChatService::instance()->fetchMessages(
        peer.id, 0,
        [this](const ForumChatHistory &history) {
            // 请求期间切换了会话：丢弃过期结果
            if (history.peer.id != m_peer.id)
                return;
            if (history.peer.isValid())
            {
                m_peer = history.peer;
                renderChatHead();
            }
            m_messages.clear();
            m_lastMessageId = 0;
            for (const ForumChatMessage &message : history.messages)
            {
                m_messages.append(message);
                m_lastMessageId = qMax(m_lastMessageId, message.id);
            }
            m_hasMore = history.hasMore;
            renderMessages();
            scrollToBottom();
            renderFriendRows();
        },
        [this](const QString &message, int) {
            ForumKit::clearLayout(m_msgsLayout);
            m_msgsLayout->addStretch(1);
            auto *error = ForumKit::makeMuted(this, tr("加载失败：%1").arg(message));
            m_msgsLayout->insertWidget(0, error);
        });
}

void ForumFriendsPage::renderChatAvatar()
{
    // 头像是异步加载的（ForumImageStore），每次都重建一个头像标签放进槽位
    ForumKit::clearLayout(m_chatAvatarSlot->layout());
    m_chatAvatarSlot->layout()->addWidget(
        ForumKit::makeAvatar(m_chatAvatarSlot, m_peer.avatar, m_peer.displayName, 36));
}

void ForumFriendsPage::renderChatHead()
{
    m_chatName->setText(m_peer.displayName);
    m_chatStatus->setText(onlineText(m_peer));
    renderChatAvatar();
}

void ForumFriendsPage::renderMessages()
{
    const int scrollValue = m_msgsScroll->verticalScrollBar()->maximum() -
                            m_msgsScroll->verticalScrollBar()->value();
    ForumKit::clearLayout(m_msgsLayout);

    if (m_messages.isEmpty())
    {
        auto *empty = ForumKit::makeMuted(this, tr("还没有消息，打个招呼吧"));
        empty->setAlignment(Qt::AlignCenter);
        m_msgsLayout->addSpacing(20);
        m_msgsLayout->addWidget(empty);
        m_msgsLayout->addSpacing(20);
    }

    for (const ForumChatMessage &message : m_messages)
    {
        auto *row = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto *bubbleColumn = new QVBoxLayout();
        bubbleColumn->setSpacing(2);

        auto *bubble = new QLabel(message.body, row);
        bubble->setObjectName(message.mine ? QStringLiteral("forumChatBubbleMine")
                                           : QStringLiteral("forumChatBubbleTheirs"));
        bubble->setWordWrap(true);
        bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bubble->setMaximumWidth(360);
        bubbleColumn->addWidget(bubble);

        auto *time = new QLabel(messageTime(message.createdAt), row);
        time->setObjectName(QStringLiteral("forumMuted"));
        time->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: 10px;")));
        time->setAlignment(message.mine ? Qt::AlignRight : Qt::AlignLeft);
        bubbleColumn->addWidget(time);

        if (message.mine)
        {
            rowLayout->addStretch(1);
            rowLayout->addLayout(bubbleColumn);
        }
        else
        {
            rowLayout->addWidget(ForumKit::makeAvatar(row, m_peer.avatar, m_peer.displayName, 26));
            rowLayout->addLayout(bubbleColumn);
            rowLayout->addStretch(1);
        }
        m_msgsLayout->addWidget(row);
    }
    m_msgsLayout->addStretch(1);
    m_moreButton->setVisible(m_hasMore);
    m_msgsScroll->verticalScrollBar()->setValue(
        m_msgsScroll->verticalScrollBar()->maximum() - scrollValue);
}

void ForumFriendsPage::appendMessages(const QVector<ForumChatMessage> &messages)
{
    bool added = false;
    bool shouldScroll = m_msgsScroll->verticalScrollBar()->value() >=
                        m_msgsScroll->verticalScrollBar()->maximum() - 60; // 本来就接近底部
    for (const ForumChatMessage &message : messages)
    {
        bool exists = false;
        for (const ForumChatMessage &item : m_messages)
            if (item.id == message.id)
            {
                exists = true;
                break;
            }
        if (exists)
            continue;
        m_messages.append(message);
        m_lastMessageId = qMax(m_lastMessageId, message.id);
        added = true;
    }
    if (!added)
        return;
    std::sort(m_messages.begin(), m_messages.end(),
              [](const ForumChatMessage &a, const ForumChatMessage &b) { return a.id < b.id; });
    renderMessages();
    if (shouldScroll)
        scrollToBottom();
}

void ForumFriendsPage::scrollToBottom()
{
    m_msgsScroll->verticalScrollBar()->setValue(m_msgsScroll->verticalScrollBar()->maximum());
}

void ForumFriendsPage::closeConversation()
{
    m_peer = {};
    m_messages.clear();
    m_lastMessageId = 0;
    m_hasMore = false;
    m_chatMain->hide();
    m_chatEmpty->show();
    renderFriendRows();
}

void ForumFriendsPage::sendCurrentMessage()
{
    const QString body = m_chatInput->toPlainText().trimmed();
    if (body.isEmpty() || m_sending || m_peer.id <= 0)
        return;
    m_sending = true;
    m_sendButton->setEnabled(false);
    const int peerId = m_peer.id;
    ForumChatService::instance()->sendMessage(
        peerId, body,
        [this](const ForumChatMessage &message) {
            m_sending = false;
            m_sendButton->setEnabled(true);
            m_chatInput->clear();
            m_chatInput->setFocus();
            if (message.isValid())
                appendMessages(QVector<ForumChatMessage>{message});
            scrollToBottom();
        },
        [this](const QString &message, int httpStatus) {
            m_sending = false;
            m_sendButton->setEnabled(true);
            NotificationManager::showError(this, message);
            if (httpStatus == 403)
            {
                // 好友关系没了（对方删了你）：关掉会话并刷新列表
                closeConversation();
                reload();
            }
        });
}

void ForumFriendsPage::loadOlderMessages()
{
    if (m_peer.id <= 0 || !m_hasMore || m_messages.isEmpty())
        return;
    int oldestId = m_messages.first().id;
    for (const ForumChatMessage &message : m_messages)
        oldestId = qMin(oldestId, message.id);
    m_moreButton->setEnabled(false);
    ForumChatService::instance()->fetchMessages(
        m_peer.id, oldestId,
        [this](const ForumChatHistory &history) {
            m_moreButton->setEnabled(true);
            if (history.peer.id != m_peer.id)
                return;
            QVector<ForumChatMessage> merged = m_messages;
            for (const ForumChatMessage &message : history.messages)
            {
                bool exists = false;
                for (const ForumChatMessage &item : merged)
                    if (item.id == message.id)
                    {
                        exists = true;
                        break;
                    }
                if (!exists)
                    merged.append(message);
            }
            m_messages = merged;
            std::sort(m_messages.begin(), m_messages.end(),
                      [](const ForumChatMessage &a, const ForumChatMessage &b) {
                          return a.id < b.id;
                      });
            m_hasMore = history.hasMore;
            renderMessages();
        },
        [this](const QString &message, int) {
            m_moreButton->setEnabled(true);
            NotificationManager::showError(this, message);
        });
}

void ForumFriendsPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}
