/**
 * @file   ForumChatService.cpp
 * @brief  方块盒子论坛好友与私信服务实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumChatService.h"

#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
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
    peer.official = object.value(QStringLiteral("official")).toBool();
    peer.lastSeenAt = static_cast<qint64>(object.value(QStringLiteral("last_seen_at")).toDouble());
    return peer;
}

ForumChatMessage ForumChatService::parseMessage(const QJsonObject &object)
{
    ForumChatMessage message;
    message.id = object.value(QStringLiteral("id")).toInt();
    message.kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("text"));
    message.body = object.value(QStringLiteral("body")).toString();
    message.image = object.value(QStringLiteral("image")).toString();
    message.replyTo = object.value(QStringLiteral("reply_to")).toInt();
    message.replyBody = object.value(QStringLiteral("reply_body")).toString();
    message.replyMine = object.value(QStringLiteral("reply_mine")).toInt() != 0;
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
                    peer.lastMessageKind = last.value(QStringLiteral("kind")).toString(QStringLiteral("text"));
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

void ForumChatService::sendMessage(int userId, const QString &body, int replyTo,
                                   std::function<void(const ForumChatMessage &)> onOk,
                                   FailHandler onFail)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("text"));
    payload.insert(QStringLiteral("body"), body);
    if (replyTo > 0)
        payload.insert(QStringLiteral("reply_to"), replyTo);
    postMessage(userId, payload, std::move(onOk), std::move(onFail));
}

void ForumChatService::withdrawMessage(int messageId, std::function<void()> onOk, FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("DELETE"), QStringLiteral("/api/messages/%1").arg(messageId), {},
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumChatService::postMessage(int userId, const QJsonObject &payload,
                                   std::function<void(const ForumChatMessage &)> onOk,
                                   FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/messages/%1").arg(userId), payload,
        [this, onOk](const QJsonObject &root) {
            const ForumChatMessage message = parseMessage(root.value(QStringLiteral("message")).toObject());
            if (onOk)
                onOk(message);
        },
        std::move(onFail));
}

bool ForumChatService::prepareImagePayload(const QString &filePath, QByteArray &data, QString &mime,
                                           QString &error)
{
    QImageReader reader(filePath);
    QImage image = reader.read();
    if (image.isNull())
    {
        error = QObject::tr("打不开这张图片，换个文件试试");
        return false;
    }
    // 最长边 1600，够聊天看了
    if (image.width() > 1600 || image.height() > 1600)
        image = image.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // 有透明通道用 PNG，否则用 JPEG 逐级压到 4 MB 以内（论坛图床上限）
    const bool hasAlpha = image.hasAlphaChannel();
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    if (hasAlpha)
    {
        image.save(&buffer, "PNG");
        mime = QStringLiteral("image/png");
    }
    else
    {
        int quality = 88;
        while (quality >= 40)
        {
            data.clear();
            buffer.seek(0);
            image.save(&buffer, "JPEG", quality);
            mime = QStringLiteral("image/jpeg");
            if (data.size() <= 3500 * 1024)
                break;
            quality -= 12;
        }
    }
    if (data.isEmpty() || data.size() > 4 * 1024 * 1024)
    {
        error = QObject::tr("这张图片压不进 4 MB 的图床限制，换一张试试");
        return false;
    }
    return true;
}

void ForumChatService::uploadImage(const QByteArray &data, const QString &mime,
                                   std::function<void(const QString &url)> onOk, FailHandler onFail)
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QNetworkRequest request{QUrl(ForumAccountService::instance()->absoluteUrl(QStringLiteral("/api/upload")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, mime);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(30000);
    const QString token = ForumAccountService::instance()->token();
    if (!token.isEmpty())
        request.setRawHeader("Cookie", QStringLiteral("bbf_sid=%1").arg(token).toUtf8());
    // 论坛开了人机验证时，上传也要带免验令牌
    const QString humanToken = ForumAccountService::instance()->humanCheckToken();
    if (!humanToken.isEmpty())
        request.setRawHeader("X-Human-Token", humanToken.toUtf8());

    QNetworkReply *reply = m_network->post(request, data);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, onOk, onFail]() {
                         reply->deleteLater();
                         const QByteArray raw = reply->readAll();
                         if (reply->error() != QNetworkReply::NoError)
                         {
                             // 论坛的错误体里带着中文原因，优先透传
                             QJsonParseError parseError{};
                             const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
                             QString message = parseError.error == QJsonParseError::NoError
                                                   ? doc.object().value(QStringLiteral("error")).toString()
                                                   : QString();
                             if (message.isEmpty())
                                 message = QObject::tr("图片上传失败，请稍后再试");
                             if (onFail)
                                 onFail(message, reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
                             return;
                         }
                         const QJsonDocument doc = QJsonDocument::fromJson(raw);
                         const QString url = doc.object().value(QStringLiteral("url")).toString();
                         if (url.isEmpty())
                         {
                             if (onFail)
                                 onFail(QObject::tr("图片上传失败，请稍后再试"), 0);
                             return;
                         }
                         if (onOk)
                             onOk(url);
                     });
}

void ForumChatService::sendImageMessage(int userId, const QString &filePath, const QString &caption,
                                        int replyTo,
                                        std::function<void(const ForumChatMessage &)> onOk,
                                        FailHandler onFail)
{
    QByteArray data;
    QString mime;
    QString error;
    if (!prepareImagePayload(filePath, data, mime, error))
    {
        if (onFail)
            onFail(error, 0);
        return;
    }
    uploadImage(
        data, mime,
        [this, userId, caption, replyTo, onOk, onFail](const QString &url) {
            QJsonObject payload;
            payload.insert(QStringLiteral("kind"), QStringLiteral("image"));
            payload.insert(QStringLiteral("image"), url);
            if (!caption.isEmpty())
                payload.insert(QStringLiteral("body"), caption);
            if (replyTo > 0)
                payload.insert(QStringLiteral("reply_to"), replyTo);
            postMessage(userId, payload, std::move(onOk), std::move(onFail));
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
                    peer.lastMessageKind = last.value(QStringLiteral("kind")).toString(QStringLiteral("text"));
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
