/**
 * @file   TouchControlOverlay.cpp
 * @brief  游戏内触屏键位悬浮层实现
 * @author BlockBox Team
 * @date   2026-11-21
 */
#include "TouchControlOverlay.h"

#include <QEvent>
#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QResizeEvent>
#include <QTouchEvent>

using namespace MobileControlSettings;

namespace {

/// 触点 id：鼠标事件视为一个独立触点（桌面探测 / 无多点触摸的设备）
constexpr int kMousePointerId = -1;
/// 摇杆有效半径按布局半径外扩一点，指尖粗一点也能按到
constexpr qreal kHitSlop = 1.18;

/// 两点欧氏距离（QPointF 没有现成的 length 方法）
inline qreal dist(const QPointF &a, const QPointF &b)
{
    const QPointF d = a - b;
    return qSqrt(d.x() * d.x() + d.y() * d.y());
}

} // namespace

TouchControlOverlay::TouchControlOverlay(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("touchControlOverlay"));
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::NoFocus);
    // 跟着主窗口拉大缩小，始终铺满游戏画面
    if (parent)
        parent->installEventFilter(this);
}

bool TouchControlOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        rebuildPlacement();
        update();
    }
    return QWidget::eventFilter(watched, event);
}

void TouchControlOverlay::refreshFromSettings()
{
    m_layout = touchLayout();
    m_layoutLoaded = true;
    rebuildPlacement();
    update();
}

qreal TouchControlOverlay::buttonScaleFactor() const
{
    switch (buttonSize()) {
    case ButtonSize::Small:
        return 0.85;
    case ButtonSize::Large:
        return 1.2;
    case ButtonSize::Standard:
    default:
        return 1.0;
    }
}

void TouchControlOverlay::rebuildPlacement()
{
    m_placed.clear();
    m_joystickCenter = QPointF();
    m_knobOffset = QPointF();
    m_pressedButtons.clear();
    m_dpadZone = -1;

    const qreal base = qBound<qreal>(36, qMin(width(), height()) / 13.0, 72);
    const qreal scale = buttonScaleFactor();
    const bool mirror = handedness() == Handedness::Left;

    for (const TouchControl &control : m_layout) {
        PlacedControl placed;
        placed.control = control;
        // 左手持机整体镜像横坐标，保存的布局不动
        const qreal x = mirror ? 1.0 - control.x : control.x;
        placed.center = QPointF(x * width(), control.y * height());
        // 摇杆略大一号，其余按钮按布局里保存的尺寸百分比缩放
        const qreal controlScale = scale * control.size / 100.0;
        placed.radius = base * (control.joystick ? 1.15 : 1.0) * controlScale;
        m_placed.append(placed);

        if (control.joystick)
            m_joystickCenter = placed.center;
    }
}

int TouchControlOverlay::hitControlIndex(const QPointF &pos) const
{
    for (int i = 0; i < m_placed.size(); ++i) {
        const PlacedControl &placed = m_placed.at(i);
        if (dist(pos, placed.center) <= placed.radius * kHitSlop)
            return i;
    }
    return -1;
}

bool TouchControlOverlay::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        auto *touch = static_cast<QTouchEvent *>(event);
        // 多点触控：每个触点独立分角色（一只手走位、一只手拖视角都行）
        const auto &points = touch->points();
        for (const QEventPoint &point : points) {
            const QPointF pos = point.position();
            switch (point.state()) {
            case QEventPoint::Pressed:
                handlePointerPressed(point.id(), pos);
                break;
            case QEventPoint::Updated:
                handlePointerMoved(point.id(), pos);
                break;
            case QEventPoint::Released:
            default:
                handlePointerReleased(point.id(), pos);
                break;
            }
        }
        event->accept();
        return true;
    }
    case QEvent::MouseButtonPress:
        handlePointerPressed(kMousePointerId,
                             static_cast<QMouseEvent *>(event)->position());
        event->accept();
        return true;
    case QEvent::MouseMove:
        handlePointerMoved(kMousePointerId,
                           static_cast<QMouseEvent *>(event)->position());
        event->accept();
        return true;
    case QEvent::MouseButtonRelease:
        handlePointerReleased(kMousePointerId,
                              static_cast<QMouseEvent *>(event)->position());
        event->accept();
        return true;
    default:
        return QWidget::event(event);
    }
}

