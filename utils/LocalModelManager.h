/**
 * @file   LocalModelManager.h
 * @brief  本地 AI 模型管理器 - 基于 Ollama 的本地大模型推理后端管理
 * @author BlockBox Team
 * @date   2026-08-06
 *
 * 负责：
 * 1. 检测本机是否已安装 Ollama，并定位可执行文件
 * 2. 检测 Ollama 服务是否在运行（HTTP 探测 11434 端口）
 * 3. 启动 / 停止 ollama serve 子进程
 * 4. 拉取（pull）、列出（list）、删除（rm）本地模型
 * 5. 引导用户下载并运行 Ollama 安装器
 *
 * 设计要点：
 * - 所有耗时操作（pull、安装器下载）均为异步，通过信号回报进度与结果
 * - 模型拉取通过解析 ollama pull 的流式 JSON 输出实时报告百分比
 * - 服务状态检测通过 HTTP GET /api/tags 完成（Ollama 原生 REST API）
 * - 拉起的 ollama serve 子进程由本对象持有生命周期，析构时自动清理
 */

#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class MultiThreadDownloader;

/**
 * @brief 本地模型信息（对应 Ollama 中一个已下载的模型）
 */
struct LocalModelInfo
{
    QString tag;     // 模型 tag，如 "gemma3:4b"
    QString name;    // 模型名（去掉 :tag 后的部分），如 "gemma3"
    QString size;    // 人类可读大小，如 "2.5 GB"
    qint64 sizeBytes = 0; // 字节数（用于排序/显示）
    QString modifiedAt; // 最后修改时间字符串
    QString digest;  // 模型 digest（用于唯一标识）
};

/**
 * @brief 可下载模型的预设条目
 *
 * 由 LocalModelManager 内置维护，UI 据此渲染"下载新模型"列表。
 * 用户提到的 "Gemma 4" / "Qwen3.6" 目前 Ollama 仓库尚无对应版本，
 * 这里采用最新可用的 Gemma 3 与 Qwen3 系列，UI 显示时附注说明。
 */
struct LocalModelPreset
{
    QString tag;         // Ollama 拉取用的完整 tag，如 "gemma3:4b"
    QString displayName; // UI 显示名，如 "Gemma 3 4B"
    QString family;      // 模型家族，如 "Gemma" / "Qwen"
    QString description; // 简短描述（参数量、推荐显存等）
    QString sizeHint;    // 大小提示，如 "约 2.5 GB"
};

/**
 * @brief 本地模型管理器
 *
 * 单例风格（每个使用者通常持有同一实例），但本身不强制单例，
 * 由 LocalModelDialog 等上层组件持有一个实例即可。
 *
 * 所有公开方法均为非阻塞异步操作，结果通过信号回报。
 */
class LocalModelManager : public QObject
{
    Q_OBJECT

public:
    /// Ollama 服务状态
    enum class ServiceStatus
    {
        Unknown,            // 未检测
        NotInstalled,       // 未安装 Ollama
        InstalledStopped,   // 已安装但服务未运行
        Running             // 服务运行中
    };
    Q_ENUM(ServiceStatus)

    explicit LocalModelManager(QObject* parent = nullptr);
    ~LocalModelManager();

    // ========================================================================
    // 安装与服务状态
    // ========================================================================

    /**
     * @brief 检测本机是否已安装 Ollama
     * @return true 表示在 PATH 或常见安装路径下找到 ollama 可执行文件
     *
     * 同步调用，仅查找文件系统，不发起网络或进程请求。
     */
    bool isOllamaInstalled() const;

    /**
     * @brief 返回 ollama 可执行文件路径（若未安装返回空字符串）
     *
     * 查找顺序：
     * 1. PATH 中的 ollama（通过 `where ollama`）
     * 2. C:\Program Files\Ollama\ollama.exe
     * 3. %LOCALAPPDATA%\Programs\Ollama\ollama.exe
     * 4. 用户主目录下的 .ollama/bin/ollama.exe
     */
    QString ollamaPath() const;

    /**
     * @brief 异步检测服务状态（HTTP 探测 11434 端口的 /api/tags）
     *
     * 探测完成后发出 serviceStatusChecked 信号。
     */
    void checkServiceStatus();

    /**
     * @brief 当前缓存的服务状态（不发起请求，仅返回最近一次检测结果）
     */
    ServiceStatus serviceStatus() const { return m_serviceStatus; }

    /**
     * @brief 启动 ollama serve 子进程
     *
     * 若服务已运行则直接 emit serviceStarted。启动成功后子进程由本对象持有。
     * 进程异常退出时会发出 serviceStopped 信号。
     * @return true 表示已发起启动流程（不代表服务已就绪）
     */
    bool startService();

