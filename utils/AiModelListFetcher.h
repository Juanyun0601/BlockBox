/**
 * @file   AiModelListFetcher.h
 * @brief  拉取服务商「可用模型列表」的通用工具（OpenAI 兼容 GET /models）
 * @author BlockBox Team
 * @date   2026-10-22
 *
 * 用途：AI 模型设置时一键获取服务商当前可用的模型 ID，免去手工输入。内部处理：
 *  - 由对话端点推导 /models 地址（自动识别 /v1、/v1beta、/v2、/openai 等版本段，按优先级依次尝试）
 *  - 鉴权（googleapis.com 走 ?key=，其余走 Authorization: Bearer）
 *  - 解析 data[]（OpenAI 兼容）与 models[]（Gemini 原生），过滤向量/语音/绘图等非对话模型
 */

#ifndef AIMODELLISTFETCHER_H
#define AIMODELLISTFETCHER_H

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

/// 一次「获取可用模型列表」的结果
struct AiModelListResult
{
    bool ok = false;        ///< 是否成功取得模型列表
    QStringList ids;        ///< 模型 ID（已去重、去掉 models/ 前缀、按名称排序）
    QString endpoint;       ///< 真正返回数据的地址
    QString error;          ///< 失败原因（面向用户的中文描述）
};

/**
 * @brief 服务商模型列表拉取器
 *
 * 每次 fetch() 只会在结束时发出一次 finished()（成功或失败）。
 */
class AiModelListFetcher : public QObject
{
    Q_OBJECT

public:
    explicit AiModelListFetcher(QObject *parent = nullptr);
    ~AiModelListFetcher() override;

    /// 拉取 apiUrl 对应服务商的模型列表；apiKey 允许为空（部分本地端点无需鉴权）
    void fetch(const QString &apiUrl, const QString &apiKey);

    /// 放弃当前请求（此后不再发出 finished）
    void cancel();

    bool isRunning() const { return m_reply != nullptr; }

    /// 由对话端点推导候选的 /models 地址（按尝试优先级排序，已去重）
    static QStringList endpointsFor(const QString &apiUrl);

    /// 从 /models 响应体解析模型 ID 列表（无法解析时返回空）
    static QStringList parseModelIds(const QByteArray &payload);

signals:
    void finished(const AiModelListResult &result);

private:
    void requestNext();
    void abortReply();

    QNetworkAccessManager *m_manager;
    QNetworkReply *m_reply;
    QStringList m_candidates;
    int m_candidateIndex;
    quint64 m_generation;   ///< 请求代号，用于丢弃过期回调
    QString m_apiKey;
    QString m_lastError;    ///< 最近一次值得告知用户的失败原因
    bool m_cancelled;
};

Q_DECLARE_METATYPE(AiModelListResult)

#endif // AIMODELLISTFETCHER_H
