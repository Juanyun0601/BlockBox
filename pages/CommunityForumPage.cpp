/**
 * @file   CommunityForumPage.cpp
 * @brief  社区页实现 —— 用论坛开放接口渲染论坛首页（与网页版同款左右栏）
 * @author BlockBox Team
 * @date   2026-10-31
 */

#include "pages/CommunityForumPage.h"

#include <QBoxLayout>
#include <QDateTime>
#include <QDesktopServices>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "utils/ForumAccountService.h"
#include "utils/SettingsManager.h"
#include "utils/ForumAccountService.h"
#include "pages/MultiplayerLobbyPage.h"

namespace {

/// 网络抖动时最多发几次（含第一次）
constexpr int kMaxRequestAttempts = 3;
constexpr int kRequestRetryDelayMs = 400;

/** 整张卡片可点：点空白处也算点卡片 */
class ClickableCard : public QFrame
{
public:
    explicit ClickableCard(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        // 只有真的按在这张卡片上，后面的松开才算点击。
        // 列表重新排版/滚动时，Qt 会把松开事件补发到光标下新出现的卡片上，
        // 这种没有按下事件配合的松开不能当成点击。
        m_pressed = event->button() == Qt::LeftButton && rect().contains(event->pos());
        QFrame::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool pressed = m_pressed;
        m_pressed = false;
        if (pressed && event->button() == Qt::LeftButton && rect().contains(event->pos()) && onClick)
            onClick();
        QFrame::mouseReleaseEvent(event);
    }

private:
    bool m_pressed = false;
};

QString relativeTime(qint64 seconds)
{
    if (seconds <= 0)
        return QString();
    const qint64 diff = QDateTime::currentSecsSinceEpoch() - seconds;
    if (diff < 60)
        return QObject::tr("刚刚");
    if (diff < 3600)
        return QObject::tr("%1 分钟前").arg(diff / 60);
    if (diff < 86400)
        return QObject::tr("%1 小时前").arg(diff / 3600);
    if (diff < 86400 * 7)
        return QObject::tr("%1 天前").arg(diff / 86400);
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd"));
}

QString truncateText(const QString &text, int maxChars)
{
    const QString simplified = text.simplified();
    if (simplified.size() <= maxChars)
        return simplified;
    return simplified.left(maxChars) + QStringLiteral("…");
}

QLabel *makeMutedLabel(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    // 注意：不要用 advancedHintLabel，那是启动器里"红色警告"的样式
    label->setObjectName(QStringLiteral("communityMuted"));
    label->setWordWrap(true);
    return label;
}

QLabel *makeSectionTitle(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("communitySectionTitle"));
    return label;
}

QFrame *makeDivider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("communityDivider"));
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    return line;
}

QFrame *makeCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("communityCard"));
    return card;
}

} // namespace

CommunityForumPage::CommunityForumPage(QWidget *parent)
    : QWidget(parent)
{
    m_network = new QNetworkAccessManager(this);
    setAttribute(Qt::WA_StyledBackground, false);

    buildUi();
    applySiteUrl();

    // 登录态变化时切换「通知 / 登录」按钮
    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            &CommunityForumPage::updateHeroButtons);

    // 社区页里的「联机大厅」是子页面：在页面内切换，不再跳独立页
    connect(this, &CommunityForumPage::lobbyOpenRequested, this, &CommunityForumPage::showLobbyPage);

    connect(SettingsManager::instance(), &SettingsManager::forumBaseUrlChanged, this, [this]() {
        m_loadedOnce = false;
        refresh();
    });
}

CommunityForumPage::~CommunityForumPage() = default;

void CommunityForumPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── 页面顶部加载进度条：数据加载期间显示，加载完自动收起 ──
    // 固定占 3px 高度，隐藏时只是变透明，页面内容不会上下跳动
    auto *loadingStrip = new QWidget(this);
    loadingStrip->setObjectName(QStringLiteral("communityLoadingStrip"));
    loadingStrip->setFixedHeight(3);
    auto *loadingStripLayout = new QVBoxLayout(loadingStrip);
    loadingStripLayout->setContentsMargins(0, 0, 0, 0);
    loadingStripLayout->setSpacing(0);

    m_loadingBar = new QProgressBar(loadingStrip);
    m_loadingBar->setObjectName(QStringLiteral("communityLoadingBar"));
    m_loadingBar->setRange(0, 100);
    m_loadingBar->setValue(0);
    m_loadingBar->setTextVisible(false);
    m_loadingBar->setFixedHeight(3);
    m_loadingBar->setVisible(false);
    loadingStripLayout->addWidget(m_loadingBar);

    m_loadingAnimation = new QPropertyAnimation(m_loadingBar, "value", this);
    m_loadingAnimation->setDuration(260);
    m_loadingAnimation->setEasingCurve(QEasingCurve::OutCubic);

    outer->addWidget(loadingStrip);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(scrollArea);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 18, 28, 28);
    layout->setSpacing(12);

    // ── 头图：论坛名 + 简介 + 地址 + 操作 ──
    auto *hero = new QFrame(content);
    hero->setObjectName(QStringLiteral("communityHero"));
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(22, 18, 22, 16);
    heroLayout->setSpacing(6);

    auto *heroTopRow = new QHBoxLayout();
    heroTopRow->setSpacing(10);

    auto *heroTextColumn = new QVBoxLayout();
    heroTextColumn->setSpacing(2);

    m_titleLabel = new QLabel(tr("方块盒子论坛"), hero);
    m_titleLabel->setObjectName(QStringLiteral("communityHeroTitle"));
    heroTextColumn->addWidget(m_titleLabel);

    auto *heroSub = new QLabel(tr("使用求助、玩法分享、插件开发、建议反馈，都在这里聊"), hero);
    heroSub->setObjectName(QStringLiteral("communityHeroSub"));
    heroSub->setWordWrap(true);
    heroTextColumn->addWidget(heroSub);

    heroTopRow->addLayout(heroTextColumn, 1);

    m_statusLabel = new QLabel(QString(), hero);
    m_statusLabel->setObjectName(QStringLiteral("communityHeroMeta"));
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);
    heroTopRow->addWidget(m_statusLabel);

    heroLayout->addLayout(heroTopRow);

    auto *heroBottomRow = new QHBoxLayout();
    heroBottomRow->setSpacing(8);

    m_urlLabel = new QLabel(QString(), hero);
    m_urlLabel->setObjectName(QStringLiteral("communityHeroMeta"));
    heroBottomRow->addWidget(m_urlLabel, 1);

    m_refreshButton = new QPushButton(tr("刷新"), hero);
    m_refreshButton->setObjectName(QStringLiteral("communityHeroButton"));
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setFixedHeight(30);
    connect(m_refreshButton, &QPushButton::clicked, this, &CommunityForumPage::refresh);
    heroBottomRow->addWidget(m_refreshButton);

    auto *openButton = new QPushButton(tr("打开网页"), hero);
    openButton->setObjectName(QStringLiteral("communityHeroButton"));
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setFixedHeight(30);
    connect(openButton, &QPushButton::clicked, this, [this]() {
        if (!m_baseUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(m_baseUrl));
    });
    heroBottomRow->addWidget(openButton);

    // 发帖：直接在启动器里写新主题，不再去网页版
    m_composeButton = new QPushButton(tr("发帖"), hero);
    m_composeButton->setObjectName(QStringLiteral("communityHeroButton"));
    m_composeButton->setCursor(Qt::PointingHandCursor);
    m_composeButton->setFixedHeight(30);
    connect(m_composeButton, &QPushButton::clicked, this, &CommunityForumPage::composeRequested);
    heroBottomRow->addWidget(m_composeButton);

    // 好友：打开启动器里的好友与私信页（登录后显示）
    m_friendsButton = new QPushButton(tr("好友"), hero);
    m_friendsButton->setObjectName(QStringLiteral("communityHeroButton"));
    m_friendsButton->setCursor(Qt::PointingHandCursor);
    m_friendsButton->setFixedHeight(30);
    connect(m_friendsButton, &QPushButton::clicked, this, &CommunityForumPage::friendsOpenRequested);
    heroBottomRow->addWidget(m_friendsButton);

    // 通知 / 登录：按登录态显示其中一个
    m_notifyButton = new QPushButton(tr("通知"), hero);
    m_notifyButton->setObjectName(QStringLiteral("communityHeroButton"));
    m_notifyButton->setCursor(Qt::PointingHandCursor);
    m_notifyButton->setFixedHeight(30);
    connect(m_notifyButton, &QPushButton::clicked, this,
            &CommunityForumPage::notificationsRequested);
    heroBottomRow->addWidget(m_notifyButton);

    m_loginButton = new QPushButton(tr("登录"), hero);
    m_loginButton->setObjectName(QStringLiteral("communityHeroButton"));
    m_loginButton->setCursor(Qt::PointingHandCursor);
    m_loginButton->setFixedHeight(30);
    connect(m_loginButton, &QPushButton::clicked, this, &CommunityForumPage::loginRequested);
    heroBottomRow->addWidget(m_loginButton);
    updateHeroButtons();

    heroLayout->addLayout(heroBottomRow);
    layout->addWidget(hero);

    // ── 数据行（通栏，四等分） ──
    auto *statsCard = makeCard(content);
    auto *statsCardLayout = new QGridLayout(statsCard);
    statsCardLayout->setContentsMargins(22, 12, 22, 12);
    statsCardLayout->setHorizontalSpacing(0);
    statsCardLayout->setVerticalSpacing(0);
    m_statsLayout = statsCardLayout;
    layout->addWidget(statsCard);

    layout->addSpacing(4);

    // ── 左右两栏 ──
    auto *columns = new QBoxLayout(QBoxLayout::LeftToRight);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(16);
    m_columnsLayout = columns;

    // 左栏：板块 + 最新讨论
    auto *leftColumn = new QWidget(content);
    leftColumn->setAttribute(Qt::WA_StyledBackground, false);
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    leftLayout->addWidget(makeSectionTitle(tr("板块"), leftColumn));
    auto *boardsCard = makeCard(leftColumn);
    m_boardsLayout = new QVBoxLayout(boardsCard);
    m_boardsLayout->setContentsMargins(12, 12, 12, 12);
    m_boardsLayout->setSpacing(8);
    m_boardsLayout->addWidget(makeMutedLabel(tr("正在加载板块…"), boardsCard));
    leftLayout->addWidget(boardsCard);

    // 联机大厅：左栏显眼位置放一小块在线房间，点开进实例助手的大厅页
    leftLayout->addSpacing(6);
    auto *lobbyTitleRow = new QHBoxLayout();
    lobbyTitleRow->setContentsMargins(0, 0, 0, 0);
    lobbyTitleRow->setSpacing(8);
    lobbyTitleRow->addWidget(makeSectionTitle(tr("联机大厅"), leftColumn));
    lobbyTitleRow->addStretch(1);
    auto *lobbyOpenButton = new QPushButton(tr("进入大厅 →"), leftColumn);
    lobbyOpenButton->setObjectName(QStringLiteral("communityTag"));
    lobbyOpenButton->setCursor(Qt::PointingHandCursor);
    lobbyOpenButton->setFlat(true);
    lobbyOpenButton->setFixedHeight(26);
    connect(lobbyOpenButton, &QPushButton::clicked, this, &CommunityForumPage::lobbyOpenRequested);
    lobbyTitleRow->addWidget(lobbyOpenButton);
    leftLayout->addLayout(lobbyTitleRow);

    auto *lobbyCard = makeCard(leftColumn);
    auto *lobbyCardLayout = new QVBoxLayout(lobbyCard);
    lobbyCardLayout->setContentsMargins(10, 6, 10, 8);
    lobbyCardLayout->setSpacing(4);
    m_lobbyLayout = new QVBoxLayout();
    m_lobbyLayout->setContentsMargins(0, 0, 0, 0);
    m_lobbyLayout->setSpacing(2);
    lobbyCardLayout->addLayout(m_lobbyLayout);
    m_lobbyHint = makeMutedLabel(tr("正在加载联机大厅…"), lobbyCard);
    m_lobbyHint->setWordWrap(true);
    lobbyCardLayout->addWidget(m_lobbyHint);
    leftLayout->addWidget(lobbyCard);

    leftLayout->addSpacing(6);
    leftLayout->addWidget(makeSectionTitle(tr("最新讨论"), leftColumn));
    auto *threadsCard = makeCard(leftColumn);
    m_threadsLayout = new QVBoxLayout(threadsCard);
    m_threadsLayout->setContentsMargins(10, 8, 10, 8);
    m_threadsLayout->setSpacing(2);
    m_threadsLayout->addWidget(makeMutedLabel(tr("正在加载主题…"), threadsCard));
    leftLayout->addWidget(threadsCard);
    leftLayout->addStretch(1);

    columns->addWidget(leftColumn, 1);

    // 右栏：社区情况 + 热门标签 + 发帖小提示
    auto *rightColumn = new QWidget(content);
    rightColumn->setAttribute(Qt::WA_StyledBackground, false);
    rightColumn->setFixedWidth(300);
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);

    auto *summaryCard = makeCard(rightColumn);
    auto *summaryLayout = new QVBoxLayout(summaryCard);
    summaryLayout->setContentsMargins(16, 14, 16, 14);
    summaryLayout->setSpacing(8);
    summaryLayout->addWidget(makeSectionTitle(tr("社区情况"), summaryCard));
    m_sideSummary = makeMutedLabel(QString(), summaryCard);
    m_sideSummary->setWordWrap(true);
    summaryLayout->addWidget(m_sideSummary);
    rightLayout->addWidget(summaryCard);

    auto *tagsCard = makeCard(rightColumn);
    auto *tagsCardLayout = new QVBoxLayout(tagsCard);
    tagsCardLayout->setContentsMargins(16, 14, 16, 14);
    tagsCardLayout->setSpacing(8);
    tagsCardLayout->addWidget(makeSectionTitle(tr("热门标签"), tagsCard));
    m_tagsLayout = new QVBoxLayout();
    m_tagsLayout->setContentsMargins(0, 0, 0, 0);
    m_tagsLayout->setSpacing(8);
    m_tagsLayout->addWidget(makeMutedLabel(tr("还没有标签"), tagsCard));
    tagsCardLayout->addLayout(m_tagsLayout);
    rightLayout->addWidget(tagsCard);

    auto *tipsCard = makeCard(rightColumn);
    auto *tipsLayout = new QVBoxLayout(tipsCard);
    tipsLayout->setContentsMargins(16, 14, 16, 14);
    tipsLayout->setSpacing(6);
    tipsLayout->addWidget(makeSectionTitle(tr("发帖小提示"), tipsCard));
    const QStringList tips = {
        tr("· 标题写清现象，比如「1.20.4 启动报 Java 找不到」"),
        tr("· 求助时带上系统、Java 版本和报错截图"),
        tr("· 支持 Markdown：加粗、代码、引用"),
        tr("· 帖子可以加标签，方便别人找到"),
    };
    for (const QString &tip : tips)
        tipsLayout->addWidget(makeMutedLabel(tip, tipsCard));
    rightLayout->addWidget(tipsCard);

    rightLayout->addStretch(1);

    columns->addWidget(rightColumn);
    m_rightColumn = rightColumn;

    layout->addLayout(columns);
    layout->addStretch(1);

    scrollArea->setWidget(content);
    outer->addWidget(scrollArea);
}

