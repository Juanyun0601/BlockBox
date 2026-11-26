/**
 * @file   ForumPlayerStatsService.cpp
 * @brief  论坛人数统计接口实现
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "ForumPlayerStatsService.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include "ForumAccountService.h"

ForumPlayerStatsService *ForumPlayerStatsService::instance()
{
    static ForumPlayerStatsService *s_instance = new ForumPlayerStatsService();
    return s_instance;
}

ForumPlayerStatsService::ForumPlayerStatsService(QObject *parent) : QObject(parent) {}

ForumPlayerStatsService::ServerAddress
ForumPlayerStatsService::parseServerAddress(const QString &raw)
{
    ServerAddress address;

    QString text = raw.trimmed();
    if (text.isEmpty())
        return address;

    // 导航帖里也可能贴成 minecraft:// 或带路径的地址，这里只留主机那一段
    const QRegularExpression scheme(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*://"));
    text.remove(scheme);
    const int cut = text.indexOf(QRegularExpression(QStringLiteral("[/?#\\s]")));
    if (cut >= 0)
        text = text.left(cut);
    text = text.trimmed().toLower();
    if (text.isEmpty())
        return address;

    QString host = text;
    quint16 port = 25565;
    const int colon = text.lastIndexOf(QLatin1Char(':'));
    // 只认「主机:端口」这种单冒号形式：带端口的才算，其它（IPv6 字面量等）交给默认端口
    if (colon > 0 && text.indexOf(QLatin1Char(':')) == colon)
    {
        bool ok = false;
        const uint parsedPort = text.mid(colon + 1).toUInt(&ok);
        if (ok && parsedPort > 0 && parsedPort <= 65535)
        {
            port = static_cast<quint16>(parsedPort);
            host = text.left(colon);
        }
        else
        {
            // 冒号后面不是端口（例如主机名里混进了别的东西）：按整串当主机，不认这个地址
            return address;
        }
    }

    static const QRegularExpression hostPattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9._-]{0,158})$"));
    host = host.trimmed();
    if (host.isEmpty() || host.length() > 160 || !hostPattern.match(host).hasMatch())
        return address;

    address.host = host;
    address.port = port;
    return address;
}

PlayerStatSeries ForumPlayerStatsService::parseSeries(const QJsonObject &root)
{
    PlayerStatSeries series;
    series.key = root.value(QStringLiteral("key")).toString();
    series.totalSamples = root.value(QStringLiteral("total")).toInt();

    const QJsonObject latest = root.value(QStringLiteral("latest")).toObject();
    series.latestOnline = latest.value(QStringLiteral("online")).toInt();
    series.latestMax = latest.value(QStringLiteral("max")).toInt();
    series.latestAt = qint64(latest.value(QStringLiteral("t")).toDouble());

    const QJsonArray points = root.value(QStringLiteral("points")).toArray();
    series.points.reserve(points.size());
    for (const QJsonValue &value : points)
    {
        const QJsonObject item = value.toObject();
        PlayerStatPoint point;
        point.t = qint64(item.value(QStringLiteral("t")).toDouble());
        point.online = item.value(QStringLiteral("online")).toInt();
        point.maxPlayers = item.value(QStringLiteral("max")).toInt();
        point.samples = item.value(QStringLiteral("samples")).toInt();
        if (point.t <= 0)
            continue;
        series.points.append(point);
    }
    return series;
}

void ForumPlayerStatsService::reportServer(const ServerAddress &address, int online,
                                           int maxPlayers)
{
    if (!address.isValid())
        return;

    QJsonObject body;
    body.insert(QStringLiteral("address"), address.key());
    body.insert(QStringLiteral("online"), qMax(0, online));
    body.insert(QStringLiteral("max"), qMax(0, maxPlayers));

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/player-stats/server"), body,
        nullptr, nullptr);
}

void ForumPlayerStatsService::fetchServer(const ServerAddress &address, int hours,
                                          SeriesHandler onOk, FailHandler onFail)
{
    if (!address.isValid() || !onOk)
        return;

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("address"), address.key());
    query.addQueryItem(QStringLiteral("hours"), QString::number(qBound(1, hours, 168)));
    const QString path = QStringLiteral("/api/player-stats/server?")
                         + query.toString(QUrl::FullyEncoded);

    ForumAccountService::instance()->getJson(
        path,
        [onOk](const QJsonObject &root) { onOk(parseSeries(root)); },
        [onFail](const QString &message, int) {
            qWarning() << "[PlayerStats] 拉取服务器人数统计失败：" << message;
            if (onFail)
                onFail();
        });
}

void ForumPlayerStatsService::reportLobby(int roomId, int players)
{
    if (roomId <= 0)
        return;

    QJsonObject body;
    body.insert(QStringLiteral("room_id"), roomId);
    body.insert(QStringLiteral("players"), qBound(0, players, 64));

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/player-stats/lobby"), body,
        nullptr, nullptr);
}

void ForumPlayerStatsService::fetchLobby(int roomId, int hours, SeriesHandler onOk,
                                         FailHandler onFail)
{
    if (roomId <= 0 || !onOk)
        return;

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("room_id"), QString::number(roomId));
    query.addQueryItem(QStringLiteral("hours"), QString::number(qBound(1, hours, 168)));
    const QString path = QStringLiteral("/api/player-stats/lobby?")
                         + query.toString(QUrl::FullyEncoded);

    ForumAccountService::instance()->getJson(
        path,
        [onOk](const QJsonObject &root) { onOk(parseSeries(root)); },
        [onFail](const QString &message, int) {
            qWarning() << "[PlayerStats] 拉取房间人数统计失败：" << message;
            if (onFail)
                onFail();
        });
}
