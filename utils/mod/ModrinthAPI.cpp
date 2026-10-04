#include "ModrinthAPI.h"

#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QSet>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <QUrlQuery>

ModrinthAPI::ModrinthAPI(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_baseUrl("https://api.modrinth.com")
    , m_projectType("mod")
    , m_currentSearchReply(nullptr)
    , m_currentCategoriesReply(nullptr)
    , m_currentDetailReply(nullptr)
    , m_currentVersionsReply(nullptr)
    , m_currentHashMatchReply(nullptr)
    , m_currentHashBatchReply(nullptr)
    , m_currentUpdateCheckReply(nullptr)
{
    m_networkManager->setTransferTimeout(10000);
    QNetworkDiskCache *diskCache = new QNetworkDiskCache(this);
    diskCache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/modrinth");
    diskCache->setMaximumCacheSize(20 * 1024 * 1024);
    m_networkManager->setCache(diskCache);
}

ModrinthAPI::~ModrinthAPI()
{
}

void ModrinthAPI::setBaseUrl(const QString &url)
{
    m_baseUrl = url;
}

void ModrinthAPI::setProjectType(const QString &projectType)
{
    m_projectType = projectType;
}

void ModrinthAPI::searchMods(const QString &query, const QString &gameVersion,
                              const QString &categoryId, int page,
                              int pageSize, const QString &sortField,
                              const QString &sortOrder)
{
    Q_UNUSED(sortOrder);
    QUrl url(m_baseUrl + "/v2/search");
    QUrlQuery urlQuery;
    if (!query.isEmpty())
        urlQuery.addQueryItem("query", query);
    urlQuery.addQueryItem("offset", QString::number(page * pageSize));
    urlQuery.addQueryItem("limit", QString::number(pageSize));

    QMap<QString, QString> sortMap;
    sortMap["relevance"] = "relevance";
    sortMap["popularity"] = "downloads";
    sortMap["name"] = "name";
    sortMap["last_updated"] = "updated";
    sortMap["date_created"] = "newest";
    urlQuery.addQueryItem("index", sortMap.value(sortField, "relevance"));

    QStringList facets;
    facets << QString("[\"project_type:%1\"]").arg(m_projectType);
    if (!gameVersion.isEmpty())
        facets << QString("[\"versions:%1\"]").arg(gameVersion);
    if (!categoryId.isEmpty())
        facets << QString("[\"categories:%1\"]").arg(categoryId);
    urlQuery.addQueryItem("facets", "[" + facets.join(",") + "]");

    url.setQuery(urlQuery);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "BlockBox/1.0");

    abortReply(m_currentSearchReply);
    m_currentSearchReply = m_networkManager->get(request);
    connect(m_currentSearchReply, &QNetworkReply::finished, this, &ModrinthAPI::onSearchReplyFinished);
}

void ModrinthAPI::getCategories()
{
    QUrl url(m_baseUrl + "/v2/tag/category");

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "BlockBox/1.0");

    abortReply(m_currentCategoriesReply);
    m_currentCategoriesReply = m_networkManager->get(request);
    connect(m_currentCategoriesReply, &QNetworkReply::finished, this, &ModrinthAPI::onCategoriesReplyFinished);
}

void ModrinthAPI::abortReply(QNetworkReply *&reply)
{
    if (reply) {
        reply->disconnect();
        reply->abort();
        reply->deleteLater();
        reply = nullptr;
    }
}

QStringList ModrinthAPI::sortFields()
{
    return {"relevance", "popularity", "name", "last_updated", "date_created"};
}

QStringList ModrinthAPI::sortOrders()
{
    return {"desc", "asc"};
}

void ModrinthAPI::onSearchReplyFinished()
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
    QJsonArray hits = root["hits"].toArray();

    ModSearchResult result;
    result.totalHits = root["total_hits"].toInt();
    result.offset = root["offset"].toInt();
    result.hasMore = (result.offset + hits.size()) < result.totalHits;

    for (const QJsonValue &val : hits) {
        QJsonObject obj = val.toObject();
        ModInfo info;
        info.id = obj["project_id"].toString();
        info.name = obj["title"].toString();
        info.description = obj["description"].toString();
        info.author = obj["author"].toString();
        info.iconUrl = obj["icon_url"].toString();
        // 搜索接口含 gallery（URL 数组）：封面用首张截图，无则留空 → 渐变占位
        {
            const QJsonArray galleryArr = obj["gallery"].toArray();
            info.coverUrl = galleryArr.isEmpty() ? QString()
                                                 : galleryArr.first().toString();
        }
        info.pageUrl = QString("https://modrinth.com/mod/%1").arg(obj["slug"].toString());
        info.source = "modrinth";
        info.downloadCount = obj["downloads"].toInt();
        info.followers = obj["follows"].toInt();
        info.latestVersion = obj["latest_version"].toString();

        QJsonArray categories = obj["categories"].toArray();
        for (const QJsonValue &v : categories)
            info.categories.append(v.toString());

        QJsonArray versions = obj["versions"].toArray();
        for (const QJsonValue &v : versions)
            info.gameVersions.append(v.toString());

        QJsonArray loaders = obj["loaders"].toArray();
        for (const QJsonValue &v : loaders)
            info.loaders.append(v.toString());

        QString dateStr = obj["date_modified"].toString();
        if (!dateStr.isEmpty())
            info.dateModified = QDateTime::fromString(dateStr, Qt::ISODate);

        result.mods.append(info);
    }

    emit searchCompleted(result);
}

