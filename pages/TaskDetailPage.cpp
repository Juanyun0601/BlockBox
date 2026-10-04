/**
 * @file   TaskDetailPage.cpp
 * @brief  任务详情页面实现（现代化重构版）
 * @author BlockBox Team
 * @date   2026-08-15
 */
#include "TaskDetailPage.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QFrame>
#include <QScrollBar>
#include <QStyle>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QUrl>

#include "components/AppMessageBox.h"

#include "utils/DownloadUtils.h"
#include "utils/ThemeManager.h"
#include "utils/VersionDownloader.h"

TaskDetailPage::TaskDetailPage(QWidget *parent)
    : QWidget(parent)
    , m_simulating(false)
    , m_animProgress(0.0)
    , m_animSpeed(0.0)
    , m_smoothEta(-1)
    , m_backButton(nullptr)
    , m_pageTitleLabel(nullptr)
    , m_statusPill(nullptr)
    , m_heroTitleLabel(nullptr)
    , m_heroSubLabel(nullptr)
    , m_heroChipsRow(nullptr)
    , m_heroChipsLayout(nullptr)
    , m_statSpeed(nullptr)
    , m_statSize(nullptr)
    , m_statEta(nullptr)
    , m_statThreads(nullptr)
    , m_ring(nullptr)
    , m_stageChip(nullptr)
    , m_stepper(nullptr)
    , m_bigPctLabel(nullptr)
    , m_bigBar(nullptr)
    , m_footLeft(nullptr)
    , m_footRight(nullptr)
    , m_speedChart(nullptr)
    , m_fileCountChip(nullptr)
    , m_fileListContainer(nullptr)
    , m_fileListLayout(nullptr)
    , m_infoGrid(nullptr)
    , m_logView(nullptr)
    , m_logCount(0)
    , m_pauseButton(nullptr)
    , m_cancelButton(nullptr)
    , m_restartButton(nullptr)
{
    initUI();

    DownloadTaskManager *manager = DownloadTaskManager::instance();
    connect(manager, &DownloadTaskManager::taskProgressUpdated,
            this, &TaskDetailPage::onTaskProgressUpdated);
    connect(manager, &DownloadTaskManager::taskStatusChanged,
            this, &TaskDetailPage::onTaskStatusChanged);
    connect(manager, &DownloadTaskManager::taskSpeedUpdated,
            this, &TaskDetailPage::onTaskSpeedUpdated);
    connect(manager, &DownloadTaskManager::taskStageChanged,
            this, &TaskDetailPage::onTaskStageChanged);
    connect(manager, &DownloadTaskManager::taskFileProgressUpdated,
            this, &TaskDetailPage::onTaskFileProgressUpdated);
    connect(manager, &DownloadTaskManager::taskRemoved,
            this, &TaskDetailPage::onTaskRemoved);

    m_animationTimer.setInterval(80);
    connect(&m_animationTimer, &QTimer::timeout, this, &TaskDetailPage::onAnimationTick);

    updateThemeColors();
}

TaskDetailPage::~TaskDetailPage()
{
}

/* ============================================================
 * UI 构建
 * ============================================================ */
QWidget *TaskDetailPage::createCard(QWidget *content, const QString &title,
                                    const QString &subtitle, const QString &cardTitleObject)
{
    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("tdCard"));

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 18, 22, 20);
    cardLayout->setSpacing(12);

    if (!title.isEmpty() || !subtitle.isEmpty())
    {
        QHBoxLayout *headLayout = new QHBoxLayout();
        headLayout->setSpacing(10);

        QLabel *titleLabel = new QLabel(title, card);
        titleLabel->setObjectName(cardTitleObject);
        headLayout->addWidget(titleLabel);

        if (!subtitle.isEmpty())
        {
            QLabel *subLabel = new QLabel(subtitle, card);
            subLabel->setObjectName(QStringLiteral("tdCardSub"));
            headLayout->addWidget(subLabel);
        }

        headLayout->addStretch();
        cardLayout->addLayout(headLayout);
    }

    cardLayout->addWidget(content);
    return card;
}

QLabel *TaskDetailPage::createStatTile(const QString &label, const QString &id)
{
    QWidget *tile = new QWidget(this);
    tile->setObjectName(QStringLiteral("tdStatTile"));
    tile->setMinimumWidth(120);

    QVBoxLayout *tileLayout = new QVBoxLayout(tile);
    tileLayout->setContentsMargins(14, 12, 14, 12);
    tileLayout->setSpacing(2);

    QLabel *labelWidget = new QLabel(label, tile);
    labelWidget->setObjectName(QStringLiteral("tdStatLabel"));
    tileLayout->addWidget(labelWidget);

    QLabel *valueWidget = new QLabel(QStringLiteral("—"), tile);
    valueWidget->setObjectName(QStringLiteral("tdStatValue"));
    valueWidget->setObjectName(id);
    valueWidget->setProperty("accent", true);
    tileLayout->addWidget(valueWidget);

    return valueWidget;
}

