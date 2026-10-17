/**
 * @file   InstanceFilePage.cpp
 * @brief  实例管理 - 通用文件浏览页面实现（资源包/光影包/存档/截图）
 * @author BlockBox Team
 * @date   2026-06-27
 */
#include "InstanceFilePage.h"
#include "ResourcesPage.h"
#include "components/BlurLoadingOverlay.h"
#include "components/FavoriteFolderDialog.h"
#include "components/LocalCategoryDialog.h"
#include "components/MasonryContentCard.h"
#include "layouts/MasonryLayout.h"
#include "utils/IconHelper.h"
#include "utils/LocalCategoryManager.h"
#include "utils/ThemeManager.h"
#include "utils/ThumbnailProvider.h"
#include "utils/mod/ModrinthAPI.h"
#include "utils/content/ContentDownloader.h"
#include "utils/DownloadTaskManager.h"
#include "utils/UpdateChecker.h"

#include <QCryptographicHash>
#include <QApplication>
#include <algorithm>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QMenu>
#include <QMimeData>
#include "components/AppMessageBox.h"
#include <QMouseEvent>
#include <QPixmapCache>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSpacerItem>
#include <QTimer>
#include <QUrl>

// 前向声明
static bool copyDir(const QString &srcPath, const QString &dstPath);
// 递归统计目录总大小（存档等目录型资源用，供大小筛选/排序）
static qint64 dirTotalSize(const QString &path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

InstanceFilePage::InstanceFilePage(FileType type, QWidget *parent)
    : QWidget(parent)
    , m_fileType(type)
    , m_searchEdit(nullptr)
    , m_statsLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_cardFlowLayout(nullptr)
    , m_viewSwitch(nullptr)
    , m_viewMode(ContentViewSwitch::List)
    , m_loadingOverlay(nullptr)
    , m_emptyLabel(nullptr)
    , m_downloadSearchBtn(nullptr)
    , m_categoryManager(nullptr)
    , m_categoryCombo(nullptr)
    , m_categoryManageBtn(nullptr)
    , m_statusCombo(nullptr)
    , m_timeCombo(nullptr)
    , m_sizeCombo(nullptr)
    , m_sortCombo(nullptr)
    , m_dragStatusLabel(nullptr)
    , m_bottomBar(nullptr)
    , m_selectAllBtn(nullptr)
    , m_checkUpdateBtn(nullptr)
    , m_copyBtn(nullptr)
    , m_openFolderBtn(nullptr)
    , m_detailBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_pasteBtn(nullptr)
    , m_updateBtn(nullptr)
    , m_installNewBtn(nullptr)
    , m_isLoading(false)
    , m_allSelected(false)
{
    m_updateCheckApi = new ModrinthAPI(this);
    m_categoryManager = new LocalCategoryManager(this);
    initUI();
    setupConnections();
}

InstanceFilePage::~InstanceFilePage()
{
}

QString InstanceFilePage::subDirName() const
{
    switch (m_fileType)
    {
    case ResourcePack: return QStringLiteral("resourcepacks");
    case ShaderPack:   return QStringLiteral("shaderpacks");
    case Save:         return QStringLiteral("saves");
    case Screenshot:   return QStringLiteral("screenshots");
    }
    return QString();
}

QString InstanceFilePage::typeDisplayName() const
{
    switch (m_fileType)
    {
    case ResourcePack: return tr("资源包");
    case ShaderPack:   return tr("光影包");
    case Save:         return tr("存档");
    case Screenshot:   return tr("截图");
    }
    return QString();
}

void InstanceFilePage::setInstancePath(const QString &path)
{
    if (m_instancePath != path)
    {
        m_instancePath = path;
        m_categoryManager->setInstancePath(path);
        m_categoryManager->setResourceType(subDirName());
        m_currentCategoryId.clear();
        reloadCategoryCombo();
        if (!path.isEmpty())
        {
            refreshFileList();
        }
    }
}

void InstanceFilePage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // 顶部筛选卡：行1=搜索+视图切换+分类，行2=状态/时间/大小/排序（顶部最多两行）
    QWidget *filterCard = new QWidget(this);
    filterCard->setObjectName("filterCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(8);

    // ---- 行1：搜索框 + 视图切换 + 分类 ----
    QHBoxLayout *topBarLayout = new QHBoxLayout();
    topBarLayout->setSpacing(8);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索%1名称...").arg(typeDisplayName()));
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->setClearButtonEnabled(true);
    topBarLayout->addWidget(m_searchEdit, 1);

    // 视图切换（列表式 / 瀑布流），偏好按文件类型持久化
    m_viewSwitch = new ContentViewSwitch(this);
    m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted(viewPrefKey(), ContentViewSwitch::List));
    m_viewMode = static_cast<int>(m_viewSwitch->viewMode());
    topBarLayout->addWidget(m_viewSwitch);

    QLabel *categoryLabel = new QLabel(tr("分类:"), this);
    categoryLabel->setObjectName("filterLabel");
    topBarLayout->addWidget(categoryLabel);

    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->setObjectName("categoryCombo");
    m_categoryCombo->setFixedHeight(28);
    m_categoryCombo->setMinimumWidth(130);
    m_categoryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_categoryCombo->setMinimumContentsLength(5);
    reloadCategoryCombo();
    topBarLayout->addWidget(m_categoryCombo);

    m_categoryManageBtn = new QPushButton(tr("管理分类"), this);
    m_categoryManageBtn->setObjectName("categoryManageBtn");
    m_categoryManageBtn->setFixedHeight(28);
    m_categoryManageBtn->setCursor(Qt::PointingHandCursor);
    topBarLayout->addWidget(m_categoryManageBtn);

    cardLayout->addLayout(topBarLayout);

    // ---- 行2：状态 / 时间 / 大小 / 排序 + 统计 ----
    QHBoxLayout *filterRow = new QHBoxLayout();
    filterRow->setSpacing(6);

    auto makeFilterLabel = [this](const QString &text) -> QLabel* {
        QLabel *label = new QLabel(text, this);
        label->setObjectName("filterLabel");
        return label;
    };
    auto makeFilterCombo = [this]() -> QComboBox* {
        QComboBox *combo = new QComboBox(this);
        combo->setObjectName("filterCombo");
        combo->setFixedHeight(28);
        combo->setMinimumWidth(104);
        return combo;
    };

    // 状态筛选（截图无禁用概念，隐藏）
    if (m_fileType != Screenshot)
    {
        filterRow->addWidget(makeFilterLabel(tr("筛选:")));
        m_statusCombo = makeFilterCombo();
        m_statusCombo->addItem(tr("全部"), 0);
        m_statusCombo->addItem(tr("已启用"), 1);
        m_statusCombo->addItem(tr("已禁用"), 2);
        filterRow->addWidget(m_statusCombo);
    }

    filterRow->addWidget(makeFilterLabel(tr("时间:")));
    m_timeCombo = makeFilterCombo();
    m_timeCombo->addItem(tr("全部时间"), 0);
    m_timeCombo->addItem(tr("最近 7 天"), 1);
    m_timeCombo->addItem(tr("最近 30 天"), 2);
    m_timeCombo->addItem(tr("最近一年"), 3);
    filterRow->addWidget(m_timeCombo);

    filterRow->addWidget(makeFilterLabel(tr("大小:")));
    m_sizeCombo = makeFilterCombo();
    m_sizeCombo->addItem(tr("全部大小"), 0);
    m_sizeCombo->addItem(tr("小于 10 MB"), 1);
    m_sizeCombo->addItem(tr("10 ~ 100 MB"), 2);
    m_sizeCombo->addItem(tr("大于 100 MB"), 3);
    filterRow->addWidget(m_sizeCombo);

    filterRow->addWidget(makeFilterLabel(tr("排序:")));
    m_sortCombo = makeFilterCombo();
    m_sortCombo->setObjectName("searchSortCombo");
    m_sortCombo->addItem(tr("名称 A→Z"), 0);
    m_sortCombo->addItem(tr("名称 Z→A"), 1);
    m_sortCombo->addItem(tr("大小 大→小"), 2);
    m_sortCombo->addItem(tr("大小 小→大"), 3);
    m_sortCombo->addItem(tr("时间 新→旧"), 4);
    m_sortCombo->addItem(tr("时间 旧→新"), 5);
    filterRow->addWidget(m_sortCombo);

    filterRow->addStretch();

    m_statsLabel = new QLabel();
    m_statsLabel->setObjectName("statusLabel");
    filterRow->addWidget(m_statsLabel);
    cardLayout->addLayout(filterRow);

    mainLayout->addWidget(filterCard);

    // 列表滚动区（直接接受拖放导入，无需先点按钮）
    m_scrollArea = new QScrollArea();
    m_scrollArea->setObjectName("fileListScroll");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_cardContainer = new QWidget();
    if (m_viewMode == ContentViewSwitch::List)
    {
        m_cardGridLayout = new QGridLayout(m_cardContainer);
        m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
        m_cardGridLayout->setSpacing(8);
    }
    else
    {
        m_cardFlowLayout = new MasonryLayout(m_cardContainer, 0, 10, 10);
        m_cardFlowLayout->setContentsMargins(14, 0, 14, 0);
    }
    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    // 空提示放在列表区域内（无资源时居中显示在卡片本该出现的位置）
    m_emptyLabel = new QLabel(tr("暂无%1").arg(typeDisplayName()));
    m_emptyLabel->setObjectName("modPlaceholderLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    mainLayout->addWidget(m_emptyLabel, 1);

    // 拖放导入结果提示（列表下方，默认隐藏）
    m_dragStatusLabel = new QLabel();
    m_dragStatusLabel->setObjectName("dropStatusLabel");
    m_dragStatusLabel->setAlignment(Qt::AlignCenter);
    m_dragStatusLabel->hide();
    mainLayout->addWidget(m_dragStatusLabel);

    // 整页接受拖放：拖文件进列表即导入，无需先切「拖入模式」
    setAcceptDrops(true);

    // 底部操作栏
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    m_bottomBar = new QWidget();
    m_bottomBar->setObjectName("modBottomBar");
    m_bottomBar->setFixedHeight(48);
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(14, 6, 14, 6);
    bottomLayout->setSpacing(10);

    m_checkUpdateBtn = new QPushButton(tr("检查更新"));
    m_checkUpdateBtn->setObjectName("bottomActionBtn");
    m_checkUpdateBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/update.svg", themeColor, 18));
    m_checkUpdateBtn->setIconSize(QSize(18, 18));
    m_checkUpdateBtn->hide();

    m_selectAllBtn = new QPushButton(tr("全选"));
    m_selectAllBtn->setObjectName("bottomActionBtn");

    m_copyBtn = new QPushButton(tr("复制"));
    m_copyBtn->setObjectName("bottomActionBtn");
    m_copyBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/copy.svg", themeColor, 18));
    m_copyBtn->setIconSize(QSize(18, 18));
    m_copyBtn->hide();

    m_openFolderBtn = new QPushButton(tr("打开文件夹"));
    m_openFolderBtn->setObjectName("bottomActionBtn");
    m_openFolderBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/folder.svg", themeColor, 18));
    m_openFolderBtn->setIconSize(QSize(18, 18));

    m_detailBtn = new QPushButton(tr("详情"));
    m_detailBtn->setObjectName("bottomActionBtn");
    m_detailBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/list.svg", themeColor, 18));
    m_detailBtn->setIconSize(QSize(18, 18));
    m_detailBtn->hide();

    m_deleteBtn = new QPushButton(tr("删除"));
    m_deleteBtn->setObjectName("bottomActionBtn");
    m_deleteBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    m_deleteBtn->setIconSize(QSize(18, 18));
    m_deleteBtn->hide();

    m_pasteBtn = new QPushButton(tr("粘贴%1").arg(typeDisplayName()));
    m_pasteBtn->setObjectName("bottomActionBtn");
    m_pasteBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/nav_import.svg", themeColor, 18));
    m_pasteBtn->setIconSize(QSize(18, 18));

    m_updateBtn = new QPushButton(tr("更新"));
    m_updateBtn->setObjectName("bottomActionBtn");
    m_updateBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/update.svg", themeColor, 18));
    m_updateBtn->setIconSize(QSize(18, 18));
    m_updateBtn->hide();

    // 「安装新XX」入口：仅支持在线下载的类型显示（截图无网络下载源）
    m_installNewBtn = nullptr;
    if (m_fileType != Screenshot)
    {
        m_installNewBtn = new QPushButton(tr("安装新%1").arg(typeDisplayName()));
        m_installNewBtn->setObjectName("bottomActionBtn");
        m_installNewBtn->setIcon(IconHelper::loadColoredIcon(
            ":/Images/Icons/install.svg", themeColor, 18));
        m_installNewBtn->setIconSize(QSize(18, 18));
        m_installNewBtn->setCursor(Qt::PointingHandCursor);
        m_installNewBtn->setToolTip(tr("前往%1下载页，自动筛选至当前实例").arg(typeDisplayName()));
    }

    bottomLayout->addWidget(m_checkUpdateBtn);
    bottomLayout->addWidget(m_selectAllBtn);
    bottomLayout->addWidget(m_copyBtn);
    bottomLayout->addWidget(m_openFolderBtn);
    bottomLayout->addWidget(m_detailBtn);
    bottomLayout->addWidget(m_deleteBtn);
    bottomLayout->addWidget(m_pasteBtn);
    bottomLayout->addWidget(m_updateBtn);
    bottomLayout->addStretch();
    if (m_installNewBtn)
        bottomLayout->addWidget(m_installNewBtn);
    mainLayout->addWidget(m_bottomBar);

    // 加载遮罩
    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();

    // 搜索无结果时前往下载页按钮
    m_downloadSearchBtn = new QPushButton(tr("前往下载%1页搜索").arg(typeDisplayName()));
    m_downloadSearchBtn->setObjectName("downloadSearchBtn");
    m_downloadSearchBtn->setFixedHeight(36);
    m_downloadSearchBtn->hide();
    mainLayout->addWidget(m_downloadSearchBtn);
}

void InstanceFilePage::setupConnections()
{
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &InstanceFilePage::onSearchTextChanged);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceFilePage::onCategoryFilterChanged);
    connect(m_categoryManageBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onCategoryManageClicked);
    if (m_statusCombo)
    {
        connect(m_statusCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &InstanceFilePage::onStatusFilterChanged);
    }
    connect(m_timeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceFilePage::onTimeFilterChanged);
    connect(m_sizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceFilePage::onSizeFilterChanged);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceFilePage::onSortChanged);
    connect(m_categoryManager, &LocalCategoryManager::categoriesChanged,
            this, &InstanceFilePage::onCategoriesChanged);
    connect(m_categoryManager, &LocalCategoryManager::assignmentsChanged,
            this, &InstanceFilePage::onAssignmentsChanged);
    connect(m_selectAllBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onSelectAllClicked);
    connect(m_checkUpdateBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onCheckUpdatesClicked);
    connect(m_updateBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onUpdateSelectedClicked);
    connect(m_openFolderBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onOpenFolderClicked);
    connect(m_pasteBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onPasteFileClicked);
    connect(m_copyBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onCopyFileClicked);
    connect(m_detailBtn, &QPushButton::clicked, this, [this]() {
        if (!m_selectedIndices.isEmpty()) {
            int idx = *m_selectedIndices.begin();
            if (idx >= 0 && idx < m_fileList.size()) {
                const FileEntry &entry = m_fileList.at(idx);
                QString info = tr("名称: %1\n路径: %2\n大小: %3\n修改时间: %4")
                    .arg(entry.fileName)
                    .arg(entry.filePath)
                    .arg(formatFileSize(entry.fileSize))
                    .arg(entry.lastModified.toString("yyyy-MM-dd hh:mm:ss"));
                AppMessageBox::information(this, tr("文件详情"), info);
            }
        }
    });
    connect(m_deleteBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onDeleteSelectedClicked);
    connect(m_downloadSearchBtn, &QPushButton::clicked,
            this, &InstanceFilePage::onDownloadSearchClicked);
    if (m_installNewBtn)
    {
        connect(m_installNewBtn, &QPushButton::clicked, this, [this]() {
            emit fileSearchRequested(QString());
        });
    }
    connect(m_updateCheckApi, &ModrinthAPI::hashesMatched,
            this, &InstanceFilePage::onHashesMatched);
    connect(m_updateCheckApi, &ModrinthAPI::hashesMatchFailed,
            this, &InstanceFilePage::onUpdateCheckBatchFailed);
    connect(m_updateCheckApi, &ModrinthAPI::updatesChecked,
            this, &InstanceFilePage::onUpdatesChecked);
    connect(m_updateCheckApi, &ModrinthAPI::updatesCheckFailed,
            this, &InstanceFilePage::onUpdateCheckBatchFailed);
    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged,
            this, &InstanceFilePage::onViewModeChanged);
}

