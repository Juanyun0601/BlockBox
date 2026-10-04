/**
 * @file   LanTransfer.cpp
 * @brief  局域网互传：设备发现 + 文件/目录收发服务实现
 * @author BlockBox Team
 * @date   2026-09-26
 */
#include "LanTransfer.h"

#include "platform.h"
#include "utils/DownloadTaskManager.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>
#include <QtGlobal>

#include <algorithm>

namespace {

constexpr quint16 kDiscoveryPort = 47321;
constexpr char kReqTag[] = "BBX_DISC_REQ|";
constexpr char kRspTag[] = "BBX_DISC_RSP|";
constexpr int kDeviceTimeoutMs = 10000;   ///< 设备超时移除时间
constexpr int kBroadcastIntervalMs = 2000;///< 广播探测间隔
constexpr int kChunkSize = 64 * 1024;     ///< 单次读写块大小
constexpr qint64 kSendHighWater = 1024 * 1024; ///< 发送端未应答字节水位

/** Windows 下路径前缀比较不区分大小写 */
bool pathStartsWith(const QString &path, const QString &prefix)
{
#ifdef Q_OS_WIN
    return path.startsWith(prefix, Qt::CaseInsensitive);
#else
    return path.startsWith(prefix, Qt::CaseSensitive);
#endif
}

bool pathEquals(const QString &path, const QString &other)
{
#ifdef Q_OS_WIN
    return path.compare(other, Qt::CaseInsensitive) == 0;
#else
    return path.compare(other, Qt::CaseSensitive) == 0;
#endif
}

QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(path);
}

} // namespace

LanTransfer *LanTransfer::m_instance = nullptr;

LanTransfer *LanTransfer::instance()
{
    if (!m_instance)
        m_instance = new LanTransfer();
    return m_instance;
}

LanTransfer::LanTransfer()
    : QObject(nullptr)
{
    m_selfId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_deviceName = QSysInfo::machineHostName();
    if (m_deviceName.trimmed().isEmpty())
        m_deviceName = tr("未知设备");
}

LanTransfer::~LanTransfer()
{
    stopService();
    stopDiscovery();
}

// ───────────────────────────── 接收服务 ─────────────────────────────

void LanTransfer::startService()
{
    if (m_serviceRunning)
        return;

    if (!m_server)
    {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection,
                this, &LanTransfer::handleIncomingConnection);
    }

    // 固定端口：远程互传（内网穿透的端口转发）需要确定的目标端口；
    // 被占用时回退到随机端口，此时仍可局域网互传（发现协议会通告实际端口）
    if (!m_server->listen(QHostAddress::AnyIPv4, kServicePort)
        && !m_server->listen(QHostAddress::AnyIPv4, 0))
    {
        qWarning() << "[LanTransfer] listen failed:" << m_server->errorString();
        return;
    }

    m_serviceRunning = true;
    // 接收端必须能够应答发现广播，否则发送方搜不到本机
    ensureUdpSocket();
    emit receivingChanged(true);
}

void LanTransfer::stopService()
{
    if (!m_serviceRunning)
        return;

    m_serviceRunning = false;
    if (m_server)
        m_server->close();
    emit receivingChanged(false);
}

bool LanTransfer::isServiceRunning() const
{
    return m_serviceRunning;
}

quint16 LanTransfer::servicePort() const
{
    return (m_server && m_serviceRunning) ? m_server->serverPort() : 0;
}

QString LanTransfer::deviceName() const
{
    return m_deviceName;
}

QList<LanShareRoot> LanTransfer::shareRoots() const
{
    QList<LanShareRoot> roots;
    roots.append({tr("Java 版游戏目录"), Platform::getMinecraftDirectory()});
    roots.append({tr("启动器数据目录"), Platform::getDataDirectory()});
    roots.append({tr("基岩版数据目录"), Platform::getBedrockDataDirectory()});
    return roots;
}

bool LanTransfer::isPathAllowed(const QString &path) const
{
    const QString clean = normalizedPath(path);
    if (clean.isEmpty())
        return false;

    const QList<LanShareRoot> roots = shareRoots();
    for (const LanShareRoot &root : roots)
    {
        const QString base = normalizedPath(root.path);
        if (base.isEmpty())
            continue;
        if (pathEquals(clean, base))
            return true;
        if (pathStartsWith(clean, base + QLatin1Char('/')))
            return true;
    }
    return false;
}

