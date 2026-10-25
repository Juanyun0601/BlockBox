/**
 * @file   VersionDownloader.cpp
 * @brief  版本下载器类实现（全异步架构）
 * @author BlockBox Team
 * @date   2026-05-30
 */
#include "VersionDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

#include <QCryptographicHash>
#include <QThreadPool>
#include <QTimer>

#include "FileReuse.h"
#include "DownloadTaskManager.h"
#include "net/Download.h"
#include "net/ChecksumValidator.h"
#include "net/NetworkAccess.h"
#include "net/HttpMetaCache.h"

#include <QStandardPaths>
#include <functional>

#include "SettingsManager.h"

#include "utils/ManifestCache.h"

VersionDownloader* VersionDownloader::m_instance = nullptr;
static QMutex s_vdInstanceMutex;

VersionDownloader::VersionDownloader()
    : m_downloadSource(Official)
    , m_currentPhase(DownloadPhase::Idle)
    , m_isDownloading(false)
    , m_isPaused(false)
    , m_totalBytes(0)
    , m_downloadedBytes(0)
    , m_clientJarSize(0)
    , m_lastSpeedBytes(0)
    , m_lastSpeedTime(0)
{
}

VersionDownloader::~VersionDownloader()
{
    abortAllActiveReplies();
    {
        QMutexLocker locker(&m_stateMutex);
        m_isDownloading = false;
        m_isPaused = false;
    }
}

VersionDownloader* VersionDownloader::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&s_vdInstanceMutex);
        if (!m_instance) {
            m_instance = new VersionDownloader();
        }
    }
    return m_instance;
}

void VersionDownloader::downloadVanilla(const QString &versionId, const QString &instancePath,
                                         const QString &instanceName, const QStringList &loaders)
{
    // 参考 HMCL 做法：新实例目录始终为 <root>/versions/<versionId>/
    downloadVanillaTo(versionId, instancePath + "/versions/" + versionId, instanceName, loaders);
}

void VersionDownloader::downloadVanillaTo(const QString &versionId, const QString &targetInstancePath,
                                           const QString &instanceName, const QStringList &loaders)
{
    if (m_isDownloading)
    {
        // 之前这里静默返回，用户点了安装没有任何反应，看起来像“装不上”
        emit downloadFailed(tr("已有下载任务正在进行，请等待完成或取消后再试"));
        return;
    }

    m_currentVersionId = versionId;
    // 修改现有实例时，直接以既有实例目录为目标，实例目录与名称保持不变
    m_currentInstancePath = targetInstancePath;
    // 新任务开始：使上一个任务遗留的在途复用扫描结果作废
    ++m_reuseScanId;
    {
        QMutexLocker locker(&m_stateMutex);
        m_isDownloading = true;
    }
    m_isPaused = false;
    m_currentPhase = DownloadPhase::ManifestFetch;

    QString taskName = instanceName;
    if (taskName.isEmpty())
    {
        taskName = targetInstancePath.split("/").last();
        if (taskName.isEmpty())
        {
            taskName = versionId;
        }
    }

    QString taskId = DownloadTaskManager::instance()->addTaskWithType(
        taskName, m_currentInstancePath, versionId, loaders, TaskType::InstanceInstall);
    m_currentTaskId = taskId;

    emit downloadStarted(taskId);
    DownloadTaskManager::instance()->updateTaskStatus(taskId, DownloadTaskStatus::Downloading, tr("开始下载"));
    DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::ManifestFetch);
    emit statusChanged(QString("开始下载 Minecraft %1...").arg(versionId));
    updateTaskStatus(tr("开始下载"));

    fetchManifest();
}

void VersionDownloader::cancelDownload()
{
    {
        QMutexLocker locker(&m_stateMutex);
        m_isDownloading = false;
        m_isPaused = false;
    }
    // 取消任务：在途复用扫描结果作废
    ++m_reuseScanId;

    m_totalBytes = 0;
    m_downloadedBytes = 0;
    m_clientJarSize = 0;

    abortAllActiveReplies();

    if (!m_currentTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->cancelTask(m_currentTaskId);
        m_currentTaskId.clear();
    }

    m_currentVersionId.clear();
    m_currentInstancePath.clear();
    m_currentPhase = DownloadPhase::Idle;

    emit downloadCancelled();
}

void VersionDownloader::pauseDownload()
{
    if (!m_isDownloading || m_isPaused)
    {
        return;
    }

    {
        QMutexLocker locker(&m_stateMutex);
        m_isPaused = true;
    }

    // MultiMC 进度逻辑：中止阶段作业，已完成文件保留在完成集合中，
    // 恢复时重新扫描磁盘续传
    if (m_phaseJob)
    {
        m_phaseJob->abort();
    }

    if (!m_currentTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->pauseTask(m_currentTaskId);
    }

    emit downloadPaused();
}

void VersionDownloader::resumeDownload()
{
    if (!m_isPaused)
    {
        return;
    }

    {
        QMutexLocker locker(&m_stateMutex);
        m_isPaused = false;
    }

    if (!m_currentTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->resumeTask(m_currentTaskId);
    }

    switch (m_currentPhase)
    {
    case DownloadPhase::ClientJar:
        // 客户端 JAR 被暂停：重新发起网络下载（复用扫描会在磁盘上找到已完成文件）
        downloadClientJarFromNetwork();
        break;
    case DownloadPhase::Libraries:
        // 重新扫描磁盘：已完成的文件（完成集合）跳过，剩余重建阶段作业
        startLibrariesDownload();
        break;
    case DownloadPhase::Assets:
        startAssetsDownload();
        break;
    default:
        break;
    }

    emit downloadResumed();
}

