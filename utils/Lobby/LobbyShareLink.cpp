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
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include "utils/SettingsManager.h"

namespace
{
constexpr char kPayloadParam[] = "d";
constexpr char kShareParam[] = "r";
constexpr char kWebJoinPage[] = "/join.html";

/** 论坛地址（去掉末尾斜杠，没配置时用默认地址） */
QString forumBase()
{
    QString base = SettingsManager::instance()->forumBaseUrl().trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.isEmpty())
        base = SettingsManager::defaultForumBaseUrl();
    return base;
}

/** 从完整链接或纯参数串里取某个参数的值 */
QString queryValue(const QString &source, const QString &key)
{
    const int question = source.indexOf(QLatin1Char('?'));
    const QString queryPart = question >= 0 ? source.mid(question + 1) : source;
    const QUrlQuery query(queryPart);
    return query.queryItemValue(key, QUrl::FullyDecoded).trimmed();
}

bool looksLikeUrl(const QString &text)
{
    return text.startsWith(QStringLiteral("blockbox://"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("://"))
           || text.contains(QStringLiteral("/join.html"), Qt::CaseInsensitive);
}

/** 房间号只允许字母数字：乱填的、带斜杠的一律当无效，避免拼进 URL 出岔子 */
QString normalizeToken(const QString &raw)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9]{4,24}$"));
    const QString token = raw.trimmed().toLower();
    return pattern.match(token).hasMatch() ? token : QString();
}

QJsonArray toJsonArray(const QStringList &names, int limit)
{
    QJsonArray array;
    for (const QString &name : names)
    {
        if (name.trimmed().isEmpty())
            continue;
        array.append(name);
        if (array.size() >= limit)
            break;
    }
    return array;
}

QStringList fromJsonArray(const QJsonValue &value, int limit)
{
    QStringList names;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array)
    {
        const QString name = item.toString().trimmed();
        if (name.isEmpty() || names.contains(name, Qt::CaseInsensitive))
            continue;
        names.append(name);
        if (names.size() >= limit)
            break;
    }
    return names;
}

bool payloadError(QString *error)
{
    if (error)
        *error = QObject::tr("链接已损坏，请让对方重新创建分享链接。");
    return false;
}
} // namespace

namespace LobbyShareLink
{
QJsonObject roomToPayload(const ForumLobbyRoom &room)
{
    QJsonObject json;
    if (!room.title.isEmpty())
        json.insert(QStringLiteral("t"), room.title);
    if (!room.description.isEmpty())
        json.insert(QStringLiteral("ds"), room.description.left(kMaxDescriptionInPayload));
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
        json.insert(QStringLiteral("md"), toJsonArray(room.mods, kMaxModsInPayload));
    if (!room.auxMods.isEmpty())
        json.insert(QStringLiteral("ma"), toJsonArray(room.auxMods, kMaxModsInPayload));
    if (!room.resourcePacks.isEmpty())
        json.insert(QStringLiteral("rp"), toJsonArray(room.resourcePacks, kMaxPacksInPayload));
    if (!room.shaderPacks.isEmpty())
        json.insert(QStringLiteral("sp"), toJsonArray(room.shaderPacks, kMaxPacksInPayload));
    return json;
}

ForumLobbyRoom payloadToRoom(const QJsonObject &payload)
{
    ForumLobbyRoom room;
    room.title = payload.value(QStringLiteral("t")).toString().trimmed();
    if (room.title.isEmpty())
        room.title = QObject::tr("联机房间");
    room.description =
        payload.value(QStringLiteral("ds")).toString().trimmed().left(kMaxDescriptionInPayload);
    room.core = payload.value(QStringLiteral("c")).toString();
    room.coreLabel = ForumLobbyService::coreDisplayName(room.core);
    room.inviteCode = payload.value(QStringLiteral("ic")).toString().trimmed();
    room.address = payload.value(QStringLiteral("ad")).toString().trimmed();
    room.gameVersion = payload.value(QStringLiteral("v")).toString().trimmed();
    room.loader = payload.value(QStringLiteral("ld")).toString().trimmed();
    room.mods = fromJsonArray(payload.value(QStringLiteral("md")), kMaxModsInPayload);
    room.auxMods = fromJsonArray(payload.value(QStringLiteral("ma")), kMaxModsInPayload);
    room.resourcePacks = fromJsonArray(payload.value(QStringLiteral("rp")), kMaxPacksInPayload);
    room.shaderPacks = fromJsonArray(payload.value(QStringLiteral("sp")), kMaxPacksInPayload);
    // 老链接没有辅助模组这几项：辅助模组补进完整模组列表，两边对得上
    for (const QString &name : room.auxMods)
    {
        if (!room.mods.contains(name, Qt::CaseInsensitive))
            room.mods.append(name);
    }
    room.status = QStringLiteral("waiting");
    // 分享链接没有过期概念（能取到就说明房主的联机还开着），置正值让 isOnline() 通过
    room.expiresIn = 3600;
    return room;
}

QString webShareUrl(const QString &token)
{
    QUrl url(forumBase() + QString::fromLatin1(kWebJoinPage));
    QUrlQuery query;
    query.addQueryItem(QString::fromLatin1(kShareParam), token);
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

bool parseLink(const QString &url, LinkTarget *target, QString *error)
{
    if (target)
        *target = LinkTarget();
    if (error)
        error->clear();

    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
    {
        if (error)
            *error = QObject::tr("链接里没有房间信息。");
        return false;
    }

    // 新式短链接：链接里只有一个房间号，房间信息去论坛服务器取
    const QString token = normalizeToken(queryValue(trimmed, QString::fromLatin1(kShareParam)));
    if (!token.isEmpty())
    {
        if (target)
            target->token = token;
        return true;
    }

    // 老式自包含链接：房间信息压缩在 ?d= 里
    QString payload = queryValue(trimmed, QString::fromLatin1(kPayloadParam));
    if (payload.isEmpty() && !looksLikeUrl(trimmed))
        payload = trimmed;

    ForumLobbyRoom room;
    QString payloadError;
    if (!parsePayload(payload, &room, &payloadError))
    {
        if (error)
            *error = payloadError;
        return false;
    }

    if (target)
    {
        target->legacy = true;
        target->room = room;
    }
    return true;
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
    if (looksLikeUrl(trimmed))
        param = queryValue(trimmed, QString::fromLatin1(kPayloadParam));

    QByteArray compressed = QByteArray::fromBase64(
        param.toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (compressed.isEmpty())
    {
        // 兼容网页端复制出来的标准 base64
        compressed = QByteArray::fromBase64(param.toLatin1());
    }
    if (compressed.isEmpty())
        return payloadError(error);

    const QByteArray raw = qUncompress(compressed);
    if (raw.isEmpty())
        return payloadError(error);

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return payloadError(error);

    ForumLobbyRoom parsed = payloadToRoom(doc.object());
    if (parsed.core.isEmpty() || (parsed.inviteCode.isEmpty() && parsed.address.isEmpty()))
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
