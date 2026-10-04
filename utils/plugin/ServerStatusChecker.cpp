/**
 * @file   ServerStatusChecker.cpp
 * @brief  Minecraft 服务器状态并发检测器实现
 *
 * 协议：Minecraft 1.7+ Server List Ping（SLP）
 *   1. TCP 连接 host:port
 *   2. 发送握手包：varint(len) + [0x00] + varint(47) + varint(hostLen) + host
 *      + uint16_be(port) + varint(2)
 *   3. 发送状态请求：varint(1) + varint(1)（即 0x01 0x01）
 *   4. 读回：varint(packetLen) + varint(packetId) + varint(jsonLen) + JSON
 *   5. JSON 中提取 players.online / players.max
 */
#include "utils/plugin/ServerStatusChecker.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThreadPool>
#include <QTcpSocket>

// ============================================================================
//  内部：单个服务器的 SLP ping 任务
// ============================================================================
class ServerPingTask : public QRunnable
{
public:
    ServerPingTask(ServerStatusChecker::Target target, int timeoutMs, ServerStatusChecker *checker)
        : m_target(target), m_timeoutMs(timeoutMs), m_checker(checker)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (m_checker->isCanceled()) {
            notify(false, -1, -1, -1);
            return;
        }

        int pingMs = -1, online = -1, max = -1;
        bool ok = false;

        QTcpSocket sock;
        QElapsedTimer timer;
        timer.start();

        sock.connectToHost(m_target.host, m_target.port);
        if (sock.waitForConnected(m_timeoutMs)) {
            sock.write(buildStatusPacket(m_target.host, m_target.port));
            if (sock.waitForBytesWritten(m_timeoutMs) && sock.waitForReadyRead(m_timeoutMs)) {
                QByteArray data = sock.readAll();
                // 响应可能分片，短等待补齐
                int extraWaits = 0;
                while (extraWaits < 4 && !isJsonComplete(data)) {
                    if (!sock.waitForReadyRead(150))
                        break;
                    data += sock.readAll();
                    ++extraWaits;
                }
                parsePlayers(data, online, max);
                pingMs = static_cast<int>(timer.elapsed());
                ok = true;
            }
        }
        sock.abort();
        notify(ok, pingMs, online, max);
    }

private:
    void notify(bool ok, int pingMs, int online, int max)
    {
        emit m_checker->statusReady(m_target.row, ok, pingMs, online, max);
        if (m_checker->decrementAndGet() <= 0)
            emit m_checker->allDone();
    }

    /** 构建 1.7+ SLP 握手 + 状态请求包 */
    static QByteArray buildStatusPacket(const QString &host, quint16 port)
    {
        QByteArray payload;
        payload.append(char(0x00));                       // handshake packet id
        payload.append(encodeVarInt(47));                 // protocol version
        QByteArray hb = host.toUtf8();
        payload.append(encodeVarInt(hb.size()));
        payload.append(hb);
        payload.append(char((port >> 8) & 0xFF));
        payload.append(char(port & 0xFF));
        payload.append(char(0x02));                       // next state: status

        QByteArray pkt;
        pkt.append(encodeVarInt(payload.size()));
        pkt.append(payload);
        pkt.append(char(0x01));                           // status request: len=1
        pkt.append(char(0x01));                           // status request: id=1
        return pkt;
    }

    static QByteArray encodeVarInt(qint32 value)
    {
        QByteArray out;
        quint32 v = static_cast<quint32>(value);
        do {
            quint8 b = v & 0x7F;
            v >>= 7;
            if (v != 0)
                b |= 0x80;
            out.append(char(b));
        } while (v != 0);
        return out;
    }

    /** 粗略判断是否已收到完整的 SLP 响应（能解析出完整 JSON 长度） */
    static bool isJsonComplete(const QByteArray &data)
    {
        int pos = 0;
        qint32 plen = 0, id = 0, jlen = 0;
        if (!readVarInt(data, pos, plen) || plen <= 0)
            return false;
        if (!readVarInt(data, pos, id))
            return false;
        if (!readVarInt(data, pos, jlen) || jlen <= 0 || jlen > 262144)
            return false;
        return data.size() - pos >= jlen;
    }

    static bool readVarInt(const QByteArray &data, int &pos, qint32 &out)
    {
        qint64 value = 0;
        int shift = 0;
        while (pos < data.size()) {
            quint8 b = static_cast<quint8>(data.at(pos++));
            value |= (static_cast<qint64>(b & 0x7F)) << shift;
            shift += 7;
            if (!(b & 0x80)) {
                out = static_cast<qint32>(value);
                return true;
            }
            if (shift > 35)
                return false;
        }
        return false;
    }

    /** 从状态 JSON 中提取 players.online / players.max */
    static void parsePlayers(const QByteArray &data, int &online, int &max)
    {
        int pos = 0;
        qint32 plen = 0, id = 0, jlen = 0;
        if (!readVarInt(data, pos, plen) || plen <= 0)
            return;
        if (!readVarInt(data, pos, id))
            return;
        if (!readVarInt(data, pos, jlen) || jlen <= 0 || jlen > 262144)
            return;
        if (data.size() - pos < jlen)
            return;

        const QByteArray json = data.mid(pos, jlen);
        QJsonDocument doc = QJsonDocument::fromJson(json);
        if (!doc.isObject())
            return;
        QJsonObject obj = doc.object();
        if (obj.contains(QLatin1String("players")) && obj.value(QLatin1String("players")).isObject()) {
            QJsonObject players = obj.value(QLatin1String("players")).toObject();
            online = players.value(QLatin1String("online")).toInt(-1);
            max = players.value(QLatin1String("max")).toInt(-1);
        }
    }

    ServerStatusChecker::Target m_target;
    int m_timeoutMs;
    ServerStatusChecker *m_checker;
};

// ============================================================================
//  ServerStatusChecker
// ============================================================================
ServerStatusChecker::ServerStatusChecker(QObject *parent)
    : QObject(parent)
{
    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(24);
}

ServerStatusChecker::~ServerStatusChecker()
{
    cancel();
    m_pool->waitForDone(5000);
}

void ServerStatusChecker::checkAll(const QList<Target> &targets, int timeoutMs, int maxConcurrent)
{
    // 取消上一轮
    m_canceled.storeRelaxed(1);
    m_pool->clear();
    m_pool->waitForDone(300);

    m_targets = targets;
    m_timeoutMs = timeoutMs;
    if (maxConcurrent > 0)
        m_pool->setMaxThreadCount(maxConcurrent);

    m_canceled.storeRelaxed(0);
    m_remaining.storeRelaxed(targets.size());

    for (const Target &t : m_targets)
        m_pool->start(new ServerPingTask(t, m_timeoutMs, this));
}

void ServerStatusChecker::cancel()
{
    m_canceled.storeRelaxed(1);
    m_pool->clear();
}
