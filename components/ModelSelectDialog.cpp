/**
 * @file   ModelSelectDialog.cpp
 * @brief  AI 模型选择与配置对话框实现
 * @author BlockBox Team
 * @date   2026-06-23
 */

#include "ModelSelectDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include "components/AppFileDialog.h"
#include "components/LocalModelDialog.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSpacerItem>
#include <QStyle>
#include <QTimer>
#include <QUrl>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

ModelSelectDialog::ModelSelectDialog(QWidget* parent)
    : AppDialogBase(parent)
    , m_scrollArea(nullptr)
    , m_contentWidget(nullptr)
    , m_contentLayout(nullptr)
    , m_configuredSection(nullptr)
    , m_configuredLayout(nullptr)
    , m_providerSection(nullptr)
    , m_providerLayout(nullptr)
    , m_configForm(nullptr)
    , m_configNameEdit(nullptr)
    , m_configIdEdit(nullptr)
    , m_configUrlEdit(nullptr)
    , m_configKeyEdit(nullptr)
    , m_configThinkingCheck(nullptr)
    , m_jsonSection(nullptr)
    , m_jsonEdit(nullptr)
    , m_jsonVisible(false)
    , m_searchEdit(nullptr)
    , m_networkManager(nullptr)
    , m_localModelBtn(nullptr)
    , m_localModelDialog(nullptr)
    , m_searchTimer(nullptr)
{
    setWindowTitle(tr("模型设置"));
    setWindowModality(Qt::NonModal);   // 覆盖基类的 WindowModal，保持原有非模态复用行为
    setObjectName("modelSelectDialog");
    m_networkManager = new QNetworkAccessManager(this);
    initUI();

    // 搜索输入防抖：避免每敲一个字就整树重建（重建会销毁并重建数百个控件）
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        rebuildConfiguredSection();
        rebuildProviderSections();
        markRebuilt();
    });
}

