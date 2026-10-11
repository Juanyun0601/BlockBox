#include "ModData.h"

#include <QHash>
#include <QJsonArray>
#include <QVector>

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

    obj["altSource"] = info.altSource;
    obj["altId"] = info.altId;
    obj["altName"] = info.altName;
    obj["altDescription"] = info.altDescription;
    obj["altIconUrl"] = info.altIconUrl;
    obj["altPageUrl"] = info.altPageUrl;
    obj["altDownloadCount"] = static_cast<qint64>(info.altDownloadCount);

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

    info.altSource = obj["altSource"].toString();
    info.altId = obj["altId"].toString();
    info.altName = obj["altName"].toString();
    info.altDescription = obj["altDescription"].toString();
    info.altIconUrl = obj["altIconUrl"].toString();
    info.altPageUrl = obj["altPageUrl"].toString();
    info.altDownloadCount = static_cast<qint64>(obj["altDownloadCount"].toDouble());

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

namespace {

// 名称匹配统一为「去首尾空白 + 忽略大小写」，避免 CurseForge/Modrinth 命名大小写差异拆散同名资源
QString normalizedResourceName(const QString &name)
{
    return name.trimmed().toLower();
}

ModInfo makeMergedCard(const ModInfo &primary, const ModInfo &alt)
{
    ModInfo merged = primary;
    merged.altSource = alt.source;
    merged.altId = alt.id;
    merged.altName = alt.name;
    merged.altDescription = alt.description;
    merged.altIconUrl = alt.iconUrl;
    merged.altPageUrl = alt.pageUrl;
    merged.altDownloadCount = alt.downloadCount;
    // 卡片展示的下载量为两源之和；各源自身下载量分别存于 downloadCount（合并前）/altDownloadCount
    merged.downloadCount = primary.downloadCount + alt.downloadCount;
    for (const QString &v : alt.gameVersions)
        if (!merged.gameVersions.contains(v))
            merged.gameVersions.append(v);
    for (const QString &l : alt.loaders)
        if (!merged.loaders.contains(l))
            merged.loaders.append(l);
    for (const QString &c : alt.categories)
        if (!merged.categories.contains(c))
            merged.categories.append(c);
    return merged;
}

} // namespace

QList<ModInfo> mergeCrossSourceResults(const QList<ModInfo> &modrinthList,
                                       const QList<ModInfo> &curseforgeList)
{
    // 同名项在两列表中的位次可能不同（同名资源在两站的排序并不一致），
    // 先建立名称→位次索引，交错合并时即可跨位次配对
    QHash<QString, int> mrByName;
    for (int i = 0; i < modrinthList.size(); ++i) {
        const QString key = normalizedResourceName(modrinthList[i].name);
        if (!key.isEmpty() && !mrByName.contains(key))
            mrByName.insert(key, i);
    }
    QHash<QString, int> cfByName;
    for (int i = 0; i < curseforgeList.size(); ++i) {
        const QString key = normalizedResourceName(curseforgeList[i].name);
        if (!key.isEmpty() && !cfByName.contains(key))
            cfByName.insert(key, i);
    }

    QVector<bool> mrConsumed(modrinthList.size(), false);
    QVector<bool> cfConsumed(curseforgeList.size(), false);

    QList<ModInfo> merged;
    int i = 0, j = 0;
    // 按两来源各自的排序位次交错输出，尽量保持整体相关度顺序；
    // 已配对的项在其原位次跳过
    while (i < modrinthList.size() || j < curseforgeList.size()) {
        if (i < modrinthList.size()) {
            if (!mrConsumed[i]) {
                const ModInfo &mr = modrinthList[i];
                const int cj = cfByName.value(normalizedResourceName(mr.name), -1);
                if (cj >= 0 && !cfConsumed[cj]) {
                    cfConsumed[cj] = true;
                    merged.append(makeMergedCard(mr, curseforgeList[cj]));
                } else {
                    merged.append(mr);
                }
                mrConsumed[i] = true;
            }
            ++i;
        }
        if (j < curseforgeList.size()) {
            if (!cfConsumed[j]) {
                const ModInfo &cf = curseforgeList[j];
                const int mi = mrByName.value(normalizedResourceName(cf.name), -1);
                if (mi >= 0 && !mrConsumed[mi]) {
                    // Modrinth 侧尚未输出：以 Modrinth 为主源合并，落位在 CurseForge 位次
                    mrConsumed[mi] = true;
                    merged.append(makeMergedCard(modrinthList[mi], cf));
                } else {
                    merged.append(cf);
                }
                cfConsumed[j] = true;
            }
            ++j;
        }
    }
    return merged;
}

ModInfo switchModInfoSource(const ModInfo &info)
{
    if (!info.isCrossSourceMerged())
        return info;

    ModInfo swapped;
    swapped.id = info.altId;
    swapped.source = info.altSource;
    swapped.name = info.altName.isEmpty() ? info.name : info.altName;
    swapped.description = info.altDescription;
    swapped.iconUrl = info.altIconUrl;
    swapped.pageUrl = info.altPageUrl;
    swapped.downloadCount = info.altDownloadCount;

    swapped.altSource = info.source;
    swapped.altId = info.id;
    swapped.altName = info.name;
    swapped.altDescription = info.description;
    swapped.altIconUrl = info.iconUrl;
    swapped.altPageUrl = info.pageUrl;
    // 合并卡片首次切换时 downloadCount 仍是双源之和，切回后详情拉取会覆写为真实值；
    // 详情已加载（或单源视角）时即为当前源自身下载量
    swapped.altDownloadCount = info.downloadCount;

    swapped.gameVersions = info.gameVersions;
    swapped.loaders = info.loaders;
    swapped.categories = info.categories;
    swapped.chineseName = info.chineseName;
    swapped.mcmodUrl = info.mcmodUrl;
    return swapped;
}
