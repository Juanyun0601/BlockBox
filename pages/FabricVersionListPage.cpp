/**
 * @file   FabricVersionListPage.cpp
 * @brief  Fabric版本列表页面实现
 * @author BlockBox Team
 * @date   2026-05-28
 */
#include "FabricVersionListPage.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include "components/AppMessageBox.h"
#include <QTimer>
#include <QtConcurrent>
#include <QUrlQuery>

#include "components/BlurLoadingOverlay.h"
#include "components/NotificationManager.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/fabric/FabricInstaller.h"

FabricVersionListPage::FabricVersionListPage(QWidget *parent)
    : QWidget(parent)
    , m_networkManager(nullptr)
    , m_currentReply(nullptr)
    , m_hasSelection(false)
    , m_installButton(nullptr)
    , m_cancelButton(nullptr)
    , m_loadingOverlay(nullptr)
    , m_statusInfoLabel(nullptr)
    , m_installProgressBar(nullptr)
    , m_isInstalling(false)
{
    initUI();
    m_networkManager = new QNetworkAccessManager(this);
}

FabricVersionListPage::~FabricVersionListPage()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }
}

void FabricVersionListPage::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(20, 20, 20, 20);
    m_mainLayout->setSpacing(15);

    m_titleLabel = new QLabel(tr("选择Fabric版本"));
    m_titleLabel->setObjectName("fabricPageTitle");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_titleLabel);

    m_statusLabel = new QLabel(tr("请先选择Minecraft版本"));
    m_statusLabel->setObjectName("fabricStatusLabel");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_statusLabel);

    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();

    m_versionList = new QTreeWidget(this);
    m_versionList->setObjectName("fabricVersionList");
    m_versionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_versionList->setHeaderHidden(true);
    m_versionList->setColumnCount(1);
    m_versionList->header()->setStretchLastSection(true);
    m_versionList->setUniformRowHeights(true);
    m_versionList->setMinimumHeight(400);
    m_mainLayout->addWidget(m_versionList);

    connect(m_versionList, &QTreeWidget::itemClicked, this, &FabricVersionListPage::onVersionItemClicked);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_installButton = new QPushButton(tr("安装选中版本"), this);
    m_installButton->setObjectName("installButton");
    m_installButton->setEnabled(false);

    m_cancelButton = new QPushButton(tr("取消"), this);
    m_cancelButton->setObjectName("cancelButton");
    m_cancelButton->setEnabled(false);

    buttonLayout->addWidget(m_installButton);
    buttonLayout->addWidget(m_cancelButton);
    m_mainLayout->addLayout(buttonLayout);

    m_installProgressBar = new QProgressBar(this);
    m_installProgressBar->setObjectName("installProgressBar");
    m_installProgressBar->setVisible(false);
    m_installProgressBar->setTextVisible(true);
    m_mainLayout->addWidget(m_installProgressBar);

    m_statusInfoLabel = new QLabel("", this);
    m_statusInfoLabel->setObjectName("fabricStatusInfo");
    m_statusInfoLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_statusInfoLabel);

    connect(m_installButton, &QPushButton::clicked, this, &FabricVersionListPage::onInstallClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        FabricInstaller::instance()->cancelInstall();
    });

    FabricInstaller *fabricInstaller = FabricInstaller::instance();
    connect(fabricInstaller, &FabricInstaller::downloadProgressUpdated,
            this, &FabricVersionListPage::onDownloadProgressUpdated);
    connect(fabricInstaller, &FabricInstaller::installProgressUpdated,
            this, &FabricVersionListPage::onInstallProgress);
    connect(fabricInstaller, &FabricInstaller::installCompleted,
            this, &FabricVersionListPage::onInstallCompleted);
    connect(fabricInstaller, &FabricInstaller::installFailed,
            this, &FabricVersionListPage::onInstallFailed);
    connect(fabricInstaller, &FabricInstaller::statusChanged,
            this, &FabricVersionListPage::onStatusChanged);
    connect(fabricInstaller, &FabricInstaller::versionListFetched,
            this, &FabricVersionListPage::onVersionListFetched);
    connect(fabricInstaller, &FabricInstaller::versionListFetchFailed,
            this, &FabricVersionListPage::onVersionListFetchFailed);
    connect(fabricInstaller, &FabricInstaller::installCancelled, this, [this]() {
        m_isInstalling = false;
        updateInstallButtonState(true, tr("安装选中版本"));
        m_cancelButton->setEnabled(false);
        m_statusInfoLabel->setText(tr("安装已取消"));
        m_statusInfoLabel->setProperty("fabricStatus", "normal");
        m_statusInfoLabel->style()->unpolish(m_statusInfoLabel);
        m_statusInfoLabel->style()->polish(m_statusInfoLabel);
    });
}

