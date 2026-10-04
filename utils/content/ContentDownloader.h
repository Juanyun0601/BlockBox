#ifndef CONTENTDOWNLOADER_H
#define CONTENTDOWNLOADER_H

#include <QObject>
#include <QString>
#include "ContentData.h"

class ContentDownloader : public QObject
{
    Q_OBJECT

public:
    explicit ContentDownloader(QObject *parent = nullptr);
    ~ContentDownloader();

    // Download a file from URL and save to the appropriate instance folder
    void downloadToInstance(const QString &downloadUrl, const QString &fileName,
                            const QString &instancePath, ContentType contentType);

    static QString installPath(const QString &instancePath, ContentType contentType);

signals:
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &error);

private:
    class Private;
    Private *d;
};

#endif