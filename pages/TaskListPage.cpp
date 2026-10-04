#include "TaskListPage.h"

#include "components/OutlinedLabel.h"

#include <QApplication>
#include <QFont>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QTimer>

#include "utils/ThemeManager.h"
#include "utils/VersionDownloader.h"

TaskListPage::TaskListPage(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_contentLayout(nullptr)
    , m_scrollArea(nullptr)
    , m_scrollContent(nullptr)
    , m_activeGroup(nullptr)
    , m_activeLayout(nullptr)
    , m_activeCount(nullptr)
    , m_failedGroup(nullptr)
    , m_failedLayout(nullptr)
    , m_failedCount(nullptr)
    , m_completedGroup(nullptr)
    , m_completedLayout(nullptr)
    , m_completedCount(nullptr)
    , m_emptyWidget(nullptr)
    , m_launchStatus(LaunchTaskCard::Idle)
    , m_launchProgress(0)
    , m_hasLaunchTask(false)
    , m_launchCardWidget(nullptr)
    , m_launchStatusLabel(nullptr)
    , m_launchProgressLabel(nullptr)
{
    initUI();

    DownloadTaskManager *manager = DownloadTaskManager::instance();
    connect(manager, &DownloadTaskManager::taskAdded, this, &TaskListPage::onTaskAdded);
    connect(manager, &DownloadTaskManager::taskProgressUpdated, this, &TaskListPage::onTaskProgressUpdated);
    connect(manager, &DownloadTaskManager::taskStatusChanged, this, &TaskListPage::onTaskStatusChanged);
    connect(manager, &DownloadTaskManager::taskRemoved, this, &TaskListPage::onTaskRemoved);

    loadTasks();
}

TaskListPage::~TaskListPage()
{
}

void TaskListPage::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(20, 20, 20, 20);
    m_mainLayout->setSpacing(16);

    OutlinedLabel *titleLabel = new OutlinedLabel(tr("任务列表"));
    titleLabel->setObjectName("pageTitleLabel");
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(titleLabel);

    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_scrollContent = new QWidget();
    m_contentLayout = new QVBoxLayout(m_scrollContent);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(8);
    m_contentLayout->setAlignment(Qt::AlignTop);

    m_emptyWidget = new QWidget();
    QVBoxLayout *emptyLayout = new QVBoxLayout(m_emptyWidget);
    emptyLayout->setContentsMargins(0, 40, 0, 40);
    emptyLayout->setAlignment(Qt::AlignCenter);
    QLabel *emptyIcon = new QLabel(tr("暂无任务"));
    QFont emptyFont = emptyIcon->font();
    emptyFont.setPointSize(24);
    emptyFont.setBold(true);
    emptyIcon->setFont(emptyFont);
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);
    QLabel *emptyText = new QLabel(tr("开始下载或启动游戏，任务将在此处显示"));
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyText);
    m_contentLayout->addWidget(m_emptyWidget);

    m_launchCardWidget = new QWidget();
    m_launchCardWidget->setObjectName("launchCardInList");
    m_launchCardWidget->setFixedHeight(50);
    m_launchCardWidget->setVisible(false);
    QHBoxLayout *launchLayout = new QHBoxLayout(m_launchCardWidget);
    launchLayout->setContentsMargins(12, 8, 12, 8);
    m_launchStatusLabel = new QLabel();
    QFont lf = m_launchStatusLabel->font();
    lf.setPointSize(10);
    lf.setBold(true);
    m_launchStatusLabel->setFont(lf);
    launchLayout->addWidget(m_launchStatusLabel, 1);
    m_launchProgressLabel = new QLabel();
    launchLayout->addWidget(m_launchProgressLabel);
    m_contentLayout->addWidget(m_launchCardWidget);

    m_activeGroup = createGroupSection(tr("进行中"), m_activeCount, m_activeLayout);
    m_activeGroup->setVisible(false);
    m_contentLayout->addWidget(m_activeGroup);

    m_failedGroup = createGroupSection(tr("已失败"), m_failedCount, m_failedLayout);
    m_failedGroup->setVisible(false);
    m_contentLayout->addWidget(m_failedGroup);

    m_completedGroup = createGroupSection(tr("已完成"), m_completedCount, m_completedLayout);
    m_completedGroup->setVisible(false);
    m_contentLayout->addWidget(m_completedGroup);

    m_contentLayout->addStretch();
    m_scrollArea->setWidget(m_scrollContent);
    m_mainLayout->addWidget(m_scrollArea);
}

