/**
 * @file   SideBar.h
 * @brief  侧边栏组件类声明 - 手风琴式层级导航
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef SIDEBAR_H
#define SIDEBAR_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QIcon>
#include <QColor>
#include <QHash>
#include <QVariant>
#include <QVector>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <QSequentialAnimationGroup>

/**
 * @brief 导航项数据结构，支持层级嵌套
 */
struct NavItem
{
    QString text;                   // 显示文本
    QString iconPath;               // 图标路径（SVG 资源或图片文件）
    bool iconIsSvg = true;          // 图标是否为 SVG（false 时按普通图片加载，不染色）
    bool isSectionLabel = false;    // 是否为分组标签
    bool isParentItem = false;      // 是否为主导航项（顶级）
    bool hideInSidebar = false;     // 是否在侧边栏中隐藏（仅数据，不渲染按钮）
    QVariant data;                  // 附加数据（如收藏夹分组的 ID）
    QVector<NavItem> children;      // 子导航项列表
};

class SideBar : public QWidget
{
    Q_OBJECT

public:
    explicit SideBar(QWidget *parent = nullptr);
    ~SideBar();

    void setSelectedIndex(int index);
    void clearSelection();
    void setNavIconColor(const QColor &color);

    /** 设置版本模式（true=基岩版），重建导航结构（资源子导航内容随版本切换） */
    void setBedrockMode(bool bedrock);
    /** 当前是否为基岩版模式 */
    bool isBedrockMode() const { return m_bedrockMode; }

    /** 获取主导航数据 */
    const QVector<NavItem>& parentItems() const { return m_parentItems; }

    /** 重建导航数据并重新渲染（收藏夹列表变化时调用） */
    void refreshNavStructure();

    void animateEntrance();

    /** 子导航面板收起后，在侧边栏底部显示/隐藏"展开"按钮 */
    void setExpandButtonVisible(bool visible);

protected:
    void showEvent(QShowEvent *event) override;

public slots:
    void onButtonClicked();

signals:
    /** 父导航项被点击（无子项时直接导航，有子项时展开/收起） */
    void parentItemClicked(int parentIndex);
    /** 子导航项被点击 */
    void childItemClicked(int parentIndex, int childIndex);
    /** "展开"按钮被点击（用于重新展开已收起的子导航面板） */
    void expandRequested();

private:
    void initUI();
    void buildNavStructure();
    void renderParentItems();

    void refreshNavIcons();
    QIcon loadColoredIcon(const QString &path, const QColor &color) const;

    QVBoxLayout *m_mainLayout;
    QScrollArea *m_scrollArea;
    QWidget *m_scrollContent;
    QVBoxLayout *m_scrollLayout;

    /** 子导航收起后用于重新展开的按钮 */
    QPushButton *m_expandBtn;

    /** 主导航数据 */
    QVector<NavItem> m_parentItems;
    /** 主导航按钮列表 */
    QList<QPushButton *> m_parentButtons;
    /** 主导航按钮图标标签（垂直布局下与按钮一一对应） */
    QList<QLabel *> m_parentIconLabels;
    /** 主导航按钮文字标签（垂直布局下与按钮一一对应） */
    QList<QLabel *> m_parentTextLabels;

    /** 当前选中的父项索引 */
    int m_selectedParentIndex;
    /** 当前版本模式（true=基岩版） */
    bool m_bedrockMode = false;

    QColor m_iconNormalColor;
    QColor m_iconSelectedColor;

    /** 滑动高亮背景 */
    QWidget *m_selectionHighlight;
    void initSelectionHighlight();
    void slideHighlightTo(int buttonIndex);
    QRect indicatorRect(QPushButton *btn) const;
};

#endif // SIDEBAR_H