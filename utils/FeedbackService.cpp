/**
 * @file   FeedbackService.cpp
 * @brief  用户反馈服务实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "FeedbackService.h"

#include <QJsonArray>
#include <QJsonValue>

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
    if (!draft.logs.isEmpty())
        body.insert(QStringLiteral("logs"), draft.logs);
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
