#include "FlowLayout.h"

FlowLayout::FlowLayout(QWidget *parent, int horizontalSpacing, int verticalSpacing)
    : QLayout(parent)
    , m_hSpace(horizontalSpacing)
    , m_vSpace(verticalSpacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    qDeleteAll(m_items);
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= m_items.size())
        return nullptr;
    return m_items.takeAt(index);
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return Qt::Orientations();
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, qMax(0, width), 0), true);
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (QLayoutItem *item : m_items) {
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins m = contentsMargins();
    size += QSize(m.left() + m.right(), m.top() + m.bottom());
    return size;
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    // 布局在去除内容边距后的内区进行
    const QMargins m = contentsMargins();
    const QRect inner = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    const int hSpace = m_hSpace >= 0
                           ? m_hSpace
                           : smartSpacing(QStyle::PM_LayoutHorizontalSpacing, this);
    const int vSpace = m_vSpace >= 0
                           ? m_vSpace
                           : smartSpacing(QStyle::PM_LayoutVerticalSpacing, this);

    int x = inner.x();
    int y = inner.y();
    int lineHeight = 0;

    for (QLayoutItem *item : m_items) {
        const int itemWidth = item->sizeHint().width();
        int nextX = x + itemWidth + hSpace;
        // 当前行已有控件且再放不下：折行
        if (x > inner.x() && nextX - hSpace > inner.right() + 1) {
            x = inner.x();
            y += lineHeight + vSpace;
            nextX = x + itemWidth + hSpace;
            lineHeight = 0;
        }

        if (!testOnly) {
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
        }

        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }

    return y + lineHeight - inner.y();
}

int FlowLayout::smartSpacing(QStyle::PixelMetric metric, const QLayout *layout)
{
    QObject *parent = layout->parent();
    if (!parent) {
        return -1;
    }
    if (parent->isWidgetType()) {
        auto *pw = static_cast<QWidget *>(parent);
        return pw->style()->pixelMetric(metric, nullptr, pw);
    }
    return static_cast<QLayout *>(parent)->spacing();
}
