/**
 * @file   ModelProviderSettingsPanel.cpp
 * @brief  模型服务商设置面板实现（双栏布局：左列表 + 右内联配置）
 * @author BlockBox Team
 * @date   2026-09-18
 */
#include "components/ModelProviderSettingsPanel.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QColor>
#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSettings>
#include <QSize>
#include <QStyle>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <functional>

#include "components/AppMessageBox.h"
#include "components/NotificationManager.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/StyleKit.h"

namespace
{
    constexpr char kCustomProvidersKey[] = "ai/customProviders";
    constexpr char kFlatCustomModelsKey[] = "ai/customModels";
    constexpr char kAddRowId[] = "__add__";

    QString settingsStoreValue(const char *key)
    {
        QSettings s(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
        return s.value(QLatin1String(key)).toString();
    }

    void setSettingsStoreValue(const char *key, const QString &value)
    {
        QSettings s(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
        s.setValue(QLatin1String(key), value);
    }

    QJsonArray parseJsonArray(const QString &json)
    {
        if (json.isEmpty())
            return {};
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isArray())
            return {};
        return doc.array();
    }

    QColor tokenColor(const QString &token)
    {
        return QColor(StyleKit::resolve(token));
    }

    /// 设置动态属性后重抛光，让 QSS 属性选择器立即生效
    void repolish(QWidget *w)
    {
        w->style()->unpolish(w);
        w->style()->polish(w);
    }
} // namespace

ModelProviderSettingsPanel::ModelProviderSettingsPanel(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("mpsPanel"));
    setFixedHeight(560);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    // ── 左栏：服务商列表 ──
    auto *sideBody = new QWidget();
    sideBody->setObjectName(QStringLiteral("mpsSideBody"));
    sideBody->setAttribute(Qt::WA_StyledBackground, true);
    sideBody->setFixedWidth(220);
    auto *sideBodyLayout = new QVBoxLayout(sideBody);
    sideBodyLayout->setContentsMargins(8, 8, 8, 8);
    sideBodyLayout->setSpacing(4);

    auto *sideScroll = new QScrollArea();
    sideScroll->setObjectName(QStringLiteral("mpsSideScroll"));
    sideScroll->setFrameShape(QFrame::NoFrame);
    sideScroll->setWidgetResizable(true);
    sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_sideList = new QWidget();
    m_sideList->setObjectName(QStringLiteral("mpsSideList"));
    m_sideLayout = new QVBoxLayout(m_sideList);
    m_sideLayout->setContentsMargins(0, 0, 0, 0);
    m_sideLayout->setSpacing(2);
    sideScroll->setWidget(m_sideList);
    sideBodyLayout->addWidget(sideScroll, 1);

    m_rowGroup = new QButtonGroup(this);
    m_rowGroup->setExclusive(true);

    m_addProviderBtn = new QPushButton(tr("+ 添加供应商"));
    m_addProviderBtn->setCursor(Qt::PointingHandCursor);
    m_addProviderBtn->setCheckable(true);
    m_addProviderBtn->setStyleSheet(StyleKit::outlineButton());
    m_rowGroup->addButton(m_addProviderBtn);
    connect(m_addProviderBtn, &QPushButton::clicked, this, [this]() {
        selectProvider(QLatin1String(kAddRowId));
    });
    sideBodyLayout->addWidget(m_addProviderBtn);

    root->addWidget(sideBody);

    // ── 右栏：内联页面 ──
    m_stack = new QStackedWidget();
    m_addPage = buildAddProviderPage();
    m_stack->addWidget(m_addPage);
    root->addWidget(m_stack, 1);

    rebuildProviderList();
    selectProvider(QStringLiteral("preset:DeepSeek"));
}

bool ModelProviderSettingsPanel::eventFilter(QObject *obj, QEvent *event)
{
    // 下拉框收起时吞掉滚轮，避免滚动页面时误改选项（与设置页其他下拉框行为一致）
    if (event->type() == QEvent::Wheel) {
        if (auto *combo = qobject_cast<QComboBox *>(obj); combo && !combo->view()->isVisible())
            return true;
    }
    return QFrame::eventFilter(obj, event);
}

// ============================================================================
// 内置服务商定义
// ============================================================================

