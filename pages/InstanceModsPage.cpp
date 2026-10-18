/**
 * @file   InstanceModsPage.cpp
 * @brief  实例管理 - 模组列表页面实现
 * @author BlockBox Team
 * @date   2026-06-19
 */
#include "InstanceModsPage.h"
#include "ResourcesPage.h"
#include "components/BlurLoadingOverlay.h"
#include "components/MasonryContentCard.h"
#include "components/ContentViewSwitch.h"
#include "components/LocalCategoryDialog.h"
#include "components/FavoriteFolderDialog.h"
#include "../layouts/FlowLayout.h"
#include "../layouts/MasonryLayout.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/LocalCategoryManager.h"
#include "utils/mod/ModNameFetcher.h"
#include "utils/mod/ModrinthAPI.h"
#include "utils/content/ContentDownloader.h"
#include "utils/DownloadTaskManager.h"

#include <QApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMenu>
#include <QMimeData>
#include "components/AppMessageBox.h"
#include <QMouseEvent>
#include <QPixmapCache>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSet>
#include <QSpacerItem>
#include <QTimer>
#include <QUrl>

#include <algorithm>

InstanceModsPage::InstanceModsPage(QWidget *parent)
    : QWidget(parent)
    , m_searchEdit(nullptr)
    , m_filterCombo(nullptr)
    , m_timeCombo(nullptr)
    , m_sizeCombo(nullptr)
    , m_sortCombo(nullptr)
    , m_statsLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_cardFlowLayout(nullptr)
    , m_viewSwitch(nullptr)
    , m_viewMode(ContentViewSwitch::Masonry)
    , m_loadingOverlay(nullptr)
    , m_emptyLabel(nullptr)
    , m_downloadSearchBtn(nullptr)
    , m_bottomBar(nullptr)
    , m_bottomCheckUpdateBtn(nullptr)
    , m_selectAllBtn(nullptr)
    , m_copyModBtn(nullptr)
    , m_toggleSelectedBtn(nullptr)
    , m_openFolderBtn(nullptr)
    , m_detailBtn(nullptr)
    , m_pasteModBtn(nullptr)
    , m_updateModBtn(nullptr)
    , m_installNewBtn(nullptr)
    , m_scanner(nullptr)
    , m_nameFetcher(nullptr)
    , m_updateCheckApi(nullptr)
    , m_updateCheckTotal(0)
    , m_updateCheckFound(0)
    , m_currentFilter(FilterAll)
    , m_timeFilter(0)
    , m_sizeFilter(0)
    , m_sortMode(0)
    , m_isLoading(false)
    , m_allSelected(false)
    , m_categoryManager(nullptr)
    , m_categoryCombo(nullptr)
    , m_categoryManageBtn(nullptr)
{
    m_scanner = new ModScanner(this);
    m_nameFetcher = new ModNameFetcher(this);
    m_updateCheckApi = new ModrinthAPI(this);
    m_categoryManager = new LocalCategoryManager(this);
    initUI();
    setupConnections();
}

InstanceModsPage::~InstanceModsPage()
{
}

void InstanceModsPage::setInstancePath(const QString &path)
{
    if (m_instancePath != path)
    {
        m_instancePath = path;
        m_categoryManager->setInstancePath(path);
        m_categoryManager->setResourceType(QStringLiteral("mods"));
        m_currentCategoryId.clear();
        reloadCategoryCombo();
        if (!path.isEmpty())
        {
            refreshModList();
        }
    }
}

void InstanceModsPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // 顶部搜索/筛选条：微透明玻璃态卡片
    QWidget *filterCard = new QWidget(this);
    filterCard->setObjectName("filterCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(8);

    // 搜索框
    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索模组名称..."));
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->setClearButtonEnabled(true);

    // ---- 行1：搜索 + 视图切换 + 分类 ----
    QHBoxLayout *searchRow = new QHBoxLayout();
    searchRow->setSpacing(8);
    searchRow->addWidget(m_searchEdit, 1);

    // 视图切换（列表式 / 瀑布流）
    m_viewSwitch = new ContentViewSwitch(this);
    m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted("instance_mods", ContentViewSwitch::List));
    m_viewMode = static_cast<int>(m_viewSwitch->viewMode());
    searchRow->addWidget(m_viewSwitch);

    // 本地资源分类筛选（手动分类）
    QLabel *categoryLabel = new QLabel(tr("分类:"));
    categoryLabel->setObjectName("filterLabel");
    searchRow->addWidget(categoryLabel);
    m_categoryCombo = new QComboBox();
    m_categoryCombo->setObjectName("categoryCombo");
    m_categoryCombo->setFixedHeight(32);
    m_categoryCombo->setMinimumWidth(140);
    m_categoryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_categoryCombo->setMinimumContentsLength(6);
    reloadCategoryCombo();
    searchRow->addWidget(m_categoryCombo);

    m_categoryManageBtn = new QPushButton(tr("管理分类"));
    m_categoryManageBtn->setObjectName("categoryManageBtn");
    m_categoryManageBtn->setFixedHeight(32);
    m_categoryManageBtn->setCursor(Qt::PointingHandCursor);
    searchRow->addWidget(m_categoryManageBtn);
    cardLayout->addLayout(searchRow);

    // ---- 行2：状态 / 时间 / 大小 / 排序 + 统计 ----
    QHBoxLayout *filterBar = new QHBoxLayout();
    filterBar->setSpacing(8);

    QLabel *filterLabel = new QLabel(tr("筛选:"));
    filterLabel->setObjectName("filterLabel");
    filterBar->addWidget(filterLabel);
    m_filterCombo = new QComboBox();
    m_filterCombo->setObjectName("filterCombo");
    m_filterCombo->setFixedHeight(32);
    m_filterCombo->setMinimumWidth(120);
    m_filterCombo->addItem(tr("全部"), FilterAll);
    m_filterCombo->addItem(tr("已启用"), FilterEnabled);
    m_filterCombo->addItem(tr("已禁用"), FilterDisabled);
    filterBar->addWidget(m_filterCombo);

    QLabel *timeLabel = new QLabel(tr("时间:"));
    timeLabel->setObjectName("filterLabel");
    filterBar->addWidget(timeLabel);
    m_timeCombo = new QComboBox();
    m_timeCombo->setObjectName("filterCombo");
    m_timeCombo->setFixedHeight(32);
    m_timeCombo->setMinimumWidth(104);
    m_timeCombo->addItem(tr("全部时间"), 0);
    m_timeCombo->addItem(tr("最近 7 天"), 1);
    m_timeCombo->addItem(tr("最近 30 天"), 2);
    m_timeCombo->addItem(tr("最近一年"), 3);
    filterBar->addWidget(m_timeCombo);

    QLabel *sizeLabel = new QLabel(tr("大小:"));
    sizeLabel->setObjectName("filterLabel");
    filterBar->addWidget(sizeLabel);
    m_sizeCombo = new QComboBox();
    m_sizeCombo->setObjectName("filterCombo");
    m_sizeCombo->setFixedHeight(32);
    m_sizeCombo->setMinimumWidth(104);
    m_sizeCombo->addItem(tr("全部大小"), 0);
    m_sizeCombo->addItem(tr("小于 10 MB"), 1);
    m_sizeCombo->addItem(tr("10 ~ 100 MB"), 2);
    m_sizeCombo->addItem(tr("大于 100 MB"), 3);
    filterBar->addWidget(m_sizeCombo);

    QLabel *sortLabel = new QLabel(tr("排序:"));
    sortLabel->setObjectName("filterLabel");
    filterBar->addWidget(sortLabel);
    m_sortCombo = new QComboBox();
    m_sortCombo->setObjectName("searchSortCombo");
    m_sortCombo->setFixedHeight(32);
    m_sortCombo->setMinimumWidth(104);
    m_sortCombo->addItem(tr("名称 A→Z"), 0);
    m_sortCombo->addItem(tr("名称 Z→A"), 1);
    m_sortCombo->addItem(tr("大小 大→小"), 2);
    m_sortCombo->addItem(tr("大小 小→大"), 3);
    m_sortCombo->addItem(tr("时间 新→旧"), 4);
    m_sortCombo->addItem(tr("时间 旧→新"), 5);
    filterBar->addWidget(m_sortCombo);

    filterBar->addStretch();

    m_statsLabel = new QLabel();
    m_statsLabel->setObjectName("statusLabel");
    filterBar->addWidget(m_statsLabel);
    cardLayout->addLayout(filterBar);

    mainLayout->addWidget(filterCard);

    // 卡片滚动区域
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_cardContainer = new QWidget();
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

    // 空提示放在列表区域内（无资源时显示在卡片本该出现的位置）
    m_emptyLabel = new QLabel(tr("暂无模组"));
    m_emptyLabel->setObjectName("modPlaceholderLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    mainLayout->addWidget(m_emptyLabel, 1);

    // 底部操作栏
    m_bottomBar = new QWidget();
    m_bottomBar->setObjectName("modBottomBar");
    m_bottomBar->setFixedHeight(48);
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(14, 6, 14, 6);
    bottomLayout->setSpacing(10);

    m_bottomCheckUpdateBtn = new QPushButton(tr("检查更新"));
    m_bottomCheckUpdateBtn->setObjectName("bottomActionBtn");
    m_bottomCheckUpdateBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/update.svg",
        ThemeManager::instance()->currentThemeColor(), 18));
    m_bottomCheckUpdateBtn->setIconSize(QSize(18, 18));

    m_selectAllBtn = new QPushButton(tr("全选"));
    m_selectAllBtn->setObjectName("bottomActionBtn");

    m_copyModBtn = new QPushButton(tr("复制"));
    m_copyModBtn->setObjectName("bottomActionBtn");
    m_copyModBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/copy.svg",
        ThemeManager::instance()->currentThemeColor(), 18));
    m_copyModBtn->setIconSize(QSize(18, 18));
    m_copyModBtn->hide();

    m_toggleSelectedBtn = new QPushButton(tr("启用/禁用"));
    m_toggleSelectedBtn->setObjectName("bottomActionBtn");
    m_toggleSelectedBtn->hide();

    m_openFolderBtn = new QPushButton(tr("打开文件夹"));
    m_openFolderBtn->setObjectName("bottomActionBtn");
    m_openFolderBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/folder.svg",
        ThemeManager::instance()->currentThemeColor(), 18));
    m_openFolderBtn->setIconSize(QSize(18, 18));

    m_detailBtn = new QPushButton(tr("详情"));
    m_detailBtn->setObjectName("bottomActionBtn");
    m_detailBtn->setCursor(Qt::PointingHandCursor);
    m_detailBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/list.svg", ThemeManager::instance()->currentThemeColor(), 16));
    m_detailBtn->setIconSize(QSize(16, 16));

    m_pasteModBtn = new QPushButton(tr("粘贴模组"));
    m_pasteModBtn->setObjectName("bottomActionBtn");

    m_updateModBtn = new QPushButton(tr("更新"));
    m_updateModBtn->setObjectName("bottomActionBtn");
    m_updateModBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/update.svg",
        ThemeManager::instance()->currentThemeColor(), 18));
    m_updateModBtn->setIconSize(QSize(18, 18));
    m_updateModBtn->hide();

    m_installNewBtn = new QPushButton(tr("安装新模组"));
    m_installNewBtn->setObjectName("bottomActionBtn");
    m_installNewBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/install.svg",
        ThemeManager::instance()->currentThemeColor(), 18));
    m_installNewBtn->setIconSize(QSize(18, 18));
    m_installNewBtn->setCursor(Qt::PointingHandCursor);
    m_installNewBtn->setToolTip(tr("前往模组下载页，自动筛选至当前实例"));

    bottomLayout->addWidget(m_bottomCheckUpdateBtn);
    bottomLayout->addWidget(m_selectAllBtn);
    bottomLayout->addWidget(m_copyModBtn);
    bottomLayout->addWidget(m_toggleSelectedBtn);
    bottomLayout->addWidget(m_openFolderBtn);
    bottomLayout->addWidget(m_detailBtn);
    bottomLayout->addWidget(m_pasteModBtn);
    bottomLayout->addWidget(m_updateModBtn);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_installNewBtn);
    mainLayout->addWidget(m_bottomBar);

    // 加载遮罩
    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();

    // 搜索无结果时前往下载页按钮
    m_downloadSearchBtn = new QPushButton(tr("前往下载模组页搜索"));
    m_downloadSearchBtn->setObjectName("downloadSearchBtn");
    m_downloadSearchBtn->setFixedHeight(36);
    m_downloadSearchBtn->hide();
    mainLayout->addWidget(m_downloadSearchBtn);
}

