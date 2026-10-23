#include "ModDetailPage.h"
#include "utils/IconHelper.h"
#include "utils/McimHelper.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModrinthAPI.h"
#include "components/AutoHeightTextBrowser.h"
#include "components/ScreenshotViewer.h"
#include "components/SourceSwitchBar.h"
#include "components/NotificationManager.h"
#include <QDesktopServices>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
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
#include <QPointer>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QSet>

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

ModDetailPage::ModDetailPage(QWidget *parent)
    : QWidget(parent)
    , m_scrollArea(nullptr)
    , m_contentWidget(nullptr)
    , m_contentLayout(nullptr)
    , m_sourceSwitchBar(nullptr)
    , m_headerWidget(nullptr)
    , m_iconLabel(nullptr)
    , m_chineseNameLabel(nullptr)
    , m_englishNameLabel(nullptr)
    , m_descriptionLabel(nullptr)
    , m_leftPanel(nullptr)
    , m_versionCombo(nullptr)
    , m_loaderCombo(nullptr)
    , m_versionTypeCombo(nullptr)
    , m_dependenciesContainer(nullptr)
    , m_dependenciesLayout(nullptr)
    , m_dependenciesSection(nullptr)
    , m_curseforgeAPI(nullptr)
    , m_modrinthAPI(nullptr)
    , m_downloadContainer(nullptr)
    , m_downloadListLayout(nullptr)
    , m_downloadCurrentPage(0)
    , m_downloadPaginationBar(nullptr)
    , m_downloadPrevBtn(nullptr)
    , m_downloadNextBtn(nullptr)
    , m_downloadPageLabel(nullptr)
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
    , m_loadingOverlay(nullptr)
{
    initAPI();
    initUI();
}

ModDetailPage::~ModDetailPage()
{
}

void ModDetailPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 顶部来源切换（跨源合并卡片显示；置于滚动区外固定不随内容滚动）
    mainLayout->addWidget(buildSourceSwitch());

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

    // 右下角悬浮翻译卡片：EN（默认，看原文）/「中」（AI 翻译当前页的简介与详细描述）
    m_translateSwitch = new TranslateSwitchCard(this);
    connect(m_translateSwitch, &TranslateSwitchCard::modeChangeRequested,
            this, &ModDetailPage::onTranslateModeChanged);

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
}

QWidget *ModDetailPage::buildSourceSwitch()
{
    // 外层容器负责与内容区对齐的边距，避免分段控件轨道背景被拉满整行
    QWidget *row = new QWidget(this);
    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(20, 12, 20, 0);
    rowLayout->setSpacing(0);

    m_sourceSwitchBar = new SourceSwitchBar(row);
    m_sourceSwitchBar->hide();
    connect(m_sourceSwitchBar, &SourceSwitchBar::sourceSelected,
            this, &ModDetailPage::switchToSource);
    rowLayout->addWidget(m_sourceSwitchBar);
    rowLayout->addStretch();

    return row;
}

void ModDetailPage::switchToSource(const QString &source)
{
    if (source == m_modInfo.source)
        return;

    ModInfo view = switchModInfoSource(m_modInfo);
    if (view.source != source)
        return;

    // 版本/截图/前置等详情数据随源失效，由切换后的详情拉取重建
    view.versionFiles.clear();
    view.screenshots.clear();
    view.dependencies.clear();

    setModInfo(view);
    emit sourceSwitchRequested(source);
}

void ModDetailPage::clearContent()
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
    m_dependenciesSection = nullptr;
    m_depResolved.clear();
    m_depResolvingIds.clear();
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

void ModDetailPage::setModInfo(const ModInfo &info)
{
    m_modInfo = info;

    // 换了资源：上一页的译文与英文原文全部作废，回到 EN
    resetTranslation();

    if (m_sourceSwitchBar) {
        m_sourceSwitchBar->setVisible(info.isCrossSourceMerged());
        m_sourceSwitchBar->setActiveSource(info.source);
    }

    if (m_loadingOverlay)
        m_loadingOverlay->showOverlay(tr("正在获取模组详情..."));

    clearContent();
    buildHeader();
    buildLeftSidebar();
    buildRightMain();
    loadIcon();
}

void ModDetailPage::selectVersionFilter(const QString &gameVersion)
{
    if (!m_versionCombo || gameVersion.isEmpty())
        return;

    const bool wasBlocked = m_versionCombo->signalsBlocked();
    m_versionCombo->blockSignals(true);

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
    // 资源本身没列出该版本时也补进下拉，保证筛选真实生效（结果为无匹配版本）
    if (idx < 0) {
        m_versionCombo->addItem(gameVersion);
        idx = m_versionCombo->count() - 1;
    }
    m_versionCombo->setCurrentIndex(idx);

    m_versionCombo->blockSignals(wasBlocked);
}

