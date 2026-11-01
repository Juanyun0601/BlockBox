/**
 * @file   ContentFavoriteSync.h
 * @brief  在线资源收藏 ↔ 论坛账号 云端同步器 - 单例
 * @author BlockBox Team
 * @date   2026-11-01
 */
#ifndef CONTENTFAVORITESYNC_H
#define CONTENTFAVORITESYNC_H

#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include "FavoritesManager.h"

/**
 * @brief 把本地资源收藏同步到论坛账号（单例，静默失败）
 *
 * - 监听 FavoritesManager 的收藏增删信号，已登录论坛账号时上报云端
 *   （短时间内连续增删会合并成一次请求）
 * - 登录 / 会话恢复时做一次对账：本地已有而云端缺失的收藏批量补传
 *   （同一用户 30 分钟内最多对账一次）
 * - 未登录时行为不变，一切只留本地；网络失败仅记日志，不打扰用户
 */
class ContentFavoriteSync : public QObject
{
    Q_OBJECT

public:
    static ContentFavoriteSync *instance();

private slots:
    void onFavoriteAdded(const FavoriteItem &item);
    void onFavoriteRemoved(const QString &itemId);
    void onUserChanged();

private:
    explicit ContentFavoriteSync(QObject *parent = nullptr);

    void flushPending();               // 发出合并后的增删请求
    void reconcile();                  // 对账入口（带节流与防重入）
    void fetchReconcilePage(int page); // 分页拉取云端收藏列表
    void finishReconcile();            // 差集计算 + 批量补传

    bool m_loggedIn = false;
    bool m_reconciling = false;
    qint64 m_lastReconcileMs = 0;
    QString m_reconciledUser;              // 上次对账的账号名
    QSet<QString> m_reconcileCloudIds;     // 对账期间累积的云端收藏 id

    QMap<QString, FavoriteItem> m_pendingAdds;   // 待上报新增：itemId → 条目
    QSet<QString> m_pendingRemoves;              // 待上报移除
    QTimer m_flushTimer;                         // 连点合并窗口

    static ContentFavoriteSync *s_instance;
};

#endif // CONTENTFAVORITESYNC_H
