/**
 * @file   InstallInstancePage.cpp
 * @brief  安装实例页面实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "InstallInstancePage.h"

#include <utility>

#include <QAbstractItemView>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>

#include <QRegularExpression>
#include <QSaveFile>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrlQuery>

#include "components/BlurLoadingOverlay.h"
#include "components/ContentViewSwitch.h"
#include "components/MasonryContentCard.h"
#include "components/NotificationManager.h"
#include "layouts/MasonryLayout.h"
#include <QResizeEvent>
#include <QTimer>
#include <QUrl>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/mod/ModData.h"

namespace {
// 瀑布流卡片固定宽度（与 MasonryContentCard 内部 kCardWidth 一致），封面回填时的兜底尺寸
constexpr int kMasonryCardW = 260;
constexpr int kMasonryBannerH = 120;
constexpr int kCoverChunkSize = 12;   // MediaWiki titles 上限 50，需预留 4 个 banner 候选/版本
/* 封面 URL 磁盘缓存：命中后不再请求 wiki 接口 */
QString coverCacheFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/version_covers/covers.json");
}

// 愚人节版本判定（筛选分支与类型图标共用，避免两处逻辑漂移）
bool isAprilFoolsId(const QString &id)
{
    if (id.contains("craftmine") ||
        id.contains("potato") ||
        id.contains("oneblockatatime") ||
        id.contains("infinite") ||
        id.contains("Shareware") ||
        id.contains("RV-Pre") ||
        id.contains("blue") || id.contains("red") || id.contains("purple") ||
        id.contains("_or_") ||
        id.contains("April", Qt::CaseInsensitive) ||
        id.contains("Fools", Qt::CaseInsensitive)) {
        return true;
    }
    const QStringList aprilFools = {
        "25w14craftmine", "24w14potato", "23w13a or b", "22w13oneblockatatime",
        "20w14infinite", "3D Shareware v1.34", "1.RV-Pre1", "15w14a",
        "2.0 blue", "2.0 red", "2.0 purple"
    };
    if (aprilFools.contains(id))
        return true;
    if (id.length() >= 6 && id[2] == 'w') {
        const int week = id.mid(3, 2).toInt();
        if ((week == 13 || week == 14) && id.length() > 5) {
            const QString afterWeek = id.mid(5);
            if (afterWeek.length() > 1 || !afterWeek.at(0).isLetter())
                return true;
        }
    }
    return false;
}

// 版本类型 → 卡片图标（qrc 内的方块图）
QString iconPathForVersion(const QString &id, const QString &type)
{
    if (isAprilFoolsId(id))                       // 愚人节版（清单 type 多为 snapshot，需先行判定）
        return QStringLiteral(":/Images/Block/Egg_enchanted.png");
    if (type == QLatin1String("release"))
        return QStringLiteral(":/Images/Block/Grass_Block.png");
    if (type == QLatin1String("snapshot"))
        return QStringLiteral(":/Images/Block/Command_Block.png");
    return QStringLiteral(":/Images/Block/Stone.png");   // old_beta / old_alpha 及其余
}

} // namespace

InstallInstancePage::InstallInstancePage(QWidget *parent)
    : QWidget(parent)
    , m_modifyBanner(nullptr)
    , m_modifyBannerLayout(nullptr)
    , m_modifyBannerLabel(nullptr)
    , m_modifyInstanceBtn(nullptr)
    , m_exitModifyBtn(nullptr)
    , m_modifyMode(false)
    , m_viewSwitch(nullptr)
    , m_viewStack(nullptr)
    , m_masonryScroll(nullptr)
    , m_masonryContainer(nullptr)
    , m_masonryLayout(nullptr)
    , m_loadingOverlay(nullptr)
    , m_networkManager(nullptr)
    , m_currentSource("official")
    , m_currentFilter("release")
    , m_versionsLoaded(false)
{
    initUI();
    initNetworkManager();
}

InstallInstancePage::~InstallInstancePage()
{
    // m_networkManager 以 this 为 parent 创建，由 Qt 父子机制自动释放，
    // 手动 delete 会导致双重释放，故此处不再手动删除。
}

void InstallInstancePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 首次进入页面时自动加载版本列表；再次进入不重新拉取（避免反复请求）
    if (!m_versionsLoaded) {
        m_versionsLoaded = true;
        onRefreshVersions();
    }
}

