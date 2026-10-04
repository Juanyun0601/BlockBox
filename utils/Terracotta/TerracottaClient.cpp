/**
 * @file   TerracottaClient.cpp
 * @brief  陶瓦联机客户端实现
 * @author BlockBox Team
 * @date   2026-07-07
 */

#include "TerracottaClient.h"

#include <functional>
#include <memory>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QResource>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVersionNumber>

#include "utils/DownloadTaskManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
/// 邀请码字符表（与 Terracotta room.rs CHARS 一致）
const char *kChars = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";

/// 将字符解码为 34 进制值（'I'→'1'，'O'→'0'，与 Terracotta lookup_char 一致）
int lookupChar(QChar ch)
{
    QChar c = ch;
    if (c == QLatin1Char('I')) c = QLatin1Char('1');
    if (c == QLatin1Char('O')) c = QLatin1Char('0');
    QString chars = QString::fromLatin1(kChars);
    int idx = chars.indexOf(c.toUpper());
    return idx; // -1 表示未找到
}

/// 默认玩家名
const QString kDefaultPlayerName = QStringLiteral("BlockBox Player");

/// Terracotta 公共节点列表 URL（参考 HMCL TerracottaNodeList）
const QString kNodeListUrl = QStringLiteral("https://terracotta.glavo.site/nodes");

/// 平台 classifier（用于下载 URL 模板替换）
/// 参考 HMCL Architecture.getCheckedName()：X86_64→"x86_64"，ARM64→"arm64"
QString platformClassifier()
{
#ifdef Q_OS_WIN
    QString os = QStringLiteral("windows");
#else
    QString os = QStringLiteral("linux");
#endif
#if defined(Q_PROCESSOR_X86_64)
    QString arch = QStringLiteral("x86_64");
#elif defined(Q_PROCESSOR_X86)
    QString arch = QStringLiteral("x86");
#elif defined(Q_PROCESSOR_ARM_64)
    QString arch = QStringLiteral("arm64");
#else
    QString arch = QStringLiteral("x86_64");
#endif
    return os + QStringLiteral("-") + arch;
}
} // namespace

TerracottaClient *TerracottaClient::instance()
{
    static TerracottaClient *s_inst = nullptr;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (!s_inst) {
        s_inst = new TerracottaClient(qApp);
    }
    return s_inst;
}

TerracottaClient::TerracottaClient(QObject *parent)
    : QObject(parent)
{
    // 轮询定时器：活跃时 500ms，空闲时由 start() 切换到较慢节奏
    m_pollTimer.setInterval(500);
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, &TerracottaClient::fetchState);

    // 端口文件检测：200ms 一次，直到读取到端口
    m_portFileTimer.setInterval(200);
    m_portFileTimer.setSingleShot(false);
    connect(&m_portFileTimer, &QTimer::timeout, this, &TerracottaClient::tryReadPortFile);

    scanInstalledVersion();
    loadBuiltinVersionInfo(); // 从内置资源读取最新版本号（瞬间完成，无需网络）

    if (!m_installedVersion.isEmpty()) {
        m_installStatus = InstallStatus::Ready;
        m_state = State::NotInstalled; // 进程未启动，先标记为未启动状态
        // 自动启动
        QTimer::singleShot(0, this, &TerracottaClient::start);
        // 启动后立即检查版本（本地比较，瞬间完成）
        QTimer::singleShot(100, this, &TerracottaClient::checkUpdate);
    } else {
        // 未安装时也立即检查版本（仅本地比较，显示"下载 vX.X.X"）
        QTimer::singleShot(100, this, &TerracottaClient::checkUpdate);
    }
}

TerracottaClient::~TerracottaClient()
{
    if (m_process) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
        }
        delete m_process;
        m_process = nullptr;
    }
}

bool TerracottaClient::isInRoom() const
{
    switch (m_state) {
    case State::HostScanning:
    case State::HostStarting:
    case State::HostOk:
    case State::GuestConnecting:
    case State::GuestStarting:
    case State::GuestOk:
        return true;
    default:
        return false;
    }
}

// ============================
// 路径与版本管理
// ============================

QString TerracottaClient::terracottaRootDir() const
{
    // 保存在 BlockBox 可执行文件所在目录下的 terracotta 子文件夹
    return QCoreApplication::applicationDirPath() + QStringLiteral("/terracotta");
}

