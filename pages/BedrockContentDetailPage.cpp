#include "BedrockContentDetailPage.h"
#include "../utils/McimHelper.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPixmapCache>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpacerItem>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "components/BlurLoadingOverlay.h"
#include "components/NotificationManager.h"
#include "components/ScreenshotViewer.h"
#include "utils/DownloadTaskManager.h"
#include "utils/FavoritesManager.h"
#include "utils/IconHelper.h"
#include "utils/MultiThreadDownloader.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/mod/CurseForgeAPI.h"
#include "utils/bedrock/BedrockContentInstaller.h"

static QNetworkAccessManager *s_bedrockDetailNAM = nullptr;

static QNetworkAccessManager *sharedBedrockDetailNAM()
{
    if (!s_bedrockDetailNAM) {
        s_bedrockDetailNAM = new QNetworkAccessManager();
        QNetworkDiskCache *diskCache = new QNetworkDiskCache();
        const QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                  + "/bedrock_detail_media";
        QDir().mkpath(cachePath);
        diskCache->setCacheDirectory(cachePath);
        diskCache->setMaximumCacheSize(50 * 1024 * 1024);
        s_bedrockDetailNAM->setCache(diskCache);
    }
    return s_bedrockDetailNAM;
}

BedrockContentDetailPage::BedrockContentDetailPage(QWidget *parent)
    : QWidget(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    initUI();
    initAPI();
}

BedrockContentDetailPage::~BedrockContentDetailPage()
{
}

void BedrockContentDetailPage::initUI()
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

    // ── 进度区（默认隐藏）──
    QWidget *progressPanel = new QWidget(this);
    progressPanel->setObjectName("modDetailSection");
    QVBoxLayout *progressLayout = new QVBoxLayout(progressPanel);
    progressLayout->setContentsMargins(12, 10, 12, 10);
    progressLayout->setSpacing(8);

    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    progressLayout->addWidget(m_progressBar);

    m_progressLabel = new QLabel();
    m_progressLabel->setObjectName("hintLabel");
    m_progressLabel->setWordWrap(true);
    progressLayout->addWidget(m_progressLabel);

    m_openFolderBtn = new QPushButton(tr("打开下载目录"));
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    m_openFolderBtn->setVisible(false);
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                            + QStringLiteral("/blockbox_bedrock_packs");
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    progressLayout->addWidget(m_openFolderBtn, 0, Qt::AlignLeft);

    m_progressBar->hide();
    m_progressLabel->hide();
    m_openFolderBtn->hide();
    mainLayout->addWidget(progressPanel);

    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();
}

void BedrockContentDetailPage::initAPI()
{
    m_curseforgeAPI = new CurseForgeAPI(this);
    m_curseforgeAPI->setGameId(QStringLiteral("78022"));
    const QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (!apiKey.isEmpty())
        m_curseforgeAPI->setApiKey(apiKey);
    const QVariant mcimVal = SettingsManager::instance()->property("use_mcim");
    const bool useMcim = mcimVal.isValid() ? mcimVal.toBool() : true;
    m_curseforgeAPI->setBaseUrl(useMcim ? QString(MCIM_BASE) + "/curseforge" : CF_BASE);

    connect(m_curseforgeAPI, &CurseForgeAPI::modDetailReceived,
            this, &BedrockContentDetailPage::onDetailReceived);
    connect(m_curseforgeAPI, &CurseForgeAPI::modDetailFailed,
            this, &BedrockContentDetailPage::onDetailFailed);
}

QString BedrockContentDetailPage::comMojangDir() const
{
    BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
    mgr->ensureInitialized();
    const BedrockInstance inst = mgr->activeInstance();
    if (inst.id.isEmpty() || inst.dataDir.isEmpty())
        return QString();
    return inst.dataDir + QStringLiteral("/com.mojang");
}

