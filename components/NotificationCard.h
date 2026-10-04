#ifndef NOTIFICATIONCARD_H
#define NOTIFICATIONCARD_H

#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QEnterEvent>
#endif

class NotificationCard : public QWidget
{
    Q_OBJECT

public:
    enum Type { Success, Info, Error };

    NotificationCard(const QString &text, Type type, int duration, QWidget *parent = nullptr);
    ~NotificationCard() override;

    Type notificationType() const { return m_type; }
    QString messageText() const { return m_text; }
    int remainingSeconds() const { return m_remainingSeconds; }

signals:
    void dismissRequested(NotificationCard *card);
    void copyClicked(const QString &text);
    void timeExtended(NotificationCard *card);

public slots:
    void dismiss();
    void extendTime(int seconds = 15);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event) override;
#else
    void enterEvent(QEvent *event) override;
#endif
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onCountdownTick();
    void onCopyClicked();

private:
    void initUI();
    void initStyle();
    void startSlideIn();

    QString m_text;
    Type m_type;
    int m_remainingSeconds;
    int m_totalDuration;
    bool m_dismissed;

    QLabel *m_textLabel;
    QPushButton *m_copyButton;
    QLabel *m_countdownLabel;
    QTimer *m_countdownTimer;
};

#endif
