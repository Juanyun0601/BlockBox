#include "GameAchievementWatcher.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVector>

#include <algorithm>

#include "utils/CommandAssistant/GameDetector.h"
#include "utils/CommandAssistant/LevelDatReader.h"

namespace {

/** 每 3 秒扫一次：存档文件很小，游戏也是隔一会儿才落盘，再快没有意义 */
constexpr int kPollIntervalMs = 3000;
/** 判断「新成就」时允许的时钟误差 */
constexpr qint64 kFreshToleranceMs = 2 * 60 * 1000;
/** 做过场成就（配方解锁）不当作真正的游戏成就 */
const char kRecipePrefix[] = "minecraft:recipes/";

/**
 * @brief 解析 advancement JSON 里 criteria 的时间
 *
 * 原版格式： "2026-11-22 09:30:00 +0800"。Qt 的 ISODate 不认空格，
 * 这里转成 "2026-11-22T09:30:00+08:00" 再解析；失败再退回本地时间。
 */
qint64 parseAdvancementTime(const QString &raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return 0;

    QString iso = text;
    if (iso.size() > 10 && iso.at(10) == QLatin1Char(' '))
        iso[10] = QLatin1Char('T');
    if (iso.size() >= 24)
        iso.insert(iso.size() - 2, QLatin1Char(':'));

    QDateTime dateTime = QDateTime::fromString(iso, Qt::ISODate);
    if (dateTime.isValid())
        return dateTime.toMSecsSinceEpoch();

    dateTime = QDateTime::fromString(text.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (dateTime.isValid())
        return dateTime.toMSecsSinceEpoch();

    return 0;
}

/** 取一条 advancement 的完成时间：criteria 里最晚的时间 */
qint64 advancementDoneTime(const QJsonObject &entry)
{
    qint64 latest = 0;
    const QJsonObject criteria = entry.value(QStringLiteral("criteria")).toObject();
    for (auto it = criteria.constBegin(); it != criteria.constEnd(); ++it)
    {
        const qint64 stamp = parseAdvancementTime(it.value().toString());
        if (stamp > latest)
            latest = stamp;
    }
    return latest;
}

} // namespace

GameAchievementWatcher::GameAchievementWatcher(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kPollIntervalMs);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &GameAchievementWatcher::poll);
}

GameAchievementWatcher::~GameAchievementWatcher() = default;

void GameAchievementWatcher::setContext(const QString &instancePath, const QString &version)
{
    if (m_instancePath == instancePath && m_version == version)
        return;

    m_instancePath = instancePath;
    m_version = version;
    resetBaseline();
    if (m_running)
        poll();
}

void GameAchievementWatcher::start()
{
    if (m_running)
        return;
    m_running = true;
    resetBaseline();
    m_timer->start();
    poll();
}

void GameAchievementWatcher::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_timer->stop();
    resetBaseline();
    m_gameWasRunning = false;
}

bool GameAchievementWatcher::isWatchingWorld() const
{
    // 只有确实有本地存档、且 level.dat 最近被游戏写过，才算「在玩单机/主机」。
    // 联机时客户端没有本地世界在更新，这里会返回 false，成就改走日志解析。
    if (!m_running || m_worldDir.isEmpty())
        return false;
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - m_levelDatMtime;
    return age >= 0 && age < 3 * 60 * 1000;
}

void GameAchievementWatcher::resetBaseline()
{
    m_worldDir.clear();
    m_worldName.clear();
    m_advancementFile.clear();
    m_advancementFileMtime = 0;
    m_baselineReady = false;
    m_monitorStartMs = 0;
    m_baseline.clear();
    m_emitted.clear();
    m_levelDatChecked = false;
    m_levelDatMtime = 0;
    m_levelDatSurvival = false;
}

bool GameAchievementWatcher::findActiveWorld(QString &worldDir, QString &worldName)
{
    if (m_instancePath.isEmpty())
        return false;

    QDir savesDir;
    const QStringList candidates = {
        QDir(m_instancePath).absoluteFilePath(QStringLiteral("saves")),
        QDir(m_instancePath).absoluteFilePath(QStringLiteral("../saves")),
        QDir(m_instancePath).absoluteFilePath(QStringLiteral("../../saves")),
    };
    for (const QString &candidate : candidates)
    {
        QDir dir(candidate);
        if (dir.exists())
        {
            savesDir = dir;
            break;
        }
    }
    if (!savesDir.exists())
        return false;

    QString latestDir;
    QDateTime latestTime;
    const QStringList subDirs = savesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subDirs)
    {
        const QString levelPath = savesDir.absoluteFilePath(sub + QStringLiteral("/level.dat"));
        QFileInfo info(levelPath);
        if (!info.exists() || !info.isFile())
            continue;
        if (!latestTime.isValid() || info.lastModified() > latestTime)
        {
            latestTime = info.lastModified();
            latestDir = savesDir.absoluteFilePath(sub);
            worldName = sub;
        }
    }

    if (latestDir.isEmpty())
        return false;
    worldDir = latestDir;
    return true;
}