void InstanceModsPage::setupConnections()
{
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &InstanceModsPage::onSearchTextChanged);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceModsPage::onFilterChanged);
    connect(m_timeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceModsPage::onTimeFilterChanged);
    connect(m_sizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceModsPage::onSizeFilterChanged);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceModsPage::onSortChanged);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceModsPage::onCategoryFilterChanged);
    connect(m_categoryManageBtn, &QPushButton::clicked,
            this, &InstanceModsPage::onCategoryManageClicked);
    connect(m_categoryManager, &LocalCategoryManager::categoriesChanged,
            this, &InstanceModsPage::onCategoriesChanged);
    connect(m_categoryManager, &LocalCategoryManager::assignmentsChanged,
            this, &InstanceModsPage::onAssignmentsChanged);
    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged,
            this, &InstanceModsPage::onViewModeChanged);
    connect(m_bottomCheckUpdateBtn, &QPushButton::clicked, this, &InstanceModsPage::onCheckUpdatesClicked);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &InstanceModsPage::onSelectAllClicked);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &InstanceModsPage::onOpenFolderClicked);
    connect(m_detailBtn, &QPushButton::clicked, this, [this]() {
        if (!m_selectedModIndices.isEmpty()) {
            int idx = *m_selectedModIndices.begin();
            if (idx >= 0 && idx < m_modList.mods.size())
                emit modDetailRequested(m_modList.mods[idx]);
        }
    });
    connect(m_pasteModBtn, &QPushButton::clicked, this, &InstanceModsPage::onPasteModClicked);
    connect(m_copyModBtn, &QPushButton::clicked, this, &InstanceModsPage::onCopyModClicked);
    connect(m_toggleSelectedBtn, &QPushButton::clicked, this, &InstanceModsPage::onToggleSelectedClicked);
    connect(m_updateModBtn, &QPushButton::clicked, this, &InstanceModsPage::onUpdateSelectedClicked);
    connect(m_downloadSearchBtn, &QPushButton::clicked, this, &InstanceModsPage::onDownloadSearchClicked);
    connect(m_installNewBtn, &QPushButton::clicked, this, [this]() {
        emit modDownloadSearchRequested(QString());
    });
    connect(m_scanner, &ModScanner::scanCompleted, this, &InstanceModsPage::onScanCompleted);
    connect(m_scanner, &ModScanner::scanFailed, this, &InstanceModsPage::onScanFailed);
    connect(m_nameFetcher, &ModNameFetcher::namesResolved,
            this, &InstanceModsPage::onModNameResolved);
    connect(m_nameFetcher, &ModNameFetcher::namesFetchFailed,
            this, &InstanceModsPage::onModNameFetchFailed);
    connect(m_updateCheckApi, &ModrinthAPI::hashesMatched,
            this, &InstanceModsPage::onHashesMatched);
    connect(m_updateCheckApi, &ModrinthAPI::hashesMatchFailed,
            this, &InstanceModsPage::onUpdateCheckBatchFailed);
    connect(m_updateCheckApi, &ModrinthAPI::updatesChecked,
            this, &InstanceModsPage::onUpdatesChecked);
    connect(m_updateCheckApi, &ModrinthAPI::updatesCheckFailed,
            this, &InstanceModsPage::onUpdateCheckBatchFailed);
}

void InstanceModsPage::refreshModList()
{
    if (m_instancePath.isEmpty())
    {
        return;
    }
    m_isLoading = true;
    showLoading(true);
    showEmptyHint(false);
    clearCards();
    m_scanner->scanMods(m_instancePath);
}

void InstanceModsPage::onSearchTextChanged(const QString &text)
{
    m_currentSearch = text.trimmed();
    applyFilterAndSearch();
}

void InstanceModsPage::onFilterChanged(int index)
{
    Q_UNUSED(index);
    m_currentFilter = m_filterCombo->currentData().toInt();
    applyFilterAndSearch();
}

void InstanceModsPage::onTimeFilterChanged(int index)
{
    if (!m_timeCombo)
        return;
    m_timeFilter = m_timeCombo->itemData(index).toInt();
    applyFilterAndSearch();
}

void InstanceModsPage::onSizeFilterChanged(int index)
{
    if (!m_sizeCombo)
        return;
    m_sizeFilter = m_sizeCombo->itemData(index).toInt();
    applyFilterAndSearch();
}

void InstanceModsPage::onSortChanged(int index)
{
    if (!m_sortCombo)
        return;
    const int newMode = m_sortCombo->itemData(index).toInt();
    if (newMode == m_sortMode)
        return;
    if (m_updateInProgress)
    {
        // 更新队列持有索引，更新过程中不重排：恢复下拉原值
        m_sortCombo->blockSignals(true);
        int oldIndex = m_sortCombo->findData(m_sortMode);
        if (oldIndex >= 0)
            m_sortCombo->setCurrentIndex(oldIndex);
        m_sortCombo->blockSignals(false);
        return;
    }
    m_sortMode = newMode;
    applySortOrder();
}

/* 按当前排序模式重排 m_modList.mods（只动数据顺序，卡片与索引由 applySortOrder 重建） */
void InstanceModsPage::sortFileList()
{
    const int mode = m_sortMode;
    // 名称比较忽略 .disabled 后缀，禁用项与本体排在一起
    auto displayName = [](const ModInfo &m) {
        QString n = m.fileName;
        if (n.endsWith(".disabled", Qt::CaseInsensitive))
            n.chop(9);
        return n;
    };
    std::sort(m_modList.mods.begin(), m_modList.mods.end(),
              [mode, &displayName, this](const ModInfo &a, const ModInfo &b) {
        switch (mode)
        {
        case 1:  // 名称 Z→A
            return QString::compare(displayName(a), displayName(b), Qt::CaseInsensitive) > 0;
        case 2:  // 大小 大→小
            return a.fileSize > b.fileSize;
        case 3:  // 大小 小→大
            return a.fileSize < b.fileSize;
        case 4:  // 时间 新→旧
            return m_mtimeCache.value(a.filePath) > m_mtimeCache.value(b.filePath);
        case 5:  // 时间 旧→新
            return m_mtimeCache.value(a.filePath) < m_mtimeCache.value(b.filePath);
        default: // 名称 A→Z
            return QString::compare(displayName(a), displayName(b), Qt::CaseInsensitive) < 0;
        }
    });
}

/* 排序变化：重排列表并整体重建卡片（索引随顺序变化，须整体重建） */
void InstanceModsPage::applySortOrder()
{
    if (m_updateInProgress)
        return;   // 更新队列持有索引，更新过程中不重排
    if (m_modList.mods.isEmpty())
        return;

    ++m_batchGeneration;   // 作废未完成的分批创建回调，避免新旧顺序混排
    sortFileList();

    // 卡片数据与列表同步（rebuildCards 以 m_cardInfos 为准）
    m_cardInfos = m_modList.mods;
    rebuildCards();

    // rebuildCards → clearCards 已清空索引映射，按新顺序重建
    m_modIndexMap.clear();
    m_sha1ToIndex.clear();
    for (int i = 0; i < m_modList.mods.size(); ++i)
    {
        const ModInfo &info = m_modList.mods.at(i);
        const QString key = info.id.isEmpty() ? info.fileName : info.id;
        m_modIndexMap.insert(key, i);
        if (!info.sha1Hash.isEmpty())
            m_sha1ToIndex.insert(info.sha1Hash, i);
    }

    // 与分批创建完成分支保持同一顺序：更新芯片 → 分类芯片 → 过滤统计
    markCachedUpdateChips();
    refreshCardCategoryChips();
    applyFilterAndSearch();
}