bool VersionDownloader::isDownloading() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_isDownloading;
}

bool VersionDownloader::isPaused() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_isPaused;
}

QString VersionDownloader::currentTaskId() const
{
    return m_currentTaskId;
}

void VersionDownloader::setCurrentTaskId(const QString &taskId)
{
    m_currentTaskId = taskId;
}

void VersionDownloader::cleanupActiveReply(QNetworkReply *reply)
{
    m_activeReplies.removeOne(reply);
    reply->deleteLater();
}

void VersionDownloader::abortAllActiveReplies()
{
    for (QNetworkReply *reply : m_activeReplies)
    {
        reply->abort();
        reply->deleteLater();
    }
    m_activeReplies.clear();
    // MultiMC net 层的在途单文件任务：abort 后由 aborted 回调收尾
    const QList<NetJob::Ptr> jobs = m_fileJobs;
    m_fileJobs.clear();
    for (const auto &job : jobs)
    {
        if (job)
            job->abort();
    }
    if (m_phaseJob)
    {
        const NetJob::Ptr phaseJob = m_phaseJob;
        m_phaseJob.reset();
        phaseJob->abort();
    }
    m_phaseActionReceived.clear();
}

void VersionDownloader::failDownload(const QString &error)
{
    m_isDownloading = false;
    // 任务失败：在途复用扫描结果作废
    ++m_reuseScanId;
    abortAllActiveReplies();

    if (!m_currentTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Failed, error);
    }

    emit downloadFailed(error);
}

// ── 清单类 JSON：MultiMC net 层的 HttpMetaCache 缓存下载 ──
// ETag/Last-Modified 条件请求：内容未变时服务器回 304，直接复用本地缓存
void VersionDownloader::fetchJsonCached(const QStringList &urls, const QString &resourcePath,
                                        std::function<void(bool ok, const QByteArray &data)> onDone)
{
    HttpMetaCache *metaCache = HttpMetaCache::instance();
    metaCache->addBase(QStringLiteral("meta"),
                       QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                           + QStringLiteral("/cache/download-meta"));

    auto entry = metaCache->resolveEntry(QStringLiteral("meta"), resourcePath);
    auto dl = Net::Download::makeCached(QUrl(urls.first()), entry);
    for (int i = 1; i < urls.size(); ++i)
        dl->addUrlFallback(QUrl(urls.at(i)));

    auto job = NetJob::Ptr(new NetJob(resourcePath, Net::networkAccessManager()));
    job->addNetAction(dl);

    connect(job.get(), &Task::finished, this, [this, job, entry, onDone]() {
        if (!job->wasSuccessful() || !m_isDownloading || m_isPaused)
        {
            onDone(false, QByteArray());
            return;
        }
        QFile file(entry->getFullPath());
        if (!file.open(QIODevice::ReadOnly))
        {
            onDone(false, QByteArray());
            return;
        }
        onDone(true, file.readAll());
    });
    job->start();
}

void VersionDownloader::fetchManifest()
{
    ManifestCache *cache = ManifestCache::instance();
    DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::ManifestFetch);

    if (cache->isManifestValid())
    {
        QString versionUrl = cache->getVersionUrl(m_currentVersionId);
        if (!versionUrl.isEmpty())
        {
            m_currentPhase = DownloadPhase::VersionJsonFetch;
            fetchVersionJson();
            return;
        }
    }

    emit statusChanged(tr("正在获取版本清单..."));
    updateTaskStatus(tr("正在获取版本清单"));

    // 双源：当前设置的源优先，另一个作回退（MultiMC 语义：候选列表逐个尝试）
    const QString officialUrl =
        QStringLiteral("https://launchermeta.mojang.com/mc/game/version_manifest_v2.json");
    const QString bmclUrl =
        QStringLiteral("https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json");
    QStringList urls;
    if (m_downloadSource == BMCL)
        urls << bmclUrl << officialUrl;
    else
        urls << officialUrl << bmclUrl;

    fetchJsonCached(urls, QStringLiteral("version_manifest_v2.json"),
        [this](bool ok, const QByteArray &data) {
            if (!ok || data.isEmpty())
            {
                if (m_isDownloading && !m_isPaused)
                    failDownload(tr("获取版本清单失败"));
                return;
            }

            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || doc.isNull())
            {
                failDownload(tr("版本清单解析失败: %1").arg(parseErr.errorString()));
                return;
            }
            QJsonObject rootObj = doc.object();

            if (!rootObj.contains("versions"))
            {
                failDownload(tr("版本清单格式错误"));
                return;
            }

            ManifestCache::instance()->setManifest(rootObj);

            QString versionUrl = ManifestCache::instance()->getVersionUrl(m_currentVersionId);
            if (versionUrl.isEmpty())
            {
                failDownload(QStringLiteral("找不到版本 %1").arg(m_currentVersionId));
                return;
            }

            m_currentPhase = DownloadPhase::VersionJsonFetch;
            fetchVersionJson();
        });
}

