/**
 * @file   GameLauncher.h
 * @brief  游戏启动器类定义
 * @author BlockBox Team
 * @date   2026-05-09
 */

#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMutex>
#include <QQueue>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QDateTime>
#include <QPair>
#include <QVector>
#include <functional>

class ErrorAnalyzer;
class JavaScanWorker;
class QThread;
struct JavaBinaryInfo;

/**
 * @brief 文件补全优先级（参考 HMCL/Prism Launcher 的任务优先级设计）
 */
enum class CompletionPriority
{
    High = 0,       // 客户端 JAR（必须最先就绪）
    Normal = 1,     // 资产索引 JSON、库文件
    Low = 2,        // 资源文件（assets，可延迟下载）
    Background = 3  // 非关键资源
};

/**
 * @brief 主机连接状态跟踪（参考 Prism Launcher 的 NetHost 设计）
 */
struct HostConnectionState
{
    int activeCount = 0;        // 当前活跃连接数
    int maxConcurrent = 6;      // 该主机最大并发数
    int successCount = 0;       // 成功次数
    int failCount = 0;          // 失败次数
    double avgLatencyMs = 0.0;  // 平均延迟
    QDateTime lastFailureTime;
};

/**
 * @brief 文件补全任务
 */
struct FileCompletionTask
{
    QString url;
    QString filePath;
    qint64 fileSize = 0;
    QString expectedHash;         // SHA1 验证哈希
    QString description;          // 人类可读描述
    int retryCount = 0;
    CompletionPriority priority = CompletionPriority::Normal;
    int timeoutMs = 60000;         // 超时时间（根据文件大小自动调整）
};

class GameLauncher : public QObject
{
    Q_OBJECT

public:
    enum LaunchStatus {
        Idle,
        Launching,
        Running,
        Failed,
        Stopped
    };

    struct LaunchConfig {
        QString instancePath;
        QString javaPath;
        QString accountName = "Player";
        int maxMemory = 4096; // MB
        int minMemory = 1024; // MB
        QStringList jvmArgs;
        QStringList gameArgs;
        QString windowWidth = "1280";
        QString windowHeight = "720";
        bool fullscreen = false;
    };

    struct JavaInfo {
        QString path;
        QString version;
        bool valid = false;
    };

    static GameLauncher* instance();
    ~GameLauncher();

    LaunchStatus status() const;
    QString errorMessage() const;

    // Java related methods
    JavaInfo detectJava();
    QList<JavaInfo> findAllJavaInstallations();
    // 带参版本：缓存由调用方在主线程读取，persistToCache=false 时不在本函数内写回设置，
    // 供后台线程调用（避免跨线程访问同一份 QSettings）
    QList<JavaInfo> findAllJavaInstallations(const QList<QPair<QString, QString>>& cachedInstallations,
                                             bool persistToCache);
    // 只读取已保存的 Java 缓存（不启动 java -version、不扫描磁盘），
    // 用于界面构造期填充列表，避免阻塞主线程
    QList<JavaInfo> cachedJavaInstallations() const;
    // 缓存为空时在后台补扫 Java：扫描在工作线程完成，结果先在主线程写回缓存，
    // 再通过 onFinished 回调（仍在主线程），供界面填充下拉列表使用
    void scanJavaInBackground(QObject* context,
                              std::function<void(const QList<JavaInfo>&)> onFinished);
    QList<JavaInfo> findJavaOnSystem();
    void initJavaCache();
    void searchJavaInDirectory(const QString& dirPath, QList<JavaInfo>& javaList,
                               QSet<QString>& seenPaths, int depth, int maxDepth);
    bool validateJavaPath(const QString& path, JavaInfo& info);

