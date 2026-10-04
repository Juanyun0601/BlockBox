#include "MasonryLayout.h"

#include <QWidget>
#include <QStyle>

/* 继承 FlowLayout 的 itemList/m_hSpace/m_vSpace（protected） */

MasonryLayout::MasonryLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : FlowLayout(parent, margin, hSpacing, vSpacing)
{
}

int MasonryLayout::smartMargin() const
{
    return contentsMargins().left();
}

/* 最短列瀑布流算法：
 * - 列数 = 容器可用宽 / (卡片宽 + 水平间距)，至少 1 列
 * - 每个条目放入当前累计高度最小的列，列内垂直堆叠
 * - 返回内容总高度（testOnly 时不真正 setGeometry） */
int MasonryLayout::doMasonry(const QRect &rect, bool testOnly) const
{
    if (itemList.isEmpty())
        return 0;

    // 卡片宽：取首个条目 sizeHint 宽度（卡片 setFixedWidth 固定）
    const int cardW = qMax(itemList.first()->sizeHint().width(), 80);
    const int hSpace = (m_hSpace >= 0) ? m_hSpace : horizontalSpacing();
    const int vSpace = (m_vSpace >= 0) ? m_vSpace : verticalSpacing();

    const int availW = rect.width();
    const int cols = qMax(1, (availW + hSpace) / (cardW + hSpace));

    QVector<int> colY(cols, 0);   // 每列累计高度
    int totalH = 0;

    for (QLayoutItem *item : itemList) {
        QSize hint = item->sizeHint();
        const int w = (hint.width() > 0) ? hint.width() : cardW;
        const int h = (hint.height() > 0) ? hint.height() : 120;

        // 最短列优先
        int minCol = 0;
        for (int c = 1; c < cols; ++c) {
            if (colY.at(c) < colY.at(minCol))
                minCol = c;
        }

        const QRect geom(rect.left() + minCol * (cardW + hSpace),
                         rect.top() + colY.at(minCol),
                         w, h);
        if (!testOnly)
            item->setGeometry(geom);

        colY[minCol] += h + vSpace;
        totalH = qMax(totalH, colY.at(minCol) - vSpace);
    }

    return totalH;
}

void MasonryLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doMasonry(rect, false);
}

QSize MasonryLayout::sizeHint() const
{
    return minimumSize();
}

QSize MasonryLayout::minimumSize() const
{
    const int cardW = itemList.isEmpty() ? 260 : qMax(itemList.first()->sizeHint().width(), 80);
    Q_UNUSED(m_hSpace);
    return QSize(cardW, 0);
}

int MasonryLayout::heightForWidth(int width) const
{
    return doMasonry(QRect(0, 0, width, 0), true);
}