void InstanceFilePage::refreshFileList()
{
    if (m_instancePath.isEmpty())
    {
        return;
    }
    m_isLoading = true;
    showLoading(true);
    showEmptyHint(false);
    clearCards();
    scanFiles();
}

void InstanceFilePage::scanFiles()
{
    m_fileList.clear();

    QString dirPath = m_instancePath + "/" + subDirName();
    QDir dir(dirPath);
    if (!dir.exists())
    {
        m_isLoading = false;
        showLoading(false);
        showEmptyHint(true);
        return;
    }

    QFileInfoList entries;
    switch (m_fileType)
    {
    case Save:
        // 存档是目录（含 .disabled 禁用目录）
        entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        break;
    case ResourcePack:
    case ShaderPack:
        // 资源包/光影包是 .zip 文件（含 .zip.disabled 禁用文件）
        entries = dir.entryInfoList(QStringList() << "*.zip" << "*.zip.disabled",
                                    QDir::Files, QDir::Name);
        break;
    case Screenshot:
        // 截图是图片文件
        entries = dir.entryInfoList(
            QStringList() << "*.png" << "*.jpg" << "*.jpeg" << "*.webp",
            QDir::Files, QDir::Name);
        break;
    }

    for (const QFileInfo &fi : entries)
    {
        FileEntry entry;
        entry.filePath = fi.absoluteFilePath();
        entry.fileName = fi.fileName();
        entry.enabled = !entry.fileName.endsWith(".disabled", Qt::CaseInsensitive);
        // 目录型（存档）递归统计大小，供大小筛选/排序使用；空目录为 0
        entry.fileSize = fi.isDir() ? dirTotalSize(fi.absoluteFilePath()) : fi.size();
        entry.lastModified = fi.lastModified();
        // 从缓存恢复 SHA-1（避免每次刷新都重新计算哈希）
        if (m_sha1Cache.contains(entry.filePath))
            entry.sha1Hash = m_sha1Cache.value(entry.filePath);
        m_fileList.append(entry);
    }

    // 按当前排序模式重排（默认名称 A→Z，与扫描顺序一致）
    sortFileList();

    m_isLoading = false;
    showLoading(false);

    // SHA-1 → 索引映射已由 sortFileList() 重建（卡片创建前建好，供更新标记使用）

    m_statsLabel->setText(tr("共 %1 个%2").arg(m_fileList.size()).arg(typeDisplayName()));

    if (m_fileList.isEmpty())
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

void InstanceFilePage::createNextBatch()
{
    static const int BATCH_SIZE = 30;
    int start = m_batchIndex;
    int end = qMin(start + BATCH_SIZE, m_fileList.size());

    for (int i = start; i < end; ++i)
    {
        const FileEntry &entry = m_fileList.at(i);
        QWidget *card = (m_viewMode == ContentViewSwitch::List)
            ? createFileCard(entry.filePath, entry.fileName,
                             entry.fileSize, entry.lastModified)
            : createMasonryCard(entry);
        m_cardWidgets.append(card);
    }

    m_batchIndex = end;

    if (m_batchIndex < m_fileList.size())
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
        for (auto it = m_updateCache.constBegin(); it != m_updateCache.constEnd(); ++it)
        {
            if (m_sha1ToIndex.contains(it.key()))
                markCardHasUpdate(m_sha1ToIndex.value(it.key()), it.value());
        }

        placeCards();
        applyFilterAndSearch();
    }
}

