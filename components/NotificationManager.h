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