    // 游戏版本 → 所需 Java 主版本区间（minJava=0 表示无下限，maxJava=999 表示无上限）
    struct JavaRequirement
    {
        int minJava = 0;
        int maxJava = 999;
        QString rangeDesc() const
        {
            return (maxJava >= 999) ? QStringLiteral("Java %1+").arg(minJava)
                                    : QStringLiteral("Java %1-%2").arg(minJava).arg(maxJava);
        }
    };
    // 按版本号解析 Java 要求：支持旧规则 1.x、2026 起新规则 26.x（含
    // 26.4-snapshot-5 这类快照/预发布版）以及旧快照 23w14a；
    // jsonMajorHint>0 时以版本 JSON 里 javaVersion.majorVersion（官方声明）为准，
    // 比按版本号硬编码更可靠
    static JavaRequirement javaRequirementFor(const QString& gameVersion, int jsonMajorHint = 0);
    JavaInfo findBestJavaVersion(const QString& gameVersion, int jsonMajorHint = 0);

    // 读 java.exe 同目录 server/jvm.dll 的 PE 头判断 JVM 位数：64 / 32；
    // 读不到（例如 Oracle javapath 这类转发存根、或只给了 "java"）返回 0
    static int javaBitness(const QString& javaExe);

    // 从 Java 版本字符串提取主版本号（"1.8.0_301"→8，"17.0.1"→17，"21"→21）
    static int javaMajorOf(const QString& version);

    // Launch related methods
    // skipFileCompletion=true: 跳过文件完整性检查与补全（首次启动）
    // skipFileCompletion=false: 执行文件完整性检查并补全缺失文件（启动失败重试时使用）
    bool launchGame(const LaunchConfig& config, bool skipFileCompletion = true);
    void stopGame();
    bool isGameRunning();

    // 判断上一次启动是否已成功出现游戏窗口（用于区分启动期崩溃与运行期崩溃）
    bool wasGameStartedSuccessfully() const;

    // 判断上一次进程结束是否由用户手动取消触发（用于在 UI 层区分"用户取消"与"游戏自然退出"）
    bool wasUserCancelled() const;

    // 判断上一次启动是否触发过「Java 缺失自救」（全盘查找 + 必要时自动安装），
    // 供 UI 层区分：自救失败后无需再做文件补全重启
    bool wasJavaRescueAttempted() const;

    // 获取当前启动配置中的实例路径
    QString currentInstancePath() const;
    
    // Account related methods
    bool checkAccountStatus();
    bool refreshMicrosoftToken(const QString &refreshToken);

signals:
    void launchStatusChanged(LaunchStatus status);
    void gameStarted();
    void gameStopped(int exitCode);
    void gameCrashed(const QString& error);
    void launchProgressChanged(int progress, const QString& message);
    void launchDetailAdded(const QString& detail);
    void errorReportGenerated(const QString& report);
    void fileCompletionFinished(bool success);
    void fileCompletionProgress(int completed, int total, const QString& currentFile);

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onProcessOutput();
    void onCompletionFileFinished();

    // ── Java 缺失自救（先全盘查找，仍没有合适的 Java 则自动安装后继续启动）──
    void onRescueScanProgress(const QString& currentPath, int percent, int etaMinutes,
                              int scannedMB, int totalMB, int foundCount);
    void onRescueScanFinished(const QList<QPair<QString, QString>>& installations);
    void onRescueFetchError(const QString& error);
    void onRescueDownloadProgress(const QString& fileName, int percent);
    void onRescueDownloadBytesProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onRescueDownloadCompleted(const QString& filePath);
    void onRescueDownloadFailed(const QString& error);
    void onRescueExtractProgress(int percent);
    void onRescueExtractCompleted(const QString& javaPath);
    void onRescueExtractFailed(const QString& error);

private:
    GameLauncher(QObject *parent = nullptr);
    static GameLauncher* m_instance;
    static QMutex m_instanceMutex;

    QProcess* m_gameProcess;
    LaunchStatus m_status;
    QString m_errorMessage;
    LaunchConfig m_currentConfig;
    ErrorAnalyzer* m_errorAnalyzer;
    QString m_processOutput;
    QNetworkAccessManager* m_networkManager;
    QTimer* m_windowCheckTimer;
    int m_windowWaitElapsedMs = 0;         // 已等待游戏窗口/启动确认的累计毫秒数（每次启动重置）
    bool m_gameStartedSuccessfully = false; // 标记游戏窗口是否已成功出现（用于区分启动期崩溃）
    bool m_userCancelled = false;          // 标记本次进程结束是否由用户手动取消触发（区分"用户取消"与"游戏自然退出"）

