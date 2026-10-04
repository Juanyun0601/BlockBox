/**
 * @file   GravityConeClient.cpp
 * @brief  基岩版联机客户端实现
 * @author BlockBox Team
 */

#include "GravityConeClient.h"

#include <functional>
#include <memory>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QResource>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

#include "utils/DownloadTaskManager.h"
#include "utils/plugin/PluginZip.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

/// 基岩版房间码字符集（与 GravityCone common.Charset 一致，排除 I/O）
const char *kCharset = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";

/// 将字符映射为 34 进制值，无效字符返回 -1
int lookupChar(QChar ch)
{
    QString chars = QString::fromLatin1(kCharset);
    return chars.indexOf(ch.toUpper());
}

/// 默认玩家名
const QString kDefaultPlayerName = QStringLiteral("BlockBox 玩家");

/// 平台 classifier（对应 release.yml 产物命名 gravitycone-cli-{os}-{arch}.zip）
QString platformClassifier()
{
    QString os;
#ifdef Q_OS_WIN
    os = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    os = QStringLiteral("darwin");
#else
    os = QStringLiteral("linux");
#endif
    QString arch;
#if defined(Q_PROCESSOR_X86_64)
    arch = QStringLiteral("amd64");
#elif defined(Q_PROCESSOR_X86)
    arch = QStringLiteral("amd64");
#elif defined(Q_PROCESSOR_ARM_64)
    arch = QStringLiteral("arm64");
#else
    arch = QStringLiteral("amd64");
#endif
    return os + QStringLiteral("-") + arch;
}

/// 可执行文件名（不含目录）
QString cliBinaryName()
{
    QString name = QStringLiteral("gravitycone-cli-") + platformClassifier();
#ifdef Q_OS_WIN
    name += QStringLiteral(".exe");
#endif
    return name;
}

} // namespace

GravityConeClient *GravityConeClient::instance()
{
    static GravityConeClient *s_inst = nullptr;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (!s_inst)
        s_inst = new GravityConeClient(qApp);
    return s_inst;
}

GravityConeClient::GravityConeClient(QObject *parent)
    : QObject(parent)
{
    // 房间状态轮询：仅房间中活跃
    m_statusTimer.setInterval(2000);
    m_statusTimer.setSingleShot(false);
    connect(&m_statusTimer, &QTimer::timeout, this, [this]() {
        sendRequest(QStringLiteral("room.status"), QJsonObject(),
                    QStringLiteral("status"));
    });

    scanInstalledVersion();
    loadBuiltinVersionInfo();

    if (!m_installedVersion.isEmpty()) {
        m_installStatus = InstallStatus::Ready;
        // 自动启动进程
        QTimer::singleShot(0, this, &GravityConeClient::start);
        QTimer::singleShot(100, this, &GravityConeClient::checkUpdate);
    } else {
        m_installStatus = InstallStatus::NotInstalled;
        // 未安装时也立即读取内置最新版本（仅本地比较，显示"下载 vX.X.X"）
        QTimer::singleShot(100, this, &GravityConeClient::checkUpdate);
    }
}

GravityConeClient::~GravityConeClient()
{
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->closeWriteChannel();
            if (!m_process->waitForFinished(1500))
                m_process->kill();
        }
        delete m_process;
        m_process = nullptr;
    }
}

