/**
 * @file   ContentDownloader.cpp
 * @brief  内容下载器实现
 * @author BlockBox Team
 * @date   2026-06-18
 */

#include "ContentDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QTemporaryFile>
#include <QProcess>

class ContentDownloader::Private
{
public:
    QNetworkAccessManager *networkManager;
    QString downloadUrl;
    QString fileName;
    QString instancePath;
    ContentType contentType;
    QString tempFilePath;
    QTemporaryFile *tempFile;
};

ContentDownloader::ContentDownloader(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->networkManager = new QNetworkAccessManager(this);
    d->tempFile = nullptr;
}

ContentDownloader::~ContentDownloader()
{
    if (d->tempFile)
    {
        d->tempFile->deleteLater();
        d->tempFile = nullptr;
    }
    delete d;
}

QString ContentDownloader::installPath(const QString &instancePath, ContentType contentType)
{
    ContentTypeConfig config = ContentTypeConfig::getConfig(contentType);
    return instancePath + "/" + config.folderName;
}

void ContentDownloader::downloadToInstance(const QString &downloadUrl, const QString &fileName,
                                            const QString &instancePath, ContentType contentType)
{
    d->downloadUrl = downloadUrl;
    d->fileName = fileName;
    d->instancePath = instancePath;
    d->contentType = contentType;

    // Determine the final file name
    QString finalName = fileName;
    if (finalName.isEmpty())
    {
        QUrl url(downloadUrl);
        finalName = url.fileName();
        if (finalName.isEmpty() || finalName.contains('?'))
        {
            finalName = QStringLiteral("download");
        }
    }

    // Create a temp file to download into
    // 若上次下载尚未完成（或已失败但对象仍存在），先释放旧 tempFile，避免泄漏
    if (d->tempFile)
    {
        d->tempFile->deleteLater();
        d->tempFile = nullptr;
    }
    d->tempFile = new QTemporaryFile(
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/blockbox_dl_XXXXXX");
    if (!d->tempFile->open())
    {
        d->tempFile->deleteLater();
        d->tempFile = nullptr;
        emit downloadFailed(tr("无法创建临时文件"));
        return;
    }
    d->tempFilePath = d->tempFile->fileName();
    d->tempFile->close();

    QNetworkRequest request{QUrl(downloadUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "BlockBox/1.0");

    QNetworkReply *reply = d->networkManager->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total)
    {
        emit downloadProgress(received, total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, finalName]()
    {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            emit downloadFailed(reply->errorString());
            return;
        }

        // Read all data and write to temp file
        QByteArray data = reply->readAll();
        QFile tempFile(d->tempFilePath);
        if (!tempFile.open(QIODevice::WriteOnly))
        {
            emit downloadFailed(tr("无法写入临时文件"));
            return;
        }
        tempFile.write(data);
        tempFile.close();

        // Determine the target directory and file path
        ContentTypeConfig config = ContentTypeConfig::getConfig(d->contentType);
        QString targetDir = d->instancePath + "/" + config.folderName;
        QDir().mkpath(targetDir);

        if (d->contentType == ContentType::World)
        {
            // For worlds: extract zip into saves/{filename_without_ext}/
            QString worldDir = targetDir + "/" + QFileInfo(finalName).completeBaseName();

            // Remove existing world folder if present
            QDir worldDirObj(worldDir);
            if (worldDirObj.exists())
            {
                worldDirObj.removeRecursively();
            }
            QDir().mkpath(worldDir);

            // Use 7z or system unzip to extract (异步执行，不阻塞 UI 线程)
            QString zipPath = d->tempFilePath;
            QProcess *extractProcess = new QProcess(this);
            connect(extractProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, extractProcess, worldDir, zipPath](int exitCode, QProcess::ExitStatus) {
                    extractProcess->deleteLater();
                    QFile::remove(zipPath);
                    if (exitCode != 0) {
                        // Fallback: try 7z
                        QProcess *fallback = new QProcess(this);
                        connect(fallback, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                            this, [this, fallback, worldDir](int, QProcess::ExitStatus) {
                                fallback->deleteLater();
                                emit downloadFinished(worldDir);
                            });
                        fallback->start("7z", QStringList() << "x" << "-o" + worldDir << "-y" << zipPath);
                    } else {
                        emit downloadFinished(worldDir);
                    }
                });
#ifdef Q_OS_WIN
            extractProcess->start("powershell", QStringList()
                << "-Command"
                << QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                   .arg(zipPath, worldDir));
#else
            extractProcess->start("unzip", QStringList() << "-o" << zipPath << "-d" << worldDir);
#endif
        }
        else if (d->contentType == ContentType::Modpack)
        {
            // For modpacks: save to temp location, signal modpackReady for import
            QString modpackDir = d->instancePath + "/" + config.folderName;
            QDir().mkpath(modpackDir);
            QString destPath = modpackDir + "/" + finalName;

            // Copy from temp to dest
            if (QFile::exists(destPath))
            {
                QFile::remove(destPath);
            }
            QFile::copy(d->tempFilePath, destPath);
            QFile::remove(d->tempFilePath);

            emit downloadFinished(destPath);
        }
        else
        {
            // For Mod, DataPack, ResourcePack, ShaderPack: simply copy to target folder
            QString destPath = targetDir + "/" + finalName;

            if (QFile::exists(destPath))
            {
                QFile::remove(destPath);
            }
            QFile::copy(d->tempFilePath, destPath);
            QFile::remove(d->tempFilePath);

            emit downloadFinished(destPath);
        }
    });
}