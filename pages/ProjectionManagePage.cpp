#include "ProjectionManagePage.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/LocalCategoryManager.h"
#include "utils/mod/ModData.h"
#include "utils/Schematic/SchematicCoverRenderer.h"
#include "components/LocalCategoryDialog.h"
#include "components/FavoriteFolderDialog.h"
#include "components/ProjectionBlockEditorDialog.h"
#include "components/AppFileDialog.h"
#include "components/ContentViewSwitch.h"
#include "components/MasonryContentCard.h"
#include "layouts/MasonryLayout.h"
#include "utils/CommandAssistant/LitematicReader.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QMimeData>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPointer>
#include "components/AppMessageBox.h"
#include <QSpacerItem>
#include <QUrl>

#include <algorithm>

ProjectionManagePage::ProjectionManagePage(QWidget *parent)
    : QWidget(parent)
    , m_searchEdit(nullptr)
    , m_viewSwitch(nullptr)
    , m_statsLabel(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGridLayout(nullptr)
    , m_cardFlowLayout(nullptr)
    , m_emptyLabel(nullptr)
    , m_bottomBar(nullptr)
    , m_openFolderBtn(nullptr)
    , m_addProjectionBtn(nullptr)
    , m_deleteProjectionBtn(nullptr)
    , m_blockEditorBtn(nullptr)
    , m_selectAllBtn(nullptr)
    , m_categoryManager(nullptr)
    , m_categoryCombo(nullptr)
    , m_categoryManageBtn(nullptr)
    , m_timeCombo(nullptr)
    , m_sizeCombo(nullptr)
    , m_sortCombo(nullptr)
{
    m_categoryManager = new LocalCategoryManager(this);
    m_categoryManager->setResourceType(QStringLiteral("schematics"));
    initUI();
}

ProjectionManagePage::~ProjectionManagePage()
{
}

void ProjectionManagePage::setInstancePath(const QString &path)
{
    if (m_instancePath != path)
    {
        m_instancePath = path;
        m_categoryManager->setInstancePath(path);
        // 封面材质来源 = 当前实例（资源包优先，回退客户端 jar）
        SchematicCoverRenderer::instance()->setTextureInstance(path);
        reloadCategoryCombo();
        if (!path.isEmpty())
        {
            loadProjections();
            placeCards();
        }
    }
}

void ProjectionManagePage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(10);

    // 顶部筛选卡：行1=搜索+分类，行2=时间/大小/排序+统计（两行统一布局）
    QWidget *filterCard = new QWidget(this);
    filterCard->setObjectName("filterCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(filterCard);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(8);

    // ---- 行1：搜索 + 分类 ----
    QHBoxLayout *topBarLayout = new QHBoxLayout();
    topBarLayout->setSpacing(8);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("modSearchEdit");
    m_searchEdit->setPlaceholderText(tr("搜索投影名称..."));
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->setClearButtonEnabled(true);
    topBarLayout->addWidget(m_searchEdit, 1);

    m_viewSwitch = new ContentViewSwitch(this);
    m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted(
        viewPrefKey(), ContentViewSwitch::Masonry));
    m_viewMode = static_cast<int>(m_viewSwitch->viewMode());
    topBarLayout->addWidget(m_viewSwitch);

    QLabel *categoryLabel = new QLabel(tr("分类:"), this);
    categoryLabel->setObjectName("filterLabel");
    topBarLayout->addWidget(categoryLabel);

    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->setObjectName("categoryCombo");
    m_categoryCombo->setFixedHeight(32);
    m_categoryCombo->setMinimumWidth(140);
    m_categoryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_categoryCombo->setMinimumContentsLength(5);
    reloadCategoryCombo();
    topBarLayout->addWidget(m_categoryCombo);

    m_categoryManageBtn = new QPushButton(tr("管理分类"), this);
    m_categoryManageBtn->setObjectName("categoryManageBtn");
    m_categoryManageBtn->setFixedHeight(32);
    m_categoryManageBtn->setCursor(Qt::PointingHandCursor);
    topBarLayout->addWidget(m_categoryManageBtn);

    cardLayout->addLayout(topBarLayout);

    // ---- 行2：时间 / 大小 / 排序 + 统计 ----
    QHBoxLayout *filterRow = new QHBoxLayout();
    filterRow->setSpacing(6);

    auto makeFilterLabel = [this](const QString &text) -> QLabel * {
        QLabel *label = new QLabel(text, this);
        label->setObjectName("filterLabel");
        return label;
    };
    auto makeFilterCombo = [this]() -> QComboBox * {
        QComboBox *combo = new QComboBox(this);
        combo->setObjectName("filterCombo");
        combo->setFixedHeight(32);
        combo->setMinimumWidth(104);
        return combo;
    };

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

    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_cardContainer = new QWidget();
    m_cardGridLayout = new QGridLayout(m_cardContainer);
    m_cardGridLayout->setContentsMargins(14, 0, 14, 0);
    m_cardGridLayout->setSpacing(8);
    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);

    // 空提示放滚动区外（放网格内会被 clearCards 删除，且筛空/空列表都要用）
    m_emptyLabel = new QLabel(tr("暂无投影文件，可点击底部「添加投影」按钮导入"));
    m_emptyLabel->setObjectName("exampleContentLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    mainLayout->addWidget(m_emptyLabel, 1);

    m_bottomBar = new QWidget();
    m_bottomBar->setObjectName("modBottomBar");
    m_bottomBar->setFixedHeight(48);
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomBar);
    bottomLayout->setContentsMargins(14, 6, 14, 6);
    bottomLayout->setSpacing(10);

    QColor themeColor = ThemeManager::instance()->currentThemeColor();

    m_openFolderBtn = new QPushButton(tr("打开文件夹"));
    m_openFolderBtn->setObjectName("bottomActionBtn");
    m_openFolderBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/folder.svg", themeColor, 18));
    m_openFolderBtn->setIconSize(QSize(18, 18));

    m_addProjectionBtn = new QPushButton(tr("添加投影"));
    m_addProjectionBtn->setObjectName("bottomActionBtn");
    m_addProjectionBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/install.svg", themeColor, 18));
    m_addProjectionBtn->setIconSize(QSize(18, 18));

    m_deleteProjectionBtn = new QPushButton(tr("删除投影"));
    m_deleteProjectionBtn->setObjectName("bottomActionBtn");
    m_deleteProjectionBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    m_deleteProjectionBtn->setIconSize(QSize(18, 18));

    m_blockEditorBtn = new QPushButton(tr("编辑投影方块"));
    m_blockEditorBtn->setObjectName("bottomActionBtn");
    m_blockEditorBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/brush.svg", QColor("#4CAF50"), 18));
    m_blockEditorBtn->setIconSize(QSize(18, 18));
    m_blockEditorBtn->setToolTip(tr("3D 立体编辑投影方块（放置/破坏/批处理）\n也可从方块盒子文件资源管理器选择投影"));

    m_selectAllBtn = new QPushButton(tr("全选"));
    m_selectAllBtn->setObjectName("bottomActionBtn");
    m_selectAllBtn->setCursor(Qt::PointingHandCursor);
    m_selectAllBtn->setToolTip(tr("选中全部可见投影，配合「删除投影」批量操作"));

    bottomLayout->addWidget(m_openFolderBtn);
    bottomLayout->addWidget(m_addProjectionBtn);
    bottomLayout->addWidget(m_selectAllBtn);
    bottomLayout->addWidget(m_deleteProjectionBtn);
    bottomLayout->addWidget(m_blockEditorBtn);
    bottomLayout->addStretch();
    mainLayout->addWidget(m_bottomBar);

    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        QString dirPath = projectionsDirPath();
        QDir dir(dirPath);
        if (!dir.exists())
            dir.mkpath(".");
        QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
    });

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ProjectionManagePage::onSearchTextChanged);
    connect(m_addProjectionBtn, &QPushButton::clicked,
            this, &ProjectionManagePage::onAddProjectionClicked);
    connect(m_selectAllBtn, &QPushButton::clicked,
            this, &ProjectionManagePage::onSelectAllClicked);
    connect(m_deleteProjectionBtn, &QPushButton::clicked,
            this, &ProjectionManagePage::onDeleteProjectionClicked);
    connect(m_blockEditorBtn, &QPushButton::clicked,
            this, &ProjectionManagePage::onBlockEditorClicked);
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectionManagePage::onCategoryFilterChanged);
    connect(m_categoryManageBtn, &QPushButton::clicked,
            this, &ProjectionManagePage::onCategoryManageClicked);
    connect(m_timeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectionManagePage::onTimeFilterChanged);
    connect(m_sizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectionManagePage::onSizeFilterChanged);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectionManagePage::onSortChanged);
