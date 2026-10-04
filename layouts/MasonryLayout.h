#ifndef MASONRYLAYOUT_H
#define MASONRYLAYOUT_H

#include "FlowLayout.h"

/* ============================================================
 * MasonryLayout — Pinterest 式瀑布流布局
 * 继承 FlowLayout 的条目管理，但布局算法改为「最短列优先」：
 *   每个卡片放入当前累计高度最小的列，列内连续堆叠，
 *   列间高度独立错落 → 真正的瀑布流视觉效果
 * 用法与 FlowLayout 完全一致（addWidget / takeAt 等继承），
 * 页面只需把 new FlowLayout(...) 换成 new MasonryLayout(...)
 * ============================================================ */
class MasonryLayout : public FlowLayout
{
public:
    explicit MasonryLayout(QWidget *parent = nullptr, int margin = 0,
                           int hSpacing = 12, int vSpacing = 12);
    ~MasonryLayout() override = default;

    void setGeometry(const QRect &rect) override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

private:
    int doMasonry(const QRect &rect, bool testOnly) const;
    int smartMargin() const;
};

#endif // MASONRYLAYOUT_H
