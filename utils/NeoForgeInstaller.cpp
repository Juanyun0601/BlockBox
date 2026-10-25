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
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

#include "DownloadTaskManager.h"
#include "forge/ForgeDownloader.h"
#include "forge/ForgeNewInstallTask.h"
#include "forge/LoaderVersionDir.h"

namespace {

// 调用方可能传游戏根目录，也可能传 versions/<版本> 版本的实例目录
QString resolveMinecraftRootPath(const QString &instancePath)
{
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(instancePath));
    if (path.isEmpty())
    {
        return path;
    }

    const QFileInfo info(path);
    if (info.absoluteDir().dirName().compare(QStringLiteral("versions"), Qt::CaseInsensitive) == 0)
    {
        return QDir::cleanPath(info.absolutePath() + QStringLiteral("/.."));
    }
    if (info.fileName().compare(QStringLiteral("versions"), Qt::CaseInsensitive) == 0)
    {
        return info.absolutePath();
    }
    return path;
}

} // namespace

NeoForgeInstaller* NeoForgeInstaller::m_instance = nullptr;
QMutex NeoForgeInstaller::m_instanceMutex;

NeoForgeInstaller::NeoForgeInstaller()
    : m_networkManager(new QNetworkAccessManager(this))
    , m_downloadSource(NeoForgeDownloadSource::BMCL)
    , m_currentReply(nullptr)
    , m_isInstalling(false)
    , m_isCancelled(false)
    , m_isDownloadPhase(true)
    , m_taskStarted(false)
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
    forge::ForgeDownloader provider;
    provider.setDownloadSource(m_downloadSource == NeoForgeDownloadSource::Official
                                   ? forge::DownloadSource::Official
                                   : forge::DownloadSource::BMCL);
    provider.setMavenFamily(forge::InstallerFamily::NeoForge);
    const QStringList candidates = provider.getNeoForgeInstallerCandidateUrls(forgeVersion);
    return candidates.isEmpty() ? QString() : candidates.first();
}

QStringList NeoForgeInstaller::installerCandidateUrls() const
{
    forge::ForgeDownloader provider;
    provider.setDownloadSource(m_downloadSource == NeoForgeDownloadSource::Official
                                   ? forge::DownloadSource::Official
                                   : forge::DownloadSource::BMCL);
    provider.setMavenFamily(forge::InstallerFamily::NeoForge);
    return provider.getNeoForgeInstallerCandidateUrls(m_currentForgeVersion);
}

void NeoForgeInstaller::downloadNeoForgeInstaller(const QString &mcVersion, const QString &forgeVersion,
                                                   const QString &instancePath, const QString &instanceName)
{
    if (m_isInstalling)
    {
        emit installFailed(tr("安装器正忙，请等待当前任务完成"));
        return;
    }

    m_currentMcVersion = mcVersion;
    m_currentForgeVersion = forgeVersion;
    m_currentInstancePath = instancePath;
    m_isInstalling = true;
    m_isCancelled = false;
    m_taskStarted = false;
    m_lastDownloadError.clear();
    m_totalBytes = 0;
    m_downloadedBytes = 0;

    if (m_currentTaskId.isEmpty())
    {
        QString taskName = instanceName;
        if (taskName.isEmpty())
        {
            taskName = instancePath.split("/").last();
            if (taskName.isEmpty())
            {
                taskName = QString("NeoForge %1").arg(forgeVersion);
            }
        }
        QString taskId = DownloadTaskManager::instance()->addTaskWithType(
            taskName, instancePath, QString("NeoForge %1").arg(forgeVersion), QStringList(),
            TaskType::LoaderInstall);
        m_currentTaskId = taskId;
    }

    DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Downloading,
                                                       tr("开始下载NeoForge"));

    emit statusChanged(tr("开始下载 NeoForge %1...").arg(forgeVersion));
    updateTaskStatus(tr("正在准备下载"));

    m_tempDir = QDir::tempPath() + "/BlockBox_NeoForge_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir tempDir(m_tempDir);
    if (!tempDir.exists())
    {
        tempDir.mkpath(".");
    }

    m_installerPath = m_tempDir + "/neoforge-installer.jar";

    emit statusChanged(tr("正在下载NeoForge安装包..."));
    updateTaskStatus(tr("正在下载安装包"));

    m_isDownloadPhase = true;

    // 安装包下载（含多源回退）放后台线程：调用方既可能是界面线程，
    // 也可能已经是安装页拉起的后台线程
    (void)QtConcurrent::run([this]() { runDownloadPhase(); });
}

