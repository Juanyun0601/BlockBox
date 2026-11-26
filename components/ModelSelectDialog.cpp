/**
 * @file   ModelSelectDialog.cpp
 * @brief  AI 模型选择对话框实现（先选服务商，再从服务商模型列表中选择）
 * @author BlockBox Team
 * @date   2026-06-23
 */

#include "ModelSelectDialog.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include <QUrl>

#include "components/LocalModelDialog.h"
#include "utils/AiProviderPresets.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"

namespace {

QString formatCacheTime(qint64 secsSinceEpoch)
{
    if (secsSinceEpoch <= 0)
        return QString();
    return QDateTime::fromSecsSinceEpoch(secsSinceEpoch).toString(QStringLiteral("MM-dd hh:mm"));
}

} // namespace

ModelSelectDialog::ModelSelectDialog(QWidget* parent)
    : AppDialogBase(parent)
    , m_searchTimer(nullptr)
    , m_fetcher(nullptr)
    , m_providerScroll(nullptr)
    , m_providerHost(nullptr)
    , m_providerLayout(nullptr)
    , m_searchEdit(nullptr)
    , m_providerTitle(nullptr)
    , m_statusLabel(nullptr)
    , m_refreshBtn(nullptr)
    , m_modelScroll(nullptr)
    , m_modelHost(nullptr)
    , m_modelLayout(nullptr)
    , m_manualEdit(nullptr)
    , m_manualBtn(nullptr)
    , m_localModelBtn(nullptr)
    , m_localModelDialog(nullptr)
{
    setWindowTitle(tr("模型设置"));
    setWindowModality(Qt::NonModal);   // 覆盖基类的 WindowModal，保持原有非模态复用行为
    setObjectName("modelSelectDialog");

    m_fetcher = new AiModelListFetcher(this);
    connect(m_fetcher, &AiModelListFetcher::finished, this, &ModelSelectDialog::onFetchFinished);

    initUI();

    // 搜索输入防抖：避免每敲一个字就整树重建
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(220);
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        rebuildProviderList();
        rebuildModelList();
    });
}