connect(m_categoryManager, &LocalCategoryManager::categoriesChanged,
            this, &ProjectionManagePage::onCategoriesChanged);
    connect(m_categoryManager, &LocalCategoryManager::assignmentsChanged,
            this, &ProjectionManagePage::onAssignmentsChanged);
    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged,
            this, &ProjectionManagePage::onViewModeChanged);

    // 3D 投影方块编辑器为启动器内置功能，按钮始终可用
    if (m_blockEditorBtn)
        m_blockEditorBtn->setVisible(true);
}

QString ProjectionManagePage::projectionsDirPath() const
{
    if (m_instancePath.isEmpty())
        return QString();
    return m_instancePath + "/schematics";
}

void ProjectionManagePage::loadProjections()
{
    m_projectionFiles.clear();
    QString dirPath = projectionsDirPath();
    if (dirPath.isEmpty())
        return;

    QDir dir(dirPath);
    if (!dir.exists())
        return;

    QStringList nameFilters;
    nameFilters << "*.litematic" << "*.nbt" << "*.schematic" << "*.schem";
    QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files, QDir::Name);

    for (const QFileInfo &fi : files)
        m_projectionFiles.append(fi);
}

QString ProjectionManagePage::dimensionTextFor(const QFileInfo &fi)
{
    const QString key = fi.absoluteFilePath();
    auto it = m_dimensionCache.constFind(key);
    if (it != m_dimensionCache.constEnd())
        return it.value();

    QString text;
    const auto info = LitematicReader::readFile(key);
    if (info && info->enclosingSizeX > 0 && info->enclosingSizeY > 0 && info->enclosingSizeZ > 0)
        text = QString("%1×%2×%3").arg(info->enclosingSizeX)
                                   .arg(info->enclosingSizeY)
                                   .arg(info->enclosingSizeZ);
    m_dimensionCache.insert(key, text);
    return text;
}

