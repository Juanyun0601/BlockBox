/**
 * @file   BiomeName.cpp
 * @brief  从当前实例读取群系中文名实现
 * @author BlockBox Team
 * @date   2026-10-17
 */

#include "BiomeName.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "utils/JarUtils.h"
#include "utils/WorldMap/BiomeCatalog.h"

#include "util.h" // biome2str（cubiomes；随版本把群系 id 映成资源名）

namespace BiomeName {

namespace {

/** 读版本 JSON 的 id 字段（目录名与 id 不一致时找 jar 用） */
QString readVersionJsonId(const QString &instancePath)
{
    const QString dirName = QFileInfo(instancePath).fileName();
    QFile f(instancePath + QStringLiteral("/") + dirName
            + QStringLiteral(".json"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject())
        return doc.object().value(QStringLiteral("id")).toString();
    return QString();
}

/** 从 lang JSON 提取 biome.* 键 → "minecraft:plains" 形式的映射 */
void mergeLangObject(const QJsonObject &obj, QHash<QString, QString> &names)
{
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
    {
        const QString key = it.key();
        const QString value = it.value().toString();
        if (value.isEmpty() || !key.startsWith(QStringLiteral("biome.")))
            continue;
        // biome.<ns>.<id> → 资源名 minecraft:<id>
        const int firstDot = key.indexOf(QLatin1Char('.'));
        const int secondDot = key.indexOf(QLatin1Char('.'), firstDot + 1);
        if (secondDot <= firstDot + 1)
            continue;
        const QString ns = key.mid(firstDot + 1, secondDot - firstDot - 1);
        const QString id = key.mid(secondDot + 1);
        if (id.isEmpty())
            continue;
        const QString resource = ns + QLatin1Char(':') + id;
        if (!names.contains(resource))
            names.insert(resource, value);
    }
}

} // namespace

QString findClientJar(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return QString();

    // 1) 标准 versions/<id>/ 布局：<目录名>.jar / <JSON id>.jar
    const QString dirName = QFileInfo(instancePath).fileName();
    QStringList candidates;
    candidates << instancePath + QStringLiteral("/") + dirName
                      + QStringLiteral(".jar");
    const QString id = readVersionJsonId(instancePath);
    if (!id.isEmpty() && id != dirName)
        candidates << instancePath + QStringLiteral("/") + id
                          + QStringLiteral(".jar");
    for (const QString &candidate : candidates)
        if (QFile::exists(candidate))
            return candidate;

    // 2) 目录里最大的 jar（PCL/HMCL 风格：<版本名>.jar 直接在目录下）
    qint64 bestSize = -1;
    QString bestJar;
    const QFileInfoList jars = QDir(instancePath).entryInfoList(
        QStringList() << QStringLiteral("*.jar"),
        QDir::Files, QDir::Name);
    for (const QFileInfo &fi : jars)
        if (fi.size() > bestSize)
        {
            bestSize = fi.size();
            bestJar = fi.absoluteFilePath();
        }
    if (!bestJar.isEmpty())
        return bestJar;

    // 3) 实例路径是游戏根目录：versions/<name>/<name>.jar
    QDir versionsDir(instancePath + QStringLiteral("/versions"));
    if (versionsDir.exists())
    {
        const QStringList dirs =
            versionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &v : dirs)
        {
            const QString candidate =
                versionsDir.absoluteFilePath(v + QStringLiteral("/") + v
                                             + QStringLiteral(".jar"));
            if (QFile::exists(candidate))
                return candidate;
        }
    }

    // 4) inheritsFrom 布局：原版父版本目录下的 <同名>.jar
    const QString parentJar = QFileInfo(instancePath).dir().absoluteFilePath(
        dirName + QStringLiteral(".jar"));
    if (QFile::exists(parentJar))
        return parentJar;

    return QString();
}

InstanceNames loadFromInstance(const QString &instancePath)
{
    InstanceNames names;
    const QString jar = findClientJar(instancePath);
    if (jar.isEmpty())
        return names;

    auto applyLang = [&jar](const QString &entry, QHash<QString, QString> &out) {
        QByteArray data;
        if (!JarUtils::extractFromJarToMemory(jar, entry, data))
            return;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return;
        mergeLangObject(doc.object(), out);
    };
    applyLang(QStringLiteral("assets/minecraft/lang/zh_cn.json"), names.zh);
    applyLang(QStringLiteral("assets/minecraft/lang/en_us.json"), names.en);
    return names;
}

QString chineseName(const QString &resourceName,
                    const InstanceNames &instanceNames)
{
    // 1) 实例 zh_cn.json（1.13+ 键形 biome.minecraft.plains）
    if (!resourceName.isEmpty() && !instanceNames.zh.isEmpty())
    {
        const auto it = instanceNames.zh.constFind(
            QStringLiteral("minecraft:") + resourceName);
        if (it != instanceNames.zh.constEnd())
            return it.value();
    }

    // 2) 内置常见群系表（key 与 biome2str 输出一致）——保证中文名
    const auto &defs = BiomeCatalog::allBiomes();
    for (const auto &d : defs)
        if (resourceName == QLatin1String(d.key))
            return QString::fromUtf8(d.displayName);

    // 3) 实例 en_us.json（覆盖内置表没有的群系，如模组群系）
    if (!resourceName.isEmpty() && !instanceNames.en.isEmpty())
    {
        const auto it = instanceNames.en.constFind(
            QStringLiteral("minecraft:") + resourceName);
        if (it != instanceNames.en.constEnd())
            return it.value();
    }

    // 4) 英文资源名兜底
    return resourceName;
}

QString nameForId(int mc, int id, const InstanceNames &instanceNames)
{
    const char *res = biome2str(mc, id);
    if (!res || !*res)
        return QString();
    return chineseName(QString::fromLatin1(res), instanceNames);
}

} // namespace BiomeName
