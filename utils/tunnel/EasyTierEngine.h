/**
 * @file   EasyTierEngine.h
 * @brief  EasyTier 内网穿透引擎（免管理员权限的 --no-tun 模式）
 * @author BlockBox Team
 * @date   2026-09-26
 *
 * EasyTier 是去中心化的开源组网方案，社区提供免费共享节点，无需注册账号、
 * 无需自建服务器（项目内的陶瓦联机功能同属 EasyTier 生态）。
 *
 * 运行方式：
 *   easytier-core --no-tun --private-mode true
 *                 --network-name X --network-secret Y
 *                 --hostname H -m blockbox -r 127.0.0.1:15890
 *                 -p <共享节点…>
 *   → 不创建 TUN 设备，因此不需要管理员权限；本机仍可被其他节点通过虚拟 IP 访问，
 *     主动访问则走 easytier-cli port-forward（见 openForward）。
 *
 * 状态与转发通过 easytier-cli 读取：
 *   easytier-cli -p 127.0.0.1:15890 node      → 本机虚拟 IP / 主机名
 *   easytier-cli -p 127.0.0.1:15890 peer      → 网络内节点列表
 *   easytier-cli -p 127.0.0.1:15890 port-forward add tcp <local> <remote>
 */
#ifndef EASYTIERENGINE_H
#define EASYTIERENGINE_H

#include "TunnelEngine.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTimer>

class QNetworkReply;

class EasyTierEngine : public TunnelEngine
{
    Q_OBJECT

public:
    explicit EasyTierEngine(QObject *parent = nullptr);
    ~EasyTierEngine() override;

    QString engineId() const override { return QStringLiteral("easytier"); }
    QString engineName() const override { return QStringLiteral("EasyTier"); }

    bool isAvailable() const override;
    bool isRunning() const override;
    TunnelStatus status() const override;

    void start(const TunnelConfig &config) override;
    void stop() override;
    void refreshPeers() override;

    QString openForward(const QString &remoteIp, quint16 remotePort,
                        quint16 localPort) override;
    void closeForward(const QString &forwardAddr) override;

    // ── 安装 ──
    /** 安装目录：<AppData>/BlockBox/easytier/<version>/ */
    QString installRoot() const;
    QString corePath() const { return m_corePath; }
    QString cliPath() const { return m_cliPath; }

    /** 检查 GitHub 最新版本并下载安装（结果通过 installFinished 报告） */
    void downloadLatest();
    /** 下载进度 0~1，-1 表示无任务 */
    qreal downloadProgress() const { return m_downloadProgress; }
    /** 远程最新版本号（未检查返回空） */
    QString latestVersion() const { return m_latestVersion; }

signals:
    /** 安装状态变化（下载中 / 已完成 / 失败） */
    void installStateChanged();
    /** 安装完成（isAvailable 变为 true） */
    void installFinished(bool success, const QString &message);
    /** 下载进度变化 */
    void downloadProgressChanged(qreal progress);
    /** 下载线路/阶段文案（当前走哪条镜像、是否在切换、是否在解压） */
    void downloadInfoChanged(const QString &info);

private:
    /** 扫描安装目录定位可执行文件 */
    void locateBinaries();
    /** 同步执行 easytier-cli 并返回标准输出（失败返回空） */
    QString runCli(const QStringList &args, int timeoutMs = 5000) const;
    /** 解析 `easytier-cli node` 输出 */
    void parseNodeInfo(const QString &output);
    /** 解析 `easytier-cli peer` 输出 */
    QList<TunnelPeer> parsePeerList(const QString &output) const;
    /** 构造启动参数 */
    QStringList buildCoreArgs(const TunnelConfig &config) const;
    /** 分配一个空闲的本机端口 */
    quint16 allocateLocalPort() const;
    /** 处理核心进程退出 */
    void onCoreFinished(int exitCode, QProcess::ExitStatus status);

    // 下载安装
    void onVersionReceived(const QByteArray &data);
    /** 按队列顺序尝试一条下载线路（官方镜像优先，直连 GitHub 兜底） */
    void startAssetDownload(const QString &url);
    void onAssetDownloadFinished();
    /** 下载内容校验（ZIP 头 + 尺寸），不通过则换下一条线路 */
    bool validateArchive(qint64 expectedSize) const;
    void extractAndInstall(const QString &archivePath, const QString &version);
    /** 给 UI 的下载阶段文案 */
    void emitDownloadInfo(const QString &info);
    /** 启动 / 停止停滞看门狗 */
    void ensureDownloadWatchdog();
    void stopDownloadWatchdog();
    /** 停滞检测：超过阈值无进度则中止当前线路（触发自动切换） */
    void checkDownloadStall();

    QProcess *m_process = nullptr;
    TunnelConfig m_config;
    TunnelStatus m_status;

    QString m_corePath;
    QString m_cliPath;

    // 端口转发表：本机监听地址 → 已登记
    QHash<QString, QString> m_forwards;

    // 下载安装
    QNetworkAccessManager m_nam;
    QNetworkReply *m_downloadReply = nullptr;
    QString m_pendingArchive;
    QString m_latestVersion;
    QString m_manualUrl;          ///< 原始下载地址（失败提示用）
    QStringList m_downloadQueue;  ///< 待尝试的下载线路
    QTimer *m_downloadWatchdog = nullptr; ///< 停滞看门狗（长时间 0% 自动换线路）
    qint64 m_lastProgressAt = 0;          ///< 最近一次进度变化时间
    qint64 m_expectedSize = -1;   ///< 资产体积（校验用，未知为 -1）
    qreal m_downloadProgress = -1.0;
    bool m_installing = false;

    // 轮询定时器
    mutable QTimer *m_pollTimer = nullptr;
};

#endif // EASYTIERENGINE_H
