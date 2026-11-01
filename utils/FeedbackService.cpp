/**
 * @file   FeedbackService.cpp
 * @brief  用户反馈服务实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "FeedbackService.h"

#include <QJsonArray>
#include <QJsonValue>

#include "AppVersion.h"

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

void FeedbackService::submitFeedback(const QString &content,
                                     std::function<void(int feedbackId)> onOk, FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("content"), content);
    body.insert(QStringLiteral("source"), QStringLiteral("launcher"));
    body.insert(QStringLiteral("app_version"), AppVersion::current());

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

FeedbackItem FeedbackService::parseFeedback(const QJsonObject &object)
{
    FeedbackItem item;
    item.id = object.value(QStringLiteral("id")).toInt();
    item.source = object.value(QStringLiteral("source")).toString();
    item.content = object.value(QStringLiteral("content")).toString();
    item.appVersion = object.value(QStringLiteral("app_version")).toString();
    item.status = object.value(QStringLiteral("status")).toString();
    item.reply = object.value(QStringLiteral("reply")).toString();
    item.handledAt = static_cast<qint64>(object.value(QStringLiteral("handled_at")).toDouble());
    item.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
    return item;
}
