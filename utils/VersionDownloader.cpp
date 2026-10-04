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

#include "FileReuse.h"
#include "DownloadTaskManager.h"

/** 计算文件 SHA1 哈希，返回小写十六进制字符串 */
static QString calculateSha1(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha1);
    const qint64 chunkSize = 1024 * 1024; // 1MB chunks
    while (!file.atEnd())
    {
        hash.addData(file.read(chunkSize));
    }
    return QString(hash.result().toHex());
}

const int VersionDownloader::MAX_CONCURRENT_DOWNLOADS;
#include "SettingsManager.h"

#include "utils/ManifestCache.h"

VersionDownloader* VersionDownloader::m_instance = nullptr;
static QMutex s_vdInstanceMutex;

VersionDownloader::VersionDownloader()
    : m_networkManager(new QNetworkAccessManager(this))
    , m_downloadSource(Official)
    , m_currentPhase(DownloadPhase::Idle)
    , m_isDownloading(false)
    , m_isPaused(false)
    , m_totalBytes(0)
    , m_downloadedBytes(0)
    , m_clientJarSize(0)
    , m_activeDownloadCount(0)
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

    QString taskId = DownloadTaskManager::instance()->addTask(taskName, m_currentInstancePath, versionId, loaders);
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
    m_pendingDownloads.clear();
    m_pendingAssets.clear();

    abortAllActiveReplies();

    if (!m_currentTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->cancelTask(m_currentTaskId);
        m_currentTaskId.clear();
    }

    m_currentVersionId.clear();
    m_currentInstancePath.clear();
    m_currentPhase = DownloadPhase::Idle;

    // 清理多段下载残留的临时分段目录，避免取消后留下磁盘垃圾
    if (!m_tempSegmentDir.isEmpty()) {
        QDir(m_tempSegmentDir).removeRecursively();
        m_tempSegmentDir.clear();
    }

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
        // 分段下载客户端 JAR 时被暂停：分段已被丢弃，重新发起网络下载
        downloadClientJarFromNetwork();
        break;
    case DownloadPhase::Libraries:
        processLibraryQueue();
        break;
    case DownloadPhase::Assets:
        processAssetQueue();
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
    m_activeFileProgress.remove(reply);
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
    m_activeFileProgress.clear();
    m_activeDownloadCount = 0;
}

void VersionDownloader::failDownload(const QString &error)
{
    m_isDownloading = false;
    // 任务失败：在途复用扫描结果作废
    ++m_reuseScanId;
    abortAllActiveReplies();
    m_pendingDownloads.clear();
    m_pendingAssets.clear();

    if (!m_currentTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Failed, error);
    }

    emit downloadFailed(error);
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

    QString manifestUrl = getManifestUrl(m_currentVersionId);
    QNetworkRequest request;
    request.setUrl(QUrl(manifestUrl));
    QNetworkReply *reply = m_networkManager->get(request);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, &VersionDownloader::onManifestReplyFinished);
}

void VersionDownloader::onManifestReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (m_isPaused || !m_isDownloading)
    {
        cleanupActiveReply(reply);
        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        QString errMsg = tr("获取版本清单失败: %1").arg(reply->errorString());
        cleanupActiveReply(reply);
        failDownload(errMsg);
        return;
    }

    QByteArray data = reply->readAll();
    cleanupActiveReply(reply);

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
        failDownload(QString("找不到版本 %1").arg(m_currentVersionId));
        return;
    }

    m_currentPhase = DownloadPhase::VersionJsonFetch;
    fetchVersionJson();
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

    if (m_downloadSource == BMCL)
    {
        versionUrl.replace("https://launchermeta.mojang.com", "https://bmclapi2.bangbang93.com");
        versionUrl.replace("https://piston-meta.mojang.com", "https://bmclapi2.bangbang93.com");
    }

    QNetworkRequest request;
    request.setUrl(QUrl(versionUrl));
    QNetworkReply *reply = m_networkManager->get(request);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, &VersionDownloader::onVersionJsonReplyFinished);
}