QString TerracottaClient::terracottaExecutablePath() const
{
    if (m_installedVersion.isEmpty()) return QString();
    // 参考 HMCL TerracottaMetadata.locateProvider：
    // 文件名格式 terracotta-{version}-{os}-{arch}[.exe]
    // arch 参考 HMCL Architecture.getCheckedName()：X86_64→"x86_64"，ARM64→"arm64"
    QString classifier = platformClassifier();
    QString fileName = QStringLiteral("terracotta-%1-%2").arg(m_installedVersion, classifier);
#ifdef Q_OS_WIN
    fileName += QStringLiteral(".exe");
#endif
    return terracottaRootDir() + QStringLiteral("/") + m_installedVersion
           + QStringLiteral("/") + fileName;
}

QString TerracottaClient::latestLocalVersion() const
{
    QDir root(terracottaRootDir());
    if (!root.exists()) return QString();

    QStringList versions = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    // 简单按字符串排序取最大值（Terracotta 版本号格式为 0.4.2 等）
    std::sort(versions.begin(), versions.end(), [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(a) > QVersionNumber::fromString(b);
    });
    for (const QString &ver : versions) {
        QDir verDir(root.absoluteFilePath(ver));
        QStringList files = verDir.entryList(QDir::Files);
        for (const QString &f : files) {
            if (f.startsWith(QStringLiteral("terracotta-"))) {
                return ver;
            }
        }
    }
    return QString();
}

void TerracottaClient::scanInstalledVersion()
{
    m_installedVersion = latestLocalVersion();
}

// ============================
// 进程管理
// ============================

void TerracottaClient::start()
{
    if (isProcessRunning()) return;

    // 仅在版本未知时扫描本地已安装版本（避免清空 extractAndInstall 刚设置的版本号）
    if (m_installedVersion.isEmpty()) {
        scanInstalledVersion();
    }

    if (m_installedVersion.isEmpty()) {
        m_installStatus = InstallStatus::NotInstalled;
        emit installStatusChanged();
        setState(State::NotInstalled);
        return;
    }

    m_installStatus = InstallStatus::Ready;
    emit installStatusChanged();

    setState(State::Launching);
    launchProcess();
}

void TerracottaClient::launchProcess()
{
    QString exePath = terracottaExecutablePath();
    if (exePath.isEmpty() || !QFileInfo::exists(exePath)) {
        setErrorMessage(tr("未找到 Terracotta 可执行文件"));
        setState(State::Fatal);
        return;
    }

    // 准备端口文件路径（传给 Terracotta --hmcl 参数）
    QString tempDir = QDir::tempPath();
    qint64 pid = QCoreApplication::applicationPid();
    qint64 rand = QRandomGenerator::global()->generate64();
    m_portFilePath = tempDir + QStringLiteral("/blockbox-terracotta-%1-%2")
                          .arg(pid).arg(rand);

    // 清理可能存在的旧文件
    QFile::remove(m_portFilePath);

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &TerracottaClient::onProcessFinished);
        connect(m_process, &QProcess::errorOccurred,
                this, &TerracottaClient::onProcessErrorOccurred);
    }

    QStringList args;
    args << QStringLiteral("--hmcl") << m_portFilePath;
    m_process->setProgram(exePath);
    m_process->setArguments(args);

#ifdef Q_OS_WIN
    // 在 Windows 上以隐藏窗口方式启动，避免弹出控制台
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    m_process->start();

    // 启动端口文件轮询（最多等待 30 秒）
    m_portFileTimer.start();
    QTimer::singleShot(30000, this, [this]() {
        if (m_portFileTimer.isActive() && m_serverPort == 0) {
            m_portFileTimer.stop();
            setErrorMessage(tr("Terracotta 启动超时"));
            setState(State::Fatal);
        }
    });
}

