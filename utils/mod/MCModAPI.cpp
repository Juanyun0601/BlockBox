#include "MCModAPI.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QUrl>
#include <QRegularExpression>

MCModAPI::MCModAPI(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_baseUrl("https://mcmod-api.zkitefly.eu.org")
{
    loadBundledDatabase();
}

MCModAPI::~MCModAPI()
{
}

void MCModAPI::loadBundledDatabase()
{
    QFile file(":/utils/mod/mcmod_names.json");
    if (!file.open(QIODevice::ReadOnly))
        return;

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        m_bundledDb.insert(it.key().toLower(), it.value().toString());
}

void MCModAPI::setBaseUrl(const QString &url)
{
    m_baseUrl = url;
}

void MCModAPI::lookupChineseName(const QString &modId, const QString &englishName)
{
    if (englishName.isEmpty())
        return;

    // Check bundled database first (case-insensitive)
    QString key = englishName.toLower();
    if (m_bundledDb.contains(key)) {
        QString cn = m_bundledDb.value(key);
        if (cn.isEmpty())
            emit lookupFailed(modId, englishName);
        else
            emit nameResolved(modId, cn, QString());
        return;
    }

    // Check runtime cache
    if (m_cache.contains(key)) {
        CacheEntry ce = m_cache.value(key);
        if (ce.chineseName.isEmpty())
            emit lookupFailed(modId, englishName);
        else
            emit nameResolved(modId, ce.chineseName, ce.mcmodUrl);
        return;
    }

    // Avoid duplicate in-flight requests
    if (m_inFlight.contains(englishName))
        return;
    m_inFlight.insert(englishName);

    QUrl url(m_baseUrl + "/s/key=" + QUrl::toPercentEncoding(englishName));
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "BlockBox/1.0");

    QNetworkReply *reply = m_networkManager->get(request);
    PendingRequest pr;
    pr.modId = modId;
    pr.englishName = englishName;
    m_pendingRequests.insert(reply, pr);
    connect(reply, &QNetworkReply::finished, this, &MCModAPI::onReplyFinished);
}

void MCModAPI::onReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    PendingRequest pr = m_pendingRequests.take(reply);
    reply->deleteLater();
    m_inFlight.remove(pr.englishName);

    if (reply->error() != QNetworkReply::NoError) {
        m_cache.insert(pr.englishName.toLower(), {QString(), QString()});
        emit lookupFailed(pr.modId, pr.englishName);
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    QJsonObject dataObj = root["data"].toObject();
    QString chineseName = dataObj["chinese_name"].toString().trimmed();

    if (chineseName.isEmpty()) {
        // Try top-level title as fallback "[IC2] 工业时代2 (Industrial Craft 2)"
        QString title = root["title"].toString();
        if (!title.isEmpty()) {
            int bracket = title.indexOf(']');
            if (bracket > 0) {
                chineseName = title.mid(bracket + 1).trimmed();
                int paren = chineseName.indexOf(" (");
                if (paren > 0)
                    chineseName = chineseName.left(paren).trimmed();
            }
        }
    }

    // Extract MC百科 class ID and construct URL
    QString mcmodUrl;
    int classId = dataObj["class_id"].toInt();
    if (classId <= 0)
        classId = dataObj["id"].toInt();
    if (classId > 0)
        mcmodUrl = QString("https://www.mcmod.cn/class/%1.html").arg(classId);

    CacheEntry ce;
    ce.chineseName = chineseName;
    ce.mcmodUrl = mcmodUrl;
    m_cache.insert(pr.englishName.toLower(), ce);

    if (chineseName.isEmpty())
        emit lookupFailed(pr.modId, pr.englishName);
    else
        emit nameResolved(pr.modId, chineseName, mcmodUrl);
}
