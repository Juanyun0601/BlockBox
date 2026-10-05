#include "GameFloatingIcon.h"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

GameFloatingIcon::GameFloatingIcon(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("gameFloatingIcon"));
    setFixedSize(48, 48);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("实例助手"));
}

void GameFloatingIcon::setIconPixmap(const QPixmap &pixmap)
{
    m_iconPixmap = pixmap;
    update();
}

void GameFloatingIcon::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 半透明深色圆底 + 细描边，保证在游戏画面上可见且不遮挡过多内容
    QPainterPath circle;
    circle.addEllipse(rect().adjusted(1, 1, -1, -1));
    painter.fillPath(circle, QColor(20, 22, 26, 130));
    painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
    painter.drawPath(circle);

    if (!m_iconPixmap.isNull()) {
        const int inner = width() / 2;
        const QPixmap scaled = m_iconPixmap.scaled(inner, inner,
                                                   Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
        painter.drawPixmap((width() - scaled.width()) / 2,
                           (height() - scaled.height()) / 2, scaled);
    }
}

void GameFloatingIcon::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_dragging = false;
    }
    QWidget::mousePressEvent(event);
}

void GameFloatingIcon::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton && parentWidget()) {
        QPoint newPos = event->globalPosition().toPoint() - m_pressOffset;
        // 位移超过阈值视为拖动，避免误触发点击
        if (!m_dragging && (newPos - pos()).manhattanLength() > 6) {
            m_dragging = true;
        }
        if (m_dragging) {
            // 限制在父窗口范围内
            const QRect bounds = parentWidget()->rect();
            newPos.setX(qBound(0, newPos.x(), bounds.width() - width()));
            newPos.setY(qBound(0, newPos.y(), bounds.height() - height()));
            move(newPos);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void GameFloatingIcon::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_dragging) {
        emit assistantToggleRequested();
    }
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void GameFloatingIcon::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    raise();
    if (!m_positioned && parentWidget()) {
        // 默认停靠在父窗口左缘垂直居中，与右侧弹出的实例助手侧栏错开
        const QRect bounds = parentWidget()->rect();
        move(12, (bounds.height() - height()) / 2);
        m_positioned = true;
    }
}
