/**
 * @file   LocalModSettingsPage.cpp
 * @brief  本地模组设置页面实现
 * @author BlockBox Team
 * @date   2026-08-29
 */

#include "LocalModSettingsPage.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmapCache>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <limits>

#include "ResourcesPage.h"
#include "utils/mod/ModDisplay.h"
#include "components/AppMessageBox.h"
#include "components/CustomCheckBox.h"
#include "components/NotificationManager.h"
#include "components/CollapsibleSectionCard.h"
#include "utils/LanguageManager.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"
#include "utils/FileExplorer.h"

namespace {

/** 与 ResourceReferenceDialog::resolveResourceDirFor 一致的目录解析:
 *  版本隔离实例优先用实例目录内 config/,非隔离回退上级 .minecraft/config/ */
QString resolveConfigDir(const QString &instancePath)
{
    const QString isolated = instancePath + QStringLiteral("/config");
    if (QDir(isolated).exists())
        return isolated;
    QDir parentDir(instancePath);
    if (parentDir.cdUp())
    {
        const QString shared = parentDir.absoluteFilePath(QStringLiteral("config"));
        if (QDir(shared).exists())
            return shared;
    }
    return isolated; // 都不存在时返回隔离路径(由上层提示未生成)
}

/** 将 TOML 字面 token 还原为展示文本:去引号 */
QString tokenDisplayText(const QString &tok)
{
    if (tok.size() >= 2 && tok.startsWith(QLatin1Char('"')) && tok.endsWith(QLatin1Char('"')))
        return ModConfigParser::unescapeTomlString(tok.mid(1, tok.size() - 2));
    return tok;
}

/** 比较设置项当前值与表单新值(StringList 按展示口径比较) */
bool optionValueEquals(const ModConfigParser::Option &opt, const QVariant &newVal)
{
    using VT = ModConfigParser::ValueType;
    switch (opt.type)
    {
    case VT::Boolean:
        return opt.value.toBool() == newVal.toBool();
    case VT::Integer:
        return opt.value.toLongLong() == newVal.toLongLong();
    case VT::Floating:
        return opt.value.toDouble() == newVal.toDouble();
    case VT::String:
        return opt.value.toString() == newVal.toString();
    case VT::StringList:
    {
        QStringList current;
        for (const QString &t : opt.value.toStringList())
            current.append(tokenDisplayText(t));
        return current == newVal.toStringList();
    }
    default:
        return true;
    }
}

} // namespace

LocalModSettingsPage::LocalModSettingsPage(QWidget *parent)
    : QWidget(parent)
    , m_iconLabel(nullptr)
    , m_nameLabel(nullptr)
    , m_metaLabel(nullptr)
    , m_toggleBtn(nullptr)
    , m_openModFolderBtn(nullptr)
    , m_openConfigFolderBtn(nullptr)
    , m_fileLabel(nullptr)
    , m_fileCombo(nullptr)
    , m_formContainer(nullptr)
    , m_formLayout(nullptr)
    , m_emptyLabel(nullptr)
    , m_resetBtn(nullptr)
    , m_saveBtn(nullptr)
{
    initUI();

    // 界面语言切换后按新语言刷新头部名称与头像
    connect(LanguageManager::instance(), &LanguageManager::languageChanged,
            this, &LocalModSettingsPage::refreshHeader);
}

LocalModSettingsPage::~LocalModSettingsPage()
{
}

