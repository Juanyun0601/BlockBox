#include "ContentDownloadPage.h"
#include "../layouts/FlowLayout.h"
#include "../layouts/MasonryLayout.h"
#include "../utils/McimHelper.h"
#include "components/BlurLoadingOverlay.h"
#include "components/ContentViewSwitch.h"
#include "components/MasonryContentCard.h"
#include "../layouts/MasonryLayout.h"
#include "components/FavoriteFolderDialog.h"
#include "components/NotificationManager.h"
#include "utils/SettingsManager.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/FavoritesManager.h"
#include "utils/mod/MCModAPI.h"

#include <QApplication>
#include <QCursor>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPixmapCache>
#include <QSet>
#include <QSpacerItem>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QUrl>

ContentDownloadPage::ContentDownloadPage(ContentType contentType, QWidget *parent)
    : QWidget(parent)
    , m_searchEdit(nullptr)
    , m_sourceCombo(nullptr)
    , m_instanceCombo(nullptr)
    , m_sortCombo(nullptr)
    , m_versionFilterCombo(nullptr)
    , m_loaderFilterCombo(nullptr)
    , m_versionFilterLabel(nullptr)
    , m_loaderFilterLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_cardFlowLayout(nullptr)
    , m_viewSwitch(nullptr)
    , m_viewMode(ContentViewSwitch::Masonry)
    , m_loadingOverlay(nullptr)
    , m_paginationBar(nullptr)
    , m_firstPageBtn(nullptr)
    , m_prevPageBtn(nullptr)
    , m_pageLabel(nullptr)
    , m_nextPageBtn(nullptr)
    , m_lastPageBtn(nullptr)
    , m_curseforgeAPI(nullptr)
    , m_modrinthAPI(nullptr)
    , m_mcmodAPI(nullptr)
    , m_currentSource(AllSources)
    , m_currentPage(0)
    , m_hasMore(false)
    , m_isLoading(false)
    , m_pendingCount(0)
    , m_totalHits(0)
    , m_contentType(contentType)
    , m_config(ContentTypeConfig::getConfig(contentType))
{
    initUI();
    initAPI();
    setupConnections();
    loadInstances();

    QTimer::singleShot(0, this, &ContentDownloadPage::loadInitialMods);
}

ContentDownloadPage::~ContentDownloadPage()
{
}

void ContentDownloadPage::initUI()
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

    QHBoxLayout *searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 0, 0, 0);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr(m_config.searchPlaceholder.toUtf8().constData()));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(36);
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    m_searchEdit->addAction(IconHelper::loadColoredIcon(":/Images/Icons/search.svg", themeColor, 18), QLineEdit::LeadingPosition);
    searchLayout->addWidget(m_searchEdit);

    QPushButton *searchBtn = new QPushButton(tr("搜索"));
    searchBtn->setObjectName("modSearchBtn");
    searchBtn->setFixedHeight(36);
    searchBtn->setFixedWidth(80);
    searchLayout->addWidget(searchBtn);
    connect(searchBtn, &QPushButton::clicked, this, &ContentDownloadPage::onSearchTriggered);

    cardLayout->addLayout(searchLayout);

    FlowLayout *filterLayout = new FlowLayout(nullptr, 0, 10, 8);

    auto createCombo = [this]() -> QComboBox* {
        QComboBox *combo = new QComboBox();
        combo->setFixedHeight(32);
        combo->setMinimumWidth(120);
        return combo;
    };

    QLabel *srcLabel = new QLabel(tr("下载源:"));
    srcLabel->setObjectName("filterLabel");
    m_sourceCombo = createCombo();
    m_sourceCombo->setObjectName("sourceCombo");
    m_sourceCombo->addItem(tr("全部来源"), AllSources);
    m_sourceCombo->addItem(tr("CurseForge"), CurseForge);
    m_sourceCombo->addItem(tr("Modrinth"), Modrinth);
    filterLayout->addWidget(srcLabel);
    filterLayout->addWidget(m_sourceCombo);

    QLabel *instLabel = new QLabel(tr("下载实例:"));
    instLabel->setObjectName("filterLabel");
    m_instanceCombo = createCombo();
    m_instanceCombo->setObjectName("instanceCombo");
    filterLayout->addWidget(instLabel);
    filterLayout->addWidget(m_instanceCombo);

    m_versionFilterLabel = new QLabel(tr("版本筛选:"));
    m_versionFilterLabel->setObjectName("filterLabel");
    m_versionFilterCombo = createCombo();
    m_versionFilterCombo->setObjectName("filterCombo");
    m_versionFilterCombo->addItem(tr("全部版本"), QString());
    m_versionFilterLabel->hide();
    m_versionFilterCombo->hide();
    filterLayout->addWidget(m_versionFilterLabel);
    filterLayout->addWidget(m_versionFilterCombo);

    m_loaderFilterLabel = new QLabel(tr("加载器筛选:"));
    m_loaderFilterLabel->setObjectName("filterLabel");
    m_loaderFilterCombo = createCombo();
    m_loaderFilterCombo->setObjectName("filterCombo");
    m_loaderFilterCombo->addItem(tr("全部加载器"), QString());
    m_loaderFilterLabel->hide();
    m_loaderFilterCombo->hide();
    filterLayout->addWidget(m_loaderFilterLabel);
    filterLayout->addWidget(m_loaderFilterCombo);

    QLabel *sortLabel = new QLabel(tr("排序方式:"));
    sortLabel->setObjectName("filterLabel");
    m_sortCombo = createCombo();
    m_sortCombo->setObjectName("sortCombo");
    populateSortCombo(AllSources);
    filterLayout->addWidget(sortLabel);
    filterLayout->addWidget(m_sortCombo);

    // 视图切换（列表式 / 瀑布流）
    m_viewSwitch = new ContentViewSwitch(this);
    m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted("content_download", ContentViewSwitch::Masonry));
    m_viewMode = static_cast<int>(m_viewSwitch->viewMode());
    filterLayout->addWidget(m_viewSwitch);

    cardLayout->addLayout(filterLayout);
    mainLayout->addWidget(filterCard);

    m_sortCombo->setEnabled(true);

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
    connect(m_firstPageBtn, &QPushButton::clicked, this, &ContentDownloadPage::onFirstPage);
    connect(m_prevPageBtn, &QPushButton::clicked, this, &ContentDownloadPage::onPrevPage);
    connect(m_nextPageBtn, &QPushButton::clicked, this, &ContentDownloadPage::onNextPage);
    connect(m_lastPageBtn, &QPushButton::clicked, this, &ContentDownloadPage::onLastPage);
}

