/**
 * @file   NewsPage.cpp
 * @brief  软件内置的「官方新闻」列表页实现
 * @author BlockBox Team
 * @date   2026-10-24
 */

#include "NewsPage.h"

#include "components/NewsCard.h"
#include "components/NewsDetailDialog.h"
#include "components/OutlinedLabel.h"
#include "utils/news/NewsFeed.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kColumnSpacing = 12;
constexpr int kRowSpacing = 12;
// 列表页卡片展示较长的摘要（首页横排卡片仍用默认的较短摘要）
constexpr int kListSummaryChars = 220;
constexpr int kTwoColumnMinWidth = 760;
} // namespace

NewsPage::NewsPage(QWidget *parent)
    : QWidget(parent)
{
  initUI();

  NewsFeed *feed = NewsFeed::instance();
  QObject::connect(feed, &NewsFeed::entriesChanged, this, [this]() {
    rebuildList();
    updateHeaderState();
  });
  QObject::connect(feed, &NewsFeed::loadingChanged, this, [this](bool) {
    updateHeaderState();
    // 还没有内容时，空态文案要跟着「加载中 / 加载结束」切换
    if (!NewsFeed::instance()->hasData())
      rebuildList();
  });
  QObject::connect(feed, &NewsFeed::requestFailed, this, [this](const QString &) {
    updateHeaderState();
  });

  rebuildList();
  updateHeaderState();
}

void NewsPage::initUI()
{
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(20, 20, 20, 20);
  root->setSpacing(12);

  // ── 标题行：官方新闻 · 刷新 ──
  auto *titleRow = new QHBoxLayout();
  titleRow->setContentsMargins(0, 0, 0, 0);
  titleRow->setSpacing(10);

  auto *titleLabel = new OutlinedLabel(tr("官方新闻"), this);
  titleLabel->setObjectName("pageTitleLabel");
  titleRow->addWidget(titleLabel);
  titleRow->addStretch();

  m_refreshBtn = new QPushButton(tr("刷新"), this);
  m_refreshBtn->setObjectName("homeLinkBtn");
  m_refreshBtn->setCursor(Qt::PointingHandCursor);
  m_refreshBtn->setToolTip(tr("重新获取最新官方新闻"));
  QObject::connect(m_refreshBtn, &QPushButton::clicked, this, []() {
    NewsFeed::instance()->refresh(true);
  });
  titleRow->addWidget(m_refreshBtn);
  root->addLayout(titleRow);

  // ── 来源与条数 ──
  auto *metaRow = new QHBoxLayout();
  metaRow->setContentsMargins(0, 0, 0, 0);
  metaRow->setSpacing(8);

  auto *sourceLabel = new QLabel(tr("来自 Minecraft 官方 · 与官方启动器同源"), this);
  sourceLabel->setObjectName("homeNewsSource");
  metaRow->addWidget(sourceLabel);
  metaRow->addStretch();

  m_countLabel = new QLabel(this);
  m_countLabel->setObjectName("homeNewsSource");
  metaRow->addWidget(m_countLabel);
  root->addLayout(metaRow);

  // ── 加载 / 网络失败提示 ──
  m_statusLabel = new QLabel(this);
  m_statusLabel->setObjectName("homeNewsStatus");
  m_statusLabel->setWordWrap(true);
  m_statusLabel->setVisible(false);
  root->addWidget(m_statusLabel);

  // ── 新闻列表（滚动区） ──
  m_scrollArea = new QScrollArea(this);
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setFrameShape(QFrame::NoFrame);
  m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_scrollArea->viewport()->installEventFilter(this);
  root->addWidget(m_scrollArea, 1);

  // ── 空态 ──
  m_emptyLabel = new QLabel(this);
  m_emptyLabel->setObjectName("homeEmptyLabel");
  m_emptyLabel->setAlignment(Qt::AlignCenter);
  m_emptyLabel->setWordWrap(true);
  m_emptyLabel->setMinimumHeight(180);
  m_emptyLabel->setVisible(false);
  root->addWidget(m_emptyLabel);
}

bool NewsPage::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() == QEvent::Resize && m_scrollArea && watched == m_scrollArea->viewport())
    scheduleRelayout();

  return QWidget::eventFilter(watched, event);
}

void NewsPage::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);

  // 数据过期时顺手更新一次（默认 30 分钟内不重复请求）
  NewsFeed::instance()->refresh(false);
  updateHeaderState();

  // 首次显示后滚动区才拿到真实宽度，按当前宽度重建一次
  QTimer::singleShot(0, this, [this]() {
    int columns = 1;
    const int cardWidth = calculateCardWidth(&columns);
    if (cardWidth != m_builtCardWidth || columns != m_builtColumns)
      rebuildList(true);
  });
}

