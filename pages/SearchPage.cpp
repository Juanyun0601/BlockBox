#include "SearchPage.h"

#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QSizePolicy>

#include "components/ForumKit.h"
#include "utils/IconHelper.h"
#include "utils/McimHelper.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"
#include "utils/mod/SearchQueryResolver.h"

namespace {

constexpr int kRequestPageSize = 24;   // 每个下载源请求的条数
constexpr int kMaxOnlineCards = 24;    // 在线资源最多展示的卡片数
constexpr int kMaxForumCards = 12;     // 论坛帖子最多展示的卡片数
constexpr int kIconSize = 40;          // 与 #modCardIcon 的 40x40 一致，避免像素被裁
constexpr int kSearchDebounceMs = 450;

/** 内容类型键 → ContentType；返回 false 表示不是启动器认识的类型（如 Bukkit 插件） */
bool contentTypeOfKind(const QString &kind, ContentType *type)
{
    static const QHash<QString, ContentType> kKindMap = {
        {QStringLiteral("mod"), ContentType::Mod},
        {QStringLiteral("modpack"), ContentType::Modpack},
        {QStringLiteral("resourcepack"), ContentType::ResourcePack},
        {QStringLiteral("shader"), ContentType::ShaderPack},
        {QStringLiteral("datapack"), ContentType::DataPack},
        {QStringLiteral("world"), ContentType::World},
    };
    const auto it = kKindMap.constFind(kind);
    if (it == kKindMap.constEnd())
        return false;
    if (type)
        *type = it.value();
    return true;
}

QString formatCount(qint64 n)
{
    if (n < 10000)
        return QString::number(n);
    if (n < 100000000)
        return QString::number(n / 10000.0, 'f', 1) + QStringLiteral(" 万");
    return QString::number(n / 100000000.0, 'f', 1) + QStringLiteral(" 亿");
}

QString sourceLabelOf(const ModInfo &info)
{
    if (info.isCrossSourceMerged())
        return QObject::tr("双源");
    if (info.source == QStringLiteral("curseforge"))
        return QStringLiteral("CurseForge");
    if (info.source == QStringLiteral("modrinth"))
        return QStringLiteral("Modrinth");
    return info.source;
}

/** 单行简介：去掉换行与连续空白，过长时截断 */
QString singleLine(const QString &text, int maxChars = 120)
{
    QString out = text.simplified();
    if (out.length() > maxChars)
        out = out.left(maxChars) + QStringLiteral("…");
    return out;
}

} // namespace

SearchPage::SearchPage(QWidget *parent)
    : QWidget(parent)
{
    initAPIs();
    initUI();
}

SearchPage::~SearchPage() = default;

void SearchPage::initAPIs()
{
    m_networkManager = new QNetworkAccessManager(this);

    m_curseforgeAPI = new CurseForgeAPI(this);
    const QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (!apiKey.isEmpty())
        m_curseforgeAPI->setApiKey(apiKey);
    // 全局搜索不限定 classId：一次拿到模组 / 整合包 / 资源包 / 光影等全部类型的在线资源，
    // 每条的 classId 会在解析阶段写进 ModInfo::contentKind
    m_curseforgeAPI->setClassId(QString());

    m_modrinthAPI = new ModrinthAPI(this);

    applyMCIMSetting();

    connect(m_curseforgeAPI, &CurseForgeAPI::searchCompleted, this,
            &SearchPage::onCurseforgeSearchCompleted);
    connect(m_curseforgeAPI, &CurseForgeAPI::searchFailed, this,
            [this](const QString &) { onCurseforgeSearchFailed(); });
    connect(m_modrinthAPI, &ModrinthAPI::searchCompleted, this,
            &SearchPage::onModrinthSearchCompleted);
    connect(m_modrinthAPI, &ModrinthAPI::searchFailed, this,
            [this](const QString &) { onModrinthSearchFailed(); });

    m_queryResolver = new SearchQueryResolver(this);
    connect(m_queryResolver, &SearchQueryResolver::resolved, this,
            [this](quint64 requestId, const QString &, const QString &keyword, const QString &) {
                onQueryResolved(requestId, keyword);
            });

    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(kSearchDebounceMs);
    connect(m_debounceTimer, &QTimer::timeout, this, [this]() {
        performSearch(m_searchInput->text().trimmed());
    });
}