void ModrinthAPI::fetchModDetail(const QString &modId)
{
    QUrl url(m_baseUrl + "/v2/project/" + modId);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "BlockBox/1.0");

    abortReply(m_currentDetailReply);
    abortReply(m_currentVersionsReply);
    m_currentDetailReply = m_networkManager->get(request);
    connect(m_currentDetailReply, &QNetworkReply::finished, this, &ModrinthAPI::onDetailReplyFinished);
}

void ModrinthAPI::onDetailReplyFinished()
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
    QJsonObject obj = doc.object();

    ModInfo info;
    info.id = obj["id"].toString();
    info.name = obj["title"].toString();
    info.description = obj["description"].toString();
    {
        QString md = obj["body"].toString();
        if (!md.isEmpty()) {
            QTextDocument doc;
            doc.setMarkdown(md);
            info.detailedDescription = doc.toHtml();
        }
    }
    info.author = obj["author"].toString();
    info.iconUrl = obj["icon_url"].toString();
    info.pageUrl = QString("https://modrinth.com/mod/%1").arg(obj["slug"].toString());
    info.source = "modrinth";
    info.downloadCount = obj["downloads"].toInt();
    info.followers = obj["followers"].toInt();

    QJsonArray categories = obj["categories"].toArray();
    for (const QJsonValue &v : categories)
        info.categories.append(v.toString());

    QJsonArray versions = obj["versions"].toArray();
    for (const QJsonValue &v : versions)
        info.gameVersions.append(v.toString());

    QJsonArray loaders = obj["loaders"].toArray();
    for (const QJsonValue &v : loaders)
        info.loaders.append(v.toString());

    QString dateStr = obj["updated"].toString();
    if (!dateStr.isEmpty())
        info.dateModified = QDateTime::fromString(dateStr, Qt::ISODate);

    dateStr = obj["published"].toString();
    if (!dateStr.isEmpty())
        info.datePublished = QDateTime::fromString(dateStr, Qt::ISODate);

    QJsonArray gallery = obj["gallery"].toArray();
    for (const QJsonValue &val : gallery) {
        QJsonObject g = val.toObject();
        ModScreenshot shot;
        shot.url = g["url"].toString();
        shot.description = g["title"].toString();
        info.screenshots.append(shot);
    }

    // 封面优先用 gallery 首张大图（瀑布流 banner），无则回退图标
    info.coverUrl = info.screenshots.isEmpty()
                        ? obj["icon_url"].toString()
                        : info.screenshots.first().url;

    // Save partial info and fetch version files
    m_pendingDetailInfo = info;
    fetchModVersions(info.id);
}

void ModrinthAPI::fetchModVersions(const QString &modId)
{
    QUrl url(m_baseUrl + "/v2/project/" + modId + "/version");
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "BlockBox/1.0");

    abortReply(m_currentVersionsReply);
    m_currentVersionsReply = m_networkManager->get(request);
    connect(m_currentVersionsReply, &QNetworkReply::finished, this, &ModrinthAPI::onVersionsReplyFinished);
}

