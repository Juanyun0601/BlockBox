/**
 * @file   AiChatPage.cpp
 * @brief  AI 聊天页面实现
 * @author BlockBox Team
 * @date   2026-06-23
 */

#include "AiChatPage.h"

#include "components/AppFileDialog.h"
#include "components/OutlinedLabel.h"
#include "components/ResourceReferenceDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include "components/AppInputDialog.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QListWidget>
#include <QListWidgetItem>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QActionGroup>
#include "components/AppMessageBox.h"
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpacerItem>
#include <QStandardPaths>
#include <QStyle>
#include <QTcpSocket>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QTextCursor>
#include <QRegularExpression>
#include <QUrl>
#include <QGraphicsDropShadowEffect>
#include <QFileDialog>
#include <QCheckBox>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSequentialAnimationGroup>

#include "components/ModelSelectDialog.h"
#include "components/NotificationManager.h"
#include "components/SkillManagerDialog.h"
#include "layouts/FlowLayout.h"
#include "utils/AiService.h"
#include "utils/DownloadTaskManager.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/VersionDownloader.h"
#include "utils/fabric/FabricInstaller.h"
#include "utils/forge/ForgeInstaller.h"
#include "utils/NeoForgeInstaller.h"
#include "utils/optifine/OptiFineInstaller.h"
#include "utils/content/ContentDownloader.h"

namespace
{
    constexpr const char *kChevronCollapsed = ":/Images/Icons/chevron_right.svg";
    constexpr const char *kChevronExpanded = ":/Images/Icons/chevron_down.svg";
    constexpr int kChevronSize = 12;

    /// 过程行折叠箭头图标（收起 ▶ / 展开 ∨）
    void setChevronPixmap(QLabel *chevronLabel, bool expanded)
    {
        if (!chevronLabel)
        {
            return;
        }
        chevronLabel->setPixmap(IconHelper::loadColoredIcon(
                                    expanded ? kChevronExpanded : kChevronCollapsed,
                                    QColor(0x88, 0x88, 0x88), kChevronSize)
                                    .pixmap(kChevronSize, kChevronSize));
    }

    /// 重新按当前宽度对思考行尾预览做左侧省略（"……最新思考"贴行尾）
    void refreshThinkingPreview(QLabel *previewLabel)
    {
        if (!previewLabel)
        {
            return;
        }
        const QString tail = previewLabel->property("fullTail").toString();
        const int maxW = qMax(previewLabel->width(), 60);
        previewLabel->setText(previewLabel->fontMetrics().elidedText(tail, Qt::ElideLeft, maxW));
    }

    /**
     * @brief 设置思考行尾预览文本
     *
     * 取思考全文的最新一段（换行拍平、最多保留末尾 80 字符），收起态在行尾
     * 向右滚动显示最新思考过程，前面的思考省略。布局未完成时延迟一拍再截断。
     */
    void setThinkingPreview(QLabel *previewLabel, const QString &reasoning)
    {
        if (!previewLabel)
        {
            return;
        }
        QString flat = reasoning;
        flat.replace(QLatin1Char('\r'), QLatin1Char(' '));
        flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
        constexpr int kMaxTailChars = 80;
        if (flat.size() > kMaxTailChars)
        {
            flat = flat.right(kMaxTailChars);
        }
        previewLabel->setProperty("fullTail", flat);
        if (previewLabel->width() > 0)
        {
            refreshThinkingPreview(previewLabel);
        }
        else
        {
            QTimer::singleShot(0, previewLabel, [previewLabel]() {
                refreshThinkingPreview(previewLabel);
            });
        }
    }

    /// 过程行耗时文本（"800 ms" / "1.2 s"）
    QString formatStepDuration(qint64 durationMs)
    {
        if (durationMs < 1000)
        {
            return QObject::tr("%1 ms").arg(durationMs);
        }
        return QObject::tr("%1 s").arg(QString::number(durationMs / 1000.0, 'f', 1));
    }

    /// 过程行摘要拍平为单行（换行转空格，超长截断加省略号）
    QString flattenSummary(QString text, int maxChars = 80)
    {
        text.replace(QLatin1Char('\r'), QLatin1Char(' '));
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (text.size() > maxChars)
        {
            text = text.left(maxChars) + QStringLiteral("…");
        }
        return text;
    }

    /// 工具名的中文显示名（未收录的工具显示原名，如技能工具）
    QString friendlyToolName(const QString &name)
    {
        static const QHash<QString, QString> kNames = {
            {QStringLiteral("web_search"), QObject::tr("搜索")},
            {QStringLiteral("fetch_webpage"), QObject::tr("网页阅读")},
            {QStringLiteral("list_instances"), QObject::tr("列出实例")},
            {QStringLiteral("check_loader_compatibility"), QObject::tr("检查加载器兼容性")},
            {QStringLiteral("get_game_versions"), QObject::tr("获取游戏版本")},
            {QStringLiteral("analyze_crash_log"), QObject::tr("分析崩溃日志")},
            {QStringLiteral("list_mods"), QObject::tr("扫描模组")},
            {QStringLiteral("detect_java"), QObject::tr("检测 Java")},
            {QStringLiteral("get_settings"), QObject::tr("查询配置")},
            {QStringLiteral("read_litematic"), QObject::tr("读取投影")},
            {QStringLiteral("search_resources"), QObject::tr("搜索资源")},
            {QStringLiteral("get_resource_detail"), QObject::tr("资源详情")},
            {QStringLiteral("list_resource_files"), QObject::tr("资源文件列表")},
            {QStringLiteral("download_resource"), QObject::tr("下载资源")},
            {QStringLiteral("update_task_list"), QObject::tr("更新任务清单")},
            {QStringLiteral("launch_game"), QObject::tr("启动游戏")},
            {QStringLiteral("download_instance"), QObject::tr("下载实例")},
            {QStringLiteral("modify_instance"), QObject::tr("修改实例")},
            {QStringLiteral("ask_user"), QObject::tr("向用户提问")},
            {QStringLiteral("write_file"), QObject::tr("写入文件")},
            {QStringLiteral("edit_file"), QObject::tr("编辑文件")},
            {QStringLiteral("create_file"), QObject::tr("创建文件")},
        };
        return kNames.value(name, name);
    }

    /// 是否为文件编辑类工具（行首显示文件名 + 增删行数）
    bool isFileEditToolName(const QString &name)
    {
        static const QStringList kFileEditTools = {
            QStringLiteral("write_file"), QStringLiteral("edit_file"),
            QStringLiteral("create_file"), QStringLiteral("save_file"),
            QStringLiteral("replace_in_file"), QStringLiteral("apply_patch"),
            QStringLiteral("str_replace_editor"),
        };
        return kFileEditTools.contains(name);
    }

    /// 从工具参数中提取文件路径（文件编辑类工具）
    QString extractEditedFilePath(const QJsonObject &args)
    {
        for (const char *key : {"path", "file_path", "filename", "file_name", "target_file", "file"})
        {
            const QString v = args.value(QLatin1String(key)).toString();
            if (!v.isEmpty())
            {
                return v;
            }
        }
        return QString();
    }

    /**
     * @brief 估算文件编辑的行级增删统计（+新增行数 / -删除行数）
     *
     * 支持三种参数形态：patch/diff 文本、old_string+new_string 替换对、
     * 整文件 content（全部记为新增）。替换对先去除公共前后缀行再计数。
     */
    QPair<int, int> computeFileDiffStat(const QJsonObject &args)
    {
        // 1) patch / diff 文本：按行首 +/- 统计（忽略 +++/--- 头与 @@ 段落）
        const QString diff = args.value(QLatin1String("diff")).toString();
        if (!diff.isEmpty())
        {
            int add = 0, del = 0;
            const QStringList lines = diff.split(QLatin1Char('\n'));
            for (const QString &line : lines)
            {
                if (line.startsWith(QLatin1String("+++")) || line.startsWith(QLatin1String("---")))
                {
                    continue;
                }
                if (line.startsWith(QLatin1Char('+')))
                {
                    ++add;
                }
                else if (line.startsWith(QLatin1Char('-')))
                {
                    ++del;
                }
            }
            return qMakePair(add, del);
        }

        auto splitLines = [](const QString &s) {
            return s.isEmpty() ? QStringList() : s.split(QLatin1Char('\n'));
        };

        // 2) old_string + new_string 替换对：去除公共前后缀行后计数
        const QString oldStr = args.value(QLatin1String("old_string")).toString();
        const QString newStr = args.value(QLatin1String("new_string")).toString();
        if (!oldStr.isEmpty() || !newStr.isEmpty())
        {
            QStringList oldLines = splitLines(oldStr);
            QStringList newLines = splitLines(newStr);
            while (!oldLines.isEmpty() && !newLines.isEmpty()
                   && oldLines.first() == newLines.first())
            {
                oldLines.removeFirst();
                newLines.removeFirst();
            }
            while (!oldLines.isEmpty() && !newLines.isEmpty()
                   && oldLines.last() == newLines.last())
            {
                oldLines.removeLast();
                newLines.removeLast();
            }
            return qMakePair(newLines.size(), oldLines.size());
        }

        // 3) 整文件写入：全部记为新增
        const QString content = args.value(QLatin1String("content")).toString();
        if (!content.isEmpty())
        {
            return qMakePair(splitLines(content).size(), 0);
        }
        return qMakePair(-1, -1); // 无法统计
    }

    /// 运行中过程行的参数摘要：取第一个有意义的字符串参数值
    QString toolArgsDigest(const QJsonObject &args)
    {
        for (const char *key : {"query", "url", "question", "instance_path", "file_path",
                                "path", "project_id", "version_id", "new_version", "loader"})
        {
            const QString v = args.value(QLatin1String(key)).toString();
            if (!v.isEmpty())
            {
                return v;
            }
        }
        return QString();
    }
}

AiChatPage::AiChatPage(QWidget *parent)
    : QWidget(parent)
    , m_currentContent("")
    , m_currentReasoning("")
    , m_currentAiBubble(nullptr)
    , m_currentThinkingBubble(nullptr)
    , m_currentThinkingContent(nullptr)
    , m_currentContentLabel(nullptr)
    , m_currentThinkingLabel(nullptr)
    , m_typingIndicator(nullptr)
    , m_currentSearchStepCard(nullptr)
    , m_isStreaming(false)
    , m_streamAborting(false)
    , m_streamThrottle(nullptr)
    , m_streamDirty(false)
    , m_streamThinkingDirty(false)
    , m_currentContentEdit(nullptr)
    , m_currentThinkingEdit(nullptr)
    , m_streamFlushedLen(0)
    , m_streamThinkingFlushedLen(0)
    , m_saveDebounceTimer(nullptr)
    , m_aiService(nullptr)
    , m_currentConvIndex(-1)
    , m_pendingTitleConvIndex(-1)
    , m_leftPanel(nullptr)
    , m_bubbleHost(nullptr)
    , m_bubbleHostLayout(nullptr)
    , m_toggleConvBtn(nullptr)
    , m_convListExpanded(false)
    , m_modeChatBtn(nullptr)
    , m_modeWorkBtn(nullptr)
    , m_modeIndicator(nullptr)
    , m_convScrollArea(nullptr)
    , m_convContainer(nullptr)
    , m_convLayout(nullptr)
    , m_newConvBtn(nullptr)
    , m_selectionHighlight(nullptr)
    , m_contextMenuIdx(-1)
    , m_topBar(nullptr)
    , m_statusDot(nullptr)
    , m_convTitleLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_chatStatus(ChatStatus::Idle)
    , m_splitter(nullptr)
    , m_messageArea(nullptr)
    , m_messageContainer(nullptr)
    , m_messageLayout(nullptr)
    , m_inputPanel(nullptr)
    , m_inputEdit(nullptr)
    , m_inputBox(nullptr)
    , m_sendBtn(nullptr)
    , m_toolsArea(nullptr)
    , m_toolsAreaLayout(nullptr)
    , m_modelBtn(nullptr)
    , m_workspaceBtn(nullptr)
    , m_thinkingEffortCombo(nullptr)
    , m_thinkingEffort(2)
    , m_permissionCombo(nullptr)
    , m_permissionMode(0)
    , m_prevPermissionMode(0)
    , m_skillBtn(nullptr)
    , m_systemPromptBtn(nullptr)
    , m_promptOptimizeBtn(nullptr)
    , m_promptOptimizeBusy(false)
    , m_referenceBtn(nullptr)
    , m_referenceChipsWidget(nullptr)
    , m_referenceChipsLayout(nullptr)
    , m_modelDialog(nullptr)
    , m_welcomeWidget(nullptr)
    , m_welcomeTitleLabel(nullptr)
    , m_welcomeDescLabel(nullptr)
    , m_welcomeQuickBtns(nullptr)
    , m_welcomeQuickLayout(nullptr)
    , m_currentMode(0)
    , m_compactMode(false)
{
    initUI();
    initDefaultModels();
    loadModelSettings();
    // 启动时异步检测 Ollama 已下载模型，合并到 m_models（服务未运行则静默跳过）
    refreshLocalModels();

    // 流式渲染节流：50ms 合并多次 chunk 更新，避免 Markdown 频繁重解析导致卡顿
    m_streamThrottle = new QTimer(this);
    m_streamThrottle->setSingleShot(true);
    connect(m_streamThrottle, &QTimer::timeout, this, [this]() {
        // 回合已被中止/结束时气泡可能已销毁，跳过本轮刷新
        if (!m_isStreaming)
        {
            return;
        }
        bool contentChanged = m_streamDirty;
        bool thinkingChanged = m_streamThinkingDirty;
        if (!contentChanged && !thinkingChanged) return;

        if (contentChanged)
        {
            m_streamDirty = false;
            if (m_currentContentEdit)
            {
                appendStreamText(m_currentContentEdit, m_currentContent, m_streamFlushedLen);
            }
            else if (m_currentContentLabel)
            {
                m_currentContentLabel->setText(m_currentContent);
            }
        }
        if (thinkingChanged)
        {
            m_streamThinkingDirty = false;
            if (m_currentThinkingEdit)
            {
                appendStreamText(m_currentThinkingEdit, m_currentReasoning, m_streamThinkingFlushedLen);
            }
            else if (m_currentThinkingLabel)
            {
                m_currentThinkingLabel->setText(m_currentReasoning);
            }
            // 同步刷新收起态的行尾预览（显示最新思考片段）
            if (m_currentThinkingBubble)
            {
                if (auto *pv = m_currentThinkingBubble->findChild<QLabel*>(QStringLiteral("thinkingPreview")))
                {
                    setThinkingPreview(pv, m_currentReasoning);
                }
            }
        }
        // 自动滚动到底部
        QScrollBar *sb = m_messageArea->verticalScrollBar();
        sb->setValue(sb->maximum());
    });
    loadThinkingEffort();
    loadPermissionMode();
    loadWorkspaceFolders();
    refreshSystemPromptSelector();

    // 对话落盘防抖：多次写入合并为一次，避免主线程频繁同步写全量文件
    m_saveDebounceTimer = new QTimer(this);
    m_saveDebounceTimer->setSingleShot(true);
    m_saveDebounceTimer->setInterval(500);
    connect(m_saveDebounceTimer, &QTimer::timeout, this, &AiChatPage::doSaveConversations);

    // 设置默认当前模型：优先 DeepSeek Chat，否则取第一个
    if (!m_models.isEmpty())
    {
        bool found = false;
        for (const AiModel &model : m_models)
        {
            if (model.id == QStringLiteral("deepseek-chat"))
            {
                m_currentModel = model;
                found = true;
                break;
            }
        }
        if (!found)
        {
            m_currentModel = m_models.first();
        }
        updateModelBtnText();
    }

    initConnections();
    loadConversations();

    // 首次进入 AI 助手页始终进入新对话草稿态（不自动加载历史对话）
    m_currentConvIndex = -1;
    if (m_convTitleLabel)
    {
        m_convTitleLabel->setText(tr("新对话"));
    }
    // 欢迎界面默认可见

    // 入场动画（匹配 SubNavPanel 交错的入场效果）
    QTimer::singleShot(100, this, [this]() {
        animateConvButtons();
    });
}

AiChatPage::~AiChatPage()
{
    doSaveConversations();
}

