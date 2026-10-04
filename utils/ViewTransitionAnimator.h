#ifndef VIEWTRANSITIONANIMATOR_H
#define VIEWTRANSITIONANIMATOR_H

#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QEasingCurve>

class QWidget;
class QPropertyAnimation;
class QGraphicsOpacityEffect;

/* ============================================================
 * ViewTransitionAnimator — 视图切换滑动过渡动画
 *
 * 在列表式 ↔ 瀑布流式切换时，将旧视图截图为 pixmap，
 * 新视图在下方重建完成后，旧截图滑出 + 新内容淡入，
 * 产生流畅的滑动过渡效果。
 *
 * 用法：
 *   1. 切换前调用 ViewTransitionAnimator::capture(container) 保存快照
 *   2. 执行重建逻辑（clear + rebuild）
 *   3. 调用 ViewTransitionAnimator::animateSlide(...) 播放动画
 * ============================================================ */
class ViewTransitionAnimator : public QObject
{
    Q_OBJECT
public:
    enum SlideDirection { SlideLeft, SlideRight, SlideUp, SlideDown };

    explicit ViewTransitionAnimator(QObject *parent = nullptr);
    ~ViewTransitionAnimator() override;

    bool isAnimating() const { return m_animating; }

    /* 便捷静态接口 */
    static QPixmap capture(QWidget *widget);

    void animateSlide(QWidget *container,
                      const QPixmap &oldSnapshot,
                      SlideDirection direction = SlideLeft,
                      int duration = 350,
                      QEasingCurve::Type easing = QEasingCurve::OutCubic);

signals:
    void finished();

private:
    bool m_animating = false;
};

#endif // VIEWTRANSITIONANIMATOR_H