void BedrockContentDetailPage::setModInfo(const ModInfo &info)
{
    m_modInfo = info;
    m_versionFiles = info.versionFiles;
    m_comMojangDir = comMojangDir();

    clearContent();
    buildHeader();
    buildDownloadBlock();
    buildLinksBlock();
    buildTagsBlock();
    buildDescriptionBlock();
    buildScreenshotBlock();

    if (m_loadingOverlay)
        m_loadingOverlay->showOverlay(tr("正在获取资源详情..."));

    if (!info.id.isEmpty())
        m_curseforgeAPI->fetchModDetail(info.id);
    else
        m_loadingOverlay->hideOverlay();
}

void BedrockContentDetailPage::setBedrockClassId(int classId)
{
    m_bedrockClassId = classId;
    if (m_bedrockCategory.isEmpty())
        m_bedrockCategory = FavoritesManager::bedrockClassIdToCategoryName(classId);
    m_contentType = FavoritesManager::bedrockClassIdToContentType(classId);
}

void BedrockContentDetailPage::setBedrockCategory(const QString &category)
{
    m_bedrockCategory = category;
}

void BedrockContentDetailPage::clearContent()
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
    m_nameLabel = nullptr;
    m_descLabel = nullptr;
    m_favoriteBtn = nullptr;
    m_leftPanel = nullptr;
    m_leftLayout = nullptr;
    m_downloadContainer = nullptr;
    m_downloadListLayout = nullptr;
    m_rightPanel = nullptr;
    m_rightLayout = nullptr;
    m_linksContainer = nullptr;
    m_linksLayout = nullptr;
    m_tagsContainer = nullptr;
    m_descriptionBrowser = nullptr;
    m_screenshotSection = nullptr;
    m_screenshotStrip = nullptr;
    m_downloadCards.clear();
    m_progressBar->hide();
    m_progressLabel->hide();
    m_openFolderBtn->hide();
}

// ── 头部 ────────────────────────────────────────────────────────────────

void BedrockContentDetailPage::buildHeader()
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
    placeholder.fill(ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme
                         ? QColor("#2d2d2d") : QColor("#e8e8e8"));
    m_iconLabel->setPixmap(placeholder);
    headerLayout->addWidget(m_iconLabel);

    QWidget *nameWidget = new QWidget();
    QVBoxLayout *nameLayout = new QVBoxLayout(nameWidget);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(4);

    m_nameLabel = new QLabel(m_modInfo.name);
    m_nameLabel->setObjectName("modDetailChineseName");
    nameLayout->addWidget(m_nameLabel);

    QLabel *srcLabel = new QLabel(tr("CurseForge · Minecraft 基岩版"));
    srcLabel->setObjectName("modDetailEnglishName");
    nameLayout->addWidget(srcLabel);

    m_descLabel = new QLabel(m_modInfo.description);
    m_descLabel->setObjectName("modDetailDesc");
    m_descLabel->setWordWrap(true);
    nameLayout->addWidget(m_descLabel);

    headerLayout->addWidget(nameWidget, 1);

    // 收藏按钮：点击弹出「收藏到分组」菜单（与 Java 版 ContentDetailPage 一致）
    m_favoriteBtn = new QPushButton(m_headerWidget);
    m_favoriteBtn->setObjectName("contentFavoriteBtn");
    m_favoriteBtn->setCursor(Qt::PointingHandCursor);
    m_favoriteBtn->setToolTip(tr("加入收藏夹"));
    m_favoriteBtn->setFixedSize(40, 40);
    m_favoriteBtn->setIconSize(QSize(20, 20));
    connect(m_favoriteBtn, &QPushButton::clicked, this, &BedrockContentDetailPage::showFavoriteMenu);
    headerLayout->addWidget(m_favoriteBtn);

    m_contentLayout->addWidget(m_headerWidget);
    updateFavoriteButtonIcon();
}

void BedrockContentDetailPage::updateFavoriteButtonIcon()
{
    if (!m_favoriteBtn)
        return;

    if (m_modInfo.id.isEmpty())
        m_currentItemId.clear();
    else
        m_currentItemId = m_currentSource + ":" + m_modInfo.id;

    const QList<QString> containingFolders =
        FavoritesManager::instance()->foldersContaining(m_currentItemId);
    const bool isFav = !containingFolders.isEmpty();

    const QColor iconColor = isFav ? QColor("#ffc107") : QColor("#888888");
    m_favoriteBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/star.svg", iconColor, 20));
    m_favoriteBtn->setToolTip(isFav ? tr("已收藏，点击管理") : tr("加入收藏夹"));
}

