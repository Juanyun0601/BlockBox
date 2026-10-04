#include "BedrockContentPage.h"

#include "../layouts/FlowLayout.h"
#include "../layouts/MasonryLayout.h"
#include "../utils/McimHelper.h"
#include "components/BlurLoadingOverlay.h"
#include "components/ContentViewSwitch.h"
#include "components/FavoriteFolderDialog.h"
#include "components/MasonryContentCard.h"
#include "components/NotificationManager.h"
#include "utils/FavoritesManager.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModData.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPixmapCache>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpacerItem>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

// CurseForge Minecraft Bedrock Edition (游戏 ID 78022) 分类
struct BedrockCategoryDef {
    int classId;
    const char *name;
};

static const BedrockCategoryDef kBedrockClasses[] = {
    { 4984, QT_TRANSLATE_NOOP("BedrockContentPage", "附加包") },  // Addons
    { 6929, QT_TRANSLATE_NOOP("BedrockContentPage", "资源包") },  // Texture Packs
    { 6913, QT_TRANSLATE_NOOP("BedrockContentPage", "地图") },    // Maps
    { 6940, QT_TRANSLATE_NOOP("BedrockContentPage", "脚本") },    // Scripts
    { 6925, QT_TRANSLATE_NOOP("BedrockContentPage", "皮肤") },    // Skins
};

static QNetworkAccessManager *s_bedrockIconNAM = nullptr;

BedrockContentPage::BedrockContentPage(QWidget *parent)
    : QWidget(parent)
{
    initUI();
    initAPI();
    setupConnections();
    QTimer::singleShot(0, this, &BedrockContentPage::loadInitialContent);
}

BedrockContentPage::~BedrockContentPage()
{
}

void BedrockContentPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // 顶部搜索/筛选条：微透明玻璃态卡片
    QWidget *filterCard = new QWidget(this);
    filterCard->setObjectName("filterCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(10);

    // ── 搜索栏 ──
    QHBoxLayout *searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 0, 0, 0);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索基岩版社区资源..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(36);
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    m_searchEdit->addAction(IconHelper::loadColoredIcon(":/Images/Icons/search.svg", themeColor, 18),
                            QLineEdit::LeadingPosition);
    searchLayout->addWidget(m_searchEdit);

    QPushButton *searchBtn = new QPushButton(tr("搜索"));
    searchBtn->setObjectName("modSearchBtn");
    searchBtn->setFixedHeight(36);
    searchBtn->setFixedWidth(80);
    searchLayout->addWidget(searchBtn);
    connect(searchBtn, &QPushButton::clicked, this, &BedrockContentPage::onSearchTriggered);

    cardLayout->addLayout(searchLayout);

    // ── 筛选栏 ──
    FlowLayout *filterLayout = new FlowLayout(nullptr, 0, 10, 8);

    auto createCombo = [this]() -> QComboBox* {
        QComboBox *combo = new QComboBox();
        combo->setFixedHeight(32);
        combo->setMinimumWidth(120);
        return combo;
    };

    QLabel *srcLabel = new QLabel(tr("下载源:"));
    srcLabel->setObjectName("filterLabel");
    QLabel *srcValue = new QLabel(tr("CurseForge"));
    srcValue->setObjectName("filterLabel");
    filterLayout->addWidget(srcLabel);
    filterLayout->addWidget(srcValue);

    QLabel *catLabel = new QLabel(tr("分类:"));
    catLabel->setObjectName("filterLabel");
    m_categoryCombo = createCombo();
    m_categoryCombo->setObjectName("filterCombo");
    populateCategoryCombo();
    filterLayout->addWidget(catLabel);
    filterLayout->addWidget(m_categoryCombo);

    QLabel *sortLabel = new QLabel(tr("排序方式:"));
    sortLabel->setObjectName("filterLabel");
    m_sortCombo = createCombo();
    m_sortCombo->setObjectName("sortCombo");
    m_sortCombo->addItem(tr("相关度"), "relevance");
    m_sortCombo->addItem(tr("热门/下载量"), "popularity");
    m_sortCombo->addItem(tr("名称"), "name");
    m_sortCombo->addItem(tr("最近更新"), "last_updated");
    m_sortCombo->addItem(tr("总下载量"), "total_downloads");
    m_sortCombo->addItem(tr("创建日期"), "date_created");
    m_sortCombo->addItem(tr("作者"), "author");
    filterLayout->addWidget(sortLabel);
    filterLayout->addWidget(m_sortCombo);

    m_viewSwitch = new ContentViewSwitch(this);
    m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted("bedrock_content",
                                                               ContentViewSwitch::Masonry));
    m_viewMode = static_cast<int>(m_viewSwitch->viewMode());
    filterLayout->addWidget(m_viewSwitch);

    cardLayout->addLayout(filterLayout);
    mainLayout->addWidget(filterCard);

    // ── 滚动卡片区 ──
    m_scrollArea = new QScrollArea();
    m_scrollArea->setObjectName("modScrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_cardContainer = new QWidget();
    m_cardContainer->setObjectName("modCardContainer");
    if (m_viewMode == ContentViewSwitch::List) {
        m_cardGridLayout = new QGridLayout(m_cardContainer);
        m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
        m_cardGridLayout->setSpacing(8);
    } else {
        m_cardFlowLayout = new MasonryLayout(m_cardContainer, 0, 10, 10);
        m_cardFlowLayout->setContentsMargins(14, 0, 14, 0);
    }
    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();

    // ── 分页 ──
    m_paginationBar = new QWidget();
    m_paginationBar->setObjectName("modPaginationBar");
    QHBoxLayout *paginationLayout = new QHBoxLayout(m_paginationBar);
    paginationLayout->setContentsMargins(14, 8, 14, 8);
    paginationLayout->setSpacing(8);
    paginationLayout->addStretch();

    m_firstPageBtn = new QPushButton(tr("首页"));
    m_firstPageBtn->setObjectName("modPageBtn");
    m_firstPageBtn->setFixedHeight(32);
    m_firstPageBtn->setEnabled(false);
    paginationLayout->addWidget(m_firstPageBtn);

    m_prevPageBtn = new QPushButton(tr("上一页"));
    m_prevPageBtn->setObjectName("modPageBtn");
    m_prevPageBtn->setFixedHeight(32);
    m_prevPageBtn->setEnabled(false);
    paginationLayout->addWidget(m_prevPageBtn);

    m_pageLabel = new QLabel(tr("第 1 页"));
    m_pageLabel->setObjectName("modPageLabel");
    m_pageLabel->setFixedHeight(32);
    paginationLayout->addWidget(m_pageLabel);

    m_nextPageBtn = new QPushButton(tr("下一页"));
    m_nextPageBtn->setObjectName("modPageBtn");
    m_nextPageBtn->setFixedHeight(32);
    m_nextPageBtn->setEnabled(false);
    paginationLayout->addWidget(m_nextPageBtn);

    m_lastPageBtn = new QPushButton(tr("末页"));
    m_lastPageBtn->setObjectName("modPageBtn");
    m_lastPageBtn->setFixedHeight(32);
    m_lastPageBtn->setEnabled(false);
    paginationLayout->addWidget(m_lastPageBtn);

    paginationLayout->addStretch();
    m_paginationBar->hide();
    mainLayout->addWidget(m_paginationBar);

    connect(m_firstPageBtn, &QPushButton::clicked, this, &BedrockContentPage::onFirstPage);
    connect(m_prevPageBtn, &QPushButton::clicked, this, &BedrockContentPage::onPrevPage);
    connect(m_nextPageBtn, &QPushButton::clicked, this, &BedrockContentPage::onNextPage);
    connect(m_lastPageBtn, &QPushButton::clicked, this, &BedrockContentPage::onLastPage);
}

void BedrockContentPage::populateCategoryCombo()
{
    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();
    m_categoryCombo->addItem(tr("全部分类"), 0);
    for (const BedrockCategoryDef &def : kBedrockClasses)
        m_categoryCombo->addItem(tr(def.name), def.classId);
    m_categoryCombo->blockSignals(false);
}