void ModDetailPage::setInstanceFilter(const QString &gameVersion, const QString &loaderType)
{
    m_instanceGameVersion = gameVersion;
    m_instanceLoaderType = loaderType;

    if (m_loaderCombo) m_loaderCombo->blockSignals(true);

    selectVersionFilter(gameVersion);

    if (m_loaderCombo && !loaderType.isEmpty()) {
        QString lower = loaderType.toLower();
        for (int i = 0; i < m_loaderCombo->count(); ++i) {
            if (m_loaderCombo->itemText(i).toLower().contains(lower)) {
                m_loaderCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    if (m_loaderCombo) m_loaderCombo->blockSignals(false);

    // 立即按带入的实例筛选刷新下载列表
    if (!gameVersion.isEmpty() || !loaderType.isEmpty()) {
        m_downloadCurrentPage = 0;
        applyDownloadFilter();
    }
}

void ModDetailPage::buildHeader()
{
    // 顶部信息区（大图标＋名称＋简介）包进卡片，内边距由布局给出
    m_headerWidget = new QWidget();
    m_headerWidget->setObjectName("modDetailHeader");
    addSectionShadow(m_headerWidget);
    QHBoxLayout *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(20, 20, 20, 20);
    headerLayout->setSpacing(20);

    m_iconLabel = new QLabel();
    m_iconLabel->setObjectName("modDetailIcon");
    m_iconLabel->setFixedSize(64, 64);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    QPixmap placeholder(64, 64);
    placeholder.fill(ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme
                         ? QColor("#2d2d2d") : QColor("#e8e8e8"));
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
    m_contentLayout->addWidget(m_headerWidget);
}

void ModDetailPage::buildLeftSidebar()
{
    m_leftPanel = new QWidget();
    m_leftPanel->setObjectName("modDetailLeftPanel");
    m_leftPanel->setAttribute(Qt::WA_StyledBackground);
    // 悬浮气泡外阴影（与子导航栏一致）
    auto *bubbleShadow = new QGraphicsDropShadowEffect(m_leftPanel);
    bubbleShadow->setBlurRadius(20);
    bubbleShadow->setOffset(0, 4);
    bubbleShadow->setColor(QColor(0, 0, 0, 50));
    m_leftPanel->setGraphicsEffect(bubbleShadow);

    QVBoxLayout *leftLayout = new QVBoxLayout(m_leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(16);

    buildFilterBlock(leftLayout);
    buildDependenciesBlock(leftLayout);
    buildDownloadListBlock(leftLayout);
    leftLayout->addStretch();
}

void ModDetailPage::buildRightMain()
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

    bodyLayout->addWidget(m_leftPanel, 3);
    bodyLayout->addWidget(rightPanel, 7);

    m_contentLayout->addWidget(mainBody, 1);
}

void ModDetailPage::addSectionShadow(QWidget *section)
{
    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(section);
    shadow->setBlurRadius(16);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(0, 0, 0, 20));
    section->setGraphicsEffect(shadow);
}

void ModDetailPage::buildFilterBlock(QVBoxLayout *parentLayout)
{
    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    addSectionShadow(section);
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

void ModDetailPage::initAPI()
{
    // 详情本身由主窗口按模组来源拉取；这里的接口只用来把「前置模组」里的 id
    // 批量换成名称和图标（CurseForge 依赖里只有数字 modId，Modrinth 只有 project_id）
    m_curseforgeAPI = new CurseForgeAPI(this);
    const QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (!apiKey.isEmpty())
        m_curseforgeAPI->setApiKey(apiKey);
    m_curseforgeAPI->setClassId(QStringLiteral("6"));

    m_modrinthAPI = new ModrinthAPI(this);
    m_modrinthAPI->setProjectType(QStringLiteral("mod"));

    applyMCIMSetting();

    connect(m_curseforgeAPI, &CurseForgeAPI::modsResolved,
            this, &ModDetailPage::onDependencyInfosResolved);
    connect(m_modrinthAPI, &ModrinthAPI::projectsResolved,
            this, &ModDetailPage::onDependencyInfosResolved);
    // 前置资源查不到不算错误：卡片退回显示原始 id，不打扰用户
    connect(m_curseforgeAPI, &CurseForgeAPI::modsResolveFailed,
            this, [this](const QString &) { m_depResolvingIds.clear(); });
    connect(m_modrinthAPI, &ModrinthAPI::projectsResolveFailed,
            this, [this](const QString &) { m_depResolvingIds.clear(); });
}

void ModDetailPage::applyMCIMSetting()
{
    if (!m_curseforgeAPI || !m_modrinthAPI)
        return;

    QVariant useMcimValue = SettingsManager::instance()->property("use_mcim");
    const bool useMcim = useMcimValue.isValid() ? useMcimValue.toBool() : true;

    m_curseforgeAPI->setBaseUrl(useMcim
        ? QStringLiteral("https://mod.mcimirror.top/curseforge")
        : QStringLiteral("https://api.curseforge.com"));
    m_modrinthAPI->setBaseUrl(useMcim
        ? QStringLiteral("https://mod.mcimirror.top/modrinth")
        : QStringLiteral("https://api.modrinth.com"));
}

void ModDetailPage::buildDependenciesBlock(QVBoxLayout *parentLayout)
{
    // 前置模组始终建块：从列表卡片进详情页时还没取到依赖，
    // 等详情返回后由 refreshDependencies() 填内容并决定是否显示
    m_dependenciesSection = new QWidget();
    m_dependenciesSection->setObjectName("modDetailSection");
    addSectionShadow(m_dependenciesSection);
    QVBoxLayout *sectionLayout = new QVBoxLayout(m_dependenciesSection);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(8);

    QLabel *title = new QLabel(tr("前置模组"));
    title->setObjectName("modDetailSectionTitle");
    sectionLayout->addWidget(title);

    m_dependenciesContainer = new QWidget();
    m_dependenciesLayout = new QVBoxLayout(m_dependenciesContainer);
    m_dependenciesLayout->setContentsMargins(0, 0, 0, 0);
    m_dependenciesLayout->setSpacing(6);
    sectionLayout->addWidget(m_dependenciesContainer);

    parentLayout->addWidget(m_dependenciesSection);

    refreshDependencies();
}

void ModDetailPage::refreshDependencies()
{
    if (!m_dependenciesSection || !m_dependenciesLayout)
        return;

    while (QLayoutItem *item = m_dependenciesLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->setVisible(false);
            w->deleteLater();
        }
        delete item;
    }

    if (m_modInfo.dependencies.isEmpty()) {
        m_dependenciesSection->setVisible(false);
        return;
    }

    for (const ModDependency &dep : m_modInfo.dependencies) {
        const ModInfo depInfo = m_depResolved.value(dep.name);
        // 批量查询还没回来时先用原始 id 占位（CurseForge 是数字 modId）
        const bool clickable = !depInfo.id.isEmpty() && !depInfo.source.isEmpty();
        const QString displayName = (clickable && !depInfo.name.isEmpty()) ? depInfo.name : dep.name;

        QWidget *depCard = new QWidget();
        depCard->setObjectName("modDetailDepCard");
        depCard->setProperty("depRequired", dep.isRequired);
        depCard->setAttribute(Qt::WA_StyledBackground);
        if (clickable) {
            depCard->setCursor(Qt::PointingHandCursor);
            depCard->setToolTip(tr("点击查看「%1」详情").arg(displayName));
        }
        depCard->setProperty("depName", dep.name);
        depCard->installEventFilter(this);

        QHBoxLayout *depLayout = new QHBoxLayout(depCard);
        depLayout->setContentsMargins(8, 6, 8, 6);
        depLayout->setSpacing(8);

        QLabel *iconLabel = new QLabel();
        iconLabel->setObjectName("modDetailDepIcon");
        iconLabel->setFixedSize(28, 28);
        iconLabel->setAlignment(Qt::AlignCenter);
        depLayout->addWidget(iconLabel);
        loadDepIcon(iconLabel, depInfo.iconUrl);

        QVBoxLayout *textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(1);

        QLabel *nameLabel = new QLabel(displayName);
        nameLabel->setObjectName("modDetailDepName");
        nameLabel->setWordWrap(true);
        nameLabel->setToolTip(displayName);
        textLayout->addWidget(nameLabel);

        if (!depInfo.source.isEmpty()) {
            QLabel *sourceLabel = new QLabel(depInfo.source == QStringLiteral("curseforge")
                                                 ? QStringLiteral("CurseForge")
                                                 : QStringLiteral("Modrinth"));
            sourceLabel->setObjectName("modDetailDepSource");
            textLayout->addWidget(sourceLabel);
        }

        depLayout->addLayout(textLayout, 1);

        QLabel *tagLabel = new QLabel(dep.isRequired ? tr("必备") : tr("可选"));
        tagLabel->setObjectName("modDetailDepTag");
        depLayout->addWidget(tagLabel, 0, Qt::AlignTop);

        m_dependenciesLayout->addWidget(depCard);
    }

    m_dependenciesSection->setVisible(true);
}

void ModDetailPage::loadDepIcon(QLabel *iconLabel, const QString &iconUrl)
{
    if (!iconLabel)
        return;

    const QString cacheKey = QStringLiteral("dep_icon:") + iconUrl;
    QPixmap cached;
    if (!iconUrl.isEmpty() && QPixmapCache::find(cacheKey, &cached)) {
        iconLabel->setPixmap(cached);
        return;
    }

    // 统一走 McimHelper::loadImage：镜像把图片请求返回成网页时会自动换原始地址重试，
    // 实在拿不到（或本来就没有图标）才画一个圆角占位块
    McimHelper::loadImage(sharedDetailNAM(), iconLabel, iconUrl, QSize(28, 28),
                          iconUrl.isEmpty() ? QString() : cacheKey,
                          [](QLabel *label) {
        QPixmap placeholder(28, 28);
        placeholder.fill(Qt::transparent);
        QPainter painter(&placeholder);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 22));
        painter.drawRoundedRect(placeholder.rect(), 8, 8);
        label->setPixmap(placeholder);
    });
}