void VersionDownloader::fetchVersionJson()
{
    ManifestCache *cache = ManifestCache::instance();
    DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::VersionJson);

    QJsonObject versionManifest = cache->getVersionJson(m_currentVersionId);
    if (!versionManifest.isEmpty())
    {
        m_currentVersionManifest = versionManifest;
        m_currentPhase = DownloadPhase::ClientJar;
        downloadClientJar();
        return;
    }

    emit statusChanged(tr("正在获取版本详情..."));
    updateTaskStatus(tr("正在获取版本详情"));

    QString versionUrl = cache->getVersionUrl(m_currentVersionId);
    QString bmclUrl = versionUrl;
    bmclUrl.replace(QStringLiteral("https://launchermeta.mojang.com"),
                    QStringLiteral("https://bmclapi2.bangbang93.com"));
    bmclUrl.replace(QStringLiteral("https://piston-meta.mojang.com"),
                    QStringLiteral("https://bmclapi2.bangbang93.com"));

    QStringList urls;
    if (m_downloadSource == BMCL)
        urls << bmclUrl << versionUrl;
    else
        urls << versionUrl << bmclUrl;
    urls.removeDuplicates();

    fetchJsonCached(urls, QStringLiteral("versions/") + m_currentVersionId
                        + QStringLiteral(".json"),
        [this](bool ok, const QByteArray &data) {
            if (!ok || data.isEmpty())
            {
                if (m_isDownloading && !m_isPaused)
                    failDownload(tr("获取版本详情失败"));
                return;
            }

            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || doc.isNull())
            {
                failDownload(tr("版本 JSON 解析失败: %1").arg(parseErr.errorString()));
                return;
            }
            QJsonObject versionManifest = doc.object();

            m_currentVersionManifest = versionManifest;
            ManifestCache::instance()->setVersionJson(m_currentVersionId, versionManifest);

            m_currentPhase = DownloadPhase::ClientJar;
            downloadClientJar();
        });
}

void VersionDownloader::downloadClientJar()
{
    DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::ClientJar);

    QString versionPath = m_currentInstancePath;

    QDir versionDir(versionPath);
    if (!versionDir.exists())
    {
        versionDir.mkpath(".");
    }

    QString jsonFileName = QFileInfo(versionPath).fileName() + ".json";
    QString versionJsonPath = versionPath + "/" + jsonFileName;
    QFile versionJsonFile(versionJsonPath);
    if (versionJsonFile.open(QIODevice::WriteOnly))
    {
        QJsonDocument doc(m_currentVersionManifest);
        versionJsonFile.write(doc.toJson());
        versionJsonFile.close();
    }

    m_totalBytes = 0;
    m_downloadedBytes = 0;
    calculateTotalSize(m_currentVersionManifest);
    addAssetTotalSize(m_currentVersionManifest);

    qint64 clientSize = 0;
    QString clientSha1;
    if (m_currentVersionManifest.contains("downloads"))
    {
        QJsonObject downloads = m_currentVersionManifest["downloads"].toObject();
        if (downloads.contains("client"))
        {
            QJsonObject client = downloads["client"].toObject();
            clientSize = client["size"].toVariant().toLongLong();
            clientSha1 = client["sha1"].toString();
        }
    }
    m_clientJarSize = clientSize;

    // 参考 PCL2：先尝试复用本机其他实例的同版本客户端 JAR（硬链接/复制），失败再走网络下载
    if (clientSize > 0)
    {
        QString clientJarPath = versionPath + "/" + m_currentVersionId + ".jar";
        startClientJarReuseScan(clientJarPath, clientSize, clientSha1);
        return;
    }

    // manifest 未提供大小时无法校验复用，维持旧行为直接下载
    downloadClientJarFromNetwork();
}

void VersionDownloader::downloadClientJarFromNetwork()
{
    QString clientUrl = getClientDownloadUrl(m_currentVersionManifest);
    if (clientUrl.isEmpty())
    {
        failDownload(tr("无法获取客户端下载地址"));
        return;
    }

    // BMCL 源保留官方地址作为兜底：镜像 302 的后端可能故障，官方地址仍在清单里
    m_clientJarFallbackUrl.clear();
    if (m_downloadSource == BMCL && clientUrl.contains("bmclapi2.bangbang93.com"))
    {
        m_clientJarFallbackUrl = clientUrl;
        m_clientJarFallbackUrl.replace("https://bmclapi2.bangbang93.com/v1/objects",
                                       "https://piston-data.mojang.com/v1/objects");
        m_clientJarFallbackUrl.replace("https://bmclapi2.bangbang93.com/objects",
                                       "https://piston-data.mojang.com/v1/objects");
    }
    m_clientJarSha1.clear();
    if (m_currentVersionManifest.contains("downloads"))
    {
        QJsonObject downloads = m_currentVersionManifest["downloads"].toObject();
        if (downloads.contains("client"))
            m_clientJarSha1 = downloads["client"].toObject()["sha1"].toString();
    }

    QString clientJarPath = m_currentInstancePath + "/" + m_currentVersionId + ".jar";

    emit statusChanged(tr("正在下载客户端 JAR 文件..."));

    // MultiMC net 层传输：Download + FileSink + SHA1 校验，
    // BMCL 源携带官方地址作回退，无需再分段
    downloadSingleFile(clientUrl, clientJarPath, m_clientJarSize, m_clientJarSha1,
                       m_clientJarFallbackUrl, true);
}

void VersionDownloader::startClientJarReuseScan(const QString &clientJarPath, qint64 clientSize,
                                                const QString &expectedSha1)
{
    const int scanId = m_reuseScanId;

    // 候选：其他实例/共享目录下的同版本 JAR（<...>/<versionId>.jar）
    QStringList candidates;
    const QStringList bases = buildReuseBases();
    candidates.reserve(bases.size());
    for (const QString &base : bases)
    {
        candidates << base + "/" + m_currentVersionId + ".jar";
    }

    emit statusChanged(tr("正在检查本地已有文件（复用）..."));
    updateTaskStatus(tr("正在检查本地已有文件"));

    QThreadPool::globalInstance()->start([this, scanId, clientJarPath, clientSize,
                                          expectedSha1, candidates]() {
        // 客户端 JAR 是启动的关键文件且体积有限，复用前按 SHA1 强校验
        bool reused = FileReuse::tryFillFromFilesystem(clientJarPath, clientSize,
                                                       candidates, expectedSha1);
        QMetaObject::invokeMethod(this, [this, scanId, reused]() {
            applyClientJarReuseResult(reused, scanId);
        }, Qt::QueuedConnection);
    });
}

