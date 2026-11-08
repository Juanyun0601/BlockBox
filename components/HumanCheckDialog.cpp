/**
 * @file   HumanCheckDialog.cpp
 * @brief  进社区前的滑块拼图人机验证弹窗
 * @author BlockBox Team
 * @date   2026-11-06
 */
#include "HumanCheckDialog.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "utils/ForumAccountService.h"
#include "utils/ThemeManager.h"

namespace {

const char kOkColor[] = "#2f9e44";
const char kBadColor[] = "#e5484d";

// 滑块条尺寸
constexpr int kSlideHeight = 44;
constexpr int kHandleWidth = 54;
constexpr int kHandleMargin = 4;

/// 把 data:image/png;base64,... 解成图片
QImage decodeDataUrl(const QString &url)
{
    const int comma = url.indexOf(QLatin1Char(','));
    if (comma < 0)
        return QImage();

    const QByteArray raw = QByteArray::fromBase64(url.mid(comma + 1).toLatin1());
    QImage image;
    if (!image.loadFromData(raw, "PNG"))
        return QImage();
    return image;
}

} // namespace

// ══════════════════════════════════════════════════════════════ 拼图预览
/**
 * @brief 验证图：底图 + 跟着滑块左右移动的拼图块
 */
class HumanPuzzleView : public QWidget
{
public:
    explicit HumanPuzzleView(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent, false);
    }

    void setChallenge(const QImage &bg, const QImage &piece, int top)
    {
        m_bg = bg;
        m_piece = piece;
        m_top = top;
        update();
    }

    void setOffset(int offset)
    {
        if (m_offset == offset)
            return;
        m_offset = offset;
        update();
    }

    void setEmptyColor(const QString &color)
    {
        m_emptyColor = QColor(color);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // 圆角裁切，跟卡片风格一致
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
        painter.setClipPath(clip);

        if (m_bg.isNull())
        {
            painter.fillRect(rect(), m_emptyColor.isValid() ? m_emptyColor
                                                            : QColor(0, 0, 0, 20));
            return;
        }

        painter.drawImage(rect(), m_bg);

        if (!m_piece.isNull())
            painter.drawImage(QPointF(m_offset, m_top), m_piece);
    }

private:
    QImage m_bg;
    QImage m_piece;
    int m_top = 0;
    int m_offset = 0;
    QColor m_emptyColor;
};

// ══════════════════════════════════════════════════════════════ 滑块条
/**
 * @brief 拖动条：拖到位置松手就交卷
 */
class HumanSlideBar : public QWidget
{
public:
    enum State
    {
        Idle,
        Verifying,
        Success,
        Failed
    };

