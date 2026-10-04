/**
 * @file   LocalCompatibilityChecker.cpp
 * @brief  本地资源兼容性检测引擎实现
 * @author BlockBox Team
 * @date   2026-08-30
 *
 * 参考开源实现：
 *  - PrismLauncher：资源包 pack_format 与游戏版本映射、JAR 元数据内存直取
 *  - HMCL 模组管理：重复模组、依赖缺失、无法识别文件等 mods/ 目录检查
 *  - Fabric/Forge 官方元数据规范：fabric.mod.json / mods.toml 依赖与版本范围
 */
#include "LocalCompatibilityChecker.h"

#include "../JarUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {

// 加载器自身提供、无需在 mods/ 内寻找前置的内置 mod id
const QSet<QString> k_builtinModIds = {
    "minecraft", "java", "fabricloader", "quilt_loader", "quilt_base",
    "forge", "neoforge", "liteloader", "rift", "legacyfabric"
};

// 资源包 pack_format → 兼容的游戏版本区间（数据来源：minecraft.wiki Pack format，
// 与 PrismLauncher 的 PackFormat 映射一致；超出表内的版本走“无法确认”提示）
struct PackFormatEntry
{
    int format;
    const char *minVersion;
    const char *maxVersion;
};

const QList<PackFormatEntry> k_packFormats = {
    {1, "1.6.1", "1.8.9"},
    {2, "1.9", "1.10.2"},
    {3, "1.11", "1.12.2"},
    {4, "1.13", "1.14.4"},
    {5, "1.15", "1.16.1"},
    {6, "1.16.2", "1.16.5"},
    {7, "1.17", "1.17.1"},
    {8, "1.18", "1.18.2"},
    {9, "1.19", "1.19.2"},
    {12, "1.19.3", "1.19.3"},
    {13, "1.19.4", "1.19.4"},
    {15, "1.20", "1.20.1"},
    {18, "1.20.2", "1.20.2"},
    {22, "1.20.3", "1.20.4"},
    {32, "1.20.5", "1.20.6"},
    {34, "1.21", "1.21.1"},
    {42, "1.21.2", "1.21.3"},
    {46, "1.21.4", "1.21.4"},
    {55, "1.21.5", "1.21.5"},
    {63, "1.21.6", "1.21.6"},
    {64, "1.21.7", "1.21.8"},
};

QString stripQuotes(const QString &raw)
{
    QString v = raw.trimmed();
    if (v.size() >= 2 && ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith('\'') && v.endsWith('\''))))
        return v.mid(1, v.size() - 2);
    return v;
}

// 取 "key = value" 行的键值（不匹配返回 false）
bool parseTomlKeyValue(const QString &line, QString &key, QString &value)
{
    int eq = line.indexOf('=');
    if (eq <= 0)
        return false;
    key = line.left(eq).trimmed();
    value = line.mid(eq + 1).trimmed();
    // 去掉行尾注释（仅处理值不在引号内的简单情况）
    if (!value.startsWith('"') && !value.startsWith('\''))
    {
        int hash = value.indexOf('#');
        if (hash >= 0)
            value = value.left(hash).trimmed();
    }
    return !key.isEmpty();
}

// 把 fabric 依赖值（字符串或字符串数组）拼接为 "||" 连接的范围表达式
QString jsonValueToRange(const QJsonValue &val)
{
    if (val.isString())
        return val.toString().trimmed();
    if (val.isArray())
    {
        QStringList parts;
        for (const QJsonValue &v : val.toArray())
        {
            if (v.isString())
                parts << v.toString().trimmed();
        }
        return parts.join(" || ");
    }
    return QString();
}

} // namespace

// =====================================================================
// 对外入口
// =====================================================================

CompatReport LocalCompatibilityChecker::checkInstance(const QString &instancePath,
                                                      const QString &gameVersion,
                                                      const QString &loaderType)
{
    CompatReport report;
    report.gameVersion = gameVersion;
    report.loaderType = loaderType;

    if (instancePath.isEmpty())
        return report;

    // ===== 扫描 mods/ 目录 =====
    QList<ModMeta> mods;
    QStringList junkFiles;
    const QString modsDirPath = instancePath + "/mods";
    QDir modsDir(modsDirPath);
    if (modsDir.exists())
    {
        QDirIterator it(modsDirPath, QDir::Files, QDirIterator::NoIteratorFlags);
        while (it.hasNext())
        {
            it.next();
            QFileInfo fi = it.fileInfo();
            const QString name = fi.fileName();
            const bool isDisabledJar = name.endsWith(".jar.disabled", Qt::CaseInsensitive);
            if (isDisabledJar || name.endsWith(".jar", Qt::CaseInsensitive))
            {
                ModMeta meta;
                if (parseModJar(fi.absoluteFilePath(), name, !isDisabledJar, meta))
                    mods.append(meta);
            }
            else
            {
                junkFiles << name;
            }
        }
    }

    // ===== 逐项检查 =====
    checkMods(report, mods, junkFiles, gameVersion, loaderType);
    checkResourcePacks(report, instancePath + "/resourcepacks", gameVersion);
    checkShaderPacks(report, instancePath + "/shaderpacks", mods, loaderType);

    // ===== 汇总统计 =====
    report.modCount = mods.size();
    report.errorCount = 0;
    report.warningCount = 0;
    report.infoCount = 0;
    for (const CompatIssue &issue : report.issues)
    {
        switch (issue.severity)
        {
        case CompatIssue::Error: ++report.errorCount; break;
        case CompatIssue::Warning: ++report.warningCount; break;
        default: ++report.infoCount; break;
        }
    }
    return report;
}

