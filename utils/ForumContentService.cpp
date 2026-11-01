/**
 * @file   ForumContentService.cpp
 * @brief  方块盒子论坛内容服务实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumContentService.h"

#include <QJsonArray>
#include <QUrlQuery>

namespace {

/** 把标签数组 / 逗号分隔字符串统一成 QStringList */
QStringList parseTags(const QJsonValue &value)
{
    QStringList tags;
    if (value.isArray())
    {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
        {
            const QString tag = item.toString().trimmed();
            if (!tag.isEmpty())
                tags.append(tag);
        }
    }
    else if (value.isString())
    {
        const QStringList parts = value.toString().split(QLatin1Char(','));
        for (const QString &part : parts)
        {
            const QString tag = part.trimmed();
            if (!tag.isEmpty())
                tags.append(tag);
        }
    }
    return tags;
}

} // namespace

ForumContentService *ForumContentService::instance()
{
    static ForumContentService *s_instance = new ForumContentService();
    return s_instance;
}

ForumContentService::ForumContentService(QObject *parent) : QObject(parent) {}

// ────────────────────────────────────────────────────────────── 浏览

void ForumContentService::fetchBoards(std::function<void(const QVector<ForumBoard> &)> onOk,
                                      FailHandler onFail)
{
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/boards"),
        [this, onOk](const QJsonObject &root) {
            QVector<ForumBoard> boards;
            const QJsonArray array = root.value(QStringLiteral("boards")).toArray();
            for (const QJsonValue &value : array)
                boards.append(parseBoard(value.toObject()));
            if (onOk)
                onOk(boards);
        },
        std::move(onFail));
}

void ForumContentService::fetchBoardThreads(
    const QString &slug, const QString &sort, int page,
    std::function<void(const ForumBoard &, const QVector<ForumThreadInfo> &,
                       const ForumPagination &)> onOk,
    FailHandler onFail)
{
    QUrlQuery query;
    if (!sort.isEmpty())
        query.addQueryItem(QStringLiteral("sort"), sort);
    if (page > 1)
        query.addQueryItem(QStringLiteral("page"), QString::number(page));

    QString path = QStringLiteral("/api/boards/%1").arg(slug);
    if (!query.isEmpty())
        path += QLatin1Char('?') + query.toString(QUrl::FullyEncoded);

    ForumAccountService::instance()->getJson(
        path,
        [this, onOk](const QJsonObject &root) {
            const ForumBoard board = parseBoard(root.value(QStringLiteral("board")).toObject());
            QVector<ForumThreadInfo> threads;
            const QJsonArray array = root.value(QStringLiteral("threads")).toArray();
            for (const QJsonValue &value : array)
                threads.append(parseThreadInfo(value.toObject()));
            const ForumPagination pagination = parsePagination(root.value(QStringLiteral("pagination")).toObject());
            if (onOk)
                onOk(board, threads, pagination);
        },
        std::move(onFail));
}

void ForumContentService::fetchTagThreads(
    const QString &tag, const QString &sort, int page,
    std::function<void(const QVector<ForumThreadInfo> &, const ForumPagination &)> onOk,
    FailHandler onFail)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("tag"), tag);
    if (!sort.isEmpty())
        query.addQueryItem(QStringLiteral("sort"), sort);
    if (page > 1)
        query.addQueryItem(QStringLiteral("page"), QString::number(page));

    const QString path = QStringLiteral("/api/threads?") + query.toString(QUrl::FullyEncoded);
    ForumAccountService::instance()->getJson(
        path,
        [this, onOk](const QJsonObject &root) {
            QVector<ForumThreadInfo> threads;
            const QJsonArray array = root.value(QStringLiteral("threads")).toArray();
            for (const QJsonValue &value : array)
                threads.append(parseThreadInfo(value.toObject()));
            const ForumPagination pagination = parsePagination(root.value(QStringLiteral("pagination")).toObject());
            if (onOk)
                onOk(threads, pagination);
        },
        std::move(onFail));
}

