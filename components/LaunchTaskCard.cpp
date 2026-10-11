#include "LaunchTaskCard.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QMenu>
#include <QPainterPath>
#include <QStyle>

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

void applyWidgetStyle(QWidget *widget, const QString &style)
{
    if (widget && widget->styleSheet() != style)
        widget->setStyleSheet(style);
}
} // namespace

LaunchTaskCard::LaunchTaskCard(QWidget *parent)
    : QWidget(parent)
    , m_status(Idle)
    , m_progress(0)
    , m_hovered(false)
    , m_spinTimer(nullptr)
{
    initUI();
}

LaunchTaskCard::~LaunchTaskCard()
{
}

void LaunchTaskCard::initUI()
{
    setObjectName("launchTaskCard");
    setCursor(QCursor(Qt::PointingHandCursor));
    setAttribute(Qt::WA_Hover);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kCardH);
    connect(this, &QWidget::customContextMenuRequested, this, &LaunchTaskCard::onContextMenu);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(kPadLeft, kPadTop, kPadRight, kPadBottom);
    m_mainLayout->setSpacing(1);

    // 第一行：状态 + 进度
    m_headerLayout = new QHBoxLayout();
    m_headerLayout->setContentsMargins(0, 0, 0, 0);
    m_headerLayout->setSpacing(8);

    m_statusTextLabel = new QLabel(tr("准备启动"));
    m_statusTextLabel->setObjectName("statusTextLabel");
    QFont statusFont = m_statusTextLabel->font();
    statusFont.setPixelSize(12);
    statusFont.setWeight(QFont::DemiBold);
    m_statusTextLabel->setFont(statusFont);
    m_headerLayout->addWidget(m_statusTextLabel, 1);

    m_progressLabel = new QLabel("0%");
    m_progressLabel->setObjectName("progressLabel");
    m_progressLabel->setFixedWidth(kProgressW);
    m_progressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont progressFont = m_progressLabel->font();
    progressFont.setPixelSize(12);
    progressFont.setWeight(QFont::DemiBold);
    m_progressLabel->setFont(progressFont);
    m_headerLayout->addWidget(m_progressLabel);

    m_mainLayout->addLayout(m_headerLayout);

    // 第二行：当前消息（超长省略号截断）
    m_metaLayout = new QHBoxLayout();
    m_metaLayout->setContentsMargins(0, 0, 0, 0);
    m_metaLayout->setSpacing(0);

    m_messageLabel = new QLabel(tr("准备启动游戏..."));
    m_messageLabel->setObjectName("messageLabel");
    m_messageLabel->setMinimumWidth(0);
    m_messageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QFont msgFont = m_messageLabel->font();
    msgFont.setPixelSize(11);
    m_messageLabel->setFont(msgFont);
    m_metaLayout->addWidget(m_messageLabel);

    m_mainLayout->addLayout(m_metaLayout);

    // 启动中的状态徽章需要持续旋转，定时重绘
    m_spinTimer = new QTimer(this);
    m_spinTimer->setInterval(60);
    connect(m_spinTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));

    applyChrome();
    updateAnimationState();
}

void LaunchTaskCard::setStatus(LaunchStatus status)
{
    m_status = status;
    switch (status) {
    case Idle:      m_statusTextLabel->setText(tr("准备启动")); break;
    case Launching: m_statusTextLabel->setText(tr("正在启动")); break;
    case Running:   m_statusTextLabel->setText(tr("游戏运行中")); break;
    case Failed:    m_statusTextLabel->setText(tr("启动失败")); break;
    case Stopped:   m_statusTextLabel->setText(tr("游戏停止")); break;
    }
    updateAnimationState();
    update();
}

void LaunchTaskCard::setProgress(int progress)
{
    m_progress = qBound(0, progress, 100);
    m_progressLabel->setText(QString("%1%").arg(m_progress));
    update();
}

void LaunchTaskCard::setMessage(const QString &message)
{
    m_message = message;
    refreshElidedText();
}

void LaunchTaskCard::addDetail(const QString &detail)
{
    m_details.append(detail);
}

void LaunchTaskCard::clearDetails()
{
    m_details.clear();
}

QStringList LaunchTaskCard::details() const
{
    return m_details;
}

void LaunchTaskCard::applyChrome()
{
    ThemeManager *tm = ThemeManager::instance();
    const QColor text(tm->currentTextColor());

    const QString primary = text.name(QColor::HexArgb);
    QColor secondary = text;
    secondary.setAlpha(160);

    applyWidgetStyle(m_statusTextLabel,
                     QStringLiteral("background: transparent; color: %1;").arg(primary));
    applyWidgetStyle(m_progressLabel,
                     QStringLiteral("background: transparent; color: %1;").arg(primary));
    applyWidgetStyle(m_messageLabel,
                     QStringLiteral("background: transparent; color: %1;").arg(secondary.name(QColor::HexArgb)));
}

void LaunchTaskCard::refreshElidedText()
{
    if (m_message.isEmpty())
    {
        m_messageLabel->clear();
        return;
    }
    const int available = width() - kPadLeft - kPadRight;
    m_messageLabel->setText(m_messageLabel->fontMetrics().elidedText(
        m_message, Qt::ElideRight, qMax(40, available)));
}

