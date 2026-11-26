/**
 * @file   PlaytimeStats.h
 * @brief  游玩时长统计：按进程启停累计每个实例的游玩时长，并与方块盒子账户打通
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 数据分两层：
 *   1. 本机：每次「游戏真跑起来 → 退出」记一条会话，按年 / 月 / 日 / 实例聚合，
 *      存在数据目录的 playtime/stats.json 里（QSaveFile 原子写，断网、崩溃都不丢）。
 *      启动器即使没登录也照常统计，登录后自动把没同步过的会话补传到账号上。
 *   2. 账号：登录方块盒子账户后，会话按唯一 id 增量上传（服务端按 id 去重，
 *      重复补传不会重复计数），于是换电脑也能看到同一份累计数据；
 *      年度报告优先用账号云端数据生成，没登录就退回本机数据。
 *
 * 「年度报告」用 annualReport(year) 取本机年度数据，登录后可用
 * fetchAnnualReport(year, ...) 拿到含其他设备、含游戏成就数的完整版本。
 */
#pragma once

#include <QDate>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QTimer;

/** 一天的游玩量（折线 / 柱状图用） */
struct PlaytimeDayBucket
{
    QDate day;
    qint64 seconds = 0;
    int sessions = 0;
};

/** 一个实例的游玩量（排行榜用） */
struct PlaytimeInstanceBucket
{
    QString path;      ///< 实例目录（绝对路径，作为聚合键）
    QString name;      ///< 实例显示名（版本目录名）
    QString version;   ///< 游戏版本
    QString loader;    ///< 加载器（可能为空）
    qint64 seconds = 0;
    int sessions = 0;
    qint64 lastPlayedAt = 0;  ///< 最后游玩时间（unix 秒）
};

/** 一次游玩会话 */
struct PlaytimeSessionRecord
{
    QString id;            ///< 全局唯一 id（QUuid，上传时作为幂等键）
    QString instancePath;  ///< 实例目录
    QString instanceName;  ///< 实例显示名
    QString version;       ///< 游戏版本
    QString loader;        ///< 加载器
    QString saveName;      ///< 单机存档名（联机 / 未识别时为空）
    qint64 startAt = 0;    ///< 开始时间（unix 秒）
    qint64 endAt = 0;      ///< 结束时间（unix 秒）
    qint64 seconds = 0;    ///< 本次时长
    int roomId = 0;        ///< 方块盒子联机房间号（0 = 不是联机）
    bool synced = false;   ///< 是否已上传到账号

    bool isValid() const { return !id.isEmpty() && seconds > 0; }
};

/** 「我的 MC 年度报告」所需的一整年数据 */
struct PlaytimeAnnualReport
{
    int year = 0;
    qint64 totalSeconds = 0;
    int sessionCount = 0;       ///< 这一年玩了多少次
    int launchCount = 0;        ///< 启动次数（与 sessionCount 同源，单列便于文案）
    int activeDays = 0;         ///< 这一年有几天在玩
    qint64 longestSessionSeconds = 0;
    QDate longestSessionDay;    ///< 最长那一次是哪天
    QString favoriteInstance;   ///< 玩得最久的实例名
    qint64 favoriteInstanceSeconds = 0;
    QString favoriteVersion;    ///< 玩得最久的版本
    int favoriteHour = -1;      ///< 最常开玩的时段（0~23，-1 = 数据不足）
    int favoriteWeekday = -1;   ///< 最常玩的星期（1=周一 … 7=周日，-1 = 数据不足）
    int busiestMonth = 0;       ///< 最活跃的月份（1~12，0 = 数据不足）
    qint64 busiestMonthSeconds = 0;
    QVector<PlaytimeDayBucket> monthly;  ///< 12 个月（day 为该月 1 号），缺月补 0
    QDate firstPlayedOn;        ///< 这一年第一次玩
    QDate lastPlayedOn;         ///< 这一年最后一次玩
    int achievementCount = -1;  ///< 这一年点亮的游戏成就数（-1 = 未登录 / 拿不到）
    int medalCount = -1;        ///< 这一年拿到的勋章数（-1 = 未登录 / 拿不到）
    bool fromAccount = false;   ///< true = 数据来自账号云端（含其他设备）

    bool hasData() const { return totalSeconds > 0 || sessionCount > 0; }
};