void TouchControlOverlay::handlePointerPressed(int id, const QPointF &pos)
{
    if (m_pointerRoles.contains(id))
        return; // 已跟踪的触点不重复分配（防误发的重复按下事件）

    const int hitIndex = hitControlIndex(pos);
    if (hitIndex >= 0) {
        const TouchControl &control = m_placed.at(hitIndex).control;
        if (control.joystick) {
            // 浮动摇杆：中心跟着第一下按住的位置走
            if (touchScheme() != TouchScheme::TapToMove
                && joystickStyle() == JoystickStyle::Floating)
                m_joystickCenter = pos;
            m_pointerRoles.insert(id, qMakePair(Role::Joystick, hitIndex));
            updateJoystickVector(pos);
        } else {
            m_pointerRoles.insert(id, qMakePair(Role::Button, hitIndex));
            if (!m_pressedButtons.contains(hitIndex))
                m_pressedButtons.append(hitIndex);
            if (!control.comboKeys.isEmpty())
                emit keysPressed(control.comboKeys); // 自定义键值 / 组合键
            else
                emit actionPressed(control.action);
        }
        update();
        return;
    }

    // 控件之外的区域：点击移动方案下整块屏幕都是移动区，其余方案拖视角
    if (touchScheme() == TouchScheme::TapToMove) {
        m_joystickCenter = QPointF(width() / 2.0, height() / 2.0);
        m_knobOffset = QPointF();
        m_pointerRoles.insert(id, qMakePair(Role::Joystick, -1));
        updateJoystickVector(pos);
        update();
        return;
    }

    m_pointerRoles.insert(id, qMakePair(Role::Look, 0));
    m_lastLookPos = pos;
}

void TouchControlOverlay::handlePointerMoved(int id, const QPointF &pos)
{
    const auto it = m_pointerRoles.find(id);
    if (it == m_pointerRoles.end())
        return;

    switch (it->first) {
    case Role::Joystick: {
        if (touchScheme() != TouchScheme::TapToMove && it->second >= 0
            && joystickStyle() == JoystickStyle::Floating) {
            // 浮动摇杆：手指滑出摇杆圈后中心跟着挪，摇杆始终在指尖
            const PlacedControl &placed = m_placed.at(it->second);
            if (dist(pos, m_joystickCenter) > placed.radius)
                m_joystickCenter = pos;
        }
        updateJoystickVector(pos);
        update();
        break;
    }
    case Role::Look: {
        const qreal sensitivity = lookSensitivity() / 100.0;
        const QPointF delta = (pos - m_lastLookPos) * sensitivity;
        if (!delta.isNull())
            emit lookDelta(int(delta.x()), int(delta.y()));
        m_lastLookPos = pos;
        break;
    }
    default:
        break;
    }
}

void TouchControlOverlay::handlePointerReleased(int id, const QPointF &pos)
{
    Q_UNUSED(pos);
    const auto it = m_pointerRoles.find(id);
    if (it == m_pointerRoles.end())
        return;

    switch (it->first) {
    case Role::Joystick:
        m_dpadZone = -1;
        m_knobOffset = QPointF();
        if (touchScheme() != TouchScheme::TapToMove && it->second >= 0)
            m_joystickCenter = m_placed.at(it->second).center; // 摇杆回位
        emitMoveVector(0.0, 0.0);
        break;
    case Role::Button: {
        const int index = it->second;
        m_pressedButtons.removeAll(index);
        if (index >= 0 && index < m_placed.size())
            if (!m_placed.at(index).control.comboKeys.isEmpty())
                emit keysReleased(m_placed.at(index).control.comboKeys);
            else
                emit actionReleased(m_placed.at(index).control.action);
        break;
    }
    default:
        break;
    }

    m_pointerRoles.remove(id);
    update();
}

