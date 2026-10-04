#include "CustomCheckBox.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>

#include "utils/ThemeManager.h"

static const int TOGGLE_WIDTH = 44;
static const int TOGGLE_HEIGHT = 24;
static const int KNOB_SIZE = 20;
static const int KNOB_MARGIN = 2;
static const int ANIMATION_DURATION = 250;

CustomCheckBox::CustomCheckBox(const QString &text, QWidget *parent)
    : QWidget(parent)
    , m_checked(false)
    , m_hovered(false)
    , m_pressed(false)
    , m_knobPosition(0.0)
    , m_animation(nullptr)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(8);

    m_checkBox = new QWidget(this);
    m_checkBox->setFixedSize(TOGGLE_WIDTH, TOGGLE_HEIGHT);
    m_checkBox->setCursor(Qt::PointingHandCursor);
    m_checkBox->setAttribute(Qt::WA_TranslucentBackground);
    m_checkBox->setAutoFillBackground(false);
    m_checkBox->setStyleSheet("background: transparent;");
    m_layout->addWidget(m_checkBox);

    m_textLabel = new QLabel(text, this);
    m_textLabel->setCursor(Qt::PointingHandCursor);
    m_textLabel->setStyleSheet("font-size: 10pt; background: transparent;");
    m_layout->addWidget(m_textLabel);

    m_layout->addStretch();

    setCursor(Qt::PointingHandCursor);
    updateThemeStyle();
}

CustomCheckBox::~CustomCheckBox()
{
}

bool CustomCheckBox::isChecked() const
{
    return m_checked;
}

void CustomCheckBox::setChecked(bool checked)
{
    if (m_checked != checked)
    {
        m_checked = checked;

        if (!m_animation)
        {
            m_animation = new QPropertyAnimation(this, "knobPosition", this);
            m_animation->setDuration(ANIMATION_DURATION);
            m_animation->setEasingCurve(QEasingCurve::InOutCubic);
        }

        m_animation->stop();
        m_animation->setStartValue(m_knobPosition);
        m_animation->setEndValue(checked ? 1.0 : 0.0);
        m_animation->start();

        emit toggled(m_checked);
    }
}

QString CustomCheckBox::text() const
{
    return m_textLabel->text();
}

void CustomCheckBox::setText(const QString &text)
{
    m_textLabel->setText(text);
}

qreal CustomCheckBox::knobPosition() const
{
    return m_knobPosition;
}

void CustomCheckBox::setKnobPosition(qreal pos)
{
    m_knobPosition = pos;
    update();
}

void CustomCheckBox::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void CustomCheckBox::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressed)
    {
        m_pressed = false;
        if (rect().contains(event->pos()))
        {
            bool targetChecked = !m_checked;
            m_checked = targetChecked;

            if (!m_animation)
            {
                m_animation = new QPropertyAnimation(this, "knobPosition", this);
                m_animation->setDuration(ANIMATION_DURATION);
                m_animation->setEasingCurve(QEasingCurve::InOutCubic);
            }

            m_animation->stop();
            m_animation->setStartValue(m_knobPosition);
            m_animation->setEndValue(targetChecked ? 1.0 : 0.0);
            m_animation->start();

            emit toggled(m_checked);
        }
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void CustomCheckBox::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect trackRect(m_checkBox->geometry());

    ThemeManager *tm = ThemeManager::instance();
    ThemeManager::ThemeType currentTheme = tm->currentTheme();
    QColor themeCol(tm->currentThemeColor());

    // ── Track ──
    QPainterPath trackPath;
    trackPath.addRoundedRect(QRectF(trackRect), TOGGLE_HEIGHT / 2, TOGGLE_HEIGHT / 2);

    if (m_knobPosition > 0.01)
    {
        QColor bg = (currentTheme == ThemeManager::DarkTheme) ? QColor("#444") : QColor("#e0e0e0");
        int r = static_cast<int>(bg.red() + (themeCol.red() - bg.red()) * m_knobPosition);
        int g = static_cast<int>(bg.green() + (themeCol.green() - bg.green()) * m_knobPosition);
        int b = static_cast<int>(bg.blue() + (themeCol.blue() - bg.blue()) * m_knobPosition);
        QColor trackColor(r, g, b);
        painter.setBrush(trackColor);
        painter.setPen(Qt::NoPen);
    }
    else
    {
        QColor bg = (currentTheme == ThemeManager::DarkTheme) ? QColor("#444") : QColor("#e0e0e0");
        // Slightly lighter track on hover
        if (m_hovered)
        {
            bg = bg.lighter(120);
        }
        painter.setBrush(bg);
        painter.setPen(Qt::NoPen);
    }
    painter.drawPath(trackPath);

    // ── Knob shadow ──
    qreal knobX = trackRect.left() + KNOB_MARGIN
        + (trackRect.width() - 2 * KNOB_MARGIN - KNOB_SIZE) * m_knobPosition;
    int knobXi = static_cast<int>(knobX);
    int knobY = trackRect.center().y() - KNOB_SIZE / 2;

    QPainterPath shadowPath;
    int shadowOffset = m_pressed ? 1 : 2;
    shadowPath.addEllipse(knobXi, knobY + 1, KNOB_SIZE, KNOB_SIZE);
    painter.setBrush(QColor(0, 0, 0, 30));
    painter.setPen(Qt::NoPen);
    painter.drawPath(shadowPath);

    // ── Knob ──
    QPainterPath knobPath;
    knobPath.addEllipse(knobXi, knobY, KNOB_SIZE, KNOB_SIZE);

    // Pressed effect: slightly smaller knob
    if (m_pressed)
    {
        QPainterPath pressedPath;
        int offset = 2;
        pressedPath.addEllipse(knobXi + offset, knobY + offset,
                               KNOB_SIZE - offset * 2, KNOB_SIZE - offset * 2);
        painter.setBrush(QColor(themeCol).lighter(170));
        painter.setPen(Qt::NoPen);
        painter.drawPath(pressedPath);
    }

    painter.setBrush(Qt::white);
    painter.setPen(Qt::NoPen);
    painter.drawPath(knobPath);

    // ── Text ──
    QColor textColor;
    if (currentTheme == ThemeManager::DarkTheme)
    {
        textColor = m_hovered ? QColor(themeCol) : QColor(200, 200, 200);
    }
    else
    {
        textColor = m_hovered ? QColor(themeCol) : QColor(51, 51, 51);
    }
    m_textLabel->setStyleSheet(QString("font-size: 10pt; color: %1; background: transparent;").arg(textColor.name()));
}

void CustomCheckBox::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    m_hovered = true;
    update();
}

void CustomCheckBox::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hovered = false;
    update();
}

void CustomCheckBox::updateThemeStyle()
{
    update();
}