void TerracottaClient::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    qDebug() << "Terracotta process exited with code" << exitCode;

    // 参考 Terracotta main.rs / lock_windows.rs：
    // Terracotta --hmcl 模式下会启动 --hmcl2 子进程作为实际 daemon，然后 wrapper 退出。
    // 在 secondary 模式下，进程读取已有 daemon 的端口写入端口文件后立即退出。
    // 这两种情况下 wrapper 进程退出都是正常行为，不代表 daemon 已停止。
    //
    // 情况1：端口还未读取，进程已退出（secondary 模式写端口文件后退出）
    if (m_serverPort == 0 && m_portFileTimer.isActive()) {
        qDebug() << "Process exited before port read. Continuing to poll port file (secondary mode).";
        QTimer::singleShot(10000, this, [this]() {
            if (m_serverPort == 0) {
                m_portFileTimer.stop();
                setErrorMessage(tr("Terracotta 启动超时：未能获取端口"));
                setState(State::Fatal);
            }
        });
        return;
    }

    // 情况2：端口已读取，进程退出（wrapper 在端口文件写入后正常退出）
    // daemon（可能是本实例的 --hmcl2 子进程，也可能是系统已有的 daemon）仍在运行。
    // 不清除端口、不改变状态，继续通过 HTTP 轮询检测 daemon 可用性。
    // 参考 HMCL：HMCL 根本不跟踪进程，只通过端口文件和 HTTP 轮询判断状态。
    qDebug() << "Wrapper process exited after port was read. Daemon should still be running on port"
             << m_serverPort << "- continuing to poll.";
    m_portFileTimer.stop();
    // 不停止 m_pollTimer，不清除 m_serverPort，不改变状态
    // 如果 daemon 确实不可用，fetchState 的连续错误计数器会检测到并切换状态
}

void TerracottaClient::onProcessErrorOccurred(QProcess::ProcessError error)
{
    qDebug() << "Terracotta process error:" << error;
    if (error == QProcess::FailedToStart) {
        m_portFileTimer.stop();
        setErrorMessage(tr("无法启动 Terracotta 进程"));
        setState(State::Fatal);
    }
}

void TerracottaClient::tryReadPortFile()
{
    if (m_portFilePath.isEmpty()) {
        m_portFileTimer.stop();
        return;
    }

    QFile file(m_portFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return; // 文件还没创建
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return;
    QJsonObject obj = doc.object();
    if (!obj.contains(QStringLiteral("port"))) return;

    int port = obj.value(QStringLiteral("port")).toInt(0);
    if (port <= 0) return;

    m_serverPort = port;
    m_portFileTimer.stop();
    QFile::remove(m_portFilePath);
    m_consecutiveErrors = 0; // 重置错误计数

    // 端口就绪，立即进入 Waiting 状态（参考 HMCL Unknown(port) -> Waiting 转换）
    // 不依赖后端首次轮询返回，让 UI 立即可用
    setState(State::Waiting);

    // 端口就绪，开始轮询状态并获取元数据
    fetchMetadata();
    fetchPublicNodes();
    m_pollTimer.start();
    // 立即触发一次
    QTimer::singleShot(0, this, &TerracottaClient::fetchState);
}

// ============================
// HTTP API
// ============================

void TerracottaClient::fetchState()
{
    if (m_serverPort == 0) return;

    QUrl url(QStringLiteral("http://127.0.0.1:%1/state").arg(m_serverPort));
    QNetworkRequest req(url);
    req.setRawHeader("Cache-Control", "no-cache");

    QNetworkReply *reply = m_networkManager.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // HTTP 请求失败 — daemon 可能已停止
            // 参考 HMCL：连续多次失败后认为 daemon 不可用，重置状态
            m_consecutiveErrors++;
            qDebug() << "State poll failed, consecutive errors:" << m_consecutiveErrors;
            if (m_consecutiveErrors >= 10) {
                qDebug() << "Daemon appears to be dead. Resetting to NotInstalled.";
                m_pollTimer.stop();
                m_serverPort = 0;
                m_lastStateIndex = -1;
                m_isFakeScanning = false;
                m_consecutiveErrors = 0;
                setState(State::NotInstalled);
            }
            return;
        }
        m_consecutiveErrors = 0; // 请求成功，重置错误计数
        QByteArray data = reply->readAll();
        handleStateResponse(data);
    });
}