void ModDetailPage::resolveDependencyInfos()
{
    if (!m_curseforgeAPI || !m_modrinthAPI || m_modInfo.dependencies.isEmpty())
        return;

    QStringList ids;
    for (const ModDependency &dep : m_modInfo.dependencies) {
        if (dep.name.isEmpty())
            continue;
        if (m_depResolved.contains(dep.name) || m_depResolvingIds.contains(dep.name))
            continue;
        ids.append(dep.name);
    }
    if (ids.isEmpty())
        return;

    m_depResolvingIds = ids;
    if (m_modInfo.source == QStringLiteral("curseforge"))
        m_curseforgeAPI->fetchModsByIds(ids);
    else if (m_modInfo.source == QStringLiteral("modrinth"))
        m_modrinthAPI->fetchProjectsByIds(ids);
    else
        m_depResolvingIds.clear();
}

void ModDetailPage::onDependencyInfosResolved(const QList<ModInfo> &infos)
{
    for (const ModInfo &info : infos) {
        if (!info.id.isEmpty())
            m_depResolved.insert(info.id, info);
    }
    m_depResolvingIds.clear();
    refreshDependencies();
}

void ModDetailPage::buildDownloadListBlock(QVBoxLayout *parentLayout)
{
    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    addSectionShadow(section);
    QVBoxLayout *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(8);

    QLabel *title = new QLabel(tr("模组下载"));
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
    connect(m_downloadPrevBtn, &QPushButton::clicked, this, &ModDetailPage::onDownloadPrevPage);

    m_downloadPageLabel = new QLabel();
    m_downloadPageLabel->setObjectName("modDetailPageLabel");
    m_downloadPageLabel->setAlignment(Qt::AlignCenter);

    m_downloadNextBtn = new QPushButton();
    m_downloadNextBtn->setObjectName("modDetailPageBtn");
    m_downloadNextBtn->setFixedSize(28, 28);
    m_downloadNextBtn->setCursor(Qt::PointingHandCursor);
    m_downloadNextBtn->setEnabled(false);
    connect(m_downloadNextBtn, &QPushButton::clicked, this, &ModDetailPage::onDownloadNextPage);

    paginationLayout->addStretch();
    paginationLayout->addWidget(m_downloadPrevBtn);
    paginationLayout->addWidget(m_downloadPageLabel);
    paginationLayout->addWidget(m_downloadNextBtn);
    paginationLayout->addStretch();

    m_downloadPaginationBar->hide();
    sectionLayout->addWidget(m_downloadPaginationBar);

    parentLayout->addWidget(section);
}

