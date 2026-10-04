/**
 * @file   SettingsGeneralPage.cpp
 * @brief  通用设置页实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "components/CustomCheckBox.h"
#include "components/NotificationHistoryDialog.h"
#include "components/OutlinedLabel.h"
#include "utils/SettingsManager.h"

void SettingsPage::initGeneralSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_generalSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    OutlinedLabel *titleLabel = new OutlinedLabel(tr("常规设置"), m_generalSettings);
    titleLabel->setObjectName("sectionTitle");
    layout->addWidget(titleLabel);

    SettingsManager *settings = SettingsManager::instance();

    // ── 游戏下载源 ──
    QVBoxLayout *downloadSourceCard = createSettingsCard(layout, tr("游戏下载源"));

    QComboBox *downloadSourceCombo = new QComboBox();
    downloadSourceCombo->addItems({tr("官方源"), tr("BMCL镜像源")});
    disableWheelEffect(downloadSourceCombo);

    QHBoxLayout *downloadSourceRow = appendSettingRow(downloadSourceCard,
        tr("原版下载源"), tr("选择原版游戏下载来源。"),
        tr("选择原版游戏下载来源，BMCL镜像源在国内下载速度更快。"));
    downloadSourceRow->addWidget(downloadSourceCombo);

    CustomCheckBox *mcimCheck = new CustomCheckBox();
    QVariant mcimVal = settings->property("use_mcim");
    mcimCheck->setChecked(mcimVal.isValid() ? mcimVal.toBool() : true);
    connect(mcimCheck, &CustomCheckBox::toggled, [=](bool checked) {
        SettingsManager::instance()->setProperty("use_mcim", checked);
    });

    QHBoxLayout *mcimRow = appendSettingRow(downloadSourceCard,
        tr("MCIM 模组 API 镜像加速"), tr("将模组下载请求转发至 MCIM 加速节点，提高国内下载速度。"),
        tr("MCIM 镜像加速可显著提升国内模组下载速度。"), true);
    mcimRow->addWidget(mcimCheck);

    // ── 模组加载器下载源 ──
    QVBoxLayout *forgeSourceCard = createSettingsCard(layout, tr("模组加载器下载源"));

    m_forgeDownloadSourceCombo = new QComboBox();
    m_forgeDownloadSourceCombo->addItem(tr("官方源"), static_cast<int>(ForgeDownloadSource::Official));
    m_forgeDownloadSourceCombo->addItem(tr("BMCL镜像源"), static_cast<int>(ForgeDownloadSource::BMCL));
    disableWheelEffect(m_forgeDownloadSourceCombo);

    int currentSource = static_cast<int>(settings->getForgeDownloadSource());
    int index = m_forgeDownloadSourceCombo->findData(currentSource);
    if (index >= 0) {
        m_forgeDownloadSourceCombo->setCurrentIndex(index);
    }

    connect(m_forgeDownloadSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        SettingsManager::instance()->setForgeDownloadSource(
            static_cast<ForgeDownloadSource>(m_forgeDownloadSourceCombo->itemData(idx).toInt())
        );
    });

    QHBoxLayout *forgeSourceRow = appendSettingRow(forgeSourceCard,
        tr("Forge下载源"), tr("选择 Forge 加载器下载来源。"),
        tr("选择Forge下载来源，BMCL镜像源在国内下载速度更快。"));
    forgeSourceRow->addWidget(m_forgeDownloadSourceCombo);

    m_fabricDownloadSourceCombo = new QComboBox();
    m_fabricDownloadSourceCombo->addItem(tr("官方源"), static_cast<int>(FabricDownloadSource::Official));
    m_fabricDownloadSourceCombo->addItem(tr("BMCL镜像源"), static_cast<int>(FabricDownloadSource::BMCL));
    disableWheelEffect(m_fabricDownloadSourceCombo);

    int fabricCurrentSource = static_cast<int>(settings->getFabricDownloadSource());
    int fabricIndex = m_fabricDownloadSourceCombo->findData(fabricCurrentSource);
    if (fabricIndex >= 0) {
        m_fabricDownloadSourceCombo->setCurrentIndex(fabricIndex);
    }

    connect(m_fabricDownloadSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        SettingsManager::instance()->setFabricDownloadSource(
            static_cast<FabricDownloadSource>(m_fabricDownloadSourceCombo->itemData(idx).toInt())
        );
    });

    QHBoxLayout *fabricSourceRow = appendSettingRow(forgeSourceCard,
        tr("Fabric下载源"), tr("选择 Fabric 加载器下载来源。"),
        tr("选择Fabric下载来源，BMCL镜像源在国内下载速度更快。"));
    fabricSourceRow->addWidget(m_fabricDownloadSourceCombo);

    m_optiFineDownloadSourceCombo = new QComboBox();
    m_optiFineDownloadSourceCombo->addItem(tr("官方源"), static_cast<int>(OptiFineDownloadSource::Official));
    m_optiFineDownloadSourceCombo->addItem(tr("BMCL镜像源"), static_cast<int>(OptiFineDownloadSource::BMCL));
    disableWheelEffect(m_optiFineDownloadSourceCombo);

    int optiFineCurrentSource = static_cast<int>(settings->getOptiFineDownloadSource());
    int optiFineIndex = m_optiFineDownloadSourceCombo->findData(optiFineCurrentSource);
    if (optiFineIndex >= 0) {
        m_optiFineDownloadSourceCombo->setCurrentIndex(optiFineIndex);
    }

    connect(m_optiFineDownloadSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        SettingsManager::instance()->setOptiFineDownloadSource(
            static_cast<OptiFineDownloadSource>(m_optiFineDownloadSourceCombo->itemData(idx).toInt())
        );
    });

    QHBoxLayout *optiFineSourceRow = appendSettingRow(forgeSourceCard,
        tr("OptiFine下载源"), tr("选择 OptiFine 下载来源。"),
        tr("选择OptiFine下载来源，BMCL镜像源在国内下载速度更快。"));
    optiFineSourceRow->addWidget(m_optiFineDownloadSourceCombo);

    m_neoForgeDownloadSourceCombo = new QComboBox();
    m_neoForgeDownloadSourceCombo->addItem(tr("官方源"), static_cast<int>(NeoForgeDownloadSource::Official));
    m_neoForgeDownloadSourceCombo->addItem(tr("BMCL镜像源"), static_cast<int>(NeoForgeDownloadSource::BMCL));
    disableWheelEffect(m_neoForgeDownloadSourceCombo);

    int neoForgeCurrentSource = static_cast<int>(settings->getNeoForgeDownloadSource());
    int neoForgeIndex = m_neoForgeDownloadSourceCombo->findData(neoForgeCurrentSource);
    if (neoForgeIndex >= 0) {
        m_neoForgeDownloadSourceCombo->setCurrentIndex(neoForgeIndex);
    }

    connect(m_neoForgeDownloadSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        SettingsManager::instance()->setNeoForgeDownloadSource(
            static_cast<NeoForgeDownloadSource>(m_neoForgeDownloadSourceCombo->itemData(idx).toInt())
        );
    });

    QHBoxLayout *neoForgeSourceRow = appendSettingRow(forgeSourceCard,
        tr("NeoForge下载源"), tr("选择 NeoForge 加载器下载来源。"),
        tr("选择NeoForge下载来源，BMCL镜像源在国内下载速度更快。"), true);
    neoForgeSourceRow->addWidget(m_neoForgeDownloadSourceCombo);

    // ── 下载线程数量 ──
    QVBoxLayout *threadCountCard = createSettingsCard(layout, tr("下载线程数量"));

    int savedThreadCount = settings->getDownloadThreadCount();

    QSlider *threadCountSlider = new QSlider(Qt::Horizontal);
    threadCountSlider->setObjectName("threadCountSlider");
    threadCountSlider->setRange(1, 128);
    threadCountSlider->setValue(savedThreadCount);
    disableWheelEffect(threadCountSlider);

    QLabel *threadCountLabel = new QLabel(tr("%1 线程").arg(savedThreadCount));
    threadCountLabel->setObjectName("settingValueLabel");
    QLabel *threadCountWarning = new QLabel();
    threadCountWarning->setObjectName("settingWarningLabel");
    threadCountWarning->setVisible(savedThreadCount > 64);
    threadCountWarning->setText(tr("64个线程已经够用了，更多线程可能会造成系统卡顿"));
    connect(threadCountSlider, &QSlider::valueChanged, [=](int value) {
        threadCountLabel->setText(tr("%1 线程").arg(value));
        threadCountWarning->setVisible(value > 64);
        SettingsManager::instance()->setDownloadThreadCount(value);
    });

    QHBoxLayout *threadCountRow = appendSettingRow(threadCountCard,
        tr("下载线程数量"), tr("控制同时下载的文件数量，数值越大下载越快。"),
        tr("更高的线程数可以加快多文件下载，但会占用更多网络与磁盘资源。"), true);
    threadCountRow->addWidget(threadCountSlider, 1);
    threadCountRow->addWidget(threadCountLabel);
    threadCountRow->addWidget(threadCountWarning);

    // ── 启动器更新 ──
    QVBoxLayout *updaterCard = createSettingsCard(layout, tr("启动器更新"));

    CustomCheckBox *autoUpdateCheck = new CustomCheckBox();
    autoUpdateCheck->setChecked(true);

    QHBoxLayout *autoUpdateRow = appendSettingRow(updaterCard,
        tr("自动检查更新"), tr("启动启动器时自动检查是否有新版本可用。"));
    autoUpdateRow->addWidget(autoUpdateCheck);

    QPushButton *checkUpdateBtn = new QPushButton(tr("手动检查更新"));
    checkUpdateBtn->setObjectName("checkUpdateBtn");

    QHBoxLayout *checkUpdateRow = appendSettingRow(updaterCard,
        tr("手动检查更新"), tr("立即检查一次启动器更新。"), QString(), true);
    checkUpdateRow->addWidget(checkUpdateBtn);

    // ── 提示信息 ──
    QVBoxLayout *notificationCard = createSettingsCard(layout, tr("提示信息"));

    QPushButton *historyBtn = new QPushButton(tr("查看提示信息历史"));
    historyBtn->setObjectName("historyBtn");
    connect(historyBtn, &QPushButton::clicked, this, [this]() {
        NotificationHistoryDialog dlg(this);
        dlg.exec();
    });

    QHBoxLayout *historyRow = appendSettingRow(notificationCard,
        tr("提示信息历史"), tr("查看启动器此前发出的所有通知与提示。"), QString(), true);
    historyRow->addWidget(historyBtn);

    // ── 底部操作 ──
    QPushButton *restoreDefaultsBtn = new QPushButton(tr("恢复默认值"), m_generalSettings);
    restoreDefaultsBtn->setObjectName("restoreDefaultsBtn");
    connect(restoreDefaultsBtn, &QPushButton::clicked, this, &SettingsPage::onRestoreDefaults);

    layout->addStretch();
    layout->addWidget(restoreDefaultsBtn, 0, Qt::AlignRight);
}
