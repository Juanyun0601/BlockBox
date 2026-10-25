/**
 * @file   AnnouncementFeed.cpp
 * @brief  方块盒子官网公告数据源实现
 * @author BlockBox Team
 * @date   2026-10-25
 */

#include "utils/news/AnnouncementFeed.h"

#include "utils/AppVersion.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

/// 官网根目录下的公告文件（与仓库 方块盒子官网/announcement.json 对应）
const char *const kAnnouncementUrl = "https://blockbox.cc.cd/announcement.json";

/// 最多展示条数（缓存里也按此上限截断，避免文件无限长、界面翻不完）
constexpr int kMaxEntries = 20;
/// 数据新鲜期：10 分钟内重复进入首页不再请求
constexpr int kRefreshIntervalSecs = 10 * 60;
/// 失败后的最短重试间隔，避免断网时反复打接口
constexpr int kFailureRetrySecs = 60;
/// 单次请求超时
constexpr int kRequestTimeoutMs = 12000;

/// 依次尝试取值，返回第一个非空字符串
QString pickString(const QJsonObject &object, const QStringList &keys)
{
  for (const QString &key : keys)
  {
    const QJsonValue value = object.value(key);
    if (value.isString())
    {
      const QString text = value.toString().trimmed();
      if (!text.isEmpty())
        return text;
    }
  }
  return QString();
}

/// 判断这一条是否被显式关闭（"enabled": false 或 "hidden": true）
bool isDisabled(const QJsonObject &object)
{
  const QJsonValue enabled = object.value(QStringLiteral("enabled"));
  if (enabled.isBool() && !enabled.toBool())
    return true;
  const QJsonValue hidden = object.value(QStringLiteral("hidden"));
  return hidden.isBool() && hidden.toBool();
}

/// 官网站点根地址（用于把站内相对路径补全成可点击的完整链接）
const char *const kSiteOrigin = "https://blockbox.cc.cd";

/**
 * @brief 补全公告链接
 *
 * 官网里写 "/download.html"、"download.html" 这类站内相对路径时，按官网根地址补全，
 * 这样运营不用每次都写完整域名；其余协议（如 mailto:、javascript:）一律丢弃，
 * 避免公告文件被写成可执行协议。
 */
QString normalizeLink(const QString &raw)
{
  const QString link = raw.trimmed();
  if (link.isEmpty())
    return QString();

  const QUrl url(link);
  const QString scheme = url.scheme().toLower();
  if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
    return link;
  if (!scheme.isEmpty())
    return QString();   // 其他协议不认

  if (link.startsWith(QStringLiteral("//")))
    return QStringLiteral("https:") + link;
  if (link.startsWith(QLatin1Char('/')))
    return QString::fromLatin1(kSiteOrigin) + link;
  return QString::fromLatin1(kSiteOrigin) + QLatin1Char('/') + link;
}

/// 取公告数组：顶层就是数组，或对象里的 announcements / entries / items / list
QJsonArray pickArray(const QJsonDocument &doc)
{
  if (doc.isArray())
    return doc.array();

  if (!doc.isObject())
    return QJsonArray();

  const QJsonObject root = doc.object();
  const QStringList keys = {
      QStringLiteral("announcements"), QStringLiteral("entries"),
      QStringLiteral("items"), QStringLiteral("list"),
      QStringLiteral("notices"), QStringLiteral("公告"),
  };
  for (const QString &key : keys)
  {
    const QJsonValue value = root.value(key);
    if (value.isArray())
      return value.toArray();
    // 只写了一条公告、没套数组时也兼容
    if (value.isObject())
    {
      QJsonArray single;
      single.append(value);
      return single;
    }
  }
  return QJsonArray();
}

} // namespace

AnnouncementFeed::AnnouncementFeed(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
  loadCache();
}

AnnouncementFeed::~AnnouncementFeed()
{
  abortReply();
}

AnnouncementFeed *AnnouncementFeed::instance()
{
  // 只由界面线程访问；刻意不释放，避免退出阶段与网络回调的析构顺序问题
  static AnnouncementFeed *s_instance = new AnnouncementFeed();
  return s_instance;
}

// ============================================================================
// 对外接口
// ============================================================================

void AnnouncementFeed::refresh(bool force)
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
    // 刚失败过：这期间进入首页不再立刻重试
    if (m_lastAttempt.isValid() && m_lastAttempt.secsTo(now) >= 0
        && m_lastAttempt.secsTo(now) < kFailureRetrySecs)
      return;
  }

  m_loading = true;
  m_lastAttempt = now;
  m_lastError.clear();
  m_generation++;
  emit loadingChanged(true);

  QNetworkRequest request{QUrl(QString::fromLatin1(kAnnouncementUrl))};
  request.setTransferTimeout(kRequestTimeoutMs);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  // 公告要求时效性，避免命中 Qt 的 HTTP 缓存
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                       QNetworkRequest::AlwaysNetwork);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("BlockBox/%1").arg(AppVersion::current()));
  request.setRawHeader("Accept", "application/json");

  QNetworkReply *reply = m_manager->get(request);
  m_reply = reply;
  const quint64 generation = m_generation;

  connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
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
      // 官网把公告全部删掉（文件是合法 JSON 但列表为空）时，界面要跟着清空；
      // 但文件为空、或内容不是合法 JSON（发布过程中被清空 / 部署了一半）时，
      // 保留上一次的公告，避免用户眼前的内容突然消失
      QVector<AnnouncementEntry> entries;
      if (!parseEntries(payload, entries))
        finishWithError(tr("公告文件内容格式有误"));
      else
        finishWithSuccess(payload, entries);
      return;
    }
    if (status > 0)
      finishWithError(tr("公告接口返回 HTTP %1").arg(status));
    else
      finishWithError(tr("网络连接失败：%1").arg(networkErrorText));
  });
}

