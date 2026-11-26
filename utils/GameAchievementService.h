/**
 * @file   GameAchievementService.h
 * @brief  全局游戏成就同步：不开实例助手也能记录，单机 / 方块盒子联机都覆盖
 * @date   2026-11-22
 *
 * 单机：盯当前实例最新存档的 advancements 文件，读 level.dat 确认「生存 + 未开作弊」。
 * 方块盒子联机：客户端拿不到服务端存档，改为解析游戏日志里「你获得了进度」的广播；
 *   因为客户端无法验证服务器端的游戏模式和作弊开关，第一次在该房间检测到成就时
 *   会让玩家确认一次「这个房间是生存、未开作弊」，确认后长期记住。
 *
 * 两条来源共用同一个待同步队列：登录方块盒子账号后自动补传，网络失败会重试。
 */
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class GameAchievementWatcher;
class QWidget;

class GameAchievementService : public QObject
{
    Q_OBJECT

public:
    explicit GameAchievementService(QWidget *notificationHost, QObject *parent = nullptr);
    ~GameAchievementService() override;

    /** 实例路径 / 版本 / 当前游戏内玩家名（每次游戏启动时同步） */
    void setSessionContext(const QString &instancePath, const QString &version,
                           const QString &localPlayerName);
    /** 通过方块盒子启动的联机房间；roomId <= 0 表示不是方块盒子联机 */
    void setLobbyContext(int roomId, const QString &roomTitle);
    bool hasLobbyContext() const { return m_lobbyRoomId > 0; }
    /** 游戏启动 / 退出 */
    void startSession();
    void stopSession();

    /** 游戏日志的一行（GameLauncher::launchDetailAdded） */
    void onGameLogLine(const QString &line);

signals:
    /** 本地文件 / 日志里发现了一条新成就（供界面刷新用） */
    void achievementEarned(const QString &title);

private:
    void onFileAdvancement(const QString &id, const QString &world, qint64 earnedAtMs);
    void handleChatAdvancement(const QString &title, const QString &line);
    void enqueueAdvancement(const QString &id, const QString &title, const QString &condition,
                            const QString &world, qint64 earnedAtSec);
    void flushQueue();
    void loadPending();
    void savePending();
    void ensureAdvancementTitles();
    QString resolveTitle(const QString &id) const;
    QString conditionForTitle(const QString &title) const;
    QString advancementIdForTitle(const QString &title) const;
    void askLobbyTrust();

    QWidget *m_host = nullptr;
    GameAchievementWatcher *m_watcher = nullptr;

    QString m_instancePath;
    QString m_version;
    QString m_localPlayerName;
    QStringList m_localNames; ///< 游戏账号名 + 论坛昵称，联机聊天里出现任一都算自己
    bool m_sessionActive = false;

    int m_lobbyRoomId = 0;
    QString m_lobbyTitle;
    enum class Trust { Unknown, Yes, No };
    Trust m_lobbyTrust = Trust::Unknown;
    bool m_lobbyPromptOpen = false;
    struct BufferedChat
    {
        QString title;
        QString condition;
        QString id;
        qint64 earnedAt = 0;
    };
    QVector<BufferedChat> m_chatBuffer;

    QHash<QString, QString> m_advancementTitles;      ///< id → 标题
    QHash<QString, QString> m_advancementDescriptions; ///< id → 达成条件（游戏内 description）
    QHash<QString, QString> m_titleToAdvancement;     ///< 标题 → id
    bool m_titlesLoading = false;
    bool m_titlesLoaded = false;
    QString m_titlesVersion;

    QJsonArray m_pending;
    QSet<QString> m_queuedIds;
    bool m_syncing = false;
    QSet<QString> m_seenAdvancements;                 ///< 本次游戏已提示过的成就
    QSet<QString> m_seenTitles;                       ///< 按标题去重（存档标题与联机日志标题）
};