void SearchPage::applyMCIMSetting()
{
    const QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    const bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    m_curseforgeAPI->setBaseUrl(useMcim ? QStringLiteral("https://mod.mcimirror.top/curseforge")
                                        : QStringLiteral("https://api.curseforge.com"));
    m_modrinthAPI->setBaseUrl(useMcim ? QStringLiteral("https://mod.mcimirror.top/modrinth")
                                      : QStringLiteral("https://api.modrinth.com"));
}

void SearchPage::setSearchFocus()
{
    if (!m_searchInput)
        return;
    m_searchInput->setFocus();
    m_searchInput->selectAll();
}

void SearchPage::clearSearch()
{
    if (m_searchInput) {
        m_searchInput->blockSignals(true);
        m_searchInput->clear();
        m_searchInput->blockSignals(false);
    }
    m_debounceTimer->stop();
    ++m_queryToken;
    m_searchActive = false;
    m_pendingRequests = 0;
    m_rawKeyword.clear();
    m_onlineKeyword.clear();
    resetResults();
    updateEmptyState();
}

QWidget *SearchPage::createSearchBar()
{
    auto *container = new QWidget();
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignCenter);

    m_searchInput = new QLineEdit(container);
    m_searchInput->setObjectName("searchHeroInput");
    m_searchInput->setPlaceholderText(tr("搜索模组、整合包、光影包、社区帖子..."));
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setFixedHeight(52);
    m_searchInput->setMinimumWidth(320);
    m_searchInput->setMaximumWidth(820);
    m_searchInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const QColor themeColor(ThemeManager::instance()->currentThemeColor());
    m_searchInput->addAction(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/search.svg"),
                                                        themeColor, 20),
                             QLineEdit::LeadingPosition);
    // 两侧留伸缩：窗口再宽搜索框也不会被拉到通栏，窄窗口下按比例收缩
    layout->addStretch(1);
    layout->addWidget(m_searchInput, 3);
    layout->addStretch(1);

    connect(m_searchInput, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.trimmed().isEmpty()) {
            m_debounceTimer->stop();
            performSearch(QString());
            return;
        }
        m_debounceTimer->start();
    });
    connect(m_searchInput, &QLineEdit::returnPressed, this, [this]() {
        m_debounceTimer->stop();
        performSearch(m_searchInput->text().trimmed());
    });

    return container;
}

