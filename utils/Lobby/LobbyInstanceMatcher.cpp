/**
 * @file   LobbyInstanceMatcher.cpp
 * @brief  本地实例扫描与联机大厅房间匹配实现
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "LobbyInstanceMatcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

#include "utils/SettingsManager.h"

namespace {

/** 缓存多久后强制重扫一次（毫秒）：文件系统时间戳兜底，防止外部改动漏掉 */
constexpr qint64 kCacheMaxAgeMs = 5 * 60 * 1000;

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
    if (info->version.contains(QLatin1Char('-'))
        && info->version.contains(QStringLiteral("forge"), Qt::CaseInsensitive))
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

/** 目录下指定后缀的文件名 */
QStringList scanFilesDir(const QString &dirPath, const QStringList &filters)
{
    QStringList names;
    QDir dir(dirPath);
    if (!dir.exists())
        return names;

    const QFileInfoList files =
        dir.entryInfoList(filters, QDir::Files | QDir::NoSymLinks);
    names.reserve(files.size());
    for (const QFileInfo &file : files)
        names.append(file.fileName());
    names.sort(Qt::CaseInsensitive);
    return names;
}

/** mods 目录下所有 jar 文件名 */
QStringList scanModsDir(const QString &dirPath)
{
    return scanFilesDir(dirPath, QStringList() << QStringLiteral("*.jar"));
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

/**
 * @brief 单个实例的缓存记录
 *
 * 只要 JSON、mods、resourcepacks、shaderpacks 这几个时间戳没变，就不重新解析，
 * 大厅里十几张房间卡反复问「本机哪个实例合适」时几乎没有磁盘开销。
 */
struct InstanceCacheEntry
{
    qint64 jsonMs = -1;
    qint64 modsMs = -1;
    qint64 resourcePacksMs = -1;
    qint64 shaderPacksMs = -1;
    LobbyInstanceInfo info;
};

struct ScanCache
{
    bool valid = false;
    QElapsedTimer age;                 ///< 上次真正扫盘的时间
    QStringList rootPaths;             ///< 参与扫描的实例根目录
    QStringList rootStamps;            ///< 每个根目录的「版本目录 + 目录本身」时间戳
    QList<LobbyInstanceInfo> instances;///< 拼好的实例列表
    QHash<QString, InstanceCacheEntry> entries; ///< 实例路径 → 单项缓存
};

ScanCache &cache()
{
    static ScanCache instance;
    return instance;
}

/** 目录的最后修改时间（毫秒，取不到返回 -1） */
qint64 dirStamp(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return -1;
    return info.lastModified().toMSecsSinceEpoch();
}

/** 文件时间戳（不存在返回 -1） */
qint64 fileStamp(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return -1;
    return info.lastModified().toMSecsSinceEpoch();
}

/** 实例根目录的时间戳：versions 目录 + 各实例子目录本身（增删实例都会变） */
QStringList computeRootStamps(const QStringList &roots)
{
    QStringList stamps;
    stamps.reserve(roots.size() * 2);
    for (const QString &root : roots)
    {
        stamps.append(QStringLiteral("%1:%2").arg(root).arg(dirStamp(root)));
        stamps.append(QString::number(dirStamp(root + QStringLiteral("/versions"))));
    }
    return stamps;
}

/** 从配置里收集实例根目录（去重） */
QStringList collectRoots()
{
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
    return roots;
}

/** 实例目录里是否真的有版本 JSON（和实例选择页一致才算实例） */
bool resolveInstanceJson(const QString &instancePath, const QString &instanceId,
                         QFileInfo *jsonOut)
{
    QFileInfo jsonFile(instancePath + QLatin1Char('/') + instanceId + QStringLiteral(".json"));
    if (jsonFile.exists())
        return true;

    const QFileInfoList jsons =
        QDir(instancePath).entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QFileInfo &candidate : jsons)
    {
        QFile file(candidate.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject() && doc.object().contains(QStringLiteral("id")))
        {
            if (jsonOut)
                *jsonOut = candidate;
            return true;
        }
    }
    return false;
}

} // namespace

