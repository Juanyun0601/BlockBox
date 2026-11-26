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
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "components/ForumPostBrowser.h"
#include "components/NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumChatService.h"
#include "utils/MedalCatalog.h"
#include "utils/StyleKit.h"
#include "utils/links/LinkRouter.h"

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

    m_followingTabButton = new QPushButton(tr("关注"), content);
    m_followingTabButton->setObjectName(QStringLiteral("forumSortButton"));
    m_followingTabButton->setCheckable(true);
    m_followingTabButton->setCursor(Qt::PointingHandCursor);
    m_followingTabButton->setFixedHeight(28);
    connect(m_followingTabButton, &QPushButton::clicked, this,
            &ForumUserProfilePage::showFollowingTab);
    tabRow->addWidget(m_followingTabButton);

    m_followersTabButton = new QPushButton(tr("粉丝"), content);
    m_followersTabButton->setObjectName(QStringLiteral("forumSortButton"));
    m_followersTabButton->setCheckable(true);
    m_followersTabButton->setCursor(Qt::PointingHandCursor);
    m_followersTabButton->setFixedHeight(28);
    connect(m_followersTabButton, &QPushButton::clicked, this,
            &ForumUserProfilePage::showFollowersTab);
    tabRow->addWidget(m_followersTabButton);
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
        else if (m_activeTab == 1)
            loadReplies(page);
        else
            loadFollows(m_activeTab == 3, page);
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
        // 同一个用户刷新当前页签即可
        if (m_activeTab == 0)
            loadThreads(m_page);
        else if (m_activeTab == 1)
            loadReplies(m_page);
        else
            loadFollows(m_activeTab == 3, m_page);
        return;
    }
    m_userId = userId;
    m_activeTab = 0;
    m_threadTotal = 0;
    m_postTotal = 0;
    m_followerCount = 0;
    m_followingCount = 0;
    m_isSelf = false;
    m_userFollowing = false;
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
            syncTabButtons();
            loadThreads(1);
        },
        [this](const QString &message, int) {
            m_refreshButton->setEnabled(true);
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void ForumUserProfilePage::loadProfileMedals()
{
    if (m_userId <= 0 || !m_profileMedalsHost)
        return;

    const int userId = m_userId;
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/users/%1/medals").arg(userId),
        [this, userId](const QJsonObject &root) {
            // 资料卡已经换成别人了：这份结果直接丢掉
            if (m_userId != userId || !m_profileMedalsHost)
                return;
            auto *row = qobject_cast<QHBoxLayout *>(m_profileMedalsHost->layout());
            if (!row)
                return;
            while (QLayoutItem *item = row->takeAt(0))
            {
                if (QWidget *widget = item->widget())
                    widget->deleteLater();
                delete item;
            }

            const QJsonArray medals = root.value(QStringLiteral("medals")).toArray();
            if (medals.isEmpty())
            {
                m_profileMedalsHost->hide();
                return;
            }

            auto *caption = ForumKit::makeMuted(m_profileMedalsHost, tr("勋章"));
            row->addWidget(caption, 0, Qt::AlignVCenter);

            int shown = 0;
            const int limit = 8;
            for (const QJsonValue &value : medals)
            {
                const QJsonObject medal = value.toObject();
                if (!medal.value(QStringLiteral("earned")).toBool())
                    continue;
                auto *icon = new QLabel(m_profileMedalsHost);
                icon->setFixedSize(26, 26);
                icon->setAlignment(Qt::AlignCenter);
                icon->setPixmap(
                    MedalCatalog::pixmapFor(medal.value(QStringLiteral("id")).toString(), 24));
                icon->setToolTip(medal.value(QStringLiteral("name")).toString());
                row->addWidget(icon, 0, Qt::AlignVCenter);
                if (++shown >= limit)
                    break;
            }

            const int earned =
                qMax(root.value(QStringLiteral("earned_count")).toInt(), medals.size());
            if (earned > shown)
            {
                row->addWidget(ForumKit::makeMuted(
                    m_profileMedalsHost, tr("+%1").arg(earned - shown)));
            }
            row->addStretch(1);
            m_profileMedalsHost->show();
        },
        [this, userId](const QString &, int) {
            if (m_userId == userId && m_profileMedalsHost)
                m_profileMedalsHost->hide();
        });
}

