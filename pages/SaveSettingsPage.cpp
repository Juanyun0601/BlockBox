/**
 * @file   SaveSettingsPage.cpp
 * @brief  存档设置页面实现
 * @author BlockBox Team
 * @date   2026-08-01
 */

#include "SaveSettingsPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include "components/AppMessageBox.h"
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

namespace {

QString gameModeName(int gameType)
{
    switch (gameType)
    {
    case 0: return QObject::tr("生存模式");
    case 1: return QObject::tr("创造模式");
    case 2: return QObject::tr("冒险模式");
    case 3: return QObject::tr("观察者模式");
    default: return QObject::tr("未知");
    }
}

QString formatTicksAsDuration(qint64 ticks)
{
    // 20 游戏刻 = 1 秒
    qint64 totalSeconds = ticks / 20;
    qint64 days = totalSeconds / 86400;
    qint64 hours = (totalSeconds % 86400) / 3600;
    qint64 minutes = (totalSeconds % 3600) / 60;
    if (days > 0)
        return QObject::tr("%1天 %2小时 %3分钟").arg(days).arg(hours).arg(minutes);
    if (hours > 0)
        return QObject::tr("%1小时 %2分钟").arg(hours).arg(minutes);
    return QObject::tr("%1分钟").arg(minutes);
}

// 将 DayTime 游戏刻转为可读的「小时:分钟」表示（0 刻 = 6:00）
QString formatDayTimeClock(qint64 ticks)
{
    const qint64 t = ((ticks % 24000) + 24000) % 24000;
    qint64 hour = (t / 1000 + 6) % 24;
    qint64 minute = ((t % 1000) * 60) / 1000;
    return QStringLiteral("%1:%2")
        .arg(hour, 2, 10, QLatin1Char('0'))
        .arg(minute, 2, 10, QLatin1Char('0'));
}

} // namespace

SaveSettingsPage::SaveSettingsPage(QWidget *parent)
    : QWidget(parent)
    , m_saveNameLabel(nullptr)
    , m_infoGrid(nullptr)
    , m_versionLabel(nullptr)
    , m_seedLabel(nullptr)
    , m_lastPlayedLabel(nullptr)
    , m_spawnLabel(nullptr)
    , m_gameModeLabel(nullptr)
    , m_playTimeLabel(nullptr)
    , m_dayTimeLabel(nullptr)
    , m_worldNameEdit(nullptr)
    , m_gameTypeCombo(nullptr)
    , m_allowCommandsCombo(nullptr)
    , m_difficultyCombo(nullptr)
    , m_lockDifficultyCheck(nullptr)
    , m_hardcoreCheck(nullptr)
    , m_dayTimeCombo(nullptr)
    , m_spawnXSpin(nullptr)
    , m_spawnYSpin(nullptr)
    , m_spawnZSpin(nullptr)
    , m_hardcoreHint(nullptr)
    , m_copySeedBtn(nullptr)
    , m_chunkbaseBtn(nullptr)
    , m_minecraftSearchBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_openFolderBtn(nullptr)
{
    initUI();
}

SaveSettingsPage::~SaveSettingsPage()
{
}