QWidget *ModDetailPage::createDownloadCard(const ModVersionFile &vf)
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
    dlBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/download.svg", QColor("#ffffff"), 18));
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

void ModDetailPage::applyDownloadFilter()
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

void ModDetailPage::updateDownloadPagination()
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

void ModDetailPage::onDownloadPrevPage()
{
    if (m_downloadCurrentPage > 0) {
        m_downloadCurrentPage--;
        applyDownloadFilter();
    }
}

void ModDetailPage::onDownloadNextPage()
{
    m_downloadCurrentPage++;
    applyDownloadFilter();
}

void ModDetailPage::buildLinksBlock(QVBoxLayout *parentLayout)
{
    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    addSectionShadow(section);
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
        {tr("问问AI"), QString(), true}
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
        if (link.label == tr("问问AI")) {
            // 问问AI：跳转 AI 助手并引用当前网络模组
            ModInfo info = m_modInfo;
            connect(btn, &QPushButton::clicked, this, [this, info]() {
                ResourceReference ref;
                ref.category = tr("网络模组");
                ref.name = info.name;
                ref.path = info.pageUrl;
                if (ref.path.isEmpty() && !info.id.isEmpty()) {
                    if (info.source == "curseforge")
                        ref.path = QStringLiteral("https://www.curseforge.com/minecraft/mc-mods/%1").arg(info.id);
                    else
                        ref.path = QStringLiteral("https://modrinth.com/project/%1").arg(info.id);
                }
                if (!info.source.isEmpty())
                    ref.extra = tr("来源: %1").arg(info.source);
                emit askAiRequested(ref);
            });
        } else if (!link.url.isEmpty()) {
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

void ModDetailPage::buildTagsBlock(QVBoxLayout *parentLayout)
{
    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    addSectionShadow(section);
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

void ModDetailPage::buildDescriptionBlock(QVBoxLayout *parentLayout)
{
    QString desc = m_modInfo.detailedDescription.isEmpty()
        ? m_modInfo.description
        : m_modInfo.detailedDescription;

    if (desc.isEmpty())
        return;

    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    addSectionShadow(section);
    QVBoxLayout *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(10);

    QLabel *title = new QLabel(tr("详细信息"));
    title->setObjectName("modDetailSectionTitle");
    sectionLayout->addWidget(title);

    // 描述区不再限制高度：文字按完整内容向下排版，由页面整体滚动
    m_descriptionBrowser = new AutoHeightTextBrowser();
    m_descriptionBrowser->setObjectName("modDetailDescBrowser");
    m_descriptionBrowser->setOpenExternalLinks(true);
    m_descriptionBrowser->setReadOnly(true);
    m_descriptionBrowser->setHtml(desc);
    sectionLayout->addWidget(m_descriptionBrowser);

    parentLayout->addWidget(section);
}

void ModDetailPage::buildScreenshotBlock(QVBoxLayout *parentLayout)
{
    m_screenshotSection = new QWidget();
    m_screenshotSection->setObjectName("modDetailSection");
    m_screenshotSection->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    addSectionShadow(m_screenshotSection);
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

void ModDetailPage::loadScreenshots(const QStringList &urls)
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

void ModDetailPage::showScreenshot(int index)
{
    if (index < 0 || index >= m_screenshotPixmaps.size())
        return;
    if (m_screenshotPixmaps[index].isNull())
        return;

    m_currentScreenshotIndex = index;
    QPixmap px = m_screenshotPixmaps[index];

    if (!m_screenshotStack || m_screenshotStack->width() <= 0) {
        // 尺寸未就绪时直接显示，不做动画
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

    // 终止上一次进行中的动画
    if (m_screenshotAnimGroup) {
        m_screenshotAnimGroup->stop();
        m_screenshotAnimGroup->deleteLater();
        m_screenshotAnimGroup = nullptr;
    }
    m_screenshotAnimating = true;

    QRect base = m_screenshotStack->rect();
    int step = base.width();

    // 底层保留旧图，上层放新图，整体向左滑动
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
        // 动画结束后把 overlay 内容提交到底层并复位
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

QPixmap ModDetailPage::scaledForDisplay(const QPixmap &px)
{
    int stackW = m_screenshotStack ? m_screenshotStack->width() : 600;
    int stackH = m_screenshotStack ? m_screenshotStack->height() : 338;
    if (px.isNull() || stackW <= 0 || stackH <= 0)
        return px;
    return px.scaled(stackW, stackH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void ModDetailPage::updateThumbnailHighlight()
{
    // Highlight active thumbnail
    if (m_thumbnailStrip) {
        QList<QLabel*> thumbs = m_thumbnailStrip->findChildren<QLabel*>();
        for (int i = 0; i < thumbs.size(); ++i) {
            bool current = (i == m_currentScreenshotIndex);
            thumbs[i]->setProperty("current", current);
            thumbs[i]->style()->polish(thumbs[i]);
        }
    }
}

void ModDetailPage::updateThumbnail(int index)
{
    if (!m_thumbnailStrip || index < 0 || index >= m_screenshotPixmaps.size())
        return;
    QList<QLabel*> thumbs = m_thumbnailStrip->findChildren<QLabel*>();
    if (index < thumbs.size() && !m_screenshotPixmaps[index].isNull())
        thumbs[index]->setPixmap(m_screenshotPixmaps[index].scaled(120, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ModDetailPage::updateScreenshotNavStyle()
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

void ModDetailPage::startCarousel()
{
    if (m_screenshotPixmaps.size() > 1)
        m_carouselTimer->start();
}

void ModDetailPage::stopCarousel()
{
    m_carouselTimer->stop();
}

void ModDetailPage::onScreenshotInteraction()
{
    stopCarousel();
    m_carouselResumeTimer->start();
}

void ModDetailPage::receiveDetail(const ModInfo &detail)
{
    // 详情里的名称/简介/详细描述会整体替换页面文字，译文随之作废，回到 EN
    resetTranslation();

    // 来源跟随详情返回值（来源切换后保证下载/收藏走对源）
    if (!detail.source.isEmpty())
        m_modInfo.source = detail.source;

    // 1. 合并远程详情到 m_modInfo，更新头部和标签信息
    if (!detail.name.isEmpty())
        m_modInfo.name = detail.name;
    if (!detail.englishName.isEmpty())
        m_modInfo.englishName = detail.englishName;
    if (!detail.chineseName.isEmpty())
        m_modInfo.chineseName = detail.chineseName;
    if (!detail.author.isEmpty())
        m_modInfo.author = detail.author;
    if (!detail.description.isEmpty())
        m_modInfo.description = detail.description;
    if (!detail.iconUrl.isEmpty())
        m_modInfo.iconUrl = detail.iconUrl;
    if (detail.downloadCount > 0)
        m_modInfo.downloadCount = detail.downloadCount;
    if (detail.followers > 0)
        m_modInfo.followers = detail.followers;
    if (detail.rank > 0)
        m_modInfo.rank = detail.rank;
    if (detail.dateModified.isValid())
        m_modInfo.dateModified = detail.dateModified;
    if (detail.datePublished.isValid())
        m_modInfo.datePublished = detail.datePublished;
    if (!detail.gameVersions.isEmpty())
        m_modInfo.gameVersions = detail.gameVersions;
    if (!detail.loaders.isEmpty())
        m_modInfo.loaders = detail.loaders;
    if (!detail.categories.isEmpty())
        m_modInfo.categories = detail.categories;
    if (!detail.dependencies.isEmpty())
        m_modInfo.dependencies = detail.dependencies;
    if (!detail.pageUrl.isEmpty())
        m_modInfo.pageUrl = detail.pageUrl;

    // 前置模组只在详情里才有：先建卡片（此时可能只有 id），再批量换成名称与图标
    if (!detail.dependencies.isEmpty()) {
        resolveDependencyInfos();
        refreshDependencies();
    }

    // 2. 更新头部文字
    if (m_chineseNameLabel) {
        QString displayName = m_modInfo.chineseName.isEmpty()
            ? m_modInfo.name
            : m_modInfo.chineseName;
        m_chineseNameLabel->setText(displayName);
    }
    if (m_englishNameLabel) {
        QString eng = m_modInfo.englishName.isEmpty()
            ? m_modInfo.name : m_modInfo.englishName;
        if (!m_modInfo.chineseName.isEmpty() && eng == m_modInfo.name)
            eng = m_modInfo.name;
        m_englishNameLabel->setText(eng);
    }
    if (m_descriptionLabel)
        m_descriptionLabel->setText(m_modInfo.description);

    // 3. 加载远程图标
    if (!detail.iconUrl.isEmpty())
        loadIcon();

    // 4. 重建标签信息区
    if (m_tagsContainer) {
        QLayout *tl = m_tagsContainer->layout();
        if (tl) {
            QLayoutItem *child;
            while ((child = tl->takeAt(0)) != nullptr) {
                if (child->widget()) delete child->widget();
                delete child;
            }
        }
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
            tl->addWidget(row);
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
        if (!m_modInfo.categories.isEmpty())
            addTagRow(tr("分类"), m_modInfo.categories.join(", "));
    }

    // 5. 更新版本筛选 Combo
    if (m_versionCombo) {
        // 记录当前筛选（用户手选优先，否则用列表页带入的实例版本），重建后恢复，
        // 否则详情返回会把版本筛选悄悄重置成“全部版本”
        const QString keepVersion = m_versionCombo->currentIndex() > 0
                                        ? m_versionCombo->currentText()
                                        : m_instanceGameVersion;
        m_versionCombo->blockSignals(true);
        while (m_versionCombo->count() > 1)
            m_versionCombo->removeItem(1);
        for (const QString &v : m_modInfo.gameVersions)
            m_versionCombo->addItem(v);
        m_versionCombo->blockSignals(false);
        selectVersionFilter(keepVersion);
    }

    // 6. 更新下载列表
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
            entry.releaseType = vf.releaseType;
            entry.widget = fileCard;
            m_downloadCards.append(entry);
        }

        applyDownloadFilter();
    }

    // 7. 更新截图
    if (!detail.screenshots.isEmpty()) {
        m_modInfo.screenshots = detail.screenshots;
        if (m_screenshotSection) {
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

    // 8. 更新/创建详细信息卡片（如果 setModInfo 时 desc 为空跳过了创建）
    bool hasDesc = !detail.detailedDescription.isEmpty() || !detail.description.isEmpty();
    if (hasDesc && !m_descriptionBrowser) {
        QWidget *rightPanel = m_contentWidget->findChild<QWidget*>("modDetailRightPanel");
        if (rightPanel) {
            QVBoxLayout *rightLayout = qobject_cast<QVBoxLayout*>(rightPanel->layout());
            if (rightLayout)
                buildDescriptionBlock(rightLayout);
        }
    }
    if (!detail.detailedDescription.isEmpty()) {
        m_modInfo.detailedDescription = detail.detailedDescription;
        if (m_descriptionBrowser)
            m_descriptionBrowser->setHtml(detail.detailedDescription);
    }

    // 隐藏加载层
    if (m_loadingOverlay)
        m_loadingOverlay->hideOverlay();
}

void ModDetailPage::hideLoading()
{
    if (m_loadingOverlay)
        m_loadingOverlay->hideOverlay();
}

void ModDetailPage::setMcmodUrl(const QString &url)
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

void ModDetailPage::loadIcon()
{
    if (m_modInfo.iconUrl.isEmpty()) return;

    QString cacheKey = QString("detail_icon:%1").arg(m_modInfo.iconUrl);
    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        m_iconLabel->setPixmap(cached);
        return;
    }

    // 镜像图片不可用时会自动改用原始地址，不会因为镜像返回网页而一直空着
    McimHelper::loadImage(sharedDetailNAM(), m_iconLabel, m_modInfo.iconUrl,
                          QSize(60, 60), cacheKey);
}

QWidget *ModDetailPage::createChip(const QString &text, const QColor &bgColor)
{
    Q_UNUSED(bgColor);
    QLabel *chip = new QLabel(text);
    chip->setObjectName("modDetailChip");
    chip->setProperty("cardRole", "chip");
    return chip;
}

bool ModDetailPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize && watched == m_screenshotContainer) {
        int targetH = m_screenshotContainer->width() * 9 / 16;
        targetH = qBound(180, targetH, 360);
        if (targetH > 0)
            m_screenshotContainer->setFixedHeight(targetH);
        return false;
    }

    if (event->type() == QEvent::Resize && watched == m_screenshotStack) {
        // 工作区尺寸变化时，让底层图片跟随，避免动画中错位
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

    // 点击左侧「前置模组」卡片 → 跳到该前置资源自己的详情页
    if (event->type() == QEvent::MouseButtonRelease) {
        QWidget *depCard = qobject_cast<QWidget*>(watched);
        if (depCard && depCard->objectName() == "modDetailDepCard") {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                const ModInfo depInfo = m_depResolved.value(depCard->property("depName").toString());
                if (!depInfo.id.isEmpty() && !depInfo.source.isEmpty()) {
                    // 延后一拍再跳：跳转会让本页重建，不能在卡片自己的事件里同步删掉它
                    QTimer::singleShot(0, this, [this, depInfo]() { emit contentClicked(depInfo); });
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ===========================================================================
// 右下角悬浮翻译卡片（EN 原文 / 中 AI 翻译当前页）
// ===========================================================================

namespace {

/// 文本里是否已含中文（含中文就不再送去翻译，避免重复请求）
bool containsChineseText(const QString &text)
{
    for (const QChar &ch : text) {
        if (ch.script() == QChar::Script_Han)
            return true;
    }
    return false;
}

} // namespace

void ModDetailPage::onTranslateModeChanged(bool chinese)
{
    if (!m_translateSwitch)
        return;

    if (chinese) {
        m_translateSwitch->setChineseMode(true);
        if (m_translating)
            return;
        if (m_translated) {
            applyTranslatedTexts();
            return;
        }
        startPageTranslation();
        return;
    }

    // 切回 EN：取消进行中的翻译并还原原文
    if (m_translating) {
        if (m_translator)
            m_translator->cancelCurrentRequest();
        m_translating = false;
        setTranslateBusy(false);
    }
    m_translateSwitch->setChineseMode(false);
    restoreOriginalTexts();
}

void ModDetailPage::startPageTranslation()
{
    if (!m_translator) {
        m_translator = new TranslationService(this);
        connect(m_translator, &TranslationService::translationFinished, this,
                [this](const QString &text) { onTranslateChunkFinished(text); });
        connect(m_translator, &TranslationService::translationError, this,
                [this](const QString &error) {
            m_translating = false;
            setTranslateBusy(false);
            if (m_translateSwitch)
                m_translateSwitch->setChineseMode(false);
            restoreOriginalTexts();
            NotificationManager::showError(this, tr("翻译失败：%1").arg(error));
        });
    }

    // 记录原文，供切回 EN 时还原
    m_origSummary = m_descriptionLabel ? m_descriptionLabel->text() : QString();
    m_origDetailHtml = m_modInfo.detailedDescription.isEmpty()
        ? m_modInfo.description
        : m_modInfo.detailedDescription;

    m_translateTexts.clear();
    m_translateKinds.clear();

    const bool summaryNeedsTranslation = !m_origSummary.isEmpty()
        && !containsChineseText(m_origSummary);
    // 详细描述区没建出来（资源没给详细介绍）时不翻，避免译文无处可写
    const bool detailNeedsTranslation = m_descriptionBrowser && !m_origDetailHtml.isEmpty()
        && !containsChineseText(m_origDetailHtml);

    if (summaryNeedsTranslation && detailNeedsTranslation
        && m_origSummary == m_origDetailHtml) {
        m_translateTexts << m_origSummary;
        m_translateKinds << TranslateSummaryAndDetail;
    } else {
        if (summaryNeedsTranslation) {
            m_translateTexts << m_origSummary;
            m_translateKinds << TranslateSummary;
        }
        if (detailNeedsTranslation) {
            m_translateTexts << m_origDetailHtml;
            m_translateKinds << TranslateDetail;
        }
    }

    if (m_translateTexts.isEmpty()) {
        NotificationManager::showInfo(this, tr("当前页面没有需要翻译的英文内容"));
        if (m_translateSwitch)
            m_translateSwitch->setChineseMode(false);
        return;
    }

    m_zhSummary.clear();
    m_zhDetailHtml.clear();
    m_translateChunkIndex = 0;
    m_translating = true;
    setTranslateBusy(true);
    translateNextChunk();
}

void ModDetailPage::translateNextChunk()
{
    if (!m_translator || m_translateChunkIndex >= m_translateTexts.size())
        return;

    const TranslationConfig config = SettingsManager::instance()->getTranslationConfig();
    m_translator->translate(m_translateTexts.at(m_translateChunkIndex), config);
}

void ModDetailPage::onTranslateChunkFinished(const QString &text)
{
    if (!m_translating || m_translateChunkIndex >= m_translateTexts.size())
        return;

    if (text.trimmed().isEmpty()) {
        m_translating = false;
        setTranslateBusy(false);
        if (m_translateSwitch)
            m_translateSwitch->setChineseMode(false);
        restoreOriginalTexts();
        NotificationManager::showError(this, tr("翻译失败：AI 返回了空结果"));
        return;
    }

    switch (m_translateKinds.at(m_translateChunkIndex)) {
    case TranslateSummary:
        m_zhSummary = text;
        break;
    case TranslateDetail:
        m_zhDetailHtml = text;
        break;
    case TranslateSummaryAndDetail:
        m_zhSummary = text;
        m_zhDetailHtml = text;
        break;
    }

    ++m_translateChunkIndex;
    if (m_translateChunkIndex < m_translateTexts.size()) {
        translateNextChunk();
        return;
    }

    m_translating = false;
    m_translated = true;
    setTranslateBusy(false);
    applyTranslatedTexts();
}

void ModDetailPage::applyTranslatedTexts()
{
    if (!m_zhSummary.isEmpty() && m_descriptionLabel)
        m_descriptionLabel->setText(m_zhSummary);
    if (!m_zhDetailHtml.isEmpty() && m_descriptionBrowser)
        m_descriptionBrowser->setHtml(m_zhDetailHtml);
}

void ModDetailPage::restoreOriginalTexts()
{
    if (!m_origSummary.isEmpty() && m_descriptionLabel)
        m_descriptionLabel->setText(m_origSummary);
    if (!m_origDetailHtml.isEmpty() && m_descriptionBrowser)
        m_descriptionBrowser->setHtml(m_origDetailHtml);
}

void ModDetailPage::setTranslateBusy(bool busy)
{
    if (m_translateSwitch)
        m_translateSwitch->setBusy(busy);
}

void ModDetailPage::resetTranslation()
{
    if (m_translating && m_translator)
        m_translator->cancelCurrentRequest();

    m_translating = false;
    m_translated = false;
    m_translateChunkIndex = 0;
    m_translateTexts.clear();
    m_translateKinds.clear();
    m_origSummary.clear();
    m_origDetailHtml.clear();
    m_zhSummary.clear();
    m_zhDetailHtml.clear();
    if (m_translateSwitch)
        m_translateSwitch->setChineseMode(false);
    setTranslateBusy(false);
}