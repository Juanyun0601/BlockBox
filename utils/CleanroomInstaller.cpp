/**
 * @file   CleanroomInstaller.cpp
 * @brief  Cleanroom安装器类实现
 * @author BlockBox Team
 * @date   2026-08-29
 */

#include "CleanroomInstaller.h"

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

CleanroomInstaller* CleanroomInstaller::m_instance = nullptr;
QMutex CleanroomInstaller::m_instanceMutex;

CleanroomInstaller::CleanroomInstaller()
    : m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
    , m_isInstalling(false)
    , m_isCancelled(false)
    , m_totalBytes(0)
    , m_downloadedBytes(0)
{
}

CleanroomInstaller::~CleanroomInstaller()
{
    cleanupTempFiles();
}

CleanroomInstaller* CleanroomInstaller::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new CleanroomInstaller();
        }
    }
    return m_instance;
}

void CleanroomInstaller::setCurrentTaskId(const QString &taskId)
{
    m_currentTaskId = taskId;
}

QString CleanroomInstaller::currentTaskId() const
{
    return m_currentTaskId;
}

void CleanroomInstaller::updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal)
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

void CleanroomInstaller::updateTaskStatus(const QString &status)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    DownloadTaskManager::instance()->updateTaskStep(m_currentTaskId, status);
}

QList<CleanroomVersionInfo> CleanroomInstaller::getCleanroomVersions(const QString &mcVersion)
{
    emit statusChanged(tr("正在获取Cleanroom版本列表..."));
    updateTaskStatus(tr("正在获取Cleanroom版本列表"));

    QString url = "https://api.github.com/repos/CleanroomMC/Cleanroom/releases";

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");

    QEventLoop loop;
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    QList<CleanroomVersionInfo> versions;

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = tr("获取Cleanroom版本列表失败: %1").arg(reply->errorString());
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

    QJsonArray releases = doc.array();

    for (const QJsonValue &value : releases) {
        QJsonObject releaseObj = value.toObject();
        QString tagName = releaseObj["tag_name"].toString();

        if (!tagName.contains(mcVersion)) {
            continue;
        }

        CleanroomVersionInfo info;
        info.version = tagName;
        info.mcVersion = mcVersion;
        info.tagName = tagName;
        info.publishedAt = releaseObj["published_at"].toString();

        QJsonArray assets = releaseObj["assets"].toArray();
        for (const QJsonValue &assetValue : assets) {
            QJsonObject assetObj = assetValue.toObject();
            QString assetName = assetObj["name"].toString();
            if (assetName.endsWith("-installer.jar") || assetName.endsWith(".jar")) {
                info.downloadUrl = assetObj["browser_download_url"].toString();
                break;
            }
        }

        if (!info.version.isEmpty() && !info.downloadUrl.isEmpty()) {
            versions.append(info);
        }
    }

    return versions;
}

void CleanroomInstaller::downloadCleanroom(const QString &mcVersion, const QString &cleanroomVersion,
                                            const QString &downloadUrl, const QString &instancePath)
{
    if (m_isInstalling) {
        emit installFailed(tr("安装器正忙"));
        return;
    }

    m_isInstalling = true;
    m_isCancelled = false;
    m_currentMcVersion = mcVersion;
    m_currentCleanroomVersion = cleanroomVersion;
    m_currentInstancePath = instancePath;
    m_downloadedBytes = 0;
    m_totalBytes = 0;

    m_tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + "/cleanroom_install_" + QString::number(QDateTime::currentMSecsSinceEpoch());

    QDir().mkpath(m_tempDir);

    QString tempFilePath = m_tempDir + "/cleanroom-" + cleanroomVersion + ".jar";

    emit statusChanged(tr("正在下载Cleanroom安装包..."));
    updateTaskStatus(tr("正在下载Cleanroom安装包"));

    if (!downloadFile(downloadUrl, tempFilePath)) {
        emit installFailed(tr("下载Cleanroom安装包失败"));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    // 创建版本目录
    QString versionPath = instancePath + "/versions/Cleanroom " + mcVersion + "-" + cleanroomVersion;
    QDir().mkpath(versionPath);

    // 复制JAR文件
    QString destJarPath = versionPath + "/cleanroom-" + cleanroomVersion + ".jar";
    if (!QFile::copy(tempFilePath, destJarPath)) {
        emit installFailed(tr("复制安装文件失败"));
        m_isInstalling = false;
        return;
    }

    // 生成版本JSON
    QJsonObject versionJson;
    versionJson["id"] = "Cleanroom " + mcVersion + "-" + cleanroomVersion;
    versionJson["inheritsFrom"] = mcVersion;
    versionJson["type"] = "release";

    QJsonObject arguments;
    QJsonArray gameArgs;
    arguments["game"] = gameArgs;
    QJsonArray jvmArgs;
    jvmArgs.append("-Djava.net.preferIPv4Stack=true");
    arguments["jvm"] = jvmArgs;
    versionJson["arguments"] = arguments;

    QJsonArray libraries;
    QJsonObject cleanroomLib;
    cleanroomLib["name"] = "net.cleanroom:cleanroom:" + cleanroomVersion;
    cleanroomLib["url"] = "https://maven.cleanroommc.com/";
    libraries.append(cleanroomLib);

    versionJson["libraries"] = libraries;

    QString jsonFilePath = versionPath + "/Cleanroom " + mcVersion + "-" + cleanroomVersion + ".json";
    QFile jsonFile(jsonFilePath);
    if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        jsonFile.write(QJsonDocument(versionJson).toJson());
        jsonFile.close();
    }

    cleanupTempFiles();
    m_isInstalling = false;

    emit installProgressUpdated(100, tr("安装完成"));
    emit installCompleted("Cleanroom " + mcVersion + "-" + cleanroomVersion);
}

bool CleanroomInstaller::downloadFile(const QString &url, const QString &filePath, int redirectDepth)
{
    if (redirectDepth > 5) {
        return false;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");

    QEventLoop loop;
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this, &CleanroomInstaller::updateTaskProgress);
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

void CleanroomInstaller::cancelInstall()
{
    m_isCancelled = true;

    if (m_currentReply) {
        m_currentReply->abort();
    }

    cleanupTempFiles();
    m_isInstalling = false;

    emit installCancelled();
}

bool CleanroomInstaller::isInstalling() const
{
    return m_isInstalling;
}

void CleanroomInstaller::cleanupTempFiles()
{
    if (!m_tempDir.isEmpty()) {
        QDir dir(m_tempDir);
        if (dir.exists()) {
            dir.removeRecursively();
        }
        m_tempDir.clear();
    }
}