void BedrockContentDetailPage::refreshFavoriteState()
{
    updateFavoriteButtonIcon();
}

void BedrockContentDetailPage::showFavoriteMenu()
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
        emit addToFavoritesRequested(m_modInfo, m_contentType, m_currentSource,
                                     m_bedrockClassId, m_bedrockCategory);
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
        NotificationManager::showSuccess(this, tr("已从收藏夹移除"));
    }
    else
    {
        FavoritesManager::instance()->addFavorite(folderId, m_modInfo, m_contentType,
                                                  m_currentSource, m_bedrockClassId, m_bedrockCategory);
        NotificationManager::showSuccess(this, tr("已添加到收藏夹"));
    }
    updateFavoriteButtonIcon();
}

// ── 下载区 ──────────────────────────────────────────────────────────────

void BedrockContentDetailPage::buildDownloadBlock()
{
    m_leftPanel = new QWidget();
    m_leftPanel->setObjectName("modDetailLeftPanel");
    m_leftPanel->setAttribute(Qt::WA_StyledBackground);

    m_leftLayout = new QVBoxLayout(m_leftPanel);
    m_leftLayout->setContentsMargins(0, 0, 0, 0);
    m_leftLayout->setSpacing(16);

    QWidget *section = new QWidget();
    section->setObjectName("modDetailSection");
    QVBoxLayout *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(8);

    QLabel *title = new QLabel(tr("版本下载"));
    title->setObjectName("modDetailSectionTitle");
    sectionLayout->addWidget(title);

    m_downloadContainer = new QWidget();
    m_downloadListLayout = new QVBoxLayout(m_downloadContainer);
    m_downloadListLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadListLayout->setSpacing(6);

    if (m_versionFiles.isEmpty()) {
        ModVersionFile vf;
        vf.version = m_modInfo.latestVersion;
        vf.gameVersions = m_modInfo.gameVersions;
        vf.downloadUrl = m_modInfo.downloadUrl;
        m_downloadListLayout->addWidget(createDownloadCard(vf));
    } else {
        for (const ModVersionFile &vf : m_versionFiles)
            m_downloadListLayout->addWidget(createDownloadCard(vf));
    }
    sectionLayout->addWidget(m_downloadContainer);
    m_leftLayout->addWidget(section);
    m_leftLayout->addStretch();

    QWidget *mainBody = new QWidget();
    mainBody->setObjectName("modDetailBody");
    QHBoxLayout *bodyLayout = new QHBoxLayout(mainBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(16);

    m_rightPanel = new QWidget();
    m_rightPanel->setObjectName("modDetailRightPanel");
    m_rightLayout = new QVBoxLayout(m_rightPanel);
    m_rightLayout->setContentsMargins(0, 0, 0, 0);
    m_rightLayout->setSpacing(16);
    m_rightLayout->addStretch();

    bodyLayout->addWidget(m_leftPanel, 2);
    bodyLayout->addWidget(m_rightPanel, 3);
    m_contentLayout->addWidget(mainBody, 1);
}

QWidget *BedrockContentDetailPage::createDownloadCard(const ModVersionFile &vf)
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
    if (vf.fileSize > 0)
        chipLayout->addWidget(createChip(formatFileSize(vf.fileSize)));
    chipLayout->addStretch();
    infoLayout->addWidget(chipRow);

    cardLayout->addWidget(infoWidget, 1);

    QPushButton *installBtn = new QPushButton();
    installBtn->setObjectName("modDetailDownloadBtn");
    installBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/download.svg", QColor("#ffffff"), 18));
    installBtn->setIconSize(QSize(18, 18));
    installBtn->setFixedSize(32, 32);
    installBtn->setToolTip(tr("下载并安装"));
    installBtn->setCursor(Qt::PointingHandCursor);
    if (!vf.downloadUrl.isEmpty()) {
        connect(installBtn, &QPushButton::clicked, this, [this, vf]() {
            startDownloadAndInstall(vf);
        });
    } else {
        installBtn->setEnabled(false);
    }
    cardLayout->addWidget(installBtn);

    m_downloadCards.append(card);
    return card;
}