void LocalModSettingsPage::initUI()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *contentWidget = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(contentWidget);
    layout->setContentsMargins(UiMetrics::kPagePadH, 8, UiMetrics::kPagePadH, 24);
    layout->setSpacing(16);

    // ── 模组名称与元信息 ──
    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(12);

    m_iconLabel = new QLabel(contentWidget);
    m_iconLabel->setFixedSize(40, 40);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    headerRow->addWidget(m_iconLabel);

    m_nameLabel = new QLabel(contentWidget);
    m_nameLabel->setObjectName("listTitle");
    headerRow->addWidget(m_nameLabel);
    headerRow->addStretch();
    layout->addLayout(headerRow);

    m_metaLabel = new QLabel(contentWidget);
    m_metaLabel->setObjectName(QStringLiteral("sectionSubtitle"));
    m_metaLabel->setWordWrap(true);
    layout->addWidget(m_metaLabel);

    // ── 快捷操作行 ──
    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(10);

    m_toggleBtn = new QPushButton(tr("启用/禁用"), contentWidget);
    m_toggleBtn->setObjectName(QStringLiteral("bottomActionBtn"));
    m_toggleBtn->setCursor(Qt::PointingHandCursor);
    connect(m_toggleBtn, &QPushButton::clicked,
            this, &LocalModSettingsPage::onToggleEnabledClicked);
    actionRow->addWidget(m_toggleBtn);

    m_openModFolderBtn = new QPushButton(tr("打开模组文件位置"), contentWidget);
    m_openModFolderBtn->setObjectName(QStringLiteral("bottomActionBtn"));
    m_openModFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_openModFolderBtn, &QPushButton::clicked,
            this, &LocalModSettingsPage::onOpenModFolderClicked);
    actionRow->addWidget(m_openModFolderBtn);

    m_openConfigFolderBtn = new QPushButton(tr("打开配置文件夹"), contentWidget);
    m_openConfigFolderBtn->setObjectName(QStringLiteral("bottomActionBtn"));
    m_openConfigFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_openConfigFolderBtn, &QPushButton::clicked,
            this, &LocalModSettingsPage::onOpenConfigFolderClicked);
    actionRow->addWidget(m_openConfigFolderBtn);

    actionRow->addStretch();
    layout->addLayout(actionRow);

    // ── 配置文件选择卡片 ──
    QVBoxLayout *fileCardLayout = createSettingsCard(layout, tr("配置文件"));

    m_fileCombo = new QComboBox();
    m_fileCombo->setMinimumWidth(320);
    m_fileCombo->setCursor(Qt::PointingHandCursor);
    connect(m_fileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LocalModSettingsPage::onConfigFileChanged);

    QHBoxLayout *fileRow = appendSettingRow(fileCardLayout, tr("选择配置文件"), QString(), true);
    fileRow->addWidget(m_fileCombo, 1);

    m_fileLabel = new QLabel(tr("配置文件"));
    m_fileLabel->hide();

    // ── 表单区 ──
    m_formContainer = new QWidget(contentWidget);
    m_formLayout = new QVBoxLayout(m_formContainer);
    m_formLayout->setContentsMargins(0, 0, 0, 0);
    m_formLayout->setSpacing(12);
    layout->addWidget(m_formContainer);

    m_emptyLabel = new QLabel(contentWidget);
    m_emptyLabel->setObjectName(QStringLiteral("modPlaceholderLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->hide();
    layout->addWidget(m_emptyLabel);

    layout->addStretch();

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea, 1);

    // ── 底部操作(对齐设置页:左侧恢复默认 + 右侧主操作) ──
    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(UiMetrics::kPagePadH, 0, UiMetrics::kPagePadH, 16);
    btnRow->setSpacing(10);

    m_resetBtn = new QPushButton(tr("恢复默认值"), this);
    m_resetBtn->setObjectName(QStringLiteral("restoreDefaultsBtn"));
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    connect(m_resetBtn, &QPushButton::clicked,
            this, &LocalModSettingsPage::onResetDefaultsClicked);
    btnRow->addWidget(m_resetBtn);

    btnRow->addStretch();

    m_saveBtn = new QPushButton(tr("保存设置"), this);
    m_saveBtn->setObjectName(QStringLiteral("saveButton"));
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setDefault(true);
    connect(m_saveBtn, &QPushButton::clicked,
            this, &LocalModSettingsPage::onSaveClicked);
    btnRow->addWidget(m_saveBtn);

    outerLayout->addLayout(btnRow);
}

