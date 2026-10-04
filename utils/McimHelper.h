/**
 * @file   McimHelper.h
 * @brief  MCIM CDN 加速工具类
 * @author BlockBox Team
 */
#ifndef MCIMHELPER_H
#define MCIMHELPER_H

#include <QString>
#include "../utils/SettingsManager.h"

/**
 * @brief MCIM (MCMod.cn Mirror) CDN 加速工具
 *
 * 提供图片 URL 重写功能，将图片请求通过 MCIM CDN 代理加速。
 *
 * URL 重写规则（来自 MCIM 文档）：
 * - Modrinth: cdn.modrinth.com -> mod.mcimirror.top
 * - CurseForge: edge.forgecdn.net -> mod.mcimirror.top
 *   注意: mediafilez.forgecdn.net 不代理
 */
class McimHelper
{
public:
    /**
     * @brief 将图片 URL 通过 MCIM CDN 代理重写
     * @param url 原始图片 URL
     * @return 加速后的 URL（如果 MCIM 未启用则返回原 URL）
     */
    static QString rewriteImageUrl(const QString &url)
    {
        QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
        if (!mcimVal.isValid() || !mcimVal.toBool())
            return url;

        // Modrinth CDN 加速
        if (url.startsWith("https://cdn.modrinth.com/")) {
            QString result = url;
            return result.replace("https://cdn.modrinth.com/", "https://mod.mcimirror.top/");
        }
        // CurseForge edge.forgecdn.net 加速（mediafilez.forgecdn.net 不代理）
        if (url.startsWith("https://edge.forgecdn.net/")) {
            QString result = url;
            return result.replace("https://edge.forgecdn.net/", "https://mod.mcimirror.top/");
        }
        return url;
    }
};

#endif // MCIMHELPER_H