void BedrockContentPage::initAPI()
{
    m_curseforgeAPI = new CurseForgeAPI(this);
    m_curseforgeAPI->setGameId(QStringLiteral("78022"));
    const QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (!apiKey.isEmpty())
        m_curseforgeAPI->setApiKey(apiKey);
    applyMCIMSetting();
}

void BedrockContentPage::applyMCIMSetting()
{
    const QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    const bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    m_curseforgeAPI->setBaseUrl(useMcim ? QString(MCIM_BASE) + "/curseforge" : CF_BASE);
}

void BedrockContentPage::setupConnections()
{
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &BedrockContentPage::onSearchTriggered);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BedrockContentPage::onCategoryChanged);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BedrockContentPage::onSortChanged);
    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged,
            this, &BedrockContentPage::onViewModeChanged);

    connect(m_curseforgeAPI, &CurseForgeAPI::searchCompleted,
            this, &BedrockContentPage::onSearchCompleted);
    connect(m_curseforgeAPI, &CurseForgeAPI::searchFailed,
            this, &BedrockContentPage::onSearchFailed);
}

void BedrockContentPage::setCategory(int classId)
{
    m_currentCategory = classId;
    const int idx = m_categoryCombo->findData(classId);
    if (idx >= 0) {
        m_categoryCombo->blockSignals(true);
        m_categoryCombo->setCurrentIndex(idx);
        m_categoryCombo->blockSignals(false);
    }
    loadInitialContent();
}

void BedrockContentPage::setSearchText(const QString &text)
{
    if (text.isEmpty())
        return;
    m_searchEdit->setText(text);
    onSearchTriggered();
}

void BedrockContentPage::loadInitialContent()
{
    applyMCIMSetting();
    m_currentQuery.clear();
    m_currentPage = 0;
    m_allMods.clear();
    clearCards();
    m_isLoading = false;
    performSearch(false);
}

void BedrockContentPage::onSearchTriggered()
{
    m_currentQuery = m_searchEdit->text().trimmed();
    m_currentPage = 0;
    m_allMods.clear();
    performSearch(false);
}

void BedrockContentPage::onCategoryChanged(int)
{
    m_currentCategory = m_categoryCombo->currentData().toInt();
    m_currentPage = 0;
    m_allMods.clear();
    performSearch(false);
}

void BedrockContentPage::onSortChanged(int)
{
    if (!m_currentQuery.isEmpty()) {
        m_currentPage = 0;
        m_allMods.clear();
        performSearch(false);
    }
}

void BedrockContentPage::onViewModeChanged(ContentViewSwitch::ViewMode mode)
{
    m_viewMode = static_cast<int>(mode);
    ContentViewSwitch::savePersisted("bedrock_content", mode);
    rebuildCards();
}

QString BedrockContentPage::selectedClassId() const
{
    return m_currentCategory > 0 ? QString::number(m_currentCategory) : QString();
}

QString BedrockContentPage::selectedSortField() const
{
    const QString universal = m_sortCombo->currentData().toString();
    QMap<QString, QString> cfSortMap;
    cfSortMap["relevance"]      = "popularity";
    cfSortMap["popularity"]     = "popularity";
    cfSortMap["name"]           = "name";
    cfSortMap["last_updated"]   = "last_updated";
    cfSortMap["total_downloads"] = "total_downloads";
    cfSortMap["date_created"]   = "date_created";
    cfSortMap["author"]         = "author";
    return cfSortMap.value(universal, "popularity");
}

void BedrockContentPage::performSearch(bool append)
{
    if (m_isLoading)
        return;
    if (!append) {
        clearCards();
        m_allMods.clear();
        m_totalHits = 0;
        m_hasMore = false;
        m_pageCache.clear();
    }
    showLoading(true);
    m_isLoading = true;
    m_paginationBar->hide();

    m_curseforgeAPI->setClassId(selectedClassId());
    m_curseforgeAPI->searchMods(m_currentQuery, QString(), QString(),
                                m_currentPage, 20, selectedSortField());
}

void BedrockContentPage::onSearchCompleted(const ModSearchResult &result)
{
    m_pageCache[m_currentPage] = result.mods;
    m_allMods.append(result.mods);
    for (const ModInfo &info : result.mods)
        addCard(info);

    m_hasMore = result.hasMore;
    m_totalHits = result.totalHits;

    m_isLoading = false;
    showLoading(false);
    placeCards();
    updatePaginationControls();
    m_loadingOverlay->clearError();
}

