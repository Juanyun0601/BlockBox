/**
 * @file   AiService.h
 * @brief  AI 服务模块 - 提供与 OpenAI 兼容 API 的流式聊天功能
 * @author BlockBox Team
 * @date   2026-06-23
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QList>
#include <QEventLoop>
#include <QTimer>

#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModrinthAPI.h"
#include "utils/mod/ModData.h"
#include "utils/content/ContentData.h"

class QNetworkAccessManager;

/**
 * @brief AI 模型配置信息
 */
struct AiModel
{
    QString id;               // 模型 ID，如 "deepseek-chat"
    QString displayName;      // 显示名称，如 "DeepSeek Chat"
    QString apiUrl;           // API 端点基础 URL
    QString apiKey;           // API 密钥
    QString websiteUrl;       // 服务商官网 URL（用于跳转）
    bool isCustom = false;    // 是否为用户自定义模型
    bool supportsThinking = false; // 是否支持思考/推理
    bool isLocal = false;     // 是否为本地 Ollama 模型（通过 http://localhost:11434/v1 接入）
    QString localTag;         // 本地模型在 Ollama 中的 tag，如 "gemma3:4b"（仅 isLocal=true 时有效）
};

/**
 * @brief Agent 工具调用记录（用于 UI 步骤卡片展示与持久化）
 */
struct AgentToolCall
{
    QString name;        // 工具名：web_search / list_instances / ...
    QString arguments;   // 原始 JSON 参数字符串
    QString result;      // 工具返回结果文本（已截断到合理长度）
    QString resultSummary; // 结果摘要（一行字，用于卡片标题）
    qint64 durationMs = 0; // 执行耗时（毫秒）
    bool success = true;   // 是否执行成功
    QString errorMessage;  // 失败时的错误说明
};

/**
 * @brief Agent 自维护任务清单中的单条任务
 *
 * 由 AI 通过 update_task_list 工具覆盖式更新，UI 据此渲染任务清单卡片。
 */
struct AgentTask
{
    QString id;          // 任务唯一标识（AI 维持稳定以便跨调用引用）
    QString content;     // 任务内容描述
    QString status;      // 状态：pending / in_progress / completed
    QString priority;    // 优先级：high / medium / low
};

/**
 * @brief 聊天消息
 */
struct ChatMessage
{
    QString role;       // "user", "assistant", "system"
    QString content;    // 消息内容
    QString reasoning;  // 思考/推理内容（仅支持推理的模型）
    QString stats;      // 统计信息（仅 AI 回复，如 token 用量/耗时/费用）
    QList<AgentToolCall> toolCalls; // 本条 AI 消息触发的工具调用步骤（用于 UI 展示与持久化）
    QList<ResourceReference> references; // 本条用户消息携带的引用列表（用于 UI 展示样式化卡片；AI 侧通过 content 中附加的引用文本感知）
};

/**
 * @brief Token 用量统计
 */
struct TokenUsage
{
    int promptTokens = 0;     // 输入 token 数
    int completionTokens = 0; // 输出 token 数
    int totalTokens = 0;      // 总 token 数
    bool valid = false;       // 是否有有效数据
};

/**
 * @brief 联网搜索模式
 */
enum class WebSearchMode
{
    Auto,   // 自动联网（AI 按需联网）
    Prefer, // 优先联网（AI 更会联网确认）
    Off     // 不联网
};

/**
 * @brief AI 助手工作模式
 */
enum class AssistantMode
{
    Chat = 0, // 聊天模式
    Work = 1  // 工作模式
};

/**
 * @brief 单条联网搜索结果（用于汇总卡片展示）
 */
struct WebSearchResult
{
    QString query;     // 触发本次搜索的查询词
    QString title;     // 网页标题
    QString url;       // 网页链接
    QString snippet;   // 摘要
    QString searchTime; // 检索时间
};

/**
 * @brief 工作区限制配置
 */
struct WorkspaceRestriction
{
    bool enabled = false;                    // 是否启用工作区限制
    QStringList allowedDirectories;          // 允许的目录列表（白名单）
    QStringList rejectedDirectories;         // 拒绝的目录列表（黑名单）
};

/**
 * @brief AI 服务类
 *
 * 通过 SSE (Server-Sent Events) 协议与 OpenAI 兼容的 API 端点通信，
 * 支持流式内容接收和推理内容展示。
 */
class AiService : public QObject
{
    Q_OBJECT

public:
    explicit AiService(QObject* parent = nullptr);
    ~AiService();

