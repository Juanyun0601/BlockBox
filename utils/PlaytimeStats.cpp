/**
 * @file   PlaytimeStats.cpp
 * @brief  游玩时长统计实现：会话采集、本机聚合、年度报告与账号同步
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 数据全部落在 <数据目录>/playtime/stats.json：
 *   - meta      累计数字（第一次 / 最后一次 / 启动次数 / 会话数 / 最后同步时间）
 *   - years     按年聚合（日、实例、小时、星期、最长一次都预聚合好，界面不用再算）
 *   - sessions  最近 2000 条会话（新的在前，只裁剪已经同步过的老记录）
 *   - pending   正在进行的会话（60 秒心跳落盘，异常退出后按最后心跳结算）
 *
 * 会话秒数一律用 QDateTime::currentSecsSinceEpoch() 差值：系统时间被改过也不会算出
 * 离谱的数字（负数按 0，单条最多 72 小时），只依赖 Qt，安卓端一样能编译。
 */

#include "PlaytimeStats.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include <algorithm>

#include "../platform.h"
#include "ForumAccountService.h"

namespace {

/** 本地统计文件格式版本 */
constexpr int kFileVersion = 1;
/** 最近会话最多保留多少条（超出只裁剪已经同步的） */
constexpr int kMaxSessions = 2000;
/** 心跳间隔：60 秒落一次盘，崩溃后最多丢一分钟 */
constexpr int kHeartbeatIntervalMs = 60 * 1000;
/** 单条会话上限 72 小时（防系统时间跳变） */
constexpr qint64 kMaxSessionSeconds = 72LL * 60 * 60;
/** 不到 5 秒的会话不记（手滑点了一下启动） */
constexpr qint64 kMinSessionSeconds = 5;
/** 一次上传最多带多少条会话 */
constexpr int kMaxSyncBatch = 200;
/** dailyBuckets 的天数上限 */
constexpr int kMaxDailyBuckets = 90;
/** 单机存档的 level.dat 多久内写过才算「这一局在玩」（游戏自动保存约 45 秒一次） */
constexpr qint64 kActiveSaveFreshMs = 15LL * 60 * 1000;

/** 实例路径为空时的合成聚合键（不会和真实目录重名） */
const QString kDefaultInstanceKey = QStringLiteral("__default__");

/** 测试 / 探针覆盖的统计文件路径（空 = 用数据目录里的默认路径） */
QString g_dataFileOverride;

// ───────────────────────── 通用小工具 ─────────────────────────

QString dayKey(const QDate &date)
{
    return date.toString(QStringLiteral("yyyy-MM-dd"));
}

QDate dateFromKey(const QString &text)
{
    return QDate::fromString(text, QStringLiteral("yyyy-MM-dd"));
}

qint64 valueInt64(const QJsonObject &object, const QString &key)
{
    return static_cast<qint64>(object.value(key).toDouble());
}

QJsonArray int64ArrayToJson(const QVector<qint64> &values)
{
    QJsonArray array;
    for (qint64 value : values)
        array.append(static_cast<double>(value));
    return array;
}

/** 读定长数组（小时 24 项 / 星期 7 项），缺的补 0，多的截掉 */
QVector<qint64> int64ArrayFromJson(const QJsonValue &value, int size)
{
    QVector<qint64> values(size, 0);
    const QJsonArray array = value.toArray();
    const int count = qMin(size, int(array.size()));
    for (int i = 0; i < count; ++i)
        values[i] = static_cast<qint64>(array.at(i).toDouble());
    return values;
}

/** 找最大值下标（全 0 / 空数组返回 -1，对应「数据不足」） */
int argMaxIndex(const QVector<qint64> &values)
{
    int best = -1;
    qint64 bestValue = 0;
    for (int i = 0; i < int(values.size()); ++i)
    {
        if (values.at(i) > bestValue)
        {
            bestValue = values.at(i);
            best = i;
        }
    }
    return best;
}

/** 实例聚合键：统一分隔符，空路径给一个合成键，避免所有会话挤进同一个空串键 */
QString instanceKeyFor(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return kDefaultInstanceKey;
    const QString clean = QDir::cleanPath(trimmed);
    return clean.isEmpty() ? kDefaultInstanceKey : clean;
}

/** 实例显示名兜底：传进来的名字 → 目录名 → 「Minecraft」 */
QString instanceDisplayName(const QString &path, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty())
        return trimmed;
    if (path != kDefaultInstanceKey)
    {
        const QString fileName = QFileInfo(path).fileName();
        if (!fileName.isEmpty())
            return fileName;
    }
    return QStringLiteral("Minecraft");
}

/** 会话按「结束日」归日：跨天会话记在结束那天，简单可预期 */
QDate sessionEndDay(const PlaytimeSessionRecord &record)
{
    const qint64 endAt = record.endAt > 0 ? record.endAt : record.startAt + record.seconds;
    const QDate endDay = QDateTime::fromSecsSinceEpoch(endAt).date();
    if (endDay.isValid())
        return endDay;
    return QDateTime::fromSecsSinceEpoch(record.startAt).date();
}

/** 会话开始时间的小时（0~23，时间异常按 0） */
int sessionStartHour(const PlaytimeSessionRecord &record)
{
    const QDateTime start = QDateTime::fromSecsSinceEpoch(record.startAt);
    return start.isValid() ? start.time().hour() : 0;
}

/** 会话开始时间是星期几（1=周一 … 7=周日，时间异常按 0） */
int sessionStartWeekday(const PlaytimeSessionRecord &record)
{
    const QDateTime start = QDateTime::fromSecsSinceEpoch(record.startAt);
    return start.isValid() ? start.date().dayOfWeek() : 0;
}

// ───────────────────────── 会话序列化 ─────────────────────────

QJsonObject sessionToJson(const PlaytimeSessionRecord &record)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), record.id);
    object.insert(QStringLiteral("start"), static_cast<double>(record.startAt));
    object.insert(QStringLiteral("end"), static_cast<double>(record.endAt));
    object.insert(QStringLiteral("seconds"), static_cast<double>(record.seconds));
    object.insert(QStringLiteral("instance_path"), record.instancePath);
    object.insert(QStringLiteral("instance_name"), record.instanceName);
    object.insert(QStringLiteral("version"), record.version);
    object.insert(QStringLiteral("loader"), record.loader);
    object.insert(QStringLiteral("save"), record.saveName);
    object.insert(QStringLiteral("room_id"), record.roomId);
    object.insert(QStringLiteral("synced"), record.synced);
    return object;
}

