/**
 * @file   SettingsBedrockGamePage.cpp
 * @brief  基岩版全局游戏设置页实现
 * @author BlockBox Team
 * @date   2026-08-16
 */
#include "SettingsBedrockGamePage.h"

#include <QDesktopServices>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include "components/CollapsibleSectionCard.h"
#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"
#include "utils/BedrockLauncher.h"
#include "utils/bedrock/BedrockInstanceManager.h"
#include "utils/SettingsManager.h"

namespace {

/** 基岩版设置项存储前缀 */
const QString kBedrockPrefix = QStringLiteral("bedrock/");

}

SettingsBedrockGamePage::SettingsBedrockGamePage(QWidget *parent)
    : QWidget(parent)
    , m_closeLauncherCheck(nullptr)
    , m_preferPreviewCheck(nullptr)
    , m_versionValue(nullptr)
    , m_packageValue(nullptr)
    , m_dataDirValue(nullptr)
    , m_openDataDirBtn(nullptr)
    , m_instanceValue(nullptr)
    , m_instanceDirValue(nullptr)
{
    initUI();
}

void SettingsBedrockGamePage::refreshInfo()
{
    refreshInstallInfo();
    refreshInstanceInfo();
}

void SettingsBedrockGamePage::initUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // ── 标题栏（标题 + 恢复默认值按钮）──
    QFrame *headerCard = new QFrame(this);
    headerCard->setObjectName("settingsHeaderCard");
    headerCard->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout *headerLayout = new QHBoxLayout(headerCard);
    headerLayout->setContentsMargins(24, 12, 24, 12);
    headerLayout->setSpacing(12);

    OutlinedLabel *titleLabel = new OutlinedLabel(tr("全局游戏设置"), headerCard);
    titleLabel->setObjectName("sectionTitle");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    QPushButton *restoreBtn = new QPushButton(tr("恢复默认值"), headerCard);
    restoreBtn->setObjectName("restoreDefaultsBtn");
    headerLayout->addWidget(restoreBtn);

    layout->addWidget(headerCard);

    auto createCard = [layout](const QString &title, QVBoxLayout **cardLayout)
    {
        auto *card = new CollapsibleSectionCard(title);
        layout->addWidget(card);
        *cardLayout = card->contentLayout();
    };

    auto appendRow = [](QVBoxLayout *cardLayout, const QString &title, const QString &desc,
                        const QString &help = QString(), bool isLast = false)
    {
        QFrame *row = new QFrame();
        row->setObjectName("settingRow");
        row->setAttribute(Qt::WA_StyledBackground, true);
        if (isLast)
            row->setProperty("lastRow", true);

        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(24, 16, 24, 16);
        rowLayout->setSpacing(20);

        QWidget *info = new QWidget(row);
        info->setObjectName("settingInfo");
        QVBoxLayout *infoLayout = new QVBoxLayout(info);
        infoLayout->setContentsMargins(0, 0, 0, 0);
        infoLayout->setSpacing(4);

        if (!title.isEmpty())
        {
            QHBoxLayout *titleRow = new QHBoxLayout();
            titleRow->setContentsMargins(0, 0, 0, 0);
            titleRow->setSpacing(6);

            QLabel *titleLabel = new QLabel(title, info);
            titleLabel->setObjectName("settingTitle");
            titleRow->addWidget(titleLabel);

            if (!help.isEmpty())
            {
                QToolButton *helpBtn = new QToolButton();
                helpBtn->setText("i");
                helpBtn->setObjectName("helpButton");
                helpBtn->setToolTip(help);
                helpBtn->setCursor(Qt::PointingHandCursor);
                helpBtn->setFixedSize(18, 18);
                connect(helpBtn, &QToolButton::clicked, [help, helpBtn]()
                {
                    QToolTip::showText(helpBtn->mapToGlobal(QPoint(helpBtn->width() + 4, -4)),
                                       help, helpBtn, QRect(), 5000);
                });
                titleRow->addWidget(helpBtn);
            }
            titleRow->addStretch();
            infoLayout->addLayout(titleRow);
        }

        if (!desc.isEmpty())
        {
            QLabel *descLabel = new QLabel(desc, info);
            descLabel->setObjectName("settingDesc");
            descLabel->setWordWrap(true);
            infoLayout->addWidget(descLabel);
        }

        rowLayout->addWidget(info, 1);
        cardLayout->addWidget(row);
        return rowLayout;
    };

    // ── 启动行为 ──
    QVBoxLayout *launchCard = nullptr;
    createCard(tr("启动行为"), &launchCard);

    m_closeLauncherCheck = new CustomCheckBox("", this);
    m_closeLauncherCheck->setToolTip(tr("游戏启动成功后自动退出本启动器。"));
    QHBoxLayout *closeRow = appendRow(launchCard,
        tr("启动后自动关闭启动器"), tr("游戏启动成功后自动退出本启动器。"));
    closeRow->addWidget(m_closeLauncherCheck);

    m_preferPreviewCheck = new CustomCheckBox("", this);
    m_preferPreviewCheck->setToolTip(tr("已安装预览版时优先启动 Beta / Preview 版本。"));
    QHBoxLayout *previewRow = appendRow(launchCard,
        tr("优先启动预览版"), tr("已安装预览版时优先启动 Beta / Preview 版本。"),
        tr("基岩版在微软商店中区分正式版与预览版（Preview/Beta），"
           "启用后优先启动预览版以体验最新功能。"), true);
    previewRow->addWidget(m_preferPreviewCheck);

    // ── 安装信息 ──
    QVBoxLayout *installCard = nullptr;
    createCard(tr("安装信息"), &installCard);

    QHBoxLayout *verRow = appendRow(installCard, tr("已安装版本"), tr("当前系统中检测到的基岩版版本。"));
    m_versionValue = new QLabel(tr("-"), this);
    m_versionValue->setObjectName("settingValueLabel");
    verRow->addWidget(m_versionValue);

    QHBoxLayout *pkgRow = appendRow(installCard, tr("应用包名"), QString());
    m_packageValue = new QLabel(tr("-"), this);
    m_packageValue->setObjectName("settingValueLabel");
    pkgRow->addWidget(m_packageValue);

    QHBoxLayout *dataRow = appendRow(installCard, tr("游戏数据目录"), tr("系统基岩版游戏数据（com.mojang）所在位置。"));
    m_dataDirValue = new QLabel(tr("-"), this);
    m_dataDirValue->setObjectName("settingValueLabel");
    m_dataDirValue->setWordWrap(true);
    m_openDataDirBtn = new QPushButton(tr("打开目录"), this);
    m_openDataDirBtn->setObjectName("browseBtn");
    m_openDataDirBtn->setEnabled(false);
    dataRow->addWidget(m_dataDirValue, 1);
    dataRow->addWidget(m_openDataDirBtn);

    QPushButton *rescanBtn = new QPushButton(tr("重新检测"), this);
    rescanBtn->setObjectName("refreshBtn");
    QHBoxLayout *actionRow = appendRow(installCard, QString(), QString(), QString(), true);
    actionRow->addStretch();
    actionRow->addWidget(rescanBtn);

    // ── 数据与实例 ──
    QVBoxLayout *instanceCard = nullptr;
    createCard(tr("数据与实例"), &instanceCard);

    QHBoxLayout *instRow = appendRow(instanceCard, tr("当前激活实例"), tr("启动基岩版时使用的数据实例。"));
    m_instanceValue = new QLabel(tr("-"), this);
    m_instanceValue->setObjectName("settingValueLabel");
    instRow->addWidget(m_instanceValue);

    QHBoxLayout *instDirRow = appendRow(instanceCard, tr("实例数据目录"), QString());
    m_instanceDirValue = new QLabel(tr("-"), this);
    m_instanceDirValue->setObjectName("settingValueLabel");
    m_instanceDirValue->setWordWrap(true);
    instDirRow->addWidget(m_instanceDirValue, 1);

    QPushButton *manageBtn = new QPushButton(tr("管理实例"), this);
    manageBtn->setObjectName("browseBtn");
    QHBoxLayout *instActionRow = appendRow(instanceCard, QString(), QString(), QString(), true);
    instActionRow->addStretch();
    instActionRow->addWidget(manageBtn);

    // ── 占位伸缩，保持内容顶部对齐 ──
    layout->addStretch();

    // ── 信号连接 ──
    connect(m_closeLauncherCheck, &CustomCheckBox::toggled, this, &SettingsBedrockGamePage::saveLaunchSettings);
    connect(m_preferPreviewCheck, &CustomCheckBox::toggled, this, &SettingsBedrockGamePage::saveLaunchSettings);

    connect(rescanBtn, &QPushButton::clicked, this, [this]()
    {
        refreshInstallInfo();
    });

    connect(m_openDataDirBtn, &QPushButton::clicked, this, [this]()
    {
        BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
        const QString root = mgr->gameDataRoot();
        if (!root.isEmpty() && QDir(root).exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(root));
    });

    connect(manageBtn, &QPushButton::clicked, this, &SettingsBedrockGamePage::manageInstancesRequested);

    connect(restoreBtn, &QPushButton::clicked, this, &SettingsBedrockGamePage::restoreDefaults);

    loadLaunchSettings();
}

void SettingsBedrockGamePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 实例信息廉价，每次显示都刷新；安装信息仅首次（避免每次弹出 PowerShell 查询）
    if (!m_installInfoLoaded) {
        refreshInstallInfo();
        m_installInfoLoaded = true;
    }
    refreshInstanceInfo();
}

void SettingsBedrockGamePage::refreshInstallInfo()
{
    BedrockLauncher *bedrock = BedrockLauncher::instance();

    if (bedrock->isInstalled())
    {
        const BedrockLauncher::BedrockInfo &info = bedrock->info();
        m_versionValue->setText(info.version.isEmpty() ? tr("-") : info.version);
        m_packageValue->setText(info.name.isEmpty() ? tr("-") : info.name);
        const QString root = BedrockInstanceManager::instance()->gameDataRoot();
        m_dataDirValue->setText(root.isEmpty() ? tr("未获取到数据目录") : root);
        m_openDataDirBtn->setEnabled(!root.isEmpty() && QDir(root).exists());
    }
    else
    {
        const QString msg = bedrock->errorMessage().isEmpty()
            ? tr("未检测到已安装的基岩版") : bedrock->errorMessage();
        m_versionValue->setText(tr("未安装"));
        m_versionValue->setToolTip(msg);
        m_packageValue->setText(tr("-"));
        m_dataDirValue->setText(tr("未获取到数据目录"));
        m_openDataDirBtn->setEnabled(false);
    }
}