void VersionDownloader::onVersionJsonReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (m_isPaused || !m_isDownloading)
    {
        cleanupActiveReply(reply);
        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        QString errMsg = tr("获取版本详情失败: %1").arg(reply->errorString());
        cleanupActiveReply(reply);
        failDownload(errMsg);
        return;
    }

    QByteArray data = reply->readAll();
    cleanupActiveReply(reply);

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

    QString clientJarPath = m_currentInstancePath + "/" + m_currentVersionId + ".jar";

    emit statusChanged(tr("正在下载客户端 JAR 文件..."));

    if (m_clientJarSize >= MIN_PARALLEL_FILE_SIZE)
    {
        downloadFileSegments(clientUrl, clientJarPath, m_clientJarSize);
        return;
    }

    m_activeDownloadCount++;
    QNetworkRequest jarRequest;
    jarRequest.setUrl(QUrl(clientUrl));
    jarRequest.setTransferTimeout(300000);
    QNetworkReply *jarReply = m_networkManager->get(jarRequest);
    m_activeReplies.append(jarReply);
    jarReply->setProperty("filePath", clientJarPath);
    jarReply->setProperty("fileSize", m_clientJarSize);
    jarReply->setProperty("isClientJar", true);
    connect(jarReply, &QNetworkReply::finished, this, &VersionDownloader::onLibraryFileFinished);
    connect(jarReply, &QNetworkReply::downloadProgress, this, &VersionDownloader::onClientJarProgress);
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

void VersionDownloader::onClientJarProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    Q_UNUSED(bytesTotal);
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) {
        m_activeFileProgress[reply] = bytesReceived;
    }
    qint64 totalInFlight = 0;
    for (auto it = m_activeFileProgress.begin(); it != m_activeFileProgress.end(); ++it)
    {
        totalInFlight += it.value();
    }
    updateTaskProgress(m_downloadedBytes + totalInFlight, m_totalBytes);
}

void VersionDownloader::downloadSingleFile(const QString &url, const QString &filePath,
                                            qint64 fileSize, const QString &expectedHash,
                                            int retryCount)
{
    if (m_isPaused || !m_isDownloading) return;

    int timeoutMs = (fileSize > 0 && fileSize < 1024 * 1024) ? 60000 : 300000;

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setTransferTimeout(timeoutMs);
    QNetworkReply *reply = m_networkManager->get(request);
    m_activeReplies.append(reply);

    reply->setProperty("url", url);
    reply->setProperty("filePath", filePath);
    reply->setProperty("fileSize", fileSize);
    reply->setProperty("retryCount", retryCount);
    reply->setProperty("expectedHash", expectedHash);

    connect(reply, &QNetworkReply::finished, this, &VersionDownloader::onLibraryFileFinished);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 bytesReceived, qint64 bytesTotal) {
        Q_UNUSED(bytesTotal);
        m_activeFileProgress[reply] = bytesReceived;
        qint64 totalInFlight = 0;
        for (auto it = m_activeFileProgress.begin(); it != m_activeFileProgress.end(); ++it)
        {
            totalInFlight += it.value();
        }
        updateTaskProgress(m_downloadedBytes + totalInFlight, m_totalBytes);
    });
}