void FabricVersionListPage::setMinecraftVersion(const QString &mcVersion)
{
    m_minecraftVersion = mcVersion;
    m_titleLabel->setText(tr("选择Fabric版本 - Minecraft %1").arg(mcVersion));
    m_hasSelection = false;
    m_fabricVersions.clear();
    m_versionList->clear();
    loadFabricVersions();
}

void FabricVersionListPage::loadFabricVersions()
{
    if (m_minecraftVersion.isEmpty()) {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取Fabric版本列表..."));
    m_versionList->clear();
    m_fabricVersions.clear();

    SettingsManager *settings = SettingsManager::instance();
    FabricDownloadSource source = settings->getFabricDownloadSource();
    FabricInstaller::instance()->setDownloadSource(source);

    QString baseUrl = FabricInstaller::instance()->getBaseUrl();
    QString url = QString("%1/v2/versions/loader/%2").arg(baseUrl, m_minecraftVersion);

    QUrl requestUrl(url);
    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox Launcher");

    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    QNetworkReply* reply = m_networkManager->get(request);
    m_currentReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);

            if (doc.isArray()) {
                QJsonArray versionsArray = doc.array();
                m_fabricVersions.clear();

                for (const QJsonValue &value : versionsArray) {
                    QJsonObject versionObj = value.toObject();
                    QJsonObject loaderObj = versionObj["loader"].toObject();
                    QJsonObject launcherMeta = versionObj["launcherMeta"].toObject();

                    FabricVersionInfo info;
                    info.fabricVersion = loaderObj["version"].toString();
                    info.minecraftVersion = versionObj["game"].toString();
                    info.hash = loaderObj["hash"].toString();
                    info.build = loaderObj["build"].toString();

                    QJsonObject versionInfoObj = launcherMeta["version"].toObject();
                    info.launcherMetaVersion = versionInfoObj["version"].toString();

                    if (!info.fabricVersion.isEmpty() && !info.minecraftVersion.isEmpty()) {
                        m_fabricVersions.append(info);
                    }
                }

                populateVersionList();
                m_loadingOverlay->hideOverlay();
                m_statusLabel->setText(tr("共找到 %1 个Fabric版本").arg(m_fabricVersions.size()));
            } else {
                QString errorMsg = tr("获取版本列表失败: 数据格式错误");
                m_statusLabel->setText(errorMsg);
                m_loadingOverlay->showError(errorMsg);
            }
        } else {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMsg;
            if (statusCode == 404) {
                errorMsg = tr("此版本无法使用Fabric");
            } else {
                errorMsg = tr("获取版本列表失败: %1").arg(reply->errorString());
            }
            m_statusLabel->setText(errorMsg);
            m_loadingOverlay->showError(errorMsg);
        }

        if (m_currentReply == reply) {
            m_currentReply = nullptr;
        }
        reply->deleteLater();
    });
}

void FabricVersionListPage::populateVersionList()
{
    m_versionList->clear();

    for (const FabricVersionInfo &info : m_fabricVersions) {
        QString displayText = QString(
            "<div style='font-size:12pt; font-weight:bold;'>Fabric %1</div>"
            "<div style='font-size:9pt; color:#666666;'>Minecraft: %2</div>"
        ).arg(info.fabricVersion).arg(info.minecraftVersion);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.fabricVersion));
        m_versionList->addTopLevelItem(item);

        QLabel *label = new QLabel(displayText);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMargin(10);
        label->setMinimumHeight(60);
        m_versionList->setItemWidget(item, 0, label);
    }

    if (m_versionList->topLevelItemCount() > 0) {
        m_versionList->setCurrentItem(m_versionList->topLevelItem(0));
    }
}

