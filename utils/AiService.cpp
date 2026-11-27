/**
 * @file   AiService.cpp
 * @brief  AI 服务模块实现 - SSE 流式聊天
 * @author BlockBox Team
 * @date   2026-06-23
 */

#include "AiService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSet>
#include <QHash>
#include <QSharedPointer>
#include <QTimer>
#include <QPointer>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QThreadPool>

#include <algorithm>

#include "utils/SettingsManager.h"
#include "utils/GameLauncher.h"
#include "utils/VersionDownloader.h"
#include "utils/LoaderCompatibility.h"
#include "utils/ErrorAnalyzer.h"
#include "utils/ManifestCache.h"
#include "utils/mod/ModScanner.h"
#include "utils/mod/ModData.h"
#include "utils/mod/ModrinthAPI.h"
#include "utils/mod/CurseForgeAPI.h"
#include "utils/WorldBackupManager.h"
#include "utils/Saves/LevelDatEditor.h"
#include "utils/ServersDatIO.h"
#include "utils/McServerPinger.h"
#include "utils/DownloadTaskManager.h"
#include "utils/WorldMap/StructureFinder.h"
#include "utils/AiEndpointUrl.h"
#include "utils/CommandAssistant/LitematicReader.h"
#include "utils/GameScreenshot.h"
#include "utils/skill/SkillManager.h"
#include "utils/plugin/PluginManager.h"
#include "utils/ForumContentService.h"

namespace {

/// 把服务商基础端点补全为 /chat/completions 完整地址（实现见 utils/AiEndpointUrl.h，
/// 翻译服务也复用同一份规则，避免把 /v1 拼成 /v1/v1）。
QString buildChatCompletionsUrl(QString fullUrl)
{
    return AiEndpointUrl::buildChatCompletionsUrl(fullUrl);
}

} // namespace

AiService::AiService(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
    , m_searchManager(new QNetworkAccessManager(this))
    , m_searchReply(nullptr)
    , m_titleReply(nullptr)
    , m_optimizeReply(nullptr)
    , m_currentToolCallIndex(-1)
    , m_hasToolCalls(false)
    , m_toolRound(0)
    , m_promptsLoaded(false)
{
    // 注册元类型，确保跨线程信号槽（如 resourceDownloadRequested）能正确传递自定义结构体
    qRegisterMetaType<ModInfo>("ModInfo");
    qRegisterMetaType<ModVersionFile>("ModVersionFile");

    // 加载工作区限制配置
    auto *sm = SettingsManager::instance();
    m_workspaceRestriction.enabled = sm->getProperty(QStringLiteral("ai/workspaceRestriction/enabled"), false).toBool();
    m_workspaceRestriction.allowedDirectories = sm->getProperty(QStringLiteral("ai/workspaceRestriction/allowedDirectories")).toStringList();
    m_workspaceRestriction.rejectedDirectories = sm->getProperty(QStringLiteral("ai/workspaceRestriction/rejectedDirectories")).toStringList();
}

AiService::~AiService()
{
    stopStreaming();
    // 等待所有后台任务完成，避免 this 指针在任务执行期间被销毁
    QThreadPool::globalInstance()->waitForDone(5000); // 最多等待5秒
}

void AiService::setWorkspaceRestriction(const WorkspaceRestriction &restriction)
{
    m_workspaceRestriction = restriction;

    // 持久化到 SettingsManager
    auto *sm = SettingsManager::instance();
    sm->setProperty(QStringLiteral("ai/workspaceRestriction/enabled"), restriction.enabled);
    sm->setProperty(QStringLiteral("ai/workspaceRestriction/allowedDirectories"), restriction.allowedDirectories);
    sm->setProperty(QStringLiteral("ai/workspaceRestriction/rejectedDirectories"), restriction.rejectedDirectories);
}

bool AiService::isPathAllowed(const QString &path) const
{
    if (!m_workspaceRestriction.enabled)
    {
        return true;
    }

    QString normalizedPath = QDir::fromNativeSeparators(path).toLower();

    // 检查是否在拒绝列表中
    for (const QString &rejected : m_workspaceRestriction.rejectedDirectories)
    {
        QString normalizedRejected = QDir::fromNativeSeparators(rejected).toLower();
        if (normalizedPath.startsWith(normalizedRejected))
        {
            return false;
        }
    }

    // 如果允许列表为空，允许所有路径（仅拒绝列表生效）
    if (m_workspaceRestriction.allowedDirectories.isEmpty())
    {
        return true;
    }

    // 检查是否在允许列表中
    for (const QString &allowed : m_workspaceRestriction.allowedDirectories)
    {
        QString normalizedAllowed = QDir::fromNativeSeparators(allowed).toLower();
        if (normalizedPath.startsWith(normalizedAllowed))
        {
            return true;
        }
    }

    return false;
}

void AiService::sendMessage(const QList<ChatMessage>& history, const AiModel& model,
                            WebSearchMode mode, AssistantMode assistantMode,
                            const QString &customSystemPrompt)
{
    // 先中止上一个请求
    stopStreaming();

    // 暂存上下文用于工具调用循环
    m_pendingHistory = history;
    m_pendingModel = model;
    m_pendingMode = mode;
    m_pendingAssistantMode = assistantMode;
    m_toolRound = 0;
    m_pendingToolCalls.clear();
    m_allSearchResults.clear(); // 重置累积搜索结果
    m_turnToolPhase = QJsonArray(); // 新一轮对话：清空上一轮的工具循环请求消息
    m_pendingToolQueue = QJsonArray();
    m_toolQueuePos = 0;

    // 上下文压缩：仅工作模式 + 非压缩中 + 估算 token 超阈值时自动触发
    // 压缩是一次完整的模型请求，移到工作线程执行，完成后回到主线程继续发送，
    // 避免阻塞 UI 最长 30 秒
    if (assistantMode == AssistantMode::Work && !m_compressing)
    {
        const int estimatedTokens = estimateTokenCount(history);
        if (estimatedTokens > CONTEXT_COMPRESS_THRESHOLD)
        {
            const int generation = ++m_sendGeneration;
            emit contextCompressionStarted();
            m_compressing = true;
            const QList<ChatMessage> historyCopy = history;
            const AiModel modelCopy = model;
            QPointer<AiService> guard(this);
            (void)QtConcurrent::run([guard, generation, historyCopy, modelCopy, estimatedTokens, customSystemPrompt]() {
                QList<ChatMessage> compressed;
                if (guard)
                {
                    compressed = guard->compressContext(historyCopy, modelCopy);
                }
                if (guard)
                {
                    QMetaObject::invokeMethod(guard, [guard, generation, compressed, estimatedTokens, customSystemPrompt]() {
                        if (guard)
                        {
                            guard->finishSendMessageAfterCompression(generation, compressed, estimatedTokens, customSystemPrompt);
                        }
                    }, Qt::QueuedConnection);
                }
            });
            return;
        }
    }

    finishSendMessageAfterCompression(-1, QList<ChatMessage>(), 0, customSystemPrompt);
}

void AiService::finishSendMessageAfterCompression(int generation,
                                                  const QList<ChatMessage> &compressed,
                                                  int estimatedTokens,
                                                  const QString &customSystemPrompt)
{
    if (generation >= 0)
    {
        m_compressing = false;
        // 压缩期间用户已重新发送或停止，丢弃本次结果（新请求会自行判断是否再压缩）
        if (generation != m_sendGeneration)
        {
            return;
        }
        const int newTokens = compressed.isEmpty() ? estimatedTokens
                                                  : estimateTokenCount(compressed);
        const bool ok = !compressed.isEmpty();
        if (ok)
        {
            m_pendingHistory = compressed;
        }
        emit contextCompressionFinished(estimatedTokens, newTokens, ok);
    }
    const AssistantMode assistantMode = m_pendingAssistantMode;
    const WebSearchMode mode = m_pendingMode;
    const AiModel model = m_pendingModel;

    // 构建 messages 数组
    // 顺序：模式系统提示词 → 联网提示词 → 对话历史
    QJsonArray messagesArray;

    // 1. 注入系统提示词：优先使用用户从提示词库选择的自定义提示词
    QString modePrompt = customSystemPrompt.isEmpty()
        ? systemPromptForMode(assistantMode)
        : customSystemPrompt;
    if (!modePrompt.isEmpty())
    {
        QJsonObject sysObj;
        sysObj["role"] = "system";
        sysObj["content"] = modePrompt;
        messagesArray.append(sysObj);
    }

    // 2. 启用联网时注入联网 system 提示
    if (mode != WebSearchMode::Off)
    {
        QString sysPrompt = webSearchSystemPrompt(mode);
        if (!sysPrompt.isEmpty())
        {
            QJsonObject sysObj;
            sysObj["role"] = "system";
            sysObj["content"] = sysPrompt;
            messagesArray.append(sysObj);
        }
    }

    // 2.5 工作模式下注入已启用技能的说明（指导 AI 如何调用技能工具）
    if (assistantMode == AssistantMode::Work)
    {
        QString skillPrompt = SkillManager::instance()->enabledSkillsPrompt();
        if (!skillPrompt.isEmpty())
        {
            QJsonObject sysObj;
            sysObj["role"] = "system";
            sysObj["content"] = skillPrompt;
            messagesArray.append(sysObj);
        }

        // 2.6 工作模式下注入已启用插件提供的 AI 工具说明
        QString pluginPrompt = PluginManager::instance()->enabledAiToolsPrompt();
        if (!pluginPrompt.isEmpty())
        {
            QJsonObject sysObj;
            sysObj["role"] = "system";
            sysObj["content"] = pluginPrompt;
            messagesArray.append(sysObj);
        }
    }

    // 3. 追加对话历史（压缩成功时为压缩后的历史，早期消息已替换为摘要）
    // 缓存友好重放规则（最大化服务商隐式前缀缓存命中率）：
    // - roundTriggeredTools 的 assistant 消息是工具循环中间轮，其内容从未进入过
    //   模型请求（请求中的 assistant 消息 content 为空、只带 tool_calls），跳过；
    // - 携带 toolPhase 的消息先把上一轮工具循环的真实请求消息（assistant tool_calls
    //   + tool 结果）原样回放，再输出本条消息，使前缀与上一轮的请求逐 token 一致。
    for (const auto& msg : m_pendingHistory)
    {
        if (msg.role == QStringLiteral("assistant") && msg.roundTriggeredTools)
        {
            continue;
        }
        for (const QJsonValue &phaseVal : msg.toolPhase)
        {
            messagesArray.append(phaseVal);
        }
        QJsonObject msgObj;
        msgObj["role"] = msg.role;
        if (!msg.images.isEmpty())
        {
            // 带图片的消息：content 改用 OpenAI 多模态分段格式（文本 + image_url），
            // 图片以 data URL 内联，服务商不需要能访问本地文件
            QJsonArray parts;
            if (!msg.content.isEmpty())
            {
                QJsonObject textPart;
                textPart["type"] = QStringLiteral("text");
                textPart["text"] = msg.content;
                parts.append(textPart);
            }
            for (const ChatImage &image : msg.images)
            {
                if (image.dataUrl.isEmpty())
                {
                    continue;
                }
                QJsonObject urlObj;
                urlObj["url"] = image.dataUrl;
                QJsonObject imagePart;
                imagePart["type"] = QStringLiteral("image_url");
                imagePart["image_url"] = urlObj;
                parts.append(imagePart);
            }
            if (parts.isEmpty())
            {
                msgObj["content"] = msg.content;
            }
            else
            {
                msgObj["content"] = parts;
            }
        }
        else
        {
            msgObj["content"] = msg.content;
        }
        messagesArray.append(msgObj);
    }

    m_pendingMessages = messagesArray;

    // 判断是否需要附加工具：
    // - 工作模式：强制启用 Agent 工具集（不依赖联网开关）
    // - 聊天模式：仅在联网开关打开且服务商不原生支持联网时附加 web_search 工具
    bool withTools;
    if (assistantMode == AssistantMode::Work)
    {
        withTools = true;
    }
    else
    {
        withTools = (mode != WebSearchMode::Off) &&
                    !providerSupportsNativeSearch(model.apiUrl);
    }

    sendRequestInternal(messagesArray, model, withTools);
}

QString AiService::systemPromptForMode(AssistantMode mode)
{
    // 首次调用时从内置 JSON 资源加载两种模式的提示词并缓存
    if (!m_promptsLoaded)
    {
        QFile file(QStringLiteral(":/resources/ai_system_prompts.json"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            file.close();
            if (parseError.error == QJsonParseError::NoError && doc.isObject())
            {
                QJsonObject obj = doc.object();
                m_systemPromptCache.insert(AssistantMode::Chat,
                                          obj.value(QStringLiteral("chat")).toString());
                m_systemPromptCache.insert(AssistantMode::Work,
                                          obj.value(QStringLiteral("work")).toString());
            }
        }
        m_promptsLoaded = true;
    }

    // 优先返回用户自定义提示词
    QString custom = customSystemPrompt(mode);
    if (!custom.isEmpty())
        return custom;

    return m_systemPromptCache.value(mode);
}

QString AiService::customSystemPrompt(AssistantMode mode) const
{
    SettingsManager *settings = SettingsManager::instance();
    QString key = (mode == AssistantMode::Chat)
        ? QStringLiteral("ai/customSystemPrompt/chat")
        : QStringLiteral("ai/customSystemPrompt/work");
    return settings->getProperty(key, QString()).toString();
}

void AiService::setCustomSystemPrompt(AssistantMode mode, const QString &prompt)
{
    SettingsManager *settings = SettingsManager::instance();
    QString key = (mode == AssistantMode::Chat)
        ? QStringLiteral("ai/customSystemPrompt/chat")
        : QStringLiteral("ai/customSystemPrompt/work");
    settings->setProperty(key, prompt);
}

void AiService::resetSystemPromptToDefault(AssistantMode mode)
{
    SettingsManager *settings = SettingsManager::instance();
    QString key = (mode == AssistantMode::Chat)
        ? QStringLiteral("ai/customSystemPrompt/chat")
        : QStringLiteral("ai/customSystemPrompt/work");
    settings->setProperty(key, QString());
}

void AiService::sendRequestInternal(const QJsonArray &messages, const AiModel &model, bool withTools)
{
    // 重置流式状态
    m_buffer.clear();
    m_toolCallsAccumulator = QJsonArray();
    m_currentToolCallIndex = -1;
    m_hasToolCalls = false;
    m_lastUsage = TokenUsage();

    // 构建请求体
    QJsonObject requestBody;
    requestBody["model"] = model.id;
    requestBody["messages"] = messages;
    requestBody["stream"] = true;

    // 请求返回 usage 统计
    QJsonObject streamOptions;
    streamOptions["include_usage"] = true;
    requestBody["stream_options"] = streamOptions;

    if (withTools)
    {
        // 工作模式附加完整 Agent 工具集；聊天模式仅附加 web_search
        if (m_pendingAssistantMode == AssistantMode::Work)
        {
            applyAgentTools(requestBody);
        }
        else
        {
            applyLocalSearchTools(requestBody, m_pendingMode);
        }
    }

    // 推理模型：按服务商附加思考启用参数
    if (model.supportsThinking)
    {
        applyThinkingParams(requestBody, model);
    }

    QJsonDocument doc(requestBody);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 构建完整 URL（避免 /v1 重复拼接）
    QString fullUrl = buildChatCompletionsUrl(model.apiUrl);

    QNetworkRequest request{QUrl(fullUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + model.apiKey).toUtf8());

    m_currentReply = m_networkManager->post(request, jsonData);

    connect(m_currentReply, &QNetworkReply::readyRead, this, &AiService::onReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &AiService::onReplyFinished);
    connect(m_currentReply, &QNetworkReply::errorOccurred, this, &AiService::onErrorOccurred);
}

void AiService::applyThinkingParams(QJsonObject &body, const AiModel &model)
{
    const QString url = model.apiUrl.toLower();
    const QString id = model.id.toLower();

    // 思考强度映射（1=低 2=中 3=高；0 不会走到这里，调用方已过滤）
    static const QStringList effortNames = {QStringLiteral("low"),
                                            QStringLiteral("medium"),
                                            QStringLiteral("high")};
    const QString effort = effortNames.value(qBound(1, m_thinkingEffort, 3) - 1,
                                             QStringLiteral("medium"));

    // 智谱 GLM（GLM-4.6 / GLM-Z1 等）
    if (url.contains("bigmodel") || url.contains("zhipuai"))
    {
        QJsonObject thinking;
        thinking["type"] = QStringLiteral("enabled");
        body["thinking"] = thinking;
        return;
    }

    // 阿里通义千问（DashScope）
    if (url.contains("aliyuncs") || url.contains("dashscope"))
    {
        body["enable_thinking"] = true;
        return;
    }

    // 腾讯混元
    if (url.contains("tencent") || url.contains("hunyuan"))
    {
        body["enable_thinking"] = true;
        return;
    }

    // OpenAI o 系列：reasoning_effort 跟随思考强度选择框
    if (url.contains("openai.com"))
    {
        body["reasoning_effort"] = effort;
        return;
    }

    // 第三方托管的 Qwen/QwQ（如硅基流动）
    if (id.contains("qwq") || id.contains("qwen"))
    {
        body["enable_thinking"] = true;
        return;
    }

    // 其余服务商（DeepSeek-R1、Kimi K1 等）默认输出 reasoning_content，无需附加参数
}

QString AiService::webSearchSystemPrompt(WebSearchMode mode) const
{
    if (mode == WebSearchMode::Auto)
    {
        return QStringLiteral(
            "你是一个具备联网搜索能力的助手。当用户的问题涉及实时信息（如最新新闻、天气、"
            "价格、汇率、赛事比分、软件版本、最新数据等）时，请主动调用联网搜索获取最新、"
            "准确的信息后再回答；对于常识性问题或无需实时信息的问题，可以直接回答。"
            "若已通过联网获取到信息，请在回答中适当标注信息来源或检索时间。");
    }
    if (mode == WebSearchMode::Prefer)
    {
        return QStringLiteral(
            "你是一个具备联网搜索能力的助手。请优先使用联网搜索确认信息的准确性与时效性，"
            "特别是涉及数据、新闻、价格、版本号、日期、人物动态、技术文档等可能随时间变化"
            "的内容时，务必先联网检索再回答。仅在非常确定且信息长期稳定的情况下才直接回答。"
            "回答时请标注关键信息的来源链接或检索时间，便于用户核实。");
    }
    return {};
}

void AiService::applyWebSearchParams(QJsonObject &body, const QString &apiUrl, WebSearchMode mode)
{
    if (mode == WebSearchMode::Off)
    {
        return;
    }

    QString lowerUrl = apiUrl.toLower();

    // Kimi / Moonshot - enable_search
    if (lowerUrl.contains("moonshot.cn"))
    {
        body["enable_search"] = true;
        return;
    }

    // 智谱 GLM - tools: [{type: "web_search", web_search: {enable: true, search_result: true}}]
    if (lowerUrl.contains("bigmodel.cn"))
    {
        QJsonArray tools;
        QJsonObject tool;
        tool["type"] = "web_search";
        QJsonObject webSearch;
        webSearch["enable"] = true;
        webSearch["search_result"] = true;
        tool["web_search"] = webSearch;
        tools.append(tool);
        body["tools"] = tools;
        return;
    }

    // 阿里 Qwen / DashScope - enable_search
    if (lowerUrl.contains("aliyuncs.com"))
    {
        QJsonObject searchOptions;
        searchOptions["enable"] = true;
        searchOptions["search_mode"] = (mode == WebSearchMode::Prefer)
                                           ? QStringLiteral("performance_first")
                                           : QStringLiteral("auto");
        body["enable_search"] = searchOptions;
        return;
    }

    // 百度 ERNIE / 千帆 - enable_search
    if (lowerUrl.contains("baidubce.com"))
    {
        body["enable_search"] = true;
        return;
    }

    // OpenRouter - plugins: [{id: "web"}]
    if (lowerUrl.contains("openrouter.ai"))
    {
        QJsonArray plugins;
        QJsonObject plugin;
        plugin["id"] = "web";
        plugin["max_results"] = (mode == WebSearchMode::Prefer) ? 5 : 3;
        plugins.append(plugin);
        body["plugins"] = plugins;
        return;
    }

    // Perplexity 自带联网搜索能力，无需附加参数
    // 其他服务商（如 DeepSeek、OpenAI）暂不支持原生联网参数，
    // 仅依靠上方注入的 system 提示引导模型行为。
}

bool AiService::providerSupportsNativeSearch(const QString &apiUrl) const
{
    QString lowerUrl = apiUrl.toLower();
    // 已在 applyWebSearchParams 中处理的原生联网服务商
    return lowerUrl.contains("moonshot.cn") ||     // Kimi
           lowerUrl.contains("bigmodel.cn") ||     // GLM
           lowerUrl.contains("aliyuncs.com") ||    // Qwen
           lowerUrl.contains("baidubce.com") ||    // ERNIE
           lowerUrl.contains("openrouter.ai") ||   // OpenRouter
           lowerUrl.contains("perplexity.ai");     // Perplexity
}

void AiService::applyLocalSearchTools(QJsonObject &body, WebSearchMode mode)
{
    QJsonArray tools;
    QJsonObject tool;
    tool["type"] = "function";

    QJsonObject function;
    function["name"] = "web_search";

    // 根据模式调整工具描述强度，引导模型调用频率
    QString description;
    if (mode == WebSearchMode::Prefer)
    {
        description = QStringLiteral(
            "搜索互联网以获取最新、准确的信息。当问题涉及实时数据、新闻、价格、版本号、"
            "日期、人物动态、技术文档或任何可能随时间变化的内容时，务必优先调用此工具"
            "进行联网确认，再基于搜索结果回答。仅在非常确定且信息长期稳定时才不调用。");
    }
    else // Auto
    {
        description = QStringLiteral(
            "搜索互联网以获取最新信息。当用户问题涉及实时信息（如最新新闻、天气、价格、"
            "汇率、赛事比分、软件版本、最新数据等）时调用此工具；对于常识性或无需实时"
            "信息的问题可以直接回答。");
    }
    function["description"] = description;

    QJsonObject parameters;
    parameters["type"] = "object";
    QJsonObject queryProp;
    queryProp["type"] = "string";
    queryProp["description"] = QStringLiteral("搜索关键词，使用用户问题或提炼出的核心查询词");
    QJsonObject propertiesObj;
    propertiesObj["query"] = queryProp;
    parameters["properties"] = propertiesObj;
    QJsonArray required;
    required.append("query");
    parameters["required"] = required;

    function["parameters"] = parameters;
    tool["function"] = function;
    tools.append(tool);

    body["tools"] = tools;
    // 让模型自行决定是否调用工具；prefer 模式通过 system 提示加强引导
    body["tool_choice"] = "auto";
}

void AiService::stopStreaming()
{
    // 使用户停止/重新发送后，仍在等待压缩结果的请求被丢弃
    m_sendGeneration++;
    // 注意：abort() 会同步触发 error/finished 信号，进而调用
    // onReplyFinished/onSearchReplyFinished，这些槽函数会修改 m_currentReply/m_searchReply。
    // 若不先置空成员变量，abort() 返回后继续执行 deleteLater 会空指针解引用闪退。
    // 解决：先保存局部指针并置空成员，让槽函数的 reply != m_xxxReply 检查直接 return。
    if (m_currentReply)
    {
        QNetworkReply* reply = m_currentReply;
        m_currentReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    if (m_searchReply)
    {
        QNetworkReply* reply = m_searchReply;
        m_searchReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    // 标题生成请求是独立的非流式请求，用户主动停止或发起新对话时一并中止，
    // 避免旧标题回调干扰新对话（onTitleReplyFinished 会因 reply != m_titleReply 直接 return）
    if (m_titleReply)
    {
        QNetworkReply* reply = m_titleReply;
        m_titleReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    m_buffer.clear();
    m_toolCallsAccumulator = QJsonArray();
    m_hasToolCalls = false;
    m_toolRound = 0;
    m_pendingToolCalls.clear();
    m_allSearchResults.clear();
    m_turnToolPhase = QJsonArray(); // 中止后丢弃未完成回合的工具循环消息
    m_pendingToolQueue = QJsonArray();
    m_toolQueuePos = 0;
    m_pendingToolImages.clear();    // 未发送的工具画面一并丢弃，避免串到下个回合
}

void AiService::onReadyRead()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
    {
        return;
    }

    QByteArray newData = reply->readAll();
    m_buffer += QString::fromUtf8(newData);

    // 按换行分割，处理完整的 SSE 行
    QStringList lines = m_buffer.split('\n');

    // 最后一行可能不完整，保留在缓冲区
    m_buffer = lines.takeLast();

    for (const QString& line : lines)
    {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
        {
            continue;
        }

        if (trimmed.startsWith("data: "))
        {
            parseSseData(trimmed.mid(6));
        }
    }
}

void AiService::onReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || reply != m_currentReply)
    {
        return;
    }

    // 处理缓冲区中可能残留的数据
    if (!m_buffer.isEmpty())
    {
        QString trimmed = m_buffer.trimmed();
        if (trimmed.startsWith("data: "))
        {
            parseSseData(trimmed.mid(6));
        }
        m_buffer.clear();
    }

    bool hadError = (reply->error() != QNetworkReply::NoError);

    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    if (hadError)
    {
        // 错误由 onErrorOccurred 处理
        return;
    }

    // 若本次响应包含 tool_calls，进入工具调用循环（不发出 streamFinished）
    if (m_hasToolCalls && !m_toolCallsAccumulator.isEmpty())
    {
        // 兜底防失控：超过最大轮次强制收尾
        // 工作模式用 MAX_TOOL_ROUNDS（50），聊天模式联网搜索用 MAX_SEARCH_ROUNDS（3）
        int maxRounds = (m_pendingAssistantMode == AssistantMode::Work)
                            ? MAX_TOOL_ROUNDS
                            : MAX_SEARCH_ROUNDS;
        if (m_toolRound >= maxRounds)
        {
            emit streamFinished(m_lastUsage);
            return;
        }

        // 将 assistant 的 tool_calls 消息加入 messages 历史与缓存回放序列
        QJsonObject assistantMsg;
        assistantMsg["role"] = "assistant";
        assistantMsg["content"] = QStringLiteral(""); // 工具调用时 content 通常为空
        assistantMsg["tool_calls"] = m_toolCallsAccumulator;
        m_pendingMessages.append(assistantMsg);
        m_turnToolPhase.append(assistantMsg);

        // 队列化本响应的全部 tool_calls：逐个执行并回填结果，全部完成后
        // 才重新请求模型。保证每个 tool_call 都有对应 tool 结果消息（协议完整），
        // 且请求前缀只增不变（单次重放内缓存仍然连续）。
        m_pendingToolQueue = m_toolCallsAccumulator;
        m_toolQueuePos = 0;

        // 执行第一个 tool_call
        // （OpenAI 协议允许一次返回多个 tool_calls，此前仅执行首个导致
        //   其余 tool_call 无结果消息、模型被迫重复调用；现改为队列逐个执行）
        dispatchToolCall(m_pendingToolQueue[m_toolQueuePos].toObject());
        return;
    }

    // 正常完成流
    emit streamFinished(m_lastUsage);
}

void AiService::onErrorOccurred(QNetworkReply::NetworkError /*error*/)
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
    {
        return;
    }

    emit errorOccurred(reply->errorString());
}

void AiService::parseSseData(const QString& data)
{
    if (data == "[DONE]")
    {
        emit streamFinished(m_lastUsage);
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        return;
    }

    QJsonObject root = doc.object();

    // 解析 usage 统计（通常在最后一个 chunk 中，choices 为空数组）
    if (root.contains("usage"))
    {
        QJsonObject usage = root["usage"].toObject();
        m_lastUsage.promptTokens = usage["prompt_tokens"].toInt();
        m_lastUsage.completionTokens = usage["completion_tokens"].toInt();
        m_lastUsage.totalTokens = usage["total_tokens"].toInt();
        m_lastUsage.valid = (m_lastUsage.totalTokens > 0);

        // 前缀缓存命中统计（命中部分通常按更低价格计费且显著降低首 token 延迟）：
        // OpenAI/GLM/Kimi 等返回 prompt_tokens_details.cached_tokens，
        // DeepSeek 返回 prompt_cache_hit_tokens；prompt_tokens 均已包含命中部分
        int cachedTokens = usage["prompt_tokens_details"].toObject()["cached_tokens"].toInt(0);
        if (cachedTokens <= 0)
        {
            cachedTokens = usage["prompt_cache_hit_tokens"].toInt(0);
        }
        m_lastUsage.cachedTokens = qMax(0, cachedTokens);
    }

    QJsonArray choices = root["choices"].toArray();

    if (choices.isEmpty())
    {
        return;
    }

    QJsonObject firstChoice = choices[0].toObject();
    QJsonObject delta = firstChoice["delta"].toObject();

    // 提取内容增量
    if (delta.contains("content"))
    {
        QString content = delta["content"].toString();
        if (!content.isEmpty())
        {
            emit streamContentReceived(content);
        }
    }

    // 提取推理/思考增量（兼容 DeepSeek 的 reasoning_content 与 OpenAI o 系列的 reasoning）
    if (delta.contains("reasoning_content"))
    {
        QString reasoning = delta["reasoning_content"].toString();
        if (!reasoning.isEmpty())
        {
            emit streamReasoningReceived(reasoning);
        }
    }
    else if (delta.contains("reasoning"))
    {
        QString reasoning = delta["reasoning"].toString();
        if (!reasoning.isEmpty())
        {
            emit streamReasoningReceived(reasoning);
        }
    }

    // 提取 tool_calls 增量（OpenAI function calling 流式格式）
    if (delta.contains("tool_calls"))
    {
        m_hasToolCalls = true;
        QJsonArray deltaToolCalls = delta["tool_calls"].toArray();
        for (const QJsonValue &tcVal : deltaToolCalls)
        {
            QJsonObject tc = tcVal.toObject();
            int index = tc["index"].toInt(0);

            // 扩展累积数组到对应索引
            while (m_toolCallsAccumulator.size() <= index)
            {
                m_toolCallsAccumulator.append(QJsonObject());
            }

            QJsonObject existing = m_toolCallsAccumulator[index].toObject();

            // 合并 id（首个 chunk 提供）
            if (tc.contains("id"))
            {
                existing["id"] = tc["id"].toString();
            }
            // 合并 type
            if (tc.contains("type"))
            {
                existing["type"] = tc["type"].toString();
            }
            // 合并 function（name 与 arguments 都是增量）
            if (tc.contains("function"))
            {
                QJsonObject deltaFunc = tc["function"].toObject();
                QJsonObject existingFunc;
                if (existing.contains("function"))
                {
                    existingFunc = existing["function"].toObject();
                }

                if (deltaFunc.contains("name"))
                {
                    existingFunc["name"] = deltaFunc["name"].toString();
                }
                if (deltaFunc.contains("arguments"))
                {
                    // arguments 是增量字符串，需追加
                    QString existingArgs = existingFunc["arguments"].toString();
                    existingArgs += deltaFunc["arguments"].toString();
                    existingFunc["arguments"] = existingArgs;
                }
                existing["function"] = existingFunc;
            }

            m_toolCallsAccumulator[index] = existing;
        }
    }

    // 检测 finish_reason（部分服务商会标记 tool_calls 完成态）
    if (firstChoice.contains("finish_reason"))
    {
        QString finishReason = firstChoice["finish_reason"].toString();
        if (finishReason == "tool_calls")
        {
            m_hasToolCalls = true;
        }
    }
}

// ============================================================================
// 本地搜索工具调用实现
// ============================================================================

void AiService::executeWebSearch(const QString &query)
{
    // 中止可能存在的上一个搜索请求
    if (m_searchReply)
    {
        m_searchReply->abort();
        m_searchReply->deleteLater();
        m_searchReply = nullptr;
    }

    // 使用 Bing 搜索（中国境内可用），请求 HTML 结果页
    QUrl searchUrl("https://cn.bing.com/search");
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("q", query);
    urlQuery.addQueryItem("count", QStringLiteral("10"));
    urlQuery.addQueryItem("setlang", QStringLiteral("zh-CN"));
    searchUrl.setQuery(urlQuery);

    QNetworkRequest request(searchUrl);
    // 模拟浏览器 UA，避免被识别为爬虫返回简化页
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                     "AppleWebKit/537.36 (KHTML, like Gecko) "
                                     "Chrome/120.0.0.0 Safari/537.36"));
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");

    // 搜索超时 8 秒
    request.setTransferTimeout(8000);

    m_searchReply = m_searchManager->get(request);
    connect(m_searchReply, &QNetworkReply::finished, this, &AiService::onSearchReplyFinished);
}

