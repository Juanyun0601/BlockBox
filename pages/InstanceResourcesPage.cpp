/**
 * @file   InstanceResourcesPage.cpp
 * @brief  实例助手 - 资源管理页面实现
 * @author BlockBox Team
 * @date   2026-07-21
 */
#include "InstanceResourcesPage.h"
#include "ResourcesPage.h"
#include "components/BlurLoadingOverlay.h"
#include "components/LocalCategoryDialog.h"
#include "components/FavoriteFolderDialog.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/LocalCategoryManager.h"
#include "utils/mod/ModNameFetcher.h"
#include "utils/mod/ModScanner.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEasingCurve>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include "components/AppMessageBox.h"
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmapCache>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSpacerItem>
#include <QUrl>
#include <QVBoxLayout>

namespace {
/** 递归复制目录，用于存档粘贴 */
bool copyDir(const QString &srcPath, const QString &dstPath)
{
    QDir srcDir(srcPath);
    if (!srcDir.exists())
    {
        return false;
    }
    QDir dstDir(dstPath);
    if (!dstDir.exists())
    {
        dstDir.mkpath(QStringLiteral("."));
    }
    for (const QFileInfo &fi : srcDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
    {
        QString destPath = dstPath + QStringLiteral("/") + fi.fileName();
        if (fi.isDir())
        {
            if (!copyDir(fi.absoluteFilePath(), destPath))
            {
                return false;
            }
        }
        else
        {
            if (!QFile::copy(fi.absoluteFilePath(), destPath))
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace

InstanceResourcesPage::InstanceResourcesPage(QWidget *parent)
    : QWidget(parent)
    , m_tabModsBtn(nullptr)
    , m_tabSavesBtn(nullptr)
    , m_tabScreenshotsBtn(nullptr)
    , m_tabResourcePacksBtn(nullptr)
    , m_tabShaderPacksBtn(nullptr)
    , m_tabBar(nullptr)
    , m_tabHighlight(nullptr)
    , m_tabAnim(nullptr)
    , m_highlightPos(0)
    , m_tabIndicator(nullptr)
    , m_searchEdit(nullptr)
    , m_statsLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardLayout(nullptr)
    , m_loadingOverlay(nullptr)
    , m_emptyLabel(nullptr)
    , m_categoryManager(nullptr)
    , m_categoryCombo(nullptr)
    , m_categoryManageBtn(nullptr)
    , m_bottomBar(nullptr)
    , m_selectAllBtn(nullptr)
    , m_openFolderBtn(nullptr)
    , m_pasteBtn(nullptr)
    , m_detailBtn(nullptr)
    , m_currentType(TypeMods)
    , m_isLoading(false)
    , m_allSelected(false)
    , m_scanner(nullptr)
    , m_nameFetcher(nullptr)
{
    m_scanner = new ModScanner(this);
    m_nameFetcher = new ModNameFetcher(this);
    m_categoryManager = new LocalCategoryManager(this);
    initUI();
    setupConnections();

    // 指示条位移动画：通过 Q_PROPERTY(highlightPos) 驱动
    m_tabAnim = new QPropertyAnimation(this, "highlightPos", this);
    m_tabAnim->setDuration(220);
    m_tabAnim->setEasingCurve(QEasingCurve::OutCubic);

    // 应用主题色样式（按钮选中态、指示条颜色）
    applyTabThemeStyles();
}

InstanceResourcesPage::~InstanceResourcesPage()
{
}

void InstanceResourcesPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // ---- 顶部 Tab 切换条 ----
    // 容器使用相对布局，指示条 m_tabHighlight 作为子控件置于按钮下方，
    // 切换 Tab 时通过 QPropertyAnimation 平滑过渡其 X 坐标。
    m_tabBar = new QWidget(this);
    m_tabBar->setObjectName("resourcesTabBar");
    m_tabBar->setFixedHeight(38);

    QHBoxLayout *tabLayout = new QHBoxLayout(m_tabBar);
    tabLayout->setContentsMargins(0, 4, 0, 6);
    tabLayout->setSpacing(4);

    // 通过 createTabButton 统一创建，便于维护样式与索引映射
    m_tabModsBtn = createTabButton(tr("模组"), TypeMods);
    m_tabSavesBtn = createTabButton(tr("存档"), TypeSave);
    m_tabScreenshotsBtn = createTabButton(tr("截图"), TypeScreenshot);
    m_tabResourcePacksBtn = createTabButton(tr("资源包"), TypeResourcePack);
    m_tabShaderPacksBtn = createTabButton(tr("光影包"), TypeShaderPack);

    m_tabButtons = {m_tabModsBtn, m_tabSavesBtn, m_tabScreenshotsBtn,
                    m_tabResourcePacksBtn, m_tabShaderPacksBtn};

    for (QPushButton *btn : m_tabButtons)
    {
        tabLayout->addWidget(btn);
    }
    tabLayout->addStretch();

    // 选中指示条：默认宽度与首个按钮对齐，位于 Tab 栏底部
    m_tabHighlight = new QWidget(m_tabBar);
    m_tabHighlight->setObjectName("resourcesTabHighlight");
    m_tabHighlight->setFixedHeight(3);
    m_tabHighlight->raise();

    mainLayout->addWidget(m_tabBar);

    // ---- 搜索/分类条：微透明玻璃态卡片 ----
    QWidget *filterCard = new QWidget(this);
    filterCard->setObjectName("filterCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索模组名称..."));
    m_searchEdit->setFixedHeight(32);
    m_searchEdit->setClearButtonEnabled(true);
    cardLayout->addWidget(m_searchEdit);

    // ---- 分类筛选行（本地资源手动分类）----
    QHBoxLayout *categoryRow = new QHBoxLayout();
    categoryRow->setSpacing(6);

    QLabel *categoryLabel = new QLabel(tr("分类:"), this);
    categoryLabel->setObjectName("filterLabel");
    categoryRow->addWidget(categoryLabel);

    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->setObjectName("categoryCombo");
    m_categoryCombo->setFixedHeight(28);
    m_categoryCombo->setMinimumWidth(130);
    m_categoryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_categoryCombo->setMinimumContentsLength(5);
    reloadCategoryCombo();
    categoryRow->addWidget(m_categoryCombo);

    m_categoryManageBtn = new QPushButton(tr("管理分类"), this);
    m_categoryManageBtn->setObjectName("categoryManageBtn");
    m_categoryManageBtn->setFixedHeight(28);
    m_categoryManageBtn->setCursor(Qt::PointingHandCursor);
    categoryRow->addWidget(m_categoryManageBtn);

    // ---- 统计行 ----
    m_statsLabel = new QLabel(this);
    m_statsLabel->setObjectName("statusLabel");
    categoryRow->addWidget(m_statsLabel, 1);
    cardLayout->addLayout(categoryRow);
    mainLayout->addWidget(filterCard);

    // ---- 卡片滚动区域 ----
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_cardContainer = new QWidget(m_scrollArea);
    m_cardLayout = new QVBoxLayout(m_cardContainer);
    m_cardLayout->setContentsMargins(0, 0, 0, 0);
    m_cardLayout->setSpacing(6);
    m_cardLayout->addStretch(); // 默认占位 stretch，让卡片在顶部对齐
    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    // 空提示放在列表区域内（无资源时显示在卡片本该出现的位置）
    m_emptyLabel = new QLabel(tr("暂无模组"), this);
    m_emptyLabel->setObjectName("modPlaceholderLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    mainLayout->addWidget(m_emptyLabel, 1);

    // ---- 底部操作栏 ----
    m_bottomBar = new QWidget(this);
    m_bottomBar->setObjectName("modBottomBar");
    m_bottomBar->setFixedHeight(42);

    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(4, 4, 4, 4);
    bottomLayout->setSpacing(6);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());

    m_selectAllBtn = new QPushButton(tr("全选"), m_bottomBar);
    m_selectAllBtn->setObjectName("bottomActionBtn");
    m_selectAllBtn->setCursor(Qt::PointingHandCursor);

    m_openFolderBtn = new QPushButton(tr("打开文件夹"), m_bottomBar);
    m_openFolderBtn->setObjectName("bottomActionBtn");
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    m_openFolderBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/folder.svg", themeColor, 16));
    m_openFolderBtn->setIconSize(QSize(16, 16));

    m_pasteBtn = new QPushButton(tr("粘贴"), m_bottomBar);
    m_pasteBtn->setObjectName("bottomActionBtn");
    m_pasteBtn->setCursor(Qt::PointingHandCursor);
    m_pasteBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/nav_import.svg", themeColor, 16));
    m_pasteBtn->setIconSize(QSize(16, 16));

    m_detailBtn = new QPushButton(tr("详情"), m_bottomBar);
    m_detailBtn->setObjectName("bottomActionBtn");
    m_detailBtn->setCursor(Qt::PointingHandCursor);
    m_detailBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/list.svg", themeColor, 16));
    m_detailBtn->setIconSize(QSize(16, 16));
    m_detailBtn->hide();

    bottomLayout->addWidget(m_selectAllBtn);
    bottomLayout->addWidget(m_openFolderBtn);
    bottomLayout->addWidget(m_pasteBtn);
    bottomLayout->addWidget(m_detailBtn);
    bottomLayout->addStretch();
    mainLayout->addWidget(m_bottomBar);

    // ---- 加载遮罩 ----
    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();
}

void InstanceResourcesPage::setupConnections()
{
    // Tab 按钮统一连接到 switchTab，由其处理选中态、指示条动画与列表刷新
    connect(m_tabModsBtn, &QPushButton::clicked, this, [this]() { switchTab(TypeMods); });
    connect(m_tabSavesBtn, &QPushButton::clicked, this, [this]() { switchTab(TypeSave); });
    connect(m_tabScreenshotsBtn, &QPushButton::clicked, this, [this]() { switchTab(TypeScreenshot); });
    connect(m_tabResourcePacksBtn, &QPushButton::clicked, this, [this]() { switchTab(TypeResourcePack); });
    connect(m_tabShaderPacksBtn, &QPushButton::clicked, this, [this]() { switchTab(TypeShaderPack); });

    connect(m_searchEdit, &QLineEdit::textChanged, this, &InstanceResourcesPage::onSearchTextChanged);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceResourcesPage::onCategoryFilterChanged);
    connect(m_categoryManageBtn, &QPushButton::clicked,
            this, &InstanceResourcesPage::onCategoryManageClicked);
    connect(m_categoryManager, &LocalCategoryManager::categoriesChanged,
            this, &InstanceResourcesPage::onCategoriesChanged);
    connect(m_categoryManager, &LocalCategoryManager::assignmentsChanged,
            this, &InstanceResourcesPage::onAssignmentsChanged);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &InstanceResourcesPage::onSelectAllClicked);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &InstanceResourcesPage::onOpenFolderClicked);
    connect(m_pasteBtn, &QPushButton::clicked, this, &InstanceResourcesPage::onPasteFileClicked);
    connect(m_detailBtn, &QPushButton::clicked, this, [this]() {
        if (m_selectedIndices.isEmpty())
            return;
        int idx = *m_selectedIndices.begin();
        if (idx < 0)
            return;
        if (m_currentType == TypeMods && idx < m_modList.size())
        {
            emit modDetailRequested(m_modList.at(idx));
        }
        else if (idx < m_fileList.size())
        {
            const FileEntry &e = m_fileList.at(idx);
            QString info = tr("名称: %1\n路径: %2\n大小: %3\n修改时间: %4")
                               .arg(e.fileName)
                               .arg(e.filePath)
                               .arg(formatFileSize(e.fileSize))
                               .arg(e.lastModified.toString("yyyy-MM-dd hh:mm:ss"));
            AppMessageBox::information(this, tr("文件详情"), info);
        }
    });