QLabel *TaskDetailPage::createChip(const QString &text)
{
    QLabel *chip = new QLabel(text, m_heroChipsRow);
    chip->setObjectName(QStringLiteral("tdChip"));
    return chip;
}

void TaskDetailPage::initUI()
{
    setObjectName(QStringLiteral("taskDetailPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    /* ---------- 顶栏 ---------- */
    QWidget *headerBar = new QWidget(this);
    headerBar->setObjectName(QStringLiteral("tdHeaderBar"));
    headerBar->setFixedHeight(56);

    QHBoxLayout *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(20, 8, 20, 8);
    headerLayout->setSpacing(12);

    m_backButton = new QPushButton(tr("← 返回列表"), headerBar);
    m_backButton->setObjectName(QStringLiteral("tdBackBtn"));
    m_backButton->setCursor(Qt::PointingHandCursor);
    connect(m_backButton, &QPushButton::clicked, this, &TaskDetailPage::onBackClicked);
    headerLayout->addWidget(m_backButton);

    m_pageTitleLabel = new QLabel(tr("下载详情"), headerBar);
    m_pageTitleLabel->setObjectName(QStringLiteral("tdPageTitle"));
    headerLayout->addWidget(m_pageTitleLabel);

    headerLayout->addStretch();

    m_statusPill = new QLabel(tr("等待中"), headerBar);
    m_statusPill->setObjectName(QStringLiteral("tdStatusPill"));
    m_statusPill->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(m_statusPill);

    mainLayout->addWidget(headerBar);

    /* ---------- 滚动内容 ---------- */
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("tdScroll"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *content = new QWidget(scrollArea);
    content->setObjectName(QStringLiteral("tdContent"));
    QVBoxLayout *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 20, 24, 40);
    contentLayout->setSpacing(18);

    /* --- Hero 卡 --- */
    QWidget *heroCard = new QWidget(content);
    heroCard->setObjectName(QStringLiteral("tdHero"));
    QHBoxLayout *heroLayout = new QHBoxLayout(heroCard);
    heroLayout->setContentsMargins(28, 24, 28, 24);
    heroLayout->setSpacing(28);

    QVBoxLayout *heroLeft = new QVBoxLayout();
    heroLeft->setSpacing(8);

    m_heroTitleLabel = new QLabel(tr("下载任务"), heroCard);
    m_heroTitleLabel->setObjectName(QStringLiteral("tdTitle"));
    m_heroTitleLabel->setWordWrap(true);
    heroLeft->addWidget(m_heroTitleLabel);

    m_heroSubLabel = new QLabel(QString(), heroCard);
    m_heroSubLabel->setObjectName(QStringLiteral("tdSub"));
    m_heroSubLabel->setWordWrap(true);
    heroLeft->addWidget(m_heroSubLabel);

    m_heroChipsRow = new QWidget(heroCard);
    m_heroChipsLayout = new QHBoxLayout(m_heroChipsRow);
    m_heroChipsLayout->setContentsMargins(0, 10, 0, 0);
    m_heroChipsLayout->setSpacing(8);
    m_heroChipsLayout->addWidget(createChip(tr("实例：—")));
    m_heroChipsLayout->addWidget(createChip(tr("版本：—")));
    m_heroChipsLayout->addWidget(createChip(tr("加载器：—")));
    m_heroChipsLayout->addStretch();
    heroLeft->addWidget(m_heroChipsRow);
    heroLayout->addLayout(heroLeft, 3);

    QVBoxLayout *heroRight = new QVBoxLayout();
    heroRight->setSpacing(12);
    heroRight->setAlignment(Qt::AlignCenter);

    m_ring = new RingProgressWidget(heroCard);
    m_ring->setObjectName(QStringLiteral("tdRing"));
    heroRight->addWidget(m_ring, 0, Qt::AlignCenter);

    QHBoxLayout *statsRow = new QHBoxLayout();
    statsRow->setSpacing(10);
    m_statSpeed = createStatTile(tr("下载速度"), QStringLiteral("tdStatSpeed"));
    m_statSize  = createStatTile(tr("已下载"), QStringLiteral("tdStatSize"));
    m_statEta   = createStatTile(tr("预计剩余"), QStringLiteral("tdStatEta"));
    m_statThreads = createStatTile(tr("当前阶段"), QStringLiteral("tdStatStage"));
    statsRow->addWidget(m_statSpeed->parentWidget());
    statsRow->addWidget(m_statSize->parentWidget());
    statsRow->addWidget(m_statEta->parentWidget());
    statsRow->addWidget(m_statThreads->parentWidget());
    heroRight->addLayout(statsRow);
    heroLayout->addLayout(heroRight, 4);

    contentLayout->addWidget(heroCard);

    /* --- 进度卡 --- */
    QWidget *stepperWidget = new QWidget(content);
    QVBoxLayout *stepperLayout = new QVBoxLayout(stepperWidget);
    stepperLayout->setContentsMargins(0, 0, 0, 0);
    stepperLayout->setSpacing(8);

    m_stageChip = new QLabel(QString(), stepperWidget);
    m_stageChip->setObjectName(QStringLiteral("tdStageChip"));
    m_stageChip->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    stepperLayout->addWidget(m_stageChip);

    m_stepper = new StageStepperWidget(stepperWidget);
    m_stepper->setObjectName(QStringLiteral("tdStepper"));
    m_stepper->setStages(QStringList()
                         << tr("获取版本清单") << tr("获取版本详情") << tr("下载客户端")
                         << tr("下载依赖库") << tr("下载资源文件") << tr("安装加载器") << tr("安装完成"));
    stepperLayout->addWidget(m_stepper);

    QWidget *bigProgressWidget = new QWidget(stepperWidget);
    QVBoxLayout *bigProgressLayout = new QVBoxLayout(bigProgressWidget);
    bigProgressLayout->setContentsMargins(0, 10, 0, 0);
    bigProgressLayout->setSpacing(8);

    QHBoxLayout *bigPctRow = new QHBoxLayout();
    m_bigPctLabel = new QLabel(QStringLiteral("0%"), bigProgressWidget);
    m_bigPctLabel->setObjectName(QStringLiteral("tdBigPct"));
    bigPctRow->addWidget(m_bigPctLabel);
    bigPctRow->addStretch();
    bigProgressLayout->addLayout(bigPctRow);

    m_bigBar = new QProgressBar(bigProgressWidget);
    m_bigBar->setObjectName(QStringLiteral("tdBigBar"));
    m_bigBar->setRange(0, 1000);
    m_bigBar->setValue(0);
    m_bigBar->setTextVisible(false);
    m_bigBar->setFixedHeight(12);
    bigProgressLayout->addWidget(m_bigBar);

    QHBoxLayout *footRow = new QHBoxLayout();
    m_footLeft = new QLabel(QString(), bigProgressWidget);
    m_footLeft->setObjectName(QStringLiteral("tdFootLabel"));
    footRow->addWidget(m_footLeft);
    footRow->addStretch();
    m_footRight = new QLabel(QString(), bigProgressWidget);
    m_footRight->setObjectName(QStringLiteral("tdFootMono"));
    footRow->addWidget(m_footRight);
    bigProgressLayout->addLayout(footRow);

    stepperLayout->addWidget(bigProgressWidget);

    contentLayout->addWidget(createCard(stepperWidget, tr("整体进度"), tr("任务由多个阶段组成，当前自动进行中")));

    /* --- 双栏：文件清单 + 速度曲线 --- */
    QHBoxLayout *twoCol = new QHBoxLayout();
    twoCol->setSpacing(18);

    m_fileListContainer = new QWidget(content);
    m_fileListContainer->setObjectName(QStringLiteral("tdFileList"));
    m_fileListLayout = new QVBoxLayout(m_fileListContainer);
    m_fileListLayout->setContentsMargins(0, 0, 0, 0);
    m_fileListLayout->setSpacing(2);

    QWidget *fileCardContent = new QWidget(content);
    QVBoxLayout *fileCardLayout = new QVBoxLayout(fileCardContent);
    fileCardLayout->setContentsMargins(0, 0, 0, 0);
    fileCardLayout->setSpacing(6);

    m_fileCountChip = new QLabel(tr("0 / 0 完成"), fileCardContent);
    m_fileCountChip->setObjectName(QStringLiteral("tdStageChip"));
    fileCardLayout->addWidget(m_fileCountChip, 0, Qt::AlignLeft);
    fileCardLayout->addWidget(m_fileListContainer);

    QWidget *filesCard = createCard(fileCardContent, tr("文件清单"), tr("每个文件独立线程下载"));
    filesCard->setMinimumWidth(360);
    twoCol->addWidget(filesCard, 3);

    QWidget *chartContent = new QWidget(content);
    QVBoxLayout *chartLayout = new QVBoxLayout(chartContent);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(8);

    m_speedChart = new SpeedChartWidget(chartContent);
    m_speedChart->setObjectName(QStringLiteral("tdSpeedChart"));
    m_speedChart->setMinimumHeight(150);
    chartLayout->addWidget(m_speedChart, 1);

    QWidget *chartCard = createCard(chartContent, tr("实时速度"), tr("过去 60 秒下载速率"));
    chartCard->setMinimumWidth(300);
    twoCol->addWidget(chartCard, 2);

    contentLayout->addLayout(twoCol);

    /* --- 任务信息 --- */
    QWidget *infoContent = new QWidget(content);
    m_infoGrid = new QGridLayout(infoContent);
    m_infoGrid->setContentsMargins(0, 0, 0, 0);
    m_infoGrid->setHorizontalSpacing(28);
    m_infoGrid->setVerticalSpacing(14);

    const QStringList keys = {
        tr("目标实例"), tr("游戏版本"), tr("加载器"), tr("下载来源"),
        tr("文件大小"), tr("任务编号"), tr("保存路径"), tr("创建时间")
    };
    for (int i = 0; i < keys.size(); ++i)
    {
        int col = i / 4;
        int row = i % 4;

        QWidget *item = new QWidget(infoContent);
        QVBoxLayout *itemLayout = new QVBoxLayout(item);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(3);

        QLabel *keyLabel = new QLabel(keys.at(i), item);
        keyLabel->setObjectName(QStringLiteral("tdInfoKey"));
        itemLayout->addWidget(keyLabel);

        QLabel *valueLabel = new QLabel(QStringLiteral("—"), item);
        valueLabel->setObjectName(QStringLiteral("tdInfoVal"));
        valueLabel->setWordWrap(true);
        itemLayout->addWidget(valueLabel);

        m_infoValues[keys.at(i)] = valueLabel;
        m_infoGrid->addWidget(item, col, row);
    }

    contentLayout->addWidget(createCard(infoContent, tr("任务信息"), tr("任务与目标实例的详细信息")));

    /* --- 任务日志 --- */
    m_logView = new QPlainTextEdit(content);
    m_logView->setObjectName(QStringLiteral("tdLog"));
    m_logView->setReadOnly(true);
    m_logView->setFrameShape(QFrame::NoFrame);
    m_logView->setMinimumHeight(160);
    m_logView->setMaximumHeight(240);
    m_logView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_logView->document()->setMaximumBlockCount(600);
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    contentLayout->addWidget(createCard(m_logView, tr("任务日志"), tr("实时输出 · 自动滚动")));

    /* --- 操作栏 --- */
    QWidget *actionContent = new QWidget(content);
    QHBoxLayout *actionLayout = new QHBoxLayout(actionContent);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(10);

    QVBoxLayout *actionInfo = new QVBoxLayout();
    actionInfo->setSpacing(2);
    QLabel *actionTitle = new QLabel(tr("需要暂停或取消这个任务？"), actionContent);
    actionTitle->setObjectName(QStringLiteral("tdCardTitle"));
    actionInfo->addWidget(actionTitle);
    QLabel *actionSub = new QLabel(tr("暂停后可随时继续，取消将删除已下载的临时文件"), actionContent);
    actionSub->setObjectName(QStringLiteral("tdCardSub"));
    actionInfo->addWidget(actionSub);
    actionLayout->addLayout(actionInfo, 1);

    m_restartButton = new QPushButton(tr("打开目录"), actionContent);
    m_restartButton->setObjectName(QStringLiteral("tdActionGhost"));
    m_restartButton->setCursor(Qt::PointingHandCursor);
    connect(m_restartButton, &QPushButton::clicked, this, [this]() {
        if (!m_currentTask.instancePath.isEmpty())
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentTask.instancePath));
        }
    });
    actionLayout->addWidget(m_restartButton);

    m_cancelButton = new QPushButton(tr("取消任务"), actionContent);
    m_cancelButton->setObjectName(QStringLiteral("tdActionWarn"));
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    connect(m_cancelButton, &QPushButton::clicked, this, &TaskDetailPage::onCancelButtonClicked);
    actionLayout->addWidget(m_cancelButton);

    m_pauseButton = new QPushButton(tr("暂停"), actionContent);
    m_pauseButton->setObjectName(QStringLiteral("tdActionPrimary"));
    m_pauseButton->setCursor(Qt::PointingHandCursor);
    connect(m_pauseButton, &QPushButton::clicked, this, &TaskDetailPage::onPauseButtonClicked);
    actionLayout->addWidget(m_pauseButton);

    contentLayout->addWidget(createCard(actionContent, QString(), QString()));

    contentLayout->addStretch();

    scrollArea->setWidget(content);
    mainLayout->addWidget(scrollArea, 1);
}

