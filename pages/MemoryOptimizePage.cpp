/**
 * @file   MemoryOptimizePage.cpp
 * @brief  内存优化页实现：内存概况（系统 / 可用 / 启动器占用）+ 一键优化 + 启动前自动优化开关
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * 样式全部走全局 QSS 里已有的对象名（settingsCard / settingsSectionHeader /
 * settingRow / settingTitle / settingDesc / settingValueLabel），
 * 只有「立即优化内存」按钮新增了一条 memoryOptimizeBtn 规则，
 * 因此浅色、深色与自定义主题切换后不需要重新上色。
 */

#include "MemoryOptimizePage.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTime>
#include <QVBoxLayout>

#include "components/CollapsibleSectionCard.h"
#include "components/CustomCheckBox.h"
#include "components/NotificationManager.h"
#include "components/OutlinedLabel.h"
#include "utils/MemoryAllocator.h"
#include "utils/SettingsManager.h"
#include "utils/SystemTools.h"

namespace {

/** 字节数转成「1.2 GB」「860 MB」这类好读的文字 */
QString formatBytes(qint64 bytes)
{
    if (bytes < 0)
        return QObject::tr("读取不到");
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(qRound(mb));
}

/** MB 数字转成同样的文字格式（系统内存接口给的是 MB） */
QString formatMegabytes(qint64 megabytes)
{
    if (megabytes < 0)
        return QObject::tr("读取不到");
    if (megabytes >= 1024)
        return QStringLiteral("%1 GB").arg(megabytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(megabytes);
}

/**
 * @brief 往卡片里加一行设置行（左侧标题 / 说明，右侧控件区），返回右侧布局
 *
 * 与设置页同款结构：整行一条分隔线，最后一行不带分隔线。
 */
QHBoxLayout *appendRow(QVBoxLayout *cardLayout, const QString &title, const QString &desc,
                       bool lastRow = false)
{
    auto *row = new QFrame();
    row->setObjectName(QStringLiteral("settingRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    if (lastRow)
        row->setProperty("lastRow", true);

    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(24, 14, 24, 14);
    rowLayout->setSpacing(20);

    auto *info = new QWidget(row);
    info->setObjectName(QStringLiteral("settingInfo"));
    auto *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(3);

    if (!title.isEmpty()) {
        auto *titleLabel = new QLabel(title, info);
        titleLabel->setObjectName(QStringLiteral("settingTitle"));
        infoLayout->addWidget(titleLabel);
    }
    if (!desc.isEmpty()) {
        auto *descLabel = new QLabel(desc, info);
        descLabel->setObjectName(QStringLiteral("settingDesc"));
        descLabel->setWordWrap(true);
        infoLayout->addWidget(descLabel);
    }

    rowLayout->addWidget(info, 1);
    cardLayout->addWidget(row);
    return rowLayout;
}

} // namespace

MemoryOptimizePage::MemoryOptimizePage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    refresh();
}

void MemoryOptimizePage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 18, 28, 28);
    layout->setSpacing(10);

    auto *pageTitle = new OutlinedLabel(tr("内存优化"), content);
    pageTitle->setObjectName(QStringLiteral("resourcesPageTitle"));
    layout->addWidget(pageTitle);