    connect(m_scanner, &ModScanner::scanCompleted, this, &InstanceResourcesPage::onScanCompleted);
    connect(m_scanner, &ModScanner::scanFailed, this, &InstanceResourcesPage::onScanFailed);
    connect(m_nameFetcher, &ModNameFetcher::namesResolved, this, &InstanceResourcesPage::onModNameResolved);
    connect(m_nameFetcher, &ModNameFetcher::namesFetchFailed, this, &InstanceResourcesPage::onModNameFetchFailed);

    // 主题色变化时重新应用 Tab 栏样式（按钮选中态、指示条颜色）
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this, [this]() {
        applyTabThemeStyles();
    });
}

void InstanceResourcesPage::setInstancePath(const QString &path)
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
            refreshList();
        }
        else
        {
            clearCards();
            m_modList.clear();
            m_fileList.clear();
            updateStatsLabel();
            showEmptyHint(true);
        }
    }
}

void InstanceResourcesPage::refreshList()
{
    if (m_instancePath.isEmpty())
    {
        return;
    }
    m_isLoading = true;
    showLoading(true);
    showEmptyHint(false);
    clearCards();

    if (m_currentType == TypeMods)
    {
        m_searchEdit->setPlaceholderText(tr("搜索模组名称..."));
        m_scanner->scanMods(m_instancePath);
    }
    else
    {
        m_searchEdit->setPlaceholderText(tr("搜索%1名称...").arg(typeDisplayName()));
        scanFiles();
    }
}

QString InstanceResourcesPage::resolveResourceDir(const QString &subDir) const
{
    if (m_instancePath.isEmpty() || subDir.isEmpty())
    {
        return QString();
    }
    // 1. 版本隔离布局：资源在 versions/{ver}/{subDir}
    const QString isolated = m_instancePath + QStringLiteral("/") + subDir;
    if (QDir(isolated).exists())
    {
        return isolated;
    }
    // 2. 非隔离布局：资源在 .minecraft/{subDir}（即 versions/{ver}/../{subDir}）
    QDir parentDir(m_instancePath);
    parentDir.cdUp();
    const QString shared = parentDir.absoluteFilePath(subDir);
    if (QDir(shared).exists())
    {
        return shared;
    }
    // 3. 都不存在，返回隔离路径（后续显示"目录不存在"提示）
    return isolated;
}

