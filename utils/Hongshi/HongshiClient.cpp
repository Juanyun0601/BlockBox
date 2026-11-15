/**
 * @file   HongshiClient.cpp
 * @brief  红石联机客户端实现（Redstone Online2 / hongshic）
 * @author BlockBox Team
 * @date   2026-08-19
 */

#include "HongshiClient.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QStringList>

#include "utils/DownloadTaskManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
/// 官方主站（Redstone Online2 起不再提供镜像站，旧镜像域名已不可用）
const QString kPrimaryBase = QStringLiteral("https://hongshi.site");

const int kHttpTimeoutMs = 10000;
const int kDownloadTimeoutMs = 300000;
/// 远小于这个体积的文件不可能是内核，避免把错误页当成程序保存
const qint64 kMinExecutableSize = 16 * 1024;

/// 去掉内核日志里的 ANSI 颜色码
QString stripAnsi(QString text)
{
    static const QRegularExpression re(QStringLiteral("\\x1B\\[[0-9;]*[A-Za-z]"));
    text.remove(re);
    return text;
}

/// 从官方错误响应里取出人能看懂的说明
QString apiErrorDetail(const QByteArray &body)
{
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject obj = doc.object();
        const QJsonValue detail = obj.value(QStringLiteral("detail"));
        if (detail.isString()) {
            return detail.toString().trimmed();
        }
        if (detail.isObject()) {
            const QJsonObject d = detail.toObject();
            const QString message = d.value(QStringLiteral("message")).toString().trimmed();
            if (!message.isEmpty()) return message;
            const QString error = d.value(QStringLiteral("error")).toString().trimmed();
            if (!error.isEmpty()) return error;
        }
        if (detail.isArray()) {
            QStringList parts;
            const QJsonArray arr = detail.toArray();
            for (const QJsonValue &item : arr) {
                if (!item.isObject()) continue;
                const QString msg = item.toObject().value(QStringLiteral("msg")).toString().trimmed();
                if (!msg.isEmpty()) parts.append(msg);
            }
            if (!parts.isEmpty()) return parts.join(QStringLiteral("；"));
        }
        const QString message = obj.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) return message;
    }
    return QString::fromUtf8(body).simplified().left(160);
}

/// 简单校验下载内容确实是可执行文件
bool looksLikeExecutable(const QByteArray &data)
{
#if defined(Q_OS_WIN)
    return data.startsWith("MZ");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (data.size() < 4) return false;
    const quint32 magic = (quint32(quint8(data.at(0))) << 24)
                          | (quint32(quint8(data.at(1))) << 16)
                          | (quint32(quint8(data.at(2))) << 8)
                          | quint32(quint8(data.at(3)));
    // Mach-O 32/64 位（含大端）与 FAT 通用二进制
    return magic == 0xFEEDFACEu || magic == 0xFEEDFACFu
           || magic == 0xCEFAEDFEu || magic == 0xCFFAEDFEu
           || magic == 0xCAFEBABEu;
#else
    return data.startsWith(QByteArrayLiteral("\x7F" "ELF"));
#endif
}

/// 让请求跟随官方接口的重定向
void applyRedirectPolicy(QNetworkRequest &req)
{
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
}

/// 统一 ETag / Last-Modified 的写法（去掉 W/ 前缀和引号），HEAD 与 GET 才能对得上
QString normalizeStamp(QString stamp)
{
    stamp = stamp.trimmed();
    if (stamp.startsWith(QStringLiteral("W/"))) {
        stamp = stamp.mid(2).trimmed();
    }
    if (stamp.size() >= 2 && stamp.startsWith(QLatin1Char('"')) && stamp.endsWith(QLatin1Char('"'))) {
        stamp = stamp.mid(1, stamp.size() - 2);
    }
    return stamp.trimmed();
}

