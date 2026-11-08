/**
 * @file   ForumFriendsPage.cpp
 * @brief  论坛好友与私信页实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumFriendsPage.h"

#include <QDateTime>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "components/ForumKit.h"
#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumChatService.h"
#include "utils/ForumImageStore.h"
#include "utils/ResourceManager.h"
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
    // 官方客服是默认好友，状态固定成客服说明，不让用户以为没人接待
    if (peer.official)
        return QObject::tr("官方客服 · 随时提问");
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
    outer->setContentsMargins(18, 14, 18, 18);
    outer->setSpacing(0);

    // ── 左右两栏 ──
    auto *columns = new QHBoxLayout();
    columns->setSpacing(14);
    outer->addLayout(columns, 1);

    // ── 左栏：与侧边栏子导航面板同款（玻璃底气泡 + 头部 + 分组标题 + 条目） ──
    auto *sidePanel = new QFrame(this);
    sidePanel->setObjectName(QStringLiteral("forumSidePanel"));
    sidePanel->setAttribute(Qt::WA_StyledBackground, true);
    sidePanel->setFixedWidth(300);
    auto *sideShadow = new QGraphicsDropShadowEffect(sidePanel);
    sideShadow->setBlurRadius(20);
    sideShadow->setOffset(0, 4);
    sideShadow->setColor(QColor(0, 0, 0, 50));
    sidePanel->setGraphicsEffect(sideShadow);

    auto *sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);

    // 面板头部：标题 + 刷新（与 SubNavPanel 头部一致）
    auto *sideHeader = new QWidget(sidePanel);
    sideHeader->setObjectName(QStringLiteral("subNavHeader"));
    auto *sideHeaderLayout = new QHBoxLayout(sideHeader);
    sideHeaderLayout->setContentsMargins(16, 12, 10, 12);
    sideHeaderLayout->setSpacing(4);
    auto *sideTitle = new QLabel(tr("聊天"), sideHeader);
    sideTitle->setObjectName(QStringLiteral("forumSideTitle"));
    sideHeaderLayout->addWidget(sideTitle);
    sideHeaderLayout->addStretch(1);
    m_refreshButton = new QPushButton(sideHeader);
    m_refreshButton->setObjectName(QStringLiteral("subNavCollapseBtn"));
    m_refreshButton->setIcon(ResourceManager::instance()->loadThemedIcon(
        QStringLiteral(":/Images/Icons/refresh.svg")));
    m_refreshButton->setIconSize(QSize(16, 16));
    m_refreshButton->setFixedSize(24, 24);
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setToolTip(tr("刷新好友与私信"));
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() { reload(); });
    sideHeaderLayout->addWidget(m_refreshButton);
    sideLayout->addWidget(sideHeader);

    // 页签：好友 / 申请 / 添加好友（胶囊分段，同 AI 聊天页的模式切换）
    auto *tabRow = new QWidget(sidePanel);
    auto *tabRowLayout = new QHBoxLayout(tabRow);
    tabRowLayout->setContentsMargins(12, 10, 12, 2);
    tabRowLayout->setSpacing(0);
    auto *tabBar = new QWidget(tabRow);
    tabBar->setObjectName(QStringLiteral("forumSideTabs"));
    auto *tabBarLayout = new QHBoxLayout(tabBar);
    tabBarLayout->setContentsMargins(3, 3, 3, 3);
    tabBarLayout->setSpacing(0);
    m_friendsTabButton = new QPushButton(tr("好友"), tabBar);
    m_requestsTabButton = new QPushButton(tr("申请"), tabBar);
    m_addTabButton = new QPushButton(tr("添加好友"), tabBar);
    for (QPushButton *button : {m_friendsTabButton, m_requestsTabButton, m_addTabButton})
    {
        button->setObjectName(QStringLiteral("forumSideTab"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(26);
        tabBarLayout->addWidget(button);
    }
    m_friendsTabButton->setChecked(true);
    connect(m_friendsTabButton, &QPushButton::clicked, this,
            [this]() { setTab(FriendsTab); });
    connect(m_requestsTabButton, &QPushButton::clicked, this,
            [this]() { setTab(RequestsTab); });
    connect(m_addTabButton, &QPushButton::clicked, this, [this]() { setTab(AddTab); });
    tabRowLayout->addWidget(tabBar);
    sideLayout->addWidget(tabRow);

    // 分组标题：随页签切换为「好友 · N」「申请 · N」「添加好友」
    m_sideGroupTitle = new QLabel(tr("好友"), sidePanel);
    m_sideGroupTitle->setObjectName(QStringLiteral("forumSideGroupTitle"));
    sideLayout->addWidget(m_sideGroupTitle);

    // 加载 / 出错提示（无内容时不占位）
    m_statusLabel = new QLabel(sidePanel);
    m_statusLabel->setObjectName(QStringLiteral("forumSideStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    sideLayout->addWidget(m_statusLabel);

    m_sideScroll = new QScrollArea(sidePanel);
    m_sideScroll->setObjectName(QStringLiteral("forumSideScroll"));
    m_sideScroll->setWidgetResizable(true);
    m_sideScroll->setFrameShape(QFrame::NoFrame);
    m_sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sideContainer = new QWidget(m_sideScroll);
    m_sideContainer->setAttribute(Qt::WA_StyledBackground, false);
    m_sideListLayout = new QVBoxLayout(m_sideContainer);
    m_sideListLayout->setContentsMargins(8, 2, 8, 10);
    m_sideListLayout->setSpacing(2);
    m_sideListLayout->addStretch(1);
    m_sideScroll->setWidget(m_sideContainer);
    sideLayout->addWidget(m_sideScroll, 1);
    columns->addWidget(sidePanel);

    // 右栏：聊天区直接铺在页面上（不再套一层大卡片）
    m_chatCard = new QWidget(this);
    m_chatCard->setObjectName(QStringLiteral("forumChatStage"));
    auto *chatLayout = new QVBoxLayout(m_chatCard);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

    // 空状态：大图标 + 两行说明
    m_chatEmpty = new QWidget(m_chatCard);
    auto *emptyLayout = new QVBoxLayout(m_chatEmpty);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(6);
    auto *emptyIcon = new QLabel(QStringLiteral("💬"), m_chatEmpty);
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setStyleSheet(StyleKit::resolve(QStringLiteral("font-size: 30px; background: transparent;")));
    auto *emptyText = ForumKit::makeMuted(m_chatEmpty, tr("在左侧选择一个好友，开始聊天"));
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch(1);
    emptyLayout->addWidget(emptyIcon);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch(1);
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

    // 输入区：表情 / 图片 / 「字数统计 + 输入框」一列 / 发送按钮
    auto *composer = new QWidget(m_chatMain);
    composer->setObjectName(QStringLiteral("forumChatComposer"));
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(16, 10, 16, 10);
    composerLayout->setSpacing(8);

    m_emojiButton = new QPushButton(QStringLiteral("😊"), composer);
    m_emojiButton->setObjectName(QStringLiteral("forumChatToolButton"));
    m_emojiButton->setFixedSize(30, 30);
    m_emojiButton->setCursor(Qt::PointingHandCursor);
    m_emojiButton->setToolTip(tr("表情"));
    connect(m_emojiButton, &QPushButton::clicked, this, &ForumFriendsPage::toggleEmojiPopup);
    composerLayout->addWidget(m_emojiButton);

    m_imageButton = new QPushButton(QStringLiteral("📷"), composer);
    m_imageButton->setObjectName(QStringLiteral("forumChatToolButton"));
    m_imageButton->setFixedSize(30, 30);
    m_imageButton->setCursor(Qt::PointingHandCursor);
    m_imageButton->setToolTip(tr("发送图片（也可以直接把截图粘进输入框）"));
    connect(m_imageButton, &QPushButton::clicked, this, &ForumFriendsPage::pickAndSendImage);
    composerLayout->addWidget(m_imageButton);

    auto *inputColumn = new QVBoxLayout();
    inputColumn->setSpacing(3);
    m_charCounter = new QLabel(composer);
    m_charCounter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_charCounter->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: 10.5px;")));
    m_charCounter->hide();
    inputColumn->addWidget(m_charCounter);
    m_chatInput = new QTextEdit(composer);
    m_chatInput->setObjectName(QStringLiteral("forumChatInput"));
    m_chatInput->setPlaceholderText(tr("输入消息，Enter 发送，Shift+Enter 换行"));
    m_chatInput->setFixedHeight(44);
    m_chatInput->setAcceptRichText(false);
    m_chatInput->installEventFilter(this);
    inputColumn->addWidget(m_chatInput);
    composerLayout->addLayout(inputColumn, 1);

    m_sendButton = new QPushButton(tr("发送"), composer);
    m_sendButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setFixedHeight(38);
    m_sendButton->setFixedWidth(78);
    m_sendButton->setToolTip(tr("Enter 发送，Shift+Enter 换行"));
    connect(m_sendButton, &QPushButton::clicked, this, &ForumFriendsPage::sendCurrentMessage);
    composerLayout->addWidget(m_sendButton);
    composerLayout->setAlignment(m_sendButton, Qt::AlignBottom);
    chatMainLayout->addWidget(composer);

    // 引用条：回复某条消息时出现在输入区上方
    m_replyBar = new QWidget(m_chatMain);
    m_replyBar->setObjectName(QStringLiteral("forumChatReplyBar"));
    auto *replyLayout = new QHBoxLayout(m_replyBar);
    replyLayout->setContentsMargins(16, 6, 16, 6);
    replyLayout->setSpacing(8);
    m_replyLabel = new QLabel(m_replyBar);
    m_replyLabel->setObjectName(QStringLiteral("forumMuted"));
    replyLayout->addWidget(m_replyLabel, 1);
    m_replyCancelButton = new QPushButton(QStringLiteral("✕"), m_replyBar);
    m_replyCancelButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_replyCancelButton->setFixedSize(22, 22);
    m_replyCancelButton->setCursor(Qt::PointingHandCursor);
    m_replyCancelButton->setToolTip(tr("取消引用"));
    connect(m_replyCancelButton, &QPushButton::clicked, this, &ForumFriendsPage::clearReplyTarget);
    replyLayout->addWidget(m_replyCancelButton);
    m_replyBar->hide();
    chatMainLayout->addWidget(m_replyBar);

    // 输入框随内容长高（1～4 行），接近字数上限时显示计数
    connect(m_chatInput, &QTextEdit::textChanged, this, [this]() {
        refreshInputSize();
        const int n = m_chatInput->toPlainText().length();
        m_charCounter->setVisible(n >= 800);
        if (n >= 800)
            m_charCounter->setText(tr("%1 / 1000").arg(n));
    });

    chatLayout->addWidget(m_chatMain, 1);
    columns->addWidget(m_chatCard, 1);

    connect(m_chatProfileButton, &QPushButton::clicked, this,
            [this]() { emit userOpenRequested(m_peer.id); });
    connect(m_chatRemoveButton, &QPushButton::clicked, this, [this]() {
        if (m_peer.id <= 0)
            return;
        if (m_peer.official)
        {
            NotificationManager::showInfo(this, tr("官方客服是默认好友，不能删除"));
            return;
        }
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
        if (m_sideGroupTitle)
            m_sideGroupTitle->setText(tr("好友"));
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
        // 截图 Ctrl+V：剪贴板里有图片就直接当图片消息发出去
        if (keyEvent->matches(QKeySequence::Paste) && m_peer.id > 0)
        {
            const QImage clip = QApplication::clipboard()->image();
            if (!clip.isNull())
            {
                const QString path = QDir::tempPath() + QStringLiteral("/bbf_chat_paste_%1.png")
                                                            .arg(QDateTime::currentMSecsSinceEpoch());
                if (clip.save(path, "PNG"))
                    sendImageFromPath(path);
                return true;
            }
        }
    }
    // 查看图片弹窗：点任意位置关闭
    if (event->type() == QEvent::MouseButtonPress && watched &&
        watched->property("imagePreviewDialog").toBool())
    {
        static_cast<QWidget *>(watched)->close();
        return true;
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

    if (m_sideGroupTitle)
        m_sideGroupTitle->setText(m_snapshot.friends.isEmpty()
                                      ? tr("好友")
                                      : tr("好友 · %1").arg(m_snapshot.friends.size()));

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
        row->setObjectName(QStringLiteral("forumSideRow"));
        // 当前正在聊的这一条按「选中」样式高亮，和子导航选中项一致
        row->setProperty("selected", m_peer.isValid() && m_peer.id == friendInfo.id);
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
        if (friendInfo.official)
        {
            auto *official = new QLabel(tr("官方"), row);
            official->setObjectName(QStringLiteral("forumOfficialBadge"));
            official->setToolTip(tr("方块盒子官方客服，遇到问题可以随时提问"));
            nameRow->addWidget(official);
        }
        auto *online = new QLabel(row);
        online->setText(friendInfo.online ? QStringLiteral("●") : QStringLiteral("○"));
        online->setToolTip(onlineText(friendInfo));
        // 官方客服行已经有「官方」徽章，不再用在线圆点占位
        online->setVisible(!friendInfo.official);
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
        if (friendInfo.lastMessageKind == QStringLiteral("image"))
            preview = tr("[图片]");
        else if (friendInfo.lastMessageKind == QStringLiteral("withdrawn"))
            preview = tr("撤回了一条消息");
        else if (!friendInfo.lastMessageBody.isEmpty())
            preview = friendInfo.lastMessageBody;
        if (!friendInfo.lastMessageBody.isEmpty() && friendInfo.lastMessageMine &&
            friendInfo.lastMessageKind != QStringLiteral("withdrawn"))
            preview = tr("我：%1").arg(preview);
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

    if (m_sideGroupTitle)
    {
        const int count = m_snapshot.incoming.size() + m_snapshot.outgoing.size();
        m_sideGroupTitle->setText(count > 0 ? tr("申请 · %1").arg(count) : tr("申请"));
    }

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
        row->setObjectName(QStringLiteral("forumSideRow"));
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
        row->setObjectName(QStringLiteral("forumSideRow"));
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

    if (m_sideGroupTitle)
        m_sideGroupTitle->setText(tr("添加好友"));

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
                row->setObjectName(QStringLiteral("forumSideRow"));
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
    // 官方客服是默认好友，没有「删除好友」这一项
    m_chatRemoveButton->setVisible(!peer.official);
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
    // 头像是异步加载的（ForumImageStore），每次都重建一个头像标签放进槽位；
    // 在线时在头像右下角叠一颗绿点
    ForumKit::clearLayout(m_chatAvatarSlot->layout());

    QWidget *wrap = new QWidget(m_chatAvatarSlot);
    wrap->setFixedSize(36, 36);
    QLabel *avatar = ForumKit::makeAvatar(wrap, m_peer.avatar, m_peer.displayName, 36);
    avatar->setGeometry(0, 0, 36, 36);
    if (m_peer.online)
    {
        QLabel *dot = new QLabel(wrap);
        dot->setObjectName(QStringLiteral("forumOnlineDot"));
        dot->setFixedSize(11, 11);
        dot->setGeometry(26, 26, 11, 11);
        dot->setToolTip(tr("在线"));
        dot->raise();
        dot->show();
    }
    m_chatAvatarSlot->layout()->addWidget(wrap);
}

void ForumFriendsPage::renderChatHead()
{
    m_chatName->setText(m_peer.displayName);
    m_chatStatus->setText(onlineText(m_peer));
    m_chatStatus->setStyleSheet(StyleKit::resolve(
        m_peer.official ? QStringLiteral("font-size: 11px; color: @PRIMARY@;")
                        : m_peer.online ? QStringLiteral("font-size: 11px; color: @SUCCESS@;")
                                        : QStringLiteral("font-size: 11px; color: @TEXT_TERTIARY@;")));
    m_chatRemoveButton->setVisible(!m_peer.official);
    renderChatAvatar();
}

QWidget *ForumFriendsPage::makeDateChip(qint64 seconds)
{
    const QDate day = QDateTime::fromSecsSinceEpoch(seconds).date();
    const QDate today = QDate::currentDate();
    QString text;
    if (day == today)
        text = tr("今天");
    else if (day == today.addDays(-1))
        text = tr("昨天");
    else
        text = day.toString(tr("M月d日"));

    auto *chip = new QLabel(text, this);
    chip->setObjectName(QStringLiteral("forumChatDateChip"));
    chip->setAlignment(Qt::AlignCenter);
    return chip;
}

void ForumFriendsPage::renderMessages()
{
    const int scrollValue = m_msgsScroll->verticalScrollBar()->maximum() -
                            m_msgsScroll->verticalScrollBar()->value();
    ForumKit::clearLayout(m_msgsLayout);

    if (m_messages.isEmpty())
    {
        auto *emptyIcon = new QLabel(QStringLiteral("👋"), this);
        emptyIcon->setAlignment(Qt::AlignCenter);
        emptyIcon->setStyleSheet(StyleKit::resolve(QStringLiteral("font-size: 26px; background: transparent;")));
        auto *empty = ForumKit::makeMuted(this, tr("还没有消息，打个招呼吧"));
        empty->setAlignment(Qt::AlignCenter);
        m_msgsLayout->addSpacing(28);
        m_msgsLayout->addWidget(emptyIcon);
        m_msgsLayout->addWidget(empty);
        m_msgsLayout->addSpacing(28);
        m_msgsLayout->addStretch(1);
        m_moreButton->setVisible(false);
        return;
    }

    QDate prevDay;
    for (int i = 0; i < m_messages.size(); ++i)
    {
        const ForumChatMessage &message = m_messages.at(i);
        const QDateTime moment = QDateTime::fromSecsSinceEpoch(message.createdAt);

        // 跨天插入日期分隔条
        if (!prevDay.isValid() || moment.date() != prevDay)
        {
            m_msgsLayout->addWidget(makeDateChip(message.createdAt));
            m_msgsLayout->addSpacing(6);
            prevDay = moment.date();
        }

        // 撤回占位：一行居中的灰色小字
        if (message.isWithdrawn())
        {
            auto *tombstone = new QLabel(message.mine ? tr("你撤回了一条消息")
                                                      : tr("对方撤回了一条消息"), this);
            tombstone->setObjectName(QStringLiteral("forumChatWithdraw"));
            tombstone->setAlignment(Qt::AlignCenter);
            m_msgsLayout->addWidget(tombstone);
            continue;
        }

        // 一组 = 相邻且同一发送者、间隔 5 分钟内的消息；组首显示头像和时间
        const bool groupStart = i == 0 ||
                                m_messages.at(i - 1).mine != message.mine ||
                                m_messages.at(i - 1).isWithdrawn() ||
                                message.createdAt - m_messages.at(i - 1).createdAt > 5 * 60;

        auto *row = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, groupStart ? 4 : 1, 0, 0);
        rowLayout->setSpacing(8);

        auto *bubbleColumn = new QVBoxLayout();
        bubbleColumn->setSpacing(3);

        // 引用条（贴在气泡/图片上方，两侧一样式）
        if (message.replyTo > 0)
        {
            auto *quote = new QLabel(tr("回复 %1：%2")
                                         .arg(message.replyMine ? tr("我") : m_peer.displayName,
                                              message.replyBody), row);
            quote->setObjectName(QStringLiteral("forumChatQuote"));
            quote->setWordWrap(true);
            quote->setMaximumWidth(400);
            // 内联样式：引用条在气泡外独立显示，不用和气泡内文字规则打架
            quote->setStyleSheet(StyleKit::resolve(
                QStringLiteral("color: @TEXT_SECONDARY@; font-size: 12px; background: transparent;"
                               "border-left: 2px solid @BORDER@; padding: 1px 0 1px 8px;")));
            bubbleColumn->addWidget(quote);
        }

        const int messageId = message.id;
        if (message.isImage())
        {
            // 图片消息：圆角卡片 + 异步加载的缩略图，点击看大图
            auto *imageFrame = new QFrame(row);
            imageFrame->setObjectName(QStringLiteral("forumChatImageFrame"));
            imageFrame->setProperty("chatImage", message.image);
            imageFrame->setMaximumWidth(400);
            auto *imageLayout = new QVBoxLayout(imageFrame);
            imageLayout->setContentsMargins(4, 4, 4, 4);
            imageLayout->setSpacing(4);
            auto *imageLabel = new QLabel(imageFrame);
            imageLabel->setFixedSize(220, 160);
            imageLabel->setAlignment(Qt::AlignCenter);
            imageLabel->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: 12px;")));
            imageLabel->setText(tr("图片加载中…"));
            imageLayout->addWidget(imageLabel);
            if (!message.body.isEmpty())
            {
                auto *caption = new QLabel(message.body, imageFrame);
                caption->setWordWrap(true);
                caption->setStyleSheet(
                    StyleKit::resolve(QStringLiteral("color: @TEXT_PRIMARY@; font-size: 13px;")));
                caption->setTextInteractionFlags(Qt::TextSelectableByMouse);
                imageLayout->addWidget(caption);
            }
            imageFrame->setCursor(Qt::PointingHandCursor);
            imageFrame->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(imageFrame, &QWidget::customContextMenuRequested, this,
                    [this, messageId, imageFrame](const QPoint &pos) {
                        showMessageMenu(messageId, imageFrame->mapToGlobal(pos));
                    });
            ForumImageStore::instance()->requestImage(
                ForumAccountService::instance()->absoluteUrl(message.image),
                [this, imageLabel](const QImage &image) {
                    if (!imageLabel || image.isNull())
                        return;
                    imageLabel->setText(QString());
                    imageLabel->setStyleSheet(QString());
                    imageLabel->setPixmap(QPixmap::fromImage(image)
                                              .scaled(220, 160, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
                });
            bubbleColumn->addWidget(imageFrame);
        }
        else
        {
            // 文字气泡：QFrame 承载（里面可以有引用），本身只当底色
            auto *bubble = new QFrame(row);
            bubble->setObjectName(message.mine ? QStringLiteral("forumChatBubbleMine")
                                               : QStringLiteral("forumChatBubbleTheirs"));
            bubble->setMaximumWidth(400);
            auto *bubbleLayout = new QVBoxLayout(bubble);
            bubbleLayout->setContentsMargins(0, 0, 0, 0);
            bubbleLayout->setSpacing(4);
            auto *body = new QLabel(message.body, bubble);
            body->setWordWrap(true);
            body->setTextInteractionFlags(Qt::TextSelectableByMouse);
            body->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(body, &QWidget::customContextMenuRequested, this,
                    [this, messageId, body](const QPoint &pos) {
                        showMessageMenu(messageId, body->mapToGlobal(pos));
                    });
            bubbleLayout->addWidget(body);
            bubbleColumn->addWidget(bubble);
        }

        if (groupStart)
        {
            // 我发的消息：对方打开过会话就是「已读」
            QString meta = messageTime(message.createdAt);
            if (message.mine && message.read)
                meta += tr(" · 已读");
            auto *time = new QLabel(meta, row);
            time->setObjectName(QStringLiteral("forumMuted"));
            time->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: 10px;")));
            time->setAlignment(message.mine ? Qt::AlignRight : Qt::AlignLeft);
            bubbleColumn->addWidget(time);
        }

        if (message.mine)
        {
            rowLayout->addStretch(1);
            rowLayout->addLayout(bubbleColumn);
        }
        else
        {
            // 组首带头像，组内其余留同宽空位对齐
            if (groupStart)
                rowLayout->addWidget(ForumKit::makeAvatar(row, m_peer.avatar, m_peer.displayName, 26));
            else
                rowLayout->addSpacing(26 + 8);
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
    clearReplyTarget();
    if (m_emojiPopup)
        m_emojiPopup->hide();
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
    const int replyTo = m_replyTo;
    ForumChatService::instance()->sendMessage(
        peerId, body, replyTo,
        [this](const ForumChatMessage &message) {
            m_sending = false;
            m_sendButton->setEnabled(true);
            m_chatInput->clear();
            m_chatInput->setFocus();
            if (message.isValid())
                appendMessages(QVector<ForumChatMessage>{message});
            scrollToBottom();
            clearReplyTarget();
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
    // 没有内容时不占面板高度
    m_statusLabel->setVisible(!text.isEmpty());
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}

void ForumFriendsPage::refreshInputSize()
{
    if (!m_chatInput)
        return;
    // 跟着内容长高：1 行时保持 44，最多约 4 行，超出后内部滚动
    const int textHeight = qRound(m_chatInput->document()->size().height());
    const int target = qBound(44, textHeight + 16, 120);
    if (m_chatInput->height() != target)
        m_chatInput->setFixedHeight(target);
}


// ────────────────────────────────────────────────────────────── 引用 / 撤回 / 图片 / 表情

QString ForumFriendsPage::messagePreview(const ForumChatMessage &message) const
{
    if (message.isWithdrawn())
        return tr("撤回了一条消息");
    if (message.isImage())
        return tr("[图片]");
    return message.body;
}

void ForumFriendsPage::setReplyTarget(int messageId, const QString &preview, bool mine)
{
    m_replyTo = messageId;
    m_replyMine = mine;
    m_replyPreview = preview;
    m_replyLabel->setText(tr("回复 %1：%2").arg(mine ? tr("我") : m_peer.displayName, preview));
    m_replyLabel->setToolTip(m_replyLabel->text());
    m_replyBar->show();
    m_chatInput->setFocus();
}

void ForumFriendsPage::clearReplyTarget()
{
    m_replyTo = 0;
    m_replyMine = false;
    m_replyPreview.clear();
    if (m_replyLabel)
        m_replyLabel->clear();
    if (m_replyBar)
        m_replyBar->hide();
}

void ForumFriendsPage::showMessageMenu(int messageId, const QPoint &globalPos)
{
    const ForumChatMessage *found = nullptr;
    for (const ForumChatMessage &item : m_messages)
    {
        if (item.id == messageId)
        {
            found = &item;
            break;
        }
    }
    if (!found || m_peer.id <= 0)
        return;
    const ForumChatMessage message = *found;

    QMenu menu(this);
    QAction *replyAction = nullptr;
    QAction *copyAction = nullptr;
    QAction *withdrawAction = nullptr;
    if (!message.isWithdrawn())
    {
        replyAction = menu.addAction(tr("回复"));
        if (!message.body.isEmpty())
            copyAction = menu.addAction(tr("复制文字"));
        // 只能撤回自己 10 分钟内发的消息
        if (message.mine &&
            QDateTime::currentSecsSinceEpoch() - message.createdAt <= 10 * 60)
            withdrawAction = menu.addAction(tr("撤回"));
    }
    if (menu.isEmpty())
        return;
    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    if (chosen == replyAction)
    {
        setReplyTarget(message.id, messagePreview(message), message.mine);
    }
    else if (chosen == copyAction)
    {
        QApplication::clipboard()->setText(message.body);
    }
    else if (chosen == withdrawAction)
    {
        ForumChatService::instance()->withdrawMessage(
            message.id,
            [this]() { openConversation(m_peer); },
            [this](const QString &msg, int) { NotificationManager::showError(this, msg); });
    }
}

void ForumFriendsPage::showImagePreview(const QString &imagePath)
{
    QDialog dialog(this);
    dialog.setProperty("imagePreviewDialog", true);
    dialog.installEventFilter(this);
    dialog.setWindowTitle(tr("查看图片"));
    dialog.setModal(true);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    QPointer<QLabel> label = new QLabel(tr("图片加载中…"), &dialog);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    ForumImageStore::instance()->requestImage(
        ForumAccountService::instance()->absoluteUrl(imagePath),
        [&dialog, label](const QImage &image) {
            if (label.isNull())
                return;
            if (image.isNull())
            {
                label->setText(QObject::tr("图片加载失败"));
                return;
            }
            QSize size = image.size();
            const QSize limit = QApplication::primaryScreen()->availableGeometry().size() * 0.7;
            if (size.width() > limit.width() || size.height() > limit.height())
                size.scale(limit, Qt::KeepAspectRatio);
            label->setPixmap(QPixmap::fromImage(image).scaled(
                size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            dialog.adjustSize();
        });

    dialog.setCursor(Qt::PointingHandCursor);
    dialog.exec();
}

void ForumFriendsPage::pickAndSendImage()
{
    if (m_peer.id <= 0 || m_sending)
        return;
    // 系统文件选择器
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.gif *.bmp)"));
    if (path.isEmpty())
        return;
    sendImageFromPath(path);
}

void ForumFriendsPage::sendImageFromPath(const QString &filePath)
{
    if (m_peer.id <= 0 || m_sending)
        return;
    m_sending = true;
    m_sendButton->setEnabled(false);
    m_imageButton->setEnabled(false);
    setStatus(tr("正在发送图片…"));
    const int peerId = m_peer.id;
    const int replyTo = m_replyTo;
    const bool isTempPaste = filePath.startsWith(QDir::tempPath());
    ForumChatService::instance()->sendImageMessage(
        peerId, filePath, QString(), replyTo,
        [this, isTempPaste, filePath](const ForumChatMessage &message) {
            m_sending = false;
            m_sendButton->setEnabled(true);
            m_imageButton->setEnabled(true);
            setStatus(QString());
            if (isTempPaste)
                QFile::remove(filePath);
            if (message.isValid())
                appendMessages(QVector<ForumChatMessage>{message});
            scrollToBottom();
            clearReplyTarget();
        },
        [this, isTempPaste, filePath](const QString &msg, int) {
            m_sending = false;
            m_sendButton->setEnabled(true);
            m_imageButton->setEnabled(true);
            setStatus(QString());
            if (isTempPaste)
                QFile::remove(filePath);
            NotificationManager::showError(this, msg);
        });
}

void ForumFriendsPage::toggleEmojiPopup()
{
    if (!m_emojiPopup)
        buildEmojiPopup();
    if (m_emojiPopup->isVisible())
    {
        m_emojiPopup->hide();
        return;
    }
    m_emojiPopup->adjustSize();
    const QSize size = m_emojiPopup->size();
    QPoint pos = m_emojiButton->mapToGlobal(QPoint(0, 0));
    pos.setY(pos.y() - size.height() - 6);
    pos.setX(qMax(4, pos.x()));
    m_emojiPopup->move(pos);
    m_emojiPopup->show();
}

void ForumFriendsPage::buildEmojiPopup()
{
    m_emojiPopup = new QWidget(this, Qt::Popup);
    m_emojiPopup->setObjectName(QStringLiteral("forumEmojiPopup"));
    auto *layout = new QGridLayout(m_emojiPopup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(2);

    static const QStringList emojis = {
        QStringLiteral("😀"), QStringLiteral("😁"), QStringLiteral("😂"), QStringLiteral("🤣"),
        QStringLiteral("😊"), QStringLiteral("😍"), QStringLiteral("😘"), QStringLiteral("😜"),
        QStringLiteral("🤔"), QStringLiteral("😎"), QStringLiteral("😴"), QStringLiteral("😭"),
        QStringLiteral("🥺"), QStringLiteral("😡"), QStringLiteral("🤯"), QStringLiteral("🙄"),
        QStringLiteral("👋"), QStringLiteral("👍"), QStringLiteral("👎"), QStringLiteral("👌"),
        QStringLiteral("🙏"), QStringLiteral("💪"), QStringLiteral("🤝"), QStringLiteral("✌️"),
        QStringLiteral("🎉"), QStringLiteral("🔥"), QStringLiteral("❤️"), QStringLiteral("💔"),
        QStringLiteral("🌹"), QStringLiteral("⭐"), QStringLiteral("🌙"), QStringLiteral("☀️"),
        QStringLiteral("🎮"), QStringLiteral("⛏️"), QStringLiteral("🏠"), QStringLiteral("🐷"),
        QStringLiteral("🐮"), QStringLiteral("🐶"), QStringLiteral("🐱"), QStringLiteral("🐭"),
        QStringLiteral("🍕"), QStringLiteral("🍔"), QStringLiteral("🍺"), QStringLiteral("☕"),
        QStringLiteral("🎁"), QStringLiteral("💰"), QStringLiteral("⚡"), QStringLiteral("💎"),
    };
    const int columns = 8;
    for (int i = 0; i < emojis.size(); ++i)
    {
        auto *button = new QToolButton(m_emojiPopup);
        button->setText(emojis.at(i));
        button->setAutoRaise(true);
        button->setFixedSize(32, 32);
        const QString emoji = emojis.at(i);
        connect(button, &QToolButton::clicked, this, [this, emoji]() {
            m_chatInput->insertPlainText(emoji);
            m_chatInput->setFocus();
            m_emojiPopup->hide();
        });
        layout->addWidget(button, i / columns, i % columns);
    }
}
