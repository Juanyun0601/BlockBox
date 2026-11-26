/**
 * @file   FeedbackService.cpp
 * @brief  用户反馈服务实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "FeedbackService.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "AppVersion.h"
#include "utils/trust/LogSanitizer.h"

namespace {

ForumPagination parsePagination(const QJsonObject &object)
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
    if (pagination.perPage <= 0)
        pagination.perPage = 20;
    return pagination;
}

} // namespace

FeedbackService *FeedbackService::instance()
{
    static FeedbackService service;
    return &service;
}

FeedbackService::FeedbackService(QObject *parent)
    : QObject(parent)
{
}

void FeedbackService::submitFeedback(const FeedbackDraft &draft,
                                     std::function<void(int feedbackId)> onOk, FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("type"), draft.type.isEmpty() ? QStringLiteral("bug") : draft.type);
    if (!draft.appVersion.isEmpty())
        body.insert(QStringLiteral("app_version"), draft.appVersion);
    if (!draft.platform.isEmpty())
        body.insert(QStringLiteral("platform"), draft.platform);
    if (!draft.osVersion.isEmpty())
        body.insert(QStringLiteral("os_version"), draft.osVersion);
    body.insert(QStringLiteral("description"), draft.description);
    if (!draft.steps.isEmpty())
        body.insert(QStringLiteral("steps"), draft.steps);
    if (!draft.expected.isEmpty())
        body.insert(QStringLiteral("expected"), draft.expected);
    if (!draft.actual.isEmpty())
        body.insert(QStringLiteral("actual"), draft.actual);
    // 日志里常带用户名、邮箱、IP 和令牌：发给服务器前先脱敏
    // （只改上传的副本，用户输入框里的内容保持原样）
    if (!draft.logs.isEmpty())
        body.insert(QStringLiteral("logs"), LogSanitizer::sanitize(draft.logs));
    if (!draft.screenshots.isEmpty())
        body.insert(QStringLiteral("screenshots"), draft.screenshots);
    if (!draft.scene.isEmpty())
        body.insert(QStringLiteral("scene"), draft.scene);
    body.insert(QStringLiteral("source"), QStringLiteral("launcher"));

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/feedback"), body,
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(root.value(QStringLiteral("id")).toInt());
        },
        std::move(onFail));
}

void FeedbackService::uploadLogFile(const QString &filePath,
                                    std::function<void(const QString &url, const QString &name, qint64 size)> onOk,
                                    FailHandler onFail)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (onFail)
            onFail(tr("打不开日志文件，请稍后再试"), 0);
        return;
    }
    // 上传前脱敏：日志里的用户名、邮箱、IP、令牌等隐私片段一律换成占位符。
    // 只处理内存里的这份数据，用户本地的日志文件保持原样（不改用户的文件）。
    const QByteArray data = LogSanitizer::sanitizeBytes(file.readAll());
    file.close();
    const QString name = QFileInfo(filePath).fileName();

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    // 与聊天附件同一套上传方式：原始字节流，文件名与版本号放自定义头里
    QNetworkRequest request{
        QUrl(ForumAccountService::instance()->absoluteUrl(QStringLiteral("/api/upload/log")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/octet-stream"));
    request.setRawHeader("X-File-Name", QString::fromUtf8(QUrl::toPercentEncoding(name)).toUtf8());
    request.setRawHeader("X-App-Version",
                         QString::fromUtf8(QUrl::toPercentEncoding(AppVersion::current())).toUtf8());
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
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, reply, onOk = std::move(onOk), onFail = std::move(onFail)]() mutable {
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
                                 message = tr("日志上传失败，请稍后再试");
                             if (onFail)
                                 onFail(message, reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
                             return;
                         }
                         const QJsonObject object = QJsonDocument::fromJson(raw).object();
                         const QString url = object.value(QStringLiteral("url")).toString();
                         if (url.isEmpty())
                         {
                             if (onFail)
                                 onFail(tr("日志上传失败，请稍后再试"), 0);
                             return;
                         }
                         if (onOk)
                             onOk(url, object.value(QStringLiteral("name")).toString(),
                                  static_cast<qint64>(object.value(QStringLiteral("size")).toDouble()));
                     });
}

void FeedbackService::fetchMyFeedbacks(
    int page,
    std::function<void(const QVector<FeedbackItem> &, const ForumPagination &)> onOk,
    FailHandler onFail)
{
    QString path = QStringLiteral("/api/me/feedbacks");
    if (page > 1)
        path += QStringLiteral("?page=%1").arg(page);

    ForumAccountService::instance()->getJson(
        path,
        [onOk](const QJsonObject &root) {
            QVector<FeedbackItem> items;
            const QJsonArray array = root.value(QStringLiteral("feedbacks")).toArray();
            for (const QJsonValue &value : array)
                items.append(FeedbackService::parseFeedback(value.toObject()));
            if (onOk)
                onOk(items, parsePagination(root.value(QStringLiteral("pagination")).toObject()));
        },
        std::move(onFail));
}

void FeedbackService::fetchPublicFeedbacks(
    const QString &status, int page,
    std::function<void(const QVector<PublicFeedbackItem> &, const FeedbackCounts &,
                       const ForumPagination &)> onOk,
    FailHandler onFail)
{
    const QString safeStatus =
        (status == QStringLiteral("open") || status == QStringLiteral("resolved")
         || status == QStringLiteral("ignored"))
            ? status
            : QStringLiteral("all");
    QString path = QStringLiteral("/api/feedbacks?status=%1").arg(safeStatus);
    if (page > 1)
        path += QStringLiteral("&page=%1").arg(page);

    ForumAccountService::instance()->getJson(
        path,
        [onOk](const QJsonObject &root) {
            QVector<PublicFeedbackItem> items;
            const QJsonArray array = root.value(QStringLiteral("feedbacks")).toArray();
            for (const QJsonValue &value : array)
            {
                const QJsonObject object = value.toObject();
                PublicFeedbackItem item;
                item.id = object.value(QStringLiteral("id")).toInt();
                item.type = object.value(QStringLiteral("type")).toString();
                item.status = object.value(QStringLiteral("status")).toString();
                item.title = object.value(QStringLiteral("title")).toString();
                item.content = object.value(QStringLiteral("content")).toString();
                item.reply = object.value(QStringLiteral("reply")).toString();
                item.handledAt = static_cast<qint64>(object.value(QStringLiteral("handled_at")).toDouble());
                item.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
                const QJsonObject user = object.value(QStringLiteral("user")).toObject();
                item.authorId = user.value(QStringLiteral("id")).toInt();
                item.authorName = user.value(QStringLiteral("display_name")).toString();
                if (item.authorName.isEmpty())
                    item.authorName = user.value(QStringLiteral("username")).toString();
                items.append(item);
            }
            const QJsonObject countsObject = root.value(QStringLiteral("counts")).toObject();
            FeedbackCounts counts;
            counts.open = countsObject.value(QStringLiteral("open")).toInt();
            counts.resolved = countsObject.value(QStringLiteral("resolved")).toInt();
            counts.ignored = countsObject.value(QStringLiteral("ignored")).toInt();
            if (onOk)
                onOk(items, counts, parsePagination(root.value(QStringLiteral("pagination")).toObject()));
        },
        std::move(onFail));
}

FeedbackItem FeedbackService::parseFeedback(const QJsonObject &object)
{
    FeedbackItem item;
    item.id = object.value(QStringLiteral("id")).toInt();
    item.type = object.value(QStringLiteral("type")).toString();
    item.source = object.value(QStringLiteral("source")).toString();
    item.content = object.value(QStringLiteral("content")).toString();
    item.appVersion = object.value(QStringLiteral("app_version")).toString();
    item.status = object.value(QStringLiteral("status")).toString();
    item.reply = object.value(QStringLiteral("reply")).toString();
    item.handledAt = static_cast<qint64>(object.value(QStringLiteral("handled_at")).toDouble());
    item.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
    return item;
}

// ────────────────────────────────────────────── 反馈详情页评论

FeedbackComment FeedbackService::parseComment(const QJsonObject &object)
{
    FeedbackComment comment;
    comment.id = object.value(QStringLiteral("id")).toInt();
    comment.content = object.value(QStringLiteral("content")).toString();
    comment.createdAt = qint64(object.value(QStringLiteral("created_at")).toDouble());
    comment.isAuthor = object.value(QStringLiteral("is_author")).toBool();
    comment.isAdmin = object.value(QStringLiteral("is_admin")).toBool();
    comment.mine = object.value(QStringLiteral("mine")).toBool();

    const QJsonObject user = object.value(QStringLiteral("user")).toObject();
    comment.userId = user.value(QStringLiteral("id")).toInt();
    comment.username = user.value(QStringLiteral("username")).toString();
    comment.displayName = user.value(QStringLiteral("display_name")).toString();
    comment.avatar = user.value(QStringLiteral("avatar")).toString();
    if (comment.displayName.isEmpty())
        comment.displayName = comment.username;
    return comment;
}

void FeedbackService::fetchComments(int feedbackId, CommentListHandler onOk, FailHandler onFail)
{
    if (feedbackId <= 0)
    {
        if (onOk)
            onOk(QVector<FeedbackComment>(), 0);
        return;
    }

    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/feedbacks/%1/comments").arg(feedbackId),
        [onOk](const QJsonObject &root) {
            QVector<FeedbackComment> comments;
            const QJsonArray array = root.value(QStringLiteral("comments")).toArray();
            comments.reserve(array.size());
            for (const QJsonValue &value : array)
                comments.append(parseComment(value.toObject()));
            if (onOk)
                onOk(comments, root.value(QStringLiteral("total")).toInt(comments.size()));
        },
        std::move(onFail));
}

void FeedbackService::postComment(int feedbackId, const QString &content, CommentHandler onOk,
                                  FailHandler onFail)
{
    if (feedbackId <= 0)
    {
        if (onFail)
            onFail(tr("这条反馈没有评论区"), 404);
        return;
    }
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        if (onFail)
            onFail(tr("登录论坛账号后才能评论"), 401);
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("content"), content);
    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/feedbacks/%1/comments").arg(feedbackId), body,
        [onOk](const QJsonObject &root) {
            if (onOk)
                onOk(parseComment(root.value(QStringLiteral("comment")).toObject()));
        },
        std::move(onFail));
}

void FeedbackService::deleteComment(int commentId, OkHandler onOk, FailHandler onFail)
{
    if (commentId <= 0)
    {
        if (onOk)
            onOk();
        return;
    }

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("DELETE"), QStringLiteral("/api/feedbacks/comments/%1").arg(commentId),
        QJsonObject(),
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}
