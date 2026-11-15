/**
 * @file   ThreadTipDialog.cpp
 * @brief  帖子打赏弹窗实现
 * @author BlockBox Team
 * @date   2026-11-15
 */
#include "ThreadTipDialog.h"

#include "ForumKit.h"
#include "NotificationManager.h"
#include "utils/ForumAccountService.h"
#include "utils/IconHelper.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShowEvent>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <cmath>

namespace {

const char kCardName[] = "threadTipCard";
constexpr int kMinAmount = 2;
constexpr int kMaxAmount = 10;

/**
 * 打赏成功后的小方块飘升动画层：盖在整个弹窗上自绘 N 个立体小方块，
 * 从确认按钮处错峰起跳，飘出卡片顶部（飞向「推流」），渐隐后由调用方关弹窗。
 */
class CubeFlightOverlay : public QWidget
{
public:
    explicit CubeFlightOverlay(QWidget *parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("cubeFlightOverlay"));
    }

    void configure(const QPointF &start, const QPointF &end, int cubes, const QColor &base)
    {
        m_start = start;
        m_end = end;
        m_cubes = cubes;
        m_base = base;
        // 每个方块的水平散布量用固定种子的伪随机数生成，重绘时飞行路径不抖
        quint32 seed = 20260901u;
        m_jitter.clear();
        for (int i = 0; i < cubes; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const qreal unit = ((seed >> 8) % 1000u) / 999.0;
            m_jitter << (unit - 0.5) * 72.0;
        }
    }

    void setProgress(qreal t)
    {
        m_t = t;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_t <= 0.0)
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < m_cubes; ++i)
        {
            const qreal delay = 0.06 + i * 0.09;
            const qreal dur = 0.6;
            qreal p = (m_t - delay) / dur;
            if (p <= 0.0)
                continue;
            p = qMin(p, 1.0);
            const qreal eased = 1.0 - std::pow(1.0 - p, 3.0);
            const QPointF to = m_end + QPointF(m_jitter.at(i), (i % 3) * 6.0 - 6.0);
            QPointF pos = m_start + (to - m_start) * eased;
            pos.rx() += std::sin((eased * 1.6 + i * 0.7) * 3.1416) * 7.0 * (1.0 - eased);

            qreal alpha = 1.0;
            if (p < 0.15)
                alpha = p / 0.15;
            else if (p > 0.72)
                alpha = qMax(0.0, (1.0 - p) / 0.28);

            // 越飘越远越小，起点稍大一点更有「抓起一把方块」的感觉
            const int size = int(26.0 * (1.0 - 0.4 * eased)) + 6;
            drawCube(painter, pos, size, alpha);
        }
    }

private:
    /** 以 middle 为三面交汇的中间棱点，画一个等距立体小方块 */
    void drawCube(QPainter &painter, const QPointF &middle, int size, qreal alpha)
    {
        const qreal w = size / 2.0;
        const QPointF top(middle.x(), middle.y() - w);
        const QPointF right(middle.x() + w, middle.y() - w / 2.0);
        const QPointF left(middle.x() - w, middle.y() - w / 2.0);
        const QPointF bottom(middle.x(), middle.y() + w);
        const QPointF bottomLeft(middle.x() - w, middle.y() + w / 2.0);
        const QPointF bottomRight(middle.x() + w, middle.y() + w / 2.0);

        painter.save();
        painter.setOpacity(alpha);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_base.lighter(135));
        painter.drawPolygon(QPolygonF() << top << right << middle << left);
        painter.setBrush(m_base);
        painter.drawPolygon(QPolygonF() << left << middle << bottom << bottomLeft);
        painter.setBrush(m_base.darker(135));
        painter.drawPolygon(QPolygonF() << middle << right << bottomRight << bottom);
        painter.restore();
    }

    QPointF m_start;
    QPointF m_end;
    int m_cubes = 0;
    QColor m_base;
    qreal m_t = 0.0;
    QVector<qreal> m_jitter;
};

} // namespace

