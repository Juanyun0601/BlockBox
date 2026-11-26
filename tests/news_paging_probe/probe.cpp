/**
 * @file   probe.cpp
 * @brief  新闻列表页「加载全部新闻」探针：真实 NewsPage + 假数据源，离屏验证分屏加载
 *
 * 覆盖：
 *   1) 首屏只展开一屏卡片（列数 × 6 行），不一次建满上百张卡；
 *   2) 底部「加载更多」栏显示「已显示 N / 总数 条」，点一次追加一屏；
 *   3) 一直点到没有剩余时按钮栏隐藏、卡片总数等于全部新闻条数；
 *   4) 列表滚到底部会自动再续一屏。
 */

#include "pages/NewsPage.h"

#include "components/NewsCard.h"
#include "components/NewsDetailDialog.h"
#include "utils/news/NewsFeed.h"

#include <QApplication>
#include <QDate>
#include <QEventLoop>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVector>
#include <cstdio>

namespace
{
int g_failures = 0;

void expect(const char *what, bool ok)
{
  std::fprintf(stderr, "[probe] %s  %s\n", ok ? "OK  " : "FAIL", what);
  if (!ok)
    ++g_failures;
}

void pump(int ms = 30)
{
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

/** 造 n 条「官方新闻」 */
QVector<NewsEntry> makeEntries(int n)
{
  QVector<NewsEntry> entries;
  entries.reserve(n);
  for (int i = 0; i < n; ++i)
  {
    NewsEntry entry;
    entry.id = QStringLiteral("news-%1").arg(i);
    entry.title = QStringLiteral("Official news item #%1").arg(i + 1);
    entry.summary = QStringLiteral("Summary text for item %1, long enough to wrap a little.")
                        .arg(i + 1);
    entry.category = QStringLiteral("Minecraft: Java Edition");
    entry.categoryLabel = QStringLiteral("Java 版");
    entry.date = QDate(2026, 11, 1).addDays(-i);
    entry.link = QStringLiteral("https://www.minecraft.net/article/%1").arg(i);
    entries.append(entry);
  }
  return entries;
}

int countCards(QWidget *page)
{
  return page->findChildren<NewsCard *>().size();
}

int columnCountOf(QWidget *page)
{
  auto *scroll = page->findChild<QScrollArea *>();
  const int viewportWidth = scroll ? scroll->viewport()->width() : 0;
  return viewportWidth >= 760 ? 2 : 1;
}

QWidget *loadMoreBar(QWidget *page)
{
  return page->findChild<QWidget *>(QStringLiteral("newsLoadMoreBar"));
}

QLabel *loadMoreHint(QWidget *page)
{
  QWidget *bar = loadMoreBar(page);
  return bar ? bar->findChild<QLabel *>(QStringLiteral("homeNewsSource")) : nullptr;
}

QScrollBar *pageScrollBar(QWidget *page)
{
  auto *scroll = page->findChild<QScrollArea *>();
  return scroll ? scroll->verticalScrollBar() : nullptr;
}
} // namespace

NewsFeed *NewsFeed::instance()
{
  static NewsFeed *s_instance = new NewsFeed();
  return s_instance;
}

void showNewsDetailDialog(QWidget *, const NewsEntry &) {}

int main(int argc, char **argv)
{
  QApplication app(argc, argv);

  constexpr int kTotal = 100;
  NewsFeed::instance()->seed(makeEntries(kTotal));

  // ── 1) 首屏：只展开一屏 ──
  NewsPage page;
  page.resize(1200, 800);
  page.show();
  pump(120);
  pump(80);

  const int columns = columnCountOf(&page);
  const int batch = columns * 6;
  const int firstScreen = countCards(&page);
  std::fprintf(stderr, "[probe] 列数=%d 首屏卡片=%d 一屏=%d\n", columns, firstScreen, batch);
  expect("首屏按列数展开整屏卡片（不是一次建满 100 张）",
         firstScreen >= batch && firstScreen % columns == 0 && firstScreen < kTotal);

  QWidget *bar = loadMoreBar(&page);
  expect("底部出现「加载更多」栏", bar && bar->isVisible());

  QLabel *hint = loadMoreHint(&page);
  const QString hintText = hint ? hint->text() : QString();
  std::fprintf(stderr, "[probe] 提示文字：%s\n", qPrintable(hintText));
  expect("提示里写明已显示条数与总条数",
         hintText.contains(QStringLiteral("已显示 %1 / %2").arg(firstScreen).arg(kTotal)));

  // ── 2) 点「加载更多」：一次追加一屏 ──
  QPushButton *moreBtn = page.findChild<QPushButton *>(QStringLiteral("newsLoadMoreBtn"));
  expect("存在「加载更多」按钮", moreBtn != nullptr);
  if (moreBtn)
  {
    moreBtn->click();
    pump(60);
    const int afterClick = countCards(&page);
    std::fprintf(stderr, "[probe] 点击后卡片=%d\n", afterClick);
    expect("点一次追加一屏卡片", afterClick == qMin(kTotal, firstScreen + batch));

    // ── 3) 一直点到底 ──
    int guard = 0;
    while (moreBtn->isVisible() && guard++ < 50)
    {
      moreBtn->click();
      pump(30);
    }
    std::fprintf(stderr, "[probe] 全部展开后卡片=%d\n", countCards(&page));
    expect("全部展开后卡片数等于官方新闻总数", countCards(&page) == kTotal);
    expect("全部展开后「加载更多」栏隐藏", bar && !bar->isVisible());
  }

  // ── 4) 新页面：滚到底自动续一屏 ──
  NewsPage autoPage;
  autoPage.resize(1200, 800);
  autoPage.show();
  pump(120);
  pump(80);

  const int beforeScroll = countCards(&autoPage);
  QScrollBar *scrollBar = pageScrollBar(&autoPage);
  expect("存在列表滚动条", scrollBar != nullptr);
  if (scrollBar)
  {
    scrollBar->setValue(scrollBar->maximum());
    pump(120);
    const int afterScroll = countCards(&autoPage);
    std::fprintf(stderr, "[probe] 滚到底前=%d 后=%d\n", beforeScroll, afterScroll);
    expect("滚到底自动追加一屏", afterScroll > beforeScroll);
    expect("自动追加的同样是一整屏",
           afterScroll == qMin(kTotal, beforeScroll + columnCountOf(&autoPage) * 6));
  }

  std::fprintf(stderr, "[probe] %s（失败 %d 项）\n", g_failures == 0 ? "全部通过" : "有失败项",
               g_failures);
  return g_failures == 0 ? 0 : 1;
}
