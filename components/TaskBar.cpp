#include "TaskBar.h"

#include <QCursor>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSpacerItem>
#include <QTimer>

#include "utils/VersionDownloader.h"

namespace {
// 悬浮外边距：左右对称，底部略大给柔化阴影留呼吸空间
constexpr int kFloatMarginX = 16;
constexpr int kFloatMarginTop = 6;
constexpr int kFloatMarginBottom = 24;

// 失败卡片停留时长：足够看清红色「失败」与失败原因，又不至于一直挂在任务栏上
constexpr int kFailedCardLingerMs = 10000;
constexpr int kFailedLaunchLingerMs = 12000;
// 鼠标停在卡片上时延后再关，避免用户正读错误信息时卡片突然消失
constexpr int kHoverDeferMs = 1500;
} // namespace

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
    , m_launchDismissTimer(nullptr)
    , m_exitAnim(nullptr)
    , m_exitAnimRunning(false)
{
    // 悬浮叠层：不参与父布局，靠几何定位压在内容上方
    if (parent)
        parent->installEventFilter(this);

    // 启动失败/异常退出的卡片不长期驻留：停留一段时间后自动收起
    m_launchDismissTimer = new QTimer(this);
    m_launchDismissTimer->setSingleShot(true);
    connect(m_launchDismissTimer, &QTimer::timeout, this, &TaskBar::onLaunchCardDismissTimeout);

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
    // 外层 taskBar 保持透明（QSS），四周留白让内部玻璃托盘悬浮于内容之上
    // 总高 = 上边距 + 托盘 66 + 下边距（阴影呼吸空间）
    setFixedHeight(96);

    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(kFloatMarginX, kFloatMarginTop, kFloatMarginX, kFloatMarginBottom);
    m_mainLayout->setSpacing(0);

    m_tray = new QWidget(this);
    m_tray->setObjectName("taskBarTray");
    // 轻柔外阴影：托盘从内容上“浮起来”，避免硬贴在界面底部
    auto *trayShadow = new QGraphicsDropShadowEffect(m_tray);
    trayShadow->setBlurRadius(28);
    trayShadow->setOffset(0, 6);
    trayShadow->setColor(QColor(0, 0, 0, 55));
    m_tray->setGraphicsEffect(trayShadow);
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

void TaskBar::updateFloatGeometry()
{
    QWidget *host = parentWidget();
    if (!host)
        return;
    const int h = height();
    const int w = qMax(0, host->width() - 2 * kFloatMarginX);
    const int x = kFloatMarginX;
    const int y = host->height() - h - kFloatMarginBottom;
    setGeometry(x, y, w, h);
    raise();
}

bool TaskBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        // 父容器尺寸变化时重算悬浮位置；动画进行中跳过，避免与 geometry 动画打架
        if (!m_exitAnimRunning)
            updateFloatGeometry();
    }
    return QWidget::eventFilter(watched, event);
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
            updateFloatGeometry();
        }

        if (!isVisible()) {
            updateFloatGeometry();
            QWidget::show();
            raise();
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
            raise();
        }
    } else {
        if (isVisible() && !m_exitAnimRunning) {
            updateFloatGeometry();
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
                updateFloatGeometry();
            });
            m_exitAnim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
}