/* ============================================================
 * 数据接口
 * ============================================================ */
void TaskDetailPage::setTaskId(const QString &taskId)
{
    m_taskId = taskId;
    resetFileList();

    m_currentTask = DownloadTaskManager::instance()->getTaskCopy(taskId);
    m_simulating = true;
    m_animProgress = m_currentTask.progressDouble;
    m_animSpeed = 0.0;
    m_smoothEta = -1;
    m_logCount = 0;
    m_logView->clear();
    m_speedChart->clearSamples();

    appendLog(tr("开始监控任务：%1").arg(taskId));
    if (!m_currentTask.instanceName.isEmpty())
    {
        appendLog(tr("目标实例：%1 · 版本 %2").arg(m_currentTask.instanceName, m_currentTask.mcVersion));
    }

    for (const QString &file : m_currentTask.fileQueue)
    {
        if (!m_fileRows.contains(file))
        {
            upsertFileRow(file, 0, 0);
        }
    }

    m_animationTimer.start();
    updateDisplay();
    updateButtons();
    updateThemeColors();
}

QString TaskDetailPage::stageToString(DownloadStage stage)
{
    switch (stage)
    {
    case DownloadStage::ManifestFetch: return tr("获取版本清单");
    case DownloadStage::VersionJson:   return tr("获取版本详情");
    case DownloadStage::ClientJar:     return tr("下载客户端");
    case DownloadStage::Libraries:     return tr("下载依赖库");
    case DownloadStage::Assets:        return tr("下载资源文件");
    case DownloadStage::LoaderInstall: return tr("安装加载器");
    case DownloadStage::Completed:     return tr("安装完成");
    default:                           return tr("准备中");
    }
}

