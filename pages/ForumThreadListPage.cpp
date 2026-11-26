/**
 * @file   ForumThreadListPage.cpp
 * @brief  论坛主题列表页实现（板块 / 搜索 / 标签，服务器导航板块带来源切换）
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumThreadListPage.h"

#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "components/ContentViewSwitch.h"
#include "components/MasonryContentCard.h"
#include "layouts/MasonryLayout.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/McimHelper.h"
#include "utils/StyleKit.h"
#include "utils/mod/ModrinthAPI.h"

namespace
{
/** Modrinth 一页取多少台服务器（和论坛板块列表一致） */
constexpr int kModrinthPageSize = 20;
/** 服务器导航板块的来源偏好（0 全部源 / 1 方块盒子源 / 2 Modrinth 源） */
constexpr auto kServerSourceKey = "bb_server_nav_source";
/** 瀑布流封面列表的视图偏好（每个页面只记「列表 / 瀑布流」） */
constexpr auto kFlowViewKey = "forum_thread_flow";

/** 封面类板块：教程 / 插件 / 投影 / 网站导航 / 服务器导航 */
bool isCoverBoardSlug(const QString &slug)
{
    return slug == QLatin1String("tutorial-share") || slug == QLatin1String("plugin-share")
           || slug == QLatin1String("schematic-share") || slug == QLatin1String("website-nav")
           || slug == QLatin1String("server-nav");
}

/** 封面卡的类型：决定渐变底色与徽标单字 */
QString coverKindOf(const ForumThreadInfo &thread)
{
    if (thread.tutorial || thread.boardSlug == QLatin1String("tutorial-share"))
        return QStringLiteral("tutorial");
    if (thread.plugin.isValid() || thread.boardSlug == QLatin1String("plugin-share"))
        return QStringLiteral("plugin");
    if (thread.schematic.isValid() || thread.boardSlug == QLatin1String("schematic-share"))
        return QStringLiteral("schematic");
    if (thread.navigation.isServer() || thread.boardSlug == QLatin1String("server-nav"))
        return QStringLiteral("server");
    if (thread.navigation.isValid() || thread.boardSlug == QLatin1String("website-nav"))
        return QStringLiteral("website");
    return QStringLiteral("post");
}

/** 封面缺省时画在正中的单字徽记 */
QString coverGlyphOf(const QString &kind)
{
    if (kind == QLatin1String("tutorial"))
        return QObject::tr("教");
    if (kind == QLatin1String("plugin"))
        return QObject::tr("插");
    if (kind == QLatin1String("schematic"))
        return QObject::tr("投");
    if (kind == QLatin1String("server"))
        return QObject::tr("服");
    if (kind == QLatin1String("website"))
        return QObject::tr("网");
    return QObject::tr("帖");
}

/** 卡片简介压短一点，瀑布流里不至于一张卡占满一屏 */
QString elideForCard(const QString &text, int maxChars)
{
    const QString flat = text.simplified();
    if (flat.size() <= maxChars)
        return flat;
    return flat.left(maxChars - 1) + QStringLiteral("…");
}

/** 去掉 HTML 标签和 Markdown 链接，只留可读正文 */
QString plainTextOf(const QString &raw)
{
    QString text = raw;
    if (text.contains(QLatin1Char('<')))
    {
        static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
        text.remove(tagRe);
    }
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    // 正文里全是 discord / modrinth / 网盘链接：先把链接整段去掉，
    // 免得把 store.xxx.com 这种「看起来像域名」的外链当成服务器地址
    static const QRegularExpression urlRe(QStringLiteral("https?://[^\\s<>()\\[\\]]+"),
                                          QRegularExpression::CaseInsensitiveOption);
    text.remove(urlRe);
    static const QRegularExpression mdLinkRe(QStringLiteral("\\([^)]*\\)"));
    text.remove(mdLinkRe);
    return text;
}
} // namespace

