/**
 * @file   McServerPinger.h
 * @brief  Minecraft 服务器状态探测（Server List Ping，协议 1.7+）
 * @author BlockBox Team
 * @date   2026-10-25
 *
 * 握手流程（均为小端无关的 VarInt 长包）：
 *   1) 连上 host:port；
 *   2) 发 0x00 Handshake(protocolVersion, host, port, nextState=1=status)，
 *      紧接着发 0x00 Status Request；
 *   3) 服务端回 0x00 Status Response，payload 是一个 JSON 字符串，
 *      含 version.name / players.online / players.max / description / favicon。
 *
 * 探测到的信息只用于展示，不参与联机流程；失败（拒绝连接、超时、
 * 非 MC 服务端）一律按「离线」处理，不抛异常。
 *
 * 地址解析：先直连 host:port；若失败且端口为默认 25565 且 host 不是 IP，
 * 再尝试 _minecraft._tcp 的 SRV 记录（很多服务器靠 SRV 换端口）。
 */

#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

class QDnsLookup;
class QTcpSocket;
class QTimer;

class McServerPinger : public QObject
{
    Q_OBJECT

public:
    struct Result
    {
        enum State
        {
            Pending = 0,   ///< 还没结果（占位）
            Online = 1,
            Offline = 2
        };

        State state = Pending;
        int latencyMs = -1;          ///< 握手往返耗时（毫秒）；离线为 -1
        QString versionName;         ///< 服务端版本名，如 "1.20.1"、"Paper 1.20.1"
        int protocolVersion = -1;    ///< 服务端协议号，未知为 -1
        int playersOnline = 0;
        int playersMax = 0;
        QString motd;                ///< 已去掉 §颜色代码的纯文本 MOTD
        QPixmap icon;                ///< 服务端 64x64 图标，可能为空
        QString error;               ///< 离线原因（内部用，界面只作 tooltip）
    };

    explicit McServerPinger(QObject *parent = nullptr);
    ~McServerPinger() override;

    /** 开始探测；key 由调用方定义（这里只用来回传结果），重复 key 排队去重 */
    void ping(const QString &key, const QString &host, quint16 port);

    /** 该 key 是否正在排队/探测中 */
    bool isPending(const QString &key) const;

    /** 放弃全部探测（页面销毁/实例切换时调用），不发出信号 */
    void cancelAll();

signals:
    void pingFinished(const QString &key, const McServerPinger::Result &result);

private:
    struct Request
    {
        QString key;
        QString host;
        quint16 port = 25565;
    };

    struct Session;

    void pump();
    void startSession(const Request &request);
    void connectSocket(Session &session, const QString &host, quint16 port);
    void sendHandshake(Session &session);
    void onConnected(Session &session);
    void onReadyRead(Session &session);
    void onSocketError(Session &session);
    void fallbackToSrv(Session &session);
    void finish(Session &session, const Result &result);
    Session *sessionOf(const QString &key) const;

    QList<Request> m_queue;
    QHash<QString, Session *> m_sessions;
    QSet<QString> m_knownKeys;   ///< 排队 + 探测中的 key，用于去重
    int m_maxConcurrent = 6;     ///< 同时探测数上限，避免几十个服务器一起打
};

Q_DECLARE_METATYPE(McServerPinger::Result)
