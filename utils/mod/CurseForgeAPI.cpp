#include "CurseForgeAPI.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>

CurseForgeAPI::CurseForgeAPI(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_baseUrl("https://api.curseforge.com")
    , m_classId("6")
    , m_currentSearchReply(nullptr)
    , m_currentCategoriesReply(nullptr)
    , m_currentDetailReply(nullptr)
    , m_currentFingerprintReply(nullptr)
{
    m_networkManager->setTransferTimeout(10000);
    QNetworkDiskCache *diskCache = new QNetworkDiskCache(this);
    diskCache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/curseforge");
    diskCache->setMaximumCacheSize(20 * 1024 * 1024);
    m_networkManager->setCache(diskCache);
}

CurseForgeAPI::~CurseForgeAPI()
{
}

void CurseForgeAPI::setApiKey(const QString &key)
{
    m_apiKey = key;
}

void CurseForgeAPI::setBaseUrl(const QString &url)
{
    m_baseUrl = url;
}

void CurseForgeAPI::setClassId(const QString &classId)
{
    m_classId = classId;
}

void CurseForgeAPI::setGameId(const QString &gameId)
{
    m_gameId = gameId;
}

void CurseForgeAPI::searchMods(const QString &query, const QString &gameVersion,
                                const QString &categoryId, int page,
                                int pageSize, const QString &sortField,
                                const QString &sortOrder)
{
    if (m_apiKey.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            emit searchFailed(tr("未配置 CurseForge API Key，请在设置中填入"));
        });
        return;
    }

    QUrl url(m_baseUrl + "/v1/mods/search");
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("gameId", m_gameId);
    if (!m_classId.isEmpty())
        urlQuery.addQueryItem("classId", m_classId);
    if (!query.isEmpty())
        urlQuery.addQueryItem("searchFilter", query);
    if (!gameVersion.isEmpty())
        urlQuery.addQueryItem("gameVersion", gameVersion);
    if (!categoryId.isEmpty())
        urlQuery.addQueryItem("categoryId", categoryId);
    urlQuery.addQueryItem("index", QString::number(page * pageSize));
    urlQuery.addQueryItem("pageSize", QString::number(pageSize));

    QMap<QString, QString> sortMap;
    sortMap["popularity"] = "2";
    sortMap["name"] = "4";
    sortMap["last_updated"] = "3";
    sortMap["total_downloads"] = "6";
    sortMap["date_created"] = "1";
    sortMap["author"] = "5";
    urlQuery.addQueryItem("sortField", sortMap.value(sortField, "2"));

    urlQuery.addQueryItem("sortOrder", sortOrder == "asc" ? "asc" : "desc");
    url.setQuery(urlQuery);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    if (!m_apiKey.isEmpty())
        request.setRawHeader("X-API-KEY", m_apiKey.toUtf8());

    abortReply(m_currentSearchReply);
    m_currentSearchReply = m_networkManager->get(request);
    connect(m_currentSearchReply, &QNetworkReply::finished, this, &CurseForgeAPI::onSearchReplyFinished);
}

void CurseForgeAPI::getCategories()
{
    if (m_apiKey.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            emit categoriesFailed(tr("未配置 CurseForge API Key，请在设置中填入"));
        });
        return;
    }

    QUrl url(m_baseUrl + "/v1/categories");
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("gameId", m_gameId);
    url.setQuery(urlQuery);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    if (!m_apiKey.isEmpty())
        request.setRawHeader("X-API-KEY", m_apiKey.toUtf8());

    abortReply(m_currentCategoriesReply);
    m_currentCategoriesReply = m_networkManager->get(request);
    connect(m_currentCategoriesReply, &QNetworkReply::finished, this, &CurseForgeAPI::onCategoriesReplyFinished);
}

QStringList CurseForgeAPI::sortFields()
{
    return {"popularity", "name", "last_updated", "total_downloads", "date_created", "author"};
}