void TerracottaClient::handleStateResponse(const QByteArray &data)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return;
    QJsonObject obj = doc.object();

    QString stateStr = obj.value(QStringLiteral("state")).toString();
    int index = obj.value(QStringLiteral("index")).toInt(-1);

    // 参考 HMCL TerracottaManager.runBackground()：
    // index 是状态新旧判别依据，只有 object.index > current.index 才采纳
    // 但假状态（m_isFakeScanning=true 时 m_lastStateIndex=-1）需要特殊处理：
    //   后端返回的任何 index >= 0 都应采纳（因为 -1 < 0）
    if (index >= 0 && index <= m_lastStateIndex) {
        // 旧状态或相同状态，丢弃（参考 HMCL: if (object.index <= index) continue;）
        return;
    }
    m_lastStateIndex = index;
    m_isFakeScanning = false; // 收到后端真实状态，清除假状态标志

    QList<Profile> newProfiles;
    QString roomCode;
    QString serverUrl;

    if (stateStr == QStringLiteral("waiting")) {
        setState(State::Waiting);
    } else if (stateStr == QStringLiteral("host-scanning")) {
        setState(State::HostScanning);
    } else if (stateStr == QStringLiteral("host-starting")) {
        roomCode = obj.value(QStringLiteral("room")).toString();
        setState(State::HostStarting);
    } else if (stateStr == QStringLiteral("host-ok")) {
        roomCode = obj.value(QStringLiteral("room")).toString();
        QJsonArray arr = obj.value(QStringLiteral("profiles")).toArray();
        for (const QJsonValue &v : arr) {
            QJsonObject p = v.toObject();
            Profile prof;
            prof.machineId = p.value(QStringLiteral("machine_id")).toString();
            prof.name = p.value(QStringLiteral("name")).toString();
            prof.vendor = p.value(QStringLiteral("vendor")).toString();
            prof.kind = p.value(QStringLiteral("kind")).toString();
            newProfiles.append(prof);
        }
        setState(State::HostOk);
    } else if (stateStr == QStringLiteral("guest-connecting")) {
        roomCode = obj.value(QStringLiteral("room")).toString();
        setState(State::GuestConnecting);
    } else if (stateStr == QStringLiteral("guest-starting")) {
        roomCode = obj.value(QStringLiteral("room")).toString();
        setState(State::GuestStarting);
    } else if (stateStr == QStringLiteral("guest-ok")) {
        serverUrl = obj.value(QStringLiteral("url")).toString();
        QJsonArray arr = obj.value(QStringLiteral("profiles")).toArray();
        for (const QJsonValue &v : arr) {
            QJsonObject p = v.toObject();
            Profile prof;
            prof.machineId = p.value(QStringLiteral("machine_id")).toString();
            prof.name = p.value(QStringLiteral("name")).toString();
            prof.vendor = p.value(QStringLiteral("vendor")).toString();
            prof.kind = p.value(QStringLiteral("kind")).toString();
            newProfiles.append(prof);
        }
        setState(State::GuestOk);
    } else if (stateStr == QStringLiteral("exception")) {
        int type = obj.value(QStringLiteral("type")).toInt(0);
        m_exceptionType = static_cast<ExceptionType>(type);
        setState(State::Exception);
    } else {
        qDebug() << "Unknown Terracotta state:" << stateStr;
    }

    // 更新数据
    bool profilesChanged_ = (newProfiles != m_profiles);
    bool roomChanged = (roomCode != m_roomCode);
    bool urlChanged = (serverUrl != m_serverUrl);

    m_profiles = newProfiles;
    m_roomCode = roomCode;
    m_serverUrl = serverUrl;

    if (profilesChanged_) emit profilesChanged();
    if (roomChanged || urlChanged) emit stateChanged();
}

void TerracottaClient::callApi(const QString &path)
{
    if (m_serverPort == 0) return;
    QUrl url(QStringLiteral("http://127.0.0.1:%1%2").arg(m_serverPort).arg(path));
    QNetworkRequest req(url);
    QNetworkReply *reply = m_networkManager.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Terracotta API call failed:" << reply->errorString();
        }
        // 立即触发状态刷新
        QTimer::singleShot(100, this, &TerracottaClient::fetchState);
    });
}

void TerracottaClient::fetchMetadata()
{
    if (m_serverPort == 0) return;
    QUrl url(QStringLiteral("http://127.0.0.1:%1/meta").arg(m_serverPort));
    QNetworkRequest req(url);
    QNetworkReply *reply = m_networkManager.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = doc.object();
        m_terracottaVersion = obj.value(QStringLiteral("version")).toString();
        emit stateChanged();
    });
}