/// 从下载响应里取构建标识：优先 ETag，没有就退回 Last-Modified
QString replyBuildStamp(QNetworkReply *reply)
{
    QString stamp = normalizeStamp(QString::fromLatin1(reply->rawHeader("ETag")));
    if (stamp.isEmpty()) {
        stamp = normalizeStamp(QString::fromLatin1(reply->rawHeader("Last-Modified")));
    }
    return stamp;
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
    readLocalState();

    if (!isPlatformSupported()) {
        m_installStatus = InstallStatus::Unsupported;
    } else {
        m_installStatus = isKernelPresent() ? InstallStatus::Ready : InstallStatus::NotInstalled;
    }

    // 旧版本没有记录 version.txt 时，异步跑一次 hongshic -v 补上版本号（不阻塞启动）
    QTimer::singleShot(900, this, [this]() {
        if (!m_installedVersion.isEmpty() || !isKernelPresent() || !isPlatformSupported()) return;
        const QString version = probeKernelVersion();
        if (version.isEmpty()) return;
        m_installedVersion = version;
        writeVersionFile(version);
        emit installStatusChanged();
    });

    // 已安装内核时异步检查更新（不阻塞 UI）
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
#ifdef Q_OS_ANDROID
    // 安卓上应用安装目录只读，核心放到应用私有数据目录
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/hongshi");
#else
    // 与陶瓦联机一致，存放在 BlockBox 可执行文件所在目录下的 hongshi 子目录
    return QCoreApplication::applicationDirPath() + QStringLiteral("/hongshi");
#endif
}

QString HongshiClient::hongshiExecutablePath() const
{
    QString name = QStringLiteral("hongshic");
#ifdef Q_OS_WIN
    name += QStringLiteral(".exe");
#endif
    return hongshiRootDir() + QStringLiteral("/") + name;
}

QString HongshiClient::versionFilePath() const
{
    return hongshiRootDir() + QStringLiteral("/version.txt");
}

QString HongshiClient::buildStampFilePath() const
{
    return hongshiRootDir() + QStringLiteral("/build.txt");
}

QString HongshiClient::platformSlug() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QStringLiteral("macos");
#elif defined(Q_OS_ANDROID) || defined(HARMONY_OS) || defined(Q_OS_OHOS)
    // 官方暂未提供手机 / 鸿蒙系统的内核，按钮上会明确提示
    return QString();
#else
    return QStringLiteral("linux");
#endif
}

QString HongshiClient::archSlug() const
{
#if defined(Q_OS_WIN)
    // 官方只发布 windows-amd64，ARM 版 Windows 由系统自带模拟运行
    return QStringLiteral("amd64");
#elif defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("arm64");
#else
    return QStringLiteral("amd64");
#endif
}

QString HongshiClient::downloadUrl() const
{
    const QString platform = platformSlug();
    if (platform.isEmpty()) return QString();
    return QStringLiteral("%1/api/download/client?platform=%2&arch=%3")
        .arg(kPrimaryBase, platform, archSlug());
}

// ============================
// 本地状态
// ============================

void HongshiClient::readLocalState()
{
    QFile vf(versionFilePath());
    if (vf.open(QIODevice::ReadOnly)) {
        m_installedVersion = QString::fromUtf8(vf.readAll()).trimmed();
        vf.close();
    }
    QFile sf(buildStampFilePath());
    if (sf.open(QIODevice::ReadOnly)) {
        m_installedStamp = normalizeStamp(QString::fromUtf8(sf.readAll()));
        sf.close();
    }
}

void HongshiClient::writeVersionFile(const QString &version)
{
    const QString root = hongshiRootDir();
    QDir().mkpath(root);
    QFile vf(versionFilePath());
    if (!vf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    vf.write(version.toUtf8());
    vf.close();
}

void HongshiClient::writeBuildStamp(const QString &stamp)
{
    if (stamp.isEmpty()) {
        QFile::remove(buildStampFilePath());
        return;
    }
    QDir().mkpath(hongshiRootDir());
    QFile sf(buildStampFilePath());
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    sf.write(stamp.toUtf8());
    sf.close();
}

QString HongshiClient::probeKernelVersion()
{
    const QString exe = hongshiExecutablePath();
    if (!QFileInfo::exists(exe)) return QString();

    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start(exe, QStringList() << QStringLiteral("-v"));
    if (!process.waitForStarted(3000)) return QString();
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return QString();
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput())
                           + QString::fromUtf8(process.readAllStandardError());
    static const QRegularExpression re(QStringLiteral("(\\d+\\.\\d+(?:\\.\\d+){0,2})"));
    const QRegularExpressionMatch match = re.match(output);
    return match.hasMatch() ? match.captured(1) : QString();
}

// ============================
// 状态查询
// ============================

bool HongshiClient::isKernelPresent() const
{
    const QFileInfo info(hongshiExecutablePath());
    return info.isFile() && info.size() > 1024;
}

bool HongshiClient::isPlatformSupported() const
{
    return !platformSlug().isEmpty();
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
    if (!m_tunnelEndpoint.isEmpty()) return m_tunnelEndpoint;
    if (m_tunnelPort <= 0) return QString();
    return QStringLiteral("%1:%2").arg(m_tunnelServer, QString::number(m_tunnelPort));
}

