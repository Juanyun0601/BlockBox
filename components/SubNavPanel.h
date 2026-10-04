/**
 * @file   SubNavPanel.h
 * @brief  子导航卡片组面板组件声明
 * @author BlockBox Team
 * @date   2026-06-18
 */
#ifndef SUBNAVPANEL_H
#define SUBNAVPANEL_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QIcon>
#include <QColor>
#include <QList>
#include <QPropertyAnimation>

#include "components/SideBar.h"

class SubNavPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SubNavPanel(QWidget *parent = nullptr);
    ~SubNavPanel();

    /** 设置关联的侧边栏，用于获取导航数据 */
    void setSideBar(SideBar *sideBar);
    /** 显示指定父项的子导航卡片组 */
    void showForParent(int parentIndex, bool selectFirst = true);
    /** 隐藏面板 */
    void hidePanel();
    /** 设置当前选中的子项索引（同步按钮 checked、图标与高亮），childIndex=-1 表示清除选中 */
    void setSelectedChild(int childIndex);
    /** 获取当前展开的父项索引，未展开时返回 -1 */
    int currentParentIndex() const { return m_currentParentIndex; }

    /** 悬浮气泡面板的固定宽度 */
    static constexpr int kWidth = 240;
    /** 面板与父容器边缘 / 侧边栏之间的间距 */
    static constexpr int kMargin = 10;

    /** 重新计算并按侧边栏贴靠悬浮几何（父容器尺寸变化时调用） */
    void updateFloatGeometry();

signals:
    /** 子导航项被点击 */
    void childItemClicked(int parentIndex, int childIndex);
    /** 面板可见性变化（展开/收起）；true=气泡显示，false=气泡隐藏 */
    void panelVisibilityChanged(bool visible);

private:
    void clearContent();
    QIcon loadColoredIcon(const QString &path, const QColor &color) const;
    /** 按图标类型加载：SVG 染色，普通图片原样缩放 */
    QIcon loadIcon(const QString &path, bool isSvg, const QColor &color) const;

    QVBoxLayout *m_mainLayout;
    QWidget *m_header;
    QLabel *m_headerTitle;
    QPushButton *m_collapseBtn;
    QScrollArea *m_scrollArea;
    QWidget *m_scrollContent;
    QVBoxLayout *m_contentLayout;

    QList<QPushButton *> m_buttons;
    QList<QLabel *> m_sectionLabels;

    SideBar *m_sideBar;
    int m_currentParentIndex;
    int m_currentChildIndex;
    int m_highlightRetryCount = 0; // slideHighlightTo 的延迟重试计数，防止无限递归
    QColor m_iconNormalColor;
    QColor m_iconSelectedColor;

    QWidget *m_selectionHighlight;
    void slideHighlightTo(int childIndex);
    QPushButton *findButtonByChildIndex(int childIndex) const;
};

#endif // SUBNAVPANEL_H