// =====================================================================
// 模组元数据解析
// =====================================================================

bool LocalCompatibilityChecker::parseModJar(const QString &filePath, const QString &fileName,
                                            bool enabled, ModMeta &meta)
{
    meta.fileName = fileName;
    meta.filePath = filePath;
    meta.enabled = enabled;

    // OptiFine 没有标准元数据，用文件名识别（官方命名 OptiFine_1.20.1_HD_U_I6.jar）
    if (fileName.contains("optifine", Qt::CaseInsensitive))
        meta.isOptifine = true;

    QMap<QString, QByteArray> extracted;
    const QStringList entries = {
        "fabric.mod.json",
        "quilt.mod.json",
        "META-INF/mods.toml",
        "META-INF/neoforge.mods.toml"
    };
    JarUtils::extractMultipleFromJarToMemory(filePath, entries, extracted);

    if (extracted.contains("fabric.mod.json"))
    {
        if (parseFabricModJson(QString::fromUtf8(extracted.value("fabric.mod.json")), meta))
            return true;
    }
    if (extracted.contains("quilt.mod.json"))
    {
        if (parseQuiltModJson(QString::fromUtf8(extracted.value("quilt.mod.json")), meta))
            return true;
    }
    if (extracted.contains("META-INF/neoforge.mods.toml"))
    {
        if (parseModsToml(QString::fromUtf8(extracted.value("META-INF/neoforge.mods.toml")), meta, "neoforge"))
            return true;
    }
    if (extracted.contains("META-INF/mods.toml"))
    {
        if (parseModsToml(QString::fromUtf8(extracted.value("META-INF/mods.toml")), meta, "forge"))
            return true;
    }

    // 没有可识别元数据的 jar：OptiFine 保留用于光影/OptiFine 检查，其余用文件名占位
    if (meta.modId.isEmpty() && !meta.isOptifine)
    {
        meta.modId = fileName;
        meta.loader.clear();
    }
    return true;
}

bool LocalCompatibilityChecker::parseFabricModJson(const QString &content, ModMeta &meta)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    QJsonObject root = doc.object();
    meta.modId = root.value("id").toString();
    meta.version = root.value("version").toString();
    meta.loader = "fabric";

    if (root.value("provides").isArray())
    {
        for (const QJsonValue &v : root.value("provides").toArray())
        {
            if (v.isString())
                meta.provides << v.toString();
        }
    }
    if (root.value("depends").isObject())
    {
        const QJsonObject deps = root.value("depends").toObject();
        for (auto it = deps.constBegin(); it != deps.constEnd(); ++it)
            meta.depends.insert(it.key(), jsonValueToRange(it.value()));
    }
    if (root.value("breaks").isObject())
    {
        const QJsonObject breaks = root.value("breaks").toObject();
        for (auto it = breaks.constBegin(); it != breaks.constEnd(); ++it)
            meta.breaks.insert(it.key(), jsonValueToRange(it.value()));
    }
    return !meta.modId.isEmpty();
}

bool LocalCompatibilityChecker::parseQuiltModJson(const QString &content, ModMeta &meta)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    QJsonObject loaderObj = doc.object().value("quilt_loader").toObject();
    if (loaderObj.isEmpty())
        return false;

    meta.modId = loaderObj.value("id").toString();
    meta.version = loaderObj.value("version").toString();
    meta.loader = "quilt";

    for (const QString &key : { QString("depends"), QString("breaks") })
    {
        if (!loaderObj.value(key).isArray())
            continue;
        for (const QJsonValue &v : loaderObj.value(key).toArray())
        {
            const QJsonObject dep = v.toObject();
            const QString depId = dep.value("id").toString();
            if (depId.isEmpty())
                continue;
            QString range = dep.value("versions").toString();
            if (dep.value("versions").isArray())
            {
                QStringList parts;
                for (const QJsonValue &r : dep.value("versions").toArray())
                    parts << r.toString();
                range = parts.join(" || ");
            }
            if (key == "depends")
                meta.depends.insert(depId, range.trimmed());
            else
                meta.breaks.insert(depId, range.trimmed());
        }
    }
    return !meta.modId.isEmpty();
}

bool LocalCompatibilityChecker::parseModsToml(const QString &content, ModMeta &meta, const QString &loader)
{
    const QStringList lines = content.split('\n');

    // ---- [[mods]] 区：取第一个 modId / version ----
    bool inMods = false;
    for (const QString &rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith('['))
        {
            inMods = (line == "[[mods]]");
            continue;
        }
        if (!inMods)
            continue;
        QString key, value;
        if (!parseTomlKeyValue(line, key, value))
            continue;
        if (key == "modId" && meta.modId.isEmpty())
            meta.modId = stripQuotes(value);
        else if (key == "version" && meta.version.isEmpty())
            meta.version = stripQuotes(value);
    }

    // ---- [[dependencies.<id>]] 区：收集前置 ----
    QList<TomlDep> deps;
    TomlDep currentDep;
    bool inDep = false;
    for (const QString &rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith('['))
        {
            // 区段边界：保存上一个依赖
            if (inDep && !currentDep.modId.isEmpty())
                deps.append(currentDep);
            inDep = false;
            currentDep = TomlDep();

            if (line.startsWith("[[dependencies.") && line.endsWith("]]"))
            {
                const QString prefix = "[[dependencies.";
                currentDep.modId = line.mid(prefix.size(), line.size() - prefix.size() - 2);
                inDep = true;
            }
            continue;
        }
        if (!inDep)
            continue;

        QString key, value;
        if (!parseTomlKeyValue(line, key, value))
            continue;
        if (key == "modId")
            currentDep.modId = stripQuotes(value);
        else if (key == "versionRange")
            currentDep.range = stripQuotes(value);
        else if (key == "mandatory")
            currentDep.required = (stripQuotes(value).toLower() == "true");
        else if (key == "type")
            currentDep.required = (stripQuotes(value).toLower() == "required");
    }
    if (inDep && !currentDep.modId.isEmpty())
        deps.append(currentDep);

    meta.tomlDeps = deps;
    meta.loader = loader;
    return !meta.modId.isEmpty();
}

