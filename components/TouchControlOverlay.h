#ifndef TOUCHCONTROLOVERLAY_H
#define TOUCHCONTROLOVERLAY_H

#include <QHash>
#include <QPointF>
#include <QVector>
#include <QWidget>

#include "utils/MobileControlSettings.h"

/**
 * @brief 游戏内触屏键位悬浮层（安卓版）
 * @author BlockBox Team
 *
 * 游戏运行期间铺在主窗口上的透明控件层：按「操作设置」里保存的触屏布局
 * 画出摇杆（或方向键 / 点击移动区）和动作按钮，手指操作时把输入转成信号
 * 发给游戏进程（见 GameLauncher::sendTouchInput 的输入桥协议）。
 *
 * 布局坐标按屏幕比例保存（MobileControlSettings::TouchControl），左手持机
 * 时显示位置整体镜像，不改动保存的布局。除了控件之外的区域都是拖动视角。
 */
class TouchControlOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit TouchControlOverlay(QWidget *parent = nullptr);

    /** 重新读取操作设置（触屏布局 / 按钮大小 / 不透明度等），显示前调用 */
    void refreshFromSettings();

signals:
    /** 摇杆 / 方向键输出：x、y 归一化到 -1~1，松手归零 */
    void moveVectorChanged(qreal x, qreal y);
    /** 动作按钮按下（action：jump / sneak / attack / use / inventory / sprint） */
    void actionPressed(const QString &action);
    /** 动作按钮抬起 */
    void actionReleased(const QString &action);
    /** 自定义键值 / 组合键按钮按下：同时按下这一组键（编辑器里绑定） */
    void keysPressed(const QStringList &keys);
    /** 组合键按钮抬起：整组一起松开 */
    void keysReleased(const QStringList &keys);
    /** 视角拖动增量（像素，已按灵敏度设置缩放） */
    void lookDelta(int dx, int dy);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    /// 画出来的一个控件：布局数据 + 当前屏幕上的圆心与半径
    struct PlacedControl
    {
        MobileControlSettings::TouchControl control;
        QPointF center;   ///< 圆心（屏幕坐标）
        qreal radius = 0; ///< 半径
    };

    enum class Role
    {
        None,
        Joystick,   ///< 摇杆 / 方向键 / 点击移动
        Button,     ///< 动作按钮（附加数据记按钮下标）
        Look        ///< 拖动视角
    };

    void rebuildPlacement();
    qreal buttonScaleFactor() const;
    int hitControlIndex(const QPointF &pos) const;
    void handlePointerPressed(int id, const QPointF &pos);
    void handlePointerMoved(int id, const QPointF &pos);
    void handlePointerReleased(int id, const QPointF &pos);
    void updateJoystickVector(const QPointF &pos);
    void emitMoveVector(qreal x, qreal y);
    QString dpadZoneLabel(int zone) const;

    QVector<MobileControlSettings::TouchControl> m_layout;
    QVector<PlacedControl> m_placed;
    QHash<int, QPair<Role, int>> m_pointerRoles; ///< 触点 id → 角色 + 附加数据
    QPointF m_joystickCenter;                    ///< 摇杆当前中心（浮动样式跟着手指走）
    QPointF m_knobOffset;                        ///< 摇杆钮相对中心的位置（推动方向）
    QVector<int> m_pressedButtons;               ///< 当前按下的按钮下标
    QPointF m_lastLookPos;
    qreal m_moveX = 0.0;                         ///< 当前输出的移动向量（变化才发射）
    qreal m_moveY = 0.0;
    int m_dpadZone = -1;                         ///< 方向键方案下当前按住的分区（0~3）
    bool m_layoutLoaded = false;
};

#endif // TOUCHCONTROLOVERLAY_H
