/**
 * @file   CustomCheckBox.h
 * @brief  自定义滑块开关组件声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef CUSTOMCHECKBOX_H
#define CUSTOMCHECKBOX_H

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPropertyAnimation>

class CustomCheckBox : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
    Q_PROPERTY(qreal knobPosition READ knobPosition WRITE setKnobPosition)

public:
    explicit CustomCheckBox(const QString &text = "", QWidget *parent = nullptr);
    ~CustomCheckBox();

    bool isChecked() const;
    void setChecked(bool checked);
    QString text() const;
    void setText(const QString &text);
    void updateThemeStyle();

    qreal knobPosition() const;
    void setKnobPosition(qreal pos);

signals:
    void toggled(bool checked);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QHBoxLayout *m_layout;
    QWidget *m_checkBox;
    QLabel *m_textLabel;
    bool m_checked;
    bool m_hovered;
    bool m_pressed;
    qreal m_knobPosition;
    QPropertyAnimation *m_animation;
};

#endif // CUSTOMCHECKBOX_H
