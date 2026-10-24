/**
 * @file   OptiFineInstaller.cpp
 * @brief  OptiFine安装器类实现
 * @author BlockBox Team
 * @date   2026-05-28
 */

#include "OptiFineInstaller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include "utils/DownloadTaskManager.h"

namespace {

// 以事件循环方式等待进程退出：等待期间本线程事件照常派发（本函数可能运行在
// 后台线程，GUI 线程不受影响）；超时或取消时强制结束进程并返回 false
bool waitForProcessFinished(QProcess &process, int timeoutMs, const std::atomic<bool> *cancelFlag = nullptr)
{
    QEventLoop loop;
    QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(timeoutMs);

    QTimer cancelPoll;
    if (cancelFlag) {
        QObject::connect(&cancelPoll, &QTimer::timeout, &loop, [&loop, cancelFlag]() {
            if (cancelFlag->load()) {
                loop.quit();
            }
        });
        cancelPoll.start(500);
    }

    loop.exec();
    cancelPoll.stop();

    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return true;
}

} // namespace

OptiFineInstaller* OptiFineInstaller::m_instance = nullptr;
QMutex OptiFineInstaller::m_instanceMutex;

OptiFineInstaller::OptiFineInstaller()
    : m_networkManager(new QNetworkAccessManager(this))
    , m_downloadSource(OptiFineDownloadSource::BMCL)
    , m_currentReply(nullptr)
    , m_isInstalling(false)
    , m_isCancelled(false)
    , m_installerFileName("")
    , m_totalBytes(0)
    , m_downloadedBytes(0)
{
}

OptiFineInstaller::~OptiFineInstaller()
{
    cleanupTempFiles();
}

OptiFineInstaller* OptiFineInstaller::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new OptiFineInstaller();
        }
    }
    return m_instance;
}

void OptiFineInstaller::setDownloadSource(OptiFineDownloadSource source)
{
    m_downloadSource = source;
}

OptiFineDownloadSource OptiFineInstaller::downloadSource() const
{
    return m_downloadSource;
}

QString OptiFineInstaller::getBaseUrl() const
{
    switch (m_downloadSource) {
    case OptiFineDownloadSource::Official:
        return "https://optifine.cn";
    case OptiFineDownloadSource::BMCL:
    default:
        return "https://bmclapi2.bangbang93.com";
    }
}

void OptiFineInstaller::setCurrentTaskId(const QString &taskId)
{
    m_currentTaskId = taskId;
}

QString OptiFineInstaller::currentTaskId() const
{
    return m_currentTaskId;
}

void OptiFineInstaller::updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    int downloadPercent = 0;
    if (bytesTotal > 0) {
        downloadPercent = static_cast<int>((bytesReceived * 100) / bytesTotal);
    }

    DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, downloadPercent);
}

void OptiFineInstaller::updateTaskStatus(const QString &status)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    DownloadTaskManager::instance()->updateTaskStep(m_currentTaskId, status);
}

QString OptiFineInstaller::getOptiFineDownloadUrl(const QString &mcVersion, const QString &type,
                                                   const QString &patch)
{
    QString normalizedMc = normalizeMcVersion(mcVersion);
    return QString("%1/optifine/%2/%3/%4").arg(getBaseUrl(), normalizedMc, type, patch);
}

QString OptiFineInstaller::normalizeMcVersion(const QString &mcVersion)
{
    if (mcVersion == "1.8") return "1.8.0";
    if (mcVersion == "1.9") return "1.9.0";
    return mcVersion;
}

QString OptiFineInstaller::resolveOfficialDownloadUrl(const QString &fileName)
{
    QNetworkRequest request;
    request.setUrl(QUrl("https://optifine.net/adloadx?f=" + fileName));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
    request.setRawHeader("Accept", "text/html");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.5");
    request.setRawHeader("X-Requested-With", "XMLHttpRequest");
    // 30 秒无数据传输即超时，避免官网解析页不可达时嵌套事件循环永不退出导致界面卡死
    request.setTransferTimeout(30000);

    // 可能在后台线程执行，QNAM 必须与调用线程同亲和，用局部实例
    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.get(request);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QString resultUrl;
    if (reply->error() == QNetworkReply::NoError) {
        QString html = QString::fromUtf8(reply->readAll());
        QRegularExpression regex("downloadx\\?f=[^\"']+");
        QRegularExpressionMatch match = regex.match(html);
        if (match.hasMatch()) {
            resultUrl = "https://optifine.net/" + match.captured();
        }
    }
    reply->deleteLater();
    return resultUrl;
}

