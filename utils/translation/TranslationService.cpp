/**
 * @file   TranslationService.cpp
 * @brief  多翻译源翻译服务实现
 * @author BlockBox Team
 * @date   2026-08-24
 */

#include "TranslationService.h"

#include "utils/AiProviderPresets.h"
#include "utils/AiEndpointUrl.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>

namespace {

/// 把网络错误整理成一句能看懂的话：HTTP 状态 + 服务端给出的原因（过长截断）
QString briefReplyError(QNetworkReply *reply, const QByteArray &body)
{
    QString message;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        message = obj.value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("message")).toString();
        if (message.isEmpty())
            message = obj.value(QStringLiteral("message")).toString();
    }
    if (message.isEmpty()) {
        message = QString::fromUtf8(body).simplified();
        if (message.size() > 160)
            message = message.left(160) + QStringLiteral("…");
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString head = status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
    if (status == 401 || status == 403)
        head += QStringLiteral("，API Key 无效或没有权限，请检查「设置 → AI 助手」");
    else if (status == 404)
        head += QStringLiteral("，接口地址不对，请检查服务商端点");
    else if (status == 429)
        head += QStringLiteral("，请求太频繁或额度用完了");
    return message.isEmpty() ? head : QStringLiteral("%1：%2").arg(head, message);
}

} // namespace

TranslationService::TranslationService(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
    , m_translating(false)
{
}

TranslationService::~TranslationService()
{
    cancelCurrentRequest();
}

void TranslationService::translate(const QString &text, const TranslationConfig &config)
{
    if (m_translating) {
        cancelCurrentRequest();
    }

    if (text.trimmed().isEmpty()) {
        emit translationError(tr("没有可翻译的文本"));
        return;
    }

    m_translating = true;

    switch (config.source) {
    case TranslationSource::AI:
        translateByAI(text, config, false);
        break;
    case TranslationSource::DeepL:
        translateByDeepL(text, config);
        break;
    case TranslationSource::Baidu:
        translateByBaidu(text, config);
        break;
    case TranslationSource::Google:
        translateByGoogle(text, config);
        break;
    case TranslationSource::CustomAPI:
        translateByCustomAPI(text, config);
        break;
    }
}

void TranslationService::translateList(const QString &payload, const TranslationConfig &config)
{
    if (m_translating)
        cancelCurrentRequest();

    if (payload.trimmed().isEmpty()) {
        emit translationError(tr("没有可翻译的文本"));
        return;
    }

    m_translating = true;
    // 列表批量翻译必须让模型按 [#n#] 格式逐条输出，只有 AI 来源能遵守这个约定
    translateByAI(payload, config, true);
}

void TranslationService::cancelCurrentRequest()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_translating = false;
}

QString TranslationService::sourceDisplayName(TranslationSource source)
{
    switch (source) {
    case TranslationSource::AI:       return QStringLiteral("AI 翻译");
    case TranslationSource::DeepL:    return QStringLiteral("DeepL");
    case TranslationSource::Baidu:    return QStringLiteral("百度翻译");
    case TranslationSource::Google:   return QStringLiteral("Google 翻译");
    case TranslationSource::CustomAPI: return QStringLiteral("自定义 API");
    }
    return {};
}

// ---------------------------------------------------------------------------
// AI 翻译（OpenAI 兼容 /chat/completions）
// ---------------------------------------------------------------------------