void ForumContentService::searchThreads(
    const QString &queryText, int page,
    std::function<void(const QString &, const QVector<ForumThreadInfo> &,
                       const ForumPagination &)> onOk,
    FailHandler onFail)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), queryText);
    if (page > 1)
        query.addQueryItem(QStringLiteral("page"), QString::number(page));

    const QString path = QStringLiteral("/api/search?") + query.toString(QUrl::FullyEncoded);
    ForumAccountService::instance()->getJson(
        path,
        [this, onOk](const QJsonObject &root) {
            const QString echoed = root.value(QStringLiteral("query")).toString();
            QVector<ForumThreadInfo> results;
            const QJsonArray array = root.value(QStringLiteral("results")).toArray();
            for (const QJsonValue &value : array)
            {
                ForumThreadInfo info = parseThreadInfo(value.toObject());
                // 搜索结果没有作者等信息，但带命中片段
                info.snippet = value.toObject().value(QStringLiteral("snippet")).toString();
                results.append(info);
            }
            const ForumPagination pagination = parsePagination(root.value(QStringLiteral("pagination")).toObject());
            if (onOk)
                onOk(echoed, results, pagination);
        },
        std::move(onFail));
}

void ForumContentService::fetchThread(int threadId, int page,
                                      std::function<void(const ForumThreadDetail &)> onOk,
                                      FailHandler onFail)
{
    QString path = QStringLiteral("/api/threads/%1").arg(threadId);
    if (page > 1)
        path += QStringLiteral("?page=%1").arg(page);

    ForumAccountService::instance()->getJson(
        path,
        [this, onOk](const QJsonObject &root) {
            ForumThreadDetail detail;
            detail.info = parseThreadInfo(root.value(QStringLiteral("thread")).toObject());
            const QJsonArray array = root.value(QStringLiteral("posts")).toArray();
            for (const QJsonValue &value : array)
                detail.posts.append(parsePost(value.toObject()));
            detail.pagination = parsePagination(root.value(QStringLiteral("pagination")).toObject());
            if (onOk)
                onOk(detail);
        },
        std::move(onFail));
}

void ForumContentService::fetchUser(int userId,
                                    std::function<void(const ForumUserProfile &)> onOk,
                                    FailHandler onFail)
{
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/users/%1").arg(userId),
        [this, onOk](const QJsonObject &root) {
            ForumUserProfile profile;
            profile.user = parseAuthor(root.value(QStringLiteral("user")).toObject());

            const QJsonObject userObject = root.value(QStringLiteral("user")).toObject();
            profile.user.threadCount = userObject.value(QStringLiteral("thread_count")).toInt();
            profile.user.postCount = userObject.value(QStringLiteral("post_count")).toInt();
            profile.friendship = root.value(QStringLiteral("friendship")).toString();

            const QJsonArray threads = root.value(QStringLiteral("threads")).toArray();
            for (const QJsonValue &value : threads)
                profile.recentThreads.append(parseThreadInfo(value.toObject()));

            const QJsonArray replies = root.value(QStringLiteral("replies")).toArray();
            for (const QJsonValue &value : replies)
            {
                const QJsonObject item = value.toObject();
                ForumUserReplyItem reply;
                reply.threadId = item.value(QStringLiteral("thread_id")).toInt();
                reply.threadTitle = item.value(QStringLiteral("thread_title")).toString();
                reply.snippet = item.value(QStringLiteral("snippet")).toString();
                profile.recentReplies.append(reply);
            }
            if (onOk)
                onOk(profile);
        },
        std::move(onFail));
}