bool sessionFromJson(const QJsonObject &object, PlaytimeSessionRecord &record)
{
    record = PlaytimeSessionRecord();
    record.id = object.value(QStringLiteral("id")).toString();
    record.instancePath = object.value(QStringLiteral("instance_path")).toString();
    record.instanceName = object.value(QStringLiteral("instance_name")).toString();
    record.version = object.value(QStringLiteral("version")).toString();
    record.loader = object.value(QStringLiteral("loader")).toString();
    record.saveName = object.value(QStringLiteral("save")).toString();
    record.startAt = valueInt64(object, QStringLiteral("start"));
    record.endAt = valueInt64(object, QStringLiteral("end"));
    record.seconds = valueInt64(object, QStringLiteral("seconds"));
    record.roomId = object.value(QStringLiteral("room_id")).toInt();
    record.synced = object.value(QStringLiteral("synced")).toBool();

    if (record.seconds < 0)
        record.seconds = 0;
    if (record.seconds > kMaxSessionSeconds)
        record.seconds = kMaxSessionSeconds;
    if (record.startAt <= 0)
        return false;
    if (record.endAt <= 0)
        record.endAt = record.startAt + record.seconds;
    return record.isValid();
}

/** 超出上限时只删「已经同步」的老记录：没上传的一条都不动 */
void trimSessionList(QVector<PlaytimeSessionRecord> &sessions)
{
    while (sessions.size() > kMaxSessions)
    {
        int dropIndex = -1;
        for (int i = int(sessions.size()) - 1; i >= 0; --i)
        {
            if (sessions.at(i).synced)
            {
                dropIndex = i;
                break;
            }
        }
        if (dropIndex < 0)
            break;
        sessions.removeAt(dropIndex);
    }
}

/** 年度报告的 12 个月骨架（day = 该月 1 号，缺月补 0） */
QVector<PlaytimeDayBucket> emptyMonthlyBuckets(int year)
{
    QVector<PlaytimeDayBucket> monthly;
    monthly.reserve(12);
    for (int month = 1; month <= 12; ++month)
    {
        PlaytimeDayBucket bucket;
        bucket.day = QDate(year, month, 1);
        monthly.append(bucket);
    }
    return monthly;
}

// ───────────────────────── 云端响应解析 ─────────────────────────

/**
 * @brief 把 /api/playtime/stats 的返回填成年度报告
 *
 * 服务端没给的字段保持 -1 / 空；monthly 一定是 12 项，缺失的月份补 0。
 */
PlaytimeAnnualReport annualReportFromCloud(const QJsonObject &root, int requestedYear)
{
    PlaytimeAnnualReport report;
    report.fromAccount = true;
    report.year = root.value(QStringLiteral("year")).toInt();
    if (report.year <= 0)
        report.year = requestedYear;
    report.monthly = emptyMonthlyBuckets(report.year);

    const QJsonObject stats = root.value(QStringLiteral("year_stats")).toObject();
    report.totalSeconds = valueInt64(stats, QStringLiteral("seconds"));
    report.sessionCount = stats.value(QStringLiteral("sessions")).toInt();
    // 服务端不单列启动次数：按会话数填，界面文案本来也是同源的
    report.launchCount = report.sessionCount;
    report.activeDays = stats.value(QStringLiteral("active_days")).toInt();
    report.firstPlayedOn = dateFromKey(stats.value(QStringLiteral("first_day")).toString());
    report.lastPlayedOn = dateFromKey(stats.value(QStringLiteral("last_day")).toString());

    const QJsonObject longest = stats.value(QStringLiteral("longest")).toObject();
    report.longestSessionSeconds = valueInt64(longest, QStringLiteral("seconds"));
    report.longestSessionDay = dateFromKey(longest.value(QStringLiteral("day")).toString());

    // 12 个月
    const QJsonArray monthly = stats.value(QStringLiteral("monthly")).toArray();
    for (int i = 0; i < 12 && i < int(monthly.size()); ++i)
        report.monthly[i].seconds = static_cast<qint64>(monthly.at(i).toDouble());

    // 最常玩的时段（0~23）
    const QVector<qint64> hourSeconds =
        int64ArrayFromJson(stats.value(QStringLiteral("hours")), 24);
    report.favoriteHour = argMaxIndex(hourSeconds);

    // 最常玩的星期（0=周一 → 报告里用 1=周一 … 7=周日）
    const QVector<qint64> weekdaySeconds =
        int64ArrayFromJson(stats.value(QStringLiteral("weekdays")), 7);
    const int weekdayIndex = argMaxIndex(weekdaySeconds);
    report.favoriteWeekday = weekdayIndex < 0 ? -1 : weekdayIndex + 1;

    // 最活跃的月份
    int busiestMonth = -1;
    qint64 busiestSeconds = 0;
    for (int i = 0; i < report.monthly.size(); ++i)
    {
        if (report.monthly.at(i).seconds > busiestSeconds)
        {
            busiestSeconds = report.monthly.at(i).seconds;
            busiestMonth = i;
        }
    }
    if (busiestMonth >= 0)
    {
        report.busiestMonth = busiestMonth + 1;
        report.busiestMonthSeconds = busiestSeconds;
    }

    // 服务端已按秒数倒序，取第一名当「最爱实例」
    const QJsonArray instances = stats.value(QStringLiteral("instances")).toArray();
    if (!instances.isEmpty())
    {
        const QJsonObject favorite = instances.first().toObject();
        report.favoriteInstance = favorite.value(QStringLiteral("name")).toString();
        report.favoriteVersion = favorite.value(QStringLiteral("version")).toString();
        report.favoriteInstanceSeconds =
            static_cast<qint64>(favorite.value(QStringLiteral("seconds")).toDouble());
    }

    // 成就 / 勋章：服务端没给就保持 -1（界面显示「绑定账户后可见」）
    const QJsonObject achievements = root.value(QStringLiteral("achievements")).toObject();
    if (achievements.contains(QStringLiteral("year")))
        report.achievementCount = achievements.value(QStringLiteral("year")).toInt();
    if (root.contains(QStringLiteral("medals")))
        report.medalCount = root.value(QStringLiteral("medals")).toInt();

    return report;
}

} // namespace

// ───────────────────────── 生命周期 ─────────────────────────

PlaytimeStatsService *PlaytimeStatsService::instance()
{
    static PlaytimeStatsService *s_instance = new PlaytimeStatsService();
    return s_instance;
}

PlaytimeStatsService::PlaytimeStatsService(QObject *parent) : QObject(parent)
{
    load();

    // 心跳：游戏中每 60 秒把 pending 落盘一次，异常退出后最多丢一分钟
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(kHeartbeatIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &PlaytimeStatsService::heartbeat);

    // 登录后补传本机没同步的会话，并顺带拉一次云端累计
    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            [this](const ForumUser &user) {
                if (!user.isValid())
                    return;
                syncNow(true);
                fetchCloudSummary();
            });
}

PlaytimeStatsService::~PlaytimeStatsService()
{
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    // 没走正常退出（没调 endSession）时留一条 pending，下次启动按心跳结算
    if (m_session.active)
        save();
}

// ───────────────────────── 存储 ─────────────────────────

QString PlaytimeStatsService::dataFilePath() const
{
    if (!g_dataFileOverride.isEmpty())
        return g_dataFileOverride;

    QString base = Platform::getDataDirectory();
    while (base.endsWith(QLatin1Char('/')) || base.endsWith(QLatin1Char('\\')))
        base.chop(1);
    return base + QStringLiteral("/playtime/stats.json");
}

