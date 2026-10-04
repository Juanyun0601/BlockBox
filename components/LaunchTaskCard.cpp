#include "LaunchTaskCard.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QMenu>
#include <QPainterPath>
#include <QStyle>

#include "utils/ThemeManager.h"

LaunchTaskCard::LaunchTaskCard(QWidget *parent)
    : QWidget(parent)
    , m_status(Idle)
    , m_progress(0)
    , m_hovered(false)
{
    initUI();
}

LaunchTaskCard::~LaunchTaskCard()
{
}

void LaunchTaskCard::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(8, 4, 8, 8);
    m_mainLayout->setSpacing(4);

    setObjectName("launchTaskCard");
    setCursor(QCursor(Qt::PointingHandCursor));
    setMinimumHeight(36);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setAttribute(Qt::WA_Hover);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &LaunchTaskCard::onContextMenu);

    m_headerLayout = new QHBoxLayout();
    m_headerLayout->setContentsMargins(22, 0, 0, 0);
    m_headerLayout->setSpacing(6);

    m_statusTextLabel = new QLabel(tr("准备启动"));
    m_statusTextLabel->setObjectName("statusTextLabel");
    QFont statusFont = m_statusTextLabel->font();
    statusFont.setPointSize(9);
    statusFont.setBold(true);
    m_statusTextLabel->setFont(statusFont);

    m_messageLabel = new QLabel(tr("准备启动游戏..."));
    m_messageLabel->setObjectName("messageLabel");
    QFont msgFont = m_messageLabel->font();
    msgFont.setPointSize(8);
    m_messageLabel->setFont(msgFont);

    m_progressLabel = new QLabel("0%");
    m_progressLabel->setObjectName("progressLabel");
    m_progressLabel->setFixedWidth(50);
    m_progressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont progressFont = m_progressLabel->font();
    progressFont.setPointSize(9);
    progressFont.setBold(true);
    m_progressLabel->setFont(progressFont);

    m_headerLayout->addWidget(m_statusTextLabel, 1);
    m_headerLayout->addWidget(m_messageLabel);
    m_headerLayout->addWidget(m_progressLabel);

    m_mainLayout->addLayout(m_headerLayout);
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
    m_messageLabel->setText(m_message);
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

void LaunchTaskCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    QString textColorStr = ThemeManager::instance()->currentTextColor();
    QColor textColor(textColorStr);

    QColor cardBg = themeColor.lighter(190);
    if (!cardBg.isValid())
        cardBg = QColor("#f0f4f0");

    QColor cardBgHover = themeColor.lighter(175);
    if (!cardBgHover.isValid())
        cardBgHover = QColor("#e8eee8");

    QColor borderColor = themeColor;
    borderColor.setAlpha(m_hovered ? 80 : 40);

    QPainterPath cardPath;
    cardPath.addRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
    painter.fillPath(cardPath, m_hovered ? cardBgHover : cardBg);
    painter.setPen(QPen(borderColor, 1));
    painter.drawPath(cardPath);

    paintStatusIcon(painter, rect());
    paintProgressBar(painter, rect());

    QString labelColor = textColor.name(QColor::HexArgb);
    m_statusTextLabel->setStyleSheet(QString("background: transparent; color: %1;").arg(textColor.name()));
    m_messageLabel->setStyleSheet(QString("background: transparent; color: %1;").arg(labelColor));
    m_progressLabel->setStyleSheet(QString("background: transparent; color: %1;").arg(textColor.name()));
}

void LaunchTaskCard::paintStatusIcon(QPainter &painter, const QRect &rect)
{
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    QColor statusColor;

    switch (m_status)
    {
    case Idle:
        statusColor = QColor("#9E9E9E");
        break;
    case Launching:
        statusColor = QColor(ThemeManager::instance()->currentInfoAccentColor());
        break;
    case Running:
        statusColor = themeColor;
        break;
    case Failed:
        statusColor = QColor("#F44336");
        break;
    case Stopped:
        statusColor = QColor("#FF9800");
        break;
    default:
        statusColor = QColor("#9E9E9E");
        break;
    }

    int iconSize = 14;
    int iconY = (height() < 36) ? 2 : 6;
    QRect iconRect(10, iconY, iconSize, iconSize);

    painter.setPen(Qt::NoPen);
    painter.setRenderHint(QPainter::Antialiasing);

    switch (m_status)
    {
    case Idle:
    {
        painter.setBrush(statusColor);
        painter.drawEllipse(iconRect.adjusted(2, 2, -2, -2));
        break;
    }
    case Launching:
    {
        QPen arcPen(statusColor, 2);
        arcPen.setCapStyle(Qt::RoundCap);
        painter.setPen(arcPen);
        painter.setBrush(Qt::NoBrush);
        QRect arcRect = iconRect.adjusted(1, 1, -1, -1);
        int startAngle = (QDateTime::currentMSecsSinceEpoch() / 8) % 360 * 16;
        painter.drawArc(arcRect, startAngle, 240 * 16);
        break;
    }
    case Running:
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
    case Failed:
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
    case Stopped:
    {
        painter.setBrush(statusColor);
        int barWidth = 3;
        int barHeight = iconSize - 4;
        int barY = iconRect.y() + 2;
        painter.drawRoundedRect(QRect(iconRect.x() + 2, barY, barWidth, barHeight), 1, 1);
        painter.drawRoundedRect(QRect(iconRect.x() + iconSize - barWidth - 2, barY, barWidth, barHeight), 1, 1);
        break;
    }
    default:
    {
        painter.setBrush(statusColor);
        painter.drawEllipse(iconRect.adjusted(2, 2, -2, -2));
        break;
    }
    }
}

void LaunchTaskCard::paintProgressBar(QPainter &painter, const QRect &rect)
{
    if (m_status != Launching)
        return;

    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    QColor progressEnd = themeColor.lighter(130);

    int barHeight = 3;
    int barY = rect.bottom() - barHeight - 2;
    int barX = 2;
    int barWidth = rect.width() - 4;

    double filledWidth = barWidth * (m_progress / 100.0);
    if (filledWidth < 0) filledWidth = 0;
    if (filledWidth > barWidth) filledWidth = barWidth;

    QPainterPath trackPath;
    trackPath.addRoundedRect(barX, barY, barWidth, barHeight, barHeight / 2.0, barHeight / 2.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 20));
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
