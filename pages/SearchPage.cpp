#include "SearchPage.h"

#include "components/OutlinedLabel.h"

#include <algorithm>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPainter>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStandardPaths>
#include <QUrl>

#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/FavoritesManager.h"
#include "utils/DownloadTaskManager.h"

static QString smartTruncate(const QString &text, int maxChars = 100)
{
    if (text.length() <= maxChars)
        return text;
    
    // Find a good break point - prefer breaking at sentence/punctuation boundaries
    int breakPoint = maxChars;
    
    // Look for sentence endings or commas within the last 20 characters
    for (int i = maxChars; i > maxChars - 20 && i > 0; --i)
    {
        QChar ch = text.at(i - 1);
        if (ch == '.' || ch == QChar(0x3002) || ch == QChar(0xFF0C) || ch == ',' || ch == ';' || ch == QChar(0xFF1B) || ch == ' ' || ch == '\n')
        {
            breakPoint = i;
            break;
        }
    }
    
    return text.left(breakPoint) + QStringLiteral("...");
}

const char *const SearchPage::kHotSearches[] = {
    "Sodium", "光影", "机械动力", "优化", "JEI", "Fabric API"
};

namespace {
QString formatCount(qint64 n)
{
    if (n < 10000)
        return QString::number(n);
    if (n < 100000000)
        return QString::number(n / 10000.0, 'f', 1) + QStringLiteral(" 万");
    return QString::number(n / 100000000.0, 'f', 1) + QStringLiteral(" 亿");
}
} // namespace

SearchPage::SearchPage(QWidget *parent)
    : QWidget(parent)
    , m_searchInput(nullptr)
    , m_filterCombo(nullptr)
    , m_subFilterCombo(nullptr)
    , m_hotSearchBar(nullptr)
    , m_sortCombo(nullptr)
    , m_resultsScroll(nullptr)
    , m_resultsContainer(nullptr)
    , m_resultsLayout(nullptr)
    , m_resultsTitle(nullptr)
    , m_noResultsLabel(nullptr)
    , m_curseforgeAPI(nullptr)
    , m_modrinthAPI(nullptr)
    , m_networkManager(nullptr)
    , m_onlineSearchPendingCount(0)
    , m_onlineSearchTimer(nullptr)
    , m_currentFilter(0)
    , m_currentSubFilter(-1)
{
    initAPIs();
    initUI();
}

SearchPage::~SearchPage()
{
}

void SearchPage::initAPIs()
{
    m_networkManager = new QNetworkAccessManager(this);

    m_curseforgeAPI = new CurseForgeAPI(this);
    QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (!apiKey.isEmpty())
        m_curseforgeAPI->setApiKey(apiKey);

    m_modrinthAPI = new ModrinthAPI(this);

    applyMCIMSetting();

    connect(m_curseforgeAPI, &CurseForgeAPI::searchCompleted, this, [this](const ModSearchResult &result) {
        if (m_onlineSearchPendingCount <= 0) return;
        m_onlineSearchPendingCount--;

        QList<SearchResultItem> items;
        for (const auto &mod : result.mods)
        {
            SearchResultItem item;
            item.title = mod.chineseName.isEmpty() ? mod.name : mod.chineseName;
            item.subtitle = tr("CurseForge - %1").arg(mod.author);
            item.description = smartTruncate(mod.description, 100);
            item.iconText = "CF";
            item.type = SearchResultItem::TypeOnlineResource;
            item.resourceType = m_pendingOnlineType;
            item.onlineSource = "curseforge";
            item.onlineId = mod.id;
            item.onlineIconUrl = mod.iconUrl;
            item.onlineAuthor = mod.author;
            item.onlineVersion = mod.latestVersion;

            item.sourceLabel = "CurseForge";
            item.mcVersion = mod.gameVersions.value(0);
            item.loader = mod.loaders.value(0);
            item.updatedText = mod.dateModified.isValid()
                ? mod.dateModified.toString("yyyy-MM-dd")
                : QString();
            item.downloadCount = mod.downloadCount;
            item.followers = mod.followers;
            item.rating = -1.0;
            item.tags = mod.categories.mid(0, 4);
            items.append(item);
        }
        if (!items.isEmpty())
            appendOnlineResults(items, tr("CurseForge"));
    });

    connect(m_curseforgeAPI, &CurseForgeAPI::searchFailed, this, [this](const QString &) {
        if (m_onlineSearchPendingCount > 0)
            m_onlineSearchPendingCount--;
    });

    connect(m_modrinthAPI, &ModrinthAPI::searchCompleted, this, [this](const ModSearchResult &result) {
        if (m_onlineSearchPendingCount <= 0) return;
        m_onlineSearchPendingCount--;

        QList<SearchResultItem> items;
        for (const auto &mod : result.mods)
        {
            SearchResultItem item;
            item.title = mod.chineseName.isEmpty() ? mod.name : mod.chineseName;
            item.subtitle = tr("Modrinth - %1").arg(mod.author);
            item.description = smartTruncate(mod.description, 100);
            item.iconText = "MR";
            item.type = SearchResultItem::TypeOnlineResource;
            item.resourceType = m_pendingOnlineType;
            item.onlineSource = "modrinth";
            item.onlineId = mod.id;
            item.onlineIconUrl = mod.iconUrl;
            item.onlineAuthor = mod.author;
            item.onlineVersion = mod.latestVersion;

            item.sourceLabel = "Modrinth";
            item.mcVersion = mod.gameVersions.value(0);
            item.loader = mod.loaders.value(0);
            item.updatedText = mod.dateModified.isValid()
                ? mod.dateModified.toString("yyyy-MM-dd")
                : QString();
            item.downloadCount = mod.downloadCount;
            item.followers = mod.followers;
            item.rating = -1.0;
            item.tags = mod.categories.mid(0, 4);
            items.append(item);
        }
        if (!items.isEmpty())
            appendOnlineResults(items, tr("Modrinth"));
    });

    connect(m_modrinthAPI, &ModrinthAPI::searchFailed, this, [this](const QString &) {
        if (m_onlineSearchPendingCount > 0)
            m_onlineSearchPendingCount--;
    });

    m_onlineSearchTimer = new QTimer(this);
    m_onlineSearchTimer->setSingleShot(true);
    m_onlineSearchTimer->setInterval(300);
    connect(m_onlineSearchTimer, &QTimer::timeout, this, [this]() {
        if (!m_pendingOnlineKeyword.isEmpty() && m_onlineSearchPendingCount > 0)
        {
            ContentTypeConfig config = ContentTypeConfig::getConfig(m_pendingOnlineType);
            m_curseforgeAPI->setClassId(config.cfClassId);
            m_modrinthAPI->setProjectType(config.mrProjectType);
            m_curseforgeAPI->searchMods(m_pendingOnlineKeyword);
            m_modrinthAPI->searchMods(m_pendingOnlineKeyword);
        }
    });
}

void SearchPage::applyMCIMSetting()
{
    QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    m_curseforgeAPI->setBaseUrl(useMcim ? QString(MCIM_BASE) + "/curseforge" : CF_BASE);
    m_modrinthAPI->setBaseUrl(useMcim ? QString(MCIM_BASE) + "/modrinth" : MODRINTH_BASE);
}

void SearchPage::setSearchFocus()
{
    m_searchInput->setFocus();
    m_searchInput->selectAll();
}

void SearchPage::clearSearch()
{
    m_searchInput->clear();
    clearResults();
}

