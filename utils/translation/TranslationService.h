/**
 * @file   TranslationService.h
 * @brief  多翻译源翻译服务 - 支持 AI/DeepL/百度/Google/自定义 API
 * @author BlockBox Team
 * @date   2026-08-24
 */

#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

/**
 * @brief 翻译源类型
 */
enum class TranslationSource {
    AI,           // 使用 AiService 调用已配置的 AI 模型
    DeepL,        // DeepL API
    Baidu,        // 百度翻译 API
    Google,       // Google Cloud Translation
    CustomAPI     // 用户自定义 OpenAI 兼容端点
};

/**
 * @brief 翻译配置
 */
struct TranslationConfig {
    TranslationSource source = TranslationSource::AI;
    // DeepL
    QString deeplApiKey;
    bool deeplFree = true;
    // 百度
    QString baiduAppId;
    QString baiduSecretKey;
    // Google
    QString googleApiKey;
    // 自定义 API
    QString customApiUrl;
    QString customApiKey;
    QString customModel;
    // 通用
    QString targetLanguage = "zh";
    QString sourceLanguage = "en";
};

/**
 * @brief 翻译服务类
 *
 * 支持多种翻译源，提供统一的翻译接口。
 * AI 翻译通过已有的 AiService 实现，其余通过各自的 REST API。
 */
class TranslationService : public QObject
{
    Q_OBJECT

public:
    explicit TranslationService(QObject *parent = nullptr);
    ~TranslationService();

    /**
     * @brief 翻译文本
     * @param text 待翻译文本（支持 HTML）
     * @param config 翻译配置
     */
    void translate(const QString &text, const TranslationConfig &config);

    /**
     * @brief 取消当前翻译请求
     */
    void cancelCurrentRequest();

    /**
     * @brief 获取翻译源的显示名称
     */
    static QString sourceDisplayName(TranslationSource source);

signals:
    /**
     * @brief 翻译完成
     * @param translatedText 翻译后的文本
     */
    void translationFinished(const QString &translatedText);

    /**
     * @brief 翻译错误
     * @param error 错误描述
     */
    void translationError(const QString &error);

private:
    void translateByAI(const QString &text, const TranslationConfig &config);
    void translateByDeepL(const QString &text, const TranslationConfig &config);
    void translateByBaidu(const QString &text, const TranslationConfig &config);
    void translateByGoogle(const QString &text, const TranslationConfig &config);
    void translateByCustomAPI(const QString &text, const TranslationConfig &config);

    QString buildAITranslationPrompt(const QString &text, const QString &targetLang);
    QString buildAITranslationUserMessage(const QString &text);

    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;
    bool m_translating;
};