void VersionDownloader::applyClientJarReuseResult(bool reused, int scanId)
{
    if (scanId != m_reuseScanId || !m_isDownloading) return;
    if (m_currentPhase != DownloadPhase::ClientJar) return;

    if (reused)
    {
        m_downloadedBytes += m_clientJarSize;
        updateTaskProgress(m_downloadedBytes, m_totalBytes);
        emit statusChanged(tr("已复用本机同版本文件，跳过客户端 JAR 下载"));
        proceedAfterClientJar();
        return;
    }

    // 暂停状态下不发起网络下载，恢复时由 resumeDownload 的 ClientJar 分支接管
    if (m_isPaused) return;
    downloadClientJarFromNetwork();
}

void VersionDownloader::proceedAfterClientJar()
{
    DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::Libraries);
    m_currentPhase = DownloadPhase::Libraries;
    downloadLogConfigs();
    startLibrariesDownload();
}

// ── 文件传输：MultiMC net 层（Download + FileSink + ChecksumValidator）──
// 传输、重试（含退避）、镜像回退、完整性校验全部由 Download/NetJob 负责；
// 这里只负责并发槽位记账、字节进度聚合与阶段推进
void VersionDownloader::downloadSingleFile(const QString &url, const QString &filePath,
                                           qint64 fileSize, const QString &expectedHash,
                                           const QString &fallbackUrl, bool isClientJar)
{
    if (m_isPaused || !m_isDownloading) return;

    NetFileParams params;
    params.url = url;
    params.filePath = filePath;
    params.fileSize = fileSize;
    params.expectedHash = expectedHash;
    params.fallbackUrl = fallbackUrl;
    params.isClientJar = isClientJar;

    auto dl = Net::Download::makeFile(QUrl(url), filePath);
    if (!fallbackUrl.isEmpty() && fallbackUrl != url)
        dl->addUrlFallback(QUrl(fallbackUrl));
    if (!expectedHash.isEmpty())
    {
        dl->addValidator(new Net::ChecksumValidator(
            QCryptographicHash::Sha1, QByteArray::fromHex(expectedHash.toLatin1())));
    }

    connect(dl.get(), &NetAction::netActionProgress, this,
            [this, filePath](int, qint64 bytesReceived, qint64) {
        // 单文件路径（客户端 JAR / 日志配置）：按字节直接汇报
        if (filePath.endsWith(QStringLiteral(".jar")))
            updateTaskProgress(m_downloadedBytes + bytesReceived, m_totalBytes);
    });

    // 单文件一个 NetJob：MultiMC 语义下由 NetJob 负责重试（3 次、带退避）
    auto job = NetJob::Ptr(new NetJob(QFileInfo(filePath).fileName(),
                                      Net::networkAccessManager()));
    job->addNetAction(dl);

    connect(job.get(), &Task::finished, this, [this, params, job]() {
        m_fileJobs.removeAll(job);
        if (job->wasSuccessful())
            onNetFileSucceeded(params);
        else if (job->wasAborted())
            onNetFileAborted(params);
        else
            onNetFileFailed(params);
    });

    m_fileJobs.append(job);
    job->start();
}

// 下载版本清单 logging.client 指定的日志配置文件（log4j2 配置，SHA1 校验）
void VersionDownloader::downloadLogConfigs()
{
    QString clientLogUrl;
    QString clientLogId;
    QString clientLogSha1;
    qint64 clientLogSize = 0;
    if (m_currentVersionManifest.contains("logging"))
    {
        QJsonObject logging = m_currentVersionManifest["logging"].toObject();
        if (logging.contains("client"))
        {
            QJsonObject client = logging["client"].toObject();
            QJsonObject file = client["file"].toObject();
            clientLogId = file["id"].toString();
            clientLogUrl = file["url"].toString();
            clientLogSha1 = file["sha1"].toString();
            clientLogSize = file["size"].toVariant().toLongLong();
        }
    }
    if (clientLogUrl.isEmpty() || clientLogId.isEmpty())
        return; // 该版本没有日志配置

    QString logConfigPath = m_currentInstancePath + "/" + clientLogId;
    if (QFile::exists(logConfigPath))
        return;

    // 该文件不在 calculateTotalSize 的估算里，计入总量避免进度越界
    m_totalBytes += clientLogSize;
    downloadSingleFile(clientLogUrl, logConfigPath, clientLogSize, clientLogSha1,
                       QString(), false);
}

void VersionDownloader::onNetFileSucceeded(const NetFileParams &params)
{
    addCompletedFile(params.filePath, params.fileSize);

    // 暂停中：只记账，不推进阶段（恢复时重扫磁盘续传）
    if (m_isPaused) return;
    if (!m_isDownloading) return;

    // 百分比由阶段作业的聚合进度负责，这里不按字节重算，避免进度回跳

    if (params.isClientJar)
    {
        proceedAfterClientJar();
    }
    // 其余单文件（日志配置）不影响阶段推进：所属阶段的 NetJob 自行聚合进度
}

void VersionDownloader::onNetFileFailed(const NetFileParams &params)
{
    if (m_isPaused || !m_isDownloading) return;

    if (params.isClientJar)
    {
        failDownload(tr("下载失败 (已重试多次): %1").arg(QFileInfo(params.filePath).fileName()));
        return;
    }
    // 日志配置属于锦上添花的文件：失败不阻塞安装，仅提示
    emit statusChanged(tr("日志配置下载失败，已跳过：%1")
                           .arg(QFileInfo(params.filePath).fileName()));
}