void SearchPage::setFilter(int index)
{
    if (!m_filterCombo || m_filterCombo->currentIndex() == index)
        return;
    m_filterCombo->setCurrentIndex(index);
    onFilterChanged(index);
}

void SearchPage::retranslatePlaceholder()
{
    m_searchInput->setPlaceholderText(tr("搜索实例、模组、设置、AI 助手..."));
}

QWidget* SearchPage::createSearchBar()
{
    auto *container = new QWidget();
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setAlignment(Qt::AlignCenter);

    m_searchInput = new QLineEdit();
    m_searchInput->setObjectName("searchPageInput");
    m_searchInput->setPlaceholderText(tr("搜索实例、模组、设置、AI 助手..."));
    m_searchInput->setFixedWidth(900);
    m_searchInput->setFixedHeight(48);
    layout->addWidget(m_searchInput);

    connect(m_searchInput, &QLineEdit::returnPressed, this, [this]() {
        performSearch(m_searchInput->text().trimmed());
    });

    return container;
}

QWidget* SearchPage::createHotSearchBar()
{
    m_hotSearchBar = new QWidget();
    auto *layout = new QHBoxLayout(m_hotSearchBar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignCenter);

    auto *label = new QLabel(tr("热门搜索"));
    label->setObjectName("searchHotLabel");
    layout->addWidget(label);

    const int hotCount = static_cast<int>(sizeof(kHotSearches) / sizeof(kHotSearches[0]));
    for (int i = 0; i < hotCount; ++i)
    {
        auto *btn = new QPushButton(QStringLiteral("  ") + kHotSearches[i]);
        btn->setObjectName("searchHotBtn");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("hotKeyword", kHotSearches[i]);
        layout->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            m_searchInput->setText(btn->property("hotKeyword").toString());
            performSearch(btn->property("hotKeyword").toString());
        });
    }

    return m_hotSearchBar;
}

QWidget* SearchPage::createResultsToolbar()
{
    auto *container = new QWidget();
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(40, 0, 40, 0);
    layout->setSpacing(8);

    m_sortCombo = new QComboBox();
    m_sortCombo->setObjectName("searchSortCombo");
    m_sortCombo->setFixedHeight(30);
    m_sortCombo->addItem(tr("相关度"));
    m_sortCombo->addItem(tr("下载量"));
    m_sortCombo->addItem(tr("最近更新"));
    m_sortCombo->addItem(tr("评分"));
    layout->addWidget(m_sortCombo);
    layout->addStretch();

    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_searchInput->text().trimmed().isEmpty())
            performSearch(m_searchInput->text().trimmed());
    });

    return container;
}

QWidget* SearchPage::createFilterBar()
{
    auto *container = new QWidget();
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(40, 0, 40, 0);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignCenter);

    m_filterCombo = new QComboBox();
    m_filterCombo->setObjectName("searchFilterCombo");
    m_filterCombo->setFixedHeight(32);
    m_filterCombo->addItem(tr("全部"));
    m_filterCombo->addItem(tr("实例"));
    m_filterCombo->addItem(tr("资源"));
    m_filterCombo->addItem(tr("设置"));
    m_filterCombo->addItem(tr("AI 助手"));
    m_filterCombo->addItem(tr("收藏"));
    m_filterCombo->addItem(tr("任务"));
    layout->addWidget(m_filterCombo);

    m_subFilterCombo = new QComboBox();
    m_subFilterCombo->setObjectName("searchSubFilterCombo");
    m_subFilterCombo->setFixedHeight(32);
    layout->addWidget(m_subFilterCombo);

    layout->addStretch();

    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onFilterChanged);
    connect(m_subFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onSubFilterChanged);

    onFilterChanged(0);

    return container;
}

QWidget* SearchPage::createResultsArea()
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(40, 0, 40, 0);
    layout->setSpacing(8);

    m_resultsTitle = new QLabel();
    m_resultsTitle->setObjectName("searchResultsTitle");
    m_resultsTitle->setVisible(false);
    layout->addWidget(m_resultsTitle);

    m_noResultsLabel = new QLabel(tr("未找到匹配的结果"));
    m_noResultsLabel->setObjectName("searchNoResults");
    m_noResultsLabel->setAlignment(Qt::AlignCenter);
    m_noResultsLabel->setVisible(false);
    layout->addWidget(m_noResultsLabel);

    m_resultsScroll = new QScrollArea();
    m_resultsScroll->setObjectName("searchPageResultsScroll");
    m_resultsScroll->setWidgetResizable(true);
    m_resultsScroll->setFrameShape(QFrame::NoFrame);
    m_resultsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_resultsScroll->setVisible(false);

    m_resultsContainer = new QWidget();
    m_resultsContainer->setObjectName("searchResultsInnerContainer");
    m_resultsLayout = new QVBoxLayout(m_resultsContainer);
    m_resultsLayout->setContentsMargins(0, 0, 0, 0);
    m_resultsLayout->setSpacing(6);
    m_resultsScroll->setWidget(m_resultsContainer);
    layout->addWidget(m_resultsScroll, 1);

    return container;
}

void SearchPage::onFilterChanged(int index)
{
    m_currentFilter = index;
    m_currentSubFilter = -1;

    // Disconnect to avoid signal during repopulation
    disconnect(m_subFilterCombo, nullptr, this, nullptr);

    m_subFilterCombo->blockSignals(true);
    m_subFilterCombo->clear();

    QStringList subFilters;
    switch (index)
    {
    case 0:
        m_subFilterCombo->setVisible(false);
        break;
    case 1:
        subFilters = {tr("全部"), tr("原版"), tr("Fabric"), tr("Forge"), tr("Quilt"), tr("NeoForge")};
        m_subFilterCombo->setVisible(true);
        break;
    case 2:
        subFilters = {tr("安装新实例"), tr("下载整合包"), tr("导入整合包"), tr("模组"), tr("数据包"), tr("资源包"), tr("光影包"), tr("世界")};
        m_subFilterCombo->setVisible(true);
        break;
    case 3:
        subFilters = {tr("通用"), tr("界面"), tr("游戏"), tr("实例"), tr("Java"), tr("高级"), tr("按键绑定"), tr("GitHub加速")};
        m_subFilterCombo->setVisible(true);
        break;
    case 4:
        subFilters = {tr("对话"), tr("模型"), tr("全部")};
        m_subFilterCombo->setVisible(true);
        break;
    case 5:
        subFilters = {tr("全部"), tr("模组"), tr("整合包"), tr("资源包"), tr("光影包"), tr("数据包"), tr("世界")};
        m_subFilterCombo->setVisible(true);
        break;
    case 6:
        subFilters = {tr("全部"), tr("进行中"), tr("已完成"), tr("失败")};
        m_subFilterCombo->setVisible(true);
        break;
    }

    for (const QString &filter : subFilters)
        m_subFilterCombo->addItem(filter);

    m_subFilterCombo->blockSignals(false);

    if (!subFilters.isEmpty())
    {
        m_currentSubFilter = 0;
        connect(m_subFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SearchPage::onSubFilterChanged);
    }

    if (!m_searchInput->text().trimmed().isEmpty())
    {
        performSearch(m_searchInput->text().trimmed());
    }
}

void SearchPage::onSubFilterChanged(int index)
{
    if (m_currentSubFilter == index)
        return;
    m_currentSubFilter = index;
    if (!m_searchInput->text().trimmed().isEmpty())
    {
        performSearch(m_searchInput->text().trimmed());
    }
}

