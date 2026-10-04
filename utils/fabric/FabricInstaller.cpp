/**
 * @file   FabricInstaller.cpp
 * @brief  Fabric安装器类实现
 * @author BlockBox Team
 * @date   2026-05-28
 */

#include "FabricInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>

#include "utils/DownloadTaskManager.h"

FabricInstaller* FabricInstaller::m_instance = nullptr;
QMutex FabricInstaller::m_instanceMutex;

FabricInstaller::FabricInstaller()
    : m_networkManager(new QNetworkAccessManager(this))
    , m_downloadSource(FabricDownloadSource::BMCL)
    , m_currentReply(nullptr)
    , m_isInstalling(false)
    , m_isCancelled(false)
    , m_totalBytes(0)
    , m_downloadedBytes(0)
{
}

FabricInstaller::~FabricInstaller()
{
    cleanupTempFiles();
}

FabricInstaller* FabricInstaller::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new FabricInstaller();
        }
    }
    return m_instance;
}

void FabricInstaller::setDownloadSource(FabricDownloadSource source)
{
    m_downloadSource = source;
}

FabricDownloadSource FabricInstaller::downloadSource() const
{
    return m_downloadSource;
}

QString FabricInstaller::getBaseUrl() const
{
    switch (m_downloadSource) {
    case FabricDownloadSource::Official:
        return "https://meta.fabricmc.cn";
    case FabricDownloadSource::BMCL:
    default:
        return "https://bmclapi2.bangbang93.com";
    }
}

void FabricInstaller::setCurrentTaskId(const QString &taskId)
{
    m_currentTaskId = taskId;
}

QString FabricInstaller::currentTaskId() const
{
    return m_currentTaskId;
}

void FabricInstaller::updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    int progress = 0;
    if (bytesTotal > 0) {
        progress = static_cast<int>((bytesReceived * 100) / bytesTotal);
    }

    DownloadTaskManager::instance()->updateTaskProgress(m_currentTaskId, progress, bytesReceived, bytesTotal);
}

void FabricInstaller::updateTaskStatus(const QString &status)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    DownloadTaskManager::instance()->updateTaskStep(m_currentTaskId, status);
}

QList<FabricVersionInfo> FabricInstaller::getFabricVersions(const QString &mcVersion)
{
    emit statusChanged(tr("正在获取Fabric版本列表..."));
    updateTaskStatus(tr("正在获取Fabric版本列表"));

    QString url = QString("%1/v2/versions/loader/%2").arg(getBaseUrl(), mcVersion);

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");

    QEventLoop loop;
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    QList<FabricVersionInfo> versions;

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = tr("获取Fabric版本列表失败: %1").arg(reply->errorString());
        emit versionListFetchFailed(errorMsg);
        reply->deleteLater();
        return versions;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    versions = parseVersionList(QString::fromUtf8(responseData));

    if (versions.isEmpty()) {
        emit versionListFetchFailed(tr("未找到可用的Fabric版本"));
    } else {
        emit versionListFetched(versions);
    }

    return versions;
}

QList<FabricVersionInfo> FabricInstaller::parseVersionList(const QString &jsonResponse)
{
    QList<FabricVersionInfo> versions;

    QJsonDocument doc = QJsonDocument::fromJson(jsonResponse.toUtf8());
    if (!doc.isArray()) {
        return versions;
    }

    QJsonArray versionArray = doc.array();

    for (const QJsonValue &value : versionArray) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject obj = value.toObject();
        QJsonObject loader = obj.value("loader").toObject();
        QJsonObject launcherMeta = obj.value("launcherMeta").toObject();

        FabricVersionInfo info;
        info.fabricVersion = loader.value("version").toString();
        info.minecraftVersion = obj.value("game").toString();
        info.hash = loader.value("hash").toString();
        info.build = loader.value("build").toString();

        QJsonObject versionInfo = launcherMeta.value("version").toObject();
        info.launcherMetaVersion = versionInfo.value("version").toString();

        if (!info.fabricVersion.isEmpty() && !info.minecraftVersion.isEmpty()) {
            versions.append(info);
        }
    }

    return versions;
}