void FabricVersionListPage::onVersionItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    QString fabricVersion = item->data(0, Qt::UserRole).toString();

    for (const FabricVersionInfo &info : m_fabricVersions) {
        if (info.fabricVersion == fabricVersion) {
            m_selectedVersion = info;
            m_hasSelection = true;
            break;
        }
    }

    if (m_hasSelection) {
        emit fabricVersionSelected(m_minecraftVersion, m_selectedVersion.fabricVersion);
        updateInstallButtonState(true, tr("安装 Fabric %1").arg(m_selectedVersion.fabricVersion));
    }
}

void FabricVersionListPage::onInstallClicked()
{
    if (!m_hasSelection) {
        NotificationManager::showError(this, tr("请先选择一个版本"));
        return;
    }

    if (m_isInstalling) {
        return;
    }

    SettingsManager *settings = SettingsManager::instance();
    QString instancePath = settings->getDefaultInstancePath();
    if (instancePath.isEmpty()) {
        NotificationManager::showError(this, tr("请先设置默认实例路径"));
        return;
    }

    FabricInstaller::instance()->setDownloadSource(settings->getFabricDownloadSource());

    m_isInstalling = true;
    updateInstallButtonState(false, tr("正在安装..."));
    m_cancelButton->setEnabled(true);
    m_statusInfoLabel->setText(tr("正在下载 Fabric %1...").arg(m_selectedVersion.fabricVersion));
    m_statusInfoLabel->setProperty("fabricStatus", "normal");
    m_statusInfoLabel->style()->unpolish(m_statusInfoLabel);
    m_statusInfoLabel->style()->polish(m_statusInfoLabel);

    QString mcVersion = m_minecraftVersion;
    QString fabricVersion = m_selectedVersion.fabricVersion;

    QTimer::singleShot(0, [mcVersion, fabricVersion, instancePath]() {
        FabricInstaller::instance()->downloadFabricInstaller(mcVersion, fabricVersion, instancePath);
    });
}

void FabricVersionListPage::onDownloadProgressUpdated(qint64 bytesReceived, qint64 bytesTotal)
{
    Q_UNUSED(bytesReceived);
    Q_UNUSED(bytesTotal);
}

void FabricVersionListPage::onInstallProgress(int progress, const QString &status)
{
    m_statusInfoLabel->setText(status);
}

void FabricVersionListPage::onInstallCompleted(const QString &version)
{
    m_isInstalling = false;
    updateInstallButtonState(true, tr("安装选中版本"));
    m_cancelButton->setEnabled(false);
    m_statusInfoLabel->setText(tr("Fabric %1 安装成功！").arg(version));
    m_statusInfoLabel->setStyleSheet(QString("color: %1; font-size: 10pt;").arg(ThemeManager::instance()->currentThemeColor()));

    NotificationManager::showSuccess(this, tr("Fabric %1 已成功安装！").arg(version));
}

void FabricVersionListPage::onInstallFailed(const QString &error)
{
    m_isInstalling = false;
    updateInstallButtonState(true, tr("安装选中版本"));
    m_cancelButton->setEnabled(false);
    m_statusInfoLabel->setText(tr("安装失败: %1").arg(error));
    m_statusInfoLabel->setStyleSheet("color: #f44336; font-size: 10pt;");

    NotificationManager::showError(this, error);
}

void FabricVersionListPage::onStatusChanged(const QString &status)
{
    Q_UNUSED(status);
}

void FabricVersionListPage::onVersionListFetched(const QList<FabricVersionInfo> &versions)
{
    m_fabricVersions = versions;
    populateVersionList();
    m_loadingOverlay->hideOverlay();
    m_statusLabel->setText(tr("共找到 %1 个Fabric版本").arg(versions.size()));
}

void FabricVersionListPage::onVersionListFetchFailed(const QString &error)
{
    m_loadingOverlay->showError(tr("获取版本列表失败: %1").arg(error));
    m_statusLabel->setText(tr("获取版本列表失败: %1").arg(error));
}

void FabricVersionListPage::updateInstallButtonState(bool enabled, const QString &text)
{
    m_installButton->setEnabled(enabled);
    m_installButton->setText(text);
}