void VersionDownloader::onLibraryFileFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    m_activeDownloadCount--;
    QString url = reply->property("url").toString();
    QString filePath = reply->property("filePath").toString();
    qint64 fileSize = reply->property("fileSize").toLongLong();
    int retryCount = reply->property("retryCount").toInt();
    bool isClientJar = reply->property("isClientJar").toBool();
    QString expectedHash = reply->property("expectedHash").toString();

    if (m_isPaused || !m_isDownloading)
    {
        // 暂停时把该文件重新排队，恢复后继续下载；取消时直接丢弃
        if (m_isPaused && !isClientJar)
        {
            PendingDownload pending;
            pending.url = url;
            pending.filePath = filePath;
            pending.fileSize = fileSize;
            pending.expectedHash = expectedHash;
            pending.retryCount = retryCount;
            if (m_currentPhase == DownloadPhase::Assets)
                m_pendingAssets.enqueue(pending);
            else
                m_pendingDownloads.enqueue(pending);
        }
        cleanupActiveReply(reply);
        if (m_isPaused) return;
        failDownload(tr("下载已取消"));
        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QNetworkReply::NetworkError err = reply->error();
        cleanupActiveReply(reply);

        if (statusCode == 404)
        {
            failDownload(tr("文件不存在 (404): %1").arg(QFileInfo(filePath).fileName()));
            return;
        }

        if (err == QNetworkReply::TimeoutError && retryCount < MAX_RETRY_COUNT)
        {
            if (retryCount > 0)
            {
                emit statusChanged(tr("下载超时，正在重试 %1 (第 %2 次)...")
                    .arg(QFileInfo(filePath).fileName())
                    .arg(retryCount + 1));
            }

            m_activeDownloadCount++;
            downloadSingleFile(url, filePath, fileSize, expectedHash, retryCount + 1);
            return;
        }

        if (retryCount < MAX_RETRY_COUNT)
        {
            if (retryCount > 0)
            {
                emit statusChanged(tr("正在重试下载 %1 (第 %2 次)...")
                    .arg(QFileInfo(filePath).fileName())
                    .arg(retryCount + 1));
            }

            m_activeDownloadCount++;
            downloadSingleFile(url, filePath, fileSize, expectedHash, retryCount + 1);
            return;
        }

        QString failMsg;
        if (err == QNetworkReply::TimeoutError)
        {
            failMsg = tr("下载超时 (已重试 %1 次): %2")
                .arg(MAX_RETRY_COUNT)
                .arg(QFileInfo(filePath).fileName());
        }
        else
        {
            failMsg = tr("下载失败 (已重试 %1 次): %2")
                .arg(MAX_RETRY_COUNT)
                .arg(QFileInfo(filePath).fileName());
        }
        failDownload(failMsg);
        return;
    }

    QByteArray data = reply->readAll();
    cleanupActiveReply(reply);

    QDir dir = QFileInfo(filePath).absoluteDir();
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        failDownload(tr("无法创建文件: %1").arg(filePath));
        return;
    }
    file.write(data);
    file.close();

    // 验证下载文件完整性（参考 HMCL FileDownloadTask IntegrityCheck）
    if (!expectedHash.isEmpty())
    {
        QString actualHash = calculateSha1(filePath);
        if (actualHash != expectedHash)
        {
            if (retryCount < 3)
            {
                PendingDownload pending;
                pending.url = url;
                pending.filePath = filePath;
                pending.fileSize = fileSize;
                pending.expectedHash = expectedHash;
                pending.retryCount = retryCount + 1;
                // 按当前阶段入队，避免跨阶段借用队列被后续扫描覆盖
                if (m_currentPhase == DownloadPhase::Assets)
                {
                    m_pendingAssets.enqueue(pending);
                    processAssetQueue();
                }
                else
                {
                    m_pendingDownloads.enqueue(pending);
                    processLibraryQueue();
                }
                return;
            }
        }
    }

    m_downloadedBytes += fileSize;
    updateTaskProgress(m_downloadedBytes, m_totalBytes);

    if (isClientJar)
    {
        proceedAfterClientJar();
        return;
    }

    if (m_currentPhase == DownloadPhase::Libraries)
    {
        processLibraryQueue();
    }
    else if (m_currentPhase == DownloadPhase::Assets)
    {
        processAssetQueue();
    }
}

void VersionDownloader::downloadFileSegments(const QString &url, const QString &filePath, qint64 fileSize)
{
    int maxSegments = qMax(2, SettingsManager::instance()->getDownloadThreadCount());
    qint64 segCount = qMin(
        static_cast<qint64>(maxSegments * 2),
        qMax(static_cast<qint64>(2), fileSize / (512 * 1024))
    );
    if (segCount > 128) segCount = 128;

    qint64 segmentSize = fileSize / segCount;
    m_tempSegmentDir = QFileInfo(filePath).absolutePath() + "/.tmpsegments_" + QFileInfo(filePath).fileName();

    for (int i = 0; i < segCount; ++i)
    {
        qint64 start = i * segmentSize;
        qint64 end = (i == segCount - 1) ? fileSize - 1 : (i + 1) * segmentSize - 1;
        QString tempPath = m_tempSegmentDir + QString("/seg_%1.tmp").arg(i);

        QNetworkRequest request;
        request.setUrl(QUrl(url));
        request.setRawHeader("Range", QString("bytes=%1-%2").arg(start).arg(end).toUtf8());
        request.setTransferTimeout(300000);
        // 参考 HMCL：强制 HTTP/1.1 避免 HTTP/2 + Range 协议错误
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

        QNetworkReply *reply = m_networkManager->get(request);
        m_activeReplies.append(reply);
        m_activeDownloadCount++;

        reply->setProperty("tempPath", tempPath);
        reply->setProperty("segmentIndex", i);
        reply->setProperty("totalSegments", static_cast<int>(segCount));
        reply->setProperty("finalPath", filePath);
        reply->setProperty("finalSize", fileSize);
        reply->setProperty("segmentStart", start);
        reply->setProperty("segmentEnd", end);
        reply->setProperty("retryCount", 0);

        connect(reply, &QNetworkReply::finished, this, &VersionDownloader::onSegmentReplyFinished);
    }
}