namespace {

/// 明显不是对话模型的 ID（安全审核、向量、重排、分类器…）：翻译交给它们必然拿不到译文
bool looksLikeNonChatModel(const QString &modelId)
{
    static const QRegularExpression re(
        QStringLiteral("(safety|moderation|guard|embedding|embed|rerank|classifier|classify)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(modelId).hasMatch();
}

/// 文本里是否已含汉字
bool containsHanText(const QString &text)
{
    for (const QChar &ch : text) {
        if (ch.script() == QChar::Script_Han)
            return true;
    }
    return false;
}

/// 文本里是否有拉丁字母（全是符号/数字的段不需要翻译）
bool containsLatinLetters(const QString &text)
{
    for (const QChar &ch : text) {
        const ushort u = ch.unicode();
        if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z'))
            return true;
    }
    return false;
}

/// 上一次真正翻译成功的模型：本轮优先用它，避免每次都先撞一鼻子灰
QString s_preferredTranslationModel;

} // namespace

void TranslationService::translateByAI(const QString &text, const TranslationConfig &config,
                                       bool listMode)
{
    m_aiCandidates.clear();
    m_aiCandidateIndex = 0;
    m_aiFailures.clear();
    m_aiText = text;
    m_aiTargetLanguage = config.targetLanguage;
    m_aiListMode = listMode;

    const bool customComplete = !config.customApiUrl.isEmpty()
        && !config.customApiKey.isEmpty() && !config.customModel.isEmpty();
    if (customComplete) {
        // 用户手填了自定义端点：照他填的来，不做替换
        m_aiCandidates.append({config.customModel, config.customApiUrl, config.customApiKey});
    } else {
        QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
        const QString defaultId = settings.value(QStringLiteral("ai/defaultModelId")).toString();
        const QList<AiConfiguredModel> configured = aiConfiguredModels();

        QSet<QString> seen;
        auto addCandidate = [this, &seen](const QString &id, const QString &url, const QString &key) {
            if (id.isEmpty() || seen.contains(id))
                return;
            if (url.isEmpty() && key.isEmpty())
                return;   // 没端点也没密钥，发出去只会 401
            seen.insert(id);
            m_aiCandidates.append({id, url, key});
        };

        auto findConfigured = [&configured](const QString &id) -> const AiConfiguredModel * {
            for (const AiConfiguredModel &m : configured) {
                if (m.id == id)
                    return &m;
            }
            return nullptr;
        };

        const bool defaultUsable = !defaultId.isEmpty() && !looksLikeNonChatModel(defaultId);

        // 1) 上次翻译成功的模型；其次是用户选的默认模型（只要它像个对话模型）
        if (!s_preferredTranslationModel.isEmpty()) {
            if (const AiConfiguredModel *m = findConfigured(s_preferredTranslationModel))
                addCandidate(m->id, m->apiUrl, m->apiKey);
        }
        if (defaultUsable) {
            if (const AiConfiguredModel *m = findConfigured(defaultId))
                addCandidate(m->id, m->apiUrl, m->apiKey);
        }
        // 2) 其它看起来能翻译的模型（跳过安全审核 / 向量 / 分类器）
        for (const AiConfiguredModel &m : configured) {
            if (!looksLikeNonChatModel(m.id))
                addCandidate(m.id, m.apiUrl, m.apiKey);
        }
        // 3) 兜底：默认模型不适合、又没有别的候选时，才把它放进来试一次
        if (!defaultUsable) {
            if (const AiConfiguredModel *m = findConfigured(defaultId))
                addCandidate(m->id, m->apiUrl, m->apiKey);
        }
        for (const AiConfiguredModel &m : configured)
            addCandidate(m.id, m.apiUrl, m.apiKey);
    }

    if (m_aiCandidates.isEmpty()) {
        m_translating = false;
        emit translationError(tr("还没有配置 AI 模型：请先到「设置 → AI 助手」里添加服务商密钥并选好默认模型，再回来翻译"));
        return;
    }

    sendAiCandidate();
}

void TranslationService::sendAiCandidate()
{
    if (m_aiCandidateIndex >= m_aiCandidates.size()) {
        m_translating = false;
        const QString detail = m_aiFailures.isEmpty()
            ? tr("AI 没有返回可用的译文")
            : m_aiFailures.first();
        m_aiCandidates.clear();
        emit translationError(tr("翻译失败：%1。请到「设置 → AI 助手」里换一个通用对话模型再试")
                                  .arg(detail));
        return;
    }

    const AiCandidate candidate = m_aiCandidates.at(m_aiCandidateIndex);

    QJsonArray messages;

    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = m_aiListMode ? buildAIListTranslationPrompt()
                                     : buildAITranslationPrompt(m_aiText, m_aiTargetLanguage);
    messages.append(sysMsg);

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = m_aiListMode ? buildAIListTranslationUserMessage(m_aiText)
                                      : buildAITranslationUserMessage(m_aiText);
    messages.append(userMsg);

    QJsonObject requestBody;
    requestBody["model"] = candidate.modelId;
    requestBody["messages"] = messages;
    requestBody["stream"] = false;

    const QUrl apiUrl(candidate.apiUrl.isEmpty()
        ? QStringLiteral("https://api.deepseek.com/v1/chat/completions")
        : AiEndpointUrl::buildChatCompletionsUrl(candidate.apiUrl));

    QNetworkRequest request(apiUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!candidate.apiKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + candidate.apiKey).toUtf8());
    }