void TaskBar::showLaunchTaskCard(bool show)
{
    // 卡片显隐由调用方决定，自动收起定时器随之作废（失败状态会重新排期）
    if (m_launchDismissTimer)
        m_launchDismissTimer->stop();

    m_hasLaunchCard = show;
    m_launchTaskCard->setVisible(show);
    m_launchTaskCard->setFixedSize(260, 48);
    if (show) {
        if (m_exitAnimRunning && m_exitAnim) {
            m_exitAnim->stop();
            m_exitAnimRunning = false;
            updateFloatGeometry();
        }
        bool wasHidden = !isVisible();
        if (wasHidden) {
            updateFloatGeometry();
            QWidget::show();
            raise();
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
            raise();
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

    if (!m_launchDismissTimer || !m_launchTaskCard)
        return;

    if (status == LaunchTaskCard::Failed || status == LaunchTaskCard::Stopped) {
        // 启动失败 / 异常退出：先留着让用户看到，过一会儿自动收起，不长期占住任务栏
        m_launchDismissTimer->start(kFailedLaunchLingerMs);
    } else {
        // 重新启动、运行中、空闲：撤销待执行的收起
        m_launchDismissTimer->stop();
    }
}

void TaskBar::onLaunchCardDismissTimeout()
{
    if (!m_launchTaskCard || !m_hasLaunchCard)
        return;

    // 鼠标停在启动卡片上时延后收起，避免用户正在看失败信息时卡片突然消失
    if (m_launchTaskCard->isVisible() && m_launchTaskCard->underMouse()) {
        m_launchDismissTimer->start(kHoverDeferMs);
        return;
    }

    // 状态可能已被再次切换（例如自动重试），只在仍处于终态时收起
    const LaunchTaskCard::LaunchStatus status = m_launchTaskCard->status();
    if (status == LaunchTaskCard::Failed || status == LaunchTaskCard::Stopped)
        showLaunchTaskCard(false);
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

    // 防御：若卡片创建时任务已经是失败态（如将来支持恢复历史任务），同样安排自动收起
    if (task.status == DownloadTaskStatus::Failed)
        scheduleDownloadCardDismissal(taskId, kFailedCardLingerMs);
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

void TaskBar::fadeOutAndRemoveDownloadCard(const QString &taskId)
{
    if (!m_downloadTaskCards.contains(taskId))
        return;

    cancelDownloadCardDismissal(taskId);
    QPointer<DownloadTaskCard> card = m_downloadTaskCards.take(taskId);

    QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(card);
    card->setGraphicsEffect(effect);

    QPropertyAnimation *anim = new QPropertyAnimation(effect, "opacity");
    anim->setDuration(400);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InCubic);
    // 以 this（TaskBar）为 context：若 TaskBar 先销毁则自动断开，避免悬挂 this。
    // card 也可能已被提前删除，故用 QPointer 防护。
    connect(anim, &QPropertyAnimation::finished, this, [this, card]() {
        if (!card) return;
        m_cardsLayout->removeWidget(card);
        card->hide();
        card->deleteLater();
        updateVisibility();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void TaskBar::scheduleDownloadCardDismissal(const QString &taskId, int delayMs)
{
    cancelDownloadCardDismissal(taskId);

    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delayMs);
    connect(timer, &QTimer::timeout, this, [this, taskId, timer]() {
        // 鼠标停在卡片上说明用户正在看失败原因，先不关，稍后再试
        DownloadTaskCard *card = m_downloadTaskCards.value(taskId, nullptr);
        if (card && card->isVisible() && card->underMouse()) {
            timer->start(kHoverDeferMs);
            return;
        }
        m_cardDismissTimers.remove(taskId);
        timer->deleteLater();
        fadeOutAndRemoveDownloadCard(taskId);
    });

    m_cardDismissTimers.insert(taskId, timer);
    timer->start();
}

void TaskBar::cancelDownloadCardDismissal(const QString &taskId)
{
    QTimer *timer = m_cardDismissTimers.take(taskId);
    if (!timer)
        return;
    timer->stop();
    timer->deleteLater();
}

void TaskBar::onTaskStatusChanged(const QString &taskId, DownloadTaskStatus status)
{
    if (!m_downloadTaskCards.contains(taskId)) {
        // 失败卡片已自动收起，任务又被重新拉起（例如在详情页点重试）：重新挂回任务栏
        if (status == DownloadTaskStatus::Queued ||
            status == DownloadTaskStatus::Downloading ||
            status == DownloadTaskStatus::Paused) {
            onTaskAdded(taskId);
        }
        return;
    }

    DownloadTask task = DownloadTaskManager::instance()->getTaskCopy(taskId);
    m_downloadTaskCards[taskId]->updateFromTask(task);

    switch (task.status) {
    case DownloadTaskStatus::Completed:
    case DownloadTaskStatus::Cancelled:
        // 正常结束/取消：立即淡出移除
        fadeOutAndRemoveDownloadCard(taskId);
        break;
    case DownloadTaskStatus::Failed:
        // 失败不立刻消失（让用户看到红色「失败」和原因），但也不许永远挂着：
        // 停留几秒后自动淡出，避免底部任务栏被清不掉的失败卡片长期占住
        scheduleDownloadCardDismissal(taskId, kFailedCardLingerMs);
        break;
    default:
        // 失败后重试、重新排队、暂停/继续：撤销待执行的自动消失
        cancelDownloadCardDismissal(taskId);
        break;
    }
}

void TaskBar::onTaskRemoved(const QString &taskId)
{
    cancelDownloadCardDismissal(taskId);
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