QWidget *InstanceFilePage::createFileCard(const QString &filePath, const QString &fileName,
                                          qint64 fileSize, const QDateTime &lastModified)
{
    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("filePath", filePath);
    card->setProperty("fileName", fileName);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    card->installEventFilter(this);

    QHBoxLayout *mainLayout = new QHBoxLayout(card);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    // 图标 - 从本地文件提取，失败则回退到类型 SVG 图标
    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    {
        QString cacheKey = QString("rp_icon:%1").arg(filePath);
        QPixmap icon;
        if (!QPixmapCache::find(cacheKey, &icon))
        {
            switch (m_fileType)
            {
            case ResourcePack:
            case ShaderPack:
                icon = ResourcesPage::extractResourcePackIcon(filePath);
                break;
            case Save:
                icon = ResourcesPage::extractSaveIcon(filePath);
                break;
            case Screenshot:
                icon = ResourcesPage::loadImageThumbnail(filePath);
                break;
            }
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
            QString fallbackIcon;
            switch (m_fileType)
            {
            case ResourcePack: fallbackIcon = ":/Images/Icons/nav_resourcepacks.svg"; break;
            case ShaderPack:   fallbackIcon = ":/Images/Icons/nav_shaders.svg"; break;
            case Save:         fallbackIcon = ":/Images/Icons/nav_worlds.svg"; break;
            case Screenshot:   fallbackIcon = ":/Images/Icons/screenshot.svg"; break;
            }
            iconLabel->setPixmap(IconHelper::loadColoredIcon(fallbackIcon, tc, 36).pixmap(36, 36));
        }
    }
    mainLayout->addWidget(iconLabel);

    // 信息区域
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    // 文件名（禁用项去掉 .disabled 后缀显示，与资源中心本地管理一致）
    QString displayName = fileName;
    if (displayName.endsWith(".disabled", Qt::CaseInsensitive))
        displayName.chop(9);
    QLabel *nameLabel = new QLabel(displayName);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(10);
    nameLabel->setFont(nameFont);
    infoLayout->addWidget(nameLabel);

    // 标签行
    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    if (fileSize > 0 || m_fileType != Save)
    {
        QString sizeText = (m_fileType == Save)
            ? tr("文件夹")
            : formatFileSize(fileSize);
        QLabel *sizeChip = new QLabel(sizeText);
        sizeChip->setObjectName("modChip");
        sizeChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(sizeChip);
    }

    if (lastModified.isValid())
    {
        QString dateText = lastModified.toString("yyyy-MM-dd");
        QLabel *dateChip = new QLabel(dateText);
        dateChip->setObjectName("modChip");
        dateChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(dateChip);
    }

    // 已禁用芯片（.disabled 后缀项）
    if (fileName.endsWith(".disabled", Qt::CaseInsensitive))
    {
        QLabel *disabledChip = new QLabel(tr("已禁用"));
        disabledChip->setObjectName("modChip");
        disabledChip->setProperty("cardRole", "chip");
        disabledChip->setStyleSheet(
            "color: #9E9E9E; background-color: rgba(158, 158, 158, 40);"
            "border-radius: 9px; padding: 2px 8px; font-size: 11px;");
        chipsLayout->addWidget(disabledChip);
    }

    // 更新提示芯片（仅资源包/光影包显示）
    if (m_fileType == ResourcePack || m_fileType == ShaderPack)
    {
        QLabel *updateChip = new QLabel(tr("可更新"));
        updateChip->setObjectName("fileUpdateChip");
        updateChip->setProperty("cardRole", "chip");
        QColor tc(ThemeManager::instance()->currentThemeColor());
        updateChip->setStyleSheet(QString(
            "color: white; background-color: %1; border-radius: 9px;"
            "padding: 2px 8px; font-size: 11px;").arg(tc.name()));
        updateChip->hide();
        chipsLayout->addWidget(updateChip);
    }

    // 分类标签（手动分类，默认隐藏，样式见 style.qss categoryChip="true"）
    QLabel *categoryChip = new QLabel();
    categoryChip->setObjectName("modChip");
    categoryChip->setProperty("categoryChip", true);
    const QString listCatName = m_categoryManager->categoryNameOf(fileName);
    if (!listCatName.isEmpty())
    {
        categoryChip->setText(listCatName);
        categoryChip->show();
    }
    chipsLayout->addWidget(categoryChip);

    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);
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

    QString capturedPath = filePath;
    QString capturedName = fileName;

    // 快捷启动按钮（仅存档类型显示）：启动游戏并直接进入该存档
    if (m_fileType == Save)
    {
        QPushButton *quickLaunchBtn = createBtn(":/Images/Icons/quick_launch.svg", tr("快捷启动"));
        connect(quickLaunchBtn, &QPushButton::clicked, this, [this, capturedName]() {
            emit quickLaunchSaveRequested(capturedName);
        });
        btnLayout->addWidget(quickLaunchBtn);

        // 存档设置按钮：请求打开存档设置页
        QPushButton *settingsBtn = createBtn(":/Images/Icons/instance_settings.svg", tr("存档设置"));
        connect(settingsBtn, &QPushButton::clicked, this, [this, capturedName]() {
            emit saveSettingsRequested(capturedName);
        });
        btnLayout->addWidget(settingsBtn);

        // 地图按钮：请求打开该存档的地表地图页
        QPushButton *mapBtn = createBtn(":/Images/Icons/map.svg", tr("地图"));
        connect(mapBtn, &QPushButton::clicked, this, [this, capturedName]() {
            emit saveMapRequested(capturedName);
        });
        btnLayout->addWidget(mapBtn);

        // 版本转化按钮：Java 版 ⇄ 基岩版存档互转（内置引擎，无需插件）
        QPushButton *convertBtn = createBtn(":/Images/Icons/convert.svg", tr("版本转化"));
        connect(convertBtn, &QPushButton::clicked, this, [this, capturedName]() {
            emit saveConvertRequested(capturedName);
        });
        btnLayout->addWidget(convertBtn);
    }

    // 光影设置按钮（仅光影包类型显示）：可视化编辑光影包选项
    if (m_fileType == ShaderPack)
    {
        QPushButton *shaderSettingsBtn = createBtn(
            ":/Images/Icons/instance_settings.svg", tr("光影设置"));
        connect(shaderSettingsBtn, &QPushButton::clicked, this, [this, capturedPath]() {
            if (m_instancePath.isEmpty())
                return;
            emit shaderSettingsRequested(capturedPath);
        });
        btnLayout->addWidget(shaderSettingsBtn);
    }

    // 详情按钮
    QPushButton *detailBtn = createBtn(":/Images/Icons/list.svg", tr("查看详情"));
    connect(detailBtn, &QPushButton::clicked, this, [this, capturedPath, capturedName, fileSize, lastModified]() {
        QString info = tr("名称: %1\n路径: %2\n大小: %3\n修改时间: %4")
            .arg(capturedName)
            .arg(capturedPath)
            .arg(formatFileSize(fileSize))
            .arg(lastModified.toString("yyyy-MM-dd hh:mm:ss"));
        AppMessageBox::information(this, tr("文件详情"), info);
    });
    btnLayout->addWidget(detailBtn);

    // 截图查看按钮（仅截图类型显示）
    if (m_fileType == Screenshot)
    {
        QPushButton *viewBtn = createBtn(":/Images/Icons/screenshot.svg", tr("查看截图"));
        connect(viewBtn, &QPushButton::clicked, this, [capturedPath]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(capturedPath));
        });
        btnLayout->addWidget(viewBtn);
    }

    // 打开文件夹按钮
    QPushButton *folderBtn = createBtn(":/Images/Icons/folder.svg", tr("打开文件位置"));
    connect(folderBtn, &QPushButton::clicked, this, [capturedPath]() {
        QFileInfo fi(capturedPath);
        QString dirPath = fi.isDir() ? capturedPath : fi.absolutePath();
        if (!dirPath.isEmpty())
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
        }
    });
    btnLayout->addWidget(folderBtn);

    // 删除按钮
    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"));
    deleteBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    connect(deleteBtn, &QPushButton::clicked, this, [this, capturedPath, capturedName]() {
        AppMessageBox::StandardButton reply = AppMessageBox::question(
            this,
            tr("删除%1").arg(typeDisplayName()),
            tr("确定要删除 \"%1\" 吗？此操作不可撤销。").arg(capturedName),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);

        if (reply == AppMessageBox::Yes)
        {
            QFileInfo fi(capturedPath);
            if (fi.isDir())
            {
                QDir(capturedPath).removeRecursively();
            }
            else
            {
                QFile::remove(capturedPath);
            }
            refreshFileList();
        }
    });
    btnLayout->addWidget(deleteBtn);

    mainLayout->addLayout(btnLayout);

    createSelectionOverlay(card);

    return card;
}