bool GravityConeClient::isInRoom() const
{
    switch (m_state) {
    case State::HostStarting:
    case State::HostOk:
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

QString GravityConeClient::gravityConeRootDir() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/gravitycone");
}

QString GravityConeClient::gravityConeExecutablePath() const
{
    if (m_installedVersion.isEmpty())
        return QString();
    return gravityConeRootDir() + QStringLiteral("/") + m_installedVersion
           + QStringLiteral("/") + cliBinaryName();
}

QString GravityConeClient::latestLocalVersion() const
{
    QDir root(gravityConeRootDir());
    if (!root.exists())
        return QString();

    QStringList versions = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(versions.begin(), versions.end(), [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(a) > QVersionNumber::fromString(b);
    });
    for (const QString &ver : versions) {
        QDir verDir(root.absoluteFilePath(ver));
        if (verDir.exists(cliBinaryName()))
            return ver;
    }
    return QString();
}

void GravityConeClient::scanInstalledVersion()
{
    m_installedVersion = latestLocalVersion();
}

// ============================
// 进程管理
// ============================

void GravityConeClient::start()
{
    if (isProcessRunning())
        return;

    if (m_installedVersion.isEmpty())
        scanInstalledVersion();

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

void GravityConeClient::stop()
{
    if (!m_process)
        return;
    if (m_process->state() == QProcess::NotRunning) {
        delete m_process;
        m_process = nullptr;
        return;
    }
    // 发送 system.shutdown，随后 stdin 关闭会触发 CLI 自身清理退出
    sendRequest(QStringLiteral("system.shutdown"), QJsonObject(),
                QStringLiteral("shutdown"));
    m_process->closeWriteChannel();
    if (!m_process->waitForFinished(1500))
        m_process->kill();
}

void GravityConeClient::launchProcess()
{
    QString exePath = gravityConeExecutablePath();
    if (exePath.isEmpty() || !QFileInfo::exists(exePath)) {
        setErrorMessage(tr("未找到 GravityCone 可执行文件"));
        setState(State::Fatal);
        return;
    }

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &GravityConeClient::onProcessFinished);
        connect(m_process, &QProcess::errorOccurred,
                this, &GravityConeClient::onProcessErrorOccurred);
        connect(m_process, &QProcess::readyReadStandardOutput,
                this, &GravityConeClient::onStdoutReady);
        // 排空 stderr，防止管道写满阻塞进程（CLI 日志本身写入文件）
        connect(m_process, &QProcess::readyReadStandardError, m_process, [this]() {
            if (m_process)
                m_process->readAllStandardError();
        });
    }

    QStringList args;
    args << QStringLiteral("--vendor") << QStringLiteral("BlockBox");

    m_process->setProgram(exePath);
    m_process->setArguments(args);
    // stdout 使用行缓冲，通过 readLine 解析 JSON 行
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

#ifdef Q_OS_WIN
    // Windows 上以隐藏窗口方式启动，避免弹出控制台
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    m_waitingForReady = true;
    m_process->start();

    // 启动超时保底：30 秒未收到 system.ready 视为失败
    QTimer::singleShot(30000, this, [this]() {
        if (m_waitingForReady && m_state == State::Launching) {
            m_waitingForReady = false;
            setErrorMessage(tr("GravityCone 启动超时"));
            setState(State::Fatal);
        }
    });
}

void GravityConeClient::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(status);
    qDebug() << "GravityCone process exited";

    m_statusTimer.stop();
    m_waitingForReady = false;
    m_pendingRequests.clear();

    // 进程退出后回到未运行状态；已安装版本仍保留
    m_role.clear();
    m_roomCode.clear();
    m_serverAddress.clear();
    m_gamePort = 0;
    m_onlineCount = 0;
    m_players.clear();
    setState(State::NotInstalled);
}

void GravityConeClient::onProcessErrorOccurred(QProcess::ProcessError error)
{
    qDebug() << "GravityCone process error:" << error;
    if (error == QProcess::FailedToStart) {
        m_waitingForReady = false;
        setErrorMessage(tr("无法启动 GravityCone 进程"));
        setState(State::Fatal);
    }
}

void GravityConeClient::onStdoutReady()
{
    if (!m_process)
        return;
    m_stdinBuffer.append(m_process->readAllStandardOutput());

    int newlineIdx;
    while ((newlineIdx = m_stdinBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_stdinBuffer.left(newlineIdx).trimmed();
        m_stdinBuffer.remove(0, newlineIdx + 1);
        if (!line.isEmpty())
            handleLine(line);
    }
}

// ============================
// JSON stdio 协议
// ============================

void GravityConeClient::sendRequest(const QString &method, const QJsonObject &params,
                                    const QString &reqKind)
{
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        qWarning() << "GravityCone process not running, ignore request:" << method;
        return;
    }

    const int id = m_nextRequestId++;
    m_pendingRequests.insert(id, reqKind);

    QJsonObject req;
    req.insert(QStringLiteral("id"), id);
    req.insert(QStringLiteral("method"), method);
    if (!params.isEmpty())
        req.insert(QStringLiteral("params"), params);

    m_process->write(QJsonDocument(req).toJson(QJsonDocument::Compact));
    m_process->write("\n");
}