QWidget *SearchPage::createResultsArea()
{
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(UiMetrics::kPagePadH, 0, UiMetrics::kPagePadH, 16);
    layout->setSpacing(0);

    // 结果整体装进一块玻璃面板：分区标题和卡片都在同一块底色上，背景再花也能看清
    m_resultsPanel = new QWidget(container);
    m_resultsPanel->setObjectName("searchResultsPanel");
    auto *panelLayout = new QVBoxLayout(m_resultsPanel);
    panelLayout->setContentsMargins(12, 4, 12, 12);
    panelLayout->setSpacing(0);
    m_resultsPanel->setVisible(false);

    m_resultsScroll = new QScrollArea(m_resultsPanel);
    m_resultsScroll->setObjectName("searchPageResultsScroll");
    m_resultsScroll->setWidgetResizable(true);
    m_resultsScroll->setFrameShape(QFrame::NoFrame);
    m_resultsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_resultsContainer = new QWidget();
    m_resultsContainer->setObjectName("searchResultsInnerContainer");
    m_resultsLayout = new QVBoxLayout(m_resultsContainer);
    m_resultsLayout->setContentsMargins(0, 0, 0, 0);
    m_resultsLayout->setSpacing(0);

    m_onlineHeader = new QLabel(m_resultsContainer);
    m_onlineHeader->setObjectName("searchSectionTitle");
    m_onlineHeader->hide();

    m_onlineContainer = new QWidget(m_resultsContainer);
    m_onlineLayout = new QVBoxLayout(m_onlineContainer);
    m_onlineLayout->setContentsMargins(0, 0, 0, 0);
    m_onlineLayout->setSpacing(8);
    m_onlineContainer->hide();

    m_forumHeader = new QLabel(m_resultsContainer);
    m_forumHeader->setObjectName("searchSectionTitle");
    m_forumHeader->hide();

    m_forumContainer = new QWidget(m_resultsContainer);
    m_forumLayout = new QVBoxLayout(m_forumContainer);
    m_forumLayout->setContentsMargins(0, 0, 0, 0);
    m_forumLayout->setSpacing(8);
    m_forumContainer->hide();

    m_statusLabel = new QLabel(m_resultsContainer);
    m_statusLabel->setObjectName("searchStatusLabel");
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->hide();

    m_emptyLabel = new QLabel(m_resultsPanel);
    m_emptyLabel->setObjectName("searchNoResults");
    m_emptyLabel->setTextFormat(Qt::PlainText);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();

    m_resultsLayout->addWidget(m_onlineHeader);
    m_resultsLayout->addWidget(m_onlineContainer);
    m_resultsLayout->addWidget(m_forumHeader);
    m_resultsLayout->addWidget(m_forumContainer);
    m_resultsLayout->addWidget(m_statusLabel);
    m_resultsLayout->addStretch(1);

    m_resultsScroll->setWidget(m_resultsContainer);
    panelLayout->addWidget(m_resultsScroll, 1);
    // 空结果提示放在面板里（滚动区之外），文字才能在整块面板正中
    panelLayout->addWidget(m_emptyLabel, 1);
    layout->addWidget(m_resultsPanel, 1);
    return container;
}

void SearchPage::initUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *topWidget = new QWidget(this);
    auto *topLayout = new QVBoxLayout(topWidget);
    topLayout->setContentsMargins(UiMetrics::kPagePadH, 56, UiMetrics::kPagePadH, 0);
    topLayout->setSpacing(14);

    topLayout->addWidget(createSearchBar());

    m_idleHint = new QLabel(tr("输入关键词，搜索在线资源与社区帖子"), topWidget);
    m_idleHint->setObjectName("searchIdleHint");
    m_idleHint->setAlignment(Qt::AlignCenter);
    topLayout->addWidget(m_idleHint);

    mainLayout->addWidget(topWidget);
    mainLayout->addSpacing(12);
    mainLayout->addWidget(createResultsArea(), 1);

    updateEmptyState();
}

void SearchPage::performSearch(const QString &keyword)
{
    const QString trimmed = keyword.trimmed();
    // 同一关键词正在搜索时不要把已有结果清空重来（回车紧跟着防抖触发会走到这里）
    if (m_searchActive && trimmed == m_rawKeyword)
        return;

    ++m_queryToken;
    m_debounceTimer->stop();

    resetResults();
    m_rawKeyword = trimmed;
    m_onlineKeyword.clear();
    m_pendingRequests = 0;
    m_searchActive = false;

    if (trimmed.isEmpty()) {
        updateEmptyState();
        return;
    }

    m_searchActive = true;
    if (m_resultsScroll)
        m_resultsScroll->verticalScrollBar()->setValue(0);

    // 论坛直接用用户原文检索（中文站），下载源需要先把中文词解析成英文名
    startForumSearch(trimmed);

    ++m_pendingRequests;   // 关键词解析
    m_queryResolver->resolve(m_queryToken, trimmed, QString());

    updateEmptyState();
}

void SearchPage::onQueryResolved(quint64 requestId, const QString &keyword)
{
    if (requestId != m_queryToken)
        return;
    if (m_pendingRequests > 0)
        --m_pendingRequests;
    m_onlineKeyword = keyword;
    startOnlineSearch(keyword);
}

