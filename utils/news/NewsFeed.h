/**
 * @file   NewsFeed.h
 * @brief  我的世界官方新闻数据源
 * @author BlockBox Team
 * @date   2026-10-24
 *
 * 数据来自 Minecraft 官方启动器使用的新闻接口
 * （https://launchercontent.mojang.com/v2/news.json），
 * 与官方启动器「新闻」页展示的是同一批内容：
 *  - 首次打开首页时异步拉取，成功后写入本地缓存；
 *  - 下次启动先显示缓存，再按需在后台更新（默认 30 分钟内不重复请求）；
 *  - 网络不可用时保留上一次成功的数据，并如实告知用户。
 */

#ifndef NEWSFEED_H
#define NEWSFEED_H

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

/// 一条「我的世界官方新闻」
struct NewsEntry
{
  QString id;             ///< 官方文章 ID（去重、稳定标识）
  QString title;          ///< 标题（官方英文原文）
  QString summary;        ///< 摘要（官方英文原文，可能为空）
  QString category;       ///< 官方分类原文，如 "Minecraft: Java Edition"
  QString categoryLabel;  ///< 本地化后的分类名，如「Java 版」
  QDate date;             ///< 发布日期
  QString link;           ///< 官方文章地址（点击查看原文）
};

/**
 * @brief 官方新闻拉取与缓存（全局单例，主线程使用）
 *
 * 调用顺序：instance()->refresh(false) 拉起请求，
 * 成功后发出 entriesChanged()，失败发出 requestFailed()。
 */
class NewsFeed : public QObject
{
  Q_OBJECT

public:
  /// 全局唯一实例（在首次使用时创建，生命周期随程序结束）
  static NewsFeed *instance();

  const QVector<NewsEntry> &entries() const { return m_entries; }
  bool hasData() const { return !m_entries.isEmpty(); }
  bool isLoading() const { return m_loading; }
  /// 当前数据对应的拉取（或读缓存）时间
  QDateTime lastUpdated() const { return m_lastUpdated; }
  /// 最近一次失败原因（面向用户的中文描述，成功后清空）
  QString lastError() const { return m_lastError; }

  /**
   * 拉取官方新闻。
   * @param force true 表示忽略节流立即请求（用户点「刷新」）；
   *              false 表示数据仍新鲜、或刚请求过时直接复用现有内容。
   */
  void refresh(bool force = false);

  /// 官方日期 → 「M月d日」（跨年时带年份）
  static QString formatNewsDate(const QDate &date);

  /// 官方分类 → 中文分类名（未知分类原样返回）
  static QString localizeCategory(const QString &category);

signals:
  /// 拉取状态变化（开始/结束）
  void loadingChanged(bool loading);
  /// 拉到新数据（或数据与上次不同）后发出
  void entriesChanged();
  /// 本次拉取失败（已保留旧数据）
  void requestFailed(const QString &error);

private:
  explicit NewsFeed(QObject *parent = nullptr);
  ~NewsFeed() override;

  void requestNext();
  void finishWithError(const QString &error);
  void finishWithSuccess(const QByteArray &payload, const QVector<NewsEntry> &entries);
  void abortReply();

  static QVector<NewsEntry> parseEntries(const QByteArray &payload);
  static QString cacheFilePath();

  void loadCache();
  void saveCache(const QByteArray &payload) const;

  QNetworkAccessManager *m_manager = nullptr;
  QNetworkReply *m_reply = nullptr;
  quint64 m_generation = 0;   ///< 请求代号，用于丢弃过期回调
  int m_candidateIndex = 0;   ///< 当前尝试到第几个接口地址
  bool m_loading = false;

  QVector<NewsEntry> m_entries;
  QDateTime m_lastUpdated;    ///< 当前数据的拉取时间（读缓存时为文件修改时间）
  QDateTime m_lastAttempt;    ///< 最近一次请求的发起时间（失败后短时间内不重试）
  QString m_lastError;
};

#endif // NEWSFEED_H