void SearchPage::performSearch(const QString &keyword)
{
    m_onlineSearchPendingCount = 0;
    m_onlineSearchTimer->stop();
    m_currentKeyword = keyword;

    if (keyword.isEmpty())
    {
        clearResults();
        return;
    }

    QList<SearchResultItem> results;
    bool needsOnlineSearch = false;

    switch (m_currentFilter)
    {
    case 0:
        results = searchAll(keyword);
        needsOnlineSearch = true;
        break;
    case 1:
        results = searchInstances(keyword);
        break;
    case 2:
    {
        // 子筛选项与资源页左侧导航栏一致（除收藏外）：
        // 0=安装新实例, 1=下载整合包, 2=导入整合包, 3=模组, 4=数据包, 5=资源包, 6=光影包, 7=世界
        ContentType types[] = {ContentType::Modpack, ContentType::Mod, ContentType::DataPack,
                               ContentType::ResourcePack, ContentType::ShaderPack, ContentType::World};
        int subIdx = m_currentSubFilter;
        if (subIdx == 0)
        {
            // 安装新实例
            SearchResultItem item;
            item.title = tr("安装新实例");
            item.subtitle = tr("安装新的 Minecraft 实例");
            item.description = tr("前往资源页面安装新的游戏实例");
            item.iconText = "+";
            item.type = SearchResultItem::TypeResource;
            item.resourceType = ContentType::Mod;
            results.append(item);
        }
        else if (subIdx == 1)
        {
            // 下载整合包
            results = searchResourceFiles(keyword, ContentType::Modpack);
            m_pendingOnlineType = ContentType::Modpack;
            needsOnlineSearch = true;
        }
        else if (subIdx == 2)
        {
            // 导入整合包
            SearchResultItem item;
            item.title = tr("导入整合包");
            item.subtitle = tr("导入本地整合包");
            item.description = tr("前往资源页面导入本地整合包文件");
            item.iconText = "I";
            item.type = SearchResultItem::TypeResource;
            item.resourceType = ContentType::Modpack;
            results.append(item);
        }
        else if (subIdx >= 3 && subIdx <= 7)
        {
            // 模组/数据包/资源包/光影包/世界
            ContentType type = types[subIdx - 3];
            results = searchResourceFiles(keyword, type);
            m_pendingOnlineType = type;
            needsOnlineSearch = true;
        }
        break;
    }
    case 3:
        results = searchSettings(keyword);
        break;
    case 4:
        results = searchAi(keyword);
        break;
    case 5:
        results = searchFavorites(keyword);
        break;
    case 6:
        results = searchTasks(keyword);
        break;
    }

    // Always append external search links
    results.append(searchExternal(keyword));

    sortResults(results);
    showResults(results);

    // Trigger async online search (searches both APIs)
    if (needsOnlineSearch)
    {
        m_pendingOnlineKeyword = keyword;
        m_onlineSearchPendingCount = 2;
        m_onlineSearchTimer->start();
    }
}

QList<SearchResultItem> SearchPage::searchAll(const QString &keyword)
{
    QList<SearchResultItem> results;
    results.append(searchInstances(keyword));
    results.append(searchSettings(keyword));
    results.append(searchAi(keyword));

    ContentType types[] = {ContentType::Mod, ContentType::DataPack, ContentType::ResourcePack,
                           ContentType::ShaderPack, ContentType::World, ContentType::Modpack};
    for (auto type : types)
    {
        results.append(searchResourceFiles(keyword, type));
    }

    results.append(searchFavorites(keyword));
    results.append(searchTasks(keyword));

    return results;
}

QList<SearchResultItem> SearchPage::searchInstances(const QString &keyword)
{
    QList<SearchResultItem> results;
    QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();

    // 子筛选器加载器名称映射：0=全部, 1=原版, 2=Fabric, 3=Forge, 4=Quilt, 5=NeoForge
    static const QStringList subFilterLoaders = {"", "", "Fabric", "Forge", "Quilt", "NeoForge"};

    for (const auto &folder : folders)
    {
        QDir dir(folder.path);
        if (!dir.exists())
            continue;

        QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries)
        {
            QString name = entry.fileName();
            if (!name.contains(keyword, Qt::CaseInsensitive))
                continue;

            // 加载器子筛选
            QStringList loaders = detectInstanceLoaders(entry.absoluteFilePath());
            if (m_currentFilter == 1 && m_currentSubFilter > 0)
            {
                if (m_currentSubFilter == 1)
                {
                    // 原版：无任何加载器
                    if (!loaders.isEmpty())
                        continue;
                }
                else
                {
                    QString expectedLoader = subFilterLoaders.value(m_currentSubFilter);
                    if (!loaders.contains(expectedLoader))
                        continue;
                }
            }

            SearchResultItem item;
            item.title = name;
            item.subtitle = tr("实例 - %1").arg(folder.name);
            item.description = entry.absoluteFilePath();
            item.iconText = name.left(1).toUpper();
            item.type = SearchResultItem::TypeInstance;
            item.instancePath = entry.absoluteFilePath();
            item.sourceLabel = tr("本地");
            item.mcVersion = loaders.join(" ").isEmpty() ? tr("原版") : loaders.join(" ");
            item.updatedText = entry.lastModified().toString("yyyy-MM-dd");
            results.append(item);
        }
    }

    return results;
}

QList<SearchResultItem> SearchPage::searchSettings(const QString &keyword)
{
    QList<SearchResultItem> results;

    struct SettingTab {
        QString name;
        QStringList keywords;
        int index;
    };

    QList<SettingTab> tabs = {
        {tr("常规设置"), {tr("常规"), tr("语言"), tr("主题"), tr("通用")}, 0},
        {tr("界面设置"), {tr("界面"), tr("背景"), tr("显示"), tr("UI"), tr("外观")}, 1},
        {tr("全局游戏设置"), {tr("游戏"), tr("全局"), tr("分辨率"), tr("内存")}, 2},
        {tr("实例设置"), {tr("实例"), tr("默认")}, 3},
        {tr("Java管理"), {tr("Java"), tr("路径"), tr("环境")}, 4},
        {tr("高级设置"), {tr("高级"), tr("下载"), tr("来源"), tr("线程")}, 5},
        {tr("按键绑定"), {tr("按键"), tr("快捷键"), tr("绑定")}, 6},
        {tr("GitHub加速"), {tr("GitHub"), tr("加速"), tr("镜像"), tr("代理")}, 7},
    };

    // 子筛选器：0=通用, 1=界面, 2=游戏, 3=实例, 4=Java, 5=高级, 6=按键绑定, 7=GitHub加速
    int subIdx = m_currentSubFilter;
    for (const auto &tab : tabs)
    {
        // 子筛选器过滤
        if (m_currentFilter == 3 && subIdx >= 0 && tab.index != subIdx)
            continue;

        bool match = tab.name.contains(keyword, Qt::CaseInsensitive);
        if (!match)
        {
            for (const auto &kw : tab.keywords)
            {
                if (kw.contains(keyword, Qt::CaseInsensitive))
                {
                    match = true;
                    break;
                }
            }
        }
        if (match)
        {
            SearchResultItem item;
            item.title = tab.name;
            item.subtitle = tr("设置");
            item.description = tr("前往 %1 页面").arg(tab.name);
            item.iconText = tab.name.left(1);
            item.type = SearchResultItem::TypeSettings;
            item.settingsTabIndex = tab.index;
            item.sourceLabel = tr("设置");
            results.append(item);
        }
    }

    return results;
}

