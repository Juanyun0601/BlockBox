#ifndef MODDATA_H
#define MODDATA_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct ModDependency {
    QString name;
    QString version;
    bool isRequired = true;
};

struct ModVersionFile {
    QString version;
    QStringList gameVersions;
    QStringList loaders;
    QString downloadUrl;
    QString fileName;
    QDateTime datePublished;
    qint64 fileSize = 0;
    QString releaseType; // "release", "beta", "alpha"
    QString sha1;        // 该版本文件的 SHA-1 哈希（更新判断用）
    QList<ModDependency> dependencies; // 该版本所依赖的前置（仅 isRequired=true 的会被一键下载）
};

struct ModScreenshot {
    QString url;
    QString description;
};

struct ModInfo {
    QString id;
    QString name;
    QString englishName;
    QString description;
    QString detailedDescription;
    QString author;
    QString iconUrl;
    QString coverUrl;
    QString pageUrl;
    QString homepageUrl;   // 从 JAR 元数据提取的官网链接
    QString issuesUrl;     // 从 JAR 元数据提取的问题追踪链接
    QString curseforgeUrl;  // 通过 ModLinkResolver 解析的 CurseForge 链接
    QString modrinthUrl;    // 通过 ModLinkResolver 解析的 Modrinth 链接
    QStringList categories;
    QStringList gameVersions;
    QStringList loaders;
    QString latestVersion;
    QString downloadUrl;
    QString source;
    QString chineseName;
    QString mcmodUrl;
    QString versionRange;
    QString modType;
    QDateTime dateModified;
    QDateTime datePublished;
    qint64 downloadCount = 0;
    qint64 followers = 0;
    int rank = 0;
    QList<ModScreenshot> screenshots;
    QList<ModDependency> dependencies;
    QList<ModVersionFile> versionFiles;

    // 本地模组相关字段
    QString fileName;
    QString filePath;
    qint64 fileSize = 0;
    bool enabled = true;
    QString loaderType;
    QString sha1Hash;      // SHA-1 哈希（Modrinth 指纹匹配用）
    quint32 curseforgeHash = 0;  // MurmurHash2（CurseForge 指纹匹配用）

    // 跨源合并字段（全部来源搜索下同名资源合并为一张卡片；altSource 为空表示未合并）
    QString altSource;           // 另一来源标识（"curseforge"/"modrinth"）
    QString altId;               // 另一来源项目 ID
    QString altName;             // 另一来源名称
    QString altDescription;      // 另一来源简介
    QString altIconUrl;          // 另一来源图标
    QString altPageUrl;          // 另一来源主页链接
    qint64 altDownloadCount = 0; // 另一来源自身下载量（downloadCount 为两源之和）

    bool isValid() const { return !id.isEmpty(); }
    bool isCrossSourceMerged() const { return !altSource.isEmpty() && !altId.isEmpty(); }
};

struct ModCategory {
    QString id;
    QString name;
    QString parentId;
};

struct ModSearchResult {
    QList<ModInfo> mods;
    int totalHits = 0;
    int offset = 0;
    bool hasMore = false;
};

struct LocalModList {
    QList<ModInfo> mods;
    int enabledCount = 0;
    int disabledCount = 0;
    int totalCount = 0;
};

// ModInfo <-> JSON 序列化（用于列表缓存等场景）
QJsonObject modInfoToJson(const ModInfo &info);
ModInfo modInfoFromJson(const QJsonObject &obj);
QJsonObject modSearchResultToJson(const ModSearchResult &result);
ModSearchResult modSearchResultFromJson(const QJsonObject &obj);

/**
 * @brief 全部来源搜索：按名称合并 CurseForge / Modrinth 同名资源
 *
 * 下载量相加，合并卡片主源固定为 Modrinth（详情页默认源），
 * CurseForge 信息存入 alt* 字段；两列表按各自排序位次交错排列。
 */
QList<ModInfo> mergeCrossSourceResults(const QList<ModInfo> &modrinthList,
                                       const QList<ModInfo> &curseforgeList);

/**
 * @brief 跨源合并卡片切换视角：主源 ↔ 副源字段互换（未合并时原样返回）
 *
 * 仅交换来源身份与展示字段（id/source/name/description/icon/pageUrl/下载量），
 * 版本文件、截图等详情数据由调用方清空后重新拉取。
 */
ModInfo switchModInfoSource(const ModInfo &info);

#endif