void ModelSelectDialog::showEvent(QShowEvent *event)
{
    // 基类负责：铺满主窗口定位 + 模糊背景 + 延迟校准
    AppDialogBase::showEvent(event);

    // 简化动画：仅淡入（不做 geometry 缩放，避免与基类的定位/延迟校准冲突）
    setWindowOpacity(0.0);

    auto *opacityAnim = new QPropertyAnimation(this, "windowOpacity");
    opacityAnim->setDuration(160);
    opacityAnim->setStartValue(0.0);
    opacityAnim->setEndValue(1.0);
    opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    opacityAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ModelSelectDialog::initUI()
{
    // 外层布局：卡片居中（AppDialogBase 提供铺满主窗口的模糊遮罩背景）
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* card = new QWidget(this);
    card->setObjectName("modelSelectCard");
    card->setMinimumSize(560, 640);
    card->setMaximumSize(780, 760);   // 保证不超出主窗口（主窗口最小 1200x800）
    outer->addWidget(card, 0, Qt::AlignCenter);

    auto* mainLayout = new QVBoxLayout(card);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========================================================================
    // 顶部标题栏
    // ========================================================================
    auto* titleBar = new QWidget();
    titleBar->setObjectName("modelDialogTitleBar");
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 16, 16, 16);

    auto* titleLabel = new QLabel(tr("模型设置"));
    titleLabel->setObjectName("modelDialogTitle");
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPixelSize(18);
    titleLabel->setFont(titleFont);
    titleLayout->addWidget(titleLabel);

    titleLayout->addStretch();

    // 本地模型管理入口：弹出 LocalModelDialog
    m_localModelBtn = new QPushButton(tr("本地模型"));
    m_localModelBtn->setObjectName("modelDialogLocalBtn");
    titleLayout->addWidget(m_localModelBtn);

    // 预留 AppDialogBase 关闭按钮的空间（右上角内侧 12px margin + 28px 按钮 + 12px 间距）
    titleLayout->addSpacing(52);

    connect(m_localModelBtn, &QPushButton::clicked, this, &ModelSelectDialog::onLocalModelClicked);

    mainLayout->addWidget(titleBar);

    // ========================================================================
    // 搜索框（含搜索图标容器）
    // ========================================================================
    auto* searchWidget = new QWidget();
    auto* searchLayout = new QHBoxLayout(searchWidget);
    searchLayout->setContentsMargins(20, 8, 20, 8);
    searchLayout->setSpacing(0);

    auto* searchContainer = new QWidget();
    searchContainer->setObjectName("modelSearchContainer");
    auto* searchContainerLayout = new QHBoxLayout(searchContainer);
    searchContainerLayout->setContentsMargins(0, 0, 0, 0);
    searchContainerLayout->setSpacing(0);

    auto* searchIconLabel = new QLabel();
    searchIconLabel->setObjectName("modelSearchIcon");
    searchIconLabel->setFixedWidth(36);
    searchIconLabel->setAlignment(Qt::AlignCenter);
    const bool darkTheme = (ThemeManager::instance()->currentTheme() != ThemeManager::LightTheme);
    const QColor searchIconColor(darkTheme ? "#808080" : "#bbbbbb");
    searchIconLabel->setPixmap(IconHelper::loadColoredIcon(":/Images/Icons/search.svg", searchIconColor, 15).pixmap(15, 15));
    searchContainerLayout->addWidget(searchIconLabel);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("modelSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索服务商或模型名称..."));
    m_searchEdit->setClearButtonEnabled(true);
    searchContainerLayout->addWidget(m_searchEdit, 1);

    searchLayout->addWidget(searchContainer);

    // 搜索过滤
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        // 防抖：等用户停止输入后再重建，避免每次按键都触发昂贵的整树重建
        m_searchTimer->start();
    });

    mainLayout->addWidget(searchWidget);

    // ========================================================================
    // 滚动内容区域
    // ========================================================================
    m_scrollArea = new QScrollArea();
    m_scrollArea->setObjectName("modelDialogScroll");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_contentWidget = new QWidget();
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(20, 0, 20, 20);
    m_contentLayout->setSpacing(0);

    m_scrollArea->setWidget(m_contentWidget);
    mainLayout->addWidget(m_scrollArea, 1);
}

void ModelSelectDialog::setModels(const QList<AiModel>& models)
{
    m_allModels = models;

    // 数据未发生任何变化（如重复打开对话框）时跳过整树重建。
    // 打开页面会调用 setModels，而每次重建都会销毁并重建数百个控件、触发全量重排，
    // 这是打开模型选择页的卡顿主因。
    if (buildSignature() == m_lastSignature)
        return;   // 数据无变化，直接复用现有控件，避免无效重建

    rebuildConfiguredSection();
    rebuildProviderSections();
    markRebuilt();
}

void ModelSelectDialog::setCurrentModel(const QString& modelId)
{
    m_currentModelId = modelId;
}

AiModel ModelSelectDialog::selectedModel() const
{
    for (const AiModel& model : m_allModels)
    {
        if (model.id == m_currentModelId)
        {
            return model;
        }
    }
    return AiModel();
}

// ============================================================================
// 渲染签名缓存（性能优化）
// ============================================================================

QString ModelSelectDialog::buildSignature() const
{
    // 汇总影响渲染结果的字段，快速判定数据是否真的变化（AiModel 无 operator==）。
    QString s = m_currentModelId
        + QLatin1Char('|')
        + (m_searchEdit ? m_searchEdit->text() : QString());
    for (const AiModel& m : m_allModels)
    {
        s += QLatin1Char('\n');
        s += m.id + QLatin1Char('|');
        s += m.displayName + QLatin1Char('|');
        s += m.apiUrl + QLatin1Char('|');
        s += m.isLocal ? QLatin1Char('L')
                       : (m.apiKey.isEmpty() ? QLatin1Char('N') : QLatin1Char('K'));
        s += m.isCustom ? QLatin1Char('C') : QLatin1Char('D');
    }
    return s;
}

void ModelSelectDialog::markRebuilt()
{
    m_lastSignature = buildSignature();
}