QString InstanceResourcesPage::typeDisplayName() const
{
    switch (m_currentType)
    {
    case TypeMods:         return tr("模组");
    case TypeSave:         return tr("存档");
    case TypeScreenshot:   return tr("截图");
    case TypeResourcePack: return tr("资源包");
    case TypeShaderPack:   return tr("光影包");
    }
    return QString();
}

QString InstanceResourcesPage::subDirName() const
{
    switch (m_currentType)
    {
    case TypeMods:         return QStringLiteral("mods");
    case TypeSave:         return QStringLiteral("saves");
    case TypeScreenshot:   return QStringLiteral("screenshots");
    case TypeResourcePack: return QStringLiteral("resourcepacks");
    case TypeShaderPack:   return QStringLiteral("shaderpacks");
    }
    return QString();
}

void InstanceResourcesPage::onTabChanged(int index)
{
    // 已由 switchTab 完成选中态切换与指示条动画，这里仅处理数据刷新
    Q_UNUSED(index);

    // 切换资源类型：分类范围随之切换
    m_categoryManager->setResourceType(subDirName());
    m_currentCategoryId.clear();
    reloadCategoryCombo();

    // 清空搜索与选中
    m_searchEdit->clear();
    deselectAllCards();

    refreshList();
}

void InstanceResourcesPage::onSearchTextChanged(const QString &text)
{
    m_currentSearch = text.trimmed();
    applyFilterAndSearch();
}

void InstanceResourcesPage::onScanCompleted(const LocalModList &result)
{
    m_isLoading = false;
    showLoading(false);

    m_modList = result.mods;
    clearCards();
    m_modIndexMap.clear();

    for (int i = 0; i < m_modList.size(); ++i)
    {
        const ModInfo &info = m_modList.at(i);
        QWidget *card = createModCard(info);
        m_cardWidgets.append(card);

        QString key = info.id.isEmpty() ? info.fileName : info.id;
        m_modIndexMap.insert(key, i);
    }

    placeCards();
    refreshCardCategoryChips();
    applyFilterAndSearch();
    updateStatsLabel();

    if (m_modList.isEmpty())
    {
        showEmptyHint(true);
    }

    // 异步获取模组中文名/英文名（仅对模组类型生效）
    if (!m_modList.isEmpty())
    {
        m_nameFetcher->fetchNamesBatch(m_modList);
    }
}

void InstanceResourcesPage::onScanFailed(const QString &error)
{
    m_isLoading = false;
    showLoading(false);
    showEmptyHint(true);
    m_statsLabel->setText(tr("扫描失败: %1").arg(error));
}

void InstanceResourcesPage::onModNameResolved(const ModInfo &info)
{
    QString key = info.id.isEmpty() ? info.fileName : info.id;
    if (!m_modIndexMap.contains(key))
    {
        return;
    }
    int idx = m_modIndexMap.value(key);
    if (idx < 0 || idx >= m_modList.size() || idx >= m_cardWidgets.size())
    {
        return;
    }

    // 更新数据
    m_modList[idx].chineseName = info.chineseName;
    m_modList[idx].englishName = info.englishName;
    m_modList[idx].mcmodUrl = info.mcmodUrl;

    // 更新卡片名称标签
    QWidget *card = m_cardWidgets.at(idx);
    QLabel *nameLabel = card->findChild<QLabel *>(QStringLiteral("cardNameLabel"));
    if (nameLabel)
    {
        QString titleText;
        if (!info.chineseName.isEmpty())
        {
            titleText = info.chineseName;
        }
        else if (!info.englishName.isEmpty())
        {
            titleText = info.englishName;
        }
        else
        {
            titleText = info.name.isEmpty() ? info.fileName : info.name;
        }
        nameLabel->setText(titleText);
    }
}

void InstanceResourcesPage::onModNameFetchFailed(const QString &modId, const QString &englishName)
{
    Q_UNUSED(modId);
    Q_UNUSED(englishName);
    // 名称获取失败时保持原文件名显示，无需特殊处理
}

void InstanceResourcesPage::scanFiles()
{
    m_fileList.clear();
    m_isLoading = true;
    showLoading(true);
    showEmptyHint(false);
    clearCards();

    QString dirPath = resolveResourceDir(subDirName());
    QDir dir(dirPath);
    if (!dir.exists())
    {
        m_isLoading = false;
        showLoading(false);
        showEmptyHint(true);
        m_statsLabel->setText(tr("%1目录不存在").arg(typeDisplayName()));
        return;
    }

    // 根据类型选择扫描方式：
    //  - 存档：扫描子目录
    //  - 截图：扫描图片文件
    //  - 资源包/光影包：扫描 .zip 文件
    QFileInfoList entries;
    switch (m_currentType)
    {
    case TypeSave:
        entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        break;
    case TypeScreenshot:
        entries = dir.entryInfoList(
            QStringList() << QStringLiteral("*.png") << QStringLiteral("*.jpg")
                          << QStringLiteral("*.jpeg") << QStringLiteral("*.webp"),
            QDir::Files, QDir::Name);
        break;
    case TypeResourcePack:
    case TypeShaderPack:
        entries = dir.entryInfoList(QStringList() << QStringLiteral("*.zip"), QDir::Files, QDir::Name);
        break;
    case TypeMods:
        // 模组走 ModScanner，不会进入此分支
        break;
    }

    for (const QFileInfo &fi : entries)
    {
        FileEntry entry;
        entry.filePath = fi.absoluteFilePath();
        entry.fileName = fi.fileName();
        entry.fileSize = fi.isDir() ? 0 : fi.size();
        entry.lastModified = fi.lastModified();
        entry.isDir = fi.isDir();
        m_fileList.append(entry);
    }

    m_isLoading = false;
    showLoading(false);

    for (int i = 0; i < m_fileList.size(); ++i)
    {
        const FileEntry &entry = m_fileList.at(i);
        QWidget *card = createFileCard(entry);
        m_cardWidgets.append(card);
    }

    placeCards();
    refreshCardCategoryChips();
    applyFilterAndSearch();
    updateStatsLabel();

    if (m_fileList.isEmpty())
    {
        showEmptyHint(true);
    }
}

