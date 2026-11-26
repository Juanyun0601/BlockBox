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
// 首屏与每次「加载更多」追加的行数；官方接口约 100 条，分屏加载避免一次建满卡片
constexpr int kRowsPerBatch = 6;
} // namespace

NewsPage::NewsPage(QWidget *parent)
    : QWidget(parent)
{
  initUI();

  NewsFeed *feed = NewsFeed::instance();
  QObject::connect(feed, &NewsFeed::entriesChanged, this, [this]() {
    // 拉到新数据：回到第一屏，避免旧的展开进度作用到新列表上
    m_renderedCount = 0;
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

  // 滚到底部自动续一屏：官方新闻有上百条，不想让用户以为列表只到这儿
  QObject::connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this,
                   [this](int value) {
                     QScrollBar *bar = m_scrollArea ? m_scrollArea->verticalScrollBar() : nullptr;
                     if (!bar || bar->maximum() <= 0 || value < bar->maximum() - 8)
                       return;
                     if (m_renderedCount >= NewsFeed::instance()->entries().size())
                       return;
                     appendNextBatch();
                   });

  // ── 底部「加载更多」栏 ──
  m_loadMoreBar = new QWidget(this);
  m_loadMoreBar->setObjectName("newsLoadMoreBar");
  auto *loadMoreRow = new QHBoxLayout(m_loadMoreBar);
  loadMoreRow->setContentsMargins(0, 0, 0, 0);
  loadMoreRow->setSpacing(8);

  m_loadMoreHint = new QLabel(m_loadMoreBar);
  m_loadMoreHint->setObjectName("homeNewsSource");
  loadMoreRow->addWidget(m_loadMoreHint);
  loadMoreRow->addStretch();

  m_loadMoreBtn = new QPushButton(tr("加载更多"), m_loadMoreBar);
  m_loadMoreBtn->setObjectName("newsLoadMoreBtn");
  m_loadMoreBtn->setCursor(Qt::PointingHandCursor);
  m_loadMoreBtn->setToolTip(tr("继续展开更早的官方新闻；直接下滑到列表底部也会自动加载"));
  QObject::connect(m_loadMoreBtn, &QPushButton::clicked, this, [this]() {
    appendNextBatch();
  });
  loadMoreRow->addWidget(m_loadMoreBtn);

  m_loadMoreBar->setVisible(false);
  root->addWidget(m_loadMoreBar);

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
    m_renderedCount = 0;
    m_emptyLabel->setText(NewsFeed::instance()->isLoading()
                              ? tr("正在获取 Minecraft 官方新闻…")
                              : tr("暂时没有可显示的官方新闻，点「刷新」再试试"));
    m_emptyLabel->setVisible(true);
    updateLoadMoreBar();
    return;
  }

  m_emptyLabel->setVisible(false);
  m_scrollArea->setVisible(true);

  // 宽度变化重建时保留已经展开的条数；数据变化时外部会把进度清零，从第一屏重新开始。
  // 只展开过首屏（还没点过「加载更多」）时按新列数重算首屏，避免窄屏建出来的 6 条一直卡在宽屏上。
  const int batchSize = qMax(1, columns * kRowsPerBatch);
  m_renderedCount = qBound(0, m_renderedCount, entries.size());
  if (m_renderedCount == 0 || m_renderedCount <= m_lastBatchSize)
    m_renderedCount = qMin(batchSize, entries.size());
  m_lastBatchSize = batchSize;

  const int savedScroll = preserveScroll ? m_scrollArea->verticalScrollBar()->value() : 0;

  // 重建滚动内容：takeWidget 不会销毁旧控件，交给事件循环回收
  if (QWidget *old = m_scrollArea->takeWidget())
    old->deleteLater();

  m_content = new QWidget();
  m_rowsLayout = new QVBoxLayout(m_content);
  m_rowsLayout->setContentsMargins(0, 0, 0, 0);
  m_rowsLayout->setSpacing(kRowSpacing);
  m_rowsLayout->setAlignment(Qt::AlignTop);

  appendEntries(0, m_renderedCount);

  m_scrollArea->setWidget(m_content);
  updateLoadMoreBar();

  // 窗口很高、首屏内容不满一屏时没有滚动条，也就没法「滑到底自动加载」：
  // 补到能滚为止，剩下的交给「加载更多」
  QTimer::singleShot(0, this, [this]() { ensureScrollable(); });

  // 宽度变化触发的重建：保持原来的滚动位置
  if (savedScroll > 0)
  {
    QTimer::singleShot(0, this, [this, savedScroll]() {
      if (m_scrollArea)
        m_scrollArea->verticalScrollBar()->setValue(savedScroll);
    });
  }
}

void NewsPage::appendEntries(int beginIndex, int endIndex)
{
  const QVector<NewsEntry> &entries = NewsFeed::instance()->entries();
  if (!m_rowsLayout || !m_content)
    return;

  const int columns = qMax(1, m_builtColumns);
  const int cardWidth = m_builtCardWidth > 0 ? m_builtCardWidth : calculateCardWidth(nullptr);
  const int end = qMin(endIndex, entries.size());

  int index = qBound(0, beginIndex, end);
  while (index < end)
  {
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kColumnSpacing);

    for (int column = 0; column < columns && index < end; ++column, ++index)
    {
      const NewsEntry entry = entries.at(index);

      auto *card = new NewsCard(entry.categoryLabel, entry.title,
                                NewsFeed::formatNewsDate(entry.date), entry.summary,
                                m_content, kListSummaryChars);
      card->setFixedSize(card->contentSizeForWidth(cardWidth));
      card->setToolTip(tr("点击查看详情"));
      QObject::connect(card, &NewsCard::clicked, this, [this, entry]() {
        showNewsDetailDialog(this, entry);
      });
      row->addWidget(card);
    }
    row->addStretch();
    m_rowsLayout->addLayout(row);
  }
}

void NewsPage::appendNextBatch()
{
  const QVector<NewsEntry> &entries = NewsFeed::instance()->entries();
  if (!m_rowsLayout || m_renderedCount >= entries.size())
  {
    updateLoadMoreBar();
    return;
  }

  const int columns = qMax(1, m_builtColumns);
  const int batchSize = qMax(1, columns * kRowsPerBatch);
  const int end = qMin(entries.size(), m_renderedCount + batchSize);
  appendEntries(m_renderedCount, end);
  m_renderedCount = end;
  updateLoadMoreBar();
}

void NewsPage::updateLoadMoreBar()
{
  if (!m_loadMoreBar || !m_loadMoreBtn || !m_loadMoreHint)
    return;

  const int total = NewsFeed::instance()->entries().size();
  const bool hasMore = m_renderedCount > 0 && m_renderedCount < total;
  m_loadMoreBar->setVisible(hasMore);

  if (!hasMore)
    return;

  m_loadMoreHint->setText(tr("已显示 %1 / %2 条").arg(m_renderedCount).arg(total));
}

void NewsPage::ensureScrollable()
{
  if (!m_scrollArea || m_fillPending)
    return;

  const int total = NewsFeed::instance()->entries().size();
  if (m_renderedCount <= 0 || m_renderedCount >= total)
    return;

  // 内容已经能滚动（或已经全部展开）就不用再补
  if (m_scrollArea->verticalScrollBar()->maximum() > 0)
    return;

  // 每补一屏让事件循环跑一拍，滚动范围才能按新内容重算，下一拍再判断是否还不足一屏
  m_fillPending = true;
  appendNextBatch();
  QTimer::singleShot(0, this, [this]() {
    m_fillPending = false;
    ensureScrollable();
  });
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