void AiService::onSearchReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || reply != m_searchReply)
    {
        return;
    }

    QString query;
    QString searchResultsText;
    int resultCount = 0;
    bool hadError = (reply->error() != QNetworkReply::NoError);

    // 提取查询词与 tool_call id（取当前正在执行的 tool_call，多工具队列时可能非首项）
    QString toolCallId;
    {
        QJsonObject currentToolCall = currentDispatchedToolCall();
        if (!currentToolCall.isEmpty())
        {
            toolCallId = currentToolCall["id"].toString();
            QJsonObject functionObj = currentToolCall["function"].toObject();
            QString argumentsStr = functionObj["arguments"].toString();
            QJsonParseError parseErr;
            QJsonDocument argDoc = QJsonDocument::fromJson(argumentsStr.toUtf8(), &parseErr);
            if (parseErr.error == QJsonParseError::NoError && argDoc.isObject())
            {
                query = argDoc.object()["query"].toString();
            }
        }
    }

    QElapsedTimer timer;
    timer.start();

    QString searchTime = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    QList<WebSearchResult> results;

    if (!hadError)
    {
        QByteArray data = reply->readAll();
        QString html = QString::fromUtf8(data);
        results = parseBingSearchResults(html, query, searchTime, 5);
        resultCount = results.size();
    }

    m_searchReply->deleteLater();
    m_searchReply = nullptr;

    // 累积本轮结果供最终汇总卡片使用
    if (!results.isEmpty())
    {
        m_allSearchResults.append(results);
        emit webSearchResultsCollected(results);
    }

    // 格式化为模型可读文本
    bool searchOk = true;
    if (results.isEmpty())
    {
        searchResultsText = QStringLiteral("搜索失败或未获取到相关结果。请基于已有知识回答用户问题，"
                                           "并说明未能通过联网获取到最新信息。");
        resultCount = 0;
        searchOk = false;
    }
    else
    {
        searchResultsText = formatSearchResults(results);
    }

    emit webSearchFinished(resultCount);

    qint64 durationMs = timer.elapsed();
    QString summary = QStringLiteral("找到 %1 条结果").arg(resultCount);
    emit agentToolCallFinished(QStringLiteral("web_search"), summary, durationMs, searchOk);

    // 记录到本回合工具调用列表（供 UI 持久化）
    AgentToolCall rec;
    rec.name = QStringLiteral("web_search");
    rec.arguments = QStringLiteral("{\"query\": \"%1\"}").arg(query);
    rec.result = searchResultsText;
    rec.resultSummary = summary;
    rec.durationMs = durationMs;
    rec.success = searchOk;
    m_pendingToolCalls.append(rec);

    continueWithToolResult(toolCallId, QStringLiteral("web_search"), searchResultsText);
}

// ============================================================================
// 网页访问工具实现
// ============================================================================

void AiService::executeFetchWebpage(const QString &url, bool extractText, int maxLength)
{
    // 中止可能存在的上一个网页访问请求
    if (m_webpageReply)
    {
        m_webpageReply->abort();
        m_webpageReply->deleteLater();
        m_webpageReply = nullptr;
    }

    // 验证URL
    QUrl requestUrl(url);
    if (!requestUrl.isValid() || (!requestUrl.scheme().startsWith("http")))
    {
        QString errMsg = QStringLiteral("无效的URL: %1").arg(url);
        emit agentToolCallFinished(QStringLiteral("fetch_webpage"), errMsg, 0, false);
        
        AgentToolCall rec;
        rec.name = QStringLiteral("fetch_webpage");
        rec.arguments = QStringLiteral("{\"url\": \"%1\"}").arg(url);
        rec.result = errMsg;
        rec.resultSummary = errMsg;
        rec.success = false;
        rec.errorMessage = errMsg;
        m_pendingToolCalls.append(rec);
        
        QString toolCallId;
        if (!m_toolCallsAccumulator.isEmpty())
        {
            QJsonObject firstToolCall = m_toolCallsAccumulator[0].toObject();
            toolCallId = firstToolCall["id"].toString();
        }
        continueWithToolResult(toolCallId, QStringLiteral("fetch_webpage"), errMsg);
        return;
    }

    QNetworkRequest request(requestUrl);
    // 模拟浏览器 UA，避免被识别为爬虫
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                     "AppleWebKit/537.36 (KHTML, like Gecko) "
                                     "Chrome/120.0.0.0 Safari/537.36"));
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");

    // 设置超时 10 秒
    request.setTransferTimeout(10000);

    m_webpageReply = m_searchManager->get(request);
    connect(m_webpageReply, &QNetworkReply::finished, this, &AiService::onWebpageReplyFinished);
    
    // 存储参数供槽函数使用
    m_webpageExtractText = extractText;
    m_webpageMaxLength = maxLength;
    m_webpageUrl = url;
}

void AiService::onWebpageReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || reply != m_webpageReply)
    {
        return;
    }

    QString url = m_webpageUrl;
    bool extractText = m_webpageExtractText;
    int maxLength = m_webpageMaxLength;
    bool hadError = (reply->error() != QNetworkReply::NoError);
    QString webpageContent;
    
    QElapsedTimer timer;
    timer.start();

    if (!hadError)
    {
        QByteArray data = reply->readAll();
        QString html = QString::fromUtf8(data);
        
        if (extractText)
        {
            // 简单的HTML文本提取（去除标签）
            webpageContent = html;
            // 移除脚本和样式
            webpageContent.remove(QRegularExpression(QStringLiteral("<script[^>]*>[\\s\\S]*?</script>"), 
                                                    QRegularExpression::CaseInsensitiveOption));
            webpageContent.remove(QRegularExpression(QStringLiteral("<style[^>]*>[\\s\\S]*?</style>"), 
                                                    QRegularExpression::CaseInsensitiveOption));
            // 移除HTML标签
            webpageContent.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
            // 清理空白字符
            webpageContent.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
            webpageContent = webpageContent.trimmed();
        }
        else
        {
            webpageContent = html;
        }
        
        // 截断到最大长度
        if (webpageContent.length() > maxLength)
        {
            webpageContent = webpageContent.left(maxLength) + QStringLiteral("\n\n... [内容已截断]");
        }
    }

    m_webpageReply->deleteLater();
    m_webpageReply = nullptr;

    // 提取 tool_call id（取当前正在执行的 tool_call，多工具队列时可能非首项）
    QString toolCallId;
    {
        QJsonObject currentToolCall = currentDispatchedToolCall();
        if (!currentToolCall.isEmpty())
        {
            toolCallId = currentToolCall["id"].toString();
        }
    }

    bool fetchOk = true;
    QString summary;
    if (hadError)
    {
        webpageContent = QStringLiteral("网页访问失败: %1").arg(reply->errorString());
        fetchOk = false;
        summary = QStringLiteral("访问失败");
    }
    else
    {
        summary = QStringLiteral("获取到 %1 个字符").arg(webpageContent.length());
    }

    qint64 durationMs = timer.elapsed();
    emit agentToolCallFinished(QStringLiteral("fetch_webpage"), summary, durationMs, fetchOk);

    // 记录到本回合工具调用列表
    AgentToolCall rec;
    rec.name = QStringLiteral("fetch_webpage");
    rec.arguments = QStringLiteral("{\"url\": \"%1\"}").arg(url);
    rec.result = webpageContent;
    rec.resultSummary = summary;
    rec.durationMs = durationMs;
    rec.success = fetchOk;
    if (!fetchOk)
    {
        rec.errorMessage = webpageContent;
    }
    m_pendingToolCalls.append(rec);

    continueWithToolResult(toolCallId, QStringLiteral("fetch_webpage"), webpageContent);
}

void AiService::generateTitle(const QString& userMessage, const QString& aiReply,
                               const AiModel& model)
{
    // 若上一次标题请求仍在飞行，先中止（reply 已置空时 abort 同步触发的槽会直接 return）
    if (m_titleReply)
    {
        QNetworkReply* reply = m_titleReply;
        m_titleReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }

    // 构建非流式请求体
    QJsonObject requestBody;
    requestBody["model"] = model.id;
    requestBody["stream"] = false;

    // system 提示：要求模型仅输出简短标题，不带引号、不带多余解释
    // 用户语言需保持，故在提示中明确"与用户消息同语言"
    QJsonObject systemMsg;
    systemMsg["role"] = QStringLiteral("system");
    systemMsg["content"] = QStringLiteral(
        "你是一个对话标题生成器。请根据给定的用户消息和 AI 回复，"
        "为本轮对话生成一个简短、准确、概括性的标题。要求：\n"
        "1. 标题长度不超过 20 个字符；\n"
        "2. 与用户消息使用相同的语言；\n"
        "3. 只输出标题本身，不要包含引号、标点符号、前缀说明或换行；\n"
        "4. 不要使用「新对话」这类无信息量的占位词。");
    QJsonObject userMsg;
    userMsg["role"] = QStringLiteral("user");
    userMsg["content"] = QStringLiteral("用户消息：%1\n\nAI 回复：%2\n\n请生成对话标题：")
                             .arg(userMessage)
                             .arg(aiReply.left(500)); // 限制 AI 回复长度，避免请求过大

    QJsonArray messages;
    messages.append(systemMsg);
    messages.append(userMsg);
    requestBody["messages"] = messages;

    QJsonDocument doc(requestBody);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 构建完整 URL（与 sendRequestInternal 相同的拼接逻辑）
    QString fullUrl = buildChatCompletionsUrl(model.apiUrl);

    QNetworkRequest request{QUrl(fullUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + model.apiKey).toUtf8());

    m_titleReply = m_networkManager->post(request, jsonData);
    connect(m_titleReply, &QNetworkReply::finished, this, &AiService::onTitleReplyFinished);
}

void AiService::onTitleReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || reply != m_titleReply)
    {
        return;
    }

    // 失败时静默忽略（保持原"新对话"标题，不报错不打扰用户）
    if (reply->error() != QNetworkReply::NoError)
    {
        m_titleReply->deleteLater();
        m_titleReply = nullptr;
        return;
    }

    QByteArray data = reply->readAll();
    m_titleReply->deleteLater();
    m_titleReply = nullptr;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray choices = root.value("choices").toArray();
    if (choices.isEmpty())
    {
        return;
    }

    QJsonObject message = choices.at(0).toObject().value("message").toObject();
    QString title = message.value("content").toString().trimmed();

    // 清理可能存在的引号、换行、多余空白
    // 注意：「」『』等为多字节字符，C++ 字符字面量会被推断为 int 而非 char，
    // 必须用 QString 重载或 QChar(码点) 形式，否则 QString::remove 重载匹配失败。
    title.remove(QChar('"'));
    title.remove(QChar('\''));
    title.remove(QChar(0x300C)); // 「
    title.remove(QChar(0x300D)); // 」
    title.remove(QChar(0x300E)); // 『
    title.remove(QChar(0x300F)); // 』
    title.remove(QChar('\n'));
    title.remove(QChar('\r'));
    title = title.simplified();

    // 截断到合理长度（与旧逻辑一致：30 字符）
    if (title.length() > 30)
    {
        title = title.left(30) + QStringLiteral("...");
    }

    if (!title.isEmpty())
    {
        emit titleGenerated(title);
    }
}

void AiService::optimizePrompt(const QString &draft, const AiModel &model)
{
    // 若上一次优化请求仍在飞行，先中止（与 generateTitle 相同的模式）
    if (m_optimizeReply)
    {
        QNetworkReply* reply = m_optimizeReply;
        m_optimizeReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }

    // 构建非流式请求体
    QJsonObject requestBody;
    requestBody["model"] = model.id;
    requestBody["stream"] = false;

    QJsonObject systemMsg;
    systemMsg["role"] = QStringLiteral("system");
    systemMsg["content"] = QStringLiteral(
        "你是一个提示词优化助手。请将用户给出的提示词草稿改写为一条清晰、"
        "结构完整、信息充分的指令。要求：\n"
        "1. 保留草稿的全部核心意图与细节，不添加草稿之外的新需求；\n"
        "2. 补全缺失的背景信息时使用占位描述（如「某模组名」），不要编造具体事实；\n"
        "3. 语句通顺、条理清晰，可将长句拆分为带序号的要点；\n"
        "4. 直接输出优化后的提示词本身，不要任何解释、前缀或 Markdown 代码块包裹。");

    QJsonObject userMsg;
    userMsg["role"] = QStringLiteral("user");
    userMsg["content"] = QStringLiteral("请优化以下提示词：\n\n%1").arg(draft);

    QJsonArray messages;
    messages.append(systemMsg);
    messages.append(userMsg);
    requestBody["messages"] = messages;

    QJsonDocument doc(requestBody);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 构建完整 URL（与 generateTitle 相同的拼接逻辑）
    QString fullUrl = buildChatCompletionsUrl(model.apiUrl);

    QNetworkRequest request{QUrl(fullUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + model.apiKey).toUtf8());

    m_optimizeReply = m_networkManager->post(request, jsonData);
    connect(m_optimizeReply, &QNetworkReply::finished, this, &AiService::onOptimizeReplyFinished);
}

void AiService::onOptimizeReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || reply != m_optimizeReply)
    {
        return;
    }

    // 失败时静默忽略（按钮恢复可用即可，不打扰用户）
    if (reply->error() != QNetworkReply::NoError)
    {
        m_optimizeReply->deleteLater();
        m_optimizeReply = nullptr;
        return;
    }

    QByteArray data = reply->readAll();
    m_optimizeReply->deleteLater();
    m_optimizeReply = nullptr;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray choices = root.value("choices").toArray();
    if (choices.isEmpty())
    {
        return;
    }

    QJsonObject message = choices.at(0).toObject().value("message").toObject();
    QString optimized = message.value("content").toString().trimmed();
    // 去掉可能包裹整段内容的 Markdown 代码围栏
    if (optimized.startsWith(QStringLiteral("```")))
    {
        optimized.remove(0, 3);
        if (optimized.startsWith(QStringLiteral("markdown"), Qt::CaseInsensitive))
        {
            optimized.remove(0, 8);
        }
        if (optimized.endsWith(QStringLiteral("```")))
        {
            optimized.chop(3);
        }
        optimized = optimized.trimmed();
    }

    if (!optimized.isEmpty())
    {
        emit promptOptimized(optimized);
    }
}

QString AiService::formatSearchResults(const QList<WebSearchResult> &results) const
{
    if (results.isEmpty())
    {
        return {};
    }

    // 拼接最终结果文本，包含检索时间便于模型标注
    QString header = QStringLiteral("以下是针对用户查询的联网搜索结果（检索时间: %1）：\n\n")
                         .arg(results.first().searchTime);
    QStringList entries;
    for (int i = 0; i < results.size(); ++i)
    {
        const auto &r = results[i];
        QString entry = QStringLiteral("## ") + QString::number(i + 1) + QStringLiteral(". ") + r.title;
        if (!r.url.isEmpty())
        {
            entry += QStringLiteral("\n链接: ") + r.url;
        }
        if (!r.snippet.isEmpty())
        {
            entry += QStringLiteral("\n摘要: ") + r.snippet;
        }
        entries.append(entry);
    }
    return header + entries.join(QStringLiteral("\n\n"));
}