QWidget *InstanceResourcesPage::createModCard(const ModInfo &info)
{
    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(64);
    card->setCursor(Qt::PointingHandCursor);

    // 存储索引到卡片属性，供点击事件使用
    QString key = info.id.isEmpty() ? info.fileName : info.id;
    card->setProperty("modKey", key);
    card->setProperty("modFilePath", info.filePath);
    card->setProperty("modFileName", info.fileName);
    card->setProperty("modEnabled", info.enabled);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    card->installEventFilter(this);
    int idx = m_cardWidgets.size();
    connect(card, &QWidget::customContextMenuRequested, this, [this, idx](const QPoint &pos) {
        onModContextMenu(pos, idx);
    });

    QHBoxLayout *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(10, 8, 10, 8);
    cardLayout->setSpacing(10);

    // 图标
    QLabel *iconLabel = new QLabel(card);
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setFixedSize(40, 40);
    iconLabel->setAlignment(Qt::AlignCenter);
    {
        QString realPath = info.filePath;
        if (realPath.endsWith(".disabled", Qt::CaseInsensitive))
        {
            realPath.chop(9);
        }
        QString cacheKey = QString("rp_icon:%1").arg(realPath);
        QPixmap icon;
        if (!QPixmapCache::find(cacheKey, &icon))
        {
            icon = ResourcesPage::extractModIcon(realPath);
            if (!icon.isNull())
            {
                QPixmapCache::insert(cacheKey, icon);
            }
        }
        if (!icon.isNull())
        {
            iconLabel->setPixmap(icon.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        else
        {
            QColor tc(ThemeManager::instance()->currentThemeColor());
            iconLabel->setPixmap(IconHelper::loadColoredIcon(":/Images/Icons/nav_mods.svg", tc, 32).pixmap(32, 32));
        }
    }
    cardLayout->addWidget(iconLabel);

    // 信息区域
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(2);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    // 名称
    QString titleText;
    if (!info.chineseName.isEmpty())
    {
        titleText = info.chineseName;
    }
    else if (!info.englishName.isEmpty())
    {
        titleText = info.englishName;
    }
    else
    {
        titleText = info.name.isEmpty() ? info.fileName : info.name;
    }

    QLabel *nameLabel = new QLabel(titleText, card);
    nameLabel->setObjectName("cardNameLabel");
    // 禁用换行，过长省略
    nameLabel->setWordWrap(false);
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(10);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    infoLayout->addWidget(nameLabel);

    // 标签行
    QWidget *chipsWidget = new QWidget(card);
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(4);

    if (!info.latestVersion.isEmpty())
    {
        QLabel *verChip = new QLabel(info.latestVersion, chipsWidget);
        verChip->setObjectName("modChip");
        chipsLayout->addWidget(verChip);
    }
    if (!info.loaderType.isEmpty())
    {
        QLabel *loaderChip = new QLabel(info.loaderType, chipsWidget);
        loaderChip->setObjectName("modChip");
        chipsLayout->addWidget(loaderChip);
    }
    if (info.fileSize > 0)
    {
        QLabel *sizeChip = new QLabel(formatFileSize(info.fileSize), chipsWidget);
        sizeChip->setObjectName("modChip");
        chipsLayout->addWidget(sizeChip);
    }
    if (!info.enabled)
    {
        QLabel *disabledChip = new QLabel(tr("已禁用"), chipsWidget);
        disabledChip->setObjectName("modChip");
        disabledChip->setStyleSheet(QStringLiteral(
            "background-color: #9E9E9E; color: white; "
            "border-radius: 4px; padding: 1px 6px; font-size: 9pt;"));
        chipsLayout->addWidget(disabledChip);
    }
    // 分类标签（手动分类，默认隐藏，样式见 style.qss categoryChip="true"）
    QLabel *categoryChip = new QLabel(chipsWidget);
    categoryChip->setObjectName("modChip");
    categoryChip->setProperty("categoryChip", true);
    categoryChip->hide();
    chipsLayout->addWidget(categoryChip);
    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);

    cardLayout->addLayout(infoLayout, 1);

    // 操作按钮
    QColor themeColor(ThemeManager::instance()->currentThemeColor());

    auto createBtn = [&](const QString &iconPath, const QString &tip, const QColor &color) -> QPushButton * {
        QPushButton *btn = new QPushButton(card);
        btn->setObjectName("modCardActionBtn");
        btn->setFixedSize(28, 28);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, color, 16));
        btn->setIconSize(QSize(16, 16));
        return btn;
    };

    // 启用/禁用切换按钮
    QPushButton *toggleBtn = createBtn(
        info.enabled ? ":/Images/Icons/toggle_on.svg" : ":/Images/Icons/toggle_off.svg",
        info.enabled ? tr("禁用") : tr("启用"),
        info.enabled ? QColor("#4CAF50") : QColor("#9E9E9E"));
    connect(toggleBtn, &QPushButton::clicked, this, [this, key]() {
        int idx = m_modIndexMap.value(key, -1);
        if (idx < 0 || idx >= m_modList.size())
        {
            return;
        }
        ModInfo info = m_modList.at(idx);
        QString oldPath = info.filePath;
        QString newPath;
        if (info.enabled)
        {
            newPath = oldPath + QStringLiteral(".disabled");
        }
        else
        {
            if (oldPath.endsWith(QStringLiteral(".disabled")))
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
            refreshList();
        }
    });
    cardLayout->addWidget(toggleBtn);

    // 删除按钮
    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"), QColor("#F44336"));
    connect(deleteBtn, &QPushButton::clicked, this, [this, key]() {
        int idx = m_modIndexMap.value(key, -1);
        if (idx < 0 || idx >= m_modList.size())
        {
            return;
        }
        ModInfo info = m_modList.at(idx);
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
            QString disabledPath = info.filePath + QStringLiteral(".disabled");
            if (QFile::exists(disabledPath))
            {
                QFile::remove(disabledPath);
            }
            refreshList();
        }
    });
    cardLayout->addWidget(deleteBtn);

    return card;
}

