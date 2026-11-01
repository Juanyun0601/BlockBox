/**
 * @file   ForumThreadDetailPage.cpp
 * @brief  论坛帖子详情页实现（版面与网页版 thread.html 对齐）
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumThreadDetailPage.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTextCursor>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/NotificationManager.h"
#include "components/QuizDialog.h"
#include "utils/ForumAccountService.h"
#include "utils/StyleKit.h"

namespace {

/** 右栏宽度，和网页版 .thread-layout 的第二列一致 */
constexpr int kRailWidth = 300;

/** 放不下两栏就上下叠，网页版断点 1080px */
constexpr int kStackBelow = 1080;

/** 楼中楼先显示几条，其余的折叠起来 */
constexpr int kVisibleChildren = 6;

QString authorDisplay(const ForumUser &author)
{
    if (!author.displayName.isEmpty())
        return author.displayName;
    if (!author.username.isEmpty())
        return author.username;
    return QObject::tr("未知用户");
}

QString joinedText(const ForumUser &author)
{
    if (author.createdAt <= 0)
        return QString();
    return QObject::tr("注册于 %1")
        .arg(QDateTime::fromSecsSinceEpoch(author.createdAt).toString(QStringLiteral("yyyy-MM-dd")));
}

QString absoluteTime(qint64 seconds)
{
    if (seconds <= 0)
        return QString();
    return QDateTime::fromSecsSinceEpoch(seconds)
        .toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

/** 层间分隔用的「·」，和网页版 meta-dot 同色 */
QLabel *makeDot(QWidget *parent)
{
    auto *dot = new QLabel(QStringLiteral("·"), parent);
    dot->setObjectName(QStringLiteral("forumMetaDot"));
    dot->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    return dot;
}

/** 胶囊操作按钮（回复 / 引用 / 点赞 / 编辑…） */
QPushButton *makeAct(QWidget *parent, const QString &text, bool danger = false)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("forumAct"));
    button->setProperty("danger", danger);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    button->setFixedHeight(28);
    return button;
}

} // namespace

ForumThreadDetailPage::ForumThreadDetailPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            [this]() {
                updateReplyBoxState();
                renderHero();
            });
    connect(ForumAccountService::instance(), &ForumAccountService::sessionExpired, this,
            [this]() {
                updateReplyBoxState();
                renderHero();
            });
}

// ────────────────────────────────────────────────────────────── 骨架

void ForumThreadDetailPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    m_content = new QWidget(m_scroll);
    m_content->setAttribute(Qt::WA_StyledBackground, false);
    auto *page = new QVBoxLayout(m_content);
    page->setContentsMargins(26, 16, 26, 28);
    page->setSpacing(14);

    // ── 状态行：只在加载 / 出错时出现，平时不占版面 ──
    m_statusLabel = new QLabel(m_content);
    m_statusLabel->setObjectName(QStringLiteral("forumStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    page->addWidget(m_statusLabel);

    // ── 面包屑：论坛首页 › 板块 › 帖子 ──
    m_crumbsLayout = new QHBoxLayout();
    m_crumbsLayout->setContentsMargins(2, 0, 2, 0);
    m_crumbsLayout->setSpacing(6);
    page->addLayout(m_crumbsLayout);

    // ── 两栏主体 ──
    m_bodyLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(22);

    m_mainColumn = new QWidget(m_content);
    m_mainColumn->setAttribute(Qt::WA_StyledBackground, false);
    m_mainLayout = new QVBoxLayout(m_mainColumn);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(14);

    // 主题头
    m_heroCard = new QFrame(m_mainColumn);
    m_heroCard->setObjectName(QStringLiteral("forumHero"));
    m_heroBody = new QVBoxLayout(m_heroCard);
    m_heroBody->setContentsMargins(24, 22, 24, 22);
    m_heroBody->setSpacing(10);
    m_heroBody->addWidget(ForumKit::makeMuted(m_heroCard, tr("正在加载主题…")));
    m_mainLayout->addWidget(m_heroCard);

    // 楼层列表
    m_floorsLayout = new QVBoxLayout();
    m_floorsLayout->setContentsMargins(0, 0, 0, 0);
    m_floorsLayout->setSpacing(14);
    m_mainLayout->addLayout(m_floorsLayout);

    // 分页
    m_pager = new ForumKit::ForumPager(m_mainColumn);
    connect(m_pager, &ForumKit::ForumPager::pageRequested, this,
            [this](int page) { loadPage(page); });
    m_mainLayout->addWidget(m_pager);

    // 回复框
    buildComposerCard();
    m_mainLayout->addWidget(m_composerCard);
    m_mainLayout->addStretch(1);

    // 右栏
    m_railHost = new QWidget(m_content);
    m_railHost->setAttribute(Qt::WA_StyledBackground, false);
    m_railLayout = new QVBoxLayout(m_railHost);
    m_railLayout->setContentsMargins(0, 0, 0, 0);
    m_railLayout->setSpacing(14);
    buildRail();

    m_bodyLayout->addWidget(m_mainColumn, 1);
    m_bodyLayout->addWidget(m_railHost, 0);
    page->addLayout(m_bodyLayout);
    page->addStretch(1);

    m_scroll->setWidget(m_content);
    outer->addWidget(m_scroll);

    renderCrumbs();
    renderComposer();
    applyLayoutMode();
}

void ForumThreadDetailPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyLayoutMode();
}