void CommunityForumPage::applySiteUrl()
{
    m_baseUrl = SettingsManager::instance()->forumBaseUrl();
    if (m_urlLabel)
    {
        m_urlLabel->setText(tr("论坛地址：%1").arg(m_baseUrl));
    }
}

void CommunityForumPage::updateHeroButtons()
{
    const bool loggedIn = ForumAccountService::instance()->isLoggedIn();
    if (m_friendsButton)
        m_friendsButton->setVisible(loggedIn);
    if (m_notifyButton)
        m_notifyButton->setVisible(loggedIn);
    if (m_loginButton)
        m_loginButton->setVisible(!loggedIn);
}

QString CommunityForumPage::forumUrl(const QString &path) const
{
    QString base = m_baseUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (path.isEmpty())
        return base;
    return base + (path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path);
}

void CommunityForumPage::openUrl(const QString &path)
{
    QDesktopServices::openUrl(QUrl(forumUrl(path)));
}

void CommunityForumPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_loadedOnce)
    {
        m_loadedOnce = true;
        refresh();
    }
}

void CommunityForumPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // 窄窗口（手机 / 小窗）改成上下堆叠
    const bool stack = width() < 980;
    if (m_columnsLayout)
    {
        const QBoxLayout::Direction wanted = stack ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
        if (m_columnsLayout->direction() != wanted)
        {
            m_columnsLayout->setDirection(wanted);
            if (m_rightColumn)
            {
                if (stack)
                {
                    m_rightColumn->setMinimumWidth(0);
                    m_rightColumn->setMaximumWidth(QWIDGETSIZE_MAX);
                }
                else
                {
                    m_rightColumn->setFixedWidth(300);
                }
            }
        }
    }

    // 板块卡片：宽屏两列，窄屏一列
    const int columns = width() < 900 ? 1 : 2;
    if (columns != m_boardColumns)
    {
        m_boardColumns = columns;
        if (!m_boards.isEmpty())
            renderBoards();
    }

    if (m_lobbyOverlay)
        m_lobbyOverlay->setGeometry(rect());
}

