/**
 * @file   CustomRadioButton.h
 * @brief  自定义单选按钮类声明 - 现代化切换开关样式
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef CUSTOMRADIOBUTTON_H
#define CUSTOMRADIOBUTTON_H

#include <QPropertyAnimation>
#include <QRadioButton>
#include <QPainter>

class CustomRadioButton : public QRadioButton
{
    Q_OBJECT
    Q_PROPERTY(qreal knobPosition READ knobPosition WRITE setKnobPosition)

public:
    explicit CustomRadioButton(const QString &text = QString(), QWidget *parent = nullptr);
    ~CustomRadioButton();

    qreal knobPosition() const;
    void setKnobPosition(qreal pos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void updateThemeStyle();
    QString getCheckText() const;
    void animateToggle();

    bool m_hovered;
    bool m_pressed;
    qreal m_knobPosition;
    QPropertyAnimation *m_animation;
};

#endif // CUSTOMRADIOBUTTON_H