QList<SearchResultItem> SearchPage::searchResourceFiles(const QString &keyword, ContentType type)
{
    QList<SearchResultItem> results;
    ContentTypeConfig config = ContentTypeConfig::getConfig(type);
    QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();

    for (const auto &folder : folders)
    {
        QDir dir(folder.path);
        if (!dir.exists())
            continue;

        QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &instanceEntry : entries)
        {
            QString instanceName = instanceEntry.fileName();
            QString contentDirPath = instanceEntry.absoluteFilePath() + "/" + config.folderName;
            QDir contentDir(contentDirPath);
            if (!contentDir.exists())
                continue;

            QStringList nameFilters;
            for (const QString &ext : config.extensions)
            {
                nameFilters << ext << (ext + ".disabled");
            }

            QFileInfoList files = contentDir.entryInfoList(nameFilters, QDir::Files);
            for (const QFileInfo &file : files)
            {
                QString fileName = file.fileName();
                QString displayName = fileName;
                if (displayName.endsWith(".disabled"))
                    displayName = displayName.left(displayName.length() - 9);

                if (displayName.contains(keyword, Qt::CaseInsensitive))
                {
                    SearchResultItem item;
                    item.title = displayName;
                    item.subtitle = tr("%1 - %2").arg(config.displayName, instanceName);
                    item.description = file.absoluteFilePath();
                    item.iconText = displayName.left(1).toUpper();
                    item.type = SearchResultItem::TypeContentList;
                    item.resourceType = type;
                    item.sourceLabel = tr("本地");
                    item.mcVersion = instanceName;
                    item.updatedText = file.lastModified().toString("yyyy-MM-dd");
                    results.append(item);
                }
            }

            if (type == ContentType::World)
            {
                QFileInfoList subdirs = contentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QFileInfo &subdir : subdirs)
                {
                    QString dirName = subdir.fileName();
                    QString displayName = dirName;
                    if (displayName.endsWith(".disabled"))
                        displayName = displayName.left(displayName.length() - 9);

                    if (displayName.contains(keyword, Qt::CaseInsensitive))
                    {
                        QFileInfo levelDat(subdir.absoluteFilePath() + "/level.dat");
                        if (levelDat.exists())
                        {
                            SearchResultItem item;
                            item.title = displayName;
                            item.subtitle = tr("世界 - %1").arg(instanceName);
                            item.description = subdir.absoluteFilePath();
                            item.iconText = displayName.left(1).toUpper();
                            item.type = SearchResultItem::TypeContentList;
                            item.resourceType = type;
                            item.sourceLabel = tr("本地");
                            item.mcVersion = instanceName;
                            item.updatedText = levelDat.lastModified().toString("yyyy-MM-dd");
                            results.append(item);
                        }
                    }
                }
            }
        }
    }

    ContentTypeConfig cfg = ContentTypeConfig::getConfig(type);
    if (cfg.displayName.contains(keyword, Qt::CaseInsensitive) ||
        keyword.contains(cfg.displayName, Qt::CaseInsensitive))
    {
        SearchResultItem navItem;
        navItem.title = tr("浏览 %1").arg(cfg.displayName);
        navItem.subtitle = tr("在线资源");
        navItem.description = tr("前往资源页面浏览和下载 %1").arg(cfg.displayName);
        navItem.iconText = cfg.displayName.left(1);
        navItem.type = SearchResultItem::TypeResource;
        navItem.resourceType = type;
        navItem.sourceLabel = tr("在线资源");
        results.prepend(navItem);
    }

    return results;
}

QList<SearchResultItem> SearchPage::searchAi(const QString &keyword)
{
    QList<SearchResultItem> results;

    bool keywordMatchNav = keyword.contains(tr("AI"), Qt::CaseInsensitive)
        || keyword.contains(tr("助手"), Qt::CaseInsensitive)
        || keyword.contains(tr("聊天"), Qt::CaseInsensitive);
    if (keywordMatchNav || tr("AI 助手").contains(keyword, Qt::CaseInsensitive))
    {
        SearchResultItem navItem;
        navItem.title = tr("打开 AI 助手");
        navItem.subtitle = tr("AI 助手");
        navItem.description = tr("与 AI 智能对话，获取帮助和建议");
        navItem.iconText = "AI";
        navItem.type = SearchResultItem::TypeAiChat;
        navItem.sourceLabel = tr("AI 助手");
        results.append(navItem);
    }

    int subIdx = m_currentSubFilter;
    bool searchConversations = (m_currentFilter != 4) || (subIdx != 1);
    bool searchModels = (m_currentFilter != 4) || (subIdx != 0);

    if (searchConversations)
    {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString filePath = dataDir + "/ai_conversations.json";
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();
            file.close();

            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isArray())
            {
                QJsonArray convArray = doc.array();
                for (const QJsonValue &val : convArray)
                {
                    QJsonObject convObj = val.toObject();
                    QString title = convObj["title"].toString();

                    bool titleMatch = title.contains(keyword, Qt::CaseInsensitive);
                    bool contentMatch = false;

                    QJsonArray msgArray = convObj["messages"].toArray();
                    for (const QJsonValue &msgVal : msgArray)
                    {
                        QJsonObject msgObj = msgVal.toObject();
                        QString content = msgObj["content"].toString();
                        if (content.contains(keyword, Qt::CaseInsensitive))
                        {
                            contentMatch = true;
                            break;
                        }
                    }

                    if (titleMatch || contentMatch)
                    {
                        SearchResultItem item;
                        item.title = title.isEmpty() ? tr("未命名对话") : title;
                        item.subtitle = tr("AI 对话");
                        item.description = contentMatch
                            ? tr("消息内容包含 \"%1\"").arg(keyword)
                            : tr("对话标题匹配");
                        item.iconText = "C";
                        item.type = SearchResultItem::TypeAiChat;
                        results.append(item);
                    }
                }
            }
        }
    }

    if (searchModels)
    {
        QStringList modelNames = {
            "DeepSeek Chat", "DeepSeek Reasoner", "DeepSeek V3",
            "GPT-4o", "GPT-4o Mini", "GPT-4 Turbo", "GPT-3.5 Turbo",
            "Claude 3.5 Sonnet", "Claude 3 Opus", "Claude 3 Haiku",
            "Gemini 1.5 Pro", "Gemini 1.5 Flash",
            "Qwen", "Qwen2.5", "Qwen Turbo",
            "GLM-4", "GLM-4 Plus",
            "Spark", "Spark Lite", "Spark Pro",
            "Moonshot", "Moonshot v1",
            "Doubao", "Doubao Pro",
            "Yi", "Yi Lightning"
        };

        for (const QString &modelName : modelNames)
        {
            if (modelName.contains(keyword, Qt::CaseInsensitive))
            {
                SearchResultItem item;
                item.title = modelName;
                item.subtitle = tr("AI 模型");
                item.description = tr("使用 %1 模型进行对话").arg(modelName);
                item.iconText = "M";
                item.type = SearchResultItem::TypeAiChat;
                results.append(item);
            }
        }
    }

    return results;
}

