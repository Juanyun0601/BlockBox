#include "PageTransitionAnimator.h"

#include <QDebug>
#include <QStyle>
#include <QPushButton>
#include <QLabel>
#include <QOpenGLWidget>

PageTransitionAnimator::PageTransitionAnimator(QStackedWidget *stackedWidget, QObject *parent)
    : QObject(parent)
    , m_stackedWidget(stackedWidget)
    , m_transitionType(FadeSlideUp)
    , m_duration(350)
    , m_easingCurve(QEasingCurve::OutCubic)
    , m_enabled(true)
    , m_animating(false)
{
}

// QGraphicsOpacityEffect 会让目标 widget 在离屏 pixmap 上绘制，
// 这会导致 QSS 中基于 objectName 的子控件样式在首次渲染时丢失。
// 在动画结束、清除 effect 之后，需要主动触发样式重算，确保所有子控件正确显示。
static void refreshStyleAfterEffect(QWidget *widget)
{
    if (!widget)
        return;

    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();

    // 对所有子控件也触发样式重算，尤其是 QPushButton 等依赖 QSS objectName 的控件
    const auto buttons = widget->findChildren<QPushButton *>();
    for (QPushButton *btn : buttons) {
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
        btn->update();
    }
    const auto labels = widget->findChildren<QLabel *>();
    for (QLabel *lbl : labels) {
        lbl->style()->unpolish(lbl);
        lbl->style()->polish(lbl);
        lbl->update();
    }
}

// QGraphicsOpacityEffect 会把控件渲染到 offscreen pixmap 再合成，
// 而 QOpenGLWidget（如 Skin3DWidget）必须直接绑定 GL 上下文渲染，无法被合成到 pixmap 中。
// 对包含 QOpenGLWidget 的页面应用透明度效果会导致其 GL 内容丢失或空白（需拖拽才能恢复）。
// 因此切换目标页含 QOpenGLWidget 时跳过透明度效果，仅保留位置动画。
static bool containsOpenGLWidget(QWidget *widget)
{
    return widget && widget->findChild<QOpenGLWidget *>() != nullptr;
}

PageTransitionAnimator::~PageTransitionAnimator()
{
}

void PageTransitionAnimator::setTransitionType(TransitionType type)
{
    m_transitionType = type;
}

void PageTransitionAnimator::setDuration(int ms)
{
    m_duration = qBound(100, ms, 2000);
}

void PageTransitionAnimator::setEasingCurve(const QEasingCurve &curve)
{
    m_easingCurve = curve;
}

void PageTransitionAnimator::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool PageTransitionAnimator::isEnabled() const
{
    return m_enabled;
}

bool PageTransitionAnimator::isAnimating() const
{
    return m_animating;
}

void PageTransitionAnimator::animateToIndex(int index)
{
    if (!m_stackedWidget || index < 0 || index >= m_stackedWidget->count())
        return;

    QWidget *to = m_stackedWidget->widget(index);
    if (!to)
        return;

    animateToWidget(to);
}