void AiChatPage::initUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========================================================================
    // 顶部栏 - 展开对话按钮 + 状态圆点 + 当前对话名称（与 HTML 原型一致）
    // ========================================================================
    m_topBar = new QWidget();
    m_topBar->setObjectName("aiChatTopBar");
    auto *topBarLayout = new QHBoxLayout(m_topBar);
    topBarLayout->setContentsMargins(12, 8, 16, 8);
    topBarLayout->setSpacing(10);

    // 左侧：对话选择切换按钮（最左）
    m_toggleConvBtn = new QPushButton(m_topBar);
    m_toggleConvBtn->setObjectName("aiChatToggleConvBtn");
    m_toggleConvBtn->setFixedSize(32, 32);
    m_toggleConvBtn->setCursor(Qt::PointingHandCursor);
    m_toggleConvBtn->setCheckable(true);
    m_toggleConvBtn->setToolTip(tr("对话选择"));
    updateToggleConvIcon();
    topBarLayout->addWidget(m_toggleConvBtn, 0, Qt::AlignVCenter);

    // 状态圆点（空闲灰 / 忙碌主题色 / 错误红）
    m_statusDot = new QLabel(m_topBar);
    m_statusDot->setObjectName("aiChatStatusDot");
    m_statusDot->setFixedSize(8, 8);
    m_statusDot->setVisible(false);
    topBarLayout->addWidget(m_statusDot, 0, Qt::AlignVCenter);

    // 中间：当前对话名称
    m_convTitleLabel = new QLabel(m_topBar);
    m_convTitleLabel->setObjectName("aiChatConvTitle");
    m_convTitleLabel->setText(tr("新对话"));
    topBarLayout->addWidget(m_convTitleLabel, 1, Qt::AlignVCenter);

    // 右侧：当前对话状态指示器
    m_statusLabel = new QLabel(m_topBar);
    m_statusLabel->setObjectName("aiChatStatusLabel");
    m_statusLabel->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: #9E9E9E; padding: 2px 10px; "
        "border: none; background: transparent;"));
    m_statusLabel->setVisible(false); // 初始空闲态隐藏
    topBarLayout->addWidget(m_statusLabel, 0, Qt::AlignVCenter);

    // 右侧预留弹性空间
    topBarLayout->addStretch();

    mainLayout->addWidget(m_topBar);

    // ========================================================================
    // 主体区域 - 左侧对话列表 + 右侧消息/输入
    // ========================================================================
    auto *bodyWidget = new QWidget();
    auto *bodyLayout = new QHBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    m_bubbleHost = bodyWidget;
    m_bubbleHostLayout = bodyLayout;

    // 左侧面板 - 对话列表（悬浮气泡，不占布局；默认收起隐藏）
    m_leftPanel = new QFrame(bodyWidget);
    m_leftPanel->setObjectName("aiChatLeftPanel");
    m_leftPanel->setAttribute(Qt::WA_StyledBackground);
    m_leftPanel->setFixedWidth(kBubbleWidth);
    m_leftPanel->setVisible(false);
    // 悬浮气泡外阴影
    auto *convShadow = new QGraphicsDropShadowEffect(m_leftPanel);
    convShadow->setBlurRadius(20);
    convShadow->setOffset(0, 4);
    convShadow->setColor(QColor(0, 0, 0, 50));
    m_leftPanel->setGraphicsEffect(convShadow);

    auto *leftLayout = new QVBoxLayout(m_leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    // 面板头部行：模式切换 + 收起按钮（与 SubNavPanel 头部布局一致）
    auto *panelHeader = new QWidget();
    panelHeader->setObjectName("subNavHeader");
    auto *panelHeaderLayout = new QHBoxLayout(panelHeader);
    panelHeaderLayout->setContentsMargins(12, 10, 8, 8);
    panelHeaderLayout->setSpacing(8);

    // 模式切换容器 - 聊天/工作
    auto *modeBar = new QWidget();
    modeBar->setObjectName("aiChatModeBar");
    auto *modeLayout = new QHBoxLayout(modeBar);
    modeLayout->setContentsMargins(3, 3, 3, 3);
    modeLayout->setSpacing(0);

    m_modeChatBtn = new QPushButton(tr("聊天"), modeBar);
    m_modeChatBtn->setObjectName("aiChatModeBtn");
    m_modeChatBtn->setCheckable(true);
    m_modeChatBtn->setChecked(true);
    m_modeChatBtn->setCursor(Qt::PointingHandCursor);
    modeLayout->addWidget(m_modeChatBtn);

    m_modeWorkBtn = new QPushButton(tr("工作"), modeBar);
    m_modeWorkBtn->setObjectName("aiChatModeBtn");
    m_modeWorkBtn->setCheckable(true);
    m_modeWorkBtn->setCursor(Qt::PointingHandCursor);
    modeLayout->addWidget(m_modeWorkBtn);

    // 滑动指示器（置于按钮之下，作为选中态背景）
    m_modeIndicator = new QWidget(modeBar);
    m_modeIndicator->setObjectName("aiChatModeIndicator");
    m_modeIndicator->setVisible(false);
    m_modeIndicator->lower();

    // 收起按钮（与 SubNavPanel 的 subNavCollapseBtn 一致）
    auto *collapseBtn = new QPushButton(panelHeader);
    collapseBtn->setObjectName("subNavCollapseBtn");
    collapseBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/chevron_left.svg", QColor("#888888"), 16));
    collapseBtn->setIconSize(QSize(16, 16));
    collapseBtn->setFixedSize(24, 24);
    collapseBtn->setCursor(Qt::PointingHandCursor);
    collapseBtn->setToolTip(tr("收起对话栏"));
    connect(collapseBtn, &QPushButton::clicked, this, [this]() {
        if (m_toggleConvBtn->isChecked())
        {
            m_toggleConvBtn->setChecked(false);
            onToggleConvList();
        }
    });

    panelHeaderLayout->addWidget(modeBar, 1);
    panelHeaderLayout->addWidget(collapseBtn);
    leftLayout->addWidget(panelHeader);

    // 滚动区域
    m_convScrollArea = new QScrollArea();
    m_convScrollArea->setObjectName("aiChatConvScrollArea");
    m_convScrollArea->setWidgetResizable(true);
    m_convScrollArea->setFrameShape(QFrame::NoFrame);
    m_convScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_convScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 默认收起
    m_convScrollArea->setVisible(false);

    // 滚动内容容器
    m_convContainer = new QWidget(m_convScrollArea);
    m_convLayout = new QVBoxLayout(m_convContainer);
    m_convLayout->setContentsMargins(0, 8, 0, 4);
    m_convLayout->setSpacing(2);

    // 对话列表分组标题（与 SubNavPanel 分组标题一致）
    auto *groupTitle = new QLabel(tr("对话"), m_convContainer);
    groupTitle->setObjectName("subNavGroupTitle");
    m_convLayout->addWidget(groupTitle);

    // 选中态滑动背景（置于按钮之下，与 SubNavPanel 高亮滑块一致）
    m_selectionHighlight = new QWidget(m_convContainer);
    m_selectionHighlight->setObjectName("subNavHighlight");
    m_selectionHighlight->setVisible(false);
    m_selectionHighlight->lower();

    // 对话按钮通过 addConversation 动态插入此处

    // 新建对话按钮（位于列表末尾）
    m_newConvBtn = new QPushButton(tr("+ 新建对话"), m_convContainer);
    m_newConvBtn->setObjectName("aiChatNewConvBtn");
    m_newConvBtn->setFixedHeight(40);
    m_newConvBtn->setCursor(Qt::PointingHandCursor);
    m_convLayout->addWidget(m_newConvBtn);

    // 底部弹性空间
    m_convLayout->addStretch();

    m_convScrollArea->setWidget(m_convContainer);
    leftLayout->addWidget(m_convScrollArea);

    // 气泡不加入 bodyLayout，仅在其展开时于宿主左侧留出空隙

    // 右侧面板
    auto *rightPanel = new QWidget();
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // 消息滚动区域
    m_messageArea = new QScrollArea();
    m_messageArea->setObjectName("aiChatMessageArea");
    m_messageArea->setWidgetResizable(true);
    m_messageArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_messageContainer = new QWidget();
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(16, 16, 16, 16);
    m_messageLayout->setSpacing(12);

    // 欢迎提示（标题 + 简介）
    m_welcomeWidget = new QWidget();
    m_welcomeWidget->setObjectName("aiChatWelcomeWidget");
    auto *welcomeLayout = new QVBoxLayout(m_welcomeWidget);
    welcomeLayout->setContentsMargins(40, 80, 40, 40);
    welcomeLayout->setSpacing(16);
    welcomeLayout->setAlignment(Qt::AlignCenter);

    m_welcomeTitleLabel = new OutlinedLabel();
    m_welcomeTitleLabel->setObjectName("aiChatWelcomeTitleLabel");
    m_welcomeTitleLabel->setAlignment(Qt::AlignCenter);
    m_welcomeTitleLabel->setWordWrap(true);
    // 水平收缩到文字宽度，让边框紧贴文字（居中显示）
    m_welcomeTitleLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    welcomeLayout->addWidget(m_welcomeTitleLabel);

    m_welcomeDescLabel = new QLabel();
    m_welcomeDescLabel->setObjectName("aiChatWelcomeDescLabel");
    m_welcomeDescLabel->setAlignment(Qt::AlignCenter);
    m_welcomeDescLabel->setWordWrap(true);
    // 水平收缩到文字宽度，让边框紧贴文字（居中显示）
    m_welcomeDescLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    welcomeLayout->addWidget(m_welcomeDescLabel);

    // 工作模式快捷按钮网格（搭配整合包、翻译模组等，点击注入提示词）
    m_welcomeQuickBtns = new QWidget();
    m_welcomeQuickBtns->setObjectName("aiChatWelcomeQuickBtns");
    m_welcomeQuickLayout = new QGridLayout(m_welcomeQuickBtns);
    m_welcomeQuickLayout->setContentsMargins(0, 12, 0, 0);
    m_welcomeQuickLayout->setSpacing(10);
    m_welcomeQuickLayout->setAlignment(Qt::AlignCenter);

    // 根据 m_compactMode 决定列数并填充快捷按钮
    buildWelcomeQuickButtons();

    welcomeLayout->addWidget(m_welcomeQuickBtns);

    m_currentMode = 0;
    updateWelcomeContent();

    // 初始显示欢迎界面（垂直居中）
    showWelcome(true);

    m_messageArea->setWidget(m_messageContainer);

    rightLayout->addWidget(m_messageArea, 1);

    // 输入区域
    m_inputPanel = new QWidget();
    m_inputPanel->setObjectName("aiChatInputPanel");

    auto *inputLayout = new QVBoxLayout(m_inputPanel);
    inputLayout->setContentsMargins(16, 10, 16, 14);
    inputLayout->setSpacing(8);

    // 引用资源标签条（输入框上方，初始隐藏）
    m_referenceChipsWidget = new QWidget();
    m_referenceChipsWidget->setObjectName("aiChatRefChipsWidget");
    m_referenceChipsLayout = new QHBoxLayout(m_referenceChipsWidget);
    m_referenceChipsLayout->setContentsMargins(0, 0, 0, 0);
    m_referenceChipsLayout->setSpacing(6);
    m_referenceChipsLayout->addStretch();
    m_referenceChipsWidget->setVisible(false);
    inputLayout->addWidget(m_referenceChipsWidget);

    // 输入框容器（圆角卡片：文本域 + 工具 + 发送按钮）
    auto *inputBox = new QFrame();
    inputBox->setObjectName("aiChatInputBox");
    m_inputBox = inputBox;
    auto *inputBoxLayout = new QVBoxLayout(inputBox);
    inputBoxLayout->setContentsMargins(12, 8, 8, 8);
    inputBoxLayout->setSpacing(4);

    m_inputEdit = new QTextEdit();
    m_inputEdit->setObjectName("aiChatInputEdit");
    m_inputEdit->setPlaceholderText(tr("输入消息，Enter 发送，Shift+Enter 换行"));
    m_inputEdit->setMaximumHeight(100);
    m_inputEdit->setAcceptRichText(false);
    m_inputEdit->setFrameShape(QFrame::NoFrame);
    m_inputEdit->installEventFilter(this);
    inputBoxLayout->addWidget(m_inputEdit);

    // ====================================================================
    // 工具行容器：左侧「模型 / 工作区 / 思考强度 / 权限」选择框，右侧「引用 /
    // 提示词优化 / 系统提示词 / 技能 / 发送」图标。布局随紧凑模式重建。
    // ====================================================================
    m_toolsArea = new QWidget(inputBox);
    m_toolsAreaLayout = new QVBoxLayout(m_toolsArea);
    m_toolsAreaLayout->setContentsMargins(0, 0, 0, 0);
    m_toolsAreaLayout->setSpacing(2);

    // --- 左侧：模型选择框（固定宽度，点击弹出模型选择窗口） ---
    m_modelBtn = new QPushButton(m_toolsArea);
    m_modelBtn->setObjectName("aiChatModelBtn");
    m_modelBtn->setCursor(Qt::PointingHandCursor);
    m_modelBtn->setFlat(true);
    m_modelBtn->setFixedSize(150, 28);
    m_modelBtn->setToolTip(tr("选择本次对话使用的模型"));
    m_modelBtn->setText(tr("选择模型"));

    // --- 左侧：工作区选择框（可不选 / 已绑定文件夹 / 绑定新文件夹） ---
    m_workspaceBtn = new QPushButton(m_toolsArea);
    m_workspaceBtn->setObjectName("aiChatWorkspaceBtn");
    m_workspaceBtn->setCursor(Qt::PointingHandCursor);
    m_workspaceBtn->setFlat(true);
    m_workspaceBtn->setFixedHeight(28);
    m_workspaceBtn->setToolTip(tr("限制 AI 可访问的目录范围："
                                  "可不选，也可选择已绑定的文件夹，或绑定新文件夹。"));
    m_workspaceBtn->setText(tr("工作区: 不选"));

    // --- 左侧：思考强度选择框（关闭/低/中/高，对支持思考的模型生效） ---
    m_thinkingEffortCombo = new QComboBox(m_toolsArea);
    m_thinkingEffortCombo->setObjectName("aiChatThinkingEffortCombo");
    m_thinkingEffortCombo->setCursor(Qt::PointingHandCursor);
    m_thinkingEffortCombo->setFixedHeight(28);
    m_thinkingEffortCombo->setToolTip(tr("思考强度：关闭后不输出思考过程，"
                                         "低/中/高控制推理深度（对支持思考的模型生效）。"));
    QIcon brainIcon = IconHelper::loadColoredIcon(":/Images/Icons/brain.svg", QColor("#64748b"), 14);
    m_thinkingEffortCombo->addItem(brainIcon, tr("思考: 关闭"), 0);
    m_thinkingEffortCombo->addItem(brainIcon, tr("思考: 低"), 1);
    m_thinkingEffortCombo->addItem(brainIcon, tr("思考: 中"), 2);
    m_thinkingEffortCombo->addItem(brainIcon, tr("思考: 高"), 3);
    m_thinkingEffortCombo->setCurrentIndex(2); // 默认"中"

    // --- 左侧：操作权限选择框（全部确认 / 关键确认 / 完全访问） ---
    m_permissionCombo = new QComboBox(m_toolsArea);
    m_permissionCombo->setObjectName("aiChatPermissionCombo");
    m_permissionCombo->setCursor(Qt::PointingHandCursor);
    m_permissionCombo->setFixedHeight(28);
    m_permissionCombo->setToolTip(tr("操作权限：全部确认——AI 执行任何写操作前都需你确认；\n"
                                     "关键确认——仅下载、删除、实例修改等关键操作需确认；\n"
                                     "完全访问——AI 直接执行所有操作，不再询问。"));
    m_permissionCombo->addItem(tr("权限: 全部确认"), 0);
    m_permissionCombo->addItem(tr("权限: 关键确认"), 1);
    m_permissionCombo->addItem(tr("权限: 完全访问"), 2);
    m_permissionCombo->setCurrentIndex(0); // 默认"全部确认"

    // --- 右侧：引用图标 ---
    m_referenceBtn = new QPushButton(m_toolsArea);
    m_referenceBtn->setObjectName("aiChatToolIconBtn");
    m_referenceBtn->setCursor(Qt::PointingHandCursor);
    m_referenceBtn->setFlat(true);
    m_referenceBtn->setFixedSize(30, 30);
    m_referenceBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/link.svg", QColor("#64748b"), 16));
    m_referenceBtn->setIconSize(QSize(16, 16));
    m_referenceBtn->setToolTip(tr("引用实例或本地磁盘中的模组、资源包、光影包、投影、存档、数据包、"
                                  "网页或任意文件，作为本次消息的上下文。"));

    // --- 右侧：提示词优化图标 ---
    m_promptOptimizeBtn = new QPushButton(m_toolsArea);
    m_promptOptimizeBtn->setObjectName("aiChatToolIconBtn");
    m_promptOptimizeBtn->setCursor(Qt::PointingHandCursor);
    m_promptOptimizeBtn->setFlat(true);
    m_promptOptimizeBtn->setFixedSize(30, 30);
    m_promptOptimizeBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/sparkles.svg", QColor("#64748b"), 16));
    m_promptOptimizeBtn->setIconSize(QSize(16, 16));
    m_promptOptimizeBtn->setEnabled(false);
    m_promptOptimizeBtn->setToolTip(tr("用 AI 优化输入框中的提示词草稿"));

    // --- 右侧：系统提示词图标 ---
    m_systemPromptBtn = new QPushButton(m_toolsArea);
    m_systemPromptBtn->setObjectName("aiChatToolIconBtn");
    m_systemPromptBtn->setCursor(Qt::PointingHandCursor);
    m_systemPromptBtn->setFlat(true);
    m_systemPromptBtn->setFixedSize(30, 30);
    m_systemPromptBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/clipboard.svg", QColor("#64748b"), 16));
    m_systemPromptBtn->setIconSize(QSize(16, 16));
    m_systemPromptBtn->setToolTip(tr("从提示词库中选择系统提示词（可多选）"));

    // --- 右侧：技能调用图标 ---
    m_skillBtn = new QPushButton(m_toolsArea);
    m_skillBtn->setObjectName("aiChatToolIconBtn");
    m_skillBtn->setCursor(Qt::PointingHandCursor);
    m_skillBtn->setFlat(true);
    m_skillBtn->setFixedSize(30, 30);
    m_skillBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/bolt.svg", QColor("#64748b"), 16));
    m_skillBtn->setIconSize(QSize(16, 16));
    m_skillBtn->setToolTip(tr("管理 AI 技能：导入、启用/禁用、删除。技能可扩展 AI 助手的工具能力。"));

    // --- 右侧：发送/中止图标 ---
    m_sendBtn = new QPushButton(m_toolsArea);
    m_sendBtn->setObjectName("aiChatSendBtn");
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    m_sendBtn->setFixedSize(32, 32);
    m_sendBtn->setIconSize(QSize(16, 16));
    m_sendBtn->setEnabled(false);
    m_sendBtn->setToolTip(tr("发送"));
    updateSendButtonIcon(false);

    // 按当前紧凑模式组装工具行（普通单行 / 紧凑两行）
    rebuildToolsRows();

    inputBoxLayout->addWidget(m_toolsArea);
    inputLayout->addWidget(inputBox);

    rightLayout->addWidget(m_inputPanel);

    bodyLayout->addWidget(rightPanel, 1);

    mainLayout->addWidget(bodyWidget, 1);
}

void AiChatPage::rebuildToolsRows()
{
    if (!m_toolsAreaLayout)
    {
        return;
    }

    // 清空旧布局（控件全部保留，仅重组布局父子关系）
    while (QLayoutItem *item = m_toolsAreaLayout->takeAt(0))
    {
        delete item;
    }

    auto *leftRow = new QHBoxLayout();
    leftRow->setSpacing(6);
    leftRow->addWidget(m_modelBtn);
    leftRow->addWidget(m_workspaceBtn);
    leftRow->addWidget(m_thinkingEffortCombo);
    leftRow->addWidget(m_permissionCombo);
    leftRow->addStretch();

    auto *rightRow = new QHBoxLayout();
    rightRow->setSpacing(2);
    rightRow->addWidget(m_referenceBtn);
    rightRow->addWidget(m_promptOptimizeBtn);
    rightRow->addWidget(m_systemPromptBtn);
    rightRow->addWidget(m_skillBtn);
    rightRow->addWidget(m_sendBtn, 0, Qt::AlignVCenter);

    if (m_compactMode)
    {
        // 紧凑模式（实例助手 450px 窄窗口）：选择框与图标拆两行
        m_toolsAreaLayout->addLayout(leftRow);
        m_toolsAreaLayout->addLayout(rightRow);
    }
    else
    {
        // 普通模式：左四选择框 + 弹性 + 右五图标，同一行
        auto *singleRow = new QHBoxLayout();
        singleRow->setSpacing(6);
        singleRow->addWidget(m_modelBtn);
        singleRow->addWidget(m_workspaceBtn);
        singleRow->addWidget(m_thinkingEffortCombo);
        singleRow->addWidget(m_permissionCombo);
        singleRow->addStretch();
        singleRow->addWidget(m_referenceBtn);
        singleRow->addWidget(m_promptOptimizeBtn);
        singleRow->addWidget(m_systemPromptBtn);
        singleRow->addWidget(m_skillBtn);
        singleRow->addWidget(m_sendBtn, 0, Qt::AlignVCenter);
        m_toolsAreaLayout->addLayout(singleRow);
        delete leftRow;
        delete rightRow;
    }

    // 固定宽度：普通模式较宽，紧凑模式收窄以适配 450px 窗口
    m_modelBtn->setFixedWidth(m_compactMode ? 96 : 150);
    m_workspaceBtn->setFixedWidth(m_compactMode ? 92 : 132);
    m_thinkingEffortCombo->setFixedWidth(m_compactMode ? 86 : 108);
    m_permissionCombo->setFixedWidth(m_compactMode ? 92 : 122);
    updateModelBtnText();
    updateWorkspaceBtnText();
}

void AiChatPage::initDefaultModels()
{
    // 预设模型清单与服务商官网映射从内置 JSON 资源加载：
    //   :/resources/ai_models.json
    // 新增模型只需在 JSON 中追加条目，无需改动本函数。
    QFile file(QStringLiteral(":/resources/ai_models.json"));
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

    const QJsonObject root = doc.object();

    // 1. 装载模型条目
    const QJsonArray models = root.value(QStringLiteral("models")).toArray();
    for (const QJsonValue &v : models)
    {
        const QJsonObject m = v.toObject();
        AiModel model;
        model.id = m.value(QStringLiteral("id")).toString();
        model.displayName = m.value(QStringLiteral("displayName")).toString();
        model.apiUrl = m.value(QStringLiteral("apiUrl")).toString();
        model.apiKey = "";
        model.isCustom = false;
        model.supportsThinking = m.value(QStringLiteral("supportsThinking")).toBool(false);
        // websiteUrl 留空，下一步通过前缀匹配填充
        m_models.append(model);
    }

    // 2. 通过 displayName 前缀匹配填充服务商官网 URL
    const QJsonArray providerUrls = root.value(QStringLiteral("providerUrls")).toArray();
    for (AiModel &model : m_models)
    {
        if (!model.websiteUrl.isEmpty())
        {
            continue; // 已有显式 URL，跳过
        }
        for (const QJsonValue &pv : providerUrls)
        {
            const QJsonObject po = pv.toObject();
            const QString prefix = po.value(QStringLiteral("prefix")).toString();
            if (!prefix.isEmpty() && model.displayName.startsWith(prefix))
            {
                model.websiteUrl = po.value(QStringLiteral("url")).toString();
                break;
            }
        }
    }

    // 3. 默认选中首项（构造函数会在存在 deepseek-chat 时改选该项）
    if (!m_models.isEmpty())
    {
        m_currentModel = m_models.at(0);
    }
}

void AiChatPage::initConnections()
{
    m_aiService = new AiService(this);
    m_aiService->setPermissionMode(m_permissionMode);

    connect(m_newConvBtn, &QPushButton::clicked, this, &AiChatPage::onNewConversation);
    // 发送按钮：流式期间作为"停止"按钮，否则作为"发送"按钮
    connect(m_sendBtn, &QPushButton::clicked, this, [this]() {
        if (m_isStreaming)
        {
            onStopStreaming();
        }
        else
        {
            onSendMessage();
        }
    });
    connect(m_modelBtn, &QPushButton::clicked, this, &AiChatPage::onModelSelectClicked);
    connect(m_referenceBtn, &QPushButton::clicked, this, &AiChatPage::onReferenceClicked);

    // 思考强度切换：同步成员变量并持久化
    connect(m_thinkingEffortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiChatPage::onThinkingEffortChanged);

    // 操作权限切换：完全访问需居中确认，取消则回退
    connect(m_permissionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiChatPage::onPermissionChanged);

    // 技能管理按钮：打开技能管理对话框
    connect(m_skillBtn, &QPushButton::clicked, this, &AiChatPage::onSkillManage);

    // 工作区选择框：弹出选择菜单（不选 / 已绑定文件夹 / 绑定新文件夹）
    connect(m_workspaceBtn, &QPushButton::clicked, this, &AiChatPage::showWorkspaceMenu);

    // 提示词优化按钮：将输入框草稿交由 AI 改写
    connect(m_promptOptimizeBtn, &QPushButton::clicked,
            this, &AiChatPage::onPromptOptimizeClicked);
    connect(m_aiService, &AiService::promptOptimized,
            this, &AiChatPage::onPromptOptimized);


    // 系统提示词库多选按钮：弹出勾选列表
    connect(m_systemPromptBtn, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("选择系统提示词"));
        dlg.setMinimumWidth(350);
        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        dlgLayout->setContentsMargins(12, 12, 12, 12);
        dlgLayout->setSpacing(8);

        QLabel *hint = new QLabel(tr("可多选，选中的提示词将合并为系统提示词。"), &dlg);
        hint->setObjectName("settingDesc");
        hint->setWordWrap(true);
        dlgLayout->addWidget(hint);

        QListWidget *listWidget = new QListWidget(&dlg);
        listWidget->setObjectName("systemPromptSelectList");

        // 从库中加载所有提示词
        QSettings s;
        QString json = s.value("ai/systemPromptLibrary", "").toString();
        struct PromptEntry { QString id; QString name; };
        QList<PromptEntry> entries;
        if (!json.isEmpty()) {
            QJsonParseError pe;
            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
            if (pe.error == QJsonParseError::NoError && doc.isArray()) {
                for (const QJsonValue &v : doc.array()) {
                    QJsonObject o = v.toObject();
                    PromptEntry e;
                    e.id = o["id"].toString();
                    e.name = o["name"].toString();
                    if (!e.id.isEmpty() && !e.name.isEmpty())
                        entries.append(e);
                }
            }
        }

        for (const PromptEntry &e : entries) {
            QListWidgetItem *item = new QListWidgetItem(e.name);
            item->setData(Qt::UserRole, e.id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(m_selectedPromptIds.contains(e.id)
                                    ? Qt::Checked : Qt::Unchecked);
            listWidget->addItem(item);
        }

        dlgLayout->addWidget(listWidget);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        QPushButton *cancelBtn = new QPushButton(tr("取消"), &dlg);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);
        QPushButton *okBtn = new QPushButton(tr("确定"), &dlg);
        okBtn->setCursor(Qt::PointingHandCursor);
        connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        btnLayout->addWidget(okBtn);
        dlgLayout->addLayout(btnLayout);

        if (dlg.exec() == QDialog::Accepted) {
            m_selectedPromptIds.clear();
            for (int i = 0; i < listWidget->count(); ++i) {
                QListWidgetItem *item = listWidget->item(i);
                if (item->checkState() == Qt::Checked)
                    m_selectedPromptIds.append(item->data(Qt::UserRole).toString());
            }
            // 图标按钮：选中态用 active 属性高亮，提示词清单放进 tooltip
            if (m_selectedPromptIds.isEmpty()) {
                m_systemPromptBtn->setToolTip(tr("从提示词库中选择系统提示词（可多选）"));
            } else if (m_selectedPromptIds.size() <= 3) {
                QStringList names;
                for (const QString &id : m_selectedPromptIds) {
                    for (const PromptEntry &e : entries) {
                        if (e.id == id) { names.append(e.name); break; }
                    }
                }
                m_systemPromptBtn->setToolTip(tr("系统提示词：%1").arg(names.join(QStringLiteral(", "))));
            } else {
                m_systemPromptBtn->setToolTip(tr("已选中 %1 个系统提示词").arg(m_selectedPromptIds.size()));
            }
            m_systemPromptBtn->setProperty("active", !m_selectedPromptIds.isEmpty());
            m_systemPromptBtn->style()->unpolish(m_systemPromptBtn);
            m_systemPromptBtn->style()->polish(m_systemPromptBtn);
        }
    });

    // 顶部"对话选择"图标按钮：点击切换对话列表的展开/收起状态
    connect(m_toggleConvBtn, &QPushButton::clicked, this, &AiChatPage::onToggleConvList);

    // 模式切换互斥 + 滑动动画
    connect(m_modeChatBtn, &QPushButton::clicked, this, [this](bool checked) {
        if (!checked)
        {
            m_modeChatBtn->setChecked(true);
            return;
        }
        switchToMode(0);
    });
    connect(m_modeWorkBtn, &QPushButton::clicked, this, [this](bool checked) {
        if (!checked)
        {
            m_modeWorkBtn->setChecked(true);
            return;
        }
        switchToMode(1);
    });

    // 主题色变化时刷新切换按钮图标颜色
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, [this]() { updateToggleConvIcon(); });
    connect(ThemeManager::instance(), &ThemeManager::textColorChanged,
            this, [this]() { updateToggleConvIcon(); });

    // Ctrl+Enter 也发送
    connect(m_inputEdit, &QTextEdit::textChanged, this, [this]() {
        const bool hasText = !m_inputEdit->toPlainText().trimmed().isEmpty();
        if (m_isStreaming)
        {
            // 流式期间"停止"按钮始终可点击
            m_sendBtn->setEnabled(true);
        }
        else
        {
            m_sendBtn->setEnabled(hasText);
        }
        // 提示词优化仅在草稿非空且无优化请求在飞行时可用
        if (!m_promptOptimizeBusy)
        {
            m_promptOptimizeBtn->setEnabled(hasText);
        }
    });

    // AI 服务信号
    connect(m_aiService, &AiService::streamContentReceived, this, &AiChatPage::onStreamContent);
    connect(m_aiService, &AiService::streamReasoningReceived, this, &AiChatPage::onStreamReasoning);
    connect(m_aiService, &AiService::streamFinished, this, &AiChatPage::onStreamFinished);
    connect(m_aiService, &AiService::errorOccurred, this, &AiChatPage::onStreamError);

    // 联网搜索过程行（一轮一行样式：搜索中 → 成功 + 耗时 + 结果数）
    connect(m_aiService, &AiService::webSearchStarted, this, [this](const QString &query) {
        // 进入搜索状态
        updateChatStatus(ChatStatus::Searching);
        // 移除打字指示器（搜索阶段替换为「搜索」过程行）
        if (m_typingIndicator)
        {
            m_messageLayout->removeWidget(m_typingIndicator);
            m_typingIndicator->deleteLater();
            m_typingIndicator = nullptr;
        }
        ensureSearchStepCard(query);
        m_searchElapsedTimer.start();

        QScrollBar *sb = m_messageArea->verticalScrollBar();
        sb->setValue(sb->maximum());
    });
    connect(m_aiService, &AiService::webSearchFinished, this, [this](int resultCount) {
        // 搜索结束，恢复思考状态（仍处于流式）
        if (m_isStreaming)
        {
            updateChatStatus(ChatStatus::Thinking);
        }
        // 「搜索」过程行置为完成态（成功 + 耗时 + 结果数摘要），行保留在消息流中
        if (m_currentSearchStepCard)
        {
            applyToolStepFinish(m_currentSearchStepCard, tr("%1 个结果").arg(resultCount),
                                m_searchElapsedTimer.elapsed(), true, false);
            m_currentSearchStepCard = nullptr;
        }
        // 搜索完成后若仍无 AI 气泡，恢复打字指示器等待模型生成
        if (!m_currentAiBubble && !m_typingIndicator)
        {
            m_typingIndicator = createTypingIndicator();
            m_messageLayout->insertWidget(m_messageLayout->count() - 1, m_typingIndicator);
        }
    });

    // 累积搜索结果，实时向汇总区域追加联网卡片
    connect(m_aiService, &AiService::webSearchResultsCollected,
            this, &AiChatPage::onWebSearchResultsCollected);

    // Agent 工具调用进度（工作模式步骤卡片）
    connect(m_aiService, &AiService::agentToolCallStarted,
            this, &AiChatPage::onAgentToolCallStarted);
    connect(m_aiService, &AiService::agentToolCallFinished,
            this, &AiChatPage::onAgentToolCallFinished);

    // Agent 触发资源下载（工作模式 download_resource 工具）
    connect(m_aiService, &AiService::resourceDownloadRequested,
            this, &AiChatPage::onResourceDownloadRequested);

    // 上下文压缩（工作模式自动触发）
    connect(m_aiService, &AiService::contextCompressionStarted,
            this, &AiChatPage::onContextCompressionStarted);
    connect(m_aiService, &AiService::contextCompressionFinished,
            this, &AiChatPage::onContextCompressionFinished);

    // AI 自维护任务清单（工作模式 update_task_list 工具）
    connect(m_aiService, &AiService::taskListUpdated,
            this, &AiChatPage::onTaskListUpdated);

    // Agent 触发实例下载/修改（工作模式 download_instance/modify_instance 工具）
    connect(m_aiService, &AiService::instanceDownloadRequested,
            this, &AiChatPage::onInstanceDownloadRequested);
    connect(m_aiService, &AiService::instanceModifyRequested,
            this, &AiChatPage::onInstanceModifyRequested);

    // Agent 向用户提问（工作模式 ask_user 工具）
    connect(m_aiService, &AiService::userQuestionAsked,
            this, &AiChatPage::onUserQuestionAsked);

    // AI 自动生成对话标题（onStreamFinished 后异步触发）
    connect(m_aiService, &AiService::titleGenerated,
            this, &AiChatPage::onTitleGenerated);

    // 右键菜单 - 在 addConversation 中为每个按钮连接
}

