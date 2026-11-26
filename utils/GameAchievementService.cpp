#include "GameAchievementService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QWidget>
#include <QtConcurrent>

#include "components/AppMessageBox.h"
#include "components/NotificationManager.h"
#include "utils/CommandAssistant/GameRegistry.h"
#include "utils/ForumAccountService.h"
#include "utils/GameAchievementWatcher.h"
#include "utils/SettingsManager.h"

namespace {

const QString kPendingKey = QStringLiteral("medals/pendingGameAchievements");
constexpr int kMaxPending = 200;
constexpr int kMaxChatBuffer = 20;

QString trustKey(int roomId)
{
    return QStringLiteral("medals/lobbyTrust/%1").arg(roomId);
}

/** 从 advancement id 的最后一段生成兜底标题：mine_stone → Mine stone */
QString fallbackTitle(const QString &id)
{
    QString path = id;
    const int colon = path.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        path = path.mid(colon + 1);
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString title = parts.isEmpty() ? path : parts.last();
    title.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!title.isEmpty())
        title[0] = title[0].toUpper();
    return title;
}

} // namespace

GameAchievementService::GameAchievementService(QWidget *notificationHost, QObject *parent)
    : QObject(parent)
    , m_host(notificationHost)
{
    m_watcher = new GameAchievementWatcher(this);
    connect(m_watcher, &GameAchievementWatcher::advancementEarned, this,
            [this](const GameAdvancement &advancement) {
                onFileAdvancement(advancement.id, advancement.world, advancement.earnedAt);
            });

    loadPending();

    // 登录成功后把离线 / 没登录时攒下的成就补传
    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            [this](const ForumUser &) { flushQueue(); });
}

GameAchievementService::~GameAchievementService() = default;

void GameAchievementService::setSessionContext(const QString &instancePath,
                                               const QString &version,
                                               const QString &localPlayerName)
{
    const bool versionChanged = version != m_version;
    m_instancePath = instancePath;
    m_version = version;
    m_localPlayerName = localPlayerName;
    m_localNames.clear();
    if (!localPlayerName.trimmed().isEmpty())
        m_localNames.append(localPlayerName.trimmed());
    const QString forumName = ForumAccountService::instance()->playerName().trimmed();
    if (!forumName.isEmpty() && !m_localNames.contains(forumName))
        m_localNames.append(forumName);
    if (m_watcher)
        m_watcher->setContext(m_instancePath, m_version);
    if (versionChanged)
    {
        m_titlesLoaded = false;
        m_titlesVersion.clear();
    }
    if (m_titlesLoaded || m_titlesLoading)
        return;
    ensureAdvancementTitles();
}

void GameAchievementService::setLobbyContext(int roomId, const QString &roomTitle)
{
    m_lobbyRoomId = qMax(0, roomId);
    m_lobbyTitle = roomTitle.trimmed();
    m_chatBuffer.clear();
    m_lobbyPromptOpen = false;
    m_lobbyTrust = Trust::Unknown;
    if (m_lobbyRoomId <= 0)
        return;

    const QVariant stored = SettingsManager::instance()->getProperty(trustKey(m_lobbyRoomId));
    if (stored.isValid())
        m_lobbyTrust = stored.toBool() ? Trust::Yes : Trust::No;
}

void GameAchievementService::startSession()
{
    m_sessionActive = true;
    m_seenAdvancements.clear();
    m_seenTitles.clear();
    if (m_watcher)
    {
        // 方块盒子联机（客户端没有本地世界在更新）只走日志解析；
        // 其它启动方式（单机 / 自己开的主机世界）走存档监听。
        if (m_lobbyRoomId > 0)
            m_watcher->stop();
        else
            m_watcher->start();
    }
    ensureAdvancementTitles();
    flushQueue();
}

void GameAchievementService::stopSession()
{
    m_sessionActive = false;
    if (m_watcher)
        m_watcher->stop();
    m_chatBuffer.clear();
    m_lobbyRoomId = 0;
    m_lobbyTitle.clear();
    m_lobbyTrust = Trust::Unknown;
    m_seenAdvancements.clear();
    m_seenTitles.clear();
}

