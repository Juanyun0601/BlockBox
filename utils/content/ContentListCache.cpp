#include "ContentListCache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>

ContentListCache *ContentListCache::m_instance = nullptr;

ContentListCache *ContentListCache::instance()
{
    if (!m_instance)
        m_instance = new ContentListCache();
    return m_instance;
}

ContentListCache::ContentListCache()
{
}

ContentListCache::~ContentListCache()
{
}

QString ContentListCache::makeKey(ContentType contentType, int source,
                                  const QString &gameVersion, const QString &loader,
                                  const QString &sortField, const QString &query, int page)
{
    QString raw = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                      .arg(static_cast<int>(contentType))
                      .arg(source)
                      .arg(gameVersion)
                      .arg(loader)
                      .arg(sortField)
                      .arg(query)
                      .arg(page);
    return QString::fromLatin1(
        QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString ContentListCache::cacheDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/cache/content_lists");
}

QString ContentListCache::filePathForKey(const QString &key) const
{
    return cacheDir() + QStringLiteral("/") + key + QStringLiteral(".json");
}

bool ContentListCache::load(const QString &key, ModSearchResult &result) const
{
    QFile file(filePathForKey(key));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    QJsonObject root = doc.object();
    qint64 savedAt = static_cast<qint64>(root.value("savedAt").toDouble());
    if (savedAt <= 0 || QDateTime::currentMSecsSinceEpoch() - savedAt > CACHE_TTL_MS)
        return false;

    result = modSearchResultFromJson(root.value("result").toObject());
    return true;
}

void ContentListCache::save(const QString &key, const ModSearchResult &result) const
{
    QDir dir(cacheDir());
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    QJsonObject root;
    root["savedAt"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    root["result"] = modSearchResultToJson(result);

    QFile file(filePathForKey(key));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void ContentListCache::clearAll()
{
    QDir dir(cacheDir());
    if (!dir.exists())
        return;
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &f : files)
        QFile::remove(dir.filePath(f));
}