void BedrockContentPage::onSearchFailed(const QString &error)
{
    m_isLoading = false;
    showLoading(false);
    m_loadingOverlay->showError(error);
    updatePaginationControls();
}

void BedrockContentPage::onFirstPage()
{
    if (!m_isLoading && m_currentPage > 0) {
        m_currentPage = 0;
        m_allMods.clear();
        if (m_pageCache.contains(m_currentPage)) {
            for (const ModInfo &info : m_pageCache[m_currentPage])
                addCard(info);
            m_isLoading = false;
            showLoading(false);
            placeCards();
            updatePaginationControls();
            m_loadingOverlay->clearError();
        } else {
            performSearch(false);
        }
    }
}

void BedrockContentPage::onPrevPage()
{
    if (!m_isLoading && m_currentPage > 0) {
        m_currentPage--;
        m_allMods.clear();
        if (m_pageCache.contains(m_currentPage)) {
            for (const ModInfo &info : m_pageCache[m_currentPage])
                addCard(info);
            m_isLoading = false;
            showLoading(false);
            placeCards();
            updatePaginationControls();
            m_loadingOverlay->clearError();
        } else {
            performSearch(false);
        }
    }
}

void BedrockContentPage::onNextPage()
{
    if (!m_isLoading && m_hasMore) {
        m_currentPage++;
        m_allMods.clear();
        if (m_pageCache.contains(m_currentPage)) {
            for (const ModInfo &info : m_pageCache[m_currentPage])
                addCard(info);
            m_isLoading = false;
            showLoading(false);
            placeCards();
            updatePaginationControls();
            m_loadingOverlay->clearError();
        } else {
            performSearch(false);
        }
    }
}

void BedrockContentPage::onLastPage()
{
    if (!m_isLoading && m_hasMore && m_totalHits > 0) {
        const int pageSize = 20;
        const int lastPage = qMax(0, (m_totalHits + pageSize - 1) / pageSize - 1);
        if (m_currentPage != lastPage) {
            m_currentPage = lastPage;
            m_allMods.clear();
            if (m_pageCache.contains(m_currentPage)) {
                for (const ModInfo &info : m_pageCache[m_currentPage])
                    addCard(info);
                m_isLoading = false;
                showLoading(false);
                placeCards();
                updatePaginationControls();
                m_loadingOverlay->clearError();
            } else {
                performSearch(false);
            }
        }
    }
}

void BedrockContentPage::clearCards()
{
    for (QWidget *w : m_cardWidgets) {
        if (m_cardGridLayout)
            m_cardGridLayout->removeWidget(w);
        if (m_cardFlowLayout)
            m_cardFlowLayout->removeWidget(w);
        w->deleteLater();
    }
    m_cardWidgets.clear();
    m_cardInfos.clear();
}

void BedrockContentPage::addCard(const ModInfo &info)
{
    m_cardInfos.append(info);
    if (m_viewMode == ContentViewSwitch::List)
        addListCard(info);
    else
        addMasonryCard(info);
}