void GravityConeClient::handleLine(const QByteArray &line)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("id")))
        handleResponse(obj);
    else if (obj.contains(QStringLiteral("event")))
        handleEvent(obj);
}

void GravityConeClient::handleResponse(const QJsonObject &obj)
{
    const int id = obj.value(QStringLiteral("id")).toInt(-1);
    const QString status = obj.value(QStringLiteral("status")).toString();
    const QString kind = m_pendingRequests.take(id);

    if (status == QStringLiteral("progress")) {
        const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        const QString step = data.value(QStringLiteral("step")).toString();
        const QString message = data.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            m_progressMessage = message;
            emit progressUpdated(step, message);
        }
        return;
    }

    if (status == QStringLiteral("error")) {
        const QJsonObject errObj = obj.value(QStringLiteral("error")).toObject();
        m_errorCode = errObj.value(QStringLiteral("code")).toString();
        const QString message = errObj.value(QStringLiteral("message")).toString();
        m_errorMessage = message;

        // 创建/加入失败 → Exception；离开/轮询失败 → 尽量回到 Waiting
        if (kind == QStringLiteral("create") || kind == QStringLiteral("join")
            || kind == QStringLiteral("confirm_ended")) {
            setState(State::Exception);
        } else if (kind == QStringLiteral("leave")) {
            resetRoomData();
            setState(State::Waiting);
        }
        emit errorOccurred(message.isEmpty()
            ? tr("GravityCone 请求失败") : message);
        return;
    }

    // success
    const QJsonValue dataVal = obj.value(QStringLiteral("data"));
    const QJsonObject data = dataVal.toObject();

    if (kind == QStringLiteral("create")) {
        m_role = QStringLiteral("host");
        m_roomCode = data.value(QStringLiteral("code")).toString();
        m_gamePort = static_cast<quint16>(data.value(QStringLiteral("game_port")).toInt());
        m_onlineCount = data.value(QStringLiteral("online_count")).toInt();
        applyPlayers(data.value(QStringLiteral("players")));
        m_statusTimer.start();
        setState(State::HostOk);
    } else if (kind == QStringLiteral("join")) {
        applyJoinSuccess(data);
    } else if (kind == QStringLiteral("leave")) {
        resetRoomData();
        setState(State::Waiting);
    } else if (kind == QStringLiteral("status")) {
        handleRoomStatusData(data);
    } else if (kind == QStringLiteral("confirm_ended")) {
        // 确认后重新触发一次状态刷新
        QTimer::singleShot(200, this, &GravityConeClient::refreshStatus);
    }
}

void GravityConeClient::applyJoinSuccess(const QJsonObject &data)
{
    m_role = QStringLiteral("guest");
    m_roomCode = data.value(QStringLiteral("room_code")).toString();
    m_serverAddress = data.value(QStringLiteral("host_address")).toString();
    m_gamePort = static_cast<quint16>(data.value(QStringLiteral("game_port")).toInt());
    m_onlineCount = data.value(QStringLiteral("online_count")).toInt();
    applyPlayers(data.value(QStringLiteral("players")));
    m_statusTimer.start();
    setState(State::GuestOk);
}