QWidget *InstanceResourcesPage::createFileCard(const FileEntry &entry)
{
    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(64);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("filePath", entry.filePath);
    card->setProperty("fileName", entry.fileName);
    card->installEventFilter(this);

    QHBoxLayout *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(10, 8, 10, 8);
    cardLayout->setSpacing(10);

    // 图标：根据类型从本地文件提取，失败回退到类型 SVG
    QLabel *iconLabel = new QLabel(card);
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setFixedSize(40, 40);
    iconLabel->setAlignment(Qt::AlignCenter);
    {
        QString cacheKey = QString("rp_icon:%1").arg(entry.filePath);
        QPixmap icon;
        if (!QPixmapCache::find(cacheKey, &icon))
        {
            switch (m_currentType)
            {
            case TypeResourcePack:
            case TypeShaderPack:
                icon = ResourcesPage::extractResourcePackIcon(entry.filePath);
                break;
            case TypeSave:
                icon = ResourcesPage::extractSaveIcon(entry.filePath);
                break;
            case TypeScreenshot:
                icon = ResourcesPage::loadImageThumbnail(entry.filePath);
                break;
            case TypeMods:
                // 不会进入此分支
                break;
            }
            if (!icon.isNull())
            {
                QPixmapCache::insert(cacheKey, icon);
            }
        }
        if (!icon.isNull())
        {
            iconLabel->setPixmap(icon.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        else
        {
            QColor tc(ThemeManager::instance()->currentThemeColor());
            QString fallbackIcon;
            switch (m_currentType)
            {
            case TypeResourcePack: fallbackIcon = ":/Images/Icons/nav_resourcepacks.svg"; break;
            case TypeShaderPack:   fallbackIcon = ":/Images/Icons/nav_shaders.svg"; break;
            case TypeSave:         fallbackIcon = ":/Images/Icons/nav_worlds.svg"; break;
            case TypeScreenshot:   fallbackIcon = ":/Images/Icons/screenshot.svg"; break;
            case TypeMods:         fallbackIcon = ":/Images/Icons/nav_mods.svg"; break;
            }
            iconLabel->setPixmap(IconHelper::loadColoredIcon(fallbackIcon, tc, 32).pixmap(32, 32));
        }
    }
    cardLayout->addWidget(iconLabel);

    // 信息区域
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(2);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *nameLabel = new QLabel(entry.fileName, card);
    nameLabel->setObjectName("cardNameLabel");
    nameLabel->setWordWrap(false);
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(10);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    infoLayout->addWidget(nameLabel);

    QWidget *chipsWidget = new QWidget(card);
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(4);

    // 存档显示"文件夹"，其他显示文件大小
    if (entry.isDir)
    {
        QLabel *sizeChip = new QLabel(tr("文件夹"), chipsWidget);
        sizeChip->setObjectName("modChip");
        chipsLayout->addWidget(sizeChip);
    }
    else if (entry.fileSize > 0)
    {
        QLabel *sizeChip = new QLabel(formatFileSize(entry.fileSize), chipsWidget);
        sizeChip->setObjectName("modChip");
        chipsLayout->addWidget(sizeChip);
    }

    if (entry.lastModified.isValid())
    {
        QLabel *dateChip = new QLabel(entry.lastModified.toString("yyyy-MM-dd"), chipsWidget);
        dateChip->setObjectName("modChip");
        chipsLayout->addWidget(dateChip);
    }
    // 分类标签（手动分类，默认隐藏，样式见 style.qss categoryChip="true"）
    QLabel *categoryChip = new QLabel(chipsWidget);
    categoryChip->setObjectName("modChip");
    categoryChip->setProperty("categoryChip", true);
    categoryChip->hide();
    chipsLayout->addWidget(categoryChip);
    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);

    cardLayout->addLayout(infoLayout, 1);

    // 右键菜单（设置分类/打开位置/复制路径等）
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    int fileIdx = m_cardWidgets.size();
    connect(card, &QWidget::customContextMenuRequested, this, [this, fileIdx](const QPoint &pos) {
        showFileContextMenu(pos, fileIdx);
    });

    // 操作按钮
    QColor themeColor(ThemeManager::instance()->currentThemeColor());

    auto createBtn = [&](const QString &iconPath, const QString &tip, const QColor &color) -> QPushButton * {
        QPushButton *btn = new QPushButton(card);
        btn->setObjectName("modCardActionBtn");
        btn->setFixedSize(28, 28);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, color, 16));
        btn->setIconSize(QSize(16, 16));
        return btn;
    };

    // 快捷启动按钮（仅存档）：启动游戏并直接进入该存档
    if (m_currentType == TypeSave)
    {
        QPushButton *quickLaunchBtn = createBtn(":/Images/Icons/quick_launch.svg", tr("快捷启动"), themeColor);
        connect(quickLaunchBtn, &QPushButton::clicked, this, [this, entry]() {
            emit quickLaunchSaveRequested(entry.fileName);
        });
        cardLayout->addWidget(quickLaunchBtn);
    }

    // 截图查看按钮（仅截图）：用系统默认程序打开图片
    if (m_currentType == TypeScreenshot)
    {
        QPushButton *viewBtn = createBtn(":/Images/Icons/screenshot.svg", tr("查看截图"), themeColor);
        connect(viewBtn, &QPushButton::clicked, this, [entry]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(entry.filePath));
        });
        cardLayout->addWidget(viewBtn);
    }

    // 打开文件位置
    QPushButton *folderBtn = createBtn(":/Images/Icons/folder.svg", tr("打开文件位置"), themeColor);
    connect(folderBtn, &QPushButton::clicked, this, [entry]() {
        QFileInfo fi(entry.filePath);
        // 存档是目录，直接打开；其他打开所在目录
        QString dirPath = fi.isDir() ? entry.filePath : fi.absolutePath();
        if (!dirPath.isEmpty())
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
        }
    });
    cardLayout->addWidget(folderBtn);

    // 删除按钮：存档递归删除目录，其他删除文件
    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"), QColor("#F44336"));
    connect(deleteBtn, &QPushButton::clicked, this, [this, entry]() {
        AppMessageBox::StandardButton reply = AppMessageBox::question(
            this,
            tr("删除%1").arg(typeDisplayName()),
            tr("确定要删除 \"%1\" 吗？此操作不可撤销。").arg(entry.fileName),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);
        if (reply == AppMessageBox::Yes)
        {
            if (entry.isDir)
            {
                QDir(entry.filePath).removeRecursively();
            }
            else
            {
                QFile::remove(entry.filePath);
            }
            refreshList();
        }
    });
    cardLayout->addWidget(deleteBtn);

    return card;
}

