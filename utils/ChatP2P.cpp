/**
 * @file   ChatP2P.cpp
 * @brief  聊天 P2P 直传：好友私聊文件的点对点传输实现
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "ChatP2P.h"

#include "utils/DownloadTaskManager.h"

#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <QtGlobal>

#include <algorithm>

namespace {

constexpr int kChunkSize = 64 * 1024;          ///< 单次读写块大小
constexpr qint64 kHighWater = 1024 * 1024;     ///< 发送端 socket 缓冲水位（背压）
constexpr int kConnectTimeoutMs = 8000;        ///< 接收端连单个地址的超时
constexpr int kHandshakeTimeoutMs = 10000;     ///< 握手应答超时（收发两侧同用）
constexpr int kStallTimeoutMs = 20000;         ///< 传输空闲超时（一段时间没有新数据）

/** 累计字节数换算窗口速度，窗口未满 400ms 时返回 -1 表示沿用上次速度 */
qint64 windowSpeed(qint64 &windowStart, qint64 &windowBytes)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (windowStart == 0)
    {
        windowStart = now;
        return -1;
    }
    const qint64 elapsed = now - windowStart;
    if (elapsed >= 400 && windowBytes > 0)
    {
        const qint64 speed = windowBytes * 1000 / elapsed;
        windowStart = now;
        windowBytes = 0;
        return speed;
    }
    return -1;
}

} // namespace

// ─────────────────────────── 邀约序列化 ───────────────────────────

QJsonObject ChatP2POffer::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("v"), 1);
    object.insert(QStringLiteral("tid"), tid);
    object.insert(QStringLiteral("act"), act.isEmpty() ? QStringLiteral("offer") : act);
    if (isOffer())
    {
        object.insert(QStringLiteral("token"), token);
        object.insert(QStringLiteral("name"), name);
        object.insert(QStringLiteral("size"), double(size));
        object.insert(QStringLiteral("port"), port);
        object.insert(QStringLiteral("exp"), double(exp));
        QJsonArray ipArray;
        for (const QString &ip : ips)
            ipArray.append(ip);
        object.insert(QStringLiteral("ips"), ipArray);
    }
    return object;
}

ChatP2POffer ChatP2POffer::fromJson(const QJsonObject &object)
{
    ChatP2POffer offer;
    offer.tid = object.value(QStringLiteral("tid")).toString();
    offer.act = object.value(QStringLiteral("act")).toString(QStringLiteral("offer"));
    if (offer.tid.isEmpty())
        return offer;
    // 不能用 isOffer() 当门槛：它要求 token/size 齐全，而字段还没解出来
    const bool offerLike = offer.act.isEmpty() || offer.act == QStringLiteral("offer");
    if (offerLike)
    {
        offer.token = object.value(QStringLiteral("token")).toString();
        offer.name = object.value(QStringLiteral("name")).toString();
        offer.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
        offer.port = static_cast<quint16>(object.value(QStringLiteral("port")).toInt());
        offer.exp = static_cast<qint64>(object.value(QStringLiteral("exp")).toDouble());
        const QJsonArray ipArray = object.value(QStringLiteral("ips")).toArray();
        for (const QJsonValue &value : ipArray)
        {
            const QString ip = value.toString();
            if (!ip.isEmpty())
                offer.ips.append(ip);
        }
    }
    return offer;
}

// ─────────────────────────── 单例与基础 ───────────────────────────

ChatP2P *ChatP2P::instance()
{
    static ChatP2P *s_instance = new ChatP2P();
    return s_instance;
}

ChatP2P::ChatP2P(QObject *parent)
    : QObject(parent)
{
    m_shareExpiryTimer = new QTimer(this);
    m_shareExpiryTimer->setSingleShot(true);
    connect(m_shareExpiryTimer, &QTimer::timeout, this, &ChatP2P::sweepExpiredShare);
}

ChatP2P::~ChatP2P()
{
    stopShare();
}

