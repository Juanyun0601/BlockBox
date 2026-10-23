/**
 * @file   AiEndpointUrl.h
 * @brief  AI 服务端点拼装（各翻译 / 对话入口共用同一套规则）
 */

#pragma once

#include <QRegularExpression>
#include <QString>

namespace AiEndpointUrl {

/**
 * @brief 把服务商基础端点补全为 /chat/completions 完整地址
 *
 * 已带完整路径的原样返回；以 /v1 /v1beta /v2 /v3 等版本段结尾的直接拼
 * /chat/completions；其余（如 https://api.deepseek.com）补 /v1/chat/completions。
 * 这样既能吃下预设里自带 /v1 的端点，也不会把 /v1 拼成 /v1/v1。
 */
inline QString buildChatCompletionsUrl(QString fullUrl)
{
    while (fullUrl.endsWith('/'))
        fullUrl.chop(1);
    if (fullUrl.endsWith(QLatin1String("/chat/completions"), Qt::CaseInsensitive))
        return fullUrl;
    static const QRegularExpression versionSuffix(
        QStringLiteral("/v\\d+(alpha|beta)?/?$"), QRegularExpression::CaseInsensitiveOption);
    if (versionSuffix.match(fullUrl).hasMatch())
        return fullUrl + QStringLiteral("/chat/completions");
    return fullUrl + QStringLiteral("/v1/chat/completions");
}

} // namespace AiEndpointUrl
