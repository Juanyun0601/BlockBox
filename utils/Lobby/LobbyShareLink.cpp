/**
 * @file   LobbyShareLink.cpp
 * @brief  联机房间分享链接的实现
 * @author BlockBox Team
 * @date   2026-11-08
 */

#include "LobbyShareLink.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

#include "utils/SettingsManager.h"

namespace
{
constexpr char kDeepLinkScheme[] = "blockbox";
constexpr char kDeepLinkJoinHost[] = "join";
constexpr char kPayloadParam[] = "d";
constexpr char kWebJoinPage[] = "/join.html";

QJsonObject roomToJson(const ForumLobbyRoom &room)
{
    QJsonObject json;
    if (!room.title.isEmpty())
        json.insert(QStringLiteral("t"), room.title);
    json.insert(QStringLiteral("c"), room.core);
    if (!room.inviteCode.isEmpty())
        json.insert(QStringLiteral("ic"), room.inviteCode);
    if (!room.address.isEmpty())
        json.insert(QStringLiteral("ad"), room.address);
    if (!room.gameVersion.isEmpty())
        json.insert(QStringLiteral("v"), room.gameVersion);
    if (!room.loader.isEmpty())
        json.insert(QStringLiteral("ld"), room.loader);
    if (!room.mods.isEmpty())
        json.insert(QStringLiteral("md"), QJsonArray::fromStringList(room.mods));
    return json;
}

QString payloadToParam(const QJsonObject &json)
{
    const QJsonDocument doc(json);
    const QByteArray compressed = qCompress(doc.toJson(QJsonDocument::Compact));
    // base64url 无填充：链接和深链里都不需要转义
    return QString::fromLatin1(compressed.toBase64(QByteArray::Base64UrlEncoding
                                                   | QByteArray::OmitTrailingEquals));
}
} // namespace

namespace LobbyShareLink
{
QString encodePayload(const ForumLobbyRoom &room)
{
    ForumLobbyRoom trimmed = room;
    if (trimmed.mods.size() > kMaxModsInPayload)
        trimmed.mods = trimmed.mods.mid(0, kMaxModsInPayload);
    return payloadToParam(roomToJson(trimmed));
}

QString webShareUrl(const ForumLobbyRoom &room)
{
    QString base = SettingsManager::instance()->forumBaseUrl().trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.isEmpty())
        base = SettingsManager::defaultForumBaseUrl();
    QUrl url(base + QString::fromLatin1(kWebJoinPage));
    QUrlQuery query;
    query.addQueryItem(QString::fromLatin1(kPayloadParam), encodePayload(room));
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

QString deepLinkUrl(const ForumLobbyRoom &room)
{
    QUrl url;
    url.setScheme(QString::fromLatin1(kDeepLinkScheme));
    url.setHost(QString::fromLatin1(kDeepLinkJoinHost));
    QUrlQuery query;
    query.addQueryItem(QString::fromLatin1(kPayloadParam), encodePayload(room));
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

bool parsePayload(const QString &encoded, ForumLobbyRoom *room, QString *error)
{
    if (room)
        *room = ForumLobbyRoom();
    if (error)
        error->clear();

    const QString trimmed = encoded.trimmed();
    if (trimmed.isEmpty())
    {
        if (error)
            *error = QObject::tr("链接里没有房间信息。");
        return false;
    }

    // 深链 / 网页链接都兼容：直接给载荷就当 ?d= 的值用
    QString param = trimmed;
    if (trimmed.startsWith(QStringLiteral("blockbox://"), Qt::CaseInsensitive)
        || trimmed.contains(QStringLiteral("/join.html?"), Qt::CaseInsensitive))
    {
        const QUrl url(trimmed);
        const QUrlQuery query(url.query());
        param = query.queryItemValue(QString::fromLatin1(kPayloadParam));
    }

    QByteArray compressed = QByteArray::fromBase64(
        param.toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (compressed.isEmpty())
    {
        // 兼容网页端复制出来的标准 base64
        compressed = QByteArray::fromBase64(param.toLatin1());
    }
    if (compressed.isEmpty())
    {
        if (error)
            *error = QObject::tr("链接已损坏，请让对方重新创建分享链接。");
        return false;
    }

    const QByteArray raw = qUncompress(compressed);
    if (raw.isEmpty())
    {
        if (error)
            *error = QObject::tr("链接已损坏，请让对方重新创建分享链接。");
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error)
            *error = QObject::tr("链接已损坏，请让对方重新创建分享链接。");
        return false;
    }

    const QJsonObject json = doc.object();
    ForumLobbyRoom parsed;
    parsed.title = json.value(QStringLiteral("t")).toString();
    parsed.core = json.value(QStringLiteral("c")).toString();
    parsed.inviteCode = json.value(QStringLiteral("ic")).toString();
    parsed.address = json.value(QStringLiteral("ad")).toString();
    parsed.gameVersion = json.value(QStringLiteral("v")).toString();
    parsed.loader = json.value(QStringLiteral("ld")).toString();
    const QJsonArray mods = json.value(QStringLiteral("md")).toArray();
    for (const QJsonValue &value : mods)
        parsed.mods.append(value.toString());
    parsed.coreLabel = ForumLobbyService::coreDisplayName(parsed.core);
    parsed.status = QStringLiteral("waiting");
    // 分享链接没有过期概念，置一个正值让 isOnline() 通过加入前的检查
    parsed.expiresIn = 3600;

    if (parsed.core.isEmpty()
        || (parsed.inviteCode.isEmpty() && parsed.address.isEmpty()))
    {
        if (error)
            *error = QObject::tr("链接里没有可用的邀请码或连接地址。");
        return false;
    }

    if (room)
        *room = parsed;
    return true;
}
} // namespace LobbyShareLink