// ============================================================================
// 已配置模型区域
// ============================================================================

void ModelSelectDialog::rebuildConfiguredSection()
{
    // 移除旧区域
    if (m_configuredSection)
    {
        m_contentLayout->removeWidget(m_configuredSection);
        m_configuredSection->deleteLater();
        m_configuredSection = nullptr;
    }

    // 收集已配置 API Key 的模型（同时进行搜索过滤）
    QString searchText = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : QString();
    QList<AiModel> configured;
    for (const AiModel& model : m_allModels)
    {
        if (!model.apiKey.isEmpty())
        {
            if (searchText.isEmpty() ||
                model.displayName.toLower().contains(searchText) ||
                model.id.toLower().contains(searchText) ||
                providerKey(model).toLower().contains(searchText))
            {
                configured.append(model);
            }
        }
    }

    if (configured.isEmpty())
    {
        return;
    }

    m_configuredSection = new QWidget();
    m_configuredSection->setObjectName("modelConfiguredSection");
    m_configuredLayout = new QVBoxLayout(m_configuredSection);
    m_configuredLayout->setContentsMargins(0, 12, 0, 12);
    m_configuredLayout->setSpacing(6);

    auto* sectionTitle = new QLabel(tr("已配置的模型"));
    sectionTitle->setObjectName("modelSectionTitle");
    m_configuredLayout->addWidget(sectionTitle);

    for (const AiModel& model : configured)
    {
        auto* item = new QWidget();
        item->setObjectName("modelConfiguredItem");
        auto* itemLayout = new QHBoxLayout(item);
        itemLayout->setContentsMargins(12, 10, 12, 10);
        itemLayout->setSpacing(8);

        bool isCurrent = (model.id == m_currentModelId);
        auto* checkIcon = new QLabel();
        checkIcon->setObjectName(isCurrent ? "modelCheckActive" : "modelCheckInactive");
        checkIcon->setAlignment(Qt::AlignCenter);
        checkIcon->setFixedSize(20, 20);
        if (isCurrent)
        {
            checkIcon->setPixmap(IconHelper::loadColoredIcon(":/Images/Icons/check.svg", QColor("#ffffff"), 12).pixmap(12, 12));
        }
        itemLayout->addWidget(checkIcon);

        auto* infoLayout = new QVBoxLayout();
        infoLayout->setSpacing(2);

        auto* nameLabel = new QLabel(model.displayName);
        nameLabel->setObjectName("modelConfigName");
        infoLayout->addWidget(nameLabel);

        // 本地模型显示来源信息，云端模型显示 API Key 占位
        auto* keyLabel = new QLabel(
            model.isLocal ? tr("本地模型 · Ollama (端口 11434)")
                          : tr("API Key: %1").arg("••••••••"));
        keyLabel->setObjectName("modelConfigKey");
        infoLayout->addWidget(keyLabel);

        itemLayout->addLayout(infoLayout, 1);

        auto* useBtn = new QPushButton(tr("使用"));
        useBtn->setObjectName("modelUseBtn");
        useBtn->setCursor(Qt::PointingHandCursor);
        useBtn->setFixedWidth(56);
        QString modelId = model.id;
        connect(useBtn, &QPushButton::clicked, this, [this, modelId]() {
            m_currentModelId = modelId;
            emit modelSelected(selectedModel());
        });
        itemLayout->addWidget(useBtn);

        // 本地模型不显示"删除"按钮（删除应通过本地模型对话框在 Ollama 中操作）
        if (!model.isLocal)
        {
            auto* delBtn = new QPushButton();
            delBtn->setObjectName("modelDelBtn");
            delBtn->setCursor(Qt::PointingHandCursor);
            delBtn->setFixedSize(28, 28);
            delBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/close.svg", QColor("#f44336"), 14));
            delBtn->setIconSize(QSize(14, 14));
            connect(delBtn, &QPushButton::clicked, this, [this, modelId]() {
                for (int i = 0; i < m_allModels.size(); ++i)
                {
                    if (m_allModels[i].id == modelId)
                    {
                        m_allModels[i].apiKey = "";
                        break;
                    }
                }
                emit modelRemoved(modelId);
                rebuildConfiguredSection();
                rebuildProviderSections();
                markRebuilt();
            });
            itemLayout->addWidget(delBtn);
        }

        m_configuredLayout->addWidget(item);
    }

    m_contentLayout->insertWidget(0, m_configuredSection);
}

