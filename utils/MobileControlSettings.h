/**
 * @file   MobileControlSettings.h
 * @brief  移动端「操作设置」数据层
 * @author BlockBox Team
 * @date   2026-11-20
 *
 * 手机 / 平板上的操作方式、触屏布局与手柄参数统一放在这里读写：
 * 设置页只负责界面，具体启动与游戏内控制都通过这套取值保持一致。
 * 所有取值都写入 QSettings（键前缀 mobile/control/），没有配置时用默认值。
 */
#ifndef MOBILECONTROLSETTINGS_H
#define MOBILECONTROLSETTINGS_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace MobileControlSettings {

/** 默认操作方式 */
enum class InputMode {
    Auto = 0,       ///< 自动识别（有手柄用手柄，没有用触屏）
    Touch,          ///< 触屏
    Gamepad,        ///< 手柄
    KeyboardMouse   ///< 键盘鼠标
};

/** 触屏时的移动控件方案 */
enum class TouchScheme {
    Joystick = 0,   ///< 摇杆 + 按钮
    DPad,           ///< 虚拟方向键
    TapToMove       ///< 点击屏幕移动
};

/** 持机方式：决定摇杆和按钮的左右位置 */
enum class Handedness {
    Right = 0,      ///< 右手持机（摇杆在左）
    Left            ///< 左手持机（摇杆在右）
};

/** 摇杆样式 */
enum class JoystickStyle {
    Fixed = 0,      ///< 固定摇杆
    Floating        ///< 跟随手指
};

/** 屏幕按钮大小 */
enum class ButtonSize {
    Small = 0,      ///< 小
    Standard,       ///< 标准
    Large           ///< 大
};

/** 手柄按键布局 */
enum class GamepadLayout {
    Auto = 0,       ///< 自动识别
    Xbox,
    PlayStation,
    Switch
};

/** 冲刺方式 */
enum class SprintMode {
    DoubleTap = 0,  ///< 双击摇杆 / 双击方向键
    PushToEdge,     ///< 推到底自动冲刺
    Disabled        ///< 关闭快捷冲刺
};

/**
 * 自定义触屏布局中的一个控件（摇杆或屏幕按钮）。
 * 坐标与尺寸都按屏幕比例保存，换手机 / 换分辨率后布局不会跑偏。
 */
struct TouchControl {
    QString id;         ///< 唯一标识（内置控件固定，自定义按钮为 custom_N）
    QString label;      ///< 按钮上显示的文字
    QString action;     ///< 对应操作：jump / sneak / attack / use / inventory / sprint / custom
    qreal x = 0.5;      ///< 圆心横坐标，相对屏幕宽度 0~1
    qreal y = 0.5;      ///< 圆心纵坐标，相对屏幕高度 0~1
    int size = 100;     ///< 尺寸百分比（100 = 标准大小，范围 60~160）
    int opacity = 85;   ///< 不透明度 30~100
    bool joystick = false; ///< 是否为左下角的移动摇杆
    /**
     * 自定义键值 / 组合键（如 ["Ctrl","Q"]）：按下按钮时同时发出这些键，
     * 抬起时一起松开；为空时按 action 走内置默认键值。
     * 在「操作设置 > 自定义键位」编辑器里改。
     */
    QStringList comboKeys;
};

// ── 触屏布局 ──
/** 默认布局（摇杆 + 跳跃 / 攻击 / 使用 / 潜行 / 背包 / 疾跑） */
QVector<TouchControl> defaultTouchLayout();
/** 读取用户自定义布局；没有保存过或数据损坏时回落到默认布局 */
QVector<TouchControl> touchLayout();
/** 保存用户自定义布局 */
void setTouchLayout(const QVector<TouchControl> &controls);
/** 把布局恢复为默认 */
void resetTouchLayout();
/** 内置按钮的默认动作名（jump → 跳跃等），自定义按钮返回空 */
QString defaultActionName(const QString &action);
/** 内置按钮的默认键值说明（jump → 空格等），自定义按钮返回空 */
QString defaultActionKeys(const QString &action);
/** 布局里下一个可用的自定义按钮 id（custom_1、custom_2…） */
QString nextCustomControlId(const QVector<TouchControl> &controls);

// ── 操作方式 ──
InputMode inputMode();
void setInputMode(InputMode mode);

TouchScheme touchScheme();
void setTouchScheme(TouchScheme scheme);

Handedness handedness();
void setHandedness(Handedness hand);

// ── 触屏 ──
JoystickStyle joystickStyle();
void setJoystickStyle(JoystickStyle style);

/** 屏幕按钮大小 */
ButtonSize buttonSize();
void setButtonSize(ButtonSize size);

/** 屏幕按钮不透明度，30 ~ 100 */
int buttonOpacity();
void setButtonOpacity(int opacity);

/** 视角灵敏度，20 ~ 200（%） */
int lookSensitivity();
void setLookSensitivity(int sensitivity);

bool hapticFeedback();
void setHapticFeedback(bool enabled);

// ── 手柄 ──
bool gamepadEnabled();
void setGamepadEnabled(bool enabled);

GamepadLayout gamepadLayout();
void setGamepadLayout(GamepadLayout layout);

/** 摇杆死区，0 ~ 30（%） */
int gamepadDeadzone();
void setGamepadDeadzone(int deadzone);

bool gamepadRumble();
void setGamepadRumble(bool enabled);

// ── 移动与视角 ──
SprintMode sprintMode();
void setSprintMode(SprintMode mode);

bool invertLookY();
void setInvertLookY(bool inverted);

bool autoJump();
void setAutoJump(bool enabled);

/** 把所有操作设置恢复为默认值 */
void resetToDefaults();

} // namespace MobileControlSettings

#endif // MOBILECONTROLSETTINGS_H
