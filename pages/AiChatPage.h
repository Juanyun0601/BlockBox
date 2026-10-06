/**
 * @file   AiChatPage.h
 * @brief  AI 聊天页面类声明
 * @author BlockBox Team
 * @date   2026-06-23
 */

#pragma once

#include <QElapsedTimer>
#include <QFrame>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include "components/OutlinedLabel.h"
#include "utils/AiService.h"
#include "utils/content/ContentData.h"
#include "utils/mod/ModData.h"

class QComboBox;
class QFrame;
class QHBoxLayout;

class AiService;
class ModelSelectDialog;

/**
 * @brief 对话数据结构
 */
struct Conversation
{
    QString id;                     // 唯一标识
    QString title;                  // 对话标题
    QList<ChatMessage> messages;    // 消息列表
    QString modelId;                // 使用的模型 ID
    QString createdAt;              // 创建时间
    int mode = 0;                   // 所属模式：0=聊天，1=工作
    QStringList systemPromptIds;    // 选中的系统提示词库 ID 列表（空=使用默认模式提示词）
};

class AiChatPage : public QWidget
{
    Q_OBJECT

public:
    explicit AiChatPage(QWidget *parent = nullptr);
    ~AiChatPage();

    /** 悬浮气泡参数 */
    static constexpr int kBubbleWidth = 240;   // 气泡固定宽度（与 SubNavPanel 一致）
    static constexpr int kBubbleMargin = 10;   // 气泡与宿主边缘间距

    /**
     * @brief 当前对话状态枚举
     *
     * 用于顶部栏状态指示器，反映 AI 当前所处的工作阶段。
     */
    enum class ChatStatus
    {
        Idle,             // 空闲
        Thinking,         // 思考中（流式开始后到收到首个内容前）
        Searching,        // 联网搜索中
        CallingTool,      // 调用 Agent 工具中
        CompressingContext, // 压缩上下文中
        Stopped,          // 用户已终止
        Error             // 发生错误
    };

    /**
     * @brief 注入当前实例上下文，启用实例资源引用功能
     * @param path    实例路径（versions/{ver} 目录或 .minecraft 根目录）
     * @param version 实例版本
     * @param loader  实例加载器名称
     *
     * 主侧边栏 AI 页与 InstanceAssistantWindow 均调用此方法注入实例上下文。
     * 实例路径非空时，「引用」对话框可列出该实例的模组/资源包/光影包/投影。
     */
    void setInstanceContext(const QString &path, const QString &version, const QString &loader);