QList<SearchResultItem> SearchPage::searchExternal(const QString &keyword)
{
    QList<SearchResultItem> results;

    struct ExternalSite {
        QString name;
        QString urlTemplate;
        QString icon;
    };

    QList<ExternalSite> sites = {
        {tr("哔哩哔哩"),  "https://search.bilibili.com/all?keyword=%1", "B"},
        {tr("抖音"),       "https://www.douyin.com/search/%1", "DY"},
        {tr("必应"),       "https://www.bing.com/search?q=%1", "B"},
        {tr("谷歌"),       "https://www.google.com/search?q=%1", "G"},
        {tr("百度"),       "https://www.baidu.com/s?wd=%1", "B"},
        {tr("MC百科"),     "https://search.mcmod.cn/s?key=%1", "M"},
        {tr("CurseForge"), "https://www.curseforge.com/minecraft/search?q=%1", "C"},
        {tr("Modrinth"),   "https://modrinth.com/search?q=%1", "M"},
    };

    QString encodedKeyword = QUrl::toPercentEncoding(keyword);

    for (const auto &site : sites)
    {
        SearchResultItem item;
        item.title = tr("在 %1 中搜索").arg(site.name);
        item.subtitle = tr("外部搜索");
        item.description = site.urlTemplate.arg(encodedKeyword);
        item.iconText = site.icon;
        item.type = SearchResultItem::TypeExternalSearch;
        item.instancePath = site.urlTemplate.arg(encodedKeyword);
        item.sourceLabel = tr("外部搜索");
        results.append(item);
    }

    return results;
}

QStringList SearchPage::detectInstanceLoaders(const QString &instancePath) const
{
    QStringList loaders;
    QDir versionsDir(instancePath + "/versions");
    if (!versionsDir.exists())
        return loaders;

    QFileInfoList versionDirs = versionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &versionDir : versionDirs)
    {
        QString versionId = versionDir.fileName();
        QFile jsonFile(versionDir.absoluteFilePath() + "/" + versionId + ".json");
        if (!jsonFile.open(QIODevice::ReadOnly))
            continue;

        QByteArray data = jsonFile.readAll();
        jsonFile.close();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError)
            continue;

        QJsonObject root = doc.object();
        if (root.contains("fabricLoader") && !loaders.contains("Fabric"))
            loaders.append("Fabric");
        if (root.contains("forge") && !loaders.contains("Forge"))
            loaders.append("Forge");
        if (root.contains("quiltLoader") && !loaders.contains("Quilt"))
            loaders.append("Quilt");
        // NeoForge 使用 mainClass 或 arguments 中的 neoforge 标识
        if ((root.contains("mainClass") && root["mainClass"].toString().contains("neoforge", Qt::CaseInsensitive))
            && !loaders.contains("NeoForge"))
            loaders.append("NeoForge");
    }

    return loaders;
}

QList<SearchResultItem> SearchPage::searchFavorites(const QString &keyword)
{
    QList<SearchResultItem> results;
    QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();

    // 子筛选器对应的 ContentType 映射
    // 0=全部, 1=模组, 2=整合包, 3=资源包, 4=光影包, 5=数据包, 6=世界
    static const ContentType subFilterTypes[] = {
        ContentType::Mod, ContentType::Modpack, ContentType::ResourcePack,
        ContentType::ShaderPack, ContentType::DataPack, ContentType::World
    };

    for (const auto &folder : folders)
    {
        for (const auto &favItem : folder.items)
        {
            // 子筛选器过滤
            if (m_currentSubFilter > 0 && m_currentSubFilter <= 6)
            {
                ContentType expectedType = subFilterTypes[m_currentSubFilter - 1];
                if (favItem.contentType != static_cast<int>(expectedType))
                    continue;
            }

            QString name = favItem.displayName();
            bool nameMatch = name.contains(keyword, Qt::CaseInsensitive);
            bool descMatch = favItem.description.contains(keyword, Qt::CaseInsensitive);
            bool authorMatch = favItem.author.contains(keyword, Qt::CaseInsensitive);

            if (nameMatch || descMatch || authorMatch)
            {
                SearchResultItem item;
                item.title = name;
                item.subtitle = tr("收藏 - %1").arg(folder.name);
                item.description = favItem.description.left(120);
                item.iconText = name.left(1).toUpper();
                item.type = SearchResultItem::TypeFavorite;
                item.favoriteItemId = favItem.id;
                item.favoriteFolderId = folder.id;
                item.resourceType = static_cast<ContentType>(favItem.contentType);
                item.sourceLabel = tr("收藏夹");
                if (favItem.addedAt > 0)
                    item.updatedText = QDateTime::fromMSecsSinceEpoch(favItem.addedAt).toString("yyyy-MM-dd");
                results.append(item);
            }
        }
    }

    // 收藏导航入口
    bool keywordMatchNav = keyword.contains(tr("收藏"), Qt::CaseInsensitive)
        || keyword.contains(tr(" favorite"), Qt::CaseInsensitive);
    if (keywordMatchNav || m_currentFilter == 5)
    {
        SearchResultItem navItem;
        navItem.title = tr("浏览收藏夹");
        navItem.subtitle = tr("收藏");
        navItem.description = tr("前往收藏夹页面管理收藏的资源");
        navItem.iconText = "F";
        navItem.type = SearchResultItem::TypeFavorite;
        results.prepend(navItem);
    }

    return results;
}

QList<SearchResultItem> SearchPage::searchTasks(const QString &keyword)
{
    QList<SearchResultItem> results;
    QList<DownloadTask> tasks = DownloadTaskManager::instance()->getAllTasks();

    // 子筛选器：0=全部, 1=进行中, 2=已完成, 3=失败
    for (const auto &task : tasks)
    {
        // 子筛选器过滤
        if (m_currentSubFilter == 1)
        {
            if (task.status != DownloadTaskStatus::Queued && task.status != DownloadTaskStatus::Downloading)
                continue;
        }
        else if (m_currentSubFilter == 2)
        {
            if (task.status != DownloadTaskStatus::Completed)
                continue;
        }
        else if (m_currentSubFilter == 3)
        {
            if (task.status != DownloadTaskStatus::Failed && task.status != DownloadTaskStatus::Cancelled)
                continue;
        }

        bool nameMatch = task.instanceName.contains(keyword, Qt::CaseInsensitive);
        bool versionMatch = task.mcVersion.contains(keyword, Qt::CaseInsensitive);
        bool loaderMatch = false;
        for (const auto &loader : task.loaders)
        {
            if (loader.contains(keyword, Qt::CaseInsensitive))
            {
                loaderMatch = true;
                break;
            }
        }

        if (nameMatch || versionMatch || loaderMatch || m_currentFilter == 6)
        {
            QString statusText;
            switch (task.status)
            {
            case DownloadTaskStatus::Queued: statusText = tr("排队中"); break;
            case DownloadTaskStatus::Downloading: statusText = tr("下载中"); break;
            case DownloadTaskStatus::Paused: statusText = tr("已暂停"); break;
            case DownloadTaskStatus::Completed: statusText = tr("已完成"); break;
            case DownloadTaskStatus::Failed: statusText = tr("失败"); break;
            case DownloadTaskStatus::Cancelled: statusText = tr("已取消"); break;
            }

            SearchResultItem item;
            item.title = task.instanceName;
            item.subtitle = tr("任务 - %1").arg(statusText);
            item.description = tr("MC %1 %2 - 进度: %3%")
                .arg(task.mcVersion, task.loaders.join(", "))
                .arg(task.progress);
            item.iconText = statusText.left(1);
            item.type = SearchResultItem::TypeTask;
            item.taskId = task.taskId;
            item.sourceLabel = tr("任务");
            item.mcVersion = task.mcVersion;
            item.loader = task.loaders.join(" ");
            results.append(item);
        }
    }

    // 任务列表导航入口
    bool keywordMatchNav = keyword.contains(tr("任务"), Qt::CaseInsensitive)
        || keyword.contains(tr("下载"), Qt::CaseInsensitive);
    if (keywordMatchNav || m_currentFilter == 6)
    {
        SearchResultItem navItem;
        navItem.title = tr("打开任务列表");
        navItem.subtitle = tr("任务");
        navItem.description = tr("查看所有下载任务");
        navItem.iconText = "T";
        navItem.type = SearchResultItem::TypeTask;
        results.prepend(navItem);
    }

    return results;
}