void TerracottaClient::fetchPublicNodes()
{
    if (m_nodesFetched) return;
    QNetworkRequest req((QUrl(kNodeListUrl)));
    req.setRawHeader("User-Agent", "BlockBox");
    QNetworkReply *reply = m_networkManager.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_nodesFetched = true;
        if (reply->error() != QNetworkReply::NoError) return;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isArray()) return;
        QJsonArray arr = doc.array();
        m_publicNodes.clear();
        for (const QJsonValue &v : arr) {
            QJsonObject obj = v.toObject();
            QString url = obj.value(QStringLiteral("url")).toString();
            if (!url.isEmpty()) {
                m_publicNodes.append(url);
            }
        }
    });
}

// ============================
// 状态切换
// ============================

void TerracottaClient::setState(State newState)
{
    if (m_state == newState) return;
    m_state = newState;
    emit stateChanged();
}

void TerracottaClient::setErrorMessage(const QString &msg)
{
    m_errorMessage = msg;
    emit errorOccurred(msg);
}

// ============================
// 房间操作
// ============================

void TerracottaClient::createRoom(const QString &playerName)
{
    // 参考 HMCL TerracottaManager.setScanning()：
    // 1. 检查进程已启动（端口已知）
    // 2. 立即设置本地假状态 HostScanning（UI 立即响应）
    // 3. 异步获取节点列表后发送 /state/scanning 请求
    if (m_serverPort == 0) {
        setErrorMessage(tr("联机核心未启动"));
        return;
    }
    if (m_state != State::Waiting) {
        setErrorMessage(tr("当前状态无法创建房间"));
        return;
    }

    QString name = playerName.trimmed().isEmpty() ? kDefaultPlayerName : playerName.trimmed();

    // 立即设置本地假状态（参考 HMCL HostScanning(-1, -1, null)）
    // 让 UI 立即切换到"扫描中"画面
    m_isFakeScanning = true;
    m_lastStateIndex = -1; // 重置 index，确保后端返回的任何状态都能被采纳
    setState(State::HostScanning);

    // 异步获取节点列表后发送 /state/scanning 请求
    auto sendScanningRequest = [this, name]() {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("player"), name);
        for (const QString &node : m_publicNodes) {
            query.addQueryItem(QStringLiteral("public_nodes"), node);
        }
        QString path = QStringLiteral("/state/scanning?") + query.toString(QUrl::FullyEncoded);
        callApi(path);
    };

    if (m_nodesFetched) {
        // 节点列表已获取，直接发送
        sendScanningRequest();
    } else {
        // 节点列表未获取，先获取再发送（参考 HMCL Task.supplyAsync(TerracottaNodeList::fetch).thenComposeAsync(...)）
        QNetworkRequest req((QUrl(kNodeListUrl)));
        req.setRawHeader("User-Agent", "BlockBox");
        req.setTransferTimeout(8000);
        QNetworkReply *reply = m_networkManager.get(req);

        QTimer *timeoutTimer = new QTimer(this);
        timeoutTimer->setSingleShot(true);
        timeoutTimer->setInterval(8000);
        connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
            if (reply->isRunning()) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, timeoutTimer, &QTimer::deleteLater);
        timeoutTimer->start();

        connect(reply, &QNetworkReply::finished, this, [this, reply, sendScanningRequest]() {
            reply->deleteLater();
            m_nodesFetched = true;
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                if (doc.isArray()) {
                    QJsonArray arr = doc.array();
                    m_publicNodes.clear();
                    for (const QJsonValue &v : arr) {
                        QJsonObject obj = v.toObject();
                        QString url = obj.value(QStringLiteral("url")).toString();
                        if (!url.isEmpty()) {
                            m_publicNodes.append(url);
                        }
                    }
                }
            }
            // 无论节点列表是否获取成功，都发送 /state/scanning 请求
            sendScanningRequest();
        });
    }
}