// =====================================================================
// 模组检查
// =====================================================================

void LocalCompatibilityChecker::checkMods(CompatReport &report,
                                          const QList<ModMeta> &mods,
                                          const QStringList &junkFiles,
                                          const QString &gameVersion,
                                          const QString &loaderType)
{
    if (mods.isEmpty())
    {
        if (!junkFiles.isEmpty())
        {
            CompatIssue issue;
            issue.severity = CompatIssue::Warning;
            issue.category = "mod";
            issue.fileName = junkFiles.join(", ");
            issue.title = QString("mods 目录中存在无法识别的文件");
            issue.detail = QString("以下文件不是模组（.jar），游戏会忽略它们：%1").arg(junkFiles.join("、"));
            issue.suggestion = QString("如不再需要可手动删除这些文件");
            addIssue(report, issue);
        }
        return;
    }

    const bool isFabricLike = (loaderType == "Fabric" || loaderType == "Quilt");
    const bool isForgeLike = (loaderType == "Forge" || loaderType == "NeoForge");

    // ===== 1. 原版实例 / 加载器不匹配 =====
    if (loaderType.isEmpty())
    {
        CompatIssue issue;
        issue.severity = CompatIssue::Error;
        issue.category = "mod";
        issue.fileName = QString("%1 个模组文件").arg(mods.size());
        issue.title = QString("原版实例无法加载模组");
        issue.detail = QString("当前实例未安装 Forge/Fabric 等加载器，%1 个模组文件不会生效。").arg(mods.size());
        issue.suggestion = QString("在安装页面为实例安装 Forge/Fabric/NeoForge 加载器，或移除这些模组");
        addIssue(report, issue);
    }
    else
    {
        for (const ModMeta &mod : mods)
        {
            if (!mod.enabled || mod.isOptifine)
                continue;
            const QString modLoader = mod.loader;
            bool mismatch = false;
            QString expectName;
            if (isForgeLike && (modLoader == "fabric" || modLoader == "quilt"))
            {
                mismatch = true;
                expectName = loaderType;
            }
            else if (isFabricLike && (modLoader == "forge" || modLoader == "neoforge"))
            {
                mismatch = true;
                expectName = loaderType;
            }
            else if (loaderType == "Fabric" && modLoader == "quilt")
            {
                mismatch = true;
                expectName = "Quilt";
            }

            if (mismatch)
            {
                CompatIssue issue;
                issue.severity = CompatIssue::Error;
                issue.category = "mod";
                issue.fileName = mod.fileName;
                issue.title = QString("模组与实例加载器不匹配");
                issue.detail = QString("「%1」是 %2 模组，无法在 %3 实例上加载。")
                                   .arg(mod.modId, modLoader.toUpper(), loaderType);
                issue.suggestion = QString("移除该模组，或安装 %1 版本的对应模组").arg(expectName);
                addIssue(report, issue);
            }
        }
    }

    // ===== 2. 重复模组 ID =====
    QMap<QString, QList<int>> byId;
    for (int i = 0; i < mods.size(); ++i)
    {
        const QString &id = mods.at(i).modId;
        if (!id.isEmpty())
            byId[id].append(i);
    }
    for (auto it = byId.constBegin(); it != byId.constEnd(); ++it)
    {
        const QList<int> &idxs = it.value();
        if (idxs.size() < 2)
            continue;

        QStringList enabledNames, disabledNames;
        for (int idx : idxs)
        {
            if (mods.at(idx).enabled)
                enabledNames << mods.at(idx).fileName;
            else
                disabledNames << mods.at(idx).fileName;
        }

        // 同 ID 同版本且同体积 → 大概率是重复复制的同一文件
        bool sameContent = false;
        for (int a = 0; a < idxs.size() && !sameContent; ++a)
        {
            for (int b = a + 1; b < idxs.size(); ++b)
            {
                const ModMeta &m1 = mods.at(idxs.at(a));
                const ModMeta &m2 = mods.at(idxs.at(b));
                if (!m1.version.isEmpty() && m1.version == m2.version
                    && QFileInfo(m1.filePath).size() == QFileInfo(m2.filePath).size())
                {
                    sameContent = true;
                    break;
                }
            }
        }

        CompatIssue issue;
        issue.category = "mod";
        issue.fileName = (enabledNames + disabledNames).join(", ");
        if (enabledNames.size() >= 2)
        {
            issue.severity = CompatIssue::Error;
            issue.title = QString("重复安装的模组「%1」").arg(it.key());
            issue.detail = QString("以下启用的模组提供同一个 ID「%1」，游戏加载时会崩溃：%2")
                               .arg(it.key(), enabledNames.join("、"));
            issue.suggestion = QString("只保留其中一个版本");
        }
        else if (!disabledNames.isEmpty())
        {
            issue.severity = CompatIssue::Info;
            issue.title = QString("模组「%1」存在禁用副本").arg(it.key());
            issue.detail = QString("同一模组同时存在启用与禁用（.jar.disabled）文件：%1")
                               .arg((enabledNames + disabledNames).join("、"));
            issue.suggestion = sameContent
                                   ? QString("删除不再使用的副本，避免混淆")
                                   : QString("确认保留的版本后可清理禁用副本");
        }
        addIssue(report, issue);
    }

    // ===== 3. 前置依赖检查（仅启用的模组） =====
    // 收集所有启用模组提供的 id（modId + provides）
    QSet<QString> providedIds;
    QMap<QString, QString> providerVersion; // id → 版本（取第一个提供者）
    for (const ModMeta &mod : mods)
    {
        if (!mod.enabled || mod.modId.isEmpty())
            continue;
        providedIds.insert(mod.modId);
        if (!providerVersion.contains(mod.modId))
            providerVersion.insert(mod.modId, mod.version);
        for (const QString &extra : mod.provides)
        {
            providedIds.insert(extra);
            if (!providerVersion.contains(extra))
                providerVersion.insert(extra, mod.version);
        }
    }

    for (const ModMeta &mod : mods)
    {
        if (!mod.enabled)
            continue;
        const bool fromToml = (mod.loader == "forge" || mod.loader == "neoforge");

        // 合并 fabric.depends 与 toml 必需依赖为统一列表
        QList<QPair<QString, QString>> depList;
        for (auto it = mod.depends.constBegin(); it != mod.depends.constEnd(); ++it)
            depList.append(qMakePair(it.key(), it.value()));
        for (const TomlDep &dep : mod.tomlDeps)
        {
            if (dep.required)
                depList.append(qMakePair(dep.modId, dep.range));
        }

        for (const auto &dep : depList)
        {
            const QString depId = dep.first;
            const QString range = dep.second;

            if (depId == "minecraft")
            {
                // 与游戏版本直接核对
                if (range.isEmpty() || gameVersion.isEmpty())
                    continue;
                const bool ok = fromToml ? forgeRangeSatisfies(gameVersion, range)
                                         : semverSatisfies(gameVersion, range);
                if (!ok)
                {
                    CompatIssue issue;
                    issue.severity = CompatIssue::Error;
                    issue.category = "mod";
                    issue.fileName = mod.fileName;
                    issue.title = QString("模组不支持当前游戏版本");
                    issue.detail = QString("「%1」声明支持的游戏版本范围为 %2，当前实例为 %3。")
                                       .arg(mod.modId, range, gameVersion);
                    issue.suggestion = QString("下载支持 %1 的模组版本").arg(gameVersion);
                    addIssue(report, issue);
                }
                continue;
            }

            if (k_builtinModIds.contains(depId))
                continue;

            if (!providedIds.contains(depId))
            {
                CompatIssue issue;
                issue.severity = CompatIssue::Error;
                issue.category = "mod";
                issue.fileName = mod.fileName;
                QString hint;
                if (depId == "fabric")
                    hint = QString("（通常即 Fabric API）");
                else if (depId.startsWith("fabric-"))
                    hint = QString("（Fabric API 的子模块）");
                issue.title = QString("缺少前置模组「%1」%2").arg(depId, hint);
                issue.detail = QString("「%1」要求前置模组「%2」%3，但当前实例中未安装。")
                                   .arg(mod.modId, depId,
                                        range.isEmpty() ? QString() : QString("版本范围 %1").arg(range));
                issue.suggestion = QString("先安装前置模组「%1」再启动游戏").arg(depId);
                addIssue(report, issue);
                continue;
            }

            // 已安装：核对版本范围
            if (range.isEmpty())
                continue;
            const QString haveVer = providerVersion.value(depId);
            if (haveVer.isEmpty())
                continue;
            const bool ok = fromToml ? forgeRangeSatisfies(haveVer, range)
                                     : semverSatisfies(haveVer, range);
            if (!ok)
            {
                CompatIssue issue;
                issue.severity = CompatIssue::Error;
                issue.category = "mod";
                issue.fileName = mod.fileName;
                issue.title = QString("前置模组版本不满足要求");
                issue.detail = QString("「%1」要求「%2」的版本为 %3，当前安装的是 %4。")
                                   .arg(mod.modId, depId, range, haveVer);
                issue.suggestion = QString("更新或更换「%1」到满足要求的版本").arg(depId);
                addIssue(report, issue);
            }
        }

        // ===== 4. 声明的冲突（breaks） =====
        for (auto it = mod.breaks.constBegin(); it != mod.breaks.constEnd(); ++it)
        {
            if (!providedIds.contains(it.key()))
                continue;
            const QString haveVer = providerVersion.value(it.key());
            const bool conflict = it.value().isEmpty()
                                      ? true
                                      : (fromToml ? forgeRangeSatisfies(haveVer, it.value())
                                                  : semverSatisfies(haveVer, it.value()));
            if (conflict)
            {
                CompatIssue issue;
                issue.severity = CompatIssue::Warning;
                issue.category = "mod";
                issue.fileName = mod.fileName + ", " + it.key();
                issue.title = QString("模组声明与「%1」冲突").arg(it.key());
                issue.detail = QString("「%1」声明与「%2」(版本 %3) 存在冲突，可能导致游戏崩溃或异常。")
                                   .arg(mod.modId, it.key(), haveVer);
                issue.suggestion = QString("参考模组说明移除或更换其中一方");
                addIssue(report, issue);
            }
        }
    }

    // ===== 5. OptiFine 相关 =====
    bool hasOptifine = false;
    bool hasIris = false, hasOculus = false;
    QString optifineFile;
    for (const ModMeta &mod : mods)
    {
        if (!mod.enabled)
            continue;
        if (mod.isOptifine)
        {
            hasOptifine = true;
            if (optifineFile.isEmpty())
                optifineFile = mod.fileName;
        }
        if (mod.modId == "iris" || mod.provides.contains("iris"))
            hasIris = true;
        if (mod.modId == "oculus" || mod.provides.contains("oculus"))
            hasOculus = true;
    }
    if (hasOptifine && isFabricLike)
    {
        CompatIssue issue;
        issue.severity = CompatIssue::Error;
        issue.category = "mod";
        issue.fileName = optifineFile;
        issue.title = QString("OptiFine 与 Fabric 不兼容");
        issue.detail = QString("OptiFine 是 Forge 专用优化模组，与 Fabric 加载器冲突，会导致启动失败。");
        issue.suggestion = QString("改用 OptiFabric + OptiFine，或卸载 OptiFine 改用 Sodium + Iris");
        addIssue(report, issue);
    }
    if (hasOptifine && (hasIris || hasOculus))
    {
        CompatIssue issue;
        issue.severity = CompatIssue::Warning;
        issue.category = "mod";
        issue.fileName = optifineFile;
        issue.title = QString("OptiFine 与 Iris/Oculus 同时安装");
        issue.detail = QString("OptiFine 与 Iris/Oculus 功能重叠且互相冲突，二者只能保留一个。");
        issue.suggestion = QString("卸载 OptiFine，保留 Iris/Oculus");
        addIssue(report, issue);
    }

    // ===== 6. 无法识别的文件 =====
    if (!junkFiles.isEmpty())
    {
        CompatIssue issue;
        issue.severity = CompatIssue::Warning;
        issue.category = "mod";
        issue.fileName = junkFiles.join(", ");
        issue.title = QString("mods 目录中存在无法识别的文件");
        issue.detail = QString("以下文件不是 .jar 模组，游戏会忽略它们（.zip 不会被当作模组加载）：%1")
                           .arg(junkFiles.join("、"));
        issue.suggestion = QString("若是模组请重命名为 .jar，否则删除");
        addIssue(report, issue);
    }
}