QStringList ChatP2P::localTransferAddresses()
{
    QStringList out;
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses)
    {
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback())
            continue;
        // 只通告站点内网地址：公网地址对面几乎连不进来（没做端口映射），反而拖慢重试
        const bool siteLocal = address.isInSubnet(QHostAddress::parseSubnet(QStringLiteral("10.0.0.0/8")))
            || address.isInSubnet(QHostAddress::parseSubnet(QStringLiteral("172.16.0.0/12")))
            || address.isInSubnet(QHostAddress::parseSubnet(QStringLiteral("192.168.0.0/16")));
        if (!siteLocal)
            continue;
        const QString text = address.toString();
        if (!out.contains(text))
            out.append(text);
    }
    return out;
}

// ─────────────────────────── 发送侧 ───────────────────────────

ChatP2POffer ChatP2P::startShare(const QString &filePath)
{
    if (!m_shareTid.isEmpty())
        return ChatP2POffer(); // 同一时刻只共享一份文件，页面上游会先提示

    QFile probe(filePath);
    if (!probe.open(QIODevice::ReadOnly))
        return ChatP2POffer();
    const qint64 size = probe.size();
    probe.close();
    if (size <= 0)
        return ChatP2POffer();

    if (!m_server)
    {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection,
                this, &ChatP2P::handleIncomingConnection);
    }
    // 固定端口优先（便于对方防火墙放行），被占用时回退随机端口（端口随邀约通告）
    if (!m_server->listen(QHostAddress::AnyIPv4, kServicePort)
        && !m_server->listen(QHostAddress::AnyIPv4, 0))
    {
        qWarning() << "[ChatP2P] 监听失败：" << m_server->errorString();
        return ChatP2POffer();
    }

    m_shareTid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_shareToken = QString::fromUtf8(QUuid::createUuid().toRfc4122().toHex());
    m_sharePath = filePath;
    m_shareSize = size;
    m_shareExpireAt = QDateTime::currentMSecsSinceEpoch() + kOfferLifetimeMs;
    m_shareCompleted = false;
    m_shareSentBytes = 0;
    m_shareSpeedWindowStart = 0;
    m_shareSpeedWindowBytes = 0;
    m_shareLastSpeed = 0;
    m_shareLastReport = 0;

    // 直传进了任务栏卡片：离开聊天页也能看到进度。首次监听会触发系统防火墙
    // 询问，放行后对方才能连进来
    m_shareTaskId = DownloadTaskManager::instance()->addTaskWithType(
        tr("聊天文件直传"), QFileInfo(filePath).fileName(), QString(), QStringList(),
        TaskType::ChatP2P);
    DownloadTaskManager::instance()->updateTaskDisplayStatus(m_shareTaskId, tr("等待对方接收"));
    DownloadTaskManager::instance()->updateTaskStatus(
        m_shareTaskId, DownloadTaskStatus::Downloading, tr("等待对方接收（P2P 直传）"));
    DownloadTaskManager::instance()->updateTaskProgressDouble(m_shareTaskId, 0.0, 0, size, 0);

    ChatP2POffer offer;
    offer.tid = m_shareTid;
    offer.token = m_shareToken;
    offer.name = QFileInfo(filePath).fileName();
    offer.size = size;
    offer.port = static_cast<quint16>(m_server->serverPort());
    offer.ips = localTransferAddresses();
    offer.exp = m_shareExpireAt;

    m_shareExpiryTimer->start(kOfferLifetimeMs);
    emit shareStarted(m_shareTid);
    return offer;
}

void ChatP2P::stopShare()
{
    if (m_shareTid.isEmpty())
        return;
    finishShare(false);
}