bool TerracottaClient::joinRoom(const QString &roomCode, const QString &playerName)
{
    if (!verifyRoomCode(roomCode)) {
        setErrorMessage(tr("邀请码格式无效"));
        return false;
    }
    if (m_state != State::Waiting) {
        setErrorMessage(tr("当前状态无法加入房间"));
        return false;
    }
    QString name = playerName.trimmed().isEmpty() ? kDefaultPlayerName : playerName.trimmed();

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("room"), roomCode.trimmed().toUpper());
    query.addQueryItem(QStringLiteral("player"), name);
    for (const QString &node : m_publicNodes) {
        query.addQueryItem(QStringLiteral("public_nodes"), node);
    }
    QString path = QStringLiteral("/state/guesting?") + query.toString(QUrl::FullyEncoded);
    callApi(path);
    return true;
}

void TerracottaClient::leaveRoom()
{
    if (m_serverPort == 0) return;
    callApi(QStringLiteral("/state/ide"));
}

// ============================
// 邀请码校验
// ============================

bool TerracottaClient::verifyRoomCode(const QString &code)
{
    // 参考 Terracotta room.rs::parse
    // 格式：U/XXXX-XXXX-XXXX-XXXX（共 20 字符），16 个 34 进制字符
    // 校验：值能被 7 整除

    QString s = code.trimmed().toUpper();
    // 将 I→1，O→0
    QString normalized;
    for (QChar c : s) {
        if (c == QLatin1Char('I')) normalized += QLatin1Char('1');
        else if (c == QLatin1Char('O')) normalized += QLatin1Char('0');
        else normalized += c;
    }

    // 必须包含 U/ 前缀
    if (!normalized.startsWith(QStringLiteral("U/"))) return false;

    QString body = normalized.mid(2);
    // 期望格式 XXXX-XXXX-XXXX-XXXX（19 字符）
    if (body.length() != 19) return false;
    // 检查分隔符位置
    if (body[4] != QLatin1Char('-') || body[9] != QLatin1Char('-')
        || body[14] != QLatin1Char('-')) {
        return false;
    }

    // 移除分隔符，得到 16 个字符
    QString digits = body;
    digits.remove(QLatin1Char('-'));
    if (digits.length() != 16) return false;

    // 计算 34 进制值
    // Terracotta 解析时是从右到左（低位到高位），即 digits[15] 是最低位
    // 但读取顺序是从 i=15 到 i=0（参考 room.rs::parse 循环）
    // 实际上 parse 的循环是 for i in (0..16).rev()，对应 code[15] 是最低位
    // 我们这里 digits[0] 对应 code 的第一个字符（最高位）
    // 所以按 read 順序 digits[0] 是最高位
    // 为避免溢出，使用 quint128 不可行（Qt 无原生支持），改用字符串大数模 7

    // 34 mod 7 = 6，所以可以用模运算逐步计算
    // value = sum(digits[i] * 34^(15-i))
    // value mod 7 = sum((digits[i] mod 7) * (34^(15-i) mod 7)) mod 7
    // 34 mod 7 = 6, 34^k mod 7 = 6^k mod 7
    // 6^1=6, 6^2=36 mod 7=1, 6^3=6, 6^4=1, ...
    // 所以 34^k mod 7 = 6 if k odd, 1 if k even

    int mod = 0;
    for (int i = 0; i < 16; ++i) {
        int v = lookupChar(digits[i]);
        if (v < 0) return false;
        int power = 15 - i;
        int powerMod = (power % 2 == 1) ? 6 : 1;
        mod = (mod + (v % 7) * powerMod) % 7;
    }
    return mod == 0;
}

// ============================
// 下载安装
// ============================

