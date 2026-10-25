/**
 * @file   VersionDownloader.h
 * @brief  版本下载器类声明（全异步架构，无 QEventLoop 阻塞）
 * @author BlockBox Team
 * @date   2026-05-30
 */
#ifndef VERSIONDOWNLOADER_H
#define VERSIONDOWNLOADER_H

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <functional>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>

#include "utils/DownloadTaskManager.h"
#include "utils/net/NetJob.h"

class VersionDownloader : public QObject
{
    Q_OBJECT

public:
    enum DownloadSource {
        Official,
        BMCL
    };

    enum class DownloadPhase {
        Idle,
        ManifestFetch,
        VersionJsonFetch,
        ClientJar,
        Libraries,
        Assets,
        Completed
    };

    struct DownloadItem {
        QString url;
        QString filePath;
        qint64 fileSize;
    };

    struct DownloadSegment {
        QString url;
        QString tempPath;
        qint64 start;
        qint64 end;
    };

    struct PendingDownload {
        QString url;
        QString filePath;
        qint64 fileSize;
        QString expectedHash;   // 预期 SHA1 哈希，用于下载后完整性验证
        QString fallbackUrl;    // 镜像源失败后的官方源兜底地址（仅 BMCL 源时填）
    };

    static VersionDownloader* instance();

    void setDownloadSource(DownloadSource source);
    DownloadSource downloadSource() const;

    void downloadVanilla(const QString &versionId, const QString &instancePath,
                         const QString &instanceName = QString(),
                         const QStringList &loaders = QStringList());
    /** 将指定版本下载到一个既有的实例目录（用于修改现有实例），
     *  文件直接写入 targetInstancePath，实例名与目录保持不变。 */
    void downloadVanillaTo(const QString &versionId, const QString &targetInstancePath,
                           const QString &instanceName = QString(),
                           const QStringList &loaders = QStringList());
    void cancelDownload();
    void pauseDownload();
    void resumeDownload();
    bool isDownloading() const;
    bool isPaused() const;

    void setCurrentTaskId(const QString &taskId);
    QString currentTaskId() const;

signals:
    void downloadStarted(const QString &taskId);
    void downloadProgressUpdated(qint64 bytesReceived, qint64 bytesTotal);
    void downloadCompleted(const QString &versionId, const QString &actualVersionPath);
    void downloadFailed(const QString &error);
    void downloadCancelled();
    void downloadPaused();
    void downloadResumed();
    void statusChanged(const QString &status);

private:
    VersionDownloader();
    ~VersionDownloader();

    void cleanupActiveReply(QNetworkReply *reply);
    void abortAllActiveReplies();
    void failDownload(const QString &error);

    void fetchManifest();
    void fetchVersionJson();
    void downloadClientJar();
    void downloadClientJarFromNetwork();
    void downloadLogConfigs();
    void startLibrariesDownload();
    void startAssetsDownload();
    void fetchAssetIndex();

    // ── 本地文件复用（参考 HMCL/PCL2：安装前复用其他实例/共享目录中的同内容文件）──
    QStringList buildReuseBases() const;
    void startClientJarReuseScan(const QString &clientJarPath, qint64 clientSize,
                                 const QString &expectedSha1);
    void applyClientJarReuseResult(bool reused, int scanId);
    void proceedAfterClientJar();
    void startLibraryScan(QList<PendingDownload> all, const QString &librariesPath);
    void applyLibraryScanResult(QList<PendingDownload> remaining, qint64 reusedBytes, int scanId);
    void startAssetsFromIndex(const QJsonObject &objects, const QString &assetId,
                              const QByteArray &indexData = QByteArray());
    void startAssetScan(QList<PendingDownload> all, const QString &assetsPath);
    void applyAssetScanResult(QList<PendingDownload> remaining, qint64 reusedBytes, int scanId);
    void completeDownload();

    // ── 清单类 JSON（MultiMC net 层：HttpMetaCache + Download::makeCached）──
    void fetchJsonCached(const QStringList &urls, const QString &resourcePath,
                         std::function<void(bool ok, const QByteArray &data)> onDone);

    // ── 文件传输（MultiMC net 层：Download + FileSink + ChecksumValidator）──
    /// 单个待下载文件的全部参数（重试/暂停重排时需要原样带回）
    struct NetFileParams
    {
        QString url;
        QString filePath;
        qint64 fileSize = 0;
        QString expectedHash;
        QString fallbackUrl;
        bool isClientJar = false;
    };
    void downloadSingleFile(const QString &url, const QString &filePath,
                            qint64 fileSize, const QString &expectedHash = QString(),
                            const QString &fallbackUrl = QString(),
                            bool isClientJar = false);
    void onNetFileSucceeded(const NetFileParams &params);
    void onNetFileFailed(const NetFileParams &params);
    void onNetFileAborted(const NetFileParams &params);

    // ── 阶段作业（MultiMC 进度逻辑：库/资源各自装进一个 NetJob，
    //    完成文件等权 + 在途文件按字节占比，由 NetJob 聚合出阶段进度）──
    void startPhaseJob(const QList<PendingDownload> &files, DownloadPhase phase);
    void onPhaseJobFinished(const NetJob::Ptr &job);
    void reportPhaseProgress(bool force);
    void addCompletedFile(const QString &filePath, qint64 fileSize);

    QString getClientDownloadUrl(const QJsonObject &versionManifest);
    void calculateTotalSize(const QJsonObject &versionManifest);
    void addAssetTotalSize(const QJsonObject &versionManifest);
    QString getActualVersionPath() const;
    void updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal);
    void updateTaskStatus(const QString &status);

    static const int MAX_RETRY_COUNT = 3;

    DownloadSource m_downloadSource;
    DownloadPhase m_currentPhase;

    QString m_currentVersionId;
    QString m_currentInstancePath;
    QJsonObject m_currentVersionManifest;
    QString m_currentTaskId;

    bool m_isDownloading;
    bool m_isPaused;

    qint64 m_totalBytes;
    qint64 m_downloadedBytes;
    qint64 m_clientJarSize;

    QList<QNetworkReply*> m_activeReplies;
    // MultiMC net 层：在途的单文件下载任务（客户端 JAR / 日志配置，取消时统一 abort）
    QList<NetJob::Ptr> m_fileJobs;

    // 当前阶段作业（库/资源各一个 NetJob）及其文件清单与完成集合
    NetJob::Ptr m_phaseJob;
    DownloadPhase m_phaseJobPhase = DownloadPhase::Libraries;
    QSet<QString> m_completedFiles;                 ///< 本次任务已完成文件的绝对路径
    QHash<NetAction *, qint64> m_phaseActionReceived; ///< 在途动作已收字节（进度聚合用）
    qint64 m_lastProgressReportMs = 0;              ///< 进度上报节流

    // BMCL 源客户端 JAR 的官方源兜底地址与校验哈希（Download 的回退地址）
    QString m_clientJarFallbackUrl;
    QString m_clientJarSha1;

    QElapsedTimer m_speedTimer;
    qint64 m_lastSpeedBytes;
    qint64 m_lastSpeedTime;

    // 复用扫描代数：开启新任务/取消/失败时自增，使在途后台扫描结果作废
    int m_reuseScanId = 0;

    mutable QMutex m_stateMutex;

    static VersionDownloader *m_instance;
};

#endif // VERSIONDOWNLOADER_H