/**
 * @file   MigrationWizardDialog.cpp
 * @brief  高级迁移向导实现
 * @author BlockBox Team
 * @date   2026-11-27
 */
#include "components/MigrationWizardDialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <functional>

#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"

namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1));
    if (bytes >= 1024LL * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024), 'f', 1));
    return QStringLiteral("%1 KB").arg(QString::number(qMax<qint64>(1, bytes / 1024), 'f', 0));
}

QString shortPath(const QString &path, int maxChars = 58)
{
    const QString native = QDir::toNativeSeparators(path);
    if (native.size() <= maxChars)
        return native;
    const int head = maxChars / 2 - 2;
    const int tail = maxChars - head - 1;
    return native.left(head) + QStringLiteral("…") + native.right(tail);
}

QString launcherLetter(const QString &key)
{
    if (key == QStringLiteral("pcl"))
        return QStringLiteral("P");
    if (key == QStringLiteral("hmcl"))
        return QStringLiteral("H");
    if (key == QStringLiteral("multimc"))
        return QStringLiteral("M");
    if (key == QStringLiteral("baka"))
        return QStringLiteral("B");
    if (key == QStringLiteral("official"))
        return QStringLiteral("O");
    if (key == QStringLiteral("fcl"))
        return QStringLiteral("F");
    if (key == QStringLiteral("zl2"))
        return QStringLiteral("Z");
    if (key == QStringLiteral("pojav"))
        return QStringLiteral("P");
    if (key == QStringLiteral("other"))
        return QStringLiteral("O");
    return QStringLiteral("+");
}

QString launcherGradient(const QString &key)
{
    if (key == QStringLiteral("pcl"))
        return QStringLiteral("#F59E0B,#B45309");
    if (key == QStringLiteral("hmcl"))
        return QStringLiteral("#6366F1,#4338CA");
    if (key == QStringLiteral("multimc"))
        return QStringLiteral("#10B981,#047857");
    if (key == QStringLiteral("baka"))
        return QStringLiteral("#EC4899,#BE185D");
    if (key == QStringLiteral("official"))
        return QStringLiteral("#3B82F6,#1D4ED8");
    if (key == QStringLiteral("fcl"))
        return QStringLiteral("#F59E0B,#B45309");
    if (key == QStringLiteral("zl2"))
        return QStringLiteral("#8B5CF6,#5B21B6");
    if (key == QStringLiteral("pojav"))
        return QStringLiteral("#0EA5E9,#0369A1");
    return QStringLiteral("#64748B,#334155");
}

QString badgeStyle(const QString &key)
{
    return QStringLiteral(
               "QLabel{background-color:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
               "stop:0 %1,stop:1 %2);border-radius:11px;color:#FFFFFF;"
               "font-size:18px;font-weight:bold;}")
        .arg(launcherGradient(key).section(QLatin1Char(','), 0, 0),
             launcherGradient(key).section(QLatin1Char(','), 1, 1));
}

void clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            delete widget;
        if (QLayout *child = item->layout()) {
            clearLayout(child);
            delete child;
        }
        delete item;
    }
}

} // namespace

MigrationWizardDialog::MigrationWizardDialog(QWidget *parent)
    : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("migrationWizardDialog"));
    setWindowTitle(tr("迁移向导"));
    initUI();
    initStyle();
    QTimer::singleShot(0, this, [this]() { startScan(); });
}

MigrationWizardDialog::~MigrationWizardDialog()
{
    if (m_copyTask) {
        m_copyTask->cancel();
        m_copyTask->wait();
    }
}

void MigrationWizardDialog::reject()
{
    if (m_migrating) {
        if (m_footerHint)
            m_footerHint->setText(tr("正在迁移，请等复制完成，或点进度页的「取消迁移」"));
        return;
    }
    AppDialogBase::reject();
}

/* ============================================================ 界面骨架 */

void MigrationWizardDialog::initUI()
{
    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("migrationCard"));
    m_card->setAttribute(Qt::WA_StyledBackground, true);
    m_card->setMinimumSize(UiMetrics::fitToScreen(this, QSize(980, 720)));
    m_card->setMaximumSize(1080, 840);

    auto *layout = new QVBoxLayout(m_card);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);

    // ── 标题 ──
    auto *header = new QHBoxLayout();
    header->setSpacing(12);
    auto *logo = new QLabel(QStringLiteral("迁"), m_card);
    logo->setObjectName(QStringLiteral("migrationLogo"));
    logo->setFixedSize(46, 46);
    logo->setAlignment(Qt::AlignCenter);
    header->addWidget(logo, 0, Qt::AlignTop | Qt::AlignHCenter);

    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);
    m_titleLabel = new QLabel(tr("迁移向导"), m_card);
    m_titleLabel->setObjectName(QStringLiteral("migrationTitle"));
    m_subtitleLabel = new QLabel(
        tr("把 PCL / HMCL / MultiMC / BakaXL / 官方启动器里的实例、账户和设置搬过来；"
           "原目录全程只读，不会改动或删除。"), m_card);
    m_subtitleLabel->setObjectName(QStringLiteral("migrationSubtitle"));
    m_subtitleLabel->setWordWrap(true);
    titleBox->addWidget(m_titleLabel);
    titleBox->addWidget(m_subtitleLabel);
    header->addLayout(titleBox, 1);
    layout->addLayout(header);

    buildStepper(layout);

    m_stack = new QStackedWidget(m_card);
    m_stack->setObjectName(QStringLiteral("migrationStack"));
    m_stack->addWidget(buildScanPage());
    m_stack->addWidget(buildSelectPage());
    m_optionsPage = buildOptionsPage();
    m_stack->addWidget(m_optionsPage);
    m_stack->addWidget(buildProgressPage());
    m_stack->addWidget(buildResultPage());
    layout->addWidget(m_stack, 1);

    // ── 底栏 ──
    auto *footer = new QHBoxLayout();
    footer->setSpacing(10);
    m_footerHint = new QLabel(m_card);
    m_footerHint->setObjectName(QStringLiteral("migrationHint"));
    m_footerHint->setWordWrap(true);
    footer->addWidget(m_footerHint, 1);

    m_prevBtn = new QPushButton(tr("上一步"), m_card);
    m_prevBtn->setObjectName(QStringLiteral("migrationSecondary"));
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    m_nextBtn = new QPushButton(tr("下一步"), m_card);
    m_nextBtn->setObjectName(QStringLiteral("migrationPrimary"));
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    m_doneBtn = new QPushButton(tr("完成"), m_card);
    m_doneBtn->setObjectName(QStringLiteral("migrationPrimary"));
    m_doneBtn->setCursor(Qt::PointingHandCursor);
    m_instancesBtn = new QPushButton(tr("查看实例列表"), m_card);
    m_instancesBtn->setObjectName(QStringLiteral("migrationSecondary"));
    m_instancesBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn = new QPushButton(tr("取消迁移"), m_card);
    m_cancelBtn->setObjectName(QStringLiteral("migrationDanger"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);

    footer->addWidget(m_prevBtn);
    footer->addWidget(m_nextBtn);
    footer->addWidget(m_cancelBtn);
    footer->addWidget(m_doneBtn);
    footer->addWidget(m_instancesBtn);
    layout->addLayout(footer);

    connect(m_prevBtn, &QPushButton::clicked, this, &MigrationWizardDialog::onPrevClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &MigrationWizardDialog::onNextClicked);
    connect(m_doneBtn, &QPushButton::clicked, this, [this]() { accept(); });
    connect(m_instancesBtn, &QPushButton::clicked, this, [this]() {
        m_wantsOpenInstances = true;
        accept();
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (!m_migrating || m_cancelRequested)
            return;
        m_cancelRequested = true;
        m_cancelBtn->setEnabled(false);
        m_cancelBtn->setText(tr("正在取消…"));
        appendLog(tr("正在取消迁移…"));
        if (m_copyTask)
            m_copyTask->cancel();
    });

    updateStepPills();
    updateNavState();
}

void MigrationWizardDialog::buildStepper(QVBoxLayout *parentLayout)
{
    auto *row = new QHBoxLayout();
    row->setSpacing(6);

    const QStringList labels = { tr("扫描"), tr("选择"), tr("方式"), tr("完成") };
    for (int i = 0; i < labels.size(); ++i) {
        auto *pill = new QLabel(QStringLiteral("%1  %2").arg(i + 1).arg(labels.at(i)), m_card);
        pill->setObjectName(QStringLiteral("migrationStepPill"));
        pill->setProperty("state", i == 0 ? QStringLiteral("active") : QStringLiteral("idle"));
        pill->setAlignment(Qt::AlignCenter);
        m_stepPills.append(pill);
        row->addWidget(pill);
        if (i < labels.size() - 1) {
            auto *line = new QFrame(m_card);
            line->setObjectName(QStringLiteral("migrationStepLine"));
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Plain);
            line->setFixedHeight(1);
            m_stepLines.append(line);
            row->addWidget(line, 1);
        }
    }
    parentLayout->addLayout(row);
}