bool GameAchievementWatcher::readDoneAdvancements(const QString &file,
                                                  QHash<QString, qint64> &done) const
{
    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(handle.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        if (!it.value().isObject())
            continue;
        const QJsonObject entry = it.value().toObject();
        if (!entry.value(QStringLiteral("done")).toBool())
            continue;
        done.insert(it.key(), advancementDoneTime(entry));
    }
    return true;
}

void GameAchievementWatcher::poll()
{
    if (!m_running)
        return;

    // 只看游戏真的在跑的时候：助手先开、后启动游戏也能正常开始基线
    if (!GameDetector::isJavaGameRunning())
    {
        if (m_gameWasRunning)
        {
            // 游戏关了：下一次启动重新记基线，免得把上一局的成就当成新的
            resetBaseline();
            m_gameWasRunning = false;
        }
        return;
    }
    m_gameWasRunning = true;

    QString worldDir;
    QString worldName;
    if (!findActiveWorld(worldDir, worldName))
        return;

    // 只认生存模式且没开作弊的存档；level.dat 没变化就复用上次结果
    const QString levelPath = worldDir + QStringLiteral("/level.dat");
    const QFileInfo levelFile(levelPath);
    const qint64 levelMtime = levelFile.lastModified().toMSecsSinceEpoch();
    if (!m_levelDatChecked || levelMtime != m_levelDatMtime || m_worldDir != worldDir)
    {
        const std::optional<LevelInfo> info = LevelDatReader::readLevelDat(levelPath);
        m_levelDatChecked = true;
        m_levelDatMtime = levelMtime;
        m_levelDatSurvival =
            info.has_value() && !info->allowCommands && info->gameType == 0;
        if (info.has_value() && !info->levelName.isEmpty())
            worldName = info->levelName;
    }
    // 玩家成就文件：advancements/<uuid>.json，取最近写入的那个
    QDir advDir(worldDir + QStringLiteral("/advancements"));
    if (!advDir.exists())
        return;

    QString advFile;
    const QStringList files =
        advDir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    if (!files.isEmpty())
        advFile = advDir.absoluteFilePath(files.first());
    if (advFile.isEmpty())
        return;

    const QFileInfo advInfo(advFile);
    const qint64 advMtime = advInfo.lastModified().toMSecsSinceEpoch();
    const bool fileChanged = advFile != m_advancementFile || advMtime != m_advancementFileMtime;
    if (m_baselineReady && !fileChanged)
        return;

    QHash<QString, qint64> done;
    if (!readDoneAdvancements(advFile, done))
        return;

    // 第一次扫、换存档、换玩家文件：只记基线，不补发旧成就
    if (!m_baselineReady || worldDir != m_worldDir || advFile != m_advancementFile)
    {
        m_worldDir = worldDir;
        m_worldName = worldName;
        m_advancementFile = advFile;
        m_advancementFileMtime = advMtime;
        m_baseline.clear();
        for (auto it = done.constBegin(); it != done.constEnd(); ++it)
            m_baseline.insert(it.key());
        m_baselineReady = true;
        m_monitorStartMs = QDateTime::currentMSecsSinceEpoch();
        return;
    }

    // 开作弊 / 创造模式时把这期间的成就也并进基线：等切回生存后不能再补发
    if (!m_levelDatSurvival)
    {
        m_baseline.clear();
        for (auto it = done.constBegin(); it != done.constEnd(); ++it)
            m_baseline.insert(it.key());
        m_advancementFileMtime = advMtime;
        m_worldName = worldName;
        return;
    }

    const qint64 freshAfter = m_monitorStartMs - kFreshToleranceMs;
    QVector<GameAdvancement> fresh;
    for (auto it = done.constBegin(); it != done.constEnd(); ++it)
    {
        const QString id = it.key();
        if (m_baseline.contains(id) || m_emitted.contains(id))
            continue;
        if (id.startsWith(QLatin1String(kRecipePrefix)))
            continue;

        qint64 earnedAt = it.value();
        if (earnedAt <= 0)
            earnedAt = advMtime;
        if (earnedAt < freshAfter)
            continue;

        m_emitted.insert(id);
        GameAdvancement advancement;
        advancement.id = id;
        advancement.world = worldName;
        advancement.earnedAt = earnedAt;
        fresh.append(advancement);
    }

    m_baseline.clear();
    for (auto it = done.constBegin(); it != done.constEnd(); ++it)
        m_baseline.insert(it.key());
    m_advancementFileMtime = advMtime;
    m_worldName = worldName;

    for (const GameAdvancement &advancement : fresh)
        emit advancementEarned(advancement);
}
