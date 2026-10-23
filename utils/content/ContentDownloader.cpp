/**
 * @file   ContentDownloader.cpp
 * @brief  内容下载器实现
 * @author BlockBox Team
 * @date   2026-06-18
 */

#include "ContentDownloader.h"

#include "../Safety.h"

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

    // 文件名可能来自远程接口或调用方，先压成安全的单层文件名，
    // 防止 “../../xxx” 之类把内容写到实例目录之外
    finalName = Safety::safeFileName(finalName, QStringLiteral("download"));

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
            QString worldName = QFileInfo(finalName).completeBaseName();
            if (worldName.isEmpty() || worldName == QLatin1String(".")
                || worldName == QLatin1String(".."))
            {
                worldName = QStringLiteral("world");
            }
            worldName = Safety::safeFileName(worldName, QStringLiteral("world"));

            QString worldDir = QDir::cleanPath(targetDir + "/" + worldName);
            // 防止 "../" 之类让 worldDir 变成 saves 目录本身（会删光所有存档）
            if (!Safety::isPathInside(targetDir, worldDir)
                || QDir::cleanPath(worldDir) == QDir::cleanPath(targetDir))
            {
                emit downloadFailed(tr("存档名称非法: %1").arg(finalName));
                return;
            }

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
                    if (exitCode != 0) {
                        // Fallback: try 7z（此时还不能删压缩包）
                        QProcess *fallback = new QProcess(this);
                        connect(fallback, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                            this, [this, fallback, worldDir, zipPath](int fallbackExit, QProcess::ExitStatus) {
                                fallback->deleteLater();
                                QFile::remove(zipPath);
                                if (fallbackExit == 0)
                                    emit downloadFinished(worldDir);
                                else
                                    emit downloadFailed(tr("解压存档失败"));
                            });
                        fallback->start("7z", QStringList() << "x" << "-o" + worldDir << "-y" << zipPath);
                    } else {
                        QFile::remove(zipPath);
                        emit downloadFinished(worldDir);
                    }
                });
#ifdef Q_OS_WIN
            extractProcess->start("powershell", QStringList()
                << "-NoProfile"
                << "-Command"
                << QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                   .arg(Safety::psQuote(zipPath), Safety::psQuote(worldDir)));
#else
            extractProcess->start("unzip", QStringList() << "-o" << zipPath << "-d" << worldDir);
#endif
        }
        else if (d->contentType == ContentType::Modpack)
        {
            // For modpacks: save to temp location, signal modpackReady for import
            QString modpackDir = d->instancePath + "/" + config.folderName;
            QDir().mkpath(modpackDir);
            QString destPath = QDir::cleanPath(modpackDir + "/" + finalName);
            if (!Safety::isPathInside(modpackDir, destPath))
            {
                emit downloadFailed(tr("文件名非法: %1").arg(finalName));
                return;
            }

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
            QString destPath = QDir::cleanPath(targetDir + "/" + finalName);
            if (!Safety::isPathInside(targetDir, destPath))
            {
                emit downloadFailed(tr("文件名非法: %1").arg(finalName));
                return;
            }

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