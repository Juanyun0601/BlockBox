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
  int m_builtCardWidth = -1;
  int m_builtColumns = -1;
  bool m_relayoutPending = false;
};

#endif // NEWSPAGE_H