QLabel *BedrockContentDetailPage::createChip(const QString &text)
{
    QLabel *chip = new QLabel(text);
    chip->setObjectName("modDetailChip");
    chip->setProperty("cardRole", "chip");
    return chip;
}

// ── 右侧区块 ────────────────────────────────────────────────────────────

void BedrockContentDetailPage::buildLinksBlock()
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

    struct LinkInfo { QString label; QString url; };
    QList<LinkInfo> links;
    links.append({ tr("CurseForge"), m_modInfo.pageUrl });
    links.append({ tr("必应搜索"), QStringLiteral("https://www.bing.com/search?q=%1").arg(m_modInfo.name) });
    links.append({ tr("百度搜索"), QStringLiteral("https://www.baidu.com/s?wd=%1").arg(m_modInfo.name) });

    for (const LinkInfo &link : links) {
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
    m_rightLayout->insertWidget(0, section);
}

void BedrockContentDetailPage::buildTagsBlock()
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
        if (value.isEmpty())
            return;
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
    if (m_modInfo.dateModified.isValid())
        addTagRow(tr("更新时间"), m_modInfo.dateModified.toString("yyyy-MM-dd HH:mm"));
    if (!m_modInfo.categories.isEmpty())
        addTagRow(tr("分类"), m_modInfo.categories.join(", "));
    if (!m_modInfo.gameVersions.isEmpty())
        addTagRow(tr("支持版本"), m_modInfo.gameVersions.join(", "));

    sectionLayout->addWidget(m_tagsContainer);
    m_rightLayout->insertWidget(1, section);
}

void BedrockContentDetailPage::buildDescriptionBlock()
{
    const QString desc = m_modInfo.detailedDescription.isEmpty()
        ? m_modInfo.description : m_modInfo.detailedDescription;
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

    m_rightLayout->addWidget(section);
}

void BedrockContentDetailPage::buildScreenshotBlock()
{
    if (m_modInfo.screenshots.isEmpty())
        return;

    m_screenshotSection = new QWidget();
    m_screenshotSection->setObjectName("modDetailSection");
    QVBoxLayout *sectionLayout = new QVBoxLayout(m_screenshotSection);
    sectionLayout->setContentsMargins(12, 12, 12, 12);
    sectionLayout->setSpacing(10);

    QLabel *title = new QLabel(tr("游戏截图"));
    title->setObjectName("modDetailSectionTitle");
    sectionLayout->addWidget(title);

    m_screenshotStrip = new QWidget();
    m_screenshotStrip->setObjectName("modDetailThumbStrip");
    m_screenshotStrip->setFixedHeight(110);
    QHBoxLayout *stripLayout = new QHBoxLayout(m_screenshotStrip);
    stripLayout->setContentsMargins(0, 0, 0, 0);
    stripLayout->setSpacing(8);

    const QList<ModScreenshot> shots = m_modInfo.screenshots;
    m_screenshotStrip->setFixedWidth(shots.size() * 148 + 4);
    m_screenshotPixmaps.clear();

    for (int i = 0; i < shots.size(); ++i) {
        const QString url = shots[i].url;
        QLabel *thumb = new QLabel();
        thumb->setObjectName("modDetailScreenshotThumb");
        thumb->setFixedSize(140, 90);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setCursor(Qt::PointingHandCursor);
        thumb->setProperty("screenshotIndex", i);
        thumb->installEventFilter(this);
        const bool isDarkThumb = (ThemeManager::instance()->currentTheme() != ThemeManager::LightTheme);
        QPixmap placeholder(140, 90);
        placeholder.fill(isDarkThumb ? QColor("#2d2d2d") : Qt::white);
        thumb->setPixmap(placeholder);
        m_screenshotPixmaps.append(placeholder);
        stripLayout->addWidget(thumb);

        const QString cacheKey = QString("bedrock_screenshot:%1").arg(url);
        QPixmap cached;
        if (QPixmapCache::find(cacheKey, &cached)) {
            m_screenshotPixmaps[i] = cached;
            thumb->setPixmap(cached.scaled(140, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else if (!url.isEmpty()) {
            QUrl qurl(McimHelper::rewriteImageUrl(url));
            QNetworkRequest request(qurl);
            request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                                 QNetworkRequest::PreferCache);
            QNetworkReply *reply = sharedBedrockDetailNAM()->get(request);
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply, thumb, cacheKey, i]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError)
                    return;
                QPixmap pix;
                pix.loadFromData(reply->readAll());
                if (!pix.isNull()) {
                    QPixmapCache::insert(cacheKey, pix);
                    if (i < m_screenshotPixmaps.size())
                        m_screenshotPixmaps[i] = pix;
                    thumb->setPixmap(pix.scaled(140, 90, Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
                }
            });
        }
    }
    stripLayout->addStretch();

    QScrollArea *thumbScroll = new QScrollArea();
    thumbScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    thumbScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    thumbScroll->setFrameShape(QFrame::NoFrame);
    thumbScroll->setWidget(m_screenshotStrip);
    sectionLayout->addWidget(thumbScroll);

    m_rightLayout->insertWidget(0, m_screenshotSection);
}

