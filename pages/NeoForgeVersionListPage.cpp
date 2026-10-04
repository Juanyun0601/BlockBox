/**
 * @file   NeoForgeVersionListPage.cpp
 * @brief  NeoForge版本列表页面实现
 * @author BlockBox Team
 * @date   2026-05-29
 */
#include "NeoForgeVersionListPage.h"

#include <algorithm>

#include <QHBoxLayout>
#include <QHeaderView>
#include "components/AppMessageBox.h"
#include <QTimer>
#include <QUrl>

#include "components/BlurLoadingOverlay.h"
#include "components/NotificationManager.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

NeoForgeVersionListPage::NeoForgeVersionListPage(QWidget *parent)
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

NeoForgeVersionListPage::~NeoForgeVersionListPage()
{
    if (m_currentReply)
    {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }
}

void NeoForgeVersionListPage::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(20, 20, 20, 20);
    m_mainLayout->setSpacing(15);

    m_titleLabel = new QLabel(tr("选择NeoForge版本"));
    m_titleLabel->setObjectName("neoForgePageTitle");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_titleLabel);

    m_statusLabel = new QLabel(tr("请先选择Minecraft版本"));
    m_statusLabel->setObjectName("neoForgeStatusLabel");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_statusLabel);

    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();

    m_versionList = new QTreeWidget(this);
    m_versionList->setObjectName("neoForgeVersionList");
    m_versionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_versionList->setHeaderHidden(true);
    m_versionList->setColumnCount(1);
    m_versionList->header()->setStretchLastSection(true);
    m_versionList->setUniformRowHeights(true);
    m_versionList->setMinimumHeight(400);
    m_mainLayout->addWidget(m_versionList);

    connect(m_versionList, &QTreeWidget::itemClicked, this, &NeoForgeVersionListPage::onVersionItemClicked);

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
    m_statusInfoLabel->setObjectName("neoForgeStatusInfo");
    m_statusInfoLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_statusInfoLabel);

    connect(m_installButton, &QPushButton::clicked, this, &NeoForgeVersionListPage::onInstallClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        NeoForgeInstaller::instance()->cancelInstall();
    });

    connect(NeoForgeInstaller::instance(), &NeoForgeInstaller::installProgressUpdated, this, &NeoForgeVersionListPage::onInstallProgress);
    connect(NeoForgeInstaller::instance(), &NeoForgeInstaller::installCompleted, this, &NeoForgeVersionListPage::onInstallCompleted);
    connect(NeoForgeInstaller::instance(), &NeoForgeInstaller::installFailed, this, &NeoForgeVersionListPage::onInstallFailed);
    connect(NeoForgeInstaller::instance(), &NeoForgeInstaller::installCancelled, this, [this]() {
        m_isInstalling = false;
        m_installButton->setEnabled(true);
        m_cancelButton->setEnabled(false);
        m_statusInfoLabel->setText(tr("安装已取消"));
        m_statusInfoLabel->setProperty("neoForgeStatus", "normal");
        m_statusInfoLabel->style()->unpolish(m_statusInfoLabel);
        m_statusInfoLabel->style()->polish(m_statusInfoLabel);
    });
}

void NeoForgeVersionListPage::setMinecraftVersion(const QString &mcVersion)
{
    m_minecraftVersion = mcVersion;
    m_titleLabel->setText(tr("选择NeoForge版本 - Minecraft %1").arg(mcVersion));
    m_hasSelection = false;
    m_versions.clear();
    m_versionList->clear();
    loadNeoForgeVersions();
}