void LocalModSettingsPage::setCurrentMod(const ModInfo &info, const QString &instancePath)
{
    m_modInfo = info;
    m_instancePath = instancePath;
    m_configDir.clear();
    if (!instancePath.isEmpty())
        m_configDir = resolveConfigDir(instancePath);

    clearForm();
    refreshHeader();
    refreshToggleState();
    reloadConfigFiles();
}

void LocalModSettingsPage::reset()
{
    m_modInfo = ModInfo();
    m_instancePath.clear();
    m_configDir.clear();
    m_configFiles.clear();
    m_currentFileIndex = -1;
    m_loaded = false;
    clearForm();
    m_nameLabel->clear();
    m_metaLabel->clear();
    m_iconLabel->hide();
    m_fileCombo->blockSignals(true);
    m_fileCombo->clear();
    m_fileCombo->blockSignals(false);
    m_fileLabel->hide();
    m_fileCombo->hide();
    m_toggleBtn->setEnabled(false);
    m_openModFolderBtn->setEnabled(false);
    m_openConfigFolderBtn->setEnabled(false);
}

void LocalModSettingsPage::clearForm()
{
    if (m_formLayout)
    {
        QLayoutItem *item = nullptr;
        while ((item = m_formLayout->takeAt(0)) != nullptr)
        {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
    }
    m_bindings.clear();
    m_dirty = false;
    m_emptyLabel->hide();
    m_saveBtn->setEnabled(false);
    m_resetBtn->setEnabled(false);
}

QString LocalModSettingsPage::configDir() const
{
    return m_configDir;
}

QString LocalModSettingsPage::modDisplayName() const
{
    // 跟随界面语言(统一走 ModDisplay),回退到去后缀文件名
    return ModDisplay::primaryName(m_modInfo, ModDisplay::fileStem(m_modInfo));
}

QString LocalModSettingsPage::formatFileSize(qint64 bytes) const
{
    if (bytes <= 0)
        return QString();
    const double kb = bytes / 1024.0;
    const double mb = kb / 1024.0;
    if (mb >= 1.0)
        return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
    if (kb >= 1.0)
        return QStringLiteral("%1 KB").arg(kb, 0, 'f', 0);
    return QStringLiteral("%1 B").arg(bytes);
}

void LocalModSettingsPage::refreshHeader()
{
    if (m_modInfo.filePath.isEmpty())
    {
        m_nameLabel->clear();
        m_metaLabel->clear();
        m_iconLabel->hide();
        return;
    }
    m_nameLabel->setText(modDisplayName());
    m_iconLabel->show();

    // 真实 logo 提取(带缓存),失败时显示首字母占位头像
    const QPixmap icon = loadModIcon(40);
    if (!icon.isNull())
    {
        m_iconLabel->setPixmap(icon);
    }
    else
    {
        QPixmap letter(40, 40);
        letter.fill(QColor("#4CAF50"));
        QPainter painter(&letter);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::white);
        QFont font;
        font.setBold(true);
        font.setPixelSize(18);
        painter.setFont(font);
        const QString text = modDisplayName().left(1).toUpper();
        painter.drawText(letter.rect(), Qt::AlignCenter,
                         text.isEmpty() ? QStringLiteral("?") : text);
        painter.end();
        m_iconLabel->setPixmap(letter);
    }

    QStringList parts;
    if (!m_modInfo.latestVersion.isEmpty())
        parts << m_modInfo.latestVersion;
    if (!m_modInfo.id.isEmpty())
        parts << m_modInfo.id;
    if (!m_modInfo.loaderType.isEmpty())
        parts << m_modInfo.loaderType;
    const QString sizeText = formatFileSize(m_modInfo.fileSize);
    if (!sizeText.isEmpty())
        parts << sizeText;
    parts << (m_modInfo.enabled ? tr("已启用") : tr("已禁用"));
    m_metaLabel->setText(parts.join(QStringLiteral(" · ")));
}