void ProjectionManagePage::clearCards()
{
    // 先摘掉布局再删控件，避免 takeAt 时 widget 仍挂在 layout 上
    if (m_cardGridLayout)
    {
        delete m_cardGridLayout;
        m_cardGridLayout = nullptr;
    }
    if (m_cardFlowLayout)
    {
        delete m_cardFlowLayout;
        m_cardFlowLayout = nullptr;
    }

    const auto children = m_cardContainer->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : children)
    {
        w->hide();
        w->deleteLater();
    }

    m_cardWidgets.clear();
    m_selectionOverlays.clear();
    m_selectionEffects.clear();
    m_selectionAnims.clear();
    m_selectedCardIndices.clear();
    m_allSelected = false;
    m_dragSelectActive = false;
    m_dragMoved = false;
    m_dragAnchorIdx = -1;
    m_dragLastHoverIdx = -1;
}

QString ProjectionManagePage::viewPrefKey() const
{
    return QStringLiteral("projection_manage");
}

void ProjectionManagePage::onViewModeChanged(ContentViewSwitch::ViewMode mode)
{
    m_viewMode = static_cast<int>(mode);
    ContentViewSwitch::savePersisted(viewPrefKey(), mode);
    placeCards();
}

void ProjectionManagePage::ensureCardLayout()
{
    if (m_cardGridLayout || m_cardFlowLayout)
        return;

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
}

void ProjectionManagePage::requestCoverInto(QLabel *banner, const QString &filePath, int w, int h)
{
    if (!banner || filePath.isEmpty())
        return;

    QPointer<QLabel> guard(banner);
    // 两阶段：先色块大致图，后实例/资源包材质细化
    SchematicCoverRenderer::instance()->request(
        filePath, w, h,
        [guard, w, h](const QString &, const QImage &img, bool /*textured*/) {
            if (!guard || img.isNull())
                return;
            QPixmap pm = QPixmap::fromImage(img);
            guard->setPixmap(MasonryContentCard::makeCoverPixmap(pm, w, h));
        });
}

QWidget *ProjectionManagePage::createMasonryCard(const QFileInfo &fi)
{
    const QString fileName = fi.completeBaseName();
    const QString filePath = fi.absoluteFilePath();

    // 稳定色板：同路径渐变恒定，封面迟到时也有底
    ModInfo seed;
    seed.source = QStringLiteral("projection");
    seed.id = filePath;
    QColor c1, c2;
    MasonryContentCard::paletteFor(seed, c1, c2);

    const int kCardW = 260;
    const int kBorder = 1;
    const int kBannerH = 148;
    const int bannerW = kCardW - kBorder * 2;

    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("contentCard");
    card->setFixedWidth(kCardW);
    card->setCursor(Qt::PointingHandCursor);
    card->setToolTip(tr("双击编辑投影，右键设置分类"));
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    card->setProperty("projectionFilePath", filePath);
    card->installEventFilter(this);
    MasonryContentCard::applyShadow(card);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(kBorder, kBorder, kBorder, kBorder);
    cardLayout->setSpacing(0);

    QLabel *banner = new QLabel(card);
    banner->setObjectName("contentCardBanner");
    banner->setFixedHeight(kBannerH);
    banner->setAlignment(Qt::AlignCenter);
    banner->setPixmap(MasonryContentCard::makeBannerPixmap(c1, c2, bannerW, kBannerH));
    cardLayout->addWidget(banner);

    // 异步 3D 等距封面（后台渲染 + 磁盘缓存；失败保留渐变底）
    requestCoverInto(banner, filePath, 512, 296);

    QWidget *body = new QWidget(card);
    body->setObjectName("contentCardBody");
    QVBoxLayout *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 10, 12, 10);
    bodyLayout->setSpacing(6);

    QLabel *nameLabel = new QLabel(fileName, body);
    nameLabel->setObjectName("contentCardName");
    nameLabel->setProperty("cardRole", "name");
    nameLabel->setWordWrap(true);
    bodyLayout->addWidget(nameLabel);

    // chips：尺寸 / 大小 / 扩展名 / 日期 / 分类
    QWidget *chipsWidget = new QWidget(body);
    chipsWidget->setObjectName("contentCardChips");
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    const QString dimText = dimensionTextFor(fi);
    if (!dimText.isEmpty())
    {
        QLabel *dimChip = new QLabel(dimText, chipsWidget);
        dimChip->setObjectName("contentChip");
        chipsLayout->addWidget(dimChip);
    }

    const qint64 size = fi.size();
    QString sizeStr;
    if (size < 1024)
        sizeStr = QString("%1 B").arg(size);
    else if (size < 1024 * 1024)
        sizeStr = QString("%1 KB").arg(size / 1024.0, 0, 'f', 1);
    else
        sizeStr = QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 1);
    QLabel *sizeChip = new QLabel(sizeStr, chipsWidget);
    sizeChip->setObjectName("contentChip");
    chipsLayout->addWidget(sizeChip);

    QLabel *extChip = new QLabel(fi.suffix(), chipsWidget);
    extChip->setObjectName("contentChip");
    chipsLayout->addWidget(extChip);

    if (fi.lastModified().isValid())
    {
        QLabel *dateChip = new QLabel(fi.lastModified().toString("yyyy-MM-dd"), chipsWidget);
        dateChip->setObjectName("contentChip");
        chipsLayout->addWidget(dateChip);
    }

    const QString catName = m_categoryManager->categoryName(fi.fileName());
    if (!catName.isEmpty())
    {
        QLabel *catChip = new QLabel(catName, chipsWidget);
        catChip->setObjectName("contentChip");
        chipsLayout->addWidget(catChip);
    }
    chipsLayout->addStretch();
    bodyLayout->addWidget(chipsWidget);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 2, 0, 0);
    QColor themeColor = ThemeManager::instance()->currentThemeColor();

    auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton * {
        QPushButton *btn = new QPushButton(body);
        btn->setObjectName("modCardActionBtn");
        btn->setProperty("cardRole", "actionBtn");
        btn->setFixedSize(32, 32);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
        btn->setIconSize(QSize(18, 18));
        return btn;
    };

    QPushButton *editBtn = createBtn(":/Images/Icons/modify.svg", tr("编辑投影"));
    connect(editBtn, &QPushButton::clicked, this, [this, filePath]() {
        emit projectionEditRequested(filePath);
    });
    btnLayout->addWidget(editBtn);

    QPushButton *blockBtn = createBtn(":/Images/Icons/brush.svg", tr("3D 编辑投影方块"));
    connect(blockBtn, &QPushButton::clicked, this, [this, fi]() {
        const bool changed = ProjectionBlockEditorDialog::editProjection(
            this, fi.absoluteFilePath(), m_instancePath);
        if (changed)
        {
            loadProjections();
            placeCards();
        }
    });
    btnLayout->addWidget(blockBtn);

    QPushButton *folderBtn = createBtn(":/Images/Icons/folder.svg", tr("打开文件位置"));
    connect(folderBtn, &QPushButton::clicked, this, [filePath]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
    });
    btnLayout->addWidget(folderBtn);

    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"));
    deleteBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    connect(deleteBtn, &QPushButton::clicked, this, [this, filePath]() {
        deleteProjectionFile(filePath);
    });
    btnLayout->addWidget(deleteBtn);
    btnLayout->addStretch();
    bodyLayout->addLayout(btnLayout);
    bodyLayout->addStretch();
    cardLayout->addWidget(body, 1);

    createSelectionOverlay(card);
    return card;
}