void LaunchTaskCard::updateAnimationState()
{
    // 窗口隐藏（卡片被收起）时不空转定时器
    const bool spinning = m_status == Launching && isVisible();
    if (spinning && !m_spinTimer->isActive())
        m_spinTimer->start();
    else if (!spinning && m_spinTimer->isActive())
        m_spinTimer->stop();
}

void LaunchTaskCard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshElidedText();
}

void LaunchTaskCard::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::ApplicationPaletteChange)
    {
        applyChrome();
        update();
    }
    QWidget::changeEvent(event);
}

void LaunchTaskCard::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateAnimationState();
}

void LaunchTaskCard::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateAnimationState();
}

void LaunchTaskCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    ThemeManager *tm = ThemeManager::instance();
    const QColor themeColor(tm->currentThemeColor());
    const bool isDark = tm->currentTheme() != ThemeManager::LightTheme;

    // 玻璃卡面：与下载任务卡片同一套处理
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

void LaunchTaskCard::paintStatusBadge(QPainter &painter)
{
    ThemeManager *tm = ThemeManager::instance();
    const QColor themeColor(tm->currentThemeColor());
    const bool isDark = tm->currentTheme() != ThemeManager::LightTheme;

    QColor statusColor;
    switch (m_status)
    {
    case Idle:      statusColor = QColor("#9E9E9E"); break;
    case Launching: statusColor = QColor(tm->currentInfoAccentColor()); break;
    case Running:   statusColor = themeColor; break;
    case Failed:    statusColor = QColor("#F44336"); break;
    case Stopped:   statusColor = QColor("#FF9800"); break;
    default:        statusColor = QColor("#9E9E9E"); break;
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

    switch (m_status)
    {
    case Idle:
        painter.setBrush(statusColor);
        painter.drawEllipse(iconRect.adjusted(4, 4, -4, -4));
        break;
    case Launching:
    {
        QPen arcPen(statusColor, 2.4);
        arcPen.setCapStyle(Qt::RoundCap);
        painter.setPen(arcPen);
        painter.setBrush(Qt::NoBrush);
        const int startAngle = static_cast<int>((QDateTime::currentMSecsSinceEpoch() / 8) % 360) * 16;
        painter.drawArc(iconRect.adjusted(2, 2, -2, -2), startAngle, 250 * 16);
        break;
    }
    case Running:
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
    case Failed:
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
    case Stopped:
        painter.setBrush(statusColor);
        painter.drawRoundedRect(QRect(iconRect.x() + 3, iconRect.y() + 2, 3, iconSize - 4), 1.5, 1.5);
        painter.drawRoundedRect(QRect(iconRect.x() + iconSize - 6, iconRect.y() + 2, 3, iconSize - 4), 1.5, 1.5);
        break;
    default:
        painter.setBrush(statusColor);
        painter.drawEllipse(iconRect.adjusted(4, 4, -4, -4));
        break;
    }
}

void LaunchTaskCard::paintProgressBar(QPainter &painter)
{
    if (m_status != Launching)
        return;

    ThemeManager *tm = ThemeManager::instance();
    const QColor themeColor(tm->currentThemeColor());
    const QColor progressEnd = themeColor.lighter(130);
    const bool isDark = tm->currentTheme() != ThemeManager::LightTheme;

    const int barHeight = 3;
    const int barY = height() - 7 - barHeight;
    const int barX = 12;
    const int barWidth = width() - 24;

    double filledWidth = barWidth * (m_progress / 100.0);
    if (filledWidth < 0) filledWidth = 0;
    if (filledWidth > barWidth) filledWidth = barWidth;

    QPainterPath trackPath;
    trackPath.addRoundedRect(barX, barY, barWidth, barHeight, barHeight / 2.0, barHeight / 2.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(isDark ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 16));
    painter.drawPath(trackPath);

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

void LaunchTaskCard::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    m_hovered = true;
    update();
}

void LaunchTaskCard::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hovered = false;
    update();
}

void LaunchTaskCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit showDetailsPageRequested();
    }
    QWidget::mousePressEvent(event);
}

void LaunchTaskCard::onContextMenu(const QPoint &pos)
{
    QMenu menu(this);

    menu.addAction(tr("查看详情页面"), this, [this]() { emit showDetailsPageRequested(); });
    menu.addSeparator();

    QAction *copyInfoAction = menu.addAction(tr("复制启动信息"));

    QAction *chosen = menu.exec(mapToGlobal(pos));

    if (chosen == copyInfoAction)
    {
        QString info;
        info += tr("启动状态: ");
        switch (m_status)
        {
        case Idle:      info += tr("准备启动"); break;
        case Launching: info += tr("正在启动"); break;
        case Running:   info += tr("游戏运行中"); break;
        case Failed:    info += tr("启动失败"); break;
        case Stopped:   info += tr("游戏停止"); break;
        default: break;
        }
        info += "\n";
        info += tr("进度: %1%\n").arg(m_progress);
        if (!m_message.isEmpty())
        {
            info += tr("信息: %1\n").arg(m_message);
        }
        if (!m_details.isEmpty())
        {
            info += tr("详细信息:\n");
            for (const QString &detail : m_details)
            {
                info += "  " + detail + "\n";
            }
        }
        QApplication::clipboard()->setText(info);
    }
}