// ============================================================================
// 服务商区域
// ============================================================================

void ModelSelectDialog::rebuildProviderSections()
{
    // 保留配置表单和 JSON 区域，避免重建时被销毁导致用户输入丢失
    if (m_configForm)
    {
        m_configForm->setParent(this);
        m_configForm->hide();
    }
    if (m_jsonSection)
    {
        m_jsonSection->setParent(this);
    }

    if (m_providerSection)
    {
        m_contentLayout->removeWidget(m_providerSection);
        m_providerSection->deleteLater();
        m_providerSection = nullptr;
        m_providerContents.clear();
        m_providerHeaders.clear();
    }

    // 搜索过滤
    QString searchText = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : QString();
    bool hasSearch = !searchText.isEmpty();

    m_providerSection = new QWidget();
    m_providerSection->setObjectName("modelProviderSection");
    m_providerLayout = new QVBoxLayout(m_providerSection);
    m_providerLayout->setContentsMargins(0, 12, 0, 12);
    m_providerLayout->setSpacing(8);

    if (!hasSearch)
    {
        auto* sectionTitle = new QLabel(tr("服务商"));
        sectionTitle->setObjectName("modelSectionTitle");
        m_providerLayout->addWidget(sectionTitle);
    }

    // 按服务商分组（跳过本地模型：本地模型只能在 LocalModelDialog 中管理）
    QMap<QString, QList<AiModel>> allProviderGroups;
    for (const AiModel& model : m_allModels)
    {
        if (model.isLocal)
        {
            continue;
        }
        QString key = providerKey(model);
        allProviderGroups[key].append(model);
    }

    // 从设置页读取服务商配置并应用到模型
    QSettings settings;
    QMap<QString, QPair<QString, QString>> providerConfigs;
    struct ProviderConfigInfo {
        QString key;
        QString configKey;
    };
    QList<ProviderConfigInfo> configInfos = {
        {"DeepSeek", "ai/provider_deepseek"},
        {"OpenAI", "ai/provider_openai"},
        {"Anthropic", "ai/provider_anthropic"},
        {"Gemini", "ai/provider_gemini"},
        {"OpenRouter", "ai/provider_openrouter"},
        {"GLM", "ai/provider_glm"},
        {"Qwen", "ai/provider_qwen"},
        {"ERNIE", "ai/provider_ernie"},
    };
    for (const ProviderConfigInfo &info : configInfos) {
        QString url = settings.value(info.configKey + "/url", "").toString();
        QString key = settings.value(info.configKey + "/key", "").toString();
        if (!url.isEmpty() || !key.isEmpty()) {
            providerConfigs[info.key] = qMakePair(url, key);
        }
    }

    // 将服务商配置应用到模型
    for (AiModel &model : m_allModels) {
        QString pKey = providerKey(model);
        if (providerConfigs.contains(pKey)) {
            auto &config = providerConfigs[pKey];
            if (!config.first.isEmpty()) {
                model.apiUrl = config.first;
            }
            if (!config.second.isEmpty()) {
                model.apiKey = config.second;
            }
        }
    }

    // ========================================================================
    // 跳转到设置页按钮
    // ========================================================================
    auto* openSettingsBtn = new QPushButton(tr("打开 AI 助手设置"));
    openSettingsBtn->setObjectName("openSettingsBtn");
    openSettingsBtn->setCursor(Qt::PointingHandCursor);
    connect(openSettingsBtn, &QPushButton::clicked, this, [this]() {
        emit openSettingsRequested();
        accept();
    });
    m_providerLayout->addWidget(openSettingsBtn);


    int insertPos = m_contentLayout->count();
    m_contentLayout->insertWidget(insertPos, m_providerSection);
}