QWidget *MigrationWizardDialog::buildScanPage()
{
    auto *page = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(10);

    m_scanStatus = new QLabel(tr("正在扫描本机的启动器…"), page);
    m_scanStatus->setObjectName(QStringLiteral("migrationSectionTitle"));
    layout->addWidget(m_scanStatus);

    m_scanBar = new QProgressBar(page);
    m_scanBar->setObjectName(QStringLiteral("migrationProgress"));
    m_scanBar->setRange(0, 0);
    m_scanBar->setTextVisible(false);
    m_scanBar->setFixedHeight(4);
    layout->addWidget(m_scanBar);

    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("migrationScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scanListHost = new QWidget(scroll);
    m_scanListHost->setObjectName(QStringLiteral("migrationListHost"));
    m_scanListLayout = new QVBoxLayout(m_scanListHost);
    m_scanListLayout->setContentsMargins(0, 0, 0, 0);
    m_scanListLayout->setSpacing(8);
    m_scanListLayout->addStretch(1);
    scroll->setWidget(m_scanListHost);
    layout->addWidget(scroll, 1);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(8);
    m_rescanBtn = new QPushButton(tr("重新扫描"), page);
    m_rescanBtn->setObjectName(QStringLiteral("migrationSecondary"));
    m_rescanBtn->setCursor(Qt::PointingHandCursor);
    m_addDirBtn = new QPushButton(tr("添加游戏目录…"), page);
    m_addDirBtn->setObjectName(QStringLiteral("migrationSecondary"));
    m_addDirBtn->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_rescanBtn);
    actions->addWidget(m_addDirBtn);
    actions->addStretch();
    layout->addLayout(actions);
    connect(m_rescanBtn, &QPushButton::clicked, this, [this]() {
        if (!m_scanning)
            startScan();
    });
    connect(m_addDirBtn, &QPushButton::clicked, this, &MigrationWizardDialog::onAddCustomDir);

    auto *tip = new QLabel(
        tr("首次扫描会遍历磁盘上的常见目录，可能需要十几秒；扫描到一家就会立刻显示，不用等全部结束。"
           "方块盒子只读取启动器配置和游戏目录，不会写入或删除对方的数据。"),
        page);
    tip->setObjectName(QStringLiteral("migrationMuted"));
    tip->setWordWrap(true);
    layout->addWidget(tip);

    return page;
}

QWidget *MigrationWizardDialog::buildSelectPage()
{
    auto *page = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(10);

    auto *top = new QHBoxLayout();
    top->setSpacing(8);
    auto *title = new QLabel(tr("选择要搬走的内容"), page);
    title->setObjectName(QStringLiteral("migrationSectionTitle"));
    top->addWidget(title);
    top->addStretch();
    m_selectAllBtn = new QPushButton(tr("全选"), page);
    m_selectAllBtn->setObjectName(QStringLiteral("migrationLink"));
    m_selectAllBtn->setCursor(Qt::PointingHandCursor);
    m_clearAllBtn = new QPushButton(tr("全不选"), page);
    m_clearAllBtn->setObjectName(QStringLiteral("migrationLink"));
    m_clearAllBtn->setCursor(Qt::PointingHandCursor);
    top->addWidget(m_selectAllBtn);
    top->addWidget(m_clearAllBtn);
    layout->addLayout(top);

    auto *hint = new QLabel(
        tr("勾选整个启动器，或展开后只挑某个游戏目录 / 只带账户和 Java。"
           "默认会带上账户、Java 和登录服，下载/内存设置默认不勾，避免悄悄改掉你现在的习惯。"),
        page);
    hint->setObjectName(QStringLiteral("migrationMuted"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("migrationScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_selectListHost = new QWidget(scroll);
    m_selectListHost->setObjectName(QStringLiteral("migrationListHost"));
    m_selectListLayout = new QVBoxLayout(m_selectListHost);
    m_selectListLayout->setContentsMargins(0, 0, 0, 0);
    m_selectListLayout->setSpacing(10);
    m_selectListLayout->addStretch(1);
    scroll->setWidget(m_selectListHost);
    layout->addWidget(scroll, 1);

    m_selectSummary = new QLabel(page);
    m_selectSummary->setObjectName(QStringLiteral("migrationSummary"));
    m_selectSummary->setWordWrap(true);
    layout->addWidget(m_selectSummary);

    connect(m_selectAllBtn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_sources.size(); ++i) {
            SourceState &s = m_sources[i];
            s.selected = true;
            s.accounts = s.detect.accountCount > 0;
            s.java = !s.detect.javaPaths.isEmpty();
            s.authServers = !s.detect.authServers.isEmpty();
            s.downloadSettings = s.detect.downloadSource >= 0 || s.detect.downloadThreads > 0;
            s.memory = s.detect.javaMaxMemoryMb > 0 || s.detect.autoMemoryKnown;
            s.dirs.clear();
            for (const LauncherGameDirInfo &info : s.detect.gameDirInfos)
                s.dirs.insert(QDir(info.path).absolutePath());
        }
        rebuildSourceCards();
        updateSelectionSummary();
    });
    connect(m_clearAllBtn, &QPushButton::clicked, this, [this]() {
        for (SourceState &s : m_sources) {
            s.selected = false;
            s.accounts = false;
            s.java = false;
            s.authServers = false;
            s.downloadSettings = false;
            s.memory = false;
            s.dirs.clear();
        }
        rebuildSourceCards();
        updateSelectionSummary();
    });

    return page;
}

QWidget *MigrationWizardDialog::buildOptionsPage()
{
    auto *page = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("怎么搬过去？"), page);
    title->setObjectName(QStringLiteral("migrationSectionTitle"));
    layout->addWidget(title);

    // ── 只登记 ──
    m_registerCard = new QFrame(page);
    m_registerCard->setObjectName(QStringLiteral("migrationOptionCard"));
    auto *registerLayout = new QVBoxLayout(m_registerCard);
    registerLayout->setContentsMargins(14, 10, 14, 10);
    registerLayout->setSpacing(3);
    m_registerRadio = new QRadioButton(tr("只登记（推荐，几秒完成）"), m_registerCard);
    m_registerRadio->setChecked(true);
    m_registerRadio->setCursor(Qt::PointingHandCursor);
    auto *registerDesc = new QLabel(
        tr("把原游戏目录直接加进方块盒子的实例列表，不复制任何文件；两个启动器可以同时用，"
           "原目录里已有存档、模组都原样生效。"),
        m_registerCard);
    registerDesc->setObjectName(QStringLiteral("migrationMuted"));
    registerDesc->setWordWrap(true);
    registerLayout->addWidget(m_registerRadio);
    registerLayout->addWidget(registerDesc);
    layout->addWidget(m_registerCard);

    // ── 复制一份 ──
    m_copyCard = new QFrame(page);
    m_copyCard->setObjectName(QStringLiteral("migrationOptionCard"));
    auto *copyLayout = new QVBoxLayout(m_copyCard);
    copyLayout->setContentsMargins(14, 10, 14, 10);
    copyLayout->setSpacing(6);
    m_copyRadio = new QRadioButton(tr("复制一份到方块盒子（原目录不动）"), m_copyCard);
    m_copyRadio->setCursor(Qt::PointingHandCursor);
    auto *copyDesc = new QLabel(
        tr("把选中的游戏目录整份复制到方块盒子自己的位置，之后两边互不影响；"
           "复制期间别关窗口，文件多的话会比较久，也会额外占用一份磁盘空间。"),
        m_copyCard);
    copyDesc->setObjectName(QStringLiteral("migrationMuted"));
    copyDesc->setWordWrap(true);
    copyLayout->addWidget(m_copyRadio);
    copyLayout->addWidget(copyDesc);

    auto *copyRootRow = new QHBoxLayout();
    copyRootRow->setSpacing(8);
    m_copyRootEdit = new QLineEdit(MigrationService::defaultCopyRoot(), m_copyCard);
    m_copyRootEdit->setObjectName(QStringLiteral("migrationPathEdit"));
    m_copyRootEdit->setPlaceholderText(tr("选择复制到哪个文件夹"));
    m_browseCopyRootBtn = new QPushButton(tr("选择…"), m_copyCard);
    m_browseCopyRootBtn->setObjectName(QStringLiteral("migrationSecondary"));
    m_browseCopyRootBtn->setCursor(Qt::PointingHandCursor);
    copyRootRow->addWidget(m_copyRootEdit, 1);
    copyRootRow->addWidget(m_browseCopyRootBtn);
    copyLayout->addLayout(copyRootRow);

    m_copyEstimate = new QLabel(m_copyCard);
    m_copyEstimate->setObjectName(QStringLiteral("migrationMuted"));
    m_diskInfo = new QLabel(m_copyCard);
    m_diskInfo->setObjectName(QStringLiteral("migrationMuted"));
    copyLayout->addWidget(m_copyEstimate);
    copyLayout->addWidget(m_diskInfo);
    layout->addWidget(m_copyCard);

    // 两个单选卡片分别放在两张卡里，父控件不同，必须用 QButtonGroup 才能互斥
    auto *modeGroup = new QButtonGroup(page);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_registerRadio);
    modeGroup->addButton(m_copyRadio);

    m_optionWarning = new QLabel(page);
    m_optionWarning->setObjectName(QStringLiteral("migrationWarning"));
    m_optionWarning->setWordWrap(true);
    layout->addWidget(m_optionWarning);

    // ── 汇总 ──
    auto *summaryCard = new QFrame(page);
    summaryCard->setObjectName(QStringLiteral("migrationSummaryCard"));
    auto *summaryLayout = new QVBoxLayout(summaryCard);
    summaryLayout->setContentsMargins(14, 10, 14, 10);
    summaryLayout->setSpacing(4);
    auto *summaryTitle = new QLabel(tr("本次会写入"), summaryCard);
    summaryTitle->setObjectName(QStringLiteral("migrationSectionTitle"));
    m_optionSummary = new QLabel(summaryCard);
    m_optionSummary->setObjectName(QStringLiteral("migrationSummary"));
    m_optionSummary->setWordWrap(true);
    summaryLayout->addWidget(summaryTitle);
    summaryLayout->addWidget(m_optionSummary);
    layout->addWidget(summaryCard);
    layout->addStretch(1);

    connect(m_registerRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on)
            refreshOptionsPage();
    });
    connect(m_copyRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on)
            refreshOptionsPage();
    });
    connect(m_browseCopyRootBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("选择复制位置"), m_copyRootEdit->text().trimmed(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!dir.isEmpty())
            m_copyRootEdit->setText(QDir::toNativeSeparators(dir));
    });
    connect(m_copyRootEdit, &QLineEdit::textChanged, this, [this]() {
        updateDiskInfo();
        updateCopyWarning();
    });

    return page;
}

