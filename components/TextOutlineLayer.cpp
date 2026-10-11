#include "TextOutlineLayer.h"

#include <QDebug>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextOption>
#include <QTextEdit>

namespace TextOutlineLayer
{

QImage buildOutlineRing(const QImage &textImage, int width)
{
    if (textImage.isNull() || textImage.size().isEmpty())
        return QImage();

    const int w = qMax(1, width);

    // 1. 盘状膨胀：8 邻域逐轮并集。直接膨胀抗锯齿 alpha，
    //    外缘自然继承 1px 柔和过渡，不会产生硬边锯齿。
    QImage ring = textImage;
    for (int round = 0; round < w; ++round)
    {
        QImage acc(ring.size(), QImage::Format_ARGB32_Premultiplied);
        acc.fill(Qt::transparent);
        {
            QPainter p(&acc);
            static const QPoint kOffsets[] = {
                QPoint(-1, -1), QPoint(0, -1), QPoint(1, -1),
                QPoint(-1,  0),                QPoint(1,  0),
                QPoint(-1,  1), QPoint(0,  1), QPoint(1,  1)
            };
            for (const QPoint &off : kOffsets)
                p.drawImage(off, ring);
            p.end();
        }
        ring = acc;
    }

    // 2. 抠掉字形本体：alpha 乘以 (1 - 文字alpha)。
    //    落在字形覆盖范围内的膨胀结果全部移除，描边只保留在轮廓之外，
    //    文字抗锯齿边缘不会被描边色垫底，正文保持锐利。
    {
        QPainter p(&ring);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(0, 0, textImage);
        p.end();
    }

    return ring;
}

QPixmap colorizeRing(const QImage &ringMask, const QColor &color, qreal devicePixelRatio)
{
    if (ringMask.isNull() || ringMask.size().isEmpty())
        return QPixmap();

    QImage colored(ringMask.size(), QImage::Format_ARGB32_Premultiplied);
    colored.fill(color);
    {
        QPainter p(&colored);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(QPoint(0, 0), ringMask);
        p.end();
    }

    QPixmap pix = QPixmap::fromImage(colored);
    pix.setDevicePixelRatio(devicePixelRatio);
    return pix;
}

QImage renderLabelText(const QLabel *label, const QRect &rect)
{
    if (!label || rect.isEmpty() || label->text().isEmpty())
        return QImage();

    const qreal dpr = label->devicePixelRatioF();
    const QSize pxSize = QSize(rect.size()) * dpr;

    // 按 QLabel 相同的规则构建富文本文档（参照 qlabel.cpp：Markdown 走 setMarkdown，
    // documentMargin 与根帧 margin 均为 0，textWidth 设为标签宽度，默认左对齐 +
    // 同款换行模式），保证描边层与 QLabel 正文逐字形重合。
    // 注意：QTextDocument 默认 documentMargin=4，而 QLabel 内部文档为 0，
    // 不归零会使描边层相对正文整体偏移 (4px, 6px) 出现重影。
    QTextDocument doc;
    doc.setDocumentMargin(0);
    doc.setDefaultFont(label->font());
    QTextFrameFormat frameFmt = doc.rootFrame()->frameFormat();
    frameFmt.setMargin(0);
    doc.rootFrame()->setFrameFormat(frameFmt);
    doc.setTextWidth(double(rect.width()));
    QTextOption textOpt;
    textOpt.setAlignment(Qt::AlignLeft);
    textOpt.setWrapMode(label->wordWrap() ? QTextOption::WordWrap : QTextOption::ManualWrap);
    doc.setDefaultTextOption(textOpt);

    switch (label->textFormat())
    {
    case Qt::MarkdownText:
        doc.setMarkdown(label->text());
        break;
    case Qt::RichText:
        doc.setHtml(label->text());
        break;
    default:
        doc.setPlainText(label->text());
        break;
    }

    // 离屏渲染文字到透明图像（文档以左上角为原点，与 QLabel 顶部对齐绘制一致）
    QImage textImg(pxSize, QImage::Format_ARGB32_Premultiplied);
    textImg.fill(Qt::transparent);
    {
        QPainter p(&textImg);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(dpr, dpr);
        p.setClipRect(QRectF(QPointF(0, 0), QSizeF(rect.size())));
        doc.drawContents(&p, QRectF(QPointF(0, 0), QSizeF(rect.size())));
        p.end();
    }
    return textImg;
}

QImage renderTextEdit(const QTextEdit *edit, const QSize &viewportSize)
{
    if (!edit || !edit->document() || viewportSize.isEmpty())
        return QImage();

    const qreal dpr = edit->devicePixelRatioF();
    const QSize pxSize = QSize(viewportSize) * dpr;

    QImage textImg(pxSize, QImage::Format_ARGB32_Premultiplied);
    textImg.fill(Qt::transparent);
    {
        QPainter p(&textImg);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(dpr, dpr);
        // 补偿滚动偏移，使描边层与 viewport 坐标系下的正文重合
        p.translate(-edit->horizontalScrollBar()->value(),
                    -edit->verticalScrollBar()->value());
        edit->document()->drawContents(&p);
        p.end();
    }
    return textImg;
}

QDebug operator<<(QDebug dbg, const CacheKey &key)
{
    QDebugStateSaver saver(dbg);
    dbg.nospace() << "CacheKey(size=" << key.size
                  << " width=" << key.outlineWidth
                  << " color=" << key.color.name()
                  << " dpr=" << key.dpr
                  << " rev=" << key.docRevision
                  << " textLen=" << key.text.size()
                  << " font=" << key.font.family() << '/' << key.font.pointSize()
                  << " fmt=" << key.textFormat
                  << ')';
    return dbg;
}

}  // namespace TextOutlineLayer
