#include "ContentDetailPage.h"
#include "utils/IconHelper.h"
#include "utils/McimHelper.h"
#include "utils/ThemeManager.h"
#include "utils/FavoritesManager.h"
#include "components/NotificationManager.h"
#include "components/ScreenshotViewer.h"
#include <QDesktopServices>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QSpacerItem>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QStandardPaths>
#include <QPixmapCache>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QEasingCurve>

static QNetworkAccessManager *s_detailNAM = nullptr;
static QNetworkDiskCache *s_detailDiskCache = nullptr;

static QNetworkAccessManager *sharedDetailNAM()
{
    if (!s_detailNAM) {
        s_detailNAM = new QNetworkAccessManager();
        s_detailDiskCache = new QNetworkDiskCache();
        QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/detail_media";
        QDir().mkpath(cachePath);
        s_detailDiskCache->setCacheDirectory(cachePath);
        s_detailDiskCache->setMaximumCacheSize(50 * 1024 * 1024);
        s_detailNAM->setCache(s_detailDiskCache);
    }
    return s_detailNAM;
}

ContentDetailPage::ContentDetailPage(ContentType contentType, QWidget *parent)
    : QWidget(parent)
    , m_contentType(contentType)
    , m_scrollArea(nullptr)
    , m_contentWidget(nullptr)
    , m_contentLayout(nullptr)
    , m_headerWidget(nullptr)
    , m_iconLabel(nullptr)
    , m_chineseNameLabel(nullptr)
    , m_englishNameLabel(nullptr)
    , m_descriptionLabel(nullptr)
    , m_favoriteBtn(nullptr)
    , m_leftPanel(nullptr)
    , m_versionCombo(nullptr)
    , m_loaderCombo(nullptr)
    , m_dependenciesContainer(nullptr)
    , m_dependenciesLayout(nullptr)
    , m_downloadContainer(nullptr)
    , m_downloadListLayout(nullptr)
    , m_linksContainer(nullptr)
    , m_linksLayout(nullptr)
    , m_tagsContainer(nullptr)
    , m_descriptionContainer(nullptr)
    , m_descriptionBrowser(nullptr)
    , m_screenshotSection(nullptr)
    , m_screenshotContainer(nullptr)
    , m_screenshotStack(nullptr)
    , m_screenshotLabel(nullptr)
    , m_screenshotOverlay(nullptr)
    , m_screenshotAnimating(false)
    , m_screenshotAnimGroup(nullptr)
    , m_prevScreenshotBtn(nullptr)
    , m_nextScreenshotBtn(nullptr)
    , m_thumbnailStrip(nullptr)
    , m_thumbScrollArea(nullptr)
    , m_thumbScrollLeftBtn(nullptr)
    , m_thumbScrollRightBtn(nullptr)
    , m_currentScreenshotIndex(-1)
    , m_carouselTimer(new QTimer(this))
    , m_carouselResumeTimer(new QTimer(this))
    , m_networkManager(new QNetworkAccessManager(this))
    , m_downloadCurrentPage(0)
    , m_downloadPaginationBar(nullptr)
    , m_downloadPrevBtn(nullptr)
    , m_downloadNextBtn(nullptr)
    , m_downloadPageLabel(nullptr)
    , m_loadingOverlay(nullptr)
{
    initUI();
}

ContentDetailPage::~ContentDetailPage()
{
}

void ContentDetailPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_scrollArea = new QScrollArea();
    m_scrollArea->setObjectName("modDetailScrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_contentWidget = new QWidget();
    m_contentWidget->setObjectName("modDetailContent");
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(20, 20, 20, 20);
    m_contentLayout->setSpacing(16);

    m_scrollArea->setWidget(m_contentWidget);
    mainLayout->addWidget(m_scrollArea);

    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, [this]() { updateScreenshotNavStyle(); });

    // Carousel: advance every 5 seconds
    m_carouselTimer->setInterval(5000);
    connect(m_carouselTimer, &QTimer::timeout, this, [this]() {
        if (m_screenshotPixmaps.size() > 1 && m_currentScreenshotIndex >= 0) {
            int next = (m_currentScreenshotIndex + 1) % m_screenshotPixmaps.size();
            showScreenshot(next);
        }
    });

    // Resume carousel after 1 minute of inactivity
    m_carouselResumeTimer->setSingleShot(true);
    m_carouselResumeTimer->setInterval(60000);
    connect(m_carouselResumeTimer, &QTimer::timeout, this, [this]() {
        startCarousel();
    });

    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        stopCarousel();
        m_carouselResumeTimer->start();
    });
}