void SettingsBedrockGamePage::refreshInstanceInfo()
{
    BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
    mgr->ensureInitialized();

    const BedrockInstance active = mgr->activeInstance();
    if (active.id.isEmpty())
    {
        m_instanceValue->setText(tr("未选择实例"));
        m_instanceDirValue->setText(tr("-"));
        return;
    }

    m_instanceValue->setText(active.name);
    const QString dir = mgr->instanceDataDir(active.id);
    m_instanceDirValue->setText(dir);
}

void SettingsBedrockGamePage::loadLaunchSettings()
{
    SettingsManager *settings = SettingsManager::instance();
    m_closeLauncherCheck->setChecked(
        settings->getProperty(kBedrockPrefix + "closeLauncherOnLaunch", false).toBool());
    m_preferPreviewCheck->setChecked(
        settings->getProperty(kBedrockPrefix + "preferPreview", false).toBool());
}

void SettingsBedrockGamePage::saveLaunchSettings()
{
    SettingsManager *settings = SettingsManager::instance();
    settings->setProperty(kBedrockPrefix + "closeLauncherOnLaunch",
                          m_closeLauncherCheck->isChecked());
    settings->setProperty(kBedrockPrefix + "preferPreview",
                          m_preferPreviewCheck->isChecked());
}

void SettingsBedrockGamePage::restoreDefaults()
{
    SettingsManager *settings = SettingsManager::instance();
    settings->setProperty(kBedrockPrefix + "closeLauncherOnLaunch", false);
    settings->setProperty(kBedrockPrefix + "preferPreview", false);
    loadLaunchSettings();
}