ForumThreadListPage::ForumThreadListPage(QWidget *parent)
    : QWidget(parent)
{
    {
        QSettings settings;
        m_serverSource = qBound(int(SourceAll),
                                settings.value(QLatin1String(kServerSourceKey), int(SourceAll)).toInt(),
                                int(SourceModrinth));
    }

    // Modrinth 源：复用模组下载用的接口，只是把项目类型换成「Java 服务器」。
    // 搜索结果的 title / 简介 / 图标 / 下载量直接当服务器卡片用。
    m_modrinthApi = new ModrinthAPI(this);
    m_modrinthApi->setBaseUrl(McimHelper::modrinthApiBaseUrl());
    m_modrinthApi->setProjectType(QStringLiteral("minecraft_java_server"));

    connect(m_modrinthApi, &ModrinthAPI::searchCompleted, this, [this](const ModSearchResult &result) {
        if (m_modrinthRequestGen != m_fetchGen)
            return;
        m_modrinthServers = result.mods;
        m_modrinthTotal = result.totalHits;
        m_modrinthPages = qMax(1, (result.totalHits + kModrinthPageSize - 1) / kModrinthPageSize);

        // 地址大多写在项目介绍正文里：批量取一次正文（一条请求），
        // 拿回来再从 Markdown / HTML 里抠地址，列表就能直接显示 + 复制
        QStringList ids;
        ids.reserve(m_modrinthServers.size());
        for (const ModInfo &server : m_modrinthServers)
        {
            if (!server.id.isEmpty())
                ids << server.id;
        }
        if (ids.isEmpty())
        {
            m_modrinthDone = true;
            finishLoad();
            return;
        }
        m_modrinthApi->fetchProjectsByIds(ids);
    });

    connect(m_modrinthApi, &ModrinthAPI::searchFailed, this, [this](const QString &error) {
        if (m_modrinthRequestGen != m_fetchGen)
            return;
        m_modrinthError = error;
        m_modrinthServers.clear();
        m_modrinthDone = true;
        finishLoad();
    });

    connect(m_modrinthApi, &ModrinthAPI::projectsResolved, this, [this](const QList<ModInfo> &projects) {
        if (m_modrinthRequestGen != m_fetchGen)
            return;
        for (const ModInfo &project : projects)
        {
            const QString address = extractServerAddress(
                project.description + QLatin1Char('\n') + project.detailedDescription);
            if (!address.isEmpty())
                m_modrinthAddresses.insert(project.id, address);
        }
        m_modrinthDone = true;
        finishLoad();
    });

    // 正文没取到不影响列表：退回只用搜索简介里的地址
    connect(m_modrinthApi, &ModrinthAPI::projectsResolveFailed, this, [this](const QString &) {
        if (m_modrinthRequestGen != m_fetchGen)
            return;
        m_modrinthDone = true;
        finishLoad();
    });

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

    // 瀑布流容器：平时在布局外面，切到封面视图时插到列表位置；
    // 卡片 260 宽、按最短列堆叠，卡片高度随简介长短自然错落
    m_flowHost = new QWidget(content);
    m_flowHost->setAttribute(Qt::WA_StyledBackground, false);
    m_flowHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_flowLayout = new MasonryLayout(m_flowHost, 0, 10, 10);
    m_flowHost->hide();

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

bool ForumThreadListPage::isServerNavBoard() const
{
    return m_mode == BoardMode && m_boardSlug == QLatin1String("server-nav");
}

bool ForumThreadListPage::sourceWantsForum() const
{
    return !isServerNavBoard() || m_serverSource != SourceModrinth;
}

bool ForumThreadListPage::sourceWantsModrinth() const
{
    return isServerNavBoard() && m_serverSource != SourceBlockBox;
}

void ForumThreadListPage::renderHeader()
{
    ForumKit::clearLayout(m_headerBody);
    m_sortButtons.clear();
    m_sourceCombo = nullptr;
    m_searchEdit = nullptr;
    m_viewSwitch = nullptr;

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
            const QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
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
        // 排序：最新回复 / 最新发布 / 最热（服务器导航板块前面再加一个来源下拉）
        auto *sortRow = new QHBoxLayout();
        sortRow->setSpacing(6);

        if (isServerNavBoard())
        {
            sortRow->addWidget(ForumKit::makeMuted(m_headerCard, tr("来源")), 0);
            m_sourceCombo = new QComboBox(m_headerCard);
            m_sourceCombo->setObjectName(QStringLiteral("forumSourceCombo"));
            m_sourceCombo->setFixedHeight(26);
            m_sourceCombo->setMinimumWidth(112);
            m_sourceCombo->setCursor(Qt::PointingHandCursor);
            m_sourceCombo->addItem(tr("全部源"), int(SourceAll));
            m_sourceCombo->addItem(tr("方块盒子源"), int(SourceBlockBox));
            m_sourceCombo->addItem(tr("Modrinth 源"), int(SourceModrinth));
            m_sourceCombo->setCurrentIndex(m_sourceCombo->findData(m_serverSource));
            m_sourceCombo->setToolTip(tr("选择服务器列表来自哪里：方块盒子社区帖，还是 Modrinth 上的服务器"));
            connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int index) {
                        if (!m_sourceCombo || index < 0)
                            return;
                        const int source = m_sourceCombo->itemData(index).toInt();
                        if (source == m_serverSource)
                            return;
                        m_serverSource = source;
                        QSettings settings;
                        settings.setValue(QLatin1String(kServerSourceKey), m_serverSource);
                        settings.sync();
                        loadPage(1);
                    });
            sortRow->addWidget(m_sourceCombo);
            sortRow->addSpacing(12);
        }

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

        // 教程 / 资源 / 导航板块：多一个「列表 / 瀑布流封面」切换，偏好按板块记住；
        // 服务器导航默认仍是列表（服务器地址一行一条更好扫），想看图可以自己切
        if (m_mode == BoardMode && boardSupportsCoverFlow())
        {
            sortRow->addSpacing(10);
            const QString viewKey = QStringLiteral("%1:%2").arg(QLatin1String(kFlowViewKey), m_boardSlug);
            const ContentViewSwitch::ViewMode fallback =
                m_boardSlug == QLatin1String("server-nav") ? ContentViewSwitch::List
                                                           : ContentViewSwitch::Masonry;
            m_viewSwitch = new ContentViewSwitch(m_headerCard);
            m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted(viewKey, fallback));
            m_viewSwitch->setToolTip(tr("切换列表 / 瀑布流封面：封面只加载作者上传的小图"));
            connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged, this,
                    [this, viewKey](ContentViewSwitch::ViewMode mode) {
                        ContentViewSwitch::savePersisted(viewKey, mode);
                        applyViewMode();
                    });
            sortRow->addWidget(m_viewSwitch);
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
    m_page = qMax(1, page);
    ++m_fetchGen;

    // 新一轮刷新：先把上一轮的结果作废，两个来源各自回来后再统一渲染
    m_forumThreads.clear();
    m_modrinthServers.clear();
    m_modrinthAddresses.clear();
    m_forumError.clear();
    m_modrinthError.clear();
    m_forumPages = 1;
    m_modrinthPages = 1;
    m_modrinthTotal = 0;
    m_totalPages = 1;

    const bool wantForum = sourceWantsForum();
    const bool wantModrinth = sourceWantsModrinth();
    m_forumDone = !wantForum;
    m_modrinthDone = !wantModrinth;

    m_refreshButton->setEnabled(false);
    setStatus(tr("正在加载…"));

    if (wantForum)
        fetchForumPage(m_page);
    if (wantModrinth)
        fetchModrinthPage(m_page);

    if (m_forumDone && m_modrinthDone)
        finishLoad();
}