void SearchPage::startOnlineSearch(const QString &query)
{
    m_curseforgeFailed = false;
    m_modrinthFailed = false;
    m_curseforgeMods.clear();
    m_modrinthMods.clear();
    m_onlinePending = 2;
    m_pendingRequests += 2;

    m_curseforgeAPI->searchMods(query, QString(), QString(), 0, kRequestPageSize,
                                QStringLiteral("popularity"));
    m_modrinthAPI->searchAllProjectTypes(query, 0, kRequestPageSize, QStringLiteral("relevance"));
}

void SearchPage::startForumSearch(const QString &query)
{
    ++m_pendingRequests;

    const quint64 token = m_queryToken;
    QPointer<SearchPage> self(this);
    ForumContentService::instance()->searchThreads(
        query, 1,
        [self, token](const QString &, const QVector<ForumThreadInfo> &threads,
                      const ForumPagination &) {
            if (!self || self->m_queryToken != token)
                return;
            self->appendForumResults(threads);
            if (self->m_pendingRequests > 0)
                --self->m_pendingRequests;
            if (self->m_pendingRequests <= 0)
                self->finishSearch();
        },
        [self, token](const QString &, int) {
            if (!self || self->m_queryToken != token)
                return;
            if (self->m_pendingRequests > 0)
                --self->m_pendingRequests;
            if (self->m_pendingRequests <= 0)
                self->finishSearch();
        });
}

void SearchPage::onCurseforgeSearchCompleted(const ModSearchResult &result)
{
    if (!m_searchActive || m_onlinePending <= 0)
        return;
    m_curseforgeMods = result.mods;
    --m_onlinePending;
    if (m_pendingRequests > 0)
        --m_pendingRequests;
    if (m_onlinePending <= 0)
        appendMergedOnlineResults();
    if (m_pendingRequests <= 0)
        finishSearch();
}

void SearchPage::onModrinthSearchCompleted(const ModSearchResult &result)
{
    if (!m_searchActive || m_onlinePending <= 0)
        return;
    m_modrinthMods = result.mods;
    --m_onlinePending;
    if (m_pendingRequests > 0)
        --m_pendingRequests;
    if (m_onlinePending <= 0)
        appendMergedOnlineResults();
    if (m_pendingRequests <= 0)
        finishSearch();
}

void SearchPage::onCurseforgeSearchFailed()
{
    if (!m_searchActive || m_onlinePending <= 0)
        return;
    // 一个源失败不影响另一个源：只记标记，等两源都收尾后再合并
    m_curseforgeFailed = true;
    --m_onlinePending;
    if (m_pendingRequests > 0)
        --m_pendingRequests;
    if (m_onlinePending <= 0)
        appendMergedOnlineResults();
    if (m_pendingRequests <= 0)
        finishSearch();
}

void SearchPage::onModrinthSearchFailed()
{
    if (!m_searchActive || m_onlinePending <= 0)
        return;
    m_modrinthFailed = true;
    --m_onlinePending;
    if (m_pendingRequests > 0)
        --m_pendingRequests;
    if (m_onlinePending <= 0)
        appendMergedOnlineResults();
    if (m_pendingRequests <= 0)
        finishSearch();
}

void SearchPage::appendMergedOnlineResults()
{
    const QList<ModInfo> modrinth = m_modrinthFailed ? QList<ModInfo>() : m_modrinthMods;
    const QList<ModInfo> curseforge = m_curseforgeFailed ? QList<ModInfo>() : m_curseforgeMods;
    appendOnlineResults(mergeCrossSourceResults(modrinth, curseforge));
    m_curseforgeMods.clear();
    m_modrinthMods.clear();
}

void SearchPage::finishSearch()
{
    m_searchActive = false;
    refreshSectionTitles();
    updateEmptyState();
}

void SearchPage::resetResults()
{
    m_items.clear();
    m_seenKeys.clear();
    m_onlinePending = 0;
    clearResultWidgets();

    if (m_statusLabel)
        m_statusLabel->hide();
    if (m_emptyLabel)
        m_emptyLabel->hide();
    refreshSectionTitles();
}

void SearchPage::clearResultWidgets()
{
    const auto clearLayout = [this](QVBoxLayout *layout) {
        if (!layout)
            return;
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (QWidget *widget = item->widget()) {
                widget->removeEventFilter(this);
                widget->deleteLater();
            }
            delete item;
        }
    };
    clearLayout(m_onlineLayout);
    clearLayout(m_forumLayout);
}

