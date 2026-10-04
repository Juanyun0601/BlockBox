/**
 * @file   LiteLoaderInstaller.cpp
 * @brief  LiteLoader安装器类实现
 * @author BlockBox Team
 * @date   2026-08-29
 */

#include "LiteLoaderInstaller.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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

LiteLoaderInstaller* LiteLoaderInstaller::m_instance = nullptr;
QMutex LiteLoaderInstaller::m_instanceMutex;

LiteLoaderInstaller::LiteLoaderInstaller()
    : m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
    , m_isInstalling(false)
    , m_isCancelled(false)
    , m_totalBytes(0)
    , m_downloadedBytes(0)
{
}

LiteLoaderInstaller::~LiteLoaderInstaller()
{
    cleanupTempFiles();
}

LiteLoaderInstaller* LiteLoaderInstaller::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new LiteLoaderInstaller();
        }
    }
    return m_instance;
}

void LiteLoaderInstaller::setCurrentTaskId(const QString &taskId)
{
    m_currentTaskId = taskId;
}

QString LiteLoaderInstaller::currentTaskId() const
{
    return m_currentTaskId;
}

void LiteLoaderInstaller::updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal)
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

void LiteLoaderInstaller::updateTaskStatus(const QString &status)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    DownloadTaskManager::instance()->updateTaskStep(m_currentTaskId, status);
}

QList<LiteLoaderVersionInfo> LiteLoaderInstaller::getLiteLoaderVersions(const QString &mcVersion)
{
    emit statusChanged(tr("正在获取LiteLoader版本列表..."));
    updateTaskStatus(tr("正在获取LiteLoader版本列表"));

    QString url = QString("https://dl.liteloader.com/versions/%1/json").arg(mcVersion);

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");

    QEventLoop loop;
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    QList<LiteLoaderVersionInfo> versions;

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = tr("获取LiteLoader版本列表失败: %1").arg(reply->errorString());
        emit statusChanged(errorMsg);
        reply->deleteLater();
        return versions;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject()) {
        return versions;
    }

    QJsonObject versionData = doc.object();
    QJsonObject injectors = versionData["injectors"].toObject();

    for (auto it = injectors.begin(); it != injectors.end(); ++it) {
        QJsonObject injectorObj = it.value().toObject();
        LiteLoaderVersionInfo info;
        info.version = injectorObj["version"].toString();
        info.mcVersion = mcVersion;
        info.downloadUrl = injectorObj["url"].toString();
        info.md5 = injectorObj["md5"].toString();

        if (!info.version.isEmpty() && !info.downloadUrl.isEmpty()) {
            versions.append(info);
        }
    }

    return versions;
}

