/**
 * @file   LogSanitizer.h
 * @brief  日志脱敏：上传 / 提交反馈前隐藏用户名、邮箱、IP、令牌等隐私信息
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 反馈日志会原样发到论坛服务器，公开反馈列表里其他人也能看到，
 * 所以上传前必须把隐私片段换掉。这里只做「文本级替换」，不改变日志结构：
 * 时间、日志级别、堆栈行号、路径层级都保留，方便开发者看懂问题。
 *
 * 覆盖范围（见 trust-spec 2.4）：
 *   - Windows 用户目录 C:\Users\<名>、C:\Documents and Settings\<名>
 *     （正斜杠 / 反斜杠、中文用户名都要能处理，只换掉用户名那一段）
 *   - Linux / macOS 用户目录 /home/<名>、/Users/<名>
 *   - 邮箱、11 位手机号、IPv4 / IPv6 地址
 *   - accessToken / access_token / client_token / clientToken / refreshToken /
 *     sessionId / Authorization: Bearer 后面的值，以及 JWT
 *
 * 全部是纯函数，可在任意线程调用（不依赖界面、不弹窗）。
 */
#pragma once

#include <QByteArray>
#include <QString>

namespace LogSanitizer {

/**
 * @brief 脱敏一段文本
 * @param text 原始文本
 * @return 脱敏后的文本；没有命中任何规则时原样返回
 */
QString sanitize(const QString &text);

/**
 * @brief 脱敏一段原始字节（按 UTF-8 处理）
 *
 * 日志文件直接读成 QByteArray 后用它，避免调用方自己处理编码。
 * 内容没有变化时返回的字节与原数据完全一致。
 */
QByteArray sanitizeBytes(const QByteArray &data);

/**
 * @brief 就地把一个日志文件脱敏
 * @param filePath 文件路径
 * @return 文件可读且写回成功（内容没有变化也算成功）
 *
 * 注意：上传场景应优先用 sanitizeBytes()：原文件保持不动，
 * 只把脱敏后的内容发出去，用户手里那份日志还是完整的。
 */
bool sanitizeFile(const QString &filePath);

} // namespace LogSanitizer