void ForumThreadDetailPage::applyLayoutMode()
{
    if (!m_bodyLayout)
        return;

    // 窄窗口把右栏折到主栏下面，避免正文被挤成一条
    const bool stacked = width() > 0 && width() < kStackBelow;
    m_bodyLayout->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_bodyLayout->setSpacing(stacked ? 16 : 22);

    if (stacked)
    {
        m_railHost->setMinimumWidth(0);
        m_railHost->setMaximumWidth(QWIDGETSIZE_MAX);
        m_railHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    else
    {
        m_railHost->setFixedWidth(kRailWidth);
        m_railHost->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    }
}

// ────────────────────────────────────────────────────────────── 面包屑

void ForumThreadDetailPage::renderCrumbs()
{
    if (!m_crumbsLayout)
        return;

    ForumKit::clearLayout(m_crumbsLayout);

    auto makeLink = [this](const QString &text, std::function<void()> onClick) {
        auto *button = new QPushButton(text, m_content);
        button->setObjectName(QStringLiteral("forumCrumb"));
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(20);
        connect(button, &QPushButton::clicked, this, [onClick]() { onClick(); });
        return button;
    };

    m_crumbsLayout->addWidget(makeLink(tr("论坛首页"), [this]() { emit backRequested(); }));

    auto *sep1 = new QLabel(QStringLiteral("›"), m_content);
    sep1->setObjectName(QStringLiteral("forumCrumbSep"));
    m_crumbsLayout->addWidget(sep1);

    if (m_info.boardSlug.isEmpty())
    {
        m_crumbsLayout->addWidget(makeLink(tr("主题"), [this]() { emit backRequested(); }));
    }
    else
    {
        const QString slug = m_info.boardSlug;
        const QString boardName = m_info.boardName;
        m_crumbsLayout->addWidget(
            makeLink(boardName, [this, slug, boardName]() { emit boardOpenRequested(slug, boardName); }));

        auto *sep2 = new QLabel(QStringLiteral("›"), m_content);
        sep2->setObjectName(QStringLiteral("forumCrumbSep"));
        m_crumbsLayout->addWidget(sep2);

        auto *current = ForumKit::makeElided(m_content, m_info.title);
        current->setObjectName(QStringLiteral("forumCrumbCurrent"));
        current->setMinimumWidth(60);
        current->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_crumbsLayout->addWidget(current, 1);
        return;
    }

    m_crumbsLayout->addStretch(1);
}

// ────────────────────────────────────────────────────────────── 主题头

void ForumThreadDetailPage::renderHero()
{
    if (!m_heroBody)
        return;

    ForumKit::clearLayout(m_heroBody);

    if (!m_info.isValid())
    {
        m_heroBody->addWidget(ForumKit::makeMuted(m_heroCard, tr("选择一个帖子就能在这里看全文")));
        return;
    }

    // ── 置顶 / 锁定徽章 ──
    if (m_info.pinned || m_info.locked)
    {
        auto *flagsRow = new QHBoxLayout();
        flagsRow->setSpacing(6);
        if (m_info.pinned)
            flagsRow->addWidget(ForumKit::makeBadge(m_heroCard, tr("置顶")));
        if (m_info.locked)
            flagsRow->addWidget(ForumKit::makeBadge(m_heroCard, tr("已锁定"), true));
        flagsRow->addStretch(1);
        m_heroBody->addLayout(flagsRow);
    }

    // ── 标题 + 右侧收藏（与网页版一致：收藏不占元信息行，避免时间/浏览数被挤换行）──
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    auto *title = new QLabel(m_info.title, m_heroCard);
    title->setObjectName(QStringLiteral("forumThreadTitle"));
    title->setWordWrap(true);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleRow->addWidget(title, 1);

    m_bookmarkButton = new QPushButton(m_info.bookmarked ? tr("★ 已收藏") : tr("☆ 收藏"),
                                       m_heroCard);
    m_bookmarkButton->setObjectName(QStringLiteral("forumBookmarkButton"));
    m_bookmarkButton->setProperty("active", m_info.bookmarked);
    m_bookmarkButton->setCursor(Qt::PointingHandCursor);
    m_bookmarkButton->setFixedHeight(28);
    connect(m_bookmarkButton, &QPushButton::clicked, this, &ForumThreadDetailPage::toggleBookmark);
    titleRow->addWidget(m_bookmarkButton, 0, Qt::AlignTop);

    m_heroBody->addLayout(titleRow);

    // ── 元信息：板块 · 作者 · 时间 · 浏览 · 回复 ·（右侧）收藏 ──
    auto *metaRow = new QHBoxLayout();
    metaRow->setSpacing(8);
    metaRow->setContentsMargins(0, 4, 0, 0);

    if (!m_info.boardSlug.isEmpty())
    {
        auto *boardChip = new QPushButton(m_info.boardName, m_heroCard);
        boardChip->setObjectName(QStringLiteral("forumBoardChip"));
        boardChip->setFlat(true);
        boardChip->setCursor(Qt::PointingHandCursor);
        boardChip->setFixedHeight(22);
        const QString slug = m_info.boardSlug;
        const QString boardName = m_info.boardName;
        connect(boardChip, &QPushButton::clicked, this,
                [this, slug, boardName]() { emit boardOpenRequested(slug, boardName); });
        metaRow->addWidget(boardChip);
    }

    if (m_info.authorId > 0)
    {
        auto *authorRow = new QHBoxLayout();
        authorRow->setSpacing(6);
        auto *avatar = ForumKit::makeAvatar(m_heroCard, m_info.authorAvatar,
                                            m_info.authorName, 20);
        authorRow->addWidget(avatar);

        auto *authorButton = new QPushButton(m_info.authorName, m_heroCard);
        authorButton->setObjectName(QStringLiteral("forumMetaAuthor"));
        authorButton->setFlat(true);
        authorButton->setCursor(Qt::PointingHandCursor);
        authorButton->setFixedHeight(20);
        const int authorId = m_info.authorId;
        connect(authorButton, &QPushButton::clicked, this,
                [this, authorId]() { emit userOpenRequested(authorId); });
        authorRow->addWidget(authorButton);
        metaRow->addLayout(authorRow);

        metaRow->addWidget(makeDot(m_heroCard));
    }

    auto *meta = new QLabel(
        tr("发布于 %1 · %2 次浏览 · %3 条回复")
            .arg(absoluteTime(m_info.createdAt).isEmpty() ? ForumKit::relativeTime(m_info.createdAt)
                                                          : absoluteTime(m_info.createdAt))
            .arg(m_info.views)
            .arg(m_info.replies),
        m_heroCard);
    meta->setObjectName(QStringLiteral("forumMuted"));
    metaRow->addWidget(meta);

    metaRow->addStretch(1);

    m_heroBody->addLayout(metaRow);

    // ── 标签 ──
    if (!m_info.tags.isEmpty())
    {
        auto *tagsRow = new QHBoxLayout();
        tagsRow->setSpacing(6);
        for (const QString &tag : m_info.tags)
        {
            auto *chip = ForumKit::makeTagChip(m_heroCard, tag);
            const QString tagValue = tag;
            connect(chip, &QPushButton::clicked, this,
                    [this, tagValue]() { emit tagOpenRequested(tagValue); });
            tagsRow->addWidget(chip);
        }
        tagsRow->addStretch(1);
        m_heroBody->addLayout(tagsRow);
    }

    // ── 管理行（仅作者与管理员看得到） ──
    auto *account = ForumAccountService::instance();
    const bool admin = account->isLoggedIn() && account->user().role == QStringLiteral("admin");
    const bool mine = account->isLoggedIn() && account->user().id == m_info.authorId;
    if (admin || mine)
    {
        auto *adminRow = new QHBoxLayout();
        adminRow->setSpacing(8);
        adminRow->addStretch(1);

        if (admin)
        {
            appendActButton(adminRow, m_info.pinned ? tr("取消置顶") : tr("置顶"), false,
                            [this]() { togglePin(); });
            appendActButton(adminRow, m_info.locked ? tr("取消锁定") : tr("锁定"), false,
                            [this]() { toggleLock(); });
        }
        if (mine)
        {
            appendActButton(adminRow, tr("编辑标题与标签"), false,
                            [this]() { editThreadMeta(); });
        }
        if (admin || mine)
        {
            appendActButton(adminRow, tr("删除主题"), true,
                            [this]() { confirmDeleteThread(); });
        }
        m_heroBody->addLayout(adminRow);
    }
}

// ────────────────────────────────────────────────────────────── 楼层

void ForumThreadDetailPage::renderPosts(const QVector<ForumPost> &posts)
{
    ForumKit::clearLayout(m_floorsLayout);
    m_postCards.clear();

    if (posts.isEmpty())
    {
        m_floorsLayout->addWidget(ForumKit::makeMuted(m_mainColumn, tr("这一页没有楼层")));
        return;
    }

    for (const ForumPost &post : posts)
        m_floorsLayout->addWidget(buildPostRow(post));
}

QWidget *ForumThreadDetailPage::buildPostRow(const ForumPost &post)
{
    // 已删除楼层：网页版只是一张虚线卡 + 一行灰字，不再渲染作者栏
    if (post.deleted)
    {
        auto *gone = new QFrame(m_mainColumn);
        gone->setObjectName(QStringLiteral("forumPost"));
        gone->setProperty("deleted", true);
        auto *goneLayout = new QVBoxLayout(gone);
        goneLayout->setContentsMargins(20, 14, 20, 14);
        goneLayout->setSpacing(6);

        auto *goneHead = new QHBoxLayout();
        goneHead->setSpacing(8);
        auto *mark = new QLabel(tr("该楼层已被作者或管理员删除"), gone);
        mark->setObjectName(QStringLiteral("forumDeletedMark"));
        goneHead->addWidget(mark);
        goneHead->addStretch(1);
        auto *floorLabel = new QLabel(tr("%1 楼").arg(post.floor), gone);
        floorLabel->setObjectName(QStringLiteral("forumFloorNo"));
        goneHead->addWidget(floorLabel);
        goneLayout->addLayout(goneHead);
        return gone;
    }

    auto *card = new QFrame(m_mainColumn);
    card->setObjectName(QStringLiteral("forumPost"));
    card->setProperty("op", post.floor == 1);

    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(20, 18, 20, 16);
    row->setSpacing(18);

    // ── 左：作者栏 ──
    auto *side = new QWidget(card);
    side->setObjectName(QStringLiteral("forumPostSide"));
    side->setFixedWidth(112);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 2, 0, 0);
    sideLayout->setSpacing(8);

    const QString name = authorDisplay(post.author);
    auto *avatar = ForumKit::makeAvatar(side, post.author.avatar, name, 52);
    sideLayout->addWidget(avatar, 0, Qt::AlignHCenter);

    auto *nameButton = new QPushButton(name, side);
    nameButton->setObjectName(QStringLiteral("forumPostName"));
    nameButton->setFlat(true);
    nameButton->setCursor(Qt::PointingHandCursor);
    nameButton->setToolTip(name);
    const int authorId = post.author.id;
    connect(nameButton, &QPushButton::clicked, this,
            [this, authorId]() { if (authorId > 0) emit userOpenRequested(authorId); });
    sideLayout->addWidget(nameButton);

    if (post.author.role == QStringLiteral("admin"))
        sideLayout->addWidget(ForumKit::makeBadge(side, tr("管理员")), 0, Qt::AlignHCenter);

    QStringList counts;
    counts << tr("%1 帖 · %2 主题").arg(post.author.postCount).arg(post.author.threadCount);
    const QString joined = joinedText(post.author);
    if (!joined.isEmpty())
        counts << joined;
    auto *countLabel = new QLabel(counts.join(QLatin1Char('\n')), side);
    countLabel->setObjectName(QStringLiteral("forumPostCounts"));
    countLabel->setAlignment(Qt::AlignHCenter);
    countLabel->setWordWrap(true);
    sideLayout->addWidget(countLabel);
    sideLayout->addStretch(1);

    row->addWidget(side, 0, Qt::AlignTop);

    // ── 右：正文 ──
    auto *body = new QWidget(card);
    body->setObjectName(QStringLiteral("forumPostBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // 楼层头：楼主 / 时间 / 已编辑 … 楼层号
    auto *headRow = new QHBoxLayout();
    headRow->setSpacing(8);
    if (post.floor == 1)
        headRow->addWidget(ForumKit::makeBadge(body, tr("楼主")));

    const QString relative = ForumKit::relativeTime(post.createdAt);
    auto *timeLabel = new QLabel(relative.isEmpty() ? absoluteTime(post.createdAt) : relative, body);
    timeLabel->setObjectName(QStringLiteral("forumPostTime"));
    timeLabel->setToolTip(absoluteTime(post.createdAt));
    headRow->addWidget(timeLabel);

    if (post.editCount > 0)
    {
        auto *edited = new QLabel(tr("已编辑"), body);
        edited->setObjectName(QStringLiteral("forumEditMark"));
        headRow->addWidget(edited);
    }

    headRow->addStretch(1);

    auto *floorLabel = new QLabel(tr("%1 楼").arg(post.floor), body);
    floorLabel->setObjectName(QStringLiteral("forumFloorNo"));
    headRow->addWidget(floorLabel);
    bodyLayout->addLayout(headRow);

    // 正文
    auto *contentHost = new QWidget(body);
    contentHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 10, 0, 0);
    contentLayout->setSpacing(0);
    if (post.deleted)
    {
        contentLayout->addWidget(ForumKit::makeMuted(contentHost, tr("该楼层已被作者或管理员删除")));
    }
    else
    {
        auto *browser = new ForumPostBrowser(contentHost);
        browser->setPostMarkdown(post.content);
        contentLayout->addWidget(browser);

        // 正文图片点开看大图；帖子内链在启动器内跳转，外部链接才开浏览器
        connect(browser, &ForumPostBrowser::imageClicked, this, [](const QString &path) {
            ForumPostBrowser::showImagePreview(path);
        });
        connect(browser, &ForumPostBrowser::linkClicked, this,
                &ForumThreadDetailPage::handleContentLink);
        m_postCards.insert(post.id, CardRef{card, browser});
    }
    bodyLayout->addWidget(contentHost);

    // 楼中楼
    if (!post.children.isEmpty())
    {
        auto *children = new QFrame(body);
        children->setObjectName(QStringLiteral("forumChildren"));
        auto *childrenLayout = new QVBoxLayout(children);
        childrenLayout->setContentsMargins(0, 6, 0, 0);
        childrenLayout->setSpacing(0);

        auto *visibleHost = new QWidget(children);
        auto *visibleLayout = new QVBoxLayout(visibleHost);
        visibleLayout->setContentsMargins(0, 0, 0, 0);
        visibleLayout->setSpacing(0);

        auto *hiddenHost = new QWidget(children);
        hiddenHost->setObjectName(QStringLiteral("forumChildrenMore"));
        auto *hiddenLayout = new QVBoxLayout(hiddenHost);
        hiddenLayout->setContentsMargins(0, 0, 0, 0);
        hiddenLayout->setSpacing(0);
        hiddenHost->hide();

        for (int i = 0; i < post.children.size(); ++i)
        {
            auto *child = static_cast<QWidget *>(buildChildRow(post.children.at(i)));
            // 首条不画虚线分隔，折叠块的第一条也不画（由折叠块自己画顶线）
            child->setProperty("first", i == 0 || i == kVisibleChildren);
            if (i < kVisibleChildren)
                visibleLayout->addWidget(child);
            else
                hiddenLayout->addWidget(child);
        }

        childrenLayout->addWidget(visibleHost);
        childrenLayout->addWidget(hiddenHost);

        if (post.children.size() > kVisibleChildren)
        {
            auto *toggle = makeAct(children, tr("查看全部 %1 条回复").arg(post.children.size()));
            toggle->setObjectName(QStringLiteral("forumChildrenToggle"));
            connect(toggle, &QPushButton::clicked, children, [toggle, hiddenHost]() {
                const bool expand = hiddenHost->isHidden();
                hiddenHost->setVisible(expand);
                if (expand)
                    toggle->hide();
            });
            childrenLayout->addWidget(toggle, 0, Qt::AlignLeft);
        }

        bodyLayout->addWidget(children);
    }

    // 操作行
    auto *actions = new QHBoxLayout();
    actions->setContentsMargins(0, 12, 0, 0);
    actions->setSpacing(8);
    auto *actionsHost = new QWidget(body);
    actionsHost->setObjectName(QStringLiteral("forumPostActions"));
    auto *actionsOuter = new QVBoxLayout(actionsHost);
    actionsOuter->setContentsMargins(0, 0, 0, 0);
    actionsOuter->addLayout(actions);
    appendPostActions(actions, post, false);
    bodyLayout->addWidget(actionsHost);

    row->addWidget(body, 1);
    setupCardMenu(card, post);
    return card;
}

QWidget *ForumThreadDetailPage::buildChildRow(const ForumPost &child)
{
    auto *row = new QWidget(m_mainColumn);
    row->setObjectName(QStringLiteral("forumChild"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 12, 0, 12);
    layout->setSpacing(10);

    const QString name = authorDisplay(child.author);
    layout->addWidget(ForumKit::makeAvatar(row, child.author.avatar, name, 28), 0, Qt::AlignTop);

    auto *body = new QWidget(row);
    body->setAttribute(Qt::WA_StyledBackground, false);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto *headRow = new QHBoxLayout();
    headRow->setSpacing(8);
    auto *nameButton = new QPushButton(name, body);
    nameButton->setObjectName(QStringLiteral("forumPostName"));
    nameButton->setFlat(true);
    nameButton->setCursor(Qt::PointingHandCursor);
    const int authorId = child.author.id;
    connect(nameButton, &QPushButton::clicked, this,
            [this, authorId]() { if (authorId > 0) emit userOpenRequested(authorId); });
    headRow->addWidget(nameButton);

    if (child.author.role == QStringLiteral("admin"))
        headRow->addWidget(ForumKit::makeBadge(body, tr("管理员")));

    auto *timeLabel = new QLabel(ForumKit::relativeTime(child.createdAt), body);
    timeLabel->setObjectName(QStringLiteral("forumChildTime"));
    headRow->addWidget(timeLabel);
    headRow->addStretch(1);

    auto *floorLabel = new QLabel(QStringLiteral("#%1").arg(child.floor), body);
    floorLabel->setObjectName(QStringLiteral("forumFloorNo"));
    headRow->addWidget(floorLabel);
    bodyLayout->addLayout(headRow);

    if (child.deleted)
    {
        bodyLayout->addWidget(ForumKit::makeMuted(body, tr("该楼层已删除")));
    }
    else
    {
        auto *browser = new ForumPostBrowser(body);
        browser->setPostMarkdown(child.content);
        bodyLayout->addWidget(browser);
        connect(browser, &ForumPostBrowser::imageClicked, this, [](const QString &path) {
            ForumPostBrowser::showImagePreview(path);
        });
        connect(browser, &ForumPostBrowser::linkClicked, this,
                &ForumThreadDetailPage::handleContentLink);
        m_postCards.insert(child.id, CardRef{row, browser});
    }

    auto *actions = new QHBoxLayout();
    actions->setContentsMargins(0, 8, 0, 0);
    actions->setSpacing(6);
    if (!child.deleted)
        appendPostActions(actions, child, true);
    bodyLayout->addLayout(actions);

    layout->addWidget(body, 1);
    setupCardMenu(row, child);
    return row;
}

void ForumThreadDetailPage::appendPostActions(QHBoxLayout *row, const ForumPost &post, bool isChild)
{
    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool admin = loggedIn && account->user().role == QStringLiteral("admin");
    const bool mine = loggedIn && account->user().id == post.author.id;
    const bool canEdit = loggedIn && !post.deleted && (mine || admin);

    if (!post.deleted)
    {
        appendActButton(row, tr("回复"), false, [this, post]() {
            if (!ForumAccountService::instance()->isLoggedIn())
            {
                emit loginRequested();
                return;
            }
            if (!ForumAccountService::instance()->hasPassedQuiz())
            {
                openQuizDialog();
                return;
            }
            setReplyTarget(post);
            scrollToComposer();
        });
        appendActButton(row, tr("引用"), false, [this, post]() { loadQuote(post); });

        auto *likeButton = new QPushButton(QStringLiteral("👍 %1").arg(post.likes), this);
        likeButton->setObjectName(QStringLiteral("forumAct"));
        likeButton->setProperty("postId", post.id);
        likeButton->setProperty("liked", post.liked);
        likeButton->setCursor(Qt::PointingHandCursor);
        likeButton->setFlat(true);
        likeButton->setFixedHeight(28);
        likeButton->setToolTip(tr("给这一楼点个赞"));
        refreshLikeButtonStyle(likeButton);
        connect(likeButton, &QPushButton::clicked, this, [this, likeButton]() {
            if (!ForumAccountService::instance()->isLoggedIn())
            {
                emit loginRequested();
                return;
            }
            const int postId = likeButton->property("postId").toInt();
            likeButton->setEnabled(false);
            ForumContentService::instance()->togglePostLike(
                postId,
                [likeButton](bool liked, int likes) {
                    likeButton->setEnabled(true);
                    likeButton->setProperty("liked", liked);
                    likeButton->setText(QStringLiteral("👍 %1").arg(likes));
                    likeButton->style()->unpolish(likeButton);
                    likeButton->style()->polish(likeButton);
                },
                [this, likeButton](const QString &message, int) {
                    likeButton->setEnabled(true);
                    NotificationManager::showError(this, message);
                });
        });
        row->addWidget(likeButton);

        appendActButton(row, tr("复制链接"), false,
                        [this, post]() { copyPostLink(post); });
    }

    if (isChild)
    {
        if (!post.deleted && !mine && loggedIn)
            appendActButton(row, tr("举报"), false, [this, post]() { reportPost(post); });
        if (canEdit)
            appendActButton(row, tr("删除"), true, [this, post]() { confirmDeletePost(post); });
        row->addStretch(1);
        return;
    }

    row->addStretch(1);

    if (canEdit)
        appendActButton(row, tr("编辑"), false, [this, post]() { startEditPost(post); });
    if (!post.deleted && !mine && loggedIn)
        appendActButton(row, tr("举报"), false, [this, post]() { reportPost(post); });
    if (canEdit)
        appendActButton(row, tr("删除"), true, [this, post]() { confirmDeletePost(post); });
}

void ForumThreadDetailPage::appendActButton(QHBoxLayout *row, const QString &text, bool danger,
                                            std::function<void()> onClick)
{
    auto *button = makeAct(this, text, danger);
    connect(button, &QPushButton::clicked, this, [onClick]() { onClick(); });
    row->addWidget(button);
}

void ForumThreadDetailPage::setupCardMenu(QWidget *card, const ForumPost &post)
{
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, card, [this, card, post](const QPoint &pos) {
        auto *account = ForumAccountService::instance();
        QMenu menu(card);
        const bool mine = account->isLoggedIn() && account->user().id == post.author.id;
        const bool admin = account->isLoggedIn() && account->user().role == QStringLiteral("admin");

        if (mine || admin)
        {
            menu.addAction(tr("编辑这条回复"), this, [this, post]() { startEditPost(post); });
            menu.addAction(tr("删除这条回复"), this, [this, post]() { confirmDeletePost(post); });
        }
        else if (account->isLoggedIn())
        {
            menu.addAction(tr("举报"), this, [this, post]() { reportPost(post); });
        }
        else
        {
            return;
        }

        menu.exec(card->mapToGlobal(pos));
    });
}

void ForumThreadDetailPage::refreshLikeButtonStyle(QPushButton *button)
{
    if (!button)
        return;
    const bool liked = button->property("liked").toBool();
    button->setProperty("likedState", liked ? QStringLiteral("on") : QStringLiteral("off"));
    style()->unpolish(button);
    style()->polish(button);
}

// ────────────────────────────────────────────────────────────── 回复框

void ForumThreadDetailPage::buildComposerCard()
{
    m_composerCard = new QFrame(m_mainColumn);
    m_composerCard->setObjectName(QStringLiteral("forumComposer"));
    m_composerBody = new QVBoxLayout(m_composerCard);
    m_composerBody->setContentsMargins(20, 18, 20, 18);
    m_composerBody->setSpacing(12);
    m_composerCard->hide();
}

void ForumThreadDetailPage::renderComposer()
{
    if (!m_composerBody)
        return;

    // 重建前先把已写的内容收起来，重新挂上后原样放回
    if (m_replyEdit)
        m_replyDraft = m_replyEdit->toPlainText();

    ForumKit::clearLayout(m_composerBody);
    m_replyEdit = nullptr;
    m_replySend = nullptr;
    m_replyCount = nullptr;
    m_replyTargetHost = nullptr;

    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool quizPassed = account->hasPassedQuiz();
    const bool admin = loggedIn && account->user().role == QStringLiteral("admin");
    const bool locked = m_info.locked && !admin;

    auto addTitle = [this](const QString &text) {
        auto *title = new QLabel(text, m_composerCard);
        title->setObjectName(QStringLiteral("forumComposerTitle"));
        title->setWordWrap(true);
        m_composerBody->addWidget(title);
    };
    auto addHint = [this](const QString &text) {
        auto *hint = ForumKit::makeMuted(m_composerCard, text);
        m_composerBody->addWidget(hint);
    };
    auto addPrimary = [this, account](const QString &text, std::function<void()> onClick) {
        auto *foot = new QHBoxLayout();
        auto *button = new QPushButton(text, m_composerCard);
        button->setObjectName(QStringLiteral("forumPrimaryButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(32);
        connect(button, &QPushButton::clicked, this, [onClick]() { onClick(); });
        foot->addWidget(button);
        foot->addStretch(1);
        m_composerBody->addLayout(foot);
        Q_UNUSED(account);
    };

    if (!m_loaded)
    {
        m_composerCard->hide();
        return;
    }
    m_composerCard->show();

    if (locked)
    {
        addTitle(tr("主题已锁定"));
        addHint(tr("这个主题已由管理员锁定，暂时不能回复。"));
        return;
    }

    if (!loggedIn)
    {
        addTitle(tr("想参与讨论？"));
        addHint(tr("登录后就能回复这个主题，也可以用同一账号在方块盒子论坛发帖。"));
        addPrimary(tr("登录 / 注册"), [this]() { emit loginRequested(); });
        return;
    }

    if (!quizPassed)
    {
        addTitle(tr("先通过入站答题，再来回复"));
        addHint(tr("答题结果记在你的账号上：随机抽 50 题、80 分通过，答错会立刻给出答案和解析。"));
        addPrimary(tr("去答题"), [this]() { openQuizDialog(); });
        return;
    }

    // ── 正常回复状态 ──
    auto *headRow = new QHBoxLayout();
    headRow->setSpacing(10);
    const ForumUser me = account->user();
    headRow->addWidget(ForumKit::makeAvatar(m_composerCard, me.avatar, authorDisplay(me), 32));

    auto *title = new QLabel(tr("回复主题"), m_composerCard);
    title->setObjectName(QStringLiteral("forumComposerTitle"));
    headRow->addWidget(title);
    headRow->addStretch(1);

    auto *headHint = new QLabel(tr("支持 Markdown · 可直接粘贴图片"), m_composerCard);
    headHint->setObjectName(QStringLiteral("forumComposerHint"));
    headRow->addWidget(headHint);
    m_composerBody->addLayout(headRow);

    // 回复目标条
    auto *targetHost = new QWidget(m_composerCard);
    targetHost->setAttribute(Qt::WA_StyledBackground, false);
    m_replyTargetHost = new QVBoxLayout(targetHost);
    m_replyTargetHost->setContentsMargins(0, 0, 0, 0);
    m_replyTargetHost->setSpacing(0);
    m_composerBody->addWidget(targetHost);

    m_replyEdit = new QPlainTextEdit(m_composerCard);
    m_replyEdit->setObjectName(QStringLiteral("forumReplyEdit"));
    m_replyEdit->setPlaceholderText(
        tr("说点什么…支持 Markdown：**加粗**、`代码`、> 引用；输入 @ 可以选人，也能直接粘贴或拖入图片"));
    m_replyEdit->setFixedHeight(140);
    m_replyEdit->installEventFilter(this);
    connect(m_replyEdit, &QPlainTextEdit::textChanged, this, [this]() {
        m_replyDraft = m_replyEdit->toPlainText();
        updateReplyCount();
    });
    m_composerBody->addWidget(m_replyEdit);

    auto *footRow = new QHBoxLayout();
    footRow->setSpacing(10);
    auto *identity = new QLabel(tr("以 %1 的身份回复 · Ctrl + Enter 发送")
                                    .arg(authorDisplay(me)),
                                m_composerCard);
    identity->setObjectName(QStringLiteral("forumComposerHint"));
    footRow->addWidget(identity);
    footRow->addStretch(1);

    m_replyCount = new QLabel(tr("0 字"), m_composerCard);
    m_replyCount->setObjectName(QStringLiteral("forumComposerCount"));
    footRow->addWidget(m_replyCount);

    m_replySend = new QPushButton(tr("发表回复"), m_composerCard);
    m_replySend->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_replySend->setCursor(Qt::PointingHandCursor);
    m_replySend->setFixedHeight(32);
    connect(m_replySend, &QPushButton::clicked, this, &ForumThreadDetailPage::submitReply);
    footRow->addWidget(m_replySend);

    m_composerBody->addLayout(footRow);

    if (!m_replyDraft.isEmpty())
    {
        m_replyEdit->setPlainText(m_replyDraft);
        m_replyEdit->moveCursor(QTextCursor::End);
    }
    updateReplyCount();
    renderReplyTarget();
}

void ForumThreadDetailPage::renderReplyTarget()
{
    if (!m_replyTargetHost)
        return;

    ForumKit::clearLayout(m_replyTargetHost);
    if (m_replyTargetPostId <= 0)
        return;

    auto *bar = new QFrame(m_composerCard);
    bar->setObjectName(QStringLiteral("forumReplyTargetBar"));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(12, 8, 8, 8);
    layout->setSpacing(8);

    auto *label = new QLabel(tr("正在回复 <b>%1</b> 的第 %2 楼")
                                 .arg(m_replyTargetName.toHtmlEscaped())
                                 .arg(m_replyTargetFloor),
                             bar);
    label->setObjectName(QStringLiteral("forumReplyTarget"));
    label->setTextFormat(Qt::RichText);
    layout->addWidget(label, 1);

    auto *clear = new QPushButton(tr("取消"), bar);
    clear->setObjectName(QStringLiteral("forumReplyTargetClear"));
    clear->setFlat(true);
    clear->setCursor(Qt::PointingHandCursor);
    clear->setFixedHeight(22);
    connect(clear, &QPushButton::clicked, this, [this]() { clearReplyTarget(); });
    layout->addWidget(clear);

    m_replyTargetHost->addWidget(bar);
}

void ForumThreadDetailPage::updateReplyCount()
{
    if (!m_replyCount || !m_replyEdit)
        return;
    m_replyCount->setText(tr("%1 字").arg(m_replyEdit->toPlainText().trimmed().size()));
}

void ForumThreadDetailPage::updateReplyBoxState()
{
    renderComposer();
}

void ForumThreadDetailPage::setReplyTarget(const ForumPost &post)
{
    // 回复楼中楼时也挂到它所在的父楼层（论坛固定两层结构）
    m_replyTargetPostId = post.parentPostId > 0 ? post.parentPostId : post.id;
    m_replyTargetFloor = post.floor;
    m_replyTargetName = authorDisplay(post.author);
    renderReplyTarget();
}

void ForumThreadDetailPage::clearReplyTarget()
{
    m_replyTargetPostId = 0;
    m_replyTargetFloor = 0;
    m_replyTargetName.clear();
    renderReplyTarget();
}

void ForumThreadDetailPage::loadQuote(const ForumPost &post)
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        emit loginRequested();
        return;
    }
    if (!ForumAccountService::instance()->hasPassedQuiz())
    {
        openQuizDialog();
        return;
    }
    if (!m_replyEdit)
        renderComposer();
    if (!m_replyEdit)
        return;

    QString quoted = post.content.trimmed();
    quoted.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    if (quoted.size() > 200)
        quoted = quoted.left(200) + QStringLiteral("…");

    const QString prefix = QStringLiteral("> %1\n\n").arg(quoted);
    if (m_replyEdit->toPlainText().trimmed().isEmpty())
    {
        m_replyEdit->setPlainText(prefix);
    }
    else
    {
        m_replyEdit->moveCursor(QTextCursor::End);
        m_replyEdit->insertPlainText(QStringLiteral("\n") + prefix);
    }

    m_replyEdit->moveCursor(QTextCursor::End);
    scrollToComposer();
    m_replyEdit->setFocus();
}

void ForumThreadDetailPage::scrollToComposer()
{
    if (!m_composerCard)
        return;
    m_scroll->ensureWidgetVisible(m_composerCard, 0, 24);
    if (m_replyEdit)
        m_replyEdit->setFocus();
}

void ForumThreadDetailPage::submitReply()
{
    if (!m_replyEdit || !m_replySend)
        return;

    const QString content = m_replyEdit->toPlainText().trimmed();
    if (content.isEmpty())
    {
        NotificationManager::showInfo(this, tr("回复内容不能为空"));
        return;
    }

    m_replySend->setEnabled(false);
    const QString originText = m_replySend->text();
    m_replySend->setText(tr("发送中…"));

    const bool isSubReply = m_replyTargetPostId > 0;
    ForumContentService::instance()->replyToThread(
        m_threadId, content, m_replyTargetPostId,
        [this, isSubReply, originText]() {
            if (m_replySend)
            {
                m_replySend->setEnabled(true);
                m_replySend->setText(originText);
            }
            m_replyDraft.clear();
            if (m_replyEdit)
                m_replyEdit->clear();
            if (isSubReply)
                clearReplyTarget();
            NotificationManager::showSuccess(this, tr("回复已发送"));
            // 先拿到最新总页数，再翻到最后一页看自己那条
            ForumContentService::instance()->fetchThread(
                m_threadId, 1,
                [this](const ForumThreadDetail &detail) {
                    loadPage(qMax(1, detail.pagination.pages), true);
                },
                [this](const QString &, int) { loadPage(m_totalPages, true); });
        },
        [this, originText](const QString &message, int) {
            if (m_replySend)
            {
                m_replySend->setEnabled(true);
                m_replySend->setText(originText);
            }
            NotificationManager::showError(this, message);
        });
}

void ForumThreadDetailPage::openQuizDialog()
{
    QWidget *host = window();
    QuizDialog dialog(host);
    connect(&dialog, &QuizDialog::loginRequested, this, &ForumThreadDetailPage::loginRequested);
    dialog.exec();
    // 通过后 reportQuizPass 会发 userChanged 带动 renderComposer；这里再刷一次兜底
    renderComposer();
}

// ────────────────────────────────────────────────────────────── 收藏 / 置顶 / 锁定

void ForumThreadDetailPage::toggleBookmark()
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        emit loginRequested();
        return;
    }

    m_bookmarkButton->setEnabled(false);
    ForumContentService::instance()->toggleThreadBookmark(
        m_threadId,
        [this](bool bookmarked) {
            m_info.bookmarked = bookmarked;
            if (m_bookmarkButton)
            {
                m_bookmarkButton->setEnabled(true);
                m_bookmarkButton->setText(bookmarked ? tr("★ 已收藏") : tr("☆ 收藏"));
                m_bookmarkButton->setProperty("active", bookmarked);
                style()->unpolish(m_bookmarkButton);
                style()->polish(m_bookmarkButton);
            }
            NotificationManager::showSuccess(this, bookmarked ? tr("已收藏，可在个人主页查看")
                                                              : tr("已取消收藏"));
        },
        [this](const QString &message, int) {
            if (m_bookmarkButton)
                m_bookmarkButton->setEnabled(true);
            NotificationManager::showError(this, message);
        });
}

