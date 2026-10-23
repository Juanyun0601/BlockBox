#include "CurseForgeAPI.h"

#include "../SettingsManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>

namespace {

// CurseForge 的 gameVersions 数组把加载器、运行环境与游戏版本混在一起
// （例如 Sodium 最新文件的 gameVersions 是 ["Client", "Fabric", "26.3"]，
//   有的模组还会出现 "Beta 1.7.3"、"Risugami's ModLoader"）。
// 版本筛选与版本标签只应出现真正的游戏版本，这里剔除已知的非版本项。
bool isGameVersionEntry(const QString &value)
{
    // 运行环境等：整体等于才算（"Server" 是环境名，"Beta 1.7.3" 是真版本）
    static const QStringList kNonVersionExact = {
        QStringLiteral("client"), QStringLiteral("server"),
        QStringLiteral("minecraft"), QStringLiteral("java edition")
    };
    // 加载器 / 服务端平台：写法多样（如 "Risugami's ModLoader"），按包含判断
    static const QStringList kNonVersionKeywords = {
        QStringLiteral("forge"), QStringLiteral("fabric"),
        QStringLiteral("quilt"), QStringLiteral("liteloader"),
        QStringLiteral("modloader"), QStringLiteral("rift"),
        QStringLiteral("babric"), QStringLiteral("ornit"),
        QStringLiteral("optifine"), QStringLiteral("bukkit"),
        QStringLiteral("spigot"), QStringLiteral("paper"),
        QStringLiteral("purpur"), QStringLiteral("sponge"),
        QStringLiteral("velocity"), QStringLiteral("waterfall"),
        QStringLiteral("bungeecord"), QStringLiteral("folia"),
        QStringLiteral("arclight"), QStringLiteral("magma"),
        QStringLiteral("mohist"), QStringLiteral("canvas")
    };
    // 运行环境要求，例如 "Java 21"
    static const QRegularExpression kJavaRuntime(QStringLiteral("^java\\s*\\d+$"));

    const QString lower = value.trimmed().toLower();
    if (lower.isEmpty() || kNonVersionExact.contains(lower))
        return false;
    if (kJavaRuntime.match(lower).hasMatch())
        return false;
    for (const QString &keyword : kNonVersionKeywords) {
        if (lower.contains(keyword))
            return false;
    }
    return true;
}

// 追加真正的游戏版本（去重、保持原顺序）
void appendGameVersions(const QJsonArray &array, QStringList &out)
{
    for (const QJsonValue &v : array) {
        const QString entry = v.toString();
        if (isGameVersionEntry(entry) && !out.contains(entry))
            out.append(entry);
    }
}

} // namespace