void CommunityForumPage::refresh()
{
    // 大厅子页面打开时，顶栏刷新改为刷新大厅
    if (m_lobbyOverlay && m_lobbyOverlay->isVisible() && m_lobbyPage)
    {
        m_lobbyPage->refresh();
        return;
    }

    if (m_pendingRequests > 0)
        return;

    applySiteUrl();
    m_boards.clear();
    m_threads.clear();
    m_tags.clear();
    m_lobbyRooms.clear();
    m_lobbyError.clear();
    m_siteUsers = m_siteThreads = m_sitePosts = m_siteOnline = 0;
    m_siteNewUsers = 0;
    m_newestUserName.clear();
    m_failedRequests = 0;
    m_pendingRequests = 3;
    m_loadedOnce = true;

    // 页面顶部进度条：3 个论坛接口全部返回后收起
    m_loadTotalSteps = 3;
    m_loadDoneSteps = 0;
    if (m_loadingBar)
    {
        m_loadingAnimation->stop();
        m_loadingBar->setValue(0);
        m_loadingBar->setVisible(true);
    }

    if (m_refreshButton)
        m_refreshButton->setEnabled(false);
    setStatus(tr("正在连接论坛…"));
    fetchLobby();

    requestJson(QStringLiteral("/api/boards"), [this](const QJsonDocument &doc) {
        const QJsonObject root = doc.object();
        const QJsonArray boards = root.value(QStringLiteral("boards")).toArray();
        for (const QJsonValue &value : boards)
        {
            const QJsonObject item = value.toObject();
            BoardInfo info;
            info.name = item.value(QStringLiteral("name")).toString();
            info.slug = item.value(QStringLiteral("slug")).toString();
            info.description = item.value(QStringLiteral("description")).toString();
            info.icon = item.value(QStringLiteral("icon")).toString();
            info.threads = item.value(QStringLiteral("thread_count")).toInt();
            info.posts = item.value(QStringLiteral("post_count")).toInt();
            m_boards.append(info);
        }
        const QJsonObject stats = root.value(QStringLiteral("stats")).toObject();
        m_siteUsers = stats.value(QStringLiteral("users")).toInt();
        m_siteThreads = stats.value(QStringLiteral("threads")).toInt();
        m_sitePosts = stats.value(QStringLiteral("posts")).toInt();
        m_siteOnline = stats.value(QStringLiteral("online")).toInt();
        m_siteNewUsers = stats.value(QStringLiteral("new_users")).toInt();
        const QJsonObject newest = stats.value(QStringLiteral("newest_user")).toObject();
        m_newestUserName = newest.value(QStringLiteral("display_name")).toString();
        if (m_newestUserName.isEmpty())
            m_newestUserName = newest.value(QStringLiteral("username")).toString();
    });

    requestJson(QStringLiteral("/api/threads?sort=latest&per_page=8"), [this](const QJsonDocument &doc) {
        const QJsonArray threads = doc.object().value(QStringLiteral("threads")).toArray();
        for (const QJsonValue &value : threads)
        {
            const QJsonObject item = value.toObject();
            ThreadInfo info;
            info.id = item.value(QStringLiteral("id")).toInt();
            info.title = item.value(QStringLiteral("title")).toString();
            info.board = item.value(QStringLiteral("board")).toObject().value(QStringLiteral("name")).toString();
            const QJsonObject author = item.value(QStringLiteral("author")).toObject();
            info.author = author.value(QStringLiteral("display_name")).toString();
            if (info.author.isEmpty())
                info.author = author.value(QStringLiteral("username")).toString();
            info.replies = item.value(QStringLiteral("reply_count")).toInt();
            info.views = item.value(QStringLiteral("views")).toInt();
            info.lastPostAt = static_cast<qint64>(item.value(QStringLiteral("last_post_at")).toDouble());
            m_threads.append(info);
        }
    });

    requestJson(QStringLiteral("/api/tags"), [this](const QJsonDocument &doc) {
        const QJsonArray tags = doc.object().value(QStringLiteral("tags")).toArray();
        for (const QJsonValue &value : tags)
        {
            const QJsonObject item = value.toObject();
            TagInfo info;
            info.tag = item.value(QStringLiteral("tag")).toString();
            info.count = item.value(QStringLiteral("n")).toInt();
            m_tags.append(info);
        }
    });
}