void ForumContentService::fetchUserThreads(
    int userId, int page,
    std::function<void(const QVector<ForumThreadInfo> &, const ForumPagination &)> onOk,
    FailHandler onFail)
{
    QString path = QStringLiteral("/api/users/%1/threads").arg(userId);
    if (page > 1)
        path += QStringLiteral("?page=%1").arg(page);

    ForumAccountService::instance()->getJson(
        path,
        [this, onOk](const QJsonObject &root) {
            QVector<ForumThreadInfo> threads;
            const QJsonArray array = root.value(QStringLiteral("threads")).toArray();
            for (const QJsonValue &value : array)
                threads.append(parseThreadInfo(value.toObject()));
            const ForumPagination pagination = parsePagination(root.value(QStringLiteral("pagination")).toObject());
            if (onOk)
                onOk(threads, pagination);
        },
        std::move(onFail));
}

void ForumContentService::fetchUserPosts(
    int userId, int page,
    std::function<void(const QVector<ForumUserReplyItem> &, const ForumPagination &)> onOk,
    FailHandler onFail)
{
    QString path = QStringLiteral("/api/users/%1/posts").arg(userId);
    if (page > 1)
        path += QStringLiteral("?page=%1").arg(page);

    ForumAccountService::instance()->getJson(
        path,
        [onOk](const QJsonObject &root) {
            QVector<ForumUserReplyItem> posts;
            const QJsonArray array = root.value(QStringLiteral("posts")).toArray();
            for (const QJsonValue &value : array)
            {
                const QJsonObject item = value.toObject();
                ForumUserReplyItem reply;
                reply.threadId = item.value(QStringLiteral("thread_id")).toInt();
                reply.threadTitle = item.value(QStringLiteral("thread_title")).toString();
                reply.snippet = item.value(QStringLiteral("snippet")).toString();
                posts.append(reply);
            }
            const ForumPagination pagination = parsePagination(root.value(QStringLiteral("pagination")).toObject());
            if (onOk)
                onOk(posts, pagination);
        },
        std::move(onFail));
}

// ────────────────────────────────────────────────────────────── 互动

void ForumContentService::createThread(const QString &boardSlug, const QString &title,
                                       const QString &content, const QStringList &tags,
                                       std::function<void(int threadId)> onOk, FailHandler onFail)
{
    QJsonObject body;
    if (!boardSlug.isEmpty())
        body.insert(QStringLiteral("board"), boardSlug);
    body.insert(QStringLiteral("title"), title);
    body.insert(QStringLiteral("content"), content);
    if (!tags.isEmpty())
    {
        QJsonArray tagArray;
        for (const QString &tag : tags)
            tagArray.append(tag);
        body.insert(QStringLiteral("tags"), tagArray);
    }

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/threads"), body,
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(root.value(QStringLiteral("id")).toInt());
        },
        std::move(onFail));
}

void ForumContentService::replyToThread(int threadId, const QString &content, int replyToPostId,
                                        std::function<void()> onOk, FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("content"), content);
    if (replyToPostId > 0)
        body.insert(QStringLiteral("reply_to"), replyToPostId);

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/threads/%1/posts").arg(threadId), body,
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumContentService::togglePostLike(int postId,
                                         std::function<void(bool liked, int likes)> onOk,
                                         FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/posts/%1/like").arg(postId), QJsonObject(),
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(root.value(QStringLiteral("liked")).toBool(),
                     root.value(QStringLiteral("likes")).toInt());
        },
        std::move(onFail));
}

void ForumContentService::toggleThreadBookmark(int threadId,
                                               std::function<void(bool bookmarked)> onOk,
                                               FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/threads/%1/bookmark").arg(threadId),
        QJsonObject(),
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(root.value(QStringLiteral("bookmarked")).toBool());
        },
        std::move(onFail));
}

// ── 资源收藏同步 ─────────────────────────────────────────