QList<WebSearchResult> AiService::parseBingSearchResults(const QString &html, const QString &query,
                                                          const QString &searchTime, int maxResults) const
{
    QList<WebSearchResult> results;
    if (html.isEmpty())
    {
        return results;
    }

    int captured = 0;

    // Bing 搜索结果项位于 <li class="b_algo">...</li> 中
    // 使用非贪婪匹配提取每个结果块
    static const QRegularExpression liRegex(
        QStringLiteral("<li\\s+class=\"b_algo\"[^>]*>(.*?)</li>"),
        QRegularExpression::DotMatchesEverythingOption);

    // 预编译所有正则表达式，避免在循环中重复编译
    static const QRegularExpression titleRegex(
        QStringLiteral("<h2[^>]*>\\s*<a\\s+[^>]*href=\"([^\"]+)\"[^>]*>(.*?)</a>"));
    static const QRegularExpression h2Regex(QStringLiteral("<h2[^>]*>(.*?)</h2>"));
    static const QRegularExpression captionRegex(
        QStringLiteral("<div[^>]*class=\"[^\"]*b_caption[^\"]*\"[^>]*>(.*?)</div>"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression pRegex(QStringLiteral("<p[^>]*>(.*?)</p>"),
                                  QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression paraRegex(
        QStringLiteral("<div[^>]*class=\"[^\"]*b_(?:paractl|lineage|algoSlug)[^\"]*\"[^>]*>(.*?)</div>"),
        QRegularExpression::DotMatchesEverythingOption);

    auto it = liRegex.globalMatch(html);
    while (it.hasNext() && captured < maxResults)
    {
        QRegularExpressionMatch match = it.next();
        QString block = match.captured(1);

        // 提取标题（<h2><a href="...">标题</a></h2> 或 <h2>标题</h2>）
        QString title;
        QString url;
        QRegularExpressionMatch titleMatch = titleRegex.match(block);
        if (titleMatch.hasMatch())
        {
            url = titleMatch.captured(1);
            title = titleMatch.captured(2);
        }
        else
        {
            // 回退：仅提取 <h2> 内文本
            QRegularExpressionMatch h2Match = h2Regex.match(block);
            if (h2Match.hasMatch())
            {
                title = h2Match.captured(1);
            }
        }

        // 提取摘要（<p> 或 class 含 b_caption/b_algoSlug 的容器）
        QString snippet;
        // 优先匹配 b_caption 内的段落
        QRegularExpressionMatch captionMatch = captionRegex.match(block);
        QString searchArea = captionMatch.hasMatch() ? captionMatch.captured(1) : block;

        QRegularExpressionMatch pMatch = pRegex.match(searchArea);
        if (pMatch.hasMatch())
        {
            snippet = pMatch.captured(1);
        }
        else
        {
            // 回退：提取 b_paractl 或其他文本容器
            QRegularExpressionMatch paraMatch = paraRegex.match(searchArea);
            if (paraMatch.hasMatch())
            {
                snippet = paraMatch.captured(1);
            }
        }

        // 清理 HTML 标签，转义 HTML 实体
        auto cleanText = [](const QString &text) -> QString {
            QString t = text;
            t.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
            t.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
            t.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
            t.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
            t.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
            t.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
            t.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
            return t.trimmed();
        };

        title = cleanText(title);
        snippet = cleanText(snippet);

        if (title.isEmpty() && snippet.isEmpty())
        {
            continue;
        }

        WebSearchResult result;
        result.query = query;
        result.title = title;
        result.url = url;
        result.snippet = snippet;
        result.searchTime = searchTime;
        results.append(result);
        captured++;
    }

    return results;
}

void AiService::continueWithToolResult(const QString &toolCallId, const QString &toolName,
                                       const QString &result, int maxLength)
{
    // 部分服务商不返回 tool_call id，生成一个占位 id
    QString id = toolCallId;
    if (id.isEmpty())
    {
        id = QStringLiteral("call_") + QString::number(m_toolRound);
    }

    // 构造 tool 角色消息，回填工具结果
    QJsonObject toolMsg;
    toolMsg["role"] = "tool";
    toolMsg["tool_call_id"] = id;
    toolMsg["name"] = toolName;
    toolMsg["content"] = truncateToolResult(result, maxLength);
    m_pendingMessages.append(toolMsg);
    m_turnToolPhase.append(toolMsg);

    // 增加工具调用轮次
    m_toolRound++;

    // 队列中还有未执行的 tool_call：继续执行（不重新请求模型），
    // 待全部 tool_call 都有结果后再统一重发
    ++m_toolQueuePos;
    if (m_toolQueuePos < m_pendingToolQueue.size())
    {
        dispatchToolCall(m_pendingToolQueue[m_toolQueuePos].toObject());
        return;
    }
    m_pendingToolQueue = QJsonArray();
    m_toolQueuePos = 0;

    // 本轮工具里取到的画面，等所有 tool 结果都回填完再作为一条 user 消息追加
    // （tool 消息必须紧跟 assistant 的 tool_calls，中间不能插别的角色）
    appendToolImagesMessage();

    // 重置 tool_calls 累积器，准备接收下一轮流式响应
    m_toolCallsAccumulator = QJsonArray();
    m_currentToolCallIndex = -1;
    m_hasToolCalls = false;

    // 重新请求模型：工作模式继续附加 Agent 工具集（允许多步调用），
    // 聊天模式按原有规则（轮次/联网开关/原生联网支持）决定
    bool withTools;
    if (m_pendingAssistantMode == AssistantMode::Work)
    {
        withTools = true;
    }
    else
    {
        withTools = (m_toolRound < MAX_SEARCH_ROUNDS) &&
                    (m_pendingMode != WebSearchMode::Off) &&
                    !providerSupportsNativeSearch(m_pendingModel.apiUrl);
    }

    sendRequestInternal(m_pendingMessages, m_pendingModel, withTools);
}

QString AiService::truncateToolResult(const QString &text, int maxLength) const
{
    if (text.size() <= maxLength)
    {
        return text;
    }
    return text.left(maxLength) + QStringLiteral("\n\n[... 结果已截断，原始长度 %1 字符 ...]")
                                      .arg(text.size());
}

bool AiService::extractToolImagePayload(const QString &raw, QString *text,
                                        QList<ChatImage> *images)
{
    if (raw.isEmpty() || images == nullptr)
    {
        return false;
    }

    // 快速排除：必须是 JSON 对象且提到 bb_image，避免把普通结果误判成图片载荷
    const QString trimmed = raw.trimmed();
    if (!trimmed.startsWith(QLatin1Char('{')) || !trimmed.contains(QStringLiteral("bb_image")))
    {
        return false;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
    {
        return false;
    }
    const QJsonObject obj = doc.object();

    QStringList urls;
    const QJsonValue imageVal = obj.value(QStringLiteral("bb_image"));
    if (imageVal.isString())
    {
        urls << imageVal.toString();
    }
    else if (imageVal.isArray())
    {
        for (const QJsonValue &v : imageVal.toArray())
        {
            if (v.isString())
            {
                urls << v.toString();
            }
        }
    }

    // 一次最多两张：再多既烧 token 也没多少信息增量
    constexpr int kMaxImagesPerTool = 2;
    for (const QString &rawUrl : urls)
    {
        if (images->size() >= kMaxImagesPerTool)
        {
            break;
        }

        const QString url = rawUrl.trimmed();
        if (url.isEmpty())
        {
            continue;
        }

        ChatImage image;
        if (url.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive))
        {
            // 只认 png / jpeg / webp，避免把任意 data URL 塞给服务商
            const QString lower = url.left(64).toLower();
            if (!lower.contains(QStringLiteral("png")) && !lower.contains(QStringLiteral("jpeg"))
                && !lower.contains(QStringLiteral("jpg")) && !lower.contains(QStringLiteral("webp")))
            {
                continue;
            }
            image.dataUrl = url;
        }
        else
        {
            // 本地图片路径：按截图同一套规则压成 JPEG data URL（长边 ≤1280）
            const QImage src(url);
            if (src.isNull())
            {
                continue;
            }
            image.dataUrl = GameScreenshot::toJpegDataUrl(src);
        }

        if (image.dataUrl.isEmpty() || !image.dataUrl.contains(QLatin1Char(',')))
        {
            continue;
        }
        images->append(image);
    }

    if (images->isEmpty())
    {
        return false;
    }

    QString body = obj.value(QStringLiteral("text")).toString().trimmed();
    if (body.isEmpty())
    {
        body = QStringLiteral("（工具返回了 %1 张画面）").arg(images->size());
    }
    if (text)
    {
        *text = body;
    }
    return true;
}

void AiService::appendToolImagesMessage()
{
    if (m_pendingToolImages.isEmpty())
    {
        return;
    }

    QJsonArray parts;
    QJsonObject textPart;
    textPart[QStringLiteral("type")] = QStringLiteral("text");
    textPart[QStringLiteral("text")] =
        QStringLiteral("上面工具返回的画面如下，请结合它继续判断。");
    parts.append(textPart);
    for (const ChatImage &image : m_pendingToolImages)
    {
        QJsonObject urlObj;
        urlObj[QStringLiteral("url")] = image.dataUrl;
        QJsonObject imagePart;
        imagePart[QStringLiteral("type")] = QStringLiteral("image_url");
        imagePart[QStringLiteral("image_url")] = urlObj;
        parts.append(imagePart);
    }

    QJsonObject msgObj;
    msgObj[QStringLiteral("role")] = QStringLiteral("user");
    msgObj[QStringLiteral("content")] = parts;

    // 同时进请求序列与本轮工具阶段：后者会让这些消息被原样回放，
    // 既保住服务商前缀缓存，后续轮次也还能"记得"看过这张图
    m_pendingMessages.append(msgObj);
    m_turnToolPhase.append(msgObj);
    m_pendingToolImages.clear();
}

// ============================================================================
// 上下文压缩
// ============================================================================

int AiService::estimateTokenCount(const QList<ChatMessage> &messages) const
{
    int tokens = 0;
    for (const auto &msg : messages)
    {
        // 每条消息固定开销（角色标记、分隔符等）
        tokens += 4;
        // content：中文字符×2，其他字符/4（近似）
        const QString &text = msg.content;
        for (const QChar &ch : text)
        {
            if (ch.unicode() > 127)
            {
                tokens += 2; // 中文/全角
            }
            else
            {
                tokens += 0; // 累积 ASCII 后统一除以 4
            }
        }
        int asciiCount = 0;
        for (const QChar &ch : text)
        {
            if (ch.unicode() <= 127)
            {
                asciiCount++;
            }
        }
        tokens += asciiCount / 4;
        // reasoning 内容也计入
        if (!msg.reasoning.isEmpty())
        {
            tokens += msg.reasoning.size() / 3;
        }
        // 工具循环消息（assistant tool_calls + tool 结果）实际会随请求发送，计入估算
        if (!msg.toolPhase.isEmpty())
        {
            tokens += estimateTokenCount(msg.toolPhase);
        }
        // 图片按张粗估（各服务商计费差异大，这里取一个偏保守的固定值）
        tokens += msg.images.size() * 800;
    }
    return tokens;
}

int AiService::estimateTokenCount(const QJsonArray &messages) const
{
    int tokens = 0;
    for (const QJsonValue &val : messages)
    {
        QJsonObject obj = val.toObject();
        tokens += 4;
        QString content = obj.value("content").toString();
        int asciiCount = 0;
        for (const QChar &ch : content)
        {
            if (ch.unicode() > 127)
            {
                tokens += 2;
            }
            else
            {
                asciiCount++;
            }
        }
        tokens += asciiCount / 4;
    }
    return tokens;
}

QList<ChatMessage> AiService::compressContext(const QList<ChatMessage> &history,
                                               const AiModel &model)
{
    if (history.size() <= CONTEXT_KEEP_RECENT_COUNT)
    {
        return {}; // 消息太少，无需压缩
    }

    // 1. 识别开头连续的 system 消息（系统提示词，需保留）
    int sysEnd = 0;
    while (sysEnd < history.size() && history[sysEnd].role == QStringLiteral("system"))
    {
        sysEnd++;
    }

    // 2. 保留最近 CONTEXT_KEEP_RECENT_COUNT 条"实际会发送"的消息
    // 工具循环中间轮的 assistant 消息（roundTriggeredTools）重放历史时会被跳过，
    // 不占窗口名额，避免有效上下文被其挤占
    int kept = 0;
    int recentStart = history.size();
    while (recentStart > sysEnd && kept < CONTEXT_KEEP_RECENT_COUNT)
    {
        --recentStart;
        const ChatMessage &winMsg = history[recentStart];
        if (winMsg.role == QStringLiteral("assistant") && winMsg.roundTriggeredTools)
        {
            continue;
        }
        kept++;
    }
    if (kept == 0 || recentStart <= sysEnd)
    {
        return {}; // 早期消息不足以压缩
    }

    // 3. 拼接早期消息（sysEnd 到 recentStart-1）为纯文本
    QStringList earlyTexts;
    for (int i = sysEnd; i < recentStart; ++i)
    {
        const ChatMessage &msg = history[i];
        QString roleLabel = (msg.role == QStringLiteral("user"))        ? QStringLiteral("用户")
                             : (msg.role == QStringLiteral("assistant")) ? QStringLiteral("助手")
                                                                         : QStringLiteral("系统");
        earlyTexts.append(QStringLiteral("[%1] %2").arg(roleLabel, msg.content));
        // 摘要指令要求保留工具关键返回结果，但工具结果只存在于 toolPhase 中，
        // 截断后附入摘要输入（完整结果过长，会挤占摘要请求上下文）
        for (const QJsonValue &phaseVal : msg.toolPhase)
        {
            const QJsonObject phaseMsg = phaseVal.toObject();
            if (phaseMsg.value("role").toString() != QStringLiteral("tool"))
            {
                continue;
            }
            QString resultText = phaseMsg.value("content").toString();
            if (resultText.size() > 600)
            {
                resultText = resultText.left(600) + QStringLiteral("…");
            }
            earlyTexts.append(QStringLiteral("[工具结果] %1").arg(resultText));
        }
    }
    QString earlyText = earlyTexts.join(QStringLiteral("\n\n"));

    // 4. 构造摘要请求：system 指令 + user 拼接文本
    // 使用独立的 QNetworkAccessManager 同步请求，避免干扰流式状态
    QNetworkAccessManager mgr;
    QJsonObject sysObj;
    sysObj["role"] = QStringLiteral("system");
    sysObj["content"] = QStringLiteral(
        "你是一个对话摘要助手。请对以下对话历史生成一份详细但简洁的摘要，"
        "重点保留对继续对话有帮助的信息，包括：\n"
        "- 已完成的工作与得出的结论\n"
        "- 当前正在处理的任务与进度\n"
        "- 已调用过的工具及其关键返回结果（如实例路径、模组清单、版本信息等）\n"
        "- 待完成的下一步\n"
        "摘要应足够全面以提供上下文，但也要足够简洁以便快速理解。"
        "用中文输出，不要寒暄。");
    QJsonObject userObj;
    userObj["role"] = QStringLiteral("user");
    userObj["content"] = QStringLiteral("请摘要以下对话历史：\n\n%1").arg(earlyText);
    QJsonArray msgs;
    msgs.append(sysObj);
    msgs.append(userObj);

    QJsonObject body;
    body["model"] = model.id;
    body["messages"] = msgs;
    body["stream"] = false; // 非流式，一次性返回
    // 摘要不需工具
    QJsonDocument doc(body);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 拼接完整 URL
    QString fullUrl = buildChatCompletionsUrl(model.apiUrl);

    QNetworkRequest request{QUrl(fullUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + model.apiKey).toUtf8());
    request.setTransferTimeout(30000); // 摘要请求 30s 超时

    QNetworkReply *reply = mgr.post(request, jsonData);

    // 同步等待（本函数在 QtConcurrent 工作线程中执行，阻塞不影响 UI 线程）
    QEventLoop loop;
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (!reply->isFinished())
    {
        // 超时
        reply->abort();
        reply->deleteLater();
        return {};
    }

    QByteArray respData = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseErr;
    QJsonDocument respDoc = QJsonDocument::fromJson(respData, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !respDoc.isObject())
    {
        return {};
    }
    QJsonObject respObj = respDoc.object();
    // 检查 API 错误
    if (respObj.contains("error"))
    {
        return {};
    }
    QJsonArray choices = respObj.value("choices").toArray();
    if (choices.isEmpty())
    {
        return {};
    }
    QString summary = choices[0].toObject()
                          .value("message")
                          .toObject()
                          .value("content")
                          .toString()
                          .trimmed();
    if (summary.isEmpty())
    {
        return {};
    }

    // 5. 构造压缩后的消息列表：[系统提示词] + [摘要 system 消息] + [最近 N 条]
    QList<ChatMessage> compressed;
    for (int i = 0; i < sysEnd; ++i)
    {
        compressed.append(history[i]);
    }
    ChatMessage summaryMsg;
    summaryMsg.role = QStringLiteral("system");
    summaryMsg.content = QStringLiteral("【对话历史摘要】\n%1").arg(summary);
    compressed.append(summaryMsg);
    for (int i = recentStart; i < history.size(); ++i)
    {
        compressed.append(history[i]);
    }
    return compressed;
}

// ============================================================================
// Agent 工具集定义与分发
// ============================================================================

void AiService::applyAgentTools(QJsonObject &body)
{
    // Agent 工具清单从内置 JSON 资源加载：
    //   :/resources/ai_agent_tools.json
    // 新增/修改工具只需编辑该 JSON,无需改动本函数。
    QFile file(QStringLiteral(":/resources/ai_agent_tools.json"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        return;
    }

    const QJsonArray toolDefs = doc.object().value(QStringLiteral("tools")).toArray();
    QJsonArray tools;
    for (const QJsonValue &v : toolDefs)
    {
        const QJsonObject def = v.toObject();
        QJsonObject tool;
        tool["type"] = QStringLiteral("function");
        QJsonObject function;
        function["name"] = def.value(QStringLiteral("name"));
        function["description"] = def.value(QStringLiteral("description"));
        function["parameters"] = def.value(QStringLiteral("parameters")).toObject();
        tool["function"] = function;
        tools.append(tool);
    }

    // 合并已启用技能（Skill）的工具定义，并刷新工具名 → 技能 ID 映射
    m_skillToolMap.clear();
    SkillManager *sm = SkillManager::instance();
    const QJsonArray skillTools = sm->enabledToolsJson();
    for (const QJsonValue &v : skillTools)
        tools.append(v);
    m_skillToolMap = sm->enabledToolMap();

    // 合并已启用插件（Plugin）提供的 AI 工具：与内置/技能工具重名时跳过，
    // 并刷新工具名 → 插件 ID 映射供 dispatchToolCall 分发
    QSet<QString> takenNames;
    for (const QJsonValue &v : tools) {
        takenNames.insert(v.toObject().value(QStringLiteral("function")).toObject()
                              .value(QStringLiteral("name")).toString());
    }
    m_pluginToolMap.clear();
    PluginManager *pm = PluginManager::instance();
    const QHash<QString, QString> pluginToolMap = pm->enabledAiToolMap();
    const QJsonArray pluginTools = pm->enabledAiToolsJson();
    for (const QJsonValue &v : pluginTools) {
        const QString toolName = v.toObject().value(QStringLiteral("function")).toObject()
                                     .value(QStringLiteral("name")).toString();
        const auto it = pluginToolMap.constFind(toolName);
        if (toolName.isEmpty() || it == pluginToolMap.constEnd() || takenNames.contains(toolName))
            continue;
        takenNames.insert(toolName);
        m_pluginToolMap.insert(toolName, it.value());
        tools.append(v);
    }

    body["tools"] = tools;
    body["tool_choice"] = QStringLiteral("auto");
}

QJsonObject AiService::currentDispatchedToolCall() const
{
    // 一次响应包含多个 tool_calls 时按队列逐个执行，异步回调据此定位当前调用
    if (!m_pendingToolQueue.isEmpty() && m_toolQueuePos < m_pendingToolQueue.size())
    {
        return m_pendingToolQueue[m_toolQueuePos].toObject();
    }
    if (!m_toolCallsAccumulator.isEmpty())
    {
        return m_toolCallsAccumulator[0].toObject();
    }
    return QJsonObject();
}

void AiService::dispatchToolCall(const QJsonObject &toolCall)
{
    QJsonObject functionObj = toolCall["function"].toObject();
    QString name = functionObj["name"].toString();
    QString argumentsStr = functionObj["arguments"].toString();
    QString toolCallId = toolCall["id"].toString();
    // 发出工具调用开始信号
    emit agentToolCallStarted(name, argumentsStr);

    // 解析参数
    QJsonObject args;
    QJsonParseError parseErr;
    QJsonDocument argDoc = QJsonDocument::fromJson(argumentsStr.toUtf8(), &parseErr);
    if (parseErr.error == QJsonParseError::NoError && argDoc.isObject())
    {
        args = argDoc.object();
    }

    // web_search 走原有的异步搜索流程（QNetworkAccessManager）
    if (name == QStringLiteral("web_search"))
    {
        QString query = args["query"].toString();
        if (query.isEmpty())
        {
            // 参数缺失，直接回填错误
            QString errMsg = QStringLiteral("web_search 缺少 query 参数");
            emit agentToolCallFinished(name, errMsg, 0, false);
            AgentToolCall rec;
            rec.name = name;
            rec.arguments = argumentsStr;
            rec.result = errMsg;
            rec.resultSummary = errMsg;
            rec.success = false;
            rec.errorMessage = errMsg;
            m_pendingToolCalls.append(rec);
            continueWithToolResult(toolCallId, name, errMsg);
            return;
        }
        emit webSearchStarted(query);
        executeWebSearch(query);
        return;
    }

    // fetch_webpage：访问指定URL并返回网页内容
    if (name == QStringLiteral("fetch_webpage"))
    {
        QString url = args["url"].toString();
        if (url.isEmpty())
        {
            // 参数缺失，直接回填错误
            QString errMsg = QStringLiteral("fetch_webpage 缺少 url 参数");
            emit agentToolCallFinished(name, errMsg, 0, false);
            AgentToolCall rec;
            rec.name = name;
            rec.arguments = argumentsStr;
            rec.result = errMsg;
            rec.resultSummary = errMsg;
            rec.success = false;
            rec.errorMessage = errMsg;
            m_pendingToolCalls.append(rec);
            continueWithToolResult(toolCallId, name, errMsg);
            return;
        }
        bool extractText = args["extract_text"].toBool(true);
        int maxLength = args["max_length"].toInt(10000);
        executeFetchWebpage(url, extractText, maxLength);
        return;
    }

    // 方块盒子论坛：搜索帖子 / 读取帖子详情。
    // 论坛服务（ForumContentService）的请求与回调都在主线程，不能像本地工具那样
    // 丢进 QtConcurrent 用 QEventLoop 等，改为「发起请求 → 回调里回填结果」的异步流程。
    if (name == QStringLiteral("search_forum"))
    {
        executeSearchForum(args);
        return;
    }
    if (name == QStringLiteral("get_forum_thread"))
    {
        executeGetForumThread(args);
        return;
    }
    // search_resources 的「插件 / 投影」类型落到方块盒子论坛：
    // 这两类原创资源只在社区里发布（插件分享 / 投影分享板块），CurseForge/Modrinth 上没有。
    if (name == QStringLiteral("search_resources"))
    {
        const QString contentType = args["content_type"].toString().trimmed().toLower();
        if (contentType == QStringLiteral("plugin") || contentType == QStringLiteral("schematic")
            || contentType == QStringLiteral("projection"))
        {
            executeSearchForum(args);
            return;
        }
    }

    // update_task_list：纯本地内存操作（覆盖 m_taskList + 发 taskListUpdated 信号），
    // 无需子线程，同步执行后回填确认信息
    if (name == QStringLiteral("update_task_list"))
    {
        QElapsedTimer timer;
        timer.start();
        QString result = executeUpdateTaskList(args);
        qint64 durationMs = timer.elapsed();

        QString summary = result.section('\n', 0, 0).trimmed();
        if (summary.isEmpty())
        {
            summary = QStringLiteral("完成");
        }
        if (summary.size() > 60)
        {
            summary = summary.left(60) + QStringLiteral("...");
        }

        emit agentToolCallFinished(name, summary, durationMs, true);

        AgentToolCall rec;
        rec.name = name;
        rec.arguments = argumentsStr;
        rec.result = result;
        rec.resultSummary = summary;
        rec.durationMs = durationMs;
        rec.success = true;
        m_pendingToolCalls.append(rec);

        continueWithToolResult(toolCallId, name, result);
        return;
    }

    // 本地工具：使用 QtConcurrent 子线程化执行避免阻塞 UI
    QElapsedTimer timer;
    timer.start();

    // 工作区限制：路径类工具统一在进线程前校验，越界直接回填错误并中止本次调用
    auto rejectDisallowedPath = [&](const QString &path) -> bool {
        if (path.isEmpty() || isPathAllowed(path))
        {
            return false;
        }
        QString errMsg = QStringLiteral("工作区限制：路径 %1 不在允许的工作区范围内").arg(path);
        emit agentToolCallFinished(name, errMsg, 0, false);
        AgentToolCall rec;
        rec.name = name;
        rec.arguments = argumentsStr;
        rec.result = errMsg;
        rec.resultSummary = errMsg;
        rec.success = false;
        rec.errorMessage = errMsg;
        m_pendingToolCalls.append(rec);
        continueWithToolResult(toolCallId, name, errMsg);
        return true;
    };

    // 通过 QtConcurrent::run 执行耗时本地工具
    QFuture<QString> future;
    if (name == QStringLiteral("list_instances"))
    {
        future = QtConcurrent::run([this]() { return this->executeListInstances(); });
    }
    else if (name == QStringLiteral("check_loader_compatibility"))
    {
        future = QtConcurrent::run([this, args]() {
            return this->executeCheckLoaderCompatibility(args);
        });
    }
    else if (name == QStringLiteral("get_game_versions"))
    {
        future = QtConcurrent::run([this]() { return this->executeGetGameVersions(); });
    }
    else if (name == QStringLiteral("analyze_crash_log"))
    {
        future = QtConcurrent::run([this, args]() {
            return this->executeAnalyzeCrashLog(args);
        });
    }
    else if (name == QStringLiteral("list_mods"))
    {
        // 工作区检查：验证 instance_path 是否在允许范围内
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeListMods(args);
        });
    }
    else if (name == QStringLiteral("detect_java"))
    {
        future = QtConcurrent::run([this]() { return this->executeDetectJava(); });
    }
    else if (name == QStringLiteral("get_settings"))
    {
        future = QtConcurrent::run([this]() { return this->executeGetSettings(); });
    }
    else if (name == QStringLiteral("read_litematic"))
    {
        // 工作区检查：验证 file_path 是否在允许范围内
        if (rejectDisallowedPath(args["file_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeReadLitematic(args);
        });
    }
    else if (name == QStringLiteral("search_resources"))
    {
        future = QtConcurrent::run([this, args]() {
            return this->executeSearchResources(args);
        });
    }
    else if (name == QStringLiteral("get_resource_detail"))
    {
        future = QtConcurrent::run([this, args]() {
            return this->executeGetResourceDetail(args);
        });
    }
    else if (name == QStringLiteral("list_resource_files"))
    {
        future = QtConcurrent::run([this, args]() {
            return this->executeListResourceFiles(args);
        });
    }
    else if (name == QStringLiteral("download_resource"))
    {
        // 工作区检查：验证 instance_path 是否在允许范围内
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeDownloadResource(args);
        });
    }
    else if (name == QStringLiteral("launch_game"))
    {
        // 工作区检查：验证 instance_path 是否在允许范围内
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeLaunchGame(args);
        });
    }
    else if (name == QStringLiteral("download_instance"))
    {
        // 工作区检查：验证 instance_path 是否在允许范围内
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeDownloadInstance(args);
        });
    }
    else if (name == QStringLiteral("modify_instance"))
    {
        // 工作区检查：验证 instance_path 是否在允许范围内
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeModifyInstance(args);
        });
    }
    else if (name == QStringLiteral("ask_user"))
    {
        future = QtConcurrent::run([this, args]() {
            return this->executeAskUser(args);
        });
    }
    else if (name == QStringLiteral("read_game_log"))
    {
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeReadGameLog(args);
        });
    }
    else if (name == QStringLiteral("read_crash_report"))
    {
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeReadCrashReport(args);
        });
    }
    else if (name == QStringLiteral("manage_mods"))
    {
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeManageMods(args);
        });
    }
    else if (name == QStringLiteral("check_mod_updates"))
    {
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeCheckModUpdates(args);
        });
    }
    else if (name == QStringLiteral("list_saves"))
    {
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeListSaves(args);
        });
    }
    else if (name == QStringLiteral("read_save_info"))
    {
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeReadSaveInfo(args);
        });
    }
    else if (name == QStringLiteral("manage_world_backup"))
    {
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeManageWorldBackup(args);
        });
    }
    else if (name == QStringLiteral("list_servers"))
    {
        if (rejectDisallowedPath(args["instance_path"].toString().trimmed()))
        {
            return;
        }
        future = QtConcurrent::run([this, args]() {
            return this->executeListServers(args);
        });
    }
    else if (name == QStringLiteral("ping_server"))
    {
        future = QtConcurrent::run([this, args]() {
            return this->executePingServer(args);
        });
    }
    else if (name == QStringLiteral("list_download_tasks"))
    {
        future = QtConcurrent::run([this]() {
            return this->executeListDownloadTasks();
        });
    }
    else if (name == QStringLiteral("find_structure"))
    {
        future = QtConcurrent::run([this, args]() {
            return this->executeFindStructure(args);
        });
    }
    else if (m_skillToolMap.contains(name))
    {
        // 技能（Skill）工具：通过 SkillManager 执行脚本或 HTTP 请求
        future = QtConcurrent::run([this, args, name]() {
            return this->executeSkillTool(name, args);
        });
    }
    else if (m_pluginToolMap.contains(name))
    {
        // 插件（Plugin）提供的 AI 工具：通过 PluginManager 执行脚本或 HTTP 请求
        future = QtConcurrent::run([this, args, name]() {
            return this->executePluginAiTool(name, args);
        });
    }
    else
    {
        // 未知工具
        QString errMsg = QStringLiteral("未知工具: %1").arg(name);
        emit agentToolCallFinished(name, errMsg, 0, false);
        AgentToolCall rec;
        rec.name = name;
        rec.arguments = argumentsStr;
        rec.result = errMsg;
        rec.resultSummary = errMsg;
        rec.success = false;
        rec.errorMessage = errMsg;
        m_pendingToolCalls.append(rec);
        continueWithToolResult(toolCallId, name, errMsg);
        return;
    }

    // 通过 QFutureWatcher 在子线程完成后回到主线程处理
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher, name, argumentsStr, toolCallId, timer]()
    {
        watcher->deleteLater();
        QString result = watcher->result();
        qint64 durationMs = timer.elapsed();

        // 工具返回的画面（bb_image）：拆成「文本结果 + 图片」两部分，
        // 图片不能走 tool 消息（内容必须是字符串），而是等本轮 tool 结果
        // 全部回填后，作为一条 user 消息附图发给模型
        QString toolText = result;
        QList<ChatImage> toolImages;
        if (extractToolImagePayload(result, &toolText, &toolImages))
        {
            result = toolText;
        }

        // 生成摘要（取结果第一行或前 60 字）
        QString summary = result.section('\n', 0, 0).trimmed();
        if (summary.isEmpty())
        {
            summary = QStringLiteral("完成");
        }
        if (summary.size() > 60)
        {
            summary = summary.left(60) + QStringLiteral("...");
        }
        if (!toolImages.isEmpty())
        {
            summary += QStringLiteral("（含 %1 张画面）").arg(toolImages.size());
        }

        emit agentToolCallFinished(name, summary, durationMs, true);

        // 记录到本回合工具调用列表
        AgentToolCall rec;
        rec.name = name;
        rec.arguments = argumentsStr;
        rec.result = result;
        rec.resultSummary = summary;
        rec.durationMs = durationMs;
        rec.success = true;
        m_pendingToolCalls.append(rec);

        if (!toolImages.isEmpty())
        {
            m_pendingToolImages.append(toolImages);
        }

        continueWithToolResult(toolCallId, name, result);
    });
    watcher->setFuture(future);
}

// ============================================================================
// 外部 MCP 客户端调用入口
// ============================================================================

QString AiService::executeExternalTool(const QString &name, const QJsonObject &args,
                                       QString *errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    // 与工作模式 Agent 工具共用工作区限制：越界路径直接拒绝，别让外部客户端绕过
    const QString instancePath = args.value(QStringLiteral("instance_path")).toString().trimmed();
    if (!instancePath.isEmpty() && !isPathAllowed(instancePath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("路径不在工作区允许范围内：%1").arg(instancePath);
        }
        return QString();
    }
    const QString filePath = args.value(QStringLiteral("file_path")).toString().trimmed();
    if (!filePath.isEmpty() && !isPathAllowed(filePath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("路径不在工作区允许范围内：%1").arg(filePath);
        }
        return QString();
    }

    // 纯本地只读工具
    if (name == QStringLiteral("list_instances"))
    {
        return executeListInstances();
    }
    if (name == QStringLiteral("check_loader_compatibility"))
    {
        return executeCheckLoaderCompatibility(args);
    }
    if (name == QStringLiteral("get_game_versions"))
    {
        return executeGetGameVersions();
    }
    if (name == QStringLiteral("analyze_crash_log"))
    {
        return executeAnalyzeCrashLog(args);
    }
    if (name == QStringLiteral("list_mods"))
    {
        return executeListMods(args);
    }
    if (name == QStringLiteral("detect_java"))
    {
        return executeDetectJava();
    }
    if (name == QStringLiteral("get_settings"))
    {
        return executeGetSettings();
    }
    if (name == QStringLiteral("read_litematic"))
    {
        return executeReadLitematic(args);
    }
    if (name == QStringLiteral("read_game_log"))
    {
        return executeReadGameLog(args);
    }
    if (name == QStringLiteral("read_crash_report"))
    {
        return executeReadCrashReport(args);
    }
    if (name == QStringLiteral("list_saves"))
    {
        return executeListSaves(args);
    }
    if (name == QStringLiteral("read_save_info"))
    {
        return executeReadSaveInfo(args);
    }
    if (name == QStringLiteral("list_servers"))
    {
        return executeListServers(args);
    }
    if (name == QStringLiteral("list_download_tasks"))
    {
        return executeListDownloadTasks();
    }
    if (name == QStringLiteral("find_structure"))
    {
        return executeFindStructure(args);
    }

    // 需要网络的只读工具（内部用 QEventLoop 等待，调用方需放在子线程）
    if (name == QStringLiteral("check_mod_updates"))
    {
        return executeCheckModUpdates(args);
    }
    if (name == QStringLiteral("ping_server"))
    {
        return executePingServer(args);
    }

    // 写操作（MCP 层已校验写入开关与 confirm，这里只负责执行）
    if (name == QStringLiteral("manage_mods"))
    {
        return executeManageMods(args);
    }
    if (name == QStringLiteral("manage_world_backup"))
    {
        return executeManageWorldBackup(args);
    }
    if (name == QStringLiteral("launch_game"))
    {
        return executeLaunchGame(args);
    }
    if (name == QStringLiteral("download_instance"))
    {
        return executeDownloadInstance(args);
    }
    if (name == QStringLiteral("modify_instance"))
    {
        return executeModifyInstance(args);
    }
    if (name == QStringLiteral("download_resource"))
    {
        return executeDownloadResource(args);
    }

    if (errorMessage)
    {
        *errorMessage = QStringLiteral("该工具未开放给外部 MCP 客户端：%1").arg(name);
    }
    return QString();
}

// ============================================================================
// Agent 本地工具执行函数实现
// ============================================================================