void InstallInstancePage::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(20, 20, 20, 20);
    m_mainLayout->setSpacing(20);

    // Filter bar with dropdowns —— 顶部微透明玻璃态卡片
    m_filterBar = new QWidget(this);
    m_filterBar->setObjectName("filterCard");
    m_filterBarLayout = new QHBoxLayout(m_filterBar);
    m_filterBarLayout->setContentsMargins(14, 10, 14, 10);
    m_filterBarLayout->setSpacing(10);

    QLabel *sourceLabel = new QLabel(tr("数据源:"), m_filterBar);
    sourceLabel->setObjectName("loaderSectionLabel");
    m_filterBarLayout->addWidget(sourceLabel);

    m_sourceCombo = new QComboBox();
    m_sourceCombo->setFixedHeight(32);
    m_sourceCombo->setMinimumWidth(120);
    m_sourceCombo->addItem(tr("官方源"));
    m_sourceCombo->addItem(tr("BMCL源"));
    m_filterBarLayout->addWidget(m_sourceCombo);

    QLabel *filterLabel = new QLabel(tr("版本类型:"), m_filterBar);
    filterLabel->setObjectName("loaderSectionLabel");
    m_filterBarLayout->addWidget(filterLabel);

    m_filterCombo = new QComboBox();
    m_filterCombo->setFixedHeight(32);
    m_filterCombo->setMinimumWidth(120);
    m_filterCombo->addItem(tr("正式版"));
    m_filterCombo->addItem(tr("测试版"));
    m_filterCombo->addItem(tr("愚人节版"));
    m_filterCombo->addItem(tr("远古版"));
    m_filterBarLayout->addWidget(m_filterCombo);

    m_filterBarLayout->addStretch();

    // 视图切换（列表式 / 瀑布流）
    m_viewSwitch = new ContentViewSwitch(m_filterBar);
    m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted("install_instance",
                                                               ContentViewSwitch::Masonry));
    m_viewMode = m_viewSwitch->viewMode();
    m_filterBarLayout->addWidget(m_viewSwitch);

    m_modifyInstanceBtn = new QPushButton(tr("修改现有实例"), m_filterBar);
    m_modifyInstanceBtn->setObjectName("instanceActionBtn");
    m_modifyInstanceBtn->setCursor(Qt::PointingHandCursor);
    m_filterBarLayout->addWidget(m_modifyInstanceBtn);
    connect(m_modifyInstanceBtn, &QPushButton::clicked, this, &InstallInstancePage::onModifyExistingInstanceClicked);

    m_mainLayout->addWidget(m_filterBar);

    // 修改现有实例模式顶部提示条（默认隐藏）
    m_modifyBanner = new QWidget(this);
    m_modifyBanner->setObjectName("modifyBanner");
    m_modifyBannerLayout = new QHBoxLayout(m_modifyBanner);
    m_modifyBannerLayout->setContentsMargins(12, 8, 12, 8);
    m_modifyBannerLayout->setSpacing(10);

    m_modifyBannerLabel = new QLabel(m_modifyBanner);
    m_modifyBannerLabel->setObjectName("modifyBannerLabel");
    m_modifyBannerLayout->addWidget(m_modifyBannerLabel, 1);

    m_exitModifyBtn = new QPushButton(tr("取消修改"), m_modifyBanner);
    m_exitModifyBtn->setObjectName("instanceActionBtn");
    m_exitModifyBtn->setCursor(Qt::PointingHandCursor);
    m_modifyBannerLayout->addWidget(m_exitModifyBtn);

    m_modifyBanner->setVisible(false);
    m_mainLayout->addWidget(m_modifyBanner);

    connect(m_exitModifyBtn, &QPushButton::clicked, this, &InstallInstancePage::exitModifyMode);

    // Blur loading overlay
    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();

    m_versionList = new QListWidget(this);
    m_versionList->setObjectName("versionListWidget");
    m_versionList->setProperty("flatContainer", true);
    m_versionList->setSelectionMode(QAbstractItemView::NoSelection);
    m_versionList->setAlternatingRowColors(false);
    m_versionList->setUniformItemSizes(true);
    m_versionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_versionList->setMovement(QListView::Static);
    m_versionList->setSpacing(8);
    m_versionList->setMinimumHeight(400);

    // 瀑布流页：滚动区 + 最短列优先布局容器
    m_masonryScroll = new QScrollArea(this);
    m_masonryScroll->setObjectName("modScrollArea");
    m_masonryScroll->setWidgetResizable(true);
    m_masonryScroll->setFrameShape(QFrame::NoFrame);
    m_masonryContainer = new QWidget();
    m_masonryContainer->setObjectName("modCardContainer");
    m_masonryLayout = new MasonryLayout(m_masonryContainer, 0, 12, 12);
    m_masonryLayout->setContentsMargins(0, 0, 0, 0);
    m_masonryScroll->setWidget(m_masonryContainer);

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_versionList);
    m_viewStack->addWidget(m_masonryScroll);
    m_viewStack->setCurrentIndex(m_viewMode == ContentViewSwitch::Masonry ? 1 : 0);
    m_mainLayout->addWidget(m_viewStack);

    // Connect signals
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstallInstancePage::onSourceChanged);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstallInstancePage::onVersionTypeFilterChanged);
    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged,
            this, &InstallInstancePage::onViewModeChanged);
    connect(m_versionList, &QListWidget::itemClicked, this, [=](QListWidgetItem *item) {
        const QString versionId = item->data(Qt::UserRole).toString();
        if (!versionId.isEmpty())
            selectVersion(versionId);
    });
}

