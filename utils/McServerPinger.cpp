#include "McServerPinger.h"

#include <QDateTime>
#include <QDnsLookup>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTcpSocket>
#include <QTimer>

namespace
{

/// 连接/响应阶段各自的超时（给跨洋服务器留足余量，避免误判离线）
const int kTimeoutMs = 6000;
/// SRV 查询超时
const int kSrvTimeoutMs = 2500;
/// 单个响应包上限，防止畸形服务端把内存打满
const int kMaxPacketBytes = 4 * 1024 * 1024;
/// 状态查询用的协议号（1.20.1）；服务端只回状态，并不校验具体版本
const int kProtocolVersion = 763;

void writeVarInt(QByteArray &out, int value)
{
    quint32 v = static_cast<quint32>(value);
    for (int i = 0; i < 5; ++i)
    {
        if ((v & ~0x7Fu) == 0)
        {
            out.append(static_cast<char>(v));
            return;
        }
        out.append(static_cast<char>((v & 0x7Fu) | 0x80u));
        v >>= 7;
    }
}

bool readVarInt(const QByteArray &data, int &pos, int &value)
{
    value = 0;
    int shift = 0;
    for (int i = 0; i < 5; ++i)
    {
        if (pos >= data.size())
            return false;
        const quint8 byte = static_cast<quint8>(data.at(pos++));
        value |= static_cast<int>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0)
            return true;
        shift += 7;
    }
    return false;
}

void writeString(QByteArray &out, const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    writeVarInt(out, utf8.size());
    out.append(utf8);
}

bool readString(const QByteArray &data, int &pos, QString &out)
{
    int len = 0;
    if (!readVarInt(data, pos, len))
        return false;
    if (len < 0 || len > kMaxPacketBytes || pos + len > data.size())
        return false;
    out = QString::fromUtf8(data.constData() + pos, len);
    pos += len;
    return true;
}

/** 聊天组件（字符串 / {text,extra} / 数组）压平成纯文本 */
void flattenChat(const QJsonValue &value, QString &out)
{
    if (value.isString())
    {
        out += value.toString();
        return;
    }
    if (value.isArray())
    {
        const QJsonArray arr = value.toArray();
        for (const QJsonValue &item : arr)
            flattenChat(item, out);
        return;
    }
    if (!value.isObject())
        return;

    const QJsonObject obj = value.toObject();
    if (obj.contains(QStringLiteral("text")))
        out += obj.value(QStringLiteral("text")).toString();
    else if (obj.contains(QStringLiteral("translate")))
        out += obj.value(QStringLiteral("translate")).toString();
    if (obj.contains(QStringLiteral("extra")))
        flattenChat(obj.value(QStringLiteral("extra")), out);
}

/** 去掉 §a §l 之类的颜色/样式代码；换行统一成空格，卡片只有一行位置 */
QString stripFormatting(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i)
    {
        const QChar ch = text.at(i);
        if (ch == QChar(0x00A7))
        {
            ++i;   // 跳过这个代码后面的一个字符
            continue;
        }
        out += (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) ? QLatin1Char(' ') : ch;
    }
    return out.simplified();
}

/** data:image/png;base64,... → 图标 */
QPixmap decodeFavicon(const QString &favicon)
{
    if (favicon.isEmpty() || !favicon.startsWith(QStringLiteral("data:image")))
        return QPixmap();
    const int comma = favicon.indexOf(QLatin1Char(','));
    if (comma <= 0 || comma + 1 >= favicon.size())
        return QPixmap();
    const QByteArray payload = QByteArray::fromBase64(favicon.mid(comma + 1).toLatin1());
    if (payload.isEmpty())
        return QPixmap();
    QPixmap pixmap;
    if (!pixmap.loadFromData(payload))
        return QPixmap();
    return pixmap;
}

}   // namespace

/**
 * 一次探测的会话状态。故意不继承 QObject：信号全部由 McServerPinger 的
 * lambda 捕获 key 后回调，避免每个会话都要 moc。
 */
struct McServerPinger::Session
{
    Request request;
    QTcpSocket *socket = nullptr;
    QTimer *timer = nullptr;
    QDnsLookup *dns = nullptr;
    QByteArray buffer;
    qint64 connectedMs = 0;    ///< 连接建立时刻，用于算延迟和区分超时阶段
    bool srvTried = false;
    bool switching = false;    ///< SRV 切换中，忽略旧连接的 error 回调
    bool finished = false;
};

McServerPinger::McServerPinger(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<McServerPinger::Result>("McServerPinger::Result");
}

McServerPinger::~McServerPinger()
{
    cancelAll();
}