/** 账号云端累计（统计页顶部「云端累计」卡片用） */
struct PlaytimeCloudSummary
{
    bool valid = false;           ///< 是否成功拿到过
    qint64 lifetimeSeconds = 0;   ///< 账号下所有年份合计
    int sessionCount = 0;
    int activeDays = 0;
    QDate firstDay;
    QDate lastDay;
    int achievementCount = -1;
    int medalCount = -1;
    qint64 fetchedAt = 0;         ///< 拉取时间（unix 秒）
};

class PlaytimeStatsService : public QObject
{
    Q_OBJECT

public:
    static PlaytimeStatsService *instance();
    ~PlaytimeStatsService() override;

    // ───────────────────────── 会话采集 ─────────────────────────

    /**
     * @brief 游戏真正跑起来了：开始记一次会话
     *
     * 同一次会话里重复调用是安全的（沿用第一条，只补空字段）。
     * 每 60 秒心跳一次落盘，异常退出时下次启动按最后心跳结算，不丢整段时长。
     *
     * @param instancePath 实例目录（聚合主键）
     * @param instanceName 实例显示名
     * @param version      游戏版本
     * @param loader       加载器名（可空）
     * @param roomId       方块盒子联机房间号（0 = 单机 / 外部服务器）
     */
    void beginSession(const QString &instancePath, const QString &instanceName,
                      const QString &version, const QString &loader, int roomId = 0);

    /** 游戏退出：结算并落盘，同时尝试把没同步的会话补传到账号 */
    void endSession();

    /** 本次会话进行中（游戏正在跑） */
    bool sessionActive() const { return m_session.active; }

    /** 本次会话已进行的秒数（没在玩返回 0） */
    qint64 currentSessionSeconds() const;

    /** 游戏中识别到存档名时补进来（单机才有） */
    void setCurrentSaveName(const QString &saveName);

    // ───────────────────────── 本机汇总 ─────────────────────────

    qint64 totalSeconds() const;                  ///< 本机累计（所有年份）
    qint64 secondsInRange(const QDate &from, const QDate &to) const;
    qint64 todaySeconds() const;
    qint64 weekSeconds() const;                   ///< 最近 7 天（含今天）
    qint64 monthSeconds() const;                  ///< 最近 30 天（含今天）
    int totalLaunchCount() const;
    int totalSessionCount() const;
    int activeDayCount() const;                   ///< 有游玩记录的天数
    QDate firstPlayedDay() const;
    QDate lastPlayedDay() const;

    /**
     * @brief 最近 days 天的每日时长（含今天，末尾是今天；没玩的那天补 0）
     * @param days 天数，钳在 1~90
     */
    QVector<PlaytimeDayBucket> dailyBuckets(int days) const;

    /** 实例排行榜（按累计时长倒序） */
    QVector<PlaytimeInstanceBucket> instanceRanking(int limit = 10) const;

    /** 最近的会话（新的在前） */
    QVector<PlaytimeSessionRecord> recentSessions(int limit = 20) const;

    /** 有数据的年份（倒序） */
    QVector<int> availableYears() const;

    /** 本机数据生成的年度报告 */
    PlaytimeAnnualReport annualReport(int year) const;

    // ───────────────────────── 账号打通 ─────────────────────────

    /** 当前是否已登录方块盒子账户（数据会同步到账号上） */
    bool accountBound() const;

    /** 还没上传到账号的会话条数 */
    int pendingSyncCount() const;

    /** 最近一次同步成功时间（unix 秒，0 = 从没成功过） */
    qint64 lastSyncAt() const { return m_lastSyncAt; }

    /** 最近一次拿到的云端累计 */
    const PlaytimeCloudSummary &cloudSummary() const { return m_cloud; }

    /**
     * @brief 把没同步的会话增量上传到账号
     * @param silent true = 后台自动同步，不做任何界面提示
     *
     * 未登录、没有待同步数据、或者正在同步时直接返回。
     * 成功后会发 syncStateChanged()。
     */
    void syncNow(bool silent = true);

    /** 拉取账号云端累计（未登录直接返回；失败保留旧数据） */
    void fetchCloudSummary();

    /**
     * @brief 拉取账号云端的某一年年度报告（含其他设备、含成就 / 勋章数）
     * @param year 年份，<= 0 表示今年
     * @param onOk 成功回调（数据已填好 fromAccount = true）
     * @param onFail 失败回调（可空）
     */
    void fetchAnnualReport(int year, std::function<void(const PlaytimeAnnualReport &)> onOk,
                           std::function<void()> onFail = nullptr);