void TaskDetailPage::setPillState(const QString &state)
{
    m_statusPill->setProperty("state", state);
    m_statusPill->style()->unpolish(m_statusPill);
    m_statusPill->style()->polish(m_statusPill);
    m_statusPill->update();
}

void TaskDetailPage::updateThemeColors()
{
    ThemeManager *theme = ThemeManager::instance();
    QColor accent(theme->currentThemeColor());
    QColor textColor(theme->currentTextColor());

    if (m_ring)
    {
        m_ring->setAccentColor(accent);
        m_ring->setTextColor(textColor);
        m_ring->setCaption(tr("进度"));
    }
    if (m_speedChart)
    {
        m_speedChart->setAccentColor(accent);
        m_speedChart->setTextColor(textColor);
    }
    if (m_stepper)
    {
        m_stepper->setAccentColor(accent);
        m_stepper->setTextColor(textColor);
        QColor muted = textColor;
        muted.setAlpha(140);
        m_stepper->setMutedColor(muted);
    }
}

/* ============================================================
 * 显示更新
 * ============================================================ */
void TaskDetailPage::updateDisplay()
{
    const double target = m_currentTask.progressDouble;
    const double displayed = m_simulating ? m_animProgress : target;

    m_heroTitleLabel->setText(m_currentTask.instanceName.isEmpty()
                                  ? tr("下载任务")
                                  : tr("安装 %1").arg(m_currentTask.instanceName));

    // Hero 元信息
    const QStringList chipTexts = {
        tr("实例：%1").arg(m_currentTask.instanceName.isEmpty()
                               ? tr("未知实例") : m_currentTask.instanceName),
        tr("版本：%1").arg(m_currentTask.mcVersion),
        m_currentTask.loaders.isEmpty()
            ? tr("加载器：无")
            : tr("加载器：%1").arg(m_currentTask.loaders.join(QStringLiteral("、")))
    };
    int chipIdx = 0;
    for (int i = 0; i < m_heroChipsLayout->count(); ++i)
    {
        QLayoutItem *item = m_heroChipsLayout->itemAt(i);
        QLabel *chip = qobject_cast<QLabel *>(item ? item->widget() : nullptr);
        if (chip && chip->objectName() == QStringLiteral("tdChip"))
        {
            if (chipIdx < chipTexts.size())
            {
                chip->setText(chipTexts.at(chipIdx));
                ++chipIdx;
            }
        }
    }

    m_heroSubLabel->setText(tr("正在传输 %1 · 共 %2 个文件")
                                .arg(m_currentTask.currentFile.isEmpty()
                                         ? tr("游戏文件") : m_currentTask.currentFile)
                                .arg(m_currentTask.fileQueue.isEmpty()
                                         ? tr("—") : QString::number(m_currentTask.fileQueue.size())));

    // 环形进度
    m_ring->setProgress(displayed);

    // 大进度条（0~1000）
    m_bigBar->setValue(static_cast<int>(displayed * 10.0));
    m_bigPctLabel->setText(QStringLiteral("%1%").arg(qRound(displayed)));

    // 阶段步骤条
    int stageIndex = static_cast<int>(m_currentTask.stage);
    if (m_currentTask.status == DownloadTaskStatus::Completed
        || m_currentTask.status == DownloadTaskStatus::Cancelled)
    {
        m_stepper->setCurrentStage(6);
    }
    else
    {
        m_stepper->setCurrentStage(qBound(0, stageIndex, 6));
    }
    m_stepper->setProgress(displayed);
    m_stageChip->setText(tr("阶段 %1 / %2 · %3")
                             .arg(stageIndex + 1).arg(m_currentTask.totalStages)
                             .arg(stageToString(m_currentTask.stage)));

    // 指标
    if (m_currentTask.downloadSpeed > 0 && m_currentTask.status == DownloadTaskStatus::Downloading)
    {
        const double mbps = m_currentTask.downloadSpeed / 1024.0 / 1024.0;
        m_animSpeed = mbps;
        m_statSpeed->setText(QStringLiteral("%1 MB/s").arg(mbps, 0, 'f', 1));
    }
    else if (!m_simulating || m_animSpeed <= 0.0)
    {
        m_statSpeed->setText(QStringLiteral("0.0 MB/s"));
    }

    if (m_currentTask.bytesTotal > 0)
    {
        m_statSize->setText(QStringLiteral("%1 / %2")
                                .arg(DownloadUtils::formatSize(m_currentTask.bytesReceived),
                                     DownloadUtils::formatSize(m_currentTask.bytesTotal)));
        m_footLeft->setText(tr("已下载 %1 · 共 %2")
                                .arg(DownloadUtils::formatSize(m_currentTask.bytesReceived),
                                     DownloadUtils::formatSize(m_currentTask.bytesTotal)));
    }
    else
    {
        m_statSize->setText(tr("计算中…"));
    }

    if (m_currentTask.downloadSpeed > 0 && m_currentTask.status == DownloadTaskStatus::Downloading
        && m_currentTask.bytesTotal > 0)
    {
        const qint64 remaining = m_currentTask.bytesTotal - m_currentTask.bytesReceived;
        if (remaining > 0)
        {
            const int rawEta = static_cast<int>(remaining / m_currentTask.downloadSpeed);
            if (m_smoothEta < 0)
            {
                m_smoothEta = rawEta;
            }
            else
            {
                m_smoothEta = static_cast<int>(m_smoothEta + (rawEta - m_smoothEta) * 0.2);
            }
            const int totalSec = m_smoothEta;
            const QString eta = QStringLiteral("%1:%2:%3")
                                    .arg(totalSec / 3600, 2, 10, QLatin1Char('0'))
                                    .arg((totalSec % 3600) / 60, 2, 10, QLatin1Char('0'))
                                    .arg(totalSec % 60, 2, 10, QLatin1Char('0'));
            m_statEta->setText(eta);
        }
        else
        {
            m_statEta->setText(QStringLiteral("00:00:00"));
        }
    }
    else
    {
        m_smoothEta = -1;
        m_statEta->setText(QStringLiteral("--:--:--"));
    }

    m_statThreads->setText(stageToString(m_currentTask.stage));
    m_footRight->setText(m_currentTask.downloadSpeed > 0
                             ? DownloadUtils::formatSpeed(m_currentTask.downloadSpeed)
                             : QStringLiteral("0 B/s"));

    // 任务信息网格
    auto setInfo = [this](const QString &key, const QString &value) {
        if (m_infoValues.contains(key))
        {
            m_infoValues[key]->setText(value);
        }
    };
    setInfo(tr("目标实例"), m_currentTask.instanceName.isEmpty() ? tr("未知实例") : m_currentTask.instanceName);
    setInfo(tr("游戏版本"), m_currentTask.mcVersion.isEmpty() ? tr("—") : m_currentTask.mcVersion);
    setInfo(tr("加载器"), m_currentTask.loaders.isEmpty() ? tr("无") : m_currentTask.loaders.join(QStringLiteral("、")));
    setInfo(tr("下载来源"), tr("下载引擎"));
    setInfo(tr("文件大小"), m_currentTask.bytesTotal > 0
                                ? QStringLiteral("%1 / %2")
                                      .arg(DownloadUtils::formatSize(m_currentTask.bytesReceived),
                                           DownloadUtils::formatSize(m_currentTask.bytesTotal))
                                : tr("计算中…"));
    setInfo(tr("任务编号"), m_taskId);
    setInfo(tr("保存路径"), m_currentTask.instancePath.isEmpty() ? tr("—") : m_currentTask.instancePath);
    setInfo(tr("创建时间"),
            m_currentTask.startTime > 0
                ? QDateTime::fromSecsSinceEpoch(m_currentTask.startTime).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                : tr("—"));

    // 文件计数
    int done = 0;
    int total = m_fileRows.size();
    for (auto it = m_fileRows.constBegin(); it != m_fileRows.constEnd(); ++it)
    {
        const FileRow &row = it.value();
        if (row.total > 0 && row.received >= row.total)
        {
            ++done;
        }
    }
    m_fileCountChip->setText(tr("%1 / %2 完成").arg(done).arg(total));

    updateStatusPill();
    updateButtons();
}

