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
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "components/AppMessageBox.h"
#include "components/ChatBookmarkDialog.h"
#include "components/ChatForwardDialog.h"
#include "components/ForumKit.h"
#include "components/NotificationManager.h"
#include "components/CreateGroupDialog.h"
#include "utils/ChatBookmarkStore.h"
#include "utils/ChatUiStore.h"
#include "utils/DownloadUtils.h"
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

/** 消息正文的单行自然宽度：中文长消息不让布局把它压成窄窄一条 */
int naturalTextWidth(const QFont &font, const QString &text, int maximum)
{
    if (text.isEmpty())
        return 0;
    const QFontMetrics metrics(font);
    int widest = 0;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines)
        widest = qMax(widest, metrics.horizontalAdvance(line));
    return qMin(maximum, widest);
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

    // 会话搜索：输入停顿后再重建左栏，避免每敲一个字都重排
    m_convFilterTimer.setSingleShot(true);
    m_convFilterTimer.setInterval(180);
    connect(&m_convFilterTimer, &QTimer::timeout, this, [this]() {
        if (m_tab == FriendsTab)
            renderFriendRows();
    });

    // 草稿：停下打字 0.6 秒才写盘
    m_draftTimer.setSingleShot(true);
    m_draftTimer.setInterval(600);
    connect(&m_draftTimer, &QTimer::timeout, this, &ForumFriendsPage::saveActiveDraft);

    // 令牌失效（在别处退出登录 / 被踢）：回到未登录的空状态
    connect(ForumAccountService::instance(), &ForumAccountService::sessionExpired, this, [this]() {
        m_snapshot = {};
        m_peer = {};
        m_group = {};
        m_groupMembers.clear();
        m_messages.clear();
        m_lastMessageId = 0;
        m_groupLastMessageId = 0;
        m_activeKey.clear();
        m_unreadAnchorId = 0;
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
    // 聊天收藏：集中查看收藏过的消息
    m_bookmarkButton = new QPushButton(sideHeader);
    m_bookmarkButton->setObjectName(QStringLiteral("subNavCollapseBtn"));
    m_bookmarkButton->setIcon(ResourceManager::instance()->loadThemedIcon(
        QStringLiteral(":/Images/Icons/star.svg")));
    m_bookmarkButton->setIconSize(QSize(16, 16));
    m_bookmarkButton->setFixedSize(24, 24);
    m_bookmarkButton->setCursor(Qt::PointingHandCursor);
    m_bookmarkButton->setToolTip(tr("聊天收藏"));
    connect(m_bookmarkButton, &QPushButton::clicked, this, &ForumFriendsPage::openBookmarks);
    sideHeaderLayout->addWidget(m_bookmarkButton);
    m_createGroupButton = new QPushButton(sideHeader);
    m_createGroupButton->setObjectName(QStringLiteral("subNavCollapseBtn"));
    m_createGroupButton->setText(QStringLiteral("＋"));
    m_createGroupButton->setFixedSize(24, 24);
    m_createGroupButton->setCursor(Qt::PointingHandCursor);
    m_createGroupButton->setToolTip(tr("发起群聊"));
    connect(m_createGroupButton, &QPushButton::clicked, this, &ForumFriendsPage::openCreateGroupDialog);
    sideHeaderLayout->addWidget(m_createGroupButton);
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

    // 会话搜索：只在「好友」页签显示，按昵称 / 最后一条消息 / 草稿过滤
    m_convSearchRow = new QWidget(sidePanel);
    auto *convSearchRowLayout = new QVBoxLayout(m_convSearchRow);
    convSearchRowLayout->setContentsMargins(12, 8, 12, 2);
    convSearchRowLayout->setSpacing(0);
    m_convSearchEdit = new QLineEdit(m_convSearchRow);
    m_convSearchEdit->setObjectName(QStringLiteral("forumConvSearch"));
    m_convSearchEdit->setPlaceholderText(tr("搜索会话或消息"));
    m_convSearchEdit->setClearButtonEnabled(true);
    m_convSearchEdit->setFixedHeight(30);
    connect(m_convSearchEdit, &QLineEdit::textChanged, this,
            [this]() { updateConversationFilter(); });
    convSearchRowLayout->addWidget(m_convSearchEdit);
    sideLayout->addWidget(m_convSearchRow);

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

    // 空状态：居中的图标胶囊 + 标题 + 说明，避免整栏只剩一句灰字
    m_chatEmpty = new QWidget(m_chatCard);
    auto *emptyLayout = new QVBoxLayout(m_chatEmpty);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(8);
    auto *emptyIcon = new QLabel(m_chatEmpty);
    emptyIcon->setObjectName(QStringLiteral("forumChatEmptyIcon"));
    emptyIcon->setFixedSize(76, 76);
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setPixmap(ResourceManager::instance()
                             ->loadThemedIcon(QStringLiteral(":/Images/Icons/topbar_chat.svg"))
                             .pixmap(32, 32));
    auto *emptyTitle = new QLabel(tr("开始一段对话"), m_chatEmpty);
    emptyTitle->setObjectName(QStringLiteral("forumChatEmptyTitle"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto *emptyText = ForumKit::makeMuted(m_chatEmpty, tr("在左侧选择一个好友或群聊，开始聊天"));
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch(1);
    emptyLayout->addWidget(emptyIcon, 0, Qt::AlignHCenter);
    emptyLayout->addSpacing(2);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch(1);
    chatLayout->addWidget(m_chatEmpty, 1);

    m_chatMain = new QWidget(m_chatCard);
    m_chatMain->hide();
    auto *chatMainLayout = new QVBoxLayout(m_chatMain);
    chatMainLayout->setContentsMargins(0, 0, 0, 0);
    chatMainLayout->setSpacing(0);

    // 会话头：悬浮玻璃条（头像 / 名字 / 状态 / 主页 / 删除好友）
    auto *head = new QWidget(m_chatMain);
    head->setObjectName(QStringLiteral("forumChatHead"));
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(18, 12, 18, 12);
    headLayout->setSpacing(10);
    m_chatAvatarSlot = new QWidget(head);
    m_chatAvatarSlot->setFixedSize(40, 40);
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
    // 私聊一键拉群：打开建群弹窗并预选对方
    m_chatGroupButton = new QPushButton(tr("拉群"), head);
    m_chatGroupButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_chatGroupButton->setCursor(Qt::PointingHandCursor);
    m_chatGroupButton->setFixedHeight(26);
    headLayout->addWidget(m_chatGroupButton);
    m_chatRemoveButton = new QPushButton(tr("删除好友"), head);
    m_chatRemoveButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_chatRemoveButton->setCursor(Qt::PointingHandCursor);
    m_chatRemoveButton->setFixedHeight(26);
    headLayout->addWidget(m_chatRemoveButton);
    // 群聊专用：看成员 / 邀请好友 / 退出（私聊会话时隐藏）
    m_chatMembersButton = new QPushButton(tr("成员"), head);
    m_chatMembersButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_chatMembersButton->setCursor(Qt::PointingHandCursor);
    m_chatMembersButton->setFixedHeight(26);
    m_chatMembersButton->hide();
    headLayout->addWidget(m_chatMembersButton);
    m_chatInviteButton = new QPushButton(tr("邀请"), head);
    m_chatInviteButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_chatInviteButton->setCursor(Qt::PointingHandCursor);
    m_chatInviteButton->setFixedHeight(26);
    m_chatInviteButton->hide();
    headLayout->addWidget(m_chatInviteButton);
    m_chatLeaveButton = new QPushButton(tr("退出群聊"), head);
    m_chatLeaveButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_chatLeaveButton->setCursor(Qt::PointingHandCursor);
    m_chatLeaveButton->setFixedHeight(26);
    m_chatLeaveButton->hide();
    headLayout->addWidget(m_chatLeaveButton);
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

    // 消息列表：留出更宽松的呼吸空间，滚动条收细
    m_msgsScroll = new QScrollArea(m_chatMain);
    m_msgsScroll->setObjectName(QStringLiteral("forumChatMsgs"));
    m_msgsScroll->setWidgetResizable(true);
    m_msgsScroll->setFrameShape(QFrame::NoFrame);
    m_msgsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_msgsContainer = new QWidget(m_msgsScroll);
    m_msgsContainer->setAttribute(Qt::WA_StyledBackground, false);
    m_msgsLayout = new QVBoxLayout(m_msgsContainer);
    m_msgsLayout->setContentsMargins(20, 14, 20, 16);
    m_msgsLayout->setSpacing(12);
    m_msgsLayout->addStretch(1);
    m_msgsScroll->setWidget(m_msgsContainer);
    chatMainLayout->addWidget(m_msgsScroll, 1);

    // 输入区：引用条 + 悬浮输入坞（表情 / 图片 / 文件 + 输入框 + 发送）
    auto *composerWrap = new QWidget(m_chatMain);
    auto *composerWrapLayout = new QVBoxLayout(composerWrap);
    composerWrapLayout->setContentsMargins(0, 6, 0, 0);
    composerWrapLayout->setSpacing(6);

    // 引用条：回复某条消息时出现在输入坞上方
    m_replyBar = new QFrame(composerWrap);
    m_replyBar->setObjectName(QStringLiteral("forumChatReplyBar"));
    auto *replyLayout = new QHBoxLayout(m_replyBar);
    replyLayout->setContentsMargins(12, 6, 8, 6);
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
    composerWrapLayout->addWidget(m_replyBar);

    m_chatInputBox = new QFrame(composerWrap);
    m_chatInputBox->setObjectName(QStringLiteral("forumChatInputBox"));
    auto *composer = m_chatInputBox;
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(10, 6, 8, 6);
    composerLayout->setSpacing(6);

    m_emojiButton = new QPushButton(composer);
    m_emojiButton->setIcon(ResourceManager::instance()->loadThemedIcon(
        QStringLiteral(":/Images/Icons/smile.svg")));
    m_emojiButton->setIconSize(QSize(16, 16));
    m_emojiButton->setObjectName(QStringLiteral("forumChatToolButton"));
    m_emojiButton->setFixedSize(32, 32);
    m_emojiButton->setCursor(Qt::PointingHandCursor);
    m_emojiButton->setToolTip(tr("表情"));
    connect(m_emojiButton, &QPushButton::clicked, this, &ForumFriendsPage::toggleEmojiPopup);
    composerLayout->addWidget(m_emojiButton);
    composerLayout->setAlignment(m_emojiButton, Qt::AlignBottom);

    m_imageButton = new QPushButton(composer);
    m_imageButton->setIcon(ResourceManager::instance()->loadThemedIcon(
        QStringLiteral(":/Images/Icons/screenshot.svg")));
    m_imageButton->setIconSize(QSize(16, 16));
    m_imageButton->setObjectName(QStringLiteral("forumChatToolButton"));
    m_imageButton->setFixedSize(32, 32);
    m_imageButton->setCursor(Qt::PointingHandCursor);
    m_imageButton->setToolTip(tr("发送图片（也可以直接把截图粘进输入框）"));
    connect(m_imageButton, &QPushButton::clicked, this, &ForumFriendsPage::pickAndSendImage);
    composerLayout->addWidget(m_imageButton);
    composerLayout->setAlignment(m_imageButton, Qt::AlignBottom);

    m_fileButton = new QPushButton(composer);
    m_fileButton->setIcon(ResourceManager::instance()->loadThemedIcon(
        QStringLiteral(":/Images/Icons/paperclip.svg")));
    m_fileButton->setIconSize(QSize(16, 16));
    m_fileButton->setObjectName(QStringLiteral("forumChatToolButton"));
    m_fileButton->setFixedSize(32, 32);
    m_fileButton->setCursor(Qt::PointingHandCursor);
    m_fileButton->setToolTip(tr("发送文件（单个不超过 5MB）"));
    connect(m_fileButton, &QPushButton::clicked, this, &ForumFriendsPage::pickAndSendFile);
    composerLayout->addWidget(m_fileButton);
    composerLayout->setAlignment(m_fileButton, Qt::AlignBottom);

    auto *inputColumn = new QVBoxLayout();
    inputColumn->setSpacing(2);
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
    m_chatInput->setFrameShape(QFrame::NoFrame);
    m_chatInput->installEventFilter(this);
    inputColumn->addWidget(m_chatInput);
    composerLayout->addLayout(inputColumn, 1);

    m_sendButton = new QPushButton(tr("发送"), composer);
    m_sendButton->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setFixedHeight(38);
    m_sendButton->setFixedWidth(76);
    m_sendButton->setToolTip(tr("Enter 发送，Shift+Enter 换行"));
    connect(m_sendButton, &QPushButton::clicked, this, &ForumFriendsPage::sendCurrentMessage);
    composerLayout->addWidget(m_sendButton);
    composerLayout->setAlignment(m_sendButton, Qt::AlignBottom);
    composerWrapLayout->addWidget(m_chatInputBox);
    chatMainLayout->addWidget(composerWrap);

    // 输入框随内容长高（1～4 行），接近字数上限时显示计数
    connect(m_chatInput, &QTextEdit::textChanged, this, [this]() {
        refreshInputSize();
        const int n = m_chatInput->toPlainText().length();
        m_charCounter->setVisible(n >= 800);
        if (n >= 800)
            m_charCounter->setText(tr("%1 / 1000").arg(n));
        // 停下来 0.6 秒就把没发出去的文字存成草稿（切换会话再回来还在）
        if (!m_draftSuppressed)
            scheduleDraftSave();
    });

    chatLayout->addWidget(m_chatMain, 1);
    columns->addWidget(m_chatCard, 1);

    connect(m_chatProfileButton, &QPushButton::clicked, this,
            [this]() { emit userOpenRequested(m_peer.id); });
    connect(m_chatMembersButton, &QPushButton::clicked, this, &ForumFriendsPage::showGroupMembers);
    connect(m_chatInviteButton, &QPushButton::clicked, this, &ForumFriendsPage::inviteToCurrentGroup);
    connect(m_chatGroupButton, &QPushButton::clicked, this, &ForumFriendsPage::startGroupFromPeer);
    connect(m_chatLeaveButton, &QPushButton::clicked, this, &ForumFriendsPage::leaveCurrentGroup);
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
        if (AppMessageBox::question(this, tr("删除好友"),
                                  tr("确定删除好友「%1」吗？聊天记录将不再可见。").arg(peerName)) !=
            AppMessageBox::Yes)
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
        ChatUiStore::instance()->setAccount(0);
        ChatBookmarkStore::instance()->setAccount(0);
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
            m_sideGroupTitle->setText(tr("会话"));
        setStatus(QString());
        return;
    }
    // 置顶 / 免打扰 / 草稿是按账号分开存的：换了账号就换一份
    ChatUiStore::instance()->setAccount(ForumAccountService::instance()->user().id);
    ChatBookmarkStore::instance()->setAccount(ForumAccountService::instance()->user().id);
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

// ────────────────────────────────────────────────────────────── 群聊

void ForumFriendsPage::openCreateGroupDialog()
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        NotificationManager::showInfo(this, tr("发起群聊需要先登录论坛账号"));
        return;
    }
    CreateGroupDialog::create(this, [this](const ForumGroupInfo &group) {
        // 建好了直接开聊；列表里补上这个新群（顺手刷新好友数据）
        openGroupConversation(group);
        reload();
    });
}

void ForumFriendsPage::startGroupFromPeer()
{
    if (!m_peer.isValid())
        return;
    // 私聊一键拉群：建群弹窗里预先勾中对方
    CreateGroupDialog::create(
        this,
        [this](const ForumGroupInfo &group) {
            openGroupConversation(group);
            reload();
        },
        m_peer.id);
}

void ForumFriendsPage::inviteToCurrentGroup()
{
    if (!m_group.isValid() || m_group.official)
        return;
    QVector<int> existingIds;
    for (const ForumGroupMember &member : m_groupMembers)
        existingIds.append(member.user.id);
    CreateGroupDialog::invite(this, m_group, existingIds, [this]() {
        // 邀请成功：重开会话刷新成员与人数，顺手刷新会话列表
        if (m_group.isValid())
            openGroupConversation(m_group);
        reload();
    });
}

void ForumFriendsPage::showGroupMembers()
{
    if (!m_group.isValid() || m_groupMembers.isEmpty())
        return;
    const int myId = ForumAccountService::instance()->user().id;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("群成员 · %1").arg(m_group.name));
    dialog.setModal(true);
    dialog.setMinimumWidth(300);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    // 标题用群信息里的真实成员数（官方群成员列表是截断返回的，按列表长度数会少）
    const int memberCount = m_group.memberCount > 0 ? m_group.memberCount : m_groupMembers.size();
    auto *title = new QLabel(tr("%1 名成员").arg(memberCount), &dialog);
    title->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_SECONDARY@; font-size: 12px;")));
    layout->addWidget(title);

    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    auto *container = new QWidget(scroll);
    auto *listLayout = new QVBoxLayout(container);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(4);

    for (const ForumGroupMember &member : m_groupMembers)
    {
        auto *row = new QWidget(container);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(4, 2, 4, 2);
        rowLayout->setSpacing(10);
        rowLayout->addWidget(ForumKit::makeAvatar(row, member.user.avatar,
                                                  member.user.displayName, 30));
        auto *name = new ForumKit::ElidedLabel(row);
        name->setFullText(member.user.displayName);
        name->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_PRIMARY@; font-size: 13px;")));
        rowLayout->addWidget(name, 1);
        if (member.owner)
        {
            auto *owner = new QLabel(tr("群主"), row);
            owner->setStyleSheet(StyleKit::resolve(
                QStringLiteral("color: @PRIMARY@; font-size: 11px; background: @BG_MUTED@;"
                               "border-radius: 6px; padding: 1px 6px;")));
            rowLayout->addWidget(owner);
        }
        if (member.user.id == myId)
        {
            auto *me = new QLabel(tr("我"), row);
            me->setStyleSheet(StyleKit::resolve(
                QStringLiteral("color: @TEXT_TERTIARY@; font-size: 11px; background: @BG_MUTED@;"
                               "border-radius: 6px; padding: 1px 6px;")));
            rowLayout->addWidget(me);
        }
        listLayout->addWidget(row);
    }
    listLayout->addStretch(1);
    scroll->setWidget(container);
    layout->addWidget(scroll, 1);

    // 成员列表被服务端截断时（官方群人太多），底部说明一下
    if (m_groupMembers.size() < memberCount)
    {
        auto *note = new QLabel(tr("成员太多，这里只显示最早加入的一部分"), &dialog);
        note->setAlignment(Qt::AlignCenter);
        note->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: 11px;")));
        layout->addWidget(note);
    }

    // 非官方群：成员弹窗里顺手给一个「邀请好友」入口
    if (!m_group.official)
    {
        auto *inviteBtn = new QPushButton(tr("邀请好友进群"), &dialog);
        inviteBtn->setObjectName(QStringLiteral("forumGhostButton"));
        inviteBtn->setCursor(Qt::PointingHandCursor);
        inviteBtn->setFixedHeight(28);
        connect(inviteBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
            dialog.accept();
            inviteToCurrentGroup();
        });
        layout->addWidget(inviteBtn);
    }

    dialog.exec();
}