void VersionDownloader::onSegmentReplyFinished()
{
    QNetworkReply *segReply = qobject_cast<QNetworkReply*>(sender());
    if (!segReply) return;

    m_activeDownloadCount--;
    if (m_isPaused || !m_isDownloading)
    {
        cleanupActiveReply(segReply);
        if (m_isPaused) return;
        failDownload(tr("分段下载已取消"));
        return;
    }

    if (segReply->error() != QNetworkReply::NoError)
    {
        int retries = segReply->property("retryCount").toInt();
        if (retries < 3)
        {
            // 重试该分段
            qint64 start = segReply->property("segmentStart").toLongLong();
            qint64 end = segReply->property("segmentEnd").toLongLong();
            QString tempPath = segReply->property("tempPath").toString();
            int segIndex = segReply->property("segmentIndex").toInt();
            int totalSegs = segReply->property("totalSegments").toInt();
            QString finalPath = segReply->property("finalPath").toString();
            qint64 finalSize = segReply->property("finalSize").toLongLong();
            QString url = segReply->request().url().toString();

            cleanupActiveReply(segReply);
            m_activeDownloadCount++;

            QNetworkRequest retryRequest;
            retryRequest.setUrl(QUrl(url));
            retryRequest.setRawHeader("Range", QString("bytes=%1-%2").arg(start).arg(end).toUtf8());
            retryRequest.setTransferTimeout(300000);
            retryRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

            QNetworkReply *retryReply = m_networkManager->get(retryRequest);
            m_activeReplies.append(retryReply);

            retryReply->setProperty("tempPath", tempPath);
            retryReply->setProperty("segmentIndex", segIndex);
            retryReply->setProperty("totalSegments", totalSegs);
            retryReply->setProperty("finalPath", finalPath);
            retryReply->setProperty("finalSize", finalSize);
            retryReply->setProperty("segmentStart", start);
            retryReply->setProperty("segmentEnd", end);
            retryReply->setProperty("retryCount", retries + 1);

            connect(retryReply, &QNetworkReply::finished, this, &VersionDownloader::onSegmentReplyFinished);
            return;
        }

        // 重试耗尽，降级到单连接下载
        cleanupActiveReply(segReply);
        qWarning() << "分段下载重试耗尽，降级到单连接下载:" << segReply->request().url().toString();

        // 清理所有进行中的分段请求
        for (QNetworkReply *r : m_activeReplies)
        {
            r->disconnect();
            r->abort();
            r->deleteLater();
        }
        m_activeReplies.clear();
        m_activeDownloadCount = 0;

        // 清理临时分段目录
        QDir(m_tempSegmentDir).removeRecursively();

        // 降级到单连接下载
        QString finalPath = segReply->property("finalPath").toString();
        qint64 finalSize = segReply->property("finalSize").toLongLong();
        downloadSingleFile(segReply->request().url().toString(), finalPath, finalSize);
        return;
    }

    QString tempPath = segReply->property("tempPath").toString();
    QDir().mkpath(QFileInfo(tempPath).absolutePath());

    QFile segFile(tempPath);
    if (segFile.open(QIODevice::WriteOnly))
    {
        segFile.write(segReply->readAll());
        segFile.close();
    }

    cleanupActiveReply(segReply);

    QDir segDir(m_tempSegmentDir);
    int totalSegs = segReply->property("totalSegments").toInt();
    if (segDir.exists() && segDir.entryList(QDir::Files).count() >= totalSegs)
    {
        QString finalPath = segReply->property("finalPath").toString();
        qint64 finalSize = segReply->property("finalSize").toLongLong();
        QDir().mkpath(QFileInfo(finalPath).absolutePath());

        QFile outFile(finalPath);
        if (!outFile.open(QIODevice::WriteOnly))
        {
            QDir(m_tempSegmentDir).removeRecursively();
            failDownload(tr("无法创建输出文件: %1").arg(finalPath));
            return;
        }

        qint64 written = 0;
        for (int j = 0; j < totalSegs; ++j)
        {
            QFile sf(QString("%1/seg_%2.tmp").arg(m_tempSegmentDir).arg(j));
            if (sf.open(QIODevice::ReadOnly))
            {
                QByteArray d = sf.readAll();
                sf.close();
                outFile.write(d);
                written += d.size();
            }
        }
        outFile.close();
        QDir(m_tempSegmentDir).removeRecursively();

        m_downloadedBytes += finalSize;
        updateTaskProgress(m_downloadedBytes, m_totalBytes);

        proceedAfterClientJar();
    }
}

