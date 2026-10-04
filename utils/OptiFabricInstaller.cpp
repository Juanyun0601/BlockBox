/**
 * @file   OptiFabricInstaller.cpp
 * @brief  OptiFabric安装器类实现
 * @author BlockBox Team
 * @date   2026-08-29
 */

#include "OptiFabricInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

#include "DownloadTaskManager.h"

OptiFabricInstaller* OptiFabricInstaller::m_instance = nullptr;
QMutex OptiFabricInstaller::m_instanceMutex;

OptiFabricInstaller::OptiFabricInstaller()
    : m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
    , m_isInstalling(false)
    , m_isCancelled(false)
    , m_totalBytes(0)
    , m_downloadedBytes(0)
{
}

OptiFabricInstaller::~OptiFabricInstaller()
{
    cleanupTempFiles();
}

OptiFabricInstaller* OptiFabricInstaller::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new OptiFabricInstaller();
        }
    }
    return m_instance;
}

void OptiFabricInstaller::setCurrentTaskId(const QString &taskId)
{
    m_currentTaskId = taskId;
}

QString OptiFabricInstaller::currentTaskId() const
{
    return m_currentTaskId;
}

void OptiFabricInstaller::updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    int percent = 0;
    if (bytesTotal > 0) {
        percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
    }

    DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, percent);
}

void OptiFabricInstaller::updateTaskStatus(const QString &status)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    DownloadTaskManager::instance()->updateTaskStep(m_currentTaskId, status);
}

QList<OptiFabricVersionInfo> OptiFabricInstaller::getOptiFabricVersions(const QString &mcVersion)
{
    emit statusChanged(tr("正在获取OptiFabric版本列表..."));
    updateTaskStatus(tr("正在获取OptiFabric版本列表"));

    QString url = QString("https://api.modrinth.com/v2/project/optifine/version?game_versions=[\"%1\"]&loaders=[\"fabric\"]").arg(mcVersion);

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");

    QEventLoop loop;
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    QList<OptiFabricVersionInfo> versions;

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = tr("获取OptiFabric版本列表失败: %1").arg(reply->errorString());
        emit statusChanged(errorMsg);
        reply->deleteLater();
        return versions;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isArray()) {
        return versions;
    }

    QJsonArray versionArray = doc.array();

    for (const QJsonValue &value : versionArray) {
        QJsonObject versionObj = value.toObject();
        OptiFabricVersionInfo info;
        info.version = versionObj["version_number"].toString();
        info.mcVersion = mcVersion;
        info.datePublished = versionObj["date_published"].toString();

        QJsonArray files = versionObj["files"].toArray();
        for (const QJsonValue &fileValue : files) {
            QJsonObject fileObj = fileValue.toObject();
            if (fileObj["primary"].toBool()) {
                info.downloadUrl = fileObj["url"].toString();
                info.fileName = fileObj["filename"].toString();
                break;
            }
        }

        if (!info.version.isEmpty() && !info.downloadUrl.isEmpty()) {
            versions.append(info);
        }
    }

    return versions;
}

void OptiFabricInstaller::downloadOptiFabric(const QString &mcVersion, const QString &optiFabricVersion,
                                              const QString &downloadUrl, const QString &instancePath)
{
    if (m_isInstalling) {
        emit installFailed(tr("安装器正忙"));
        return;
    }

    m_isInstalling = true;
    m_isCancelled = false;
    m_currentMcVersion = mcVersion;
    m_currentOptiFabricVersion = optiFabricVersion;
    m_currentInstancePath = instancePath;
    m_downloadedBytes = 0;
    m_totalBytes = 0;

    m_tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + "/optifabric_install_" + QString::number(QDateTime::currentMSecsSinceEpoch());

    QDir().mkpath(m_tempDir);

    QString tempFilePath = m_tempDir + "/optifabric-" + optiFabricVersion + ".jar";

    emit statusChanged(tr("正在下载OptiFabric安装包..."));
    updateTaskStatus(tr("正在下载OptiFabric安装包"));

    if (!downloadFile(downloadUrl, tempFilePath)) {
        emit installFailed(tr("下载OptiFabric安装包失败"));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    // OptiFabric是模组，需要放到mods文件夹
    QString modsPath = instancePath + "/mods";
    QDir().mkpath(modsPath);

    // 复制JAR文件到mods目录
    QString destJarPath = modsPath + "/optifabric-" + optiFabricVersion + ".jar";
    if (!QFile::copy(tempFilePath, destJarPath)) {
        emit installFailed(tr("复制安装文件失败"));
        m_isInstalling = false;
        return;
    }

    cleanupTempFiles();
    m_isInstalling = false;

    emit installProgressUpdated(100, tr("安装完成"));
    emit installCompleted("OptiFabric " + optiFabricVersion);
}

bool OptiFabricInstaller::downloadFile(const QString &url, const QString &filePath, int redirectDepth)
{
    if (redirectDepth > 5) {
        return false;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");

    QEventLoop loop;
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this, &OptiFabricInstaller::updateTaskProgress);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
        m_downloadedBytes = bytesReceived;
        m_totalBytes = bytesTotal;
    });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::ContentReSendError) {
            QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
            if (!redirectTarget.isNull()) {
                QString redirectUrl = redirectTarget.toString();
                reply->deleteLater();
                return downloadFile(redirectUrl, filePath, redirectDepth + 1);
            }
        }
        reply->deleteLater();
        return false;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(responseData);
    file.close();

    return true;
}

void OptiFabricInstaller::cancelInstall()
{
    m_isCancelled = true;

    if (m_currentReply) {
        m_currentReply->abort();
    }

    cleanupTempFiles();
    m_isInstalling = false;

    emit installCancelled();
}

bool OptiFabricInstaller::isInstalling() const
{
    return m_isInstalling;
}

void OptiFabricInstaller::cleanupTempFiles()
{
    if (!m_tempDir.isEmpty()) {
        QDir dir(m_tempDir);
        if (dir.exists()) {
            dir.removeRecursively();
        }
        m_tempDir.clear();
    }
}