void LobbyInstanceInfo::buildIndex()
{
    lowerMods.clear();
    lowerMods.reserve(mods.size());
    for (const QString &name : mods)
        lowerMods.insert(name.toLower());

    lowerResourcePacks.clear();
    lowerResourcePacks.reserve(resourcePacks.size());
    for (const QString &name : resourcePacks)
        lowerResourcePacks.insert(name.toLower());

    lowerShaderPacks.clear();
    lowerShaderPacks.reserve(shaderPacks.size());
    for (const QString &name : shaderPacks)
        lowerShaderPacks.insert(name.toLower());
}

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

QStringList scanResourcePacks(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return QStringList();
    // 资源包是目录或 zip 都有效，只把 zip 当「文件」列出来即可
    return scanFilesDir(instancePath + QStringLiteral("/resourcepacks"),
                        QStringList() << QStringLiteral("*.zip"));
}

QStringList scanShaderPacks(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return QStringList();
    return scanFilesDir(instancePath + QStringLiteral("/shaderpacks"),
                        QStringList() << QStringLiteral("*.zip"));
}

void invalidateCache()
{
    cache() = ScanCache();
}

void warmUp()
{
    scanInstances();
}

QList<LobbyInstanceInfo> scanInstances(bool forceRefresh)
{
    ScanCache &c = cache();
    const QStringList roots = collectRoots();
    const QStringList stamps = computeRootStamps(roots);

    const bool rootUnchanged = c.valid && c.rootPaths == roots && c.rootStamps == stamps;
    const bool fresh = c.valid && c.age.isValid() && c.age.elapsed() < kCacheMaxAgeMs;
    if (!forceRefresh && rootUnchanged && fresh)
        return c.instances;

    QHash<QString, InstanceCacheEntry> nextEntries;
    QList<LobbyInstanceInfo> instances;
    instances.reserve(c.instances.size() + 8);

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

            QFileInfo jsonFile;
            if (!resolveInstanceJson(instancePath, instanceId, &jsonFile))
                continue;

            const qint64 jsonMs = jsonFile.lastModified().toMSecsSinceEpoch();
            const qint64 modsMs = dirStamp(instancePath + QStringLiteral("/mods"));
            const qint64 packsMs = dirStamp(instancePath + QStringLiteral("/resourcepacks"));
            const qint64 shadersMs = dirStamp(instancePath + QStringLiteral("/shaderpacks"));

            // 时间戳完全没变就复用上次解析结果
            const auto cached = c.entries.constFind(instancePath);
            if (!forceRefresh && cached != c.entries.constEnd()
                && cached->jsonMs == jsonMs && cached->modsMs == modsMs
                && cached->resourcePacksMs == packsMs && cached->shaderPacksMs == shadersMs)
            {
                nextEntries.insert(instancePath, *cached);
                instances.append(cached->info);
                continue;
            }

            InstanceCacheEntry entry;
            entry.jsonMs = jsonMs;
            entry.modsMs = modsMs;
            entry.resourcePacksMs = packsMs;
            entry.shaderPacksMs = shadersMs;

            LobbyInstanceInfo info;
            info.path = instancePath;
            info.id = instanceId;
            info.modifiedAt = jsonFile.lastModified();
            readInstanceMeta(instancePath, instanceId, &info);
            info.mods = scanMods(instancePath);
            info.resourcePacks = scanResourcePacks(instancePath);
            info.shaderPacks = scanShaderPacks(instancePath);
            info.buildIndex();
            entry.info = info;

            nextEntries.insert(instancePath, entry);
            instances.append(info);
        }
    }

    c.valid = true;
    c.rootPaths = roots;
    c.rootStamps = stamps;
    c.instances = instances;
    c.entries = nextEntries;
    c.age.start();
    return instances;
}

Requirement requirementFor(const QString &version, const QString &loader,
                           const QStringList &requiredMods, const QStringList &auxMods,
                           const QStringList &resourcePacks, const QStringList &shaderPacks)
{
    Requirement requirement;
    requirement.version = version.trimmed();
    requirement.loader = normalizeLoader(loader);

    auto appendClean = [](QStringList *target, const QStringList &source) {
        for (const QString &name : source)
        {
            const QString trimmed = name.trimmed();
            if (trimmed.isEmpty() || target->contains(trimmed, Qt::CaseInsensitive))
                continue;
            target->append(trimmed);
        }
    };
    appendClean(&requirement.requiredMods, requiredMods);
    appendClean(&requirement.auxMods, auxMods);
    // 必装列表里去掉辅助模组：辅助缺了照样能进房
    if (!requirement.auxMods.isEmpty())
    {
        QStringList required;
        for (const QString &name : requirement.requiredMods)
        {
            if (!requirement.auxMods.contains(name, Qt::CaseInsensitive))
                required.append(name);
        }
        requirement.requiredMods = required;
    }
    appendClean(&requirement.resourcePacks, resourcePacks);
    appendClean(&requirement.shaderPacks, shaderPacks);
    return requirement;
}