void GameAchievementService::ensureAdvancementTitles()
{
    if (m_titlesLoaded || m_titlesLoading || m_instancePath.isEmpty() || m_version.isEmpty())
        return;

    m_titlesLoading = true;
    const QString instancePath = m_instancePath;
    const QString version = m_version;
    using AdvancementMaps = QPair<QHash<QString, QString>, QHash<QString, QString>>;
    auto *futureWatcher = new QFutureWatcher<AdvancementMaps>(this);
    connect(futureWatcher, &QFutureWatcher<AdvancementMaps>::finished, this,
            [this, futureWatcher, version]() {
                m_titlesLoading = false;
                const AdvancementMaps maps = futureWatcher->result();
                futureWatcher->deleteLater();
                const QHash<QString, QString> titles = maps.first;
                if (!titles.isEmpty())
                {
                    m_advancementTitles = titles;
                    m_advancementDescriptions = maps.second;
                    m_titleToAdvancement.clear();
                    for (auto it = titles.constBegin(); it != titles.constEnd(); ++it)
                    {
                        if (!it.value().isEmpty())
                            m_titleToAdvancement.insert(it.value(), it.key());
                    }
                }
                m_titlesLoaded = true;
                m_titlesVersion = version;

                // 已经排队但还没发出去的成就，补上游戏里的正式标题和达成条件
                if (m_pending.isEmpty())
                    return;
                for (int i = 0; i < m_pending.size(); ++i)
                {
                    QJsonObject item = m_pending.at(i).toObject();
                    const QString id = item.value(QStringLiteral("id")).toString();
                    const QString localized = m_advancementTitles.value(id);
                    const QString condition = m_advancementDescriptions.value(id);
                    bool changed = false;
                    if (!localized.isEmpty())
                    {
                        item.insert(QStringLiteral("title"), localized);
                        changed = true;
                    }
                    if (!condition.isEmpty())
                    {
                        item.insert(QStringLiteral("condition"), condition);
                        changed = true;
                    }
                    if (changed)
                        m_pending.replace(i, item);
                }
                savePending();
            });
    futureWatcher->setFuture(QtConcurrent::run([instancePath, version]() {
        GameRegistry registry;
        registry.extractFromGameDir(instancePath, version);
        return qMakePair(registry.advancementTitles(), registry.advancementDescriptions());
    }));
}

QString GameAchievementService::resolveTitle(const QString &id) const
{
    const QString localized = m_advancementTitles.value(id);
    return localized.isEmpty() ? fallbackTitle(id) : localized;
}

QString GameAchievementService::conditionForTitle(const QString &title) const
{
    const QString mapped = m_titleToAdvancement.value(title);
    if (!mapped.isEmpty())
    {
        const QString description = m_advancementDescriptions.value(mapped);
        if (!description.isEmpty())
            return description;
    }
    return tr("在游戏中达成该成就");
}

void GameAchievementService::onFileAdvancement(const QString &id, const QString &world,
                                               qint64 earnedAtMs)
{
    if (id.isEmpty())
        return;
    QString condition = m_advancementDescriptions.value(id);
    if (condition.isEmpty())
        condition = tr("在游戏中达成该成就");
    enqueueAdvancement(id, resolveTitle(id), condition, world, earnedAtMs / 1000);
}