void GravityConeClient::handleEvent(const QJsonObject &obj)
{
    const QString event = obj.value(QStringLiteral("event")).toString();
    const QJsonValue dataVal = obj.value(QStringLiteral("data"));

    if (event == QStringLiteral("system.ready")) {
        m_gravityConeVersion = dataVal.toObject().value(QStringLiteral("version")).toString();
        m_waitingForReady = false;
        if (m_state == State::Launching)
            setState(State::Waiting);
        else if (m_state == State::Fatal || m_state == State::NotInstalled)
            setState(State::Waiting);
        return;
    }

    if (event == QStringLiteral("paperconnect.room.info")) {
        // 加入成功后服务先推事件再回响应，二者数据一致
        const QJsonObject data = dataVal.toObject();
        if (m_state == State::GuestStarting)
            applyJoinSuccess(data);
        return;
    }

    if (event == QStringLiteral("paperconnect.connection.ready")) {
        if (m_state == State::GuestStarting) {
            const QJsonObject data = dataVal.toObject();
            if (data.contains(QStringLiteral("game_port")))
                m_gamePort = static_cast<quint16>(data.value(QStringLiteral("game_port")).toInt());
            m_statusTimer.start();
            setState(State::GuestOk);
        }
        return;
    }

    if (event == QStringLiteral("paperconnect.connection.error")
        || event == QStringLiteral("paperconnect.connection.port_busy")) {
        const QJsonObject data = dataVal.toObject();
        QString message = data.value(QStringLiteral("error")).toString();
        if (message.isEmpty())
            message = data.value(QStringLiteral("message")).toString();
        if (message.isEmpty())
            message = tr("基岩版联机连接异常");
        if (event == QStringLiteral("paperconnect.connection.port_busy")) {
            message = tr("本地游戏端口被占用，请关闭正在运行的基岩版后重试。") + message;
        }
        setErrorMessage(message);
        setState(State::Exception);
        return;
    }

    if (event == QStringLiteral("room.closed")
        || event == QStringLiteral("room.disconnected")) {
        if (isInRoom()) {
            const QJsonObject data = dataVal.toObject();
            const QString reason = data.value(QStringLiteral("reason")).toString();
            resetRoomData();
            setState(State::Waiting);
            if (!reason.isEmpty())
                emit errorOccurred(reason);
        }
        return;
    }

    if (event == QStringLiteral("room.player_joined")
        || event == QStringLiteral("room.player_left")
        || event == QStringLiteral("room.guest_player_list_updated")) {
        // 玩家列表以 room.status 轮询为准，事件触发一次刷新
        if (isInRoom())
            QTimer::singleShot(0, this, &GravityConeClient::refreshStatus);
        return;
    }
}

void GravityConeClient::handleRoomStatusData(const QJsonObject &data)
{
    const QString role = data.value(QStringLiteral("role")).toString();

    if (role == QStringLiteral("none")) {
        // 服务端已无房间（远端关闭等），回到空闲
        if (isInRoom()) {
            resetRoomData();
            setState(State::Waiting);
        }
        return;
    }

    if (role == QStringLiteral("host")) {
        const QString code = data.value(QStringLiteral("code")).toString();
        if (!code.isEmpty())
            m_roomCode = code;
        m_gamePort = static_cast<quint16>(data.value(QStringLiteral("game_port")).toInt());
        m_onlineCount = data.value(QStringLiteral("online_count")).toInt();
        applyPlayers(data.value(QStringLiteral("players")));
        m_role = QStringLiteral("host");
        if (m_state == State::HostOk)
            return;
        m_statusTimer.start();
        setState(State::HostOk);
    } else if (role == QStringLiteral("guest")) {
        const QString code = data.value(QStringLiteral("room_code")).toString();
        if (!code.isEmpty())
            m_roomCode = code;
        const QString addr = data.value(QStringLiteral("host_address")).toString();
        if (!addr.isEmpty())
            m_serverAddress = addr;
        m_gamePort = static_cast<quint16>(data.value(QStringLiteral("game_port")).toInt());
        m_onlineCount = data.value(QStringLiteral("online_count")).toInt();
        applyPlayers(data.value(QStringLiteral("players")));
        m_role = QStringLiteral("guest");
        if (m_state == State::GuestOk)
            return;
        m_statusTimer.start();
        setState(State::GuestOk);
    }
}