    /**
     * @brief 启用紧凑模式（适用于实例助手等窄窗口场景）
     * @param compact 是否启用紧凑模式
     *
     * 启用后将缩小欢迎页内边距、快捷按钮列数（3→2）、消息区/输入区内边距，
     * 并通过动态属性 compactMode 触发 QSS 中的紧凑样式覆盖。
     */
    void setCompactMode(bool compact);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onNewConversation();
    void onConversationSelected(int row);
    void onConvListContextMenu(int index, const QPoint &globalPos);
    void onDeleteConversation();
    void onRenameConversation();
    void onCopyConversation();
    void onClearAllConversations();
    void onSendMessage();
    void onStopStreaming(); ///< 用户点击停止按钮，终止当前流式请求
    void onModelSelectClicked();
    void onReferenceClicked(); ///< 点击「引用」按钮，弹出资源选择对话框
    void onPromptOptimizeClicked(); ///< 点击「提示词优化」按钮，AI 改写输入框草稿
    void onPromptOptimized(const QString &optimized); ///< 提示词优化完成，写回输入框
    void onStreamContent(const QString &delta);
    void onStreamReasoning(const QString &delta);
    void onStreamFinished(const TokenUsage &usage);
    void onStreamError(const QString &error);
    void onToggleConvList();
    void onThinkingEffortChanged(int index); ///< 思考强度切换
    void onPermissionChanged(int index); ///< 操作权限切换（完全访问需居中确认）
    void onWebSearchResultsCollected(const QList<WebSearchResult> &results); ///< 累积搜索结果到「搜索」过程行
    void onAgentToolCallStarted(const QString &name, const QString &arguments); ///< Agent 工具调用开始
    void onAgentToolCallFinished(const QString &name, const QString &resultSummary,
                                 qint64 durationMs, bool success); ///< Agent 工具调用结束
    void onResourceDownloadRequested(const ModInfo &info, const ModVersionFile &file,
                                     const QString &instancePath, ContentType contentType); ///< Agent 触发资源下载
    void onContextCompressionStarted(); ///< 上下文压缩开始
    void onContextCompressionFinished(int originalTokens, int compressedTokens, bool success); ///< 上下文压缩结束
    void onTaskListUpdated(const QList<AgentTask> &tasks); ///< AI 自维护任务清单更新
    void onInstanceDownloadRequested(const QString &versionId, const QString &instancePath,
                                      const QString &instanceName, const QString &loader,
                                      const QString &loaderVersion, const QString &source); ///< Agent 触发实例下载
    void onInstanceModifyRequested(const QString &instancePath, const QString &newVersion,
                                    const QString &loader, const QString &loaderVersion,
                                    const QString &source); ///< Agent 触发实例修改
    void onUserQuestionAsked(const QString &question, const QStringList &options,
                              const QString &defaultValue, bool allowOther); ///< Agent 向用户提问
    void onTitleGenerated(const QString &title); ///< AI 自动生成对话标题完成
    void onSkillManage(); ///< 打开技能管理对话框

private:
    void initUI();
    void initConnections();
    void initDefaultModels();

    /**
     * @brief 更新顶部栏状态指示器
     * @param status 新状态
     *
     * 切换状态文本、颜色与发送按钮的可用性（流式期间发送按钮变为"停止"按钮）。
     */
    void updateChatStatus(ChatStatus status);

    /**
     * @brief 刷新发送/中止按钮的图标与外观
     * @param streaming true=流式中（显示红色中止方块图标），false=空闲（显示发送纸飞机图标）
     */
    void updateSendButtonIcon(bool streaming);

    /**
     * @brief 按 m_compactMode 重建输入框内工具行布局
     *
     * 普通模式：左侧三个选择框与右侧五个图标同一行；紧凑模式（实例助手窄窗口）
     * 拆为两行（选择框一行、图标一行），避免 450px 宽度溢出。
     * 首次在 initUI 中调用，setCompactMode 切换时重新调用。
     */
    void rebuildToolsRows();

    // 对话管理
    void addConversation(const Conversation &conv);
    void switchToConversation(int index);
    void updateConversationTitle(int index, const QString &title);
    void rebuildConvButtonsForMode();          ///< 按当前模式重建左侧对话按钮列表
    QList<int> filteredConvIndices() const;    ///< 当前模式下可见对话在 m_conversations 中的索引
    void switchToMode(int mode);               ///< 切换聊天/工作模式

    // 对话列表悬浮气泡：重新计算几何（宿主尺寸变化时调用）
    void updateConvBubbleGeometry();

    // 实例下载触发（download_instance / modify_instance 共用）
    void triggerInstanceDownload(const QString &versionId, const QString &instancePath,
                                 const QString &instanceName, const QString &loader,
                                 const QString &loaderVersion, const QString &source);

    // 消息显示
    void clearMessageArea();
    void showWelcome(bool visible);     ///< 欢迎区已移除：仅清空消息布局并隐藏欢迎控件
    void addMessageBubble(const ChatMessage &msg);
    QWidget* createMessageWidget(const QString &text, bool isUser,
                                 const QList<ResourceReference> &refs = {});
    QWidget* createThinkingWidget(const QString &text, bool running = false);
    QWidget* createTypingIndicator();

