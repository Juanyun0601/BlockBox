/**
 * @file   LobbyShareLink.h
 * @brief  联机房间分享链接：生成网页邀请短链 + blockbox:// 一键加入深链
 * @author BlockBox Team
 * @date   2026-11-08
 *
 * 分享链接里不再塞房间内容：点「创建分享链接」时先把房间信息（标题 / 简介 / 邀请码 /
 * 连接地址 / 版本 / 加载器 / 模组 / 资源包 / 光影包）传到论坛，链接里只带一段很短的
 * 随机房间号：
 *   https://论坛地址/join.html?r=房间号   /   blockbox://join?r=房间号
 * 网页端 join.html 和客户端都按房间号回服务器取房间数据；房主结束联机后服务端会删掉
 * 这条记录，打开链接的一方就会看到「联机可能已经结束」。
 *
 * 老版本的自包含长链接（?d=压缩载荷）仍然能解开，已经发出去的邀请不会突然失效：
 *   载荷格式：紧凑 JSON（t=标题 ds=简介 c=核心 ic=邀请码 ad=地址 v=版本 ld=加载器 md=模组）
 *   → UTF-8 → qCompress（4 字节长度头 + zlib）→ base64url（无填充）。
 */

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "utils/ForumLobbyService.h"

namespace LobbyShareLink
{
/** 分享数据里各类清单最多带多少项（和论坛接口的限制对齐，防止超长被拒） */
constexpr int kMaxModsInPayload = 80;
constexpr int kMaxPacksInPayload = 40;
/** 简要介绍最多多少字（和论坛接口的限制对齐） */
constexpr int kMaxDescriptionInPayload = 200;

/** 房间 → 分享数据（紧凑 JSON：t / ds / c / ic / ad / v / ld / md / ma / rp / sp） */
QJsonObject roomToPayload(const ForumLobbyRoom &room);

/** 分享数据 → 房间（缺字段给默认值；联机已检查过有效期，isOnline() 为 true） */
ForumLobbyRoom payloadToRoom(const QJsonObject &payload);

/** 网页邀请短链（论坛地址 + join.html?r=房间号） */
QString webShareUrl(const QString &token);

/** 分享链接解析结果 */
struct LinkTarget
{
    QString token;         ///< 新式短链接里的房间号（房间信息要去服务器取）
    ForumLobbyRoom room;   ///< 老式链接里自带的房间信息
    bool legacy = false;   ///< 是否是老式自包含链接

    bool isValid() const { return !token.isEmpty() || legacy; }
};

/**
 * @brief 解析深链 / 网页链接 / 网页地址栏里的参数
 * @param url     blockbox://join?r=... 、https://论坛/join.html?r=... 、
 *                老式的 blockbox://join?d=... ，或直接给 ?d= 的载荷 / 参数串
 * @param target  输出：房间号（新式）或房间信息（老式）
 * @param error   输出：失败时的提示
 * @return 解析成功返回 true
 */
bool parseLink(const QString &url, LinkTarget *target, QString *error = nullptr);

/**
 * @brief 只解老式 ?d= 载荷（保留给老链接和外部调用）
 * @param room  输出：解出的房间（expiresIn 已填好，isOnline() 为 true）
 * @param error 输出：失败时的提示
 * @return 解码成功返回 true
 */
bool parsePayload(const QString &encoded, ForumLobbyRoom *room, QString *error = nullptr);
} // namespace LobbyShareLink