QString AiService::executeListInstances()
{
    auto *sm = SettingsManager::instance();
    QList<InstanceFolderInfo> folders = sm->getInstanceFolders();
    if (folders.isEmpty())
    {
        return QStringLiteral("尚未配置任何实例目录。请在启动器设置中添加实例文件夹。");
    }

    QStringList lines;
    lines.append(QStringLiteral("共 %1 个实例目录：").arg(folders.size()));
    for (const auto &folder : folders)
    {
        QString defaultMark = folder.isDefault ? QStringLiteral(" [默认]") : QString();
        lines.append(QStringLiteral("\n## %1%2").arg(folder.name, defaultMark));
        lines.append(QStringLiteral("路径: %1").arg(folder.path));

        // 扫描该目录下的子实例（含 .minecraft 或 versions 子目录）
        QDir dir(folder.path);
        if (!dir.exists())
        {
            lines.append(QStringLiteral("状态: 目录不存在"));
            continue;
        }

        QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        int instanceCount = 0;
        for (const QString &sub : subDirs)
        {
            QString subPath = dir.filePath(sub);
            QString versionJsonPath = subPath + QStringLiteral("/.minecraft/versions");
            QString versionJsonPath2 = subPath + QStringLiteral("/versions");
            // 简单判定：含 .minecraft 或 versions 目录视为实例
            if (QDir(versionJsonPath).exists() || QDir(versionJsonPath2).exists() ||
                QFile::exists(subPath + QStringLiteral("/.minecraft/launcher_profiles.json")))
            {
                instanceCount++;
                lines.append(QStringLiteral("  - %1").arg(sub));
            }
        }
        if (instanceCount == 0)
        {
            lines.append(QStringLiteral("  （未检测到子实例）"));
        }
    }
    return lines.join('\n');
}

QString AiService::executeCheckLoaderCompatibility(const QJsonObject &args)
{
    QString loader = args["loader"].toString().trimmed();
    QJsonArray installedArr = args["installed_loaders"].toArray();
    QStringList installed;
    for (const QJsonValue &v : installedArr)
    {
        installed.append(v.toString().trimmed());
    }

    if (loader.isEmpty())
    {
        return QStringLiteral("错误：缺少 loader 参数");
    }

    auto *lc = LoaderCompatibility::instance();
    LoaderCompatibilityResult result = lc->checkCompatibility(loader, installed);

    QStringList lines;
    lines.append(QStringLiteral("加载器: %1").arg(loader));
    lines.append(QStringLiteral("已安装: %1").arg(installed.isEmpty() ? QStringLiteral("（无）") : installed.join(", ")));
    lines.append(QStringLiteral("可否安装: %1").arg(result.canInstall ? QStringLiteral("是") : QStringLiteral("否")));
    lines.append(QStringLiteral("是否存在冲突: %1").arg(result.hasConflict ? QStringLiteral("是") : QStringLiteral("否")));
    lines.append(QStringLiteral("是否需要依赖: %1").arg(result.needsDependency ? QStringLiteral("是") : QStringLiteral("否")));
    if (!result.conflictLoaderName.isEmpty())
    {
        lines.append(QStringLiteral("冲突加载器: %1").arg(result.conflictLoaderName));
    }
    if (!result.dependencyLoaderName.isEmpty())
    {
        lines.append(QStringLiteral("依赖加载器: %1").arg(result.dependencyLoaderName));
    }
    if (!result.message.isEmpty())
    {
        lines.append(QStringLiteral("说明: %1").arg(result.message));
    }

    // 附加不兼容列表与依赖列表
    QStringList incompatible = lc->getIncompatibleLoaders(loader);
    if (!incompatible.isEmpty())
    {
        lines.append(QStringLiteral("\n%1 的不兼容加载器: %2").arg(loader, incompatible.join(", ")));
    }
    QStringList deps = lc->getRequiredDependencies(loader);
    if (!deps.isEmpty())
    {
        lines.append(QStringLiteral("%1 所需依赖: %2").arg(loader, deps.join(", ")));
    }

    return lines.join('\n');
}

QString AiService::executeGetGameVersions()
{
    auto *cache = ManifestCache::instance();
    if (!cache->isManifestValid())
    {
        return QStringLiteral("版本清单缓存无效或已过期。请先在启动器中刷新版本列表。");
    }

    QJsonObject manifest = cache->getManifest();
    QJsonArray versions = manifest["versions"].toArray();
    if (versions.isEmpty())
    {
        return QStringLiteral("版本清单为空。");
    }

    QStringList releases;
    QStringList snapshots;
    int total = versions.size();
    for (const QJsonValue &v : versions)
    {
        QJsonObject obj = v.toObject();
        QString id = obj["id"].toString();
        QString type = obj["type"].toString();
        if (type == QStringLiteral("release"))
        {
            releases.append(id);
        }
        else if (type == QStringLiteral("snapshot"))
        {
            snapshots.append(id);
        }
    }

    QStringList lines;
    lines.append(QStringLiteral("Mojang 官方版本清单（共 %1 个版本）").arg(total));
    lines.append(QStringLiteral("\n## Release 版本（%1 个）").arg(releases.size()));
    // 仅列前 30 个，避免过长
    int showCount = qMin(releases.size(), 30);
    for (int i = 0; i < showCount; ++i)
    {
        lines.append(releases[i]);
    }
    if (releases.size() > showCount)
    {
        lines.append(QStringLiteral("...（其余 %1 个省略）").arg(releases.size() - showCount));
    }
    lines.append(QStringLiteral("\n## Snapshot 版本（%1 个，仅列前 15 个）").arg(snapshots.size()));
    int snapShow = qMin(snapshots.size(), 15);
    for (int i = 0; i < snapShow; ++i)
    {
        lines.append(snapshots[i]);
    }
    if (snapshots.size() > snapShow)
    {
        lines.append(QStringLiteral("...（其余 %1 个省略）").arg(snapshots.size() - snapShow));
    }
    lines.append(QStringLiteral("\n最新版本: %1").arg(manifest["latest"].toObject()["release"].toString()));

    return lines.join('\n');
}

QString AiService::executeAnalyzeCrashLog(const QJsonObject &args)
{
    QString logText = args["log_text"].toString();
    if (logText.isEmpty())
    {
        return QStringLiteral("错误：缺少 log_text 参数");
    }

    auto *analyzer = ErrorAnalyzer::instance();
    // 崩溃关键信息通常在日志末尾，保留末尾而不是开头
    QString truncated = logText.size() > 8000 ? logText.right(8000) : logText;
    ErrorAnalyzer::ErrorInfo info = analyzer->analyzeLog(truncated);

    QStringList lines;
    lines.append(QStringLiteral("错误类型: %1").arg(analyzer->getErrorTypeName(info.errorType)));
    if (!info.errorCode.isEmpty())
    {
        lines.append(QStringLiteral("错误代码: %1").arg(info.errorCode));
    }
    if (!info.ruleName.isEmpty())
    {
        lines.append(QStringLiteral("命中规则: %1").arg(info.ruleName));
    }
    if (!info.cause.isEmpty())
    {
        lines.append(QStringLiteral("可能原因: %1").arg(info.cause));
    }
    if (!info.verify.isEmpty())
    {
        lines.append(QStringLiteral("验证方法: %1").arg(info.verify));
    }
    if (!info.errorMessage.isEmpty())
    {
        lines.append(QStringLiteral("错误信息: %1").arg(info.errorMessage));
    }
    if (!info.solution.isEmpty())
    {
        lines.append(QStringLiteral("\n## 修复建议"));
        lines.append(info.solution);
    }
    if (!info.evidence.isEmpty())
    {
        lines.append(QStringLiteral("\n## 日志证据"));
        lines.append(info.evidence.join('\n'));
    }
    if (!info.details.isEmpty())
    {
        lines.append(QStringLiteral("\n## 详细信息"));
        lines.append(info.details);
    }
    return lines.join('\n');
}

QString AiService::executeListMods(const QJsonObject &args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }

    ModScanner scanner;
    LocalModList modList;
    bool ok = scanner.scanModsSync(instancePath, modList);
    if (!ok)
    {
        return QStringLiteral("扫描失败：无法读取 %1 的 mods 目录").arg(instancePath);
    }

    if (modList.mods.isEmpty())
    {
        return QStringLiteral("实例 %1 的 mods 目录为空或不存在。").arg(instancePath);
    }

    QStringList lines;
    lines.append(QStringLiteral("实例路径: %1").arg(instancePath));
    lines.append(QStringLiteral("模组统计: 共 %1 个，启用 %2 个，禁用 %3 个")
                     .arg(modList.totalCount)
                     .arg(modList.enabledCount)
                     .arg(modList.disabledCount));
    lines.append(QStringLiteral("\n## 模组清单"));
    for (const ModInfo &mod : modList.mods)
    {
        QString name = mod.name.isEmpty() ? mod.fileName : mod.name;
        QString status = mod.enabled ? QStringLiteral("[启用]") : QStringLiteral("[禁用]");
        QString loader = mod.loaderType.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(mod.loaderType);
        QString version = mod.latestVersion.isEmpty() ? QString() : QStringLiteral(" v%1").arg(mod.latestVersion);
        lines.append(QStringLiteral("%1 %2%3%4").arg(status, name, version, loader));
    }
    return lines.join('\n');
}

QString AiService::executeDetectJava()
{
    auto *launcher = GameLauncher::instance();
    QList<GameLauncher::JavaInfo> javas = launcher->findAllJavaInstallations();

    // 同时读取启动器记录的 Java 安装列表
    auto *sm = SettingsManager::instance();
    QList<QPair<QString, QString>> recorded = sm->getJavaInstallations();

    QStringList lines;
    lines.append(QStringLiteral("检测到 %1 个 Java 安装：").arg(javas.size()));
    for (const auto &j : javas)
    {
        QString validMark = j.valid ? QStringLiteral("[有效]") : QStringLiteral("[无效]");
        lines.append(QStringLiteral("  %1 %2 - %3").arg(validMark, j.version, j.path));
    }

    lines.append(QStringLiteral("\n启动器记录的 Java 列表（%1 个）：").arg(recorded.size()));
    for (const auto &p : recorded)
    {
        lines.append(QStringLiteral("  %1 - %2").arg(p.second, p.first));
    }

    QString currentJava = sm->getJavaPath();
    if (!currentJava.isEmpty())
    {
        lines.append(QStringLiteral("\n当前默认 Java 路径: %1").arg(currentJava));
    }
    bool autoSelect = sm->getAutoSelectJava();
    lines.append(QStringLiteral("按游戏版本自动选择 Java: %1").arg(autoSelect ? QStringLiteral("已启用") : QStringLiteral("未启用")));

    return lines.join('\n');
}

QString AiService::executeGetSettings()
{
    auto *sm = SettingsManager::instance();
    QStringList lines;
    lines.append(QStringLiteral("## 启动器当前配置"));

    // 内存
    lines.append(QStringLiteral("\n### 内存分配"));
    lines.append(QStringLiteral("自动分配: %1").arg(sm->isAutoMemoryEnabled() ? QStringLiteral("是") : QStringLiteral("否")));
    lines.append(QStringLiteral("最大内存: %1 MB").arg(sm->getMaxMemory()));
    lines.append(QStringLiteral("最小内存: %1 MB").arg(sm->getMinMemory()));

    // 下载源
    lines.append(QStringLiteral("\n### 下载源"));
    auto downloadSrc = sm->getDownloadSource();
    QString srcName;
    switch (downloadSrc)
    {
    case DownloadSource::Official: srcName = QStringLiteral("官方"); break;
    case DownloadSource::BMCL: srcName = QStringLiteral("BMCLAPI"); break;
    case DownloadSource::Auto: srcName = QStringLiteral("自动"); break;
    }
    lines.append(QStringLiteral("总下载源: %1").arg(srcName));
    lines.append(QStringLiteral("下载线程数: %1").arg(sm->getDownloadThreadCount()));

    // 版本隔离
    lines.append(QStringLiteral("\n### 版本隔离"));
    lines.append(QStringLiteral("已启用: %1").arg(sm->isVersionIsolationEnabled() ? QStringLiteral("是") : QStringLiteral("否")));

    // 实例目录
    lines.append(QStringLiteral("\n### 实例目录"));
    QList<InstanceFolderInfo> folders = sm->getInstanceFolders();
    lines.append(QStringLiteral("共 %1 个").arg(folders.size()));
    for (const auto &f : folders)
    {
        lines.append(QStringLiteral("  - %1: %2%3").arg(f.name, f.path,
                                                            f.isDefault ? QStringLiteral(" [默认]") : QString()));
    }

    // 账户
    lines.append(QStringLiteral("\n### 账户"));
    QList<AccountInfo> accounts = sm->getAccounts();
    lines.append(QStringLiteral("共 %1 个账户").arg(accounts.size()));
    AccountInfo defaultAcc = sm->getDefaultAccount();
    if (!defaultAcc.username.isEmpty())
    {
        lines.append(QStringLiteral("默认账户: %1 (%2)").arg(defaultAcc.username, defaultAcc.type));
    }

    // GitHub 加速
    lines.append(QStringLiteral("\n### GitHub 加速"));
    lines.append(QStringLiteral("已启用: %1").arg(sm->isGithubAccelerationEnabled() ? QStringLiteral("是") : QStringLiteral("否")));

    return lines.join('\n');
}

// ============================================================================
// 资源 API 同步化封装（子线程 QEventLoop 阻塞等待信号，超时 15s 兜底）
// ============================================================================

namespace {
/// 文件大小格式化（文件局部辅助函数，供 executeXxx 共用）
QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
    return QStringLiteral("%1 GB").arg(bytes / (1024LL * 1024 * 1024));
}
} // namespace

ModSearchResult AiService::syncSearchCurseForge(const QString& query, const QString& classId,
                                                 const QString& gameVersion, int page, int pageSize,
                                                 QString& error)
{
    error.clear();
    ModSearchResult result;

    QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (apiKey.isEmpty() && CurseForgeAPI::localApiKey().isEmpty() && !CurseForgeAPI::mirrorEnabled())
    {
        error = QStringLiteral("CURSEFORGE_API_KEY_MISSING");
        return result;
    }

    CurseForgeAPI api; // 临时实例，线程亲和性为当前子线程
    api.setApiKey(apiKey);
    api.setClassId(classId);

    // MCIM 镜像设置
    QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    if (useMcim)
    {
        api.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/curseforge"));
    }

    QEventLoop loop;
    QTimer::singleShot(15000, &loop, [&]() { loop.quit(); }); // 15s 超时兜底

    QObject::connect(&api, &CurseForgeAPI::searchCompleted, &loop,
        [&](const ModSearchResult& r) { result = r; loop.quit(); });
    QObject::connect(&api, &CurseForgeAPI::searchFailed, &loop,
        [&](const QString& e) { error = e; loop.quit(); });

    api.searchMods(query, gameVersion, QString(), page, pageSize,
                    QStringLiteral("popularity"), QStringLiteral("desc"));
    loop.exec();

    return result;
}

ModSearchResult AiService::syncSearchModrinth(const QString& query, const QString& projectType,
                                                const QString& gameVersion, const QString& loader,
                                                int page, int pageSize, QString& error)
{
    error.clear();
    ModSearchResult result;

    ModrinthAPI api;
    api.setProjectType(projectType);

    // MCIM 镜像设置
    QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    if (useMcim)
    {
        api.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/modrinth"));
    }

    QEventLoop loop;
    QTimer::singleShot(15000, &loop, [&]() { loop.quit(); });

    QObject::connect(&api, &ModrinthAPI::searchCompleted, &loop,
        [&](const ModSearchResult& r) { result = r; loop.quit(); });
    QObject::connect(&api, &ModrinthAPI::searchFailed, &loop,
        [&](const QString& e) { error = e; loop.quit(); });

    // Modrinth 将 loader 视为 category，故将 loader 作为 categoryId 传入以走 facets 过滤
    api.searchMods(query, gameVersion, loader, page, pageSize,
                    QStringLiteral("relevance"), QStringLiteral("desc"));
    loop.exec();

    return result;
}

ModInfo AiService::syncFetchDetailCurseForge(const QString& projectId, QString& error)
{
    error.clear();
    ModInfo info;

    QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (apiKey.isEmpty() && CurseForgeAPI::localApiKey().isEmpty() && !CurseForgeAPI::mirrorEnabled())
    {
        error = QStringLiteral("CURSEFORGE_API_KEY_MISSING");
        return info;
    }

    CurseForgeAPI api;
    api.setApiKey(apiKey);

    QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    if (useMcim)
    {
        api.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/curseforge"));
    }

    QEventLoop loop;
    QTimer::singleShot(15000, &loop, [&]() { loop.quit(); });

    QObject::connect(&api, &CurseForgeAPI::modDetailReceived, &loop,
        [&](const ModInfo& d) { info = d; loop.quit(); });
    QObject::connect(&api, &CurseForgeAPI::modDetailFailed, &loop,
        [&](const QString& e) { error = e; loop.quit(); });

    api.fetchModDetail(projectId);
    loop.exec();

    return info;
}

ModInfo AiService::syncFetchDetailModrinth(const QString& projectId, QString& error)
{
    error.clear();
    ModInfo info;

    ModrinthAPI api;

    QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    if (useMcim)
    {
        api.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/modrinth"));
    }

    QEventLoop loop;
    QTimer::singleShot(15000, &loop, [&]() { loop.quit(); });

    QObject::connect(&api, &ModrinthAPI::modDetailReceived, &loop,
        [&](const ModInfo& d) { info = d; loop.quit(); });
    QObject::connect(&api, &ModrinthAPI::modDetailFailed, &loop,
        [&](const QString& e) { error = e; loop.quit(); });

    api.fetchModDetail(projectId);
    loop.exec();

    return info;
}

// ============================================================================
// Agent 资源类工具执行函数实现
// ============================================================================

QString AiService::executeSearchResources(const QJsonObject& args)
{
    QString query = args["query"].toString().trimmed();
    QString contentType = args["content_type"].toString().trimmed().toLower();
    QString gameVersion = args["game_version"].toString().trimmed();
    QString loader = args["loader"].toString().trimmed().toLower();
    int page = args["page"].toInt(0);
    QString source = args["source"].toString().trimmed().toLower();

    if (query.isEmpty())
    {
        return QStringLiteral("错误：缺少 query 参数");
    }
    if (contentType.isEmpty())
    {
        return QStringLiteral("错误：缺少 content_type 参数");
    }

    // 映射 content_type 到 cfClassId / mrProjectType
    QString cfClassId;
    QString mrProjectType;
    if (contentType == QStringLiteral("mod"))
    {
        cfClassId = QStringLiteral("6");
        mrProjectType = QStringLiteral("mod");
    }
    else if (contentType == QStringLiteral("resourcepack"))
    {
        cfClassId = QStringLiteral("12");
        mrProjectType = QStringLiteral("resourcepack");
    }
    else if (contentType == QStringLiteral("shaderpack"))
    {
        cfClassId = QStringLiteral("6552");
        mrProjectType = QStringLiteral("shader");
    }
    else if (contentType == QStringLiteral("datapack"))
    {
        cfClassId = QStringLiteral("4546");
        mrProjectType = QStringLiteral("datapack");
    }
    else if (contentType == QStringLiteral("modpack"))
    {
        cfClassId = QStringLiteral("4471");
        mrProjectType = QStringLiteral("modpack");
    }
    else if (contentType == QStringLiteral("world"))
    {
        // Modrinth 没有「世界存档」分类，只能查 CurseForge
        cfClassId = QStringLiteral("17");
        mrProjectType.clear();
    }
    else
    {
        return QStringLiteral("错误：content_type 必须是 mod/resourcepack/shaderpack/datapack/modpack/world 之一");
    }

    if (source.isEmpty())
    {
        source = QStringLiteral("auto");
    }

    const int pageSize = 20;

    QList<ModInfo> allResults;
    bool cfSkipped = false;
    bool cfFailed = false;
    QString cfError;
    bool mrFailed = false;
    QString mrError;

    bool queryCf = (source == QStringLiteral("curseforge") || source == QStringLiteral("auto"));
    bool queryMr = (source == QStringLiteral("modrinth") || source == QStringLiteral("auto"))
                   && !mrProjectType.isEmpty();
    if (source == QStringLiteral("modrinth") && mrProjectType.isEmpty())
    {
        mrFailed = true;
        mrError = QStringLiteral("Modrinth 没有这个资源类型（例如世界存档只能从 CurseForge 获取）");
    }

    if (queryCf)
    {
        ModSearchResult r = syncSearchCurseForge(query, cfClassId, gameVersion, page, pageSize, cfError);
        if (cfError == QStringLiteral("CURSEFORGE_API_KEY_MISSING"))
        {
            cfSkipped = true;
            cfError.clear();
        }
        else if (!cfError.isEmpty())
        {
            cfFailed = true;
        }
        else
        {
            for (const ModInfo& m : r.mods)
            {
                allResults.append(m);
            }
        }
    }

    if (queryMr)
    {
        ModSearchResult r = syncSearchModrinth(query, mrProjectType, gameVersion, loader, page, pageSize, mrError);
        if (!mrError.isEmpty())
        {
            mrFailed = true;
        }
        else
        {
            for (const ModInfo& m : r.mods)
            {
                allResults.append(m);
            }
        }
    }

    // auto 模式：合并去重（按项目名归一化小写比较），按 downloadCount 降序排序，截取前 pageSize 条
    if (source == QStringLiteral("auto") && !allResults.isEmpty())
    {
        QSet<QString> seenNames;
        QList<ModInfo> deduped;
        for (const ModInfo& m : allResults)
        {
            QString key = m.name.toLower().trimmed();
            if (key.isEmpty() || !seenNames.contains(key))
            {
                seenNames.insert(key);
                deduped.append(m);
            }
        }
        std::sort(deduped.begin(), deduped.end(),
            [](const ModInfo& a, const ModInfo& b) { return a.downloadCount > b.downloadCount; });
        if (deduped.size() > pageSize)
        {
            deduped = deduped.mid(0, pageSize);
        }
        allResults = deduped;
    }

    // 格式化输出
    QStringList lines;
    int total = allResults.size();
    int currentPageNum = page + 1;
    // 简单判断是否还有下一页：若返回条数 >= pageSize 则可能有下一页
    bool hasMore = (total >= pageSize);

    lines.append(QStringLiteral("## 资源搜索结果（共 %1 条，当前第 %2 页%3）")
                    .arg(total)
                    .arg(currentPageNum)
                    .arg(hasMore ? QStringLiteral("，可能有下一页") : QStringLiteral("，已到末页")));

    if (total == 0)
    {
        lines.append(QStringLiteral("未找到匹配结果。"));
    }
    else
    {
        int idx = 1;
        for (const ModInfo& m : allResults)
        {
            QString sourceTag = (m.source == QStringLiteral("curseforge"))
                                    ? QStringLiteral("[CF]")
                                    : QStringLiteral("[MR]");
            QString name = m.name.isEmpty() ? m.id : m.name;
            QString author = m.author.isEmpty() ? QStringLiteral("未知") : m.author;
            QString description = m.description;
            if (description.size() > 100)
            {
                description = description.left(100) + QStringLiteral("...");
            }
            QString loaders = m.loaders.isEmpty() ? QStringLiteral("未知") : m.loaders.join(QStringLiteral("/"));
            QString url = m.pageUrl.isEmpty() ? QStringLiteral("(无链接)") : m.pageUrl;

            lines.append(QStringLiteral("%1. %2 %3 (%4) - %5 | 下载量: %6 | 加载器: %7 | 链接: %8")
                            .arg(idx)
                            .arg(sourceTag)
                            .arg(name)
                            .arg(author)
                            .arg(description)
                            .arg(m.downloadCount)
                            .arg(loaders)
                            .arg(url));
            idx++;
        }
    }

    if (cfSkipped)
    {
        lines.append(QStringLiteral("\n注：CurseForge API Key 未配置，已仅查询 Modrinth"));
    }
    if (cfFailed)
    {
        lines.append(QStringLiteral("\n注：CurseForge 查询失败：%1").arg(cfError));
    }
    if (mrFailed)
    {
        lines.append(QStringLiteral("\n注：Modrinth 查询失败：%1").arg(mrError));
    }

    return lines.join('\n');
}

QString AiService::executeGetResourceDetail(const QJsonObject& args)
{
    QString projectId = args["project_id"].toString().trimmed();
    QString source = args["source"].toString().trimmed().toLower();
    QString contentType = args["content_type"].toString().trimmed();

    if (projectId.isEmpty())
    {
        return QStringLiteral("错误：缺少 project_id 参数");
    }
    if (source.isEmpty())
    {
        return QStringLiteral("错误：缺少 source 参数");
    }

    QString error;
    ModInfo info;

    if (source == QStringLiteral("curseforge"))
    {
        info = syncFetchDetailCurseForge(projectId, error);
    }
    else if (source == QStringLiteral("modrinth"))
    {
        info = syncFetchDetailModrinth(projectId, error);
    }
    else
    {
        return QStringLiteral("错误：source 必须是 curseforge 或 modrinth");
    }

    if (error == QStringLiteral("CURSEFORGE_API_KEY_MISSING"))
    {
        return QStringLiteral("CurseForge API Key 未配置，请在启动器设置中填入后再使用 CurseForge 数据源");
    }
    if (!error.isEmpty())
    {
        return QStringLiteral("获取详情失败：%1").arg(error);
    }
    if (!info.isValid())
    {
        return QStringLiteral("获取详情失败：返回的数据无效");
    }

    QStringList lines;
    lines.append(QStringLiteral("## 资源详情：%1").arg(info.name.isEmpty() ? info.id : info.name));
    lines.append(QStringLiteral("作者: %1").arg(info.author.isEmpty() ? QStringLiteral("未知") : info.author));
    lines.append(QStringLiteral("项目 ID: %1").arg(info.id));
    lines.append(QStringLiteral("数据源: %1").arg(source));
    if (!contentType.isEmpty())
    {
        lines.append(QStringLiteral("内容类型: %1").arg(contentType));
    }
    if (!info.latestVersion.isEmpty())
    {
        lines.append(QStringLiteral("最新版本: %1").arg(info.latestVersion));
    }
    lines.append(QStringLiteral("下载量: %1").arg(info.downloadCount));
    if (!info.pageUrl.isEmpty())
    {
        lines.append(QStringLiteral("页面链接: %1").arg(info.pageUrl));
    }

    // 描述（截断到 500 字）
    QString desc = info.detailedDescription.isEmpty() ? info.description : info.detailedDescription;
    if (desc.isEmpty())
    {
        lines.append(QStringLiteral("\n描述: (无)"));
    }
    else
    {
        if (desc.size() > 500)
        {
            desc = desc.left(500) + QStringLiteral("...");
        }
        lines.append(QStringLiteral("\n## 描述"));
        lines.append(desc);
    }

    // 截图链接列表
    if (!info.screenshots.isEmpty())
    {
        lines.append(QStringLiteral("\n## 截图"));
        int i = 1;
        for (const ModScreenshot& s : info.screenshots)
        {
            lines.append(QStringLiteral("%1. %2").arg(i).arg(s.url));
            i++;
        }
    }

    // 依赖列表
    if (!info.dependencies.isEmpty())
    {
        lines.append(QStringLiteral("\n## 依赖"));
        int i = 1;
        for (const ModDependency& d : info.dependencies)
        {
            QString req = d.isRequired ? QStringLiteral("[必需]") : QStringLiteral("[可选]");
            QString ver = d.version.isEmpty() ? QString() : QStringLiteral(" @ %1").arg(d.version);
            lines.append(QStringLiteral("%1. %2 %3%4").arg(i).arg(req, d.name, ver));
            i++;
        }
    }

    // 最近 10 条 versionFiles 摘要
    if (!info.versionFiles.isEmpty())
    {
        int showCount = qMin(info.versionFiles.size(), 10);
        lines.append(QStringLiteral("\n## 版本文件（最近 %1 条）").arg(showCount));
        for (int i = 0; i < showCount; ++i)
        {
            const ModVersionFile& f = info.versionFiles[i];
            QString sizeStr = formatFileSize(f.fileSize);
            QString timeStr = f.datePublished.toString(QStringLiteral("yyyy-MM-dd"));
            lines.append(QStringLiteral("%1. %2 | %3 | %4 | %5")
                            .arg(i + 1)
                            .arg(f.fileName)
                            .arg(sizeStr)
                            .arg(f.releaseType)
                            .arg(timeStr));
        }
    }

    return lines.join('\n');
}