QPixmap LocalModSettingsPage::loadModIcon(int size) const
{
    if (m_modInfo.filePath.isEmpty())
        return QPixmap();
    QString realPath = m_modInfo.filePath;
    if (realPath.endsWith(QLatin1String(".disabled"), Qt::CaseInsensitive))
        realPath.chop(9);
    if (realPath.isEmpty())
        return QPixmap();
    // 缓存键与模组列表页一致
    const QString cacheKey = QStringLiteral("rp_icon:%1").arg(realPath);
    QPixmap icon;
    if (!QPixmapCache::find(cacheKey, &icon))
    {
        icon = ResourcesPage::extractModIcon(realPath);
        if (!icon.isNull())
            QPixmapCache::insert(cacheKey, icon);
    }
    if (icon.isNull())
        return QPixmap();
    return icon.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void LocalModSettingsPage::refreshToggleState()
{
    const bool hasMod = !m_modInfo.filePath.isEmpty();
    m_toggleBtn->setEnabled(hasMod);
    m_openModFolderBtn->setEnabled(hasMod);
    m_openConfigFolderBtn->setEnabled(!m_configDir.isEmpty());
    if (hasMod)
        m_toggleBtn->setText(m_modInfo.enabled ? tr("禁用模组") : tr("启用模组"));
}

void LocalModSettingsPage::reloadConfigFiles()
{
    m_fileCombo->blockSignals(true);
    m_fileCombo->clear();

    m_configFiles = ModConfigParser::collectConfigFiles(m_modInfo, m_configDir);
    if (m_configFiles.isEmpty())
    {
        m_fileCombo->blockSignals(false);
        m_fileLabel->hide();
        m_fileCombo->hide();
        m_loaded = false;
        clearForm();
        m_emptyLabel->setText(
            tr("未在实例 config 目录找到该模组的配置文件。\n"
               "部分模组的配置文件需先启动一次游戏才会生成。"));
        m_emptyLabel->show();
        return;
    }

    for (const QString &path : m_configFiles)
    {
        m_fileCombo->addItem(QFileInfo(path).fileName(), path);
        m_fileCombo->setItemData(m_fileCombo->count() - 1, path, Qt::ToolTipRole);
    }
    // 多于一个文件时允许切换;仅一个时禁用但保留展示
    const bool multiple = m_configFiles.size() > 1;
    m_fileLabel->setVisible(true);
    m_fileCombo->setVisible(true);
    m_fileCombo->setEnabled(multiple);
    m_fileCombo->blockSignals(false);

    parseCurrentFile();
}

void LocalModSettingsPage::parseCurrentFile()
{
    const QString path = m_fileCombo->currentData().toString();
    if (path.isEmpty())
    {
        m_loaded = false;
        m_currentFileIndex = -1;
        clearForm();
        return;
    }
    m_currentFileIndex = m_fileCombo->currentIndex();

    ModConfigParser::Document doc;
    if (!ModConfigParser::parse(path, doc))
    {
        m_loaded = false;
        clearForm();
        m_emptyLabel->setText(tr("无法解析配置文件:%1").arg(doc.error));
        m_emptyLabel->show();
        return;
    }

    m_doc = doc;
    m_loaded = true;
    buildForm();
}

void LocalModSettingsPage::buildForm()
{
    clearForm();
    m_updating = true;

    int visibleOptions = 0;
    for (int si = 0; si < m_doc.sections.size(); ++si)
    {
        const ModConfigParser::Section &sec = m_doc.sections.at(si);
        if (sec.options.isEmpty())
            continue;
        renderSection(si, m_formLayout);
        visibleOptions += sec.options.size();
    }

    m_updating = false;
    if (visibleOptions == 0)
    {
        m_emptyLabel->setText(tr("该配置文件中没有可编辑的设置项。"));
        m_emptyLabel->show();
    }
}

void LocalModSettingsPage::renderSection(int sectionIndex, QVBoxLayout *parentLayout)
{
    const ModConfigParser::Section &sec = m_doc.sections.at(sectionIndex);

    QString title = sec.path.isEmpty() ? tr("常规设置") : sec.path;
    auto *card = new CollapsibleSectionCard(title, false, m_formContainer);
    QVBoxLayout *cardLayout = card->contentLayout();

    for (int oi = 0; oi < sec.options.size(); ++oi)
        addOptionRow(cardLayout, sectionIndex, oi);

    parentLayout->addWidget(card);
}

QString LocalModSettingsPage::optionTooltip(int sectionIndex, int optionIndex) const
{
    const ModConfigParser::Option &opt =
        m_doc.sections.at(sectionIndex).options.at(optionIndex);

    QStringList lines;
    if (!opt.comment.isEmpty())
        lines << opt.comment;
    if (opt.defaultValue.isValid() && !opt.defaultValue.toString().isEmpty())
        lines << tr("默认值:%1").arg(opt.defaultValue.toString());
    if (opt.hasRange)
        lines << tr("范围:%1 ~ %2").arg(opt.rangeMin).arg(opt.rangeMax);
    if (!opt.allowedValues.isEmpty())
        lines << tr("可选值:%1").arg(opt.allowedValues.join(QStringLiteral(", ")));
    return lines.join(QLatin1Char('\n'));
}

void LocalModSettingsPage::addOptionRow(QVBoxLayout *parentLayout, int sectionIndex,
                                        int optionIndex)
{
    const ModConfigParser::Option &opt =
        m_doc.sections.at(sectionIndex).options.at(optionIndex);

    QFrame *row = new QFrame();
    row->setObjectName("settingRow");
    row->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(24, 16, 24, 16);
    rowLayout->setSpacing(20);

    QWidget *info = new QWidget(row);
    info->setObjectName("settingInfo");
    QVBoxLayout *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    QLabel *keyLabel = new QLabel(opt.key, info);
    keyLabel->setObjectName("settingTitle");
    keyLabel->setToolTip(optionTooltip(sectionIndex, optionIndex));
    infoLayout->addWidget(keyLabel);

    if (!opt.comment.isEmpty())
    {
        QLabel *descLabel = new QLabel(opt.comment, info);
        descLabel->setObjectName("settingDesc");
        descLabel->setWordWrap(true);
        infoLayout->addWidget(descLabel);
    }

    rowLayout->addWidget(info, 1);

    // 复杂结构:只读展示原文
    if (opt.unsupported)
    {
        QLabel *rawLabel = new QLabel(opt.rawText, row);
        rawLabel->setObjectName("settingValueLabel");
        rawLabel->setToolTip(optionTooltip(sectionIndex, optionIndex));
        rowLayout->addWidget(rawLabel, 1);
        parentLayout->addWidget(row);
        return;
    }

    Binding binding;
    binding.sectionIndex = sectionIndex;
    binding.optionIndex = optionIndex;

    using VT = ModConfigParser::ValueType;
    // 推断控件类型
    OptionKind kind = OptionKind::Unsupported;
    if (opt.type == VT::Boolean)
        kind = OptionKind::Bool;
    else if (opt.type == VT::Integer)
        kind = OptionKind::Int;
    else if (opt.type == VT::Floating)
        kind = OptionKind::Double;
    else if (opt.type == VT::String)
        kind = opt.allowedValues.isEmpty() ? OptionKind::String : OptionKind::Enum;
    else if (opt.type == VT::StringList)
        kind = OptionKind::StringList;

    switch (kind)
    {
    case OptionKind::Bool:
    {
        CustomCheckBox *sw = new CustomCheckBox(QString(), row);
        sw->setChecked(opt.value.toBool());
        connect(sw, &CustomCheckBox::toggled, this, [this](bool) {
            if (!m_updating)
                m_dirty = true;
        });
        binding.kind = kind;
        binding.switchBox = sw;
        rowLayout->addStretch();
        rowLayout->addWidget(sw);
        break;
    }
    case OptionKind::Enum:
    {
        QComboBox *combo = new QComboBox(row);
        combo->addItems(opt.allowedValues);
        const QString current = opt.value.toString();
        int idx = combo->findText(current);
        if (idx < 0)
        {
            combo->addItem(current);
            idx = combo->count() - 1;
        }
        combo->setCurrentIndex(idx);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    if (!m_updating)
                        m_dirty = true;
                });
        binding.kind = kind;
        binding.comboBox = combo;
        rowLayout->addStretch();
        rowLayout->addWidget(combo);
        break;
    }
    case OptionKind::Int:
    {
        const qlonglong value = opt.value.toLongLong();
        const int intMin = std::numeric_limits<int>::min();
        const int intMax = std::numeric_limits<int>::max();
        if (value >= intMin && value <= intMax)
        {
            QSpinBox *spin = new QSpinBox(row);
            if (opt.hasRange)
            {
                const qlonglong lo = qBound<qlonglong>(
                    (qlonglong)intMin, (qlonglong)std::ceil(opt.rangeMin), (qlonglong)intMax);
                const qlonglong hi = qBound<qlonglong>(
                    (qlonglong)intMin, (qlonglong)std::floor(opt.rangeMax), (qlonglong)intMax);
                spin->setRange((int)lo, (int)hi);
            }
            else
            {
                spin->setRange(intMin, intMax);
            }
            spin->setValue((int)value);
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                    this, [this](int) {
                        if (!m_updating)
                            m_dirty = true;
                    });
            binding.kind = kind;
            binding.spinBox = spin;
            rowLayout->addStretch();
            rowLayout->addWidget(spin);
        }
        else
        {
            // 超出 32 位整数范围:退化为文本输入(收集时按 Int 校验转换)
            QLineEdit *edit = new QLineEdit(QString::number(value), row);
            connect(edit, &QLineEdit::textChanged, this, [this](const QString &) {
                if (!m_updating)
                    m_dirty = true;
            });
            binding.kind = OptionKind::Int;
            binding.lineEdit = edit;
            rowLayout->addWidget(edit, 1);
        }
        break;
    }
    case OptionKind::Double:
    {
        QDoubleSpinBox *spin = new QDoubleSpinBox(row);
        if (opt.hasRange)
        {
            spin->setRange(opt.rangeMin, opt.rangeMax);
            const double span = opt.rangeMax - opt.rangeMin;
            if (span > 0)
                spin->setSingleStep(span / 100.0);
        }
        else
        {
            spin->setRange(-1e12, 1e12);
            spin->setSingleStep(0.1);
        }
        spin->setDecimals(6);
        spin->setValue(opt.value.toDouble());
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) {
                    if (!m_updating)
                        m_dirty = true;
                });
        binding.kind = kind;
        binding.doubleSpinBox = spin;
        rowLayout->addStretch();
        rowLayout->addWidget(spin);
        break;
    }
    case OptionKind::String:
    {
        QLineEdit *edit = new QLineEdit(opt.value.toString(), row);
        connect(edit, &QLineEdit::textChanged, this, [this](const QString &) {
            if (!m_updating)
                m_dirty = true;
        });
        binding.kind = kind;
        binding.lineEdit = edit;
        rowLayout->addWidget(edit, 1);
        break;
    }
    case OptionKind::StringList:
    {
        QStringList display;
        for (const QString &tok : opt.value.toStringList())
            display.append(tokenDisplayText(tok));
        QLineEdit *edit = new QLineEdit(display.join(QStringLiteral(", ")), row);
        edit->setToolTip(tr("多个值使用英文逗号分隔"));
        connect(edit, &QLineEdit::textChanged, this, [this](const QString &) {
            if (!m_updating)
                m_dirty = true;
        });
        binding.kind = kind;
        binding.lineEdit = edit;
        rowLayout->addWidget(edit, 1);
        break;
    }
    default:
        break;
    }

    m_bindings.append(binding);
    parentLayout->addWidget(row);
}