ModelSelectDialog::~ModelSelectDialog() = default;

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
    card->setMinimumSize(UiMetrics::fitToScreen(this, QSize(620, 620)));
    card->setMaximumSize(820, 760);   // 保证不超出主窗口（主窗口最小 1200x800）
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
    connect(m_localModelBtn, &QPushButton::clicked, this, &ModelSelectDialog::onLocalModelClicked);

    // 预留 AppDialogBase 关闭按钮的空间（右上角内侧 12px margin + 28px 按钮 + 12px 间距）
    titleLayout->addSpacing(52);

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
    mainLayout->addWidget(searchWidget);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        // 防抖：等用户停止输入后再重建，避免每次按键都触发昂贵的列表重建
        if (m_searchTimer)
            m_searchTimer->start();
    });

    // ========================================================================
    // 主体：左侧服务商列表 + 右侧模型列表
    // ========================================================================
    auto* body = new QWidget();
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(20, 0, 20, 20);
    bodyLayout->setSpacing(14);

    // ── 左栏：服务商 ──
    auto* leftColumn = new QWidget();
    leftColumn->setObjectName("msdProviderColumn");
    leftColumn->setFixedWidth(196);
    auto* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    m_providerScroll = new QScrollArea();
    m_providerScroll->setObjectName("msdProviderScroll");
    m_providerScroll->setFrameShape(QFrame::NoFrame);
    m_providerScroll->setWidgetResizable(true);
    m_providerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_providerScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_providerHost = new QWidget();
    m_providerHost->setObjectName("msdProviderHost");
    m_providerLayout = new QVBoxLayout(m_providerHost);
    m_providerLayout->setContentsMargins(0, 0, 4, 0);
    m_providerLayout->setSpacing(1);
    m_providerScroll->setWidget(m_providerHost);
    leftLayout->addWidget(m_providerScroll, 1);

    auto* configProviderBtn = new QPushButton(tr("＋ 配置服务商"));
    configProviderBtn->setObjectName("msdConfigProviderBtn");
    configProviderBtn->setCursor(Qt::PointingHandCursor);
    configProviderBtn->setStyleSheet(StyleKit::outlineButton());
    connect(configProviderBtn, &QPushButton::clicked, this, [this]() {
        emit openSettingsRequested();
        accept();
    });
    leftLayout->addWidget(configProviderBtn);

    bodyLayout->addWidget(leftColumn);

    // ── 右栏：模型 ──
    auto* rightColumn = new QWidget();
    rightColumn->setObjectName("msdModelColumn");
    auto* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);

    m_providerTitle = new QLabel(tr("选择服务商"));
    m_providerTitle->setObjectName("modelProviderHeader");
    headerRow->addWidget(m_providerTitle, 1);

    m_refreshBtn = new QPushButton(tr("刷新列表"));
    m_refreshBtn->setObjectName("msdRefreshBtn");
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setStyleSheet(StyleKit::ghostButton());
    headerRow->addWidget(m_refreshBtn);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() { fetchModels(true); });

    rightLayout->addLayout(headerRow);

    m_statusLabel = new QLabel();
    m_statusLabel->setObjectName("msdStatusLabel");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(StyleKit::captionLabel());
    rightLayout->addWidget(m_statusLabel);

    m_modelScroll = new QScrollArea();
    m_modelScroll->setObjectName("modelDialogScroll");
    m_modelScroll->setWidgetResizable(true);
    m_modelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_modelScroll->setFrameShape(QFrame::NoFrame);

    m_modelHost = new QWidget();
    m_modelLayout = new QVBoxLayout(m_modelHost);
    m_modelLayout->setContentsMargins(0, 0, 4, 0);
    m_modelLayout->setSpacing(0);
    m_modelScroll->setWidget(m_modelHost);
    rightLayout->addWidget(m_modelScroll, 1);

    // 手动输入模型 ID（服务商未返回该模型时兜底）
    auto* manualRow = new QHBoxLayout();
    manualRow->setContentsMargins(0, 0, 0, 0);
    manualRow->setSpacing(8);

    m_manualEdit = new QLineEdit();
    m_manualEdit->setObjectName("msdManualEdit");
    m_manualEdit->setPlaceholderText(tr("手动输入模型 ID…"));
    manualRow->addWidget(m_manualEdit, 1);

    m_manualBtn = new QPushButton(tr("使用该模型"));
    m_manualBtn->setObjectName("msdManualBtn");
    m_manualBtn->setCursor(Qt::PointingHandCursor);
    m_manualBtn->setStyleSheet(StyleKit::smallPrimaryButton());
    manualRow->addWidget(m_manualBtn);
    connect(m_manualBtn, &QPushButton::clicked, this, &ModelSelectDialog::addManualModel);
    connect(m_manualEdit, &QLineEdit::returnPressed, this, &ModelSelectDialog::addManualModel);

    rightLayout->addLayout(manualRow);
    bodyLayout->addWidget(rightColumn, 1);

    mainLayout->addWidget(body, 1);
}

// ============================================================================
// 对外接口
// ============================================================================