void ModrinthAPI::onVersionsReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply != m_currentVersionsReply) return;
    m_currentVersionsReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        // Emit partial info even if versions fail
        emit modDetailReceived(m_pendingDetailInfo);
        m_pendingDetailInfo = ModInfo();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        emit modDetailReceived(m_pendingDetailInfo);
        m_pendingDetailInfo = ModInfo();
        return;
    }
    QJsonArray arr = doc.array();

    // Populate versionFiles from Modrinth's version array
    for (const QJsonValue &val : arr) {
        QJsonObject versionObj = val.toObject();
        ModVersionFile vf;
        vf.version = versionObj["name"].toString();
        vf.fileName = versionObj["version_number"].toString();

        // game versions
        QJsonArray gameVersions = versionObj["game_versions"].toArray();
        for (const QJsonValue &v : gameVersions)
            vf.gameVersions.append(v.toString());

        // loaders
        QJsonArray loaders = versionObj["loaders"].toArray();
        for (const QJsonValue &v : loaders)
            vf.loaders.append(v.toString());

        // files (download URL from first file)
        QJsonArray files = versionObj["files"].toArray();
        if (!files.isEmpty()) {
            QJsonObject fileObj = files[0].toObject();
            vf.downloadUrl = fileObj["url"].toString();
            vf.fileSize = fileObj["size"].toInt();
            vf.sha1 = fileObj["hashes"].toObject()["sha1"].toString();
            QString fname = fileObj["filename"].toString();
            vf.fileName = fname.isEmpty() ? versionObj["version_number"].toString() : fname;
        }

        QString dateStr = versionObj["date_published"].toString();
        if (!dateStr.isEmpty())
            vf.datePublished = QDateTime::fromString(dateStr, Qt::ISODate);

        vf.releaseType = versionObj["version_type"].toString();

        // Parse dependencies (Modrinth: dependency_type ∈ required/optional/incompatible)
        QJsonArray depsArr = versionObj["dependencies"].toArray();
        for (const QJsonValue &dv : depsArr)
        {
            QJsonObject d = dv.toObject();
            ModDependency dep;
            dep.name = d["project_id"].toString();
            dep.version = d["version_type"].toString();
            dep.isRequired = (d["dependency_type"].toString() == QStringLiteral("required"));
            if (!dep.name.isEmpty())
                vf.dependencies.append(dep);
        }

        m_pendingDetailInfo.versionFiles.append(vf);

        // Use first version as "latest" for backward compatibility
        if (m_pendingDetailInfo.versionFiles.size() == 1) {
            m_pendingDetailInfo.latestVersion = vf.version;
            m_pendingDetailInfo.downloadUrl = vf.downloadUrl;
        }
    }

    // Sort by date descending (newest first)
    std::sort(m_pendingDetailInfo.versionFiles.begin(), m_pendingDetailInfo.versionFiles.end(),
        [](const ModVersionFile &a, const ModVersionFile &b) {
            return a.datePublished > b.datePublished;
        });

    // 用最新版本的依赖回填 ModInfo.dependencies，便于详情页 UI 显示
    if (!m_pendingDetailInfo.versionFiles.isEmpty())
        m_pendingDetailInfo.dependencies = m_pendingDetailInfo.versionFiles.first().dependencies;

    emit modDetailReceived(m_pendingDetailInfo);
    m_pendingDetailInfo = ModInfo();
}

void ModrinthAPI::matchHash(const QString &sha1)
{
    if (sha1.isEmpty()) {
        QTimer::singleShot(0, this, [this, sha1]() {
            emit hashMatchFailed(sha1, tr("SHA-1 哈希为空"));
        });
        return;
    }

    QUrl url(m_baseUrl + "/v2/version_file/" + sha1 + "?algorithm=sha1");
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "BlockBox/1.0");

    m_pendingHashSha1 = sha1;
    abortReply(m_currentHashMatchReply);
    m_currentHashMatchReply = m_networkManager->get(request);
    connect(m_currentHashMatchReply, &QNetworkReply::finished,
            this, &ModrinthAPI::onHashMatchReplyFinished);
}

void ModrinthAPI::matchHashes(const QList<QString> &sha1s)
{
    // 过滤空值并统一小写（Modrinth 要求小写哈希）
    QJsonArray hashesArr;
    QSet<QString> seen;
    for (const QString &sha1 : sha1s)
    {
        QString h = sha1.toLower();
        if (h.isEmpty() || seen.contains(h))
            continue;
        seen.insert(h);
        hashesArr.append(h);
    }

    if (hashesArr.isEmpty())
    {
        QTimer::singleShot(0, this, [this]() {
            emit hashesMatchFailed(tr("没有可匹配的哈希"));
        });
        return;
    }

    QUrl url(m_baseUrl + "/v2/version_files");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "BlockBox/1.0");

    QJsonObject body;
    body["hashes"] = hashesArr;
    body["algorithm"] = QStringLiteral("sha1");

    abortReply(m_currentHashBatchReply);
    m_currentHashBatchReply = m_networkManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_currentHashBatchReply, &QNetworkReply::finished,
            this, &ModrinthAPI::onHashBatchReplyFinished);
}