// ───────────────────────────── 设备发现 ─────────────────────────────

void LanTransfer::ensureUdpSocket()
{
    if (m_udp)
        return;

    m_udp = new QUdpSocket(this);
    connect(m_udp, &QUdpSocket::readyRead, this, [this]() {
        while (m_udp && m_udp->hasPendingDatagrams())
        {
            const QNetworkDatagram dgram = m_udp->receiveDatagram();
            handleDatagram(dgram.data(), dgram.senderAddress().toString(),
                           dgram.senderPort());
        }
    });

    if (!m_udp->bind(QHostAddress::AnyIPv4, kDiscoveryPort,
                     QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
    {
        // 绑定失败时仍然可以发送探测广播，只是收不到他机应答
        qWarning() << "[LanTransfer] udp bind failed:" << m_udp->errorString();
    }

    if (!m_broadcastTimer)
    {
        m_broadcastTimer = new QTimer(this);
        m_broadcastTimer->setInterval(kBroadcastIntervalMs);
        connect(m_broadcastTimer, &QTimer::timeout, this, &LanTransfer::sendDiscoveryRequest);
    }
    if (!m_sweepTimer)
    {
        m_sweepTimer = new QTimer(this);
        m_sweepTimer->setInterval(kBroadcastIntervalMs);
        connect(m_sweepTimer, &QTimer::timeout, this, &LanTransfer::sweepExpiredDevices);
    }
}

void LanTransfer::startDiscovery()
{
    ensureUdpSocket();
    if (m_discovering)
    {
        // 已在搜索时（手动刷新）立刻再广播一次，不重置定时器
        sendDiscoveryRequest();
        return;
    }
    m_discovering = true;
    sendDiscoveryRequest();
    m_broadcastTimer->start();
    m_sweepTimer->start();
}

void LanTransfer::stopDiscovery()
{
    m_discovering = false;
    if (m_broadcastTimer)
        m_broadcastTimer->stop();
    if (m_sweepTimer)
        m_sweepTimer->stop();
    if (!m_devices.isEmpty())
    {
        m_devices.clear();
        emit devicesChanged();
    }
}

bool LanTransfer::isDiscovering() const
{
    return m_discovering;
}

QList<LanDevice> LanTransfer::devices() const
{
    QList<LanDevice> list = m_devices.values();
    std::sort(list.begin(), list.end(), [](const LanDevice &a, const LanDevice &b) {
        if (a.name != b.name)
            return a.name < b.name;
        return a.address < b.address;
    });
    return list;
}

LanDevice LanTransfer::device(const QString &id) const
{
    return m_devices.value(id);
}

void LanTransfer::sendDiscoveryRequest()
{
    if (!m_udp)
        return;
    const QByteArray payload = QByteArray(kReqTag) + "{}";
    m_udp->writeDatagram(payload, QHostAddress::Broadcast, kDiscoveryPort);
    m_udp->writeDatagram(payload, QHostAddress(QStringLiteral("255.255.255.255")),
                         kDiscoveryPort);
}

QByteArray LanTransfer::advertisementPayload() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), m_selfId);
    obj.insert(QStringLiteral("name"), m_deviceName);
    obj.insert(QStringLiteral("port"), static_cast<int>(servicePort()));

    QJsonArray roots;
    const QList<LanShareRoot> list = shareRoots();
    for (const LanShareRoot &root : list)
    {
        QJsonObject item;
        item.insert(QStringLiteral("name"), root.name);
        item.insert(QStringLiteral("path"), root.path);
        roots.append(item);
    }
    obj.insert(QStringLiteral("roots"), roots);

    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return QByteArray(kRspTag) + body;
}