void ContentDownloadPage::initAPI()
{
    m_curseforgeAPI = new CurseForgeAPI(this);
    QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (!apiKey.isEmpty())
        m_curseforgeAPI->setApiKey(apiKey);

    m_modrinthAPI = new ModrinthAPI(this);
    m_mcmodAPI = new MCModAPI(this);

    applyMCIMSetting();
}

void ContentDownloadPage::applyMCIMSetting()
{
    QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    m_curseforgeAPI->setBaseUrl(useMcim ? QString(MCIM_BASE) + "/curseforge" : CF_BASE);
    m_modrinthAPI->setBaseUrl(useMcim ? QString(MCIM_BASE) + "/modrinth" : MODRINTH_BASE);
}

void ContentDownloadPage::setupConnections()
{
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &ContentDownloadPage::onSearchTriggered);
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContentDownloadPage::onSourceChanged);
    connect(m_instanceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContentDownloadPage::onInstanceChanged);
    connect(m_versionFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContentDownloadPage::onVersionFilterChanged);
    connect(m_loaderFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContentDownloadPage::onLoaderFilterChanged);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContentDownloadPage::onSortChanged);

    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged,
            this, &ContentDownloadPage::onViewModeChanged);

    connect(m_curseforgeAPI, &CurseForgeAPI::searchCompleted, this, &ContentDownloadPage::onSearchCompleted);
    connect(m_curseforgeAPI, &CurseForgeAPI::searchFailed, this, &ContentDownloadPage::onSearchFailed);
    connect(m_curseforgeAPI, &CurseForgeAPI::categoriesLoaded, this, &ContentDownloadPage::onCategoriesLoaded);
    connect(m_curseforgeAPI, &CurseForgeAPI::categoriesFailed, this, [this](const QString &error) {
        onSearchFailed(error);
    });

    connect(m_modrinthAPI, &ModrinthAPI::searchCompleted, this, &ContentDownloadPage::onSearchCompleted);
    connect(m_modrinthAPI, &ModrinthAPI::searchFailed, this, &ContentDownloadPage::onSearchFailed);
    connect(m_modrinthAPI, &ModrinthAPI::categoriesLoaded, this, &ContentDownloadPage::onCategoriesLoaded);
    connect(m_modrinthAPI, &ModrinthAPI::categoriesFailed, this, [this](const QString &error) {
        onSearchFailed(error);
    });

    connect(m_mcmodAPI, &MCModAPI::nameResolved, this, &ContentDownloadPage::onChineseNameResolved);
}

void ContentDownloadPage::setCurrentInstancePath(const QString &path)
{
    m_currentInstancePath = path;
}

