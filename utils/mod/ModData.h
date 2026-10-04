#ifndef MODDATA_H
#define MODDATA_H

#include <QDateTime>
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

    bool isValid() const { return !id.isEmpty(); }
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

#endif
