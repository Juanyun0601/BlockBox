#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSet>

#include "NotificationCard.h"

struct NotificationRecord {
    QString text;
    NotificationCard::Type type;
    QDateTime timestamp;
};

class NotificationManager : public QObject
{
    Q_OBJECT

public:
    static NotificationManager *instance();

    static void showSuccess(QWidget *parent, const QString &text, int duration = 5000);
    static void showInfo(QWidget *parent, const QString &text, int duration = 5000);
    static void showError(QWidget *parent, const QString &text, int duration = 7000);
    /// 同 showError，但卡片上多一个「网络自检」按钮（下载/加载因网络失败时用）
    static void showErrorWithNetworkCheck(QWidget *parent, const QString &text, int duration = 7000);

    /**
     * @brief 拼装「标题 + 详情」两段式文案
     *
     * 译文里漏写 %2 占位符时（历史上确实漏过），改为换行直接接上详情：
     * 详情不会再被吞掉，也不会触发 QString::arg 的「Argument missing」告警。
     *
     * @param titleTemplate 含 %1（必要时 %2）的标题模板
     * @param titleArg      标题参数，例如 Minecraft 版本号
     * @param detail        详情，例如安装路径或失败的加载器列表
     */
    static QString withDetail(const QString &titleTemplate, const QString &titleArg,
                              const QString &detail);

    static QList<NotificationRecord> history();
    static void clearHistory();

    /// 重排所有卡片（窗口尺寸变化时传 false：直接落位，不播动画）
    void repositionAll(bool animate = true);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit NotificationManager(QObject *parent = nullptr);
    ~NotificationManager() override;

    void showCard(NotificationCard::Type type, QWidget *parent, const QString &text, int duration,
                  const QString &actionText = QString());
    void removeCard(NotificationCard *card);

    static NotificationManager *m_instance;
    QList<NotificationCard*> m_cards;
    QSet<QWidget*> m_hosts;   ///< 已监听尺寸变化的宿主窗口
    QList<NotificationRecord> m_history;
};

#endif
