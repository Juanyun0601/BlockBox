/**
 * @file   ChatP2P.h
 * @brief  聊天 P2P 直传：好友私聊文件的点对点传输（TCP 拉取式）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 好友私聊互发文件的直连通道，绕开服务器上传的 5 MB 限制与下载流量：
 * 发送方在本机起一个小型 TCP 服务，把「地址 + 端口 + 一次性口令」写进
 * kind:"p2p" 的邀约消息发给对方；接收方看到邀约后主动连过来拉取文件。
 *
 * 协议（行文本 + 二进制负载）：
 *   C→S  BBX_CHAT v1 <token>     → 出示一次性口令
 *   S→C  BBX_OK <size>           → 校验通过，随后紧跟 size 字节文件内容
 *   S→C  BBX_ERR <原因>          → 口令无效 / 邀约过期 / 文件读不了
 *
 * 安全边界：口令只随私聊消息发给对方（服务端把私信限定在会话双方）；
 * 接收端只写用户选定的保存路径；落盘字节数严格等于通告的 size；
 * 连接、握手与传输空闲均有超时。
 */
#ifndef CHATP2P_H
#define CHATP2P_H

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QTcpServer;
class QTcpSocket;
class QTimer;

/** 一次聊天直传的邀约（序列化进 kind:"p2p" 消息的 body） */
struct ChatP2POffer
{
    QString tid;     ///< 传输 id：一次邀约一个，信号与消息卡片用它对账
    QString token;   ///< 一次性口令：接收方握手时必须出示
    QString name;    ///< 原始文件名
    qint64 size = 0; ///< 文件字节数
    quint16 port = 0;///< 发送方 TCP 监听端口
    QStringList ips; ///< 发送方可直连的 IPv4（站点内网地址）
    QString act;     ///< offer = 邀约 / retry-normal = 请对方改走普通发送
    qint64 exp = 0;  ///< 邀约失效时间（ms since epoch，act=offer 时有效）

    /** 是一份可用的直传邀约（tid/口令/大小齐全才算） */
    bool isOffer() const
    {
        return (act.isEmpty() || act == QStringLiteral("offer")) && !tid.isEmpty()
            && !token.isEmpty() && size > 0;
    }
    /** 是一条控制消息（如「请对方改用普通发送」） */
    bool isControl() const { return !tid.isEmpty() && act == QStringLiteral("retry-normal"); }
    bool isValid() const { return isOffer() || isControl(); }
    /** 邀约是否带完整的直连信息（控制消息没有） */
    bool hasTransferInfo() const { return size > 0 && port > 0 && !ips.isEmpty(); }
    bool isExpired(qint64 nowMs = 0) const
    {
        if (exp <= 0)
            return false;
        if (nowMs <= 0)
            nowMs = QDateTime::currentMSecsSinceEpoch();
        return nowMs > exp;
    }

    QJsonObject toJson() const;
    static ChatP2POffer fromJson(const QJsonObject &object);
};

class ChatP2P : public QObject
{
    Q_OBJECT

public:
    /** 直传服务默认端口（LanTransfer 占 47321/47322，这里错开） */
    static constexpr quint16 kServicePort = 47323;
    /** 邀约有效期：过时接收方连上来会被拒绝 */
    static constexpr int kOfferLifetimeMs = 10 * 60 * 1000;

    static ChatP2P *instance();

    /** 本机可通告给私聊对方的站点内网 IPv4 列表 */
    static QStringList localTransferAddresses();

    // ── 发送侧（邀约方：起服务等对方来拉） ─────────────────────
    /**
     * @brief 开始共享一个本地文件：起监听并生成邀约（tid/token/port/ips/exp）
     * @return 填好的邀约；监听失败或文件读不了时 tid 为空
     */
    ChatP2POffer startShare(const QString &filePath);
    /** 停止共享（取消邀约 / 清理监听），未完成会以失败收尾 */
    void stopShare();
    /** 当前正在共享的传输 id（没有则为空；同一时刻只支持一份） */
    QString activeShareTid() const { return m_shareTid; }
    /** 当前共享是否已经完整发给过对方（保持监听供对方重试落盘） */
    bool shareCompleted() const { return m_shareCompleted; }

    // ── 接收侧（主动连过去拉文件） ─────────────────────────────
    /**
     * @brief 按邀约把文件拉取到 savePath
     * @return 请求 id（即邀约 tid）；邀约无效或同一邀约已在接收中返回空串
     */
    QString requestFile(const ChatP2POffer &offer, const QString &savePath);
    /** 某个邀约是否正在接收中 */
    bool isReceiving(const QString &tid) const { return m_receives.contains(tid); }

signals:
    void shareStarted(const QString &tid);
    /** 发送进度：percent 0~100，speed 字节/秒 */
    void shareProgress(const QString &tid, double percent, qint64 bytesSent,
                       qint64 bytesTotal, qint64 speed);
    void shareFinished(const QString &tid, bool success, const QString &message);
    /** 接收进度：percent 0~100，speed 字节/秒 */
    void receiveProgress(const QString &requestId, double percent, qint64 bytesReceived,
                         qint64 bytesTotal, qint64 speed);
    void receiveFinished(const QString &requestId, bool success, const QString &savePath,
                         const QString &message);

private slots:
    void handleIncomingConnection();
    void sweepExpiredShare();

private:
    explicit ChatP2P(QObject *parent = nullptr);
    ~ChatP2P() override;
    Q_DISABLE_COPY(ChatP2P)

    class ShareConnection;   ///< 对方来拉文件的一个连接
    class ReceiveTransfer;   ///< 本机主动发起的一次拉取
    friend class ShareConnection;
    friend class ReceiveTransfer;

    // ── 供 ShareConnection / ReceiveTransfer 回调 ──────────────
    bool validateShareToken(const QString &token) const;
    QString shareFilePath() const { return m_sharePath; }
    void reportShareProgress(qint64 bytes);
    void reportShareComplete();
    void onShareConnectionClosed(ShareConnection *connection);
    void reportReceiveProgress(const QString &requestId, qint64 received, qint64 total, qint64 speed);
    void reportReceiveFinished(const QString &requestId, bool success, const QString &savePath,
                               const QString &message);

    void finishShare(bool expired);
    /** 关掉所有正在拉文件的连接（ShareConnection 定义在实现文件里，收尾时调） */
    void closeShareConnections();

    QTcpServer *m_server = nullptr;
    QString m_shareTid;
    QString m_shareToken;
    QString m_sharePath;
    QString m_shareTaskId;          ///< 任务栏卡片 id（开始共享时创建）
    qint64 m_shareSize = 0;
    qint64 m_shareExpireAt = 0;
    bool m_shareCompleted = false;
    QList<ShareConnection *> m_shareConns;
    QTimer *m_shareExpiryTimer = nullptr;
    qint64 m_shareSentBytes = 0;    ///< 进度参考（取连接里的最大值）
    qint64 m_shareSpeedWindowStart = 0;
    qint64 m_shareSpeedWindowBytes = 0;
    qint64 m_shareLastSpeed = 0;
    qint64 m_shareLastReport = 0;

    QHash<QString, ReceiveTransfer *> m_receives;
};

#endif // CHATP2P_H