void OptiFineInstaller::downloadOptiFineInstaller(const QString &mcVersion, const QString &optiFineVersion,
                                                   const QString &installerFileName, const QString &instancePath)
{
    if (m_isInstalling) {
        emit installFailed(tr("安装器正忙"));
        return;
    }

    m_isInstalling = true;
    m_isCancelled = false;
    m_currentMcVersion = mcVersion;
    m_currentOptiFineVersion = optiFineVersion;
    m_currentInstancePath = instancePath;
    m_installerFileName = installerFileName;
    m_downloadedBytes = 0;
    m_totalBytes = 0;

    QStringList parts = optiFineVersion.split("_");
    if (parts.size() < 2) {
        emit installFailed(tr("无效的OptiFine版本格式: %1").arg(optiFineVersion));
        m_isInstalling = false;
        return;
    }

    QString patch = parts.takeLast();
    QString type = parts.join("_");
    m_currentType = type;
    m_currentPatch = patch;

    m_tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + "/optifine_install_" + QString::number(QDateTime::currentMSecsSinceEpoch());

    QDir().mkpath(m_tempDir);

    QString downloadUrl;
    if (m_downloadSource == OptiFineDownloadSource::Official) {
        downloadUrl = resolveOfficialDownloadUrl(installerFileName);
    } else {
        downloadUrl = getOptiFineDownloadUrl(mcVersion, type, patch);
    }

    QString tempFilePath = m_tempDir + "/OptiFine-" + optiFineVersion + "-" + mcVersion + ".jar";

    emit statusChanged(tr("正在下载OptiFine安装包..."));
    updateTaskStatus(tr("正在下载OptiFine安装包"));

    if (!downloadFile(downloadUrl, tempFilePath)) {
        emit installFailed(tr("下载OptiFine安装包失败"));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    installOptiFine(tempFilePath, instancePath);
}

void OptiFineInstaller::installOptiFine(const QString &installerPath, const QString &instancePath)
{
    emit statusChanged(tr("正在分析安装包..."));
    updateTaskStatus(tr("正在分析安装包"));
    emit installProgressUpdated(5, tr("分析安装包"));
    DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, 5);

    QString launchWrapperVersion = "1.12";

    if (jarEntryExists(installerPath, "launchwrapper-of.txt")) {
        QString versionContent = readJarEntry(installerPath, "launchwrapper-of.txt");
        if (!versionContent.isEmpty()) {
            launchWrapperVersion = versionContent.trimmed();
        }
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    emit statusChanged(tr("正在创建版本目录..."));
    updateTaskStatus(tr("正在创建版本目录"));
    emit installProgressUpdated(15, tr("创建版本目录"));
    DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, 15);

    QString versionPath = createVersionDirectory(instancePath, m_currentMcVersion, m_currentType, m_currentPatch);
    if (versionPath.isEmpty()) {
        emit installFailed(tr("创建版本目录失败"));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    emit statusChanged(tr("正在复制LaunchWrapper..."));
    updateTaskStatus(tr("正在复制LaunchWrapper"));
    emit installProgressUpdated(30, tr("复制LaunchWrapper"));
    DownloadTaskManager::instance()->updateTaskStageProgress(m_currentTaskId, DownloadStage::LoaderInstall, 30);

    copyLaunchWrapper(installerPath, instancePath, launchWrapperVersion);

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    emit statusChanged(tr("正在复制OptiFine库..."));
    updateTaskStatus(tr("正在复制OptiFine库"));
    emit installProgressUpdated(45, tr("复制OptiFine库"));
    DownloadTaskManager::instance()->updateTaskStageProgress(m_currentTaskId, DownloadStage::LoaderInstall, 45);

    copyOptiFineLibrary(installerPath, instancePath, m_currentMcVersion, m_currentType, m_currentPatch);

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    bool hasPatcher = jarEntryExists(installerPath, "optifine/Patcher.class");

    if (hasPatcher) {
        emit statusChanged(tr("正在运行OptiFine修补器..."));
        updateTaskStatus(tr("正在运行OptiFine修补器"));
        emit installProgressUpdated(55, tr("运行OptiFine修补器"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, 55);

        QString gameJarPath = instancePath + "/versions/" + m_currentMcVersion + "/" + m_currentMcVersion + ".jar";
        QString outputLibPath = instancePath + "/libraries/optifine/OptiFine/"
                               + m_currentMcVersion + "_" + m_currentType + "_" + m_currentPatch
                               + "/optifine-OptiFine-" + m_currentMcVersion + "_" + m_currentType + "_" + m_currentPatch + ".jar";

        if (!runOptiFinePatcher(installerPath, gameJarPath, outputLibPath)) {
            if (m_isCancelled) {
                m_isInstalling = false;
                return;
            }
            emit installFailed(tr("OptiFine修补器运行失败"));
            m_isInstalling = false;
            return;
        }

        if (m_isCancelled) {
            m_isInstalling = false;
            return;
        }
    }

    emit statusChanged(tr("正在生成版本配置..."));
    updateTaskStatus(tr("正在生成版本配置"));
    emit installProgressUpdated(70, tr("生成版本配置"));
    DownloadTaskManager::instance()->updateTaskStageProgress(m_currentTaskId, DownloadStage::LoaderInstall, 70);

    QJsonObject versionJson = generateVersionJson(m_currentMcVersion, m_currentType,
                                                   m_currentPatch, launchWrapperVersion);

    QString versionId = m_currentMcVersion + "-OptiFine_" + m_currentType + "_" + m_currentPatch;
    QString jsonFilePath = versionPath + "/" + versionId + ".json";
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
    DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, 90);

    cleanupTempFiles();

    m_isInstalling = false;
    emit installProgressUpdated(100, tr("安装完成"));
    emit installCompleted(versionId);

    if (!m_currentTaskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, 90);
        DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Completed,
                                                          tr("安装完成"));
    }
}

