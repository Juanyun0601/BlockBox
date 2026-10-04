/**
 * @file   LanTransfer.h
 * @brief  局域网互传：设备发现 + 文件/目录收发服务
 * @author BlockBox Team
 * @date   2026-09-26
 *
 * 同一局域网内的多个方块盒子客户端可互相发现并传输本地资源（模组、存档等）。
 *
 * 协议（全部为本机局域网内明文协议，不做公网暴露）：
 *  - 发现：UDP 广播端口 kDiscoveryPort
 *          请求 BBX_DISC_REQ|<json>，应答 BBX_DISC_RSP|<json>（单播回请求方）
 *  - 传输：TCP 随机端口（在发现应答里通告），按行文本命令 + 二进制负载：
 *          ROOTS              → 若干 JSON 行 + OK
 *          LIST <dir>         → 若干 JSON 行 + OK / ERR <msg>
 *          MKDIR <dir>        → OK / ERR <msg>
 *          PUT <file> <size>  → 接收 size 字节后回 OK / ERR <msg>
 *          QUIT               → 关闭连接
 *
 * 安全边界：接收端只允许在自身共享目录（.minecraft / BlockBox 数据目录 /
 * 基岩版数据目录）内读写，路径统一做前缀校验，杜绝任意路径写入。
 */
#ifndef LANTRANSFER_H
#define LANTRANSFER_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QFile;
class QTcpServer;
class QTcpSocket;
class QUdpSocket;
class QTimer;

/** 局域网设备共享的根目录（发送方资源管理器的入口） */
struct LanShareRoot
{
    QString name;  ///< 显示名，如「Java 版游戏目录」
    QString path;  ///< 接收端绝对路径
};

/** 局域网中被发现的设备 */
struct LanDevice
{
    QString id;      ///< 设备唯一标识（每次启动随机生成）
    QString name;    ///< 设备名（主机名）
    QString address; ///< IPv4 地址
    quint16 port = 0;///< 传输服务 TCP 端口
    QList<LanShareRoot> roots; ///< 对方通告的共享根目录
    qint64 lastSeen = 0;       ///< 最近一次发现时间（ms）
    bool self = false;         ///< 是否为本机
    bool remote = false;       ///< 是否为远程设备（经内网穿透的虚拟网络节点）

    bool isValid() const { return !address.isEmpty() && port > 0; }
};

/** 远端目录条目（资源管理器列表项） */
struct LanEntry
{
    QString name;
    bool isDir = false;
    qint64 size = 0;
};

class LanTransfer : public QObject
{
    Q_OBJECT

public:
    /** 传输服务默认端口（远程互传的端口转发以此为目标，勿随意改动） */
    static constexpr quint16 kServicePort = 47322;

    static LanTransfer *instance();

    // ── 接收服务 ────────────────────────────────────────────────
    /** 启动接收服务（TCP 监听 + 发现应答），可重复调用 */
    void startService();
    /** 停止接收服务 */
    void stopService();
    bool isServiceRunning() const;

    QString deviceName() const;
    /** 当前正在进行的局域网发送任务 id（无则为空） */
    QString sendingTaskId() const { return m_sendTaskId; }
    /** 本机通告的共享根目录 */
    QList<LanShareRoot> shareRoots() const;
    /** 本机传输服务端口（未运行为 0） */
    quint16 servicePort() const;

    // ── 设备发现 ────────────────────────────────────────────────
    void startDiscovery();
    void stopDiscovery();
    bool isDiscovering() const;
    QList<LanDevice> devices() const;
    LanDevice device(const QString &id) const;

    // ── 远端浏览（供弹窗内的资源管理器使用） ────────────────────
    void browseRoots(const LanDevice &device);
    void browseDirectory(const LanDevice &device, const QString &remotePath);