void AnnouncementFeed::finishWithSuccess(const QByteArray &payload,
                                         const QVector<AnnouncementEntry> &entries)
{
  // 只有内容真的变了才通知界面重建，避免每次进首页都闪一下
  bool changed = entries.size() != m_entries.size();
  for (int i = 0; !changed && i < entries.size(); ++i)
    changed = entries.at(i).id != m_entries.at(i).id;

  m_entries = entries;
  m_lastUpdated = QDateTime::currentDateTime();
  m_lastError.clear();
  saveCache(payload);

  m_loading = false;
  if (changed)
    emit entriesChanged();
  emit loadingChanged(false);
}

void AnnouncementFeed::finishWithError(const QString &error)
{
  m_loading = false;
  m_lastError = error;
  qWarning() << "[AnnouncementFeed] 获取官网公告失败:" << error;
  emit loadingChanged(false);
  emit requestFailed(error);
}

void AnnouncementFeed::abortReply()
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

/**
 * @brief 解析公告文件
 *
 * @param payload 官网公告文件的原始内容
 * @param entries 解析结果（合法但一条公告都没有时为空）
 * @return false 表示文件为空或不是合法 JSON（调用方应保留旧数据）
 */
bool AnnouncementFeed::parseEntries(const QByteArray &payload, QVector<AnnouncementEntry> &entries)
{
  entries.clear();

  if (payload.trimmed().isEmpty())
    return false;

  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError)
  {
    qWarning() << "[AnnouncementFeed] 公告 JSON 解析失败:" << parseError.errorString();
    return false;
  }

  const QJsonArray array = pickArray(doc);
  QSet<QString> seen;

  for (const QJsonValue &value : array)
  {
    AnnouncementEntry entry;

    if (value.isString())
    {
      // 极简写法：只写一句文字，不跳转
      entry.text = value.toString().simplified();
    }
    else if (value.isObject())
    {
      const QJsonObject object = value.toObject();
      if (isDisabled(object))
        continue;

      entry.text = pickString(object, {QStringLiteral("text"), QStringLiteral("content"),
                                       QStringLiteral("message"), QStringLiteral("title")})
                       .simplified();
      entry.link = normalizeLink(pickString(object, {QStringLiteral("link"), QStringLiteral("url"),
                                                    QStringLiteral("href")}));
      entry.date = pickString(object, {QStringLiteral("date"), QStringLiteral("time")});
      entry.id = pickString(object, {QStringLiteral("id")});
    }
    else
    {
      continue;
    }

    if (entry.text.isEmpty())
      continue;

    if (entry.id.isEmpty())
      entry.id = entry.link.isEmpty()
                     ? QStringLiteral("t:%1").arg(entry.text)
                     : QStringLiteral("l:%1|t:%2").arg(entry.link, entry.text);

    // 同一条（同文字 + 同链接）只保留一次，避免官网手滑重复
    if (seen.contains(entry.id))
      continue;
    seen.insert(entry.id);

    entries.append(entry);
    if (entries.size() >= kMaxEntries)
      break;
  }

  return true;
}

// ============================================================================
// 本地缓存
// ============================================================================

QString AnnouncementFeed::cacheFilePath()
{
  const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return base + QStringLiteral("/announcements/announcement.json");
}

void AnnouncementFeed::loadCache()
{
  const QString path = cacheFilePath();
  QFile file(path);
  if (!file.exists() || !file.open(QIODevice::ReadOnly))
    return;

  const QByteArray payload = file.readAll();
  file.close();

  QVector<AnnouncementEntry> entries;
  if (!parseEntries(payload, entries) || entries.isEmpty())
    return;

  m_entries = entries;
  m_lastUpdated = QFileInfo(path).lastModified();
}

void AnnouncementFeed::saveCache(const QByteArray &payload) const
{
  const QString path = cacheFilePath();
  const QFileInfo info(path);
  if (!QDir().mkpath(info.absolutePath()))
  {
    qWarning() << "[AnnouncementFeed] 无法创建公告缓存目录:" << info.absolutePath();
    return;
  }

  // 先写临时文件再替换，避免写到一半退出留下坏缓存
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
  {
    qWarning() << "[AnnouncementFeed] 无法写入公告缓存:" << path;
    return;
  }
  file.write(payload);
  if (!file.commit())
    qWarning() << "[AnnouncementFeed] 公告缓存保存失败:" << path;
}
