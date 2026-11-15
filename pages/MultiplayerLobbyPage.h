/**
 * @file   MultiplayerLobbyPage.h
 * @brief  联机大厅（社区页内的子页面）：看房间、复制邀请码、一键挑实例加入
 * @author BlockBox Team
 * @date   2026-11-06
 *
 * 大厅不再塞在实例助手里：开房仍在「实例助手 > 联机」里完成，点「发布到联机大厅」
 * 后房间出现在这里。房间卡与「下载模组」等页的模组卡同款，支持列表 / 瀑布流切换；
 * 单击卡片进入房间详情页，详情页里看完整介绍、模组、前置整合包并加入。
 * 加入时按房间的版本 / 加载器 / 模组挑一个最新且合适的本地实例，然后交给主窗口
 * 启动游戏并直连房间（陶瓦核心会先在本机拉起客人端转发）。
 */

#pragma once

#include "utils/ForumLobbyService.h"
#include "utils/Lobby/LobbyInstanceMatcher.h"

#include <QList>
#include <QVector>
#include <QWidget>

#include <functional>

class ContentViewSwitch;
class MasonryLayout;
class MultiplayerLobbyDetailPage;
class QAction;
class QBoxLayout;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QTimer;
class QVBoxLayout;

class MultiplayerLobbyPage : public QWidget
{
    Q_OBJECT

public:
    explicit MultiplayerLobbyPage(QWidget *parent = nullptr);
    ~MultiplayerLobbyPage() override;

    /** 重新拉取房间列表（切到本页时自动调用） */
    void refresh();

    /** 房间详情页开着时关掉它并返回 true（顶栏返回键逐级退回用） */
    bool closeDetailIfVisible();
    bool isDetailVisible() const;

signals:
    /**
     * @brief 用指定地址启动游戏
     * @param room    房间信息（版本 / 加载器 / 模组 / 资源包等，用来挑实例、问要不要补装）
     * @param address 联机核心给出的主机地址
     * @param port    联机核心给出的端口
     */
    void joinGameRequested(const ForumLobbyRoom &room, const QString &address, quint16 port);
    /** 「我要开房」：打开实例助手的联机页 */
    void openInstanceAssistantRequested();
    /** 发布 / 加入前需要登录论坛账号 */
    void forumLoginRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void applyStyle();
    void updateResponsiveLayout();
    void requestRooms();
    /** 两个来源都返回（或都失败）后聚合成一份列表；还有来源没回来时直接返回 */
    void onSourceFetchSettled();
    void renderRooms();
    /** 清空房间卡与当前布局（列表 / 瀑布流 / 空状态共用） */
    void clearRoomViews();
    void applyRoomViewMode(int mode);
    QWidget *createRoomListCard(const ForumLobbyRoom &room);
    QWidget *createRoomMasonryCard(const ForumLobbyRoom &room);
    /** 空 / 加载中 / 连不上时的居中占位卡（可选一个操作按钮） */
    QWidget *createStateCard(const QString &glyph, const QString &title, const QString &hint,
                             const QString &buttonText, const std::function<void()> &onClick);
    /** 刷新顶部横幅里的房间概况 */
    void updateHeroMeta();
    void joinRoom(const ForumLobbyRoom &room);
    void showModpackNotice(const ForumLobbyRoom &room);
    void setStatus(const QString &text, bool error = false);

    /** 打开 / 关闭房间详情页（叠在本页之上） */
    void openRoomDetail(const ForumLobbyRoom &room);
    void hideRoomDetail();
    void setJoinHint(const QString &text, bool error = false);
    /** 刷新时把详情页里的房间信息同步成最新的 */
    void syncDetailRoom();

    void onTerracottaStateChanged();
    void handlePendingJoin(bool success, const QString &message = QString());
    static bool parseHostPort(const QString &text, QString *host, quint16 *port);
    QString matchText(const ForumLobbyRoom &room, QString *missingText) const;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_heroMetaLabel = nullptr;
    QVBoxLayout *m_rootLayout = nullptr;
    QWidget *m_hero = nullptr;
    QWidget *m_filterBar = nullptr;
    QBoxLayout *m_heroLayout = nullptr;
    QWidget *m_heroActions = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QAction *m_searchIcon = nullptr;
    QComboBox *m_sourceCombo = nullptr;
    QComboBox *m_coreCombo = nullptr;
    ContentViewSwitch *m_viewSwitch = nullptr;
    QLabel *m_statusLabel = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_container = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    MasonryLayout *m_masonryLayout = nullptr;
    QVector<QWidget *> m_roomCards;
    QTimer *m_refreshTimer = nullptr;

    // 房间详情页（第一次点卡片时创建，盖在本页内容上）
    QWidget *m_detailOverlay = nullptr;
    MultiplayerLobbyDetailPage *m_detailPage = nullptr;
    int m_detailRoomId = 0;

    QVector<ForumLobbyRoom> m_rooms;
    int m_viewMode = 1;   ///< 与 ContentViewSwitch::ViewMode 对齐（0 列表 / 1 瀑布流）
    /** 房间来源：0 全部源 / 1 方块盒子源 / 2 红石联机源（记住上次的选择） */
    int m_source = 0;
    bool m_loading = false;
    bool m_loadedOnce = false;
    bool m_compactLayout = false;
    QString m_errorText;
    int m_total = 0;

    // 双来源聚合：一次刷新里论坛源和红石源各自拉一份，都回来后再合成列表
    QVector<ForumLobbyRoom> m_forumRooms;  ///< 方块盒子源（论坛大厅）本轮结果
    QVector<ForumLobbyRoom> m_hongshiRooms; ///< 红石联机源本轮结果（已转成大厅房间结构）
    int m_forumTotal = 0;
    bool m_forumPending = false;
    bool m_hongshiPending = false;
    QString m_forumError;
    QString m_hongshiError;
    int m_fetchGen = 0;    ///< 刷新轮次：旧一轮的回调全部作废

    // 陶瓦核心：等进程就绪 / 端口转发完成后再启动游戏
    bool m_pendingJoin = false;
    bool m_pendingWaitingCore = false;
    int m_pendingRoomId = 0;
    QString m_pendingCode;
    ForumLobbyRoom m_pendingRoom;      ///< 待加入的房间（版本 / 加载器 / 模组 / 资源包都在里面）
    /** 实例扫描结果：列表里每张卡都要问「本机哪个实例合适」，只扫一次 */
    QList<LobbyInstanceInfo> m_instancePool;
};
