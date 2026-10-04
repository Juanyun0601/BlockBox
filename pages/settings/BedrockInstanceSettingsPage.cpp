/**
 * @file   BedrockInstanceSettingsPage.cpp
 * @brief  基岩版单个实例设置页实现
 * @author BlockBox Team
 * @date   2026-08-27
 */
#include "BedrockInstanceSettingsPage.h"

#include <QDesktopServices>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include "components/CollapsibleSectionCard.h"
#include "components/CustomCheckBox.h"
#include "components/OutlinedLabel.h"
#include "utils/SettingsManager.h"
#include "utils/bedrock/BedrockInstanceManager.h"

namespace {

const QString kInstancePrefix = QStringLiteral("bedrock/instance/");
const QString kKeyCloseLauncher = QStringLiteral("/closeLauncherOnLaunch");
const QString kKeyPreferPreview = QStringLiteral("/preferPreview");
const QString kGlobalCloseLauncher = QStringLiteral("bedrock/closeLauncherOnLaunch");
const QString kGlobalPreferPreview = QStringLiteral("bedrock/preferPreview");

QString instanceKey(const QString &instanceId, const QString &suffix)
{
    return kInstancePrefix + instanceId + suffix;
}

/**
 * @brief 统计指定目录下的子目录数量（不存在时返回 0）
 */
int countSubdirs(const QString &path)
{
    QDir dir(path);
    if (!dir.exists())
        return 0;
    return dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
}

} // namespace

BedrockInstanceSettingsPage::BedrockInstanceSettingsPage(QWidget *parent)
    : QWidget(parent)
{
    initUI();
}

BedrockInstanceSettingsPage::~BedrockInstanceSettingsPage() = default;

bool BedrockInstanceSettingsPage::closeLauncherSetting(const QString &instanceId)
{
    SettingsManager *settings = SettingsManager::instance();
    const QVariant v = settings->getProperty(instanceKey(instanceId, kKeyCloseLauncher));
    if (v.isValid())
        return v.toBool();
    return settings->getProperty(kGlobalCloseLauncher, false).toBool();
}

bool BedrockInstanceSettingsPage::preferPreviewSetting(const QString &instanceId)
{
    SettingsManager *settings = SettingsManager::instance();
    const QVariant v = settings->getProperty(instanceKey(instanceId, kKeyPreferPreview));
    if (v.isValid())
        return v.toBool();
    return settings->getProperty(kGlobalPreferPreview, false).toBool();
}

void BedrockInstanceSettingsPage::setInstanceId(const QString &instanceId)
{
    m_instanceId = instanceId;
    loadSettings();
    refreshInstanceInfo();
    refreshStorageStats();
}

QVBoxLayout *BedrockInstanceSettingsPage::createSettingsCard(QVBoxLayout *parentLayout,
                                                             const QString &title)
{
    auto *card = new CollapsibleSectionCard(title);
    parentLayout->addWidget(card);
    return card->contentLayout();
}

QHBoxLayout *BedrockInstanceSettingsPage::appendSettingRow(QVBoxLayout *cardLayout,
                                                           const QString &title,
                                                           const QString &desc,
                                                           const QString &help,
                                                           bool isLast)
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
}