void SaveSettingsPage::initUI()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *contentWidget = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(contentWidget);
    layout->setContentsMargins(20, 12, 20, 20);
    layout->setSpacing(12);

    // 存档名称
    m_saveNameLabel = new QLabel(m_saveName, contentWidget);
    QFont nameFont = m_saveNameLabel->font();
    nameFont.setBold(true);
    nameFont.setPixelSize(17);
    m_saveNameLabel->setFont(nameFont);
    layout->addWidget(m_saveNameLabel);

    // ── 存档信息卡片 ──
    QWidget *infoCard = new QWidget(contentWidget);
    infoCard->setObjectName("funcCard");
    auto *infoCardLayout = new QVBoxLayout(infoCard);
    infoCardLayout->setContentsMargins(16, 16, 16, 16);
    infoCardLayout->setSpacing(10);

    auto *infoHeader = new QLabel(tr("存档信息"), infoCard);
    QFont headerFont = infoHeader->font();
    headerFont.setBold(true);
    headerFont.setPixelSize(13);
    infoHeader->setFont(headerFont);
    infoCardLayout->addWidget(infoHeader);

    m_infoGrid = new QGridLayout();
    m_infoGrid->setHorizontalSpacing(16);
    m_infoGrid->setVerticalSpacing(8);
    m_infoGrid->setColumnStretch(1, 1);
    infoCardLayout->addLayout(m_infoGrid);

    m_versionLabel = new QLabel(infoCard);
    m_versionLabel->setObjectName("saveInfoValue");

    m_seedLabel = new QLabel(infoCard);
    m_seedLabel->setObjectName("saveInfoValue");
    m_seedLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_lastPlayedLabel = new QLabel(infoCard);
    m_lastPlayedLabel->setObjectName("saveInfoValue");

    m_spawnLabel = new QLabel(infoCard);
    m_spawnLabel->setObjectName("saveInfoValue");

    m_gameModeLabel = new QLabel(infoCard);
    m_gameModeLabel->setObjectName("saveInfoValue");

    m_playTimeLabel = new QLabel(infoCard);
    m_playTimeLabel->setObjectName("saveInfoValue");

    m_dayTimeLabel = new QLabel(infoCard);
    m_dayTimeLabel->setObjectName("saveInfoValue");

    // 种子行：数值 + 复制 + Chunkbase
    auto *seedRow = new QHBoxLayout();
    seedRow->setSpacing(6);
    seedRow->addWidget(m_seedLabel, 1);
    m_copySeedBtn = new QPushButton(tr("复制"), infoCard);
    m_copySeedBtn->setObjectName("bottomActionBtn");
    m_copySeedBtn->setCursor(Qt::PointingHandCursor);
    m_copySeedBtn->setToolTip(tr("复制种子"));
    connect(m_copySeedBtn, &QPushButton::clicked, this, &SaveSettingsPage::onCopySeedClicked);
    seedRow->addWidget(m_copySeedBtn);
    m_chunkbaseBtn = new QPushButton(tr("Chunkbase"), infoCard);
    m_chunkbaseBtn->setObjectName("bottomActionBtn");
    m_chunkbaseBtn->setCursor(Qt::PointingHandCursor);
    m_chunkbaseBtn->setToolTip(tr("在 Chunkbase 网站查看种子地形"));
    connect(m_chunkbaseBtn, &QPushButton::clicked, this, &SaveSettingsPage::onOpenChunkbaseClicked);
    seedRow->addWidget(m_chunkbaseBtn);

    m_minecraftSearchBtn = new QPushButton(tr("种子地图"), infoCard);
    m_minecraftSearchBtn->setObjectName("bottomActionBtn");
    m_minecraftSearchBtn->setCursor(Qt::PointingHandCursor);
    m_minecraftSearchBtn->setToolTip(tr("在 MinecraftSearch 查看此种子地图"));
    connect(m_minecraftSearchBtn, &QPushButton::clicked, this, &SaveSettingsPage::onOpenMinecraftSearchClicked);
    seedRow->addWidget(m_minecraftSearchBtn);

    addInfoRow(tr("版本"), m_versionLabel);
    {
        auto *seedRowWidget = new QWidget(infoCard);
        seedRowWidget->setLayout(seedRow);
        addInfoRow(tr("种子"), seedRowWidget);
    }
    addInfoRow(tr("最后游玩"), m_lastPlayedLabel);
    addInfoRow(tr("出生点"), m_spawnLabel);
    addInfoRow(tr("游戏模式"), m_gameModeLabel);
    addInfoRow(tr("游玩时长"), m_playTimeLabel);
    addInfoRow(tr("游戏内时间"), m_dayTimeLabel);

    layout->addWidget(infoCard);

    // ── 设置卡片 ──
    QWidget *settingCard = new QWidget(contentWidget);
    settingCard->setObjectName("funcCard");
    auto *settingCardLayout = new QVBoxLayout(settingCard);
    settingCardLayout->setContentsMargins(16, 16, 16, 16);
    settingCardLayout->setSpacing(10);

    auto *settingHeader = new QLabel(tr("设置"), settingCard);
    settingHeader->setFont(headerFont);
    settingCardLayout->addWidget(settingHeader);

    // 世界名称
    auto *worldNameLabel = new QLabel(tr("世界名称"), settingCard);
    m_worldNameEdit = new QLineEdit(settingCard);
    m_worldNameEdit->setPlaceholderText(tr("存档目录名"));
    m_worldNameEdit->setToolTip(tr("修改后将重命名存档文件夹并同步 level.dat"));
    auto *worldNameRow = new QHBoxLayout();
    worldNameRow->setSpacing(12);
    worldNameRow->addWidget(worldNameLabel);
    worldNameRow->addWidget(m_worldNameEdit, 1);
    settingCardLayout->addLayout(worldNameRow);

    // 游戏模式
    auto *gameTypeLabel = new QLabel(tr("游戏模式"), settingCard);
    m_gameTypeCombo = new QComboBox(settingCard);
    m_gameTypeCombo->addItem(tr("生存模式"), 0);
    m_gameTypeCombo->addItem(tr("创造模式"), 1);
    m_gameTypeCombo->addItem(tr("冒险模式"), 2);
    m_gameTypeCombo->addItem(tr("观察者模式"), 3);
    auto *gameTypeRow = new QHBoxLayout();
    gameTypeRow->setSpacing(12);
    gameTypeRow->addWidget(gameTypeLabel);
    gameTypeRow->addWidget(m_gameTypeCombo);
    gameTypeRow->addStretch();
    settingCardLayout->addLayout(gameTypeRow);

    // 允许作弊
    auto *allowCommandsLabel = new QLabel(tr("允许作弊"), settingCard);
    m_allowCommandsCombo = new QComboBox(settingCard);
    m_allowCommandsCombo->addItem(tr("关闭"), 0);
    m_allowCommandsCombo->addItem(tr("开启"), 1);
    auto *allowRow = new QHBoxLayout();
    allowRow->setSpacing(12);
    allowRow->addWidget(allowCommandsLabel);
    allowRow->addWidget(m_allowCommandsCombo);
    allowRow->addStretch();
    settingCardLayout->addLayout(allowRow);

    // 难度 + 锁定
    auto *difficultyLabel = new QLabel(tr("难度"), settingCard);
    m_difficultyCombo = new QComboBox(settingCard);
    m_difficultyCombo->addItem(tr("和平"), 0);
    m_difficultyCombo->addItem(tr("简单"), 1);
    m_difficultyCombo->addItem(tr("普通"), 2);
    m_difficultyCombo->addItem(tr("困难"), 3);

    m_lockDifficultyCheck = new QCheckBox(tr("锁定难度"), settingCard);
    m_lockDifficultyCheck->setToolTip(tr("锁定后无法在游戏内修改难度"));
    connect(m_lockDifficultyCheck, &QCheckBox::toggled,
            this, &SaveSettingsPage::onHardcoreToggled);

    auto *difficultyRow = new QHBoxLayout();
    difficultyRow->setSpacing(12);
    difficultyRow->addWidget(difficultyLabel);
    difficultyRow->addWidget(m_difficultyCombo);
    difficultyRow->addWidget(m_lockDifficultyCheck);
    difficultyRow->addStretch();
    settingCardLayout->addLayout(difficultyRow);

    // 极限模式
    auto *hardcoreLabel = new QLabel(tr("极限模式"), settingCard);
    m_hardcoreCheck = new QCheckBox(tr("开启"), settingCard);
    m_hardcoreCheck->setToolTip(tr("极限模式：难度固定为困难并锁定，死亡即删除存档"));
    connect(m_hardcoreCheck, &QCheckBox::toggled,
            this, &SaveSettingsPage::onHardcoreToggled);
    auto *hardcoreRow = new QHBoxLayout();
    hardcoreRow->setSpacing(12);
    hardcoreRow->addWidget(hardcoreLabel);
    hardcoreRow->addWidget(m_hardcoreCheck);
    hardcoreRow->addStretch();
    settingCardLayout->addLayout(hardcoreRow);

    m_hardcoreHint = new QLabel(tr("极限模式存档的难度固定为困难，无法修改。"), settingCard);
    m_hardcoreHint->setStyleSheet(QStringLiteral("color: #888888; font-size: 12px;"));
    m_hardcoreHint->hide();
    settingCardLayout->addWidget(m_hardcoreHint);

    // 游戏内时间
    auto *dayTimeLabel = new QLabel(tr("游戏内时间"), settingCard);
    m_dayTimeCombo = new QComboBox(settingCard);
    m_dayTimeCombo->addItem(tr("6:00（黎明）"), 0);
    m_dayTimeCombo->addItem(tr("8:00（清晨）"), 2000);
    m_dayTimeCombo->addItem(tr("12:00（正午）"), 6000);
    m_dayTimeCombo->addItem(tr("18:00（黄昏）"), 12000);
    m_dayTimeCombo->addItem(tr("0:00（午夜）"), 18000);
    auto *dayTimeRow = new QHBoxLayout();
    dayTimeRow->setSpacing(12);
    dayTimeRow->addWidget(dayTimeLabel);
    dayTimeRow->addWidget(m_dayTimeCombo);
    dayTimeRow->addStretch();
    settingCardLayout->addLayout(dayTimeRow);

    // 出生点坐标
    auto *spawnLabel = new QLabel(tr("出生点"), settingCard);
    m_spawnXSpin = new QSpinBox(settingCard);
    m_spawnYSpin = new QSpinBox(settingCard);
    m_spawnZSpin = new QSpinBox(settingCard);
    const int spawnRange = 30000000;
    for (QSpinBox *spin : { m_spawnXSpin, m_spawnYSpin, m_spawnZSpin })
    {
        spin->setRange(-spawnRange, spawnRange);
        spin->setFixedWidth(110);
    }
    auto *spawnRow = new QHBoxLayout();
    spawnRow->setSpacing(8);
    spawnRow->addWidget(spawnLabel);
    spawnRow->addWidget(new QLabel(tr("X"), settingCard));
    spawnRow->addWidget(m_spawnXSpin);
    spawnRow->addWidget(new QLabel(tr("Y"), settingCard));
    spawnRow->addWidget(m_spawnYSpin);
    spawnRow->addWidget(new QLabel(tr("Z"), settingCard));
    spawnRow->addWidget(m_spawnZSpin);
    spawnRow->addStretch();
    settingCardLayout->addLayout(spawnRow);

    layout->addWidget(settingCard);
    layout->addStretch();

    // ── 底部按钮 ──
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(10);
    m_openFolderBtn = new QPushButton(tr("打开存档文件夹"), contentWidget);
    m_openFolderBtn->setObjectName("bottomActionBtn");
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &SaveSettingsPage::onOpenFolderClicked);
    bottomRow->addWidget(m_openFolderBtn);
    bottomRow->addStretch();

    m_saveBtn = new QPushButton(tr("保存修改"), contentWidget);
    m_saveBtn->setObjectName("addFirstAccountBtn");
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveBtn, &QPushButton::clicked, this, &SaveSettingsPage::onSaveClicked);
    bottomRow->addWidget(m_saveBtn);
    layout->addLayout(bottomRow);

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);

    // 初始占位
    m_versionLabel->setText(tr("—"));
    m_seedLabel->setText(tr("—"));
    m_lastPlayedLabel->setText(tr("—"));
    m_spawnLabel->setText(tr("—"));
    m_gameModeLabel->setText(tr("—"));
    m_playTimeLabel->setText(tr("—"));
    m_dayTimeLabel->setText(tr("—"));
    m_copySeedBtn->setEnabled(false);
    m_chunkbaseBtn->setEnabled(false);
    m_minecraftSearchBtn->setEnabled(false);
    m_worldNameEdit->setEnabled(false);
    m_gameTypeCombo->setEnabled(false);
    m_allowCommandsCombo->setEnabled(false);
    m_difficultyCombo->setEnabled(false);
    m_lockDifficultyCheck->setEnabled(false);
    m_hardcoreCheck->setEnabled(false);
    m_dayTimeCombo->setEnabled(false);
    m_spawnXSpin->setEnabled(false);
    m_spawnYSpin->setEnabled(false);
    m_spawnZSpin->setEnabled(false);
    m_saveBtn->setEnabled(false);
}