void LanTransfer::handleDatagram(const QByteArray &data, const QString &senderAddr,
                                 quint16 senderPort)
{
    if (data.startsWith(QByteArray(kReqTag)))
    {
        // 只有接收服务运行时才通告自己，避免"开着软件却收不到文件"的错觉
        if (!m_serviceRunning || !m_udp)
            return;
        m_udp->writeDatagram(advertisementPayload(),
                             QHostAddress(senderAddr), senderPort);
        return;
    }

    if (!data.startsWith(QByteArray(kRspTag)))
        return;

    const QByteArray body = data.mid(static_cast<int>(qstrlen(kRspTag)));
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString id = obj.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id == m_selfId)
        return;

    LanDevice &dev = m_devices[id];
    const bool isNew = dev.id.isEmpty();
    dev.id = id;
    dev.name = obj.value(QStringLiteral("name")).toString();
    dev.address = senderAddr;
    dev.port = static_cast<quint16>(obj.value(QStringLiteral("port")).toInt());
    dev.lastSeen = QDateTime::currentMSecsSinceEpoch();
    dev.self = false;
    dev.roots.clear();
    const QJsonArray roots = obj.value(QStringLiteral("roots")).toArray();
    for (const QJsonValue &value : roots)
    {
        const QJsonObject item = value.toObject();
        LanShareRoot root;
        root.name = item.value(QStringLiteral("name")).toString();
        root.path = item.value(QStringLiteral("path")).toString();
        if (!root.path.isEmpty())
            dev.roots.append(root);
    }

    if (isNew)
        emit devicesChanged();
}

void LanTransfer::sweepExpiredDevices()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (auto it = m_devices.begin(); it != m_devices.end();)
    {
        if (now - it->lastSeen > kDeviceTimeoutMs)
        {
            it = m_devices.erase(it);
            changed = true;
        }
        else
        {
            ++it;
        }
    }
    if (changed)
        emit devicesChanged();
}

// ─────────────────── 接收端：单连接协议处理 ───────────────────

class LanTransfer::Connection : public QObject
{
public:
    Connection(LanTransfer *owner, QTcpSocket *socket)
        : QObject(owner)
        , m_owner(owner)
        , m_socket(socket)
    {
        m_socket->setParent(this);
        connect(m_socket, &QTcpSocket::readyRead, this, &Connection::onReadyRead);
        connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
            if (m_payloadRemaining > 0)
                failReceive(tr("连接中断"));
        });
    }