    /**
     * @brief 停止由本对象启动的 ollama serve 子进程
     *
     * 注意：只会停止由 startService 启动的子进程，不会影响系统已运行的 Ollama 服务。
     */
    void stopService();

    // ========================================================================
    // 模型管理
    // ========================================================================

    /**
     * @brief 异步拉取模型
     * @param tag 模型 tag，如 "gemma3:4b"
     *
     * 通过 ollama pull <tag> 流式输出实时进度。同一时刻只允许一个拉取任务，
     * 已有任务进行中时调用会被忽略并 emit pullFailed("已有任务进行中")。
     */
    void pullModel(const QString& tag);

    /**
     * @brief 取消正在进行的模型拉取
     */
    void cancelPull();

    /**
     * @brief 异步列出已下载的本地模型
     *
     * 优先用 HTTP GET /api/tags（服务运行时可用，速度快）；
     * 服务未运行时回退到 ollama list 子进程。
     * 完成后发出 modelListReady 信号。
     */
    void listModels();

    /**
     * @brief 异步删除模型
     * @param tag 模型 tag
     *
     * 通过 ollama rm <tag> 实现。完成后发出 modelRemoved 信号。
     */
    void removeModel(const QString& tag);

    /**
     * @brief 是否正在拉取模型
     */
    bool isPulling() const { return m_pullProcess != nullptr; }

    /**
     * @brief 获取内置可下载模型预设列表
     *
     * 包含 Gemma 3 系列与 Qwen3 系列各档位，UI 据此渲染下载卡片。
     */
    QList<LocalModelPreset> presetModels() const;

    // ========================================================================
    // 安装器下载与运行
    // ========================================================================

    /**
     * @brief 异步下载 Ollama 安装器到软件同级 BlockBox 文件夹
     *
     * 使用 MultiThreadDownloader 多线程下载 OllamaSetup.exe，并接入
     * DownloadTaskManager 任务系统（在任务栏显示进度卡片）。
     * 支持多源回退：依次尝试官方源、GitHub Releases、镜像源。
     * 完成后发出 installerDownloaded 信号；失败发出 installerDownloadFailed。
     */
    void downloadInstaller();

    /**
     * @brief 取消正在进行的安装器下载
     */
    void cancelInstallerDownload();

    /**
     * @brief 运行已下载的安装器（QProcess::startDetached）
     * @return true 表示已成功启动安装器进程
     *
     * 安装器会请求 UAC 提权，安装完成后 Ollama 会自动注册到 PATH 并启动服务。
     * 调用方应在收到 installerDownloaded 后再调用此方法。
     */
    bool runInstaller();

    /**
     * @brief 安装器本地路径（未下载时返回空）
     */
    QString installerPath() const { return m_installerPath; }

    /**
     * @brief 是否正在下载安装器
     */
    bool isDownloadingInstaller() const { return m_installerDownloader != nullptr; }

    /**
     * @brief Ollama 安装器下载目录（软件同级 BlockBox 文件夹）
     *
     * 路径为 <应用所在目录>/BlockBox/，不存在则自动创建。
     * 所有本地模型相关文件（安装器等）均存放于此。
     */
    static QString downloadDir();

    // ========================================================================
    // 工具方法
    // ========================================================================

    /**
     * @brief Ollama 服务基础 URL，如 "http://localhost:11434"
     */
    static QString ollamaBaseUrl();

    /**
     * @brief Ollama OpenAI 兼容端点 URL，如 "http://localhost:11434/v1"
     *
     * 该 URL 可直接填入 AiModel.apiUrl，供 AiService 复用现有 OpenAI 兼容请求逻辑。
     */
    static QString ollamaOpenAiUrl();

signals:
    /**
     * @brief 服务状态检测完成
     * @param status 最新服务状态
     */
    void serviceStatusChecked(LocalModelManager::ServiceStatus status);

    /**
     * @brief 服务已启动（或检测到已在运行）
     */
    void serviceStarted();

    /**
     * @brief 服务已停止 / 异常退出
     * @param errorMessage 错误说明（正常停止时为空）
     */
    void serviceStopped(const QString& errorMessage);

    /**
     * @brief 模型拉取进度更新
     * @param tag 模型 tag
     * @param percent 进度百分比（0-100，下载阶段有值；digest 阶段可能为 -1）
     * @param speedText 速度文本（如 "5.2 MB/s"），可能为空
     * @param statusText 状态文本（如 "pulling manifest..." / "verifying..."）
     */
    void pullProgress(const QString& tag, int percent, const QString& speedText,
                      const QString& statusText);

