/**
 * @file   HongshiClient.cpp
 * @brief  红石联机客户端实现
 * @author BlockBox Team
 * @date   2026-08-19
 */

#include "HongshiClient.h"

#include <functional>
#include <memory>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

#include "utils/DownloadTaskManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
/// 主站 / 镜像站（参考 hongshionline/hongshi-shell lib.rs）
const QString kPrimaryBase = QStringLiteral("https://hongshi.site");
const QString kMirrorBase  = QStringLiteral("https://shithub.site");

const int kHttpTimeoutMs   = 8000;
const int kDownloadTimeoutMs = 300000;

/// 平台下载接口路径（参考 shells：Windows 直接 /api/download/windows）
QString downloadApiPath()
{
#ifdef Q_OS_WIN
    return QStringLiteral("/api/download/windows");
#else
    QString platform;
#if defined(Q_OS_MACOS)
    platform = QStringLiteral("darwin");
#else
    platform = QStringLiteral("linux");
#endif
    QString arch;
#if defined(Q_PROCESSOR_ARM_64)
    arch = QStringLiteral("arm64");
#else
    arch = QStringLiteral("amd64");
#endif
    return QStringLiteral("/api/download/%1?arch=%2").arg(platform, arch);
#endif
}
} // namespace

HongshiClient *HongshiClient::instance()
{
    static HongshiClient *s_inst = nullptr;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (!s_inst) {
        s_inst = new HongshiClient(qApp);
    }
    return s_inst;
}

HongshiClient::HongshiClient(QObject *parent)
    : QObject(parent)
{
    // 隧道状态轮询：500ms 一次（参考 shells HomeView startPolling 间隔）
    m_pollTimer.setInterval(500);
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, &HongshiClient::pollStatus);

    // 进程存活巡检：500ms 一次，配合 finished 信号双保险
    m_watchTimer.setInterval(500);
    m_watchTimer.setSingleShot(false);
    connect(&m_watchTimer, &QTimer::timeout, this, [this]() {
        if (!m_process) return;
        if (m_process->state() == QProcess::NotRunning) {
            m_watchTimer.stop();
            return;
        }
        parseStatusFile();
    });

    QFile vf(versionFilePath());
    if (vf.open(QIODevice::ReadOnly)) {
        m_installedVersion = QString::fromUtf8(vf.readAll()).trimmed();
        vf.close();
    }
    m_installStatus = isKernelPresent() ? InstallStatus::Ready : InstallStatus::NotInstalled;

    // 启动后立即异步检查远程版本（不阻塞 UI）
    QTimer::singleShot(200, this, &HongshiClient::checkUpdate);
}

HongshiClient::~HongshiClient()
{
    if (m_process) {
        m_process->kill();
        if (!m_process->waitForFinished(2000)) {
            m_process->terminate();
        }
        delete m_process;
        m_process = nullptr;
    }
}

// ============================
// 路径
// ============================

QString HongshiClient::hongshiRootDir() const
{
    // 与陶瓦联机一致，存放在 BlockBox 可执行文件所在目录下的 hongshi 子目录
    return QCoreApplication::applicationDirPath() + QStringLiteral("/hongshi");
}

QString HongshiClient::hongshiExecutablePath() const
{
    QString name = QStringLiteral("hongshi");
#ifdef Q_OS_WIN
    name += QStringLiteral(".exe");
#endif
    return hongshiRootDir() + QStringLiteral("/") + name;
}

QString HongshiClient::versionFilePath() const
{
    return hongshiRootDir() + QStringLiteral("/version.txt");
}

QString HongshiClient::statusFilePath() const
{
    return hongshiRootDir() + QStringLiteral("/tunnel.ini");
}

QString HongshiClient::logDirPath() const
{
    return hongshiRootDir() + QStringLiteral("/logs");
}

QString HongshiClient::downloadApiPath() const
{
    return ::downloadApiPath();
}

// ============================
// 状态查询
// ============================

bool HongshiClient::isKernelPresent() const
{
    return QFileInfo::exists(hongshiExecutablePath());
}

bool HongshiClient::isProcessRunning() const
{
    return m_process != nullptr && m_process->state() != QProcess::NotRunning;
}

bool HongshiClient::isTunnelActive() const
{
    switch (m_state) {
    case State::Launching:
    case State::Connecting:
    case State::Open:
        return true;
    default:
        return false;
    }
}

QString HongshiClient::tunnelAddress() const
{
    if (m_tunnelPort <= 0) return QString();
    return QStringLiteral("%1:%2").arg(m_tunnelServer, QString::number(m_tunnelPort));
}

// ============================
// 远程版本查询
// ============================

