#pragma once

#include <QObject>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QVector>
#include <QWidget>
#include <QEasingCurve>

class ContentAnimator : public QObject
{
    Q_OBJECT

public:
    explicit ContentAnimator(QObject *parent = nullptr);
    ~ContentAnimator();

    void animateStaggered(const QVector<QWidget*> &widgets,
                          int staggerDelay = 80,
                          int duration = 300,
                          QEasingCurve::Type easing = QEasingCurve::OutCubic);

    void animateFadeIn(QWidget *widget, int duration = 300);
    void animateSlideIn(QWidget *widget, int duration = 350, Qt::Orientation orientation = Qt::Vertical);
    void animateScaleIn(QWidget *widget, int duration = 300);

    void resetWidget(QWidget *widget);
    void resetAll();

    bool isAnimating() const;

signals:
    void finished();

private:
    QVector<QWidget*> m_animatedWidgets;
    bool m_animating;

    void applyInitialState(QWidget *widget);
};
