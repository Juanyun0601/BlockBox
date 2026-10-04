/**
 * @file   CollapsibleSectionCard.h
 * @brief  可折叠设置分区卡片
 * @author BlockBox Team
 * @date   2026-08-18
 *
 * 卡片顶部为可点击的标题栏（左侧带展开/折叠箭头），点击标题栏即可
 * 切换下方内容区的展开/收起。样式与设置页卡片（QFrame#settingsCard）一致，
 * 因此可直接用于设置页、实例游戏设置页、投影编辑页等场景。
 */
#ifndef COLLAPSIBLESECTIONCARD_H
#define COLLAPSIBLESECTIONCARD_H

#include <QFrame>

class QPushButton;
class QVBoxLayout;
class QWidget;

/**
 * @brief 可折叠的设置分区卡片
 */
class CollapsibleSectionCard : public QFrame
{
    Q_OBJECT

public:
    explicit CollapsibleSectionCard(const QString &title,
                                    bool collapsed = false,
                                    QWidget *parent = nullptr);

    /** 内容区布局，供调用方添加设置行 */
    QVBoxLayout *contentLayout() const;

    void setTitle(const QString &title);
    QString title() const;

    bool isCollapsed() const;

    /** 展开/收起（true=展开） */
    void setCollapsed(bool collapsed);

signals:
    /** 展开状态变化（true=展开，false=收起） */
    void expandedChanged(bool expanded);

private:
    void updateArrowIcon(bool expanded);

    QPushButton *m_headerBtn;
    QWidget *m_content;
    QVBoxLayout *m_contentLayout;
};

#endif // COLLAPSIBLESECTIONCARD_H