void ForumFriendsPage::leaveCurrentGroup()
{
    if (!m_group.isValid())
        return;
    // 官方全用户群是默认群聊，不能退出（按钮本来就藏了，这里再兜一层）
    if (m_group.official)
        return;
    const int groupId = m_group.id;
    const QString groupName = m_group.name;
    const bool isOwner = m_group.ownerId == ForumAccountService::instance()->user().id;
    const QString confirmText =
        isOwner ? tr("你是群主，退出将解散群聊「%1」，所有群消息都会删除。确定退出吗？").arg(groupName)
                : tr("确定退出群聊「%1」吗？退出后要再聊就得让群主重新邀请。").arg(groupName);
    if (AppMessageBox::question(this, tr("退出群聊"), confirmText) != AppMessageBox::Yes)
        return;
    ForumChatService::instance()->leaveGroup(
        groupId,
        [this, groupId](bool dissolved) {
            NotificationManager::showSuccess(this, dissolved ? tr("群聊已解散") : tr("已退出群聊"));
            if (m_group.id == groupId)
                closeConversation();
            reload();
        },
        [this](const QString &message, int) {
            NotificationManager::showError(this, message);
        });
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
    // 切走页面：把还在输入框里的文字存成草稿
    saveActiveDraft();
    m_pollTimer.stop();
}

