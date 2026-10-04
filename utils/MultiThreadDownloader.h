#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <QString>
#include <QVector>

class MultiThreadDownloader : public QObject
{
    Q_OBJECT

public:
    explicit MultiThreadDownloader(QObject *parent = nullptr);
    ~MultiThreadDownloader();

    void startDownload(const QString &url, const QString &savePath, int threadCount,
                       const QString &taskId, const QString &userAgent = QString());
    void cancelDownload();

signals:
    void downloadProgress(const QString &taskId, qint64 bytesReceived, qint64 bytesTotal);
    void downloadCompleted(const QString &taskId, const QString &savePath);
    void downloadFailed(const QString &taskId, const QString &error);

private:
    void fetchFileSize();
    void startSegmentDownloads();
    QNetworkReply* downloadSegment(int index, qint64 start, qint64 end);
    void onSegmentFinished();
    void mergeSegments();
    void cleanup();
    void failDownload(const QString &error);
    void updateProgress();

    struct Segment {
        int index;
        qint64 start;
        qint64 end;
        QString tempPath;
        QNetworkReply *reply;
        bool completed;
    };

    QNetworkAccessManager *m_networkManager;
    QString m_url;
    QString m_savePath;
    QString m_taskId;
    QString m_userAgent;
    qint64 m_fileSize;
    int m_threadCount;
    bool m_cancelled;
    bool m_fetchingSize;

    QVector<Segment> m_segments;
    int m_completedSegments;
    QSet<QNetworkReply*> m_activeReplies;
    QString m_tempDir;
};