void SaveSettingsPage::setCurrentSave(const QString &saveFolderPath, const QString &saveName)
{
    m_saveFolderPath = saveFolderPath;
    m_saveName = saveName;
    m_saveNameLabel->setText(saveName);
    m_loaded = false;
    loadSaveInfo();
}

void SaveSettingsPage::reset()
{
    m_saveFolderPath.clear();
    m_saveName.clear();
    m_loaded = false;
    m_saveNameLabel->clear();
    m_versionLabel->setText(tr("—"));
    m_seedLabel->setText(tr("—"));
    m_lastPlayedLabel->setText(tr("—"));
    m_spawnLabel->setText(tr("—"));
    m_gameModeLabel->setText(tr("—"));
    m_playTimeLabel->setText(tr("—"));
    m_dayTimeLabel->setText(tr("—"));
    m_copySeedBtn->setEnabled(false);
    m_chunkbaseBtn->setEnabled(false);
    m_minecraftSearchBtn->setEnabled(false);
    m_worldNameEdit->clear();
    m_worldNameEdit->setEnabled(false);
    m_gameTypeCombo->setCurrentIndex(0);
    m_gameTypeCombo->setEnabled(false);
    m_allowCommandsCombo->setCurrentIndex(0);
    m_allowCommandsCombo->setEnabled(false);
    m_difficultyCombo->setCurrentIndex(1);
    m_difficultyCombo->setEnabled(false);
    m_lockDifficultyCheck->setChecked(false);
    m_lockDifficultyCheck->setEnabled(false);
    m_hardcoreCheck->setChecked(false);
    m_hardcoreCheck->setEnabled(false);
    m_dayTimeCombo->setCurrentIndex(0);
    m_dayTimeCombo->setEnabled(false);
    m_spawnXSpin->setValue(0);
    m_spawnYSpin->setValue(0);
    m_spawnZSpin->setValue(0);
    m_spawnXSpin->setEnabled(false);
    m_spawnYSpin->setEnabled(false);
    m_spawnZSpin->setEnabled(false);
    m_saveBtn->setEnabled(false);
}