QWidget *MigrationWizardDialog::buildProgressPage()
{
    auto *page = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("正在迁移"), page);
    title->setObjectName(QStringLiteral("migrationSectionTitle"));
    layout->addWidget(title);

    m_progressLabel = new QLabel(tr("准备中…"), page);
    m_progressLabel->setObjectName(QStringLiteral("migrationSummary"));
    m_progressLabel->setWordWrap(true);
    layout->addWidget(m_progressLabel);

    m_progressBar = new QProgressBar(page);
    m_progressBar->setObjectName(QStringLiteral("migrationProgress"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setFixedHeight(8);
    layout->addWidget(m_progressBar);

    m_logView = new QTextEdit(page);
    m_logView->setObjectName(QStringLiteral("migrationLog"));
    m_logView->setReadOnly(true);
    m_logView->setMinimumHeight(120);
    layout->addWidget(m_logView, 1);

    auto *tip = new QLabel(
        tr("迁移过程中可以继续用电脑做别的事，但别关这个窗口。取消复制时，方块盒子会删掉"
           "自己刚建的那份副本，不会碰原目录。"),
        page);
    tip->setObjectName(QStringLiteral("migrationMuted"));
    tip->setWordWrap(true);
    layout->addWidget(tip);

    return page;
}

QWidget *MigrationWizardDialog::buildResultPage()
{
    auto *page = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(10);

    m_resultTitle = new QLabel(tr("迁移完成"), page);
    m_resultTitle->setObjectName(QStringLiteral("migrationResultTitle"));
    layout->addWidget(m_resultTitle);

    m_resultSummary = new QLabel(page);
    m_resultSummary->setObjectName(QStringLiteral("migrationSummary"));
    m_resultSummary->setWordWrap(true);
    layout->addWidget(m_resultSummary);

    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("migrationScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *detailHost = new QWidget(scroll);
    detailHost->setObjectName(QStringLiteral("migrationListHost"));
    auto *detailLayout = new QVBoxLayout(detailHost);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    m_resultDetail = new QLabel(detailHost);
    m_resultDetail->setObjectName(QStringLiteral("migrationResultDetail"));
    m_resultDetail->setWordWrap(true);
    m_resultDetail->setTextFormat(Qt::RichText);
    m_resultDetail->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detailLayout->addWidget(m_resultDetail);
    detailLayout->addStretch(1);
    scroll->setWidget(detailHost);
    layout->addWidget(scroll, 1);

    auto *tip = new QLabel(
        tr("原启动器的数据没有动过。想换回原来的启动器，直接打开它就能继续用。"),
        page);
    tip->setObjectName(QStringLiteral("migrationMuted"));
    tip->setWordWrap(true);
    layout->addWidget(tip);

    return page;
}

/* ============================================================ 样式 */

void MigrationWizardDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool light = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString accent = tm->currentThemeColor();

    const QString cardBg = light ? QStringLiteral("rgba(255,255,255,246)")
                                 : QStringLiteral("rgba(40,40,44,246)");
    const QString cardBorder = light ? QStringLiteral("rgba(214,214,214,235)")
                                     : QStringLiteral("rgba(92,92,98,235)");
    const QString title = light ? QStringLiteral("#1A1A1A") : QStringLiteral("#F3F3F5");
    const QString text = light ? QStringLiteral("#333333") : QStringLiteral("#E6E6E8");
    const QString sub = light ? QStringLiteral("#777777") : QStringLiteral("#A7A7AD");
    const QString rowBg = light ? QStringLiteral("rgba(0,0,0,10)")
                                : QStringLiteral("rgba(255,255,255,14)");
    const QString rowBorder = light ? QStringLiteral("rgba(0,0,0,24)")
                                    : QStringLiteral("rgba(255,255,255,26)");
    const QString inputBg = light ? QStringLiteral("#F6F6F6") : QStringLiteral("#35353A");
    const QString inputBorder = light ? QStringLiteral("#D6D6D6") : QStringLiteral("#55555B");
    const QString danger = QStringLiteral("#DC5A54");

    setStyleSheet(QString(
                      "QWidget#migrationCard{background-color:%1;border:1px solid %2;border-radius:18px;}"
                      "QLabel#migrationLogo{background-color:%3;border-radius:13px;color:#FFFFFF;"
                      "font-size:20px;font-weight:bold;}"
                      "QLabel#migrationTitle{color:%4;font-size:19px;font-weight:bold;}"
                      "QLabel#migrationSubtitle{color:%5;font-size:12px;}"
                      "QLabel#migrationSectionTitle{color:%4;font-size:14px;font-weight:bold;}"
                      "QLabel#migrationMuted{color:%5;font-size:12px;}"
                      "QLabel#migrationHint{color:%5;font-size:12px;}"
                      "QLabel#migrationSummary{color:%6;font-size:13px;}"
                      "QLabel#migrationWarning{color:%7;font-size:12px;}"
                      "QLabel#migrationResultTitle{color:%4;font-size:20px;font-weight:bold;}"
                      "QLabel#migrationResultDetail{color:%6;font-size:13px;line-height:1.5;}"
                      "QLabel#migrationStepPill{color:%5;background-color:%8;border:1px solid transparent;"
                      "border-radius:9px;padding:4px 12px;font-size:12px;}"
                      "QLabel#migrationStepPill[state=\"active\"]{color:#FFFFFF;background-color:%3;"
                      "font-weight:bold;}"
                      "QLabel#migrationStepPill[state=\"done\"]{color:%6;background-color:%8;"
                      "border:1px solid %3;font-weight:bold;}"
                      "QFrame#migrationStepLine{background-color:%9;border:none;}"
                      "QScrollArea#migrationScroll,QWidget#migrationListHost{background:transparent;}"
                      "QWidget#migrationSourceBody{background:transparent;}"
                      "QFrame#migrationSourceCard,QFrame#migrationScanRow{background-color:%8;"
                      "border:1px solid %9;border-radius:12px;}"
                      "QFrame#migrationOptionCard,QFrame#migrationSummaryCard{background-color:%8;"
                      "border:1px solid %9;border-radius:12px;}"
                      "QLabel#migrationScanName{color:%4;font-size:13px;font-weight:bold;}"
                      "QLabel#migrationScanDetail{color:%5;font-size:11px;}"
                      "QLabel#migrationScanStatus{color:%5;font-size:11px;}"
                      "QLabel#migrationScanStatus[state=\"found\"]{color:#2E9E63;font-weight:bold;}"
                      "QLabel#migrationScanStatus[state=\"missing\"]{color:%5;}"
                      "QLabel#migrationSourceName{color:%4;font-size:14px;font-weight:bold;}"
                      "QLabel#migrationSourceSummary{color:%5;font-size:11px;}"
                      "QLabel#migrationPath{color:%6;font-size:12px;}"
                      "QLabel#migrationPathMeta{color:%5;font-size:11px;}"
                      "QPushButton#migrationPrimary{background-color:%3;color:#FFFFFF;border:none;"
                      "border-radius:9px;padding:7px 20px;font-size:13px;font-weight:bold;}"
                      "QPushButton#migrationPrimary:disabled{background-color:%10;color:%5;}"
                      "QPushButton#migrationSecondary{background-color:%8;color:%6;"
                      "border:1px solid %9;border-radius:9px;padding:6px 16px;font-size:12px;}"
                      "QPushButton#migrationSecondary:disabled{color:%5;}"
                      "QPushButton#migrationLink{background:transparent;border:none;color:%3;"
                      "padding:2px 4px;font-size:12px;}"
                      "QPushButton#migrationDanger{background-color:transparent;color:%7;"
                      "border:1px solid %7;border-radius:9px;padding:6px 16px;font-size:12px;}"
                      "QLineEdit#migrationPathEdit{background-color:%11;color:%6;"
                      "border:1px solid %12;border-radius:8px;padding:5px 9px;font-size:12px;}"
                      "QProgressBar#migrationProgress{background-color:%8;border:none;"
                      "border-radius:4px;}"
                      "QProgressBar#migrationProgress::chunk{background-color:%3;border-radius:4px;}"
                      "QTextEdit#migrationLog{background-color:%8;color:%6;border:1px solid %9;"
                      "border-radius:10px;padding:8px;font-size:12px;}"
                      "QRadioButton{color:%6;font-size:13px;}")
                      .arg(cardBg, cardBorder, accent, title, sub, text, danger, rowBg,
                           rowBorder, light ? QStringLiteral("#E4E4E6") : QStringLiteral("#3D3D42"),
                           inputBg, inputBorder));
}

/* ============================================================ 扫描 */

void MigrationWizardDialog::startScan()
{
    if (m_scanning)
        return;

    // 清空上一次的结果（自定义目录也一起清掉，重新扫描后可以再手动加）
    clearLayout(m_scanListLayout);
    m_scanListLayout->addStretch(1);
    m_scanRows.clear();
    m_sources.clear();
    m_sourceIndex.clear();
    m_scanKeys.clear();

    QStringList keys = LauncherImporter::supportedKeys();
    if (!LauncherImporter::isAndroidTarget() && !keys.contains(QStringLiteral("official")))
        keys << QStringLiteral("official");

    m_scanKeys = keys;
    for (const QString &key : std::as_const(keys))
        addScanRow(key, MigrationService::displayNameFor(key), false);

    m_scanning = true;
    m_scanDone = false;
    m_scanPending = keys.size();
    m_scanDots = 0;
    m_scanStatus->setText(tr("正在并行扫描 %1 个启动器…").arg(keys.size()));
    m_scanBar->setVisible(true);
    m_scanBar->setRange(0, 0);
    m_rescanBtn->setEnabled(false);
    updateNavState();

    if (!m_scanDotTimer) {
        m_scanDotTimer = new QTimer(this);
        m_scanDotTimer->setInterval(380);
        connect(m_scanDotTimer, &QTimer::timeout, this, [this]() {
            m_scanDots = (m_scanDots + 1) % 4;
            m_scanStatus->setText(
                tr("正在并行扫描 %1 个启动器%2")
                    .arg(m_scanKeys.size())
                    .arg(QString(m_scanDots, QLatin1Char('.'))));
        });
    }
    m_scanDotTimer->start();

    for (const QString &key : std::as_const(keys)) {
        auto *watcher = new QFutureWatcher<LauncherDetect>(this);
        connect(watcher, &QFutureWatcher<LauncherDetect>::finished, this,
                [this, key, watcher]() {
                    onScanFinished(key, watcher->result());
                    watcher->deleteLater();
                });
        watcher->setFuture(QtConcurrent::run([key]() {
            return LauncherImporter::detectOne(key);
        }));
    }
}

void MigrationWizardDialog::addScanRow(const QString &key, const QString &name, bool custom)
{
    auto *row = new QFrame(m_scanListHost);
    row->setObjectName(QStringLiteral("migrationScanRow"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 9, 12, 9);
    layout->setSpacing(10);

    auto *logo = new QLabel(launcherLetter(key.section(QLatin1Char(':'), 0, 0)), row);
    logo->setFixedSize(34, 34);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(badgeStyle(key.section(QLatin1Char(':'), 0, 0)));
    layout->addWidget(logo);

    auto *texts = new QVBoxLayout();
    texts->setSpacing(1);
    auto *nameLabel = new QLabel(name, row);
    nameLabel->setObjectName(QStringLiteral("migrationScanName"));
    auto *detail = new QLabel(custom ? tr("手动添加的游戏目录") : tr("等待扫描…"), row);
    detail->setObjectName(QStringLiteral("migrationScanDetail"));
    detail->setWordWrap(true);
    texts->addWidget(nameLabel);
    texts->addWidget(detail);
    layout->addLayout(texts, 1);

    auto *status = new QLabel(custom ? tr("已添加") : tr("扫描中…"), row);
    status->setObjectName(QStringLiteral("migrationScanStatus"));
    status->setProperty("state", custom ? QStringLiteral("found") : QStringLiteral("scanning"));
    layout->addWidget(status, 0, Qt::AlignTop);

    ScanRow scanRow;
    scanRow.status = status;
    scanRow.detail = detail;
    scanRow.frame = row;
    m_scanRows.insert(key, scanRow);

    // 插在末尾的 stretch 之前
    m_scanListLayout->insertWidget(qMax(0, m_scanListLayout->count() - 1), row);
}

void MigrationWizardDialog::onScanFinished(const QString &key, const LauncherDetect &detect)
{
    const ScanRow row = m_scanRows.value(key);
    if (row.status) {
        row.status->setText(detect.detected ? tr("已找到") : tr("未安装"));
        row.status->setProperty("state", detect.detected ? QStringLiteral("found")
                                                         : QStringLiteral("missing"));
        row.status->style()->unpolish(row.status);
        row.status->style()->polish(row.status);
    }
    if (row.detail) {
        QStringList bits;
        if (detect.detected) {
            if (!detect.gameDirs.isEmpty())
                bits << tr("%1 个游戏目录").arg(detect.gameDirs.size());
            if (detect.versionCount > 0)
                bits << tr("%1 个版本").arg(detect.versionCount);
            if (detect.accountCount > 0)
                bits << tr("%1 个账户").arg(detect.accountCount);
            if (!detect.javaPaths.isEmpty())
                bits << tr("%1 个 Java").arg(detect.javaPaths.size());
            if (bits.isEmpty())
                bits << (detect.detail.isEmpty() ? tr("可读取配置") : detect.detail);
        } else {
            bits << tr("没有找到配置或游戏目录");
        }
        row.detail->setText(bits.join(QStringLiteral(" · ")));
    }

    if (detect.detected) {
        SourceState source;
        source.key = key;
        source.detect = detect;
        source.selected = true;
        source.accounts = detect.accountCount > 0;
        source.java = !detect.javaPaths.isEmpty();
        source.authServers = !detect.authServers.isEmpty();
        source.downloadSettings = false;
        source.memory = detect.javaMaxMemoryMb > 0 || detect.autoMemoryKnown;
        for (const LauncherGameDirInfo &info : detect.gameDirInfos)
            source.dirs.insert(QDir(info.path).absolutePath());
        const bool hasContent = !source.dirs.isEmpty() || source.accounts || source.java
                                || source.authServers || source.memory || source.downloadSettings;
        if (hasContent) {
            m_sourceIndex.insert(key, m_sources.size());
            m_sources.append(source);
        } else if (row.detail) {
            row.detail->setText(tr("配置能读到，但没有可迁移的版本 / 账户 / Java"));
        }
    }

    if (m_scanPending > 0)
        --m_scanPending;
    finishScanIfDone();
}

void MigrationWizardDialog::finishScanIfDone()
{
    if (m_scanPending > 0 || !m_scanning)
        return;

    m_scanning = false;
    m_scanDone = true;
    if (m_scanDotTimer)
        m_scanDotTimer->stop();
    m_rescanBtn->setEnabled(true);
    m_scanBar->setVisible(false);

    // 并行扫描完成顺序不固定：按扫描页的行序重排，卡片顺序每次一致（手动添加的排最后）
    std::stable_sort(m_sources.begin(), m_sources.end(),
                     [this](const SourceState &a, const SourceState &b) {
                         int ra = m_scanKeys.indexOf(a.key);
                         int rb = m_scanKeys.indexOf(b.key);
                         if (ra < 0)
                             ra = 10000;
                         if (rb < 0)
                             rb = 10000;
                         return ra < rb;
                     });
    m_sourceIndex.clear();
    for (int i = 0; i < m_sources.size(); ++i)
        m_sourceIndex.insert(m_sources.at(i).key, i);

    int versions = 0;
    for (const SourceState &source : std::as_const(m_sources))
        versions += source.detect.versionCount;
    m_scanStatus->setText(
        m_sources.isEmpty()
            ? tr("没有检测到可迁移的启动器")
            : tr("扫描完成：找到 %1 个可迁移来源，共 %2 个版本")
                  .arg(m_sources.size())
                  .arg(versions));

    updateNavState();
    if (m_sources.isEmpty()) {
        m_footerHint->setText(
            tr("没找到也没关系：点「添加游戏目录…」手动指向任意 .minecraft 文件夹，"
               "或者先用旧启动器玩一次再回来扫描。"));
    }
}

void MigrationWizardDialog::onAddCustomDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择游戏目录（里面要有 versions 文件夹）"), QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty())
        return;

    const QString abs = QDir(dir).absolutePath();
    if (MigrationService::countVersions(abs) <= 0) {
        m_footerHint->setText(
            tr("这个目录里没有找到可启动的版本。请选择包含 versions 文件夹的 .minecraft 目录。"));
        return;
    }
    for (const SourceState &source : std::as_const(m_sources)) {
        for (const QString &existing : source.detect.gameDirs) {
            if (QDir(existing).absolutePath().compare(abs, Qt::CaseInsensitive) == 0) {
                m_footerHint->setText(tr("这个游戏目录已经在列表里了。"));
                return;
            }
        }
    }

    LauncherDetect detect;
    detect.key = QStringLiteral("custom");
    detect.displayName = QFileInfo(abs).fileName();
    detect.detected = true;
    detect.minecraftDir = abs;
    detect.gameDirs << abs;
    detect.versionCount = MigrationService::countVersions(abs);

    LauncherGameDirInfo info;
    info.path = abs;
    info.versions = [abs]() {
        QStringList names;
        const QDir versionsDir(QDir(abs).filePath(QStringLiteral("versions")));
        const QStringList entries =
            versionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &name : entries) {
            if (QFileInfo::exists(versionsDir.absoluteFilePath(
                    name + QLatin1Char('/') + name + QStringLiteral(".json")))) {
                names << name;
            }
        }
        return names;
    }();
    detect.gameDirInfos.append(info);

    SourceState source;
    source.key = QStringLiteral("custom");
    source.detect = detect;
    source.custom = true;
    source.selected = true;
    source.accounts = false;
    source.java = false;
    source.authServers = false;
    source.downloadSettings = false;
    source.memory = false;
    source.dirs.insert(abs);
    m_sources.append(source);

    const QString rowKey = QStringLiteral("custom:") + abs;
    addScanRow(rowKey, QFileInfo(abs).fileName(), true);
    const ScanRow row = m_scanRows.value(rowKey);
    if (row.detail)
        row.detail->setText(shortPath(abs, 64));

    if (m_step == 1)
        rebuildSourceCards();
    if (!m_scanning) {
        m_scanDone = true;
        if (m_scanStatus)
            m_scanStatus->setText(tr("已添加手动目录，可以继续扫描或直接点「下一步」"));
    }
    updateNavState();
}