void NeoForgeVersionListPage::loadNeoForgeVersions()
{
    if (m_minecraftVersion.isEmpty())
    {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取NeoForge版本列表..."));
    m_versionList->clear();
    m_versions.clear();

    QString url = QString("https://bmclapi2.bangbang93.com/neoforge/list/%1").arg(m_minecraftVersion);
    QUrl requestUrl(url);
    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox Launcher");

    if (m_currentReply)
    {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    QNetworkReply* reply = m_networkManager->get(request);
    m_currentReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError)
        {
            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);

            if (doc.isArray())
            {
                QJsonArray versionsArray = doc.array();
                for (const QJsonValue &value : versionsArray)
                {
                    QJsonObject versionObj = value.toObject();
                    NeoForgeVersionInfo info;
                    info.version = versionObj["version"].toString();
                    info.mcversion = versionObj["mcversion"].toString();
                    m_versions.append(info);
                }

                std::sort(m_versions.begin(), m_versions.end(),
                    [](const NeoForgeVersionInfo &a, const NeoForgeVersionInfo &b) {
                        return a.version > b.version;
                    });

                for (const NeoForgeVersionInfo &info : m_versions)
                {
                    QString displayText = QString(
                        "<div style='font-size:12pt; font-weight:bold;'>NeoForge %1</div>"
                        "<div style='font-size:9pt; color:#666666;'>Minecraft: %2</div>"
                    ).arg(info.version).arg(info.mcversion);

                    QTreeWidgetItem *item = new QTreeWidgetItem();
                    item->setText(0, displayText);
                    item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
                    m_versionList->addTopLevelItem(item);

                    QLabel *label = new QLabel(displayText);
                    label->setWordWrap(true);
                    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                    label->setMargin(10);
                    label->setMinimumHeight(60);
                    m_versionList->setItemWidget(item, 0, label);
                }

                if (m_versionList->topLevelItemCount() > 0)
                {
                    m_versionList->setCurrentItem(m_versionList->topLevelItem(0));
                }

                m_loadingOverlay->hideOverlay();
                m_statusLabel->setText(tr("共找到 %1 个NeoForge版本").arg(m_versions.size()));
            }
            else
            {
                QString errorMsg = tr("获取版本列表失败: 数据格式错误");
                m_statusLabel->setText(errorMsg);
                m_loadingOverlay->showError(errorMsg);
            }
        }
        else
        {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMsg;
            if (statusCode == 404)
            {
                errorMsg = tr("此Minecraft版本不支持NeoForge");
            }
            else
            {
                errorMsg = tr("获取版本列表失败: %1").arg(reply->errorString());
            }
            m_statusLabel->setText(errorMsg);
            m_loadingOverlay->showError(errorMsg);
        }

        if (m_currentReply == reply)
        {
            m_currentReply = nullptr;
        }
        reply->deleteLater();
    });
}

void NeoForgeVersionListPage::onVersionItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    QString version = item->data(0, Qt::UserRole).toString();

    for (const NeoForgeVersionInfo &info : m_versions)
    {
        if (info.version == version)
        {
            m_selectedVersion = info;
            m_hasSelection = true;
            break;
        }
    }

    if (m_hasSelection)
    {
        emit neoForgeVersionSelected(m_minecraftVersion, m_selectedVersion.version);
        m_installButton->setEnabled(true);
        m_installButton->setText(tr("安装 NeoForge %1").arg(m_selectedVersion.version));
    }
}

void NeoForgeVersionListPage::onInstallClicked()
{
    if (!m_hasSelection)
    {
        NotificationManager::showError(this, tr("请先选择一个版本"));
        return;
    }

    if (m_isInstalling)
    {
        return;
    }

    SettingsManager *settings = SettingsManager::instance();
    QString instancePath = settings->getDefaultInstancePath();
    if (instancePath.isEmpty())
    {
        NotificationManager::showError(this, tr("请先设置默认实例路径"));
        return;
    }

    m_isInstalling = true;
    m_installButton->setEnabled(false);
    m_cancelButton->setEnabled(true);
    m_statusInfoLabel->setText(tr("正在下载 NeoForge %1...").arg(m_selectedVersion.version));
    m_statusInfoLabel->setProperty("neoForgeStatus", "normal");
    m_statusInfoLabel->style()->unpolish(m_statusInfoLabel);
    m_statusInfoLabel->style()->polish(m_statusInfoLabel);

    NeoForgeInstaller::instance()->setDownloadSource(settings->getNeoForgeDownloadSource());

    QString mcVersion = m_minecraftVersion;
    QString neoForgeVersion = m_selectedVersion.version;
    QTimer::singleShot(0, [mcVersion, neoForgeVersion, instancePath]() {
        NeoForgeInstaller::instance()->downloadNeoForgeInstaller(mcVersion, neoForgeVersion, instancePath);
    });
}

void NeoForgeVersionListPage::onInstallProgress(int progress, const QString &status)
{
    Q_UNUSED(progress);
    m_statusInfoLabel->setText(status);
}

void NeoForgeVersionListPage::onInstallCompleted(const QString &version)
{
    m_isInstalling = false;
    m_installButton->setEnabled(true);
    m_cancelButton->setEnabled(false);
    m_statusInfoLabel->setText(tr("NeoForge %1 安装成功！").arg(version));
    m_statusInfoLabel->setStyleSheet(QString("color: %1; font-size: 10pt;").arg(ThemeManager::instance()->currentThemeColor()));

    NotificationManager::showSuccess(this, tr("NeoForge %1 已成功安装！").arg(version));
}

void NeoForgeVersionListPage::onInstallFailed(const QString &error)
{
    m_isInstalling = false;
    m_installButton->setEnabled(true);
    m_cancelButton->setEnabled(false);
    m_statusInfoLabel->setText(tr("安装失败: %1").arg(error));
    m_statusInfoLabel->setStyleSheet("color: #f44336; font-size: 10pt;");

    NotificationManager::showError(this, error);
}