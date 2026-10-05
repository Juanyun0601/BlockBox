#ifndef FLOWLAYOUT_H
#define FLOWLAYOUT_H

#include <QLayout>
#include <QRect>
#include <QStyle>

/**
 * @brief 流式布局：子控件按可用宽度自动换行排列
 *
 * 用于预设色板等"固定大小小控件成组"的场景。与固定单行 QHBoxLayout 不同，
 * FlowLayout 依据父控件实际宽度在运行时折行，使所在容器没有横向的最小宽度
 * 压力——窗口多宽就排多宽，窄窗口下自动折成多行，保证页面整体始终宽度自适应。
 */
class FlowLayout : public QLayout
{
    Q_OBJECT

public:
    explicit FlowLayout(QWidget *parent = nullptr,
                        int horizontalSpacing = 6,
                        int verticalSpacing = 6);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    static int smartSpacing(QStyle::PixelMetric metric, const QLayout *layout);

    QList<QLayoutItem *> m_items;
    int m_hSpace;
    int m_vSpace;
};

#endif // FLOWLAYOUT_H