void SearchPage::showResults(const QList<SearchResultItem> &results)
{
    clearResults();
    m_currentResults = results;

    if (results.isEmpty())
    {
        m_resultsTitle->setVisible(false);
        m_resultsScroll->setVisible(false);
        m_noResultsLabel->setVisible(true);
        return;
    }

    m_noResultsLabel->setVisible(false);

    int externalStart = -1;
    int onlineStart = -1;
    for (int i = 0; i < results.size(); ++i)
    {
        if (results[i].type == SearchResultItem::TypeExternalSearch && externalStart < 0)
            externalStart = i;
        if (results[i].type == SearchResultItem::TypeOnlineResource && onlineStart < 0)
            onlineStart = i;
    }

    int localCount = results.size();
    if (externalStart >= 0) localCount = qMin(localCount, externalStart);
    if (onlineStart >= 0) localCount = qMin(localCount, onlineStart);

    if (localCount < results.size())
    {
        m_resultsTitle->setText(tr("本地结果 (%1 项)").arg(localCount));
    }
    else
    {
        m_resultsTitle->setText(tr("搜索结果 (%1 项)").arg(results.size()));
    }
    m_resultsTitle->setVisible(true);

    appendResultWidgets(results, 0);

    m_resultsScroll->setVisible(true);
}

void SearchPage::sortResults(QList<SearchResultItem> &results)
{
    if (!m_sortCombo) return;
    const int sortMode = m_sortCombo->currentIndex();
    if (sortMode <= 0) return;

    auto comparator = [sortMode](const SearchResultItem &a, const SearchResultItem &b) -> bool {
        // Keep external search links always at the bottom
        const bool aExt = a.type == SearchResultItem::TypeExternalSearch;
        const bool bExt = b.type == SearchResultItem::TypeExternalSearch;
        if (aExt != bExt)
            return !aExt;
        if (aExt)
            return a.title < b.title;

        switch (sortMode)
        {
        case 1: // 下载量
            return a.downloadCount > b.downloadCount;
        case 2: // 最近更新
            return a.updatedText > b.updatedText;
        case 3: // 评分/关注度
        {
            // 优先使用rating，如果没有则使用followers
            double aScore = a.rating > 0 ? a.rating : (a.followers > 0 ? a.followers : 0);
            double bScore = b.rating > 0 ? b.rating : (b.followers > 0 ? b.followers : 0);
            return aScore > bScore;
        }
        default:
            return false;
        }
    };

    std::stable_sort(results.begin(), results.end(), comparator);
}

QString SearchPage::typeBadgeText(const SearchResultItem &item) const
{
    switch (item.type)
    {
    case SearchResultItem::TypeOnlineResource:
    {
        const auto cfg = ContentTypeConfig::getConfig(item.resourceType);
        return cfg.displayName;
    }
    case SearchResultItem::TypeInstance:      return tr("实例");
    case SearchResultItem::TypeSettings:      return tr("设置");
    case SearchResultItem::TypeResource:      return tr("资源");
    case SearchResultItem::TypeContentList:   return ContentTypeConfig::getConfig(item.resourceType).displayName;
    case SearchResultItem::TypeAiChat:        return tr("AI 助手");
    case SearchResultItem::TypeExternalSearch:return tr("外部搜索");
    case SearchResultItem::TypeFavorite:      return tr("收藏");
    case SearchResultItem::TypeTask:          return tr("任务");
    }
    return QString();
}

QString SearchPage::sourceLabelFor(const SearchResultItem &item) const
{
    if (!item.sourceLabel.isEmpty())
        return item.sourceLabel;
    if (item.type == SearchResultItem::TypeOnlineResource)
        return item.onlineSource == "curseforge" ? "CurseForge" : "Modrinth";
    if (item.type == SearchResultItem::TypeExternalSearch)
        return tr("外部搜索");
    if (item.type == SearchResultItem::TypeInstance)
        return tr("本地");
    if (item.type == SearchResultItem::TypeSettings)
        return tr("设置");
    if (item.type == SearchResultItem::TypeFavorite)
        return tr("收藏夹");
    if (item.type == SearchResultItem::TypeTask)
        return tr("任务");
    return item.subtitle;
}