void InstallInstancePage::initNetworkManager()
{
    m_networkManager = new QNetworkAccessManager(this);
}

void InstallInstancePage::onSourceChanged(int id)
{
    if (id == 0) {
        m_currentSource = "official";
    } else {
        m_currentSource = "bmcl";
    }
    onRefreshVersions();
}

void InstallInstancePage::onVersionTypeFilterChanged(int id)
{
    switch (id) {
    case 0:
        m_currentFilter = "release";
        break;
    case 1:
        m_currentFilter = "snapshot";
        break;
    case 2:
        m_currentFilter = "prerelease";
        break;
    case 3:
        m_currentFilter = "old";
        break;
    }
    populateVersionList();
}

void InstallInstancePage::onRefreshVersions()
{
    m_loadingOverlay->showOverlay(tr("正在获取版本列表..."));
    m_loadingOverlay->updateProgress(30);
    m_versionList->clear();

    if (m_currentSource == "official") {
        getVersionsFromOfficial();
    } else {
        getVersionsFromBMCL();
    }
}

void InstallInstancePage::getVersionsFromOfficial()
{
    QUrl url("https://launchermeta.mojang.com/mc/game/version_manifest.json");
    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            parseVersionsResponse(doc);
        } else {
            QString errorMsg = tr("获取版本列表失败: %1").arg(reply->errorString());
            NotificationManager::showError(this, errorMsg);
            m_loadingOverlay->showError(errorMsg);
            // 加载失败，下次进入页面时重试
            m_versionsLoaded = false;
        }
        reply->deleteLater();
    });
}

void InstallInstancePage::getVersionsFromBMCL()
{
    QUrl url("https://bmclapi2.bangbang93.com/mc/game/version_manifest.json");
    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            parseVersionsResponse(doc);
        } else {
            QString errorMsg = tr("获取版本列表失败: %1").arg(reply->errorString());
            NotificationManager::showError(this, errorMsg);
            m_loadingOverlay->showError(errorMsg);
            // 加载失败，下次进入页面时重试
            m_versionsLoaded = false;
        }
        reply->deleteLater();
    });
}

void InstallInstancePage::parseVersionsResponse(const QJsonDocument &doc)
{
    // Clear the array by reassigning an empty array
    m_allVersions = QJsonArray();

    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains("versions")) {
            m_allVersions = obj["versions"].toArray();
        }
    }

    m_loadingOverlay->updateProgress(70, tr("正在构建版本卡片..."));
    rebuildVersionCards();
    m_loadingOverlay->hideOverlay();
}

