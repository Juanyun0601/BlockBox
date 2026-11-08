/**
 * @file   LobbyShareLink.h
 * @brief  联机房间分享链接：生成网页邀请链接 + blockbox:// 一键加入深链
 * @author BlockBox Team
 * @date   2026-11-08
 *
 * 分享链接把房间信息（标题 / 邀请码 / 连接地址 / 版本 / 加载器 / 模组）压缩编码进
 * URL 参数里，网页端 join.html 解开就能展示邀请码并提供「通过方块盒子一键加入」
 * 按钮；客户端解析 blockbox:// 参数后走大厅同款加入流程。
 *
 * 载荷格式：紧凑 JSON（t=标题 c=核心 ic=邀请码 ad=地址 v=版本 ld=加载器 md=模组）
 *   → UTF-8 → qCompress（4 字节长度头 + zlib）→ base64url（无填充）。
 * 网页端去掉 4 字节头后用 DecompressionStream('deflate') 即可解开。
 */

#pragma once

#include <QString>
#include <QStringList>

#include "utils/ForumLobbyService.h"

namespace LobbyShareLink
{
/** 载荷里最多带多少个模组名，防止链接过长 */
constexpr int kMaxModsInPayload = 120;

/** 载荷编码（qCompress + base64url，无填充） */
QString encodePayload(const ForumLobbyRoom &room);

/** 网页邀请链接（论坛地址 + join.html?d=载荷） */
QString webShareUrl(const ForumLobbyRoom &room);

/** blockbox:// 一键加入深链（join?d=载荷） */
QString deepLinkUrl(const ForumLobbyRoom &room);

/**
 * @brief 解出深链 / 网页链接 ?d= 参数里的房间
 * @param room  输出：解出的房间（expiresIn 已填好，isOnline() 为 true）
 * @param error 输出：失败时的提示
 * @return 解码成功返回 true
 */
bool parsePayload(const QString &encoded, ForumLobbyRoom *room, QString *error = nullptr);
}