QString InstanceFilePage::viewPrefKey() const
{
    return QStringLiteral("instance_file_%1").arg(subDirName());
}

/* 视图切换：按文件类型持久化偏好并重建卡片 */
void InstanceFilePage::onViewModeChanged(ContentViewSwitch::ViewMode mode)
{
    m_viewMode = static_cast<int>(mode);
    ContentViewSwitch::savePersisted(viewPrefKey(), mode);
    rebuildCards();
}

void InstanceFilePage::rebuildCards()
{
    ++m_batchGeneration;   // 作废在途的分批创建回调
    clearCards();

    if (m_cardGridLayout) { delete m_cardGridLayout; m_cardGridLayout = nullptr; }
    if (m_cardFlowLayout) { delete m_cardFlowLayout; m_cardFlowLayout = nullptr; }
    if (m_viewMode == ContentViewSwitch::List)
    {
        m_cardGridLayout = new QGridLayout(m_cardContainer);
        m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
        m_cardGridLayout->setSpacing(8);
    }
    else
    {
        m_cardFlowLayout = new MasonryLayout(m_cardContainer, 0, 10, 10);
        m_cardFlowLayout->setContentsMargins(14, 0, 14, 0);
    }

    // 重新走分批创建，避免资源多时一次性重建卡顿
    m_batchIndex = 0;
    int gen = m_batchGeneration;
    QTimer::singleShot(0, this, [this, gen]() {
        if (gen == m_batchGeneration)
            createNextBatch();
    });
}

/* 瀑布流卡片：截图以真实缩略图为封面横幅；其余类型拿不到封面，
 * 提取图标在封面区正中放大（拿不到图标则首字母徽标居中兜底） */
QWidget *InstanceFilePage::createMasonryCard(const FileEntry &entry)
{
    // 主色调：按文件路径稳定散列取渐变色板，同一路径颜色恒定
    ModInfo paletteSeed;
    paletteSeed.source = QStringLiteral("instance_file");
    paletteSeed.id = entry.filePath;
    QColor c1, c2;
    MasonryContentCard::paletteFor(paletteSeed, c1, c2);

    const int bannerH = 120;
    const int kCardW = 260;

    // 提取图标（非截图类型）：居中徽标与 banner 底色共用它
    QPixmap icon;
    if (m_fileType != Screenshot)
    {
        const QString cacheKey = QString("rp_icon:%1").arg(entry.filePath);
        if (!QPixmapCache::find(cacheKey, &icon))
        {
            switch (m_fileType)
            {
            case ResourcePack:
            case ShaderPack:
                icon = ResourcesPage::extractResourcePackIcon(entry.filePath);
                break;
            case Save:
                icon = ResourcesPage::extractSaveIcon(entry.filePath);
                break;
            case Screenshot:
                break;
            }
            if (!icon.isNull())
                QPixmapCache::insert(cacheKey, icon);
        }
    }
    // banner 底色：有图标 → 图标同色系渐变；拿不到图标 → 散列色板
    QColor banner1 = c1, banner2 = c2;
    MasonryContentCard::paletteForIcon(icon, banner1, banner2);

    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("contentCard");
    card->setFixedWidth(kCardW);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("filePath", entry.filePath);
    card->setProperty("fileName", entry.fileName);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    card->installEventFilter(this);
    MasonryContentCard::applyShadow(card);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    // banner 内缩 1px，避免占满顶部盖住卡片边框
    cardLayout->setContentsMargins(1, 1, 1, 1);
    cardLayout->setSpacing(0);

    // 封面横幅：截图用真实图片，其余类型用渐变色块（图标同色系 / 散列色板）
    const int bannerW = kCardW - 2;
    QLabel *banner = new QLabel(card);
    banner->setObjectName("contentCardBanner");
    banner->setFixedHeight(bannerH);
    banner->setAlignment(Qt::AlignCenter);
    QPixmap cover;
    if (m_fileType == Screenshot)
    {
        const QString cacheKey = QString("rp_cover:%1").arg(entry.filePath);
        if (!QPixmapCache::find(cacheKey, &cover))
        {
            cover = ThumbnailProvider::getThumbnail(entry.filePath, 512);
            if (!cover.isNull())
                QPixmapCache::insert(cacheKey, cover);
        }
    }
    if (!cover.isNull())
        banner->setPixmap(MasonryContentCard::makeCoverPixmap(cover, bannerW, bannerH));
    else
        banner->setPixmap(MasonryContentCard::makeBannerPixmap(banner1, banner2, bannerW, bannerH));
    cardLayout->addWidget(banner);

    // 徽标：截图封面本身就是内容，不叠 logo；其余类型无封面，图标居中放大
    if (m_fileType != Screenshot)
    {
        if (!icon.isNull())
        {
            // 无封面（非截图类型永远拿不到封面）：图标在封面区正中放大，不放右下角
            const int badgeSize = MasonryContentCard::logoSizeFor(true, bannerH);
            QLabel *logo = new QLabel(banner);
            logo->setObjectName("contentCardLogo");
            logo->setAlignment(Qt::AlignCenter);
            logo->setPixmap(MasonryContentCard::makeRoundIcon(icon, badgeSize));
            MasonryContentCard::placeLogo(logo, true, bannerW, bannerH);
            auto *logoShadow = new QGraphicsDropShadowEffect(logo);
            logoShadow->setBlurRadius(4);
            logoShadow->setOffset(0, 1);
            logoShadow->setColor(QColor(0, 0, 0, 180));
            logo->setGraphicsEffect(logoShadow);
            logo->raise();
        }
        else
        {
            // 提取失败：首字母渐变徽标兜底，同样居中放大（与 MasonryContentCard::build 一致）
            const QString letter = entry.fileName.isEmpty()
                ? QStringLiteral("?") : entry.fileName.left(1).toUpper();
            const int badgeSize = MasonryContentCard::logoSizeFor(true, bannerH);
            QLabel *logo = new QLabel(banner);
            logo->setObjectName("contentCardLogo");
            logo->setAlignment(Qt::AlignCenter);
            logo->setPixmap(MasonryContentCard::makeLogoPixmap(c1, c2, letter, badgeSize));
            MasonryContentCard::placeLogo(logo, true, bannerW, bannerH);
            auto *logoShadow = new QGraphicsDropShadowEffect(logo);
            logoShadow->setBlurRadius(4);
            logoShadow->setOffset(0, 1);
            logoShadow->setColor(QColor(0, 0, 0, 180));
            logo->setGraphicsEffect(logoShadow);
            logo->raise();
        }
    }

    // 内容区
    QWidget *body = new QWidget(card);
    QVBoxLayout *bv = new QVBoxLayout(body);
    bv->setContentsMargins(12, 12, 12, 10);
    bv->setSpacing(6);

    QString masonryDisplayName = entry.fileName;
    if (masonryDisplayName.endsWith(".disabled", Qt::CaseInsensitive))
        masonryDisplayName.chop(9);
    QLabel *nameLabel = new QLabel(masonryDisplayName, body);
    nameLabel->setObjectName("modCardName");   // 与列表卡一致，兼容 findChild 逻辑
    bv->addWidget(nameLabel);

    // chips：大小 / 修改日期 / 可更新
    QWidget *chipsWidget = new QWidget(body);
    chipsWidget->setObjectName("contentCardChips");
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);
    auto addChip = [&](const QString &text) {
        QLabel *chip = new QLabel(text);
        chip->setObjectName("contentChip");
        chipsLayout->addWidget(chip);
    };
    if (entry.fileSize > 0 || m_fileType != Save)
        addChip((m_fileType == Save) ? tr("文件夹") : formatFileSize(entry.fileSize));
    if (entry.lastModified.isValid())
        addChip(entry.lastModified.toString("yyyy-MM-dd"));
    if (!entry.enabled)
        addChip(tr("已禁用"));
    if (m_fileType == ResourcePack || m_fileType == ShaderPack)
    {
        QLabel *updateChip = new QLabel(tr("可更新"), chipsWidget);
        updateChip->setObjectName("fileUpdateChip");   // markCardHasUpdate 经 findChild 定位
        QColor tc(ThemeManager::instance()->currentThemeColor());
        updateChip->setStyleSheet(QString(
            "color: white; background-color: %1; border-radius: 9px;"
            "padding: 2px 8px; font-size: 11px;").arg(tc.name()));
        updateChip->hide();
        chipsLayout->addWidget(updateChip);
    }

    // 分类标签（手动分类，默认隐藏）
    QLabel *categoryChip = new QLabel(chipsWidget);
    categoryChip->setObjectName("contentChip");
    categoryChip->setProperty("categoryChip", true);
    const QString catName = m_categoryManager->categoryNameOf(entry.fileName);
    if (!catName.isEmpty())
    {
        categoryChip->setText(catName);
        categoryChip->show();
    }
    chipsLayout->addWidget(categoryChip);

    chipsLayout->addStretch();
    bv->addWidget(chipsWidget);
    bv->addStretch();

    // 操作按钮（与列表卡同套动作）
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 4, 0, 0);
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton * {
        QPushButton *btn = new QPushButton();
        btn->setObjectName("contentCardActionBtn");
        btn->setFixedSize(32, 32);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
        btn->setIconSize(QSize(18, 18));
        btnLayout->addWidget(btn);
        return btn;
    };

    const QString capturedPath = entry.filePath;
    const QString capturedName = entry.fileName;

    // 快捷启动 + 存档设置（仅存档类型显示）
    if (m_fileType == Save)
    {
        connect(createBtn(":/Images/Icons/quick_launch.svg", tr("快捷启动")), &QPushButton::clicked,
                this, [this, capturedName]() { emit quickLaunchSaveRequested(capturedName); });
        connect(createBtn(":/Images/Icons/instance_settings.svg", tr("存档设置")), &QPushButton::clicked,
                this, [this, capturedName]() { emit saveSettingsRequested(capturedName); });
        connect(createBtn(":/Images/Icons/map.svg", tr("地图")), &QPushButton::clicked,
                this, [this, capturedName]() { emit saveMapRequested(capturedName); });
        connect(createBtn(":/Images/Icons/convert.svg", tr("版本转化")), &QPushButton::clicked,
                this, [this, capturedName]() { emit saveConvertRequested(capturedName); });
    }

    // 光影设置（仅光影包类型显示）
    if (m_fileType == ShaderPack)
    {
        connect(createBtn(":/Images/Icons/instance_settings.svg", tr("光影设置")), &QPushButton::clicked,
                this, [this, capturedPath]() {
                    if (!m_instancePath.isEmpty())
                        emit shaderSettingsRequested(capturedPath);
                });
    }

    // 详情按钮
    connect(createBtn(":/Images/Icons/list.svg", tr("查看详情")), &QPushButton::clicked, this,
            [this, capturedPath, capturedName, entry]() {
                QString info = tr("名称: %1\n路径: %2\n大小: %3\n修改时间: %4")
                    .arg(capturedName)
                    .arg(capturedPath)
                    .arg(formatFileSize(entry.fileSize))
                    .arg(entry.lastModified.toString("yyyy-MM-dd hh:mm:ss"));
                AppMessageBox::information(this, tr("文件详情"), info);
            });

    // 截图查看按钮（仅截图类型显示）
    if (m_fileType == Screenshot)
    {
        connect(createBtn(":/Images/Icons/screenshot.svg", tr("查看截图")), &QPushButton::clicked,
                this, [capturedPath]() {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(capturedPath));
                });
    }

    // 打开文件夹按钮
    connect(createBtn(":/Images/Icons/folder.svg", tr("打开文件位置")), &QPushButton::clicked,
            this, [capturedPath]() {
                QFileInfo fi(capturedPath);
                QString dirPath = fi.isDir() ? capturedPath : fi.absolutePath();
                if (!dirPath.isEmpty())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
            });

    // 删除按钮
    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"));
    deleteBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    connect(deleteBtn, &QPushButton::clicked, this, [this, capturedPath, capturedName]() {
        AppMessageBox::StandardButton reply = AppMessageBox::question(
            this,
            tr("删除%1").arg(typeDisplayName()),
            tr("确定要删除 \"%1\" 吗？此操作不可撤销。").arg(capturedName),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);

        if (reply == AppMessageBox::Yes)
        {
            QFileInfo fi(capturedPath);
            if (fi.isDir())
                QDir(capturedPath).removeRecursively();
            else
                QFile::remove(capturedPath);
            refreshFileList();
        }
    });
    btnLayout->addStretch();
    bv->addLayout(btnLayout);

    cardLayout->addWidget(body);

    createSelectionOverlay(card);

    return card;
}