QString AiService::executeListResourceFiles(const QJsonObject& args)
{
    QString projectId = args["project_id"].toString().trimmed();
    QString source = args["source"].toString().trimmed().toLower();
    QString gameVersion = args["game_version"].toString().trimmed();
    QString loader = args["loader"].toString().trimmed().toLower();
    QString releaseType = args["release_type"].toString().trimmed().toLower();

    if (projectId.isEmpty())
    {
        return QStringLiteral("错误：缺少 project_id 参数");
    }
    if (source.isEmpty())
    {
        return QStringLiteral("错误：缺少 source 参数");
    }

    QString error;
    ModInfo info;

    if (source == QStringLiteral("curseforge"))
    {
        info = syncFetchDetailCurseForge(projectId, error);
    }
    else if (source == QStringLiteral("modrinth"))
    {
        info = syncFetchDetailModrinth(projectId, error);
    }
    else
    {
        return QStringLiteral("错误：source 必须是 curseforge 或 modrinth");
    }

    if (error == QStringLiteral("CURSEFORGE_API_KEY_MISSING"))
    {
        return QStringLiteral("CurseForge API Key 未配置");
    }
    if (!error.isEmpty())
    {
        return QStringLiteral("获取详情失败：%1").arg(error);
    }
    if (!info.isValid())
    {
        return QStringLiteral("获取详情失败：返回的数据无效");
    }

    // 对 versionFiles 列表按 game_version / loader / release_type 逐级过滤
    QList<ModVersionFile> filtered;
    for (const ModVersionFile& f : info.versionFiles)
    {
        if (!gameVersion.isEmpty())
        {
            bool matchVersion = false;
            for (const QString& v : f.gameVersions)
            {
                if (v.compare(gameVersion, Qt::CaseInsensitive) == 0)
                {
                    matchVersion = true;
                    break;
                }
            }
            if (!matchVersion) continue;
        }
        if (!loader.isEmpty())
        {
            bool matchLoader = false;
            for (const QString& l : f.loaders)
            {
                if (l.compare(loader, Qt::CaseInsensitive) == 0)
                {
                    matchLoader = true;
                    break;
                }
            }
            if (!matchLoader) continue;
        }
        if (!releaseType.isEmpty())
        {
            if (f.releaseType.compare(releaseType, Qt::CaseInsensitive) != 0) continue;
        }
        filtered.append(f);
    }

    QStringList lines;
    lines.append(QStringLiteral("## 资源文件列表：%1").arg(info.name.isEmpty() ? info.id : info.name));
    lines.append(QStringLiteral("共 %1 个匹配文件").arg(filtered.size()));

    if (filtered.isEmpty())
    {
        lines.append(QStringLiteral("未找到匹配文件，请放宽过滤条件或检查 project_id"));
        return lines.join('\n');
    }

    int idx = 1;
    for (const ModVersionFile& f : filtered)
    {
        QString sizeStr = formatFileSize(f.fileSize);
        QString timeStr = f.datePublished.toString(QStringLiteral("yyyy-MM-dd"));
        QString versions = f.gameVersions.isEmpty() ? QStringLiteral("未知") : f.gameVersions.join(QStringLiteral("/"));
        QString loaders = f.loaders.isEmpty() ? QStringLiteral("未知") : f.loaders.join(QStringLiteral("/"));
        QString url = f.downloadUrl.isEmpty() ? QStringLiteral("(无链接)") : f.downloadUrl;

        lines.append(QStringLiteral("%1. %2 | %3 | %4 | %5 | 支持版本: %6 | 加载器: %7 | URL: %8")
                        .arg(idx)
                        .arg(f.fileName)
                        .arg(sizeStr)
                        .arg(f.releaseType)
                        .arg(timeStr)
                        .arg(versions)
                        .arg(loaders)
                        .arg(url));
        idx++;
    }

    return lines.join('\n');
}

QString AiService::executeDownloadResource(const QJsonObject& args)
{
    QString projectId = args["project_id"].toString().trimmed();
    QString source = args["source"].toString().trimmed().toLower();
    QString instancePath = args["instance_path"].toString().trimmed();
    QString fileId = args["file_id"].toString().trimmed();
    QString contentTypeStr = args["content_type"].toString().trimmed().toLower();
    QString gameVersion = args["game_version"].toString().trimmed();
    QString loader = args["loader"].toString().trimmed().toLower();

    if (projectId.isEmpty())
    {
        return QStringLiteral("错误：缺少 project_id 参数");
    }
    if (source.isEmpty())
    {
        return QStringLiteral("错误：缺少 source 参数");
    }
    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }

    // 校验 instance_path
    QDir instanceDir(instancePath);
    if (!instanceDir.exists())
    {
        return QStringLiteral("实例路径无效：%1，请先用 list_instances 查看可用实例").arg(instancePath);
    }

    // 映射 content_type 到 ContentType 枚举（默认 mod）
    ContentType typeEnum = ContentType::Mod;
    if (contentTypeStr.isEmpty() || contentTypeStr == QStringLiteral("mod"))
    {
        typeEnum = ContentType::Mod;
    }
    else if (contentTypeStr == QStringLiteral("resourcepack"))
    {
        typeEnum = ContentType::ResourcePack;
    }
    else if (contentTypeStr == QStringLiteral("shaderpack"))
    {
        typeEnum = ContentType::ShaderPack;
    }
    else if (contentTypeStr == QStringLiteral("datapack"))
    {
        typeEnum = ContentType::DataPack;
    }
    else if (contentTypeStr == QStringLiteral("modpack"))
    {
        typeEnum = ContentType::Modpack;
    }
    else if (contentTypeStr == QStringLiteral("world"))
    {
        typeEnum = ContentType::World;
    }
    else
    {
        return QStringLiteral("错误：content_type 必须是 mod/resourcepack/shaderpack/datapack/modpack/world 之一");
    }

    // 获取资源详情
    QString error;
    ModInfo info;
    if (source == QStringLiteral("curseforge"))
    {
        info = syncFetchDetailCurseForge(projectId, error);
    }
    else if (source == QStringLiteral("modrinth"))
    {
        info = syncFetchDetailModrinth(projectId, error);
    }
    else
    {
        return QStringLiteral("错误：source 必须是 curseforge 或 modrinth");
    }

    if (error == QStringLiteral("CURSEFORGE_API_KEY_MISSING"))
    {
        return QStringLiteral("CurseForge API Key 未配置");
    }
    if (!error.isEmpty())
    {
        return QStringLiteral("获取资源详情失败：%1").arg(error);
    }
    if (!info.isValid())
    {
        return QStringLiteral("获取资源详情失败：返回的数据无效");
    }
    if (info.versionFiles.isEmpty())
    {
        return QStringLiteral("该资源没有可用的版本文件");
    }

    // 选择文件
    ModVersionFile selectedFile;
    bool found = false;

    if (!fileId.isEmpty())
    {
        // 按 file_id 查找：优先匹配 version 字段，其次匹配 fileName 包含 fileId
        for (const ModVersionFile& f : info.versionFiles)
        {
            if (f.version == fileId)
            {
                selectedFile = f;
                found = true;
                break;
            }
        }
        if (!found)
        {
            for (const ModVersionFile& f : info.versionFiles)
            {
                if (f.fileName.contains(fileId, Qt::CaseInsensitive))
                {
                    selectedFile = f;
                    found = true;
                    break;
                }
            }
        }
        if (!found)
        {
            return QStringLiteral("未找到 file_id=%1 的文件，请用 list_resource_files 查看可用文件").arg(fileId);
        }
    }
    else
    {
        // 自动选择：先按 game_version + loader 过滤
        QList<ModVersionFile> candidates;
        for (const ModVersionFile& f : info.versionFiles)
        {
            bool match = true;
            if (!gameVersion.isEmpty())
            {
                bool matchVersion = false;
                for (const QString& v : f.gameVersions)
                {
                    if (v.compare(gameVersion, Qt::CaseInsensitive) == 0)
                    {
                        matchVersion = true;
                        break;
                    }
                }
                if (!matchVersion) match = false;
            }
            if (!loader.isEmpty())
            {
                bool matchLoader = false;
                for (const QString& l : f.loaders)
                {
                    if (l.compare(loader, Qt::CaseInsensitive) == 0)
                    {
                        matchLoader = true;
                        break;
                    }
                }
                if (!matchLoader) match = false;
            }
            if (match) candidates.append(f);
        }

        if (candidates.isEmpty())
        {
            // game_version 与 loader 都未传：直接选 versionFiles 首个（最新）
            if (gameVersion.isEmpty() && loader.isEmpty())
            {
                selectedFile = info.versionFiles.first();
                found = true;
            }
            else
            {
                QStringList conditions;
                if (!gameVersion.isEmpty()) conditions.append(QStringLiteral("版本 ") + gameVersion);
                if (!loader.isEmpty()) conditions.append(QStringLiteral("加载器 ") + loader);
                return QStringLiteral("未找到匹配 %1 的文件，请用 list_resource_files 查看可用文件")
                    .arg(conditions.join(QStringLiteral(" + ")));
            }
        }
        else
        {
            // 按 release_type 优先级（release > beta > alpha）选最新发布时间的文件
            std::sort(candidates.begin(), candidates.end(),
                [](const ModVersionFile& a, const ModVersionFile& b) {
                    int aPri = (a.releaseType == QStringLiteral("release")) ? 3
                                : (a.releaseType == QStringLiteral("beta")) ? 2 : 1;
                    int bPri = (b.releaseType == QStringLiteral("release")) ? 3
                                : (b.releaseType == QStringLiteral("beta")) ? 2 : 1;
                    if (aPri != bPri) return aPri > bPri;
                    return a.datePublished > b.datePublished;
                });
            selectedFile = candidates.first();
            found = true;
        }
    }

    if (!found)
    {
        return QStringLiteral("未找到匹配的下载文件，请用 list_resource_files 查看可用文件");
    }

    // 触发下载信号（不等待下载完成，立即返回）
    emit resourceDownloadRequested(info, selectedFile, instancePath, typeEnum);

    QString sizeStr = formatFileSize(selectedFile.fileSize);
    QString folderName = ContentTypeConfig::getConfig(typeEnum).folderName;

    QStringList lines;
    lines.append(QStringLiteral("已触发下载："));
    lines.append(QStringLiteral("- 文件名: %1").arg(selectedFile.fileName));
    lines.append(QStringLiteral("- 大小: %1").arg(sizeStr));
    lines.append(QStringLiteral("- 目标路径: %1/%2/%3").arg(instancePath, folderName, selectedFile.fileName));
    lines.append(QStringLiteral("- 请到下载页查看进度"));

    return lines.join('\n');
}

// ============================================================================
// 方块盒子论坛工具实现（主线程异步）
//
// 论坛接口封装在 ForumContentService 里，请求与回调都跑在主线程，所以这两个工具
// 不能像本地工具那样丢进 QtConcurrent 用 QEventLoop 阻塞等待；改为发起请求后
// 在回调里回填结果，工具循环（dispatch → continueWithToolResult）保持原样。
// ============================================================================

namespace {

/** 论坛板块：中文名 / 英文别名 → 服务端 slug（返回空串表示全站） */
QString normalizeForumBoardSlug(const QString &raw)
{
    const QString key = raw.trimmed().toLower();
    if (key.isEmpty() || key == QLatin1String("all") || key == QStringLiteral("全部"))
    {
        return QString();
    }

    static const QHash<QString, QString> kBoards = {
        // 社区原创资源：插件 / 投影 / 教程 / 有奖问答
        {QStringLiteral("plugin-share"), QStringLiteral("plugin-share")},
        {QStringLiteral("plugin"), QStringLiteral("plugin-share")},
        {QStringLiteral("插件"), QStringLiteral("plugin-share")},
        {QStringLiteral("插件分享"), QStringLiteral("plugin-share")},
        {QStringLiteral("schematic-share"), QStringLiteral("schematic-share")},
        {QStringLiteral("schematic"), QStringLiteral("schematic-share")},
        {QStringLiteral("projection"), QStringLiteral("schematic-share")},
        {QStringLiteral("投影"), QStringLiteral("schematic-share")},
        {QStringLiteral("投影分享"), QStringLiteral("schematic-share")},
        {QStringLiteral("tutorial-share"), QStringLiteral("tutorial-share")},
        {QStringLiteral("tutorial"), QStringLiteral("tutorial-share")},
        {QStringLiteral("教程"), QStringLiteral("tutorial-share")},
        {QStringLiteral("教程分享"), QStringLiteral("tutorial-share")},
        {QStringLiteral("qa"), QStringLiteral("qa")},
        {QStringLiteral("question"), QStringLiteral("qa")},
        {QStringLiteral("问答"), QStringLiteral("qa")},
        {QStringLiteral("有奖问答"), QStringLiteral("qa")},
        // 导航与普通讨论板块
        {QStringLiteral("server-nav"), QStringLiteral("server-nav")},
        {QStringLiteral("服务器导航"), QStringLiteral("server-nav")},
        {QStringLiteral("服务器"), QStringLiteral("server-nav")},
        {QStringLiteral("website-nav"), QStringLiteral("website-nav")},
        {QStringLiteral("网站导航"), QStringLiteral("website-nav")},
        {QStringLiteral("网站"), QStringLiteral("website-nav")},
        {QStringLiteral("general"), QStringLiteral("general")},
        {QStringLiteral("综合讨论"), QStringLiteral("general")},
        {QStringLiteral("chat"), QStringLiteral("chat")},
        {QStringLiteral("闲聊"), QStringLiteral("chat")},
        {QStringLiteral("help"), QStringLiteral("help")},
        {QStringLiteral("求助"), QStringLiteral("help")},
        {QStringLiteral("使用求助"), QStringLiteral("help")},
        {QStringLiteral("guide"), QStringLiteral("guide")},
        {QStringLiteral("教程与分享"), QStringLiteral("guide")},
        {QStringLiteral("feedback"), QStringLiteral("feedback")},
        {QStringLiteral("反馈"), QStringLiteral("feedback")},
        {QStringLiteral("意见建议"), QStringLiteral("feedback")},
        {QStringLiteral("plugin-dev"), QStringLiteral("plugin")},
        {QStringLiteral("插件与开发"), QStringLiteral("plugin")},
    };
    return kBoards.value(key);
}

/** 板块 slug → 中文名（搜索结果里给模型一个能读懂的范围说明） */
QString forumBoardDisplayName(const QString &slug)
{
    static const QHash<QString, QString> kNames = {
        {QStringLiteral("general"), QStringLiteral("综合讨论")},
        {QStringLiteral("help"), QStringLiteral("使用求助")},
        {QStringLiteral("guide"), QStringLiteral("教程与分享")},
        {QStringLiteral("plugin"), QStringLiteral("插件与开发")},
        {QStringLiteral("plugin-share"), QStringLiteral("插件分享")},
        {QStringLiteral("schematic-share"), QStringLiteral("投影分享")},
        {QStringLiteral("tutorial-share"), QStringLiteral("教程分享")},
        {QStringLiteral("website-nav"), QStringLiteral("网站导航")},
        {QStringLiteral("server-nav"), QStringLiteral("服务器导航")},
        {QStringLiteral("qa"), QStringLiteral("有奖问答")},
        {QStringLiteral("feedback"), QStringLiteral("意见建议")},
        {QStringLiteral("chat"), QStringLiteral("闲聊灌水")},
    };
    return kNames.value(slug, slug);
}

/** 帖子类型：优先看帖子上挂的作品信息，其次按板块兜底 */
QString forumThreadKindText(const ForumThreadInfo &info)
{
    if (info.plugin.isValid())
        return QStringLiteral("插件");
    if (info.schematic.isValid())
        return QStringLiteral("投影");
    if (info.tutorial)
        return QStringLiteral("教程");
    if (info.question.isValid())
        return QStringLiteral("有奖问答");
    if (info.navigation.isValid())
        return info.navigation.isServer() ? QStringLiteral("服务器导航") : QStringLiteral("网站导航");
    if (info.boardSlug == QLatin1String("plugin-share"))
        return QStringLiteral("插件");
    if (info.boardSlug == QLatin1String("schematic-share"))
        return QStringLiteral("投影");
    if (info.boardSlug == QLatin1String("tutorial-share"))
        return QStringLiteral("教程");
    if (info.boardSlug == QLatin1String("qa"))
        return QStringLiteral("有奖问答");
    if (info.boardSlug == QLatin1String("server-nav"))
        return QStringLiteral("服务器导航");
    if (info.boardSlug == QLatin1String("website-nav"))
        return QStringLiteral("网站导航");
    return QStringLiteral("帖子");
}

/** 帖子网页版地址：软件内的 LinkRouter 能识别 thread.html?id=xxx 并就地打开 */
QString forumThreadWebUrl(int threadId)
{
    if (threadId <= 0)
        return QString();
    auto *account = ForumAccountService::instance();
    const QString base = account ? account->baseUrl() : QString();
    if (base.isEmpty())
        return QString();
    return base + QStringLiteral("/thread.html?id=%1").arg(threadId);
}

/** 秒级时间戳 → yyyy-MM-dd（0 返回空串） */
QString formatForumTime(qint64 seconds)
{
    if (seconds <= 0)
        return QString();
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd"));
}

/** 文件大小（论坛接口给的是字节数） */
QString formatForumSize(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("大小未知");
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}

/** 标签拼成 #a、#b，帖子上没有标签时返回空串 */
QString forumTagsText(const ForumThreadInfo &info)
{
    QStringList parts;
    for (const QString &tag : info.tags)
    {
        const QString trimmed = tag.trimmed();
        if (!trimmed.isEmpty())
            parts << QStringLiteral("#") + trimmed;
    }
    return parts.join(QStringLiteral("、"));
}

/** 一句话摘要：压掉换行，超长截断 */
QString forumBrief(const QString &text, int maxChars)
{
    QString flat = text;
    flat.replace(QLatin1Char('\r'), QLatin1Char(' '));
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    flat = flat.simplified();
    if (flat.size() > maxChars)
        flat = flat.left(maxChars) + QStringLiteral("…");
    return flat;
}

/** 主题列表 → 模型可读文本 */
QString formatForumThreadList(const QString &query, const QString &scope,
                              const QVector<ForumThreadInfo> &threads,
                              const ForumPagination &pagination)
{
    QStringList lines;
    lines.append(QStringLiteral("## 方块盒子论坛搜索结果"));
    QStringList scopeParts;
    scopeParts << (scope.isEmpty() ? QStringLiteral("全站") : scope);
    if (!query.isEmpty())
        scopeParts << QStringLiteral("关键词「%1」").arg(query);
    else
        scopeParts << QStringLiteral("未填关键词，按板块 / 排序浏览");
    lines.append(QStringLiteral("范围：%1").arg(scopeParts.join(QStringLiteral(" · "))));
    lines.append(QStringLiteral("共 %1 条，第 %2/%3 页")
                     .arg(pagination.total > 0 ? pagination.total : threads.size())
                     .arg(pagination.page)
                     .arg(pagination.pages));
    lines.append(QString());

    if (threads.isEmpty())
    {
        lines.append(QStringLiteral("没有找到匹配的帖子。可以换关键词、去掉板块限制，或改用他人常用的叫法再搜一次。"));
        return lines.join(QLatin1Char('\n'));
    }

    int index = 1;
    for (const ForumThreadInfo &info : threads)
    {
        const QString title = info.title.trimmed().isEmpty() ? QStringLiteral("(无标题)")
                                                             : info.title.trimmed();
        lines.append(QStringLiteral("%1. 【%2】%3")
                         .arg(index++)
                         .arg(forumThreadKindText(info), title));

        QStringList meta;
        if (!info.boardName.isEmpty())
            meta << info.boardName;
        if (!info.authorName.trimmed().isEmpty())
            meta << QStringLiteral("作者 %1").arg(info.authorName.trimmed());
        const QString published = formatForumTime(info.createdAt);
        if (!published.isEmpty())
            meta << QStringLiteral("发布于 %1").arg(published);
        if (!meta.isEmpty())
            lines.append(QStringLiteral("   %1").arg(meta.join(QStringLiteral(" · "))));

        if (info.plugin.isValid())
        {
            QString pluginLine = info.plugin.name.trimmed();
            if (!info.plugin.version.trimmed().isEmpty())
                pluginLine += QStringLiteral(" v") + info.plugin.version.trimmed();
            pluginLine += QStringLiteral("（%1，下载 %2 次）")
                              .arg(formatForumSize(info.plugin.fileSize))
                              .arg(info.plugin.downloads);
            lines.append(QStringLiteral("   插件包：%1").arg(pluginLine));
        }
        if (info.schematic.isValid())
        {
            QString schematicLine = info.schematic.name.trimmed();
            if (schematicLine.isEmpty())
                schematicLine = info.schematic.fileName.trimmed();
            schematicLine += QStringLiteral("（%1 个版本，%2，下载 %3 次）")
                                 .arg(info.schematic.versionCount())
                                 .arg(formatForumSize(info.schematic.fileSize))
                                 .arg(info.schematic.downloads);
            lines.append(QStringLiteral("   投影：%1").arg(schematicLine));
        }
        if (info.question.isValid())
        {
            QString statusText = QStringLiteral("未知");
            if (info.question.isOpen())
                statusText = QStringLiteral("托管中");
            else if (info.question.isSettled())
                statusText = QStringLiteral("已发放");
            else if (info.question.status == QLatin1String("refunded"))
                statusText = QStringLiteral("已退回");
            lines.append(QStringLiteral("   悬赏：%1 方块 ｜ 状态：%2")
                             .arg(info.question.bounty)
                             .arg(statusText));
        }
        if (info.navigation.isValid())
        {
            lines.append(QStringLiteral("   导航：%1 → %2")
                             .arg(info.navigation.isServer() ? QStringLiteral("服务器")
                                                             : QStringLiteral("网站"),
                                  info.navigation.url));
        }

        const QString tags = forumTagsText(info);
        if (!tags.isEmpty())
            lines.append(QStringLiteral("   标签：%1").arg(tags));

        QStringList stats;
        stats << QStringLiteral("回复 %1").arg(info.replies);
        stats << QStringLiteral("浏览 %1").arg(info.views);
        const QString lastPost = formatForumTime(info.lastPostAt);
        if (!lastPost.isEmpty())
            stats << QStringLiteral("最后回复 %1").arg(lastPost);
        lines.append(QStringLiteral("   %1").arg(stats.join(QStringLiteral(" ｜ "))));

        const QString snippet = forumBrief(info.snippet, 160);
        if (!snippet.isEmpty())
            lines.append(QStringLiteral("   摘要：%1").arg(snippet));

        const QString url = forumThreadWebUrl(info.id);
        lines.append(QStringLiteral("   帖子 #%1：%2")
                         .arg(info.id)
                         .arg(url.isEmpty() ? QStringLiteral("（论坛地址未配置）") : url));
        lines.append(QString());
    }

    lines.append(QStringLiteral("提示：需要看正文、附件或下载方式时，调用 get_forum_thread(thread_id = 帖子号) 读取详情。"));
    return lines.join(QLatin1Char('\n'));
}

/** 主题列表 → 聊天页展示用的轻量结果（只保留标题 / 类型 / 链接） */
QList<ForumSearchHit> buildForumSearchHits(const QVector<ForumThreadInfo> &threads)
{
    QList<ForumSearchHit> hits;
    for (const ForumThreadInfo &info : threads)
    {
        ForumSearchHit hit;
        hit.id = info.id;
        hit.title = info.title.trimmed();
        hit.boardName = info.boardName.trimmed().isEmpty() ? forumBoardDisplayName(info.boardSlug)
                                                           : info.boardName.trimmed();
        hit.authorName = info.authorName.trimmed();
        hit.kind = forumThreadKindText(info);
        hit.snippet = forumBrief(info.snippet, 120);
        hit.url = forumThreadWebUrl(info.id);
        hits.append(hit);
    }
    return hits;
}

/** 帖子详情 → 模型可读文本（正文 + 附件 / 作品信息 + 前几层回复） */
QString formatForumThreadDetail(const ForumThreadDetail &detail, int maxBodyChars)
{
    const ForumThreadInfo &info = detail.info;
    QStringList lines;
    lines.append(QStringLiteral("# 方块盒子论坛帖子 #%1：%2")
                     .arg(info.id)
                     .arg(info.title.trimmed().isEmpty() ? QStringLiteral("(无标题)") : info.title));

    QStringList meta;
    if (!info.boardName.trimmed().isEmpty())
        meta << QStringLiteral("板块：%1").arg(info.boardName);
    meta << QStringLiteral("类型：%1").arg(forumThreadKindText(info));
    if (!info.authorName.trimmed().isEmpty())
        meta << QStringLiteral("作者：%1").arg(info.authorName.trimmed());
    lines.append(QStringLiteral("- %1").arg(meta.join(QStringLiteral(" ｜ "))));

    QStringList stats;
    stats << QStringLiteral("浏览 %1").arg(info.views);
    stats << QStringLiteral("回复 %1").arg(info.replies);
    if (info.comments > 0)
        stats << QStringLiteral("评论 %1").arg(info.comments);
    const QString created = formatForumTime(info.createdAt);
    if (!created.isEmpty())
        stats << QStringLiteral("发布于 %1").arg(created);
    lines.append(QStringLiteral("- %1").arg(stats.join(QStringLiteral(" ｜ "))));

    const QString tags = forumTagsText(info);
    if (!tags.isEmpty())
        lines.append(QStringLiteral("- 标签：%1").arg(tags));

    const QString url = forumThreadWebUrl(info.id);
    if (!url.isEmpty())
        lines.append(QStringLiteral("- 链接：%1").arg(url));

    if (info.source.isRepost())
    {
        QString sourceLine = QStringLiteral("- 作品来源：转载");
        if (!info.source.url.trimmed().isEmpty())
            sourceLine += QStringLiteral("（原作品 %1）").arg(info.source.url.trimmed());
        if (!info.source.note.trimmed().isEmpty())
            sourceLine += QStringLiteral("：%1").arg(forumBrief(info.source.note, 200));
        lines.append(sourceLine);
    }
    else
    {
        lines.append(QStringLiteral("- 作品来源：原创"));
    }

    if (info.plugin.isValid())
    {
        QString line = QStringLiteral("- 插件包：%1").arg(info.plugin.name.trimmed());
        if (!info.plugin.version.trimmed().isEmpty())
            line += QStringLiteral(" v%1").arg(info.plugin.version.trimmed());
        line += QStringLiteral("（作者 %1，%2，下载 %3 次）")
                    .arg(info.plugin.author.trimmed().isEmpty() ? QStringLiteral("未知")
                                                                : info.plugin.author.trimmed())
                    .arg(formatForumSize(info.plugin.fileSize))
                    .arg(info.plugin.downloads);
        lines.append(line);
        const QString desc = forumBrief(info.plugin.description, 300);
        if (!desc.isEmpty())
            lines.append(QStringLiteral("  插件简介：%1").arg(desc));
        for (const ForumResourceLink &link : info.plugin.links)
        {
            if (link.isValid())
                lines.append(QStringLiteral("  相关链接：%1 → %2")
                                 .arg(link.label.trimmed().isEmpty() ? QStringLiteral("链接") : link.label.trimmed(),
                                      link.url));
        }
        lines.append(QStringLiteral("  安装方式：在软件的帖子页面点「安装插件」即可；AI 不能直接装插件包。"));
    }

    if (info.schematic.isValid())
    {
        QString line = QStringLiteral("- 投影：%1（%2 个版本，%3，下载 %4 次）")
                           .arg(info.schematic.name.trimmed().isEmpty() ? info.schematic.fileName.trimmed()
                                                                        : info.schematic.name.trimmed())
                           .arg(info.schematic.versionCount())
                           .arg(formatForumSize(info.schematic.fileSize))
                           .arg(info.schematic.downloads);
        lines.append(line);
        for (const ForumSchematicFileInfo &file : info.schematic.files)
        {
            lines.append(QStringLiteral("  - 版本「%1」：%2（%3，下载 %4 次）")
                             .arg(file.label.trimmed().isEmpty() ? QStringLiteral("默认") : file.label.trimmed(),
                                  file.fileName)
                             .arg(formatForumSize(file.fileSize))
                             .arg(file.downloads));
        }
        for (const ForumResourceLink &link : info.schematic.links)
        {
            if (link.isValid())
                lines.append(QStringLiteral("  相关链接：%1 → %2")
                                 .arg(link.label.trimmed().isEmpty() ? QStringLiteral("链接") : link.label.trimmed(),
                                      link.url));
        }
        lines.append(QStringLiteral("  下载方式：在软件的帖子页面点「下载」，会存进当前实例的 schematics 文件夹；AI 不能直接下载。"));
    }

    if (!info.attachments.isEmpty())
    {
        lines.append(QStringLiteral("- 教程附件（%1 个）：").arg(info.attachments.size()));
        for (const ForumAttachmentInfo &attachment : info.attachments)
        {
            lines.append(QStringLiteral("  - %1：%2（下载 %3 次）")
                             .arg(attachment.name.trimmed().isEmpty() ? attachment.fileName : attachment.name,
                                  formatForumSize(attachment.fileSize))
                             .arg(attachment.downloads));
        }
    }

    if (info.question.isValid())
    {
        QString statusText = QStringLiteral("未知");
        if (info.question.isOpen())
            statusText = QStringLiteral("托管中");
        else if (info.question.isSettled())
            statusText = QStringLiteral("已发放");
        else if (info.question.status == QLatin1String("refunded"))
            statusText = QStringLiteral("已退回");
        QString line = QStringLiteral("- 有奖问答：悬赏 %1 方块 ｜ 状态：%2")
                           .arg(info.question.bounty)
                           .arg(statusText);
        if (info.question.daysLeft > 0)
            line += QStringLiteral(" ｜ 还剩 %1 天自动结算").arg(info.question.daysLeft);
        if (!info.question.winnerName.trimmed().isEmpty())
        {
            line += QStringLiteral(" ｜ 得主：%1（%2 楼）")
                        .arg(info.question.winnerName)
                        .arg(info.question.winnerFloor);
        }
        lines.append(line);
    }

    if (info.navigation.isValid())
    {
        lines.append(QStringLiteral("- 导航：%1 → %2")
                         .arg(info.navigation.isServer() ? QStringLiteral("服务器地址")
                                                         : QStringLiteral("网站地址"),
                              info.navigation.url));
    }

    // 正文：顶层第一层（1 楼）。搜索接口不返回正文，所以这里给全一点
    QString body;
    int firstTopLevelIndex = -1;
    for (int i = 0; i < detail.posts.size(); ++i)
    {
        if (detail.posts[i].parentPostId == 0)
        {
            firstTopLevelIndex = i;
            break;
        }
    }
    if (firstTopLevelIndex >= 0)
        body = detail.posts[firstTopLevelIndex].content;
    body = body.trimmed();

    lines.append(QString());
    lines.append(QStringLiteral("## 正文"));
    if (body.isEmpty())
    {
        lines.append(QStringLiteral("（正文为空或接口没有返回正文）"));
    }
    else if (body.size() > maxBodyChars)
    {
        lines.append(body.left(maxBodyChars));
        lines.append(QStringLiteral("\n…（正文过长已截断，想看后面部分可以换 page 参数或用 fetch_webpage 打开帖子链接）"));
    }
    else
    {
        lines.append(body);
    }

    // 回复：跳过 1 楼，取前 3 层有效回复，方便 AI 参考别人的结论
    QVector<ForumPost> replies;
    for (int i = firstTopLevelIndex + 1; i >= 0 && i < detail.posts.size(); ++i)
    {
        const ForumPost &post = detail.posts[i];
        if (post.parentPostId != 0 || post.deleted || post.content.trimmed().isEmpty())
            continue;
        replies.append(post);
        if (replies.size() >= 3)
            break;
    }
    if (!replies.isEmpty())
    {
        lines.append(QString());
        lines.append(QStringLiteral("## 回复（前 %1 条）").arg(replies.size()));
        for (const ForumPost &post : replies)
        {
            const QString author = post.author.displayName.trimmed().isEmpty()
                                       ? post.author.username.trimmed()
                                       : post.author.displayName.trimmed();
            lines.append(QStringLiteral("%1 楼 %2：%3")
                             .arg(post.floor)
                             .arg(author.isEmpty() ? QStringLiteral("匿名") : author,
                                  forumBrief(post.content, 300)));
        }
    }

    if (detail.pagination.pages > 1)
    {
        lines.append(QString());
        lines.append(QStringLiteral("（共 %1 页，当前第 %2 页；想继续看回复可传 page 参数）")
                         .arg(detail.pagination.pages)
                         .arg(detail.pagination.page));
    }

    return lines.join(QLatin1Char('\n'));
}

} // namespace

