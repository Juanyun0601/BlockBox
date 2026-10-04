#include "ViewTransitionAnimator.h"
#include <QLabel>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QWidget>
#include <QStyle>

ViewTransitionAnimator::ViewTransitionAnimator(QObject *parent)
    : QObject(parent)
    , m_animating(false)
{
}

ViewTransitionAnimator::~ViewTransitionAnimator()
{
}

QPixmap ViewTransitionAnimator::capture(QWidget *widget)
{
    if (!widget) return QPixmap();
    return widget->grab();
}

void ViewTransitionAnimator::animateSlide(QWidget *container,
                                           const QPixmap &oldSnapshot,
                                           SlideDirection direction,
                                           int duration,
                                           QEasingCurve::Type easing)
{
    if (!container || oldSnapshot.isNull() || m_animating)
        return;

    m_animating = true;

    QWidget *overlay = new QWidget(container);
    overlay->setGeometry(container->rect());
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->show();
    overlay->raise();

    QLabel *snapshotLabel = new QLabel(overlay);
    snapshotLabel->setPixmap(oldSnapshot);
    snapshotLabel->setGeometry(0, 0, oldSnapshot.width(), oldSnapshot.height());

    QPoint startPos(0, 0);
    QPoint endPos;

    switch (direction) {
    case SlideLeft:
        endPos = QPoint(-oldSnapshot.width(), 0);
        break;
    case SlideRight:
        endPos = QPoint(oldSnapshot.width(), 0);
        break;
    case SlideUp:
        endPos = QPoint(0, -oldSnapshot.height());
        break;
    case SlideDown:
        endPos = QPoint(0, oldSnapshot.height());
        break;
    }

    auto *effect = new QGraphicsOpacityEffect(overlay);
    effect->setOpacity(1.0);
    overlay->setGraphicsEffect(effect);

    auto *group = new QParallelAnimationGroup(this);

    auto *opacityAnim = new QPropertyAnimation(effect, "opacity");
    opacityAnim->setDuration(duration);
    opacityAnim->setStartValue(1.0);
    opacityAnim->setEndValue(0.0);
    opacityAnim->setEasingCurve(easing);
    group->addAnimation(opacityAnim);

    auto *slideAnim = new QPropertyAnimation(overlay, "pos");
    slideAnim->setDuration(duration);
    slideAnim->setStartValue(startPos);
    slideAnim->setEndValue(endPos);
    slideAnim->setEasingCurve(easing);
    group->addAnimation(slideAnim);

    connect(group, &QParallelAnimationGroup::finished, this, [this, overlay]()
    {
        m_animating = false;
        overlay->deleteLater();
        emit finished();
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}
