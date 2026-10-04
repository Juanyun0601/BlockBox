/**
 * @file   TerracottaClient.h
 * @brief  陶瓦联机客户端 - 管理 Terracotta 进程与 HTTP API 通信
 * @author BlockBox Team
 * @date   2026-07-07
 *
 * 参考 HMCL TerracottaManager 实现：
 *   1. 启动 Terracotta 可执行文件（--hmcl 模式）
 *   2. 通过临时文件读取 HTTP 服务端口
 *   3. 轮询 /state/ 获取状态，调用 /state/scanning、/state/guesting、/state/ide
 */

#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

/**
 * @brief 陶瓦联机客户端单例
 *
 * 负责与 Terracotta 后端进程交互，状态机遵循 Terracotta API：
 *   waiting → host-scanning → host-starting → host-ok
 *   waiting → guest-connecting → guest-starting → guest-ok
 *   任意状态 → exception / waiting（用户取消）
 */
class TerracottaClient : public QObject
{
    Q_OBJECT

public:
    /// 联机客户端状态（对应 Terracotta /state 返回的 state 字段）
    enum class State
    {
        NotInstalled,    ///< 未安装 Terracotta 可执行文件
        Launching,       ///< 正在启动 Terracotta 进程
        Waiting,         ///< 空闲，可创建/加入房间
        HostScanning,    ///< 房主：扫描本地 MC 端口
        HostStarting,    ///< 房主：正在启动房间
        HostOk,          ///< 房主：房间已就绪
        GuestConnecting, ///< 房客：正在连接房主
        GuestStarting,   ///< 房客：正在启动端口转发
        GuestOk,         ///< 房客：已连接，可以进入服务器
        Exception,       ///< 异常状态
        Fatal            ///< 致命错误（进程崩溃等）
    };
    Q_ENUM(State)

    /// 玩家资料（对应 Terracotta profiles 数组项）
    struct Profile
    {
        QString machineId; ///< 机器标识
        QString name;      ///< 玩家名
        QString vendor;    ///< 客户端标识
        QString kind;      ///< "HOST" / "LOCAL" / "GUEST"

        bool operator==(const Profile &other) const
        {
            return machineId == other.machineId && name == other.name
                   && vendor == other.vendor && kind == other.kind;
        }
        bool operator!=(const Profile &other) const { return !(*this == other); }
    };

    /// 异常类型（对应 Terracotta exception.type）
    enum class ExceptionType
    {
        PingHostFail = 0,            ///< 无法连接房主
        PingHostRst = 1,             ///< 连接被房主重置
        GuestEasytierCrash = 2,      ///< 房客 EasyTier 崩溃
        HostEasytierCrash = 3,       ///< 房主 EasyTier 崩溃
        PingServerRst = 4,           ///< Minecraft 服务器无响应
        ScaffoldingInvalidResponse = 5 ///< 联机协议响应无效
    };
    Q_ENUM(ExceptionType)

    /// 安装状态
    enum class InstallStatus
    {
        NotInstalled,     ///< 未安装
        Ready,            ///< 已安装可用
        CheckingUpdate,   ///< 正在检查更新
        UpdateAvailable,  ///< 有新版本可更新
        UpToDate,         ///< 已是最新版本
        Downloading,      ///< 正在下载
        DownloadFailed    ///< 下载失败
    };
    Q_ENUM(InstallStatus)

    /**
     * @brief 获取单例
     */
    static TerracottaClient *instance();

    /// 当前状态
    State state() const { return m_state; }
    /// 当前房间邀请码（仅 HostOk / Guest* 状态有效）
    QString roomCode() const { return m_roomCode; }
    /// 房客连接服务器地址（仅 GuestOk 状态有效，如 "127.0.0.1" 或 "127.0.0.1:25565"）
    QString serverUrl() const { return m_serverUrl; }
    /// 当前玩家列表
    QList<Profile> profiles() const { return m_profiles; }
    /// 当前异常类型（仅 Exception 状态有效）
    ExceptionType exceptionType() const { return m_exceptionType; }
    /// 错误信息
    QString errorMessage() const { return m_errorMessage; }
    /// 当前 Terracotta 版本（启动后通过 /meta 获取）
    QString terracottaVersion() const { return m_terracottaVersion; }
    /// 已安装版本号（未安装返回空）
    QString installedVersion() const { return m_installedVersion; }
    /// 远程最新版本号（未检测返回空）
    QString latestVersion() const { return m_latestVersion; }
    /// 安装状态
    InstallStatus installStatus() const { return m_installStatus; }
    /// 下载进度（0.0~1.0，-1 表示无下载任务）
    qreal downloadProgress() const { return m_downloadProgress; }
    /// 是否已启动 Terracotta 进程
    bool isProcessRunning() const { return m_process != nullptr && m_process->state() != QProcess::NotRunning; }
    /// 是否处于联机房间中（HostOk / GuestOk / 各种过渡状态）
    bool isInRoom() const;