    explicit HumanSlideBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }

    std::function<void(int value, int durationMs)> onReleased;
    /// 拖动过程中每挪一点就回调一次，让上面的拼图跟着一起走
    std::function<void(int value)> onMoved;

    void setColors(const QString &accent, const QString &track, const QString &hint)
    {
        m_accent = QColor(accent);
        m_track = QColor(track);
        m_hint = QColor(hint);
        update();
    }

    void setRange(int maxValue)
    {
        m_max = qMax(1, maxValue);
    }

    void setState(State state)
    {
        if (m_state == state)
            return;
        m_state = state;
        if (state == Success)
            m_handleX = maxHandleX();
        update();
    }

    void setLocked(bool locked)
    {
        m_locked = locked;
        if (locked)
            m_dragging = false;
        setCursor(locked ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }

    void reset()
    {
        m_state = Idle;
        m_locked = false;
        m_dragging = false;
        m_value = 0;
        m_handleX = kHandleMargin;
        m_timer.invalidate();
        if (onMoved)
            onMoved(0);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF track(0, 0, width(), height());
        const qreal radius = height() / 2.0;

        painter.setPen(Qt::NoPen);
        painter.setBrush(m_track);
        painter.drawRoundedRect(track, radius, radius);

        QColor active = m_accent;
        if (m_state == Success)
            active = QColor(kOkColor);
        else if (m_state == Failed)
            active = QColor(kBadColor);

        // 已经滑过去的那一段
        QColor fill = active;
        fill.setAlpha(56);
        painter.setBrush(fill);
        painter.drawRoundedRect(
            QRectF(0, 0, m_handleX + kHandleWidth / 2.0, height()), radius, radius);

        // 提示文字（拖起来之后就不挡视线了）
        if (m_state == Idle && !m_dragging)
        {
            painter.setPen(m_hint);
            painter.setFont(font());
            painter.drawText(track, Qt::AlignCenter, tr("向右拖动滑块"));
        }

        // 手柄
        const qreal handleHeight = height() - kHandleMargin * 2.0;
        const QRectF handle(m_handleX, kHandleMargin, kHandleWidth, handleHeight);
        painter.setBrush(active);
        painter.drawRoundedRect(handle, handleHeight / 2.0, handleHeight / 2.0);

        // 手柄上的图标：对勾 / 叉 / 双箭头
        const QPointF center = handle.center();
        QPen pen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        if (m_state == Success)
        {
            QPainterPath path;
            path.moveTo(center.x() - 6, center.y());
            path.lineTo(center.x() - 1, center.y() + 5);
            path.lineTo(center.x() + 7, center.y() - 5);
            painter.drawPath(path);
        }
        else if (m_state == Failed)
        {
            painter.drawLine(QPointF(center.x() - 5, center.y() - 5),
                             QPointF(center.x() + 5, center.y() + 5));
            painter.drawLine(QPointF(center.x() + 5, center.y() - 5),
                             QPointF(center.x() - 5, center.y() + 5));
        }
        else
        {
            for (int i = 0; i < 2; ++i)
            {
                const qreal dx = center.x() - 5 + i * 6;
                QPainterPath path;
                path.moveTo(dx - 2, center.y() - 5);
                path.lineTo(dx + 2, center.y());
                path.lineTo(dx - 2, center.y() + 5);
                painter.drawPath(path);
            }
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_locked || m_state == Verifying || m_state == Success)
        {
            event->ignore();
            return;
        }
        if (event->button() != Qt::LeftButton)
        {
            event->ignore();
            return;
        }

        if (m_state == Failed)
            reset();

        // 按在手柄上就按原来的抓点拖，按在别处就让手柄滑到手指下面
        const qreal hx = m_handleX;
        if (event->pos().x() >= hx && event->pos().x() <= hx + kHandleWidth)
            m_grab = event->pos().x() - hx;
        else
            m_grab = kHandleWidth / 2.0;

        m_dragging = true;
        m_timer.start();
        moveHandleTo(event->pos().x());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_dragging)
        {
            event->ignore();
            return;
        }
        moveHandleTo(event->pos().x());
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_dragging)
        {
            event->ignore();
            return;
        }

        m_dragging = false;
        event->accept();

        if (m_value <= 0)
        {
            reset();
            return;
        }

        m_state = Verifying;
        update();
        if (onReleased)
            onReleased(m_value, int(m_timer.elapsed()));
    }

private:
    qreal maxHandleX() const
    {
        return qMax<qreal>(kHandleMargin, width() - kHandleWidth - kHandleMargin);
    }

    void moveHandleTo(int cursorX)
    {
        const qreal handleX = qBound<qreal>(kHandleMargin, cursorX - m_grab, maxHandleX());
        const qreal span = maxHandleX() - kHandleMargin;
        const qreal progress = span <= 0 ? 0.0 : (handleX - kHandleMargin) / span;

        m_handleX = handleX;
        m_value = qRound(progress * m_max);
        if (onMoved)
            onMoved(m_value);
        update();
    }

    State m_state = Idle;
    QElapsedTimer m_timer;
    QColor m_accent = QColor(0x2f, 0x9e, 0x44);
    QColor m_track = QColor(0, 0, 0, 18);
    QColor m_hint = QColor(0x8a, 0x8a, 0x8a);
    qreal m_handleX = kHandleMargin;
    qreal m_grab = kHandleWidth / 2.0;
    int m_max = 1;
    int m_value = 0;
    bool m_dragging = false;
    bool m_locked = false;
};