void ForumThreadDetailPage::togglePin()
{
    const bool target = !m_info.pinned;
    ForumContentService::instance()->setThreadPinned(
        m_threadId, target,
        [this, target]() {
            m_info.pinned = target;
            NotificationManager::showSuccess(this, target ? tr("已置顶") : tr("已取消置顶"));
            renderHero();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

void ForumThreadDetailPage::toggleLock()
{
    const bool target = !m_info.locked;
    ForumContentService::instance()->setThreadLocked(
        m_threadId, target,
        [this, target]() {
            m_info.locked = target;
            NotificationManager::showSuccess(this, target ? tr("已锁定，暂不接受新回复")
                                                          : tr("已取消锁定"));
            renderHero();
            renderComposer();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

// ────────────────────────────────────────────────────────────── 复制链接

void ForumThreadDetailPage::copyThreadLink()
{
    QString base = ForumAccountService::instance()->baseUrl();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QString url = QStringLiteral("%1/thread.html?id=%2").arg(base).arg(m_threadId);
    QApplication::clipboard()->setText(url);
    NotificationManager::showSuccess(this, tr("本页链接已复制"));
}

void ForumThreadDetailPage::copyPostLink(const ForumPost &post)
{
    QString base = ForumAccountService::instance()->baseUrl();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QString url =
        QStringLiteral("%1/thread.html?id=%2#post-%3").arg(base).arg(m_threadId).arg(post.id);
    QApplication::clipboard()->setText(url);
    NotificationManager::showSuccess(this, tr("楼层链接已复制"));
}

// ────────────────────────────────────────────────────────────── 楼层编辑 / 删除 / 举报

void ForumThreadDetailPage::startEditPost(const ForumPost &post)
{
    const CardRef ref = m_postCards.value(post.id);
    if (ref.card.isNull() || ref.browser.isNull())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("编辑回复"));
    dialog.setMinimumWidth(520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *edit = new QPlainTextEdit(&dialog);
    edit->setPlainText(post.content);
    edit->setPlaceholderText(tr("支持 Markdown"));
    edit->setMinimumHeight(180);
    layout->addWidget(edit);
    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), &dialog);
    auto *save = new QPushButton(tr("保存"), &dialog);
    save->setObjectName(QStringLiteral("forumPrimaryButton"));
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString content = edit->toPlainText().trimmed();
    if (content.isEmpty() || content == post.content)
        return;

    ForumContentService::instance()->editPost(
        post.id, content,
        [this]() {
            NotificationManager::showSuccess(this, tr("回复已更新"));
            reload();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

void ForumThreadDetailPage::confirmDeletePost(const ForumPost &post)
{
    const QString question = post.parentPostId > 0
                                 ? tr("确定删除这条楼中楼回复吗？此操作不可撤销。")
                                 : tr("确定删除这条回复吗？楼中楼会一并隐藏，此操作不可撤销。");
    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("删除回复"), question, AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    ForumContentService::instance()->deletePost(
        post.id,
        [this]() {
            NotificationManager::showSuccess(this, tr("回复已删除"));
            reload();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

void ForumThreadDetailPage::confirmDeleteThread()
{
    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("删除主题"), tr("确定删除这个主题吗？所有楼层都不再显示，此操作不可撤销。"),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    ForumContentService::instance()->deleteThread(
        m_threadId,
        [this]() {
            NotificationManager::showSuccess(this, tr("主题已删除"));
            clearContent();
            emit threadDeleted();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

void ForumThreadDetailPage::editThreadMeta()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("编辑标题与标签"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel(tr("标题"), &dialog));
    auto *titleEdit = new QLineEdit(m_info.title, &dialog);
    layout->addWidget(titleEdit);

    layout->addWidget(new QLabel(tr("标签（用逗号分隔，最多 4 个）"), &dialog));
    auto *tagsEdit = new QLineEdit(m_info.tags.join(QStringLiteral(", ")), &dialog);
    layout->addWidget(tagsEdit);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), &dialog);
    auto *save = new QPushButton(tr("保存"), &dialog);
    save->setObjectName(QStringLiteral("forumPrimaryButton"));
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString title = titleEdit->text().trimmed();
    QStringList tags;
    for (const QString &tag : tagsEdit->text().split(QLatin1Char(',')))
    {
        const QString trimmed = tag.trimmed();
        if (!trimmed.isEmpty())
            tags.append(trimmed);
    }
    if (tags.size() > 4)
        tags = tags.mid(0, 4);
    if (title.isEmpty())
    {
        NotificationManager::showInfo(this, tr("标题不能为空"));
        return;
    }

    ForumContentService::instance()->editThread(
        m_threadId, title, tags,
        [this]() {
            NotificationManager::showSuccess(this, tr("主题已更新"));
            reload();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

void ForumThreadDetailPage::reportPost(const ForumPost &post)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("举报楼层"));
    dialog.setMinimumWidth(420);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("说明举报原因（最多 200 字）"), &dialog));
    auto *reasonEdit = new QPlainTextEdit(&dialog);
    reasonEdit->setPlaceholderText(tr("广告 / 人身攻击 / 刷屏…"));
    reasonEdit->setFixedHeight(90);
    layout->addWidget(reasonEdit);
    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), &dialog);
    auto *submit = new QPushButton(tr("提交举报"), &dialog);
    submit->setObjectName(QStringLiteral("forumPrimaryButton"));
    buttons->addWidget(cancel);
    buttons->addWidget(submit);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(submit, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString reason = reasonEdit->toPlainText().trimmed();
    if (reason.isEmpty())
    {
        NotificationManager::showInfo(this, tr("先填写举报原因"));
        return;
    }

    ForumContentService::instance()->reportPost(
        post.id, reason,
        [this]() { NotificationManager::showSuccess(this, tr("已提交举报，管理员会尽快处理")); },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

// ────────────────────────────────────────────────────────────── 右栏

void ForumThreadDetailPage::buildRail()
{
    m_railLayout->addStretch(1);
}

void ForumThreadDetailPage::renderRail()
{
    if (!m_railLayout)
        return;

    ForumKit::clearLayout(m_railLayout);

    auto makeCard = [this](const QString &title) {
        auto *card = new QFrame(m_railHost);
        card->setObjectName(QStringLiteral("forumRailCard"));
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 15, 16, 15);
        layout->setSpacing(10);
        auto *titleLabel = new QLabel(title, card);
        titleLabel->setObjectName(QStringLiteral("forumRailTitle"));
        layout->addWidget(titleLabel);
        m_railLayout->addWidget(card);
        return layout;
    };

    // ── 主题信息 ──
    auto *info = makeCard(tr("主题信息"));

    auto *statsRow = new QHBoxLayout();
    statsRow->setSpacing(10);
    auto addStat = [statsRow](const QString &value, const QString &caption, QWidget *parent) {
        auto *box = new QFrame(parent);
        box->setObjectName(QStringLiteral("forumStatBox"));
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(12, 9, 12, 9);
        boxLayout->setSpacing(0);
        auto *valueLabel = new QLabel(value, box);
        valueLabel->setObjectName(QStringLiteral("forumStatValue"));
        boxLayout->addWidget(valueLabel);
        auto *captionLabel = new QLabel(caption, box);
        captionLabel->setObjectName(QStringLiteral("forumRailKey"));
        boxLayout->addWidget(captionLabel);
        statsRow->addWidget(box, 1);
    };
    addStat(QString::number(m_info.views), tr("浏览"), m_railHost);
    addStat(QString::number(m_info.replies), tr("回复"), m_railHost);
    info->addLayout(statsRow);

    auto addInfoRow = [this, info](const QString &key, QWidget *value) {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        auto *keyLabel = new QLabel(key, m_railHost);
        keyLabel->setObjectName(QStringLiteral("forumRailKey"));
        keyLabel->setFixedWidth(52);
        row->addWidget(keyLabel, 0, Qt::AlignTop);
        row->addWidget(value, 1);
        info->addLayout(row);
    };

    if (!m_info.boardSlug.isEmpty())
    {
        auto *link = new QPushButton(m_info.boardName, m_railHost);
        link->setObjectName(QStringLiteral("forumRailLink"));
        link->setFlat(true);
        link->setCursor(Qt::PointingHandCursor);
        const QString slug = m_info.boardSlug;
        const QString boardName = m_info.boardName;
        connect(link, &QPushButton::clicked, this,
                [this, slug, boardName]() { emit boardOpenRequested(slug, boardName); });
        addInfoRow(tr("版块"), link);
    }

    if (m_info.authorId > 0)
    {
        auto *link = new QPushButton(m_info.authorName, m_railHost);
        link->setObjectName(QStringLiteral("forumRailLink"));
        link->setFlat(true);
        link->setCursor(Qt::PointingHandCursor);
        const int authorId = m_info.authorId;
        connect(link, &QPushButton::clicked, this,
                [this, authorId]() { emit userOpenRequested(authorId); });
        addInfoRow(tr("楼主"), link);
    }

    auto *published = new QLabel(absoluteTime(m_info.createdAt), m_railHost);
    published->setObjectName(QStringLiteral("forumRailValue"));
    published->setWordWrap(true);
    addInfoRow(tr("发布"), published);

    auto *lastReply = new QLabel(
        m_info.lastPostAt > 0 ? ForumKit::relativeTime(m_info.lastPostAt) : QStringLiteral("—"),
        m_railHost);
    lastReply->setObjectName(QStringLiteral("forumRailValue"));
    addInfoRow(tr("最后回复"), lastReply);

    if (m_info.locked)
    {
        auto *note = new QLabel(tr("该主题已锁定，暂不接受新回复"), m_railHost);
        note->setObjectName(QStringLiteral("forumRailNote"));
        note->setWordWrap(true);
        info->addWidget(note);
    }

    auto *refresh = new QPushButton(tr("刷新本页"), m_railHost);
    refresh->setObjectName(QStringLiteral("forumRailButton"));
    refresh->setCursor(Qt::PointingHandCursor);
    refresh->setFixedHeight(32);
    connect(refresh, &QPushButton::clicked, this, [this]() { reload(); });
    info->addWidget(refresh);

    auto *copy = new QPushButton(tr("复制本页链接"), m_railHost);
    copy->setObjectName(QStringLiteral("forumRailButton"));
    copy->setCursor(Qt::PointingHandCursor);
    copy->setFixedHeight(32);
    connect(copy, &QPushButton::clicked, this, &ForumThreadDetailPage::copyThreadLink);
    info->addWidget(copy);

    // ── 同版块主题 ──
    auto *board = makeCard(tr("同版块主题"));
    auto *listHost = new QWidget(m_railHost);
    listHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    auto *loading = new QLabel(tr("加载中…"), listHost);
    loading->setObjectName(QStringLiteral("forumRailEmpty"));
    listLayout->addWidget(loading);
    board->addWidget(listHost);

    m_railThreadList = listLayout;
    m_railThreadHost = listHost;
    if (!m_info.boardSlug.isEmpty())
        loadRailThreads(m_info.boardSlug);

    // ── 社区提示 ──
    auto *tips = makeCard(tr("社区提示"));
    const QStringList tipList{tr("友善交流，就事论事"), tr("提问前先搜一下同类问题"),
                              tr("图片可直接粘贴进回复框")};
    for (const QString &tip : tipList)
    {
        auto *label = new QLabel(QStringLiteral("✓  %1").arg(tip), m_railHost);
        label->setObjectName(QStringLiteral("forumRailTip"));
        label->setWordWrap(true);
        tips->addWidget(label);
    }

    m_railLayout->addStretch(1);
}

void ForumThreadDetailPage::loadRailThreads(const QString &slug)
{
    ForumContentService::instance()->fetchBoardThreads(
        slug, QStringLiteral("latest"), 1,
        [this](const ForumBoard &, const QVector<ForumThreadInfo> &threads,
               const ForumPagination &) {
            if (!m_railThreadList)
                return;
            ForumKit::clearLayout(m_railThreadList);
            m_railThreadIds.clear();

            QVector<ForumThreadInfo> others;
            for (const ForumThreadInfo &thread : threads)
            {
                if (thread.id == m_threadId)
                    continue;
                others.append(thread);
                if (others.size() >= 5)
                    break;
            }

            if (others.isEmpty())
            {
                auto *empty = new QLabel(tr("这个版块还没有其他主题"), m_railThreadHost);
                empty->setObjectName(QStringLiteral("forumRailEmpty"));
                m_railThreadList->addWidget(empty);
                return;
            }

            for (const ForumThreadInfo &thread : others)
            {
                // 用 QFrame 而不是 QPushButton：#forumRailThread 里放的是「标题 + 元信息」两行，
                // QPushButton 的 sizeHint 不吃子布局，会被压成一行高、文字被裁掉。
                auto *item = new QFrame(m_railThreadHost);
                item->setObjectName(QStringLiteral("forumRailThread"));
                item->setCursor(Qt::PointingHandCursor);
                item->setToolTip(thread.title);
                auto *itemLayout = new QVBoxLayout(item);
                itemLayout->setContentsMargins(0, 7, 0, 7);
                itemLayout->setSpacing(2);
                auto *titleLabel = ForumKit::makeElided(item, thread.title);
                titleLabel->setObjectName(QStringLiteral("forumRailThreadTitle"));
                itemLayout->addWidget(titleLabel);
                auto *meta = new QLabel(tr("%1 · %2 回复").arg(thread.authorName).arg(thread.replies),
                                        item);
                meta->setObjectName(QStringLiteral("forumRailThreadMeta"));
                itemLayout->addWidget(meta);
                m_railThreadIds.insert(item, thread.id);
                item->installEventFilter(this);
                m_railThreadList->addWidget(item);
            }
        },
        [this](const QString &, int) {
            if (!m_railThreadList)
                return;
            ForumKit::clearLayout(m_railThreadList);
            auto *empty = new QLabel(tr("暂时读不到同版块主题"), m_railThreadHost);
            empty->setObjectName(QStringLiteral("forumRailEmpty"));
            m_railThreadList->addWidget(empty);
        });
}

// ────────────────────────────────────────────────────────────── 加载

void ForumThreadDetailPage::openThread(int threadId)
{
    if (m_threadId == threadId && m_loaded)
        return;
    clearContent();
    m_threadId = threadId;
    loadPage(1);
}

void ForumThreadDetailPage::clearContent()
{
    m_threadId = 0;
    m_info = ForumThreadInfo();
    m_page = 1;
    m_totalPages = 1;
    m_loaded = false;
    m_postCards.clear();
    m_editingPostId = 0;
    m_replyDraft.clear();
    clearReplyTarget();

    renderCrumbs();
    renderHero();
    ForumKit::clearLayout(m_floorsLayout);
    m_pager->setPage(1, 1);
    renderComposer();
    setStatus(QString());

    if (m_railLayout)
    {
        ForumKit::clearLayout(m_railLayout);
        m_railThreadHost = nullptr;
        m_railThreadList = nullptr;
        m_railThreadIds.clear();
        m_railLayout->addStretch(1);
    }
}

void ForumThreadDetailPage::loadPage(int page, bool scrollToBottomAfter)
{
    if (m_threadId <= 0)
        return;

    setStatus(tr("正在加载帖子…"));

    ForumContentService::instance()->fetchThread(
        m_threadId, page,
        [this, scrollToBottomAfter](const ForumThreadDetail &detail) {
            if (m_threadId != detail.info.id)
                return; // 已经切到别的帖子了，丢弃过期结果

            m_info = detail.info;
            m_page = qMax(1, detail.pagination.page);
            m_totalPages = qMax(1, detail.pagination.pages);
            m_loaded = true;
            setStatus(QString());

            renderCrumbs();
            renderHero();
            renderPosts(detail.posts);
            m_pager->setPage(m_page, m_totalPages);
            renderComposer();
            renderRail();

            QMetaObject::invokeMethod(this, [this, scrollToBottomAfter]() {
                if (scrollToBottomAfter)
                    m_scroll->verticalScrollBar()->setValue(
                        m_scroll->verticalScrollBar()->maximum());
                else
                    m_scroll->verticalScrollBar()->setValue(0);
            }, Qt::QueuedConnection);
        },
        [this](const QString &message, int httpStatus) {
            Q_UNUSED(httpStatus);
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void ForumThreadDetailPage::reload(bool scrollToBottomAfter)
{
    if (m_threadId > 0)
        loadPage(m_page, scrollToBottomAfter);
}

// ────────────────────────────────────────────────────────────── 杂项

bool ForumThreadDetailPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_replyEdit && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->modifiers() & Qt::ControlModifier) && (key->key() == Qt::Key_Return
                                                         || key->key() == Qt::Key_Enter))
        {
            submitReply();
            return true;
        }
    }
    // 侧栏「同版块主题」整行可点（QFrame 没有 clicked 信号，走事件过滤）
    if (event->type() == QEvent::MouseButtonRelease && m_railThreadIds.contains(static_cast<QWidget *>(watched)))
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            const int threadId = m_railThreadIds.value(static_cast<QWidget *>(watched));
            if (threadId > 0)
                emit threadOpenRequested(threadId);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ForumThreadDetailPage::handleContentLink(const QUrl &url)
{
    auto *service = ForumAccountService::instance();
    QString text = url.toString();
    const QString base = service->baseUrl();
    // 论坛站内链接：绝对地址先去掉站点前缀，统一按站内路径处理
    if (!base.isEmpty() && text.startsWith(base))
        text = text.mid(base.size());

    if (text.contains(QStringLiteral("thread.html")))
    {
        bool ok = false;
        const int id = QUrlQuery(QUrl(text)).queryItemValue(QStringLiteral("id")).toInt(&ok);
        if (ok && id > 0)
        {
            if (id == m_threadId)
                m_scroll->verticalScrollBar()->setValue(0);
            else
                emit threadOpenRequested(id); // 帖子内链：交给上层切换，返回键仍回到来源页
            return;
        }
    }
    if (text.contains(QStringLiteral("board.html")))
    {
        const QString slug = QUrlQuery(QUrl(text)).queryItemValue(QStringLiteral("slug"));
        if (!slug.isEmpty())
        {
            emit boardOpenRequested(slug, slug);
            return;
        }
    }
    // 其余都是站外链接，交给系统浏览器
    QDesktopServices::openUrl(url);
}

void ForumThreadDetailPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.isEmpty());
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}