void AiService::executeSearchForum(const QJsonObject &args)
{
    const QJsonObject toolCall = currentDispatchedToolCall();
    const QJsonObject functionObj = toolCall.value(QStringLiteral("function")).toObject();
    QString toolName = functionObj.value(QStringLiteral("name")).toString();
    if (toolName.isEmpty())
        toolName = QStringLiteral("search_forum");
    const QString toolCallId = toolCall.value(QStringLiteral("id")).toString();
    QString argumentsJson = functionObj.value(QStringLiteral("arguments")).toString();
    if (argumentsJson.isEmpty())
        argumentsJson = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));

    const int generation = m_sendGeneration;
    QElapsedTimer timer;
    timer.start();

    const QString contentType = args.value(QStringLiteral("content_type")).toString().trimmed().toLower();
    const QString query = args.value(QStringLiteral("query")).toString().trimmed().left(60);
    QString boardRaw = args.value(QStringLiteral("board")).toString().trimmed();
    if (boardRaw.isEmpty())
    {
        if (contentType == QStringLiteral("plugin"))
            boardRaw = QStringLiteral("plugin-share");
        else if (contentType == QStringLiteral("schematic") || contentType == QStringLiteral("projection"))
            boardRaw = QStringLiteral("schematic-share");
    }
    const QString boardSlug = normalizeForumBoardSlug(boardRaw);
    if (!boardRaw.isEmpty() && boardSlug.isEmpty())
    {
        const QString message = QStringLiteral(
            "未知板块「%1」。可用：plugin（插件分享）、schematic（投影分享）、tutorial（教程分享）、"
            "question（有奖问答）、server-nav（服务器导航）、website-nav（网站导航）、"
            "help（使用求助）、general（综合讨论）、chat（闲聊），或者不传 board 搜全站。")
            .arg(boardRaw);
        finishForumToolCall(toolName, toolCallId, message, QStringLiteral("板块参数错误"), 0, false, argumentsJson);
        return;
    }

    // search_resources 走到这里时关键词是必需的（不能只按板块浏览）
    if (toolName == QStringLiteral("search_resources") && query.isEmpty())
    {
        finishForumToolCall(toolName, toolCallId, QStringLiteral("错误：缺少 query 参数"),
                            QStringLiteral("参数错误"), 0, false, argumentsJson);
        return;
    }

    QString sort = args.value(QStringLiteral("sort")).toString().trimmed().toLower();
    if (sort != QStringLiteral("latest") && sort != QStringLiteral("new") && sort != QStringLiteral("hot"))
        sort = QStringLiteral("latest");
    const QString tag = args.value(QStringLiteral("tag")).toString().trimmed().left(20);
    // search_forum 的 page 从 1 开始；search_resources 的 page 是 0 基（与 CF / Modrinth 一致），
    // 走论坛时换算成论坛的 1 基页码
    const bool fromSearchResources = (toolName == QStringLiteral("search_resources"));
    const int page = qMax(1, args.value(QStringLiteral("page")).toInt(fromSearchResources ? 0 : 1)
                             + (fromSearchResources ? 1 : 0));

    QSharedPointer<bool> settled(new bool(false));
    QPointer<AiService> guard(this);

    const auto finishOnce = [guard, settled, generation, toolName, toolCallId, argumentsJson, timer](
                                const QString &resultText, const QString &summary, bool success,
                                const QList<ForumSearchHit> &hits) {
        if (*settled)
            return;
        *settled = true;
        if (!guard || guard->m_sendGeneration != generation)
            return;
        guard->finishForumToolCall(toolName, toolCallId, resultText, summary,
                                   timer.elapsed(), success, argumentsJson, hits);
    };

    // 45 秒兜底：论坛慢、断网或被人机验证拦住时，别让工具循环一直挂着
    QTimer::singleShot(45000, this, [finishOnce]() {
        finishOnce(QStringLiteral("论坛搜索超时（45 秒没有响应）。如果软件弹出了人机验证，在里面完成验证后重试即可；"
                                  "也可能是网络较慢，稍后再试，或先用 web_search 查通用资料。"),
                   QStringLiteral("搜索超时"), false, QList<ForumSearchHit>());
    });

    const auto onFail = [finishOnce](const QString &message, int httpStatus) {
        QString text = QStringLiteral("论坛搜索失败：%1").arg(message);
        if (httpStatus == 403)
        {
            text += QStringLiteral("\n（论坛可能要求人机验证：若弹出了验证滑块，完成后再试一次即可。）");
        }
        finishOnce(text, QStringLiteral("搜索失败"), false, QList<ForumSearchHit>());
    };

    auto *service = ForumContentService::instance();
    const QString scope = boardSlug.isEmpty() ? QStringLiteral("全站")
                                              : forumBoardDisplayName(boardSlug);

    if (!query.isEmpty())
    {
        // 关键词搜索（可限定板块）
        service->searchThreads(
            query, page,
            [finishOnce, query, scope](const QString &, const QVector<ForumThreadInfo> &threads,
                                       const ForumPagination &pagination) {
                const QString text = formatForumThreadList(query, scope, threads, pagination);
                const QString summary = threads.isEmpty()
                                            ? QStringLiteral("没有找到相关帖子")
                                            : QStringLiteral("找到 %1 个帖子").arg(threads.size());
                finishOnce(text, summary, true, buildForumSearchHits(threads));
            },
            onFail, boardSlug);
        return;
    }

    // 没有关键词：按标签 / 板块 / 排序浏览最新或最热内容
    if (!tag.isEmpty())
    {
        const QString tagScope = QStringLiteral("标签 #%1").arg(tag);
        service->fetchTagThreads(
            tag, sort, page,
            [finishOnce, tagScope, sort](const QVector<ForumThreadInfo> &threads,
                                         const ForumPagination &pagination) {
                const QString text = formatForumThreadList(QString(), tagScope, threads, pagination);
                QString summary = threads.isEmpty()
                                      ? QStringLiteral("这个标签下暂时没有帖子")
                                      : QStringLiteral("找到 %1 个帖子").arg(threads.size());
                if (!sort.isEmpty())
                    summary += QStringLiteral(" · 按%1")
                                   .arg(sort == QStringLiteral("hot")
                                            ? QStringLiteral("热度")
                                            : sort == QStringLiteral("new") ? QStringLiteral("发布时间")
                                                                            : QStringLiteral("最新回复"));
                finishOnce(text, summary, true, buildForumSearchHits(threads));
            },
            onFail);
        return;
    }

    if (!boardSlug.isEmpty())
    {
        service->fetchBoardThreads(
            boardSlug, sort, page,
            [finishOnce, scope](const ForumBoard &board, const QVector<ForumThreadInfo> &threads,
                                const ForumPagination &pagination) {
                const QString boardName = board.name.trimmed().isEmpty() ? scope : board.name.trimmed();
                const QString text = formatForumThreadList(QString(), boardName, threads, pagination);
                const QString summary = threads.isEmpty()
                                            ? QStringLiteral("%1 暂时没有帖子").arg(boardName)
                                            : QStringLiteral("%1 找到 %2 个帖子").arg(boardName).arg(threads.size());
                finishOnce(text, summary, true, buildForumSearchHits(threads));
            },
            onFail);
        return;
    }

    // 既没关键词也没限定板块：给全站最新 / 最热主题
    service->fetchTagThreads(
        QString(), sort, page,
        [finishOnce, sort](const QVector<ForumThreadInfo> &threads, const ForumPagination &pagination) {
            const QString scope = sort == QStringLiteral("hot") ? QStringLiteral("全站热门")
                                                               : QStringLiteral("全站最新");
            const QString text = formatForumThreadList(QString(), scope, threads, pagination);
            const QString summary = threads.isEmpty()
                                        ? QStringLiteral("论坛暂时没有帖子")
                                        : QStringLiteral("%1 · %2 个帖子").arg(scope).arg(threads.size());
            finishOnce(text, summary, true, buildForumSearchHits(threads));
        },
        onFail);
}

void AiService::executeGetForumThread(const QJsonObject &args)
{
    const QJsonObject toolCall = currentDispatchedToolCall();
    const QJsonObject functionObj = toolCall.value(QStringLiteral("function")).toObject();
    QString toolName = functionObj.value(QStringLiteral("name")).toString();
    if (toolName.isEmpty())
        toolName = QStringLiteral("get_forum_thread");
    const QString toolCallId = toolCall.value(QStringLiteral("id")).toString();
    QString argumentsJson = functionObj.value(QStringLiteral("arguments")).toString();
    if (argumentsJson.isEmpty())
        argumentsJson = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));

    const int generation = m_sendGeneration;
    QElapsedTimer timer;
    timer.start();

    int threadId = args.value(QStringLiteral("thread_id")).toInt();
    if (threadId <= 0)
        threadId = args.value(QStringLiteral("threadId")).toInt();
    if (threadId <= 0)
        threadId = args.value(QStringLiteral("id")).toInt();
    if (threadId <= 0)
    {
        finishForumToolCall(toolName, toolCallId,
                            QStringLiteral("错误：缺少 thread_id 参数（就是 search_forum 结果里的「帖子 #N」）"),
                            QStringLiteral("参数错误"), 0, false, argumentsJson);
        return;
    }

    const int page = qMax(1, args.value(QStringLiteral("page")).toInt(1));
    const int maxBodyChars = qBound(500, args.value(QStringLiteral("max_chars")).toInt(6000), 12000);

    QSharedPointer<bool> settled(new bool(false));
    QPointer<AiService> guard(this);

    const auto finishOnce = [guard, settled, generation, toolName, toolCallId, argumentsJson, timer](
                                const QString &resultText, const QString &summary, bool success) {
        if (*settled)
            return;
        *settled = true;
        if (!guard || guard->m_sendGeneration != generation)
            return;
        guard->finishForumToolCall(toolName, toolCallId, resultText, summary,
                                   timer.elapsed(), success, argumentsJson, QList<ForumSearchHit>());
    };

    QTimer::singleShot(45000, this, [finishOnce]() {
        finishOnce(QStringLiteral("读取帖子超时（45 秒没有响应）。如果软件弹出了人机验证，在里面完成验证后重试即可；"
                                  "也可能是网络较慢，稍后再试。"),
                   QStringLiteral("读取超时"), false);
    });

    const auto onFail = [finishOnce](const QString &message, int httpStatus) {
        QString text = QStringLiteral("读取帖子失败：%1").arg(message);
        if (httpStatus == 403)
        {
            text += QStringLiteral("\n（论坛可能要求人机验证：若弹出了验证滑块，完成后再试一次即可。）");
        }
        finishOnce(text, QStringLiteral("读取失败"), false);
    };

    ForumContentService::instance()->fetchThread(
        threadId, page,
        [finishOnce, maxBodyChars](const ForumThreadDetail &detail) {
            if (!detail.info.isValid())
            {
                finishOnce(QStringLiteral("没有找到这个帖子，可能已被删除或线程号不对。"),
                           QStringLiteral("帖子不存在"), false);
                return;
            }
            const QString text = formatForumThreadDetail(detail, maxBodyChars);
            QString summary = QStringLiteral("《%1》· %2 条回复")
                                  .arg(detail.info.title.trimmed().isEmpty()
                                           ? QStringLiteral("未命名帖子")
                                           : detail.info.title.trimmed())
                                  .arg(detail.info.replies);
            finishOnce(text, summary, true);
        },
        onFail);
}

void AiService::finishForumToolCall(const QString &toolName, const QString &toolCallId,
                                    const QString &resultText, const QString &summary,
                                    qint64 durationMs, bool success, const QString &argumentsJson,
                                    const QList<ForumSearchHit> &hits)
{
    // 先让聊天页把帖子标题铺成可点击链接（步骤卡片此刻仍是「进行中」态），
    // 再发完成信号；卡片上会记住这是富文本结果，不会被结果摘要覆盖。
    if (!hits.isEmpty())
    {
        emit forumSearchResultsCollected(hits);
    }

    emit agentToolCallFinished(toolName, summary, durationMs, success);

    AgentToolCall rec;
    rec.name = toolName;
    rec.arguments = argumentsJson;
    rec.result = resultText;
    rec.resultSummary = summary;
    rec.durationMs = durationMs;
    rec.success = success;
    if (!success)
    {
        rec.errorMessage = resultText;
    }
    m_pendingToolCalls.append(rec);

    continueWithToolResult(toolCallId, toolName, resultText, 9000);
}

QString AiService::executeUpdateTaskList(const QJsonObject& args)
{
    QJsonArray tasksArr = args["tasks"].toArray();
    if (tasksArr.isEmpty())
    {
        // 传入空数组：清空任务清单
        m_taskList.clear();
        emit taskListUpdated(m_taskList);
        return QStringLiteral("任务清单已清空");
    }

    // 合法的状态与优先级集合
    static const QSet<QString> validStatus = {
        QStringLiteral("pending"), QStringLiteral("in_progress"), QStringLiteral("completed")
    };
    static const QSet<QString> validPriority = {
        QStringLiteral("high"), QStringLiteral("medium"), QStringLiteral("low")
    };

    QList<AgentTask> newTasks;
    newTasks.reserve(tasksArr.size());
    QStringList parseErrors;

    for (int i = 0; i < tasksArr.size(); ++i)
    {
        QJsonObject t = tasksArr[i].toObject();
        AgentTask task;
        task.id = t["id"].toString().trimmed();
        task.content = t["content"].toString().trimmed();
        task.status = t["status"].toString().trimmed().toLower();
        task.priority = t["priority"].toString().trimmed().toLower();

        if (task.id.isEmpty())
        {
            parseErrors.append(QStringLiteral("第 %1 项缺少 id").arg(i + 1));
            continue;
        }
        if (task.content.isEmpty())
        {
            parseErrors.append(QStringLiteral("任务 %1 缺少 content").arg(task.id));
            continue;
        }
        if (!validStatus.contains(task.status))
        {
            // 容错：未知状态归为 pending
            task.status = QStringLiteral("pending");
        }
        if (!validPriority.contains(task.priority))
        {
            task.priority = QStringLiteral("medium");
        }
        newTasks.append(task);
    }

    if (newTasks.isEmpty())
    {
        return QStringLiteral("任务清单更新失败：%1").arg(
            parseErrors.isEmpty() ? QStringLiteral("无有效任务") : parseErrors.join(QStringLiteral("；")));
    }

    m_taskList = newTasks;
    emit taskListUpdated(m_taskList);

    // 统计各状态数量
    int pending = 0, inProgress = 0, completed = 0;
    for (const auto& t : newTasks)
    {
        if (t.status == QStringLiteral("completed")) ++completed;
        else if (t.status == QStringLiteral("in_progress")) ++inProgress;
        else ++pending;
    }

    QStringList lines;
    lines.append(QStringLiteral("任务清单已更新（共 %1 项）").arg(newTasks.size()));
    lines.append(QStringLiteral("- 进行中: %1，待处理: %2，已完成: %3")
                     .arg(inProgress).arg(pending).arg(completed));
    if (!parseErrors.isEmpty())
    {
        lines.append(QStringLiteral("- 忽略的无效项: %1")
                         .arg(parseErrors.join(QStringLiteral("；"))));
    }

    return lines.join('\n');
}

// ============================================================================
// Agent 实例管理工具实现
// ============================================================================

QString AiService::executeLaunchGame(const QJsonObject& args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("启动失败：缺少 instance_path 参数");
    }

    if (!QDir(instancePath).exists())
    {
        return QStringLiteral("启动失败：实例路径不存在 - %1").arg(instancePath);
    }

    // 构建 LaunchConfig
    GameLauncher::LaunchConfig config;
    config.instancePath = instancePath;

    QString javaPath = args["java_path"].toString().trimmed();
    if (!javaPath.isEmpty() && javaPath.toLower() != QStringLiteral("auto"))
    {
        config.javaPath = javaPath;
    }
    else
    {
        // 自动匹配：留空，由 GameLauncher 内部处理（若其不支持，尝试用 findBestJavaVersion）
        auto *launcher = GameLauncher::instance();
        // 尝试从实例路径推断游戏版本（读取 versions 目录）
        QDir versionsDir(QDir(instancePath).absoluteFilePath(QStringLiteral("versions")));
        if (versionsDir.exists())
        {
            QStringList entries = versionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            if (!entries.isEmpty())
            {
                GameLauncher::JavaInfo ji = launcher->findBestJavaVersion(entries.first());
                if (ji.valid)
                {
                    config.javaPath = ji.path;
                }
            }
        }
        if (config.javaPath.isEmpty())
        {
            // 兜底：用 "auto" 占位，GameLauncher 会处理
            config.javaPath = QStringLiteral("auto");
        }
    }

    int maxMemory = args["max_memory"].toInt(4096);
    if (maxMemory < 512)
    {
        maxMemory = 4096;
    }
    config.maxMemory = maxMemory;
    config.minMemory = qMax(1024, maxMemory / 4);

    int waitSeconds = args["wait_seconds"].toInt(15);
    waitSeconds = qBound(5, waitSeconds, 30);

    auto *launcher = GameLauncher::instance();

    // 若已有游戏在启动/运行（含并行启动的其他实例会话），拒绝重复启动
    if (GameLauncher::anySessionBusy() || launcher->isGameRunning())
    {
        return QStringLiteral("已有一个游戏实例正在运行，请先关闭后再启动");
    }

    // 收集日志的缓冲（子线程局部）
    QStringList logBuffer;
    bool crashed = false;
    QString crashMsg;
    bool started = false;
    bool stopped = false;
    int stopCode = -1;

    QEventLoop loop;
    QTimer waitTimer;
    waitTimer.setSingleShot(true);

    // 订阅日志信号（跨线程自动 QueuedConnection）
    QMetaObject::Connection logConn = connect(
        launcher, &GameLauncher::launchDetailAdded,
        &loop, [&logBuffer](const QString& detail) {
            logBuffer.append(detail);
        });

    QMetaObject::Connection crashConn = connect(
        launcher, &GameLauncher::gameCrashed,
        &loop, [&loop, &crashed, &crashMsg](const QString& error) {
            crashed = true;
            crashMsg = error;
            loop.quit();
        });

    QMetaObject::Connection startedConn = connect(
        launcher, &GameLauncher::gameStarted,
        &loop, [&started]() { started = true; });

    QMetaObject::Connection stoppedConn = connect(
        launcher, &GameLauncher::gameStopped,
        &loop, [&loop, &stopped, &stopCode](int exitCode) {
            stopped = true;
            stopCode = exitCode;
            loop.quit();
        });

    connect(&waitTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    // 在主线程触发启动（GameLauncher 有主线程亲和性）
    bool launchOk = false;
    QMetaObject::invokeMethod(launcher,
        [launcher, config, &launchOk]() {
            launchOk = launcher->launchGame(config, true);
        }, Qt::BlockingQueuedConnection);

    if (!launchOk)
    {
        disconnect(logConn);
        disconnect(crashConn);
        disconnect(startedConn);
        disconnect(stoppedConn);
        return QStringLiteral("启动失败：%1").arg(launcher->errorMessage());
    }

    // 等待收集日志（超时或崩溃/退出时提前结束）
    waitTimer.start(waitSeconds * 1000);
    loop.exec();

    disconnect(logConn);
    disconnect(crashConn);
    disconnect(startedConn);
    disconnect(stoppedConn);

    // 组织返回结果
    QStringList lines;

    GameLauncher::LaunchStatus finalStatus = launcher->status();
    QString statusStr;
    switch (finalStatus)
    {
        case GameLauncher::Running: statusStr = QStringLiteral("运行中"); break;
        case GameLauncher::Launching: statusStr = QStringLiteral("启动中"); break;
        case GameLauncher::Failed: statusStr = QStringLiteral("失败"); break;
        case GameLauncher::Stopped: statusStr = QStringLiteral("已停止"); break;
        default: statusStr = QStringLiteral("空闲"); break;
    }

    lines.append(QStringLiteral("启动状态: %1").arg(statusStr));
    if (started)
    {
        lines.append(QStringLiteral("游戏进程已成功启动"));
    }
    if (crashed)
    {
        lines.append(QStringLiteral("游戏崩溃: %1").arg(crashMsg));
    }
    if (stopped)
    {
        lines.append(QStringLiteral("游戏已退出，退出码: %1").arg(stopCode));
    }

    // 日志摘要（最多保留 50 行，避免过长）
    lines.append(QStringLiteral("\n启动日志（共 %1 行，显示最近 %2 行）:")
                    .arg(logBuffer.size())
                    .arg(qMin(logBuffer.size(), 50)));
    int startIdx = qMax(0, logBuffer.size() - 50);
    for (int i = startIdx; i < logBuffer.size(); ++i)
    {
        lines.append(logBuffer[i]);
    }

    return lines.join('\n');
}

QString AiService::executeDownloadInstance(const QJsonObject& args)
{
    QString versionId = args["version_id"].toString().trimmed();
    QString instancePath = args["instance_path"].toString().trimmed();
    if (versionId.isEmpty() || instancePath.isEmpty())
    {
        return QStringLiteral("下载失败：缺少 version_id 或 instance_path 参数");
    }

    QString instanceName = args["instance_name"].toString().trimmed();
    QString loader = args["loader"].toString().trimmed().toLower();
    QString loaderVersion = args["loader_version"].toString().trimmed();
    QString source = args["source"].toString().trimmed().toLower();

    if (loader.isEmpty() || loader == QStringLiteral("none"))
    {
        loader.clear();
    }
    if (source.isEmpty())
    {
        source = QStringLiteral("bmcl");
    }

    // 跨线程信号触发主线程下载（不阻塞）
    emit instanceDownloadRequested(versionId, instancePath, instanceName,
                                    loader, loaderVersion, source);

    QStringList lines;
    lines.append(QStringLiteral("已触发实例下载："));
    lines.append(QStringLiteral("- 版本: %1").arg(versionId));
    lines.append(QStringLiteral("- 实例路径: %1").arg(instancePath));
    if (!instanceName.isEmpty())
    {
        lines.append(QStringLiteral("- 实例名: %1").arg(instanceName));
    }
    if (!loader.isEmpty())
    {
        lines.append(QStringLiteral("- 加载器: %1%2")
                         .arg(loader)
                         .arg(loaderVersion.isEmpty() ? QString() : QStringLiteral(" ") + loaderVersion));
    }
    lines.append(QStringLiteral("- 下载源: %1").arg(source));
    lines.append(QStringLiteral("- 请到下载任务页查看进度"));
    return lines.join('\n');
}

