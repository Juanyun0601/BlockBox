/**
 * @file   AiProviderHeaders.h
 * @brief  个别服务商要求的附加请求头（目前只有 OpenCode Zen / Go 的会话头）
 */

#pragma once

#include <QByteArray>
#include <QNetworkRequest>
#include <QString>
#include <QUuid>

namespace AiProviderHeaders {

/**
 * @brief 本次运行的稳定会话 ID
 *
 * 服务端按它把同一段对话路由到同一处并复用 prompt 缓存，所以必须稳定，
 * 不能每个请求都新生成一个（否则每次都是冷启动）。
 */
inline const QString &stableSessionId()
{
    static const QString id = QStringLiteral("ses_")
                              + QUuid::createUuid().toString(QUuid::WithoutBraces);
    return id;
}

/**
 * @brief 按接口地址决定要不要附加服务商专用请求头
 *
 * opencode.ai（OpenCode Zen / Go）要求每个推理请求都带 x-opencode-session，
 * 不带会直接回 400 MissingSessionID；官方同时要求客户端自报身份、不要用通用
 * HTTP 库的名字，所以顺带补一个 User-Agent。
 */
inline void apply(QNetworkRequest &request, const QString &apiUrl)
{
    if (!apiUrl.contains(QLatin1String("opencode.ai"), Qt::CaseInsensitive))
        return;

    request.setRawHeader("x-opencode-session", stableSessionId().toUtf8());
    request.setRawHeader("User-Agent", QByteArrayLiteral("BlockBox/1.0 (AI assistant)"));
}

} // namespace AiProviderHeaders
