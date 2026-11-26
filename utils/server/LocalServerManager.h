/**
 * @file   LocalServerManager.h
 * @brief  一键本地开服：为当前实例准备并运行一台本地 Minecraft 服务端
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 职责（只做「从 0 到能跑」，不做专业面板）：
 *   1. 按实例版本挑服务端核心（原版 / Paper / Fabric / Forge / NeoForge），缺东西时自动下载；
 *   2. 在 {实例}/server/ 下铺好 eula.txt 与 server.properties（保留用户改过的项）；
 *   3. 可以把实例里的某个存档复制成服务端世界，接着原来的进度一起玩；
 *   4. 用本机 Java 启动服务端，把控制台输出一行行抛给界面；
 *   5. 停止时先发 stop 走存档保存流程，超时才强杀。
 *
 * 模组实例（Fabric / Quilt / Forge / NeoForge）会把实例的 mods 目录链接给服务端，
 * 双端共用同一份模组。Forge / NeoForge 走官方安装器（--installServer），
 * 安装完用安装器生成的 @args 文件启动，不在启动器里自己拼一长串 classpath。
 */

#ifndef LOCALSERVERMANAGER_H
#define LOCALSERVERMANAGER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class LocalServerManager : public QObject
{
    Q_OBJECT

public:
    /// 服务端核心：原版 / Paper（更省资源）/ Fabric / Forge / NeoForge（走官方安装器）
    enum class Core
    {
        Vanilla = 0,
        Paper = 1,
        Fabric = 2,
        Forge = 3,
        NeoForge = 4
    };
    Q_ENUM(Core)

    /// 一键开服的生命周期状态
    enum class State
    {
        Idle = 0,      ///< 没开服
        Preparing = 1, ///< 正在准备（下载 / 安装 / 复制存档 / 写配置）
        Starting = 2,  ///< 服务端进程已拉起，等它输出 Done
        Running = 3,   ///< 可以进了
        Stopping = 4   ///< 正在停止
    };
    Q_ENUM(State)

    /// 开服参数（界面上能改的那几项）
    struct Config
    {
        QString instancePath;          ///< 实例根目录
        QString version;               ///< 游戏版本，如 "1.20.4"
        QString loader;                ///< 加载器，如 "Fabric"；空表示原版
        Core core = Core::Vanilla;     ///< 服务端核心
        int port = 25565;              ///< 服务器端口
        int maxPlayers = 10;           ///< 最大玩家数
        int memoryMb = 2048;           ///< 分配给服务端的内存（MB）
        bool onlineMode = false;       ///< 正版验证（联机大厅/局域网随便玩时关掉）
        QString motd = QStringLiteral("方块盒子本地服务器");
        /**
         * @brief 用实例里的哪个存档开局（存档目录绝对路径；空 = 新建世界）
         *
         * 只在服务端还没有世界时复制一次；复制完服务端就用自己那份 world，
         * 之后两边各玩各的（客户端那边再动不会同步过去）。
         */
        QString worldSource;
        /**
         * @brief 直接指定服务端目录（「下载服务端」用）
         *
         * 为空时跟着实例走（{实例}/server）；非空时不看 instancePath，
         * 文件就落在指定目录里。
         */
        QString serverDirOverride;
        /**
         * @brief 只把服务端文件准备好，不写 eula / 不启动（下载服务端用）
         *
         * 这个模式下不需要 Java，也不会碰实例目录。
         */
        bool downloadOnly = false;
    };

    explicit LocalServerManager(QObject *parent = nullptr);
    ~LocalServerManager() override;

    // ---- 静态描述工具 ----
    /// 核心的英文键（写配置用）：vanilla / paper / fabric / forge / neoforge
    static QString coreKey(Core core);
    /// 英文键还原成核心（配置里存的就是它）
    static Core coreFromKey(const QString &key, Core fallback = Core::Vanilla);
    /// 核心在界面上的名字
    static QString coreDisplayName(Core core);
    /// 按实例加载器给一个默认核心
    static Core defaultCoreForLoader(const QString &loader);
    /// 当前加载器能不能一键开服（Forge / NeoForge 也支持，走官方安装器）
    static bool loaderSupported(const QString &loader);
    /// 这个核心要不要跑官方安装器（Forge / NeoForge）
    static bool coreUsesInstaller(Core core);
    /// 这个核心在当前游戏版本能不能用（Forge 1.13+、NeoForge 1.20.2+）
    static bool coreAvailableForVersion(Core core, const QString &version);
    /**
     * @brief 解析 Maven 元数据里的版本列表，挑出最合适的一个
     * @param xml      maven-metadata.xml 内容
     * @param prefix   只认这个前缀的版本（Forge 用 "1.20.1-"，NeoForge 用 "21.1."）
     * @param best     找到的版本（去掉前缀后的部分也放进 candidates）
     * @return 找到返回 true
     *
     * Forge / NeoForge 的版本号都带 "-beta" 之类的预发布后缀，稳定版优先；
     * 同一版本段里取数字最大的那个。
     */
    static bool pickMavenVersion(const QByteArray &xml, const QString &prefix, QString *best);

    // ---- 实例上下文 ----
    /**
     * @brief 注入实例上下文
     *
     * 只有换了实例才会重置状态；同一个实例重复注入（窗口每次打开都会调一次）
     * 不会打断正在运行的服务器。
     */
    void setInstance(const QString &path, const QString &version, const QString &loader);
    QString instancePath() const { return m_config.instancePath; }
    QString version() const { return m_config.version; }
    QString loader() const { return m_config.loader; }

    // ---- 参数 ----
    Config config() const { return m_config; }
    /**
     * @brief 换一套开服参数（界面改动时调用）
     *
     * 没开服时端口跟着一起换，保证界面上显示的地址、写进 server.properties 的端口
     * 与实际监听的一致；开服期间不允许改，改了也不生效。
     */
    void setConfig(const Config &config);
    void setPort(int port) { m_config.port = port; if (!isRunning() && !isBusy()) m_activePort = port; }

    // ---- 路径 ----
    /// 服务端工作目录：{实例}/server
    QString serverDir() const;
    /// 当前核心的 jar 路径（Forge / NeoForge 用安装器，返回安装器 jar）
    QString jarPath() const;
    /// 服务端是否已经准备好（jar 下好了 / Forge 装好了）
    bool jarReady() const;
    /// 服务端世界目录：{实例}/server/world
    QString serverWorldDir() const;
    /// 服务端世界是从哪个存档复制来的（复制时写下的标记，没有则空）
    QString serverWorldSource() const;
    /**
     * @brief 删掉服务端现有的 world（用别的存档覆盖之前调）
     *
     * 只删 {实例}/server/world，不碰实例自己的 saves。
     */
    bool resetServerWorld(QString *error);

    // ---- 运行状态 ----
    State state() const { return m_state; }
    bool isRunning() const { return m_state == State::Running; }
    /// 正在准备 / 启动 / 停止（界面据此禁用按钮）
    bool isBusy() const;
    /// 实际使用的端口（默认端口被占用时会顺延）
    int port() const { return m_activePort > 0 ? m_activePort : m_config.port; }
    /// 本机玩家要填的地址：127.0.0.1:端口
    QString address() const;
    QString lastError() const { return m_lastError; }
    /// 正在准备时的说明文字（进度条用），没在准备时为空
    QString progressText() const { return m_progressText; }

    /**
     * @brief 把一个存档目录整份复制成服务端世界（同步，跑在工作线程里）
     *
     * 会跳过 session.lock（客户端锁文件，带过去服务端会以为世界被占用），
     * 进度按已复制字节 / 总字节回调；cancel 置位时提前收工。
     */
    static bool copyWorldTree(const QString &sourceDir, const QString &targetDir,
                              const std::function<void(qint64 done, qint64 total)> &onProgress,
                              const std::atomic_bool *cancel, QString *error);

public slots:
    /// 一键开服：准备（必要时下载 / 安装 / 复制存档）→ 启动
    void start();
    /// 停止服务器：先发 stop 让服务端存盘，超时才强杀
    void stop();
    /**
     * @brief 把一条服务端命令发到控制台（等同于在服务端窗口里打字回车）
     *
     * 常用：`op 玩家名`、`whitelist add 玩家名`、`say 内容`、`stop`。
     * 没在运行时不发，返回 false。
     */
    bool sendCommand(const QString &command);

signals:
    void stateChanged(LocalServerManager::State state);
    /// 服务端控制台输出（已去掉颜色码与空行）
    void logLine(const QString &line);
    /// 准备阶段的进度说明，如 "正在下载服务端 45%"
    void progressChanged(const QString &text, int percent);
    /// 服务端打印 Done(...)，可以进游戏了
    void ready(const QString &address);
    /// 开服失败（下载失败 / 没 Java / 端口占用 / 启动报错），reason 可直接给用户看
    void failed(const QString &reason);
    /// 运行中的服务端异常退出（崩溃 / 被外部结束），界面好给个明确提示
    void crashed(const QString &reason);
    /**
     * @brief 「只下载」模式下文件准备好了
     * @param path         下载到的文件（原版 / Paper / Fabric 是服务端 jar，Forge / NeoForge 是安装器）
     * @param needsInstall true 表示这是安装器，还要跑一次 --installServer 才能开服
     */
    void downloadFinished(const QString &path, bool needsInstall);

private:
    // ---- 准备阶段 ----
    void setState(State state);
    void failWith(const QString &reason);
    void log(const QString &line);
    /// 端口占用时顺延到一个空闲端口
    int pickFreePort(int preferred);
    /// 写 eula.txt 与 server.properties（已存在的键保留用户的值）
    bool writeConfigs(QString *error);
    /// 模组实例：把实例 mods 目录链接到服务端目录
    void prepareModsDir();
    /// 找一台满足版本要求的 Java；拷到 *path 返回 true
    bool resolveJava(QString *path, QString *error);

    // ---- 世界准备（用实例存档开局） ----
    /// 这次开服要不要先把存档复制过来
    bool needsWorldCopy() const;
    /// 启动异步复制，完成后继续下载 / 安装
    void beginWorldCopy();
    void onWorldCopyFinished();

    // ---- 下载 / 安装阶段 ----
    /// 准备服务端本体：普通核心下 jar，Forge / NeoForge 跑安装器
    void ensureServerProvided();
    void ensureJar();
    void fetchVanillaServer();
    void fetchPaperServer();
    void fetchFabricServer();
    /// 取 JSON（按候选顺序回退），成功回调 onOk
    void fetchJson(const QStringList &urls, int index,
                   const std::function<void(bool ok, const QByteArray &data)> &onDone);
    /// 下载文件到 jar 路径（.part 中转），成功回调 onOk
    void fetchFile(const QStringList &urls, int index,
                   const std::function<void(bool ok)> &onDone);
    void reportDownloadProgress(qint64 received, qint64 total);
    void onJarReady();

    // ---- Forge / NeoForge 安装 ----
    void ensureInstallerServer();
    void fetchInstallerVersion();
    void downloadInstaller(const QString &version);
    void runInstaller(const QString &installerPath, const QString &version);
    void onInstallerFinished(int exitCode, QProcess::ExitStatus status);
    /// 找安装器生成的启动配置：@args 文件或 forge-*.jar
    bool locateInstallerLauncher(QString *argsFile, QString *launcherJar) const;
    /**
     * @brief 候选的 Forge / NeoForge 版本号
     *
     * 本次刚解析出来的 + 上次装好时写进标记文件的（软件重开后再开服用得上）。
     */
    QStringList installerVersionCandidates() const;
    /// 装好之后写的标记文件（记核心与版本，重开软件也认得出）
    QString readyMarkerPath() const;
    QString installerJarPath() const;
    QString installerVersion() const;
    QStringList buildLaunchArgs() const;

    // ---- 启动阶段 ----
    void launchProcess(const QString &javaPath);
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    /// 从服务端输出里识别「端口被占用 / EULA / 世界损坏」这类常见故障
    QString friendlyLaunchError(const QString &line) const;

    Config m_config;
    State m_state = State::Idle;
    QString m_lastError;
    QString m_progressText;
    int m_progressPercent = -1;
    int m_activePort = 25565;

    QString m_javaPath;      ///< 本次使用的 java 可执行文件

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_activeReply = nullptr;   ///< 当前在途请求（取消时统一 abort）
    bool m_jarDownloading = false;
    /**
     * @brief 本次准备已作废（失败 / 取消）
     *
     * abort() 会让在途请求立刻走到 finished 回调，如果不挡住，失败之后
     * 回调会继续试下一个镜像源、甚至又拉起一次下载。
     */
    bool m_aborted = false;

    // ---- Forge / NeoForge 安装状态 ----
    QString m_installerVersion;   ///< 解析出来的 Forge / NeoForge 版本号
    QProcess *m_installerProcess = nullptr;
    QString m_installerOutput;    ///< 安装器输出尾部（失败时给用户看）

    // ---- 世界复制 ----
    std::shared_ptr<std::atomic_bool> m_worldCopyCancel;
    std::shared_ptr<std::atomic<qint64>> m_worldCopyDone;
    std::shared_ptr<std::atomic<qint64>> m_worldCopyTotal;
    std::shared_ptr<std::atomic_bool> m_worldCopyOk;
    std::shared_ptr<QString> m_worldCopyError;
    QFutureWatcher<void> *m_worldCopyWatcher = nullptr;
    QTimer *m_worldCopyTimer = nullptr;

    QProcess *m_process = nullptr;
    QString m_outputBuffer;   ///< 半行缓存（QProcess 输出按块到达）
    QElapsedTimer m_startTimer;
    QString m_pendingAddress;

    QTimer *m_stopTimer = nullptr;   ///< 停止超时：到点强杀
    /**
     * @brief 准备阶段看门狗
     *
     * 国内访问 Paper / Mojang 的下载 CDN 时常出现「连不上也不报错」的挂起，
     * QNetworkAccessManager 的传输超时只管有数据之后的空闲，管不了建连卡死，
     * 所以整段准备另设一个总超时，到点给用户一个明确结论。
     */
    QTimer *m_prepareTimer = nullptr;
};

#endif // LOCALSERVERMANAGER_H