    const QByteArray body = QJsonDocument(requestBody).toJson(QJsonDocument::Compact);
    m_currentReply = m_networkManager->post(request, body);
    connect(m_currentReply, &QNetworkReply::finished, this, [this]() { handleAiReply(); });
}

void TranslationService::handleAiReply()
{
    QNetworkReply *reply = m_currentReply;
    if (!reply)
        return;
    m_currentReply = nullptr;

    const QNetworkReply::NetworkError networkError = reply->error();
    if (networkError == QNetworkReply::OperationCanceledError) {
        reply->deleteLater();
        m_translating = false;
        m_aiCandidates.clear();
        return;
    }

    QString failure;
    QString content;

    if (networkError != QNetworkReply::NoError) {
        failure = briefReplyError(reply, reply->readAll());
    } else {
        const QByteArray data = reply->readAll();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            failure = tr("返回内容解析失败");
        } else {
            const QJsonObject obj = doc.object();
            const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty()) {
                const QString apiMessage = obj.value(QStringLiteral("error")).toObject()
                                              .value(QStringLiteral("message")).toString();
                failure = apiMessage.isEmpty() ? tr("返回空结果") : apiMessage;
            } else {
                content = choices.at(0).toObject()
                              .value(QStringLiteral("message")).toObject()
                              .value(QStringLiteral("content")).toString();
                // 去掉模型爱加的 markdown 代码块包裹
                if (content.startsWith(QStringLiteral("```html"))) {
                    content = content.mid(7);
                } else if (content.startsWith(QStringLiteral("```"))) {
                    content = content.mid(3);
                }
                if (content.endsWith(QStringLiteral("```")))
                    content.chop(3);
                content = content.trimmed();

                if (content.isEmpty())
                    failure = tr("返回空结果");
                else if (!aiContentLooksTranslated(content))
                    failure = m_aiListMode
                        ? tr("返回的内容不是翻译结果（这个模型可能不是对话模型）")
                        : tr("返回的内容不是译文（这个模型可能不是对话模型）");
            }
        }
    }
    reply->deleteLater();

    if (failure.isEmpty()) {
        s_preferredTranslationModel = m_aiCandidates.at(m_aiCandidateIndex).modelId;
        m_translating = false;
        m_aiCandidates.clear();
        m_aiFailures.clear();
        if (m_aiListMode)
            emit listTranslationFinished(content);
        else
            emit translationFinished(content);
        return;
    }

    // 这个模型不行：记下原因，换下一个候选
    m_aiFailures << QStringLiteral("%1：%2")
                        .arg(m_aiCandidates.at(m_aiCandidateIndex).modelId, failure);
    ++m_aiCandidateIndex;
    sendAiCandidate();
}

