/**
 * @file   MarkdownOutlinedLabel.h
 * @brief  支持 Markdown/富文本的带描边文字标签（文字笔画描边，非区域边框）
 * @author BlockBox Team
 */
#ifndef MARKDOWNOUTLINEDLABEL_H
#define MARKDOWNOUTLINEDLABEL_H

#include <QLabel>
#include <QPixmap>

#include "TextOutlineLayer.h"

/**
 * @class MarkdownOutlinedLabel
 * @brief 在 QLabel 基础上为 Markdown/富文本文字绘制外描边轮廓
 *
 * 与 OutlinedLabel 的区别：OutlinedLabel 仅支持纯文本（QPainter::drawText），
 * 无法渲染 Markdown 的加粗/代码/链接等格式。本类将 QLabel 内部的
 * QTextDocument 离屏渲染后，通过 TextOutlineLayer 生成「字形轮廓外侧」的
 * 描边环（盘状膨胀 + 抠掉字形本体），叠在正文之下——描边不垫在文字
 * 抗锯齿边缘之下，正文保持锐利。文字可选、链接点击等 QLabel 既有行为不变。
 *
 * 描边颜色默认取 ThemeManager 的当前文字边框颜色（@TEXT_BORDER@）。
 */
class MarkdownOutlinedLabel : public QLabel
{
    Q_OBJECT
    Q_PROPERTY(int outlineWidth READ outlineWidth WRITE setOutlineWidth)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor)

public:
    explicit MarkdownOutlinedLabel(const QString &text = QString(), QWidget *parent = nullptr);

    /// 描边宽度（像素）
    int outlineWidth() const;
    void setOutlineWidth(int width);

    /// 描边颜色；默认跟随 ThemeManager 文字边框颜色
    QColor outlineColor() const;
    void setOutlineColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool outlineDirty() const;
    void renderOutlineLayer();

    int m_outlineWidth;
    QColor m_outlineColor;
    bool m_customColor;
    QPixmap m_outlineLayer;       ///< 描边层缓存（含设备像素比）
    TextOutlineLayer::CacheKey m_cacheKey;  ///< 描边层缓存指纹
};

#endif // MARKDOWNOUTLINEDLABEL_H