void SaveSettingsPage::addInfoRow(const QString &head, QWidget *content)
{
    int row = m_infoGrid->rowCount();
    auto *headLabel = new QLabel(head, this);
    headLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 13px;"));
    m_infoGrid->addWidget(headLabel, row, 0, Qt::AlignLeft | Qt::AlignTop);
    m_infoGrid->addWidget(content, row, 1);
}

void SaveSettingsPage::loadSaveInfo()
{
    if (m_loaded || m_saveFolderPath.isEmpty())
        return;

    auto result = LevelDatEditor::readSave(m_saveFolderPath);
    m_loaded = true;

    if (!result.has_value())
    {
        m_versionLabel->setText(tr("读取失败"));
        m_seedLabel->setText(tr("无法解析 level.dat"));
        m_lastPlayedLabel->setText(tr("—"));
        m_spawnLabel->setText(tr("—"));
        m_gameModeLabel->setText(tr("—"));
        m_playTimeLabel->setText(tr("—"));
        m_dayTimeLabel->setText(tr("—"));
        m_worldNameEdit->setEnabled(false);
        m_gameTypeCombo->setEnabled(false);
        m_allowCommandsCombo->setEnabled(false);
        m_difficultyCombo->setEnabled(false);
        m_lockDifficultyCheck->setEnabled(false);
        m_hardcoreCheck->setEnabled(false);
        m_dayTimeCombo->setEnabled(false);
        m_spawnXSpin->setEnabled(false);
        m_spawnYSpin->setEnabled(false);
        m_spawnZSpin->setEnabled(false);
        m_saveBtn->setEnabled(false);
        return;
    }

    m_info = result.value();

    m_versionLabel->setText(m_info.versionName.isEmpty() ? tr("未知") : m_info.versionName);

    if (m_info.hasSeed)
    {
        m_seedLabel->setText(QString::number(m_info.seed));
        m_copySeedBtn->setEnabled(true);
        m_chunkbaseBtn->setEnabled(!versionForChunkbase().isEmpty());
        m_minecraftSearchBtn->setEnabled(!versionForMinecraftSearch().isEmpty());
    }
    else
    {
        m_seedLabel->setText(tr("无法读取"));
        m_copySeedBtn->setEnabled(false);
        m_chunkbaseBtn->setEnabled(false);
        m_minecraftSearchBtn->setEnabled(false);
    }

    m_lastPlayedLabel->setText(formatLastPlayed());
    m_spawnLabel->setText(m_info.hasSpawn
        ? QString("%1 / %2 / %3").arg(m_info.spawnX).arg(m_info.spawnY).arg(m_info.spawnZ)
        : tr("未知"));
    m_gameModeLabel->setText(m_info.hardcore ? tr("极限模式") : gameModeName(m_info.gameType));
    m_playTimeLabel->setText(formatPlayTime());
    m_dayTimeLabel->setText(m_info.hasDayTime
        ? formatDayTimeClock(m_info.dayTime)
        : tr("—"));

    // 世界名称（默认显示存档目录名）
    m_worldNameEdit->setText(m_saveName.isEmpty() ? m_info.levelName : m_saveName);
    m_worldNameEdit->setEnabled(true);

    // 游戏模式
    m_gameTypeCombo->setEnabled(true);
    const int gameTypeIdx = m_gameTypeCombo->findData(qBound(0, m_info.gameType, 3));
    if (m_info.hasGameType && gameTypeIdx >= 0)
        m_gameTypeCombo->setCurrentIndex(gameTypeIdx);

    // 游戏内时间
    m_dayTimeCombo->setEnabled(true);
    if (m_info.hasDayTime)
        setDayTimePreset(m_info.dayTime);

    m_allowCommandsCombo->setEnabled(true);
    m_difficultyCombo->setEnabled(true);
    m_lockDifficultyCheck->setEnabled(true);
    m_hardcoreCheck->setEnabled(true);
    m_saveBtn->setEnabled(true);

    m_allowCommandsCombo->setCurrentIndex(m_info.allowCommands ? 1 : 0);
    if (m_info.hasDifficulty)
    {
        m_difficultyCombo->setCurrentIndex(qBound(0, m_info.difficulty, 3));
    }
    m_lockDifficultyCheck->setChecked(m_info.difficultyLocked);
    m_hardcoreCheck->setChecked(m_info.hardcore);

    // 出生点
    m_spawnXSpin->setValue(m_info.hasSpawn ? m_info.spawnX : 0);
    m_spawnYSpin->setValue(m_info.hasSpawn ? m_info.spawnY : 0);
    m_spawnZSpin->setValue(m_info.hasSpawn ? m_info.spawnZ : 0);
    refreshSpawnSpinEnable();

    if (m_info.hardcore)
    {
        m_difficultyCombo->setEnabled(false);
        m_lockDifficultyCheck->setEnabled(false);
        m_hardcoreHint->show();
    }
    else
    {
        m_hardcoreHint->hide();
        refreshHardcoreState();
    }
}