void GravityConeClient::applyPlayers(const QJsonValue &playersVal)
{
    if (!playersVal.isArray())
        return;

    QList<Player> newPlayers;
    const QJsonArray arr = playersVal.toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject p = v.toObject();
        Player player;
        player.name = p.value(QStringLiteral("player")).toString();
        player.clientId = p.value(QStringLiteral("clientId")).toString();
        player.isRoomHost = p.value(QStringLiteral("isRoomHost")).toBool();
        if (player.name.isEmpty())
            player.name = p.value(QStringLiteral("name")).toString();
        newPlayers.append(player);
    }

    if (newPlayers != m_players) {
        m_players = newPlayers;
        emit playersChanged();
    }
}

void GravityConeClient::resetRoomData()
{
    m_statusTimer.stop();
    m_role.clear();
    m_roomCode.clear();
    m_serverAddress.clear();
    m_gamePort = 0;
    m_onlineCount = 0;
    if (!m_players.isEmpty()) {
        m_players.clear();
        emit playersChanged();
    }
}

void GravityConeClient::setState(State newState)
{
    if (m_state == newState)
        return;
    m_state = newState;
    emit stateChanged();
}

void GravityConeClient::setErrorMessage(const QString &msg)
{
    m_errorMessage = msg;
    emit errorOccurred(msg);
}

// ============================
// 房间操作
// ============================

void GravityConeClient::createRoom(const QString &playerName)
{
    if (!isProcessRunning()) {
        if (m_installedVersion.isEmpty())
            setErrorMessage(tr("未安装 GravityCone 联机核心"));
        else
            setErrorMessage(tr("联机核心未启动"));
        return;
    }
    if (m_state != State::Waiting) {
        setErrorMessage(tr("当前状态无法创建房间"));
        return;
    }

    QString name = playerName.trimmed().isEmpty() ? kDefaultPlayerName : playerName.trimmed();

    QJsonObject params;
    params.insert(QStringLiteral("protocol"), QStringLiteral("paperconnect"));
    params.insert(QStringLiteral("player_name"), name);
    setState(State::HostStarting);
    sendRequest(QStringLiteral("room.create"), params, QStringLiteral("create"));
}

void GravityConeClient::joinRoom(const QString &code, const QString &playerName)
{
    if (!isProcessRunning()) {
        if (m_installedVersion.isEmpty())
            setErrorMessage(tr("未安装 GravityCone 联机核心"));
        else
            setErrorMessage(tr("联机核心未启动"));
        return;
    }
    if (m_state != State::Waiting) {
        setErrorMessage(tr("当前状态无法加入房间"));
        return;
    }

    QString name = playerName.trimmed().isEmpty() ? kDefaultPlayerName : playerName.trimmed();

    QJsonObject params;
    params.insert(QStringLiteral("code"), code.trimmed().toUpper());
    params.insert(QStringLiteral("player_name"), name);
    setState(State::GuestStarting);
    sendRequest(QStringLiteral("room.join"), params, QStringLiteral("join"));
}

void GravityConeClient::leaveRoom()
{
    if (!isProcessRunning())
        return;
    if (!isInRoom()) {
        // 异常状态点击"离开"= 重置回到空闲
        resetRoomData();
        setState(State::Waiting);
        return;
    }
    sendRequest(QStringLiteral("room.leave"), QJsonObject(), QStringLiteral("leave"));
}

void GravityConeClient::cancelJoin()
{
    if (!isProcessRunning())
        return;
    sendRequest(QStringLiteral("room.cancel_join"), QJsonObject(),
                QStringLiteral("cancel_join"));
    resetRoomData();
    setState(State::Waiting);
}

void GravityConeClient::confirmMinecraftEnded()
{
    if (!isProcessRunning())
        return;
    sendRequest(QStringLiteral("room.confirm_minecraft_ended"), QJsonObject(),
                QStringLiteral("confirm_ended"));
}

void GravityConeClient::refreshStatus()
{
    if (!isProcessRunning() || !isInRoom())
        return;
    sendRequest(QStringLiteral("room.status"), QJsonObject(), QStringLiteral("status"));
}