    // 缓存: 避免启动流程中重复读取/合并 JSON
    QJsonObject m_cachedMergedJson;
    QString m_cachedGameDir;
    QString m_cachedBasePath;

    // 文件补全队列（异步并行下载，按优先级排序）
    QList<FileCompletionTask> m_fileCompletionQueue;
    QList<QNetworkReply*> m_activeCompletionReplies;
    int m_completionTotal = 0;
    int m_completionCompleted = 0;
    int m_completionFailed = 0;
    int m_completionExisted = 0;
    int m_completionActivePriority = 0; // 当前活跃的最高优先级

    // 并发控制（参考 Prism Launcher 的 NetAction 系统）
    static const int MAX_COMPLETION_CONCURRENT = 12;
    static const int MAX_ASSET_CONCURRENT = 6;
    int m_dynamicMaxConcurrent = MAX_COMPLETION_CONCURRENT; // 自适应并发上限
    int m_consecutiveErrors = 0;                             // 连续错误计数

    // 按主机跟踪连接状态
    QMap<QString, HostConnectionState> m_hostStates;

    // 下载速度与 ETA 跟踪
    QElapsedTimer m_downloadSpeedTimer;
    qint64 m_downloadBytesSinceLastMeasure = 0;
    double m_currentDownloadSpeedBps = 0.0;   // 当前下载速度（字节/秒）
    double m_averageDownloadSpeedBps = 0.0;   // 平均下载速度
    int m_speedMeasureCount = 0;

    // 进度更新节流（避免过度发射信号）
    QElapsedTimer m_lastProgressEmit;
    static const int PROGRESS_EMIT_INTERVAL_MS = 200;

    // 镜像健康状态（避免频繁切换到不可用镜像）
    QMap<QString, bool> m_mirrorHealth;
    QMap<QString, QDateTime> m_mirrorCooldownUntil;

    // ── Java 缺失自救状态 ──
    enum class RescueStage { None, Scanning, Installing };
    RescueStage m_rescueStage = RescueStage::None;
    bool m_javaRescueAttempted = false;      // 本次启动是否触发过自救（UI 层用于跳过补文件重启）
    bool m_rescueInstallAttempted = false;   // 自动安装只尝试一次，防来回打转
    bool m_rescueOfficialMirrorRetried = false; // TUNA 镜像失败后是否已换官方源重试
    LaunchConfig m_rescueConfig;
    QString m_rescueGameVersion;
    int m_rescueJsonJavaMajor = 0;
    int m_rescueSelectedMajor = 0;           // 本次自动安装的 Java 主版本
    int m_rescueGeneration = 0;              // 自救代数（防止超时定时器误杀新一轮扫描）
    int m_rescueLastProgressBucket = -1;     // 进度回显节流（按 10% 粒度）
    JavaScanWorker* m_rescueScanWorker = nullptr;
    QVector<QMetaObject::Connection> m_rescueDownloaderConnections;

    // Private methods
    QJsonObject readVersionJson(const QString& instancePath);
    QJsonObject mergeInheritsFromJson(const QString& instancePath, const QJsonObject& versionJson);
    QString resolveVersionId(const QString& instancePath);
    QString resolveJarName(const QString& instancePath, const QJsonObject& versionJson);
    QStringList buildClasspath(const QString& instancePath, const QJsonObject& versionJson);
    QStringList buildLaunchCommand(const LaunchConfig& config);
    bool prepareLaunchEnvironment();
    void cleanup();
    void analyzeError(const QString& errorOutput);
    
    // New methods for improved launch flow
    bool preCheck(const LaunchConfig& config);
    void downloadMissingFiles(const QString& instancePath);
    bool extractNativesFromJar(const QString& jarPath, const QString& nativesPath);
    bool extractNatives(const QString& instancePath);
    bool executeCustomCommands();