private:
    void sendLine(const QByteArray &line) { m_socket->write(line + '\n'); }

    void onReadyRead()
    {
        m_buf += m_socket->readAll();
        while (true)
        {
            if (m_payloadRemaining > 0)
            {
                if (m_buf.isEmpty())
                    break;
                const qsizetype take = qMin<qsizetype>(m_payloadRemaining, m_buf.size());
                if (m_file.isOpen())
                    m_file.write(m_buf.constData(), take);
                m_buf.remove(0, take);
                m_payloadRemaining -= take;
                m_received += take;
                m_speedWindowBytes += take;
                reportProgress();
                if (m_payloadRemaining == 0)
                    completePut();
                continue;
            }

            const qsizetype nl = m_buf.indexOf('\n');
            if (nl < 0)
                break;
            QByteArray line = m_buf.left(nl);
            m_buf.remove(0, nl + 1);
            if (line.endsWith('\r'))
                line.chop(1);
            handleLine(line);
        }
    }

    void handleLine(const QByteArray &line)
    {
        if (line == "ROOTS")
        {
            const QList<LanShareRoot> roots = m_owner->shareRoots();
            for (const LanShareRoot &root : roots)
            {
                QJsonObject item;
                item.insert(QStringLiteral("name"), root.name);
                item.insert(QStringLiteral("path"), root.path);
                sendLine(QJsonDocument(item).toJson(QJsonDocument::Compact));
            }
            sendLine("OK");
            return;
        }
        if (line.startsWith("LIST "))
        {
            sendList(QString::fromUtf8(line.mid(5)));
            return;
        }
        if (line.startsWith("MKDIR "))
        {
            doMkdir(QString::fromUtf8(line.mid(6)));
            return;
        }
        if (line.startsWith("PUT "))
        {
            beginPut(QString::fromUtf8(line.mid(4)));
            return;
        }
        if (line == "QUIT")
        {
            m_socket->disconnectFromHost();
            return;
        }
        sendLine("ERR unknown command");
    }

    void sendList(const QString &path)
    {
        if (!m_owner->isPathAllowed(path))
        {
            sendLine(QByteArray("ERR ") + tr("路径不在共享范围内").toUtf8());
            return;
        }
        QDir dir(path);
        if (!dir.exists())
        {
            sendLine(QByteArray("ERR ") + tr("目录不存在").toUtf8());
            return;
        }

        const QFileInfoList entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &info : entries)
        {
            if (info.isSymLink())
                continue;
            QJsonObject item;
            item.insert(QStringLiteral("name"), info.fileName());
            item.insert(QStringLiteral("dir"), info.isDir());
            item.insert(QStringLiteral("size"), info.isDir() ? QJsonValue(0)
                                                             : QJsonValue(double(info.size())));
            sendLine(QJsonDocument(item).toJson(QJsonDocument::Compact));
        }
        sendLine("OK");
    }

    void doMkdir(const QString &path)
    {
        if (!m_owner->isPathAllowed(path))
        {
            sendLine(QByteArray("ERR ") + tr("路径不在共享范围内").toUtf8());
            return;
        }
        if (QDir().mkpath(path))
            sendLine("OK");
        else
            sendLine(QByteArray("ERR ") + tr("创建目录失败").toUtf8());
    }

    void beginPut(const QString &args)
    {
        const int sp = args.lastIndexOf(QLatin1Char(' '));
        if (sp <= 0)
        {
            sendLine("ERR bad PUT command");
            return;
        }
        const QString path = args.left(sp);
        bool ok = false;
        const qint64 size = args.mid(sp + 1).trimmed().toLongLong(&ok);
        if (!ok || size < 0)
        {
            sendLine("ERR bad PUT size");
            return;
        }
        if (!m_owner->isPathAllowed(path))
        {
            sendLine(QByteArray("ERR ") + tr("路径不在共享范围内").toUtf8());
            return;
        }

        const QFileInfo fi(path);
        if (!QDir().mkpath(fi.absolutePath()))
        {
            sendLine(QByteArray("ERR ") + tr("无法创建目标目录").toUtf8());
            return;
        }
        if (fi.exists() && fi.isDir())
        {
            sendLine(QByteArray("ERR ") + tr("目标已是目录").toUtf8());
            return;
        }

        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            sendLine(QByteArray("ERR ") + tr("无法写入目标文件").toUtf8());
            return;
        }

        m_path = path;
        m_size = size;
        m_received = 0;
        m_payloadRemaining = size;
        m_speedStart = 0;
        m_speedWindowBytes = 0;
        m_lastReport = 0;
        m_taskId = DownloadTaskManager::instance()->addTask(
            tr("文件传输"), fi.absolutePath(), QString());
        DownloadTaskManager::instance()->updateTaskDisplayStatus(m_taskId, tr("传输中"));
        DownloadTaskManager::instance()->updateTaskStatus(
            m_taskId, DownloadTaskStatus::Downloading, tr("接收: %1").arg(fi.fileName()));
        DownloadTaskManager::instance()->updateTaskCurrentFile(m_taskId, fi.fileName());
        DownloadTaskManager::instance()->updateTaskProgressDouble(m_taskId, 0.0, 0, size, 0);

        if (size == 0)
            completePut();
    }

    void completePut()
    {
        m_file.close();
        const QString path = m_path;
        const qint64 size = m_size;
        m_payloadRemaining = 0;
        m_path.clear();
        m_size = 0;
        m_received = 0;

        sendLine("OK");

        DownloadTaskManager *mgr = DownloadTaskManager::instance();
        mgr->updateTaskProgressDouble(m_taskId, 100.0, size, size, 0);
        mgr->updateTaskDisplayStatus(m_taskId, tr("已完成"));
        mgr->updateTaskStatus(m_taskId, DownloadTaskStatus::Completed, tr("接收完成"));

        emit m_owner->fileReceived(path, size);
        m_taskId.clear();
    }

    void failReceive(const QString &message)
    {
        if (m_file.isOpen())
            m_file.close();
        m_payloadRemaining = 0;
        if (!m_taskId.isEmpty())
        {
            DownloadTaskManager *mgr = DownloadTaskManager::instance();
            mgr->updateTaskDisplayStatus(m_taskId, tr("失败"));
            mgr->updateTaskStatus(m_taskId, DownloadTaskStatus::Failed, message);
            m_taskId.clear();
        }
        m_path.clear();
        m_size = 0;
        m_received = 0;
    }

    void reportProgress()
    {
        if (m_taskId.isEmpty())
            return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_speedStart == 0)
        {
            m_speedStart = now;
            m_lastReport = now;
        }

        qint64 speed = 0;
        const qint64 elapsed = now - m_speedStart;
        if (elapsed >= 400 && m_speedWindowBytes > 0)
        {
            speed = m_speedWindowBytes * 1000 / elapsed;
            m_speedStart = now;
            m_speedWindowBytes = 0;
        }
        if (now - m_lastReport < 120)
            return;
        m_lastReport = now;

        const double pct = (m_size > 0) ? (double(m_received) * 100.0 / double(m_size)) : 100.0;
        DownloadTaskManager::instance()->updateTaskProgressDouble(
            m_taskId, qBound(0.0, pct, 100.0), m_received, m_size, speed);
    }

    LanTransfer *m_owner = nullptr;
    QTcpSocket *m_socket = nullptr;
    QByteArray m_buf;
    qint64 m_payloadRemaining = 0;
    QFile m_file;
    QString m_path;
    qint64 m_size = 0;
    qint64 m_received = 0;
    QString m_taskId;
    qint64 m_speedStart = 0;
    qint64 m_speedWindowBytes = 0;
    qint64 m_lastReport = 0;
};