void ContentDownloadPage::loadInstances()
{
    m_instanceCombo->blockSignals(true);
    m_instanceCombo->clear();
    m_instanceDataMap.clear();

    m_instanceCombo->addItem(tr("任意"), InstanceAny);
    m_instanceCombo->addItem(tr("自定义"), InstanceCustom);

    QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    for (const InstanceFolderInfo &folder : folders) {
        QDir versionsDir(folder.path + "/versions");
        if (!versionsDir.exists())
            continue;

        QDirIterator it(versionsDir.path(), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            QString instancePath = it.next();
            QFileInfo instanceInfo(instancePath);
            QString instanceName = instanceInfo.fileName();

            if (instanceName.startsWith("."))
                continue;

            QFile versionJsonFile(instancePath + "/" + instanceName + ".json");
            if (!versionJsonFile.exists())
                continue;

            ContentInstanceFilterData data;
            data.instanceName = instanceName;
            data.gameVersion = instanceName;
            data.loaderType = QString();

            if (versionJsonFile.open(QIODevice::ReadOnly)) {
                QByteArray jsonData = versionJsonFile.readAll();
                QJsonParseError parseError;
                QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parseError);
                if (parseError.error == QJsonParseError::NoError) {
                    QJsonObject rootObj = jsonDoc.object();

                    if (rootObj.contains("inheritsFrom"))
                        data.gameVersion = rootObj["inheritsFrom"].toString();
                    else if (rootObj.contains("clientVersion"))
                        data.gameVersion = rootObj["clientVersion"].toString();
                    else if (rootObj.contains("id"))
                        data.gameVersion = rootObj["id"].toString();

                    // Detect loader from JSON keys, values, and instance name
                    auto detectLoader = [](const QString &text) -> QString {
                        QString lower = text.toLower();
                        if (lower.contains("fabric")) return "Fabric";
                        if (lower.contains("forge")) return "Forge";
                        if (lower.contains("quilt")) return "Quilt";
                        if (lower.contains("neoforge")) return "NeoForge";
                        return QString();
                    };
                    // Scan all top-level keys
                    for (auto it = rootObj.constBegin(); it != rootObj.constEnd(); ++it) {
                        data.loaderType = detectLoader(it.key());
                        if (!data.loaderType.isEmpty()) break;
                        QJsonValue val = it.value();
                        if (val.isString()) {
                            data.loaderType = detectLoader(val.toString());
                            if (!data.loaderType.isEmpty()) break;
                        }
                    }
                    // Check common value fields
                    if (data.loaderType.isEmpty()) {
                        data.loaderType = detectLoader(rootObj["inheritsFrom"].toString());
                    }
                    if (data.loaderType.isEmpty()) {
                        data.loaderType = detectLoader(rootObj["id"].toString());
                    }
                }
                versionJsonFile.close();
            }

            // Fallback: detect loader from instance folder name or game version
            if (data.loaderType.isEmpty()) {
                QString check = instanceName + " " + data.gameVersion;
                QString lower = check.toLower();
                if (lower.contains("neoforge")) data.loaderType = "NeoForge";
                else if (lower.contains("forge")) data.loaderType = "Forge";
                else if (lower.contains("fabric")) data.loaderType = "Fabric";
                else if (lower.contains("quilt")) data.loaderType = "Quilt";
            }

            m_instanceDataMap.insert(instancePath, data);
            m_instanceCombo->addItem(instanceName, instancePath);
        }
    }

    int selectIndex = 0;
    if (!m_currentInstancePath.isEmpty()) {
        for (int i = 0; i < m_instanceCombo->count(); ++i) {
            if (m_instanceCombo->itemData(i).toString() == m_currentInstancePath) {
                selectIndex = i;
                break;
            }
        }
    }
    m_instanceCombo->setCurrentIndex(selectIndex);
    m_instanceCombo->blockSignals(false);

    setFilterCombosVisible(false);
    populateVersionLoaderFilter();
}