CurseForgeAPI::CurseForgeAPI(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_baseUrl(mirrorEnabled() ? QStringLiteral("https://mod.mcimirror.top/curseforge")
                                : QStringLiteral("https://api.curseforge.com"))
    , m_classId("6")
    , m_currentSearchReply(nullptr)
    , m_currentCategoriesReply(nullptr)
    , m_currentDetailReply(nullptr)
    , m_currentModsReply(nullptr)
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

bool CurseForgeAPI::mirrorEnabled()
{
    const QVariant val = SettingsManager::instance()->property("use_mcim");
    return val.isValid() ? val.toBool() : true;
}

bool CurseForgeAPI::usingMirror() const
{
    return m_baseUrl.contains(QStringLiteral("mcimirror.top"), Qt::CaseInsensitive);
}

QString CurseForgeAPI::effectiveApiKey() const
{
    if (!m_apiKey.isEmpty())
        return m_apiKey;
    return localApiKey();
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
    if (effectiveApiKey().isEmpty() && !usingMirror()) {
        QTimer::singleShot(0, this, [this]() {
            emit searchFailed(tr("未配置 CurseForge API Key，请在设置中填入或启用 MCIM 镜像加速"));
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
    const QString apiKey = effectiveApiKey();
    if (!apiKey.isEmpty())
        request.setRawHeader("X-API-KEY", apiKey.toUtf8());

    abortReply(m_currentSearchReply);
    m_currentSearchReply = m_networkManager->get(request);
    connect(m_currentSearchReply, &QNetworkReply::finished, this, &CurseForgeAPI::onSearchReplyFinished);
}

void CurseForgeAPI::getCategories()
{
    if (effectiveApiKey().isEmpty() && !usingMirror()) {
        QTimer::singleShot(0, this, [this]() {
            emit categoriesFailed(tr("未配置 CurseForge API Key，请在设置中填入或启用 MCIM 镜像加速"));
        });
        return;
    }

    QUrl url(m_baseUrl + "/v1/categories");
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("gameId", m_gameId);
    url.setQuery(urlQuery);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    const QString apiKey = effectiveApiKey();
    if (!apiKey.isEmpty())
        request.setRawHeader("X-API-KEY", apiKey.toUtf8());

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
    if (effectiveApiKey().isEmpty() && !usingMirror()) {
        QTimer::singleShot(0, this, [this]() {
            emit modDetailFailed(tr("未配置 CurseForge API Key，请在设置中填入或启用 MCIM 镜像加速"));
        });
        return;
    }

    QUrl url(m_baseUrl + "/v1/mods/" + modId);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    const QString apiKey = effectiveApiKey();
    if (!apiKey.isEmpty())
        request.setRawHeader("X-API-KEY", apiKey.toUtf8());

    m_currentDetailId = modId;
    m_detailFallbackTried = false;

    abortReply(m_currentDetailReply);
    m_currentDetailReply = m_networkManager->get(request);
    connect(m_currentDetailReply, &QNetworkReply::finished, this, &CurseForgeAPI::onDetailReplyFinished);
}

bool CurseForgeAPI::switchOffMirror()
{
    if (!usingMirror())
        return false;
    m_baseUrl = QStringLiteral("https://api.curseforge.com");
    return true;
}

void CurseForgeAPI::fetchModsByIds(const QStringList &modIds)
{
    QJsonArray idArray;
    for (const QString &id : modIds) {
        bool ok = false;
        const int numericId = id.trimmed().toInt(&ok);
        if (ok && numericId > 0)
            idArray.append(numericId);
    }

    if (idArray.isEmpty()) {
        QTimer::singleShot(0, this, [this]() { emit modsResolved(QList<ModInfo>()); });
        return;
    }

    if (effectiveApiKey().isEmpty() && !usingMirror()) {
        QTimer::singleShot(0, this, [this]() {
            emit modsResolveFailed(tr("未配置 CurseForge API Key，请在设置中填入或启用 MCIM 镜像加速"));
        });
        return;
    }

    QJsonObject body;
    body["modIds"] = idArray;

    QUrl url(m_baseUrl + "/v1/mods");
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Content-Type", "application/json");
    const QString apiKey = effectiveApiKey();
    if (!apiKey.isEmpty())
        request.setRawHeader("X-API-KEY", apiKey.toUtf8());

    abortReply(m_currentModsReply);
    m_currentModsReply = m_networkManager->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_currentModsReply, &QNetworkReply::finished, this, &CurseForgeAPI::onModsReplyFinished);
}

void CurseForgeAPI::onModsReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply != m_currentModsReply) return;
    m_currentModsReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit modsResolveFailed(reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.isNull()) {
        emit modsResolveFailed(tr("Invalid JSON response from server"));
        return;
    }

    QList<ModInfo> mods;
    const QJsonArray dataArr = doc.object()["data"].toArray();
    for (const QJsonValue &val : dataArr) {
        ModInfo info;
        parseAddon(val.toObject(), info);
        if (!info.id.isEmpty())
            mods.append(info);
    }

    emit modsResolved(mods);
}

void CurseForgeAPI::fingerprintMod(quint32 fingerprint)
{
    if (effectiveApiKey().isEmpty() && !usingMirror()) {
        QTimer::singleShot(0, this, [this]() {
            emit fingerprintFailed(tr("未配置 CurseForge API Key，请在设置中填入或启用 MCIM 镜像加速"));
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
    const QString apiKey = effectiveApiKey();
    if (!apiKey.isEmpty())
        request.setRawHeader("X-API-KEY", apiKey.toUtf8());

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
        // 镜像加速挂了（常见是 503）：退回官方接口再试一次（需要已配置 API Key，
        // 没有 key 时官方会直接 403，就按失败处理，和以前一样）
        if (!m_detailFallbackTried && switchOffMirror()) {
            m_detailFallbackTried = true;
            fetchModDetail(m_currentDetailId);
            return;
        }
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
    {
        // authors 可能为空数组，对空数组调用 first() 会直接崩溃
        const QJsonArray authors = obj["authors"].toArray();
        if (!authors.isEmpty())
            info.author = authors.first().toObject()["name"].toString();
    }
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
    appendGameVersions(gameVersions, info.gameVersions);

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
        appendGameVersions(fileGameVersions, vf.gameVersions);

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
    {
        // authors / latestFiles 可能为空，空数组调用 first() 会崩溃
        const QJsonArray authors = obj["authors"].toArray();
        if (!authors.isEmpty())
            info.author = authors.first().toObject()["name"].toString();
    }
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
    appendGameVersions(gameVersions, info.gameVersions);

    QJsonArray categories = obj["categories"].toArray();
    for (const QJsonValue &v : categories)
        info.categories.append(v.toObject()["name"].toString());

    const QJsonArray latestFilesArr = obj["latestFiles"].toArray();
    QJsonObject latestFile = latestFilesArr.isEmpty() ? QJsonObject()
                                                      : latestFilesArr.first().toObject();
    if (!latestFile.isEmpty()) {
        info.latestVersion = latestFile["displayName"].toString();
        info.downloadUrl = latestFile["downloadUrl"].toString();
        QJsonArray modLoaders = latestFile["modLoaders"].toArray();
        for (const QJsonValue &v : modLoaders)
            info.loaders.append(v.toString());
        QStringList fileVersions;
        appendGameVersions(latestFile["gameVersions"].toArray(), fileVersions);
        if (!fileVersions.isEmpty())
            info.gameVersions = fileVersions;
    }

    QString dateStr = obj["dateModified"].toString();
    if (!dateStr.isEmpty())
        info.dateModified = QDateTime::fromString(dateStr, Qt::ISODate);
}
