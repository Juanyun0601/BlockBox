/**
 * @file   ContentTranslationService.cpp
 * @brief  社区内容翻译服务实现
 * @author BlockBox Team
 * @date   2026-11-21
 */

#include "ContentTranslationService.h"

#include <QJsonDocument>
#include <QUrl>

ContentTranslationService *ContentTranslationService::instance()
{
    static ContentTranslationService inst;
    return &inst;
}

ContentTranslation ContentTranslationService::parseTranslation(const QJsonObject &object)
{
    ContentTranslation t;
    t.source = object.value(QStringLiteral("source")).toString();
    t.projectId = object.value(QStringLiteral("project_id")).toString();
    t.lang = object.value(QStringLiteral("lang")).toString();
    t.summary = object.value(QStringLiteral("summary")).toString();
    t.detailHtml = object.value(QStringLiteral("detail_html")).toString();
    t.authorName = object.value(QStringLiteral("author_name")).toString();
    t.status = object.value(QStringLiteral("status")).toString();
    t.updatedAt = static_cast<qint64>(object.value(QStringLiteral("updated_at")).toDouble());
    return t;
}

void ContentTranslationService::fetchTranslation(const QString &source, const QString &projectId,
                                                 const QString &lang,
                                                 std::function<void(const ContentTranslation &)> onOk,
                                                 FailHandler onFail)
{
    if (source.isEmpty() || projectId.isEmpty()) {
        if (onOk)
            onOk(ContentTranslation{});
        return;
    }

    const QString path = QStringLiteral("/api/content-translations?source=%1&project_id=%2&lang=%3")
                             .arg(QString::fromUtf8(QUrl::toPercentEncoding(source)),
                                  QString::fromUtf8(QUrl::toPercentEncoding(projectId)),
                                  QString::fromUtf8(QUrl::toPercentEncoding(lang)));

    ForumAccountService::instance()->getJson(
        path,
        [onOk](const QJsonObject &root) {
            const QJsonValue value = root.value(QStringLiteral("translation"));
            if (!value.isObject()) {
                // 服务端还没有这个接口 / 暂无翻译：都按「没有翻译」处理
                if (onOk)
                    onOk(ContentTranslation{});
                return;
            }
            if (onOk)
                onOk(parseTranslation(value.toObject()));
        },
        std::move(onFail));
}

void ContentTranslationService::fetchMyTranslation(const QString &source, const QString &projectId,
                                                   const QString &lang,
                                                   std::function<void(const ContentTranslation &)> onOk,
                                                   FailHandler onFail)
{
    if (source.isEmpty() || projectId.isEmpty()) {
        if (onOk)
            onOk(ContentTranslation{});
        return;
    }

    const QString path = QStringLiteral("/api/content-translations/mine?source=%1&project_id=%2&lang=%3")
                             .arg(QString::fromUtf8(QUrl::toPercentEncoding(source)),
                                  QString::fromUtf8(QUrl::toPercentEncoding(projectId)),
                                  QString::fromUtf8(QUrl::toPercentEncoding(lang)));

    ForumAccountService::instance()->getJson(
        path,
        [onOk](const QJsonObject &root) {
            const QJsonValue value = root.value(QStringLiteral("translation"));
            if (!value.isObject()) {
                if (onOk)
                    onOk(ContentTranslation{});
                return;
            }
            if (onOk)
                onOk(parseTranslation(value.toObject()));
        },
        std::move(onFail));
}

void ContentTranslationService::submitTranslation(const ContentTranslation &draft,
                                                  const QString &sourceSummary,
                                                  const QString &sourceDetailHtml,
                                                  const QString &projectName,
                                                  std::function<void()> onOk, FailHandler onFail)
{
    QJsonObject body;
    body.insert(QStringLiteral("source"), draft.source);
    body.insert(QStringLiteral("project_id"), draft.projectId);
    if (!projectName.isEmpty())
        body.insert(QStringLiteral("project_name"), projectName);
    body.insert(QStringLiteral("lang"), draft.lang);
    body.insert(QStringLiteral("summary"), draft.summary);
    body.insert(QStringLiteral("detail_html"), draft.detailHtml);
    if (!sourceSummary.isEmpty())
        body.insert(QStringLiteral("source_summary"), sourceSummary);
    if (!sourceDetailHtml.isEmpty())
        body.insert(QStringLiteral("source_detail_html"), sourceDetailHtml);

    ForumAccountService::instance()->sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/content-translations"), body,
        [onOk](const QJsonObject &) {
            if (onOk)
                onOk();
        },
        std::move(onFail));
}
