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
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "components/AppMessageBox.h"
#include "components/ChatBookmarkDialog.h"
#include "components/ChatForwardDialog.h"
#include "components/ChatSettingsDialog.h"
#include "components/ForumKit.h"
#include "components/NotificationManager.h"
#include "components/CreateGroupDialog.h"
#include "utils/ChatBookmarkStore.h"
#include "utils/ChatP2P.h"
#include "utils/ChatPrefs.h"
#include "utils/ChatUiStore.h"
#include "utils/DownloadUtils.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumChatService.h"
#include "utils/ForumImageStore.h"
#include "utils/IconHelper.h"
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

    // 会话搜索：输入停顿后再重建左栏，避免每敲一个字都重排
    m_convFilterTimer.setSingleShot(true);
    m_convFilterTimer.setInterval(180);
    connect(&m_convFilterTimer, &QTimer::timeout, this, [this]() { renderSide(); });

    // 草稿：停下打字 0.6 秒才写盘
    m_draftTimer.setSingleShot(true);
    m_draftTimer.setInterval(600);
    connect(&m_draftTimer, &QTimer::timeout, this, &ForumFriendsPage::saveActiveDraft);

    // ── P2P 直传 ──
    m_p2pFallbackTimer.setSingleShot(true);
    connect(&m_p2pFallbackTimer, &QTimer::timeout, this, &ForumFriendsPage::onP2pFallbackTimeout);
    connect(ChatP2P::instance(), &ChatP2P::shareProgress, this, &ForumFriendsPage::onP2pShareProgress);
    connect(ChatP2P::instance(), &ChatP2P::shareFinished, this, &ForumFriendsPage::onP2pShareFinished);
    connect(ChatP2P::instance(), &ChatP2P::receiveFinished, this, &ForumFriendsPage::onP2pReceiveFinished);
    connect(ChatP2P::instance(), &ChatP2P::receiveProgress, this,
            [this](const QString &requestId, double percent, qint64, qint64, qint64) {
                const int messageId = m_p2pRecvMessageIds.value(requestId, 0);
                if (QPushButton *button = m_p2pRecvButtons.value(messageId))
                    button->setText(tr("%1%").arg(qRound(percent)));
            });

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

    // 聊天设置里改了发送方式 / 字号：同步输入提示，并按新字号重绘当前消息
    connect(ChatPrefs::instance(), &ChatPrefs::changed, this, [this]() {
        updateSendHint();
        if (m_messages.isEmpty())
            return;
        if (m_group.isValid() || m_peer.id > 0)
            renderMessages();
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

    // 面板头部：标题 + 一张菜单（原来三个小按钮的功能都收进菜单里）
    auto *sideHeader = new QWidget(sidePanel);
    sideHeader->setObjectName(QStringLiteral("subNavHeader"));
    auto *sideHeaderLayout = new QHBoxLayout(sideHeader);
    sideHeaderLayout->setContentsMargins(16, 12, 10, 12);
    sideHeaderLayout->setSpacing(4);
    auto *sideTitle = new QLabel(tr("聊天"), sideHeader);
    sideTitle->setObjectName(QStringLiteral("forumSideTitle"));
    sideHeaderLayout->addWidget(sideTitle);
    sideHeaderLayout->addStretch(1);
    m_menuButton = new QPushButton(sideHeader);
    m_menuButton->setObjectName(QStringLiteral("subNavCollapseBtn"));
    m_menuButton->setText(QStringLiteral("☰"));
    m_menuButton->setFixedSize(24, 24);
    m_menuButton->setCursor(Qt::PointingHandCursor);
    m_menuButton->setToolTip(tr("菜单：创建群聊 / 添加好友 / 聊天收藏 / 刷新"));
    sideHeaderLayout->addWidget(m_menuButton);
    sideLayout->addWidget(sideHeader);

    // ☰ 菜单：和网页版一致的三项，另加「聊天收藏」（启动器独有的入口）
    auto *sideMenu = new QMenu(this);
    sideMenu->addAction(tr("创建群聊"), this, &ForumFriendsPage::openCreateGroupDialog);
    sideMenu->addAction(tr("添加好友"), this, &ForumFriendsPage::openAddFriendDialog);
    sideMenu->addAction(tr("聊天收藏"), this, &ForumFriendsPage::openBookmarks);
    sideMenu->addSeparator();
    sideMenu->addAction(tr("刷新"), this, [this]() { reload(); });
    connect(m_menuButton, &QPushButton::clicked, this, [this, sideMenu]() {
        sideMenu->exec(m_menuButton->mapToGlobal(QPoint(0, m_menuButton->height() + 4)));
    });

    // 会话搜索：按昵称 / 最后一条消息 / 草稿过滤
    m_convSearchRow = new QWidget(sidePanel);
    auto *convSearchRowLayout = new QVBoxLayout(m_convSearchRow);
    convSearchRowLayout->setContentsMargins(12, 8, 12, 2);
    convSearchRowLayout->setSpacing(0);
    m_convSearchEdit = new QLineEdit(m_convSearchRow);
    m_convSearchEdit->setObjectName(QStringLiteral("forumConvSearch"));
    m_convSearchEdit->setPlaceholderText(tr("搜索好友或群聊"));
    m_convSearchEdit->setClearButtonEnabled(true);
    m_convSearchEdit->setFixedHeight(30);
    connect(m_convSearchEdit, &QLineEdit::textChanged, this,
            [this]() { updateConversationFilter(); });
    convSearchRowLayout->addWidget(m_convSearchEdit);
    sideLayout->addWidget(m_convSearchRow);

    // 分组标题：会话 · N
    m_sideGroupTitle = new QLabel(tr("会话"), sidePanel);
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
    // 聊天设置：置顶 / 免打扰、发送方式与字号都收在这个居中弹窗里
    m_chatSettingsButton = new QPushButton(tr("设置"), head);
    m_chatSettingsButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_chatSettingsButton->setCursor(Qt::PointingHandCursor);
    m_chatSettingsButton->setFixedHeight(26);
    m_chatSettingsButton->setToolTip(tr("聊天设置"));
    headLayout->addWidget(m_chatSettingsButton);
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

    // 输入区：引用条 + 输入卡片（和 AI 助手页同款：文本域在上，工具行在下）
    auto *composerWrap = new QWidget(m_chatMain);
    auto *composerWrapLayout = new QVBoxLayout(composerWrap);
    composerWrapLayout->setContentsMargins(16, 6, 16, 14);
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
    auto *composerLayout = new QVBoxLayout(composer);
    composerLayout->setContentsMargins(12, 8, 8, 8);
    composerLayout->setSpacing(4);

    // 文本域：整行铺满卡片，随内容长高（字号与 AI 助手页一致）
    m_chatInput = new QTextEdit(composer);
    m_chatInput->setObjectName(QStringLiteral("forumChatInput"));
    m_chatInput->setFixedHeight(44);
    m_chatInput->setAcceptRichText(false);
    m_chatInput->setFrameShape(QFrame::NoFrame);
    // 文档留白交给样式里的内边距，行高才能按整行对齐（不会切出半行字）
    m_chatInput->document()->setDocumentMargin(0);
    m_chatInput->installEventFilter(this);
    composerLayout->addWidget(m_chatInput);

    // 工具行：左边表情 / 图片 / 文件，右边字数与发送
    auto *composerTools = new QHBoxLayout();
    composerTools->setContentsMargins(0, 0, 0, 0);
    composerTools->setSpacing(2);

    m_emojiButton = new QPushButton(composer);
    m_emojiButton->setIcon(ResourceManager::instance()->loadThemedIcon(
        QStringLiteral(":/Images/Icons/smile.svg")));
    m_emojiButton->setIconSize(QSize(16, 16));
    m_emojiButton->setObjectName(QStringLiteral("forumChatToolButton"));
    m_emojiButton->setFixedSize(30, 30);
    m_emojiButton->setCursor(Qt::PointingHandCursor);
    m_emojiButton->setToolTip(tr("表情"));
    m_emojiButton->setAccessibleName(tr("表情"));
    connect(m_emojiButton, &QPushButton::clicked, this, &ForumFriendsPage::toggleEmojiPopup);
    composerTools->addWidget(m_emojiButton);

    m_imageButton = new QPushButton(composer);
    m_imageButton->setIcon(ResourceManager::instance()->loadThemedIcon(
        QStringLiteral(":/Images/Icons/screenshot.svg")));
    m_imageButton->setIconSize(QSize(16, 16));
    m_imageButton->setObjectName(QStringLiteral("forumChatToolButton"));
    m_imageButton->setFixedSize(30, 30);
    m_imageButton->setCursor(Qt::PointingHandCursor);
    m_imageButton->setToolTip(tr("发送图片（也可以直接把截图粘进输入框）"));
    m_imageButton->setAccessibleName(tr("发送图片"));
    connect(m_imageButton, &QPushButton::clicked, this, &ForumFriendsPage::pickAndSendImage);
    composerTools->addWidget(m_imageButton);

    m_fileButton = new QPushButton(composer);
    m_fileButton->setIcon(ResourceManager::instance()->loadThemedIcon(
        QStringLiteral(":/Images/Icons/paperclip.svg")));
    m_fileButton->setIconSize(QSize(16, 16));
    m_fileButton->setObjectName(QStringLiteral("forumChatToolButton"));
    m_fileButton->setFixedSize(30, 30);
    m_fileButton->setCursor(Qt::PointingHandCursor);
    m_fileButton->setToolTip(tr("发送文件（单个不超过 5MB）"));
    m_fileButton->setAccessibleName(tr("发送文件"));
    connect(m_fileButton, &QPushButton::clicked, this, &ForumFriendsPage::pickAndSendFile);
    composerTools->addWidget(m_fileButton);

    composerTools->addStretch(1);

    m_charCounter = new QLabel(composer);
    m_charCounter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_charCounter->setStyleSheet(StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: 11px;")));
    m_charCounter->hide();
    composerTools->addWidget(m_charCounter, 0, Qt::AlignVCenter);

    // 发送：右下角的主色图标按钮（同 AI 助手页）
    m_sendButton = new QPushButton(composer);
    m_sendButton->setObjectName(QStringLiteral("forumChatSendBtn"));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setFixedSize(32, 32);
    m_sendButton->setIconSize(QSize(16, 16));
    m_sendButton->setIcon(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/send.svg"),
                                                    QColor(QStringLiteral("#FFFFFF")), 16));
    m_sendButton->setAccessibleName(tr("发送"));
    connect(m_sendButton, &QPushButton::clicked, this, &ForumFriendsPage::sendCurrentMessage);
    composerTools->addWidget(m_sendButton, 0, Qt::AlignVCenter);

    composerLayout->addLayout(composerTools);
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

    // 右栏之二：系统通知面板（复用站内通知，好友申请在这里直接同意 / 拒绝）
    m_noticeCard = new QWidget(this);
    m_noticeCard->setObjectName(QStringLiteral("forumChatStage"));
    m_noticeCard->hide();
    auto *noticeLayout = new QVBoxLayout(m_noticeCard);
    noticeLayout->setContentsMargins(0, 0, 0, 0);
    noticeLayout->setSpacing(0);

    auto *noticeHead = new QWidget(m_noticeCard);
    noticeHead->setObjectName(QStringLiteral("forumChatHead"));
    noticeHead->setAttribute(Qt::WA_StyledBackground, true);
    auto *noticeHeadLayout = new QHBoxLayout(noticeHead);
    noticeHeadLayout->setContentsMargins(18, 12, 14, 12);
    noticeHeadLayout->setSpacing(10);

    auto *noticeIcon = new QLabel(noticeHead);
    noticeIcon->setFixedSize(36, 36);
    noticeIcon->setAlignment(Qt::AlignCenter);
    noticeIcon->setPixmap(ResourceManager::instance()
                              ->loadThemedIcon(QStringLiteral(":/Images/Icons/topbar_notify.svg"))
                              .pixmap(20, 20));
    noticeHeadLayout->addWidget(noticeIcon);

    auto *noticeTitleColumn = new QVBoxLayout();
    noticeTitleColumn->setSpacing(1);
    auto *noticeTitle = new QLabel(tr("系统通知"), noticeHead);
    noticeTitle->setObjectName(QStringLiteral("forumRowTitle"));
    noticeTitleColumn->addWidget(noticeTitle);
    m_noticeHint = new QLabel(tr("暂无未读通知"), noticeHead);
    m_noticeHint->setObjectName(QStringLiteral("forumMuted"));
    m_noticeHint->setStyleSheet(StyleKit::resolve(QStringLiteral("font-size: 11px; color: @TEXT_TERTIARY@;")));
    noticeTitleColumn->addWidget(m_noticeHint);
    noticeHeadLayout->addLayout(noticeTitleColumn, 1);

    m_noticeMarkAllButton = new QPushButton(tr("全部已读"), noticeHead);
    m_noticeMarkAllButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_noticeMarkAllButton->setCursor(Qt::PointingHandCursor);
    m_noticeMarkAllButton->setFixedHeight(26);
    connect(m_noticeMarkAllButton, &QPushButton::clicked, this, &ForumFriendsPage::markAllNotificationsRead);
    noticeHeadLayout->addWidget(m_noticeMarkAllButton);
    noticeLayout->addWidget(noticeHead);

    m_noticeScroll = new QScrollArea(m_noticeCard);
    m_noticeScroll->setObjectName(QStringLiteral("forumSideScroll"));
    m_noticeScroll->setWidgetResizable(true);
    m_noticeScroll->setFrameShape(QFrame::NoFrame);
    m_noticeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_noticeContainer = new QWidget(m_noticeScroll);
    m_noticeListLayout = new QVBoxLayout(m_noticeContainer);
    m_noticeListLayout->setContentsMargins(10, 8, 10, 14);
    m_noticeListLayout->setSpacing(2);
    m_noticeListLayout->addStretch(1);
    m_noticeScroll->setWidget(m_noticeContainer);
    noticeLayout->addWidget(m_noticeScroll, 1);
    columns->addWidget(m_noticeCard, 1);

    // 输入框占位文字、发送按钮提示跟着「Enter 发送」偏好走
    updateSendHint();

    connect(m_chatSettingsButton, &QPushButton::clicked, this, &ForumFriendsPage::openChatSettings);
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
    // 页面显示后样式（字号）才真正生效，这里按实际行高重新量一次输入框高度；
    // 再延后一拍量一次，避免样式还没刷完时量到旧字号。
    refreshInputSize();
    QTimer::singleShot(0, this, [this]() { refreshInputSize(); });
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
            const Qt::KeyboardModifiers mods = keyEvent->modifiers();
            // Ctrl+Enter 始终发送；Enter 是否发送看「聊天设置」里的偏好，
            // Shift+Enter 始终换行（与输入框占位提示保持一致）
            if (mods & Qt::ControlModifier)
            {
                sendCurrentMessage();
                return true;
            }
            if (mods & Qt::ShiftModifier)
                return QWidget::eventFilter(watched, event);
            if (ChatPrefs::instance()->sendOnEnter())
            {
                sendCurrentMessage();
                return true;
            }
            return QWidget::eventFilter(watched, event);
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
    setStatus(tr("正在加载…"));
    ForumChatService::instance()->fetchFriends(
        [this](const ForumFriendSnapshot &snapshot) {
            setStatus(QString());
            m_snapshot = snapshot;
            refreshNoticeBadge();
        },
        [this](const QString &message, int) {
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
            const int beforeIncoming = m_snapshot.incoming.size();
            m_snapshot.friends = result.friends;
            m_snapshot.groups = result.groups;
            m_snapshot.totalUnread = result.totalUnread;
            // 有新的好友申请：站内通知也跟着变，顺手把「系统通知」角标刷一下
            if (result.incomingRequests != beforeIncoming)
                refreshNoticeBadge();
            else
                renderSide();
            if (m_peer.id > 0 && !result.messages.isEmpty())
                appendMessages(result.messages);
            if (m_group.isValid() && !result.groupMessages.isEmpty())
                appendMessages(result.groupMessages);
        },
        [this](const QString &, int) { m_pollBusy = false; });
}

void ForumFriendsPage::refreshNoticeBadge()
{
    ForumContentService::instance()->fetchNotifications(
        1,
        [this](const QVector<ForumNotificationItem> &items, int unread, const ForumPagination &) {
            m_notifications = items;
            m_noticeUnread = unread;
            m_notificationsLoaded = true;
            renderSide();
            if (m_noticeCard && m_noticeCard->isVisible())
                renderNotificationPane();
        },
        [this](const QString &, int) {
            // 拉通知失败不影响会话列表，只是角标不更新
            renderSide();
        });
}

// ────────────────────────────────────────────────────────────── 左栏

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
    renderSide();
}

