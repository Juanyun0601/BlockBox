/**
 * @file   CreationCenterPage.cpp
 * @brief  创作中心实现
 * @author BlockBox Team
 * @date   2026-11-14
 */

#include "CreationCenterPage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/ForumClaimDialog.h"
#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/StyleKit.h"

CreationCenterPage::CreationCenterPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void CreationCenterPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_viewStack = new QStackedWidget(this);

    // ── 作品列表页 ──
    m_listPage = new QWidget(m_viewStack);
    auto *listLayout = new QVBoxLayout(m_listPage);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    m_scroll = new QScrollArea(m_listPage);
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
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        loadAllWorks();
        loadClaims();
    });
    statusRow->addWidget(m_refreshButton);
    layout->addLayout(statusRow);

    // ── 作品数据汇总卡（进入时作品一次拉齐，四个数字就是全部作品的真实统计） ──
    auto *summaryCard = ForumKit::makeCard(content, QStringLiteral("forumHeaderCard"));
    auto *summaryLayout = new QVBoxLayout(summaryCard);
    summaryLayout->setContentsMargins(20, 16, 20, 16);
    summaryLayout->setSpacing(10);

    auto *summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(8);
    const auto addStat = [summaryCard, summaryRow](QLabel **target, const QString &caption) {
        auto *box = new QFrame(summaryCard);
        box->setObjectName(QStringLiteral("forumStatBox"));
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(14, 10, 14, 10);
        boxLayout->setSpacing(0);
        auto *value = new QLabel(QStringLiteral("0"), box);
        value->setObjectName(QStringLiteral("forumStatValue"));
        boxLayout->addWidget(value);
        auto *captionLabel = new QLabel(caption, box);
        captionLabel->setObjectName(QStringLiteral("forumRailKey"));
        boxLayout->addWidget(captionLabel);
        *target = value;
        summaryRow->addWidget(box);
    };
    addStat(&m_statWorks, tr("作品总数"));
    addStat(&m_statViews, tr("总浏览"));
    addStat(&m_statReplies, tr("总回复"));
    addStat(&m_statDownloads, tr("总下载"));
    summaryRow->addStretch(1);
    summaryLayout->addLayout(summaryRow);
    layout->addWidget(summaryCard);

    // ── 认领请求卡：有人认领我转载的作品时出现，同意后作品直接转给对方 ──
    m_claimsCard = ForumKit::makeCard(content, QStringLiteral("forumCard"));
    auto *claimsCardLayout = new QVBoxLayout(m_claimsCard);
    claimsCardLayout->setContentsMargins(20, 16, 20, 16);
    claimsCardLayout->setSpacing(8);
    m_claimsTitle = new QLabel(tr("认领请求"), m_claimsCard);
    m_claimsTitle->setObjectName(QStringLiteral("forumPluginSectionTitle"));
    claimsCardLayout->addWidget(m_claimsTitle);
    auto *claimsHint = ForumKit::makeMuted(
        m_claimsCard,
        tr("有人认领你转载的作品：先和对方沟通确认，同意后作品会转到对方名下，转载标记也会去掉"));
    claimsHint->setWordWrap(true);
    claimsCardLayout->addWidget(claimsHint);
    m_claimsLayout = new QVBoxLayout();
    m_claimsLayout->setContentsMargins(0, 0, 0, 0);
    m_claimsLayout->setSpacing(6);
    claimsCardLayout->addLayout(m_claimsLayout);
    m_claimsCard->hide();
    layout->addWidget(m_claimsCard);

    // ── 我的作品标题 + 搜索框 ──
    auto *listHeaderRow = new QHBoxLayout();
    listHeaderRow->setSpacing(10);
    listHeaderRow->addWidget(ForumKit::makeSectionTitle(content, tr("我的作品")));
    listHeaderRow->addStretch(1);
    m_searchEdit = new QLineEdit(content);
    m_searchEdit->setObjectName(QStringLiteral("forumSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("搜标题或标签"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(180);
    m_searchEdit->setFixedHeight(30);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_keyword = text.trimmed();
        m_page = 1;
        applyFilter();
        renderCurrentPage();
    });
    listHeaderRow->addWidget(m_searchEdit);
    layout->addLayout(listHeaderRow);

    // ── 类型筛选胶囊 + 数量 ──
    m_filterRow = new QWidget(content);
    auto *filterRowLayout = new QHBoxLayout(m_filterRow);
    filterRowLayout->setContentsMargins(0, 0, 0, 0);
    filterRowLayout->setSpacing(6);
    const auto addChip = [this, filterRowLayout](WorkFilter filter, const QString &label) {
        auto *chip = new QPushButton(label, m_filterRow);
        chip->setObjectName(QStringLiteral("forumFilterChip"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setCheckable(true);
        chip->setFixedHeight(26);
        chip->setChecked(filter == WorkFilter::All);
        connect(chip, &QPushButton::clicked, this, [this, chip, filter]() {
            chip->setChecked(true);   // 胶囊不是自由切换的开关，选中态统一由 setFilter 管
            setFilter(filter);
        });
        m_filterChips[static_cast<int>(filter)] = chip;
        filterRowLayout->addWidget(chip);
    };
    addChip(WorkFilter::All, tr("全部"));
    addChip(WorkFilter::Plugin, tr("插件"));
    addChip(WorkFilter::Schematic, tr("投影"));
    addChip(WorkFilter::Plain, tr("普通帖"));
    filterRowLayout->addStretch(1);
    m_countLabel = ForumKit::makeMuted(m_filterRow);
    filterRowLayout->addWidget(m_countLabel);
    layout->addWidget(m_filterRow);

    // ── 列表 ──
    m_listLayout = new QVBoxLayout();
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(8);
    layout->addLayout(m_listLayout);

    // ── 分页（拉齐作品后翻页在本地完成） ──
    m_pager = new ForumKit::ForumPager(content);
    connect(m_pager, &ForumKit::ForumPager::pageRequested, this, [this](int page) {
        m_page = page;
        renderCurrentPage();
    });
    layout->addWidget(m_pager);

    layout->addStretch(1);
    m_scroll->setWidget(content);
    listLayout->addWidget(m_scroll);
    m_viewStack->addWidget(m_listPage);

    // ── 作品详情子页（管理视角：数据 / 版本 / 编辑 / 删除） ──
    m_workDetail = new CreationWorkDetailPage(m_viewStack);
    m_viewStack->addWidget(m_workDetail);
    connect(m_workDetail, &CreationWorkDetailPage::backRequested, this,
            [this]() { showWorkList(); });
    connect(m_workDetail, &CreationWorkDetailPage::threadOpenRequested, this,
            &CreationCenterPage::threadOpenRequested);
    connect(m_workDetail, &CreationWorkDetailPage::workChanged, this,
            [this]() { loadAllWorks(); });
    connect(m_workDetail, &CreationWorkDetailPage::workDeleted, this, [this]() {
        showWorkList();
        m_page = 1;
        loadAllWorks();
    });

    outer->addWidget(m_viewStack);
}

void CreationCenterPage::showWorkList()
{
    if (m_viewStack)
        m_viewStack->setCurrentWidget(m_listPage);
}

void CreationCenterPage::openWork(const ForumThreadInfo &thread)
{
    if (!m_workDetail || !thread.isValid())
        return;
    m_workDetail->openWork(thread);
    m_viewStack->setCurrentWidget(m_workDetail);
}

bool CreationCenterPage::returnToWorkList()
{
    if (m_viewStack && m_workDetail && m_viewStack->currentWidget() == m_workDetail)
    {
        showWorkList();
        return true;
    }
    return false;
}

void CreationCenterPage::open()
{
    // 重新进入创作中心时回到作品列表第一页
    showWorkList();
    ForumKit::clearLayout(m_listLayout);
    m_allWorks.clear();
    m_filtered.clear();
    m_filter = WorkFilter::All;
    m_keyword.clear();
    if (m_searchEdit)
    {
        // 静默清空：不触发 textChanged 的重筛重渲染，状态在下面统一重置
        m_searchEdit->blockSignals(true);
        m_searchEdit->clear();
        m_searchEdit->blockSignals(false);
    }
    m_page = 1;
    m_totalPages = 1;
    m_partial = false;
    m_pager->setPage(1, 1);
    updateSummary();
    updateFilterChips();
    m_claims.clear();
    renderClaims(m_claims);

    const bool loggedIn = ForumAccountService::instance()->isLoggedIn();
    m_filterRow->setVisible(loggedIn);
    m_searchEdit->setVisible(loggedIn);

    if (!loggedIn)
    {
        auto *hint = ForumKit::makeMuted(this, tr("登录方块盒子账号后，这里会显示你发布的作品"));
        hint->setAlignment(Qt::AlignCenter);
        m_listLayout->addSpacing(24);
        m_listLayout->addWidget(hint);
        m_listLayout->addSpacing(24);
        return;
    }

    loadClaims();
    loadAllWorks();
}

/** 进入时把作品一次拉齐：先拉第一页拿到总页数，再按页继续连拉（上限内） */
void CreationCenterPage::loadAllWorks()
{
    const int userId = ForumAccountService::instance()->user().id;
    if (userId <= 0 || m_loading)
        return;

    m_loading = true;
    m_refreshButton->setEnabled(false);
    m_allWorks.clear();
    m_partial = false;
    setStatus(tr("正在加载作品…"));
    fetchPage(userId, 1);
}

void CreationCenterPage::fetchPage(int userId, int page)
{
    ForumContentService::instance()->fetchUserThreads(
        userId, page,
        [this, userId, page](const QVector<ForumThreadInfo> &threads,
                             const ForumPagination &pagination) {
            for (const ForumThreadInfo &thread : threads)
                if (thread.isValid())
                    m_allWorks.append(thread);

            const int lastPage = qMin(qMax(1, pagination.pages), kMaxAutoPages);
            m_partial = m_partial || pagination.pages > kMaxAutoPages;
            if (page < lastPage)
            {
                setStatus(tr("正在加载作品（第 %1 / %2 页）…").arg(page + 1).arg(lastPage));
                fetchPage(userId, page + 1);
                return;
            }
            finishLoad();
        },
        [this](const QString &message, int) {
            if (m_allWorks.isEmpty())
            {
                m_loading = false;
                m_refreshButton->setEnabled(true);
                setStatus(tr("加载失败：%1").arg(message), true);
                ForumKit::clearLayout(m_listLayout);
                m_pager->setPage(1, 1);
                return;
            }
            // 中途某页挂了：就用已经拉到的部分继续展示
            m_partial = true;
            finishLoad();
            setStatus(tr("部分作品加载失败：%1").arg(message), true);
        });
}

void CreationCenterPage::finishLoad()
{
    m_loading = false;
    m_refreshButton->setEnabled(true);
    setStatus(QString());
    updateSummary();
    updateFilterChips();
    applyFilter();
    renderCurrentPage();
}

bool CreationCenterPage::matchesFilter(const ForumThreadInfo &thread, WorkFilter filter)
{
    switch (filter)
    {
    case WorkFilter::Plugin:
        return thread.plugin.isValid();
    case WorkFilter::Schematic:
        return thread.schematic.isValid();
    case WorkFilter::Plain:
        return !thread.plugin.isValid() && !thread.schematic.isValid();
    case WorkFilter::All:
    default:
        return true;
    }
}

/** 按当前类型胶囊 + 关键词重筛一遍，并同步胶囊上的数量 */
void CreationCenterPage::applyFilter()
{
    m_filtered.clear();
    for (const ForumThreadInfo &thread : m_allWorks)
    {
        if (!matchesFilter(thread, m_filter))
            continue;
        if (!m_keyword.isEmpty())
        {
            const bool hitTitle = thread.title.contains(m_keyword, Qt::CaseInsensitive);
            bool hitTag = false;
            for (const QString &tag : thread.tags)
                if (tag.contains(m_keyword, Qt::CaseInsensitive))
                {
                    hitTag = true;
                    break;
                }
            if (!hitTitle && !hitTag)
                continue;
        }
        m_filtered.append(thread);
    }
    if (m_countLabel)
        m_countLabel->setText(tr("共 %1 个作品").arg(m_filtered.size()));
}

void CreationCenterPage::setFilter(WorkFilter filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    for (int i = 0; i < 4; ++i)
        if (m_filterChips[i])
            m_filterChips[i]->setChecked(static_cast<WorkFilter>(i) == filter);
    m_page = 1;
    applyFilter();
    renderCurrentPage();
}

/** 胶囊文字带上各类型数量，比如「插件 3」 */
void CreationCenterPage::updateFilterChips()
{
    for (int i = 0; i < 4; ++i)
    {
        auto *chip = m_filterChips[i];
        if (!chip)
            continue;
        const WorkFilter filter = static_cast<WorkFilter>(i);
        int count = 0;
        for (const ForumThreadInfo &thread : m_allWorks)
            if (matchesFilter(thread, filter))
                ++count;
        const QString label = filter == WorkFilter::All      ? tr("全部 %1")
                              : filter == WorkFilter::Plugin ? tr("插件 %1")
                              : filter == WorkFilter::Schematic ? tr("投影 %1")
                                                                : tr("普通帖 %1");
        chip->setText(label.arg(count));
        chip->setChecked(filter == m_filter);
    }
}

void CreationCenterPage::renderCurrentPage()
{
    m_totalPages = qMax(1, (m_filtered.size() + kPageSize - 1) / kPageSize);
    m_page = qBound(1, m_page, m_totalPages);
    const QVector<ForumThreadInfo> pageWorks =
        m_filtered.mid((m_page - 1) * kPageSize, kPageSize);
    ForumKit::clearLayout(m_listLayout);
    m_pager->setPage(m_page, m_totalPages);

    if (pageWorks.isEmpty())
    {
        // 未登录只在 open() 与这里兜底显示提示
        if (!ForumAccountService::instance()->isLoggedIn())
        {
            auto *hint = ForumKit::makeMuted(this, tr("登录方块盒子账号后，这里会显示你发布的作品"));
            hint->setAlignment(Qt::AlignCenter);
            m_listLayout->addSpacing(24);
            m_listLayout->addWidget(hint);
            m_listLayout->addSpacing(24);
            return;
        }

        if (m_allWorks.isEmpty())
        {
            auto *icon = ForumKit::makeMuted(this, QStringLiteral("📦"));
            icon->setAlignment(Qt::AlignCenter);
            QFont iconFont = icon->font();
            iconFont.setPointSize(26);
            icon->setFont(iconFont);
            m_listLayout->addSpacing(18);
            m_listLayout->addWidget(icon);

            auto *hint = ForumKit::makeMuted(this, tr("还没有发布过作品，去发第一贴吧"));
            hint->setAlignment(Qt::AlignCenter);
            m_listLayout->addWidget(hint);

            auto *publishButton = new QPushButton(tr("去发布"), this);
            publishButton->setObjectName(QStringLiteral("forumPrimaryButton"));
            publishButton->setCursor(Qt::PointingHandCursor);
            publishButton->setFixedHeight(34);
            connect(publishButton, &QPushButton::clicked, this,
                    &CreationCenterPage::publishRequested);
            m_listLayout->addSpacing(10);
            m_listLayout->addWidget(publishButton, 0, Qt::AlignCenter);
        }
        else
        {
            auto *hint = ForumKit::makeMuted(this, tr("没有符合条件的作品，换个筛选或关键词试试"));
            hint->setAlignment(Qt::AlignCenter);
            m_listLayout->addSpacing(24);
            m_listLayout->addWidget(hint);

            auto *clearButton = new QPushButton(tr("清除筛选"), this);
            clearButton->setObjectName(QStringLiteral("forumGhostButton"));
            clearButton->setCursor(Qt::PointingHandCursor);
            clearButton->setFixedHeight(30);
            connect(clearButton, &QPushButton::clicked, this, [this]() {
                if (m_searchEdit)
                    m_searchEdit->clear();
                setFilter(WorkFilter::All);
            });
            m_listLayout->addSpacing(10);
            m_listLayout->addWidget(clearButton, 0, Qt::AlignCenter);
        }
        m_listLayout->addSpacing(24);
        return;
    }

    for (const ForumThreadInfo &thread : pageWorks)
    {
        auto *row = new ForumKit::ClickableCard(this);
        row->setObjectName(QStringLiteral("forumThreadRow"));
        row->onClick = [this, thread]() { openWork(thread); };

        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(14, 10, 14, 10);
        rowLayout->setSpacing(4);

        // 标题行：标题 + 徽标，操作按钮收在行尾（不再单独占一行，行高省一半）
        auto *titleRow = new QHBoxLayout();
        titleRow->setSpacing(8);
        auto *title = new ForumKit::ElidedLabel(row);
        title->setObjectName(QStringLiteral("forumRowTitle"));
        title->setFullText(thread.title);
        titleRow->addWidget(title, 1);
        if (thread.plugin.isValid())
            titleRow->addWidget(ForumKit::makeBadge(row, tr("插件")));
        if (thread.schematic.isValid())
            titleRow->addWidget(ForumKit::makeBadge(row, tr("投影")));
        if (thread.source.isRepost())
            titleRow->addWidget(ForumKit::makeBadge(row, tr("转载")));
        if (thread.pinned)
            titleRow->addWidget(ForumKit::makeBadge(row, tr("置顶")));
        if (thread.locked)
            titleRow->addWidget(ForumKit::makeBadge(row, tr("锁定"), true));

        auto *editButton = new QPushButton(tr("编辑"), row);
        editButton->setObjectName(QStringLiteral("forumGhostButton"));
        editButton->setCursor(Qt::PointingHandCursor);
        editButton->setFixedHeight(26);
        connect(editButton, &QPushButton::clicked, this, [this, thread]() { editWork(thread); });
        titleRow->addWidget(editButton);

        auto *openButton = new QPushButton(tr("详情"), row);
        openButton->setObjectName(QStringLiteral("forumGhostButton"));
        openButton->setCursor(Qt::PointingHandCursor);
        openButton->setFixedHeight(26);
        connect(openButton, &QPushButton::clicked, this, [this, thread]() { openWork(thread); });
        titleRow->addWidget(openButton);

        auto *deleteButton = new QPushButton(tr("删除"), row);
        deleteButton->setObjectName(QStringLiteral("dangerBtn"));
        deleteButton->setCursor(Qt::PointingHandCursor);
        deleteButton->setFixedHeight(26);
        connect(deleteButton, &QPushButton::clicked, this, [this, thread]() { deleteWork(thread); });
        titleRow->addWidget(deleteButton);
        rowLayout->addLayout(titleRow);

        // 信息行：插件 / 投影附加信息与数据合并成一条
        QStringList metaParts;
        if (thread.plugin.isValid())
        {
            if (!thread.plugin.version.isEmpty())
                metaParts << tr("版本 %1").arg(thread.plugin.version);
            metaParts << ForumKit::humanFileSize(thread.plugin.fileSize);
            metaParts << tr("已下载 %1 次").arg(thread.plugin.downloads);
        }
        else if (thread.schematic.isValid())
        {
            // 一帖多份（主世界 / 下界…）：先说清有几个版本
            if (thread.schematic.versionCount() > 1)
                metaParts << tr("%1 个版本").arg(thread.schematic.versionCount());
            metaParts << ForumKit::humanFileSize(thread.schematic.fileSize);
            metaParts << tr("已下载 %1 次").arg(thread.schematic.downloads);
        }
        if (!thread.boardName.isEmpty())
            metaParts << thread.boardName;
        metaParts << tr("浏览 %1").arg(thread.views);
        metaParts << tr("回复 %1").arg(thread.replies);
        if (thread.createdAt > 0)
            metaParts << tr("发布于 %1").arg(ForumKit::relativeTime(thread.createdAt));

        auto *meta = new ForumKit::ElidedLabel(row);
        meta->setObjectName(QStringLiteral("forumMuted"));
        meta->setFullText(metaParts.join(QStringLiteral(" · ")));
        rowLayout->addWidget(meta);

        // 标签
        if (!thread.tags.isEmpty())
        {
            auto *tagsRow = new QHBoxLayout();
            tagsRow->setSpacing(6);
            for (const QString &tag : thread.tags)
                tagsRow->addWidget(ForumKit::makeTagChip(row, tag));
            tagsRow->addStretch(1);
            rowLayout->addLayout(tagsRow);
        }

        m_listLayout->addWidget(row);
    }

    // 作品超出自动拉取上限时，在最后一页下方说明一下
    if (m_partial && m_page >= m_totalPages)
    {
        auto *note = ForumKit::makeMuted(
            this, tr("作品较多，这里只显示最近的 %1 个，更早的可以用筛选或搜索找").arg(m_allWorks.size()));
        note->setAlignment(Qt::AlignCenter);
        m_listLayout->addSpacing(6);
        m_listLayout->addWidget(note);
    }
}

void CreationCenterPage::editWork(const ForumThreadInfo &thread)
{
    if (thread.id <= 0)
        return;

    // 正文在首楼里：先拉详情拿首楼内容，再进编辑弹窗
    setStatus(tr("正在打开编辑…"));
    ForumContentService::instance()->fetchThread(
        thread.id, 1,
        [this, thread](const ForumThreadDetail &detail) {
            setStatus(QString());

            int firstPostId = 0;
            QString content;
            if (!detail.posts.isEmpty() && detail.posts.first().isValid())
            {
                firstPostId = detail.posts.first().id;
                content = detail.posts.first().content;
            }

            QString title;
            QStringList tags;
            QString newContent;
            if (!CreationWorkDetailPage::runEditDialog(this, thread.title, thread.tags, content,
                                                       &title, &tags, &newContent))
                return;

            const bool contentChanged = firstPostId > 0 && newContent != content;
            const bool metaChanged = title != thread.title || tags != thread.tags;
            if (!metaChanged && !contentChanged)
            {
                NotificationManager::showInfo(this, tr("内容没有变化"));
                return;
            }

            const auto finish = [this]() {
                NotificationManager::showSuccess(this, tr("作品已更新"));
                loadAllWorks();
            };
            const auto fail = [this](const QString &message, int) {
                NotificationManager::showError(this, message);
            };
            const auto saveMeta = [this, firstPostId, contentChanged, newContent, finish, fail]() {
                if (!contentChanged)
                {
                    finish();
                    return;
                }
                ForumContentService::instance()->editPost(firstPostId, newContent, finish, fail);
            };

            if (!metaChanged)
            {
                saveMeta();
                return;
            }
            // 这里只改标题/标签：把帖子上已绑的相关链接原样带回，避免被清空
            const QVector<ForumResourceLink> keepLinks =
                thread.plugin.isValid() ? thread.plugin.links : thread.schematic.links;
            ForumContentService::instance()->editThread(thread.id, title, tags, keepLinks, saveMeta, fail);
        },
        [this](const QString &message, int) {
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void CreationCenterPage::deleteWork(const ForumThreadInfo &thread)
{
    if (thread.id <= 0)
        return;

    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("删除作品"),
        tr("确定删除《%1》吗？所有内容都不再显示，此操作不可撤销。").arg(thread.title),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    ForumContentService::instance()->deleteThread(
        thread.id,
        [this]() {
            NotificationManager::showSuccess(this, tr("作品已删除"));
            loadAllWorks();
        },
        [this](const QString &message, int) {
            NotificationManager::showError(this, message);
        });
}

void CreationCenterPage::loadClaims()
{
    if (m_claimsLoading || !ForumAccountService::instance()->isLoggedIn())
        return;
    m_claimsLoading = true;
    ForumContentService::instance()->fetchClaims(
        true, 0, QStringLiteral("pending"), 1,
        [this](const QVector<ForumClaimInfo> &claims, const ForumPagination &) {
            m_claimsLoading = false;
            m_claims = claims;
            renderClaims(claims);
        },
        [this](const QString &, int) {
            m_claimsLoading = false;
            // 认领请求是附加信息，拉不到不挡住作品列表
        });
}

void CreationCenterPage::renderClaims(const QVector<ForumClaimInfo> &claims)
{
    if (!m_claimsCard || !m_claimsLayout)
        return;
    ForumKit::clearLayout(m_claimsLayout);
    if (claims.isEmpty())
    {
        m_claimsCard->hide();
        return;
    }

    m_claimsCard->show();
    m_claimsTitle->setText(tr("认领请求（%1）").arg(claims.size()));
    for (const ForumClaimInfo &claim : claims)
    {
        auto *row = new QFrame(m_claimsCard);
        row->setObjectName(QStringLiteral("forumThreadRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(14, 10, 14, 10);
        rowLayout->setSpacing(10);

        auto *textColumn = new QVBoxLayout();
        textColumn->setContentsMargins(0, 0, 0, 0);
        textColumn->setSpacing(2);
        auto *title = new ForumKit::ElidedLabel(row);
        title->setObjectName(QStringLiteral("forumRowTitle"));
        title->setFullText(tr("《%1》").arg(claim.threadTitle));
        textColumn->addWidget(title);

        const QString claimantName = claim.claimant.displayName.isEmpty()
                                         ? claim.claimant.username
                                         : claim.claimant.displayName;
        QStringList metaParts;
        metaParts << tr("认领人：%1").arg(claimantName);
        if (claim.createdAt > 0)
            metaParts << ForumKit::relativeTime(claim.createdAt);
        if (claim.messageCount > 0)
            metaParts << tr("%1 条沟通").arg(claim.messageCount);
        auto *meta = new ForumKit::ElidedLabel(row);
        meta->setObjectName(QStringLiteral("forumMuted"));
        meta->setFullText(metaParts.join(QStringLiteral(" · ")));
        textColumn->addWidget(meta);
        rowLayout->addLayout(textColumn, 1);

        auto *openButton = new QPushButton(tr("沟通并处理"), row);
        openButton->setObjectName(QStringLiteral("forumGhostButton"));
        openButton->setCursor(Qt::PointingHandCursor);
        openButton->setFixedHeight(26);
        const int claimId = claim.id;
        connect(openButton, &QPushButton::clicked, this, [this, claimId]() {
            ForumClaimChatDialog::showDialog(this, claimId, [this](bool approved) {
                loadClaims();
                if (approved)
                    loadAllWorks();
            });
        });
        rowLayout->addWidget(openButton);
        m_claimsLayout->addWidget(row);
    }
}

void CreationCenterPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}

/** 汇总卡：全部作品的浏览 / 回复 / 下载（插件与投影的下载都算） */
void CreationCenterPage::updateSummary()
{
    qint64 views = 0;
    qint64 replies = 0;
    qint64 downloads = 0;
    for (const ForumThreadInfo &thread : m_allWorks)
    {
        views += thread.views;
        replies += thread.replies;
        if (thread.plugin.isValid())
            downloads += thread.plugin.downloads;
        if (thread.schematic.isValid())
            downloads += thread.schematic.downloads;
    }

    m_statWorks->setText(QString::number(m_allWorks.size()));
    m_statViews->setText(QString::number(views));
    m_statReplies->setText(QString::number(replies));
    m_statDownloads->setText(QString::number(downloads));
}