void ChatP2P::finishShare(bool expired)
{
    const QString tid = m_shareTid;
    const bool completed = m_shareCompleted;
    const QString taskId = m_shareTaskId;
    if (tid.isEmpty())
        return;

    if (m_shareExpiryTimer)
        m_shareExpiryTimer->stop();
    if (m_server)
        m_server->close();
    closeShareConnections();

    m_shareTid.clear();
    m_shareToken.clear();
    m_sharePath.clear();
    m_shareTaskId.clear();
    m_shareSize = 0;
    m_shareExpireAt = 0;
    m_shareCompleted = false;
    m_shareSentBytes = 0;

    // 已完整发完的共享到点自然结束，不再惊动界面；没发完的按取消 / 过期收尾
    if (completed)
        return;
    if (!taskId.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskDisplayStatus(
            taskId, expired ? tr("已过期") : tr("已取消"));
        DownloadTaskManager::instance()->updateTaskStatus(
            taskId, expired ? DownloadTaskStatus::Failed : DownloadTaskStatus::Cancelled,
            expired ? tr("邀约已过期，对方没有接收") : tr("直传已取消"));
    }
    emit shareFinished(tid, false, expired ? tr("邀约已过期，对方没有接收") : tr("直传已取消"));
}

void ChatP2P::sweepExpiredShare()
{
    if (!m_shareTid.isEmpty())
        finishShare(true);
}

bool ChatP2P::validateShareToken(const QString &token) const
{
    return !m_shareTid.isEmpty() && !m_shareToken.isEmpty() && token == m_shareToken;
}

void ChatP2P::reportShareProgress(qint64 bytes)
{
    if (m_shareTid.isEmpty() || m_shareSize <= 0)
        return;
    // 多个连接同时拉同一份文件时以最靠前的为准（实际场景几乎只有一个连接）
    const qint64 sent = qBound<qint64>(0, bytes, m_shareSize);
    if (sent < m_shareSentBytes)
        return;
    m_shareSpeedWindowBytes += sent - m_shareSentBytes;
    m_shareSentBytes = sent;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_shareSpeedWindowStart == 0)
        m_shareSpeedWindowStart = now;
    const qint64 elapsed = now - m_shareSpeedWindowStart;
    if (elapsed >= 400 && m_shareSpeedWindowBytes > 0)
    {
        m_shareLastSpeed = m_shareSpeedWindowBytes * 1000 / elapsed;
        m_shareSpeedWindowStart = now;
        m_shareSpeedWindowBytes = 0;
    }
    if (now - m_shareLastReport < 120)
        return;
    m_shareLastReport = now;

    const double percent = qBound(0.0, double(m_shareSentBytes) * 100.0 / double(m_shareSize), 100.0);
    if (!m_shareTaskId.isEmpty())
        DownloadTaskManager::instance()->updateTaskProgressDouble(
            m_shareTaskId, percent, m_shareSentBytes, m_shareSize, m_shareLastSpeed);
    emit shareProgress(m_shareTid, percent, m_shareSentBytes, m_shareSize, m_shareLastSpeed);
}

void ChatP2P::reportShareComplete()
{
    if (m_shareTid.isEmpty() || m_shareCompleted)
        return;
    m_shareCompleted = true;
    if (m_shareSize > 0)
    {
        if (!m_shareTaskId.isEmpty())
        {
            DownloadTaskManager::instance()->updateTaskProgressDouble(
                m_shareTaskId, 100.0, m_shareSize, m_shareSize, 0);
            DownloadTaskManager::instance()->updateTaskDisplayStatus(m_shareTaskId, tr("已完成"));
            DownloadTaskManager::instance()->updateTaskStatus(
                m_shareTaskId, DownloadTaskStatus::Completed, tr("对方已接收"));
        }
        emit shareProgress(m_shareTid, 100.0, m_shareSize, m_shareSize, m_shareLastSpeed);
    }
    emit shareFinished(m_shareTid, true, tr("对方已接收"));
}

void ChatP2P::onShareConnectionClosed(ShareConnection *connection)
{
    m_shareConns.removeAll(connection);
}

// ─────────────────────────── 接收侧 ───────────────────────────

void ChatP2P::reportReceiveProgress(const QString &requestId, qint64 received, qint64 total,
                                    qint64 speed)
{
    if (total <= 0)
        return;
    const double percent = qBound(0.0, double(received) * 100.0 / double(total), 100.0);
    emit receiveProgress(requestId, percent, received, total, speed);
}

