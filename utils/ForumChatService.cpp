/**
 * @file   ForumChatService.cpp
 * @brief  方块盒子论坛好友与私信服务实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumChatService.h"

#include <QJsonArray>
#include <QUrlQuery>

ForumChatService *ForumChatService::instance()
{
    static ForumChatService *s_instance = new ForumChatService();
    return s_instance;
}

ForumChatService::ForumChatService(QObject *parent) : QObject(parent) {}

// ────────────────────────────────────────────────────────────── 解析

ForumFriendInfo ForumChatService::parsePeer(const QJsonObject &object)
{
    ForumFriendInfo peer;
    peer.id = object.value(QStringLiteral("id")).toInt();
    peer.username = object.value(QStringLiteral("username")).toString();
    peer.displayName = object.value(QStringLiteral("display_name")).toString();
    if (peer.displayName.isEmpty())
        peer.displayName = peer.username;
    peer.avatar = object.value(QStringLiteral("avatar")).toString();
    peer.online = object.value(QStringLiteral("online")).toBool();
    peer.lastSeenAt = static_cast<qint64>(object.value(QStringLiteral("last_seen_at")).toDouble());
    return peer;
}

ForumChatMessage ForumChatService::parseMessage(const QJsonObject &object)
{
    ForumChatMessage message;
    message.id = object.value(QStringLiteral("id")).toInt();
    message.body = object.value(QStringLiteral("body")).toString();
    message.mine = object.value(QStringLiteral("mine")).toBool();
    message.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
    message.read = object.value(QStringLiteral("read_at")).toDouble() > 0;
    return message;
}

// ────────────────────────────────────────────────────────────── 好友

void ForumChatService::fetchFriends(std::function<void(const ForumFriendSnapshot &)> onOk,
                                    FailHandler onFail)
{
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/friends"),
        [this, onOk](const QJsonObject &root) {
            ForumFriendSnapshot snapshot;
            snapshot.totalUnread = root.value(QStringLiteral("total_unread")).toInt();

            const QJsonArray friends = root.value(QStringLiteral("friends")).toArray();
            for (const QJsonValue &value : friends)
            {
                const QJsonObject object = value.toObject();
                ForumFriendInfo peer = parsePeer(object);
                peer.unread = object.value(QStringLiteral("unread")).toInt();
                const QJsonObject last = object.value(QStringLiteral("last_message")).toObject();
                if (!last.isEmpty())
                {
                    peer.lastMessageBody = last.value(QStringLiteral("body")).toString();
                    peer.lastMessageAt =
                        static_cast<qint64>(last.value(QStringLiteral("created_at")).toDouble());
                    peer.lastMessageMine = last.value(QStringLiteral("mine")).toBool();
                }
                snapshot.friends.append(peer);
            }

            for (const QJsonValue &value : root.value(QStringLiteral("incoming")).toArray())
            {
                const QJsonObject object = value.toObject();
                ForumFriendRequest request;
                request.id = object.value(QStringLiteral("id")).toInt();
                request.createdAt =
                    static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
                request.user = parsePeer(object.value(QStringLiteral("user")).toObject());
                snapshot.incoming.append(request);
            }

            for (const QJsonValue &value : root.value(QStringLiteral("outgoing")).toArray())
            {
                const QJsonObject object = value.toObject();
                ForumFriendRequest request;
                request.id = object.value(QStringLiteral("id")).toInt();
                request.createdAt =
                    static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
                request.user = parsePeer(object.value(QStringLiteral("user")).toObject());
                snapshot.outgoing.append(request);
            }

            if (onOk)
                onOk(snapshot);
        },
        std::move(onFail));
}

void ForumChatService::sendFriendRequest(int userId,
                                         std::function<void(const QString &status)> onOk,
                                         FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("user_id"), userId);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/friends/requests"), body,
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(root.value(QStringLiteral("status")).toString());
        },
        std::move(onFail));
}

void ForumChatService::sendFriendRequestByName(const QString &username,
                                               std::function<void(const QString &status)> onOk,
                                               FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/friends/requests"), body,
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(root.value(QStringLiteral("status")).toString());
        },
        std::move(onFail));
}

void ForumChatService::acceptFriendRequest(int requestId, std::function<void()> onOk,
                                           FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"),
        QStringLiteral("/api/friends/requests/%1/accept").arg(requestId), {},
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumChatService::declineFriendRequest(int requestId, std::function<void()> onOk,
                                            FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"),
        QStringLiteral("/api/friends/requests/%1/decline").arg(requestId), {},
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumChatService::withdrawFriendRequest(int requestId, std::function<void()> onOk,
                                             FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("DELETE"),
        QStringLiteral("/api/friends/requests/%1").arg(requestId), {},
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumChatService::removeFriend(int userId, std::function<void()> onOk, FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("DELETE"), QStringLiteral("/api/friends/%1").arg(userId), {},
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumChatService::searchUsers(const QString &keyword,
                                   std::function<void(const QVector<ForumFriendInfo> &)> onOk,
                                   FailHandler onFail)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), keyword);
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/users/search?%1").arg(query.toString(QUrl::FullyEncoded)),
        [this, onOk](const QJsonObject &root) {
            QVector<ForumFriendInfo> users;
            const QJsonArray array = root.value(QStringLiteral("users")).toArray();
            for (const QJsonValue &value : array)
                users.append(parsePeer(value.toObject()));
            if (onOk)
                onOk(users);
        },
        std::move(onFail));
}

// ────────────────────────────────────────────────────────────── 私信

void ForumChatService::fetchMessages(int userId, int beforeId,
                                     std::function<void(const ForumChatHistory &)> onOk,
                                     FailHandler onFail)
{
    QUrlQuery query;
    if (beforeId > 0)
        query.addQueryItem(QStringLiteral("before_id"), QString::number(beforeId));
    const QString suffix = query.isEmpty() ? QString() : QStringLiteral("?%1")
                                                                    .arg(query.toString(QUrl::FullyEncoded));
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/messages/%1%2").arg(userId).arg(suffix),
        [this, onOk](const QJsonObject &root) {
            ForumChatHistory history;
            history.peer = parsePeer(root.value(QStringLiteral("user")).toObject());
            const QJsonArray array = root.value(QStringLiteral("messages")).toArray();
            for (const QJsonValue &value : array)
                history.messages.append(parseMessage(value.toObject()));
            history.hasMore = root.value(QStringLiteral("has_more")).toBool();
            if (onOk)
                onOk(history);
        },
        std::move(onFail));
}

void ForumChatService::sendMessage(int userId, const QString &body,
                                   std::function<void(const ForumChatMessage &)> onOk,
                                   FailHandler onFail)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("body"), body);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/messages/%1").arg(userId), payload,
        [this, onOk](const QJsonObject &root) {
            const ForumChatMessage message = parseMessage(root.value(QStringLiteral("message")).toObject());
            if (onOk)
                onOk(message);
        },
        std::move(onFail));
}

void ForumChatService::poll(int peerId, int afterId,
                            std::function<void(const ForumPollResult &)> onOk,
                            FailHandler onFail)
{
    QUrlQuery query;
    if (peerId > 0)
        query.addQueryItem(QStringLiteral("peer_id"), QString::number(peerId));
    if (peerId > 0 && afterId > 0)
        query.addQueryItem(QStringLiteral("after_id"), QString::number(afterId));
    const QString suffix =
        query.isEmpty() ? QString() : QStringLiteral("?%1").arg(query.toString(QUrl::FullyEncoded));
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/messages/poll%1").arg(suffix),
        [this, onOk](const QJsonObject &root) {
            ForumPollResult result;
            result.totalUnread = root.value(QStringLiteral("total_unread")).toInt();
            result.incomingRequests = root.value(QStringLiteral("incoming_requests")).toInt();
            const QJsonArray friends = root.value(QStringLiteral("friends")).toArray();
            for (const QJsonValue &value : friends)
            {
                const QJsonObject object = value.toObject();
                ForumFriendInfo peer = parsePeer(object);
                peer.unread = object.value(QStringLiteral("unread")).toInt();
                const QJsonObject last = object.value(QStringLiteral("last_message")).toObject();
                if (!last.isEmpty())
                {
                    peer.lastMessageBody = last.value(QStringLiteral("body")).toString();
                    peer.lastMessageAt =
                        static_cast<qint64>(last.value(QStringLiteral("created_at")).toDouble());
                    peer.lastMessageMine = last.value(QStringLiteral("mine")).toBool();
                }
                result.friends.append(peer);
            }
            const QJsonArray messages = root.value(QStringLiteral("messages")).toArray();
            for (const QJsonValue &value : messages)
                result.messages.append(parseMessage(value.toObject()));
            if (onOk)
                onOk(result);
        },
        std::move(onFail));
}
