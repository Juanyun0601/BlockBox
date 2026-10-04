/**
 * @file   OutlinedLabel.h
 * @brief  带描边的文字标签（文字笔画描边，非区域边框）
 * @author BlockBox Team
 */
#ifndef OUTLINEDLABEL_H
#define OUTLINEDLABEL_H

#include <QLabel>

/**
 * @class OutlinedLabel
 * @brief 在 QLabel 基础上为文字笔画绘制描边轮廓
 *
 * 描边颜色默认取 ThemeManager 的当前文字边框颜色（@TEXT_BORDER@），
 * 文字填充色仍沿用样式表/调色板设置。适合直接显示在页面背景上的
 * 标题、区段标题等文字，替代 QSS 无法实现的文字描边效果。
 */
class OutlinedLabel : public QLabel
{
    Q_OBJECT
    Q_PROPERTY(int outlineWidth READ outlineWidth WRITE setOutlineWidth)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor)

public:
    explicit OutlinedLabel(const QString &text = QString(), QWidget *parent = nullptr);

    /// 描边宽度（像素）
    int outlineWidth() const;
    void setOutlineWidth(int width);

    /// 描边颜色；默认跟随 ThemeManager 文字边框颜色
    QColor outlineColor() const;
    void setOutlineColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_outlineWidth;
    QColor m_outlineColor;
    bool m_customColor;
};

#endif // OUTLINEDLABEL_H
