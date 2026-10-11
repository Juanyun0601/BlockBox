/**
 * @file   DownloadTaskCard.cpp
 * @brief  下载任务卡片组件实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "DownloadTaskCard.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QFont>
#include <QIcon>
#include <QMenu>
#include <QPainterPath>
#include <QPixmap>

#include "utils/ThemeManager.h"

namespace {
// 卡片几何常量：与 initUI 的布局边距、paintStatusBadge / paintProgressBar 的手绘坐标保持一致
constexpr int kCardH = 48;
constexpr int kPadTop = 5;
constexpr int kPadBottom = 10;
constexpr int kPadLeft = 44;    // 状态徽章 10 + 24 + 间距 10
constexpr int kPadRight = 10;
constexpr int kBadgeLeft = 10;
constexpr int kBadgeSize = 24;
constexpr int kProgressW = 56;
constexpr int kRowGap = 8;

void applyWidgetStyle(QWidget *widget, const QString &style)
{
    if (widget && widget->styleSheet() != style)
        widget->setStyleSheet(style);
}

/// 操作按钮图标：自绘而不是用字形，避免 ▶ / ⏸ 被 Segoe UI Emoji 接管成彩色图标
QIcon actionIcon(const QColor &color, int shape)
{
    const int size = 14;
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    switch (shape)
    {
    case 0: // 暂停：两竖条
        p.drawRoundedRect(QRectF(3.5, 3, 2.6, 8), 1.2, 1.2);
        p.drawRoundedRect(QRectF(7.9, 3, 2.6, 8), 1.2, 1.2);
        break;
    case 1: // 继续：右向三角
    {
        QPainterPath tri;
        tri.moveTo(4.5, 3.2);
        tri.lineTo(11.4, 7);
        tri.lineTo(4.5, 10.8);
        tri.closeSubpath();
        p.drawPath(tri);
        break;
    }
    default: // 取消：叉
    {
        QPen pen(color, 2.0);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(QPointF(4.2, 4.2), QPointF(9.8, 9.8));
        p.drawLine(QPointF(9.8, 4.2), QPointF(4.2, 9.8));
        break;
    }
    }
    p.end();
    return QIcon(pm);
}
} // namespace

DownloadTaskCard::DownloadTaskCard(const QString &taskId, QWidget *parent)
    : QWidget(parent)
    , m_taskId(taskId)
    , m_expanded(false)
    , m_hovered(false)
    , m_smoothProgress(0.0)
    , m_smoothEta(-1)
    , m_spinTimer(nullptr)
    , m_pauseIconShape(-1)
{
    initUI();
}

DownloadTaskCard::~DownloadTaskCard()
{
}

void DownloadTaskCard::initUI()
{
    setObjectName("downloadTaskCard");
    setCursor(QCursor(Qt::PointingHandCursor));
    setAttribute(Qt::WA_Hover);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kCardH);
    setMinimumWidth(240);
    setMaximumWidth(380);
    connect(this, &QWidget::customContextMenuRequested, this, &DownloadTaskCard::onContextMenu);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(kPadLeft, kPadTop, kPadRight, kPadBottom);
    m_mainLayout->setSpacing(1);

    // 第一行：实例名 + 进度
    m_headerLayout = new QHBoxLayout();
    m_headerLayout->setContentsMargins(0, 0, 0, 0);
    m_headerLayout->setSpacing(kRowGap);

    m_instanceNameLabel = new QLabel();
    m_instanceNameLabel->setObjectName("instanceNameLabel");
    m_instanceNameLabel->setMinimumWidth(0);
    m_instanceNameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QFont instanceFont = m_instanceNameLabel->font();
    instanceFont.setPixelSize(12);
    instanceFont.setWeight(QFont::DemiBold);
    m_instanceNameLabel->setFont(instanceFont);
    m_headerLayout->addWidget(m_instanceNameLabel, 1);

    m_progressLabel = new QLabel("0.00%");
    m_progressLabel->setObjectName("progressLabel");
    m_progressLabel->setFixedWidth(kProgressW);
    m_progressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont progressFont = m_progressLabel->font();
    progressFont.setPixelSize(12);
    progressFont.setWeight(QFont::DemiBold);
    m_progressLabel->setFont(progressFont);
    m_headerLayout->addWidget(m_progressLabel);

    m_mainLayout->addLayout(m_headerLayout);

    // 第二行：状态 · 速度 + 操作按钮
    m_metaLayout = new QHBoxLayout();
    m_metaLayout->setContentsMargins(0, 0, 0, 0);
    m_metaLayout->setSpacing(6);

    m_statusTextLabel = new QLabel(tr("下载新实例"));
    m_statusTextLabel->setObjectName("statusTextLabel");
    QFont statusFont = m_statusTextLabel->font();
    statusFont.setPixelSize(11);
    m_statusTextLabel->setFont(statusFont);
    m_metaLayout->addWidget(m_statusTextLabel);

    m_speedLabel = new QLabel();
    m_speedLabel->setObjectName("speedLabel");
    QFont speedFont = m_speedLabel->font();
    speedFont.setPixelSize(11);
    m_speedLabel->setFont(speedFont);
    m_metaLayout->addWidget(m_speedLabel);

    m_metaLayout->addStretch();

    m_cancelButton = new QPushButton();
    m_cancelButton->setFixedSize(16, 16);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setToolTip(tr("取消"));
    m_cancelButton->setIcon(actionIcon(QColor("#f44336"), 2));
    m_cancelButton->setIconSize(QSize(14, 14));
    connect(m_cancelButton, &QPushButton::clicked, this, &DownloadTaskCard::onCancelButtonClicked);
    m_metaLayout->addWidget(m_cancelButton);

    m_pauseButton = new QPushButton();
    m_pauseButton->setFixedSize(16, 16);
    m_pauseButton->setCursor(Qt::PointingHandCursor);
    m_pauseButton->setIconSize(QSize(14, 14));
    connect(m_pauseButton, &QPushButton::clicked, this, &DownloadTaskCard::onPauseButtonClicked);
    m_metaLayout->addWidget(m_pauseButton);

    m_mainLayout->addLayout(m_metaLayout);

    m_detailsWidget = new QWidget();
    m_detailsWidget->setVisible(false);
    m_detailsLayout = new QVBoxLayout(m_detailsWidget);
    m_detailsLayout->setContentsMargins(0, 2, 0, 2);
    m_detailsLayout->setSpacing(2);

    m_loadersLabel = new QLabel();
    m_loadersLabel->setObjectName("loadersLabel");
    QFont detailFont = m_loadersLabel->font();
    detailFont.setPixelSize(11);
    m_loadersLabel->setFont(detailFont);
    m_detailsLayout->addWidget(m_loadersLabel);

    m_stepLabel = new QLabel();
    m_stepLabel->setObjectName("stepLabel");
    m_stepLabel->setFont(detailFont);
    m_detailsLayout->addWidget(m_stepLabel);

    m_sizeLabel = new QLabel();
    m_sizeLabel->setObjectName("sizeLabel");
    m_sizeLabel->setFont(detailFont);
    m_detailsLayout->addWidget(m_sizeLabel);

    m_mainLayout->addWidget(m_detailsWidget);

    m_toggleDetailsButton = nullptr;

    // 下载中的状态徽章需要持续旋转，定时重绘（其余状态停止计时，避免空转）
    m_spinTimer = new QTimer(this);
    m_spinTimer->setInterval(60);
    connect(m_spinTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));

    applyChrome();
    updateDisplay();
}

void DownloadTaskCard::setTaskId(const QString &taskId)
{
    m_taskId = taskId;
}

QString DownloadTaskCard::taskId() const
{
    return m_taskId;
}

void DownloadTaskCard::updateFromTask(const DownloadTask &task)
{
    m_currentTask = task;
    m_instanceName = task.instanceName;
    refreshElidedText();
    setExpanded(m_expanded);
    updateDisplay();
}

void DownloadTaskCard::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_detailsWidget->setVisible(expanded);
}

bool DownloadTaskCard::isExpanded() const
{
    return m_expanded;
}

void DownloadTaskCard::onCardClicked()
{
    emit cardClicked(m_taskId);
}

void DownloadTaskCard::onToggleDetails()
{
    setExpanded(!m_expanded);
}

void DownloadTaskCard::onCancelButtonClicked()
{
    emit cancelRequested(m_taskId);
}

void DownloadTaskCard::onPauseButtonClicked()
{
    if (m_currentTask.status == DownloadTaskStatus::Downloading)
    {
        emit pauseRequested(m_taskId);
    }
    else if (m_currentTask.status == DownloadTaskStatus::Paused)
    {
        emit resumeRequested(m_taskId);
    }
}

void DownloadTaskCard::onContextMenu(const QPoint &pos)
{
    QMenu menu(this);

    bool isCancellable = m_currentTask.status == DownloadTaskStatus::Queued
                         || m_currentTask.status == DownloadTaskStatus::Downloading
                         || m_currentTask.status == DownloadTaskStatus::Paused;

    if (m_currentTask.status == DownloadTaskStatus::Downloading)
    {
        menu.addAction(tr("暂停"), this, [this]() { emit pauseRequested(m_taskId); });
    }
    else if (m_currentTask.status == DownloadTaskStatus::Paused)
    {
        menu.addAction(tr("继续"), this, [this]() { emit resumeRequested(m_taskId); });
    }

    if (isCancellable)
    {
        menu.addAction(tr("取消任务"), this, [this]() { emit cancelRequested(m_taskId); });
    }

    menu.addSeparator();
    menu.addAction(tr("查看详情"), this, [this]() { emit showDetailsRequested(m_taskId); });
    menu.addSeparator();

    QAction *copyInfoAction = menu.addAction(tr("复制任务信息"));

    QAction *chosen = menu.exec(mapToGlobal(pos));

    if (chosen == copyInfoAction)
    {
        QString info;
        info += tr("实例: %1\n").arg(m_currentTask.instanceName);
        info += tr("状态: ");
        switch (m_currentTask.status)
        {
        case DownloadTaskStatus::Queued:    info += tr("等待中"); break;
        case DownloadTaskStatus::Downloading: info += tr("下载中"); break;
        case DownloadTaskStatus::Paused:    info += tr("已暂停"); break;
        case DownloadTaskStatus::Completed: info += tr("已完成"); break;
        case DownloadTaskStatus::Failed:    info += tr("失败"); break;
        case DownloadTaskStatus::Cancelled: info += tr("已取消"); break;
        default: break;
        }
        info += "\n";
        if (!m_currentTask.loaders.isEmpty())
        {
            info += tr("加载器: %1\n").arg(m_currentTask.loaders.join(", "));
        }
        if (m_currentTask.bytesTotal > 0)
        {
            info += tr("大小: %1 / %2\n")
                .arg(DownloadUtils::formatSize(m_currentTask.bytesReceived))
                .arg(DownloadUtils::formatSize(m_currentTask.bytesTotal));
        }
        info += tr("进度: %1%").arg(m_currentTask.progressDouble, 0, 'f', 2);
        QApplication::clipboard()->setText(info);
    }
}

void DownloadTaskCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit cardClicked(m_taskId);
    }
    QWidget::mousePressEvent(event);
}

void DownloadTaskCard::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    m_hovered = true;
    update();
}

void DownloadTaskCard::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hovered = false;
    update();
}

void DownloadTaskCard::applyChrome()
{
    ThemeManager *tm = ThemeManager::instance();
    const QColor text(tm->currentTextColor());
    const QColor theme(tm->currentThemeColor());

    const QString primary = text.name(QColor::HexArgb);
    QColor secondary = text;
    secondary.setAlpha(160);
    QColor tertiary = text;
    tertiary.setAlpha(130);

    applyWidgetStyle(m_instanceNameLabel,
                     QStringLiteral("background: transparent; color: %1;").arg(primary));
    applyWidgetStyle(m_progressLabel,
                     QStringLiteral("background: transparent; color: %1;").arg(primary));
    applyWidgetStyle(m_statusTextLabel,
                     QStringLiteral("background: transparent; color: %1;").arg(secondary.name(QColor::HexArgb)));
    applyWidgetStyle(m_speedLabel,
                     QStringLiteral("background: transparent; color: %1;").arg(tertiary.name(QColor::HexArgb)));
    applyWidgetStyle(m_loadersLabel,
                     QStringLiteral("background: transparent; color: %1;").arg(secondary.name(QColor::HexArgb)));
    applyWidgetStyle(m_stepLabel,
                     QStringLiteral("background: transparent; color: %1;").arg(secondary.name(QColor::HexArgb)));
    applyWidgetStyle(m_sizeLabel,
                     QStringLiteral("background: transparent; color: %1;").arg(secondary.name(QColor::HexArgb)));

    const QString base = QStringLiteral(
        "QPushButton { border: none; border-radius: 8px; font-size: 10px; font-weight: 600; padding: 0; }");

    applyWidgetStyle(m_cancelButton,
                     base + QStringLiteral(
                         "QPushButton { background: rgba(244,67,54,0.14); color: #f44336; }"
                         "QPushButton:hover { background: rgba(244,67,54,0.26); }"));

    if (m_currentTask.status == DownloadTaskStatus::Paused)
    {
        applyWidgetStyle(m_pauseButton,
                         base + QStringLiteral(
                                    "QPushButton { background: rgba(%1,%2,%3,0.16); color: %4; }"
                                    "QPushButton:hover { background: rgba(%1,%2,%3,0.28); }")
                                    .arg(theme.red()).arg(theme.green()).arg(theme.blue())
                                    .arg(theme.name()));
    }
    else
    {
        applyWidgetStyle(m_pauseButton,
                         base + QStringLiteral(
                         "QPushButton { background: rgba(255,152,0,0.14); color: #ff9800; }"
                         "QPushButton:hover { background: rgba(255,152,0,0.26); }"));
    }

    // 暂停/继续图标随状态与主题色更新（图标是自绘的，颜色不吃字形回退影响）
    const bool resumable = m_currentTask.status == DownloadTaskStatus::Paused;
    const int shape = resumable ? 1 : 0;
    const QColor iconColor = resumable ? theme : QColor(QStringLiteral("#ff9800"));
    if (shape != m_pauseIconShape || iconColor != m_pauseIconColor)
    {
        m_pauseIconShape = shape;
        m_pauseIconColor = iconColor;
        m_pauseButton->setIcon(actionIcon(iconColor, shape));
    }
}

void DownloadTaskCard::refreshElidedText()
{
    if (m_instanceName.isEmpty())
    {
        m_instanceNameLabel->clear();
        return;
    }
    const int available = width() - kPadLeft - kPadRight - kProgressW - kRowGap;
    m_instanceNameLabel->setText(m_instanceNameLabel->fontMetrics().elidedText(
        m_instanceName, Qt::ElideRight, qMax(40, available)));
}

void DownloadTaskCard::updateAnimationState()
{
    const bool spinning = m_currentTask.status == DownloadTaskStatus::Downloading;
    if (spinning && !m_spinTimer->isActive())
        m_spinTimer->start();
    else if (!spinning && m_spinTimer->isActive())
        m_spinTimer->stop();
}

void DownloadTaskCard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshElidedText();
}

void DownloadTaskCard::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::ApplicationPaletteChange)
    {
        applyChrome();
        update();
    }
    QWidget::changeEvent(event);
}

void DownloadTaskCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    ThemeManager *tm = ThemeManager::instance();
    const QColor themeColor(tm->currentThemeColor());
    const bool isDark = tm->currentTheme() != ThemeManager::LightTheme;

    // 玻璃卡面：亮色用白色提亮、暗色用白色微透明叠加，托盘与壁纸背景都能透出层次
    const QColor fill = isDark ? QColor(255, 255, 255, m_hovered ? 34 : 18)
                               : QColor(255, 255, 255, m_hovered ? 252 : 214);
    QColor borderColor;
    if (m_hovered)
    {
        borderColor = themeColor;
        borderColor.setAlpha(isDark ? 130 : 100);
    }
    else
    {
        borderColor = isDark ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 18);
    }

    QPainterPath cardPath;
    cardPath.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    painter.fillPath(cardPath, fill);
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(cardPath);

    paintStatusBadge(painter);
    paintProgressBar(painter);
}

void DownloadTaskCard::paintStatusBadge(QPainter &painter)
{
    ThemeManager *tm = ThemeManager::instance();
    const QColor themeColor(tm->currentThemeColor());
    const bool isDark = tm->currentTheme() != ThemeManager::LightTheme;

    QColor statusColor;
    switch (m_currentTask.status)
    {
    case DownloadTaskStatus::Queued:       statusColor = QColor("#9E9E9E"); break;
    case DownloadTaskStatus::Downloading:  statusColor = QColor(tm->currentInfoAccentColor()); break;
    case DownloadTaskStatus::Paused:       statusColor = QColor("#FF9800"); break;
    case DownloadTaskStatus::Completed:    statusColor = themeColor; break;
    case DownloadTaskStatus::Failed:       statusColor = QColor("#F44336"); break;
    case DownloadTaskStatus::Cancelled:    statusColor = QColor("#FF9800"); break;
    default:                               statusColor = QColor("#9E9E9E"); break;
    }

    const int badgeY = kPadTop + qMax(0, (height() - kPadTop - kPadBottom - kBadgeSize) / 2);
    const QRect badgeRect(kBadgeLeft, badgeY, kBadgeSize, kBadgeSize);

    painter.setPen(Qt::NoPen);
    QColor badgeFill = statusColor;
    badgeFill.setAlpha(isDark ? 40 : 24);
    painter.setBrush(badgeFill);
    painter.drawRoundedRect(badgeRect, 8, 8);

    const int iconSize = 14;
    const QRect iconRect(badgeRect.center().x() - iconSize / 2,
                         badgeRect.center().y() - iconSize / 2, iconSize, iconSize);

    switch (m_currentTask.status)
    {
    case DownloadTaskStatus::Queued:
        painter.setBrush(statusColor);
        painter.drawEllipse(iconRect.adjusted(4, 4, -4, -4));
        break;
    case DownloadTaskStatus::Downloading:
    {
        QPen arcPen(statusColor, 2.4);
        arcPen.setCapStyle(Qt::RoundCap);
        painter.setPen(arcPen);
        painter.setBrush(Qt::NoBrush);
        const int startAngle = static_cast<int>((QDateTime::currentMSecsSinceEpoch() / 8) % 360) * 16;
        painter.drawArc(iconRect.adjusted(2, 2, -2, -2), startAngle, 250 * 16);
        break;
    }
    case DownloadTaskStatus::Paused:
        painter.setBrush(statusColor);
        painter.drawRoundedRect(QRect(iconRect.x() + 3, iconRect.y() + 2, 3, iconSize - 4), 1.5, 1.5);
        painter.drawRoundedRect(QRect(iconRect.x() + iconSize - 6, iconRect.y() + 2, 3, iconSize - 4), 1.5, 1.5);
        break;
    case DownloadTaskStatus::Completed:
    {
        painter.setBrush(Qt::NoBrush);
        QPen checkPen(statusColor, 2.2);
        checkPen.setCapStyle(Qt::RoundCap);
        checkPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(checkPen);
        QPainterPath checkPath;
        checkPath.moveTo(iconRect.x() + 3, iconRect.y() + 7);
        checkPath.lineTo(iconRect.x() + 6, iconRect.y() + 10.5);
        checkPath.lineTo(iconRect.x() + 11.5, iconRect.y() + 3.5);
        painter.drawPath(checkPath);
        break;
    }
    case DownloadTaskStatus::Failed:
    {
        painter.setBrush(Qt::NoBrush);
        QPen xPen(statusColor, 2.2);
        xPen.setCapStyle(Qt::RoundCap);
        painter.setPen(xPen);
        QRect xRect = iconRect.adjusted(3, 3, -3, -3);
        painter.drawLine(xRect.topLeft(), xRect.bottomRight());
        painter.drawLine(xRect.topRight(), xRect.bottomLeft());
        break;
    }
    case DownloadTaskStatus::Cancelled:
        painter.setBrush(statusColor);
        painter.drawRoundedRect(iconRect.adjusted(3, 3, -3, -3), 2, 2);
        break;
    default:
        painter.setBrush(statusColor);
        painter.drawEllipse(iconRect.adjusted(4, 4, -4, -4));
        break;
    }
}

void DownloadTaskCard::paintProgressBar(QPainter &painter)
{
    if (m_currentTask.status != DownloadTaskStatus::Downloading
        && m_currentTask.status != DownloadTaskStatus::Paused
        && m_currentTask.status != DownloadTaskStatus::Queued)
    {
        return;
    }

    ThemeManager *tm = ThemeManager::instance();
    const QColor themeColor(tm->currentThemeColor());
    const QColor progressEnd = themeColor.lighter(130);
    const bool isDark = tm->currentTheme() != ThemeManager::LightTheme;

    const int barHeight = 3;
    const int barY = height() - 7 - barHeight;
    const int barX = 12;
    const int barWidth = width() - 24;

    double progress = m_smoothProgress;
    if (m_currentTask.status == DownloadTaskStatus::Queued)
    {
        progress = 0.0;
    }
    double filledWidth = barWidth * (progress / 100.0);
    if (filledWidth < 0) filledWidth = 0;
    if (filledWidth > barWidth) filledWidth = barWidth;

    // 背景轨道
    QPainterPath trackPath;
    trackPath.addRoundedRect(barX, barY, barWidth, barHeight, barHeight / 2.0, barHeight / 2.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(isDark ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 16));
    painter.drawPath(trackPath);

    // 主题色渐变填充，端点与任务列表页进度条一致
    if (filledWidth > 0)
    {
        QLinearGradient gradient(barX, 0, barX + filledWidth, 0);
        gradient.setColorAt(0.0, themeColor);
        gradient.setColorAt(1.0, progressEnd);

        QPainterPath fillPath;
        fillPath.addRoundedRect(barX, barY, static_cast<int>(filledWidth), barHeight,
                                barHeight / 2.0, barHeight / 2.0);
        painter.setBrush(gradient);
        painter.drawPath(fillPath);
    }
}

void DownloadTaskCard::updateDisplay()
{
    // 指数平滑处理（参考 PCL2 做法），使进度条动画流畅不抖动
    double actualProgress = m_currentTask.progressDouble;
    if (m_currentTask.status == DownloadTaskStatus::Downloading)
    {
        if (actualProgress < m_smoothProgress)
        {
            m_smoothProgress = actualProgress;
        }
        else
        {
            m_smoothProgress += (actualProgress - m_smoothProgress) * 0.3;
            if (m_smoothProgress < actualProgress
                && m_smoothProgress + 0.01 < actualProgress)
            {
                m_smoothProgress += 0.01;
            }
            if (m_smoothProgress > actualProgress)
            {
                m_smoothProgress = actualProgress;
            }
        }
    }
    else
    {
        m_smoothProgress = actualProgress;
    }

    m_progressLabel->setText(QString("%1%").arg(m_smoothProgress, 0, 'f', 2));

    QString statusText;
    switch (m_currentTask.status)
    {
    case DownloadTaskStatus::Queued:
        statusText = tr("等待中");
        break;
    case DownloadTaskStatus::Downloading:
        statusText = tr("下载中");
        break;
    case DownloadTaskStatus::Paused:
        statusText = tr("已暂停");
        break;
    case DownloadTaskStatus::Completed:
        statusText = tr("已完成");
        break;
    case DownloadTaskStatus::Failed:
        statusText = tr("失败");
        break;
    case DownloadTaskStatus::Cancelled:
        statusText = tr("已取消");
        break;
    default:
        statusText = tr("下载新实例");
        break;
    }
    m_statusTextLabel->setText(statusText);

    // 速度显示
    if (m_currentTask.downloadSpeed > 0
        && m_currentTask.status == DownloadTaskStatus::Downloading)
    {
        m_speedLabel->setText(DownloadUtils::formatSpeed(m_currentTask.downloadSpeed));
        m_speedLabel->setVisible(true);
    }
    else
    {
        m_speedLabel->setVisible(false);
    }

    // 取消按钮可见性
    bool isCancellable = m_currentTask.status == DownloadTaskStatus::Queued
                         || m_currentTask.status == DownloadTaskStatus::Downloading
                         || m_currentTask.status == DownloadTaskStatus::Paused;
    m_cancelButton->setVisible(isCancellable);

    // 详情区域
    if (!m_currentTask.loaders.isEmpty())
    {
        m_loadersLabel->setText(tr("加载器：%1").arg(m_currentTask.loaders.join("\u3001")));
        m_loadersLabel->setVisible(true);
    }
    else
    {
        m_loadersLabel->setVisible(false);
    }

    if (!m_currentTask.currentStep.isEmpty())
    {
        m_stepLabel->setText(tr("当前：%1").arg(m_currentTask.currentStep));
    }
    else
    {
        m_stepLabel->setText(tr("当前：%1").arg(m_currentTask.statusMessage));
    }

    if (m_currentTask.bytesTotal > 0)
    {
        QString sizeText = tr("大小：%1 / %2")
                               .arg(DownloadUtils::formatSize(m_currentTask.bytesReceived),
                                    DownloadUtils::formatSize(m_currentTask.bytesTotal));
        if (m_currentTask.downloadSpeed > 0
            && m_currentTask.status == DownloadTaskStatus::Downloading)
        {
            qint64 remaining = m_currentTask.bytesTotal - m_currentTask.bytesReceived;
            if (remaining > 0)
            {
                // ETA 指数平滑，避免剧烈跳变
                int rawEta = static_cast<int>(remaining / m_currentTask.downloadSpeed);
                if (m_smoothEta < 0)
                {
                    m_smoothEta = rawEta;
                }
                else
                {
                    m_smoothEta = static_cast<int>(m_smoothEta
                        + (rawEta - m_smoothEta) * 0.2);
                }
                sizeText += tr(" | 剩余：%1").arg(DownloadUtils::formatEta(m_smoothEta));
            }
        }
        else
        {
            m_smoothEta = -1;
        }
        m_sizeLabel->setText(sizeText);
    }
    else
    {
        m_sizeLabel->setText(tr("大小：计算中..."));
    }

    // 操作按钮：取消常驻于可取消状态；暂停/继续随状态切换（图标由 applyChrome 绘制）
    const bool isPausable = m_currentTask.status == DownloadTaskStatus::Downloading;
    const bool isResumable = m_currentTask.status == DownloadTaskStatus::Paused;
    m_pauseButton->setVisible(isPausable || isResumable);
    if (isResumable)
        m_pauseButton->setToolTip(tr("继续"));
    else if (isPausable)
        m_pauseButton->setToolTip(tr("暂停"));

    applyChrome();
    updateAnimationState();
    update();
}