void TaskDetailPage::updateStatusPill()
{
    const DownloadTaskStatus status = m_currentTask.status;
    QString text;
    QString state;
    switch (status)
    {
    case DownloadTaskStatus::Queued:
        text = tr("排队中"); state = QStringLiteral("queued"); break;
    case DownloadTaskStatus::Downloading:
        text = tr("下载中"); state = QStringLiteral("running"); break;
    case DownloadTaskStatus::Paused:
        text = tr("已暂停"); state = QStringLiteral("paused"); break;
    case DownloadTaskStatus::Completed:
        text = tr("已完成"); state = QStringLiteral("done"); break;
    case DownloadTaskStatus::Failed:
        text = tr("失败：%1").arg(m_currentTask.statusMessage); state = QStringLiteral("failed"); break;
    case DownloadTaskStatus::Cancelled:
        text = tr("已取消"); state = QStringLiteral("paused"); break;
    default:
        text = tr("未知状态"); state = QStringLiteral("queued"); break;
    }
    m_statusPill->setText(text);
    setPillState(state);
}

void TaskDetailPage::updateButtons()
{
    const bool isCancellable = DownloadTaskManager::instance()->isTaskCancellable(m_taskId);
    const bool isPausable = DownloadTaskManager::instance()->isTaskPausable(m_taskId);
    const bool isResumable = m_currentTask.status == DownloadTaskStatus::Paused;

    m_cancelButton->setEnabled(isCancellable);
    m_cancelButton->setText(isCancellable ? tr("取消任务") : tr("已结束"));

    if (isResumable)
    {
        m_pauseButton->setText(tr("继续"));
        m_pauseButton->setObjectName(QStringLiteral("tdActionPrimary"));
        m_pauseButton->style()->unpolish(m_pauseButton);
        m_pauseButton->style()->polish(m_pauseButton);
        m_pauseButton->setEnabled(true);
    }
    else if (isPausable)
    {
        m_pauseButton->setText(tr("暂停"));
        m_pauseButton->setObjectName(QStringLiteral("tdActionAccent"));
        m_pauseButton->style()->unpolish(m_pauseButton);
        m_pauseButton->style()->polish(m_pauseButton);
        m_pauseButton->setEnabled(true);
    }
    else
    {
        m_pauseButton->setEnabled(false);
    }
}