// ============================
// 邀请码校验（基岩版 P/ 格式）
// ============================

bool GravityConeClient::verifyRoomCode(const QString &code)
{
    // 参考 GravityCone core/protocol/paperconnect/roomcode.go
    // 格式：P/XXXX-XXXX-XXXX-XXXX（16 个 34 进制字符，S 部分小端值 % 7 == 0）
    QString s = code.trimmed().toUpper();

    if (s.startsWith(QStringLiteral("P/")))
        s = s.mid(2);

    s.remove(QLatin1Char('-'));
    if (s.length() != 16)
        return false;

    // 校验字符集
    for (const QChar &c : s) {
        if (lookupChar(c) < 0)
            return false;
    }

    // S 部分 = 后 8 字符，按小端 base-34 求值并判断是否整除 7
    // value = sum(chars[i] * 34^i)，i 从 0（最低位）开始
    qint64 value = 0;
    qint64 multiplier = 1;
    for (int i = 8; i < 16; ++i) {
        const int v = lookupChar(s.at(i));
        value = (value + v * multiplier) % 7;
        multiplier = (multiplier * 34) % 7;
    }
    return value == 0;
}

// ============================
// 下载安装
// ============================

void GravityConeClient::loadBuiltinVersionInfo()
{
    QFile f(QStringLiteral(":/resources/gravitycone_version.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot load builtin gravitycone_version.json";
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    const QJsonObject obj = doc.object();
    m_latestVersion = obj.value(QStringLiteral("version_latest")).toString();
}

void GravityConeClient::checkUpdate()
{
    if (m_installStatus == InstallStatus::Downloading
        || m_installStatus == InstallStatus::CheckingUpdate)
        return;

    if (m_latestVersion.isEmpty())
        loadBuiltinVersionInfo();

    if (m_latestVersion.isEmpty()) {
        m_installStatus = m_installedVersion.isEmpty()
            ? InstallStatus::NotInstalled : InstallStatus::Ready;
        emit installStatusChanged();
        return;
    }

    if (m_installedVersion.isEmpty()) {
        m_installStatus = InstallStatus::NotInstalled;
    } else {
        const int cmp = QVersionNumber::compare(
            QVersionNumber::fromString(m_installedVersion),
            QVersionNumber::fromString(m_latestVersion));
        m_installStatus = (cmp >= 0) ? InstallStatus::UpToDate
                                     : InstallStatus::UpdateAvailable;
    }
    emit installStatusChanged();
}

void GravityConeClient::downloadLatest()
{
    if (m_installStatus == InstallStatus::Downloading)
        return;

    if (m_latestVersion.isEmpty())
        loadBuiltinVersionInfo();

    if (!m_latestVersion.isEmpty() && m_latestVersion == m_installedVersion) {
        m_installStatus = InstallStatus::UpToDate;
        emit installStatusChanged();
        return;
    }

    if (m_latestVersion.isEmpty()) {
        setErrorMessage(tr("无法获取 GravityCone 最新版本号"));
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

QStringList GravityConeClient::buildDownloadUrls(const QString &version)
{
    QFile f(QStringLiteral(":/resources/gravitycone_version.json"));
    QStringList urls;
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        const QJsonObject obj = doc.object();

        const QString classifier = platformClassifier();
        auto substitute = [&](const QString &tmpl) -> QString {
            return QString(tmpl)
                .replace(QStringLiteral("${version}"), version)
                .replace(QStringLiteral("${classifier}"), classifier);
        };

        const QJsonArray cnArr = obj.value(QStringLiteral("downloads_CN")).toArray();
        for (const QJsonValue &v : cnArr) {
            const QString u = substitute(v.toString());
            if (!u.isEmpty())
                urls << u;
        }
        const QJsonArray ghArr = obj.value(QStringLiteral("downloads")).toArray();
        for (const QJsonValue &v : ghArr) {
            const QString u = substitute(v.toString());
            if (!u.isEmpty())
                urls << u;
        }
    }

    if (urls.isEmpty()) {
        urls << QStringLiteral(
            "https://github.com/Tianpao/GravityCone/releases/download/v%1/gravitycone-cli-%2.zip")
            .arg(version, platformClassifier());
    }
    return urls;
}

void GravityConeClient::downloadRelease(const QString &version)
{
    const QString rootDir = gravityConeRootDir();
    QDir().mkpath(rootDir);
    const QString archivePath = rootDir + QStringLiteral("/gravitycone-cli-%1.zip").arg(version);
    const QStringList urls = buildDownloadUrls(version);

    const QString taskId = DownloadTaskManager::instance()->addTask(
        tr("基岩版联机核心 v%1").arg(version), rootDir, QString(), QStringList());
    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading, tr("下载中"));
    DownloadTaskManager::instance()->updateTaskStep(taskId, tr("下载联机核心"));

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
        req.setTransferTimeout(30000);

        QNetworkReply *reply = m_networkManager.get(req);

        QTimer *timeoutTimer = new QTimer(this);
        timeoutTimer->setSingleShot(true);
        timeoutTimer->setInterval(30000);
        connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
            if (reply->isRunning())
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, timeoutTimer, &QTimer::deleteLater);
        timeoutTimer->start();

        connect(reply, &QNetworkReply::downloadProgress, this, [this, taskId](qint64 received, qint64 total) {
            if (total > 0) {
                m_downloadProgress = qreal(received) / qreal(total);
                emit downloadProgressChanged(m_downloadProgress);
                DownloadTaskManager::instance()->updateTaskProgressPercent(
                    taskId, static_cast<int>(m_downloadProgress * 100));
                DownloadTaskManager::instance()->updateTaskFileProgress(
                    taskId, QString(), received, total);
            }
        });

        connect(reply, &QNetworkReply::finished, this, [this, reply, archivePath, version, taskId, index, tryDownload]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "Download source" << index << "failed:" << reply->errorString();
                (*tryDownload)(index + 1);
                return;
            }

            QFile file(archivePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                setErrorMessage(tr("无法写入临时文件: %1").arg(archivePath));
                m_installStatus = InstallStatus::DownloadFailed;
                emit installStatusChanged();
                DownloadTaskManager::instance()->updateTaskStatus(
                    taskId, DownloadTaskStatus::Failed, tr("写入文件失败"));
                return;
            }
            file.write(reply->readAll());
            file.close();

            DownloadTaskManager::instance()->updateTaskStep(taskId, tr("解压安装中"));
            DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);

            extractAndInstall(archivePath, version, taskId);
        });
    };

    (*tryDownload)(0);
}

