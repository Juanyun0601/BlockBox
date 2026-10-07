#include "ModData.h"

#include <QJsonArray>

QJsonObject modInfoToJson(const ModInfo &info)
{
    QJsonObject obj;
    obj["id"] = info.id;
    obj["name"] = info.name;
    obj["englishName"] = info.englishName;
    obj["description"] = info.description;
    obj["detailedDescription"] = info.detailedDescription;
    obj["author"] = info.author;
    obj["iconUrl"] = info.iconUrl;
    obj["coverUrl"] = info.coverUrl;
    obj["pageUrl"] = info.pageUrl;
    obj["homepageUrl"] = info.homepageUrl;
    obj["issuesUrl"] = info.issuesUrl;
    obj["curseforgeUrl"] = info.curseforgeUrl;
    obj["modrinthUrl"] = info.modrinthUrl;

    QJsonArray categories;
    for (const QString &c : info.categories)
        categories.append(c);
    obj["categories"] = categories;

    QJsonArray gameVersions;
    for (const QString &v : info.gameVersions)
        gameVersions.append(v);
    obj["gameVersions"] = gameVersions;

    QJsonArray loaders;
    for (const QString &l : info.loaders)
        loaders.append(l);
    obj["loaders"] = loaders;

    obj["latestVersion"] = info.latestVersion;
    obj["downloadUrl"] = info.downloadUrl;
    obj["source"] = info.source;
    obj["chineseName"] = info.chineseName;
    obj["mcmodUrl"] = info.mcmodUrl;
    obj["versionRange"] = info.versionRange;
    obj["modType"] = info.modType;
    obj["dateModified"] = info.dateModified.toSecsSinceEpoch();
    obj["datePublished"] = info.datePublished.toSecsSinceEpoch();
    obj["downloadCount"] = static_cast<qint64>(info.downloadCount);
    obj["followers"] = static_cast<qint64>(info.followers);
    obj["rank"] = info.rank;

    return obj;
}

ModInfo modInfoFromJson(const QJsonObject &obj)
{
    ModInfo info;
    info.id = obj["id"].toString();
    info.name = obj["name"].toString();
    info.englishName = obj["englishName"].toString();
    info.description = obj["description"].toString();
    info.detailedDescription = obj["detailedDescription"].toString();
    info.author = obj["author"].toString();
    info.iconUrl = obj["iconUrl"].toString();
    info.coverUrl = obj["coverUrl"].toString();
    info.pageUrl = obj["pageUrl"].toString();
    info.homepageUrl = obj["homepageUrl"].toString();
    info.issuesUrl = obj["issuesUrl"].toString();
    info.curseforgeUrl = obj["curseforgeUrl"].toString();
    info.modrinthUrl = obj["modrinthUrl"].toString();

    const QJsonArray categories = obj["categories"].toArray();
    for (const QJsonValue &v : categories)
        info.categories.append(v.toString());

    const QJsonArray gameVersions = obj["gameVersions"].toArray();
    for (const QJsonValue &v : gameVersions)
        info.gameVersions.append(v.toString());

    const QJsonArray loaders = obj["loaders"].toArray();
    for (const QJsonValue &v : loaders)
        info.loaders.append(v.toString());

    info.latestVersion = obj["latestVersion"].toString();
    info.downloadUrl = obj["downloadUrl"].toString();
    info.source = obj["source"].toString();
    info.chineseName = obj["chineseName"].toString();
    info.mcmodUrl = obj["mcmodUrl"].toString();
    info.versionRange = obj["versionRange"].toString();
    info.modType = obj["modType"].toString();
    info.dateModified = QDateTime::fromSecsSinceEpoch(obj["dateModified"].toInteger());
    info.datePublished = QDateTime::fromSecsSinceEpoch(obj["datePublished"].toInteger());
    info.downloadCount = static_cast<qint64>(obj["downloadCount"].toDouble());
    info.followers = static_cast<qint64>(obj["followers"].toDouble());
    info.rank = obj["rank"].toInt();

    return info;
}

QJsonObject modSearchResultToJson(const ModSearchResult &result)
{
    QJsonObject obj;
    obj["totalHits"] = result.totalHits;
    obj["offset"] = result.offset;
    obj["hasMore"] = result.hasMore;

    QJsonArray mods;
    for (const ModInfo &info : result.mods)
        mods.append(modInfoToJson(info));
    obj["mods"] = mods;

    return obj;
}

ModSearchResult modSearchResultFromJson(const QJsonObject &obj)
{
    ModSearchResult result;
    result.totalHits = obj["totalHits"].toInt();
    result.offset = obj["offset"].toInt();
    result.hasMore = obj["hasMore"].toBool();

    const QJsonArray mods = obj["mods"].toArray();
    for (const QJsonValue &v : mods) {
        if (v.isObject())
            result.mods.append(modInfoFromJson(v.toObject()));
    }

    return result;
}