void TaskDetailPage::onAnimationTick()
{
    if (!m_simulating)
    {
        return;
    }

    const double target = m_currentTask.progressDouble;
    const double diff = target - m_animProgress;
    if (qAbs(diff) < 0.05)
    {
        m_animProgress = target;
    }
    else
    {
        m_animProgress += diff * 0.25;
    }

    // 平滑速度显示
    if (m_currentTask.status == DownloadTaskStatus::Downloading && m_currentTask.downloadSpeed > 0)
    {
        const double targetMbps = m_currentTask.downloadSpeed / 1024.0 / 1024.0;
        m_animSpeed += (targetMbps - m_animSpeed) * 0.2;
        m_statSpeed->setText(QStringLiteral("%1 MB/s").arg(m_animSpeed, 0, 'f', 1));
    }

    m_ring->setProgress(m_animProgress);
    m_stepper->setProgress(m_animProgress);
    m_bigBar->setValue(static_cast<int>(m_animProgress * 10.0));
    m_bigPctLabel->setText(QStringLiteral("%1%").arg(qRound(m_animProgress)));
}

/* ============================================================
 * 文件清单
 * ============================================================ */
void TaskDetailPage::resetFileList()
{
    while (QLayoutItem *item = m_fileListLayout->takeAt(0))
    {
        if (QWidget *w = item->widget())
        {
            w->deleteLater();
        }
        delete item;
    }
    m_fileRows.clear();
}

