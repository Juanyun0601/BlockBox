/**
 * @file   ForumThreadDetailPage.h
 * @brief  论坛帖子详情页：楼层 / 楼中楼 / 点赞 / 收藏 / 回复
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 版面与网页版 thread.html 对齐：
 *   面包屑（论坛首页 › 板块 › 帖子）
 *   两栏：主栏 = 主题头 + 楼层列表 + 分页 + 回复框；右栏 = 主题信息 / 同版块主题 / 社区提示
 *   主题头：置顶锁定徽章 + 标题 + 板块胶囊 + 作者 + 时间 / 浏览 / 回复 + 收藏按钮 + 标签
 *   楼层：左侧作者栏（头像 / 昵称 / 身份 / 发帖数）+ 右侧正文（楼主、时间、楼层号 + 正文 + 楼中楼 + 操作行）
 *   回复框：头像 + 回复主题标题，楼中楼回复在框内显示“正在回复谁”，支持 Ctrl + Enter 发送
 *
 * 数据来源：/api/threads/:id（分页，楼中楼嵌在楼层 children 里）。
 * 自己的楼层右键可编辑 / 删除，别人的楼层右键可举报；
 * 自己的主题可通过「管理」改标题标签或删除整帖；管理员还能置顶 / 锁定。
 */
#pragma once

#include <QHash>
#include <QPointer>
#include <QVector>
#include <QWidget>

#include <functional>

#include "components/ForumKit.h"
#include "components/ForumPostBrowser.h"
#include "utils/ForumContentService.h"

class QBoxLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QUrl;
class QVBoxLayout;

class ForumThreadDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumThreadDetailPage(QWidget *parent = nullptr);

    /** 打开一个帖子（从第 1 页开始加载） */
    void openThread(int threadId);

    /** 清空内容，避免切换帖子时短暂看到上一帖的残留 */
    void clearContent();

    /** 当前帖子 id（0 表示尚未打开过） */
    int threadId() const { return m_threadId; }

signals:
    /** 点了板块名：打开该板块的主题列表 */
    void boardOpenRequested(const QString &slug, const QString &boardName);

    /** 点了标签：打开该标签的主题列表 */
    void tagOpenRequested(const QString &tag);

    /** 点了作者：打开用户主页 */
    void userOpenRequested(int userId);

    /** 未登录时想回复 / 点赞：请求跳到论坛账户页登录 */
    void loginRequested();

    /** 自己删除了主题，通知列表页等刷新 */
    void threadDeleted();

    /** 点了「回到论坛」面包屑 */
    void backRequested();

    /** 右栏点开同版块的其他主题 */
    void threadOpenRequested(int threadId);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct CardRef
    {
        QPointer<QWidget> card;
        QPointer<ForumPostBrowser> browser;
    };

    // ── 搭骨架 ──
    void buildUi();
    void buildComposerCard();
    void buildRail();
    void applyLayoutMode();

    // ── 取数据 ──
    void loadPage(int page, bool scrollToBottomAfter = false);
    void reload(bool scrollToBottomAfter = false);
    void loadRailThreads(const QString &slug);

    // ── 渲染 ──
    void renderCrumbs();
    void renderHero();
    void renderPosts(const QVector<ForumPost> &posts);
    QWidget *buildPostRow(const ForumPost &post);
    QWidget *buildChildRow(const ForumPost &child);
    void appendPostActions(QHBoxLayout *row, const ForumPost &post, bool isChild);
    void appendActButton(QHBoxLayout *row, const QString &text, bool danger,
                         std::function<void()> onClick);
    void setupCardMenu(QWidget *card, const ForumPost &post);
    void renderReplyTarget();
    void renderComposer();
    void renderRail();

    // ── 交互 ──
    void setReplyTarget(const ForumPost &post);
    void clearReplyTarget();
    void loadQuote(const ForumPost &post);
    void submitReply();
    void copyThreadLink();
    void copyPostLink(const ForumPost &post);
    void toggleBookmark();
    void togglePin();
    void toggleLock();
    void startEditPost(const ForumPost &post);
    void confirmDeletePost(const ForumPost &post);
    void confirmDeleteThread();
    void editThreadMeta();
    void reportPost(const ForumPost &post);
    void openQuizDialog();
    void refreshLikeButtonStyle(QPushButton *button);
    void updateReplyBoxState();
    void updateReplyCount();
    void handleContentLink(const QUrl &url);
    void scrollToComposer();
    void setStatus(const QString &text, bool error = false);

    int m_threadId = 0;
    ForumThreadInfo m_info;
    int m_page = 1;
    int m_totalPages = 1;
    bool m_loaded = false;

    // 回复目标（楼中楼）
    int m_replyTargetPostId = 0;
    int m_replyTargetFloor = 0;
    QString m_replyTargetName;
    QString m_replyDraft;   ///< 重建回复框时保留已写内容
    int m_editingPostId = 0;
    QHash<int, CardRef> m_postCards;

    // ── UI ──
    QScrollArea *m_scroll = nullptr;
    QWidget *m_content = nullptr;
    QLabel *m_statusLabel = nullptr;

    // 面包屑
    QHBoxLayout *m_crumbsLayout = nullptr;

    // 两栏
    QBoxLayout *m_bodyLayout = nullptr;
    QWidget *m_mainColumn = nullptr;
    QWidget *m_railHost = nullptr;
    QVBoxLayout *m_mainLayout = nullptr;
    QVBoxLayout *m_railLayout = nullptr;

    // 主题头
    QFrame *m_heroCard = nullptr;
    QVBoxLayout *m_heroBody = nullptr;
    QPushButton *m_bookmarkButton = nullptr;

    // 楼层
    QVBoxLayout *m_floorsLayout = nullptr;
    ForumKit::ForumPager *m_pager = nullptr;

    // 回复框
    QFrame *m_composerCard = nullptr;
    QVBoxLayout *m_composerBody = nullptr;
    QVBoxLayout *m_replyTargetHost = nullptr;
    QPlainTextEdit *m_replyEdit = nullptr;
    QPushButton *m_replySend = nullptr;
    QLabel *m_replyCount = nullptr;

    // 右栏同版块主题列表（异步回填用）
    QWidget *m_railThreadHost = nullptr;
    QVBoxLayout *m_railThreadList = nullptr;
    QHash<QWidget *, int> m_railThreadIds;   ///< 侧栏主题行 -> 主题 id（整行点击）
};