void NeoForgeInstaller::runDownloadPhase()
{
    const QStringList candidates = installerCandidateUrls();
    bool downloaded = false;

    for (const QString &url : candidates)
    {
        if (m_isCancelled)
        {
            return;
        }

        qDebug() << "[NeoForgeInstaller] 尝试安装包地址:" << url;
        if (downloadFile(url, m_installerPath))
        {
            downloaded = true;
            break;
        }
    }

    if (m_isCancelled)
    {
        return;
    }

    if (!downloaded)
    {
        m_isInstalling = false;
        cleanupTempFiles();
        if (!m_currentTaskId.isEmpty())
        {
            DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Failed,
                                                              tr("安装失败"));
        }
        emit installFailed(m_lastDownloadError.isEmpty()
                               ? tr("下载NeoForge安装包失败")
                               : tr("下载NeoForge安装包失败：%1").arg(m_lastDownloadError));
        return;
    }

    // 安装任务里会创建 QObject（父对象在对象所属线程），必须回到该线程启动
    QMetaObject::invokeMethod(this, [this]() { installNeoForge(m_installerPath, m_currentInstancePath); },
                              Qt::QueuedConnection);
}

void NeoForgeInstaller::installNeoForge(const QString &installerPath, const QString &instancePath)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, installerPath, instancePath]() {
            installNeoForge(installerPath, instancePath);
        }, Qt::QueuedConnection);
        return;
    }

    if (m_isCancelled)
    {
        return;
    }

    m_installerPath = installerPath;
    m_currentInstancePath = instancePath;
    startInstallTask();
}

void NeoForgeInstaller::startInstallTask()
{
    m_isDownloadPhase = false;
    m_taskStarted = true;

    // 官方安装程序要求目录里有 launcher_profiles.json，且只能产出官方布局；
    // 这里改成 MultiMC 同款做法：直接解析 install_profile、跑处理器、自己写版本 JSON
    emit statusChanged(tr("正在解析NeoForge安装信息..."));
    updateTaskStatus(tr("正在解析安装配置"));
    emit installProgressUpdated(0, tr("正在解析安装配置"));
    if (!m_currentTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, 60);
    }

    const QString gameRoot = resolveMinecraftRootPath(m_currentInstancePath);

    if (m_installTask)
    {
        m_installTask->deleteLater();
        m_installTask = nullptr;
    }
    m_installTask = new forge::ForgeNewInstallTask(m_installerPath, gameRoot, this,
                                                   forge::InstallerFamily::NeoForge);
    // 安装页上选的下载源优先于全局设置
    m_installTask->setDownloadSource(m_downloadSource == NeoForgeDownloadSource::Official
                                         ? forge::DownloadSource::Official
                                         : forge::DownloadSource::BMCL);

    connect(m_installTask, &forge::ForgeNewInstallTask::progressUpdated, this,
            [this](const QString &stage, int percent)
            {
                emit statusChanged(stage);
                emit installProgressUpdated(percent, stage);
                updateTaskStatus(stage);
                if (!m_currentTaskId.isEmpty())
                {
                    // 安装器阶段整体落在 60%→100%
                    DownloadTaskManager::instance()->updateTaskProgressPercent(
                        m_currentTaskId, 60 + percent * 40 / 100);
                }
            });

    connect(m_installTask, &forge::ForgeNewInstallTask::completed, this,
            [this](bool success, const QString &error)
            {
                m_isInstalling = false;
                m_taskStarted = false;

                const QString versionId =
                    m_installTask ? m_installTask->writtenVersionId() : QString();
                if (m_installTask)
                {
                    m_installTask->deleteLater();
                    m_installTask = nullptr;
                }

                // 处理器/解压线程此时都已结束，删安装包是安全的
                cleanupTempFiles();

                if (m_isCancelled)
                {
                    return; // 取消信号已经发过
                }

                if (success)
                {
                    // 原版基准目录并进生成的 NeoForge 目录：版本 JAR 改由实例目录提供，
                    // FML 的 ignoreList 才认领得到它（否则原版 JAR 会被当成普通模块，
                    // 与补丁客户端的包导出冲突，启动直接 ResolutionException）
                    if (!versionId.isEmpty())
                    {
                        forge::consolidateLoaderVersionDir(resolveMinecraftRootPath(m_currentInstancePath),
                                                           m_currentMcVersion, versionId, false);
                    }
                    emit installProgressUpdated(100, tr("安装完成"));
                    if (!m_currentTaskId.isEmpty())
                    {
                        DownloadTaskManager::instance()->updateTaskStatus(
                            m_currentTaskId, DownloadTaskStatus::Completed, tr("安装完成"));
                        DownloadTaskManager::instance()->updateTaskProgressPercent(m_currentTaskId, 100);
                    }
                    emit installCompleted(versionId);
                }
                else
                {
                    if (!m_currentTaskId.isEmpty())
                    {
                        DownloadTaskManager::instance()->updateTaskStatus(
                            m_currentTaskId, DownloadTaskStatus::Failed, tr("安装失败"));
                    }
                    emit installFailed(error);
                }
            });

    m_installTask->execute();
}

