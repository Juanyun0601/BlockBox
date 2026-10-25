#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H

#include <QDateTime>
#include <QList>
#include <QObject>

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

    void repositionAll();

private:
    explicit NotificationManager(QObject *parent = nullptr);
    ~NotificationManager() override;

    void showCard(NotificationCard::Type type, QWidget *parent, const QString &text, int duration);
    void removeCard(NotificationCard *card);

    static NotificationManager *m_instance;
    QList<NotificationCard*> m_cards;
    QList<NotificationRecord> m_history;
};

#endif