    /**
     * @brief 发送聊天消息
     * @param history 对话历史消息列表
     * @param model   AI 模型配置
     * @param mode    联网搜索模式（默认不联网）
     * @param assistantMode AI 助手工作模式（决定注入的系统提示词，默认聊天模式）
     * @param customSystemPrompt 用户从提示词库选择的自定义系统提示词（非空时替换模式提示词）
     *
     * 构建 JSON 请求体，包含 model、messages 数组和 stream: true。
     * POST 到 {apiUrl}/v1/chat/completions 端点。
     * 启用联网时，会注入 system 提示并按服务商附加联网参数。
     * 系统提示词顺序：自定义/模式提示词 → 联网提示词 → 对话历史。
     */
    void sendMessage(const QList<ChatMessage>& history, const AiModel& model,
                     WebSearchMode mode = WebSearchMode::Off,
                     AssistantMode assistantMode = AssistantMode::Chat,
                     const QString &customSystemPrompt = QString());

    /**
     * @brief 获取指定模式下的系统提示词
     * @param mode AI 助手工作模式
     * @return 系统提示词文本（首次调用从内置 JSON 资源加载并缓存）
     *
     * 从 :/resources/ai_system_prompts.json 读取，按 "chat" / "work" 键取值。
     * 资源加载失败时返回空字符串，调用方应自行处理空值。
     */
    QString systemPromptForMode(AssistantMode mode);

    /**
     * @brief 获取指定模式的自定义系统提示词
     * @param mode AI 助手工作模式
     * @return 自定义提示词文本；未设置时返回空字符串
     */
    QString customSystemPrompt(AssistantMode mode) const;

    /**
     * @brief 设置指定模式的自定义系统提示词
     * @param mode AI 助手工作模式
     * @param prompt 自定义提示词文本（空字符串等同于清除）
     *
     * 持久化到 SettingsManager（ai/customSystemPrompt/chat 或 work）。
     */
    void setCustomSystemPrompt(AssistantMode mode, const QString &prompt);

    /**
     * @brief 恢复指定模式的系统提示词为内置默认值
     * @param mode AI 助手工作模式
     *
     * 清除 SettingsManager 中的自定义提示词，后续 systemPromptForMode
     * 将返回内置资源文件中的默认提示词。
     */
    void resetSystemPromptToDefault(AssistantMode mode);

    /**
     * @brief 停止当前流式请求
     *
     * 中止当前正在进行的网络回复，清理资源。
     */
    void stopStreaming();

    // --- 思考强度 ---
    /**
     * @brief 设置思考强度（0=关闭，1=低，2=中，3=高）
     * @param effort 思考强度等级
     *
     * 仅对支持思考的模型生效：OpenAI o 系列映射为 reasoning_effort，
     * 其余服务商在 effort > 0 时启用思考（低于高强度按服务商默认行为）。
     */
    void setThinkingEffort(int effort) { m_thinkingEffort = effort; }

    /**
     * @brief 获取当前思考强度
     * @return 思考强度等级（0-3）
     */
    int thinkingEffort() const { return m_thinkingEffort; }

    // --- 操作权限 ---
    /**
     * @brief 设置 AI 操作权限级别（0=全部确认，1=关键确认，2=完全访问）
     * @param mode 权限级别
     *
     * 全部确认：AI 执行任何写操作前都需要用户确认；
     * 关键确认：仅下载、删除、实例修改等关键操作需要确认；
     * 完全访问：AI 直接执行所有操作，不再询问。
     * 当前为运行时标记，供后续工具确认逻辑（agentToolCall 前置校验）使用。
     */
    void setPermissionMode(int mode) { m_permissionMode = mode; }

    /**
     * @brief 获取当前操作权限级别
     * @return 权限级别（0-2）
     */
    int permissionMode() const { return m_permissionMode; }


    // --- 提示词优化 ---
    /**
     * @brief 异步优化一段提示词草稿（非流式请求，独立于主对话流）
     * @param draft 用户输入框中的提示词草稿
     * @param model 使用的 AI 模型配置
     *
     * 使用独立的 QNetworkReply（m_optimizeReply），不影响主对话流。
     * 完成后 emit promptOptimized 信号；失败时静默忽略。
     * 上一次优化请求仍在飞行时先中止再发起。
     */
    void optimizePrompt(const QString &draft, const AiModel &model);