QString GameAchievementService::advancementIdForTitle(const QString &title) const
{
    // 语言文件已经加载时用真正的 advancement id，保证多次游戏、存档与联机日志
    // 记到的是同一条成就；模组自定义 / 文件没读到时用标题指纹兜底。
    const QString mapped = m_titleToAdvancement.value(title);
    if (!mapped.isEmpty())
        return mapped;
    const QByteArray hash =
        QCryptographicHash::hash(title.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QStringLiteral("ingame:") + QString::fromLatin1(hash);
}

void GameAchievementService::onGameLogLine(const QString &line)
{
    // 只处理通过方块盒子启动的联机；单机由存档监听负责，避免同一条成就提示两次
    if (!m_sessionActive || m_lobbyRoomId <= 0 || m_localNames.isEmpty())
        return;
    if (m_watcher && m_watcher->isWatchingWorld())
        return;

    QString clean = line;
    clean.remove(QRegularExpression(QStringLiteral("§.")));
    if (!clean.contains(QStringLiteral("advancement"), Qt::CaseInsensitive)
        && !clean.contains(QStringLiteral("goal"), Qt::CaseInsensitive)
        && !clean.contains(QStringLiteral("challenge"), Qt::CaseInsensitive)
        && !clean.contains(QStringLiteral("进度"))
        && !clean.contains(QStringLiteral("挑战"))
        && !clean.contains(QStringLiteral("目标")))
    {
        return;
    }

    static const QRegularExpression enPattern(
        QStringLiteral(R"((?:\[CHAT\]\s*)?(.+?)\s+has\s+(?:made the advancement|reached the goal|completed the challenge)\s*[\[【](.+?)[\]】])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression zhPattern(
        QStringLiteral(R"((?:\[CHAT\]\s*)?(.+?)\s*(?:取得了进度|获得了进度|完成了进度|达成了目标|完成了挑战|达成进度|完成进度)\s*[\[【](.+?)[\]】])"));

    QString player;
    QString title;
    QRegularExpressionMatch match = enPattern.match(clean);
    if (match.hasMatch())
    {
        player = match.captured(1).trimmed();
        title = match.captured(2).trimmed();
    }
    else
    {
        match = zhPattern.match(clean);
        if (!match.hasMatch())
            return;
        player = match.captured(1).trimmed();
        title = match.captured(2).trimmed();
    }

    // 只要自己的成就：日志里可能带着队伍前缀 / 称号后缀，按名字边界判断
    bool mine = false;
    for (const QString &name : m_localNames)
    {
        if (name.isEmpty())
            continue;
        if (player.compare(name, Qt::CaseInsensitive) == 0
            || player.endsWith(name, Qt::CaseInsensitive)
            || player.contains(QLatin1Char(' ') + name + QLatin1Char(' '), Qt::CaseInsensitive))
        {
            mine = true;
            break;
        }
    }
    if (!mine)
        return;
    if (title.isEmpty())
        return;

    handleChatAdvancement(title, clean);
}

void GameAchievementService::handleChatAdvancement(const QString &title, const QString &line)
{
    Q_UNUSED(line);
    const QString id = advancementIdForTitle(title);
    if (m_seenAdvancements.contains(id) || m_queuedIds.contains(id))
        return;

    if (m_lobbyTrust == Trust::Unknown)
    {
        if (m_chatBuffer.size() < kMaxChatBuffer)
        {
            BufferedChat buffered;
            buffered.title = title;
            buffered.condition = conditionForTitle(title);
            buffered.id = id;
            buffered.earnedAt = QDateTime::currentSecsSinceEpoch();
            m_chatBuffer.append(buffered);
        }
        if (!m_lobbyPromptOpen)
            QTimer::singleShot(0, this, [this]() { askLobbyTrust(); });
        return;
    }
    if (m_lobbyTrust == Trust::No)
        return;

    enqueueAdvancement(id, title, conditionForTitle(title),
                       tr("方块盒子联机 · %1").arg(m_lobbyTitle),
                       QDateTime::currentSecsSinceEpoch());
}

void GameAchievementService::askLobbyTrust()
{
    if (m_lobbyPromptOpen || m_lobbyTrust != Trust::Unknown || m_lobbyRoomId <= 0)
        return;
    m_lobbyPromptOpen = true;

    const QString room = m_lobbyTitle.isEmpty() ? tr("这个房间") : m_lobbyTitle;
    if (m_host)
    {
        m_host->raise();
        m_host->activateWindow();
    }
    const AppMessageBox::StandardButton answer = AppMessageBox::question(
        m_host, tr("方块盒子联机"),
        tr("你正在通过方块盒子联机游玩「%1」。\n\n这个房间是生存模式、并且没有开启作弊吗？\n"
           "只有确认后，游戏里新获得的成就才会同步到勋章墙；选「否」以后不再记录这个房间的成就。")
            .arg(room),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);

    m_lobbyPromptOpen = false;
    m_lobbyTrust = answer == AppMessageBox::Yes ? Trust::Yes : Trust::No;
    SettingsManager::instance()->setProperty(trustKey(m_lobbyRoomId), m_lobbyTrust == Trust::Yes);

    if (m_lobbyTrust != Trust::Yes)
    {
        m_chatBuffer.clear();
        return;
    }

    const QVector<BufferedChat> buffered = m_chatBuffer;
    m_chatBuffer.clear();
    for (const BufferedChat &item : buffered)
        enqueueAdvancement(item.id, item.title, conditionForTitle(item.title),
                           tr("方块盒子联机 · %1").arg(m_lobbyTitle), item.earnedAt);
}

void GameAchievementService::enqueueAdvancement(const QString &id, const QString &title,
                                                const QString &condition, const QString &world,
                                                qint64 earnedAtSec)
{
    if (id.isEmpty() || m_queuedIds.contains(id) || m_seenAdvancements.contains(id))
        return;
    const QString shownTitle = title.isEmpty() ? resolveTitle(id) : title;
    if (shownTitle.isEmpty() || m_seenTitles.contains(shownTitle))
        return;

    m_seenAdvancements.insert(id);
    m_seenTitles.insert(shownTitle);

    QJsonObject item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("title"), shownTitle);
    item.insert(QStringLiteral("condition"),
                condition.isEmpty() ? tr("在游戏中达成该成就") : condition);
    item.insert(QStringLiteral("world"), world.left(60));
    item.insert(QStringLiteral("earned_at"), static_cast<double>(earnedAtSec));

    m_queuedIds.insert(id);
    m_pending.append(item);
    while (m_pending.size() > kMaxPending)
    {
        const QString dropped =
            m_pending.first().toObject().value(QStringLiteral("id")).toString();
        m_queuedIds.remove(dropped);
        m_pending.removeAt(0);
    }
    savePending();

    emit achievementEarned(shownTitle);
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        NotificationManager::showInfo(
            m_host, tr("获得游戏成就：%1（登录方块盒子账号后会同步到勋章墙）").arg(shownTitle));
        return;
    }

    NotificationManager::showSuccess(m_host, tr("已获得勋章：%1").arg(shownTitle));
    flushQueue();
}