void ForumThreadListPage::fetchForumPage(int page)
{
    const int gen = m_fetchGen;

    if (m_mode == BoardMode)
    {
        ForumContentService::instance()->fetchBoardThreads(
            m_boardSlug, m_sort, page,
            [this, gen](const ForumBoard &board, const QVector<ForumThreadInfo> &threads,
                        const ForumPagination &pagination) {
                if (gen != m_fetchGen)
                    return;
                if (!board.name.isEmpty())
                    m_boardName = board.name;
                m_forumThreads = threads;
                m_forumPages = qMax(1, pagination.pages);
                m_totalPages = m_forumPages;
                // 服务器导航板块的页码由本页自己管（两个来源共用同一个页码）
                if (!isServerNavBoard())
                    m_page = qMax(1, pagination.page);
                m_forumDone = true;
                finishLoad();
            },
            [this, gen](const QString &message, int) {
                if (gen != m_fetchGen)
                    return;
                m_forumError = message;
                m_forumDone = true;
                finishLoad();
            });
    }
    else if (m_mode == SearchMode)
    {
        ForumContentService::instance()->searchThreads(
            m_query, page,
            [this, gen](const QString &, const QVector<ForumThreadInfo> &results,
                        const ForumPagination &pagination) {
                if (gen != m_fetchGen)
                    return;
                m_forumThreads = results;
                m_totalPages = qMax(1, pagination.pages);
                m_page = qMax(1, pagination.page);
                m_forumDone = true;
                finishLoad();
            },
            [this, gen](const QString &message, int) {
                if (gen != m_fetchGen)
                    return;
                m_forumError = message;
                m_forumDone = true;
                finishLoad();
            });
    }
    else
    {
        ForumContentService::instance()->fetchTagThreads(
            m_tag, m_sort, page,
            [this, gen](const QVector<ForumThreadInfo> &threads,
                        const ForumPagination &pagination) {
                if (gen != m_fetchGen)
                    return;
                m_forumThreads = threads;
                m_totalPages = qMax(1, pagination.pages);
                m_page = qMax(1, pagination.page);
                m_forumDone = true;
                finishLoad();
            },
            [this, gen](const QString &message, int) {
                if (gen != m_fetchGen)
                    return;
                m_forumError = message;
                m_forumDone = true;
                finishLoad();
            });
    }
}

void ForumThreadListPage::fetchModrinthPage(int page)
{
    if (!m_modrinthApi)
    {
        // 理论上不会发生（构造函数里就建好了）；真发生也别让页面一直转圈
        m_modrinthDone = true;
        finishLoad();
        return;
    }

    m_modrinthRequestGen = m_fetchGen;
    // 板块的排序按钮同时管两个来源：最新回复≈最近更新、最新发布≈新建、最热≈下载最多
    m_modrinthApi->searchMods(QString(), QString(), QString(), qMax(0, page - 1),
                              kModrinthPageSize, modrinthSortFor(m_sort));
}

void ForumThreadListPage::finishLoad()
{
    if (!m_forumDone || !m_modrinthDone)
        return;

    m_refreshButton->setEnabled(true);

    const bool forumOk = m_forumError.isEmpty();
    const bool modrinthOk = m_modrinthError.isEmpty();

    if (isServerNavBoard())
    {
        const bool showForum = sourceWantsForum();
        const bool showModrinth = sourceWantsModrinth();

        QString text;
        bool error = false;
        if (showForum && showModrinth)
        {
            if (forumOk && modrinthOk)
                text = tr("本页：方块盒子源 %1 台 · Modrinth 源 %2 台")
                           .arg(m_forumThreads.size())
                           .arg(m_modrinthServers.size());
            else if (!forumOk && !modrinthOk)
            {
                text = tr("两个来源都没连上：%1").arg(m_forumError);
                error = true;
            }
            else if (!forumOk)
            {
                text = tr("方块盒子源没连上：%1").arg(m_forumError);
                error = true;
            }
            else
            {
                text = tr("Modrinth 源没连上：%1").arg(m_modrinthError);
                error = true;
            }
        }
        else if (showModrinth)
        {
            if (modrinthOk)
                text = tr("Modrinth 共 %1 台服务器，本页 %2 台")
                           .arg(formatCount(m_modrinthTotal))
                           .arg(m_modrinthServers.size());
            else
            {
                text = tr("加载失败：%1").arg(m_modrinthError);
                error = true;
            }
        }
        else
        {
            if (!forumOk)
            {
                text = tr("加载失败：%1").arg(m_forumError);
                error = true;
            }
        }
        setStatus(text, error);
        renderServerNavList();
    }
    else if (!forumOk)
    {
        setStatus(tr("加载失败：%1").arg(m_forumError), true);
    }
    else
    {
        setStatus(QString());
        renderThreads(m_forumThreads);
    }

    updatePager();
}