void PageTransitionAnimator::animateToWidget(QWidget *widget)
{
    if (!m_stackedWidget || !widget || m_animating)
        return;

    QWidget *from = m_stackedWidget->currentWidget();
    if (from == widget)
        return;

    if (!m_enabled)
    {
        m_stackedWidget->setCurrentWidget(widget);
        return;
    }

    m_animating = true;
    emit transitionStarted();

    QParallelAnimationGroup *group = nullptr;

    switch (m_transitionType)
    {
    case Fade:
        group = createFadeAnimation(from, widget);
        break;
    case SlideLeft:
        group = createSlideAnimation(from, widget, Qt::Horizontal, -1);
        break;
    case SlideRight:
        group = createSlideAnimation(from, widget, Qt::Horizontal, 1);
        break;
    case SlideUp:
        group = createSlideAnimation(from, widget, Qt::Vertical, 1);
        break;
    case SlideDown:
        group = createSlideAnimation(from, widget, Qt::Vertical, -1);
        break;
    case ZoomIn:
        group = createZoomAnimation(from, widget, true);
        break;
    case ZoomOut:
        group = createZoomAnimation(from, widget, false);
        break;
    case FadeSlideUp:
        group = createFadeSlideUpAnimation(from, widget);
        break;
    }

    if (!group)
    {
        m_stackedWidget->setCurrentWidget(widget);
        m_animating = false;
        return;
    }

    connect(group, &QParallelAnimationGroup::finished, this, [this]()
    {
        m_animating = false;
        emit transitionFinished();
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

QParallelAnimationGroup* PageTransitionAnimator::createFadeAnimation(QWidget *from, QWidget *to)
{
    auto *group = new QParallelAnimationGroup();

    // 目标页含 QOpenGLWidget 时跳过透明度效果（见 containsOpenGLWidget 说明）
    const bool fromGL = containsOpenGLWidget(from);
    const bool toGL = containsOpenGLWidget(to);

    if (from && !fromGL)
    {
        auto *effect = new QGraphicsOpacityEffect(from);
        from->setGraphicsEffect(effect);
        auto *anim = new QPropertyAnimation(effect, "opacity");
        anim->setDuration(m_duration);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        anim->setEasingCurve(m_easingCurve);
        group->addAnimation(anim);
    }

    m_stackedWidget->setCurrentWidget(to);

    if (!toGL)
    {
        auto *effect = new QGraphicsOpacityEffect(to);
        to->setGraphicsEffect(effect);
        auto *anim = new QPropertyAnimation(effect, "opacity");
        anim->setDuration(m_duration);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(m_easingCurve);
        group->addAnimation(anim);
    }

    connect(group, &QParallelAnimationGroup::finished, to, [to]()
    {
        to->setGraphicsEffect(nullptr);
        refreshStyleAfterEffect(to);
    });

    return group;
}

QParallelAnimationGroup* PageTransitionAnimator::createSlideAnimation(QWidget *from, QWidget *to, Qt::Orientation orientation, int direction)
{
    auto *group = new QParallelAnimationGroup();
    QRect startGeometry = m_stackedWidget->rect();

    int offset = (orientation == Qt::Horizontal) ? startGeometry.width() : startGeometry.height();
    offset *= direction;

    // 目标页含 QOpenGLWidget 时跳过透明度效果（见 containsOpenGLWidget 说明）
    const bool fromGL = containsOpenGLWidget(from);
    const bool toGL = containsOpenGLWidget(to);

    if (from && !fromGL)
    {
        auto *anim = new QPropertyAnimation(from, "pos");
        anim->setDuration(m_duration);
        anim->setStartValue(from->pos());
        if (orientation == Qt::Horizontal)
            anim->setEndValue(QPoint(from->x() - offset, from->y()));
        else
            anim->setEndValue(QPoint(from->x(), from->y() - offset));
        anim->setEasingCurve(m_easingCurve);
        group->addAnimation(anim);

        auto *opacityEffect = new QGraphicsOpacityEffect(from);
        from->setGraphicsEffect(opacityEffect);
        auto *opacityAnim = new QPropertyAnimation(opacityEffect, "opacity");
        opacityAnim->setDuration(m_duration * 0.7);
        opacityAnim->setStartValue(1.0);
        opacityAnim->setEndValue(0.0);
        opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
        group->addAnimation(opacityAnim);
    }

    to->setGeometry(startGeometry);
    if (orientation == Qt::Horizontal)
        to->move(to->x() + offset, to->y());
    else
        to->move(to->x(), to->y() + offset);
    m_stackedWidget->setCurrentWidget(to);

    auto *anim = new QPropertyAnimation(to, "pos");
    anim->setDuration(m_duration);
    anim->setStartValue(to->pos());
    anim->setEndValue(startGeometry.topLeft());
    anim->setEasingCurve(m_easingCurve);
    group->addAnimation(anim);

    if (!toGL)
    {
        auto *opacityEffect = new QGraphicsOpacityEffect(to);
        to->setGraphicsEffect(opacityEffect);
        auto *opacityAnim = new QPropertyAnimation(opacityEffect, "opacity");
        opacityAnim->setDuration(m_duration);
        opacityAnim->setStartValue(0.3);
        opacityAnim->setEndValue(1.0);
        opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
        group->addAnimation(opacityAnim);
    }

    connect(group, &QParallelAnimationGroup::finished, to, [to]()
    {
        to->setGraphicsEffect(nullptr);
        refreshStyleAfterEffect(to);
    });

    return group;
}

QParallelAnimationGroup* PageTransitionAnimator::createZoomAnimation(QWidget *from, QWidget *to, bool zoomIn)
{
    Q_UNUSED(zoomIn);
    auto *group = new QParallelAnimationGroup();

    // 目标页含 QOpenGLWidget 时跳过透明度效果（见 containsOpenGLWidget 说明）
    const bool fromGL = containsOpenGLWidget(from);
    const bool toGL = containsOpenGLWidget(to);

    if (from && !fromGL)
    {
        auto *effect = new QGraphicsOpacityEffect(from);
        from->setGraphicsEffect(effect);
        auto *anim = new QPropertyAnimation(effect, "opacity");
        anim->setDuration(m_duration);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        anim->setEasingCurve(m_easingCurve);
        group->addAnimation(anim);
    }

    m_stackedWidget->setCurrentWidget(to);

    if (!toGL)
    {
        auto *effect = new QGraphicsOpacityEffect(to);
        to->setGraphicsEffect(effect);
        auto *opacityAnim = new QPropertyAnimation(effect, "opacity");
        opacityAnim->setDuration(m_duration);
        opacityAnim->setStartValue(0.0);
        opacityAnim->setEndValue(1.0);
        opacityAnim->setEasingCurve(m_easingCurve);
        group->addAnimation(opacityAnim);
    }

    connect(group, &QParallelAnimationGroup::finished, to, [to]()
    {
        to->setGraphicsEffect(nullptr);
        refreshStyleAfterEffect(to);
    });

    return group;
}

QParallelAnimationGroup* PageTransitionAnimator::createFadeSlideUpAnimation(QWidget *from, QWidget *to)
{
    auto *group = new QParallelAnimationGroup();
    QRect startGeometry = m_stackedWidget->rect();
    int offset = 40;

    // 目标页含 QOpenGLWidget（如 AccountManagePage / SkinEditorPage 中的 Skin3DWidget）时，
    // 跳过透明度效果，否则 QGraphicsEffect 会破坏其 GL 渲染导致皮肤模型空白。
    const bool fromGL = containsOpenGLWidget(from);
    const bool toGL = containsOpenGLWidget(to);

    if (from && !fromGL)
    {
        auto *opacityEffect = new QGraphicsOpacityEffect(from);
        from->setGraphicsEffect(opacityEffect);
        auto *opacityAnim = new QPropertyAnimation(opacityEffect, "opacity");
        opacityAnim->setDuration(m_duration * 0.6);
        opacityAnim->setStartValue(1.0);
        opacityAnim->setEndValue(0.0);
        opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
        group->addAnimation(opacityAnim);
    }

    to->setGeometry(startGeometry);
    to->move(to->x(), to->y() + offset);
    m_stackedWidget->setCurrentWidget(to);

    auto *posAnim = new QPropertyAnimation(to, "pos");
    posAnim->setDuration(m_duration);
    posAnim->setStartValue(to->pos());
    posAnim->setEndValue(startGeometry.topLeft());
    posAnim->setEasingCurve(QEasingCurve::OutBack);
    group->addAnimation(posAnim);

    if (!toGL)
    {
        auto *opacityEffect = new QGraphicsOpacityEffect(to);
        to->setGraphicsEffect(opacityEffect);
        auto *opacityAnim = new QPropertyAnimation(opacityEffect, "opacity");
        opacityAnim->setDuration(m_duration);
        opacityAnim->setStartValue(0.0);
        opacityAnim->setEndValue(1.0);
        opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
        group->addAnimation(opacityAnim);
    }

    connect(group, &QParallelAnimationGroup::finished, to, [to]()
    {
        to->setGraphicsEffect(nullptr);
        refreshStyleAfterEffect(to);
    });

    return group;
}