void PlaytimeStatsService::setDataFileOverrideForTesting(const QString &path)
{
    const QString cleaned = path.trimmed();
    if (g_dataFileOverride == cleaned)
        return;
    g_dataFileOverride = cleaned;

    PlaytimeStatsService *service = instance();
    service->load();
    emit service->statsChanged();
}

bool PlaytimeStatsService::reloadFromDisk()
{
    const bool exists = QFile::exists(dataFilePath());
    load();
    emit statsChanged();
    return exists;
}

PlaytimeStatsService::YearBucket &PlaytimeStatsService::yearBucket(int year)
{
    YearBucket &bucket = m_years[year];
    if (bucket.hourSeconds.size() != 24)
        bucket.hourSeconds.resize(24);
    if (bucket.weekdaySeconds.size() != 7)
        bucket.weekdaySeconds.resize(7);
    return bucket;
}

void PlaytimeStatsService::load()
{
    m_years.clear();
    m_instances.clear();
    m_sessions.clear();
    m_totalSeconds = 0;
    m_totalSessionCount = 0;
    m_totalLaunchCount = 0;
    m_firstDay = QDate();
    m_lastDay = QDate();
    m_lastSyncAt = 0;
    m_dirty = false;

    QJsonObject meta;
    QFile file(dataFilePath());
    if (file.open(QIODevice::ReadOnly))
    {
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();

        meta = root.value(QStringLiteral("meta")).toObject();
        m_lastSyncAt = valueInt64(meta, QStringLiteral("last_sync_at"));

        // 实例展示信息（名字 / 版本 / 加载器 / 最后游玩时间）
        const QJsonObject instances = root.value(QStringLiteral("instances")).toObject();
        for (auto it = instances.constBegin(); it != instances.constEnd(); ++it)
        {
            const QJsonObject item = it.value().toObject();
            InstanceMeta info;
            info.name = item.value(QStringLiteral("name")).toString();
            info.version = item.value(QStringLiteral("version")).toString();
            info.loader = item.value(QStringLiteral("loader")).toString();
            info.lastPlayedAt = valueInt64(item, QStringLiteral("last"));
            m_instances.insert(it.key(), info);
        }

        // 年度聚合
        const QJsonObject years = root.value(QStringLiteral("years")).toObject();
        for (auto it = years.constBegin(); it != years.constEnd(); ++it)
        {
            bool ok = false;
            const int year = it.key().toInt(&ok);
            if (!ok || year <= 0)
                continue;

            const QJsonObject item = it.value().toObject();
            YearBucket bucket;
            bucket.seconds = valueInt64(item, QStringLiteral("seconds"));
            bucket.sessions = item.value(QStringLiteral("sessions")).toInt();
            bucket.launches = item.value(QStringLiteral("launches")).toInt();

            const QJsonObject days = item.value(QStringLiteral("days")).toObject();
            for (auto dayIt = days.constBegin(); dayIt != days.constEnd(); ++dayIt)
                bucket.daySeconds.insert(dayIt.key(), static_cast<qint64>(dayIt.value().toDouble()));

            const QJsonObject daySessions = item.value(QStringLiteral("day_sessions")).toObject();
            for (auto dayIt = daySessions.constBegin(); dayIt != daySessions.constEnd(); ++dayIt)
                bucket.daySessions.insert(dayIt.key(), dayIt.value().toInt());

            const QJsonObject instanceSeconds = item.value(QStringLiteral("instances")).toObject();
            for (auto instIt = instanceSeconds.constBegin(); instIt != instanceSeconds.constEnd();
                 ++instIt)
            {
                bucket.instanceSeconds.insert(instIt.key(),
                                              static_cast<qint64>(instIt.value().toDouble()));
            }

            const QJsonObject instanceSessions =
                item.value(QStringLiteral("instance_sessions")).toObject();
            for (auto instIt = instanceSessions.constBegin(); instIt != instanceSessions.constEnd();
                 ++instIt)
            {
                bucket.instanceSessions.insert(instIt.key(), instIt.value().toInt());
            }

            bucket.hourSeconds = int64ArrayFromJson(item.value(QStringLiteral("hours")), 24);
            bucket.weekdaySeconds = int64ArrayFromJson(item.value(QStringLiteral("weekdays")), 7);

            const QJsonObject longest = item.value(QStringLiteral("longest")).toObject();
            bucket.longestSeconds = valueInt64(longest, QStringLiteral("seconds"));
            bucket.longestDay = longest.value(QStringLiteral("day")).toString();
            bucket.longestInstance = longest.value(QStringLiteral("instance")).toString();

            bucket.firstDay = dateFromKey(item.value(QStringLiteral("first")).toString());
            bucket.lastDay = dateFromKey(item.value(QStringLiteral("last")).toString());
            m_years.insert(year, bucket);
        }

        // 最近会话
        const QJsonArray sessions = root.value(QStringLiteral("sessions")).toArray();
        m_sessions.reserve(int(sessions.size()));
        for (const QJsonValue &value : sessions)
        {
            PlaytimeSessionRecord record;
            if (!sessionFromJson(value.toObject(), record))
                continue;
            m_sessions.append(record);
        }
        trimSessionList(m_sessions);

        // 文件里只有会话列表、没有年度聚合（外部改动 / 老文件）：从会话补算一遍
        if (m_years.isEmpty() && !m_sessions.isEmpty())
        {
            const QVector<PlaytimeSessionRecord> restored = m_sessions;
            m_sessions.clear();
            m_instances.clear();
            m_totalSeconds = 0;
            m_totalSessionCount = 0;
            m_totalLaunchCount = 0;
            m_firstDay = QDate();
            m_lastDay = QDate();
            // 从最早的一条开始补：applySession 是前插，补完仍然是新的在前
            for (int i = int(restored.size()) - 1; i >= 0; --i)
            {
                const PlaytimeSessionRecord &record = restored.at(i);
                if (!record.isValid())
                    continue;
                applySession(record);
                const QDate bucketDay = sessionEndDay(record);
                if (bucketDay.isValid())
                {
                    yearBucket(bucketDay.year()).launches += 1;
                    m_totalLaunchCount += 1;
                }
            }
        }

        // 累计数字以年度聚合为准（老文件 / 外部改动也能自愈），缺的再用 meta 兜底
        if (!m_years.isEmpty())
        {
            qint64 totalSeconds = 0;
            int totalSessions = 0;
            int totalLaunches = 0;
            QDate firstDay;
            QDate lastDay;
            for (auto it = m_years.constBegin(); it != m_years.constEnd(); ++it)
            {
                totalSeconds += it.value().seconds;
                totalSessions += it.value().sessions;
                totalLaunches += it.value().launches;
                if (it.value().firstDay.isValid()
                    && (!firstDay.isValid() || it.value().firstDay < firstDay))
                {
                    firstDay = it.value().firstDay;
                }
                if (it.value().lastDay.isValid()
                    && (!lastDay.isValid() || it.value().lastDay > lastDay))
                {
                    lastDay = it.value().lastDay;
                }
            }
            m_totalSeconds = totalSeconds;
            m_totalSessionCount = totalSessions;
            m_totalLaunchCount = totalLaunches;
            m_firstDay = firstDay;
            m_lastDay = lastDay;
        }
        if (m_totalLaunchCount <= 0)
            m_totalLaunchCount = meta.value(QStringLiteral("launches")).toInt();
        if (m_totalSessionCount <= 0)
            m_totalSessionCount = meta.value(QStringLiteral("sessions")).toInt();
        if (!m_firstDay.isValid())
            m_firstDay = dateFromKey(meta.value(QStringLiteral("first_day")).toString());
        if (!m_lastDay.isValid())
            m_lastDay = dateFromKey(meta.value(QStringLiteral("last_day")).toString());
    }

    m_loaded = true;

    // 上次异常退出留下的 pending：按最后心跳结算（至少 1 秒），然后清掉
    if (!m_session.active)
        finalizeAbandonedSession();
}