void ContentDetailPage::clearContent()
{
    while (QLayoutItem *item = m_contentLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->setVisible(false);
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_headerWidget = nullptr;
    m_iconLabel = nullptr;
    m_chineseNameLabel = nullptr;
    m_englishNameLabel = nullptr;
    m_descriptionLabel = nullptr;
    m_leftPanel = nullptr;
    m_versionCombo = nullptr;
    m_loaderCombo = nullptr;
    m_versionTypeCombo = nullptr;
    m_dependenciesContainer = nullptr;
    m_dependenciesLayout = nullptr;
    m_downloadContainer = nullptr;
    m_downloadListLayout = nullptr;
    m_linksContainer = nullptr;
    m_linksLayout = nullptr;
    m_tagsContainer = nullptr;
    m_descriptionContainer = nullptr;
    m_descriptionBrowser = nullptr;
    m_downloadCards.clear();
    m_downloadCurrentPage = 0;
    m_downloadPaginationBar = nullptr;
    m_downloadPrevBtn = nullptr;
    m_downloadNextBtn = nullptr;
    m_downloadPageLabel = nullptr;
    m_instanceGameVersion.clear();
    m_instanceLoaderType.clear();
    m_screenshotSection = nullptr;
    m_screenshotContainer = nullptr;
    m_screenshotLabel = nullptr;
    m_prevScreenshotBtn = nullptr;
    m_nextScreenshotBtn = nullptr;
    m_thumbnailStrip = nullptr;
    m_thumbScrollArea = nullptr;
    m_thumbScrollLeftBtn = nullptr;
    m_thumbScrollRightBtn = nullptr;
    m_screenshotUrls.clear();
    m_screenshotPixmaps.clear();
    m_currentScreenshotIndex = -1;
    m_carouselTimer->stop();
    m_carouselResumeTimer->stop();
}

void ContentDetailPage::setModInfo(const ModInfo &info)
{
    m_modInfo = info;

    // 初始化来源（CurseForge / Modrinth）
    if (m_currentSource.isEmpty() && !m_modInfo.source.isEmpty())
    {
        m_currentSource = m_modInfo.source;
    }
    if (!m_modInfo.id.isEmpty() && !m_currentSource.isEmpty())
    {
        m_currentItemId = m_currentSource + ":" + m_modInfo.id;
    }

    if (m_loadingOverlay) {
        auto config = ContentTypeConfig::getConfig(m_contentType);
        m_loadingOverlay->showOverlay(tr("正在获取%1详情...").arg(config.displayName));
    }

    clearContent();
    buildHeader();
    buildLeftSidebar();
    buildRightMain();
    loadIcon();
}

void ContentDetailPage::setInstanceFilter(const QString &gameVersion, const QString &loaderType)
{
    m_instanceGameVersion = gameVersion;
    m_instanceLoaderType = loaderType;

    if (m_versionCombo) m_versionCombo->blockSignals(true);
    if (m_loaderCombo) m_loaderCombo->blockSignals(true);

    if (m_versionCombo && !gameVersion.isEmpty()) {
        int idx = m_versionCombo->findText(gameVersion);
        if (idx < 0) {
            for (int i = 1; i < m_versionCombo->count(); ++i) {
                if (m_versionCombo->itemText(i).contains(gameVersion, Qt::CaseInsensitive)
                    || gameVersion.contains(m_versionCombo->itemText(i), Qt::CaseInsensitive)) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx >= 0)
            m_versionCombo->setCurrentIndex(idx);
    }
    if (m_loaderCombo && !loaderType.isEmpty()) {
        QString lower = loaderType.toLower();
        for (int i = 0; i < m_loaderCombo->count(); ++i) {
            if (m_loaderCombo->itemText(i).toLower().contains(lower)) {
                m_loaderCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    if (m_versionCombo) m_versionCombo->blockSignals(false);
    if (m_loaderCombo) m_loaderCombo->blockSignals(false);
}

void ContentDetailPage::setCurrentSource(const QString &source)
{
    m_currentSource = source;
    if (!m_modInfo.id.isEmpty() && !m_currentSource.isEmpty())
    {
        m_currentItemId = m_currentSource + ":" + m_modInfo.id;
    }
    updateFavoriteButtonIcon();
}

void ContentDetailPage::setReturnFolderId(const QString &folderId)
{
    m_returnFolderId = folderId;
}

void ContentDetailPage::refreshFavoriteState()
{
    updateFavoriteButtonIcon();
}

void ContentDetailPage::updateFavoriteButtonIcon()
{
    if (!m_favoriteBtn)
    {
        return;
    }

    if (m_modInfo.id.isEmpty() || m_currentSource.isEmpty())
    {
        m_currentItemId.clear();
    }
    else
    {
        m_currentItemId = m_currentSource + ":" + m_modInfo.id;
    }

    QList<QString> containingFolders = FavoritesManager::instance()->foldersContaining(m_currentItemId);
    bool isFav = !containingFolders.isEmpty();

    QColor iconColor = isFav ? QColor("#ffc107") : QColor("#888888");
    m_favoriteBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/star.svg", iconColor, 20));
    m_favoriteBtn->setToolTip(isFav ? tr("已收藏，点击管理") : tr("加入收藏夹"));
}

void ContentDetailPage::showFavoriteMenu()
{
    if (m_currentItemId.isEmpty() || m_modInfo.id.isEmpty())
    {
        NotificationManager::showInfo(this, tr("当前资源尚未加载完成，无法收藏"));
        return;
    }

    QList<FavoriteFolder> folders = FavoritesManager::instance()->folders();
    QList<QString> containingIds = FavoritesManager::instance()->foldersContaining(m_currentItemId);

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

    QAction *chosen = menu.exec(m_favoriteBtn->mapToGlobal(QPoint(0, m_favoriteBtn->height() + 4)));
    if (!chosen)
    {
        return;
    }

    if (chosen == newFolderAct)
    {
        // 复用 MainWindow 的对话框逻辑：触发信号让 MainWindow 弹窗
        emit addToFavoritesRequested(m_modInfo, m_contentType, m_currentSource);
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
        FavoritesManager::instance()->removeFavorite(folderId, m_currentItemId);
    }
    else
    {
        FavoritesManager::instance()->addFavorite(folderId, m_modInfo, m_contentType, m_currentSource);
    }
    updateFavoriteButtonIcon();
}

void ContentDetailPage::buildHeader()
{
    m_headerWidget = new QWidget();
    m_headerWidget->setObjectName("modDetailHeader");
    QHBoxLayout *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 16);
    headerLayout->setSpacing(16);

    m_iconLabel = new QLabel();
    m_iconLabel->setObjectName("modDetailIcon");
    m_iconLabel->setFixedSize(64, 64);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    QPixmap placeholder(64, 64);
    placeholder.fill(QColor("#e8e8e8"));
    m_iconLabel->setPixmap(placeholder);
    headerLayout->addWidget(m_iconLabel);

    QWidget *nameWidget = new QWidget();
    QVBoxLayout *nameLayout = new QVBoxLayout(nameWidget);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(4);

    QString displayName = m_modInfo.chineseName.isEmpty()
        ? m_modInfo.name
        : m_modInfo.chineseName;

    m_chineseNameLabel = new QLabel(displayName);
    m_chineseNameLabel->setObjectName("modDetailChineseName");
    nameLayout->addWidget(m_chineseNameLabel);

    QString engName = m_modInfo.englishName.isEmpty() ? m_modInfo.name : m_modInfo.englishName;
    if (!m_modInfo.chineseName.isEmpty() && engName == m_modInfo.name) {
        engName = m_modInfo.name;
    }
    m_englishNameLabel = new QLabel(engName);
    m_englishNameLabel->setObjectName("modDetailEnglishName");
    nameLayout->addWidget(m_englishNameLabel);

    m_descriptionLabel = new QLabel(m_modInfo.description);
    m_descriptionLabel->setObjectName("modDetailDesc");
    m_descriptionLabel->setWordWrap(true);
    nameLayout->addWidget(m_descriptionLabel);

    headerLayout->addWidget(nameWidget, 1);

    // 收藏按钮
    m_favoriteBtn = new QPushButton(m_headerWidget);
    m_favoriteBtn->setObjectName("contentFavoriteBtn");
    m_favoriteBtn->setCursor(Qt::PointingHandCursor);
    m_favoriteBtn->setToolTip(tr("加入收藏夹"));
    m_favoriteBtn->setFixedSize(40, 40);
    m_favoriteBtn->setIconSize(QSize(20, 20));
    m_favoriteBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #ffffff;"
        "  border: 1px solid #e0e0e0;"
        "  border-radius: 10px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #fff8e1;"
        "  border-color: #ffc107;"
        "}"
        );
    connect(m_favoriteBtn, &QPushButton::clicked, this, &ContentDetailPage::showFavoriteMenu);
    headerLayout->addWidget(m_favoriteBtn);

    m_contentLayout->addWidget(m_headerWidget);
    updateFavoriteButtonIcon();
}

void ContentDetailPage::buildLeftSidebar()
{
    m_leftPanel = new QWidget();
    m_leftPanel->setObjectName("modDetailLeftPanel");
    m_leftPanel->setAttribute(Qt::WA_StyledBackground);

    QVBoxLayout *leftLayout = new QVBoxLayout(m_leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(16);

    buildFilterBlock(leftLayout);
    buildDependenciesBlock(leftLayout);
    buildDownloadListBlock(leftLayout);
    leftLayout->addStretch();
}

void ContentDetailPage::buildRightMain()
{
    QWidget *rightPanel = new QWidget();
    rightPanel->setObjectName("modDetailRightPanel");
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(16);

    buildScreenshotBlock(rightLayout);
    buildLinksBlock(rightLayout);
    buildTagsBlock(rightLayout);
    buildDescriptionBlock(rightLayout);
    rightLayout->addStretch();

    QWidget *mainBody = new QWidget();
    mainBody->setObjectName("modDetailBody");
    QHBoxLayout *bodyLayout = new QHBoxLayout(mainBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(16);

    bodyLayout->addWidget(m_leftPanel, 2);
    bodyLayout->addWidget(rightPanel, 3);

    m_contentLayout->addWidget(mainBody, 1);
}

void ContentDetailPage::buildFilterBlock(QVBoxLayout *parentLayout)
{
    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    QVBoxLayout *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(10);

    QLabel *title = new QLabel(tr("筛选选项"));
    title->setObjectName("modDetailSectionTitle");
    sectionLayout->addWidget(title);

    m_versionCombo = new QComboBox();
    m_versionCombo->setObjectName("modDetailCombo");
    m_versionCombo->addItem(tr("全部版本"));
    for (const QString &v : m_modInfo.gameVersions)
        m_versionCombo->addItem(v);
    sectionLayout->addWidget(m_versionCombo);

    m_loaderCombo = new QComboBox();
    m_loaderCombo->setObjectName("modDetailCombo");
    m_loaderCombo->addItem(tr("全部加载器"));
    QStringList commonLoaders = {"forge", "fabric", "neoforge", "quilt"};
    for (const QString &l : commonLoaders)
        m_loaderCombo->addItem(l);
    sectionLayout->addWidget(m_loaderCombo);

    m_versionTypeCombo = new QComboBox();
    m_versionTypeCombo->setObjectName("modDetailCombo");
    m_versionTypeCombo->addItem(tr("全部"));
    m_versionTypeCombo->addItem(tr("正式版"));
    m_versionTypeCombo->addItem(tr("测试版"));
    m_versionTypeCombo->addItem(tr("Alpha"));
    sectionLayout->addWidget(m_versionTypeCombo);

    connect(m_versionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { applyDownloadFilter(); });
    connect(m_loaderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { applyDownloadFilter(); });
    connect(m_versionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { applyDownloadFilter(); });

    parentLayout->addWidget(section);
}

void ContentDetailPage::buildDependenciesBlock(QVBoxLayout *parentLayout)
{
    if (m_modInfo.dependencies.isEmpty())
        return;

    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    QVBoxLayout *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(8);

    auto config = ContentTypeConfig::getConfig(m_contentType);
    QLabel *title = new QLabel(tr("前置%1").arg(config.displayName));
    title->setObjectName("modDetailSectionTitle");
    sectionLayout->addWidget(title);

    m_dependenciesContainer = new QWidget();
    m_dependenciesLayout = new QVBoxLayout(m_dependenciesContainer);
    m_dependenciesLayout->setContentsMargins(0, 0, 0, 0);
    m_dependenciesLayout->setSpacing(6);

    for (const ModDependency &dep : m_modInfo.dependencies) {
        QWidget *depCard = new QWidget();
        depCard->setObjectName("modDetailDepCard");
        depCard->setProperty("depRequired", dep.isRequired);
        QHBoxLayout *depLayout = new QHBoxLayout(depCard);
        depLayout->setContentsMargins(8, 6, 8, 6);
        depLayout->setSpacing(8);

        QLabel *nameLabel = new QLabel(dep.name);
        nameLabel->setObjectName("modDetailDepName");
        depLayout->addWidget(nameLabel, 1);

        QLabel *tagLabel = new QLabel(dep.isRequired ? tr("必备") : tr("可选"));
        tagLabel->setObjectName("modDetailDepTag");
        depLayout->addWidget(tagLabel);

        m_dependenciesLayout->addWidget(depCard);
    }

    sectionLayout->addWidget(m_dependenciesContainer);
    parentLayout->addWidget(section);
}

void ContentDetailPage::buildDownloadListBlock(QVBoxLayout *parentLayout)
{
    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    QVBoxLayout *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(8);

    auto config = ContentTypeConfig::getConfig(m_contentType);
    QLabel *title = new QLabel(config.downloadLabel);
    title->setObjectName("modDetailSectionTitle");
    sectionLayout->addWidget(title);

    m_downloadContainer = new QWidget();
    m_downloadListLayout = new QVBoxLayout(m_downloadContainer);
    m_downloadListLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadListLayout->setSpacing(6);

    if (m_modInfo.versionFiles.isEmpty()) {
        ModVersionFile vf;
        vf.version = m_modInfo.latestVersion;
        vf.gameVersions = m_modInfo.gameVersions;
        vf.loaders = m_modInfo.loaders;
        vf.downloadUrl = m_modInfo.downloadUrl;
        QWidget *fileCard = createDownloadCard(vf);
        m_downloadListLayout->addWidget(fileCard);
        DownloadCardEntry entry;
        entry.gameVersions = vf.gameVersions;
        entry.loaders = vf.loaders;
        entry.releaseType = vf.releaseType;
        entry.widget = fileCard;
        m_downloadCards.append(entry);
    } else {
        for (const ModVersionFile &vf : m_modInfo.versionFiles) {
            QWidget *fileCard = createDownloadCard(vf);
            m_downloadListLayout->addWidget(fileCard);
            DownloadCardEntry entry;
            entry.gameVersions = vf.gameVersions;
            entry.loaders = vf.loaders;
            entry.releaseType = vf.releaseType;
            entry.widget = fileCard;
            m_downloadCards.append(entry);
        }
    }

    sectionLayout->addWidget(m_downloadContainer);

    // 分页控制栏（微透明玻璃态卡片，与资源列表页底部翻页栏一致）
    m_downloadPaginationBar = new QWidget();
    m_downloadPaginationBar->setObjectName("modPaginationBar");
    QHBoxLayout *paginationLayout = new QHBoxLayout(m_downloadPaginationBar);
    paginationLayout->setContentsMargins(14, 8, 14, 8);
    paginationLayout->setSpacing(6);

    m_downloadPrevBtn = new QPushButton();
    m_downloadPrevBtn->setObjectName("modDetailPageBtn");
    m_downloadPrevBtn->setFixedSize(28, 28);
    m_downloadPrevBtn->setCursor(Qt::PointingHandCursor);
    m_downloadPrevBtn->setEnabled(false);
    connect(m_downloadPrevBtn, &QPushButton::clicked, this, &ContentDetailPage::onDownloadPrevPage);

    m_downloadPageLabel = new QLabel();
    m_downloadPageLabel->setObjectName("modDetailPageLabel");
    m_downloadPageLabel->setAlignment(Qt::AlignCenter);

    m_downloadNextBtn = new QPushButton();
    m_downloadNextBtn->setObjectName("modDetailPageBtn");
    m_downloadNextBtn->setFixedSize(28, 28);
    m_downloadNextBtn->setCursor(Qt::PointingHandCursor);
    m_downloadNextBtn->setEnabled(false);
    connect(m_downloadNextBtn, &QPushButton::clicked, this, &ContentDetailPage::onDownloadNextPage);

    paginationLayout->addStretch();
    paginationLayout->addWidget(m_downloadPrevBtn);
    paginationLayout->addWidget(m_downloadPageLabel);
    paginationLayout->addWidget(m_downloadNextBtn);
    paginationLayout->addStretch();

    m_downloadPaginationBar->hide();
    sectionLayout->addWidget(m_downloadPaginationBar);

    parentLayout->addWidget(section);
}

QWidget *ContentDetailPage::createDownloadCard(const ModVersionFile &vf)
{
    QWidget *card = new QWidget();
    card->setObjectName("modDetailDownloadCard");
    QHBoxLayout *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(10, 8, 10, 8);
    cardLayout->setSpacing(10);

    QWidget *infoWidget = new QWidget();
    QVBoxLayout *infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    QLabel *verLabel = new QLabel(vf.version.isEmpty() ? tr("最新版本") : vf.version);
    verLabel->setObjectName("modDetailDownloadVer");
    infoLayout->addWidget(verLabel);

    QWidget *chipRow = new QWidget();
    QHBoxLayout *chipLayout = new QHBoxLayout(chipRow);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(4);
    if (!vf.releaseType.isEmpty()) {
        QLabel *typeBadge = new QLabel();
        if (vf.releaseType == "beta") {
            typeBadge->setText(tr("测试版"));
            typeBadge->setObjectName("modDetailVersionBadge-beta");
        } else if (vf.releaseType == "alpha") {
            typeBadge->setText("Alpha");
            typeBadge->setObjectName("modDetailVersionBadge-alpha");
        } else {
            typeBadge->setText(tr("正式版"));
            typeBadge->setObjectName("modDetailVersionBadge-release");
        }
        typeBadge->setProperty("cardRole", "chip");
        chipLayout->addWidget(typeBadge);
    }
    if (!vf.gameVersions.isEmpty())
        chipLayout->addWidget(createChip(vf.gameVersions.first()));
    if (!vf.loaders.isEmpty())
        chipLayout->addWidget(createChip(vf.loaders.first()));
    chipLayout->addStretch();
    infoLayout->addWidget(chipRow);

    cardLayout->addWidget(infoWidget, 1);

    QPushButton *dlBtn = new QPushButton();
    dlBtn->setObjectName("modDetailDownloadBtn");
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    dlBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/download.svg", themeColor, 18));
    dlBtn->setIconSize(QSize(18, 18));
    dlBtn->setFixedSize(32, 32);
    dlBtn->setToolTip(tr("下载"));
    dlBtn->setCursor(Qt::PointingHandCursor);
    if (!vf.downloadUrl.isEmpty()) {
        connect(dlBtn, &QPushButton::clicked, this, [this, vf]() {
            emit downloadRequested(m_modInfo, vf);
        });
    }
    cardLayout->addWidget(dlBtn);

    return card;
}

void ContentDetailPage::applyDownloadFilter()
{
    QString selectedVersion;
    QString selectedLoader;
    QString selectedType;
    if (m_versionCombo && m_versionCombo->currentIndex() > 0)
        selectedVersion = m_versionCombo->currentText();
    if (m_loaderCombo && m_loaderCombo->currentIndex() > 0)
        selectedLoader = m_loaderCombo->currentText();
    if (m_versionTypeCombo) {
        int typeIdx = m_versionTypeCombo->currentIndex();
        if (typeIdx == 1)
            selectedType = "release";
        else if (typeIdx == 2)
            selectedType = "beta";
        else if (typeIdx == 3)
            selectedType = "alpha";
    }

    // 先计算匹配的索引列表
    QList<int> matchedIndices;
    for (int i = 0; i < m_downloadCards.size(); ++i) {
        const DownloadCardEntry &entry = m_downloadCards[i];
        bool versionMatch = selectedVersion.isEmpty();
        if (!versionMatch) {
            for (const QString &gv : entry.gameVersions) {
                if (gv.contains(selectedVersion, Qt::CaseInsensitive)) {
                    versionMatch = true;
                    break;
                }
            }
        }
        bool loaderMatch = selectedLoader.isEmpty();
        if (!loaderMatch) {
            for (const QString &l : entry.loaders) {
                if (l.contains(selectedLoader, Qt::CaseInsensitive)) {
                    loaderMatch = true;
                    break;
                }
            }
        }
        bool typeMatch = selectedType.isEmpty() || entry.releaseType == selectedType;
        if (versionMatch && loaderMatch && typeMatch)
            matchedIndices.append(i);
    }

    // 分页：计算总页数并限制当前页
    int totalPages = (matchedIndices.size() + kDownloadPageSize - 1) / kDownloadPageSize;
    if (totalPages <= 0) totalPages = 1;
    if (m_downloadCurrentPage >= totalPages)
        m_downloadCurrentPage = totalPages - 1;
    if (m_downloadCurrentPage < 0)
        m_downloadCurrentPage = 0;

    int startIdx = m_downloadCurrentPage * kDownloadPageSize;
    int endIdx = qMin(startIdx + kDownloadPageSize, matchedIndices.size());

    // 构建当前页应显示的索引集合
    QSet<int> visibleSet;
    for (int i = startIdx; i < endIdx; ++i)
        visibleSet.insert(matchedIndices[i]);

    for (int i = 0; i < m_downloadCards.size(); ++i) {
        m_downloadCards[i].widget->setVisible(visibleSet.contains(i));
    }

    updateDownloadPagination();
}

void ContentDetailPage::updateDownloadPagination()
{
    // 统计当前筛选后总条数
    QString selectedVersion;
    QString selectedLoader;
    QString selectedType;
    if (m_versionCombo && m_versionCombo->currentIndex() > 0)
        selectedVersion = m_versionCombo->currentText();
    if (m_loaderCombo && m_loaderCombo->currentIndex() > 0)
        selectedLoader = m_loaderCombo->currentText();
    if (m_versionTypeCombo) {
        int typeIdx = m_versionTypeCombo->currentIndex();
        if (typeIdx == 1) selectedType = "release";
        else if (typeIdx == 2) selectedType = "beta";
        else if (typeIdx == 3) selectedType = "alpha";
    }

    int totalMatched = 0;
    for (const DownloadCardEntry &entry : m_downloadCards) {
        bool vm = selectedVersion.isEmpty();
        if (!vm) {
            for (const QString &gv : entry.gameVersions) {
                if (gv.contains(selectedVersion, Qt::CaseInsensitive)) { vm = true; break; }
            }
        }
        bool lm = selectedLoader.isEmpty();
        if (!lm) {
            for (const QString &l : entry.loaders) {
                if (l.contains(selectedLoader, Qt::CaseInsensitive)) { lm = true; break; }
            }
        }
        bool tm = selectedType.isEmpty() || entry.releaseType == selectedType;
        if (vm && lm && tm) totalMatched++;
    }

    int totalPages = (totalMatched + kDownloadPageSize - 1) / kDownloadPageSize;
    if (totalPages <= 0) totalPages = 1;

    bool needPagination = totalPages > 1;
    if (m_downloadPaginationBar)
        m_downloadPaginationBar->setVisible(needPagination);

    if (!needPagination)
        return;

    if (m_downloadPageLabel)
        m_downloadPageLabel->setText(tr("第 %1 / %2 页").arg(m_downloadCurrentPage + 1).arg(totalPages));
    if (m_downloadPrevBtn)
        m_downloadPrevBtn->setEnabled(m_downloadCurrentPage > 0);
    if (m_downloadNextBtn)
        m_downloadNextBtn->setEnabled(m_downloadCurrentPage < totalPages - 1);

    // 更新上/下箭头图标
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    if (m_downloadPrevBtn) {
        m_downloadPrevBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/chevron_left.svg", themeColor, 14));
        m_downloadPrevBtn->setIconSize(QSize(14, 14));
        m_downloadPrevBtn->setStyleSheet(
            "QPushButton { border: none; border-radius: 14px; background: #f0f0f0; }"
            "QPushButton:hover { background: #e0e0e0; }"
            "QPushButton:disabled { background: #f8f8f8; }");
    }
    if (m_downloadNextBtn) {
        m_downloadNextBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/chevron_right.svg", themeColor, 14));
        m_downloadNextBtn->setIconSize(QSize(14, 14));
        m_downloadNextBtn->setStyleSheet(
            "QPushButton { border: none; border-radius: 14px; background: #f0f0f0; }"
            "QPushButton:hover { background: #e0e0e0; }"
            "QPushButton:disabled { background: #f8f8f8; }");
    }
}

void ContentDetailPage::onDownloadPrevPage()
{
    if (m_downloadCurrentPage > 0) {
        m_downloadCurrentPage--;
        applyDownloadFilter();
    }
}

void ContentDetailPage::onDownloadNextPage()
{
    m_downloadCurrentPage++;
    applyDownloadFilter();
}

void ContentDetailPage::buildLinksBlock(QVBoxLayout *parentLayout)
{
    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    QVBoxLayout *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(10);

    QLabel *title = new QLabel(tr("相关链接"));
    title->setObjectName("modDetailSectionTitle");
    sectionLayout->addWidget(title);

    m_linksContainer = new QWidget();
    m_linksLayout = new QHBoxLayout(m_linksContainer);
    m_linksLayout->setContentsMargins(0, 0, 0, 0);
    m_linksLayout->setSpacing(8);
    m_linksLayout->addStretch();

    struct LinkInfo {
        QString label;
        QString url;
        bool alwaysShow;
    };

    QList<LinkInfo> links = {
        {tr("MC百科"), m_modInfo.mcmodUrl, false},
        {tr("CurseForge"), QString(), false},
        {tr("Modrinth"), QString(), false},
        {tr("必应搜索"), QString("https://www.bing.com/search?q=%1").arg(m_modInfo.name), true},
        {tr("百度搜索"), QString("https://www.baidu.com/s?wd=%1").arg(m_modInfo.name), true},
        {tr("AI介绍"), QString(), true}
    };

    if (m_modInfo.source == "curseforge" || m_modInfo.pageUrl.contains("curseforge"))
        links[1].url = m_modInfo.pageUrl;
    if (m_modInfo.source == "modrinth" || m_modInfo.pageUrl.contains("modrinth"))
        links[2].url = m_modInfo.pageUrl;

    for (const LinkInfo &link : links) {
        if (!link.alwaysShow && link.url.isEmpty())
            continue;

        QPushButton *btn = new QPushButton(link.label);
        btn->setObjectName("modDetailLinkBtn");
        btn->setCursor(Qt::PointingHandCursor);
        if (!link.url.isEmpty()) {
            connect(btn, &QPushButton::clicked, this, [link]() {
                QDesktopServices::openUrl(QUrl(link.url));
            });
        } else {
            btn->setEnabled(false);
        }
        m_linksLayout->addWidget(btn);
    }

    m_linksLayout->addStretch();
    sectionLayout->addWidget(m_linksContainer);
    parentLayout->addWidget(section);
}

void ContentDetailPage::buildTagsBlock(QVBoxLayout *parentLayout)
{
    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    QVBoxLayout *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(10);

    QLabel *title = new QLabel(tr("标签信息"));
    title->setObjectName("modDetailSectionTitle");
    sectionLayout->addWidget(title);

    m_tagsContainer = new QWidget();
    QVBoxLayout *tagsLayout = new QVBoxLayout(m_tagsContainer);
    tagsLayout->setContentsMargins(0, 0, 0, 0);
    tagsLayout->setSpacing(6);

    auto addTagRow = [&](const QString &key, const QString &value) {
        if (value.isEmpty()) return;
        QWidget *row = new QWidget();
        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        QLabel *keyLabel = new QLabel(key);
        keyLabel->setObjectName("modDetailTagKey");
        rowLayout->addWidget(keyLabel);

        QLabel *valueLabel = new QLabel(value);
        valueLabel->setObjectName("modDetailTagValue");
        valueLabel->setWordWrap(true);
        rowLayout->addWidget(valueLabel, 1);

        tagsLayout->addWidget(row);
    };

    addTagRow(tr("作者"), m_modInfo.author);
    if (m_modInfo.downloadCount > 0)
        addTagRow(tr("下载量"), QString::number(m_modInfo.downloadCount));
    if (m_modInfo.rank > 0)
        addTagRow(tr("排行"), QString::number(m_modInfo.rank));
    if (m_modInfo.dateModified.isValid())
        addTagRow(tr("更新时间"), m_modInfo.dateModified.toString("yyyy-MM-dd HH:mm"));
    if (m_modInfo.datePublished.isValid())
        addTagRow(tr("发布时间"), m_modInfo.datePublished.toString("yyyy-MM-dd HH:mm"));
    if (m_modInfo.followers > 0)
        addTagRow(tr("关注量"), QString::number(m_modInfo.followers));
    if (!m_modInfo.modType.isEmpty())
        addTagRow(tr("类型"), m_modInfo.modType);
    if (!m_modInfo.versionRange.isEmpty())
        addTagRow(tr("支持版本"), m_modInfo.versionRange);
    else if (!m_modInfo.gameVersions.isEmpty())
        addTagRow(tr("支持版本"), m_modInfo.gameVersions.first() + " - " + m_modInfo.gameVersions.last());
    if (!m_modInfo.loaders.isEmpty())
        addTagRow(tr("支持加载器"), m_modInfo.loaders.join(", "));

    sectionLayout->addWidget(m_tagsContainer);
    parentLayout->addWidget(section);
}

void ContentDetailPage::buildDescriptionBlock(QVBoxLayout *parentLayout)
{
    QString desc = m_modInfo.detailedDescription.isEmpty()
        ? m_modInfo.description
        : m_modInfo.detailedDescription;

    if (desc.isEmpty())
        return;

    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    QVBoxLayout *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(10);

    QLabel *title = new QLabel(tr("详细信息"));
    title->setObjectName("modDetailSectionTitle");
    sectionLayout->addWidget(title);

    m_descriptionBrowser = new QTextBrowser();
    m_descriptionBrowser->setObjectName("modDetailDescBrowser");
    m_descriptionBrowser->setOpenExternalLinks(true);
    m_descriptionBrowser->setReadOnly(true);
    m_descriptionBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_descriptionBrowser->setHtml(desc);
    sectionLayout->addWidget(m_descriptionBrowser);

    parentLayout->addWidget(section);
}

void ContentDetailPage::buildScreenshotBlock(QVBoxLayout *parentLayout)
{
    m_screenshotSection = new QWidget();
    m_screenshotSection->setObjectName("modDetailSection");
    m_screenshotSection->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QVBoxLayout *sectionLayout = new QVBoxLayout(m_screenshotSection);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(10);

    QLabel *title = new QLabel(tr("游戏截图"));
    title->setObjectName("modDetailSectionTitle");
    title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    sectionLayout->addWidget(title);

    // Screenshot display with navigation arrows overlaid
    m_screenshotContainer = new QWidget();
    m_screenshotContainer->setObjectName("modDetailScreenshotContainer");
    m_screenshotContainer->setStyleSheet("border: 1px solid #d0d0d0;");
    m_screenshotContainer->installEventFilter(this);
    QGridLayout *displayLayout = new QGridLayout(m_screenshotContainer);
    displayLayout->setContentsMargins(4, 0, 4, 0);
    displayLayout->setSpacing(0);

    // 双层叠加的工作区：底层旧图、上层新图，用于向左滚动切换
    m_screenshotStack = new QWidget(m_screenshotContainer);
    m_screenshotStack->setObjectName("modDetailScreenshotStack");
    m_screenshotStack->setStyleSheet("background: transparent;");
    m_screenshotStack->installEventFilter(this);

    m_screenshotLabel = new QLabel(m_screenshotStack);
    m_screenshotLabel->setObjectName("modDetailScreenshot");
    m_screenshotLabel->setAlignment(Qt::AlignCenter);
    m_screenshotLabel->setCursor(Qt::PointingHandCursor);
    m_screenshotLabel->setStyleSheet("background: transparent;");
    m_screenshotLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_screenshotLabel->installEventFilter(this);

    m_screenshotOverlay = new QLabel(m_screenshotStack);
    m_screenshotOverlay->setObjectName("modDetailScreenshotOverlay");
    m_screenshotOverlay->setAlignment(Qt::AlignCenter);
    m_screenshotOverlay->setStyleSheet("background: transparent;");
    m_screenshotOverlay->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_screenshotOverlay->hide();

    displayLayout->addWidget(m_screenshotStack, 0, 0, 1, 3);

    m_prevScreenshotBtn = new QPushButton();
    m_prevScreenshotBtn->setObjectName("modDetailScreenshotPrev");
    m_prevScreenshotBtn->setFixedSize(32, 32);
    m_prevScreenshotBtn->setCursor(Qt::PointingHandCursor);
    m_prevScreenshotBtn->setFlat(true);
    displayLayout->addWidget(m_prevScreenshotBtn, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    m_nextScreenshotBtn = new QPushButton();
    m_nextScreenshotBtn->setObjectName("modDetailScreenshotNext");
    m_nextScreenshotBtn->setFixedSize(32, 32);
    m_nextScreenshotBtn->setCursor(Qt::PointingHandCursor);
    m_nextScreenshotBtn->setFlat(true);
    displayLayout->addWidget(m_nextScreenshotBtn, 0, 2, Qt::AlignRight | Qt::AlignVCenter);

    sectionLayout->addWidget(m_screenshotContainer);

    // Set arrow icons using current theme color
    updateScreenshotNavStyle();

    // Connect navigation
    connect(m_prevScreenshotBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentScreenshotIndex > 0) {
            showScreenshot(m_currentScreenshotIndex - 1);
            onScreenshotInteraction();
        }
    });
    connect(m_nextScreenshotBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentScreenshotIndex < m_screenshotPixmaps.size() - 1) {
            showScreenshot(m_currentScreenshotIndex + 1);
            onScreenshotInteraction();
        }
    });

    m_thumbnailStrip = new QWidget();
    m_thumbnailStrip->setObjectName("modDetailThumbStrip");
    m_thumbnailStrip->setFixedHeight(90);
    QHBoxLayout *stripLayout = new QHBoxLayout(m_thumbnailStrip);
    stripLayout->setContentsMargins(0, 0, 0, 0);
    stripLayout->setSpacing(4);

    QStringList urls;
    for (const ModScreenshot &s : m_modInfo.screenshots) {
        urls << s.url;
    }

    for (int i = 0; i < urls.size(); ++i) {
        QLabel *thumb = new QLabel();
        thumb->setObjectName("modDetailScreenshotThumb");
        thumb->setFixedSize(120, 80);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setCursor(Qt::PointingHandCursor);
        thumb->setStyleSheet("border: 1px solid #d0d0d0;");
        thumb->installEventFilter(this);
        thumb->setProperty("screenshotIndex", i);
        QPixmap placeholder(120, 80);
        placeholder.fill(Qt::white);
        thumb->setPixmap(placeholder);
        stripLayout->addWidget(thumb);
    }
    stripLayout->addStretch();

    // Scroll area wrapping thumbnails (no auto-resize, layout provides height)
    m_thumbScrollArea = new QScrollArea();
    m_thumbScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbScrollArea->setFrameShape(QFrame::NoFrame);
    m_thumbScrollArea->setWidget(m_thumbnailStrip);
    // Pin strip width so viewport can scroll it horizontally
    m_thumbnailStrip->setFixedWidth(urls.size() * 124 - 4);

    // Scroll buttons
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    m_thumbScrollLeftBtn = new QPushButton();
    m_thumbScrollLeftBtn->setFixedSize(24, 80);
    m_thumbScrollLeftBtn->setCursor(Qt::PointingHandCursor);
    m_thumbScrollLeftBtn->setFlat(true);
    m_thumbScrollLeftBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/chevron_left.svg", themeColor, 18));
    m_thumbScrollLeftBtn->setIconSize(QSize(18, 18));

    m_thumbScrollRightBtn = new QPushButton();
    m_thumbScrollRightBtn->setFixedSize(24, 80);
    m_thumbScrollRightBtn->setCursor(Qt::PointingHandCursor);
    m_thumbScrollRightBtn->setFlat(true);
    m_thumbScrollRightBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/chevron_right.svg", themeColor, 18));
    m_thumbScrollRightBtn->setIconSize(QSize(18, 18));

    // Container: [leftBtn | scrollArea | rightBtn]
    QWidget *thumbContainer = new QWidget();
    thumbContainer->setFixedHeight(90);
    QHBoxLayout *containerLayout = new QHBoxLayout(thumbContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(2);
    containerLayout->addWidget(m_thumbScrollLeftBtn);
    containerLayout->addWidget(m_thumbScrollArea, 1);
    containerLayout->addWidget(m_thumbScrollRightBtn);

    sectionLayout->addWidget(thumbContainer);
    parentLayout->addWidget(m_screenshotSection);

    // Scroll by one thumbnail width + spacing
    connect(m_thumbScrollLeftBtn, &QPushButton::clicked, this, [this]() {
        if (!m_thumbScrollArea) return;
        QScrollBar *hbar = m_thumbScrollArea->horizontalScrollBar();
        hbar->setValue(qMax(0, hbar->value() - 124));
    });
    connect(m_thumbScrollRightBtn, &QPushButton::clicked, this, [this]() {
        if (!m_thumbScrollArea) return;
        QScrollBar *hbar = m_thumbScrollArea->horizontalScrollBar();
        hbar->setValue(qMin(hbar->maximum(), hbar->value() + 124));
    });

    loadScreenshots(urls);
}

void ContentDetailPage::loadScreenshots(const QStringList &urls)
{
    m_screenshotUrls = urls;
    m_screenshotPixmaps.resize(urls.size());

    if (urls.isEmpty())
        return;

    for (int i = 0; i < urls.size(); ++i) {
        QString urlStr = urls[i];
        if (urlStr.isEmpty())
            continue;

        QString cacheKey = QString("detail_screenshot:%1").arg(urlStr);
        QPixmap cached;
        if (QPixmapCache::find(cacheKey, &cached)) {
            m_screenshotPixmaps[i] = cached;
            if (m_currentScreenshotIndex < 0)
                m_currentScreenshotIndex = i;
            updateThumbnail(i);
            continue;
        }

        QUrl url(McimHelper::rewriteImageUrl(urlStr));
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        QNetworkReply *reply = sharedDetailNAM()->get(request);

        int idx = i;
        connect(reply, &QNetworkReply::finished, this, [this, reply, idx, cacheKey]() {
            reply->deleteLater();

            if (reply->error() == QNetworkReply::NoError) {
                QByteArray data = reply->readAll();
                QPixmap pixmap;
                pixmap.loadFromData(data);
                if (!pixmap.isNull()) {
                    QPixmapCache::insert(cacheKey, pixmap);
                    if (idx < m_screenshotPixmaps.size())
                        m_screenshotPixmaps[idx] = pixmap;

                    if (m_currentScreenshotIndex < 0 && idx == 0)
                        m_currentScreenshotIndex = 0;

                    updateThumbnail(idx);
                }
            }
        });
    }

    // Show first screenshot if loaded
    if (m_currentScreenshotIndex < 0 && !m_screenshotPixmaps.isEmpty()) {
        for (int i = 0; i < m_screenshotPixmaps.size(); ++i) {
            if (!m_screenshotPixmaps[i].isNull()) {
                showScreenshot(i);
                break;
            }
        }
    }
    if (m_currentScreenshotIndex >= 0)
        showScreenshot(m_currentScreenshotIndex);

    // Start carousel if multiple screenshots
    if (m_screenshotPixmaps.size() > 1)
        startCarousel();
}

void ContentDetailPage::showScreenshot(int index)
{
    if (index < 0 || index >= m_screenshotPixmaps.size())
        return;
    if (m_screenshotPixmaps[index].isNull())
        return;

    m_currentScreenshotIndex = index;
    QPixmap px = m_screenshotPixmaps[index];

    if (!m_screenshotStack || m_screenshotStack->width() <= 0) {
        m_screenshotLabel->setPixmap(scaledForDisplay(px));
        m_screenshotLabel->setGeometry(m_screenshotStack ? m_screenshotStack->rect()
                                                         : QRect(0, 0, 600, 338));
        updateScreenshotNavStyle();
        updateThumbnailHighlight();
        return;
    }

    const bool isFirst = m_screenshotLabel->pixmap().isNull();
    if (isFirst) {
        m_screenshotLabel->setGeometry(m_screenshotStack->rect());
        m_screenshotLabel->setPixmap(scaledForDisplay(px));
        updateScreenshotNavStyle();
        updateThumbnailHighlight();
        return;
    }

    if (m_screenshotAnimGroup) {
        m_screenshotAnimGroup->stop();
        m_screenshotAnimGroup->deleteLater();
        m_screenshotAnimGroup = nullptr;
    }
    m_screenshotAnimating = true;

    QRect base = m_screenshotStack->rect();
    int step = base.width();

    m_screenshotLabel->setGeometry(base);
    m_screenshotOverlay->setPixmap(scaledForDisplay(px));
    m_screenshotOverlay->setGeometry(base.translated(step, 0));
    m_screenshotOverlay->setScaledContents(false);
    m_screenshotOverlay->show();
    m_screenshotOverlay->raise();

    auto *group = new QParallelAnimationGroup(this);
    auto *oldAnim = new QPropertyAnimation(m_screenshotLabel, "geometry");
    oldAnim->setDuration(600);
    oldAnim->setStartValue(base);
    oldAnim->setEndValue(base.translated(-step, 0));
    oldAnim->setEasingCurve(QEasingCurve::InOutCubic);
    auto *newAnim = new QPropertyAnimation(m_screenshotOverlay, "geometry");
    newAnim->setDuration(600);
    newAnim->setStartValue(base.translated(step, 0));
    newAnim->setEndValue(base);
    newAnim->setEasingCurve(QEasingCurve::InOutCubic);
    group->addAnimation(oldAnim);
    group->addAnimation(newAnim);

    QObject::connect(group, &QParallelAnimationGroup::finished, this,
      [this, px, base]() {
        m_screenshotOverlay->hide();
        m_screenshotOverlay->setGeometry(base);
        m_screenshotLabel->setPixmap(px);
        m_screenshotLabel->setGeometry(base);
        m_screenshotAnimating = false;
        if (m_screenshotAnimGroup) {
            m_screenshotAnimGroup->deleteLater();
            m_screenshotAnimGroup = nullptr;
        }
      });

    m_screenshotAnimGroup = group;
    group->start();

    updateScreenshotNavStyle();
    updateThumbnailHighlight();
}

QPixmap ContentDetailPage::scaledForDisplay(const QPixmap &px)
{
    int stackW = m_screenshotStack ? m_screenshotStack->width() : 600;
    int stackH = m_screenshotStack ? m_screenshotStack->height() : 338;
    if (px.isNull() || stackW <= 0 || stackH <= 0)
        return px;
    return px.scaled(stackW, stackH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void ContentDetailPage::updateThumbnailHighlight()
{
    if (m_thumbnailStrip) {
        QList<QLabel*> thumbs = m_thumbnailStrip->findChildren<QLabel*>();
        for (int i = 0; i < thumbs.size(); ++i) {
            thumbs[i]->setStyleSheet(i == m_currentScreenshotIndex
                ? "border: 2px solid #409eff;"
                : "border: 1px solid #d0d0d0;");
        }
    }
}

void ContentDetailPage::updateThumbnail(int index)
{
    if (!m_thumbnailStrip || index < 0 || index >= m_screenshotPixmaps.size())
        return;
    QList<QLabel*> thumbs = m_thumbnailStrip->findChildren<QLabel*>();
    if (index < thumbs.size() && !m_screenshotPixmaps[index].isNull())
        thumbs[index]->setPixmap(m_screenshotPixmaps[index].scaled(120, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ContentDetailPage::updateScreenshotNavStyle()
{
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    QString btnStyle = QString(
        "QPushButton { background: rgba(0,0,0,40); border: none; border-radius: 16px; }"
        "QPushButton:hover { background: rgba(0,0,0,70); border-radius: 16px; }"
    );

    QIcon prevIcon = IconHelper::loadColoredIcon(":/Images/Icons/chevron_left.svg", themeColor, 20);
    QIcon nextIcon = IconHelper::loadColoredIcon(":/Images/Icons/chevron_right.svg", themeColor, 20);

    if (m_prevScreenshotBtn) {
        m_prevScreenshotBtn->setIcon(prevIcon);
        m_prevScreenshotBtn->setIconSize(QSize(20, 20));
        m_prevScreenshotBtn->setStyleSheet(btnStyle);
        m_prevScreenshotBtn->setVisible(m_currentScreenshotIndex > 0);
    }
    if (m_nextScreenshotBtn) {
        m_nextScreenshotBtn->setIcon(nextIcon);
        m_nextScreenshotBtn->setIconSize(QSize(20, 20));
        m_nextScreenshotBtn->setStyleSheet(btnStyle);
        m_nextScreenshotBtn->setVisible(m_currentScreenshotIndex < m_screenshotPixmaps.size() - 1);
    }
    if (m_thumbScrollLeftBtn) {
        m_thumbScrollLeftBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/chevron_left.svg", themeColor, 18));
        m_thumbScrollLeftBtn->setIconSize(QSize(18, 18));
    }
    if (m_thumbScrollRightBtn) {
        m_thumbScrollRightBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/chevron_right.svg", themeColor, 18));
        m_thumbScrollRightBtn->setIconSize(QSize(18, 18));
    }
}

void ContentDetailPage::startCarousel()
{
    if (m_screenshotPixmaps.size() > 1)
        m_carouselTimer->start();
}

void ContentDetailPage::stopCarousel()
{
    m_carouselTimer->stop();
}

void ContentDetailPage::onScreenshotInteraction()
{
    stopCarousel();
    m_carouselResumeTimer->start();
}

void ContentDetailPage::receiveDetail(const ModInfo &detail)
{
    // 同步远端返回的 source（CurseForge / Modrinth）以更新收藏状态
    if (!detail.source.isEmpty() && m_currentSource != detail.source)
    {
        m_currentSource = detail.source;
    }
    if (!detail.id.isEmpty() && !m_currentSource.isEmpty())
    {
        m_currentItemId = m_currentSource + ":" + detail.id;
        m_modInfo.id = detail.id;
    }
    updateFavoriteButtonIcon();

    if (!detail.versionFiles.isEmpty() && m_downloadListLayout) {
        while (QLayoutItem *item = m_downloadListLayout->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
        m_downloadCards.clear();
        m_downloadCurrentPage = 0;

        for (const ModVersionFile &vf : detail.versionFiles) {
            QWidget *fileCard = createDownloadCard(vf);
            m_downloadListLayout->addWidget(fileCard);
            DownloadCardEntry entry;
            entry.gameVersions = vf.gameVersions;
            entry.loaders = vf.loaders;
            entry.widget = fileCard;
            m_downloadCards.append(entry);
        }

        applyDownloadFilter();
    }

    // Update screenshots if API provides more
    if (!detail.screenshots.isEmpty()) {
        m_modInfo.screenshots = detail.screenshots;
        if (m_screenshotSection) {
            // Rebuild thumbnail strip
            QLayoutItem *child;
            while ((child = m_thumbnailStrip->layout()->takeAt(0)) != nullptr) {
                if (child->widget())
                    delete child->widget();
                delete child;
            }
            for (int i = 0; i < detail.screenshots.size(); ++i) {
                QLabel *thumb = new QLabel();
                thumb->setObjectName("modDetailScreenshotThumb");
                thumb->setFixedSize(120, 80);
                thumb->setAlignment(Qt::AlignCenter);
                thumb->setCursor(Qt::PointingHandCursor);
                thumb->setStyleSheet("border: 1px solid #d0d0d0;");
                thumb->installEventFilter(this);
                thumb->setProperty("screenshotIndex", i);
                QPixmap placeholder(120, 80);
                placeholder.fill(Qt::white);
                thumb->setPixmap(placeholder);
                m_thumbnailStrip->layout()->addWidget(thumb);
            }
            static_cast<QHBoxLayout*>(m_thumbnailStrip->layout())->addStretch();
            int totalW = detail.screenshots.size() * 124 - 4;
            m_thumbnailStrip->setFixedWidth(totalW);
            if (m_thumbScrollArea) {
                m_thumbScrollArea->horizontalScrollBar()->setValue(0);
            }
            m_currentScreenshotIndex = -1;
            QStringList urls;
            for (const ModScreenshot &s : detail.screenshots)
                urls << s.url;
            loadScreenshots(urls);
        }
    }

    if (!detail.detailedDescription.isEmpty()) {
        m_modInfo.detailedDescription = detail.detailedDescription;
        if (m_descriptionBrowser) {
            m_descriptionBrowser->setHtml(detail.detailedDescription);
        }
    }

    // Hide loading overlay
    if (m_loadingOverlay)
        m_loadingOverlay->hideOverlay();
}

void ContentDetailPage::hideLoading()
{
    if (m_loadingOverlay)
        m_loadingOverlay->hideOverlay();
}

void ContentDetailPage::setMcmodUrl(const QString &url)
{
    if (url.isEmpty())
        return;
    m_modInfo.mcmodUrl = url;
    QList<QPushButton*> btns = m_linksContainer->findChildren<QPushButton*>();
    for (QPushButton *btn : btns) {
        if (btn->text() == tr("MC百科")) {
            btn->setEnabled(true);
            disconnect(btn, nullptr, nullptr, nullptr);
            QString urlCopy = url;
            connect(btn, &QPushButton::clicked, this, [urlCopy]() {
                QDesktopServices::openUrl(QUrl(urlCopy));
            });
            break;
        }
    }
}

void ContentDetailPage::loadIcon()
{
    if (m_modInfo.iconUrl.isEmpty()) return;

    QString cacheKey = QString("detail_icon:%1").arg(m_modInfo.iconUrl);
    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        m_iconLabel->setPixmap(cached);
        return;
    }

    QUrl url(McimHelper::rewriteImageUrl(m_modInfo.iconUrl));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply *reply = sharedDetailNAM()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cacheKey]() {
        if (reply->error() == QNetworkReply::NoError) {
            QPixmap pixmap;
            pixmap.loadFromData(reply->readAll());
            if (!pixmap.isNull()) {
                QPixmap scaled = pixmap.scaled(60, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QPixmapCache::insert(cacheKey, scaled);
                m_iconLabel->setPixmap(scaled);
            }
        }
        reply->deleteLater();
    });
}

QWidget *ContentDetailPage::createChip(const QString &text, const QColor &bgColor)
{
    Q_UNUSED(bgColor);
    QLabel *chip = new QLabel(text);
    chip->setObjectName("modDetailChip");
    chip->setProperty("cardRole", "chip");
    return chip;
}

bool ContentDetailPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize && watched == m_screenshotContainer) {
        int targetH = m_screenshotContainer->width() * 9 / 16;
        targetH = qBound(180, targetH, 360);
        if (targetH > 0)
            m_screenshotContainer->setFixedHeight(targetH);
        return false;
    }

    if (event->type() == QEvent::Resize && watched == m_screenshotStack) {
        if (!m_screenshotAnimating) {
            m_screenshotLabel->setGeometry(m_screenshotStack->rect());
            if (m_screenshotOverlay)
                m_screenshotOverlay->setGeometry(m_screenshotStack->rect());
        }
        return false;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        // Click on main screenshot → open viewer
        if (watched == m_screenshotLabel && !m_screenshotPixmaps.isEmpty()) {
            ScreenshotViewer *viewer = new ScreenshotViewer(m_screenshotPixmaps, m_currentScreenshotIndex, this);
            viewer->setAttribute(Qt::WA_DeleteOnClose);
            viewer->show();
            return true;
        }

        // Click on thumbnail → switch to that screenshot
        QLabel *thumb = qobject_cast<QLabel*>(watched);
        if (thumb && thumb->objectName() == "modDetailScreenshotThumb") {
            bool ok;
            int idx = thumb->property("screenshotIndex").toInt(&ok);
            if (ok && idx >= 0 && idx < m_screenshotPixmaps.size() && idx != m_currentScreenshotIndex) {
                showScreenshot(idx);
                onScreenshotInteraction();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}