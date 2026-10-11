/**
 * @file   TextOutlineLayer.h
 * @brief  文字描边层的共享实现（膨胀掩码 + 抠出字形本体，仅外侧描边）
 * @author BlockBox Team
 */
#ifndef TEXTOUTLINELAYER_H
#define TEXTOUTLINELAYER_H

#include <QColor>
#include <QDebug>
#include <QFont>
#include <QImage>
#include <QPixmap>

class QLabel;
class QTextEdit;

/**
 * @namespace TextOutlineLayer
 * @brief MarkdownOutlinedLabel 与 OutlinedTextEdit 共用的文字描边层绘制工具
 *
 * 描边生成规则：先把文字（含抗锯齿）离屏渲染成透明图像，然后对文字 alpha
 * 做「盘状膨胀」得到外扩轮廓，再用「DestinationOut 抠掉字形本体」把落字形
 * 覆盖范围内的部分去除。这样描边只存在于字形轮廓之外：
 *   - 文字自身的抗锯齿边缘不会被描边色垫底，正文保持锐利不发糊；
 *   - 描边外缘继承文字 alpha 的柔和过渡，视觉上是环绕文字的一圈净色光晕。
 */
namespace TextOutlineLayer
{
/// 对文字图像（ARGB32_Premultiplied，含抗锯齿）生成宽度为 width 像素的外描边环掩码
QImage buildOutlineRing(const QImage &textImage, int width);

/// 把掩码（仅 alpha 有效）染成指定颜色并转为带设备像素比的 QPixmap
QPixmap colorizeRing(const QImage &ringMask, const QColor &color, qreal devicePixelRatio);

/**
 * @brief 按 QLabel 绘制富文本的同一规则离屏渲染文字图像
 *
 * 复刻 qlabel.cpp 的富文本绘制约定：同 default font / 根帧 margin 0 /
 * textWidth=标签宽度 / 水平左对齐与换行模式，保证描边层与 QLabel 正文逐字形重合。
 */
QImage renderLabelText(const QLabel *label, const QRect &rect);

/**
 * @brief 按 QTextEdit 绘制文档的同一规则离屏渲染文字图像
 *
 * 直接使用编辑器自身的 QTextDocument（无需复刻排版），并补偿滚动偏移，
 * 与 viewport 坐标系对齐。
 */
QImage renderTextEdit(const QTextEdit *edit, const QSize &viewportSize);

/// 绘制描边层缓存的公共指纹项
struct CacheKey
{
    QSize size;
    int outlineWidth = 0;
    QColor color;
    qreal dpr = 1.0;
    quint64 docRevision = 0;
    QString text;
    QFont font;
    int textFormat = 0;

    bool operator==(const CacheKey &other) const
    {
        return size == other.size
            && outlineWidth == other.outlineWidth
            && color == other.color
            && qFuzzyCompare(dpr, other.dpr)
            && docRevision == other.docRevision
            && text == other.text
            && font == other.font
            && textFormat == other.textFormat;
    }
    bool operator!=(const CacheKey &other) const { return !(*this == other); }
};

/// CacheKey 的流输出（便于调试打印失效原因）
QDebug operator<<(QDebug dbg, const CacheKey &key);

}  // namespace TextOutlineLayer

#endif  // TEXTOUTLINELAYER_H