QWidget* TaskListPage::createGroupSection(const QString &title, QLabel *&countLabel, QVBoxLayout *&contentLayout)
{
    QWidget *group = new QWidget();
    QVBoxLayout *groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(8);

    QWidget *header = new QWidget();
    QHBoxLayout *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 6, 8, 6);

    QLabel *titleLbl = new QLabel(title);
    QFont f = titleLbl->font();
    f.setPointSize(12);
    f.setBold(true);
    titleLbl->setFont(f);
    headerLayout->addWidget(titleLbl, 1);

    countLabel = new QLabel("0");
    QFont cf = countLabel->font();
    cf.setPointSize(11);
    cf.setBold(true);
    countLabel->setFont(cf);
    headerLayout->addWidget(countLabel);

    groupLayout->addWidget(header);

    QWidget *content = new QWidget();
    contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 0, 0, 0);
    contentLayout->setSpacing(8);
    groupLayout->addWidget(content);

    return group;
}

void TaskListPage::loadTasks()
{
    QList<DownloadTask> tasks = DownloadTaskManager::instance()->getAllTasks();
    for (const DownloadTask &task : tasks) {
        DownloadTaskCard *card = new DownloadTaskCard(task.taskId);
        card->updateFromTask(task);
        connect(card, &DownloadTaskCard::cardClicked, this, &TaskListPage::taskDetailRequested);
        connect(card, &DownloadTaskCard::cancelRequested, this, [](const QString &) {
            VersionDownloader::instance()->cancelDownload();
        });
        connect(card, &DownloadTaskCard::pauseRequested, this, [](const QString &) {
            VersionDownloader::instance()->pauseDownload();
        });
        connect(card, &DownloadTaskCard::resumeRequested, this, [](const QString &) {
            VersionDownloader::instance()->resumeDownload();
        });
        m_taskCards[task.taskId] = card;
    }
    rebuildList();
}

void TaskListPage::rebuildList()
{
    while (m_activeLayout && m_activeLayout->count() > 0) {
        QLayoutItem *item = m_activeLayout->takeAt(0);
        delete item;
    }
    while (m_failedLayout && m_failedLayout->count() > 0) {
        QLayoutItem *item = m_failedLayout->takeAt(0);
        delete item;
    }
    while (m_completedLayout && m_completedLayout->count() > 0) {
        QLayoutItem *item = m_completedLayout->takeAt(0);
        delete item;
    }

    QList<DownloadTask> tasks = DownloadTaskManager::instance()->getAllTasks();

    int activeCount = 0;
    int failedCount = 0;
    int completedCount = 0;

    for (const DownloadTask &task : tasks) {
        if (!m_taskCards.contains(task.taskId)) continue;
        DownloadTaskCard *card = m_taskCards[task.taskId];

        switch (task.status) {
        case DownloadTaskStatus::Queued:
        case DownloadTaskStatus::Downloading:
        case DownloadTaskStatus::Paused:
            m_activeLayout->addWidget(card);
            activeCount++;
            break;
        case DownloadTaskStatus::Failed:
            m_failedLayout->addWidget(card);
            failedCount++;
            break;
        case DownloadTaskStatus::Completed:
        case DownloadTaskStatus::Cancelled:
            m_completedLayout->addWidget(card);
            completedCount++;
            break;
        default:
            break;
        }
    }

    m_activeGroup->setVisible(activeCount > 0);
    m_failedGroup->setVisible(failedCount > 0);
    m_completedGroup->setVisible(completedCount > 0);
    m_emptyWidget->setVisible(activeCount == 0 && failedCount == 0 && completedCount == 0);

    m_activeCount->setText(QString::number(activeCount));
    m_failedCount->setText(QString::number(failedCount));
    m_completedCount->setText(QString::number(completedCount));
}

void TaskListPage::updateGroupCounts()
{
    int active = m_activeLayout ? m_activeLayout->count() : 0;
    int failed = m_failedLayout ? m_failedLayout->count() : 0;
    int completed = m_completedLayout ? m_completedLayout->count() : 0;
    m_activeCount->setText(QString::number(active));
    m_failedCount->setText(QString::number(failed));
    m_completedCount->setText(QString::number(completed));
    m_activeGroup->setVisible(active > 0);
    m_failedGroup->setVisible(failed > 0);
    m_completedGroup->setVisible(completed > 0);
    m_emptyWidget->setVisible(active == 0 && failed == 0 && completed == 0);
}

void TaskListPage::setLaunchTaskInfo(const QString &name, LaunchTaskCard::LaunchStatus status, int progress, const QString &message)
{
    m_launchName = name;
    m_launchStatus = status;
    m_launchProgress = progress;
    m_launchMessage = message;
    m_hasLaunchTask = (status != LaunchTaskCard::Idle);
    updateLaunchDisplay();
}