bool LocalModSettingsPage::collectConfigEdits()
{
    bool changed = false;
    for (const Binding &b : m_bindings)
    {
        ModConfigParser::Option &opt =
            m_doc.sections[b.sectionIndex].options[b.optionIndex];

        QVariant newVal;
        bool valid = false;
        switch (b.kind)
        {
        case OptionKind::Bool:
            newVal = b.switchBox->isChecked();
            valid = true;
            break;
        case OptionKind::Enum:
            newVal = b.comboBox->currentText();
            valid = true;
            break;
        case OptionKind::Int:
            if (b.spinBox)
            {
                newVal = static_cast<qlonglong>(b.spinBox->value());
                valid = true;
            }
            else if (b.lineEdit)
            {
                bool ok = false;
                const qlonglong v = b.lineEdit->text().toLongLong(&ok);
                if (ok)
                {
                    newVal = v;
                    valid = true;
                }
            }
            break;
        case OptionKind::Double:
            newVal = b.doubleSpinBox->value();
            valid = true;
            break;
        case OptionKind::String:
            newVal = b.lineEdit->text();
            valid = true;
            break;
        case OptionKind::StringList:
        {
            const QStringList tokens =
                b.lineEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
            QStringList out;
            for (QString t : tokens)
            {
                t = t.trimmed();
                if (!t.isEmpty())
                    out.append(t);
            }
            newVal = out;
            valid = true;
            break;
        }
        default:
            break;
        }

        if (!valid || optionValueEquals(opt, newVal))
            continue;
        opt.value = newVal;
        opt.edited = true;
        changed = true;
    }
    return changed;
}

