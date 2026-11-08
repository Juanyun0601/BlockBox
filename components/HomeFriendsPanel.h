/**
 * @file   HomeFriendsPanel.h
 * @brief  首页左侧好友列表：头像 + 用户名，好友开房时置顶并显示「一起玩」
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 首页「封面图 + 皮肤模型」横排的最左侧，宽度与右侧皮肤面板一致：
 *   - 已登录时列出论坛好友，点卡片进入和该好友的私信页；
 *   - 每隔半分钟同步一次好友与联机大厅房间，好友开了大厅房间就把卡片置顶，
 *     右侧出现「一起玩」按钮，点击直接加入他的房间。
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

class QLabel;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

class HomeFriendsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HomeFriendsPanel(QWidget *parent = nullptr);
    ~HomeFriendsPanel() override;

    /** 首页显示 / 隐藏时启停轮询；面板被隐藏（无好友等）不影响继续同步 */
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
    /** 标题栏「全部」/ 空态里的「去添加好友」：打开好友与私信页 */
    void openFriendsPageRequested();
    /** 是否应该显示左栏：只有已登录且确实有好友时才显示 */
    void panelVisibilityChanged(bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void buildUi();
    void loadFriends();
    void loadRooms();
    void render();
    void renderState(const QString &text, const QString &actionText = QString(),
                     const std::function<void()> &onAction = nullptr);
    QWidget *createFriendCard(const ForumFriendInfo &info, const ForumLobbyRoom *room);
    void rebuildHeader();
    int onlineFriendCount() const;
    /** 当前数据指纹：内容没变就不重建列表，避免轮询时滚动位置和头像闪烁 */
    QString dataSignature() const;
    /** 好友 id → 该好友正在开放的大厅房间（同一人多个房间取最新的） */
    QHash<int, ForumLobbyRoom> activeRoomsByHost() const;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_openButton = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QTimer *m_pollTimer = nullptr;

    QVector<ForumFriendInfo> m_friends;
    QVector<ForumLobbyRoom> m_rooms;
    bool m_friendsLoading = false;
    bool m_roomsLoading = false;
    bool m_friendsFailed = false;
    bool m_roomsFailed = false;
    bool m_loadedOnce = false;
    bool m_active = false;
    QString m_builtSignature;
};