void TaskListPage::updateLaunchDisplay()
{
    if (!m_hasLaunchTask) {
        m_launchCardWidget->setVisible(false);
        rebuildList();
        return;
    }

    QString statusText;
    switch (m_launchStatus) {
    case LaunchTaskCard::Launching:
        statusText = tr("启动游戏 - %1").arg(m_launchName);
        break;
    case LaunchTaskCard::Running:
        statusText = tr("游戏运行中 - %1").arg(m_launchName);
        break;
    case LaunchTaskCard::Failed:
        statusText = tr("启动失败 - %1").arg(m_launchName);
        break;
    case LaunchTaskCard::Stopped:
        statusText = tr("游戏已停止 - %1").arg(m_launchName);
        break;
    default:
        statusText = m_launchName;
        break;
    }
    if (!m_launchMessage.isEmpty()) {
        statusText += " (" + m_launchMessage + ")";
    }

    m_launchStatusLabel->setText(statusText);
    m_launchProgressLabel->setText(QString("%1%").arg(m_launchProgress));
    m_launchCardWidget->setVisible(true);
    rebuildList();
}

void TaskListPage::onTaskAdded(const QString &taskId)
{
    DownloadTask task = DownloadTaskManager::instance()->getTaskCopy(taskId);

    DownloadTaskCard *card = new DownloadTaskCard(taskId);
    card->updateFromTask(task);
    connect(card, &DownloadTaskCard::cardClicked, this, &TaskListPage::taskDetailRequested);
    connect(card, &DownloadTaskCard::cancelRequested, this, [](const QString &) {
        VersionDownloader::instance()->cancelDownload();
    });
    connect(card, &DownloadTaskCard::pauseRequested, this, [](const QString &) {
        VersionDownloader::instance()->pauseDownload();
    });
    connect(card, &DownloadTaskCard::resumeRequested, this, [](const QString &) {
        VersionDownloader::instance()->resumeDownload();
    });
    m_taskCards[taskId] = card;

    rebuildList();

    // Slide-up animation for the newly added card
    // 避免 QApplication::processEvents() 嵌套事件处理导致 card 在动画期间被
    // deleteLater() 后出现悬空指针；延迟到布局稳定后再取几何启动动画。
    QTimer::singleShot(0, this, [this, taskId]() {
        if (!m_taskCards.contains(taskId))
            return;
        DownloadTaskCard *card = m_taskCards.value(taskId);
        if (!card)
            return;
        card->show();
        QRect finalGeo = card->geometry();
        if (finalGeo.isValid() && finalGeo.height() > 0) {
            QRect startGeo = finalGeo;
            startGeo.moveTop(finalGeo.top() + 40);
            card->setGeometry(startGeo);

            QPropertyAnimation *anim = new QPropertyAnimation(card, "geometry", this);
            anim->setDuration(300);
            anim->setStartValue(startGeo);
            anim->setEndValue(finalGeo);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    });
}

void TaskListPage::onTaskProgressUpdated(const QString &taskId, double progress)
{
    Q_UNUSED(progress);
    if (m_taskCards.contains(taskId)) {
        DownloadTask task = DownloadTaskManager::instance()->getTaskCopy(taskId);
        m_taskCards[taskId]->updateFromTask(task);
    }
}

void TaskListPage::onTaskStatusChanged(const QString &taskId, DownloadTaskStatus status)
{
    Q_UNUSED(status);

    if (m_taskCards.contains(taskId)) {
        DownloadTask task = DownloadTaskManager::instance()->getTaskCopy(taskId);
        m_taskCards[taskId]->updateFromTask(task);

        if (task.status == DownloadTaskStatus::Completed || task.status == DownloadTaskStatus::Cancelled) {
            DownloadTaskCard *card = m_taskCards[taskId];
            m_taskCards.remove(taskId);

            QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(card);
            card->setGraphicsEffect(effect);

            QPropertyAnimation *anim = new QPropertyAnimation(effect, "opacity");
            anim->setDuration(400);
            anim->setStartValue(1.0);
            anim->setEndValue(0.0);
            anim->setEasingCurve(QEasingCurve::InCubic);
            connect(anim, &QPropertyAnimation::finished, card, [card]() {
                card->hide();
                card->deleteLater();
            });
            anim->start(QAbstractAnimation::DeleteWhenStopped);

            rebuildList();
            return;
        }
    }
    rebuildList();
}

void TaskListPage::onTaskRemoved(const QString &taskId)
{
    if (m_taskCards.contains(taskId)) {
        DownloadTaskCard *card = m_taskCards[taskId];
        m_taskCards.remove(taskId);
        card->deleteLater();
    }
    rebuildList();
}