void HongshiClient::fetchCoreVersion()
{
    if (m_fetchingVersion) return;
    m_fetchingVersion = true;

    auto tryBase = std::make_shared<std::function<void(int)>>();
    *tryBase = [this, tryBase](int index) {
        if (index >= 2) {
            // 主站与镜像均失败
            m_fetchingVersion = false;
            if (m_pendingDownload) {
                m_pendingDownload = false;
                downloadFailed(tr("获取版本信息失败，请检查网络后重试"), QString());
            } else {
                applyInstallStatus();
            }
            return;
        }

        const QString base = (index == 0) ? kPrimaryBase : kMirrorBase;
        QUrl url(base + QStringLiteral("/core_version.json"));
        QNetworkRequest req(url);
        req.setRawHeader("User-Agent", "BlockBox");
        req.setTransferTimeout(kHttpTimeoutMs);

        QNetworkReply *reply = m_networkManager.get(req);
        QTimer *timeoutTimer = new QTimer(this);
        timeoutTimer->setSingleShot(true);
        timeoutTimer->setInterval(kHttpTimeoutMs);
        connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
            if (reply->isRunning()) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, timeoutTimer, &QTimer::deleteLater);
        timeoutTimer->start();

        connect(reply, &QNetworkReply::finished, this, [this, reply, tryBase, index]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    const QJsonValue v = doc.object().value(QStringLiteral("version"));
                    QString ver;
                    if (v.isString()) ver = v.toString();
                    else if (v.isDouble()) ver = QString::number(qint64(v.toDouble()));
                    if (!ver.isEmpty()) {
                        m_latestVersion = ver.trimmed();
                        m_fetchingVersion = false;
                        if (m_pendingDownload) {
                            m_pendingDownload = false;
                            startDownload();
                        } else {
                            applyInstallStatus();
                        }
                        return;
                    }
                }
            }
            // 尝试下一个站点
            (*tryBase)(index + 1);
        });
    };

    (*tryBase)(0);
}

void HongshiClient::applyInstallStatus()
{
    if (!isKernelPresent()) {
        m_installStatus = InstallStatus::NotInstalled;
    } else if (m_installedVersion.isEmpty() || m_latestVersion.isEmpty()) {
        m_installStatus = InstallStatus::Ready;
    } else {
        int cmp = QVersionNumber::compare(
            QVersionNumber::fromString(m_installedVersion),
            QVersionNumber::fromString(m_latestVersion));
        m_installStatus = (cmp >= 0) ? InstallStatus::UpToDate : InstallStatus::UpdateAvailable;
    }
    emit installStatusChanged();
}

void HongshiClient::checkUpdate()
{
    if (m_installStatus == InstallStatus::Downloading
        || m_installStatus == InstallStatus::CheckingUpdate) {
        return;
    }
    m_installStatus = InstallStatus::CheckingUpdate;
    emit installStatusChanged();
    fetchCoreVersion();
}

// ============================
// 下载安装
// ============================

void HongshiClient::downloadLatest()
{
    if (m_installStatus == InstallStatus::Downloading
        || m_installStatus == InstallStatus::CheckingUpdate) {
        return;
    }

    if (m_latestVersion.isEmpty()) {
        // 版本未知状态：先获取版本再下载
        m_pendingDownload = true;
        m_installStatus = InstallStatus::CheckingUpdate;
        emit installStatusChanged();
        fetchCoreVersion();
        return;
    }

    if (!m_installedVersion.isEmpty() && m_latestVersion == m_installedVersion) {
        m_installStatus = InstallStatus::UpToDate;
        emit installStatusChanged();
        return;
    }

    startDownload();
}

void HongshiClient::startDownload()
{
    if (m_latestVersion.isEmpty()) {
        downloadFailed(tr("无法获取红石联机最新版本号"), QString());
        return;
    }

    m_installStatus = InstallStatus::Downloading;
    m_downloadProgress = 0.0;
    emit installStatusChanged();
    emit downloadProgressChanged(0.0);

    // 获取签名下载地址（主站 → 镜像）
    auto tryBase = std::make_shared<std::function<void(int)>>();
    *tryBase = [this, tryBase](int index) {
        if (index >= 2) {
            downloadFailed(tr("获取下载地址失败，请检查网络后重试"), QString());
            return;
        }
        const QString base = (index == 0) ? kPrimaryBase : kMirrorBase;
        QUrl url(base + downloadApiPath());
        QNetworkRequest req(url);
        req.setRawHeader("User-Agent", "BlockBox");
        req.setTransferTimeout(kHttpTimeoutMs);

        QNetworkReply *reply = m_networkManager.get(req);
        QTimer *timeoutTimer = new QTimer(this);
        timeoutTimer->setSingleShot(true);
        timeoutTimer->setInterval(kHttpTimeoutMs);
        connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
            if (reply->isRunning()) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, timeoutTimer, &QTimer::deleteLater);
        timeoutTimer->start();

        connect(reply, &QNetworkReply::finished, this, [this, reply, tryBase, index]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    const QString u = doc.object().value(QStringLiteral("url")).toString();
                    if (!u.isEmpty()) {
                        startDownloadFile(u);
                        return;
                    }
                }
            }
            (*tryBase)(index + 1);
        });
    };

    (*tryBase)(0);
}