void ForumUserProfilePage::renderProfile(const ForumUserProfile &profile)
{
    if (m_coverBanner)
        m_coverBanner->setCover(profile.user.cover);

    m_isSelf = profile.friendship == QStringLiteral("self");
    m_userFollowing = profile.user.following;
    m_followerCount = profile.user.followerCount;
    m_followingCount = profile.user.followingCount;

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

    // 统计：关注 / 粉丝在前，后面两个是原来就有的主题 / 回复
    auto *statsRow = new QHBoxLayout();
    statsRow->setSpacing(24);
    struct StatItem
    {
        QString caption;
        int value;
        QLabel **store;
    };
    m_followingValue = nullptr;
    m_followersValue = nullptr;
    const QVector<StatItem> stats = {
        {tr("关注"), m_followingCount, &m_followingValue},
        {tr("粉丝"), m_followerCount, &m_followersValue},
        {tr("主题"), qMax(profile.user.threadCount, m_threadTotal), nullptr},
        {tr("回复"), qMax(profile.user.postCount, m_postTotal), nullptr},
    };
    for (const StatItem &stat : stats)
    {
        auto *column = new QVBoxLayout();
        column->setSpacing(0);
        auto *value = new QLabel(QString::number(stat.value), m_profileCard);
        value->setObjectName(QStringLiteral("forumStatValue"));
        value->setAlignment(Qt::AlignHCenter);
        if (stat.store)
            *stat.store = value;
        auto *caption = ForumKit::makeMuted(m_profileCard, stat.caption);
        caption->setAlignment(Qt::AlignHCenter);
        column->addWidget(value);
        column->addWidget(caption);
        statsRow->addLayout(column);
    }
    statsRow->addStretch(1);
    m_profileBody->addLayout(statsRow);

    // 已点亮的勋章：只读展示，点图标悬停能看名字；没有勋章时整行隐藏
    m_profileMedalsHost = new QWidget(m_profileCard);
    auto *medalsRow = new QHBoxLayout(m_profileMedalsHost);
    medalsRow->setContentsMargins(0, 2, 0, 0);
    medalsRow->setSpacing(6);
    m_profileMedalsHost->hide();
    m_profileBody->addWidget(m_profileMedalsHost);
    loadProfileMedals();

    // 关注 / 加好友 / 发私信
    renderProfileActions(profile);
}

void ForumUserProfilePage::handleBioLink(const QUrl &url)
{
    // 简介里的站内帖子链接在启动器内打开；CF / Modrinth 资源页也留在软件里，
    // 其余链接交给系统浏览器
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
    LinkRouter::instance()->open(url, this);
}