void TouchControlOverlay::updateJoystickVector(const QPointF &pos)
{
    if (touchScheme() == TouchScheme::DPad) {
        // 方向键方案：按手指相对摇杆中心的角度分四个方向，输出单位向量
        int zone = -1;
        if (!pos.isNull() && !m_joystickCenter.isNull()) {
            const QPointF offset = pos - m_joystickCenter;
            if (offset.manhattanLength() > 6) { // 过小的位移不算，避免原地抖动
                if (qAbs(offset.x()) >= qAbs(offset.y()))
                    zone = offset.x() > 0 ? 3 : 2;
                else
                    zone = offset.y() > 0 ? 1 : 0;
            }
        }
        if (zone != m_dpadZone) {
            m_dpadZone = zone;
            switch (zone) {
            case 0:  emitMoveVector(0.0, -1.0); break;
            case 1:  emitMoveVector(0.0, 1.0);  break;
            case 2:  emitMoveVector(-1.0, 0.0); break;
            case 3:  emitMoveVector(1.0, 0.0);  break;
            default: emitMoveVector(0.0, 0.0);  break;
            }
        }
        return;
    }

    // 摇杆 / 点击移动：输出相对摇杆中心的归一化向量
    qreal x = 0.0, y = 0.0;
    if (!pos.isNull() && !m_joystickCenter.isNull()) {
        qreal range = 60.0;
        for (const PlacedControl &placed : m_placed) {
            if (placed.control.joystick) {
                range = placed.radius;
                break;
            }
        }
        const QPointF offset = pos - m_joystickCenter;
        const qreal length = dist(pos, m_joystickCenter);
        if (length > 1e-6) {
            x = offset.x() / range;
            y = offset.y() / range;
            // 向量整体限幅到单位长度；推到底冲刺交给游戏侧判定
            const qreal norm = qSqrt(x * x + y * y);
            if (norm > 1.0) {
                x /= norm;
                y /= norm;
            }
            // 摇杆钮画在限幅后的位置上，方向和力度一目了然
            const qreal knobRange = range * 0.55;
            const QPointF visual = offset * (qMin(length, knobRange) / length);
            m_knobOffset = (touchScheme() == TouchScheme::TapToMove) ? QPointF() : visual;
        }
    } else {
        m_knobOffset = QPointF();
    }
    emitMoveVector(x, y);
}

void TouchControlOverlay::emitMoveVector(qreal x, qreal y)
{
    // 变化太小不重复发，游戏桥和日志都清爽一点
    if (qAbs(x - m_moveX) < 0.02 && qAbs(y - m_moveY) < 0.02)
        return;
    m_moveX = x;
    m_moveY = y;
    emit moveVectorChanged(x, y);
}

QString TouchControlOverlay::dpadZoneLabel(int zone) const
{
    switch (zone) {
    case 0: return tr("上");
    case 1: return tr("下");
    case 2: return tr("左");
    case 3: return tr("右");
    default: return QString();
    }
}

void TouchControlOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (!m_layoutLoaded || m_placed.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 不透明度设置直接决定控件浓度（30~100），按下时再加亮一档
    const int baseAlpha = qBound(30, buttonOpacity(), 100);
    const int fillAlpha = int(baseAlpha * 0.55);
    const int strokeAlpha = int(baseAlpha * 0.85);

    const QPen strokePen(QColor(235, 238, 245, strokeAlpha), 2);
    const QColor fillColor(20, 22, 26, fillAlpha);
    const QColor pressedFill(52, 120, 86, qMin(255, fillAlpha + 60));

    const TouchScheme scheme = touchScheme();

    for (int i = 0; i < m_placed.size(); ++i) {
        const PlacedControl &placed = m_placed.at(i);
        const QRectF circle(placed.center.x() - placed.radius,
                            placed.center.y() - placed.radius,
                            placed.radius * 2.0, placed.radius * 2.0);
        const bool pressed = m_pressedButtons.contains(i);

        if (placed.control.joystick) {
            if (scheme == TouchScheme::TapToMove) {
                // 点击移动方案不画摇杆：只在角落留一个淡淡的提示圈
                painter.setPen(QPen(QColor(235, 238, 245, 45), 2, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(circle);
                continue;
            }
            if (scheme == TouchScheme::DPad) {
                // 虚拟方向键：圆底 + 十字四个字，按住时整块加亮
                QPainterPath cross;
                cross.addEllipse(circle);
                painter.fillPath(cross, m_dpadZone >= 0 ? pressedFill : fillColor);
                painter.setPen(strokePen);
                painter.drawPath(cross);

                painter.setPen(QColor(235, 238, 245, qMin(255, strokeAlpha + 20)));
                QFont font = this->font();
                font.setPixelSize(int(placed.radius * 0.5));
                font.setWeight(QFont::DemiBold);
                painter.setFont(font);
                const qreal half = placed.radius * 0.45;
                painter.drawText(QRectF(placed.center.x() - half, placed.center.y() - placed.radius,
                                        half * 2.0, placed.radius * 0.62),
                                 Qt::AlignCenter, dpadZoneLabel(0));
                painter.drawText(QRectF(placed.center.x() - half, placed.center.y() + placed.radius * 0.38,
                                        half * 2.0, placed.radius * 0.62),
                                 Qt::AlignCenter, dpadZoneLabel(1));
                painter.drawText(QRectF(placed.center.x() - placed.radius, placed.center.y() - placed.radius * 0.5,
                                        placed.radius * 0.62, placed.radius),
                                 Qt::AlignCenter, dpadZoneLabel(2));
                painter.drawText(QRectF(placed.center.x() + placed.radius * 0.38, placed.center.y() - placed.radius * 0.5,
                                        placed.radius * 0.62, placed.radius),
                                 Qt::AlignCenter, dpadZoneLabel(3));
                continue;
            }

            // 摇杆：外圈（浮动样式跟着手指走）+ 中心钮（画在推动方向上）
            const QRectF ring(m_joystickCenter.x() - placed.radius,
                              m_joystickCenter.y() - placed.radius,
                              placed.radius * 2.0, placed.radius * 2.0);
            QPainterPath ringPath;
            ringPath.addEllipse(ring);
            painter.fillPath(ringPath, fillColor);
            painter.setPen(strokePen);
            painter.drawPath(ringPath);

            const qreal knobRadius = placed.radius * 0.42;
            const QPointF knobCenter = m_joystickCenter + m_knobOffset;
            QPainterPath knob;
            knob.addEllipse(QRectF(knobCenter.x() - knobRadius, knobCenter.y() - knobRadius,
                                   knobRadius * 2.0, knobRadius * 2.0));
            painter.fillPath(knob, QColor(235, 238, 245, qMin(255, strokeAlpha + 20)));
            continue;
        }

        // 动作按钮：圆底 + 文字
        QPainterPath buttonPath;
        buttonPath.addEllipse(circle);
        painter.fillPath(buttonPath, pressed ? pressedFill : fillColor);
        painter.setPen(strokePen);
        painter.drawPath(buttonPath);

        // 标签优先用编辑器里写的文字；没写就显示动作名，都没有叫「按钮」
        QString label = placed.control.label;
        if (label.isEmpty())
            label = defaultActionName(placed.control.action);
        if (label.isEmpty())
            label = tr("按钮");

        painter.setPen(QColor(235, 238, 245, qMin(255, strokeAlpha + 20)));
        QFont font = this->font();
        const int chars = label.length();
        font.setPixelSize(int(placed.radius * (chars <= 1 ? 0.62 : 0.46)));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.drawText(circle, Qt::AlignCenter, label);
    }
}

void TouchControlOverlay::showEvent(QShowEvent *event)
{
    if (parentWidget())
        setGeometry(parentWidget()->rect());
    QWidget::showEvent(event);
    refreshFromSettings();
    raise();
}

void TouchControlOverlay::hideEvent(QHideEvent *event)
{
    // 隐藏时清干净在按状态，游戏退出后不会残留"按键一直按着"
    m_pointerRoles.clear();
    m_pressedButtons.clear();
    m_dpadZone = -1;
    m_knobOffset = QPointF();
    if (m_moveX != 0.0 || m_moveY != 0.0)
        emitMoveVector(0.0, 0.0);
    QWidget::hideEvent(event);
}