void ForumFriendsPage::openChatSettings()
{
    const QString key = activeConversationKey();
    if (key.isEmpty())
        return; // 没打开会话时头部不显示设置按钮，这里只做兜底

    const bool group = m_group.isValid();
    ChatSettingsDialog dialog(this);
    dialog.setConversation(key, group ? m_group.name : m_peer.displayName, group);
    // 弹窗里清空了草稿：输入框里还没发出去的文字也一起清掉
    connect(&dialog, &ChatSettingsDialog::draftCleared, this, [this, key]() {
        if (key == activeConversationKey() && m_chatInput)
            m_chatInput->clear();
    });
    dialog.exec();
    // 备注 / 置顶 / 免打扰 / 预览开关可能变了：左栏重排并刷新会话头
    renderSide();
    if (m_group.isValid())
        m_chatName->setText(conversationTitle(true, m_group.id, m_group.name));
    else if (m_peer.id > 0)
        renderChatHead();
    // 发送方式与字号由 ChatPrefs::changed 另外刷新
}

QString ForumFriendsPage::conversationTitle(bool group, int id, const QString &fallback) const
{
    const QString remark =
        ChatUiStore::instance()->remark(ChatUiStore::conversationKey(group, id));
    return remark.isEmpty() ? fallback : remark;
}