QWidget *InstanceFilePage::createSelectionOverlay(QWidget *card)
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

void InstanceFilePage::animateCardHighlight(int idx, bool selected)
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

QString InstanceFilePage::selectionOverlayStyle() const
{
    QColor c = ThemeManager::instance()->currentThemeColor();
    return QString("#cardSelectionOverlay { background-color: rgba(%1, %2, %3, 40); "
                   "border: 2px solid %4; border-radius: 6px; }")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.name());
}

void InstanceFilePage::onSearchTextChanged(const QString &text)
{
    m_currentSearch = text.trimmed();
    applyFilterAndSearch();
}

/* 按当前排序模式重排 m_fileList 并重建 SHA-1 → 索引映射 */
void InstanceFilePage::sortFileList()
{
    const int mode = m_sortMode;
    // 名称比较忽略 .disabled 后缀，禁用项与本体排在一起
    auto displayName = [](const FileEntry &e) {
        QString n = e.fileName;
        if (n.endsWith(".disabled", Qt::CaseInsensitive))
            n.chop(9);
        return n;
    };
    std::sort(m_fileList.begin(), m_fileList.end(),
              [mode, &displayName](const FileEntry &a, const FileEntry &b) {
        switch (mode)
        {
        case 1:  // 名称 Z→A
            return QString::compare(displayName(a), displayName(b), Qt::CaseInsensitive) > 0;
        case 2:  // 大小 大→小
            return a.fileSize > b.fileSize;
        case 3:  // 大小 小→大
            return a.fileSize < b.fileSize;
        case 4:  // 时间 新→旧
            return a.lastModified > b.lastModified;
        case 5:  // 时间 旧→新
            return a.lastModified < b.lastModified;
        default: // 名称 A→Z
            return QString::compare(displayName(a), displayName(b), Qt::CaseInsensitive) < 0;
        }
    });

    m_sha1ToIndex.clear();
    for (int i = 0; i < m_fileList.size(); ++i)
    {
        const QString &h = m_fileList.at(i).sha1Hash;
        if (!h.isEmpty())
            m_sha1ToIndex.insert(h, i);
    }
}

/* 排序变化：重排文件列表并重建卡片（索引随顺序变化，须整体重建） */
void InstanceFilePage::applySortOrder()
{
    if (m_updateInProgress)
        return;   // 更新队列持有索引，更新过程中不重排
    sortFileList();
    if (!m_fileList.isEmpty())
        rebuildCards();
}

void InstanceFilePage::onSelectAllClicked()
{
    m_allSelected = !m_allSelected;
    m_selectAllBtn->setText(m_allSelected ? tr("取消全选") : tr("全选"));

    m_selectedIndices.clear();

    if (m_allSelected)
    {
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            if (m_cardWidgets[i]->isVisible())
            {
                m_selectedIndices.insert(i);
                animateCardHighlight(i, true);
            }
        }
    }
    else
    {
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            animateCardHighlight(i, false);
        }
    }

    updateBottomBarState();
}

void InstanceFilePage::onOpenFolderClicked()
{
    if (m_instancePath.isEmpty())
        return;

    QString dirPath = m_instancePath + "/" + subDirName();
    QDir dir(dirPath);
    if (dir.exists())
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
    }
    else
    {
        AppMessageBox::information(this, tr("提示"),
            tr("%1目录不存在: %2").arg(typeDisplayName()).arg(dirPath));
    }
}