void PlaytimeStatsService::save()
{
    if (!m_loaded)
        return;

    QJsonObject root;
    root.insert(QStringLiteral("version"), kFileVersion);

    QJsonObject meta;
    if (m_firstDay.isValid())
        meta.insert(QStringLiteral("first_day"), dayKey(m_firstDay));
    if (m_lastDay.isValid())
        meta.insert(QStringLiteral("last_day"), dayKey(m_lastDay));
    meta.insert(QStringLiteral("launches"), m_totalLaunchCount);
    meta.insert(QStringLiteral("sessions"), m_totalSessionCount);
    meta.insert(QStringLiteral("last_sync_at"), static_cast<double>(m_lastSyncAt));
    root.insert(QStringLiteral("meta"), meta);

    QJsonObject instances;
    for (auto it = m_instances.constBegin(); it != m_instances.constEnd(); ++it)
    {
        QJsonObject item;
        item.insert(QStringLiteral("name"), it.value().name);
        item.insert(QStringLiteral("version"), it.value().version);
        item.insert(QStringLiteral("loader"), it.value().loader);
        item.insert(QStringLiteral("last"), static_cast<double>(it.value().lastPlayedAt));
        instances.insert(it.key(), item);
    }
    root.insert(QStringLiteral("instances"), instances);

    QJsonObject years;
    for (auto it = m_years.constBegin(); it != m_years.constEnd(); ++it)
    {
        const YearBucket &bucket = it.value();
        QJsonObject item;
        item.insert(QStringLiteral("seconds"), static_cast<double>(bucket.seconds));
        item.insert(QStringLiteral("sessions"), bucket.sessions);
        item.insert(QStringLiteral("launches"), bucket.launches);

        QJsonObject days;
        for (auto dayIt = bucket.daySeconds.constBegin(); dayIt != bucket.daySeconds.constEnd();
             ++dayIt)
        {
            days.insert(dayIt.key(), static_cast<double>(dayIt.value()));
        }
        item.insert(QStringLiteral("days"), days);

        QJsonObject daySessions;
        for (auto dayIt = bucket.daySessions.constBegin(); dayIt != bucket.daySessions.constEnd();
             ++dayIt)
        {
            daySessions.insert(dayIt.key(), dayIt.value());
        }
        item.insert(QStringLiteral("day_sessions"), daySessions);

        QJsonObject instanceSeconds;
        for (auto instIt = bucket.instanceSeconds.constBegin();
             instIt != bucket.instanceSeconds.constEnd(); ++instIt)
        {
            instanceSeconds.insert(instIt.key(), static_cast<double>(instIt.value()));
        }
        item.insert(QStringLiteral("instances"), instanceSeconds);

        QJsonObject instanceSessions;
        for (auto instIt = bucket.instanceSessions.constBegin();
             instIt != bucket.instanceSessions.constEnd(); ++instIt)
        {
            instanceSessions.insert(instIt.key(), instIt.value());
        }
        item.insert(QStringLiteral("instance_sessions"), instanceSessions);

        item.insert(QStringLiteral("hours"), int64ArrayToJson(bucket.hourSeconds));
        item.insert(QStringLiteral("weekdays"), int64ArrayToJson(bucket.weekdaySeconds));

        QJsonObject longest;
        longest.insert(QStringLiteral("seconds"), static_cast<double>(bucket.longestSeconds));
        if (!bucket.longestDay.isEmpty())
            longest.insert(QStringLiteral("day"), bucket.longestDay);
        if (!bucket.longestInstance.isEmpty())
            longest.insert(QStringLiteral("instance"), bucket.longestInstance);
        item.insert(QStringLiteral("longest"), longest);

        if (bucket.firstDay.isValid())
            item.insert(QStringLiteral("first"), dayKey(bucket.firstDay));
        if (bucket.lastDay.isValid())
            item.insert(QStringLiteral("last"), dayKey(bucket.lastDay));

        years.insert(QString::number(it.key()), item);
    }
    root.insert(QStringLiteral("years"), years);

    QJsonArray sessions;
    for (const PlaytimeSessionRecord &record : m_sessions)
        sessions.append(sessionToJson(record));
    root.insert(QStringLiteral("sessions"), sessions);

    // 正在进行中的会话：写 pending，异常退出后下次启动按心跳结算
    if (m_session.active)
    {
        QJsonObject pending;
        pending.insert(QStringLiteral("id"), m_session.id);
        pending.insert(QStringLiteral("start"), static_cast<double>(m_session.startAt));
        pending.insert(QStringLiteral("heartbeat"), static_cast<double>(m_session.heartbeatAt));
        pending.insert(QStringLiteral("instance_path"), m_session.instancePath);
        pending.insert(QStringLiteral("instance_name"), m_session.instanceName);
        pending.insert(QStringLiteral("version"), m_session.version);
        pending.insert(QStringLiteral("loader"), m_session.loader);
        pending.insert(QStringLiteral("save"), m_session.saveName);
        pending.insert(QStringLiteral("room_id"), m_session.roomId);
        root.insert(QStringLiteral("pending"), pending);
    }

    const QString path = dataFilePath();
    const QString dirPath = QFileInfo(path).absolutePath();
    if (!dirPath.isEmpty())
        QDir().mkpath(dirPath);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        qWarning() << "[Playtime] 无法写入统计文件：" << path << file.errorString();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
    {
        qWarning() << "[Playtime] 统计文件原子写入失败：" << path << file.errorString();
        return;
    }
    m_dirty = false;
}

// ───────────────────────── 会话采集 ─────────────────────────