QStringList CurseForgeAPI::sortOrders()
{
    return {"desc", "asc"};
}

void CurseForgeAPI::abortReply(QNetworkReply *&reply)
{
    if (reply) {
        reply->disconnect();
        reply->abort();
        reply->deleteLater();
        reply = nullptr;
    }
}

void CurseForgeAPI::onSearchReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply != m_currentSearchReply) return;
    m_currentSearchReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        emit searchFailed(tr("Invalid JSON response from server"));
        return;
    }
    QJsonObject root = doc.object();
    QJsonArray dataArr = root["data"].toArray();

    ModSearchResult result;
    QJsonObject pagination = root["pagination"].toObject();
    result.totalHits = pagination["totalCount"].toInt();
    result.offset = pagination["index"].toInt();
    result.hasMore = (result.offset + dataArr.size()) < result.totalHits;

    for (const QJsonValue &val : dataArr) {
        QJsonObject obj = val.toObject();
        ModInfo info;
        parseAddon(obj, info);
        result.mods.append(info);
    }

    emit searchCompleted(result);
}

void CurseForgeAPI::onCategoriesReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply != m_currentCategoriesReply) return;
    m_currentCategoriesReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit categoriesFailed(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        emit categoriesFailed(tr("Invalid JSON response from server"));
        return;
    }
    QJsonObject root = doc.object();
    QJsonArray dataArr = root["data"].toArray();

    QList<ModCategory> categories;
    for (const QJsonValue &val : dataArr) {
        QJsonObject obj = val.toObject();
        if (obj["classId"].toInt() != m_classId.toInt()) continue;
        ModCategory cat;
        cat.id = QString::number(obj["id"].toInt());
        cat.name = obj["name"].toString();
        cat.parentId = QString::number(obj["parentCategoryId"].toInt());
        categories.append(cat);
    }

    emit categoriesLoaded(categories);
}

void CurseForgeAPI::fetchModDetail(const QString &modId)
{
    if (m_apiKey.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            emit modDetailFailed(tr("未配置 CurseForge API Key"));
        });
        return;
    }

    QUrl url(m_baseUrl + "/v1/mods/" + modId);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    if (!m_apiKey.isEmpty())
        request.setRawHeader("X-API-KEY", m_apiKey.toUtf8());

    abortReply(m_currentDetailReply);
    m_currentDetailReply = m_networkManager->get(request);
    connect(m_currentDetailReply, &QNetworkReply::finished, this, &CurseForgeAPI::onDetailReplyFinished);
}

void CurseForgeAPI::fingerprintMod(quint32 fingerprint)
{
    if (m_apiKey.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            emit fingerprintFailed(tr("未配置 CurseForge API Key，请在设置中填入"));
        });
        return;
    }

    if (fingerprint == 0) {
        QTimer::singleShot(0, this, [this]() {
            emit fingerprintFailed(tr("指纹值为空"));
        });
        return;
    }

    QUrl url(m_baseUrl + "/v1/fingerprints/432");
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Content-Type", "application/json");
    if (!m_apiKey.isEmpty())
        request.setRawHeader("X-API-KEY", m_apiKey.toUtf8());

    // 构建 JSON payload: {"fingerprints": [hash]}
    QJsonObject payload;
    QJsonArray fpArr;
    fpArr.append(static_cast<qint64>(fingerprint));
    payload["fingerprints"] = fpArr;

    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    m_pendingFingerprint = fingerprint;
    abortReply(m_currentFingerprintReply);
    m_currentFingerprintReply = m_networkManager->post(request, body);
    connect(m_currentFingerprintReply, &QNetworkReply::finished,
            this, &CurseForgeAPI::onFingerprintReplyFinished);
}

