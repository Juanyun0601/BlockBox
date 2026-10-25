/**
 * @file   HomeAnnouncementBar.cpp
 * @brief  首页公告栏实现
 * @author BlockBox Team
 * @date   2026-10-25
 */

#include "components/HomeAnnouncementBar.h"

#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QUrl>

#include <functional>

namespace {

/// 公告栏整条高度（与首页卡片间距节奏对齐）
constexpr int kBarHeight = 46;
/// 「公告」标签高度
constexpr int kBadgeHeight = 24;
/// 多条公告自动切换间隔
constexpr int kRotateIntervalMs = 8000;
/// 单行公告文字最小宽度（窗口很窄时也不会把翻页控件挤掉）
constexpr int kTextMinWidth = 60;

} // namespace

/**
 * @brief 单行公告文字：超宽自动省略（右侧「…」），带链接时可点击
 *
 * QLabel 自带的省略只能靠 QFontMetrics 在 resize 时重算，
 * 这里把完整文字留一份，宽高变化后重新取省略结果。
 */
class AnnouncementTextLabel : public QLabel
{
public:
  explicit AnnouncementTextLabel(QWidget *parent = nullptr) : QLabel(parent)
  {
    setObjectName("homeAnnounceText");
    setTextFormat(Qt::PlainText);
    // Ignored：宽度完全交给布局分配，避免长公告把 row 撑破
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }

  void setFullText(const QString &text)
  {
    m_fullText = text;
    updateElidedText();
  }

  /** 这条公告能否跳转：决定鼠标手势与 hover 高亮 */
  void setLinkEnabled(bool enabled)
  {
    m_linkEnabled = enabled;
    setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setHovered(false);
  }

  /// 点击回调（本类是 .cpp 内的局部实现，用回调代替信号，避免额外 moc）
  std::function<void()> onActivated;

protected:
  void resizeEvent(QResizeEvent *event) override
  {
    QLabel::resizeEvent(event);
    updateElidedText();
  }

  void mouseReleaseEvent(QMouseEvent *event) override
  {
    if (m_linkEnabled && event->button() == Qt::LeftButton
        && rect().contains(event->position().toPoint()) && onActivated)
      onActivated();
    QLabel::mouseReleaseEvent(event);
  }

  void enterEvent(QEnterEvent *event) override
  {
    QLabel::enterEvent(event);
    if (m_linkEnabled)
      setHovered(true);
  }

  void leaveEvent(QEvent *event) override
  {
    QLabel::leaveEvent(event);
    setHovered(false);
  }

private:
  void updateElidedText()
  {
    const int avail = contentsRect().width();
    if (avail <= 0 || m_fullText.isEmpty())
    {
      QLabel::setText(m_fullText);
      return;
    }
    QLabel::setText(fontMetrics().elidedText(m_fullText, Qt::ElideRight, avail));
  }

  void setHovered(bool on)
  {
    if (m_hovered == on)
      return;
    m_hovered = on;
    setProperty("hovered", on);
    style()->unpolish(this);
    style()->polish(this);
    update();
  }

  QString m_fullText;
  bool m_linkEnabled = false;
  bool m_hovered = false;
};

// ============================================================================
// HomeAnnouncementBar
// ============================================================================

HomeAnnouncementBar::HomeAnnouncementBar(QWidget *parent)
    : QWidget(parent)
{
  setObjectName("homeAnnouncementBar");
  setAttribute(Qt::WA_StyledBackground, true);
  setFixedHeight(kBarHeight);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  buildUi();
  refreshIcons();

  m_rotateTimer = new QTimer(this);
  m_rotateTimer->setInterval(kRotateIntervalMs);
  connect(m_rotateTimer, &QTimer::timeout, this, [this]() { stepPage(1); });

  AnnouncementFeed *feed = AnnouncementFeed::instance();
  connect(feed, &AnnouncementFeed::entriesChanged, this, &HomeAnnouncementBar::reloadFromFeed);
  connect(feed, &AnnouncementFeed::loadingChanged, this, [this](bool loading) {
    if (!loading)
      reloadFromFeed();
  });

  // 先用本地缓存填一次；网络结果到达后由上面的信号自动刷新
  reloadFromFeed();
}

HomeAnnouncementBar::~HomeAnnouncementBar() = default;

