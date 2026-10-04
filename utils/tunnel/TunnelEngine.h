/**
 * @file   TunnelEngine.h
 * @brief  内网穿透引擎抽象：把「不同网络下的两台设备」接进同一虚拟网络
 * @author BlockBox Team
 * @date   2026-09-26
 *
 * 引擎只负责组网与端口转发，具体的文件传输仍走 LanTransfer 的 TCP 协议，
 * 因此任意引擎（EasyTier / OpenP2P / …）接入后，上层弹窗与任务卡片都不用改：
 *
 *   发送方引擎.openForward(对端虚拟IP, 对端服务端口) → 本机 127.0.0.1:port
 *   LanTransfer 连接 127.0.0.1:port → 复用 ROOTS/LIST/MKDIR/PUT 全套协议
 */
#ifndef TUNNELENGINE_H
#define TUNNELENGINE_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

/** 组网参数（两台设备必须一致的只有 networkName / networkSecret） */
struct TunnelConfig
{
    QString networkName;                    ///< 虚拟网络名称
    QString networkSecret;                  ///< 虚拟网络密钥
    QString hostname;                       ///< 本机在虚拟网络中的主机名
    QStringList peerUrls;                   ///< 共享节点 / 发现地址（可多个）
    bool noTun = true;                      ///< 无 TUN 模式：免管理员权限
    bool privateMode = true;                ///< 仅允许同名同密钥的节点接入
};

/** 虚拟网络中的对端节点 */
struct TunnelPeer
{
    QString virtualIp;    ///< 虚拟 IP（如 10.144.144.2）
    QString hostname;     ///< 对端主机名
    QString cost;         ///< 链路代价：Local / 数字 / p2p / relay
    QString latency;      ///< 延迟（ms）
    QString tunnelProto;  ///< 打洞协议 udp / tcp / relay
    bool isSelf = false;  ///< 是否为本机

    bool isValid() const { return !virtualIp.isEmpty(); }
};

/** 引擎运行状态快照 */
struct TunnelStatus
{
    bool running = false;   ///< 引擎进程是否在运行
    QString virtualIp;      ///< 本机虚拟 IP
    QString hostname;       ///< 本机主机名
    QString message;        ///< 状态说明 / 错误信息
    QList<TunnelPeer> peers;///< 网络内其他节点
};

/**
 * @brief 内网穿透引擎接口
 *
 * 生命周期：isAvailable()（二进制就绪）→ start() → refreshPeers() 循环
 *          → stop()。发送时按需 openForward()，用完 closeForward()。
 */
class TunnelEngine : public QObject
{
    Q_OBJECT

public:
    explicit TunnelEngine(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~TunnelEngine() override = default;

    /** 引擎标识（设置持久化用） */
    virtual QString engineId() const = 0;
    /** 引擎显示名 */
    virtual QString engineName() const = 0;

    /** 运行时依赖（可执行文件等）是否已就绪 */
    virtual bool isAvailable() const = 0;
    /** 引擎是否正在运行 */
    virtual bool isRunning() const = 0;
    /** 当前状态快照 */
    virtual TunnelStatus status() const = 0;

    /** 启动组网（异步，结果通过 statusChanged 报告） */
    virtual void start(const TunnelConfig &config) = 0;
    /** 停止组网 */
    virtual void stop() = 0;
    /** 刷新本机信息与对端列表（异步） */
    virtual void refreshPeers() = 0;

    /**
     * @brief 建立一条到远端服务的端口转发
     * @param remoteIp   远端虚拟 IP
     * @param remotePort 远端服务端口
     * @param localPort  本机监听端口（0 = 自动分配）
     * @return 本机监听地址（如 "127.0.0.1:147322"），失败返回空
     */
    virtual QString openForward(const QString &remoteIp, quint16 remotePort,
                                quint16 localPort = 0) = 0;
    /** 关闭 openForward 返回的转发 */
    virtual void closeForward(const QString &forwardAddr) = 0;

signals:
    /** 状态（本机虚拟 IP / 运行态）变化 */
    void statusChanged();
    /** 对端列表变化 */
    void peersChanged();
    /** 错误（启动失败、二进制缺失、命令超时等） */
    void errorOccurred(const QString &message);
    /** 需要下载运行时依赖（isAvailable() == false 时触发） */
    void installRequired();
};

#endif // TUNNELENGINE_H
