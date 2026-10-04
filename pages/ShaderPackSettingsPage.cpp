/**
 * @file   ShaderPackSettingsPage.cpp
 * @brief  光影包设置页面实现
 * @author BlockBox Team
 * @date   2026-08-02
 */

#include "ShaderPackSettingsPage.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include "components/AppMessageBox.h"
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "components/BlurLoadingOverlay.h"
#include "components/CustomCheckBox.h"
#include "utils/LanguageManager.h"
#include "utils/ThemeManager.h"

ShaderPackSettingsPage::ShaderPackSettingsPage(QWidget *parent)
    : QWidget(parent)
    , m_parser(new ShaderPackParser(this))
    , m_headerLabel(nullptr)
    , m_statsLabel(nullptr)
    , m_tabWidget(nullptr)
    , m_emptyLabel(nullptr)
    , m_resetBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_loadingOverlay(nullptr)
{
    initUI();
}

ShaderPackSettingsPage::~ShaderPackSettingsPage() = default;

void ShaderPackSettingsPage::initUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // 标题栏（对齐设置页 sectionTitle）
    m_headerLabel = new OutlinedLabel(QString(), this);
    m_headerLabel->setObjectName(QStringLiteral("sectionTitle"));
    m_headerLabel->setWordWrap(true);
    layout->addWidget(m_headerLabel);

    // 统计/目标提示（对齐设置页 sectionSubtitle）
    m_statsLabel = new QLabel();
    m_statsLabel->setObjectName(QStringLiteral("sectionSubtitle"));
    m_statsLabel->setWordWrap(true);
    layout->addWidget(m_statsLabel);

    // 分类分页
    m_tabWidget = new QTabWidget();
    m_tabWidget->setDocumentMode(true);
    layout->addWidget(m_tabWidget, 1);

    // 空提示
    m_emptyLabel = new QLabel(tr("未从该光影包中解析到可配置选项。"));
    m_emptyLabel->setObjectName(QStringLiteral("modPlaceholderLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    layout->addWidget(m_emptyLabel);

    // 底部按钮（对齐设置页：左侧恢复默认 + 右侧主操作）
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);

    m_resetBtn = new QPushButton(tr("恢复默认值"));
    m_resetBtn->setObjectName(QStringLiteral("restoreDefaultsBtn"));
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(m_resetBtn);

    btnRow->addStretch();

    m_saveBtn = new QPushButton(tr("保存设置"));
    m_saveBtn->setObjectName(QStringLiteral("saveButton"));
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setDefault(true);
    btnRow->addWidget(m_saveBtn);

    layout->addLayout(btnRow);

    connect(m_resetBtn, &QPushButton::clicked, this, &ShaderPackSettingsPage::onResetClicked);
    connect(m_saveBtn, &QPushButton::clicked, this, &ShaderPackSettingsPage::onSaveClicked);

    // 解析进度遮罩（复用现有加载进度条组件）
    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();
    connect(m_parser, &ShaderPackParser::progressChanged, this,
            [this](int percent, const QString &phase) {
                m_loadingOverlay->updateProgress(percent, phase);
                // 解析为同步阻塞，强制刷新 UI 让进度条实时更新
                QCoreApplication::processEvents();
            });
}

void ShaderPackSettingsPage::setCurrentPack(const QString &packPath, const QString &gameDir)
{
    m_packPath = packPath;
    m_gameDir = gameDir;

    // 重新解析前清空上一次的界面内容
    clearForm();

    // 延迟到事件循环中解析：页面刚切换为当前页，若立即同步解析，会在
    // 隐藏/未绘制状态下执行 window()->grab() 整窗抓图，导致 UI 卡死无响应。
    // 用 singleShot(0) 让本帧先完成布局与绘制，再由 buildTabs 显示遮罩，
    // 解析进度随 progressChanged + processEvents() 实时刷新。
    const int gen = ++m_parseGeneration;
    QTimer::singleShot(0, this, [this, gen]() {
        if (gen != m_parseGeneration)
            return; // 已被更新的 setCurrentPack 作废
        buildTabs();
    });
}

