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
     * @brief 翻译一批「列表条目」（名称 + 简介），固定走 AI
     *
     * 输入为按行排列的 `[#n#] 名称 ||| 简介`，模型需按同样格式返回；
     * 结果通过 listTranslationFinished 发出，由调用方解析。
     * 这类请求需要模型遵循格式约定，因此不使用 DeepL/百度/Google 等纯文本翻译源。
     * @param payload 待翻译文本
     * @param config 翻译配置（AI 模型/端点/密钥）
     */
    void translateList(const QString &payload, const TranslationConfig &config);

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
     * @brief 列表批量翻译完成（translateList 的结果）
     * @param translatedText 模型返回的 `[#n#] 名称 ||| 简介` 各行
     */
    void listTranslationFinished(const QString &translatedText);

    /**
     * @brief 翻译错误
     * @param error 错误描述
     */
    void translationError(const QString &error);

private:
    void translateByAI(const QString &text, const TranslationConfig &config, bool listMode);
    void translateByDeepL(const QString &text, const TranslationConfig &config);
    void translateByBaidu(const QString &text, const TranslationConfig &config);
    void translateByGoogle(const QString &text, const TranslationConfig &config);
    void translateByCustomAPI(const QString &text, const TranslationConfig &config);

    /// 一个可用的 AI 模型（端点/密钥已解析好）
    struct AiCandidate {
        QString modelId;
        QString apiUrl;
        QString apiKey;
    };

    /// 依次尝试候选模型：默认模型若返回的不是译文（例如被选成了安全审核模型），自动换下一个
    void sendAiCandidate();
    void handleAiReply();
    /// 响应内容像不像译文（列表模式看 [#n#] 标记，单段看是否真译成了中文）
    bool aiContentLooksTranslated(const QString &content) const;

    QString buildAITranslationPrompt(const QString &text, const QString &targetLang);
    QString buildAITranslationUserMessage(const QString &text);
    QString buildAIListTranslationPrompt();
    QString buildAIListTranslationUserMessage(const QString &payload);

    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;
    bool m_translating;

    // 当前这一轮 AI 翻译的候选模型与失败记录（仅在 AI 路径使用）
    QList<AiCandidate> m_aiCandidates;
    int m_aiCandidateIndex = 0;
    QStringList m_aiFailures;
    QString m_aiText;
    QString m_aiTargetLanguage;
    bool m_aiListMode = false;
};