void NeoForgeInstaller::cancelInstall()
{
    if (!m_isInstalling && !m_taskStarted)
    {
        return;
    }

    m_isCancelled = true;
    m_isInstalling = false;

    // reply 可能存活在下载后台线程，跨线程直接 abort 不安全，排队投递过去；
    // 安装任务只打取消标记，由任务自己在处理器/解压收尾后结束
    if (m_currentReply)
    {
        QMetaObject::invokeMethod(m_currentReply, &QNetworkReply::abort, Qt::QueuedConnection);
        m_currentReply = nullptr;
    }

    if (m_installTask)
    {
        m_installTask->cancel();
    }

    if (!m_currentTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->cancelTask(m_currentTaskId);
    }

    // 安装任务仍在跑时不能删临时目录（任务还在读安装包），
    // 交给任务的完成回调统一清理
    if (!m_taskStarted)
    {
        cleanupTempFiles();
    }

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

    if (redirectDepth > 10)
    {
        m_lastDownloadError = tr("重定向次数过多");
        qWarning() << "[NeoForgeInstaller]" << m_lastDownloadError << url;
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
    // 60 秒无数据传输即超时，避免连接停滞时嵌套事件循环永不退出导致界面卡死
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::NoLessSafeRedirectPolicy);

    // 本函数可能在后台线程执行（加载器安装已移出 GUI 线程），
    // QNetworkAccessManager 必须与调用线程同亲和，这里用局部实例，
    // 离开作用域时连同其子 reply 一起销毁
    QNetworkAccessManager nam;

    QNetworkReply *reply = nam.get(request);
    m_currentReply = reply;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastDownloadError = tr("无法创建文件: %1").arg(filePath);
        qWarning() << "[NeoForgeInstaller]" << m_lastDownloadError;
        reply->abort();
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    QEventLoop loop;
    // 数据落盘保持直接连接，在执行线程内写文件；进度上报挂到 this（GUI 线程）排队
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::readyRead, [reply, &file]() {
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
        m_lastDownloadError = errorMsg;
        qWarning() << "[NeoForgeInstaller]" << "下载失败:" << url << errorMsg;
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    reply->deleteLater();
    m_currentReply = nullptr;

    if (!expectedSha1.isEmpty()) {
        QString actualSha1 = calculateSha1(filePath);
        if (actualSha1.toLower() != expectedSha1.toLower()) {
            m_lastDownloadError = tr("文件SHA1校验失败: %1").arg(filePath);
            qWarning() << "[NeoForgeInstaller]" << m_lastDownloadError;
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


