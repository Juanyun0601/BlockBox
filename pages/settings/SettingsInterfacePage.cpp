/**
 * @file   SettingsInterfacePage.cpp
 * @brief  界面设置页实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "pages/SettingsPage.h"

#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QPixmap>
#include <QProcess>
#include <QProgressBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QFileDialog>
#include "components/AppMessageBox.h"
#include <functional>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "../ColorPickerPage.h"
#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"
#include "../../layouts/FlowLayout.h"
#include "utils/BackgroundManager.h"
#include "utils/BingWallpaperManager.h"
#include "utils/HomeCarouselStore.h"
#include "utils/HomePresetCovers.h"
#include "utils/LanguageManager.h"
#include "utils/PanoramaThemeManager.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

namespace {
// 首页轮播缩略图尺寸（16:9）
constexpr int kHomeCarouselThumbW = 96;
constexpr int kHomeCarouselThumbH = 54;
} // namespace

void SettingsPage::initInterfaceSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_interfaceSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // ── 标题栏（标题 + 恢复默认值按钮）──
    createSettingsHeader(layout, tr("界面设置"));

    // ── 启动器主题 ──
    QVBoxLayout *themeCard = createSettingsCard(layout, tr("启动器主题"));

    QComboBox *themeCombo = new QComboBox();
    themeCombo->setObjectName("themeCombo");
    themeCombo->addItems({tr("浅色主题"), tr("深色主题"), tr("自定义主题")});
    disableWheelEffect(themeCombo);
    // 按当前实际生效的主题预选，避免已处于深色/自定义主题时下拉仍停在「浅色主题」
    auto themeToIndex = [](ThemeManager::ThemeType theme) {
        switch (theme) {
        case ThemeManager::DarkTheme:   return 1;
        case ThemeManager::CustomTheme: return 2;
        default:                        return 0;
        }
    };
    {
        const QSignalBlocker blocker(themeCombo);
        themeCombo->setCurrentIndex(themeToIndex(ThemeManager::instance()->currentTheme()));
    }
    connect(themeCombo, &QComboBox::currentIndexChanged, this, &SettingsPage::onThemeChanged);
    // 主题在别处被切换（新手引导、其他入口）时同步下拉选中项
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, themeCombo,
            [themeCombo, themeToIndex](ThemeManager::ThemeType theme) {
        const int index = themeToIndex(theme);
        if (themeCombo->currentIndex() == index)
            return;
        const QSignalBlocker blocker(themeCombo);
        themeCombo->setCurrentIndex(index);
    });

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

        // 预设颜色网格：流式布局，容器宽度不足时自动折行，
        // 避免单行固定排布把设置页最小宽度撑到 650px 导致横向滚动
        QWidget *presetWrap = new QWidget();
        presetWrap->setObjectName("settingRow");
        presetWrap->setAttribute(Qt::WA_StyledBackground, true);
        if (isLast) {
            presetWrap->setProperty("lastRow", true);
        }
        QHBoxLayout *presetLayout = new QHBoxLayout(presetWrap);
        presetLayout->setContentsMargins(24, 0, 24, 16);
        FlowLayout *flow = new FlowLayout(nullptr, 0, 6, 6);
        presetLayout->addLayout(flow, 1);

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
    m_backgroundModeCombo->addItems({tr("经典"), tr("纯色"), tr("图片"), tr("流光"), tr("旋转"), tr("必应壁纸")});
    disableWheelEffect(m_backgroundModeCombo);
    {
        const QSignalBlocker blocker(m_backgroundModeCombo);
        m_backgroundModeCombo->setCurrentIndex(static_cast<int>(BackgroundManager::instance()->currentMode()));
    }
    connect(m_backgroundModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onBackgroundModeChanged);

    QHBoxLayout *bgModeRow = appendSettingRow(backgroundCard,
        tr("背景模式"), tr("选择启动器背景的显示方式，旋转背景如《我的世界》启动界面般缓慢转动，必应壁纸使用必应官方每日壁纸。"), QString());
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

    // --- Rotating panorama section ---
    m_rotatingSection = new QWidget();
    QVBoxLayout *rotatingLayout = new QVBoxLayout(m_rotatingSection);
    rotatingLayout->setContentsMargins(0, 0, 0, 0);
    rotatingLayout->setSpacing(0);

    m_sceneCombo = new QComboBox();
    disableWheelEffect(m_sceneCombo);
    m_sceneCombo->addItem(tr("主世界"), QStringLiteral("overworld"));
    m_sceneCombo->addItem(tr("下界"), QStringLiteral("nether"));
    m_sceneCombo->addItem(tr("末地"), QStringLiteral("end"));
    m_sceneCombo->addItem(tr("樱花"), QStringLiteral("cherry"));
    m_sceneCombo->addItem(tr("沙漠"), QStringLiteral("desert"));
    m_sceneCombo->addItem(tr("山地"), QStringLiteral("mountains"));
    m_sceneCombo->addItem(tr("黑森林"), QStringLiteral("darkforest"));
    m_sceneCombo->addItem(tr("深暗之域"), QStringLiteral("deep_dark"));
    m_sceneCombo->addItem(tr("红树林沼泽"), QStringLiteral("wild"));
    m_sceneCombo->insertSeparator(m_sceneCombo->count());
    m_sceneCombo->addItem(tr("主菜单·经典"), QStringLiteral("menu_classic"));
    m_sceneCombo->addItem(tr("主菜单·现代"), QStringLiteral("menu_modern"));
    m_sceneCombo->insertSeparator(m_sceneCombo->count());
    m_sceneCombo->addItem(tr("自定义图片"), QString());
    {
        const QSignalBlocker blocker(m_sceneCombo);
        const int sceneIdx = m_sceneCombo->findData(BackgroundManager::instance()->panoramaScene());
        m_sceneCombo->setCurrentIndex(sceneIdx >= 0 ? sceneIdx : m_sceneCombo->count() - 1);
    }

    QHBoxLayout *sceneRow = appendSettingRow(rotatingLayout,
        tr("全景场景"), tr("选择旋转背景使用的全景主题。首次选择某个主题时会自动下载它的六张全景图，下载完成后立即应用；选「自定义图片」则使用下方路径的图片。"), QString());
    sceneRow->addWidget(m_sceneCombo);

    // 下载状态行：选中未下载的主题时显示进度，失败时可直接重试
    QFrame *sceneDownloadRow = new QFrame();
    sceneDownloadRow->setObjectName("settingRow");
    sceneDownloadRow->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout *sceneDownloadLayout = new QHBoxLayout(sceneDownloadRow);
    sceneDownloadLayout->setContentsMargins(24, 12, 24, 12);
    sceneDownloadLayout->setSpacing(12);

    m_sceneStatusLabel = new QLabel(sceneDownloadRow);
    m_sceneStatusLabel->setObjectName("settingDesc");
    m_sceneStatusLabel->setWordWrap(true);
    m_sceneStatusLabel->setTextFormat(Qt::PlainText);
    sceneDownloadLayout->addWidget(m_sceneStatusLabel, 1);

    m_sceneProgressBar = new QProgressBar(sceneDownloadRow);
    m_sceneProgressBar->setObjectName("sceneDownloadBar");
    m_sceneProgressBar->setTextVisible(false);
    m_sceneProgressBar->setFixedWidth(140);
    m_sceneProgressBar->setVisible(false);
    sceneDownloadLayout->addWidget(m_sceneProgressBar);

    m_sceneRetryBtn = new QPushButton(tr("重试"), sceneDownloadRow);
    m_sceneRetryBtn->setObjectName("browseBtn");
    m_sceneRetryBtn->setCursor(Qt::PointingHandCursor);
    m_sceneRetryBtn->setVisible(false);
    sceneDownloadLayout->addWidget(m_sceneRetryBtn);

    rotatingLayout->addWidget(sceneDownloadRow);

    // 自转速度：0%~400%，100% 为默认速度
    QSlider *speedSlider = new QSlider(Qt::Horizontal);
    speedSlider->setObjectName("bgSpeedSlider");
    speedSlider->setRange(0, 400);
    speedSlider->setSingleStep(10);
    speedSlider->setPageStep(50);
    speedSlider->setValue(BackgroundManager::instance()->rotatingSpeed());
    disableWheelEffect(speedSlider);

    QLabel *speedValueLabel = new QLabel();
    speedValueLabel->setFixedWidth(52);
    speedValueLabel->setAlignment(Qt::AlignCenter);
    speedValueLabel->setText(speedSlider->value() == 0
                                 ? tr("静止")
                                 : tr("%1%").arg(speedSlider->value()));

    QHBoxLayout *speedRow = appendSettingRow(rotatingLayout,
        tr("旋转速度"), tr("调整全景背景自转的快慢，调到最左侧则完全静止。"), QString(), true);
    speedRow->addWidget(speedSlider, 1);
    speedRow->addWidget(speedValueLabel);

    connect(speedSlider, &QSlider::valueChanged, [speedValueLabel](int val) {
        speedValueLabel->setText(val == 0 ? tr("静止") : tr("%1%").arg(val));
        BackgroundManager::instance()->setRotatingSpeed(val);
    });

    backgroundCard->addWidget(m_rotatingSection);

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

    backgroundCard->addWidget(m_imageSection);

    // --- Blur section（图片与旋转全景共用） ---
    m_blurSection = new QWidget();
    QVBoxLayout *blurLayout = new QVBoxLayout(m_blurSection);
    blurLayout->setContentsMargins(0, 0, 0, 0);
    blurLayout->setSpacing(0);

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

    QHBoxLayout *blurRow = appendSettingRow(blurLayout,
        tr("模糊程度"), tr("调整背景图片的模糊强度。"), QString(), true);
    blurRow->addWidget(blurSlider, 1);
    blurRow->addWidget(blurValueLabel);

    connect(blurSlider, &QSlider::valueChanged, [this, blurValueLabel](int val) {
        blurValueLabel->setText(val > 0 ? QString::number(val) : tr("无"));
        BackgroundManager::instance()->setBlurRadius(val);
    });

    backgroundCard->addWidget(m_blurSection);

    // --- Bing wallpaper section ---
    m_bingSection = new QWidget();
    QVBoxLayout *bingLayout = new QVBoxLayout(m_bingSection);
    bingLayout->setContentsMargins(0, 0, 0, 0);
    bingLayout->setSpacing(0);

    // 信息行：标题 + 动态状态（版权信息 / 下载进度 / 错误提示）+ 刷新按钮
    QFrame *bingInfoRow = new QFrame();
    bingInfoRow->setObjectName("settingRow");
    bingInfoRow->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout *bingInfoLayout = new QHBoxLayout(bingInfoRow);
    bingInfoLayout->setContentsMargins(24, 16, 24, 16);
    bingInfoLayout->setSpacing(20);

    QVBoxLayout *bingInfoCol = new QVBoxLayout();
    bingInfoCol->setContentsMargins(0, 0, 0, 0);
    bingInfoCol->setSpacing(4);
    QLabel *bingTitleLabel = new QLabel(tr("必应每日壁纸"), bingInfoRow);
    bingTitleLabel->setObjectName("settingTitle");
    QLabel *bingStatusLabel = new QLabel(bingInfoRow);
    bingStatusLabel->setObjectName("settingDesc");
    bingStatusLabel->setWordWrap(true);
    bingInfoCol->addWidget(bingTitleLabel);
    bingInfoCol->addWidget(bingStatusLabel);
    bingInfoLayout->addLayout(bingInfoCol, 1);

    QPushButton *bingRefreshBtn = new QPushButton(tr("刷新"), bingInfoRow);
    bingRefreshBtn->setObjectName("browseBtn");
    bingRefreshBtn->setCursor(Qt::PointingHandCursor);
    bingInfoLayout->addWidget(bingRefreshBtn);
    bingLayout->addWidget(bingInfoRow);

    connect(bingRefreshBtn, &QPushButton::clicked, this, [this, bingStatusLabel]() {
        bingStatusLabel->setText(tr("正在获取必应壁纸列表…"));
        BingWallpaperManager::instance()->refresh();
    });

    // 缩略图行：横向滚动的最近 8 天壁纸缩略图，点击即选用
    QScrollArea *bingStripScroll = new QScrollArea(m_bingSection);
    bingStripScroll->setWidgetResizable(true);
    bingStripScroll->setFixedHeight(76);
    bingStripScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    bingStripScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bingStripScroll->setFrameShape(QFrame::NoFrame);

    QWidget *bingStripContainer = new QWidget(bingStripScroll);
    QHBoxLayout *bingStripLayout = new QHBoxLayout(bingStripContainer);
    bingStripLayout->setContentsMargins(0, 2, 0, 2);
    bingStripLayout->setSpacing(8);
    bingStripLayout->addStretch();
    bingStripScroll->setWidget(bingStripContainer);

    QFrame *bingStripRow = new QFrame();
    bingStripRow->setObjectName("settingRow");
    bingStripRow->setAttribute(Qt::WA_StyledBackground, true);
    bingStripRow->setProperty("lastRow", true);
    QHBoxLayout *bingStripRowLayout = new QHBoxLayout(bingStripRow);
    bingStripRowLayout->setContentsMargins(24, 12, 24, 12);
    bingStripRowLayout->setSpacing(20);
    bingStripRowLayout->addWidget(bingStripScroll, 1);
    bingLayout->addWidget(bingStripRow);

    // 更新缩略图栏的选中高亮
    auto updateBingStripChecked = [this, bingStripContainer]() {
        int cur = BingWallpaperManager::instance()->currentIndex();
        const QString accent = ThemeManager::instance()->currentThemeColor();
        const QList<QPushButton*> buttons = bingStripContainer->findChildren<QPushButton*>();
        for (QPushButton *btn : buttons) {
            if (!btn->property("bingIndex").isValid())
                continue;
            bool selected = btn->property("bingIndex").toInt() == cur;
            btn->setProperty("bingSelected", selected);
            btn->setStyleSheet(selected
                ? QString("QPushButton { border: 2px solid %1; }").arg(accent)
                : QString());
        }
    };

    // 根据当前选中壁纸刷新信息行文本
    auto updateBingStatus = [bingStatusLabel]() {
        BingWallpaperManager *mgr = BingWallpaperManager::instance();
        if (mgr->wallpapers().isEmpty()) {
            bingStatusLabel->setText(tr("正在获取必应壁纸列表…"));
            return;
        }
        if (mgr->currentIndex() < 0 || mgr->currentIndex() >= mgr->wallpapers().size())
            return;
        const BingWallpaperManager::WallpaperInfo info = mgr->wallpapers().at(mgr->currentIndex());
        QDate date = QDate::fromString(info.date, "yyyyMMdd");
        QString dateText = date.isValid() ? date.toString("yyyy-MM-dd") : info.date;
        QString line = QString("%1 · %2").arg(dateText, info.title);
        if (info.copyright.isEmpty()) {
            bingStatusLabel->setText(line);
        } else {
            bingStatusLabel->setText(line + "\n" + info.copyright);
        }
    };

    // 重建缩略图栏（列表加载或刷新后调用）
    auto rebuildBingStrip = [this, bingStripContainer, bingStripLayout, bingStatusLabel,
                             updateBingStripChecked, updateBingStatus]() {
        // 清空旧按钮（保留末尾的 stretch）
        while (bingStripLayout->count() > 1) {
            QLayoutItem *item = bingStripLayout->takeAt(0);
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }

        BingWallpaperManager *mgr = BingWallpaperManager::instance();
        const QList<BingWallpaperManager::WallpaperInfo> list = mgr->wallpapers();
        for (int i = list.size() - 1; i >= 0; --i) {
            QPushButton *thumbBtn = new QPushButton(bingStripContainer);
            thumbBtn->setFixedSize(108, 60);
            thumbBtn->setIconSize(QSize(104, 56));
            thumbBtn->setCursor(Qt::PointingHandCursor);
            thumbBtn->setProperty("bingIndex", i);
            thumbBtn->setToolTip(list.at(i).title.isEmpty() ? list.at(i).copyright : list.at(i).title);

            const QString thumbPath = mgr->thumbPath(i);
            if (QFile::exists(thumbPath)) {
                thumbBtn->setIcon(QIcon(thumbPath));
            }

            connect(thumbBtn, &QPushButton::clicked, this,
                    [this, i, bingStatusLabel, updateBingStripChecked, updateBingStatus]() {
                BingWallpaperManager::instance()->selectWallpaper(i);
                updateBingStripChecked();
                // 未命中缓存时先给出下载中提示，完成后由 wallpaperImageReady 刷新文本
                if (BingWallpaperManager::instance()->currentImagePath().isEmpty()) {
                    bingStatusLabel->setText(tr("正在下载壁纸…"));
                } else {
                    updateBingStatus();
                }
            });

            bingStripLayout->insertWidget(0, thumbBtn);
        }

        updateBingStripChecked();
        updateBingStatus();
    };

    BingWallpaperManager *bingMgr = BingWallpaperManager::instance();
    connect(bingMgr, &BingWallpaperManager::wallpaperListUpdated, this,
            [rebuildBingStrip]() { rebuildBingStrip(); });
    connect(bingMgr, &BingWallpaperManager::wallpaperThumbReady, this,
            [this, bingStripContainer](int index, const QString &path) {
                const QList<QPushButton*> buttons = bingStripContainer->findChildren<QPushButton*>();
                for (QPushButton *btn : buttons) {
                    if (btn->property("bingIndex").toInt() == index) {
                        btn->setIcon(QIcon(path));
                    }
                }
            });
    connect(bingMgr, &BingWallpaperManager::wallpaperImageReady, this,
            [updateBingStatus](const QString &) { updateBingStatus(); });
    connect(bingMgr, &BingWallpaperManager::fetchFailed, this,
            [bingStatusLabel](const QString &reason) {
                bingStatusLabel->setText(tr("获取失败：%1").arg(reason));
            });

    // 进入设置页时若壁纸列表已就绪，直接按缓存内容重建缩略图栏
    if (!bingMgr->wallpapers().isEmpty()) {
        rebuildBingStrip();
    } else {
        bingStatusLabel->setText(tr("正在获取必应壁纸列表…"));
    }

    backgroundCard->addWidget(m_bingSection);

    // Sections visibility (initial + on mode / scene change)
    auto updateBackgroundSections = [this]() {
        const int idx = m_backgroundModeCombo->currentIndex();
        const bool rotating = idx == BackgroundManager::Rotating;
        const bool customScene = rotating && m_sceneCombo->currentData().toString().isEmpty();
        m_solidColorSection->setVisible(idx == BackgroundManager::SolidColor);
        m_rotatingSection->setVisible(rotating);
        m_imageSection->setVisible(idx == BackgroundManager::Image || customScene);
        m_blurSection->setVisible(idx == BackgroundManager::Image || rotating);
        m_bingSection->setVisible(idx == BackgroundManager::Bing);
        updatePanoramaDownloadStatus();
    };
    updateBackgroundSections();

    // 当前在用的主题素材缺失（新安装、清理过缓存或从旧版本升级）时自动补下载
    auto ensureCurrentScene = [this]() {
        if (m_backgroundModeCombo->currentIndex() != BackgroundManager::Rotating)
            return;
        QString currentScene = BackgroundManager::instance()->panoramaScene();
        if (currentScene.isEmpty()) {
            // 选「自定义图片」但没指定可用图片：和背景层一样回退到「主世界」兜底
            const QString imagePath = BackgroundManager::instance()->imagePath();
            if (!imagePath.isEmpty() && QFileInfo::exists(imagePath))
                return;
            currentScene = PanoramaThemeManager::defaultThemeId();
        }
        PanoramaThemeManager *themes = PanoramaThemeManager::instance();
        if (!themes->isThemeReady(currentScene) && !themes->isDownloading(currentScene))
            themes->ensureTheme(currentScene);
        updatePanoramaDownloadStatus();
    };
    ensureCurrentScene();

    // 全景主题下载状态实时同步到设置页
    PanoramaThemeManager *panoramaThemes = PanoramaThemeManager::instance();
    connect(panoramaThemes, &PanoramaThemeManager::themeProgress, this,
            [this](const QString &id, int completed, int total, qint64, int percent) {
        const QString currentId = m_sceneCombo->currentData().toString();
        if (currentId != id) {
            // 「自定义图片」没选图时会兜底下载默认主题，也让它显示进度
            if (currentId.isEmpty()
                && id == PanoramaThemeManager::defaultThemeId()) {
                m_sceneProgressBar->setVisible(true);
                m_sceneProgressBar->setRange(0, qMax(1, total) * 100);
                m_sceneProgressBar->setValue(completed * 100 + qBound(0, percent, 100));
                m_sceneStatusLabel->setText(
                    tr("未指定自定义图片，正在下载「主世界」全景图（%1/%2）…")
                        .arg(completed)
                        .arg(total));
            }
            return;
        }
        m_sceneDownloadError.clear();
        m_sceneProgressBar->setVisible(true);
        m_sceneProgressBar->setRange(0, qMax(1, total) * 100);
        m_sceneProgressBar->setValue(completed * 100 + qBound(0, percent, 100));
        m_sceneStatusLabel->setText(tr("正在下载「%1」的全景图（%2/%3）…")
                                        .arg(m_sceneCombo->currentText())
                                        .arg(completed)
                                        .arg(total));
    });
    connect(panoramaThemes, &PanoramaThemeManager::themeReady, this,
            [this](const QString &id) {
        if (m_sceneCombo->currentData().toString() != id) {
            if (m_sceneCombo->currentData().toString().isEmpty()
                && id == PanoramaThemeManager::defaultThemeId()) {
                updatePanoramaDownloadStatus();
            }
            return;
        }
        m_sceneDownloadError.clear();
        // 用户选了这个主题但还没应用：六张图下载完再切换，避免中途黑屏
        if (m_pendingPanoramaScene == id) {
            m_pendingPanoramaScene.clear();
            BackgroundManager::instance()->setPanoramaScene(id);
        }
        updatePanoramaDownloadStatus();
    });
    connect(panoramaThemes, &PanoramaThemeManager::themeFailed, this,
            [this](const QString &id, const QString &reason) {
        if (m_sceneCombo->currentData().toString() != id) {
            if (m_sceneCombo->currentData().toString().isEmpty()
                && id == PanoramaThemeManager::defaultThemeId()) {
                updatePanoramaDownloadStatus();
            }
            return;
        }
        m_sceneDownloadError = reason;
        updatePanoramaDownloadStatus();
    });

    connect(m_sceneRetryBtn, &QPushButton::clicked, this, [this]() {
        const QString id = m_sceneCombo->currentData().toString();
        if (id.isEmpty())
            return;
        m_pendingPanoramaScene = id;
        m_sceneDownloadError.clear();
        PanoramaThemeManager::instance()->ensureTheme(id, true);
        updatePanoramaDownloadStatus();
    });

    // Toggle visibility on mode change
    connect(m_backgroundModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, updateBackgroundSections, ensureCurrentScene](int) {
        updateBackgroundSections();
        ensureCurrentScene();
    });

    connect(m_sceneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, updateBackgroundSections](int) {
        updateBackgroundSections();

        const QString id = m_sceneCombo->currentData().toString();
        if (id.isEmpty()) {
            // 切回自定义图片时，之前为某个主题发起的下载没必要继续
            if (!m_pendingPanoramaScene.isEmpty()) {
                PanoramaThemeManager::instance()->cancelTheme(m_pendingPanoramaScene);
                m_pendingPanoramaScene.clear();
            }
            m_sceneDownloadError.clear();
            BackgroundManager::instance()->setPanoramaScene(QString());
            updatePanoramaDownloadStatus();
            return;
        }

        PanoramaThemeManager *themes = PanoramaThemeManager::instance();
        if (themes->isThemeReady(id)) {
            // 已经下载过：立即应用；之前没下完的别的主题就不用继续了
            if (!themes->activeTheme().isEmpty()) {
                themes->cancelTheme(themes->activeTheme());
            }
            m_pendingPanoramaScene.clear();
            m_sceneDownloadError.clear();
            BackgroundManager::instance()->setPanoramaScene(id);
        } else {
            // 尚未下载：先下这个主题的六张图，完成后再应用
            m_pendingPanoramaScene = id;
            m_sceneDownloadError.clear();
            themes->ensureTheme(id, true);
        }
        updatePanoramaDownloadStatus();
    });

    connect(m_browseImageBtn, &QPushButton::clicked, [this]() {
        QString filePath = QFileDialog::getOpenFileName(
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
    // 首页轮播图
    // ========================================================================
    QVBoxLayout *carouselCard = createSettingsCard(layout, tr("首页轮播图"));

    m_homeCarouselStatusLabel = new QLabel();
    m_homeCarouselStatusLabel->setObjectName("settingDesc");
    m_homeCarouselStatusLabel->setWordWrap(true);

    m_homeCarouselStrip = new QWidget();
    m_homeCarouselStripLayout = new FlowLayout(m_homeCarouselStrip, 0, 8, 8);

    auto *addCarouselBtn = new QPushButton(tr("添加图片…"));
    addCarouselBtn->setObjectName("browseBtn");
    addCarouselBtn->setCursor(Qt::PointingHandCursor);
    connect(addCarouselBtn, &QPushButton::clicked, this, [this]() {
        addHomeCarouselImages();
    });

    auto *resetCarouselBtn = new QPushButton(tr("恢复默认"));
    resetCarouselBtn->setObjectName("restoreDefaultsBtn");
    resetCarouselBtn->setCursor(Qt::PointingHandCursor);
    connect(resetCarouselBtn, &QPushButton::clicked, this, [this]() {
        clearHomeCarouselImages();
    });

    QHBoxLayout *carouselRow = appendSettingRow(carouselCard,
        tr("轮播图片"), tr("预设封面图不随安装包内置，需要先在首页下载；这里添加的图片会排在预设图前面。"),
        QString(), true);
    auto *carouselBtnRow = new QHBoxLayout();
    carouselBtnRow->setContentsMargins(0, 0, 0, 0);
    carouselBtnRow->setSpacing(8);
    carouselBtnRow->addWidget(addCarouselBtn);
    carouselBtnRow->addWidget(resetCarouselBtn);
    carouselRow->addLayout(carouselBtnRow);

    carouselCard->addWidget(m_homeCarouselStatusLabel);
    carouselCard->addWidget(m_homeCarouselStrip);

    refreshHomeCarouselRows();

    // ========================================================================
    // 语言设置
    // ========================================================================
    // 简体中文和英文随安装包内置；繁體中文 / Español / 日本語 / Русский 的翻译
    // 作为语言包放在官网，选中时才联网下载，下好后一直可用（断网也能切）。
    QVBoxLayout *languageCard = createSettingsCard(layout, tr("语言设置"));

    m_languageCombo = new QComboBox();
    disableWheelEffect(m_languageCombo);
    refreshLanguageComboLabels();

    LanguageManager::Language currentLang = LanguageManager::instance()->currentLanguage();
    int langIndex = static_cast<int>(currentLang);
    if (langIndex < 0 || langIndex >= m_languageCombo->count())
        langIndex = 0;
    {
        const QSignalBlocker blocker(m_languageCombo);
        m_languageCombo->setCurrentIndex(langIndex);
    }

    // 下载状态行：选中未下载的语言时显示进度，失败时可直接重试
    QFrame *languagePackRow = new QFrame();
    languagePackRow->setObjectName("settingRow");
    languagePackRow->setAttribute(Qt::WA_StyledBackground, true);
    languagePackRow->setProperty("lastRow", true);
    m_languagePackRow = languagePackRow;
    QHBoxLayout *languagePackLayout = new QHBoxLayout(languagePackRow);
    languagePackLayout->setContentsMargins(24, 12, 24, 12);
    languagePackLayout->setSpacing(12);

    m_languagePackStatusLabel = new QLabel(languagePackRow);
    m_languagePackStatusLabel->setObjectName("settingDesc");
    m_languagePackStatusLabel->setWordWrap(true);
    m_languagePackStatusLabel->setTextFormat(Qt::PlainText);
    languagePackLayout->addWidget(m_languagePackStatusLabel, 1);

    m_languagePackProgressBar = new QProgressBar(languagePackRow);
    m_languagePackProgressBar->setObjectName("cardProgressBar");
    m_languagePackProgressBar->setTextVisible(false);
    m_languagePackProgressBar->setFixedWidth(140);
    m_languagePackProgressBar->setVisible(false);
    languagePackLayout->addWidget(m_languagePackProgressBar);

    m_languagePackActionBtn = new QPushButton(tr("下载语言包"), languagePackRow);
    m_languagePackActionBtn->setObjectName("browseBtn");
    m_languagePackActionBtn->setCursor(Qt::PointingHandCursor);
    m_languagePackActionBtn->setVisible(false);
    languagePackLayout->addWidget(m_languagePackActionBtn);

    connect(m_languageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
        LanguageManager *langManager = LanguageManager::instance();
        const LanguageManager::Language lang = static_cast<LanguageManager::Language>(index);

        if (lang == langManager->currentLanguage())
            return;

        // 不随安装包内置的语言：先下好语言包，下完再问是否重启
        if (!LanguageManager::isInstalled(lang)) {
            startLanguagePackDownload(index);
            return;
        }

        // 界面文字在各页面创建时就已经翻译好，运行时只换翻译器并不能重译已建好的
        // 控件，会出现「一半新语言、一半旧语言的界面」。这里与新手引导保持一致：
        // 只写入偏好，然后重启进程，由启动时的 loadLanguageSync() 干净加载。
        langManager->setLanguagePreference(lang);
        const AppMessageBox::StandardButton restartReply = AppMessageBox::question(
            this, tr("切换语言"),
            tr("语言已保存，需要重启方块盒子后才会完全生效。是否立即重启？"),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (restartReply == AppMessageBox::Yes) {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList());
            QCoreApplication::quit();
        }
    });

    connect(m_languagePackActionBtn, &QPushButton::clicked, this, [this]() {
        const int index = m_languagePackTarget >= 0 ? m_languagePackTarget
                                                    : m_languageCombo->currentIndex();
        startLanguagePackDownload(index);
    });

    LanguageManager *languagePacks = LanguageManager::instance();
    connect(languagePacks, &LanguageManager::languagePackDownloadProgress, this,
            [this](LanguageManager::Language lang, qint64 received, qint64 total) {
        if (static_cast<int>(lang) != m_languagePackTarget)
            return;
        m_languagePackError.clear();
        m_languagePackProgressBar->setVisible(true);
        if (total > 0) {
            m_languagePackProgressBar->setRange(0, 100);
            m_languagePackProgressBar->setValue(
                qBound(0, static_cast<int>(received * 100 / total), 100));
        } else {
            m_languagePackProgressBar->setRange(0, 0);   // 大小未知：走滚动条
        }
        m_languagePackStatusLabel->setText(tr("正在下载「%1」语言包…")
                                               .arg(LanguageManager::nativeName(lang)));
        m_languagePackActionBtn->setVisible(false);
    });
    connect(languagePacks, &LanguageManager::languagePackDownloadFinished, this,
            [this](LanguageManager::Language lang) {
        if (static_cast<int>(lang) != m_languagePackTarget)
            return;
        m_languagePackError.clear();
        m_languagePackProgressBar->setVisible(false);
        m_languageCombo->setEnabled(true);
        refreshLanguageComboLabels();
        updateLanguagePackStatus();

        // 语言包已就位：写入偏好并询问是否立即重启生效
        LanguageManager *langManager = LanguageManager::instance();
        langManager->setLanguagePreference(lang);
        const AppMessageBox::StandardButton restartReply = AppMessageBox::question(
            this, tr("语言包已下载"),
            tr("「%1」语言包已下载完成，重启方块盒子后即可切换。是否立即重启？")
                .arg(LanguageManager::nativeName(lang)),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (restartReply == AppMessageBox::Yes) {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList());
            QCoreApplication::quit();
        }
    });
    connect(languagePacks, &LanguageManager::languagePackDownloadFailed, this,
            [this](LanguageManager::Language lang, const QString &error) {
        if (static_cast<int>(lang) != m_languagePackTarget)
            return;
        m_languagePackError = error;
        m_languagePackProgressBar->setVisible(false);
        m_languageCombo->setEnabled(true);
        refreshLanguageComboLabels();
        updateLanguagePackStatus();
    });

    QHBoxLayout *languageRow = appendSettingRow(languageCard,
        tr("语言"), tr("选择应用界面的显示语言。简体中文和 English 自带，其他语言的翻译体积较大，选中后会先下载语言包。"),
        QString());
    languageRow->addWidget(m_languageCombo);

    // 状态行排在下拉框下面；没有待下载的语言时整行隐藏
    languageCard->addWidget(languagePackRow);

    // 上次选的是需要下载的语言、但本机没有语言包（新装 / 清理过数据 / 从旧版本升级）
    if (LanguageManager::instance()->hasPendingLanguagePack())
        m_languagePackTarget = static_cast<int>(LanguageManager::instance()->pendingLanguagePack());
    updateLanguagePackStatus();

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

    // ── 占位伸缩，保持内容顶部对齐 ──
    layout->addStretch();
}

void SettingsPage::updatePanoramaDownloadStatus()
{
    if (!m_sceneCombo || !m_sceneStatusLabel || !m_sceneProgressBar || !m_sceneRetryBtn)
        return;

    const QString id = m_sceneCombo->currentData().toString();
    const QString name = m_sceneCombo->currentText();
    PanoramaThemeManager *themes = PanoramaThemeManager::instance();

    m_sceneProgressBar->setVisible(false);
    m_sceneRetryBtn->setVisible(false);

    if (id.isEmpty()) {
        const QString imagePath = BackgroundManager::instance()->imagePath();
        if (!imagePath.isEmpty() && QFileInfo::exists(imagePath)) {
            m_sceneStatusLabel->setText(tr("使用自定义图片，无需下载。"));
        } else if (PanoramaThemeManager::isThemeReady(PanoramaThemeManager::defaultThemeId())) {
            m_sceneStatusLabel->setText(tr("未指定自定义图片，正在使用「主世界」全景场景。"));
        } else {
            m_sceneStatusLabel->setText(tr("未指定自定义图片，将自动使用「主世界」全景场景。"));
        }
        return;
    }

    if (themes->isDownloading(id)) {
        const int total = PanoramaThemeManager::fileCount();
        m_sceneProgressBar->setVisible(true);
        m_sceneProgressBar->setRange(0, qMax(1, total) * 100);
        m_sceneProgressBar->setValue(themes->completedFiles() * 100);
        m_sceneStatusLabel->setText(tr("正在下载「%1」的全景图（%2/%3）…")
                                        .arg(name)
                                        .arg(themes->completedFiles())
                                        .arg(total));
        return;
    }

    if (PanoramaThemeManager::isThemeReady(id)) {
        const bool applied = BackgroundManager::instance()->panoramaScene() == id;
        m_sceneStatusLabel->setText(applied
            ? tr("「%1」已下载并在使用中。").arg(name)
            : tr("「%1」已下载，选中后会立即应用。").arg(name));
        return;
    }

    if (!m_sceneDownloadError.isEmpty()) {
        m_sceneStatusLabel->setText(tr("「%1」下载失败：%2").arg(name, m_sceneDownloadError));
        m_sceneRetryBtn->setVisible(true);
        return;
    }

    m_sceneStatusLabel->setText(tr("「%1」尚未下载，选择后会自动下载。").arg(name));
}

void SettingsPage::refreshHomeCarouselRows()
{
    if (!m_homeCarouselStripLayout || !m_homeCarouselStrip || !m_homeCarouselStatusLabel)
        return;

    // 清空旧缩略图（FlowLayout 是自定义布局，需自己 takeAt）
    while (QLayoutItem *item = m_homeCarouselStripLayout->takeAt(0))
    {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    const QStringList images = HomeCarouselStore::existingImages(HomeCarouselStore::savedImages());
    if (images.isEmpty())
    {
        // 自定义图清空后回落到已下载的预设封面；一张都没下过时首页会显示下载入口
        m_homeCarouselStatusLabel->setText(
            HomePresetCovers::isDownloaded()
                ? tr("当前使用下载好的预设封面图。")
                : tr("还没有封面图：回首页点「下载预设图」，或在这里添加自己的图片。"));
        m_homeCarouselStrip->hide();
        return;
    }

    m_homeCarouselStatusLabel->setText(
        tr("已添加 %1 张图片，按此顺序轮播。点击缩略图可移除。").arg(images.size()));
    m_homeCarouselStrip->show();

    const qreal dpr = devicePixelRatioF();
    for (const QString &path : images)
    {
        auto *thumb = new QToolButton(m_homeCarouselStrip);
        thumb->setObjectName("carouselThumb");
        thumb->setCursor(Qt::PointingHandCursor);
        thumb->setFixedSize(kHomeCarouselThumbW, kHomeCarouselThumbH);
        thumb->setIconSize(QSize(kHomeCarouselThumbW, kHomeCarouselThumbH));
        thumb->setToolTip(tr("点击移除：%1").arg(QFileInfo(path).fileName()));

        QPixmap pixmap(path);
        if (!pixmap.isNull())
        {
            pixmap = pixmap.scaled(int(kHomeCarouselThumbW * dpr), int(kHomeCarouselThumbH * dpr),
                                   Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            pixmap.setDevicePixelRatio(dpr);
            thumb->setIcon(QIcon(pixmap));
        }
        else
        {
            thumb->setText(tr("无法预览"));
        }

        const QString imagePath = path;
        connect(thumb, &QToolButton::clicked, this, [this, imagePath]() {
            const AppMessageBox::StandardButton reply = AppMessageBox::question(
                this, tr("移除轮播图片"),
                tr("要从首页轮播中移除这张图片吗？"),
                AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
            if (reply != AppMessageBox::Yes)
                return;

            QStringList images = HomeCarouselStore::savedImages();
            if (images.removeAll(imagePath) > 0)
            {
                HomeCarouselStore::saveImages(images);
                HomeCarouselStore::removeImageFile(imagePath);
            }
            refreshHomeCarouselRows();
            emit carouselImagesChanged();
        });

        m_homeCarouselStripLayout->addWidget(thumb);
    }
}

void SettingsPage::addHomeCarouselImages()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("选择轮播图片"), QString(), HomeCarouselStore::imageFileFilter());
    if (files.isEmpty())
        return;

    QStringList images = HomeCarouselStore::existingImages(HomeCarouselStore::savedImages());
    int failed = 0;
    for (const QString &file : files)
    {
        const QString imported = HomeCarouselStore::importImage(file);
        if (imported.isEmpty())
        {
            ++failed;
            continue;
        }
        if (!images.contains(imported))
            images.append(imported);
    }

    if (images != HomeCarouselStore::savedImages())
    {
        HomeCarouselStore::saveImages(images);
        refreshHomeCarouselRows();
        emit carouselImagesChanged();
    }

    if (failed > 0)
    {
        AppMessageBox::warning(this, tr("添加轮播图片"),
            tr("有 %1 张图片无法识别或读取失败，已跳过。").arg(failed));
    }
}

void SettingsPage::clearHomeCarouselImages()
{
    const QStringList images = HomeCarouselStore::savedImages();
    if (images.isEmpty())
        return;

    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("恢复默认轮播图"),
        tr("将移除已添加的 %1 张图片（只删除方块盒子内的副本，不影响原图），"
           "首页改回显示下载好的预设封面图。是否继续？").arg(images.size()),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    HomeCarouselStore::clearCustom();
    refreshHomeCarouselRows();
    emit carouselImagesChanged();
}

// 首页下载完预设封面 / 导入了自己的风景图后同步这页的轮播图卡片
void SettingsPage::refreshCarouselEditor()
{
    refreshHomeCarouselRows();
}

/* ============================================================
 * 语言包（不随安装包内置的语言）
 * ============================================================ */