void CommunityForumPage::requestJson(const QString &path,
                                     const std::function<void(const QJsonDocument &)> &onSuccess)
{
    sendJsonRequest(path, onSuccess, 1);
}

void CommunityForumPage::sendJsonRequest(
    const QString &path, const std::function<void(const QJsonDocument &)> &onSuccess, int attempt)
{
    QNetworkRequest request{QUrl(forumUrl(path))};
    request.setTransferTimeout(8000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
    // 重试时换一条新连接、不走 HTTP/2：个别网络会把 HTTP/2 连接直接掐断
    if (attempt > 1)
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, onSuccess, attempt]() {
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString networkDetail = reply->errorString();
        const QByteArray raw = reply->readAll();
        reply->deleteLater();

        if (networkError != QNetworkReply::NoError)
        {
            // 连接被重置 / 握手失败 / 超时：多半是抖动，悄悄再试一次，别让用户看到"连不上论坛"
            if (attempt < kMaxRequestAttempts && ForumAccountService::isTransientNetworkError(
                                                     int(networkError)))
            {
                qWarning() << "[CommunityForum] 板块数据请求失败，准备重试:" << networkDetail;
                QTimer::singleShot(kRequestRetryDelayMs, this, [this, path, onSuccess, attempt]() {
                    m_network->clearConnectionCache();
                    sendJsonRequest(path, onSuccess, attempt + 1);
                });
                return;
            }

            qWarning() << "[CommunityForum] 连不上论坛:" << forumUrl(path) << networkDetail;
            m_failedRequests += 1;
            if (m_failedRequests == 1)
                setStatus(tr("连接失败：%1").arg(networkDetail), true);
            finishRequest();
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            m_failedRequests += 1;
            if (m_failedRequests == 1)
                setStatus(tr("返回的数据无法解析，确认地址指向的是方块盒子论坛"), true);
            finishRequest();
            return;
        }

        if (onSuccess)
            onSuccess(doc);
        finishRequest();
    });
}

void CommunityForumPage::finishRequest()
{
    m_pendingRequests -= 1;

    // 每完成一个接口就推进一格进度
    m_loadDoneSteps += 1;
    updateLoadingBar();

    if (m_pendingRequests > 0)
        return;

    if (m_refreshButton)
        m_refreshButton->setEnabled(true);

    if (m_failedRequests == 0)
        setStatus(tr("已连接 · %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"))));
    else if (m_failedRequests >= 3)
        setStatus(tr("连不上论坛，检查一下网络，或者点「刷新」再试"), true);
    else
        setStatus(tr("部分内容加载失败，可以点「刷新」再试"), true);

    renderAll();
}

