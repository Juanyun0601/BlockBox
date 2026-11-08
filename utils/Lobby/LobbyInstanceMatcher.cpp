/**
 * @file   LobbyInstanceMatcher.cpp
 * @brief  本地实例扫描与联机大厅房间匹配实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "LobbyInstanceMatcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include "utils/SettingsManager.h"

namespace {

/** 从文本里认加载器（和主窗口 applyInstanceContext 的规则保持一致） */
QString detectLoader(const QString &text)
{
    const QString lower = text.toLower();
    if (lower.contains(QStringLiteral("neoforge")))
        return QStringLiteral("NeoForge");
    if (lower.contains(QStringLiteral("fabric")))
        return QStringLiteral("Fabric");
    if (lower.contains(QStringLiteral("quilt")))
        return QStringLiteral("Quilt");
    if (lower.contains(QStringLiteral("forge")))
        return QStringLiteral("Forge");
    return QString();
}

/** 从 "1.20.1-forge-47.2.0" 这类 id 里抠出游戏版本 */
QString extractVersion(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("(\\d+\\.\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch match = re.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

/** 读取实例 JSON，解析出版本 / 加载器 */
void readInstanceMeta(const QString &instancePath, const QString &instanceId,
                      LobbyInstanceInfo *info)
{
    info->version = instanceId;
    info->loader.clear();

    QFile jsonFile(instancePath + QLatin1Char('/') + instanceId + QStringLiteral(".json"));
    if (!jsonFile.open(QIODevice::ReadOnly))
        return;

    const QByteArray raw = jsonFile.readAll();
    jsonFile.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject root = doc.object();
    const QString id = root.value(QStringLiteral("id")).toString();
    const QString inherits = root.value(QStringLiteral("inheritsFrom")).toString();
    const QString clientVersion = root.value(QStringLiteral("clientVersion")).toString();

    if (!inherits.isEmpty())
        info->version = inherits;
    else if (!clientVersion.isEmpty())
        info->version = clientVersion;
    else if (!id.isEmpty())
        info->version = id;

    // 老版本 Forge 的 id 形如 1.20.1-forge-47.2.0，没有 inheritsFrom 时再抠一次
    if (info->version.contains(QLatin1Char('-')) && info->version.contains(QStringLiteral("forge"),
                                                                           Qt::CaseInsensitive))
    {
        const QString extracted = extractVersion(info->version);
        if (!extracted.isEmpty())
            info->version = extracted;
    }

    // 先看 JSON 的键 / 字符串值里有没有加载器关键字，再从 inheritsFrom / id 兜底
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        info->loader = detectLoader(it.key());
        if (!info->loader.isEmpty())
            break;
        const QJsonValue value = it.value();
        if (value.isString())
        {
            info->loader = detectLoader(value.toString());
            if (!info->loader.isEmpty())
                break;
        }
    }
    if (info->loader.isEmpty())
        info->loader = detectLoader(inherits);
    if (info->loader.isEmpty())
        info->loader = detectLoader(id);
    if (info->loader.isEmpty())
        info->loader = detectLoader(instanceId);
}

/** mods 目录下所有 jar 文件名 */
QStringList scanModsDir(const QString &dirPath)
{
    QStringList names;
    QDir dir(dirPath);
    if (!dir.exists())
        return names;

    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.jar"),
                                                  QDir::Files | QDir::NoSymLinks);
    names.reserve(files.size());
    for (const QFileInfo &file : files)
        names.append(file.fileName());
    names.sort(Qt::CaseInsensitive);
    return names;
}

/** 版本隔离关掉时模组在游戏根目录的 mods；实例目录不存在 mods 时用它兜底 */
QStringList scanFallbackMods(const QString &instancePath)
{
    // versions/<id> → 上两级是游戏根目录
    QDir dir(instancePath);
    if (!dir.cdUp() || !dir.cdUp())
        return QStringList();
    return scanModsDir(dir.absoluteFilePath(QStringLiteral("mods")));
}

} // namespace