bool ForumFriendsPage::eventFilter(QObject *watched, QEvent *event)
{
    // 引用条：点一下滚到被引用的原消息
    if (watched && event->type() == QEvent::MouseButtonRelease)
    {
        const int replyTargetId = watched->property("replyTargetId").toInt();
        if (replyTargetId > 0)
        {
            scrollToMessage(replyTargetId);
            return true;
        }
    }
    // 输入坞聚焦时亮起主色描边（与 AI 聊天页的输入框一致）
    if (watched == m_chatInput &&
        (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
    {
        if (m_chatInputBox)
        {
            m_chatInputBox->setProperty("focused", event->type() == QEvent::FocusIn);
            m_chatInputBox->style()->unpolish(m_chatInputBox);
            m_chatInputBox->style()->polish(m_chatInputBox);
        }
    }
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
        if (keyEvent->matches(QKeySequence::Paste) && (m_peer.id > 0 || m_group.isValid()))
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
    const int groupId = m_group.id;
    const int groupAfterId = m_groupLastMessageId;
    ForumChatService::instance()->poll(
        peerId, groupId, afterId, groupAfterId,
        [this](const ForumPollResult &result) {
            m_pollBusy = false;
            m_snapshot.friends = result.friends;
            m_snapshot.groups = result.groups;
            m_snapshot.totalUnread = result.totalUnread;
            if (m_tab == FriendsTab)
                renderFriendRows();
            if (m_peer.id > 0 && !result.messages.isEmpty())
                appendMessages(result.messages);
            if (m_group.isValid() && !result.groupMessages.isEmpty())
                appendMessages(result.groupMessages);
        },
        [this](const QString &, int) { m_pollBusy = false; });
}

// ────────────────────────────────────────────────────────────── 左栏

void ForumFriendsPage::setTab(Tab tab)
{
    if (m_tab == tab)
        return;
    m_tab = tab;
    // 会话搜索只对「好友」页签有意义
    if (m_convSearchRow)
        m_convSearchRow->setVisible(tab == FriendsTab);
    if (tab != FriendsTab)
        reload();
    else
        renderSide();
}

void ForumFriendsPage::updateConversationFilter()
{
    m_convFilterTimer.start();
}

QString ForumFriendsPage::activeConversationKey() const
{
    if (m_group.isValid())
        return ChatUiStore::conversationKey(true, m_group.id);
    if (m_peer.id > 0)
        return ChatUiStore::conversationKey(false, m_peer.id);
    return QString();
}

void ForumFriendsPage::saveActiveDraft()
{
    m_draftTimer.stop();
    const QString key = activeConversationKey();
    if (key.isEmpty() || !m_chatInput)
        return;
    ChatUiStore::instance()->setDraft(key, m_chatInput->toPlainText());
}

void ForumFriendsPage::scheduleDraftSave()
{
    if (m_activeKey.isEmpty())
        return;
    m_draftTimer.start();
}

void ForumFriendsPage::restoreDraft(const QString &key)
{
    if (key.isEmpty() || !m_chatInput)
        return;
    const QString draft = ChatUiStore::instance()->draft(key);
    if (draft.isEmpty())
        return;
    m_chatInput->setPlainText(draft);
    m_chatInput->moveCursor(QTextCursor::End);
    refreshInputSize();
}

void ForumFriendsPage::showConversationMenu(bool group, int id, const QPoint &globalPos)
{
    if (id <= 0)
        return;
    const QString key = ChatUiStore::conversationKey(group, id);
    ChatUiStore *store = ChatUiStore::instance();
    QMenu menu(this);
    QAction *pinAction = menu.addAction(store->isPinned(key) ? tr("取消置顶") : tr("置顶会话"));
    QAction *muteAction = menu.addAction(store->isMuted(key) ? tr("取消免打扰") : tr("消息免打扰"));
    QAction *clearDraftAction = nullptr;
    if (!store->draft(key).isEmpty())
    {
        menu.addSeparator();
        clearDraftAction = menu.addAction(tr("删除草稿"));
    }
    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    if (chosen == pinAction)
        store->setPinned(key, !store->isPinned(key));
    else if (chosen == muteAction)
        store->setMuted(key, !store->isMuted(key));
    else if (chosen == clearDraftAction)
    {
        store->setDraft(key, QString());
        // 正好开着这个会话：输入框里没发出去的文字也一起清掉
        if (key == activeConversationKey() && m_chatInput)
            m_chatInput->clear();
    }
    renderFriendRows();
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

    ChatUiStore *store = ChatUiStore::instance();
    const QString filter = m_convSearchEdit ? m_convSearchEdit->text().trimmed() : QString();
    const bool filtering = !filter.isEmpty();
    int rendered = 0;   ///< 过滤后真正显示出来的会话数（搜索无结果时给提示）

    // 会话行 = 好友 + 群聊混排；官方客服仍是默认好友，固定排最前面
    const int conversationCount = m_snapshot.friends.size() + m_snapshot.groups.size();
    if (m_sideGroupTitle)
        m_sideGroupTitle->setText(conversationCount > 0 ? tr("会话 · %1").arg(conversationCount)
                                                        : tr("会话"));

    if (conversationCount == 0)
    {
        auto *empty = ForumKit::makeMuted(this, tr("还没有好友，去「添加好友」找一个吧"));
        empty->setAlignment(Qt::AlignCenter);
        m_sideListLayout->addSpacing(20);
        m_sideListLayout->addWidget(empty);
        m_sideListLayout->addSpacing(20);
    }

    // 一行会话：isGroup 区分好友 / 群聊，lastAt 是最后一条消息时间（0 = 没聊过），
    // pinned 是本机置顶（有置顶时排在同组最前面）
    struct SideEntry
    {
        bool isGroup = false;
        int id = 0;
        qint64 lastAt = 0;
        bool pinned = false;
    };

    auto renderFriendRow = [this, store, filter, filtering, &rendered](const ForumFriendInfo &friendInfo) {
        const QString key = ChatUiStore::conversationKey(false, friendInfo.id);
        const bool pinned = store->isPinned(key);
        const bool muted = store->isMuted(key);
        const QString draft = store->draft(key);

        // 预览：有未发送草稿就先显示草稿，否则按最后一条消息的类型显示
        QString preview = tr("还没有聊过天");
        if (!draft.isEmpty())
            preview = draft.simplified();
        else if (friendInfo.lastMessageKind == QStringLiteral("image"))
            preview = tr("[图片]");
        else if (friendInfo.lastMessageKind == QStringLiteral("file"))
            preview = tr("[文件]");
        else if (friendInfo.lastMessageKind == QStringLiteral("withdrawn"))
            preview = tr("撤回了一条消息");
        else if (!friendInfo.lastMessageBody.isEmpty())
            preview = friendInfo.lastMessageBody;
        if (draft.isEmpty() && !friendInfo.lastMessageBody.isEmpty() && friendInfo.lastMessageMine &&
            friendInfo.lastMessageKind != QStringLiteral("withdrawn"))
            preview = tr("我：%1").arg(preview);
        const QString previewText = draft.isEmpty() ? preview : tr("[草稿] %1").arg(preview);

        // 会话搜索：昵称 / 账号名 / 预览内容任一命中才显示
        if (filtering)
        {
            const bool hit = friendInfo.displayName.contains(filter, Qt::CaseInsensitive) ||
                             friendInfo.username.contains(filter, Qt::CaseInsensitive) ||
                             previewText.contains(filter, Qt::CaseInsensitive);
            if (!hit)
                return;
        }

        auto *row = new ForumKit::ClickableCard(this);
        row->setObjectName(QStringLiteral("forumSideRow"));
        // 当前正在聊的这一条按「选中」样式高亮，和子导航选中项一致
        row->setProperty("selected", m_peer.isValid() && m_peer.id == friendInfo.id);
        const int peerId = friendInfo.id;
        row->onClick = [this, peerId]() {
            for (const ForumFriendInfo &item : m_snapshot.friends)
            {
                if (item.id == peerId)
                {
                    openConversation(item);
                    return;
                }
            }
        };
        // 右键：置顶 / 消息免打扰
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(row, &QWidget::customContextMenuRequested, this,
                [this, peerId, row](const QPoint &pos) {
                    showConversationMenu(false, peerId, row->mapToGlobal(pos));
                });

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 10, 12, 10);
        rowLayout->setSpacing(10);

        // 官方客服用方块盒子图标当头像，普通好友照旧
        rowLayout->addWidget(friendInfo.official
                                 ? ForumKit::makeOfficialAvatar(row, 36)
                                 : ForumKit::makeAvatar(row, friendInfo.avatar,
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
        if (pinned)
        {
            auto *pin = new QLabel(row);
            pin->setObjectName(QStringLiteral("forumSideStateIcon"));
            pin->setPixmap(ResourceManager::instance()
                               ->loadThemedIcon(QStringLiteral(":/Images/Icons/pin_filled.svg"))
                               .pixmap(12, 12));
            pin->setToolTip(tr("已置顶"));
            nameRow->addWidget(pin);
        }
        if (muted)
        {
            auto *mute = new QLabel(row);
            mute->setObjectName(QStringLiteral("forumSideStateIcon"));
            mute->setPixmap(ResourceManager::instance()
                                ->loadThemedIcon(QStringLiteral(":/Images/Icons/bell_off.svg"))
                                .pixmap(12, 12));
            mute->setToolTip(tr("消息免打扰"));
            nameRow->addWidget(mute);
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
            if (muted)
            {
                // 免打扰只留一个小圆点：知道有新消息，但不抢注意力
                auto *dot = new QLabel(row);
                dot->setObjectName(QStringLiteral("forumUnreadDot"));
                dot->setFixedSize(8, 8);
                dot->setToolTip(tr("%1 条未读消息").arg(friendInfo.unread));
                nameRow->addWidget(dot);
            }
            else
            {
                auto *badge = new QLabel(row);
                badge->setObjectName(QStringLiteral("forumUnreadBadge"));
                badge->setText(friendInfo.unread > 99 ? QStringLiteral("99+")
                                                      : QString::number(friendInfo.unread));
                nameRow->addWidget(badge);
            }
        }
        textColumn->addLayout(nameRow);

        auto *previewLabel = new ForumKit::ElidedLabel(row);
        previewLabel->setObjectName(draft.isEmpty() ? QStringLiteral("forumMuted")
                                                    : QStringLiteral("forumDraftPreview"));
        previewLabel->setFullText(previewText);
        textColumn->addWidget(previewLabel);

        rowLayout->addLayout(textColumn, 1);

        auto *time = new QLabel(messageTime(friendInfo.lastMessageAt), row);
        time->setObjectName(QStringLiteral("forumMuted"));
        time->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: 11px;")));
        rowLayout->addWidget(time, 0, Qt::AlignTop);

        m_sideListLayout->addWidget(row);
        ++rendered;
    };

    auto renderGroupRow = [this, store, filter, filtering, &rendered](const ForumGroupInfo &group) {
        const QString key = ChatUiStore::conversationKey(true, group.id);
        const bool pinned = store->isPinned(key);
        const bool muted = store->isMuted(key);
        const QString draft = store->draft(key);

        // 群预览带上发言人：谁说了什么一眼能分清（系统消息本身就是完整句子，不加前缀）；
        // 有草稿时优先显示草稿
        QString preview = tr("还没有聊过天");
        if (!draft.isEmpty())
            preview = draft.simplified();
        else if (group.lastMessageKind == QStringLiteral("system"))
            preview = group.lastMessageBody;
        else if (group.lastMessageKind == QStringLiteral("image"))
            preview = tr("[图片]");
        else if (group.lastMessageKind == QStringLiteral("file"))
            preview = tr("[文件]");
        else if (group.lastMessageKind == QStringLiteral("withdrawn"))
            preview = tr("撤回了一条消息");
        else if (!group.lastMessageBody.isEmpty())
            preview = group.lastMessageBody;
        if (draft.isEmpty() && !group.lastMessageBody.isEmpty() &&
            group.lastMessageKind != QStringLiteral("withdrawn") &&
            group.lastMessageKind != QStringLiteral("system"))
        {
            if (group.lastMessageMine)
                preview = tr("我：%1").arg(preview);
            else if (!group.lastMessageSender.isEmpty())
                preview = tr("%1：%2").arg(group.lastMessageSender, preview);
        }
        const QString previewText = draft.isEmpty() ? preview : tr("[草稿] %1").arg(preview);

        // 会话搜索：群名 / 预览内容任一命中才显示
        if (filtering && !group.name.contains(filter, Qt::CaseInsensitive) &&
            !previewText.contains(filter, Qt::CaseInsensitive))
            return;

        auto *row = new ForumKit::ClickableCard(this);
        row->setObjectName(QStringLiteral("forumSideRow"));
        row->setProperty("selected", m_group.isValid() && m_group.id == group.id);
        const int groupId = group.id;
        row->onClick = [this, groupId]() {
            for (const ForumGroupInfo &item : m_snapshot.groups)
            {
                if (item.id == groupId)
                {
                    openGroupConversation(item);
                    return;
                }
            }
        };
        // 右键：置顶 / 消息免打扰
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(row, &QWidget::customContextMenuRequested, this,
                [this, groupId, row](const QPoint &pos) {
                    showConversationMenu(true, groupId, row->mapToGlobal(pos));
                });

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 10, 12, 10);
        rowLayout->setSpacing(10);

        // 官方全用户群用方块盒子图标当头像；普通群有图标就显示图标，没有就按群名首字
        rowLayout->addWidget(group.official
                                 ? ForumKit::makeOfficialAvatar(row, 36)
                                 : ForumKit::makeGroupAvatar(row, group.avatar, group.name, 36));

        auto *textColumn = new QVBoxLayout();
        textColumn->setSpacing(3);

        auto *nameRow = new QHBoxLayout();
        nameRow->setSpacing(6);
        auto *name = new ForumKit::ElidedLabel(row);
        name->setObjectName(QStringLiteral("forumRowTitle"));
        name->setFullText(group.name);
        nameRow->addWidget(name, 1);
        if (group.official)
        {
            auto *official = new QLabel(tr("官方"), row);
            official->setObjectName(QStringLiteral("forumOfficialBadge"));
            official->setToolTip(tr("方块盒子全用户讨论群，所有玩家默认都在这个群里"));
            nameRow->addWidget(official);
        }
        if (pinned)
        {
            auto *pin = new QLabel(row);
            pin->setObjectName(QStringLiteral("forumSideStateIcon"));
            pin->setPixmap(ResourceManager::instance()
                               ->loadThemedIcon(QStringLiteral(":/Images/Icons/pin_filled.svg"))
                               .pixmap(12, 12));
            pin->setToolTip(tr("已置顶"));
            nameRow->addWidget(pin);
        }
        if (muted)
        {
            auto *mute = new QLabel(row);
            mute->setObjectName(QStringLiteral("forumSideStateIcon"));
            mute->setPixmap(ResourceManager::instance()
                                ->loadThemedIcon(QStringLiteral(":/Images/Icons/bell_off.svg"))
                                .pixmap(12, 12));
            mute->setToolTip(tr("消息免打扰"));
            nameRow->addWidget(mute);
        }
        if (group.unread > 0)
        {
            if (muted)
            {
                auto *dot = new QLabel(row);
                dot->setObjectName(QStringLiteral("forumUnreadDot"));
                dot->setFixedSize(8, 8);
                dot->setToolTip(tr("%1 条未读消息").arg(group.unread));
                nameRow->addWidget(dot);
            }
            else
            {
                auto *badge = new QLabel(row);
                badge->setObjectName(QStringLiteral("forumUnreadBadge"));
                badge->setText(group.unread > 99 ? QStringLiteral("99+")
                                                 : QString::number(group.unread));
                nameRow->addWidget(badge);
            }
        }
        textColumn->addLayout(nameRow);

        auto *previewLabel = new ForumKit::ElidedLabel(row);
        previewLabel->setObjectName(draft.isEmpty() ? QStringLiteral("forumMuted")
                                                    : QStringLiteral("forumDraftPreview"));
        previewLabel->setFullText(previewText);
        textColumn->addWidget(previewLabel);

        rowLayout->addLayout(textColumn, 1);

        auto *time = new QLabel(messageTime(group.lastMessageAt), row);
        time->setObjectName(QStringLiteral("forumMuted"));
        time->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: 11px;")));
        rowLayout->addWidget(time, 0, Qt::AlignTop);

        m_sideListLayout->addWidget(row);
        ++rendered;
    };

    // 官方客服置顶，其余好友与群按最后一条消息时间混排
    for (const ForumFriendInfo &friendInfo : m_snapshot.friends)
    {
        if (friendInfo.official)
            renderFriendRow(friendInfo);
    }
    QVector<SideEntry> entries;
    for (const ForumFriendInfo &friendInfo : m_snapshot.friends)
    {
        if (friendInfo.official)
            continue;
        const QString key = ChatUiStore::conversationKey(false, friendInfo.id);
        entries.append({false, friendInfo.id, friendInfo.lastMessageAt, store->isPinned(key)});
    }
    for (const ForumGroupInfo &group : m_snapshot.groups)
    {
        const QString key = ChatUiStore::conversationKey(true, group.id);
        entries.append({true, group.id, group.lastMessageAt, store->isPinned(key)});
    }
    // 置顶的排最前，其余按最后一条消息时间倒序
    std::sort(entries.begin(), entries.end(), [](const SideEntry &a, const SideEntry &b) {
        if (a.pinned != b.pinned)
            return a.pinned;
        if (a.lastAt != b.lastAt)
            return a.lastAt > b.lastAt;
        return a.id < b.id;
    });
    for (const SideEntry &entry : entries)
    {
        if (entry.isGroup)
        {
            for (const ForumGroupInfo &group : m_snapshot.groups)
            {
                if (group.id == entry.id)
                {
                    renderGroupRow(group);
                    break;
                }
            }
        }
        else
        {
            for (const ForumFriendInfo &friendInfo : m_snapshot.friends)
            {
                if (friendInfo.id == entry.id)
                {
                    renderFriendRow(friendInfo);
                    break;
                }
            }
        }
    }
    // 搜索词一个没命中：给一句明确提示，而不是空着一片
    if (rendered == 0 && filtering)
    {
        auto *empty = ForumKit::makeMuted(this, tr("没有找到包含「%1」的会话").arg(filter));
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        m_sideListLayout->addSpacing(20);
        m_sideListLayout->addWidget(empty);
        m_sideListLayout->addSpacing(20);
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
    // 离开上一个会话前，先把输入框里没发出去的文字存成草稿
    saveActiveDraft();
    // 打开会话时顺手记下这位好友的未读数，用来在消息里标出「这才是新消息」
    int unread = 0;
    for (const ForumFriendInfo &item : m_snapshot.friends)
    {
        if (item.id == peer.id)
        {
            unread = item.unread;
            break;
        }
    }

    m_peer = peer;
    m_group = {};
    m_groupMembers.clear();
    m_messages.clear();
    m_lastMessageId = 0;
    m_groupLastMessageId = 0;
    m_unreadAnchorId = 0;
    m_hasMore = false;
    m_groupHasMore = false;
    m_fileDownloads.clear();
    m_chatEmpty->hide();
    m_chatMain->show();
    m_moreButton->hide();
    // 换上这个会话的草稿（清空/恢复都别记成草稿）
    m_draftSuppressed = true;
    m_chatInput->clear();
    m_activeKey = ChatUiStore::conversationKey(false, peer.id);
    restoreDraft(m_activeKey);
    m_draftSuppressed = false;
    m_chatName->setText(peer.isValid() ? peer.displayName : tr("…"));
    m_chatStatus->setText(peer.isValid() ? onlineText(peer) : QString());
    m_chatStatus->setStyleSheet(
        StyleKit::resolve(QStringLiteral("font-size: 11px; color: @TEXT_TERTIARY@;")));
    // 私聊头部的按钮组合：主页 + 拉群 + 删除好友（官方客服没有删除）
    m_chatProfileButton->setVisible(true);
    m_chatGroupButton->setVisible(true);
    m_chatRemoveButton->setVisible(!peer.official);
    m_chatMembersButton->setVisible(false);
    m_chatInviteButton->setVisible(false);
    m_chatLeaveButton->setVisible(false);
    renderChatAvatar();
    ForumKit::clearLayout(m_msgsLayout);
    m_msgsLayout->addStretch(1);
    auto *loading = ForumKit::makeMuted(this, tr("正在加载消息…"));
    m_msgsLayout->insertWidget(0, loading);
    renderFriendRows(); // 高亮当前会话行

    ForumChatService::instance()->fetchMessages(
        peer.id, 0,
        [this, unread](const ForumChatHistory &history) {
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
            // 未读数是从列表上抄下来的：从末尾往前数这么多条，第一条就是「新消息」起点
            m_unreadAnchorId = 0;
            if (unread > 0 && !m_messages.isEmpty())
            {
                const int start = qBound(0, m_messages.size() - unread, m_messages.size() - 1);
                m_unreadAnchorId = m_messages.at(start).id;
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

void ForumFriendsPage::openGroupConversation(const ForumGroupInfo &group)
{
    // 离开上一个会话前，先把输入框里没发出去的文字存成草稿
    saveActiveDraft();
    const int unread = group.unread;

    m_peer = {};
    m_group = group;
    m_groupMembers.clear();
    m_messages.clear();
    m_lastMessageId = 0;
    m_groupLastMessageId = 0;
    m_unreadAnchorId = 0;
    m_hasMore = false;
    m_groupHasMore = false;
    m_fileDownloads.clear();
    m_chatEmpty->hide();
    m_chatMain->show();
    m_moreButton->hide();
    // 换上这个群的草稿（清空/恢复都别记成草稿）
    m_draftSuppressed = true;
    m_chatInput->clear();
    m_activeKey = ChatUiStore::conversationKey(true, group.id);
    restoreDraft(m_activeKey);
    m_draftSuppressed = false;
    m_chatName->setText(group.name);
    m_chatStatus->setText(tr("%1 名成员").arg(group.memberCount));
    m_chatStatus->setStyleSheet(
        StyleKit::resolve(QStringLiteral("font-size: 11px; color: @TEXT_TERTIARY@;")));
    // 群聊头部的按钮组合：成员 + 邀请 + 退出群聊
    // （官方全用户群是默认群聊：人已经在群里了，不显示退出和邀请）
    m_chatProfileButton->setVisible(false);
    m_chatGroupButton->setVisible(false);
    m_chatRemoveButton->setVisible(false);
    m_chatMembersButton->setVisible(true);
    m_chatInviteButton->setVisible(!group.official);
    m_chatLeaveButton->setVisible(!group.official);
    renderChatAvatar();
    ForumKit::clearLayout(m_msgsLayout);
    m_msgsLayout->addStretch(1);
    auto *loading = ForumKit::makeMuted(this, tr("正在加载消息…"));
    m_msgsLayout->insertWidget(0, loading);
    renderFriendRows(); // 高亮当前会话行

    ForumChatService::instance()->fetchGroupMessages(
        group.id, 0,
        [this, unread](const ForumGroupDetail &detail) {
            // 请求期间切换了会话：丢弃过期结果
            if (!m_group.isValid() || detail.group.id != m_group.id)
                return;
            m_group = detail.group;
            m_groupMembers = detail.members;
            m_chatStatus->setText(tr("%1 名成员").arg(m_group.memberCount));
            m_messages.clear();
            m_groupLastMessageId = 0;
            for (const ForumChatMessage &message : detail.messages)
            {
                m_messages.append(message);
                m_groupLastMessageId = qMax(m_groupLastMessageId, message.id);
            }
            m_unreadAnchorId = 0;
            if (unread > 0 && !m_messages.isEmpty())
            {
                const int start = qBound(0, m_messages.size() - unread, m_messages.size() - 1);
                m_unreadAnchorId = m_messages.at(start).id;
            }
            m_groupHasMore = detail.hasMore;
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
    // 在线时在头像右下角叠一颗绿点；群聊没有在线概念，用群名首字占位头像
    ForumKit::clearLayout(m_chatAvatarSlot->layout());

    QWidget *wrap = new QWidget(m_chatAvatarSlot);
    wrap->setFixedSize(40, 40);
    if (m_group.isValid())
    {
        QLabel *avatar = m_group.official ? ForumKit::makeOfficialAvatar(wrap, 40)
                                          : ForumKit::makeGroupAvatar(wrap, m_group.avatar,
                                                                      m_group.name, 40);
        avatar->setGeometry(0, 0, 40, 40);
        m_chatAvatarSlot->layout()->addWidget(wrap);
        return;
    }
    QLabel *avatar = m_peer.official ? ForumKit::makeOfficialAvatar(wrap, 40)
                                     : ForumKit::makeAvatar(wrap, m_peer.avatar,
                                                            m_peer.displayName, 40);
    avatar->setGeometry(0, 0, 40, 40);
    // 官方客服与左侧会话行一致：不显示在线圆点
    if (m_peer.online && !m_peer.official)
    {
        QLabel *dot = new QLabel(wrap);
        dot->setObjectName(QStringLiteral("forumOnlineDot"));
        dot->setFixedSize(11, 11);
        dot->setGeometry(29, 29, 11, 11);
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
    // 消息 id → 行控件：引用条点一下要滚到原消息
    m_messageRows.clear();

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

        // 跨天插入日期分隔条（居中胶囊，不拉满整行）
        if (!prevDay.isValid() || moment.date() != prevDay)
        {
            m_msgsLayout->addSpacing(2);
            m_msgsLayout->addWidget(makeDateChip(message.createdAt), 0, Qt::AlignHCenter);
            m_msgsLayout->addSpacing(2);
            prevDay = moment.date();
        }

        // 打开会话时还有未读：在第一条未读前面标一道「以下是新消息」
        if (m_unreadAnchorId > 0 && message.id == m_unreadAnchorId)
        {
            auto *divider = new QLabel(tr("以下是新消息"), this);
            divider->setObjectName(QStringLiteral("forumChatUnreadDivider"));
            divider->setAlignment(Qt::AlignCenter);
            m_msgsLayout->addSpacing(2);
            m_msgsLayout->addWidget(divider, 0, Qt::AlignHCenter);
            m_msgsLayout->addSpacing(2);
        }

        // 撤回占位：一行居中的灰色小字（群里能看到是谁撤回的）
        if (message.isWithdrawn())
        {
            QString tombstoneText;
            if (message.mine)
                tombstoneText = tr("你撤回了一条消息");
            else if (m_group.isValid() && !message.senderName.isEmpty())
                tombstoneText = tr("%1 撤回了一条消息").arg(message.senderName);
            else
                tombstoneText = tr("对方撤回了一条消息");
            auto *tombstone = new QLabel(tombstoneText, this);
            tombstone->setObjectName(QStringLiteral("forumChatWithdraw"));
            tombstone->setAlignment(Qt::AlignCenter);
            m_msgsLayout->addWidget(tombstone, 0, Qt::AlignHCenter);
            continue;
        }

        // 系统消息（如「×× 邀请 ×× 加入了群聊」）：一行居中的灰色小字，不可点不可引用
        if (message.kind == QStringLiteral("system"))
        {
            auto *notice = new QLabel(message.body, this);
            notice->setObjectName(QStringLiteral("forumChatWithdraw"));
            notice->setAlignment(Qt::AlignCenter);
            notice->setWordWrap(true);
            notice->setMaximumWidth(420);
            m_msgsLayout->addWidget(notice, 0, Qt::AlignHCenter);
            continue;
        }

        // 一组 = 相邻且同一发送者、间隔 5 分钟内的消息；组首显示头像和时间
        const bool groupStart = i == 0 ||
                                m_messages.at(i - 1).mine != message.mine ||
                                m_messages.at(i - 1).isWithdrawn() ||
                                m_messages.at(i - 1).kind == QStringLiteral("system") ||
                                message.createdAt - m_messages.at(i - 1).createdAt > 5 * 60;

        auto *row = new QWidget(this);
        row->setObjectName(QStringLiteral("forumChatRow"));
        row->setAttribute(Qt::WA_StyledBackground, true);
        m_messageRows.insert(message.id, row); // 引用条跳转要用
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, groupStart ? 4 : 1, 0, 0);
        rowLayout->setSpacing(8);

        auto *bubbleColumn = new QVBoxLayout();
        bubbleColumn->setSpacing(3);

        // 群聊里别人的消息：气泡上方先来一行发送者昵称
        if (m_group.isValid() && !message.mine && groupStart && !message.isWithdrawn())
        {
            auto *sender = new QLabel(message.senderName, row);
            sender->setStyleSheet(StyleKit::resolve(
                QStringLiteral("color: @TEXT_SECONDARY@; font-size: 11px; background: transparent;")));
            bubbleColumn->addWidget(sender);
        }

        // 引用条（贴在气泡/图片上方，两侧一样式）
        if (message.replyTo > 0)
        {
            // 被引用的人：私聊就是对方；群里优先用消息里的发送者昵称
            QString replyName;
            if (message.replyMine)
                replyName = tr("我");
            else if (m_group.isValid())
                replyName = findMessageSenderName(message.replyTo);
            else
                replyName = m_peer.displayName;
            const QString quoteText = replyName.isEmpty() ? tr("回复：%1").arg(message.replyBody)
                                                          : tr("回复 %1：%2").arg(replyName, message.replyBody);
            auto *quote = new QLabel(quoteText, row);
            quote->setObjectName(QStringLiteral("forumChatQuote"));
            quote->setWordWrap(true);
            quote->setMaximumWidth(400);
            // 点引用条：滚到被引用的原消息（eventFilter 里处理）
            quote->setCursor(Qt::PointingHandCursor);
            quote->setToolTip(tr("点击查看原消息"));
            quote->setProperty("replyTargetId", message.replyTo);
            quote->installEventFilter(this);
            // 引用文字至少按自身自然宽度显示，不要被消息气泡压成两行
            const int quoteMaxWidth = qBound(160, m_msgsScroll->viewport()->width() - 110, 372);
            QFont quoteFont = quote->font();
            quoteFont.setPixelSize(12);
            quote->setMinimumWidth(qMin(quoteMaxWidth,
                                        naturalTextWidth(quoteFont, quoteText, quoteMaxWidth) + 16));
            bubbleColumn->addWidget(quote);
        }

        const int messageId = message.id;
        if (message.isImage())
        {
            // 图片消息：圆角卡片 + 异步加载的缩略图，点击看大图
            auto *imageFrame = new ForumKit::ClickableCard(row);
            imageFrame->setObjectName(message.mine ? QStringLiteral("forumChatImageFrameMine")
                                                   : QStringLiteral("forumChatImageFrame"));
            imageFrame->setProperty("chatImage", message.image);
            imageFrame->setMaximumWidth(400);
            auto *imageLayout = new QVBoxLayout(imageFrame);
            imageLayout->setContentsMargins(6, 6, 6, 8);
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
            // 点击看大图（群消息配了 CDN 时直接用服务端给的 CDN 地址）
            const QString previewPath = message.mediaUrl();
            imageFrame->onClick = [this, previewPath]() { showImagePreview(previewPath); };
            ForumImageStore::instance()->requestImage(
                message.mediaUrl(),
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
        else if (message.isFile())
        {
            // 文件消息：文件卡片（图标 + 文件名 + 大小）+ 提取下载按钮。
            // 群聊的下载地址配了 CDN 时走 CDN，私聊始终从论坛服务器直连。
            auto *fileCard = new QFrame(row);
            fileCard->setObjectName(message.mine ? QStringLiteral("forumChatBubbleMine")
                                                 : QStringLiteral("forumChatBubbleTheirs"));
            fileCard->setMaximumWidth(400);
            fileCard->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(fileCard, &QWidget::customContextMenuRequested, this,
                    [this, messageId, fileCard](const QPoint &pos) {
                        showMessageMenu(messageId, fileCard->mapToGlobal(pos));
                    });
            auto *fileLayout = new QHBoxLayout(fileCard);
            fileLayout->setContentsMargins(12, 10, 12, 10);
            fileLayout->setSpacing(10);

            auto *icon = new QLabel(QStringLiteral("📄"), fileCard);
            icon->setStyleSheet(StyleKit::resolve(QStringLiteral("font-size: 22px; background: transparent;")));
            fileLayout->addWidget(icon);

            auto *fileColumn = new QVBoxLayout();
            fileColumn->setSpacing(2);
            const QString fileNameText = message.fileName.isEmpty() ? tr("文件") : message.fileName;
            auto *nameLabel = new ForumKit::ElidedLabel(fileCard);
            nameLabel->setFullText(fileNameText);
            nameLabel->setStyleSheet(
                StyleKit::resolve(QStringLiteral("color: @TEXT_PRIMARY@; font-size: 13px;")));
            // 文件名先按自身宽度占位，只有真的放不下时才省略
            QFont nameFont = nameLabel->font();
            nameFont.setPixelSize(13);
            nameLabel->setMinimumWidth(qMin(210,
                                            QFontMetrics(nameFont).horizontalAdvance(fileNameText) + 10));
            fileColumn->addWidget(nameLabel);
            const QString sizeText =
                message.fileSize > 0 ? DownloadUtils::formatSize(message.fileSize) : tr("未知大小");
            auto *sizeLabel = new QLabel(sizeText, fileCard);
            sizeLabel->setStyleSheet(
                StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: 11px;")));
            fileColumn->addWidget(sizeLabel);
            fileLayout->addLayout(fileColumn, 1);

            auto *downloadButton = new QPushButton(fileCard);
            downloadButton->setObjectName(QStringLiteral("forumGhostButton"));
            downloadButton->setCursor(Qt::PointingHandCursor);
            downloadButton->setFixedHeight(26);
            const bool busy = m_fileDownloads.contains(messageId);
            downloadButton->setEnabled(!busy);
            downloadButton->setText(busy ? tr("下载中…") : tr("提取"));
            connect(downloadButton, &QPushButton::clicked, this,
                    [this, messageId, downloadButton] { downloadChatFile(messageId); });
            fileLayout->addWidget(downloadButton);
            bubbleColumn->addWidget(fileCard);

            if (!message.body.isEmpty())
            {
                auto *caption = new QLabel(message.body, row);
                caption->setWordWrap(true);
                caption->setMaximumWidth(400);
                caption->setStyleSheet(
                    StyleKit::resolve(QStringLiteral("color: @TEXT_PRIMARY@; font-size: 13px;")));
                caption->setTextInteractionFlags(Qt::TextSelectableByMouse);
                bubbleColumn->addWidget(caption);
            }
        }
        else
        {
            // 文字气泡：QFrame 承载（里面可以有引用），本身只当底色
            auto *bubble = new QFrame(row);
            bubble->setObjectName(message.mine ? QStringLiteral("forumChatBubbleMine")
                                               : QStringLiteral("forumChatBubbleTheirs"));
            bubble->setMaximumWidth(420);
            auto *bubbleLayout = new QVBoxLayout(bubble);
            bubbleLayout->setContentsMargins(12, 8, 12, 8);
            bubbleLayout->setSpacing(4);
            auto *body = new QLabel(message.body, bubble);
            body->setWordWrap(true);
            body->setTextInteractionFlags(Qt::TextSelectableByMouse);
            // 中文长句在布局里会被压成窄窄一条：按正文自然宽度给个下限，
            // 同时跟随窗口宽度收窄，避免窄屏下气泡顶出可视区
            const int viewportWidth = m_msgsScroll->viewport()->width();
            const int maxTextWidth = qBound(160, viewportWidth - 110, 372);
            QFont bodyFont = body->font();
            bodyFont.setPixelSize(13);
            const int naturalWidth = naturalTextWidth(bodyFont, message.body, maxTextWidth);
            // +12 给 QLabel 文本排版留一点余量，避免最后一个字被挤到下一行
            body->setMinimumWidth(qMin(maxTextWidth, naturalWidth + 12));
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
            // 我发的消息：对方打开过会话就是「已读」（群消息没有已读回执）
            QString meta = messageTime(message.createdAt);
            if (message.mine && message.read && !m_group.isValid())
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
            // 组首带头像，组内其余留同宽空位对齐（群里头像跟着发送者走）
            if (groupStart)
            {
                QLabel *avatar = nullptr;
                if (m_group.isValid())
                    avatar = ForumKit::makeAvatar(row, message.senderAvatar,
                                                  message.senderName, 26);
                // 私聊对象是官方客服时，气泡头像也用方块盒子图标
                else if (m_peer.official)
                    avatar = ForumKit::makeOfficialAvatar(row, 26);
                else
                    avatar = ForumKit::makeAvatar(row, m_peer.avatar,
                                                  m_peer.displayName, 26);
                rowLayout->addWidget(avatar);
                // 头像与气泡顶部对齐，不被下方的发送时间整体推低
                rowLayout->setAlignment(avatar, Qt::AlignTop);
            }
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
        // 私聊与群聊各有一个游标，轮询带上的是当前会话那一个
        if (m_group.isValid())
            m_groupLastMessageId = qMax(m_groupLastMessageId, message.id);
        else
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
    // 关掉会话说先把没发出去的文字留成草稿
    saveActiveDraft();
    m_draftSuppressed = true;
    m_chatInput->clear();
    m_draftSuppressed = false;
    m_activeKey.clear();
    m_peer = {};
    m_group = {};
    m_groupMembers.clear();
    m_messages.clear();
    m_lastMessageId = 0;
    m_groupLastMessageId = 0;
    m_unreadAnchorId = 0;
    m_hasMore = false;
    m_groupHasMore = false;
    m_fileDownloads.clear();
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
    const bool groupOpen = m_group.isValid();
    if (body.isEmpty() || m_sending || (m_peer.id <= 0 && !groupOpen))
        return;
    m_sending = true;
    m_sendButton->setEnabled(false);
    const int peerId = m_peer.id;
    const int groupId = m_group.id;
    const int replyTo = m_replyTo;
    auto onSent = [this](const ForumChatMessage &message) {
        m_sending = false;
        m_sendButton->setEnabled(true);
        m_draftSuppressed = true;
        m_chatInput->clear();
        m_draftSuppressed = false;
        // 已经发出去的内容不再是草稿
        if (!m_activeKey.isEmpty())
            ChatUiStore::instance()->setDraft(m_activeKey, QString());
        m_chatInput->setFocus();
        if (message.isValid())
            appendMessages(QVector<ForumChatMessage>{message});
        scrollToBottom();
        clearReplyTarget();
        renderFriendRows(); // 左栏预览从「草稿」变回最后一条消息
    };
    auto onSendFailed = [this](const QString &message, int httpStatus) {
        m_sending = false;
        m_sendButton->setEnabled(true);
        NotificationManager::showError(this, message);
        if (httpStatus == 403)
        {
            // 好友关系没了 / 被移出群聊或群已解散：关掉会话并刷新列表
            closeConversation();
            reload();
        }
    };
    if (groupOpen)
        ForumChatService::instance()->sendGroupMessage(groupId, body, replyTo, onSent, onSendFailed);
    else
        ForumChatService::instance()->sendMessage(peerId, body, replyTo, onSent, onSendFailed);
}

void ForumFriendsPage::loadOlderMessages()
{
    const bool groupOpen = m_group.isValid();
    if ((m_peer.id <= 0 && !groupOpen) || m_messages.isEmpty())
        return;
    if (groupOpen ? !m_groupHasMore : !m_hasMore)
        return;
    int oldestId = m_messages.first().id;
    for (const ForumChatMessage &message : m_messages)
        oldestId = qMin(oldestId, message.id);
    m_moreButton->setEnabled(false);
    if (groupOpen)
    {
        const int groupId = m_group.id;
        ForumChatService::instance()->fetchGroupMessages(
            groupId, oldestId,
            [this, groupId](const ForumGroupDetail &detail) {
                m_moreButton->setEnabled(true);
                if (!m_group.isValid() || detail.group.id != m_group.id)
                    return;
                QVector<ForumChatMessage> merged = m_messages;
                for (const ForumChatMessage &message : detail.messages)
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
                m_groupHasMore = detail.hasMore;
                renderMessages();
            },
            [this](const QString &message, int) {
                m_moreButton->setEnabled(true);
                NotificationManager::showError(this, message);
            });
        return;
    }
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
    if (message.kind == QStringLiteral("system"))
        return message.body;
    if (message.isImage())
        return tr("[图片]");
    if (message.isFile())
        return tr("[文件] %1").arg(message.fileName);
    return message.body;
}

QString ForumFriendsPage::findMessageSenderName(int messageId) const
{
    for (const ForumChatMessage &item : m_messages)
    {
        if (item.id != messageId)
            continue;
        if (item.mine)
            return tr("我");
        return item.senderName;
    }
    return QString();
}

void ForumFriendsPage::setReplyTarget(int messageId, const QString &preview, bool mine,
                                      const QString &peerName)
{
    m_replyTo = messageId;
    m_replyMine = mine;
    m_replyPreview = preview;
    const QString name = mine ? tr("我") : (peerName.isEmpty() ? m_peer.displayName : peerName);
    m_replyLabel->setText(tr("回复 %1：%2").arg(name, preview));
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
    if (!found || (m_peer.id <= 0 && !m_group.isValid()))
        return;
    const ForumChatMessage message = *found;
    const bool groupOpen = m_group.isValid();

    QMenu menu(this);
    QAction *replyAction = nullptr;
    QAction *copyAction = nullptr;
    QAction *forwardAction = nullptr;
    QAction *bookmarkAction = nullptr;
    QAction *copyImageAction = nullptr;
    QAction *saveImageAction = nullptr;
    QAction *withdrawAction = nullptr;
    if (!message.isWithdrawn())
    {
        replyAction = menu.addAction(tr("回复"));
        if (!message.body.isEmpty())
            copyAction = menu.addAction(tr("复制文字"));
        // 文字和图片可以转发；文件要先占用下载流量，先不做转发
        if (message.isImage() || !message.body.isEmpty())
            forwardAction = menu.addAction(tr("转发…"));
        const bool bookmarked =
            ChatBookmarkStore::instance()->contains(activeConversationKey(), message.id);
        bookmarkAction = menu.addAction(bookmarked ? tr("取消收藏") : tr("收藏消息"));
        if (message.isImage())
        {
            menu.addSeparator();
            copyImageAction = menu.addAction(tr("复制图片"));
            saveImageAction = menu.addAction(tr("保存图片…"));
        }
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
        // 群聊引用条带上被回复人的昵称
        setReplyTarget(message.id, messagePreview(message), message.mine,
                       groupOpen ? message.senderName : QString());
    }
    else if (chosen == copyAction)
    {
        QApplication::clipboard()->setText(message.body);
    }
    else if (chosen == forwardAction)
    {
        forwardMessage(message);
    }
    else if (chosen == bookmarkAction)
    {
        toggleMessageBookmark(message);
    }
    else if (chosen == copyImageAction)
    {
        copyMessageImage(message);
    }
    else if (chosen == saveImageAction)
    {
        saveMessageImage(message);
    }
    else if (chosen == withdrawAction)
    {
        if (groupOpen)
        {
            ForumChatService::instance()->withdrawGroupMessage(
                message.id,
                [this]() { openGroupConversation(m_group); },
                [this](const QString &msg, int) { NotificationManager::showError(this, msg); });
        }
        else
        {
            ForumChatService::instance()->withdrawMessage(
                message.id,
                [this]() { openConversation(m_peer); },
                [this](const QString &msg, int) { NotificationManager::showError(this, msg); });
        }
    }
}

void ForumFriendsPage::openBookmarks()
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        NotificationManager::showInfo(this, tr("查看聊天收藏需要先登录论坛账号"));
        return;
    }
    ChatBookmarkStore::instance()->setAccount(ForumAccountService::instance()->user().id);
    ChatBookmarkDialog::show(this, [this](const QString &key) { openConversationByKey(key); });
}

void ForumFriendsPage::openConversationByKey(const QString &key)
{
    if (key.isEmpty())
        return;
    const int id = key.mid(1).toInt();
    if (id <= 0)
        return;
    if (ChatUiStore::isGroupKey(key))
    {
        for (const ForumGroupInfo &groupInfo : m_snapshot.groups)
        {
            if (groupInfo.id == id)
            {
                openGroupConversation(groupInfo);
                return;
            }
        }
        reload();
        NotificationManager::showInfo(this, tr("这个群聊已经不在列表里了，可能已经解散"));
        return;
    }
    for (const ForumFriendInfo &friendInfo : m_snapshot.friends)
    {
        if (friendInfo.id == id)
        {
            openConversation(friendInfo);
            return;
        }
    }
    reload();
    NotificationManager::showInfo(this, tr("这段会话已经不在好友列表里了"));
}

void ForumFriendsPage::forwardMessage(const ForumChatMessage &message)
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        NotificationManager::showInfo(this, tr("转发消息需要先登录论坛账号"));
        return;
    }
    if (message.isWithdrawn() || (message.body.isEmpty() && !message.isImage()))
        return;

    ChatForwardTarget target;
    if (!ChatForwardDialog::pick(this, m_snapshot.friends, m_snapshot.groups,
                                 messagePreview(message), target))
        return;

    if (message.isImage())
    {
        forwardImageMessage(message, target.group, target.id, target.name);
        return;
    }

    setStatus(tr("正在转发…"));
    auto onSent = [this, target](const ForumChatMessage &sent) {
        setStatus(QString());
        if (sent.isValid())
            NotificationManager::showInfo(this, tr("已转发给 %1").arg(target.name));
    };
    auto onFail = [this](const QString &error, int) {
        setStatus(QString());
        NotificationManager::showError(this, error);
    };
    if (target.group)
        ForumChatService::instance()->sendGroupMessage(target.id, message.body, 0, onSent, onFail);
    else
        ForumChatService::instance()->sendMessage(target.id, message.body, 0, onSent, onFail);
}

void ForumFriendsPage::forwardImageMessage(const ForumChatMessage &message, bool toGroup,
                                           int targetId, const QString &targetName)
{
    setStatus(tr("正在转发图片…"));
    // 先取回原图（走图片缓存，已经看过的图不用重新下载），再重新上传到图床
    ForumImageStore::instance()->requestImage(
        message.mediaUrl(), [this, message, toGroup, targetId, targetName](const QImage &image) {
            if (image.isNull())
            {
                setStatus(QString());
                NotificationManager::showError(this, tr("图片加载失败，转发没有完成"));
                return;
            }
            const QString tempPath = QDir::tempPath() + QStringLiteral("/bbf_forward_%1.png")
                                                               .arg(QDateTime::currentMSecsSinceEpoch());
            if (!image.save(tempPath, "PNG"))
            {
                setStatus(QString());
                NotificationManager::showError(this, tr("图片保存失败，转发没有完成"));
                return;
            }
            const QString caption = message.body.left(200);
            auto onSent = [this, tempPath, targetName](const ForumChatMessage &sent) {
                QFile::remove(tempPath);
                setStatus(QString());
                if (sent.isValid())
                    NotificationManager::showInfo(this, tr("已转发给 %1").arg(targetName));
            };
            auto onFail = [this, tempPath](const QString &error, int) {
                QFile::remove(tempPath);
                setStatus(QString());
                NotificationManager::showError(this, error);
            };
            if (toGroup)
                ForumChatService::instance()->sendGroupImageMessage(targetId, tempPath, caption, 0,
                                                                    onSent, onFail);
            else
                ForumChatService::instance()->sendImageMessage(targetId, tempPath, caption, 0,
                                                               onSent, onFail);
        });
}

void ForumFriendsPage::copyMessageImage(const ForumChatMessage &message)
{
    ForumImageStore::instance()->requestImage(
        message.mediaUrl(), [this](const QImage &image) {
            if (image.isNull())
            {
                NotificationManager::showError(this, tr("图片加载失败"));
                return;
            }
            QApplication::clipboard()->setImage(image);
            NotificationManager::showInfo(this, tr("图片已复制到剪贴板"));
        });
}

void ForumFriendsPage::saveMessageImage(const ForumChatMessage &message)
{
    ForumImageStore::instance()->requestImage(
        message.mediaUrl(), [this](const QImage &image) {
            if (image.isNull())
            {
                NotificationManager::showError(this, tr("图片加载失败"));
                return;
            }
            QString directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            if (directory.isEmpty())
                directory = QDir::homePath();
            const QString suggested = directory + QStringLiteral("/BlockBox-chat-%1.png")
                                                      .arg(QDateTime::currentDateTime().toString(
                                                          QStringLiteral("yyyyMMdd-HHmmss")));
            QString path = QFileDialog::getSaveFileName(this, tr("保存图片"), suggested,
                                                        tr("图片 (*.png *.jpg *.jpeg)"));
            if (path.isEmpty())
                return;
            if (QFileInfo(path).suffix().isEmpty())
                path += QStringLiteral(".png");
            if (!image.save(path))
            {
                NotificationManager::showError(this, tr("图片保存失败，请换个位置再试"));
                return;
            }
            NotificationManager::showInfo(this, tr("图片已保存到 %1").arg(path));
        });
}

void ForumFriendsPage::toggleMessageBookmark(const ForumChatMessage &message)
{
    const QString key = activeConversationKey();
    if (key.isEmpty())
        return;
    ChatBookmarkStore::instance()->setAccount(ForumAccountService::instance()->user().id);
    ChatBookmarkStore *store = ChatBookmarkStore::instance();
    if (store->contains(key, message.id))
    {
        store->removeByMessage(key, message.id);
        NotificationManager::showInfo(this, tr("已取消收藏"));
        return;
    }

    ChatBookmark bookmark;
    bookmark.conversationKey = key;
    bookmark.conversationName = m_group.isValid() ? m_group.name : m_peer.displayName;
    bookmark.messageId = message.id;
    bookmark.kind = message.kind;
    bookmark.body = message.body;
    bookmark.image = message.image;
    bookmark.fileName = message.fileName;
    bookmark.fileSize = message.fileSize;
    bookmark.createdAt = message.createdAt;
    if (message.mine)
        bookmark.senderName = tr("我");
    else if (m_group.isValid() && !message.senderName.isEmpty())
        bookmark.senderName = message.senderName;
    else
        bookmark.senderName = m_peer.displayName;
    if (store->add(bookmark).isEmpty())
    {
        NotificationManager::showError(this, tr("收藏失败，请稍后再试"));
        return;
    }
    NotificationManager::showInfo(this, tr("已收藏，点聊天页左上角的星标可以查看"));
}

void ForumFriendsPage::scrollToMessage(int messageId)
{
    QWidget *row = m_messageRows.value(messageId, nullptr);
    if (!row)
    {
        NotificationManager::showInfo(this, tr("原消息不在当前已加载的记录里，先点上方「查看更早的消息」"));
        return;
    }
    m_msgsScroll->ensureWidgetVisible(row, 0, 90);
    // 闪一下底色，让人一眼看到跳到了哪条消息
    row->setProperty("flash", true);
    row->style()->unpolish(row);
    row->style()->polish(row);
    QPointer<QWidget> guard(row);
    QTimer::singleShot(1300, this, [guard]() {
        if (!guard)
            return;
        guard->setProperty("flash", false);
        guard->style()->unpolish(guard);
        guard->style()->polish(guard);
    });
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
    if ((m_peer.id <= 0 && !m_group.isValid()) || m_sending)
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
    const bool groupOpen = m_group.isValid();
    if ((m_peer.id <= 0 && !groupOpen) || m_sending)
        return;
    m_sending = true;
    m_sendButton->setEnabled(false);
    m_imageButton->setEnabled(false);
    m_fileButton->setEnabled(false);
    setStatus(tr("正在发送图片…"));
    const int peerId = m_peer.id;
    const int groupId = m_group.id;
    const int replyTo = m_replyTo;
    const bool isTempPaste = filePath.startsWith(QDir::tempPath());
    auto onImageSent = [this, isTempPaste, filePath](const ForumChatMessage &message) {
        m_sending = false;
        m_sendButton->setEnabled(true);
        m_imageButton->setEnabled(true);
        m_fileButton->setEnabled(true);
        setStatus(QString());
        if (isTempPaste)
            QFile::remove(filePath);
        if (message.isValid())
            appendMessages(QVector<ForumChatMessage>{message});
        scrollToBottom();
        clearReplyTarget();
    };
    auto onImageFailed = [this, isTempPaste, filePath](const QString &msg, int) {
        m_sending = false;
        m_sendButton->setEnabled(true);
        m_imageButton->setEnabled(true);
        m_fileButton->setEnabled(true);
        setStatus(QString());
        if (isTempPaste)
            QFile::remove(filePath);
        NotificationManager::showError(this, msg);
    };
    if (groupOpen)
        ForumChatService::instance()->sendGroupImageMessage(
            groupId, filePath, QString(), replyTo, onImageSent, onImageFailed);
    else
        ForumChatService::instance()->sendImageMessage(peerId, filePath, QString(), replyTo,
                                                       onImageSent, onImageFailed);
}

void ForumFriendsPage::pickAndSendFile()
{
    if ((m_peer.id <= 0 && !m_group.isValid()) || m_sending)
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("选择文件"), QString(),
                                                      tr("所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    sendFileFromPath(path);
}

void ForumFriendsPage::sendFileFromPath(const QString &filePath)
{
    const bool groupOpen = m_group.isValid();
    if ((m_peer.id <= 0 && !groupOpen) || m_sending)
        return;
    // 发之前本地先过一遍大小和类型，超了当场提示（服务端还会再校验一次）
    const QString problem = ForumChatService::checkChatFile(filePath);
    if (!problem.isEmpty())
    {
        NotificationManager::showError(this, problem);
        return;
    }

    m_sending = true;
    m_sendButton->setEnabled(false);
    m_imageButton->setEnabled(false);
    m_fileButton->setEnabled(false);
    setStatus(tr("正在上传文件… 0%"));
    const int peerId = m_peer.id;
    const int groupId = m_group.id;
    const int replyTo = m_replyTo;
    auto onFileSent = [this](const ForumChatMessage &message) {
        m_sending = false;
        m_sendButton->setEnabled(true);
        m_imageButton->setEnabled(true);
        m_fileButton->setEnabled(true);
        setStatus(QString());
        if (message.isValid())
            appendMessages(QVector<ForumChatMessage>{message});
        scrollToBottom();
        clearReplyTarget();
    };
    auto onFileFailed = [this](const QString &msg, int) {
        m_sending = false;
        m_sendButton->setEnabled(true);
        m_imageButton->setEnabled(true);
        m_fileButton->setEnabled(true);
        setStatus(QString());
        NotificationManager::showError(this, msg);
    };
    const auto onProgress = [this](qint64 sent, qint64 total) {
        if (total > 0)
            setStatus(tr("正在上传文件… %1%").arg(qRound(sent * 100.0 / static_cast<double>(total))));
    };
    if (groupOpen)
        ForumChatService::instance()->sendGroupFileMessage(groupId, filePath, QString(), replyTo,
                                                           onProgress, onFileSent, onFileFailed);
    else
        ForumChatService::instance()->sendFileMessage(peerId, filePath, QString(), replyTo,
                                                      onProgress, onFileSent, onFileFailed);
}

void ForumFriendsPage::downloadChatFile(int messageId)
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
    if (!found || !found->isFile() || m_fileDownloads.contains(messageId))
        return;
    const ForumChatMessage message = *found;

    // 先问保存到哪，确认了才去服务器提取（提取会计入本周下载流量）
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString defaultName = message.fileName;
    defaultName.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString defaultPath = documents.isEmpty()
                                    ? defaultName
                                    : documents + QStringLiteral("/") + defaultName;
    const QString savePath = QFileDialog::getSaveFileName(this, tr("保存文件"), defaultPath);
    if (savePath.isEmpty())
        return;

    QPointer<QPushButton> button(qobject_cast<QPushButton *>(sender()));
    m_fileDownloads.insert(messageId);
    if (button)
    {
        button->setEnabled(false);
        button->setText(tr("下载中…"));
    }
    setStatus(tr("正在提取文件…"));

    // 提取（服务端扣本周流量）→ 拿下载地址 → 带进度下载落盘
    const bool groupOpen = m_group.isValid();
    ForumChatService::instance()->extractFile(
        groupOpen, messageId,
        [this, button, savePath, messageId](const ForumChatFileExtract &extract) {
            if (button)
                button->setToolTip(tr("本周剩余下载流量 %1").arg(DownloadUtils::formatSize(extract.remaining)));
            ForumChatService::instance()->downloadChatFile(
                extract.downloadUrl(), savePath,
                [this, button, size = extract.size](qint64 sent, qint64) {
                    if (button && size > 0)
                        button->setText(tr("%1%").arg(qRound(sent * 100.0 / static_cast<double>(size))));
                },
                [this, button, messageId, savePath]() {
                    m_fileDownloads.remove(messageId);
                    if (button)
                    {
                        button->setEnabled(true);
                        button->setText(tr("已保存"));
                    }
                    setStatus(QString());
                    NotificationManager::showSuccess(this, tr("文件已保存到 %1").arg(savePath));
                },
                [this, button, messageId](const QString &msg, int) {
                    m_fileDownloads.remove(messageId);
                    if (button)
                    {
                        button->setEnabled(true);
                        button->setText(tr("提取"));
                    }
                    setStatus(QString());
                    NotificationManager::showError(this, msg);
                });
        },
        [this, button, messageId](const QString &msg, int) {
            m_fileDownloads.remove(messageId);
            if (button)
            {
                button->setEnabled(true);
                button->setText(tr("提取"));
            }
            setStatus(QString());
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