namespace {

/** ContentType 枚举值 → 接口的 content_type 关键字 */
QString contentFavoriteTypeKey(int contentType)
{
    switch (static_cast<ContentType>(contentType))
    {
    case ContentType::DataPack: return QStringLiteral("datapack");
    case ContentType::ResourcePack: return QStringLiteral("resourcepack");
    case ContentType::ShaderPack: return QStringLiteral("shaderpack");
    case ContentType::World: return QStringLiteral("world");
    case ContentType::Modpack: return QStringLiteral("modpack");
    case ContentType::Mod:
    default: return QStringLiteral("mod");
    }
}

int contentFavoriteTypeFromKey(const QString &key)
{
    if (key == QLatin1String("datapack")) return int(ContentType::DataPack);
    if (key == QLatin1String("resourcepack")) return int(ContentType::ResourcePack);
    if (key == QLatin1String("shaderpack")) return int(ContentType::ShaderPack);
    if (key == QLatin1String("world")) return int(ContentType::World);
    if (key == QLatin1String("modpack")) return int(ContentType::Modpack);
    return int(ContentType::Mod);
}

/** 收藏条目 → 服务端 meta 快照（字段与 FavoriteItem 对应，缺省字段不下发） */
QJsonObject contentFavoriteMeta(const FavoriteItem &item)
{
    QJsonObject meta;
    meta.insert(QStringLiteral("name"), item.name);
    if (!item.chineseName.isEmpty())
        meta.insert(QStringLiteral("chineseName"), item.chineseName);
    if (!item.englishName.isEmpty())
        meta.insert(QStringLiteral("englishName"), item.englishName);
    if (!item.description.isEmpty())
        meta.insert(QStringLiteral("description"), item.description);
    if (!item.iconUrl.isEmpty())
        meta.insert(QStringLiteral("iconUrl"), item.iconUrl);
    if (!item.pageUrl.isEmpty())
        meta.insert(QStringLiteral("pageUrl"), item.pageUrl);
    if (!item.author.isEmpty())
        meta.insert(QStringLiteral("author"), item.author);
    if (item.downloadCount > 0)
        meta.insert(QStringLiteral("downloadCount"), double(item.downloadCount));
    if (item.bedrockClassId > 0)
        meta.insert(QStringLiteral("bedrockClassId"), item.bedrockClassId);
    if (!item.bedrockCategory.isEmpty())
        meta.insert(QStringLiteral("bedrockCategory"), item.bedrockCategory);
    return meta;
}

/** 服务端单条资源收藏 → FavoriteItem（id 格式与 FavoritesManager::makeItemId 一致） */
FavoriteItem contentFavoriteFromJson(const QJsonObject &object)
{
    FavoriteItem item;
    item.source = object.value(QStringLiteral("source")).toString();
    item.remoteId = object.value(QStringLiteral("remote_id")).toString();
    item.id = item.source + QLatin1Char(':') + item.remoteId;
    item.contentType = contentFavoriteTypeFromKey(
        object.value(QStringLiteral("content_type")).toString());

    const QJsonObject meta = object.value(QStringLiteral("meta")).toObject();
    item.name = meta.value(QStringLiteral("name")).toString();
    item.chineseName = meta.value(QStringLiteral("chineseName")).toString();
    item.englishName = meta.value(QStringLiteral("englishName")).toString();
    item.description = meta.value(QStringLiteral("description")).toString();
    item.iconUrl = meta.value(QStringLiteral("iconUrl")).toString();
    item.pageUrl = meta.value(QStringLiteral("pageUrl")).toString();
    item.author = meta.value(QStringLiteral("author")).toString();
    item.downloadCount = qint64(meta.value(QStringLiteral("downloadCount")).toDouble());
    item.bedrockClassId = meta.value(QStringLiteral("bedrockClassId")).toInt();
    item.bedrockCategory = meta.value(QStringLiteral("bedrockCategory")).toString();
    item.addedAt = qint64(object.value(QStringLiteral("created_at")).toDouble()) * 1000;  // 秒 → 毫秒
    return item;
}

}  // namespace