bool LocalModSettingsPage::confirmDiscard()
{
    if (!m_dirty)
        return true;
    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("未保存的修改"),
        tr("当前配置文件有未保存的修改,继续将丢失这些修改。是否继续?"),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);
    return reply == AppMessageBox::Yes;
}

void LocalModSettingsPage::onConfigFileChanged(int index)
{
    if (index == m_currentFileIndex)
        return;
    if (!confirmDiscard())
    {
        // 取消切换:回退下拉选中到已加载的文件
        m_fileCombo->blockSignals(true);
        m_fileCombo->setCurrentIndex(m_currentFileIndex);
        m_fileCombo->blockSignals(false);
        return;
    }
    parseCurrentFile();
}

void LocalModSettingsPage::onToggleEnabledClicked()
{
    if (m_modInfo.filePath.isEmpty())
        return;

    const QString path = m_modInfo.filePath;
    QString target;
    if (m_modInfo.enabled)
        target = path + QStringLiteral(".disabled");
    else
        target = path.endsWith(QLatin1String(".disabled"), Qt::CaseInsensitive)
            ? path.left(path.size() - 9)
            : path;

    if (!QFile::rename(path, target))
    {
        AppMessageBox::warning(this, tr("操作失败"),
                               tr("无法修改模组文件:%1").arg(path));
        return;
    }

    m_modInfo.filePath = target;
    m_modInfo.enabled = !m_modInfo.enabled;
    refreshHeader();
    refreshToggleState();
    emit modSettingsChanged();
}