void McServerPinger::ping(const QString &key, const QString &host, quint16 port)
{
    QString address = host.trimmed();
    if (address.startsWith(QLatin1Char('[')) && address.endsWith(QLatin1Char(']')))
        address = address.mid(1, address.size() - 2);
    if (key.isEmpty() || address.isEmpty())
        return;
    if (m_knownKeys.contains(key))
        return;

    Request request;
    request.key = key;
    request.host = address;
    request.port = port == 0 ? 25565 : port;
    m_queue.append(request);
    m_knownKeys.insert(key);
    pump();
}

bool McServerPinger::isPending(const QString &key) const
{
    return m_knownKeys.contains(key);
}

void McServerPinger::cancelAll()
{
    m_queue.clear();
    const QList<Session *> sessions = m_sessions.values();
    for (Session *session : sessions)
    {
        if (!session)
            continue;
        session->finished = true;
        if (session->socket)
        {
            session->socket->disconnect(this);
            session->socket->abort();
            session->socket->deleteLater();
        }
        if (session->timer)
        {
            session->timer->stop();
            session->timer->deleteLater();
        }
        if (session->dns)
        {
            session->dns->disconnect(this);
            session->dns->deleteLater();
        }
        delete session;
    }
    m_sessions.clear();
    m_knownKeys.clear();
}

void McServerPinger::pump()
{
    while (m_sessions.size() < m_maxConcurrent && !m_queue.isEmpty())
        startSession(m_queue.takeFirst());
}

void McServerPinger::startSession(const Request &request)
{
    Session *session = new Session;
    session->request = request;
    session->socket = new QTcpSocket(this);
    session->timer = new QTimer(this);
    session->timer->setSingleShot(true);
    m_sessions.insert(request.key, session);

    // 回调只带 key：会话可能在 socket 自己发信号的过程中被回收
    const QString key = request.key;
    connect(session->socket, &QTcpSocket::connected, this, [this, key]() {
        if (Session *current = sessionOf(key))
            onConnected(*current);
    });
    connect(session->socket, &QTcpSocket::readyRead, this, [this, key]() {
        if (Session *current = sessionOf(key))
            onReadyRead(*current);
    });
    connect(session->socket, &QTcpSocket::errorOccurred, this,
            [this, key](QAbstractSocket::SocketError) {
                if (Session *current = sessionOf(key))
                    onSocketError(*current);
            });
    connect(session->timer, &QTimer::timeout, this, [this, key]() {
        Session *current = sessionOf(key);
        if (!current || current->finished)
            return;
        if (current->connectedMs <= 0)
        {
            onSocketError(*current);   // 连接阶段超时：还能退到 SRV
            return;
        }
        Result result;
        result.state = Result::Offline;
        result.error = tr("服务器响应超时");
        finish(*current, result);
    });

    session->socket->connectToHost(request.host, request.port);
    session->timer->start(kTimeoutMs);
}

McServerPinger::Session *McServerPinger::sessionOf(const QString &key) const
{
    return m_sessions.value(key, nullptr);
}

void McServerPinger::onConnected(Session &session)
{
    if (session.finished)
        return;
    session.connectedMs = QDateTime::currentMSecsSinceEpoch();
    session.timer->start(kTimeoutMs);   // 握手回复的时限单独计时
    sendHandshake(session);
}

void McServerPinger::sendHandshake(Session &session)
{
    if (!session.socket || session.socket->state() != QAbstractSocket::ConnectedState)
        return;

    QByteArray handshakeBody;
    writeVarInt(handshakeBody, 0x00);              // packet id: Handshake
    writeVarInt(handshakeBody, kProtocolVersion);
    writeString(handshakeBody, session.request.host);
    handshakeBody.append(static_cast<char>((session.request.port >> 8) & 0xFF));
    handshakeBody.append(static_cast<char>(session.request.port & 0xFF));
    writeVarInt(handshakeBody, 0x01);              // next state: status

    QByteArray handshakeFrame;
    writeVarInt(handshakeFrame, handshakeBody.size());
    handshakeFrame.append(handshakeBody);

    QByteArray requestBody;
    writeVarInt(requestBody, 0x00);                // packet id: Status Request
    QByteArray requestFrame;
    writeVarInt(requestFrame, requestBody.size());
    requestFrame.append(requestBody);

    session.socket->write(handshakeFrame + requestFrame);
    session.socket->flush();
}

void McServerPinger::connectSocket(Session &session, const QString &host, quint16 port)
{
    if (session.finished || !session.socket)
        return;

    session.switching = true;
    session.socket->abort();
    session.switching = false;

    session.buffer.clear();
    session.connectedMs = 0;
    session.request.host = host;
    session.request.port = port;
    session.socket->connectToHost(host, port);
    session.timer->start(kTimeoutMs);
}