void VersionDownloader::onNetFileAborted(const NetFileParams &params)
{
    Q_UNUSED(params);
    // 暂停/取消路径：abortAllActiveReplies 与恢复重扫已完成清理
}

// ── 阶段作业：MultiMC 进度逻辑 ──
// 库/资源各自装进一个 NetJob：NetJob 按文件数等权聚合（每个文件 0~1000），
// 在途文件按字节占比折算，进度由 NetJob 自己维护，这里只做权重包络映射
void VersionDownloader::startPhaseJob(const QList<PendingDownload> &files, DownloadPhase phase)
{
    if (files.isEmpty())
        return; // 调用方已处理空列表

    m_phaseJobPhase = phase;

    auto job = NetJob::Ptr(new NetJob(phase == DownloadPhase::Libraries
                                          ? tr("下载依赖库") : tr("下载资源文件"),
                                      Net::networkAccessManager()));

    for (const PendingDownload &dl : files)
    {
        auto dlAction = Net::Download::makeFile(QUrl(dl.url), dl.filePath);
        if (!dl.fallbackUrl.isEmpty() && dl.fallbackUrl != dl.url)
            dlAction->addUrlFallback(QUrl(dl.fallbackUrl));
        if (!dl.expectedHash.isEmpty())
        {
            dlAction->addValidator(new Net::ChecksumValidator(
                QCryptographicHash::Sha1, QByteArray::fromHex(dl.expectedHash.toLatin1())));
        }

        const QString fileName = QFileInfo(dl.filePath).fileName();
        NetAction *actionPtr = dlAction.get();
        connect(actionPtr, &NetAction::netActionProgress, this,
                [this, actionPtr, fileName, dl](int, qint64 received, qint64) {
            m_phaseActionReceived[actionPtr] = received;
            DownloadTaskManager::instance()->updateTaskFileProgress(
                m_currentTaskId, fileName, received, dl.fileSize);
            reportPhaseProgress(false);
        });
        connect(actionPtr, &NetAction::succeeded, this, [this, actionPtr, dl](int) {
            m_phaseActionReceived.remove(actionPtr);
            addCompletedFile(dl.filePath, dl.fileSize);
            reportPhaseProgress(true);
        });
        connect(actionPtr, &NetAction::failed, this, [this, actionPtr](int) {
            m_phaseActionReceived.remove(actionPtr);
        });

        job->addNetAction(dlAction);
    }

    connect(job.get(), &Task::progress, this,
            [this](qint64, qint64) { reportPhaseProgress(false); });
    connect(job.get(), &Task::finished, this, [this, job]() {
        onPhaseJobFinished(job);
    });

    m_phaseJob = job;
    job->start();
}

void VersionDownloader::addCompletedFile(const QString &filePath, qint64 fileSize)
{
    if (m_completedFiles.contains(filePath))
        return;
    m_completedFiles.insert(filePath);
    m_downloadedBytes += fileSize;
}

void VersionDownloader::reportPhaseProgress(bool force)
{
    if (!m_phaseJob || m_currentTaskId.isEmpty())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && now - m_lastProgressReportMs < 150)
        return;
    m_lastProgressReportMs = now;

    const qint64 current = m_phaseJob->getProgress();
    const qint64 total = m_phaseJob->getTotalProgress();
    if (total <= 0)
        return;

    const double jobPct = qBound(0.0, current * 100.0 / total, 100.0);

    // 阶段权重包络（HMCL 式）：依赖库 25~60、资源文件 60~80
    int startPercent = 0;
    int weight = 0;
    if (m_phaseJobPhase == DownloadPhase::Libraries)   { startPercent = 25; weight = 35; }
    else if (m_phaseJobPhase == DownloadPhase::Assets) { startPercent = 60; weight = 20; }
    else return;

    const int global = qBound(0, startPercent + static_cast<int>(weight * jobPct / 100.0), 100);

    qint64 inFlight = 0;
    for (auto it = m_phaseActionReceived.constBegin();
         it != m_phaseActionReceived.constEnd(); ++it)
    {
        inFlight += it.value();
    }

    DownloadTaskManager::instance()->updateTaskProgress(
        m_currentTaskId, global, m_downloadedBytes + inFlight, m_totalBytes);
}

void VersionDownloader::onPhaseJobFinished(const NetJob::Ptr &job)
{
    if (m_phaseJob == job)
        m_phaseJob.reset();
    m_phaseActionReceived.clear();

    if (!m_isDownloading)
        return; // 已取消：abortAllActiveReplies 已清理
    if (job->wasAborted())
        return; // 暂停：恢复时重扫磁盘续传

    if (!job->wasSuccessful())
    {
        const QStringList failed = job->getFailedFiles();
        failDownload(tr("下载失败 (已重试多次): %1")
                         .arg(failed.isEmpty() ? tr("未知文件")
                                               : QFileInfo(failed.first()).fileName()));
        return;
    }

    if (m_phaseJobPhase == DownloadPhase::Libraries)
    {
        DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::Assets);
        m_currentPhase = DownloadPhase::Assets;
        startAssetsDownload();
    }
    else if (m_phaseJobPhase == DownloadPhase::Assets)
    {
        completeDownload();
    }
}