void PlaytimeStatsService::beginSession(const QString &instancePath, const QString &instanceName,
                                        const QString &version, const QString &loader, int roomId)
{
    if (!m_loaded)
        load();

    const QString path = instancePath.trimmed();
    const QString displayName = instanceDisplayName(instanceKeyFor(path), instanceName);
    const QString cleanVersion = version.trimmed();
    const QString cleanLoader = loader.trimmed();
    const int cleanRoomId = qMax(0, roomId);

    // 同一次会话里重复调用：沿用第一条，只补空字段
    if (m_session.active)
    {
        if (m_session.instancePath.isEmpty() && !path.isEmpty())
            m_session.instancePath = path;
        if (m_session.instanceName.isEmpty() && !displayName.isEmpty())
            m_session.instanceName = displayName;
        if (m_session.version.isEmpty() && !cleanVersion.isEmpty())
            m_session.version = cleanVersion;
        if (m_session.loader.isEmpty() && !cleanLoader.isEmpty())
            m_session.loader = cleanLoader;
        if (m_session.roomId <= 0 && cleanRoomId > 0)
            m_session.roomId = cleanRoomId;
        save();
        return;
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    m_session = ActiveSession();
    m_session.active = true;
    m_session.hasData = true;
    m_session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_session.instancePath = path;
    m_session.instanceName = displayName;
    m_session.version = cleanVersion;
    m_session.loader = cleanLoader;
    m_session.saveName = detectActiveSaveName(path);
    m_session.startAt = now;
    m_session.heartbeatAt = now;
    m_session.roomId = cleanRoomId;

    // 启动次数：即便这次会话太短最后不记账，也算启动过一次
    m_totalLaunchCount += 1;
    const QDate startDay = QDateTime::fromSecsSinceEpoch(now).date();
    if (startDay.isValid())
        yearBucket(startDay.year()).launches += 1;

    m_dirty = true;
    save();

    if (m_heartbeatTimer)
        m_heartbeatTimer->start();

    emit sessionStarted();
    emit statsChanged();
}

void PlaytimeStatsService::endSession()
{
    if (!m_session.active)
        return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 seconds = now - m_session.startAt;
    if (seconds < 0)
        seconds = 0; // 系统时间被往回改：按 0 处理，别扣时长
    if (seconds > kMaxSessionSeconds)
        seconds = kMaxSessionSeconds;

    PlaytimeSessionRecord record;
    record.id = m_session.id;
    record.instancePath = m_session.instancePath;
    record.instanceName = m_session.instanceName;
    record.version = m_session.version;
    record.loader = m_session.loader;
    record.saveName = m_session.saveName;
    record.startAt = m_session.startAt;
    record.seconds = seconds;
    record.endAt = record.startAt + seconds;
    record.roomId = m_session.roomId;
    record.synced = false;

    // 先停心跳，再清掉进行中状态：save() 会顺带把 pending 从文件里删掉
    if (m_heartbeatTimer)
        m_heartbeatTimer->stop();
    m_session = ActiveSession();

    // 不到 5 秒不记（手滑点了一下启动），启动次数已经在 beginSession 里留下
    if (seconds < kMinSessionSeconds)
    {
        save();
        return;
    }

    applySession(record);
    save();

    emit sessionEnded(static_cast<int>(record.seconds));
    emit statsChanged();

    // 后台把没同步的会话补传到账号（未登录 / 没数据时 syncNow 自己会返回）
    syncNow(true);
}

qint64 PlaytimeStatsService::currentSessionSeconds() const
{
    if (!m_session.active)
        return 0;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 seconds = now - m_session.startAt;
    if (seconds < 0)
        seconds = 0;
    return qMin(seconds, kMaxSessionSeconds);
}

void PlaytimeStatsService::setCurrentSaveName(const QString &saveName)
{
    if (!m_session.active)
        return;
    const QString clean = saveName.trimmed();
    if (clean.isEmpty() || clean == m_session.saveName)
        return;
    // 与服务端 save 字段限长保持一致
    m_session.saveName = clean.left(60);
    save();
}

void PlaytimeStatsService::heartbeat()
{
    if (!m_session.active)
        return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // 心跳时间不能早于开始时间，否则崩溃恢复会算出负数
    m_session.heartbeatAt = qMax(now, m_session.startAt);

    // 单机才有存档名：每次心跳顺手补一次（联机时找不到就是空）
    if (m_session.saveName.trimmed().isEmpty())
    {
        const QString detected = detectActiveSaveName(m_session.instancePath);
        if (!detected.isEmpty())
            m_session.saveName = detected.left(60);
    }

    m_dirty = true;
    save();
    emit statsChanged();
}

void PlaytimeStatsService::applySession(const PlaytimeSessionRecord &record)
{
    if (!record.isValid())
        return;

    const qint64 safeSeconds = qBound<qint64>(0, record.seconds, kMaxSessionSeconds);
    if (safeSeconds <= 0)
        return;

    const QDate bucketDay = sessionEndDay(record);
    const QDate startDay = QDateTime::fromSecsSinceEpoch(record.startAt).date();
    if (!bucketDay.isValid())
        return;

    PlaytimeSessionRecord stored = record;
    stored.seconds = safeSeconds;
    stored.endAt = stored.startAt + safeSeconds;

    const QString key = instanceKeyFor(stored.instancePath);
    const QString displayName = instanceDisplayName(key, stored.instanceName);

    YearBucket &bucket = yearBucket(bucketDay.year());
    bucket.seconds += safeSeconds;
    bucket.sessions += 1;
    const QString day = dayKey(bucketDay);
    bucket.daySeconds[day] += safeSeconds;
    bucket.daySessions[day] += 1;
    bucket.instanceSeconds[key] += safeSeconds;
    bucket.instanceSessions[key] += 1;

    // 最常玩的时段 / 星期按「会话开始时间」归桶，权重为本次秒数
    if (startDay.isValid())
    {
        const int hour = sessionStartHour(stored);
        if (hour >= 0 && hour < 24)
            bucket.hourSeconds[hour] += safeSeconds;
        const int weekday = sessionStartWeekday(stored);
        if (weekday >= 1 && weekday <= 7)
            bucket.weekdaySeconds[weekday - 1] += safeSeconds;
    }

    if (stored.seconds > bucket.longestSeconds)
    {
        bucket.longestSeconds = stored.seconds;
        bucket.longestDay = day;
        bucket.longestInstance = displayName;
    }
    if (!bucket.firstDay.isValid() || bucketDay < bucket.firstDay)
        bucket.firstDay = bucketDay;
    if (!bucket.lastDay.isValid() || bucketDay > bucket.lastDay)
        bucket.lastDay = bucketDay;

    // 实例展示信息随最后一次会话刷新（名字 / 版本 / 加载器）
    InstanceMeta &info = m_instances[key];
    if (!displayName.isEmpty())
        info.name = displayName;
    if (!stored.version.trimmed().isEmpty())
        info.version = stored.version.trimmed();
    if (!stored.loader.trimmed().isEmpty())
        info.loader = stored.loader.trimmed();
    info.lastPlayedAt = qMax(info.lastPlayedAt, stored.endAt > 0 ? stored.endAt : stored.startAt);

    m_totalSeconds += safeSeconds;
    m_totalSessionCount += 1;
    if (!m_firstDay.isValid() || bucketDay < m_firstDay)
        m_firstDay = bucketDay;
    if (!m_lastDay.isValid() || bucketDay > m_lastDay)
        m_lastDay = bucketDay;

    m_sessions.prepend(stored);
    trimSessionList(m_sessions);
    m_dirty = true;
}

void PlaytimeStatsService::finalizeAbandonedSession()
{
    QFile file(dataFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    const QJsonObject pending = root.value(QStringLiteral("pending")).toObject();
    if (pending.isEmpty())
        return;

    const qint64 startAt = valueInt64(pending, QStringLiteral("start"));
    qint64 heartbeatAt = valueInt64(pending, QStringLiteral("heartbeat"));
    if (startAt <= 0)
        return; // 时间都不对，没法结算
    if (heartbeatAt < startAt)
        heartbeatAt = startAt;

    PlaytimeSessionRecord record;
    record.id = pending.value(QStringLiteral("id")).toString();
    if (record.id.isEmpty())
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.instancePath = pending.value(QStringLiteral("instance_path")).toString();
    record.instanceName = pending.value(QStringLiteral("instance_name")).toString();
    record.version = pending.value(QStringLiteral("version")).toString();
    record.loader = pending.value(QStringLiteral("loader")).toString();
    record.saveName = pending.value(QStringLiteral("save")).toString();
    record.roomId = pending.value(QStringLiteral("room_id")).toInt();
    record.startAt = startAt;
    record.endAt = heartbeatAt;
    // 崩溃恢复：按最后一次心跳结算，至少记 1 秒，避免整段时长白丢
    record.seconds = qBound<qint64>(1, heartbeatAt - startAt, kMaxSessionSeconds);
    record.synced = false;

    applySession(record);
    // 内存里已经没有进行中会话了，save() 不会再写 pending，等于把这条清掉
    save();
    emit statsChanged();
}

QString PlaytimeStatsService::detectActiveSaveName(const QString &instancePath)
{
    const QString base = instancePath.trimmed();
    if (base.isEmpty())
        return QString();

    // 版本隔离 → 存档在实例目录内；非隔离 → 在实例根目录的 saves/ 下（和 GameLauncher 一致）
    const QStringList candidates = {
        QDir::cleanPath(base + QStringLiteral("/saves")),
        QDir::cleanPath(base + QStringLiteral("/../saves")),
        QDir::cleanPath(base + QStringLiteral("/../../saves")),
    };

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const QString &candidate : candidates)
    {
        QDir savesDir(candidate);
        if (!savesDir.exists())
            continue;

        QString bestName;
        qint64 bestMtime = 0;
        const QFileInfoList entries =
            savesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries)
        {
            const QFileInfo levelDat(entry.absoluteFilePath() + QStringLiteral("/level.dat"));
            if (!levelDat.exists() || !levelDat.isFile())
                continue;
            const qint64 mtime = levelDat.lastModified().toMSecsSinceEpoch();
            // 太久没被游戏写过的存档不是这一局在玩的（单机自动保存约 45 秒一次）
            if (nowMs - mtime > kActiveSaveFreshMs)
                continue;
            if (mtime > bestMtime)
            {
                bestMtime = mtime;
                bestName = entry.fileName();
            }
        }
        if (!bestName.isEmpty())
            return bestName;
    }
    return QString();
}