bool OptiFineInstaller::runOptiFinePatcher(const QString &installerPath, const QString &gameJarPath,
                                           const QString &outputLibPath)
{
    QString javaPath = getJavaPath();

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    QStringList arguments;
    arguments << "-cp" << installerPath
              << "optifine.Patcher"
              << gameJarPath
              << installerPath
              << outputLibPath;

    QDir().mkpath(QFileInfo(outputLibPath).absolutePath());

    process.start(javaPath, arguments);

    if (!process.waitForStarted(30000)) {
        qWarning() << "OptiFine patcher failed to start";
        return false;
    }

    // 修补器可能运行数分钟，必须用事件循环等待，否则执行线程会被占死；
    // 期间用户取消可即时中断
    if (!waitForProcessFinished(process, 10 * 60 * 1000, &m_isCancelled)) {
        if (m_isCancelled) {
            return false;
        }
        qWarning() << "OptiFine patcher timed out (10 minutes)";
        return false;
    }

    if (process.exitCode() != 0) {
        qWarning() << "OptiFine patcher exited with code" << process.exitCode();
        qWarning() << "Patcher output:" << process.readAll();
        return false;
    }

    return true;
}

QJsonObject OptiFineInstaller::generateVersionJson(const QString &mcVersion, const QString &type,
                                                    const QString &patch, const QString &launchWrapperVersion)
{
    QJsonObject versionJson;

    QString versionId = mcVersion + "-OptiFine_" + type + "_" + patch;

    versionJson["id"] = versionId;
    versionJson["type"] = "release";
    versionJson["releaseTime"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    versionJson["time"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    versionJson["mainClass"] = "net.minecraft.launchwrapper.Launch";
    versionJson["inheritsFrom"] = mcVersion;

    QJsonObject argumentsJson;
    argumentsJson["game"] = QJsonArray({
        "--tweakClass", "optifine.OptiFineTweaker"
    });

    versionJson["arguments"] = argumentsJson;
    versionJson["jar"] = mcVersion;

    QJsonArray librariesArray;

    QJsonObject launchWrapperLib;
    launchWrapperLib["name"] = QString("net.minecraft:launchwrapper-of:%1").arg(launchWrapperVersion);
    launchWrapperLib["downloads"] = QJsonObject({
        {"artifact", QJsonObject({
            {"url", ""},
            {"path", QString("optifine/launchwrapper-of/%1/launchwrapper-of-%2.jar")
                        .arg(launchWrapperVersion, launchWrapperVersion)},
            {"sha1", ""},
            {"size", 0}
        })}
    });
    librariesArray.append(launchWrapperLib);

    QJsonObject optiFineLib;
    QString optiFineName = QString("optifine:OptiFine:%1_%2_%3").arg(mcVersion, type, patch);
    optiFineLib["name"] = optiFineName;
    optiFineLib["downloads"] = QJsonObject({
        {"artifact", QJsonObject({
            {"url", ""},
            {"path", QString("optifine/OptiFine/%1_%2_%3/optifine-OptiFine-%4_%5_%6.jar")
                        .arg(mcVersion, type, patch, mcVersion, type, patch)},
            {"sha1", ""},
            {"size", 0}
        })}
    });
    librariesArray.append(optiFineLib);

    versionJson["libraries"] = librariesArray;

    versionJson["minecraftArguments"] = "--username ${auth_player_name} --version ${version_name} --gameDir ${game_directory} --assetsDir ${assets_root} --assetIndex ${assets_index_name} --uuid ${auth_uuid} --accessToken ${auth_access_token} --userType ${user_type} --versionType ${version_type} --tweakClass optifine.OptiFineTweaker";

    return versionJson;
}

QString OptiFineInstaller::createVersionDirectory(const QString &instancePath, const QString &mcVersion,
                                                    const QString &type, const QString &patch)
{
    QString versionId = mcVersion + "-OptiFine_" + type + "_" + patch;
    QString versionPath = instancePath + "/versions/" + versionId;

    QDir versionDir(versionPath);
    if (!versionDir.exists()) {
        if (!versionDir.mkpath(".")) {
            return QString();
        }
    }

    return versionPath;
}

void OptiFineInstaller::copyLaunchWrapper(const QString &installerPath, const QString &instancePath,
                                           const QString &launchWrapperVersion)
{
    QString launchWrapperDir = instancePath + "/libraries/optifine/launchwrapper-of/"
                              + launchWrapperVersion;
    QDir().mkpath(launchWrapperDir);

    QString destPath = launchWrapperDir + "/launchwrapper-of-" + launchWrapperVersion + ".jar";

    QString launchWrapperEntry = "launchwrapper-of-" + launchWrapperVersion + ".jar";
    if (jarEntryExists(installerPath, launchWrapperEntry)) {
        extractJarEntry(installerPath, launchWrapperEntry, destPath);
        return;
    }

    if (jarEntryExists(installerPath, "launchwrapper-2.0.jar")) {
        extractJarEntry(installerPath, "launchwrapper-2.0.jar", destPath);
        return;
    }
}

void OptiFineInstaller::copyOptiFineLibrary(const QString &installerPath, const QString &instancePath,
                                             const QString &mcVersion, const QString &type, const QString &patch)
{
    QString optiFineDir = instancePath + "/libraries/optifine/OptiFine/"
                         + mcVersion + "_" + type + "_" + patch;
    QDir().mkpath(optiFineDir);

    QString destPath = optiFineDir + "/optifine-OptiFine-" + mcVersion + "_" + type + "_" + patch + ".jar";

    bool hasPatcher = jarEntryExists(installerPath, "optifine/Patcher.class");

    if (hasPatcher) {
        return;
    }

    QFile sourceFile(installerPath);
    if (sourceFile.exists()) {
        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }
        sourceFile.copy(destPath);
    }
}

bool OptiFineInstaller::downloadFile(const QString &url, const QString &filePath,
                                      const QString &expectedSha1, int redirectDepth)
{
    if (redirectDepth > 10) {
        return false;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
    // 60 秒无数据传输即超时，避免连接停滞时嵌套事件循环永不退出导致界面卡死
    request.setTransferTimeout(60000);

    // 可能在后台线程执行，QNAM 必须与调用线程同亲和，用局部实例，
    // 离开作用域时连同其子 reply 一起销毁
    QNetworkAccessManager nam;

    QNetworkReply *reply = nam.get(request);
    m_currentReply = reply;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        reply->abort();
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    QEventLoop loop;
    // 进度上报挂在 this（GUI 线程）上跨线程排队；数据落盘保持直接连接，
    // 在执行线程内写文件
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 bytesReceived, qint64 bytesTotal) {
        Q_UNUSED(reply);
        this->m_totalBytes = bytesTotal;
        this->m_downloadedBytes = bytesReceived;
        emit downloadProgressUpdated(bytesReceived, bytesTotal);
        updateTaskProgress(bytesReceived, bytesTotal);
    });

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::readyRead, [reply, &file]() {
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
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errorMsg = QString("下载失败 (HTTP %1): %2").arg(statusCode).arg(url);
        qWarning() << errorMsg;
        emit installFailed(errorMsg);
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }
}

