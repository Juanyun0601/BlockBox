/**
 * @file   ForumThreadListPage.h
 * @brief  论坛主题列表页：板块 / 搜索结果 / 标签 三种模式
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 同一个列表 UI 服务三个入口：
 *   openBoard(slug, name) —— 板块下的主题，带排序（最新回复/最新发布/最热）和「发帖」按钮
 *   openSearch(query)     —— 站内搜索结果，带可回车的搜索框
 *   openTag(tag)          —— 按标签筛选全站主题
 * 帖子行点击后发 threadOpenRequested，由主窗口切到帖子详情页。
 */
#pragma once

#include <QWidget>

#include "components/ForumKit.h"
#include "utils/ForumContentService.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class ForumThreadListPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumThreadListPage(QWidget *parent = nullptr);

    void openBoard(const QString &slug, const QString &boardName);
    void openSearch(const QString &query);
    void openTag(const QString &tag);

signals:
    /** 点击某个主题行 */
    void threadOpenRequested(int threadId);

    /** 板块模式下点「发帖」，boardSlug 为预选板块 */
    void composeRequested(const QString &boardSlug);

    /** 搜索结果里点击的作者 / 板块等（预留） */
    void boardOpenRequested(const QString &slug, const QString &boardName);

private:
    enum Mode
    {
        BoardMode = 0,
        SearchMode = 1,
        TagMode = 2
    };

    void buildUi();
    void loadPage(int page);
    void renderHeader();
    void renderThreads(const QVector<ForumThreadInfo> &threads);
    void setSort(const QString &sort);
    void setStatus(const QString &text, bool error = false);

    Mode m_mode = BoardMode;
    QString m_boardSlug;
    QString m_boardName;
    QString m_query;
    QString m_tag;
    QString m_sort = QStringLiteral("latest");
    int m_page = 1;
    int m_totalPages = 1;

    // UI
    QScrollArea *m_scroll = nullptr;
    QFrame *m_headerCard = nullptr;
    QVBoxLayout *m_headerBody = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    ForumKit::ForumPager *m_pager = nullptr;
    QPushButton *m_composeButton = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QVector<QPushButton *> m_sortButtons;
};
