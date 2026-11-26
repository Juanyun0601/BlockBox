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

    NotificationCard(const QString &text, Type type, int duration,
                     const QString &actionText = QString(), QWidget *parent = nullptr);
    ~NotificationCard() override;

    Type notificationType() const { return m_type; }
    QString messageText() const { return m_text; }
    int remainingSeconds() const { return m_remainingSeconds; }
    /// 是否已经进入退场流程（正在滑出、等待销毁）
    bool isDismissing() const { return m_dismissed; }

    /// 停掉正在跑的滑入 / 重排动画，避免和外面的定位抢位置
    void stopPosAnimations();

    /// 显示卡片并播放左下角滑入动画（由 NotificationManager 在所有卡片排好位后调用）
    void startSlideIn();

signals:
    void dismissRequested(NotificationCard *card);
    void copyClicked(const QString &text);
    void timeExtended(NotificationCard *card);
    /// 点了卡片上的附加操作按钮（如「网络自检」）
    void actionClicked(NotificationCard *card);

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
    /// 退场收尾：隐藏并销毁卡片（滑出动画结束和兜底超时都会走到这里）
    void finishDismiss();
    /// 悬停暂停期间定期确认指针还在卡片上，漏收 leave 时恢复倒计时
    void onHoverWatchdog();

private:
    void initUI();
    void initStyle();
    void resumeCountdownIfNeeded();
    bool pointerOnCard() const;

    QString m_text;
    QString m_actionText;
    Type m_type;
    int m_remainingSeconds;
    int m_totalDuration;
    bool m_dismissed;
    bool m_finished;

    QLabel *m_textLabel;
    QPushButton *m_copyButton;
    QPushButton *m_actionButton;
    QLabel *m_countdownLabel;
    QTimer *m_countdownTimer;
    QTimer *m_hoverWatchdog;
};

#endif