QString AiService::executeModifyInstance(const QJsonObject& args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("修改失败：缺少 instance_path 参数");
    }

    QString newVersion = args["new_version"].toString().trimmed();
    QString loader = args["loader"].toString().trimmed().toLower();
    QString loaderVersion = args["loader_version"].toString().trimmed();
    QString source = args["source"].toString().trimmed().toLower();

    if (newVersion.isEmpty() && (loader.isEmpty() || loader == QStringLiteral("none")))
    {
        return QStringLiteral("修改失败：至少需要指定 new_version 或 loader 之一");
    }

    if (!QDir(instancePath).exists())
    {
        return QStringLiteral("修改失败：实例路径不存在 - %1").arg(instancePath);
    }

    if (loader.isEmpty() || loader == QStringLiteral("none"))
    {
        loader.clear();
    }
    if (source.isEmpty())
    {
        source = QStringLiteral("bmcl");
    }

    // 跨线程信号触发主线程下载（不阻塞）
    emit instanceModifyRequested(instancePath, newVersion, loader, loaderVersion, source);

    QStringList lines;
    lines.append(QStringLiteral("已触发实例修改（下载新版本到同实例路径）："));
    lines.append(QStringLiteral("- 实例路径: %1").arg(instancePath));
    if (!newVersion.isEmpty())
    {
        lines.append(QStringLiteral("- 新游戏版本: %1").arg(newVersion));
    }
    if (!loader.isEmpty())
    {
        lines.append(QStringLiteral("- 加载器: %1%2")
                         .arg(loader)
                         .arg(loaderVersion.isEmpty() ? QString() : QStringLiteral(" ") + loaderVersion));
    }
    lines.append(QStringLiteral("- 下载源: %1").arg(source));
    lines.append(QStringLiteral("- 注意：新旧版本目录将共存，不会删除旧版本"));
    lines.append(QStringLiteral("- 请到下载任务页查看进度"));
    return lines.join('\n');
}

QString AiService::executeAskUser(const QJsonObject& args)
{
    QString question = args["question"].toString().trimmed();
    if (question.isEmpty())
    {
        return QStringLiteral("错误：缺少 question 参数");
    }

    // 解析选项列表
    QStringList options;
    QJsonArray optArr = args["options"].toArray();
    for (const QJsonValue& v : optArr)
    {
        QString opt = v.toString().trimmed();
        if (!opt.isEmpty())
        {
            options.append(opt);
        }
    }

    QString defaultValue = args["default_value"].toString();
    bool allowOther = args["allow_other"].toBool(false);

    // 子线程 QEventLoop 阻塞等待主线程回填答案
    QString answer;
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    QMetaObject::Connection answerConn = connect(
        this, &AiService::userAnswerProvided,
        &loop, [&loop, &answer](const QString& a) {
            answer = a;
            loop.quit();
        });

    // 超时兜底（10 分钟，避免用户离开导致工具永久阻塞）
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    // 触发主线程弹对话框
    emit userQuestionAsked(question, options, defaultValue, allowOther);

    // 启动超时计时器并阻塞等待
    timeoutTimer.start(10 * 60 * 1000);
    loop.exec();

    disconnect(answerConn);

    // 超时仍未回答
    if (answer.isEmpty() && !timeoutTimer.isActive())
    {
        return QStringLiteral("用户未在 10 分钟内回答，已超时取消");
    }

    if (answer.isEmpty())
    {
        return QStringLiteral("用户取消了回答");
    }
    return QStringLiteral("用户回答: %1").arg(answer);
}

void AiService::provideUserAnswer(const QString& answer)
{
    emit userAnswerProvided(answer);
}

QString AiService::executeSkillTool(const QString &toolName, const QJsonObject &args)
{
    // 通过工具名映射定位所属技能 ID
    auto it = m_skillToolMap.constFind(toolName);
    if (it == m_skillToolMap.constEnd())
    {
        return QStringLiteral("ERROR: 未找到技能工具: ") + toolName;
    }

    QString skillId = it.value();
    QString error;
    QString result = SkillManager::instance()->executeTool(skillId, toolName, args, error);

    // 技能未信任
    if (error == QStringLiteral("SKILL_NOT_TRUSTED"))
    {
        return QStringLiteral("ERROR: 技能「") + skillId +
               QStringLiteral("」未被信任，请在技能管理中确认信任后再使用。");
    }

    if (result.isEmpty() && !error.isEmpty())
    {
        return QStringLiteral("ERROR: ") + error;
    }
    if (!error.isEmpty())
    {
        // 有错误但有部分输出，附加警告
        result = result + QStringLiteral("\n[警告: ") + error + QStringLiteral("]");
    }
    if (result.isEmpty())
    {
        result = QStringLiteral("(工具执行完成，无输出)");
    }
    return result;
}

QString AiService::executePluginAiTool(const QString &toolName, const QJsonObject &args)
{
    // 通过工具名映射定位所属插件 ID
    const auto it = m_pluginToolMap.constFind(toolName);
    if (it == m_pluginToolMap.constEnd())
    {
        return QStringLiteral("ERROR: 未找到插件工具: ") + toolName;
    }

    const QString pluginId = it.value();
    QString error;
    QString result = PluginManager::instance()->executeAiTool(pluginId, toolName, args, error);

    if (error == QStringLiteral("PLUGIN_DISABLED"))
    {
        return QStringLiteral("ERROR: 插件「") + pluginId +
               QStringLiteral("」已停用，无法调用其工具。");
    }
    if (error == QStringLiteral("PLUGIN_AI_TOOL_NOT_ALLOWED"))
    {
        return QStringLiteral("ERROR: 插件「") + pluginId +
               QStringLiteral("」未声明 ai:tool 权限，无法调用其工具。");
    }

    if (result.isEmpty() && !error.isEmpty())
    {
        return QStringLiteral("ERROR: ") + error;
    }
    if (!error.isEmpty())
    {
        // 有错误但有部分输出，附加警告
        result = result + QStringLiteral("\n[警告: ") + error + QStringLiteral("]");
    }
    if (result.isEmpty())
    {
        result = QStringLiteral("(工具执行完成，无输出)");
    }
    return result;
}