// ============================================================================
// 事件处理
// ============================================================================

bool AiChatPage::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_inputEdit)
    {
        if (event->type() == QEvent::FocusIn)
        {
            // 输入框聚焦：高亮外层输入容器边框
            if (QWidget *box = m_inputEdit->parentWidget())
            {
                box->setProperty("focused", true);
                box->style()->unpolish(box);
                box->style()->polish(box);
            }
        }
        else if (event->type() == QEvent::FocusOut)
        {
            if (QWidget *box = m_inputEdit->parentWidget())
            {
                box->setProperty("focused", false);
                box->style()->unpolish(box);
                box->style()->polish(box);
            }
        }

        if (event->type() == QEvent::KeyPress)
        {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            {
                if (keyEvent->modifiers() & Qt::ShiftModifier)
                {
                    // Shift+Enter: 换行，允许默认行为
                    return QWidget::eventFilter(obj, event);
                }
                else
                {
                    // Enter: 发送消息
                    onSendMessage();
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void AiChatPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateConvBubbleGeometry();
}

void AiChatPage::updateConvBubbleGeometry()
{
    if (!m_leftPanel || !m_bubbleHost || !m_convListExpanded)
    {
        return;
    }
    const int x = kBubbleMargin;
    const int y = kBubbleMargin;
    const int h = qMax(120, m_bubbleHost->height() - 2 * kBubbleMargin);
    m_leftPanel->setGeometry(x, y, kBubbleWidth, h);
    m_leftPanel->raise();
}

// ============================================================================
// 对话管理
// ============================================================================

void AiChatPage::onNewConversation()
{
    // 不可重复创建新对话：当前已处于新对话页面（无选中对话）则直接返回
    if (m_currentConvIndex < 0)
    {
        return;
    }

    if (m_isStreaming)
    {
        // 标记主动中断，忽略后续 streamFinished/error 信号
        m_streamAborting = true;
        m_aiService->stopStreaming();
        m_isStreaming = false;
    }

    // 进入新对话草稿态：清空消息区并显示欢迎界面
    clearMessageArea();
    showWelcome(true);

    // 左侧对话列表不选中任何对话
    m_currentConvIndex = -1;
    for (QPushButton *btn : m_convButtons)
    {
        btn->setChecked(false);
    }
    if (m_selectionHighlight)
    {
        m_selectionHighlight->setVisible(false);
    }

    // 更新顶部栏标题
    if (m_convTitleLabel)
    {
        m_convTitleLabel->setText(tr("新对话"));
    }

    m_inputEdit->setFocus();
}

void AiChatPage::addConversation(const Conversation &conv)
{
    m_conversations.append(conv);
    int index = m_conversations.size() - 1;

    // 仅当对话属于当前模式时才创建并显示按钮
    if (conv.mode == m_currentMode)
    {
        auto *btn = new QPushButton(conv.title, m_convContainer);
        btn->setObjectName("subNavButton");
        btn->setFixedHeight(40);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        btn->setProperty("convIndex", index);
        btn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/nav_ai.svg",
                                                 QColor("#666666"), 18));
        btn->setIconSize(QSize(18, 18));

        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            int idx = btn->property("convIndex").toInt();
            onConversationSelected(idx);
        });

        connect(btn, &QWidget::customContextMenuRequested, this, [this, btn](const QPoint &localPos) {
            int idx = btn->property("convIndex").toInt();
            onConvListContextMenu(idx, btn->mapToGlobal(localPos));
        });

        // 在 "+ 新建对话" 按钮之前插入
        int insertPos = m_convLayout->count() - 2;
        m_convLayout->insertWidget(insertPos, btn);
        m_convButtons.append(btn);

        // 立即激活布局，使新按钮的 geometry 马上生效
        // 避免 slideHighlightTo 因 geometry 为空而延后，导致旧位置高亮残留
        m_convContainer->layout()->activate();
    }
}

QList<int> AiChatPage::filteredConvIndices() const
{
    QList<int> result;
    for (int i = 0; i < m_conversations.size(); ++i)
    {
        if (m_conversations[i].mode == m_currentMode)
        {
            result.append(i);
        }
    }
    return result;
}

void AiChatPage::rebuildConvButtonsForMode()
{
    // 移除现有按钮（不删除对话数据）
    for (QPushButton *btn : m_convButtons)
    {
        m_convLayout->removeWidget(btn);
        delete btn;
    }
    m_convButtons.clear();

    // 按当前模式重建按钮
    QList<int> indices = filteredConvIndices();
    for (int index : indices)
    {
        const Conversation &conv = m_conversations[index];
        auto *btn = new QPushButton(conv.title, m_convContainer);
        btn->setObjectName("subNavButton");
        btn->setFixedHeight(40);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        btn->setProperty("convIndex", index);
        btn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/nav_ai.svg",
                                                 QColor("#666666"), 18));
        btn->setIconSize(QSize(18, 18));

        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            int idx = btn->property("convIndex").toInt();
            onConversationSelected(idx);
        });

        connect(btn, &QWidget::customContextMenuRequested, this, [this, btn](const QPoint &localPos) {
            int idx = btn->property("convIndex").toInt();
            onConvListContextMenu(idx, btn->mapToGlobal(localPos));
        });

        // 在 "+ 新建对话" 按钮之前插入
        int insertPos = m_convLayout->count() - 2;
        m_convLayout->insertWidget(insertPos, btn);
        m_convButtons.append(btn);
    }

    m_convContainer->layout()->activate();
}

void AiChatPage::switchToMode(int mode)
{
    if (mode == m_currentMode)
    {
        return;
    }

    // 中断流式
    if (m_isStreaming)
    {
        m_streamAborting = true;
        m_aiService->stopStreaming();
        m_isStreaming = false;
    }

    m_currentMode = mode;

    // 更新模式按钮选中态
    m_modeChatBtn->setChecked(mode == 0);
    m_modeWorkBtn->setChecked(mode == 1);

    // 重建对话按钮列表
    rebuildConvButtonsForMode();

    // 清空消息区并进入草稿态
    clearMessageArea();
    showWelcome(true);
    m_currentConvIndex = -1;
    if (m_selectionHighlight)
    {
        m_selectionHighlight->setVisible(false);
    }
    if (m_convTitleLabel)
    {
        m_convTitleLabel->setText(tr("新对话"));
    }

    // 更新欢迎界面文字
    updateWelcomeContent();

    // 清空系统提示词选择
    refreshSystemPromptSelector();

    // 滑动模式指示器
    slideModeIndicator(mode);
}

void AiChatPage::switchToConversation(int index)
{
    if (index < 0 || index >= m_conversations.size())
    {
        return;
    }

    m_currentConvIndex = index;

    // 更新按钮选中状态（通过 convIndex 属性匹配真实索引）
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    int btnPos = -1;
    for (int i = 0; i < m_convButtons.size(); ++i)
    {
        int btnIdx = m_convButtons[i]->property("convIndex").toInt();
        if (btnIdx == index)
        {
            m_convButtons[i]->setChecked(true);
            m_convButtons[i]->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/nav_ai.svg",
                                                                  themeColor.isValid() ? themeColor : QColor("#10B981"), 18));
            btnPos = i;
        }
        else
        {
            m_convButtons[i]->setChecked(false);
            m_convButtons[i]->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/nav_ai.svg",
                                                                  QColor("#666666"), 18));
        }
    }
    if (btnPos >= 0)
    {
        slideHighlightTo(btnPos);
    }

    const Conversation &conv = m_conversations[index];

    // 更新顶部栏的当前对话名称
    if (m_convTitleLabel)
    {
        m_convTitleLabel->setText(conv.title);
    }

    // 切换到该对话的模型（modelId 为空时保持当前默认模型）
    if (!conv.modelId.isEmpty())
    {
        for (const AiModel &model : m_models)
        {
            if (model.id == conv.modelId)
            {
                m_currentModel = model;
                updateModelBtnText();
                break;
            }
        }
    }
    else
    {
        updateModelBtnText();
    }

    // 清空消息区域
    clearMessageArea();

    // 加载历史消息
    for (const ChatMessage &msg : conv.messages)
    {
        addMessageBubble(msg);
    }

    m_inputEdit->setFocus();
}

void AiChatPage::clearMessageArea()
{
    // 移除所有项（消息气泡、stretch），保留 m_welcomeWidget 不删除
    QLayoutItem *item;
    while ((item = m_messageLayout->takeAt(0)) != nullptr)
    {
        if (item->widget() && item->widget() != m_welcomeWidget)
        {
            item->widget()->deleteLater();
        }
        delete item;
    }

    // 默认重置为消息模式：仅底部 stretch（消息从顶部排列）
    m_welcomeWidget->setVisible(false);
    m_messageLayout->addStretch();
}

void AiChatPage::showWelcome(bool visible)
{
    // 欢迎区（标题/简介/工作模式快捷按钮）已按需求整体移除：
    // 空状态只保留背景与底部输入区，welcome 控件保留但永不显示。
    Q_UNUSED(visible)

    // 先清空布局中的所有 stretch 和 m_welcomeWidget（不删除 widget）
    QLayoutItem *item;
    while ((item = m_messageLayout->takeAt(0)) != nullptr)
    {
        // 不删除 m_welcomeWidget，只删除其他 widget 和 stretch
        if (item->widget() && item->widget() != m_welcomeWidget)
        {
            item->widget()->deleteLater();
        }
        delete item;
    }

    m_welcomeWidget->setVisible(false);
    m_messageLayout->addStretch();
}

void AiChatPage::updateConversationTitle(int index, const QString &title)
{
    if (index < 0 || index >= m_conversations.size())
    {
        return;
    }

    m_conversations[index].title = title;

    // m_convButtons 是按当前模式过滤后的按钮列表，不能按 index 直接对应。
    // 需通过 convIndex 属性匹配真实对话索引对应的按钮。
    for (QPushButton *btn : m_convButtons)
    {
        if (btn->property("convIndex").toInt() == index)
        {
            btn->setText(title);
            break;
        }
    }

    // 同步更新顶部栏的当前对话名称
    if (index == m_currentConvIndex && m_convTitleLabel)
    {
        m_convTitleLabel->setText(title);
    }
}

void AiChatPage::onConversationSelected(int index)
{
    if (index < 0 || index >= m_conversations.size() || index == m_currentConvIndex)
    {
        return;
    }

    if (m_isStreaming)
    {
        // 标记主动中断，忽略后续 streamFinished/error 信号，避免追加到错误的对话
        m_streamAborting = true;
        m_aiService->stopStreaming();
        m_isStreaming = false;
    }

    switchToConversation(index);
}

void AiChatPage::onConvListContextMenu(int index, const QPoint &globalPos)
{
    if (index < 0 || index >= m_conversations.size())
    {
        return;
    }

    m_contextMenuIdx = index;

    QMenu menu(this);

    QAction *renameAction = menu.addAction(tr("重命名对话"));
    QAction *copyAction = menu.addAction(tr("复制对话内容"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(tr("删除对话"));
    menu.addSeparator();
    QAction *clearAllAction = menu.addAction(tr("清空当前模式对话"));
    clearAllAction->setEnabled(!filteredConvIndices().isEmpty());

    // 高亮当前右键项（通过 convIndex 属性查找对应按钮）
    for (int i = 0; i < m_convButtons.size(); ++i)
    {
        int btnIdx = m_convButtons[i]->property("convIndex").toInt();
        if (btnIdx == index)
        {
            m_convButtons[i]->setChecked(true);
            slideHighlightTo(i);
            break;
        }
    }

    QAction *chosen = menu.exec(globalPos);

    if (chosen == renameAction)
    {
        onRenameConversation();
    }
    else if (chosen == copyAction)
    {
        onCopyConversation();
    }
    else if (chosen == deleteAction)
    {
        onDeleteConversation();
    }
    else if (chosen == clearAllAction)
    {
        onClearAllConversations();
    }
}

void AiChatPage::onDeleteConversation()
{
    int currentRow = m_contextMenuIdx;
    if (currentRow < 0 || currentRow >= m_conversations.size())
    {
        return;
    }

    const Conversation &conv = m_conversations.at(currentRow);
    QString title = conv.title.isEmpty() ? tr("新对话") : conv.title;
    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("删除对话"),
        tr("确定要删除对话 \"%1\" 吗？此操作不可撤销。").arg(title),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply != AppMessageBox::Yes)
    {
        return;
    }

    // 删除数据
    m_conversations.removeAt(currentRow);

    // 调整选中
    if (m_conversations.isEmpty())
    {
        m_currentConvIndex = -1;
        clearMessageArea();
        showWelcome(true);
        if (m_selectionHighlight)
            m_selectionHighlight->setVisible(false);
        if (m_convTitleLabel)
        {
            m_convTitleLabel->setText(tr("新对话"));
        }
    }
    else
    {
        // 重建按钮列表（删除后索引可能变化）
        rebuildConvButtonsForMode();

        // 切换到当前模式的下一个对话，若无则进入草稿态
        QList<int> indices = filteredConvIndices();
        if (indices.isEmpty())
        {
            m_currentConvIndex = -1;
            clearMessageArea();
            showWelcome(true);
            m_selectionHighlight->setVisible(false);
            if (m_convTitleLabel)
            {
                m_convTitleLabel->setText(tr("新对话"));
            }
        }
        else
        {
            // 选择当前模式的第一个对话
            switchToConversation(indices.first());
        }
    }

    saveConversations();
}

void AiChatPage::onRenameConversation()
{
    int currentRow = m_contextMenuIdx;
    if (currentRow < 0 || currentRow >= m_conversations.size())
    {
        return;
    }

    Conversation &conv = m_conversations[currentRow];
    QString oldTitle = conv.title.isEmpty() ? tr("新对话") : conv.title;

    bool ok = false;
    QString newTitle = AppInputDialog::getText(this, tr("重命名对话"),
        tr("请输入新名称:"), QLineEdit::Normal, oldTitle, &ok);

    if (ok && !newTitle.trimmed().isEmpty())
    {
        conv.title = newTitle.trimmed();
        // 通过 convIndex 属性匹配真实对话索引对应的按钮，而非按 currentRow 直接索引
        for (QPushButton *btn : m_convButtons)
        {
            if (btn->property("convIndex").toInt() == currentRow)
            {
                btn->setText(newTitle.trimmed());
                break;
            }
        }
        saveConversations();
    }
}

void AiChatPage::onCopyConversation()
{
    int currentRow = m_contextMenuIdx;
    if (currentRow < 0 || currentRow >= m_conversations.size())
    {
        return;
    }

    const Conversation &conv = m_conversations.at(currentRow);
    QStringList lines;
    QString title = conv.title.isEmpty() ? tr("新对话") : conv.title;
    lines << title << QString("-").repeated(40);

    for (const ChatMessage &msg : conv.messages)
    {
        QString role = (msg.role == "user") ? tr("用户") : tr("AI 助手");
        lines << QString("[%1] %2").arg(role, msg.content);
    }

    QApplication::clipboard()->setText(lines.join("\n"));
}

void AiChatPage::onClearAllConversations()
{
    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("清空当前模式对话"),
        tr("确定要清空当前模式下的所有对话吗？此操作不可撤销。"),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply != AppMessageBox::Yes)
    {
        return;
    }

    // 仅删除当前模式的对话，保留其他模式的对话
    QList<Conversation> remaining;
    for (const Conversation &conv : m_conversations)
    {
        if (conv.mode != m_currentMode)
        {
            remaining.append(conv);
        }
    }
    m_conversations = remaining;

    // 重建按钮列表
    rebuildConvButtonsForMode();

    m_currentConvIndex = -1;
    if (m_selectionHighlight)
        m_selectionHighlight->setVisible(false);
    clearMessageArea();
    showWelcome(true);
    if (m_convTitleLabel)
    {
        m_convTitleLabel->setText(tr("新对话"));
    }
    saveConversations();
}

// ============================================================================
// 动画
// ============================================================================