    // --- 工作区限制 ---
    /**
     * @brief 获取当前工作区限制配置
     * @return 工作区限制配置结构体
     */
    WorkspaceRestriction workspaceRestriction() const { return m_workspaceRestriction; }

    /**
     * @brief 设置工作区限制配置
     * @param restriction 工作区限制配置结构体
     *
     * 持久化到 SettingsManager（ai/workspaceRestriction/...）。
     */
    void setWorkspaceRestriction(const WorkspaceRestriction &restriction);

    /**
     * @brief 检查路径是否在工作区限制范围内
     * @param path 要检查的路径
     * @return true 表示路径允许访问，false 表示被拒绝
     *
     * 检查逻辑：
     * 1. 如果工作区限制未启用，返回 true
     * 2. 如果路径在拒绝列表中，返回 false
     * 3. 如果路径在允许列表中，返回 true
     * 4. 如果允许列表为空，返回 true（不限制）
     * 5. 否则返回 false
     */
    bool isPathAllowed(const QString &path) const;

    /**
     * @brief 获取当前请求使用的对话历史（含压缩后的历史）
     * @return 对话历史列表
     *
     * 压缩成功后，m_pendingHistory 已被替换为压缩后的历史。
     * UI 在收到 contextCompressionFinished(success=true) 后调用此方法取回压缩后的历史，
     * 更新本地 Conversation::messages 并持久化。
     */
    QList<ChatMessage> pendingHistory() const { return m_pendingHistory; }

    /**
     * @brief 主线程回填用户对 ask_user 工具的回答
     * @param answer 用户回答文本（取消时传空字符串）
     *
     * 由 AiChatPage 在用户提交对话框答案后调用。内部 emit userAnswerProvided 信号，
     * 子线程 executeAskUser 的 QEventLoop 收到后退出阻塞并返回答案给模型。
     */
    void provideUserAnswer(const QString& answer);

    /**
     * @brief 异步生成对话标题（非流式请求，独立于主对话流）
     * @param userMessage 用户的第一条消息
     * @param aiReply AI 的回复内容（用于辅助生成更贴切的标题）
     * @param model 使用的 AI 模型配置
     *
     * 在 onStreamFinished 后调用。使用独立的 QNetworkReply（m_titleReply），
     * 不影响主对话流。完成后 emit titleGenerated 信号。
     * 请求失败时静默忽略（保持原"新对话"标题，不报错）。
     */
    void generateTitle(const QString& userMessage, const QString& aiReply,
                       const AiModel& model);

signals:
    /**
     * @brief 流式内容增量到达
     * @param delta 新增的文本片段
     */
    void streamContentReceived(const QString& delta);

    /**
     * @brief 推理/思考内容增量到达
     * @param delta 新增的推理文本片段
     */
    void streamReasoningReceived(const QString& delta);

    /**
     * @brief 流式传输完成
     * @param usage Token 用量统计（可能无效，若 API 未返回）
     */
    void streamFinished(const TokenUsage &usage);

    /**
     * @brief 发生错误
     * @param error 错误描述信息
     */
    void errorOccurred(const QString& error);

    /**
     * @brief 联网搜索已触发（用于 UI 提示）
     * @param query 搜索关键词
     */
    void webSearchStarted(const QString& query);

    /**
     * @brief 联网搜索已完成（用于 UI 提示）
     * @param resultCount 搜索结果条数
     */
    void webSearchFinished(int resultCount);

    /**
     * @brief 单轮搜索结果已采集（含标题/链接/摘要，用于汇总卡片）
     * @param results 本轮搜索的结构化结果列表
     */
    void webSearchResultsCollected(const QList<WebSearchResult> &results);

    /**
     * @brief Agent 工具调用开始（用于 UI 插入"进行中"步骤卡片）
     * @param name 工具名（web_search / list_instances / ...）
     * @param arguments 原始 JSON 参数字符串
     *
     * 工作模式启用 Agent 工具时，每次模型触发 tool_calls 都会先发此信号，
     * 随后执行工具，结束后发 agentToolCallFinished。
     */
    void agentToolCallStarted(const QString &name, const QString &arguments);

    /**
     * @brief Agent 工具调用结束（用于 UI 更新步骤卡片为完成态）
     * @param name 工具名
     * @param resultSummary 结果摘要（一行字，用于卡片标题）
     * @param durationMs 执行耗时（毫秒）
     * @param success 是否成功
     *
     * 注意：web_search 工具的进度仍由 webSearchStarted/Finished/ResultsCollected
     * 三个信号单独驱动 UI 的搜索提示与汇总卡片，本信号仅用于步骤卡片统一记录。
     */
    void agentToolCallFinished(const QString &name, const QString &resultSummary,
                               qint64 durationMs, bool success);

