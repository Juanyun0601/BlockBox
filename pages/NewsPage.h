/**
 * @file   NewsPage.h
 * @brief  软件内置的「官方新闻」列表页
 * @author BlockBox Team
 * @date   2026-10-24
 *
 * 数据来自 NewsFeed（与官方启动器同源），整页在软件内浏览，不打开浏览器。
 */

#ifndef NEWSPAGE_H
#define NEWSPAGE_H

#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QVBoxLayout;

class NewsPage : public QWidget
{
  Q_OBJECT

public:
  explicit NewsPage(QWidget *parent = nullptr);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  void initUI();
  /// 按当前宽度重建新闻卡片（数据变化或窗口宽度变化时调用）
  /// @param preserveScroll 宽度变化引起的重建保持原滚动位置
  void rebuildList(bool preserveScroll = false);
  /// 把 entries[beginIndex, endIndex) 的卡片按当前列数追加到列表末尾
  void appendEntries(int beginIndex, int endIndex);
  /// 再展开一屏新闻（官方接口约 100 条，分屏加载不至于一次建满控件）
  void appendNextBatch();
  /// 底部「加载更多」栏：还剩条目时显示，全部展开后隐藏
  void updateLoadMoreBar();
  /// 首屏内容不足一屏（大屏 / 高窗口）时继续补几屏，保证列表能滚动
  void ensureScrollable();
  /// 刷新按钮状态 / 加载与失败提示
  void updateHeaderState();
  /// 合并连续 resize，延迟一拍重建
  void scheduleRelayout();
  /// 依据可视宽度计算单张卡片宽度与列数
  int calculateCardWidth(int *columns) const;

  QLabel *m_countLabel = nullptr;
  QLabel *m_statusLabel = nullptr;
  QLabel *m_emptyLabel = nullptr;
  QPushButton *m_refreshBtn = nullptr;
  QScrollArea *m_scrollArea = nullptr;
  QWidget *m_content = nullptr;
  QVBoxLayout *m_rowsLayout = nullptr;
  QWidget *m_loadMoreBar = nullptr;
  QLabel *m_loadMoreHint = nullptr;
  QPushButton *m_loadMoreBtn = nullptr;
  int m_builtCardWidth = -1;
  int m_builtColumns = -1;
  /// 当前列数下一屏的条数，用来判断用户是否只展开了首屏（只展开首屏时，列数变化按新列数重算）
  int m_lastBatchSize = 0;
  /// 当前已展开到列表里的条数（列表页分屏加载：首屏一屏，之后每次追加一屏）
  int m_renderedCount = 0;
  bool m_relayoutPending = false;
  /// 正在补屏（内容不满一屏时逐屏追加），防止重入
  bool m_fillPending = false;
};

#endif // NEWSPAGE_H