void BedrockContentPage::addListCard(const ModInfo &info)
{
    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, [this, card](const QPoint &pos) {
        int idx = m_cardWidgets.indexOf(card);
        if (idx < 0 || idx >= m_allMods.size())
            return;
        const ModInfo &info = m_allMods[idx];
        QMenu menu(this);
        QAction *detailAction = menu.addAction(tr("查看详情"));
        menu.addSeparator();
        QAction *copyNameAction = menu.addAction(tr("复制名称"));
        QAction *copyLinkAction = info.pageUrl.isEmpty() ? nullptr
            : menu.addAction(tr("复制链接"));
        menu.addSeparator();
        QAction *openPageAction = info.pageUrl.isEmpty() ? nullptr
            : menu.addAction(tr("在浏览器中打开"));
        QAction *chosen = menu.exec(card->mapToGlobal(pos));
        if (chosen == detailAction)
            emit contentDetailRequested(info, m_currentCategory);
        else if (chosen == copyNameAction)
            QApplication::clipboard()->setText(info.name);
        else if (chosen == copyLinkAction)
            QApplication::clipboard()->setText(info.pageUrl);
        else if (chosen == openPageAction)
            QDesktopServices::openUrl(QUrl(info.pageUrl));
    });

    QHBoxLayout *mainLayout = new QHBoxLayout(card);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    QPixmap placeholder(48, 48);
    placeholder.fill(ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme
                         ? QColor("#2d2d2d") : QColor("#e8e8e8"));
    iconLabel->setPixmap(placeholder);
    mainLayout->addWidget(iconLabel);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *nameLabel = new QLabel(info.name);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    infoLayout->addWidget(nameLabel);

    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    auto addChip = [&](const QString &text) {
        if (text.isEmpty())
            return;
        QLabel *chip = new QLabel(text);
        chip->setObjectName("modChip");
        chip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(chip);
    };
    addChip(info.categories.value(0));
    addChip(info.gameVersions.value(0));
    if (info.dateModified.isValid())
        addChip(info.dateModified.toString("yyyy-MM-dd"));
    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);
    mainLayout->addLayout(infoLayout, 1);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    QPushButton *favBtn = new QPushButton();
    favBtn->setObjectName("modCardActionBtn");
    favBtn->setProperty("cardRole", "actionBtn");
    favBtn->setFixedSize(32, 32);
    favBtn->setToolTip(tr("收藏"));
    favBtn->setCursor(Qt::PointingHandCursor);
    favBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/star.svg", QColor("#888888"), 18));
    favBtn->setIconSize(QSize(18, 18));
    connect(favBtn, &QPushButton::clicked, this, [this, info, favBtn]() {
        showFavoriteMenu(info, favBtn);
    });
    mainLayout->addWidget(favBtn);

    QPushButton *dlBtn = new QPushButton();
    dlBtn->setObjectName("modCardActionBtn");
    dlBtn->setProperty("cardRole", "actionBtn");
    dlBtn->setFixedSize(32, 32);
    dlBtn->setToolTip(tr("查看详情"));
    dlBtn->setCursor(Qt::PointingHandCursor);
    dlBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/download.svg", themeColor, 18));
    dlBtn->setIconSize(QSize(18, 18));
    connect(dlBtn, &QPushButton::clicked, this, [this, info]() {
        emit contentDetailRequested(info, m_currentCategory);
    });
    mainLayout->addWidget(dlBtn);

    card->installEventFilter(this);
    m_cardWidgets.append(card);

    if (!info.iconUrl.isEmpty())
        loadCardIcon(iconLabel, info.iconUrl);
}

/* 瀑布流卡片操作配置（小卡片展开态与瀑布流共用） */
QList<MasonryContentCard::ActionSpec> BedrockContentPage::buildMasonryActions(const ModInfo &info)
{
    QList<MasonryContentCard::ActionSpec> actions;
    actions.append({ QStringLiteral(":/Images/Icons/star.svg"), tr("收藏"), QColor("#888888"),
                     [this, info]() { showFavoriteMenu(info, nullptr); } });
    actions.append({ QStringLiteral(":/Images/Icons/download.svg"), tr("查看详情"), QColor(),
                     [this, info]() { emit contentDetailRequested(info, m_currentCategory); } });
    return actions;
}

void BedrockContentPage::addMasonryCard(const ModInfo &info)
{
    QWidget *card = MasonryContentCard::build(info, m_cardContainer, buildMasonryActions(info));
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, [this, card](const QPoint &pos) {
        int idx = m_cardWidgets.indexOf(card);
        if (idx < 0 || idx >= m_allMods.size())
            return;
        const ModInfo &info = m_allMods[idx];
        QMenu menu(this);
        QAction *detailAction = menu.addAction(tr("查看详情"));
        menu.addSeparator();
        QAction *copyNameAction = menu.addAction(tr("复制名称"));
        QAction *copyLinkAction = info.pageUrl.isEmpty() ? nullptr
            : menu.addAction(tr("复制链接"));
        menu.addSeparator();
        QAction *openPageAction = info.pageUrl.isEmpty() ? nullptr
            : menu.addAction(tr("在浏览器中打开"));
        QAction *chosen = menu.exec(card->mapToGlobal(pos));
        if (chosen == detailAction)
            emit contentDetailRequested(info, m_currentCategory);
        else if (chosen == copyNameAction)
            QApplication::clipboard()->setText(info.name);
        else if (chosen == copyLinkAction)
            QApplication::clipboard()->setText(info.pageUrl);
        else if (chosen == openPageAction)
            QDesktopServices::openUrl(QUrl(info.pageUrl));
    });

    card->installEventFilter(this);
    m_cardWidgets.append(card);
}