int ThreadTipDialog::tip(QWidget *parent, const ForumThreadInfo &info)
{
    if (!info.isValid())
        return 0;

    ThreadTipDialog dialog(info, parent);
    if (dialog.exec() != QDialog::Accepted)
        return 0;
    return dialog.m_amount;
}

ThreadTipDialog::ThreadTipDialog(const ForumThreadInfo &info, QWidget *parent)
    : AppDialogBase(parent)
    , m_info(info)
    , m_points(ForumAccountService::instance()->user().points)
{
    setObjectName(QStringLiteral("threadTipDialog"));
    setWindowTitle(tr("打赏这个帖子"));

    initUI();
    initStyle();

    // 余额以服务端为准再刷一次，失败就悄悄用缓存值
    QPointer<ThreadTipDialog> guard(this);
    ForumAccountService::instance()->getJson(
        QStringLiteral("/api/points"),
        [guard](const QJsonObject &root) {
            if (guard)
            {
                guard->m_points = root.value(QStringLiteral("points")).toInt();
                guard->updateBalanceView();
            }
        },
        {});
}

// ───────────────────────────── UI ─────────────────────────────

void ThreadTipDialog::initUI()
{
    m_card = new QWidget(this);
    m_card->setObjectName(QString::fromLatin1(kCardName));
    m_card->setMinimumSize(420, 430);
    m_card->setMaximumSize(480, 500);

    QVBoxLayout *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(22, 18, 22, 18);
    cardLayout->setSpacing(10);

    // ── 标题栏 ──
    QWidget *header = new QWidget(m_card);
    QHBoxLayout *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(2, 2, 2, 2);
    headerRow->setSpacing(10);

    QLabel *icon = new QLabel(header);
    icon->setObjectName(QStringLiteral("tipHeaderIcon"));
    icon->setFixedSize(32, 32);
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_StyledBackground, true);
    icon->setPixmap(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/tip.svg"),
                                                ThemeManager::instance()->currentThemeColor(),
                                                16).pixmap(16, 16));
    headerRow->addWidget(icon);

    QLabel *title = new QLabel(tr("打赏这个帖子"), header);
    title->setObjectName(QStringLiteral("tipTitle"));
    headerRow->addWidget(title, 1);
    cardLayout->addWidget(header);

    // ── 帖子预览：作者 + 标题 ──
    auto *threadRow = new ForumKit::ElidedLabel(m_card);
    threadRow->setObjectName(QStringLiteral("tipThreadRow"));
    threadRow->setFullText(tr("给 @%1 的帖子「%2」打赏")
                               .arg(m_info.authorName.isEmpty()
                                        ? QStringLiteral("作者")
                                        : m_info.authorName,
                                    m_info.title));
    cardLayout->addWidget(threadRow);

    // ── 数量步进器 ──
    auto *amountHead = new QLabel(tr("打赏数量"), m_card);
    amountHead->setObjectName(QStringLiteral("tipSectionHead"));
    cardLayout->addWidget(amountHead);

    QWidget *stepper = new QWidget(m_card);
    QHBoxLayout *stepperRow = new QHBoxLayout(stepper);
    stepperRow->setContentsMargins(0, 2, 0, 2);
    stepperRow->setSpacing(14);

    m_minusButton = new QPushButton(QStringLiteral("−"), stepper);
    m_minusButton->setObjectName(QStringLiteral("tipStepButton"));
    m_minusButton->setCursor(Qt::PointingHandCursor);
    m_minusButton->setFixedSize(34, 34);
    stepperRow->addStretch();
    stepperRow->addWidget(m_minusButton);

    m_amountLabel = new QLabel(stepper);
    m_amountLabel->setObjectName(QStringLiteral("tipAmount"));
    m_amountLabel->setAlignment(Qt::AlignCenter);
    m_amountLabel->setFixedWidth(110);
    stepperRow->addWidget(m_amountLabel);

    m_plusButton = new QPushButton(QStringLiteral("+"), stepper);
    m_plusButton->setObjectName(QStringLiteral("tipStepButton"));
    m_plusButton->setCursor(Qt::PointingHandCursor);
    m_plusButton->setFixedSize(34, 34);
    stepperRow->addWidget(m_plusButton);
    stepperRow->addStretch();
    cardLayout->addWidget(stepper);

    m_helperLabel = new QLabel(tr("最少 %1 个 · 最多 %2 个").arg(kMinAmount).arg(kMaxAmount), m_card);
    m_helperLabel->setObjectName(QStringLiteral("tipHelper"));
    m_helperLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_helperLabel);

    // ── 余额 ──
    m_balanceLabel = new QLabel(m_card);
    m_balanceLabel->setObjectName(QStringLiteral("tipBalance"));
    m_balanceLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_balanceLabel);
    updateBalanceView();

    // ── 底部按钮 ──
    m_confirmButton = new QPushButton(m_card);
    m_confirmButton->setObjectName(QStringLiteral("primaryButton"));
    m_confirmButton->setCursor(Qt::PointingHandCursor);
    connect(m_confirmButton, &QPushButton::clicked, this, &ThreadTipDialog::confirmTip);
    cardLayout->addWidget(m_confirmButton);

    auto *note = new QLabel(tr("打赏不会转给作者，但会提升这个帖子的推流"), m_card);
    note->setObjectName(QStringLiteral("tipNote"));
    note->setWordWrap(true);
    note->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(note);

    connect(m_minusButton, &QPushButton::clicked, this, [this]() {
        m_amount = qMax(kMinAmount, m_amount - 1);
        updateAmountViews();
    });
    connect(m_plusButton, &QPushButton::clicked, this, [this]() {
        m_amount = qMin(kMaxAmount, m_amount + 1);
        updateAmountViews();
    });
    updateAmountViews();

    QGridLayout *main = new QGridLayout(this);
    main->setContentsMargins(18, 18, 18, 18);
    main->addWidget(m_card, 0, 0);
}

