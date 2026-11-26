/**
 * @file   HomeFriendsPanel.h
 * @brief  首页好友栏：头像 + 用户名的一排卡片，好友开房时置顶并显示「一起玩」
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 首页「最近游玩」下方的独立一栏（原来的轮播左侧窄列表）：
 *   - 已登录时列出论坛好友，点卡片进入和该好友的私信页；
 *   - 每隔半分钟同步一次好友与联机大厅房间，好友开了大厅房间就把卡片置顶、
 *     右侧出现「一起玩」按钮，点击直接加入他的房间；
 *   - 卡片与「最近游玩」「论坛热门帖」卡片同宽，只摆一行（一行能放几张就显示几张），
 *     其余好友去好友页看；
 *   - 未登录 / 没有好友 / 加载失败都有对应的空态提示。
 */

#pragma once

#include <QHash>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

#include "utils/ForumChatService.h"
#include "utils/ForumLobbyService.h"

class FlowLayout;
class QLabel;
class QPushButton;
class QTimer;

class HomeFriendsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HomeFriendsPanel(QWidget *parent = nullptr);
    ~HomeFriendsPanel() override;

    /** 首页显示 / 隐藏时启停轮询 */
    void setActive(bool active);

    /** 重新拉取好友与联机大厅房间（切回首页 / 顶栏刷新 / 定时轮询） */
    void refresh();

signals:
    /** 点击好友卡片：打开与这个好友的私信页 */
    void friendChatRequested(int userId);
    /** 点击好友卡片上的「一起玩」：加入该好友正在开放的联机房间 */
    void joinRoomRequested(const ForumLobbyRoom &room);
    /** 未登录空态里的「登录」按钮 */
    void loginRequested();
    /** 标题栏「查看全部」/ 空态里的「去添加好友」：打开好友与私信页 */
    void openFriendsPageRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void loadFriends();
    void loadRooms();
    void render();
    /** 清空当前卡片（空态切换 / 重新分列时用） */
    void clearCards();
    /** 空态：一行提示 + 可选按钮，占住卡片流的位置 */
    void showState(const QString &text, const QString &actionText = QString(),
                   const std::function<void()> &onAction = nullptr);
    void hideState();
    /** 卡片流容器宽度变化后延迟一拍重排，合并连续 resize */
    void scheduleRebuild();
    /** 按容器宽度算出一行能摆几张卡片（一栏只摆一行，与最近游玩 / 热门帖同宽） */
    int calculateColumns() const;
    /** 按容器宽度算出每张卡片的宽度 */
    int calculateCardWidth() const;
    QWidget *createFriendCard(const ForumFriendInfo &info, const ForumLobbyRoom *room, int cardWidth);
    void rebuildHeader();
    int onlineFriendCount() const;
    /** 当前数据指纹：内容没变就不重建列表，避免轮询时头像闪烁 */
    QString dataSignature() const;
    /** 好友 id → 该好友正在开放的大厅房间（同一人多个房间取最新的） */
    QHash<int, ForumLobbyRoom> activeRoomsByHost() const;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_openButton = nullptr;
    QWidget *m_listHost = nullptr;       // 好友卡片流容器
    FlowLayout *m_listLayout = nullptr;
    // 空态行（未登录 / 加载中 / 加载失败 / 还没有好友）
    QWidget *m_stateBar = nullptr;
    QLabel *m_stateLabel = nullptr;
    QPushButton *m_stateBtn = nullptr;
    std::function<void()> m_stateAction;
    QTimer *m_pollTimer = nullptr;

    QVector<ForumFriendInfo> m_friends;
    QVector<ForumLobbyRoom> m_rooms;
    bool m_friendsLoading = false;
    bool m_roomsLoading = false;
    bool m_friendsFailed = false;
    bool m_roomsFailed = false;
    bool m_loadedOnce = false;
    bool m_active = false;
    bool m_rebuildPending = false;
    QString m_builtSignature;
    int m_builtCardWidth = -1;   // 上次建卡片用的宽度
};