void ModrinthAPI::checkModUpdates(const QList<QString> &sha1s,
                                  const QStringList &gameVersions,
                                  const QStringList &loaders)
{
    QJsonArray hashesArr;
    QSet<QString> seen;
    for (const QString &sha1 : sha1s)
    {
        QString h = sha1.toLower();
        if (h.isEmpty() || seen.contains(h))
            continue;
        seen.insert(h);
        hashesArr.append(h);
    }

    if (hashesArr.isEmpty())
    {
        QTimer::singleShot(0, this, [this]() {
            emit updatesCheckFailed(tr("没有可检查的哈希"));
        });
        return;
    }

    QUrl url(m_baseUrl + "/v2/version_files/update");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "BlockBox/1.0");

    QJsonObject body;
    body["hashes"] = hashesArr;
    body["algorithm"] = QStringLiteral("sha1");
    QJsonArray loadersArr;
    for (const QString &l : loaders)
    {
        QString lw = l.toLower();
        if (!lw.isEmpty())
            loadersArr.append(lw);
    }
    QJsonArray gvArr;
    for (const QString &gv : gameVersions)
    {
        if (!gv.isEmpty())
            gvArr.append(gv);
    }
    body["loaders"] = loadersArr;
    body["game_versions"] = gvArr;

    abortReply(m_currentUpdateCheckReply);
    m_currentUpdateCheckReply = m_networkManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_currentUpdateCheckReply, &QNetworkReply::finished,
            this, &ModrinthAPI::onUpdateCheckReplyFinished);
}

ModVersionFile ModrinthAPI::parseVersionObject(const QJsonObject &obj)
{
    ModVersionFile vf;
    vf.version = obj["name"].toString();
    vf.fileName = obj["version_number"].toString();
    vf.releaseType = obj["version_type"].toString();

    QJsonArray gameVersions = obj["game_versions"].toArray();
    for (const QJsonValue &v : gameVersions)
        vf.gameVersions.append(v.toString());

    QJsonArray loaders = obj["loaders"].toArray();
    for (const QJsonValue &v : loaders)
        vf.loaders.append(v.toString());

    QJsonArray files = obj["files"].toArray();
    if (!files.isEmpty()) {
        QJsonObject fileObj = files[0].toObject();
        vf.downloadUrl = fileObj["url"].toString();
        vf.fileSize = fileObj["size"].toInt();
        vf.sha1 = fileObj["hashes"].toObject()["sha1"].toString();
        // 使用实际文件名（如 "sodium-fabric-0.6.1.jar"），无则回退到版本号
        QString fname = fileObj["filename"].toString();
        vf.fileName = fname.isEmpty() ? obj["version_number"].toString() : fname;
    }

    QString dateStr = obj["date_published"].toString();
    if (!dateStr.isEmpty())
        vf.datePublished = QDateTime::fromString(dateStr, Qt::ISODate);

    QJsonArray depsArr = obj["dependencies"].toArray();
    for (const QJsonValue &dv : depsArr)
    {
        QJsonObject d = dv.toObject();
        ModDependency dep;
        dep.name = d["project_id"].toString();
        dep.version = d["version_type"].toString();
        dep.isRequired = (d["dependency_type"].toString() == QStringLiteral("required"));
        if (!dep.name.isEmpty())
            vf.dependencies.append(dep);
    }

    return vf;
}

void ModrinthAPI::onHashMatchReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply != m_currentHashMatchReply) return;
    m_currentHashMatchReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit hashMatchFailed(m_pendingHashSha1, reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        emit hashMatchFailed(m_pendingHashSha1, tr("Invalid JSON response from server"));
        return;
    }
    QJsonObject obj = doc.object();

    QString projectId = obj["project_id"].toString();

    emit hashMatched(m_pendingHashSha1, projectId, parseVersionObject(obj));
}

void ModrinthAPI::onHashBatchReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply != m_currentHashBatchReply) return;
    m_currentHashBatchReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit hashesMatchFailed(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        emit hashesMatchFailed(tr("Invalid JSON response from server"));
        return;
    }
    QJsonObject obj = doc.object();

    QMap<QString, ModVersionFile> versions;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        versions.insert(it.key(), parseVersionObject(it.value().toObject()));

    emit hashesMatched(versions);
}

void ModrinthAPI::onUpdateCheckReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply != m_currentUpdateCheckReply) return;
    m_currentUpdateCheckReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit updatesCheckFailed(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        emit updatesCheckFailed(tr("Invalid JSON response from server"));
        return;
    }
    QJsonObject obj = doc.object();

    QMap<QString, ModVersionFile> versions;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        versions.insert(it.key(), parseVersionObject(it.value().toObject()));

    emit updatesChecked(versions);
}

void ModrinthAPI::onCategoriesReplyFinished()
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
    QJsonArray arr = doc.array();

    QList<ModCategory> categories;
    for (const QJsonValue &val : arr) {
        QJsonObject obj = val.toObject();
        QString header = obj["header"].toString();
        if (header != "Categories" && header != "Mod Loaders")
            continue;
        ModCategory cat;
        cat.id = obj["name"].toString();
        cat.name = obj["name"].toString();
        categories.append(cat);
    }

    emit categoriesLoaded(categories);
}