void ThreadTipDialog::initStyle()
{
    ThemeManager *tm = ThemeManager::instance();
    const QString themeColor = tm->currentThemeColor();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);

    const QString cardBg     = isLight ? "rgba(255, 255, 255, 248)" : "rgba(44, 44, 48, 248)";
    const QString cardBorder = isLight ? "rgba(210, 210, 210, 230)" : "rgba(90, 90, 96, 230)";
    const QString titleColor = isLight ? "#1a1a1a" : "#f2f2f2";
    const QString textColor  = isLight ? "#333333" : "#e8e8e8";
    const QString subColor   = isLight ? "#64748B" : "#9aa3b2";
    const QString muted      = isLight ? "#94A3B8" : "#7a8290";
    const QString fieldBg    = isLight ? "#f4f4f4" : "#3b3b40";
    const QString fieldBorder= isLight ? "#d4d4d4" : "#55555a";
    const QString hoverBg    = isLight ? "rgba(15,23,42,0.08)" : "rgba(255,255,255,0.08)";

    const QString style = QString(
        "QDialog { background-color: transparent; border: none; }"
        "QWidget#%1 {"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 18px;"
        "}"
        "QLabel#tipTitle { color: %4; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#tipHeaderIcon { background: %5; border-radius: 9px; }"
        "QLabel#tipThreadRow {"
        "    color: %6; font-size: 12px; background: %7;"
        "    border: 1px solid %8; border-radius: 10px; padding: 8px 10px;"
        "}"
        "QLabel#tipSectionHead { color: %9; font-size: 12.5px; font-weight: 600; background: transparent; }"
        "QPushButton#tipStepButton {"
        "    background: %7; border: 1px solid %8; border-radius: 17px;"
        "    color: %4; font-size: 16px; font-weight: 700;"
        "}"
        "QPushButton#tipStepButton:hover { background: %10; }"
        "QPushButton#tipStepButton:disabled { color: %11; }"
        "QLabel#tipAmount { color: %4; font-size: 20px; font-weight: 700; background: transparent; }"
        "QLabel#tipHelper { color: %11; font-size: 11.5px; background: transparent; }"
        "QLabel#tipBalance { color: %6; font-size: 12.5px; background: transparent; }"
        "QLabel#tipNote { color: %11; font-size: 11.5px; background: transparent; }"
    ).arg(QString::fromLatin1(kCardName), cardBg, cardBorder, titleColor, themeColor,
          subColor, fieldBg, fieldBorder, textColor, hoverBg, muted);

    setStyleSheet(style);
}