void ForumContentService::addContentFavorite(const FavoriteItem &item,
                                             std::function<void()> onOk, FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("source"), item.source);
    body.insert(QStringLiteral("remote_id"), item.remoteId);
    body.insert(QStringLiteral("content_type"), contentFavoriteTypeKey(item.contentType));
    body.insert(QStringLiteral("meta"), contentFavoriteMeta(item));
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/me/content-favorites"), body,
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumContentService::removeContentFavorite(const QString &source, const QString &remoteId,
                                                std::function<void()> onOk, FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("DELETE"),
        QStringLiteral("/api/me/content-favorites/%1/%2").arg(source, remoteId),
        QJsonObject(),
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumContentService::fetchContentFavorites(
    int page, int perPage,
    std::function<void(const QVector<FavoriteItem> &, int total)> onOk, FailHandler onFail)
{
    const QString path =
        QStringLiteral("/api/me/content-favorites?page=%1&per_page=%2").arg(page).arg(perPage);
    ForumAccountService::instance()->getJson(
        path,
        [onOk](const QJsonObject &root) {
            QVector<FavoriteItem> items;
            const QJsonArray array = root.value(QStringLiteral("items")).toArray();
            items.reserve(array.size());
            for (const QJsonValue &value : array)
            {
                items.append(contentFavoriteFromJson(value.toObject()));
            }
            const int total = root.value(QStringLiteral("pagination"))
                                  .toObject()
                                  .value(QStringLiteral("total"))
                                  .toInt();
            if (onOk)
                onOk(items, total);
        },
        std::move(onFail));
}

void ForumContentService::pushContentFavoritesBulk(const QVector<FavoriteItem> &items,
                                                   std::function<void(int added)> onOk,
                                                   FailHandler onFail)
{
    QJsonArray array;
    for (const FavoriteItem &item : items)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("source"), item.source);
        entry.insert(QStringLiteral("remote_id"), item.remoteId);
        entry.insert(QStringLiteral("content_type"), contentFavoriteTypeKey(item.contentType));
        entry.insert(QStringLiteral("meta"), contentFavoriteMeta(item));
        array.append(entry);
    }
    QJsonObject body;
    body.insert(QStringLiteral("items"), array);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/me/content-favorites/bulk"), body,
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(root.value(QStringLiteral("added")).toInt());
        },
        std::move(onFail));
}