void VersionDownloader::startLibrariesDownload()
{
    QList<PendingDownload> all;

    if (m_currentVersionManifest.contains("libraries"))
    {
        QJsonArray libraries = m_currentVersionManifest["libraries"].toArray();
        QString basePath;
        if (SettingsManager::instance()->isVersionIsolationEnabled())
        {
            basePath = m_currentInstancePath;
        }
        else
        {
            // 非隔离模式：共享 libraries 目录在根路径下
            basePath = QDir::cleanPath(m_currentInstancePath + "/../..");
        }
        QString librariesPath = basePath + "/libraries";

        QDir libDir(librariesPath);
        if (!libDir.exists())
        {
            libDir.mkpath(".");
        }

        for (const QJsonValue &value : libraries)
        {
            QJsonObject library = value.toObject();
            if (library.contains("downloads"))
            {
                QJsonObject downloads = library["downloads"].toObject();
                if (downloads.contains("artifact"))
                {
                    QJsonObject artifact = downloads["artifact"].toObject();
                    PendingDownload dl;
                    dl.url = artifact["url"].toString();
                    dl.filePath = librariesPath + "/" + artifact["path"].toString();
                    dl.fileSize = artifact["size"].toVariant().toLongLong();
                    dl.expectedHash = artifact["sha1"].toString();

                    if (m_downloadSource == BMCL)
                    {
                        // 保留官方地址：BMCL 的镜像后端可能单个故障（403/429），用于兜底重试
                        dl.fallbackUrl = dl.url;
                        dl.url.replace("https://libraries.minecraft.net", "https://bmclapi2.bangbang93.com/maven");
                    }

                    all.append(dl);
                }
            }
        }

        // 在后台线程完成"已在位校验 + 跨实例复用"，之后仅对缺失文件发起网络下载
        startLibraryScan(all, librariesPath);
        return;
    }

    DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::Assets);
    m_currentPhase = DownloadPhase::Assets;
    startAssetsDownload();
}

void VersionDownloader::startLibraryScan(QList<PendingDownload> all, const QString &librariesPath)
{
    if (all.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::Assets);
        m_currentPhase = DownloadPhase::Assets;
        startAssetsDownload();
        return;
    }

    const int scanId = m_reuseScanId;
    const QStringList bases = buildReuseBases();

    emit statusChanged(tr("正在检查本地已有文件（复用）..."));
    updateTaskStatus(tr("正在检查本地已有文件"));

    const QSet<QString> completedSnapshot = m_completedFiles;
    QThreadPool::globalInstance()->start([this, scanId, all, bases, librariesPath, completedSnapshot]() {
        QList<PendingDownload> remaining;
        qint64 reusedBytes = 0;

        for (const PendingDownload &dl : all)
        {
            // 本次任务已完成（暂停恢复重扫）：字节已计入，直接跳过
            if (completedSnapshot.contains(dl.filePath))
                continue;
            // 相对 libraries 目录的路径（如 com/google/guava/.../guava.jar）
            QString rel = dl.filePath;
            if (rel.startsWith(librariesPath + "/"))
                rel = rel.mid(librariesPath.size() + 1);

            // 候选：其他实例目录及共享目录中的同路径依赖库
            QStringList candidates;
            candidates.reserve(bases.size());
            for (const QString &base : bases)
            {
                candidates << base + "/libraries/" + rel;
            }

            if (FileReuse::tryFillFromFilesystem(dl.filePath, dl.fileSize, candidates))
                reusedBytes += dl.fileSize;
            else
                remaining.append(dl);
        }

        QMetaObject::invokeMethod(this, [this, scanId, remaining, reusedBytes]() {
            applyLibraryScanResult(remaining, reusedBytes, scanId);
        }, Qt::QueuedConnection);
    });
}

void VersionDownloader::applyLibraryScanResult(QList<PendingDownload> remaining, qint64 reusedBytes, int scanId)
{
    if (scanId != m_reuseScanId || !m_isDownloading) return;
    if (m_currentPhase != DownloadPhase::Libraries) return;
    if (m_isPaused) return; // 恢复时重新进入阶段（重扫磁盘）

    if (reusedBytes > 0)
    {
        m_downloadedBytes += reusedBytes;
        updateTaskProgress(m_downloadedBytes, m_totalBytes);
    }

    if (remaining.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::Assets);
        m_currentPhase = DownloadPhase::Assets;
        startAssetsDownload();
        return;
    }

    emit statusChanged(tr("正在下载 %1 个依赖库...").arg(remaining.size()));
    startPhaseJob(remaining, DownloadPhase::Libraries);
}

void VersionDownloader::startAssetsDownload()
{
    if (!m_currentVersionManifest.contains("assetIndex"))
    {
        completeDownload();
        return;
    }

    m_currentPhase = DownloadPhase::Assets;
    DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::Assets);
    fetchAssetIndex();
}

