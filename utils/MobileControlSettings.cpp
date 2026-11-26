/**
 * @file   MobileControlSettings.cpp
 * @brief  移动端「操作设置」数据层实现
 * @author BlockBox Team
 * @date   2026-11-20
 */
#include "utils/MobileControlSettings.h"

#include "utils/SettingsManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

const QString kPrefix = QStringLiteral("mobile/control/");

QString key(const char *name)
{
    return kPrefix + QLatin1String(name);
}

/// 读取枚举值：超出范围或没有配置时回落到默认值，避免旧配置让下拉框选中无效项
template <typename T>
T readEnum(const char *name, T fallback, int maxValue)
{
    const int value = SettingsManager::instance()->getProperty(key(name), static_cast<int>(fallback)).toInt();
    if (value < 0 || value > maxValue)
        return fallback;
    return static_cast<T>(value);
}

int readInt(const char *name, int fallback, int minValue, int maxValue)
{
    const int value = SettingsManager::instance()->getProperty(key(name), fallback).toInt();
    return qBound(minValue, value, maxValue);
}

bool readBool(const char *name, bool fallback)
{
    return SettingsManager::instance()->getProperty(key(name), fallback).toBool();
}

/// 单个触屏控件 → JSON 对象（x/y 存比例值，换分辨率布局不跑偏）
QJsonObject controlToJson(const MobileControlSettings::TouchControl &control)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), control.id);
    obj.insert(QStringLiteral("label"), control.label);
    obj.insert(QStringLiteral("action"), control.action);
    obj.insert(QStringLiteral("x"), control.x);
    obj.insert(QStringLiteral("y"), control.y);
    obj.insert(QStringLiteral("size"), control.size);
    obj.insert(QStringLiteral("opacity"), control.opacity);
    obj.insert(QStringLiteral("joystick"), control.joystick);
    if (!control.comboKeys.isEmpty()) {
        QJsonArray keys;
        for (const QString &k : control.comboKeys)
            keys.append(k);
        obj.insert(QStringLiteral("keys"), keys);
    }
    return obj;
}

/// JSON 对象 → 单个触屏控件；缺字段回落内置默认，数值夹到合法区间
MobileControlSettings::TouchControl controlFromJson(const QJsonObject &obj)
{
    MobileControlSettings::TouchControl control;
    control.id = obj.value(QStringLiteral("id")).toString();
    control.label = obj.value(QStringLiteral("label")).toString();
    control.action = obj.value(QStringLiteral("action")).toString();
    control.x = qBound(0.0, obj.value(QStringLiteral("x")).toDouble(0.5), 1.0);
    control.y = qBound(0.0, obj.value(QStringLiteral("y")).toDouble(0.5), 1.0);
    control.size = qBound(60, obj.value(QStringLiteral("size")).toInt(100), 160);
    control.opacity = qBound(30, obj.value(QStringLiteral("opacity")).toInt(85), 100);
    control.joystick = obj.value(QStringLiteral("joystick")).toBool(false);
    const QJsonArray keys = obj.value(QStringLiteral("keys")).toArray();
    for (const QJsonValue &k : keys) {
        const QString keyName = k.toString().trimmed();
        if (!keyName.isEmpty())
            control.comboKeys.append(keyName);
    }
    return control;
}

} // namespace

