#ifndef TASKLISTPAGE_H
#define TASKLISTPAGE_H

#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "components/DownloadTaskCard.h"
#include "components/LaunchTaskCard.h"
#include "utils/DownloadTaskManager.h"

class TaskListPage : public QWidget
{
    Q_OBJECT

public:
    explicit TaskListPage(QWidget *parent = nullptr);
    ~TaskListPage();

    void setLaunchTaskInfo(const QString &name, LaunchTaskCard::LaunchStatus status, int progress, const QString &message = QString());

signals:
    void backToMainRequested();
    void taskDetailRequested(const QString &taskId);

private slots:
    void onTaskAdded(const QString &taskId);
    void onTaskProgressUpdated(const QString &taskId, double progress);
    void onTaskStatusChanged(const QString &taskId, DownloadTaskStatus status);
    void onTaskRemoved(const QString &taskId);

private:
    void initUI();
    void loadTasks();
    void rebuildList();
    QWidget* createGroupSection(const QString &title, QLabel *&countLabel, QVBoxLayout *&contentLayout);
    void updateGroupCounts();

    QVBoxLayout *m_mainLayout;
    QVBoxLayout *m_contentLayout;
    QScrollArea *m_scrollArea;
    QWidget *m_scrollContent;

    QMap<QString, DownloadTaskCard*> m_taskCards;

    QWidget *m_activeGroup;
    QVBoxLayout *m_activeLayout;
    QLabel *m_activeCount;

    QWidget *m_failedGroup;
    QVBoxLayout *m_failedLayout;
    QLabel *m_failedCount;

    QWidget *m_completedGroup;
    QVBoxLayout *m_completedLayout;
    QLabel *m_completedCount;

    QWidget *m_emptyWidget;

    void updateLaunchDisplay();

    // Launch task tracking
    QString m_launchName;
    LaunchTaskCard::LaunchStatus m_launchStatus;
    int m_launchProgress;
    QString m_launchMessage;
    bool m_hasLaunchTask;
    QWidget *m_launchCardWidget;
    QLabel *m_launchStatusLabel;
    QLabel *m_launchProgressLabel;
};

#endif // TASKLISTPAGE_H