    /**
     * @brief 模型拉取完成
     * @param tag 模型 tag
     */
    void pullFinished(const QString& tag);

    /**
     * @brief 模型拉取失败 / 被取消
     * @param tag 模型 tag（取消时可能为空）
     * @param errorMessage 错误说明
     */
    void pullFailed(const QString& tag, const QString& errorMessage);

    /**
     * @brief 模型列表已就绪
     * @param models 已下载的本地模型列表
     *
     * 由 listModels() 触发。服务未运行且 ollama list 失败时发出空列表。
     */
    void modelListReady(const QList<LocalModelInfo>& models);

    /**
     * @brief 模型删除完成
     * @param tag 被删除的模型 tag
     * @param success 是否成功
     * @param errorMessage 失败说明（成功时为空）
     */
    void modelRemoved(const QString& tag, bool success, const QString& errorMessage);

    /**
     * @brief 安装器下载进度
     * @param percent 进度百分比（0-100）
     * @param receivedBytes 已下载字节数
     * @param totalBytes 总字节数（未知时为 -1）
     */
    void installerDownloadProgress(int percent, qint64 receivedBytes, qint64 totalBytes);

    /**
     * @brief 安装器下载完成
     * @param localPath 安装器本地路径
     */
    void installerDownloaded(const QString& localPath);

    /**
     * @brief 安装器下载失败
     * @param errorMessage 错误说明
     */
    void installerDownloadFailed(const QString& errorMessage);

private slots:
    void onStatusReplyFinished();
    void onListReplyFinished();
    void onPullReadyRead();
    void onPullFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onPullErrorOccurred(QProcess::ProcessError error);
    void onServiceProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onServiceProcessErrorOccurred(QProcess::ProcessError error);
    void onRemoveFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onInstallerProgress(const QString& taskId, qint64 received, qint64 total);
    void onInstallerCompleted(const QString& taskId, const QString& savePath);
    void onInstallerFailed(const QString& taskId, const QString& error);

private:
    /**
     * @brief 通过 ollama list 子进程获取模型列表（服务未运行时的回退方案）
     */
    void listModelsViaProcess();

    /**
     * @brief 用当前 installerUrls()[m_installerUrlIndex] 启动一次下载
     *
     * 由 downloadInstaller 触发首个源，onInstallerFailed 在源失败时递增
     * m_installerUrlIndex 后再次调用本函数切换到下一个源。
     */
    void startInstallerDownloadWithCurrentUrl();

    /**
     * @brief 解析 ollama pull 流式输出的一行 JSON
     * @param line 一行 JSON 文本
     * @param tag  当前拉取的模型 tag（用于 emit 信号）
     *
     * ollama pull 输出形如：
     *   {"status":"pulling manifest"}
     *   {"status":"downloading","digest":"...","total":...,"completed":...}
     *   {"status":"success","total":...,"status":"success"}
     */
    void parsePullLine(const QString& line, const QString& tag);

    /**
     * @brief 把字节数格式化为人类可读字符串
     */
    static QString formatBytes(qint64 bytes);

    /**
     * @brief 构造内置的可下载模型预设列表
     */
    static QList<LocalModelPreset> buildPresetModels();

    QNetworkAccessManager* m_networkManager;
    QProcess* m_serviceProcess;     // 本对象启动的 ollama serve 子进程
    QProcess* m_pullProcess;        // 当前模型拉取进程
    QProcess* m_removeProcess;      // 当前模型删除进程
    QString m_removeTag;            // 正在删除的模型 tag（供 onRemoveFinished 使用）
    MultiThreadDownloader* m_installerDownloader;
    QString m_installerPath;        // 安装器本地路径
    QString m_installerTaskId;      // 安装器下载在任务系统中的 taskId
    int m_installerUrlIndex = 0;    // 当前使用的下载源索引（多源回退）
    ServiceStatus m_serviceStatus;

    static constexpr int OLLAMA_PORT = 11434;       ///< Ollama 默认服务端口
    static constexpr int STATUS_CHECK_TIMEOUT_MS = 2000; ///< 服务状态检测超时
    static constexpr int LIST_TIMEOUT_MS = 8000;    ///< 模型列表请求超时
    static constexpr int INSTALLER_THREAD_COUNT = 4;    ///< 安装器多线程下载数
    /// 安装器下载源列表（依次回退）：官方源、GitHub Releases、镜像源
    static const QStringList installerUrls();
};
