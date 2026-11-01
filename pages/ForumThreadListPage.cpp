/**
 * @file   ForumThreadListPage.cpp
 * @brief  论坛主题列表页实现（板块 / 搜索 / 标签）
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumThreadListPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "utils/StyleKit.h"

ForumThreadListPage::ForumThreadListPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void ForumThreadListPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(m_scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 18, 28, 28);
    layout->setSpacing(12);

    // ── 状态行 ──
    auto *statusRow = new QHBoxLayout();
    statusRow->setSpacing(10);
    m_statusLabel = new QLabel(content);
    m_statusLabel->setObjectName(QStringLiteral("forumStatus"));
    statusRow->addWidget(m_statusLabel);
    statusRow->addStretch(1);
    m_refreshButton = new QPushButton(tr("刷新"), content);
    m_refreshButton->setObjectName(QStringLiteral("forumGhostButton"));
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setFixedHeight(28);
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() { loadPage(m_page); });
    statusRow->addWidget(m_refreshButton);
    layout->addLayout(statusRow);

    // ── 头卡：标题 + 排序 / 搜索框 / 发帖 ──
    m_headerCard = ForumKit::makeCard(content, QStringLiteral("forumHeaderCard"));
    m_headerBody = new QVBoxLayout(m_headerCard);
    m_headerBody->setContentsMargins(20, 16, 20, 16);
    m_headerBody->setSpacing(10);
    m_headerBody->addWidget(ForumKit::makeMuted(m_headerCard, tr("从「社区」页选择板块或搜索")));
    layout->addWidget(m_headerCard);

    // ── 主题列表 ──
    m_listLayout = new QVBoxLayout();
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(8);
    layout->addLayout(m_listLayout);

    // ── 分页 ──
    m_pager = new ForumKit::ForumPager(content);
    connect(m_pager, &ForumKit::ForumPager::pageRequested, this,
            [this](int page) { loadPage(page); });
    layout->addWidget(m_pager);

    layout->addStretch(1);
    m_scroll->setWidget(content);
    outer->addWidget(m_scroll);
}

void ForumThreadListPage::openBoard(const QString &slug, const QString &boardName)
{
    m_mode = BoardMode;
    m_boardSlug = slug;
    m_boardName = boardName;
    m_page = 1;
    renderHeader();
    loadPage(1);
}

void ForumThreadListPage::openSearch(const QString &query)
{
    m_mode = SearchMode;
    m_query = query;
    m_page = 1;
    renderHeader();
    loadPage(1);
}

void ForumThreadListPage::openTag(const QString &tag)
{
    m_mode = TagMode;
    m_tag = tag;
    m_page = 1;
    renderHeader();
    loadPage(1);
}

void ForumThreadListPage::renderHeader()
{
    ForumKit::clearLayout(m_headerBody);
    m_sortButtons.clear();

    // 标题行
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    QString title;
    switch (m_mode)
    {
    case BoardMode:
        title = m_boardName.isEmpty() ? tr("板块") : m_boardName;
        break;
    case SearchMode:
        title = tr("搜索：%1").arg(m_query);
        break;
    case TagMode:
        title = tr("标签：%1").arg(m_tag);
        break;
    }
    auto *titleLabel = new QLabel(title, m_headerCard);
    titleLabel->setObjectName(QStringLiteral("forumThreadTitle"));
    titleLabel->setWordWrap(true);
    titleRow->addWidget(titleLabel, 1);

    if (m_mode == BoardMode)
    {
        m_composeButton = new QPushButton(tr("✏ 发帖"), m_headerCard);
        m_composeButton->setObjectName(QStringLiteral("forumPrimaryButton"));
        m_composeButton->setCursor(Qt::PointingHandCursor);
        m_composeButton->setFixedHeight(30);
        const QString slug = m_boardSlug;
        connect(m_composeButton, &QPushButton::clicked, this,
                [this, slug]() { emit composeRequested(slug); });
        titleRow->addWidget(m_composeButton);
    }
    m_headerBody->addLayout(titleRow);

    // 模式专属行
    if (m_mode == SearchMode)
    {
        auto *searchRow = new QHBoxLayout();
        searchRow->setSpacing(8);
        m_searchEdit = new QLineEdit(m_query, m_headerCard);
        m_searchEdit->setObjectName(QStringLiteral("forumSearchEdit"));
        m_searchEdit->setPlaceholderText(tr("换个关键词再搜"));
        m_searchEdit->setClearButtonEnabled(true);
        searchRow->addWidget(m_searchEdit, 1);
        auto *searchButton = new QPushButton(tr("搜索"), m_headerCard);
        searchButton->setObjectName(QStringLiteral("forumPrimaryButton"));
        searchButton->setCursor(Qt::PointingHandCursor);
        searchButton->setFixedHeight(30);
        connect(searchButton, &QPushButton::clicked, this, [this]() {
            const QString keyword = m_searchEdit->text().trimmed();
            if (keyword.isEmpty() || keyword == m_query)
                return;
            openSearch(keyword);
        });
        connect(m_searchEdit, &QLineEdit::returnPressed, searchButton, &QPushButton::click);
        searchRow->addWidget(searchButton);
        m_headerBody->addLayout(searchRow);
    }
    else if (m_mode == BoardMode || m_mode == TagMode)
    {
        // 排序：最新回复 / 最新发布 / 最热
        auto *sortRow = new QHBoxLayout();
        sortRow->setSpacing(6);
        sortRow->addWidget(ForumKit::makeMuted(m_headerCard, tr("排序")), 0);

        const QVector<QPair<QString, QString>> sorts = {
            {QStringLiteral("latest"), tr("最新回复")},
            {QStringLiteral("new"), tr("最新发布")},
            {QStringLiteral("hot"), tr("最热")},
        };
        for (const auto &item : sorts)
        {
            auto *button = new QPushButton(item.second, m_headerCard);
            button->setObjectName(QStringLiteral("forumSortButton"));
            button->setCheckable(true);
            button->setAutoExclusive(true);
            button->setChecked(item.first == m_sort);
            button->setCursor(Qt::PointingHandCursor);
            button->setFixedHeight(26);
            const QString sort = item.first;
            connect(button, &QPushButton::clicked, this, [this, sort]() { setSort(sort); });
            m_sortButtons.append(button);
            sortRow->addWidget(button);
        }
        sortRow->addStretch(1);
        m_headerBody->addLayout(sortRow);
    }
}

void ForumThreadListPage::setSort(const QString &sort)
{
    if (m_sort == sort)
        return;
    m_sort = sort;
    m_page = 1;
    loadPage(1);
}

void ForumThreadListPage::loadPage(int page)
{
    m_refreshButton->setEnabled(false);
    setStatus(tr("正在加载…"));

    auto failHandler = [this](const QString &message, int) {
        m_refreshButton->setEnabled(true);
        setStatus(tr("加载失败：%1").arg(message), true);
    };

    if (m_mode == BoardMode)
    {
        ForumContentService::instance()->fetchBoardThreads(
            m_boardSlug, m_sort, page,
            [this, page](const ForumBoard &board, const QVector<ForumThreadInfo> &threads,
                         const ForumPagination &pagination) {
                m_refreshButton->setEnabled(true);
                setStatus(QString());
                if (!board.name.isEmpty())
                    m_boardName = board.name;
                m_page = qMax(1, pagination.page);
                m_totalPages = qMax(1, pagination.pages);
                renderThreads(threads);
                m_pager->setPage(m_page, m_totalPages);
            },
            failHandler);
    }
    else if (m_mode == SearchMode)
    {
        ForumContentService::instance()->searchThreads(
            m_query, page,
            [this, page](const QString &, const QVector<ForumThreadInfo> &results,
                         const ForumPagination &pagination) {
                m_refreshButton->setEnabled(true);
                setStatus(QString());
                m_page = qMax(1, pagination.page);
                m_totalPages = qMax(1, pagination.pages);
                renderThreads(results);
                m_pager->setPage(m_page, m_totalPages);
            },
            failHandler);
    }
    else
    {
        ForumContentService::instance()->fetchTagThreads(
            m_tag, m_sort, page,
            [this, page](const QVector<ForumThreadInfo> &threads,
                         const ForumPagination &pagination) {
                m_refreshButton->setEnabled(true);
                setStatus(QString());
                m_page = qMax(1, pagination.page);
                m_totalPages = qMax(1, pagination.pages);
                renderThreads(threads);
                m_pager->setPage(m_page, m_totalPages);
            },
            failHandler);
    }
}

void ForumThreadListPage::renderThreads(const QVector<ForumThreadInfo> &threads)
{
    ForumKit::clearLayout(m_listLayout);

    if (threads.isEmpty())
    {
        const QString empty = m_mode == SearchMode ? tr("没有搜到相关主题，换个关键词试试")
                                                   : tr("这里还没有主题，来发第一帖吧");
        auto *emptyLabel = ForumKit::makeMuted(this, empty);
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_listLayout->addSpacing(24);
        m_listLayout->addWidget(emptyLabel);
        m_listLayout->addSpacing(24);
        return;
    }

    for (const ForumThreadInfo &thread : threads)
    {
        auto *row = new ForumKit::ClickableCard(this);
        row->setObjectName(QStringLiteral("forumThreadRow"));
        const int threadId = thread.id;
        row->onClick = [this, threadId]() { emit threadOpenRequested(threadId); };

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(14, 12, 14, 12);
        rowLayout->setSpacing(12);

        auto *textColumn = new QVBoxLayout();
        textColumn->setSpacing(4);

        // 标题行（含置顶 / 锁定徽章）
        auto *titleRow = new QHBoxLayout();
        titleRow->setSpacing(6);
        if (thread.pinned)
            titleRow->addWidget(ForumKit::makeBadge(row, tr("置顶")));
        if (thread.locked)
            titleRow->addWidget(ForumKit::makeBadge(row, tr("锁定"), true));

        auto *title = new ForumKit::ElidedLabel(row);
        title->setObjectName(QStringLiteral("forumRowTitle"));
        title->setFullText(thread.title);
        title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        titleRow->addWidget(title, 1);
        textColumn->addLayout(titleRow);

        // 命中片段（搜索结果）
        QString meta;
        if (!thread.snippet.isEmpty())
        {
            auto *snippet = ForumKit::makeMuted(row, thread.snippet);
            textColumn->addWidget(snippet);
        }

        // 元信息
        QStringList metaParts;
        if (m_mode != BoardMode && !thread.boardName.isEmpty())
            metaParts << thread.boardName;
        if (!thread.authorName.isEmpty())
            metaParts << thread.authorName;
        if (thread.lastPostAt > 0)
            metaParts << ForumKit::relativeTime(thread.lastPostAt);
        else if (thread.createdAt > 0)
            metaParts << ForumKit::relativeTime(thread.createdAt);
        meta = metaParts.join(QStringLiteral(" · "));
        auto *metaLabel = ForumKit::makeMuted(row, meta);
        textColumn->addWidget(metaLabel);

        rowLayout->addLayout(textColumn, 1);

        // 右侧统计
        auto *countsColumn = new QVBoxLayout();
        countsColumn->setSpacing(2);
        auto *replies = new QLabel(tr("%1 回复").arg(thread.replies), row);
        replies->setObjectName(QStringLiteral("forumRowCount"));
        replies->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *views = new QLabel(tr("%1 浏览").arg(thread.views), row);
        views->setObjectName(QStringLiteral("forumMuted"));
        views->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        countsColumn->addWidget(replies);
        countsColumn->addWidget(views);
        rowLayout->addLayout(countsColumn);

        m_listLayout->addWidget(row);
    }
}

void ForumThreadListPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}