void VersionDownloader::fetchAssetIndex()
{
    QJsonObject assetIndex = m_currentVersionManifest["assetIndex"].toObject();
    QString assetId = assetIndex["id"].toString();

    ManifestCache *cache = ManifestCache::instance();
    QJsonObject rootObj = cache->getAssetIndex(assetId);

    // 缓存命中且包含有效对象表 → 直接进入后台扫描，无需重新拉取索引
    if (!rootObj.isEmpty() && rootObj.contains("objects")
        && !rootObj["objects"].toObject().isEmpty())
    {
        // 缓存命中也要把索引写到 assets/indexes/，否则启动器在启动时还会再下载一次
        startAssetsFromIndex(rootObj["objects"].toObject(), assetId,
                             QJsonDocument(rootObj).toJson(QJsonDocument::Compact));
        return;
    }

    emit statusChanged(tr("正在获取资源索引..."));
    updateTaskStatus(tr("正在获取资源索引"));

    QString assetUrl = assetIndex["url"].toString();
    QString bmclUrl = assetUrl;
    bmclUrl.replace(QStringLiteral("https://launchermeta.mojang.com"),
                    QStringLiteral("https://bmclapi2.bangbang93.com"));
    bmclUrl.replace(QStringLiteral("https://piston-meta.mojang.com"),
                    QStringLiteral("https://bmclapi2.bangbang93.com"));

    QStringList urls;
    if (m_downloadSource == BMCL)
        urls << bmclUrl << assetUrl;
    else
        urls << assetUrl << bmclUrl;
    urls.removeDuplicates();

    fetchJsonCached(urls, QStringLiteral("asset_indexes/") + assetId
                        + QStringLiteral(".json"),
        [this, assetId](bool ok, const QByteArray &data) {
            if (!ok || data.isEmpty())
            {
                if (m_isDownloading && !m_isPaused)
                    failDownload(tr("获取资源索引失败"));
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonObject rootObj = doc.object();

            ManifestCache::instance()->setAssetIndex(assetId, rootObj);

            if (!rootObj.contains("objects"))
            {
                failDownload(tr("资源索引格式错误"));
                return;
            }

            startAssetsFromIndex(rootObj["objects"].toObject(), assetId, data);
        });
}

void VersionDownloader::startAssetsFromIndex(const QJsonObject &objects, const QString &assetId,
                                             const QByteArray &indexData)
{
    QString basePath;
    if (SettingsManager::instance()->isVersionIsolationEnabled())
    {
        basePath = m_currentInstancePath;
    }
    else
    {
        basePath = QDir::cleanPath(m_currentInstancePath + "/../..");
    }
    QString assetsPath = basePath + "/assets";

    // 索引 JSON 落盘：缓存命中路径原先不写文件，会导致启动时资产索引缺失而重新下载
    if (!indexData.isEmpty())
    {
        QString assetIndexPath = assetsPath + "/indexes/" + assetId + ".json";
        QDir().mkpath(QFileInfo(assetIndexPath).absolutePath());
        QFile file(assetIndexPath);
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(indexData);
            file.close();
        }
    }

    QList<PendingDownload> all;
    all.reserve(objects.size());
    for (auto it = objects.begin(); it != objects.end(); ++it)
    {
        QJsonObject asset = it.value().toObject();
        QString hash = asset["hash"].toString();
        // 跳过无效 hash 的条目，避免拼出错误路径导致整个任务 404 失败
        if (hash.length() < 2)
            continue;

        QString hashPrefix = hash.left(2);

        PendingDownload dl;
        if (m_downloadSource == BMCL)
        {
            // 保留官方地址：BMCL 的镜像后端可能单个故障（403/429），用于兜底重试
            dl.url = QString("https://bmclapi2.bangbang93.com/assets/%1/%2").arg(hashPrefix).arg(hash);
            dl.fallbackUrl = QString("https://resources.download.minecraft.net/%1/%2").arg(hashPrefix).arg(hash);
        }
        else
        {
            dl.url = QString("https://resources.download.minecraft.net/%1/%2").arg(hashPrefix).arg(hash);
        }
        dl.filePath = assetsPath + "/objects/" + hashPrefix + "/" + hash;
        dl.fileSize = asset["size"].toVariant().toLongLong();
        dl.expectedHash = hash;
        all.append(dl);
    }

    // 在后台线程完成"已在位校验 + 跨实例复用"，之后仅对缺失文件发起网络下载
    startAssetScan(all, assetsPath);
}

void VersionDownloader::startAssetScan(QList<PendingDownload> all, const QString &assetsPath)
{
    if (all.isEmpty())
    {
        completeDownload();
        return;
    }

    const int scanId = m_reuseScanId;
    const QStringList bases = buildReuseBases();

    emit statusChanged(tr("正在检查本地已有文件（复用）..."));
    updateTaskStatus(tr("正在检查本地已有文件"));

    const QSet<QString> completedSnapshot = m_completedFiles;
    QThreadPool::globalInstance()->start([this, scanId, all, bases, assetsPath, completedSnapshot]() {
        QList<PendingDownload> remaining;
        qint64 reusedBytes = 0;

        for (const PendingDownload &dl : all)
        {
            // 本次任务已完成（暂停恢复重扫）：字节已计入，直接跳过
            if (completedSnapshot.contains(dl.filePath))
                continue;
            // 资源以内容寻址存储：objects/<hash前2位>/<hash>，路径即内容标识，
            // 大小一致即可视为同内容（参考 PCL2 的按大小跳过策略）
            QString rel = dl.filePath;
            if (rel.startsWith(assetsPath + "/"))
                rel = rel.mid(assetsPath.size() + 1);

            QStringList candidates;
            candidates.reserve(bases.size());
            for (const QString &base : bases)
            {
                candidates << base + "/assets/" + rel;
            }

            if (FileReuse::tryFillFromFilesystem(dl.filePath, dl.fileSize, candidates))
                reusedBytes += dl.fileSize;
            else
                remaining.append(dl);
        }

        QMetaObject::invokeMethod(this, [this, scanId, remaining, reusedBytes]() {
            applyAssetScanResult(remaining, reusedBytes, scanId);
        }, Qt::QueuedConnection);
    });
}

void VersionDownloader::applyAssetScanResult(QList<PendingDownload> remaining, qint64 reusedBytes, int scanId)
{
    if (scanId != m_reuseScanId || !m_isDownloading) return;
    if (m_currentPhase != DownloadPhase::Assets) return;
    if (m_isPaused) return; // 恢复时重新进入阶段（重扫磁盘）

    if (reusedBytes > 0)
    {
        m_downloadedBytes += reusedBytes;
        updateTaskProgress(m_downloadedBytes, m_totalBytes);
    }
    if (remaining.isEmpty())
    {
        completeDownload();
        return;
    }

    emit statusChanged(tr("正在下载 %1 个资源文件...").arg(remaining.size()));
    startPhaseJob(remaining, DownloadPhase::Assets);
}