QWidget *SearchPage::buildResultCard(const SearchResultItem &item, int index, const QColor &themeColor)
{
    auto *card = new QWidget(m_resultsContainer);
    card->setObjectName("searchResultCard");
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("resultIndex", index);
    if (item.type == SearchResultItem::TypeOnlineResource)
        card->setProperty("resultType", "onlineResource");
    else if (item.type == SearchResultItem::TypeExternalSearch)
        card->setProperty("resultType", "externalSearch");
    else
        card->setProperty("resultType", "localInstance");

    auto *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(12);

    // Icon (44x44) with rounded corner; external / online get distinct hues
    QColor iconBg = themeColor;
    if (item.type == SearchResultItem::TypeExternalSearch)
        iconBg = QColor("#78909C");
    else if (item.type == SearchResultItem::TypeOnlineResource)
        iconBg = QColor("#5C6BC0");
    else if (item.type == SearchResultItem::TypeFavorite)
        iconBg = QColor("#EC4899");
    else if (item.type == SearchResultItem::TypeTask)
        iconBg = QColor("#34D399");

    auto *iconLabel = new QLabel();
    iconLabel->setObjectName("searchResultIcon");
    iconLabel->setFixedSize(46, 46);
    iconLabel->setAlignment(Qt::AlignCenter);
    {
        QPixmap pix(46, 46);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(iconBg);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(0, 0, 46, 46, 11, 11);
        painter.setPen(Qt::white);
        QFont font;
        font.setBold(true);
        font.setPixelSize(18);
        painter.setFont(font);
        painter.drawText(pix.rect(), Qt::AlignCenter, item.iconText.isEmpty() ? "?" : item.iconText.left(2));
        painter.end();
        iconLabel->setPixmap(pix);
    }
    cardLayout->addWidget(iconLabel);

    // Middle info column
    auto *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(3);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    titleRow->setContentsMargins(0, 0, 0, 0);

    QString displayTitle = item.title;
    if (!m_currentKeyword.isEmpty() && displayTitle.contains(m_currentKeyword, Qt::CaseInsensitive))
    {
        // Prototype-style highlight: wrap matched substring with <mark>
        int idx = displayTitle.indexOf(m_currentKeyword, 0, Qt::CaseInsensitive);
        QString html = displayTitle.left(idx)
            + QStringLiteral("<span style=\"background:rgba(245,158,11,0.30);color:#0F172A;\">")
            + displayTitle.mid(idx, m_currentKeyword.length())
            + QStringLiteral("</span>")
            + displayTitle.mid(idx + m_currentKeyword.length());
        displayTitle = html;
    }
    else
    {
        displayTitle = displayTitle.toHtmlEscaped();
    }

    auto *titleLabel = new QLabel(displayTitle);
    titleLabel->setObjectName("searchResultTitle");
    titleLabel->setTextFormat(Qt::RichText);
    titleRow->addWidget(titleLabel);

    if (!typeBadgeText(item).isEmpty())
    {
        auto *badge = new QLabel(typeBadgeText(item));
        badge->setObjectName("searchResultTypeBadge");
        badge->setProperty("badgeKind", "mod");
        if (item.type == SearchResultItem::TypeInstance) badge->setProperty("badgeKind", "instance");
        else if (item.type == SearchResultItem::TypeSettings) badge->setProperty("badgeKind", "setting");
        else if (item.type == SearchResultItem::TypeFavorite) badge->setProperty("badgeKind", "favorite");
        else if (item.type == SearchResultItem::TypeTask) badge->setProperty("badgeKind", "task");
        else if (item.type == SearchResultItem::TypeExternalSearch) badge->setProperty("badgeKind", "external");
        titleRow->addWidget(badge);
    }
    titleRow->addStretch();
    infoLayout->addLayout(titleRow);

    // Meta row: source · author · mc version · loader · updated
    auto *metaRow = new QHBoxLayout();
    metaRow->setSpacing(6);
    metaRow->setContentsMargins(0, 0, 0, 0);

    auto *srcLabel = new QLabel(sourceLabelFor(item));
    srcLabel->setObjectName("searchResultSrc");
    srcLabel->setProperty("resultType", card->property("resultType"));
    metaRow->addWidget(srcLabel);

    QString authorText = item.subtitle;
    const int dash = authorText.indexOf(QStringLiteral(" - "));
    if (dash >= 0)
        authorText = authorText.mid(dash + 3);
    if (authorText.isEmpty() || authorText == item.subtitle)
        authorText.clear();

    const QStringList metaParts = { authorText, item.mcVersion, item.loader, item.updatedText };
    for (const QString &part : metaParts)
    {
        if (part.isEmpty())
            continue;
        auto *chip = new QLabel(part);
        chip->setObjectName("searchResultMetaChip");
        metaRow->addWidget(chip);
    }

    metaRow->addStretch();
    infoLayout->addLayout(metaRow);

    // Description line
    if (!item.description.isEmpty())
    {
        QString desc = item.description;
        if (item.type == SearchResultItem::TypeExternalSearch)
            desc = tr("在浏览器中打开搜索");
        auto *descLabel = new QLabel(desc);
        descLabel->setObjectName("searchResultDescription");
        descLabel->setWordWrap(true);
        infoLayout->addWidget(descLabel);
    }

    // Tags row (only for online resources)
    if (item.type == SearchResultItem::TypeOnlineResource && !item.tags.isEmpty())
    {
        auto *tagsRow = new QHBoxLayout();
        tagsRow->setSpacing(4);
        tagsRow->setContentsMargins(0, 0, 0, 0);
        for (const QString &tag : item.tags)
        {
            auto *tagLabel = new QLabel(tag);
            tagLabel->setObjectName("searchResultTag");
            tagsRow->addWidget(tagLabel);
        }
        tagsRow->addStretch();
        infoLayout->addLayout(tagsRow);
    }

    cardLayout->addLayout(infoLayout, 1);

    // Right column: stats chips + version + action
    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(6);
    rightCol->setContentsMargins(0, 0, 0, 0);
    rightCol->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    if (item.downloadCount > 0)
    {
        auto *dlChip = new QLabel(QStringLiteral("\u2b07 ") + formatCount(item.downloadCount));
        dlChip->setObjectName("searchResultStatChip");
        rightCol->addWidget(dlChip, 0, Qt::AlignRight);
    }
    if (item.rating > 0)
    {
        auto *ratingChip = new QLabel(QStringLiteral("\u2605 ") + QString::number(item.rating, 'f', 1));
        ratingChip->setObjectName("searchResultStatChip");
        rightCol->addWidget(ratingChip, 0, Qt::AlignRight);
    }
    else if (item.followers > 0)
    {
        auto *followChip = new QLabel(QStringLiteral("\u2665 ") + formatCount(item.followers));
        followChip->setObjectName("searchResultStatChip");
        rightCol->addWidget(followChip, 0, Qt::AlignRight);
    }

    auto *actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(6);
    actionsRow->setContentsMargins(0, 0, 0, 0);

    if (item.type == SearchResultItem::TypeOnlineResource)
    {
        if (!item.onlineVersion.isEmpty())
        {
            auto *verLabel = new QLabel(item.onlineVersion);
            verLabel->setObjectName("searchResultVersion");
            actionsRow->addWidget(verLabel);
        }
        auto *openBtn = new QPushButton(tr("查看"));
        openBtn->setObjectName("searchResultActionBtn");
        openBtn->setCursor(Qt::PointingHandCursor);
        actionsRow->addWidget(openBtn);
    }
    else if (item.type == SearchResultItem::TypeExternalSearch)
    {
        auto *openBtn = new QPushButton(tr("打开 \u2197"));
        openBtn->setObjectName("searchResultActionBtn");
        openBtn->setCursor(Qt::PointingHandCursor);
        actionsRow->addWidget(openBtn);
    }
    else
    {
        auto *openBtn = new QPushButton(tr("打开"));
        openBtn->setObjectName("searchResultActionBtn");
        openBtn->setCursor(Qt::PointingHandCursor);
        actionsRow->addWidget(openBtn);
    }
    rightCol->addLayout(actionsRow);

    cardLayout->addLayout(rightCol);

    return card;
}

void SearchPage::appendResultWidgets(const QList<SearchResultItem> &results, int baseIndex, int insertPos, bool addOnlineSep)
{
    if (results.isEmpty()) return;

    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    int onlineStart = -1;
    int externalStart = -1;
    for (int i = 0; i < results.size(); ++i)
    {
        if (results[i].type == SearchResultItem::TypeExternalSearch && externalStart < 0)
            externalStart = i;
        if (results[i].type == SearchResultItem::TypeOnlineResource && onlineStart < 0)
            onlineStart = i;
    }

    for (int i = 0; i < results.size(); ++i)
    {
        const auto &item = results[i];

        if (onlineStart >= 0 && i == onlineStart && addOnlineSep)
        {
            auto *sepWidget = new QWidget(m_resultsContainer);
            sepWidget->setObjectName("externalSearchSep");
            sepWidget->setFixedHeight(44);
            auto *sepLayout = new QHBoxLayout(sepWidget);
            sepLayout->setContentsMargins(0, 0, 0, 0);
            sepLayout->setSpacing(10);
            auto *lineLeft = new QWidget();
            lineLeft->setObjectName("externalSearchSepLine");
            lineLeft->setFixedHeight(1);
            lineLeft->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            auto *sepLabel = new QLabel(tr("线上资源"));
            sepLabel->setObjectName("externalSearchSepLabel");
            auto *lineRight = new QWidget();
            lineRight->setObjectName("externalSearchSepLine");
            lineRight->setFixedHeight(1);
            lineRight->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            sepLayout->addWidget(lineLeft);
            sepLayout->addWidget(sepLabel);
            sepLayout->addWidget(lineRight);
            if (insertPos >= 0)
                m_resultsLayout->insertWidget(insertPos++, sepWidget);
            else
                m_resultsLayout->addWidget(sepWidget);
        }

        if (externalStart >= 0 && i == externalStart && onlineStart != i)
        {
            auto *sepWidget = new QWidget(m_resultsContainer);
            sepWidget->setObjectName("externalSearchSep");
            sepWidget->setFixedHeight(44);
            auto *sepLayout = new QHBoxLayout(sepWidget);
            sepLayout->setContentsMargins(0, 0, 0, 0);
            sepLayout->setSpacing(10);
            auto *lineLeft = new QWidget();
            lineLeft->setObjectName("externalSearchSepLine");
            lineLeft->setFixedHeight(1);
            lineLeft->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            auto *sepLabel = new QLabel(tr("外部搜索"));
            sepLabel->setObjectName("externalSearchSepLabel");
            auto *lineRight = new QWidget();
            lineRight->setObjectName("externalSearchSepLine");
            lineRight->setFixedHeight(1);
            lineRight->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            sepLayout->addWidget(lineLeft);
            sepLayout->addWidget(sepLabel);
            sepLayout->addWidget(lineRight);
            if (insertPos >= 0)
                m_resultsLayout->insertWidget(insertPos++, sepWidget);
            else
                m_resultsLayout->addWidget(sepWidget);
        }

        QWidget *card = buildResultCard(item, baseIndex + i, themeColor);
        if (insertPos >= 0)
            m_resultsLayout->insertWidget(insertPos++, card);
        else
            m_resultsLayout->addWidget(card);
        card->installEventFilter(this);
    }

    if (m_resultsLayout->count() == 0 || !m_resultsLayout->itemAt(m_resultsLayout->count() - 1)->spacerItem())
        m_resultsLayout->addStretch();
}