QString ModelSelectDialog::providerKey(const AiModel& model) const
{
    // 从 displayName 或 id 提取服务商名称
    QString name = model.displayName;
    // 两词服务商名（如 "OpenCode Go" / "OpenCode Zen"）
    if (name.startsWith(QStringLiteral("OpenCode Go")))
    {
        return QStringLiteral("OpenCode Go");
    }
    if (name.startsWith(QStringLiteral("OpenCode Zen")))
    {
        return QStringLiteral("OpenCode Zen");
    }
    if (name.contains(" "))
    {
        name = name.section(" ", 0, 0);
    }
    if (name.contains("("))
    {
        name = name.section("(", 0, 0).trimmed();
    }
    return name;
}

// ============================================================================
// 事件处理
// ============================================================================

void ModelSelectDialog::onProviderToggled(const QString& key)
{
    if (m_providerContents.contains(key))
    {
        QWidget* content = m_providerContents[key];
        content->setVisible(!content->isVisible());
    }
}

void ModelSelectDialog::onModelClicked(const AiModel& model)
{
    showConfigForm(model);
}

void ModelSelectDialog::openProviderWebsite(const QString& providerKey)
{
    // 优先从模型数据中查找 websiteUrl
    for (const AiModel& model : m_allModels)
    {
        if (this->providerKey(model) == providerKey && !model.websiteUrl.isEmpty())
        {
            QDesktopServices::openUrl(QUrl(model.websiteUrl));
            return;
        }
    }

    // 回退：硬编码映射（与 AiChatPage::initDefaultModels 中的 providerUrls 保持一致）
    static const QMap<QString, QString> websites = {
        // 国际服务商
        {"AI21",        "https://www.ai21.com"},
        {"Amazon",      "https://aws.amazon.com/bedrock"},
        {"Anthropic",   "https://www.anthropic.com"},
        {"Cerebras",    "https://www.cerebras.ai"},
        {"Cohere",      "https://cohere.com"},
        {"DeepInfra",   "https://deepinfra.com"},
        {"DeepSeek",    "https://www.deepseek.com"},
        {"Fireworks",   "https://fireworks.ai"},
        {"Gemini",      "https://ai.google.dev"},
        {"Groq",        "https://groq.com"},
        {"HuggingFace", "https://huggingface.co"},
        {"Inflection",  "https://inflection.ai"},
        {"Lepton",      "https://lepton.ai"},
        {"Meta",        "https://llama.meta.com"},
        {"MiniMax",     "https://www.minimaxi.com"},
        {"Mistral",     "https://mistral.ai"},
        {"NVIDIA",      "https://build.nvidia.com"},
        {"OpenAI",      "https://platform.openai.com"},
        {"OpenCode Go", "https://opencode.ai"},
        {"OpenCode Zen","https://opencode.ai"},
        {"OpenRouter",  "https://openrouter.ai"},
        {"Perplexity",  "https://www.perplexity.ai"},
        {"Replicate",   "https://replicate.com"},
        {"Scaleway",    "https://www.scaleway.com"},
        {"Together",    "https://www.together.ai"},
        // 国内服务商
        {"Baichuan",    "https://www.baichuan-ai.com"},
        {"ChatGLM",     "https://open.bigmodel.cn"},
        {"CloudWalk",   "https://www.cloudwalk.com"},
        {"Doubao",      "https://www.volcengine.com/product/doubao"},
        {"ERNIE",       "https://yiyan.baidu.com"},
        {"GLM",         "https://open.bigmodel.cn"},
        {"Grok",        "https://x.ai"},
        {"Hunyuan",     "https://cloud.tencent.com/product/hunyuan"},
        {"InternLM",    "https://internlm-chat.intern-ai.org.cn"},
        {"Kimi",        "https://www.moonshot.cn"},
        {"MiniCPM",     "https://modelbest.cn"},
        {"Moonshot",    "https://www.moonshot.cn"},
        {"OrionStar",   "https://www.orionstar.com"},
        {"Pangu",       "https://www.huaweicloud.com/product/pangu.html"},
        {"Qwen",        "https://tongyi.aliyun.com"},
        {"SenseChat",   "https://platform.sensenova.cn"},
        {"Spark",       "https://xinghuo.xfyun.cn"},
        {"Step",        "https://www.stepfun.com"},
        {"TRS",         "https://www.trs.com.cn"},
        {"Unisound",    "https://www.unisound.com"},
        {"Yi",          "https://www.lingyiwanwu.com"},
        {"元象",         "https://www.xverse.cn"},
        {"天工",         "https://www.tiangong.cn"},
        {"快意",         "https://www.kuaishou.com"},
        {"360",         "https://ai.360.com"},
    };

    QString url = websites.value(providerKey);
    if (url.isEmpty())
    {
        // 尝试根据服务商名称自动生成 URL
        QString safeName = providerKey.toLower().remove(' ');
        url = QString("https://%1.com").arg(safeName);
    }
    QDesktopServices::openUrl(QUrl(url));
}

