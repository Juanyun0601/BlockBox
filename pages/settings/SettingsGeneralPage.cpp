/**
 * @file   SettingsGeneralPage.cpp
 * @brief  通用设置页实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "pages/SettingsPage.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "components/CustomCheckBox.h"
#include "components/NotificationHistoryDialog.h"
#include "components/NotificationManager.h"
#include "components/OutlinedLabel.h"
#include "components/QuizDialog.h"
#include "pages/OnboardingWizard.h"
#include "platform.h"
#include "utils/QuizBank.h"
#include "utils/SettingsManager.h"
#include "utils/UpdateChecker.h"
#include "utils/WorldMap/StructureIcons.h"

#include <QCoreApplication>
#include <QProcess>

void SettingsPage::initGeneralSettings()
{
    QVBoxLayout *layout = new QVBoxLayout(m_generalSettings);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    // ── 标题栏（标题 + 恢复默认值按钮）──
    createSettingsHeader(layout, tr("常规设置"));

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

    QHBoxLayout *cfKeyRow = appendSettingRow(downloadSourceCard,
        tr("CurseForge API Key"), tr("直连 CurseForge 官方源时所需的密钥，启用 MCIM 镜像加速时可不填。"),
        tr("在 CurseForge 官网个人后台申请；密钥仅保存在本机设置中，不会随启动器分发或上传。官方源必须填入，MCIM 镜像加速开启时无需填入即可使用。"));
    QLineEdit *cfKeyEdit = new QLineEdit();
    cfKeyEdit->setEchoMode(QLineEdit::Password);
    cfKeyEdit->setPlaceholderText(tr("填入你的 CurseForge API Key"));
    cfKeyEdit->setText(settings->property("curseforge_api_key").toString());
    connect(cfKeyEdit, &QLineEdit::editingFinished, cfKeyEdit, [cfKeyEdit]() {
        SettingsManager::instance()->setProperty("curseforge_api_key", cfKeyEdit->text().trimmed());
    });
    cfKeyRow->addWidget(cfKeyEdit);

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

    // ── 文件选择器 ──
    const bool mobilePicker = Platform::isMobile();
    QVBoxLayout *pickerCard = createSettingsCard(layout, tr("文件选择器"));

    QHBoxLayout *pickerTypeRow = appendSettingRow(pickerCard,
        tr("选择器类型"),
        mobilePicker ? tr("手机端固定使用软件自带的文件浏览器。")
                     : tr("选择打开文件、文件夹时使用的窗口，电脑端默认使用系统自带。"),
        tr("系统自带会打开操作系统提供的文件选择窗口；软件自带使用启动器内置的文件浏览器，"
           "可以直接浏览实例里的模组、资源包、光影包、存档等文件。手机端固定使用软件自带。"),
        true);

    if (mobilePicker) {
        QLabel *fixedPickerLabel = new QLabel(tr("软件自带"));
        fixedPickerLabel->setObjectName("settingValueLabel");
        pickerTypeRow->addWidget(fixedPickerLabel);
    } else {
        QComboBox *pickerTypeCombo = new QComboBox();
        pickerTypeCombo->addItem(tr("系统自带（文件资源管理器）"),
                                 static_cast<int>(FilePickerType::System));
        pickerTypeCombo->addItem(tr("软件自带（内置文件浏览器）"),
                                 static_cast<int>(FilePickerType::BuiltIn));
        disableWheelEffect(pickerTypeCombo);

        int pickerIndex = pickerTypeCombo->findData(
            static_cast<int>(settings->getFilePickerType()));
        if (pickerIndex < 0)
            pickerIndex = pickerTypeCombo->findData(
                static_cast<int>(SettingsManager::defaultFilePickerType()));
        pickerTypeCombo->setCurrentIndex(pickerIndex >= 0 ? pickerIndex : 0);

        connect(pickerTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [pickerTypeCombo](int idx) {
            SettingsManager::instance()->setFilePickerType(
                static_cast<FilePickerType>(pickerTypeCombo->itemData(idx).toInt()));
        });
        pickerTypeRow->addWidget(pickerTypeCombo);
    }

    // ── 地图结构图标：文字旁小图标（默认） / 结构概览 ──
    QVBoxLayout *mapIconCard = createSettingsCard(layout, tr("地图结构图标"));

    QComboBox *mapIconCombo = new QComboBox();
    mapIconCombo->addItem(tr("图标（文字旁小图标）"),
                          static_cast<int>(MapStructureIconStyle::Icon));
    mapIconCombo->addItem(tr("结构概览（页面主图）"),
                          static_cast<int>(MapStructureIconStyle::Overview));
    disableWheelEffect(mapIconCombo);

    int mapIconIndex = mapIconCombo->findData(
        static_cast<int>(settings->getMapStructureIconStyle()));
    mapIconCombo->setCurrentIndex(mapIconIndex >= 0 ? mapIconIndex : 0);

    connect(mapIconCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [mapIconCombo](int idx) {
        const auto style = static_cast<MapStructureIconStyle>(
            mapIconCombo->itemData(idx).toInt());
        SettingsManager::instance()->setMapStructureIconStyle(style);
        // 立即生效：清内存缓存并通知已打开的地图页重取图标
        StructureIcons::setStyle(style == MapStructureIconStyle::Overview
                                     ? StructureIcons::Style::Overview
                                     : StructureIcons::Style::Icon);
    });

    QHBoxLayout *mapIconRow = appendSettingRow(mapIconCard,
        tr("地图结构图标"), tr("存档地图页第二行结构开关与地图标记使用的图标。"),
        tr("图标为生成结构表中文字旁的 16px 小图标（默认）；结构概览为 Wiki 条目页面的主图。离线时回退为内置徽章。"), true);
    mapIconRow->addWidget(mapIconCombo);

    // ── 启动行为 ──
    QVBoxLayout *startupCard = createSettingsCard(layout, tr("启动行为"));

    CustomCheckBox *autoStartCheck = new CustomCheckBox();
    autoStartCheck->setChecked(settings->isAutoStartEnabled());
    connect(autoStartCheck, &CustomCheckBox::toggled, [](bool checked) {
        SettingsManager::instance()->setAutoStartEnabled(checked);
    });

    QHBoxLayout *autoStartRow = appendSettingRow(startupCard,
        tr("开机自启动"), tr("登录系统后自动启动方块盒子。"),
        tr("Windows 上写入当前用户注册表 Run 键，macOS 写入 LaunchAgents，Linux 写入 autostart 目录；关闭时移除对应条目。"), true);
    autoStartRow->addWidget(autoStartCheck);

    // ── 启动器更新 ──
    QVBoxLayout *updaterCard = createSettingsCard(layout, tr("启动器更新"));

    QLabel *versionValue = new QLabel(AppVersion::current());
    versionValue->setObjectName("settingValueLabel");

    QHBoxLayout *versionRow = appendSettingRow(updaterCard,
        tr("当前版本"), tr("启动器当前安装的版本号。"));
    versionRow->addWidget(versionValue);

    CustomCheckBox *autoUpdateCheck = new CustomCheckBox();
    autoUpdateCheck->setChecked(settings->getProperty("auto_check_update", true).toBool());
    connect(autoUpdateCheck, &CustomCheckBox::toggled, [](bool checked) {
        SettingsManager::instance()->setProperty("auto_check_update", checked);
    });

    QHBoxLayout *autoUpdateRow = appendSettingRow(updaterCard,
        tr("自动检查更新"), tr("启动启动器时自动检查是否有新版本可用。"));
    autoUpdateRow->addWidget(autoUpdateCheck);

    // ── 升级通道：抢先升级看测试版+正式版，保守升级仅看正式版 ──
    QComboBox *channelCombo = new QComboBox();
    channelCombo->addItem(tr("抢先升级（测试版与正式版）"), QStringLiteral("beta"));
    channelCombo->addItem(tr("保守升级（仅正式版）"), QStringLiteral("stable"));
    disableWheelEffect(channelCombo);

    const QString channel = settings->getProperty("update_channel", QStringLiteral("beta")).toString();
    const int channelIndex = channelCombo->findData(channel);
    if (channelIndex >= 0)
        channelCombo->setCurrentIndex(channelIndex);
    connect(channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [channelCombo](int index) {
        SettingsManager::instance()->setProperty("update_channel",
                                                 channelCombo->itemData(index).toString());
    });

    QHBoxLayout *channelRow = appendSettingRow(updaterCard,
        tr("升级通道"), tr("抢先升级可第一时间体验测试版，保守升级仅在正式版发布时提示更新。"),
        tr("测试版版本号形如 1.0.0-beta1，正式版形如 1.0.0。测试版包含新功能但可能不稳定，追求稳定请选择保守升级。"), true);
    channelRow->addWidget(channelCombo);

    QPushButton *checkUpdateBtn = new QPushButton(tr("手动检查更新"));
    checkUpdateBtn->setObjectName("checkUpdateBtn");
    connect(checkUpdateBtn, &QPushButton::clicked, this, [this, checkUpdateBtn]() {
        UpdateChecker *checker = UpdateChecker::instance();
        if (checker->isChecking())
            return;
        checkUpdateBtn->setEnabled(false);
        checkUpdateBtn->setText(tr("检查中…"));
        checker->checkForUpdates(this, false);
    });
    connect(UpdateChecker::instance(), &UpdateChecker::checkFinished, this,
            [checkUpdateBtn](bool, const QString &) {
        checkUpdateBtn->setEnabled(true);
        checkUpdateBtn->setText(tr("手动检查更新"));
    });

    QHBoxLayout *checkUpdateRow = appendSettingRow(updaterCard,
        tr("手动检查更新"), tr("立即检查一次启动器更新。"), QString(), true);
    checkUpdateRow->addWidget(checkUpdateBtn);

    // ── 新手引导 ──
    QVBoxLayout *onboardingCard = createSettingsCard(layout, tr("新手引导"));

    QPushButton *openGuideBtn = new QPushButton(tr("打开新手引导"));
    openGuideBtn->setObjectName("historyBtn");
    openGuideBtn->setCursor(Qt::PointingHandCursor);
    connect(openGuideBtn, &QPushButton::clicked, this, [this]() {
        QWidget *host = window();
        OnboardingWizard wizard(host);
        wizard.exec();
        if (wizard.restartRequested() && host) {
            host->hide();
            QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList());
            QCoreApplication::quit();
        }
    });

    QHBoxLayout *onboardingRow = appendSettingRow(onboardingCard,
        tr("新手引导"), tr("重新打开首次启动时的配置向导，可重新设置语言、下载源、账户、Java 与实例文件夹等。"), QString(), true);
    onboardingRow->addWidget(openGuideBtn);

    // ── 入站答题 ──
    QVBoxLayout *quizCard = createSettingsCard(layout, tr("入站答题"));

    QPushButton *startQuizBtn = new QPushButton(
        QuizBank::hasPassed() ? tr("再答一次") : tr("开始答题"));
    startQuizBtn->setObjectName("historyBtn");
    startQuizBtn->setCursor(Qt::PointingHandCursor);
    connect(startQuizBtn, &QPushButton::clicked, this, [this, startQuizBtn]() {
        QWidget *host = window();
        QuizDialog dialog(host);
        if (dialog.exec() == QDialog::Accepted) {
            startQuizBtn->setText(tr("再答一次"));
            if (host)
                NotificationManager::showSuccess(
                    host, tr("已通过入站答题，分数 %1 分。").arg(QuizBank::lastScore()), 6000);
        }
    });

    QHBoxLayout *quizRow = appendSettingRow(quizCard,
        tr("入站答题"), tr("从题库随机抽 50 题，每题 2 分，达到 80 分即通过；答错会显示正确答案与解析。"), QString(), true);
    quizRow->addWidget(startQuizBtn);

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

    // ── 占位伸缩，保持内容顶部对齐 ──
    layout->addStretch();
}