QString OptiFineInstaller::calculateSha1(const QString &filePath)
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

bool OptiFineInstaller::verifySha1(const QString &filePath, const QString &expectedSha1)
{
    QString actualSha1 = calculateSha1(filePath);
    return actualSha1.toLower() == expectedSha1.toLower();
}

void OptiFineInstaller::cancelInstall()
{
    if (!m_isInstalling) {
        return;
    }

    m_isCancelled = true;
    m_isInstalling = false;

    // reply 可能存活在执行安装的后台线程，跨线程直接 abort 不安全；
    // 通过排队调用投递到 reply 所属线程执行
    if (m_currentReply) {
        QMetaObject::invokeMethod(m_currentReply, &QNetworkReply::abort, Qt::QueuedConnection);
        m_currentReply = nullptr;
    }

    if (!m_currentTaskId.isEmpty()) {
        DownloadTaskManager::instance()->cancelTask(m_currentTaskId);
    }

    cleanupTempFiles();

    emit installCancelled();
}

bool OptiFineInstaller::isInstalling() const
{
    return m_isInstalling;
}

void OptiFineInstaller::cleanupTempFiles()
{
    if (!m_tempDir.isEmpty()) {
        QDir tempDir(m_tempDir);
        if (tempDir.exists()) {
            tempDir.removeRecursively();
        }
        m_tempDir.clear();
    }
}

