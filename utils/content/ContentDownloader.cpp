/**
 * @file   ContentDownloader.cpp
 * @brief  内容下载器实现 — 基于 MultiMC net 层（NetJob/Download/FileSink）
 * @author BlockBox Team
 * @date   2026-06-18
 */

#include "ContentDownloader.h"

#include "../Safety.h"
#include "../download/DownloadMirrors.h"
#include "../net/NetJob.h"
#include "../net/Download.h"
#include "../net/ChecksumValidator.h"
#include "../net/NetworkAccess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>

class ContentDownloader::Private
{
public:
    ~Private()
    {
        if (job)
            job->abort();
    }

    NetJob::Ptr job;
    QString fileName;
    QString instancePath;
    ContentType contentType;
    QTemporaryFile *tempFile = nullptr; ///< 存档类下载的临时压缩包
};

ContentDownloader::ContentDownloader(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
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

    // 上一次下载未结束就发起新下载：取消旧任务
    if (d->job)
    {
        d->job->abort();
        d->job.reset();
    }

    const ContentTypeConfig config = ContentTypeConfig::getConfig(contentType);

    // 存档：先下到临时文件再解压；其余类型直接落到目标文件夹
    QString targetPath;
    if (contentType == ContentType::World)
    {
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
        targetPath = d->tempFile->fileName();
        d->tempFile->close();
    }
    else
    {
        const QString targetDir = d->instancePath + "/" + config.folderName;
        QDir().mkpath(targetDir);
        targetPath = QDir::cleanPath(targetDir + "/" + finalName);
        if (!Safety::isPathInside(targetDir, targetPath))
        {
            emit downloadFailed(tr("文件名非法: %1").arg(finalName));
            return;
        }
    }

    // MultiMC net 层：候选地址（镜像在前、官方兜底）装进一个 Download 动作
    const QStringList candidates = DownloadMirrors::expandFileCandidates(downloadUrl);
    auto dl = Net::Download::makeFile(QUrl(candidates.first()), targetPath);
    for (int i = 1; i < candidates.size(); ++i)
        dl->addUrlFallback(QUrl(candidates.at(i)));

    auto job = NetJob::Ptr(new NetJob(finalName, Net::networkAccessManager()));
    job->addNetAction(dl);

    QPointer<ContentDownloader> self(this);

    connect(dl.get(), &NetAction::netActionProgress, this,
            [this](int, qint64 received, qint64 total) {
        emit downloadProgress(received, total);
    });

    connect(job.get(), &Task::finished, this, [this, self, job, targetPath, finalName, config]() {
        d->job.reset();
        if (!self)
            return;
        if (!job->wasSuccessful())
        {
            emit downloadFailed(tr("下载失败：已尝试全部下载源"));
            return;
        }

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

            const QString targetDir = d->instancePath + "/" + config.folderName;
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
            QString zipPath = targetPath;
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
        else
        {
            emit downloadFinished(targetPath);
        }
    });

    d->job = job;
    job->start();
}