void SaveSettingsPage::onHardcoreToggled()
{
    refreshHardcoreState();
    refreshSpawnSpinEnable();
}

void SaveSettingsPage::refreshHardcoreState()
{
    const bool hardcore = m_hardcoreCheck->isChecked();
    const bool locked = m_lockDifficultyCheck->isChecked();

    if (hardcore)
    {
        // 极限模式：难度固定为困难并锁定
        m_difficultyCombo->setCurrentIndex(3); // 困难
        m_difficultyCombo->setEnabled(false);
        m_lockDifficultyCheck->setChecked(true);
        m_lockDifficultyCheck->setEnabled(false);
        m_hardcoreHint->show();
    }
    else
    {
        m_hardcoreHint->hide();
        m_lockDifficultyCheck->setEnabled(true);
        m_difficultyCombo->setEnabled(!locked);
    }
}

void SaveSettingsPage::refreshSpawnSpinEnable()
{
    const bool enable = m_loaded && !m_saveFolderPath.isEmpty();
    m_spawnXSpin->setEnabled(enable);
    m_spawnYSpin->setEnabled(enable);
    m_spawnZSpin->setEnabled(enable);
}

void SaveSettingsPage::setDayTimePreset(qint64 ticks)
{
    // 先移除之前追加的自定义项（前 5 项为内置预设）
    while (m_dayTimeCombo->count() > 5)
        m_dayTimeCombo->removeItem(m_dayTimeCombo->count() - 1);

    int idx = m_dayTimeCombo->findData(ticks);
    if (idx >= 0)
    {
        m_dayTimeCombo->setCurrentIndex(idx);
    }
    else
    {
        // 自定义时间：追加临时项并选中
        m_dayTimeCombo->addItem(formatDayTimeClock(ticks), ticks);
        m_dayTimeCombo->setCurrentIndex(m_dayTimeCombo->count() - 1);
    }
}