QWidget *ProjectionManagePage::createListCard(const QFileInfo &fi, int row)
{
    QColor themeColor = ThemeManager::instance()->currentThemeColor();
    QString fileName = fi.completeBaseName();

    QWidget *card = new QWidget(m_cardContainer);
    card->setObjectName("modCardListItem");
    card->setProperty("cardRole", "container");
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);
    card->setToolTip(tr("双击编辑投影，右键设置分类"));
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    card->setProperty("projectionFilePath", fi.absoluteFilePath());
    card->installEventFilter(this);

    QHBoxLayout *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(14);

    QLabel *iconLabel = new QLabel(card);
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    QIcon bpIcon = IconHelper::loadColoredIcon(
        ":/Images/Icons/list.svg", themeColor, 36);
    iconLabel->setPixmap(bpIcon.pixmap(36, 36));
    iconLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(iconLabel);

    // 异步 3D 封面小图：渲染成功则替换列表图标
    QPointer<QLabel> iconGuard(iconLabel);
    SchematicCoverRenderer::instance()->request(
        fi.absoluteFilePath(), 96, 96,
        [iconGuard](const QString &, const QImage &img, bool /*textured*/) {
            if (!iconGuard || img.isNull())
                return;
            QPixmap pm = QPixmap::fromImage(img);
            iconGuard->setPixmap(pm.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        });

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *nameLabel = new QLabel(fileName, card);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    infoLayout->addWidget(nameLabel);

    QWidget *chipsWidget = new QWidget(card);
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    const QString dimText = dimensionTextFor(fi);
    if (!dimText.isEmpty())
    {
        QLabel *dimChip = new QLabel(dimText, chipsWidget);
        dimChip->setObjectName("modChip");
        dimChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(dimChip);
    }

    qint64 size = fi.size();
    QString sizeStr;
    if (size < 1024)
        sizeStr = QString("%1 B").arg(size);
    else if (size < 1024 * 1024)
        sizeStr = QString("%1 KB").arg(size / 1024.0, 0, 'f', 1);
    else
        sizeStr = QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 1);

    QLabel *sizeChip = new QLabel(sizeStr, chipsWidget);
    sizeChip->setObjectName("modChip");
    sizeChip->setProperty("cardRole", "chip");
    chipsLayout->addWidget(sizeChip);

    if (fi.lastModified().isValid())
    {
        QLabel *dateChip = new QLabel(
            fi.lastModified().toString("yyyy-MM-dd"), chipsWidget);
        dateChip->setObjectName("modChip");
        dateChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(dateChip);
    }

    QLabel *extChip = new QLabel(fi.suffix(), chipsWidget);
    extChip->setObjectName("modChip");
    extChip->setProperty("cardRole", "chip");
    chipsLayout->addWidget(extChip);

    const QString catName = m_categoryManager->categoryName(fi.fileName());
    if (!catName.isEmpty())
    {
        QLabel *catChip = new QLabel(catName, chipsWidget);
        catChip->setObjectName("modChip");
        catChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(catChip);
    }

    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);
    cardLayout->addLayout(infoLayout, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton* {
        QPushButton *btn = new QPushButton(card);
        btn->setObjectName("modCardActionBtn");
        btn->setProperty("cardRole", "actionBtn");
        btn->setFixedSize(32, 32);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
        btn->setIconSize(QSize(18, 18));
        return btn;
    };

    QPushButton *editBtn = createBtn(":/Images/Icons/modify.svg", tr("编辑投影"));
    connect(editBtn, &QPushButton::clicked, this, [this, fi]() {
        emit projectionEditRequested(fi.absoluteFilePath());
    });
    btnLayout->addWidget(editBtn);

    QPushButton *blockBtn = createBtn(":/Images/Icons/brush.svg", tr("3D 编辑投影方块"));
    connect(blockBtn, &QPushButton::clicked, this, [this, fi]() {
        const bool changed = ProjectionBlockEditorDialog::editProjection(this, fi.absoluteFilePath(), m_instancePath);
        if (changed)
        {
            loadProjections();
            placeCards();
        }
    });
    btnLayout->addWidget(blockBtn);

    QPushButton *folderBtn = createBtn(":/Images/Icons/folder.svg", tr("打开文件位置"));
    connect(folderBtn, &QPushButton::clicked, this, [fi]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    });
    btnLayout->addWidget(folderBtn);

    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除"));
    deleteBtn->setIcon(IconHelper::loadColoredIcon(
        ":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    connect(deleteBtn, &QPushButton::clicked, this, [this, fi]() {
        deleteProjectionFile(fi.absoluteFilePath());
    });
    btnLayout->addWidget(deleteBtn);

    cardLayout->addLayout(btnLayout);

    if (m_cardGridLayout)
        m_cardGridLayout->addWidget(card, row, 0, 1, 1);
    createSelectionOverlay(card);
    return card;
}

