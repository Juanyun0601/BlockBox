/**
 * @file   DownloadTaskManager.h
 * @brief  下载任务管理器类定义
 * @author BlockBox Team
 * @date   2026-05-09
 */

#pragma once

#include <QAtomicInt>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

enum class DownloadStage {
    ManifestFetch,
    VersionJson,
    ClientJar,
    Libraries,
    Assets,
    LoaderInstall,
    Completed
};

/// 阶段权重配置（参考 HMCL 做法，各阶段有明确的百分比范围）
struct StageWeight
{
    DownloadStage stage;
    int startPercent;   ///< 阶段起始百分比
    int endPercent;     ///< 阶段结束百分比
    int weight;         ///< 阶段权重（endPercent - startPercent）
};

enum class DownloadTaskStatus {
    Queued,
    Downloading,
    Paused,
    Completed,
    Failed,
    Cancelled
};

struct DownloadTask {
    QString taskId;
    QString instanceName;
    QString instancePath;
    QString mcVersion;
    QStringList loaders;
    int progress;
    double progressDouble;
    DownloadTaskStatus status;
    QString statusMessage;
    QString displayStatus; ///< 卡片显示用短状态（传输中/已完成/失败/已取消）
    qint64 bytesReceived;
    qint64 bytesTotal;
    qint64 downloadSpeed;
    QString currentStep;
    QString currentFile;
    DownloadStage stage;
    int stageProgress;
    int totalStages;
    QStringList fileQueue;
    int retryCount;
    int maxRetries;
    qint64 startTime;
    qint64 estimatedEndTime;

    DownloadTask()
        : progress(0)
        , progressDouble(0.0)
        , status(DownloadTaskStatus::Queued)
        , bytesReceived(0)
        , bytesTotal(0)
        , downloadSpeed(0)
        , stage(DownloadStage::ManifestFetch)
        , stageProgress(0)
        , totalStages(7)
        , retryCount(0)
        , maxRetries(3)
        , startTime(0)
        , estimatedEndTime(0)
    {}
};

class DownloadTaskManager : public QObject
{
    Q_OBJECT

public:
    static DownloadTaskManager* instance();

    QString addTask(const QString &instanceName, const QString &instancePath, const QString &mcVersion, const QStringList &loaders = QStringList());
    void updateTaskProgress(const QString &taskId, int progress, qint64 bytesReceived, qint64 bytesTotal);
    void updateTaskProgressDouble(const QString &taskId, double progress, qint64 bytesReceived, qint64 bytesTotal, qint64 speed);
    void updateTaskCurrentFile(const QString &taskId, const QString &fileName);
    void updateTaskDisplayStatus(const QString &taskId, const QString &display);
    void updateTaskStatus(const QString &taskId, DownloadTaskStatus status, const QString &message = QString());
    void updateTaskStep(const QString &taskId, const QString &step);
    void updateTaskSpeed(const QString &taskId, qint64 speed);
    void updateTaskStage(const QString &taskId, DownloadStage stage, int stageProgress = 0);
    void updateTaskFileProgress(const QString &taskId, const QString &fileName, qint64 bytesReceived, qint64 bytesTotal);
    void updateTaskProgressPercent(const QString &taskId, int percent);
    /// 统一阶段进度更新（参考 HMCL 做法）
    void updateTaskStageProgress(const QString &taskId, DownloadStage stage, int internalProgress);

    /// 阶段权重计算
    static StageWeight getStageWeight(DownloadStage stage);
    static int calculateStageProgress(DownloadStage stage, int internalProgress);
    void removeTask(const QString &taskId);
    void cancelTask(const QString &taskId);
    void pauseTask(const QString &taskId);
    void resumeTask(const QString &taskId);
    bool isTaskCancellable(const QString &taskId) const;
    bool isTaskPausable(const QString &taskId) const;
    QList<DownloadTask> getAllTasks() const;
    const DownloadTask* getTask(const QString &taskId) const;
    DownloadTask getTaskCopy(const QString &taskId) const { return m_tasks.value(taskId); }
    int getActiveTaskCount() const;
    int getTotalTaskCount() const;

signals:
    void taskAdded(const QString &taskId);
    void taskProgressUpdated(const QString &taskId, double progress);
    void taskSpeedUpdated(const QString &taskId, qint64 speed);
    void taskStageChanged(const QString &taskId, DownloadStage stage, int stageProgress);
    void taskFileProgressUpdated(const QString &taskId, const QString &fileName, qint64 bytesReceived, qint64 bytesTotal);
    void taskStatusChanged(const QString &taskId, DownloadTaskStatus status);
    void taskRemoved(const QString &taskId);
    void taskCancelled(const QString &taskId);
    void taskPaused(const QString &taskId);
    void taskResumed(const QString &taskId);
    void activeTaskCountChanged(int count);

private:
    DownloadTaskManager();
    ~DownloadTaskManager();

    QString generateTaskId();
    void emitActiveTaskCountChanged();

    static DownloadTaskManager *m_instance;
    static QMutex m_instanceMutex;
    mutable QMutex m_tasksMutex;
    QMap<QString, DownloadTask> m_tasks;
    QAtomicInt m_nextTaskId;
};
