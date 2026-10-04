#ifndef TASKBAR_H
#define TASKBAR_H

#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QWidget>

#include "DownloadTaskCard.h"
#include "LaunchTaskCard.h"
#include "utils/DownloadTaskManager.h"

class QPropertyAnimation;

class TaskBar : public QWidget
{
    Q_OBJECT

public:
    explicit TaskBar(QWidget *parent = nullptr);
    ~TaskBar();

    // Launch task card methods
    void showLaunchTaskCard(bool show);
    LaunchTaskCard* launchTaskCard() const;
    void updateLaunchTaskStatus(LaunchTaskCard::LaunchStatus status);
    void updateLaunchTaskProgress(int progress);
    void updateLaunchTaskMessage(const QString &message);
    void addLaunchTaskDetail(const QString &detail);

signals:
    void taskDetailRequested(const QString &taskId);
    void launchTaskCardClicked();
    void showTaskListPageRequested();

private slots:
    void onTaskAdded(const QString &taskId);
    void onTaskProgressUpdated(const QString &taskId, double progress);
    void onTaskSpeedUpdated(const QString &taskId, qint64 speed);
    void onTaskStageChanged(const QString &taskId, DownloadStage stage, int stageProgress);
    void onTaskStatusChanged(const QString &taskId, DownloadTaskStatus status);
    void onTaskRemoved(const QString &taskId);
    void onActiveTaskCountChanged(int count);
    void onDownloadTaskCardClicked(const QString &taskId);


private:
    void initUI();
    void initDownloadTaskManager();
    void updateVisibility();
    void setupCardConnections(DownloadTaskCard *card, const QString &taskId);

    QHBoxLayout *m_mainLayout;
    QScrollArea *m_scrollArea;
    QWidget *m_cardsContent;
    QHBoxLayout *m_cardsLayout;
    QPushButton *m_viewAllBtn;

    // Launch task
    LaunchTaskCard *m_launchTaskCard;
    bool m_hasLaunchCard;

    // Download tasks
    QMap<QString, DownloadTaskCard*> m_downloadTaskCards;

    // Exit animation
    QPropertyAnimation *m_exitAnim;
    bool m_exitAnimRunning;
    QRect m_exitAnimTargetGeo;
};

#endif // TASKBAR_H