QList<ModelProviderSettingsPanel::PresetInfo> ModelProviderSettingsPanel::presetProviders()
{
    return {
        {QStringLiteral("DeepSeek"), tr("DeepSeek"), QStringLiteral("https://api.deepseek.com/v1"), QStringLiteral("ai/provider_deepseek")},
        {QStringLiteral("OpenAI"), tr("OpenAI"), QStringLiteral("https://api.openai.com/v1"), QStringLiteral("ai/provider_openai")},
        {QStringLiteral("Anthropic"), tr("Anthropic"), QStringLiteral("https://api.anthropic.com"), QStringLiteral("ai/provider_anthropic")},
        {QStringLiteral("Gemini"), tr("Google Gemini"), QStringLiteral("https://generativelanguage.googleapis.com/v1"), QStringLiteral("ai/provider_gemini")},
        {QStringLiteral("OpenRouter"), tr("OpenRouter"), QStringLiteral("https://openrouter.ai/api/v1"), QStringLiteral("ai/provider_openrouter")},
        {QStringLiteral("GLM"), tr("智谱 GLM"), QStringLiteral("https://open.bigmodel.cn/api/paas/v4"), QStringLiteral("ai/provider_glm")},
        {QStringLiteral("Qwen"), tr("通义千问"), QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1"), QStringLiteral("ai/provider_qwen")},
        {QStringLiteral("ERNIE"), tr("文心一言"), QStringLiteral("https://aip.baidubce.com"), QStringLiteral("ai/provider_ernie")},
    };
}

QString ModelProviderSettingsPanel::presetUrlHint(const QString &key)
{
    // 与预设模型清单（:/resources/ai_models.json）匹配的 URL 子串
    if (key == QLatin1String("OpenAI")) return QStringLiteral("openai.com");
    if (key == QLatin1String("DeepSeek")) return QStringLiteral("deepseek.com");
    if (key == QLatin1String("Anthropic")) return QStringLiteral("anthropic.com");
    if (key == QLatin1String("Gemini")) return QStringLiteral("googleapis.com");
    if (key == QLatin1String("OpenRouter")) return QStringLiteral("openrouter.ai");
    if (key == QLatin1String("GLM")) return QStringLiteral("bigmodel.cn");
    if (key == QLatin1String("Qwen")) return QStringLiteral("dashscope.aliyuncs");
    if (key == QLatin1String("ERNIE")) return QStringLiteral("baidubce.com");
    return QString();
}

// ============================================================================
// 数据访问
// ============================================================================

QJsonArray ModelProviderSettingsPanel::loadCustomProviders() const
{
    return parseJsonArray(settingsStoreValue(kCustomProvidersKey));
}

void ModelProviderSettingsPanel::saveCustomProviders(const QJsonArray &providers)
{
    setSettingsStoreValue(kCustomProvidersKey,
        QString::fromUtf8(QJsonDocument(providers).toJson(QJsonDocument::Compact)));
}

QJsonArray ModelProviderSettingsPanel::loadFlatCustomModels() const
{
    return parseJsonArray(settingsStoreValue(kFlatCustomModelsKey));
}

void ModelProviderSettingsPanel::saveFlatCustomModels(const QJsonArray &models)
{
    setSettingsStoreValue(kFlatCustomModelsKey,
        QString::fromUtf8(QJsonDocument(models).toJson(QJsonDocument::Compact)));
}

void ModelProviderSettingsPanel::syncFlattenedCustomModels()
{
    // ai/customModels 中无 providerId 标记的条目（旧数据 / 内置服务商的自定义模型）原样保留，
    // 带 providerId 的条目一律由自定义供应商记录重新生成，避免两处状态漂移。
    QJsonArray kept;
    for (const QJsonValue &v : loadFlatCustomModels()) {
        const QJsonObject e = v.toObject();
        if (!e.contains(QStringLiteral("providerId")))
            kept.append(e);
    }

    for (const QJsonValue &pv : loadCustomProviders()) {
        const QJsonObject prov = pv.toObject();
        const QString pid = prov[QStringLiteral("id")].toString();
        const QString provName = prov[QStringLiteral("name")].toString();
        const QString url = prov[QStringLiteral("apiUrl")].toString();
        const QString key = prov[QStringLiteral("apiKey")].toString();
        const QString fmt = prov[QStringLiteral("apiFormat")].toString(QStringLiteral("openai"));
        // 展平后的显示名带供应商前缀，ModelSelectDialog 按首词分组可正确归组
        for (const QJsonValue &mv : prov[QStringLiteral("models")].toArray()) {
            const QJsonObject m = mv.toObject();
            QJsonObject entry;
            entry[QStringLiteral("id")] = m[QStringLiteral("id")].toString();
            entry[QStringLiteral("displayName")] =
                provName + QLatin1Char(' ') + m[QStringLiteral("displayName")].toString();
            entry[QStringLiteral("apiUrl")] = url;
            entry[QStringLiteral("apiKey")] = key;
            entry[QStringLiteral("apiFormat")] = fmt;
            entry[QStringLiteral("supportsThinking")] = m[QStringLiteral("supportsThinking")].toBool(false);
            entry[QStringLiteral("isCustom")] = true;
            entry[QStringLiteral("providerId")] = pid;
            kept.append(entry);
        }
    }
    saveFlatCustomModels(kept);
}

void ModelProviderSettingsPanel::appendFlatCustomModel(const QJsonObject &entry)
{
    QJsonArray arr = loadFlatCustomModels();
    arr.append(entry);
    saveFlatCustomModels(arr);
}

// ============================================================================
// 左栏列表
// ============================================================================

QLabel *ModelProviderSettingsPanel::createGroupLabel(const QString &text) const
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("mpsGroupLabel"));
    return label;
}

QLabel *ModelProviderSettingsPanel::createStatusDot(bool active) const
{
    auto *dot = new QLabel();
    dot->setObjectName(QStringLiteral("mpsRowDot"));
    dot->setFixedSize(8, 8);
    dot->setStyleSheet(StyleKit::resolve(active
        ? QStringLiteral("QLabel#mpsRowDot { background: @SUCCESS@; border-radius: 4px; }")
        : QStringLiteral("QLabel#mpsRowDot { background: @TEXT_DISABLED@; border-radius: 4px; }")));
    dot->setToolTip(active ? tr("已配置") : tr("未配置"));
    return dot;
}

QPushButton *ModelProviderSettingsPanel::createProviderRow(const QString &name, bool active, const QString &rowId)
{
    auto *row = new QPushButton();
    row->setObjectName(QStringLiteral("mpsProviderRow"));
    row->setCursor(Qt::PointingHandCursor);
    row->setCheckable(true);
    row->setChecked(rowId == m_currentId);

    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(2, 0, 2, 0);
    rowLayout->setSpacing(6);
    auto *nameLabel = new QLabel(name, row);
    rowLayout->addWidget(nameLabel, 1);
    rowLayout->addWidget(createStatusDot(active));

    m_rowGroup->addButton(row);
    connect(row, &QPushButton::clicked, this, [this, rowId]() {
        selectProvider(rowId);
    });
    return row;
}