void VersionDownloader::downloadLogConfigs()
{
    // 参考 HMCL：下载 logging 配置文件 (log4j XML)
    QString versionJsonPath = m_currentInstancePath + "/" + QFileInfo(m_currentInstancePath).fileName() + ".json";
    QFile jsonFile(versionJsonPath);
    if (!jsonFile.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll());
    jsonFile.close();
    QJsonObject root = doc.object();

    if (!root.contains("logging") || !root["logging"].isObject()) return;
    QJsonObject logging = root["logging"].toObject();
    if (!logging.contains("client") || !logging["client"].isObject()) return;
    QJsonObject client = logging["client"].toObject();
    if (!client.contains("file") || !client["file"].isObject()) return;

    QJsonObject fileInfo = client["file"].toObject();
    QString fileId = fileInfo["id"].toString();
    QString fileUrl = fileInfo["url"].toString();

    if (fileId.isEmpty() || fileUrl.isEmpty()) return;

    QString logConfigsDir = m_currentInstancePath + "/log_configs";
    QDir().mkpath(logConfigsDir);
    QString destPath = logConfigsDir + "/" + fileId;

    // 已存在且大小匹配则跳过
    if (QFile::exists(destPath))
    {
        QFileInfo fi(destPath);
        qint64 expectedSize = fileInfo["size"].toVariant().toLongLong();
        if (fi.size() == expectedSize) return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(fileUrl));
    QNetworkReply* reply = m_networkManager->get(request);
    m_activeReplies.append(reply);
    connect(reply, &QNetworkReply::finished, [this, reply, destPath]() {
        reply->deleteLater();
        m_activeReplies.removeOne(reply);
        if (reply->error() == QNetworkReply::NoError)
        {
            QFile file(destPath);
            if (file.open(QIODevice::WriteOnly))
            {
                file.write(reply->readAll());
                file.close();
            }
        }
    });
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

                    if (m_downloadSource == BMCL)
                    {
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

    QThreadPool::globalInstance()->start([this, scanId, all, bases, librariesPath]() {
        QList<PendingDownload> remaining;
        qint64 reusedBytes = 0;

        for (const PendingDownload &dl : all)
        {
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

    if (reusedBytes > 0)
    {
        m_downloadedBytes += reusedBytes;
        updateTaskProgress(m_downloadedBytes, m_totalBytes);
    }

    m_pendingDownloads.clear();
    for (const PendingDownload &dl : remaining)
        m_pendingDownloads.enqueue(dl);
    if (m_pendingDownloads.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::Assets);
        m_currentPhase = DownloadPhase::Assets;
        startAssetsDownload();
        return;
    }

    emit statusChanged(tr("正在下载 %1 个依赖库...").arg(m_pendingDownloads.size()));

    int concurrent = qMin(MAX_CONCURRENT_DOWNLOADS, m_pendingDownloads.size());
    for (int i = 0; i < concurrent; ++i)
    {
        processLibraryQueue();
    }
}

void VersionDownloader::processLibraryQueue()
{
    if (m_currentPhase != DownloadPhase::Libraries) return;
    if (m_isPaused) return;

    if (m_pendingDownloads.isEmpty())
    {
        if (m_activeDownloadCount == 0)
        {
            DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::Assets);
            m_currentPhase = DownloadPhase::Assets;
            startAssetsDownload();
        }
        return;
    }

    while (m_activeDownloadCount < MAX_CONCURRENT_DOWNLOADS && !m_pendingDownloads.isEmpty())
    {
        PendingDownload dl = m_pendingDownloads.dequeue();
        m_activeDownloadCount++;
        downloadSingleFile(dl.url, dl.filePath, dl.fileSize, dl.expectedHash, dl.retryCount);
    }
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
        startAssetsFromIndex(rootObj["objects"].toObject(), assetId);
        return;
    }

    emit statusChanged(tr("正在获取资源索引..."));
    updateTaskStatus(tr("正在获取资源索引"));

    QString assetUrl = assetIndex["url"].toString();
    if (m_downloadSource == BMCL)
    {
        assetUrl.replace("https://launchermeta.mojang.com", "https://bmclapi2.bangbang93.com");
        assetUrl.replace("https://piston-meta.mojang.com", "https://bmclapi2.bangbang93.com");
    }

    QNetworkRequest request;
    request.setUrl(QUrl(assetUrl));
    QNetworkReply *reply = m_networkManager->get(request);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, &VersionDownloader::onAssetIndexReplyFinished);
}

