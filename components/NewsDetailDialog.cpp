/**
 * @file   NewsDetailDialog.cpp
 * @brief  软件内查看官方新闻详情的实现
 * @author BlockBox Team
 * @date   2026-10-24
 */

#include "NewsDetailDialog.h"

#include "AppMessageBox.h"
#include "utils/news/NewsFeed.h"

#include <QStringList>

void showNewsDetailDialog(QWidget *parent, const NewsEntry &entry)
{
  AppMessageBox box(parent);
  box.setWindowTitle(entry.title);
  box.setIcon(AppMessageBox::Information);
  box.setText(entry.title);

  // 分类 · 日期，再空一行接官方摘要
  QStringList metaParts;
  if (!entry.categoryLabel.isEmpty())
    metaParts << entry.categoryLabel;
  const QString dateText = NewsFeed::formatNewsDate(entry.date);
  if (!dateText.isEmpty())
    metaParts << dateText;

  QString info = metaParts.join(QStringLiteral(" · "));
  if (!entry.summary.isEmpty())
  {
    if (!info.isEmpty())
      info += QStringLiteral("\n\n");
    info += entry.summary;
  }
  if (info.isEmpty())
    info = QStringLiteral("官方暂未提供这条新闻的摘要。");
  info += QStringLiteral("\n\n内容来自 Minecraft 官方新闻。");

  box.setInformativeText(info);
  box.setStandardButtons(AppMessageBox::Close);
  box.exec();
}