    /**
     * @brief 资源下载已请求（由 download_resource 工具触发，主线程接收后转发到下载基础设施）
     * @param info 资源项目信息
     * @param file 选定的版本文件
     * @param instancePath 目标实例路径
     * @param contentType 内容类型（决定落地子目录）
     *
     * 该信号在子线程（QtConcurrent::run）中发出，主线程槽函数接收后调用
     * ContentDownloader/DownloadTaskManager 触发实际下载。由于是跨线程信号，
     * ModInfo 与 ModVersionFile 已在构造函数中通过 qRegisterMetaType 注册。
     */
    void resourceDownloadRequested(const ModInfo& info, const ModVersionFile& file,
                                    const QString& instancePath, ContentType contentType);

    /**
     * @brief 实例下载已请求（由 download_instance 工具触发，主线程接收后调用 VersionDownloader）
     * @param versionId Minecraft 版本号，如 "1.20.4"
     * @param instancePath 实例根目录（绝对路径）
     * @param instanceName 实例显示名（可空）
     * @param loader 加载器类型（forge/fabric/neoforge/quilt/optifine/none，可空）
     * @param loaderVersion 加载器版本（可空）
     * @param source 下载源（official/bmcl）
     *
     * 该信号在子线程（QtConcurrent::run）中发出，主线程槽函数接收后触发实际下载。
     * 下载进度通过 DownloadTaskManager 统一反馈，工具本身不阻塞等待完成。
     */
    void instanceDownloadRequested(const QString& versionId, const QString& instancePath,
                                    const QString& instanceName, const QString& loader,
                                    const QString& loaderVersion, const QString& source);

    /**
     * @brief 实例修改已请求（由 modify_instance 工具触发，主线程接收后下载新版本到同实例路径）
     * @param instancePath 实例根目录（绝对路径）
     * @param newVersion 新游戏版本（可空表示不改版本）
     * @param loader 加载器类型（forge/fabric/neoforge/quilt/optifine/none，可空表示不加加载器）
     * @param loaderVersion 加载器版本（可空）
     * @param source 下载源（official/bmcl）
     *
     * 本质是向同一实例路径下载新版本（不删除旧版本目录，新旧版本目录共存）。
     * 下载进度通过 DownloadTaskManager 统一反馈，工具本身不阻塞等待完成。
     */
    void instanceModifyRequested(const QString& instancePath, const QString& newVersion,
                                  const QString& loader, const QString& loaderVersion,
                                  const QString& source);

    /**
     * @brief AI 向用户提问（由 ask_user 工具触发，主线程接收后弹出交互对话框）
     * @param question 问题文本
     * @param options 预设选项列表（非空时单选；空时让用户自由输入）
     * @param defaultValue 默认值（可空）
     * @param allowOther 是否允许在选择选项之外自由输入其他答案
     *
     * 该信号在子线程（QtConcurrent::run）中发出，主线程槽函数接收后弹对话框收集用户回答，
     * 随后调用 provideUserAnswer() 回填答案。工具在子线程用 QEventLoop 阻塞等待答案。
     */
    void userQuestionAsked(const QString& question, const QStringList& options,
                            const QString& defaultValue, bool allowOther);

    /**
     * @brief 用户回答已提供（内部信号，由 provideUserAnswer 发出，子线程 QEventLoop 监听）
     * @param answer 用户回答文本（取消时为空字符串）
     */
    void userAnswerProvided(const QString& answer);

    /**
     * @brief 上下文压缩开始（仅工作模式自动触发）
     *
     * 当对话历史估算 token 数超过阈值（24000）时，在 sendMessage 内部自动触发。
     * UI 收到此信号后可显示"正在压缩上下文..."提示。
     */
    void contextCompressionStarted();

    /**
     * @brief 上下文压缩结束
     * @param originalTokens 压缩前估算 token 数
     * @param compressedTokens 压缩后估算 token 数
     * @param success 是否压缩成功（失败时仍用原历史继续请求）
     *
     * UI 收到此信号后可显示压缩结果提示或移除压缩提示。
     * 压缩成功时，UI 应调用 pendingHistory() 取回压缩后的历史并持久化到当前对话，
     * 避免下次发送时重复触发压缩。
     */
    void contextCompressionFinished(int originalTokens, int compressedTokens, bool success);