void TerracottaClient::loadBuiltinVersionInfo()
{
    // 参考 HMCL TerracottaMetadata：从内置资源读取版本配置（无需网络请求）
    QFile f(QStringLiteral(":/resources/terracotta_version.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot load builtin terracotta_version.json";
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    QJsonObject obj = doc.object();
    m_latestVersion = obj.value(QStringLiteral("version_latest")).toString();
}

void TerracottaClient::checkUpdate()
{
    // 仅在空闲状态（未安装/已就绪/已是最新/有更新）时才检查
    if (m_installStatus == InstallStatus::Downloading
        || m_installStatus == InstallStatus::CheckingUpdate) {
        return;
    }

    if (m_latestVersion.isEmpty()) {
        loadBuiltinVersionInfo();
    }
    if (m_latestVersion.isEmpty()) {
        // 内置配置读取失败
        m_installStatus = m_installedVersion.isEmpty()
            ? InstallStatus::NotInstalled
            : InstallStatus::Ready;
        emit installStatusChanged();
        return;
    }

    // 本地版本比较（瞬间完成，无网络请求）
    if (m_installedVersion.isEmpty()) {
        // 未安装
        m_installStatus = InstallStatus::NotInstalled;
    } else {
        int cmp = QVersionNumber::compare(
            QVersionNumber::fromString(m_installedVersion),
            QVersionNumber::fromString(m_latestVersion));
        if (cmp >= 0) {
            m_installStatus = InstallStatus::UpToDate;
        } else {
            m_installStatus = InstallStatus::UpdateAvailable;
        }
    }
    emit installStatusChanged();
}

void TerracottaClient::downloadLatest()
{
    if (m_installStatus == InstallStatus::Downloading) return;

    if (m_latestVersion.isEmpty()) {
        loadBuiltinVersionInfo();
    }

    // 若已安装相同版本，无需下载
    if (!m_latestVersion.isEmpty() && m_latestVersion == m_installedVersion) {
        m_installStatus = InstallStatus::UpToDate;
        emit installStatusChanged();
        return;
    }

    if (m_latestVersion.isEmpty()) {
        setErrorMessage(tr("无法获取 Terracotta 最新版本号"));
        m_installStatus = InstallStatus::DownloadFailed;
        emit installStatusChanged();
        return;
    }

    m_installStatus = InstallStatus::Downloading;
    m_downloadProgress = 0.0;
    emit installStatusChanged();
    emit downloadProgressChanged(0.0);

    downloadRelease(m_latestVersion);
}

QStringList TerracottaClient::buildDownloadUrls(const QString &version)
{
    // 参考 HMCL terracotta.json：优先使用国内镜像源，GitHub 作为备用
    QFile f(QStringLiteral(":/resources/terracotta_version.json"));
    QStringList urls;
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        QJsonObject obj = doc.object();

        QString classifier = platformClassifier();
        // 模板替换
        auto substitute = [&](const QString &tmpl) -> QString {
            return QString(tmpl)
                .replace(QStringLiteral("${version}"), version)
                .replace(QStringLiteral("${classifier}"), classifier);
        };

        // 优先国内镜像源
        QJsonArray cnArr = obj.value(QStringLiteral("downloads_CN")).toArray();
        for (const QJsonValue &v : cnArr) {
            QString u = substitute(v.toString());
            if (!u.isEmpty()) urls << u;
        }
        // 然后 GitHub
        QJsonArray ghArr = obj.value(QStringLiteral("downloads")).toArray();
        for (const QJsonValue &v : ghArr) {
            QString u = substitute(v.toString());
            if (!u.isEmpty()) urls << u;
        }
    }

    // 兜底：若配置读取失败，使用默认 GitHub URL
    if (urls.isEmpty()) {
        QString classifier = platformClassifier();
        urls << QStringLiteral(
            "https://github.com/burningtnt/Terracotta/releases/download/v%1/terracotta-%1-%2-pkg.tar.gz")
                .arg(version, classifier);
    }
    return urls;
}

void TerracottaClient::downloadRelease(const QString &version)
{
    QString rootDir = terracottaRootDir();
    QDir().mkpath(rootDir);
    QString archivePath = rootDir + QStringLiteral("/terracotta-%1-pkg.tar.gz").arg(version);
    QStringList urls = buildDownloadUrls(version);

    // 创建下载任务（接入项目任务栏，下载完成后任务自动消失）
    QString taskId = DownloadTaskManager::instance()->addTask(
        tr("陶瓦联机核心 v%1").arg(version), rootDir, QString(), QStringList());
    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading, tr("下载中"));
    DownloadTaskManager::instance()->updateTaskStep(taskId, tr("下载联机核心"));

    // 多镜像源依次尝试（第一个失败自动切换下一个）
    auto tryDownload = std::make_shared<std::function<void(int)>>();
    *tryDownload = [this, urls, archivePath, version, taskId, tryDownload](int index) {
        if (index >= urls.size()) {
            setErrorMessage(tr("所有下载源均失败，请检查网络后重试"));
            m_installStatus = InstallStatus::DownloadFailed;
            emit installStatusChanged();
            DownloadTaskManager::instance()->updateTaskStatus(
                taskId, DownloadTaskStatus::Failed, tr("下载失败"));
            return;
        }

        const QString &url = urls[index];
        QNetworkRequest req((QUrl(url)));
        req.setRawHeader("User-Agent", "BlockBox");
        req.setTransferTimeout(30000); // 单源 30 秒超时

        QNetworkReply *reply = m_networkManager.get(req);

        // 超时保底
        QTimer *timeoutTimer = new QTimer(this);
        timeoutTimer->setSingleShot(true);
        timeoutTimer->setInterval(30000);
        connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
            if (reply->isRunning()) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, timeoutTimer, &QTimer::deleteLater);
        timeoutTimer->start();

        connect(reply, &QNetworkReply::downloadProgress, this, [this, taskId](qint64 received, qint64 total) {
            if (total > 0) {
                m_downloadProgress = qreal(received) / qreal(total);
                emit downloadProgressChanged(m_downloadProgress);
                int pct = static_cast<int>(m_downloadProgress * 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
                DownloadTaskManager::instance()->updateTaskFileProgress(
                    taskId, QString(), received, total);
            }
        });

        connect(reply, &QNetworkReply::finished, this, [this, reply, archivePath, version, taskId, index, tryDownload]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "Download source" << index << "failed:" << reply->errorString();
                // 尝试下一个镜像源
                (*tryDownload)(index + 1);
                return;
            }

            QByteArray data = reply->readAll();
            QFile file(archivePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                setErrorMessage(tr("无法写入临时文件: %1").arg(archivePath));
                m_installStatus = InstallStatus::DownloadFailed;
                emit installStatusChanged();
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Failed, tr("写入文件失败"));
                return;
            }
            file.write(data);
            file.close();

            // 进入解压阶段
            DownloadTaskManager::instance()->updateTaskStep(taskId, tr("解压安装中"));
            DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);

            extractAndInstall(archivePath, version, taskId);
        });
    };

    (*tryDownload)(0);
}