void CurseForgeAPI::onDetailReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply != m_currentDetailReply) return;
    m_currentDetailReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit modDetailFailed(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        emit modDetailFailed(tr("Invalid JSON response from server"));
        return;
    }
    QJsonObject root = doc.object();
    QJsonObject obj = root["data"].toObject();

    ModInfo info;
    info.id = QString::number(obj["id"].toInt());
    info.name = obj["name"].toString();
    info.description = obj["summary"].toString();
    info.detailedDescription = obj["body"].toString();
    info.author = obj["authors"].toArray().first().toObject()["name"].toString();
    info.pageUrl = obj["links"].toObject()["websiteUrl"].toString();
    info.source = "curseforge";
    info.downloadCount = obj["downloadCount"].toInt();
    info.followers = obj["follows"].toInt();

    QJsonObject logo = obj["logo"].toObject();
    info.iconUrl = logo["thumbnailUrl"].toString();
    if (info.iconUrl.isEmpty())
        info.iconUrl = logo["url"].toString();   // thumbnailUrl 缺失时回退原图
    info.coverUrl = logo["url"].toString();

    QJsonArray gameVersions = obj["gameVersions"].toArray();
    for (const QJsonValue &v : gameVersions)
        info.gameVersions.append(v.toString());

    QJsonArray categoriesArr = obj["categories"].toArray();
    for (const QJsonValue &v : categoriesArr)
        info.categories.append(v.toObject()["name"].toString());

    // Parse all version files
    QJsonArray latestFiles = obj["latestFiles"].toArray();
    for (int fi = 0; fi < latestFiles.size(); ++fi) {
        QJsonObject file = latestFiles[fi].toObject();
        if (file.isEmpty()) continue;

        ModVersionFile vf;
        vf.version = file["displayName"].toString();
        vf.downloadUrl = file["downloadUrl"].toString();
        vf.fileName = file["fileName"].toString();
        vf.fileSize = file["fileLength"].toInt();

        QJsonArray modLoaders = file["modLoaders"].toArray();
        for (const QJsonValue &v : modLoaders)
            vf.loaders.append(v.toString());

        QJsonArray fileGameVersions = file["gameVersions"].toArray();
        for (const QJsonValue &v : fileGameVersions)
            vf.gameVersions.append(v.toString());

        QString fileDate = file["fileDate"].toString();
        if (!fileDate.isEmpty())
            vf.datePublished = QDateTime::fromString(fileDate, Qt::ISODate);

        int rt = file["releaseType"].toInt();
        switch (rt) {
            case 2: vf.releaseType = "beta"; break;
            case 3: vf.releaseType = "alpha"; break;
            default: vf.releaseType = "release"; break;
        }

        // Parse dependencies of this file (CurseForge: relationType 3 = required)
        QJsonArray fileDeps = file["dependencies"].toArray();
        for (const QJsonValue &val : fileDeps)
        {
            QJsonObject d = val.toObject();
            ModDependency dep;
            dep.name = QString::number(d["modId"].toInt());
            dep.isRequired = (d["relationType"].toInt() == 3);
            vf.dependencies.append(dep);
        }

        info.versionFiles.append(vf);

        // Use the first file as "latest" for backward compatibility
        if (fi == 0) {
            info.latestVersion = vf.version;
            info.downloadUrl = vf.downloadUrl;
            info.loaders = vf.loaders;
            info.gameVersions = vf.gameVersions;
        }
    }

    QString dateStr = obj["dateModified"].toString();
    if (!dateStr.isEmpty())
        info.dateModified = QDateTime::fromString(dateStr, Qt::ISODate);

    // Parse screenshots
    QJsonArray screenshots = obj["screenshots"].toArray();
    for (const QJsonValue &val : screenshots) {
        QJsonObject s = val.toObject();
        ModScreenshot shot;
        shot.url = s["url"].toString();
        shot.description = s["title"].toString();
        info.screenshots.append(shot);
    }

    // 用第一个（latest）文件的依赖回填 ModInfo.dependencies，便于详情页 UI 显示
    if (!info.versionFiles.isEmpty())
        info.dependencies = info.versionFiles.first().dependencies;

    emit modDetailReceived(info);
}