void ChatP2P::reportReceiveFinished(const QString &requestId, bool success,
                                    const QString &savePath, const QString &message)
{
    if (m_receives.remove(requestId) == 0)
        return;
    emit receiveFinished(requestId, success, savePath, message);
}

// ─────────────────── 发送侧：一个来拉文件的连接 ───────────────────

class ChatP2P::ShareConnection : public QObject
{
public:
    ShareConnection(ChatP2P *owner, QTcpSocket *socket)
        : QObject(owner)
        , m_owner(owner)
        , m_socket(socket)
    {
        m_socket->setParent(this);
        connect(m_socket, &QTcpSocket::readyRead, this, &ShareConnection::onReadyRead);
        connect(m_socket, &QTcpSocket::bytesWritten, this, &ShareConnection::onBytesWritten);
        connect(m_socket, &QTcpSocket::disconnected, this, &ShareConnection::close);
        connect(m_socket, &QTcpSocket::errorOccurred, this, &ShareConnection::close);
        // 迟迟不出示口令的连接直接掐掉
        QTimer::singleShot(kHandshakeTimeoutMs, this, [this]() {
            if (m_phase == Phase::AwaitHandshake)
                close();
        });
    }

    void close()
    {
        if (m_closed)
            return;
        m_closed = true;
        if (m_socket)
            m_socket->abort();
        if (m_file)
        {
            m_file->close();
            delete m_file;
            m_file = nullptr;
        }
        m_owner->onShareConnectionClosed(this);
        deleteLater();
    }

private:
    enum class Phase
    {
        AwaitHandshake,
        Sending,
    };

    void onReadyRead()
    {
        if (m_phase == Phase::Sending)
        {
            m_socket->readAll(); // 传输期不再需要对方说话
            return;
        }
        m_buffer += m_socket->readAll();
        const qsizetype nl = m_buffer.indexOf('\n');
        if (nl < 0)
        {
            if (m_buffer.size() > 4096)
                close();
            return;
        }
        QByteArray line = m_buffer.left(nl);
        m_buffer.clear();
        if (line.endsWith('\r'))
            line.chop(1);
        handleHandshake(QString::fromUtf8(line));
    }

    void handleHandshake(const QString &line)
    {
        // 期望：BBX_CHAT v1 <token>
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() != 3 || parts.first() != QStringLiteral("BBX_CHAT")
            || parts.at(1) != QStringLiteral("v1"))
        {
            reject(tr("协议不支持"));
            return;
        }
        if (!m_owner->validateShareToken(parts.last()))
        {
            reject(tr("直传邀约无效或已过期"));
            return;
        }
        m_file = new QFile(m_owner->shareFilePath());
        if (!m_file->open(QIODevice::ReadOnly))
        {
            reject(tr("对方读不了这个文件"));
            return;
        }
        const qint64 size = m_file->size();
        if (size != m_owner->m_shareSize)
        {
            // 邀约之后文件又被改动：size 对不上就拒收，避免两边进度错乱
            reject(tr("文件内容有变化，请重新发送"));
            return;
        }
        m_phase = Phase::Sending;
        m_socket->write(QByteArray("BBX_OK ") + QByteArray::number(size) + '\n');
        writeNextChunk();
    }

    void reject(const QString &message)
    {
        m_socket->write(QByteArray("BBX_ERR ") + message.toUtf8() + '\n');
        // 给对方一点时间读到应答再断开
        QTimer::singleShot(500, this, &ShareConnection::close);
    }

    void onBytesWritten()
    {
        if (m_phase != Phase::Sending)
            return;
        if (m_written >= m_owner->m_shareSize && m_socket->bytesToWrite() == 0)
        {
            // 整份文件已经交给对方
            m_owner->reportShareComplete();
            m_socket->disconnectFromHost();
            return;
        }
        writeNextChunk();
    }

    void writeNextChunk()
    {
        if (m_phase != Phase::Sending || !m_file)
            return;
        // 背压：socket 缓冲超过水位就等 bytesWritten 回调再继续
        if (m_socket->bytesToWrite() >= kHighWater)
            return;
        const QByteArray chunk = m_file->read(kChunkSize);
        if (chunk.isEmpty())
            return; // 文件读完，剩下的等刷出后由 onBytesWritten 收尾
        m_socket->write(chunk);
        m_written += chunk.size();
        m_owner->reportShareProgress(m_written);
    }

    ChatP2P *m_owner = nullptr;
    QTcpSocket *m_socket = nullptr;
    QFile *m_file = nullptr;
    QByteArray m_buffer;
    Phase m_phase = Phase::AwaitHandshake;
    qint64 m_written = 0;
    bool m_closed = false;
};

