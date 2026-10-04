/**
 * @file   NeoForgeInstaller.cpp
 * @brief  NeoForge安装器类实现
 * @author BlockBox Team
 * @date   2026-05-29
 */

#include "NeoForgeInstaller.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUuid>
#include <QUrl>

#include "DownloadTaskManager.h"

NeoForgeInstaller* NeoForgeInstaller::m_instance = nullptr;
QMutex NeoForgeInstaller::m_instanceMutex;

NeoForgeInstaller::NeoForgeInstaller()
    : m_networkManager(new QNetworkAccessManager(this))
    , m_downloadSource(NeoForgeDownloadSource::BMCL)
    , m_currentReply(nullptr)
    , m_currentProcess(nullptr)
    , m_isInstalling(false)
    , m_isCancelled(false)
    , m_isDownloadPhase(true)
    , m_totalBytes(0)
    , m_downloadedBytes(0)
{
}

NeoForgeInstaller::~NeoForgeInstaller()
{
    cleanupTempFiles();
}

NeoForgeInstaller* NeoForgeInstaller::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new NeoForgeInstaller();
        }
    }
    return m_instance;
}

void NeoForgeInstaller::setDownloadSource(NeoForgeDownloadSource source)
{
    m_downloadSource = source;
}

NeoForgeDownloadSource NeoForgeInstaller::downloadSource() const
{
    return m_downloadSource;
}

void NeoForgeInstaller::setCurrentTaskId(const QString &taskId)
{
    m_currentTaskId = taskId;
}

QString NeoForgeInstaller::currentTaskId() const
{
    return m_currentTaskId;
}

void NeoForgeInstaller::updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal)
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

void NeoForgeInstaller::updateTaskStatus(const QString &status)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    DownloadTaskManager::instance()->updateTaskStep(m_currentTaskId, status);
}

QString NeoForgeInstaller::getNeoForgeDownloadUrl(const QString &forgeVersion)
{
    switch (m_downloadSource) {
    case NeoForgeDownloadSource::BMCL:
        return QString("https://bmclapi2.bangbang93.com/neoforge/version/%1/download/installer.jar")
               .arg(forgeVersion);
    case NeoForgeDownloadSource::Official:
    default:
        return QString("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/neoforge-%2-installer.jar")
               .arg(forgeVersion, forgeVersion);
    }
}

void NeoForgeInstaller::downloadNeoForgeInstaller(const QString &mcVersion, const QString &forgeVersion,
                                                   const QString &instancePath, const QString &instanceName)
{
    if (m_isInstalling) {
        emit installFailed(tr("安装器正忙，请等待当前任务完成"));
        return;
    }

    m_currentMcVersion = mcVersion;
    m_currentForgeVersion = forgeVersion;
    m_currentInstancePath = instancePath;
    m_isInstalling = true;
    m_isCancelled = false;
    m_totalBytes = 0;
    m_downloadedBytes = 0;

    if (m_currentTaskId.isEmpty()) {
        QString taskName = instanceName;
        if (taskName.isEmpty()) {
            taskName = instancePath.split("/").last();
            if (taskName.isEmpty()) {
                taskName = QString("NeoForge %1").arg(forgeVersion);
            }
        }
        QString taskId = DownloadTaskManager::instance()->addTask(
            taskName, instancePath, QString("NeoForge %1").arg(forgeVersion));
        m_currentTaskId = taskId;
    }

    DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Downloading,
                                                       tr("开始下载NeoForge"));

    emit statusChanged(tr("开始下载 NeoForge %1...").arg(forgeVersion));
    updateTaskStatus(tr("正在准备下载"));

    m_tempDir = QDir::tempPath() + "/BlockBox_NeoForge_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir tempDir(m_tempDir);
    if (!tempDir.exists()) {
        tempDir.mkpath(".");
    }

    QString downloadUrl = getNeoForgeDownloadUrl(forgeVersion);
    QString installerPath = m_tempDir + "/neoforge-installer.jar";

    emit statusChanged(tr("正在下载NeoForge安装包..."));
    updateTaskStatus(tr("正在下载安装包"));

    m_isDownloadPhase = true;

    if (!downloadFile(downloadUrl, installerPath)) {
        emit installFailed(tr("下载NeoForge安装包失败"));
        m_isInstalling = false;
        cleanupTempFiles();
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    installNeoForge(installerPath, instancePath);
}

