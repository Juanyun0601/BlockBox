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

class ErrorAnalyzer;

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
    QList<JavaInfo> findJavaOnSystem();
    void initJavaCache();
    void searchJavaInDirectory(const QString& dirPath, QList<JavaInfo>& javaList,
                               QSet<QString>& seenPaths, int depth, int maxDepth);
    bool validateJavaPath(const QString& path, JavaInfo& info);
    JavaInfo findBestJavaVersion(const QString& gameVersion);

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