void LanTransfer::handleIncomingConnection()
{
    while (m_server && m_server->hasPendingConnections())
    {
        QTcpSocket *sock = m_server->nextPendingConnection();
        auto *conn = new Connection(this, sock);
        connect(sock, &QTcpSocket::disconnected, conn, &QObject::deleteLater);
    }
}

// ───────────────────── 远端浏览（发送方资源管理器） ─────────────────────

void LanTransfer::browseRoots(const LanDevice &device)
{
    startBrowse(device, QByteArrayLiteral("ROOTS"));
}

void LanTransfer::browseDirectory(const LanDevice &device, const QString &remotePath)
{
    startBrowse(device, QByteArray("LIST ") + remotePath.toUtf8());
}

void LanTransfer::startBrowse(const LanDevice &device, const QByteArray &command)
{
    if (!device.isValid())
    {
        emit browseFailed(device.id, tr("目标设备不可用"));
        return;
    }

    if (m_browseSocket)
    {
        m_browseSocket->disconnect(this);
        m_browseSocket->abort();
        m_browseSocket->deleteLater();
        m_browseSocket = nullptr;
    }

    m_browseDeviceId = device.id;
    m_browseBuffer.clear();
    m_browseEntries.clear();
    m_browsePending = true;
    m_browseCommand = QString::fromUtf8(command);
    m_browsePath = command.startsWith("LIST ")
        ? QString::fromUtf8(command.mid(5))
        : QString();

    QTcpSocket *sock = new QTcpSocket(this);
    m_browseSocket = sock;

    connect(sock, &QTcpSocket::connected, this, [sock, command]() {
        sock->write(command + '\n');
    });

    connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
        if (sock != m_browseSocket)
            return;
        m_browseBuffer += sock->readAll();
        while (true)
        {
            const qsizetype nl = m_browseBuffer.indexOf('\n');
            if (nl < 0)
                break;
            QByteArray line = m_browseBuffer.left(nl);
            m_browseBuffer.remove(0, nl + 1);
            if (line.endsWith('\r'))
                line.chop(1);

            if (line == "OK")
            {
                m_browsePending = false;
                if (m_browseCommand == QLatin1String("ROOTS"))
                {
                    QList<LanShareRoot> roots;
                    for (const QByteArray &raw : m_browseEntries)
                    {
                        const QJsonObject obj = QJsonDocument::fromJson(raw).object();
                        LanShareRoot root;
                        root.name = obj.value(QStringLiteral("name")).toString();
                        root.path = obj.value(QStringLiteral("path")).toString();
                        if (!root.path.isEmpty())
                            roots.append(root);
                    }
                    emit rootsReceived(m_browseDeviceId, roots);
                }
                else
                {
                    QList<LanEntry> entries;
                    for (const QByteArray &raw : m_browseEntries)
                    {
                        const QJsonObject obj = QJsonDocument::fromJson(raw).object();
                        LanEntry entry;
                        entry.name = obj.value(QStringLiteral("name")).toString();
                        entry.isDir = obj.value(QStringLiteral("dir")).toBool();
                        entry.size = static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble());
                        if (!entry.name.isEmpty())
                            entries.append(entry);
                    }
                    emit directoryReceived(m_browseDeviceId, m_browsePath, entries);
                }
                m_browseEntries.clear();
                continue;
            }
            if (line.startsWith("ERR"))
            {
                m_browsePending = false;
                m_browseEntries.clear();
                emit browseFailed(m_browseDeviceId,
                                  QString::fromUtf8(line.mid(4)).trimmed());
                continue;
            }
            if (line.startsWith('{'))
                m_browseEntries.append(line);
        }
    });

    connect(sock, &QTcpSocket::errorOccurred, this,
            [this, sock](QAbstractSocket::SocketError) {
                if (sock != m_browseSocket || !m_browsePending)
                    return;
                m_browsePending = false;
                emit browseFailed(m_browseDeviceId, sock->errorString());
            });

    connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
        const bool wasPending = (sock == m_browseSocket) && m_browsePending;
        if (sock == m_browseSocket)
            m_browseSocket = nullptr;
        if (wasPending)
        {
            m_browsePending = false;
            emit browseFailed(m_browseDeviceId, tr("连接已断开"));
        }
        sock->deleteLater();
    });

    sock->connectToHost(device.address, device.port);
}