void InstanceModsPage::onScanCompleted(const LocalModList &result)
{
    m_isLoading = false;
    showLoading(false);
    m_modList = result;

    // 构建修改时间缓存（ModScanner 不填 dateModified），时间筛选/排序共用；
    // 启用/禁用切换会重扫，此处重建即完成缓存更新
    m_mtimeCache.clear();
    m_mtimeCache.reserve(result.mods.size());
    for (const ModInfo &info : result.mods)
        m_mtimeCache.insert(info.filePath, QFileInfo(info.filePath).lastModified());

    // 按当前排序模式重排（默认名称 A→Z；须在建立索引映射之前完成）
    sortFileList();

    clearCards();
    m_modIndexMap.clear();
    m_sha1ToIndex.clear();

    // 先建立索引映射（不创建卡片），避免耗时操作阻塞 UI
    // 注意遍历 m_modList.mods（已按当前排序重排），保证映射与卡片顺序一致
    for (int i = 0; i < m_modList.mods.size(); ++i)
    {
        const ModInfo &info = m_modList.mods.at(i);

        // 建立 mod 标识到索引的映射
        QString key = info.id.isEmpty() ? info.fileName : info.id;
        m_modIndexMap.insert(key, i);

        // 建立 SHA-1 到索引的映射（更新检查用）
        if (!info.sha1Hash.isEmpty())
            m_sha1ToIndex.insert(info.sha1Hash, i);
    }

    m_statsLabel->setText(tr("共 %1 个模组 (%2 启用, %3 禁用)")
        .arg(result.totalCount)
        .arg(result.enabledCount)
        .arg(result.disabledCount));

    // 异步获取中文名和英文名
    if (result.totalCount > 0)
    {
        m_nameFetcher->fetchNamesBatch(m_modList.mods);
    }

    if (result.totalCount == 0)
    {
        showEmptyHint(true);
        return;
    }

    // 分批创建卡片，避免资源过多时一次性创建导致闪退
    ++m_batchGeneration;
    m_batchIndex = 0;
    int gen = m_batchGeneration;
    QTimer::singleShot(0, this, [this, gen]() {
        if (gen == m_batchGeneration)
            createNextBatch();
    });
}

void InstanceModsPage::createNextBatch()
{
    static const int BATCH_SIZE = 30;
    int start = m_batchIndex;
    int end = qMin(start + BATCH_SIZE, m_modList.mods.size());

    for (int i = start; i < end; ++i)
    {
        const ModInfo &info = m_modList.mods.at(i);
        addCard(info);
    }

    m_batchIndex = end;

    if (m_batchIndex < m_modList.mods.size())
    {
        // 还有剩余，继续下一批（yield to event loop）
        int gen = m_batchGeneration;
        QTimer::singleShot(0, this, [this, gen]() {
            if (gen == m_batchGeneration)
                createNextBatch();
        });
    }
    else
    {
        // 全部创建完毕，一次性排布并标记更新
        markCachedUpdateChips();
        placeCards();
        refreshCardCategoryChips();
        applyFilterAndSearch();
    }
}

void InstanceModsPage::onScanFailed(const QString &error)
{
    m_isLoading = false;
    showLoading(false);
    m_loadingOverlay->showError(tr("扫描失败: %1").arg(error));
}

void InstanceModsPage::addListCard(const ModInfo &info)
{
    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);

    // 存储 ModInfo 到卡片属性，供点击事件使用
    card->setProperty("modId", info.id.isEmpty() ? info.fileName : info.id);
    card->setProperty("modFilePath", info.filePath);
    card->setProperty("modFileName", info.fileName);
    card->setProperty("modEnabled", info.enabled);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    card->installEventFilter(this);
    connect(card, &QWidget::customContextMenuRequested, this, &InstanceModsPage::onModContextMenu);

    QHBoxLayout *mainLayout = new QHBoxLayout(card);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    // 图标 - 从本地 jar 文件提取，失败则回退到类型 SVG 图标
    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    {
        // .disabled 后缀需截掉以定位真实 jar
        QString realPath = info.filePath;
        if (realPath.endsWith(".disabled", Qt::CaseInsensitive))
            realPath.chop(9);
        QString cacheKey = QString("rp_icon:%1").arg(realPath);
        QPixmap icon;
        if (!QPixmapCache::find(cacheKey, &icon))
        {
            icon = ResourcesPage::extractModIcon(realPath);
            if (!icon.isNull())
                QPixmapCache::insert(cacheKey, icon);
        }
        if (!icon.isNull())
        {
            iconLabel->setPixmap(icon.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        else
        {
            QColor tc(ThemeManager::instance()->currentThemeColor());
            iconLabel->setPixmap(IconHelper::loadColoredIcon(":/Images/Icons/nav_mods.svg", tc, 36).pixmap(36, 36));
        }
    }
    mainLayout->addWidget(iconLabel);

    // 信息区域
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    // ---- 名称区域：有中文翻译时合并为「中文翻译（英文）」单行 ----
    // 不再单独显示灰色英文副标题：副标题会撑高固定高度卡片，挤压下方 chip 使其圆角失效
    QString titleText;

    if (!info.chineseName.isEmpty())
    {
        titleText = info.chineseName;
        if (!info.englishName.isEmpty() && info.englishName != info.chineseName)
            titleText += QStringLiteral("（") + info.englishName + QStringLiteral("）");
    }
    else if (!info.englishName.isEmpty())
    {
        titleText = info.englishName;
    }
    else
    {
        titleText = info.name.isEmpty() ? info.fileName : info.name;
    }
    // 禁用项显示名去掉 .disabled 后缀（仅影响显示，不动 fileName 属性）
    if (titleText.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
        titleText.chop(9);

    QLabel *nameLabel = new QLabel(titleText);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    infoLayout->addWidget(nameLabel);

    // 标签行
    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    if (!info.latestVersion.isEmpty())
    {
        QLabel *verChip = new QLabel(info.latestVersion);
        verChip->setObjectName("modChip");
        verChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(verChip);
    }
    if (!info.loaderType.isEmpty())
    {
        QLabel *loaderChip = new QLabel(info.loaderType);
        loaderChip->setObjectName("modChip");
        loaderChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(loaderChip);
    }
    // 作者标签（多作者取首位，过长截断）
    if (!info.author.isEmpty())
    {
        QString authorText = info.author;
        int commaPos = authorText.indexOf(',');
        if (commaPos > 0)
            authorText = authorText.left(commaPos).trimmed() + tr(" 等");
        if (authorText.length() > 12)
            authorText = authorText.left(12) + "…";
        QLabel *authorChip = new QLabel(authorText);
        authorChip->setObjectName("modChip");
        authorChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(authorChip);
    }
    // 文件大小标签
    if (info.fileSize > 0)
    {
        QString sizeText;
        double kb = info.fileSize / 1024.0;
        double mb = kb / 1024.0;
        if (mb >= 1.0)
            sizeText = QString::number(mb, 'f', 1) + " MB";
        else if (kb >= 1.0)
            sizeText = QString::number(kb, 'f', 0) + " KB";
        else
            sizeText = QString::number(info.fileSize) + " B";
        QLabel *sizeChip = new QLabel(sizeText);
        sizeChip->setObjectName("modChip");
        sizeChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(sizeChip);
    }
    // 已禁用芯片（灰底，样式同资源中心本地管理）
    if (!info.enabled)
    {
        QLabel *disabledChip = new QLabel(tr("已禁用"));
        disabledChip->setObjectName("modChip");
        disabledChip->setProperty("cardRole", "chip");
        disabledChip->setStyleSheet(
            "color: #9E9E9E; background-color: rgba(158, 158, 158, 40);"
            "border-radius: 9px; padding: 2px 8px; font-size: 11px;");
        chipsLayout->addWidget(disabledChip);
    }
    // 分类标签（手动分类，默认隐藏，样式见 style.qss categoryChip="true"）
    QLabel *categoryChip = new QLabel();
    categoryChip->setObjectName("modChip");
    categoryChip->setProperty("categoryChip", true);
    categoryChip->hide();
    chipsLayout->addWidget(categoryChip);
    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);

    // 更新可用标签（默认隐藏，检测到更新时显示）
    QLabel *updateChip = new QLabel(tr("有更新"));
    updateChip->setObjectName("modUpdateChip");
    updateChip->setProperty("cardRole", "chip");
    updateChip->setStyleSheet(
        "background-color: #FF9800; color: white; "
        "border-radius: 4px; padding: 2px 8px; font-size: 9pt; font-weight: bold;");
    updateChip->hide();
    chipsLayout->addWidget(updateChip);
    mainLayout->addLayout(infoLayout, 1);

    // 操作按钮
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

    ModInfo capturedInfo = info;

    // 详情按钮
    QPushButton *detailBtn = createBtn(":/Images/Icons/list.svg", tr("查看详情"));
    connect(detailBtn, &QPushButton::clicked, this, [this, capturedInfo]() {
        emit modDetailRequested(capturedInfo);
    });
    btnLayout->addWidget(detailBtn);

    // 问问AI 按钮：跳转 AI 助手并引用该本地模组
    QPushButton *askAiBtn = createBtn(":/Images/Icons/nav_ai.svg", tr("问问AI"));
    connect(askAiBtn, &QPushButton::clicked, this, [this, capturedInfo]() {
        QString displayName = capturedInfo.chineseName.isEmpty()
            ? (capturedInfo.englishName.isEmpty() ? capturedInfo.name : capturedInfo.englishName)
            : capturedInfo.chineseName;
        if (displayName.isEmpty())
            displayName = capturedInfo.fileName;
        ResourceReference ref;
        ref.category = tr("本地模组");
        ref.name = displayName;
        ref.path = capturedInfo.filePath;
        ref.size = capturedInfo.fileSize;
        emit askAiRequested(ref);
    });
    btnLayout->addWidget(askAiBtn);

    // 开关按钮 — 以不同图标和颜色区分启用/禁用状态
    QPushButton *toggleBtn = createBtn(
        info.enabled ? ":/Images/Icons/toggle_on.svg" : ":/Images/Icons/toggle_off.svg",
        info.enabled ? tr("禁用") : tr("启用"));
    {
        QColor toggleColor = info.enabled ? QColor("#4CAF50") : QColor("#9E9E9E");
        toggleBtn->setIcon(IconHelper::loadColoredIcon(
            info.enabled ? ":/Images/Icons/toggle_on.svg" : ":/Images/Icons/toggle_off.svg",
            toggleColor, 18));
    }
    connect(toggleBtn, &QPushButton::clicked, this, [this, capturedInfo]() {
        onToggleMod(capturedInfo);
    });
    btnLayout->addWidget(toggleBtn);

    // 打开文件位置按钮
    QPushButton *folderBtn = createBtn(":/Images/Icons/folder.svg", tr("打开文件位置"));
    connect(folderBtn, &QPushButton::clicked, this, [capturedInfo]() {
        QFileInfo fileInfo(capturedInfo.filePath);
        QString dirPath = fileInfo.absolutePath();
        if (!dirPath.isEmpty())
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
        }
    });
    btnLayout->addWidget(folderBtn);

    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"));
    deleteBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    connect(deleteBtn, &QPushButton::clicked, this, [this, capturedInfo]() {
        onDeleteMod(capturedInfo);
    });
    btnLayout->addWidget(deleteBtn);

    mainLayout->addLayout(btnLayout);

    createSelectionOverlay(card);

    m_cardWidgets.append(card);
}

