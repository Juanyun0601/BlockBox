/**
 * @file   ContentFavoriteSync.cpp
 * @brief  在线资源收藏 ↔ 论坛账号 云端同步器实现
 * @author BlockBox Team
 * @date   2026-11-01
 */
#include "ContentFavoriteSync.h"

#include <QDebug>
#include <QDateTime>
#include <QMap>

#include "ForumAccountService.h"
#include "ForumContentService.h"

namespace {

/** 连续增删的合并窗口 */
constexpr int kFlushDelayMs = 800;
/** 同一账号两次对账的最小间隔 */
constexpr qint64 kReconcileIntervalMs = 30 * 60 * 1000;
/** 云端列表单页条数（服务端 per_page 上限） */
constexpr int kReconcilePerPage = 60;

/** itemId（"source:remoteId"）拆成 source / remoteId，不合法返回 false */
bool splitItemId(const QString &itemId, QString &source, QString &remoteId)
{
    const int sep = itemId.indexOf(QLatin1Char(':'));
    if (sep <= 0 || sep + 1 >= itemId.size())
        return false;
    source = itemId.left(sep);
    remoteId = itemId.mid(sep + 1);
    return true;
}

}  // namespace

ContentFavoriteSync *ContentFavoriteSync::s_instance = nullptr;

ContentFavoriteSync *ContentFavoriteSync::instance()
{
    if (!s_instance)
        s_instance = new ContentFavoriteSync();
    return s_instance;
}

ContentFavoriteSync::ContentFavoriteSync(QObject *parent)
    : QObject(parent)
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(kFlushDelayMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &ContentFavoriteSync::flushPending);

    FavoritesManager *favorites = FavoritesManager::instance();
    connect(favorites, &FavoritesManager::favoriteAdded,
            this, &ContentFavoriteSync::onFavoriteAdded);
    connect(favorites, &FavoritesManager::favoriteRemoved,
            this, &ContentFavoriteSync::onFavoriteRemoved);

    ForumAccountService *account = ForumAccountService::instance();
    m_loggedIn = account->isLoggedIn();
    // 登录成功 / 会话恢复 / 资料刷新都会发 userChanged，借助它触发对账
    connect(account, &ForumAccountService::userChanged,
            this, &ContentFavoriteSync::onUserChanged);
    connect(account, &ForumAccountService::sessionExpired, this, [this]() {
        m_loggedIn = false;
        m_reconciledUser.clear();
    });
}

void ContentFavoriteSync::onFavoriteAdded(const FavoriteItem &item)
{
    if (!m_loggedIn || !ForumAccountService::instance()->isLoggedIn())
        return;
    m_pendingRemoves.remove(item.id);
    m_pendingAdds.insert(item.id, item);
    m_flushTimer.start();
}

void ContentFavoriteSync::onFavoriteRemoved(const QString &itemId)
{
    if (!m_loggedIn || !ForumAccountService::instance()->isLoggedIn())
        return;
    m_pendingAdds.remove(itemId);
    m_pendingRemoves.insert(itemId);
    m_flushTimer.start();
}

void ContentFavoriteSync::flushPending()
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        m_pendingAdds.clear();
        m_pendingRemoves.clear();
        return;
    }

    const QMap<QString, FavoriteItem> adds = m_pendingAdds;
    const QSet<QString> removes = m_pendingRemoves;
    m_pendingAdds.clear();
    m_pendingRemoves.clear();

    ForumContentService *content = ForumContentService::instance();
    for (const FavoriteItem &item : adds)
    {
        content->addContentFavorite(item, nullptr, [](const QString &message, int) {
            qWarning() << "[ContentFavoriteSync] 收藏上报失败:" << message;
        });
    }
    for (const QString &itemId : removes)
    {
        QString source, remoteId;
        if (!splitItemId(itemId, source, remoteId))
            continue;
        content->removeContentFavorite(source, remoteId, nullptr,
                                       [](const QString &message, int) {
                                           qWarning() << "[ContentFavoriteSync] 取消收藏上报失败:" << message;
                                       });
    }
}

void ContentFavoriteSync::onUserChanged()
{
    ForumAccountService *account = ForumAccountService::instance();
    m_loggedIn = account->isLoggedIn();
    if (!m_loggedIn)
    {
        m_reconciledUser.clear();
        return;
    }

    const QString userName = account->user().username;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (userName == m_reconciledUser && nowMs - m_lastReconcileMs < kReconcileIntervalMs)
        return;
    reconcile();
}

void ContentFavoriteSync::reconcile()
{
    if (m_reconciling || !ForumAccountService::instance()->isLoggedIn())
        return;
    m_reconciling = true;
    m_lastReconcileMs = QDateTime::currentMSecsSinceEpoch();
    m_reconciledUser = ForumAccountService::instance()->user().username;
    m_reconcileCloudIds.clear();
    fetchReconcilePage(1);
}

void ContentFavoriteSync::fetchReconcilePage(int page)
{
    ForumContentService::instance()->fetchContentFavorites(
        page, kReconcilePerPage,
        [this, page](const QVector<FavoriteItem> &items, int total) {
            for (const FavoriteItem &item : items)
                m_reconcileCloudIds.insert(item.id);
            const int fetched = (page - 1) * kReconcilePerPage + items.size();
            if (fetched < total && !items.isEmpty())
            {
                fetchReconcilePage(page + 1);
                return;
            }
            finishReconcile();
        },
        [this](const QString &message, int) {
            qWarning() << "[ContentFavoriteSync] 对账拉取云端收藏失败:" << message;
            m_reconciling = false;
        });
}

void ContentFavoriteSync::finishReconcile()
{
    QVector<FavoriteItem> missing;
    {
        QSet<QString> localIds;
        const QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
        for (const FavoriteFolder &folder : folders)
        {
            for (const FavoriteItem &item : folder.items)
            {
                if (localIds.contains(item.id))
                    continue;
                localIds.insert(item.id);
                if (!m_reconcileCloudIds.contains(item.id))
                    missing.append(item);
            }
        }
    }
    m_reconcileCloudIds.clear();

    if (!missing.isEmpty())
    {
        ForumContentService::instance()->pushContentFavoritesBulk(
            missing,
            [](int added) {
                qDebug() << "[ContentFavoriteSync] 对账补传完成，新增" << added << "条";
            },
            [](const QString &message, int) {
                qWarning() << "[ContentFavoriteSync] 对账补传失败:" << message;
            });
    }
    m_reconciling = false;
}
