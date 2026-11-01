/**
 * @file   ForumImageStore.h
 * @brief  论坛帖子图片的加载与内存缓存
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 帖子正文 / 头像里的站内图片（/uploads/...）统一从这里取：
 * 命中缓存立即回调，未命中去下载并去重，下载完成后所有等待者
 * 一起收到回调，同时广播 imageLoaded 让正文重排。
 */
#pragma once

#include <QCache>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

class QImage;
class QNetworkAccessManager;

class ForumImageStore : public QObject
{
    Q_OBJECT

public:
    static ForumImageStore *instance();

    /**
     * @brief 取一张图片（异步）
     * @param absoluteUrl 图片完整地址（相对路径请先用 ForumAccountService::absoluteUrl 拼好）
     * @param onDone 图片就绪回调；下载失败时回传空 QImage（不会反复重试）
     */
    void requestImage(const QString &absoluteUrl, std::function<void(const QImage &)> onDone);

    /** 丢弃全部缓存与失败记录（刷新帖子时调用，让图片有机会重新加载） */
    void clearCache();

    /** 帖子刷新时只清失败记录，缓存仍然有效 */
    void clearFailures();

signals:
    /** 任一图片下载完成（url 为完整地址） */
    void imageLoaded(const QString &url);

private:
    explicit ForumImageStore(QObject *parent = nullptr);

    QNetworkAccessManager *m_network = nullptr;
    QCache<QString, QImage> m_cache;
    QHash<QString, QVector<std::function<void(const QImage &)>>> m_pending;
    QSet<QString> m_failed;
};