void TaskDetailPage::upsertFileRow(const QString &fileName, qint64 received, qint64 total)
{
    if (fileName.isEmpty())
    {
        return;
    }

    FileRow &row = m_fileRows[fileName];
    if (!row.row)
    {
        row.row = new QWidget(m_fileListContainer);
        row.row->setObjectName(QStringLiteral("tdFileRow"));
        row.row->setMinimumHeight(34);

        QHBoxLayout *rowLayout = new QHBoxLayout(row.row);
        rowLayout->setContentsMargins(4, 4, 4, 4);
        rowLayout->setSpacing(10);

        row.nameLabel = new QLabel(fileName, row.row);
        row.nameLabel->setObjectName(QStringLiteral("tdFileName"));
        QFont nameFont = row.nameLabel->font();
        nameFont.setPixelSize(12);
        row.nameLabel->setFont(nameFont);
        rowLayout->addWidget(row.nameLabel, 1);

        row.bar = new QProgressBar(row.row);
        row.bar->setObjectName(QStringLiteral("tdFileBar"));
        row.bar->setRange(0, 1000);
        row.bar->setValue(0);
        row.bar->setTextVisible(false);
        row.bar->setFixedWidth(120);
        row.bar->setFixedHeight(6);
        rowLayout->addWidget(row.bar);

        row.stateLabel = new QLabel(tr("排队中"), row.row);
        row.stateLabel->setObjectName(QStringLiteral("tdFileState"));
        row.stateLabel->setMinimumWidth(48);
        row.stateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(row.stateLabel);

        m_fileListLayout->addWidget(row.row);
    }

    row.received = received;
    row.total = total;

    if (total > 0)
    {
        const qint64 value64 = qBound(static_cast<qint64>(0),
                                      received * 1000 / total,
                                      static_cast<qint64>(1000));
        const int value = static_cast<int>(value64);
        row.bar->setValue(value);
        if (received >= total)
        {
            row.bar->setProperty("done", true);
            row.stateLabel->setText(tr("完成"));
        }
        else
        {
            row.bar->setProperty("done", false);
            row.stateLabel->setText(QStringLiteral("%1%").arg(value / 10));
        }
    }
    else
    {
        row.stateLabel->setText(tr("排队中"));
    }

    row.bar->style()->unpolish(row.bar);
    row.bar->style()->polish(row.bar);
}

/* ============================================================
 * 日志
 * ============================================================ */