void SearchPage::appendOnlineResults(const QList<ModInfo> &mods)
{
    int added = 0;
    for (const ModInfo &info : mods) {
        if (added >= kMaxOnlineCards)
            break;

        ContentType type = ContentType::Mod;
        if (!contentTypeOfKind(info.contentKind, &type))
            continue;   // 不是启动器能安装的内容类型（插件等）

        QString key = info.source + QLatin1Char(':') + info.id;
        if (key.endsWith(QLatin1Char(':')) || m_seenKeys.contains(key))
            continue;

        SearchResultItem item;
        item.kind = SearchResultItem::OnlineResource;
        item.title = info.chineseName.isEmpty() ? info.name : info.chineseName;
        item.summary = singleLine(info.description);
        item.sourceLabel = sourceLabelOf(info);
        item.kindLabel = ContentTypeConfig::getConfig(type).displayName;
        item.contentType = type;
        item.modInfo = info;
        item.iconUrl = info.iconUrl.isEmpty() ? info.altIconUrl : info.iconUrl;
        item.downloadCount = info.downloadCount;

        if (!info.gameVersions.isEmpty())
            item.chips << info.gameVersions.first();
        if (!info.loaders.isEmpty())
            item.chips << info.loaders.first();
        if (item.chips.isEmpty() && !info.categories.isEmpty())
            item.chips << info.categories.first();
        if (info.dateModified.isValid())
            item.chips << info.dateModified.toString(QStringLiteral("yyyy-MM-dd"));

        m_seenKeys.insert(key);
        if (info.isCrossSourceMerged())
            m_seenKeys.insert(info.altSource + QLatin1Char(':') + info.altId);

        m_items.append(item);
        m_onlineLayout->addWidget(buildResultCard(item, m_items.size() - 1));
        ++added;
    }

    refreshSectionTitles();
    updateEmptyState();
}

void SearchPage::appendForumResults(const QVector<ForumThreadInfo> &threads)
{
    int added = 0;
    for (const ForumThreadInfo &thread : threads) {
        if (added >= kMaxForumCards)
            break;
        if (thread.id <= 0)
            continue;

        const QString key = QStringLiteral("forum:%1").arg(thread.id);
        if (m_seenKeys.contains(key))
            continue;

        SearchResultItem item;
        item.kind = SearchResultItem::ForumThread;
        item.title = thread.title;
        item.summary = singleLine(thread.snippet);
        item.sourceLabel = tr("社区");
        item.threadId = thread.id;
        item.replies = thread.replies;
        item.views = thread.views;

        if (!thread.boardName.isEmpty())
            item.chips << thread.boardName;
        if (!thread.tags.isEmpty())
            item.chips << thread.tags.first();
        const qint64 activeAt = thread.lastPostAt > 0 ? thread.lastPostAt : thread.createdAt;
        if (activeAt > 0)
            item.chips << ForumKit::relativeTime(activeAt);

        m_seenKeys.insert(key);

        m_items.append(item);
        m_forumLayout->addWidget(buildResultCard(item, m_items.size() - 1));
        ++added;
    }

    refreshSectionTitles();
    updateEmptyState();
}

