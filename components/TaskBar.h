#ifndef TASKBAR_H
#define TASKBAR_H

#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
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

    // ── 启动卡片（多实例并行启动：一个实例一张卡片，键为实例路径）──
    // 取出该实例的启动卡片，没有则创建（标题带实例名）
    LaunchTaskCard* ensureLaunchCard(const QString &instancePath, const QString &title);
    LaunchTaskCard* launchCard(const QString &instancePath) const;
    void showLaunchCard(const QString &instancePath, bool show);
    void updateLaunchCardStatus(const QString &instancePath, LaunchTaskCard::LaunchStatus status);
    void updateLaunchCardProgress(const QString &instancePath, int progress);
    void updateLaunchCardMessage(const QString &instancePath, const QString &message);
    void addLaunchCardDetail(const QString &instancePath, const QString &detail);
    /** 任务彻底结束后移除卡片（正常退出/用户取消时立即收起） */
    void removeLaunchCard(const QString &instancePath);
    /** 是否还有显示中的启动卡片 */
    bool hasVisibleLaunchCards() const;

    /** 按父控件尺寸重算悬浮几何（贴底居中），并置顶显示 */
    void updateFloatGeometry();

    /** 是否处于收起状态（整条任务栏滑到窗口下沿，只剩「展开」小按钮） */
    bool isCollapsed() const { return m_collapsed; }
    /** 收起 / 展开任务栏；animate=false 时直接落位（用于初始化与尺寸变化） */
    void setCollapsed(bool collapsed, bool animate = true);

signals:
    void taskDetailRequested(const QString &taskId);
    /** 启动卡片被点击：携带卡片所属实例路径，主窗口据此聚焦对应启动会话 */
    void launchTaskCardClicked(const QString &instancePath);
    void showTaskListPageRequested();
    /** 收起状态变化（true=已收起，只剩展开按钮） */
    void collapsedChanged(bool collapsed);

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
    void onToggleCollapsed();

private:
    void initUI();
    void initDownloadTaskManager();
    void updateVisibility();
    void setupCardConnections(DownloadTaskCard *card, const QString &taskId);
    void updateCountBadge();
    void fadeOutAndRemoveDownloadCard(const QString &taskId);
    void scheduleDownloadCardDismissal(const QString &taskId, int delayMs);
    void cancelDownloadCardDismissal(const QString &taskId);
    void onLaunchCardDismissTimeout(const QString &instancePath);

    /** 展开态几何：整条托盘贴底居中 */
    QRect expandedGeometry() const;
    /** 收起态几何：只剩底部居中的「展开」小胶囊，贴在窗口下沿 */
    QRect collapsedGeometry() const;
    /** 收起时任务栏顶边的 y：只露出展开按钮那一行 */
    int collapsedTop() const;
    /** 按当前收起状态落位（收起时把托盘藏起来并收窄，展开时恢复整条） */
    void applyFloatGeometry();
    /** 展开态显示的任务数（下载卡片 + 显示中的启动卡片） */
    int visibleTaskCount() const;
    /** 刷新收起/展开按钮的文案、图标、提示与鼠标指针 */
    void updateToggleButton();

    QVBoxLayout *m_mainLayout;
    QWidget *m_toggleRow;
    QPushButton *m_toggleBtn;
    QWidget *m_tray;
    QHBoxLayout *m_trayLayout;
    QHBoxLayout *m_launchCardsLayout;
    QScrollArea *m_scrollArea;
    QWidget *m_cardsContent;
    QHBoxLayout *m_cardsLayout;
    QPushButton *m_viewAllBtn;
    QLabel *m_countBadge;

    // Launch tasks（多实例并行启动：按实例路径一卡一会话）
    QHash<QString, LaunchTaskCard*> m_launchCards;
    // 启动失败/异常退出的启动卡片自动收起的定时器（按实例路径索引）
    QHash<QString, QTimer*> m_launchDismissTimers;

    // Download tasks
    QMap<QString, DownloadTaskCard*> m_downloadTaskCards;

    // 任务结束后自动收起卡片用的一次性定时器（按任务 id 索引）
    QHash<QString, QTimer*> m_cardDismissTimers;

    // 收起 / 展开滑动动画
    QPropertyAnimation *m_slideAnim;
    bool m_slideAnimRunning;
    bool m_collapsed;

    // Exit animation
    QPropertyAnimation *m_exitAnim;
    bool m_exitAnimRunning;
    QRect m_exitAnimTargetGeo;
};

#endif // TASKBAR_H