QString AiService::executeReadLitematic(const QJsonObject &args)
{
    QString filePath = args["file_path"].toString().trimmed();
    if (filePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 file_path 参数");
    }

    // 规范化路径：移除可能的 file:/// 前缀和首尾引号
    if (filePath.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive))
    {
        filePath = QUrl(filePath).toLocalFile();
    }
    if (filePath.startsWith(QStringLiteral("\"")) && filePath.endsWith(QStringLiteral("\"")))
    {
        filePath = filePath.mid(1, filePath.size() - 2);
    }

    bool includeMaterial = args.value(QStringLiteral("include_material_list")).toBool(true);

    if (!QFileInfo::exists(filePath))
    {
        return QStringLiteral("错误：文件不存在 - %1").arg(filePath);
    }

    auto info = LitematicReader::readFile(filePath);
    if (!info)
    {
        return QStringLiteral("错误：解析 .litematic 文件失败（文件损坏或格式不正确）- %1").arg(filePath);
    }

    // 摘要首行（取结果第一行作为 UI 卡片标题，控制在 60 字内）
    QString summaryName = info->name.isEmpty() ? QFileInfo(filePath).fileName() : info->name;
    QString summary = QStringLiteral("投影「%1」 %2×%3×%4，共 %5 方块")
                          .arg(summaryName)
                          .arg(info->enclosingSizeX)
                          .arg(info->enclosingSizeY)
                          .arg(info->enclosingSizeZ)
                          .arg(info->totalBlocks);
    if (summary.size() > 60)
    {
        summary = summary.left(57) + QStringLiteral("...");
    }

    QStringList lines;
    lines.append(summary);

    // --- 基本信息 ---
    lines.append(QStringLiteral("\n## 基本信息"));
    lines.append(QStringLiteral("文件名: %1").arg(QFileInfo(filePath).fileName()));
    lines.append(QStringLiteral("文件路径: %1").arg(filePath));
    lines.append(QStringLiteral("投影名称: %1").arg(info->name.isEmpty() ? QStringLiteral("(未命名)") : info->name));
    if (!info->author.isEmpty())
    {
        lines.append(QStringLiteral("作者: %1").arg(info->author));
    }
    if (!info->description.isEmpty())
    {
        // 描述可能很长，截断到 200 字
        QString desc = info->description;
        if (desc.size() > 200)
        {
            desc = desc.left(200) + QStringLiteral("...");
        }
        lines.append(QStringLiteral("描述: %1").arg(desc));
    }
    if (info->timeCreated > 0)
    {
        lines.append(QStringLiteral("创建时间: %1").arg(QDateTime::fromMSecsSinceEpoch(info->timeCreated).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }
    if (info->timeModified > 0)
    {
        lines.append(QStringLiteral("修改时间: %1").arg(QDateTime::fromMSecsSinceEpoch(info->timeModified).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }
    if (info->minecraftDataVersion > 0)
    {
        lines.append(QStringLiteral("MC 数据版本: %1").arg(info->minecraftDataVersion));
    }
    if (info->version > 0)
    {
        lines.append(QStringLiteral("Litematica 格式版本: %1").arg(info->version));
    }

    // --- 尺寸与统计 ---
    lines.append(QStringLiteral("\n## 尺寸与统计"));
    lines.append(QStringLiteral("包围盒大小: %1 × %2 × %3（长 × 宽 × 高）")
                     .arg(info->enclosingSizeX)
                     .arg(info->enclosingSizeY)
                     .arg(info->enclosingSizeZ));
    lines.append(QStringLiteral("总体积: %1 方块格").arg(info->totalVolume));
    lines.append(QStringLiteral("总方块数: %1").arg(info->totalBlocks));
    lines.append(QStringLiteral("区域数: %1").arg(info->regionCount > 0 ? info->regionCount : info->regions.size()));

    // --- 区域清单 ---
    if (!info->regions.isEmpty())
    {
        lines.append(QStringLiteral("\n## 区域清单（共 %1 个）").arg(info->regions.size()));
        for (const auto &r : info->regions)
        {
            lines.append(QStringLiteral("\n### %1").arg(r.name.isEmpty() ? QStringLiteral("(未命名区域)") : r.name));
            lines.append(QStringLiteral("位置: (%1, %2, %3)").arg(r.posX).arg(r.posY).arg(r.posZ));
            lines.append(QStringLiteral("尺寸: %1 × %2 × %3").arg(r.sizeX).arg(r.sizeY).arg(r.sizeZ));
            lines.append(QStringLiteral("体积: %1").arg(r.volume));
            lines.append(QStringLiteral("非空方块数: %1").arg(r.blockCount));
            lines.append(QStringLiteral("调色板大小: %1").arg(r.paletteSize));
        }
    }

    // --- 材料清单 ---
    if (includeMaterial)
    {
        if (info->materialList.isEmpty())
        {
            lines.append(QStringLiteral("\n## 材料清单\n（空）"));
        }
        else
        {
            int totalMaterialTypes = info->materialList.size();
            // 控制输出长度，避免超过工具结果截断阈值（4000 字符）
            // 每行约 30-50 字符，保留 80 行左右给材料，其余部分留给元信息
            static constexpr int MAX_MATERIAL_LINES = 80;

            lines.append(QStringLiteral("\n## 材料清单（共 %1 种方块，按数量降序）").arg(totalMaterialTypes));

            int shown = std::min(totalMaterialTypes, MAX_MATERIAL_LINES);
            for (int i = 0; i < shown; ++i)
            {
                const auto &m = info->materialList[i];
                lines.append(QStringLiteral("%1. %2 × %3").arg(i + 1).arg(m.blockId).arg(m.count));
            }
            if (totalMaterialTypes > MAX_MATERIAL_LINES)
            {
                lines.append(QStringLiteral("...（剩余 %1 种方块未显示，可缩小投影范围或自行解析文件获取完整清单）")
                                .arg(totalMaterialTypes - MAX_MATERIAL_LINES));
            }
        }
    }
    else
    {
        lines.append(QStringLiteral("\n## 材料清单\n（已跳过，include_material_list=false；材料种类数: %1）")
                         .arg(info->materialList.size()));
    }

    return lines.join('\n');
}

// ============================================================================
// Agent 扩展工具实现（日志/崩溃报告/模组管理/存档/服务器/下载任务/种子结构）
// ============================================================================

namespace {

/// 游戏刻 → 时长文本（20 tick = 1 秒）
QString ticksToDuration(qint64 ticks)
{
    qint64 seconds = ticks / 20;
    qint64 hours = seconds / 3600;
    qint64 minutes = (seconds % 3600) / 60;
    if (hours > 0)
    {
        return QStringLiteral("%1 小时 %2 分钟").arg(hours).arg(minutes);
    }
    if (minutes > 0)
    {
        return QStringLiteral("%1 分钟").arg(minutes);
    }
    return QStringLiteral("%1 秒").arg(seconds);
}

/// 游戏模式编号 → 名称
QString gameTypeName(int gameType)
{
    switch (gameType)
    {
    case 0: return QStringLiteral("生存");
    case 1: return QStringLiteral("创造");
    case 2: return QStringLiteral("冒险");
    case 3: return QStringLiteral("旁观");
    default: return QStringLiteral("未知(%1)").arg(gameType);
    }
}

/// 难度编号 → 名称
QString difficultyName(int difficulty)
{
    switch (difficulty)
    {
    case 0: return QStringLiteral("和平");
    case 1: return QStringLiteral("简单");
    case 2: return QStringLiteral("普通");
    case 3: return QStringLiteral("困难");
    default: return QStringLiteral("未知(%1)").arg(difficulty);
    }
}

/// 维度编号 → 名称（LevelDatEditor 约定：-1=下界，0=主世界，1=末地）
QString dimensionName(int dimension)
{
    switch (dimension)
    {
    case -1: return QStringLiteral("下界");
    case 0: return QStringLiteral("主世界");
    case 1: return QStringLiteral("末地");
    default: return QStringLiteral("未知(%1)").arg(dimension);
    }
}

/// 存档文件夹名校验：拒绝路径分隔符，防止越出 saves 目录
bool isValidWorldFolderName(const QString &worldName)
{
    return !worldName.isEmpty() && !worldName.contains('/') && !worldName.contains('\\')
           && worldName != QStringLiteral(".") && worldName != QStringLiteral("..");
}

/// 下载任务状态 → 文本
QString downloadTaskStatusText(DownloadTaskStatus status)
{
    switch (status)
    {
    case DownloadTaskStatus::Queued: return QStringLiteral("排队中");
    case DownloadTaskStatus::Downloading: return QStringLiteral("下载中");
    case DownloadTaskStatus::Paused: return QStringLiteral("已暂停");
    case DownloadTaskStatus::Completed: return QStringLiteral("已完成");
    case DownloadTaskStatus::Failed: return QStringLiteral("失败");
    case DownloadTaskStatus::Cancelled: return QStringLiteral("已取消");
    default: return QStringLiteral("未知");
    }
}

} // namespace

QString AiService::executeReadGameLog(const QJsonObject &args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }

    QString logFile = args["log_file"].toString().trimmed();
    if (logFile.isEmpty())
    {
        logFile = QStringLiteral("latest.log");
    }
    // 防路径穿越：只允许纯文件名
    if (logFile.contains('/') || logFile.contains('\\') || logFile.contains(".."))
    {
        return QStringLiteral("错误：log_file 只能是 logs/ 目录下的文件名");
    }

    int tailLines = args["tail_lines"].toInt(200);
    tailLines = qBound(1, tailLines, 1000);

    QString logPath = instancePath + QStringLiteral("/logs/") + logFile;
    QFile file(logPath);
    if (!file.exists())
    {
        // 日志文件不存在时列出 logs 目录下可用文件
        QDir logsDir(instancePath + QStringLiteral("/logs"));
        QStringList available = logsDir.exists()
                                    ? logsDir.entryList(QDir::Files, QDir::Time)
                                    : QStringList();
        if (available.isEmpty())
        {
            return QStringLiteral("未找到日志：%1（该实例下也不存在 logs 目录）").arg(logPath);
        }
        return QStringLiteral("未找到日志文件 %1。logs 目录下可用文件：\n%2")
            .arg(logFile, available.join(QStringLiteral("\n")));
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return QStringLiteral("无法读取日志文件：%1").arg(logPath);
    }

    QStringList allLines = QString::fromUtf8(file.readAll()).split('\n');
    file.close();
    while (!allLines.isEmpty() && allLines.last().isEmpty())
    {
        allLines.removeLast();
    }

    int total = allLines.size();
    int start = qMax(0, total - tailLines);
    QStringList tail = allLines.mid(start);

    QStringList lines;
    lines.append(QStringLiteral("日志文件: %1").arg(logPath));
    lines.append(QStringLiteral("共 %1 行，以下为末尾 %2 行：").arg(total).arg(tail.size()));
    lines.append(tail.join('\n'));
    return lines.join('\n');
}

QString AiService::executeReadCrashReport(const QJsonObject &args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }

    QDir crashDir(instancePath + QStringLiteral("/crash-reports"));
    if (!crashDir.exists())
    {
        return QStringLiteral("该实例下不存在 crash-reports 目录（没有记录过崩溃报告）。");
    }

    // 按名称排序（崩溃报告文件名含时间戳，倒序即最新在前）
    QStringList reports = crashDir.entryList(QStringList() << QStringLiteral("*.txt"),
                                             QDir::Files, QDir::Name);
    if (reports.isEmpty())
    {
        return QStringLiteral("crash-reports 目录为空（没有崩溃报告文件）。");
    }
    std::reverse(reports.begin(), reports.end());

    int reportIndex = args["report_index"].toInt(0);
    // 清单序号从 1 开始；不传则默认读最新一份
    if (reportIndex <= 0)
    {
        reportIndex = 1;
    }
    if (reportIndex > reports.size())
    {
        return QStringLiteral("序号 %1 超出范围：共有 %2 份崩溃报告。")
            .arg(reportIndex)
            .arg(reports.size());
    }

    QStringList listLines;
    for (int i = 0; i < reports.size(); ++i)
    {
        QFileInfo fi(crashDir.filePath(reports[i]));
        listLines.append(QStringLiteral("%1. %2（%3，%4）")
                             .arg(i + 1)
                             .arg(reports[i])
                             .arg(fi.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm")))
                             .arg(formatFileSize(fi.size())));
    }

    QString chosenPath = crashDir.filePath(reports[reportIndex - 1]);
    QFile file(chosenPath);
    QString content;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        content = QString::fromUtf8(file.readAll());
        file.close();
    }
    else
    {
        content = QStringLiteral("（读取失败）");
    }
    if (content.size() > 4000)
    {
        content = content.left(4000) + QStringLiteral("\n...（内容过长已截断）");
    }

    QStringList lines;
    lines.append(QStringLiteral("共 %1 份崩溃报告：").arg(reports.size()));
    lines.append(listLines.join('\n'));
    lines.append(QStringLiteral("\n## 报告内容（第 %1 份：%2）").arg(reportIndex).arg(reports[reportIndex - 1]));
    lines.append(content);
    return lines.join('\n');
}

QString AiService::executeManageMods(const QJsonObject &args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }
    QString action = args["action"].toString().trimmed().toLower();
    if (action != QStringLiteral("enable") && action != QStringLiteral("disable")
        && action != QStringLiteral("delete"))
    {
        return QStringLiteral("错误：action 只能是 enable / disable / delete");
    }

    // 目标文件名列表（省略 = 全部模组）
    QStringList targets;
    QJsonArray nameArr = args["file_names"].toArray();
    for (const QJsonValue &v : nameArr)
    {
        QString n = v.toString().trimmed();
        if (!n.isEmpty())
        {
            targets.append(n);
        }
    }

    ModScanner scanner;
    LocalModList modList;
    if (!scanner.scanModsSync(instancePath, modList))
    {
        return QStringLiteral("扫描失败：无法读取 %1 的 mods 目录").arg(instancePath);
    }
    if (modList.mods.isEmpty())
    {
        return QStringLiteral("实例 %1 的 mods 目录为空，没有可管理的模组。").arg(instancePath);
    }

    // 文件名归一化：去 .disabled 后缀、转小写，便于匹配
    auto normalizeName = [](const QString &fileName) {
        QString n = fileName;
        if (n.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
        {
            n.chop(QStringLiteral(".disabled").size());
        }
        return n.toLower();
    };
    QSet<QString> targetSet;
    for (const QString &t : targets)
    {
        targetSet.insert(normalizeName(t));
    }

    QStringList resultLines;
    int succeeded = 0;
    int failed = 0;
    for (const ModInfo &mod : modList.mods)
    {
        QString displayName = mod.name.isEmpty() ? mod.fileName : mod.name;
        if (!targetSet.isEmpty() && !targetSet.contains(normalizeName(mod.fileName)))
        {
            continue;
        }

        QString oldPath = mod.filePath;
        bool ok = false;
        QString detail;
        if (action == QStringLiteral("enable"))
        {
            if (mod.enabled)
            {
                ok = true;
                detail = QStringLiteral("已是启用状态，跳过");
            }
            else if (oldPath.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
            {
                QString newPath = oldPath.left(oldPath.size()
                                               - QStringLiteral(".disabled").size());
                ok = QFile::rename(oldPath, newPath);
                detail = ok ? QStringLiteral("已启用") : QStringLiteral("重命名失败");
            }
            else
            {
                ok = false;
                detail = QStringLiteral("文件缺少 .disabled 后缀，无法启用");
            }
        }
        else if (action == QStringLiteral("disable"))
        {
            if (!mod.enabled)
            {
                ok = true;
                detail = QStringLiteral("已是禁用状态，跳过");
            }
            else
            {
                ok = QFile::rename(oldPath, oldPath + QStringLiteral(".disabled"));
                detail = ok ? QStringLiteral("已禁用") : QStringLiteral("重命名失败");
            }
        }
        else // delete
        {
            ok = QFile::remove(oldPath);
            QString disabledPath = oldPath + QStringLiteral(".disabled");
            if (QFile::exists(disabledPath))
            {
                ok = QFile::remove(disabledPath) && ok;
            }
            detail = ok ? QStringLiteral("已删除") : QStringLiteral("删除失败");
        }

        if (ok)
        {
            succeeded++;
        }
        else
        {
            failed++;
        }
        resultLines.append(QStringLiteral("%1: %2").arg(displayName, detail));
    }

    if (resultLines.isEmpty())
    {
        return QStringLiteral("没有匹配到目标模组。请用 list_mods 确认模组文件名后重试。");
    }

    QStringList lines;
    lines.append(QStringLiteral("操作: %1，成功 %2 项，失败 %3 项")
                     .arg(action)
                     .arg(succeeded)
                     .arg(failed));
    lines.append(resultLines.join('\n'));
    return lines.join('\n');
}

QString AiService::executeCheckModUpdates(const QJsonObject &args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }

    // 资源包 / 光影包 / 数据包走同一套哈希比对，只是扫描目录和文案不同
    const QString resourceType = args["resource_type"].toString().trimmed().toLower();
    if (!resourceType.isEmpty() && resourceType != QStringLiteral("mod")
        && resourceType != QStringLiteral("mods"))
    {
        return executeCheckFileResourceUpdates(args, resourceType);
    }

    ModScanner scanner;
    LocalModList modList;
    if (!scanner.scanModsSync(instancePath, modList))
    {
        return QStringLiteral("扫描失败：无法读取 %1 的 mods 目录").arg(instancePath);
    }
    if (modList.mods.isEmpty())
    {
        return QStringLiteral("实例 %1 的 mods 目录为空，没有可检查的模组。").arg(instancePath);
    }

    QStringList gameVersions;
    QString gv = args["game_version"].toString().trimmed();
    if (!gv.isEmpty())
    {
        gameVersions.append(gv);
    }
    QStringList loaders;
    QString loader = args["loader"].toString().trimmed().toLower();
    if (!loader.isEmpty())
    {
        loaders.append(loader);
    }

    // 收集本地文件指纹：Modrinth 用 SHA-1，CurseForge 用 MurmurHash2
    QMap<QString, ModInfo> modsBySha1;
    QList<QString> sha1List;
    QMap<quint32, ModInfo> modsByMurmur;
    QList<quint32> murmurList;
    for (const ModInfo &mod : modList.mods)
    {
        if (!mod.sha1Hash.isEmpty() && !modsBySha1.contains(mod.sha1Hash))
        {
            modsBySha1.insert(mod.sha1Hash, mod);
            sha1List.append(mod.sha1Hash);
        }
        if (mod.curseforgeHash != 0 && !modsByMurmur.contains(mod.curseforgeHash))
        {
            modsByMurmur.insert(mod.curseforgeHash, mod);
            murmurList.append(mod.curseforgeHash);
        }
    }

    // ── 第一步：Modrinth 批量更新检查 ──
    QMap<QString, ModVersionFile> modrinthUpdates;
    {
        ModrinthAPI api;
        QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
        bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
        if (useMcim)
        {
            api.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/modrinth"));
        }

        QEventLoop loop;
        QTimer::singleShot(30000, &loop, [&]() { loop.quit(); });
        QObject::connect(&api, &ModrinthAPI::updatesChecked, &loop,
            [&](const QMap<QString, ModVersionFile>& versions) {
                modrinthUpdates = versions;
                loop.quit();
            });
        QObject::connect(&api, &ModrinthAPI::updatesCheckFailed, &loop, [&]() { loop.quit(); });

        api.checkModUpdates(sha1List, gameVersions, loaders);
        loop.exec();
    }

    // Modrinth 未命中的模组再走 CurseForge 指纹识别（至少识别出模组与主页）
    auto matchedOnModrinth = [&modrinthUpdates](const ModInfo &mod) {
        return !mod.sha1Hash.isEmpty() && modrinthUpdates.contains(mod.sha1Hash);
    };

    QMap<quint32, QString> cfFingerprintToModId;
    QMap<QString, ModInfo> cfModsById;
    QList<quint32> unmatchedMurmurs;
    for (const ModInfo &mod : modList.mods)
    {
        if (mod.curseforgeHash != 0 && !matchedOnModrinth(mod))
        {
            unmatchedMurmurs.append(mod.curseforgeHash);
        }
    }
    if (!unmatchedMurmurs.isEmpty())
    {
        CurseForgeAPI api;
        QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
        api.setApiKey(apiKey);
        QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
        bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
        if (useMcim)
        {
            api.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/curseforge"));
        }

        QEventLoop loop;
        QTimer::singleShot(30000, &loop, [&]() { loop.quit(); });
        QObject::connect(&api, &CurseForgeAPI::fingerprintsCompleted, &loop,
            [&](const QMap<quint32, QString>& map) {
                cfFingerprintToModId = map;
                loop.quit();
            });
        QObject::connect(&api, &CurseForgeAPI::fingerprintsFailed, &loop, [&]() { loop.quit(); });

        api.fingerprintMods(unmatchedMurmurs);
        loop.exec();

        // 指纹命中的模组 ID 拉取基础信息（名称与主页）
        QStringList cfModIds = cfFingerprintToModId.values();
        cfModIds.removeAll(QString());
        if (!cfModIds.isEmpty())
        {
            QEventLoop loop2;
            QTimer::singleShot(30000, &loop2, [&]() { loop2.quit(); });
            QObject::connect(&api, &CurseForgeAPI::modsResolved, &loop2,
                [&](const QList<ModInfo>& mods) {
                    for (const ModInfo &m : mods)
                    {
                        cfModsById.insert(m.id, m);
                    }
                    loop2.quit();
                });
            api.fetchModsByIds(cfModIds);
            loop2.exec();
        }
    }

    // ── 汇总输出 ──
    QStringList updatableLines;   // Modrinth 有更新
    QStringList upToDateLines;    // Modrinth 已是最新
    QStringList cfOnlyLines;      // 仅识别到 CurseForge 模组
    QStringList unknownLines;     // 两源均未匹配
    for (const ModInfo &mod : modList.mods)
    {
        QString displayName = mod.name.isEmpty() ? mod.fileName : mod.name;
        if (matchedOnModrinth(mod))
        {
            const ModVersionFile &latest = modrinthUpdates.value(mod.sha1Hash);
            bool hasUpdate = !latest.sha1.isEmpty() && latest.sha1 != mod.sha1Hash;
            if (hasUpdate)
            {
                updatableLines.append(
                    QStringLiteral("%1（文件 %2）：%3 → %4，文件 %5，发布于 %6\n  下载: %7")
                        .arg(displayName, mod.fileName,
                             mod.latestVersion.isEmpty() ? QStringLiteral("未知") : mod.latestVersion,
                             latest.version, latest.fileName,
                             latest.datePublished.toString(QStringLiteral("yyyy-MM-dd")),
                             latest.downloadUrl));
            }
            else
            {
                upToDateLines.append(displayName);
            }
            continue;
        }

        if (mod.curseforgeHash != 0 && cfFingerprintToModId.contains(mod.curseforgeHash))
        {
            QString modId = cfFingerprintToModId.value(mod.curseforgeHash);
            const ModInfo &cf = cfModsById.value(modId);
            QString page = !cf.pageUrl.isEmpty()
                               ? cf.pageUrl
                               : QStringLiteral("https://www.curseforge.com/minecraft/search?search=%1")
                                     .arg(QString::fromUtf8(QUrl::toPercentEncoding(displayName)));
            cfOnlyLines.append(QStringLiteral("%1（文件 %2）：已识别为 CurseForge 模组 %3，未匹配到 Modrinth 版本数据，可到主页核对更新\n  主页: %4")
                                   .arg(displayName, mod.fileName,
                                        cf.name.isEmpty() ? modId : cf.name, page));
            continue;
        }

        unknownLines.append(displayName);
    }

    QStringList lines;
    lines.append(QStringLiteral("模组更新检查结果（共 %1 个模组，联网检查 %2 个；Modrinth 可更新 %3，已是最新 %4，仅识别到 CurseForge %5，未匹配 %6）")
                     .arg(modList.mods.size())
                     .arg(sha1List.size() + murmurList.size())
                     .arg(updatableLines.size())
                     .arg(upToDateLines.size())
                     .arg(cfOnlyLines.size())
                     .arg(unknownLines.size()));

    if (!updatableLines.isEmpty())
    {
        lines.append(QStringLiteral("\n## 可更新的模组（%1 个）").arg(updatableLines.size()));
        lines.append(updatableLines.join(QStringLiteral("\n")));
    }
    if (!cfOnlyLines.isEmpty())
    {
        lines.append(QStringLiteral("\n## 仅在 CurseForge 识别到（%1 个）").arg(cfOnlyLines.size()));
        lines.append(cfOnlyLines.join(QStringLiteral("\n")));
    }
    if (!upToDateLines.isEmpty())
    {
        lines.append(QStringLiteral("\n## 已是最新（%1 个）").arg(upToDateLines.size()));
        lines.append(upToDateLines.join(QStringLiteral("、")));
    }
    if (!unknownLines.isEmpty())
    {
        int show = qMin(unknownLines.size(), 15);
        lines.append(QStringLiteral("\n## 未匹配到在线数据（%1 个，%2）")
                         .arg(unknownLines.size())
                         .arg(unknownLines.size() > show ? QStringLiteral("仅列前 %1 个") : QStringLiteral("全部")));
        lines.append(unknownLines.mid(0, show).join(QStringLiteral("、")));
        if (unknownLines.size() > show)
        {
            lines.append(QStringLiteral("..."));
        }
    }

    return lines.join('\n');
}

QString AiService::executeCheckFileResourceUpdates(const QJsonObject &args,
                                                   const QString &resourceType)
{
    const QString instancePath = args["instance_path"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }

    QString folder;
    QString displayName;
    QStringList validSuffixes;
    const QString type = resourceType.trimmed().toLower();
    if (type == QStringLiteral("resourcepack") || type == QStringLiteral("resourcepacks"))
    {
        folder = QStringLiteral("resourcepacks");
        displayName = QStringLiteral("资源包");
        validSuffixes << QStringLiteral(".zip");
    }
    else if (type == QStringLiteral("shader") || type == QStringLiteral("shaderpack")
             || type == QStringLiteral("shaderpacks"))
    {
        folder = QStringLiteral("shaderpacks");
        displayName = QStringLiteral("光影包");
        validSuffixes << QStringLiteral(".zip");
    }
    else if (type == QStringLiteral("datapack") || type == QStringLiteral("datapacks")
             || type == QStringLiteral("data_pack"))
    {
        folder = QStringLiteral("datapacks");
        displayName = QStringLiteral("数据包");
        validSuffixes << QStringLiteral(".zip") << QStringLiteral(".jar");
    }
    else
    {
        return QStringLiteral("错误：不支持的 resource_type「%1」，可选 mod / resourcepack / shader / datapack")
            .arg(resourceType);
    }

    const QDir dir(instancePath + QStringLiteral("/") + folder);
    if (!dir.exists())
    {
        return QStringLiteral("实例 %1 没有 %2 目录（%3），没有可检查的%4。")
            .arg(instancePath, folder, dir.absolutePath(), displayName);
    }

    // 收集本地文件指纹：sha1 → 文件显示名列表（同名内容可能存了多份）
    QMap<QString, QStringList> namesBySha1;
    QStringList disabledNames;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo &fi : entries)
    {
        const QString lower = fi.fileName().toLower();
        bool matched = false;
        for (const QString &suffix : validSuffixes)
        {
            if (lower.endsWith(suffix) || lower.endsWith(suffix + QStringLiteral(".disabled")))
            {
                matched = true;
                break;
            }
        }
        if (!matched)
            continue;

        QFile file(fi.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(&file);
        const QString sha1 = QString::fromLatin1(hash.result().toHex());
        file.close();
        if (sha1.isEmpty())
            continue;

        namesBySha1[sha1].append(fi.fileName());
        if (fi.fileName().endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
            disabledNames.append(fi.fileName());
    }

    if (namesBySha1.isEmpty())
    {
        return QStringLiteral("实例 %1 的 %2 目录（%3）里没有可联网检查更新的%4。")
            .arg(instancePath, folder, displayName);
    }

    QStringList sha1s = namesBySha1.keys();
    sha1s.sort();

    QMap<QString, ModVersionFile> installed;
    QMap<QString, ModVersionFile> latest;
    {
        ModrinthAPI api;
        QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
        bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
        if (useMcim)
        {
            api.setBaseUrl(QStringLiteral("https://mod.mcimirror.top/modrinth"));
        }

        // 第一步：批量哈希匹配，拿到已安装版本
        {
            QEventLoop loop;
            QTimer::singleShot(30000, &loop, [&]() { loop.quit(); });
            QObject::connect(&api, &ModrinthAPI::hashesMatched, &loop,
                [&](const QMap<QString, ModVersionFile> &versions) {
                    installed = versions;
                    loop.quit();
                });
            QObject::connect(&api, &ModrinthAPI::hashesMatchFailed, &loop,
                             [&]() { loop.quit(); });
            api.matchHashes(sha1s);
            loop.exec();
        }

        // 第二步：批量取最新版本。资源包 / 光影包 / 数据包不按加载器与游戏版本过滤
        {
            QEventLoop loop;
            QTimer::singleShot(30000, &loop, [&]() { loop.quit(); });
            QObject::connect(&api, &ModrinthAPI::updatesChecked, &loop,
                [&](const QMap<QString, ModVersionFile> &versions) {
                    latest = versions;
                    loop.quit();
                });
            QObject::connect(&api, &ModrinthAPI::updatesCheckFailed, &loop,
                             [&]() { loop.quit(); });
            api.checkModUpdates(sha1s, QStringList(), QStringList());
            loop.exec();
        }
    }

    QStringList updatableLines;
    QStringList upToDateLines;
    QStringList unknownLines;
    for (const QString &sha1 : sha1s)
    {
        const QStringList names = namesBySha1.value(sha1);
        const QString label = names.join(QStringLiteral("、"));
        if (!latest.contains(sha1) || !installed.contains(sha1))
        {
            unknownLines.append(label);
            continue;
        }

        const ModVersionFile target = latest.value(sha1);
        const ModVersionFile current = installed.value(sha1);
        bool hasUpdate = false;
        if (target.sha1.isEmpty() || current.sha1.isEmpty())
        {
            hasUpdate = target.sha1 != current.sha1;
        }
        else if (target.sha1 != current.sha1
                 && current.datePublished.isValid()
                 && target.datePublished > current.datePublished)
        {
            hasUpdate = true;
        }

        if (!hasUpdate)
        {
            upToDateLines.append(label);
            continue;
        }

        updatableLines.append(
            QStringLiteral("- %1：%2 → %3，新文件 %4，发布于 %5\n  下载: %6")
                .arg(label,
                     current.version.isEmpty() ? QStringLiteral("未知版本") : current.version,
                     target.version.isEmpty() ? QStringLiteral("新版本") : target.version,
                     target.fileName,
                     target.datePublished.toString(QStringLiteral("yyyy-MM-dd")),
                     target.downloadUrl));
    }

    QStringList lines;
    lines.append(QStringLiteral("%1更新检查结果（共 %2 个，联网检查 %3 个；可更新 %4，已是最新 %5，未匹配 %6）")
                     .arg(displayName)
                     .arg(namesBySha1.size())
                     .arg(sha1s.size())
                     .arg(updatableLines.size())
                     .arg(upToDateLines.size())
                     .arg(unknownLines.size()));
    if (!disabledNames.isEmpty())
    {
        lines.append(QStringLiteral("注意：其中 %1 个处于禁用状态（.disabled），更新后会保持禁用。")
                         .arg(disabledNames.size()));
    }
    if (installed.isEmpty() && latest.isEmpty())
    {
        lines.append(QStringLiteral("联网检查没有拿到结果（可能是网络或镜像不可用），稍后可在「实例管理 > %1」页手动点「检查更新」重试。")
                         .arg(displayName));
    }
    if (!updatableLines.isEmpty())
    {
        lines.append(QStringLiteral("\n## 可更新的%1（%2 个）").arg(displayName).arg(updatableLines.size()));
        lines.append(updatableLines.join(QStringLiteral("\n")));
    }
    if (!upToDateLines.isEmpty())
    {
        lines.append(QStringLiteral("\n## 已是最新（%1 个）").arg(upToDateLines.size()));
        lines.append(upToDateLines.join(QStringLiteral("、")));
    }
    if (!unknownLines.isEmpty())
    {
        lines.append(QStringLiteral("\n## 未匹配到在线数据（%1 个）").arg(unknownLines.size()));
        lines.append(unknownLines.join(QStringLiteral("、")));
    }
    lines.append(QStringLiteral("\n提示：在「实例管理 > %1」页可以点「检查更新」查看可更新标记，并一键更新。")
                     .arg(displayName));
    return lines.join('\n');
}

QString AiService::executeListSaves(const QJsonObject &args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }

    QString savesDir = WorldBackupManager::savesDirForInstance(instancePath);
    QStringList worlds = WorldBackupManager::listWorldNames(instancePath);
    if (worlds.isEmpty())
    {
        return QStringLiteral("实例 %1 的存档目录（%2）为空或不存在。").arg(instancePath, savesDir);
    }

    QStringList lines;
    lines.append(QStringLiteral("存档目录: %1").arg(savesDir));
    lines.append(QStringLiteral("共 %1 个存档：").arg(worlds.size()));
    for (const QString &world : worlds)
    {
        QString folder = savesDir + QStringLiteral("/") + world;
        auto info = LevelDatEditor::readSave(folder);
        if (info.has_value())
        {
            QString lastPlayed = info->lastPlayed > 0
                                     ? QDateTime::fromMSecsSinceEpoch(info->lastPlayed)
                                           .toString(QStringLiteral("yyyy-MM-dd hh:mm"))
                                     : QStringLiteral("未知");
            lines.append(QStringLiteral("  - 文件夹: %1 | 存档名: %2 | 版本: %3 | 最后游玩: %4")
                             .arg(world,
                                  info->levelName.isEmpty() ? world : info->levelName,
                                  info->versionName.isEmpty() ? QStringLiteral("未知") : info->versionName,
                                  lastPlayed));
        }
        else
        {
            lines.append(QStringLiteral("  - 文件夹: %1（无法解析 level.dat）").arg(world));
        }
    }
    return lines.join('\n');
}

QString AiService::executeReadSaveInfo(const QJsonObject &args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    QString worldName = args["world_name"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }
    if (!isValidWorldFolderName(worldName))
    {
        return QStringLiteral("错误：world_name 应为存档文件夹名（可用 list_saves 查询）");
    }

    QString savesDir = WorldBackupManager::savesDirForInstance(instancePath);
    QString folder = savesDir + QStringLiteral("/") + worldName;
    if (!QDir(folder).exists())
    {
        return QStringLiteral("存档文件夹不存在：%1").arg(folder);
    }

    auto info = LevelDatEditor::readSave(folder);
    if (!info.has_value())
    {
        return QStringLiteral("无法解析 %1/level.dat（存档可能损坏）。").arg(folder);
    }

    QStringList lines;
    lines.append(QStringLiteral("存档: %1（文件夹 %2）")
                     .arg(info->levelName.isEmpty() ? worldName : info->levelName, worldName));
    lines.append(QStringLiteral("游戏版本: %1")
                     .arg(info->versionName.isEmpty() ? QStringLiteral("未知") : info->versionName));
    if (info->lastPlayed > 0)
    {
        lines.append(QStringLiteral("最后游玩: %1")
                         .arg(QDateTime::fromMSecsSinceEpoch(info->lastPlayed)
                                  .toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
    }
    if (info->playTimeTicks > 0)
    {
        lines.append(QStringLiteral("累计游玩: %1").arg(ticksToDuration(info->playTimeTicks)));
    }
    if (info->hasSeed)
    {
        // 与游戏 /seed 一致的显示方式（按有符号 64 位展示）
        lines.append(QStringLiteral("世界种子: %1").arg(static_cast<qint64>(info->seed)));
    }
    else
    {
        lines.append(QStringLiteral("世界种子: 未读取到"));
    }
    if (info->hasSpawn)
    {
        lines.append(QStringLiteral("出生点: X=%1, Y=%2, Z=%3")
                         .arg(info->spawnX)
                         .arg(info->spawnY)
                         .arg(info->spawnZ));
    }
    if (info->hasGameType)
    {
        lines.append(QStringLiteral("游戏模式: %1%2")
                         .arg(gameTypeName(info->gameType),
                              info->hardcore ? QStringLiteral("（极限模式）") : QString()));
    }
    lines.append(QStringLiteral("允许作弊: %1").arg(info->allowCommands ? QStringLiteral("是") : QStringLiteral("否")));
    if (info->hasDifficulty)
    {
        lines.append(QStringLiteral("难度: %1%2")
                         .arg(difficultyName(info->difficulty),
                              info->difficultyLocked ? QStringLiteral("（已锁定）") : QString()));
    }
    if (info->hasDayTime)
    {
        lines.append(QStringLiteral("游戏内时间: 第 %1 天")
                         .arg(info->dayTime / 24000 + 1));
    }

    auto player = LevelDatEditor::readPlayerState(folder);
    if (player.has_value() && player->hasPos)
    {
        lines.append(QStringLiteral("玩家位置: X=%1, Y=%2, Z=%3（%4）")
                         .arg(QString::number(player->x, 'f', 1))
                         .arg(QString::number(player->y, 'f', 1))
                         .arg(QString::number(player->z, 'f', 1))
                         .arg(dimensionName(player->dimension)));
    }

    return lines.join('\n');
}

QString AiService::executeManageWorldBackup(const QJsonObject &args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    QString worldName = args["world_name"].toString().trimmed();
    QString action = args["action"].toString().trimmed().toLower();
    QString backupName = args["backup_name"].toString().trimmed();

    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }
    if (!isValidWorldFolderName(worldName))
    {
        return QStringLiteral("错误：world_name 应为存档文件夹名（可用 list_saves 查询）");
    }
    if (action != QStringLiteral("list") && action != QStringLiteral("create")
        && action != QStringLiteral("restore"))
    {
        return QStringLiteral("错误：action 只能是 list / create / restore");
    }

    WorldBackupManager *wbm = WorldBackupManager::instance();

    if (action == QStringLiteral("list"))
    {
        QList<WorldBackupInfo> backups = wbm->listBackups(instancePath, worldName);
        if (backups.isEmpty())
        {
            return QStringLiteral("存档 %1 暂无备份。").arg(worldName);
        }
        QStringList lines;
        lines.append(QStringLiteral("存档 %1 共 %2 个备份：").arg(worldName).arg(backups.size()));
        for (const WorldBackupInfo &b : backups)
        {
            lines.append(QStringLiteral("  - %1（创建于 %2，包大小 %3，原始 %4%5）")
                             .arg(b.id,
                                  b.createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm")),
                                  formatFileSize(b.archiveSize),
                                  formatFileSize(b.sourceSize),
                                  b.automatic ? QStringLiteral("，自动备份") : QString()));
        }
        return lines.join('\n');
    }

    if (action == QStringLiteral("create"))
    {
        WorldBackupInfo outInfo;
        QString error;
        bool ok = wbm->createBackupSync(instancePath, worldName, QString(), false, &outInfo, &error);
        if (!ok)
        {
            return QStringLiteral("备份失败：%1").arg(error.isEmpty() ? QStringLiteral("未知错误") : error);
        }
        // 与手动备份一致：超出保留份数时清理最旧的备份
        wbm->applyRetentionPolicy(instancePath, worldName, WorldBackupManager::keepCount());
        return QStringLiteral("备份成功：存档 %1 已备份为 %2（创建于 %3，包大小 %4）。")
            .arg(worldName,
                 outInfo.id,
                 outInfo.createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm")),
                 formatFileSize(outInfo.archiveSize));
    }

    // restore
    if (backupName.isEmpty())
    {
        return QStringLiteral("错误：restore 需要提供 backup_name（可用 action=list 查询备份名）");
    }
    QString error;
    bool ok = wbm->restoreBackupSync(instancePath, worldName, backupName, &error);
    if (!ok)
    {
        return QStringLiteral("恢复失败：%1").arg(error.isEmpty() ? QStringLiteral("未知错误") : error);
    }
    return QStringLiteral("恢复成功：存档 %1 已恢复到备份 %2。")
        .arg(worldName, backupName);
}

QString AiService::executeListServers(const QJsonObject &args)
{
    QString instancePath = args["instance_path"].toString().trimmed();
    if (instancePath.isEmpty())
    {
        return QStringLiteral("错误：缺少 instance_path 参数");
    }

    QStringList candidates = ServersDatIO::candidatePaths(instancePath);
    QString path = ServersDatIO::existingPath(candidates);
    if (path.isEmpty())
    {
        return QStringLiteral("该实例下未找到 servers.dat（从未添加过服务器或尚未启动过游戏）。");
    }

    QList<ServersDatIO::Entry> entries = ServersDatIO::read(path);
    if (entries.isEmpty())
    {
        return QStringLiteral("服务器列表（%1）为空。").arg(path);
    }

    QStringList lines;
    lines.append(QStringLiteral("服务器列表（来源 %1，共 %2 个）：").arg(path).arg(entries.size()));
    for (int i = 0; i < entries.size(); ++i)
    {
        const ServersDatIO::Entry &e = entries[i];
        QString name = e.name.isEmpty() ? QStringLiteral("（未命名）") : e.name;
        lines.append(QStringLiteral("  %1. %2 — %3").arg(i + 1).arg(name, ServersDatIO::formatAddress(e.host, e.port)));
    }
    return lines.join('\n');
}

QString AiService::executePingServer(const QJsonObject &args)
{
    QString address = args["address"].toString().trimmed();
    if (address.isEmpty())
    {
        return QStringLiteral("错误：缺少 address 参数");
    }

    QString host;
    quint16 port = 25565;
    if (!ServersDatIO::parseAddress(address, host, port) || host.isEmpty())
    {
        host = address;
        port = 25565;
    }

    // 临时探测实例（线程亲和性为当前子线程），QEventLoop 等待探测结果
    McServerPinger pinger;
    McServerPinger::Result result;
    QEventLoop loop;
    QTimer::singleShot(15000, &loop, [&]() { loop.quit(); });
    QObject::connect(&pinger, &McServerPinger::pingFinished, &loop,
        [&](const QString&, const McServerPinger::Result &r) {
            result = r;
            loop.quit();
        });

    pinger.ping(QStringLiteral("ai_ping"), host, port);
    loop.exec();

    QStringList lines;
    lines.append(QStringLiteral("服务器: %1").arg(ServersDatIO::formatAddress(host, port)));
    if (result.state == McServerPinger::Result::Online)
    {
        lines.append(QStringLiteral("状态: 在线"));
        if (result.latencyMs >= 0)
        {
            lines.append(QStringLiteral("延迟: %1 ms").arg(result.latencyMs));
        }
        if (!result.versionName.isEmpty())
        {
            lines.append(QStringLiteral("服务端版本: %1").arg(result.versionName));
        }
        lines.append(QStringLiteral("在线人数: %1 / %2")
                         .arg(result.playersOnline)
                         .arg(result.playersMax));
        if (!result.motd.isEmpty())
        {
            lines.append(QStringLiteral("MOTD: %1").arg(result.motd));
        }
    }
    else
    {
        lines.append(QStringLiteral("状态: 离线或无法连接"));
        if (!result.error.isEmpty())
        {
            lines.append(QStringLiteral("原因: %1").arg(result.error));
        }
    }
    return lines.join('\n');
}

QString AiService::executeListDownloadTasks()
{
    QList<DownloadTask> tasks = DownloadTaskManager::instance()->getAllTasks();
    if (tasks.isEmpty())
    {
        return QStringLiteral("当前没有下载任务。");
    }

    QStringList lines;
    lines.append(QStringLiteral("当前共 %1 个下载任务：").arg(tasks.size()));
    for (const DownloadTask &t : tasks)
    {
        QString name = t.instanceName.isEmpty() ? t.taskId : t.instanceName;
        if (!t.mcVersion.isEmpty())
        {
            name += QStringLiteral("（%1）").arg(t.mcVersion);
        }
        QString speed;
        if (t.status == DownloadTaskStatus::Downloading && t.downloadSpeed > 0)
        {
            speed = QStringLiteral("，速度 %1/s").arg(formatFileSize(t.downloadSpeed));
        }
        QString progress = QStringLiteral("%1%").arg(t.progress);
        if (t.bytesTotal > 0)
        {
            progress += QStringLiteral("（%1/%2）")
                            .arg(formatFileSize(t.bytesReceived), formatFileSize(t.bytesTotal));
        }
        lines.append(QStringLiteral("  - [%1] %2：%3，%4%5%6")
                         .arg(taskTypeDisplayName(t.type),
                              name,
                              downloadTaskStatusText(t.status),
                              progress,
                              speed,
                              t.statusMessage.isEmpty() ? QString()
                                                        : QStringLiteral("，%1").arg(t.statusMessage)));
    }
    return lines.join('\n');
}

QString AiService::executeFindStructure(const QJsonObject &args)
{
    QJsonValue seedValue = args["seed"];
    if (seedValue.isUndefined() || seedValue.isNull())
    {
        return QStringLiteral("错误：缺少 seed 参数（可先用 read_save_info 读取存档种子）");
    }
    bool seedOk = false;
    qint64 seed = static_cast<qint64>(seedValue.toVariant().toLongLong(&seedOk));
    if (!seedOk)
    {
        return QStringLiteral("错误：seed 参数无效，应为整数（可先用 read_save_info 读取存档种子）");
    }

    QString mcVersion = args["mc_version"].toString().trimmed();
    int mc = mcVersion.isEmpty() ? 0 : StructureFinder::versionToInt(mcVersion);
    if (mc == 0)
    {
        return QStringLiteral("错误：无法识别游戏版本 %1（示例：1.20.1、1.21.4）").arg(mcVersion);
    }

    QString dimStr = args["dimension"].toString().trimmed().toLower();
    int dim;
    if (dimStr == QStringLiteral("nether"))
    {
        dim = -1;
    }
    else if (dimStr == QStringLiteral("end"))
    {
        dim = 1;
    }
    else
    {
        dim = 0;
    }

    // 范围：默认 ±10000，单边上限 ±60000（区域网格数随范围平方增长，防失控）
    qint64 minX = args["min_x"].toInt(-10000);
    qint64 minZ = args["min_z"].toInt(-10000);
    qint64 maxX = args["max_x"].toInt(10000);
    qint64 maxZ = args["max_z"].toInt(10000);
    minX = qBound<qint64>(qint64(-60000), minX, qint64(60000));
    minZ = qBound<qint64>(qint64(-60000), minZ, qint64(60000));
    maxX = qBound<qint64>(qint64(-60000), maxX, qint64(60000));
    maxZ = qBound<qint64>(qint64(-60000), maxZ, qint64(60000));
    if (minX >= maxX || minZ >= maxZ)
    {
        return QStringLiteral("错误：范围参数无效（min_x/min_z 必须小于 max_x/max_z）");
    }

    // 结构开关：默认仅稀有结构；传 structures 时按 key/中文名匹配
    const QVector<StructureFinder::StructureDef> &defs = StructureFinder::allStructures();
    QSet<QString> wanted;
    QJsonArray structArr = args["structures"].toArray();
    for (const QJsonValue &v : structArr)
    {
        QString s = v.toString().trimmed().toLower();
        if (!s.isEmpty())
        {
            wanted.insert(s);
        }
    }

    QVector<bool> enabled(defs.size(), false);
    bool anyEnabled = false;
    QStringList unknownKeys;
    for (int i = 0; i < defs.size(); ++i)
    {
        const StructureFinder::StructureDef &def = defs[i];
        if (def.type == StructureFinder::kUnsupportedType)
        {
            continue;
        }
        if (wanted.isEmpty())
        {
            enabled[i] = def.rare;
        }
        else
        {
            QString key = QString::fromUtf8(def.key).toLower();
            QString display = QString::fromUtf8(def.displayName);
            enabled[i] = wanted.contains(key) || wanted.contains(display.toLower());
        }
        anyEnabled = anyEnabled || enabled[i];
    }
    for (const QString &w : wanted)
    {
        bool found = false;
        for (const StructureFinder::StructureDef &def : defs)
        {
            if (QString::fromUtf8(def.key).toLower() == w
                || QString::fromUtf8(def.displayName).toLower() == w)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            unknownKeys.append(w);
        }
    }

    if (!anyEnabled)
    {
        QStringList known;
        for (const StructureFinder::StructureDef &def : defs)
        {
            if (def.type != StructureFinder::kUnsupportedType)
            {
                known.append(QString::fromUtf8(def.key));
            }
        }
        return QStringLiteral("错误：没有可查找的结构类型。可用类型：%1")
            .arg(known.join(QStringLiteral("、")));
    }

    QVector<StructureFinder::Hit> hits =
        StructureFinder::find(static_cast<quint64>(seed), mc, dim,
                              minX, minZ, maxX, maxZ, enabled);

    QStringList lines;
    lines.append(QStringLiteral("种子 %1（版本 %2，维度 %3，范围 X[%4,%5] Z[%6,%7]）命中 %8 个结构：")
                     .arg(seed)
                     .arg(mcVersion, dimensionName(dim))
                     .arg(minX)
                     .arg(maxX)
                     .arg(minZ)
                     .arg(maxZ)
                     .arg(hits.size()));
    if (!unknownKeys.isEmpty())
    {
        lines.append(QStringLiteral("忽略的未知结构类型：%1").arg(unknownKeys.join(QStringLiteral("、"))));
    }

    // 按结构类型分组输出，避免长列表混杂
    int shown = 0;
    const int maxShow = 200;
    for (int i = 0; i < defs.size() && shown < maxShow; ++i)
    {
        if (!enabled[i])
        {
            continue;
        }
        QStringList coords;
        for (const StructureFinder::Hit &hit : hits)
        {
            if (hit.structIdx == i)
            {
                coords.append(QStringLiteral("X=%1, Z=%2").arg(hit.x).arg(hit.z));
                shown++;
                if (shown >= maxShow)
                {
                    break;
                }
            }
        }
        if (!coords.isEmpty())
        {
            lines.append(QStringLiteral("  %1: %2")
                             .arg(QString::fromUtf8(defs[i].displayName),
                                  coords.join(QStringLiteral("；"))));
        }
    }
    if (hits.size() > shown)
    {
        lines.append(QStringLiteral("...（其余 %1 个省略）").arg(hits.size() - shown));
    }
    if (hits.isEmpty())
    {
        lines.append(QStringLiteral("（范围内未命中，可扩大范围或检查维度/版本是否正确）"));
    }
    lines.append(QStringLiteral("注：坐标为生成区域网格坐标，实际是否生成还受地形与生物群系影响。"));
    return lines.join('\n');
}
