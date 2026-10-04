#include "NotificationCard.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVBoxLayout>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

NotificationCard::NotificationCard(const QString &text, Type type, int duration, QWidget *parent)
    : QWidget(parent)
    , m_text(text)
    , m_type(type)
    , m_remainingSeconds(duration / 1000)
    , m_totalDuration(duration)
    , m_dismissed(false)
    , m_textLabel(nullptr)
    , m_copyButton(nullptr)
    , m_countdownLabel(nullptr)
    , m_countdownTimer(nullptr)
{
    setAttribute(Qt::WA_TranslucentBackground, true);

    initUI();
    initStyle();

    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout, this, &NotificationCard::onCountdownTick);
    m_countdownTimer->start();

    startSlideIn();
}

NotificationCard::~NotificationCard()
{
}

void NotificationCard::initUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(16, 6, 8, 6);
    mainLayout->setSpacing(8);

    m_textLabel = new QLabel(m_text, this);
    m_textLabel->setWordWrap(true);
    m_textLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_textLabel->setMinimumWidth(100);
    m_textLabel->setMaximumWidth(360);

    QHBoxLayout *rightLayout = new QHBoxLayout;
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);

    m_copyButton = new QPushButton(this);
    m_copyButton->setFixedSize(18, 18);
    m_copyButton->setCursor(Qt::PointingHandCursor);
    m_copyButton->setToolTip(tr("复制"));
    connect(m_copyButton, &QPushButton::clicked, this, &NotificationCard::onCopyClicked);

    QString themeColor = ThemeManager::instance()->currentThemeColor();
    m_copyButton->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/copy.svg", QColor(themeColor), 14));

    m_countdownLabel = new QLabel(QString("%1s").arg(m_remainingSeconds), this);
    m_countdownLabel->setAlignment(Qt::AlignCenter);
    m_countdownLabel->setFixedWidth(28);
    m_countdownLabel->setCursor(Qt::PointingHandCursor);
    m_countdownLabel->setToolTip(tr("点击增加15秒"));
    m_countdownLabel->installEventFilter(this);

    rightLayout->addWidget(m_copyButton, 0, Qt::AlignVCenter);
    rightLayout->addWidget(m_countdownLabel, 0, Qt::AlignVCenter);

    mainLayout->addWidget(m_textLabel, 1);
    mainLayout->addLayout(rightLayout);

    setLayout(mainLayout);

    adjustSize();
    int contentWidth = m_textLabel->sizeHint().width();
    int w = qMin(420, qMax(160, contentWidth + 80));
    setFixedWidth(w);
}

void NotificationCard::initStyle()
{
    setFont(QFont("Microsoft YaHei", 10));

    m_textLabel->setStyleSheet(
        "QLabel {"
        "    color: palette(text);"
        "    background: transparent;"
        "    border: none;"
        "    font-size: 13px;"
        "}"
    );

    m_copyButton->setStyleSheet(
        "QPushButton {"
        "    background: transparent;"
        "    border: none;"
        "    border-radius: 3px;"
        "    padding: 1px;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(128,128,128,0.15);"
        "}"
    );

    m_countdownLabel->setStyleSheet(
        "QLabel {"
        "    color: palette(text);"
        "    font-size: 11px;"
        "    font-weight: bold;"
        "    background: transparent;"
        "    border: none;"
        "}"
    );
}

void NotificationCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    ThemeManager *tm = ThemeManager::instance();
    bool isDark = (tm->currentTheme() == ThemeManager::DarkTheme);

    QColor accentColor, bgColor, borderColor, textColor;
    switch (m_type) {
    case Success:
        accentColor = QColor("#4CAF50");
        bgColor     = isDark ? QColor("#1b3d1b") : QColor("#e8f5e9");
        borderColor = isDark ? QColor("#2d5d2d") : QColor("#c8e6c9");
        textColor   = isDark ? QColor("#a5d6a7") : QColor("#2e7d32");
        break;
    case Info:
        accentColor = QColor(tm->currentInfoAccentColor());
        bgColor     = isDark ? QColor("#1a2d3d") : QColor(tm->getInfoColorLight());
        borderColor = isDark ? QColor("#2d3d5d") : QColor(tm->getInfoColorLightAccent());
        textColor   = isDark ? QColor(tm->getInfoColorMediumAccent()) : QColor(tm->getInfoColorPressed());
        break;
    case Error:
        accentColor = QColor("#F44336");
        bgColor     = isDark ? QColor("#3d1a1a") : QColor("#ffebee");
        borderColor = isDark ? QColor("#5d2d2d") : QColor("#ffcdd2");
        textColor   = isDark ? QColor("#ef9a9a") : QColor("#c62828");
        break;
    }

    QRect r = rect().adjusted(1, 1, -1, -1);
    int radius = 8;

    QPainterPath bodyPath;
    bodyPath.addRoundedRect(r, radius, radius);
    painter.fillPath(bodyPath, bgColor);

    painter.setPen(QPen(borderColor, 1));
    painter.drawPath(bodyPath);

    QPainterPath accentPath;
    accentPath.addRoundedRect(QRectF(r.left(), r.top(), 5, r.height()), 3, 3);
    QPainterPath clipPath;
    clipPath.addRoundedRect(r, radius, radius);
    accentPath = clipPath.intersected(accentPath);
    painter.fillPath(accentPath, accentColor);

    if (m_totalDuration > 0) {
        qreal totalSec = m_totalDuration / 1000.0;
        qreal progress = qMax(0.0, (qreal)m_remainingSeconds / totalSec);
        if (progress > 0.0) {
            QString themeColor = ThemeManager::instance()->currentThemeColor();
            qreal pw = r.width(), ph = r.height();
            qreal perimeter = 2.0 * (pw + ph) - 8.0 * radius + 2.0 * 3.14159265358979323846 * radius;
            qreal drawn = progress * perimeter;

            QPen ringPen(QColor(themeColor), 2);
            ringPen.setDashPattern({drawn, perimeter});
            painter.setPen(ringPen);
            painter.setBrush(Qt::NoBrush);

            QPainterPath ringPath;
            ringPath.addRoundedRect(r, radius, radius);
            painter.drawPath(ringPath);
        }
    }
}

void NotificationCard::startSlideIn()
{
    QWidget *p = parentWidget();
    if (!p) return;

    QWidget *win = window();

    int startX = -width();
    int endX = 16;
    int targetY = win->height() - height() - 16;

    move(startX, targetY);
    show();
    raise();

    QPropertyAnimation *anim = new QPropertyAnimation(this, "pos", this);
    anim->setDuration(350);
    anim->setStartValue(QPoint(startX, targetY));
    anim->setEndValue(QPoint(endX, targetY));
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void NotificationCard::dismiss()
{
    if (m_dismissed) return;
    m_dismissed = true;

    m_countdownTimer->stop();

    QPropertyAnimation *anim = new QPropertyAnimation(this, "pos", this);
    anim->setDuration(250);
    anim->setStartValue(pos());
    anim->setEndValue(QPoint(-width(), pos().y()));
    anim->setEasingCurve(QEasingCurve::InCubic);

    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        emit dismissRequested(this);
        hide();
        deleteLater();
    });

    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void NotificationCard::extendTime(int seconds)
{
    int newRemaining = m_remainingSeconds + seconds;
    if (newRemaining > 30)
        newRemaining = 30;
    m_remainingSeconds = newRemaining;
    m_countdownLabel->setText(QString("%1s").arg(m_remainingSeconds));

    if (!m_countdownTimer->isActive())
        m_countdownTimer->start();

    emit timeExtended(this);
}

void NotificationCard::onCountdownTick()
{
    m_remainingSeconds--;
    if (m_remainingSeconds <= 0) {
        m_remainingSeconds = 0;
        m_countdownLabel->setText("0s");
        dismiss();
    } else {
        m_countdownLabel->setText(QString("%1s").arg(m_remainingSeconds));
    }
}

void NotificationCard::onCopyClicked()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(m_text);

    QColor flashColor;
    switch (m_type) {
    case Success: flashColor = QColor("#4CAF50"); break;
    case Info:    flashColor = QColor(ThemeManager::instance()->currentInfoAccentColor()); break;
    case Error:   flashColor = QColor("#F44336"); break;
    }
    m_copyButton->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/copy.svg", flashColor, 16));

    QTimer::singleShot(600, this, [this]() {
        QString themeColor = ThemeManager::instance()->currentThemeColor();
        m_copyButton->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/copy.svg", QColor(themeColor), 16));
    });

    emit copyClicked(m_text);
}

bool NotificationCard::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_countdownLabel) {
        if (event->type() == QEvent::MouseButtonPress) {
            extendTime(15);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void NotificationCard::enterEvent(QEnterEvent *event)
#else
void NotificationCard::enterEvent(QEvent *event)
#endif
{
    QWidget::enterEvent(event);
    if (!m_dismissed && m_countdownTimer->isActive()) {
        m_countdownTimer->stop();
    }
}

void NotificationCard::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (!m_dismissed && m_remainingSeconds > 0 && !m_countdownTimer->isActive()) {
        m_countdownTimer->start();
    }
}