void ProjectionManagePage::placeCards()
{
    clearCards();

    if (m_projectionFiles.isEmpty())
    {
        m_emptyLabel->setText(tr("暂无投影文件，可点击底部「添加投影」按钮导入"));
        showEmptyHint(true);
        updateSelectionBar();
        return;
    }
    showEmptyHint(false);

    ensureCardLayout();

    // 按当前排序模式重排副本（不动持久化顺序 m_projectionFiles）
    QList<QFileInfo> sorted = m_projectionFiles;
    sortProjections(sorted);

    int row = 0;
    int visibleCount = 0;

    for (int i = 0; i < sorted.size(); ++i)
    {
        const QFileInfo &fi = sorted[i];
        QString fileName = fi.completeBaseName();

        if (!m_currentSearch.isEmpty())
        {
            if (!fileName.contains(m_currentSearch, Qt::CaseInsensitive))
                continue;
        }

        // 时间 / 大小 / 分类组合筛选
        if (!passesFilters(fi))
            continue;

        QWidget *card = nullptr;
        if (m_viewMode == ContentViewSwitch::List)
        {
            card = createListCard(fi, row);
            ++row;
        }
        else
        {
            card = createMasonryCard(fi);
            if (m_cardFlowLayout)
                m_cardFlowLayout->addWidget(card);
        }

        m_cardWidgets.append(card);
        visibleCount++;
    }

    if (m_viewMode == ContentViewSwitch::List && m_cardGridLayout)
    {
        QSpacerItem *spacer = new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding);
        m_cardGridLayout->addItem(spacer, row + 1, 0);
    }

    // 筛空（有文件但无可见项）：给提示并隐藏滚动区，恢复时由 updateSelectionBar 收尾
    if (visibleCount == 0)
    {
        m_emptyLabel->setText(tr("没有符合筛选条件的投影"));
        showEmptyHint(true);
    }
    updateSelectionBar();
}

void ProjectionManagePage::showEmptyHint(bool show)
{
    m_emptyLabel->setVisible(show);
    // 空提示占据列表区域时隐藏滚动区（提示在滚动区外，不再进网格被 clearCards 删除）
    m_scrollArea->setVisible(!show);
}

void ProjectionManagePage::onSearchTextChanged(const QString &text)
{
    m_currentSearch = text;
    placeCards();
}

void ProjectionManagePage::onAddProjectionClicked()
{
    QString dirPath = projectionsDirPath();
    QDir dir(dirPath);
    if (!dir.exists())
        dir.mkpath(".");

    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    if (!mimeData->hasUrls())
    {
        AppMessageBox::information(this, tr("添加投影"),
            tr("请先复制投影文件（.litematic / .nbt / .schematic / .schem）到剪贴板，然后点击「添加投影」。\n\n"
               "也可以点击「打开文件夹」手动放入文件。\n\n"
               "投影文件由 Litematica 等开源模组生成，存放于 schematics 目录下。"));
        return;
    }

    QStringList validSuffixes = { ".litematic", ".nbt", ".schematic", ".schem" };
    int copiedCount = 0;

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

        QString destPath = dirPath + "/" + fi.fileName();
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
            copiedCount++;
        else
            AppMessageBox::warning(this, tr("导入失败"),
                tr("无法复制文件 \"%1\"").arg(fi.fileName()));
    }

    if (copiedCount > 0)
    {
        loadProjections();
        placeCards();
    }
}