void ModelSelectDialog::showConfigForm(const AiModel& model)
{
    m_configModel = model;
    m_configNameEdit->setText(model.displayName);
    m_configIdEdit->setText(model.id);
    m_configUrlEdit->setText(model.apiUrl);
    m_configKeyEdit->setText(model.apiKey);
    if (m_configThinkingCheck)
    {
        m_configThinkingCheck->setChecked(model.supportsThinking);
    }
    m_configForm->setVisible(true);
    m_configForm->raise();

    // 延迟滚动到底部，等待布局完成后再读取正确的 maximum
    QTimer::singleShot(0, this, [this]() {
        QScrollBar* sb = m_scrollArea->verticalScrollBar();
        sb->setValue(sb->maximum());
    });
}

void ModelSelectDialog::hideConfigForm()
{
    m_configForm->setVisible(false);
}

void ModelSelectDialog::onSaveConfig()
{
    QString name = m_configNameEdit->text().trimmed();
    QString id = m_configIdEdit->text().trimmed();
    QString url = m_configUrlEdit->text().trimmed();
    QString key = m_configKeyEdit->text().trimmed();

    if (name.isEmpty() || id.isEmpty() || url.isEmpty())
    {
        return;
    }

    AiModel model;
    model.id = id;
    model.displayName = name;
    model.apiUrl = url;
    model.apiKey = key;
    model.websiteUrl = m_configModel.websiteUrl;
    model.isCustom = true;
    model.supportsThinking = m_configThinkingCheck ? m_configThinkingCheck->isChecked()
                                                    : m_configModel.supportsThinking;

    // 更新或添加到列表
    bool found = false;
    for (int i = 0; i < m_allModels.size(); ++i)
    {
        if (m_allModels[i].id == id)
        {
            m_allModels[i] = model;
            found = true;
            break;
        }
    }
    if (!found)
    {
        m_allModels.append(model);
    }

    m_currentModelId = id;
    m_configForm->setVisible(false);

    emit modelConfigured(model);
    emit modelSelected(model);
    rebuildConfiguredSection();
    rebuildProviderSections();
    markRebuilt();
}