void InstanceFilePage::onPasteFileClicked()
{
    if (m_instancePath.isEmpty())
        return;

    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    if (!mimeData->hasUrls())
    {
        AppMessageBox::information(this, tr("粘贴%1").arg(typeDisplayName()),
            tr("剪贴板中没有文件，请先复制文件。"));
        return;
    }

    QString targetDir = m_instancePath + "/" + subDirName();
    QDir dir(targetDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    int copiedCount = 0;
    QStringList validSuffixes;
    switch (m_fileType)
    {
    case ResourcePack:
    case ShaderPack:
        validSuffixes = { ".zip" };
        break;
    case Screenshot:
        validSuffixes = { ".png", ".jpg", ".jpeg", ".webp" };
        break;
    case Save:
        // 存档是目录，粘贴整个文件夹
        break;
    }

    for (const QUrl &url : mimeData->urls())
    {
        QString srcPath = url.toLocalFile();
        if (srcPath.isEmpty())
            continue;

        QFileInfo fi(srcPath);

        if (m_fileType == Save)
        {
            // 存档：复制整个目录
            if (!fi.isDir())
                continue;

            QString destPath = targetDir + "/" + fi.fileName();
            if (QDir(destPath).exists())
            {
                int ret = AppMessageBox::question(this, tr("目录已存在"),
                    tr("存档 \"%1\" 已存在，是否覆盖？").arg(fi.fileName()),
                    AppMessageBox::Yes | AppMessageBox::No | AppMessageBox::Cancel);
                if (ret == AppMessageBox::Cancel)
                    break;
                if (ret == AppMessageBox::No)
                    continue;
                QDir(destPath).removeRecursively();
            }
            if (copyDir(srcPath, destPath))
            {
                copiedCount++;
            }
        }
        else
        {
            // 文件：检查后缀
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

            QString destPath = targetDir + "/" + fi.fileName();
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
    }

    if (copiedCount > 0)
    {
        m_statsLabel->setText(tr("已粘贴 %1 个%2，正在刷新列表...")
            .arg(copiedCount).arg(typeDisplayName()));
        refreshFileList();
    }
}

void InstanceFilePage::onCopyFileClicked()
{
    if (m_selectedIndices.isEmpty())
        return;

    QList<QUrl> urls;
    QStringList names;
    for (int idx : m_selectedIndices)
    {
        if (idx < 0 || idx >= m_fileList.size())
            continue;

        const FileEntry &entry = m_fileList.at(idx);
        QFileInfo fi(entry.filePath);
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

    m_statsLabel->setText(tr("已复制 %1 个%2").arg(urls.size()).arg(typeDisplayName()));
}

void InstanceFilePage::onDeleteSelectedClicked()
{
    if (m_selectedIndices.isEmpty())
        return;

    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("批量删除"),
        tr("确定要删除选中的 %1 个%2吗？此操作不可撤销。")
            .arg(m_selectedIndices.size()).arg(typeDisplayName()),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply != AppMessageBox::Yes)
        return;

    for (int idx : m_selectedIndices)
    {
        if (idx < 0 || idx >= m_fileList.size())
            continue;

        const FileEntry &entry = m_fileList.at(idx);
        QFileInfo fi(entry.filePath);
        if (fi.isDir())
        {
            QDir(entry.filePath).removeRecursively();
        }
        else
        {
            QFile::remove(entry.filePath);
        }
    }

    refreshFileList();
}

void InstanceFilePage::onDownloadSearchClicked()
{
    emit fileSearchRequested(m_currentSearch);
}

bool InstanceFilePage::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *card = qobject_cast<QWidget *>(watched);
    if (!card)
        return QWidget::eventFilter(watched, event);

    QString filePath = card->property("filePath").toString();
    if (filePath.isEmpty())
        return QWidget::eventFilter(watched, event);

    // 找到卡片在 m_cardWidgets 中的索引
    int idx = -1;
    for (int i = 0; i < m_cardWidgets.size(); ++i)
    {
        if (m_cardWidgets[i] == card)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonDblClick)
    {
        // 双击打开文件/文件夹
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
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

    if (event->type() == QEvent::ContextMenu)
    {
        QContextMenuEvent *ctxEvent = static_cast<QContextMenuEvent *>(event);
        QMenu menu(this);

        QAction *openAction = menu.addAction(tr("打开"));
        QAction *openFolderAction = menu.addAction(tr("打开文件位置"));
        QAction *copyPathAction = menu.addAction(tr("复制文件路径"));
        appendCategoryMenu(&menu, card->property("fileName").toString(), filePath);
        menu.addSeparator();
        QAction *detailAction = menu.addAction(tr("文件详情"));
        menu.addSeparator();
        QAction *deleteAction = menu.addAction(tr("删除"));

        QAction *chosen = menu.exec(ctxEvent->globalPos());

        if (chosen == openAction)
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        }
        else if (chosen == openFolderAction)
        {
            QFileInfo fi(filePath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.isDir() ? filePath : fi.absolutePath()));
        }
        else if (chosen == copyPathAction)
        {
            QApplication::clipboard()->setText(filePath);
        }
        else if (chosen == detailAction && idx >= 0 && idx < m_fileList.size())
        {
            const FileEntry &entry = m_fileList.at(idx);
            QString info = tr("名称: %1\n路径: %2\n大小: %3\n修改时间: %4")
                .arg(entry.fileName)
                .arg(entry.filePath)
                .arg(formatFileSize(entry.fileSize))
                .arg(entry.lastModified.toString("yyyy-MM-dd hh:mm:ss"));
            AppMessageBox::information(this, tr("文件详情"), info);
        }
        else if (chosen == deleteAction)
        {
            QString fileName = card->property("fileName").toString();
            AppMessageBox::StandardButton reply = AppMessageBox::question(
                this, tr("删除%1").arg(typeDisplayName()),
                tr("确定要删除 \"%1\" 吗？此操作不可撤销。").arg(fileName),
                AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);

            if (reply == AppMessageBox::Yes)
            {
                QFileInfo fi(filePath);
                if (fi.isDir())
                    QDir(filePath).removeRecursively();
                else
                    QFile::remove(filePath);
                refreshFileList();
            }
        }
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

void InstanceFilePage::clearCards()
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
    m_selectedIndices.clear();
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

void InstanceFilePage::placeCards()
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

        QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        m_cardGridLayout->addItem(spacer, row, 0);
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

void InstanceFilePage::showLoading(bool show)
{
    if (show)
    {
        m_loadingOverlay->showOverlay(tr("正在扫描%1...").arg(typeDisplayName()));
    }
    else
    {
        m_loadingOverlay->hideOverlay();
    }
}

void InstanceFilePage::showEmptyHint(bool show)
{
    if (show)
        m_emptyLabel->setText(tr("暂无%1").arg(typeDisplayName()));
    m_emptyLabel->setVisible(show);
    // 空态时隐藏列表滚动区，让提示文字占据列表区域
    m_scrollArea->setVisible(!show);
    if (!show)
        m_downloadSearchBtn->hide();
}

void InstanceFilePage::updateBottomBarState()
{
    bool isPackType = (m_fileType == ResourcePack || m_fileType == ShaderPack);
    m_checkUpdateBtn->setVisible(isPackType);

    bool hasSelection = !m_selectedIndices.isEmpty();
    m_copyBtn->setVisible(hasSelection);
    m_detailBtn->setVisible(hasSelection);
    m_deleteBtn->setVisible(hasSelection);
    m_pasteBtn->setVisible(!hasSelection);

    // 更新按钮：仅当选中了有更新的文件时显示
    bool hasUpdatable = false;
    if (hasSelection && isPackType)
    {
        for (int idx : m_selectedIndices)
        {
            if (idx < 0 || idx >= m_fileList.size())
                continue;
            const QString &h = m_fileList.at(idx).sha1Hash;
            if (!h.isEmpty() && m_updateCache.contains(h))
            {
                hasUpdatable = true;
                break;
            }
        }
    }
    m_updateBtn->setVisible(hasUpdatable);
    m_updateBtn->setEnabled(!m_updateInProgress);
}

void InstanceFilePage::toggleCardSelection(int idx)
{
    if (idx < 0 || idx >= m_cardWidgets.size())
        return;

    if (m_allSelected)
    {
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }

    if (m_selectedIndices.contains(idx))
    {
        m_selectedIndices.remove(idx);
        animateCardHighlight(idx, false);
    }
    else
    {
        m_selectedIndices.insert(idx);
        animateCardHighlight(idx, true);
    }

    updateBottomBarState();
}

int InstanceFilePage::cardIndexAtGlobal(const QPoint &globalPos) const
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

void InstanceFilePage::selectCardDuringDrag(int idx)
{
    if (idx < 0 || idx >= m_cardWidgets.size())
        return;

    if (m_selectedIndices.contains(idx))
        return;

    if (m_allSelected)
    {
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }

    m_selectedIndices.insert(idx);
    animateCardHighlight(idx, true);
    updateBottomBarState();
}

void InstanceFilePage::deselectCardDuringDrag(int idx)
{
    if (idx < 0 || idx >= m_cardWidgets.size())
        return;

    if (!m_selectedIndices.contains(idx))
        return;

    if (m_allSelected)
    {
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }

    m_selectedIndices.remove(idx);
    animateCardHighlight(idx, false);
    updateBottomBarState();
}

void InstanceFilePage::deselectAllCards()
{
    for (int idx : m_selectedIndices)
    {
        if (idx >= 0 && idx < m_cardWidgets.size())
            animateCardHighlight(idx, false);
    }
    m_selectedIndices.clear();
    m_allSelected = false;
    m_selectAllBtn->setText(tr("全选"));
    updateBottomBarState();
}

void InstanceFilePage::applyFilterAndSearch()
{
    int visibleCount = 0;
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

    for (int i = 0; i < m_cardWidgets.size() && i < m_fileList.size(); ++i)
    {
        bool visible = true;

        const FileEntry &entry = m_fileList.at(i);
        if (!m_currentSearch.isEmpty())
        {
            visible = entry.fileName.contains(m_currentSearch, Qt::CaseInsensitive);
        }

        // 状态筛选（截图无禁用概念，m_statusCombo 不存在时恒为 0）
        if (visible && m_statusFilter == 1 && !entry.enabled)
            visible = false;
        else if (visible && m_statusFilter == 2 && entry.enabled)
            visible = false;

        // 时间筛选（按修改时间）
        if (visible && timeCutoff.isValid() && entry.lastModified < timeCutoff)
            visible = false;

        // 大小筛选（存档为递归统计的目录大小）
        if (visible && m_sizeFilter > 0)
        {
            if (m_sizeFilter == 1 && entry.fileSize >= k10MB)
                visible = false;
            else if (m_sizeFilter == 2
                     && (entry.fileSize < k10MB || entry.fileSize > k100MB))
                visible = false;
            else if (m_sizeFilter == 3 && entry.fileSize <= k100MB)
                visible = false;
        }

        // 分类筛选（手动分类 + 智能筛选哨兵值）
        if (visible && !m_currentCategoryId.isEmpty())
        {
            if (m_currentCategoryId == QStringLiteral("__uncategorized__"))
            {
                visible = m_categoryManager->categoryOf(entry.fileName).isEmpty();
            }
            else if (m_currentCategoryId == QStringLiteral("__updatable__"))
            {
                visible = !entry.sha1Hash.isEmpty()
                          && m_updateCache.contains(entry.sha1Hash);
            }
            else if (m_currentCategoryId == QStringLiteral("__recent__"))
            {
                visible = entry.lastModified >= now.addDays(-7);
            }
            else if (m_currentCategoryId == QStringLiteral("__large__"))
            {
                visible = entry.fileSize > k100MB;
            }
            else
            {
                visible = (m_categoryManager->categoryOf(entry.fileName)
                           == m_currentCategoryId);
            }
        }

        m_cardWidgets[i]->setVisible(visible);

        if (visible)
        {
            visibleCount++;
        }
    }

    const bool anyFilterActive = !m_currentSearch.isEmpty()
        || !m_currentCategoryId.isEmpty()
        || m_statusFilter != 0 || m_timeFilter != 0 || m_sizeFilter != 0;
    if (anyFilterActive)
    {
        m_statsLabel->setText(tr("显示 %1 个%2 (共 %3 个)")
            .arg(visibleCount).arg(typeDisplayName()).arg(m_fileList.size()));
    }
    else
    {
        m_statsLabel->setText(tr("共 %1 个%2").arg(m_fileList.size()).arg(typeDisplayName()));
    }

    // 搜索结果为空时显示提示；截图为本地生成内容，不支持在线搜索
    // 仅搜索词激活时引导前往下载页；筛选条件隐藏的资源实际存在，不引导
    if (visibleCount == 0 && m_fileList.size() > 0 && m_fileType != Screenshot
        && !m_currentSearch.isEmpty())
    {
        m_emptyLabel->setText(tr("没有找到匹配的%1").arg(typeDisplayName()));
        m_emptyLabel->setVisible(true);
        m_downloadSearchBtn->setVisible(true);
    }
    else if (visibleCount == 0 && m_fileList.size() > 0 && !m_currentSearch.isEmpty())
    {
        // 截图搜索无结果：给提示但不去下载页
        m_emptyLabel->setText(tr("没有找到匹配的%1").arg(typeDisplayName()));
        m_emptyLabel->setVisible(true);
        m_downloadSearchBtn->hide();
    }
    else if (visibleCount == 0 && m_fileList.size() > 0)
    {
        // 纯筛选（分类/状态/时间/大小）全部被隐藏：提示但不引导去下载页
        m_emptyLabel->setText(tr("没有符合筛选条件的%1").arg(typeDisplayName()));
        m_emptyLabel->setVisible(true);
        m_downloadSearchBtn->hide();
    }
    else
    {
        if (m_fileList.isEmpty())
            m_emptyLabel->setText(tr("暂无%1").arg(typeDisplayName()));
        m_emptyLabel->setVisible(m_fileList.isEmpty());
        m_downloadSearchBtn->hide();
    }
    // 提示可见时列表为空，隐藏滚动区让提示占据列表区域
    m_scrollArea->setVisible(!m_emptyLabel->isVisible());
}

QString InstanceFilePage::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024)
        return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString InstanceFilePage::computeFileSha1(const QString &filePath)
{
    if (m_sha1Cache.contains(filePath))
        return m_sha1Cache.value(filePath);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QByteArray data = file.readAll();
    QString sha1 = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
    m_sha1Cache.insert(filePath, sha1);
    return sha1;
}

void InstanceFilePage::onCheckUpdatesClicked()
{
    if (m_fileType != ResourcePack && m_fileType != ShaderPack)
        return;
    if (m_fileList.isEmpty())
        return;

    resetUpdateState();

    // 计算当前类型所有文件的 SHA-1（懒计算，缓存复用）
    showLoading(true);
    m_statsLabel->setText(tr("正在计算文件哈希..."));
    m_sha1ToIndex.clear();
    QList<QString> sha1s;
    for (int i = 0; i < m_fileList.size(); ++i)
    {
        FileEntry &entry = m_fileList[i];
        if (entry.sha1Hash.isEmpty())
            entry.sha1Hash = computeFileSha1(entry.filePath);
        if (entry.sha1Hash.isEmpty())
            continue;
        m_sha1ToIndex.insert(entry.sha1Hash, i);
        sha1s.append(entry.sha1Hash);
    }
    showLoading(false);

    sha1s.removeDuplicates();
    sha1s.sort();

    if (sha1s.isEmpty())
    {
        m_statsLabel->setText(tr("没有可检查更新的%1").arg(typeDisplayName()));
        return;
    }

    m_checkUpdateBtn->setEnabled(false);
    m_checkUpdateBtn->setText(tr("检查中..."));
    m_statsLabel->setText(tr("正在检查更新..."));

    // 第一步：批量哈希匹配，得到已安装版本（一次请求）
    m_updateCheckApi->matchHashes(sha1s);
}

void InstanceFilePage::onHashesMatched(const QMap<QString, ModVersionFile> &versions)
{
    m_installedBySha1 = versions;

    // 第二步：批量获取最新版本。资源包/光影包不按加载器与游戏版本过滤
    m_updateCheckApi->checkModUpdates(m_sha1ToIndex.keys(), QStringList(), QStringList());
}

void InstanceFilePage::onUpdatesChecked(const QMap<QString, ModVersionFile> &versions)
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

    m_checkUpdateBtn->setEnabled(true);
    m_checkUpdateBtn->setText(tr("检查更新"));

    if (m_updateCheckFound > 0)
    {
        m_statsLabel->setText(tr("检查完成，发现 %1 个%2有更新")
            .arg(m_updateCheckFound).arg(typeDisplayName()));
    }
    else
    {
        m_statsLabel->setText(tr("所有%1已是最新版本").arg(typeDisplayName()));
    }

    // 智能筛选「可更新」激活时，按新检查结果立即刷新可见性
    if (m_currentCategoryId == QStringLiteral("__updatable__"))
        applyFilterAndSearch();

    updateBottomBarState();
}

