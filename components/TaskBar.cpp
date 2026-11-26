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
#include <utility>

#include "utils/ResourceManager.h"
#include "utils/ThemeManager.h"
#include "utils/VersionDownloader.h"

namespace {
// 悬浮外边距：左右对称，底部略大给柔化阴影留呼吸空间
constexpr int kFloatMarginX = 16;
constexpr int kFloatMarginTop = 6;
constexpr int kFloatMarginBottom = 24;

// 玻璃托盘高度（滚动区 56 + 上下内边距 5+5）
constexpr int kTrayHeight = 66;
// 托盘上方的收起/展开按钮行：高 24、与托盘间隔 6
constexpr int kToggleRowHeight = 24;
constexpr int kToggleGap = 6;
// 任务栏总高 = 上边距 + 按钮行 + 间隔 + 托盘 + 下边距
constexpr int kTaskBarHeight =
    kFloatMarginTop + kToggleRowHeight + kToggleGap + kTrayHeight + kFloatMarginBottom;
// 收起后只在窗口下沿露出的高度（正好是按钮行 + 一点点呼吸空间）
constexpr int kCollapsedVisibleHeight = kFloatMarginTop + kToggleRowHeight + 4;

// 失败卡片停留时长：足够看清红色「失败」与失败原因，又不至于一直挂在任务栏上
constexpr int kFailedCardLingerMs = 10000;
constexpr int kFailedLaunchLingerMs = 12000;
// 鼠标停在卡片上时延后再关，避免用户正读错误信息时卡片突然消失
constexpr int kHoverDeferMs = 1500;

// 收起 / 展开滑动时长
constexpr int kCollapseDurationMs = 240;
constexpr int kExpandDurationMs = 280;
} // namespace

TaskBar::TaskBar(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_toggleRow(nullptr)
    , m_toggleBtn(nullptr)
    , m_tray(nullptr)
    , m_trayLayout(nullptr)
    , m_launchCardsLayout(nullptr)
    , m_scrollArea(nullptr)
    , m_cardsContent(nullptr)
    , m_cardsLayout(nullptr)
    , m_viewAllBtn(nullptr)
    , m_countBadge(nullptr)
    , m_slideAnim(nullptr)
    , m_slideAnimRunning(false)
    , m_collapsed(false)
    , m_exitAnim(nullptr)
    , m_exitAnimRunning(false)
{
    // 悬浮叠层：不参与父布局，靠几何定位压在内容上方
    if (parent)
        parent->installEventFilter(this);

    initUI();
    initDownloadTaskManager();

    // 主题切换后重新按主题取色刷新两个箭头图标
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](ThemeManager::ThemeType) { updateToggleButton(); });

    setVisible(false);
}

TaskBar::~TaskBar()
{
}

void TaskBar::initUI()
{
    setObjectName("taskBar");
    // 外层 taskBar 保持透明（QSS），四周留白让内部玻璃托盘悬浮于内容之上
    // 总高 = 上边距 + 收起按钮行 + 间隔 + 托盘 66 + 下边距（阴影呼吸空间）
    setFixedHeight(kTaskBarHeight);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(kFloatMarginX, kFloatMarginTop, kFloatMarginX,
                                     kFloatMarginBottom);
    m_mainLayout->setSpacing(kToggleGap);

    // 托盘上方的收起按钮行：水平居中，收起后这一行就是窗口下沿仅剩的「展开」按钮。
    // 固定高度 + 顶部对齐：收起时托盘被隐藏，多出来的空间也不会把按钮顶到看不见的位置。
    m_toggleRow = new QWidget(this);
    m_toggleRow->setObjectName("taskBarToggleRow");
    m_toggleRow->setFixedHeight(kToggleRowHeight);
    auto *toggleRowLayout = new QHBoxLayout(m_toggleRow);
    toggleRowLayout->setContentsMargins(0, 0, 0, 0);
    toggleRowLayout->setSpacing(0);
    toggleRowLayout->addStretch(1);

    m_toggleBtn = new QPushButton(m_toggleRow);
    m_toggleBtn->setObjectName("taskBarToggleBtn");
    m_toggleBtn->setFixedHeight(kToggleRowHeight);
    m_toggleBtn->setMinimumWidth(84);
    m_toggleBtn->setCursor(QCursor(Qt::PointingHandCursor));
    connect(m_toggleBtn, &QPushButton::clicked, this, &TaskBar::onToggleCollapsed);
    // 左右各一条弹簧：按钮始终落在任务栏顶部正中间
    toggleRowLayout->addWidget(m_toggleBtn, 0);
    toggleRowLayout->addStretch(1);
    m_mainLayout->addWidget(m_toggleRow, 0, Qt::AlignTop);

    m_tray = new QWidget(this);
    m_tray->setObjectName("taskBarTray");
    // 轻柔外阴影：托盘从内容上“浮起来”，避免硬贴在界面底部
    auto *trayShadow = new QGraphicsDropShadowEffect(m_tray);
    trayShadow->setBlurRadius(28);
    trayShadow->setOffset(0, 6);
    trayShadow->setColor(QColor(0, 0, 0, 55));
    m_tray->setGraphicsEffect(trayShadow);
    m_mainLayout->addWidget(m_tray, 1);

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

    // 启动卡片容器（多实例并行启动，按实例路径动态创建/销毁），
    // 常驻托盘左侧、位于滚动区之前，不参与滚动
    m_launchCardsLayout = new QHBoxLayout();
    m_launchCardsLayout->setContentsMargins(0, 0, 0, 0);
    m_launchCardsLayout->setSpacing(10);
    m_trayLayout->addLayout(m_launchCardsLayout, 0);

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
    updateToggleButton();
}