void InstallInstancePage::populateVersionList()
{
    m_versionList->clear();
    m_versionTypeMap.clear();
    m_versionRawType.clear();
    if (!m_coverCacheLoaded)
        loadCoverCache();

    // 清空瀑布流卡片（takeAt 拿到的 QWidgetItem 不持有 widget，需手动删除）
    m_masonryCards.clear();
    if (m_masonryLayout) {
        while (QLayoutItem *item = m_masonryLayout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    }

    QStringList coverIds;
    for (const QJsonValue &value : std::as_const(m_allVersions)) {
        QJsonObject versionObj = value.toObject();
        QString id = versionObj["id"].toString();
        QString type = versionObj["type"].toString();

        // Filter by version type
        if (m_currentFilter == "release") {
            if (type != "release") {
                continue;
            }
        } else if (m_currentFilter == "snapshot") {
            bool isTestVersion = false;
            if (type == "snapshot") {
                isTestVersion = true;
            }
            if (id.contains("snapshot", Qt::CaseInsensitive) ||
                id.contains("rc", Qt::CaseInsensitive) ||
                id.contains("pre", Qt::CaseInsensitive) ||
                id.contains("exp", Qt::CaseInsensitive) ||
                id.contains("Combat Test", Qt::CaseInsensitive)) {
                isTestVersion = true;
            }
            if (id.length() == 6 && id[2] == 'w' &&
                id.left(2).toInt() > 0 &&
                id.mid(3, 2).toInt() > 0 &&
                (id[5] == 'a' || id[5] == 'b')) {
                isTestVersion = true;
            }
            if (id.length() == 7 && id[2] == 'w' &&
                id.left(2).toInt() > 0 &&
                id.mid(3, 2).toInt() > 0 &&
                (id[6] == 'a' || id[6] == 'b')) {
                isTestVersion = true;
            }
            if (!isTestVersion) {
                continue;
            }
        } else if (m_currentFilter == "prerelease") {
            if (!isAprilFoolsId(id)) {
                continue;
            }
        } else if (m_currentFilter == "old") {
            bool isOldVersion = false;
            if (id.startsWith("rd-") || id.startsWith("mc-") || id.startsWith("0.0.") ||
                id.startsWith("0.2") || id.startsWith("0.3") ||
                id.startsWith("Infdev") || id.startsWith("v1.0.") ||
                id.startsWith("v1.1.") || id.startsWith("v1.2.") || id.startsWith("Beta 1.")) {
                isOldVersion = true;
            }
            QStringList oldVersions = {
                "0.30", "0.31 20091223-1", "0.31 20091223-2", "0.31 20091224",
                "0.31 20091225", "0.31 20091226", "0.31 20091227", "0.31 20091228",
                "0.31 20091229", "0.31 20100101", "0.31 20100102", "0.31 20100103",
                "0.31 20100104", "0.31 20100105", "0.31 20100106", "0.31 20100111-1",
                "0.31 20100111-2", "0.31 20100113", "0.31 20100113 (Creative)", "0.31 20100120",
                "0.31 20100205", "0.31 20100206", "0.31 20100207-1", "0.31 20100207-2",
                "0.31 20100211", "0.31 20100212-1", "0.31 20100212-2", "Infdev 20100227",
                "Infdev 20100320", "Infdev 20100321", "Infdev 20100325", "Infdev 20100327",
                "Infdev 20100330-1", "Infdev 20100330-2", "Infdev 20100415", "Infdev 20100416",
                "Infdev 20100417", "Infdev 20100418", "Infdev 20100419", "Infdev 20100420",
                "Infdev 20100503", "Infdev 20100512", "Infdev 20100513", "Infdev 20100514",
                "Infdev 20100515", "Infdev 20100516", "Infdev 20100517", "Infdev 20100518",
                "Infdev 20100519", "Infdev 20100520", "Infdev 20100618", "Infdev 20100624",
                "Infdev 20100625-1", "Infdev 20100625-2", "Infdev 20100630", "v1.0.0",
                "v1.0.1", "v1.0.2", "v1.0.2_01", "v1.0.2_02", "v1.0.3", "v1.0.4", "v1.0.5",
                "v1.0.6", "v1.0.7", "v1.0.8", "v1.0.9", "v1.0.10", "v1.0.11", "v1.0.12",
                "v1.0.13", "v1.0.13_01", "v1.0.14", "v1.0.15", "v1.0.16", "v1.0.17", "v1.1.0",
                "v1.1.0-2", "v1.2.0", "v1.2.0_01", "v1.2.1", "v1.2.2", "v1.2.3", "v1.2.3_01",
                "v1.2.3_02", "v1.2.3_03", "v1.2.3_04", "v1.2.4", "v1.2.5", "v1.2.6", "Beta 1.0",
                "Beta 1.0_01", "Beta 1.0.2", "Beta 1.1", "Beta 1.1_01", "Beta 1.1_02", "Beta 1.2",
                "Beta 1.2_01", "Beta 1.2_02", "Beta 1.3", "Beta 1.3_01", "Beta 1.4", "Beta 1.4_01",
                "Beta 1.5", "Beta 1.5_01", "Beta 1.5_02", "Beta 1.6.1", "Beta 1.6.2", "Beta 1.6.4",
                "Beta 1.7", "Beta 1.7_01", "Beta 1.7.2", "Beta 1.7.3", "Beta 1.8", "Beta 1.8-pre1",
                "Beta 1.8-pre2", "Beta 1.8-pre3", "Beta 1.8-pre4", "Beta 1.8-pre5", "Beta 1.8-pre6",
                "Beta 1.8-pre7", "Beta 1.8.1"
            };
            if (oldVersions.contains(id)) {
                isOldVersion = true;
            }
            if (!isOldVersion) {
                continue;
            }
        }

        // Version date
        QString dateString;
        if (versionObj.contains("releaseTime")) {
            QDateTime releaseTime = QDateTime::fromString(versionObj["releaseTime"].toString(), Qt::ISODate);
            dateString = releaseTime.toString("yyyy-MM-dd HH:mm");
        } else if (versionObj.contains("time")) {
            QDateTime time = QDateTime::fromString(versionObj["time"].toString(), Qt::ISODate);
            dateString = time.toString("yyyy-MM-dd HH:mm");
        }

        // Version type display name
        QString typeDisplay = type;
        if (type == "release") typeDisplay = tr("正式版");
        else if (type == "snapshot") typeDisplay = tr("快照");
        else if (type == "old_beta" || type == "old_alpha") typeDisplay = tr("远古版");
        else typeDisplay = tr("其他");

        m_versionTypeMap[id] = typeDisplay;
        m_versionRawType[id] = type;

        createVersionListCard(id, type, dateString, typeDisplay);
        if (m_viewMode == ContentViewSwitch::Masonry) {
            addMasonryVersionCard(id, dateString, typeDisplay);
            coverIds << id;
        }
    }

    if (!coverIds.isEmpty())
        requestVersionCovers(coverIds);

    if (m_versionList->count() > 0) {
        m_versionList->setCurrentRow(0);
    } else {
        m_loadingOverlay->showError(tr("没有找到符合条件的版本"));
    }
}

void InstallInstancePage::rebuildVersionCards()
{
    m_versionList->clear();
    populateVersionList();
}

void InstallInstancePage::createVersionListCard(const QString &id, const QString &type,
                                                  const QString &dateString, const QString &typeDisplay)
{
    QListWidgetItem *item = new QListWidgetItem(m_versionList);
    QWidget *cardWidget = new QWidget();
    cardWidget->setObjectName("modCardListItem");
    cardWidget->setProperty("cardRole", "container");
    cardWidget->setProperty("flatCard", true);
    cardWidget->setFixedHeight(76);
    cardWidget->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *mainLayout = new QHBoxLayout(cardWidget);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    // 去掉全局的灰底/渐变底，方块图直接透明展示（仅作用于本卡片图标）
    iconLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    // 版本类型方块图（透明底），资源缺失时回退字母占位
    const QPixmap typeIcon(iconPathForVersion(id, type));
    if (!typeIcon.isNull()) {
        iconLabel->setPixmap(typeIcon.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        QPixmap placeholderIcon(48, 48);
        QColor iconColor = (type == "release") ? QColor("#4CAF50") :
                           (type == "snapshot") ? QColor("#FF9800") : QColor("#9E9E9E");
        placeholderIcon.fill(iconColor.lighter(180));
        {
            QPainter painter(&placeholderIcon);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::white);
            QFont font;
            font.setBold(true);
            font.setPixelSize(24);
            painter.setFont(font);
            QString letter = type.isEmpty() ? "?" : type.left(1).toUpper();
            painter.drawText(placeholderIcon.rect(), Qt::AlignCenter, letter);
            painter.end();
        }
        iconLabel->setPixmap(placeholderIcon);
    }
    mainLayout->addWidget(iconLabel);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *nameLabel = new QLabel(id);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    infoLayout->addWidget(nameLabel);

    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    if (!dateString.isEmpty()) {
        QLabel *dateChip = new QLabel(dateString);
        dateChip->setObjectName("modChip");
        dateChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(dateChip);
    }

    QLabel *typeChip = new QLabel(typeDisplay);
    typeChip->setObjectName("modChip");
    typeChip->setProperty("cardRole", "chip");
    chipsLayout->addWidget(typeChip);

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

    QPushButton *serverBtn = createBtn(":/Images/Icons/server.svg", tr("下载服务端"));
    if (serverBtn->icon().isNull()) {
        serverBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/download.svg", themeColor, 18));
    }
    connect(serverBtn, &QPushButton::clicked, [this]() {
        NotificationManager::showInfo(this, tr("下载服务端功能将在后续版本中实现！"));
    });
    btnLayout->addWidget(serverBtn);

    QPushButton *installBtn = createBtn(":/Images/Icons/install.svg", tr("安装此版本"));
    connect(installBtn, &QPushButton::clicked, this, [this, id]() { selectVersion(id); });
    btnLayout->addWidget(installBtn);

    mainLayout->addLayout(btnLayout);

    item->setData(Qt::UserRole, id);
    m_versionList->setItemWidget(item, cardWidget);
    item->setSizeHint(QSize(0, 76));
}

void InstallInstancePage::onViewModeChanged(ContentViewSwitch::ViewMode mode)
{
    m_viewMode = mode;
    ContentViewSwitch::savePersisted("install_instance", mode);
    if (m_viewStack)
        m_viewStack->setCurrentIndex(mode == ContentViewSwitch::List ? 0 : 1);
    // 列表模式下不预建瀑布流卡片，切到瀑布流时按当前筛选重建
    if (mode == ContentViewSwitch::Masonry && !m_allVersions.isEmpty())
        populateVersionList();
}

void InstallInstancePage::selectVersion(const QString &versionId)
{
    if (versionId.isEmpty())
        return;
    if (m_modifyMode) {
        emit modifyVersionSelected(versionId, m_versionTypeMap.value(versionId),
                                   m_modifyInstancePath, m_modifyInstanceName);
    } else {
        emit versionSelected(versionId, m_versionTypeMap.value(versionId));
    }
}

/* 瀑布流版本卡片：渐变 banner + wiki 封面 + 名称/描述/chips + 安装按钮 */
void InstallInstancePage::addMasonryVersionCard(const QString &id, const QString &dateString,
                                                const QString &typeDisplay)
{
    ModInfo info;
    info.id = id;
    info.name = id;
    info.chineseName = id;
    info.source = m_currentSource;
    info.iconUrl = iconPathForVersion(id, m_versionRawType.value(id));
    info.description = dateString.isEmpty()
                           ? tr("Minecraft %1 版本").arg(typeDisplay)
                           : tr("Minecraft %1 版本，发布于 %2").arg(typeDisplay, dateString);
    // chips 复用展示字段承载「日期 / 类型」，与列表卡片信息保持一致
    if (!dateString.isEmpty())
        info.gameVersions = QStringList{dateString};
    info.categories = QStringList{typeDisplay};
    info.coverUrl = m_coverUrls.value(id);

    QList<MasonryContentCard::ActionSpec> actions;
    actions.append({QStringLiteral(":/Images/Icons/server.svg"), tr("下载服务端"), QColor(),
                    [this]() {
                        NotificationManager::showInfo(this, tr("下载服务端功能将在后续版本中实现！"));
                    }});
    actions.append({QStringLiteral(":/Images/Icons/install.svg"), tr("安装此版本"), QColor(),
                    [this, id]() { selectVersion(id); }});

    QWidget *card = MasonryContentCard::build(info, m_masonryContainer, actions);
    card->setProperty("versionId", id);
    card->installEventFilter(this);
    m_masonryCards.insert(id, card);
    m_masonryLayout->addWidget(card);
}

/* wiki 标题映射：官方清单 id → minecraft.wiki 页面标题 */
QString InstallInstancePage::wikiTitleFor(const QString &id, const QString &type)
{
    if (type == QLatin1String("old_beta") && id.startsWith(QLatin1Char('b')))
        return QStringLiteral("Java Edition Beta ") + id.mid(1);
    if (type == QLatin1String("old_alpha") && id.startsWith(QLatin1Char('a')))
        return QStringLiteral("Java Edition Alpha v") + id.mid(1);
    if (type == QLatin1String("old_alpha") && id.startsWith(QLatin1String("inf-")))
        return QStringLiteral("Java Edition Infdev ") + id.mid(4);
    return QStringLiteral("Java Edition ") + id;
}

/* 批量拉取版本封面：优先取 wiki 上的「<版本号> banner」官方主题图，缺失再回退页面首图 */
void InstallInstancePage::requestVersionCovers(const QStringList &versionIds)
{
    QStringList need;
    need.reserve(versionIds.size());
    for (const QString &id : versionIds) {
        if (!m_coverUrls.contains(id) && !m_coverMissing.contains(id))
            need << id;
    }
    if (need.isEmpty())
        return;

    // 分块串行请求：本块回包后再请求下一块，让列表头部的卡片先拿到封面
    requestCoverChunk(need, 0);
}

void InstallInstancePage::requestCoverChunk(const QStringList &need, int begin)
{
    if (begin < 0 || begin >= need.size())
        return;

    const QStringList chunk = need.mid(begin, kCoverChunkSize);

    // 阶段 1：官方主题图。wiki 各版本页普遍带 "<版本号> banner"（1170×500 宣传图），
    // 命名有 jpg/png 与大小写差异，一次把候选都问上
    static const char *const kBannerNames[] = {
        "banner.jpg", "banner.png", "Banner.png", "Banner.jpg",
    };
    QHash<QString, QString> fileToId;
    QStringList fileTitles;
    fileTitles.reserve(chunk.size() * 4);
    for (const QString &id : chunk) {
        for (const char *name : kBannerNames) {
            const QString title = QStringLiteral("File:%1 %2").arg(id, QLatin1String(name));
            fileToId.insert(title, id);
            fileTitles << title;
        }
    }

    QUrl url(QStringLiteral("https://minecraft.wiki/api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    query.addQueryItem(QStringLiteral("prop"), QStringLiteral("imageinfo"));
    query.addQueryItem(QStringLiteral("iiprop"), QStringLiteral("url"));
    query.addQueryItem(QStringLiteral("iiurlwidth"), QStringLiteral("800"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("formatversion"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("titles"), fileTitles.join(QLatin1Char('|')));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "BlockBox/1.0 (Minecraft version covers)");
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, fileToId, chunk, need, begin]() {
                reply->deleteLater();
                if (reply->error() == QNetworkReply::NoError) {
                    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
                    const QJsonArray pages =
                        root.value(QStringLiteral("query")).toObject()
                            .value(QStringLiteral("pages")).toArray();
                    for (const QJsonValue &value : pages) {
                        const QJsonObject page = value.toObject();
                        const QString id = fileToId.value(page.value(QStringLiteral("title")).toString());
                        if (id.isEmpty())
                            continue;
                        const QJsonArray infos = page.value(QStringLiteral("imageinfo")).toArray();
                        if (infos.isEmpty())
                            continue;
                        const QJsonObject info = infos.first().toObject();
                        const QString coverUrl =
                            info.value(QStringLiteral("thumburl")).toString().isEmpty()
                                ? info.value(QStringLiteral("url")).toString()
                                : info.value(QStringLiteral("thumburl")).toString();
                        if (coverUrl.isEmpty())
                            continue;
                        m_coverUrls.insert(id, coverUrl);
                        applyCoverToCard(id, coverUrl);
                    }
                }
                // 阶段 2：没有主题图的版本回退页面首图；单块失败不阻塞后续封面
                QStringList rest;
                for (const QString &id : chunk) {
                    if (!m_coverUrls.contains(id))
                        rest << id;
                }
                if (rest.isEmpty()) {
                    saveCoverCache();
                    requestCoverChunk(need, begin + chunk.size());
                } else {
                    requestFallbackCovers(rest, need, begin, chunk.size());
                }
            });
}

void InstallInstancePage::requestFallbackCovers(const QStringList &ids, const QStringList &need,
                                                int begin, int taken)
{
    QHash<QString, QString> titleToId;
    QStringList titles;
    titles.reserve(ids.size());
    for (const QString &id : ids) {
        const QString title = wikiTitleFor(id, m_versionRawType.value(id));
        titleToId.insert(title, id);
        titles << title;
    }

    QUrl url(QStringLiteral("https://minecraft.wiki/api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    query.addQueryItem(QStringLiteral("prop"), QStringLiteral("pageimages"));
    query.addQueryItem(QStringLiteral("piprop"), QStringLiteral("thumbnail"));
    query.addQueryItem(QStringLiteral("pithumbsize"), QStringLiteral("800"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("formatversion"), QStringLiteral("2"));
    query.addQueryItem(QStringLiteral("titles"), titles.join(QLatin1Char('|')));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "BlockBox/1.0 (Minecraft version covers)");
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, titleToId, need, begin, taken]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            const QJsonArray pages =
                root.value(QStringLiteral("query")).toObject().value(QStringLiteral("pages")).toArray();
            for (const QJsonValue &value : pages) {
                const QJsonObject page = value.toObject();
                const QString id = titleToId.value(page.value(QStringLiteral("title")).toString());
                if (id.isEmpty())
                    continue;
                const QString coverUrl = page.value(QStringLiteral("thumbnail")).toObject()
                                             .value(QStringLiteral("source")).toString();
                if (coverUrl.isEmpty()) {
                    // 页面不存在或页面无配图，记为已知缺失，避免每次重建重复请求
                    m_coverMissing.insert(id);
                    continue;
                }
                m_coverUrls.insert(id, coverUrl);
                applyCoverToCard(id, coverUrl);
            }
        }
        saveCoverCache();
        requestCoverChunk(need, begin + taken);
    });
}