void InstanceModsPage::addCard(const ModInfo &info)
{
    m_cardInfos.append(info);
    if (m_viewMode == ContentViewSwitch::List)
        addListCard(info);
    else
        addMasonryCard(info);
}

/* 瀑布流卡片构建（小卡片展开态与瀑布流共用；parent 为挂载图层） */
QWidget *InstanceModsPage::createMasonryCardWidget(const ModInfo &info, QWidget *parent)
{
    QList<MasonryContentCard::ActionSpec> actions;
    ModInfo capturedInfo = info;

    actions.append({ QStringLiteral(":/Images/Icons/list.svg"), tr("查看详情"), QColor(),
                     [this, capturedInfo]() { emit modDetailRequested(capturedInfo); } });

    actions.append({ QStringLiteral(":/Images/Icons/nav_ai.svg"), tr("问问AI"), QColor(),
                     [this, capturedInfo]() {
                         QString displayName = capturedInfo.chineseName.isEmpty()
                             ? (capturedInfo.englishName.isEmpty() ? capturedInfo.name : capturedInfo.englishName)
                             : capturedInfo.chineseName;
                         if (displayName.isEmpty())
                             displayName = capturedInfo.fileName;
                         ResourceReference ref;
                         ref.category = tr("本地模组");
                         ref.name = displayName;
                         ref.path = capturedInfo.filePath;
                         ref.size = capturedInfo.fileSize;
                         emit askAiRequested(ref);
                     } });

    actions.append({ info.enabled ? QStringLiteral(":/Images/Icons/toggle_on.svg")
                                  : QStringLiteral(":/Images/Icons/toggle_off.svg"),
                     info.enabled ? tr("禁用") : tr("启用"),
                     info.enabled ? QColor("#4CAF50") : QColor("#9E9E9E"),
                     [this, capturedInfo]() { onToggleMod(capturedInfo); } });

    actions.append({ QStringLiteral(":/Images/Icons/folder.svg"), tr("打开文件位置"), QColor(),
                     [capturedInfo]() {
                         QFileInfo fileInfo(capturedInfo.filePath);
                         QString dirPath = fileInfo.absolutePath();
                         if (!dirPath.isEmpty())
                             QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
                     } });

    actions.append({ QStringLiteral(":/Images/Icons/delete.svg"), tr("删除"), QColor("#F44336"),
                     [this, capturedInfo]() { onDeleteMod(capturedInfo); } });

    // 显示用副本：禁用项名称去掉 .disabled 后缀（仅影响卡片显示，
    // 动作回调与卡片属性仍使用原始 info）
    ModInfo displayInfo = info;
    if (displayInfo.name.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
        displayInfo.name.chop(9);
    QWidget *card = MasonryContentCard::build(displayInfo, parent, actions);

    // 兼容既有逻辑的属性与事件（contextMenu / eventFilter / 选中高亮）
    card->setProperty("modId", info.id.isEmpty() ? info.fileName : info.id);
    card->setProperty("modFilePath", info.filePath);
    card->setProperty("modFileName", info.fileName);
    card->setProperty("modEnabled", info.enabled);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    card->installEventFilter(this);
    connect(card, &QWidget::customContextMenuRequested, this, &InstanceModsPage::onModContextMenu);

    // 隐藏的更新芯片（markCardHasUpdate 通过 findChild("modUpdateChip") 定位）
    QLabel *updateChip = new QLabel(tr("有更新"), card);
    updateChip->setObjectName("modUpdateChip");
    updateChip->setStyleSheet(
        "background-color: #FF9800; color: white; "
        "border-radius: 4px; padding: 2px 8px; font-size: 9pt; font-weight: bold;");
    updateChip->hide();

    // 分类标签（手动分类，追加到 chips 行末尾，样式见 style.qss categoryChip="true"）
    if (QWidget *chipsWidget = card->findChild<QWidget *>("contentCardChips"))
    {
        if (QHBoxLayout *cl = qobject_cast<QHBoxLayout *>(chipsWidget->layout()))
        {
            // 已禁用芯片（灰底，样式同资源中心本地管理）
            if (!info.enabled)
            {
                QLabel *disabledChip = new QLabel(tr("已禁用"));
                disabledChip->setObjectName("modChip");
                disabledChip->setProperty("cardRole", "chip");
                disabledChip->setStyleSheet(
                    "color: #9E9E9E; background-color: rgba(158, 158, 158, 40);"
                    "border-radius: 9px; padding: 2px 8px; font-size: 11px;");
                cl->insertWidget(cl->count() - 1, disabledChip);
            }
            QLabel *categoryChip = new QLabel();
            categoryChip->setObjectName("contentChip");
            categoryChip->setProperty("categoryChip", true);
            categoryChip->hide();
            cl->insertWidget(cl->count() - 1, categoryChip);
        }
    }

    createSelectionOverlay(card);
    return card;
}

/* 瀑布流卡片（复用 MasonryContentCard 组件，保留选中/更新芯片/属性兼容） */
void InstanceModsPage::addMasonryCard(const ModInfo &info)
{
    QWidget *card = createMasonryCardWidget(info, m_cardContainer);
    m_cardWidgets.append(card);
}

/* 视图切换：重建卡片（保留当前列表数据） */
void InstanceModsPage::onViewModeChanged(ContentViewSwitch::ViewMode mode)
{
    m_viewMode = static_cast<int>(mode);
    ContentViewSwitch::savePersisted("instance_mods", mode);
    rebuildCards();
}

void InstanceModsPage::rebuildCards()
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

QWidget *InstanceModsPage::createSelectionOverlay(QWidget *card)
{
    QWidget *overlay = new QWidget(card);
    overlay->setObjectName("cardSelectionOverlay");
    overlay->setGeometry(card->rect());
    overlay->setStyleSheet(selectionOverlayStyle());
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->raise();
    overlay->hide();

    QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(overlay);
    effect->setOpacity(0.0);
    overlay->setGraphicsEffect(effect);

    QPropertyAnimation *anim = new QPropertyAnimation(effect, "opacity", this);
    anim->setDuration(180);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    // 淡出结束后隐藏遮罩（overlay 销毁时自动断开，避免悬空指针）
    connect(anim, &QPropertyAnimation::finished, overlay, [overlay, effect]() {
        if (effect->opacity() <= 0.01)
            overlay->hide();
    });

    m_selectionOverlays.append(overlay);
    m_selectionEffects.append(effect);
    m_selectionAnims.append(anim);
    return overlay;
}

void InstanceModsPage::animateCardHighlight(int idx, bool selected)
{
    if (idx < 0 || idx >= m_selectionOverlays.size())
        return;

    QWidget *overlay = m_selectionOverlays[idx];
    QGraphicsOpacityEffect *effect = m_selectionEffects[idx];
    QPropertyAnimation *anim = m_selectionAnims[idx];

    anim->stop();
    anim->setStartValue(effect->opacity());
    anim->setEndValue(selected ? 1.0 : 0.0);
    if (selected)
        overlay->show();
    anim->start();
}

QString InstanceModsPage::selectionOverlayStyle() const
{
    QColor c = ThemeManager::instance()->currentThemeColor();
    return QString("#cardSelectionOverlay { background-color: rgba(%1, %2, %3, 40); "
                   "border: 2px solid %4; border-radius: 6px; }")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.name());
}

void InstanceModsPage::onToggleMod(const ModInfo &info)
{
    QString oldPath = info.filePath;
    QString newPath;
    if (info.enabled)
    {
        newPath = oldPath + ".disabled";
    }
    else
    {
        if (oldPath.endsWith(".disabled"))
        {
            newPath = oldPath.left(oldPath.length() - 9);
        }
        else
        {
            return;
        }
    }

    if (QFile::rename(oldPath, newPath))
    {
        refreshModList();
    }
}

void InstanceModsPage::onDeleteMod(const ModInfo &info)
{
    QString displayName = info.name.isEmpty() ? info.fileName : info.name;
    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("删除模组"),
        tr("确定要删除模组 \"%1\" 吗？此操作不可撤销。").arg(displayName),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply == AppMessageBox::Yes)
    {
        QFile::remove(info.filePath);
        QString disabledPath = info.filePath + ".disabled";
        if (QFile::exists(disabledPath))
        {
            QFile::remove(disabledPath);
        }
        refreshModList();
    }
}

