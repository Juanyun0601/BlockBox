/**
 * @file   UpdateProgressButton.cpp
 * @brief  侧边栏圆形更新进度按钮实现
 * @author BlockBox Team
 * @date   2026-10-05
 */
#include "components/UpdateProgressButton.h"

#include <QPainter>

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

} // namespace

UpdateProgressButton::UpdateProgressButton(QWidget *parent)
    : QPushButton(parent)
{
    setFixedSize(kButtonSize, kButtonSize);
    setCursor(Qt::PointingHandCursor);
    setFlat(true);
    // 完全自绘，屏蔽 QPushButton 原生样式
    setStyleSheet(QStringLiteral("QPushButton{border:none;background:transparent;}"));

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
    if (m_state == State::Downloading && m_percent > 0) {
        QPen arcPen(stateColor(), kRingWidth);
        arcPen.setCapStyle(Qt::RoundCap);
        p.setPen(arcPen);
        // 从 12 点钟方向顺时针绘制
        const int span = -m_percent * 360 * 16;
        p.drawArc(ring, 90 * 16, span);
    } else if (m_state == State::Completed || m_state == State::Failed) {
        QPen ringPen(stateColor(), kRingWidth);
        ringPen.setCapStyle(Qt::RoundCap);
        p.setPen(ringPen);
        p.drawEllipse(ring);
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

void UpdateProgressButton::onDownloadStarted()
{
    m_state = State::Downloading;
    m_percent = 0;
    syncToState();
}

void UpdateProgressButton::onDownloadProgress(qint64 received, qint64 total)
{
    if (total > 0)
        m_percent = static_cast<int>(received * 100 / total);
    setToolTip(tr("正在下载更新… %1%").arg(m_percent));
    update();
}

void UpdateProgressButton::onDownloadFinished()
{
    m_state = State::Completed;
    m_percent = 100;
    syncToState();
}

void UpdateProgressButton::onDownloadFailed(const QString &error)
{
    Q_UNUSED(error);
    m_state = State::Failed;
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
