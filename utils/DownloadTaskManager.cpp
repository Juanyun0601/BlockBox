/**
 * @file   DownloadTaskManager.cpp
 * @brief  下载任务管理器类实现
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "DownloadTaskManager.h"

#include <QDateTime>
#include <QtGlobal>

DownloadTaskManager* DownloadTaskManager::m_instance = nullptr;
QMutex DownloadTaskManager::m_instanceMutex;

DownloadTaskManager::DownloadTaskManager()
    : m_nextTaskId(1)
{
}

DownloadTaskManager::~DownloadTaskManager()
{
}

DownloadTaskManager* DownloadTaskManager::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new DownloadTaskManager();
        }
    }
    return m_instance;
}

QString DownloadTaskManager::generateTaskId()
{
    // 使用原子递增，避免多线程并发调用产生重复任务 ID
    return QString("task_%1").arg(m_nextTaskId.fetchAndAddRelaxed(1));
}

void DownloadTaskManager::emitActiveTaskCountChanged()
{
    QMutexLocker locker(&m_tasksMutex);
    int count = 0;
    for (const DownloadTask &task : m_tasks) {
        if (task.status == DownloadTaskStatus::Queued || task.status == DownloadTaskStatus::Downloading) {
            count++;
        }
    }
    emit activeTaskCountChanged(count);
}

QString DownloadTaskManager::addTask(const QString &instanceName, const QString &instancePath, const QString &mcVersion, const QStringList &loaders)
{
    QString taskId = generateTaskId();
    
    DownloadTask task;
    task.taskId = taskId;
    task.instanceName = instanceName;
    task.instancePath = instancePath;
    task.mcVersion = mcVersion;
    task.loaders = loaders;
    task.progress = 0;
    task.status = DownloadTaskStatus::Queued;
    task.statusMessage = tr("等待中");
    task.stage = DownloadStage::ManifestFetch;
    task.totalStages = 7;
    task.maxRetries = 3;
    task.startTime = QDateTime::currentSecsSinceEpoch();
    
    {
        QMutexLocker locker(&m_tasksMutex);
        m_tasks[taskId] = task;
    }
    emit taskAdded(taskId);
    emitActiveTaskCountChanged();
    
    return taskId;
}

void DownloadTaskManager::updateTaskProgress(const QString &taskId, int progress, qint64 bytesReceived, qint64 bytesTotal)
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId)) {
        return;
    }
    
    DownloadTask &task = m_tasks[taskId];
    task.progress = progress;
    task.progressDouble = static_cast<double>(progress);
    task.bytesReceived = bytesReceived;
    task.bytesTotal = bytesTotal;
    
    emit taskProgressUpdated(taskId, static_cast<double>(progress));
}

void DownloadTaskManager::updateTaskProgressDouble(const QString &taskId, double progress, qint64 bytesReceived, qint64 bytesTotal, qint64 speed)
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId)) {
        return;
    }
    
    DownloadTask &task = m_tasks[taskId];
    task.progress = static_cast<int>(progress);
    task.progressDouble = progress;
    task.bytesReceived = bytesReceived;
    task.bytesTotal = bytesTotal;
    task.downloadSpeed = speed;
    
    emit taskProgressUpdated(taskId, progress);
}

void DownloadTaskManager::updateTaskCurrentFile(const QString &taskId, const QString &fileName)
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId)) {
        return;
    }
    
    DownloadTask &task = m_tasks[taskId];
    task.currentFile = fileName;
}

void DownloadTaskManager::updateTaskDisplayStatus(const QString &taskId, const QString &display)
{
    {
        QMutexLocker locker(&m_tasksMutex);
        if (!m_tasks.contains(taskId)) {
            return;
        }
        DownloadTask &task = m_tasks[taskId];
        task.displayStatus = display;
    }

    emit taskStatusChanged(taskId, m_tasks.value(taskId).status);
}

void DownloadTaskManager::updateTaskStatus(const QString &taskId, DownloadTaskStatus status, const QString &message)
{
    {
        QMutexLocker locker(&m_tasksMutex);
        if (!m_tasks.contains(taskId)) {
            return;
        }
    
        DownloadTask &task = m_tasks[taskId];
        task.status = status;
        if (!message.isEmpty()) {
            task.statusMessage = message;
        }
    }
    
    emit taskStatusChanged(taskId, status);
    emitActiveTaskCountChanged();
}

void DownloadTaskManager::updateTaskStep(const QString &taskId, const QString &step)
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId)) {
        return;
    }
    
    DownloadTask &task = m_tasks[taskId];
    task.currentStep = step;
}

void DownloadTaskManager::updateTaskSpeed(const QString &taskId, qint64 speed)
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId)) {
        return;
    }

    DownloadTask &task = m_tasks[taskId];
    task.downloadSpeed = speed;

    emit taskSpeedUpdated(taskId, speed);
}

void DownloadTaskManager::updateTaskStage(const QString &taskId, DownloadStage stage, int stageProgress)
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId)) {
        return;
    }

    DownloadTask &task = m_tasks[taskId];
    task.stage = stage;
    task.stageProgress = stageProgress;

    emit taskStageChanged(taskId, stage, stageProgress);
}

void DownloadTaskManager::updateTaskProgressPercent(const QString &taskId, int percent)
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId))
    {
        return;
    }

    DownloadTask &task = m_tasks[taskId];
    // 进度不倒退保护（参考 HMCL）
    if (percent <= task.progress)
    {
        return;
    }

    task.progress = percent;
    task.progressDouble = static_cast<double>(percent);
    // 不覆盖 bytesReceived/bytesTotal/downloadSpeed
    // 保留原版下载阶段的字节信息，用于 ETA 计算

    emit taskProgressUpdated(taskId, static_cast<double>(percent));
}

StageWeight DownloadTaskManager::getStageWeight(DownloadStage stage)
{
    // 参考 HMCL 做法：各阶段权重分配
    switch (stage)
    {
    case DownloadStage::ManifestFetch:  return {stage, 0,  5,  5};
    case DownloadStage::VersionJson:    return {stage, 5,  10, 5};
    case DownloadStage::ClientJar:      return {stage, 10, 25, 15};
    case DownloadStage::Libraries:      return {stage, 25, 60, 35};
    case DownloadStage::Assets:         return {stage, 60, 80, 20};
    case DownloadStage::LoaderInstall:  return {stage, 80, 98, 18};
    case DownloadStage::Completed:      return {stage, 98, 100, 2};
    }
    return {stage, 0, 0, 0};
}

int DownloadTaskManager::calculateStageProgress(DownloadStage stage, int internalProgress)
{
    StageWeight sw = getStageWeight(stage);
    if (sw.weight <= 0) return 0;
    int clamped = qBound(0, internalProgress, 100);
    return sw.startPercent + sw.weight * clamped / 100;
}

void DownloadTaskManager::updateTaskStageProgress(const QString &taskId, DownloadStage stage, int internalProgress)
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId)) return;

    int globalProgress = calculateStageProgress(stage, internalProgress);
    DownloadTask &task = m_tasks[taskId];

    // 阶段内不允许回退
    if (task.stage == stage && globalProgress <= task.progress)
    {
        return;
    }

    // 阶段切换时：取阶段权重进度与当前进度的较大值，避免进度回退
    if (globalProgress < task.progress)
    {
        globalProgress = task.progress;
    }

    task.stage = stage;
    task.stageProgress = internalProgress;
    task.progress = globalProgress;
    task.progressDouble = static_cast<double>(globalProgress);

    emit taskStageChanged(taskId, stage, internalProgress);
    emit taskProgressUpdated(taskId, task.progressDouble);
}

void DownloadTaskManager::updateTaskFileProgress(const QString &taskId, const QString &fileName, qint64 bytesReceived, qint64 bytesTotal)
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId)) {
        return;
    }

    DownloadTask &task = m_tasks[taskId];
    task.currentFile = fileName;

    emit taskFileProgressUpdated(taskId, fileName, bytesReceived, bytesTotal);
}

void DownloadTaskManager::removeTask(const QString &taskId)
{
    {
        QMutexLocker locker(&m_tasksMutex);
        if (!m_tasks.contains(taskId)) {
            return;
        }
    
        m_tasks.remove(taskId);
    }
    emit taskRemoved(taskId);
    emitActiveTaskCountChanged();
}

void DownloadTaskManager::cancelTask(const QString &taskId)
{
    {
        QMutexLocker locker(&m_tasksMutex);
        if (!m_tasks.contains(taskId)) {
            return;
        }
    
        DownloadTask &task = m_tasks[taskId];
        if (task.status != DownloadTaskStatus::Queued && 
            task.status != DownloadTaskStatus::Downloading &&
            task.status != DownloadTaskStatus::Paused) {
            return;
        }
    
        task.status = DownloadTaskStatus::Cancelled;
        task.statusMessage = tr("已取消");
    }
    
    emit taskCancelled(taskId);
    emit taskStatusChanged(taskId, DownloadTaskStatus::Cancelled);
    emitActiveTaskCountChanged();
}

void DownloadTaskManager::pauseTask(const QString &taskId)
{
    {
        QMutexLocker locker(&m_tasksMutex);
        if (!m_tasks.contains(taskId)) {
            return;
        }
    
        DownloadTask &task = m_tasks[taskId];
        if (task.status != DownloadTaskStatus::Downloading) {
            return;
        }
    
        task.status = DownloadTaskStatus::Paused;
        task.statusMessage = tr("已暂停");
    }
    
    emit taskPaused(taskId);
    emit taskStatusChanged(taskId, DownloadTaskStatus::Paused);
    emitActiveTaskCountChanged();
}

void DownloadTaskManager::resumeTask(const QString &taskId)
{
    {
        QMutexLocker locker(&m_tasksMutex);
        if (!m_tasks.contains(taskId)) {
            return;
        }
    
        DownloadTask &task = m_tasks[taskId];
        if (task.status != DownloadTaskStatus::Paused) {
            return;
        }
    
        task.status = DownloadTaskStatus::Downloading;
        task.statusMessage = tr("下载中");
    }
    
    emit taskResumed(taskId);
    emit taskStatusChanged(taskId, DownloadTaskStatus::Downloading);
    emitActiveTaskCountChanged();
}

bool DownloadTaskManager::isTaskCancellable(const QString &taskId) const
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId)) {
        return false;
    }
    
    const DownloadTask &task = m_tasks[taskId];
    return task.status == DownloadTaskStatus::Queued || 
           task.status == DownloadTaskStatus::Downloading ||
           task.status == DownloadTaskStatus::Paused;
}

bool DownloadTaskManager::isTaskPausable(const QString &taskId) const
{
    QMutexLocker locker(&m_tasksMutex);
    if (!m_tasks.contains(taskId)) {
        return false;
    }
    
    const DownloadTask &task = m_tasks[taskId];
    return task.status == DownloadTaskStatus::Downloading;
}

QList<DownloadTask> DownloadTaskManager::getAllTasks() const
{
    QMutexLocker locker(&m_tasksMutex);
    return m_tasks.values();
}

const DownloadTask* DownloadTaskManager::getTask(const QString &taskId) const
{
    QMutexLocker locker(&m_tasksMutex);
    auto it = m_tasks.constFind(taskId);
    if (it != m_tasks.constEnd()) {
        return &it.value();
    }
    return nullptr;
}

int DownloadTaskManager::getActiveTaskCount() const
{
    QMutexLocker locker(&m_tasksMutex);
    int count = 0;
    for (const DownloadTask &task : m_tasks) {
        if (task.status == DownloadTaskStatus::Queued || task.status == DownloadTaskStatus::Downloading) {
            count++;
        }
    }
    return count;
}

int DownloadTaskManager::getTotalTaskCount() const
{
    QMutexLocker locker(&m_tasksMutex);
    return m_tasks.size();
}
