#include "CardSelectionOverlay.h"

#include <QEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QEasingCurve>

#include "utils/ThemeManager.h"

CardSelectionOverlay::CardSelectionOverlay(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("cardSelectionOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // 跟随卡片尺寸：卡片被布局重排时遮罩同步占满整卡
    if (parent)
    {
        parent->installEventFilter(this);
        setGeometry(parent->rect());
    }
    hide();

    m_anim = new QPropertyAnimation(this, "highlightOpacity", this);
    m_anim->setDuration(180);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    // 淡出结束后自动隐藏，不再吃命中测试与绘制
    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacity <= 0.01)
            hide();
    });
    // 主题色变化时下一帧取新色，选中的高亮不用等重建
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, QOverload<>::of(&QWidget::update));
}

void CardSelectionOverlay::setHighlightOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
    update();
}

void CardSelectionOverlay::setSelected(bool selected)
{
    m_anim->stop();
    m_anim->setStartValue(m_opacity);
    m_anim->setEndValue(selected ? 1.0 : 0.0);
    if (selected)
        show();
    m_anim->start();
}

void CardSelectionOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (m_opacity <= 0.0)
        return;

    const QColor c = ThemeManager::instance()->currentThemeColor();
    QColor fill = c;
    fill.setAlpha(40);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_opacity);
    // 内收 1px 让 2px 描边完整落在卡片矩形内
    p.setPen(QPen(c, 2));
    p.setBrush(fill);
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 6, 6);
}

bool CardSelectionOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parent() && event->type() == QEvent::Resize && parentWidget())
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(watched, event);
}