void InstanceModsPage::onModContextMenu(const QPoint &pos)
{
    QWidget *card = qobject_cast<QWidget *>(sender());
    if (!card)
    {
        return;
    }

    QString filePath = card->property("modFilePath").toString();
    QString fileName = card->property("modFileName").toString();
    bool enabled = card->property("modEnabled").toBool();

    if (filePath.isEmpty())
    {
        return;
    }

    QMenu menu(this);

    QAction *toggleAction = menu.addAction(enabled ? tr("禁用模组") : tr("启用模组"));
    menu.addSeparator();

    // ── 设置分类子菜单 ──
    QMenu *categoryMenu = menu.addMenu(tr("设置分类"));
    {
        const QString currentCat = m_categoryManager->categoryOf(fileName);
        const QList<LocalResourceCategory> cats = m_categoryManager->categories();
        for (const LocalResourceCategory &c : cats)
        {
            QAction *action = categoryMenu->addAction(c.name);
            action->setCheckable(true);
            action->setChecked(currentCat == c.id);
            QObject::connect(action, &QAction::triggered, this,
                [this, filePath, categoryId = c.id]() {
                    m_categoryManager->assignCategory(filePath, categoryId);
                });
        }
        if (cats.isEmpty())
        {
            QAction *emptyAction = categoryMenu->addAction(tr("暂无分类"));
            emptyAction->setEnabled(false);
        }
        QAction *clearAction = categoryMenu->addAction(tr("清除分类"));
        clearAction->setEnabled(!currentCat.isEmpty());
        QObject::connect(clearAction, &QAction::triggered, this,
            [this, filePath]() {
                m_categoryManager->assignCategory(filePath, QString());
            });
        categoryMenu->addSeparator();
        QAction *newCatAction = categoryMenu->addAction(tr("新建分类…"));
        QObject::connect(newCatAction, &QAction::triggered, this,
            [this, filePath]() {
                FavoriteFolderDialog dialog(FavoriteFolderDialog::CreateMode, this);
                dialog.setWindowTitle(tr("新建分类"));
                dialog.setPresets(LocalCategoryManager::suggestedNames(m_categoryManager->resourceType()));
                if (dialog.exec() != QDialog::Accepted)
                    return;
                const QString name = dialog.folderName();
                if (name.isEmpty())
                    return;
                const QString newId = m_categoryManager->addCategory(name);
                m_categoryManager->assignCategory(filePath, newId);
            });
    }

    QAction *detailAction = menu.addAction(tr("查看详情"));
    QAction *openFolderAction = menu.addAction(tr("打开文件位置"));
    QAction *copyPathAction = menu.addAction(tr("复制文件路径"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(tr("删除模组"));

    QAction *chosen = menu.exec(card->mapToGlobal(pos));

    if (chosen == toggleAction)
    {
        ModInfo info;
        info.filePath = filePath;
        info.enabled = enabled;
        onToggleMod(info);
    }
    else if (chosen == detailAction)
    {
        // 从 modIndexMap 中找到对应的 mod 并发出详情信号
        QString modId = card->property("modId").toString();
        if (m_modIndexMap.contains(modId))
        {
            int idx = m_modIndexMap.value(modId);
            if (idx >= 0 && idx < m_modList.mods.size())
            {
                emit modDetailRequested(m_modList.mods[idx]);
            }
        }
    }
    else if (chosen == openFolderAction)
    {
        QFileInfo fi(filePath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    }
    else if (chosen == copyPathAction)
    {
        QApplication::clipboard()->setText(filePath);
    }
    else if (chosen == deleteAction)
    {
        QString displayName = fileName.isEmpty() ? QFileInfo(filePath).fileName() : fileName;
        AppMessageBox::StandardButton reply = AppMessageBox::question(
            this,
            tr("删除模组"),
            tr("确定要删除模组 \"%1\" 吗？此操作不可撤销。").arg(displayName),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);

        if (reply == AppMessageBox::Yes)
        {
            QFile::remove(filePath);
            QString disabledPath = filePath + ".disabled";
            if (QFile::exists(disabledPath))
            {
                QFile::remove(disabledPath);
            }
            refreshModList();
        }
    }
}

void InstanceModsPage::onModNameResolved(const ModInfo &info)
{
    // 在 modList 中找到对应的索引
    QString key = info.id.isEmpty() ? info.fileName : info.id;
    if (!m_modIndexMap.contains(key))
        return;

    int idx = m_modIndexMap.value(key);
    if (idx < 0 || idx >= m_modList.mods.size())
        return;

    // 更新内存中的 mod 数据
    ModInfo &target = m_modList.mods[idx];
    if (!info.chineseName.isEmpty())
        target.chineseName = info.chineseName;
    if (!info.englishName.isEmpty())
        target.englishName = info.englishName;
    if (!info.mcmodUrl.isEmpty())
        target.mcmodUrl = info.mcmodUrl;

    // 更新卡片显示
    updateCardNames(idx, target);
}

void InstanceModsPage::onModNameFetchFailed(const QString &modId, const QString &englishName)
{
    Q_UNUSED(englishName);
    // 查找对应的 mod，将其标记为已尝试过
    QString key = modId;
    if (!m_modIndexMap.contains(key))
        return;

    int idx = m_modIndexMap.value(key);
    if (idx < 0 || idx >= m_modList.mods.size())
        return;
}

void InstanceModsPage::onCheckUpdatesClicked()
{
    if (m_modList.mods.isEmpty())
        return;

    resetUpdateState();

    // 若选中了模组，只检查选中的模组
    QList<int> checkIndices;
    if (!m_selectedModIndices.isEmpty())
    {
        for (int idx : m_selectedModIndices)
        {
            if (idx >= 0 && idx < m_modList.mods.size())
            {
                const ModInfo &mod = m_modList.mods.at(idx);
                if (!mod.sha1Hash.isEmpty() && mod.enabled)
                    checkIndices.append(idx);
            }
        }
    }
    else
    {
        // 构建待检查列表：只检查有 SHA-1 哈希且已启用的模组
        for (int i = 0; i < m_modList.mods.size(); ++i)
        {
            const ModInfo &mod = m_modList.mods.at(i);
            if (!mod.sha1Hash.isEmpty() && mod.enabled)
                checkIndices.append(i);
        }
    }

    m_updateCheckTotal = checkIndices.size();
    if (m_updateCheckTotal == 0)
    {
        m_statsLabel->setText(tr("没有可检查更新的模组"));
        return;
    }

    m_bottomCheckUpdateBtn->setEnabled(false);
    m_bottomCheckUpdateBtn->setText(tr("检查中..."));
    m_statsLabel->setText(tr("正在检查更新..."));

    detectInstanceMeta();

    // 收集哈希并去重排序（缓存命中判断用）
    QList<QString> sha1s;
    QSet<QString> seen;
    for (int idx : checkIndices)
    {
        const QString &h = m_modList.mods.at(idx).sha1Hash;
        if (!h.isEmpty() && !seen.contains(h))
        {
            seen.insert(h);
            sha1s.append(h);
        }
    }
    sha1s.sort();

    // 命中缓存：同一组哈希在 6 小时内已检查过，直接复用结果
    if (m_selectedModIndices.isEmpty()
        && sha1s == m_updateCacheKeys
        && m_updateCacheTime.isValid()
        && m_updateCacheTime.secsTo(QDateTime::currentDateTime()) < 6 * 3600)
    {
        applyCachedUpdates();
        return;
    }

    m_updateCacheKeys = sha1s;
    m_updateCacheTime = QDateTime();
    m_updateCache.clear();
    updateBottomBarState();

    // 第一步：批量哈希匹配，得到本地文件对应的已安装版本（一次请求）
    m_updateCheckApi->matchHashes(sha1s);
}

void InstanceModsPage::onHashesMatched(const QMap<QString, ModVersionFile> &versions)
{
    m_installedBySha1 = versions;

    QStringList gameVersions;
    if (!m_gameVersion.isEmpty())
        gameVersions << m_gameVersion;
    QStringList loaders;
    if (!m_loaderType.isEmpty())
        loaders << m_loaderType;

    // 第二步：批量请求满足当前实例 MC 版本和加载器的最新版本（一次请求）
    m_updateCheckApi->checkModUpdates(m_updateCacheKeys, gameVersions, loaders);
}

void InstanceModsPage::onUpdatesChecked(const QMap<QString, ModVersionFile> &versions)
{
    for (auto it = versions.constBegin(); it != versions.constEnd(); ++it)
    {
        const QString &sha1 = it.key();

        // 必须能在本地已安装版本中找到，否则跳过
        if (!m_installedBySha1.contains(sha1))
            continue;

        const ModVersionFile &latest = it.value();
        const ModVersionFile installed = m_installedBySha1.value(sha1);

        // 更新判断（参考 PCL-CE/HMCL）：最新版发布日期更晚 且 文件哈希不同
        bool hasUpdate = false;
        if (latest.sha1.isEmpty() || installed.sha1.isEmpty())
        {
            hasUpdate = latest.sha1 != installed.sha1;
        }
        else if (latest.sha1 != installed.sha1
                 && installed.datePublished.isValid()
                 && latest.datePublished > installed.datePublished)
        {
            hasUpdate = true;
        }

        if (hasUpdate)
        {
            m_updateCache.insert(sha1, latest);
            if (m_sha1ToIndex.contains(sha1))
                markCardHasUpdate(m_sha1ToIndex.value(sha1), latest);
        }
    }

    m_updateCheckFound = m_updateCache.size();
    m_updateCacheTime = QDateTime::currentDateTime();

    m_bottomCheckUpdateBtn->setEnabled(true);
    m_bottomCheckUpdateBtn->setText(tr("检查更新"));

    if (m_updateCheckFound > 0)
    {
        m_statsLabel->setText(tr("检查完成，发现 %1 个模组有更新").arg(m_updateCheckFound));
    }
    else
    {
        m_statsLabel->setText(tr("所有模组已是最新版本"));
    }

    updateBottomBarState();
}

void InstanceModsPage::onUpdateCheckBatchFailed(const QString &error)
{
    Q_UNUSED(error);
    m_updateCacheKeys.clear();
    m_updateCache.clear();
    m_bottomCheckUpdateBtn->setEnabled(true);
    m_bottomCheckUpdateBtn->setText(tr("检查更新"));
    m_statsLabel->setText(tr("检查更新失败"));

    updateBottomBarState();
}

void InstanceModsPage::resetUpdateState()
{
    // 清除之前标记的更新芯片
    for (QWidget *card : m_cardWidgets)
    {
        QLabel *chip = card->findChild<QLabel *>("modUpdateChip");
        if (chip)
            chip->hide();
    }
    m_installedBySha1.clear();
    m_updateCheckTotal = 0;
    m_updateCheckFound = 0;
}

void InstanceModsPage::markCardHasUpdate(int index, const ModVersionFile &target)
{
    if (index < 0 || index >= m_cardWidgets.size())
        return;

    QWidget *card = m_cardWidgets[index];
    QLabel *chip = card->findChild<QLabel *>("modUpdateChip");
    if (!chip)
        return;

    chip->show();
    if (index < m_modList.mods.size())
    {
        const ModInfo &mod = m_modList.mods.at(index);
        QString targetVersion = target.version.isEmpty() ? target.fileName : target.version;
        if (mod.latestVersion.isEmpty())
            chip->setToolTip(tr("可更新到 %1").arg(targetVersion));
        else
            chip->setToolTip(tr("可更新到 %1（当前 %2）").arg(targetVersion, mod.latestVersion));
    }
}

void InstanceModsPage::applyCachedUpdates()
{
    m_updateCheckFound = m_updateCache.size();

    for (auto it = m_updateCache.constBegin(); it != m_updateCache.constEnd(); ++it)
    {
        if (m_sha1ToIndex.contains(it.key()))
            markCardHasUpdate(m_sha1ToIndex.value(it.key()), it.value());
    }

    m_bottomCheckUpdateBtn->setEnabled(true);
    m_bottomCheckUpdateBtn->setText(tr("检查更新"));

    if (m_updateCheckFound > 0)
        m_statsLabel->setText(tr("检查完成，发现 %1 个模组有更新").arg(m_updateCheckFound));
    else
        m_statsLabel->setText(tr("所有模组已是最新版本"));

    updateBottomBarState();
}

void InstanceModsPage::detectInstanceMeta()
{
    m_gameVersion.clear();
    m_loaderType.clear();
    if (m_instancePath.isEmpty())
        return;

    QFileInfo fi(m_instancePath);
    QString instanceName = fi.fileName();
    QString jsonPath = m_instancePath + "/" + instanceName + ".json";
    if (!QFile::exists(jsonPath))
        jsonPath = m_instancePath + "/version.json";

    QFile versionJsonFile(jsonPath);
    if (!versionJsonFile.open(QIODevice::ReadOnly))
        return;

    QByteArray jsonData = versionJsonFile.readAll();
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return;
    QJsonObject rootObj = jsonDoc.object();

    if (rootObj.contains("inheritsFrom"))
        m_gameVersion = rootObj["inheritsFrom"].toString();
    else if (rootObj.contains("clientVersion"))
        m_gameVersion = rootObj["clientVersion"].toString();
    else if (rootObj.contains("id"))
        m_gameVersion = rootObj["id"].toString();

    // 剥离加载器后缀（如 "1.20.1-forge-47.2.0" → "1.20.1"）
    if (m_gameVersion.contains('-'))
    {
        QString lower = m_gameVersion.toLower();
        if (lower.contains("forge") || lower.contains("fabric")
            || lower.contains("quilt") || lower.contains("neoforge")
            || lower.contains("optifine"))
        {
            m_gameVersion = m_gameVersion.left(m_gameVersion.indexOf('-'));
        }
    }

    auto detectLoader = [](const QString &text) -> QString {
        QString lower = text.toLower();
        if (lower.contains("neoforge")) return "NeoForge";
        if (lower.contains("forge")) return "Forge";
        if (lower.contains("fabric")) return "Fabric";
        if (lower.contains("quilt")) return "Quilt";
        return QString();
    };

    for (auto it = rootObj.constBegin(); it != rootObj.constEnd(); ++it)
    {
        m_loaderType = detectLoader(it.key());
        if (!m_loaderType.isEmpty()) break;
        QJsonValue val = it.value();
        if (val.isString())
        {
            m_loaderType = detectLoader(val.toString());
            if (!m_loaderType.isEmpty()) break;
        }
    }
    if (m_loaderType.isEmpty())
        m_loaderType = detectLoader(rootObj["inheritsFrom"].toString());
    if (m_loaderType.isEmpty())
        m_loaderType = detectLoader(rootObj["id"].toString());
}

void InstanceModsPage::onSelectAllClicked()
{
    m_allSelected = !m_allSelected;
    m_selectAllBtn->setText(m_allSelected ? tr("取消全选") : tr("全选"));

    m_selectedModIndices.clear();

    if (m_allSelected)
    {
        // 将所有可见卡片加入多选集合
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            if (m_cardWidgets[i]->isVisible())
            {
                m_selectedModIndices.insert(i);
                animateCardHighlight(i, true);
            }
        }
    }
    else
    {
        // 取消全选，清除所有样式
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            animateCardHighlight(i, false);
        }
    }

    updateBottomBarState();
}

void InstanceModsPage::onOpenFolderClicked()
{
    if (m_instancePath.isEmpty())
        return;

    // 若选中了模组，定位到第一个选中模组文件所在目录
    if (!m_selectedModIndices.isEmpty())
    {
        int firstIdx = *m_selectedModIndices.begin();
        if (firstIdx >= 0 && firstIdx < m_modList.mods.size())
        {
            const ModInfo &mod = m_modList.mods.at(firstIdx);
            QFileInfo fi(mod.filePath);
            if (fi.exists())
            {
                QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
                return;
            }
        }
    }

    // 否则打开模组目录
    QString modsDir = m_instancePath + "/mods";
    QDir dir(modsDir);
    if (dir.exists())
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
    }
    else
    {
        AppMessageBox::information(this, tr("提示"), tr("模组目录不存在: %1").arg(modsDir));
    }
}