// ══════════════════════════════════════════════════════════════ 弹窗
HumanCheckDialog::HumanCheckDialog(QWidget *parent)
    : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("blockHumanCheckDialog"));
    setWindowTitle(tr("人机验证"));

    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    m_accentColor = tm->currentThemeColor();
    m_cardBg = isLight ? QStringLiteral("rgba(255, 255, 255, 244)")
                       : QStringLiteral("rgba(46, 46, 50, 244)");
    m_cardBorder = isLight ? QStringLiteral("rgba(210, 210, 210, 220)")
                           : QStringLiteral("rgba(92, 92, 98, 220)");
    m_textColor = isLight ? QStringLiteral("#4a4a4a") : QStringLiteral("#c9c9c9");
    m_subColor = isLight ? QStringLiteral("#8a8a8a") : QStringLiteral("#909090");
    m_okColor = QLatin1String(kOkColor);
    m_badColor = QLatin1String(kBadColor);

    initUI();
    initStyle();

    ForumAccountService *service = ForumAccountService::instance();
    connect(service, &ForumAccountService::humanChallengeReady,
            this, &HumanCheckDialog::onChallengeReady);
    connect(service, &ForumAccountService::humanCheckPassed,
            this, &HumanCheckDialog::onCheckPassed);
    connect(service, &ForumAccountService::humanCheckFailed,
            this, &HumanCheckDialog::onCheckFailed);

    fetchChallenge();
}

bool HumanCheckDialog::verify(QWidget *parent)
{
    HumanCheckDialog dialog(parent);
    return dialog.exec() == QDialog::Accepted;
}

void HumanCheckDialog::initUI()
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("blockHumanCheckCard"));
    card->setFixedWidth(368);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 22, 24, 20);
    cardLayout->setSpacing(12);

    m_titleLabel = new QLabel(tr("人机验证"), card);

    m_hintLabel = new QLabel(tr("拖动滑块，把拼图对进缺口，确认不是机器人"), card);
    m_hintLabel->setWordWrap(true);

    m_puzzle = new HumanPuzzleView(card);
    m_puzzle->setFixedSize(320, 180);

    m_slide = new HumanSlideBar(card);
    m_slide->setFixedSize(320, kSlideHeight);
    m_slide->setRange(232);
    m_slide->onMoved = [this](int value) {
        // 滑块的值就是拼图左边在画布里的坐标，拖动时拼图块跟着一起走
        m_puzzle->setOffset(value);
    };
    m_slide->onReleased = [this](int value, int durationMs) {
        // 滑块位置是拼图左边在画布里的坐标，补上拼图自带的透明边才是缺口位置
        submitAnswer(value + m_piecePad, durationMs);
    };

    m_statusLabel = new QLabel(card);
    m_statusLabel->setWordWrap(true);

    m_refreshButton = new QPushButton(tr("换一张"), card);
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setFlat(true);

    cardLayout->addWidget(m_titleLabel);
    cardLayout->addWidget(m_hintLabel);
    cardLayout->addWidget(m_puzzle);
    cardLayout->addWidget(m_slide);

    QHBoxLayout *bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(8);
    bottomRow->addWidget(m_statusLabel, 1);
    bottomRow->addWidget(m_refreshButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    cardLayout->addLayout(bottomRow);

    rootLayout->addWidget(card, 0, Qt::AlignCenter);

    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        if (!m_busy)
            fetchChallenge();
    });
}

void HumanCheckDialog::initStyle()
{
    const bool isLight = (ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme);
    // 轨道色用 #AARRGGBB：QColor 认不了 CSS 的 rgba(...) 写法，会退化成纯黑
    const QString trackColor = isLight ? QStringLiteral("#18000000") : QStringLiteral("#1EFFFFFF");

    m_titleLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 17px; font-weight: 700;").arg(m_textColor));
    m_hintLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;").arg(m_subColor));
    m_statusLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;").arg(m_subColor));

    m_refreshButton->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; border: none;"
                       " color: %1; font-size: 12px; padding: 2px 6px; }"
                       "QPushButton:hover { color: %2; }"
                       "QPushButton:disabled { color: %3; }")
            .arg(m_subColor, m_accentColor, m_cardBorder));

    m_puzzle->setEmptyColor(trackColor);
    m_slide->setColors(m_accentColor, trackColor, m_subColor);

    if (QWidget *card = findChild<QWidget *>(QStringLiteral("blockHumanCheckCard")))
    {
        card->setStyleSheet(
            QStringLiteral("QWidget#blockHumanCheckCard { background-color: %1;"
                           " border: 1px solid %2; border-radius: 16px; }")
                .arg(m_cardBg, m_cardBorder));
    }
}

