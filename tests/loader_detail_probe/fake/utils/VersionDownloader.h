/**
 * 探针用 VersionDownloader 桩：仅声明 LoaderDetailPage.cpp 引用到的接口。
 */
#ifndef VERSIONDOWNLOADER_H
#define VERSIONDOWNLOADER_H

#include <QObject>
#include <QString>
#include <QStringList>

class VersionDownloader : public QObject
{
    Q_OBJECT
public:
    enum DownloadSource { Official, BMCL };

    static VersionDownloader *instance()
    {
        static VersionDownloader m;
        return &m;
    }

    void setDownloadSource(DownloadSource) {}
    QString currentTaskId() const { return QString(); }
    void downloadVanillaTo(const QString &, const QString &, const QString &,
                           const QStringList & = QStringList()) {}

signals:
    void downloadStarted(const QString &taskId);
    void downloadProgressUpdated(qint64 bytesReceived, qint64 bytesTotal);
    void downloadCompleted(const QString &versionId, const QString &actualVersionPath);
    void downloadFailed(const QString &error);
    void downloadCancelled();
    void statusChanged(const QString &status);
};

#endif // VERSIONDOWNLOADER_H