// ============================
// 远程查询与更新检查
// ============================

void HongshiClient::applyInstallStatus()
{
    if (!isPlatformSupported()) {
        m_installStatus = InstallStatus::Unsupported;
    } else if (!isKernelPresent()) {
        m_installStatus = InstallStatus::NotInstalled;
    } else {
        m_installStatus = InstallStatus::Ready;
    }
    emit installStatusChanged();
}

void HongshiClient::checkUpdate()
{
    if (!isPlatformSupported()) {
        m_installStatus = InstallStatus::Unsupported;
        emit installStatusChanged();
        return;
    }
    if (!isKernelPresent()) {
        m_latestVersion.clear();
        m_installStatus = InstallStatus::NotInstalled;
        emit installStatusChanged();
        return;
    }
    if (m_checkingUpdate || m_installStatus == InstallStatus::Downloading) {
        return;
    }

    m_checkingUpdate = true;
    m_installStatus = InstallStatus::CheckingUpdate;
    emit installStatusChanged();

    QNetworkRequest req((QUrl(downloadUrl())));
    req.setRawHeader("User-Agent", "BlockBox");
    req.setRawHeader("Accept", "application/json");
    applyRedirectPolicy(req);
    req.setTransferTimeout(kHttpTimeoutMs);

    QNetworkReply *reply = m_networkManager.head(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_checkingUpdate = false;

        if (reply->error() != QNetworkReply::NoError) {
            // 网络不通时保持「已安装」，不误报更新失败
            m_installStatus = InstallStatus::Ready;
            emit installStatusChanged();
            return;
        }

        const QString stamp = replyBuildStamp(reply);

        if (stamp.isEmpty()) {
            m_installStatus = InstallStatus::Ready;
        } else if (!m_installedStamp.isEmpty() && m_installedStamp == stamp) {
            m_installStatus = InstallStatus::UpToDate;
        } else {
            // 没有本机构建记录时也提示可更新，让用户能主动补一次最新内核
            m_installStatus = InstallStatus::UpdateAvailable;
        }
        emit installStatusChanged();
    });
}

// ============================
// 下载安装
// ============================

void HongshiClient::downloadLatest()
{
    if (!isPlatformSupported()) {
        m_installStatus = InstallStatus::Unsupported;
        emit installStatusChanged();
        setErrorMessage(tr("红石联机官方暂未提供适用于当前系统的内核"));
        return;
    }
    if (m_installStatus == InstallStatus::Downloading
        || m_installStatus == InstallStatus::CheckingUpdate
        || m_checkingUpdate) {
        return;
    }
    if (isProcessRunning()) {
        downloadFailed(tr("红石联机正在运行，请先停止联机后再更新内核"), QString());
        return;
    }
    startDownload();
}