InstanceCheck compare(const LobbyInstanceInfo &instance, const Requirement &requirement)
{
    InstanceCheck check;
    check.instance = instance;

    const QString wantedVersion = requirement.version.trimmed();
    if (!wantedVersion.isEmpty() && instance.version != wantedVersion)
        return check; // MatchLevel::None

    // 版本一致但加载器不一致：游戏根本进不去，直接不算候选
    // （原版房间例外：房间没写加载器时任何实例都能进，装了加载器的实例同样能连原版服）
    const QString wantedLoader = normalizeLoader(requirement.loader);
    if (!wantedLoader.isEmpty() && normalizeLoader(instance.loader) != wantedLoader)
        return check;

    // 模组集合：实例若还没建索引（外部手工构造），这里补一次
    QSet<QString> builtMods;
    const QSet<QString> *localMods = &instance.lowerMods;
    if (localMods->isEmpty() && !instance.mods.isEmpty())
    {
        builtMods.reserve(instance.mods.size());
        for (const QString &name : instance.mods)
            builtMods.insert(name.toLower());
        localMods = &builtMods;
    } else if (localMods->isEmpty())
    {
        localMods = &builtMods;
    }

    QSet<QString> builtPacks;
    const QSet<QString> *localPacks = &instance.lowerResourcePacks;
    if (localPacks->isEmpty() && !instance.resourcePacks.isEmpty())
    {
        builtPacks.reserve(instance.resourcePacks.size());
        for (const QString &name : instance.resourcePacks)
            builtPacks.insert(name.toLower());
        localPacks = &builtPacks;
    }
    else if (localPacks->isEmpty())
    {
        localPacks = &builtPacks;
    }

    QSet<QString> builtShaders;
    const QSet<QString> *localShaders = &instance.lowerShaderPacks;
    if (localShaders->isEmpty() && !instance.shaderPacks.isEmpty())
    {
        builtShaders.reserve(instance.shaderPacks.size());
        for (const QString &name : instance.shaderPacks)
            builtShaders.insert(name.toLower());
        localShaders = &builtShaders;
    }
    else if (localShaders->isEmpty())
    {
        localShaders = &builtShaders;
    }

    for (const QString &name : requirement.requiredMods)
    {
        if (!localMods->contains(name.toLower()))
            check.missingMods.append(name);
    }
    for (const QString &name : requirement.auxMods)
    {
        if (!localMods->contains(name.toLower()))
            check.missingAuxMods.append(name);
    }
    for (const QString &name : requirement.resourcePacks)
    {
        if (!localPacks->contains(name.toLower()))
            check.missingResourcePacks.append(name);
    }
    for (const QString &name : requirement.shaderPacks)
    {
        if (!localShaders->contains(name.toLower()))
            check.missingShaderPacks.append(name);
    }

    // 等级：必装模组齐了算「模组匹配」，否则看加载器 / 版本哪一层对上了
    if (check.missingMods.isEmpty())
        check.level = MatchLevel::Mods;
    else if (!wantedLoader.isEmpty())
        check.level = MatchLevel::Loader;
    else
        check.level = MatchLevel::Version;

    // ── 打分 ──
    // 版本 + 加载器对得上先给底分；必装模组按覆盖比例给分，缺一个扣一份。
    int score = 1000;
    if (!wantedLoader.isEmpty())
        score += 120;
    if (!instance.loader.trimmed().isEmpty())
        score += 20;

    const int requiredTotal = requirement.requiredMods.size();
    if (requiredTotal > 0)
    {
        const int matched = requiredTotal - check.missingMods.size();
        score += (matched * 160) / requiredTotal;
        score -= check.missingMods.size() * 25;
    }
    // 辅助模组 / 资源包 / 光影包只做加分：房主那套越齐越贴近，但不是进房门槛
    const int auxTotal = requirement.auxMods.size();
    if (auxTotal > 0)
    {
        const int matched = auxTotal - check.missingAuxMods.size();
        score += (matched * 60) / auxTotal;
    }
    const int packTotal = requirement.resourcePacks.size();
    if (packTotal > 0)
    {
        const int matched = packTotal - check.missingResourcePacks.size();
        score += (matched * 30) / packTotal;
    }
    const int shaderTotal = requirement.shaderPacks.size();
    if (shaderTotal > 0)
    {
        const int matched = shaderTotal - check.missingShaderPacks.size();
        score += (matched * 30) / shaderTotal;
    }

    // 最近装 / 改过的实例略微优先（最多 40 分，界限两年内）
    if (instance.modifiedAt.isValid())
    {
        const qint64 days = instance.modifiedAt.daysTo(QDateTime::currentDateTime());
        if (days >= 0 && days < 730)
            score += int(40 - (40.0 * double(days) / 730.0));
    }

    check.score = score;
    return check;
}