bool BedrockContentDetailPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QLabel *thumb = qobject_cast<QLabel *>(watched);
        if (thumb && thumb->objectName() == QLatin1String("modDetailScreenshotThumb")) {
            const int idx = thumb->property("screenshotIndex").toInt();
            if (idx >= 0 && idx < m_screenshotPixmaps.size()) {
                ScreenshotViewer *viewer = new ScreenshotViewer(m_screenshotPixmaps, idx, this);
                viewer->setAttribute(Qt::WA_DeleteOnClose);
                viewer->show();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString BedrockContentDetailPage::formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024LL * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

// ── 详情数据 ──────────────────────────────────────────────────────────────

void BedrockContentDetailPage::onDetailReceived(const ModInfo &detail)
{
    if (m_loadingOverlay)
        m_loadingOverlay->hideOverlay();

    m_modInfo = detail;
    m_modInfo.screenshots = detail.screenshots;

    // 重建下载列表
    m_versionFiles = detail.versionFiles;
    if (m_downloadListLayout) {
        while (QLayoutItem *item = m_downloadListLayout->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
        m_downloadCards.clear();
        if (m_versionFiles.isEmpty()) {
            ModVersionFile vf;
            vf.version = detail.latestVersion;
            vf.gameVersions = detail.gameVersions;
            vf.downloadUrl = detail.downloadUrl;
            m_downloadListLayout->addWidget(createDownloadCard(vf));
        } else {
            for (const ModVersionFile &vf : m_versionFiles)
                m_downloadListLayout->addWidget(createDownloadCard(vf));
        }
    }

    if (m_descLabel && !detail.description.isEmpty())
        m_descLabel->setText(detail.description);

    if (m_descriptionBrowser && !detail.detailedDescription.isEmpty())
        m_descriptionBrowser->setHtml(detail.detailedDescription);

    // 重建截图
    if (m_screenshotSection && !detail.screenshots.isEmpty()) {
        m_screenshotSection->deleteLater();
        m_screenshotSection = nullptr;
        m_screenshotStrip = nullptr;
        buildScreenshotBlock();
    }

    refreshFavoriteState();
}

void BedrockContentDetailPage::onDetailFailed(const QString &error)
{
    if (m_loadingOverlay)
        m_loadingOverlay->hideOverlay();
    NotificationManager::showInfo(this, tr("获取资源详情失败: %1").arg(error));
}

// ── 下载并安装 ────────────────────────────────────────────────────────────

void BedrockContentDetailPage::startDownloadAndInstall(const ModVersionFile &vf)
{
    if (m_downloader)
        return;
    if (vf.downloadUrl.isEmpty()) {
        NotificationManager::showInfo(this, tr("该版本暂无可用的下载链接。"));
        return;
    }

    if (m_comMojangDir.isEmpty()) {
        NotificationManager::showInfo(this,
            tr("尚未选择基岩版实例，请先在「资源>安装新实例」中安装基岩版并创建实例。"));
        return;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/blockbox_bedrock_packs");
    QDir d(dir);
    if (!d.exists())
        d.mkpath(QStringLiteral("."));

    QString fileName = vf.fileName;
    if (fileName.isEmpty()) {
        QUrl url(vf.downloadUrl);
        fileName = url.fileName();
    }
    if (fileName.isEmpty())
        fileName = QStringLiteral("bedrock_pack.mcpack");
    fileName.replace(QChar(' '), QChar('_'));
    const QString savePath = dir + QLatin1Char('/') + fileName;

    DownloadTaskManager *mgr = DownloadTaskManager::instance();
    m_activeTaskId = mgr->addTask(fileName, dir, m_modInfo.name,
                                  QStringList() << QStringLiteral("bedrock"));
    mgr->updateTaskStatus(m_activeTaskId, DownloadTaskStatus::Downloading, tr("连接中..."));

    m_downloader = new MultiThreadDownloader(this);
    connect(m_downloader, &MultiThreadDownloader::downloadProgress, this,
        [this](const QString &, qint64 received, qint64 total) {
            if (total > 0) {
                const int pct = static_cast<int>((received * 100) / total);
                m_progressBar->setVisible(true);
                m_progressBar->setValue(pct);
                m_progressLabel->setVisible(true);
                m_progressLabel->setText(tr("正在下载... %1 / %2")
                                             .arg(formatFileSize(received), formatFileSize(total)));
                DownloadTaskManager::instance()->updateTaskProgressPercent(m_activeTaskId, pct);
            }
        });
    connect(m_downloader, &MultiThreadDownloader::downloadCompleted, this,
        [this](const QString &, const QString &path) {
            DownloadTaskManager *mgr = DownloadTaskManager::instance();
            mgr->updateTaskProgressPercent(m_activeTaskId, 100);
            mgr->updateTaskStatus(m_activeTaskId, DownloadTaskStatus::Completed, tr("下载完成"));
            m_activeTaskId.clear();

            m_downloader->deleteLater();
            m_downloader = nullptr;

            m_progressBar->setVisible(true);
            m_progressBar->setValue(100);
            m_progressLabel->setText(tr("下载完成: %1").arg(path));
            m_openFolderBtn->setVisible(true);

            doInstall(path);
        });
    connect(m_downloader, &MultiThreadDownloader::downloadFailed, this,
        [this](const QString &, const QString &error) {
            DownloadTaskManager::instance()->updateTaskStatus(m_activeTaskId,
                                                              DownloadTaskStatus::Failed, error);
            m_activeTaskId.clear();
            m_progressBar->setVisible(true);
            m_progressBar->setValue(0);
            m_progressLabel->setVisible(true);
            m_progressLabel->setText(tr("下载失败: %1").arg(error));
            NotificationManager::showError(this, tr("基岩版资源下载失败: %1").arg(error));
            m_downloader->deleteLater();
            m_downloader = nullptr;
        });

    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_progressLabel->setVisible(true);
    m_progressLabel->setText(tr("准备下载..."));

    const int threads = SettingsManager::instance()->getDownloadThreadCount();
    m_downloader->startDownload(vf.downloadUrl, savePath, threads, m_activeTaskId,
                                QStringLiteral("BlockBox/1.0"));
}

void BedrockContentDetailPage::doInstall(const QString &filePath)
{
    m_progressLabel->setText(tr("正在安装到基岩版实例..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);

    QString err;
    const bool ok = BedrockContentInstaller::installPack(filePath, m_comMojangDir, &err);

    QApplication::restoreOverrideCursor();
    QFile::remove(filePath);

    if (ok) {
        m_progressLabel->setText(tr("安装完成，启动基岩版后即可在游戏中使用。"));
        NotificationManager::showSuccess(this, tr("基岩版资源安装成功！"), 7000);
    } else {
        m_progressLabel->setText(tr("安装失败: %1").arg(err));
        NotificationManager::showError(this, tr("基岩版资源安装失败: %1").arg(err));
    }
}