bool TranslationService::aiContentLooksTranslated(const QString &content) const
{
    if (m_aiListMode)
        return content.contains(QStringLiteral("[#"));

    // 译成中文时，结果里必须出现汉字；否则说明模型只是把原文/审核结论丢了回来
    const bool targetIsChinese =
        m_aiTargetLanguage.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);
    if (targetIsChinese && containsLatinLetters(m_aiText) && !containsHanText(content))
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// DeepL 翻译
// ---------------------------------------------------------------------------
void TranslationService::translateByDeepL(const QString &text, const TranslationConfig &config)
{
    if (config.deeplApiKey.isEmpty()) {
        m_translating = false;
        emit translationError(tr("请先配置 DeepL API Key"));
        return;
    }

    QUrl apiUrl(config.deeplFree
        ? "https://api-free.deepl.com/v2/translate"
        : "https://api.deepl.com/v2/translate");

    QUrlQuery params;
    params.addQueryItem("text", text);
    params.addQueryItem("source_lang", config.sourceLanguage.toUpper());
    params.addQueryItem("target_lang", config.targetLanguage.toUpper());
    params.addQueryItem("tag_handling", "html");

    QNetworkRequest request(apiUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Authorization",
        ("DeepL-Auth-Key " + config.deeplApiKey).toUtf8());

    QByteArray body = params.toString(QUrl::FullyEncoded).toUtf8();
    m_currentReply = m_networkManager->post(request, body);

    connect(m_currentReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_currentReply;
        if (!reply) return;

        m_currentReply = nullptr;
        m_translating = false;

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            QByteArray errData = reply->readAll();
            QJsonDocument errDoc = QJsonDocument::fromJson(errData);
            QString errMsg = errDoc.object().value("message").toString();
            if (errMsg.isEmpty()) errMsg = reply->errorString();
            emit translationError(tr("DeepL 翻译失败: %1").arg(errMsg));
            reply->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit translationError(tr("DeepL 响应解析失败"));
            return;
        }

        QJsonObject obj = doc.object();
        QJsonArray translations = obj.value("translations").toArray();
        if (translations.isEmpty()) {
            emit translationError(tr("DeepL 返回空结果"));
            return;
        }

        QString translatedText = translations[0].toObject()
            .value("text").toString();

        emit translationFinished(translatedText);
    });
}