qint64 SaveSettingsPage::currentDayTimeTicks() const
{
    const QVariant v = m_dayTimeCombo->currentData();
    if (v.isValid())
        return v.toLongLong();
    return 6000;
}

void SaveSettingsPage::onSaveClicked()
{
    // 1. 世界名称修改 → 先重命名文件夹并同步 level.dat
    const QString newName = m_worldNameEdit->text().trimmed();
    QString currentFolder = m_saveFolderPath;
    if (!newName.isEmpty() && newName != m_saveName)
    {
        QString newPath;
        QString error;
        if (!LevelDatEditor::renameWorld(m_saveFolderPath, newName, &newPath, &error))
        {
            AppMessageBox::warning(this, tr("重命名失败"),
                tr("无法重命名世界：\n%1").arg(error));
            return;
        }
        const QString oldName = m_saveName;
        currentFolder = newPath;
        m_saveFolderPath = newPath;
        m_saveName = newName;
        m_saveNameLabel->setText(newName);
        emit saveRenamed(oldName, newName);
    }

    // 2. 其它设置
    SaveChanges changes;
    changes.allowCommands = (m_allowCommandsCombo->currentIndex() == 1);
    changes.difficulty = m_difficultyCombo->currentData().toInt();
    changes.difficultyLocked = m_lockDifficultyCheck->isChecked();
    changes.gameType = m_gameTypeCombo->currentData().toInt();
    changes.hardcore = m_hardcoreCheck->isChecked();
    changes.dayTime = currentDayTimeTicks();
    changes.spawnX = m_spawnXSpin->value();
    changes.spawnY = m_spawnYSpin->value();
    changes.spawnZ = m_spawnZSpin->value();

    QString error;
    if (!LevelDatEditor::applyChanges(currentFolder, changes, &error))
    {
        AppMessageBox::warning(this, tr("保存失败"),
            tr("无法保存存档设置：\n%1").arg(error.isEmpty() ? tr("未做任何修改") : error));
        return;
    }

    AppMessageBox::information(this, tr("保存成功"),
        tr("存档设置已保存。\n注意：若游戏正在运行，修改可能需要重启游戏后生效。"));
}