void ModelSelectDialog::onCustomJsonClicked()
{
    m_jsonVisible = !m_jsonVisible;
    m_jsonSection->setVisible(m_jsonVisible);

    if (m_jsonVisible)
    {
        // 只输出自定义模型的 JSON（内置模型不导出）
        QJsonArray arr;
        for (const AiModel& model : m_allModels)
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
            arr.append(obj);
        }
        QJsonDocument doc(arr);
        m_jsonEdit->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
    }

    QScrollBar* sb = m_scrollArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void ModelSelectDialog::onImportJsonFile()
{
    QString filePath = AppFileDialog::getOpenFileName(
        this, tr("导入模型配置"), QString(),
        tr("JSON 文件 (*.json);;所有文件 (*)"));
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError)
        return;

    int added = 0;
    auto addModel = [&](const QJsonObject &obj) {
        AiModel model;
        model.id = obj["id"].toString();
        model.displayName = obj["displayName"].toString();
        model.apiUrl = obj["apiUrl"].toString();
        model.apiKey = obj["apiKey"].toString();
        model.websiteUrl = obj["websiteUrl"].toString();
        model.supportsThinking = obj["supportsThinking"].toBool(false);
        model.isCustom = true;

        if (model.id.isEmpty() || model.apiUrl.isEmpty())
            return;

        for (const AiModel &m : m_allModels)
            if (m.id == model.id && m.apiUrl == model.apiUrl)
                return;

        m_allModels.append(model);
        emit modelConfigured(model);
        ++added;
    };

    if (doc.isArray())
    {
        const QJsonArray arr = doc.array();
        for (const QJsonValue &val : arr)
            addModel(val.toObject());
    }
    else if (doc.isObject())
    {
        addModel(doc.object());
    }

    if (added > 0)
    {
        rebuildConfiguredSection();
        rebuildProviderSections();
        markRebuilt();
    }
}

void ModelSelectDialog::onExportJsonFile()
{
    QString filePath = AppFileDialog::getSaveFileName(
        this, tr("导出模型配置"), QString(),
        tr("JSON 文件 (*.json);;所有文件 (*)"));
    if (filePath.isEmpty())
        return;

    QJsonArray arr;
    for (const AiModel &model : m_allModels)
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
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

void ModelSelectDialog::onLocalModelClicked()
{
    // 首次打开时创建独立窗口，后续复用
    if (!m_localModelDialog)
    {
        m_localModelDialog = new LocalModelDialog(this);
        m_localModelDialog->setAttribute(Qt::WA_DeleteOnClose, false);

        // 用户在本地模型对话框中点击"使用此模型"：转发为标准 modelSelected 信号
        // 让 AiChatPage 复用现有 modelSelected 处理逻辑更新 m_currentModel
        connect(m_localModelDialog, &LocalModelDialog::localModelSelected,
                this, [this](const AiModel& model) {
            // 同步到本地缓存：若已存在同 id 则更新，否则追加
            bool found = false;
            for (AiModel& m : m_allModels)
            {
                if (m.id == model.id)
                {
                    m = model;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                m_allModels.append(model);
            }
            emit modelSelected(model);
            // 刷新已配置区域，让本地模型显示在"已配置的模型"中
            rebuildConfiguredSection();
            markRebuilt();
        });

        // 本地模型清单发生变化（拉取/删除完成）：通知上层 AiChatPage 刷新 m_models
        connect(m_localModelDialog, &LocalModelDialog::localModelsChanged,
                this, [this]() {
            // 把 LocalModelDialog 当前已知的本地模型同步到 m_allModels：
            // 1. 删除 m_allModels 中所有旧的 isLocal=true 条目
            // 2. 追加 LocalModelDialog::localAiModels() 的最新结果
            QList<AiModel> nonLocal;
            for (const AiModel& m : m_allModels)
            {
                if (!m.isLocal)
                {
                    nonLocal.append(m);
                }
            }
            m_allModels = nonLocal + m_localModelDialog->localAiModels();
            rebuildConfiguredSection();
            markRebuilt();
            emit localModelsChanged();
        });
    }

    m_localModelDialog->show();
    m_localModelDialog->raise();
    m_localModelDialog->activateWindow();
}

bool ModelSelectDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick)
    {
        auto* btn = qobject_cast<QPushButton*>(obj);
        if (btn)
        {
            // 仅双击模型项时打开官网；服务商标题与热门按钮的双击不再触发跳转
            if (btn->objectName() == "modelProviderItem")
            {
                QString key = btn->property("providerKey").toString();
                if (!key.isEmpty())
                {
                    openProviderWebsite(key);
                    return true;
                }
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}