void ProjectionManagePage::onDeleteProjectionClicked()
{
    if (m_projectionFiles.isEmpty())
        return;

    // 选中了部分投影：批量删除选中的投影
    if (!m_selectedCardIndices.isEmpty())
    {
        QStringList paths;
        for (int idx : m_selectedCardIndices)
        {
            if (idx >= 0 && idx < m_cardWidgets.size())
                paths.append(m_cardWidgets[idx]->property("projectionFilePath").toString());
        }
        if (paths.isEmpty())
            return;

        AppMessageBox::StandardButton reply = AppMessageBox::question(
            this,
            tr("删除投影"),
            tr("确定要删除选中的 %1 个投影文件吗？此操作不可撤销。").arg(paths.size()),
            AppMessageBox::Yes | AppMessageBox::No,
            AppMessageBox::No);

        if (reply == AppMessageBox::Yes)
        {
            for (const QString &p : paths)
                QFile::remove(p);
            loadProjections();
            placeCards();
        }
        return;
    }

    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("删除投影"),
        tr("确定要删除所有投影文件吗？此操作不可撤销。"),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply == AppMessageBox::Yes)
    {
        for (const QFileInfo &fi : m_projectionFiles)
            QFile::remove(fi.absoluteFilePath());
        loadProjections();
        placeCards();
    }
}

void ProjectionManagePage::deleteProjectionFile(const QString &filePath)
{
    QFileInfo fi(filePath);
    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("删除投影"),
        tr("确定要删除投影 \"%1\" 吗？此操作不可撤销。").arg(fi.completeBaseName()),
        AppMessageBox::Yes | AppMessageBox::No,
        AppMessageBox::No);

    if (reply == AppMessageBox::Yes)
    {
        QFile::remove(filePath);
        loadProjections();
        placeCards();
    }
}

bool ProjectionManagePage::eventFilter(QObject *watched, QEvent *event)
{
    // 双击卡片进入投影编辑
    if (event->type() == QEvent::MouseButtonDblClick)
    {
        QWidget *card = qobject_cast<QWidget *>(watched);
        if (card && card->property("projectionFilePath").isValid())
        {
            emit projectionEditRequested(card->property("projectionFilePath").toString());
            return true;
        }
    }

    // 右键卡片：设置分类子菜单
    if (event->type() == QEvent::ContextMenu)
    {
        QWidget *ctxCard = qobject_cast<QWidget *>(watched);
        if (ctxCard && ctxCard->property("projectionFilePath").isValid())
        {
            QContextMenuEvent *ctx = static_cast<QContextMenuEvent *>(event);
            const QString fileName = QFileInfo(
                ctxCard->property("projectionFilePath").toString()).fileName();
            QMenu menu(this);
            appendCategoryMenu(&menu, fileName);
            menu.exec(ctx->globalPos());
            return true;
        }
    }

    QWidget *card = qobject_cast<QWidget *>(watched);
    if (!card)
        return QWidget::eventFilter(watched, event);

    int idx = m_cardWidgets.indexOf(card);
    if (idx < 0)
        return QWidget::eventFilter(watched, event);

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
                            selectCardDuringDrag(i);
                    }
                    else
                    {
                        // 反向滑动：取消勾选滑过的可见卡片
                        for (int i = m_dragLastHoverIdx - 1; i >= hoverIdx; --i)
                            deselectCardDuringDrag(i);
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
        if (idx >= 0 && idx < m_selectionOverlays.size())
            m_selectionOverlays[idx]->setGeometry(card->rect());
    }

    return QWidget::eventFilter(watched, event);
}

void ProjectionManagePage::onBlockEditorClicked()
{
    QString dirPath = projectionsDirPath();
    QDir dir(dirPath);
    if (!dir.exists())
        dir.mkpath(".");

    // 优先用方块盒子文件资源管理器选择 .litematic 投影
    const QString filter = tr("投影文件 (*.litematic *.nbt *.schematic *.schem);;Litematica (*.litematic);;全部文件 (*)");
    const QString file = AppFileDialog::getOpenFileName(this, tr("选择投影以编辑方块"),
                                                        dirPath, filter);
    if (file.isEmpty())
        return;

    const QString lowered = file.toLower();
    if (!lowered.endsWith(QStringLiteral(".litematic")))
    {
        // 非 litematic：先转换为 litematic 临时文件再编辑
        AppMessageBox::information(this, tr("提示"),
            tr("仅支持编辑 .litematic 格式的投影。\n请先选择 .litematic 文件，或使用现有「编辑投影」功能转换格式。"));
        return;
    }

    const bool changed = ProjectionBlockEditorDialog::editProjection(this, file, m_instancePath);
    if (changed)
    {
        // 刷新列表（文件大小/修改时间可能变化）
        loadProjections();
        placeCards();
    }
}

void ProjectionManagePage::onSelectAllClicked()
{
    m_allSelected = !m_allSelected;

    if (m_allSelected)
    {
        // 将所有可见卡片加入多选集合
        for (int i = 0; i < m_cardWidgets.size(); ++i)
        {
            m_selectedCardIndices.insert(i);
            animateCardHighlight(i, true);
        }
    }
    else
    {
        // 取消全选，清除所有样式
        for (int i = 0; i < m_cardWidgets.size(); ++i)
            animateCardHighlight(i, false);
        m_selectedCardIndices.clear();
    }

    updateSelectionBar();
}