void ContentDownloadPage::populateVersionLoaderFilter()
{
    m_versionFilterCombo->blockSignals(true);
    m_loaderFilterCombo->blockSignals(true);

    QString currentVersion = m_versionFilterCombo->currentData().toString();
    QString currentLoader = m_loaderFilterCombo->currentData().toString();

    m_versionFilterCombo->clear();
    m_loaderFilterCombo->clear();
    m_versionFilterCombo->addItem(tr("全部版本"), QString());
    m_loaderFilterCombo->addItem(tr("全部加载器"), QString());

    QSet<QString> versions;
    QSet<QString> loaders;
    for (auto it = m_instanceDataMap.constBegin(); it != m_instanceDataMap.constEnd(); ++it) {
        if (!it.value().gameVersion.isEmpty())
            versions.insert(it.value().gameVersion);
        if (!it.value().loaderType.isEmpty())
            loaders.insert(it.value().loaderType);
    }

    for (const QString &v : versions)
        m_versionFilterCombo->addItem(v, v);
    for (const QString &l : loaders)
        m_loaderFilterCombo->addItem(l, l);

    int vIdx = m_versionFilterCombo->findData(currentVersion);
    if (vIdx >= 0)
        m_versionFilterCombo->setCurrentIndex(vIdx);
    int lIdx = m_loaderFilterCombo->findData(currentLoader);
    if (lIdx >= 0)
        m_loaderFilterCombo->setCurrentIndex(lIdx);

    m_versionFilterCombo->blockSignals(false);
    m_loaderFilterCombo->blockSignals(false);
}

void ContentDownloadPage::setFilterCombosVisible(bool visible)
{
    m_versionFilterLabel->setVisible(visible);
    m_versionFilterCombo->setVisible(visible);
    m_loaderFilterLabel->setVisible(visible);
    m_loaderFilterCombo->setVisible(visible);
}

QString ContentDownloadPage::selectedGameVersion() const
{
    int idx = m_instanceCombo->currentIndex();
    QVariant data = m_instanceCombo->itemData(idx);
    int typeVal = data.toInt();

    if (typeVal == InstanceCustom) {
        return m_versionFilterCombo->currentData().toString();
    } else if (typeVal >= 0) {
        QString instancePath = data.toString();
        if (m_instanceDataMap.contains(instancePath))
            return m_instanceDataMap[instancePath].gameVersion;
    }
    return QString();
}

QString ContentDownloadPage::selectedLoader() const
{
    int idx = m_instanceCombo->currentIndex();
    QVariant data = m_instanceCombo->itemData(idx);
    int typeVal = data.toInt();

    if (typeVal == InstanceCustom) {
        return m_loaderFilterCombo->currentData().toString();
    } else if (typeVal >= 0) {
        QString instancePath = data.toString();
        if (m_instanceDataMap.contains(instancePath))
            return m_instanceDataMap[instancePath].loaderType;
    }
    return QString();
}

void ContentDownloadPage::refreshCurrentSource()
{
    m_currentSource = m_sourceCombo->currentData().toInt();

    if (m_categoriesCache.contains(m_currentSource)) {
        onCategoriesLoaded(m_categoriesCache[m_currentSource]);
        return;
    }

    if (m_currentSource == CurseForge) {
        m_curseforgeAPI->getCategories();
    } else {
        m_modrinthAPI->getCategories();
    }
}

void ContentDownloadPage::onSearchTriggered()
{
    m_currentQuery = m_searchEdit->text().trimmed();
    m_currentPage = 0;
    m_allMods.clear();
    performSearch(false);
}

void ContentDownloadPage::setSearchText(const QString &text)
{
    if (text.isEmpty())
        return;
    m_searchEdit->setText(text);
    onSearchTriggered();
}

void ContentDownloadPage::onSourceChanged(int index)
{
    Q_UNUSED(index);
    m_currentSource = m_sourceCombo->currentData().toInt();

    m_pendingCount = 0;
    m_isLoading = false;

    populateSortCombo(m_currentSource);

    if (m_currentSource != AllSources) {
        refreshCurrentSource();
    }

    if (!m_currentQuery.isEmpty()) {
        m_currentPage = 0;
        m_allMods.clear();
        performSearch(false);
    }
}

void ContentDownloadPage::onInstanceChanged(int index)
{
    QVariant data = m_instanceCombo->itemData(index);
    int typeVal = data.toInt();

    if (typeVal == InstanceCustom) {
        setFilterCombosVisible(true);
    } else {
        setFilterCombosVisible(false);
    }

    if (typeVal >= 0)
        emit instanceSelectionChanged(data.toString());

    m_currentPage = 0;
    m_allMods.clear();
    performSearch(false);
}

void ContentDownloadPage::onVersionFilterChanged(int)
{
    int idx = m_instanceCombo->currentIndex();
    if (m_instanceCombo->itemData(idx).toInt() == InstanceCustom) {
        m_currentPage = 0;
        m_allMods.clear();
        performSearch(false);
    }
}

void ContentDownloadPage::onLoaderFilterChanged(int)
{
    int idx = m_instanceCombo->currentIndex();
    if (m_instanceCombo->itemData(idx).toInt() == InstanceCustom) {
        m_currentPage = 0;
        m_allMods.clear();
        performSearch(false);
    }
}

