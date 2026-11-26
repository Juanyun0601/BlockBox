/**
 * @file   SettingsControlPage.cpp
 * @brief  移动端「操作设置」页实现（触屏方案 / 手柄 / 视角）
 * @author BlockBox Team
 * @date   2026-11-20
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPair>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <initializer_list>

#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"
#include "components/TouchLayoutEditorDialog.h"
#include "utils/MobileControlSettings.h"

void SettingsPage::initControlSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_controlSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // ── 标题栏（标题 + 恢复默认值按钮）──
    QFrame *headerCard = new QFrame(m_controlSettings);
    headerCard->setObjectName(QStringLiteral("settingsHeaderCard"));
    headerCard->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout *headerLayout = new QHBoxLayout(headerCard);
    headerLayout->setContentsMargins(24, 12, 24, 12);
    headerLayout->setSpacing(12);

    OutlinedLabel *titleLabel = new OutlinedLabel(tr("操作设置"), headerCard);
    titleLabel->setObjectName(QStringLiteral("sectionTitle"));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    QPushButton *restoreBtn = new QPushButton(tr("恢复默认值"), headerCard);
    restoreBtn->setObjectName(QStringLiteral("restoreDefaultsBtn"));
    headerLayout->addWidget(restoreBtn);
    layout->addWidget(headerCard);

    QLabel *hintLabel = new QLabel(
        tr("手机 / 平板上的操作方式、触屏布局与手柄参数，修改后立即保存。"), m_controlSettings);
    hintLabel->setObjectName(QStringLiteral("keyBindDesc"));
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    // ── 通用小工具 ──
    auto makeCombo = [](std::initializer_list<QPair<QString, int>> items) {
        auto *combo = new QComboBox();
        for (const auto &item : items)
            combo->addItem(item.first, item.second);
        return combo;
    };
    auto syncCombo = [](QComboBox *combo, int value) {
        const int index = combo->findData(value);
        if (index >= 0)
            combo->setCurrentIndex(index);
    };
    auto makeSlider = [this](int minValue, int maxValue, int value) {
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(minValue, maxValue);
        slider->setValue(value);
        disableWheelEffect(slider);
        return slider;
    };
    auto makeValueLabel = [this](int value, const QString &suffix) {
        auto *label = new QLabel(tr("%1%2").arg(value).arg(suffix), m_controlSettings);
        label->setObjectName(QStringLiteral("settingValueLabel"));
        label->setMinimumWidth(44);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return label;
    };

    // ── 操作方式 ──
    QVBoxLayout *modeCard = createSettingsCard(layout, tr("操作方式"));

    QComboBox *modeCombo = makeCombo({
        {tr("自动识别"), static_cast<int>(MobileControlSettings::InputMode::Auto)},
        {tr("触屏"),    static_cast<int>(MobileControlSettings::InputMode::Touch)},
        {tr("手柄"),    static_cast<int>(MobileControlSettings::InputMode::Gamepad)},
        {tr("键盘鼠标"), static_cast<int>(MobileControlSettings::InputMode::KeyboardMouse)},
    });
    QHBoxLayout *modeRow = appendSettingRow(modeCard,
        tr("默认操作方式"),
        tr("启动游戏后默认使用的操作方式。"),
        tr("选择「自动识别」时，检测到手柄会自动切到手柄操作；没有手柄则使用触屏。"));
    modeRow->addWidget(modeCombo);

    QComboBox *schemeCombo = makeCombo({
        {tr("摇杆 + 按钮"),   static_cast<int>(MobileControlSettings::TouchScheme::Joystick)},
        {tr("虚拟方向键"),    static_cast<int>(MobileControlSettings::TouchScheme::DPad)},
        {tr("点击屏幕移动"),  static_cast<int>(MobileControlSettings::TouchScheme::TapToMove)},
    });
    QHBoxLayout *schemeRow = appendSettingRow(modeCard,
        tr("触控方案"),
        tr("触屏时左侧移动控件的样式，右侧始终是拖动视角的区域。"),
        tr("摇杆 + 按钮最接近基岩版的操作手感；虚拟方向键适合精确走位；点击屏幕移动只用单指也能玩。"));
    schemeRow->addWidget(schemeCombo);

    QComboBox *handCombo = makeCombo({
        {tr("右手持机"), static_cast<int>(MobileControlSettings::Handedness::Right)},
        {tr("左手持机"), static_cast<int>(MobileControlSettings::Handedness::Left)},
    });
    QHBoxLayout *handRow = appendSettingRow(modeCard,
        tr("持机方式"),
        tr("左手持机时，摇杆与按钮的左右位置互换。"), QString(), true);
    handRow->addWidget(handCombo);

    // ── 触屏 ──
    QVBoxLayout *touchCard = createSettingsCard(layout, tr("触屏"));

    QComboBox *joystickCombo = makeCombo({
        {tr("固定摇杆"),   static_cast<int>(MobileControlSettings::JoystickStyle::Fixed)},
        {tr("跟随手指"),   static_cast<int>(MobileControlSettings::JoystickStyle::Floating)},
    });
    QHBoxLayout *joystickRow = appendSettingRow(touchCard,
        tr("摇杆样式"),
        tr("固定摇杆始终停在一个位置；跟随手指会在按下的地方生成摇杆。"), QString());
    joystickRow->addWidget(joystickCombo);

    QComboBox *buttonSizeCombo = makeCombo({
        {tr("小"),   static_cast<int>(MobileControlSettings::ButtonSize::Small)},
        {tr("标准"), static_cast<int>(MobileControlSettings::ButtonSize::Standard)},
        {tr("大"),   static_cast<int>(MobileControlSettings::ButtonSize::Large)},
    });
    QHBoxLayout *buttonSizeRow = appendSettingRow(touchCard,
        tr("按钮大小"),
        tr("跳跃、潜行、攻击、使用等屏幕按钮的尺寸，按钮越大小屏幕上越好按。"));
    buttonSizeRow->addWidget(buttonSizeCombo);

    QSlider *opacitySlider = makeSlider(30, 100, MobileControlSettings::buttonOpacity());
    QLabel *opacityValue = makeValueLabel(opacitySlider->value(), tr("%"));
    QHBoxLayout *opacityRow = appendSettingRow(touchCard,
        tr("按钮透明度"),
        tr("屏幕按钮的不透明度，调低后按钮更不挡视野。"));
    opacityRow->addWidget(opacitySlider, 1);
    opacityRow->addWidget(opacityValue);

    QSlider *lookSlider = makeSlider(20, 200, MobileControlSettings::lookSensitivity());
    QLabel *lookValue = makeValueLabel(lookSlider->value(), tr("%"));
    QHBoxLayout *lookRow = appendSettingRow(touchCard,
        tr("视角灵敏度"),
        tr("拖动屏幕转视角的速度，数值越大转得越快。"),
        tr("100% 为标准速度；晕 3D 或想更精细地瞄准可适当调低。"));
    lookRow->addWidget(lookSlider, 1);
    lookRow->addWidget(lookValue);

    CustomCheckBox *hapticCheck = new CustomCheckBox(QString(), m_controlSettings);
    hapticCheck->setChecked(MobileControlSettings::hapticFeedback());
    QHBoxLayout *hapticRow = appendSettingRow(touchCard,
        tr("触感反馈"),
        tr("按下屏幕按钮时让手机轻微震动一下。"));

    // ── 自定义键位：打开键位布局编辑器（拖位置 / 改文字 / 绑键值 / 组合键） ──
    QPushButton *editLayoutBtn = new QPushButton(tr("编辑键位布局"), m_controlSettings);
    editLayoutBtn->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *editLayoutRow = appendSettingRow(touchCard,
        tr("自定义键位"),
        tr("拖动改按钮位置，改按钮文字、大小、浓度，给按钮绑单个键或组合键，还能添加、删除自己的按钮。"),
        QString(), true);
    editLayoutRow->addWidget(editLayoutBtn);
    hapticRow->addWidget(hapticCheck);

    // ── 手柄 ──
    QVBoxLayout *gamepadCard = createSettingsCard(layout, tr("手柄"));

    CustomCheckBox *gamepadCheck = new CustomCheckBox(QString(), m_controlSettings);
    gamepadCheck->setChecked(MobileControlSettings::gamepadEnabled());
    QHBoxLayout *gamepadRow = appendSettingRow(gamepadCard,
        tr("启用手柄"),
        tr("连接外接手柄后可以直接用手柄操作，关闭后只响应触屏。"));
    gamepadRow->addWidget(gamepadCheck);

    QComboBox *layoutCombo = makeCombo({
        {tr("自动识别"), static_cast<int>(MobileControlSettings::GamepadLayout::Auto)},
        {tr("Xbox"),     static_cast<int>(MobileControlSettings::GamepadLayout::Xbox)},
        {tr("PlayStation"), static_cast<int>(MobileControlSettings::GamepadLayout::PlayStation)},
        {tr("Switch"),   static_cast<int>(MobileControlSettings::GamepadLayout::Switch)},
    });
    QHBoxLayout *layoutRow = appendSettingRow(gamepadCard,
        tr("按键布局"),
        tr("选择手柄上 A / B / X / Y 的排列方式，自动识别不准时手动指定。"));
    layoutRow->addWidget(layoutCombo);

    QSlider *deadzoneSlider = makeSlider(0, 30, MobileControlSettings::gamepadDeadzone());
    QLabel *deadzoneValue = makeValueLabel(deadzoneSlider->value(), tr("%"));
    QHBoxLayout *deadzoneRow = appendSettingRow(gamepadCard,
        tr("摇杆死区"),
        tr("摇杆轻轻晃动时不触发移动的范围，手柄漂移时适当调大。"));
    deadzoneRow->addWidget(deadzoneSlider, 1);
    deadzoneRow->addWidget(deadzoneValue);

    CustomCheckBox *rumbleCheck = new CustomCheckBox(QString(), m_controlSettings);
    rumbleCheck->setChecked(MobileControlSettings::gamepadRumble());
    QHBoxLayout *rumbleRow = appendSettingRow(gamepadCard,
        tr("震动反馈"),
        tr("游戏里受伤、挖掘等事件让手柄震动（需要手柄支持）。"), QString(), true);
    rumbleRow->addWidget(rumbleCheck);

    // ── 移动与视角 ──
    QVBoxLayout *moveCard = createSettingsCard(layout, tr("移动与视角"));

    QComboBox *sprintCombo = makeCombo({
        {tr("双击摇杆"),  static_cast<int>(MobileControlSettings::SprintMode::DoubleTap)},
        {tr("推到底自动"), static_cast<int>(MobileControlSettings::SprintMode::PushToEdge)},
        {tr("关闭"),      static_cast<int>(MobileControlSettings::SprintMode::Disabled)},
    });
    QHBoxLayout *sprintRow = appendSettingRow(moveCard,
        tr("冲刺方式"),
        tr("手机上没有 Shift 键，用这里选择怎么进入疾跑。"));
    sprintRow->addWidget(sprintCombo);

    CustomCheckBox *invertYCheck = new CustomCheckBox(QString(), m_controlSettings);
    invertYCheck->setChecked(MobileControlSettings::invertLookY());
    QHBoxLayout *invertYRow = appendSettingRow(moveCard,
        tr("反转上下视角"),
        tr("开启后手指向上滑是抬头，向下滑是低头。"));
    invertYRow->addWidget(invertYCheck);

    CustomCheckBox *autoJumpCheck = new CustomCheckBox(QString(), m_controlSettings);
    autoJumpCheck->setChecked(MobileControlSettings::autoJump());
    QHBoxLayout *autoJumpRow = appendSettingRow(moveCard,
        tr("自动跳跃"),
        tr("走到方块边缘时自动跳上去，手机上赶路更省事。"), QString(), true);
    autoJumpRow->addWidget(autoJumpCheck);

    // ── 信号：改动即保存 ──
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [modeCombo](int index) {
        MobileControlSettings::setInputMode(static_cast<MobileControlSettings::InputMode>(
            modeCombo->itemData(index).toInt()));
    });
    connect(schemeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [schemeCombo](int index) {
        MobileControlSettings::setTouchScheme(static_cast<MobileControlSettings::TouchScheme>(
            schemeCombo->itemData(index).toInt()));
    });
    connect(handCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [handCombo](int index) {
        MobileControlSettings::setHandedness(static_cast<MobileControlSettings::Handedness>(
            handCombo->itemData(index).toInt()));
    });
    connect(joystickCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [joystickCombo](int index) {
        MobileControlSettings::setJoystickStyle(static_cast<MobileControlSettings::JoystickStyle>(
            joystickCombo->itemData(index).toInt()));
    });
    connect(buttonSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [buttonSizeCombo](int index) {
        MobileControlSettings::setButtonSize(static_cast<MobileControlSettings::ButtonSize>(
            buttonSizeCombo->itemData(index).toInt()));
    });
    connect(opacitySlider, &QSlider::valueChanged, this, [opacityValue](int value) {
        opacityValue->setText(tr("%1%").arg(value));
        MobileControlSettings::setButtonOpacity(value);
    });
    connect(lookSlider, &QSlider::valueChanged, this, [lookValue](int value) {
        lookValue->setText(tr("%1%").arg(value));
        MobileControlSettings::setLookSensitivity(value);
    });
    connect(deadzoneSlider, &QSlider::valueChanged, this, [deadzoneValue](int value) {
        deadzoneValue->setText(tr("%1%").arg(value));
        MobileControlSettings::setGamepadDeadzone(value);
    });
    connect(hapticCheck, &CustomCheckBox::toggled, this,
            [](bool checked) { MobileControlSettings::setHapticFeedback(checked); });
    connect(gamepadCheck, &CustomCheckBox::toggled, this,
            [](bool checked) { MobileControlSettings::setGamepadEnabled(checked); });
    connect(layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [layoutCombo](int index) {
        MobileControlSettings::setGamepadLayout(static_cast<MobileControlSettings::GamepadLayout>(
            layoutCombo->itemData(index).toInt()));
    });
    connect(rumbleCheck, &CustomCheckBox::toggled, this,
            [](bool checked) { MobileControlSettings::setGamepadRumble(checked); });
    connect(sprintCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [sprintCombo](int index) {
        MobileControlSettings::setSprintMode(static_cast<MobileControlSettings::SprintMode>(
            sprintCombo->itemData(index).toInt()));
    });
    connect(invertYCheck, &CustomCheckBox::toggled, this,
            [](bool checked) { MobileControlSettings::setInvertLookY(checked); });
    connect(autoJumpCheck, &CustomCheckBox::toggled, this,
            [](bool checked) { MobileControlSettings::setAutoJump(checked); });

    // 自定义键位：弹出编辑器，保存后游戏内悬浮键位立即按新布局生效
    connect(editLayoutBtn, &QPushButton::clicked, this, [this]() {
        TouchLayoutEditorDialog dialog(this);
        dialog.exec();
    });

    // 恢复默认值后把控件同步回默认状态
    auto syncControls = [=]() {
        syncCombo(modeCombo, static_cast<int>(MobileControlSettings::inputMode()));
        syncCombo(schemeCombo, static_cast<int>(MobileControlSettings::touchScheme()));
        syncCombo(handCombo, static_cast<int>(MobileControlSettings::handedness()));
        syncCombo(joystickCombo, static_cast<int>(MobileControlSettings::joystickStyle()));
        syncCombo(buttonSizeCombo, static_cast<int>(MobileControlSettings::buttonSize()));
        syncCombo(layoutCombo, static_cast<int>(MobileControlSettings::gamepadLayout()));
        syncCombo(sprintCombo, static_cast<int>(MobileControlSettings::sprintMode()));
        opacitySlider->setValue(MobileControlSettings::buttonOpacity());
        lookSlider->setValue(MobileControlSettings::lookSensitivity());
        deadzoneSlider->setValue(MobileControlSettings::gamepadDeadzone());
        opacityValue->setText(tr("%1%").arg(opacitySlider->value()));
        lookValue->setText(tr("%1%").arg(lookSlider->value()));
        deadzoneValue->setText(tr("%1%").arg(deadzoneSlider->value()));
        hapticCheck->setChecked(MobileControlSettings::hapticFeedback());
        gamepadCheck->setChecked(MobileControlSettings::gamepadEnabled());
        rumbleCheck->setChecked(MobileControlSettings::gamepadRumble());
        invertYCheck->setChecked(MobileControlSettings::invertLookY());
        autoJumpCheck->setChecked(MobileControlSettings::autoJump());
    };
    syncControls();

    connect(restoreBtn, &QPushButton::clicked, this, [this, syncControls]() {
        MobileControlSettings::resetToDefaults();
        syncControls();
    });

    // ── 占位伸缩，保持内容顶部对齐 ──
    layout->addStretch();
}