void VersionDownloader::onAssetIndexReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (m_isPaused || !m_isDownloading)
    {
        cleanupActiveReply(reply);
        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        QString errMsg = tr("获取资源索引失败: %1").arg(reply->errorString());
        cleanupActiveReply(reply);
        failDownload(errMsg);
        return;
    }

    QByteArray data = reply->readAll();
    cleanupActiveReply(reply);

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject rootObj = doc.object();

    QJsonObject assetIndex = m_currentVersionManifest["assetIndex"].toObject();
    QString assetId = assetIndex["id"].toString();
    ManifestCache::instance()->setAssetIndex(assetId, rootObj);

    if (!rootObj.contains("objects"))
    {
        failDownload(tr("资源索引格式错误"));
        return;
    }

    startAssetsFromIndex(rootObj["objects"].toObject(), assetId, data);
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
            dl.url = QString("https://bmclapi2.bangbang93.com/assets/%1/%2").arg(hashPrefix).arg(hash);
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

    QThreadPool::globalInstance()->start([this, scanId, all, bases, assetsPath]() {
        QList<PendingDownload> remaining;
        qint64 reusedBytes = 0;

        for (const PendingDownload &dl : all)
        {
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

    if (reusedBytes > 0)
    {
        m_downloadedBytes += reusedBytes;
        updateTaskProgress(m_downloadedBytes, m_totalBytes);
    }

    m_pendingAssets.clear();
    for (const PendingDownload &dl : remaining)
        m_pendingAssets.enqueue(dl);
    if (m_pendingAssets.isEmpty())
    {
        completeDownload();
        return;
    }

    emit statusChanged(tr("正在下载 %1 个资源文件...").arg(m_pendingAssets.size()));

    int concurrent = qMin(MAX_CONCURRENT_DOWNLOADS, m_pendingAssets.size());
    for (int i = 0; i < concurrent; ++i)
    {
        processAssetQueue();
    }
}

void VersionDownloader::completeDownload()
{
    m_currentPhase = DownloadPhase::Completed;
    emit downloadCompleted(m_currentVersionId, getActualVersionPath());
    DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Completed, tr("下载完成"));
    DownloadTaskManager::instance()->updateTaskStage(m_currentTaskId, DownloadStage::Completed, 100);
    m_isDownloading = false;
}

void VersionDownloader::processAssetQueue()
{
    if (m_currentPhase != DownloadPhase::Assets) return;
    if (m_isPaused) return;

    if (m_pendingAssets.isEmpty())
    {
        if (m_activeDownloadCount == 0)
        {
            completeDownload();
        }
        return;
    }

    while (m_activeDownloadCount < MAX_CONCURRENT_DOWNLOADS && !m_pendingAssets.isEmpty())
    {
        PendingDownload dl = m_pendingAssets.dequeue();
        m_activeDownloadCount++;
        downloadSingleFile(dl.url, dl.filePath, dl.fileSize, dl.expectedHash, dl.retryCount);
    }
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

QString VersionDownloader::getManifestUrl(const QString &versionId)
{
    Q_UNUSED(versionId);
    if (m_downloadSource == BMCL)
    {
        return QString("https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json");
    }
    return QString("https://launchermeta.mojang.com/mc/game/version_manifest_v2.json");
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
        url.replace("https://launcher.mojang.com/v1/objects", "https://bmclapi2.bangbang93.com/objects");
        url.replace("https://piston-data.mojang.com/v1/objects", "https://bmclapi2.bangbang93.com/objects");
    }

    return url;
}

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

    int percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
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