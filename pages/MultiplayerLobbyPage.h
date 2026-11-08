/**
 * @file   MultiplayerLobbyPage.h
 * @brief  联机大厅（社区页内的子页面）：看房间、复制邀请码、一键挑实例加入
 * @author BlockBox Team
 * @date   2026-11-06
 *
 * 大厅不再塞在实例助手里：开房仍在「实例助手 > 联机」里完成，点「发布到联机大厅」
 * 后房间出现在这里。加入时按房间的版本 / 加载器 / 模组挑一个最新且合适的本地实例，
 * 然后交给主窗口启动游戏并直连房间（陶瓦核心会先在本机拉起客人端转发）。
 */

#pragma once

#include "utils/ForumLobbyService.h"

#include <QVector>
#include <QWidget>

#include <functional>

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

signals:
    /** 用指定地址启动游戏：版本 / 加载器 / 模组用于挑本地实例 */
    void joinGameRequested(const QString &address, quint16 port, const QString &gameVersion,
                           const QString &loader, const QStringList &mods);
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
    void renderRooms();
    QWidget *createRoomCard(const ForumLobbyRoom &room);
    /** 空 / 加载中 / 连不上时的居中占位卡（可选一个操作按钮） */
    QWidget *createStateCard(const QString &glyph, const QString &title, const QString &hint,
                             const QString &buttonText, const std::function<void()> &onClick);
    /** 刷新顶部横幅里的房间概况 */
    void updateHeroMeta();
    void joinRoom(const ForumLobbyRoom &room);
    void showModpackNotice(const ForumLobbyRoom &room);
    void setStatus(const QString &text, bool error = false);

    void onTerracottaStateChanged();
    void handlePendingJoin(bool success, const QString &message = QString());
    static bool parseHostPort(const QString &text, QString *host, quint16 *port);
    QString matchText(const ForumLobbyRoom &room, QString *missingText) const;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_heroMetaLabel = nullptr;
    QVBoxLayout *m_rootLayout = nullptr;
    QBoxLayout *m_heroLayout = nullptr;
    QWidget *m_heroActions = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QAction *m_searchIcon = nullptr;
    QComboBox *m_coreCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_container = nullptr;
    QVBoxLayout *m_roomsLayout = nullptr;
    QTimer *m_refreshTimer = nullptr;

    QVector<ForumLobbyRoom> m_rooms;
    bool m_loading = false;
    bool m_loadedOnce = false;
    bool m_compactLayout = false;
    QString m_errorText;
    int m_total = 0;

    // 陶瓦核心：等进程就绪 / 端口转发完成后再启动游戏
    bool m_pendingJoin = false;
    bool m_pendingWaitingCore = false;
    int m_pendingRoomId = 0;
    QString m_pendingCode;
    QString m_pendingVersion;
    QString m_pendingLoader;
    QStringList m_pendingMods;
};