void HongshiClient::startDownload()
{
    const QString url = downloadUrl();
    if (url.isEmpty()) {
        m_installStatus = InstallStatus::Unsupported;
        emit installStatusChanged();
        setErrorMessage(tr("红石联机官方暂未提供适用于当前系统的内核"));
        return;
    }

    QString rootDir = hongshiRootDir();
    if (!QDir().mkpath(rootDir)) {
        downloadFailed(tr("无法创建内核目录：%1").arg(rootDir), QString());
        return;
    }

    const QString target = hongshiExecutablePath();
    const QString tmpPath = target + QStringLiteral(".download");
    QFile::remove(tmpPath);

    m_installStatus = InstallStatus::Downloading;
    m_downloadProgress = 0.0;
    emit installStatusChanged();
    emit downloadProgressChanged(0.0);

    // 接入项目任务栏（与陶瓦联机下载一致）
    QString taskId = DownloadTaskManager::instance()->addTask(
        tr("红石联机核心"), rootDir, QString(), QStringList());
    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading, tr("下载中"));
    DownloadTaskManager::instance()->updateTaskStep(taskId, tr("下载联机核心"));

    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("User-Agent", "BlockBox");
    req.setRawHeader("Accept", "application/octet-stream, application/json");
    applyRedirectPolicy(req);
    req.setTransferTimeout(kDownloadTimeoutMs);

    QNetworkReply *reply = m_networkManager.get(req);

    connect(reply, &QNetworkReply::downloadProgress, this, [this, taskId](qint64 received, qint64 total) {
        if (total <= 0) return;
        m_downloadProgress = qreal(received) / qreal(total);
        emit downloadProgressChanged(m_downloadProgress);
        DownloadTaskManager::instance()->updateTaskProgressPercent(
            taskId, int(m_downloadProgress * 100));
        DownloadTaskManager::instance()->updateTaskFileProgress(
            taskId, QString(), received, total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, tmpPath, target, taskId]() {
        reply->deleteLater();

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();

        if (reply->error() != QNetworkReply::NoError) {
            QString msg;
            if (httpStatus == 429) {
                bool ok = false;
                const int retryAfter = reply->rawHeader("Retry-After").toInt(&ok);
                msg = ok ? tr("官方服务器繁忙，请 %1 秒后再试").arg(retryAfter)
                         : tr("官方服务器繁忙，请稍后再试");
            } else if (httpStatus == 404) {
                msg = tr("官方暂未发布适用于当前系统的红石联机内核");
                const QString detail = apiErrorDetail(body);
                if (!detail.isEmpty()) msg += QStringLiteral("（%1）").arg(detail);
            } else if (httpStatus == 400 || httpStatus == 422) {
                msg = tr("红石联机内核的下载参数不受支持");
                const QString detail = apiErrorDetail(body);
                if (!detail.isEmpty()) msg += QStringLiteral("（%1）").arg(detail);
            } else {
                const QString detail = apiErrorDetail(body);
                msg = detail.isEmpty() ? tr("下载失败：%1").arg(reply->errorString())
                                       : tr("下载失败：%1").arg(detail);
            }
            QFile::remove(tmpPath);
            downloadFailed(msg, taskId);
            return;
        }

        // 官方接口出错时也会带 200 的 HTML/JSON 页面，先按内容类型挡掉
        if (contentType.contains(QStringLiteral("json"))
            || contentType.startsWith(QStringLiteral("text/"))) {
            QFile::remove(tmpPath);
            downloadFailed(tr("官方服务器没有返回内核文件：%1").arg(apiErrorDetail(body)), taskId);
            return;
        }
        if (body.size() < kMinExecutableSize) {
            QFile::remove(tmpPath);
            downloadFailed(tr("下载到的内核文件不完整，请稍后重试"), taskId);
            return;
        }
        if (!looksLikeExecutable(body)) {
            QFile::remove(tmpPath);
            downloadFailed(tr("下载到的文件不是可执行程序，官方接口可能临时异常，请稍后重试"), taskId);
            return;
        }

        QFile file(tmpPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(body) != body.size()) {
            file.close();
            QFile::remove(tmpPath);
            downloadFailed(tr("无法写入内核文件，请检查磁盘空间和目录权限"), taskId);
            return;
        }
        file.close();

        // 原子替换旧内核（Windows 上文件被占用时 rename 会失败）
        if (QFileInfo::exists(target) && !QFile::remove(target)) {
            QFile::remove(tmpPath);
            downloadFailed(tr("删除旧内核失败，请先停止联机后再试"), taskId);
            return;
        }
        if (!QFile::rename(tmpPath, target)) {
            QFile::remove(tmpPath);
            downloadFailed(tr("保存内核失败，请检查目录权限"), taskId);
            return;
        }

#ifndef Q_OS_WIN
        // 设置可执行权限（macOS / Linux）
        QFile::setPermissions(target,
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
            | QFile::ReadGroup | QFile::ExeGroup
            | QFile::ReadOther | QFile::ExeOther);
#endif

        // 记录构建标识，用来判断下次是否有更新
        const QString stamp = replyBuildStamp(reply);
        m_installedStamp = stamp;
        writeBuildStamp(stamp);

        // 版本号由内核自己打印，失败时留空（界面显示「已就绪」）
        const QString version = probeKernelVersion();
        m_installedVersion = version;
        if (version.isEmpty()) {
            QFile::remove(versionFilePath());
        } else {
            writeVersionFile(version);
        }
        m_latestVersion = version;

        m_downloadProgress = 1.0;
        m_installStatus = version.isEmpty() ? InstallStatus::Ready : InstallStatus::UpToDate;
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
    m_downloadProgress = -1.0;
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
    if (m_fetchingNodes) return;
    m_fetchingNodes = true;

    QNetworkRequest req((QUrl(kPrimaryBase + QStringLiteral("/api/server/list"))));
    req.setRawHeader("User-Agent", "BlockBox");
    req.setRawHeader("Accept", "application/json");
    applyRedirectPolicy(req);
    req.setTransferTimeout(kHttpTimeoutMs);

    QNetworkReply *reply = m_networkManager.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_fetchingNodes = false;

        QList<Node> nodes;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject obj = doc.object();
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    Node n;
                    n.name = it.key();
                    n.host = it.value().toString().trimmed();
                    if (!n.host.isEmpty()) {
                        nodes.append(n);
                    }
                }
            }
        }
        m_nodes = nodes;
        emit serverNodesChanged();
    });
}