void CommunityForumPage::updateLoadingBar()
{
    if (!m_loadingBar)
        return;

    const int total = qMax(1, m_loadTotalSteps);
    const int done = qBound(0, m_loadDoneSteps, total);
    const int target = (done * 100) / total;

    if (m_loadingAnimation)
    {
        m_loadingAnimation->stop();
        m_loadingAnimation->setStartValue(m_loadingBar->value());
        m_loadingAnimation->setEndValue(target);
        m_loadingAnimation->start();
    }
    else
    {
        m_loadingBar->setValue(target);
    }

    // 走满后再淡出，避免进度条没到头就消失
    if (done >= total)
    {
        QTimer::singleShot(320, this, [this]() {
            if (!m_loadingBar || m_loadDoneSteps < m_loadTotalSteps || m_pendingRequests > 0)
                return;
            m_loadingBar->setVisible(false);
        });
    }
}

void CommunityForumPage::setStatus(const QString &text, bool isError)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    // 头图是绿色渐变，用白色/浅琥珀提示，避免在深色底上看不清
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color:#ffe082;")
                                         : QStringLiteral("color:rgba(255,255,255,0.82);"));
}

void CommunityForumPage::clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        else if (QLayout *child = item->layout())
            clearLayout(child);
        delete item;
    }
}

void CommunityForumPage::renderAll()
{
    renderStats();
    renderBoards();
    renderLobby();
    renderThreads();
    renderTags();
}

void CommunityForumPage::renderStats()
{
    clearLayout(m_statsLayout);

    const QVector<QPair<QString, QString>> items = {
        {QString::number(m_siteUsers), tr("注册成员")},
        {QString::number(m_siteThreads), tr("主题")},
        {QString::number(m_sitePosts), tr("回复")},
        {QString::number(m_siteOnline), tr("15 分钟在线")},
    };

    // 四格等宽：格子在偶数列，分隔线在奇数列（列 stretch 为 0）
    for (int i = 0; i < items.size(); ++i)
    {
        const int column = i * 2;

        if (i > 0)
        {
            auto *divider = new QFrame(this);
            divider->setObjectName(QStringLiteral("communityStatDivider"));
            divider->setFrameShape(QFrame::NoFrame);
            divider->setFixedSize(1, 32);
            m_statsLayout->addWidget(divider, 0, column - 1, 2, 1, Qt::AlignVCenter);
            m_statsLayout->setColumnStretch(column - 1, 0);
        }

        auto *value = new QLabel(items[i].first, this);
        value->setObjectName(QStringLiteral("communityStatValue"));
        value->setAlignment(Qt::AlignHCenter);
        auto *caption = new QLabel(items[i].second, this);
        caption->setObjectName(QStringLiteral("communityMuted"));
        caption->setAlignment(Qt::AlignHCenter);

        m_statsLayout->addWidget(value, 0, column);
        m_statsLayout->addWidget(caption, 1, column);
        m_statsLayout->setColumnStretch(column, 1);
    }

    // 右栏「社区情况」
    if (m_sideSummary)
    {
        const QString newest = m_newestUserName.isEmpty() ? tr("暂无") : m_newestUserName;
        m_sideSummary->setText(
            tr("· 最近 24 小时新增 %1 位成员\n· 新加入：%2\n· 共 %3 位成员、%4 个主题")
                .arg(m_siteNewUsers)
                .arg(newest)
                .arg(m_siteUsers)
                .arg(m_siteThreads));
    }
}

void CommunityForumPage::renderBoards()
{
    clearLayout(m_boardsLayout);

    if (m_boards.isEmpty())
    {
        m_boardsLayout->addWidget(makeMutedLabel(tr("没有拿到板块数据，稍后刷新再试"), this));
        return;
    }

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    const int columns = m_boardColumns > 0 ? m_boardColumns : 1;
    for (int i = 0; i < columns; ++i)
        grid->setColumnStretch(i, 1);

    int index = 0;
    for (const BoardInfo &board : m_boards)
    {
        auto *card = new ClickableCard(this);
        card->setObjectName(QStringLiteral("communityBoardCard"));
        card->onClick = [this, slug = board.slug, name = board.name]() {
            emit boardOpenRequested(slug, name);
        };

        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(10, 9, 10, 9);
        cardLayout->setSpacing(10);

        auto *icon = new QLabel(board.icon.isEmpty() ? QStringLiteral("💬") : board.icon, card);
        icon->setObjectName(QStringLiteral("communityBoardIcon"));
        icon->setAlignment(Qt::AlignCenter);
        icon->setFixedSize(34, 34);
        cardLayout->addWidget(icon);

        auto *textLayout = new QVBoxLayout();
        textLayout->setSpacing(1);
        auto *name = new QLabel(board.name, card);
        name->setObjectName(QStringLiteral("communityBoardName"));
        auto *desc = new QLabel(truncateText(board.description, columns > 1 ? 18 : 26), card);
        desc->setObjectName(QStringLiteral("communityMuted"));
        textLayout->addWidget(name);
        textLayout->addWidget(desc);
        cardLayout->addLayout(textLayout, 1);

        auto *count = new QLabel(tr("%1 主题").arg(board.threads), card);
        count->setObjectName(QStringLiteral("communityBoardCount"));
        count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cardLayout->addWidget(count);

        grid->addWidget(card, index / columns, index % columns);
        ++index;
    }

    m_boardsLayout->addLayout(grid);
}