void AiChatPage::slideHighlightTo(int index)
{
    if (index < 0 || index >= m_convButtons.size())
    {
        if (m_selectionHighlight)
            m_selectionHighlight->setVisible(false);
        return;
    }

    QPushButton *target = m_convButtons[index];
    QRect targetGeo = target->geometry();

    // 布局未生效时延后处理
    if (targetGeo.isNull() || targetGeo.isEmpty())
    {
        QTimer::singleShot(0, this, [this, index]() {
            slideHighlightTo(index);
        });
        return;
    }

    m_selectionHighlight->setVisible(true);

    if (m_selectionHighlight->geometry().isNull() || m_selectionHighlight->geometry().isEmpty())
    {
        // 首次定位：直接跳转（无动画）
        m_selectionHighlight->setGeometry(targetGeo);
    }
    else
    {
        // 后续切换：滑动动画
        auto *anim = new QPropertyAnimation(m_selectionHighlight, "geometry");
        anim->setDuration(220);
        anim->setStartValue(m_selectionHighlight->geometry());
        anim->setEndValue(targetGeo);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void AiChatPage::buildWelcomeQuickButtons()
{
    if (!m_welcomeQuickLayout || !m_welcomeQuickBtns)
        return;

    // 清空旧按钮（保留布局本身）
    while (QLayoutItem *item = m_welcomeQuickLayout->takeAt(0))
    {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    struct QuickItem
    {
        QString label;
        QString prompt;
    };
    const QList<QuickItem> quickItems = {
        { tr("搭配整合包"),       tr("请帮我搭配一个新的 Minecraft 整合包到目标实例：[用户填的实例名]\n"
                                   "- Minecraft 版本：[用户填的内容]\n"
                                   "- 加载器及版本（Forge/Fabric/Quilt）：[用户填的内容]\n"
                                   "- 整合包风格（科技/魔法/冒险/生存/生电）：[用户填的内容]\n"
                                   "- 主要玩法诉求：[用户填的内容]\n"
                                   "- 预计模组数量：[用户填的内容]\n"
                                   "- 其他特殊要求：[用户填的内容]\n"
                                   "请基于以上信息推荐互相兼容的模组组合，并说明每个模组的作用。") },
        { tr("翻译模组"),         tr("请帮我翻译一个 Minecraft 模组，目标实例为：[用户填的实例名]\n"
                                   "- 待翻译的模组名称：[用户填的内容]\n"
                                   "- 目标语言：[用户填的内容]\n"
                                   "- 专业术语注意事项：[用户填的内容]\n"
                                   "请使用工具调取该实例的 Minecraft 版本与加载器类型，定位模组文件并读取需要翻译的资源。") },
        { tr("解决游戏崩溃"),     tr("我的 Minecraft 崩溃了，请帮我分析崩溃原因，崩溃实例为：[用户填的实例名]\n"
                                   "- 崩溃时的操作场景：[用户填的内容]\n"
                                   "- 其他补充信息：[用户填的内容]\n"
                                   "请使用工具调取该实例的 Minecraft 版本、加载器、模组列表，并读取 crash report 与 latest.log 定位问题。") },
        { tr("调试模组"),         tr("请帮我调试实例中的多个模组，让它们相互兼容、稳定运行，目标实例为：[用户填的实例名]\n"
                                   "- 存在的问题或冲突描述：[用户填的内容]\n"
                                   "- 希望优先保留的模组：[用户填的内容]\n"
                                   "- 其他要求：[用户填的内容]\n"
                                   "请使用工具调取该实例的模组列表与日志，分析模组间冲突；必要时可以替换功能相似的模组以实现兼容，并说明替换理由。") },
        { tr("升级实例"),         tr("请帮我直接升级实例到新版本，目标实例为：[用户填的实例名]\n"
                                   "- 目标 Minecraft 版本：[用户填的内容]\n"
                                   "- 目标加载器及版本（Forge/Fabric/Quilt）：[用户填的内容]\n"
                                   "- 其他要求：[用户填的内容]\n"
                                   "请使用工具先备份该实例，然后直接执行升级：升级 Minecraft 版本、加载器以及所有模组到兼容版本，升级完成后自动进入调试模组环节，排查并解决模组冲突，确保实例可正常启动运行。") },
        { tr("投影原材料"),       tr("请帮我统计 Minecraft 投影中所有方块的合成原材料清单，目标实例为：[用户填的实例名]\n"
                                   "- 投影文件名：[用户填的内容]\n"
                                   "- 其他要求：[用户填的内容]\n"
                                   "请使用工具读取该投影文件，解析其中所有方块/机器的数量，并按照合成配方递归统计所需的原始材料（例如红石中继器所需的红石、石头、木材等），最终输出每种原材料的合计数量清单。") },
    };

    // 紧凑模式使用 2 列以适配窄窗口，普通模式使用 3 列
    const int kCols = m_compactMode ? 2 : 3;
    int row = 0;
    int col = 0;
    for (const QuickItem &item : quickItems)
    {
        auto *btn = new QPushButton(item.label, m_welcomeQuickBtns);
        btn->setObjectName("aiChatQuickBtn");
        btn->setCursor(Qt::PointingHandCursor);
        // 使用 property 携带提示词，避免 lambda 捕获临时变量
        btn->setProperty("prompt", item.prompt);
        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            QString prompt = btn->property("prompt").toString();
            injectPromptToInput(prompt);
        });
        m_welcomeQuickLayout->addWidget(btn, row, col);
        ++col;
        if (col >= kCols)
        {
            col = 0;
            ++row;
        }
    }
}

void AiChatPage::setCompactMode(bool compact)
{
    if (m_compactMode == compact && property("compactMode").toBool() == compact)
        return;

    m_compactMode = compact;
    // 设置动态属性，触发 QSS 中 AiChatPage[compactMode="true"] 选择器
    setProperty("compactMode", compact);

    // 欢迎页内边距：紧凑模式缩小左右与上方留白
    if (m_welcomeWidget && m_welcomeWidget->layout())
    {
        if (compact)
            m_welcomeWidget->layout()->setContentsMargins(20, 40, 20, 20);
        else
            m_welcomeWidget->layout()->setContentsMargins(40, 80, 40, 40);
    }

    // 欢迎页间距：紧凑模式收紧标题/简介/快捷按钮间距
    if (m_welcomeWidget && m_welcomeWidget->layout())
    {
        if (compact)
            m_welcomeWidget->layout()->setSpacing(10);
        else
            m_welcomeWidget->layout()->setSpacing(16);
    }

    // 快捷按钮网格列数切换
    buildWelcomeQuickButtons();

    // 工具行布局重建：紧凑模式拆两行（选择框一行、图标一行）
    rebuildToolsRows();

    // 消息区内容内边距
    if (m_messageLayout)
    {
        if (compact)
            m_messageLayout->setContentsMargins(12, 12, 12, 12);
        else
            m_messageLayout->setContentsMargins(16, 16, 16, 16);
    }

    // 输入区内边距
    if (m_inputPanel && m_inputPanel->layout())
    {
        if (compact)
            m_inputPanel->layout()->setContentsMargins(10, 8, 10, 10);
        else
            m_inputPanel->layout()->setContentsMargins(16, 12, 16, 16);
    }

    // 输入框卡片内边距：紧凑模式收紧，给窄窗口留出更多文本空间
    if (m_inputBox && m_inputBox->layout())
    {
        if (compact)
            m_inputBox->layout()->setContentsMargins(10, 6, 6, 6);
        else
            m_inputBox->layout()->setContentsMargins(12, 8, 8, 8);
    }

    // 重新应用样式，使动态属性选择器立即生效
    this->style()->unpolish(this);
    this->style()->polish(this);
    // 对子控件也触发样式刷新（动态属性选择器需要重新计算）
    for (QWidget *child : findChildren<QWidget *>())
    {
        child->style()->unpolish(child);
        child->style()->polish(child);
    }
}

void AiChatPage::updateWelcomeContent()
{
    if (!m_welcomeTitleLabel || !m_welcomeDescLabel)
        return;

    if (m_currentMode == 0)
    {
        // 聊天模式
        m_welcomeTitleLabel->setText(tr("与 AI 助手一起聊天"));
        m_welcomeDescLabel->setText(
            tr("你好！在这里可以和我聊聊关于 Minecraft 的一切——"
               "询问生存技巧与红石生电、分享游戏中的趣事、"
               "获取游戏灵感与模组推荐，也可以让我帮你解读游戏崩溃日志。"));
        // 聊天模式不显示快捷按钮
        if (m_welcomeQuickBtns)
        {
            m_welcomeQuickBtns->setVisible(false);
        }
    }
    else
    {
        // 工作模式
        m_welcomeTitleLabel->setText(tr("让 AI 助手帮你做事"));
        m_welcomeDescLabel->setText(
            tr("你好！在这里可以交给我一些专业任务——"
               "搭配整合包、翻译模组、排查游戏崩溃、调试模组、"
               "升级游戏版本并解决模组冲突等。"));
        // 工作模式显示快捷按钮
        if (m_welcomeQuickBtns)
        {
            m_welcomeQuickBtns->setVisible(true);
        }
    }
}

void AiChatPage::injectPromptToInput(const QString &prompt)
{
    if (!m_inputEdit || prompt.isEmpty())
    {
        return;
    }

    // 若当前已有草稿文字，则追加（原文字 + 换行 + 提示词）
    QString current = m_inputEdit->toPlainText().trimmed();
    QString finalText = current.isEmpty() ? prompt : (current + "\n" + prompt);
    m_inputEdit->setPlainText(finalText);

    // 光标移到末尾并聚焦
    QTextCursor cursor = m_inputEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_inputEdit->setTextCursor(cursor);
    m_inputEdit->setFocus();
}

void AiChatPage::slideModeIndicator(int targetIndex)
{
    if (!m_modeIndicator)
        return;

    QPushButton *target = (targetIndex == 0) ? m_modeChatBtn : m_modeWorkBtn;
    if (!target)
        return;

    QRect targetGeo = target->geometry();
    if (targetGeo.isNull() || targetGeo.isEmpty())
    {
        QTimer::singleShot(0, this, [this, targetIndex]() {
            slideModeIndicator(targetIndex);
        });
        return;
    }

    m_modeIndicator->setVisible(true);
    m_modeIndicator->raise();
    // 让按钮文字在指示器之上
    target->raise();
    if (targetIndex == 0)
        m_modeWorkBtn->raise();
    else
        m_modeChatBtn->raise();

    if (m_modeIndicator->geometry().isNull() || m_modeIndicator->geometry().isEmpty())
    {
        m_modeIndicator->setGeometry(targetGeo);
    }
    else
    {
        auto *anim = new QPropertyAnimation(m_modeIndicator, "geometry");
        anim->setDuration(200);
        anim->setStartValue(m_modeIndicator->geometry());
        anim->setEndValue(targetGeo);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void AiChatPage::updateToggleConvIcon()
{
    if (!m_toggleConvBtn)
        return;

    // 收起态使用次要灰，展开态使用主题强调色
    QColor color = m_convListExpanded
                       ? QColor(ThemeManager::instance()->currentThemeColor())
                       : QColor("#888888");
    m_toggleConvBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/list_view.svg", color, 20));
}

void AiChatPage::onToggleConvList()
{
    m_convListExpanded = m_toggleConvBtn->isChecked();

    // 切换对话列表滚动区域的可见性
    m_convScrollArea->setVisible(m_convListExpanded);

    // 悬浮气泡：展开时显示并重新贴靠几何，收起时隐藏
    m_leftPanel->setVisible(m_convListExpanded);
    if (m_convListExpanded)
    {
        updateConvBubbleGeometry();
    }
    // 宿主左侧留出空隙，避免气泡遮挡消息区
    if (m_bubbleHostLayout)
    {
        const int gap = m_convListExpanded ? kBubbleWidth + kBubbleMargin : 0;
        m_bubbleHostLayout->setContentsMargins(0, 0, 0, 0);
        m_bubbleHostLayout->setContentsMargins(gap, 0, 0, 0);
    }

    // 同步按钮选中态视觉与图标颜色
    m_toggleConvBtn->setChecked(m_convListExpanded);
    updateToggleConvIcon();

    // 展开时重新定位选中滑块和模式指示器（面板收起时 geometry 不正确）
    if (m_convListExpanded)
    {
        QTimer::singleShot(0, this, [this]() {
            // 重新定位对话选中滑块（通过 convIndex 属性查找按钮位置）
            if (m_selectionHighlight && m_currentConvIndex >= 0)
            {
                int btnPos = -1;
                for (int i = 0; i < m_convButtons.size(); ++i)
                {
                    if (m_convButtons[i]->property("convIndex").toInt() == m_currentConvIndex)
                    {
                        btnPos = i;
                        break;
                    }
                }
                if (btnPos >= 0)
                {
                    m_selectionHighlight->setVisible(false);
                    m_selectionHighlight->setGeometry(QRect());
                    slideHighlightTo(btnPos);
                }
            }
            // 重新定位模式指示器
            if (m_modeIndicator)
            {
                m_modeIndicator->setGeometry(QRect());
                slideModeIndicator(m_modeChatBtn->isChecked() ? 0 : 1);
            }
        });
    }
}

void AiChatPage::animateConvButtons()
{
    auto *staggerGroup = new QSequentialAnimationGroup(this);

    auto addFadeIn = [staggerGroup](QWidget *w, int delay)
    {
        w->setGraphicsEffect(nullptr);
        auto *effect = new QGraphicsOpacityEffect(w);
        effect->setOpacity(0.0);
        w->setGraphicsEffect(effect);
        auto *fadeIn = new QPropertyAnimation(effect, "opacity");
        fadeIn->setDuration(100);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);
        fadeIn->setEasingCurve(QEasingCurve::OutCubic);
        staggerGroup->addPause(delay);
        staggerGroup->addAnimation(fadeIn);
    };

    for (QPushButton *btn : m_convButtons)
        addFadeIn(btn, 20);

    if (m_newConvBtn)
        addFadeIn(m_newConvBtn, 20);

    // 动画结束后清除透明度特效，避免残留 QGraphicsEffect
    // 导致后续带图标的按钮渲染异常（图标不随特效正确绘制）
    connect(staggerGroup, &QSequentialAnimationGroup::finished, this, [this]() {
        for (QPushButton *btn : m_convButtons)
        {
            if (btn)
                btn->setGraphicsEffect(nullptr);
        }
        if (m_newConvBtn)
            m_newConvBtn->setGraphicsEffect(nullptr);
    });

    staggerGroup->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================================================================
// 状态指示器与停止控制
// ============================================================================

void AiChatPage::updateChatStatus(ChatStatus status)
{
    m_chatStatus = status;

    if (!m_statusLabel)
    {
        return;
    }

    QString text;
    QString color;
    bool visible = true;

    switch (status)
    {
    case ChatStatus::Idle:
        text = QString();
        visible = false;
        break;
    case ChatStatus::Thinking:
        text = tr("思考中...");
        color = QStringLiteral("#2196F3");
        break;
    case ChatStatus::Searching:
        text = tr("搜索中...");
        color = QStringLiteral("#FF9800");
        break;
    case ChatStatus::CallingTool:
        text = tr("调用工具中...");
        color = QStringLiteral("#9C27B0");
        break;
    case ChatStatus::CompressingContext:
        text = tr("压缩上下文中...");
        color = QStringLiteral("#009688");
        break;
    case ChatStatus::Stopped:
        text = tr("已终止");
        color = QStringLiteral("#9E9E9E");
        break;
    case ChatStatus::Error:
        text = tr("错误");
        color = QStringLiteral("#F44336");
        break;
    }

    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: %1; padding: 2px 10px; "
        "border: none; background: transparent;").arg(color));
    m_statusLabel->setVisible(visible);

    // 同步状态圆点颜色与可见性
    if (m_statusDot)
    {
        QString dotColor = color.isEmpty() ? QStringLiteral("#94A3B8") : color;
        m_statusDot->setStyleSheet(QStringLiteral(
            "background: %1; border-radius: 4px;").arg(dotColor));
        m_statusDot->setVisible(visible);
    }

    // 同步发送/中止按钮：流式期间显示红色中止图标，否则显示发送图标
    if (m_sendBtn)
    {
        bool streaming = (status == ChatStatus::Thinking ||
                          status == ChatStatus::Searching ||
                          status == ChatStatus::CallingTool ||
                          status == ChatStatus::CompressingContext);
        updateSendButtonIcon(streaming);
        if (streaming)
        {
            m_sendBtn->setEnabled(true);
        }
        else
        {
            m_sendBtn->setEnabled(
                !m_inputEdit->toPlainText().trimmed().isEmpty());
        }
    }
}

void AiChatPage::onStopStreaming()
{
    if (!m_isStreaming)
    {
        return;
    }

    // 标记为用户主动终止：保留已生成的内容并保存到对话历史
    m_streamAborting = true;
    m_aiService->stopStreaming();
    m_isStreaming = false;

    // 停止节流定时器
    if (m_streamThrottle)
    {
        m_streamThrottle->stop();
    }

    // 切换 Markdown 渲染并保留已生成内容
    finalizeStreamWidgets();

    // 隐藏打字指示器
    if (m_typingIndicator)
    {
        m_messageLayout->removeWidget(m_typingIndicator);
        m_typingIndicator->deleteLater();
        m_typingIndicator = nullptr;
    }

    // 在 AI 气泡底部追加"已终止"统计
    QString statsText = tr("⛔ 已终止 · 耗时 %1s")
                            .arg(QString::number(m_requestTimer.elapsed() / 1000.0, 'f', 1));
    if (m_currentAiBubble)
    {
        updateBubbleStats(m_currentAiBubble, statsText);
    }

    // 保存部分 AI 回复到对话历史
    if (m_currentConvIndex >= 0 && !m_currentContent.isEmpty())
    {
        ChatMessage aiMsg;
        aiMsg.role = "assistant";
        aiMsg.content = m_currentContent;
        aiMsg.reasoning = m_currentReasoning;
        aiMsg.stats = statsText;
        aiMsg.toolCalls = m_currentAgentToolCalls;
        m_conversations[m_currentConvIndex].messages.append(aiMsg);
        saveConversations();
    }

    // 清理当前回合状态
    m_currentAiBubble = nullptr;
    m_currentThinkingBubble = nullptr;
    m_currentContentLabel = nullptr;
    m_currentThinkingLabel = nullptr;
    m_currentContentEdit = nullptr;
    m_currentThinkingEdit = nullptr;
    m_currentSearchResults.clear();
    m_currentSearchStepCard = nullptr;
    m_currentAgentStepsWidget = nullptr;
    m_currentAgentStepsLayout = nullptr;
    m_currentToolStepCards.clear();
    m_currentAgentToolCalls.clear();
    m_currentPendingStepCard = nullptr;
    m_currentTaskListCard = nullptr;
    m_streamDirty = false;
    m_streamThinkingDirty = false;

    updateChatStatus(ChatStatus::Stopped);

    // 短暂延迟后恢复为空闲状态
    QTimer::singleShot(2000, this, [this]() {
        if (m_chatStatus == ChatStatus::Stopped)
        {
            updateChatStatus(ChatStatus::Idle);
        }
    });

    // 重置中断标志：不依赖 stopStreaming() 的回调（回调可能延迟或丢失）。
    // 迟到的回调由 onStreamFinished/onStreamError 的守卫拦截。
    m_streamAborting = false;

    m_inputEdit->setFocus();
}

// ============================================================================
// 消息发送
// ============================================================================

void AiChatPage::onSendMessage()
{
    QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty() || m_isStreaming)
    {
        return;
    }

    // 重置中断标志：上一次切换对话/停止可能残留 m_streamAborting=true，
    // 若不重置会导致本次 AI 回复被 onStreamFinished 误判为"主动中断"而丢弃
    m_streamAborting = false;

    // 当前处于新对话草稿态：发送时才真正创建对话并加入列表
    if (m_currentConvIndex < 0)
    {
        Conversation conv;
        conv.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        conv.title = tr("新对话");
        conv.modelId = m_currentModel.id;
        conv.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
        conv.mode = m_currentMode;  // 绑定当前模式
        conv.systemPromptIds = m_selectedPromptIds;  // 绑定选中的系统提示词

        addConversation(conv);
        m_currentConvIndex = m_conversations.size() - 1;

        // 选中新建的对话按钮（通过 convIndex 属性匹配）
        int btnPos = -1;
        for (int i = 0; i < m_convButtons.size(); ++i)
        {
            int btnIdx = m_convButtons[i]->property("convIndex").toInt();
            if (btnIdx == m_currentConvIndex)
            {
                m_convButtons[i]->setChecked(true);
                btnPos = i;
            }
            else
            {
                m_convButtons[i]->setChecked(false);
            }
        }
        if (btnPos >= 0)
        {
            slideHighlightTo(btnPos);
        }

        if (m_convTitleLabel)
        {
            m_convTitleLabel->setText(conv.title);
        }

        // 隐藏欢迎界面
        showWelcome(false);
        saveConversations();
    }

    // 检查 API Key
    if (m_currentModel.apiKey.isEmpty())
    {
        // 提示用户设置 API Key
        ChatMessage errorMsg;
        errorMsg.role = "assistant";
        errorMsg.content = tr("请先在模型设置中配置 API Key。点击下方的「模型选择」按钮，选择或添加模型并填入 API Key。");
        m_conversations[m_currentConvIndex].messages.append(errorMsg);
        addMessageBubble(errorMsg);
        saveConversations();
        return;
    }

    // 本地模型额外校验：Ollama 服务必须在运行（探测 11434 端口）
    if (m_currentModel.isLocal)
    {
        QTcpSocket socket(this);
        socket.connectToHost(QStringLiteral("127.0.0.1"), 11434);
        if (!socket.waitForConnected(1500))
        {
            ChatMessage errorMsg;
            errorMsg.role = "assistant";
            errorMsg.content = tr("本地模型 %1 需要 Ollama 服务运行，但无法连接到 11434 端口。"
                                  "请点击「模型选择」→「本地模型」，先安装/启动 Ollama 服务。")
                                  .arg(m_currentModel.displayName);
            m_conversations[m_currentConvIndex].messages.append(errorMsg);
            addMessageBubble(errorMsg);
            saveConversations();
            return;
        }
        socket.disconnectFromHost();
    }

    // 添加用户消息（若有待发送引用，将引用描述附加到消息文本末尾作为上下文）
    ChatMessage userMsg;
    userMsg.role = "user";
    userMsg.content = text;
    const QString refText = buildReferenceText();
    if (!refText.isEmpty())
    {
        userMsg.content = text + QStringLiteral("\n\n") + refText;
        // 保存引用快照，供用户消息气泡样式化展示（AI 侧仍通过 content 中附加文本感知）
        userMsg.references = m_pendingReferences;
    }
    m_conversations[m_currentConvIndex].messages.append(userMsg);
    addMessageBubble(userMsg);

    m_inputEdit->clear();
    m_sendBtn->setEnabled(false);
    // 引用随消息发出后清空，避免下一条消息重复携带
    clearPendingReferences();

    // 标题不再立即截取，改由 onStreamFinished 完成后异步调用 AI 生成更贴切的标题。
    // 期间标题保持"新对话"占位，AI 标题到达后通过 onTitleGenerated 替换。

    // 开始流式接收
    m_isStreaming = true;
    m_currentContent = "";
    m_currentReasoning = "";
    m_currentAiBubble = nullptr;
    m_currentThinkingBubble = nullptr;
    m_currentThinkingContent = nullptr;
    m_currentContentLabel = nullptr;
    m_currentThinkingLabel = nullptr;
    m_currentContentEdit = nullptr;
    m_currentThinkingEdit = nullptr;
    m_streamFlushedLen = 0;
    m_streamThinkingFlushedLen = 0;
    m_typingIndicator = nullptr;
    m_currentSearchStepCard = nullptr;
    m_currentSearchResults.clear();
    m_currentAgentStepsWidget = nullptr;
    m_currentAgentStepsLayout = nullptr;
    m_currentToolStepCards.clear();
    m_currentAgentToolCalls.clear();
    m_currentPendingStepCard = nullptr;
    m_currentTaskListCard = nullptr;

    // 显示打字指示器
    m_typingIndicator = createTypingIndicator();
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, m_typingIndicator);

    m_requestTimer.start();
    // 思考强度选择为运行时总开关：>0 时覆盖模型配置中的 supportsThinking
    AiModel sendModel = m_currentModel;
    sendModel.supportsThinking = (m_thinkingEffort > 0);
    m_aiService->setThinkingEffort(m_thinkingEffort);
    // 根据当前对话所属模式注入对应的系统提示词
    AssistantMode assistantMode = (m_conversations[m_currentConvIndex].mode == 1)
                                      ? AssistantMode::Work
                                      : AssistantMode::Chat;
    // 从对话绑定的提示词 ID 列表中解析自定义系统提示词
    QString customPrompt;
    const QStringList &promptIds = m_conversations[m_currentConvIndex].systemPromptIds;
    if (!promptIds.isEmpty()) {
        QSettings s;
        QString json = s.value("ai/systemPromptLibrary", "").toString();
        if (!json.isEmpty()) {
            QJsonParseError pe;
            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
            if (pe.error == QJsonParseError::NoError && doc.isArray()) {
                QStringList parts;
                for (const QString &id : promptIds) {
                    for (const QJsonValue &v : doc.array()) {
                        QJsonObject o = v.toObject();
                        if (o["id"].toString() == id) {
                            QString content = o["content"].toString().trimmed();
                            if (!content.isEmpty())
                                parts.append(content);
                            break;
                        }
                    }
                }
                customPrompt = parts.join(QStringLiteral("\n\n---\n\n"));
            }
        }
    }
    // 联网搜索不再单独提供模式选择，作为 Agent 可调用工具按需触发（默认不注入联网提示）
    m_aiService->sendMessage(m_conversations[m_currentConvIndex].messages, sendModel,
                             WebSearchMode::Off, assistantMode, customPrompt);
    saveConversations();

    // 进入思考中状态
    updateChatStatus(ChatStatus::Thinking);
}

// ============================================================================
// 流式接收
// ============================================================================

/// 同步流式编辑器高度：让气泡随文档增长（编辑器内部不出现滚动条）。
/// 收起态下文档尚未按实际宽度换行，此时算出的高度不可信，待展开时再补一次。
static void syncStreamEditHeight(QTextEdit *edit)
{
    if (!edit || !edit->isVisible())
    {
        return;
    }
    const int h = qMax(int(edit->document()->size().height()) + 2, edit->fontMetrics().height() + 4);
    if (edit->height() != h)
    {
        edit->setFixedHeight(h);
    }
}

QTextEdit* AiChatPage::createStreamEdit(const QString &objectName, QWidget *parent)
{
    auto *edit = new QTextEdit(parent);
    edit->setObjectName(objectName);
    edit->setReadOnly(true);
    edit->setUndoRedoEnabled(false);
    edit->setAcceptRichText(false);
    edit->setFrameStyle(QFrame::NoFrame);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setLineWrapMode(QTextEdit::WidgetWidth);
    edit->setCursor(Qt::IBeamCursor);
    edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    edit->document()->setDocumentMargin(0);
    edit->viewport()->setAutoFillBackground(false);
    return edit;
}

void AiChatPage::appendStreamText(QTextEdit *edit, const QString &full, int &flushedLen)
{
    if (!edit)
    {
        return;
    }

    if (full.size() > flushedLen)
    {
        edit->moveCursor(QTextCursor::End);
        edit->insertPlainText(full.mid(flushedLen));
        flushedLen = full.size();
    }

    syncStreamEditHeight(edit);
}

void AiChatPage::finalizeStreamWidgets()
{
    // 补齐节流期间尚未刷入的尾部
    appendStreamText(m_currentContentEdit, m_currentContent, m_streamFlushedLen);
    appendStreamText(m_currentThinkingEdit, m_currentReasoning, m_streamThinkingFlushedLen);

    // 正文：隐藏流式编辑器，切回 QLabel 一次性渲染 Markdown（保持既有样式）
    if (m_currentContentEdit)
    {
        m_currentContentEdit->hide();
        m_currentContentEdit->deleteLater();
        m_currentContentEdit = nullptr;
        m_streamFlushedLen = 0;
    }
    if (m_currentContentLabel)
    {
        m_currentContentLabel->setTextFormat(Qt::MarkdownText);
        m_currentContentLabel->setText(m_currentContent);
        m_currentContentLabel->show();
    }

    // 思考：纯文本编辑器直接保留，无需再渲染
    if (m_currentThinkingLabel)
    {
        m_currentThinkingLabel->setText(m_currentReasoning);
    }
}

void AiChatPage::onStreamContent(const QString &delta)
{
    m_currentContent += delta;

    // 隐藏打字指示器
    if (m_typingIndicator)
    {
        m_messageLayout->removeWidget(m_typingIndicator);
        m_typingIndicator->deleteLater();
        m_typingIndicator = nullptr;
    }
    if (!m_currentAiBubble)
    {
        // 创建 AI 消息气泡
        m_currentAiBubble = createMessageWidget("", false);
        m_messageLayout->insertWidget(m_messageLayout->count() - 1, m_currentAiBubble);

        // 找到气泡中的内容标签；流式期间改用 QTextEdit 增量追加，
        // 避免 QLabel 全量 setText 每次重新排版整篇正文造成卡顿
        m_currentContentLabel = m_currentAiBubble->findChild<QLabel*>("msgContent");
        if (m_currentContentLabel)
        {
            // 流式期间正文为纯文本，收尾时再切回 Markdown
            m_currentContentLabel->setTextFormat(Qt::PlainText);
        }
        if (auto *bubbleFrame = m_currentAiBubble->findChild<QFrame*>(QStringLiteral("aiChatAiBubble")))
        {
            m_currentContentEdit = createStreamEdit(QStringLiteral("streamMsgContent"), bubbleFrame);
            if (auto *bubbleLayout = bubbleFrame->layout())
            {
                bubbleLayout->addWidget(m_currentContentEdit);
            }
        }
        if (m_currentContentEdit && m_currentContentLabel)
        {
            m_currentContentLabel->hide();
        }
        m_streamFlushedLen = 0;
    }

    // 收到首个正文内容：思考阶段结束，思考行状态置为「已完成」
    if (m_currentThinkingBubble)
    {
        if (auto *st = m_currentThinkingBubble->findChild<QLabel*>(QStringLiteral("thinkingStatusText")))
        {
            if (st->text() != tr("已完成"))
            {
                st->setText(tr("已完成"));
                st->setProperty("type", QStringLiteral("success"));
                st->style()->polish(st);
            }
        }
    }

    // 标记有新内容待刷新，节流定时器 50ms 后触发 UI 更新
    m_streamDirty = true;
    if (!m_streamThrottle->isActive())
    {
        m_streamThrottle->start(50);
    }
}

void AiChatPage::onStreamReasoning(const QString &delta)
{
    m_currentReasoning += delta;

    if (!m_currentThinkingBubble)
    {
        m_currentThinkingBubble = createThinkingWidget(m_currentReasoning, true);
        int insertPos = m_messageLayout->count() - 1;
        if (m_currentAiBubble)
        {
            insertPos = m_messageLayout->indexOf(m_currentAiBubble);
        }
        m_messageLayout->insertWidget(insertPos, m_currentThinkingBubble);
        m_currentThinkingLabel = m_currentThinkingBubble->findChild<QLabel*>("thinkingContent");
        m_currentThinkingEdit = m_currentThinkingBubble->findChild<QTextEdit*>("thinkingStreamContent");
        m_currentThinkingContent = m_currentThinkingBubble->findChild<QWidget*>("thinkingContentWidget");
        // 编辑器已带入当前已累积的思考文本，避免后续追加时重复
        m_streamThinkingFlushedLen = m_currentThinkingEdit ? m_currentReasoning.size() : 0;
    }

    // 节流刷新：标记有新思考内容待刷新，由 m_streamThrottle 统一批量更新 UI
    m_streamThinkingDirty = true;
    if (!m_streamThrottle->isActive())
    {
        m_streamThrottle->start(50);
    }
}