void InstanceModsPage::onPasteModClicked()
{
    if (m_instancePath.isEmpty())
        return;

    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    if (!mimeData->hasUrls())
    {
        AppMessageBox::information(this, tr("粘贴模组"), tr("剪贴板中没有文件，请先复制模组文件。"));
        return;
    }

    QString modsDir = m_instancePath + "/mods";
    QDir dir(modsDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    int copiedCount = 0;
    QStringList validSuffixes = { ".jar", ".disabled" };

    for (const QUrl &url : mimeData->urls())
    {
        QString srcPath = url.toLocalFile();
        if (srcPath.isEmpty())
            continue;

        QFileInfo fi(srcPath);
        bool isValid = false;
        for (const QString &suf : validSuffixes)
        {
            if (fi.fileName().endsWith(suf, Qt::CaseInsensitive))
            {
                isValid = true;
                break;
            }
        }
        if (!isValid)
            continue;

        QString destPath = modsDir + "/" + fi.fileName();
        if (QFile::exists(destPath))
        {
            int ret = AppMessageBox::question(this, tr("文件已存在"),
                tr("文件 \"%1\" 已存在，是否覆盖？").arg(fi.fileName()),
                AppMessageBox::Yes | AppMessageBox::No | AppMessageBox::Cancel);
            if (ret == AppMessageBox::Cancel)
                break;
            if (ret == AppMessageBox::No)
                continue;
            QFile::remove(destPath);
        }

        if (QFile::copy(srcPath, destPath))
        {
            copiedCount++;
        }
        else
        {
            AppMessageBox::warning(this, tr("粘贴失败"),
                tr("无法复制文件 \"%1\"").arg(fi.fileName()));
        }
    }

    if (copiedCount > 0)
    {
        m_statsLabel->setText(tr("已粘贴 %1 个模组文件，正在刷新列表...").arg(copiedCount));
        refreshModList();
    }
}

void InstanceModsPage::onCopyModClicked()
{
    if (m_selectedModIndices.isEmpty())
        return;

    QList<QUrl> urls;
    QStringList names;
    for (int idx : m_selectedModIndices)
    {
        if (idx < 0 || idx >= m_modList.mods.size())
            continue;

        const ModInfo &mod = m_modList.mods.at(idx);
        QFileInfo fi(mod.filePath);
        if (fi.exists())
        {
            urls.append(QUrl::fromLocalFile(fi.absoluteFilePath()));
            names.append(fi.fileName());
        }
    }

    if (urls.isEmpty())
    {
        AppMessageBox::warning(this, tr("复制失败"), tr("没有可复制的文件"));
        return;
    }

    QMimeData *mimeData = new QMimeData();
    mimeData->setUrls(urls);

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setMimeData(mimeData);

    m_statsLabel->setText(tr("已复制 %1 个模组").arg(urls.size()));
}

void InstanceModsPage::onToggleSelectedClicked()
{
    if (m_selectedModIndices.isEmpty())
        return;

    for (int idx : m_selectedModIndices)
    {
        if (idx < 0 || idx >= m_modList.mods.size())
            continue;
        onToggleMod(m_modList.mods.at(idx));
    }
}

void InstanceModsPage::onDownloadSearchClicked()
{
    emit modDownloadSearchRequested(m_currentSearch);
}

void InstanceModsPage::updateCardNames(int index, const ModInfo &info)
{
    if (index < 0 || index >= m_cardWidgets.size())
        return;

    QWidget *card = m_cardWidgets[index];
    if (!card)
        return;

    // 查找卡片中的名称标签
    QLabel *nameLabel = card->findChild<QLabel *>("modCardName");
    if (!nameLabel)
        return;

    // 标题：有中文名时按「中文（英文）」合并，否则英文名/原始名
    QString titleText;

    if (!info.chineseName.isEmpty())
    {
        titleText = info.chineseName;
        if (!info.englishName.isEmpty() && info.englishName != info.chineseName)
            titleText += QStringLiteral("（") + info.englishName + QStringLiteral("）");
    }
    else if (!info.englishName.isEmpty())
    {
        titleText = info.englishName;
    }
    else
    {
        titleText = info.name.isEmpty() ? info.fileName : info.name;
    }
    // 禁用项显示名去掉 .disabled 后缀（仅影响显示，不动 fileName 属性）
    if (titleText.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
        titleText.chop(9);

    nameLabel->setText(titleText);
}

void InstanceModsPage::refreshCardNameLabels()
{
    for (int i = 0; i < m_cardWidgets.size() && i < m_modList.mods.size(); ++i)
    {
        updateCardNames(i, m_modList.mods[i]);
    }
}

bool InstanceModsPage::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *card = qobject_cast<QWidget *>(watched);
    if (!card)
        return QWidget::eventFilter(watched, event);

    QString modId = card->property("modId").toString();
    if (modId.isEmpty() || !m_modIndexMap.contains(modId))
        return QWidget::eventFilter(watched, event);

    int idx = m_modIndexMap.value(modId);
    if (idx < 0 || idx >= m_modList.mods.size())
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonDblClick)
    {
        // 双击打开详情页
        emit modDetailRequested(m_modList.mods[idx]);
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            // 按下左键开始滑动多选：抓取鼠标，拖拽经过的卡片将被快速勾选
            m_dragSelectActive = true;
            m_dragMoved = false;
            m_dragPressPos = mouseEvent->globalPos();
            m_dragAnchorIdx = idx;
            m_dragLastHoverIdx = idx;
            card->grabMouse();
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_dragSelectActive && (mouseEvent->buttons() & Qt::LeftButton))
        {
            // 超过拖拽阈值才视为滑动多选，避免与单击冲突
            if (!m_dragMoved &&
                (mouseEvent->globalPos() - m_dragPressPos).manhattanLength() >= QApplication::startDragDistance())
            {
                m_dragMoved = true;
                selectCardDuringDrag(m_dragAnchorIdx);
            }

            if (m_dragMoved)
            {
                int hoverIdx = cardIndexAtGlobal(mouseEvent->globalPos());
                if (hoverIdx >= 0 && hoverIdx != m_dragLastHoverIdx)
                {
                    if (hoverIdx > m_dragLastHoverIdx)
                    {
                        // 正向滑动：勾选滑过的可见卡片
                        for (int i = m_dragLastHoverIdx + 1; i <= hoverIdx; ++i)
                        {
                            if (m_cardWidgets[i]->isVisible())
                                selectCardDuringDrag(i);
                        }
                    }
                    else
                    {
                        // 反向滑动：取消勾选滑过的可见卡片
                        for (int i = m_dragLastHoverIdx - 1; i >= hoverIdx; --i)
                        {
                            if (m_cardWidgets[i]->isVisible())
                                deselectCardDuringDrag(i);
                        }
                    }
                    m_dragLastHoverIdx = hoverIdx;
                }
            }
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_dragSelectActive && mouseEvent->button() == Qt::LeftButton)
        {
            m_dragSelectActive = false;
            card->releaseMouse();
            if (!m_dragMoved)
            {
                // 未发生拖拽：视为单击，切换该卡片的选中状态
                toggleCardSelection(m_dragAnchorIdx);
            }
            m_dragMoved = false;
            m_dragAnchorIdx = -1;
            m_dragLastHoverIdx = -1;
            return true;
        }
    }

    if (event->type() == QEvent::Resize)
    {
        int ovIdx = m_cardWidgets.indexOf(card);
        if (ovIdx >= 0 && ovIdx < m_selectionOverlays.size())
            m_selectionOverlays[ovIdx]->setGeometry(card->rect());
    }

    return QWidget::eventFilter(watched, event);
}

