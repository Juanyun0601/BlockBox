/**
 * @file   HongshiClient.h
 * @brief  红石联机客户端 - 管理红石联机（Redstone Online2 / hongshic）核心进程与状态
 * @author BlockBox Team
 * @date   2026-08-19
 *
 * 参考官方接入文档 https://hongshi.site/api.html：
 *   1. 内核 hongshic 存放于 <程序目录>/hongshi/（hongshic.exe + version.txt + build.txt）
 *   2. 下载接口 GET /api/download/client?platform=<windows|linux|macos>&arch=<amd64|arm64>
 *      直接返回可执行文件；中转节点列表 GET /api/server/list
 *   3. 启动方式：hongshic -t <节点> -p <本地MC端口>
 *   4. 从内核 stdout 中读取 endpoint=<地址>，它就是分享给好友的联机地址
 *
 * 注意：官方从 Redstone Online2 起不再提供 core_version.json / newserver.json，
 * 也不再使用 tunnel.ini；版本更新通过下载接口的 ETag / Last-Modified 判断。
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>

class HongshiClient : public QObject
{
    Q_OBJECT

public:
    /// 联机客户端运行状态
    enum class State
    {
        NotInstalled,   ///< 未安装内核（hongshic 缺失）
        Ready,          ///< 已安装但未运行
        Launching,      ///< 正在启动内核进程
        Connecting,     ///< 内核运行中，隧道尚未建立
        Open,           ///< 隧道已建立（stdout 已输出 endpoint=）
        Failed,         ///< 内核进程异常退出
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
        DownloadFailed,  ///< 下载失败
        Unsupported      ///< 当前平台官方没有可用的内核
    };
    Q_ENUM(InstallStatus)

    /// 中转服务器节点
    struct Node
    {
        QString name; ///< 节点名称（官方返回的区域名）
        QString host; ///< 节点地址（传给 -t 参数）
    };

    /**
     * @brief 获取单例
     */
    static HongshiClient *instance();

    /// 当前运行状态
    State state() const { return m_state; }
    /// 安装状态
    InstallStatus installStatus() const { return m_installStatus; }
    /// 内核是否已存在（hongshic 文件存在）
    bool isKernelPresent() const;
    /// 当前平台是否有官方内核可下载
    bool isPlatformSupported() const;
    /// 已安装版本号（version.txt 内容，无记录返回空）
    QString installedVersion() const { return m_installedVersion; }
    /// 远程最新版本号（官方新接口不提供版本号，通常为空；仅供界面展示兼容）
    QString latestVersion() const { return m_latestVersion; }
    /// 下载进度（0.0~1.0，-1 表示无下载任务）
    qreal downloadProgress() const { return m_downloadProgress; }
    /// 是否已启动内核进程
    bool isProcessRunning() const;
    /// 是否处于联机活跃状态（启动中/连接中/隧道已建立）
    bool isTunnelActive() const;
    /// 当前错误信息
    QString errorMessage() const { return m_errorMessage; }

    /// 隧道服务器地址（endpoint 的主机部分）
    QString tunnelServer() const { return m_tunnelServer; }
    /// 隧道端口（endpoint 的端口部分）
    int tunnelPort() const { return m_tunnelPort; }
    /// 完整隧道地址（内核 stdout 输出的 endpoint，未建立返回空）
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
     * @brief 检查远程是否有新版本（异步，不下载）
     */
    void checkUpdate();

    /**
     * @brief 下载最新版本内核（异步）
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
    QString hongshiExecutablePath() const;  ///< hongshic(.exe)
    QString versionFilePath() const;        ///< version.txt
    QString buildStampFilePath() const;     ///< build.txt（下载接口的 ETag / Last-Modified）

    // ---- 平台与下载地址 ----
    QString platformSlug() const;           ///< windows / linux / macos，不支持时为空
    QString archSlug() const;               ///< amd64 / arm64
    QString downloadUrl() const;            ///< 官方内核下载接口完整地址

    // ---- 本地状态 ----
    void readLocalState();                  ///< 读取 version.txt / build.txt
    void writeVersionFile(const QString &version);
    void writeBuildStamp(const QString &stamp);
    QString probeKernelVersion();           ///< 运行 hongshic -v 读取版本号

    // ---- 远程查询 ----
    void applyInstallStatus();              ///< 依据本地状态刷新安装状态

    // ---- 下载安装 ----
    void startDownload();                   ///< 直接下载官方内核
    void downloadFailed(const QString &msg, const QString &taskId);

    // ---- 进程与状态 ----
    void onStdoutReady();
    void onStderrReady();
    void handleKernelOutputLine(const QString &line);
    void resetTunnelInfo();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void setState(State newState);
    void setErrorMessage(const QString &msg);

    // ---- 内部数据 ----
    State m_state = State::NotInstalled;
    InstallStatus m_installStatus = InstallStatus::NotInstalled;
    QString m_installedVersion;
    QString m_latestVersion;
    QString m_installedStamp;   ///< 本机内核对应的官方构建标识
    QString m_errorMessage;
    qreal m_downloadProgress = -1.0;

    QString m_tunnelEndpoint;   ///< 内核 stdout 输出的完整 endpoint
    QString m_tunnelServer;
    int m_tunnelPort = -1;
    QList<Node> m_nodes;

    bool m_checkingUpdate = false;
    bool m_fetchingNodes = false;
    bool m_stopping = false;

    QProcess *m_process = nullptr;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QNetworkAccessManager m_networkManager;
};
