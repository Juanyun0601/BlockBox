/**
 * @file   VersionDownloader.h
 * @brief  版本下载器类声明（全异步架构，无 QEventLoop 阻塞）
 * @author BlockBox Team
 * @date   2026-05-30
 */
#ifndef VERSIONDOWNLOADER_H
#define VERSIONDOWNLOADER_H

#include <QElapsedTimer>
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
        int retryCount = 0;
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

private slots:
    void onManifestReplyFinished();
    void onVersionJsonReplyFinished();
    void onClientJarProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onLibraryFileFinished();
    void onAssetIndexReplyFinished();

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
    void processLibraryQueue();
    void startAssetsDownload();
    void fetchAssetIndex();
    void processAssetQueue();

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
    void downloadSingleFile(const QString &url, const QString &filePath,
                            qint64 fileSize, const QString &expectedHash = QString(),
                            int retryCount = 0);
    void downloadFileSegments(const QString &url, const QString &filePath, qint64 fileSize);
    void onSegmentReplyFinished();

    QString getManifestUrl(const QString &versionId);
    QString getClientDownloadUrl(const QJsonObject &versionManifest);
    void calculateTotalSize(const QJsonObject &versionManifest);
    void addAssetTotalSize(const QJsonObject &versionManifest);
    QString getActualVersionPath() const;
    void updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal);
    void updateTaskStatus(const QString &status);

    static const int MAX_CONCURRENT_DOWNLOADS = 128;
    static const int MAX_RETRY_COUNT = 3;
    static const qint64 MIN_PARALLEL_FILE_SIZE = 2 * 1024 * 1024;

    QNetworkAccessManager *m_networkManager;
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
    QMap<QNetworkReply*, qint64> m_activeFileProgress;
    QQueue<PendingDownload> m_pendingDownloads;
    QQueue<PendingDownload> m_pendingAssets;
    int m_activeDownloadCount;

    QString m_tempSegmentDir;

    QElapsedTimer m_speedTimer;
    qint64 m_lastSpeedBytes;
    qint64 m_lastSpeedTime;

    // 复用扫描代数：开启新任务/取消/失败时自增，使在途后台扫描结果作废
    int m_reuseScanId = 0;

    mutable QMutex m_stateMutex;

    static VersionDownloader *m_instance;
};

#endif // VERSIONDOWNLOADER_H