void GameAchievementService::flushQueue()
{
    if (m_syncing || m_pending.isEmpty())
        return;
    ForumAccountService *service = ForumAccountService::instance();
    if (!service->isLoggedIn())
        return;

    QJsonArray batch;
    for (const QJsonValue &value : m_pending)
    {
        QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        const QString localized = m_advancementTitles.value(id);
        if (!localized.isEmpty())
            item.insert(QStringLiteral("title"), localized);
        batch.append(item);
    }
    if (batch.isEmpty())
        return;

    m_syncing = true;
    QJsonObject body;
    body.insert(QStringLiteral("achievements"), batch);
    service->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/medals/game"), body,
        [this, batch](const QJsonObject &root) {
            Q_UNUSED(root);
            m_syncing = false;
            QSet<QString> sent;
            for (const QJsonValue &value : batch)
                sent.insert(value.toObject().value(QStringLiteral("id")).toString());

            QJsonArray rest;
            for (const QJsonValue &value : m_pending)
            {
                const QString id = value.toObject().value(QStringLiteral("id")).toString();
                if (!sent.contains(id))
                    rest.append(value);
            }
            m_pending = rest;
            for (const QString &id : sent)
                m_queuedIds.remove(id);
            savePending();

            if (!m_pending.isEmpty() && ForumAccountService::instance()->isLoggedIn())
                QTimer::singleShot(1000, this, [this]() { flushQueue(); });
        },
        [this](const QString &, int) {
            m_syncing = false;
            QTimer::singleShot(30000, this, [this]() { flushQueue(); });
        });
}

void GameAchievementService::loadPending()
{
    const QByteArray raw = SettingsManager::instance()->getProperty(kPendingKey).toByteArray();
    if (raw.isEmpty())
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isArray())
        return;

    for (const QJsonValue &value : doc.array())
    {
        const QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || m_queuedIds.contains(id))
            continue;
        m_queuedIds.insert(id);
        m_pending.append(item);
    }
    while (m_pending.size() > kMaxPending)
    {
        const QString dropped =
            m_pending.first().toObject().value(QStringLiteral("id")).toString();
        m_queuedIds.remove(dropped);
        m_pending.removeAt(0);
    }
}

void GameAchievementService::savePending()
{
    SettingsManager::instance()->setProperty(
        kPendingKey, QJsonDocument(m_pending).toJson(QJsonDocument::Compact));
}
