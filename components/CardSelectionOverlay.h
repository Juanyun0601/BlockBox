#ifndef CARDSELECTIONOVERLAY_H
#define CARDSELECTIONOVERLAY_H

#include <QWidget>

class QPropertyAnimation;

/* ============================================================
 * CardSelectionOverlay — 卡片选中高亮遮罩（自绘 + 内建淡入淡出）
 * 旧实现是「透明子控件 + QGraphicsOpacityEffect」：这个组合放进
 * QScrollArea 后滚动一次，效果控件的脏区登记就会失效，选中高亮
 * 从此不再被重绘（状态还在、屏幕上却消失）。这里改成 paintEvent
 * 自绘透明度，动画每帧走普通 repaint，不经过效果合成器，滚动后
 * 高亮保持稳定。
 * 多个本地资源列表页共用（模组 / 文件 / 投影 / 服务器）。
 * ============================================================ */
class CardSelectionOverlay : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal highlightOpacity READ highlightOpacity WRITE setHighlightOpacity)

public:
    explicit CardSelectionOverlay(QWidget *parent = nullptr);

    qreal highlightOpacity() const { return m_opacity; }
    void setHighlightOpacity(qreal opacity);

    /* 切换选中态：true 淡入，false 淡出（淡出结束后自动隐藏） */
    void setSelected(bool selected);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    qreal m_opacity = 0.0;
    QPropertyAnimation *m_anim = nullptr;
};

#endif // CARDSELECTIONOVERLAY_H