QWidget *SearchPage::buildResultCard(const SearchResultItem &item, int index)
{
    const QColor themeColor(ThemeManager::instance()->currentThemeColor());
    const bool isForum = item.kind == SearchResultItem::ForumThread;

    auto *card = new QWidget();
    card->setObjectName("modCardListItem");   // 与「下载模组」等页的模组卡片同一套外观
    card->setProperty("cardRole", "container");
    card->setProperty("resultIndex", index);
    card->setCursor(Qt::PointingHandCursor);
    card->setMinimumHeight(88);

    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(14, 12, 14, 12);
    row->setSpacing(14);

    // 图标：先画渐变字母占位，在线资源拿到封面图标后替换
    auto *iconLabel = new QLabel(card);
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(kIconSize, kIconSize);
    iconLabel->setAlignment(Qt::AlignCenter);
    if (isForum) {
        iconLabel->setPixmap(makeLetterIcon(item.title, QColor(0x63, 0x66, 0xF1),
                                            QColor(0x43, 0x38, 0xCA), kIconSize));
    } else {
        iconLabel->setPixmap(makeLetterIcon(item.title, themeColor, themeColor.darker(135),
                                            kIconSize));
    }
    row->addWidget(iconLabel, 0, Qt::AlignVCenter);

    // 中间：标题 / 简介 / 信息芯片
    auto *infoColumn = new QVBoxLayout();
    infoColumn->setContentsMargins(0, 0, 0, 0);
    infoColumn->setSpacing(4);

    auto *titleLabel = new QLabel(card);
    titleLabel->setObjectName("modCardName");
    titleLabel->setTextFormat(Qt::RichText);
    titleLabel->setText(highlightKeyword(item.title));
    titleLabel->setWordWrap(false);
    titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    infoColumn->addWidget(titleLabel);

    auto *summaryLabel = new ForumKit::ElidedLabel(card);
    summaryLabel->setObjectName("contentCardDesc");
    summaryLabel->setFullText(item.summary);
    infoColumn->addWidget(summaryLabel);

    auto *chipsRow = new QHBoxLayout();
    chipsRow->setContentsMargins(0, 0, 0, 0);
    chipsRow->setSpacing(6);
    QStringList chips;
    chips << item.kindLabel << item.sourceLabel << item.chips;
    for (const QString &chip : chips) {
        if (chip.isEmpty())
            continue;
        auto *chipLabel = new QLabel(chip, card);
        chipLabel->setObjectName("modChip");
        chipLabel->setProperty("cardRole", "chip");
        chipsRow->addWidget(chipLabel);
    }
    chipsRow->addStretch(1);
    infoColumn->addLayout(chipsRow);

    row->addLayout(infoColumn, 1);

    // 右侧：统计数字 + 打开按钮
    auto *rightColumn = new QVBoxLayout();
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(6);
    rightColumn->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    QString statText;
    if (isForum) {
        if (item.replies >= 0 && item.views >= 0)
            statText = tr("%1 回复 · %2 浏览").arg(item.replies).arg(item.views);
        else if (item.replies >= 0)
            statText = tr("%1 回复").arg(item.replies);
    } else if (item.downloadCount > 0) {
        statText = tr("%1 下载").arg(formatCount(item.downloadCount));
    }
    if (!statText.isEmpty()) {
        auto *statLabel = new QLabel(statText, card);
        statLabel->setObjectName("searchResultStatChip");
        rightColumn->addWidget(statLabel, 0, Qt::AlignRight);
    }

    auto *openButton = new QPushButton(card);
    openButton->setObjectName("modCardActionBtn");
    openButton->setProperty("cardRole", "actionBtn");
    openButton->setFixedSize(32, 32);
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setToolTip(isForum ? tr("打开帖子") : tr("查看详情"));
    openButton->setIcon(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/chevron_right.svg"),
                                                    themeColor, 18));
    openButton->setIconSize(QSize(18, 18));
    connect(openButton, &QPushButton::clicked, card, [this, index]() { onCardActivated(index); });
    rightColumn->addWidget(openButton, 0, Qt::AlignRight);

    row->addLayout(rightColumn);

    card->installEventFilter(this);

    if (!item.iconUrl.isEmpty())
        loadCardIcon(iconLabel, item.iconUrl);

    return card;
}

QPixmap SearchPage::makeLetterIcon(const QString &text, const QColor &c1, const QColor &c2,
                                   int size) const
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal radius = size * 0.23;
    painter.setPen(Qt::NoPen);
    QLinearGradient gradient(0, 0, size, size);
    gradient.setColorAt(0.0, c1);
    gradient.setColorAt(1.0, c2);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(0, 0, size, size), radius, radius);

    const QString letter = text.trimmed().isEmpty() ? QStringLiteral("?")
                                                    : text.trimmed().left(1).toUpper();
    painter.setPen(Qt::white);
    QFont font;
    font.setBold(true);
    font.setPixelSize(qRound(size * 0.42));
    painter.setFont(font);
    painter.drawText(pix.rect(), Qt::AlignCenter, letter);
    painter.end();
    return pix;
}