void SaveSettingsPage::onOpenFolderClicked()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_saveFolderPath));
}

void SaveSettingsPage::onCopySeedClicked()
{
    if (m_info.hasSeed)
    {
        QApplication::clipboard()->setText(QString::number(m_info.seed));
    }
}

void SaveSettingsPage::onOpenChunkbaseClicked()
{
    if (!m_info.hasSeed)
        return;
    const QString url = chunkbaseUrl();
    if (url.isEmpty())
    {
        AppMessageBox::information(this, tr("无法打开"),
            tr("无法确定存档版本，请手动打开 Chunkbase 并输入种子。"));
        return;
    }
    QDesktopServices::openUrl(QUrl(url));
}

void SaveSettingsPage::onOpenMinecraftSearchClicked()
{
    if (!m_info.hasSeed)
        return;
    const QString url = minecraftSearchUrl();
    if (url.isEmpty())
    {
        AppMessageBox::information(this, tr("无法打开"),
            tr("无法确定存档版本，请手动打开 MinecraftSearch 种子地图并输入种子。"));
        return;
    }
    QDesktopServices::openUrl(QUrl(url));
}

QString SaveSettingsPage::formatPlayTime() const
{
    return m_info.playTimeTicks > 0
        ? formatTicksAsDuration(m_info.playTimeTicks)
        : tr("—");
}