    // 流式渲染（正文/思考增量追加，避免 QLabel 全量重排）
    QTextEdit* createStreamEdit(const QString &objectName, QWidget *parent);
    void appendStreamText(QTextEdit *edit, const QString &full, int &flushedLen);
    void finalizeStreamWidgets(); ///< 收尾：正文切回 QLabel 的 Markdown 渲染，思考编辑器保留
    void invalidateStreamingWidgets(); ///< 消息控件整体销毁前作废流式指针与挂起的刷新

    // AI 气泡底部操作栏
    void createBubbleActions(QWidget *container, const QString &content);
    void updateBubbleStats(QWidget *container, const QString &statsText);
    void regenerateLastAnswer();
    void updateBubbleMaxWidths();

    // Agent 工具步骤卡片
    /**
     * @brief 创建一个工具/搜索过程行（含折叠展开，一轮一行样式）
     * @param name 步骤名（如「搜索」或工具名）
     * @param arguments 参数原文（展开态显示；可为空）
     * @param resultSummary 结果摘要（已完成态显示在行尾；进行中态传空）
     * @param durationMs 耗时（毫秒，进行中态传 0）
     * @param success 是否成功（进行中态传 true）
     * @param finished 是否已完成（false 表示进行中态）
     * @return 过程行容器 QWidget
     *
     * 收起态单行：SVG 折叠箭头 + 名称 + 状态 + 耗时 + 行尾摘要
     * 展开态：名称行下方缩进显示参数原文、结果原文
     */
    QWidget* createToolStepCard(const QString &name, const QString &arguments,
                                const QString &resultSummary, qint64 durationMs,
                                bool success, bool finished);
    /**
     * @brief 标记当前进行中的步骤卡片为完成态
     * @param name 工具名（用于匹配进行中卡片）
     * @param resultSummary 结果摘要
     * @param durationMs 耗时
     * @param success 是否成功
     */
    void finishToolStepCard(const QString &name, const QString &resultSummary,
                            qint64 durationMs, bool success);
    /**
     * @brief 将指定过程行更新为完成态（finishToolStepCard 与「搜索」行共用）
     * @param card 目标过程行
     * @param resultSummary 结果摘要（显示在行尾）
     * @param durationMs 耗时（毫秒）
     * @param success 是否成功
     * @param updateResultText 是否用摘要覆盖展开态结果区（「搜索」行传 false，保留站点列表）
     */
    void applyToolStepFinish(QWidget *card, const QString &resultSummary,
                             qint64 durationMs, bool success, bool updateResultText);
    /**
     * @brief 获取或创建当前回合的「搜索」过程行
     * @param query 搜索查询词（作为参数原文）
     * @return 搜索过程行指针
     */
    QWidget* ensureSearchStepCard(const QString &query);
    /**
     * @brief 从历史 ChatMessage 重建 Agent 步骤卡片（加载历史对话时调用）
     * @param toolCalls 该消息的工具调用记录列表
     */
    void rebuildToolStepCards(const QList<AgentToolCall> &toolCalls);

    /**
     * @brief 创建或刷新当前 AI 回复内的任务清单卡片
     * @param tasks 最新任务列表（覆盖式重绘）
     *
     * 首次调用时在当前 AI 气泡内创建卡片容器；后续调用清空容器内容后重新填充。
     * 任务按 id 排序展示，状态用图标区分（○ pending / ◐ in_progress / ✓ completed）。
     */
    void rebuildTaskListCard(const QList<AgentTask> &tasks);

    // 动画
    void slideHighlightTo(int index);
    void animateConvButtons();

    // 切换按钮图标刷新（根据主题色与展开状态）
    void updateToggleConvIcon();
    void slideModeIndicator(int targetIndex); ///< 模式指示器滑动动画
    void updateWelcomeContent();              ///< 根据当前模式刷新欢迎界面文字
    void injectPromptToInput(const QString &prompt); ///< 一键注入提示词到输入框
    /**
     * @brief 根据 m_compactMode 重建工作模式快捷按钮网格
     *
     * 紧凑模式使用 2 列布局以适配窄窗口（实例助手），普通模式使用 3 列布局。
     * 首次在 initUI 中调用，后续 setCompactMode 切换模式时重新调用。
     */
    void buildWelcomeQuickButtons();