// 下拉框项目：索引必须与 LanguageManager::Language 枚举一致；
// 本机还没有语言包的语言在名字后面标一句「需下载」。
void SettingsPage::refreshLanguageComboLabels()
{
    if (!m_languageCombo)
        return;

    const int count = static_cast<int>(LanguageManager::Russian) + 1;
    {
        const QSignalBlocker blocker(m_languageCombo);
        if (m_languageCombo->count() != count) {
            m_languageCombo->clear();
            for (int i = 0; i < count; ++i)
                m_languageCombo->addItem(QString());
        }
        for (int i = 0; i < count; ++i) {
            const auto lang = static_cast<LanguageManager::Language>(i);
            QString name;
            switch (lang) {
            case LanguageManager::Chinese:            name = tr("简体中文"); break;
            case LanguageManager::ChineseTraditional: name = tr("繁體中文"); break;
            case LanguageManager::English:            name = tr("English"); break;
            case LanguageManager::Spanish:            name = tr("Español"); break;
            case LanguageManager::Japanese:           name = tr("日本語"); break;
            case LanguageManager::Russian:            name = tr("Русский"); break;
            }
            if (!LanguageManager::isInstalled(lang))
                name += tr("（需下载）");
            m_languageCombo->setItemText(i, name);
        }
    }
}