void ModelProviderSettingsPanel::rebuildProviderList()
{
    while (QLayoutItem *item = m_sideLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    SettingsManager *settings = SettingsManager::instance();

    m_sideLayout->addWidget(createGroupLabel(tr("服务商")));
    for (const PresetInfo &info : presetProviders()) {
        const bool active = !settings->getProperty(info.configKey + QStringLiteral("/key")).toString().isEmpty();
        m_sideLayout->addWidget(createProviderRow(info.name, active, QStringLiteral("preset:") + info.key));
    }

    m_sideLayout->addWidget(createGroupLabel(tr("自定义供应商")));
    const QJsonArray providers = loadCustomProviders();
    if (providers.isEmpty()) {
        auto *hint = new QLabel(tr("暂无自定义供应商"));
        hint->setObjectName(QStringLiteral("mpsEmptyHint"));
        m_sideLayout->addWidget(hint);
    }
    for (const QJsonValue &pv : providers) {
        const QJsonObject prov = pv.toObject();
        const bool active = !prov[QStringLiteral("apiKey")].toString().isEmpty()
            && !prov[QStringLiteral("models")].toArray().isEmpty();
        m_sideLayout->addWidget(createProviderRow(
            prov[QStringLiteral("name")].toString(), active,
            QStringLiteral("custom:") + prov[QStringLiteral("id")].toString()));
    }

    m_sideLayout->addStretch();
    m_addProviderBtn->setChecked(m_currentId == QLatin1String(kAddRowId));
}

void ModelProviderSettingsPanel::selectProvider(const QString &rowId, bool force)
{
    if (!force && rowId == m_currentId)
        return;
    m_currentId = rowId;

    if (m_page) {
        m_stack->removeWidget(m_page);
        m_page->deleteLater();
        m_page = nullptr;
    }

    if (rowId != QLatin1String(kAddRowId)) {
        // 服务商页每次选中都重建，保证反映最新配置
        if (rowId.startsWith(QStringLiteral("preset:"))) {
            const QString key = rowId.mid(7);
            for (const PresetInfo &info : presetProviders()) {
                if (info.key == key) {
                    m_page = wrapScroll(buildPresetPage(info));
                    break;
                }
            }
        } else if (rowId.startsWith(QStringLiteral("custom:"))) {
            const QString pid = rowId.mid(7);
            for (const QJsonValue &pv : loadCustomProviders()) {
                const QJsonObject prov = pv.toObject();
                if (prov[QStringLiteral("id")].toString() == pid) {
                    m_page = wrapScroll(buildCustomProviderPage(prov));
                    break;
                }
            }
        }
        if (m_page)
            m_stack->addWidget(m_page);
    }

    if (m_page || rowId == QLatin1String(kAddRowId)) {
        if (m_page)
            m_stack->setCurrentWidget(m_page);
        else
            m_stack->setCurrentWidget(m_addPage);
        rebuildProviderList();
    } else {
        // 目标已不存在（被删除等）：回落到第一个内置服务商
        m_currentId.clear();
        selectProvider(QStringLiteral("preset:DeepSeek"), true);
    }
}

// ============================================================================
// 通用小件
// ============================================================================

QWidget *ModelProviderSettingsPanel::wrapScroll(QWidget *content) const
{
    auto *scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("mpsPageScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    return scroll;
}

QHBoxLayout *ModelProviderSettingsPanel::createFormRow(const QString &labelText, QWidget *field, QWidget *trailing) const
{
    auto *rowLayout = new QHBoxLayout();
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(10);

    auto *label = new QLabel(labelText);
    label->setObjectName(QStringLiteral("mpsFieldLabel"));
    label->setFixedWidth(72);
    rowLayout->addWidget(label);
    rowLayout->addWidget(field, 1);
    if (trailing)
        rowLayout->addWidget(trailing);
    return rowLayout;
}

QWidget *ModelProviderSettingsPanel::createModelListCard(QVBoxLayout *&rowsLayout) const
{
    auto *card = new QWidget();
    card->setObjectName(QStringLiteral("mpsModelCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    rowsLayout = new QVBoxLayout(card);
    rowsLayout->setContentsMargins(8, 8, 8, 8);
    rowsLayout->setSpacing(2);
    return card;
}

QWidget *ModelProviderSettingsPanel::createModelRow(const QString &name, const QString &subtitle, bool checked,
                                                    const std::function<void()> &onClick,
                                                    const std::function<void()> &onRemove) const
{
    auto *row = new QPushButton();
    row->setObjectName(QStringLiteral("mpsModelRow"));
    row->setCursor(Qt::PointingHandCursor);
    row->setFlat(true);
    // QPushButton 的 sizeHint 不感知子布局，两行内容（名称 + ID）需要显式最小高度
    row->setMinimumHeight((subtitle.isEmpty() || subtitle == name) ? 38 : 52);

    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(6, 4, 6, 4);
    rowLayout->setSpacing(8);

    auto *checkIcon = new QLabel(row);
    checkIcon->setFixedSize(18, 18);
    checkIcon->setAlignment(Qt::AlignCenter);
    if (checked) {
        checkIcon->setPixmap(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/check.svg"), tokenColor(QStringLiteral("@PRIMARY@")), 12)
                                 .pixmap(12, 12));
    }
    rowLayout->addWidget(checkIcon);

    auto *infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(1);
    auto *nameLabel = new QLabel(name, row);
    nameLabel->setObjectName(QStringLiteral("mpsModelName"));
    infoLayout->addWidget(nameLabel);
    if (!subtitle.isEmpty() && subtitle != name) {
        auto *subLabel = new QLabel(subtitle, row);
        subLabel->setObjectName(QStringLiteral("mpsModelId"));
        subLabel->setToolTip(subtitle);
        infoLayout->addWidget(subLabel);
    }
    rowLayout->addLayout(infoLayout, 1);

    if (onRemove) {
        auto *delBtn = new QToolButton(row);
        delBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/close.svg"), tokenColor(QStringLiteral("@DANGER@")), 14));
        delBtn->setIconSize(QSize(14, 14));
        delBtn->setAutoRaise(true);
        delBtn->setToolTip(tr("删除"));
        connect(delBtn, &QToolButton::clicked, row, onRemove);
        rowLayout->addWidget(delBtn);
    }

    if (onClick)
        connect(row, &QPushButton::clicked, row, onClick);
    return row;
}

// ============================================================================
// 内置服务商页
// ============================================================================

QWidget *ModelProviderSettingsPanel::buildPresetPage(const PresetInfo &info)
{
    SettingsManager *settings = SettingsManager::instance();

    auto *page = new QWidget();
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(4, 0, 8, 0);
    pageLayout->setSpacing(10);

    auto *title = new QLabel(info.name);
    title->setStyleSheet(StyleKit::pageTitle());
    pageLayout->addWidget(title);

    auto *desc = new QLabel(
        tr("配置 %1 的 API 密钥与端点。配置后在模型选择窗口即可使用对应模型。").arg(info.name));
    desc->setStyleSheet(StyleKit::mutedLabel());
    desc->setWordWrap(true);
    pageLayout->addWidget(desc);

    const QString savedKey = settings->getProperty(info.configKey + QStringLiteral("/key")).toString();
    const QString savedUrl = settings->getProperty(info.configKey + QStringLiteral("/url")).toString();
    const QString effectiveUrl = savedUrl.isEmpty() ? info.defaultUrl : savedUrl;

    // ── API Key ──
    auto *keyEdit = new QLineEdit(savedKey);
    keyEdit->setPlaceholderText(tr("输入 API Key"));
    keyEdit->setEchoMode(QLineEdit::Password);

    auto *toggleBtn = new QToolButton();
    toggleBtn->setText(tr("显示"));
    toggleBtn->setCursor(Qt::PointingHandCursor);
    toggleBtn->setStyleSheet(StyleKit::ghostButton());
    connect(toggleBtn, &QToolButton::clicked, toggleBtn, [keyEdit, toggleBtn]() {
        const bool hidden = keyEdit->echoMode() == QLineEdit::Password;
        keyEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        toggleBtn->setText(hidden ? tr("隐藏") : tr("显示"));
    });

    pageLayout->addLayout(createFormRow(tr("API Key"), keyEdit, toggleBtn));
    connect(keyEdit, &QLineEdit::editingFinished, this, [this, info, keyEdit]() {
        SettingsManager::instance()->setProperty(info.configKey + QStringLiteral("/key"),
            keyEdit->text().trimmed());
        rebuildProviderList();
    });

    // ── API 地址 ──
    auto *urlEdit = new QLineEdit(effectiveUrl);
    urlEdit->setPlaceholderText(info.defaultUrl);

    auto *resetUrlBtn = new QToolButton();
    resetUrlBtn->setText(tr("重置"));
    resetUrlBtn->setCursor(Qt::PointingHandCursor);
    resetUrlBtn->setStyleSheet(StyleKit::ghostButton());
    resetUrlBtn->setVisible(!savedUrl.isEmpty() && savedUrl != info.defaultUrl);
    connect(resetUrlBtn, &QToolButton::clicked, this, [this, info, urlEdit, resetUrlBtn]() {
        SettingsManager::instance()->setProperty(info.configKey + QStringLiteral("/url"), QString());
        urlEdit->setText(info.defaultUrl);
        resetUrlBtn->hide();
    });

    pageLayout->addLayout(createFormRow(tr("API 地址"), urlEdit, resetUrlBtn));
    connect(urlEdit, &QLineEdit::editingFinished, this, [this, info, urlEdit, resetUrlBtn]() {
        const QString url = urlEdit->text().trimmed();
        const bool isDefault = url.isEmpty() || url == info.defaultUrl;
        SettingsManager::instance()->setProperty(info.configKey + QStringLiteral("/url"),
            isDefault ? QString() : url);
        if (url.isEmpty())
            urlEdit->setText(info.defaultUrl);
        resetUrlBtn->setVisible(!isDefault);
    });

    // ── 模型列表 ──
    const QString defaultModelId =
        settings->getProperty(info.configKey + QStringLiteral("/selectedModel")).toString();

    auto *listTitle = new QLabel(tr("模型列表"));
    listTitle->setStyleSheet(StyleKit::cardTitle());
    pageLayout->addWidget(listTitle);

    QVBoxLayout *rowsLayout = nullptr;
    QWidget *modelCard = createModelListCard(rowsLayout);

    // 预设模型（按 URL 特征从内置清单中筛选）
    QJsonArray presetModels;
    QFile modelsFile(QStringLiteral(":/resources/ai_models.json"));
    if (modelsFile.open(QIODevice::ReadOnly)) {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(modelsFile.readAll(), &pe);
        modelsFile.close();
        const QString hint = presetUrlHint(info.key);
        if (pe.error == QJsonParseError::NoError && doc.isObject() && !hint.isEmpty()) {
            for (const QJsonValue &v : doc.object()[QStringLiteral("models")].toArray()) {
                const QJsonObject m = v.toObject();
                if (m[QStringLiteral("apiUrl")].toString().contains(hint))
                    presetModels.append(m);
            }
        }
    }

    // 本服务商下的自定义模型（扁平表中 apiUrl 一致且无 providerId 的条目）
    QJsonArray customEntries;
    for (const QJsonValue &v : loadFlatCustomModels()) {
        const QJsonObject e = v.toObject();
        if (!e.contains(QStringLiteral("providerId"))
            && e[QStringLiteral("apiUrl")].toString() == effectiveUrl)
            customEntries.append(e);
    }

    const bool hasAny = !presetModels.isEmpty() || !customEntries.isEmpty();
    if (!hasAny) {
        auto *empty = new QLabel(tr("该服务商暂无预设模型，可在下方输入模型 ID 添加。"));
        empty->setObjectName(QStringLiteral("mpsEmptyHint"));
        empty->setWordWrap(true);
        rowsLayout->addWidget(empty);
    }

    auto setDefaultModel = [this, info](const QString &id, const QString &displayName) {
        SettingsManager::instance()->setProperty(info.configKey + QStringLiteral("/selectedModel"), id);
        SettingsManager::instance()->setProperty(info.configKey + QStringLiteral("/selectedModelName"), displayName);
        NotificationManager::showInfo(this, tr("已将 %1 设为 %2 的默认模型。").arg(displayName, info.name));
        selectProvider(m_currentId, true);
    };

    for (const QJsonValue &v : presetModels) {
        const QJsonObject m = v.toObject();
        const QString id = m[QStringLiteral("id")].toString();
        const QString displayName = m[QStringLiteral("displayName")].toString();
        rowsLayout->addWidget(createModelRow(displayName, id, id == defaultModelId,
            [setDefaultModel, id, displayName]() { setDefaultModel(id, displayName); }));
    }
    for (const QJsonValue &v : customEntries) {
        const QJsonObject e = v.toObject();
        const QString id = e[QStringLiteral("id")].toString();
        const QString displayName = e[QStringLiteral("displayName")].toString(id);
        rowsLayout->addWidget(createModelRow(displayName, id, id == defaultModelId,
            [setDefaultModel, id, displayName]() { setDefaultModel(id, displayName); },
            [this, id, effectiveUrl]() {
                // 从扁平表中移除该自定义模型（仅限无 providerId 的同端点条目）
                QJsonArray kept;
                for (const QJsonValue &ev : loadFlatCustomModels()) {
                    const QJsonObject e = ev.toObject();
                    if (e[QStringLiteral("id")].toString() != id
                        || e.contains(QStringLiteral("providerId"))
                        || e[QStringLiteral("apiUrl")].toString() != effectiveUrl)
                        kept.append(e);
                }
                saveFlatCustomModels(kept);
                selectProvider(m_currentId, true);
            }));
    }

    // 空态 / 有内容时切换卡片描边（虚线 = 空态，实线 = 有内容）
    modelCard->setProperty("filled", hasAny);
    repolish(modelCard);
    pageLayout->addWidget(modelCard);

    // ── 添加自定义模型 ──
    auto *customIdEdit = new QLineEdit();
    customIdEdit->setPlaceholderText(tr("输入自定义模型 ID，如 deepseek-chat"));

    auto *addBtn = new QPushButton(tr("添加模型"));
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setStyleSheet(StyleKit::smallPrimaryButton());
    connect(addBtn, &QPushButton::clicked, this, [this, info, customIdEdit]() {
        const QString id = customIdEdit->text().trimmed();
        if (id.isEmpty())
            return;
        QJsonObject entry;
        entry[QStringLiteral("id")] = id;
        entry[QStringLiteral("displayName")] = info.name + QLatin1Char(' ') + id;
        entry[QStringLiteral("apiUrl")] = info.defaultUrl;
        entry[QStringLiteral("apiKey")] =
            SettingsManager::instance()->getProperty(info.configKey + QStringLiteral("/key")).toString();
        entry[QStringLiteral("isCustom")] = true;
        appendFlatCustomModel(entry);
        NotificationManager::showSuccess(this, tr("已添加模型 %1。").arg(id));
        selectProvider(m_currentId, true);
    });
    pageLayout->addLayout(createFormRow(tr("自定义模型"), customIdEdit, addBtn));

    pageLayout->addStretch();
    return page;
}

// ============================================================================
// 自定义供应商页
// ============================================================================

QWidget *ModelProviderSettingsPanel::buildCustomProviderPage(const QJsonObject &prov)
{
    const QString pid = prov[QStringLiteral("id")].toString();

    // 原位修改供应商记录并同步展平表
    auto updateProvider = [this, pid](const std::function<void(QJsonObject &)> &mutate) {
        QJsonArray providers;
        for (const QJsonValue &v : loadCustomProviders()) {
            QJsonObject p = v.toObject();
            if (p[QStringLiteral("id")].toString() == pid)
                mutate(p);
            providers.append(p);
        }
        saveCustomProviders(providers);
        syncFlattenedCustomModels();
    };

    auto *page = new QWidget();
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(4, 0, 8, 0);
    pageLayout->setSpacing(10);

    // 标题行：名称 + 删除按钮
    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    auto *title = new QLabel(prov[QStringLiteral("name")].toString());
    title->setStyleSheet(StyleKit::pageTitle());
    titleRow->addWidget(title);
    titleRow->addStretch();

    auto *delBtn = new QPushButton(tr("删除供应商"));
    delBtn->setCursor(Qt::PointingHandCursor);
    delBtn->setStyleSheet(StyleKit::resolve(QStringLiteral(
        "QPushButton { color: @DANGER@; background: transparent; border: none;"
        " border-radius: 6px; padding: 4px 10px; font-size: 12px; }"
        "QPushButton:hover { background: @DANGER_BG@; }")));
    titleRow->addWidget(delBtn);
    pageLayout->addLayout(titleRow);

    auto *desc = new QLabel(tr("自定义供应商的模型会同时出现在模型选择窗口中，修改会即时保存。"));
    desc->setStyleSheet(StyleKit::mutedLabel());
    desc->setWordWrap(true);
    pageLayout->addWidget(desc);

    // ── 名称 ──
    auto *nameEdit = new QLineEdit(prov[QStringLiteral("name")].toString());
    pageLayout->addLayout(createFormRow(tr("名称"), nameEdit));
    connect(nameEdit, &QLineEdit::editingFinished, this, [this, updateProvider, nameEdit, title]() {
        const QString name = nameEdit->text().trimmed();
        if (name.isEmpty())
            return;
        updateProvider([&name](QJsonObject &p) { p[QStringLiteral("name")] = name; });
        title->setText(name);
        rebuildProviderList();
    });

    // ── API 地址 ──
    auto *urlEdit = new QLineEdit(prov[QStringLiteral("apiUrl")].toString());
    urlEdit->setPlaceholderText(QStringLiteral("https://api.example.com/v1"));
    pageLayout->addLayout(createFormRow(tr("API 地址"), urlEdit));
    connect(urlEdit, &QLineEdit::editingFinished, this, [updateProvider, urlEdit]() {
        updateProvider([&urlEdit](QJsonObject &p) { p[QStringLiteral("apiUrl")] = urlEdit->text().trimmed(); });
    });

    // ── API Key ──
    auto *keyEdit = new QLineEdit(prov[QStringLiteral("apiKey")].toString());
    keyEdit->setPlaceholderText(tr("输入 API Key"));
    keyEdit->setEchoMode(QLineEdit::Password);
    auto *toggleBtn = new QToolButton();
    toggleBtn->setText(tr("显示"));
    toggleBtn->setCursor(Qt::PointingHandCursor);
    toggleBtn->setStyleSheet(StyleKit::ghostButton());
    connect(toggleBtn, &QToolButton::clicked, toggleBtn, [keyEdit, toggleBtn]() {
        const bool hidden = keyEdit->echoMode() == QLineEdit::Password;
        keyEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        toggleBtn->setText(hidden ? tr("隐藏") : tr("显示"));
    });
    pageLayout->addLayout(createFormRow(tr("API Key"), keyEdit, toggleBtn));
    connect(keyEdit, &QLineEdit::editingFinished, this, [this, updateProvider, keyEdit]() {
        updateProvider([&keyEdit](QJsonObject &p) { p[QStringLiteral("apiKey")] = keyEdit->text().trimmed(); });
        rebuildProviderList();
    });

    // ── API 格式 ──
    auto *formatCombo = new QComboBox();
    formatCombo->addItem(tr("OpenAI 兼容 (/v1/chat/completions)"), QStringLiteral("openai"));
    formatCombo->addItem(tr("Anthropic Messages (/v1/messages)"), QStringLiteral("anthropic"));
    formatCombo->addItem(tr("Gemini (/v1beta)"), QStringLiteral("gemini"));
    formatCombo->installEventFilter(this);
    const QString savedFormat = prov[QStringLiteral("apiFormat")].toString(QStringLiteral("openai"));
    formatCombo->setCurrentIndex(qMax(0, formatCombo->findData(savedFormat)));
    pageLayout->addLayout(createFormRow(tr("API 格式"), formatCombo));
    connect(formatCombo, &QComboBox::currentIndexChanged, this, [updateProvider, formatCombo](int) {
        updateProvider([&formatCombo](QJsonObject &p) {
            p[QStringLiteral("apiFormat")] = formatCombo->currentData().toString();
        });
    });

    auto *formatHint = new QLabel(
        tr("当前版本以 OpenAI 兼容协议与模型通信，其他格式暂作记录，后续版本支持直连。"));
    formatHint->setStyleSheet(StyleKit::captionLabel());
    formatHint->setWordWrap(true);
    pageLayout->addWidget(formatHint);

    // ── 模型列表 ──
    const QString defaultModelId = prov[QStringLiteral("defaultModel")].toString();

    auto *listTitle = new QLabel(tr("模型列表"));
    listTitle->setStyleSheet(StyleKit::cardTitle());
    pageLayout->addWidget(listTitle);

    QVBoxLayout *rowsLayout = nullptr;
    QWidget *modelCard = createModelListCard(rowsLayout);

    const QJsonArray models = prov[QStringLiteral("models")].toArray();
    if (models.isEmpty()) {
        auto *empty = new QLabel(tr("当前没有配置模型，添加模型后可在聊天中使用。"));
        empty->setObjectName(QStringLiteral("mpsEmptyHint"));
        empty->setWordWrap(true);
        rowsLayout->addWidget(empty);
    }
    for (const QJsonValue &v : models) {
        const QJsonObject m = v.toObject();
        const QString id = m[QStringLiteral("id")].toString();
        const QString displayName = m[QStringLiteral("displayName")].toString(id);
        rowsLayout->addWidget(createModelRow(displayName, id, id == defaultModelId,
            [this, updateProvider, id]() {
                updateProvider([&id](QJsonObject &p) { p[QStringLiteral("defaultModel")] = id; });
                selectProvider(m_currentId, true);
            },
            [this, updateProvider, id]() {
                updateProvider([&id](QJsonObject &p) {
                    QJsonArray kept;
                    for (const QJsonValue &mv : p[QStringLiteral("models")].toArray()) {
                        if (mv.toObject()[QStringLiteral("id")].toString() != id)
                            kept.append(mv);
                    }
                    p[QStringLiteral("models")] = kept;
                    if (p[QStringLiteral("defaultModel")].toString() == id)
                        p[QStringLiteral("defaultModel")] = QString();
                });
                rebuildProviderList();
                selectProvider(m_currentId, true);
            }));
    }
    modelCard->setProperty("filled", !models.isEmpty());
    repolish(modelCard);
    pageLayout->addWidget(modelCard);

    // ── 添加模型 ──
    auto *idEdit = new QLineEdit();
    idEdit->setPlaceholderText(tr("模型 ID，如 gpt-5"));
    auto *nameField = new QLineEdit();
    nameField->setPlaceholderText(tr("显示名称（可选）"));
    auto *addBtn = new QPushButton(tr("添加模型"));
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setStyleSheet(StyleKit::smallPrimaryButton());
    connect(addBtn, &QPushButton::clicked, this, [this, updateProvider, idEdit, nameField]() {
        const QString id = idEdit->text().trimmed();
        if (id.isEmpty())
            return;
        const QString displayName = nameField->text().trimmed().isEmpty()
            ? id : nameField->text().trimmed();
        updateProvider([&id, &displayName](QJsonObject &p) {
            QJsonObject m;
            m[QStringLiteral("id")] = id;
            m[QStringLiteral("displayName")] = displayName;
            QJsonArray models = p[QStringLiteral("models")].toArray();
            models.append(m);
            p[QStringLiteral("models")] = models;
        });
        NotificationManager::showSuccess(this, tr("已添加模型 %1。").arg(id));
        rebuildProviderList();
        selectProvider(m_currentId, true);
    });

    auto *addRow = new QHBoxLayout();
    addRow->setContentsMargins(0, 0, 0, 0);
    addRow->setSpacing(10);
    addRow->addWidget(idEdit, 1);
    addRow->addWidget(nameField, 1);
    addRow->addWidget(addBtn);
    pageLayout->addLayout(addRow);

    // ── 删除供应商 ──
    connect(delBtn, &QPushButton::clicked, this, [this, pid, prov]() {
        const QString name = prov[QStringLiteral("name")].toString();
        if (AppMessageBox::question(this, tr("删除供应商"),
                tr("确定删除自定义供应商“%1”吗？其下模型将同时从模型选择窗口移除。").arg(name))
            != AppMessageBox::Yes)
            return;

        QJsonArray kept;
        for (const QJsonValue &v : loadCustomProviders()) {
            if (v.toObject()[QStringLiteral("id")].toString() != pid)
                kept.append(v);
        }
        saveCustomProviders(kept);
        syncFlattenedCustomModels();
        NotificationManager::showSuccess(this, tr("已删除供应商 %1。").arg(name));
        m_currentId.clear();
        selectProvider(QStringLiteral("preset:DeepSeek"), true);
    });

    pageLayout->addStretch();
    return page;
}

// ============================================================================
// 添加供应商页
// ============================================================================

QWidget *ModelProviderSettingsPanel::buildAddProviderPage()
{
    auto *page = new QWidget();
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(4, 0, 8, 0);
    pageLayout->setSpacing(10);

    auto *title = new QLabel(tr("添加模型供应商"));
    title->setStyleSheet(StyleKit::pageTitle());
    pageLayout->addWidget(title);

    auto *desc = new QLabel(tr("配置一个完全自定义的 API 端点和初始模型。"));
    desc->setStyleSheet(StyleKit::mutedLabel());
    pageLayout->addWidget(desc);

    m_addNameEdit = new QLineEdit();
    m_addNameEdit->setPlaceholderText(tr("如：智谱 GLM"));
    pageLayout->addLayout(createFormRow(tr("名称"), m_addNameEdit));

    m_addUrlEdit = new QLineEdit();
    m_addUrlEdit->setPlaceholderText(QStringLiteral("https://api.example.com/v1"));
    pageLayout->addLayout(createFormRow(tr("API 地址"), m_addUrlEdit));

    m_addKeyEdit = new QLineEdit();
    m_addKeyEdit->setPlaceholderText(tr("输入 API Key"));
    m_addKeyEdit->setEchoMode(QLineEdit::Password);
    auto *toggleBtn = new QToolButton();
    toggleBtn->setText(tr("显示"));
    toggleBtn->setCursor(Qt::PointingHandCursor);
    toggleBtn->setStyleSheet(StyleKit::ghostButton());
    connect(toggleBtn, &QToolButton::clicked, toggleBtn, [this, toggleBtn]() {
        const bool hidden = m_addKeyEdit->echoMode() == QLineEdit::Password;
        m_addKeyEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        toggleBtn->setText(hidden ? tr("隐藏") : tr("显示"));
    });
    pageLayout->addLayout(createFormRow(tr("API Key"), m_addKeyEdit, toggleBtn));

    m_addFormatCombo = new QComboBox();
    m_addFormatCombo->addItem(tr("OpenAI 兼容 (/v1/chat/completions)"), QStringLiteral("openai"));
    m_addFormatCombo->addItem(tr("Anthropic Messages (/v1/messages)"), QStringLiteral("anthropic"));
    m_addFormatCombo->addItem(tr("Gemini (/v1beta)"), QStringLiteral("gemini"));
    m_addFormatCombo->installEventFilter(this);
    pageLayout->addLayout(createFormRow(tr("API 格式"), m_addFormatCombo));

    // ── 初始模型列表 ──
    auto *listTitle = new QLabel(tr("模型列表"));
    listTitle->setStyleSheet(StyleKit::cardTitle());
    pageLayout->addWidget(listTitle);

    QWidget *modelCard = createModelListCard(m_addModelsLayout);
    pageLayout->addWidget(modelCard);

    // 内联添加模型行（点击“添加模型”展开）
    auto *inlineRowWidget = new QWidget();
    auto *inlineRow = new QHBoxLayout(inlineRowWidget);
    inlineRow->setContentsMargins(0, 0, 0, 0);
    inlineRow->setSpacing(10);
    auto *stageIdEdit = new QLineEdit();
    stageIdEdit->setPlaceholderText(tr("模型 ID，如 gpt-5"));
    auto *stageNameEdit = new QLineEdit();
    stageNameEdit->setPlaceholderText(tr("显示名称（可选）"));
    auto *stageConfirmBtn = new QPushButton(tr("确定"));
    stageConfirmBtn->setCursor(Qt::PointingHandCursor);
    stageConfirmBtn->setStyleSheet(StyleKit::smallPrimaryButton());
    inlineRow->addWidget(stageIdEdit, 1);
    inlineRow->addWidget(stageNameEdit, 1);
    inlineRow->addWidget(stageConfirmBtn);
    inlineRowWidget->hide();
    pageLayout->addWidget(inlineRowWidget);

    auto *stageBtn = new QPushButton(tr("+ 添加模型"));
    stageBtn->setCursor(Qt::PointingHandCursor);
    stageBtn->setStyleSheet(StyleKit::outlineButton());
    connect(stageBtn, &QPushButton::clicked, this, [inlineRowWidget, stageIdEdit]() {
        inlineRowWidget->setVisible(!inlineRowWidget->isVisible());
        if (inlineRowWidget->isVisible())
            stageIdEdit->setFocus();
    });
    pageLayout->addWidget(stageBtn);

    auto stageModel = [this, stageIdEdit, stageNameEdit, inlineRowWidget]() {
        const QString id = stageIdEdit->text().trimmed();
        if (id.isEmpty())
            return;
        const QString displayName = stageNameEdit->text().trimmed().isEmpty()
            ? id : stageNameEdit->text().trimmed();
        QJsonObject m;
        m[QStringLiteral("id")] = id;
        m[QStringLiteral("displayName")] = displayName;
        m_stagedModels.append(m);
        stageIdEdit->clear();
        stageNameEdit->clear();
        refreshAddModelsCard();
        inlineRowWidget->hide();
    };
    connect(stageConfirmBtn, &QPushButton::clicked, this, stageModel);
    connect(stageIdEdit, &QLineEdit::returnPressed, this, stageModel);

    pageLayout->addStretch();

    // ── 底部：提示 + 创建按钮 ──
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(10);
    auto *hint = new QLabel(tr("ⓘ 添加供应商前，请至少添加一个模型。"));
    hint->setObjectName(QStringLiteral("mpsEmptyHint"));
    bottomRow->addWidget(hint, 1);

    m_addCreateBtn = new QPushButton(tr("添加供应商"));
    m_addCreateBtn->setCursor(Qt::PointingHandCursor);
    m_addCreateBtn->setStyleSheet(StyleKit::primaryButton());
    m_addCreateBtn->setEnabled(false);
    bottomRow->addWidget(m_addCreateBtn);
    pageLayout->addLayout(bottomRow);

    connect(m_addNameEdit, &QLineEdit::textChanged, this, [this]() {
        m_addCreateBtn->setEnabled(
            !m_addNameEdit->text().trimmed().isEmpty()
            && !m_addUrlEdit->text().trimmed().isEmpty()
            && !m_stagedModels.isEmpty());
    });
    connect(m_addUrlEdit, &QLineEdit::textChanged, this, [this]() {
        m_addCreateBtn->setEnabled(
            !m_addNameEdit->text().trimmed().isEmpty()
            && !m_addUrlEdit->text().trimmed().isEmpty()
            && !m_stagedModels.isEmpty());
    });

    // 确定创建
    connect(m_addCreateBtn, &QPushButton::clicked, this, [this]() {
        const QString name = m_addNameEdit->text().trimmed();
        const QString url = m_addUrlEdit->text().trimmed();
        if (name.isEmpty() || url.isEmpty() || m_stagedModels.isEmpty())
            return;
        if (!url.startsWith(QStringLiteral("http"))) {
            NotificationManager::showError(this, tr("API 地址需以 http(s):// 开头，请检查后重试。"));
            return;
        }

        QJsonObject prov;
        prov[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        prov[QStringLiteral("name")] = name;
        prov[QStringLiteral("apiUrl")] = url;
        prov[QStringLiteral("apiKey")] = m_addKeyEdit->text().trimmed();
        prov[QStringLiteral("apiFormat")] = m_addFormatCombo->currentData().toString();
        prov[QStringLiteral("models")] = m_stagedModels;
        prov[QStringLiteral("defaultModel")] =
            m_stagedModels.at(0).toObject()[QStringLiteral("id")].toString();

        QJsonArray providers = loadCustomProviders();
        providers.append(prov);
        saveCustomProviders(providers);
        syncFlattenedCustomModels();

        // 重置表单
        m_addNameEdit->clear();
        m_addUrlEdit->clear();
        m_addKeyEdit->clear();
        m_addFormatCombo->setCurrentIndex(0);
        m_stagedModels = QJsonArray();
        refreshAddModelsCard();

        NotificationManager::showSuccess(this, tr("已添加供应商 %1。").arg(name));
        selectProvider(QStringLiteral("custom:") + prov[QStringLiteral("id")].toString(), true);
    });

    refreshAddModelsCard();
    return page;
}

void ModelProviderSettingsPanel::refreshAddModelsCard()
{
    if (!m_addModelsLayout)
        return;

    while (QLayoutItem *item = m_addModelsLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (m_stagedModels.isEmpty()) {
        auto *empty = new QLabel(tr("当前没有配置模型，添加模型后可在聊天中使用。"));
        empty->setObjectName(QStringLiteral("mpsEmptyHint"));
        empty->setWordWrap(true);
        m_addModelsLayout->addWidget(empty);
    }
    for (const QJsonValue &v : m_stagedModels) {
        const QJsonObject m = v.toObject();
        const QString id = m[QStringLiteral("id")].toString();
        m_addModelsLayout->addWidget(createModelRow(
            m[QStringLiteral("displayName")].toString(id), id, false, nullptr, [this, id]() {
                QJsonArray kept;
                for (const QJsonValue &ev : m_stagedModels) {
                    if (ev.toObject()[QStringLiteral("id")].toString() != id)
                        kept.append(ev);
                }
                m_stagedModels = kept;
                refreshAddModelsCard();
            }));
    }

    // 空态虚线 / 有内容实线
    if (QWidget *card = m_addModelsLayout->parentWidget()) {
        card->setProperty("filled", !m_stagedModels.isEmpty());
        repolish(card);
    }
    m_addCreateBtn->setEnabled(
        !m_addNameEdit->text().trimmed().isEmpty()
        && !m_addUrlEdit->text().trimmed().isEmpty()
        && !m_stagedModels.isEmpty());
}
