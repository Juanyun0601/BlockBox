/**
 * @file   AssetUrls.h
 * @brief  按需下载的图片素材（首页轮播封面 / 旋转全景图）的取图地址
 *
 * 这两类图都不随安装包分发，第一次用到时才从网上取，统一放在官网：
 *   - 首页轮播封面：/assets/home-covers/preset_1.jpg … preset_8.jpg
 *   - 旋转全景图：  /assets/panorama/<主题>/panorama_0.jpg … panorama_5.jpg
 *
 * 正常优先走 CDN 边缘（cdn.mcblockbox.top，与论坛公开资源同一套 CDN）：
 * 国内访问更快，也不占源站出口带宽；CDN 没命中或临时不可用时，
 * 客户端会自动回退官网直连，保证下载仍然能成。
 */

#ifndef ASSETURLS_H
#define ASSETURLS_H

#include <QString>
#include <QStringList>

namespace AssetUrls {

/** CDN 边缘域名（换 CDN 域名时只改这里） */
inline QString cdnBase()
{
    return QStringLiteral("https://cdn.mcblockbox.top");
}

/** 官网源站域名（CDN 兜底，换官网域名时只改这里） */
inline QString originBase()
{
    return QStringLiteral("https://mcblockbox.top");
}

/**
 * @brief 某个素材目录的候选基址
 * @param path 站内路径，例如 "/assets/home-covers/"
 * @return CDN 优先、官网兜底，按尝试顺序排列
 */
inline QStringList baseCandidates(const QString &path)
{
    QStringList urls;
    urls.append(cdnBase() + path);
    urls.append(originBase() + path);
    return urls;
}

} // namespace AssetUrls

#endif // ASSETURLS_H
