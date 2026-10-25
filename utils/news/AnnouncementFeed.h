/**
 * @file   AnnouncementFeed.h
 * @brief  方块盒子官网公告数据源（首页公告栏）
 * @author BlockBox Team
 * @date   2026-10-25
 *
 * 数据来自官网根目录的 announcement.json
 * （https://blockbox.cc.cd/announcement.json），由官网维护者手动编辑：
 *  - 首页显示时异步拉取，成功后写入本地缓存；
 *  - 下次启动先显示缓存，再按需在后台更新（默认 10 分钟内不重复请求）；
 *  - 断网时保留上一次成功的数据，界面照常展示旧公告；
 *  - 一条公告 = 一段文字 + 一个可选跳转链接，多条按文件里的顺序分页切换。
 *
 * 支持的 JSON 形态（见官网 README 的「公告文件」一节）：
 *   { "announcements": [ { "text": "...", "link": "https://..." } ] }
 *   [ { "text": "...", "link": "https://..." } ]
 *   { "announcements": [ "纯文字公告" ] }
 */

#ifndef ANNOUNCEMENTFEED_H
#define ANNOUNCEMENTFEED_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

/// 一条官网公告
struct AnnouncementEntry
{
  QString id;    ///< 稳定标识（用于去重，缺省时由文字 + 链接生成）
  QString text;  ///< 公告文字（单行展示，过长自动省略）
  QString link;  ///< 点击跳转地址（空表示这条公告不可点击）
  QString date;  ///< 发布日期文字（可选，仅作数据留存）
};

/**
 * @brief 官网公告的拉取与缓存（全局单例，主线程使用）
 *
 * 调用顺序：instance()->refresh(false) 拉起请求，
 * 成功后发出 entriesChanged()，失败发出 requestFailed()。
 */
class AnnouncementFeed : public QObject
{
  Q_OBJECT

public:
  /// 全局唯一实例（首次使用时创建，生命周期随程序结束）
  static AnnouncementFeed *instance();

  const QVector<AnnouncementEntry> &entries() const { return m_entries; }
  bool hasData() const { return !m_entries.isEmpty(); }
  bool isLoading() const { return m_loading; }
  /// 当前数据对应的拉取（或读缓存）时间
  QDateTime lastUpdated() const { return m_lastUpdated; }
  /// 最近一次失败原因（面向用户的中文描述，成功后清空）
  QString lastError() const { return m_lastError; }

  /**
   * 拉取官网公告。
   * @param force true 表示忽略节流立即请求；false 表示数据仍新鲜时直接复用现有内容。
   */
  void refresh(bool force = false);

signals:
  /// 拉取状态变化（开始/结束）
  void loadingChanged(bool loading);
  /// 拿到新数据（或数据与上次不同）后发出
  void entriesChanged();
  /// 本次拉取失败（已保留旧数据）
  void requestFailed(const QString &error);

private:
  explicit AnnouncementFeed(QObject *parent = nullptr);
  ~AnnouncementFeed() override;

  void finishWithSuccess(const QByteArray &payload, const QVector<AnnouncementEntry> &entries);
  void finishWithError(const QString &error);
  void abortReply();

  static bool parseEntries(const QByteArray &payload, QVector<AnnouncementEntry> &entries);
  static QString cacheFilePath();

  void loadCache();
  void saveCache(const QByteArray &payload) const;

  QNetworkAccessManager *m_manager = nullptr;
  QNetworkReply *m_reply = nullptr;
  quint64 m_generation = 0;   ///< 请求代号，用于丢弃过期回调
  bool m_loading = false;

  QVector<AnnouncementEntry> m_entries;
  QDateTime m_lastUpdated;    ///< 当前数据的拉取时间（读缓存时为文件修改时间）
  QDateTime m_lastAttempt;    ///< 最近一次请求的发起时间（失败后短时间内不重试）
  QString m_lastError;
};

#endif // ANNOUNCEMENTFEED_H
