#include "NotificationManager.h"
#include "NotificationCard.h"

#include <QCoreApplication>
#include <QPropertyAnimation>
#include <QWidget>

NotificationManager *NotificationManager::m_instance = nullptr;

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
}

NotificationManager::~NotificationManager()
{
    m_instance = nullptr;
    for (NotificationCard *card : m_cards) {
        card->deleteLater();
    }
    m_cards.clear();
}

NotificationManager *NotificationManager::instance()
{
    if (!m_instance) {
        // 挂到 qApp 下，应用退出时随 qApp 一并析构，残留卡片也会被清理
        m_instance = new NotificationManager(QCoreApplication::instance());
    }
    return m_instance;
}

void NotificationManager::showSuccess(QWidget *parent, const QString &text, int duration)
{
    instance()->showCard(NotificationCard::Success, parent, text, duration);
}

void NotificationManager::showInfo(QWidget *parent, const QString &text, int duration)
{
    instance()->showCard(NotificationCard::Info, parent, text, duration);
}

void NotificationManager::showError(QWidget *parent, const QString &text, int duration)
{
    instance()->showCard(NotificationCard::Error, parent, text, duration);
}

QString NotificationManager::withDetail(const QString &titleTemplate, const QString &titleArg,
                                        const QString &detail)
{
    QString format = titleTemplate;
    if (!format.contains(QLatin1String("%1"))) {
        // 连标题参数都丢了：把参数补在前面，至少不吞信息
        format.prepend(QLatin1String("%1 "));
    }
    if (!format.contains(QLatin1String("%2"))) {
        // 译文漏了 %2：详情换行接在后面，效果与模板一致
        return format.arg(titleArg) + QLatin1String("\r\n") + detail;
    }
    // 两个占位符一次性替换：标题参数里万一含有 %2 也不会被再次解析
    return format.arg(titleArg, detail);
}

void NotificationManager::showCard(NotificationCard::Type type, QWidget *parent,
                                    const QString &text, int duration)
{
    NotificationCard *card = new NotificationCard(text, type, duration, parent);

    connect(card, &NotificationCard::dismissRequested,
            this, &NotificationManager::removeCard);

    m_cards.append(card);

    NotificationRecord rec;
    rec.text = text;
    rec.type = type;
    rec.timestamp = QDateTime::currentDateTime();
    m_history.append(rec);

    if (m_history.size() > 500)
        m_history.removeFirst();

    repositionAll();
}

void NotificationManager::removeCard(NotificationCard *card)
{
    m_cards.removeOne(card);
    repositionAll();
}

QList<NotificationRecord> NotificationManager::history()
{
    return instance()->m_history;
}

void NotificationManager::clearHistory()
{
    instance()->m_history.clear();
}

void NotificationManager::repositionAll()
{
    if (m_cards.isEmpty()) return;

    QWidget *win = m_cards.first()->window();
    if (!win) return;

    int x = 16;
    int y = win->height() - 16;

    for (int i = m_cards.size() - 1; i >= 0; --i) {
        NotificationCard *card = m_cards.at(i);
        y -= card->height();

        QPoint targetPos(x, y);

        if (card->pos() != targetPos) {
            QPropertyAnimation *anim = new QPropertyAnimation(card, "pos", card);
            anim->setDuration(200);
            anim->setStartValue(card->pos());
            anim->setEndValue(targetPos);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }

        y -= 8;
    }
}
