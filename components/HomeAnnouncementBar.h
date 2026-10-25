/**
 * @file   HomeAnnouncementBar.h
 * @brief  首页公告栏（封面图 / 皮肤模型下方的一行公告）
 * @author BlockBox Team
 * @date   2026-10-25
 *
 * 内容来自官网 announcement.json（见 AnnouncementFeed）：
 *  - 一次显示一条，左侧「公告」标签固定，右侧是文字与翻页控件；
 *  - 多条公告分批切换：自动轮播 + 左右按钮手动翻，鼠标停在上面时暂停轮播；
 *  - 公告带链接时整段文字可点，点击用系统浏览器打开；
 *  - 没有任何公告（或断网且无缓存）时整条隐藏，不占用首页空间。
 */

#ifndef HOMEANNOUNCEMENTBAR_H
#define HOMEANNOUNCEMENTBAR_H

#include <QVector>
#include <QWidget>

#include "utils/news/AnnouncementFeed.h"

class QLabel;
class QPushButton;
class QTimer;
class AnnouncementTextLabel;

class HomeAnnouncementBar : public QWidget
{
  Q_OBJECT

public:
  explicit HomeAnnouncementBar(QWidget *parent = nullptr);
  ~HomeAnnouncementBar() override;

  /// 按数据源当前内容刷新；没有公告时自动隐藏整条
  void reloadFromFeed();

protected:
  void changeEvent(QEvent *event) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void buildUi();
  /// 按当前主题色重绘标签图标与翻页箭头（切换主题时跟随）
  void refreshIcons();
  /// 显示第 index 条（越界自动取模）
  void showPage(int index);
  void stepPage(int delta);
  void openCurrentLink();
  void updateNavState();
  void startAutoRotate();
  void stopAutoRotate();

  QLabel *m_badgeIcon = nullptr;
  QLabel *m_badgeText = nullptr;
  AnnouncementTextLabel *m_textLabel = nullptr;
  QLabel *m_pageLabel = nullptr;
  QPushButton *m_prevBtn = nullptr;
  QPushButton *m_nextBtn = nullptr;
  QTimer *m_rotateTimer = nullptr;

  QVector<AnnouncementEntry> m_entries;
  int m_index = 0;
  bool m_hovered = false;   ///< 鼠标停在公告上时暂停自动轮播
};

#endif // HOMEANNOUNCEMENTBAR_H
