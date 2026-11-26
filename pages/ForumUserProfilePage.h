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

    /** 点击关注 / 粉丝名单里的用户：打开 TA 的主页 */
    void userOpenRequested(int userId);

    /** 点击「发私信」：打开好友与私信页并直接和TA开聊天 */
    void chatOpenRequested(int userId);

    /** 点击「去好友页处理」：打开好友与私信页 */
    void friendsOpenRequested();

private:
    void buildUi();
    void loadProfile();
    /** 拉取这位用户已点亮的勋章，在资料卡里显示一排小图标 */
    void loadProfileMedals();
    void renderProfile(const ForumUserProfile &profile);
    /** 简介里的站内帖子链接在启动器内打开，其余交给系统浏览器 */
    void handleBioLink(const QUrl &url);
    void renderProfileActions(const ForumUserProfile &profile);
    /** 关注 / 取关后同步按钮状态和 TA 的粉丝数 */
    void updateFollowState(bool following, int followerCount, int followingCount);
    void showThreadsTab();
    void showRepliesTab();
    void showFollowingTab();
    void showFollowersTab();
    /** 页签切换时把另外三个按钮取消选中 */
    void syncTabButtons();
    void loadThreads(int page);
    void loadReplies(int page);
    /** followers = true 拉粉丝，false 拉 TA 关注的人 */
    void loadFollows(bool followers, int page);
    void setStatus(const QString &text, bool error = false);

    int m_userId = 0;
    int m_activeTab = 0;   ///< 0 = 主题，1 = 回复，2 = 关注，3 = 粉丝
    int m_page = 1;
    int m_totalPages = 1;
    int m_threadTotal = 0;
    int m_postTotal = 0;
    int m_followerCount = 0;
    int m_followingCount = 0;
    bool m_isSelf = false;         ///< 看的是不是自己的主页
    bool m_userFollowing = false;  ///< 我是否关注了 TA
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
    QPushButton *m_followingTabButton = nullptr;
    QPushButton *m_followersTabButton = nullptr;
    QPushButton *m_followButton = nullptr;      ///< 关注 / 已关注
    QPushButton *m_friendButton = nullptr;      ///< 加好友 / 撤回申请
    QPushButton *m_chatButton = nullptr;        ///< 发私信（已是好友时显示）
    QLabel *m_followingValue = nullptr;         ///< 资料卡里的关注数
    QLabel *m_followersValue = nullptr;         ///< 资料卡里的粉丝数
    QWidget *m_profileMedalsHost = nullptr;     ///< 资料卡里的勋章图标行
    QVBoxLayout *m_listLayout = nullptr;
    ForumKit::ForumPager *m_pager = nullptr;
};