// ============================
// 进程管理与状态读取
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

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &HongshiClient::onProcessFinished);
        connect(m_process, &QProcess::errorOccurred,
                this, &HongshiClient::onProcessErrorOccurred);
        connect(m_process, &QProcess::readyReadStandardOutput,
                this, &HongshiClient::onStdoutReady);
        connect(m_process, &QProcess::readyReadStandardError,
                this, &HongshiClient::onStderrReady);
        connect(m_process, &QProcess::started, this, [this]() {
            if (m_state == State::Launching) setState(State::Connecting);
        });
    }

    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    resetTunnelInfo();

    setState(State::Launching);

    QDir().mkpath(hongshiRootDir());

    // 官方内核的启动参数：-t 中转节点，-p 本地游戏端口
    QStringList args;
    args << QStringLiteral("-t") << server.trimmed()
         << QStringLiteral("-p") << QString::number(port);
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
}

void HongshiClient::stopKernel()
{
    m_stopping = true;
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
    m_stopping = false;
    resetTunnelInfo();
    setState(isKernelPresent() ? State::Ready : State::NotInstalled);
}

void HongshiClient::onStdoutReady()
{
    if (!m_process) return;
    m_stdoutBuffer += m_process->readAllStandardOutput();

    int newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray raw = m_stdoutBuffer.left(newline);
        m_stdoutBuffer.remove(0, newline + 1);
        handleKernelOutputLine(stripAnsi(QString::fromUtf8(raw).trimmed()));
    }

    // 一直不换行时别把缓冲撑爆（保留尾部，端点在最后一段）
    if (m_stdoutBuffer.size() > 64 * 1024) {
        m_stdoutBuffer = m_stdoutBuffer.right(8 * 1024);
    }
}

void HongshiClient::onStderrReady()
{
    if (!m_process) return;
    m_stderrBuffer += m_process->readAllStandardError();
    if (m_stderrBuffer.size() > 8 * 1024) {
        m_stderrBuffer = m_stderrBuffer.right(8 * 1024);
    }
}

void HongshiClient::handleKernelOutputLine(const QString &line)
{
    if (line.isEmpty()) return;

    // 官方输出：... port=34575 endpoint=1.2.3.4:34575
    const int at = line.indexOf(QStringLiteral("endpoint="));
    if (at < 0) return;

    QString rest = line.mid(at + 9).trimmed();
    int end = 0;
    while (end < rest.size() && !rest.at(end).isSpace()) {
        ++end;
    }
    const QString endpoint = rest.left(end);
    if (endpoint.isEmpty()) return;

    m_tunnelEndpoint = endpoint;
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && colon + 1 < endpoint.size()) {
        m_tunnelServer = endpoint.left(colon);
        bool ok = false;
        const int port = endpoint.mid(colon + 1).toInt(&ok);
        m_tunnelPort = ok ? port : -1;
    }
    setState(State::Open);
}

void HongshiClient::resetTunnelInfo()
{
    m_tunnelEndpoint.clear();
    m_tunnelServer.clear();
    m_tunnelPort = -1;
}

void HongshiClient::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    qDebug() << "Hongshi 内核进程已退出，exitCode =" << exitCode;

    const bool wasOpen = (m_state == State::Open);
    resetTunnelInfo();

    // stopKernel() 主动结束进程时由它自己更新状态
    if (m_stopping) return;

    if (exitCode == 0) {
        // 官方约定：退出码 0 表示隧道结束（中转回收或节点关闭），不是启动失败
        setState(isKernelPresent() ? State::Ready : State::NotInstalled);
        if (!wasOpen) {
            setErrorMessage(tr("红石联机内核已退出，未能建立隧道"));
        }
        return;
    }

    const QString detail = QString::fromUtf8(m_stderrBuffer).trimmed();
    if (!detail.isEmpty()) {
        qDebug() << "Hongshi 内核错误输出：" << detail;
    }
    setState(State::Failed);
    setErrorMessage(tr("红石联机内核未能连接中转服务器，请检查网络或更换节点后重试"));
}

void HongshiClient::onProcessErrorOccurred(QProcess::ProcessError error)
{
    qDebug() << "Hongshi 内核进程出错：" << int(error);
    if (error == QProcess::FailedToStart) {
        resetTunnelInfo();
        setState(State::Fatal);
        setErrorMessage(tr("无法启动红石联机内核进程"));
    }
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