// =====================================================================
// 资源包检查
// =====================================================================

void LocalCompatibilityChecker::checkResourcePacks(CompatReport &report,
                                                   const QString &resourcePackDir,
                                                   const QString &gameVersion)
{
    QDir dir(resourcePackDir);
    if (!dir.exists())
        return;

    // 资源包有两种形态：.zip 与文件夹
    const QStringList zips = listFiles(resourcePackDir, { ".zip" });
    QStringList folders;
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        folders << fi.absoluteFilePath();

    report.resourcePackCount = zips.size() + folders.size();
    if (report.resourcePackCount == 0)
        return;

    const int expected = resourcePackFormatFor(gameVersion);

    if (expected == -1 && !gameVersion.isEmpty())
    {
        CompatIssue issue;
        issue.severity = CompatIssue::Info;
        issue.category = "resourcepack";
        issue.title = QString("快照版本无法核对资源包格式");
        issue.detail = QString("当前游戏版本「%1」不是正式版本号，跳过资源包 pack_format 核对。").arg(gameVersion);
        addIssue(report, issue);
    }

    auto checkOne = [&](const QString &path, bool isZip)
    {
        const QString name = QFileInfo(path).fileName();

        // 读取 pack.mcmeta
        QByteArray mcmeta;
        if (isZip)
            JarUtils::extractFromJarToMemory(path, "pack.mcmeta", mcmeta);
        else
        {
            QFile f(path + "/pack.mcmeta");
            if (f.open(QIODevice::ReadOnly))
                mcmeta = f.readAll();
        }

        if (mcmeta.isEmpty())
        {
            CompatIssue issue;
            issue.severity = CompatIssue::Error;
            issue.category = "resourcepack";
            issue.fileName = name;
            issue.title = QString("无法读取资源包的 pack.mcmeta");
            issue.detail = QString("「%1」中缺少 pack.mcmeta 或压缩包已损坏，游戏将无法加载该资源包。").arg(name);
            issue.suggestion = QString("重新下载该资源包，或删除损坏文件");
            addIssue(report, issue);
            return;
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(mcmeta, &err);
        QJsonObject packObj = doc.object().value("pack").toObject();
        if (err.error != QJsonParseError::NoError || packObj.isEmpty())
        {
            CompatIssue issue;
            issue.severity = CompatIssue::Error;
            issue.category = "resourcepack";
            issue.fileName = name;
            issue.title = QString("资源包 pack.mcmeta 格式错误");
            issue.detail = QString("「%1」的 pack.mcmeta 不是有效的 JSON，游戏将无法加载该资源包。").arg(name);
            issue.suggestion = QString("重新下载该资源包");
            addIssue(report, issue);
            return;
        }

        // pack.png 缺失提示（列表中会显示紫黑格占位图）
        bool hasPng = false;
        if (isZip)
            hasPng = JarUtils::listEntriesInJar(path).contains("pack.png");
        else
            hasPng = QFile::exists(path + "/pack.png");
        if (!hasPng)
        {
            CompatIssue issue;
            issue.severity = CompatIssue::Info;
            issue.category = "resourcepack";
            issue.fileName = name;
            issue.title = QString("资源包缺少 pack.png 图标");
            issue.detail = QString("「%1」没有提供 pack.png，列表中会显示默认占位图标。").arg(name);
            addIssue(report, issue);
        }

        const int packFormat = packObj.value("pack_format").toInt(-9999);
        if (packFormat == -9999)
        {
            CompatIssue issue;
            issue.severity = CompatIssue::Warning;
            issue.category = "resourcepack";
            issue.fileName = name;
            issue.title = QString("资源包未声明 pack_format");
            issue.detail = QString("「%1」的 pack.mcmeta 中缺少 pack_format 字段，游戏可能拒绝加载。").arg(name);
            issue.suggestion = QString("联系作者补充 pack_format，或手动编辑 pack.mcmeta");
            addIssue(report, issue);
            return;
        }

        if (expected == -2)
        {
            // 版本号正规但超出已知映射表（更新的正式版）
            CompatIssue issue;
            issue.severity = CompatIssue::Info;
            issue.category = "resourcepack";
            issue.fileName = name;
            issue.title = QString("资源包格式无法确认");
            issue.detail = QString("「%1」使用 pack_format %2，当前版本 %3 的对应格式未收录，无法自动判断兼容性。")
                               .arg(name).arg(packFormat).arg(gameVersion);
            issue.suggestion = QString("如游戏内提示“不兼容”可忽略或等待资源包更新");
            addIssue(report, issue);
            return;
        }
        if (expected <= 0)
            return; // 版本无法识别：已在开头统一提示

        // pack 声明了 supported_formats 且包含实例期望格式 → 兼容
        if (packObj.contains("supported_formats"))
        {
            const QJsonValue sf = packObj.value("supported_formats");
            int lo = -9999, hi = -9999;
            if (sf.isArray() && sf.toArray().size() == 2)
            {
                lo = sf.toArray().at(0).toInt(-9999);
                hi = sf.toArray().at(1).toInt(-9999);
            }
            else if (sf.isObject())
            {
                lo = sf.toObject().value("min_inclusive").toInt(-9999);
                hi = sf.toObject().value("max_inclusive").toInt(-9999);
            }
            if (lo != -9999 && hi != -9999 && expected >= lo && expected <= hi)
                return;
        }

        if (packFormat == expected)
            return;

        // 反查 pack_format 对应的版本区间，给出可读说明
        QString packRangeText;
        for (const PackFormatEntry &e : k_packFormats)
        {
            if (e.format == packFormat)
            {
                packRangeText = QByteArray(e.minVersion) == QByteArray(e.maxVersion)
                                    ? QString::fromLatin1(e.minVersion)
                                    : QString("%1 ~ %2").arg(e.minVersion, e.maxVersion);
                break;
            }
        }

        const bool tooNew = packFormat > expected;
        CompatIssue issue;
        issue.severity = CompatIssue::Warning;
        issue.category = "resourcepack";
        issue.fileName = name;
        issue.title = tooNew ? QString("资源包为更新的游戏版本设计")
                             : QString("资源包为更旧的游戏版本设计");
        issue.detail = QString("「%1」的 pack_format 为 %2%3，当前实例 %4 需要 pack_format %5，"
                               "加载可能出现材质缺失或部分内容失效。")
                           .arg(name)
                           .arg(packFormat)
                           .arg(packRangeText.isEmpty() ? QString() : QString("（适配 %1）").arg(packRangeText))
                           .arg(gameVersion)
                           .arg(expected);
        issue.suggestion = QString("更换为适配 %1 的资源包版本").arg(gameVersion);
        addIssue(report, issue);
    };

    for (const QString &zip : zips)
        checkOne(zip, true);
    for (const QString &folder : folders)
        checkOne(folder, false);
}

// =====================================================================
// 光影包检查
// =====================================================================

void LocalCompatibilityChecker::checkShaderPacks(CompatReport &report,
                                                 const QString &shaderPackDir,
                                                 const QList<ModMeta> &mods,
                                                  const QString &loaderType)
{
    Q_UNUSED(loaderType);
    QDir dir(shaderPackDir);
    if (!dir.exists())
        return;

    const QStringList zips = listFiles(shaderPackDir, { ".zip" });
    report.shaderPackCount = zips.size();
    if (zips.isEmpty())
        return;

    // 有效光影包必须包含 shaders/ 目录（Iris 与 OptiFine 格式一致）
    QStringList invalid;
    for (const QString &zip : zips)
    {
        if (JarUtils::listEntriesInJar(zip, "shaders/").isEmpty())
            invalid << QFileInfo(zip).fileName();
    }
    if (!invalid.isEmpty())
    {
        CompatIssue issue;
        issue.severity = CompatIssue::Error;
        issue.category = "shaderpack";
        issue.fileName = invalid.join(", ");
        issue.title = QString("不是有效的光影包");
        issue.detail = QString("以下压缩包缺少 shaders 目录，不是光影包（可能是材质包误放入光影目录）：%1")
                           .arg(invalid.join("、"));
        issue.suggestion = QString("移到 resourcepacks 目录或删除");
        addIssue(report, issue);
    }

    // 光影加载器可用性
    bool hasLoader = false;
    for (const ModMeta &mod : mods)
    {
        if (!mod.enabled)
            continue;
        if (mod.isOptifine || mod.modId == "iris" || mod.provides.contains("iris")
            || mod.modId == "oculus" || mod.provides.contains("oculus"))
        {
            hasLoader = true;
            break;
        }
    }
    if (!hasLoader)
    {
        CompatIssue issue;
        issue.severity = CompatIssue::Warning;
        issue.category = "shaderpack";
        issue.title = QString("未安装光影加载器，光影包不会生效");
        issue.detail = QString("实例中安装了 %1 个光影包，但没有 Iris / OptiFine / Oculus，游戏内无法开启光影。")
                           .arg(zips.size());
        issue.suggestion = QString("在模组管理中安装 Iris（Fabric）或 Oculus（Forge）");
        addIssue(report, issue);
    }
}

// =====================================================================
// 工具函数
// =====================================================================

void LocalCompatibilityChecker::addIssue(CompatReport &report, const CompatIssue &issue)
{
    report.issues.append(issue);
}

QStringList LocalCompatibilityChecker::listFiles(const QString &dirPath, const QStringList &suffixes)
{
    QStringList result;
    QDir dir(dirPath);
    if (!dir.exists())
        return result;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::NoIteratorFlags);
    while (it.hasNext())
    {
        it.next();
        const QString path = it.filePath();
        bool match = suffixes.isEmpty();
        for (const QString &suffix : suffixes)
        {
            if (path.endsWith(suffix, Qt::CaseInsensitive))
            {
                match = true;
                break;
            }
        }
        if (match)
            result << path;
    }
    result.sort();
    return result;
}

