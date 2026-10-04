/**
 * @file   TranslationService.cpp
 * @brief  多翻译源翻译服务实现
 * @author BlockBox Team
 * @date   2026-08-24
 */

#include "TranslationService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>

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
        translateByAI(text, config);
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
// AI 翻译（通过 SSE 流式调用，同步等待结果）
// ---------------------------------------------------------------------------
void TranslationService::translateByAI(const QString &text, const TranslationConfig &config)
{
    // 构建翻译请求体
    QJsonArray messages;

    // System prompt
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = buildAITranslationPrompt(text, config.targetLanguage);
    messages.append(sysMsg);

    // User message
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = buildAITranslationUserMessage(text);
    messages.append(userMsg);

    // 从设置中读取 AI 模型配置（使用用户默认的 AI 模型）
    // 这里直接调用 AiService 的同步接口
    // 由于 AiService 是异步 SSE，我们用一个 QEventLoop 等待结果
    // 但更好的方式是让 ContentDetailPage 自己调用 AiService
    // 这里提供一个简化实现：通过网络直接调用

    // 使用自定义 API 端点来调用 AI
    QJsonObject requestBody;
    requestBody["model"] = config.customModel.isEmpty() ? "deepseek-chat" : config.customModel;
    requestBody["messages"] = messages;
    requestBody["stream"] = false;

    QUrl apiUrl(config.customApiUrl.isEmpty()
        ? "https://api.deepseek.com/v1/chat/completions"
        : config.customApiUrl + "/v1/chat/completions");

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
            emit translationError(tr("AI 翻译请求失败: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit translationError(tr("AI 翻译响应解析失败"));
            return;
        }

        QJsonObject obj = doc.object();
        QJsonArray choices = obj.value("choices").toArray();
        if (choices.isEmpty()) {
            emit translationError(tr("AI 翻译返回空结果"));
            return;
        }

        QString content = choices[0].toObject()
            .value("message").toObject()
            .value("content").toString();

        // 去除可能的 markdown 代码块包裹
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

    QUrl apiUrl(config.customApiUrl);
    if (!config.customApiUrl.endsWith("/v1/chat/completions")) {
        apiUrl = QUrl(config.customApiUrl + "/v1/chat/completions");
    }

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
            emit translationError(tr("自定义 API 翻译失败: %1").arg(reply->errorString()));
            reply->deleteLater();
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