    /**
     * @brief AI 自维护任务清单已更新（由 update_task_list 工具触发）
     * @param tasks 最新任务列表（覆盖式，UI 据此整体重绘清单卡片）
     *
     * 每次模型调用 update_task_list 工具时发出，tasks 为覆盖后的完整清单。
     * UI 应在当前 AI 回复气泡内创建或刷新任务清单卡片。
     */
    void taskListUpdated(const QList<AgentTask> &tasks);

    /**
     * @brief 对话标题已生成（由 generateTitle 异步请求完成后发出）
     * @param title 生成的标题文本（已去除引号、换行、空白）
     *
     * AiChatPage 在 onStreamFinished 后调用 generateTitle，收到此信号后
     * 通过 updateConversationTitle 更新当前对话标题（仅当仍是"新对话"时生效）。
     */
    void titleGenerated(const QString &title);

    /**
     * @brief 提示词优化已完成（由 optimizePrompt 异步请求完成后发出）
     * @param optimized 优化后的提示词文本
     *
     * AiChatPage 在用户点击「提示词优化」按钮后调用 optimizePrompt，
     * 收到此信号后将优化结果写回输入框。失败时静默忽略（不发信号）。
     */
    void promptOptimized(const QString &optimized);

private slots:
    void onReadyRead();
    void onReplyFinished();
    void onErrorOccurred(QNetworkReply::NetworkError error);
    void onSearchReplyFinished(); ///< 本地搜索响应处理
    void onWebpageReplyFinished(); ///< 网页访问响应处理
    void onTitleReplyFinished();  ///< 标题生成响应处理
    void onOptimizeReplyFinished(); ///< 提示词优化响应处理

private:
    /**
     * @brief 解析 SSE 数据行
     * @param data JSON 字符串数据
     */
    void parseSseData(const QString& data);

    /**
     * @brief 根据服务商与模式附加联网参数到请求体
     * @param body    请求体 JSON 对象
     * @param apiUrl  模型 API 基础 URL
     * @param mode    联网搜索模式
     *
     * 识别常见服务商（Kimi、GLM、Qwen、ERNIE、OpenRouter 等）并附加
     * 对应的联网参数；Perplexity 自带联网；其他服务商仅依赖 system 提示。
     */
    void applyWebSearchParams(QJsonObject &body, const QString &apiUrl, WebSearchMode mode);

    /**
     * @brief 构建联网模式的 system 提示文本
     * @param mode 联网搜索模式
     * @return system 提示内容
     */
    QString webSearchSystemPrompt(WebSearchMode mode) const;

    /**
     * @brief 判断服务商是否原生支持联网搜索（无需本软件工具回退）
     * @param apiUrl 模型 API 基础 URL
     * @return true 表示原生支持（Kimi/GLM/Qwen/ERNIE/OpenRouter/Perplexity 等）
     */
    bool providerSupportsNativeSearch(const QString &apiUrl) const;

    /**
     * @brief 为不支持原生联网的服务商附加 OpenAI function calling 工具定义
     * @param body 请求体 JSON 对象
     * @param mode 联网搜索模式（影响工具描述强度）
     *
     * 在请求体中注入 web_search 工具定义，引导模型通过 tool_calls 触发本地搜索。
     */
    void applyLocalSearchTools(QJsonObject &body, WebSearchMode mode);

    /**
     * @brief 为工作模式附加完整 Agent 工具集（含本地只读工具 + web_search）
     * @param body 请求体 JSON 对象
     *
     * 工作模式专用。注入以下工具定义：web_search、list_instances、
     * check_loader_compatibility、get_game_versions、analyze_crash_log、
     * list_mods、detect_java、get_settings。tool_choice 设为 auto。
     */
    void applyAgentTools(QJsonObject &body);

    /**
     * @brief 通用工具调用分发：按 function.name 调度到具体执行函数
     * @param toolCall 单个 tool_call 对象（含 id、function.name、function.arguments）
     *
     * 工作模式 Agent 循环的核心入口。先发 agentToolCallStarted 信号，
     * 根据 name 分发到 executeXxx 函数，执行完毕后发 agentToolCallFinished 信号，
     * 并通过 continueWithToolResult 回填结果重新请求模型。
     */
    void dispatchToolCall(const QJsonObject &toolCall);

