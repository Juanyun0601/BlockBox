#include "NotificationCard.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVBoxLayout>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

#include "platform.h"

NotificationCard::NotificationCard(const QString &text, Type type, int duration,
                                   const QString &actionText, QWidget *parent)
    : QWidget(parent)
    , m_text(text)
    , m_actionText(actionText)
    , m_type(type)
    , m_remainingSeconds(duration / 1000)
    , m_totalDuration(duration)
    , m_dismissed(false)
    , m_finished(false)
    , m_textLabel(nullptr)
    , m_copyButton(nullptr)
    , m_actionButton(nullptr)
    , m_countdownLabel(nullptr)
    , m_countdownTimer(nullptr)
    , m_hoverWatchdog(nullptr)
{
    setAttribute(Qt::WA_TranslucentBackground, true);

    // 先定字体再排版：initUI 里的文字宽度测量要和最终显示用同一套字体，
    // 否则长提示换行后的高度会估小，最后一行被裁掉
    setFont(QFont("Microsoft YaHei", 10));
    initUI();
    initStyle();

    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout, this, &NotificationCard::onCountdownTick);
    m_countdownTimer->start();

    // 悬停暂停的看门狗：触屏、卡片被重排移走、窗口被切走时 leave 都可能收不到，
    // 没有它倒计时会永久停住，卡片就再也不会消失
    m_hoverWatchdog = new QTimer(this);
    m_hoverWatchdog->setInterval(400);
    connect(m_hoverWatchdog, &QTimer::timeout, this, &NotificationCard::onHoverWatchdog);

    // 滑入动画交给 NotificationManager 在所有卡片排好位之后再启动，
    // 否则会和「堆叠重排」的动画同时抢这个卡片的 pos
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

    if (!m_actionText.isEmpty()) {
        m_actionButton = new QPushButton(m_actionText, this);
        m_actionButton->setCursor(Qt::PointingHandCursor);
        m_actionButton->setToolTip(m_actionText);
        connect(m_actionButton, &QPushButton::clicked, this, [this]() {
            emit actionClicked(this);
        });
    }

    m_countdownLabel = new QLabel(QString("%1s").arg(m_remainingSeconds), this);
    m_countdownLabel->setAlignment(Qt::AlignCenter);
    m_countdownLabel->setFixedWidth(28);
    m_countdownLabel->setCursor(Qt::PointingHandCursor);
    m_countdownLabel->setToolTip(tr("点击增加15秒"));
    m_countdownLabel->installEventFilter(this);

    if (m_actionButton)
        rightLayout->addWidget(m_actionButton, 0, Qt::AlignVCenter);
    rightLayout->addWidget(m_copyButton, 0, Qt::AlignVCenter);
    rightLayout->addWidget(m_countdownLabel, 0, Qt::AlignVCenter);

    mainLayout->addWidget(m_textLabel, 1);
    mainLayout->addLayout(rightLayout);

    setLayout(mainLayout);

    adjustSize();
    // 按最长一行量文字宽度：wordWrap 打开后 QLabel::sizeHint 的宽度不可靠，
    // 直接用字体度量，长提示卡片才不会挤成一条窄条
    const QFontMetrics fm(m_textLabel->font());
    int contentWidth = 0;
    const QStringList lines = m_text.split(QLatin1Char('\n'));
    for (const QString &line : lines)
        contentWidth = qMax(contentWidth, fm.horizontalAdvance(line));
    contentWidth = qBound(120, contentWidth, 360);

    // 宽度按所在窗口收窄：小窗 / 手机竖屏里 420px 的卡片会顶出屏幕右边
    // 附加操作按钮（如「网络自检」）会占掉右侧约 66px，也要算进排版宽度
    const int actionExtra = m_actionButton ? 66 : 0;
    QWidget *host = parentWidget();
    const int hostWidth = host ? host->width() : 0;
    const int maxWidth = hostWidth > 0 ? qBound(160, hostWidth - 32, 420) : 420;
    const int w = qMin(maxWidth, qMax(160, contentWidth + 82 + actionExtra));
    setFixedWidth(w);
    m_textLabel->setMaximumWidth(qMax(100, w - 82 - actionExtra));
    // 固定宽度后长提示会换行，高度要按换行后的排版重算，否则最后一行被裁掉
    adjustSize();
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

    if (m_actionButton) {
        m_actionButton->setStyleSheet(
            "QPushButton {"
            "    background: transparent;"
            "    border: 1px solid palette(mid);"
            "    border-radius: 9px;"
            "    padding: 1px 8px;"
            "    font-size: 11px;"
            "    color: palette(text);"
            "}"
            "QPushButton:hover {"
            "    background-color: rgba(128,128,128,0.15);"
            "}"
        );
    }

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

    // 先停掉重排时可能已经给这张卡挂上的位置动画，滑入才是唯一动作者
    stopPosAnimations();

    // 与 NotificationManager::repositionAll 的落点保持一致：
    // 桌面左下角，手机水平居中（左下角是侧边导航）
    const bool mobile = (Platform::getFormFactor() == Platform::FormFactor::Mobile);
    int endX = mobile ? (p->width() - width()) / 2 : 16;
    // 兜底：窗口比卡片还小时把卡片拉回窗口内，别让它露在窗口外面
    endX = qBound(8, endX, qMax(8, p->width() - width() - 8));
    int targetY = qBound(8, p->height() - height() - 16, qMax(8, p->height() - height() - 8));

    int startX = mobile ? endX : -width();
    int startY = mobile ? p->height() : targetY; // 手机端从底部滑入

    move(startX, startY);
    show();
    raise();

    QPropertyAnimation *anim = new QPropertyAnimation(this, "pos", this);
    anim->setDuration(350);
    anim->setStartValue(QPoint(startX, startY));
    anim->setEndValue(QPoint(endX, targetY));
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void NotificationCard::stopPosAnimations()
{
    const QList<QPropertyAnimation *> anims = findChildren<QPropertyAnimation *>();
    for (QPropertyAnimation *anim : anims)
        anim->stop();
}