void ShaderPackSettingsPage::reset()
{
    m_packPath.clear();
    m_gameDir.clear();
    m_loaded = false;
    ++m_parseGeneration; // 作废可能在排队的延迟解析回调
    clearForm();
}

void ShaderPackSettingsPage::clearForm()
{
    if (m_tabWidget)
        m_tabWidget->clear();
    m_bindings.clear();
    m_headerLabel->clear();
    m_statsLabel->clear();
    m_emptyLabel->hide();
    m_saveBtn->setEnabled(false);
    m_resetBtn->setEnabled(false);
}

void ShaderPackSettingsPage::buildTabs()
{
    m_loadingOverlay->showOverlay(tr("正在解析光影包..."));
    QCoreApplication::processEvents();

    if (!m_parser->parsePack(m_packPath))
    {
        m_loadingOverlay->hideOverlay();
        m_headerLabel->setText(tr("无法解析光影包：%1").arg(m_packPath));
        m_statsLabel->setText(tr("该文件可能不是有效的光影包，或已被损坏。"));
        m_saveBtn->setEnabled(false);
        m_resetBtn->setEnabled(false);
        return;
    }
    m_loadingOverlay->hideOverlay();

    m_parser->loadCurrentValues(m_gameDir);
    m_loaded = true;

    // 按软件界面语言自动匹配光影包显示语言（对齐 Iris 语言回退规则）
    {
        const LanguageManager::Language lang = LanguageManager::instance()->currentLanguage();
        QString appLangCode;
        switch (lang)
        {
        case LanguageManager::English:
            appLangCode = QStringLiteral("en");
            break;
        case LanguageManager::ChineseTraditional:
            appLangCode = QStringLiteral("zh_Hant");
            break;
        case LanguageManager::Spanish:
            appLangCode = QStringLiteral("es");
            break;
        case LanguageManager::Chinese:
        default:
            appLangCode = QStringLiteral("zh");
            break;
        }
        m_parser->autoSetLanguage(appLangCode);
    }

    m_headerLabel->setText(tr("光影包：%1").arg(m_parser->packDisplayName()));

    const QList<ShaderPackParser::ScreenCategory> &screens = m_parser->screens();
    if (screens.isEmpty() || m_parser->options().isEmpty())
    {
        m_emptyLabel->show();
        m_saveBtn->setEnabled(false);
        m_resetBtn->setEnabled(false);
        updateStatsLabel();
        return;
    }

    m_saveBtn->setEnabled(true);
    m_resetBtn->setEnabled(true);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());

    // 只把主屏及其直接引用的子分类建为顶层 tab（对齐 Iris OptionMenuContainer：
    // 主屏是入口，screen 指令中的 [xxx] 是一级分类），更深层级在页内递归分组。
    const ShaderPackParser::ScreenCategory &main = m_parser->mainScreen();

    // 主屏自身 + 主屏 subScreens 引用的分类（按主屏声明顺序）
    QList<const ShaderPackParser::ScreenCategory *> topScreens;
    topScreens << &main;
    for (const QString &subKey : main.subScreens)
    {
        const ShaderPackParser::ScreenCategory *sub = findScreen(subKey);
        if (sub && sub != &main)
            topScreens << sub;
    }
    // 若主屏未定义任何子分类链接（极端情况），再兜底追加其余分类
    if (topScreens.size() == 1)
    {
        for (const ShaderPackParser::ScreenCategory &cat : screens)
        {
            if (&cat != &main && !topScreens.contains(&cat))
                topScreens << &cat;
        }
    }

    for (const ShaderPackParser::ScreenCategory *cat : topScreens)
    {
        // 每个一级分类一页：外层滚动区 + 内层布局
        auto *page = new QWidget();
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(0);

        auto *scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *inner = new QWidget();
        inner->setObjectName(QStringLiteral("shaderFormContainer"));
        auto *innerLayout = new QVBoxLayout(inner);
        innerLayout->setContentsMargins(0, 0, 0, 0);
        innerLayout->setSpacing(0);

        QSet<QString> visited;
        renderCategory(*cat, innerLayout, visited, true);

        innerLayout->addStretch();
        scroll->setWidget(inner);
        pageLayout->addWidget(scroll);
        const QString title = cat->displayName.isEmpty() ? cat->key : cat->displayName;
        m_tabWidget->addTab(page, title);
    }

    updateStatsLabel();
}

