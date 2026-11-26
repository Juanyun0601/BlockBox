/**
 * @file   ForumFriendsPage.h
 * @brief  论坛好友与私信页 —— 与论坛网页版 friends.html 同款布局
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 左栏只有「聊天」标题、搜索框、会话列表和一张菜单（创建群聊 / 添加好友 / 聊天收藏 / 刷新）；
 * 好友和群聊混排成一份会话列表，最上面固定一条「系统通知」。
 * 系统通知复用站内通知（和通知页、网页版同一份数据），好友申请在右栏面板里直接同意 / 拒绝。
 * 页面可见时每 4 秒轮询一次 /api/messages/poll，刷新会话列表与聊天新消息。
 */
#pragma once

#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "utils/ForumChatService.h"
#include "utils/ForumContentService.h"

class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTextEdit;
class QVBoxLayout;

class ForumFriendsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumFriendsPage(QWidget *parent = nullptr);

    /** 进入页面：拉一次好友数据（未登录时显示登录引导） */
    void open();

    /** 直接打开和某人的会话（从用户主页的「发私信」进来），userId<=0 等价 open() */
    void openChat(int userId);

signals:
    /** 未登录点击「去登录」：跳到论坛账户页登录表单 */
    void loginRequested();

    /** 点击对方头像 / 主页按钮：打开TA的论坛主页 */
    void userOpenRequested(int userId);

    /** 系统通知里点开帖子类通知：在启动器内打开帖子详情 */
    void threadOpenRequested(int threadId);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void reload();
    void pollOnce();

    void renderSide();
    void renderFriendRows();

    /** 右栏切到「系统通知」面板：拉一页站内通知并渲染（好友申请可以直接处理） */
    void openNotificationPane();
    void renderNotificationPane();
    /** 重新拉一次未读通知数，顺带刷新左栏「系统通知」行 */
    void refreshNoticeBadge();
    /** 把通知全部标为已读，再刷新面板与角标 */
    void markAllNotificationsRead();
    /** 「添加好友」弹窗：按用户名 / 昵称搜索并加好友 */
    void openAddFriendDialog();

    void openConversation(const ForumFriendInfo &peer);
    void openGroupConversation(const ForumGroupInfo &group);
    void renderChatAvatar();
    void renderChatHead();
    void renderMessages();
    void appendMessages(const QVector<ForumChatMessage> &messages);
    void scrollToBottom();
    void closeConversation();
    void sendCurrentMessage();
    void loadOlderMessages();
    void refreshInputSize();
    QWidget *makeDateChip(qint64 seconds);

    // ── 群聊 ──
    void openCreateGroupDialog();
    void showGroupMembers();
    void inviteToCurrentGroup();
    void startGroupFromPeer();
    void leaveCurrentGroup();

    // ── 图片 / 文件 / 引用 / 撤回 / 表情 ──
    void setReplyTarget(int messageId, const QString &preview, bool mine,
                        const QString &peerName = QString());
    void clearReplyTarget();
    void showMessageMenu(int messageId, const QPoint &globalPos);
    void showImagePreview(const QString &imagePath);
    void pickAndSendImage();
    void sendImageFromPath(const QString &filePath);
    void pickAndSendFile();
    void sendFileFromPath(const QString &filePath);
    void downloadChatFile(int messageId);
    void toggleEmojiPopup();
    void buildEmojiPopup();
    QString messagePreview(const ForumChatMessage &message) const;

    // ── P2P 直传 ──
    /** 私聊发文件先试直传：起本机监听并把邀约发给对方；返回 false 表示没发出去（回落普通上传） */
    bool trySendFileP2P(const QString &filePath);
    /** 走原来的服务器上传通道发文件（sendFileFromPath 的老逻辑，直传兜底也用它） */
    void uploadFileViaServer(const QString &filePath);
    /** 清掉进行中的直传状态；stopShare 为真时连本机监听一起停 */
    void resetP2pSendState(bool stopShare);
    /** 直传状态收尾后把发送相关按钮恢复可用 */
    void restoreComposerAfterSend();
    /** 点直传邀约卡片上的「接收」：选保存位置后连对方拉文件 */
    void receiveP2PFile(int messageId);
    /** 收到一条 p2p 消息的钩子（对方发来的控制消息在这里处理） */
    void handleP2PMessage(const ForumChatMessage &message);
    /** 撤回自己发出去的直传邀约消息（回落普通上传时清掉聊天里的邀约卡片） */
    void withdrawP2POfferMessage();
    void onP2pFallbackTimeout();
    void onP2pShareProgress(const QString &tid, double percent, qint64 bytesSent,
                            qint64 bytesTotal, qint64 speed);
    void onP2pShareFinished(const QString &tid, bool success, const QString &message);
    void onP2pReceiveFinished(const QString &requestId, bool success, const QString &savePath,
                              const QString &message);

    void setStatus(const QString &text, bool error = false);
    QString peerStatusText(const ForumFriendInfo &peer) const;

    // ── 会话整理：置顶 / 免打扰 / 草稿 / 搜索 ──
    /** 会话行右键菜单：置顶 / 免打扰（好友与群聊共用） */
    void showConversationMenu(bool group, int id, const QPoint &globalPos);
    /** 当前打开的会话在 ChatUiStore 里的 key（没有会话时为空） */
    QString activeConversationKey() const;
    /** 把输入框里的文字存成当前会话的草稿（空内容会删掉草稿） */
    void saveActiveDraft();
    /** 打开会话时把草稿放回输入框 */
    void restoreDraft(const QString &key);
    /** 打字时延迟保存草稿，避免每个字都写一次文件 */
    void scheduleDraftSave();
    /** 会话搜索词变化后延迟刷新左栏 */
    void updateConversationFilter();
    /** 打开「聊天设置」居中弹窗（当前会话置顶 / 免打扰 + 发送方式 / 字号） */
    void openChatSettings();
    /** 会话在界面上的名字：有本机备注就用备注，否则用昵称 / 群名 */
    QString conversationTitle(bool group, int id, const QString &fallback) const;
    /** 按偏好刷新输入框占位文字与发送按钮提示（Enter 发送 / Ctrl+Enter 发送） */
    void updateSendHint();

    // ── 转发 / 收藏 / 图片 / 引用定位 ──
    /** 打开聊天收藏列表（左栏顶部星标按钮） */
    void openBookmarks();
    /** 按会话 key（f12 / g3）打开对应的私聊或群聊（从收藏跳回来用） */
    void openConversationByKey(const QString &key);
    /** 转发一条消息：选会话后文字直接发、图片取回原图再发 */
    void forwardMessage(const ForumChatMessage &message);
    /** 转发图片消息（内部：先取回原图，存到临时文件再重新上传） */
    void forwardImageMessage(const ForumChatMessage &message, bool toGroup, int targetId,
                             const QString &targetName);
    /** 图片消息：复制到剪贴板 */
    void copyMessageImage(const ForumChatMessage &message);
    /** 图片消息：另存为本地图片 */
    void saveMessageImage(const ForumChatMessage &message);
    /** 收藏 / 取消收藏一条消息（当前会话内） */
    void toggleMessageBookmark(const ForumChatMessage &message);
    /** 点引用条：滚到被引用的原消息并闪一下 */
    void scrollToMessage(int messageId);

    /** 在当前会话消息里找一条消息的发送者昵称（群聊引用条用；找不到返回空） */
    QString findMessageSenderName(int messageId) const;

    // ── 左栏（近似子导航面板：头部菜单 + 搜索框 + 系统通知 + 会话列表）──
    QLabel *m_statusLabel = nullptr;
    QLabel *m_sideGroupTitle = nullptr;   ///< 分组标题（会话 · N）
    QPushButton *m_menuButton = nullptr;  ///< ☰ 菜单：创建群聊 / 添加好友 / 聊天收藏 / 刷新
    QScrollArea *m_sideScroll = nullptr;
    QWidget *m_sideContainer = nullptr;
    QVBoxLayout *m_sideListLayout = nullptr;
    QWidget *m_convSearchRow = nullptr;    ///< 会话搜索框所在行
    QLineEdit *m_convSearchEdit = nullptr; ///< 按昵称 / 最后一条消息过滤会话列表

    // ── 右栏（聊天面板）──
    QWidget *m_chatCard = nullptr;
    QWidget *m_chatEmpty = nullptr;        ///< 未选会话时的占位（图标 + 说明）
    QWidget *m_chatMain = nullptr;
    QWidget *m_chatAvatarSlot = nullptr;   ///< 会话头像槽位（头像是异步加载的，按需重建）
    QLabel *m_chatName = nullptr;
    QLabel *m_chatStatus = nullptr;
    QPushButton *m_chatProfileButton = nullptr;  ///< 私聊：看对方主页
    QPushButton *m_chatGroupButton = nullptr;    ///< 私聊：一键拉群（带上对方建新群）
    QPushButton *m_chatSettingsButton = nullptr; ///< 聊天设置（居中弹窗）
    QPushButton *m_chatRemoveButton = nullptr;   ///< 私聊：删除好友
    QPushButton *m_chatMembersButton = nullptr;  ///< 群聊：查看成员
    QPushButton *m_chatInviteButton = nullptr;   ///< 群聊：邀请好友进群（官方群没有）
    QPushButton *m_chatLeaveButton = nullptr;    ///< 群聊：退出群聊（群主退出即解散）
    QPushButton *m_moreButton = nullptr;
    QScrollArea *m_msgsScroll = nullptr;
    QWidget *m_msgsContainer = nullptr;
    QVBoxLayout *m_msgsLayout = nullptr;
    QFrame *m_chatInputBox = nullptr;      ///< 输入坞（工具 + 输入框 + 发送）
    QTextEdit *m_chatInput = nullptr;
    QLabel *m_charCounter = nullptr;       ///< 字数统计（接近上限时出现）
    QPushButton *m_sendButton = nullptr;

    // ── 右栏（系统通知面板，复用站内通知）──
    QWidget *m_noticeCard = nullptr;
    QLabel *m_noticeHint = nullptr;        ///< 未读数说明
    QPushButton *m_noticeMarkAllButton = nullptr;
    QScrollArea *m_noticeScroll = nullptr;
    QWidget *m_noticeContainer = nullptr;
    QVBoxLayout *m_noticeListLayout = nullptr;
    QVector<ForumNotificationItem> m_notifications;
    int m_noticeUnread = 0;                ///< 未读通知数（左栏「系统通知」角标）
    bool m_notificationsLoaded = false;

    // ── 状态 ──
    ForumFriendSnapshot m_snapshot;
    ForumFriendInfo m_peer;              ///< 当前私聊会话的好友（与 m_group 二选一）
    ForumGroupInfo m_group;              ///< 当前群聊会话（isValid 时代表群聊已打开）
    QVector<ForumGroupMember> m_groupMembers; ///< 当前群的成员（打开会话时拉全）
    QVector<ForumChatMessage> m_messages; ///< 当前会话消息（按 id 升序）
    int m_lastMessageId = 0;             ///< 私聊已拿到的最大消息 id
    int m_groupLastMessageId = 0;        ///< 群聊已拿到的最大消息 id
    bool m_hasMore = false;              ///< 私聊还有更早的消息
    bool m_groupHasMore = false;         ///< 群聊还有更早的消息
    bool m_sending = false;
    bool m_pollBusy = false;
    QTimer m_pollTimer;
    QTimer m_convFilterTimer;               ///< 会话搜索防抖
    QTimer m_draftTimer;                    ///< 草稿保存防抖
    QString m_activeKey;                    ///< 当前会话的 ChatUiStore key（f12 / g3）
    int m_unreadAnchorId = 0;               ///< 打开会话时的第一条未读消息 id（0 = 没有未读）
    bool m_draftSuppressed = false;          ///< 正在程序化清空/恢复输入框，不记草稿

    // ── 图片 / 文件 / 引用 / 表情 ──
    QPushButton *m_emojiButton = nullptr;
    QPushButton *m_imageButton = nullptr;
    QPushButton *m_fileButton = nullptr;
    QSet<int> m_fileDownloads;           ///< 正在提取/下载中的文件消息 id（防重复点击）
    QHash<int, QPushButton *> m_p2pSendButtons; ///< 自己邀约卡片上的状态按钮（进度刷新用）
    QHash<int, QPushButton *> m_p2pRecvButtons; ///< 对方邀约卡片上的「接收」按钮
    QHash<QString, int> m_p2pRecvMessageIds;    ///< 直传请求 id（tid）→ 邀约消息 id
    QSet<int> m_p2pSentMessages;         ///< 已完成直传的邀约消息 id（渲染「已发送」）
    QSet<int> m_p2pSavedMessages;        ///< 已保存完成的对方邀约消息 id（渲染「已保存」）
    ChatP2POffer m_p2pOffer;             ///< 进行中的直传邀约（tid 非空 = 有直传在等对方）
    QString m_p2pFilePath;               ///< 正在直传的本地文件
    qint64 m_p2pFileSize = 0;
    int m_p2pMessageId = 0;              ///< 邀约消息 id（完成后标「已发送」用）
    int m_p2pLastPercent = 0;            ///< 直传最近一次进度（重渲染卡片时显示）
    bool m_p2pFallback = false;          ///< ≤5MB 的邀约等 25 秒没建立连接就自动改走服务器
    QTimer m_p2pFallbackTimer;
    QWidget *m_replyBar = nullptr;
    QLabel *m_replyLabel = nullptr;
    QPushButton *m_replyCancelButton = nullptr;
    int m_replyTo = 0;                   ///< 正在引用的消息 id（0 = 没有）
    QString m_replyPreview;
    bool m_replyMine = false;
    QWidget *m_emojiPopup = nullptr;     ///< 表情弹窗（懒创建，Qt::Popup）
    QHash<int, QWidget *> m_messageRows; ///< 消息 id → 这一条对应的行控件（引用跳转用）
};