void ForumThreadListPage::updatePager()
{
    int pages = m_totalPages;
    if (isServerNavBoard())
    {
        const bool showForum = sourceWantsForum();
        const bool showModrinth = sourceWantsModrinth();
        if (showForum && showModrinth)
            pages = qMax(m_forumPages, m_modrinthPages);
        else if (showModrinth)
            pages = m_modrinthPages;
        else
            pages = m_forumPages;
    }
    m_pager->setPage(m_page, qMax(1, pages));
}

void ForumThreadListPage::renderThreads(const QVector<ForumThreadInfo> &threads)
{
    // 先把瀑布流容器从布局里摘掉，再清列表：否则 clearLayout 会把容器当成普通行一起删掉
    detachCoverFlow();
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

    if (coverFlowActive())
        renderCoverFlow(threads);
    else
        appendThreadRows(threads);
}

bool ForumThreadListPage::boardSupportsCoverFlow() const
{
    return m_mode == BoardMode && isCoverBoardSlug(m_boardSlug);
}

bool ForumThreadListPage::coverFlowActive() const
{
    return boardSupportsCoverFlow() && m_viewSwitch
           && m_viewSwitch->viewMode() == ContentViewSwitch::Masonry;
}

void ForumThreadListPage::detachCoverFlow()
{
    if (m_flowAttached && m_flowHost)
    {
        m_listLayout->removeWidget(m_flowHost);
        m_flowHost->hide();
        m_flowAttached = false;
    }
    if (m_flowLayout)
        ForumKit::clearLayout(m_flowLayout);
}

void ForumThreadListPage::applyViewMode()
{
    // 数据已经在手上：切视图只重排，不重新请求
    if (isServerNavBoard())
        renderServerNavList();
    else if (!m_forumThreads.isEmpty() || !m_forumError.isEmpty())
        renderThreads(m_forumThreads);
}

void ForumThreadListPage::renderCoverFlow(const QVector<ForumThreadInfo> &threads)
{
    if (!m_flowHost || !m_flowLayout)
        return;
    for (const ForumThreadInfo &thread : threads)
        m_flowLayout->addWidget(createThreadCoverCard(thread));
    if (!m_flowAttached)
    {
        m_listLayout->addWidget(m_flowHost);
        m_flowAttached = true;
    }
    m_flowHost->show();
    m_flowHost->updateGeometry();
}