void LocalModSettingsPage::onOpenModFolderClicked()
{
    if (m_modInfo.filePath.isEmpty())
        return;
    const QString dirPath = QFileInfo(m_modInfo.filePath).absolutePath();
    if (!dirPath.isEmpty())
        FileExplorer::open(this, dirPath);
}

void LocalModSettingsPage::onOpenConfigFolderClicked()
{
    if (m_configDir.isEmpty())
        return;
    if (!QDir(m_configDir).exists())
    {
        NotificationManager::showInfo(this, tr("配置目录尚未生成:%1").arg(m_configDir));
        return;
    }
    FileExplorer::open(this, m_configDir);
}

void LocalModSettingsPage::onSaveClicked()
{
    if (!m_loaded)
        return;
    collectConfigEdits();

    QString error;
    if (!ModConfigParser::save(m_doc, &error))
    {
        AppMessageBox::warning(this, tr("保存失败"),
                               tr("写入配置文件时出错:%1").arg(error));
        return;
    }

    m_dirty = false;
    NotificationManager::showSuccess(this, tr("模组设置已保存"));
}

void LocalModSettingsPage::onResetDefaultsClicked()
{
    if (!m_loaded)
        return;

    m_updating = true;
    bool anyDefault = false;
    for (const Binding &b : m_bindings)
    {
        ModConfigParser::Option &opt =
            m_doc.sections[b.sectionIndex].options[b.optionIndex];
        if (opt.unsupported || !opt.defaultValue.isValid())
            continue;
        const QString d = opt.defaultValue.toString();
        if (d.isEmpty())
            continue;
        anyDefault = true;

        switch (b.kind)
        {
        case OptionKind::Bool:
            if (b.switchBox)
                b.switchBox->setChecked(d.compare(QLatin1String("true"),
                                                  Qt::CaseInsensitive) == 0);
            break;
        case OptionKind::Enum:
            if (b.comboBox)
            {
                int idx = b.comboBox->findText(d);
                if (idx < 0)
                    idx = b.comboBox->findText(d, Qt::MatchFixedString);
                if (idx >= 0)
                    b.comboBox->setCurrentIndex(idx);
            }
            break;
        case OptionKind::Int:
            if (b.spinBox)
                b.spinBox->setValue(d.toInt());
            else if (b.lineEdit)
                b.lineEdit->setText(d);
            break;
        case OptionKind::Double:
            if (b.doubleSpinBox)
                b.doubleSpinBox->setValue(d.toDouble());
            break;
        case OptionKind::StringList:
            if (b.lineEdit)
            {
                QString t = d;
                if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']')))
                    t = t.mid(1, t.size() - 2);
                b.lineEdit->setText(t);
            }
            break;
        case OptionKind::String:
            if (b.lineEdit)
                b.lineEdit->setText(d);
            break;
        default:
            break;
        }

        m_dirty = true;
    }
    m_updating = false;

    if (!anyDefault)
        NotificationManager::showInfo(
            this, tr("该配置文件没有标注默认值,无法恢复。"));
}

