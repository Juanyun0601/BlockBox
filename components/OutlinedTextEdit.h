/**
 * @file   OutlinedTextEdit.h
 * @brief  带文字外描边的只读 QTextEdit（AI 回答流式输出阶段使用）
 * @author BlockBox Team
 */
#ifndef OUTLINEDTEXTEDIT_H
#define OUTLINEDTEXTEDIT_H

#include <QPixmap>
#include <QTextEdit>

#include "TextOutlineLayer.h"

/**
 * @class OutlinedTextEdit
 * @brief 在 QTextEdit 正文之下绘制文字外描边层的轻量包装
 *
 * 直接使用编辑器自身的 QTextDocument 离屏渲染文字并生成描边环
 * （TextOutlineLayer：盘状膨胀 + 抠掉字形本体），排版零复刻、逐像素对齐。
 * 供 AI 助手页流式回答使用，使流式纯文本阶段与收尾后的 Markdown 标签
 * 描边观感一致；描边颜色默认跟随 ThemeManager 文字边框颜色。
 *
 * 仅设计用于只读、无滚动条、随内容增高（fixedHeight）的场景；
 * 滚动偏移已在渲染时补偿，普通滚动场景同样成立。
 */
class OutlinedTextEdit : public QTextEdit
{
    Q_OBJECT
    Q_PROPERTY(int outlineWidth READ outlineWidth WRITE setOutlineWidth)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor)
    Q_PROPERTY(bool outlineEnabled READ outlineEnabled WRITE setOutlineEnabled)

public:
    explicit OutlinedTextEdit(QWidget *parent = nullptr);

    int outlineWidth() const;
    void setOutlineWidth(int width);

    /// 描边颜色；默认跟随 ThemeManager 文字边框颜色
    QColor outlineColor() const;
    void setOutlineColor(const QColor &color);

    /// 描边开关（思考内容等不需要描边的流式编辑器可关闭）
    bool outlineEnabled() const;
    void setOutlineEnabled(bool enabled);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool outlineDirty() const;
    void renderOutlineLayer();

    int m_outlineWidth;
    QColor m_outlineColor;
    bool m_customColor;
    bool m_outlineEnabled;
    QPixmap m_outlineLayer;       ///< 描边层缓存（含设备像素比）
    TextOutlineLayer::CacheKey m_cacheKey;  ///< 描边层缓存指纹
};

#endif // OUTLINEDTEXTEDIT_H