QString SaveSettingsPage::formatLastPlayed() const
{
    if (m_info.lastPlayed <= 0)
        return tr("—");
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(m_info.lastPlayed);
    return dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
}

QString SaveSettingsPage::versionForChunkbase() const
{
    const QString v = m_info.versionName;
    if (v.isEmpty() || v.contains(QLatin1Char(' ')))
        return QString();
    // 去掉可能的 "Java版 " 前缀
    QString clean = v;
    if (clean.startsWith(QStringLiteral("Java版")))
        clean = clean.mid(4).trimmed();

    // 快照/预览版本含字母，无法确定平台版本
    if (std::any_of(clean.begin(), clean.end(), [](const QChar &c) { return c.isLetter(); }))
        return QString();

    // 1.21+ 使用完整次版本号；其余使用主版本号
    if (clean.startsWith(QStringLiteral("1.21")))
        return clean.replace(QLatin1Char('.'), QLatin1Char('_'));
    if (clean.contains(QLatin1Char('.')))
    {
        QStringList parts = clean.split(QLatin1Char('.'));
        return parts.mid(0, 2).join(QLatin1Char('_'));
    }
    return clean;
}

QString SaveSettingsPage::chunkbaseUrl() const
{
    const QString ver = versionForChunkbase();
    if (ver.isEmpty())
        return QString();
    return QStringLiteral("https://www.chunkbase.com/apps/seed-map#seed=%1&platform=java_%2&dimension=overworld")
        .arg(m_info.seed)
        .arg(ver);
}

QString SaveSettingsPage::versionForMinecraftSearch() const
{
    const QString v = m_info.versionName;
    if (v.isEmpty() || v.contains(QLatin1Char(' ')))
        return QString();
    // 去掉可能的 "Java版 " 前缀
    QString clean = v;
    if (clean.startsWith(QStringLiteral("Java版")))
        clean = clean.mid(4).trimmed();

    // 快照/预览版本含字母，无法确定平台版本
    if (std::any_of(clean.begin(), clean.end(), [](const QChar &c) { return c.isLetter(); }))
        return QString();

    // MinecraftSearch 的 platform 参数去掉开头的 "1." 前缀，如 1.26.2 -> 26.2
    if (clean.startsWith(QStringLiteral("1.")))
        clean = clean.mid(2);
    return clean;
}

QString SaveSettingsPage::minecraftSearchUrl() const
{
    const QString ver = versionForMinecraftSearch();
    if (ver.isEmpty())
        return QString();
    QString url = QStringLiteral("https://minecraftsearch.com/zh-CN/工具/种子地图#seed=%1&platform=java_%2&dimension=overworld")
        .arg(m_info.seed)
        .arg(ver);
    // 带上出生点坐标，打开后直接定位到当前世界出生点附近
    if (m_info.hasSpawn)
        url += QStringLiteral("&x=%1&z=%2&zoom=12").arg(m_info.spawnX).arg(m_info.spawnZ);
    return url;
}