void HumanCheckDialog::fetchChallenge()
{
    if (m_passed)
        return;

    m_awaitingChallenge = true;
    m_challengeId.clear();
    m_busy = true;
    m_slide->reset();
    m_slide->setLocked(true);
    m_refreshButton->setEnabled(false);
    setStatus(tr("正在出题…"));

    ForumAccountService::instance()->requestHumanChallenge();
}

void HumanCheckDialog::onChallengeReady(const QJsonObject &challenge)
{
    if (m_passed)
        return;

    const QImage bg = decodeDataUrl(challenge.value(QStringLiteral("bg")).toString());
    const QImage piece = decodeDataUrl(challenge.value(QStringLiteral("piece_img")).toString());
    const QJsonObject pieceInfo = challenge.value(QStringLiteral("piece")).toObject();

    const int width = challenge.value(QStringLiteral("width")).toInt(320);
    const int height = challenge.value(QStringLiteral("height")).toInt(180);
    const int span = pieceInfo.value(QStringLiteral("w")).toInt(piece.width());
    const int top = pieceInfo.value(QStringLiteral("top")).toInt(0);
    const int pad = pieceInfo.value(QStringLiteral("pad")).toInt(0);

    if (bg.isNull() || piece.isNull() || span <= 0 || width - span <= 0)
    {
        setStatus(tr("验证图没加载出来，点“换一张”再试一次"), m_badColor);
        m_busy = false;
        m_awaitingChallenge = false;
        m_slide->setLocked(true);
        m_refreshButton->setEnabled(true);
        return;
    }

    m_challengeId = challenge.value(QStringLiteral("id")).toString();
    m_piecePad = pad;
    m_maxAnswer = qMax(1, width - span);

    m_puzzle->setFixedSize(width, height);
    m_puzzle->setChallenge(bg, piece, top);
    m_puzzle->setOffset(0);

    m_slide->setRange(m_maxAnswer);
    m_slide->reset();
    m_slide->setLocked(false);

    m_busy = false;
    m_awaitingChallenge = false;
    m_refreshButton->setEnabled(true);
    setStatus(tr("拖动滑块，把拼图对进缺口"));
}

void HumanCheckDialog::submitAnswer(int answer, int durationMs)
{
    if (m_challengeId.isEmpty())
        return;

    m_busy = true;
    m_awaitingChallenge = false;
    m_slide->setLocked(true);
    m_refreshButton->setEnabled(false);
    setStatus(tr("正在核对…"));

    // 一道题只能交一次，交完题号就作废
    const QString id = m_challengeId;
    m_challengeId.clear();
    ForumAccountService::instance()->submitHumanCheck(id, answer, durationMs);
}

void HumanCheckDialog::onCheckPassed()
{
    if (m_passed)
        return;

    m_passed = true;
    m_busy = false;
    m_slide->setState(HumanSlideBar::Success);
    setStatus(tr("验证通过，正在进入社区…"), m_okColor);
    QTimer::singleShot(420, this, &QDialog::accept);
}

void HumanCheckDialog::onCheckFailed(const QString &message)
{
    if (m_passed)
        return;

    m_busy = false;
    m_slide->setState(HumanSlideBar::Failed);
    m_slide->setLocked(true);
    setStatus(message.isEmpty() ? tr("验证没通过，再试一次") : message, m_badColor);

    if (m_awaitingChallenge)
    {
        // 连题都没拿到：多半是网络问题，留给用户手动重试
        m_awaitingChallenge = false;
        m_refreshButton->setEnabled(true);
        return;
    }

    // 题已经作废，稍等一下自动换一张新图
    QTimer::singleShot(800, this, [this]() {
        if (!m_passed)
            fetchChallenge();
    });
}

void HumanCheckDialog::setStatus(const QString &text, const QString &color)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;")
            .arg(color.isEmpty() ? m_subColor : color));
}