void CurseForgeAPI::onFingerprintReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply != m_currentFingerprintReply) return;
    m_currentFingerprintReply = nullptr;

    quint32 sentFp = m_pendingFingerprint;
    m_pendingFingerprint = 0;

    if (reply->error() != QNetworkReply::NoError) {
        emit fingerprintFailed(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        emit fingerprintFailed(tr("Invalid JSON response from server"));
        return;
    }
    QJsonObject root = doc.object();
    QJsonObject dataObj = root["data"].toObject();
    QJsonArray exactMatches = dataObj["exactMatches"].toArray();
    QJsonArray unmatchedArr = dataObj["unmatchedFingerprints"].toArray();

    // 检查我们的指纹是否在 unmatched 中
    bool matched = true;
    for (const QJsonValue &v : unmatchedArr) {
        if (v.toInt() == static_cast<qint64>(sentFp)) {
            matched = false;
            break;
        }
    }

    if (!matched || exactMatches.isEmpty()) {
        emit fingerprintFailed(tr("指纹匹配失败"));
        return;
    }

    // 取 exactMatches 中的第一个（因为只发了一个 fingerprint）
    QJsonObject match = exactMatches.first().toObject();
    QJsonObject file = match["file"].toObject();
    // 优先从 file.modId 取项目 ID，兜底用 match.id
    int modId = file["modId"].toInt();
    if (modId <= 0)
        modId = match["id"].toInt();
    if (modId <= 0) {
        emit fingerprintFailed(tr("指纹匹配返回无效的项目 ID"));
        return;
    }

    QString pageUrl = QString("https://www.curseforge.com/minecraft/mc-mods/%1").arg(modId);
    emit fingerprintCompleted(sentFp, QString::number(modId), pageUrl);
}

void CurseForgeAPI::parseAddon(const QJsonObject &obj, ModInfo &info)
{
    info.id = QString::number(obj["id"].toInt());
    info.name = obj["name"].toString();
    info.description = obj["summary"].toString();
    info.author = obj["authors"].toArray().first().toObject()["name"].toString();
    info.pageUrl = obj["links"].toObject()["websiteUrl"].toString();
    info.source = "curseforge";
    info.downloadCount = obj["downloadCount"].toInt();
    info.followers = obj["follows"].toInt();

    QJsonObject logo = obj["logo"].toObject();
    info.iconUrl = logo["thumbnailUrl"].toString();
    if (info.iconUrl.isEmpty())
        info.iconUrl = logo["url"].toString();   // thumbnailUrl 缺失时回退原图
    info.coverUrl = logo["url"].toString();

    QJsonArray gameVersions = obj["gameVersions"].toArray();
    for (const QJsonValue &v : gameVersions)
        info.gameVersions.append(v.toString());

    QJsonArray categories = obj["categories"].toArray();
    for (const QJsonValue &v : categories)
        info.categories.append(v.toObject()["name"].toString());

    QJsonObject latestFile = obj["latestFiles"].toArray().first().toObject();
    if (!latestFile.isEmpty()) {
        info.latestVersion = latestFile["displayName"].toString();
        info.downloadUrl = latestFile["downloadUrl"].toString();
        QJsonArray modLoaders = latestFile["modLoaders"].toArray();
        for (const QJsonValue &v : modLoaders)
            info.loaders.append(v.toString());
        QJsonArray fileGameVersions = latestFile["gameVersions"].toArray();
        if (!fileGameVersions.isEmpty()) {
            QStringList versions;
            for (const QVariant &v : fileGameVersions.toVariantList())
                versions.append(v.toString());
            info.gameVersions = versions;
        }
    }

    QString dateStr = obj["dateModified"].toString();
    if (!dateStr.isEmpty())
        info.dateModified = QDateTime::fromString(dateStr, Qt::ISODate);
}
