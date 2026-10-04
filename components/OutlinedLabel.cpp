#include "OutlinedLabel.h"
#include "utils/ThemeManager.h"

#include <QColor>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

OutlinedLabel::OutlinedLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent),
      m_outlineWidth(1),
      m_customColor(false)
{
    // 跟随主题文字边框颜色变化实时重绘
    connect(ThemeManager::instance(), &ThemeManager::textBorderColorChanged,
            this, [this](const QString &) {
        if (!m_customColor)
            update();
    });
}

int OutlinedLabel::outlineWidth() const
{
    return m_outlineWidth;
}

void OutlinedLabel::setOutlineWidth(int width)
{
    m_outlineWidth = qMax(1, width);
    update();
}

QColor OutlinedLabel::outlineColor() const
{
    if (m_customColor)
        return m_outlineColor;
    return QColor(ThemeManager::instance()->currentTextBorderColor());
}

void OutlinedLabel::setOutlineColor(const QColor &color)
{
    m_customColor = true;
    m_outlineColor = color;
    update();
}

void OutlinedLabel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 绘制样式表背景（透明背景时无副作用）
    QStyleOption opt;
    opt.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);

    const QString txt = text();
    if (txt.isEmpty())
        return;

    // 布局 flags（与 QLabel 一致：alignment + wordWrap）
    int flags = 0;
    if (alignment() & Qt::AlignLeft)     flags |= Qt::AlignLeft;
    if (alignment() & Qt::AlignRight)    flags |= Qt::AlignRight;
    if (alignment() & Qt::AlignHCenter)  flags |= Qt::AlignHCenter;
    if (alignment() & Qt::AlignTop)      flags |= Qt::AlignTop;
    if (alignment() & Qt::AlignVCenter)  flags |= Qt::AlignVCenter;
    if (alignment() & Qt::AlignBottom)   flags |= Qt::AlignBottom;
    if (wordWrap())
        flags |= Qt::TextWordWrap;
    else
        flags |= Qt::TextSingleLine;

    const QColor outline = outlineColor();
    // 文字填充色：优先使用样式表前景色（palette 已由 QSS polish 更新）
    const QColor fill = palette().color(QPalette::WindowText);
    const QRect r = rect();

    painter.setFont(font());

    // 通过 8 方向偏移绘制描边
    const int w = m_outlineWidth;
    painter.setPen(QPen(outline));
    const QPoint offsets[] = {
        QPoint(-w, -w), QPoint(0, -w), QPoint(w, -w),
        QPoint(-w, 0),               QPoint(w, 0),
        QPoint(-w, w),  QPoint(0, w),  QPoint(w, w)
    };
    for (const QPoint &off : offsets)
        painter.drawText(r.translated(off), flags, txt);

    // 最后绘制文字本身
    painter.setPen(QPen(fill));
    painter.drawText(r, flags, txt);
}
