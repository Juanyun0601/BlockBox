#include "MultiThreadDownloader.h"
#include "GithubAccelerator.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

MultiThreadDownloader::MultiThreadDownloader(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_fileSize(0)
    , m_threadCount(4)
    , m_cancelled(false)
    , m_fetchingSize(false)
    , m_completedSegments(0)
{
}

MultiThreadDownloader::~MultiThreadDownloader()
{
    cancelDownload();
}

void MultiThreadDownloader::startDownload(const QString &url, const QString &savePath,
                                            int threadCount, const QString &taskId,
                                            const QString &userAgent)
{
    m_url = GithubAccelerator::instance()->accelerateUrl(url);
    m_savePath = savePath;
    m_threadCount = qBound(1, threadCount, 128);
    m_taskId = taskId;
    m_userAgent = userAgent.trimmed();
    if (m_userAgent.isEmpty()) {
        m_userAgent = QStringLiteral("BlockBox/1.0");
    }
    m_cancelled = false;
    m_completedSegments = 0;
    m_segments.clear();
    m_activeReplies.clear();
    m_fileSize = 0;

    fetchFileSize();
}

void MultiThreadDownloader::fetchFileSize()
{
    m_fetchingSize = true;

    QPointer<MultiThreadDownloader> self(this);

    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    request.setRawHeader("Accept", "*/*");
    request.setTransferTimeout(15000);

    QNetworkReply *reply = m_networkManager->head(request);
    m_activeReplies.insert(reply);

    connect(reply, &QNetworkReply::finished, this, [self, reply]() {
        if (!self) return;

        self->m_activeReplies.remove(reply);

        if (self->m_cancelled) {
            reply->deleteLater();
            self->failDownload(QStringLiteral("已取消"));
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            reply->deleteLater();
            self->failDownload(QStringLiteral("无法获取文件信息: %1").arg(reply->errorString()));
            return;
        }

        qint64 fileSize = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();

        bool acceptRanges = false;
        QByteArray acceptRangesHeader = reply->rawHeader("Accept-Ranges");
        if (acceptRangesHeader.toLower() == "bytes") {
            acceptRanges = true;
        }

        reply->deleteLater();

        if (!acceptRanges || fileSize <= 0) {
            self->failDownload(QStringLiteral("服务器不支持多线程分段下载（缺少 Accept-Ranges: bytes），请使用单线程下载"));
            return;
        }

        if (fileSize < 1024 * 1024) {
            self->m_threadCount = 1;
        }

        self->m_fileSize = fileSize;
        self->m_fetchingSize = false;
        self->startSegmentDownloads();
    });
}

void MultiThreadDownloader::startSegmentDownloads()
{
    QDir().mkpath(QFileInfo(m_savePath).absolutePath());

    QString baseName = QFileInfo(m_savePath).fileName();
    m_tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                + "/blockbox_segments_" + baseName + "_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(m_tempDir);

    int actualThreads = qMin(m_threadCount, static_cast<int>(m_fileSize / (256 * 1024)));
    if (actualThreads < 1) actualThreads = 1;

    qint64 segmentSize = m_fileSize / actualThreads;
    m_segments.resize(actualThreads);

    for (int i = 0; i < actualThreads; ++i) {
        Segment &seg = m_segments[i];
        seg.index = i;
        seg.start = i * segmentSize;
        seg.end = (i == actualThreads - 1) ? m_fileSize - 1 : (i + 1) * segmentSize - 1;
        seg.tempPath = m_tempDir + QString("/seg_%1.tmp").arg(i);
        seg.completed = false;
        seg.reply = nullptr;
    }

    for (int i = 0; i < actualThreads; ++i) {
        downloadSegment(i, m_segments[i].start, m_segments[i].end);
    }
}

QNetworkReply* MultiThreadDownloader::downloadSegment(int index, qint64 start, qint64 end)
{
    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Range", QString("bytes=%1-%2").arg(start).arg(end).toUtf8());
    request.setTransferTimeout(300000);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply *reply = m_networkManager->get(request);
    m_activeReplies.insert(reply);
    m_segments[index].reply = reply;

    reply->setProperty("segIndex", index);

    connect(reply, &QNetworkReply::finished, this, &MultiThreadDownloader::onSegmentFinished);

    connect(reply, &QNetworkReply::downloadProgress, this,
        [this](qint64 bytesReceived, qint64 bytesTotal) {
            Q_UNUSED(bytesReceived);
            Q_UNUSED(bytesTotal);
            updateProgress();
        });

    return reply;
}

void MultiThreadDownloader::onSegmentFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    m_activeReplies.remove(reply);

    int segIndex = reply->property("segIndex").toInt();

    if (m_cancelled) {
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QString errorStr = reply->errorString();
        reply->deleteLater();
        failDownload(QStringLiteral("分段 %1 下载失败: %2").arg(segIndex).arg(errorStr));
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    if (segIndex < 0 || segIndex >= m_segments.size()) return;

Segment &seg = m_segments[segIndex];
    QDir().mkpath(QFileInfo(seg.tempPath).absolutePath());
    QFile segFile(seg.tempPath);
    if (!segFile.open(QIODevice::WriteOnly)) {
        failDownload(QStringLiteral("无法写入分段 %1 文件: %2").arg(segIndex).arg(seg.tempPath));
        return;
    }
    qint64 written = segFile.write(data);
    segFile.close();
    if (written != data.size()) {
        failDownload(QStringLiteral("分段 %1 写入不完整（%2/%3 字节）").arg(segIndex).arg(written).arg(data.size()));
        return;
    }
    seg.completed = true;
    m_completedSegments++;

    updateProgress();

    if (m_completedSegments >= m_segments.size()) {
        mergeSegments();
    }
}

void MultiThreadDownloader::mergeSegments()
{
    QFile outFile(m_savePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        failDownload(QStringLiteral("无法创建输出文件: %1").arg(m_savePath));
        return;
    }

    qint64 written = 0;
    for (int i = 0; i < m_segments.size(); ++i) {
        QFile segFile(m_segments[i].tempPath);
        if (segFile.open(QIODevice::ReadOnly)) {
            QByteArray data = segFile.readAll();
            segFile.close();
            outFile.write(data);
            written += data.size();
        }
    }
    outFile.close();

    cleanup();

    qDebug() << "[MultiThreadDownloader] Download completed:" << m_savePath;
    emit downloadCompleted(m_taskId, m_savePath);
}

void MultiThreadDownloader::updateProgress()
{
    qint64 totalReceived = 0;
    for (const Segment &seg : m_segments) {
        if (seg.completed) {
            totalReceived += (seg.end - seg.start + 1);
        }
    }
    emit downloadProgress(m_taskId, totalReceived, m_fileSize);
}

void MultiThreadDownloader::failDownload(const QString &error)
{
    m_cancelled = true;
    cleanup();
    qWarning() << "[MultiThreadDownloader] Failed:" << error;
    emit downloadFailed(m_taskId, error);
}

void MultiThreadDownloader::cancelDownload()
{
    m_cancelled = true;
    cleanup();
}

void MultiThreadDownloader::cleanup()
{
    for (QNetworkReply *reply : m_activeReplies) {
        reply->abort();
        reply->deleteLater();
    }
    m_activeReplies.clear();
    m_segments.clear();

    if (!m_tempDir.isEmpty()) {
        QDir dir(m_tempDir);
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }
}