void HongshiClient::startDownloadFile(const QString &url)
{
    QString rootDir = hongshiRootDir();
    QDir().mkpath(rootDir);

    // 接入项目任务栏（与陶瓦联机下载一致）
    QString taskId = DownloadTaskManager::instance()->addTask(
        tr("红石联机核心 v%1").arg(m_latestVersion), rootDir, QString(), QStringList());
    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading, tr("下载中"));
    DownloadTaskManager::instance()->updateTaskStep(taskId, tr("下载联机核心"));

    QString tmpPath = rootDir + QStringLiteral("/hongshi.exe.download");

    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("User-Agent", "BlockBox");
    req.setTransferTimeout(kDownloadTimeoutMs);

    QNetworkReply *reply = m_networkManager.get(req);

    QTimer *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(kDownloadTimeoutMs);
    connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, timeoutTimer, &QTimer::deleteLater);
    timeoutTimer->start();

    connect(reply, &QNetworkReply::downloadProgress, this, [this, taskId](qint64 received, qint64 total) {
        if (total > 0) {
            m_downloadProgress = qreal(received) / qreal(total);
            emit downloadProgressChanged(m_downloadProgress);
            DownloadTaskManager::instance()->updateTaskProgressPercent(
                taskId, int(m_downloadProgress * 100));
            DownloadTaskManager::instance()->updateTaskFileProgress(
                taskId, QString(), received, total);
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, tmpPath, taskId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            downloadFailed(tr("下载失败：%1").arg(reply->errorString()), taskId);
            return;
        }

        QByteArray data = reply->readAll();
        QFile file(tmpPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            downloadFailed(tr("无法写入临时文件"), taskId);
            return;
        }
        file.write(data);
        file.close();

        // 原子替换旧内核（Windows rename 无法覆盖已存在文件，先删旧）
        const QString target = hongshiExecutablePath();
        if (QFileInfo::exists(target)) {
            if (!QFile::remove(target)) {
                downloadFailed(tr("删除旧内核失败"), taskId);
                return;
            }
        }
        if (!QFile::rename(tmpPath, target)) {
            downloadFailed(tr("保存内核失败"), taskId);
            return;
        }

#ifndef Q_OS_WIN
        // 设置可执行权限（macOS / Linux）
        QFile::setPermissions(target,
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
            | QFile::ReadGroup | QFile::ExeGroup
            | QFile::ReadOther | QFile::ExeOther);
#endif

        // 记录当前内核版本
        QFile vf(versionFilePath());
        if (vf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            vf.write(m_latestVersion.toUtf8());
            vf.close();
        }

        m_installedVersion = m_latestVersion;
        m_downloadProgress = 1.0;
        m_installStatus = InstallStatus::Ready;
        emit installStatusChanged();
        emit downloadProgressChanged(1.0);

        DownloadTaskManager::instance()->updateTaskStatus(
            taskId, DownloadTaskStatus::Completed, tr("安装完成"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
    });
}

void HongshiClient::downloadFailed(const QString &msg, const QString &taskId)
{
    setErrorMessage(msg);
    m_installStatus = InstallStatus::DownloadFailed;
    emit installStatusChanged();
    if (!taskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(
            taskId, DownloadTaskStatus::Failed, tr("下载失败"));
    }
}

// ============================
// 服务器节点列表
// ============================

void HongshiClient::refreshServerNodes()
{
    auto tryBase = std::make_shared<std::function<void(int)>>();
    *tryBase = [this, tryBase](int index) {
        if (index >= 2) return;
        const QString base = (index == 0) ? kPrimaryBase : kMirrorBase;
        QUrl url(base + QStringLiteral("/newserver.json"));
        QNetworkRequest req(url);
        req.setRawHeader("User-Agent", "BlockBox");
        req.setTransferTimeout(kHttpTimeoutMs);

        QNetworkReply *reply = m_networkManager.get(req);
        QTimer *timeoutTimer = new QTimer(this);
        timeoutTimer->setSingleShot(true);
        timeoutTimer->setInterval(kHttpTimeoutMs);
        connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
            if (reply->isRunning()) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, timeoutTimer, &QTimer::deleteLater);
        timeoutTimer->start();

        connect(reply, &QNetworkReply::finished, this, [this, reply, tryBase, index]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    QList<Node> nodes;
                    for (auto it = obj.begin(); it != obj.end(); ++it) {
                        Node n;
                        n.name = it.key();
                        n.host = it.value().toString().trimmed();
                        if (!n.host.isEmpty()) {
                            nodes.append(n);
                        }
                    }
                    if (!nodes.isEmpty()) {
                        m_nodes = nodes;
                        emit serverNodesChanged();
                        return;
                    }
                }
            }
            (*tryBase)(index + 1);
        });
    };

    (*tryBase)(0);
}

