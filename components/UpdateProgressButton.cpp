/**
 * @file   UpdateProgressButton.cpp
 * @brief  侧边栏圆形更新进度按钮实现
 * @author BlockBox Team
 * @date   2026-10-05
 */
#include "components/UpdateProgressButton.h"

#include <cmath>

#include <QPainter>
#include <QTimer>

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "utils/UpdateChecker.h"

namespace {

constexpr int kButtonSize = 44;   // 侧边栏宽 76px，44px 圆钮居中合适
constexpr int kRingWidth = 3;     // 环形线宽
constexpr int kIconSize = 18;     // 中心图标边长
// 下载完成态的绿色与失败态的红色（不随主题变化，语义固定）
const QColor kCompletedColor(0x2E, 0xCC, 0x71);
const QColor kFailedColor(0xE7, 0x4C, 0x3C);

// 动画节拍：约 30fps，让环形进度连续生长
constexpr int kAnimationIntervalMs = 33;
// 未知进度时每帧转过的角度（约 2.7 秒转一圈，慢而明显）
constexpr qreal kSpinStepDegrees = 4.5;
// 未知进度那一段短弧的张角
constexpr int kIndeterminateArcDegrees = 78;

} // namespace

UpdateProgressButton::UpdateProgressButton(QWidget *parent)
    : QPushButton(parent)
{
    setFixedSize(kButtonSize, kButtonSize);
    setCursor(Qt::PointingHandCursor);
    setFlat(true);
    // 完全自绘，屏蔽 QPushButton 原生样式
    setStyleSheet(QStringLiteral("QPushButton{border:none;background:transparent;}"));

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(kAnimationIntervalMs);
    connect(m_animTimer, &QTimer::timeout, this, &UpdateProgressButton::onAnimationTick);

    connect(this, &QPushButton::clicked, this, &UpdateProgressButton::onClicked);
    // 跟随主题主色重绘下载环
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, [this](const QString &) { update(); });
    connect(UpdateChecker::instance(), &UpdateChecker::downloadStarted,
            this, &UpdateProgressButton::onDownloadStarted);
    connect(UpdateChecker::instance(), &UpdateChecker::downloadProgress,
            this, &UpdateProgressButton::onDownloadProgress);
    connect(UpdateChecker::instance(), &UpdateChecker::downloadFinished,
            this, &UpdateProgressButton::onDownloadFinished);
    connect(UpdateChecker::instance(), &UpdateChecker::downloadFailed,
            this, &UpdateProgressButton::onDownloadFailed);

    // 与检查器当前状态同步（按钮可能在下载进行中才被创建）
    if (UpdateChecker::instance()->downloadState() == UpdateChecker::DownloadCompleted) {
        m_state = State::Completed;
        m_targetFraction = 1.0;
        m_displayFraction = 1.0;
        syncToState();
    }
}

void UpdateProgressButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF bounds = rect();
    // 悬停/按下时的淡色底圆
    if (underMouse()) {
        QColor bg = palette().color(QPalette::Window);
        bg.setAlpha(isDown() ? 70 : 40);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawEllipse(bounds.adjusted(2, 2, -2, -2));
    }

    // 环形轨道
    const qreal inset = 2 + kRingWidth;
    const QRectF ring = bounds.adjusted(inset, inset, -inset, -inset);
    QColor track = palette().color(QPalette::WindowText);
    track.setAlpha(30);
    QPen trackPen(track, kRingWidth, Qt::SolidLine, Qt::RoundCap);
    trackPen.setCapStyle(Qt::RoundCap);
    p.setPen(trackPen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(ring);

    // 进度弧 / 完成环 / 失败环
    if (m_state == State::Downloading) {
        QPen arcPen(stateColor(), kRingWidth);
        arcPen.setCapStyle(Qt::RoundCap);
        p.setPen(arcPen);
        if (m_targetFraction <= 0.0 && m_displayFraction <= 0.0) {
            // 总大小未知（分块传输、加速节点没给长度）或还没有数据进来：
            // 画一段绕圈旋转的短弧，让人一眼看出正在下载而不是卡住
            const int startAngle =
                static_cast<int>((90.0 - m_spinDegrees) * 16.0);
            p.drawArc(ring, startAngle, -kIndeterminateArcDegrees * 16);
        } else {
            // 从 12 点钟方向顺时针绘制，长度按收发比例连续增长
            const int span = -static_cast<int>(qBound<qreal>(0.0, m_displayFraction, 1.0)
                                               * 360.0 * 16.0);
            if (span != 0)
                p.drawArc(ring, 90 * 16, span);
        }
    } else if (m_state == State::Completed || m_state == State::Failed) {
        QPen ringPen(stateColor(), kRingWidth);
        ringPen.setCapStyle(Qt::RoundCap);
        p.setPen(ringPen);
        p.drawEllipse(ring);
    }

    // 中心内容：已经能算出百分比时直接显示数字（一眼看出在往前涨），
    // 还没拿到进度、下载完成、下载失败时仍然显示下载图标
    const int percent = qMin(100, static_cast<int>(m_displayFraction * 100.0 + 0.5));
    if (m_state == State::Downloading && percent > 0) {
        QFont percentFont = font();
        percentFont.setBold(true);
        percentFont.setPixelSize(percent >= 100 ? 10 : 11);
        p.setFont(percentFont);
        p.setPen(stateColor());
        p.drawText(rect(), Qt::AlignCenter, QString::number(percent) + QStringLiteral("%"));
        return;
    }

    // 中心图标：下载中/完成用主题强调色，失败用红色
    QColor iconColor = (m_state == State::Failed)
        ? kFailedColor
        : (m_state == State::Completed ? kCompletedColor : accentColor());
    const QIcon icon = IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/download.svg"), iconColor, kIconSize);
    const QRectF iconRect((width() - kIconSize) / 2.0, (height() - kIconSize) / 2.0,
                          kIconSize, kIconSize);
    icon.paint(&p, iconRect.toRect());
}