    // ───────────────────────── 存储 / 测试 ─────────────────────────

    /** 本地统计文件完整路径（默认 <数据目录>/playtime/stats.json） */
    QString dataFilePath() const;

    /** 重新从磁盘读取（探针 / 外部改动后用） */
    bool reloadFromDisk();

    /**
     * @brief 覆盖统计文件路径（仅测试 / 探针用；传空串恢复默认）
     *
     * 需在第一次访问 instance() 之后、读取数据之前调用；会重新加载。
     */
    static void setDataFileOverrideForTesting(const QString &path);

signals:
    /** 一次会话开始（游戏刚跑起来） */
    void sessionStarted();
    /** 一次会话结束，seconds 为本次时长 */
    void sessionEnded(int seconds);
    /** 本机汇总发生变化（每次会话结束 / 心跳落盘后） */
    void statsChanged();
    /** 账号同步状态或云端数据变化（同步成功 / 拉取到云端累计） */
    void syncStateChanged();

private:
    explicit PlaytimeStatsService(QObject *parent = nullptr);

    struct ActiveSession
    {
        bool active = false;
        QString id;
        QString instancePath;
        QString instanceName;
        QString version;
        QString loader;
        QString saveName;
        qint64 startAt = 0;      ///< unix 秒
        qint64 heartbeatAt = 0;  ///< 最后落盘时间（unix 秒）
        int roomId = 0;
        bool hasData = false;
    };

    struct YearBucket
    {
        qint64 seconds = 0;
        int sessions = 0;
        int launches = 0;
        QHash<QString, qint64> daySeconds;      ///< "yyyy-MM-dd" → 秒
        QHash<QString, int> daySessions;
        QHash<QString, qint64> instanceSeconds; ///< 实例路径 → 秒
        QHash<QString, int> instanceSessions;
        QVector<qint64> hourSeconds;            ///< 24 个
        QVector<qint64> weekdaySeconds;         ///< 7 个（0=周一）
        qint64 longestSeconds = 0;
        QString longestDay;
        QString longestInstance;
        QDate firstDay;
        QDate lastDay;
    };

    struct InstanceMeta
    {
        QString name;
        QString version;
        QString loader;
        qint64 lastPlayedAt = 0;
    };

    void load();
    void save();
    void heartbeat();
    /** 取某年的聚合桶（顺手把 24 小时 / 7 星期的数组补成全 0） */
    YearBucket &yearBucket(int year);
    /** 把一次会话累加进年 / 实例聚合（不落盘） */
    void applySession(const PlaytimeSessionRecord &record);
    void finalizeAbandonedSession();
    /** 游戏中识别当前实例最近改动的存档名（单机才有，找不到返回空） */
    static QString detectActiveSaveName(const QString &instancePath);

    ActiveSession m_session;
    QHash<int, YearBucket> m_years;
    QHash<QString, InstanceMeta> m_instances;   ///< 实例路径 → 展示信息
    QVector<PlaytimeSessionRecord> m_sessions;  ///< 最近会话（新的在前，最多保留 2000 条）
    int m_totalLaunchCount = 0;
    int m_totalSessionCount = 0;
    qint64 m_totalSeconds = 0;
    QDate m_firstDay;
    QDate m_lastDay;
    bool m_loaded = false;
    bool m_dirty = false;
    QTimer *m_heartbeatTimer = nullptr;

    // 账号同步
    qint64 m_lastSyncAt = 0;
    bool m_syncing = false;
    PlaytimeCloudSummary m_cloud;
};

// ───────────────────────────── 文案工具 ─────────────────────────────

namespace Playtime
{
/** 「2 小时 13 分钟」/「58 分钟」/「不到 1 分钟」 */
QString formatDuration(qint64 seconds);

/** 「1234.5 小时」（年度报告大数字用；不足 1 小时显示分钟） */
QString formatHours(qint64 seconds);

/** 「周三」（dow: 1=周一 … 7=周日，0 / 越界返回空串） */
QString weekdayName(int dow);

/** 「20:00 ~ 21:00」（hour 0~23，越界返回空串） */
QString hourRangeText(int hour);
} // namespace Playtime