void FabricInstaller::downloadFabricInstaller(const QString &mcVersion, const QString &fabricVersion,
                                              const QString &instancePath)
{
    if (m_isInstalling) {
        emit installFailed(tr("安装器正忙"));
        return;
    }

    m_isInstalling = true;
    m_isCancelled = false;
    m_currentMcVersion = mcVersion;
    m_currentFabricVersion = fabricVersion;
    m_currentInstancePath = instancePath;
    m_downloadedBytes = 0;
    m_totalBytes = 0;

    m_tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + "/fabric_install_" + QString::number(QDateTime::currentMSecsSinceEpoch());

    QDir().mkpath(m_tempDir);

    emit statusChanged(tr("正在获取Fabric版本信息..."));
    updateTaskStatus(tr("正在获取Fabric版本信息"));

    QList<FabricVersionInfo> versions = getFabricVersions(mcVersion);

    FabricVersionInfo targetVersion;
    bool found = false;

    for (const FabricVersionInfo &version : versions) {
        if (version.fabricVersion == fabricVersion) {
            targetVersion = version;
            found = true;
            break;
        }
    }

    if (!found) {
        emit installFailed(tr("未找到指定版本的Fabric: %1").arg(fabricVersion));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    QString downloadUrl = getFabricDownloadUrl(mcVersion, fabricVersion, targetVersion.hash);
    QString tempFilePath = m_tempDir + "/fabric-loader-" + fabricVersion + "-" + mcVersion + ".jar";

    emit statusChanged(tr("正在下载Fabric安装包..."));
    updateTaskStatus(tr("正在下载Fabric安装包"));

    if (!downloadFile(downloadUrl, tempFilePath, targetVersion.hash)) {
        emit installFailed(tr("下载Fabric安装包失败"));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    installFabric(tempFilePath, instancePath);
}

void FabricInstaller::installFabric(const QString &installerPath, const QString &instancePath)
{
    emit statusChanged(tr("正在创建版本目录..."));
    updateTaskStatus(tr("正在创建版本目录"));
    emit installProgressUpdated(10, tr("创建版本目录"));

    QString versionPath = createVersionDirectory(instancePath, m_currentMcVersion, m_currentFabricVersion);
    if (versionPath.isEmpty()) {
        emit installFailed(tr("创建版本目录失败"));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    emit statusChanged(tr("正在复制安装文件..."));
    updateTaskStatus(tr("正在复制安装文件"));
    emit installProgressUpdated(30, tr("复制安装文件"));

    if (!copyInstallerJar(installerPath, versionPath, m_currentFabricVersion)) {
        emit installFailed(tr("复制安装文件失败"));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    emit statusChanged(tr("正在生成版本配置..."));
    updateTaskStatus(tr("正在生成版本配置"));
    emit installProgressUpdated(60, tr("生成版本配置"));

    QJsonObject versionJson = generateVersionJson(installerPath, m_currentMcVersion, m_currentFabricVersion);

    QString dirName = QFileInfo(versionPath).fileName();
    QString jsonFilePath = versionPath + "/" + dirName + ".json";
    QFile jsonFile(jsonFilePath);

    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit installFailed(tr("无法创建版本配置文件"));
        m_isInstalling = false;
        return;
    }

    QJsonDocument jsonDoc(versionJson);
    jsonFile.write(jsonDoc.toJson(QJsonDocument::Indented));
    jsonFile.close();

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    emit installProgressUpdated(90, tr("完成"));

    cleanupTempFiles();

    m_isInstalling = false;
    emit installProgressUpdated(100, tr("安装完成"));
    emit installCompleted(QString("fabric-loader-%1-%2").arg(m_currentFabricVersion, m_currentMcVersion));

    if (!m_currentTaskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Completed,
                                                          tr("安装完成"));
    }
}

QString FabricInstaller::getFabricDownloadUrl(const QString &mcVersion, const QString &fabricVersion,
                                              const QString &hash)
{
    return QString("%1/v2/versions/loader/%2/%3/loader/%4/fabric-loader-%5-%6.jar")
           .arg(getBaseUrl(), mcVersion, fabricVersion, hash, fabricVersion, mcVersion);
}

QString FabricInstaller::createVersionDirectory(const QString &instancePath,
                                              const QString &mcVersion,
                                              const QString &fabricVersion)
{
    QString versionId = QString("fabric-loader-%1-%2").arg(fabricVersion, mcVersion);
    QString versionPath = instancePath + "/versions/" + versionId;

    QDir versionDir(versionPath);
    if (!versionDir.exists()) {
        if (!versionDir.mkpath(".")) {
            return QString();
        }
    }

    return versionPath;
}

bool FabricInstaller::copyInstallerJar(const QString &installerPath,
                                      const QString &versionPath,
                                      const QString &fabricVersion)
{
    QString destPath = versionPath + "/fabric-loader-" + fabricVersion + ".jar";

    QFile sourceFile(installerPath);
    if (!sourceFile.exists()) {
        return false;
    }

    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    return sourceFile.copy(destPath);
}

QJsonObject FabricInstaller::generateVersionJson(const QString &installerPath,
                                                const QString &mcVersion,
                                                const QString &fabricVersion)
{
    QJsonObject versionJson;

    versionJson["id"] = QString("fabric-loader-%1-%2").arg(fabricVersion, mcVersion);
    versionJson["type"] = "release";
    versionJson["releaseTime"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    versionJson["time"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    // 使用现代 Fabric Loader 0.11+ 的 Knot 主类（字符串格式，符合 Mojang 版本 JSON 规范）
    // 旧版 FabricClientLauncher/FabricServerLauncher 已在 0.11+ 中移除
    versionJson["mainClass"] = QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient");

    QString versionId = QString("fabric-loader-%1-%2").arg(fabricVersion, mcVersion);

    QJsonObject argumentsJson;
    argumentsJson["game"] = QJsonArray({
        "--username", "${auth_player_name}",
        "--version", "${version_name}",
        "--gameDir", "${game_directory}",
        "--assetsDir", "${assets_root}",
        "--assetIndex", "${assets_index_name}",
        "--uuid", "${auth_uuid}",
        "--accessToken", "${auth_access_token}",
        "--userType", "${user_type}",
        "--versionType", "${version_type}",
        "--launchTarget", "fabric-client"
    });

    versionJson["arguments"] = argumentsJson;

    QJsonArray librariesArray;

    QJsonObject fabricLoaderLib;
    fabricLoaderLib["name"] = QString("net.fabricmc:fabric-loader:%1").arg(fabricVersion);
    fabricLoaderLib["downloads"] = QJsonObject({
        {"artifact", QJsonObject({
            {"url", getFabricDownloadUrl(mcVersion, fabricVersion, "")},
            {"path", QString("net/fabricmc/fabric-loader/%1/fabric-loader-%2-%3.jar").arg(fabricVersion, fabricVersion, mcVersion)},
            {"sha1", ""},
            {"size", 0}
        })}
    });
    librariesArray.append(fabricLoaderLib);

    QJsonObject fabricApiLib;
    fabricApiLib["name"] = "net.fabricmc:fabric-api:0.76.0+1.19";
    fabricApiLib["url"] = "https://maven.fabricmc.net/";
    fabricApiLib["downloads"] = QJsonObject({
        {"artifact", QJsonObject({
            {"url", "https://maven.fabricmc.net/net/fabricmc/fabric-api/fabric-api-0.76.0+1.19.jar"},
            {"path", "net/fabricmc/fabric-api/fabric-api-0.76.0+1.19.jar"},
            {"sha1", ""},
            {"size", 0}
        })}
    });
    librariesArray.append(fabricApiLib);

    QJsonObject minecraftLib;
    minecraftLib["name"] = QString("com.mojang:minecraft:%1").arg(mcVersion);
    librariesArray.append(minecraftLib);

    versionJson["libraries"] = librariesArray;

    versionJson["clientVersion"] = mcVersion;
    versionJson["inheritsFrom"] = mcVersion;
    versionJson["jar"] = QJsonValue(mcVersion);
    versionJson["minecraftArguments"] = "--username ${auth_player_name} --version ${version_name} --gameDir ${game_directory} --assetsDir ${assets_root} --assetIndex ${assets_index_name} --uuid ${auth_uuid} --accessToken ${auth_access_token} --userType ${user_type} --versionType ${version_type}";

    return versionJson;
}

bool FabricInstaller::downloadFile(const QString &url, const QString &filePath,
                                  const QString &expectedSha1, int redirectDepth)
{
    if (redirectDepth > 10) {
        return false;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");

    QNetworkReply *reply = m_networkManager->get(request);
    m_currentReply = reply;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        reply->abort();
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    QEventLoop loop;
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 bytesReceived, qint64 bytesTotal) {
        Q_UNUSED(reply);
        this->m_totalBytes = bytesTotal;
        this->m_downloadedBytes = bytesReceived;
        emit downloadProgressUpdated(bytesReceived, bytesTotal);
        updateTaskProgress(bytesReceived, bytesTotal);
    });

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::readyRead, this, [reply, &file]() {
        file.write(reply->readAll());
    });

    loop.exec();

    file.close();

    if (m_isCancelled) {
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    QNetworkReply::NetworkError error = reply->error();

    if (error == QNetworkReply::NoError) {
        QVariant redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectUrl.isValid()) {
            reply->deleteLater();
            m_currentReply = nullptr;
            return downloadFile(redirectUrl.toString(), filePath, expectedSha1, redirectDepth + 1);
        }

        reply->deleteLater();
        m_currentReply = nullptr;

        if (!expectedSha1.isEmpty()) {
            if (!verifySha1(filePath, expectedSha1)) {
                QFile::remove(filePath);
                return false;
            }
        }

        return true;
    } else if (error == QNetworkReply::OperationCanceledError) {
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    } else {
        QString errorString = reply->errorString();
        reply->deleteLater();
        m_currentReply = nullptr;
        qWarning() << "Download failed:" << errorString;
        return false;
    }
}

QString FabricInstaller::calculateSha1(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    file.close();

    return hash.result().toHex();
}

bool FabricInstaller::verifySha1(const QString &filePath, const QString &expectedSha1)
{
    QString actualSha1 = calculateSha1(filePath);
    return actualSha1.toLower() == expectedSha1.toLower();
}

void FabricInstaller::cancelInstall()
{
    if (!m_isInstalling) {
        return;
    }

    m_isCancelled = true;
    m_isInstalling = false;

    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    if (!m_currentTaskId.isEmpty()) {
        DownloadTaskManager::instance()->cancelTask(m_currentTaskId);
    }

    cleanupTempFiles();

    emit installCancelled();
}

bool FabricInstaller::isInstalling() const
{
    return m_isInstalling;
}

void FabricInstaller::cleanupTempFiles()
{
    if (!m_tempDir.isEmpty()) {
        QDir tempDir(m_tempDir);
        if (tempDir.exists()) {
            tempDir.removeRecursively();
        }
        m_tempDir.clear();
    }
}

QString FabricInstaller::getJavaPath() const
{
    QString javaPath = SettingsManager::instance()->getJavaPath();

    if (javaPath.isEmpty()) {
        javaPath = qEnvironmentVariable("JAVA_HOME");
        if (!javaPath.isEmpty()) {
#ifdef Q_OS_WIN
            javaPath += "/bin/java.exe";
#else
            javaPath += "/bin/java";
#endif
        } else {
#ifdef Q_OS_WIN
            javaPath = "java.exe";
#else
            javaPath = "java";
#endif
        }
    }

    return javaPath;
}

QString FabricInstaller::getMinecraftLibrariesPath(const QString &instancePath) const
{
    return instancePath + "/libraries";
}
