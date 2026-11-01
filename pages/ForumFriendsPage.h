/**
 * @file   ForumFriendsPage.h
 * @brief  论坛好友与私信页 —— 与论坛网页版 friends.html 同款布局
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 左栏三个页签：好友（带未读角标与最后一条私信）/ 申请（同意·拒绝·撤回）/
 * 添加好友（按用户名或昵称搜索）；右栏是当前会话的聊天面板。
 * 页面可见时每 4 秒轮询一次 /api/messages/poll，刷新会话列表与聊天新消息。
 */
#pragma once

#include <QTimer>
#include <QVector>
#include <QWidget>

#include "utils/ForumChatService.h"

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

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum Tab
    {
        FriendsTab,
        RequestsTab,
        AddTab,
    };

    void buildUi();
    void reload();
    void pollOnce();
    void setTab(Tab tab);

    void renderSide();
    void renderFriendRows();
    void renderRequestRows();
    void renderAddTab();

    void openConversation(const ForumFriendInfo &peer);
    void renderChatAvatar();
    void renderChatHead();
    void renderMessages();
    void appendMessages(const QVector<ForumChatMessage> &messages);
    void scrollToBottom();
    void closeConversation();
    void sendCurrentMessage();
    void loadOlderMessages();
    void detachSearchEdit();
    void refreshInputSize();
    QWidget *makeDateChip(qint64 seconds);

    // ── 图片 / 引用 / 撤回 / 表情 ──
    void setReplyTarget(int messageId, const QString &preview, bool mine);
    void clearReplyTarget();
    void showMessageMenu(int messageId, const QPoint &globalPos);
    void showImagePreview(const QString &imagePath);
    void pickAndSendImage();
    void sendImageFromPath(const QString &filePath);
    void toggleEmojiPopup();
    void buildEmojiPopup();
    QString messagePreview(const ForumChatMessage &message) const;

    void setStatus(const QString &text, bool error = false);
    QString peerStatusText(const ForumFriendInfo &peer) const;
    void runSearch(const QString &keyword);

    // ── 左栏（近似子导航面板：头部 + 胶囊页签 + 分组标题 + 条目列表）──
    QLabel *m_statusLabel = nullptr;
    QLabel *m_sideGroupTitle = nullptr;   ///< 分组标题（好友 · N / 申请 · N / 添加好友）
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_friendsTabButton = nullptr;
    QPushButton *m_requestsTabButton = nullptr;
    QPushButton *m_addTabButton = nullptr;
    QScrollArea *m_sideScroll = nullptr;
    QWidget *m_sideContainer = nullptr;
    QVBoxLayout *m_sideListLayout = nullptr;
    QLineEdit *m_searchEdit = nullptr;

    // ── 右栏（聊天面板）──
    QWidget *m_chatCard = nullptr;
    QWidget *m_chatEmpty = nullptr;        ///< 未选会话时的占位（图标 + 说明）
    QWidget *m_chatMain = nullptr;
    QWidget *m_chatAvatarSlot = nullptr;   ///< 会话头像槽位（头像是异步加载的，按需重建）
    QLabel *m_chatName = nullptr;
    QLabel *m_chatStatus = nullptr;
    QPushButton *m_chatProfileButton = nullptr;
    QPushButton *m_chatRemoveButton = nullptr;
    QPushButton *m_moreButton = nullptr;
    QScrollArea *m_msgsScroll = nullptr;
    QWidget *m_msgsContainer = nullptr;
    QVBoxLayout *m_msgsLayout = nullptr;
    QTextEdit *m_chatInput = nullptr;
    QLabel *m_charCounter = nullptr;       ///< 字数统计（接近上限时出现）
    QPushButton *m_sendButton = nullptr;

    // ── 状态 ──
    Tab m_tab = FriendsTab;
    ForumFriendSnapshot m_snapshot;
    ForumFriendInfo m_peer;              ///< 当前会话的好友
    QVector<ForumChatMessage> m_messages; ///< 当前会话消息（按 id 升序）
    int m_lastMessageId = 0;             ///< 已拿到的最大消息 id
    bool m_hasMore = false;
    bool m_sending = false;
    bool m_pollBusy = false;
    QTimer m_pollTimer;
    QTimer m_searchTimer;

    // ── 图片 / 引用 / 表情 ──
    QPushButton *m_emojiButton = nullptr;
    QPushButton *m_imageButton = nullptr;
    QWidget *m_replyBar = nullptr;
    QLabel *m_replyLabel = nullptr;
    QPushButton *m_replyCancelButton = nullptr;
    int m_replyTo = 0;                   ///< 正在引用的消息 id（0 = 没有）
    QString m_replyPreview;
    bool m_replyMine = false;
    QWidget *m_emojiPopup = nullptr;     ///< 表情弹窗（懒创建，Qt::Popup）
};
