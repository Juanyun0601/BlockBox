#ifndef GAMEFLOATINGICON_H
#define GAMEFLOATINGICON_H

#include <QPixmap>
#include <QPoint>
#include <QWidget>

/**
 * @brief 游戏内悬浮的方块盒子图标（安卓版实例助手入口）
 * @author BlockBox Team
 *
 * 游戏启动后显示在主窗口上层的半透明圆形图标，支持拖动调整位置；
 * 未产生拖动位移的按下-抬起视为点击，发出 assistantToggleRequested()
 * 信号用于弹出/收起实例助手侧栏。
 */
class GameFloatingIcon : public QWidget
{
    Q_OBJECT

public:
    explicit GameFloatingIcon(QWidget *parent = nullptr);

    void setIconPixmap(const QPixmap &pixmap);

signals:
    /** 单击悬浮图标：请求弹出/收起实例助手 */
    void assistantToggleRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QPixmap m_iconPixmap;
    QPoint m_pressOffset;
    bool m_dragging = false;
    bool m_positioned = false; ///< 是否已按默认位置停靠（仅首次显示时设置）
};

#endif // GAMEFLOATINGICON_H