void ForumContentService::editPost(int postId, const QString &content,
                                   std::function<void()> onOk, FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("content"), content);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("PATCH"), QStringLiteral("/api/posts/%1").arg(postId), body,
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumContentService::deletePost(int postId, std::function<void()> onOk, FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("DELETE"), QStringLiteral("/api/posts/%1").arg(postId), QJsonObject(),
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumContentService::editThread(int threadId, const QString &title, const QStringList &tags,
                                     std::function<void()> onOk, FailHandler onFail)
{
    QJsonObject body;
    if (!title.isEmpty())
        body.insert(QStringLiteral("title"), title);
    if (!tags.isEmpty())
        body.insert(QStringLiteral("tags"), tags.join(QStringLiteral(",")));
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("PATCH"), QStringLiteral("/api/threads/%1").arg(threadId), body,
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumContentService::deleteThread(int threadId, std::function<void()> onOk, FailHandler onFail)
{
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("DELETE"), QStringLiteral("/api/threads/%1").arg(threadId), QJsonObject(),
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumContentService::reportPost(int postId, const QString &reason,
                                     std::function<void()> onOk, FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("post_id"), postId);
    body.insert(QStringLiteral("reason"), reason);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/reports"), body,
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumContentService::setThreadPinned(int threadId, bool pinned, std::function<void()> onOk,
                                          FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("pinned"), pinned);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("PATCH"), QStringLiteral("/api/admin/threads/%1").arg(threadId), body,
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

void ForumContentService::setThreadLocked(int threadId, bool locked, std::function<void()> onOk,
                                          FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("locked"), locked);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("PATCH"), QStringLiteral("/api/admin/threads/%1").arg(threadId), body,
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}

// ────────────────────────────────────────────────────────────── 通知

void ForumContentService::fetchNotifications(
    int page,
    std::function<void(const QVector<ForumNotificationItem> &, int unread,
                       const ForumPagination &)> onOk,
    FailHandler onFail)
{
    QString path = QStringLiteral("/api/notifications?per_page=20");
    if (page > 1)
        path += QStringLiteral("&page=%1").arg(page);

    ForumAccountService::instance()->getJson(
        path,
        [this, onOk](const QJsonObject &root) {
            QVector<ForumNotificationItem> items;
            const QJsonArray array = root.value(QStringLiteral("items")).toArray();
            for (const QJsonValue &value : array)
            {
                const QJsonObject object = value.toObject();
                ForumNotificationItem item;
                item.id = object.value(QStringLiteral("id")).toInt();
                item.type = object.value(QStringLiteral("type")).toString();
                item.targetKind = object.value(QStringLiteral("target_kind")).toString();
                item.threadId = object.value(QStringLiteral("thread_id")).toInt();
                item.postId = object.value(QStringLiteral("post_id")).toInt();
                item.threadTitle = object.value(QStringLiteral("thread_title")).toString();
                item.excerpt = object.value(QStringLiteral("excerpt")).toString();
                item.count = object.value(QStringLiteral("count")).toInt();
                if (item.count <= 0)
                    item.count = 1;
                item.unread = object.value(QStringLiteral("unread")).toBool();
                item.unreadCount = object.value(QStringLiteral("unread_count")).toInt();
                item.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());

                const QJsonArray actors = object.value(QStringLiteral("actors")).toArray();
                for (const QJsonValue &actor : actors)
                {
                    const QJsonObject actorObject = actor.toObject();
                    QString name = actorObject.value(QStringLiteral("display_name")).toString();
                    if (name.isEmpty())
                        name = actorObject.value(QStringLiteral("username")).toString();
                    if (!name.isEmpty())
                        item.actorNames.append(name);
                }

                const QJsonArray ids = object.value(QStringLiteral("ids")).toArray();
                for (const QJsonValue &id : ids)
                    item.ids.append(id.toInt());
                if (item.ids.isEmpty())
                    item.ids.append(item.id);

                items.append(item);
            }
            const int unread = root.value(QStringLiteral("unread")).toInt();
            const ForumPagination pagination = parsePagination(root.value(QStringLiteral("pagination")).toObject());
            if (onOk)
                onOk(items, unread, pagination);
        },
        std::move(onFail));
}

void ForumContentService::markNotificationsRead(const QVector<int> &ids,
                                                std::function<void(int unread)> onOk,
                                                FailHandler onFail)
{
    QJsonObject body;
    if (!ids.isEmpty())
    {
        QJsonArray idArray;
        for (int id : ids)
            idArray.append(id);
        body.insert(QStringLiteral("ids"), idArray);
    }
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/notifications/read"), body,
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(root.value(QStringLiteral("unread")).toInt());
        },
        std::move(onFail));
}

// ────────────────────────────────────────────────────────────── 解析

ForumBoard ForumContentService::parseBoard(const QJsonObject &object)
{
    ForumBoard board;
    board.id = object.value(QStringLiteral("id")).toInt();
    board.slug = object.value(QStringLiteral("slug")).toString();
    board.name = object.value(QStringLiteral("name")).toString();
    board.description = object.value(QStringLiteral("description")).toString();
    board.icon = object.value(QStringLiteral("icon")).toString();
    board.threads = object.value(QStringLiteral("thread_count")).toInt();
    board.posts = object.value(QStringLiteral("post_count")).toInt();
    return board;
}

