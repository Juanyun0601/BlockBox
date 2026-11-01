#ifndef TASKBAR_H
#define TASKBAR_H

#include <QHash>
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
class QTimer;

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

    /** 按父控件尺寸重算悬浮几何（贴底居中），并置顶显示 */
    void updateFloatGeometry();

signals:
    void taskDetailRequested(const QString &taskId);
    void launchTaskCardClicked();
    void showTaskListPageRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    void updateCountBadge();
    void fadeOutAndRemoveDownloadCard(const QString &taskId);
    void scheduleDownloadCardDismissal(const QString &taskId, int delayMs);
    void cancelDownloadCardDismissal(const QString &taskId);
    void onLaunchCardDismissTimeout();

    QHBoxLayout *m_mainLayout;
    QWidget *m_tray;
    QHBoxLayout *m_trayLayout;
    QScrollArea *m_scrollArea;
    QWidget *m_cardsContent;
    QHBoxLayout *m_cardsLayout;
    QPushButton *m_viewAllBtn;
    QLabel *m_countBadge;

    // Launch task
    LaunchTaskCard *m_launchTaskCard;
    bool m_hasLaunchCard;

    // Download tasks
    QMap<QString, DownloadTaskCard*> m_downloadTaskCards;

    // 任务结束后自动收起卡片用的一次性定时器（按任务 id 索引）
    QHash<QString, QTimer*> m_cardDismissTimers;
    // 启动失败/异常退出的启动卡片自动收起的定时器
    QTimer *m_launchDismissTimer;

    // Exit animation
    QPropertyAnimation *m_exitAnim;
    bool m_exitAnimRunning;
    QRect m_exitAnimTargetGeo;
};

#endif // TASKBAR_H