void VersionDownloader::completeDownload()
{
    m_currentPhase = DownloadPhase::Completed;
    // 先复位下载状态再广播完成：完成回调里若同步发起下一次安装，
    // downloadVanillaTo 的 m_isDownloading 守卫会误吞新任务
    m_isDownloading = false;
    emit downloadCompleted(m_currentVersionId, getActualVersionPath());
    DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Completed, tr("下载完成"));
    DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::Completed, 100);
}

QStringList VersionDownloader::buildReuseBases() const
{
    // 可复用文件的搜索范围（参考 HMCL/PCL2 的共享目录 + 实例目录布局）：
    //  - 所有已注册实例文件夹根：共享 libraries/assets 所在（非隔离实例与外部安装器写入处）
    //  - 每个根下 versions/<实例>/：版本隔离模式下实例自带的 libraries/assets
    // 目标实例自身被排除，避免把目标文件本身当作复用来源
    QStringList roots;
    const QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    for (const InstanceFolderInfo &folder : folders)
    {
        if (!folder.path.isEmpty())
            roots << QDir(folder.path).absolutePath();
    }
    if (roots.isEmpty())
    {
        // 兜底：按目标实例的 <root>/versions/<name> 布局向上推导
        roots << QDir::cleanPath(m_currentInstancePath + "/../..");
        roots << QDir::cleanPath(m_currentInstancePath + "/..");
    }
    roots.removeDuplicates();

    const QString targetAbs = QDir(m_currentInstancePath).absolutePath();

    QStringList bases;
    for (const QString &root : roots)
    {
        bases << root;
        QDir versionsDir(root + "/versions");
        const QStringList instances = versionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &inst : instances)
        {
            const QString instDir = versionsDir.absoluteFilePath(inst);
            if (QDir(instDir).absolutePath() == targetAbs)
                continue;
            bases << instDir;
        }
    }
    bases.removeDuplicates();
    return bases;
}

void VersionDownloader::setDownloadSource(DownloadSource source)
{
    m_downloadSource = source;
}

VersionDownloader::DownloadSource VersionDownloader::downloadSource() const
{
    return m_downloadSource;
}

QString VersionDownloader::getClientDownloadUrl(const QJsonObject &versionManifest)
{
    if (!versionManifest.contains("downloads"))
    {
        return QString();
    }

    QJsonObject downloads = versionManifest["downloads"].toObject();
    if (!downloads.contains("client"))
    {
        return QString();
    }

    QJsonObject client = downloads["client"].toObject();
    QString url = client["url"].toString();

    if (m_downloadSource == BMCL)
    {
        // BMCLAPI 的客户端镜像端点保留 /v1 路径段（/objects/<hash>/client.jar 会 429）
        url.replace("https://launcher.mojang.com/v1/objects", "https://bmclapi2.bangbang93.com/v1/objects");
        url.replace("https://piston-data.mojang.com/v1/objects", "https://bmclapi2.bangbang93.com/v1/objects");
    }

    return url;
}

/* 依赖库/资源的并发下载上限：BMCLAPI 对单 IP 并发限流严格（429），
 * 用低并发换成功率；官方源维持原有高并发 */
void VersionDownloader::calculateTotalSize(const QJsonObject &versionManifest)
{
    if (versionManifest.contains("downloads"))
    {
        QJsonObject downloads = versionManifest["downloads"].toObject();
        if (downloads.contains("client"))
        {
            m_totalBytes += downloads["client"].toObject()["size"].toVariant().toLongLong();
        }
    }

    if (versionManifest.contains("libraries"))
    {
        QJsonArray libraries = versionManifest["libraries"].toArray();
        for (const QJsonValue &value : libraries)
        {
            QJsonObject library = value.toObject();
            if (library.contains("downloads"))
            {
                QJsonObject downloads = library["downloads"].toObject();
                if (downloads.contains("artifact"))
                {
                    m_totalBytes += downloads["artifact"].toObject()["size"].toVariant().toLongLong();
                }
            }
        }
    }
}

void VersionDownloader::addAssetTotalSize(const QJsonObject &versionManifest)
{
    if (!versionManifest.contains("assetIndex"))
    {
        return;
    }

    QJsonObject assetIndex = versionManifest["assetIndex"].toObject();
    QString assetId = assetIndex["id"].toString();

    ManifestCache *cache = ManifestCache::instance();
    QJsonObject rootObj = cache->getAssetIndex(assetId);

    if (!rootObj.isEmpty() && rootObj.contains("objects"))
    {
        QJsonObject objects = rootObj["objects"].toObject();
        for (auto it = objects.begin(); it != objects.end(); ++it)
        {
            QJsonObject asset = it.value().toObject();
            m_totalBytes += asset["size"].toVariant().toLongLong();
        }
    }
}

void VersionDownloader::updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (m_currentTaskId.isEmpty() || bytesTotal <= 0) return;

    int percent = qBound(0, static_cast<int>((bytesReceived * 100) / bytesTotal), 100);
    DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, percent);
}

QString VersionDownloader::getActualVersionPath() const
{
    return m_currentInstancePath;
}

void VersionDownloader::updateTaskStatus(const QString &status)
{
    if (m_currentTaskId.isEmpty()) return;
    DownloadTaskManager::instance()->updateTaskStep(m_currentTaskId, status);
}