void InstanceFilePage::onUpdateCheckBatchFailed(const QString &error)
{
    Q_UNUSED(error);
    m_installedBySha1.clear();
    m_checkUpdateBtn->setEnabled(true);
    m_checkUpdateBtn->setText(tr("检查更新"));
    m_statsLabel->setText(tr("检查更新失败"));
    updateBottomBarState();
}

void InstanceFilePage::resetUpdateState()
{
    // 清除之前标记的更新芯片
    for (QWidget *card : m_cardWidgets)
    {
        QLabel *chip = card->findChild<QLabel *>("fileUpdateChip");
        if (chip)
            chip->hide();
    }
    m_installedBySha1.clear();
    m_updateCheckFound = 0;
}

void InstanceFilePage::markCardHasUpdate(int index, const ModVersionFile &target)
{
    if (index < 0 || index >= m_cardWidgets.size())
        return;

    QWidget *card = m_cardWidgets[index];
    QLabel *chip = card->findChild<QLabel *>("fileUpdateChip");
    if (!chip)
        return;

    chip->show();
    QString targetVersion = target.version.isEmpty() ? target.fileName : target.version;
    chip->setToolTip(tr("可更新到 %1").arg(targetVersion));
}

void InstanceFilePage::onUpdateSelectedClicked()
{
    if (m_updateInProgress)
        return;

    m_updateQueue.clear();
    for (int idx : m_selectedIndices)
    {
        if (idx < 0 || idx >= m_fileList.size())
            continue;
        const FileEntry &entry = m_fileList.at(idx);
        if (!entry.sha1Hash.isEmpty() && m_updateCache.contains(entry.sha1Hash))
            m_updateQueue.append(idx);
    }

    if (m_updateQueue.isEmpty())
        return;

    m_updateInProgress = true;
    m_updateBtn->setEnabled(false);
    m_updateBtn->setText(tr("更新中..."));
    m_statsLabel->setText(tr("正在更新 %1 个%2...")
        .arg(m_updateQueue.size()).arg(typeDisplayName()));
    processNextModUpdate();
}

void InstanceFilePage::processNextModUpdate()
{
    if (m_updateQueue.isEmpty())
    {
        m_updateInProgress = false;
        m_updateBtn->setEnabled(true);
        m_updateBtn->setText(tr("更新"));
        m_statsLabel->setText(tr("%1更新完成").arg(typeDisplayName()));
        refreshFileList();
        return;
    }

    int idx = m_updateQueue.takeFirst();
    if (idx < 0 || idx >= m_fileList.size())
    {
        processNextModUpdate();
        return;
    }

    const FileEntry entry = m_fileList.at(idx);
    if (entry.sha1Hash.isEmpty() || !m_updateCache.contains(entry.sha1Hash))
    {
        processNextModUpdate();
        return;
    }

    const ModVersionFile target = m_updateCache.value(entry.sha1Hash);
    if (target.downloadUrl.isEmpty())
    {
        processNextModUpdate();
        return;
    }

    // 目标文件名需为 .zip，否则回退到下载 URL 文件名或本地文件名
    QString fileName = target.fileName;
    if (fileName.isEmpty() || !fileName.endsWith(".zip", Qt::CaseInsensitive))
    {
        QUrl url(target.downloadUrl);
        fileName = url.fileName();
        if (fileName.isEmpty() || fileName.contains('?')
            || !fileName.endsWith(".zip", Qt::CaseInsensitive))
        {
            fileName = entry.fileName;
        }
    }

    // 注册下载任务
    QString instanceName = QFileInfo(m_instancePath).fileName();
    QString taskId = DownloadTaskManager::instance()->addTask(
        instanceName, m_instancePath, QString(), QStringList());

    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading,
        tr("更新%1: ").arg(typeDisplayName()) + entry.fileName);
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
        [this, taskId, downloader, entry, fileName](const QString &destPath)
        {
            DownloadTaskManager::instance()->updateTaskStage(taskId, DownloadStage::Completed, 100);
            DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, 100);
            DownloadTaskManager::instance()->updateTaskStatus(
                taskId, DownloadTaskStatus::Completed, tr("更新完成"));
            downloader->deleteLater();
            m_updateCache.remove(entry.sha1Hash);
            m_sha1Cache.remove(entry.filePath);
            removeOldFile(entry, destPath);
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

    ContentType ct = (m_fileType == ResourcePack)
        ? ContentType::ResourcePack : ContentType::ShaderPack;
    downloader->downloadToInstance(target.downloadUrl, fileName, m_instancePath, ct);
}

void InstanceFilePage::removeOldFile(const FileEntry &entry, const QString &newPath)
{
    if (entry.filePath.isEmpty())
        return;

    // 新旧文件同名时，ContentDownloader 已覆盖写入，无需删除
    if (QFileInfo(entry.filePath).absoluteFilePath()
        == QFileInfo(newPath).absoluteFilePath())
        return;

    if (QFile::exists(entry.filePath))
        QFile::remove(entry.filePath);
}

// 拖放悬停高亮：给滚动区加主题色虚线边框，提示可松手导入
void InstanceFilePage::setDropHighlight(bool on)
{
    if (!m_scrollArea)
        return;
    if (on)
    {
        const QColor tc = ThemeManager::instance()->currentThemeColor();
        m_scrollArea->setStyleSheet(QString(
            "QScrollArea#fileListScroll { border: 2px dashed %1; border-radius: 8px; }")
            .arg(tc.name()));
    }
    else
    {
        m_scrollArea->setStyleSheet(QString());
    }
}