void CommunityForumPage::showLobbyPage()
{
    if (!m_lobbyOverlay)
    {
        m_lobbyOverlay = new QWidget(this);
        m_lobbyOverlay->setObjectName(QStringLiteral("communityLobbyOverlay"));
        m_lobbyOverlay->setAttribute(Qt::WA_StyledBackground, true);
        m_lobbyOverlay->setStyleSheet(
            QStringLiteral("QWidget#communityLobbyOverlay{background:palette(base);}"));

        auto *layout = new QVBoxLayout(m_lobbyOverlay);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_lobbyPage = new MultiplayerLobbyPage(m_lobbyOverlay);
        layout->addWidget(m_lobbyPage, 1);

        connect(m_lobbyPage, &MultiplayerLobbyPage::joinGameRequested, this,
                &CommunityForumPage::joinGameRequested);
        connect(m_lobbyPage, &MultiplayerLobbyPage::openInstanceAssistantRequested, this,
                &CommunityForumPage::openInstanceAssistantRequested);
        connect(m_lobbyPage, &MultiplayerLobbyPage::forumLoginRequested, this,
                &CommunityForumPage::loginRequested);
    }

    m_lobbyOverlay->setGeometry(rect());
    m_lobbyOverlay->show();
    m_lobbyOverlay->raise();
    if (m_lobbyPage)
        m_lobbyPage->refresh();
}

void CommunityForumPage::hideLobbyPage()
{
    if (m_lobbyOverlay)
        m_lobbyOverlay->hide();
}

bool CommunityForumPage::closeLobbyIfVisible()
{
    if (!m_lobbyOverlay || !m_lobbyOverlay->isVisible())
        return false;
    hideLobbyPage();
    return true;
}

void CommunityForumPage::fetchLobby()
{
    m_lobbyLoaded = false;
    m_lobbyError.clear();
    if (m_lobbyHint)
        m_lobbyHint->setText(tr("正在加载联机大厅…"));

    ForumLobbyService::instance()->fetchRooms(
        QString(), QString(),
        [this](const QVector<ForumLobbyRoom> &rooms, int) {
            m_lobbyRooms = rooms;
            m_lobbyLoaded = true;
            renderLobby();
        },
        [this](const QString &message, int) {
            m_lobbyRooms.clear();
            m_lobbyLoaded = true;
            m_lobbyError = message;
            renderLobby();
        });
}

void CommunityForumPage::renderLobby()
{
    if (!m_lobbyLayout)
        return;

    clearLayout(m_lobbyLayout);

    constexpr int kMaxRows = 4;
    const int count = qMin(m_lobbyRooms.size(), kMaxRows);
    for (int i = 0; i < count; ++i)
    {
        const ForumLobbyRoom &room = m_lobbyRooms.at(i);

        auto *row = new ClickableCard(this);
        row->setObjectName(QStringLiteral("communityThreadRow"));
        row->onClick = [this]() { emit lobbyOpenRequested(); };

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 8, 8, 8);
        rowLayout->setSpacing(10);

        auto *chip = new QLabel(room.coreLabel.isEmpty()
                                    ? ForumLobbyService::coreDisplayName(room.core)
                                    : room.coreLabel,
                                row);
        chip->setObjectName(QStringLiteral("communityLobbyChip"));
        chip->setAlignment(Qt::AlignCenter);
        rowLayout->addWidget(chip);

        auto *textLayout = new QVBoxLayout();
        textLayout->setSpacing(1);
        auto *title = new QLabel(truncateText(room.title, 30), row);
        title->setObjectName(QStringLiteral("communityLobbyTitle"));

        QStringList metaParts;
        if (!room.gameVersion.isEmpty())
            metaParts << room.gameVersion;
        if (!room.loader.isEmpty())
            metaParts << room.loader;
        const QString hostName =
            room.host.displayName.isEmpty() ? room.host.username : room.host.displayName;
        if (!hostName.isEmpty())
            metaParts << tr("房主 %1").arg(hostName);
        metaParts << tr("%1 人").arg(room.joinCount);
        auto *meta = new QLabel(metaParts.join(QStringLiteral(" · ")), row);
        meta->setObjectName(QStringLiteral("communityMuted"));

        textLayout->addWidget(title);
        textLayout->addWidget(meta);
        rowLayout->addLayout(textLayout, 1);

        const QString code = room.joinTarget();
        if (!code.isEmpty())
        {
            auto *codeLabel = new QLabel(code, row);
            codeLabel->setObjectName(QStringLiteral("communityLobbyCode"));
            codeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            codeLabel->setToolTip(tr("邀请码 / 连接地址，进大厅后可一键加入"));
            rowLayout->addWidget(codeLabel);
        }

        m_lobbyLayout->addWidget(row);
    }

    if (!m_lobbyHint)
        return;

    if (!m_lobbyError.isEmpty())
        m_lobbyHint->setText(tr("联机大厅暂时连不上：%1").arg(m_lobbyError));
    else if (!m_lobbyLoaded)
        m_lobbyHint->setText(tr("正在加载联机大厅…"));
    else if (m_lobbyRooms.isEmpty())
        m_lobbyHint->setText(
            tr("现在还没有人开房间。在实例助手的联机页点「发布到联机大厅」，房间就会出现在这里。"));
    else
        m_lobbyHint->setText(
            tr("共 %1 个房间 · 进入大厅后按版本、加载器和模组自动挑最新的实例，一键加入")
                .arg(m_lobbyRooms.size()));
}