QRect TaskBar::expandedGeometry() const
{
    QWidget *host = parentWidget();
    if (!host)
        return geometry();
    const int w = qMax(0, host->width() - 2 * kFloatMarginX);
    const int y = host->height() - height() - kFloatMarginBottom;
    return QRect(kFloatMarginX, y, w, height());
}

int TaskBar::collapsedTop() const
{
    QWidget *host = parentWidget();
    if (!host)
        return y();
    return host->height() - kCollapsedVisibleHeight;
}

QRect TaskBar::collapsedGeometry() const
{
    QWidget *host = parentWidget();
    if (!host)
        return geometry();
    // 收起后整条只占一枚小胶囊的宽度，水平居中贴在窗口下沿：
    // 与展开态按钮的正中位置对齐，收起/展开过程中按钮横向不动，只是上下滑动。
    const int btnW = m_toggleBtn ? m_toggleBtn->sizeHint().width() : 96;
    const int maxW = qMax(0, host->width() - 2 * kFloatMarginX);
    const int w = qMin(maxW, 2 * kFloatMarginX + btnW + 4);
    return QRect((host->width() - w) / 2, collapsedTop(), w, height());
}

void TaskBar::applyFloatGeometry()
{
    if (!parentWidget())
        return;
    if (m_collapsed) {
        // 收起态：托盘藏起来（也就不会再吃鼠标事件），整条收成只剩展开按钮的小胶囊
        m_tray->hide();
        setGeometry(collapsedGeometry());
    } else {
        // 展开态：托盘恢复显示，整条贴底居中
        m_tray->show();
        setGeometry(expandedGeometry());
    }
}

void TaskBar::updateFloatGeometry()
{
    applyFloatGeometry();
    raise();
}

