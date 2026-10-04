#include "CustomRadioButton.h"

#include <QEnterEvent>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionButton>

#include "utils/ThemeManager.h"

static const int TOGGLE_WIDTH = 44;
static const int TOGGLE_HEIGHT = 24;
static const int KNOB_SIZE = 20;
static const int KNOB_MARGIN = 2;
static const int ANIMATION_DURATION = 280;

CustomRadioButton::CustomRadioButton(const QString &text, QWidget *parent)
    : QRadioButton(text, parent)
    , m_hovered(false)
    , m_pressed(false)
    , m_knobPosition(isChecked() ? 1.0 : 0.0)
    , m_animation(nullptr)
{
    setMinimumHeight(TOGGLE_HEIGHT + 4);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    connect(this, &QRadioButton::toggled, this, &CustomRadioButton::animateToggle);
}

CustomRadioButton::~CustomRadioButton()
{}

qreal CustomRadioButton::knobPosition() const
{
    return m_knobPosition;
}

void CustomRadioButton::setKnobPosition(qreal pos)
{
    m_knobPosition = pos;
    update();
}

void CustomRadioButton::animateToggle()
{
    if (!m_animation)
    {
        m_animation = new QPropertyAnimation(this, "knobPosition", this);
        m_animation->setDuration(ANIMATION_DURATION);
        m_animation->setEasingCurve(QEasingCurve::OutCubic);
    }

    m_animation->stop();
    m_animation->setStartValue(m_knobPosition);
    m_animation->setEndValue(isChecked() ? 1.0 : 0.0);
    m_animation->start();
}

void CustomRadioButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    ThemeManager *tm = ThemeManager::instance();
    ThemeManager::ThemeType currentTheme = tm->currentTheme();
    QString themeColorStr = tm->currentThemeColor();
    QColor themeCol(themeColorStr);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QStyleOptionButton opt;
    initStyleOption(&opt);
    QRect indicatorRect = style()->subElementRect(QStyle::SE_RadioButtonIndicator, &opt, this);

    QRect trackRect(indicatorRect.left(), indicatorRect.center().y() - TOGGLE_HEIGHT / 2,
                    TOGGLE_WIDTH, TOGGLE_HEIGHT);

    // ── Track background ──
    QPainterPath trackPath;
    trackPath.addRoundedRect(QRectF(trackRect), TOGGLE_HEIGHT / 2, TOGGLE_HEIGHT / 2);

    QColor trackColor;
    if (m_knobPosition > 0.01)
    {
        // 动态颜色插值：从默认背景到主题色
        QColor bg = (currentTheme == ThemeManager::DarkTheme) ? QColor("#444") : QColor("#e0e0e0");
        int r = static_cast<int>(bg.red() + (themeCol.red() - bg.red()) * m_knobPosition);
        int g = static_cast<int>(bg.green() + (themeCol.green() - bg.green()) * m_knobPosition);
        int b = static_cast<int>(bg.blue() + (themeCol.blue() - bg.blue()) * m_knobPosition);
        trackColor = QColor(r, g, b);
    }
    else
    {
        trackColor = (currentTheme == ThemeManager::DarkTheme) ? QColor("#444") : QColor("#e0e0e0");
        if (m_hovered)
            trackColor = trackColor.lighter(115);
    }
    painter.setBrush(trackColor);
    painter.setPen(Qt::NoPen);
    painter.drawPath(trackPath);

    // ── Knob position: checked = right, unchecked = left ──
    qreal knobX = trackRect.left() + KNOB_MARGIN
        + (trackRect.width() - 2 * KNOB_MARGIN - KNOB_SIZE) * m_knobPosition;
    int knobXi = static_cast<int>(knobX);
    int knobY = trackRect.center().y() - KNOB_SIZE / 2;

    // ── Knob shadow (dynamic based on state) ──
    int shadowAlpha = m_pressed ? 20 : 35;
    int shadowOffset = m_pressed ? 1 : 2;
    QPainterPath shadowPath;
    shadowPath.addEllipse(knobXi, knobY + shadowOffset, KNOB_SIZE, KNOB_SIZE);
    painter.setBrush(QColor(0, 0, 0, shadowAlpha));
    painter.setPen(Qt::NoPen);
    painter.drawPath(shadowPath);

    // ── Knob ──
    QPainterPath knobPath;
    knobPath.addEllipse(knobXi, knobY, KNOB_SIZE, KNOB_SIZE);

    // 按下效果：稍小的旋钮 + 颜色变化
    if (m_pressed)
    {
        int pressOffset = 2;
        QPainterPath pressedPath;
        pressedPath.addEllipse(knobXi + pressOffset, knobY + pressOffset,
                               KNOB_SIZE - pressOffset * 2, KNOB_SIZE - pressOffset * 2);
        painter.setBrush(QColor(themeCol).lighter(160));
        painter.setPen(Qt::NoPen);
        painter.drawPath(pressedPath);
    }

    painter.setBrush(Qt::white);
    painter.setPen(Qt::NoPen);
    painter.drawPath(knobPath);

    // ── Text inside knob (T/F or 开/关) ──
    QString checkText = getCheckText();
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(9);
    painter.setFont(font);
    painter.setPen(isChecked() ? Qt::white : QColor(themeColorStr));
    QRect knobRect(knobXi, knobY, KNOB_SIZE, KNOB_SIZE);
    painter.drawText(knobRect, Qt::AlignCenter, checkText);

    // ── Label text ──
    QRect textRect(trackRect.right() + 6, 0, width() - trackRect.right() - 6, height());
    QFont textFont = painter.font();
    textFont.setBold(false);
    textFont.setPointSize(10);
    painter.setFont(textFont);
    QColor textColor = (currentTheme == ThemeManager::DarkTheme) ? QColor("#ccc") : QColor("#333");
    if (m_hovered || hasFocus())
        textColor = QColor(themeColorStr);
    painter.setPen(textColor);
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text());
}

void CustomRadioButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_pressed = true;
        update();
    }
    QRadioButton::mousePressEvent(event);
}

void CustomRadioButton::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_pressed = false;
        update();
    }
    QRadioButton::mouseReleaseEvent(event);
}

void CustomRadioButton::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    m_hovered = true;
    setCursor(Qt::PointingHandCursor);
    update();
}

void CustomRadioButton::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hovered = false;
    update();
}

void CustomRadioButton::focusInEvent(QFocusEvent *event)
{
    Q_UNUSED(event);
    update();
}

void CustomRadioButton::focusOutEvent(QFocusEvent *event)
{
    Q_UNUSED(event);
    update();
}

QString CustomRadioButton::getCheckText() const
{
    QString language = QLocale::system().name();
    if (language.startsWith("zh")) {
        return isChecked() ? "开" : "关";
    } else {
        return isChecked() ? "T" : "F";
    }
}

void CustomRadioButton::updateThemeStyle()
{
    animateToggle();
    update();
}