void ForumFriendsPage::updateSendHint()
{
    if (!m_chatInput || !m_sendButton)
        return;
    const bool enterSends = ChatPrefs::instance()->sendOnEnter();
    m_chatInput->setPlaceholderText(enterSends ? tr("输入消息，Enter 发送，Shift+Enter 换行")
                                               : tr("输入消息，Ctrl+Enter 发送，Enter 换行"));
    m_sendButton->setToolTip(enterSends ? tr("Enter 发送，Shift+Enter 换行")
                                        : tr("Ctrl+Enter 发送，Enter 换行"));
}

void ForumFriendsPage::renderSide()
{
    ForumKit::clearLayout(m_sideListLayout);

    // 系统通知：固定第一条，有没有好友申请都在，未读用角标提示
    {
        auto *row = new ForumKit::ClickableCard(this);
        row->setObjectName(QStringLiteral("forumSideRow"));
        row->onClick = [this]() { openNotificationPane(); };

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 10, 12, 10);
        rowLayout->setSpacing(10);

        auto *icon = new QLabel(row);
        icon->setFixedSize(36, 36);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(ResourceManager::instance()
                            ->loadThemedIcon(QStringLiteral(":/Images/Icons/topbar_notify.svg"))
                            .pixmap(18, 18));
        rowLayout->addWidget(icon);

        auto *textColumn = new QVBoxLayout();
        textColumn->setSpacing(3);
        auto *nameRow = new QHBoxLayout();
        nameRow->setSpacing(6);
        auto *name = new QLabel(tr("系统通知"), row);
        name->setObjectName(QStringLiteral("forumRowTitle"));
        nameRow->addWidget(name);
        if (m_noticeUnread > 0)
        {
            auto *badge = new QLabel(row);
            badge->setObjectName(QStringLiteral("forumUnreadBadge"));
            badge->setText(m_noticeUnread > 99 ? QStringLiteral("99+") : QString::number(m_noticeUnread));
            nameRow->addWidget(badge);
        }
        nameRow->addStretch(1);
        textColumn->addLayout(nameRow);

        QString previewText;
        if (m_snapshot.incoming.size() > 1)
            previewText = tr("%1 位玩家申请加你为好友").arg(m_snapshot.incoming.size());
        else if (m_snapshot.incoming.size() == 1)
            previewText = tr("有人申请加你为好友");
        else if (m_noticeUnread > 0)
            previewText = tr("%1 条未读通知").arg(m_noticeUnread);
        else
            previewText = tr("暂无新通知");
        auto *preview = new ForumKit::ElidedLabel(row);
        preview->setObjectName(QStringLiteral("forumMuted"));
        preview->setFullText(previewText);
        textColumn->addWidget(preview);
        rowLayout->addLayout(textColumn, 1);

        m_sideListLayout->addWidget(row);
    }

    renderFriendRows();
}