// ---------------------------------------------------------------------------
// 百度翻译
// ---------------------------------------------------------------------------
void TranslationService::translateByBaidu(const QString &text, const TranslationConfig &config)
{
    if (config.baiduAppId.isEmpty() || config.baiduSecretKey.isEmpty()) {
        m_translating = false;
        emit translationError(tr("请先配置百度翻译 App ID 和密钥"));
        return;
    }

    // 生成签名
    QString salt = QString::number(QRandomGenerator::global()->bounded(100000));
    QString signStr = config.baiduAppId + text + salt + config.baiduSecretKey;
    QString sign = QCryptographicHash::hash(signStr.toUtf8(), QCryptographicHash::Md5).toHex();

    QUrl apiUrl("https://fanyi-api.baidu.com/api/trans/vip/translate");
    QUrlQuery params;
    params.addQueryItem("q", text);
    params.addQueryItem("from", config.sourceLanguage);
    params.addQueryItem("to", config.targetLanguage);
    params.addQueryItem("appid", config.baiduAppId);
    params.addQueryItem("salt", salt);
    params.addQueryItem("sign", sign);

    QNetworkRequest request(apiUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QByteArray body = params.toString(QUrl::FullyEncoded).toUtf8();
    m_currentReply = m_networkManager->post(request, body);

    connect(m_currentReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_currentReply;
        if (!reply) return;

        m_currentReply = nullptr;
        m_translating = false;

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            emit translationError(tr("百度翻译请求失败: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit translationError(tr("百度翻译响应解析失败"));
            return;
        }

        QJsonObject obj = doc.object();

        // 检查错误码
        int errorCode = obj.value("error_code").toInt(-1);
        if (errorCode != 0 && errorCode != -1) {
            QString errMsg = obj.value("error_msg").toString();
            emit translationError(tr("百度翻译错误 [%1]: %2").arg(errorCode).arg(errMsg));
            return;
        }

        QJsonArray transResult = obj.value("trans_result").toArray();
        if (transResult.isEmpty()) {
            emit translationError(tr("百度翻译返回空结果"));
            return;
        }

        // 合并多段翻译结果
        QString translatedText;
        for (const QJsonValue &v : transResult) {
            if (!translatedText.isEmpty()) translatedText += "\n";
            translatedText += v.toObject().value("dst").toString();
        }

        emit translationFinished(translatedText);
    });
}

// ---------------------------------------------------------------------------
// Google Cloud Translation
// ---------------------------------------------------------------------------
void TranslationService::translateByGoogle(const QString &text, const TranslationConfig &config)
{
    if (config.googleApiKey.isEmpty()) {
        m_translating = false;
        emit translationError(tr("请先配置 Google Cloud Translation API Key"));
        return;
    }

    QUrl apiUrl("https://translation.googleapis.com/language/translate/v2");

    QJsonObject requestBody;
    requestBody["q"] = text;
    requestBody["target"] = config.targetLanguage;
    requestBody["source"] = config.sourceLanguage;
    requestBody["format"] = "html";

    QUrlQuery params;
    params.addQueryItem("key", config.googleApiKey);

    QUrl finalUrl = apiUrl;
    finalUrl.setQuery(params);

    QNetworkRequest request(finalUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QByteArray body = QJsonDocument(requestBody).toJson(QJsonDocument::Compact);
    m_currentReply = m_networkManager->post(request, body);

    connect(m_currentReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_currentReply;
        if (!reply) return;

        m_currentReply = nullptr;
        m_translating = false;

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            QByteArray errData = reply->readAll();
            QJsonDocument errDoc = QJsonDocument::fromJson(errData);
            QString errMsg = errDoc.object()
                .value("error").toObject()
                .value("message").toString();
            if (errMsg.isEmpty()) errMsg = reply->errorString();
            emit translationError(tr("Google 翻译失败: %1").arg(errMsg));
            reply->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit translationError(tr("Google 翻译响应解析失败"));
            return;
        }

        QJsonObject dataObj = doc.object().value("data").toObject();
        QJsonArray translations = dataObj.value("translations").toArray();
        if (translations.isEmpty()) {
            emit translationError(tr("Google 翻译返回空结果"));
            return;
        }

        QString translatedText = translations[0].toObject()
            .value("translatedText").toString();

        emit translationFinished(translatedText);
    });
}

// ---------------------------------------------------------------------------
// 自定义 OpenAI 兼容 API
// ---------------------------------------------------------------------------
void TranslationService::translateByCustomAPI(const QString &text, const TranslationConfig &config)
{
    if (config.customApiUrl.isEmpty()) {
        m_translating = false;
        emit translationError(tr("请先配置自定义 API 端点"));
        return;
    }

    QJsonArray messages;

    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = buildAITranslationPrompt(text, config.targetLanguage);
    messages.append(sysMsg);

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = buildAITranslationUserMessage(text);
    messages.append(userMsg);

    QJsonObject requestBody;
    requestBody["model"] = config.customModel.isEmpty() ? "gpt-3.5-turbo" : config.customModel;
    requestBody["messages"] = messages;
    requestBody["stream"] = false;

    // 端点补全规则与 AI 助手一致：自带 /v1 的地址不会被拼成 /v1/v1
    const QUrl apiUrl(AiEndpointUrl::buildChatCompletionsUrl(config.customApiUrl));

    QNetworkRequest request(apiUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!config.customApiKey.isEmpty()) {
        request.setRawHeader("Authorization",
            ("Bearer " + config.customApiKey).toUtf8());
    }

    QByteArray body = QJsonDocument(requestBody).toJson(QJsonDocument::Compact);
    m_currentReply = m_networkManager->post(request, body);

    connect(m_currentReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_currentReply;
        if (!reply) return;

        m_currentReply = nullptr;
        m_translating = false;

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            const QByteArray body = reply->readAll();
            const QString detail = briefReplyError(reply, body);
            reply->deleteLater();
            emit translationError(tr("自定义 API 翻译失败（%1）").arg(detail));
            return;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit translationError(tr("自定义 API 响应解析失败"));
            return;
        }

        QJsonObject obj = doc.object();
        QJsonArray choices = obj.value("choices").toArray();
        if (choices.isEmpty()) {
            emit translationError(tr("自定义 API 返回空结果"));
            return;
        }

        QString content = choices[0].toObject()
            .value("message").toObject()
            .value("content").toString();

        // 去除 markdown 代码块包裹
        if (content.startsWith("```html")) {
            content = content.mid(7);
        } else if (content.startsWith("```")) {
            content = content.mid(3);
        }
        if (content.endsWith("```")) {
            content.chop(3);
        }
        content = content.trimmed();

        emit translationFinished(content);
    });
}

