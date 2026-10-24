/**
 * @file   PageFilter.h
 * @brief  子导航搜索框 → 页面内容过滤的共享工具
 * @author BlockBox Team
 * @date   2026-10-24
 *
 * 由 SubNavPanel 顶部搜索框驱动，经 MainWindow 分发 Q_INVOKABLE
 * applyPageFilter(QString) 到当前页。两类复用设施：
 *
 *  - PageFilter::VisibilityFilter：可见性记账 —— 只恢复「自己隐藏过」的控件，
 *    查询清空时不会把页面因状态切换（单选联动、空态等）主动隐藏的控件误显示。
 *  - PageFilter::PanelFilter：设置类面板（卡片 + 设置行结构）的行级过滤，
 *    SettingsPage / InstanceGameSettingsPage 等复用。
 */
#ifndef PAGEFILTER_H
#define PAGEFILTER_H

#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

#include "components/CollapsibleSectionCard.h"

namespace PageFilter {

/** 收集控件文本（含自身）与子树内所有可匹配文本（标签、按钮、列表项） */
inline QString collectText(const QWidget *w)
{
    if (!w)
        return QString();

    QString text;
    // 自身是文本控件时（如分区标题 QLabel）也要计入
    if (const auto *selfLabel = qobject_cast<const QLabel *>(w))
        text += selfLabel->text() + QStringLiteral(" ");
    else if (const auto *selfBtn = qobject_cast<const QPushButton *>(w))
        text += selfBtn->text() + QStringLiteral(" ");

    const auto labels = w->findChildren<QLabel *>();
    for (const QLabel *l : labels)
        text += l->text() + QStringLiteral(" ");

    const auto buttons = w->findChildren<QPushButton *>();
    for (const QPushButton *b : buttons)
        text += b->text() + QStringLiteral(" ");

    if (const auto *lw = qobject_cast<const QListWidget *>(w))
    {
        for (int i = 0; i < lw->count(); ++i)
        {
            const QListWidgetItem *item = lw->item(i);
            if (item)
                text += item->text() + QStringLiteral(" ");
        }
    }
    return text;
}

/** 大小写不敏感包含匹配；查询为空视为命中 */
inline bool matches(const QString &text, const QString &query)
{
    return query.isEmpty() || text.contains(query, Qt::CaseInsensitive);
}

/**
 * 可见性记账：setShow 按过滤结果设置可见性，clear 只恢复自己隐藏过的控件。
 * 页面主动隐藏（isHidden）的控件不接管，避免清空查询时误显示。
 * 用 QPointer 记账：列表页重建卡片（delete 旧控件）后不残留悬空指针。
 */
class VisibilityFilter
{
public:
    void setShow(QWidget *w, bool show)
    {
        if (!w)
            return;
        if (show)
        {
            if (removeTracked(w))
                w->setVisible(true);
        }
        else if (!w->isHidden())
        {
            m_hidden.append(w);
            w->setVisible(false);
        }
    }

    /** 恢复本过滤器隐藏过的全部控件（已销毁的条目顺带清除） */
    void clear()
    {
        for (const QPointer<QWidget> &w : std::as_const(m_hidden))
        {
            if (w)
                w->setVisible(true);
        }
        m_hidden.clear();
    }

    /** 本过滤器当前隐藏的控件数（卡片页用于判断是否需要空态） */
    int hiddenCount() const
    {
        int n = 0;
        for (const QPointer<QWidget> &w : m_hidden)
        {
            if (w)
                ++n;
        }
        return n;
    }

private:
    bool removeTracked(QWidget *w)
    {
        bool found = false;
        for (int i = m_hidden.size() - 1; i >= 0; --i)
        {
            if (m_hidden.at(i).data() == w)
            {
                m_hidden.removeAt(i);
                found = true;
            }
            else if (!m_hidden.at(i))
            {
                m_hidden.removeAt(i); // 顺带清理已销毁条目
            }
        }
        return found;
    }

    QList<QPointer<QWidget>> m_hidden;
};

/**
 * 设置类面板过滤器：对 panel 布局的直接子控件按关键词过滤。
 *  - CollapsibleSectionCard：行级过滤；卡片标题命中则整卡显示。
 *  - QListWidget：按条目过滤（恢复时一并恢复条目）。
 *  - 其它控件：按子树文本整体显示/隐藏。
 * 查询置空时只恢复本过滤器隐藏过的控件/条目。
 */
class PanelFilter
{
public:
    /** 对 panel 应用查询（空查询 = 恢复本过滤器的隐藏） */
    void apply(QWidget *panel, const QString &query)
    {
        if (!panel)
            return;
        if (query.isEmpty())
        {
            clear();
            return;
        }
        auto *lay = panel->layout();
        if (!lay)
            return;

        for (int i = 0; i < lay->count(); ++i)
        {
            QLayoutItem *item = lay->itemAt(i);
            if (!item)
                continue;
            QWidget *w = item->widget();
            if (!w)
                continue;

            if (auto *card = qobject_cast<CollapsibleSectionCard *>(w))
            {
                applyToCard(card, query);
            }
            else if (auto *lw = qobject_cast<QListWidget *>(w))
            {
                applyToList(lw, query);
            }
            else
            {
                m_vis.setShow(w, matches(collectText(w), query));
            }
        }
    }

    /** 恢复本过滤器隐藏过的全部控件/列表项 */
    void clear()
    {
        m_vis.clear();
        for (const QPointer<QListWidget> &lw : std::as_const(m_touchedLists))
        {
            if (!lw)
                continue;
            for (int i = 0; i < lw->count(); ++i)
            {
                if (QListWidgetItem *item = lw->item(i))
                    item->setHidden(false);
            }
        }
        m_touchedLists.clear();
    }

private:
    void applyToCard(CollapsibleSectionCard *card, const QString &query)
    {
        const bool titleMatch = card->title().contains(query, Qt::CaseInsensitive);
        QVBoxLayout *contentLayout = card->contentLayout();
        int rowCount = 0;
        bool anyVisible = false;

        if (contentLayout)
        {
            for (int i = 0; i < contentLayout->count(); ++i)
            {
                QLayoutItem *item = contentLayout->itemAt(i);
                if (!item)
                    continue;
                QWidget *row = item->widget();
                if (!row)
                    continue;
                ++rowCount;
                const bool rowMatch = titleMatch || matches(collectText(row), query);
                m_vis.setShow(row, rowMatch);
                anyVisible = anyVisible || rowMatch;
            }
        }

        const bool cardVisible = rowCount > 0 ? anyVisible : matches(collectText(card), query);
        m_vis.setShow(card, cardVisible);
    }

    void applyToList(QListWidget *lw, const QString &query)
    {
        bool anyVisible = true;
        if (lw->count() > 0)
        {
            anyVisible = false;
            for (int i = 0; i < lw->count(); ++i)
            {
                QListWidgetItem *item = lw->item(i);
                if (!item)
                    continue;
                const bool itemMatch = matches(item->text(), query);
                item->setHidden(!itemMatch);
                anyVisible = anyVisible || itemMatch;
            }
            m_touchedLists.append(lw);
        }
        m_vis.setShow(lw, anyVisible);
    }

    VisibilityFilter m_vis;
    QList<QPointer<QListWidget>> m_touchedLists;
};

} // namespace PageFilter

#endif // PAGEFILTER_H
