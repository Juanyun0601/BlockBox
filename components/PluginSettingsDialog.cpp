/**
 * @file   PluginSettingsDialog.cpp
 * @brief  插件设置对话框类实现
 * @author BlockBox Team
 * @date   2026-08-06
 */
#include "PluginSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"
#include "utils/plugin/PluginManager.h"

PluginSettingsDialog::PluginSettingsDialog(const PluginInfo &plugin, QWidget *parent)
    : AppDialogBase(parent)
    , m_plugin(plugin)
    , m_card(nullptr)
    , m_saveBtn(nullptr)
    , m_resetBtn(nullptr)
    , m_cancelBtn(nullptr)
{
    setObjectName(QStringLiteral("pluginSettingsDialog"));
    setWindowTitle(tr("%1 - 插件设置").arg(plugin.name));
    initUI();
    initStyle();
}

void PluginSettingsDialog::initUI()
{
    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("appDialogCard"));
    m_card->setMinimumWidth(420);
    m_card->setMaximumWidth(520);

    QVBoxLayout *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(26, 22, 26, 20);
    cardLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel(tr("「%1」插件设置").arg(m_plugin.name), m_card);
    titleLabel->setObjectName(QStringLiteral("appDialogTitleLabel"));
    cardLayout->addWidget(titleLabel);
    cardLayout->addSpacing(2);

    QScrollArea *scroll = new QScrollArea(m_card);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *formHost = new QWidget(scroll);
    formHost->setObjectName(QStringLiteral("pluginSettingsForm"));
    QFormLayout *form = new QFormLayout(formHost);
    form->setContentsMargins(0, 0, 8, 0);
    form->setSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    for (const PluginSettingItem &item : m_plugin.settings) {
        const QString current = PluginManager::instance()->pluginSetting(m_plugin, item.key);
        QWidget *field = createFieldWidget(item, current);
        if (!field)
            continue;

        QLabel *label = new QLabel(item.label.isEmpty() ? item.key : item.label, formHost);
        label->setObjectName(QStringLiteral("pluginSettingLabel"));
        label->setWordWrap(true);
        form->addRow(label, field);
        m_fieldWidgets.insert(item.key, field);
    }

    if (m_plugin.settings.isEmpty()) {
        QLabel *empty = new QLabel(tr("该插件没有可配置的设置项。"), formHost);
        empty->setObjectName(QStringLiteral("pluginSettingEmpty"));
        empty->setAlignment(Qt::AlignCenter);
        form->addRow(empty);
    }

    scroll->setWidget(formHost);
    scroll->setFixedHeight(280);
    cardLayout->addWidget(scroll, 1);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    btnRow->addStretch();

    m_resetBtn = new QPushButton(tr("恢复默认"), m_card);
    m_resetBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_resetBtn->setCursor(Qt::PointingHandCursor);

    m_cancelBtn = new QPushButton(tr("取消"), m_card);
    m_cancelBtn->setObjectName(QStringLiteral("appDialogBtn"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);

    m_saveBtn = new QPushButton(tr("保存设置"), m_card);
    m_saveBtn->setObjectName(QStringLiteral("appDialogBtnPrimary"));
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setDefault(true);

    btnRow->addWidget(m_resetBtn);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_saveBtn);
    cardLayout->addLayout(btnRow);

    QHBoxLayout *main = new QHBoxLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->addWidget(m_card, 0, Qt::AlignCenter);

    connect(m_saveBtn, &QPushButton::clicked, this, &PluginSettingsDialog::onSave);
    connect(m_resetBtn, &QPushButton::clicked, this, &PluginSettingsDialog::onReset);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

QWidget *PluginSettingsDialog::createFieldWidget(const PluginSettingItem &item,
                                                 const QString &currentValue)
{
    const QString type = item.type;
    const QString value = currentValue.isEmpty() ? item.defaultValue : currentValue;

    if (type == QLatin1String("bool")) {
        QCheckBox *box = new QCheckBox(m_card);
        box->setObjectName(QStringLiteral("pluginSettingCheck"));
        box->setChecked(value == QLatin1String("true") || value == QLatin1String("1"));
        return box;
    }

    if (type == QLatin1String("select")) {
        QComboBox *combo = new QComboBox(m_card);
        combo->setObjectName(QStringLiteral("pluginSettingCombo"));
        combo->addItems(item.options);
        const int idx = combo->findText(value);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        else if (!value.isEmpty())
            combo->addItem(value);
        return combo;
    }

    if (type == QLatin1String("number")) {
        QDoubleSpinBox *spin = new QDoubleSpinBox(m_card);
        spin->setObjectName(QStringLiteral("pluginSettingSpin"));
        spin->setRange(-1e9, 1e9);
        spin->setDecimals(2);
        bool ok = false;
        const double d = value.toDouble(&ok);
        if (ok)
            spin->setValue(d);
        else
            spin->setValue(0);
        return spin;
    }

    // text / color / 其他 → QLineEdit
    QLineEdit *edit = new QLineEdit(m_card);
    edit->setObjectName(QStringLiteral("appDialogLineEdit"));
    edit->setText(value);
    return edit;
}