void TerracottaClient::extractAndInstall(const QString &archivePath, const QString &version, const QString &taskId)
{
    // 创建版本目录
    QString targetDir = terracottaRootDir() + QStringLiteral("/") + version;
    QDir().mkpath(targetDir);

    // 调用系统 tar 解压（Windows 10+ 自带 tar）
    QProcess *tar = new QProcess(this);
    tar->setWorkingDirectory(targetDir);
    tar->setProgram(QStringLiteral("tar"));
    tar->setArguments({QStringLiteral("-xzf"), archivePath, QStringLiteral("-C"), targetDir});

    connect(tar, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, tar, archivePath, version, targetDir, taskId](int exitCode, QProcess::ExitStatus) {
        tar->deleteLater();
        QFile::remove(archivePath);

        if (exitCode != 0) {
            setErrorMessage(tr("解压 Terracotta 包失败，退出码 %1").arg(exitCode));
            m_installStatus = InstallStatus::DownloadFailed;
            emit installStatusChanged();
            DownloadTaskManager::instance()->updateTaskStatus(
                taskId, DownloadTaskStatus::Failed, tr("解压失败"));
            return;
        }

        // 设置可执行权限（非 Windows）
#ifndef Q_OS_WIN
        QDir dir(targetDir);
        QStringList files = dir.entryList(QDir::Files);
        for (const QString &f : files) {
            if (f.startsWith(QStringLiteral("terracotta-"))) {
                QFile::setPermissions(dir.absoluteFilePath(f),
                    QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                    | QFile::ReadGroup | QFile::ExeGroup
                    | QFile::ReadOther | QFile::ExeOther);
            }
        }
#endif

        // 更新已安装版本
        m_installedVersion = version;
        m_installStatus = InstallStatus::Ready;
        m_downloadProgress = 1.0;
        emit installStatusChanged();
        emit downloadProgressChanged(1.0);

        // 标记任务完成（TaskBar 会自动以淡出动画移除任务卡片）
        DownloadTaskManager::instance()->updateTaskStatus(
            taskId, DownloadTaskStatus::Completed, tr("安装完成"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);

        // 自动启动
        start();
    });

    tar->start();
}