void ForumUserProfilePage::renderProfileActions(const ForumUserProfile &profile)
{
    m_friendship = profile.friendship;
    m_followButton = nullptr;
    m_friendButton = nullptr;
    m_chatButton = nullptr;

    // 自己的主页 / 被封禁的用户 / 未登录时不显示关注和好友操作
    if (m_friendship == QStringLiteral("self") ||
        profile.user.status == QStringLiteral("banned") ||
        !ForumAccountService::instance()->isLoggedIn())
        return;

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(8);

    // 关注是单向的：点一下就关注，再点取消，不用等对方同意
    m_followButton = new QPushButton(m_userFollowing ? tr("已关注") : tr("＋ 关注"), m_profileCard);
    m_followButton->setObjectName(m_userFollowing ? QStringLiteral("forumGhostButton")
                                                  : QStringLiteral("forumPrimaryButton"));
    m_followButton->setCursor(Qt::PointingHandCursor);
    m_followButton->setFixedHeight(30);
    connect(m_followButton, &QPushButton::clicked, this, [this]() {
        if (m_followButton)
            m_followButton->setEnabled(false);
        const bool follow = !m_userFollowing;
        ForumContentService::instance()->setUserFollow(
            m_userId, follow,
            [this](const ForumFollowResult &result) {
                updateFollowState(result.following, result.followerCount, result.followingCount);
                NotificationManager::showSuccess(
                    this, result.following ? tr("已关注") : tr("已取消关注"));
            },
            [this](const QString &message, int) {
                if (m_followButton)
                    m_followButton->setEnabled(true);
                NotificationManager::showError(this, message);
            });
    });
    actionRow->addWidget(m_followButton);

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

void ForumUserProfilePage::updateFollowState(bool following, int followerCount, int)
{
    m_userFollowing = following;
    m_followerCount = followerCount;

    if (m_followButton)
    {
        m_followButton->setEnabled(true);
        m_followButton->setText(following ? tr("已关注") : tr("＋ 关注"));
        // 已关注时换成描边按钮，和旁边的加好友按钮区分开
        m_followButton->setObjectName(following ? QStringLiteral("forumGhostButton")
                                                : QStringLiteral("forumPrimaryButton"));
        m_followButton->style()->unpolish(m_followButton);
        m_followButton->style()->polish(m_followButton);
    }
    // 关注 / 取关只改变 TA 的粉丝数；资料卡上的「关注」是 TA 关注了多少人，不受影响
    if (m_followersValue)
        m_followersValue->setText(QString::number(followerCount));
}

void ForumUserProfilePage::showThreadsTab()
{
    if (m_activeTab == 0)
        return;
    m_activeTab = 0;
    syncTabButtons();
    loadThreads(1);
}

void ForumUserProfilePage::showRepliesTab()
{
    if (m_activeTab == 1)
        return;
    m_activeTab = 1;
    syncTabButtons();
    loadReplies(1);
}

void ForumUserProfilePage::showFollowingTab()
{
    if (m_activeTab == 2)
        return;
    m_activeTab = 2;
    syncTabButtons();
    loadFollows(false, 1);
}

void ForumUserProfilePage::showFollowersTab()
{
    if (m_activeTab == 3)
        return;
    m_activeTab = 3;
    syncTabButtons();
    loadFollows(true, 1);
}

void ForumUserProfilePage::syncTabButtons()
{
    m_threadsTabButton->setChecked(m_activeTab == 0);
    m_repliesTabButton->setChecked(m_activeTab == 1);
    m_followingTabButton->setChecked(m_activeTab == 2);
    m_followersTabButton->setChecked(m_activeTab == 3);
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
                    metaParts << (thread.tutorial ? tr("%1 评论").arg(thread.comments)
                                                  : tr("%1 回复").arg(thread.replies));
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

void ForumUserProfilePage::loadFollows(bool followers, int page)
{
    if (m_userId <= 0)
        return;
    m_refreshButton->setEnabled(false);
    setStatus(followers ? tr("正在加载粉丝…") : tr("正在加载关注…"));

    ForumContentService::instance()->fetchUserFollows(
        m_userId, followers, page,
        [this, followers](const QVector<ForumUser> &users, const ForumPagination &pagination) {
            m_refreshButton->setEnabled(true);
            setStatus(QString());
            if (followers)
            {
                m_followerCount = pagination.total;
                if (m_followersValue)
                    m_followersValue->setText(QString::number(m_followerCount));
            }
            else
            {
                m_followingCount = pagination.total;
                if (m_followingValue)
                    m_followingValue->setText(QString::number(m_followingCount));
            }
            m_page = qMax(1, pagination.page);
            m_totalPages = qMax(1, pagination.pages);

            ForumKit::clearLayout(m_listLayout);
            if (users.isEmpty())
            {
                QString text;
                if (followers)
                    text = m_isSelf ? tr("还没有粉丝，多发帖、多回复让大家认识你")
                                    : tr("TA 还没有粉丝");
                else
                    text = m_isSelf
                               ? tr("还没有关注任何人，在别人的主页点「关注」就会出现在这里")
                               : tr("TA 还没有关注任何人");
                auto *empty = ForumKit::makeMuted(this, text);
                empty->setAlignment(Qt::AlignCenter);
                empty->setWordWrap(true);
                m_listLayout->addSpacing(20);
                m_listLayout->addWidget(empty);
                m_listLayout->addSpacing(20);
            }
            else
            {
                for (const ForumUser &user : users)
                {
                    auto *row = new ForumKit::ClickableCard(this);
                    row->setObjectName(QStringLiteral("forumThreadRow"));
                    const int userId = user.id;
                    row->onClick = [this, userId]() { emit userOpenRequested(userId); };

                    auto *rowLayout = new QHBoxLayout(row);
                    rowLayout->setContentsMargins(14, 10, 14, 10);
                    rowLayout->setSpacing(10);
                    rowLayout->addWidget(
                        ForumKit::makeAvatar(row, user.avatar, user.displayName, 38));

                    auto *textColumn = new QVBoxLayout();
                    textColumn->setSpacing(3);

                    auto *nameRow = new QHBoxLayout();
                    nameRow->setSpacing(6);
                    auto *name = new ForumKit::ElidedLabel(row);
                    name->setObjectName(QStringLiteral("forumRowTitle"));
                    name->setFullText(user.displayName);
                    nameRow->addWidget(name);
                    if (user.role == QStringLiteral("admin"))
                        nameRow->addWidget(ForumKit::makeBadge(row, tr("管理员")));
                    else if (user.role == QStringLiteral("moderator"))
                        nameRow->addWidget(ForumKit::makeBadge(row, tr("次级管理员")));
                    nameRow->addStretch(1);
                    textColumn->addLayout(nameRow);

                    // 第二行：@账号名 · 简介（简介太长就省略）
                    QString meta = QStringLiteral("@") + user.username;
                    const QString bio = user.bio.simplified();
                    if (!bio.isEmpty())
                        meta += QStringLiteral(" · ") + bio;
                    auto *line = new ForumKit::ElidedLabel(row);
                    line->setObjectName(QStringLiteral("forumMuted"));
                    line->setFullText(meta);
                    textColumn->addWidget(line);

                    rowLayout->addLayout(textColumn, 1);
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