// =====================================================================
// 版本比较与范围求值
// =====================================================================

LocalCompatibilityChecker::VerSeg LocalCompatibilityChecker::parseVersion(const QString &version)
{
    VerSeg seg;
    QString v = version.trimmed();
    if (v.isEmpty())
        return seg;

    // 去掉构建元数据；分离预发布段
    int plus = v.indexOf('+');
    if (plus >= 0)
        v = v.left(plus);
    int dash = v.indexOf('-');
    if (dash >= 0)
    {
        seg.prerelease = v.mid(dash + 1);
        v = v.left(dash);
    }

    const QStringList parts = v.split('.');
    for (const QString &p : parts)
    {
        bool ok = false;
        const int n = p.toInt(&ok);
        seg.numbers.append(ok ? n : 0); // 非数字段按 0 处理，保持宽松
    }
    seg.valid = !seg.numbers.isEmpty();
    return seg;
}

int LocalCompatibilityChecker::compareVersions(const QString &a, const QString &b)
{
    return compareSegments(parseVersion(a), parseVersion(b));
}

int LocalCompatibilityChecker::compareSegments(const VerSeg &a, const VerSeg &b)
{
    const int maxLen = qMax(a.numbers.size(), b.numbers.size());
    for (int i = 0; i < maxLen; ++i)
    {
        const int na = (i < a.numbers.size()) ? a.numbers.at(i) : 0;
        const int nb = (i < b.numbers.size()) ? b.numbers.at(i) : 0;
        if (na != nb)
            return na < nb ? -1 : 1;
    }
    // 版本号相同：有预发布段的更低（1.0.0-pre < 1.0.0）
    if (a.prerelease.isEmpty() && b.prerelease.isEmpty())
        return 0;
    if (a.prerelease.isEmpty())
        return 1;
    if (b.prerelease.isEmpty())
        return -1;
    return a.prerelease.compare(b.prerelease);
}

