/**
 * @file   CommunityForumPage.h
 * @brief  社区页 —— 方块盒子论坛首页（与论坛网页版首页同款左右栏）
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 页面结构（对齐论坛网页版首页）：
 *   顶部：品牌绿渐变头图（论坛名 / 简介 / 地址 / 刷新 / 打开网页 / 论坛设置）
 *   数据行：成员 / 主题 / 回复 / 在线
 *   左右两栏：
 *     左栏：板块（卡片网格）+ 最新讨论（帖子列表）
 *     右栏：社区情况 + 热门标签 + 发帖小提示
 *   窄窗口（手机 / 小窗）自动改成上下堆叠，板块改成单列。
 *
 * 数据来源：论坛的 /api/boards、/api/threads、/api/tags。
 * 论坛地址取自 SettingsManager::forumBaseUrl()，可以改成 http://127.0.0.1:3210 本地调试。
 */

#pragma once

#include <QNetworkAccessManager>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QBoxLayout;
class QGridLayout;
class QHBoxLayout;
class QJsonDocument;
class QLabel;
class QLayout;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QVBoxLayout;

class CommunityForumPage : public QWidget
{
    Q_OBJECT

public:
    explicit CommunityForumPage(QWidget *parent = nullptr);
    ~CommunityForumPage() override;

    /** 重新拉取论坛数据（进入页面 / 点击刷新时调用） */
    void refresh();

signals:
    /** 点击「论坛设置」：跳到 设置 → 高级设置 修改论坛地址 */
    void openAdvancedSettingsRequested();

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

    /** 点击「通知」：跳到论坛账户页的通知页签 */
    void notificationsRequested();

    /** 未登录时点「登录」：跳到论坛账户页 */
    void loginRequested();

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
    void requestJson(const QString &path, const std::function<void(const QJsonDocument &)> &onSuccess);
    /** 实际发一次请求；网络抖动时按 attempt 重试（第 2 次起退回 HTTP/1.1） */
    void sendJsonRequest(const QString &path,
                         const std::function<void(const QJsonDocument &)> &onSuccess, int attempt);
    void finishRequest();
    void renderAll();
    void renderStats();
    void renderBoards();
    void renderThreads();
    void renderTags();
    void clearLayout(QLayout *layout);
    void setStatus(const QString &text, bool isError = false);
    void openUrl(const QString &path);
    QString forumUrl(const QString &path) const;

    QNetworkAccessManager *m_network = nullptr;
    QString m_baseUrl;

    // 头图
    QLabel *m_titleLabel = nullptr;
    QLabel *m_urlLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_composeButton = nullptr;   ///< 「发帖」：进启动器内的发帖页
    QPushButton *m_friendsButton = nullptr;   ///< 「好友」：进启动器内的好友与私信页（已登录时显示）
    QPushButton *m_notifyButton = nullptr;    ///< 「通知」：跳账户页通知页签（已登录时显示）
    QPushButton *m_loginButton = nullptr;     ///< 「登录」：跳账户页（未登录时显示）

    // 左右栏
    QBoxLayout *m_columnsLayout = nullptr;
    QWidget *m_rightColumn = nullptr;
    QLabel *m_sideSummary = nullptr;

    // 各区块
    QGridLayout *m_statsLayout = nullptr;
    QVBoxLayout *m_boardsLayout = nullptr;
    QVBoxLayout *m_threadsLayout = nullptr;
    QVBoxLayout *m_tagsLayout = nullptr;

    QVector<BoardInfo> m_boards;
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
    bool m_loadedOnce = false;
};
