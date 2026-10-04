#ifndef LAUNCHTASKCARD_H
#define LAUNCHTASKCARD_H

#include <QEnterEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>
#include <QWidget>

class LaunchTaskCard : public QWidget
{
    Q_OBJECT

public:
    enum LaunchStatus {
        Idle,
        Launching,
        Running,
        Failed,
        Stopped
    };

    explicit LaunchTaskCard(QWidget *parent = nullptr);
    ~LaunchTaskCard();

    void setStatus(LaunchStatus status);
    void setProgress(int progress);
    void setMessage(const QString &message);
    void addDetail(const QString &detail);
    void clearDetails();
    QStringList details() const;

signals:
    void showDetailsPageRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onContextMenu(const QPoint &pos);

private:
    void initUI();
    void paintStatusIcon(QPainter &painter, const QRect &rect);
    void paintProgressBar(QPainter &painter, const QRect &rect);

    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_headerLayout;

    QLabel *m_statusTextLabel;
    QLabel *m_progressLabel;
    QLabel *m_messageLabel;

    LaunchStatus m_status;
    int m_progress;
    QString m_message;
    QStringList m_details;
    bool m_hovered;
};

#endif // LAUNCHTASKCARD_H