void ForumFriendsPage::renderFriendRows()
{
    const int scrollValue = m_sideScroll->verticalScrollBar()->value();

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
        const bool hidePreview = store->hidesPreview(key);
        const QString draft = store->draft(key);
        // 会话备注：本机起的名字优先，没有备注时还是昵称
        const QString remark = store->remark(key);
        const QString title = remark.isEmpty() ? friendInfo.displayName : remark;

        // 预览：有未发送草稿就先显示草稿，否则按最后一条消息的类型显示
        QString preview = tr("还没有聊过天");
        if (!draft.isEmpty())
            preview = draft.simplified();
        else if (friendInfo.lastMessageKind == QStringLiteral("image"))
            preview = tr("[图片]");
        else if (friendInfo.lastMessageKind == QStringLiteral("file"))
            preview = tr("[文件]");
        else if (friendInfo.lastMessageKind == QStringLiteral("p2p"))
            preview = tr("[文件]"); // 直传邀约在列表里按文件显示
        else if (friendInfo.lastMessageKind == QStringLiteral("withdrawn"))
            preview = tr("撤回了一条消息");
        else if (!friendInfo.lastMessageBody.isEmpty())
            preview = friendInfo.lastMessageBody;
        if (draft.isEmpty() && !friendInfo.lastMessageBody.isEmpty() && friendInfo.lastMessageMine &&
            friendInfo.lastMessageKind != QStringLiteral("withdrawn"))
            preview = tr("我：%1").arg(preview);
        QString previewText = draft.isEmpty() ? preview : tr("[草稿] %1").arg(preview);
        // 隐藏预览：只说明有消息，不露出内容（草稿是自己写的字，照常显示）
        if (hidePreview && draft.isEmpty() && friendInfo.lastMessageAt > 0)
            previewText = tr("消息内容已隐藏");

        // 会话搜索：昵称 / 账号名 / 本机备注 / 预览内容任一命中才显示
        if (filtering)
        {
            const bool hit = title.contains(filter, Qt::CaseInsensitive) ||
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
        name->setFullText(title);
        if (!remark.isEmpty())
            name->setToolTip(tr("昵称：%1").arg(friendInfo.displayName));
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
        const bool hidePreview = store->hidesPreview(key);
        const QString draft = store->draft(key);
        const QString remark = store->remark(key);
        const QString title = remark.isEmpty() ? group.name : remark;

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
        QString previewText = draft.isEmpty() ? preview : tr("[草稿] %1").arg(preview);
        // 隐藏预览：列表里只提示有消息，不露出内容（草稿是自己写的字，照常显示）
        if (hidePreview && draft.isEmpty() && group.lastMessageAt > 0)
            previewText = tr("消息内容已隐藏");

        // 会话搜索：群名 / 本机备注 / 预览内容任一命中才显示
        if (filtering && !title.contains(filter, Qt::CaseInsensitive) &&
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
        name->setFullText(title);
        if (!remark.isEmpty())
            name->setToolTip(tr("群名：%1").arg(group.name));
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

// ────────────────────────────────────────────────────────────── 系统通知（复用站内通知）

void ForumFriendsPage::openNotificationPane()
{
    if (m_noticeCard)
    {
        m_chatCard->hide();
        m_noticeCard->show();
    }
    m_notificationsLoaded = false;
    renderNotificationPane();
    ForumContentService::instance()->fetchNotifications(
        1,
        [this](const QVector<ForumNotificationItem> &items, int unread, const ForumPagination &) {
            m_notifications = items;
            m_noticeUnread = unread;
            m_notificationsLoaded = true;
            if (m_noticeCard && m_noticeCard->isVisible())
                renderNotificationPane();
            renderSide();
        },
        [this](const QString &message, int) {
            m_notificationsLoaded = true;
            if (m_noticeCard && m_noticeCard->isVisible())
                renderNotificationPane();
            NotificationManager::showError(this, message);
        });
}

void ForumFriendsPage::renderNotificationPane()
{
    if (!m_noticeListLayout)
        return;

    const int scrollValue = m_noticeScroll->verticalScrollBar()->value();
    ForumKit::clearLayout(m_noticeListLayout);

    if (m_noticeHint)
        m_noticeHint->setText(m_noticeUnread > 0 ? tr("%1 条未读通知").arg(m_noticeUnread)
                                                 : tr("暂无未读通知"));
    if (m_noticeMarkAllButton)
        m_noticeMarkAllButton->setEnabled(m_noticeUnread > 0);

    if (m_notifications.isEmpty())
    {
        if (!m_notificationsLoaded)
        {
            auto *loading = ForumKit::makeMuted(this, tr("正在加载通知…"));
            loading->setAlignment(Qt::AlignCenter);
            m_noticeListLayout->addSpacing(24);
            m_noticeListLayout->addWidget(loading);
            m_noticeListLayout->addStretch(1);
            return;
        }
        auto *empty = ForumKit::makeMuted(
            this, tr("还没有通知。别人回复你、在帖子里 @ 你，或申请加你好友，都会收进这里。"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        m_noticeListLayout->addSpacing(30);
        m_noticeListLayout->addWidget(empty);
        m_noticeListLayout->addStretch(1);
        return;
    }

    for (const ForumNotificationItem &item : m_notifications)
    {
        auto *row = new ForumKit::ClickableCard(this);
        row->setObjectName(QStringLiteral("forumSideRow"));

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 10, 12, 10);
        rowLayout->setSpacing(10);

        // 左侧类型图标：回复 / 提及 / 系统 / 好友，和通知页保持同一套
        QString iconPath = QStringLiteral(":/Images/Icons/send.svg");
        if (item.type == QStringLiteral("mention"))
            iconPath = QStringLiteral(":/Images/Icons/sparkles.svg");
        else if (item.type == QStringLiteral("system"))
            iconPath = QStringLiteral(":/Images/Icons/announce.svg");
        else if (item.type == QStringLiteral("friend") || item.type == QStringLiteral("friend_accepted"))
            iconPath = QStringLiteral(":/Images/Icons/topbar_notify.svg");
        auto *typeIcon = new QLabel(row);
        typeIcon->setObjectName(QStringLiteral("forumUnreadDot"));
        typeIcon->setFixedSize(32, 32);
        typeIcon->setAlignment(Qt::AlignCenter);
        typeIcon->setPixmap(ResourceManager::instance()->loadThemedIcon(iconPath).pixmap(17, 17));
        rowLayout->addWidget(typeIcon, 0, Qt::AlignTop);

        auto *textColumn = new QVBoxLayout();
        textColumn->setSpacing(3);

        QString actors = item.actorNames.isEmpty() ? tr("系统")
                                                   : item.actorNames.mid(0, 2).join(QStringLiteral("、"));
        if (item.actorNames.size() > 2)
            actors = tr("%1 等 %2 人").arg(actors, QString::number(item.actorNames.size()));

        QString action;
        if (item.type == QStringLiteral("friend"))
            action = tr("申请加你为好友");
        else if (item.type == QStringLiteral("friend_accepted"))
            action = tr("同意了你的好友申请");
        else if (item.type == QStringLiteral("mention"))
            action = tr("在帖子里提到了你");
        else if (item.type == QStringLiteral("system"))
            action = tr("系统公告");
        else if (item.targetKind == QStringLiteral("post"))
            action = tr("回复了你的楼层");
        else
            action = tr("回复了你的主题");

        auto *title = new ForumKit::ElidedLabel(row);
        title->setObjectName(QStringLiteral("forumRowTitle"));
        title->setFullText(QStringLiteral("%1 %2").arg(actors, action));
        textColumn->addWidget(title);

        const QString quote = item.type == QStringLiteral("friend") || item.type == QStringLiteral("friend_accepted")
                                  ? QString()
                                  : QStringLiteral("「%1」").arg(item.threadTitle);
        if (!quote.isEmpty())
        {
            auto *quoteLabel = new ForumKit::ElidedLabel(row);
            quoteLabel->setObjectName(QStringLiteral("forumMuted"));
            quoteLabel->setFullText(quote);
            textColumn->addWidget(quoteLabel);
        }
        if (!item.excerpt.isEmpty())
        {
            auto *excerptLabel = new ForumKit::ElidedLabel(row);
            excerptLabel->setObjectName(QStringLiteral("forumMuted"));
            excerptLabel->setFullText(item.excerpt.simplified());
            textColumn->addWidget(excerptLabel);
        }
        auto *meta = new QLabel(ForumKit::relativeTime(item.createdAt), row);
        meta->setObjectName(QStringLiteral("forumMuted"));
        meta->setStyleSheet(StyleKit::resolve(QStringLiteral("font-size: 11px; color: @TEXT_TERTIARY@;")));
        textColumn->addWidget(meta);
        rowLayout->addLayout(textColumn, 1);

        // 好友申请：直接同意 / 拒绝（和通知页、网页版一致）
        if (item.requestId > 0)
        {
            const int requestId = item.requestId;
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
        }
        else
        {
            const QVector<int> ids = item.ids;
            const int threadId = item.threadId;
            row->onClick = [this, ids, threadId]() {
                ForumContentService::instance()->markNotificationsRead(ids, [this](int unread) {
                    m_noticeUnread = unread;
                    renderSide();
                    if (m_noticeCard && m_noticeCard->isVisible())
                        openNotificationPane();
                });
                if (threadId > 0)
                    emit threadOpenRequested(threadId);
            };
        }

        m_noticeListLayout->addWidget(row);
    }

    // 我发出去、还在等对方同意的申请：放在最下面，可以撤回
    if (!m_snapshot.outgoing.isEmpty())
    {
        auto *heading = new QLabel(tr("我发出的好友申请"), this);
        heading->setObjectName(QStringLiteral("forumSideGroupTitle"));
        m_noticeListLayout->addSpacing(6);
        m_noticeListLayout->addWidget(heading);
        for (const ForumFriendRequest &request : m_snapshot.outgoing)
        {
            auto *row = new QFrame(this);
            row->setObjectName(QStringLiteral("forumSideRow"));
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(12, 10, 12, 10);
            rowLayout->setSpacing(10);
            rowLayout->addWidget(ForumKit::makeAvatar(row, request.user.avatar, request.user.displayName, 32));
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
            m_noticeListLayout->addWidget(row);
        }
    }

    m_noticeListLayout->addStretch(1);
    m_noticeScroll->verticalScrollBar()->setValue(scrollValue);
}

void ForumFriendsPage::markAllNotificationsRead()
{
    if (m_noticeMarkAllButton)
        m_noticeMarkAllButton->setEnabled(false);
    ForumContentService::instance()->markNotificationsRead(
        {},
        [this](int unread) {
            m_noticeUnread = unread;
            NotificationManager::showSuccess(this, tr("已全部标为已读"));
            m_notificationsLoaded = false;
            renderSide();
            if (m_noticeCard && m_noticeCard->isVisible())
                openNotificationPane();
        },
        [this](const QString &message, int) {
            if (m_noticeMarkAllButton)
                m_noticeMarkAllButton->setEnabled(true);
            NotificationManager::showError(this, message);
        });
}

// ────────────────────────────────────────────────────────────── 添加好友弹窗

void ForumFriendsPage::openAddFriendDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("添加好友"));
    dialog.setObjectName(QStringLiteral("forumAddFriendDialog"));
    dialog.setMinimumWidth(420);
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    auto *hint = ForumKit::makeMuted(&dialog, tr("按用户名或昵称搜索玩家，对方同意后就能聊天。"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *searchEdit = new QLineEdit(&dialog);
    searchEdit->setPlaceholderText(tr("输入用户名或昵称搜索"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setFixedHeight(32);
    layout->addWidget(searchEdit);

    auto *resultScroll = new QScrollArea(&dialog);
    resultScroll->setWidgetResizable(true);
    resultScroll->setFrameShape(QFrame::NoFrame);
    resultScroll->setMinimumHeight(220);
    auto *resultHost = new QWidget(resultScroll);
    auto *resultLayout = new QVBoxLayout(resultHost);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(4);
    auto *placeholder = ForumKit::makeMuted(resultHost, tr("搜到玩家后点「加好友」，对方同意就能聊天了"));
    placeholder->setAlignment(Qt::AlignCenter);
    resultLayout->addWidget(placeholder);
    resultLayout->addStretch(1);
    resultScroll->setWidget(resultHost);
    layout->addWidget(resultScroll, 1);

    auto *footer = new QHBoxLayout();
    footer->addStretch(1);
    auto *closeButton = new QPushButton(tr("关闭"), &dialog);
    closeButton->setObjectName(QStringLiteral("forumGhostButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setFixedHeight(28);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    footer->addWidget(closeButton);
    layout->addLayout(footer);

    auto runQuery = [this, resultLayout, resultHost](const QString &keyword) {
        ForumKit::clearLayout(resultLayout);
        const QString query = keyword.trimmed();
        if (query.isEmpty())
        {
            auto *tip = ForumKit::makeMuted(resultHost, tr("搜到玩家后点「加好友」，对方同意就能聊天了"));
            tip->setAlignment(Qt::AlignCenter);
            resultLayout->addWidget(tip);
            resultLayout->addStretch(1);
            return;
        }
        auto *loading = ForumKit::makeMuted(resultHost, tr("正在搜索…"));
        loading->setAlignment(Qt::AlignCenter);
        resultLayout->addWidget(loading);
        resultLayout->addStretch(1);

        ForumChatService::instance()->searchUsers(
            query,
            [this, resultLayout, resultHost, query](const QVector<ForumFriendInfo> &users) {
                ForumKit::clearLayout(resultLayout);
                if (users.isEmpty())
                {
                    auto *empty = ForumKit::makeMuted(resultHost, tr("没有找到这个用户"));
                    empty->setAlignment(Qt::AlignCenter);
                    resultLayout->addWidget(empty);
                    resultLayout->addStretch(1);
                    return;
                }
                for (const ForumFriendInfo &user : users)
                {
                    auto *row = new QFrame(resultHost);
                    row->setObjectName(QStringLiteral("forumSideRow"));
                    auto *rowLayout = new QHBoxLayout(row);
                    rowLayout->setContentsMargins(10, 8, 10, 8);
                    rowLayout->setSpacing(10);
                    rowLayout->addWidget(
                        ForumKit::makeAvatar(row, user.avatar, user.displayName, 32));
                    auto *textColumn = new QVBoxLayout();
                    textColumn->setSpacing(2);
                    auto *name = new ForumKit::ElidedLabel(row);
                    name->setObjectName(QStringLiteral("forumRowTitle"));
                    name->setFullText(user.displayName);
                    textColumn->addWidget(name);
                    textColumn->addWidget(ForumKit::makeMuted(row, QStringLiteral("@%1").arg(user.username)));
                    rowLayout->addLayout(textColumn, 1);

                    // 按钮文案按当前关系决定：已是好友 / 同意申请 / 撤回申请 / 加好友
                    bool isFriend = false;
                    for (const ForumFriendInfo &item : m_snapshot.friends)
                    {
                        if (item.id == user.id)
                        {
                            isFriend = true;
                            break;
                        }
                    }
                    int incomingRequestId = 0;
                    for (const ForumFriendRequest &request : m_snapshot.incoming)
                    {
                        if (request.user.id == user.id)
                        {
                            incomingRequestId = request.id;
                            break;
                        }
                    }
                    int outgoingRequestId = 0;
                    for (const ForumFriendRequest &request : m_snapshot.outgoing)
                    {
                        if (request.user.id == user.id)
                        {
                            outgoingRequestId = request.id;
                            break;
                        }
                    }

                    auto *actionButton = new QPushButton(row);
                    actionButton->setCursor(Qt::PointingHandCursor);
                    actionButton->setFixedHeight(26);
                    const int userId = user.id;
                    if (isFriend)
                    {
                        actionButton->setObjectName(QStringLiteral("forumGhostButton"));
                        actionButton->setText(tr("已是好友"));
                        actionButton->setEnabled(false);
                    }
                    else if (incomingRequestId > 0)
                    {
                        actionButton->setObjectName(QStringLiteral("forumPrimaryButton"));
                        actionButton->setText(tr("同意申请"));
                        connect(actionButton, &QPushButton::clicked, this, [this, incomingRequestId, actionButton]() {
                            actionButton->setEnabled(false);
                            ForumChatService::instance()->acceptFriendRequest(
                                incomingRequestId,
                                [this]() {
                                    NotificationManager::showSuccess(this, tr("已同意好友申请"));
                                    reload();
                                },
                                [this, actionButton](const QString &message, int) {
                                    actionButton->setEnabled(true);
                                    NotificationManager::showError(this, message);
                                });
                        });
                    }
                    else if (outgoingRequestId > 0)
                    {
                        actionButton->setObjectName(QStringLiteral("forumGhostButton"));
                        actionButton->setText(tr("撤回申请"));
                        connect(actionButton, &QPushButton::clicked, this,
                                [this, outgoingRequestId, actionButton]() {
                                    actionButton->setEnabled(false);
                                    ForumChatService::instance()->withdrawFriendRequest(
                                        outgoingRequestId,
                                        [this]() { reload(); },
                                        [this, actionButton](const QString &message, int) {
                                            actionButton->setEnabled(true);
                                            NotificationManager::showError(this, message);
                                        });
                                });
                    }
                    else
                    {
                        actionButton->setObjectName(QStringLiteral("forumPrimaryButton"));
                        actionButton->setText(tr("加好友"));
                        connect(actionButton, &QPushButton::clicked, this, [this, userId, actionButton]() {
                            actionButton->setEnabled(false);
                            actionButton->setText(tr("已申请"));
                            ForumChatService::instance()->sendFriendRequest(
                                userId,
                                [this](const QString &status) {
                                    NotificationManager::showSuccess(
                                        this, status == QStringLiteral("friends")
                                                  ? tr("对方也申请了你，已经成为好友")
                                                  : tr("申请已发出，等对方同意"));
                                    reload();
                                },
                                [this, actionButton](const QString &message, int) {
                                    actionButton->setEnabled(true);
                                    actionButton->setText(tr("加好友"));
                                    NotificationManager::showError(this, message);
                                });
                        });
                    }
                    rowLayout->addWidget(actionButton);
                    resultLayout->addWidget(row);
                }
                resultLayout->addStretch(1);
            },
            [resultLayout, resultHost](const QString &message, int) {
                ForumKit::clearLayout(resultLayout);
                auto *error = ForumKit::makeMuted(resultHost, message);
                error->setAlignment(Qt::AlignCenter);
                resultLayout->addWidget(error);
                resultLayout->addStretch(1);
            });
    };

    // 输入停顿 320ms 再搜，回车立刻搜
    auto *searchTimer = new QTimer(&dialog);
    searchTimer->setSingleShot(true);
    searchTimer->setInterval(320);
    connect(searchTimer, &QTimer::timeout, &dialog, [searchEdit, &runQuery]() { runQuery(searchEdit->text()); });
    connect(searchEdit, &QLineEdit::textChanged, searchTimer, qOverload<>(&QTimer::start));
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, [searchTimer, searchEdit, &runQuery]() {
        searchTimer->stop();
        runQuery(searchEdit->text());
    });

    searchEdit->setFocus();
    dialog.exec();
    reload();
}

// ────────────────────────────────────────────────────────────── 右栏聊天

void ForumFriendsPage::openConversation(const ForumFriendInfo &peer)
{
    // 从「系统通知」面板点回来：先把右栏切回聊天区
    if (m_noticeCard && m_noticeCard->isVisible())
        m_noticeCard->hide();
    m_chatCard->show();
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
    m_chatName->setText(peer.isValid() ? conversationTitle(false, peer.id, peer.displayName)
                                       : tr("…"));
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
    renderSide(); // 高亮当前会话行

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
            renderSide();
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
    if (m_noticeCard && m_noticeCard->isVisible())
        m_noticeCard->hide();
    m_chatCard->show();
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
    m_chatName->setText(conversationTitle(true, group.id, group.name));
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
    renderSide(); // 高亮当前会话行

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
            renderSide();
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
    m_chatName->setText(conversationTitle(false, m_peer.id, m_peer.displayName));
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
    // 直传卡片上的按钮每次重画都要重新登记（控件是新建的）
    m_p2pSendButtons.clear();
    m_p2pRecvButtons.clear();

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
                QStringLiteral("color: @TEXT_SECONDARY@; font-size: %1px; background: transparent;"))
                                      .arg(ChatPrefs::instance()->fontPx(11)));
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
            // 字号跟随「聊天设置」里的档位（其他颜色 / 左边线仍走全局样式）
            quote->setStyleSheet(QStringLiteral("QLabel#forumChatQuote { font-size: %1px; }")
                                     .arg(ChatPrefs::instance()->fontPx(12)));
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
            quoteFont.setPixelSize(ChatPrefs::instance()->fontPx(12));
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
                    StyleKit::resolve(QStringLiteral("color: @TEXT_PRIMARY@; font-size: %1px;"))
                        .arg(ChatPrefs::instance()->fontPx(13)));
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
            nameLabel->setObjectName(QStringLiteral("forumChatFileName"));
            nameLabel->setFullText(fileNameText);
            nameLabel->setStyleSheet(
                StyleKit::resolve(QStringLiteral("#forumChatFileName { color: @TEXT_PRIMARY@;"
                                                 " font-size: %1px; }"))
                    .arg(ChatPrefs::instance()->fontPx(13)));
            // 文件名先按自身宽度占位，只有真的放不下时才省略
            QFont nameFont = nameLabel->font();
            nameFont.setPixelSize(ChatPrefs::instance()->fontPx(13));
            nameLabel->setMinimumWidth(qMin(210,
                                            QFontMetrics(nameFont).horizontalAdvance(fileNameText) + 10));
            fileColumn->addWidget(nameLabel);
            const QString sizeText =
                message.fileSize > 0 ? DownloadUtils::formatSize(message.fileSize) : tr("未知大小");
            auto *sizeLabel = new QLabel(sizeText, fileCard);
            sizeLabel->setObjectName(QStringLiteral("forumChatFileSize"));
            sizeLabel->setStyleSheet(
                StyleKit::resolve(QStringLiteral("#forumChatFileSize { color: @TEXT_TERTIARY@;"
                                                 " font-size: %1px; }"))
                    .arg(ChatPrefs::instance()->fontPx(11)));
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
                    StyleKit::resolve(QStringLiteral("color: @TEXT_PRIMARY@; font-size: %1px;"))
                        .arg(ChatPrefs::instance()->fontPx(13)));
                caption->setTextInteractionFlags(Qt::TextSelectableByMouse);
                bubbleColumn->addWidget(caption);
            }
        }
        else if (message.isP2PControl())
        {
            // 直传控制消息：一条居中的灰字说明（「对方希望改用普通方式…」）
            const QString text = message.mine
                ? tr("已请求对方改用普通方式发送文件")
                : tr("对方接收不了直传，希望你改用普通方式发送");
            QLabel *label = new QLabel(text, row);
            label->setAlignment(Qt::AlignHCenter);
            label->setWordWrap(true);
            label->setStyleSheet(
                StyleKit::resolve(QStringLiteral("color: @TEXT_TERTIARY@; font-size: %1px;"))
                    .arg(ChatPrefs::instance()->fontPx(11)));
            bubbleColumn->addWidget(label);
        }
        else if (message.isP2POffer())
        {
            // 直传邀约卡片：文件卡样式，按钮按当前直传状态显示
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

            auto *icon = new QLabel(QStringLiteral("⚡"), fileCard);
            icon->setStyleSheet(StyleKit::resolve(QStringLiteral("font-size: 22px; background: transparent;")));
            fileLayout->addWidget(icon);

            auto *fileColumn = new QVBoxLayout();
            fileColumn->setSpacing(2);
            const QString fileNameText = message.p2p.name.isEmpty() ? tr("文件") : message.p2p.name;
            auto *nameLabel = new ForumKit::ElidedLabel(fileCard);
            nameLabel->setObjectName(QStringLiteral("forumChatFileName"));
            nameLabel->setFullText(fileNameText);
            nameLabel->setStyleSheet(
                StyleKit::resolve(QStringLiteral("#forumChatFileName { color: @TEXT_PRIMARY@;"
                                                 " font-size: %1px; }"))
                    .arg(ChatPrefs::instance()->fontPx(13)));
            QFont nameFont = nameLabel->font();
            nameFont.setPixelSize(ChatPrefs::instance()->fontPx(13));
            nameLabel->setMinimumWidth(qMin(210,
                                            QFontMetrics(nameFont).horizontalAdvance(fileNameText) + 10));
            fileColumn->addWidget(nameLabel);
            const QString sizeText =
                message.p2p.size > 0 ? DownloadUtils::formatSize(message.p2p.size) : tr("未知大小");
            auto *sizeLabel = new QLabel(sizeText, fileCard);
            sizeLabel->setObjectName(QStringLiteral("forumChatFileSize"));
            sizeLabel->setStyleSheet(
                StyleKit::resolve(QStringLiteral("#forumChatFileSize { color: @TEXT_TERTIARY@;"
                                                 " font-size: %1px; }"))
                    .arg(ChatPrefs::instance()->fontPx(11)));
            fileColumn->addWidget(sizeLabel);
            fileLayout->addLayout(fileColumn, 1);

            auto *stateButton = new QPushButton(fileCard);
            stateButton->setObjectName(QStringLiteral("forumGhostButton"));
            stateButton->setCursor(Qt::PointingHandCursor);
            stateButton->setFixedHeight(26);
            const bool expired = message.p2p.isExpired();
            const bool active = ChatP2P::instance()->activeShareTid() == message.p2p.tid
                                && !ChatP2P::instance()->shareCompleted();
            const bool receiving = m_fileDownloads.contains(messageId)
                                   || ChatP2P::instance()->isReceiving(message.p2p.tid);
            if (message.mine)
            {
                // 自己发的邀约：看本机这份直传到哪一步了
                stateButton->setEnabled(false);
                if (m_p2pSentMessages.contains(messageId))
                    stateButton->setText(tr("已发送"));
                else if (expired || !active)
                    stateButton->setText(tr("已过期"));
                else if (m_p2pLastPercent > 0)
                    stateButton->setText(tr("传输中 %1%").arg(m_p2pLastPercent));
                else
                    stateButton->setText(tr("等待对方接收"));
                m_p2pSendButtons.insert(messageId, stateButton);
            }
            else
            {
                // 对方发来的邀约：点「接收」选个保存位置就连过去拉文件
                stateButton->setEnabled(!expired && !receiving);
                if (expired)
                    stateButton->setText(tr("已过期"));
                else if (m_p2pSavedMessages.contains(messageId))
                {
                    stateButton->setEnabled(false);
                    stateButton->setText(tr("已保存"));
                }
                else if (receiving)
                    stateButton->setText(tr("接收中…"));
                else
                {
                    stateButton->setText(tr("接收"));
                    connect(stateButton, &QPushButton::clicked, this,
                            [this, messageId, stateButton] { receiveP2PFile(messageId); });
                }
                m_p2pRecvButtons.insert(messageId, stateButton);
            }
            fileLayout->addWidget(stateButton);
            bubbleColumn->addWidget(fileCard);
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
            body->setObjectName(QStringLiteral("forumChatBodyText"));
            body->setWordWrap(true);
            // 字号跟随「聊天设置」档位：气泡里的正文是聊天最主要的阅读内容。
            // 这里必须带 #objectName 选择器，否则会被全局气泡样式的 13px 压回去。
            body->setStyleSheet(QStringLiteral("QLabel#forumChatBodyText { font-size: %1px; }")
                                    .arg(ChatPrefs::instance()->fontPx(13)));
            body->setTextInteractionFlags(Qt::TextSelectableByMouse);
            // 中文长句在布局里会被压成窄窄一条：按正文自然宽度给个下限，
            // 同时跟随窗口宽度收窄，避免窄屏下气泡顶出可视区
            const int viewportWidth = m_msgsScroll->viewport()->width();
            const int maxTextWidth = qBound(160, viewportWidth - 110, 372);
            QFont bodyFont = body->font();
            bodyFont.setPixelSize(ChatPrefs::instance()->fontPx(13));
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
        // P2P 控制消息（如「请对方改用普通发送」）在进列表时就地处理
        if (message.kind == QStringLiteral("p2p"))
            handleP2PMessage(message);
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
    m_p2pRecvMessageIds.clear();
    // 还没人接收的直传邀约随会话关闭一起取消
    if (!m_p2pOffer.tid.isEmpty())
    {
        resetP2pSendState(true);
        restoreComposerAfterSend();
        setStatus(QString());
    }
    clearReplyTarget();
    if (m_emojiPopup)
        m_emojiPopup->hide();
    m_chatMain->hide();
    m_chatEmpty->show();
    m_chatCard->show();
    if (m_noticeCard)
        m_noticeCard->hide();
    renderSide();
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
        renderSide(); // 左栏预览从「草稿」变回最后一条消息
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
    // 跟着内容长高：默认两行高（留点呼吸空间），最多 4 行，再多就在框内滚动。
    // 高度按整行取整（含 CSS 的上下内边距），避免文字被卡在半个行高上像被切断。
    const int lineHeight = qMax(1, m_chatInput->fontMetrics().lineSpacing() + 1);
    const int docHeight = qMax(1, qRound(m_chatInput->document()->size().height()));
    const int lines = qBound(2, (docHeight + lineHeight / 2) / lineHeight, 4);
    const int target = lineHeight * lines + 12; // 12 = QSS padding 上下各 6
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
    if (message.isP2POffer())
        return tr("[文件] %1").arg(message.p2p.name);
    if (message.isP2PControl())
        return tr("[文件]");
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
    QAction *retryNormalAction = nullptr;
    QAction *withdrawAction = nullptr;
    if (!message.isWithdrawn())
    {
        replyAction = menu.addAction(tr("回复"));
        // P2P 消息的 body 装的是直传邀约数据，不作为文字复制 / 转发
        const bool textLike = !message.body.isEmpty() && !message.isP2POffer() && !message.isP2PControl();
        if (textLike)
            copyAction = menu.addAction(tr("复制文字"));
        // 文字和图片可以转发；文件要先占用下载流量，先不做转发
        if (message.isImage() || textLike)
            forwardAction = menu.addAction(tr("转发…"));
        const bool bookmarked =
            ChatBookmarkStore::instance()->contains(activeConversationKey(), message.id);
        bookmarkAction = menu.addAction(bookmarked ? tr("取消收藏") : tr("收藏消息"));
        // 直传邀约还热着但连不上对方时，可以请对方改走服务器通道重发
        if (message.isP2POffer() && !message.mine && !groupOpen
            && message.p2p.size > 0 && message.p2p.size <= 5 * 1024 * 1024
            && QDateTime::currentSecsSinceEpoch() - message.createdAt <= 10 * 60)
            retryNormalAction = menu.addAction(tr("请对方改用普通发送"));
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
    else if (chosen == retryNormalAction)
    {
        // 请对方改用普通方式重发：对方收到后撤掉直传邀约、走服务器通道
        ForumChatService::instance()->sendP2PControl(
            m_peer.id, message.p2p.tid, QStringLiteral("retry-normal"),
            [this](const ForumChatMessage &sent) {
                if (sent.isValid())
                    appendMessages(QVector<ForumChatMessage>{sent});
                scrollToBottom();
            },
            [this](const QString &msg, int) {
                NotificationManager::showError(this, msg);
            });
    }
    else if (chosen == saveImageAction)
    {
        saveMessageImage(message);
    }
    else if (chosen == withdrawAction)
    {
        // 撤回直传邀约等于取消这次直传：先停掉本机监听再撤
        if (message.isP2POffer() && message.p2p.tid == m_p2pOffer.tid)
        {
            resetP2pSendState(true);
            restoreComposerAfterSend();
            setStatus(QString());
        }
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
    // 私聊且开了「优先直传」就先试 P2P；没发出去（起不了监听 / 服务端不支持）
    // 或已有一份直传在等对方时，回落到原来的服务器上传通道
    if (!groupOpen && ChatPrefs::instance()->p2pDirectSend()
        && ChatP2P::instance()->activeShareTid().isEmpty())
    {
        if (trySendFileP2P(filePath))
            return;
    }
    uploadFileViaServer(filePath);
}

void ForumFriendsPage::uploadFileViaServer(const QString &filePath)
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
        restoreComposerAfterSend();
        setStatus(QString());
        if (message.isValid())
            appendMessages(QVector<ForumChatMessage>{message});
        scrollToBottom();
        clearReplyTarget();
    };
    auto onFileFailed = [this](const QString &msg, int) {
        restoreComposerAfterSend();
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

bool ForumFriendsPage::trySendFileP2P(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.isFile() || info.size() <= 0)
        return false; // 让服务器通道给出「打不开文件」的提示

    m_p2pOffer = ChatP2P::instance()->startShare(filePath);
    if (!m_p2pOffer.isOffer())
        return false; // 起不了监听（端口等）→ 回落普通上传

    m_p2pFilePath = filePath;
    m_p2pFileSize = m_p2pOffer.size;
    m_p2pMessageId = 0;
    m_p2pLastPercent = 0;
    m_sending = true;
    m_sendButton->setEnabled(false);
    m_imageButton->setEnabled(false);
    m_fileButton->setEnabled(false);
    setStatus(tr("正在创建直传邀请…"));

    const int peerId = m_peer.id;
    const int replyTo = m_replyTo;
    ForumChatService::instance()->sendP2POffer(
        peerId, m_p2pOffer, replyTo,
        [this](const ForumChatMessage &message) {
            // 服务端放行了 p2p 类型：把邀约挂起来等对方接收
            m_p2pMessageId = message.id;
            if (message.isValid())
                appendMessages(QVector<ForumChatMessage>{message});
            scrollToBottom();
            clearReplyTarget();
            if (m_p2pFileSize <= 5 * 1024 * 1024)
            {
                // 小文件等 25 秒还没人连就当对方不在，自动改走服务器发送，用户无感
                m_p2pFallback = true;
                m_p2pFallbackTimer.start(25 * 1000);
                setStatus(tr("等待对方接收（P2P 直传）…"));
            }
            else
            {
                // 大文件只能直传：先放开发送框，后台继续等对方上线接收
                restoreComposerAfterSend();
                setStatus(tr("等待对方上线接收，直传邀请保留 10 分钟…"));
            }
        },
        [this, filePath](const QString &msg, int) {
            // 服务端还没放行 p2p 类型（或其他失败）：静默回落普通上传
            const bool hadOffer = m_p2pOffer.isOffer();
            resetP2pSendState(true);
            restoreComposerAfterSend();
            if (hadOffer)
            {
                setStatus(tr("直传邀请发不出去，改用服务器发送…"));
                uploadFileViaServer(filePath);
            }
            else
            {
                setStatus(QString());
                NotificationManager::showError(this, msg);
            }
        });
    return true;
}

void ForumFriendsPage::resetP2pSendState(bool stopShare)
{
    m_p2pFallbackTimer.stop();
    m_p2pFallback = false;
    // 先清自己的状态再停监听：stopShare 触发的 shareFinished 信号里
    // 找不到进行中的邀约，就不会二次收尾
    const ChatP2POffer offer = m_p2pOffer;
    m_p2pOffer = {};
    m_p2pMessageId = 0;
    m_p2pFileSize = 0;
    m_p2pLastPercent = 0;
    m_p2pFilePath.clear();
    if (stopShare && !offer.tid.isEmpty())
        ChatP2P::instance()->stopShare();
}

void ForumFriendsPage::restoreComposerAfterSend()
{
    m_sending = false;
    m_sendButton->setEnabled(true);
    m_imageButton->setEnabled(true);
    m_fileButton->setEnabled(true);
}

void ForumFriendsPage::onP2pFallbackTimeout()
{
    if (!m_p2pFallback || m_p2pOffer.tid.isEmpty())
        return;
    const QString path = m_p2pFilePath;
    resetP2pSendState(true);
    withdrawP2POfferMessage(); // 文件改走服务器，聊天里那条邀约卡片就撤掉
    setStatus(tr("直传未建立，改用服务器发送…"));
    uploadFileViaServer(path);
}

void ForumFriendsPage::onP2pShareProgress(const QString &tid, double percent, qint64 bytesSent,
                                          qint64, qint64)
{
    if (tid != m_p2pOffer.tid)
        return;
    if (m_p2pFallback)
    {
        // 对方已经连上来开传了：撤掉 25 秒的回退
        m_p2pFallbackTimer.stop();
        m_p2pFallback = false;
    }
    m_p2pLastPercent = qRound(percent);
    Q_UNUSED(bytesSent);
    setStatus(tr("正在直传… %1%").arg(qRound(percent)));
    if (QPushButton *button = m_p2pSendButtons.value(m_p2pMessageId))
        button->setText(tr("传输中 %1%").arg(qRound(percent)));
}

void ForumFriendsPage::onP2pShareFinished(const QString &tid, bool success, const QString &message)
{
    if (tid != m_p2pOffer.tid)
        return; // 不是当前挂着的直传（旧邀约到点自然清理）
    if (success)
    {
        if (m_p2pMessageId > 0)
            m_p2pSentMessages.insert(m_p2pMessageId);
        resetP2pSendState(false); // 监听保留到到期，对方落盘失败还能重连重试
        restoreComposerAfterSend();
        setStatus(QString());
        NotificationManager::showSuccess(this, tr("文件已通过直传发给对方"));
        renderMessages();
        return;
    }
    // 邀约到点对方也没来：小文件兜底走服务器，大文件只能提醒
    const QString path = m_p2pFilePath;
    const qint64 size = m_p2pFileSize;
    resetP2pSendState(false);
    restoreComposerAfterSend();
    setStatus(QString());
    if (size > 0 && size <= 5 * 1024 * 1024 && QFileInfo(path).isFile())
        uploadFileViaServer(path);
    else
        NotificationManager::showError(this, message);
}

void ForumFriendsPage::handleP2PMessage(const ForumChatMessage &message)
{
    if (message.mine || !message.isP2PControl())
        return;
    if (message.p2p.act != QStringLiteral("retry-normal"))
        return;
    if (message.p2p.tid != m_p2pOffer.tid)
        return; // 邀约已经不在（自动回退过了 / 会话重开），忽略
    // 对方连不上我的直传，请我改用普通方式重发
    const QString path = m_p2pFilePath;
    const qint64 size = m_p2pFileSize;
    const bool small = size > 0 && size <= 5 * 1024 * 1024;
    resetP2pSendState(true);
    if (small && QFileInfo(path).isFile())
    {
        withdrawP2POfferMessage();
        NotificationManager::showInfo(this, tr("对方接收不了直传，改用服务器重发文件…"));
        uploadFileViaServer(path);
    }
    else
    {
        restoreComposerAfterSend();
        setStatus(QString());
        NotificationManager::showError(
            this, tr("对方接收不了直传，超过 5 MB 的文件只能等双方都在线时再直传"));
    }
}

void ForumFriendsPage::withdrawP2POfferMessage()
{
    if (m_p2pMessageId <= 0)
        return;
    ForumChatService::instance()->withdrawMessage(m_p2pMessageId, nullptr, nullptr);
    // 本地立刻变成撤回占位，不等轮询
    for (ForumChatMessage &item : m_messages)
    {
        if (item.id == m_p2pMessageId)
        {
            item.kind = QStringLiteral("withdrawn");
            item.p2p = {};
            break;
        }
    }
    renderMessages();
}

void ForumFriendsPage::receiveP2PFile(int messageId)
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
    if (!found || !found->isP2POffer() || found->mine || m_fileDownloads.contains(messageId))
        return;
    const ForumChatMessage message = *found;
    if (message.p2p.isExpired())
        return; // 按钮已置灰，正常点不到

    // 先问保存到哪，确认了才去连对方
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString defaultName = message.p2p.name;
    defaultName.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString defaultPath = documents.isEmpty()
                                    ? defaultName
                                    : documents + QStringLiteral("/") + defaultName;
    const QString savePath = QFileDialog::getSaveFileName(this, tr("保存文件"), defaultPath);
    if (savePath.isEmpty())
        return;

    QPointer<QPushButton> button(m_p2pRecvButtons.value(messageId));
    m_fileDownloads.insert(messageId);
    if (button)
    {
        button->setEnabled(false);
        button->setText(tr("连接对方…"));
    }
    setStatus(tr("正在通过直传接收 %1…").arg(message.p2p.name));
    const QString requestId = ChatP2P::instance()->requestFile(message.p2p, savePath);
    if (requestId.isEmpty())
    {
        m_fileDownloads.remove(messageId);
        if (button)
        {
            button->setEnabled(true);
            button->setText(tr("接收"));
        }
        setStatus(QString());
        NotificationManager::showError(this, tr("暂时接收不了这份文件，稍后再试"));
        return;
    }
    m_p2pRecvMessageIds.insert(requestId, messageId);
}

void ForumFriendsPage::onP2pReceiveFinished(const QString &requestId, bool success,
                                            const QString &savePath, const QString &message)
{
    const int messageId = m_p2pRecvMessageIds.take(requestId);
    if (messageId <= 0)
        return;
    m_fileDownloads.remove(messageId);
    if (success)
        m_p2pSavedMessages.insert(messageId);
    setStatus(QString());
    if (success)
        NotificationManager::showSuccess(this, tr("文件已保存到 %1").arg(savePath));
    else
        NotificationManager::showError(this, message);
    renderMessages(); // 按钮按最新状态重画（已保存 / 接收重试）
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