QWidget *ForumThreadListPage::createThreadCoverCard(const ForumThreadInfo &thread)
{
    auto *card = new ForumKit::ClickableCard(m_flowHost);
    card->setObjectName(QStringLiteral("contentCard"));
    card->setFixedWidth(260);
    card->setToolTip(thread.title);
    const int threadId = thread.id;
    card->onClick = [this, threadId]() { emit threadOpenRequested(threadId); };
    MasonryContentCard::applyShadow(card);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(1, 1, 1, 1);
    cardLayout->setSpacing(0);

    constexpr int kBannerW = 258;
    constexpr int kBannerH = 120;

    const QString kind = coverKindOf(thread);
    QColor c1;
    QColor c2;
    MasonryContentCard::paletteForSeed(QStringLiteral("forum:%1:%2").arg(kind).arg(thread.id), c1, c2);

    auto *banner = new QLabel(card);
    banner->setObjectName(QStringLiteral("contentCardBanner"));
    banner->setFixedHeight(kBannerH);
    banner->setPixmap(MasonryContentCard::makeBannerPixmap(c1, c2, kBannerW, kBannerH));
    banner->setProperty("gradientBanner", true);
    cardLayout->addWidget(banner);

    auto *logo = new QLabel(banner);
    logo->setObjectName(QStringLiteral("contentCardLogo"));
    logo->setAlignment(Qt::AlignCenter);
    logo->setPixmap(MasonryContentCard::makeLogoPixmap(
        c1, c2, coverGlyphOf(kind), MasonryContentCard::logoSizeFor(true, kBannerH)));
    MasonryContentCard::placeLogo(logo, true, kBannerW, kBannerH);
    logo->raise();

    // 作者传了封面就用真实小图（CDN 地址优先），没传就保持类型渐变 + 单字徽记
    if (thread.cover.isValid())
    {
        const QString url = ForumAccountService::instance()->absoluteUrl(thread.cover.bestUrl());
        QPointer<QLabel> bannerGuard(banner);
        QPointer<QLabel> logoGuard(logo);
        ForumImageStore::instance()->requestImage(
            url, [bannerGuard, logoGuard](const QImage &image) {
                if (bannerGuard.isNull() || image.isNull())
                    return;
                bannerGuard->setPixmap(MasonryContentCard::makeCoverPixmap(
                    QPixmap::fromImage(image), kBannerW, kBannerH));
                bannerGuard->setProperty("gradientBanner", false);
                if (!logoGuard.isNull())
                    MasonryContentCard::placeLogo(logoGuard, false, kBannerW, kBannerH);
            });
    }

    auto *body = new QWidget(card);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 12, 12, 12);
    bodyLayout->setSpacing(5);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(6);
    if (thread.pinned)
        titleRow->addWidget(ForumKit::makeBadge(body, tr("置顶")));
    else if (thread.locked)
        titleRow->addWidget(ForumKit::makeBadge(body, tr("锁定"), true));
    auto *title = new ForumKit::ElidedLabel(body);
    title->setObjectName(QStringLiteral("contentCardName"));
    title->setMinimumWidth(80);
    title->setFullText(thread.title);
    titleRow->addWidget(title, 1);
    bodyLayout->addLayout(titleRow);

    const QString snippet = elideForCard(thread.snippet, 68);
    if (!snippet.isEmpty())
    {
        auto *desc = new QLabel(snippet, body);
        desc->setObjectName(QStringLiteral("contentCardDesc"));
        desc->setWordWrap(true);
        bodyLayout->addWidget(desc);
    }

    // 服务器导航帖把地址直接摆出来，卡片上也能复制
    if (thread.navigation.isServer() && !thread.navigation.url.trimmed().isEmpty())
    {
        auto *addrRow = new QHBoxLayout();
        addrRow->setContentsMargins(0, 2, 0, 0);
        addrRow->setSpacing(6);
        auto *addr = new ForumKit::ElidedLabel(body);
        addr->setObjectName(QStringLiteral("contentCardDesc"));
        addr->setFullText(thread.navigation.url.trimmed());
        addr->setToolTip(thread.navigation.url.trimmed());
        addrRow->addWidget(addr, 1);
        auto *copyButton = new QPushButton(tr("复制"), body);
        copyButton->setObjectName(QStringLiteral("contentCardActionBtn"));
        copyButton->setCursor(Qt::PointingHandCursor);
        copyButton->setFixedHeight(24);
        copyButton->setToolTip(tr("复制服务器地址"));
        const QString address = thread.navigation.url.trimmed();
        connect(copyButton, &QPushButton::clicked, this,
                [this, address]() { copyServerAddress(address); });
        addrRow->addWidget(copyButton);
        bodyLayout->addLayout(addrRow);
    }

    QStringList metaParts;
    if (!thread.authorName.isEmpty())
        metaParts << thread.authorName;
    if (thread.lastPostAt > 0)
        metaParts << ForumKit::relativeTime(thread.lastPostAt);
    else if (thread.createdAt > 0)
        metaParts << ForumKit::relativeTime(thread.createdAt);
    if (!metaParts.isEmpty())
    {
        auto *meta = new QLabel(metaParts.join(QStringLiteral(" · ")), body);
        meta->setObjectName(QStringLiteral("forumMuted"));
        meta->setWordWrap(true);
        bodyLayout->addWidget(meta);
    }

    // 计数行：插件 / 投影 / 教程 / 导航帖聊的是评论，其它帖是楼层回复
    const bool commentThread = thread.plugin.isValid() || thread.schematic.isValid()
                               || thread.tutorial || thread.navigation.isValid()
                               || thread.boardSlug == QLatin1String("plugin-share")
                               || thread.boardSlug == QLatin1String("schematic-share")
                               || thread.boardSlug == QLatin1String("website-nav")
                               || thread.boardSlug == QLatin1String("server-nav");
    auto *counts = new QLabel(
        commentThread ? tr("%1 评论 · %2 浏览").arg(thread.comments).arg(thread.views)
                      : tr("%1 回复 · %2 浏览").arg(thread.replies).arg(thread.views),
        body);
    counts->setObjectName(QStringLiteral("forumMuted"));
    bodyLayout->addWidget(counts);

    cardLayout->addWidget(body);
    return card;
}

void ForumThreadListPage::renderServerNavList()
{
    detachCoverFlow();
    ForumKit::clearLayout(m_listLayout);

    const bool showForum = sourceWantsForum();
    const bool showModrinth = sourceWantsModrinth();
    const bool bothSources = showForum && showModrinth;
    const bool forumOk = m_forumError.isEmpty();
    const bool modrinthOk = m_modrinthError.isEmpty();

    const bool hasForum = showForum && forumOk && !m_forumThreads.isEmpty();
    const bool hasModrinth = showModrinth && modrinthOk && !m_modrinthServers.isEmpty();

    if (!hasForum && !hasModrinth)
    {
        QString empty;
        if (m_forumError.isEmpty() && m_modrinthError.isEmpty())
        {
            empty = tr("这里还没有人分享服务器，点右上角「发帖」把你的服务器地址发上来吧");
        }
        else if (showForum && showModrinth)
        {
            if (!forumOk && !modrinthOk)
                empty = tr("两个来源暂时都连不上，稍后点「刷新」再试一次");
            else if (!forumOk)
                empty = tr("方块盒子源没连上，Modrinth 这一页也没有服务器，稍后点「刷新」再试一次");
            else
                empty = tr("Modrinth 源没连上，方块盒子这边也还没有服务器，稍后点「刷新」再试一次");
        }
        else if (showModrinth)
        {
            empty = tr("Modrinth 上没有取到服务器，稍后点「刷新」再试一次");
        }
        else
        {
            empty = tr("暂时没有取到服务器列表，稍后点「刷新」再试一次");
        }

        auto *emptyLabel = ForumKit::makeMuted(this, empty);
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_listLayout->addSpacing(24);
        m_listLayout->addWidget(emptyLabel);
        m_listLayout->addSpacing(24);
        return;
    }

    if (bothSources && forumOk)
        appendSectionHeader(tr("方块盒子源"),
                            hasForum ? tr("本页 %1 台").arg(m_forumThreads.size())
                                     : tr("本页没有服务器"));
    if (hasForum)
    {
        if (coverFlowActive())
            renderCoverFlow(m_forumThreads);
        else
            appendThreadRows(m_forumThreads);
    }

    if (hasModrinth)
    {
        if (bothSources)
            appendSectionHeader(tr("Modrinth 源"),
                                tr("共 %1 台 · 本页 %2 台")
                                    .arg(formatCount(m_modrinthTotal))
                                    .arg(m_modrinthServers.size()));
        for (const ModInfo &server : m_modrinthServers)
            m_listLayout->addWidget(createModrinthServerRow(server));
    }
    else if (bothSources && modrinthOk)
    {
        appendSectionHeader(tr("Modrinth 源"), tr("本页没有找到服务器"));
    }
}