void ShaderPackSettingsPage::renderCategory(const ShaderPackParser::ScreenCategory &cat,
                                            QVBoxLayout *parentLayout,
                                            QSet<QString> visited,
                                            bool isLastInParent)
{
    if (visited.contains(cat.key))
        return; // 防循环引用（如 fog 与 colored_lights 都引用 lpv_vl）
    visited.insert(cat.key);

    // 直属选项（跳过 "*" 占位等无效名），记录最后一个有效选项
    QList<ShaderPackParser::Option *> validOptions;
    for (const QString &name : cat.options)
    {
        ShaderPackParser::Option *opt = m_parser->findOption(name);
        if (opt)
            validOptions.append(opt);
    }
    const bool hasGroups = !cat.subScreens.isEmpty();
    for (int i = 0; i < validOptions.size(); ++i)
    {
        const bool last = isLastInParent && !hasGroups
                          && (i == validOptions.size() - 1);
        renderOptionRow(*validOptions.at(i), parentLayout, last);
    }

    // 嵌套子分类：QGroupBox 分组递归渲染（支持二级、三级……任意层级）
    for (int i = 0; i < cat.subScreens.size(); ++i)
    {
        const QString &subKey = cat.subScreens.at(i);
        const ShaderPackParser::ScreenCategory *sub = findScreen(subKey);
        if (!sub || visited.contains(subKey))
            continue;
        auto *group = new QGroupBox(sub->displayName.isEmpty() ? sub->key : sub->displayName);
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(8, 6, 8, 6);
        groupLayout->setSpacing(4);
        renderCategory(*sub, groupLayout, visited,
                       isLastInParent && (i == cat.subScreens.size() - 1));
        if (groupLayout->count() > 0)
            parentLayout->addWidget(group);
        else
            delete group;
    }
}

const ShaderPackParser::ScreenCategory *ShaderPackSettingsPage::findScreen(const QString &key) const
{
    const QList<ShaderPackParser::ScreenCategory> &screens = m_parser->screens();
    for (const ShaderPackParser::ScreenCategory &cat : screens)
    {
        if (cat.key == key)
            return &cat;
    }
    return nullptr;
}