QString PluginSettingsDialog::fieldValue(const PluginSettingItem &item) const
{
    QWidget *w = m_fieldWidgets.value(item.key);
    if (!w)
        return QString();

    if (QCheckBox *box = qobject_cast<QCheckBox *>(w))
        return box->isChecked() ? QStringLiteral("true") : QStringLiteral("false");

    if (QComboBox *combo = qobject_cast<QComboBox *>(w))
        return combo->currentText();

    if (QDoubleSpinBox *spin = qobject_cast<QDoubleSpinBox *>(w))
        return QString::number(spin->value(), 'f', spin->decimals());

    if (QLineEdit *edit = qobject_cast<QLineEdit *>(w))
        return edit->text().trimmed();

    return QString();
}

void PluginSettingsDialog::onSave()
{
    PluginManager *pm = PluginManager::instance();
    for (const PluginSettingItem &item : m_plugin.settings)
        pm->setPluginSetting(m_plugin, item.key, fieldValue(item));
    accept();
}

void PluginSettingsDialog::onReset()
{
    // 恢复默认：清空存储值后按清单默认值重建控件
    PluginManager *pm = PluginManager::instance();
    for (const PluginSettingItem &item : m_plugin.settings) {
        pm->setPluginSetting(m_plugin, item.key, QString());
        if (QWidget *w = m_fieldWidgets.value(item.key)) {
            if (QCheckBox *box = qobject_cast<QCheckBox *>(w)) {
                box->setChecked(item.defaultValue == QLatin1String("true")
                                || item.defaultValue == QLatin1String("1"));
            } else if (QComboBox *combo = qobject_cast<QComboBox *>(w)) {
                const int idx = combo->findText(item.defaultValue);
                if (idx >= 0)
                    combo->setCurrentIndex(idx);
            } else if (QDoubleSpinBox *spin = qobject_cast<QDoubleSpinBox *>(w)) {
                bool ok = false;
                const double d = item.defaultValue.toDouble(&ok);
                spin->setValue(ok ? d : 0);
            } else if (QLineEdit *edit = qobject_cast<QLineEdit *>(w)) {
                edit->setText(item.defaultValue);
            }
        }
    }
}

void PluginSettingsDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const QString themeHover = tm->getThemeColorHover();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg      = isLight ? "rgba(255, 255, 255, 244)" : "rgba(46, 46, 50, 244)";
    const QString cardBorder  = isLight ? "rgba(210, 210, 210, 220)" : "rgba(92, 92, 98, 220)";
    const QString titleColor  = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor   = isLight ? "#333333" : "#e8e8e8";
    const QString fieldBg     = isLight ? "#f5f5f5" : "#3b3b40";
    const QString fieldBorder = isLight ? "#d4d4d4" : "#55555a";

    const QString style = QString(
        "QWidget#appDialogCard {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 16px;"
        "}"
        "QLabel#appDialogTitleLabel {"
        "    color: %3;"
        "    background-color: transparent;"
        "    font-size: 15px;"
        "    font-weight: bold;"
        "}"
        "QLabel#pluginSettingLabel {"
        "    color: %4;"
        "    font-size: 13px;"
        "    background-color: transparent;"
        "}"
        "QLabel#pluginSettingEmpty {"
        "    color: #999999;"
        "    font-size: 13px;"
        "    background-color: transparent;"
        "    padding: 30px 0;"
        "}"
        "QWidget#pluginSettingsForm { background: transparent; }"
        "QCheckBox#pluginSettingCheck { color: %4; font-size: 13px; }"
        "QComboBox#pluginSettingCombo {"
        "    background-color: %5;"
        "    border: 1px solid %6;"
        "    border-radius: 8px;"
        "    padding: 5px 10px;"
        "    color: %4;"
        "    font-size: 13px;"
        "}"
        "QComboBox#pluginSettingCombo::drop-down { border: none; width: 22px; }"
        "QDoubleSpinBox#pluginSettingSpin {"
        "    background-color: %5;"
        "    border: 1px solid %6;"
        "    border-radius: 8px;"
        "    padding: 5px 10px;"
        "    color: %4;"
        "    font-size: 13px;"
        "}"
        "QPushButton#appDialogBtn {"
        "    background-color: %5;"
        "    border: 1px solid %6;"
        "    border-radius: 12px;"
        "    color: %4;"
        "    padding: 7px 20px;"
        "    font-size: 13px;"
        "}"
        "QPushButton#appDialogBtn:hover { background-color: rgba(0,0,0,28); }"
        "QPushButton#appDialogBtnPrimary {"
        "    background-color: %7;"
        "    border: 1px solid %7;"
        "    border-radius: 12px;"
        "    color: #ffffff;"
        "    padding: 7px 24px;"
        "    font-size: 13px;"
        "}"
        "QPushButton#appDialogBtnPrimary:hover { background-color: %8; border-color: %8; }")
        .arg(cardBg, cardBorder, titleColor, textColor)
        .arg(fieldBg, fieldBorder, themeColor, themeHover);

    setStyleSheet(style);
}
