/**
 * @file   NewsFeed.cpp
 * @brief  我的世界官方新闻数据源实现
 * @author BlockBox Team
 * @date   2026-10-24
 */

#include "utils/news/NewsFeed.h"

#include "utils/AppVersion.h"

#include <algorithm>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

namespace {

/// 官方启动器使用的新闻接口（v2 为当前版本，v1 作为兜底）
const char *const kFeedEndpoints[] = {
    "https://launchercontent.mojang.com/v2/news.json",
    "https://launchercontent.mojang.com/news.json",
};
constexpr int kEndpointCount = int(sizeof(kFeedEndpoints) / sizeof(kFeedEndpoints[0]));

/// 首页最多展示条数（缓存里也按此上限截断，避免文件无限长）
constexpr int kMaxEntries = 10;
/// 数据新鲜期：30 分钟内重复进入首页不再请求
constexpr int kRefreshIntervalSecs = 30 * 60;
/// 失败后的最短重试间隔，避免断网时反复打接口
constexpr int kFailureRetrySecs = 60;
/// 单次请求超时
constexpr int kRequestTimeoutMs = 15000;

QDate parseOfficialDate(const QString &text)
{
  const QString dateText = text.trimmed().left(10);
  if (dateText.isEmpty())
    return QDate();
  QDate date = QDate::fromString(dateText, Qt::ISODate);
  if (!date.isValid())
    date = QDate::fromString(text.trimmed(), Qt::ISODate);
  return date;
}

} // namespace

NewsFeed::NewsFeed(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
  loadCache();
}

NewsFeed::~NewsFeed()
{
  abortReply();
}

NewsFeed *NewsFeed::instance()
{
  // 只由界面线程访问；刻意不释放，避免退出阶段与网络回调的析构顺序问题
  static NewsFeed *s_instance = new NewsFeed();
  return s_instance;
}

// ============================================================================
// 对外接口
// ============================================================================

void NewsFeed::refresh(bool force)
{
  if (m_loading)
    return;

  const QDateTime now = QDateTime::currentDateTime();
  if (!force)
  {
    const bool fresh = m_lastUpdated.isValid() && m_lastUpdated.secsTo(now) >= 0
                       && m_lastUpdated.secsTo(now) < kRefreshIntervalSecs;
    if (fresh && hasData())
      return;
    // 刚失败过：这期间进入首页不再立刻重试，把请求留给用户手动刷新
    if (m_lastAttempt.isValid() && m_lastAttempt.secsTo(now) >= 0
        && m_lastAttempt.secsTo(now) < kFailureRetrySecs)
      return;
  }

  m_loading = true;
  m_lastAttempt = now;
  m_candidateIndex = 0;
  m_lastError.clear();
  m_generation++;
  emit loadingChanged(true);
  requestNext();
}

QString NewsFeed::formatNewsDate(const QDate &date)
{
  if (!date.isValid())
    return QString();

  const QDate today = QDate::currentDate();
  if (date.year() == today.year())
    return QStringLiteral("%1月%2日").arg(date.month()).arg(date.day());
  return QStringLiteral("%1年%2月%3日").arg(date.year()).arg(date.month()).arg(date.day());
}

QString NewsFeed::localizeCategory(const QString &category)
{
  static const QHash<QString, QString> kLabels = {
      {QStringLiteral("Minecraft: Java Edition"), QStringLiteral("Java 版")},
      {QStringLiteral("Minecraft for Windows"), QStringLiteral("基岩版")},
      {QStringLiteral("Minecraft: Bedrock Edition"), QStringLiteral("基岩版")},
      {QStringLiteral("Minecraft Dungeons II"), QStringLiteral("地下城 II")},
      {QStringLiteral("Minecraft Dungeons"), QStringLiteral("地下城")},
      {QStringLiteral("Minecraft Legends"), QStringLiteral("传奇")},
      {QStringLiteral("Minecraft Education"), QStringLiteral("教育版")},
  };

  const QString trimmed = category.trimmed();
  return kLabels.value(trimmed, trimmed);
}

// ============================================================================
// 网络请求
// ============================================================================

void NewsFeed::requestNext()
{
  if (m_candidateIndex >= kEndpointCount)
  {
    finishWithError(m_lastError.isEmpty()
                        ? tr("暂时无法获取官方新闻，请检查网络后重试")
                        : m_lastError);
    return;
  }

  const QString url = QString::fromLatin1(kFeedEndpoints[m_candidateIndex++]);

  QNetworkRequest request{QUrl(url)};
  request.setTransferTimeout(kRequestTimeoutMs);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  // 新闻要求实时性，避免命中 Qt 的 HTTP 缓存
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                       QNetworkRequest::AlwaysNetwork);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("BlockBox/%1").arg(AppVersion::current()));
  request.setRawHeader("Accept", "application/json");

  QNetworkReply *reply = m_manager->get(request);
  m_reply = reply;
  const quint64 generation = m_generation;

  connect(reply, &QNetworkReply::finished, this, [this, reply, url, generation]() {
    if (generation != m_generation)
    {
      reply->deleteLater();
      return;
    }
    if (m_reply == reply)
      m_reply = nullptr;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (networkError == QNetworkReply::NoError && status == 200)
    {
      const QVector<NewsEntry> entries = parseEntries(payload);
      if (!entries.isEmpty())
      {
        finishWithSuccess(payload, entries);
        return;
      }
      m_lastError = tr("官方新闻接口返回了无法识别的数据");
      qWarning() << "[NewsFeed] 无法解析新闻数据:" << url;
    }
    else if (status > 0)
    {
      m_lastError = tr("官方新闻接口返回 HTTP %1").arg(status);
      qWarning() << "[NewsFeed] 请求失败:" << url << "HTTP" << status;
    }
    else
    {
      m_lastError = tr("网络连接失败：%1").arg(networkErrorText);
      qWarning() << "[NewsFeed] 网络错误:" << url << networkErrorText;
    }

    // 还有备用地址就继续尝试
    requestNext();
  });
}