void InstanceModsPage::clearCards()
{
    for (QWidget *w : m_cardWidgets)
    {
        if (m_cardGridLayout)
            m_cardGridLayout->removeWidget(w);
        if (m_cardFlowLayout)
            m_cardFlowLayout->removeWidget(w);
        w->deleteLater();
    }
    m_cardWidgets.clear();
    m_cardInfos.clear();
    m_modIndexMap.clear();
    m_sha1ToIndex.clear();
    m_installedBySha1.clear();
    m_selectedModIndices.clear();
    m_allSelected = false;
    m_dragSelectActive = false;
    m_dragMoved = false;
    m_dragAnchorIdx = -1;
    m_dragLastHoverIdx = -1;
    // 遮罩随卡片销毁，动画由本页持有，需显式释放
    m_selectionOverlays.clear();
    m_selectionEffects.clear();
    for (QPropertyAnimation *anim : m_selectionAnims)
        anim->deleteLater();
    m_selectionAnims.clear();
    updateBottomBarState();
}

void InstanceModsPage::placeCards()
{
    if (m_viewMode == ContentViewSwitch::List && m_cardGridLayout)
    {
        while (QLayoutItem *item = m_cardGridLayout->takeAt(0))
        {
            delete item;
        }

        int row = 0;
        for (QWidget *card : m_cardWidgets)
        {
            m_cardGridLayout->addWidget(card, row, 0);
            row++;
        }

        if (m_cardWidgets.isEmpty())
        {
            QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
            m_cardGridLayout->addItem(spacer, 0, 0);
        }
        return;
    }

    if (m_cardFlowLayout)
    {
        while (QLayoutItem *item = m_cardFlowLayout->takeAt(0))
        {
            delete item;
        }
        for (QWidget *card : m_cardWidgets)
        {
            m_cardFlowLayout->addWidget(card);
        }
        if (m_cardWidgets.isEmpty())
        {
            QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
            m_cardFlowLayout->addItem(spacer);
        }
    }
}

void InstanceModsPage::showLoading(bool show)
{
    if (show)
    {
        m_loadingOverlay->showOverlay(tr("正在扫描模组..."));
    }
    else
    {
        m_loadingOverlay->hideOverlay();
    }
}

void InstanceModsPage::showEmptyHint(bool show)
{
    m_emptyLabel->setVisible(show);
    // 空态时隐藏列表滚动区，让提示文字占据列表区域
    m_scrollArea->setVisible(!show);
    // 按钮只在搜索无结果时显示，不在"暂无模组"时显示
    if (!show)
        m_downloadSearchBtn->hide();
}

void InstanceModsPage::updateBottomBarState()
{
    bool hasSelection = !m_selectedModIndices.isEmpty();
    m_copyModBtn->setVisible(hasSelection);
    m_toggleSelectedBtn->setVisible(hasSelection);
    m_detailBtn->setVisible(hasSelection);
    m_pasteModBtn->setVisible(!hasSelection);

    // 更新按钮：仅当选中了有更新的模组时显示
    bool hasUpdatable = false;
    if (hasSelection)
    {
        for (int idx : m_selectedModIndices)
        {
            if (idx < 0 || idx >= m_modList.mods.size())
                continue;
            const ModInfo &mod = m_modList.mods.at(idx);
            if (!mod.sha1Hash.isEmpty() && m_updateCache.contains(mod.sha1Hash))
            {
                hasUpdatable = true;
                break;
            }
        }
    }
    m_updateModBtn->setVisible(hasUpdatable);
    m_updateModBtn->setEnabled(!m_updateInProgress);
}

void InstanceModsPage::onUpdateSelectedClicked()
{
    if (m_updateInProgress)
        return;

    m_updateQueue.clear();
    for (int idx : m_selectedModIndices)
    {
        if (idx < 0 || idx >= m_modList.mods.size())
            continue;
        const ModInfo &mod = m_modList.mods.at(idx);
        if (!mod.sha1Hash.isEmpty() && m_updateCache.contains(mod.sha1Hash))
            m_updateQueue.append(idx);
    }

    if (m_updateQueue.isEmpty())
        return;

    m_updateInProgress = true;
    m_updateModBtn->setEnabled(false);
    m_updateModBtn->setText(tr("更新中..."));
    m_statsLabel->setText(tr("正在更新 %1 个模组...").arg(m_updateQueue.size()));
    processNextModUpdate();
}

void InstanceModsPage::processNextModUpdate()
{
    if (m_updateQueue.isEmpty())
    {
        m_updateInProgress = false;
        m_updateModBtn->setEnabled(true);
        m_updateModBtn->setText(tr("更新"));
        m_statsLabel->setText(tr("模组更新完成"));
        refreshModList();
        return;
    }

    int idx = m_updateQueue.takeFirst();
    if (idx < 0 || idx >= m_modList.mods.size())
    {
        processNextModUpdate();
        return;
    }

    const ModInfo mod = m_modList.mods.at(idx);
    if (mod.sha1Hash.isEmpty() || !m_updateCache.contains(mod.sha1Hash))
    {
        processNextModUpdate();
        return;
    }

    const ModVersionFile target = m_updateCache.value(mod.sha1Hash);
    if (target.downloadUrl.isEmpty())
    {
        processNextModUpdate();
        return;
    }

    QString fileName = target.fileName;
    if (fileName.isEmpty())
    {
        QUrl url(target.downloadUrl);
        fileName = url.fileName();
        if (fileName.isEmpty() || fileName.contains('?'))
            fileName = mod.fileName;
    }

    // 注册下载任务
    QString instanceName = QFileInfo(m_instancePath).fileName();
    QString taskId = DownloadTaskManager::instance()->addTask(
        instanceName, m_instancePath, QString(), QStringList());

    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading,
        tr("更新模组: ") + mod.name);
    DownloadTaskManager::instance()->updateTaskCurrentFile(taskId, fileName);
    DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::ClientJar, 0);

    auto *downloader = new ContentDownloader(this);

    connect(downloader, &ContentDownloader::downloadProgress, this,
        [taskId, fileName](qint64 received, qint64 total)
        {
            DownloadTaskManager::instance()->updateTaskFileProgress(taskId, fileName, received, total);
            if (total > 0)
            {
                int pct = qBound(0, static_cast<int>(received * 100 / total), 100);
                DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, pct);
            }
        });

    connect(downloader, &ContentDownloader::downloadFinished, this,
        [this, taskId, downloader, mod, fileName](const QString &destPath)
        {
            DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::Completed, 100);
            DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
            DownloadTaskManager::instance()->updateTaskStatus(
                taskId, DownloadTaskStatus::Completed, tr("更新完成"));
            downloader->deleteLater();
            m_updateCache.remove(mod.sha1Hash);
            removeOldModFiles(mod, destPath);
            processNextModUpdate();
        });

    connect(downloader, &ContentDownloader::downloadFailed, this,
        [this, taskId, downloader](const QString &error)
        {
            DownloadTaskManager::instance()->updateTaskStatus(
                taskId, DownloadTaskStatus::Failed, error);
            downloader->deleteLater();
            processNextModUpdate();
        });

    downloader->downloadToInstance(target.downloadUrl, fileName, m_instancePath, ContentType::Mod);
}

void InstanceModsPage::removeOldModFiles(const ModInfo &mod, const QString &newPath)
{
    QStringList candidates;
    candidates << mod.filePath << (mod.filePath + ".disabled");

    QFileInfo newInfo(newPath);
    for (const QString &p : candidates)
    {
        if (p.isEmpty())
            continue;
        if (QFileInfo(p).absoluteFilePath() == newInfo.absoluteFilePath())
            continue;
        if (QFile::exists(p))
            QFile::remove(p);
    }
}

void InstanceModsPage::markCachedUpdateChips()
{
    for (auto it = m_updateCache.constBegin(); it != m_updateCache.constEnd(); ++it)
    {
        if (m_sha1ToIndex.contains(it.key()))
            markCardHasUpdate(m_sha1ToIndex.value(it.key()), it.value());
    }
}