// ───────────────────────────── 发送 ─────────────────────────────

QString LanTransfer::sendFiles(const LanDevice &device, const QStringList &localPaths,
                               const QString &remoteDir)
{
    QStringList existing;
    for (const QString &path : localPaths)
    {
        if (QFileInfo::exists(path))
            existing << path;
    }
    if (existing.isEmpty())
        return QString();
    if (!device.isValid() || isSending())
        return QString();

    beginSend(device, existing, normalizedPath(remoteDir));
    return m_sendTaskId;
}

void LanTransfer::cancelSend()
{
    if (m_sendTaskId.isEmpty())
        return;
    m_sendCancelled = true;
    finishSend(false, tr("已取消"));
}

bool LanTransfer::isSending() const
{
    return !m_sendTaskId.isEmpty();
}

void LanTransfer::beginSend(const LanDevice &device, const QStringList &localPaths,
                            const QString &remoteDir)
{
    QList<SendAction> mkdirs;
    QList<SendAction> puts;
    qint64 total = 0;

    if (!remoteDir.isEmpty())
    {
        SendAction rootMkdir;
        rootMkdir.type = SendAction::Mkdir;
        rootMkdir.remotePath = remoteDir;
        mkdirs.append(rootMkdir);
    }
    for (const QString &path : localPaths)
        collectLocalTree(path, remoteDir, mkdirs, puts, total);

    if (puts.isEmpty() && mkdirs.isEmpty())
        return;

    m_sendDevice = device;
    m_sendRemoteDir = remoteDir;
    m_sendActions = mkdirs;
    m_sendActions.append(puts);
    m_sendIndex = 0;
    m_sendTotal = total;
    m_sendDone = 0;
    m_sendWritten = 0;
    m_sendFlushed = 0;
    m_sendCancelled = false;
    m_sendPayloadMode = false;
    m_sendAwaitingAck = false;
    m_sendBuffer.clear();
    m_sendSpeedWindowStart = 0;
    m_sendSpeedWindowBytes = 0;
    m_sendLastSpeed = 0;
    m_sendLastReport = 0;

    m_sendTaskId = DownloadTaskManager::instance()->addTask(tr("文件传输"), remoteDir,
                                                            QString());
    DownloadTaskManager::instance()->updateTaskDisplayStatus(m_sendTaskId, tr("传输中"));
    DownloadTaskManager::instance()->updateTaskStatus(
        m_sendTaskId, DownloadTaskStatus::Downloading,
        tr("发送到 %1").arg(device.name));
    DownloadTaskManager::instance()->updateTaskProgressDouble(m_sendTaskId, 0.0, 0, total, 0);
    reportSendProgress();

    m_sendSocket = new QTcpSocket(this);
    connect(m_sendSocket, &QTcpSocket::connected, this, [this]() {
        sendNextAction();
    });
    connect(m_sendSocket, &QTcpSocket::readyRead, this, &LanTransfer::onSendReadyRead);
    connect(m_sendSocket, &QTcpSocket::bytesWritten, this, &LanTransfer::onSendBytesWritten);
    connect(m_sendSocket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                onSendSocketError();
            });
    connect(m_sendSocket, &QTcpSocket::disconnected, this, [this]() {
        if (!m_sendTaskId.isEmpty())
            finishSend(false, tr("连接已断开"));
    });

    m_sendSocket->connectToHost(device.address, device.port);
}

