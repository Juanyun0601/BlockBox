/**
 * @file   QuizReminder.cpp
 * @brief  右下角入站答题提醒小窗实现
 * @author BlockBox Team
 * @date   2026-10-20
 */

#include "QuizReminder.h"

#include <QColor>
#include <QDialog>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QVBoxLayout>

#include "NotificationManager.h"
#include "QuizDialog.h"
#include "utils/QuizBank.h"
#include "utils/ThemeManager.h"

namespace {

/// 把主题色（#RRGGBB）转成带透明度的 rgba()，用于半透明底
QString rgbaOf(const QString &hex, int alpha)
{
    QColor color(hex);
    if (!color.isValid())
        color = QColor(0x4c, 0x8d, 0xff);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha);
}

} // namespace

QuizReminder::QuizReminder(QWidget *host)
    : QWidget(host)
    , m_host(host)
{
    setObjectName(QStringLiteral("blockQuizReminder"));
    // 作为主窗口内的浮层：不独立成窗、不抢焦点，点击直接落到按钮上
    setAttribute(Qt::WA_StyledBackground, true);

    initUI();

    if (m_host)
        m_host->installEventFilter(this);
}

void QuizReminder::initUI()
{
    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    const QString accent = tm->currentThemeColor();
    const QString textColor = isLight ? QStringLiteral("#4a4a4a") : QStringLiteral("#d8d8d8");
    const QString subColor = isLight ? QStringLiteral("#7a7a7a") : QStringLiteral("#9a9a9a");
    const QString cardBg = isLight ? QStringLiteral("rgba(255, 255, 255, 248)")
                                   : QStringLiteral("rgba(44, 44, 48, 248)");
    const QString cardBorder = isLight ? QStringLiteral("rgba(208, 208, 208, 235)")
                                       : QStringLiteral("rgba(96, 96, 102, 235)");

    setStyleSheet(
        QStringLiteral("QWidget#blockQuizReminder { background-color: %1;"
                       " border: 1px solid %2; border-radius: 14px; }")
            .arg(cardBg, cardBorder));

    setFixedWidth(320);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    QLabel *titleLabel = new QLabel(tr("入站答题"), this);
    titleLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 14px; font-weight: 700;").arg(textColor));
    layout->addWidget(titleLabel);

    QLabel *detailLabel = new QLabel(
        tr("随机抽 %1 题、每题 %2 分，达到 %3 分即可通过。答错会显示正确答案。")
            .arg(QuizBank::kExamQuestionCount)
            .arg(QuizBank::kPointsPerQuestion)
            .arg(QuizBank::kPassScore),
        this);
    detailLabel->setWordWrap(true);
    detailLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;").arg(subColor));
    layout->addWidget(detailLabel);

    QHBoxLayout *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 4, 0, 0);
    buttonRow->setSpacing(8);
    buttonRow->addStretch(1);

    m_skipButton = new QPushButton(tr("跳过"), this);
    m_skipButton->setCursor(Qt::PointingHandCursor);
    m_skipButton->setMinimumHeight(30);
    m_skipButton->setStyleSheet(
        QStringLiteral("QPushButton { background-color: transparent; color: %1;"
                       " border: none; border-radius: 8px; padding: 5px 14px;"
                       " font-size: 12px; }"
                       "QPushButton:hover { background-color: %2; }")
            .arg(subColor, rgbaOf(accent, 28)));
    connect(m_skipButton, &QPushButton::clicked, this, &QuizReminder::onSkipClicked);
    buttonRow->addWidget(m_skipButton);

    m_startButton = new QPushButton(tr("开始答题"), this);
    m_startButton->setCursor(Qt::PointingHandCursor);
    m_startButton->setMinimumHeight(30);
    m_startButton->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: #ffffff; border: none;"
                       " border-radius: 8px; padding: 5px 16px; font-size: 12px;"
                       " font-weight: 600; }"
                       "QPushButton:hover { background-color: %2; }")
            .arg(accent, rgbaOf(accent, 220)));
    connect(m_startButton, &QPushButton::clicked, this, &QuizReminder::onStartClicked);
    buttonRow->addWidget(m_startButton);

    layout->addLayout(buttonRow);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(1.0);
    setGraphicsEffect(m_opacityEffect);
}

void QuizReminder::showReminder()
{
    if (QuizBank::hasPassed())
    {
        hide();
        return;
    }

    adjustSize();
    placeAtHostCorner();
    show();
    raise();

    if (!m_fadePlayed)
    {
        m_fadePlayed = true;
        QPropertyAnimation *fade = new QPropertyAnimation(m_opacityEffect, "opacity", this);
        fade->setDuration(180);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

bool QuizReminder::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host && m_host)
    {
        if (event->type() == QEvent::Resize && isVisible())
            placeAtHostCorner();
    }
    return QWidget::eventFilter(watched, event);
}

void QuizReminder::placeAtHostCorner()
{
    if (!m_host)
        return;

    const int margin = 24;
    const int x = qMax(0, m_host->width() - width() - margin);
    const int y = qMax(0, m_host->height() - height() - margin);
    move(x, y);
}

void QuizReminder::onStartClicked()
{
    // 先收起提醒，避免浮层压在答题弹窗下面
    hide();

    QWidget *host = window();
    QuizDialog dialog(host);
    const bool accepted = (dialog.exec() == QDialog::Accepted);

    if (accepted || QuizBank::hasPassed())
    {
        if (host)
        {
            NotificationManager::showSuccess(
                host, tr("已通过入站答题，分数 %1 分。").arg(QuizBank::lastScore()), 6000);
        }
        deleteLater();
        return;
    }

    if (!m_skipped)
        showReminder();
}

void QuizReminder::onSkipClicked()
{
    m_skipped = true;
    hide();
}