    auto *subtitle = new QLabel(
        tr("启动器开久了会攥着一堆用不到的闲置内存，点一下就能还给系统，游戏能多分到一些"), content);
    subtitle->setObjectName(QStringLiteral("resourcesPageSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    layout->addSpacing(10);

    // ── 内存概况 ──
    auto *statsCard = new CollapsibleSectionCard(tr("内存概况"), false, content);
    QVBoxLayout *statsLayout = statsCard->contentLayout();

    m_usageLabel = new QLabel(statsCard);
    m_usageLabel->setObjectName(QStringLiteral("settingValueLabel"));
    m_usageBar = new QProgressBar(statsCard);
    m_usageBar->setObjectName(QStringLiteral("largeProgressBar"));
    m_usageBar->setTextVisible(false);
    // 窄窗口（手机 / 分屏）下自动缩短，别把左侧说明挤成一列字
    m_usageBar->setMinimumWidth(120);
    m_usageBar->setMaximumWidth(280);

    // 数字挂在小进度条上方并右对齐，和下面几行的数值挤在同一条竖线上
    auto *usageControl = new QWidget(statsCard);
    usageControl->setObjectName(QStringLiteral("settingInfo"));
    auto *usageControlLayout = new QVBoxLayout(usageControl);
    usageControlLayout->setContentsMargins(0, 0, 0, 0);
    usageControlLayout->setSpacing(6);
    usageControlLayout->addWidget(m_usageLabel, 0, Qt::AlignRight);
    usageControlLayout->addWidget(m_usageBar, 0, Qt::AlignRight);

    QHBoxLayout *usageRow = appendRow(statsLayout, tr("系统内存"),
                                      tr("整机物理内存的占用情况，越满越容易卡顿。"));
    usageRow->addWidget(usageControl);

    m_availableLabel = new QLabel(statsCard);
    m_availableLabel->setObjectName(QStringLiteral("settingValueLabel"));
    QHBoxLayout *availableRow = appendRow(
        statsLayout, tr("可用内存"),
        tr("系统当前空着、可以立刻分给程序的内存，这个数字越大游戏越稳。"));
    availableRow->addWidget(m_availableLabel);

    m_launcherLabel = new QLabel(statsCard);
    m_launcherLabel->setObjectName(QStringLiteral("settingValueLabel"));
    QHBoxLayout *launcherRow = appendRow(
        statsLayout, tr("启动器占用"),
        tr("方块盒子自己现在占用的物理内存；优化主要就是把这块里闲置的部分还回去。"));
    launcherRow->addWidget(m_launcherLabel);

    m_updatedLabel = new QLabel(statsCard);
    m_updatedLabel->setObjectName(QStringLiteral("settingValueLabel"));
    auto *refreshBtn = new QPushButton(tr("刷新"), statsCard);
    refreshBtn->setObjectName(QStringLiteral("browseBtn"));
    refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(refreshBtn, &QPushButton::clicked, this, [this]() { updateMemoryStats(); });

    QHBoxLayout *updatedRow = appendRow(statsLayout, tr("更新时间"),
                                        tr("数字是点进来或点「刷新」时采集的。"), true);
    updatedRow->addWidget(m_updatedLabel);
    updatedRow->addWidget(refreshBtn);

    layout->addWidget(statsCard);

    // ── 立即优化 ──
    auto *optimizeCard = new CollapsibleSectionCard(tr("立即优化"), false, content);
    QVBoxLayout *optimizeLayout = optimizeCard->contentLayout();

    auto *actionArea = new QWidget(optimizeCard);
    actionArea->setAttribute(Qt::WA_StyledBackground, false);
    auto *actionLayout = new QVBoxLayout(actionArea);
    actionLayout->setContentsMargins(24, 16, 24, 16);
    actionLayout->setSpacing(10);

    m_optimizeBtn = new QPushButton(tr("立即优化内存"), actionArea);
    m_optimizeBtn->setObjectName(QStringLiteral("memoryOptimizeBtn"));
    m_optimizeBtn->setCursor(Qt::PointingHandCursor);
    m_optimizeBtn->setMinimumHeight(44);
    m_optimizeBtn->setMinimumWidth(180);
    connect(m_optimizeBtn, &QPushButton::clicked, this, [this]() { runOptimize(); });

    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(14);
    btnRow->addWidget(m_optimizeBtn);

    m_resultLabel = new QLabel(tr("还没优化过，点左边的按钮试试"), actionArea);
    m_resultLabel->setObjectName(QStringLiteral("memoryOptimizeResult"));
    m_resultLabel->setWordWrap(true);
    btnRow->addWidget(m_resultLabel, 1);
    actionLayout->addLayout(btnRow);

    auto *tipLabel = new QLabel(
        tr("优化只动启动器自己的闲置内存，不会去关你别的软件，游戏开着的时候点也安全。"), actionArea);
    tipLabel->setObjectName(QStringLiteral("settingDesc"));
    tipLabel->setWordWrap(true);
    actionLayout->addWidget(tipLabel);

    optimizeLayout->addWidget(actionArea);

    // ── 启动游戏前先优化内存（与设置页的开关是同一个设置） ──
    auto *switchArea = new QWidget(optimizeCard);
    switchArea->setAttribute(Qt::WA_StyledBackground, false);
    auto *switchOuter = new QVBoxLayout(switchArea);
    switchOuter->setContentsMargins(0, 0, 0, 0);
    switchOuter->setSpacing(0);

    m_preLaunchCheck = new CustomCheckBox(QString(), switchArea);
    connect(m_preLaunchCheck, &CustomCheckBox::toggled, this, [](bool checked) {
        SettingsManager::instance()->setOptimizeMemoryBeforeLaunchEnabled(checked);
    });
    // 设置页里有同一个开关：那边改了这边跟着回填，回填时不要再写回去
    connect(SettingsManager::instance(), &SettingsManager::optimizeMemoryBeforeLaunchChanged,
            this, [this](bool enabled) {
                if (!m_preLaunchCheck || m_preLaunchCheck->isChecked() == enabled)
                    return;
                const QSignalBlocker blocker(m_preLaunchCheck);
                m_preLaunchCheck->setChecked(enabled);
            });

    QHBoxLayout *switchRow = appendRow(
        switchOuter, tr("启动游戏前先优化内存"),
        tr("打开后每次点启动都会先优化一次再拉起游戏，开关与「设置 → 全局游戏设置」里的那个是同一个。"),
        true);
    switchRow->addWidget(m_preLaunchCheck);

    optimizeLayout->addWidget(switchArea);

    layout->addWidget(optimizeCard);

    layout->addStretch(1);

    auto *hint = new QLabel(
        tr("这里不碰系统设置、也不结束后台进程，只是把启动器占着不用的内存还回系统；"
           "想让它每次启动都自动做一次，可以在上面打开开关。"),
        content);
    hint->setObjectName(QStringLiteral("forumMuted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void MemoryOptimizePage::refresh()
{
    // 设置页可能刚改过开关：进来时按存储值回填，回填过程不要反过来写设置
    const bool preLaunch = SettingsManager::instance()->isOptimizeMemoryBeforeLaunchEnabled();
    if (m_preLaunchCheck) {
        const QSignalBlocker blocker(m_preLaunchCheck);
        m_preLaunchCheck->setChecked(preLaunch);
    }
    updateMemoryStats();
}

void MemoryOptimizePage::updateMemoryStats()
{
    const int totalMb = MemoryAllocator::detectTotalSystemMemory();
    const int availableMb = MemoryAllocator::detectAvailableMemory();
    const qint64 launcherBytes = SystemTools::currentProcessWorkingSetBytes();

    if (totalMb > 0 && availableMb >= 0) {
        const int usedMb = qMax(0, totalMb - availableMb);
        m_usageLabel->setText(tr("%1 / %2")
                                  .arg(formatMegabytes(usedMb), formatMegabytes(totalMb)));
        m_usageBar->setVisible(true);
        m_usageBar->setRange(0, totalMb);
        m_usageBar->setValue(usedMb);
        m_availableLabel->setText(formatMegabytes(availableMb));
    } else {
        m_usageLabel->setText(tr("读取不到"));
        m_usageBar->setVisible(false);
        m_availableLabel->setText(tr("读取不到"));
    }

    m_launcherLabel->setText(formatBytes(launcherBytes));
    m_updatedLabel->setText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
}

void MemoryOptimizePage::runOptimize()
{
    const qint64 beforeBytes = SystemTools::currentProcessWorkingSetBytes();
    const qint64 freedBytes = SystemTools::optimizeMemorySilently();
    const qint64 afterBytes = SystemTools::currentProcessWorkingSetBytes();

    updateMemoryStats();

    QString result;
    if (freedBytes > 0) {
        result = tr("刚刚释放了 %1，启动器现在占用 %2")
                     .arg(formatBytes(freedBytes), formatBytes(afterBytes));
    } else if (beforeBytes > 0 && afterBytes > 0 && beforeBytes > afterBytes) {
        result = tr("刚刚释放了 %1，启动器现在占用 %2")
                     .arg(formatBytes(beforeBytes - afterBytes), formatBytes(afterBytes));
    } else {
        result = tr("内存已经比较干净了，这次没有再能释放的空间");
    }

    m_resultLabel->setText(result);
    NotificationManager::showSuccess(this, tr("内存优化完成：%1").arg(result));
}
