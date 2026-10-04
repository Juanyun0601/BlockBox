/**
 * @file   ForgeVersionListPage.cpp
 * @brief  Forge 版本列表页面实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "ForgeVersionListPage.h"

#include <algorithm>

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
#include "utils/forge/ForgeInstaller.h"
#include "utils/optifine/OptiFineInstaller.h"
#include "utils/LiteLoaderInstaller.h"
#include "utils/CleanroomInstaller.h"
#include "utils/OptiFabricInstaller.h"

ForgeVersionListPage::ForgeVersionListPage(QWidget *parent)
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
    , m_forgeInstaller(nullptr)
{
    initUI();
    m_networkManager = new QNetworkAccessManager(this);
    m_forgeInstaller = new ForgeInstaller(this);
    connect(m_forgeInstaller, &ForgeInstaller::installStarted, this, &ForgeVersionListPage::onForgeInstallStarted);
    connect(m_forgeInstaller, &ForgeInstaller::installProgress, this, &ForgeVersionListPage::onForgeInstallProgress);
    connect(m_forgeInstaller, &ForgeInstaller::installCompleted, this, &ForgeVersionListPage::onForgeInstallCompleted);
    connect(m_forgeInstaller, &ForgeInstaller::installFailed, this, &ForgeVersionListPage::onForgeInstallFailed);
    connect(m_forgeInstaller, &ForgeInstaller::installCancelled, this, &ForgeVersionListPage::onForgeInstallCancelled);
}

ForgeVersionListPage::~ForgeVersionListPage()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }
}

void ForgeVersionListPage::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(20, 20, 20, 20);
    m_mainLayout->setSpacing(15);

    m_titleLabel = new QLabel(tr("选择Forge版本"));
    m_titleLabel->setObjectName("forgePageTitle");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_titleLabel);

    m_statusLabel = new QLabel(tr("请先选择Minecraft版本"));
    m_statusLabel->setObjectName("forgeStatusLabel");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_statusLabel);

    m_loadingOverlay = new BlurLoadingOverlay(this);
    m_loadingOverlay->hide();

    m_versionList = new QTreeWidget(this);
    m_versionList->setObjectName("forgeVersionList");
    m_versionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_versionList->setHeaderHidden(true);
    m_versionList->setColumnCount(1);
    m_versionList->header()->setStretchLastSection(true);
    m_versionList->setUniformRowHeights(true);
    m_versionList->setMinimumHeight(400);
    m_mainLayout->addWidget(m_versionList);

    connect(m_versionList, &QTreeWidget::itemClicked, this, &ForgeVersionListPage::onVersionItemClicked);

    // 安装按钮区域
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

    // 安装进度条
    m_installProgressBar = new QProgressBar(this);
    m_installProgressBar->setObjectName("installProgressBar");
    m_installProgressBar->setVisible(false);
    m_installProgressBar->setTextVisible(true);
    m_mainLayout->addWidget(m_installProgressBar);

    // 状态信息标签
    m_statusInfoLabel = new QLabel("", this);
    m_statusInfoLabel->setObjectName("forgeStatusInfo");
    m_statusInfoLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_statusInfoLabel);

    // 连接信号
    connect(m_installButton, &QPushButton::clicked, this, &ForgeVersionListPage::onInstallClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        m_forgeInstaller->cancelInstall();
    });

    connect(OptiFineInstaller::instance(), &OptiFineInstaller::installProgressUpdated, this, &ForgeVersionListPage::onInstallProgress);
    connect(OptiFineInstaller::instance(), &OptiFineInstaller::installCompleted, this, &ForgeVersionListPage::onInstallCompleted);
    connect(OptiFineInstaller::instance(), &OptiFineInstaller::installFailed, this, &ForgeVersionListPage::onInstallFailed);
    connect(OptiFineInstaller::instance(), &OptiFineInstaller::installCancelled, this, [this]() {
        m_isInstalling = false;
        m_installButton->setEnabled(true);
        m_cancelButton->setEnabled(false);
        m_statusInfoLabel->setText(tr("安装已取消"));
        m_statusInfoLabel->setProperty("forgeStatus", "normal");
        m_statusInfoLabel->style()->unpolish(m_statusInfoLabel);
        m_statusInfoLabel->style()->polish(m_statusInfoLabel);
    });
}

void ForgeVersionListPage::setMinecraftVersion(const QString &mcVersion)
{
    m_minecraftVersion = mcVersion;
    m_titleLabel->setText(tr("选择%1版本 - Minecraft %2").arg(getLoaderDisplayName()).arg(mcVersion));
    m_hasSelection = false;
    m_forgeVersions.clear();
    m_versionList->clear();
    
    QString loaderNameLower = m_loaderName.toLower();
    if (loaderNameLower == "neoforge") {
        loadNeoForgeVersions();
    } else if (loaderNameLower == "optifine") {
        loadOptiFineVersions();
    } else if (loaderNameLower == "fabric") {
        loadFabricVersions();
    } else if (loaderNameLower == "quilt") {
        loadQuiltVersions();
    } else if (loaderNameLower == "legacyfabric") {
        loadLegacyFabricVersions();
    } else if (loaderNameLower == "liteloader") {
        loadLiteLoaderVersions();
    } else if (loaderNameLower == "cleanroom") {
        loadCleanroomVersions();
    } else if (loaderNameLower == "optifabric") {
        loadOptiFabricVersions();
    } else {
        loadForgeVersions();
    }
}

void ForgeVersionListPage::setLoaderName(const QString &loaderName)
{
    m_loaderName = loaderName;
}

void ForgeVersionListPage::loadForgeVersions()
{
    if (m_minecraftVersion.isEmpty()) {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取%1版本列表...").arg(getLoaderDisplayName()));
    m_versionList->clear();
    m_forgeVersions.clear();

    QString url = QString("https://bmclapi2.bangbang93.com/forge/minecraft/%1").arg(m_minecraftVersion);
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
                for (const QJsonValue &value : versionsArray) {
                    QJsonObject versionObj = value.toObject();
                    ForgeVersionInfo info;
                    info.version = versionObj["version"].toString();
                    info.mcversion = versionObj["mcversion"].toString();
                    info.branch = versionObj["branch"].toString();
                    info.build = versionObj["build"].toInt();
                    info.modified = versionObj["modified"].toString();
                    info.hasInstaller = false;
                    info.isAvailable = false;

                    QJsonArray filesArray = versionObj["files"].toArray();
                    for (const QJsonValue &fileValue : filesArray) {
                        QJsonObject fileObj = fileValue.toObject();
                        QString category = fileObj["category"].toString();
                        QString format = fileObj["format"].toString();
                        if (category == "installer" || category == "installer-win") {
                            info.hasInstaller = true;
                            info.isAvailable = true;
                            break;
                        }
                    }

                    m_forgeVersions.append(info);
                }
            }

            populateVersionList();
            m_loadingOverlay->hideOverlay();
            m_statusLabel->setText(tr("共找到 %1 个%2版本").arg(m_forgeVersions.size()).arg(getLoaderDisplayName()));
        } else {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMsg;
            if (statusCode == 404) {
                errorMsg = tr("此版本无法使用%1").arg(getLoaderDisplayName());
                m_statusLabel->setText(errorMsg);
            } else {
                errorMsg = tr("获取版本列表失败: %1").arg(reply->errorString());
                m_statusLabel->setText(errorMsg);
            }
            m_loadingOverlay->showError(errorMsg);
        }

        if (m_currentReply == reply) {
            m_currentReply = nullptr;
        }
        reply->deleteLater();
    });
}

void ForgeVersionListPage::loadNeoForgeVersions()
{
    if (m_minecraftVersion.isEmpty()) {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取NeoForge版本列表..."));
    m_versionList->clear();
    m_forgeVersions.clear();

    QString url = QString("https://bmclapi2.bangbang93.com/neoforge/list/%1").arg(m_minecraftVersion);
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
                populateNeoForgeVersionList(versionsArray);
                m_loadingOverlay->hideOverlay();
                m_statusLabel->setText(tr("共找到 %1 个NeoForge版本").arg(m_forgeVersions.size()));
            } else {
                QString errorMsg = tr("获取版本列表失败: 数据格式错误");
                m_statusLabel->setText(errorMsg);
                m_loadingOverlay->showError(errorMsg);
            }
        } else {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMsg;
            if (statusCode == 404) {
                errorMsg = tr("此版本无法使用NeoForge");
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

void ForgeVersionListPage::loadOptiFineVersions()
{
    if (m_minecraftVersion.isEmpty()) {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取OptiFine版本列表..."));
    m_versionList->clear();
    m_forgeVersions.clear();

    QString url = QString("https://bmclapi2.bangbang93.com/optifine/%1").arg(m_minecraftVersion);
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
                populateOptiFineVersionList(versionsArray);
                m_loadingOverlay->hideOverlay();
                m_statusLabel->setText(tr("共找到 %1 个OptiFine版本").arg(m_forgeVersions.size()));
            } else {
                QString errorMsg = tr("获取版本列表失败: 数据格式错误");
                m_statusLabel->setText(errorMsg);
                m_loadingOverlay->showError(errorMsg);
            }
        } else {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMsg;
            if (statusCode == 404) {
                errorMsg = tr("此版本无法使用OptiFine");
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

void ForgeVersionListPage::loadFabricVersions()
{
    if (m_minecraftVersion.isEmpty()) {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取Fabric版本列表..."));
    m_versionList->clear();
    m_forgeVersions.clear();

    QString url = QString("https://bmclapi2.bangbang93.com/fabric-meta/v2/versions/loader/%1").arg(m_minecraftVersion);
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
                populateFabricVersionList(versionsArray);
                m_loadingOverlay->hideOverlay();
                m_statusLabel->setText(tr("共找到 %1 个Fabric版本").arg(m_forgeVersions.size()));
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

void ForgeVersionListPage::loadQuiltVersions()
{
    if (m_minecraftVersion.isEmpty()) {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取Quilt版本列表..."));
    m_versionList->clear();
    m_forgeVersions.clear();

    QString url = QString("https://meta.quiltmc.org/v3/versions/loader/%1").arg(m_minecraftVersion);
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
                populateQuiltVersionList(versionsArray);
                m_loadingOverlay->hideOverlay();
                m_statusLabel->setText(tr("共找到 %1 个Quilt版本").arg(m_forgeVersions.size()));
            } else {
                QString errorMsg = tr("获取版本列表失败: 数据格式错误");
                m_statusLabel->setText(errorMsg);
                m_loadingOverlay->showError(errorMsg);
            }
        } else {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMsg;
            if (statusCode == 404) {
                errorMsg = tr("此版本无法使用Quilt");
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

void ForgeVersionListPage::loadLegacyFabricVersions()
{
    if (m_minecraftVersion.isEmpty()) {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取LegacyFabric版本列表..."));
    m_versionList->clear();
    m_forgeVersions.clear();

    QString url = QString("https://meta.legacyfabric.net/v2/versions/loader/%1").arg(m_minecraftVersion);
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
                populateLegacyFabricVersionList(versionsArray);
                m_loadingOverlay->hideOverlay();
                m_statusLabel->setText(tr("共找到 %1 个LegacyFabric版本").arg(m_forgeVersions.size()));
            } else {
                QString errorMsg = tr("获取版本列表失败: 数据格式错误");
                m_statusLabel->setText(errorMsg);
                m_loadingOverlay->showError(errorMsg);
            }
        } else {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMsg;
            if (statusCode == 404) {
                errorMsg = tr("此版本无法使用LegacyFabric");
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

void ForgeVersionListPage::loadLiteLoaderVersions()
{
    if (m_minecraftVersion.isEmpty()) {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取LiteLoader版本列表..."));
    m_versionList->clear();
    m_forgeVersions.clear();

    QString url = QString("https://dl.liteloader.com/versions/%1/json").arg(m_minecraftVersion);
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

            if (doc.isObject()) {
                QJsonObject versionData = doc.object();
                populateLiteLoaderVersionList(versionData);
                m_loadingOverlay->hideOverlay();
                m_statusLabel->setText(tr("共找到 %1 个LiteLoader版本").arg(m_forgeVersions.size()));
            } else {
                QString errorMsg = tr("获取版本列表失败: 数据格式错误");
                m_statusLabel->setText(errorMsg);
                m_loadingOverlay->showError(errorMsg);
            }
        } else {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMsg;
            if (statusCode == 404) {
                errorMsg = tr("此版本无法使用LiteLoader");
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

void ForgeVersionListPage::loadCleanroomVersions()
{
    if (m_minecraftVersion.isEmpty()) {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取Cleanroom版本列表..."));
    m_versionList->clear();
    m_forgeVersions.clear();

    QString url = "https://api.github.com/repos/CleanroomMC/Cleanroom/releases";
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
                populateCleanroomVersionList(versionsArray);
                m_loadingOverlay->hideOverlay();
                m_statusLabel->setText(tr("共找到 %1 个Cleanroom版本").arg(m_forgeVersions.size()));
            } else {
                QString errorMsg = tr("获取版本列表失败: 数据格式错误");
                m_statusLabel->setText(errorMsg);
                m_loadingOverlay->showError(errorMsg);
            }
        } else {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMsg;
            if (statusCode == 404) {
                errorMsg = tr("此版本无法使用Cleanroom");
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

void ForgeVersionListPage::loadOptiFabricVersions()
{
    if (m_minecraftVersion.isEmpty()) {
        m_statusLabel->setText(tr("请先选择Minecraft版本"));
        return;
    }

    m_loadingOverlay->showOverlay(tr("正在获取OptiFabric版本列表..."));
    m_versionList->clear();
    m_forgeVersions.clear();

    QString url = QString("https://api.modrinth.com/v2/project/optifine/version?game_versions=[\"%1\"]&loaders=[\"fabric\"]").arg(m_minecraftVersion);
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
                populateOptiFabricVersionList(versionsArray);
                m_loadingOverlay->hideOverlay();
                m_statusLabel->setText(tr("共找到 %1 个OptiFabric版本").arg(m_forgeVersions.size()));
            } else {
                QString errorMsg = tr("获取版本列表失败: 数据格式错误");
                m_statusLabel->setText(errorMsg);
                m_loadingOverlay->showError(errorMsg);
            }
        } else {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMsg;
            if (statusCode == 404) {
                errorMsg = tr("此版本无法使用OptiFabric");
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

void ForgeVersionListPage::populateVersionList()
{
    m_versionList->clear();

    QList<ForgeVersionInfo> availableVersions;
    QList<ForgeVersionInfo> unavailableVersions;

    for (const ForgeVersionInfo &info : m_forgeVersions) {
        if (info.isAvailable) {
            availableVersions.append(info);
        } else {
            unavailableVersions.append(info);
        }
    }

    std::sort(availableVersions.begin(), availableVersions.end(), 
        [](const ForgeVersionInfo &a, const ForgeVersionInfo &b) {
            return a.build > b.build;
        });
    std::sort(unavailableVersions.begin(), unavailableVersions.end(),
        [](const ForgeVersionInfo &a, const ForgeVersionInfo &b) {
            return a.build > b.build;
        });

    QString loaderDisplayName = getLoaderDisplayName();

    for (const ForgeVersionInfo &info : availableVersions) {
        QString displayText;
        QString modifiedStr;
        if (!info.modified.isEmpty()) {
            QDateTime modifiedTime = QDateTime::fromString(info.modified, Qt::ISODate);
            modifiedStr = modifiedTime.toString("yyyy-MM-dd");
        }

        QString branchStr = info.branch.isEmpty() ? "" : QString(" [%1]").arg(info.branch);
        displayText = QString(
            "<div style='font-size:12pt; font-weight:bold;'>%1 %2%3</div>"
            "<div style='font-size:9pt; color:#666666;'>Build: %4 | %5</div>"
        ).arg(loaderDisplayName).arg(info.version).arg(branchStr).arg(info.build).arg(modifiedStr);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
        item->setData(0, Qt::UserRole + 1, info.isAvailable);
        m_versionList->addTopLevelItem(item);

        QLabel *label = new QLabel(displayText);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMargin(10);
        label->setMinimumHeight(60);
        m_versionList->setItemWidget(item, 0, label);
    }

    for (const ForgeVersionInfo &info : unavailableVersions) {
        QString displayText;
        QString modifiedStr;
        if (!info.modified.isEmpty()) {
            QDateTime modifiedTime = QDateTime::fromString(info.modified, Qt::ISODate);
            modifiedStr = modifiedTime.toString("yyyy-MM-dd");
        }

        QString branchStr = info.branch.isEmpty() ? "" : QString(" [%1]").arg(info.branch);
        displayText = QString(
            "<div style='font-size:12pt; font-weight:bold; color:#999999;'>%1 %2%3 (不可用)</div>"
            "<div style='font-size:9pt; color:#999999;'>Build: %4 | %5</div>"
        ).arg(loaderDisplayName).arg(info.version).arg(branchStr).arg(info.build).arg(modifiedStr);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
        item->setData(0, Qt::UserRole + 1, false);
        m_versionList->addTopLevelItem(item);

        QLabel *label = new QLabel(displayText);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMargin(10);
        label->setMinimumHeight(60);
        label->setStyleSheet("color: #999999;");
        m_versionList->setItemWidget(item, 0, label);
    }

    if (m_versionList->topLevelItemCount() > 0) {
        m_versionList->setCurrentItem(m_versionList->topLevelItem(0));
    }
}

void ForgeVersionListPage::onVersionItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    QString version = item->data(0, Qt::UserRole).toString();

    for (const ForgeVersionInfo &info : m_forgeVersions) {
        if (info.version == version) {
            m_selectedVersion = info;
            m_hasSelection = true;
            break;
        }
    }

    if (m_hasSelection) {
        emit forgeVersionSelected(m_loaderName, m_minecraftVersion, m_selectedVersion.version);
        
        // 更新安装按钮状态
        if (m_selectedVersion.isAvailable) {
            m_installButton->setEnabled(true);
            m_installButton->setText(tr("安装 %1 %2").arg(getLoaderDisplayName()).arg(m_selectedVersion.version));
        } else {
            m_installButton->setEnabled(false);
            m_installButton->setText(tr("此版本不可用"));
        }
    }
}

void ForgeVersionListPage::populateNeoForgeVersionList(const QJsonArray &versions)
{
    m_versionList->clear();
    m_forgeVersions.clear();

    for (const QJsonValue &value : versions) {
        QJsonObject versionObj = value.toObject();
        ForgeVersionInfo info;
        info.version = versionObj["version"].toString();
        info.mcversion = versionObj["mcversion"].toString();
        info.branch = "";
        info.build = 0;
        info.modified = "";
        info.hasInstaller = true;
        info.isAvailable = true;

        m_forgeVersions.append(info);
    }

    std::sort(m_forgeVersions.begin(), m_forgeVersions.end(), 
        [](const ForgeVersionInfo &a, const ForgeVersionInfo &b) {
            return a.version > b.version;
        });

    for (const ForgeVersionInfo &info : m_forgeVersions) {
        QString displayText;
        
        if (info.isAvailable) {
            displayText = QString(
                "<div style='font-size:12pt; font-weight:bold;'>NeoForge %1</div>"
                "<div style='font-size:9pt; color:#666666;'>Minecraft: %2</div>"
            ).arg(info.version).arg(info.mcversion);
        } else {
            displayText = QString(
                "<div style='font-size:12pt; font-weight:bold; color:#999999;'>NeoForge %1 (不可用)</div>"
                "<div style='font-size:9pt; color:#999999;'>Minecraft: %2</div>"
            ).arg(info.version).arg(info.mcversion);
        }

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
        item->setData(0, Qt::UserRole + 1, info.isAvailable);
        m_versionList->addTopLevelItem(item);

        QLabel *label = new QLabel(displayText);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMargin(10);
        label->setMinimumHeight(60);
        if (!info.isAvailable) {
            label->setStyleSheet("color: #999999;");
        }
        m_versionList->setItemWidget(item, 0, label);
    }

    if (m_versionList->topLevelItemCount() > 0) {
        m_versionList->setCurrentItem(m_versionList->topLevelItem(0));
    }
}

void ForgeVersionListPage::populateOptiFineVersionList(const QJsonArray &versions)
{
    m_versionList->clear();
    m_forgeVersions.clear();

    for (const QJsonValue &value : versions) {
        QJsonObject versionObj = value.toObject();
        ForgeVersionInfo info;
        QString type = versionObj["type"].toString();
        QString patch = versionObj["patch"].toString();
        info.version = QString("%1_%2").arg(type).arg(patch);
        info.mcversion = versionObj["mcversion"].toString();
        info.fileName = versionObj["filename"].toString();
        info.branch = "";
        info.build = 0;
        info.modified = "";
        info.hasInstaller = true;
        info.isAvailable = true;

        m_forgeVersions.append(info);
    }

    for (const ForgeVersionInfo &info : m_forgeVersions) {
        QString displayText;
        
        displayText = QString(
            "<div style='font-size:12pt; font-weight:bold;'>OptiFine %1</div>"
            "<div style='font-size:9pt; color:#666666;'>Minecraft: %2</div>"
        ).arg(info.version).arg(info.mcversion);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
        item->setData(0, Qt::UserRole + 1, info.isAvailable);
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

void ForgeVersionListPage::populateFabricVersionList(const QJsonArray &versions)
{
    m_versionList->clear();
    m_forgeVersions.clear();

    for (const QJsonValue &value : versions) {
        QJsonObject versionObj = value.toObject();
        ForgeVersionInfo info;
        QJsonObject loaderObj = versionObj["loader"].toObject();
        info.version = loaderObj["version"].toString();
        info.mcversion = m_minecraftVersion;
        info.branch = "";
        info.build = 0;
        info.modified = "";
        info.hasInstaller = true;
        info.isAvailable = true;

        m_forgeVersions.append(info);
    }

    for (const ForgeVersionInfo &info : m_forgeVersions) {
        QString displayText;
        
        displayText = QString(
            "<div style='font-size:12pt; font-weight:bold;'>Fabric Loader %1</div>"
            "<div style='font-size:9pt; color:#666666;'>Minecraft: %2</div>"
        ).arg(info.version).arg(info.mcversion);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
        item->setData(0, Qt::UserRole + 1, info.isAvailable);
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

void ForgeVersionListPage::populateQuiltVersionList(const QJsonArray &versions)
{
    m_versionList->clear();
    m_forgeVersions.clear();

    for (const QJsonValue &value : versions) {
        QJsonObject versionObj = value.toObject();
        ForgeVersionInfo info;
        QJsonObject loaderObj = versionObj["loader"].toObject();
        info.version = loaderObj["version"].toString();
        info.mcversion = m_minecraftVersion;
        info.branch = "";
        info.build = 0;
        info.modified = "";
        info.hasInstaller = true;
        info.isAvailable = true;

        m_forgeVersions.append(info);
    }

    for (const ForgeVersionInfo &info : m_forgeVersions) {
        QString displayText;
        
        displayText = QString(
            "<div style='font-size:12pt; font-weight:bold;'>Quilt Loader %1</div>"
            "<div style='font-size:9pt; color:#666666;'>Minecraft: %2</div>"
        ).arg(info.version).arg(info.mcversion);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
        item->setData(0, Qt::UserRole + 1, info.isAvailable);
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

void ForgeVersionListPage::populateLegacyFabricVersionList(const QJsonArray &versions)
{
    m_versionList->clear();
    m_forgeVersions.clear();

    for (const QJsonValue &value : versions) {
        QJsonObject versionObj = value.toObject();
        ForgeVersionInfo info;
        QJsonObject loaderObj = versionObj["loader"].toObject();
        info.version = loaderObj["version"].toString();
        info.mcversion = m_minecraftVersion;
        info.branch = "";
        info.build = 0;
        info.modified = "";
        info.hasInstaller = true;
        info.isAvailable = true;

        m_forgeVersions.append(info);
    }

    for (const ForgeVersionInfo &info : m_forgeVersions) {
        QString displayText;
        
        displayText = QString(
            "<div style='font-size:12pt; font-weight:bold;'>LegacyFabric Loader %1</div>"
            "<div style='font-size:9pt; color:#666666;'>Minecraft: %2</div>"
        ).arg(info.version).arg(info.mcversion);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
        item->setData(0, Qt::UserRole + 1, info.isAvailable);
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

void ForgeVersionListPage::populateLiteLoaderVersionList(const QJsonObject &versionData)
{
    m_versionList->clear();
    m_forgeVersions.clear();

    QJsonObject injectors = versionData["injectors"].toObject();
    for (auto it = injectors.begin(); it != injectors.end(); ++it) {
        QString injectorName = it.key();
        QJsonObject injectorObj = it.value().toObject();
        
        ForgeVersionInfo info;
        info.version = injectorObj["version"].toString();
        info.mcversion = m_minecraftVersion;
        info.branch = "";
        info.build = 0;
        info.modified = "";
        info.hasInstaller = true;
        info.isAvailable = true;
        info.fileName = injectorObj["url"].toString();

        m_forgeVersions.append(info);
    }

    for (const ForgeVersionInfo &info : m_forgeVersions) {
        QString displayText;
        
        displayText = QString(
            "<div style='font-size:12pt; font-weight:bold;'>LiteLoader %1</div>"
            "<div style='font-size:9pt; color:#666666;'>Minecraft: %2</div>"
        ).arg(info.version).arg(info.mcversion);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
        item->setData(0, Qt::UserRole + 1, info.isAvailable);
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

void ForgeVersionListPage::populateCleanroomVersionList(const QJsonArray &versions)
{
    m_versionList->clear();
    m_forgeVersions.clear();

    for (const QJsonValue &value : versions) {
        QJsonObject releaseObj = value.toObject();
        QString tagName = releaseObj["tag_name"].toString();
        
        if (!tagName.contains(m_minecraftVersion)) {
            continue;
        }

        ForgeVersionInfo info;
        info.version = tagName;
        info.mcversion = m_minecraftVersion;
        info.branch = "";
        info.build = 0;
        info.modified = releaseObj["published_at"].toString();
        info.hasInstaller = true;
        info.isAvailable = true;

        QJsonArray assets = releaseObj["assets"].toArray();
        for (const QJsonValue &assetValue : assets) {
            QJsonObject assetObj = assetValue.toObject();
            QString assetName = assetObj["name"].toString();
            if (assetName.endsWith("-installer.jar") || assetName.endsWith(".jar")) {
                info.fileName = assetObj["browser_download_url"].toString();
                break;
            }
        }

        m_forgeVersions.append(info);
    }

    for (const ForgeVersionInfo &info : m_forgeVersions) {
        QString displayText;
        
        displayText = QString(
            "<div style='font-size:12pt; font-weight:bold;'>Cleanroom %1</div>"
            "<div style='font-size:9pt; color:#666666;'>Minecraft: %2</div>"
        ).arg(info.version).arg(info.mcversion);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
        item->setData(0, Qt::UserRole + 1, info.isAvailable);
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

void ForgeVersionListPage::populateOptiFabricVersionList(const QJsonArray &versions)
{
    m_versionList->clear();
    m_forgeVersions.clear();

    for (const QJsonValue &value : versions) {
        QJsonObject versionObj = value.toObject();
        ForgeVersionInfo info;
        info.version = versionObj["version_number"].toString();
        info.mcversion = m_minecraftVersion;
        info.branch = "";
        info.build = 0;
        info.modified = versionObj["date_published"].toString();
        info.hasInstaller = true;
        info.isAvailable = true;

        QJsonArray files = versionObj["files"].toArray();
        for (const QJsonValue &fileValue : files) {
            QJsonObject fileObj = fileValue.toObject();
            if (fileObj["primary"].toBool()) {
                info.fileName = fileObj["url"].toString();
                break;
            }
        }

        m_forgeVersions.append(info);
    }

    for (const ForgeVersionInfo &info : m_forgeVersions) {
        QString displayText;
        
        displayText = QString(
            "<div style='font-size:12pt; font-weight:bold;'>OptiFabric %1</div>"
            "<div style='font-size:9pt; color:#666666;'>Minecraft: %2</div>"
        ).arg(info.version).arg(info.mcversion);

        QTreeWidgetItem *item = new QTreeWidgetItem();
        item->setText(0, displayText);
        item->setData(0, Qt::UserRole, QVariant::fromValue(info.version));
        item->setData(0, Qt::UserRole + 1, info.isAvailable);
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

QString ForgeVersionListPage::getLoaderDisplayName() const
{
    QString loaderNameLower = m_loaderName.toLower();
    if (loaderNameLower == "neoforge") {
        return "NeoForge";
    } else if (loaderNameLower == "fabric") {
        return "Fabric";
    } else if (loaderNameLower == "quilt") {
        return "Quilt";
    } else if (loaderNameLower == "optifine") {
        return "OptiFine";
    } else if (loaderNameLower == "legacyfabric") {
        return "LegacyFabric";
    } else if (loaderNameLower == "liteloader") {
        return "LiteLoader";
    } else if (loaderNameLower == "cleanroom") {
        return "Cleanroom";
    } else if (loaderNameLower == "optifabric") {
        return "OptiFabric";
    } else {
        return "Forge";
    }
}

void ForgeVersionListPage::onInstallClicked()
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
    
    QString loaderNameLower = m_loaderName.toLower();
    
    m_isInstalling = true;
    m_installButton->setEnabled(false);
    m_cancelButton->setEnabled(true);
    m_statusInfoLabel->setText(tr("正在下载 %1 %2...").arg(getLoaderDisplayName()).arg(m_selectedVersion.version));
    m_statusInfoLabel->setProperty("forgeStatus", "normal");
    m_statusInfoLabel->style()->unpolish(m_statusInfoLabel);
    m_statusInfoLabel->style()->polish(m_statusInfoLabel);
    
    if (loaderNameLower == "optifine") {
        OptiFineInstaller::instance()->setDownloadSource(settings->getOptiFineDownloadSource());
        
        QString mcVersion = m_minecraftVersion;
        QString optiFineVersion = m_selectedVersion.version;
        QString optiFineFileName = m_selectedVersion.fileName;
        QTimer::singleShot(0, [mcVersion, optiFineVersion, optiFineFileName, instancePath]() {
            OptiFineInstaller::instance()->downloadOptiFineInstaller(mcVersion, optiFineVersion, optiFineFileName, instancePath);
        });
    } else if (loaderNameLower == "liteloader") {
        LiteLoaderInstaller::instance()->setCurrentTaskId(ForgeInstaller::instance()->currentTaskId());
        connect(LiteLoaderInstaller::instance(), &LiteLoaderInstaller::installCompleted, this, &ForgeVersionListPage::onInstallCompleted);
        connect(LiteLoaderInstaller::instance(), &LiteLoaderInstaller::installFailed, this, &ForgeVersionListPage::onInstallFailed);
        LiteLoaderInstaller::instance()->downloadLiteLoader(m_minecraftVersion, m_selectedVersion.version, instancePath);
    } else if (loaderNameLower == "cleanroom") {
        CleanroomInstaller::instance()->setCurrentTaskId(ForgeInstaller::instance()->currentTaskId());
        connect(CleanroomInstaller::instance(), &CleanroomInstaller::installCompleted, this, &ForgeVersionListPage::onInstallCompleted);
        connect(CleanroomInstaller::instance(), &CleanroomInstaller::installFailed, this, &ForgeVersionListPage::onInstallFailed);
        CleanroomInstaller::instance()->downloadCleanroom(m_minecraftVersion, m_selectedVersion.version, m_selectedVersion.fileName, instancePath);
    } else if (loaderNameLower == "optifabric") {
        OptiFabricInstaller::instance()->setCurrentTaskId(ForgeInstaller::instance()->currentTaskId());
        connect(OptiFabricInstaller::instance(), &OptiFabricInstaller::installCompleted, this, &ForgeVersionListPage::onInstallCompleted);
        connect(OptiFabricInstaller::instance(), &OptiFabricInstaller::installFailed, this, &ForgeVersionListPage::onInstallFailed);
        OptiFabricInstaller::instance()->downloadOptiFabric(m_minecraftVersion, m_selectedVersion.version, m_selectedVersion.fileName, instancePath);
    } else {
        m_forgeInstaller->setInstancePath(instancePath);
        m_forgeInstaller->setMcVersion(m_minecraftVersion);
        m_forgeInstaller->setForgeVersion(m_selectedVersion.version);
        m_forgeInstaller->startInstall();
    }
}

void ForgeVersionListPage::onInstallProgress(int progress, const QString &status)
{
    Q_UNUSED(progress);
    m_statusInfoLabel->setText(status);
}

void ForgeVersionListPage::onInstallCompleted(const QString &version)
{
    m_isInstalling = false;
    m_installButton->setEnabled(true);
    m_cancelButton->setEnabled(false);
    m_statusInfoLabel->setText(tr("%1 %2 安装成功！").arg(getLoaderDisplayName()).arg(version));
    m_statusInfoLabel->setStyleSheet(QString("color: %1; font-size: 10pt;").arg(ThemeManager::instance()->currentThemeColor()));
    
    NotificationManager::showSuccess(this, tr("%1 %2 已成功安装！").arg(getLoaderDisplayName()).arg(version));
}

void ForgeVersionListPage::onInstallFailed(const QString &error)
{
    m_isInstalling = false;
    m_installButton->setEnabled(true);
    m_cancelButton->setEnabled(false);
    m_statusInfoLabel->setText(tr("安装失败: %1").arg(error));
    m_statusInfoLabel->setStyleSheet("color: #f44336; font-size: 10pt;");
    
    NotificationManager::showError(this, error);
}

void ForgeVersionListPage::onForgeInstallStarted(const QString &taskId)
{
    Q_UNUSED(taskId);
}

void ForgeVersionListPage::onForgeInstallProgress(const QString &taskId, const QString &stage, int percent)
{
    Q_UNUSED(taskId);
    m_installProgressBar->setVisible(true);
    m_installProgressBar->setValue(percent);
    m_statusInfoLabel->setText(stage);
}

void ForgeVersionListPage::onForgeInstallCompleted(const QString &taskId)
{
    Q_UNUSED(taskId);
    m_isInstalling = false;
    m_installButton->setEnabled(true);
    m_cancelButton->setEnabled(false);
    m_installProgressBar->setVisible(false);
    m_statusInfoLabel->setText(tr("%1 %2 安装成功！").arg(getLoaderDisplayName()).arg(m_selectedVersion.version));
    m_statusInfoLabel->setStyleSheet(QString("color: %1; font-size: 10pt;").arg(ThemeManager::instance()->currentThemeColor()));

    NotificationManager::showSuccess(this, tr("%1 %2 已成功安装！").arg(getLoaderDisplayName()).arg(m_selectedVersion.version));
}

void ForgeVersionListPage::onForgeInstallFailed(const QString &taskId, const QString &error)
{
    Q_UNUSED(taskId);
    m_isInstalling = false;
    m_installButton->setEnabled(true);
    m_cancelButton->setEnabled(false);
    m_installProgressBar->setVisible(false);
    m_statusInfoLabel->setText(tr("安装失败: %1").arg(error));
    m_statusInfoLabel->setStyleSheet("color: #f44336; font-size: 10pt;");

    NotificationManager::showError(this, error);
}

void ForgeVersionListPage::onForgeInstallCancelled(const QString &taskId)
{
    Q_UNUSED(taskId);
    m_isInstalling = false;
    m_installButton->setEnabled(true);
    m_cancelButton->setEnabled(false);
    m_installProgressBar->setVisible(false);
    m_statusInfoLabel->setText(tr("安装已取消"));
    m_statusInfoLabel->setProperty("forgeStatus", "normal");
    m_statusInfoLabel->style()->unpolish(m_statusInfoLabel);
    m_statusInfoLabel->style()->polish(m_statusInfoLabel);
}