MatchLevel evaluate(const LobbyInstanceInfo &instance, const QString &version,
                    const QString &loader, const QStringList &mods,
                    QStringList *missingMods)
{
    if (missingMods)
        missingMods->clear();

    const Requirement requirement = requirementFor(version, loader, mods);
    const InstanceCheck check = compare(instance, requirement);
    if (check.level == MatchLevel::None)
        return MatchLevel::None;

    if (missingMods && !check.missingMods.isEmpty())
        *missingMods = check.missingMods;
    return check.level;
}

bool pickBest(const QString &version, const QString &loader, const QStringList &mods,
              LobbyInstanceInfo *best, MatchLevel *level, QStringList *missingMods,
              const QList<LobbyInstanceInfo> &pool)
{
    const Requirement requirement = requirementFor(version, loader, mods);
    const JoinPlan plan = makePlan(requirement, pool);
    if (!plan.found)
        return false;

    const InstanceCheck &winner = plan.best;
    if (best)
        *best = winner.instance;
    if (level)
        *level = winner.level;
    if (missingMods)
        *missingMods = winner.missingMods;
    return true;
}

JoinPlan makePlan(const Requirement &requirement, const QList<LobbyInstanceInfo> &pool)
{
    JoinPlan plan;
    plan.requirement = requirement;

    QList<LobbyInstanceInfo> instances = pool.isEmpty() ? scanInstances() : pool;
    if (instances.isEmpty())
    {
        plan.installHint = QCoreApplication::translate(
            "LobbyInstanceMatcher", "本机还没有安装任何实例，先装一个游戏实例再加入。");
        return plan;
    }

    QList<InstanceCheck> checks;
    checks.reserve(instances.size());
    for (const LobbyInstanceInfo &instance : instances)
    {
        InstanceCheck check = compare(instance, requirement);
        if (check.level == MatchLevel::None)
            continue;
        checks.append(check);
    }

    if (checks.isEmpty())
    {
        QString wanted = requirement.version.isEmpty()
                             ? QCoreApplication::translate("LobbyInstanceMatcher", "任意版本")
                             : requirement.version;
        if (!requirement.loader.isEmpty())
            wanted += QStringLiteral(" + ") + requirement.loader;
        plan.installHint = QCoreApplication::translate(
                               "LobbyInstanceMatcher",
                               "本机没有 %1 的实例，装一个对应版本后再加入大厅房间。")
                               .arg(wanted);
        return plan;
    }

    // 得分高的在前；同分时最近改动的优先，再按实例名稳定排序
    std::sort(checks.begin(), checks.end(), [](const InstanceCheck &a, const InstanceCheck &b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.instance.modifiedAt != b.instance.modifiedAt)
            return a.instance.modifiedAt > b.instance.modifiedAt;
        return a.instance.id < b.instance.id;
    });

    plan.found = true;
    plan.best = checks.first();
    const int limit = qMin(checks.size(), 8);
    plan.candidates.reserve(limit);
    for (int i = 0; i < limit; ++i)
        plan.candidates.append(checks.at(i));
    return plan;
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
