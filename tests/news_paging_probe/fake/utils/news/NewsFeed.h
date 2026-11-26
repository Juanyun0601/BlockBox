/**
 * @file   NewsFeed.h（探针替身）
 * @brief  新闻分屏加载探针用的假数据源：不联网，条目由探针自行注入
 */

#pragma once

#include <QDate>
#include <QObject>
#include <QString>
#include <QVector>

struct NewsEntry
{
  QString id;
  QString title;
  QString summary;
  QString category;
  QString categoryLabel;
  QDate date;
  QString link;
};

class NewsFeed : public QObject
{
  Q_OBJECT

public:
  static NewsFeed *instance();

  const QVector<NewsEntry> &entries() const { return m_entries; }
  bool hasData() const { return !m_entries.isEmpty(); }
  bool isLoading() const { return false; }
  QString lastError() const { return QString(); }
  void refresh(bool = false) {}

  static QString formatNewsDate(const QDate &date) { return date.toString(QStringLiteral("M月d日")); }
  static QString localizeCategory(const QString &category) { return category; }

  void seed(const QVector<NewsEntry> &entries)
  {
    m_entries = entries;
    emit entriesChanged();
  }

signals:
  void loadingChanged(bool loading);
  void entriesChanged();
  void requestFailed(const QString &error);

private:
  QVector<NewsEntry> m_entries;
};
