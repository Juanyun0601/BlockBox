/**
 * @file   HongshiClient.h
 * @brief  红石联机客户端 - 管理红石联机(hongshi) 核心进程与状态文件
 * @author BlockBox Team
 * @date   2026-08-19
 *
 * 参考官方开源外壳 hongshionline/hongshi-shell (Tauri+Rust) 实现：
 *   1. 内核存放于 <程序目录>/hongshi/（hongshi.exe + version.txt + tunnel.ini）
 *   2. 从 https://hongshi.site（主站）/ https://shithub.site（镜像）
 *      获取 core_version.json（版本号）、newserver.json（中转节点列表）
 *      与 /api/download/xxx（签名下载地址）
 *   3. 启动方式：hongshi.exe -server <节点> -port <本地MC端口> -status-file <tunnel.ini>
 *   4. 通过轮询 tunnel.ini（status / server / port）判断隧道是否建立
 */

#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class HongshiClient : public QObject
{
    Q_OBJECT

public:
    /// 联机客户端运行状态
    enum class State
    {
        NotInstalled,   ///< 未安装内核（hongshi.exe 缺失）
        Ready,          ///< 已安装但未运行
        Launching,      ///< 正在启动内核进程
        Connecting,     ///< 内核运行中，隧道尚未建立
        Open,           ///< 隧道已建立（tunnel.ini status=open）
        Failed,         ///< 内核进程意外退出
        Fatal           ///< 启动失败（参数错误 / 进程无法拉起）
    };
    Q_ENUM(State)

    /// 安装状态
    enum class InstallStatus
    {
        NotInstalled,    ///< 未安装
        Ready,           ///< 已安装可用
        CheckingUpdate,  ///< 正在检查更新
        UpdateAvailable, ///< 有新版本可更新
        UpToDate,        ///< 已是最新版本
        Downloading,     ///< 正在下载
        DownloadFailed   ///< 下载失败
    };
    Q_ENUM(InstallStatus)

    /// 中转服务器节点
    struct Node
    {
        QString name; ///< 节点名称
        QString host; ///< 节点地址（传给 -server 参数）
    };

    /**
     * @brief 获取单例
     */
    static HongshiClient *instance();

    /// 当前运行状态
    State state() const { return m_state; }
    /// 安装状态
    InstallStatus installStatus() const { return m_installStatus; }
    /// 内核是否已存在（hongshi.exe 文件存在）
    bool isKernelPresent() const;
    /// 已安装版本号（version.txt 内容，无记录返回空）
    QString installedVersion() const { return m_installedVersion; }
    /// 远程最新版本号（未检测返回空）
    QString latestVersion() const { return m_latestVersion; }
    /// 下载进度（0.0~1.0，-1 表示无下载任务）
    qreal downloadProgress() const { return m_downloadProgress; }
    /// 是否已启动内核进程
    bool isProcessRunning() const;
    /// 是否处于联机活跃状态（启动中/连接中/隧道已建立）
    bool isTunnelActive() const;
    /// 当前错误信息
    QString errorMessage() const { return m_errorMessage; }

    /// 隧道服务器地址（tunnel.ini server 字段）
    QString tunnelServer() const { return m_tunnelServer; }
    /// 隧道端口（tunnel.ini port 字段）
    int tunnelPort() const { return m_tunnelPort; }
    /// 完整隧道地址（server:port，未建立返回空）
    QString tunnelAddress() const;

    /// 当前可用的服务器节点列表
    QList<Node> serverNodes() const { return m_nodes; }

    /**
     * @brief 启动红石联机内核
     * @param server 中转服务器节点地址
     * @param port   本地 Minecraft 端口（对局域网开放的端口）
     */
    void startKernel(const QString &server, quint16 port);

    /**
     * @brief 停止内核进程（结束联机）
     */
    void stopKernel();

    /**
     * @brief 检查远程最新版本（异步，不下载）
     */
    void checkUpdate();

    /**
     * @brief 检查并下载最新版本内核（异步）
     */
    void downloadLatest();

    /**
     * @brief 刷新服务器节点列表（异步，成功后发 serverNodesChanged）
     */
    void refreshServerNodes();

signals:
    /// 运行状态变化
    void stateChanged();
    /// 安装状态变化
    void installStatusChanged();
    /// 错误发生
    void errorOccurred(const QString &error);
    /// 服务器节点列表变化
    void serverNodesChanged();
    /// 下载进度变化
    void downloadProgressChanged(qreal progress);

private:
    explicit HongshiClient(QObject *parent = nullptr);
    ~HongshiClient() override;
    Q_DISABLE_COPY_MOVE(HongshiClient)

    // ---- 路径 ----
    QString hongshiRootDir() const;         ///< <程序目录>/hongshi/
    QString hongshiExecutablePath() const;  ///< hongshi.exe
    QString versionFilePath() const;        ///< version.txt
    QString statusFilePath() const;         ///< tunnel.ini
    QString logDirPath() const;             ///< logs/
    QString downloadApiPath() const;        ///< 平台下载接口路径

    // ---- 远程查询 ----
    void fetchCoreVersion();                ///< 获取远程最新版本号（主站→镜像）
    void applyInstallStatus();              ///< 依据本地/远程版本刷新安装状态

    // ---- 下载安装 ----
    void startDownload();                   ///< 获取签名下载地址并下载
    void startDownloadFile(const QString &url);
    void downloadFailed(const QString &msg, const QString &taskId);

    // ---- 进程与状态 ----
    void launchProcess();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void pollStatus();
    void parseStatusFile();
    void setState(State newState);
    void setErrorMessage(const QString &msg);

    // ---- 内部数据 ----
    State m_state = State::NotInstalled;
    InstallStatus m_installStatus = InstallStatus::NotInstalled;
    QString m_installedVersion;
    QString m_latestVersion;
    QString m_errorMessage;
    qreal m_downloadProgress = -1.0;

    QString m_tunnelServer;
    int m_tunnelPort = -1;
    QList<Node> m_nodes;

    bool m_fetchingVersion = false;
    bool m_pendingDownload = false;
    bool m_stopping = false;

    QProcess *m_process = nullptr;
    QNetworkAccessManager m_networkManager;
    QTimer m_pollTimer;               ///< 隧道状态轮询定时器
    QTimer m_watchTimer;              ///< 进程存活轻量巡检
};