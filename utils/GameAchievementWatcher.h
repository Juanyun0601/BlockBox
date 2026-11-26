/**
 * @file   GameAchievementWatcher.h
 * @brief  监听单机存档里「生存模式、未开作弊」下新获得的游戏成就
 * @date   2026-11-22
 *
 * 实例助手打开且游戏正在运行时才开始监听：
 *   - 第一次扫到存档时，把当时已经完成的成就全部记成「旧成就」，不会补发；
 *   - 之后存档里出现新的 advancement，才算这次新获得的；
 *   - 只认 level.dat 里 GameType=0（生存）且 allowCommands=0（未开作弊）的存档；
 *   - 换存档 / 换玩家文件会重新记一次基线，避免把另一个世界里的旧成就带进来。
 *
 * 监听只覆盖单机存档 advancements 目录下的玩家 JSON；联机服的成就存在服务端，客户端拿不到。
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class QTimer;
class QDateTime;

/** 一条新获得的游戏成就 */
struct GameAdvancement
{
    QString id;       ///< 例如 minecraft:story/mine_stone
    QString world;    ///< 存档显示名
    qint64 earnedAt = 0; ///< 完成时间（毫秒时间戳）
};

class GameAchievementWatcher : public QObject
{
    Q_OBJECT

public:
    explicit GameAchievementWatcher(QObject *parent = nullptr);
    ~GameAchievementWatcher() override;

    /** 当前实例路径 / 版本（助手 setInstanceContext 时同步过来） */
    void setContext(const QString &instancePath, const QString &version);

    /** 助手显示时开始监听；隐藏时停止并清掉本次基线 */
    void start();
    void stop();
    bool isRunning() const { return m_running; }
    /** 当前是否确实在玩一个本地存档（用来避免联机日志和存档监听重复记同一条成就） */
    bool isWatchingWorld() const;

signals:
    /** 新获得一条符合「生存模式、未开作弊」的成就 */
    void advancementEarned(const GameAdvancement &advancement);

private slots:
    void poll();

private:
    void resetBaseline();
    /** 找到最近在玩的存档目录，返回 {存档目录, 存档显示名} */
    bool findActiveWorld(QString &worldDir, QString &worldName);
    /** 解析 advancements/<uuid>.json，只收 done=true 的条目 */
    bool readDoneAdvancements(const QString &file, QHash<QString, qint64> &done) const;

    QTimer *m_timer = nullptr;
    QString m_instancePath;
    QString m_version;
    bool m_running = false;
    bool m_gameWasRunning = false;

    // 当前监听的存档
    QString m_worldDir;
    QString m_worldName;
    QString m_advancementFile;
    qint64 m_advancementFileMtime = 0;
    bool m_baselineReady = false;
    qint64 m_monitorStartMs = 0;
    QSet<QString> m_baseline;
    QSet<QString> m_emitted;

    // level.dat 缓存（内容变化才重新解析）
    qint64 m_levelDatMtime = 0;
    bool m_levelDatChecked = false;
    bool m_levelDatSurvival = false;
};