void HomeAnnouncementBar::buildUi()
{
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(14, 0, 10, 0);
  layout->setSpacing(10);

  // ── 左侧「公告」小胶囊：喇叭图标 + 文字 ──
  auto *badge = new QWidget(this);
  badge->setObjectName("homeAnnounceBadge");
  badge->setAttribute(Qt::WA_StyledBackground, true);
  badge->setFixedHeight(kBadgeHeight);
  badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  auto *badgeLayout = new QHBoxLayout(badge);
  badgeLayout->setContentsMargins(8, 0, 9, 0);
  badgeLayout->setSpacing(5);

  m_badgeIcon = new QLabel(badge);
  m_badgeIcon->setObjectName("homeAnnounceBadgeIcon");
  m_badgeIcon->setFixedSize(13, 13);
  m_badgeIcon->setAlignment(Qt::AlignCenter);
  badgeLayout->addWidget(m_badgeIcon, 0, Qt::AlignVCenter);

  m_badgeText = new QLabel(tr("公告"), badge);
  m_badgeText->setObjectName("homeAnnounceBadgeText");
  badgeLayout->addWidget(m_badgeText, 0, Qt::AlignVCenter);

  layout->addWidget(badge, 0, Qt::AlignVCenter);

  // ── 中间公告文字（带链接时可点） ──
  m_textLabel = new AnnouncementTextLabel(this);
  m_textLabel->setMinimumWidth(kTextMinWidth);
  layout->addWidget(m_textLabel, 1);
  m_textLabel->onActivated = [this]() { openCurrentLink(); };

  // ── 右侧翻页：页码 + 上一条 / 下一条 ──
  m_pageLabel = new QLabel(this);
  m_pageLabel->setObjectName("homeAnnouncePage");
  m_pageLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  layout->addWidget(m_pageLabel, 0, Qt::AlignVCenter);

  const auto makeNavButton = [this]() {
    auto *btn = new QPushButton(this);
    btn->setObjectName("homeAnnounceNav");
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(22, 22);
    btn->setIconSize(QSize(14, 14));
    btn->setFlat(true);
    btn->setFocusPolicy(Qt::NoFocus);
    return btn;
  };
  m_prevBtn = makeNavButton();
  m_nextBtn = makeNavButton();
  m_prevBtn->setToolTip(tr("上一条公告"));
  m_nextBtn->setToolTip(tr("下一条公告"));
  connect(m_prevBtn, &QPushButton::clicked, this, [this]() { stepPage(-1); });
  connect(m_nextBtn, &QPushButton::clicked, this, [this]() { stepPage(1); });
  layout->addWidget(m_prevBtn, 0, Qt::AlignVCenter);
  layout->addWidget(m_nextBtn, 0, Qt::AlignVCenter);
}

void HomeAnnouncementBar::refreshIcons()
{
  const QColor accent(ThemeManager::instance()->currentThemeColor());
  const QColor arrow(ThemeManager::instance()->currentTextColor());

  if (m_badgeIcon)
  {
    m_badgeIcon->setPixmap(
        IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/announce.svg"), accent, 13)
            .pixmap(13, 13));
  }
  if (m_prevBtn)
  {
    m_prevBtn->setIcon(
        IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/chevron_left.svg"), arrow, 14));
  }
  if (m_nextBtn)
  {
    m_nextBtn->setIcon(
        IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/chevron_right.svg"), arrow, 14));
  }
}

void HomeAnnouncementBar::reloadFromFeed()
{
  m_entries = AnnouncementFeed::instance()->entries();

  if (m_entries.isEmpty())
  {
    // 没有公告就不占位置：整条隐藏，首页保持干净
    stopAutoRotate();
    m_index = 0;
    setVisible(false);
    return;
  }

  if (m_index < 0 || m_index >= m_entries.size())
    m_index = 0;

  setVisible(true);
  showPage(m_index);
}

void HomeAnnouncementBar::showPage(int index)
{
  const int count = m_entries.size();
  if (count == 0)
    return;

  m_index = ((index % count) + count) % count;
  const AnnouncementEntry &entry = m_entries.at(m_index);

  m_textLabel->setFullText(entry.text);
  const bool hasLink = !entry.link.isEmpty();
  m_textLabel->setLinkEnabled(hasLink);
  m_textLabel->setToolTip(hasLink ? tr("%1\n\n点击查看详情").arg(entry.text) : entry.text);

  updateNavState();

  if (count > 1)
    startAutoRotate();
  else
    stopAutoRotate();
}

void HomeAnnouncementBar::stepPage(int delta)
{
  if (m_entries.size() <= 1)
    return;
  showPage(m_index + delta);
}

void HomeAnnouncementBar::updateNavState()
{
  const int count = m_entries.size();
  const bool multi = count > 1;

  // 只有一条时不需要翻页控件
  m_pageLabel->setVisible(multi);
  m_prevBtn->setVisible(multi);
  m_nextBtn->setVisible(multi);

  if (multi)
    m_pageLabel->setText(QStringLiteral("%1/%2").arg(m_index + 1).arg(count));
}

void HomeAnnouncementBar::openCurrentLink()
{
  if (m_index < 0 || m_index >= m_entries.size())
    return;

  const QUrl url(m_entries.at(m_index).link);
  const bool isWebLink = url.isValid()
      && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"));
  if (!isWebLink)
    return;

  if (!QDesktopServices::openUrl(url))
    qWarning() << "[HomeAnnouncementBar] 无法打开公告链接:" << url.toString();
}

void HomeAnnouncementBar::startAutoRotate()
{
  if (!m_rotateTimer || m_entries.size() <= 1 || m_hovered || !isVisible())
    return;
  m_rotateTimer->start();
}

void HomeAnnouncementBar::stopAutoRotate()
{
  if (m_rotateTimer)
    m_rotateTimer->stop();
}

void HomeAnnouncementBar::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  // 切换主题会重新下发样式表，图标跟着新配色重绘
  if (event->type() == QEvent::StyleChange)
    refreshIcons();
}

void HomeAnnouncementBar::enterEvent(QEnterEvent *event)
{
  QWidget::enterEvent(event);
  // 鼠标停在公告上时暂停自动轮播，方便看清 / 点击
  m_hovered = true;
  stopAutoRotate();
}

void HomeAnnouncementBar::leaveEvent(QEvent *event)
{
  QWidget::leaveEvent(event);
  m_hovered = false;
  startAutoRotate();
}

void HomeAnnouncementBar::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);
  startAutoRotate();
  // 回到首页时按节流规则更新一次（数据新鲜则直接复用）
  AnnouncementFeed::instance()->refresh(false);
}

void HomeAnnouncementBar::hideEvent(QHideEvent *event)
{
  QWidget::hideEvent(event);
  stopAutoRotate();
}
