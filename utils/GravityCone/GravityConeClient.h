/**
 * @file   GravityConeClient.h
 * @brief  基岩版联机客户端 - 管理 GravityCone CLI 进程与 JSON stdio 协议通信
 * @author BlockBox Team
 *
 * 基岩版实例助手的联机功能由 GravityCone（开源项目，Tianpao/GravityCone）提供。
 * 本类负责：
 *   1. 下载/安装/更新 GravityCone CLI 可执行文件
 *   2. 以 stdin/stdout JSON 行协议拉起 gravitycone-cli 进程
 *   3. 调用 room.* / system.* 接口实现建房 / 加房 / 离开 / 状态轮询
 *
 * 参考 GravityCone cli/protocol.go（请求/响应/事件结构）与
 * cli/handler_room.go（房间接口参数与返回）实现。
 */

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class GravityConeClient : public QObject
{
    Q_OBJECT

public:
    /// 联机客户端状态
    enum class State
    {
        NotInstalled,   ///< 未安装 GravityCone / 进程未运行
        Launching,      ///< 正在启动 CLI 进程
        Waiting,        ///< 空闲，可创建/加入房间
        HostStarting,   ///< 房主：正在创建房间
        HostOk,         ///< 房主：房间已就绪（可分享邀请码）
        GuestStarting,  ///< 房客：正在加入房间
        GuestOk,        ///< 房客：已加入，可进入游戏连接
        Exception,      ///< 接口返回错误
        Fatal           ///< 致命错误（进程崩溃等）
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

    /// 房间内玩家
    struct Player
    {
        QString name;          ///< 玩家名（协议字段 player）
        QString clientId;      ///< 客户端标识
        bool isRoomHost = false; ///< 是否房主

        bool operator==(const Player &other) const
        {
            return name == other.name && clientId == other.clientId
                   && isRoomHost == other.isRoomHost;
        }
        bool operator!=(const Player &other) const { return !(*this == other); }
    };

    /**
     * @brief 获取单例
     */
    static GravityConeClient *instance();

    State state() const { return m_state; }
    InstallStatus installStatus() const { return m_installStatus; }
    /// 当前角色：host / guest / 空（未在房间）
    QString role() const { return m_role; }
    /// 房间邀请码（HostOk / GuestOk 状态有效）
    QString roomCode() const { return m_roomCode; }
    /// 房客连接服务器地址（GuestOk 状态有效，如 "127.0.0.1"）
    QString serverAddress() const { return m_serverAddress; }
    /// 游戏端口（房主：对外端口；房客：本地转发端口）
    quint16 gamePort() const { return m_gamePort; }
    /// 在线人数
    int onlineCount() const { return m_onlineCount; }
    /// 房间内玩家列表
    QList<Player> players() const { return m_players; }
    /// 错误码（仅 Exception 状态有效）
    QString errorCode() const { return m_errorCode; }
    /// 错误信息
    QString errorMessage() const { return m_errorMessage; }
    /// 最近一次加入房间的进度步骤描述
    QString progressMessage() const { return m_progressMessage; }
    /// 启动后从 system.ready 事件获取的 CLI 版本
    QString gravityConeVersion() const { return m_gravityConeVersion; }
    /// 已安装版本号（未安装返回空）
    QString installedVersion() const { return m_installedVersion; }
    /// 远程最新版本号（未检测返回空）
    QString latestVersion() const { return m_latestVersion; }
    /// 下载进度（0.0~1.0，-1 表示无下载任务）
    qreal downloadProgress() const { return m_downloadProgress; }
    /// 是否已启动 CLI 进程
    bool isProcessRunning() const
    {
        return m_process != nullptr && m_process->state() != QProcess::NotRunning;
    }
    /// 是否处于房间中（HostOk / GuestOk / 各过渡状态）
    bool isInRoom() const;

    /**
     * @brief 启动 GravityCone CLI 后台进程（若已安装且未运行）
     *
     * 流程：拉起进程 → 等待 system.ready 事件 → 进入 Waiting。
     */
    void start();

    /**
     * @brief 停止 GravityCone 进程（发送 system.shutdown 并终止进程）
     */
    void stop();

    /**
     * @brief 创建房间（基岩版 paperconnect 协议）
     * @param playerName 房主玩家名（可空，使用默认名）
     */
    void createRoom(const QString &playerName);

    /**
     * @brief 加入房间（自动识别 paperconnect / scaffolding 协议）
     * @param code       邀请码（P/XXXX-XXXX-XXXX-XXXX 或 U/...）
     * @param playerName 玩家名（可空）
     */
    void joinRoom(const QString &code, const QString &playerName);

    /**
     * @brief 离开当前房间 / 取消操作（回到 Waiting 状态）
     */
    void leaveRoom();

    /// 取消正在进行的加入操作
    void cancelJoin();

    /**
     * @brief 确认基岩版已退出（释放 NetherNet UDP 端口后重新绑定），
     *        供房客端口被占用时调用
     */
    void confirmMinecraftEnded();

    /// 手动刷新一次房间状态
    void refreshStatus();

    /**
     * @brief 检查并下载最新版本 GravityCone（异步）
     *
     * 使用 GitHub Releases 发布产物 gravitycone-cli-{os}-{arch}.zip。
     */
    void downloadLatest();

    /**
     * @brief 检查远程最新版本（异步，不下载）
     */
    void checkUpdate();

    /**
     * @brief 校验基岩版邀请码格式（P/XXXX-XXXX-XXXX-XXXX）
     *
     * 校验 S 部分（后 8 字符）小端 base-34 值可被 7 整除，
     * 参考 GravityCone core/protocol/paperconnect/roomcode.go::ParsePaperConnectRoomCode。
     * 同时兼容不带 P/ 前缀的输入（自动补齐）。
     */
    static bool verifyRoomCode(const QString &code);

signals:
    /// 状态变化
    void stateChanged();
    /// 玩家列表变化
    void playersChanged();
    /// 错误发生
    void errorOccurred(const QString &error);
    /// 安装状态变化
    void installStatusChanged();
    /// 下载进度变化
    void downloadProgressChanged(qreal progress);
    /// 加入房间进度消息（step, message）
    void progressUpdated(const QString &step, const QString &message);

private:
    explicit GravityConeClient(QObject *parent = nullptr);
    ~GravityConeClient() override;
    Q_DISABLE_COPY_MOVE(GravityConeClient)

    // ---- 路径与版本管理 ----
    QString gravityConeRootDir() const;         ///< <应用目录>/gravitycone/
    QString gravityConeExecutablePath() const;  ///< 当前版本可执行文件完整路径
    QString latestLocalVersion() const;         ///< 本地最新版本号（扫描目录）
    void scanInstalledVersion();
    void loadBuiltinVersionInfo();              ///< 从内置资源读取版本配置（无需网络）

    // ---- 进程管理 ----
    void launchProcess();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onStdoutReady();

    // ---- JSON stdio 协议 ----
    void sendRequest(const QString &method, const QJsonObject &params, const QString &reqKind);
    void handleLine(const QByteArray &line);
    void handleResponse(const QJsonObject &obj);
    void handleEvent(const QJsonObject &obj);
    void handleRoomStatusData(const QJsonObject &data);
    void applyJoinSuccess(const QJsonObject &data);
    void applyPlayers(const QJsonValue &playersVal);
    void resetRoomData();
    void setState(State newState);
    void setErrorMessage(const QString &msg);

    // ---- 下载安装 ----
    QStringList buildDownloadUrls(const QString &version);
    void downloadRelease(const QString &version);
    void extractAndInstall(const QString &archivePath, const QString &version, const QString &taskId);

    // ---- 内部数据 ----
    State m_state = State::NotInstalled;
    InstallStatus m_installStatus = InstallStatus::NotInstalled;
    QString m_role;                    ///< 当前角色（host/guest）
    QString m_roomCode;
    QString m_serverAddress;
    quint16 m_gamePort = 0;
    int m_onlineCount = 0;
    QList<Player> m_players;
    QString m_errorCode;
    QString m_errorMessage;
    QString m_progressMessage;
    QString m_gravityConeVersion;
    QString m_installedVersion;
    QString m_latestVersion;
    qreal m_downloadProgress = -1.0;

    QProcess *m_process = nullptr;
    QByteArray m_stdinBuffer;                       ///< stdout 行缓冲
    QNetworkAccessManager m_networkManager;
    QTimer m_statusTimer;                           ///< 房间状态轮询定时器

    int m_nextRequestId = 1;                        ///< 自增请求 ID
    QHash<int, QString> m_pendingRequests;          ///< 请求 ID -> 请求类型
    bool m_waitingForReady = false;
};