    // --- Agent 本地工具执行函数（耗时工具使用 QtConcurrent 子线程化） ---
    /// 列出本机所有实例目录及子实例
    QString executeListInstances();
    /// 检查加载器兼容性（参数：loader, installed_loaders 数组）
    QString executeCheckLoaderCompatibility(const QJsonObject &args);
    /// 获取 Mojang 官方版本清单
    QString executeGetGameVersions();
    /// 分析崩溃日志文本（参数：log_text）
    QString executeAnalyzeCrashLog(const QJsonObject &args);
    /// 扫描指定实例的模组清单（参数：instance_path）
    QString executeListMods(const QJsonObject &args);
    /// 检测本机 Java 环境
    QString executeDetectJava();
    /// 查询启动器当前配置
    QString executeGetSettings();
    /// 读取 Litematica 投影文件信息（参数：file_path, include_material_list?）
    QString executeReadLitematic(const QJsonObject &args);

    // --- Agent 资源类工具执行函数（在子线程中执行，QEventLoop 阻塞等待 API 信号） ---
    /// 搜索在线资源（CurseForge/Modrinth 聚合）
    QString executeSearchResources(const QJsonObject& args);
    /// 获取资源详情
    QString executeGetResourceDetail(const QJsonObject& args);
    /// 列出资源版本文件（支持过滤）
    QString executeListResourceFiles(const QJsonObject& args);
    /// 触发资源下载到实例（emit resourceDownloadRequested 后立即返回）
    QString executeDownloadResource(const QJsonObject& args);

    // --- Agent 任务清单工具 ---
    /// 覆盖式更新 AI 自维护任务清单（emit taskListUpdated 后回填确认信息）
    QString executeUpdateTaskList(const QJsonObject& args);

    // --- Agent 实例管理工具 ---
    /// 启动游戏并收集启动日志（invokeMethod 触发主线程 launchGame + QEventLoop 等待日志）
    QString executeLaunchGame(const QJsonObject& args);
    /// 下载新实例（emit instanceDownloadRequested 后立即返回，不阻塞等待完成）
    QString executeDownloadInstance(const QJsonObject& args);
    /// 修改实例版本（emit instanceModifyRequested 后立即返回，本质是下载新版本到同路径）
    QString executeModifyInstance(const QJsonObject& args);

    // --- Agent 用户交互工具 ---
    /// 向用户提问并阻塞等待回答（emit userQuestionAsked + QEventLoop 等待 userAnswerProvided）
    QString executeAskUser(const QJsonObject& args);

    // --- Agent 技能（Skill）工具 ---
    /**
     * @brief 执行技能工具（在子线程中同步阻塞执行）
     * @param toolName 工具名（通过 m_skillToolMap 定位所属技能 ID）
     * @param args     工具参数 JSON 对象
     * @return 执行结果文本（失败时返回空，错误信息通过前缀 "ERROR: " 返回）
     *
     * 查找 m_skillToolMap 获取技能 ID，调用 SkillManager::executeTool 执行。
     * 未找到工具或技能未信任时返回带 ERROR 前缀的错误说明。
     */
    QString executeSkillTool(const QString& toolName, const QJsonObject& args);

    // --- 资源 API 同步化封装（子线程 QEventLoop 阻塞等待信号，超时 15s 兜底） ---
    /// 同步搜索 CurseForge；apiKey 缺失时 error 置为 "CURSEFORGE_API_KEY_MISSING"
    ModSearchResult syncSearchCurseForge(const QString& query, const QString& classId,
                                          const QString& gameVersion, int page, int pageSize,
                                          QString& error);
    /// 同步搜索 Modrinth；loader 通过 categoryId facets 过滤
    ModSearchResult syncSearchModrinth(const QString& query, const QString& projectType,
                                        const QString& gameVersion, const QString& loader,
                                        int page, int pageSize, QString& error);
    /// 同步获取 CurseForge 资源详情；apiKey 缺失时 error 置为 "CURSEFORGE_API_KEY_MISSING"
    ModInfo syncFetchDetailCurseForge(const QString& projectId, QString& error);
    /// 同步获取 Modrinth 资源详情
    ModInfo syncFetchDetailModrinth(const QString& projectId, QString& error);

    /**
     * @brief 通用工具结果回填：将工具执行结果以 tool 角色消息追加到 pending messages 后重新请求
     * @param toolCallId 关联的 tool_call ID
     * @param toolName 工具名（写入 tool 消息的 name 字段）
     * @param result 工具执行结果文本
     *
     * 替代原 continueWithSearchResults，支持任意工具的结果回填。
     * 重新请求时仍会附加 Agent 工具集，允许模型继续多步调用。
     */
    void continueWithToolResult(const QString &toolCallId, const QString &toolName,
                                const QString &result);