void ContentDownloadPage::onSortChanged(int index)
{
    Q_UNUSED(index);
    if (!m_currentQuery.isEmpty()) {
        m_currentPage = 0;
        m_allMods.clear();
        performSearch(false);
    }
}

void ContentDownloadPage::onFirstPage()
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

void ContentDownloadPage::onPrevPage()
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

void ContentDownloadPage::onNextPage()
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

void ContentDownloadPage::onLastPage()
{
    if (!m_isLoading && m_hasMore && m_totalHits > 0 && m_currentSource != AllSources) {
        int pageSize = 20;
        int lastPage = qMax(0, (m_totalHits + pageSize - 1) / pageSize - 1);
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

void ContentDownloadPage::loadInitialMods()
{
    applyMCIMSetting();
    m_currentQuery.clear();
    m_currentPage = 0;
    m_allMods.clear();
    clearCards();
    m_isLoading = false;

    if (m_currentSource == AllSources) {
        performSearch(false);
    } else {
        refreshCurrentSource();
    }
}

void ContentDownloadPage::performSearch(bool append)
{
    if (m_isLoading) return;
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

    m_currentSource = m_sourceCombo->currentData().toInt();
    QString gameVersion = selectedGameVersion();

    if (m_currentSource == AllSources) {
        QString universalSort = m_sortCombo->currentData().toString();

        QMap<QString, QString> cfSortMap;
        cfSortMap["relevance"]      = "popularity";
        cfSortMap["popularity"]     = "popularity";
        cfSortMap["name"]           = "name";
        cfSortMap["last_updated"]   = "last_updated";
        cfSortMap["total_downloads"] = "total_downloads";
        cfSortMap["date_created"]   = "date_created";
        cfSortMap["author"]         = "author";

        QMap<QString, QString> modrinthSortMap;
        modrinthSortMap["relevance"]       = "relevance";
        modrinthSortMap["popularity"]      = "popularity";
        modrinthSortMap["name"]            = "name";
        modrinthSortMap["last_updated"]    = "last_updated";
        modrinthSortMap["total_downloads"] = "popularity";
        modrinthSortMap["date_created"]    = "date_created";
        modrinthSortMap["author"]          = "name";

        m_pendingCount = 2;
        m_curseforgeAPI->setClassId(m_config.cfClassId);
        m_curseforgeAPI->searchMods(m_currentQuery, gameVersion, QString(),
                                     m_currentPage, 20,
                                     cfSortMap.value(universalSort, "popularity"));
        m_modrinthAPI->setProjectType(m_config.mrProjectType);
        m_modrinthAPI->searchMods(m_currentQuery, gameVersion, QString(),
                                   m_currentPage, 20,
                                   modrinthSortMap.value(universalSort, "relevance"));
    } else {
        QString sortField = m_sortCombo->currentData().toString();
        if (m_currentSource == CurseForge) {
            m_curseforgeAPI->setClassId(m_config.cfClassId);
            m_curseforgeAPI->searchMods(m_currentQuery, gameVersion, QString(),
                                         m_currentPage, 20, sortField);
        } else {
            m_modrinthAPI->setProjectType(m_config.mrProjectType);
            m_modrinthAPI->searchMods(m_currentQuery, gameVersion, QString(),
                                       m_currentPage, 20, sortField);
        }
    }
}

void ContentDownloadPage::onSearchCompleted(const ModSearchResult &result)
{
    m_pageCache[m_currentPage] = result.mods;
    m_allMods.append(result.mods);
    for (const ModInfo &info : result.mods)
        addCard(info);

    if (m_currentSource == AllSources) {
        m_pendingCount--;
        m_hasMore = m_hasMore || result.hasMore;
        m_totalHits += result.totalHits;
        if (m_pendingCount > 0)
            return;
    } else {
        m_hasMore = result.hasMore;
        m_totalHits = result.totalHits;
    }

    m_isLoading = false;
    showLoading(false);
    placeCards();

    updatePaginationControls();
    m_loadingOverlay->clearError();
}

void ContentDownloadPage::onSearchFailed(const QString &error)
{
    if (m_currentSource == AllSources) {
        m_pendingCount--;
        if (m_pendingCount > 0)
            return;
        if (!m_allMods.isEmpty()) {
            m_isLoading = false;
            showLoading(false);
            placeCards();
            updatePaginationControls();
            return;
        }
    }

    m_isLoading = false;
    showLoading(false);
    m_loadingOverlay->showError(error);
    updatePaginationControls();
}

void ContentDownloadPage::onCategoriesLoaded(const QList<ModCategory> &categories)
{
    m_categories = categories;
    m_categoriesCache[m_currentSource] = categories;

    if (m_currentSource != AllSources && m_currentQuery.isEmpty() && m_allMods.isEmpty()) {
        m_currentPage = 0;
        performSearch(false);
    }
}

void ContentDownloadPage::onChineseNameResolved(const QString &modId, const QString &chineseName, const QString &mcmodUrl)
{
    if (!mcmodUrl.isEmpty())
        m_mcmodUrls.insert(modId, mcmodUrl);

    QWidget *card = m_chineseNameLookups.value(modId);
    if (!card) return;

    QLabel *nameLabel = card->findChild<QLabel*>("modCardName");
    if (nameLabel) {
        QString currentText = nameLabel->text();
        nameLabel->setText(chineseName + QStringLiteral("  (") + currentText + QStringLiteral(")"));
    }

    emit modMcmodUrlResolved(modId, mcmodUrl);
}

void ContentDownloadPage::clearCards()
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
    m_chineseNameLookups.clear();
    m_mcmodUrls.clear();
}

static QNetworkAccessManager *s_modIconNAM = nullptr;

void ContentDownloadPage::addCard(const ModInfo &info)
{
    m_cardInfos.append(info);
    if (m_viewMode == ContentViewSwitch::List)
        addListCard(info);
    else
        addMasonryCard(info);
}

void ContentDownloadPage::addListCard(const ModInfo &info)
{
    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, &ContentDownloadPage::onCardContextMenu);

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
        QLabel *chip = new QLabel(text);
        chip->setObjectName("modChip");
        chip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(chip);
    };
    if (!info.gameVersions.isEmpty()) addChip(info.gameVersions.first());
    if (!info.loaders.isEmpty()) addChip(info.loaders.first());
    if (!info.categories.isEmpty()) addChip(info.categories.first());
    if (info.dateModified.isValid()) addChip(info.dateModified.toString("yyyy-MM-dd"));

    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);
    mainLayout->addLayout(infoLayout, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton* {
        QPushButton *btn = new QPushButton();
        btn->setObjectName("modCardActionBtn");
        btn->setProperty("cardRole", "actionBtn");
        btn->setFixedSize(32, 32);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
        btn->setIconSize(QSize(18, 18));
        return btn;
    };



    QPushButton *favBtn = createBtn(":/Images/Icons/star.svg", tr("收藏"));
    favBtn->setProperty("cardRole", "favBtn");
    connect(favBtn, &QPushButton::clicked, this, [this, info, favBtn]() {
        if (info.id.isEmpty() || info.source.isEmpty())
        {
            NotificationManager::showInfo(this, tr("当前资源信息不完整，无法收藏"));
            return;
        }

        QString itemId = info.source + ":" + info.id;
        QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
        QList<QString> containingIds = FavoritesManager::instance()->foldersContaining(itemId);

        QMenu menu(this);
        menu.setWindowTitle(tr("收藏到分组"));

        QList<QAction *> folderActions;
        for (const FavoriteFolder &f : folders)
        {
            bool contains = containingIds.contains(f.id);
            QString label = f.name + (contains ? QStringLiteral("  ✓") : QString());
            QAction *act = menu.addAction(label);
            act->setCheckable(true);
            act->setChecked(contains);
            act->setData(f.id);
            folderActions.append(act);
        }

        menu.addSeparator();
        QAction *newFolderAct = menu.addAction(tr("新建收藏夹..."));

        QAction *chosen = menu.exec(favBtn->mapToGlobal(QPoint(0, favBtn->height() + 4)));
        if (!chosen)
        {
            return;
        }

        if (chosen == newFolderAct)
        {
            // 新建收藏夹并直接收藏当前资源
            FavoriteFolderDialog dlg(FavoriteFolderDialog::CreateMode, this);
            if (dlg.exec() == QDialog::Accepted)
            {
                QString newId = FavoritesManager::instance()->createFolder(dlg.folderName());
                if (!newId.isEmpty())
                {
                    FavoritesManager::instance()->addFavorite(newId, info, m_contentType, info.source);
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
            FavoritesManager::instance()->addFavorite(folderId, info, m_contentType, info.source);
            NotificationManager::showSuccess(this, tr("已添加到收藏夹"));
        }
    });
    btnLayout->addWidget(favBtn);

    QPushButton *saveBtn = createBtn(":/Images/Icons/download.svg", tr("一键下载最新版本及前置"));
    connect(saveBtn, &QPushButton::clicked, this, [this, info]() {
        emit cardDownloadRequested(info);
    });
    btnLayout->addWidget(saveBtn);

    mainLayout->addLayout(btnLayout);

    card->installEventFilter(this);
    m_cardWidgets.append(card);

    if (!info.iconUrl.isEmpty())
        loadCardIcon(iconLabel, info.iconUrl);

    QString lookupKey = info.source + ":" + info.id;
    m_chineseNameLookups.insert(lookupKey, card);
    m_mcmodAPI->lookupChineseName(lookupKey, info.name);
}

/* 瀑布流卡片操作配置（小卡片展开态与瀑布流共用） */
QList<MasonryContentCard::ActionSpec> ContentDownloadPage::buildMasonryActions(const ModInfo &info)
{
    QList<MasonryContentCard::ActionSpec> actions;
    Q_UNUSED(info)

    actions.append({ QStringLiteral(":/Images/Icons/star.svg"), tr("收藏"), QColor(), [this, info]() {
        if (info.id.isEmpty() || info.source.isEmpty())
        {
            NotificationManager::showInfo(this, tr("当前资源信息不完整，无法收藏"));
            return;
        }
        QString itemId = info.source + ":" + info.id;
        QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
        QList<QString> containingIds = FavoritesManager::instance()->foldersContaining(itemId);
        QMenu menu(this);
        menu.setWindowTitle(tr("收藏到分组"));
        for (const FavoriteFolder &f : folders)
        {
            bool contains = containingIds.contains(f.id);
            QString label = f.name + (contains ? QStringLiteral("  ✓") : QString());
            QAction *act = menu.addAction(label);
            act->setCheckable(true);
            act->setChecked(contains);
            act->setData(f.id);
        }
        menu.addSeparator();
        QAction *newFolderAct = menu.addAction(tr("新建收藏夹..."));
        QAction *chosen = menu.exec(QCursor::pos());
        if (!chosen)
            return;
        if (chosen == newFolderAct)
        {
            FavoriteFolderDialog dlg(FavoriteFolderDialog::CreateMode, this);
            if (dlg.exec() == QDialog::Accepted)
            {
                QString newId = FavoritesManager::instance()->createFolder(dlg.folderName());
                if (!newId.isEmpty())
                {
                    FavoritesManager::instance()->addFavorite(newId, info, m_contentType, info.source);
                    NotificationManager::showSuccess(this, tr("已添加到新收藏夹“%1”").arg(dlg.folderName()));
                }
            }
            return;
        }
        QString folderId = chosen->data().toString();
        if (folderId.isEmpty())
            return;
        if (containingIds.contains(folderId))
        {
            FavoritesManager::instance()->removeFavorite(folderId, itemId);
            NotificationManager::showSuccess(this, tr("已从收藏夹移除"));
        }
        else
        {
            FavoritesManager::instance()->addFavorite(folderId, info, m_contentType, info.source);
            NotificationManager::showSuccess(this, tr("已添加到收藏夹"));
        }
    }});

    actions.append({ QStringLiteral(":/Images/Icons/download.svg"), tr("一键下载最新版本及前置"), QColor(),
                     [this, info]() { emit cardDownloadRequested(info); } });

    return actions;
}

/* 瀑布流卡片（复用 MasonryContentCard 组件） */
void ContentDownloadPage::addMasonryCard(const ModInfo &info)
{
    QWidget *card = MasonryContentCard::build(info, m_cardContainer, buildMasonryActions(info));
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, &ContentDownloadPage::onCardContextMenu);

    card->installEventFilter(this);
    m_cardWidgets.append(card);

    QString lookupKey = info.source + ":" + info.id;
    m_chineseNameLookups.insert(lookupKey, card);
    m_mcmodAPI->lookupChineseName(lookupKey, info.name);
}

/* 视图切换：重建卡片（保留当前页数据） */
void ContentDownloadPage::onViewModeChanged(ContentViewSwitch::ViewMode mode)
{
    m_viewMode = static_cast<int>(mode);
    ContentViewSwitch::savePersisted("content_download", mode);
    rebuildCards();
}

void ContentDownloadPage::rebuildCards()
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



void ContentDownloadPage::loadCardIcon(QLabel *iconLabel, const QString &iconUrl)
{
    QString cacheKey = QString("mod_icon:%1").arg(iconUrl);
    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        iconLabel->setPixmap(cached);
        return;
    }

    if (!s_modIconNAM) {
        s_modIconNAM = new QNetworkAccessManager();
        QNetworkDiskCache *diskCache = new QNetworkDiskCache();
        QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/mod_icons";
        QDir().mkpath(cachePath);
        diskCache->setCacheDirectory(cachePath);
        diskCache->setMaximumCacheSize(50 * 1024 * 1024);
        s_modIconNAM->setCache(diskCache);
    }

    QUrl url(McimHelper::rewriteImageUrl(iconUrl));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply *reply = s_modIconNAM->get(request);
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

bool ContentDownloadPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton)
            return QWidget::eventFilter(watched, event);

        QWidget *card = qobject_cast<QWidget*>(watched);
        if (card && m_cardWidgets.contains(card)) {
            for (QWidget *w : m_cardWidgets) {
                bool selected = (w == card);
                w->setProperty("selected", selected);
                w->style()->polish(w);
            }
            int idx = m_cardWidgets.indexOf(card);
            if (idx >= 0 && idx < m_allMods.size()) {
                emit contentClicked(m_allMods[idx]);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ContentDownloadPage::onCardContextMenu(const QPoint &pos)
{
    QWidget *card = qobject_cast<QWidget *>(sender());
    if (!card || !m_cardWidgets.contains(card))
    {
        return;
    }

    int idx = m_cardWidgets.indexOf(card);
    if (idx < 0 || idx >= m_allMods.size())
    {
        return;
    }

    const ModInfo &info = m_allMods[idx];
    QString contentTypeName = m_config.displayName;

    QMenu menu(this);

    QAction *detailAction = menu.addAction(tr("查看详情"));
    menu.addSeparator();
    QAction *copyNameAction = menu.addAction(tr("复制%1名称").arg(contentTypeName));
    QAction *copyLinkAction = nullptr;
    if (!info.pageUrl.isEmpty())
    {
        copyLinkAction = menu.addAction(tr("复制%1链接").arg(contentTypeName));
    }
    menu.addSeparator();
    QAction *openPageAction = nullptr;
    if (!info.pageUrl.isEmpty())
    {
        openPageAction = menu.addAction(tr("在浏览器中打开"));
    }

    QAction *chosen = menu.exec(card->mapToGlobal(pos));

    if (chosen == detailAction)
    {
        emit contentClicked(info);
    }
    else if (chosen == copyNameAction)
    {
        QString displayName = info.chineseName.isEmpty() ? info.name : info.chineseName;
        QApplication::clipboard()->setText(displayName);
    }
    else if (chosen == copyLinkAction)
    {
        QApplication::clipboard()->setText(info.pageUrl);
    }
    else if (chosen == openPageAction)
    {
        QDesktopServices::openUrl(QUrl(info.pageUrl));
    }
}

void ContentDownloadPage::placeCards()
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

void ContentDownloadPage::updatePaginationControls()
{
    bool hasCards = !m_cardWidgets.isEmpty();
    if (!hasCards) {
        m_paginationBar->hide();
        return;
    }

    int pageSize = 20;
    int pageNum = m_currentPage + 1;
    int totalPages = 0;
    bool knownTotal = false;

    if (m_currentSource != AllSources && m_totalHits > 0) {
        totalPages = (m_totalHits + pageSize - 1) / pageSize;
        knownTotal = true;
    }

    if (knownTotal && totalPages <= 1) {
        m_paginationBar->hide();
        return;
    }

    QString pageText = knownTotal
        ? tr("第 %1/%2 页").arg(pageNum).arg(totalPages)
        : tr("第 %1 页").arg(pageNum);
    m_pageLabel->setText(pageText);

    m_paginationBar->show();
    m_firstPageBtn->setEnabled(m_currentPage > 0);
    m_prevPageBtn->setEnabled(m_currentPage > 0);
    m_nextPageBtn->setEnabled(m_hasMore);
    m_lastPageBtn->setEnabled(knownTotal && m_currentPage < totalPages - 1);
}

int ContentDownloadPage::columnCount() const
{
    return 1;
}

void ContentDownloadPage::showLoading(bool show)
{
    if (show) {
        QString status = m_currentQuery.isEmpty()
            ? tr("正在加载%1列表...").arg(m_config.displayName)
            : tr("正在搜索: %1").arg(m_currentQuery);
        m_loadingOverlay->showOverlay(status);
    } else {
        m_loadingOverlay->hideOverlay();
    }
}

void ContentDownloadPage::populateSortCombo(int source)
{
    Q_UNUSED(source);
    m_sortCombo->clear();
    m_sortCombo->addItem(tr("相关度"), "relevance");
    m_sortCombo->addItem(tr("热门/下载量"), "popularity");
    m_sortCombo->addItem(tr("名称"), "name");
    m_sortCombo->addItem(tr("最近更新"), "last_updated");
    m_sortCombo->addItem(tr("总下载量"), "total_downloads");
    m_sortCombo->addItem(tr("创建日期"), "date_created");
    m_sortCombo->addItem(tr("作者"), "author");
}