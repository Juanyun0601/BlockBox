/**
 * @file   ForumUserProfilePage.h
 * @brief  论坛用户主页：资料卡 + 主题 / 回复 列表
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 在帖子详情页或列表里点作者名进入。
 * 资料来自 GET /api/users/:id，「主题 / 回复」页签分别走
 * users/:id/threads 与 users/:id/posts（带分页）。
 */
#pragma once

#include <QWidget>

#include "components/ForumKit.h"
#include "utils/ForumContentService.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class ForumUserProfilePage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumUserProfilePage(QWidget *parent = nullptr);

    /** 打开一个用户的主页 */
    void openUser(int userId);

signals:
    /** 点击该用户发布的主题 / 回复所在的主题 */
    void threadOpenRequested(int threadId);

    /** 点击「发私信」：打开好友与私信页并直接和TA开聊天 */
    void chatOpenRequested(int userId);

    /** 点击「去好友页处理」：打开好友与私信页 */
    void friendsOpenRequested();

private:
    void buildUi();
    void loadProfile();
    void renderProfile(const ForumUserProfile &profile);
    /** 简介里的站内帖子链接在启动器内打开，其余交给系统浏览器 */
    void handleBioLink(const QUrl &url);
    void renderFriendActions(const ForumUserProfile &profile);
    void showThreadsTab();
    void showRepliesTab();
    void loadThreads(int page);
    void loadReplies(int page);
    void setStatus(const QString &text, bool error = false);

    int m_userId = 0;
    int m_activeTab = 0;   // 0 = 主题，1 = 回复
    int m_page = 1;
    int m_totalPages = 1;
    int m_threadTotal = 0;
    int m_postTotal = 0;
    QString m_friendship;        ///< 我和TA的好友关系（none/self/friends/outgoing/incoming）

    QScrollArea *m_scroll = nullptr;
    QFrame *m_profileCard = nullptr;
    /** 资料卡顶部的封面横幅（没有封面时是主题色渐变） */
    ForumKit::CoverBanner *m_coverBanner = nullptr;
    QVBoxLayout *m_profileBody = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_threadsTabButton = nullptr;
    QPushButton *m_repliesTabButton = nullptr;
    QPushButton *m_friendButton = nullptr;      ///< 加好友 / 撤回申请
    QPushButton *m_chatButton = nullptr;        ///< 发私信（已是好友时显示）
    QVBoxLayout *m_listLayout = nullptr;
    ForumKit::ForumPager *m_pager = nullptr;
};
