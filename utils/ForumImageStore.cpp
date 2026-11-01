/**
 * @file   ForumImageStore.cpp
 * @brief  论坛帖子图片加载实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumImageStore.h"

#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
// 缓存按字节计费，64 MB 足够同时翻看几页图文帖
constexpr int kCacheCostLimit = 64 * 1024 * 1024;
constexpr int kRequestTimeoutMs = 20000;
} // namespace

ForumImageStore *ForumImageStore::instance()
{
    static ForumImageStore *s_instance = new ForumImageStore();
    return s_instance;
}

ForumImageStore::ForumImageStore(QObject *parent)
    : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_cache.setMaxCost(kCacheCostLimit);
}

void ForumImageStore::requestImage(const QString &absoluteUrl,
                                   std::function<void(const QImage &)> onDone)
{
    if (absoluteUrl.isEmpty() || !onDone)
        return;

    if (QImage *cached = m_cache.object(absoluteUrl))
    {
        onDone(*cached);
        return;
    }
    if (m_failed.contains(absoluteUrl))
    {
        onDone(QImage());
        return;
    }

    // 同一张图只发一个请求，回调排队等下载完成
    m_pending[absoluteUrl].append(std::move(onDone));
    if (m_pending[absoluteUrl].size() > 1)
        return;

    QNetworkRequest request{QUrl(absoluteUrl)};
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, absoluteUrl]() {
        reply->deleteLater();

        QImage image;
        if (reply->error() == QNetworkReply::NoError && reply->size() <= 16 * 1024 * 1024)
            image.loadFromData(reply->readAll());

        const QVector<std::function<void(const QImage &)>> waiters = m_pending.take(absoluteUrl);
        if (image.isNull())
        {
            // 失败也记录下来，避免正文重排时反复触发下载
            m_failed.insert(absoluteUrl);
        }
        else
        {
            m_cache.insert(absoluteUrl, new QImage(image), image.sizeInBytes());
        }

        for (const auto &onDone : waiters)
            onDone(image);
        emit imageLoaded(absoluteUrl);
    });
}

void ForumImageStore::clearCache()
{
    m_cache.clear();
    m_failed.clear();
}

void ForumImageStore::clearFailures()
{
    m_failed.clear();
}