// 下载状态行：显示进度 / 失败原因 / 重启提示，不需要时整行隐藏
void SettingsPage::updateLanguagePackStatus()
{
    if (!m_languagePackStatusLabel || !m_languagePackProgressBar || !m_languagePackActionBtn)
        return;

    LanguageManager *langManager = LanguageManager::instance();
    const int target = m_languagePackTarget;

    if (m_languagePackRow)
        m_languagePackRow->setVisible(target >= 0);

    if (target < 0) {
        m_languagePackStatusLabel->setVisible(false);
        m_languagePackProgressBar->setVisible(false);
        m_languagePackActionBtn->setVisible(false);
        return;
    }

    const auto lang = static_cast<LanguageManager::Language>(target);
    const QString name = LanguageManager::nativeName(lang);

    if (langManager->isDownloadingLanguagePack()
        && static_cast<int>(langManager->downloadingLanguage()) == target) {
        m_languagePackStatusLabel->setVisible(true);
        m_languagePackProgressBar->setVisible(true);
        m_languagePackActionBtn->setVisible(false);
        m_languagePackStatusLabel->setText(tr("正在下载「%1」语言包…").arg(name));
        return;
    }

    m_languagePackProgressBar->setVisible(false);
    m_languagePackStatusLabel->setVisible(true);

    // 语言包已经在，只是还没重启（上一步询问重启时选了「稍后」）
    if (LanguageManager::isInstalled(lang)) {
        m_languagePackStatusLabel->setText(
            tr("「%1」语言包已下载完成，重启方块盒子后即可切换。").arg(name));
        m_languagePackActionBtn->setVisible(false);
        return;
    }

    if (!m_languagePackError.isEmpty()) {
        m_languagePackStatusLabel->setText(
            tr("「%1」语言包下载失败：%2").arg(name, m_languagePackError));
        m_languagePackActionBtn->setText(tr("重试"));
    } else {
        m_languagePackStatusLabel->setText(
            tr("「%1」的界面翻译不随安装包附带，需要联网下载语言包后才能使用。").arg(name));
        m_languagePackActionBtn->setText(tr("下载语言包"));
    }
    m_languagePackActionBtn->setVisible(true);
}

// 选中需要下载的语言 / 点「下载语言包」「重试」都走这里
void SettingsPage::startLanguagePackDownload(int langIndex)
{
    if (!m_languageCombo || langIndex < 0 || langIndex >= m_languageCombo->count())
        return;

    const auto lang = static_cast<LanguageManager::Language>(langIndex);
    LanguageManager *langManager = LanguageManager::instance();
    m_languagePackTarget = langIndex;
    m_languagePackError.clear();

    // 语言包已经在（例如上次下载完没重启）：直接走切换流程
    if (LanguageManager::isInstalled(lang)) {
        langManager->setLanguagePreference(lang);
        const AppMessageBox::StandardButton restartReply = AppMessageBox::question(
            this, tr("切换语言"),
            tr("语言已保存，需要重启方块盒子后才会完全生效。是否立即重启？"),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (restartReply == AppMessageBox::Yes) {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList());
            QCoreApplication::quit();
        }
        updateLanguagePackStatus();
        return;
    }

    if (!langManager->isDownloadingLanguagePack()) {
        // 下载期间先禁掉下拉框，避免同时开两个语言的下载
        m_languageCombo->setEnabled(false);
        langManager->downloadLanguagePack(lang);
    }
    updateLanguagePackStatus();
}