void AiChatPage::onStreamFinished(const TokenUsage &usage)
{
    // 若是主动中断（切换对话/新建对话/用户点击停止），忽略本次回调，避免追加到错误的对话
    if (m_streamAborting)
    {
        m_streamAborting = false;
        m_isStreaming = false;
        m_streamThrottle->stop();
        m_streamDirty = false;
        m_streamThinkingDirty = false;
        return;
    }

    // 守卫：迟到回调检测
    // 场景：onStopStreaming/切换对话已清理 m_currentAiBubble 并重置 m_isStreaming，
    // 但 stopStreaming() 的回调延迟到达。此时不应再处理，否则会向错误对话追加空消息。
    if (!m_isStreaming && !m_currentAiBubble)
    {
        return;
    }

    m_isStreaming = false;

    // 流式完成，恢复空闲状态
    updateChatStatus(ChatStatus::Idle);

    // 停止节流定时器，立即刷新最终内容
    m_streamThrottle->stop();
    m_streamDirty = false;
    m_streamThinkingDirty = false;

    // 切回 QLabel 渲染最终内容（全文只解析一次）
    finalizeStreamWidgets();

    // 思考状态与行尾预览收尾
    if (m_currentThinkingBubble)
    {
        if (auto *st = m_currentThinkingBubble->findChild<QLabel*>(QStringLiteral("thinkingStatusText")))
        {
            st->setText(tr("已完成"));
            st->setProperty("type", QStringLiteral("success"));
            st->style()->polish(st);
        }
        if (auto *pv = m_currentThinkingBubble->findChild<QLabel*>(QStringLiteral("thinkingPreview")))
        {
            setThinkingPreview(pv, m_currentReasoning);
        }
    }

    // 耗时（毫秒）
    double elapsedSec = m_requestTimer.elapsed() / 1000.0;

    // 隐藏打字指示器
    if (m_typingIndicator)
    {
        m_messageLayout->removeWidget(m_typingIndicator);
        m_typingIndicator->deleteLater();
        m_typingIndicator = nullptr;
    }
    // 更新 AI 气泡的统计信息
    QString statsText;
    if (m_currentAiBubble)
    {
        if (usage.valid)
        {
            // 费用估算（通用均价：$0.5/1M 输入, $1.5/1M 输出）
            double cost = (usage.promptTokens * 0.5 + usage.completionTokens * 1.5) / 1000000.0;
            statsText = tr("✓ 已完成 · 输入 %1 · 输出 %2 · 共 %3 tokens · 耗时 %4s · ≈ $%5")
                            .arg(usage.promptTokens)
                            .arg(usage.completionTokens)
                            .arg(usage.totalTokens)
                            .arg(QString::number(elapsedSec, 'f', 1))
                            .arg(QString::number(cost, 'f', 4));
        }
        else
        {
            statsText = tr("✓ 已完成 · 耗时 %1s").arg(QString::number(elapsedSec, 'f', 1));
        }

        updateBubbleStats(m_currentAiBubble, statsText);
    }

    // 保存 AI 回复到对话数据（含统计信息与 Agent 工具调用记录）
    if (m_currentConvIndex >= 0)
    {
        ChatMessage aiMsg;
        aiMsg.role = "assistant";
        aiMsg.content = m_currentContent;
        aiMsg.reasoning = m_currentReasoning;
        aiMsg.stats = statsText;
        aiMsg.toolCalls = m_currentAgentToolCalls;
        m_conversations[m_currentConvIndex].messages.append(aiMsg);
    }

    m_currentAiBubble = nullptr;
    m_currentThinkingBubble = nullptr;
    m_currentContentLabel = nullptr;
    m_currentThinkingLabel = nullptr;
    m_currentContentEdit = nullptr;
    m_currentThinkingEdit = nullptr;
    m_currentSearchResults.clear();
    m_currentSearchStepCard = nullptr;
    m_currentAgentStepsWidget = nullptr;
    m_currentAgentStepsLayout = nullptr;
    m_currentToolStepCards.clear();
    m_currentAgentToolCalls.clear();
    m_currentPendingStepCard = nullptr;
    m_currentTaskListCard = nullptr;

    m_sendBtn->setEnabled(!m_inputEdit->toPlainText().trimmed().isEmpty());
    m_inputEdit->setFocus();
    saveConversations();

    // 自动生成对话标题：仅当标题仍为"新对话"（即首轮对话且用户未手动重命名）时触发。
    // 使用独立的非流式请求（m_titleReply），不干扰主对话流；失败时静默忽略，保持原占位标题。
    // 记录 pending 索引，避免用户在标题返回前切换对话时错写到其他对话。
    if (m_currentConvIndex >= 0 &&
        m_conversations[m_currentConvIndex].title == tr("新对话") &&
        !m_conversations[m_currentConvIndex].messages.isEmpty())
    {
        // 取最后一条 user 消息作为标题生成依据（通常即本轮用户输入）
        QString userMsgForTitle;
        const auto &msgs = m_conversations[m_currentConvIndex].messages;
        for (int i = msgs.size() - 1; i >= 0; --i)
        {
            if (msgs.at(i).role == "user")
            {
                userMsgForTitle = msgs.at(i).content;
                break;
            }
        }
        if (!userMsgForTitle.isEmpty())
        {
            m_pendingTitleConvIndex = m_currentConvIndex;
            m_aiService->generateTitle(userMsgForTitle, m_currentContent, m_currentModel);
        }
    }
}

void AiChatPage::onStreamError(const QString &error)
{
    // 若是主动中断（切换对话/新建对话/用户点击停止），忽略本次错误回调
    if (m_streamAborting)
    {
        m_streamAborting = false;
        m_isStreaming = false;
        m_streamThrottle->stop();
        m_streamDirty = false;
        m_streamThinkingDirty = false;
        return;
    }

    // 守卫：迟到回调检测（与 onStreamFinished 同理）
    if (!m_isStreaming && !m_currentAiBubble)
    {
        return;
    }

    m_isStreaming = false;

    // 错误状态
    updateChatStatus(ChatStatus::Error);

    // 短暂延迟后恢复为空闲状态
    QTimer::singleShot(3000, this, [this]() {
        if (m_chatStatus == ChatStatus::Error)
        {
            updateChatStatus(ChatStatus::Idle);
        }
    });

    // 停止节流并切换到 Markdown 渲染
    m_streamThrottle->stop();
    m_streamDirty = false;
    m_streamThinkingDirty = false;
    finalizeStreamWidgets();

    // 隐藏打字指示器
    if (m_typingIndicator)
    {
        m_messageLayout->removeWidget(m_typingIndicator);
        m_typingIndicator->deleteLater();
        m_typingIndicator = nullptr;
    }
    // 显示错误消息
    ChatMessage errorMsg;
    errorMsg.role = "assistant";
    errorMsg.content = tr("错误: %1").arg(error);
    addMessageBubble(errorMsg);

    if (m_currentConvIndex >= 0)
    {
        m_conversations[m_currentConvIndex].messages.append(errorMsg);
    }

    m_currentAiBubble = nullptr;
    m_currentThinkingBubble = nullptr;
    m_currentContentLabel = nullptr;
    m_currentThinkingLabel = nullptr;
    m_currentContentEdit = nullptr;
    m_currentThinkingEdit = nullptr;
    m_currentSearchResults.clear();
    m_currentSearchStepCard = nullptr;

    m_sendBtn->setEnabled(true);
    m_inputEdit->setFocus();
    saveConversations();
}

// ============================================================================
// 模型选择
// ============================================================================

void AiChatPage::onModelSelectClicked()
{
    // 首次打开时创建独立窗口，后续重复利用
    if (!m_modelDialog)
    {
        m_modelDialog = new ModelSelectDialog(this);
        m_modelDialog->setAttribute(Qt::WA_DeleteOnClose, false);

        // 模型配置保存：实时更新模型列表
        connect(m_modelDialog, &ModelSelectDialog::modelConfigured, this, [this](const AiModel &model) {
            bool found = false;
            for (int i = 0; i < m_models.size(); ++i)
            {
                if (m_models[i].id == model.id)
                {
                    m_models[i] = model;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                m_models.append(model);
            }
            saveModelSettings();
        });

        // 模型移除：清空对应 API Key
        connect(m_modelDialog, &ModelSelectDialog::modelRemoved, this, [this](const QString &modelId) {
            for (int i = 0; i < m_models.size(); ++i)
            {
                if (m_models[i].id == modelId)
                {
                    m_models[i].apiKey = "";
                    break;
                }
            }
            saveModelSettings();
        });

        // 选择模型：实时更新当前对话使用的模型
        connect(m_modelDialog, &ModelSelectDialog::modelSelected, this, [this](const AiModel &model) {
            if (model.id.isEmpty())
            {
                return;
            }
            m_currentModel = model;
            updateModelBtnText();

            if (m_currentConvIndex >= 0)
            {
                m_conversations[m_currentConvIndex].modelId = model.id;
                saveConversations();
            }
        });

        // 本地模型清单变化：同步到 m_models 并持久化
        // LocalModelDialog 拉取/删除完成时触发，ModelSelectDialog 已更新自身 m_allModels，
        // 这里直接读取并写回持久化文件。
        connect(m_modelDialog, &ModelSelectDialog::localModelsChanged, this, [this]() {
            m_models = m_modelDialog->allModels();
            saveModelSettings();
            // 同时触发一次异步刷新，确保 m_models 与 Ollama 实际状态一致
            refreshLocalModels();
        });

        // 打开设置页请求：转发到主窗口
        connect(m_modelDialog, &ModelSelectDialog::openSettingsRequested, this, [this]() {
            QWidget *w = window();
            if (w) {
                QMetaObject::invokeMethod(w, "openAiAssistantSettings", Qt::QueuedConnection);
            }
        });
    }

    // 同步最新的模型列表与当前选中模型
    m_modelDialog->setModels(m_models);
    m_modelDialog->setCurrentModel(m_currentModel.id);

    // 作为独立窗口显示并置于前台
    m_modelDialog->show();
    m_modelDialog->raise();
    m_modelDialog->activateWindow();
}

// ============================================================================
// 消息气泡
// ============================================================================

void AiChatPage::addMessageBubble(const ChatMessage &msg)
{
    bool isUser = (msg.role == "user");

    // 历史 AI 消息：先恢复思考过程行（位于内容气泡上方），默认收起（行尾显示最新思考片段）
    if (!isUser && !msg.reasoning.isEmpty())
    {
        QWidget *thinkingBubble = createThinkingWidget(msg.reasoning);
        m_messageLayout->insertWidget(m_messageLayout->count() - 1, thinkingBubble);
    }

    // 历史 AI 消息：先恢复 Agent 工具调用步骤卡片（位于内容气泡之前）
    if (!isUser && !msg.toolCalls.isEmpty())
    {
        rebuildToolStepCards(msg.toolCalls);
    }

    QWidget *bubble = createMessageWidget(msg.content, isUser, msg.references);

    // 插入到 stretch 之前
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, bubble);

    // 历史 AI 消息：恢复统计信息
    if (!isUser && !msg.stats.isEmpty())
    {
        updateBubbleStats(bubble, msg.stats);
    }

    // 自动滚动
    QScrollBar *sb = m_messageArea->verticalScrollBar();
    QTimer::singleShot(50, this, [sb]() {
        sb->setValue(sb->maximum());
    });
}

// ============================================================================
// 文本右键菜单
// ============================================================================

void AiChatPage::setupTextContextMenu(QLabel *label, bool isUser)
{
    // 启用文本可选（鼠标拖选）
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(label, &QWidget::customContextMenuRequested, this, [this, label, isUser](const QPoint &pos) {
        QMenu menu(label);
        menu.setStyleSheet(label->styleSheet());

        bool hasSelection = label->hasSelectedText();

        // 通用项
        QAction *copySelAction = nullptr;
        if (hasSelection)
        {
            copySelAction = menu.addAction(tr("复制选中"));
            menu.addSeparator();
        }

        QAction *copyAllPlainAction = menu.addAction(tr("复制纯文本"));
        QAction *copyAllMdAction = nullptr;
        if (!isUser)
        {
            copyAllMdAction = menu.addAction(tr("复制为 Markdown"));
        }

        menu.addSeparator();
        QAction *selectAllAction = menu.addAction(tr("全选"));

        QAction *regenAction = nullptr;
        QAction *quoteAction = nullptr;
        if (!isUser)
        {
            menu.addSeparator();
            // 引用 AI 回答：优先引用选中文字，无选中则引用整段回复
            if (hasSelection)
            {
                quoteAction = menu.addAction(tr("引用选中文字"));
            }
            else
            {
                quoteAction = menu.addAction(tr("引用此段回答"));
            }
            menu.addSeparator();
            regenAction = menu.addAction(tr("重新回答"));
        }

        QAction *chosen = menu.exec(label->mapToGlobal(pos));

        if (chosen == copySelAction)
        {
            QApplication::clipboard()->setText(label->selectedText());
        }
        else if (chosen == copyAllPlainAction)
        {
            QApplication::clipboard()->setText(label->text());
        }
        else if (chosen == copyAllMdAction)
        {
            QApplication::clipboard()->setText(label->text());
        }
        else if (chosen == selectAllAction)
        {
            label->setSelection(0, label->text().length());
        }
        else if (chosen == quoteAction)
        {
            // 选中文字优先，否则引用整段
            const QString quoteText = hasSelection ? label->selectedText() : label->text();
            addQuoteReference(quoteText);
        }
        else if (chosen == regenAction)
        {
            regenerateLastAnswer();
        }
    });
}

QWidget* AiChatPage::createMessageWidget(const QString &text, bool isUser,
                                         const QList<ResourceReference> &refs)
{
    auto *container = new QWidget();
    container->setObjectName("aiChatBubbleContainer");
    container->setAttribute(Qt::WA_StyledBackground, false);
    container->setAutoFillBackground(false);
    auto *outerLayout = new QVBoxLayout(container);
    outerLayout->setContentsMargins(0, 0, 0, 8);
    outerLayout->setSpacing(4);

    // 第一行：气泡 + 对齐
    auto *rowLayout = new QHBoxLayout();
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);

    auto *bubble = new QFrame();
    bubble->setObjectName(isUser ? "aiChatUserBubble" : "aiChatAiBubble");

    auto *bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(14, 10, 14, 10);
    bubbleLayout->setSpacing(0);

    // 用户消息且有引用：剥离附加的引用文本块，仅展示用户输入文本，
    // 引用部分改用样式化卡片展示（AI 侧仍通过 content 中的引用文本感知）
    QString displayText = text;
    if (isUser && !refs.isEmpty())
    {
        const int refMarker = displayText.indexOf(QStringLiteral("📎 引用资源"));
        if (refMarker >= 0)
        {
            displayText = displayText.left(refMarker).trimmed();
        }
    }
    auto *contentLabel = new QLabel(displayText);
    contentLabel->setObjectName("msgContent");
    contentLabel->setWordWrap(true);
    // 用户气泡用纯文本；AI 气泡用 Markdown 渲染
    contentLabel->setTextFormat(isUser ? Qt::PlainText : Qt::MarkdownText);
    contentLabel->setOpenExternalLinks(true);
    // 文本可选（鼠标拖选）
    contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // AI 气泡：水平扩展占满可用宽度（随窗口自适应）
    // 用户气泡：保持紧凑右对齐
    if (!isUser)
    {
        bubble->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        // contentLabel 用 Ignored，避免 QLabel 因 wordWrap 要求过宽导致 bubble 撑爆
        contentLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    else
    {
        bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    }

    setupTextContextMenu(contentLabel, isUser);
    bubbleLayout->addWidget(contentLabel);

    // 用户消息：在气泡内追加只读引用卡片（样式化展示已发送引用）
    if (isUser && !refs.isEmpty())
    {
        QWidget *refChips = buildReadOnlyRefChips(refs);
        bubbleLayout->addWidget(refChips);
    }

    if (isUser)
    {
        rowLayout->addStretch();
        rowLayout->addWidget(bubble);
    }
    else
    {
        // AI 气泡：不加 stretch，让 Expanding sizePolicy 的 bubble 占满全部宽度
        rowLayout->addWidget(bubble);
    }
    outerLayout->addLayout(rowLayout);

    // AI 气泡：创建底部操作栏（统计 + 按钮）
    if (!isUser)
    {
        createBubbleActions(container, text);
    }

    return container;
}

QWidget* AiChatPage::createThinkingWidget(const QString &text, bool running)
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 单行标题：SVG 折叠箭头 + 「思考」 + 状态（整行可点击展开/收起）
    auto *headerBtn = new QPushButton(container);
    headerBtn->setObjectName("thinkingHeaderRow");
    headerBtn->setCursor(Qt::PointingHandCursor);
    headerBtn->setFlat(true);
    auto *headerLayout = new QHBoxLayout(headerBtn);
    headerLayout->setContentsMargins(2, 2, 2, 2);
    headerLayout->setSpacing(6);

    auto *chevronLabel = new QLabel(headerBtn);
    chevronLabel->setObjectName("thinkingChevron");
    chevronLabel->setFixedSize(kChevronSize, kChevronSize);
    chevronLabel->setAlignment(Qt::AlignCenter);
    setChevronPixmap(chevronLabel, false);
    headerLayout->addWidget(chevronLabel);

    auto *titleLabel = new QLabel(tr("思考"), headerBtn);
    titleLabel->setObjectName("thinkingTitle");
    headerLayout->addWidget(titleLabel);

    // 状态：思考中…（琥珀色）→ 已完成（绿色）
    auto *statusLabel = new QLabel(running ? tr("思考中…") : tr("已完成"), headerBtn);
    statusLabel->setObjectName("thinkingStatusText");
    statusLabel->setProperty("type", running ? QStringLiteral("info") : QStringLiteral("success"));
    statusLabel->style()->polish(statusLabel);
    headerLayout->addWidget(statusLabel);

    // 思考内容占满剩余一行：流式时显示最新思考（前面的省略），行尾为最新内容
    auto *previewLabel = new QLabel(headerBtn);
    previewLabel->setObjectName("thinkingPreview");
    previewLabel->setMinimumWidth(0);
    headerLayout->addWidget(previewLabel, 1);
    setThinkingPreview(previewLabel, text);

    // 展开内容：默认收起；流式期间用 QTextEdit 增量追加（QLabel 全量重排会卡顿），历史消息用 QLabel
    auto *contentWidget = new QWidget();
    contentWidget->setObjectName("thinkingContentWidget");
    contentWidget->setVisible(false);
    auto *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 4, 0, 0);

    QTextEdit *streamEdit = nullptr;
    if (running)
    {
        streamEdit = createStreamEdit(QStringLiteral("thinkingStreamContent"), contentWidget);
        streamEdit->setPlainText(text);
        contentLayout->addWidget(streamEdit);
    }
    else
    {
        auto *contentLabel = new QLabel(text);
        contentLabel->setObjectName("thinkingContent");
        contentLabel->setWordWrap(true);
        contentLabel->setTextFormat(Qt::PlainText);
        setupTextContextMenu(contentLabel, false);
        contentLayout->addWidget(contentLabel);
    }

    layout->addWidget(headerBtn);
    layout->addWidget(contentWidget);

    // 点击标题行切换展开/收起，箭头在 ▶ / ∨ 间切换
    connect(headerBtn, &QPushButton::clicked, container, [contentWidget, chevronLabel, streamEdit]() {
        const bool visible = !contentWidget->isVisible();
        contentWidget->setVisible(visible);
        setChevronPixmap(chevronLabel, visible);
        if (visible && streamEdit)
        {
            // 展开后文档才按实际宽度换行，补一次高度同步
            QTimer::singleShot(0, streamEdit, [streamEdit]() { syncStreamEditHeight(streamEdit); });
        }
    });

    return container;
}

// ============================================================================
// 打字指示器
// ============================================================================

QWidget* AiChatPage::createTypingIndicator()
{
    auto *container = new QWidget();
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *dot1 = new QLabel("●");
    dot1->setObjectName("typingDot1");
    layout->addWidget(dot1);

    auto *dot2 = new QLabel("●");
    dot2->setObjectName("typingDot2");
    layout->addWidget(dot2);

    auto *dot3 = new QLabel("●");
    dot3->setObjectName("typingDot3");
    layout->addWidget(dot3);

    layout->addStretch();

    return container;
}

// ============================================================================
// AI 气泡底部操作栏
// ============================================================================

void AiChatPage::createBubbleActions(QWidget *container, const QString &content)
{
    if (!container)
    {
        return;
    }

    auto *outerLayout = qobject_cast<QVBoxLayout*>(container->layout());
    if (!outerLayout)
    {
        return;
    }

    // 创建 actions 容器
    auto *actionsWidget = new QWidget(container);
    actionsWidget->setObjectName("bubbleActionsWidget");
    actionsWidget->setAutoFillBackground(false);
    actionsWidget->setAttribute(Qt::WA_StyledBackground, false);
    auto *actionsLayout = new QHBoxLayout(actionsWidget);
    actionsLayout->setContentsMargins(0, 4, 0, 0);
    actionsLayout->setSpacing(6);
    outerLayout->addWidget(actionsWidget);

    // 统计信息标签（初始为空，隐藏；onStreamFinished 时 updateBubbleStats 填充）
    auto *statsLabel = new QLabel(actionsWidget);
    statsLabel->setObjectName("aiChatStatsLabel");
    statsLabel->setWordWrap(true);
    statsLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    statsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    statsLabel->hide();
    actionsLayout->addWidget(statsLabel, 1);

    // 弹性空间（统计为空时把按钮推到右边）
    actionsLayout->addStretch();

    // 按钮工厂
    QColor normalColor("#9aa0a6");
    QColor activeColor("#1a73e8");
    auto makeBtn = [actionsWidget, normalColor](const QString &svgPath, const QString &tip) {
        auto *btn = new QPushButton(actionsWidget);
        btn->setObjectName("bubbleActionBtn");
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedSize(28, 28);
        btn->setFlat(true);
        btn->setIcon(IconHelper::loadColoredIcon(svgPath, normalColor, 16));
        btn->setIconSize(QSize(16, 16));
        return btn;
    };

    auto *copyPlainBtn = makeBtn(QStringLiteral(":/Images/Icons/copy.svg"), tr("复制纯文本"));
    auto *copyMdBtn = makeBtn(QStringLiteral(":/Images/Icons/copy_md.svg"), tr("复制为 Markdown"));
    auto *regenBtn = makeBtn(QStringLiteral(":/Images/Icons/refresh.svg"), tr("重新回答"));
    auto *likeBtn = makeBtn(QStringLiteral(":/Images/Icons/thumbs_up.svg"), tr("点赞"));
    auto *dislikeBtn = makeBtn(QStringLiteral(":/Images/Icons/thumbs_down.svg"), tr("踩"));

    actionsLayout->addWidget(copyPlainBtn);
    actionsLayout->addWidget(copyMdBtn);
    actionsLayout->addWidget(regenBtn);
    actionsLayout->addWidget(likeBtn);
    actionsLayout->addWidget(dislikeBtn);

    // 连接信号
    QString capturedContent = content;
    connect(copyPlainBtn, &QPushButton::clicked, this, [capturedContent]() {
        QApplication::clipboard()->setText(capturedContent);
    });
    connect(copyMdBtn, &QPushButton::clicked, this, [capturedContent]() {
        QApplication::clipboard()->setText(capturedContent);
    });
    connect(regenBtn, &QPushButton::clicked, this, [this]() {
        regenerateLastAnswer();
    });

    // 点赞/踩：切换选中态并改变图标颜色
    auto refreshIcons = [activeColor, normalColor](QPushButton *like, QPushButton *dislike) {
        bool likeActive = like->property("active").toBool();
        bool dislikeActive = dislike->property("active").toBool();
        like->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/thumbs_up.svg",
                                                  likeActive ? activeColor : normalColor, 16));
        dislike->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/thumbs_down.svg",
                                                     dislikeActive ? activeColor : normalColor, 16));
    };
    connect(likeBtn, &QPushButton::clicked, this, [likeBtn, dislikeBtn, refreshIcons]() {
        bool active = likeBtn->property("active").toBool();
        likeBtn->setProperty("active", !active);
        dislikeBtn->setProperty("active", false);
        refreshIcons(likeBtn, dislikeBtn);
    });
    connect(dislikeBtn, &QPushButton::clicked, this, [likeBtn, dislikeBtn, refreshIcons]() {
        bool active = dislikeBtn->property("active").toBool();
        dislikeBtn->setProperty("active", !active);
        likeBtn->setProperty("active", false);
        refreshIcons(likeBtn, dislikeBtn);
    });
}

void AiChatPage::updateBubbleStats(QWidget *container, const QString &statsText)
{
    if (!container)
    {
        return;
    }

    // 直接查找 statsLabel 并更新文本
    auto *statsLabel = container->findChild<QLabel*>(QStringLiteral("aiChatStatsLabel"));
    if (!statsLabel)
    {
        return;
    }

    if (statsText.isEmpty())
    {
        statsLabel->hide();
    }
    else
    {
        statsLabel->setText(statsText);
        statsLabel->show();
    }
}

