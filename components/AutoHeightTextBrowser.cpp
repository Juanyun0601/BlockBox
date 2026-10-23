/**
 * @file   AutoHeightTextBrowser.cpp
 * @brief  随内容自动撑高的富文本浏览框实现
 * @author BlockBox Team
 * @date   2026-10-22
 */
#include "AutoHeightTextBrowser.h"

#include <QAbstractTextDocumentLayout>
#include <QEvent>
#include <QMargins>
#include <QResizeEvent>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QtMath>

AutoHeightTextBrowser::AutoHeightTextBrowser(QWidget *parent)
    : QTextBrowser(parent)
    , m_fitting(false)
    , m_fitPending(false)
    , m_sized(false)
    , m_fitPasses(0)
{
    // 不显示任何滚动条，高度完全由内容决定
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // 排版结果或文本内容变化后延后一拍重排高度，避免在布局过程中改动尺寸
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, &AutoHeightTextBrowser::scheduleFit);
    connect(this, &QTextEdit::textChanged,
            this, &AutoHeightTextBrowser::scheduleFit);
}

void AutoHeightTextBrowser::fitToContents()
{
    if (m_fitting || !document())
        return;

    const int width = viewport()->width();
    if (width <= 0)
        return;   // 尚未完成首次布局

    m_fitting = true;
    QTextDocument *doc = document();
    const qreal docWidth = doc->textWidth();
    if (docWidth < 0 || qRound(docWidth) != width)
        doc->setTextWidth(width);
    const int contentHeight = qCeil(doc->size().height());

    // 文档高度只覆盖正文本身，边框和内边距（主题里 QTextEdit/QTextBrowser 的 padding）
    // 另外占高度，不补上的话最后一两行会被切掉，所以这里把这段固定占用算进来。
    int chrome;
    if (m_sized && viewport()->height() > 0) {
        // 已经按内容设置过高度：控件高度与视口高度的差值就是被边框/内边距吃掉的部分
        chrome = qMax(0, height() - viewport()->height());
    } else {
        // 首次布局时视口高度还没定，先用内容边距估算，下一轮会按实测值校正
        const QMargins margins = contentsMargins();
        chrome = margins.top() + margins.bottom() + 2;
    }

    if (contentHeight <= 0) {
        m_fitting = false;
        return;
    }
    const int targetHeight = contentHeight + chrome + 1;
    const bool changed = (height() != targetHeight);
    if (changed)
        setFixedHeight(targetHeight);   // m_fitting 仍为 true，避免 resizeEvent 立刻重入
    m_sized = true;
    m_fitting = false;

    if (changed && m_fitPasses < 4) {
        ++m_fitPasses;
        scheduleFit();   // 再走一轮，用实测的边框/内边距把高度校准到位
    } else if (!changed) {
        m_fitPasses = 0;
    }
}

void AutoHeightTextBrowser::scheduleFit()
{
    if (m_fitPending)
        return;
    m_fitPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_fitPending = false;
        fitToContents();
    });
}

void AutoHeightTextBrowser::resizeEvent(QResizeEvent *event)
{
    QTextBrowser::resizeEvent(event);
    fitToContents();
}

void AutoHeightTextBrowser::changeEvent(QEvent *event)
{
    QTextBrowser::changeEvent(event);
    // 换主题、换字号都会改变排版高度，需要重新贴合内容
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange)
        scheduleFit();
}

void AutoHeightTextBrowser::showEvent(QShowEvent *event)
{
    QTextBrowser::showEvent(event);
    fitToContents();
    // 首次显示时宽度可能还没最终确定，再在事件循环空闲时补算一次
    scheduleFit();
}

