/**
 * @file   FrpManager.h
 * @brief  frp 服务商联机管理：樱花 SakuraFrp / OpenFrp
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 两家服务商的接入方式一致：
 *   1. 官网面板注册并创建 TCP 隧道（本地地址 127.0.0.1，端口 = 游戏端口）；
 *   2. 应用内通过各家公开 API 自动下载对应的 frpc 客户端：
 *        樱花    GET https://api.natfrp.com/v4/system/clients → frpc 段各平台直链
 *        OpenFrp GET https://api.openfrp.net/commonQuery/get?key=software
 *                → data.latest_full + data.source[] 拼 {source}/{ver}/frpc_windows_amd64.zip
 *   3. 填入访问密钥 + 隧道 ID 启动（免配置文件）：
 *        樱花    frpc -f <访问密钥>:<隧道ID>
 *        OpenFrp frpc -u <访问密钥> -p <隧道ID>
 *   4. 增量解析 frpc 日志（剥离 ANSI 色码），提取远程连接地址（域名:端口）
 *      供 Minecraft 直连；地址也可以在面板隧道详情里查看。
 */
#ifndef FRPMANAGER_H
#define FRPMANAGER_H

#include <QColor>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class FrpManager : public QObject
{
    Q_OBJECT

public:
    enum class Provider
    {
        Sakura,  ///< 樱花内网穿透 SakuraFrp（natfrp.com）
        OpenFrp  ///< OpenFrp（openfrp.net）
    };

    static FrpManager *instance();

    static QString providerId(Provider p);    ///< 存储用 id：sakura / openfrp
    static QString providerName(Provider p);  ///< 显示名：樱花联机 / OpenFrp 联机
    static QColor providerColor(Provider p);  ///< 下拉框圆点图标颜色
    static QString panelUrl(Provider p);      ///< 官网面板（注册 / 建隧道 / 查密钥）

    // ── 安装 ──
    QString installRoot(Provider p) const;      ///< <AppData>/BlockBox/frp/<id>/
    bool isFrpcPresent(Provider p) const;
    QString frpcPath(Provider p) const;
    QString installedVersion(Provider p) const; ///< 已安装版本（Settings 持久化）
    QString latestVersion(Provider p) const;    ///< API 查到的最新版本（未查返回空）
    /** 检查最新版本并下载安装 frpc（结果通过 installFinished 报告） */
    void downloadFrpc(Provider p);
    /** 手动选择 frpc 程序：拷入安装目录后按已安装处理，失败返回 false */
    bool importFrpc(Provider p, const QString &exePath);
    bool isDownloading() const { return m_dlReply != nullptr; }
    bool isDownloading(Provider p) const { return m_dlProvider == p && isDownloading(); }
    qreal downloadProgress() const { return m_downloadProgress; }
    QString downloadInfo() const { return m_downloadInfo; }

    // ── 配置（按服务商持久化到 Settings）──
    QString accessKey(Provider p) const;
    void setAccessKey(Provider p, const QString &key);
    int tunnelId(Provider p) const;
    void setTunnelId(Provider p, int id);

    // ── 运行 ──
    bool isRunning() const;
    bool isTunnelEstablished() const { return m_established; }
    QString remoteAddress() const { return m_remoteAddress; }
    /** 状态说明：连接节点中 / 隧道已建立 / 已断开（含原因） */
    QString statusText() const;
    /** 启动隧道（使用已保存的密钥与隧道 ID），结果通过信号报告 */
    void start(Provider p);
    void stop();

signals:
    void stateChanged();
    void remoteAddressChanged();
    void downloadStateChanged();
    void downloadProgressChanged(qreal progress);
    void downloadInfoChanged(const QString &info);
    void installFinished(bool success, const QString &message);
    void errorOccurred(const QString &message);

private:
    explicit FrpManager(QObject *parent = nullptr);
    ~FrpManager() override;
    Q_DISABLE_COPY_MOVE(FrpManager)

    /** 扫描安装目录定位 frpc（精确 frpc.exe 优先，其次 frpc*.exe） */
    void locateFrpc(Provider p);
    QStringList buildLaunchArgs(Provider p) const;
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void parseLogLine(const QString &line);
    /** 从日志行提取远程地址（优先服务商专属域名后缀，排除 API/本机/内网地址） */
    QString extractAddress(const QString &line) const;

    // ── 下载安装 ──
    void fetchSakuraDownload();
    void fetchOpenFrpDownload();
    void startAssetDownload(const QString &url, qint64 expectedSize);
    void onAssetDownloadFinished();
    bool validateArchive(const QString &path, bool isZip, qint64 expectedSize) const;
    void extractAndInstall(const QString &archivePath, Provider p,
                           const QString &version, bool isZip);
    void finishInstall(bool success, const QString &message);
    void emitDownloadInfo(const QString &info);
    void ensureWatchdog();
    void stopWatchdog();
    void checkStall();

    static FrpManager *m_instance;
    QNetworkAccessManager m_nam;

    // 运行状态
    QProcess *m_process = nullptr;
    Provider m_runningProvider = Provider::Sakura;
    bool m_established = false;
    QString m_remoteAddress;
    QString m_lastLogLine;
    QString m_exitReason;
    QString m_logBuffer;

    // 下载状态
    Provider m_dlProvider = Provider::Sakura;
    QString m_latestVersion;
    QStringList m_downloadQueue;
    QString m_pendingArchive;
    bool m_pendingIsZip = true;
    qint64 m_expectedSize = -1;
    QNetworkReply *m_dlReply = nullptr;
    QTimer *m_watchdog = nullptr;
    qint64 m_lastProgressAt = 0;
    qreal m_downloadProgress = -1.0;
    QString m_downloadInfo;
    bool m_importing = false;

    // frpc 路径缓存（<id> → 路径）
    QHash<QString, QString> m_frpcPaths;
};

#endif // FRPMANAGER_H