void AiChatPage::regenerateLastAnswer()
{
    if (m_isStreaming)
    {
        return;
    }
    if (m_currentConvIndex < 0)
    {
        return;
    }

    auto &msgs = m_conversations[m_currentConvIndex].messages;

    // 找最后一条 assistant 消息
    int lastAssistant = -1;
    for (int i = msgs.size() - 1; i >= 0; --i)
    {
        if (msgs[i].role == "assistant")
        {
            lastAssistant = i;
            break;
        }
    }
    if (lastAssistant < 0)
    {
        return;
    }

    // 删除该 assistant 消息（以及其后所有消息）
    while (msgs.size() > lastAssistant)
    {
        msgs.removeLast();
    }

    // 重新渲染消息区
    clearMessageArea();
    for (const ChatMessage &msg : msgs)
    {
        addMessageBubble(msg);
    }

    // 重新发送
    m_isStreaming = true;
    m_currentContent = "";
    m_currentReasoning = "";
    m_currentAiBubble = nullptr;
    m_currentThinkingBubble = nullptr;
    m_currentThinkingContent = nullptr;
    m_currentContentLabel = nullptr;
    m_currentThinkingLabel = nullptr;
    m_currentContentEdit = nullptr;
    m_currentThinkingEdit = nullptr;
    m_streamFlushedLen = 0;
    m_streamThinkingFlushedLen = 0;
    m_typingIndicator = nullptr;
    m_currentSearchStepCard = nullptr;
    m_currentSearchResults.clear();

    m_typingIndicator = createTypingIndicator();
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, m_typingIndicator);

    m_requestTimer.start();
    // 思考强度选择为运行时总开关，覆盖模型配置中的 supportsThinking
    AiModel regenModel = m_currentModel;
    regenModel.supportsThinking = (m_thinkingEffort > 0);
    m_aiService->setThinkingEffort(m_thinkingEffort);
    // 重新生成同样按当前对话模式注入系统提示词
    AssistantMode regenMode = (m_conversations[m_currentConvIndex].mode == 1)
                                  ? AssistantMode::Work
                                  : AssistantMode::Chat;
    // 从对话绑定的提示词 ID 列表中解析自定义系统提示词
    QString regenCustomPrompt;
    const QStringList &regenPromptIds = m_conversations[m_currentConvIndex].systemPromptIds;
    if (!regenPromptIds.isEmpty()) {
        QSettings s;
        QString json = s.value("ai/systemPromptLibrary", "").toString();
        if (!json.isEmpty()) {
            QJsonParseError pe;
            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
            if (pe.error == QJsonParseError::NoError && doc.isArray()) {
                QStringList parts;
                for (const QString &id : regenPromptIds) {
                    for (const QJsonValue &v : doc.array()) {
                        QJsonObject o = v.toObject();
                        if (o["id"].toString() == id) {
                            QString content = o["content"].toString().trimmed();
                            if (!content.isEmpty())
                                parts.append(content);
                            break;
                        }
                    }
                }
                regenCustomPrompt = parts.join(QStringLiteral("\n\n---\n\n"));
            }
        }
    }
    m_aiService->sendMessage(msgs, regenModel, WebSearchMode::Off, regenMode, regenCustomPrompt);
    saveConversations();
}

void AiChatPage::updateBubbleMaxWidths()
{
    // 不再需要：气泡宽度由 Expanding sizePolicy 自然适应 container 宽度
}

// ============================================================================
// 持久化
// ============================================================================

QString AiChatPage::conversationsFilePath() const
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
    return dir.filePath("ai_conversations.json");
}

void AiChatPage::saveConversations()
{
    // 防抖：短时间内多次调用只触发一次真实落盘
    if (m_saveDebounceTimer)
    {
        m_saveDebounceTimer->start();
    }
}

void AiChatPage::doSaveConversations()
{
    QJsonArray convArray;
    for (const Conversation &conv : m_conversations)
    {
        QJsonObject convObj;
        convObj["id"] = conv.id;
        convObj["title"] = conv.title;
        convObj["modelId"] = conv.modelId;
        convObj["createdAt"] = conv.createdAt;
        convObj["mode"] = conv.mode;
        if (!conv.systemPromptIds.isEmpty()) {
            QJsonArray spArray;
            for (const QString &id : conv.systemPromptIds)
                spArray.append(id);
            convObj["systemPromptIds"] = spArray;
        }

        QJsonArray msgArray;
        for (const ChatMessage &msg : conv.messages)
        {
            QJsonObject msgObj;
            msgObj["role"] = msg.role;
            msgObj["content"] = msg.content;
            if (!msg.reasoning.isEmpty())
            {
                msgObj["reasoning"] = msg.reasoning;
            }
            if (!msg.stats.isEmpty())
            {
                msgObj["stats"] = msg.stats;
            }
            // 序列化 Agent 工具调用记录（仅 AI 消息且工具调用列表非空时写入）
            if (!msg.toolCalls.isEmpty())
            {
                QJsonArray tcArray;
                for (const AgentToolCall &tc : msg.toolCalls)
                {
                    QJsonObject tcObj;
                    tcObj["name"] = tc.name;
                    tcObj["arguments"] = tc.arguments;
                    tcObj["result"] = tc.result;
                    tcObj["resultSummary"] = tc.resultSummary;
                    tcObj["durationMs"] = tc.durationMs;
                    tcObj["success"] = tc.success;
                    if (!tc.errorMessage.isEmpty())
                    {
                        tcObj["errorMessage"] = tc.errorMessage;
                    }
                    tcArray.append(tcObj);
                }
                msgObj["toolCalls"] = tcArray;
            }
            // 序列化用户消息携带的引用列表（用于历史消息样式化展示）
            if (!msg.references.isEmpty())
            {
                QJsonArray refArray;
                for (const ResourceReference &ref : msg.references)
                {
                    QJsonObject refObj;
                    refObj["category"] = ref.category;
                    refObj["name"] = ref.name;
                    refObj["path"] = ref.path;
                    refObj["size"] = qint64(ref.size);
                    refObj["extra"] = ref.extra;
                    refArray.append(refObj);
                }
                msgObj["references"] = refArray;
            }
            msgArray.append(msgObj);
        }
        convObj["messages"] = msgArray;

        convArray.append(convObj);
    }

    QJsonDocument doc(convArray);
    QFile file(conversationsFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

void AiChatPage::loadConversations()
{
    QFile file(conversationsFilePath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        return;
    }

    if (!doc.isArray())
    {
        return;
    }

    QJsonArray convArray = doc.array();

    for (const QJsonValue &val : convArray)
    {
        QJsonObject convObj = val.toObject();

        Conversation conv;
        conv.id = convObj["id"].toString();
        conv.title = convObj["title"].toString();
        conv.modelId = convObj["modelId"].toString();
        conv.createdAt = convObj["createdAt"].toString();
        conv.mode = convObj["mode"].toInt(0);  // 兼容旧数据，默认为聊天模式
        // 反序列化系统提示词 ID 列表（向后兼容：旧数据无此字段时为空列表）
        QJsonArray spArray = convObj["systemPromptIds"].toArray();
        for (const QJsonValue &spVal : spArray)
            conv.systemPromptIds.append(spVal.toString());

        QJsonArray msgArray = convObj["messages"].toArray();
        for (const QJsonValue &msgVal : msgArray)
        {
            QJsonObject msgObj = msgVal.toObject();
            ChatMessage msg;
            msg.role = msgObj["role"].toString();
            msg.content = msgObj["content"].toString();
            msg.reasoning = msgObj["reasoning"].toString();
            msg.stats = msgObj["stats"].toString();
            // 反序列化 Agent 工具调用记录（向后兼容：旧数据无此字段时为空列表）
            QJsonArray tcArray = msgObj["toolCalls"].toArray();
            for (const QJsonValue &tcVal : tcArray)
            {
                QJsonObject tcObj = tcVal.toObject();
                AgentToolCall tc;
                tc.name = tcObj["name"].toString();
                tc.arguments = tcObj["arguments"].toString();
                tc.result = tcObj["result"].toString();
                tc.resultSummary = tcObj["resultSummary"].toString();
                tc.durationMs = tcObj["durationMs"].toVariant().toLongLong();
                tc.success = tcObj["success"].toBool(true);
                tc.errorMessage = tcObj["errorMessage"].toString();
                msg.toolCalls.append(tc);
            }
            // 反序列化用户消息携带的引用列表（向后兼容：旧数据无此字段时为空列表）
            QJsonArray refArray = msgObj["references"].toArray();
            for (const QJsonValue &refVal : refArray)
            {
                QJsonObject refObj = refVal.toObject();
                ResourceReference ref;
                ref.category = refObj["category"].toString();
                ref.name = refObj["name"].toString();
                ref.path = refObj["path"].toString();
                ref.size = refObj["size"].toVariant().toLongLong();
                ref.extra = refObj["extra"].toString();
                msg.references.append(ref);
            }
            conv.messages.append(msg);
        }

        addConversation(conv);
    }
}

QString AiChatPage::modelsFilePath() const
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
    return dir.filePath("ai_models.json");
}

void AiChatPage::saveModelSettings()
{
    // 保存到 QSettings（向后兼容）
    QSettings settings;
    QJsonArray qsArray;
    for (const AiModel &model : m_models)
    {
        if (!model.isCustom)
            continue;
        QJsonObject obj;
        obj["id"] = model.id;
        obj["displayName"] = model.displayName;
        obj["apiUrl"] = model.apiUrl;
        obj["apiKey"] = model.apiKey;
        obj["websiteUrl"] = model.websiteUrl;
        obj["supportsThinking"] = model.supportsThinking;
        qsArray.append(obj);
    }
    settings.setValue("ai/customModels",
        QString::fromUtf8(QJsonDocument(qsArray).toJson(QJsonDocument::Compact)));

    // 同时保存到 JSON 文件
    QJsonArray fileArray;
    for (const AiModel &model : m_models)
    {
        QJsonObject obj;
        obj["id"] = model.id;
        obj["displayName"] = model.displayName;
        obj["apiUrl"] = model.apiUrl;
        obj["apiKey"] = model.apiKey;
        obj["websiteUrl"] = model.websiteUrl;
        obj["supportsThinking"] = model.supportsThinking;
        obj["isCustom"] = model.isCustom;
        obj["isLocal"] = model.isLocal;
        if (model.isLocal)
        {
            obj["localTag"] = model.localTag;
        }
        fileArray.append(obj);
    }
    QFile file(modelsFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(fileArray).toJson(QJsonDocument::Indented));
        file.close();
    }
}

void AiChatPage::loadModelSettings()
{
    // 先尝试从 JSON 文件加载
    QFile file(modelsFilePath());
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray data = file.readAll();
        file.close();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isArray())
        {
            QJsonArray arr = doc.array();
            for (const QJsonValue &val : arr)
            {
                QJsonObject obj = val.toObject();
                AiModel model;
                model.id = obj["id"].toString();
                model.displayName = obj["displayName"].toString();
                model.apiUrl = obj["apiUrl"].toString();
                model.apiKey = obj["apiKey"].toString();
                model.websiteUrl = obj["websiteUrl"].toString();
                model.supportsThinking = obj["supportsThinking"].toBool(false);
                model.isCustom = obj["isCustom"].toBool(false);
                model.isLocal = obj["isLocal"].toBool(false);
                if (model.isLocal)
                {
                    model.localTag = obj["localTag"].toString();
                }
                m_models.append(model);
            }
            return;
        }
    }

    // 回退：从 QSettings 加载（向后兼容）
    QSettings settings;
    QString jsonStr = settings.value("ai/customModels").toString();
    if (jsonStr.isEmpty())
    {
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
    {
        return;
    }

    QJsonArray modelsArray = doc.array();
    for (const QJsonValue &val : modelsArray)
    {
        QJsonObject modelObj = val.toObject();
        AiModel model;
        model.id = modelObj["id"].toString();
        model.displayName = modelObj["displayName"].toString();
        model.apiUrl = modelObj["apiUrl"].toString();
        model.apiKey = modelObj["apiKey"].toString();
        model.websiteUrl = modelObj["websiteUrl"].toString();
        model.supportsThinking = modelObj["supportsThinking"].toBool(false);
        model.isCustom = true;
        m_models.append(model);
    }

    // 将 QSettings 中的数据迁移到 JSON 文件
    saveModelSettings();
}

void AiChatPage::refreshLocalModels()
{
    // 异步拉取 Ollama 已下载模型清单，完成后合并到 m_models
    // 服务未运行时静默跳过（保留 m_models 现状）
    static QNetworkAccessManager* sNAM = nullptr;
    if (!sNAM)
    {
        sNAM = new QNetworkAccessManager(this);
    }

    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:11434/api/tags")));
    req.setTransferTimeout(2000);
    QNetworkReply* reply = sNAM->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            return; // 服务未运行，静默跳过
        }

        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject())
        {
            return;
        }
        const QJsonArray arr = doc.object().value("models").toArray();
        if (arr.isEmpty())
        {
            return;
        }

        // 清除旧的本地模型条目，追加最新结果
        for (int i = m_models.size() - 1; i >= 0; --i)
        {
            if (m_models[i].isLocal)
            {
                m_models.removeAt(i);
            }
        }
        for (const QJsonValue& v : arr)
        {
            const QJsonObject m = v.toObject();
            const QString tag = m.value("name").toString();
            if (tag.isEmpty())
            {
                continue;
            }
            AiModel model;
            model.id = "local-" + tag;
            model.id.replace(':', '-');
            model.displayName = QStringLiteral("%1 (%2)").arg(tag, tr("本地"));
            model.apiUrl = QStringLiteral("http://127.0.0.1:11434/v1");
            model.apiKey = QStringLiteral("ollama");
            model.websiteUrl = QStringLiteral("https://ollama.com");
            model.isLocal = true;
            model.localTag = tag;
            m_models.append(model);
        }
        saveModelSettings();
    });
}

// ============================================================================
// 思考强度
// ============================================================================

void AiChatPage::onThinkingEffortChanged(int index)
{
    if (index < 0 || !m_thinkingEffortCombo)
    {
        return;
    }
    m_thinkingEffort = m_thinkingEffortCombo->itemData(index).toInt();
    saveThinkingEffort();
}

void AiChatPage::saveThinkingEffort()
{
    QSettings settings;
    settings.setValue("ai/thinkingEffort", m_thinkingEffort);
}

void AiChatPage::loadThinkingEffort()
{
    if (!m_thinkingEffortCombo)
    {
        return;
    }
    QSettings settings;
    int effort = settings.value("ai/thinkingEffort", 2).toInt();
    if (effort < 0 || effort > 3)
    {
        effort = 2; // 回退到"中"
    }
    m_thinkingEffort = effort;

    // 在下拉框中找到对应项并选中（不触发信号，避免重复保存）
    QSignalBlocker blocker(m_thinkingEffortCombo);
    for (int i = 0; i < m_thinkingEffortCombo->count(); ++i)
    {
        if (m_thinkingEffortCombo->itemData(i).toInt() == effort)
        {
            m_thinkingEffortCombo->setCurrentIndex(i);
            break;
        }
    }
}

// ============================================================================
// 操作权限
// ============================================================================

void AiChatPage::onPermissionChanged(int index)
{
    if (index < 0 || !m_permissionCombo)
    {
        return;
    }
    const int mode = m_permissionCombo->itemData(index).toInt();

    // 切换到"完全访问"时弹居中确认；取消则回退到之前的级别
    if (mode == 2 && m_prevPermissionMode != 2)
    {
        const auto ret = AppMessageBox::question(
            this,
            tr("开启完全访问？"),
            tr("完全访问模式下，AI 将直接执行包括修改/删除文件、下载资源、"
               "更改实例在内的所有操作，不再向你确认。\n\n"
               "仅在你信任当前任务时开启。确定开启吗？"),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);
        if (ret != AppMessageBox::Yes)
        {
            QSignalBlocker blocker(m_permissionCombo);
            m_permissionCombo->setCurrentIndex(
                m_permissionCombo->findData(m_prevPermissionMode));
            return;
        }
    }

    m_permissionMode = mode;
    m_prevPermissionMode = mode;
    if (m_aiService)
    {
        m_aiService->setPermissionMode(mode);
    }
    savePermissionMode();
}

void AiChatPage::savePermissionMode()
{
    QSettings settings;
    settings.setValue("ai/permissionMode", m_permissionMode);
}

void AiChatPage::loadPermissionMode()
{
    if (!m_permissionCombo)
    {
        return;
    }
    QSettings settings;
    int mode = settings.value("ai/permissionMode", 0).toInt();
    if (mode < 0 || mode > 2)
    {
        mode = 0; // 回退到"全部确认"
    }
    m_permissionMode = mode;
    m_prevPermissionMode = mode;

    QSignalBlocker blocker(m_permissionCombo);
    for (int i = 0; i < m_permissionCombo->count(); ++i)
    {
        if (m_permissionCombo->itemData(i).toInt() == mode)
        {
            m_permissionCombo->setCurrentIndex(i);
            break;
        }
    }
}

// ============================================================================
// 工作区选择
// ============================================================================

void AiChatPage::loadWorkspaceFolders()
{
    QSettings settings;

    // 已绑定文件夹列表：首次使用时以启动器已绑定的实例文件夹作为初始清单，
    // 之后独立维护（在 AI 页绑定的新文件夹不影响启动器实例列表）
    if (!settings.contains("ai/workspaceFolders"))
    {
        QStringList seeded;
        const auto folders = SettingsManager::instance()->getInstanceFolders();
        for (const auto &f : folders)
        {
            if (!f.path.isEmpty())
            {
                seeded.append(QFileInfo(f.path).absoluteFilePath());
            }
        }
        settings.setValue("ai/workspaceFolders", seeded);
    }
    m_workspaceBoundFolders = settings.value("ai/workspaceFolders").toStringList();

    // 上次选中的工作区（空 = 不选）
    m_workspaceSelectedPath = settings.value("ai/workspacePath").toString();
    if (!m_workspaceSelectedPath.isEmpty()
        && !m_workspaceBoundFolders.contains(m_workspaceSelectedPath))
    {
        // 选中的文件夹已不在绑定清单中（如被清理），回退为不选
        m_workspaceSelectedPath.clear();
        settings.setValue("ai/workspacePath", QString());
    }
    applyWorkspaceSelection();
    updateWorkspaceBtnText();
}

void AiChatPage::saveWorkspaceSelection()
{
    QSettings settings;
    settings.setValue("ai/workspaceFolders", m_workspaceBoundFolders);
    settings.setValue("ai/workspacePath", m_workspaceSelectedPath);
}

void AiChatPage::applyWorkspaceSelection()
{
    if (!m_aiService)
    {
        return; // initConnections 之前（构造期 loadWorkspaceFolders）仅同步 UI
    }
    // 选中的文件夹映射为单目录白名单；不选则关闭工作区限制
    WorkspaceRestriction restriction;
    restriction.enabled = !m_workspaceSelectedPath.isEmpty();
    if (restriction.enabled)
    {
        restriction.allowedDirectories.append(m_workspaceSelectedPath);
    }
    m_aiService->setWorkspaceRestriction(restriction);
}

void AiChatPage::showWorkspaceMenu()
{
    QMenu menu(m_workspaceBtn);

    QActionGroup group(&menu);
    group.setExclusive(true);

    // 不选：不限制 AI 可访问目录
    QAction *noneAction = menu.addAction(tr("不选（不限制目录）"));
    noneAction->setCheckable(true);
    noneAction->setChecked(m_workspaceSelectedPath.isEmpty());
    group.addAction(noneAction);

    // 已绑定的文件夹
    if (!m_workspaceBoundFolders.isEmpty())
    {
        menu.addSeparator();
        for (const QString &path : m_workspaceBoundFolders)
        {
            const QString name = QDir(path).dirName();
            QAction *action = menu.addAction(
                IconHelper::loadColoredIcon(":/Images/Icons/folder.svg", QColor("#64748b"), 14),
                name.isEmpty() ? path : name);
            action->setCheckable(true);
            action->setChecked(path == m_workspaceSelectedPath);
            action->setToolTip(path);
            group.addAction(action);
            connect(action, &QAction::triggered, this, [this, path]() {
                m_workspaceSelectedPath = path;
                applyWorkspaceSelection();
                saveWorkspaceSelection();
                updateWorkspaceBtnText();
            });
        }
    }

    menu.addSeparator();

    // 绑定新文件夹
    QAction *bindAction = menu.addAction(
        IconHelper::loadColoredIcon(":/Images/Icons/nav_folder_plus.svg", QColor("#64748b"), 14),
        tr("绑定新文件夹..."));
    connect(bindAction, &QAction::triggered, this, [this, &menu]() {
        menu.close();
        QString dir = AppFileDialog::getExistingDirectory(this, tr("选择要绑定的工作区文件夹"),
                                                          QCoreApplication::applicationDirPath());
        if (dir.isEmpty())
        {
            return;
        }
        dir = QFileInfo(dir).absoluteFilePath();
        if (!m_workspaceBoundFolders.contains(dir))
        {
            m_workspaceBoundFolders.append(dir);
        }
        m_workspaceSelectedPath = dir;
        applyWorkspaceSelection();
        saveWorkspaceSelection();
        updateWorkspaceBtnText();
    });

    menu.exec(m_workspaceBtn->mapToGlobal(
        QPoint(0, m_workspaceBtn->height() + 2)));
}

void AiChatPage::updateWorkspaceBtnText()
{
    if (!m_workspaceBtn)
    {
        return;
    }
    const QString prefix = tr("工作区: ");
    const QString text = m_workspaceSelectedPath.isEmpty()
                             ? tr("不选")
                             : QDir(m_workspaceSelectedPath).dirName();
    QFontMetrics fm(m_workspaceBtn->font());
    // 预留左右内边距与"▾"箭头宽度
    const int avail = m_workspaceBtn->width() - 20 - fm.horizontalAdvance(QStringLiteral(" ▾"));
    QString elided = fm.elidedText(text, Qt::ElideMiddle, qMax(20, avail));
    m_workspaceBtn->setText(prefix + elided + QStringLiteral(" ▾"));
    m_workspaceBtn->setToolTip(m_workspaceSelectedPath.isEmpty()
                                   ? tr("限制 AI 可访问的目录范围：可不选，"
                                        "也可选择已绑定的文件夹，或绑定新文件夹。")
                                   : m_workspaceSelectedPath);
    // 选中工作区后高亮（与工具图标按钮的 active 态一致）
    m_workspaceBtn->setProperty("active", !m_workspaceSelectedPath.isEmpty());
    m_workspaceBtn->style()->unpolish(m_workspaceBtn);
    m_workspaceBtn->style()->polish(m_workspaceBtn);
}

// ============================================================================
// 模型/发送按钮外观
// ============================================================================

void AiChatPage::updateModelBtnText()
{
    if (!m_modelBtn)
    {
        return;
    }
    const QString name = m_currentModel.displayName.isEmpty()
                             ? tr("选择模型")
                             : m_currentModel.displayName;
    QFontMetrics fm(m_modelBtn->font());
    // 预留左右内边距与"▾"箭头宽度
    const int avail = m_modelBtn->width() - 30 - fm.horizontalAdvance(QStringLiteral(" ▾"));
    QString elided = fm.elidedText(name, Qt::ElideRight, qMax(20, avail));
    m_modelBtn->setText(elided + QStringLiteral(" ▾"));
    m_modelBtn->setToolTip(name);
}

void AiChatPage::updateSendButtonIcon(bool streaming)
{
    if (!m_sendBtn)
    {
        return;
    }
    if (streaming)
    {
        // 流式中：红色按钮 + 白色方块（中止）
        m_sendBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/stop.svg",
                                                       QColor("#ffffff"), 16));
        m_sendBtn->setToolTip(tr("停止生成"));
        m_sendBtn->setStyleSheet(QStringLiteral(
            "QPushButton#aiChatSendBtn { background: #F44336; color: white; "
            "border: none; border-radius: 10px; }"
            "QPushButton#aiChatSendBtn:hover { background: #D32F2F; }"));
    }
    else
    {
        // 空闲：主题渐变按钮 + 白色纸飞机（发送）
        m_sendBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/send.svg",
                                                       QColor("#ffffff"), 16));
        m_sendBtn->setToolTip(tr("发送"));
        m_sendBtn->setStyleSheet(QString()); // 恢复默认样式
    }
}

// ============================================================================
// 提示词优化
// ============================================================================

void AiChatPage::onPromptOptimizeClicked()
{
    const QString draft = m_inputEdit->toPlainText().trimmed();
    if (draft.isEmpty() || m_promptOptimizeBusy || m_isStreaming || !m_aiService)
    {
        return;
    }
    if (m_currentModel.id.isEmpty())
    {
        return;
    }

    m_promptOptimizeBusy = true;
    m_promptOptimizeBtn->setEnabled(false);
    m_promptOptimizeBtn->setToolTip(tr("优化中…"));
    m_promptOptimizeBtn->setProperty("active", true);
    m_promptOptimizeBtn->style()->unpolish(m_promptOptimizeBtn);
    m_promptOptimizeBtn->style()->polish(m_promptOptimizeBtn);

    m_aiService->optimizePrompt(draft, m_currentModel);
}

void AiChatPage::onPromptOptimized(const QString &optimized)
{
    m_promptOptimizeBusy = false;
    m_promptOptimizeBtn->setToolTip(tr("用 AI 优化输入框中的提示词草稿"));
    m_promptOptimizeBtn->setProperty("active", false);
    m_promptOptimizeBtn->style()->unpolish(m_promptOptimizeBtn);
    m_promptOptimizeBtn->style()->polish(m_promptOptimizeBtn);
    m_promptOptimizeBtn->setEnabled(!m_inputEdit->toPlainText().trimmed().isEmpty());

    if (optimized.isEmpty())
    {
        return; // 优化失败或返回空，保留原草稿
    }
    // 替换输入框内容（Ctrl+Z 可撤销回草稿）
    m_inputEdit->setPlainText(optimized);
    QTextCursor cursor = m_inputEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_inputEdit->setTextCursor(cursor);
}