// ───────────────────────── 本机汇总 ─────────────────────────

qint64 PlaytimeStatsService::totalSeconds() const
{
    return m_totalSeconds;
}

qint64 PlaytimeStatsService::secondsInRange(const QDate &from, const QDate &to) const
{
    if (!from.isValid() && !to.isValid())
        return m_totalSeconds;

    qint64 total = 0;
    for (auto it = m_years.constBegin(); it != m_years.constEnd(); ++it)
    {
        const QHash<QString, qint64> &days = it.value().daySeconds;
        for (auto dayIt = days.constBegin(); dayIt != days.constEnd(); ++dayIt)
        {
            const QDate day = dateFromKey(dayIt.key());
            if (!day.isValid())
                continue;
            if (from.isValid() && day < from)
                continue;
            if (to.isValid() && day > to)
                continue;
            total += dayIt.value();
        }
    }
    return total;
}

qint64 PlaytimeStatsService::todaySeconds() const
{
    const QDate today = QDate::currentDate();
    return secondsInRange(today, today);
}

qint64 PlaytimeStatsService::weekSeconds() const
{
    const QDate today = QDate::currentDate();
    return secondsInRange(today.addDays(-6), today);
}

qint64 PlaytimeStatsService::monthSeconds() const
{
    const QDate today = QDate::currentDate();
    return secondsInRange(today.addDays(-29), today);
}

int PlaytimeStatsService::totalLaunchCount() const
{
    return m_totalLaunchCount;
}

int PlaytimeStatsService::totalSessionCount() const
{
    return m_totalSessionCount;
}

int PlaytimeStatsService::activeDayCount() const
{
    int count = 0;
    for (auto it = m_years.constBegin(); it != m_years.constEnd(); ++it)
    {
        const QHash<QString, qint64> &days = it.value().daySeconds;
        for (auto dayIt = days.constBegin(); dayIt != days.constEnd(); ++dayIt)
        {
            if (dayIt.value() > 0)
                ++count;
        }
    }
    return count;
}

QDate PlaytimeStatsService::firstPlayedDay() const
{
    return m_firstDay;
}

QDate PlaytimeStatsService::lastPlayedDay() const
{
    return m_lastDay;
}

QVector<PlaytimeDayBucket> PlaytimeStatsService::dailyBuckets(int days) const
{
    const int count = qBound(1, days, kMaxDailyBuckets);
    QVector<PlaytimeDayBucket> buckets;
    buckets.reserve(count);

    // 末尾固定是今天：offset 从 count-1 递减到 0
    const QDate today = QDate::currentDate();
    for (int offset = count - 1; offset >= 0; --offset)
    {
        const QDate day = today.addDays(-offset);
        PlaytimeDayBucket bucket;
        bucket.day = day;
        const auto yearIt = m_years.constFind(day.year());
        if (yearIt != m_years.constEnd())
        {
            const QString key = dayKey(day);
            bucket.seconds = yearIt.value().daySeconds.value(key);
            bucket.sessions = yearIt.value().daySessions.value(key);
        }
        buckets.append(bucket);
    }
    return buckets;
}

