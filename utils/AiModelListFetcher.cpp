/**
 * @file   AiModelListFetcher.cpp
 * @brief  拉取服务商「可用模型列表」的实现
 * @author BlockBox Team
 * @date   2026-10-22
 */

#include "utils/AiModelListFetcher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {

/// 去掉末尾的斜杠
QString trimmedBase(QString url)
{
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

/// 末段是否为 v1 / v2 / v1beta / v3alpha 这类版本段
bool endsWithVersionSegment(const QString &url)
{
    const int slash = url.lastIndexOf(QLatin1Char('/'));
    if (slash < 0)
        return false;

    const QString segment = url.mid(slash + 1).toLower();
    if (segment.size() < 2 || segment.at(0) != QLatin1Char('v') || !segment.at(1).isDigit())
        return false;

    for (int i = 1; i < segment.size(); ++i) {
        if (!segment.at(i).isLetterOrNumber())
            return false;
    }
    return true;
}

/// 过滤向量、语音、绘图等与对话无关的模型，让列表保持精简
bool isLikelyChatModel(const QString &id)
{
    static const QStringList kNonChatKeywords = {
        QStringLiteral("embedding"),  QStringLiteral("embed-"), QStringLiteral("whisper"),
        QStringLiteral("tts"),        QStringLiteral("dall-e"), QStringLiteral("dalle"),
        QStringLiteral("moderation"), QStringLiteral("rerank"), QStringLiteral("davinci"),
        QStringLiteral("babbage"),    QStringLiteral("transcribe"), QStringLiteral("guard")
    };

    const QString lower = id.toLower();
    for (const QString &keyword : kNonChatKeywords) {
        if (lower.contains(keyword))
            return false;
    }
    return true;
}

} // namespace

AiModelListFetcher::AiModelListFetcher(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
    , m_reply(nullptr)
    , m_candidateIndex(0)
    , m_generation(0)
    , m_cancelled(false)
{
}

AiModelListFetcher::~AiModelListFetcher()
{
    m_generation++;
    abortReply();
}

QStringList AiModelListFetcher::endpointsFor(const QString &apiUrl)
{
    QString base = trimmedBase(apiUrl.trimmed());
    if (base.isEmpty())
        return QStringList();

    // 用户可能只填了域名：默认按 https 处理
    if (!base.contains(QStringLiteral("://")))
        base.prepend(QStringLiteral("https://"));

    // 去掉对话/补全端点后缀，回到接口根
    static const QStringList kEndpointSuffixes = {
        QStringLiteral("/chat/completions"), QStringLiteral("/completions"),
        QStringLiteral("/responses"),        QStringLiteral("/messages"),
        QStringLiteral("/embeddings")
    };
    for (const QString &suffix : kEndpointSuffixes) {
        if (base.endsWith(suffix, Qt::CaseInsensitive)) {
            base = trimmedBase(base.left(base.size() - suffix.size()));
            break;
        }
    }
    if (base.isEmpty())
        return QStringList();

    QStringList urls;
    auto addCandidate = [&urls](const QString &url) {
        if (!url.isEmpty() && !urls.contains(url))
            urls.append(url);
    };

    // Gemini 的 OpenAI 兼容端点是 .../v1beta/openai
    if (base.endsWith(QLatin1String("/openai"), Qt::CaseInsensitive)) {
        addCandidate(base + QStringLiteral("/models"));
        addCandidate(base + QStringLiteral("/v1/models"));
        return urls;
    }
    if (endsWithVersionSegment(base)) {
        // 已带版本段：/models 就挂在版本段后面，再拼 /v1 会得到 .../v1/v1/models
        addCandidate(base + QStringLiteral("/models"));
        return urls;
    }
    addCandidate(base + QStringLiteral("/v1/models"));
    addCandidate(base + QStringLiteral("/models"));
    return urls;
}

QStringList AiModelListFetcher::parseModelIds(const QByteArray &payload)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return QStringList();

    const QJsonObject root = doc.object();
    QJsonArray entries = root.value(QStringLiteral("data")).toArray();
    if (entries.isEmpty())
        entries = root.value(QStringLiteral("models")).toArray();   // Gemini 原生格式

    QStringList ids;
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        QString id = entry.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            id = entry.value(QStringLiteral("name")).toString();   // Gemini: models/gemini-2.5-pro
        if (id.isEmpty())
            id = entry.value(QStringLiteral("model")).toString();
        if (id.startsWith(QLatin1String("models/")))
            id = id.mid(7);
        id = id.trimmed();

        if (id.isEmpty() || ids.contains(id) || !isLikelyChatModel(id))
            continue;
        ids.append(id);
    }

    ids.sort(Qt::CaseInsensitive);
    return ids;
}