void NeoForgeInstaller::installNeoForge(const QString &installerPath, const QString &instancePath)
{
    emit statusChanged(tr("正在启动NeoForge安装程序..."));
    updateTaskStatus(tr("正在启动NeoForge安装程序"));
    emit installProgressUpdated(10, tr("启动安装程序"));

    m_isDownloadPhase = false;
    DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, 30);

    QString javaPath = getJavaPath();
    if (javaPath.isEmpty()) {
        emit installFailed(tr("未找到Java运行环境"));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    m_currentProcess = new QProcess(this);

    connect(m_currentProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        QString output = QString::fromUtf8(m_currentProcess->readAllStandardOutput());
        qDebug() << "[NeoForgeInstaller]" << output.trimmed();

        if (output.contains("Installing")) {
            emit installProgressUpdated(50, tr("正在安装NeoForge..."));
            updateTaskStatus(tr("正在安装NeoForge"));
        }
    });

    connect(m_currentProcess, &QProcess::readyReadStandardError, this, [this]() {
        QString errorOutput = QString::fromUtf8(m_currentProcess->readAllStandardError());
        if (!errorOutput.isEmpty()) {
            qWarning() << "[NeoForgeInstaller]" << errorOutput.trimmed();
        }
    });

    connect(m_currentProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, instancePath](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus);

        if (m_isCancelled) {
            return;
        }

        if (exitCode == 0) {
            emit installProgressUpdated(90, tr("安装完成"));
            updateTaskStatus(tr("安装完成"));
            DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, 90);

            cleanupTempFiles();

            m_isInstalling = false;

            QString versionId = m_currentMcVersion + "-NeoForge" + m_currentForgeVersion;
            emit installProgressUpdated(100, tr("安装完成"));
            emit installCompleted(versionId);

            if (!m_currentTaskId.isEmpty()) {
                DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Completed,
                                                                   tr("安装完成"));
                DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, 100);
            }
        } else {
            m_isInstalling = false;
            emit installFailed(tr("NeoForge安装程序异常退出（错误码: %1）").arg(exitCode));

            if (!m_currentTaskId.isEmpty()) {
                DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Failed,
                                                                   tr("安装失败"));
            }
        }
    });

    emit statusChanged(tr("正在运行NeoForge安装程序..."));
    updateTaskStatus(tr("正在运行NeoForge安装程序"));
    emit installProgressUpdated(30, tr("运行安装程序"));

    QStringList args;
    args << "-jar" << installerPath << "--installClient" << instancePath;

    m_currentProcess->start(javaPath, args);

    if (!m_currentProcess->waitForStarted(10000)) {
        emit installFailed(tr("无法启动NeoForge安装程序"));
        m_isInstalling = false;
        return;
    }
}

void NeoForgeInstaller::cancelInstall()
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

    if (m_currentProcess && m_currentProcess->state() != QProcess::NotRunning) {
        m_currentProcess->terminate();
        if (!m_currentProcess->waitForFinished(3000)) {
            m_currentProcess->kill();
        }
    }

    if (!m_currentTaskId.isEmpty()) {
        DownloadTaskManager::instance()->cancelTask(m_currentTaskId);
    }

    cleanupTempFiles();

    emit installCancelled();
}

bool NeoForgeInstaller::isInstalling() const
{
    return m_isInstalling;
}

bool NeoForgeInstaller::downloadFile(const QString &url, const QString &filePath,
                                     const QString &expectedSha1, int redirectDepth)
{
    if (m_isCancelled) {
        return false;
    }

    if (redirectDepth > 10) {
        emit installFailed(tr("下载重定向次数过多"));
        return false;
    }

    QString fileName = QFileInfo(filePath).fileName();
    if (!m_currentTaskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskCurrentFile(m_currentTaskId, fileName);
    }

    m_speedTimer.start();

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_networkManager->get(request);
    m_currentReply = reply;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit installFailed(tr("无法创建文件: %1").arg(filePath));
        reply->abort();
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::readyRead, this, [reply, &file]() {
        file.write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
        m_totalBytes = bytesTotal;
        m_downloadedBytes = bytesReceived;
        emit downloadProgressUpdated(bytesReceived, bytesTotal);
        updateTaskProgress(bytesReceived, bytesTotal);
    });

    loop.exec();

    file.close();

    if (m_isCancelled) {
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QVariant redirectVariant = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectVariant.isValid()) {
            QUrl redirectUrl = redirectVariant.toUrl();
            if (redirectUrl.isRelative()) {
                redirectUrl = reply->url().resolved(redirectUrl);
            }
            reply->deleteLater();
            m_currentReply = nullptr;
            return downloadFile(redirectUrl.toString(), filePath, expectedSha1, redirectDepth + 1);
        }

        QString errorMsg = reply->errorString();
        emit installFailed(tr("下载文件失败: %1").arg(errorMsg));
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    reply->deleteLater();
    m_currentReply = nullptr;

    if (!expectedSha1.isEmpty()) {
        QString actualSha1 = calculateSha1(filePath);
        if (actualSha1.toLower() != expectedSha1.toLower()) {
            emit installFailed(tr("文件SHA1校验失败: %1").arg(filePath));
            QFile::remove(filePath);
            return false;
        }
    }

    return true;
}

QString NeoForgeInstaller::calculateSha1(const QString &filePath)
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

void NeoForgeInstaller::cleanupTempFiles()
{
    if (!m_tempDir.isEmpty()) {
        QDir tempDir(m_tempDir);
        if (tempDir.exists()) {
            tempDir.removeRecursively();
        }
        m_tempDir.clear();
    }
}

QString NeoForgeInstaller::getJavaPath() const
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