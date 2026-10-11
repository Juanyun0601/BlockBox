#include "TaskBar.h"

#include <QCursor>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSpacerItem>

#include "utils/VersionDownloader.h"

TaskBar::TaskBar(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_tray(nullptr)
    , m_trayLayout(nullptr)
    , m_scrollArea(nullptr)
    , m_cardsContent(nullptr)
    , m_cardsLayout(nullptr)
    , m_viewAllBtn(nullptr)
    , m_countBadge(nullptr)
    , m_launchTaskCard(nullptr)
    , m_hasLaunchCard(false)
    , m_exitAnim(nullptr)
    , m_exitAnimRunning(false)
{
    initUI();
    initDownloadTaskManager();
    setVisible(false);
}

TaskBar::~TaskBar()
{
}

void TaskBar::initUI()
{
    setObjectName("taskBar");
    // 外层 taskBar 保持透明（QSS），四周留白让内部玻璃托盘悬浮于窗口背景之上
    setFixedHeight(82);

    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(16, 6, 16, 10);
    m_mainLayout->setSpacing(0);

    m_tray = new QWidget(this);
    m_tray->setObjectName("taskBarTray");
    m_mainLayout->addWidget(m_tray);

    m_trayLayout = new QHBoxLayout(m_tray);
    m_trayLayout->setContentsMargins(12, 5, 12, 5);
    m_trayLayout->setSpacing(10);

    // 活跃下载任务计数胶囊
    m_countBadge = new QLabel(m_tray);
    m_countBadge->setObjectName("taskCountBadge");
    m_countBadge->setAlignment(Qt::AlignCenter);
    m_countBadge->setFixedHeight(26);
    m_countBadge->setMinimumWidth(26);
    m_countBadge->hide();
    m_trayLayout->addWidget(m_countBadge, 0, Qt::AlignVCenter);

    // Launch task card (hidden by default)，常驻托盘左侧，不参与滚动
    m_launchTaskCard = new LaunchTaskCard();
    m_launchTaskCard->hide();
    m_launchTaskCard->setFixedHeight(48);
    m_launchTaskCard->setFixedWidth(260);
    connect(m_launchTaskCard, &LaunchTaskCard::showDetailsPageRequested, this, &TaskBar::launchTaskCardClicked);
    m_trayLayout->addWidget(m_launchTaskCard, 0, Qt::AlignVCenter);

    m_scrollArea = new QScrollArea();
    m_scrollArea->setObjectName("taskBarScrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    // 比卡片高 8px：给横向滚动条预留竖向空间，滚动条出现时卡片仍完整可见
    m_scrollArea->setFixedHeight(56);

    m_cardsContent = new QWidget();
    m_cardsContent->setObjectName("taskBarCardsContent");
    m_cardsLayout = new QHBoxLayout(m_cardsContent);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(10);
    m_cardsLayout->addStretch();

    m_scrollArea->setWidget(m_cardsContent);
    m_trayLayout->addWidget(m_scrollArea, 1);

    m_viewAllBtn = new QPushButton(tr("全部任务 ›"));
    m_viewAllBtn->setObjectName("viewAllTaskBtn");
    m_viewAllBtn->setCursor(QCursor(Qt::PointingHandCursor));
    m_viewAllBtn->setFixedHeight(48);
    m_viewAllBtn->setMinimumWidth(96);
    connect(m_viewAllBtn, &QPushButton::clicked, this, &TaskBar::showTaskListPageRequested);
    m_trayLayout->addWidget(m_viewAllBtn, 0, Qt::AlignVCenter);

    updateCountBadge();
}

void TaskBar::updateCountBadge()
{
    const int count = m_downloadTaskCards.size();
    m_countBadge->setText(QString::number(count));
    m_countBadge->setVisible(count > 0);
}

void TaskBar::initDownloadTaskManager()
{
    DownloadTaskManager *manager = DownloadTaskManager::instance();

    connect(manager, &DownloadTaskManager::taskAdded, this, &TaskBar::onTaskAdded);
    connect(manager, &DownloadTaskManager::taskProgressUpdated, this, &TaskBar::onTaskProgressUpdated);
    connect(manager, &DownloadTaskManager::taskSpeedUpdated, this, &TaskBar::onTaskSpeedUpdated);
    connect(manager, &DownloadTaskManager::taskStageChanged, this, &TaskBar::onTaskStageChanged);
    connect(manager, &DownloadTaskManager::taskStatusChanged, this, &TaskBar::onTaskStatusChanged);
    connect(manager, &DownloadTaskManager::taskRemoved, this, &TaskBar::onTaskRemoved);
    connect(manager, &DownloadTaskManager::activeTaskCountChanged, this, &TaskBar::onActiveTaskCountChanged);
}

void TaskBar::setupCardConnections(DownloadTaskCard *card, const QString &taskId)
{
    Q_UNUSED(taskId);
    connect(card, &DownloadTaskCard::cardClicked, this, &TaskBar::onDownloadTaskCardClicked);
    connect(card, &DownloadTaskCard::cancelRequested, this, [](const QString &) {
        VersionDownloader::instance()->cancelDownload();
    });
    connect(card, &DownloadTaskCard::pauseRequested, this, [](const QString &) {
        VersionDownloader::instance()->pauseDownload();
    });
    connect(card, &DownloadTaskCard::resumeRequested, this, [](const QString &) {
        VersionDownloader::instance()->resumeDownload();
    });
}

void TaskBar::updateVisibility()
{
    bool hasDownloadTasks = !m_downloadTaskCards.isEmpty();
    bool shouldShow = m_hasLaunchCard || hasDownloadTasks;
    updateCountBadge();

    if (shouldShow) {
        if (m_exitAnimRunning && m_exitAnim) {
            m_exitAnim->stop();
            m_exitAnimRunning = false;
            setGeometry(m_exitAnimTargetGeo);
        }

        if (!isVisible()) {
            QWidget::show();
            QRect finalGeo = geometry();
            QRect startGeo = finalGeo;
            startGeo.moveTop(finalGeo.top() + height());
            setGeometry(startGeo);

            QPropertyAnimation *anim = new QPropertyAnimation(this, "geometry", this);
            anim->setDuration(300);
            anim->setStartValue(startGeo);
            anim->setEndValue(finalGeo);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    } else {
        if (isVisible() && !m_exitAnimRunning) {
            m_exitAnimTargetGeo = geometry();
            QRect endGeo = m_exitAnimTargetGeo;
            endGeo.moveTop(m_exitAnimTargetGeo.top() + height());

            m_exitAnim = new QPropertyAnimation(this, "geometry", this);
            m_exitAnimRunning = true;
            m_exitAnim->setDuration(300);
            m_exitAnim->setStartValue(m_exitAnimTargetGeo);
            m_exitAnim->setEndValue(endGeo);
            m_exitAnim->setEasingCurve(QEasingCurve::InCubic);
            connect(m_exitAnim, &QPropertyAnimation::finished, this, [this]() {
                m_exitAnimRunning = false;
                m_exitAnim = nullptr;
                hide();
                setGeometry(m_exitAnimTargetGeo);
            });
            m_exitAnim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
}

void TaskBar::showLaunchTaskCard(bool show)
{
    m_hasLaunchCard = show;
    m_launchTaskCard->setVisible(show);
    m_launchTaskCard->setFixedSize(260, 48);
    if (show) {
        if (m_exitAnimRunning && m_exitAnim) {
            m_exitAnim->stop();
            m_exitAnimRunning = false;
            setGeometry(m_exitAnimTargetGeo);
        }
        bool wasHidden = !isVisible();
        if (wasHidden) {
            QWidget::show();
            QRect finalGeo = geometry();
            QRect startGeo = finalGeo;
            startGeo.moveTop(finalGeo.top() + height());
            setGeometry(startGeo);

            QPropertyAnimation *anim = new QPropertyAnimation(this, "geometry", this);
            anim->setDuration(300);
            anim->setStartValue(startGeo);
            anim->setEndValue(finalGeo);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        } else {
            setVisible(true);
        }
    } else {
        updateVisibility();
    }
}

LaunchTaskCard* TaskBar::launchTaskCard() const
{
    return m_launchTaskCard;
}

void TaskBar::updateLaunchTaskStatus(LaunchTaskCard::LaunchStatus status)
{
    if (m_launchTaskCard) {
        m_launchTaskCard->setStatus(status);
    }
}

void TaskBar::updateLaunchTaskProgress(int progress)
{
    if (m_launchTaskCard) {
        m_launchTaskCard->setProgress(progress);
    }
}

void TaskBar::updateLaunchTaskMessage(const QString &message)
{
    if (m_launchTaskCard) {
        m_launchTaskCard->setMessage(message);
    }
}

void TaskBar::addLaunchTaskDetail(const QString &detail)
{
    if (m_launchTaskCard) {
        m_launchTaskCard->addDetail(detail);
    }
}

void TaskBar::onTaskAdded(const QString &taskId)
{
    DownloadTask task = DownloadTaskManager::instance()->getTaskCopy(taskId);

    DownloadTaskCard *card = new DownloadTaskCard(taskId);
    card->updateFromTask(task);

    setupCardConnections(card, taskId);

    m_downloadTaskCards[taskId] = card;
    m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card, 0, Qt::AlignVCenter);
    updateVisibility();

    card->show();
    QRect finalGeo = card->geometry();
    QRect startGeo = finalGeo;
    startGeo.moveTop(finalGeo.top() + 30);
    card->setGeometry(startGeo);

    QPropertyAnimation *anim = new QPropertyAnimation(card, "geometry", this);
    anim->setDuration(250);
    anim->setStartValue(startGeo);
    anim->setEndValue(finalGeo);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void TaskBar::onTaskProgressUpdated(const QString &taskId, double progress)
{
    Q_UNUSED(progress);
    if (m_downloadTaskCards.contains(taskId)) {
        DownloadTask task = DownloadTaskManager::instance()->getTaskCopy(taskId);
        m_downloadTaskCards[taskId]->updateFromTask(task);
    }
}

void TaskBar::onTaskSpeedUpdated(const QString &taskId, qint64 speed)
{
    Q_UNUSED(speed);
    if (m_downloadTaskCards.contains(taskId)) {
        DownloadTask task = DownloadTaskManager::instance()->getTaskCopy(taskId);
        m_downloadTaskCards[taskId]->updateFromTask(task);
    }
}

void TaskBar::onTaskStageChanged(const QString &taskId, DownloadStage stage, int stageProgress)
{
    Q_UNUSED(stage);
    Q_UNUSED(stageProgress);
    if (m_downloadTaskCards.contains(taskId)) {
        DownloadTask task = DownloadTaskManager::instance()->getTaskCopy(taskId);
        m_downloadTaskCards[taskId]->updateFromTask(task);
    }
}

void TaskBar::onTaskStatusChanged(const QString &taskId, DownloadTaskStatus status)
{
    Q_UNUSED(status);
    if (m_downloadTaskCards.contains(taskId)) {
        DownloadTask task = DownloadTaskManager::instance()->getTaskCopy(taskId);
        m_downloadTaskCards[taskId]->updateFromTask(task);

        if (task.status == DownloadTaskStatus::Completed || task.status == DownloadTaskStatus::Cancelled) {
            DownloadTaskCard *card = m_downloadTaskCards[taskId];
            m_downloadTaskCards.remove(taskId);

            QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(card);
            card->setGraphicsEffect(effect);

            QPropertyAnimation *anim = new QPropertyAnimation(effect, "opacity");
            anim->setDuration(400);
            anim->setStartValue(1.0);
            anim->setEndValue(0.0);
            anim->setEasingCurve(QEasingCurve::InCubic);
            // 以 this（TaskBar）为 context：若 TaskBar 先销毁则自动断开，避免悬挂 this。
            // card 可能因其他原因被删除，故 capture 时同时用 QPointer 防护。
            connect(anim, &QPropertyAnimation::finished, this, [this, card]() {
                if (!card) return;
                m_cardsLayout->removeWidget(card);
                card->hide();
                card->deleteLater();
                updateVisibility();
            });
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
}

void TaskBar::onTaskRemoved(const QString &taskId)
{
    if (m_downloadTaskCards.contains(taskId)) {
        DownloadTaskCard *card = m_downloadTaskCards[taskId];
        m_cardsLayout->removeWidget(card);
        card->deleteLater();
        m_downloadTaskCards.remove(taskId);
        updateVisibility();
    }
}

void TaskBar::onActiveTaskCountChanged(int count)
{
    Q_UNUSED(count);
}

void TaskBar::onDownloadTaskCardClicked(const QString &taskId)
{
    emit taskDetailRequested(taskId);
}