void InstanceModsPage::toggleCardSelection(int idx)
{
    if (idx < 0 || idx >= m_cardWidgets.size())
        return;

    // 清除全选状态
    if (m_allSelected)
    {
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }

    if (m_selectedModIndices.contains(idx))
    {
        m_selectedModIndices.remove(idx);
        animateCardHighlight(idx, false);
    }
    else
    {
        m_selectedModIndices.insert(idx);
        animateCardHighlight(idx, true);
    }

    updateBottomBarState();
}

int InstanceModsPage::cardIndexAtGlobal(const QPoint &globalPos) const
{
    QWidget *w = QApplication::widgetAt(globalPos);
    while (w)
    {
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            if (m_cardWidgets[i] == w)
                return i;
        }
        w = w->parentWidget();
    }
    return -1;
}

void InstanceModsPage::selectCardDuringDrag(int idx)
{
    if (idx < 0 || idx >= m_cardWidgets.size())
        return;

    if (m_selectedModIndices.contains(idx))
        return;

    // 清除全选状态
    if (m_allSelected)
    {
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }

    m_selectedModIndices.insert(idx);
    animateCardHighlight(idx, true);
    updateBottomBarState();
}

void InstanceModsPage::deselectCardDuringDrag(int idx)
{
    if (idx < 0 || idx >= m_cardWidgets.size())
        return;

    if (!m_selectedModIndices.contains(idx))
        return;

    // 清除全选状态
    if (m_allSelected)
    {
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }

    m_selectedModIndices.remove(idx);
    animateCardHighlight(idx, false);
    updateBottomBarState();
}

void InstanceModsPage::deselectAllCards()
{
    for (int idx : m_selectedModIndices)
    {
        if (idx >= 0 && idx < m_cardWidgets.size())
            animateCardHighlight(idx, false);
    }
    m_selectedModIndices.clear();
    m_allSelected = false;
    m_selectAllBtn->setText(tr("全选"));
    updateBottomBarState();
}

void InstanceModsPage::applyFilterAndSearch()
{
    int visibleCount = 0;
    int enabledCount = 0;
    int disabledCount = 0;

    const QDateTime now = QDateTime::currentDateTime();
    // 时间筛选截止点（0 = 不启用）
    QDateTime timeCutoff;
    if (m_timeFilter == 1)
        timeCutoff = now.addDays(-7);
    else if (m_timeFilter == 2)
        timeCutoff = now.addDays(-30);
    else if (m_timeFilter == 3)
        timeCutoff = now.addDays(-365);

    const qint64 k10MB = 10LL * 1024 * 1024;
    const qint64 k100MB = 100LL * 1024 * 1024;

    for (int i = 0; i < m_cardWidgets.size() && i < m_modList.mods.size(); ++i)
    {
        const ModInfo &info = m_modList.mods.at(i);
        bool visible = true;

        // 筛选：启用/禁用
        switch (m_currentFilter)
        {
        case FilterAll:
            break;
        case FilterEnabled:
            visible = info.enabled;
            break;
        case FilterDisabled:
            visible = !info.enabled;
            break;
        }

        // 搜索：模糊匹配名称（中文名、英文名、文件名）
        if (visible && !m_currentSearch.isEmpty())
        {
            QString searchLower = m_currentSearch.toLower();
            QString nameLower = info.name.toLower();
            QString fileNameLower = info.fileName.toLower();
            QString chineseLower = info.chineseName.toLower();
            QString englishLower = info.englishName.toLower();
            visible = nameLower.contains(searchLower)
                      || fileNameLower.contains(searchLower)
                      || chineseLower.contains(searchLower)
                      || englishLower.contains(searchLower);
        }

        // 时间筛选（按文件修改时间）
        if (visible && timeCutoff.isValid()
            && m_mtimeCache.value(info.filePath) < timeCutoff)
        {
            visible = false;
        }

        // 大小筛选（阈值语义与资源中心本地管理一致）
        if (visible && m_sizeFilter > 0)
        {
            if (m_sizeFilter == 1 && info.fileSize >= k10MB)
                visible = false;
            else if (m_sizeFilter == 2
                     && (info.fileSize < k10MB || info.fileSize > k100MB))
                visible = false;
            else if (m_sizeFilter == 3 && info.fileSize <= k100MB)
                visible = false;
        }

        // 分类筛选：手动分类 / 未分类 + 智能筛选哨兵值
        if (visible && !m_currentCategoryId.isEmpty())
        {
            if (m_currentCategoryId == QStringLiteral("__uncategorized__"))
            {
                visible = m_categoryManager->categoryOf(info.fileName).isEmpty();
            }
            else if (m_currentCategoryId == QStringLiteral("__updatable__"))
            {
                visible = !info.sha1Hash.isEmpty()
                          && m_updateCache.contains(info.sha1Hash);
            }
            else if (m_currentCategoryId == QStringLiteral("__recent__"))
            {
                visible = m_mtimeCache.value(info.filePath) >= now.addDays(-7);
            }
            else if (m_currentCategoryId == QStringLiteral("__large__"))
            {
                visible = info.fileSize > k100MB;
            }
            else
            {
                visible = (m_categoryManager->categoryOf(info.fileName)
                           == m_currentCategoryId);
            }
        }

        m_cardWidgets[i]->setVisible(visible);

        if (visible)
        {
            visibleCount++;
        }

        if (info.enabled)
        {
            enabledCount++;
        }
        else
        {
            disabledCount++;
        }
    }

    // 更新统计标签（时间/大小筛选激活时同样显示可见数）
    if (m_currentSearch.isEmpty() && m_currentFilter == FilterAll
        && m_currentCategoryId.isEmpty() && m_timeFilter == 0 && m_sizeFilter == 0)
    {
        m_statsLabel->setText(tr("共 %1 个模组 (%2 启用, %3 禁用)")
            .arg(m_modList.totalCount)
            .arg(m_modList.enabledCount)
            .arg(m_modList.disabledCount));
    }
    else
    {
        m_statsLabel->setText(tr("显示 %1 个模组 (共 %2 个)")
            .arg(visibleCount)
            .arg(m_modList.totalCount));
    }

    // 空态：有搜索词时引导前往下载页；纯筛选隐藏的模组实际存在，不引导
    if (visibleCount == 0 && m_modList.totalCount > 0 && !m_currentSearch.isEmpty())
    {
        m_emptyLabel->setText(tr("没有找到匹配的模组"));
        m_emptyLabel->setVisible(true);
        m_downloadSearchBtn->setVisible(true);
    }
    else if (visibleCount == 0 && m_modList.totalCount > 0)
    {
        // 纯筛选（分类/状态/时间/大小）全部被隐藏：提示但不引导去下载页
        m_emptyLabel->setText(tr("没有符合筛选条件的模组"));
        m_emptyLabel->setVisible(true);
        m_downloadSearchBtn->hide();
    }
    else
    {
        if (m_modList.totalCount == 0)
            m_emptyLabel->setText(tr("暂无模组"));
        m_emptyLabel->setVisible(m_modList.totalCount == 0);
        m_downloadSearchBtn->hide();
    }
    // 提示可见时列表为空，隐藏滚动区让提示文字占据列表区域
    m_scrollArea->setVisible(!m_emptyLabel->isVisible());

    Q_UNUSED(enabledCount);
    Q_UNUSED(disabledCount);
}

// ── 本地资源分类 ─────────────────────────────────────────────────────────

void InstanceModsPage::reloadCategoryCombo()
{
    if (!m_categoryCombo)
        return;

    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();
    m_categoryCombo->addItem(tr("全部"), QString());

    // 智能筛选：由模组属性直接判定，无需手动归类
    m_categoryCombo->addItem(tr("可更新"), QStringLiteral("__updatable__"));
    m_categoryCombo->addItem(tr("最近修改"), QStringLiteral("__recent__"));
    m_categoryCombo->addItem(tr("大文件"), QStringLiteral("__large__"));
    m_categoryCombo->insertSeparator(m_categoryCombo->count());

    const QList<LocalResourceCategory> cats = m_categoryManager->categories();
    for (const LocalResourceCategory &c : cats)
        m_categoryCombo->addItem(c.name, c.id);
    m_categoryCombo->addItem(tr("未分类"), QStringLiteral("__uncategorized__"));

    // 尝试恢复之前选中的分类（可能已被删除，回退到“全部”）
    int restore = 0;
    if (!m_currentCategoryId.isEmpty())
    {
        for (int i = 0; i < m_categoryCombo->count(); ++i)
        {
            if (m_categoryCombo->itemData(i).toString() == m_currentCategoryId)
            {
                restore = i;
                break;
            }
        }
    }
    m_categoryCombo->setCurrentIndex(restore);
    m_currentCategoryId = m_categoryCombo->itemData(restore).toString();
    m_categoryCombo->blockSignals(false);
}

void InstanceModsPage::onCategoryFilterChanged(int index)
{
    if (!m_categoryCombo)
        return;
    m_currentCategoryId = m_categoryCombo->itemData(index).toString();
    applyFilterAndSearch();
}

void InstanceModsPage::onCategoryManageClicked()
{
    LocalCategoryDialog dialog(m_categoryManager, this);
    dialog.exec();
}

void InstanceModsPage::onCategoriesChanged()
{
    reloadCategoryCombo();
    refreshCardCategoryChips();
    applyFilterAndSearch();
}

void InstanceModsPage::onAssignmentsChanged()
{
    refreshCardCategoryChips();
    applyFilterAndSearch();
}

void InstanceModsPage::refreshCardCategoryChips()
{
    for (int i = 0; i < m_cardWidgets.size() && i < m_modList.mods.size(); ++i)
    {
        QWidget *card = m_cardWidgets[i];
        const ModInfo &info = m_modList.mods[i];
        const QString catName = m_categoryManager->categoryNameOf(info.fileName);

        const QList<QLabel *> labels = card->findChildren<QLabel *>();
        for (QLabel *chip : labels)
        {
            if (!chip->property("categoryChip").toBool())
                continue;
            if (catName.isEmpty())
            {
                chip->clear();
                chip->hide();
            }
            else
            {
                chip->setText(catName);
                chip->show();
            }
        }
    }
}