void BedrockContentPage::rebuildCards()
{
    const QList<ModInfo> infos = m_cardInfos;
    clearCards();

    if (m_cardGridLayout) { delete m_cardGridLayout; m_cardGridLayout = nullptr; }
    if (m_cardFlowLayout) { delete m_cardFlowLayout; m_cardFlowLayout = nullptr; }
    if (m_viewMode == ContentViewSwitch::List) {
        m_cardGridLayout = new QGridLayout(m_cardContainer);
        m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
        m_cardGridLayout->setSpacing(8);
    } else {
        m_cardFlowLayout = new MasonryLayout(m_cardContainer, 0, 10, 10);
        m_cardFlowLayout->setContentsMargins(14, 0, 14, 0);
    }

    for (const ModInfo &info : infos)
        addCard(info);
    placeCards();
}

void BedrockContentPage::showFavoriteMenu(const ModInfo &info, QWidget *anchor)
{
    if (info.id.isEmpty())
    {
        NotificationManager::showInfo(this, tr("当前资源信息不完整，无法收藏"));
        return;
    }

    const QString itemId = QStringLiteral("curseforge:") + info.id;
    const ContentType type = FavoritesManager::bedrockClassIdToContentType(m_currentCategory);
    const QString category = FavoritesManager::bedrockClassIdToCategoryName(m_currentCategory);

    QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
    QList<QString> containingIds = FavoritesManager::instance()->foldersContaining(itemId);

    QMenu menu(this);
    menu.setWindowTitle(tr("收藏到分组"));

    if (folders.isEmpty())
    {
        menu.addAction(tr("暂无收藏夹"))->setEnabled(false);
    }
    else
    {
        for (const FavoriteFolder &f : folders)
        {
            bool contains = containingIds.contains(f.id);
            QString label = f.name + (contains ? QStringLiteral("  ✓") : QString());
            QAction *act = menu.addAction(label);
            act->setCheckable(true);
            act->setChecked(contains);
            act->setData(f.id);
        }
    }

    menu.addSeparator();
    QAction *newFolderAct = menu.addAction(tr("新建收藏夹..."));

    QPoint popupPos;
    if (anchor && anchor != this)
        popupPos = anchor->mapToGlobal(QPoint(0, anchor->height() + 4));
    else
        popupPos = QCursor::pos();

    QAction *chosen = menu.exec(popupPos);
    if (!chosen)
    {
        return;
    }

    if (chosen == newFolderAct)
    {
        FavoriteFolderDialog dlg(FavoriteFolderDialog::CreateMode, this);
        if (dlg.exec() == QDialog::Accepted)
        {
            QString newId = FavoritesManager::instance()->createFolder(dlg.folderName());
            if (!newId.isEmpty())
            {
                FavoritesManager::instance()->addFavorite(newId, info, type, QStringLiteral("curseforge"),
                                                          m_currentCategory, category);
                NotificationManager::showSuccess(this, tr("已添加到新收藏夹“%1”").arg(dlg.folderName()));
            }
        }
        return;
    }

    QString folderId = chosen->data().toString();
    if (folderId.isEmpty())
    {
        return;
    }

    bool wasFav = containingIds.contains(folderId);
    if (wasFav)
    {
        FavoritesManager::instance()->removeFavorite(folderId, itemId);
        NotificationManager::showSuccess(this, tr("已从收藏夹移除"));
    }
    else
    {
        FavoritesManager::instance()->addFavorite(folderId, info, type, QStringLiteral("curseforge"),
                                                  m_currentCategory, category);
        NotificationManager::showSuccess(this, tr("已添加到收藏夹"));
    }
}

