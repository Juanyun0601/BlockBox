/**
 * @file   NewsDetailDialog.h
 * @brief  软件内查看官方新闻详情（不跳转浏览器）
 * @author BlockBox Team
 * @date   2026-10-24
 */

#ifndef NEWSDETAILDIALOG_H
#define NEWSDETAILDIALOG_H

class QWidget;
struct NewsEntry;

/**
 * @brief 在软件内弹出官方新闻详情
 *
 * 展示标题、分类、日期与官方提供的摘要正文；不打开任何网页。
 * @param parent 宿主窗口（弹窗会以模糊遮罩覆盖该窗口）
 * @param entry  要展示的新闻
 */
void showNewsDetailDialog(QWidget *parent, const NewsEntry &entry);

#endif // NEWSDETAILDIALOG_H