void InstanceFilePage::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls())
    {
        event->ignore();
        return;
    }
    // 方块盒子更新安装包交给主窗口拖入安装，本页不拦截
    const auto dropUrls = event->mimeData()->urls();
    for (const QUrl &u : dropUrls)
    {
        if (u.isLocalFile() && UpdateChecker::isUpdatePackageFile(u.toLocalFile()))
        {
            event->ignore();
            return;
        }
    }
    event->acceptProposedAction();
    setDropHighlight(true);
}

void InstanceFilePage::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void InstanceFilePage::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDropHighlight(false);
    QWidget::dragLeaveEvent(event);
}

void InstanceFilePage::dropEvent(QDropEvent *event)
{
    setDropHighlight(false);
    if (event->mimeData()->hasUrls())
    {
        handleDroppedFiles(event->mimeData()->urls());
        event->acceptProposedAction();
    }
    else
    {
        event->ignore();
    }
}

void InstanceFilePage::handleDroppedFiles(const QList<QUrl> &urls)
{
    if (m_instancePath.isEmpty())
    {
        m_dragStatusLabel->setText(tr("实例路径未设置"));
        m_dragStatusLabel->setStyleSheet("color: #F44336;");
        m_dragStatusLabel->show();
        return;
    }

    QString targetDir = m_instancePath + "/" + subDirName();
    QDir dir(targetDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    int copiedCount = 0;
    int skippedCount = 0;
    QStringList validSuffixes;

    switch (m_fileType)
    {
    case ResourcePack:
    case ShaderPack:
        validSuffixes = { ".zip" };
        break;
    case Screenshot:
        validSuffixes = { ".png", ".jpg", ".jpeg", ".webp" };
        break;
    case Save:
        // 存档是目录，不需要后缀过滤
        break;
    }

    for (const QUrl &url : urls)
    {
        QString srcPath = url.toLocalFile();
        if (srcPath.isEmpty())
            continue;

        QFileInfo fi(srcPath);

        if (m_fileType == Save)
        {
            // 存档：复制整个目录
            if (!fi.isDir())
            {
                skippedCount++;
                continue;
            }

            QString destPath = targetDir + "/" + fi.fileName();
            if (QDir(destPath).exists())
            {
                // 跳过已存在的目录
                skippedCount++;
                continue;
            }
            if (copyDir(srcPath, destPath))
            {
                copiedCount++;
            }
        }
        else
        {
            // 文件：检查后缀
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
            {
                skippedCount++;
                continue;
            }

            QString destPath = targetDir + "/" + fi.fileName();
            if (QFile::exists(destPath))
            {
                // 跳过已存在的文件
                skippedCount++;
                continue;
            }

            if (QFile::copy(srcPath, destPath))
            {
                copiedCount++;
            }
        }
    }

    // 显示结果
    QString resultText;
    if (copiedCount > 0 && skippedCount > 0)
    {
        resultText = tr("已添加 %1 个%2，跳过 %3 个")
            .arg(copiedCount).arg(typeDisplayName()).arg(skippedCount);
        m_dragStatusLabel->setStyleSheet("color: #4CAF50;");
    }
    else if (copiedCount > 0)
    {
        resultText = tr("已成功添加 %1 个%2").arg(copiedCount).arg(typeDisplayName());
        m_dragStatusLabel->setStyleSheet("color: #4CAF50;");
    }
    else if (skippedCount > 0)
    {
        resultText = tr("跳过 %1 个文件（格式不匹配或已存在）").arg(skippedCount);
        m_dragStatusLabel->setStyleSheet("color: #FF9800;");
    }
    else
    {
        resultText = tr("没有有效的文件被添加");
        m_dragStatusLabel->setStyleSheet("color: #F44336;");
    }

    m_dragStatusLabel->setText(resultText);
    m_dragStatusLabel->show();

    // 如果有文件被添加，刷新普通模式的文件列表
    if (copiedCount > 0)
    {
        refreshFileList();
    }
}

// 递归复制目录
static bool copyDir(const QString &srcPath, const QString &dstPath)
{
    QDir srcDir(srcPath);
    if (!srcDir.exists())
        return false;

    QDir dstDir(dstPath);
    if (!dstDir.exists())
    {
        dstDir.mkpath(".");
    }

    for (const QFileInfo &fi : srcDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
    {
        QString destPath = dstPath + "/" + fi.fileName();
        if (fi.isDir())
        {
            if (!copyDir(fi.absoluteFilePath(), destPath))
                return false;
        }
        else
        {
            if (!QFile::copy(fi.absoluteFilePath(), destPath))
                return false;
        }
    }
    return true;
}

// ============================
// 本地资源分类筛选
// ============================

void InstanceFilePage::reloadCategoryCombo()
{
    if (!m_categoryCombo)
        return;

    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();
    m_categoryCombo->addItem(tr("全部"), QString());

    // 智能筛选：由文件属性直接判定，无需手动归类
    if (m_fileType == ResourcePack || m_fileType == ShaderPack)
        m_categoryCombo->addItem(tr("可更新"), QStringLiteral("__updatable__"));
    m_categoryCombo->addItem(tr("最近修改"), QStringLiteral("__recent__"));
    m_categoryCombo->addItem(tr("大文件"), QStringLiteral("__large__"));
    m_categoryCombo->insertSeparator(m_categoryCombo->count());

    const QList<LocalResourceCategory> cats = m_categoryManager->categories();
    for (const LocalResourceCategory &c : cats)
    {
        m_categoryCombo->addItem(c.name, c.id);
    }
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

void InstanceFilePage::onCategoryFilterChanged(int index)
{
    if (!m_categoryCombo)
        return;
    m_currentCategoryId = m_categoryCombo->itemData(index).toString();

    // 智能筛选「可更新」依赖 SHA-1 → 更新缓存：未算过哈希时先懒计算
    //（计算结果进 m_sha1Cache，后续检查更新可复用）
    if (m_currentCategoryId == QStringLiteral("__updatable__")
        && (m_fileType == ResourcePack || m_fileType == ShaderPack)
        && !m_fileList.isEmpty())
    {
        bool needHash = false;
        for (const FileEntry &entry : m_fileList)
        {
            if (entry.sha1Hash.isEmpty())
            {
                needHash = true;
                break;
            }
        }
        if (needHash)
        {
            showLoading(true);
            for (FileEntry &entry : m_fileList)
            {
                if (entry.sha1Hash.isEmpty())
                    entry.sha1Hash = computeFileSha1(entry.filePath);
            }
            showLoading(false);
            // 只回填哈希、不重排（卡片已按原顺序创建），重建索引映射即可
            m_sha1ToIndex.clear();
            for (int i = 0; i < m_fileList.size(); ++i)
            {
                const QString &h = m_fileList.at(i).sha1Hash;
                if (!h.isEmpty())
                    m_sha1ToIndex.insert(h, i);
            }
        }
    }

    applyFilterAndSearch();
}

void InstanceFilePage::onCategoryManageClicked()
{
    LocalCategoryDialog dialog(m_categoryManager, this);
    dialog.exec();
}

void InstanceFilePage::onStatusFilterChanged(int index)
{
    if (!m_statusCombo)
        return;
    m_statusFilter = m_statusCombo->itemData(index).toInt();
    applyFilterAndSearch();
}

void InstanceFilePage::onTimeFilterChanged(int index)
{
    if (!m_timeCombo)
        return;
    m_timeFilter = m_timeCombo->itemData(index).toInt();
    applyFilterAndSearch();
}

void InstanceFilePage::onSizeFilterChanged(int index)
{
    if (!m_sizeCombo)
        return;
    m_sizeFilter = m_sizeCombo->itemData(index).toInt();
    applyFilterAndSearch();
}

void InstanceFilePage::onSortChanged(int index)
{
    if (!m_sortCombo)
        return;
    const int newMode = m_sortCombo->itemData(index).toInt();
    if (newMode == m_sortMode)
        return;
    m_sortMode = newMode;
    applySortOrder();
}

void InstanceFilePage::onCategoriesChanged()
{
    reloadCategoryCombo();
    refreshCardCategoryChips();
    applyFilterAndSearch();
}

void InstanceFilePage::onAssignmentsChanged()
{
    refreshCardCategoryChips();
    applyFilterAndSearch();
}

void InstanceFilePage::refreshCardCategoryChips()
{
    for (int i = 0; i < m_cardWidgets.size() && i < m_fileList.size(); ++i)
    {
        const QString catName = m_categoryManager->categoryNameOf(m_fileList.at(i).fileName);
        const QList<QLabel *> labels = m_cardWidgets.at(i)->findChildren<QLabel *>();
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

void InstanceFilePage::appendCategoryMenu(QMenu *menu, const QString &fileName, const QString &filePath)
{
    // ── 设置分类子菜单 ──
    QMenu *categoryMenu = menu->addMenu(tr("设置分类"));
    const QString currentCat = m_categoryManager->categoryOf(fileName);
    const QList<LocalResourceCategory> cats = m_categoryManager->categories();
    for (const LocalResourceCategory &c : cats)
    {
        QAction *action = categoryMenu->addAction(c.name);
        action->setCheckable(true);
        action->setChecked(currentCat == c.id);
        connect(action, &QAction::triggered, this,
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
    connect(clearAction, &QAction::triggered, this,
        [this, filePath]() {
            m_categoryManager->assignCategory(filePath, QString());
        });
    categoryMenu->addSeparator();
    QAction *newCatAction = categoryMenu->addAction(tr("新建分类…"));
    connect(newCatAction, &QAction::triggered, this,
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