/**
 * @file   McimHelper.h
 * @brief  MCIM CDN 加速工具类
 * @author BlockBox Team
 */
#ifndef MCIMHELPER_H
#define MCIMHELPER_H

#include <QByteArray>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPixmapCache>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <functional>
#include "SettingsManager.h"

/**
 * @brief MCIM (MCMod.cn Mirror) CDN 加速工具
 *
 * 提供图片 URL 重写功能，将图片请求通过 MCIM CDN 代理加速。
 *
 * URL 重写规则（来自 MCIM 文档）：
 * - Modrinth: cdn.modrinth.com -> mod.mcimirror.top
 * - CurseForge: edge.forgecdn.net -> mod.mcimirror.top
 *   注意: mediafilez.forgecdn.net 不代理
 *
 * 注意：镜像的图片路径偶尔会「HTTP 200 但返回网页」，此时图片解不出来，
 * 所以图片统一用 loadImage() 加载——它会自动换原始地址重试，并记住镜像不可用。
 */
class McimHelper
{
public:
    /**
     * @brief 将图片 URL 通过 MCIM CDN 代理重写
     * @param url 原始图片 URL
     * @return 加速后的 URL（如果 MCIM 未启用或镜像图片已确认不可用则返回原 URL）
     */
    static QString rewriteImageUrl(const QString &url)
    {
        QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
        if (!mcimVal.isValid() || !mcimVal.toBool())
            return url;
        // 本次会话已经确认镜像图片不可用，别再让每张图白等一次
        if (imageHostBroken())
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

    /** 镜像图片域名拿到的是网页而不是图片时调用：本次会话后续图片直接走原始地址 */
    static void reportImageHostBroken() { imageHostBrokenRef() = true; }
    static bool imageHostBroken() { return imageHostBrokenRef(); }

    /**
     * @brief Modrinth API 基地址（跟随 use_mcim 设置）
     *
     * 未设置时默认开启镜像，与资源下载页等其余页面的行为保持一致。
     */
    static QString modrinthApiBaseUrl()
    {
        QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
        const bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
        return useMcim ? QStringLiteral("https://mod.mcimirror.top/modrinth")
                       : QStringLiteral("https://api.modrinth.com");
    }

    /**
     * @brief 将内容文件下载 URL 通过 MCIM 镜像改写（未开启镜像时原样返回）
     *
     * 与 rewriteImageUrl 同一套主机映射，但不含图片专用的「镜像不可用」标记；
     * 调用方应在下载失败时回退原始地址。
     */
    static QString rewriteFileUrl(const QString &url)
    {
        QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
        if (!mcimVal.isValid() || !mcimVal.toBool())
            return url;
        if (url.startsWith(QStringLiteral("https://cdn.modrinth.com/")))
            return QString(url).replace(QStringLiteral("https://cdn.modrinth.com/"),
                                        QStringLiteral("https://mod.mcimirror.top/"));
        if (url.startsWith(QStringLiteral("https://edge.forgecdn.net/")))
            return QString(url).replace(QStringLiteral("https://edge.forgecdn.net/"),
                                        QStringLiteral("https://mod.mcimirror.top/"));
        return url;
    }

    /**
     * @brief 下载网络图片并显示到 label（自动处理镜像回退）
     *
     * 先按镜像地址请求；若返回的不是图片（镜像把图片请求返回成网页）或请求失败，
     * 自动改用原始地址重试一次，并把镜像标记为不可用。
     *
     * @param nam        网络管理器（页面里复用的那个即可）
     * @param label      显示图片的 QLabel（内部用 QPointer 判活，可安全提前销毁）
     * @param url        原始图片 URL（未经镜像重写）
     * @param targetSize 缩放后的目标尺寸（保持比例）
     * @param cacheKey   QPixmapCache 键，空字符串表示不缓存
     * @param onFailed   最终仍拿不到图片时调用，通常用来画占位块
     */
    static void loadImage(QNetworkAccessManager *nam, QLabel *label, const QString &url,
                          const QSize &targetSize, const QString &cacheKey = QString(),
                          const std::function<void(QLabel *)> &onFailed = std::function<void(QLabel *)>())
    {
        if (!label)
            return;
        if (url.isEmpty() || !nam) {
            if (onFailed)
                onFailed(label);
            return;
        }
        const QPointer<QLabel> guard(label);
        fetch(nam, guard, rewriteImageUrl(url), url, targetSize, cacheKey, onFailed);
    }

private:
    static bool &imageHostBrokenRef()
    {
        static bool broken = false;
        return broken;
    }

    static void fetch(QNetworkAccessManager *nam, const QPointer<QLabel> &label,
                      const QString &requestUrl, const QString &originalUrl,
                      const QSize &targetSize, const QString &cacheKey,
                      const std::function<void(QLabel *)> &onFailed)
    {
        const QUrl target(requestUrl);
        if (!target.isValid()) {
            if (!label.isNull() && onFailed)
                onFailed(label);
            return;
        }

        QNetworkRequest request(target);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        QNetworkReply *reply = nam->get(request);
        // 以 nam 作为上下文：label 被销毁时仍要能走完回退逻辑
        QObject::connect(reply, &QNetworkReply::finished, nam,
                         [=]() {
            reply->deleteLater();
            QPixmap pixmap;
            const bool fetched = (reply->error() == QNetworkReply::NoError)
                                 && pixmap.loadFromData(reply->readAll());
            if (fetched) {
                const QPixmap scaled = pixmap.scaled(targetSize, Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation);
                if (!cacheKey.isEmpty())
                    QPixmapCache::insert(cacheKey, scaled);
                if (!label.isNull())
                    label->setPixmap(scaled);
                return;
            }

            // 镜像把图片返回成了网页（HTTP 200 + text/html）：记住并改用原始地址再试一次
            if (!originalUrl.isEmpty() && requestUrl != originalUrl) {
                reportImageHostBroken();
                fetch(nam, label, rewriteImageUrl(originalUrl), QString(), targetSize, cacheKey, onFailed);
                return;
            }

            if (!label.isNull() && onFailed)
                onFailed(label);
        });
    }
};

#endif // MCIMHELPER_H