void TaskBar::setCollapsed(bool collapsed, bool animate)
{
    if (m_collapsed == collapsed)
        return;

    if (m_exitAnimRunning) {
        if (m_exitAnim)
            m_exitAnim->stop();
        m_exitAnim = nullptr;
        m_exitAnimRunning = false;
    }

    m_collapsed = collapsed;
    updateToggleButton();

    if (!parentWidget() || !isVisible() || !animate) {
        applyFloatGeometry();
        emit collapsedChanged(m_collapsed);
        return;
    }

    if (m_slideAnim) {
        m_slideAnim->stop();
        m_slideAnim = nullptr;
        m_slideAnimRunning = false;
    }

    const QRect fullRect = expandedGeometry();

    if (m_collapsed) {
        // 收起：整条（含托盘）先滑到窗口下沿，托盘滑出视野后再收成小胶囊
        setGeometry(fullRect);
        QRect end = fullRect;
        end.moveTop(collapsedTop());

        m_slideAnimRunning = true;
        auto *anim = new QPropertyAnimation(this, "geometry", this);
        m_slideAnim = anim;
        anim->setDuration(kCollapseDurationMs);
        anim->setStartValue(fullRect);
        anim->setEndValue(end);
        anim->setEasingCurve(QEasingCurve::InCubic);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            m_slideAnimRunning = false;
            m_slideAnim = nullptr;
            applyFloatGeometry();
            updateVisibility();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        // 展开：先在窗口下沿恢复整条宽度与托盘（按钮横向、纵向都不跳），
        // 再把整条向上滑回原位
        QRect interim(fullRect.x(), collapsedTop(), fullRect.width(), fullRect.height());
        setGeometry(interim);
        m_tray->show();

        m_slideAnimRunning = true;
        auto *anim = new QPropertyAnimation(this, "geometry", this);
        m_slideAnim = anim;
        anim->setDuration(kExpandDurationMs);
        anim->setStartValue(interim);
        anim->setEndValue(fullRect);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            m_slideAnimRunning = false;
            m_slideAnim = nullptr;
            applyFloatGeometry();
            updateVisibility();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    emit collapsedChanged(m_collapsed);
}

void TaskBar::onToggleCollapsed()
{
    setCollapsed(!m_collapsed);
}

int TaskBar::visibleTaskCount() const
{
    int count = m_downloadTaskCards.size();
    for (LaunchTaskCard *card : std::as_const(m_launchCards)) {
        // 用 isHidden 而不是 isVisible：收起时托盘被藏起来，但卡片逻辑上仍是显示中的
        if (card && !card->isHidden())
            ++count;
    }
    return count;
}

void TaskBar::updateToggleButton()
{
    if (!m_toggleBtn)
        return;

    ResourceManager *res = ResourceManager::instance();
    const QSize iconSize(12, 12);
    if (m_collapsed) {
        // 收起后按钮是唯一露在外面的一枚小胶囊：顺手把任务数带上，免得看不到进度
        const int tasks = visibleTaskCount();
        m_toggleBtn->setText(tasks > 0 ? tr("展开 · %1").arg(tasks) : tr("展开"));
        m_toggleBtn->setToolTip(tr("展开任务栏"));
        m_toggleBtn->setIcon(res->loadThemedIcon(QStringLiteral(":/Images/Icons/chevron_up.svg")));
    } else {
        m_toggleBtn->setText(tr("收起"));
        m_toggleBtn->setToolTip(tr("收起任务栏，只留展开按钮"));
        m_toggleBtn->setIcon(res->loadThemedIcon(QStringLiteral(":/Images/Icons/chevron_down.svg")));
    }
    m_toggleBtn->setIconSize(iconSize);
    m_toggleBtn->setCursor(QCursor(Qt::PointingHandCursor));
}

bool TaskBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        // 父容器尺寸变化时重算悬浮位置；动画进行中跳过，避免与 geometry 动画打架
        if (!m_exitAnimRunning && !m_slideAnimRunning)
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
    bool shouldShow = hasVisibleLaunchCards() || hasDownloadTasks;
    updateCountBadge();
    updateToggleButton();

    if (shouldShow) {
        if (m_exitAnimRunning && m_exitAnim) {
            m_exitAnim->stop();
            m_exitAnim = nullptr;
            m_exitAnimRunning = false;
            applyFloatGeometry();
        }

        if (!isVisible()) {
            applyFloatGeometry();
            QWidget::show();
            raise();
            QRect finalGeo = geometry();
            QRect startGeo = finalGeo;
            // 收起态只露出展开按钮，所以从窗口下沿下方一点点滑入即可
            startGeo.moveTop(finalGeo.top() + (m_collapsed ? kCollapsedVisibleHeight : height()));
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
        if (isVisible() && !m_exitAnimRunning && !m_slideAnimRunning) {
            applyFloatGeometry();
            m_exitAnimTargetGeo = geometry();
            QRect endGeo = m_exitAnimTargetGeo;
            endGeo.moveTop(m_exitAnimTargetGeo.top()
                           + (m_collapsed ? kCollapsedVisibleHeight : height()));

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
                // 没有任务时收起状态没有意义：复位的展开态，下次出现就是完整任务栏
                if (m_collapsed) {
                    m_collapsed = false;
                    updateToggleButton();
                    emit collapsedChanged(false);
                }
                updateFloatGeometry();
            });
            m_exitAnim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
}

LaunchTaskCard* TaskBar::ensureLaunchCard(const QString &instancePath, const QString &title)
{
    if (LaunchTaskCard *existing = m_launchCards.value(instancePath, nullptr))
        return existing;

    auto *card = new LaunchTaskCard();
    card->setName(title);
    card->hide();
    card->setFixedHeight(48);
    card->setFixedWidth(260);
    const QString path = instancePath;
    connect(card, &LaunchTaskCard::showDetailsPageRequested, this, [this, path]() {
        emit launchTaskCardClicked(path);
    });
    m_launchCards.insert(instancePath, card);
    m_launchCardsLayout->addWidget(card, 0, Qt::AlignVCenter);
    return card;
}

LaunchTaskCard* TaskBar::launchCard(const QString &instancePath) const
{
    return m_launchCards.value(instancePath, nullptr);
}

void TaskBar::showLaunchCard(const QString &instancePath, bool show)
{
    LaunchTaskCard *card = m_launchCards.value(instancePath, nullptr);
    if (!card)
        return;

    // 卡片显隐由调用方决定，自动收起定时器随之作废（失败状态会重新排期）
    if (QTimer *dismiss = m_launchDismissTimers.value(instancePath, nullptr)) {
        dismiss->stop();
        if (!show) {
            dismiss->deleteLater();
            m_launchDismissTimers.remove(instancePath);
        }
    }

    card->setVisible(show);
    card->setFixedSize(260, 48);
    // 显隐、滑入动画、收起态都在 updateVisibility 里统一处理
    updateVisibility();
}

void TaskBar::updateLaunchCardStatus(const QString &instancePath, LaunchTaskCard::LaunchStatus status)
{
    LaunchTaskCard *card = m_launchCards.value(instancePath, nullptr);
    if (!card)
        return;
    card->setStatus(status);

    QTimer *dismiss = m_launchDismissTimers.value(instancePath, nullptr);
    if (status == LaunchTaskCard::Failed || status == LaunchTaskCard::Stopped) {
        // 启动失败 / 异常退出：先留着让用户看到，过一会儿自动收起，不长期占住任务栏
        if (!dismiss) {
            dismiss = new QTimer(this);
            dismiss->setSingleShot(true);
            const QString path = instancePath;
            connect(dismiss, &QTimer::timeout, this, [this, path]() {
                onLaunchCardDismissTimeout(path);
            });
            m_launchDismissTimers.insert(instancePath, dismiss);
        }
        dismiss->start(kFailedLaunchLingerMs);
    } else if (dismiss) {
        // 重新启动、运行中、空闲：撤销待执行的收起
        dismiss->stop();
    }
}

void TaskBar::onLaunchCardDismissTimeout(const QString &instancePath)
{
    LaunchTaskCard *card = m_launchCards.value(instancePath, nullptr);
    QTimer *dismiss = m_launchDismissTimers.value(instancePath, nullptr);
    if (!card || !dismiss || !card->isVisible())
        return;

    // 鼠标停在启动卡片上时延后收起，避免用户正在看失败信息时卡片突然消失
    if (card->underMouse()) {
        dismiss->start(kHoverDeferMs);
        return;
    }

    // 状态可能已被再次切换（例如自动重试），只在仍处于终态时收起
    const LaunchTaskCard::LaunchStatus status = card->status();
    if (status == LaunchTaskCard::Failed || status == LaunchTaskCard::Stopped) {
        showLaunchCard(instancePath, false);
        dismiss->deleteLater();
        m_launchDismissTimers.remove(instancePath);
    }
}

void TaskBar::updateLaunchCardProgress(const QString &instancePath, int progress)
{
    LaunchTaskCard *card = m_launchCards.value(instancePath, nullptr);
    if (card) {
        card->setProgress(progress);
    }
}

void TaskBar::updateLaunchCardMessage(const QString &instancePath, const QString &message)
{
    LaunchTaskCard *card = m_launchCards.value(instancePath, nullptr);
    if (card) {
        card->setMessage(message);
    }
}

void TaskBar::addLaunchCardDetail(const QString &instancePath, const QString &detail)
{
    LaunchTaskCard *card = m_launchCards.value(instancePath, nullptr);
    if (card) {
        card->addDetail(detail);
    }
}

void TaskBar::removeLaunchCard(const QString &instancePath)
{
    if (QTimer *dismiss = m_launchDismissTimers.take(instancePath)) {
        dismiss->stop();
        dismiss->deleteLater();
    }
    LaunchTaskCard *card = m_launchCards.take(instancePath);
    if (!card)
        return;
    m_launchCardsLayout->removeWidget(card);
    card->deleteLater();
    updateVisibility();
}

bool TaskBar::hasVisibleLaunchCards() const
{
    for (LaunchTaskCard *card : std::as_const(m_launchCards)) {
        // 不能看 isVisible：任务栏初始隐藏或收起时，卡片的祖先不可见，
        // 但卡片本身仍是「要求显示」的状态，任务栏必须据此滑出来。
        if (card && !card->isHidden())
            return true;
    }
    return false;
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