void NotificationCard::dismiss()
{
    if (m_dismissed) return;
    m_dismissed = true;

    m_countdownTimer->stop();
    m_hoverWatchdog->stop();

    // 退出方向与滑入方向对称：桌面滑出左边缘，手机滑出底边缘
    QWidget *p = parentWidget();
    const bool mobile = (Platform::getFormFactor() == Platform::FormFactor::Mobile);
    const QPoint exitPos = mobile ? QPoint(pos().x(), p ? p->height() + 8 : pos().y() + height() + 8)
                                  : QPoint(-width(), pos().y());

    QPropertyAnimation *anim = new QPropertyAnimation(this, "pos", this);
    anim->setDuration(250);
    anim->setStartValue(pos());
    anim->setEndValue(exitPos);
    anim->setEasingCurve(QEasingCurve::InCubic);

    connect(anim, &QPropertyAnimation::finished, this, &NotificationCard::finishDismiss);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    // 兜底：滑出动画万一被外部停掉（重排 / 窗口缩放等），finished 就不再发出，
    // 卡片会永远显示「0s」停在屏幕上。到点直接收尾，宁可少一段动画也不留残影
    QTimer::singleShot(600, this, &NotificationCard::finishDismiss);
}

void NotificationCard::finishDismiss()
{
    if (m_finished) return;
    m_finished = true;

    // 可能还有没跑完的滑出 / 重排动画，先停掉再隐藏销毁
    stopPosAnimations();
    emit dismissRequested(this);
    hide();
    deleteLater();
}

void NotificationCard::extendTime(int seconds)
{
    if (m_dismissed) return;

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
#ifndef Q_OS_ANDROID
    // 悬停暂停倒计时（鼠标交互）。触屏设备没有悬停，enter/leave 事件
    // 不可靠：leave 收不到会让倒计时永久停住，通知卡成"残影"不消失。
    if (m_dismissed || !m_countdownTimer->isActive())
        return;
    m_countdownTimer->stop();
    m_hoverWatchdog->start();
#endif
}

void NotificationCard::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
#ifndef Q_OS_ANDROID
    m_hoverWatchdog->stop();
    resumeCountdownIfNeeded();
#endif
}

void NotificationCard::onHoverWatchdog()
{
    if (m_dismissed || !pointerOnCard()) {
        m_hoverWatchdog->stop();
        resumeCountdownIfNeeded();
    }
}

void NotificationCard::resumeCountdownIfNeeded()
{
    if (!m_dismissed && m_remainingSeconds > 0 && !m_countdownTimer->isActive())
        m_countdownTimer->start();
}

bool NotificationCard::pointerOnCard() const
{
#ifdef Q_OS_ANDROID
    return false;
#else
    if (!isVisible())
        return false;
    // 窗口不在前台（切到桌面 / 别的软件）就别再因为指针恰好压在卡片上而暂停，
    // 否则回到软件时会看到一个永远不动的提示卡
    QWidget *top = window();
    if (top && !top->isActiveWindow())
        return false;
    return rect().contains(mapFromGlobal(QCursor::pos()));
#endif
}