void BedrockInstanceSettingsPage::initUI()
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *contentWidget = new QWidget(scrollArea);
    QVBoxLayout *layout = new QVBoxLayout(contentWidget);
    layout->setContentsMargins(16, 8, 16, 16);
    layout->setSpacing(12);

    // 顶部工具栏（与 Java 版实例游戏设置页一致）
    QHBoxLayout *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);

    QLabel *titleLabel = new QLabel(tr("实例设置"), contentWidget);
    titleLabel->setObjectName("listTitle");
    toolbar->addWidget(titleLabel);
    toolbar->addStretch();

    QPushButton *resetBtn = new QPushButton(tr("重置"), contentWidget);
    resetBtn->setObjectName("restoreDefaultsBtn");
    QPushButton *saveBtn = new QPushButton(tr("保存设置"), contentWidget);
    saveBtn->setObjectName("saveButton");

    toolbar->addWidget(resetBtn);
    toolbar->addWidget(saveBtn);
    layout->addLayout(toolbar);

    // ── 实例信息 ──
    QVBoxLayout *infoCard = createSettingsCard(layout, tr("实例信息"));

    QHBoxLayout *nameRow = appendSettingRow(infoCard, tr("实例名称"), QString());
    QLabel *nameValue = new QLabel(tr("-"), contentWidget);
    nameValue->setObjectName("settingValueLabel");
    nameRow->addWidget(nameValue);
    m_nameValue = nameValue;

    QHBoxLayout *verRow = appendSettingRow(infoCard, tr("游戏版本"), QString());
    QLabel *verValue = new QLabel(tr("-"), contentWidget);
    verValue->setObjectName("settingValueLabel");
    verRow->addWidget(verValue);
    m_versionValue = verValue;

    QHBoxLayout *lastRow = appendSettingRow(infoCard, tr("上次游玩"), QString());
    QLabel *lastValue = new QLabel(tr("-"), contentWidget);
    lastValue->setObjectName("settingValueLabel");
    lastRow->addWidget(lastValue);
    m_lastPlayedValue = lastValue;

    QHBoxLayout *dataRow = appendSettingRow(infoCard,
        tr("游戏数据目录"), tr("该实例的游戏数据存放位置。"), QString(), true);
    QLabel *dataValue = new QLabel(tr("-"), contentWidget);
    dataValue->setObjectName("settingValueLabel");
    dataValue->setWordWrap(true);
    m_openDataDirBtn = new QPushButton(tr("打开目录"), contentWidget);
    m_openDataDirBtn->setObjectName("browseBtn");
    dataRow->addWidget(dataValue, 1);
    dataRow->addWidget(m_openDataDirBtn);
    m_dataDirValue = dataValue;

    // ── 启动设置 ──
    QVBoxLayout *launchCard = createSettingsCard(layout, tr("启动设置"));

    QHBoxLayout *activeRow = appendSettingRow(launchCard,
        tr("当前激活实例"), tr("启动游戏时将读写该实例的数据目录。"));
    QLabel *activeStatus = new QLabel(tr("-"), contentWidget);
    activeStatus->setObjectName("settingValueLabel");
    m_activateBtn = new QPushButton(tr("设为当前实例"), contentWidget);
    m_activateBtn->setObjectName("browseBtn");
    activeRow->addWidget(activeStatus, 1);
    activeRow->addWidget(m_activateBtn);
    m_activeStatusValue = activeStatus;

    m_closeLauncherCheck = new CustomCheckBox("", contentWidget);
    m_closeLauncherCheck->setToolTip(tr("启动基岩版成功后自动退出本启动器。"));
    QHBoxLayout *closeRow = appendSettingRow(launchCard,
        tr("启动后自动关闭启动器"), tr("启动基岩版成功后自动退出本启动器。"),
        tr("此设置为该实例的专属覆盖，未设置时沿用全局「设置-基岩版游戏」中的对应项。"));
    closeRow->addWidget(m_closeLauncherCheck);

    m_preferPreviewCheck = new CustomCheckBox("", contentWidget);
    m_preferPreviewCheck->setToolTip(tr("已安装预览版时优先启动 Beta / Preview 版本。"));
    QHBoxLayout *previewRow = appendSettingRow(launchCard,
        tr("优先启动预览版"), tr("已安装预览版时优先启动 Beta / Preview 版本。"),
        tr("基岩版在微软商店中区分正式版与预览版（Preview/Beta），"
           "启用后启动该实例时优先启动预览版。未设置时沿用全局设置。"), true);
    previewRow->addWidget(m_preferPreviewCheck);

    // ── 数据与存储 ──
    QVBoxLayout *storageCard = createSettingsCard(layout, tr("数据与存储"));

    QHBoxLayout *mojangRow = appendSettingRow(storageCard,
        tr("com.mojang 目录"), tr("基岩版游戏实际读写的数据目录。"));
    QLabel *mojangValue = new QLabel(tr("-"), contentWidget);
    mojangValue->setObjectName("settingValueLabel");
    mojangValue->setWordWrap(true);
    m_openMojangBtn = new QPushButton(tr("打开"), contentWidget);
    m_openMojangBtn->setObjectName("browseBtn");
    mojangRow->addWidget(mojangValue, 1);
    mojangRow->addWidget(m_openMojangBtn);
    m_mojangDirValue = mojangValue;

    QHBoxLayout *worldsRow = appendSettingRow(storageCard,
        tr("世界"), tr("minecraftWorlds 目录中的世界存档数量。"));
    QLabel *worldsValue = new QLabel(tr("--"), contentWidget);
    worldsValue->setObjectName("settingValueLabel");
    worldsRow->addWidget(worldsValue);
    m_worldsCountValue = worldsValue;

    QHBoxLayout *rpRow = appendSettingRow(storageCard,
        tr("资源包"), QString());
    QLabel *rpValue = new QLabel(tr("--"), contentWidget);
    rpValue->setObjectName("settingValueLabel");
    rpRow->addWidget(rpValue);
    m_resourcePacksCountValue = rpValue;

    QHBoxLayout *bpRow = appendSettingRow(storageCard,
        tr("行为包"), QString());
    QLabel *bpValue = new QLabel(tr("--"), contentWidget);
    bpValue->setObjectName("settingValueLabel");
    bpRow->addWidget(bpValue);
    m_behaviorPacksCountValue = bpValue;

    QHBoxLayout *spRow = appendSettingRow(storageCard,
        tr("皮肤包"), QString(), QString(), true);
    QLabel *spValue = new QLabel(tr("--"), contentWidget);
    spValue->setObjectName("settingValueLabel");
    spRow->addWidget(spValue);
    m_skinPacksCountValue = spValue;

    QPushButton *rescanBtn = new QPushButton(tr("重新统计"), contentWidget);
    rescanBtn->setObjectName("refreshBtn");
    m_rescanBtn = rescanBtn;

    layout->addStretch();
    layout->addWidget(rescanBtn, 0, Qt::AlignRight);

    // ── 信号连接 ──
    m_loading = true;
    connect(m_closeLauncherCheck, &CustomCheckBox::toggled, this, [this]() {
        if (m_loading)
            return;
        saveSettings();
        emit settingChanged();
    });
    connect(m_preferPreviewCheck, &CustomCheckBox::toggled, this, [this]() {
        if (m_loading)
            return;
        saveSettings();
        emit settingChanged();
    });
    m_loading = false;

    connect(resetBtn, &QPushButton::clicked, this, &BedrockInstanceSettingsPage::restoreDefaults);
    connect(saveBtn, &QPushButton::clicked, this, &BedrockInstanceSettingsPage::saveSettings);

    connect(m_openDataDirBtn, &QPushButton::clicked, this, [this]() {
        const BedrockInstance inst = BedrockInstanceManager::instance()->instanceById(m_instanceId);
        if (!inst.dataDir.isEmpty() && QDir(inst.dataDir).exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(inst.dataDir));
    });

    connect(m_activateBtn, &QPushButton::clicked, this, [this]() {
        if (!m_instanceId.isEmpty())
            emit activateInstanceRequested(m_instanceId);
    });

    connect(m_openMojangBtn, &QPushButton::clicked, this, [this]() {
        const BedrockInstance inst = BedrockInstanceManager::instance()->instanceById(m_instanceId);
        const QString dir = inst.dataDir + QStringLiteral("/com.mojang");
        if (QDir(dir).exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    connect(m_rescanBtn, &QPushButton::clicked, this, &BedrockInstanceSettingsPage::refreshStorageStats);

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);
}

void BedrockInstanceSettingsPage::loadSettings()
{
    m_loading = true;

    m_closeLauncherCheck->setChecked(closeLauncherSetting(m_instanceId));
    m_preferPreviewCheck->setChecked(preferPreviewSetting(m_instanceId));

    m_loading = false;
}

void BedrockInstanceSettingsPage::saveSettings()
{
    if (m_instanceId.isEmpty())
        return;

    SettingsManager *settings = SettingsManager::instance();
    settings->setProperty(instanceKey(m_instanceId, kKeyCloseLauncher),
                          m_closeLauncherCheck->isChecked());
    settings->setProperty(instanceKey(m_instanceId, kKeyPreferPreview),
                          m_preferPreviewCheck->isChecked());
}

void BedrockInstanceSettingsPage::restoreDefaults()
{
    if (m_instanceId.isEmpty())
        return;

    SettingsManager *settings = SettingsManager::instance();
    settings->removeProperty(instanceKey(m_instanceId, kKeyCloseLauncher));
    settings->removeProperty(instanceKey(m_instanceId, kKeyPreferPreview));
    loadSettings();
    emit settingChanged();
}

void BedrockInstanceSettingsPage::refreshInstanceInfo()
{
    BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
    const BedrockInstance inst = mgr->instanceById(m_instanceId);

    if (m_nameValue)
        m_nameValue->setText(inst.id.isEmpty() ? tr("-") : inst.name);

    if (m_versionValue)
        m_versionValue->setText(inst.version.isEmpty() ? tr("未检测到") : inst.version);

    if (m_lastPlayedValue)
        m_lastPlayedValue->setText(inst.lastPlayed.isValid()
                                      ? inst.lastPlayed.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                      : tr("从未游玩"));

    if (m_dataDirValue) {
        m_dataDirValue->setText(inst.dataDir.isEmpty() ? tr("-") : inst.dataDir);
        if (m_openDataDirBtn)
            m_openDataDirBtn->setEnabled(!inst.dataDir.isEmpty() && QDir(inst.dataDir).exists());
    }

    if (m_openMojangBtn) {
        const QString mojangDir = inst.dataDir + QStringLiteral("/com.mojang");
        m_openMojangBtn->setEnabled(!inst.dataDir.isEmpty() && QDir(mojangDir).exists());
    }

    const bool isActive = !inst.id.isEmpty() && inst.id == mgr->activeInstanceId();
    if (m_activeStatusValue)
        m_activeStatusValue->setText(isActive ? tr("启用中") : tr("未激活"));
}

void BedrockInstanceSettingsPage::refreshStorageStats()
{
    const BedrockInstance inst = BedrockInstanceManager::instance()->instanceById(m_instanceId);
    const QString mojangDir = inst.dataDir + QStringLiteral("/com.mojang");

    const int worlds = countSubdirs(mojangDir + QStringLiteral("/minecraftWorlds"));
    const int rp = countSubdirs(mojangDir + QStringLiteral("/resource_packs"));
    const int bp = countSubdirs(mojangDir + QStringLiteral("/behavior_packs"));
    const int sp = countSubdirs(mojangDir + QStringLiteral("/skin_packs"));

    if (m_worldsCountValue)
        m_worldsCountValue->setText(QString::number(worlds));
    if (m_resourcePacksCountValue)
        m_resourcePacksCountValue->setText(QString::number(rp));
    if (m_behaviorPacksCountValue)
        m_behaviorPacksCountValue->setText(QString::number(bp));
    if (m_skinPacksCountValue)
        m_skinPacksCountValue->setText(QString::number(sp));
}