void AiModelListFetcher::fetch(const QString &apiUrl, const QString &apiKey)
{
    m_generation++;
    abortReply();
    m_cancelled = false;
    m_lastError.clear();
    m_candidateIndex = 0;
    m_apiKey = apiKey.trimmed();
    m_candidates = endpointsFor(apiUrl);

    if (m_candidates.isEmpty()) {
        AiModelListResult result;
        result.error = tr("请先填写有效的 API 地址");
        emit finished(result);
        return;
    }

    requestNext();
}

void AiModelListFetcher::cancel()
{
    m_cancelled = true;
    m_generation++;
    abortReply();
}

void AiModelListFetcher::abortReply()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void AiModelListFetcher::requestNext()
{
    if (m_cancelled)
        return;

    if (m_candidateIndex >= m_candidates.size()) {
        AiModelListResult result;
        result.error = m_lastError.isEmpty()
                           ? tr("无法获取模型列表，请检查 API 地址与网络连接")
                           : m_lastError;
        emit finished(result);
        return;
    }

    QString url = m_candidates.at(m_candidateIndex++);
    const QString displayUrl = url;   // 不含 ?key= 的地址，用于展示

    // Gemini 走 ?key= 鉴权，其余服务商走 Bearer 头
    const bool queryKeyAuth = url.contains(QLatin1String("googleapis.com")) && !m_apiKey.isEmpty();
    if (queryKeyAuth) {
        QUrl keyedUrl(url);
        QUrlQuery query(keyedUrl);
        query.addQueryItem(QStringLiteral("key"), m_apiKey);
        keyedUrl.setQuery(query);
        url = keyedUrl.toString(QUrl::FullyEncoded);
    }

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(10000);
    request.setRawHeader("Accept", "application/json");
    if (!m_apiKey.isEmpty() && !queryKeyAuth)
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());

    QNetworkReply *reply = m_manager->get(request);
    m_reply = reply;
    const quint64 generation = m_generation;

    connect(reply, &QNetworkReply::finished, this, [this, reply, displayUrl, generation]() {
        if (generation != m_generation) {   // 已被取消或被新请求取代
            reply->deleteLater();
            return;
        }
        if (m_reply == reply)
            m_reply = nullptr;

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString networkErrorText = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (networkError == QNetworkReply::NoError && status == 200) {
            AiModelListResult result;
            result.ids = parseModelIds(payload);
            if (result.ids.isEmpty()) {
                m_lastError = tr("服务商返回的模型列表为空");
            } else {
                result.ok = true;
                result.endpoint = displayUrl;
                emit finished(result);
                return;
            }
        } else if (status == 404 || status == 405) {
            // 该候选地址不存在，静默尝试下一个
        } else if (status == 401 || status == 403) {
            m_lastError = tr("鉴权失败（HTTP %1），请检查 API Key 是否正确").arg(status);
        } else if (status >= 500) {
            m_lastError = tr("服务商暂时不可用（HTTP %1）").arg(status);
        } else if (status > 0) {
            m_lastError = tr("请求被拒绝（HTTP %1）").arg(status);
        } else {
            m_lastError = tr("网络请求失败：%1").arg(networkErrorText);
        }

        requestNext();
    });
}