void ModelSelectDialog::setModels(const QList<AiModel>& models)
{
    m_allModels = models;

    // 建立模型 ID → 展示名映射：内置预设清单优先，其次是已配置模型
    m_knownNames.clear();
    QFile file(QStringLiteral(":/resources/ai_models.json"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
        file.close();
        if (pe.error == QJsonParseError::NoError && doc.isObject()) {
            for (const QJsonValue &v : doc.object().value(QStringLiteral("models")).toArray()) {
                const QJsonObject m = v.toObject();
                const QString id = m.value(QStringLiteral("id")).toString();
                const QString name = m.value(QStringLiteral("displayName")).toString();
                if (!id.isEmpty() && !name.isEmpty() && !m_knownNames.contains(id))
                    m_knownNames.insert(id, name);
            }
        }
    }
    for (const AiModel &m : m_allModels) {
        if (!m.id.isEmpty() && !m.displayName.isEmpty() && !m_knownNames.contains(m.id))
            m_knownNames.insert(m.id, m.displayName);
    }

    rebuildAll();
}

void ModelSelectDialog::setCurrentModel(const QString& modelId)
{
    if (m_currentModelId == modelId)
        return;
    m_currentModelId = modelId;

    // 上层在 setModels 之后才调用本方法：重新挑一次「当前模型所属服务商」
    m_selectedRowId.clear();
    m_loadedRowId.clear();
    rebuildAll();
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
// 服务商数据
// ============================================================================

QList<ModelSelectDialog::ProviderEntry> ModelSelectDialog::providerEntries() const
{
    QList<ProviderEntry> entries;
    QSettings store(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));

    // 1) 自定义供应商（用户自己添加的排在最前）
    const QString customJson = store.value(QStringLiteral("ai/customProviders")).toString();
    if (!customJson.isEmpty()) {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(customJson.toUtf8(), &pe);
        if (pe.error == QJsonParseError::NoError && doc.isArray()) {
            for (const QJsonValue &v : doc.array()) {
                const QJsonObject o = v.toObject();
                ProviderEntry e;
                e.providerId = o.value(QStringLiteral("id")).toString();
                e.name = o.value(QStringLiteral("name")).toString();
                if (e.name.isEmpty())
                    e.name = tr("自定义供应商");
                e.apiUrl = o.value(QStringLiteral("apiUrl")).toString();
                e.apiKey = o.value(QStringLiteral("apiKey")).toString();
                e.selectedModel = o.value(QStringLiteral("defaultModel")).toString();
                e.isCustom = true;
                e.rowId = QStringLiteral("custom:") + e.providerId;
                entries.append(e);
            }
        }
    }

    // 2) 旧版「自定义模型」扁平表：不属于内置服务商、也不属于上面自定义供应商的条目，
    //    按端点收拢成合成供应商，保证升级后仍能在新界面里选到这些模型。
    {
        QSet<QString> knownUrls;
        for (const ProviderEntry &e : entries)
            knownUrls.insert(normalizedUrl(e.apiUrl));

        QStringList presetHints;
        for (const AiProviderPreset &p : aiProviderPresets()) {
            if (!p.urlHint.isEmpty())
                presetHints << p.urlHint;
        }

        QMap<QString, ProviderEntry> legacyProviders;   // 归一化端点 → 合成供应商
        const QString flatJson = store.value(QStringLiteral("ai/customModels")).toString();
        if (!flatJson.isEmpty()) {
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(flatJson.toUtf8(), &pe);
            if (pe.error == QJsonParseError::NoError && doc.isArray()) {
                for (const QJsonValue &v : doc.array()) {
                    const QJsonObject o = v.toObject();
                    if (o.contains(QStringLiteral("providerId")))
                        continue;
                    const QString url = o.value(QStringLiteral("apiUrl")).toString().trimmed();
                    if (url.isEmpty())
                        continue;

                    bool matchesPreset = false;
                    for (const QString &hint : presetHints) {
                        if (url.contains(hint)) {
                            matchesPreset = true;
                            break;
                        }
                    }
                    const QString key = normalizedUrl(url);
                    if (matchesPreset || knownUrls.contains(key))
                        continue;

                    if (!legacyProviders.contains(key)) {
                        ProviderEntry e;
                        e.rowId = QStringLiteral("legacy:") + url;
                        e.apiUrl = url;
                        e.apiKey = o.value(QStringLiteral("apiKey")).toString();
                        e.isCustom = true;
                        const QString displayName = o.value(QStringLiteral("displayName")).toString();
                        const QString prefix = displayName.contains(QLatin1Char(' '))
                            ? displayName.section(QLatin1Char(' '), 0, 0)
                            : QUrl(url).host();
                        e.name = prefix.trimmed().isEmpty() ? tr("自定义服务商") : prefix.trimmed();
                        legacyProviders.insert(key, e);
                    }
                }
            }
        }
        for (auto it = legacyProviders.constBegin(); it != legacyProviders.constEnd(); ++it)
            entries.append(it.value());
    }

    // 3) 内置服务商
    for (const AiProviderPreset &p : aiProviderPresets()) {
        ProviderEntry e;
        e.rowId = QStringLiteral("preset:") + p.key;
        e.name = p.name;
        e.configKey = p.configKey;
        e.urlHint = p.urlHint;
        const QString savedUrl = store.value(p.configKey + QStringLiteral("/url")).toString();
        e.apiUrl = savedUrl.isEmpty() ? p.defaultUrl : savedUrl;
        e.apiKey = store.value(p.configKey + QStringLiteral("/key")).toString();
        e.selectedModel = store.value(p.configKey + QStringLiteral("/selectedModel")).toString();
        e.selectedModelName = store.value(p.configKey + QStringLiteral("/selectedModelName")).toString();
        entries.append(e);
    }

    return entries;
}

ModelSelectDialog::ProviderEntry ModelSelectDialog::providerForRow(const QString& rowId) const
{
    for (const ProviderEntry &e : providerEntries()) {
        if (e.rowId == rowId)
            return e;
    }
    return {};
}

ModelSelectDialog::ProviderEntry ModelSelectDialog::currentProvider() const
{
    return providerForRow(m_selectedRowId);
}

QString ModelSelectDialog::providerOfModel(const AiModel& model) const
{
    const QString wantedUrl = normalizedUrl(model.apiUrl);
    const QList<ProviderEntry> entries = providerEntries();

    // 先按端点精确匹配，再按展示名前缀匹配
    for (const ProviderEntry &e : entries) {
        if (!wantedUrl.isEmpty() && normalizedUrl(e.apiUrl) == wantedUrl)
            return e.rowId;
    }
    for (const ProviderEntry &e : entries) {
        if (!model.displayName.isEmpty() && model.displayName.startsWith(e.name + QLatin1Char(' ')))
            return e.rowId;
    }
    for (const ProviderEntry &e : entries) {
        if (!e.selectedModel.isEmpty() && e.selectedModel == model.id)
            return e.rowId;
    }
    return QString();
}

QString ModelSelectDialog::normalizedUrl(const QString& url)
{
    QString s = url.trimmed();
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    return s;
}

bool ModelSelectDialog::isLocalEndpoint(const QString& url)
{
    const QString host = QUrl(url).host().toLower();
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost")
        || host == QLatin1String("::1") || host == QLatin1String("0.0.0.0");
}

// ============================================================================
// 左栏：服务商列表
// ============================================================================

void ModelSelectDialog::rebuildAll()
{
    const QList<ProviderEntry> entries = providerEntries();

    QString target = m_selectedRowId;
    if (providerForRow(target).rowId.isEmpty()) {
        target = providerOfModel(selectedModel());
    }
    if (target.isEmpty() && m_currentModelId.isEmpty()) {
        // 没有当前模型：优先挑一个已配置 Key 的服务商
        for (const ProviderEntry &e : entries) {
            if (!e.apiKey.isEmpty()) {
                target = e.rowId;
                break;
            }
        }
    }
    if (target.isEmpty() && !entries.isEmpty())
        target = entries.first().rowId;

    m_selectedRowId = target;
    rebuildProviderList();

    if (!target.isEmpty()) {
        selectProvider(target, true);
    } else {
        m_providerTitle->setText(tr("选择服务商"));
        updateStatusLabel(tr("还没有可用的服务商，请先在设置中添加。"));
        applyIds(QStringList());
    }
}

void ModelSelectDialog::rebuildProviderList()
{
    if (!m_providerLayout)
        return;

    while (QLayoutItem *item = m_providerLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const QString searchText = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : QString();
    const QList<ProviderEntry> entries = providerEntries();

    auto addGroupLabel = [this](const QString &text) {
        auto *label = new QLabel(text);
        label->setObjectName("modelSectionTitle");
        m_providerLayout->addWidget(label);
    };

    auto createRow = [this](const ProviderEntry &entry) {
        auto *row = new QPushButton();
        row->setObjectName("msdProviderRow");
        row->setCursor(Qt::PointingHandCursor);
        row->setCheckable(true);
        row->setChecked(entry.rowId == m_selectedRowId);
        row->setToolTip(entry.apiUrl);

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(6, 0, 6, 0);
        rowLayout->setSpacing(7);

        auto *dot = new QLabel(row);
        dot->setObjectName("msdProviderDot");
        dot->setFixedSize(7, 7);
        dot->setStyleSheet(QStringLiteral("QLabel#msdProviderDot { background: %1; border-radius: 3px; }")
                               .arg(entry.apiKey.isEmpty() ? StyleKit::resolve(QStringLiteral("@TEXT_DISABLED@"))
                                                           : StyleKit::resolve(QStringLiteral("@SUCCESS@"))));
        rowLayout->addWidget(dot);

        auto *nameLabel = new QLabel(row);
        nameLabel->setObjectName("msdProviderName");
        const QString elided = QFontMetrics(nameLabel->font())
                                   .elidedText(entry.name, Qt::ElideRight, 136);
        nameLabel->setText(elided);
        rowLayout->addWidget(nameLabel, 1);

        connect(row, &QPushButton::clicked, this, [this, rowId = entry.rowId]() {
            selectProvider(rowId);
        });
        m_providerLayout->addWidget(row);
    };

    const QList<ProviderEntry> customs = entries;
    bool addedCustomGroup = false;
    for (const ProviderEntry &e : customs) {
        if (!e.isCustom)
            continue;
        if (!searchText.isEmpty()
            && !e.name.toLower().contains(searchText)
            && !e.apiUrl.toLower().contains(searchText))
            continue;
        if (!addedCustomGroup) {
            addGroupLabel(tr("自定义供应商"));
            addedCustomGroup = true;
        }
        createRow(e);
    }

    bool addedPresetGroup = false;
    for (const ProviderEntry &e : entries) {
        if (e.isCustom)
            continue;
        if (!searchText.isEmpty()
            && !e.name.toLower().contains(searchText)
            && !e.apiUrl.toLower().contains(searchText))
            continue;
        if (!addedPresetGroup) {
            addGroupLabel(tr("服务商"));
            addedPresetGroup = true;
        }
        createRow(e);
    }

    if (!addedCustomGroup && !addedPresetGroup) {
        auto *empty = new QLabel(tr("没有匹配的服务商"));
        empty->setObjectName("modelEmptyHint");
        empty->setWordWrap(true);
        m_providerLayout->addWidget(empty);
    }

    m_providerLayout->addStretch();
}

void ModelSelectDialog::selectProvider(const QString& rowId, bool force)
{
    const ProviderEntry provider = providerForRow(rowId);
    if (provider.rowId.isEmpty())
        return;

    const bool changed = (m_selectedRowId != rowId);
    m_selectedRowId = rowId;
    if (changed)
        m_currentIds.clear();

    // 上一次拉取还挂在别的服务商上：取消并复位刷新按钮，避免一直显示「获取中…」
    if (m_fetcher->isRunning() && m_fetchingRowId != rowId) {
        m_fetcher->cancel();
        m_fetchingRowId.clear();
    }
    if (m_refreshBtn && m_fetchingRowId.isEmpty()) {
        m_refreshBtn->setEnabled(true);
        m_refreshBtn->setText(tr("刷新列表"));
    }

    rebuildProviderList();

    m_providerTitle->setText(provider.name);
    if (m_manualEdit)
        m_manualEdit->clear();

    if (!force && m_loadedRowId == rowId && !m_currentIds.isEmpty())
        return;

    // 1) 优先展示缓存，打开即可看到上次的模型列表
    const QStringList cached = AiModelListFetcher::cachedIds(provider.apiUrl);
    if (!cached.isEmpty()) {
        applyIds(cached);
        updateStatusLabel(tr("已从缓存载入 %1 个模型 · 更新于 %2")
                              .arg(cached.size())
                              .arg(formatCacheTime(AiModelListFetcher::cachedAt(provider.apiUrl))));
        return;
    }

    // 2) 没有缓存：先展示内置清单/已配置模型作为兜底
    applyIds(fallbackIds(provider));

    if (provider.apiUrl.trimmed().isEmpty()) {
        updateStatusLabel(tr("该服务商还没有配置 API 地址，请在设置中补全。"), true);
        return;
    }
    if (provider.apiKey.isEmpty() && !isLocalEndpoint(provider.apiUrl)) {
        updateStatusLabel(tr("该服务商还没有配置 API Key，配置后即可自动获取模型列表；也可以手动输入模型 ID。"));
        return;
    }

    // 3) 自动向服务商获取一次（每个服务商每次打开最多一次）
    if (!m_autoFetched.contains(rowId) || force) {
        m_autoFetched.insert(rowId);
        fetchModels(false);
    }
}

// ============================================================================
// 右栏：模型列表
// ============================================================================

void ModelSelectDialog::rebuildModelList()
{
    if (!m_modelLayout)
        return;

    while (QLayoutItem *item = m_modelLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const ProviderEntry provider = currentProvider();
    const QString searchText = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : QString();

    QStringList ids;
    for (const QString &id : m_currentIds) {
        if (searchText.isEmpty()
            || id.toLower().contains(searchText)
            || displayNameFor(provider, id).toLower().contains(searchText))
            ids.append(id);
    }

    if (ids.isEmpty()) {
        auto *empty = new QLabel();
        empty->setObjectName("modelEmptyHint");
        empty->setWordWrap(true);
        if (provider.rowId.isEmpty())
            empty->setText(tr("请先在左侧选择一个服务商。"));
        else if (m_currentIds.isEmpty())
            empty->setText(tr("暂无模型列表，点右上角「刷新列表」从服务商获取，或在下方手动输入模型 ID。"));
        else
            empty->setText(tr("没有匹配的模型。"));
        m_modelLayout->addWidget(empty);
        m_modelLayout->addStretch();
        return;
    }

    for (int i = 0; i < ids.size(); ++i) {
        const QString id = ids.at(i);
        const QString name = displayNameFor(provider, id);
        const bool isCurrent = (id == m_currentModelId);

        auto *row = new QWidget();
        row->setObjectName("modelConfiguredItem");
        row->setProperty("modelId", id);
        row->setCursor(Qt::PointingHandCursor);
        row->installEventFilter(this);

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 7, 10, 7);
        rowLayout->setSpacing(8);

        auto *checkIcon = new QLabel();
        checkIcon->setObjectName(isCurrent ? "modelCheckActive" : "modelCheckInactive");
        checkIcon->setAlignment(Qt::AlignCenter);
        checkIcon->setFixedSize(20, 20);
        checkIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
        if (isCurrent) {
            checkIcon->setPixmap(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/check.svg"),
                                                             QColor("#ffffff"), 12).pixmap(12, 12));
        }
        rowLayout->addWidget(checkIcon);

        auto *nameLabel = new QLabel(name);
        nameLabel->setObjectName("modelConfigName");
        nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(nameLabel, 1);

        if (name != id) {
            auto *idLabel = new QLabel(id);
            idLabel->setObjectName("modelConfigKey");
            idLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            rowLayout->addWidget(idLabel);
        }

        m_modelLayout->addWidget(row);

        if (i + 1 < ids.size()) {
            auto *separator = new QFrame();
            separator->setObjectName("modelItemSeparator");
            separator->setFixedHeight(1);
            m_modelLayout->addWidget(separator);
        }
    }

    m_modelLayout->addStretch();
}

void ModelSelectDialog::applyIds(const QStringList& ids)
{
    m_currentIds.clear();
    for (const QString &raw : ids) {
        const QString id = raw.trimmed();
        if (!id.isEmpty() && !m_currentIds.contains(id))
            m_currentIds.append(id);
    }
    m_loadedRowId = m_selectedRowId;
    rebuildModelList();
}

QStringList ModelSelectDialog::fallbackIds(const ProviderEntry& provider) const
{
    QStringList ids;
    auto addId = [&ids](const QString &id) {
        if (!id.isEmpty() && !ids.contains(id))
            ids.append(id);
    };

    // 已配置模型：端点一致或展示名带服务商前缀
    for (const AiModel &m : m_allModels) {
        if (m.isLocal || m.id.isEmpty())
            continue;
        if (!provider.apiUrl.isEmpty()
            && normalizedUrl(m.apiUrl) == normalizedUrl(provider.apiUrl)) {
            addId(m.id);
        } else if (!m.displayName.isEmpty()
                   && m.displayName.startsWith(provider.name + QLatin1Char(' '))) {
            addId(m.id);
        }
    }

    // 内置预设清单中属于该服务商的模型
    if (!provider.isCustom && !provider.urlHint.isEmpty()) {
        QFile file(QStringLiteral(":/resources/ai_models.json"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
            file.close();
            if (pe.error == QJsonParseError::NoError && doc.isObject()) {
                for (const QJsonValue &v : doc.object().value(QStringLiteral("models")).toArray()) {
                    const QJsonObject m = v.toObject();
                    if (m.value(QStringLiteral("apiUrl")).toString().contains(provider.urlHint))
                        addId(m.value(QStringLiteral("id")).toString());
                }
            }
        }
    }

    // 自定义供应商自己登记过的模型
    if (provider.isCustom && !provider.providerId.isEmpty()) {
        QSettings store(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
        const QString json = store.value(QStringLiteral("ai/customProviders")).toString();
        if (!json.isEmpty()) {
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
            if (pe.error == QJsonParseError::NoError && doc.isArray()) {
                for (const QJsonValue &v : doc.array()) {
                    const QJsonObject o = v.toObject();
                    if (o.value(QStringLiteral("id")).toString() != provider.providerId)
                        continue;
                    for (const QJsonValue &mv : o.value(QStringLiteral("models")).toArray())
                        addId(mv.toObject().value(QStringLiteral("id")).toString());
                }
            }
        }
    }

    // 旧版扁平表里的同端点模型（合成供应商没有 models 子数组）
    if (provider.rowId.startsWith(QStringLiteral("legacy:"))) {
        QSettings store(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
        const QString json = store.value(QStringLiteral("ai/customModels")).toString();
        if (!json.isEmpty()) {
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
            if (pe.error == QJsonParseError::NoError && doc.isArray()) {
                for (const QJsonValue &v : doc.array()) {
                    const QJsonObject o = v.toObject();
                    if (o.contains(QStringLiteral("providerId")))
                        continue;
                    if (normalizedUrl(o.value(QStringLiteral("apiUrl")).toString())
                        != normalizedUrl(provider.apiUrl))
                        continue;
                    addId(o.value(QStringLiteral("id")).toString());
                }
            }
        }
    }

    if (!provider.selectedModel.isEmpty())
        addId(provider.selectedModel);

    return ids;
}

void ModelSelectDialog::fetchModels(bool force)
{
    Q_UNUSED(force);
    const ProviderEntry provider = currentProvider();
    if (provider.rowId.isEmpty())
        return;
    if (provider.apiUrl.trimmed().isEmpty()) {
        updateStatusLabel(tr("该服务商还没有配置 API 地址，请在设置中补全。"), true);
        return;
    }

    if (m_fetcher->isRunning())
        m_fetcher->cancel();

    m_fetchingRowId = provider.rowId;
    m_refreshBtn->setEnabled(false);
    m_refreshBtn->setText(tr("获取中…"));
    updateStatusLabel(tr("正在从 %1 获取模型列表…").arg(provider.name));
    m_fetcher->fetch(provider.apiUrl, provider.apiKey);
}

void ModelSelectDialog::onFetchFinished(const AiModelListResult& result)
{
    if (m_refreshBtn) {
        m_refreshBtn->setEnabled(true);
        m_refreshBtn->setText(tr("刷新列表"));
    }

    const ProviderEntry provider = providerForRow(m_fetchingRowId);
    m_fetchingRowId.clear();

    if (!result.ok) {
        if (!provider.rowId.isEmpty() && provider.rowId == m_selectedRowId)
            updateStatusLabel(result.error, true);
        return;
    }

    if (!provider.rowId.isEmpty())
        AiModelListFetcher::storeCache(provider.apiUrl, result.ids);

    if (!provider.rowId.isEmpty() && provider.rowId == m_selectedRowId) {
        applyIds(result.ids);
        updateStatusLabel(tr("已从服务商获取 %1 个模型（列表已缓存，下次打开无需重新获取）")
                              .arg(result.ids.size()));
    }
}

void ModelSelectDialog::updateStatusLabel(const QString& text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error
        ? StyleKit::resolve(QStringLiteral("color: @DANGER@; font-size: 11px;"))
        : StyleKit::captionLabel());
}

// ============================================================================
// 选择模型
// ============================================================================

QString ModelSelectDialog::displayNameFor(const ProviderEntry& provider, const QString& id) const
{
    const QString known = m_knownNames.value(id);
    if (!known.isEmpty())
        return known;
    if (provider.name.isEmpty())
        return id;
    return provider.name + QLatin1Char(' ') + id;
}

AiModel ModelSelectDialog::makeModel(const ProviderEntry& provider, const QString& id) const
{
    AiModel model;
    model.id = id;
    model.displayName = displayNameFor(provider, id);
    model.apiUrl = provider.apiUrl;
    model.apiKey = provider.apiKey;
    model.isCustom = true;   // 让聊天页把它写入自定义模型清单，重启后仍能找到
    return model;
}

void ModelSelectDialog::saveProviderDefault(const ProviderEntry& provider, const QString& id)
{
    if (provider.isCustom) {
        QSettings store(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
        const QString json = store.value(QStringLiteral("ai/customProviders")).toString();
        if (json.isEmpty())
            return;
        QJsonParseError pe;
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isArray())
            return;

        QJsonArray providers = doc.array();
        for (int i = 0; i < providers.size(); ++i) {
            QJsonObject o = providers.at(i).toObject();
            if (o.value(QStringLiteral("id")).toString() != provider.providerId)
                continue;
            o.insert(QStringLiteral("defaultModel"), id);
            providers[i] = o;
            break;
        }
        store.setValue(QStringLiteral("ai/customProviders"),
                       QString::fromUtf8(QJsonDocument(providers).toJson(QJsonDocument::Compact)));
        return;
    }

    if (provider.configKey.isEmpty())
        return;
    SettingsManager *settings = SettingsManager::instance();
    settings->setProperty(provider.configKey + QStringLiteral("/selectedModel"), id);
    settings->setProperty(provider.configKey + QStringLiteral("/selectedModelName"),
                          displayNameFor(provider, id));
}

void ModelSelectDialog::chooseModel(const QString& id)
{
    const ProviderEntry provider = currentProvider();
    const QString modelId = id.trimmed();
    if (provider.rowId.isEmpty() || modelId.isEmpty())
        return;

    AiModel model = makeModel(provider, modelId);

    bool found = false;
    for (AiModel &m : m_allModels) {
        if (m.id == modelId) {
            m = model;
            found = true;
            break;
        }
    }
    if (!found)
        m_allModels.append(model);

    saveProviderDefault(provider, modelId);
    m_currentModelId = modelId;

    emit modelConfigured(model);
    emit modelSelected(model);
    accept();
}

void ModelSelectDialog::addManualModel()
{
    if (!m_manualEdit)
        return;
    const QString id = m_manualEdit->text().trimmed();
    if (id.isEmpty())
        return;

    const ProviderEntry provider = currentProvider();

    if (!m_currentIds.contains(id)) {
        m_currentIds.append(id);
        rebuildModelList();
    }

    // 已有缓存时把手工模型并入缓存，保证下次打开还在列表里
    if (!provider.apiUrl.isEmpty()) {
        QStringList cached = AiModelListFetcher::cachedIds(provider.apiUrl);
        if (!cached.isEmpty() && !cached.contains(id)) {
            cached.append(id);
            AiModelListFetcher::storeCache(provider.apiUrl, cached);
        }
    }

    chooseModel(id);
}

// ============================================================================
// 事件处理
// ============================================================================

bool ModelSelectDialog::eventFilter(QObject* obj, QEvent* event)
{
    // 模型列表项：左键选中并关闭窗口
    if (event->type() == QEvent::MouseButtonPress) {
        auto* row = qobject_cast<QWidget*>(obj);
        if (row && !row->property("modelId").toString().isEmpty()) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                chooseModel(row->property("modelId").toString());
                return true;
            }
        }
    }

    // 屏蔽列表项的默认上下文菜单
    if (event->type() == QEvent::ContextMenu) {
        auto* row = qobject_cast<QWidget*>(obj);
        if (row && !row->property("modelId").toString().isEmpty())
            return true;
    }

    return QDialog::eventFilter(obj, event);
}

void ModelSelectDialog::onLocalModelClicked()
{
    // 首次打开时创建独立窗口，后续复用
    if (!m_localModelDialog)
    {
        m_localModelDialog = new LocalModelDialog(this);
        m_localModelDialog->setAttribute(Qt::WA_DeleteOnClose, false);

        // 用户在本地模型对话框中点击"使用此模型"：转发为标准 modelSelected 信号
        connect(m_localModelDialog, &LocalModelDialog::localModelSelected,
                this, [this](const AiModel& model) {
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
        });

        // 本地模型清单发生变化（拉取/删除完成）：通知上层 AiChatPage 刷新 m_models
        connect(m_localModelDialog, &LocalModelDialog::localModelsChanged,
                this, [this]() {
            QList<AiModel> nonLocal;
            for (const AiModel& m : m_allModels)
            {
                if (!m.isLocal)
                {
                    nonLocal.append(m);
                }
            }
            m_allModels = nonLocal + m_localModelDialog->localAiModels();
            rebuildModelList();
            emit localModelsChanged();
        });
    }

    m_localModelDialog->show();
    m_localModelDialog->raise();
    m_localModelDialog->activateWindow();
}
