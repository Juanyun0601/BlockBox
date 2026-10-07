#ifndef CONTENTLISTCACHE_H
#define CONTENTLISTCACHE_H

#include <QString>
#include "../mod/ModData.h"
#include "ContentData.h"

/**
 * @brief 资源列表首屏磁盘缓存
 *
 * 将「资源>模组/整合包/资源包」等列表页的第一页搜索结果缓存到本地磁盘，
 * 下次进入页面时可直接加载缓存，无需等待网络请求。
 */
class ContentListCache
{
public:
    static ContentListCache *instance();

    /** 生成缓存键（含来源、筛选条件、排序、页码等，保证不同请求不互相覆盖） */
    static QString makeKey(ContentType contentType, int source,
                           const QString &gameVersion, const QString &loader,
                           const QString &sortField, const QString &query, int page);

    /** 尝试加载缓存的第一页列表；命中且未过期返回 true */
    bool load(const QString &key, ModSearchResult &result) const;

    /** 将第一页列表写入磁盘缓存 */
    void save(const QString &key, const ModSearchResult &result) const;

    void clearAll();

private:
    ContentListCache();
    ~ContentListCache();

    QString cacheDir() const;
    QString filePathForKey(const QString &key) const;

    static ContentListCache *m_instance;

    /** 缓存有效期：默认 6 小时 */
    static constexpr qint64 CACHE_TTL_MS = 6LL * 60 * 60 * 1000;
};

#endif // CONTENTLISTCACHE_H
