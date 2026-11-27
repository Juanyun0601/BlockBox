/**
 * @file   CommunityForumPage.h
 * @brief  社区页 —— 方块盒子论坛首页（与论坛网页版首页同款左右栏）
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 页面结构（对齐论坛网页版首页）：
 *   顶部：品牌绿渐变头图（论坛名 / 简介 / 地址 / 刷新 / 打开网页）
 *   数据行：成员 / 主题 / 回复 / 在线
 *   左右两栏：
 *     左栏：板块（卡片网格）+ 联机大厅（在线房间）+ 最新讨论（帖子列表）
 *     右栏：社区情况 + 热门标签 + 发帖小提示
 *   窄窗口（手机 / 小窗）自动改成上下堆叠，板块改成单列。
 *
 * 数据来源：论坛的 /api/boards、/api/threads、/api/tags、/api/lobby/rooms。
 * 论坛地址取自 SettingsManager::forumBaseUrl()（默认官方论坛）。
 */

#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

#include "utils/ForumLobbyService.h"

class QBoxLayout;
class QGridLayout;
class QHBoxLayout;
class QJsonObject;
class QLabel;
class QLayout;
class QProgressBar;
class QPropertyAnimation;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QVBoxLayout;
class MultiplayerLobbyPage;

class CommunityForumPage : public QWidget
{
    Q_OBJECT

public:
    explicit CommunityForumPage(QWidget *parent = nullptr);
    ~CommunityForumPage() override;

    /** 重新拉取论坛数据；联机大厅子页面打开时改为刷新大厅 */
    Q_INVOKABLE void refresh();

    /** 联机大厅子页面打开时关闭它并返回 true（顶栏返回键用） */
    bool closeLobbyIfVisible();

    /** 大厅里还开着房间详情时先关详情并返回 true（顶栏返回键逐级退回用） */
    bool closeLobbyDetailIfVisible();

signals:
    /** 点击一个主题行：在启动器内打开帖子详情 */
    void threadOpenRequested(int threadId);

    /** 点击板块卡片：在启动器内打开该板块的主题列表 */
    void boardOpenRequested(const QString &slug, const QString &boardName);

    /** 点击标签：在启动器内打开该标签的主题列表 */
    void tagOpenRequested(const QString &tag);

    /** 点击「发帖」：打开启动器内的发帖页 */
    void composeRequested();

    /** 点击「好友」：打开启动器内的好友与私信页 */
    void friendsOpenRequested();

    /** 点击「我的主页」：打开自己的论坛个人主页（关注 / 粉丝名单也在这里） */
    void myProfileRequested();

    /** 点击「通知」：跳到论坛账户页的通知页签 */
    void notificationsRequested();

    /** 未登录时点「登录」：跳到论坛账户页 */
    void loginRequested();

    /** 点击联机大厅的房间或「进入联机大厅」：在社区页内打开大厅子页面 */
    void lobbyOpenRequested();

    /** 联机大厅「一键加入」：交给主窗口按房间要求挑实例、按需补装后启动 */
    void joinGameRequested(const ForumLobbyRoom &room, const QString &address, quint16 port);

    /** 联机大厅「我要开房」：打开实例助手 */
    void openInstanceAssistantRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct BoardInfo
    {
        QString name;
        QString slug;
        QString description;
        QString icon;
        int threads = 0;
        int posts = 0;
    };

    struct ThreadInfo
    {
        int id = 0;
        QString title;
        QString board;
        QString author;
        int replies = 0;
        int views = 0;
        qint64 lastPostAt = 0;
    };

    struct TagInfo
    {
        QString tag;
        int count = 0;
    };

    void buildUi();
    void applySiteUrl();
    void updateHeroButtons();
    void requestAll();
    /** 走论坛账户服务拉接口数据（自带登录令牌与人机验证令牌） */
    void requestJson(const QString &path, const std::function<void(const QJsonObject &)> &onSuccess);
    void finishRequest();
    /** 刷新页面顶部加载进度条（done/total 为已完成 / 总请求数） */
    void updateLoadingBar();
    void renderAll();
    void renderStats();
    void renderBoards();
    void renderLobby();
    void renderThreads();
    void renderTags();
    void fetchLobby();
    void showLobbyPage();
    void hideLobbyPage();
    void clearLayout(QLayout *layout);
    void setStatus(const QString &text, bool isError = false);
    void openUrl(const QString &path);
    QString forumUrl(const QString &path) const;

    QString m_baseUrl;

    // 页面顶部加载进度条（数据加载期间显示）
    QProgressBar *m_loadingBar = nullptr;
    QPropertyAnimation *m_loadingAnimation = nullptr;
    QWidget *m_loadingStrip = nullptr;      ///< 进度条所在的 3px 细条
    QScrollArea *m_contentScroll = nullptr; ///< 论坛正文，打开联机大厅时暂时收起
    int m_loadTotalSteps = 0;   ///< 本轮加载的总请求数
    int m_loadDoneSteps = 0;    ///< 本轮已完成的请求数

    // 头图
    QLabel *m_titleLabel = nullptr;
    QLabel *m_urlLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_composeButton = nullptr;   ///< 「发帖」：进启动器内的发帖页
    QPushButton *m_friendsButton = nullptr;   ///< 「好友」：进启动器内的好友与私信页（已登录时显示）
    QPushButton *m_myProfileButton = nullptr; ///< 「我的主页」：看自己的关注 / 粉丝名单（已登录时显示）
    QPushButton *m_notifyButton = nullptr;    ///< 「通知」：跳账户页通知页签（已登录时显示）
    QPushButton *m_loginButton = nullptr;     ///< 「登录」：跳账户页（未登录时显示）

    // 左右栏
    QBoxLayout *m_columnsLayout = nullptr;
    QWidget *m_rightColumn = nullptr;
    QLabel *m_sideSummary = nullptr;

    // 各区块
    QGridLayout *m_statsLayout = nullptr;
    QVBoxLayout *m_boardsLayout = nullptr;
    QVBoxLayout *m_lobbyLayout = nullptr;
    QLabel *m_lobbyHint = nullptr;
    QVBoxLayout *m_threadsLayout = nullptr;
    QVBoxLayout *m_tagsLayout = nullptr;

    QVector<BoardInfo> m_boards;
    QVector<ForumLobbyRoom> m_lobbyRooms;
    bool m_lobbyLoaded = false;
    QString m_lobbyError;
    QWidget *m_lobbyOverlay = nullptr;
    MultiplayerLobbyPage *m_lobbyPage = nullptr;
    QVector<ThreadInfo> m_threads;
    QVector<TagInfo> m_tags;
    int m_siteUsers = 0;
    int m_siteThreads = 0;
    int m_sitePosts = 0;
    int m_siteOnline = 0;
    int m_siteNewUsers = 0;
    QString m_newestUserName;

    int m_boardColumns = 2;
    int m_pendingRequests = 0;
    int m_failedRequests = 0;
    bool m_humanBlocked = false;   ///< 这一轮有请求被人机验证拦下来了
    bool m_loadedOnce = false;
};