void AiChatPage::refreshSystemPromptSelector()
{
    m_selectedPromptIds.clear();
    if (m_systemPromptBtn)
    {
        m_systemPromptBtn->setProperty("active", false);
        m_systemPromptBtn->style()->unpolish(m_systemPromptBtn);
        m_systemPromptBtn->style()->polish(m_systemPromptBtn);
        m_systemPromptBtn->setToolTip(tr("从提示词库中选择系统提示词（可多选）"));
    }
}

QString AiChatPage::resolveSystemPrompt() const
{
    if (m_selectedPromptIds.isEmpty())
        return QString();

    QSettings s;
    QString json = s.value("ai/systemPromptLibrary", "").toString();
    if (json.isEmpty()) return QString();

    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isArray()) return QString();

    QStringList parts;
    for (const QString &id : m_selectedPromptIds) {
        for (const QJsonValue &v : doc.array()) {
            QJsonObject o = v.toObject();
            if (o["id"].toString() == id) {
                QString content = o["content"].toString().trimmed();
                if (!content.isEmpty())
                    parts.append(content);
                break;
            }
        }
    }
    return parts.join(QStringLiteral("\n\n---\n\n"));
}

// ============================================================================
// 联网搜索过程行
// ============================================================================

QWidget* AiChatPage::ensureSearchStepCard(const QString &query)
{
    if (m_currentSearchStepCard)
    {
        return m_currentSearchStepCard;
    }

    // 创建「搜索」过程行（进行中态），插入到 AI 气泡之前（尚无气泡则插到 stretch 之前）
    m_currentSearchStepCard = createToolStepCard(tr("搜索"), query, QString(), 0, true, false);
    int insertPos = m_messageLayout->count() - 1;
    if (m_currentAiBubble)
    {
        insertPos = m_messageLayout->indexOf(m_currentAiBubble);
    }
    m_messageLayout->insertWidget(insertPos, m_currentSearchStepCard);
    return m_currentSearchStepCard;
}

void AiChatPage::onWebSearchResultsCollected(const QList<WebSearchResult> &results)
{
    if (results.isEmpty())
    {
        return;
    }

    // 累积到当前回复的搜索结果
    m_currentSearchResults.append(results);

    // 确保「搜索」过程行存在（结果先于 webSearchStarted 到达的兜底）
    QWidget *stepCard = ensureSearchStepCard(results.first().query);

    // 追加站点行到展开态结果区（富文本链接，可直接打开网页）
    if (auto *resultLabel = stepCard->findChild<QLabel*>(QStringLiteral("agentToolStepResult")))
    {
        QString html = resultLabel->property("searchHtml").toString();
        for (const auto &r : results)
        {
            if (!html.isEmpty())
            {
                html += QStringLiteral("<br>");
            }
            if (!r.url.isEmpty())
            {
                html += QStringLiteral("<a href=\"%1\">%2</a>")
                            .arg(r.url.toHtmlEscaped(), r.title.toHtmlEscaped());
            }
            else
            {
                html += r.title.toHtmlEscaped();
            }
        }
        resultLabel->setProperty("searchHtml", html);
        resultLabel->setTextFormat(Qt::RichText);
        resultLabel->setText(html);
    }

    // 自动滚动到底部
    QScrollBar *sb = m_messageArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}

// ============================================================================
// Agent 工具步骤卡片
// ============================================================================

void AiChatPage::onAgentToolCallStarted(const QString &name, const QString &arguments)
{
    // 进入工具调用状态
    updateChatStatus(ChatStatus::CallingTool);

    // 移除打字指示器（工具调用阶段替换为步骤卡片）
    if (m_typingIndicator)
    {
        m_messageLayout->removeWidget(m_typingIndicator);
        m_typingIndicator->deleteLater();
        m_typingIndicator = nullptr;
    }

    // 首次创建步骤容器（位于 AI 气泡之前，透明分组容器，步骤行平铺为纯文字行）
    if (!m_currentAgentStepsWidget)
    {
        m_currentAgentStepsWidget = new QFrame(m_messageContainer);
        m_currentAgentStepsWidget->setObjectName("agentStepsContainer");
        m_currentAgentStepsLayout = new QVBoxLayout(m_currentAgentStepsWidget);
        m_currentAgentStepsLayout->setContentsMargins(0, 0, 0, 0);
        m_currentAgentStepsLayout->setSpacing(2);

        // 插入到 AI 气泡之前（若已存在），否则插到 stretch 之前
        int insertPos = m_messageLayout->count() - 1;
        if (m_currentAiBubble)
        {
            insertPos = m_messageLayout->indexOf(m_currentAiBubble);
        }
        m_messageLayout->insertWidget(insertPos, m_currentAgentStepsWidget);
    }

    // 创建"进行中"步骤卡片
    QWidget *card = createToolStepCard(name, arguments, QString(), 0, true, false);
    m_currentAgentStepsLayout->addWidget(card);
    m_currentToolStepCards.append(card);
    m_currentPendingStepCard = card;

    // 滚动到底部
    QScrollBar *sb = m_messageArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AiChatPage::onAgentToolCallFinished(const QString &name, const QString &resultSummary,
                                         qint64 durationMs, bool success)
{
    // 工具调用结束，恢复思考状态（仍处于流式）
    if (m_isStreaming)
    {
        updateChatStatus(ChatStatus::Thinking);
    }

    // 更新当前进行中卡片为完成态
    finishToolStepCard(name, resultSummary, durationMs, success);

    // 若尚未创建 AI 气泡且无打字指示器，恢复打字指示器等待模型生成
    if (!m_currentAiBubble && !m_typingIndicator)
    {
        m_typingIndicator = createTypingIndicator();
        m_messageLayout->insertWidget(m_messageLayout->count() - 1, m_typingIndicator);
    }

    QScrollBar *sb = m_messageArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AiChatPage::onTaskListUpdated(const QList<AgentTask> &tasks)
{
    // 移除打字指示器（任务清单阶段替换为清单卡片）
    if (m_typingIndicator)
    {
        m_messageLayout->removeWidget(m_typingIndicator);
        m_typingIndicator->deleteLater();
        m_typingIndicator = nullptr;
    }

    rebuildTaskListCard(tasks);

    // 滚动到底部
    QScrollBar *sb = m_messageArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AiChatPage::rebuildTaskListCard(const QList<AgentTask> &tasks)
{
    // 首次创建卡片容器（覆盖式刷新：后续调用复用同一容器，清空内容后重填）
    if (!m_currentTaskListCard)
    {
        auto *card = new QFrame(m_messageContainer);
        card->setObjectName("taskListCard");
        card->setStyleSheet(QStringLiteral(
            "QFrame#taskListCard { background: rgba(33, 150, 243, 0.04); "
            "border: 1px solid rgba(33, 150, 243, 0.2); border-radius: 10px; }"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->setSpacing(8);

        // 插入位置：步骤容器之前（清单位于步骤上方），否则 AI 气泡之前，否则 stretch 之前
        int insertPos = m_messageLayout->count() - 1;
        if (m_currentAgentStepsWidget)
        {
            insertPos = m_messageLayout->indexOf(m_currentAgentStepsWidget);
        }
        else if (m_currentAiBubble)
        {
            insertPos = m_messageLayout->indexOf(m_currentAiBubble);
        }
        m_messageLayout->insertWidget(insertPos, card);
        m_currentTaskListCard = card;
    }

    auto *cardLayout = qobject_cast<QVBoxLayout*>(m_currentTaskListCard->layout());
    if (!cardLayout)
    {
        return;
    }

    // 清空旧内容
    QLayoutItem *item;
    while ((item = cardLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
        {
            item->widget()->deleteLater();
        }
        delete item;
    }

    // 统计已完成数
    int doneCount = 0;
    for (const auto &t : tasks)
    {
        if (t.status == QStringLiteral("completed"))
        {
            ++doneCount;
        }
    }

    // 标题行：📋 任务清单 + 进度文本
    auto *headerRow = new QWidget(m_currentTaskListCard);
    auto *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    auto *titleLabel = new QLabel(
        QStringLiteral("任务清单"), m_currentTaskListCard);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 13px; font-weight: 600; color: #1a1a1a; border: none; background: transparent;"));
    auto *titleIcon = new QLabel(m_currentTaskListCard);
    titleIcon->setPixmap(IconHelper::loadColoredIcon(":/Images/Icons/clipboard.svg",
                                                     QColor("#1a1a1a"), 14).pixmap(14, 14));
    headerLayout->addWidget(titleIcon);
    headerLayout->addWidget(titleLabel);

    // 进度文本徽章
    QString progressText;
    if (tasks.isEmpty())
    {
        progressText = QStringLiteral("0 项");
    }
    else if (doneCount == tasks.size())
    {
        progressText = QStringLiteral("全部完成 ✓");
    }
    else
    {
        progressText = QStringLiteral("%1/%2").arg(doneCount).arg(tasks.size());
    }
    auto *progressBadge = new QLabel(progressText, m_currentTaskListCard);
    progressBadge->setStyleSheet(QStringLiteral(
        "font-size: 11px; font-weight: 600; color: #1565C0; "
        "background: rgba(33, 150, 243, 0.12); border: none; "
        "border-radius: 8px; padding: 1px 8px;"));
    progressBadge->setFixedHeight(18);
    headerLayout->addWidget(progressBadge);
    headerLayout->addStretch(1);
    cardLayout->addWidget(headerRow);

    // 进度条（细条，蓝色填充）
    if (!tasks.isEmpty())
    {
        auto *progressBar = new QFrame(m_currentTaskListCard);
        progressBar->setObjectName("taskListProgressBar");
        progressBar->setFixedHeight(4);
        progressBar->setStyleSheet(QStringLiteral(
            "QFrame#taskListProgressBar { background: rgba(33, 150, 243, 0.15); "
            "border: none; border-radius: 2px; }"));
        auto *barLayout = new QHBoxLayout(progressBar);
        barLayout->setContentsMargins(0, 0, 0, 0);
        barLayout->setSpacing(0);

        int fillPercent = doneCount * 100 / tasks.size();
        auto *fillWidget = new QWidget(progressBar);
        fillWidget->setStyleSheet(QStringLiteral(
            "background: #2196F3; border-radius: 2px;"));
        fillWidget->setFixedHeight(4);
        barLayout->addWidget(fillWidget, fillPercent);
        barLayout->addStretch(100 - fillPercent);
        cardLayout->addWidget(progressBar);
    }

    // 分隔线
    auto *separator = new QFrame(m_currentTaskListCard);
    separator->setFixedHeight(1);
    separator->setStyleSheet(QStringLiteral(
        "background: rgba(33, 150, 243, 0.15); border: none;"));
    cardLayout->addWidget(separator);

    // 各任务行
    for (int i = 0; i < tasks.size(); ++i)
    {
        const auto &task = tasks.at(i);
        auto *row = new QFrame(m_currentTaskListCard);
        row->setObjectName("taskListRow");
        // in_progress 行加浅蓝背景高亮
        if (task.status == QStringLiteral("in_progress"))
        {
            row->setStyleSheet(QStringLiteral(
                "QFrame#taskListRow { background: rgba(33, 150, 243, 0.08); "
                "border: none; border-radius: 6px; }"));
        }
        else
        {
            row->setStyleSheet(QStringLiteral(
                "QFrame#taskListRow { background: transparent; border: none; }"));
        }
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 6, 8, 6);
        rowLayout->setSpacing(10);

        // 状态图标
        QString icon;
        QString iconColor;
        if (task.status == QStringLiteral("completed"))
        {
            icon = QStringLiteral("✓");
            iconColor = QStringLiteral("#4CAF50");
        }
        else if (task.status == QStringLiteral("in_progress"))
        {
            icon = QStringLiteral("◐");
            iconColor = QStringLiteral("#2196F3");
        }
        else
        {
            icon = QStringLiteral("○");
            iconColor = QStringLiteral("#9E9E9E");
        }

        auto *iconLabel = new QLabel(icon, row);
        iconLabel->setStyleSheet(QStringLiteral(
            "font-size: 14px; font-weight: bold; color: %1; border: none; background: transparent;")
            .arg(iconColor));
        iconLabel->setFixedWidth(18);
        iconLabel->setAlignment(Qt::AlignCenter);
        rowLayout->addWidget(iconLabel);

        // 内容
        auto *contentLabel = new QLabel(task.content, row);
        contentLabel->setWordWrap(true);
        QString contentBase = QStringLiteral(
            "font-size: 13px; border: none; background: transparent;");
        if (task.status == QStringLiteral("completed"))
        {
            contentLabel->setStyleSheet(contentBase + QStringLiteral(" color: #9E9E9E;"));
            QFont f = contentLabel->font();
            f.setStrikeOut(true);
            contentLabel->setFont(f);
        }
        else if (task.status == QStringLiteral("in_progress"))
        {
            contentLabel->setStyleSheet(contentBase + QStringLiteral(" color: #1a1a1a; font-weight: 600;"));
        }
        else
        {
            contentLabel->setStyleSheet(contentBase + QStringLiteral(" color: #555;"));
        }
        rowLayout->addWidget(contentLabel, 1);

        // 优先级标签（胶囊式）
        QString prioText;
        QString prioColor;
        QString prioBg;
        if (task.priority == QStringLiteral("high"))
        {
            prioText = QStringLiteral("高");
            prioColor = QStringLiteral("#C62828");
            prioBg = QStringLiteral("rgba(244, 67, 54, 0.1)");
        }
        else if (task.priority == QStringLiteral("medium"))
        {
            prioText = QStringLiteral("中");
            prioColor = QStringLiteral("#E65100");
            prioBg = QStringLiteral("rgba(255, 152, 0, 0.1)");
        }
        else
        {
            prioText = QStringLiteral("低");
            prioColor = QStringLiteral("#616161");
            prioBg = QStringLiteral("rgba(158, 158, 158, 0.1)");
        }
        auto *prioLabel = new QLabel(prioText, row);
        prioLabel->setStyleSheet(QStringLiteral(
            "font-size: 11px; font-weight: 600; color: %1; background: %2; "
            "border: none; border-radius: 10px; padding: 2px 8px;")
            .arg(prioColor, prioBg));
        prioLabel->setFixedHeight(18);
        prioLabel->setAlignment(Qt::AlignCenter);
        rowLayout->addWidget(prioLabel);

        cardLayout->addWidget(row);

        // 任务行之间加细分隔线（最后一行不加）
        if (i < tasks.size() - 1)
        {
            auto *lineSep = new QFrame(m_currentTaskListCard);
            lineSep->setFixedHeight(1);
            lineSep->setStyleSheet(QStringLiteral(
                "background: rgba(0, 0, 0, 0.06); border: none;"));
            cardLayout->addWidget(lineSep);
        }
    }
}

void AiChatPage::onResourceDownloadRequested(const ModInfo &info, const ModVersionFile &file,
                                             const QString &instancePath, ContentType contentType)
{
    // 校验实例路径存在
    if (!QDir(instancePath).exists())
    {
        qWarning() << "[AiChatPage] Instance path does not exist:" << instancePath;
        return;
    }

    // 计算落地路径并确保目录存在
    QString targetPath = ContentDownloader::installPath(instancePath, contentType);
    if (!QDir().mkpath(targetPath))
    {
        qWarning() << "[AiChatPage] Failed to create target directory:" << targetPath;
        return;
    }

    // 文件名兜底（参考 mainwindow.cpp::onContentDownloadRequested 的处理）
    QString fileName = file.fileName;
    if (fileName.isEmpty())
    {
        QUrl url(file.downloadUrl);
        fileName = url.fileName();
        if (fileName.isEmpty() || fileName.contains('?'))
        {
            QString safeName = info.name;
            safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
            QString ver = file.version.isEmpty() ? info.latestVersion : file.version;
            if (!ver.isEmpty())
            {
                safeName += "-" + ver;
            }
            fileName = safeName + ".jar";
        }
    }

    // 注册下载任务
    QString instanceName = QFileInfo(instancePath).fileName();
    QString taskId = DownloadTaskManager::instance()->addTask(
        instanceName, instancePath, QString(), QStringList());

    ContentTypeConfig config = ContentTypeConfig::getConfig(contentType);
    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading,
        QStringLiteral("下载%1: ").arg(config.displayName) + info.name);
    DownloadTaskManager::instance()->updateTaskCurrentFile(taskId, fileName);
    DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::ClientJar, 0);

    // 每次下载创建独立 ContentDownloader，parent=this 随父对象自动清理，
    // 并在下载完成/失败时主动 deleteLater 释放资源
    auto *downloader = new ContentDownloader(this);

    connect(downloader, &ContentDownloader::downloadProgress, this,
        [taskId, fileName](qint64 received, qint64 total)
        {
            DownloadTaskManager::instance()->updateTaskFileProgress(taskId, fileName, received, total);
            if (total > 0)
            {
                int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
            }
        });

    connect(downloader, &ContentDownloader::downloadFinished, this,
        [taskId, downloader](const QString &)
        {
            DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::Completed, 100);
            DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
            DownloadTaskManager::instance()->updateTaskStatus(
                taskId, DownloadTaskStatus::Completed, QStringLiteral("下载完成"));
            downloader->deleteLater();
        });

    connect(downloader, &ContentDownloader::downloadFailed, this,
        [taskId, downloader](const QString &error)
        {
            DownloadTaskManager::instance()->updateTaskStatus(
                taskId, DownloadTaskStatus::Failed, error);
            downloader->deleteLater();
        });

    downloader->downloadToInstance(file.downloadUrl, fileName, instancePath, contentType);
}

void AiChatPage::onInstanceDownloadRequested(const QString &versionId, const QString &instancePath,
                                              const QString &instanceName, const QString &loader,
                                              const QString &loaderVersion, const QString &source)
{
    triggerInstanceDownload(versionId, instancePath, instanceName, loader, loaderVersion, source);
}

void AiChatPage::onInstanceModifyRequested(const QString &instancePath, const QString &newVersion,
                                            const QString &loader, const QString &loaderVersion,
                                            const QString &source)
{
    // 实例修改：用新版本号下载到同实例路径（无 instanceName，复用路径末段）
    if (newVersion.isEmpty())
    {
        // 仅更换加载器：无法在此自动完成（需先有原版），提示用户
        NotificationManager::showError(this,
            tr("修改实例：仅更换加载器需要先确保实例已有对应游戏版本，请通过实例管理页操作"));
        return;
    }
    QString name = QFileInfo(instancePath).fileName();
    triggerInstanceDownload(newVersion, instancePath, name, loader, loaderVersion, source);
}

void AiChatPage::triggerInstanceDownload(const QString &versionId, const QString &instancePath,
                                          const QString &instanceName, const QString &loader,
                                          const QString &loaderVersion, const QString &source)
{
    // 设置下载源
    VersionDownloader::DownloadSource src = (source == QStringLiteral("official"))
        ? VersionDownloader::Official : VersionDownloader::BMCL;
    VersionDownloader::instance()->setDownloadSource(src);

    // 断开旧连接避免重复（用 this 作为接收方上下文）
    disconnect(VersionDownloader::instance(), &VersionDownloader::downloadCompleted, this, nullptr);
    disconnect(VersionDownloader::instance(), &VersionDownloader::downloadFailed, this, nullptr);

    // 准备加载器安装信息（在 downloadCompleted 回调中使用）
    QString ldType = loader.toLower();
    QString ldVer = loaderVersion;
    bool hasLoader = !ldType.isEmpty() && ldType != QStringLiteral("none") && !ldVer.isEmpty();
    QString mcVer = versionId;
    QString instPath = instancePath;
    QString instName = instanceName;

    // 下载完成回调：若提供了完整加载器信息（类型+版本），触发加载器安装
    connect(VersionDownloader::instance(), &VersionDownloader::downloadCompleted,
        this, [this, ldType, ldVer, mcVer, instPath, instName]
        (const QString &verId, const QString &actualVersionPath) {
            Q_UNUSED(verId);
            if (ldType.isEmpty() || ldType == QStringLiteral("none"))
            {
                NotificationManager::showSuccess(this,
                    tr("Minecraft %1 已下载完成\r\n%2").arg(mcVer, actualVersionPath));
                return;
            }

            // 触发加载器安装（loaderVersion 必须提供，否则提示用户手动安装）
            if (ldVer.isEmpty())
            {
                NotificationManager::showInfo(this,
                    tr("原版已下载完成。加载器 %1 未指定版本，请到实例管理页手动安装").arg(ldType));
                return;
            }

            NotificationManager::showInfo(this,
                tr("原版已下载完成，正在安装加载器 %1 %2...").arg(ldType, ldVer));

            // 按加载器类型分发安装
            if (ldType == QStringLiteral("fabric") || ldType == QStringLiteral("quilt")
                || ldType == QStringLiteral("legacyfabric"))
            {
                FabricInstaller::instance()->downloadFabricInstaller(mcVer, ldVer, instPath);
            }
            else if (ldType == QStringLiteral("forge"))
            {
                ForgeInstaller::instance()->setVersionIsolationEnabled(
                    SettingsManager::instance()->isVersionIsolationEnabled());
                ForgeInstaller::instance()->downloadForgeInstaller(mcVer, ldVer, instPath, instName);
            }
            else if (ldType == QStringLiteral("neoforge"))
            {
                NeoForgeInstaller::instance()->setDownloadSource(
                    SettingsManager::instance()->getNeoForgeDownloadSource());
                NeoForgeInstaller::instance()->downloadNeoForgeInstaller(mcVer, ldVer, instPath, instName);
            }
            else if (ldType == QStringLiteral("optifine"))
            {
                // OptiFine 需要 installerFileName，用标准命名
                QString installerFileName = QStringLiteral("OptiFine_%1_%2.jar").arg(mcVer, ldVer);
                OptiFineInstaller::instance()->downloadOptiFineInstaller(
                    mcVer, ldVer, installerFileName, instPath);
            }
            else
            {
                NotificationManager::showError(this,
                    tr("不支持的加载器类型: %1").arg(ldType));
            }
        });

    connect(VersionDownloader::instance(), &VersionDownloader::downloadFailed,
        this, [this](const QString &error) {
            NotificationManager::showError(this, error);
        });

    // 触发原版下载
    QStringList loaderMeta;
    if (hasLoader)
    {
        loaderMeta << QStringLiteral("%1 %2").arg(ldType, ldVer);
    }
    VersionDownloader::instance()->downloadVanilla(versionId, instancePath, instanceName, loaderMeta);

    NotificationManager::showInfo(this,
        tr("开始下载 Minecraft %1 到\r\n%2").arg(versionId, instancePath));
}

void AiChatPage::onUserQuestionAsked(const QString &question, const QStringList &options,
                                      const QString &defaultValue, bool allowOther)
{
    // 创建提问对话框（模态，居中于父窗口）
    QDialog dialog(this);
    dialog.setWindowTitle(tr("AI 提问"));
    dialog.setModal(true);
    dialog.setMinimumWidth(420);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    // 问题文本
    QLabel *questionLabel = new QLabel(question, &dialog);
    questionLabel->setWordWrap(true);
    questionLabel->setStyleSheet(QStringLiteral("font-size: 14px; color: #333;"));
    layout->addWidget(questionLabel);

    // 输入框（无选项或允许其他时显示）
    QLineEdit *inputEdit = nullptr;
    if (options.isEmpty() || allowOther)
    {
        inputEdit = new QLineEdit(&dialog);
        inputEdit->setText(defaultValue);
        inputEdit->setPlaceholderText(tr("在此输入您的回答..."));
        layout->addWidget(inputEdit);
    }

    // 选项按钮（有选项时显示）
    if (!options.isEmpty())
    {
        QFrame *btnFrame = new QFrame(&dialog);
        QVBoxLayout *btnLayout = new QVBoxLayout(btnFrame);
        btnLayout->setContentsMargins(0, 0, 0, 0);
        btnLayout->setSpacing(8);

        for (const QString &opt : options)
        {
            QPushButton *btn = new QPushButton(opt, btnFrame);
            btn->setCursor(Qt::PointingHandCursor);
            const bool isAiDark = (ThemeManager::instance()->currentTheme()
                                   == ThemeManager::DarkTheme);
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { padding: 8px 12px; text-align: left; "
                "border: 1px solid %1; border-radius: 6px; background: %2; }"
                "QPushButton:hover { background: %3; border-color: %4; }")
                .arg(isAiDark ? "#4a4a4a" : "#ddd",
                     isAiDark ? "#2e2e32" : "#fafafa",
                     isAiDark ? "#3d4a66" : "#e8f0fe",
                     isAiDark ? "#5a93e8" : "#4a90e2"));
            btnLayout->addWidget(btn);

            connect(btn, &QPushButton::clicked, &dialog, [this, opt, &dialog]() {
                dialog.accept();
                m_aiService->provideUserAnswer(opt);
            });
        }

        // 如果同时有输入框，添加"使用输入内容"按钮
        if (inputEdit != nullptr)
        {
            QPushButton *useInputBtn = new QPushButton(tr("使用上方输入的文本"), btnFrame);
            useInputBtn->setCursor(Qt::PointingHandCursor);
            const bool isAiDarkUse = (ThemeManager::instance()->currentTheme()
                                      == ThemeManager::DarkTheme);
            useInputBtn->setStyleSheet(QStringLiteral(
                "QPushButton { padding: 8px 12px; text-align: center; "
                "border: 1px solid %1; border-radius: 6px; "
                "background: %2; color: %1; font-weight: bold; }"
                "QPushButton:hover { background: %3; }")
                .arg(isAiDarkUse ? "#5a93e8" : "#4a90e2",
                     isAiDarkUse ? "#2b3a53" : "#e8f0fe",
                     isAiDarkUse ? "#35476a" : "#d0e0fc"));
            btnLayout->addWidget(useInputBtn);
            connect(useInputBtn, &QPushButton::clicked, &dialog, [this, inputEdit, &dialog]() {
                QString text = inputEdit->text().trimmed();
                dialog.accept();
                m_aiService->provideUserAnswer(text);
            });
        }

        layout->addWidget(btnFrame);
    }

    // 底部按钮栏（仅输入框场景需要确定/取消）
    if (inputEdit != nullptr && options.isEmpty())
    {
        QDialogButtonBox *buttonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttonBox->button(QDialogButtonBox::Ok)->setText(tr("确定"));
        buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
        layout->addWidget(buttonBox);

        connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [this, inputEdit, &dialog]() {
            QString text = inputEdit->text().trimmed();
            dialog.accept();
            m_aiService->provideUserAnswer(text);
        });
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        // 回车提交
        connect(inputEdit, &QLineEdit::returnPressed, buttonBox,
                &QDialogButtonBox::accepted);
    }

    // 对话框被关闭（X 按钮或 Cancel）统一视为取消，回填空答案
    connect(&dialog, &QDialog::rejected, this, [this]() {
        m_aiService->provideUserAnswer(QString());
    });

    dialog.exec();
}