// ---------------------------------------------------------------------------
// AI 翻译提示词构建（从资源文件加载）
// ---------------------------------------------------------------------------
QString TranslationService::buildAITranslationPrompt(const QString &text, const QString &targetLang)
{
    Q_UNUSED(text);
    Q_UNUSED(targetLang);

    // 从资源文件加载翻译提示词
    QFile file(QStringLiteral(":/resources/ai_system_prompts.json"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            QString prompt = doc.object().value("translation").toString();
            if (!prompt.isEmpty()) {
                return prompt;
            }
        }
    }

    // 回退到默认提示词
    QString langName = (targetLang == "zh") ? "简体中文" : targetLang;
    return QStringLiteral(
        "你是 Minecraft 模组翻译专家。请将以下模组描述从英文翻译为%1。\n\n"
        "规则：\n"
        "1. 保留所有 HTML 标签结构，只翻译标签内的文本\n"
        "2. 保留所有 URL 链接不翻译\n"
        "3. Minecraft 术语使用官方译名（Overworld→主世界, Nether→下界, Ender Dragon→末影龙, Crafting Table→工作台）\n"
        "4. 模组名不翻译（如 Create, Tinkers' Construct, JEI, Mekanism 等保留英文）\n"
        "5. 翻译风格简洁自然，适合游戏社区阅读\n"
        "6. 不要添加任何解释、注释或前缀后缀，只输出翻译后的 HTML\n"
        "7. 如果原文包含格式代码（§x 或 &x），原样保留\n"
        "8. 保留原文的段落结构和换行"
    ).arg(langName);
}

QString TranslationService::buildAITranslationUserMessage(const QString &text)
{
    return QStringLiteral("请翻译以下模组描述：\n\n%1").arg(text);
}

// ---------------------------------------------------------------------------
// 列表批量翻译提示词（每条「[#序号#] 名称 ||| 简介」）
// ---------------------------------------------------------------------------
QString TranslationService::buildAIListTranslationPrompt()
{
    QFile file(QStringLiteral(":/resources/ai_system_prompts.json"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QString prompt =
                doc.object().value(QStringLiteral("translation_list")).toString();
            if (!prompt.isEmpty())
                return prompt;
        }
    }

    return QStringLiteral(
        "你是 Minecraft 资源本地化译者。下面每行是一条资源，格式为「[#序号#] 名称 ||| 简介」。\n"
        "请把每一条翻译成简体中文，并严格保持格式：\n"
        "1. 每行以原样的 [#序号#] 开头，序号、条目数量与顺序都不能改；\n"
        "2. 名称与简介之间必须保留 ||| 分隔符；\n"
        "3. 名称为空的就保持空，简介为空的也保持空；\n"
        "4. 名称译成符合中文习惯的标题（例如 Complementary Shaders → 互补光影，"
        "Fresh Animations → 鲜活动画）；\n"
        "5. 简介译得简洁自然，不要补充原文没有的信息；\n"
        "6. 只输出翻译后的各行，不要字段名、不要解释、不要 Markdown、不要代码块。");
}

QString TranslationService::buildAIListTranslationUserMessage(const QString &payload)
{
    return QStringLiteral("请逐行翻译下面的资源列表：\n\n%1").arg(payload);
}