bool LocalCompatibilityChecker::constraintSatisfied(const VerSeg &ver, const QString &constraint)
{
    QString c = constraint.trimmed();
    if (c.isEmpty() || c == "*" || c == "x" || c == "X")
        return true;

    // 通配符 x-range：1.20.x / 1.x / 1.2.*
    if (c.contains('x') || c.contains('X') || c.contains('*'))
    {
        static const QRegularExpression wildcard("^([0-9]+)(?:\\.([0-9]+|x|X|\\*))?(?:\\.([0-9]+|x|X|\\*))?$");
        QRegularExpressionMatch m = wildcard.match(c);
        if (m.hasMatch())
        {
            const int major = m.captured(1).toInt();
            const QString minor = m.captured(2);
            const QString patch = m.captured(3);
            const bool minorWild = minor.isEmpty() || minor.toLower() == "x" || minor == "*";
            if (minorWild)
                return ver.numbers.value(0, -1) == major;
            const bool patchWild = patch.isEmpty() || patch.toLower() == "x" || patch == "*";
            if (patchWild)
                return ver.numbers.value(0, -1) == major
                           && ver.numbers.value(1, -1) == minor.toInt();
            // "1.2.3" 带 * 的组合不会走到这里
        }
        // 无法按通配符解析 → 继续走下面的普通比较
    }

    if (c.startsWith(">="))
        return compareSegments(ver, parseVersion(c.mid(2))) >= 0;
    if (c.startsWith("<="))
        return compareSegments(ver, parseVersion(c.mid(2))) <= 0;
    if (c.startsWith(">"))
        return compareSegments(ver, parseVersion(c.mid(1))) > 0;
    if (c.startsWith("<"))
        return compareSegments(ver, parseVersion(c.mid(1))) < 0;
    if (c.startsWith("="))
        return compareSegments(ver, parseVersion(c.mid(1))) == 0;
    if (c.startsWith('^'))
    {
        // ^1.2.3 → >=1.2.3 <2.0.0；^0.2.3 → <0.3.0；^0.0.3 → <0.0.4
        const VerSeg base = parseVersion(c.mid(1));
        if (!base.valid)
            return true;
        VerSeg upper = base;
        if (base.numbers.value(0, 0) > 0)
        {
            upper.numbers[0] = base.numbers.at(0) + 1;
        }
        else if (base.numbers.size() >= 2 && base.numbers.value(1, 0) > 0)
        {
            upper.numbers[1] = base.numbers.at(1) + 1;
        }
        else if (base.numbers.size() >= 3 && base.numbers.value(2, 0) > 0)
        {
            upper.numbers[2] = base.numbers.at(2) + 1;
        }
        else
        {
            upper = parseVersion("1.0.0"); // ^0 / ^0.0 → <1.0.0
        }
        upper.prerelease.clear();
        return compareSegments(ver, base) >= 0 && compareSegments(ver, upper) < 0;
    }
    if (c.startsWith('~'))
    {
        // ~1.2.3 → >=1.2.3 <1.3.0；~1 → >=1 <2
        const VerSeg base = parseVersion(c.mid(1));
        if (!base.valid)
            return true;
        VerSeg upper = base;
        if (base.numbers.size() >= 2)
            upper.numbers[1] = base.numbers.at(1) + 1;
        else
            upper.numbers[0] = base.numbers.at(0) + 1;
        upper.prerelease.clear();
        return compareSegments(ver, base) >= 0 && compareSegments(ver, upper) < 0;
    }

    // 裸版本 → 精确匹配（node-semver 语义）
    const VerSeg target = parseVersion(c);
    if (!target.valid)
    {
        // 无法解析的声明，退化为前缀比较
        QStringList parts;
        for (int n : ver.numbers)
            parts << QString::number(n);
        return parts.join('.').startsWith(c);
    }
    return compareSegments(ver, target) == 0;
}