namespace MobileControlSettings {

// ── 触屏布局 ──

QVector<TouchControl> defaultTouchLayout()
{
    // 右手持机（摇杆在左）为基准；左手持机由悬浮层按 handedness 镜像显示，不改存档
    QVector<TouchControl> controls;

    TouchControl joystick;
    joystick.id = QStringLiteral("joystick");
    joystick.label = QString();          // 摇杆画圈不写字
    joystick.action = QStringLiteral("move");
    joystick.x = 0.14;
    joystick.y = 0.74;
    joystick.joystick = true;
    controls.append(joystick);

    TouchControl jump;
    jump.id = QStringLiteral("jump");
    jump.label = QStringLiteral("跳");
    jump.action = QStringLiteral("jump");
    jump.x = 0.865;
    jump.y = 0.72;
    controls.append(jump);

    TouchControl attack;
    attack.id = QStringLiteral("attack");
    attack.label = QStringLiteral("攻击");
    attack.action = QStringLiteral("attack");
    attack.x = 0.73;
    attack.y = 0.58;
    controls.append(attack);

    TouchControl use;
    use.id = QStringLiteral("use");
    use.label = QStringLiteral("使用");
    use.action = QStringLiteral("use");
    use.x = 0.865;
    use.y = 0.52;
    controls.append(use);

    TouchControl sneak;
    sneak.id = QStringLiteral("sneak");
    sneak.label = QStringLiteral("潜行");
    sneak.action = QStringLiteral("sneak");
    sneak.x = 0.73;
    sneak.y = 0.87;
    sneak.size = 90;
    controls.append(sneak);

    TouchControl sprint;
    sprint.id = QStringLiteral("sprint");
    sprint.label = QStringLiteral("疾跑");
    sprint.action = QStringLiteral("sprint");
    sprint.x = 0.14;
    sprint.y = 0.54;
    sprint.size = 85;
    controls.append(sprint);

    TouchControl inventory;
    inventory.id = QStringLiteral("inventory");
    inventory.label = QStringLiteral("背包");
    inventory.action = QStringLiteral("inventory");
    inventory.x = 0.925;
    inventory.y = 0.28;
    inventory.size = 80;
    controls.append(inventory);

    return controls;
}

QVector<TouchControl> touchLayout()
{
    const QString json = SettingsManager::instance()->getProperty(key("layout")).toString();
    if (json.isEmpty())
        return defaultTouchLayout();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    const QJsonArray arr = doc.array();
    if (parseError.error != QJsonParseError::NoError || arr.isEmpty())
        return defaultTouchLayout();

    QVector<TouchControl> controls;
    controls.reserve(arr.size());
    for (const QJsonValue &value : arr) {
        if (!value.isObject())
            continue;
        TouchControl control = controlFromJson(value.toObject());
        if (control.id.isEmpty())
            continue;
        controls.append(control);
    }
    return controls.isEmpty() ? defaultTouchLayout() : controls;
}

void setTouchLayout(const QVector<TouchControl> &controls)
{
    QJsonArray arr;
    for (const TouchControl &control : controls)
        arr.append(controlToJson(control));
    SettingsManager::instance()->setProperty(key("layout"),
        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void resetTouchLayout()
{
    SettingsManager::instance()->setProperty(key("layout"), QString());
}

QString defaultActionName(const QString &action)
{
    if (action == QLatin1String("move"))    return QStringLiteral("移动");
    if (action == QLatin1String("jump"))    return QStringLiteral("跳跃");
    if (action == QLatin1String("attack"))  return QStringLiteral("攻击");
    if (action == QLatin1String("use"))     return QStringLiteral("使用");
    if (action == QLatin1String("sneak"))   return QStringLiteral("潜行");
    if (action == QLatin1String("sprint"))  return QStringLiteral("疾跑");
    if (action == QLatin1String("inventory")) return QStringLiteral("背包");
    return QString();
}

QString defaultActionKeys(const QString &action)
{
    if (action == QLatin1String("jump"))      return QStringLiteral("空格");
    if (action == QLatin1String("attack"))    return QStringLiteral("鼠标左键");
    if (action == QLatin1String("use"))       return QStringLiteral("鼠标右键");
    if (action == QLatin1String("sneak"))     return QStringLiteral("Shift");
    if (action == QLatin1String("sprint"))    return QStringLiteral("Ctrl");
    if (action == QLatin1String("inventory")) return QStringLiteral("E");
    return QString();
}

QString nextCustomControlId(const QVector<TouchControl> &controls)
{
    int maxIndex = 0;
    for (const TouchControl &control : controls) {
        // 自定义按钮 id 形如 custom_3，编号取现有最大值 +1
        if (control.id.startsWith(QLatin1String("custom_"))) {
            bool ok = false;
            const int index = control.id.mid(7).toInt(&ok);
            if (ok && index > maxIndex)
                maxIndex = index;
        }
    }
    return QStringLiteral("custom_%1").arg(maxIndex + 1);
}

InputMode inputMode()
{
    return readEnum("mode", InputMode::Auto,
                    static_cast<int>(InputMode::KeyboardMouse));
}

void setInputMode(InputMode mode)
{
    SettingsManager::instance()->setProperty(key("mode"), static_cast<int>(mode));
}

TouchScheme touchScheme()
{
    return readEnum("touchScheme", TouchScheme::Joystick,
                    static_cast<int>(TouchScheme::TapToMove));
}

void setTouchScheme(TouchScheme scheme)
{
    SettingsManager::instance()->setProperty(key("touchScheme"), static_cast<int>(scheme));
}

Handedness handedness()
{
    return readEnum("handedness", Handedness::Right, static_cast<int>(Handedness::Left));
}

void setHandedness(Handedness hand)
{
    SettingsManager::instance()->setProperty(key("handedness"), static_cast<int>(hand));
}

JoystickStyle joystickStyle()
{
    return readEnum("joystickStyle", JoystickStyle::Fixed,
                    static_cast<int>(JoystickStyle::Floating));
}

void setJoystickStyle(JoystickStyle style)
{
    SettingsManager::instance()->setProperty(key("joystickStyle"), static_cast<int>(style));
}

ButtonSize buttonSize()
{
    return readEnum("buttonSize", ButtonSize::Standard, static_cast<int>(ButtonSize::Large));
}

void setButtonSize(ButtonSize size)
{
    SettingsManager::instance()->setProperty(key("buttonSize"), static_cast<int>(size));
}

int buttonOpacity()
{
    return readInt("buttonOpacity", 85, 30, 100);
}

void setButtonOpacity(int opacity)
{
    SettingsManager::instance()->setProperty(key("buttonOpacity"), qBound(30, opacity, 100));
}

int lookSensitivity()
{
    return readInt("lookSensitivity", 100, 20, 200);
}

void setLookSensitivity(int sensitivity)
{
    SettingsManager::instance()->setProperty(key("lookSensitivity"), qBound(20, sensitivity, 200));
}

bool hapticFeedback()
{
    return readBool("hapticFeedback", true);
}

void setHapticFeedback(bool enabled)
{
    SettingsManager::instance()->setProperty(key("hapticFeedback"), enabled);
}

bool gamepadEnabled()
{
    return readBool("gamepadEnabled", true);
}

void setGamepadEnabled(bool enabled)
{
    SettingsManager::instance()->setProperty(key("gamepadEnabled"), enabled);
}

GamepadLayout gamepadLayout()
{
    return readEnum("gamepadLayout", GamepadLayout::Auto,
                    static_cast<int>(GamepadLayout::Switch));
}

void setGamepadLayout(GamepadLayout layout)
{
    SettingsManager::instance()->setProperty(key("gamepadLayout"), static_cast<int>(layout));
}

int gamepadDeadzone()
{
    return readInt("gamepadDeadzone", 10, 0, 30);
}

void setGamepadDeadzone(int deadzone)
{
    SettingsManager::instance()->setProperty(key("gamepadDeadzone"), qBound(0, deadzone, 30));
}

bool gamepadRumble()
{
    return readBool("gamepadRumble", true);
}

void setGamepadRumble(bool enabled)
{
    SettingsManager::instance()->setProperty(key("gamepadRumble"), enabled);
}

SprintMode sprintMode()
{
    return readEnum("sprintMode", SprintMode::DoubleTap, static_cast<int>(SprintMode::Disabled));
}

void setSprintMode(SprintMode mode)
{
    SettingsManager::instance()->setProperty(key("sprintMode"), static_cast<int>(mode));
}

bool invertLookY()
{
    return readBool("invertLookY", false);
}

void setInvertLookY(bool inverted)
{
    SettingsManager::instance()->setProperty(key("invertLookY"), inverted);
}

bool autoJump()
{
    return readBool("autoJump", false);
}

void setAutoJump(bool enabled)
{
    SettingsManager::instance()->setProperty(key("autoJump"), enabled);
}

void resetToDefaults()
{
    setInputMode(InputMode::Auto);
    setTouchScheme(TouchScheme::Joystick);
    setHandedness(Handedness::Right);
    setJoystickStyle(JoystickStyle::Fixed);
    setButtonSize(ButtonSize::Standard);
    setButtonOpacity(85);
    setLookSensitivity(100);
    setHapticFeedback(true);
    setGamepadEnabled(true);
    setGamepadLayout(GamepadLayout::Auto);
    setGamepadDeadzone(10);
    setGamepadRumble(true);
    setSprintMode(SprintMode::DoubleTap);
    setInvertLookY(false);
    setAutoJump(false);
    resetTouchLayout();
}

} // namespace MobileControlSettings