QVector<PlaytimeInstanceBucket> PlaytimeStatsService::instanceRanking(int limit) const
{
    const int max = qBound(1, limit, 100);

    // 跨年合并：同一个实例目录的时长 / 次数累加
    QHash<QString, PlaytimeInstanceBucket> merged;
    for (auto it = m_years.constBegin(); it != m_years.constEnd(); ++it)
    {
        const YearBucket &year = it.value();
        for (auto instIt = year.instanceSeconds.constBegin();
             instIt != year.instanceSeconds.constEnd(); ++instIt)
        {
            PlaytimeInstanceBucket &bucket = merged[instIt.key()];
            bucket.path = instIt.key();
            bucket.seconds += instIt.value();
            bucket.sessions += year.instanceSessions.value(instIt.key());
        }
    }

    for (auto it = merged.begin(); it != merged.end(); ++it)
    {
        const InstanceMeta info = m_instances.value(it.key());
        it.value().name = instanceDisplayName(it.key(), info.name);
        it.value().version = info.version;
        it.value().loader = info.loader;
        it.value().lastPlayedAt = info.lastPlayedAt;
    }

    QVector<PlaytimeInstanceBucket> list;
    list.reserve(merged.size());
    for (auto it = merged.constBegin(); it != merged.constEnd(); ++it)
    {
        if (it.value().seconds > 0)
            list.append(it.value());
    }
    std::sort(list.begin(), list.end(),
              [](const PlaytimeInstanceBucket &left, const PlaytimeInstanceBucket &right) {
                  if (left.seconds != right.seconds)
                      return left.seconds > right.seconds;
                  return left.name < right.name;
              });
    if (list.size() > max)
        list.resize(max);
    return list;
}

QVector<PlaytimeSessionRecord> PlaytimeStatsService::recentSessions(int limit) const
{
    const int count = limit <= 0 ? int(m_sessions.size())
                                 : qMin(limit, int(m_sessions.size()));
    QVector<PlaytimeSessionRecord> list;
    list.reserve(count);
    for (int i = 0; i < count; ++i)
        list.append(m_sessions.at(i));
    return list;
}

QVector<int> PlaytimeStatsService::availableYears() const
{
    QVector<int> years;
    for (auto it = m_years.constBegin(); it != m_years.constEnd(); ++it)
    {
        if (it.value().seconds > 0 || it.value().sessions > 0)
            years.append(it.key());
    }
    std::sort(years.begin(), years.end(), [](int left, int right) { return left > right; });
    return years;
}

PlaytimeAnnualReport PlaytimeStatsService::annualReport(int year) const
{
    if (year <= 0)
        year = QDate::currentDate().year();

    PlaytimeAnnualReport report;
    report.year = year;
    report.monthly = emptyMonthlyBuckets(year);

    const auto yearIt = m_years.constFind(year);
    if (yearIt == m_years.constEnd())
        return report;

    const YearBucket &bucket = yearIt.value();
    report.totalSeconds = bucket.seconds;
    report.sessionCount = bucket.sessions;
    report.launchCount = bucket.launches;
    report.firstPlayedOn = bucket.firstDay;
    report.lastPlayedOn = bucket.lastDay;
    report.longestSessionSeconds = bucket.longestSeconds;
    report.longestSessionDay = dateFromKey(bucket.longestDay);

    // 每日数据汇总到 12 个月（缺月补 0），顺手数一下活跃天数
    int activeDays = 0;
    for (auto dayIt = bucket.daySeconds.constBegin(); dayIt != bucket.daySeconds.constEnd();
         ++dayIt)
    {
        const QDate day = dateFromKey(dayIt.key());
        if (!day.isValid() || day.year() != year)
            continue;
        if (dayIt.value() > 0)
            ++activeDays;
        report.monthly[day.month() - 1].seconds += dayIt.value();
        report.monthly[day.month() - 1].sessions += bucket.daySessions.value(dayIt.key());
    }
    report.activeDays = activeDays;

    // 玩得最久的实例
    QString favoriteKey;
    qint64 favoriteSeconds = 0;
    for (auto instIt = bucket.instanceSeconds.constBegin();
         instIt != bucket.instanceSeconds.constEnd(); ++instIt)
    {
        if (instIt.value() > favoriteSeconds)
        {
            favoriteSeconds = instIt.value();
            favoriteKey = instIt.key();
        }
    }
    if (!favoriteKey.isEmpty())
    {
        const InstanceMeta info = m_instances.value(favoriteKey);
        report.favoriteInstance = instanceDisplayName(favoriteKey, info.name);
        report.favoriteVersion = info.version;
        report.favoriteInstanceSeconds = favoriteSeconds;
    }

    // 最常玩的时段 / 星期：按会话开始时间归桶，权重为秒；没数据返回 -1
    report.favoriteHour = argMaxIndex(bucket.hourSeconds);
    const int weekdayIndex = argMaxIndex(bucket.weekdaySeconds);
    report.favoriteWeekday = weekdayIndex < 0 ? -1 : weekdayIndex + 1;

    // 最活跃的月份
    int busiestMonth = -1;
    qint64 busiestSeconds = 0;
    for (int i = 0; i < report.monthly.size(); ++i)
    {
        if (report.monthly.at(i).seconds > busiestSeconds)
        {
            busiestSeconds = report.monthly.at(i).seconds;
            busiestMonth = i;
        }
    }
    if (busiestMonth >= 0)
    {
        report.busiestMonth = busiestMonth + 1;
        report.busiestMonthSeconds = busiestSeconds;
    }

    return report;
}

// ───────────────────────── 账号打通 ─────────────────────────

bool PlaytimeStatsService::accountBound() const
{
    return ForumAccountService::instance()->isLoggedIn();
}

int PlaytimeStatsService::pendingSyncCount() const
{
    int count = 0;
    for (const PlaytimeSessionRecord &record : m_sessions)
    {
        if (!record.synced)
            ++count;
    }
    return count;
}

void PlaytimeStatsService::syncNow(bool silent)
{
    if (m_syncing || !accountBound())
        return;

    // 只发没同步的会话，一次最多 200 条
    QVector<PlaytimeSessionRecord> batch;
    for (const PlaytimeSessionRecord &record : m_sessions)
    {
        if (record.synced || !record.isValid())
            continue;
        batch.append(record);
        if (batch.size() >= kMaxSyncBatch)
            break;
    }
    if (batch.isEmpty())
        return;

    QJsonArray sessions;
    for (const PlaytimeSessionRecord &record : batch)
    {
        const QDate day = sessionEndDay(record);
        QJsonObject item;
        item.insert(QStringLiteral("id"), record.id);
        item.insert(QStringLiteral("day"), dayKey(day));
        item.insert(QStringLiteral("start_at"), static_cast<double>(record.startAt));
        item.insert(QStringLiteral("seconds"), static_cast<double>(record.seconds));
        item.insert(QStringLiteral("instance"), record.instanceName.left(80));
        item.insert(QStringLiteral("version"), record.version.left(40));
        item.insert(QStringLiteral("save"), record.saveName.left(60));
        item.insert(QStringLiteral("hour"), sessionStartHour(record));
        // dow：服务端 0=周一 … 6=周日
        const int weekday = sessionStartWeekday(record);
        item.insert(QStringLiteral("dow"), weekday >= 1 ? weekday - 1 : 0);
        sessions.append(item);
    }

    m_syncing = true;
    QJsonObject body;
    body.insert(QStringLiteral("sessions"), sessions);

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/playtime/sync"), body,
        [this, batch](const QJsonObject &root) {
            m_syncing = false;
            const QJsonValue ok = root.value(QStringLiteral("ok"));
            if (ok.isBool() && !ok.toBool())
            {
                qWarning() << "[Playtime] 同步被服务端拒绝，下次再试";
                return;
            }

            // 服务端按 id 去重，这里按发送过的 id 原地标记，不用重传
            QSet<QString> sentIds;
            for (const PlaytimeSessionRecord &record : batch)
                sentIds.insert(record.id);
            int marked = 0;
            for (PlaytimeSessionRecord &record : m_sessions)
            {
                if (!record.synced && sentIds.contains(record.id))
                {
                    record.synced = true;
                    ++marked;
                }
            }

            m_lastSyncAt = QDateTime::currentSecsSinceEpoch();
            m_dirty = true;
            save();
            emit syncStateChanged();

            // 还有没传完的：歇一秒继续下一批（服务端限流 120 次/小时）
            if (marked > 0 && pendingSyncCount() > 0)
                QTimer::singleShot(1000, this, [this]() { syncNow(true); });
        },
        [this, silent](const QString &message, int status) {
            m_syncing = false;
            // 失败静默：下次会话结束再试；只有手动同步才留一条日志方便排查
            if (!silent)
                qWarning() << "[Playtime] 同步失败：" << message << status;
        });
}