void AiChatPage::onTitleGenerated(const QString &title)
{
    // 通过 m_pendingTitleConvIndex 定位到请求标题时的对话，
    // 避免用户在标题返回前切换对话导致错写到当前对话。
    int idx = m_pendingTitleConvIndex;
    m_pendingTitleConvIndex = -1;

    if (idx < 0 || idx >= m_conversations.size())
    {
        return;
    }

    // 仅当目标对话标题仍为"新对话"时替换，避免覆盖用户中途的手动重命名
    if (m_conversations.at(idx).title != tr("新对话"))
    {
        return;
    }

    updateConversationTitle(idx, title);
    saveConversations();
}

void AiChatPage::onSkillManage()
{
    // 打开技能管理对话框（模态），关闭后技能列表变化由 SkillManager::skillsChanged 自动刷新
    if (m_skillBtn)
    {
        m_skillBtn->setProperty("active", true);
        m_skillBtn->style()->polish(m_skillBtn);
    }
    SkillManagerDialog dlg(this);
    dlg.exec();
    if (m_skillBtn)
    {
        m_skillBtn->setProperty("active", false);
        m_skillBtn->style()->polish(m_skillBtn);
    }
}

void AiChatPage::onContextCompressionStarted()
{
    // 进入压缩上下文状态
    updateChatStatus(ChatStatus::CompressingContext);

    // 压缩期间禁用输入栏，避免用户继续发送导致状态混乱
    m_inputEdit->setEnabled(false);

    // 在消息区显示"正在压缩上下文..."提示（复用打字指示器位置）
    if (m_typingIndicator)
    {
        m_messageLayout->removeWidget(m_typingIndicator);
        m_typingIndicator->deleteLater();
        m_typingIndicator = nullptr;
    }
    m_typingIndicator = createTypingIndicator();
    // createTypingIndicator 默认是三点动画，这里复用即可；
    // 若希望显示文字提示，可改为一个 QLabel，但为保持 UI 一致性沿用动画指示器
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, m_typingIndicator);

    QScrollBar *sb = m_messageArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AiChatPage::onContextCompressionFinished(int originalTokens, int compressedTokens, bool success)
{
    // 压缩结束，恢复思考状态（仍处于流式）
    if (m_isStreaming)
    {
        updateChatStatus(ChatStatus::Thinking);
    }

    // 恢复输入栏
    m_inputEdit->setEnabled(true);

    // 移除压缩提示（打字指示器），后续流式请求会重新创建
    if (m_typingIndicator)
    {
        m_messageLayout->removeWidget(m_typingIndicator);
        m_typingIndicator->deleteLater();
        m_typingIndicator = nullptr;
    }

    if (success)
    {
        // 从 AiService 取回压缩后的历史，更新当前对话并持久化
        // 避免下次发送时重复触发压缩
        if (m_currentConvIndex >= 0 && m_currentConvIndex < m_conversations.size())
        {
            m_conversations[m_currentConvIndex].messages = m_aiService->pendingHistory();
            saveConversations();
        }
        qDebug() << "[AiChatPage] Context compressed:" << originalTokens
                 << "->" << compressedTokens << "tokens";
    }
    else
    {
        qWarning() << "[AiChatPage] Context compression failed, continuing with original history";
    }
}

QWidget* AiChatPage::createToolStepCard(const QString &name, const QString &arguments,
                                        const QString &resultSummary, qint64 durationMs,
                                        bool success, bool finished)
{
    auto *card = new QFrame(m_messageContainer);
    card->setObjectName("agentToolStepCard");
    card->setProperty("toolName", name);
    card->setProperty("finished", finished);
    card->setProperty("success", success);

    // 卡片状态：通过 QSS [status="..."] 选择器控制文字颜色
    QString cardStatus = finished
        ? (success ? QStringLiteral("success") : QStringLiteral("danger"))
        : QStringLiteral("pending");
    card->setProperty("status", cardStatus);
    card->style()->polish(card);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // 单行标题：SVG 折叠箭头 + 名称 + 状态 + 耗时 + 摘要（整行可点击展开/收起）
    auto *headerRow = new QPushButton(card);
    headerRow->setObjectName("agentToolStepHeaderRow");
    headerRow->setCursor(Qt::PointingHandCursor);
    headerRow->setFlat(true);
    auto *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(2, 2, 2, 2);
    headerLayout->setSpacing(6);

    auto *chevronLabel = new QLabel(headerRow);
    chevronLabel->setObjectName("agentToolStepChevron");
    chevronLabel->setFixedSize(kChevronSize, kChevronSize);
    chevronLabel->setAlignment(Qt::AlignCenter);
    setChevronPixmap(chevronLabel, false);
    headerLayout->addWidget(chevronLabel);

    // 解析参数 JSON（用于文件编辑行显示与运行中摘要）
    QJsonObject argsObj;
    {
        QJsonParseError argsErr;
        QJsonDocument argsDoc = QJsonDocument::fromJson(arguments.toUtf8(), &argsErr);
        if (argsErr.error == QJsonParseError::NoError && argsDoc.isObject())
        {
            argsObj = argsDoc.object();
        }
    }

    // 步骤名（加粗）：文件编辑类工具显示文件名，其余显示中文工具名
    QString displayName = friendlyToolName(name);
    if (isFileEditToolName(name))
    {
        const QString filePath = extractEditedFilePath(argsObj);
        if (!filePath.isEmpty())
        {
            displayName = QFileInfo(filePath).fileName();
        }
    }
    auto *nameLabel = new QLabel(displayName, headerRow);
    nameLabel->setObjectName("agentToolStepName");
    headerLayout->addWidget(nameLabel);

    // 文件编辑行：文件名后显示行级增删统计（+N 绿 / -M 红）
    if (isFileEditToolName(name))
    {
        const auto stat = computeFileDiffStat(argsObj);
        if (stat.first > 0)
        {
            auto *addLabel = new QLabel(QStringLiteral("+%1").arg(stat.first), headerRow);
            addLabel->setObjectName("agentToolStepDiffAdd");
            headerLayout->addWidget(addLabel);
        }
        if (stat.second > 0)
        {
            auto *delLabel = new QLabel(QStringLiteral("-%1").arg(stat.second), headerRow);
            delLabel->setObjectName("agentToolStepDiffDel");
            headerLayout->addWidget(delLabel);
        }
    }

    // 状态文字（执行中 / 成功 / 失败，颜色由 QSS 控制）
    QString statusText = finished ? (success ? tr("成功") : tr("失败")) : tr("执行中…");
    QString badgeType = finished
        ? (success ? QStringLiteral("success") : QStringLiteral("danger"))
        : QStringLiteral("info");
    auto *statusLabel = new QLabel(statusText, headerRow);
    statusLabel->setObjectName("agentToolStepStatusText");
    statusLabel->setProperty("type", badgeType);
    statusLabel->style()->polish(statusLabel);
    headerLayout->addWidget(statusLabel);

    // 耗时
    if (finished && durationMs > 0)
    {
        auto *durationLabel = new QLabel(formatStepDuration(durationMs), headerRow);
        durationLabel->setObjectName("agentToolStepDuration");
        headerLayout->addWidget(durationLabel);
    }

    // 行尾摘要：完成态显示结果摘要，运行中显示参数摘要（次要色，单行）
    QString lineSummary = resultSummary;
    if (lineSummary.isEmpty() && !finished)
    {
        if (!argsObj.isEmpty())
        {
            lineSummary = toolArgsDigest(argsObj);
        }
        else if (!arguments.isEmpty() && !arguments.trimmed().startsWith(QLatin1Char('{')))
        {
            lineSummary = arguments; // 非 JSON 参数（如搜索查询词）直接显示
        }
    }
    if (!lineSummary.isEmpty())
    {
        auto *summaryLabel = new QLabel(flattenSummary(lineSummary), headerRow);
        summaryLabel->setObjectName("agentToolStepSummary");
        headerLayout->addWidget(summaryLabel, 1);
    }

    layout->addWidget(headerRow);

    // 展开内容（默认收起，缩进纯文字）：参数原文、结果原文
    auto *detailsWidget = new QFrame(card);
    detailsWidget->setObjectName("agentToolStepDetails");
    detailsWidget->setVisible(false);
    auto *detailsLayout = new QVBoxLayout(detailsWidget);
    detailsLayout->setContentsMargins(20, 2, 6, 4);
    detailsLayout->setSpacing(4);

    if (!arguments.isEmpty())
    {
        auto *argsLabel = new QLabel(arguments, detailsWidget);
        argsLabel->setObjectName("agentToolStepArgs");
        argsLabel->setWordWrap(true);
        argsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        argsLabel->setTextFormat(Qt::PlainText);
        detailsLayout->addWidget(argsLabel);
    }

    // 结果原文（进行中态留空占位，applyToolStepFinish 填充）
    auto *resultLabel = new QLabel(detailsWidget);
    resultLabel->setObjectName("agentToolStepResult");
    resultLabel->setWordWrap(true);
    resultLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    resultLabel->setOpenExternalLinks(true);
    resultLabel->setTextFormat(Qt::PlainText);
    resultLabel->setProperty("status", success ? QStringLiteral("success") : QStringLiteral("danger"));
    resultLabel->style()->polish(resultLabel);
    if (finished)
    {
        resultLabel->setText(resultSummary.isEmpty() ? tr("（无）") : resultSummary);
    }
    detailsLayout->addWidget(resultLabel);

    layout->addWidget(detailsWidget);

    // 点击标题行切换详情可见性
    connect(headerRow, &QPushButton::clicked, card, [card, detailsWidget]() {
        const bool visible = !detailsWidget->isVisible();
        detailsWidget->setVisible(visible);
        setChevronPixmap(card->findChild<QLabel*>(QStringLiteral("agentToolStepChevron")), visible);
    });

    return card;
}

void AiChatPage::finishToolStepCard(const QString &name, const QString &resultSummary,
                                    qint64 durationMs, bool success)
{
    Q_UNUSED(name); // 名称已存于 card property，无需使用参数

    if (!m_currentPendingStepCard)
    {
        return;
    }

    QWidget *card = m_currentPendingStepCard;
    m_currentPendingStepCard = nullptr;
    applyToolStepFinish(card, resultSummary, durationMs, success, true);
}

void AiChatPage::applyToolStepFinish(QWidget *card, const QString &resultSummary,
                                     qint64 durationMs, bool success, bool updateResultText)
{
    if (!card)
    {
        return;
    }

    // 更新卡片状态属性与样式
    card->setProperty("finished", true);
    card->setProperty("success", success);
    card->setProperty("status", success ? QStringLiteral("success") : QStringLiteral("danger"));
    card->style()->polish(card);

    auto *headerRow = card->findChild<QWidget*>(QStringLiteral("agentToolStepHeaderRow"));
    if (!headerRow)
    {
        return;
    }
    auto *hLayout = qobject_cast<QHBoxLayout*>(headerRow->layout());

    // 状态文字：成功 / 失败
    if (auto *statusLabel = card->findChild<QLabel*>(QStringLiteral("agentToolStepStatusText")))
    {
        statusLabel->setText(success ? tr("成功") : tr("失败"));
        statusLabel->setProperty("type", success ? QStringLiteral("success") : QStringLiteral("danger"));
        statusLabel->style()->polish(statusLabel);
    }

    // 耗时（已存在则更新，否则插在状态文字之后）
    if (durationMs > 0)
    {
        auto *durationLabel = card->findChild<QLabel*>(QStringLiteral("agentToolStepDuration"));
        if (!durationLabel)
        {
            durationLabel = new QLabel(headerRow);
            durationLabel->setObjectName(QStringLiteral("agentToolStepDuration"));
            if (hLayout)
            {
                int insertIdx = hLayout->count();
                if (auto *statusLabel = card->findChild<QLabel*>(QStringLiteral("agentToolStepStatusText")))
                {
                    insertIdx = hLayout->indexOf(statusLabel) + 1;
                }
                hLayout->insertWidget(insertIdx, durationLabel);
            }
        }
        durationLabel->setText(formatStepDuration(durationMs));
    }

    // 更新或插入行尾摘要
    if (!resultSummary.isEmpty())
    {
        auto *summaryLabel = card->findChild<QLabel*>(QStringLiteral("agentToolStepSummary"));
        if (!summaryLabel)
        {
            summaryLabel = new QLabel(headerRow);
            summaryLabel->setObjectName(QStringLiteral("agentToolStepSummary"));
            if (hLayout)
            {
                hLayout->addWidget(summaryLabel, 1);
            }
        }
        summaryLabel->setText(flattenSummary(resultSummary));
    }

    // 填充结果原文到展开态结果区（「搜索」行传 updateResultText=false，保留站点列表）
    if (updateResultText)
    {
        if (auto *resultLabel = card->findChild<QLabel*>(QStringLiteral("agentToolStepResult")))
        {
            resultLabel->setProperty("status", success ? QStringLiteral("success") : QStringLiteral("danger"));
            resultLabel->style()->polish(resultLabel);
            resultLabel->setText(resultSummary.isEmpty() ? tr("（无）") : resultSummary);
        }
    }
}

void AiChatPage::rebuildToolStepCards(const QList<AgentToolCall> &toolCalls)
{
    if (toolCalls.isEmpty())
    {
        return;
    }

    // 创建步骤容器（透明，仅作布局分组，步骤行直接平铺在消息流中）
    m_currentAgentStepsWidget = new QFrame(m_messageContainer);
    m_currentAgentStepsWidget->setObjectName("agentStepsContainer");
    m_currentAgentStepsLayout = new QVBoxLayout(m_currentAgentStepsWidget);
    m_currentAgentStepsLayout->setContentsMargins(0, 0, 0, 0);
    m_currentAgentStepsLayout->setSpacing(2);

    // 历史卡片直接以完成态创建
    for (const AgentToolCall &tc : toolCalls)
    {
        QWidget *card = createToolStepCard(tc.name, tc.arguments, tc.resultSummary,
                                           tc.durationMs, tc.success, true);
        // 填充完整结果文本
        auto *resultLabel = card->findChild<QLabel*>("agentToolStepResult");
        if (resultLabel)
        {
            resultLabel->setText(tc.result.isEmpty() ? tr("（无）") : tc.result);
        }
        // 失败时追加错误信息
        if (!tc.success && !tc.errorMessage.isEmpty())
        {
            auto *errLabel = new QLabel(tr("错误：%1").arg(tc.errorMessage), card);
            card->findChild<QWidget*>("agentToolStepDetails")->layout()->addWidget(errLabel);
        }
        m_currentAgentStepsLayout->addWidget(card);
    }

    // 插入到 stretch 之前
    m_messageLayout->insertWidget(m_messageLayout->count() - 1, m_currentAgentStepsWidget);

    // 重置（历史加载不保留这些指针，避免被 onStreamFinished 误清）
    m_currentAgentStepsWidget = nullptr;
    m_currentAgentStepsLayout = nullptr;
}

// ============================
// 资源引用功能实现
// ============================

namespace
{
    /// 格式化文件大小为可读字符串（与 ResourceReferenceDialog 保持一致）
    QString formatRefSize(qint64 bytes)
    {
        if (bytes < 1024)
        {
            return QString::number(bytes) + QStringLiteral(" B");
        }
        if (bytes < 1024 * 1024)
        {
            return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
        }
        if (bytes < 1024 * 1024 * 1024)
        {
            return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
        }
        return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + QStringLiteral(" GB");
    }
}

void AiChatPage::setInstanceContext(const QString &path, const QString &version, const QString &loader)
{
    m_instancePath = path;
    m_instanceVersion = version;
    m_instanceLoader = loader;
}

void AiChatPage::onReferenceClicked()
{
    // 引用窗口使用专用的资源引用对话框（实例资源 / 本地资源 / 网络资源 / 网页），
    // 支持引用文件夹（存档/世界）、当前实例、任意文件等目录型资源。
    ResourceReferenceDialog dlg(m_instancePath, m_instanceVersion, m_instanceLoader, this);
    if (dlg.exec() == QDialog::Accepted)
    {
        const QList<ResourceReference> selected = dlg.selectedReferences();
        if (selected.isEmpty())
        {
            return;
        }
        // 合并到待发送引用列表（去重）
        for (const ResourceReference &ref : selected)
        {
            if (!m_pendingReferences.contains(ref))
            {
                m_pendingReferences.append(ref);
            }
        }
        rebuildReferenceChips();
    }
}

void AiChatPage::rebuildReferenceChips()
{
    // 清空现有标签条（保留尾部 stretch）
    while (m_referenceChipsLayout->count() > 1)
    {
        QLayoutItem *item = m_referenceChipsLayout->takeAt(0);
        if (item->widget())
        {
            item->widget()->deleteLater();
        }
        delete item;
    }

    // 无引用时隐藏标签条
    m_referenceChipsWidget->setVisible(!m_pendingReferences.isEmpty());

    // 标号从 1 开始，资源引用与 AI 回答引用统一连续编号
    for (int i = 0; i < m_pendingReferences.size(); ++i)
    {
        addReferenceChip(m_pendingReferences.at(i), i);
    }
}

void AiChatPage::addReferenceChip(const ResourceReference &ref, int index)
{
    auto *chip = new QFrame(m_referenceChipsWidget);
    chip->setObjectName(QStringLiteral("aiChatRefChip"));
    chip->setProperty("chipCategory", ref.category);

    auto *chipLayout = new QHBoxLayout(chip);
    chipLayout->setContentsMargins(8, 2, 4, 2);
    chipLayout->setSpacing(4);

    // 标号 1-based：[N] [类别] 名称
    auto *label = new QLabel(QStringLiteral("[%1] [%2] %3")
                                 .arg(index + 1)
                                 .arg(ref.category, ref.name), chip);
    label->setObjectName(QStringLiteral("aiChatRefChipLabel"));
    // AI 回答引用无 path，用 extra（完整引用文字）作为 tooltip
    const QString tipText = (ref.category == QStringLiteral("AI回答"))
                                ? ref.extra
                                : ref.path;
    label->setToolTip(tipText);
    chipLayout->addWidget(label);

    // 关闭按钮绑定当前引用索引
    auto *closeBtn = new QPushButton(QStringLiteral("×"), chip);
    closeBtn->setObjectName(QStringLiteral("aiChatRefChipClose"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFlat(true);
    closeBtn->setFixedSize(18, 18);
    closeBtn->setProperty("refIndex", index);
    connect(closeBtn, &QPushButton::clicked, this, [this, closeBtn]() {
        bool ok = false;
        const int idx = closeBtn->property("refIndex").toInt(&ok);
        if (ok)
        {
            removeReferenceAt(idx);
        }
    });
    chipLayout->addWidget(closeBtn);

    // 插入到 stretch 之前
    m_referenceChipsLayout->insertWidget(m_referenceChipsLayout->count() - 1, chip);
}

QWidget* AiChatPage::buildReadOnlyRefChips(const QList<ResourceReference> &refs)
{
    auto *widget = new QWidget();
    widget->setObjectName(QStringLiteral("aiChatBubbleRefChips"));
    // 使用 FlowLayout 自动换行，适配气泡宽度
    auto *flow = new FlowLayout(widget, 0, 4, 4);

    for (int i = 0; i < refs.size(); ++i)
    {
        const ResourceReference &ref = refs.at(i);
        auto *chip = new QFrame(widget);
        chip->setObjectName(QStringLiteral("aiChatRefChip"));
        chip->setProperty("chipCategory", ref.category);

        auto *chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(8, 2, 8, 2);
        chipLayout->setSpacing(4);

        // 标号 1-based：[N] [类别] 名称
        auto *label = new QLabel(QStringLiteral("[%1] [%2] %3")
                                     .arg(i + 1)
                                     .arg(ref.category, ref.name), chip);
        label->setObjectName(QStringLiteral("aiChatRefChipLabel"));
        // AI 回答引用无 path，用 extra（完整引用文字）作为 tooltip
        const QString tipText = (ref.category == QStringLiteral("AI回答"))
                                    ? ref.extra
                                    : ref.path;
        label->setToolTip(tipText);
        chipLayout->addWidget(label);

        flow->addWidget(chip);
    }
    return widget;
}

void AiChatPage::removeReferenceAt(int index)
{
    if (index < 0 || index >= m_pendingReferences.size())
    {
        return;
    }
    m_pendingReferences.removeAt(index);
    rebuildReferenceChips();
}

void AiChatPage::clearPendingReferences()
{
    if (m_pendingReferences.isEmpty())
    {
        return;
    }
    m_pendingReferences.clear();
    rebuildReferenceChips();
}

void AiChatPage::addQuoteReference(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    ResourceReference ref;
    ref.category = QStringLiteral("AI回答");
    // name 保存截断预览（前 30 字符 + 省略号），便于标签条展示
    const int previewLen = 30;
    ref.name = (trimmed.length() > previewLen)
                   ? trimmed.left(previewLen) + QStringLiteral("…")
                   : trimmed;
    // extra 保存完整引用文字，AI 可见，用于上下文
    ref.extra = trimmed;

    m_pendingReferences.append(ref);
    rebuildReferenceChips();
}

QString AiChatPage::buildReferenceText() const
{
    if (m_pendingReferences.isEmpty())
    {
        return QString();
    }

    // 统一连续编号（1-based），AI 可见标号，便于用户与 AI 交互时引用
    QString text = QStringLiteral("📎 引用资源（标号 [N] 供后续指代使用）:");
    for (int i = 0; i < m_pendingReferences.size(); ++i)
    {
        const ResourceReference &ref = m_pendingReferences.at(i);
        const QString num = QString::number(i + 1);

        // AI 回答引用：仅发送完整内容，避免与 name 预览重复
        //（name 仅用于 UI 标签条展示截断预览，不参与发送）
        if (ref.category == QStringLiteral("AI回答"))
        {
            text += QStringLiteral("\n[%1] [%2] 引用内容：「%3」")
                        .arg(num, ref.category, ref.extra);
            continue;
        }

        QString line = QStringLiteral("\n[%1] [%2] %3").arg(num, ref.category, ref.name);
        // 区分本地文件与网络/网页引用的描述格式
        const bool isNetworkOrWeb = (ref.category.startsWith(QStringLiteral("网络"))
                                     || ref.category == QStringLiteral("网页"));
        QStringList parts;
        if (ref.size > 0)
        {
            // 网络资源 size 字段存的是下载量，本地文件存的是字节数
            parts << (isNetworkOrWeb
                          ? tr("下载量: %1").arg(ref.size)
                          : formatRefSize(ref.size));
        }
        if (!ref.extra.isEmpty())
        {
            parts << ref.extra;
        }
        parts << (isNetworkOrWeb
                      ? QStringLiteral("链接: %1").arg(ref.path)
                      : QStringLiteral("路径: %1").arg(ref.path));
        text += line + QStringLiteral(" (") + parts.join(QStringLiteral(", ")) + QStringLiteral(")");
    }
    return text;
}
