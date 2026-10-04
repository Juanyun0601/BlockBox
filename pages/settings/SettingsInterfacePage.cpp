/**
 * @file   SettingsInterfacePage.cpp
 * @brief  界面设置页实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "pages/SettingsPage.h"

#include <QColor>
#include <QComboBox>
#include <QDebug>
#include <QSignalBlocker>
#include "components/AppFileDialog.h"
#include <functional>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "../ColorPickerPage.h"
#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"
#include "utils/BackgroundManager.h"
#include "utils/LanguageManager.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

void SettingsPage::initInterfaceSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_interfaceSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    OutlinedLabel *titleLabel = new OutlinedLabel(tr("界面设置"), m_interfaceSettings);
    titleLabel->setObjectName("sectionTitle");
    layout->addWidget(titleLabel);

    // ── 启动器主题 ──
    QVBoxLayout *themeCard = createSettingsCard(layout, tr("启动器主题"));

    QComboBox *themeCombo = new QComboBox();
    themeCombo->addItems({tr("浅色主题"), tr("深色主题"), tr("自定义主题")});
    disableWheelEffect(themeCombo);
    connect(themeCombo, &QComboBox::currentIndexChanged, this, &SettingsPage::onThemeChanged);

    QHBoxLayout *themeRow = appendSettingRow(themeCard,
        tr("启动器主题"), tr("切换整个应用的外观主题。"), QString(), true);
    themeRow->addWidget(themeCombo);

    // ========================================================================
    // 自定义颜色
    // ========================================================================
    QVBoxLayout *themeColorCard = createSettingsCard(layout, tr("自定义颜色"));

    auto updateBtnColor = [](QPushButton *btn, const QString &color) {
        btn->setStyleSheet(QString(
            "QPushButton { background-color: %1; border-radius: 8px; border: 2px solid #d8d8d8; }"
            "QPushButton:hover { border-color: %1; }"
        ).arg(color));
    };

    // 创建一个颜色设置项（信息行 + 颜色按钮 + 调色板按钮 + 预设网格）
    auto makeColorSection = [&](QPushButton *&outColorBtn,
                                 const QString &title, const QString &desc, const QString &helpText,
                                 const QString &initialColor,
                                 const std::function<void(const QString&)>& applyColor,
                                 bool isLast) {
        outColorBtn = new QPushButton();
        outColorBtn->setFixedSize(40, 40);
        outColorBtn->setObjectName("colorPickerBtn");
        outColorBtn->setCursor(Qt::PointingHandCursor);
        updateBtnColor(outColorBtn, initialColor);

        QPushButton *paletteBtn = new QPushButton(tr("调色板"));
        paletteBtn->setObjectName("paletteBtn");
        paletteBtn->setFixedWidth(72);
        paletteBtn->setCursor(Qt::PointingHandCursor);

        QHBoxLayout *row = appendSettingRow(themeColorCard, title, desc, helpText);
        row->addWidget(outColorBtn);
        row->addWidget(paletteBtn);

        // 预设颜色网格
        QHBoxLayout *flow = new QHBoxLayout();
        flow->setContentsMargins(0, 0, 0, 0);
        flow->setSpacing(6);

        QList<ThemeManager::PresetColor> presets = ThemeManager::instance()->presetColors();
        for (int i = 0; i < presets.size(); i++)
        {
            QPushButton *btn = new QPushButton();
            btn->setFixedSize(32, 32);
            btn->setToolTip(presets[i].name);
            updateBtnColor(btn, presets[i].color);

            connect(btn, &QPushButton::clicked, [=]() {
                updateBtnColor(outColorBtn, presets[i].color);
                applyColor(presets[i].color);
            });

            flow->addWidget(btn);
        }

        QWidget *presetWrap = new QWidget();
        presetWrap->setObjectName("settingRow");
        presetWrap->setAttribute(Qt::WA_StyledBackground, true);
        if (isLast) {
            presetWrap->setProperty("lastRow", true);
        }
        QHBoxLayout *presetLayout = new QHBoxLayout(presetWrap);
        presetLayout->setContentsMargins(24, 0, 24, 16);
        presetLayout->addLayout(flow);
        presetLayout->addStretch();
        themeColorCard->addWidget(presetWrap);

        return paletteBtn;
    };

    // 辅助：打开调色板，实时预览，取消时恢复
    auto openColorPicker = [&](QPushButton *colorBtn, const QString &currentColor,
                                const std::function<void(const QString&)>& applyColor) {
        QString savedColor = currentColor;

        ColorPickerPage picker(this, QColor(currentColor));

        // 实时预览：对话框内调色时即时反映到主界面
        connect(&picker, &ColorPickerPage::colorChanged, [&](const QColor &color) {
            QString cs = color.name();
            updateBtnColor(colorBtn, cs);
            applyColor(cs);
        });

        if (picker.exec() == QDialog::Accepted)
        {
            // 确认：保留当前颜色
        }
        else
        {
            // 取消：恢复原色
            updateBtnColor(colorBtn, savedColor);
            applyColor(savedColor);
        }
    };

    // ---- 主色调 ----
    QPushButton *themeColorBtn;
    QPushButton *themePaletteBtn = makeColorSection(themeColorBtn,
        tr("主色调"),
        tr("应用于按钮、高亮、进度条等界面元素"),
        tr("主色调决定应用的主视觉颜色，可在预设色与调色板中选择。"),
        ThemeManager::instance()->currentThemeColor(),
        [](const QString &c) { ThemeManager::instance()->setThemeColor(c); },
        false);

    connect(themePaletteBtn, &QPushButton::clicked, [=]() {
        openColorPicker(themeColorBtn, ThemeManager::instance()->currentThemeColor(),
                        [](const QString &c) { ThemeManager::instance()->setThemeColor(c); });
    });

    // ---- 文字颜色 ----
    QPushButton *textColorBtn;
    QPushButton *textPaletteBtn = makeColorSection(textColorBtn,
        tr("文字颜色"),
        tr("界面文字颜色，深色/自定义主题时生效"),
        tr("自定义文字颜色，深色或自定义主题下生效。"),
        ThemeManager::instance()->currentTextColor(),
        [](const QString &c) { ThemeManager::instance()->setTextColor(c); },
        false);

    connect(textPaletteBtn, &QPushButton::clicked, [=]() {
        openColorPicker(textColorBtn, ThemeManager::instance()->currentTextColor(),
                        [](const QString &c) { ThemeManager::instance()->setTextColor(c); });
    });

    // ---- 文字边框颜色 ----
    QPushButton *textBorderColorBtn;
    QPushButton *textBorderPaletteBtn = makeColorSection(textBorderColorBtn,
        tr("文字边框颜色"),
        tr("文字标签的边框颜色，如卡片名称、信息标签"),
        tr("用于文字标签的边框强调颜色。"),
        ThemeManager::instance()->currentTextBorderColor(),
        [](const QString &c) { ThemeManager::instance()->setTextBorderColor(c); },
        false);

    connect(textBorderPaletteBtn, &QPushButton::clicked, [=]() {
        openColorPicker(textBorderColorBtn, ThemeManager::instance()->currentTextBorderColor(),
                        [](const QString &c) { ThemeManager::instance()->setTextBorderColor(c); });
    });

    // ---- 边框颜色 ----
    QPushButton *borderColorBtn;
    QPushButton *borderPaletteBtn = makeColorSection(borderColorBtn,
        tr("边框颜色"),
        tr("卡片容器的边框颜色，如功能卡片、任务卡片等"),
        tr("用于卡片容器的边框颜色。"),
        ThemeManager::instance()->currentBorderColor(),
        [](const QString &c) { ThemeManager::instance()->setBorderColor(c); },
        false);

    connect(borderPaletteBtn, &QPushButton::clicked, [=]() {
        openColorPicker(borderColorBtn, ThemeManager::instance()->currentBorderColor(),
                        [](const QString &c) { ThemeManager::instance()->setBorderColor(c); });
    });

    // ---- 次要主题色 ----
    QPushButton *secondaryColorBtn;
    QPushButton *secondaryPaletteBtn = makeColorSection(secondaryColorBtn,
        tr("次要主题色"),
        tr("辅助强调效果，如悬浮状态、次要按钮、边框高亮"),
        tr("用于悬浮状态、次要按钮等辅助强调效果。"),
        ThemeManager::instance()->currentSecondaryThemeColor(),
        [](const QString &c) { ThemeManager::instance()->setSecondaryThemeColor(c); },
        false);

    connect(secondaryPaletteBtn, &QPushButton::clicked, [=]() {
        openColorPicker(secondaryColorBtn, ThemeManager::instance()->currentSecondaryThemeColor(),
                        [](const QString &c) { ThemeManager::instance()->setSecondaryThemeColor(c); });
    });

    // ---- 侧边栏背景颜色 ----
    QPushButton *sidebarBgColorBtn;
    QPushButton *sidebarBgPaletteBtn = makeColorSection(sidebarBgColorBtn,
        tr("侧边栏背景色"),
        tr("设置左侧导航栏的背景颜色"),
        tr("自定义左侧导航栏的背景颜色。"),
        !ThemeManager::instance()->currentSidebarBgColor().isEmpty() ? ThemeManager::instance()->currentSidebarBgColor() : "#ffffff",
        [](const QString &c) { ThemeManager::instance()->setSidebarBgColor(c); },
        true);

    connect(sidebarBgPaletteBtn, &QPushButton::clicked, [=]() {
        QString current = ThemeManager::instance()->currentSidebarBgColor();
        if (current.isEmpty()) current = "#ffffff";
        openColorPicker(sidebarBgColorBtn, current,
                        [](const QString &c) { ThemeManager::instance()->setSidebarBgColor(c); });
    });

    // ========================================================================
    // 背景设置
    // ========================================================================
    QVBoxLayout *backgroundCard = createSettingsCard(layout, tr("背景设置"));

    m_backgroundModeCombo = new QComboBox();
    m_backgroundModeCombo->addItems({tr("经典"), tr("纯色"), tr("图片"), tr("流光"), tr("旋转")});
    disableWheelEffect(m_backgroundModeCombo);
    {
        const QSignalBlocker blocker(m_backgroundModeCombo);
        m_backgroundModeCombo->setCurrentIndex(static_cast<int>(BackgroundManager::instance()->currentMode()));
    }
    connect(m_backgroundModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onBackgroundModeChanged);

    QHBoxLayout *bgModeRow = appendSettingRow(backgroundCard,
        tr("背景模式"), tr("选择启动器背景的显示方式，旋转背景如《我的世界》启动界面般缓慢转动。"), QString());
    bgModeRow->addWidget(m_backgroundModeCombo);

    // --- Solid color section ---
    m_solidColorSection = new QWidget();
    QVBoxLayout *solidLayout = new QVBoxLayout(m_solidColorSection);
    solidLayout->setContentsMargins(0, 0, 0, 0);
    solidLayout->setSpacing(0);

    m_solidColorBtn = new QPushButton();
    m_solidColorBtn->setFixedSize(40, 40);
    m_solidColorBtn->setObjectName("bgColorBtn");
    m_solidColorBtn->setCursor(Qt::PointingHandCursor);
    QString initialBgColor = BackgroundManager::instance()->solidColor();
    m_solidColorBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; border-radius: 8px; border: 2px solid #d8d8d8; }"
        "QPushButton:hover { border-color: %1; }"
    ).arg(initialBgColor));
    m_solidColorBtn->setProperty("backgroundColor", initialBgColor);

    QPushButton *bgPaletteBtn = new QPushButton(tr("调色板"));
    bgPaletteBtn->setObjectName("bgPaletteBtn");
    bgPaletteBtn->setFixedWidth(72);
    bgPaletteBtn->setCursor(Qt::PointingHandCursor);

    QHBoxLayout *solidColorRow = appendSettingRow(solidLayout,
        tr("纯色颜色"), tr("选择纯色背景使用的颜色。"), QString());
    solidColorRow->addWidget(m_solidColorBtn);
    solidColorRow->addWidget(bgPaletteBtn);

    connect(bgPaletteBtn, &QPushButton::clicked, [this]() {
        QColor current(m_solidColorBtn->property("backgroundColor").toString());
        ColorPickerPage picker(this, current);
        connect(&picker, &ColorPickerPage::colorChanged, [this](const QColor &c) {
            QString cs = c.name();
            m_solidColorBtn->setStyleSheet(QString(
                "QPushButton { background-color: %1; border-radius: 8px; border: 2px solid #d8d8d8; }"
                "QPushButton:hover { border-color: %1; }"
            ).arg(cs));
            m_solidColorBtn->setProperty("backgroundColor", cs);
            if (m_backgroundModeCombo->currentIndex() == BackgroundManager::SolidColor) {
                BackgroundManager::instance()->setSolidColor(cs);
            }
        });
        picker.exec();
    });

    backgroundCard->addWidget(m_solidColorSection);

    // --- Image section ---
    m_imageSection = new QWidget();
    QVBoxLayout *imageLayout = new QVBoxLayout(m_imageSection);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    imageLayout->setSpacing(0);

    m_imagePathEdit = new QLineEdit();
    m_imagePathEdit->setPlaceholderText(tr("选择背景图片..."));
    m_imagePathEdit->setText(BackgroundManager::instance()->imagePath());

    m_browseImageBtn = new QPushButton(tr("浏览..."));
    m_browseImageBtn->setObjectName("browseBtn");
    m_browseImageBtn->setCursor(Qt::PointingHandCursor);

    QHBoxLayout *imageRow = appendSettingRow(imageLayout,
        tr("背景图片路径"), tr("选择用于背景的图片文件。"), QString());
    imageRow->addWidget(m_imagePathEdit, 1);
    imageRow->addWidget(m_browseImageBtn);

    QSlider *blurSlider = new QSlider(Qt::Horizontal);
    blurSlider->setObjectName("bgBlurSlider");
    blurSlider->setRange(0, 64);
    blurSlider->setValue(BackgroundManager::instance()->blurRadius());
    disableWheelEffect(blurSlider);

    QLabel *blurValueLabel = new QLabel();
    blurValueLabel->setFixedWidth(40);
    blurValueLabel->setAlignment(Qt::AlignCenter);
    int initBlur = BackgroundManager::instance()->blurRadius();
    blurValueLabel->setText(initBlur > 0 ? QString::number(initBlur) : tr("无"));

    QHBoxLayout *blurRow = appendSettingRow(imageLayout,
        tr("模糊程度"), tr("调整背景图片的模糊强度。"), QString(), true);
    blurRow->addWidget(blurSlider, 1);
    blurRow->addWidget(blurValueLabel);

    connect(blurSlider, &QSlider::valueChanged, [this, blurValueLabel](int val) {
        blurValueLabel->setText(val > 0 ? QString::number(val) : tr("无"));
        BackgroundManager::instance()->setBlurRadius(val);
    });

    backgroundCard->addWidget(m_imageSection);

    // Initial visibility based on current mode
    int modeIdx = m_backgroundModeCombo->currentIndex();
    m_solidColorSection->setVisible(modeIdx == BackgroundManager::SolidColor);
    m_imageSection->setVisible(modeIdx == BackgroundManager::Image
                            || modeIdx == BackgroundManager::Rotating);

    // Toggle visibility on mode change
    connect(m_backgroundModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        m_solidColorSection->setVisible(idx == BackgroundManager::SolidColor);
        m_imageSection->setVisible(idx == BackgroundManager::Image
                                || idx == BackgroundManager::Rotating);
    });

    connect(m_browseImageBtn, &QPushButton::clicked, [this]() {
        QString filePath = AppFileDialog::getOpenFileName(
            this, tr("选择背景图片"),
            QString(),
            tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.gif);;所有文件 (*)"));
        if (!filePath.isEmpty()) {
            m_imagePathEdit->setText(filePath);
            if (m_backgroundModeCombo->currentIndex() == BackgroundManager::Image
                || m_backgroundModeCombo->currentIndex() == BackgroundManager::Rotating) {
                BackgroundManager::instance()->setImagePath(filePath);
            }
        }
    });

    connect(m_imagePathEdit, &QLineEdit::textChanged, [this]() {
        if (m_backgroundModeCombo->currentIndex() == BackgroundManager::Image
            || m_backgroundModeCombo->currentIndex() == BackgroundManager::Rotating) {
            BackgroundManager::instance()->setImagePath(m_imagePathEdit->text());
        }
    });

    // ========================================================================
    // 语言设置
    // ========================================================================
    QVBoxLayout *languageCard = createSettingsCard(layout, tr("语言设置"));

    QComboBox *languageCombo = new QComboBox();
    languageCombo->addItems({tr("简体中文"), tr("繁體中文"), tr("English"), tr("Español")});
    disableWheelEffect(languageCombo);

    LanguageManager::Language currentLang = LanguageManager::instance()->currentLanguage();
    int langIndex;
    switch (currentLang)
    {
    case LanguageManager::ChineseTraditional:
        langIndex = 1;
        break;
    case LanguageManager::English:
        langIndex = 2;
        break;
    case LanguageManager::Spanish:
        langIndex = 3;
        break;
    default:
        langIndex = 0;
        break;
    }
    {
        const QSignalBlocker blocker(languageCombo);
        languageCombo->setCurrentIndex(langIndex);
    }

    connect(languageCombo, &QComboBox::currentIndexChanged, [=](int index) {
        LanguageManager *langManager = LanguageManager::instance();
        LanguageManager::Language lang;
        switch (index)
        {
        case 1:
            lang = LanguageManager::ChineseTraditional;
            break;
        case 2:
            lang = LanguageManager::English;
            break;
        case 3:
            lang = LanguageManager::Spanish;
            break;
        default:
            lang = LanguageManager::Chinese;
            break;
        }
        langManager->setLanguage(lang);
    });

    QHBoxLayout *languageRow = appendSettingRow(languageCard,
        tr("语言"), tr("选择应用界面的显示语言。"), QString(), true);
    languageRow->addWidget(languageCombo);

    // ========================================================================
    // 隐藏导航栏按钮
    // ========================================================================
    QVBoxLayout *navButtonsCard = createSettingsCard(layout, tr("隐藏导航栏按钮"));

    SettingsManager *settings = SettingsManager::instance();

    CustomCheckBox *hideHomeCheck = new CustomCheckBox();
    hideHomeCheck->setChecked(!settings->isHomeButtonVisible());
    connect(hideHomeCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setHomeButtonVisible(!checked);
    });

    QHBoxLayout *hideHomeRow = appendSettingRow(navButtonsCard,
        tr("隐藏首页按钮"), tr("在左侧导航栏中隐藏首页入口。"), QString());
    hideHomeRow->addWidget(hideHomeCheck);

    CustomCheckBox *hideResourcesCheck = new CustomCheckBox();
    hideResourcesCheck->setChecked(!settings->isResourcesButtonVisible());
    connect(hideResourcesCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setResourcesButtonVisible(!checked);
    });

    QHBoxLayout *hideResourcesRow = appendSettingRow(navButtonsCard,
        tr("隐藏资源按钮"), tr("在左侧导航栏中隐藏资源入口。"), QString());
    hideResourcesRow->addWidget(hideResourcesCheck);

    CustomCheckBox *hideSettingsCheck = new CustomCheckBox();
    hideSettingsCheck->setChecked(!settings->isSettingsButtonVisible());
    connect(hideSettingsCheck, &CustomCheckBox::toggled, [=](bool checked) {
        settings->setSettingsButtonVisible(!checked);
    });

    QHBoxLayout *hideSettingsRow = appendSettingRow(navButtonsCard,
        tr("隐藏设置按钮"), tr("在左侧导航栏中隐藏设置入口。"), QString(), true);
    hideSettingsRow->addWidget(hideSettingsCheck);

    // ── 底部操作 ──
    QPushButton *restoreDefaultsBtn = new QPushButton(tr("恢复默认值"), m_interfaceSettings);
    restoreDefaultsBtn->setObjectName("restoreDefaultsBtn");
    connect(restoreDefaultsBtn, &QPushButton::clicked, this, &SettingsPage::onRestoreDefaults);

    layout->addStretch();
    layout->addWidget(restoreDefaultsBtn, 0, Qt::AlignRight);
}
