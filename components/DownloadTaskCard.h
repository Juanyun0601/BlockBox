/**
 * @file   DownloadTaskCard.h
 * @brief  下载任务卡片组件类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef DOWNLOADTASKCARD_H
#define DOWNLOADTASKCARD_H

#include <QEnterEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "utils/DownloadTaskManager.h"
#include "utils/DownloadUtils.h"

class DownloadTaskCard : public QWidget
{
    Q_OBJECT

public:
    explicit DownloadTaskCard(const QString &taskId, QWidget *parent = nullptr);
    ~DownloadTaskCard();

    void setTaskId(const QString &taskId);
    QString taskId() const;
    void updateFromTask(const DownloadTask &task);
    void setExpanded(bool expanded);
    bool isExpanded() const;

signals:
    void cardClicked(const QString &taskId);
    void showDetailsRequested(const QString &taskId);
    void cancelRequested(const QString &taskId);
    void pauseRequested(const QString &taskId);
    void resumeRequested(const QString &taskId);

private slots:
    void onCardClicked();
    void onToggleDetails();
    void onCancelButtonClicked();
    void onPauseButtonClicked();
    void onContextMenu(const QPoint &pos);

private:
    void initUI();
    void updateDisplay();
    void paintProgressBar(QPainter &painter, const QRect &rect);
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

    QString m_taskId;
    bool m_expanded;
    bool m_hovered;
    DownloadTask m_currentTask;
    double m_smoothProgress;
    int m_smoothEta;

    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_headerLayout;
    QLabel *m_statusTextLabel;
    QLabel *m_progressLabel;
    QLabel *m_speedLabel;
    QLabel *m_instanceNameLabel;
    QPushButton *m_toggleDetailsButton;
    QWidget *m_detailsWidget;
    QVBoxLayout *m_detailsLayout;
    QLabel *m_detailsLabel;
    QLabel *m_loadersLabel;
    QLabel *m_stepLabel;
    QLabel *m_sizeLabel;
    QPushButton *m_cancelButton;
    QPushButton *m_pauseButton;
};

#endif // DOWNLOADTASKCARD_H