void UpdateProgressButton::onAnimationTick()
{
    if (m_state != State::Downloading) {
        m_animTimer->stop();
        return;
    }

    if (m_targetFraction <= 0.0) {
        // 还没拿到可用的百分比：让短弧匀速绕圈
        m_spinDegrees = std::fmod(m_spinDegrees + kSpinStepDegrees, 360.0);
        update();
        return;
    }

    const qreal remaining = m_targetFraction - m_displayFraction;
    if (remaining <= 0.0002) {
        // 进度反而变小了（换源重下、字节数从头算起）：直接缩回去，
        // 不要停在更大的弧长上等它慢慢追上来
        if (m_displayFraction > m_targetFraction) {
            m_displayFraction = m_targetFraction;
            update();
        }
        return; // 已经追平，等下一次进度信号
    }

    // 平滑追上去：每次走剩余差的 26%，环形进度看起来是连续推进的，
    // 不会几百 KB 才挪一格（小圆钮上整 1% 的跳跃几乎看不出来）
    m_displayFraction += remaining * 0.26;
    if (m_targetFraction - m_displayFraction <= 0.002)
        m_displayFraction = m_targetFraction;
    update();
}

void UpdateProgressButton::onDownloadStarted()
{
    m_state = State::Downloading;
    m_targetFraction = 0.0;
    m_displayFraction = 0.0;
    m_spinDegrees = 0.0;
    syncToState();
}

void UpdateProgressButton::onDownloadProgress(qint64 received, qint64 total)
{
    if (total > 0 && received > 0) {
        m_targetFraction = qBound<qreal>(
            0.0, static_cast<qreal>(received) / static_cast<qreal>(total), 1.0);
        setToolTip(tr("正在下载更新… %1%").arg(static_cast<int>(m_targetFraction * 100.0)));
    } else {
        // 总大小未知（total<=0）或重试刚开始：回到"旋转"状态，不要停在上一轮的弧上
        m_targetFraction = 0.0;
        m_displayFraction = 0.0;
        setToolTip(tr("正在下载更新…"));
    }

    if (m_state != State::Downloading) {
        m_state = State::Downloading;
        syncToState();
        return;
    }
    if (!m_animTimer->isActive())
        m_animTimer->start();
    update();
}

void UpdateProgressButton::onDownloadFinished()
{
    m_state = State::Completed;
    m_targetFraction = 1.0;
    m_displayFraction = 1.0;
    syncToState();
}

void UpdateProgressButton::onDownloadFailed(const QString &error)
{
    Q_UNUSED(error);
    m_state = State::Failed;
    m_displayFraction = 0.0;
    m_targetFraction = 0.0;
    syncToState();
}

void UpdateProgressButton::onClicked()
{
    UpdateChecker *checker = UpdateChecker::instance();
    if (m_state == State::Completed)
        checker->installDownloadedUpdate();
    else if (m_state == State::Failed && !checker->isDownloading())
        checker->startDownload();
}

void UpdateProgressButton::syncToState()
{
    setVisible(m_state != State::Hidden);

    if (m_state == State::Downloading) {
        if (!m_animTimer->isActive())
            m_animTimer->start();
    } else {
        m_animTimer->stop();
    }

    switch (m_state) {
    case State::Downloading:
        setToolTip(tr("正在下载更新…"));
        break;
    case State::Completed:
        setToolTip(tr("更新已下载完成，点击重启并安装"));
        break;
    case State::Failed:
        setToolTip(tr("下载失败，点击重试"));
        break;
    default:
        break;
    }
    update();
}

QColor UpdateProgressButton::stateColor() const
{
    switch (m_state) {
    case State::Completed:
        return kCompletedColor;
    case State::Failed:
        return kFailedColor;
    default:
        return accentColor();
    }
}

QColor UpdateProgressButton::accentColor() const
{
    const QColor theme(ThemeManager::instance()->currentThemeColor());
    return theme.isValid() ? theme : palette().color(QPalette::Highlight);
}
