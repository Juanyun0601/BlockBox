/**
 * @file   TaskDetailPage.h
 * @brief  任务详情页面类声明（现代化重构版）
 * @author BlockBox Team
 * @date   2026-08-15
 */
#ifndef TASKDETAILPAGE_H
#define TASKDETAILPAGE_H

#include <QGridLayout>
#include <QLabel>
#include <QMap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "components/TaskProgressWidgets.h"
#include "utils/DownloadTaskManager.h"

/**
 * @class TaskDetailPage
 * @brief 任务详情页（现代化布局）
 *
 * 布局自上而下：
 *  - 顶栏：返回按钮 + 标题 + 状态胶囊
 *  - 滚动区域（Hero 卡 / 进度卡(阶段步骤条+大进度条) / 文件清单 / 任务信息 / 任务日志 / 操作栏）
 */
class TaskDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit TaskDetailPage(QWidget *parent = nullptr);
    ~TaskDetailPage();

    void setTaskId(const QString &taskId);

signals:
    void backToTaskListRequested();
    void taskCancelled(const QString &taskId);

private slots:
    void onTaskProgressUpdated(const QString &taskId, double progress);
    void onTaskStatusChanged(const QString &taskId, DownloadTaskStatus status);
    void onTaskSpeedUpdated(const QString &taskId, qint64 speed);
    void onTaskStageChanged(const QString &taskId, DownloadStage stage, int stageProgress);
    void onTaskFileProgressUpdated(const QString &taskId, const QString &fileName,
                                   qint64 bytesReceived, qint64 bytesTotal);
    void onTaskRemoved(const QString &taskId);
    void onCancelButtonClicked();
    void onPauseButtonClicked();
    void onBackClicked();
    void onAnimationTick();

private:
    void initUI();
    void updateDisplay();
    void updateButtons();
    void updateStatusPill();
    void updateThemeColors();
    void resetFileList();
    void upsertFileRow(const QString &fileName, qint64 received, qint64 total);
    void appendLog(const QString &text, const QString &color = QString());
    QWidget *createCard(QWidget *content, const QString &title, const QString &subtitle = QString(),
                        const QString &cardTitleObject = QStringLiteral("tdCardTitle"));
    QLabel *createStatTile(const QString &label, const QString &id);
    QLabel *createChip(const QString &text);
    void setPillState(const QString &state);
    static QString stageToString(DownloadStage stage);

    struct FileRow
    {
        QWidget *row = nullptr;
        QLabel *nameLabel = nullptr;
        QProgressBar *bar = nullptr;
        QLabel *stateLabel = nullptr;
        qint64 received = 0;
        qint64 total = 0;
    };

    QString m_taskId;
    DownloadTask m_currentTask;
    bool m_simulating;
    double m_animProgress;
    double m_animSpeed;
    int m_smoothEta;

    // 顶栏
    QPushButton *m_backButton;
    QLabel *m_pageTitleLabel;
    QLabel *m_statusPill;

    // Hero
    QLabel *m_heroTitleLabel;
    QLabel *m_heroSubLabel;
    QWidget *m_heroChipsRow;
    QHBoxLayout *m_heroChipsLayout;
    QLabel *m_statSpeed;
    QLabel *m_statSize;
    QLabel *m_statEta;
    QLabel *m_statThreads;
    RingProgressWidget *m_ring;

    // 进度卡
    QLabel *m_stageChip;
    StageStepperWidget *m_stepper;
    QLabel *m_bigPctLabel;
    QProgressBar *m_bigBar;
    QLabel *m_footLeft;
    QLabel *m_footRight;
    SpeedChartWidget *m_speedChart;

    // 文件清单
    QLabel *m_fileCountChip;
    QWidget *m_fileListContainer;
    QVBoxLayout *m_fileListLayout;
    QMap<QString, FileRow> m_fileRows;

    // 任务信息
    QGridLayout *m_infoGrid;
    QMap<QString, QLabel *> m_infoValues;

    // 日志
    QPlainTextEdit *m_logView;
    int m_logCount;

    // 操作栏
    QPushButton *m_pauseButton;
    QPushButton *m_cancelButton;
    QPushButton *m_restartButton;

    QTimer m_animationTimer;
};

#endif // TASKDETAILPAGE_H
