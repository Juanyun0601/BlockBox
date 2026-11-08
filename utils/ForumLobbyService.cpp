/**
 * @file   ForumLobbyService.cpp
 * @brief  方块盒子论坛联机大厅接口实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumLobbyService.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <platform.h>
#include <QUrlQuery>

ForumLobbyService *ForumLobbyService::instance()
{
    static ForumLobbyService *s_instance = new ForumLobbyService();
    return s_instance;
}

ForumLobbyService::ForumLobbyService(QObject *parent) : QObject(parent) {}

QString ForumLobbyService::coreDisplayName(const QString &core)
{
    if (core == QStringLiteral("terracotta"))
        return tr("陶瓦联机");
    if (core == QStringLiteral("hongshi"))
        return tr("红石联机");
    if (core == QStringLiteral("frp"))
        return tr("内网穿透");
    return tr("直连");
}

ForumLobbyRoom ForumLobbyService::parseRoom(const QJsonObject &object)
{
    ForumLobbyRoom room;
    room.id = object.value(QStringLiteral("id")).toInt();
    room.title = object.value(QStringLiteral("title")).toString();
    room.description = object.value(QStringLiteral("description")).toString();
    room.core = object.value(QStringLiteral("core")).toString();
    room.coreLabel = object.value(QStringLiteral("core_label")).toString();
    if (room.coreLabel.isEmpty())
        room.coreLabel = coreDisplayName(room.core);
    room.inviteCode = object.value(QStringLiteral("invite_code")).toString();
    room.address = object.value(QStringLiteral("address")).toString();
    room.gameVersion = object.value(QStringLiteral("game_version")).toString();
    room.loader = object.value(QStringLiteral("loader")).toString();
    room.maxPlayers = object.value(QStringLiteral("max_players")).toInt(8);
    room.cover = object.value(QStringLiteral("cover")).toString();
    room.md = object.value(QStringLiteral("md")).toString();
    room.modpackMode = object.value(QStringLiteral("modpack_mode")).toString();
    if (room.modpackMode != QLatin1String("online") && room.modpackMode != QLatin1String("link"))
        room.modpackMode.clear();
    room.modpackName = object.value(QStringLiteral("modpack_name")).toString();
    room.modpackUrl = object.value(QStringLiteral("modpack_url")).toString();
    room.modpackMeta = QString::fromUtf8(
        QJsonDocument(object.value(QStringLiteral("modpack_meta")).toObject())
            .toJson(QJsonDocument::Compact));
    if (room.modpackMode.isEmpty() || room.modpackName.isEmpty())
    {
        room.modpackMode.clear();
        room.modpackName.clear();
        room.modpackUrl.clear();
        room.modpackMeta.clear();
    }
    room.status = object.value(QStringLiteral("status")).toString();
    room.joinCount = object.value(QStringLiteral("join_count")).toInt();
    room.createdAt = qint64(object.value(QStringLiteral("created_at")).toDouble());
    room.updatedAt = qint64(object.value(QStringLiteral("updated_at")).toDouble());
    room.expiresIn = object.value(QStringLiteral("expires_in")).toInt();
    room.mine = object.value(QStringLiteral("mine")).toBool();

    const QJsonArray mods = object.value(QStringLiteral("mods")).toArray();
    for (const QJsonValue &value : mods)
    {
        const QString name = value.toString().trimmed();
        if (!name.isEmpty())
            room.mods.append(name);
    }

    const QJsonObject host = object.value(QStringLiteral("host")).toObject();
    if (!host.isEmpty())
    {
        room.host.id = host.value(QStringLiteral("id")).toInt();
        room.host.username = host.value(QStringLiteral("username")).toString();
        room.host.displayName = host.value(QStringLiteral("display_name")).toString();
        room.host.avatar = host.value(QStringLiteral("avatar")).toString();
        if (room.host.displayName.isEmpty())
            room.host.displayName = room.host.username;
    }
    return room;
}

void ForumLobbyService::fetchRooms(const QString &keyword, const QString &core,
                                   RoomListHandler onOk, FailHandler onFail)
{
    QUrlQuery query;
    if (!keyword.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("q"), keyword.trimmed());
    if (!core.isEmpty())
        query.addQueryItem(QStringLiteral("core"), core);
    query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("60"));

    QString path = QStringLiteral("/api/lobby/rooms");
    if (!query.isEmpty())
        path += QLatin1Char('?') + query.toString(QUrl::FullyEncoded);

    ForumAccountService::instance()->getJson(
        path,
        [onOk](const QJsonObject &root) {
            QVector<ForumLobbyRoom> rooms;
            const QJsonArray array = root.value(QStringLiteral("rooms")).toArray();
            rooms.reserve(array.size());
            for (const QJsonValue &value : array)
                rooms.append(parseRoom(value.toObject()));
            if (onOk)
                onOk(rooms, root.value(QStringLiteral("total")).toInt(rooms.size()));
        },
        std::move(onFail));
}

void ForumLobbyService::publishRoom(const ForumLobbyRoom &room, RoomHandler onOk, FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("title"), room.title);
    body.insert(QStringLiteral("description"), room.description);
    body.insert(QStringLiteral("core"), room.core);
    body.insert(QStringLiteral("invite_code"), room.inviteCode);
    body.insert(QStringLiteral("address"), room.address);
    body.insert(QStringLiteral("game_version"), room.gameVersion);
    body.insert(QStringLiteral("loader"), room.loader);
    body.insert(QStringLiteral("max_players"), room.maxPlayers);
    if (!room.status.isEmpty())
        body.insert(QStringLiteral("status"), room.status);

    QJsonArray mods;
    for (const QString &name : room.mods)
        mods.append(name);
    body.insert(QStringLiteral("mods"), mods);

    // 封面 / Markdown 介绍 / 前置整合包：空值也照发，让再次发布能清掉旧值
    body.insert(QStringLiteral("cover"), room.cover);
    body.insert(QStringLiteral("md"), room.md);
    body.insert(QStringLiteral("modpack_mode"), room.modpackMode);
    body.insert(QStringLiteral("modpack_name"), room.modpackName);
    body.insert(QStringLiteral("modpack_url"), room.modpackUrl);
    if (!room.modpackMeta.isEmpty())
    {
        const QJsonDocument metaDoc = QJsonDocument::fromJson(room.modpackMeta.toUtf8());
        if (metaDoc.isObject())
            body.insert(QStringLiteral("modpack_meta"), metaDoc.object());
    }

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/lobby/rooms"), body,
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(parseRoom(root.value(QStringLiteral("room")).toObject()));
        },
        std::move(onFail));
}

void ForumLobbyService::uploadImage(const QString &filePath, UploadHandler onDone)
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        if (onDone)
            onDone(QString(), tr("需要先登录论坛账号才能上传图片"));
        return;
    }

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile() || info.size() <= 0)
    {
        if (onDone)
            onDone(QString(), tr("打不开这张图片，换个文件试试"));
        return;
    }

    // 论坛 /api/upload 收 4 MB；先原样读，超过限制再用 QImage 压一遍
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (onDone)
            onDone(QString(), tr("打不开这张图片，换个文件试试"));
        return;
    }
    QByteArray data = file.readAll();
    file.close();

    QString mime;
    {
        const QByteArray format = QImageReader(filePath).format().toLower();
        if (format == QByteArrayLiteral("png"))
            mime = QStringLiteral("image/png");
        else if (format == QByteArrayLiteral("jpg") || format == QByteArrayLiteral("jpeg"))
            mime = QStringLiteral("image/jpeg");
        else if (format == QByteArrayLiteral("webp"))
            mime = QStringLiteral("image/webp");
        else if (format == QByteArrayLiteral("gif"))
            mime = QStringLiteral("image/gif");
    }

    // 非 GIF（动图重新编码会丢帧）且超了 4 MB：缩到长边 2560、压到 3.5 MB 以内再发
    const qint64 kLimit = 4 * 1024 * 1024;
    const QImage source = [&]() {
        QImageReader reader(filePath);
        const QSize size = reader.size();
        const int longEdge = qMax(size.width(), size.height());
        if (longEdge <= 2560)
            return QImage(filePath);
        QSize scaled = size.scaled(2560, 2560, Qt::KeepAspectRatio);
        reader.setScaledSize(scaled);
        return reader.read();
    }();
    if (data.size() > kLimit && mime != QLatin1String("image/gif") && !source.isNull())
    {
        QByteArray buffer;
        QBuffer stream(&buffer);
        stream.open(QIODevice::WriteOnly);
        if (mime != QLatin1String("image/webp") && source.save(&stream, "PNG"))
        {
            mime = QStringLiteral("image/png");
            int quality = 90;
            while (quality >= 50)
            {
                buffer.clear();
                stream.seek(0);
                if (source.save(&stream, "JPEG", quality))
                {
                    mime = QStringLiteral("image/jpeg");
                    if (buffer.size() <= 3500 * 1024)
                        break;
                }
                quality -= 10;
            }
            data = buffer;
        }
    }

    if (data.isEmpty() || data.size() > kLimit)
    {
        if (onDone)
            onDone(QString(), tr("这张图片压不进 4 MB 的图床限制，换一张试试"));
        return;
    }
    if (mime.isEmpty())
        mime = QStringLiteral("image/png");

    QNetworkRequest request{QUrl(ForumAccountService::instance()->absoluteUrl(QStringLiteral("/api/upload")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, mime);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(30000);
    const QString token = ForumAccountService::instance()->token();
    if (!token.isEmpty())
        request.setRawHeader("Cookie", QStringLiteral("bbf_sid=%1").arg(token).toUtf8());
    const QString humanToken = ForumAccountService::instance()->humanCheckToken();
    if (!humanToken.isEmpty())
        request.setRawHeader("X-Human-Token", humanToken.toUtf8());

    QNetworkAccessManager *network = new QNetworkAccessManager(this);
    QNetworkReply *reply = network->post(request, data);
    connect(reply, &QNetworkReply::finished, this,
            [reply, network, onDone]() {
                reply->deleteLater();
                network->deleteLater();
                const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                if (reply->error() != QNetworkReply::NoError)
                {
                    QString message = doc.object().value(QStringLiteral("error")).toString();
                    if (message.isEmpty())
                        message = tr("图片上传失败，请稍后再试");
                    if (onDone)
                        onDone(QString(), message);
                    return;
                }
                const QString url = doc.object().value(QStringLiteral("url")).toString();
                if (url.isEmpty())
                {
                    if (onDone)
                        onDone(QString(), tr("图片上传失败，请稍后再试"));
                    return;
                }
                if (onDone)
                    onDone(url, QString());
            });
}

void ForumLobbyService::sendHeartbeat(int roomId, const QString &status,
                                      RoomHandler onOk, FailHandler onFail)
{
    if (roomId <= 0)
    {
        if (onFail)
            onFail(tr("房间还没发布"), 0);
        return;
    }

    QJsonObject body;
    if (!status.isEmpty())
        body.insert(QStringLiteral("status"), status);

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"),
        QStringLiteral("/api/lobby/rooms/%1/heartbeat").arg(roomId), body,
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(parseRoom(root.value(QStringLiteral("room")).toObject()));
        },
        std::move(onFail));
}

void ForumLobbyService::closeRoom(int roomId, OkHandler onOk, FailHandler onFail)
{
    if (roomId <= 0)
    {
        if (onOk)
            onOk();
        return;
    }

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("DELETE"), QStringLiteral("/api/lobby/rooms/%1").arg(roomId),
        QJsonObject(),
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumLobbyService::recordJoin(int roomId, RoomHandler onOk, FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/lobby/rooms/%1/join").arg(roomId),
        QJsonObject(),
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(parseRoom(root.value(QStringLiteral("room")).toObject()));
        },
        std::move(onFail));
}

// ────────────────────────────────────────────── 发布模板（本机 JSON 持久化）

namespace {

QString publishTemplatePath()
{
    return Platform::getDataDirectory() + QStringLiteral("/lobby_publish_templates.json");
}

QJsonObject templateToObject(const ForumLobbyService::PublishTemplate &tpl)
{
    QJsonObject object;
    object.insert(QStringLiteral("title"), tpl.title);
    object.insert(QStringLiteral("description"), tpl.description);
    object.insert(QStringLiteral("max_players"), tpl.maxPlayers);
    object.insert(QStringLiteral("cover"), tpl.cover);
    object.insert(QStringLiteral("cover_path"), tpl.coverPath);
    object.insert(QStringLiteral("md"), tpl.md);
    object.insert(QStringLiteral("modpack_mode"), tpl.modpackMode);
    object.insert(QStringLiteral("modpack_name"), tpl.modpackName);
    object.insert(QStringLiteral("modpack_url"), tpl.modpackUrl);
    object.insert(QStringLiteral("modpack_meta"), tpl.modpackMeta);
    return object;
}

ForumLobbyService::PublishTemplate templateFromObject(const QJsonObject &object)
{
    ForumLobbyService::PublishTemplate tpl;
    tpl.title = object.value(QStringLiteral("title")).toString();
    tpl.description = object.value(QStringLiteral("description")).toString();
    tpl.maxPlayers = object.value(QStringLiteral("max_players")).toInt(8);
    if (tpl.maxPlayers <= 0 || tpl.maxPlayers > 64)
        tpl.maxPlayers = 8;
    tpl.cover = object.value(QStringLiteral("cover")).toString();
    if (!tpl.cover.startsWith(QLatin1String("/uploads/")))
        tpl.cover.clear();
    tpl.coverPath = object.value(QStringLiteral("cover_path")).toString();
    tpl.md = object.value(QStringLiteral("md")).toString();
    tpl.modpackMode = object.value(QStringLiteral("modpack_mode")).toString();
    if (tpl.modpackMode != QLatin1String("online") && tpl.modpackMode != QLatin1String("link"))
        tpl.modpackMode.clear();
    tpl.modpackName = object.value(QStringLiteral("modpack_name")).toString();
    tpl.modpackUrl = object.value(QStringLiteral("modpack_url")).toString();
    tpl.modpackMeta = object.value(QStringLiteral("modpack_meta")).toString();
    return tpl;
}

} // namespace

ForumLobbyService::PublishTemplate ForumLobbyService::loadPublishTemplate()
{
    QFile file(publishTemplatePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return {};
    return templateFromObject(doc.object());
}

void ForumLobbyService::savePublishTemplate(const PublishTemplate &tpl)
{
    QFile file(publishTemplatePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(templateToObject(tpl)).toJson(QJsonDocument::Indented));
    file.close();
}