void ThreadTipDialog::showEvent(QShowEvent *event)
{
    AppDialogBase::showEvent(event);
    setWindowOpacity(0.0);
    auto *anim = new QPropertyAnimation(this, "windowOpacity");
    anim->setDuration(160);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ThreadTipDialog::mousePressEvent(QMouseEvent *event)
{
    // 请求发出后到动画播完前不让点遮罩关闭，避免「扣了方块却错过反馈」
    if (m_tipping)
    {
        event->accept();
        return;
    }
    AppDialogBase::mousePressEvent(event);
}

// ───────────────────────────── 状态 ─────────────────────────────

void ThreadTipDialog::updateAmountViews()
{
    m_amountLabel->setText(tr("%1 方块").arg(m_amount));
    m_minusButton->setEnabled(m_amount > kMinAmount);
    m_plusButton->setEnabled(m_amount < kMaxAmount);
    m_confirmButton->setText(tr("打赏 %1 方块").arg(m_amount));
}

void ThreadTipDialog::updateBalanceView()
{
    m_balanceLabel->setText(tr("我的余额：%1 方块").arg(m_points));
}

// ───────────────────────────── 打赏 ─────────────────────────────

void ThreadTipDialog::confirmTip()
{
    if (m_tipping)
        return;

    if (m_points >= 0 && m_amount > m_points)
    {
        NotificationManager::showError(this, tr("方块余额不足，还差 %1 个").arg(m_amount - m_points));
        return;
    }

    m_tipping = true;
    m_confirmButton->setEnabled(false);
    m_minusButton->setEnabled(false);
    m_plusButton->setEnabled(false);
    m_confirmButton->setText(tr("正在打赏…"));

    QPointer<ThreadTipDialog> guard(this);
    ForumContentService::instance()->tipThread(
        m_info.id, m_amount,
        [guard](int points) {
            if (!guard)
                return;
            // 服务端回了新余额就记下来，别覆盖成「不知道」的 -1
            if (points >= 0)
                guard->m_points = points;
            guard->playCubeFlight();
        },
        [guard](const QString &message, int) {
            if (!guard)
                return;
            guard->m_tipping = false;
            guard->m_confirmButton->setEnabled(true);
            guard->m_minusButton->setEnabled(guard->m_amount > kMinAmount);
            guard->m_plusButton->setEnabled(guard->m_amount < kMaxAmount);
            guard->updateAmountViews();
            NotificationManager::showError(guard.data(), message);
        });
}

void ThreadTipDialog::playCubeFlight()
{
    auto *overlay = new CubeFlightOverlay(this);
    overlay->setGeometry(rect());
    overlay->raise();
    const QPointF start = m_confirmButton->mapTo(this, QPoint(m_confirmButton->width() / 2,
                                                              m_confirmButton->height() / 2));
    const QPointF end = m_card->mapTo(this, QPoint(m_card->width() / 2, -24));
    overlay->configure(start, end, m_amount,
                       QColor(StyleKit::resolve(QStringLiteral("@PRIMARY@"))));

    auto *anim = new QVariantAnimation(this);
    anim->setDuration(1150);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    connect(anim, &QVariantAnimation::valueChanged, overlay,
            [overlay](const QVariant &value) { overlay->setProgress(value.toReal()); });
    connect(anim, &QVariantAnimation::finished, this, [this]() { accept(); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