void ChatP2P::handleIncomingConnection()
{
    while (m_server && m_server->hasPendingConnections())
    {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            break;
        if (m_shareTid.isEmpty())
        {
            socket->write(QByteArray("BBX_ERR ") + tr("没有在共享的文件").toUtf8() + '\n');
            socket->flush();
            // 走 disconnectFromHost 而不是 abort：给错误应答留出送达的时间
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            socket->disconnectFromHost();
            continue;
        }
        m_shareConns.append(new ShareConnection(this, socket));
    }
}

// ─────────────────── 接收侧：一次主动拉取 ───────────────────

class ChatP2P::ReceiveTransfer : public QObject
{
public:
    ReceiveTransfer(ChatP2P *owner, const ChatP2POffer &offer, const QString &savePath)
        : QObject(owner)
        , m_owner(owner)
        , m_offer(offer)
        , m_savePath(savePath)
    {
        m_socket = new QTcpSocket(this);
        connect(m_socket, &QTcpSocket::connected, this, &ReceiveTransfer::onConnected);
        connect(m_socket, &QTcpSocket::readyRead, this, &ReceiveTransfer::onReadyRead);
        connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            if (m_phase == Phase::Connecting)
                tryNextAddress(); // 换下一个地址接着试
            else if (m_phase != Phase::Done)
                fail(tr("连接中断"));
        });
        connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
            if (m_phase == Phase::Payload)
                fail(tr("连接中断"));
        });

        m_stallTimer = new QTimer(this);
        m_stallTimer->setSingleShot(true);
        connect(m_stallTimer, &QTimer::timeout, this, [this]() {
            if (m_phase != Phase::Done)
                fail(tr("传输超时，对方可能已下线"));
        });
    }

    void start()
    {
        if (m_offer.isExpired())
        {
            fail(tr("直传邀约已过期，请让对方重新发送"));
            return;
        }
        if (m_offer.ips.isEmpty())
        {
            fail(tr("对方没有给出可直连的地址"));
            return;
        }
        tryNextAddress();
    }