void ProjectionManagePage::toggleCardSelection(int cardIdx)
{
    if (cardIdx < 0 || cardIdx >= m_cardWidgets.size())
        return;

    // 清除全选状态
    if (m_allSelected)
    {
        m_allSelected = false;
        if (m_selectAllBtn)
            m_selectAllBtn->setText(tr("全选"));
    }

    if (m_selectedCardIndices.contains(cardIdx))
    {
        m_selectedCardIndices.remove(cardIdx);
        animateCardHighlight(cardIdx, false);
    }
    else
    {
        m_selectedCardIndices.insert(cardIdx);
        animateCardHighlight(cardIdx, true);
    }

    updateSelectionBar();
}

void ProjectionManagePage::deselectAllCards()
{
    for (int idx : m_selectedCardIndices)
        animateCardHighlight(idx, false);
    m_selectedCardIndices.clear();
    updateSelectionBar();
}

int ProjectionManagePage::cardIndexAtGlobal(const QPoint &globalPos) const
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

void ProjectionManagePage::selectCardDuringDrag(int cardIdx)
{
    if (cardIdx < 0 || cardIdx >= m_cardWidgets.size())
        return;

    if (m_selectedCardIndices.contains(cardIdx))
        return;

    // 清除全选状态
    if (m_allSelected)
    {
        m_allSelected = false;
        if (m_selectAllBtn)
            m_selectAllBtn->setText(tr("全选"));
    }

    m_selectedCardIndices.insert(cardIdx);
    animateCardHighlight(cardIdx, true);
    updateSelectionBar();
}

void ProjectionManagePage::deselectCardDuringDrag(int cardIdx)
{
    if (cardIdx < 0 || cardIdx >= m_cardWidgets.size())
        return;

    if (!m_selectedCardIndices.contains(cardIdx))
        return;

    // 清除全选状态
    if (m_allSelected)
    {
        m_allSelected = false;
        if (m_selectAllBtn)
            m_selectAllBtn->setText(tr("全选"));
    }

    m_selectedCardIndices.remove(cardIdx);
    animateCardHighlight(cardIdx, false);
    updateSelectionBar();
}

QWidget *ProjectionManagePage::createSelectionOverlay(QWidget *card)
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

void ProjectionManagePage::animateCardHighlight(int idx, bool selected)
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

QString ProjectionManagePage::selectionOverlayStyle() const
{
    QColor c = ThemeManager::instance()->currentThemeColor();
    return QString("#cardSelectionOverlay { background-color: rgba(%1, %2, %3, 40); "
                   "border: 2px solid %4; border-radius: 6px; }")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.name());
}

void ProjectionManagePage::updateSelectionBar()
{
    const int total = m_cardWidgets.size();
    const int selected = m_selectedCardIndices.size();
    const int all = m_projectionFiles.size();
    const bool anyFilterActive = !m_currentSearch.isEmpty()
        || !m_currentCategoryId.isEmpty()
        || m_timeFilter != 0
        || m_sizeFilter != 0;

    if (selected == 0)
    {
        if (anyFilterActive)
            m_statsLabel->setText(tr("显示 %1 个投影 (共 %2 个)").arg(total).arg(all));
        else
            m_statsLabel->setText(tr("共 %1 个投影").arg(all));
    }
    else
    {
        if (anyFilterActive)
            m_statsLabel->setText(tr("显示 %1 个投影 (共 %2 个，已选 %3 个)")
                .arg(total).arg(all).arg(selected));
        else
            m_statsLabel->setText(tr("共 %1 个投影（已选 %2 个）").arg(total).arg(selected));
    }

    // 全选按钮文案：所有可见卡片都被选中时显示「取消全选」
    bool allSelected = !m_cardWidgets.isEmpty() && selected == total;
    m_allSelected = allSelected;
    if (m_selectAllBtn)
        m_selectAllBtn->setText(m_allSelected ? tr("取消全选") : tr("全选"));

    // 删除按钮文案：有选中时提示可批量删除
    if (m_deleteProjectionBtn)
    {
        if (selected == 0)
            m_deleteProjectionBtn->setText(tr("删除投影"));
        else
            m_deleteProjectionBtn->setText(tr("删除选中 (%1)").arg(selected));
    }
}


// ─────────────── 分类 / 时间 / 大小 / 排序筛选 ───────────────