void CommunityForumPage::renderThreads()
{
    clearLayout(m_threadsLayout);

    if (m_threads.isEmpty())
    {
        m_threadsLayout->addWidget(makeMutedLabel(tr("还没有人发帖，去论坛发第一帖吧"), this));
        return;
    }

    for (int i = 0; i < m_threads.size(); ++i)
    {
        const ThreadInfo &thread = m_threads[i];
        if (i > 0)
            m_threadsLayout->addWidget(makeDivider(this));

        auto *row = new ClickableCard(this);
        row->setObjectName(QStringLiteral("communityThreadRow"));
        row->onClick = [this, id = thread.id]() {
            emit threadOpenRequested(id);
        };

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 9, 8, 9);
        rowLayout->setSpacing(12);

        auto *textLayout = new QVBoxLayout();
        textLayout->setSpacing(2);
        auto *title = new QLabel(truncateText(thread.title, 40), row);
        title->setObjectName(QStringLiteral("communityThreadTitle"));
        auto *meta = new QLabel(
            tr("%1 · %2 · %3")
                .arg(thread.board.isEmpty() ? tr("综合讨论") : thread.board,
                     thread.author.isEmpty() ? tr("匿名") : thread.author,
                     relativeTime(thread.lastPostAt)),
            row);
        meta->setObjectName(QStringLiteral("communityMuted"));
        textLayout->addWidget(title);
        textLayout->addWidget(meta);
        rowLayout->addLayout(textLayout, 1);

        auto *counts = new QHBoxLayout();
        counts->setSpacing(14);
        const QVector<QPair<QString, QString>> statItems = {
            {QString::number(thread.replies), tr("回复")},
            {QString::number(thread.views), tr("浏览")},
        };
        for (const auto &stat : statItems)
        {
            auto *column = new QVBoxLayout();
            column->setSpacing(0);
            auto *value = new QLabel(stat.first, row);
            value->setObjectName(QStringLiteral("communityThreadCount"));
            value->setAlignment(Qt::AlignRight);
            auto *caption = new QLabel(stat.second, row);
            caption->setObjectName(QStringLiteral("communityMuted"));
            caption->setAlignment(Qt::AlignRight);
            column->addWidget(value);
            column->addWidget(caption);
            counts->addLayout(column);
        }
        rowLayout->addLayout(counts);

        m_threadsLayout->addWidget(row);
    }
}

void CommunityForumPage::renderTags()
{
    clearLayout(m_tagsLayout);

    if (m_tags.isEmpty())
    {
        m_tagsLayout->addWidget(makeMutedLabel(tr("还没有标签，发帖时加上就会出现在这里"), this));
        return;
    }

    // 右栏宽度有限，一行放两个标签
    auto *rowLayout = new QHBoxLayout();
    rowLayout->setSpacing(8);
    int count = 0;
    for (const TagInfo &tag : m_tags)
    {
        auto *button = new QPushButton(QStringLiteral("#%1  %2").arg(tag.tag).arg(tag.count), this);
        button->setObjectName(QStringLiteral("communityTag"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFlat(true);
        button->setFixedHeight(28);
        connect(button, &QPushButton::clicked, this, [this, name = tag.tag]() {
            emit tagOpenRequested(name);
        });
        rowLayout->addWidget(button);
        if (++count % 2 == 0)
        {
            rowLayout->addStretch(1);
            m_tagsLayout->addLayout(rowLayout);
            rowLayout = new QHBoxLayout();
            rowLayout->setSpacing(8);
        }
    }
    rowLayout->addStretch(1);
    m_tagsLayout->addLayout(rowLayout);
}
