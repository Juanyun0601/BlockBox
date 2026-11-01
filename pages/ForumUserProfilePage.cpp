/**
 * @file   ForumUserProfilePage.cpp
 * @brief  论坛用户主页实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumUserProfilePage.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "components/ForumPostBrowser.h"
#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumChatService.h"
#include "utils/StyleKit.h"

ForumUserProfilePage::ForumUserProfilePage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void ForumUserProfilePage::buildUi()
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
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() { loadProfile(); });
    statusRow->addWidget(m_refreshButton);
    layout->addLayout(statusRow);

    // ── 资料卡 ──
    m_profileCard = ForumKit::makeCard(content, QStringLiteral("forumHeaderCard"));
    auto *cardLayout = new QVBoxLayout(m_profileCard);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    m_coverBanner = new ForumKit::CoverBanner(m_profileCard);
    m_coverBanner->setAspectRatio(3.0);
    m_coverBanner->setRadius(14);
    cardLayout->addWidget(m_coverBanner);

    auto *cardBody = new QWidget(m_profileCard);
    m_profileBody = new QVBoxLayout(cardBody);
    m_profileBody->setContentsMargins(20, 16, 20, 16);
    m_profileBody->setSpacing(10);
    m_profileBody->addWidget(ForumKit::makeMuted(cardBody, tr("点帖子里的作者名字可以看TA的主页")));
    cardLayout->addWidget(cardBody);

    layout->addWidget(m_profileCard);

    // ── 页签行 ──
    auto *tabRow = new QHBoxLayout();
    tabRow->setSpacing(6);
    m_threadsTabButton = new QPushButton(tr("主题"), content);
    m_threadsTabButton->setObjectName(QStringLiteral("forumSortButton"));
    m_threadsTabButton->setCheckable(true);
    m_threadsTabButton->setChecked(true);
    m_threadsTabButton->setCursor(Qt::PointingHandCursor);
    m_threadsTabButton->setFixedHeight(28);
    connect(m_threadsTabButton, &QPushButton::clicked, this,
            &ForumUserProfilePage::showThreadsTab);
    tabRow->addWidget(m_threadsTabButton);

    m_repliesTabButton = new QPushButton(tr("回复"), content);
    m_repliesTabButton->setObjectName(QStringLiteral("forumSortButton"));
    m_repliesTabButton->setCheckable(true);
    m_repliesTabButton->setCursor(Qt::PointingHandCursor);
    m_repliesTabButton->setFixedHeight(28);
    connect(m_repliesTabButton, &QPushButton::clicked, this,
            &ForumUserProfilePage::showRepliesTab);
    tabRow->addWidget(m_repliesTabButton);
    tabRow->addStretch(1);
    layout->addLayout(tabRow);

    // ── 列表 ──
    m_listLayout = new QVBoxLayout();
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(8);
    layout->addLayout(m_listLayout);

    // ── 分页 ──
    m_pager = new ForumKit::ForumPager(content);
    connect(m_pager, &ForumKit::ForumPager::pageRequested, this, [this](int page) {
        if (m_activeTab == 0)
            loadThreads(page);
        else
            loadReplies(page);
    });
    layout->addWidget(m_pager);

    layout->addStretch(1);
    m_scroll->setWidget(content);
    outer->addWidget(m_scroll);
}

void ForumUserProfilePage::openUser(int userId)
{
    if (m_userId == userId && !m_profileBody->isEmpty())
    {
        // 同一个用户刷新列表即可
        if (m_activeTab == 0)
            loadThreads(m_page);
        else
            loadReplies(m_page);
        return;
    }
    m_userId = userId;
    m_activeTab = 0;
    m_threadTotal = 0;
    m_postTotal = 0;
    m_friendship = QStringLiteral("none");
    ForumKit::clearLayout(m_listLayout);
    loadProfile();
}

void ForumUserProfilePage::loadProfile()
{
    if (m_userId <= 0)
        return;

    m_refreshButton->setEnabled(false);
    setStatus(tr("正在加载用户资料…"));

    ForumContentService::instance()->fetchUser(
        m_userId,
        [this](const ForumUserProfile &profile) {
            m_refreshButton->setEnabled(true);
            setStatus(QString());
            renderProfile(profile);
            m_activeTab = 0;
            m_threadsTabButton->setChecked(true);
            m_repliesTabButton->setChecked(false);
            loadThreads(1);
        },
        [this](const QString &message, int) {
            m_refreshButton->setEnabled(true);
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void ForumUserProfilePage::renderProfile(const ForumUserProfile &profile)
{
    if (m_coverBanner)
        m_coverBanner->setCover(profile.user.cover);

    ForumKit::clearLayout(m_profileBody);

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(14);

    // 头像贴顶：简介是 Markdown，可能有好几行，居中的话会飘到中间
    topRow->addWidget(ForumKit::makeAvatar(m_profileCard, profile.user.avatar,
                                           profile.user.displayName, 64),
                      0, Qt::AlignTop);

    auto *textColumn = new QVBoxLayout();
    textColumn->setSpacing(4);

    auto *nameRow = new QHBoxLayout();
    nameRow->setSpacing(8);
    auto *name = new QLabel(profile.user.displayName, m_profileCard);
    name->setObjectName(QStringLiteral("forumThreadTitle"));
    nameRow->addWidget(name);
    if (profile.user.role == QStringLiteral("admin"))
        nameRow->addWidget(ForumKit::makeBadge(m_profileCard, tr("管理员")));
    if (profile.user.status == QStringLiteral("muted"))
        nameRow->addWidget(ForumKit::makeBadge(m_profileCard, tr("禁言中"), true));
    nameRow->addStretch(1);
    textColumn->addLayout(nameRow);

    const QString bio = profile.user.bio.trimmed();
    if (bio.isEmpty())
    {
        textColumn->addWidget(
            ForumKit::makeMuted(m_profileCard, tr("这个人很神秘，什么都没写")));
    }
    else
    {
        // 简介按 Markdown 渲染，和帖子正文同一套排版（加粗、代码、列表、链接、图片）
        auto *bioBrowser = new ForumPostBrowser(m_profileCard);
        bioBrowser->setObjectName(QStringLiteral("forumProfileBio"));
        bioBrowser->setCompactMode(true);
        bioBrowser->setPostMarkdown(bio);
        connect(bioBrowser, &ForumPostBrowser::linkClicked, this,
                &ForumUserProfilePage::handleBioLink);
        connect(bioBrowser, &ForumPostBrowser::imageClicked, this,
                [](const QString &path) { ForumPostBrowser::showImagePreview(path); });
        textColumn->addWidget(bioBrowser);
    }

    const QString registerDate = profile.user.createdAt > 0
                                     ? QDateTime::fromSecsSinceEpoch(profile.user.createdAt)
                                           .toString(QStringLiteral("yyyy-MM-dd"))
                                     : QString();
    textColumn->addWidget(ForumKit::makeMuted(
        m_profileCard,
        tr("注册于 %1").arg(registerDate.isEmpty() ? QStringLiteral("—") : registerDate)));

    topRow->addLayout(textColumn, 1);
    m_profileBody->addLayout(topRow);

    // 统计
    auto *statsRow = new QHBoxLayout();
    statsRow->setSpacing(24);
    const QVector<QPair<QString, int>> stats = {
        {tr("主题"), qMax(profile.user.threadCount, m_threadTotal)},
        {tr("回复"), qMax(profile.user.postCount, m_postTotal)},
    };
    for (const auto &stat : stats)
    {
        auto *column = new QVBoxLayout();
        column->setSpacing(0);
        auto *value = new QLabel(QString::number(stat.second), m_profileCard);
        value->setObjectName(QStringLiteral("forumStatValue"));
        value->setAlignment(Qt::AlignHCenter);
        auto *caption = ForumKit::makeMuted(m_profileCard, stat.first);
        caption->setAlignment(Qt::AlignHCenter);
        column->addWidget(value);
        column->addWidget(caption);
        statsRow->addLayout(column);
    }
    statsRow->addStretch(1);
    m_profileBody->addLayout(statsRow);

    // 好友操作行（加好友 / 发私信）
    renderFriendActions(profile);
}

void ForumUserProfilePage::handleBioLink(const QUrl &url)
{
    // 简介里的站内帖子链接在启动器内打开，其余链接交给系统浏览器
    auto *service = ForumAccountService::instance();
    QString text = url.toString();
    const QString base = service ? service->baseUrl() : QString();
    if (!base.isEmpty() && text.startsWith(base))
        text = text.mid(base.size());

    if (text.contains(QStringLiteral("thread.html")))
    {
        bool ok = false;
        const int id = QUrlQuery(QUrl(text)).queryItemValue(QStringLiteral("id")).toInt(&ok);
        if (ok && id > 0)
        {
            emit threadOpenRequested(id);
            return;
        }
    }
    QDesktopServices::openUrl(url);
}

void ForumUserProfilePage::renderFriendActions(const ForumUserProfile &profile)
{
    m_friendship = profile.friendship;
    m_friendButton = nullptr;
    m_chatButton = nullptr;

    // 自己的主页 / 被封禁的用户 / 未登录时不显示好友操作
    if (m_friendship == QStringLiteral("self") ||
        profile.user.status == QStringLiteral("banned") ||
        !ForumAccountService::instance()->isLoggedIn())
        return;

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(8);

    if (m_friendship == QStringLiteral("friends"))
    {
        m_chatButton = new QPushButton(tr("💬 发私信"), m_profileCard);
        m_chatButton->setObjectName(QStringLiteral("forumPrimaryButton"));
        m_chatButton->setCursor(Qt::PointingHandCursor);
        m_chatButton->setFixedHeight(30);
        connect(m_chatButton, &QPushButton::clicked, this,
                [this]() { emit chatOpenRequested(m_userId); });
        actionRow->addWidget(m_chatButton);
    }
    else if (m_friendship == QStringLiteral("outgoing"))
    {
        m_friendButton = new QPushButton(tr("已申请，点击撤回"), m_profileCard);
        m_friendButton->setObjectName(QStringLiteral("forumGhostButton"));
        m_friendButton->setCursor(Qt::PointingHandCursor);
        m_friendButton->setFixedHeight(30);
        connect(m_friendButton, &QPushButton::clicked, this, [this]() {
            if (m_friendButton)
                m_friendButton->setEnabled(false);
            // 主页数据里没有申请 id：先拉好友数据找到自己发出的那条再撤
            ForumChatService::instance()->fetchFriends(
                [this](const ForumFriendSnapshot &snapshot) {
                    for (const ForumFriendRequest &request : snapshot.outgoing)
                    {
                        if (request.user.id != m_userId)
                            continue;
                        ForumChatService::instance()->withdrawFriendRequest(
                            request.id,
                            [this]() {
                                NotificationManager::showSuccess(this, tr("已撤回好友申请"));
                                loadProfile();
                            },
                            [this](const QString &message, int) {
                                NotificationManager::showError(this, message);
                                loadProfile();
                            });
                        return;
                    }
                },
                [this](const QString &message, int) {
                    if (m_friendButton)
                        m_friendButton->setEnabled(true);
                    NotificationManager::showError(this, message);
                });
        });
        actionRow->addWidget(m_friendButton);
    }
    else if (m_friendship == QStringLiteral("incoming"))
    {
        auto *hint = new QPushButton(tr("TA想加你好友，去好友页处理"), m_profileCard);
        hint->setObjectName(QStringLiteral("forumGhostButton"));
        hint->setCursor(Qt::PointingHandCursor);
        hint->setFixedHeight(30);
        connect(hint, &QPushButton::clicked, this,
                &ForumUserProfilePage::friendsOpenRequested);
        actionRow->addWidget(hint);
    }
    else
    {
        m_friendButton = new QPushButton(tr("＋ 加好友"), m_profileCard);
        m_friendButton->setObjectName(QStringLiteral("forumPrimaryButton"));
        m_friendButton->setCursor(Qt::PointingHandCursor);
        m_friendButton->setFixedHeight(30);
        connect(m_friendButton, &QPushButton::clicked, this, [this]() {
            if (m_friendButton)
                m_friendButton->setEnabled(false);
            ForumChatService::instance()->sendFriendRequest(
                m_userId,
                [this](const QString &status) {
                    NotificationManager::showSuccess(
                        this, status == QStringLiteral("friends")
                                  ? tr("对方也申请了你，已经成为好友")
                                  : tr("申请已发出，等对方同意"));
                    loadProfile();
                },
                [this](const QString &message, int) {
                    if (m_friendButton)
                        m_friendButton->setEnabled(true);
                    NotificationManager::showError(this, message);
                });
        });
        actionRow->addWidget(m_friendButton);
    }
    actionRow->addStretch(1);
    m_profileBody->addLayout(actionRow);
}

void ForumUserProfilePage::showThreadsTab()
{
    if (m_activeTab == 0)
        return;
    m_activeTab = 0;
    m_repliesTabButton->setChecked(false);
    m_threadsTabButton->setChecked(true);
    loadThreads(1);
}

void ForumUserProfilePage::showRepliesTab()
{
    if (m_activeTab == 1)
        return;
    m_activeTab = 1;
    m_threadsTabButton->setChecked(false);
    m_repliesTabButton->setChecked(true);
    loadReplies(1);
}

void ForumUserProfilePage::loadThreads(int page)
{
    if (m_userId <= 0)
        return;
    m_refreshButton->setEnabled(false);
    setStatus(tr("正在加载主题…"));

    ForumContentService::instance()->fetchUserThreads(
        m_userId, page,
        [this](const QVector<ForumThreadInfo> &threads, const ForumPagination &pagination) {
            m_refreshButton->setEnabled(true);
            setStatus(QString());
            m_threadTotal = qMax(m_threadTotal, pagination.total);
            m_page = qMax(1, pagination.page);
            m_totalPages = qMax(1, pagination.pages);

            ForumKit::clearLayout(m_listLayout);
            if (threads.isEmpty())
            {
                auto *empty = ForumKit::makeMuted(this, tr("TA 还没有发过主题"));
                empty->setAlignment(Qt::AlignCenter);
                m_listLayout->addSpacing(20);
                m_listLayout->addWidget(empty);
                m_listLayout->addSpacing(20);
            }
            else
            {
                for (const ForumThreadInfo &thread : threads)
                {
                    auto *row = new ForumKit::ClickableCard(this);
                    row->setObjectName(QStringLiteral("forumThreadRow"));
                    const int threadId = thread.id;
                    row->onClick = [this, threadId]() { emit threadOpenRequested(threadId); };

                    auto *rowLayout = new QVBoxLayout(row);
                    rowLayout->setContentsMargins(14, 11, 14, 11);
                    rowLayout->setSpacing(3);

                    auto *title = new ForumKit::ElidedLabel(row);
                    title->setObjectName(QStringLiteral("forumRowTitle"));
                    title->setFullText(thread.title);
                    rowLayout->addWidget(title);

                    QStringList metaParts;
                    if (!thread.boardName.isEmpty())
                        metaParts << thread.boardName;
                    metaParts << tr("%1 回复").arg(thread.replies);
                    if (thread.lastPostAt > 0)
                        metaParts << ForumKit::relativeTime(thread.lastPostAt);
                    rowLayout->addWidget(ForumKit::makeMuted(row, metaParts.join(QStringLiteral(" · "))));

                    m_listLayout->addWidget(row);
                }
            }
            m_pager->setPage(m_page, m_totalPages);
        },
        [this](const QString &message, int) {
            m_refreshButton->setEnabled(true);
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void ForumUserProfilePage::loadReplies(int page)
{
    if (m_userId <= 0)
        return;
    m_refreshButton->setEnabled(false);
    setStatus(tr("正在加载回复…"));

    ForumContentService::instance()->fetchUserPosts(
        m_userId, page,
        [this](const QVector<ForumUserReplyItem> &posts, const ForumPagination &pagination) {
            m_refreshButton->setEnabled(true);
            setStatus(QString());
            m_postTotal = qMax(m_postTotal, pagination.total);
            m_page = qMax(1, pagination.page);
            m_totalPages = qMax(1, pagination.pages);

            ForumKit::clearLayout(m_listLayout);
            if (posts.isEmpty())
            {
                auto *empty = ForumKit::makeMuted(this, tr("TA 还没有回复过帖子"));
                empty->setAlignment(Qt::AlignCenter);
                m_listLayout->addSpacing(20);
                m_listLayout->addWidget(empty);
                m_listLayout->addSpacing(20);
            }
            else
            {
                for (const ForumUserReplyItem &post : posts)
                {
                    auto *row = new ForumKit::ClickableCard(this);
                    row->setObjectName(QStringLiteral("forumThreadRow"));
                    const int threadId = post.threadId;
                    row->onClick = [this, threadId]() { emit threadOpenRequested(threadId); };

                    auto *rowLayout = new QVBoxLayout(row);
                    rowLayout->setContentsMargins(14, 11, 14, 11);
                    rowLayout->setSpacing(3);

                    auto *title = new ForumKit::ElidedLabel(row);
                    title->setObjectName(QStringLiteral("forumRowTitle"));
                    title->setFullText(post.threadTitle.isEmpty() ? tr("（无标题）")
                                                                  : post.threadTitle);
                    rowLayout->addWidget(title);

                    if (!post.snippet.isEmpty())
                        rowLayout->addWidget(ForumKit::makeMuted(row, post.snippet));

                    m_listLayout->addWidget(row);
                }
            }
            m_pager->setPage(m_page, m_totalPages);
        },
        [this](const QString &message, int) {
            m_refreshButton->setEnabled(true);
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void ForumUserProfilePage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}