void NewsFeed::finishWithSuccess(const QByteArray &payload, const QVector<NewsEntry> &entries)
{
  m_entries = entries;
  m_lastUpdated = QDateTime::currentDateTime();
  m_lastError.clear();
  saveCache(payload);

  m_loading = false;
  emit entriesChanged();
  emit loadingChanged(false);
}

void NewsFeed::finishWithError(const QString &error)
{
  m_loading = false;
  m_lastError = error;
  qWarning() << "[NewsFeed] 获取官方新闻失败:" << error;
  emit loadingChanged(false);
  emit requestFailed(error);
}

void NewsFeed::abortReply()
{
  if (!m_reply)
    return;

  QNetworkReply *reply = m_reply;
  m_reply = nullptr;
  reply->disconnect(this);
  reply->abort();
  reply->deleteLater();
}

// ============================================================================
// 解析
// ============================================================================

QVector<NewsEntry> NewsFeed::parseEntries(const QByteArray &payload)
{
  QVector<NewsEntry> entries;

  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    return entries;

  const QJsonArray array = doc.object().value(QStringLiteral("entries")).toArray();
  for (const QJsonValue &value : array)
  {
    const QJsonObject object = value.toObject();
    const QString title = object.value(QStringLiteral("title")).toString().simplified();
    if (title.isEmpty())
      continue;

    // 官方文章链接：新版字段是 readMoreLink，老版本偶有 newsPageLink
    QString link = object.value(QStringLiteral("readMoreLink")).toString().trimmed();
    if (link.isEmpty())
      link = object.value(QStringLiteral("newsPageLink")).toString().trimmed();
    if (link.isEmpty())
      continue;

    NewsEntry entry;
    entry.title = title;
    entry.summary = object.value(QStringLiteral("text")).toString().simplified();
    entry.category = object.value(QStringLiteral("category")).toString().trimmed();
    entry.categoryLabel = localizeCategory(entry.category);
    entry.date = parseOfficialDate(object.value(QStringLiteral("date")).toString());
    entry.link = link;
    entry.id = object.value(QStringLiteral("id")).toString().trimmed();
    if (entry.id.isEmpty())
      entry.id = link;
    entries.append(entry);
  }

  // 按发布日期倒序（日期缺失的排到最后）
  std::stable_sort(entries.begin(), entries.end(),
                   [](const NewsEntry &a, const NewsEntry &b) {
                     const QDate left = a.date.isValid() ? a.date : QDate(1970, 1, 1);
                     const QDate right = b.date.isValid() ? b.date : QDate(1970, 1, 1);
                     return left > right;
                   });

  // 同一个链接（同一篇文章）只保留最新的一条
  QVector<NewsEntry> result;
  QSet<QString> seenLinks;
  for (const NewsEntry &entry : entries)
  {
    if (seenLinks.contains(entry.link))
      continue;
    seenLinks.insert(entry.link);
    result.append(entry);
    if (result.size() >= kMaxEntries)
      break;
  }
  return result;
}

// ============================================================================
// 本地缓存
// ============================================================================

QString NewsFeed::cacheFilePath()
{
  const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return base + QStringLiteral("/news/official_news.json");
}

void NewsFeed::loadCache()
{
  const QString path = cacheFilePath();
  QFile file(path);
  if (!file.exists() || !file.open(QIODevice::ReadOnly))
    return;

  const QByteArray payload = file.readAll();
  file.close();

  const QVector<NewsEntry> entries = parseEntries(payload);
  if (entries.isEmpty())
    return;

  m_entries = entries;
  m_lastUpdated = QFileInfo(path).lastModified();
}

void NewsFeed::saveCache(const QByteArray &payload) const
{
  const QString path = cacheFilePath();
  const QFileInfo info(path);
  if (!QDir().mkpath(info.absolutePath()))
  {
    qWarning() << "[NewsFeed] 无法创建新闻缓存目录:" << info.absolutePath();
    return;
  }

  // 先写临时文件再替换，避免写到一半退出留下坏缓存
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
  {
    qWarning() << "[NewsFeed] 无法写入新闻缓存:" << path;
    return;
  }
  file.write(payload);
  if (!file.commit())
    qWarning() << "[NewsFeed] 新闻缓存保存失败:" << path;
}