int NewsPage::calculateCardWidth(int *columns) const
{
  int availableWidth = 0;
  if (m_scrollArea && m_scrollArea->viewport()->width() > 0)
    availableWidth = m_scrollArea->viewport()->width();
  if (availableWidth <= 0)
    availableWidth = qMax(360, width() - 40);

  const int cols = availableWidth >= kTwoColumnMinWidth ? 2 : 1;
  if (columns)
    *columns = cols;

  return qMax(220, (availableWidth - (cols - 1) * kColumnSpacing) / cols);
}

void NewsPage::rebuildList(bool preserveScroll)
{
  if (!m_scrollArea)
    return;

  const QVector<NewsEntry> &entries = NewsFeed::instance()->entries();

  int columns = 1;
  const int cardWidth = calculateCardWidth(&columns);
  m_builtCardWidth = cardWidth;
  m_builtColumns = columns;

  m_countLabel->setText(entries.isEmpty() ? QString() : tr("共 %1 条").arg(entries.size()));

  // 空态：隐藏滚动区，直接给出说明
  if (entries.isEmpty())
  {
    m_scrollArea->setVisible(false);
    m_emptyLabel->setText(NewsFeed::instance()->isLoading()
                              ? tr("正在获取 Minecraft 官方新闻…")
                              : tr("暂时没有可显示的官方新闻，点「刷新」再试试"));
    m_emptyLabel->setVisible(true);
    return;
  }

  m_emptyLabel->setVisible(false);
  m_scrollArea->setVisible(true);

  const int savedScroll = preserveScroll ? m_scrollArea->verticalScrollBar()->value() : 0;

  // 重建滚动内容：takeWidget 不会销毁旧控件，交给事件循环回收
  if (QWidget *old = m_scrollArea->takeWidget())
    old->deleteLater();

  auto *content = new QWidget();
  auto *rows = new QVBoxLayout(content);
  rows->setContentsMargins(0, 0, 0, 0);
  rows->setSpacing(kRowSpacing);
  rows->setAlignment(Qt::AlignTop);

  int index = 0;
  while (index < entries.size())
  {
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kColumnSpacing);

    for (int column = 0; column < columns && index < entries.size(); ++column, ++index)
    {
      const NewsEntry entry = entries.at(index);

      auto *card = new NewsCard(entry.categoryLabel, entry.title,
                                NewsFeed::formatNewsDate(entry.date), entry.summary,
                                content, kListSummaryChars);
      card->setFixedSize(card->contentSizeForWidth(cardWidth));
      card->setToolTip(tr("点击查看详情"));
      QObject::connect(card, &NewsCard::clicked, this, [this, entry]() {
        showNewsDetailDialog(this, entry);
      });
      row->addWidget(card);
    }
    row->addStretch();
    rows->addLayout(row);
  }

  m_scrollArea->setWidget(content);
  // 宽度变化触发的重建：保持原来的滚动位置
  if (savedScroll > 0)
  {
    QTimer::singleShot(0, this, [this, savedScroll]() {
      if (m_scrollArea)
        m_scrollArea->verticalScrollBar()->setValue(savedScroll);
    });
  }
}

void NewsPage::scheduleRelayout()
{
  if (m_relayoutPending)
    return;

  m_relayoutPending = true;
  QTimer::singleShot(0, this, [this]() {
    m_relayoutPending = false;
    int columns = 1;
    const int cardWidth = calculateCardWidth(&columns);
    if (cardWidth == m_builtCardWidth && columns == m_builtColumns)
      return;
    rebuildList(true);
  });
}

void NewsPage::updateHeaderState()
{
  const NewsFeed *feed = NewsFeed::instance();

  if (m_refreshBtn)
  {
    m_refreshBtn->setEnabled(!feed->isLoading());
    m_refreshBtn->setText(feed->isLoading() ? tr("刷新中…") : tr("刷新"));
  }

  if (!m_statusLabel)
    return;

  QString text;
  if (!feed->lastError().isEmpty())
  {
    text = feed->hasData()
        ? tr("网络不太顺畅，当前显示的是最近一次同步的官方新闻")
        : tr("暂时获取不到官方新闻：%1").arg(feed->lastError());
  }

  m_statusLabel->setText(text);
  m_statusLabel->setVisible(!text.isEmpty());
}
