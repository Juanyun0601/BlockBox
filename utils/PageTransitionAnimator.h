#pragma once

#include <QObject>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QStackedWidget>
#include <QWidget>

class PageTransitionAnimator : public QObject
{
    Q_OBJECT

public:
    enum TransitionType {
        Fade,
        SlideLeft,
        SlideRight,
        SlideUp,
        SlideDown,
        ZoomIn,
        ZoomOut,
        FadeSlideUp
    };

    explicit PageTransitionAnimator(QStackedWidget *stackedWidget, QObject *parent = nullptr);
    ~PageTransitionAnimator();

    void setTransitionType(TransitionType type);
    void setDuration(int ms);
    void setEasingCurve(const QEasingCurve &curve);
    void setEnabled(bool enabled);

    bool isEnabled() const;
    bool isAnimating() const;

    void animateToIndex(int index);
    void animateToWidget(QWidget *widget);

signals:
    void transitionStarted();
    void transitionFinished();

private:
    QStackedWidget *m_stackedWidget;
    TransitionType m_transitionType;
    int m_duration;
    QEasingCurve m_easingCurve;
    bool m_enabled;
    bool m_animating;

    QParallelAnimationGroup* createFadeAnimation(QWidget *from, QWidget *to);
    QParallelAnimationGroup* createSlideAnimation(QWidget *from, QWidget *to, Qt::Orientation orientation, int direction);
    QParallelAnimationGroup* createZoomAnimation(QWidget *from, QWidget *to, bool zoomIn);
    QParallelAnimationGroup* createFadeSlideUpAnimation(QWidget *from, QWidget *to);
};
