/**
 * @file   CommunityLobbyPage.h
 * @brief  社区联机大厅页 - 显示所有红石联机的开放房间
 * @author BlockBox Team
 * @date   2026-08-30
 *
 * 页面结构：
 *   顶部：Tab标签（联机大厅/论坛/服务器）
 *   Tab1-联机大厅：搜索栏 + 筛选条件 + 房间卡片网格
 *   Tab2-论坛：即将实现占位
 *   Tab3-服务器：即将实现占位
 *
 * 数据来源：
 *   通过 HongshiLobbyClient 从红石联机服务器拉取房间列表，
 *   支持自动定时刷新 + 手动刷新。
 */

#pragma once

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "utils/Hongshi/HongshiLobbyClient.h"

class FlowLayout;

class CommunityLobbyPage : public QWidget
{
    Q_OBJECT

public:
    explicit CommunityLobbyPage(QWidget *parent = nullptr);
    ~CommunityLobbyPage() override;

    void refreshRooms();     ///< 手动刷新房间列表

    void refresh();

signals:
    /// 用户点击加入房间，携带房间信息
    void joinRoomRequested(const HongshiRoomInfo &room);
    /// 用户点击创建房间
    void createRoomRequested();

private slots:
    void onSearchTextChanged(const QString &text);
    void onVersionFilterChanged(int index);
    void onStatusFilterChanged(int index);
    void onRefreshClicked();
    void onCreateRoomClicked();
    void onRoomCardJoinClicked(const QString &roomId);
    void onRoomsRefreshed(const QList<HongshiRoomInfo> &rooms);
    void onRefreshError(const QString &error);
    void onLoadingChanged(bool loading);

private:
    void initUI();
    void buildEmptyState(QWidget *parent);
    QWidget *createComingSoonTab(const QString &title);

    void fetchRoomList();

    void applyThemeStyles();
    void clearRoomCards();
    void populateRoomCards();
    QWidget *createRoomCard(const HongshiRoomInfo &room);
    void updateEmptyState();

    /// 格式化玩家数显示
    QString formatPlayerCount(int current, int max) const;
    /// 格式化创建时间
    QString formatRelativeTime(qint64 timestamp) const;
    /// 获取状态显示文字
    QString statusDisplayText(const QString &status) const;
    /// 获取状态对应颜色属性
    QString statusProperty(const QString &status) const;

private:
    // ---- Tab标签 ----
    QTabWidget *m_tabWidget;

    // ---- 搜索与筛选 ----
    QLineEdit *m_searchEdit;
    QComboBox *m_versionFilter;
    QComboBox *m_statusFilter;
    QPushButton *m_refreshBtn;
    QPushButton *m_createRoomBtn;
    QLabel *m_roomCountLabel;

    // ---- 房间卡片区域 ----
    QScrollArea *m_scrollArea;
    QWidget *m_cardContainer;
    FlowLayout *m_cardFlowLayout;

    // ---- 空状态 ----
    QWidget *m_emptyWidget;
    QLabel *m_emptyIcon;
    QLabel *m_emptyText;

    // ---- 数据 ----
    QList<HongshiRoomInfo> m_allRooms;        ///< 全部房间
    QList<HongshiRoomInfo> m_filteredRooms;   ///< 筛选后房间
    QList<QWidget *> m_cardWidgets;           ///< 当前显示的卡片控件

    QTimer m_autoRefreshTimer;                ///< 自动刷新定时器
};
