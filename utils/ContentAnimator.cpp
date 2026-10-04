#include "ContentAnimator.h"
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <QStyle>
#include <QPushButton>
#include <QLabel>

ContentAnimator::ContentAnimator(QObject *parent)
    : QObject(parent)
    , m_animating(false)
{
}

// QGraphicsOpacityEffect 会让控件在离屏 pixmap 上绘制，
// 导致 QSS 中基于 objectName 的样式在动画结束后丢失。
// 清除 effect 后需要主动触发样式重算。
static void refreshWidgetStyle(QWidget *widget)
{
    if (!widget)
        return;

    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

ContentAnimator::~ContentAnimator()
{
    resetAll();
}

void ContentAnimator::applyInitialState(QWidget *widget)
{
    auto *effect = new QGraphicsOpacityEffect(widget);
    effect->setOpacity(0.0);
    widget->setGraphicsEffect(effect);
}

void ContentAnimator::animateStaggered(const QVector<QWidget*> &widgets,
                                        int staggerDelay,
                                        int duration,
                                        QEasingCurve::Type easing)
{
    if (widgets.isEmpty() || m_animating)
        return;

    m_animating = true;
    m_animatedWidgets = widgets;

    auto *group = new QSequentialAnimationGroup(this);

    for (int i = 0; i < widgets.size(); ++i)
    {
        QWidget *w = widgets[i];
        if (!w) continue;

        applyInitialState(w);

        auto *effect = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
        if (!effect) continue;

        auto *anim = new QPropertyAnimation(effect, "opacity");
        anim->setDuration(duration);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(easing);

        group->addPause(staggerDelay);
        group->addAnimation(anim);
    }

    connect(group, &QSequentialAnimationGroup::finished, this, [this, widgets]()
    {
        m_animating = false;
        for (QWidget *w : widgets)
        {
            if (w) {
                w->setGraphicsEffect(nullptr);
                refreshWidgetStyle(w);
            }
        }
        emit finished();
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void ContentAnimator::animateFadeIn(QWidget *widget, int duration)
{
    if (!widget) return;

    applyInitialState(widget);

    auto *effect = qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect());
    if (!effect) return;

    auto *anim = new QPropertyAnimation(effect, "opacity");
    anim->setDuration(duration);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);

    m_animatedWidgets.append(widget);

    connect(anim, &QPropertyAnimation::finished, this, [widget]()
    {
        if (widget) {
            widget->setGraphicsEffect(nullptr);
            refreshWidgetStyle(widget);
        }
    });
    connect(anim, &QPropertyAnimation::finished, anim, &QObject::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ContentAnimator::animateSlideIn(QWidget *widget, int duration, Qt::Orientation orientation)
{
    if (!widget) return;

    int offset = (orientation == Qt::Vertical) ? 30 : 30;
    QPoint startPos = widget->pos();
    QPoint offsetPos = (orientation == Qt::Vertical)
        ? QPoint(startPos.x(), startPos.y() + offset)
        : QPoint(startPos.x() + offset, startPos.y());

    widget->move(offsetPos);

    applyInitialState(widget);

    auto *group = new QParallelAnimationGroup();

    auto *effect = qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect());
    if (effect)
    {
        auto *opacityAnim = new QPropertyAnimation(effect, "opacity");
        opacityAnim->setDuration(duration);
        opacityAnim->setStartValue(0.0);
        opacityAnim->setEndValue(1.0);
        opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
        group->addAnimation(opacityAnim);
    }

    auto *posAnim = new QPropertyAnimation(widget, "pos");
    posAnim->setDuration(duration);
    posAnim->setStartValue(offsetPos);
    posAnim->setEndValue(startPos);
    posAnim->setEasingCurve(QEasingCurve::OutBack);
    group->addAnimation(posAnim);

    m_animatedWidgets.append(widget);

    connect(group, &QParallelAnimationGroup::finished, this, [widget]()
    {
        if (widget) {
            widget->setGraphicsEffect(nullptr);
            refreshWidgetStyle(widget);
        }
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void ContentAnimator::animateScaleIn(QWidget *widget, int duration)
{
    if (!widget) return;

    applyInitialState(widget);

    auto *effect = qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect());
    if (!effect) return;

    auto *anim = new QPropertyAnimation(effect, "opacity");
    anim->setDuration(duration);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutBack);

    m_animatedWidgets.append(widget);

    connect(anim, &QPropertyAnimation::finished, this, [widget]()
    {
        if (widget) {
            widget->setGraphicsEffect(nullptr);
            refreshWidgetStyle(widget);
        }
    });
    connect(anim, &QPropertyAnimation::finished, anim, &QObject::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ContentAnimator::resetWidget(QWidget *widget)
{
    if (!widget) return;
    widget->setGraphicsEffect(nullptr);
}

void ContentAnimator::resetAll()
{
    for (auto *w : m_animatedWidgets)
    {
        if (w)
            w->setGraphicsEffect(nullptr);
    }
    m_animatedWidgets.clear();
    m_animating = false;
}

bool ContentAnimator::isAnimating() const
{
    return m_animating;
}