ForumThreadInfo ForumContentService::parseThreadInfo(const QJsonObject &object)
{
    ForumThreadInfo info;
    info.id = object.value(QStringLiteral("id")).toInt();
    info.title = object.value(QStringLiteral("title")).toString();
    info.pinned = object.value(QStringLiteral("pinned")).toBool();
    info.locked = object.value(QStringLiteral("locked")).toBool();
    info.views = object.value(QStringLiteral("views")).toInt();
    info.replies = object.value(QStringLiteral("reply_count")).toInt();
    info.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
    info.lastPostAt = static_cast<qint64>(object.value(QStringLiteral("last_post_at")).toDouble());
    info.bookmarked = object.value(QStringLiteral("bookmarked")).toBool();
    info.snippet = object.value(QStringLiteral("snippet")).toString();
    info.tags = parseTags(object.value(QStringLiteral("tags")));

    const QJsonObject board = object.value(QStringLiteral("board")).toObject();
    info.boardSlug = board.value(QStringLiteral("slug")).toString();
    info.boardName = board.value(QStringLiteral("name")).toString();

    const QJsonObject author = object.value(QStringLiteral("author")).toObject();
    info.authorId = author.value(QStringLiteral("id")).toInt();
    info.authorName = authorName(author);
    info.authorAvatar = author.value(QStringLiteral("avatar")).toString();
    return info;
}

ForumPost ForumContentService::parsePost(const QJsonObject &object)
{
    ForumPost post;
    post.id = object.value(QStringLiteral("id")).toInt();
    post.floor = object.value(QStringLiteral("floor")).toInt();
    post.parentPostId = object.value(QStringLiteral("parent_post_id")).toInt();
    post.content = object.value(QStringLiteral("content")).toString();
    post.likes = object.value(QStringLiteral("likes")).toInt();
    post.liked = object.value(QStringLiteral("liked")).toBool();
    post.deleted = object.value(QStringLiteral("deleted")).toBool();
    post.editCount = object.value(QStringLiteral("edit_count")).toInt();
    post.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
    post.author = parseAuthor(object.value(QStringLiteral("author")).toObject());

    const QJsonArray children = object.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &value : children)
        post.children.append(parsePost(value.toObject()));
    return post;
}

ForumPagination ForumContentService::parsePagination(const QJsonObject &object)
{
    ForumPagination pagination;
    pagination.page = object.value(QStringLiteral("page")).toInt();
    pagination.perPage = object.value(QStringLiteral("per_page")).toInt();
    pagination.total = object.value(QStringLiteral("total")).toInt();
    pagination.pages = object.value(QStringLiteral("pages")).toInt();
    if (pagination.page <= 0)
        pagination.page = 1;
    if (pagination.pages <= 0)
        pagination.pages = 1;
    return pagination;
}

QString ForumContentService::authorName(const QJsonObject &author)
{
    QString name = author.value(QStringLiteral("display_name")).toString();
    if (name.isEmpty())
        name = author.value(QStringLiteral("username")).toString();
    return name;
}

ForumUser ForumContentService::parseAuthor(const QJsonObject &author)
{
    ForumUser user;
    user.id = author.value(QStringLiteral("id")).toInt();
    user.username = author.value(QStringLiteral("username")).toString();
    user.displayName = author.value(QStringLiteral("display_name")).toString();
    if (user.displayName.isEmpty())
        user.displayName = user.username;
    user.avatar = author.value(QStringLiteral("avatar")).toString();
    user.cover = author.value(QStringLiteral("cover")).toString();
    user.role = author.value(QStringLiteral("role")).toString();
    user.status = author.value(QStringLiteral("status")).toString();
    // 个人简介：用户主页按 Markdown 渲染，楼层作者里通常没有这个字段
    user.bio = author.value(QStringLiteral("bio")).toString();
    user.threadCount = author.value(QStringLiteral("thread_count")).toInt();
    user.postCount = author.value(QStringLiteral("post_count")).toInt();
    // 楼层里的作者用 joined_at，个人资料接口用 created_at
    qint64 joined = static_cast<qint64>(author.value(QStringLiteral("joined_at")).toDouble());
    if (joined <= 0)
        joined = static_cast<qint64>(author.value(QStringLiteral("created_at")).toDouble());
    user.createdAt = joined;
    return user;
}