bool InstanceResourcesPage::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *card = qobject_cast<QWidget *>(watched);
    if (!card)
    {
        return QWidget::eventFilter(watched, event);
    }

    // 通过卡片在 m_cardWidgets 中的位置定位索引
    int idx = m_cardWidgets.indexOf(card);
    if (idx < 0)
    {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonDblClick)
    {
        // 双击行为：
        //  - 模组：打开详情
        //  - 存档：打开存档目录
        //  - 截图：用系统默认程序打开图片
        //  - 资源包/光影包：打开所在目录
        if (m_currentType == TypeMods && idx < m_modList.size())
        {
            emit modDetailRequested(m_modList.at(idx));
        }
        else if (idx < m_fileList.size())
        {
            const FileEntry &e = m_fileList.at(idx);
            if (m_currentType == TypeScreenshot)
            {
                QDesktopServices::openUrl(QUrl::fromLocalFile(e.filePath));
            }
            else if (m_currentType == TypeSave)
            {
                QDesktopServices::openUrl(QUrl::fromLocalFile(e.filePath));
            }
            else
            {
                QFileInfo fi(e.filePath);
                QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
            }
        }
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            toggleCardSelection(idx);
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void InstanceResourcesPage::clearCards()
{
    for (QWidget *w : m_cardWidgets)
    {
        m_cardLayout->removeWidget(w);
        w->deleteLater();
    }
    m_cardWidgets.clear();
    m_selectedIndices.clear();
    m_allSelected = false;
    m_selectAllBtn->setText(tr("全选"));
    updateBottomBarState();
}

void InstanceResourcesPage::placeCards()
{
    // 移除已有的卡片项（保留末尾的占位 stretch）
    while (m_cardLayout->count() > 1)
    {
        QLayoutItem *item = m_cardLayout->takeAt(0);
        delete item;
    }

    // 在 stretch 之前依次插入卡片
    for (int i = 0; i < m_cardWidgets.size(); ++i)
    {
        m_cardLayout->insertWidget(i, m_cardWidgets.at(i));
    }
}

void InstanceResourcesPage::showLoading(bool show)
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

void InstanceResourcesPage::showEmptyHint(bool show)
{
    m_emptyLabel->setText(tr("暂无%1").arg(typeDisplayName()));
    m_emptyLabel->setVisible(show);
    // 空态时隐藏列表滚动区，让提示文字占据列表区域
    m_scrollArea->setVisible(!show);
}

void InstanceResourcesPage::updateBottomBarState()
{
    bool hasSelection = !m_selectedIndices.isEmpty();
    m_detailBtn->setVisible(hasSelection && m_currentType == TypeMods);
    // 选中时隐藏粘贴按钮，避免误操作
    m_pasteBtn->setVisible(!hasSelection);
}

void InstanceResourcesPage::toggleCardSelection(int idx)
{
    if (idx < 0 || idx >= m_cardWidgets.size())
    {
        return;
    }

    if (m_allSelected)
    {
        m_allSelected = false;
        m_selectAllBtn->setText(tr("全选"));
    }

    if (m_selectedIndices.contains(idx))
    {
        m_selectedIndices.remove(idx);
        m_cardWidgets[idx]->setStyleSheet(QString());
    }
    else
    {
        m_selectedIndices.insert(idx);
        m_cardWidgets[idx]->setStyleSheet(selectedCardStyle());
    }

    updateBottomBarState();
}

void InstanceResourcesPage::deselectAllCards()
{
    for (int idx : m_selectedIndices)
    {
        if (idx >= 0 && idx < m_cardWidgets.size())
        {
            m_cardWidgets[idx]->setStyleSheet(QString());
        }
    }
    m_selectedIndices.clear();
    m_allSelected = false;
    m_selectAllBtn->setText(tr("全选"));
    updateBottomBarState();
}

QString InstanceResourcesPage::selectedCardStyle() const
{
    QString color = ThemeManager::instance()->currentThemeColor();
    return QString("#modCardListItem { border: 2px solid %1; }").arg(color);
}

void InstanceResourcesPage::applyFilterAndSearch()
{
    int visibleCount = 0;

    for (int i = 0; i < m_cardWidgets.size(); ++i)
    {
        bool visible = true;

        if (m_currentType == TypeMods && i < m_modList.size())
        {
            const ModInfo &info = m_modList.at(i);
            if (!m_currentSearch.isEmpty())
            {
                QString searchLower = m_currentSearch.toLower();
                visible = info.name.toLower().contains(searchLower)
                          || info.fileName.toLower().contains(searchLower)
                          || info.chineseName.toLower().contains(searchLower)
                          || info.englishName.toLower().contains(searchLower);
            }
            // 分类筛选
            if (visible && !m_currentCategoryId.isEmpty())
            {
                const QString modCategory = m_categoryManager->categoryOf(info.fileName);
                if (m_currentCategoryId == QStringLiteral("__uncategorized__"))
                    visible = modCategory.isEmpty();
                else
                    visible = (modCategory == m_currentCategoryId);
            }
        }
        else if (i < m_fileList.size())
        {
            const FileEntry &entry = m_fileList.at(i);
            if (!m_currentSearch.isEmpty())
            {
                visible = entry.fileName.toLower().contains(m_currentSearch.toLower());
            }
            // 分类筛选
            if (visible && !m_currentCategoryId.isEmpty())
            {
                const QString entryCategory = m_categoryManager->categoryOf(entry.fileName);
                if (m_currentCategoryId == QStringLiteral("__uncategorized__"))
                    visible = entryCategory.isEmpty();
                else
                    visible = (entryCategory == m_currentCategoryId);
            }
        }

        m_cardWidgets[i]->setVisible(visible);
        if (visible)
        {
            ++visibleCount;
        }
    }

    // 若搜索后选中项被隐藏，保持选中索引不变（但隐藏样式无需变更）
    Q_UNUSED(visibleCount);
}

void InstanceResourcesPage::updateStatsLabel()
{
    // 分类筛选生效时显示筛选后的数量
    if (!m_currentCategoryId.isEmpty())
    {
        int visibleCount = 0;
        for (QWidget *w : m_cardWidgets)
        {
            if (w->isVisible())
            {
                ++visibleCount;
            }
        }
        m_statsLabel->setText(tr("显示 %1 个%2").arg(visibleCount).arg(typeDisplayName()));
        return;
    }

    if (m_currentType == TypeMods)
    {
        int enabled = 0;
        int disabled = 0;
        for (const ModInfo &m : m_modList)
        {
            if (m.enabled)
            {
                ++enabled;
            }
            else
            {
                ++disabled;
            }
        }
        m_statsLabel->setText(tr("共 %1 个模组 (%2 启用, %3 禁用)")
                                  .arg(m_modList.size())
                                  .arg(enabled)
                                  .arg(disabled));
    }
    else
    {
        m_statsLabel->setText(tr("共 %1 个%2").arg(m_fileList.size()).arg(typeDisplayName()));
    }
}

QString InstanceResourcesPage::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024)
    {
        return QString::number(bytes) + QStringLiteral(" B");
    }
    if (bytes < 1024 * 1024)
    {
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    }
    if (bytes < 1024 * 1024 * 1024)
    {
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    }
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + QStringLiteral(" GB");
}

void InstanceResourcesPage::onSelectAllClicked()
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
                m_cardWidgets[i]->setStyleSheet(selectedCardStyle());
            }
        }
    }
    else
    {
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            m_cardWidgets[i]->setStyleSheet(QString());
        }
    }

    updateBottomBarState();
}