/* ============================================================ 选择页 */

void MigrationWizardDialog::rebuildSourceCards()
{
    if (!m_selectListLayout)
        return;
    clearLayout(m_selectListLayout);
    m_selectListLayout->addStretch(1);

    int added = 0;
    for (int i = 0; i < m_sources.size(); ++i) {
        if (!m_sources.at(i).detect.detected)
            continue;
        QWidget *card = createSourceCard(i);
        m_selectListLayout->insertWidget(m_selectListLayout->count() - 1, card);
        ++added;
    }
    if (added == 0) {
        auto *empty = new QLabel(tr("没有可以选择的来源。回到上一步手动添加游戏目录试试。"),
                                 m_selectListHost);
        empty->setObjectName(QStringLiteral("migrationMuted"));
        empty->setWordWrap(true);
        m_selectListLayout->insertWidget(0, empty);
    }
    updateSelectionSummary();
}

QWidget *MigrationWizardDialog::createSourceCard(int index)
{
    SourceState &source = m_sources[index];
    const LauncherDetect &detect = source.detect;

    auto *card = new QFrame(m_selectListHost);
    card->setObjectName(QStringLiteral("migrationSourceCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    // ── 标题行 ──
    auto *header = new QHBoxLayout();
    header->setSpacing(10);
    auto *check = new QCheckBox(card);
    check->setCursor(Qt::PointingHandCursor);
    check->setChecked(source.selected);
    header->addWidget(check);

    auto *logo = new QLabel(launcherLetter(source.key), card);
    logo->setFixedSize(36, 36);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(badgeStyle(source.custom ? QStringLiteral("custom") : source.key));
    header->addWidget(logo);

    auto *nameBox = new QVBoxLayout();
    nameBox->setSpacing(1);
    const QString cardName = source.custom && !source.detect.displayName.isEmpty()
                                 ? source.detect.displayName
                                 : MigrationService::displayNameFor(source.key);
    auto *name = new QLabel(cardName, card);
    name->setObjectName(QStringLiteral("migrationSourceName"));
    auto *summary = new QLabel(sourceSummary(source), card);
    summary->setObjectName(QStringLiteral("migrationSourceSummary"));
    summary->setWordWrap(true);
    nameBox->addWidget(name);
    nameBox->addWidget(summary);
    header->addLayout(nameBox, 1);

    auto *expandBtn = new QPushButton(source.expanded ? tr("收起") : tr("展开"), card);
    expandBtn->setObjectName(QStringLiteral("migrationLink"));
    expandBtn->setCursor(Qt::PointingHandCursor);
    header->addWidget(expandBtn);
    layout->addLayout(header);

    // ── 明细 ──
    auto *body = new QWidget(card);
    body->setObjectName(QStringLiteral("migrationSourceBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(46, 2, 0, 2);
    bodyLayout->setSpacing(6);
    body->setVisible(source.expanded);

    const auto syncHeader = [this, index, check]() {
        if (index < 0 || index >= m_sources.size())
            return;
        SourceState &s = m_sources[index];
        int total = 0;
        int on = 0;
        for (const LauncherGameDirInfo &info : s.detect.gameDirInfos) {
            ++total;
            if (s.dirs.contains(QDir(info.path).absolutePath()))
                ++on;
        }
        if (s.detect.accountCount > 0) {
            ++total;
            if (s.accounts)
                ++on;
        }
        if (!s.detect.javaPaths.isEmpty()) {
            ++total;
            if (s.java)
                ++on;
        }
        if (!s.detect.authServers.isEmpty()) {
            ++total;
            if (s.authServers)
                ++on;
        }
        if (s.detect.downloadSource >= 0 || s.detect.downloadThreads > 0) {
            ++total;
            if (s.downloadSettings)
                ++on;
        }
        if (s.detect.javaMaxMemoryMb > 0 || s.detect.autoMemoryKnown) {
            ++total;
            if (s.memory)
                ++on;
        }
        QSignalBlocker blocker(check);
        check->setCheckState(on <= 0 ? Qt::Unchecked
                                     : (on >= total ? Qt::Checked : Qt::PartiallyChecked));
        s.selected = (on > 0);
    };

    // 游戏目录
    if (!detect.gameDirInfos.isEmpty()) {
        auto *section = new QLabel(tr("游戏目录"), body);
        section->setObjectName(QStringLiteral("migrationPathMeta"));
        bodyLayout->addWidget(section);
    }
    for (const LauncherGameDirInfo &info : detect.gameDirInfos) {
        const QString abs = QDir(info.path).absolutePath();
        auto *row = new QHBoxLayout();
        row->setSpacing(8);
        auto *dirCheck = new QCheckBox(body);
        dirCheck->setCursor(Qt::PointingHandCursor);
        dirCheck->setChecked(source.dirs.contains(abs));
        row->addWidget(dirCheck, 0, Qt::AlignTop);

        auto *texts = new QVBoxLayout();
        texts->setSpacing(1);
        auto *pathLabel = new QLabel(shortPath(abs), body);
        pathLabel->setObjectName(QStringLiteral("migrationPath"));
        pathLabel->setToolTip(QDir::toNativeSeparators(abs));
        auto *meta = new QLabel(
            tr("%1 个版本 · %2 个模组 · %3 个存档")
                .arg(info.versions.size())
                .arg(info.modCount)
                .arg(info.saveCount),
            body);
        meta->setObjectName(QStringLiteral("migrationPathMeta"));
        texts->addWidget(pathLabel);
        texts->addWidget(meta);
        row->addLayout(texts, 1);
        bodyLayout->addLayout(row);

        connect(dirCheck, &QCheckBox::toggled, this,
                [this, &source, abs, syncHeader](bool on) {
                    if (on)
                        source.dirs.insert(abs);
                    else
                        source.dirs.remove(abs);
                    syncHeader();
                    updateSelectionSummary();
                });
    }

    // 账户
    if (detect.accountCount > 0) {
        auto *accountCheck = new QCheckBox(tr("导入 %1 个账户").arg(detect.accountCount), body);
        accountCheck->setObjectName(QStringLiteral("migrationOptionCheck"));
        accountCheck->setChecked(source.accounts);
        accountCheck->setCursor(Qt::PointingHandCursor);
        if (!detect.accountNames.isEmpty())
            accountCheck->setToolTip(detect.accountNames.join(QLatin1Char('\n')));
        bodyLayout->addWidget(accountCheck);
        connect(accountCheck, &QCheckBox::toggled, this,
                [this, &source, syncHeader](bool on) {
                    source.accounts = on;
                    syncHeader();
                    updateSelectionSummary();
                });
    } else if (!source.custom) {
        auto *none = new QLabel(
            tr("没有可读取的账户（导入后可在方块盒子账户页重新登录）"), body);
        none->setObjectName(QStringLiteral("migrationPathMeta"));
        none->setWordWrap(true);
        bodyLayout->addWidget(none);
    }

    // Java
    if (!detect.javaPaths.isEmpty()) {
        auto *javaCheck =
            new QCheckBox(tr("导入 %1 个 Java").arg(detect.javaPaths.size()), body);
        javaCheck->setChecked(source.java);
        javaCheck->setCursor(Qt::PointingHandCursor);
        javaCheck->setToolTip(detect.javaPaths.join(QLatin1Char('\n')));
        bodyLayout->addWidget(javaCheck);
        connect(javaCheck, &QCheckBox::toggled, this,
                [this, &source, syncHeader](bool on) {
                    source.java = on;
                    syncHeader();
                    updateSelectionSummary();
                });
    }

    // 第三方登录服务器
    if (!detect.authServers.isEmpty()) {
        auto *authCheck = new QCheckBox(
            tr("导入 %1 个第三方登录服").arg(detect.authServers.size()), body);
        authCheck->setChecked(source.authServers);
        authCheck->setCursor(Qt::PointingHandCursor);
        QStringList names;
        for (const LauncherAuthServer &server : detect.authServers)
            names << (server.name.isEmpty() ? server.url : server.name);
        authCheck->setToolTip(names.join(QLatin1Char('\n')));
        bodyLayout->addWidget(authCheck);
        connect(authCheck, &QCheckBox::toggled, this,
                [this, &source, syncHeader](bool on) {
                    source.authServers = on;
                    syncHeader();
                    updateSelectionSummary();
                });
    }

    // 下载设置
    if (detect.downloadSource >= 0 || detect.downloadThreads > 0) {
        QStringList bits;
        if (detect.downloadSource == 1)
            bits << tr("镜像下载");
        else if (detect.downloadSource == 0)
            bits << tr("官方下载");
        if (detect.downloadThreads > 0)
            bits << tr("%1 线程").arg(detect.downloadThreads);
        auto *downloadCheck = new QCheckBox(
            tr("沿用下载设置（%1）").arg(bits.join(QStringLiteral(" · "))), body);
        downloadCheck->setChecked(source.downloadSettings);
        downloadCheck->setCursor(Qt::PointingHandCursor);
        bodyLayout->addWidget(downloadCheck);
        connect(downloadCheck, &QCheckBox::toggled, this,
                [this, &source, syncHeader](bool on) {
                    source.downloadSettings = on;
                    syncHeader();
                    updateSelectionSummary();
                });
    }

    // 内存
    if (detect.javaMaxMemoryMb > 0 || detect.autoMemoryKnown) {
        QStringList bits;
        if (detect.javaMaxMemoryMb > 0)
            bits << tr("上限 %1 MB").arg(detect.javaMaxMemoryMb);
        if (detect.autoMemoryKnown)
            bits << (detect.autoMemory ? tr("自动分配") : tr("手动分配"));
        auto *memoryCheck =
            new QCheckBox(tr("沿用内存设置（%1）").arg(bits.join(QStringLiteral(" · "))), body);
        memoryCheck->setChecked(source.memory);
        memoryCheck->setCursor(Qt::PointingHandCursor);
        bodyLayout->addWidget(memoryCheck);
        connect(memoryCheck, &QCheckBox::toggled, this,
                [this, &source, syncHeader](bool on) {
                    source.memory = on;
                    syncHeader();
                    updateSelectionSummary();
                });
    }

    if (source.key == QStringLiteral("baka")) {
        auto *note = new QLabel(tr("BakaXL 的账户是设备加密的，读不到，只能带游戏目录和版本。"), body);
        note->setObjectName(QStringLiteral("migrationPathMeta"));
        note->setWordWrap(true);
        bodyLayout->addWidget(note);
    }

    layout->addWidget(body);
    if (!source.expanded)
        body->setVisible(false);

    connect(expandBtn, &QPushButton::clicked, this, [this, index, body, expandBtn]() {
        if (index < 0 || index >= m_sources.size())
            return;
        SourceState &s = m_sources[index];
        s.expanded = !s.expanded;
        body->setVisible(s.expanded);
        expandBtn->setText(s.expanded ? tr("收起") : tr("展开"));
    });

    connect(check, &QCheckBox::clicked, this, [this, index, check](bool) {
        if (index < 0 || index >= m_sources.size())
            return;
        SourceState &s = m_sources[index];
        const bool on = (check->checkState() != Qt::Unchecked);
        s.selected = on;
        s.accounts = on && s.detect.accountCount > 0;
        s.java = on && !s.detect.javaPaths.isEmpty();
        s.authServers = on && !s.detect.authServers.isEmpty();
        s.downloadSettings =
            on && (s.detect.downloadSource >= 0 || s.detect.downloadThreads > 0);
        s.memory = on && (s.detect.javaMaxMemoryMb > 0 || s.detect.autoMemoryKnown);
        s.dirs.clear();
        if (on) {
            for (const LauncherGameDirInfo &info : s.detect.gameDirInfos)
                s.dirs.insert(QDir(info.path).absolutePath());
        }
        // 重建会删掉当前复选框，放到下一轮事件循环再做
        QTimer::singleShot(0, this, [this]() {
            rebuildSourceCards();
            updateSelectionSummary();
        });
    });

    return card;
}

QString MigrationWizardDialog::sourceSummary(const SourceState &source) const
{
    const LauncherDetect &detect = source.detect;
    QStringList bits;
    if (detect.versionCount > 0)
        bits << tr("%1 个版本").arg(detect.versionCount);
    if (detect.accountCount > 0)
        bits << tr("%1 个账户").arg(detect.accountCount);
    else if (!source.custom)
        bits << tr("账户需重新登录");
    if (!detect.javaPaths.isEmpty())
        bits << tr("%1 个 Java").arg(detect.javaPaths.size());
    if (!detect.authServers.isEmpty())
        bits << tr("%1 个登录服").arg(detect.authServers.size());
    if (detect.downloadSource >= 0 || detect.downloadThreads > 0)
        bits << tr("可带下载设置");
    if (detect.javaMaxMemoryMb > 0 || detect.autoMemoryKnown)
        bits << tr("可带内存设置");
    if (bits.isEmpty())
        bits << tr("可导入");
    return bits.join(QStringLiteral(" · "));
}

QStringList MigrationWizardDialog::selectedGameDirs() const
{
    QStringList dirs;
    QSet<QString> seen;
    for (const SourceState &source : m_sources) {
        if (!source.selected)
            continue;
        for (const LauncherGameDirInfo &info : source.detect.gameDirInfos) {
            const QString abs = QDir(info.path).absolutePath();
            if (!source.dirs.contains(abs))
                continue;
            const QString key = abs.toLower();
            if (seen.contains(key))
                continue;
            seen.insert(key);
            dirs << abs;
        }
    }
    return dirs;
}

int MigrationWizardDialog::selectedDirCount() const
{
    return selectedGameDirs().size();
}

int MigrationWizardDialog::selectedVersionCount() const
{
    int count = 0;
    QSet<QString> seen;
    for (const SourceState &source : m_sources) {
        if (!source.selected)
            continue;
        for (const LauncherGameDirInfo &info : source.detect.gameDirInfos) {
            const QString abs = QDir(info.path).absolutePath();
            if (!source.dirs.contains(abs) || seen.contains(abs.toLower()))
                continue;
            seen.insert(abs.toLower());
            count += info.versions.size();
        }
    }
    return count;
}

int MigrationWizardDialog::selectedAccountCount() const
{
    int count = 0;
    for (const SourceState &source : m_sources) {
        if (source.selected && source.accounts)
            count += source.detect.accountCount;
    }
    return count;
}

int MigrationWizardDialog::selectedJavaCount() const
{
    int count = 0;
    for (const SourceState &source : m_sources) {
        if (source.selected && source.java)
            count += source.detect.javaPaths.size();
    }
    return count;
}

int MigrationWizardDialog::selectedAuthCount() const
{
    int count = 0;
    for (const SourceState &source : m_sources) {
        if (source.selected && source.authServers)
            count += source.detect.authServers.size();
    }
    return count;
}

bool MigrationWizardDialog::hasAnySelection() const
{
    if (selectedDirCount() > 0 || selectedAccountCount() > 0 || selectedJavaCount() > 0
        || selectedAuthCount() > 0) {
        return true;
    }
    for (const SourceState &source : m_sources) {
        if (source.selected && (source.downloadSettings || source.memory))
            return true;
    }
    return false;
}

void MigrationWizardDialog::updateSelectionSummary()
{
    if (!m_selectSummary)
        return;
    if (!hasAnySelection()) {
        m_selectSummary->setText(tr("还没有勾选任何内容"));
        updateNavState();
        return;
    }
    QStringList bits;
    int selectedSources = 0;
    for (const SourceState &source : std::as_const(m_sources)) {
        if (!source.selected)
            continue;
        if (!source.dirs.isEmpty() || source.accounts || source.java || source.authServers
            || source.downloadSettings || source.memory) {
            ++selectedSources;
        }
    }
    bits << tr("已选 %1 个来源").arg(selectedSources);
    if (selectedDirCount() > 0)
        bits << tr("%1 个游戏目录").arg(selectedDirCount());
    if (selectedVersionCount() > 0)
        bits << tr("%1 个版本").arg(selectedVersionCount());
    if (selectedAccountCount() > 0)
        bits << tr("%1 个账户").arg(selectedAccountCount());
    if (selectedJavaCount() > 0)
        bits << tr("%1 个 Java").arg(selectedJavaCount());
    if (selectedAuthCount() > 0)
        bits << tr("%1 个登录服").arg(selectedAuthCount());
    int downloadSources = 0;
    int memorySources = 0;
    for (const SourceState &source : std::as_const(m_sources)) {
        if (!source.selected)
            continue;
        if (source.downloadSettings)
            ++downloadSources;
        if (source.memory)
            ++memorySources;
    }
    if (downloadSources > 0)
        bits << tr("下载设置");
    if (memorySources > 0)
        bits << tr("内存设置");
    m_selectSummary->setText(bits.join(QStringLiteral(" · ")));
    updateNavState();
}

/* ============================================================ 导航 */

void MigrationWizardDialog::showStep(int step)
{
    if (step < 0)
        step = 0;
    m_step = step;
    if (step <= 2)
        m_stack->setCurrentIndex(step);
    else if (step == 3)
        m_stack->setCurrentIndex(3);
    updateStepPills();
    updateNavState();
}

void MigrationWizardDialog::onNextClicked()
{
    if (m_migrating)
        return;
    if (m_step == 0) {
        if (!m_scanDone)
            return;
        showStep(1);
        rebuildSourceCards();
        return;
    }
    if (m_step == 1) {
        if (!hasAnySelection()) {
            m_footerHint->setText(tr("先勾选至少一个要迁移的内容。"));
            return;
        }
        // 上一页可能改过勾选：重新进方式页时重算复制占用
        m_totalEstimate = 0;
        m_estimating = false;
        showStep(2);
        refreshOptionsPage();
        return;
    }
    if (m_step == 2) {
        startMigration();
    }
}

void MigrationWizardDialog::onPrevClicked()
{
    if (m_migrating)
        return;
    if (m_step == 2)
        showStep(1);
    else if (m_step == 1)
        showStep(0);
}

void MigrationWizardDialog::updateStepPills()
{
    for (int i = 0; i < m_stepPills.size(); ++i) {
        QLabel *pill = m_stepPills.at(i);
        QString state = QStringLiteral("idle");
        if (i < m_step)
            state = QStringLiteral("done");
        else if (i == m_step)
            state = QStringLiteral("active");
        pill->setProperty("state", state);
        pill->style()->unpolish(pill);
        pill->style()->polish(pill);
    }
    const QString lineColor = (ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme)
                                  ? QStringLiteral("#DADADA")
                                  : QStringLiteral("#4A4A50");
    for (QFrame *line : m_stepLines) {
        line->setStyleSheet(QStringLiteral("background-color:%1;border:none;").arg(lineColor));
    }
}

void MigrationWizardDialog::updateNavState()
{
    if (!m_stack)
        return;
    const bool resultPage = (m_stack->currentIndex() == 4);
    const bool copy = m_copyRadio && m_copyRadio->isChecked();

    m_prevBtn->setVisible(!resultPage && !m_migrating && m_step >= 1 && m_step <= 2);
    m_nextBtn->setVisible(!resultPage && !m_migrating && m_step <= 2);
    m_cancelBtn->setVisible(m_migrating);
    m_doneBtn->setVisible(resultPage && !m_migrating);
    m_instancesBtn->setVisible(resultPage && !m_migrating && m_openInstancesAvailable);

    if (m_nextBtn->isVisible()) {
        m_nextBtn->setText(m_step == 2
                               ? (copy ? tr("复制并开始迁移") : tr("开始迁移"))
                               : tr("下一步"));
        bool enabled = false;
        if (m_step == 0) {
            enabled = m_scanDone;
            if (enabled) {
                enabled = false;
                for (const SourceState &source : m_sources) {
                    if (source.detect.detected) {
                        enabled = true;
                        break;
                    }
                }
            }
        } else if (m_step == 1) {
            enabled = hasAnySelection();
        } else if (m_step == 2) {
            enabled = hasAnySelection();
        }
        m_nextBtn->setEnabled(enabled);
    }

    if (m_migrating) {
        m_footerHint->setText(tr("正在迁移，请不要关闭窗口…"));
        return;
    }
    if (resultPage) {
        m_footerHint->setText(tr("迁移结果已保存，可以在实例列表里使用新搬来的目录。"));
        return;
    }
    switch (m_step) {
    case 0:
        if (!m_scanning)
            m_footerHint->setText(tr("扫描只读取配置，不会修改或删除其他启动器的文件。"));
        break;
    case 1:
        m_footerHint->setText(tr("可以只挑一个游戏目录，或只带账户 / Java；展开每张卡就能细选。"));
        break;
    case 2:
        m_footerHint->setText(tr("默认只登记、不复制文件；选「复制一份」会多占一份磁盘空间。"));
        break;
    default:
        break;
    }
}

/* ============================================================ 方式页 */

void MigrationWizardDialog::refreshOptionsPage()
{
    if (!m_optionsPage)
        return;
    const bool copy = m_copyRadio->isChecked();
    m_copyRootEdit->setEnabled(copy);
    m_browseCopyRootBtn->setEnabled(copy);
    m_copyEstimate->setEnabled(copy);
    m_diskInfo->setEnabled(copy);

    QStringList bits;
    if (selectedDirCount() > 0) {
        bits << (copy ? tr("复制 %1 个游戏目录").arg(selectedDirCount())
                      : tr("登记 %1 个游戏目录").arg(selectedDirCount()));
    }
    if (selectedVersionCount() > 0)
        bits << tr("%1 个版本").arg(selectedVersionCount());
    if (selectedAccountCount() > 0)
        bits << tr("%1 个账户").arg(selectedAccountCount());
    if (selectedJavaCount() > 0)
        bits << tr("%1 个 Java").arg(selectedJavaCount());
    if (selectedAuthCount() > 0)
        bits << tr("%1 个第三方登录服").arg(selectedAuthCount());

    int downloadSources = 0;
    int memorySources = 0;
    for (const SourceState &source : m_sources) {
        if (!source.selected)
            continue;
        if (source.downloadSettings)
            ++downloadSources;
        if (source.memory)
            ++memorySources;
    }
    if (downloadSources > 0)
        bits << tr("下载设置");
    if (memorySources > 0)
        bits << tr("内存设置");
    m_optionSummary->setText(bits.isEmpty() ? tr("还没有勾选任何内容")
                                            : tr("将迁移：%1").arg(bits.join(QStringLiteral(" · "))));

    if (copy) {
        if (m_totalEstimate <= 0 && !m_estimating)
            updateCopyEstimate();
        updateDiskInfo();
        updateCopyWarning();
    } else {
        m_optionWarning->clear();
    }
    updateNavState();
}

void MigrationWizardDialog::updateCopyEstimate()
{
    if (!m_copyRadio || !m_copyRadio->isChecked())
        return;

    QStringList dirs;
    const QStringList selected = selectedGameDirs();
    for (const QString &dir : selected)
        dirs << QDir::toNativeSeparators(dir);
    if (dirs.isEmpty()) {
        m_estimating = false;
        m_totalEstimate = 0;
        m_copyEstimate->setText(tr("还没有选择要复制的游戏目录"));
        return;
    }

    const qint64 generation = ++m_estimateGeneration;
    m_estimating = true;
    m_totalEstimate = 0;
    m_copyEstimate->setText(tr("正在估算占用空间…"));
    updateNavState();

    auto *watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, &QFutureWatcher<qint64>::finished, this,
            [this, generation, watcher]() {
                const qint64 total = watcher->result();
                watcher->deleteLater();
                if (generation != m_estimateGeneration)
                    return;
                m_estimating = false;
                m_totalEstimate = total;
                m_copyEstimate->setText(tr("预计占用：%1").arg(formatBytes(total)));
                updateDiskInfo();
                updateCopyWarning();
                updateNavState();
            });
    watcher->setFuture(QtConcurrent::run([dirs]() {
        qint64 total = 0;
        for (const QString &dir : dirs)
            total += MigrationService::directorySize(dir);
        return total;
    }));
}

void MigrationWizardDialog::updateDiskInfo()
{
    if (!m_diskInfo)
        return;
    const QString root = m_copyRootEdit ? m_copyRootEdit->text().trimmed() : QString();
    if (root.isEmpty()) {
        m_diskInfo->setText(tr("还没有选择复制位置"));
        return;
    }
    QString existing = root;
    while (!existing.isEmpty() && !QFileInfo::exists(existing)) {
        const QString parent = QFileInfo(existing).absolutePath();
        if (parent == existing)
            break;
        existing = parent;
    }
    const QStorageInfo storage(existing);
    if (storage.isValid() && storage.isReady()) {
        m_diskInfo->setText(tr("所在磁盘可用：%1").arg(formatBytes(storage.bytesAvailable())));
    } else {
        m_diskInfo->setText(tr("暂时读不到磁盘剩余空间"));
    }
}

void MigrationWizardDialog::updateCopyWarning()
{
    if (!m_optionWarning)
        return;
    if (!m_copyRadio || !m_copyRadio->isChecked()) {
        m_optionWarning->clear();
        return;
    }
    const QString root = m_copyRootEdit->text().trimmed();
    if (root.isEmpty()) {
        m_optionWarning->setText(tr("请先选择复制到哪个文件夹。"));
        return;
    }
    // 只做只读检查：别在用户打字过程中就 mkdir 一串目录
    QString existing = root;
    while (!existing.isEmpty() && !QFileInfo::exists(existing)) {
        const QString parent = QFileInfo(existing).absolutePath();
        if (parent == existing)
            break;
        existing = parent;
    }
    if (existing.isEmpty() || !QFileInfo(existing).isDir()
        || !QFileInfo(existing).isWritable()) {
        m_optionWarning->setText(tr("这个位置不可写，请换一个文件夹（不要选系统盘根目录）。"));
        return;
    }
    if (m_totalEstimate > 0) {
        const QStorageInfo storage(root);
        if (storage.isValid() && storage.isReady()
            && storage.bytesAvailable() < m_totalEstimate + 64LL * 1024 * 1024) {
            m_optionWarning->setText(
                tr("磁盘空间可能不够：预计需要 %1，当前可用 %2。")
                    .arg(formatBytes(m_totalEstimate), formatBytes(storage.bytesAvailable())));
            return;
        }
    }
    m_optionWarning->clear();
}

/* ============================================================ 执行迁移 */

MigrationSelection MigrationWizardDialog::selectionFor(const SourceState &source) const
{
    MigrationSelection selection;
    for (const LauncherGameDirInfo &info : source.detect.gameDirInfos) {
        const QString abs = QDir(info.path).absolutePath();
        if (source.dirs.contains(abs))
            selection.gameDirs << abs;
    }
    selection.accounts = source.accounts;
    selection.java = source.java;
    selection.authServers = source.authServers;
    selection.downloadSettings = source.downloadSettings;
    selection.memory = source.memory;
    selection.copyGameDirs = m_copyRadio && m_copyRadio->isChecked();
    selection.copyRoot = m_copyRootEdit ? m_copyRootEdit->text().trimmed() : QString();
    return selection;
}

void MigrationWizardDialog::startMigration()
{
    if (m_migrating || !hasAnySelection())
        return;

    const bool copy = m_copyRadio->isChecked();
    const QString copyRoot = m_copyRootEdit->text().trimmed();
    if (copy) {
        if (copyRoot.isEmpty()) {
            m_optionWarning->setText(tr("请先选择复制到哪个文件夹。"));
            return;
        }
        if (!MigrationService::isWritableDir(copyRoot)) {
            m_optionWarning->setText(tr("复制位置不可写，请换一个文件夹。"));
            return;
        }
        const QString rootAbs = QDir(copyRoot).absolutePath();
        for (const SourceState &source : std::as_const(m_sources)) {
            if (!source.selected)
                continue;
            for (const LauncherGameDirInfo &info : source.detect.gameDirInfos) {
                const QString srcAbs = QDir(info.path).absolutePath();
                if (!source.dirs.contains(srcAbs))
                    continue;
                if (rootAbs.compare(srcAbs, Qt::CaseInsensitive) == 0
                    || rootAbs.startsWith(srcAbs + QLatin1Char('/'), Qt::CaseInsensitive)) {
                    m_optionWarning->setText(
                        tr("复制位置不能放在要复制的游戏目录里面：%1")
                            .arg(QDir::toNativeSeparators(srcAbs)));
                    return;
                }
            }
        }

        // 估算已经算完的话，先在开始前拦一次空间不足，别复制到一半才失败
        if (m_totalEstimate > 0) {
            const QStorageInfo storage(rootAbs);
            if (storage.isValid() && storage.isReady()
                && storage.bytesAvailable() < m_totalEstimate + 64LL * 1024 * 1024) {
                m_optionWarning->setText(
                    tr("磁盘空间可能不够：预计需要 %1，当前可用 %2。")
                        .arg(formatBytes(m_totalEstimate), formatBytes(storage.bytesAvailable())));
                return;
            }
        }
    }

    m_result = MigrationApplyResult();
    m_migrationLines.clear();
    m_copyQueue.clear();
    m_copyIndex = 0;
    m_copyJavaPaths.clear();
    m_cancelRequested = false;
    m_cancelBtn->setEnabled(true);
    m_cancelBtn->setText(tr("取消迁移"));
    m_progressBar->setValue(0);
    m_logView->clear();
    m_step = 3;
    m_stack->setCurrentIndex(3);
    updateStepPills();
    setMigrating(true);

    appendLog(tr("开始迁移…"));
    QSet<QString> handledDirs;
    for (int i = 0; i < m_sources.size(); ++i) {
        if (m_cancelRequested)
            break;
        SourceState &source = m_sources[i];
        if (!source.selected)
            continue;
        const MigrationSelection selection = selectionFor(source);
        if (selection.gameDirs.isEmpty() && !selection.accounts && !selection.java
            && !selection.authServers && !selection.downloadSettings && !selection.memory) {
            continue;
        }

        const int a0 = m_result.addedAccounts;
        const int f0 = m_result.addedFolders;
        const int v0 = m_result.versionCount;
        const int j0 = m_result.addedJavas;
        const int s0 = m_result.addedAuthServers;

        const bool wantsData = selection.accounts || selection.java || selection.authServers
                               || selection.downloadSettings || selection.memory;
        if (source.key != QStringLiteral("custom") && wantsData) {
            appendLog(tr("正在导入 %1 的账户 / Java / 设置…")
                          .arg(MigrationService::displayNameFor(source.key)));
            MigrationService::applySettingsAndAccounts(source.key, selection, m_result);
        }
        if (selection.java) {
            for (const QString &javaExe : source.detect.javaPaths)
                m_copyJavaPaths << javaExe;
        }

        int sourceDirCount = 0;
        int sharedDirCount = 0;
        for (const QString &dir : selection.gameDirs) {
            if (m_cancelRequested)
                break;
            // 多个启动器可能共用同一个 .minecraft：只处理一次，别复制出好几份
            const QString dirKey = QDir(dir).absolutePath().toLower();
            if (handledDirs.contains(dirKey)) {
                ++sharedDirCount;
                continue;
            }
            handledDirs.insert(dirKey);
            ++sourceDirCount;
            if (copy) {
                CopyJob job;
                job.key = source.key;
                job.sourceDir = dir;
                m_copyQueue << job;
            } else {
                MigrationService::registerGameDir(source.key, dir, dir, m_result);
            }
        }

        QStringList bits;
        if (m_result.addedAccounts - a0 > 0)
            bits << tr("%1 个账户").arg(m_result.addedAccounts - a0);
        if (m_result.addedFolders - f0 > 0)
            bits << tr("%1 个游戏目录").arg(m_result.addedFolders - f0);
        if (m_result.versionCount - v0 > 0)
            bits << tr("%1 个版本").arg(m_result.versionCount - v0);
        if (m_result.addedJavas - j0 > 0)
            bits << tr("%1 个 Java").arg(m_result.addedJavas - j0);
        if (m_result.addedAuthServers - s0 > 0)
            bits << tr("%1 个登录服").arg(m_result.addedAuthServers - s0);
        if (copy && sourceDirCount > 0)
            bits << tr("复制 %1 个目录").arg(sourceDirCount);
        if (bits.isEmpty() && sharedDirCount > 0)
            bits << tr("游戏目录与其他来源共用，已合并导入");
        m_migrationLines << tr("%1：%2")
                                .arg(MigrationService::displayNameFor(source.key),
                                     bits.isEmpty() ? tr("没有新增内容")
                                                    : bits.join(QStringLiteral(" · ")));
    }

    if (m_copyQueue.isEmpty() || m_cancelRequested) {
        finishMigration();
        return;
    }

    m_progressLabel->setText(tr("准备复制 %1 个游戏目录…").arg(m_copyQueue.size()));
    startNextCopy();
}

void MigrationWizardDialog::startNextCopy()
{
    if (m_cancelRequested || m_copyIndex >= m_copyQueue.size()) {
        finishMigration();
        return;
    }

    const CopyJob job = m_copyQueue.at(m_copyIndex);
    const QString root = m_copyRootEdit->text().trimmed();
    const QString destination = MigrationService::uniqueCopyDestination(
        root, MigrationService::folderNameFor(job.key, job.sourceDir));

    m_progressBar->setValue(0);
    m_progressLabel->setText(
        tr("正在复制 %1/%2：%3")
            .arg(m_copyIndex + 1)
            .arg(m_copyQueue.size())
            .arg(shortPath(job.sourceDir, 52)));
    appendLog(tr("复制到：%1").arg(QDir::toNativeSeparators(destination)));

    if (m_copyTask) {
        m_copyTask->deleteLater();
        m_copyTask = nullptr;
    }
    m_copyTask = new GameDirCopyTask(job.sourceDir, destination, this);
    connect(m_copyTask, &GameDirCopyTask::progress,
            this, &MigrationWizardDialog::onCopyProgress);
    connect(m_copyTask, &GameDirCopyTask::finishedCopy,
            this, &MigrationWizardDialog::onCopyFinished);
    m_copyTask->start();
}

void MigrationWizardDialog::onCopyProgress(qint64 copied, qint64 total,
                                           const QString &file)
{
    Q_UNUSED(file);
    if (total <= 0)
        return;
    int percent = static_cast<int>((copied * 100) / total);
    percent = qBound(0, percent, 100);
    m_progressBar->setValue(percent);
}

void MigrationWizardDialog::onCopyFinished(const QString &destination,
                                           const QString &error)
{
    const CopyJob job = m_copyQueue.value(m_copyIndex);
    if (m_copyTask) {
        m_copyTask->deleteLater();
        m_copyTask = nullptr;
    }

    if (error.isEmpty()) {
        MigrationService::registerGameDir(job.key, destination, job.sourceDir, m_result);
        ++m_result.copiedDirs;
        appendLog(tr("已复制：%1").arg(shortPath(job.sourceDir, 48)));
        m_migrationLines << tr("已复制到：%1").arg(shortPath(destination, 62));
    } else if (m_cancelRequested) {
        appendLog(tr("已取消复制，未完成的副本已清理"));
    } else {
        m_result.errors << error;
        appendLog(tr("复制失败：%1").arg(error));
    }

    ++m_copyIndex;
    if (m_cancelRequested)
        finishMigration();
    else
        startNextCopy();
}

void MigrationWizardDialog::finishMigration()
{
    m_migrating = false;
    m_migrationDone = true;
    setMigrating(false);

    if (m_result.copiedDirs > 0 || m_result.addedFolders > 0 || m_result.addedAccounts > 0
        || m_result.addedJavas > 0 || m_result.addedAuthServers > 0
        || m_result.downloadApplied || m_result.memoryApplied) {
        m_result.ok = true;
    }

    const QStringList warnings = MigrationService::verify(m_result, m_copyJavaPaths);
    for (const QString &warning : warnings)
        m_result.notes << warning;
    if (m_cancelRequested)
        m_result.notes << tr("已取消剩余的复制任务，没有完成的副本已经清理掉。");

    buildResultText();
    m_step = 3;
    m_stack->setCurrentIndex(4);
    updateStepPills();
    updateNavState();
}

void MigrationWizardDialog::buildResultText()
{
    if (!m_resultTitle || !m_resultSummary || !m_resultDetail)
        return;

    const bool success = m_result.ok;
    if (success && !m_result.errors.isEmpty())
        m_resultTitle->setText(tr("迁移完成，但有部分未能导入"));
    else if (success)
        m_resultTitle->setText(tr("迁移完成"));
    else if (m_result.errors.isEmpty())
        m_resultTitle->setText(tr("没有需要新增的内容"));
    else
        m_resultTitle->setText(tr("迁移没有完成"));

    QStringList summary;
    if (m_result.addedAccounts > 0)
        summary << tr("%1 个账户").arg(m_result.addedAccounts);
    if (m_result.addedFolders > 0)
        summary << tr("%1 个游戏目录").arg(m_result.addedFolders);
    if (m_result.versionCount > 0)
        summary << tr("%1 个版本").arg(m_result.versionCount);
    if (m_result.addedJavas > 0)
        summary << tr("%1 个 Java").arg(m_result.addedJavas);
    if (m_result.addedAuthServers > 0)
        summary << tr("%1 个登录服").arg(m_result.addedAuthServers);
    if (m_result.downloadApplied)
        summary << tr("下载设置");
    if (m_result.memoryApplied)
        summary << tr("内存设置");
    m_resultSummary->setText(summary.isEmpty()
                                 ? tr("这次没有新增内容，可以返回上一步重新勾选。")
                                 : tr("新增：%1").arg(summary.join(QStringLiteral(" · "))));

    QStringList html;
    if (!m_migrationLines.isEmpty()) {
        html << QStringLiteral("<b>%1</b>").arg(tr("逐项结果"));
        for (const QString &line : std::as_const(m_migrationLines))
            html << QStringLiteral("• %1").arg(line.toHtmlEscaped());
    }
    if (!m_result.notes.isEmpty()) {
        html << QStringLiteral("<br><b>%1</b>").arg(tr("说明"));
        for (const QString &note : std::as_const(m_result.notes))
            html << QStringLiteral("• %1").arg(note.toHtmlEscaped());
    }
    if (!m_result.errors.isEmpty()) {
        html << QStringLiteral("<br><b style='color:#DC5A54'>%1</b>").arg(tr("需要处理"));
        for (const QString &error : std::as_const(m_result.errors))
            html << QStringLiteral("• %1").arg(error.toHtmlEscaped());
    }
    if (html.isEmpty())
        html << tr("没有可显示的详情。");
    m_resultDetail->setText(html.join(QStringLiteral("<br>")));
}

void MigrationWizardDialog::setMigrating(bool migrating)
{
    m_migrating = migrating;
    updateNavState();
}

void MigrationWizardDialog::appendLog(const QString &text)
{
    if (!m_logView)
        return;
    m_logView->append(text);
}