void PlaytimeStatsService::fetchCloudSummary()
{
    if (!accountBound())
        return;

    const int year = QDate::currentDate().year();
    const QString path = QStringLiteral("/api/playtime/stats?year=%1").arg(year);

    ForumAccountService::instance()->getJson(
        path,
        [this](const QJsonObject &root) {
            // 老版本论坛没有这个接口（或反代返回了一个成功但内容不对的页面）时，
            // 不能把「空对象」当成「云端累计 0 分钟」，宁可保持未拉取状态
            if (!root.contains(QStringLiteral("year_stats")) &&
                !root.contains(QStringLiteral("lifetime")))
            {
                qWarning() << "[Playtime] 云端统计接口返回内容不符合预期，本次忽略";
                return;
            }
            m_cloud.valid = true;
            m_cloud.lifetimeSeconds = valueInt64(root, QStringLiteral("lifetime"));
            m_cloud.sessionCount = root.value(QStringLiteral("total_sessions")).toInt();
            m_cloud.activeDays = root.value(QStringLiteral("active_days")).toInt();
            m_cloud.firstDay = dateFromKey(root.value(QStringLiteral("first_day")).toString());
            m_cloud.lastDay = dateFromKey(root.value(QStringLiteral("last_day")).toString());

            const QJsonObject achievements = root.value(QStringLiteral("achievements")).toObject();
            m_cloud.achievementCount =
                achievements.contains(QStringLiteral("total"))
                    ? achievements.value(QStringLiteral("total")).toInt()
                    : -1;
            m_cloud.medalCount =
                root.contains(QStringLiteral("medals"))
                    ? root.value(QStringLiteral("medals")).toInt()
                    : -1;
            m_cloud.fetchedAt = QDateTime::currentSecsSinceEpoch();
            emit syncStateChanged();
        },
        [](const QString &message, int status) {
            // 拉取失败保留旧数据，不打扰用户
            qWarning() << "[Playtime] 拉取云端累计失败：" << message << status;
        });
}

void PlaytimeStatsService::fetchAnnualReport(
    int year, std::function<void(const PlaytimeAnnualReport &)> onOk,
    std::function<void()> onFail)
{
    if (year <= 0)
        year = QDate::currentDate().year();

    // 没登录：让界面退回本机数据
    if (!accountBound())
    {
        if (onFail)
            onFail();
        return;
    }

    const int requestedYear = year;
    const QString path = QStringLiteral("/api/playtime/stats?year=%1").arg(requestedYear);

    ForumAccountService::instance()->getJson(
        path,
        [requestedYear, onOk, onFail](const QJsonObject &root) {
            // 接口不存在 / 返回内容不对：按失败处理，界面退回本机年度数据
            if (!root.contains(QStringLiteral("year_stats")))
            {
                qWarning() << "[Playtime] 云端年度报告返回内容不符合预期，退回本机数据";
                if (onFail)
                    onFail();
                return;
            }
            const PlaytimeAnnualReport report = annualReportFromCloud(root, requestedYear);
            if (onOk)
                onOk(report);
        },
        [onFail](const QString &message, int status) {
            qWarning() << "[Playtime] 拉取年度报告失败：" << message << status;
            if (onFail)
                onFail();
        });
}

// ───────────────────────── 文案工具 ─────────────────────────

namespace Playtime {

QString formatDuration(qint64 seconds)
{
    const qint64 total = qMax<qint64>(0, seconds);
    // 0 说「0 分钟」：还没玩就说「不到 1 分钟」会让人以为已经记了一小段
    if (total <= 0)
        return QCoreApplication::translate("Playtime", "0 分钟");
    if (total < 60)
        return QCoreApplication::translate("Playtime", "不到 1 分钟");

    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    if (hours > 0)
    {
        if (minutes > 0)
            return QCoreApplication::translate("Playtime", "%1 小时 %2 分钟")
                .arg(hours)
                .arg(minutes);
        return QCoreApplication::translate("Playtime", "%1 小时").arg(hours);
    }
    return QCoreApplication::translate("Playtime", "%1 分钟").arg(qMax<qint64>(1, minutes));
}

QString formatHours(qint64 seconds)
{
    const qint64 total = qMax<qint64>(0, seconds);
    // 不足 1 小时按分钟显示（「45 分钟」比「0.8 小时」直观）
    if (total < 3600)
        return formatDuration(total);

    QString value = QString::number(static_cast<double>(total) / 3600.0, 'f', 1);
    if (value.endsWith(QLatin1String(".0")))
        value.chop(2);
    return QCoreApplication::translate("Playtime", "%1 小时").arg(value);
}

QString weekdayName(int dow)
{
    switch (dow)
    {
    case 1:
        return QCoreApplication::translate("Playtime", "周一");
    case 2:
        return QCoreApplication::translate("Playtime", "周二");
    case 3:
        return QCoreApplication::translate("Playtime", "周三");
    case 4:
        return QCoreApplication::translate("Playtime", "周四");
    case 5:
        return QCoreApplication::translate("Playtime", "周五");
    case 6:
        return QCoreApplication::translate("Playtime", "周六");
    case 7:
        return QCoreApplication::translate("Playtime", "周日");
    default:
        return QString();
    }
}

QString hourRangeText(int hour)
{
    if (hour < 0 || hour > 23)
        return QString();

    const QString from = QStringLiteral("%1:00").arg(hour, 2, 10, QLatin1Char('0'));
    const int next = (hour + 1) % 24;
    const QString to = QStringLiteral("%1:00").arg(next, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1 ~ %2").arg(from, to);
}

} // namespace Playtime