    /**
     * @brief 截断工具结果文本到合理长度，避免超出模型上下文窗口
     * @param text 原始结果文本
     * @param maxLength 最大保留长度（字符数，默认 4000）
     * @return 截断后的文本（超出时尾部追加截断提示）
     */
    QString truncateToolResult(const QString &text, int maxLength = 4000) const;

    /**
     * @brief 根据模型配置与服务商附加思考/推理启用参数
     * @param body  请求体 JSON 对象
     * @param model 模型配置
     *
     * 仅当 model.supportsThinking 为 true 时生效。按服务商识别附加对应参数：
     * 智谱 GLM (thinking)、阿里 Qwen/腾讯混元 (enable_thinking)、OpenAI o 系列
     * (reasoning_effort) 等；无法识别的服务商不附加参数（依赖模型默认行为，
     * 如 DeepSeek-R1、Kimi K1 默认输出 reasoning_content）。
     */
    void applyThinkingParams(QJsonObject &body, const AiModel &model);

    /**
     * @brief 内部发送请求（复用 URL 拼接与请求头设置）
     * @param messages 已构建好的 messages 数组
     * @param model    模型配置
     * @param withTools 是否附加本地搜索工具定义
     *
     * 调用前需已设置 m_pendingHistory/m_pendingModel/m_pendingMode 以支持工具调用循环。
     */
    void sendRequestInternal(const QJsonArray &messages, const AiModel &model, bool withTools);

    /**
     * @brief 执行本地网络搜索
     * @param query 搜索关键词
     *
     * 使用 Bing 搜索 HTML 解析（中国境内可用），结果通过 onSearchReplyFinished 处理。
     */
    void executeWebSearch(const QString &query);

    /**
     * @brief 执行网页访问工具
     * @param url 要访问的网页URL
     * @param extractText 是否提取纯文本（默认true）
     * @param maxLength 返回内容的最大字符数（默认10000）
     *
     * 访问指定URL并返回网页内容，结果通过 onWebpageReplyFinished 处理。
     */
    void executeFetchWebpage(const QString &url, bool extractText = true, int maxLength = 10000);

    /**
     * @brief 解析 Bing 搜索结果 HTML，提取标题/链接/摘要
     * @param html       原始 HTML 数据
     * @param query      触发本次搜索的查询词
     * @param searchTime 检索时间字符串
     * @param maxResults 最大结果条数
     * @return 结构化搜索结果列表（供汇总卡片展示）
     */
    QList<WebSearchResult> parseBingSearchResults(const QString &html, const QString &query,
                                                   const QString &searchTime, int maxResults = 5) const;

    /**
     * @brief 将结构化搜索结果格式化为模型可读文本
     * @param results 搜索结果列表
     * @return 格式化文本（含标题、链接、摘要、检索时间）
     */
    QString formatSearchResults(const QList<WebSearchResult> &results) const;

    /**
     * @brief 工具调用循环：将搜索结果回填并重新请求模型生成最终回答
     */
    void continueWithSearchResults(const QString &query, const QString &searchResults);

    /**
     * @brief 估算消息列表的 token 数（近似）
     * @param messages 消息列表
     * @return 估算 token 数
     *
     * 估算规则：中文字符×2 + 非中文字符/4，再叠加每条消息的固定开销（4 token）。
     * 该估算偏保守，实际 token 数通常小于估算值。
     */
    int estimateTokenCount(const QList<ChatMessage> &messages) const;

    /**
     * @brief 估算 QJsonArray 形式 messages 的 token 数（用于压缩后内部校验）
     */
    int estimateTokenCount(const QJsonArray &messages) const;

    /**
     * @brief 压缩对话历史：用当前模型生成早期消息摘要，替换原早期消息
     * @param history 原始对话历史
     * @param model 当前模型配置
     * @return 压缩后的对话历史（失败时返回空列表，调用方按原历史继续）
     *
     * 流程：
     * 1. 保留系统提示词（开头连续的 role=system 消息）与最近 6 条消息
     * 2. 将中间的早期消息拼接为文本，附加摘要指令发送给当前模型
     * 3. 用模型返回的摘要构造一条 role=system 的"对话历史摘要"消息
     * 4. 返回 [系统提示词] + [摘要消息] + [最近 6 条消息]
     *
     * 同步阻塞调用（在 QtConcurrent 工作线程中由 sendMessage 异步派发执行，
     * 不阻塞 UI 线程；UI 侧应在收到 contextCompressionStarted 后显示提示）。
     */
    QList<ChatMessage> compressContext(const QList<ChatMessage> &history,
                                       const AiModel &model);