void LanTransfer::collectLocalTree(const QString &localPath, const QString &remoteBase,
                                   QList<SendAction> &mkdirs, QList<SendAction> &puts,
                                   qint64 &totalBytes)
{
    const QFileInfo fi(localPath);
    if (!fi.exists())
        return;

    const QString remoteSelf = remoteBase.isEmpty()
        ? fi.fileName()
        : remoteBase + QLatin1Char('/') + fi.fileName();

    if (fi.isDir())
    {
        SendAction dirAction;
        dirAction.type = SendAction::Mkdir;
        dirAction.remotePath = remoteSelf;
        mkdirs.append(dirAction);

        QDirIterator it(localPath,
                        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        const QDir base(localPath);
        while (it.hasNext())
        {
            const QString child = it.next();
            const QFileInfo childInfo(child);
            const QString remoteChild = remoteSelf + QLatin1Char('/')
                + base.relativeFilePath(child);
            if (childInfo.isDir())
            {
                SendAction dirItem;
                dirItem.type = SendAction::Mkdir;
                dirItem.remotePath = normalizedPath(remoteChild);
                mkdirs.append(dirItem);
            }
            else if (childInfo.isFile())
            {
                SendAction fileItem;
                fileItem.type = SendAction::Put;
                fileItem.remotePath = normalizedPath(remoteChild);
                fileItem.localPath = child;
                fileItem.size = childInfo.size();
                puts.append(fileItem);
                totalBytes += fileItem.size;
            }
        }
        return;
    }

    SendAction fileItem;
    fileItem.type = SendAction::Put;
    fileItem.remotePath = remoteSelf;
    fileItem.localPath = fi.absoluteFilePath();
    fileItem.size = fi.size();
    puts.append(fileItem);
    totalBytes += fileItem.size;
}

void LanTransfer::sendNextAction()
{
    if (!m_sendSocket)
        return;
    if (m_sendIndex >= m_sendActions.size())
    {
        finishSend(true, tr("传输完成"));
        return;
    }

    const SendAction action = m_sendActions.at(m_sendIndex);
    if (action.type == SendAction::Mkdir)
    {
        m_sendPayloadMode = false;
        m_sendAwaitingAck = true;
        m_sendSocket->write(QByteArray("MKDIR ") + action.remotePath.toUtf8() + '\n');
        return;
    }

    m_sendFile = new QFile(action.localPath);
    if (!m_sendFile->open(QIODevice::ReadOnly))
    {
        const QString error = tr("无法读取文件: %1")
                                  .arg(QFileInfo(action.localPath).fileName());
        delete m_sendFile;
        m_sendFile = nullptr;
        finishSend(false, error);
        return;
    }

    const qint64 actualSize = m_sendFile->size();
    if (actualSize != action.size)
    {
        m_sendTotal += (actualSize - action.size);
        m_sendActions[m_sendIndex].size = actualSize;
    }

    m_sendWritten = 0;
    m_sendFlushed = 0;
    m_sendPayloadMode = true;
    m_sendAwaitingAck = true;

    const QString fileName = QFileInfo(action.localPath).fileName();
    DownloadTaskManager::instance()->updateTaskCurrentFile(m_sendTaskId, fileName);
    DownloadTaskManager::instance()->updateTaskStep(m_sendTaskId,
                                                    tr("正在发送 %1").arg(fileName));
    m_sendSocket->write(QByteArray("PUT ") + action.remotePath.toUtf8() + ' '
                        + QByteArray::number(actualSize) + '\n');
    writeSendChunk();
}

void LanTransfer::writeSendChunk()
{
    if (!m_sendSocket || !m_sendFile || !m_sendPayloadMode)
        return;
    if (m_sendIndex >= m_sendActions.size())
        return;

    // 背压：socket 内缓冲超过水位时等待 bytesWritten 回调，避免大文件撑爆内存
    if (m_sendWritten - m_sendFlushed >= kSendHighWater)
        return;

    const QByteArray chunk = m_sendFile->read(kChunkSize);
    if (chunk.isEmpty())
        return;

    m_sendSocket->write(chunk);
    m_sendWritten += chunk.size();
    m_sendSpeedWindowBytes += chunk.size();
    reportSendProgress();
}

void LanTransfer::onSendBytesWritten(qint64 bytes)
{
    m_sendFlushed += bytes;
    if (m_sendPayloadMode)
        writeSendChunk();
}

void LanTransfer::onSendReadyRead()
{
    if (!m_sendSocket)
        return;
    m_sendBuffer += m_sendSocket->readAll();

    while (true)
    {
        const qsizetype nl = m_sendBuffer.indexOf('\n');
        if (nl < 0)
            break;
        QByteArray line = m_sendBuffer.left(nl);
        m_sendBuffer.remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);

        if (line == "OK")
        {
            if (m_sendPayloadMode)
            {
                const SendAction action = m_sendActions.at(m_sendIndex);
                m_sendDone += action.size;
                m_sendWritten = 0;
                m_sendFlushed = 0;
                m_sendPayloadMode = false;
                if (m_sendFile)
                {
                    m_sendFile->close();
                    delete m_sendFile;
                    m_sendFile = nullptr;
                }
            }
            m_sendAwaitingAck = false;
            ++m_sendIndex;
            reportSendProgress();
            sendNextAction();
            if (!m_sendSocket)
                return;
            continue;
        }

        if (line.startsWith("ERR"))
        {
            const QString message = QString::fromUtf8(line.mid(4)).trimmed();
            finishSend(false, message.isEmpty() ? tr("对方拒绝了本次传输") : message);
            return;
        }
    }
}