void ShaderPackSettingsPage::renderOptionRow(const ShaderPackParser::Option &opt,
                                             QVBoxLayout *parentLayout,
                                             bool isLast)
{
    // 每项一行（对齐设置页 settingRow）：左侧标题+说明，右侧控件
    auto *row = new QFrame();
    row->setObjectName(QStringLiteral("settingRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    if (isLast)
        row->setProperty("lastRow", true);

    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(24, 16, 24, 16);
    rowLayout->setSpacing(20);

    auto *info = new QWidget(row);
    info->setObjectName(QStringLiteral("settingInfo"));
    auto *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    auto *nameLabel = new QLabel(opt.displayName.isEmpty() ? opt.name : opt.displayName, info);
    nameLabel->setObjectName(QStringLiteral("settingTitle"));
    nameLabel->setToolTip(opt.description);
    nameLabel->setWordWrap(false);
    infoLayout->addWidget(nameLabel);

    if (!opt.description.isEmpty())
    {
        auto *descLabel = new QLabel(opt.description, info);
        descLabel->setObjectName(QStringLiteral("settingDesc"));
        descLabel->setWordWrap(true);
        infoLayout->addWidget(descLabel);
    }

    rowLayout->addWidget(info, 1);

    ControlBinding binding;
    binding.optionName = opt.name;

    if (opt.type == QStringLiteral("bool"))
    {
        auto *sw = new CustomCheckBox(opt.currentValue == QStringLiteral("true") ? tr("开") : tr("关"));
        sw->setChecked(opt.currentValue == QStringLiteral("true"));
        connect(sw, &CustomCheckBox::toggled, this, [this, sw](bool checked) {
            sw->setText(checked ? tr("开") : tr("关"));
            onValueChanged();
        });
        binding.switchBox = sw;
        rowLayout->addWidget(sw);
    }
    else if (!opt.values.isEmpty())
    {
        // 字符串选项：允许值列表
        auto *combo = new QComboBox();
        combo->setCursor(Qt::PointingHandCursor);
        for (const QString &v : opt.values)
        {
            QString label = opt.valueLabels.value(v, v);
            if (!opt.valuePrefix.isEmpty() || !opt.valueSuffix.isEmpty())
                label = opt.valuePrefix + label + opt.valueSuffix;
            combo->addItem(label, v);
        }
        int idx = combo->findData(opt.currentValue);
        combo->setCurrentIndex(qBound(0, idx, combo->count() - 1));
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ShaderPackSettingsPage::onValueChanged);
        binding.comboBox = combo;
        rowLayout->addWidget(combo);
    }
    else
    {
        auto *edit = new QLineEdit(opt.currentValue);
        edit->setPlaceholderText(opt.name);
        connect(edit, &QLineEdit::textChanged,
                this, &ShaderPackSettingsPage::onValueChanged);
        binding.lineEdit = edit;
        rowLayout->addWidget(edit);
    }

    parentLayout->addWidget(row);
    m_bindings.append(binding);
}

void ShaderPackSettingsPage::updateStatsLabel()
{
    const QList<ShaderPackParser::Option> &options = m_parser->options();
    m_statsLabel->setText(tr("共解析到 %1 项设置，将写入 %2 配置文件。")
                              .arg(options.size())
                              .arg(m_parser->configTargetName()));
}

void ShaderPackSettingsPage::onValueChanged()
{
    if (m_updating)
        return;

    m_updating = true;
    for (const ControlBinding &b : m_bindings)
    {
        ShaderPackParser::Option *opt = m_parser->findOption(b.optionName);
        if (!opt)
            continue;
        if (b.switchBox)
        {
            opt->currentValue = b.switchBox->isChecked() ? QStringLiteral("true")
                                                         : QStringLiteral("false");
        }
        else if (b.comboBox)
        {
            opt->currentValue = b.comboBox->currentData().toString();
        }
        else if (b.spinBox)
        {
            opt->currentValue = QString::number(b.spinBox->value());
        }
        else if (b.doubleSpinBox)
        {
            opt->currentValue = QString::number(b.doubleSpinBox->value());
        }
        else if (b.lineEdit)
        {
            opt->currentValue = b.lineEdit->text();
        }
    }
    m_updating = false;
}

void ShaderPackSettingsPage::onSaveClicked()
{
    onValueChanged();
    if (m_parser->saveConfig(m_gameDir))
    {
        emit settingsSaved();
        AppMessageBox::information(this, tr("保存成功"),
                                 tr("光影包设置已写入 %1 配置文件。\n"
                                    "重新启动游戏后生效。")
                                     .arg(m_parser->configTargetName()));
    }
    else
    {
        AppMessageBox::warning(this, tr("保存失败"),
                             tr("写入配置文件失败，请检查目录权限。"));
    }
}

void ShaderPackSettingsPage::onResetClicked()
{
    m_parser->resetToDefaults();

    m_updating = true;
    const QList<ShaderPackParser::Option> &options = m_parser->options();
    for (const ShaderPackParser::Option &opt : options)
    {
        for (const ControlBinding &b : m_bindings)
        {
            if (b.optionName != opt.name)
                continue;
            if (b.switchBox)
            {
                b.switchBox->setChecked(opt.defaultValue == QStringLiteral("true"));
                b.switchBox->setText(opt.defaultValue == QStringLiteral("true") ? tr("开") : tr("关"));
            }
            else if (b.comboBox)
            {
                int idx = b.comboBox->findData(opt.defaultValue);
                b.comboBox->setCurrentIndex(qBound(0, idx, b.comboBox->count() - 1));
            }
            else if (b.spinBox)
            {
                b.spinBox->setValue(opt.defaultValue.toInt());
            }
            else if (b.doubleSpinBox)
            {
                b.doubleSpinBox->setValue(opt.defaultValue.toDouble());
            }
            else if (b.lineEdit)
            {
                b.lineEdit->setText(opt.defaultValue);
            }
            break;
        }
    }
    m_updating = false;
}