    // ── 发送 ────────────────────────────────────────────────────
    /**
     * @brief 将本地文件/目录发送到远端设备的指定目录
     * @param device     目标设备
     * @param localPaths 本地文件或目录（目录会递归上传）
     * @param remoteDir  远端目标目录（必须位于对方共享根目录内）
     * @return 任务 id（DownloadTaskManager 中的任务卡片），失败返回空串
     */
    QString sendFiles(const LanDevice &device, const QStringList &localPaths,
                      const QString &remoteDir);
    /** 取消当前发送（仅支持单并发发送） */
    void cancelSend();
    bool isSending() const;

signals:
    /** 设备列表变化（发现新设备 / 设备超时移除） */
    void devicesChanged();
    /** 接收服务状态变化 */
    void receivingChanged(bool running);
    /** 远端根目录返回 */
    void rootsReceived(const QString &deviceId, const QList<LanShareRoot> &roots);
    /** 远端目录列表返回 */
    void directoryReceived(const QString &deviceId, const QString &remotePath,
                           const QList<LanEntry> &entries);
    /** 远端浏览失败 */
    void browseFailed(const QString &deviceId, const QString &error);
    /** 发送进度（taskId 对应任务卡片） */
    void sendProgress(const QString &taskId, double percent, qint64 bytesSent,
                      qint64 bytesTotal, qint64 speed);
    /** 发送结束 */
    void sendFinished(const QString &taskId, bool success, const QString &message);
    /** 本机接收到一个文件 */
    void fileReceived(const QString &path, qint64 size);

private:
    LanTransfer();
    ~LanTransfer() override;
    Q_DISABLE_COPY(LanTransfer)

    class Connection;

    // ── 发现 ────────────────────────────────────────────────────
    void ensureUdpSocket();
    void sendDiscoveryRequest();
    void handleDatagram(const QByteArray &data, const QString &senderAddr, quint16 senderPort);
    QByteArray advertisementPayload() const;
    void sweepExpiredDevices();

    // ── 接收端 ──────────────────────────────────────────────────
    void handleIncomingConnection();
    bool isPathAllowed(const QString &path) const;

    // ── 发送端状态机 ────────────────────────────────────────────
    struct SendAction
    {
        enum Type { Mkdir, Put } type = Mkdir;
        QString remotePath;
        QString localPath;
        qint64 size = 0;
    };
    void beginSend(const LanDevice &device, const QStringList &localPaths,
                   const QString &remoteDir);
    void collectLocalTree(const QString &localPath, const QString &remoteBase,
                          QList<SendAction> &mkdirs, QList<SendAction> &puts,
                          qint64 &totalBytes);
    void sendNextAction();
    void writeSendChunk();
    void onSendReadyRead();
    void onSendBytesWritten(qint64 bytes);
    void onSendSocketError();
    void finishSend(bool success, const QString &message);
    void reportSendProgress(qint64 speed = -1);

private:
    static LanTransfer *m_instance;

    // 本机身份
    QString m_selfId;
    QString m_deviceName;

    // 接收服务
    QTcpServer *m_server = nullptr;
    bool m_serviceRunning = false;

    // 发现
    QUdpSocket *m_udp = nullptr;
    QTimer *m_broadcastTimer = nullptr;
    QTimer *m_sweepTimer = nullptr;
    bool m_discovering = false;
    QHash<QString, LanDevice> m_devices;

    // 远端浏览（单并发，UI 串行使用）
    void startBrowse(const LanDevice &device, const QByteArray &command);
    QTcpSocket *m_browseSocket = nullptr;
    QString m_browseDeviceId;
    QString m_browseCommand;   ///< "ROOTS" 或 "LIST <path>"
    QString m_browsePath;
    QByteArray m_browseBuffer;
    QList<QByteArray> m_browseEntries;  ///< OK 之前收到的 JSON 行
    bool m_browsePending = false;       ///< 是否仍在等待本次浏览的响应

    // 发送（单并发）
    QTcpSocket *m_sendSocket = nullptr;
    QString m_sendTaskId;
    LanDevice m_sendDevice;
    QString m_sendRemoteDir;
    QList<SendAction> m_sendActions;
    int m_sendIndex = 0;
    qint64 m_sendTotal = 0;
    qint64 m_sendDone = 0;      ///< 已确认完成的字节数（不含当前文件未应答部分）
    qint64 m_sendWritten = 0;   ///< 当前文件已交给 socket 的字节数
    qint64 m_sendFlushed = 0;   ///< 当前文件已被 socket 刷出的字节数（背压依据）
    bool m_sendCancelled = false;
    QFile *m_sendFile = nullptr;
    QByteArray m_sendBuffer;    ///< 发送端待解析的远端应答
    qint64 m_sendSpeedWindowStart = 0;
    qint64 m_sendSpeedWindowBytes = 0;
    qint64 m_sendLastSpeed = 0;
    qint64 m_sendLastReport = 0;
    bool m_sendAwaitingAck = false;
    bool m_sendPayloadMode = false;
};

#endif // LANTRANSFER_H