void InstallInstancePage::loadCoverCache()
{
    m_coverCacheLoaded = true;
    QFile f(coverCacheFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject urls = root.value(QStringLiteral("urls")).toObject();
    for (auto it = urls.begin(); it != urls.end(); ++it) {
        const QString coverUrl = it.value().toString();
        if (!coverUrl.isEmpty())
            m_coverUrls.insert(it.key(), coverUrl);
    }
    for (const QJsonValue &value : root.value(QStringLiteral("missing")).toArray()) {
        const QString id = value.toString();
        if (!id.isEmpty())
            m_coverMissing.insert(id);
    }
}

void InstallInstancePage::saveCoverCache() const
{
    QJsonObject urls;
    for (auto it = m_coverUrls.constBegin(); it != m_coverUrls.constEnd(); ++it)
        urls.insert(it.key(), it.value());
    QJsonArray missing;
    for (const QString &id : m_coverMissing)
        missing.append(id);
    QJsonObject root;
    root.insert(QStringLiteral("urls"), urls);
    root.insert(QStringLiteral("missing"), missing);

    const QString path = coverCacheFile();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

void InstallInstancePage::applyCoverToCard(const QString &versionId, const QString &url)
{
    QWidget *card = m_masonryCards.value(versionId);
    if (!card)
        return;
    QLabel *banner = card->findChild<QLabel *>(QStringLiteral("contentCardBanner"));
    if (!banner)
        return;
    const QPixmap current = banner->pixmap();
    const int w = current.width() > 0 ? current.width() : kMasonryCardW - 2;
    const int h = current.height() > 0 ? current.height() : kMasonryBannerH;
    MasonryContentCard::loadCoverInto(banner, url, w, h);
}

/* 点击瀑布流卡片（或其空白处）→ 与安装按钮一致的版本选择行为 */
bool InstallInstancePage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QVariant versionId = watched->property("versionId");
        if (versionId.isValid() && mouseEvent->button() == Qt::LeftButton) {
            selectVersion(versionId.toString());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void InstallInstancePage::onModifyExistingInstanceClicked()
{
    emit modifyExistingInstanceRequested();
}

void InstallInstancePage::enterModifyMode(const QString &instancePath, const QString &instanceName,
                                          const QString &version, const QString &loader)
{
    m_modifyMode = true;
    m_modifyInstancePath = instancePath;
    m_modifyInstanceName = instanceName;
    m_modifyInstanceVersion = version;
    m_modifyInstanceLoader = loader;

    QString info = tr("正在修改现有实例：%1").arg(instanceName);
    if (!version.isEmpty())
        info += tr("　当前版本：%1").arg(version);
    if (!loader.isEmpty())
        info += tr("　加载器：%1").arg(loader);
    m_modifyBannerLabel->setText(info);
    m_modifyBanner->setVisible(true);
    m_modifyInstanceBtn->setText(tr("修改现有实例中..."));
    m_modifyInstanceBtn->setEnabled(false);
}

void InstallInstancePage::exitModifyMode()
{
    m_modifyMode = false;
    m_modifyInstancePath.clear();
    m_modifyInstanceName.clear();
    m_modifyInstanceVersion.clear();
    m_modifyInstanceLoader.clear();
    m_modifyBanner->setVisible(false);
    m_modifyInstanceBtn->setText(tr("修改现有实例"));
    m_modifyInstanceBtn->setEnabled(true);
}