void ForumThreadListPage::appendSectionHeader(const QString &title, const QString &note)
{
    auto *header = new QWidget(this);
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(2, 10, 2, 0);
    row->setSpacing(10);
    row->addWidget(ForumKit::makeSectionTitle(header, title));
    if (!note.isEmpty())
    {
        auto *noteLabel = ForumKit::makeMuted(header, note);
        // 计数只是陪衬：窄屏也不要折成两行，宁可省略
        noteLabel->setWordWrap(false);
        row->addWidget(noteLabel);
    }
    row->addStretch(1);
    m_listLayout->addWidget(header);
}

void ForumThreadListPage::appendThreadRows(const QVector<ForumThreadInfo> &threads)
{
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

        // 标题行（含置顶 / 锁定 / 插件 / 教程 / 导航徽章）
        auto *titleRow = new QHBoxLayout();
        titleRow->setSpacing(6);
        if (thread.pinned)
            titleRow->addWidget(ForumKit::makeBadge(row, tr("置顶")));
        if (thread.locked)
            titleRow->addWidget(ForumKit::makeBadge(row, tr("锁定"), true));
        if (thread.plugin.isValid())
            titleRow->addWidget(ForumKit::makeBadge(row, tr("插件")));
        else if (thread.tutorial)
            titleRow->addWidget(ForumKit::makeBadge(row, tr("教程")));
        else if (thread.navigation.isValid()
                 || thread.boardSlug == QLatin1String("website-nav")
                 || thread.boardSlug == QLatin1String("server-nav"))
            titleRow->addWidget(ForumKit::makeBadge(row, tr("导航")));
        // 有奖问答：标题旁直接挂赏金，盖楼前一眼看得出值不值得答
        if (thread.question.isValid())
        {
            QString badgeText = tr("🎁 %1 方块").arg(thread.question.bounty);
            if (thread.question.isSettled())
                badgeText += tr(" · 已发放");
            else if (thread.question.status == QStringLiteral("refunded"))
                badgeText += tr(" · 已退回");
            titleRow->addWidget(ForumKit::makeBadge(row, badgeText, true));
        }

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

        // 服务器帖：直接把地址摆出来，点一下就能复制去游戏里连
        if (thread.navigation.isServer() && !thread.navigation.url.trimmed().isEmpty())
        {
            const QString address = thread.navigation.url.trimmed();
            auto *addrRow = new QHBoxLayout();
            addrRow->setSpacing(8);
            addrRow->addWidget(ForumKit::makeBadge(row, tr("服务器地址")));
            auto *addrLabel = new QLabel(address, row);
            addrLabel->setObjectName(QStringLiteral("forumMuted"));
            addrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            addrRow->addWidget(addrLabel, 0);
            auto *copyButton = new QPushButton(tr("复制地址"), row);
            copyButton->setObjectName(QStringLiteral("forumGhostButton"));
            copyButton->setCursor(Qt::PointingHandCursor);
            copyButton->setFixedHeight(24);
            connect(copyButton, &QPushButton::clicked, this,
                    [this, address]() { copyServerAddress(address); });
            addrRow->addWidget(copyButton);
            addrRow->addStretch(1);
            textColumn->addLayout(addrRow);
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

        // 右侧统计：插件 / 投影分享板块里的帖子、以及教程帖 / 导航帖讨论的是评论，不是楼层回复
        const bool resourceThread = thread.boardSlug == QLatin1String("plugin-share")
                                    || thread.boardSlug == QLatin1String("schematic-share")
                                    || thread.boardSlug == QLatin1String("website-nav")
                                    || thread.boardSlug == QLatin1String("server-nav")
                                    || thread.plugin.isValid() || thread.schematic.isValid()
                                    || thread.tutorial || thread.navigation.isValid();
        auto *countsColumn = new QVBoxLayout();
        countsColumn->setSpacing(2);
        auto *replies = new QLabel(resourceThread ? tr("%1 评论").arg(thread.comments)
                                                  : tr("%1 回复").arg(thread.replies),
                                   row);
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

QWidget *ForumThreadListPage::createModrinthServerRow(const ModInfo &server)
{
    auto *row = new ForumKit::ClickableCard(this);
    row->setObjectName(QStringLiteral("forumThreadRow"));
    const QString pageUrl = server.pageUrl;
    row->onClick = [pageUrl]() {
        if (!pageUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(pageUrl));
    };

    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(14, 12, 14, 12);
    rowLayout->setSpacing(12);

    // 图标：Modrinth CDN 的 96px 图标，圆角与论坛头像一致
    auto *icon = new QLabel(row);
    icon->setObjectName(QStringLiteral("forumAvatar"));
    icon->setFixedSize(44, 44);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(StyleKit::resolve(
        QStringLiteral("QLabel#forumAvatar{border-radius:12px;background:@BG_MUTED@;}")));
    if (!server.iconUrl.isEmpty())
    {
        QPointer<QLabel> guard(icon);
        ForumImageStore::instance()->requestImage(server.iconUrl, [guard](const QImage &image) {
            if (guard.isNull() || image.isNull())
                return;
            const qreal dpr = guard->devicePixelRatioF();
            QPixmap pixmap = QPixmap::fromImage(image)
                                 .scaled(int(40 * dpr), int(40 * dpr), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
            guard->setPixmap(pixmap);
        });
    }
    rowLayout->addWidget(icon, 0, Qt::AlignTop);

    auto *textColumn = new QVBoxLayout();
    textColumn->setSpacing(4);

    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(6);
    titleRow->addWidget(ForumKit::makeBadge(row, tr("Modrinth")));
    auto *title = new ForumKit::ElidedLabel(row);
    title->setObjectName(QStringLiteral("forumRowTitle"));
    title->setFullText(server.name);
    title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleRow->addWidget(title, 1);
    textColumn->addLayout(titleRow);

    if (!server.description.isEmpty())
    {
        auto *desc = new ForumKit::ElidedLabel(row);
        desc->setObjectName(QStringLiteral("forumMuted"));
        desc->setFullText(server.description);
        textColumn->addWidget(desc);
    }

    // 地址：先看批量正文里抠出来的，再退回搜索简介；有些服直接把地址当项目名
    QString address = m_modrinthAddresses.value(server.id);
    if (address.isEmpty())
        address = extractServerAddress(server.description);
    if (address.isEmpty())
    {
        static const QRegularExpression titleRe(
            QStringLiteral("^([a-z0-9](?:[a-z0-9.\\-]{0,60})\\.[a-z]{2,}(?::\\d{2,5})?)$"),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = titleRe.match(server.name.trimmed());
        if (match.hasMatch())
            address = match.captured(1).toLower();
    }
    if (!address.isEmpty())
    {
        auto *addrRow = new QHBoxLayout();
        addrRow->setSpacing(8);
        addrRow->addWidget(ForumKit::makeBadge(row, tr("服务器地址")));
        auto *addrLabel = new QLabel(address, row);
        addrLabel->setObjectName(QStringLiteral("forumMuted"));
        addrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        addrRow->addWidget(addrLabel, 0);
        addrRow->addStretch(1);
        textColumn->addLayout(addrRow);
    }

    QStringList metaParts;
    if (!server.gameVersions.isEmpty())
        metaParts << server.gameVersions.mid(0, 2).join(QStringLiteral(" / "));
    if (server.downloadCount > 0)
        metaParts << tr("%1 次下载").arg(formatCount(server.downloadCount));
    if (metaParts.isEmpty())
        metaParts << tr("来自 Modrinth 的 Java 版服务器");
    auto *metaLabel = ForumKit::makeMuted(row, metaParts.join(QStringLiteral(" · ")));
    textColumn->addWidget(metaLabel);

    rowLayout->addLayout(textColumn, 1);

    auto *actionColumn = new QVBoxLayout();
    actionColumn->setSpacing(4);
    if (!address.isEmpty())
    {
        auto *copyButton = new QPushButton(tr("复制地址"), row);
        copyButton->setObjectName(QStringLiteral("forumGhostButton"));
        copyButton->setCursor(Qt::PointingHandCursor);
        copyButton->setFixedHeight(26);
        connect(copyButton, &QPushButton::clicked, this,
                [this, address]() { copyServerAddress(address); });
        actionColumn->addWidget(copyButton);
    }
    auto *openButton = new QPushButton(tr("在 Modrinth 查看"), row);
    openButton->setObjectName(QStringLiteral("forumGhostButton"));
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setFixedHeight(26);
    connect(openButton, &QPushButton::clicked, this, [pageUrl]() {
        if (!pageUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(pageUrl));
    });
    actionColumn->addWidget(openButton);
    rowLayout->addLayout(actionColumn);

    return row;
}

void ForumThreadListPage::copyServerAddress(const QString &address)
{
    if (address.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(address);
    NotificationManager::showSuccess(window(), tr("已复制服务器地址：%1").arg(address));
}

QString ForumThreadListPage::extractServerAddress(const QString &text)
{
    if (text.trimmed().isEmpty())
        return QString();

    const QString plain = plainTextOf(text);

    // 结果统一小写：域名本身不区分大小写，复制出去也更整齐
    const auto tidy = [](const QString &value) {
        QString out = value.trimmed();
        while (!out.isEmpty()
               && (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(','))
                   || out.endsWith(QLatin1Char(';'))))
            out.chop(1);
        return out.toLower();
    };
    const auto portOk = [](const QString &value) {
        const int colon = value.lastIndexOf(QLatin1Char(':'));
        if (colon < 0)
            return true;
        bool ok = false;
        const int port = value.mid(colon + 1).toInt(&ok);
        return ok && port > 0 && port <= 65535;
    };
    const auto acceptable = [](const QString &value) {
        if (value.isEmpty() || value.size() > 253)
            return false;
        // 正文里常见的非服务器域名：论坛、视频、仓库、赞助入口等，一律不认
        static const QStringList banned = {
            QStringLiteral("modrinth.com"),   QStringLiteral("discord.gg"),
            QStringLiteral("discord.com"),    QStringLiteral("github.com"),
            QStringLiteral("youtube.com"),    QStringLiteral("youtu.be"),
            QStringLiteral("twitter.com"),    QStringLiteral("x.com"),
            QStringLiteral("patreon.com"),    QStringLiteral("ko-fi.com"),
            QStringLiteral("buymeacoffee.com"), QStringLiteral("twitch.tv"),
            QStringLiteral("reddit.com"),     QStringLiteral("planetminecraft.com"),
            QStringLiteral("curseforge.com"), QStringLiteral("google.com"),
            QStringLiteral("bing.com"),       QStringLiteral("qq.com"),
            QStringLiteral("bilibili.com"),   QStringLiteral("gitee.com"),
            QStringLiteral("mcmod.cn"),       QStringLiteral("namemc.com"),
            QStringLiteral("spigotmc.org"),   QStringLiteral("paypal.com"),
            QStringLiteral("microsoft.com"),  QStringLiteral("minecraft.net"),
        };
        for (const QString &domain : banned)
        {
            if (value == domain || value.endsWith(QLatin1Char('.') + domain))
                return false;
        }
        return value.contains(QLatin1Char('.'));
    };

    // ① 明写「IP / 地址 / Server IP / Join at」：可信度最高，正文里作者也是这么标的
    static const QRegularExpression labeledRe(
        QStringLiteral("(?i)(?:服务器地址|服务端地址|加入地址|进入地址|连接地址|服务器\\s*ip|"
                       "server\\s*(?:ip|address)|(?:java|基岩)\\s*(?:版)?\\s*(?:ip|地址)|"
                       "ip\\s*address|ip|address|host|join(?:\\s+us)?(?:\\s+ingame)?\\s+at)"
                       "[\\s:*_`\"'「」“”()（）:：=#|\\-]*"
                       "([a-z0-9](?:[a-z0-9.\\-]{0,60})\\.[a-z]{2,}(?::\\d{1,5})?"
                       "|\\d{1,3}(?:\\.\\d{1,3}){3}(?::\\d{1,5})?)"));
    auto match = labeledRe.match(plain);
    while (match.hasMatch())
    {
        const QString value = tidy(match.captured(1));
        if (acceptable(value) && portOk(value))
            return value;
        match = labeledRe.match(plain, match.capturedEnd());
    }

    // ② 带端口的地址（play.xxx.net:25565 这种，端口是强信号）
    static const QRegularExpression portRe(
        QStringLiteral("(?i)\\b([a-z0-9](?:[a-z0-9.\\-]{0,60})\\.[a-z]{2,}:\\d{2,5})\\b"));
    match = portRe.match(plain);
    while (match.hasMatch())
    {
        const QString value = tidy(match.captured(1));
        if (acceptable(value) && portOk(value))
            return value;
        match = portRe.match(plain, match.capturedEnd());
    }

    // ③ 常见的联机子域：play. / mc. / server. / join.
    static const QRegularExpression hostRe(
        QStringLiteral("(?i)\\b((?:play|mc|server|join|srv)\\.[a-z0-9](?:[a-z0-9.\\-]{0,60})\\.[a-z]{2,})\\b"));
    match = hostRe.match(plain);
    while (match.hasMatch())
    {
        const QString value = tidy(match.captured(1));
        if (acceptable(value) && portOk(value))
            return value;
        match = hostRe.match(plain, match.capturedEnd());
    }

    // ④ 单独成行的一串地址（作者常把地址塞进代码块，就一行）
    static const QRegularExpression lineRe(
        QStringLiteral("(?im)^\\s*([a-z0-9](?:[a-z0-9.\\-]{0,60})\\.[a-z]{2,}(?::\\d{1,5})?)\\s*$"));
    match = lineRe.match(plain);
    while (match.hasMatch())
    {
        const QString value = tidy(match.captured(1));
        if (acceptable(value) && portOk(value))
            return value;
        match = lineRe.match(plain, match.capturedEnd());
    }

    return QString();
}

QString ForumThreadListPage::modrinthSortFor(const QString &sort)
{
    // 和按钮语义对齐：最新回复看「最近更新」，最新发布看「新建」，最热看「下载最多」
    if (sort == QLatin1String("new"))
        return QStringLiteral("date_created");
    if (sort == QLatin1String("hot"))
        return QStringLiteral("popularity");
    return QStringLiteral("last_updated");
}

QString ForumThreadListPage::formatCount(qint64 count)
{
    // 直接按本地习惯加千位分隔（4,071），比「4.1 万」更不挑语言
    return QLocale().toString(count);
}

void ForumThreadListPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}