    // 持久化
    void saveConversations();
    void doSaveConversations();   ///< 真正执行对话落盘（由防抖定时器触发或析构时直接调用）
    void loadConversations();
    void saveModelSettings();
    void loadModelSettings();
    /**
     * @brief 异步刷新本地 Ollama 已下载模型，合并到 m_models
     *
     * 启动时与 LocalModelDialog 关闭后调用。通过 HTTP GET /api/tags 拉取
     * Ollama 已下载模型清单，把 m_models 中旧的 isLocal=true 条目替换为最新结果。
     * 服务未运行时静默跳过（保留 m_models 现状，不删除旧条目）。
     */
    void refreshLocalModels();
    void saveThinkingEffort(); ///< 保存思考强度选择
    void loadThinkingEffort(); ///< 加载思考强度选择
    void savePermissionMode(); ///< 保存操作权限选择
    void loadPermissionMode(); ///< 加载操作权限选择
    void refreshSystemPromptSelector(); ///< 从提示词库刷新多选按钮
    QString resolveSystemPrompt() const; ///< 根据选中的提示词 ID 拼接系统提示词

    // 工作区选择
    void loadWorkspaceFolders();      ///< 加载已绑定文件夹列表与当前选择
    void saveWorkspaceSelection();    ///< 持久化当前工作区选择
    void showWorkspaceMenu();         ///< 弹出工作区选择菜单（不选/已绑定文件夹/绑定新文件夹）
    void applyWorkspaceSelection();   ///< 将当前工作区选择同步到 AiService 工作区限制
    void updateWorkspaceBtnText();    ///< 刷新工作区选择框文字
    void updateModelBtnText();        ///< 刷新模型选择框文字（定宽省略显示）
    QString conversationsFilePath() const;
    QString modelsFilePath() const;

    // 文本右键菜单
    void setupTextContextMenu(QLabel *label, bool isUser);

    // 资源引用处理
    void rebuildReferenceChips();              ///< 根据 m_pendingReferences 重建标签条
    void addReferenceChip(const ResourceReference &ref, int index); ///< 添加单个引用标签（含标号）
    void removeReferenceAt(int index);         ///< 移除指定索引的引用
    void clearPendingReferences();             ///< 清空所有待发送引用
    /**
     * @brief 将一段 AI 回答文字作为引用加入待发送列表
     * @param text 被引用的 AI 回答文字（可为选中片段或整段回复）
     *
     * 以 category="AI回答" 构造 ResourceReference：
     *  - name 保存截断预览（前 30 字符 + 省略号）
     *  - extra 保存完整引用文字（AI 可见，用于上下文）
     *  - path/size 留空
     * 添加后自动重建标签条，引用随下一条用户消息一同发送。
     */
    void addQuoteReference(const QString &text);
    /**
     * @brief 构建只读引用卡片控件（用于用户消息气泡内展示已发送的引用）
     * @param refs 引用列表
     * @return 控件指针（含标号 + 类别 + 名称，无关闭按钮）
     *
     * 与输入框标签条样式一致，但不带 × 关闭按钮，仅用于回显已发送引用。
     */
    QWidget* buildReadOnlyRefChips(const QList<ResourceReference> &refs);
    /**
     * @brief 将待发送引用列表格式化为附加到用户消息的文本描述
     * @return 格式化的引用描述文本（无引用时返回空字符串）
     *
     * 格式：每行一条「[类别] 名称 (大小, 路径[, 附加说明])」，
     * 整体包裹在「[引用资源]」段落中，附加在用户输入文本之后。
     */
    QString buildReferenceText() const;