    // 文件补全完成后的后续启动步骤（解压 Natives、构建命令、启动进程）
    void proceedWithLaunch(const LaunchConfig& config, const QString& javaPath);

    // ── Java 缺失自救 ──
    // 启动时没有合适的 Java（没装/路径失效/版本不兼容）：
    // 先全盘查找本机 Java，仍没有合适的就自动安装匹配版本，装好后再继续启动。
    // gameVersion/jsonJavaMajor 用于计算该游戏版本所需的 Java 主版本区间
    void beginJavaRescue(const QString& gameVersion, int jsonJavaMajor);
    // JavaBinaryInfo 仅前向声明，moc 无法为其生成元类型，
    // 故 versionListFetched 信号经 lambda 转调本方法（同线程直连，无需注册）
    void onRescueVersionListFetched(const QVector<JavaBinaryInfo>& binaries);
    void startJavaAutoInstall();
    void finishJavaRescue(const QString& javaPath);
    void failJavaRescue(const QString& reason);
    void connectJavaDownloaderRescue();
    void disconnectJavaDownloaderRescue();
    // 镜像/下载失败时的重试判定：TUNA 失败换官方源再试一次，仍失败才放弃
    void retryOrFailRescueDownload(const QString& reason);
    // 在候选列表里按需求区间严格挑选 Java（逐个 validateJavaPath，64位/真实JVM优先）
    QString selectRescueJava(const QList<JavaInfo>& candidates);

    // Helper methods for file download
    QByteArray downloadFile(const QUrl& url, bool* success);
    bool saveFile(const QString& filePath, const QByteArray& data);
    QString calculateFileHash(const QString& filePath);
    QJsonObject getVersionManifest();
    QJsonObject getVersionInfo(const QString& versionId);
    bool downloadClientJar(const QString& instancePath, const QJsonObject& versionJson);
    bool downloadLibrariesForInstance(const QJsonObject& versionJson, const QString& librariesPath);

    // Mirror fallback
    static void applyMirrorFallback(QString& url);

    // Asset download (async)
    void startAssetsDownload();
    void processAssetQueue();
    void processCompletionQueue();
    void downloadSingleCompletionFile(const FileCompletionTask& task);
    void cleanupActiveReply(QNetworkReply* reply);
    
    // 优化的文件补全系统（参考 HMCL/Prism Launcher）
    void enqueueCompletionTask(const FileCompletionTask& task);          // 按优先级插入队列
    void sortCompletionQueueByPriority();                                // 队列按优先级排序
    void cleanupStaleTempFiles();                                        // 清理残留 .tmp 文件
    QString extractHostFromUrl(const QString& url) const;                // 从 URL 提取主机名
    int getAvailableSlotForHost(const QString& host) const;              // 获取主机的可用槽位
    void recordDownloadSpeed(qint64 bytes, qint64 elapsedMs);            // 记录下载速度
    void updateAdaptiveConcurrency(bool success);                        // 自适应并发调整
    bool isMirrorAvailable(const QString& url);                          // 检查镜像是否可用
    void setMirrorCooldown(const QString& mirrorUrl);                    // 设置镜像冷却
    static bool isRetryableError(int httpStatusCode, const QString& errorString); // 判断错误是否可重试
    
    // Helper methods for pre-launch processing
    bool updateLauncherProfiles(const LaunchConfig& config);
    bool updateOptionsTxt(const LaunchConfig& config);
    bool adjustGraphicsSettings();
    bool extractLog4jConfig(const LaunchConfig& config);
    bool preLaunchProcessing(const LaunchConfig& config);
    
    // Helper methods for game window monitoring
    void waitForGameWindow();
    
    // Helper methods for error handling and logging
    void logError(const QString& errorType, const QString& errorMessage, const QString& details = "");
    void logInfo(const QString& message);
    
    // Completion finished check helper
    void checkAndEmitCompletionFinished();
    
    // Common helper: evaluate Mojang-style rules[] array (os/platform filtering)
    // Returns true if the entry should be included on the current platform
    static bool evaluateLibraryRules(const QJsonArray& rules);
};