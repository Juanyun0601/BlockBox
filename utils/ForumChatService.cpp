/**
 * @file   ForumChatService.cpp
 * @brief  方块盒子论坛好友与私信服务实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumChatService.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
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
    // P2P 直传邀约 / 控制消息的数据都装在 body 里（一段紧凑 JSON）
    if (message.kind == QStringLiteral("p2p") && !message.body.isEmpty())
    {
        const QJsonDocument doc = QJsonDocument::fromJson(message.body.toUtf8());
        if (doc.isObject())
            message.p2p = ChatP2POffer::fromJson(doc.object());
    }
    message.image = object.value(QStringLiteral("image")).toString();
    message.fileName = object.value(QStringLiteral("file_name")).toString();
    message.fileUrl = object.value(QStringLiteral("file_url")).toString();
    message.fileSize = static_cast<qint64>(object.value(QStringLiteral("file_size")).toDouble());
    message.fileDlUrl = object.value(QStringLiteral("dl_url")).toString();
    message.replyTo = object.value(QStringLiteral("reply_to")).toInt();
    message.replyBody = object.value(QStringLiteral("reply_body")).toString();
    message.replyMine = object.value(QStringLiteral("reply_mine")).toInt() != 0;
    message.mine = object.value(QStringLiteral("mine")).toBool();
    message.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
    message.read = object.value(QStringLiteral("read_at")).toDouble() > 0;
    // 群消息带 sender；私信没有这个字段，保持空即可
    const QJsonObject sender = object.value(QStringLiteral("sender")).toObject();
    if (!sender.isEmpty())
    {
        message.senderId = sender.value(QStringLiteral("id")).toInt();
        message.senderName = sender.value(QStringLiteral("display_name")).toString();
        if (message.senderName.isEmpty())
            message.senderName = sender.value(QStringLiteral("username")).toString();
        message.senderAvatar = sender.value(QStringLiteral("avatar")).toString();
    }
    return message;
}

ForumGroupInfo ForumChatService::parseGroup(const QJsonObject &object)
{
    ForumGroupInfo group;
    group.id = object.value(QStringLiteral("id")).toInt();
    group.name = object.value(QStringLiteral("name")).toString();
    group.avatar = object.value(QStringLiteral("avatar")).toString();
    group.ownerId = object.value(QStringLiteral("owner_id")).toInt();
    group.official = object.value(QStringLiteral("official")).toBool();
    group.memberCount = object.value(QStringLiteral("member_count")).toInt();
    group.unread = object.value(QStringLiteral("unread")).toInt();
    const QJsonObject last = object.value(QStringLiteral("last_message")).toObject();
    if (!last.isEmpty())
    {
        group.lastMessageBody = last.value(QStringLiteral("body")).toString();
        group.lastMessageKind = last.value(QStringLiteral("kind")).toString(QStringLiteral("text"));
        group.lastMessageAt = static_cast<qint64>(last.value(QStringLiteral("created_at")).toDouble());
        group.lastMessageMine = last.value(QStringLiteral("mine")).toBool();
        group.lastMessageSender = last.value(QStringLiteral("sender_name")).toString();
    }
    return group;
}

ForumFriendInfo ForumChatService::parseFriendRow(const QJsonObject &object)
{
    ForumFriendInfo peer = parsePeer(object);
    peer.unread = object.value(QStringLiteral("unread")).toInt();
    const QJsonObject last = object.value(QStringLiteral("last_message")).toObject();
    if (!last.isEmpty())
    {
        peer.lastMessageBody = last.value(QStringLiteral("body")).toString();
        peer.lastMessageKind = last.value(QStringLiteral("kind")).toString(QStringLiteral("text"));
        peer.lastMessageAt = static_cast<qint64>(last.value(QStringLiteral("created_at")).toDouble());
        peer.lastMessageMine = last.value(QStringLiteral("mine")).toBool();
    }
    return peer;
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
                snapshot.friends.append(parseFriendRow(value.toObject()));

            for (const QJsonValue &value : root.value(QStringLiteral("groups")).toArray())
                snapshot.groups.append(parseGroup(value.toObject()));

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

// ────────────────────────────────────────────────────────────── 群聊

void ForumChatService::createGroup(const QString &name, const QString &avatar,
                                   const QVector<int> &memberIds,
                                   std::function<void(const ForumGroupInfo &)> onOk,
                                   FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("name"), name);
    if (!avatar.trimmed().isEmpty())
        body.insert(QStringLiteral("avatar"), avatar.trimmed());
    QJsonArray ids;
    for (const int id : memberIds)
        ids.append(id);
    body.insert(QStringLiteral("member_ids"), ids);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/groups"), body,
        [this, onOk](const QJsonObject &root) {
            const ForumGroupInfo group = parseGroup(root.value(QStringLiteral("group")).toObject());
            if (onOk)
                onOk(group);
        },
        std::move(onFail));
}

void ForumChatService::uploadGroupIcon(const QString &filePath,
                                       std::function<void(const QString &url)> onOk,
                                       FailHandler onFail)
{
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
    {
        if (onFail)
            onFail(QObject::tr("打不开这张图片，换个文件试试"), 0);
        return;
    }
    // 群图标最大显示也就 40 多像素，缩到 512 传上去足够清晰
    if (image.width() > 512 || image.height() > 512)
        image = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QString mime;
    if (image.hasAlphaChannel())
    {
        image.save(&buffer, "PNG");
        mime = QStringLiteral("image/png");
    }
    else
    {
        image.save(&buffer, "JPEG", 90);
        mime = QStringLiteral("image/jpeg");
    }
    if (data.isEmpty())
    {
        if (onFail)
            onFail(QObject::tr("这张图片处理失败，换一张试试"), 0);
        return;
    }
    uploadImage(data, mime, std::move(onOk), std::move(onFail));
}

void ForumChatService::inviteGroupMembers(int groupId, const QVector<int> &memberIds,
                                          std::function<void(int added, int memberCount)> onOk,
                                          FailHandler onFail)
{
    QJsonObject body;
    QJsonArray ids;
    for (const int id : memberIds)
        ids.append(id);
    body.insert(QStringLiteral("member_ids"), ids);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/groups/%1/members").arg(groupId), body,
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(root.value(QStringLiteral("added")).toInt(),
                     root.value(QStringLiteral("member_count")).toInt());
        },
        std::move(onFail));
}

void ForumChatService::fetchGroupMessages(int groupId, int beforeId,
                                          std::function<void(const ForumGroupDetail &)> onOk,
                                          FailHandler onFail)
{
    QUrlQuery query;
    if (beforeId > 0)
        query.addQueryItem(QStringLiteral("before_id"), QString::number(beforeId));
    const QString suffix = query.isEmpty() ? QString() : QStringLiteral("?%1")
                                                                    .arg(query.toString(QUrl::FullyEncoded));
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/groups/%1/messages%2").arg(groupId).arg(suffix),
        [this, onOk](const QJsonObject &root) {
            ForumGroupDetail detail;
            detail.group = parseGroup(root.value(QStringLiteral("group")).toObject());
            const QJsonArray members = root.value(QStringLiteral("members")).toArray();
            for (const QJsonValue &value : members)
            {
                const QJsonObject object = value.toObject();
                ForumGroupMember member;
                member.user = parsePeer(object);
                member.owner = object.value(QStringLiteral("owner")).toBool();
                detail.members.append(member);
            }
            const QJsonArray array = root.value(QStringLiteral("messages")).toArray();
            for (const QJsonValue &value : array)
                detail.messages.append(parseMessage(value.toObject()));
            detail.hasMore = root.value(QStringLiteral("has_more")).toBool();
            if (onOk)
                onOk(detail);
        },
        std::move(onFail));
}

void ForumChatService::sendGroupMessage(int groupId, const QString &body, int replyTo,
                                        std::function<void(const ForumChatMessage &)> onOk,
                                        FailHandler onFail)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("text"));
    payload.insert(QStringLiteral("body"), body);
    if (replyTo > 0)
        payload.insert(QStringLiteral("reply_to"), replyTo);
    postGroupMessage(groupId, payload, std::move(onOk), std::move(onFail));
}

void ForumChatService::withdrawGroupMessage(int messageId, std::function<void()> onOk,
                                            FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("DELETE"), QStringLiteral("/api/group-messages/%1").arg(messageId), {},
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumChatService::leaveGroup(int groupId, std::function<void(bool dissolved)> onOk,
                                  FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("DELETE"), QStringLiteral("/api/groups/%1").arg(groupId), {},
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(root.value(QStringLiteral("dissolved")).toBool());
        },
        std::move(onFail));
}

void ForumChatService::postGroupMessage(int groupId, const QJsonObject &payload,
                                        std::function<void(const ForumChatMessage &)> onOk,
                                        FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/groups/%1/messages").arg(groupId), payload,
        [this, onOk](const QJsonObject &root) {
            const ForumChatMessage message = parseMessage(root.value(QStringLiteral("message")).toObject());
            if (onOk)
                onOk(message);
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
    if (data.isEmpty() || data.size() > 5 * 1024 * 1024)
    {
        error = QObject::tr("这张图片压不进 5 MB 的上限，换一张试试");
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

void ForumChatService::sendGroupImageMessage(int groupId, const QString &filePath,
                                             const QString &caption, int replyTo,
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
        [this, groupId, caption, replyTo, onOk, onFail](const QString &url) {
            QJsonObject payload;
            payload.insert(QStringLiteral("kind"), QStringLiteral("image"));
            payload.insert(QStringLiteral("image"), url);
            if (!caption.isEmpty())
                payload.insert(QStringLiteral("body"), caption);
            if (replyTo > 0)
                payload.insert(QStringLiteral("reply_to"), replyTo);
            postGroupMessage(groupId, payload, std::move(onOk), std::move(onFail));
        },
        std::move(onFail));
}

// ────────────────────────────────────────────────────────────── 聊天文件

QString ForumChatService::checkChatFile(const QString &filePath)
{
    QFileInfo info(filePath);
    if (!info.isFile() || info.size() <= 0)
        return QObject::tr("打不开这个文件，换个文件试试");
    if (info.size() > 5 * 1024 * 1024)
        return QObject::tr("文件超过 5 MB 了，发不了这么大的");
    static const QStringList blocked = {
        QStringLiteral("exe"), QStringLiteral("msi"), QStringLiteral("bat"), QStringLiteral("cmd"),
        QStringLiteral("scr"), QStringLiteral("vbs"), QStringLiteral("com"), QStringLiteral("pif"),
        QStringLiteral("cpl"),
    };
    if (blocked.contains(info.suffix().toLower()))
        return QObject::tr("程序安装包（exe/msi/bat 等）不支持发送，先打包成压缩包再发");
    return QString();
}

void ForumChatService::sendP2POffer(int userId, const ChatP2POffer &offer, int replyTo,
                                    std::function<void(const ForumChatMessage &)> onOk,
                                    FailHandler onFail)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("p2p"));
    payload.insert(QStringLiteral("body"),
                   QString::fromUtf8(QJsonDocument(offer.toJson()).toJson(QJsonDocument::Compact)));
    payload.insert(QStringLiteral("file_name"), offer.name);
    payload.insert(QStringLiteral("file_size"), offer.size);
    if (replyTo > 0)
        payload.insert(QStringLiteral("reply_to"), replyTo);
    postMessage(userId, payload, std::move(onOk), std::move(onFail));
}

void ForumChatService::sendP2PControl(int userId, const QString &tid, const QString &act,
                                      std::function<void(const ForumChatMessage &)> onOk,
                                      FailHandler onFail)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("p2p"));
    QJsonObject body;
    body.insert(QStringLiteral("v"), 1);
    body.insert(QStringLiteral("tid"), tid);
    body.insert(QStringLiteral("act"), act);
    payload.insert(QStringLiteral("body"),
                   QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));
    postMessage(userId, payload, std::move(onOk), std::move(onFail));
}

void ForumChatService::uploadChatFile(const QString &filePath, const ProgressHandler &onProgress,
                                      std::function<void(const QString &url, const QString &name)> onOk,
                                      FailHandler onFail)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (onFail)
            onFail(QObject::tr("打不开这个文件，换个文件试试"), 0);
        return;
    }
    const QByteArray data = file.readAll();
    file.close();
    const QString name = QFileInfo(filePath).fileName();

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QNetworkRequest request{
        QUrl(ForumAccountService::instance()->absoluteUrl(QStringLiteral("/api/upload/chat")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/octet-stream"));
    // 原始文件名放自定义头里（URL 编码），服务端会解开并消毒后存档
    request.setRawHeader("X-File-Name", QString::fromUtf8(QUrl::toPercentEncoding(name)).toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(120000);
    const QString token = ForumAccountService::instance()->token();
    if (!token.isEmpty())
        request.setRawHeader("Cookie", QStringLiteral("bbf_sid=%1").arg(token).toUtf8());
    const QString humanToken = ForumAccountService::instance()->humanCheckToken();
    if (!humanToken.isEmpty())
        request.setRawHeader("X-Human-Token", humanToken.toUtf8());

    QNetworkReply *reply = m_network->post(request, data);
    if (onProgress)
    {
        QObject::connect(reply, &QNetworkReply::uploadProgress, reply,
                         [onProgress](qint64 sent, qint64 total) { onProgress(sent, total); });
    }
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, onOk = std::move(onOk), onFail = std::move(onFail)]() mutable {
                         reply->deleteLater();
                         const QByteArray raw = reply->readAll();
                         if (reply->error() != QNetworkReply::NoError)
                         {
                             QJsonParseError parseError{};
                             const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
                             QString message = parseError.error == QJsonParseError::NoError
                                                   ? doc.object().value(QStringLiteral("error")).toString()
                                                   : QString();
                             if (message.isEmpty())
                                 message = QObject::tr("文件上传失败，请稍后再试");
                             if (onFail)
                                 onFail(message, reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
                             return;
                         }
                         const QJsonObject object = QJsonDocument::fromJson(raw).object();
                         const QString url = object.value(QStringLiteral("url")).toString();
                         if (url.isEmpty())
                         {
                             if (onFail)
                                 onFail(QObject::tr("文件上传失败，请稍后再试"), 0);
                             return;
                         }
                         if (onOk)
                             onOk(url, object.value(QStringLiteral("name")).toString());
                     });
}

void ForumChatService::sendFileMessage(int userId, const QString &filePath, const QString &caption,
                                       int replyTo, const ProgressHandler &onProgress,
                                       std::function<void(const ForumChatMessage &)> onOk,
                                       FailHandler onFail)
{
    const QString problem = checkChatFile(filePath);
    if (!problem.isEmpty())
    {
        if (onFail)
            onFail(problem, 0);
        return;
    }
    uploadChatFile(
        filePath, onProgress,
        [this, userId, caption, replyTo, onOk, onFail](const QString &url, const QString &name) {
            QJsonObject payload;
            payload.insert(QStringLiteral("kind"), QStringLiteral("file"));
            payload.insert(QStringLiteral("file_url"), url);
            payload.insert(QStringLiteral("file_name"), name);
            if (!caption.isEmpty())
                payload.insert(QStringLiteral("body"), caption);
            if (replyTo > 0)
                payload.insert(QStringLiteral("reply_to"), replyTo);
            postMessage(userId, payload, std::move(onOk), std::move(onFail));
        },
        std::move(onFail));
}

void ForumChatService::sendGroupFileMessage(int groupId, const QString &filePath,
                                            const QString &caption, int replyTo,
                                            const ProgressHandler &onProgress,
                                            std::function<void(const ForumChatMessage &)> onOk,
                                            FailHandler onFail)
{
    const QString problem = checkChatFile(filePath);
    if (!problem.isEmpty())
    {
        if (onFail)
            onFail(problem, 0);
        return;
    }
    uploadChatFile(
        filePath, onProgress,
        [this, groupId, caption, replyTo, onOk, onFail](const QString &url, const QString &name) {
            QJsonObject payload;
            payload.insert(QStringLiteral("kind"), QStringLiteral("file"));
            payload.insert(QStringLiteral("file_url"), url);
            payload.insert(QStringLiteral("file_name"), name);
            if (!caption.isEmpty())
                payload.insert(QStringLiteral("body"), caption);
            if (replyTo > 0)
                payload.insert(QStringLiteral("reply_to"), replyTo);
            postGroupMessage(groupId, payload, std::move(onOk), std::move(onFail));
        },
        std::move(onFail));
}

void ForumChatService::extractFile(bool group, int messageId,
                                   std::function<void(const ForumChatFileExtract &)> onOk,
                                   FailHandler onFail)
{
    const QString path = group ? QStringLiteral("/api/groups/files/%1/extract").arg(messageId)
                               : QStringLiteral("/api/messages/file/%1/extract").arg(messageId);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), path, {},
        [onOk](const QJsonObject &root) {
            ForumChatFileExtract extract;
            extract.url = root.value(QStringLiteral("url")).toString();
            extract.dlUrl = root.value(QStringLiteral("dl_url")).toString();
            if (extract.dlUrl.isEmpty())
                extract.dlUrl = extract.url;
            extract.name = root.value(QStringLiteral("name")).toString();
            extract.size = static_cast<qint64>(root.value(QStringLiteral("size")).toDouble());
            extract.remaining = static_cast<qint64>(root.value(QStringLiteral("remaining")).toDouble());
            if (onOk)
                onOk(extract);
        },
        std::move(onFail));
}

void ForumChatService::downloadChatFile(const QString &urlOrPath, const QString &savePath,
                                        const ProgressHandler &onProgress,
                                        std::function<void()> onOk, FailHandler onFail)
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    const QUrl url = QUrl(urlOrPath.startsWith(QStringLiteral("http"))
                              ? urlOrPath
                              : ForumAccountService::instance()->absoluteUrl(urlOrPath));
    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
    request.setTransferTimeout(120000);

    QNetworkReply *reply = m_network->get(request);
    if (onProgress)
    {
        QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                         [onProgress](qint64 received, qint64 total) { onProgress(received, total); });
    }
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, savePath, onOk = std::move(onOk), onFail = std::move(onFail)]() mutable {
                         reply->deleteLater();
                         if (reply->error() != QNetworkReply::NoError)
                         {
                             if (onFail)
                                 onFail(QObject::tr("文件下载失败，请稍后再试"),
                                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
                             return;
                         }
                         QFile file(savePath);
                         if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                         {
                             if (onFail)
                                 onFail(QObject::tr("保存不了这个位置，换一个保存位置试试"), 0);
                             return;
                         }
                         file.write(reply->readAll());
                         file.close();
                         if (onOk)
                             onOk();
                     });
}

void ForumChatService::fetchChatQuota(std::function<void(const ForumChatQuota &)> onOk,
                                      FailHandler onFail)
{
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/chat/quota"),
        [onOk](const QJsonObject &root) {
            ForumChatQuota quota;
            quota.used = static_cast<qint64>(root.value(QStringLiteral("used")).toDouble());
            quota.limit = static_cast<qint64>(root.value(QStringLiteral("limit")).toDouble());
            quota.remaining = static_cast<qint64>(root.value(QStringLiteral("remaining")).toDouble());
            if (onOk)
                onOk(quota);
        },
        std::move(onFail));
}

void ForumChatService::poll(int peerId, int groupId, int afterId, int groupAfterId,
                            std::function<void(const ForumPollResult &)> onOk,
                            FailHandler onFail)
{
    QUrlQuery query;
    if (peerId > 0)
        query.addQueryItem(QStringLiteral("peer_id"), QString::number(peerId));
    if (peerId > 0 && afterId > 0)
        query.addQueryItem(QStringLiteral("after_id"), QString::number(afterId));
    if (groupId > 0)
        query.addQueryItem(QStringLiteral("group_id"), QString::number(groupId));
    if (groupId > 0 && groupAfterId > 0)
        query.addQueryItem(QStringLiteral("group_after_id"), QString::number(groupAfterId));
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
                result.friends.append(parseFriendRow(value.toObject()));
            for (const QJsonValue &value : root.value(QStringLiteral("groups")).toArray())
                result.groups.append(parseGroup(value.toObject()));
            const QJsonArray messages = root.value(QStringLiteral("messages")).toArray();
            for (const QJsonValue &value : messages)
                result.messages.append(parseMessage(value.toObject()));
            const QJsonArray groupMessages = root.value(QStringLiteral("group_messages")).toArray();
            for (const QJsonValue &value : groupMessages)
                result.groupMessages.append(parseMessage(value.toObject()));
            if (onOk)
                onOk(result);
        },
        std::move(onFail));
}