    // 当前 AI 回复构建
    QString m_currentContent;
    QString m_currentReasoning;
    QElapsedTimer m_requestTimer; ///< 请求耗时计时
    QWidget* m_currentAiBubble;
    QWidget* m_currentThinkingBubble;
    QWidget* m_currentThinkingContent;
    QLabel* m_currentContentLabel;
    QLabel* m_currentThinkingLabel;
    QWidget* m_typingIndicator;
    QWidget* m_currentSearchStepCard; ///< 当前回合的「搜索」过程行（联网搜索状态）
    QElapsedTimer m_searchElapsedTimer; ///< 搜索耗时计时
    QList<WebSearchResult> m_currentSearchResults; ///< 当前回复累积的联网搜索结果
    QWidget* m_currentAgentStepsWidget; ///< 当前 AI 回复的 Agent 步骤容器
    QVBoxLayout* m_currentAgentStepsLayout; ///< Agent 步骤容器布局
    QList<QWidget*> m_currentToolStepCards; ///< 当前回合的步骤卡片列表（按创建顺序）
    QList<AgentToolCall> m_currentAgentToolCalls; ///< 当前回合累积的工具调用记录（供持久化）
    QWidget* m_currentPendingStepCard; ///< 进行中的步骤卡片指针（待 finishToolStepCard 更新）
    QWidget* m_currentTaskListCard; ///< 当前 AI 回复的任务清单卡片（由 update_task_list 驱动）
    bool m_isStreaming;
    bool m_streamAborting;  ///< 流式被主动中断（切换对话/新建对话等），忽略后续 streamFinished/error 信号

// 流式渲染节流
    QTimer *m_streamThrottle;       ///< 节流定时器，50ms 触发一次 UI 更新
    bool m_streamDirty;             ///< 节流期间有新内容待刷新
    bool m_streamThinkingDirty;     ///< 节流期间有新思考内容待刷新

    // 流式正文/思考改用 QTextEdit 增量追加：QLabel 每次 setText 都会重新排版并重绘全文，
    // 正文上千字后单次刷新耗时即超过 50ms 节流周期，导致回答过程中界面卡顿
    QTextEdit *m_currentContentEdit;   ///< 流式期间的正文编辑器（收尾时切回 QLabel 渲染 Markdown）
    QTextEdit *m_currentThinkingEdit;  ///< 流式期间的思考编辑器（纯文本，收尾后保留）
    int m_streamFlushedLen;            ///< 已追加进正文编辑器的字符数
    int m_streamThinkingFlushedLen;    ///< 已追加进思考编辑器的字符数

    // 对话持久化防抖（合并频繁的 saveConversations 调用为单次落盘）
    QTimer *m_saveDebounceTimer;    ///< 落盘防抖定时器（约 500ms）

    // AI 服务
    AiService *m_aiService;

    // 模型
    QList<AiModel> m_models;
    AiModel m_currentModel;

    // 对话数据
    QList<Conversation> m_conversations;
    int m_currentConvIndex;
    int m_pendingTitleConvIndex;  ///< 正在生成标题的对话索引（-1 表示无），用于跨对话切换时定位正确的对话

    // 左侧面板 - 子导航风格（悬浮气泡）
    QFrame *m_leftPanel;
    QWidget *m_bubbleHost;          ///< 气泡的浮动宿主（body 区域）
    QHBoxLayout *m_bubbleHostLayout; ///< 宿主布局（用于展开时左侧留出空隙）
    QPushButton *m_toggleConvBtn;   ///< 对话选择切换按钮（收起/展开）
    bool m_convListExpanded;        ///< 对话列表是否处于展开状态
    QPushButton *m_modeChatBtn;     ///< 聊天模式按钮
    QPushButton *m_modeWorkBtn;     ///< 工作模式按钮
    QWidget *m_modeIndicator;      ///< 模式切换滑动指示器
    QScrollArea *m_convScrollArea;
    QWidget *m_convContainer;
    QVBoxLayout *m_convLayout;
    QList<QPushButton *> m_convButtons;
    QPushButton *m_newConvBtn;
    QWidget *m_selectionHighlight;   ///< 选中态滑动背景
    int m_contextMenuIdx;