QVBoxLayout *LocalModSettingsPage::createSettingsCard(QBoxLayout *parentLayout,
                                                      const QString &title,
                                                      bool startCollapsed)
{
    auto *card = new CollapsibleSectionCard(title, startCollapsed);
    parentLayout->addWidget(card);
    return card->contentLayout();
}

QHBoxLayout *LocalModSettingsPage::appendSettingRow(QVBoxLayout *cardLayout,
                                                    const QString &title,
                                                    const QString &desc,
                                                    bool isLast)
{
    QFrame *row = new QFrame();
    row->setObjectName("settingRow");
    row->setAttribute(Qt::WA_StyledBackground, true);
    if (isLast)
        row->setProperty("lastRow", true);

    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(24, 16, 24, 16);
    rowLayout->setSpacing(20);

    QWidget *info = new QWidget(row);
    info->setObjectName("settingInfo");
    QVBoxLayout *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    if (!title.isEmpty())
    {
        QLabel *titleLabel = new QLabel(title, info);
        titleLabel->setObjectName("settingTitle");
        infoLayout->addWidget(titleLabel);
    }

    if (!desc.isEmpty())
    {
        QLabel *descLabel = new QLabel(desc, info);
        descLabel->setObjectName("settingDesc");
        descLabel->setWordWrap(true);
        infoLayout->addWidget(descLabel);
    }

    rowLayout->addWidget(info, 1);

    cardLayout->addWidget(row);
    return rowLayout;
}