void LiteLoaderInstaller::downloadLiteLoader(const QString &mcVersion, const QString &liteLoaderVersion,
                                              const QString &instancePath)
{
    if (m_isInstalling) {
        emit installFailed(tr("安装器正忙"));
        return;
    }

    m_isInstalling = true;
    m_isCancelled = false;
    m_currentMcVersion = mcVersion;
    m_currentLiteLoaderVersion = liteLoaderVersion;
    m_currentInstancePath = instancePath;
    m_downloadedBytes = 0;
    m_totalBytes = 0;

    m_tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + "/liteloader_install_" + QString::number(QDateTime::currentMSecsSinceEpoch());

    QDir().mkpath(m_tempDir);

    emit statusChanged(tr("正在获取LiteLoader版本信息..."));
    updateTaskStatus(tr("正在获取LiteLoader版本信息"));

    QList<LiteLoaderVersionInfo> versions = getLiteLoaderVersions(mcVersion);

    LiteLoaderVersionInfo targetVersion;
    bool found = false;

    for (const LiteLoaderVersionInfo &version : versions) {
        if (version.version == liteLoaderVersion) {
            targetVersion = version;
            found = true;
            break;
        }
    }

    if (!found) {
        emit installFailed(tr("未找到指定版本的LiteLoader: %1").arg(liteLoaderVersion));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    QString downloadUrl = targetVersion.downloadUrl;
    QString tempFilePath = m_tempDir + "/liteloader-" + liteLoaderVersion + ".jar";

    emit statusChanged(tr("正在下载LiteLoader安装包..."));
    updateTaskStatus(tr("正在下载LiteLoader安装包"));

    if (!downloadFile(downloadUrl, tempFilePath, targetVersion.md5)) {
        emit installFailed(tr("下载LiteLoader安装包失败"));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    // 创建版本目录
    QString versionPath = instancePath + "/versions/LiteLoader " + mcVersion + "-" + liteLoaderVersion;
    QDir().mkpath(versionPath);

    // 复制JAR文件
    QString destJarPath = versionPath + "/liteloader-" + liteLoaderVersion + ".jar";
    if (!QFile::copy(tempFilePath, destJarPath)) {
        emit installFailed(tr("复制安装文件失败"));
        m_isInstalling = false;
        return;
    }

    // 生成版本JSON
    QJsonObject versionJson;
    versionJson["id"] = "LiteLoader " + mcVersion + "-" + liteLoaderVersion;
    versionJson["inheritsFrom"] = mcVersion;
    versionJson["type"] = "release";

    QJsonObject mainClass;
    mainClass["artifact"] = "net.minecraft.launchwrapper.Launch";
    versionJson["mainClass"] = mainClass;

    QJsonArray libraries;
    QJsonObject liteloaderLib;
    liteloaderLib["name"] = "com.mumfrey:liteloader:" + liteLoaderVersion;
    liteloaderLib["url"] = "https://dl.liteloader.com/versions/";
    libraries.append(liteloaderLib);

    QJsonObject launchwrapperLib;
    launchwrapperLib["name"] = "net.minecraft:launchwrapper:1.12";
    launchwrapperLib["url"] = "https://libraries.minecraft.net/";
    libraries.append(launchwrapperLib);

    versionJson["libraries"] = libraries;

    QString jsonFilePath = versionPath + "/LiteLoader " + mcVersion + "-" + liteLoaderVersion + ".json";
    QFile jsonFile(jsonFilePath);
    if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        jsonFile.write(QJsonDocument(versionJson).toJson());
        jsonFile.close();
    }

    cleanupTempFiles();
    m_isInstalling = false;

    emit installProgressUpdated(100, tr("安装完成"));
    emit installCompleted("LiteLoader " + mcVersion + "-" + liteLoaderVersion);
}

bool LiteLoaderInstaller::downloadFile(const QString &url, const QString &filePath,
                                        const QString &expectedMd5, int redirectDepth)
{
    if (redirectDepth > 5) {
        return false;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");

    QEventLoop loop;
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this, &LiteLoaderInstaller::updateTaskProgress);
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
                return downloadFile(redirectUrl, filePath, expectedMd5, redirectDepth + 1);
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

    if (!expectedMd5.isEmpty()) {
        return verifyMd5(filePath, expectedMd5);
    }

    return true;
}

QString LiteLoaderInstaller::calculateMd5(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!hash.addData(&file)) {
        return QString();
    }

    return hash.result().toHex();
}

bool LiteLoaderInstaller::verifyMd5(const QString &filePath, const QString &expectedMd5)
{
    QString actualMd5 = calculateMd5(filePath);
    return actualMd5.toLower() == expectedMd5.toLower();
}

void LiteLoaderInstaller::cancelInstall()
{
    m_isCancelled = true;

    if (m_currentReply) {
        m_currentReply->abort();
    }

    cleanupTempFiles();
    m_isInstalling = false;

    emit installCancelled();
}

bool LiteLoaderInstaller::isInstalling() const
{
    return m_isInstalling;
}

void LiteLoaderInstaller::cleanupTempFiles()
{
    if (!m_tempDir.isEmpty()) {
        QDir dir(m_tempDir);
        if (dir.exists()) {
            dir.removeRecursively();
        }
        m_tempDir.clear();
    }
}