// ============================
// 进程管理与状态轮询
// ============================

void HongshiClient::startKernel(const QString &server, quint16 port)
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        setErrorMessage(tr("红石联机内核已在运行"));
        return;
    }
    if (!isKernelPresent()) {
        setErrorMessage(tr("未找到红石联机内核程序，请先下载"));
        return;
    }
    if (server.trimmed().isEmpty()) {
        setErrorMessage(tr("请先选择服务器节点"));
        return;
    }
    if (port == 0) {
        setErrorMessage(tr("端口号无效"));
        return;
    }

    stopKernel();

    setState(State::Launching);

    QDir().mkpath(hongshiRootDir());
    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &HongshiClient::onProcessFinished);
        connect(m_process, &QProcess::errorOccurred,
                this, &HongshiClient::onProcessErrorOccurred);
    }

    QStringList args;
    args << QStringLiteral("-server") << server.trimmed()
         << QStringLiteral("-port") << QString::number(port)
         << QStringLiteral("-status-file") << statusFilePath();
    m_process->setProgram(hongshiExecutablePath());
    m_process->setArguments(args);
    m_process->setWorkingDirectory(hongshiRootDir());

#ifdef Q_OS_WIN
    // Windows 下隐藏控制台窗口启动
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    m_process->start();
    m_pollTimer.start();
    m_watchTimer.start();

    // 立即读取一次状态
    QTimer::singleShot(100, this, &HongshiClient::parseStatusFile);
}

void HongshiClient::stopKernel()
{
    m_stopping = true;
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
    m_stopping = false;
    m_pollTimer.stop();
    m_watchTimer.stop();
    m_tunnelServer.clear();
    m_tunnelPort = -1;
    setState(isKernelPresent() ? State::Ready : State::NotInstalled);
}

void HongshiClient::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(status);
    qDebug() << "Hongshi kernel process exited";

    if (m_stopping) {
        // 用户主动停止，不视为异常
        setState(isKernelPresent() ? State::Ready : State::NotInstalled);
        return;
    }

    bool wasActive = isTunnelActive();
    m_pollTimer.stop();
    m_watchTimer.stop();
    m_tunnelServer.clear();
    m_tunnelPort = -1;

    if (wasActive) {
        setState(State::Failed);
        setErrorMessage(tr("红石联机内核进程意外退出"));
    } else if (m_state == State::Launching) {
        // 启动后立刻退出：视为启动失败
        setState(State::Failed);
        setErrorMessage(tr("红石联机内核启动后立即退出，请查看日志"));
    } else {
        setState(isKernelPresent() ? State::Ready : State::NotInstalled);
    }
}

void HongshiClient::onProcessErrorOccurred(QProcess::ProcessError error)
{
    qDebug() << "Hongshi kernel process error:" << int(error);
    if (error == QProcess::FailedToStart) {
        m_pollTimer.stop();
        m_watchTimer.stop();
        setState(State::Fatal);
        setErrorMessage(tr("无法启动红石联机内核进程"));
    }
}

void HongshiClient::parseStatusFile()
{
    if (!QFileInfo::exists(statusFilePath())) return;

    QFile f(statusFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QByteArray data = f.readAll();
    f.close();

    QString status;
    QString server;
    int port = -1;

    const QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.startsWith(QStringLiteral("status="))) {
            status = t.mid(7).trimmed();
        } else if (t.startsWith(QStringLiteral("server="))) {
            server = t.mid(7).trimmed();
        } else if (t.startsWith(QStringLiteral("port="))) {
            port = t.mid(5).trimmed().toInt();
        }
    }

    if (status == QStringLiteral("open") && port > 0) {
        m_tunnelServer = server;
        m_tunnelPort = port;
        setState(State::Open);
    } else if (m_state == State::Open) {
        // 从 open 掉回非 open：可能隧道已拆
        m_tunnelServer.clear();
        m_tunnelPort = -1;
        setState(State::Connecting);
    }
}

void HongshiClient::pollStatus()
{
    if (!m_process) {
        m_pollTimer.stop();
        return;
    }
    if (m_process->state() == QProcess::NotRunning) {
        return; // 已退出，finished 信号会处理
    }
    parseStatusFile();
}

// ============================
// 状态切换
// ============================

void HongshiClient::setState(State newState)
{
    if (m_state == newState) return;
    m_state = newState;
    emit stateChanged();
}

void HongshiClient::setErrorMessage(const QString &msg)
{
    m_errorMessage = msg;
    emit errorOccurred(msg);
}