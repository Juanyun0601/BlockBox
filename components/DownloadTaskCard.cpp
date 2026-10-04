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
#include <QMenu>
#include <QPainterPath>

#include "utils/ThemeManager.h"

DownloadTaskCard::DownloadTaskCard(const QString &taskId, QWidget *parent)
    : QWidget(parent)
    , m_taskId(taskId)
    , m_expanded(false)
    , m_hovered(false)
    , m_smoothProgress(0.0)
    , m_smoothEta(-1)
{
    initUI();
}

DownloadTaskCard::~DownloadTaskCard()
{
}

void DownloadTaskCard::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(8, 4, 8, 8);
    m_mainLayout->setSpacing(4);

    setObjectName("downloadTaskCard");
    setCursor(QCursor(Qt::PointingHandCursor));
    setMinimumHeight(36);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setAttribute(Qt::WA_Hover);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &DownloadTaskCard::onContextMenu);

    m_headerLayout = new QHBoxLayout();
    m_headerLayout->setContentsMargins(22, 0, 0, 0);
    m_headerLayout->setSpacing(6);

    m_instanceNameLabel = new QLabel();
    m_instanceNameLabel->setObjectName("instanceNameLabel");
    QFont instanceFont = m_instanceNameLabel->font();
    instanceFont.setPointSize(9);
    instanceFont.setBold(true);
    m_instanceNameLabel->setFont(instanceFont);
    m_headerLayout->addWidget(m_instanceNameLabel, 1);

    m_statusTextLabel = new QLabel(tr("下载新实例"));
    m_statusTextLabel->setObjectName("statusTextLabel");
    QFont statusFont = m_statusTextLabel->font();
    statusFont.setPointSize(8);
    m_statusTextLabel->setFont(statusFont);
    m_headerLayout->addWidget(m_statusTextLabel);

    m_progressLabel = new QLabel("0.00%");
    m_progressLabel->setObjectName("progressLabel");
    m_progressLabel->setFixedWidth(50);
    m_progressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont progressFont = m_progressLabel->font();
    progressFont.setPointSize(9);
    progressFont.setBold(true);
    m_progressLabel->setFont(progressFont);
    m_headerLayout->addWidget(m_progressLabel);

    m_speedLabel = new QLabel();
    m_speedLabel->setObjectName("speedLabel");
    m_speedLabel->setFixedWidth(70);
    m_speedLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont speedFont = m_speedLabel->font();
    speedFont.setPointSize(8);
    m_speedLabel->setFont(speedFont);
    m_headerLayout->addWidget(m_speedLabel);

    m_cancelButton = new QPushButton();
    m_cancelButton->setFixedSize(18, 18);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setToolTip(tr("取消"));
    connect(m_cancelButton, &QPushButton::clicked, this, &DownloadTaskCard::onCancelButtonClicked);
    m_headerLayout->addWidget(m_cancelButton);

    m_pauseButton = new QPushButton();
    m_pauseButton->setFixedSize(18, 18);
    m_pauseButton->setCursor(Qt::PointingHandCursor);
    connect(m_pauseButton, &QPushButton::clicked, this, &DownloadTaskCard::onPauseButtonClicked);
    m_headerLayout->addWidget(m_pauseButton);

    m_mainLayout->addLayout(m_headerLayout);

    m_detailsWidget = new QWidget();
    m_detailsWidget->setVisible(false);
    m_detailsLayout = new QVBoxLayout(m_detailsWidget);
    m_detailsLayout->setContentsMargins(24, 2, 0, 2);
    m_detailsLayout->setSpacing(2);

    m_loadersLabel = new QLabel();
    m_loadersLabel->setObjectName("loadersLabel");
    QFont detailFont = m_loadersLabel->font();
    detailFont.setPointSize(9);
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
    m_instanceNameLabel->setText(task.instanceName);
    setExpanded(m_expanded);
    updateDisplay();
}

void DownloadTaskCard::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_detailsWidget->setVisible(expanded);
    if (m_toggleDetailsButton)
    {
        m_toggleDetailsButton->setText(expanded ? "\u25B2" : "\u25BC");
    }

    if (expanded)
    {
        setMinimumHeight(80);
        setMaximumHeight(16777215);
    }
    else
    {
        setMinimumHeight(36);
        setMaximumHeight(16777215);
    }
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

void DownloadTaskCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    QString textColorStr = ThemeManager::instance()->currentTextColor();
    QColor textColor(textColorStr);

    // 绘制卡片背景
    QColor cardBg = themeColor.lighter(190);
    if (!cardBg.isValid())
    {
        cardBg = QColor("#f0f4f0");
    }
    QColor cardBgHover = themeColor.lighter(175);
    if (!cardBgHover.isValid())
    {
        cardBgHover = QColor("#e8eee8");
    }
    QColor borderColor = themeColor;
    borderColor.setAlpha(m_hovered ? 80 : 40);

    QPainterPath cardPath;
    cardPath.addRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
    painter.fillPath(cardPath, m_hovered ? cardBgHover : cardBg);
    painter.setPen(QPen(borderColor, 1));
    painter.drawPath(cardPath);

    // 绘制状态图标
    QColor statusColor;
    switch (m_currentTask.status)
    {
    case DownloadTaskStatus::Queued:
        statusColor = QColor("#9E9E9E");
        break;
    case DownloadTaskStatus::Downloading:
        statusColor = QColor(ThemeManager::instance()->currentInfoAccentColor());
        break;
    case DownloadTaskStatus::Paused:
        statusColor = QColor("#FF9800");
        break;
    case DownloadTaskStatus::Completed:
        statusColor = themeColor;
        break;
    case DownloadTaskStatus::Failed:
        statusColor = QColor("#F44336");
        break;
    case DownloadTaskStatus::Cancelled:
        statusColor = QColor("#FF9800");
        break;
    default:
        statusColor = QColor("#9E9E9E");
        break;
    }

    QRect iconRect;
    int iconSize = 14;
    int iconY = (height() < 36) ? 2 : 6;
    iconRect = QRect(10, iconY, iconSize, iconSize);

    painter.setPen(Qt::NoPen);

    switch (m_currentTask.status)
    {
    case DownloadTaskStatus::Queued:
        // 实心圆 - 排队中
        painter.setBrush(statusColor);
        painter.drawEllipse(iconRect.adjusted(2, 2, -2, -2));
        break;
    case DownloadTaskStatus::Downloading:
    {
        // 旋转弧 - 下载中
        QPen arcPen(statusColor, 2);
        arcPen.setCapStyle(Qt::RoundCap);
        painter.setPen(arcPen);
        painter.setBrush(Qt::NoBrush);
        QRect arcRect = iconRect.adjusted(1, 1, -1, -1);
        int startAngle = (QDateTime::currentMSecsSinceEpoch() / 8) % 360 * 16;
        painter.drawArc(arcRect, startAngle, 240 * 16);
        break;
    }
    case DownloadTaskStatus::Paused:
        // 暂停双竖线
    {
        painter.setBrush(statusColor);
        int barWidth = 3;
        int barHeight = iconSize - 4;
        int barY = iconRect.y() + 2;
        painter.drawRoundedRect(QRect(iconRect.x() + 2, barY, barWidth, barHeight), 1, 1);
        painter.drawRoundedRect(QRect(iconRect.x() + iconSize - barWidth - 2, barY, barWidth, barHeight), 1, 1);
        break;
    }
    case DownloadTaskStatus::Completed:
        // 对勾 - 已完成
    {
        painter.setBrush(Qt::NoBrush);
        QPen checkPen(statusColor, 2);
        checkPen.setCapStyle(Qt::RoundCap);
        checkPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(checkPen);
        QPointF p1(iconRect.x() + iconSize * 0.2, iconRect.y() + iconSize * 0.55);
        QPointF p2(iconRect.x() + iconSize * 0.45, iconRect.y() + iconSize * 0.78);
        QPointF p3(iconRect.x() + iconSize * 0.82, iconRect.y() + iconSize * 0.22);
        QPainterPath checkPath;
        checkPath.moveTo(p1);
        checkPath.lineTo(p2);
        checkPath.lineTo(p3);
        painter.drawPath(checkPath);
        break;
    }
    case DownloadTaskStatus::Failed:
        // X标记 - 失败
    {
        painter.setBrush(Qt::NoBrush);
        QPen xPen(statusColor, 2);
        xPen.setCapStyle(Qt::RoundCap);
        painter.setPen(xPen);
        QRect xRect = iconRect.adjusted(3, 3, -3, -3);
        painter.drawLine(xRect.topLeft(), xRect.bottomRight());
        painter.drawLine(xRect.topRight(), xRect.bottomLeft());
        break;
    }
    case DownloadTaskStatus::Cancelled:
        // 正方形 - 已取消
        painter.setBrush(statusColor);
        painter.drawRoundedRect(iconRect.adjusted(3, 3, -3, -3), 2, 2);
        break;
    default:
        painter.setBrush(statusColor);
        painter.drawEllipse(iconRect.adjusted(2, 2, -2, -2));
        break;
    }

    // 绘制进度条
    paintProgressBar(painter, rect());

    // 更新子控件样式
    QString btnStyleBase =
        "QPushButton {"
        "   border: none;"
        "   border-radius: 9px;"
        "   font-size: 10px;"
        "   font-weight: bold;"
        "}";
    QString cancelStyle = btnStyleBase +
        "QPushButton { background: rgba(244,67,54,0.12); color: #f44336; }"
        "QPushButton:hover { background: rgba(244,67,54,0.22); }";
    m_cancelButton->setStyleSheet(cancelStyle);
    m_cancelButton->setText("\u2715");

    bool isPausable = m_currentTask.status == DownloadTaskStatus::Downloading;
    bool isResumable = m_currentTask.status == DownloadTaskStatus::Paused;
    m_pauseButton->setVisible(isPausable || isResumable);

    if (isResumable)
    {
        QString themeColorStr = themeColor.name();
        QString themeHover = ThemeManager::instance()->getThemeColorHover();
        QString resumeStyle = btnStyleBase +
            QString("QPushButton { background: rgba(%1,%2,%3,0.12); color: %4; }"
                    "QPushButton:hover { background: rgba(%1,%2,%3,0.22); color: %5; }")
                .arg(themeColor.red()).arg(themeColor.green()).arg(themeColor.blue())
                .arg(themeColorStr, themeHover);
        m_pauseButton->setStyleSheet(resumeStyle);
        m_pauseButton->setText("\u25B6");
        m_pauseButton->setToolTip(tr("继续"));
    }
    else if (isPausable)
    {
        QString pauseStyle = btnStyleBase +
            "QPushButton { background: rgba(255,152,0,0.12); color: #FF9800; }"
            "QPushButton:hover { background: rgba(255,152,0,0.22); }";
        m_pauseButton->setStyleSheet(pauseStyle);
        m_pauseButton->setText("\u23F8");
        m_pauseButton->setToolTip(tr("暂停"));
    }

    // 标签颜色
    QColor detailColor = textColor;
    detailColor.setAlpha(160);
    QString labelColor = detailColor.name(QColor::HexArgb);
    QString labelStyle = QString("background: transparent; color: %1;").arg(labelColor);
    m_statusTextLabel->setStyleSheet(labelStyle);
    m_progressLabel->setStyleSheet(QString("background: transparent; color: %1;").arg(textColorStr));
    m_speedLabel->setStyleSheet(labelStyle);
    m_instanceNameLabel->setStyleSheet(QString("background: transparent; color: %1;").arg(textColorStr));
    m_loadersLabel->setStyleSheet(labelStyle);
    m_stepLabel->setStyleSheet(labelStyle);
    m_sizeLabel->setStyleSheet(labelStyle);
}

void DownloadTaskCard::paintProgressBar(QPainter &painter, const QRect &rect)
{
    if (m_currentTask.status != DownloadTaskStatus::Downloading
        && m_currentTask.status != DownloadTaskStatus::Paused
        && m_currentTask.status != DownloadTaskStatus::Queued)
    {
        return;
    }

    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    QColor progressEnd = themeColor.lighter(130);

    int barHeight = 3;
    int barY = rect.bottom() - barHeight - 2;
    int barX = 2;
    int barWidth = rect.width() - 4;

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
    painter.setBrush(QColor(0, 0, 0, 20));
    painter.drawPath(trackPath);

    // 渐变填充条
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

    update();
}