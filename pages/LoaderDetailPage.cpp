#include "LoaderDetailPage.h"
#include "utils/IconHelper.h"
#include "utils/LoaderCompatibility.h"
#include "utils/ThemeManager.h"
#include "utils/SettingsManager.h"
#include "components/NotificationManager.h"
#include "components/MasonryContentCard.h"
#include "utils/VersionDownloader.h"
#include "utils/forge/ForgeInstaller.h"
#include "utils/NeoForgeInstaller.h"
#include "utils/fabric/FabricInstaller.h"
#include "utils/optifine/OptiFineInstaller.h"
#include "utils/LiteLoaderInstaller.h"
#include "utils/CleanroomInstaller.h"
#include "utils/OptiFabricInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include "components/AppFileDialog.h"
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "components/AppMessageBox.h"
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPropertyAnimation>
#include <QtConcurrent>
#include <QTimer>
#include <QVersionNumber>

LoaderDetailPage::LoaderDetailPage(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_topBarWidget(nullptr)
    , m_topBarLayout(nullptr)
    , m_instanceNameEdit(nullptr)
    , m_installButton(nullptr)
    , m_pathWidget(nullptr)
    , m_pathLayout(nullptr)
    , m_pathComboBox(nullptr)
    , m_addFolderButton(nullptr)
    , m_sourceLabel(nullptr)
    , m_sourceComboBox(nullptr)
    , m_selectedLoadersWidget(nullptr)
    , m_selectedLoadersLayout(nullptr)
    , m_selectedLabel(nullptr)
    , m_selectedCardsContainer(nullptr)
    , m_selectedCardsLayout(nullptr)
    , m_loaderListWidget(nullptr)
    , m_messageBar(nullptr)
    , m_messageBarLayout(nullptr)
    , m_messageLabel(nullptr)
    , m_messageTimer(nullptr)
    , m_currentVersion("")
    , m_versionType("")
    , m_instancePath("")
    , m_modifyMode(false)
    , m_modifyInstancePath("")
    , m_modifyInstanceName("")
    , m_modifyInstanceVersion("")
    , m_modifyInstanceLoader("")
    , m_modifyInfoWidget(nullptr)
    , m_modifyInfoLayout(nullptr)
    , m_modifyInfoLabel(nullptr)
    , m_isInstalling(false)
{
    initUI();
    initLoaderCards();
}

LoaderDetailPage::~LoaderDetailPage()
{
}

void LoaderDetailPage::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(20, 20, 20, 20);
    m_mainLayout->setSpacing(10);

    m_topBarWidget = new QWidget(this);
    m_topBarLayout = new QHBoxLayout(m_topBarWidget);
    m_topBarLayout->setContentsMargins(0, 0, 0, 0);
    m_topBarLayout->setSpacing(15);

    QLabel *instanceNameLabel = new QLabel(tr("实例名称:"), m_topBarWidget);
    instanceNameLabel->setObjectName("loaderSectionLabel");
    m_topBarLayout->addWidget(instanceNameLabel);

    m_instanceNameEdit = new QLineEdit(m_topBarWidget);
    m_instanceNameEdit->setObjectName("loaderInstanceNameEdit");
    m_instanceNameEdit->setPlaceholderText(tr("请输入实例名称"));
    m_topBarLayout->addWidget(m_instanceNameEdit, 1);

    m_installButton = new QPushButton(tr("安装"), m_topBarWidget);
    m_installButton->setObjectName("installButton");
    m_installButton->setEnabled(false);
    m_topBarLayout->addWidget(m_installButton);

    m_mainLayout->addWidget(m_topBarWidget);

    m_pathWidget = new QWidget(this);
    m_pathLayout = new QHBoxLayout(m_pathWidget);
    m_pathLayout->setContentsMargins(0, 0, 0, 0);
    m_pathLayout->setSpacing(15);

    QLabel *pathTitleLabel = new QLabel(tr("安装位置:"), m_pathWidget);
    pathTitleLabel->setObjectName("loaderSectionLabel");
    m_pathLayout->addWidget(pathTitleLabel);

    m_pathComboBox = new QComboBox(m_pathWidget);
    m_pathComboBox->setObjectName("loaderPathCombo");
    m_pathComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pathLayout->addWidget(m_pathComboBox, 1);

    m_addFolderButton = new QPushButton(tr("+"), m_pathWidget);
    m_addFolderButton->setObjectName("loaderAddFolderBtn");
    m_pathLayout->addWidget(m_addFolderButton);

    m_sourceLabel = new QLabel(tr("下载源:"), m_pathWidget);
    m_sourceLabel->setObjectName("loaderSectionLabel");
    m_pathLayout->addWidget(m_sourceLabel);

    m_sourceComboBox = new QComboBox(m_pathWidget);
    m_sourceComboBox->setObjectName("loaderSourceCombo");
    m_sourceComboBox->addItem(tr("官方源"), "official");
    m_sourceComboBox->addItem(tr("BMCL源"), "bmcl");
    m_pathLayout->addWidget(m_sourceComboBox);

    m_mainLayout->addWidget(m_pathWidget);

    m_selectedLoadersWidget = new QWidget(this);
    m_selectedLoadersLayout = new QHBoxLayout(m_selectedLoadersWidget);
    m_selectedLoadersLayout->setContentsMargins(0, 0, 0, 0);
    m_selectedLoadersLayout->setSpacing(10);

    m_selectedLabel = new QLabel(tr("已选加载器:"), m_selectedLoadersWidget);
    m_selectedLabel->setObjectName("loaderSectionLabel");
    m_selectedLoadersLayout->addWidget(m_selectedLabel);

    m_selectedCardsContainer = new QWidget(m_selectedLoadersWidget);
    m_selectedCardsLayout = new QHBoxLayout(m_selectedCardsContainer);
    m_selectedCardsLayout->setContentsMargins(0, 0, 0, 0);
    m_selectedCardsLayout->setSpacing(10);
    m_selectedCardsLayout->addStretch();
    m_selectedLoadersLayout->addWidget(m_selectedCardsContainer);

    m_selectedLoadersWidget->setVisible(false);
    m_mainLayout->addWidget(m_selectedLoadersWidget);

    m_loaderListWidget = new QListWidget(this);
    m_loaderListWidget->setObjectName("loaderListWidget");
    m_loaderListWidget->setSelectionMode(QAbstractItemView::NoSelection);
    m_loaderListWidget->setAlternatingRowColors(false);
    m_loaderListWidget->setUniformItemSizes(true);
    m_loaderListWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_loaderListWidget->setMovement(QListView::Static);
    m_loaderListWidget->setSpacing(8);

    connect(m_loaderListWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
      QString loaderName = item->data(Qt::UserRole).toString();
      if (!loaderName.isEmpty()) {
        emit viewAllVersionsRequested(loaderName, m_currentVersion);
      }
    });

    m_mainLayout->addWidget(m_loaderListWidget);

    // 修改现有实例：底部当前实例信息提示条（默认隐藏）
    m_modifyInfoWidget = new QWidget(this);
    m_modifyInfoWidget->setObjectName("modifyInfoBar");
    m_modifyInfoLayout = new QHBoxLayout(m_modifyInfoWidget);
    m_modifyInfoLayout->setContentsMargins(15, 10, 15, 10);
    m_modifyInfoLayout->setSpacing(10);
    m_modifyInfoLabel = new QLabel(m_modifyInfoWidget);
    m_modifyInfoLabel->setObjectName("modifyInfoLabel");
    m_modifyInfoLabel->setWordWrap(true);
    m_modifyInfoLayout->addWidget(m_modifyInfoLabel);
    m_modifyInfoWidget->setVisible(false);
    m_mainLayout->addWidget(m_modifyInfoWidget);

    m_messageBar = new QWidget(this);
    m_messageBar->setObjectName("messageBar");
    m_messageBar->setFixedHeight(40);
    m_messageBar->setVisible(false);
    m_messageBarLayout = new QHBoxLayout(m_messageBar);
    m_messageBarLayout->setContentsMargins(15, 0, 15, 0);
    m_messageBarLayout->setSpacing(10);
    m_messageLabel = new QLabel(m_messageBar);
    m_messageLabel->setObjectName("loaderMessageLabel");
    m_messageBarLayout->addWidget(m_messageLabel);
    m_messageBarLayout->addStretch();
    m_mainLayout->addWidget(m_messageBar);

    m_messageTimer = new QTimer(this);
    m_messageTimer->setSingleShot(true);
    connect(m_messageTimer, &QTimer::timeout, this, [this]() {
        m_messageBar->setVisible(false);
    });

    connect(m_addFolderButton, &QPushButton::clicked, this, [this]() {
        QString dir = AppFileDialog::getExistingDirectory(this, tr("选择实例文件夹"),
            QDir::homePath(), AppFileDialog::ShowDirsOnly | AppFileDialog::DontResolveSymlinks);
        if (!dir.isEmpty()) {
            SettingsManager::instance()->addInstanceFolder(QFileInfo(dir).fileName(), dir, false);
            loadInstanceFolders();
        }
    });

    connect(m_installButton, &QPushButton::clicked, this, [this]() {
        QString instanceName = m_instanceNameEdit->text().trimmed();
        if (!instanceName.isEmpty() && !m_currentVersion.isEmpty()) {
            if (m_selectedLoaders.isEmpty()) {
                if (m_isInstalling) return;
                m_isInstalling = true;
                m_installButton->setEnabled(false);
                QString source = m_sourceComboBox->currentData().toString();
                VersionDownloader::DownloadSource downloadSource =
                    (source == "bmcl") ? VersionDownloader::BMCL : VersionDownloader::Official;
                VersionDownloader::instance()->setDownloadSource(downloadSource);
                QString targetPath = m_modifyMode ? m_modifyInstancePath
                                                   : m_pathComboBox->currentData().toString();
                disconnect(VersionDownloader::instance(), &VersionDownloader::downloadCompleted, this, nullptr);
                disconnect(VersionDownloader::instance(), &VersionDownloader::downloadFailed, this, nullptr);
                disconnect(VersionDownloader::instance(), &VersionDownloader::downloadCancelled, this, nullptr);
                disconnect(VersionDownloader::instance(), &VersionDownloader::statusChanged, this, nullptr);
                disconnect(VersionDownloader::instance(), &VersionDownloader::downloadProgressUpdated, this, nullptr);
                connect(VersionDownloader::instance(), &VersionDownloader::downloadCompleted,
                    this, [this, instanceName](const QString &, const QString &actualVersionPath) {
                        m_isInstalling = false;
                        m_installButton->setEnabled(true);
                        NotificationManager::showSuccess(this,
                            tr("Minecraft %1 已成功安装到\r\n%2").arg(m_currentVersion).arg(actualVersionPath));
                        if (m_modifyMode)
                            emit instanceModified(m_modifyInstancePath);
                        else
                            emit installRequested(instanceName, m_currentVersion, m_selectedLoaders);
                    });
                connect(VersionDownloader::instance(), &VersionDownloader::downloadFailed,
                    this, [this](const QString &error) {
                        m_isInstalling = false;
                        m_installButton->setEnabled(true);
                        NotificationManager::showError(this, error);
                    });
                connect(VersionDownloader::instance(), &VersionDownloader::downloadCancelled,
                    this, [this]() {
                        m_isInstalling = false;
                        m_installButton->setEnabled(true);
                    });
                VersionDownloader::instance()->downloadVanillaTo(m_currentVersion, targetPath, instanceName);
            } else {
                if (m_isInstalling) return;
                m_isInstalling = true;
                m_installButton->setEnabled(false);
                disconnect(VersionDownloader::instance(), &VersionDownloader::downloadCompleted, this, nullptr);
                disconnect(VersionDownloader::instance(), &VersionDownloader::downloadFailed, this, nullptr);
                disconnect(VersionDownloader::instance(), &VersionDownloader::downloadCancelled, this, nullptr);
                disconnect(VersionDownloader::instance(), &VersionDownloader::statusChanged, this, nullptr);
                disconnect(VersionDownloader::instance(), &VersionDownloader::downloadProgressUpdated, this, nullptr);
                connect(VersionDownloader::instance(), &VersionDownloader::downloadCompleted,
                    this, [this, instanceName](const QString &, const QString &actualVersionPath) {
                        QString mcVer = m_currentVersion;
                        QString instPath = actualVersionPath;
                        QMap<QString, SelectedLoader> ldrs = m_selectedLoaders;
                        QString instName = instanceName;
                        auto finishModify = [this]() {
                            m_isInstalling = false;
                            m_installButton->setEnabled(true);
                            emit instanceModified(m_modifyInstancePath);
                        };
                        if (ldrs.isEmpty()) {
                            m_isInstalling = false;
                            m_installButton->setEnabled(true);
                            NotificationManager::showSuccess(this,
                                tr("Minecraft %1 与 mod 加载器已成功安装到\r\n%2").arg(mcVer).arg(instPath));
                            if (m_modifyMode)
                                finishModify();
                            else
                                emit installRequested(instName, mcVer, m_selectedLoaders);
                            return;
                        }
                        int pendingCount = 0;
                        QStringList failedLoaders;
                        QObject cleanupGuard;
                        QString mainTaskId = VersionDownloader::instance()->currentTaskId();
                        ForgeInstaller::instance()->setCurrentTaskId(mainTaskId);
                        NeoForgeInstaller::instance()->setCurrentTaskId(mainTaskId);
                        FabricInstaller::instance()->setCurrentTaskId(mainTaskId);
                        OptiFineInstaller::instance()->setCurrentTaskId(mainTaskId);
                        LiteLoaderInstaller::instance()->setCurrentTaskId(mainTaskId);
                        CleanroomInstaller::instance()->setCurrentTaskId(mainTaskId);
                        OptiFabricInstaller::instance()->setCurrentTaskId(mainTaskId);
                        auto checkAllComplete = [this, &pendingCount, &failedLoaders, mcVer, instPath, instName]() {
                            if (pendingCount <= 0) {
                                m_isInstalling = false;
                                m_installButton->setEnabled(true);
                                if (failedLoaders.isEmpty()) {
                                    NotificationManager::showSuccess(this,
                                        tr("Minecraft %1 与 mod 加载器已成功安装到\r\n%2").arg(mcVer).arg(instPath));
                                } else {
                                    NotificationManager::showError(this,
                                        tr("Minecraft %1 安装成功，但以下加载器安装失败：\r\n%2")
                                            .arg(mcVer).arg(failedLoaders.join("\r\n")));
                                }
                                if (m_modifyMode)
                                    emit instanceModified(m_modifyInstancePath);
                                else
                                    emit installRequested(instName, mcVer, m_selectedLoaders);
                            }
                        };
                        bool hasOptiFine = ldrs.contains("OptiFine");
                        for (auto it = ldrs.constBegin(); it != ldrs.constEnd(); ++it) {
                            const QString &name = it.key();
                            const SelectedLoader &loader = it.value();
                            QString lowerName = name.toLower();
                            if (lowerName == "optifine") continue;
                            pendingCount++;
                            if (lowerName == "forge") {
                                auto onSuccess = [this, &pendingCount, &checkAllComplete](const QString &) {
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                auto onFailed = [this, &pendingCount, name, &failedLoaders, &checkAllComplete](const QString &, const QString &error) {
                                    failedLoaders.append(name + ": " + error);
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                connect(ForgeInstaller::instance(), &ForgeInstaller::installCompleted,
                                        &cleanupGuard, onSuccess);
                                connect(ForgeInstaller::instance(), &ForgeInstaller::installFailed,
                                        &cleanupGuard, onFailed);
                                ForgeInstaller::instance()->setVersionIsolationEnabled(
                                    SettingsManager::instance()->isVersionIsolationEnabled());
                                ForgeInstaller::instance()->downloadForgeInstaller(mcVer, loader.version, instPath, instName);
                            } else if (lowerName == "neoforge") {
                                auto onSuccess = [this, &pendingCount, &checkAllComplete]() {
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                auto onFailed = [this, &pendingCount, name, &failedLoaders, &checkAllComplete](const QString &error) {
                                    failedLoaders.append(name + ": " + error);
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                NeoForgeInstaller::instance()->setDownloadSource(
                                    SettingsManager::instance()->getNeoForgeDownloadSource());
                                connect(NeoForgeInstaller::instance(), &NeoForgeInstaller::installCompleted,
                                        &cleanupGuard, onSuccess);
                                connect(NeoForgeInstaller::instance(), &NeoForgeInstaller::installFailed,
                                        &cleanupGuard, onFailed);
                                NeoForgeInstaller::instance()->downloadNeoForgeInstaller(mcVer, loader.version, instPath, instName);
                            } else if (lowerName == "fabric" || lowerName == "quilt" || lowerName == "legacyfabric") {
                                auto onSuccess = [this, &pendingCount, &checkAllComplete]() {
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                auto onFailed = [this, &pendingCount, name, &failedLoaders, &checkAllComplete](const QString &error) {
                                    failedLoaders.append(name + ": " + error);
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                connect(FabricInstaller::instance(), &FabricInstaller::installCompleted,
                                        &cleanupGuard, onSuccess);
                                connect(FabricInstaller::instance(), &FabricInstaller::installFailed,
                                        &cleanupGuard, onFailed);
                                FabricInstaller::instance()->downloadFabricInstaller(mcVer, loader.version, instPath);
                            } else if (lowerName == "liteloader") {
                                auto onSuccess = [this, &pendingCount, &checkAllComplete]() {
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                auto onFailed = [this, &pendingCount, name, &failedLoaders, &checkAllComplete](const QString &error) {
                                    failedLoaders.append(name + ": " + error);
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                connect(LiteLoaderInstaller::instance(), &LiteLoaderInstaller::installCompleted,
                                        &cleanupGuard, onSuccess);
                                connect(LiteLoaderInstaller::instance(), &LiteLoaderInstaller::installFailed,
                                        &cleanupGuard, onFailed);
                                LiteLoaderInstaller::instance()->downloadLiteLoader(mcVer, loader.version, instPath);
                            } else if (lowerName == "cleanroom") {
                                auto onSuccess = [this, &pendingCount, &checkAllComplete]() {
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                auto onFailed = [this, &pendingCount, name, &failedLoaders, &checkAllComplete](const QString &error) {
                                    failedLoaders.append(name + ": " + error);
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                connect(CleanroomInstaller::instance(), &CleanroomInstaller::installCompleted,
                                        &cleanupGuard, onSuccess);
                                connect(CleanroomInstaller::instance(), &CleanroomInstaller::installFailed,
                                        &cleanupGuard, onFailed);
                                CleanroomInstaller::instance()->downloadCleanroom(mcVer, loader.version, loader.fileName, instPath);
                            } else if (lowerName == "optifabric") {
                                auto onSuccess = [this, &pendingCount, &checkAllComplete]() {
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                auto onFailed = [this, &pendingCount, name, &failedLoaders, &checkAllComplete](const QString &error) {
                                    failedLoaders.append(name + ": " + error);
                                    pendingCount--;
                                    checkAllComplete();
                                };
                                connect(OptiFabricInstaller::instance(), &OptiFabricInstaller::installCompleted,
                                        &cleanupGuard, onSuccess);
                                connect(OptiFabricInstaller::instance(), &OptiFabricInstaller::installFailed,
                                        &cleanupGuard, onFailed);
                                OptiFabricInstaller::instance()->downloadOptiFabric(mcVer, loader.version, loader.fileName, instPath);
                            }
                        }
                        if (hasOptiFine) {
                            pendingCount++;
                            SelectedLoader optiFineLoader = ldrs["OptiFine"];
                            auto onSuccess = [this, &pendingCount, &checkAllComplete]() {
                                pendingCount--;
                                checkAllComplete();
                            };
                            auto onFailed = [this, &pendingCount, &failedLoaders, &checkAllComplete](const QString &error) {
                                failedLoaders.append("OptiFine: " + error);
                                pendingCount--;
                                checkAllComplete();
                            };
                            OptiFineInstaller::instance()->setDownloadSource(
                                SettingsManager::instance()->getOptiFineDownloadSource());
                            connect(OptiFineInstaller::instance(), &OptiFineInstaller::installCompleted,
                                    &cleanupGuard, onSuccess);
                            connect(OptiFineInstaller::instance(), &OptiFineInstaller::installFailed,
                                    &cleanupGuard, onFailed);
                            OptiFineInstaller::instance()->downloadOptiFineInstaller(mcVer, optiFineLoader.version, optiFineLoader.fileName, instPath);
                        }
                        if (pendingCount == 0) {
                            checkAllComplete();
                        }
                    });
                connect(VersionDownloader::instance(), &VersionDownloader::downloadFailed,
                    this, [this](const QString &error) {
                        m_isInstalling = false;
                        m_installButton->setEnabled(true);
                        NotificationManager::showError(this, error);
                    });
                connect(VersionDownloader::instance(), &VersionDownloader::downloadCancelled,
                    this, [this]() {
                        m_isInstalling = false;
                        m_installButton->setEnabled(true);
                    });
                QString instPath = m_modifyMode ? m_modifyInstancePath
                                                 : m_pathComboBox->currentData().toString();
                QString instName = instanceName;
                QStringList loaderList;
                for (auto it = m_selectedLoaders.constBegin(); it != m_selectedLoaders.constEnd(); ++it) {
                    loaderList << QString("%1 %2").arg(it.value().name, it.value().version);
                }
                VersionDownloader::instance()->downloadVanillaTo(m_currentVersion, instPath, instName, loaderList);
            }
        }
    });
}

void LoaderDetailPage::initLoaderCards()
{
    struct LoaderInfo {
        QString name;
        QString logoPath;
        QString description;
    };
    QList<LoaderInfo> loaders = {
        {"Forge", ":/Images/Loader/Forge.png", tr("最流行的模组加载器，支持大量模组")},
        {"Fabric", ":/Images/Loader/Fabric.png", tr("轻量级模组加载器，性能更好")},
        {"Quilt", ":/Images/Loader/Quilt.png", tr("Fabric的分支，提供更多功能")},
        {"NeoForge", ":/Images/Loader/NeoForge.png", tr("Forge的继承者，支持现代Minecraft")},
        {"LiteLoader", ":/Images/Loader/Liteloader.ico", tr("轻量级模组加载器，专注于客户端修改")},
        {"OptiFabric", ":/Images/Loader/OptiFabric.png", tr("Optifine与Fabric的兼容层")},
        {"LabyMod", ":/Images/Loader/LabyMod.png", tr("客户端修改模组，提供更多功能")},
        {"Cleanroom", ":/Images/Loader/Cleanroom.png", tr("干净的模组开发环境")},
        {"LegacyFabric", ":/Images/Loader/legacyfabric.png", tr("为旧版本Minecraft提供Fabric支持")},
        {"OptiFine", ":/Images/Loader/OptiFine.png", tr("优化Minecraft渲染的模组加载器")}
    };
    for (const LoaderInfo &loader : loaders) {
        createLoaderListCard(loader.name, loader.logoPath, loader.description);
    }
}

void LoaderDetailPage::createLoaderListCard(const QString &name, const QString &logoPath,
                                              const QString &description)
{
    Q_UNUSED(description);
    QListWidgetItem *item = new QListWidgetItem(m_loaderListWidget);
    item->setData(Qt::UserRole, name);
    QWidget *cardWidget = new QWidget();
    cardWidget->setObjectName("modCardListItem");
    cardWidget->setProperty("cardRole", "container");
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
    QPixmap loaderIcon(logoPath);
    if (loaderIcon.isNull()) {
        QColor c1 = (name == "Forge") ? QColor("#6FCF97") :
                    (name == "Fabric") ? QColor("#FFB74D") :
                    (name == "Quilt") ? QColor("#CE93D8") :
                    (name == "NeoForge") ? QColor("#F06292") : QColor("#BDBDBD");
        QColor c2 = (name == "Forge") ? QColor("#2E7D32") :
                    (name == "Fabric") ? QColor("#F57C00") :
                    (name == "Quilt") ? QColor("#7B1FA2") :
                    (name == "NeoForge") ? QColor("#C2185B") : QColor("#757575");
        QString letter = name.isEmpty() ? "?" : name.left(1).toUpper();
        loaderIcon = MasonryContentCard::makeLogoPixmap(c1, c2, letter)
                         .scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        loaderIcon = MasonryContentCard::makeRoundIcon(loaderIcon, 48);
    }
    iconLabel->setPixmap(loaderIcon);
    mainLayout->addWidget(iconLabel);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *nameLabel = new QLabel(name);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    infoLayout->addWidget(nameLabel);

    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    QLabel *mcChip = new QLabel(tr("MC: ?"));
    mcChip->setObjectName("modChip");
    mcChip->setProperty("cardRole", "chip");
    chipsLayout->addWidget(mcChip);

    QLabel *latestChip = new QLabel(tr("未知"));
    latestChip->setObjectName("modChip");
    latestChip->setProperty("cardRole", "chip");
    chipsLayout->addWidget(latestChip);

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

    QPushButton *installBtn = createBtn(":/Images/Icons/install.svg", tr("添加最新加载器"));
    connect(installBtn, &QPushButton::clicked, [this, name]() {
        emit viewAllVersionsRequested(name, m_currentVersion);
    });
    btnLayout->addWidget(installBtn);

    QPushButton *viewAllBtn = createBtn(":/Images/Icons/list_view.svg", tr("所有版本"));
    connect(viewAllBtn, &QPushButton::clicked, [this, name]() {
        emit viewAllVersionsRequested(name, m_currentVersion);
    });
    btnLayout->addWidget(viewAllBtn);

    mainLayout->addLayout(btnLayout);

    m_loaderListWidget->setItemWidget(item, cardWidget);
    item->setSizeHint(QSize(0, 76));
}

void LoaderDetailPage::rebuildLoaderCards()
{
    m_loaderListWidget->clear();
    initLoaderCards();
}

void LoaderDetailPage::setVersionInfo(const QString &versionId)
{
    m_currentVersion = versionId;
    updateInstanceName();
    updateSelectedLoadersDisplay();
}

void LoaderDetailPage::setVersionType(const QString &versionType)
{
    m_versionType = versionType;
    updateInstanceName();
}

void LoaderDetailPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    loadInstanceFolders();
    updateSelectedLoadersDisplay();
}

void LoaderDetailPage::loadInstanceFolders()
{
    m_pathComboBox->clear();
    QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    int defaultIndex = 0;
    for (int i = 0; i < folders.size(); ++i) {
        const InstanceFolderInfo &folder = folders[i];
        m_pathComboBox->addItem(folder.name, folder.path);
        if (folder.isDefault) {
            defaultIndex = i;
        }
    }
    if (!folders.isEmpty()) {
        m_pathComboBox->setCurrentIndex(defaultIndex);
        m_instancePath = folders[defaultIndex].path;
    } else {
        QString appDir = QCoreApplication::applicationDirPath();
        m_instancePath = appDir + "/.minecraft";
        m_pathComboBox->addItem(tr("默认实例文件夹"), m_instancePath);
        m_pathComboBox->setCurrentIndex(0);
    }
}

void LoaderDetailPage::addSelectedLoader(const QString &loaderName, const QString &loaderVersion,
                                          const QString &fileName)
{
    tryAddLoaderWithCompatibility(loaderName, loaderVersion, fileName);
}

void LoaderDetailPage::removeSelectedLoader(const QString &loaderName)
{
    m_selectedLoaders.remove(loaderName);
    updateSelectedLoadersDisplay();
}

void LoaderDetailPage::showMessage(const QString &message, bool isError)
{
    if (m_messageTimer->isActive()) {
        m_messageTimer->stop();
    }
    m_messageLabel->setText(message);
    if (isError) {
        m_messageBar->setProperty("msgType", "error");
        m_messageBar->style()->unpolish(m_messageBar);
        m_messageBar->style()->polish(m_messageBar);
        m_messageLabel->setProperty("msgType", "error");
        m_messageLabel->style()->unpolish(m_messageLabel);
        m_messageLabel->style()->polish(m_messageLabel);
    } else {
        m_messageBar->setProperty("msgType", "warning");
        m_messageBar->style()->unpolish(m_messageBar);
        m_messageBar->style()->polish(m_messageBar);
        m_messageLabel->setProperty("msgType", "warning");
        m_messageLabel->style()->unpolish(m_messageLabel);
        m_messageLabel->style()->polish(m_messageLabel);
    }
    m_messageBar->setVisible(true);
    m_messageTimer->start(5000);
}

void LoaderDetailPage::enterModifyMode(const QString &instancePath, const QString &instanceName,
                                       const QString &version, const QString &loader)
{
    m_modifyMode = true;
    m_modifyInstancePath = instancePath;
    m_modifyInstanceName = instanceName;
    m_modifyInstanceVersion = version;
    m_modifyInstanceLoader = loader;

    m_selectedLoaders.clear();
    updateSelectedLoadersDisplay();

    m_instanceNameEdit->setText(instanceName);
    m_instanceNameEdit->setEnabled(false);
    m_instanceNameEdit->setToolTip(tr("修改现有实例时实例名称保持不变"));

    QString info = tr("正在修改现有实例：%1").arg(instanceName);
    if (!version.isEmpty())
        info += tr("　　当前版本：%1").arg(version);
    if (!loader.isEmpty())
        info += tr("　　加载器：%1").arg(loader);
    m_modifyInfoLabel->setText(info);
    m_modifyInfoWidget->setVisible(true);

    m_installButton->setText(tr("修改此实例"));
}

void LoaderDetailPage::exitModifyMode()
{
    m_modifyMode = false;
    m_modifyInstancePath.clear();
    m_modifyInstanceName.clear();
    m_modifyInstanceVersion.clear();
    m_modifyInstanceLoader.clear();

    m_instanceNameEdit->setEnabled(true);
    m_modifyInfoWidget->setVisible(false);
    m_installButton->setText(tr("安装"));
    updateInstanceName();
}

bool LoaderDetailPage::checkLoaderCompatibility(const QString &loaderName)
{
    QStringList installedLoaders = getInstalledLoaderNames();
    LoaderCompatibilityResult result = LoaderCompatibility::instance()->checkCompatibility(loaderName, installedLoaders);
    if (result.hasConflict) {
        showMessage(tr("%1 与已安装的 %2 不兼容，无法同时安装").arg(loaderName).arg(result.conflictLoaderName), true);
        return false;
    }
    return true;
}

void LoaderDetailPage::tryAddLoaderWithCompatibility(const QString &loaderName, const QString &loaderVersion,
                                                      const QString &fileName)
{
    QStringList installedLoaders = getInstalledLoaderNames();
    LoaderCompatibilityResult result = LoaderCompatibility::instance()->checkCompatibility(loaderName, installedLoaders);
    if (result.hasConflict) {
        showMessage(tr("%1 与已安装的 %2 不兼容，无法同时安装").arg(loaderName).arg(result.conflictLoaderName), true);
        return;
    }
    if (result.needsDependency) {
        QString depLoaderName = result.dependencyLoaderName;
        SelectedLoader depLoader;
        depLoader.name = depLoaderName;
        depLoader.version = tr("自动选择");
        depLoader.fileName = QString();
        m_selectedLoaders[depLoaderName] = depLoader;
        SelectedLoader loader;
        loader.name = loaderName;
        loader.version = loaderVersion;
        loader.fileName = fileName;
        m_selectedLoaders[loaderName] = loader;
        updateSelectedLoadersDisplay();
        showMessage(tr("已自动添加 %1 并添加 %2 %3").arg(depLoaderName).arg(loaderName).arg(loaderVersion), false);
    } else {
        SelectedLoader loader;
        loader.name = loaderName;
        loader.version = loaderVersion;
        loader.fileName = fileName;
        m_selectedLoaders[loaderName] = loader;
        updateSelectedLoadersDisplay();
        showMessage(tr("已添加 %1 %2").arg(loaderName).arg(loaderVersion), false);
    }
}

QStringList LoaderDetailPage::getInstalledLoaderNames() const
{
    QStringList names;
    QMapIterator<QString, SelectedLoader> it(m_selectedLoaders);
    while (it.hasNext()) {
        it.next();
        names.append(it.value().name);
    }
    return names;
}

void LoaderDetailPage::updateSelectedLoadersDisplay()
{
    while (m_selectedCardsLayout->count() > 1) {
        QLayoutItem *item = m_selectedCardsLayout->takeAt(0);
        if (item && item->widget()) {
            delete item->widget();
        }
        delete item;
    }
    if (m_selectedLoaders.isEmpty()) {
        m_selectedLoadersWidget->setVisible(false);
        m_installButton->setEnabled(!m_currentVersion.isEmpty());
        updateInstanceName();
        return;
    }
    m_selectedLoadersWidget->setVisible(true);
    m_installButton->setEnabled(true);
    QMapIterator<QString, SelectedLoader> it(m_selectedLoaders);
    while (it.hasNext()) {
        it.next();
        const SelectedLoader &loader = it.value();
        QWidget *card = createSelectedLoaderCard(loader.name, loader.version);
        m_selectedCardsLayout->insertWidget(m_selectedCardsLayout->count() - 1, card);
    }
    updateInstanceName();
}

QWidget* LoaderDetailPage::createSelectedLoaderCard(const QString &loaderName, const QString &loaderVersion)
{
    QWidget *card = new QWidget();
    card->setObjectName("selectedLoaderCard");
    card->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *layout = new QHBoxLayout(card);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(8);
    QLabel *nameLabel = new QLabel(loaderName);
    nameLabel->setObjectName("loaderSelectedCardName");
    layout->addWidget(nameLabel);
    QLabel *versionLabel = new QLabel(loaderVersion);
    versionLabel->setObjectName("loaderSelectedCardVersion");
    layout->addWidget(versionLabel);
    QLabel *closeLabel = new QLabel(QStringLiteral("\u00D7"));
    closeLabel->setObjectName("loaderSelectedCardClose");
    layout->addWidget(closeLabel);
    card->installEventFilter(this);
    return card;
}

bool LoaderDetailPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget *widget = qobject_cast<QWidget*>(watched);
        if (widget && widget->objectName() == "selectedLoaderCard") {
            QHBoxLayout *layout = qobject_cast<QHBoxLayout*>(widget->layout());
            if (layout) {
                QLabel *nameLabel = qobject_cast<QLabel*>(layout->itemAt(0)->widget());
                if (nameLabel) {
                    QString loaderName = nameLabel->text();
                    removeSelectedLoader(loaderName);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LoaderDetailPage::updateInstanceName()
{
    if (m_modifyMode) {
        m_instanceNameEdit->setText(m_modifyInstanceName);
        return;
    }
    QStringList parts;
    if (!m_versionType.isEmpty()) {
        parts << m_versionType;
    }
    if (!m_currentVersion.isEmpty()) {
        parts << m_currentVersion;
    }
    QMapIterator<QString, SelectedLoader> it(m_selectedLoaders);
    while (it.hasNext()) {
        it.next();
        const SelectedLoader &loader = it.value();
        parts << QString("%1-%2").arg(loader.name).arg(loader.version);
    }
    QString instanceName = parts.join("-");
    m_instanceNameEdit->setText(instanceName);
}
