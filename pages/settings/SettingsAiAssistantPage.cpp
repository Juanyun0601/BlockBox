/**
 * @file   SettingsAiAssistantPage.cpp
 * @brief  AI 助手设置页实现
 * @author BlockBox Team
 * @date   2026-08-25
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextEdit>
#include <QUuid>
#include <QVBoxLayout>

#include "components/AiModelFetchRow.h"
#include "components/ModelProviderSettingsPanel.h"
#include <QFileDialog>
#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"
#include "utils/AiProviderPresets.h"
#include "utils/SettingsManager.h"
#include "utils/dsh/DshPluginInstaller.h"
#include "utils/UiMetrics.h"
#include "platform.h"

void SettingsPage::initAiAssistantSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_aiAssistantSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // ── 标题栏（标题 + 恢复默认值按钮）──
    createSettingsHeader(layout, tr("AI 助手"));

    QLabel *hintLabel = new QLabel(tr("配置 AI 助手的模型、联网搜索和工作模式等参数"), m_aiAssistantSettings);
    hintLabel->setObjectName("advancedHintLabel");
    layout->addWidget(hintLabel);

    SettingsManager *settings = SettingsManager::instance();

    // ── 默认模型设置（列出已配置/已添加的真实模型）──
    QVBoxLayout *defaultModelCard = createSettingsCard(layout, tr("默认模型"));

    QComboBox *defaultModelCombo = new QComboBox();
    m_aiDefaultModelCombo = defaultModelCombo;
    disableWheelEffect(defaultModelCombo);

    // 用真实模型填充下拉框（自定义模型 + 已配置服务商的选中模型）
    refreshAiDefaultModelCombo();

    connect(defaultModelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_aiDefaultModelCombo)
            return;
        // itemData 存模型 ID；空串 = 自动（沿用内置默认逻辑）
        SettingsManager::instance()->setProperty(
            "ai/defaultModelId", m_aiDefaultModelCombo->itemData(index).toString());
    });

    QHBoxLayout *defaultModelRow = appendSettingRow(defaultModelCard,
        tr("默认模型"), tr("选择 AI 助手默认使用的模型，列表仅显示已配置或已添加的模型。"),
        tr("选择「自动」时由启动器按内置优先级挑选；配置服务商或添加自定义模型后列表会自动更新。"));
    defaultModelRow->addWidget(defaultModelCombo);

    // ── 服务商配置（左列服务商 + 右侧内联配置）──
    QVBoxLayout *providerCard = createSettingsCard(layout, tr("服务商配置"));

    QWidget *providerContent = new QWidget();
    providerContent->setObjectName("providerCardContent");
    QVBoxLayout *providerContentLayout = new QVBoxLayout(providerContent);
    providerContentLayout->setContentsMargins(24, 8, 24, 8);
    providerContentLayout->setSpacing(8);

    QLabel *providerHint = new QLabel(
        tr("在左侧选择服务商并填写 API 地址 / 密钥。模型不需要在这里添加：聊天里选模型时先选服务商，再从服务商返回的模型列表里挑，列表会缓存。"),
        providerContent);
    providerHint->setObjectName("settingDesc");
    providerHint->setWordWrap(true);
    providerContentLayout->addWidget(providerHint);

    ModelProviderSettingsPanel *providerPanel = new ModelProviderSettingsPanel(providerContent);
    providerContentLayout->addWidget(providerPanel);
    connect(providerPanel, &ModelProviderSettingsPanel::providersChanged,
            this, &SettingsPage::refreshAiDefaultModelCombo);

    providerCard->addWidget(providerContent);

    // ── 联网搜索设置 ──
    QVBoxLayout *webSearchCard = createSettingsCard(layout, tr("联网搜索"));

    QComboBox *webSearchCombo = new QComboBox();
    webSearchCombo->addItems({tr("自动（AI 按需联网）"), tr("优先（AI 优先联网确认）"), tr("关闭")});
    disableWheelEffect(webSearchCombo);

    int savedWebSearchMode = settings->getProperty("ai/webSearchMode", 0).toInt();
    webSearchCombo->setCurrentIndex(savedWebSearchMode);

    connect(webSearchCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        settings->setProperty("ai/webSearchMode", index);
    });

    QHBoxLayout *webSearchRow = appendSettingRow(webSearchCard,
        tr("联网模式"), tr("控制 AI 助手是否联网搜索信息。"),
        tr("联网搜索可获取最新信息，但会增加响应时间。"));
    webSearchRow->addWidget(webSearchCombo);

    // ── 深度思考设置 ──
    QVBoxLayout *deepThinkingCard = createSettingsCard(layout, tr("深度思考"));

    CustomCheckBox *deepThinkingCheck = new CustomCheckBox();
    bool savedDeepThinking = settings->getProperty("ai/deepThinking", false).toBool();
    deepThinkingCheck->setChecked(savedDeepThinking);
    connect(deepThinkingCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setProperty("ai/deepThinking", checked);
    });

    QHBoxLayout *deepThinkingRow = appendSettingRow(deepThinkingCard,
        tr("默认启用深度思考"), tr("开启后 AI 会先进行推理分析再回答，适合复杂问题。"),
        tr("深度思考会消耗更多 token，但能提供更准确、深入的回答。"), true);
    deepThinkingRow->addWidget(deepThinkingCheck);

    // ── 工作模式设置 ──
    QVBoxLayout *workModeCard = createSettingsCard(layout, tr("工作模式"));

    CustomCheckBox *agentToolsCheck = new CustomCheckBox();
    bool savedAgentTools = settings->getProperty("ai/agentTools", true).toBool();
    agentToolsCheck->setChecked(savedAgentTools);
    connect(agentToolsCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setProperty("ai/agentTools", checked);
    });

    QHBoxLayout *agentToolsRow = appendSettingRow(workModeCard,
        tr("启用 Agent 工具"), tr("允许 AI 助手在工作模式下执行本地工具（如查看实例、分析日志等）。"),
        tr("Agent 工具可让 AI 直接操作启动器功能，提高工作效率。"));
    agentToolsRow->addWidget(agentToolsCheck);

    CustomCheckBox *autoTitleCheck = new CustomCheckBox();
    bool savedAutoTitle = settings->getProperty("ai/autoTitle", true).toBool();
    autoTitleCheck->setChecked(savedAutoTitle);
    connect(autoTitleCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setProperty("ai/autoTitle", checked);
    });

    QHBoxLayout *autoTitleRow = appendSettingRow(workModeCard,
        tr("自动生成对话标题"), tr("对话结束后自动生成简洁的标题。"),
        tr("启用后可自动为对话创建有意义的标题，便于管理。"), true);
    autoTitleRow->addWidget(autoTitleCheck);

    // ── 上下文设置 ──
    QVBoxLayout *contextCard = createSettingsCard(layout, tr("上下文管理"));

    QComboBox *contextSizeCombo = new QComboBox();
    contextSizeCombo->addItems({tr("8K tokens"), tr("16K tokens"), tr("32K tokens"), tr("64K tokens"), tr("128K tokens")});
    disableWheelEffect(contextSizeCombo);

    int savedContextSize = settings->getProperty("ai/contextSize", 2).toInt();
    contextSizeCombo->setCurrentIndex(savedContextSize);

    connect(contextSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
        settings->setProperty("ai/contextSize", index);
    });

    QHBoxLayout *contextSizeRow = appendSettingRow(contextCard,
        tr("上下文大小"), tr("控制发送给 AI 的历史消息数量。"),
        tr("更大的上下文可让 AI 记住更多对话内容，但会消耗更多 token。"));
    contextSizeRow->addWidget(contextSizeCombo);

    CustomCheckBox *autoCompressCheck = new CustomCheckBox();
    bool savedAutoCompress = settings->getProperty("ai/autoCompress", true).toBool();
    autoCompressCheck->setChecked(savedAutoCompress);
    connect(autoCompressCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setProperty("ai/autoCompress", checked);
    });

    QHBoxLayout *autoCompressRow = appendSettingRow(contextCard,
        tr("自动压缩上下文"), tr("当对话历史过长时自动压缩，避免超出模型限制。"),
        tr("压缩会保留最近的消息并生成早期对话摘要，可显著延长对话长度。"), true);
    autoCompressRow->addWidget(autoCompressCheck);

    // ── 系统提示词设置 ──
    QVBoxLayout *systemPromptCard = createSettingsCard(layout, tr("系统提示词"));

    // 包装容器，提供左右内边距
    QWidget *spContent = new QWidget();
    spContent->setObjectName("providerCardContent");
    QVBoxLayout *spContentLayout = new QVBoxLayout(spContent);
    spContentLayout->setContentsMargins(24, 8, 24, 8);
    spContentLayout->setSpacing(8);

    // 从资源文件加载默认提示词（辅助 lambda）
    auto loadDefaultPrompt = [](const QString &key) -> QString {
        QFile file(QStringLiteral(":/resources/ai_system_prompts.json"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError pe;
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
            file.close();
            if (pe.error == QJsonParseError::NoError && doc.isObject())
                return doc.object().value(key).toString();
        }
        return QString();
    };

    // 聊天模式提示词
    QLabel *chatPromptSummary = new QLabel();
    chatPromptSummary->setObjectName("settingDesc");
    chatPromptSummary->setWordWrap(true);
    auto updateChatSummary = [chatPromptSummary, settings, loadDefaultPrompt]() {
        QString custom = settings->getProperty("ai/customSystemPrompt/chat", QString()).toString();
        QString text;
        if (custom.isEmpty()) {
            QString def = loadDefaultPrompt(QStringLiteral("chat"));
            text = def.left(50) + QStringLiteral("...") + tr(" (默认)");
        } else {
            text = custom.left(50) + QStringLiteral("...");
        }
        chatPromptSummary->setText(text);
    };
    updateChatSummary();

    QPushButton *chatPromptEditBtn = new QPushButton(tr("编辑"));
    chatPromptEditBtn->setCursor(Qt::PointingHandCursor);
    chatPromptEditBtn->setFixedWidth(80);
    connect(chatPromptEditBtn, &QPushButton::clicked, this, [this, settings, loadDefaultPrompt, updateChatSummary]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("编辑聊天模式系统提示词"));
        dlg.setMinimumSize(UiMetrics::fitToScreen(&dlg, QSize(600, 450)));

        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        dlgLayout->setContentsMargins(16, 16, 16, 16);
        dlgLayout->setSpacing(12);

        QLabel *descLabel = new QLabel(tr("系统提示词定义了 AI 助手的角色、能力和行为规范。修改后将在下次对话时生效。"), &dlg);
        descLabel->setObjectName("settingDesc");
        descLabel->setWordWrap(true);
        dlgLayout->addWidget(descLabel);

        QTextEdit *textEdit = new QTextEdit(&dlg);
        textEdit->setObjectName("systemPromptEditor");
        QFont monoFont(QStringLiteral("Consolas"), 10);
        monoFont.setStyleHint(QFont::Monospace);
        textEdit->setFont(monoFont);
        textEdit->setAcceptRichText(false);
        QString custom = settings->getProperty("ai/customSystemPrompt/chat", QString()).toString();
        if (custom.isEmpty())
            textEdit->setPlainText(loadDefaultPrompt(QStringLiteral("chat")));
        else
            textEdit->setPlainText(custom);
        dlgLayout->addWidget(textEdit);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(8);

        QPushButton *resetBtn = new QPushButton(tr("恢复默认"), &dlg);
        resetBtn->setCursor(Qt::PointingHandCursor);
        connect(resetBtn, &QPushButton::clicked, &dlg, [settings, textEdit, loadDefaultPrompt]() {
            settings->setProperty("ai/customSystemPrompt/chat", QString());
            textEdit->setPlainText(loadDefaultPrompt(QStringLiteral("chat")));
        });
        btnLayout->addWidget(resetBtn);

        btnLayout->addStretch();

        QPushButton *cancelBtn = new QPushButton(tr("取消"), &dlg);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);

        QPushButton *saveBtn = new QPushButton(tr("保存"), &dlg);
        saveBtn->setCursor(Qt::PointingHandCursor);
        connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        btnLayout->addWidget(saveBtn);

        dlgLayout->addLayout(btnLayout);

        if (dlg.exec() == QDialog::Accepted) {
            QString defaultPrompt = loadDefaultPrompt(QStringLiteral("chat"));
            QString edited = textEdit->toPlainText().trimmed();
            if (edited == defaultPrompt)
                settings->setProperty("ai/customSystemPrompt/chat", QString());
            else
                settings->setProperty("ai/customSystemPrompt/chat", edited);
            updateChatSummary();
        }
    });

    QHBoxLayout *chatPromptRow = appendSettingRow(spContentLayout,
        tr("聊天模式提示词"), tr("定义 AI 助手在聊天模式下的角色和行为。"),
        tr("自定义提示词可让 AI 以特定风格或身份回复，修改后在新对话中生效。"));
    chatPromptRow->addWidget(chatPromptSummary, 1);
    chatPromptRow->addWidget(chatPromptEditBtn);

    // 工作模式提示词
    QLabel *workPromptSummary = new QLabel();
    workPromptSummary->setObjectName("settingDesc");
    workPromptSummary->setWordWrap(true);
    auto updateWorkSummary = [workPromptSummary, settings, loadDefaultPrompt]() {
        QString custom = settings->getProperty("ai/customSystemPrompt/work", QString()).toString();
        QString text;
        if (custom.isEmpty()) {
            QString def = loadDefaultPrompt(QStringLiteral("work"));
            text = def.left(50) + QStringLiteral("...") + tr(" (默认)");
        } else {
            text = custom.left(50) + QStringLiteral("...");
        }
        workPromptSummary->setText(text);
    };
    updateWorkSummary();

    QPushButton *workPromptEditBtn = new QPushButton(tr("编辑"));
    workPromptEditBtn->setCursor(Qt::PointingHandCursor);
    workPromptEditBtn->setFixedWidth(80);
    connect(workPromptEditBtn, &QPushButton::clicked, this, [this, settings, loadDefaultPrompt, updateWorkSummary]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("编辑工作模式系统提示词"));
        dlg.setMinimumSize(UiMetrics::fitToScreen(&dlg, QSize(600, 450)));

        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        dlgLayout->setContentsMargins(16, 16, 16, 16);
        dlgLayout->setSpacing(12);

        QLabel *descLabel = new QLabel(tr("工作模式提示词定义了 AI 助手的工具调用能力和专业行为规范，包含 Agent 工具说明。"), &dlg);
        descLabel->setObjectName("settingDesc");
        descLabel->setWordWrap(true);
        dlgLayout->addWidget(descLabel);

        QTextEdit *textEdit = new QTextEdit(&dlg);
        textEdit->setObjectName("systemPromptEditor");
        QFont monoFont2(QStringLiteral("Consolas"), 10);
        monoFont2.setStyleHint(QFont::Monospace);
        textEdit->setFont(monoFont2);
        textEdit->setAcceptRichText(false);
        QString custom = settings->getProperty("ai/customSystemPrompt/work", QString()).toString();
        if (custom.isEmpty())
            textEdit->setPlainText(loadDefaultPrompt(QStringLiteral("work")));
        else
            textEdit->setPlainText(custom);
        dlgLayout->addWidget(textEdit);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(8);

        QPushButton *resetBtn = new QPushButton(tr("恢复默认"), &dlg);
        resetBtn->setCursor(Qt::PointingHandCursor);
        connect(resetBtn, &QPushButton::clicked, &dlg, [settings, textEdit, loadDefaultPrompt]() {
            settings->setProperty("ai/customSystemPrompt/work", QString());
            textEdit->setPlainText(loadDefaultPrompt(QStringLiteral("work")));
        });
        btnLayout->addWidget(resetBtn);

        btnLayout->addStretch();

        QPushButton *cancelBtn = new QPushButton(tr("取消"), &dlg);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);

        QPushButton *saveBtn = new QPushButton(tr("保存"), &dlg);
        saveBtn->setCursor(Qt::PointingHandCursor);
        connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        btnLayout->addWidget(saveBtn);

        dlgLayout->addLayout(btnLayout);

        if (dlg.exec() == QDialog::Accepted) {
            QString defaultPrompt = loadDefaultPrompt(QStringLiteral("work"));
            QString edited = textEdit->toPlainText().trimmed();
            if (edited == defaultPrompt)
                settings->setProperty("ai/customSystemPrompt/work", QString());
            else
                settings->setProperty("ai/customSystemPrompt/work", edited);
            updateWorkSummary();
        }
    });

    QHBoxLayout *workPromptRow = appendSettingRow(spContentLayout,
        tr("工作模式提示词"), tr("定义 AI 助手在工作模式下的工具调用能力和专业行为。"),
        tr("工作模式提示词包含 Agent 工具说明，修改前请确保了解各工具的用途。"), true);
    workPromptRow->addWidget(workPromptSummary, 1);
    workPromptRow->addWidget(workPromptEditBtn);

    systemPromptCard->addWidget(spContent);

    // ── 系统提示词库管理 ──
    QVBoxLayout *promptLibCard = createSettingsCard(layout, tr("系统提示词库"));

    // 包装容器，提供左右内边距
    QWidget *plContent = new QWidget();
    plContent->setObjectName("providerCardContent");
    QVBoxLayout *plContentLayout = new QVBoxLayout(plContent);
    plContentLayout->setContentsMargins(24, 8, 24, 8);
    plContentLayout->setSpacing(8);

    QLabel *promptLibHint = new QLabel(tr("管理系统提示词库，在 AI 聊天页新建对话时可从下拉框中选择使用（支持多选合并）。"), plContent);
    promptLibHint->setObjectName("settingDesc");
    promptLibHint->setWordWrap(true);
    plContentLayout->addWidget(promptLibHint);

    QListWidget *promptLibList = new QListWidget();
    promptLibList->setObjectName("providerList");
    promptLibList->setMinimumHeight(80);
    promptLibList->setMaximumHeight(180);

    // 加载提示词库
    auto loadPromptLibrary = [promptLibList, settings]() {
        promptLibList->clear();
        QString json = settings->getProperty("ai/systemPromptLibrary", QString()).toString();
        if (json.isEmpty()) return;
        QJsonParseError pe;
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isArray()) return;
        for (const QJsonValue &v : doc.array()) {
            QJsonObject o = v.toObject();
            QString name = o["name"].toString();
            QString content = o["content"].toString();
            QString summary = content.left(40).replace('\n', ' ');
            if (content.length() > 40) summary += QStringLiteral("...");
            QListWidgetItem *item = new QListWidgetItem(QStringLiteral("%1 — %2").arg(name, summary));
            item->setData(Qt::UserRole, o["id"].toString());
            item->setData(Qt::UserRole + 1, name);
            item->setData(Qt::UserRole + 2, content);
            promptLibList->addItem(item);
        }
    };
    loadPromptLibrary();

    plContentLayout->addWidget(promptLibList);

    // 添加/编辑/删除按钮行
    QHBoxLayout *promptLibBtnRow = new QHBoxLayout();
    promptLibBtnRow->setSpacing(8);

    QPushButton *addPromptBtn = new QPushButton(tr("添加"));
    addPromptBtn->setCursor(Qt::PointingHandCursor);
    promptLibBtnRow->addWidget(addPromptBtn);

    QPushButton *editPromptBtn = new QPushButton(tr("编辑"));
    editPromptBtn->setCursor(Qt::PointingHandCursor);
    promptLibBtnRow->addWidget(editPromptBtn);

    QPushButton *delPromptBtn = new QPushButton(tr("删除"));
    delPromptBtn->setCursor(Qt::PointingHandCursor);
    promptLibBtnRow->addWidget(delPromptBtn);

    promptLibBtnRow->addStretch();
    plContentLayout->addLayout(promptLibBtnRow);

    promptLibCard->addWidget(plContent);

    // 添加提示词
    connect(addPromptBtn, &QPushButton::clicked, this, [this, settings, promptLibList, loadPromptLibrary]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("添加系统提示词"));
        dlg.setMinimumSize(UiMetrics::fitToScreen(&dlg, QSize(520, 400)));
        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        dlgLayout->setContentsMargins(16, 16, 16, 16);
        dlgLayout->setSpacing(12);

        QLineEdit *nameEdit = new QLineEdit();
        nameEdit->setPlaceholderText(tr("例如：翻译专家、红石工程师"));
        QFormLayout *nameForm = new QFormLayout();
        nameForm->addRow(tr("名称:"), nameEdit);
        dlgLayout->addLayout(nameForm);

        QTextEdit *contentEdit = new QTextEdit();
        contentEdit->setPlaceholderText(tr("输入系统提示词内容..."));
        QFont monoFont(QStringLiteral("Consolas"), 10);
        monoFont.setStyleHint(QFont::Monospace);
        contentEdit->setFont(monoFont);
        contentEdit->setAcceptRichText(false);
        dlgLayout->addWidget(contentEdit, 1);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        QPushButton *cancelBtn = new QPushButton(tr("取消"), &dlg);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);
        QPushButton *saveBtn = new QPushButton(tr("保存"), &dlg);
        saveBtn->setCursor(Qt::PointingHandCursor);
        connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        btnLayout->addWidget(saveBtn);
        dlgLayout->addLayout(btnLayout);

        if (dlg.exec() == QDialog::Accepted) {
            QString name = nameEdit->text().trimmed();
            QString content = contentEdit->toPlainText().trimmed();
            if (name.isEmpty() || content.isEmpty()) return;

            QString json = settings->getProperty("ai/systemPromptLibrary", QString()).toString();
            QJsonArray arr;
            if (!json.isEmpty()) {
                QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
                if (d.isArray()) arr = d.array();
            }
            QJsonObject obj;
            obj["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            obj["name"] = name;
            obj["content"] = content;
            arr.append(obj);
            settings->setProperty("ai/systemPromptLibrary",
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            loadPromptLibrary();
        }
    });

    // 编辑提示词
    connect(editPromptBtn, &QPushButton::clicked, this, [this, settings, promptLibList, loadPromptLibrary]() {
        QListWidgetItem *sel = promptLibList->currentItem();
        if (!sel) return;
        QString id = sel->data(Qt::UserRole).toString();
        QString oldName = sel->data(Qt::UserRole + 1).toString();
        QString oldContent = sel->data(Qt::UserRole + 2).toString();

        QDialog dlg(this);
        dlg.setWindowTitle(tr("编辑系统提示词"));
        dlg.setMinimumSize(UiMetrics::fitToScreen(&dlg, QSize(520, 400)));
        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        dlgLayout->setContentsMargins(16, 16, 16, 16);
        dlgLayout->setSpacing(12);

        QLineEdit *nameEdit = new QLineEdit(oldName);
        QFormLayout *nameForm = new QFormLayout();
        nameForm->addRow(tr("名称:"), nameEdit);
        dlgLayout->addLayout(nameForm);

        QTextEdit *contentEdit = new QTextEdit();
        contentEdit->setPlainText(oldContent);
        QFont monoFont2(QStringLiteral("Consolas"), 10);
        monoFont2.setStyleHint(QFont::Monospace);
        contentEdit->setFont(monoFont2);
        contentEdit->setAcceptRichText(false);
        dlgLayout->addWidget(contentEdit, 1);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        QPushButton *cancelBtn = new QPushButton(tr("取消"), &dlg);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);
        QPushButton *saveBtn = new QPushButton(tr("保存"), &dlg);
        saveBtn->setCursor(Qt::PointingHandCursor);
        connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        btnLayout->addWidget(saveBtn);
        dlgLayout->addLayout(btnLayout);

        if (dlg.exec() == QDialog::Accepted) {
            QString name = nameEdit->text().trimmed();
            QString content = contentEdit->toPlainText().trimmed();
            if (name.isEmpty() || content.isEmpty()) return;

            QString json = settings->getProperty("ai/systemPromptLibrary", QString()).toString();
            QJsonArray arr;
            if (!json.isEmpty()) {
                QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
                if (d.isArray()) arr = d.array();
            }
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject e = arr[i].toObject();
                if (e["id"].toString() == id) {
                    e["name"] = name;
                    e["content"] = content;
                    arr[i] = e;
                    break;
                }
            }
            settings->setProperty("ai/systemPromptLibrary",
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            loadPromptLibrary();
        }
    });

    // 删除提示词
    connect(delPromptBtn, &QPushButton::clicked, this, [this, settings, promptLibList, loadPromptLibrary]() {
        QListWidgetItem *sel = promptLibList->currentItem();
        if (!sel) return;
        QString id = sel->data(Qt::UserRole).toString();

        QString json = settings->getProperty("ai/systemPromptLibrary", QString()).toString();
        QJsonArray arr;
        if (!json.isEmpty()) {
            QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
            if (d.isArray()) arr = d.array();
        }
        QJsonArray newArr;
        for (const QJsonValue &v : arr) {
            QJsonObject e = v.toObject();
            if (e["id"].toString() != id)
                newArr.append(e);
        }
        settings->setProperty("ai/systemPromptLibrary",
            QString::fromUtf8(QJsonDocument(newArr).toJson(QJsonDocument::Compact)));
        loadPromptLibrary();
    });

    // ── 本地 MCP 服务器（外部 AI 客户端接入）──
    QVBoxLayout *mcpCard = createSettingsCard(layout, tr("本地 MCP 服务器"));

    QWidget *mcpContent = new QWidget();
    mcpContent->setObjectName("providerCardContent");
    QVBoxLayout *mcpContentLayout = new QVBoxLayout(mcpContent);
    mcpContentLayout->setContentsMargins(24, 8, 24, 8);
    mcpContentLayout->setSpacing(8);

    QLabel *mcpHint = new QLabel(
        tr("开启后，Claude Code、Cursor、Cherry Studio 等支持 MCP 的 AI 客户端可以通过本机地址"
           "直接操作方块盒子：看实例和模组、读日志与崩溃报告、查存档、备份世界、按种子找结构、"
           "启停模组等。服务只监听 127.0.0.1，默认只读。"),
        mcpContent);
    mcpHint->setObjectName("settingDesc");
    mcpHint->setWordWrap(true);
    mcpContentLayout->addWidget(mcpHint);

    CustomCheckBox *mcpEnableCheck = new CustomCheckBox();
    mcpEnableCheck->setChecked(settings->getProperty("mcp/enabled", false).toBool());
    QHBoxLayout *mcpEnableRow = appendSettingRow(mcpContentLayout,
        tr("启用本地 MCP 服务器"), tr("在本机开放一个 MCP 接口，供外部 AI 客户端连接。"),
        tr("只监听 127.0.0.1，并校验下面的访问令牌；关闭后立即停止监听。"));
    mcpEnableRow->addWidget(mcpEnableCheck);

    QSpinBox *mcpPortSpin = new QSpinBox();
    mcpPortSpin->setRange(1024, 65535);
    mcpPortSpin->setValue(settings->getProperty("mcp/port", 17890).toInt());
    QHBoxLayout *mcpPortRow = appendSettingRow(mcpContentLayout,
        tr("端口"), tr("MCP 服务监听的本地端口，默认 17890。"),
        tr("端口被占用时状态栏会显示失败原因，换一个端口即可。"));
    mcpPortRow->addWidget(mcpPortSpin);

    CustomCheckBox *mcpWriteCheck = new CustomCheckBox();
    mcpWriteCheck->setChecked(settings->getProperty("mcp/allowWrite", false).toBool());
    QHBoxLayout *mcpWriteRow = appendSettingRow(mcpContentLayout,
        tr("允许写操作"), tr("允许外部客户端启停/删除模组、备份或恢复存档、下载实例、启动游戏。"),
        tr("写操作每次仍需客户端显式传 confirm=true；不确定时请保持关闭。"), true);
    mcpWriteRow->addWidget(mcpWriteCheck);

    QString mcpToken = settings->getProperty("mcp/token", QString()).toString().trimmed();
    if (mcpToken.isEmpty())
    {
        mcpToken = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
        settings->setProperty("mcp/token", mcpToken);
    }

    QLineEdit *mcpTokenEdit = new QLineEdit(mcpToken);
    mcpTokenEdit->setReadOnly(true);
    QPushButton *mcpTokenRegenBtn = new QPushButton(tr("重新生成"));
    mcpTokenRegenBtn->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *mcpTokenRow = appendSettingRow(mcpContentLayout,
        tr("访问令牌"), tr("客户端连接时要在请求头里带上它，防止别的程序偷偷调用。"),
        tr("重新生成后，已经配好的客户端需要同步更新令牌。"));
    mcpTokenRow->addWidget(mcpTokenEdit, 1);
    mcpTokenRow->addWidget(mcpTokenRegenBtn);

    m_mcpStatusLabel = new QLabel(tr("状态：未启动"));
    m_mcpStatusLabel->setObjectName("settingDesc");
    m_mcpStatusLabel->setWordWrap(true);
    mcpContentLayout->addWidget(m_mcpStatusLabel);

    QPushButton *mcpCopyBtn = new QPushButton(tr("复制客户端配置"));
    mcpCopyBtn->setCursor(Qt::PointingHandCursor);
    QPushButton *mcpDocsBtn = new QPushButton(tr("接入文档"));
    mcpDocsBtn->setCursor(Qt::PointingHandCursor);
    mcpDocsBtn->setToolTip(tr("在软件内打开 MCP 接入指南：怎么填地址、令牌和客户端配置"));
    QHBoxLayout *mcpCopyRow = new QHBoxLayout();
    mcpCopyRow->addWidget(mcpDocsBtn);
    mcpCopyRow->addStretch();
    mcpCopyRow->addWidget(mcpCopyBtn);
    mcpContentLayout->addLayout(mcpCopyRow);
    connect(mcpDocsBtn, &QPushButton::clicked, this, [this]() {
        emit openLibraryDocRequested(QStringLiteral("mcp"), QStringLiteral("enable"));
    });

    mcpCard->addWidget(mcpContent);

    connect(mcpEnableCheck, &CustomCheckBox::toggled, this, [=](bool checked) {
        settings->setProperty("mcp/enabled", checked);
        emit mcpServerSettingsChanged();
    });
    connect(mcpPortSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int value) {
        settings->setProperty("mcp/port", value);
        emit mcpServerSettingsChanged();
    });
    connect(mcpWriteCheck, &CustomCheckBox::toggled, this, [=](bool checked) {
        settings->setProperty("mcp/allowWrite", checked);
        emit mcpServerSettingsChanged();
    });
    connect(mcpTokenRegenBtn, &QPushButton::clicked, this, [=]() {
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
        settings->setProperty("mcp/token", token);
        mcpTokenEdit->setText(token);
        emit mcpServerSettingsChanged();
    });
    connect(mcpCopyBtn, &QPushButton::clicked, this, [=]() {
        QJsonObject server;
        server.insert(QStringLiteral("type"), QStringLiteral("http"));
        server.insert(QStringLiteral("url"),
                      QStringLiteral("http://127.0.0.1:%1/mcp").arg(mcpPortSpin->value()));
        QJsonObject headers;
        headers.insert(QStringLiteral("Authorization"),
                       QStringLiteral("Bearer %1").arg(mcpTokenEdit->text()));
        server.insert(QStringLiteral("headers"), headers);

        QJsonObject mcpServers;
        mcpServers.insert(QStringLiteral("blockbox"), server);
        QJsonObject root;
        root.insert(QStringLiteral("mcpServers"), mcpServers);

        QGuiApplication::clipboard()->setText(
            QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)));
        if (m_mcpStatusLabel)
        {
            m_mcpStatusLabel->setText(tr("客户端配置已复制，粘贴到 MCP 客户端的配置里即可。"));
        }
    });

    // ── DeepSeek Harness 插件（一键把方块盒子的能力接进 DSH）──
    QVBoxLayout *dshCard = createSettingsCard(layout, tr("DeepSeek Harness 插件"));

    QWidget *dshContent = new QWidget();
    dshContent->setObjectName("providerCardContent");
    QVBoxLayout *dshContentLayout = new QVBoxLayout(dshContent);
    dshContentLayout->setContentsMargins(24, 8, 24, 8);
    dshContentLayout->setSpacing(8);

    QLabel *dshHint = new QLabel(
        tr("把方块盒子的本地能力接进 DeepSeek Harness（DSH）：装好并重启 DSH 后，在 DSH 里就能直接查实例、"
           "看模组、读游戏日志、分析崩溃、查存档、按种子找结构。插件会自动读上面这张卡里的端口和令牌，不用手填。"),
        dshContent);
    dshHint->setObjectName("settingDesc");
    dshHint->setWordWrap(true);
    dshContentLayout->addWidget(dshHint);

    m_dshProfileCombo = new QComboBox();
    QPushButton *dshRefreshBtn = new QPushButton(tr("重新检测"));
    dshRefreshBtn->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *dshProfileRow = appendSettingRow(dshContentLayout,
        tr("安装到"), tr("选择要装进哪个 DSH 配置，一般只有 desktop。"),
        tr("列表来自本机 DSH 的配置目录，没找到说明这台机器还没装过 DSH。"));
    dshProfileRow->addWidget(m_dshProfileCombo, 1);
    dshProfileRow->addWidget(dshRefreshBtn);

    m_dshStatusLabel = new QLabel(tr("状态：正在检测…"));
    m_dshStatusLabel->setObjectName("settingDesc");
    m_dshStatusLabel->setWordWrap(true);
    dshContentLayout->addWidget(m_dshStatusLabel);

    m_dshInstallButton = new QPushButton(tr("一键安装"));
    m_dshInstallButton->setCursor(Qt::PointingHandCursor);
    m_dshUninstallButton = new QPushButton(tr("卸载"));
    m_dshUninstallButton->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *dshActionRow = new QHBoxLayout();
    dshActionRow->addWidget(m_dshUninstallButton);
    dshActionRow->addStretch();
    dshActionRow->addWidget(m_dshInstallButton);
    dshContentLayout->addLayout(dshActionRow);

    dshCard->addWidget(dshContent);

    connect(dshRefreshBtn, &QPushButton::clicked, this, [this]() { refreshDshPluginCard(); });
    connect(m_dshProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateDshPluginStatus(); });
    connect(m_dshInstallButton, &QPushButton::clicked, this, [this]() {
        const QString profileDir = m_dshProfileCombo ? m_dshProfileCombo->currentData().toString() : QString();
        if (profileDir.isEmpty())
        {
            return;
        }
        const QString payloadDir = QDir(Platform::getDataDirectory())
            .filePath(QStringLiteral("dsh-plugin/") + QString::fromLatin1(DshPluginInstaller::kPackageName));
        QString error;
        if (!DshPluginInstaller::writePayload(payloadDir, &error)
            || !DshPluginInstaller::installInto(profileDir, payloadDir, &error))
        {
            if (m_dshStatusLabel)
            {
                m_dshStatusLabel->setText(tr("安装失败：%1").arg(error));
            }
            return;
        }
        refreshDshPluginCard();
        if (m_dshStatusLabel)
        {
            m_dshStatusLabel->setText(tr("已装到 %1。重启 DeepSeek Harness 后生效。")
                                          .arg(QFileInfo(profileDir).fileName()));
        }
    });
    connect(m_dshUninstallButton, &QPushButton::clicked, this, [this]() {
        const QString profileDir = m_dshProfileCombo ? m_dshProfileCombo->currentData().toString() : QString();
        if (profileDir.isEmpty())
        {
            return;
        }
        QString error;
        if (!DshPluginInstaller::uninstallFrom(profileDir, &error))
        {
            if (m_dshStatusLabel)
            {
                m_dshStatusLabel->setText(tr("卸载失败：%1").arg(error));
            }
            return;
        }
        refreshDshPluginCard();
    });
    connect(this, &SettingsPage::mcpServerSettingsChanged, this, [this]() { updateDshPluginStatus(); });

    refreshDshPluginCard();

    // ── 占位伸缩，保持内容顶部对齐 ──
    layout->addStretch();
}

void SettingsPage::refreshAiDefaultModelCombo()
{
    if (!m_aiDefaultModelCombo)
        return;

    SettingsManager *settings = SettingsManager::instance();
    const QString savedId = settings->getProperty("ai/defaultModelId", QString()).toString();

    const QSignalBlocker blocker(m_aiDefaultModelCombo);
    m_aiDefaultModelCombo->clear();

    // 第一项：自动（空 ID，由 AiChatPage 按内置优先级挑选）
    m_aiDefaultModelCombo->addItem(tr("自动"), QString());

    // 真实模型列表：自定义模型 + 已配 Key 服务商的选中模型（与翻译设置共用 aiConfiguredModels）
    for (const AiConfiguredModel &m : aiConfiguredModels()) {
        const QString label = QStringLiteral("%1 (%2)").arg(m.displayName, m.id);
        m_aiDefaultModelCombo->addItem(label, m.id);
    }

    // 恢复选中项；已保存的 ID 不在列表中时补一项，避免静默丢失用户选择
    int index = 0;
    if (!savedId.isEmpty()) {
        index = m_aiDefaultModelCombo->findData(savedId);
        if (index < 0) {
            m_aiDefaultModelCombo->addItem(tr("已移除的模型 (%1)").arg(savedId), savedId);
            index = m_aiDefaultModelCombo->count() - 1;
        }
    }
    m_aiDefaultModelCombo->setCurrentIndex(index);
}

void SettingsPage::setMcpServerStatus(bool running, quint16 port, const QString &detail)
{
    if (!m_mcpStatusLabel)
    {
        return;
    }

    if (running)
    {
        m_mcpStatusLabel->setText(
            tr("状态：正在监听 http://127.0.0.1:%1/mcp").arg(port));
    }
    else if (!detail.trimmed().isEmpty())
    {
        m_mcpStatusLabel->setText(tr("状态：启动失败（%1）").arg(detail));
    }
    else
    {
        m_mcpStatusLabel->setText(tr("状态：未启动"));
    }
}

void SettingsPage::refreshDshPluginCard()
{
    if (!m_dshProfileCombo || !m_dshStatusLabel)
    {
        return;
    }

    const QString previousDir = m_dshProfileCombo->currentData().toString();
    const QVector<DshPluginInstaller::Profile> profiles = DshPluginInstaller::detectProfiles();

    {
        const QSignalBlocker blocker(m_dshProfileCombo);
        m_dshProfileCombo->clear();
        for (const DshPluginInstaller::Profile &profile : profiles)
        {
            const QString label =
                profile.installed
                    ? tr("%1（已安装 %2）")
                          .arg(profile.name,
                               profile.version.isEmpty() ? tr("版本未知") : profile.version)
                    : profile.name;
            m_dshProfileCombo->addItem(label, profile.dir);
        }
        const int restore = m_dshProfileCombo->findData(previousDir);
        if (restore >= 0)
        {
            m_dshProfileCombo->setCurrentIndex(restore);
        }
    }

    if (profiles.isEmpty())
    {
        m_dshStatusLabel->setText(
            tr("状态：没找到 DeepSeek Harness 的配置目录（%1），这台机器可能还没装过 DSH。")
                .arg(DshPluginInstaller::defaultHome()));
        if (m_dshInstallButton)
        {
            m_dshInstallButton->setEnabled(false);
        }
        if (m_dshUninstallButton)
        {
            m_dshUninstallButton->setEnabled(false);
        }
        return;
    }

    updateDshPluginStatus();
}

void SettingsPage::updateDshPluginStatus()
{
    if (!m_dshProfileCombo || !m_dshStatusLabel)
    {
        return;
    }

    const QString profileDir = m_dshProfileCombo->currentData().toString();
    if (profileDir.isEmpty())
    {
        if (m_dshInstallButton)
        {
            m_dshInstallButton->setEnabled(false);
        }
        if (m_dshUninstallButton)
        {
            m_dshUninstallButton->setEnabled(false);
        }
        return;
    }

    QString version;
    const bool installed = DshPluginInstaller::isInstalled(profileDir, &version);
    if (m_dshInstallButton)
    {
        m_dshInstallButton->setEnabled(true);
    }
    if (m_dshUninstallButton)
    {
        m_dshUninstallButton->setEnabled(installed);
    }

    SettingsManager *settings = SettingsManager::instance();
    const bool mcpEnabled = settings->getProperty("mcp/enabled", false).toBool();

    QString text = installed
        ? tr("状态：已安装（%1）。重启 DeepSeek Harness 后生效。")
              .arg(version.isEmpty() ? tr("版本未知") : version)
        : tr("状态：未安装。");
    if (!mcpEnabled)
    {
        text += QLatin1Char(' ');
        text += tr("还要在上面「本地 MCP 服务器」里勾选「启用本地 MCP 服务器」，DSH 才能拿到方块盒子的工具。");
    }
    m_dshStatusLabel->setText(text);
}
