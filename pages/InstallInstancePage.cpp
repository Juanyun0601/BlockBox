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
#include <QEventLoop>
#include <QPainter>

#include <QRegularExpression>

#include "components/BlurLoadingOverlay.h"
#include "components/NotificationManager.h"
#include <QResizeEvent>
#include <QTimer>
#include <QUrl>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

InstallInstancePage::InstallInstancePage(QWidget *parent)
    : QWidget(parent)
    , m_modifyBanner(nullptr)
    , m_modifyBannerLayout(nullptr)
    , m_modifyBannerLabel(nullptr)
    , m_modifyInstanceBtn(nullptr)
    , m_exitModifyBtn(nullptr)
    , m_modifyMode(false)
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
    m_mainLayout->addWidget(m_versionList);

    // Connect signals
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstallInstancePage::onSourceChanged);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstallInstancePage::onVersionTypeFilterChanged);
    connect(m_versionList, &QListWidget::itemClicked, this, [=](QListWidgetItem *item) {
        QString versionId = item->data(Qt::UserRole).toString();
        if (!versionId.isEmpty()) {
            if (m_modifyMode) {
                emit modifyVersionSelected(versionId, m_versionTypeMap.value(versionId),
                                           m_modifyInstancePath, m_modifyInstanceName);
            } else {
                emit versionSelected(versionId, m_versionTypeMap.value(versionId));
            }
        }
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
            bool isAprilFoolsVersion = false;
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
                isAprilFoolsVersion = true;
            }
            QStringList aprilFools = {
                "25w14craftmine", "24w14potato", "23w13a or b", "22w13oneblockatatime",
                "20w14infinite", "3D Shareware v1.34", "1.RV-Pre1", "15w14a",
                "2.0 blue", "2.0 red", "2.0 purple"
            };
            if (aprilFools.contains(id)) {
                isAprilFoolsVersion = true;
            }
            if (id.length() >= 6 && id[2] == 'w') {
                int week = id.mid(3, 2).toInt();
                if (week == 13 || week == 14) {
                    if (id.length() > 5) {
                        QString afterWeek = id.mid(5);
                        if (afterWeek.length() > 1 || !afterWeek.at(0).isLetter()) {
                            isAprilFoolsVersion = true;
                        }
                    }
                }
            }
            if (!isAprilFoolsVersion) {
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

        createVersionListCard(id, type, dateString, typeDisplay);
    }

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
    connect(installBtn, &QPushButton::clicked, [this, id]() {
        if (m_modifyMode) {
            emit modifyVersionSelected(id, m_versionTypeMap.value(id),
                                       m_modifyInstancePath, m_modifyInstanceName);
        } else {
            emit versionSelected(id, m_versionTypeMap.value(id));
        }
    });
    btnLayout->addWidget(installBtn);

    mainLayout->addLayout(btnLayout);

    item->setData(Qt::UserRole, id);
    m_versionList->setItemWidget(item, cardWidget);
    item->setSizeHint(QSize(0, 76));
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