void InstanceResourcesPage::onOpenFolderClicked()
{
    if (m_instancePath.isEmpty())
    {
        return;
    }

    QString dirPath = resolveResourceDir(subDirName());
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

void InstanceResourcesPage::onPasteFileClicked()
{
    if (m_instancePath.isEmpty())
    {
        return;
    }

    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    if (!mimeData->hasUrls())
    {
        AppMessageBox::information(this, tr("粘贴%1").arg(typeDisplayName()),
            tr("剪贴板中没有文件，请先复制文件。"));
        return;
    }

    QString targetDir = resolveResourceDir(subDirName());
    QDir dir(targetDir);
    if (!dir.exists())
    {
        dir.mkpath(QStringLiteral("."));
    }

    // 根据类型确定有效后缀；存档类型粘贴整个目录
    QStringList validSuffixes;
    bool pasteAsDir = false;
    switch (m_currentType)
    {
    case TypeMods:
        validSuffixes = { QStringLiteral(".jar"), QStringLiteral(".disabled") };
        break;
    case TypeResourcePack:
    case TypeShaderPack:
        validSuffixes = { QStringLiteral(".zip") };
        break;
    case TypeScreenshot:
        validSuffixes = { QStringLiteral(".png"), QStringLiteral(".jpg"),
                          QStringLiteral(".jpeg"), QStringLiteral(".webp") };
        break;
    case TypeSave:
        pasteAsDir = true; // 存档是目录，复制整个文件夹
        break;
    }

    int copiedCount = 0;
    for (const QUrl &url : mimeData->urls())
    {
        QString srcPath = url.toLocalFile();
        if (srcPath.isEmpty())
        {
            continue;
        }
        QFileInfo fi(srcPath);

        if (pasteAsDir)
        {
            // 存档：仅接受目录
            if (!fi.isDir())
            {
                continue;
            }
            QString destPath = targetDir + QStringLiteral("/") + fi.fileName();
            if (QDir(destPath).exists())
            {
                int ret = AppMessageBox::question(this, tr("目录已存在"),
                    tr("存档 \"%1\" 已存在，是否覆盖？").arg(fi.fileName()),
                    AppMessageBox::Yes | AppMessageBox::No | AppMessageBox::Cancel);
                if (ret == AppMessageBox::Cancel)
                {
                    break;
                }
                if (ret == AppMessageBox::No)
                {
                    continue;
                }
                QDir(destPath).removeRecursively();
            }
            if (copyDir(srcPath, destPath))
            {
                ++copiedCount;
            }
            else
            {
                AppMessageBox::warning(this, tr("粘贴失败"),
                    tr("无法复制存档 \"%1\"").arg(fi.fileName()));
            }
        }
        else
        {
            // 文件型：检查后缀
            if (!fi.isFile())
            {
                continue;
            }
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
                continue;
            }

            QString destPath = targetDir + QStringLiteral("/") + fi.fileName();
            if (QFile::exists(destPath))
            {
                int ret = AppMessageBox::question(this, tr("文件已存在"),
                    tr("文件 \"%1\" 已存在，是否覆盖？").arg(fi.fileName()),
                    AppMessageBox::Yes | AppMessageBox::No | AppMessageBox::Cancel);
                if (ret == AppMessageBox::Cancel)
                {
                    break;
                }
                if (ret == AppMessageBox::No)
                {
                    continue;
                }
                QFile::remove(destPath);
            }

            if (QFile::copy(srcPath, destPath))
            {
                ++copiedCount;
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
        refreshList();
    }
}

void InstanceResourcesPage::onModContextMenu(const QPoint &pos, int idx)
{
    if (idx < 0 || idx >= m_modList.size())
    {
        return;
    }
    const ModInfo &info = m_modList.at(idx);
    QWidget *card = m_cardWidgets.value(idx);
    if (!card)
    {
        return;
    }

    QMenu menu(this);
    QAction *toggleAction = menu.addAction(info.enabled ? tr("禁用模组") : tr("启用模组"));
    menu.addSeparator();

    // ── 设置分类子菜单 ──
    QMenu *categoryMenu = menu.addMenu(tr("设置分类"));
    {
        const QString currentCat = m_categoryManager->categoryOf(info.fileName);
        const QList<LocalResourceCategory> cats = m_categoryManager->categories();
        for (const LocalResourceCategory &c : cats)
        {
            QAction *action = categoryMenu->addAction(c.name);
            action->setCheckable(true);
            action->setChecked(currentCat == c.id);
            QObject::connect(action, &QAction::triggered, this,
                [this, fileName = info.fileName, categoryId = c.id]() {
                    m_categoryManager->assignCategory(fileName, categoryId);
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
            [this, fileName = info.fileName]() {
                m_categoryManager->assignCategory(fileName, QString());
            });
        categoryMenu->addSeparator();
        QAction *newCatAction = categoryMenu->addAction(tr("新建分类…"));
        QObject::connect(newCatAction, &QAction::triggered, this,
            [this, fileName = info.fileName]() {
                FavoriteFolderDialog dialog(FavoriteFolderDialog::CreateMode, this);
                dialog.setWindowTitle(tr("新建分类"));
                dialog.setPresets(LocalCategoryManager::suggestedNames(m_categoryManager->resourceType()));
                if (dialog.exec() != QDialog::Accepted)
                    return;
                const QString name = dialog.folderName();
                if (name.isEmpty())
                    return;
                const QString newId = m_categoryManager->addCategory(name);
                m_categoryManager->assignCategory(fileName, newId);
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
        QString oldPath = info.filePath;
        QString newPath;
        if (info.enabled)
        {
            newPath = oldPath + QStringLiteral(".disabled");
        }
        else
        {
            if (oldPath.endsWith(QStringLiteral(".disabled")))
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
            refreshList();
        }
    }
    else if (chosen == detailAction)
    {
        emit modDetailRequested(info);
    }
    else if (chosen == openFolderAction)
    {
        QFileInfo fi(info.filePath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    }
    else if (chosen == copyPathAction)
    {
        QApplication::clipboard()->setText(info.filePath);
    }
    else if (chosen == deleteAction)
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
            QString disabledPath = info.filePath + QStringLiteral(".disabled");
            if (QFile::exists(disabledPath))
            {
                QFile::remove(disabledPath);
            }
            refreshList();
        }
    }
}

// ============================
// 本地资源分类（模组/资源包/光影包/存档/截图）
// ============================

void InstanceResourcesPage::showFileContextMenu(const QPoint &pos, int idx)
{
    if (idx < 0 || idx >= m_fileList.size())
    {
        return;
    }
    const FileEntry &entry = m_fileList.at(idx);
    QWidget *card = m_cardWidgets.value(idx);
    if (!card)
    {
        return;
    }

    QMenu menu(this);

    // ── 设置分类子菜单 ──
    QMenu *categoryMenu = menu.addMenu(tr("设置分类"));
    {
        const QString currentCat = m_categoryManager->categoryOf(entry.fileName);
        const QList<LocalResourceCategory> cats = m_categoryManager->categories();
        for (const LocalResourceCategory &c : cats)
        {
            QAction *action = categoryMenu->addAction(c.name);
            action->setCheckable(true);
            action->setChecked(currentCat == c.id);
            QObject::connect(action, &QAction::triggered, this,
                [this, fileName = entry.fileName, categoryId = c.id]() {
                    m_categoryManager->assignCategory(fileName, categoryId);
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
            [this, fileName = entry.fileName]() {
                m_categoryManager->assignCategory(fileName, QString());
            });
        categoryMenu->addSeparator();
        QAction *newCatAction = categoryMenu->addAction(tr("新建分类…"));
        QObject::connect(newCatAction, &QAction::triggered, this,
            [this, fileName = entry.fileName]() {
                FavoriteFolderDialog dialog(FavoriteFolderDialog::CreateMode, this);
                dialog.setWindowTitle(tr("新建分类"));
                dialog.setPresets(LocalCategoryManager::suggestedNames(m_categoryManager->resourceType()));
                if (dialog.exec() != QDialog::Accepted)
                    return;
                const QString name = dialog.folderName();
                if (name.isEmpty())
                    return;
                const QString newId = m_categoryManager->addCategory(name);
                m_categoryManager->assignCategory(fileName, newId);
            });
    }

    QAction *openFolderAction = menu.addAction(tr("打开文件位置"));
    QAction *copyPathAction = menu.addAction(tr("复制文件路径"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(tr("删除%1").arg(typeDisplayName()));

    QAction *chosen = menu.exec(card->mapToGlobal(pos));
    if (chosen == openFolderAction)
    {
        QFileInfo fi(entry.filePath);
        QString dirPath = fi.isDir() ? entry.filePath : fi.absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
    }
    else if (chosen == copyPathAction)
    {
        QApplication::clipboard()->setText(entry.filePath);
    }
    else if (chosen == deleteAction)
    {
        AppMessageBox::StandardButton reply = AppMessageBox::question(
            this,
            tr("删除%1").arg(typeDisplayName()),
            tr("确定要删除 \"%1\" 吗？此操作不可撤销。").arg(entry.fileName),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);
        if (reply == AppMessageBox::Yes)
        {
            if (entry.isDir)
            {
                QDir(entry.filePath).removeRecursively();
            }
            else
            {
                QFile::remove(entry.filePath);
            }
            refreshList();
        }
    }
}

void InstanceResourcesPage::reloadCategoryCombo()
{
    if (!m_categoryCombo)
    {
        return;
    }

    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();
    m_categoryCombo->addItem(tr("全部"), QString());
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

void InstanceResourcesPage::onCategoryFilterChanged(int index)
{
    if (!m_categoryCombo)
    {
        return;
    }
    m_currentCategoryId = m_categoryCombo->itemData(index).toString();
    applyFilterAndSearch();
    updateStatsLabel();
}

void InstanceResourcesPage::onCategoryManageClicked()
{
    LocalCategoryDialog dialog(m_categoryManager, this);
    dialog.exec();
}

void InstanceResourcesPage::onCategoriesChanged()
{
    reloadCategoryCombo();
    refreshCardCategoryChips();
    applyFilterAndSearch();
    updateStatsLabel();
}

void InstanceResourcesPage::onAssignmentsChanged()
{
    refreshCardCategoryChips();
    applyFilterAndSearch();
    updateStatsLabel();
}

void InstanceResourcesPage::refreshCardCategoryChips()
{
    for (int i = 0; i < m_cardWidgets.size(); ++i)
    {
        QWidget *card = m_cardWidgets.at(i);
        QString catName;

        if (m_currentType == TypeMods && i < m_modList.size())
        {
            catName = m_categoryManager->categoryNameOf(m_modList.at(i).fileName);
        }
        else if (i < m_fileList.size())
        {
            catName = m_categoryManager->categoryNameOf(m_fileList.at(i).fileName);
        }

        const QList<QLabel *> labels = card->findChildren<QLabel *>();
        for (QLabel *chip : labels)
        {
            if (!chip->property("categoryChip").toBool())
            {
                continue;
            }
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

// ============================
// Tab 栏样式与动画
// ============================

QPushButton *InstanceResourcesPage::createTabButton(const QString &text, int index)
{
    QPushButton *btn = new QPushButton(text, m_tabBar);
    btn->setObjectName("resourcesTabBtn");
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(28);
    // 记录索引，便于在样式表中识别（也可用于点击响应）
    btn->setProperty("tabIndex", index);
    return btn;
}

void InstanceResourcesPage::applyTabThemeStyles()
{
    if (!m_tabBar)
    {
        return;
    }

    QString themeColor = ThemeManager::instance()->currentThemeColor();
    QString textColor = ThemeManager::instance()->currentTextColor();
    QString borderColor = ThemeManager::instance()->currentBorderColor();

    QColor tc(themeColor);
    // 半透明悬停背景：基于主题色 12% 透明度
    QString hoverBg = QString("rgba(%1, %2, %3, 0.12)")
                          .arg(tc.red()).arg(tc.green()).arg(tc.blue());
    // 选中态浅色背景：基于主题色 18% 透明度
    QString checkedBg = QString("rgba(%1, %2, %3, 0.18)")
                            .arg(tc.red()).arg(tc.green()).arg(tc.blue());

    // Tab 栏容器样式：底部 1px 分隔线，营造层次感
    m_tabBar->setStyleSheet(QString(
        "#resourcesTabBar {"
        "  background: palette(window);"
        "  border-bottom: 1px solid %1;"
        "}"
    ).arg(borderColor));

    // 指示条样式：主题色，圆角
    if (m_tabHighlight)
    {
        m_tabHighlight->setStyleSheet(QString(
            "#resourcesTabHighlight {"
            "  background: %1;"
            "  border-radius: 2px;"
            "}"
        ).arg(themeColor));
    }

    // 按钮样式：默认透明文字色，悬停半透明背景，选中态主题色文字+浅色背景
    QString btnStyle = QString(
        "QPushButton#resourcesTabBtn {"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 2px 12px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "  color: %1;"
        "  background: transparent;"
        "}"
        "QPushButton#resourcesTabBtn:hover {"
        "  background: %2;"
        "}"
        "QPushButton#resourcesTabBtn:checked {"
        "  color: %3;"
        "  background: %4;"
        "}"
        "QPushButton#resourcesTabBtn:pressed {"
        "  background: %5;"
        "}"
    ).arg(textColor, hoverBg, themeColor, checkedBg, hoverBg);

    for (QPushButton *btn : m_tabButtons)
    {
        if (btn)
        {
            btn->setStyleSheet(btnStyle);
        }
    }
}

void InstanceResourcesPage::switchTab(int index)
{
    if (index == m_currentType || index < 0 || index >= m_tabButtons.size())
    {
        return;
    }
    m_currentType = static_cast<ResourceType>(index);

    // 更新按钮选中态
    for (int i = 0; i < m_tabButtons.size(); ++i)
    {
        if (m_tabButtons[i])
        {
            m_tabButtons[i]->setChecked(i == index);
        }
    }

    // 触发指示条位移动画
    if (m_tabAnim && m_tabHighlight && m_tabButtons[index])
    {
        QPushButton *targetBtn = m_tabButtons[index];
        int barWidth = targetBtn->width() - 8; // 比按钮略窄，视觉更精致
        if (barWidth < 20)
        {
            barWidth = 20;
        }
        m_tabHighlight->setFixedWidth(barWidth);

        int targetX = targetBtn->x() + (targetBtn->width() - barWidth) / 2;
        int y = m_tabBar->height() - 6; // 距底部 6px（与 Tab 栏底部边距对应）

        if (m_tabAnim->state() == QPropertyAnimation::Running)
        {
            m_tabAnim->stop();
        }
        m_tabAnim->setStartValue(m_highlightPos);
        m_tabAnim->setEndValue(targetX);
        // 动画过程中同步 Y 坐标（防止窗口未显示时 Y 未设置）
        m_tabHighlight->move(m_highlightPos, y);
        m_tabAnim->start();
    }

    // 处理数据刷新（原 onTabChanged 逻辑）
    onTabChanged(index);
}

void InstanceResourcesPage::updateHighlightGeometry()
{
    if (!m_tabHighlight || m_tabButtons.isEmpty() || m_currentType < 0
        || m_currentType >= m_tabButtons.size() || !m_tabButtons[m_currentType])
    {
        return;
    }

    QPushButton *btn = m_tabButtons[m_currentType];
    int barWidth = btn->width() - 8;
    if (barWidth < 20)
    {
        barWidth = 20;
    }
    m_tabHighlight->setFixedWidth(barWidth);

    int x = btn->x() + (btn->width() - barWidth) / 2;
    int y = m_tabBar->height() - 6;
    m_tabHighlight->move(x, y);
    m_highlightPos = x;
}

void InstanceResourcesPage::setHighlightPos(int pos)
{
    m_highlightPos = pos;
    if (m_tabHighlight && m_tabBar)
    {
        int y = m_tabBar->height() - 6;
        m_tabHighlight->move(pos, y);
    }
}

void InstanceResourcesPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // 窗口尺寸变化时同步指示条位置（无动画）
    updateHighlightGeometry();
}

void InstanceResourcesPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 首次显示时按钮几何已确定，初始化指示条位置
    updateHighlightGeometry();
}