void GravityConeClient::extractAndInstall(const QString &archivePath, const QString &version, const QString &taskId)
{
    const QString targetDir = gravityConeRootDir() + QStringLiteral("/") + version;
    QDir().mkpath(targetDir);

    // 解压 zip（内含 gravitycone-cli-{os}-{arch}.exe 单一可执行文件）
    bool ok = false;
    const int entries = PluginZip::extractAllToDir(archivePath, targetDir, &ok);
    QFile::remove(archivePath);

    if (!ok || entries < 0 || !QFileInfo::exists(targetDir + QStringLiteral("/") + cliBinaryName())) {
        setErrorMessage(tr("解压 GravityCone 包失败"));
        m_installStatus = InstallStatus::DownloadFailed;
        emit installStatusChanged();
        DownloadTaskManager::instance()->updateTaskStatus(
            taskId, DownloadTaskStatus::Failed, tr("解压失败"));
        return;
    }

#ifndef Q_OS_WIN
    QFile::setPermissions(targetDir + QStringLiteral("/") + cliBinaryName(),
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
        | QFile::ReadGroup | QFile::ExeGroup
        | QFile::ReadOther | QFile::ExeOther);
#endif

    m_installedVersion = version;
    m_installStatus = InstallStatus::Ready;
    m_downloadProgress = 1.0;
    emit installStatusChanged();
    emit downloadProgressChanged(1.0);

    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Completed, tr("安装完成"));
    DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);

    // 自动启动
    start();
}