void SearchPage::appendOnlineResults(const QList<SearchResultItem> &newResults, const QString &sourceName)
{
    if (newResults.isEmpty()) return;

    // Remove trailing stretch
    QLayoutItem *stretchItem = nullptr;
    if (m_resultsLayout->count() > 0)
    {
        stretchItem = m_resultsLayout->itemAt(m_resultsLayout->count() - 1);
        if (stretchItem && stretchItem->spacerItem())
        {
            m_resultsLayout->removeItem(stretchItem);
            delete stretchItem;
        }
    }

    // Determine where local results end in the current data
    int localEnd = m_currentResults.size();
    for (int i = 0; i < m_currentResults.size(); ++i)
    {
        if (m_currentResults[i].type == SearchResultItem::TypeOnlineResource ||
            m_currentResults[i].type == SearchResultItem::TypeExternalSearch)
        {
            localEnd = i;
            break;
        }
    }

    // Check if online results already exist in old data (avoids duplicate separator)
    bool hadOnlineBefore = false;
    for (const auto &r : m_currentResults)
    {
        if (r.type == SearchResultItem::TypeOnlineResource)
        {
            hadOnlineBefore = true;
            break;
        }
    }

    // Update the data model: insert new online results after local items
    QList<SearchResultItem> combined;
    for (int i = 0; i < localEnd; ++i)
        combined.append(m_currentResults[i]);
    int insertStart = combined.size();
    for (const auto &r : newResults)
        combined.append(r);
    for (int i = localEnd; i < m_currentResults.size(); ++i)
        combined.append(m_currentResults[i]);

    m_currentResults = combined;

    // Update title to show "本地结果" count
    m_resultsTitle->setText(tr("本地结果 (%1 项)").arg(localEnd));

    // Find insert position in layout: right before the "外部搜索" separator, if it exists
    int layoutInsertPos = m_resultsLayout->count();
    for (int i = 0; i < m_resultsLayout->count(); ++i)
    {
        QLayoutItem *item = m_resultsLayout->itemAt(i);
        if (item && item->widget() && item->widget()->objectName() == "externalSearchSep")
        {
            auto *label = item->widget()->findChild<QLabel*>("externalSearchSepLabel");
            if (label && label->text() == tr("外部搜索"))
            {
                layoutInsertPos = i;
                break;
            }
        }
    }

    // Incrementally add the new online result widgets (avoids full clear+rebuild flash)
    appendResultWidgets(newResults, insertStart, layoutInsertPos, !hadOnlineBefore);

    m_resultsScroll->setVisible(true);
}

void SearchPage::clearResults()
{
    m_currentResults.clear();
    m_resultsTitle->setVisible(false);
    m_noResultsLabel->setVisible(false);
    m_resultsScroll->setVisible(false);

    while (QLayoutItem *layoutItem = m_resultsLayout->takeAt(0))
    {
        if (layoutItem->widget())
        {
            layoutItem->widget()->removeEventFilter(this);
            delete layoutItem->widget();
        }
        delete layoutItem;
    }
}

void SearchPage::onResultClicked(int index)
{
    if (index < 0 || index >= m_currentResults.size())
        return;

    const auto &item = m_currentResults[index];

    switch (item.type)
    {
    case SearchResultItem::TypeInstance:
        emit navigateToInstance(item.instancePath);
        break;
    case SearchResultItem::TypeSettings:
        emit navigateToSettings(item.settingsTabIndex);
        break;
    case SearchResultItem::TypeResource:
        // 安装新实例 / 导入整合包 是特殊的资源导航项
        if (item.iconText == "+")
            emit navigateToInstallInstance();
        else if (item.iconText == "I")
            emit navigateToModpackImport();
        else
            emit navigateToResource(item.resourceType);
        break;
    case SearchResultItem::TypeContentList:
        emit navigateToResource(item.resourceType);
        break;
    case SearchResultItem::TypeAiChat:
        emit navigateToAiChat();
        break;
    case SearchResultItem::TypeExternalSearch:
        openExternalUrl(item.instancePath);
        break;
    case SearchResultItem::TypeOnlineResource:
    {
        ModInfo modInfo;
        modInfo.id = item.onlineId;
        modInfo.name = item.title;
        modInfo.author = item.onlineAuthor;
        modInfo.iconUrl = item.onlineIconUrl;
        modInfo.latestVersion = item.onlineVersion;
        modInfo.source = item.onlineSource;
        emit navigateToResourceDetail(modInfo, item.resourceType);
        break;
    }
    case SearchResultItem::TypeFavorite:
        emit navigateToFavorites();
        break;
    case SearchResultItem::TypeTask:
        emit navigateToTaskList();
        break;
    }
}

void SearchPage::openExternalUrl(const QString &url)
{
    QDesktopServices::openUrl(QUrl(url));
}

void SearchPage::initUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Fixed top section (title, search bar, filters)
    auto *topWidget = new QWidget();
    auto *topLayout = new QVBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);

    topLayout->addSpacing(60);

    auto *titleLabel = new OutlinedLabel(tr("方块盒子智能搜索"));
    titleLabel->setObjectName("searchPageTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    topLayout->addWidget(titleLabel);

    topLayout->addSpacing(12);

    topLayout->addWidget(createSearchBar());

    topLayout->addSpacing(10);

    topLayout->addWidget(createHotSearchBar());

    topLayout->addSpacing(14);

    topLayout->addWidget(createFilterBar());

    topLayout->addSpacing(8);

    mainLayout->addWidget(topWidget);

    // Results toolbar (sort) + results area fills remaining space
    mainLayout->addWidget(createResultsToolbar());
    mainLayout->addWidget(createResultsArea(), 1);
}

bool SearchPage::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        auto *widget = qobject_cast<QWidget *>(obj);
        if (widget)
        {
            bool ok;
            int idx = widget->property("resultIndex").toInt(&ok);
            if (ok)
            {
                onResultClicked(idx);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