void McServerPinger::onSocketError(Session &session)
{
    if (session.finished || session.switching)
        return;

    if (!session.srvTried)
    {
        // 直连不通：默认端口 + 域名的话再看 SRV 记录（很多服靠它换端口）
        const bool isIp = !QHostAddress(session.request.host).isNull();
        if (session.request.port == 25565 && !isIp && !session.request.host.isEmpty())
        {
            fallbackToSrv(session);
            return;
        }
        session.srvTried = true;
    }

    Result result;
    result.state = Result::Offline;
    if (session.socket)
        result.error = session.socket->errorString();
    finish(session, result);
}

void McServerPinger::fallbackToSrv(Session &session)
{
    session.srvTried = true;
    if (session.socket)
    {
        session.switching = true;
        session.socket->abort();
        session.switching = false;
    }
    session.buffer.clear();
    session.connectedMs = 0;

    if (!session.dns)
    {
        session.dns = new QDnsLookup(this);
        session.dns->setType(QDnsLookup::SRV);
        session.dns->setName(QStringLiteral("_minecraft._tcp.") + session.request.host);
    }

    const QString key = session.request.key;
    connect(session.dns, &QDnsLookup::finished, this, [this, key]() {
        Session *current = sessionOf(key);
        if (!current || current->finished)
            return;

        QString target;
        quint16 targetPort = 25565;
        if (current->dns && current->dns->error() == QDnsLookup::NoError
            && !current->dns->serviceRecords().isEmpty())
        {
            const QDnsServiceRecord record = current->dns->serviceRecords().first();
            target = record.target();
            if (target.endsWith(QLatin1Char('.')))
                target.chop(1);
            targetPort = record.port();
        }

        if (target.isEmpty())
        {
            Result result;
            result.state = Result::Offline;
            result.error = tr("无法连接到服务器");
            finish(*current, result);
            return;
        }
        connectSocket(*current, target, targetPort);
    });

    session.timer->start(kSrvTimeoutMs);
    session.dns->lookup();
}

void McServerPinger::onReadyRead(Session &session)
{
    if (session.finished || !session.socket)
        return;

    session.buffer += session.socket->readAll();
    if (session.buffer.size() > kMaxPacketBytes)
    {
        Result result;
        result.state = Result::Offline;
        result.error = tr("服务器返回的数据异常");
        finish(session, result);
        return;
    }

    int pos = 0;
    int frameLen = 0;
    if (!readVarInt(session.buffer, pos, frameLen))
        return;   // 包头还没收全
    if (frameLen <= 0 || frameLen > kMaxPacketBytes)
    {
        Result result;
        result.state = Result::Offline;
        result.error = tr("服务器返回的数据异常");
        finish(session, result);
        return;
    }
    if (session.buffer.size() - pos < frameLen)
        return;   // 包体还没收全

    int packetPos = pos;
    int packetId = 0;
    if (!readVarInt(session.buffer, packetPos, packetId) || packetId != 0x00)
    {
        Result result;
        result.state = Result::Offline;
        result.error = tr("该端口不是 Minecraft 服务器");
        finish(session, result);
        return;
    }

    QString payload;
    if (!readString(session.buffer, packetPos, payload))
    {
        Result result;
        result.state = Result::Offline;
        result.error = tr("服务器返回的数据异常");
        finish(session, result);
        return;
    }

    Result result;
    result.state = Result::Online;
    if (session.connectedMs > 0)
    {
        result.latencyMs = static_cast<int>(
            QDateTime::currentMSecsSinceEpoch() - session.connectedMs);
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject())
    {
        const QJsonObject root = doc.object();

        const QJsonObject version = root.value(QStringLiteral("version")).toObject();
        result.versionName = version.value(QStringLiteral("name")).toString();
        if (version.contains(QStringLiteral("protocol")))
            result.protocolVersion = version.value(QStringLiteral("protocol")).toInt(-1);

        const QJsonObject players = root.value(QStringLiteral("players")).toObject();
        result.playersOnline = players.value(QStringLiteral("online")).toInt(0);
        result.playersMax = players.value(QStringLiteral("max")).toInt(0);

        QString motd;
        flattenChat(root.value(QStringLiteral("description")), motd);
        result.motd = stripFormatting(motd);

        result.icon = decodeFavicon(root.value(QStringLiteral("favicon")).toString());
    }

    finish(session, result);
}

void McServerPinger::finish(Session &session, const Result &result)
{
    if (session.finished)
        return;
    session.finished = true;

    const QString key = session.request.key;
    Session *raw = &session;
    m_sessions.remove(key);
    m_knownKeys.remove(key);

    if (raw->socket)
    {
        raw->socket->disconnect(this);
        raw->socket->abort();
        raw->socket->deleteLater();
    }
    if (raw->timer)
    {
        raw->timer->stop();
        raw->timer->deleteLater();
    }
    if (raw->dns)
    {
        raw->dns->disconnect(this);
        raw->dns->deleteLater();
    }
    delete raw;

    emit pingFinished(key, result);
    pump();
}