private:
    enum class Phase
    {
        Connecting,
        AwaitReply,
        Payload,
        Done,
    };

    void tryNextAddress()
    {
        if (m_ipIndex >= m_offer.ips.size())
        {
            fail(tr("连不上对方，对方可能不在线或不在同一网络"));
            return;
        }
        m_phase = Phase::Connecting;
        m_socket->abort();
        m_socket->connectToHost(m_offer.ips.at(m_ipIndex), m_offer.port);
        ++m_ipIndex;
        const int attempt = m_ipIndex;
        QTimer::singleShot(kConnectTimeoutMs, this, [this, attempt]() {
            if (m_phase == Phase::Connecting && attempt == m_ipIndex)
                tryNextAddress();
        });
    }

    void onConnected()
    {
        m_phase = Phase::AwaitReply;
        m_socket->write(QByteArray("BBX_CHAT v1 ") + m_offer.token.toUtf8() + '\n');
        m_stallTimer->start(kHandshakeTimeoutMs);
    }

    void onReadyRead()
    {
        m_stallTimer->start(kStallTimeoutMs);
        m_buffer += m_socket->readAll();

        if (m_phase == Phase::AwaitReply)
        {
            const qsizetype nl = m_buffer.indexOf('\n');
            if (nl < 0)
                return;
            QByteArray line = m_buffer.left(nl);
            m_buffer.remove(0, nl + 1);
            if (line.endsWith('\r'))
                line.chop(1);
            handleReply(line);
            if (m_phase != Phase::Payload)
                return;
        }

        while (!m_buffer.isEmpty() && m_phase == Phase::Payload)
        {
            const qint64 remaining = m_offer.size - m_received;
            const qint64 take = qMin<qint64>(remaining, m_buffer.size());
            if (take <= 0)
            {
                fail(tr("传输数据异常"));
                return;
            }
            if (m_file)
                m_file->write(m_buffer.constData(), take);
            m_buffer.remove(0, take);
            m_received += take;
            const qint64 speed = windowSpeed(m_speedWindowStart, m_speedWindowBytes);
            if (speed >= 0)
                m_lastSpeed = speed;
            m_owner->reportReceiveProgress(m_offer.tid, m_received, m_offer.size, m_lastSpeed);
            if (m_received == m_offer.size)
            {
                finish();
                return;
            }
        }
    }

    void handleReply(const QByteArray &line)
    {
        if (line.startsWith("BBX_OK "))
        {
            const qint64 size = line.mid(7).trimmed().toLongLong();
            if (size != m_offer.size || size <= 0)
            {
                fail(tr("文件大小对不上，请让对方重新发送"));
                return;
            }
            m_file = new QFile(m_savePath);
            if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                fail(tr("保存文件失败：没法写入这个位置"));
                return;
            }
            m_phase = Phase::Payload;
            return;
        }
        if (line.startsWith("BBX_ERR "))
        {
            const QString message = QString::fromUtf8(line.mid(8)).trimmed();
            fail(message.isEmpty() ? tr("对方拒绝了本次直传") : message);
            return;
        }
        fail(tr("协议不支持"));
    }

    void finish()
    {
        m_phase = Phase::Done;
        if (m_file)
        {
            m_file->close();
            delete m_file;
            m_file = nullptr;
        }
        m_socket->abort();
        m_stallTimer->stop();
        m_owner->reportReceiveFinished(m_offer.tid, true, m_savePath, QString());
        deleteLater();
    }

    void fail(const QString &message)
    {
        if (m_phase == Phase::Done)
            return;
        m_phase = Phase::Done;
        m_stallTimer->stop();
        m_socket->abort();
        if (m_file)
        {
            m_file->close();
            delete m_file;
            m_file = nullptr;
            // 收了一半的文件删掉，避免留下打不开的半成品
            QFile::remove(m_savePath);
        }
        m_owner->reportReceiveFinished(m_offer.tid, false, m_savePath, message);
        deleteLater();
    }

    ChatP2P *m_owner = nullptr;
    ChatP2POffer m_offer;
    QString m_savePath;
    QTcpSocket *m_socket = nullptr;
    QTimer *m_stallTimer = nullptr;
    QFile *m_file = nullptr;
    QByteArray m_buffer;
    Phase m_phase = Phase::Connecting;
    int m_ipIndex = 0;
    qint64 m_received = 0;
    qint64 m_speedWindowStart = 0;
    qint64 m_speedWindowBytes = 0;
    qint64 m_lastSpeed = 0;
};

// ─────────────────── 依赖完整类型定义的收发入口 ───────────────────

void ChatP2P::closeShareConnections()
{
    // 先摘出列表再关，close() 里会回来把自己从列表移除
    const QList<ShareConnection *> connections = m_shareConns;
    m_shareConns.clear();
    for (ShareConnection *connection : connections)
        connection->close();
}

QString ChatP2P::requestFile(const ChatP2POffer &offer, const QString &savePath)
{
    if (!offer.isOffer() || !offer.hasTransferInfo() || offer.isExpired())
        return QString();
    if (m_receives.contains(offer.tid))
        return offer.tid; // 同一邀约已在接收中，不要重复发起

    auto *transfer = new ReceiveTransfer(this, offer, savePath);
    m_receives.insert(offer.tid, transfer);
    transfer->start();
    return offer.tid;
}