QString OptiFineInstaller::getJavaPath() const
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

QString OptiFineInstaller::readJarEntry(const QString &jarPath, const QString &entryPath)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    QStringList arguments;
    arguments << "xf" << jarPath << entryPath;
    process.setWorkingDirectory(m_tempDir);
    process.start("jar", arguments);

    if (!process.waitForStarted(10000)) {
        qWarning() << "JAR process failed to start for reading" << entryPath;
        return QString();
    }

    waitForProcessFinished(process, 60000);

    QString extractedEntryPath = m_tempDir + "/" + entryPath;
    QFile tempFile(extractedEntryPath);
    QString content;

    if (tempFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        content = QString::fromUtf8(tempFile.readAll());
        tempFile.close();
    }

    if (tempFile.exists()) {
        tempFile.remove();
    }

    QDir dir = QFileInfo(extractedEntryPath).absoluteDir();
    if (dir.exists() && dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
        dir.rmdir(".");
    }

    return content;
}

bool OptiFineInstaller::extractJarEntry(const QString &jarPath, const QString &entryPath, const QString &destPath)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    QStringList arguments;
    arguments << "xf" << jarPath << entryPath;
    process.setWorkingDirectory(m_tempDir);
    process.start("jar", arguments);

    if (!process.waitForStarted(10000)) {
        qWarning() << "JAR process failed to start for extracting" << entryPath;
        return false;
    }

    waitForProcessFinished(process, 60000);

    QString sourcePath = m_tempDir + "/" + entryPath;
    QFile extractedFile(sourcePath);
    if (!extractedFile.exists()) {
        qWarning() << "Failed to extract" << entryPath << "from" << jarPath;
        return false;
    }

    QDir().mkpath(QFileInfo(destPath).absolutePath());

    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    bool result = extractedFile.copy(destPath);
    extractedFile.remove();

    QString extractedEntryPath = m_tempDir + "/" + entryPath;
    QDir().rmdir(QFileInfo(extractedEntryPath).absolutePath());

    return result;
}

bool OptiFineInstaller::jarEntryExists(const QString &jarPath, const QString &entryPath)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    QStringList arguments;
    arguments << "tf" << jarPath;
    process.start("jar", arguments);

    if (!process.waitForStarted(10000)) {
        qWarning() << "JAR process failed to start for listing entries";
        return false;
    }

    waitForProcessFinished(process, 60000);

    QString output = QString::fromUtf8(process.readAllStandardOutput());
    QStringList lines = output.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        if (line.trimmed() == entryPath) {
            return true;
        }
    }

    return false;
}