void ProjectionManagePage::reloadCategoryCombo()
{
    if (!m_categoryCombo)
        return;

    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();
    m_categoryCombo->addItem(tr("全部"), QString());
    // 智能筛选：由文件属性直接判定，无需手动归类（本页无在线更新检查，不加"可更新"）
    m_categoryCombo->addItem(tr("最近修改"), QStringLiteral("__recent__"));
    m_categoryCombo->addItem(tr("大文件"), QStringLiteral("__large__"));
    m_categoryCombo->insertSeparator(m_categoryCombo->count());
    const QList<LocalResourceCategory> cats = m_categoryManager->categories();
    for (const LocalResourceCategory &c : cats)
    {
        m_categoryCombo->addItem(c.name, c.id);
    }
    m_categoryCombo->addItem(tr("未分类"), QStringLiteral("__uncategorized__"));

    // 恢复之前选中的分类（已被删除则回退「全部」）
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

bool ProjectionManagePage::passesFilters(const QFileInfo &fi) const
{
    // 时间筛选（按修改时间）
    if (m_timeFilter != 0)
    {
        int days = 0;
        if (m_timeFilter == 1)
            days = 7;
        else if (m_timeFilter == 2)
            days = 30;
        else if (m_timeFilter == 3)
            days = 365;
        if (fi.lastModified() < QDateTime::currentDateTime().addDays(-days))
            return false;
    }

    // 大小筛选
    if (m_sizeFilter != 0)
    {
        const qint64 k10MB = 10LL * 1024 * 1024;
        const qint64 k100MB = 100LL * 1024 * 1024;
        const qint64 size = fi.size();
        if (m_sizeFilter == 1 && size >= k10MB)
            return false;
        if (m_sizeFilter == 2 && (size < k10MB || size > k100MB))
            return false;
        if (m_sizeFilter == 3 && size <= k100MB)
            return false;
    }

    // 分类筛选（手动分类 + 智能筛选哨兵值）
    if (!m_currentCategoryId.isEmpty())
    {
        if (m_currentCategoryId == QStringLiteral("__uncategorized__"))
        {
            if (!m_categoryManager->categoryOf(fi.fileName()).isEmpty())
                return false;
        }
        else if (m_currentCategoryId == QStringLiteral("__recent__"))
        {
            if (fi.lastModified() < QDateTime::currentDateTime().addDays(-7))
                return false;
        }
        else if (m_currentCategoryId == QStringLiteral("__large__"))
        {
            if (fi.size() <= 100LL * 1024 * 1024)
                return false;
        }
        else if (m_categoryManager->categoryOf(fi.fileName()) != m_currentCategoryId)
        {
            return false;
        }
    }
    return true;
}

void ProjectionManagePage::sortProjections(QList<QFileInfo> &list) const
{
    const int mode = m_sortMode;
    std::sort(list.begin(), list.end(), [mode](const QFileInfo &a, const QFileInfo &b) {
        switch (mode)
        {
        case 1:  // 名称 Z→A
            return QString::compare(a.completeBaseName(), b.completeBaseName(), Qt::CaseInsensitive) > 0;
        case 2:  // 大小 大→小
            return a.size() > b.size();
        case 3:  // 大小 小→大
            return a.size() < b.size();
        case 4:  // 时间 新→旧
            return a.lastModified() > b.lastModified();
        case 5:  // 时间 旧→新
            return a.lastModified() < b.lastModified();
        default: // 名称 A→Z
            return QString::compare(a.completeBaseName(), b.completeBaseName(), Qt::CaseInsensitive) < 0;
        }
    });
}

void ProjectionManagePage::onCategoryFilterChanged(int index)
{
    if (!m_categoryCombo)
        return;
    m_currentCategoryId = m_categoryCombo->itemData(index).toString();
    placeCards();
}

void ProjectionManagePage::onCategoryManageClicked()
{
    LocalCategoryDialog dialog(m_categoryManager, this);
    dialog.exec();
}

void ProjectionManagePage::onCategoriesChanged()
{
    reloadCategoryCombo();
    placeCards();
}

void ProjectionManagePage::onAssignmentsChanged()
{
    placeCards();
}

void ProjectionManagePage::onTimeFilterChanged(int index)
{
    if (!m_timeCombo)
        return;
    m_timeFilter = m_timeCombo->itemData(index).toInt();
    placeCards();
}

void ProjectionManagePage::onSizeFilterChanged(int index)
{
    if (!m_sizeCombo)
        return;
    m_sizeFilter = m_sizeCombo->itemData(index).toInt();
    placeCards();
}

void ProjectionManagePage::onSortChanged(int index)
{
    if (!m_sortCombo)
        return;
    m_sortMode = m_sortCombo->itemData(index).toInt();
    placeCards();
}

void ProjectionManagePage::appendCategoryMenu(QMenu *menu, const QString &key)
{
    // ── 设置分类子菜单（键 = 文件名，含扩展名）──
    QMenu *categoryMenu = menu->addMenu(tr("设置分类"));
    const QString currentCat = m_categoryManager->categoryOf(key);
    const QList<LocalResourceCategory> cats = m_categoryManager->categories();
    for (const LocalResourceCategory &c : cats)
    {
        QAction *action = categoryMenu->addAction(c.name);
        action->setCheckable(true);
        action->setChecked(currentCat == c.id);
        connect(action, &QAction::triggered, this,
            [this, key, categoryId = c.id]() {
                m_categoryManager->assignCategory(key, categoryId);
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
        [this, key]() {
            m_categoryManager->assignCategory(key, QString());
        });
    categoryMenu->addSeparator();
    QAction *newCatAction = categoryMenu->addAction(tr("新建分类…"));
    connect(newCatAction, &QAction::triggered, this,
        [this, key]() {
            FavoriteFolderDialog dialog(FavoriteFolderDialog::CreateMode, this);
            dialog.setWindowTitle(tr("新建分类"));
            dialog.setPresets(LocalCategoryManager::suggestedNames(m_categoryManager->resourceType()));
            if (dialog.exec() != QDialog::Accepted)
                return;
            const QString name = dialog.folderName();
            if (name.isEmpty())
                return;
            const QString newId = m_categoryManager->addCategory(name);
            m_categoryManager->assignCategory(key, newId);
        });
}