bool LocalCompatibilityChecker::semverSatisfies(const QString &version, const QString &range)
{
    const QString r = range.trimmed();
    if (r.isEmpty() || r == "*")
        return true;

    // "||" 分隔的多个可选范围
    const QStringList groups = r.split("||");
    if (groups.size() > 1)
    {
        for (const QString &g : groups)
        {
            if (semverSatisfies(version, g))
                return true;
        }
        return false;
    }

    // 空格分隔的多个条件 → AND
    const VerSeg ver = parseVersion(version);
    if (!ver.valid)
        return true; // 版本未知时不误报
    const QStringList constraints = r.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (const QString &c : constraints)
    {
        if (!constraintSatisfied(ver, c))
            return false;
    }
    return true;
}

bool LocalCompatibilityChecker::forgeRangeSatisfies(const QString &version, const QString &range)
{
    const QString r = range.trimmed();
    if (r.isEmpty() || r == "*")
        return true;

    const VerSeg ver = parseVersion(version);
    if (!ver.valid)
        return true;

    // 提取所有方括号区段（如 "[1.20,)" 或 "[1.19,1.20.1),[1.20.2,)"）
    static const QRegularExpression groupRx("([\\(\\[])([^\\]\\)]*)([\\]\\)])");
    QRegularExpressionMatchIterator it = groupRx.globalMatch(r);

    bool hasGroup = false;
    while (it.hasNext())
    {
        const QRegularExpressionMatch m = it.next();
        hasGroup = true;
        const bool lowerInclusive = (m.captured(1) == "[");
        const bool upperInclusive = (m.captured(3) == "]");

        const QStringList bounds = m.captured(2).split(',');
        const QString lowerB = bounds.value(0).trimmed();
        const QString upperB = bounds.size() > 1 ? bounds.value(1).trimmed() : QString();

        bool ok = true;
        if (!lowerB.isEmpty())
        {
            const int cmp = compareSegments(ver, parseVersion(lowerB));
            ok = lowerInclusive ? (cmp >= 0) : (cmp > 0);
        }
        if (ok && !upperB.isEmpty())
        {
            const int cmp = compareSegments(ver, parseVersion(upperB));
            ok = upperInclusive ? (cmp <= 0) : (cmp < 0);
        }
        if (ok)
            return true;
    }

    if (hasGroup)
        return false;

    // 无括号：裸 "1.20" 按精确匹配
    return compareSegments(ver, parseVersion(r)) == 0;
}

int LocalCompatibilityChecker::resourcePackFormatFor(const QString &gameVersion)
{
    QString v = gameVersion.trimmed();
    if (v.isEmpty())
        return -1;

    // 去掉预发布后缀（1.20.1-pre2 → 1.20.1）
    int dash = v.indexOf('-');
    if (dash > 0)
        v = v.left(dash);

    // 正式版本号形如 1.20 / 1.21.8；快照（24w33a）、旧版（b1.7.3）等返回 -1
    static const QRegularExpression releaseRx("^\\d+\\.\\d+(\\.\\d+)?$");
    if (!releaseRx.match(v).hasMatch())
        return -1;

    for (const PackFormatEntry &e : k_packFormats)
    {
        if (compareVersions(v, QString::fromLatin1(e.minVersion)) >= 0
            && compareVersions(v, QString::fromLatin1(e.maxVersion)) <= 0)
        {
            return e.format;
        }
    }
    return -2; // 正式版本号但超出已知映射（更新的正式版）
}