void LanTransfer::onSendSocketError()
{
    if (m_sendTaskId.isEmpty())
        return;
    const QString error = m_sendSocket ? m_sendSocket->errorString() : tr("连接失败");
    finishSend(false, error.isEmpty() ? tr("连接失败") : error);
}

void LanTransfer::finishSend(bool success, const QString &message)
{
    const QString taskId = m_sendTaskId;
    const qint64 total = m_sendTotal;
    const bool cancelled = m_sendCancelled;
    if (taskId.isEmpty() && !m_sendSocket)
        return;

    if (m_sendFile)
    {
        m_sendFile->close();
        delete m_sendFile;
        m_sendFile = nullptr;
    }
    if (m_sendSocket)
    {
        disconnect(m_sendSocket, nullptr, this, nullptr);
        m_sendSocket->abort();
        m_sendSocket->deleteLater();
        m_sendSocket = nullptr;
    }

    m_sendTaskId.clear();
    m_sendActions.clear();
    m_sendIndex = 0;
    m_sendTotal = 0;
    m_sendDone = 0;
    m_sendWritten = 0;
    m_sendFlushed = 0;
    m_sendCancelled = false;
    m_sendPayloadMode = false;
    m_sendAwaitingAck = false;
    m_sendBuffer.clear();

    if (!taskId.isEmpty())
    {
        DownloadTaskManager *mgr = DownloadTaskManager::instance();
        if (success)
        {
            mgr->updateTaskProgressDouble(taskId, 100.0, total, total, 0);
            mgr->updateTaskDisplayStatus(taskId, tr("已完成"));
            mgr->updateTaskStatus(taskId, DownloadTaskStatus::Completed, tr("传输完成"));
        }
        else if (cancelled)
        {
            mgr->updateTaskDisplayStatus(taskId, tr("已取消"));
            mgr->updateTaskStatus(taskId, DownloadTaskStatus::Cancelled, tr("已取消"));
        }
        else
        {
            mgr->updateTaskDisplayStatus(taskId, tr("失败"));
            mgr->updateTaskStatus(taskId, DownloadTaskStatus::Failed, message);
        }
    }

    emit sendFinished(taskId, success, message);
}

void LanTransfer::reportSendProgress(qint64 speed)
{
    Q_UNUSED(speed);
    if (m_sendTaskId.isEmpty())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_sendSpeedWindowStart == 0)
    {
        m_sendSpeedWindowStart = now;
        m_sendLastReport = now;
    }

    const qint64 elapsed = now - m_sendSpeedWindowStart;
    if (elapsed >= 400 && m_sendSpeedWindowBytes > 0)
    {
        m_sendLastSpeed = m_sendSpeedWindowBytes * 1000 / elapsed;
        m_sendSpeedWindowStart = now;
        m_sendSpeedWindowBytes = 0;
    }
    if (now - m_sendLastReport < 120)
        return;
    m_sendLastReport = now;

    const qint64 sent = m_sendDone + m_sendWritten;
    const double percent = (m_sendTotal > 0)
        ? qBound(0.0, double(sent) * 100.0 / double(m_sendTotal), 100.0)
        : 0.0;
    DownloadTaskManager::instance()->updateTaskProgressDouble(
        m_sendTaskId, percent, sent, m_sendTotal, m_sendLastSpeed);
    emit sendProgress(m_sendTaskId, percent, sent, m_sendTotal, m_sendLastSpeed);
}