    /**
     * @brief 启动 Terracotta 后台进程（若已安装且未运行）
     *
     * 流程：拉起进程 → 等待端口文件 → 读取端口 → 进入 Launching → 轮询 /state
     */
    void start();

    /**
     * @brief 创建房间（房主模式）
     * @param playerName 房主玩家名（可选，空则使用 "BlockBox 玩家"）
     */
    void createRoom(const QString &playerName);

    /**
     * @brief 加入房间（房客模式）
     * @param roomCode 邀请码（格式 U/XXXX-XXXX-XXXX-XXXX）
     * @param playerName 玩家名（可选）
     * @return 是否成功提交加入请求（房间码格式校验通过且当前处于 Waiting 状态）
     */
    bool joinRoom(const QString &roomCode, const QString &playerName);

    /**
     * @brief 离开房间/取消操作（回到 Waiting 状态）
     */
    void leaveRoom();

    /**
     * @brief 检查并下载最新版本 Terracotta（异步）
     *
     * 调用 GitHub Releases API 获取最新版本号，若比本地新则下载并解压安装。
     */
    void downloadLatest();

    /**
     * @brief 检查远程最新版本（异步，不下载）
     *
     * 从 GitHub Releases API 获取最新版本号，与本地比较后更新 InstallStatus。
     */
    void checkUpdate();

    /**
     * @brief 校验邀请码格式
     * @param code 邀请码字符串
     * @return 是否符合 U/XXXX-XXXX-XXXX-XXXX 格式且校验位正确
     *
     * 参考 Terracotta src/controller/rooms/scaffolding/room.rs::parse
     */
    static bool verifyRoomCode(const QString &code);

signals:
    /// 状态变化
    void stateChanged();
    /// 玩家列表变化
    void profilesChanged();
    /// 错误发生
    void errorOccurred(const QString &error);
    /// 安装状态变化
    void installStatusChanged();
    /// 下载进度变化
    void downloadProgressChanged(qreal progress);

private:
    explicit TerracottaClient(QObject *parent = nullptr);
    ~TerracottaClient() override;
    Q_DISABLE_COPY_MOVE(TerracottaClient)

    // ---- 路径与版本管理 ----
    QString terracottaRootDir() const;       ///< 返回 <AppData>/BlockBox/terracotta/
    QString terracottaExecutablePath() const; ///< 当前版本可执行文件完整路径
    QString latestLocalVersion() const;       ///< 本地最新版本号（扫描目录）
    void scanInstalledVersion();              ///< 扫描本地已安装版本

    // ---- 进程管理 ----
    void launchProcess();                    ///< 拉起 Terracotta 进程
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void tryReadPortFile();                  ///< 尝试读取端口文件

    // ---- HTTP API 调用 ----
    void fetchState();                       ///< 轮询 /state/
    void handleStateResponse(const QByteArray &data);
    void callApi(const QString &path);       ///< GET 调用 API（如 /state/ide、/state/scanning?...）

    void fetchMetadata();                    ///< 获取 /meta 信息

    // ---- 节点列表 ----
    void fetchPublicNodes();                 ///< 获取公共节点列表

    // ---- 状态切换 ----
    void setState(State newState);
    void setErrorMessage(const QString &msg);

    // ---- 下载安装 ----
    void loadBuiltinVersionInfo();           ///< 从内置资源读取版本配置（无需网络）
    QStringList buildDownloadUrls(const QString &version); ///< 构建多镜像下载源列表
    void downloadRelease(const QString &version);
    void extractAndInstall(const QString &archivePath, const QString &version, const QString &taskId);

    // ---- 内部数据 ----
    State m_state = State::NotInstalled;
    QString m_roomCode;
    QString m_serverUrl;
    QList<Profile> m_profiles;
    ExceptionType m_exceptionType = ExceptionType::PingHostFail;
    QString m_errorMessage;
    QString m_terracottaVersion;
    QString m_installedVersion;
    QString m_latestVersion;                ///< 远程最新版本号（从 GitHub API 获取）
    InstallStatus m_installStatus = InstallStatus::NotInstalled;
    qreal m_downloadProgress = -1.0;

    int m_serverPort = 0;             ///< Terracotta HTTP API 端口
    int m_lastStateIndex = -1;        ///< 上次轮询到的状态索引（用于去重）
    bool m_isFakeScanning = false;    ///< 本地假状态标志（参考 HMCL HostScanning(-1,-1,null)）

    QProcess *m_process = nullptr;
    QString m_portFilePath;           ///< 端口文件路径（传递给 Terracotta --hmcl 参数）
    QNetworkAccessManager m_networkManager;
    QTimer m_pollTimer;               ///< 状态轮询定时器
    QTimer m_portFileTimer;           ///< 端口文件检测定时器
    QList<QString> m_publicNodes;     ///< 缓存的公共节点 URL 列表
    bool m_nodesFetched = false;
    int m_consecutiveErrors = 0;      ///< HTTP 轮询连续失败计数（用于检测 daemon 死亡）
};