    /// 压缩完成（或无需压缩）后回到主线程继续构建并发送请求；
    /// generation 与 m_sendGeneration 不一致说明已被新请求取代，直接丢弃
    void finishSendMessageAfterCompression(int generation,
                                           const QList<ChatMessage> &compressed,
                                           int estimatedTokens,
                                           const QString &customSystemPrompt);

    QNetworkAccessManager* m_networkManager;
    QNetworkReply* m_currentReply;
    QString m_buffer; // 缓冲区，用于暂存不完整的 SSE 行
    TokenUsage m_lastUsage; // 最近一次请求的用量统计

    // --- 本地搜索工具调用相关 ---
    QNetworkAccessManager* m_searchManager;   // 搜索请求管理器
    QNetworkReply* m_searchReply;             // 当前搜索回复
    QNetworkReply* m_webpageReply;            // 当前网页访问回复
    bool m_webpageExtractText;                // 网页访问：是否提取纯文本
    int m_webpageMaxLength;                   // 网页访问：最大返回长度
    QString m_webpageUrl;                     // 网页访问：当前请求URL
    QNetworkReply* m_titleReply;              // 标题生成请求回复（独立于主对话流）
    QNetworkReply* m_optimizeReply;           // 提示词优化请求回复（独立于主对话流）
    int m_thinkingEffort = 2;                 // 思考强度（0=关闭，1=低，2=中，3=高）
    int m_permissionMode = 0;                 // 操作权限（0=全部确认，1=关键确认，2=完全访问）
    QList<ChatMessage> m_pendingHistory;      // 暂存对话历史（工具调用循环用）
    AiModel m_pendingModel;                   // 暂存模型配置
    WebSearchMode m_pendingMode;              // 暂存联网模式
    AssistantMode m_pendingAssistantMode;     // 暂存 AI 助手工作模式（决定是否启用 Agent 工具）
    QJsonArray m_pendingMessages;             // 当前请求的 messages 数组（含 tool_calls 消息）
    QJsonArray m_toolCallsAccumulator;        // 累积的 tool_calls（流式增量合并）
    int m_currentToolCallIndex;               // 当前正在累积的 tool_call 索引
    bool m_hasToolCalls;                      // 本次响应是否包含 tool_calls
    int m_toolRound;                          // 工具调用轮次计数（兜底防失控）
    QList<WebSearchResult> m_allSearchResults; ///< 累积所有轮次的搜索结果（供汇总卡片）
    QList<AgentToolCall> m_pendingToolCalls;  ///< 当前回合累积的工具调用记录（供 UI 持久化）
    QHash<QString, QString> m_skillToolMap;  ///< 技能工具名 → 技能 ID 映射（每次请求刷新）
    QList<AgentTask> m_taskList;             ///< AI 自维护任务清单（由 update_task_list 覆盖式更新）
    static constexpr int MAX_SEARCH_ROUNDS = 3; ///< 最大搜索轮次（聊天模式联网搜索用）
    static constexpr int MAX_TOOL_ROUNDS = 50;  ///< Agent 工具调用兜底上限（工作模式防失控）

    // --- 上下文压缩相关 ---
    static constexpr int CONTEXT_COMPRESS_THRESHOLD = 24000; ///< 触发压缩的 token 阈值（覆盖大多数 32K 模型）
    static constexpr int CONTEXT_KEEP_RECENT_COUNT = 6;      ///< 压缩时保留最近 N 条消息不压缩
    bool m_compressing = false; ///< 是否正在执行上下文压缩（避免压缩请求再次触发压缩）
    int m_sendGeneration = 0;   ///< 发送代际计数：丢弃压缩完成前已被新请求/停止取代的结果

    // --- 工作区限制 ---
    WorkspaceRestriction m_workspaceRestriction; ///< 工作区限制配置

    // --- 系统提示词缓存 ---
    QHash<AssistantMode, QString> m_systemPromptCache; ///< 按模式缓存的系统提示词
    bool m_promptsLoaded;                              ///< 提示词资源是否已加载
};