void SearchPage::loadCardIcon(QLabel *label, const QString &iconUrl)
{
    if (!label || iconUrl.isEmpty())
        return;
    // 拉取失败时保留渐变字母占位，不用空图遮住
    McimHelper::loadImage(m_networkManager, label, iconUrl, QSize(kIconSize, kIconSize),
                          QStringLiteral("search_card_icon:%1").arg(iconUrl));
}

void SearchPage::refreshSectionTitles()
{
    if (!m_onlineHeader || !m_forumHeader)
        return;

    const int onlineCount = m_onlineLayout ? m_onlineLayout->count() : 0;
    const int forumCount = m_forumLayout ? m_forumLayout->count() : 0;

    m_onlineHeader->setText(tr("在线资源 · %1").arg(onlineCount));
    m_onlineHeader->setVisible(onlineCount > 0);
    m_onlineContainer->setVisible(onlineCount > 0);

    m_forumHeader->setText(tr("社区讨论 · %1").arg(forumCount));
    m_forumHeader->setVisible(forumCount > 0);
    m_forumContainer->setVisible(forumCount > 0);
}

void SearchPage::updateEmptyState()
{
    if (!m_resultsPanel)
        return;

    const bool hasKeyword = !m_rawKeyword.isEmpty();
    m_resultsPanel->setVisible(hasKeyword);
    m_idleHint->setVisible(!hasKeyword);

    if (!hasKeyword) {
        m_statusLabel->hide();
        m_emptyLabel->hide();
        return;
    }

    if (m_searchActive) {
        m_emptyLabel->hide();
        m_resultsScroll->show();
        m_statusLabel->setText(tr("正在搜索…"));
        m_statusLabel->show();
        return;
    }

    if (m_items.isEmpty()) {
        m_statusLabel->hide();
        m_resultsScroll->hide();
        m_emptyLabel->setText(tr("没有找到与「%1」相关的内容").arg(m_rawKeyword));
        m_emptyLabel->show();
        return;
    }

    m_resultsScroll->show();
    m_emptyLabel->hide();
    if (m_curseforgeFailed && m_modrinthFailed) {
        m_statusLabel->setText(tr("在线资源暂时无法访问，只显示了社区结果"));
        m_statusLabel->show();
    } else {
        m_statusLabel->hide();
    }
}

void SearchPage::onCardActivated(int index)
{
    if (index < 0 || index >= m_items.size())
        return;

    const SearchResultItem &item = m_items.at(index);
    if (item.kind == SearchResultItem::ForumThread) {
        emit navigateToForumThread(item.threadId);
        return;
    }
    emit navigateToResourceDetail(item.modInfo, item.contentType);
}

QString SearchPage::highlightKeyword(const QString &text) const
{
    QStringList terms;
    if (!m_rawKeyword.isEmpty())
        terms << m_rawKeyword;
    if (!m_onlineKeyword.isEmpty() && !terms.contains(m_onlineKeyword))
        terms << m_onlineKeyword;

    int index = -1;
    int length = 0;
    for (const QString &term : terms) {
        const int found = text.indexOf(term, 0, Qt::CaseInsensitive);
        if (found >= 0) {
            index = found;
            length = term.length();
            break;
        }
    }
    if (index < 0)
        return text.toHtmlEscaped();

    return text.left(index).toHtmlEscaped()
           + QStringLiteral("<span style=\"background-color:rgba(245,158,11,0.35);\">")
           + text.mid(index, length).toHtmlEscaped()
           + QStringLiteral("</span>")
           + text.mid(index + length).toHtmlEscaped();
}

bool SearchPage::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *widget = qobject_cast<QWidget *>(obj);
        if (widget) {
            const QVariant indexValue = widget->property("resultIndex");
            if (indexValue.isValid()) {
                onCardActivated(indexValue.toInt());
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