void BedrockContentPage::loadCardIcon(QLabel *iconLabel, const QString &iconUrl)
{
    const QString cacheKey = QString("bedrock_icon:%1").arg(iconUrl);
    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        iconLabel->setPixmap(cached);
        return;
    }

    if (!s_bedrockIconNAM) {
        s_bedrockIconNAM = new QNetworkAccessManager();
        QNetworkDiskCache *diskCache = new QNetworkDiskCache();
        const QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                  + "/bedrock_icons";
        QDir().mkpath(cachePath);
        diskCache->setCacheDirectory(cachePath);
        diskCache->setMaximumCacheSize(50 * 1024 * 1024);
        s_bedrockIconNAM->setCache(diskCache);
    }

    QUrl url(McimHelper::rewriteImageUrl(iconUrl));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply *reply = s_bedrockIconNAM->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, iconLabel, cacheKey]() {
        if (reply->error() == QNetworkReply::NoError) {
            QPixmap pixmap;
            pixmap.loadFromData(reply->readAll());
            if (!pixmap.isNull()) {
                QPixmap scaled = pixmap.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QPixmapCache::insert(cacheKey, scaled);
                iconLabel->setPixmap(scaled);
            }
        }
        reply->deleteLater();
    });
}

bool BedrockContentPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton)
            return QWidget::eventFilter(watched, event);

        QWidget *card = qobject_cast<QWidget *>(watched);
        if (card && m_cardWidgets.contains(card)) {
            int idx = m_cardWidgets.indexOf(card);
            if (idx >= 0 && idx < m_allMods.size()) {
                emit contentDetailRequested(m_allMods[idx], m_currentCategory);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BedrockContentPage::placeCards()
{
    if (m_viewMode == ContentViewSwitch::List && m_cardGridLayout) {
        while (QLayoutItem *item = m_cardGridLayout->takeAt(0))
            delete item;
        int row = 0;
        for (int i = 0; i < m_cardWidgets.size(); ++i) {
            m_cardGridLayout->addWidget(m_cardWidgets[i], row, 0);
            row++;
        }
        if (m_cardWidgets.isEmpty()) {
            QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
            m_cardGridLayout->addItem(spacer, 0, 0);
        }
        return;
    }

    if (m_cardFlowLayout) {
        while (QLayoutItem *item = m_cardFlowLayout->takeAt(0))
            delete item;
        for (QWidget *w : m_cardWidgets)
            m_cardFlowLayout->addWidget(w);
        if (m_cardWidgets.isEmpty()) {
            QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
            m_cardFlowLayout->addItem(spacer);
        }
    }
}

void BedrockContentPage::updatePaginationControls()
{
    const bool hasCards = !m_cardWidgets.isEmpty();
    if (!hasCards) {
        m_paginationBar->hide();
        return;
    }

    const int pageSize = 20;
    const int pageNum = m_currentPage + 1;
    int totalPages = 0;
    bool knownTotal = false;
    if (m_totalHits > 0) {
        totalPages = (m_totalHits + pageSize - 1) / pageSize;
        knownTotal = true;
    }
    if (knownTotal && totalPages <= 1) {
        m_paginationBar->hide();
        return;
    }

    m_pageLabel->setText(knownTotal ? tr("第 %1/%2 页").arg(pageNum).arg(totalPages)
                                    : tr("第 %1 页").arg(pageNum));
    m_paginationBar->show();
    m_firstPageBtn->setEnabled(m_currentPage > 0);
    m_prevPageBtn->setEnabled(m_currentPage > 0);
    m_nextPageBtn->setEnabled(m_hasMore);
    m_lastPageBtn->setEnabled(knownTotal && m_currentPage < totalPages - 1);
}

void BedrockContentPage::showLoading(bool show)
{
    if (show) {
        const QString status = m_currentQuery.isEmpty()
            ? tr("正在加载基岩版社区资源...")
            : tr("正在搜索: %1").arg(m_currentQuery);
        m_loadingOverlay->showOverlay(status);
    } else {
        m_loadingOverlay->hideOverlay();
    }
}