namespace LobbyInstanceMatcher {

QString normalizeLoader(const QString &loader)
{
    const QString trimmed = loader.trimmed();
    if (trimmed.isEmpty())
        return QString();
    if (trimmed.compare(QStringLiteral("vanilla"), Qt::CaseInsensitive) == 0
        || trimmed == QStringLiteral("原版") || trimmed == QStringLiteral("纯净"))
    {
        return QString();
    }
    if (trimmed.compare(QStringLiteral("fabric"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Fabric");
    if (trimmed.compare(QStringLiteral("neoforge"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("NeoForge");
    if (trimmed.compare(QStringLiteral("forge"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Forge");
    if (trimmed.compare(QStringLiteral("quilt"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Quilt");
    const QString detected = detectLoader(trimmed);
    return detected.isEmpty() ? trimmed : detected;
}

QStringList scanMods(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return QStringList();
    const QStringList mods = scanModsDir(instancePath + QStringLiteral("/mods"));
    if (!mods.isEmpty())
        return mods;
    return scanFallbackMods(instancePath);
}

QList<LobbyInstanceInfo> scanInstances()
{
    QList<LobbyInstanceInfo> instances;

    QStringList roots;
    const QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    for (const InstanceFolderInfo &folder : folders)
    {
        const QString path = QDir(folder.path).absolutePath();
        if (!path.isEmpty() && !roots.contains(path))
            roots.append(path);
    }
    if (roots.isEmpty())
    {
        const QString fallback =
            QCoreApplication::applicationDirPath() + QStringLiteral("/.minecraft");
        roots.append(QDir(fallback).absolutePath());
    }

    for (const QString &root : roots)
    {
        QDir versionsDir(root + QStringLiteral("/versions"));
        if (!versionsDir.exists())
            continue;

        const QFileInfoList dirs = versionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &dirInfo : dirs)
        {
            const QString instanceId = dirInfo.fileName();
            const QString instancePath = dirInfo.absoluteFilePath();
            if (instanceId.startsWith(QLatin1Char('.')))
                continue;

            // 和实例选择页一致：目录里有同名 JSON（或任意带 id 的 JSON）才算实例
            QFileInfo jsonFile(instancePath + QLatin1Char('/') + instanceId + QStringLiteral(".json"));
            if (!jsonFile.exists())
            {
                const QFileInfoList jsons = QDir(instancePath).entryInfoList(
                    QStringList() << QStringLiteral("*.json"), QDir::Files);
                bool found = false;
                for (const QFileInfo &candidate : jsons)
                {
                    QFile file(candidate.absoluteFilePath());
                    if (!file.open(QIODevice::ReadOnly))
                        continue;
                    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                    file.close();
                    if (doc.isObject() && doc.object().contains(QStringLiteral("id")))
                    {
                        jsonFile = candidate;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    continue;
            }

            LobbyInstanceInfo info;
            info.path = instancePath;
            info.id = instanceId;
            info.modifiedAt = jsonFile.lastModified();
            readInstanceMeta(instancePath, instanceId, &info);
            info.mods = scanMods(instancePath);
            instances.append(info);
        }
    }

    return instances;
}

MatchLevel evaluate(const LobbyInstanceInfo &instance, const QString &version,
                    const QString &loader, const QStringList &mods,
                    QStringList *missingMods)
{
    if (missingMods)
        missingMods->clear();

    const QString wantedVersion = version.trimmed();
    if (!wantedVersion.isEmpty() && instance.version != wantedVersion)
        return MatchLevel::None;

    MatchLevel level = MatchLevel::Version;

    const QString wantedLoader = normalizeLoader(loader);
    if (!wantedLoader.isEmpty())
    {
        if (normalizeLoader(instance.loader) != wantedLoader)
            return level;
    }
    level = MatchLevel::Loader;

    if (!mods.isEmpty())
    {
        QSet<QString> local;
        for (const QString &name : instance.mods)
            local.insert(name.toLower());

        QStringList missing;
        for (const QString &name : mods)
        {
            const QString trimmed = name.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (!local.contains(trimmed.toLower()))
                missing.append(trimmed);
        }
        if (missing.isEmpty())
            level = MatchLevel::Mods;
        else if (missingMods)
            *missingMods = missing;
    }

    return level;
}

bool pickBest(const QString &version, const QString &loader, const QStringList &mods,
              LobbyInstanceInfo *best, MatchLevel *level,
              QStringList *missingMods,
              const QList<LobbyInstanceInfo> &pool)
{
    QList<LobbyInstanceInfo> instances = pool.isEmpty() ? scanInstances() : pool;
    if (instances.isEmpty())
        return false;

    LobbyInstanceInfo winner;
    MatchLevel winnerLevel = MatchLevel::None;
    QStringList winnerMissing;
    bool hasWinner = false;

    for (const LobbyInstanceInfo &instance : instances)
    {
        QStringList missing;
        const MatchLevel current = evaluate(instance, version, loader, mods, &missing);
        if (current == MatchLevel::None)
            continue;

        const bool betterLevel = current > winnerLevel;
        const bool sameLevelNewer = current == winnerLevel
            && (!hasWinner || instance.modifiedAt > winner.modifiedAt);
        if (!hasWinner || betterLevel || sameLevelNewer)
        {
            winner = instance;
            winnerLevel = current;
            winnerMissing = missing;
            hasWinner = true;
        }
    }

    if (!hasWinner)
        return false;

    if (best)
        *best = winner;
    if (level)
        *level = winnerLevel;
    if (missingMods)
        *missingMods = winnerMissing;
    return true;
}

QString levelText(MatchLevel level)
{
    switch (level)
    {
    case MatchLevel::Mods:
        return QCoreApplication::translate("LobbyInstanceMatcher", "版本、加载器和模组都匹配");
    case MatchLevel::Loader:
        return QCoreApplication::translate("LobbyInstanceMatcher", "版本和加载器匹配");
    case MatchLevel::Version:
        return QCoreApplication::translate("LobbyInstanceMatcher", "仅版本匹配");
    default:
        return QCoreApplication::translate("LobbyInstanceMatcher", "没有匹配的实例");
    }
}

} // namespace LobbyInstanceMatcher