    // 顶部栏 - 对话选择按钮 + 当前对话名称
    QWidget *m_topBar;
    QLabel *m_statusDot;            ///< 顶部栏状态圆点（空闲灰 / 忙碌主题色 / 错误红）
    QLabel *m_convTitleLabel;       ///< 当前对话名称
    QLabel *m_statusLabel;         ///< 当前对话状态指示器（顶部栏右侧）
    ChatStatus m_chatStatus;       ///< 当前对话状态

    // 右侧面板
    QSplitter *m_splitter;
    QScrollArea *m_messageArea;
    QWidget *m_messageContainer;
    QVBoxLayout *m_messageLayout;

    // 输入区域
    QWidget *m_inputPanel;
    QTextEdit *m_inputEdit;
    QFrame *m_inputBox;             ///< 输入框圆角卡片容器（紧凑模式收紧内边距）
    QPushButton *m_sendBtn;
    QWidget *m_toolsArea;           ///< 输入框内工具行容器（随紧凑模式重建布局）
    QVBoxLayout *m_toolsAreaLayout; ///< 工具行容器布局
    QPushButton *m_modelBtn;        ///< 模型选择框（固定宽度，点击弹出模型选择窗口）
    QPushButton *m_workspaceBtn;    ///< 工作区选择框（不选/已绑定文件夹/绑定新文件夹）
    QStringList m_workspaceBoundFolders; ///< 已绑定的工作区文件夹列表
    QString m_workspaceSelectedPath;     ///< 当前选中的工作区文件夹（空=不选）
    QComboBox *m_thinkingEffortCombo;    ///< 思考强度选择框（关闭/低/中/高）
    int m_thinkingEffort;           ///< 当前思考强度（0=关闭，1=低，2=中，3=高）
    QComboBox *m_permissionCombo;   ///< 操作权限选择框（全部确认/关键确认/完全访问）
    int m_permissionMode;           ///< 当前操作权限（0=全部确认，1=关键确认，2=完全访问）
    int m_prevPermissionMode;       ///< 切换前的权限级别（完全访问取消时回退用）
    QPushButton *m_skillBtn; ///< 技能管理入口按钮
    QPushButton *m_systemPromptBtn; ///< 系统提示词库多选按钮
    QPushButton *m_promptOptimizeBtn; ///< 提示词优化按钮（AI 改写输入框草稿）
    bool m_promptOptimizeBusy;        ///< 提示词优化请求是否正在飞行（防并发、锁定按钮）
    QStringList m_selectedPromptIds; ///< 当前选中的系统提示词 ID

    // 资源引用区域
    QPushButton *m_referenceBtn;       ///< 「引用」按钮，弹出资源选择对话框
    QWidget *m_referenceChipsWidget;   ///< 引用资源标签条容器（输入框上方）
    QHBoxLayout *m_referenceChipsLayout; ///< 标签条布局
    QList<ResourceReference> m_pendingReferences; ///< 待附加到下一条消息的引用列表

    // 实例上下文（用于资源引用功能）
    QString m_instancePath;    ///< 当前实例路径（versions/{ver} 目录或 .minecraft 根目录）
    QString m_instanceVersion; ///< 当前实例版本
    QString m_instanceLoader;  ///< 当前实例加载器

    // 模型设置窗口（独立窗口）
    ModelSelectDialog *m_modelDialog;

    // 欢迎提示
    QWidget *m_welcomeWidget;       ///< 欢迎界面容器（标题 + 简介）
    OutlinedLabel *m_welcomeTitleLabel;  ///< 欢迎大标题
    QLabel *m_welcomeDescLabel;     ///< 欢迎简介文字
    QWidget *m_welcomeQuickBtns;    ///< 工作模式快捷按钮容器
    QGridLayout *m_welcomeQuickLayout; ///< 快捷按钮网格布局（便于切换列数）
    int m_currentMode;              ///< 当前模式：0=聊天，1=工作
    bool m_compactMode;             ///< 紧凑模式（实例助手窄窗口场景）
};