void TaskDetailPage::appendLog(const QString &text, const QString &color)
{
    if (!m_logView)
    {
        return;
    }

    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    QTextCursor cursor = m_logView->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat timeFormat;
    timeFormat.setForeground(QColor(QStringLiteral("#64748B")));
    cursor.insertText(QStringLiteral("[%1] ").arg(time), timeFormat);

    QTextCharFormat tagFormat;
    tagFormat.setForeground(QColor(QStringLiteral("#F59E0B")));
    cursor.insertText(QStringLiteral("[Task/INFO]: "), tagFormat);

    QTextCharFormat textFormat;
    textFormat.setForeground(color.isEmpty() ? QColor(QStringLiteral("#CBD5E1")) : QColor(color));
    cursor.insertText(text, textFormat);
    cursor.insertBlock();

    m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->maximum());
    ++m_logCount;
}

/* ============================================================
 * 任务信号槽
 * ============================================================ */
void TaskDetailPage::onTaskProgressUpdated(const QString &taskId, double progress)
{
    Q_UNUSED(progress);
    if (taskId == m_taskId)
    {
        m_currentTask = DownloadTaskManager::instance()->getTaskCopy(taskId);
        updateDisplay();
    }
}

void TaskDetailPage::onTaskStatusChanged(const QString &taskId, DownloadTaskStatus status)
{
    if (taskId == m_taskId)
    {
        m_currentTask = DownloadTaskManager::instance()->getTaskCopy(taskId);
        updateDisplay();
        updateButtons();

        switch (status)
        {
        case DownloadTaskStatus::Downloading:
            appendLog(tr("任务开始下载"));
            break;
        case DownloadTaskStatus::Paused:
            appendLog(tr("任务已暂停"), QStringLiteral("#FBBF24"));
            break;
        case DownloadTaskStatus::Completed:
            appendLog(tr("任务完成"), QStringLiteral("#34D399"));
            break;
        case DownloadTaskStatus::Failed:
            appendLog(tr("任务失败：%1").arg(m_currentTask.statusMessage), QStringLiteral("#F87171"));
            break;
        case DownloadTaskStatus::Cancelled:
            appendLog(tr("任务已取消"), QStringLiteral("#FBBF24"));
            break;
        default:
            break;
        }
    }
}

void TaskDetailPage::onTaskSpeedUpdated(const QString &taskId, qint64 speed)
{
    if (taskId == m_taskId)
    {
        m_currentTask = DownloadTaskManager::instance()->getTaskCopy(taskId);
        const double mbps = speed / 1024.0 / 1024.0;
        if (m_speedChart)
        {
            m_speedChart->addSample(mbps);
            m_speedChart->setPeakHintText(tr("实时 %1").arg(mbps, 0, 'f', 1) + QStringLiteral(" MB/s"));
        }
        updateDisplay();
    }
}

void TaskDetailPage::onTaskStageChanged(const QString &taskId, DownloadStage stage, int stageProgress)
{
    if (taskId == m_taskId)
    {
        m_currentTask = DownloadTaskManager::instance()->getTaskCopy(taskId);
        m_currentTask.stage = stage;
        m_currentTask.stageProgress = stageProgress;
        appendLog(tr("阶段切换：%1 (%2/%3)")
                      .arg(stageToString(stage))
                      .arg(static_cast<int>(stage) + 1)
                      .arg(m_currentTask.totalStages));
        updateDisplay();
    }
}

void TaskDetailPage::onTaskFileProgressUpdated(const QString &taskId, const QString &fileName,
                                               qint64 bytesReceived, qint64 bytesTotal)
{
    if (taskId != m_taskId)
    {
        return;
    }
    upsertFileRow(fileName, bytesReceived, bytesTotal);
}

void TaskDetailPage::onTaskRemoved(const QString &taskId)
{
    if (taskId == m_taskId)
    {
        m_simulating = false;
        m_animationTimer.stop();
        emit backToTaskListRequested();
    }
}

/* ============================================================
 * 按钮处理
 * ============================================================ */
void TaskDetailPage::onBackClicked()
{
    m_simulating = false;
    m_animationTimer.stop();
    emit backToTaskListRequested();
}

void TaskDetailPage::onCancelButtonClicked()
{
    AppMessageBox::StandardButton reply = AppMessageBox::question(
        this,
        tr("确认取消"),
        tr("确定要取消此下载任务吗？\n已下载的文件将被删除。"),
        AppMessageBox::Yes | AppMessageBox::No
    );

    if (reply == AppMessageBox::Yes)
    {
        VersionDownloader::instance()->cancelDownload();
        emit taskCancelled(m_taskId);
        appendLog(tr("正在取消任务…"), QStringLiteral("#FBBF24"));
    }
}

void TaskDetailPage::onPauseButtonClicked()
{
    if (m_currentTask.status == DownloadTaskStatus::Downloading)
    {
        VersionDownloader::instance()->pauseDownload();
    }
    else if (m_currentTask.status == DownloadTaskStatus::Paused)
    {
        VersionDownloader::instance()->resumeDownload();
    }
}
