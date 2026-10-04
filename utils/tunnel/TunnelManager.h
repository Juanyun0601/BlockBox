/**
 * @file   TunnelManager.h
 * @brief  内网穿透门面：管理引擎、组网参数与共享节点
 * @author BlockBox Team
 * @date   2026-09-26
 *
 * 对上层只暴露三个概念：
 *   1. 组网邀请码（网络名|密钥）—— 两台设备填同一份即可互通
 *   2. 启动 / 停止 / 状态（本机虚拟 IP、对端节点列表）
 *   3. 端口转发 openForward —— 把「对方的传输端口」映射成本机回环地址
 */
#ifndef TUNNELMANAGER_H
#define TUNNELMANAGER_H

#include "TunnelEngine.h"

#include <QObject>
#include <QString>
#include <QStringList>

class TunnelManager : public QObject
{
    Q_OBJECT

public:
    static TunnelManager *instance();

    /** 当前引擎（后续可切换 OpenP2P 等实现） */
    TunnelEngine *engine() const { return m_engine; }

    /** 运行时依赖是否就绪 */
    bool isAvailable() const;
    /** 是否正在组网 */
    bool isRunning() const;
    /** 当前状态 */
    TunnelStatus status() const;

    // ── 组网参数 ──
    QString networkName() const;
    QString networkSecret() const;
    void setNetworkIdentity(const QString &name, const QString &secret);

    /** 邀请码：网络名|密钥，发给对方整段粘贴即可 */
    QString inviteCode() const;
    /** 解析邀请码，格式非法返回 false */
    bool setInviteCode(const QString &code);

    /** 共享节点 / 发现地址列表（首次为空时自动拉取社区节点） */
    QStringList peerUrls() const;
    void setPeerUrls(const QStringList &urls);

    // ── 控制 ──
    void start();
    void stop();
    void refreshPeers();

    /** 触发下载运行时依赖（isAvailable() == false 时） */
    void ensureInstalled();

signals:
    void statusChanged();
    void peersChanged();
    void errorOccurred(const QString &message);
    void installStateChanged();
    void installFinished(bool success, const QString &message);
    void downloadProgressChanged(qreal progress);
    /** 下载线路/阶段文案（当前镜像、切换、解压） */
    void downloadInfoChanged(const QString &info);
    /** 共享节点列表加载完成 */
    void peerUrlsChanged();

private:
    explicit TunnelManager(QObject *parent = nullptr);
    ~TunnelManager() override;
    Q_DISABLE_COPY_MOVE(TunnelManager)

    void loadOrFetchPeerUrls();
    void fetchDefaultPeerUrls();

    static TunnelManager *m_instance;
    TunnelEngine *m_engine = nullptr;
    QStringList m_peerUrls;
    bool m_peerUrlsLoaded = false;
};

#endif // TUNNELMANAGER_H
