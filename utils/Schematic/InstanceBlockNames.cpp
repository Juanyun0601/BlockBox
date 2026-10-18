/**
 * @file   InstanceBlockNames.cpp
 * @brief  从游戏实例读取方块中文名实现
 * @author BlockBox Team
 * @date   2026-10-17
 */

#include "InstanceBlockNames.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "utils/JarUtils.h"

namespace InstanceBlockNames {

namespace {

bool looksChinese(const QString &text)
{
    for (const QChar &ch : text)
    {
        const ushort u = ch.unicode();
        if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF)
            || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0x3000 && u <= 0x303F)
            || (u >= 0xFF00 && u <= 0xFFEF))
        {
            return true;
        }
    }
    return false;
}

void applyBlockLangObject(const QJsonObject &obj, QHash<QString, QString> &out)
{
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
    {
        const QString &key = it.key();
        if (!key.startsWith(QLatin1String("block.")))
            continue;
        const QString value = it.value().toString();
        if (value.isEmpty() || !looksChinese(value))
            continue;
        // block.<ns>.<id> -> <ns>:<id>
        const QString body = key.mid(6);
        const int firstDot = body.indexOf(QLatin1Char('.'));
        if (firstDot <= 0)
            continue;
        const QString ns = body.left(firstDot);
        const QString id = body.mid(firstDot + 1);
        if (id.isEmpty())
            continue;
        out.insert((ns + QLatin1Char(':') + id).toLower(), value);
    }
}

bool loadLangJsonFile(const QString &path, QHash<QString, QString> &out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    applyBlockLangObject(doc.object(), out);
    return true;
}

bool loadLangFromJar(const QString &jarPath, const QString &entry,
                     QHash<QString, QString> &out)
{
    QByteArray data;
    if (!JarUtils::extractFromJarToMemory(jarPath, entry, data))
        return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    applyBlockLangObject(doc.object(), out);
    return true;
}

bool loadZhFromAssets(const QString &assetsRoot, const QString &assetIndexId,
                      QHash<QString, QString> &out)
{
    if (assetsRoot.isEmpty() || assetIndexId.isEmpty())
        return false;
    const QString indexPath = assetsRoot + QStringLiteral("/indexes/")
                              + assetIndexId + QStringLiteral(".json");
    QFile idxFile(indexPath);
    if (!idxFile.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(idxFile.readAll(), &err);
    idxFile.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject objects =
        doc.object().value(QStringLiteral("objects")).toObject();
    const QJsonObject zh =
        objects.value(QStringLiteral("minecraft/lang/zh_cn.json")).toObject();
    const QString hash = zh.value(QStringLiteral("hash")).toString();
    if (hash.size() < 2)
        return false;
    const QString objPath = assetsRoot + QStringLiteral("/objects/")
                            + hash.left(2) + QLatin1Char('/') + hash;
    return loadLangJsonFile(objPath, out);
}

QString findAssetIndexId(const QString &versionDir)
{
    const QString base = QFileInfo(versionDir).fileName();
    const QStringList candidates = {
        versionDir + QLatin1Char('/') + base + QStringLiteral(".json"),
        versionDir + QStringLiteral("/version.json"),
    };
    for (const QString &path : candidates)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        f.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        const QString id = doc.object()
                               .value(QStringLiteral("assetIndex"))
                               .toObject()
                               .value(QStringLiteral("id"))
                               .toString();
        if (!id.isEmpty())
            return id;
    }
    return QString();
}

QStringList candidateAssetRoots(const QString &instancePath)
{
    QStringList roots;
    const auto add = [&roots](const QString &p) {
        if (p.isEmpty())
            return;
        const QString abs = QDir::cleanPath(QDir(p).absolutePath());
        if (abs.isEmpty() || roots.contains(abs) || !QDir(abs).exists())
            return;
        roots.append(abs);
    };
    const QFileInfo instInfo(instancePath);
    const QString verDir = instInfo.dir().absolutePath();          // .../versions
    const QString gameDir = QDir(verDir).absolutePath();           // .../versions
    const QString gameParent = QDir(verDir + QStringLiteral("/..")).absolutePath(); // .../.minecraft
    const QString gameGrand = QDir(gameParent + QStringLiteral("/..")).absolutePath();

    // 版本隔离：实例目录自带 assets
    add(instancePath + QStringLiteral("/assets"));
    add(instancePath);

    // 共享 .minecraft：assets 在 versions 的上级（.minecraft/assets）
    add(gameParent + QStringLiteral("/assets"));
    add(gameDir + QStringLiteral("/../assets"));
    add(gameGrand + QStringLiteral("/assets"));

    // 实例根下再嵌一层 .minecraft
    add(instancePath + QStringLiteral("/.minecraft/assets"));
    add(gameParent + QStringLiteral("/.minecraft/assets"));
    return roots;
}

QString findClientJar(const QString &instancePath)
{
    const QDir dir(instancePath);
    if (!dir.exists())
        return QString();
    const QString base = QFileInfo(instancePath).fileName();
    QString bestJar;
    qint64 bestSize = -1;
    const QFileInfoList jars =
        dir.entryInfoList({QStringLiteral("*.jar")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : jars)
    {
        if (fi.completeBaseName() == base)
            return fi.absoluteFilePath();
        if (fi.size() > bestSize)
        {
            bestSize = fi.size();
            bestJar = fi.absoluteFilePath();
        }
    }
    return bestJar;
}

void loadFromMods(const QString &instancePath, QHash<QString, QString> &out)
{
    const QStringList modRoots = {
        instancePath + QStringLiteral("/mods"),
        instancePath + QStringLiteral("/.minecraft/mods"),
        QFileInfo(instancePath).dir().absolutePath() + QStringLiteral("/mods"),
        QFileInfo(instancePath).dir().absolutePath() + QStringLiteral("/../mods"),
    };
    for (const QString &modRoot : modRoots)
    {
        const QDir modDir(modRoot);
        if (!modDir.exists())
            continue;
        const QFileInfoList jars =
            modDir.entryInfoList({QStringLiteral("*.jar")}, QDir::Files, QDir::Time);
        for (const QFileInfo &fi : jars)
        {
            loadLangFromJar(fi.absoluteFilePath(),
                            QStringLiteral("assets/minecraft/lang/zh_cn.json"), out);
        }
    }
}

void loadFromResourcePacks(const QString &instancePath, QHash<QString, QString> &out)
{
    const QStringList packRoots = {
        instancePath + QStringLiteral("/resourcepacks"),
        instancePath + QStringLiteral("/.minecraft/resourcepacks"),
        QFileInfo(instancePath).dir().absolutePath() + QStringLiteral("/resourcepacks"),
        QFileInfo(instancePath).dir().absolutePath() + QStringLiteral("/../resourcepacks"),
    };
    const QStringList zipEntries = {
        QStringLiteral("assets/minecraft/lang/zh_cn.json"),
        QStringLiteral("minecraft/lang/zh_cn.json"),
    };
    for (const QString &packRoot : packRoots)
    {
        const QDir packDir(packRoot);
        if (!packDir.exists())
            continue;
        const QFileInfoList zips =
            packDir.entryInfoList({QStringLiteral("*.zip")}, QDir::Files, QDir::Time);
        for (const QFileInfo &fi : zips)
        {
            for (const QString &entry : zipEntries)
                loadLangFromJar(fi.absoluteFilePath(), entry, out);
        }
        const QFileInfoList dirs =
            packDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        for (const QFileInfo &di : dirs)
        {
            loadLangJsonFile(di.absoluteFilePath()
                                 + QStringLiteral("/assets/minecraft/lang/zh_cn.json"),
                             out);
        }
    }
}

} // namespace

QString baseBlockId(const QString &blockId)
{
    QString n = blockId.trimmed();
    const int bracket = n.indexOf(QLatin1Char('['));
    if (bracket >= 0)
        n = n.left(bracket);
    return n;
}

QString lookup(const QHash<QString, QString> &cnMap, const QString &blockId)
{
    return cnMap.value(baseBlockId(blockId).toLower());
}

QHash<QString, QString> loadChineseMap(const QString &instancePath)
{
    QHash<QString, QString> out;
    if (instancePath.isEmpty() || !QDir(instancePath).exists())
        return out;

    // 1) 实例 assets 索引里的 zh_cn
    const QString assetIndexId = findAssetIndexId(instancePath);
    for (const QString &root : candidateAssetRoots(instancePath))
    {
        if (!assetIndexId.isEmpty() && loadZhFromAssets(root, assetIndexId, out)
            && !out.isEmpty())
        {
            break;
        }
        if (out.isEmpty())
        {
            const QDir idxDir(root + QStringLiteral("/indexes"));
            if (!idxDir.exists())
                continue;
            const QStringList idxs =
                idxDir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
            for (const QString &idx : idxs)
            {
                if (loadZhFromAssets(root, QFileInfo(idx).completeBaseName(), out)
                    && !out.isEmpty())
                {
                    break;
                }
            }
            if (!out.isEmpty())
                break;
        }
    }

    // 2) 客户端 JAR 内 zh_cn
    if (out.isEmpty())
    {
        const QString jar = findClientJar(instancePath);
        if (!jar.isEmpty())
        {
            loadLangFromJar(jar, QStringLiteral("assets/minecraft/lang/zh_cn.json"),
                            out);
        }
    }

    // 3) 模组 / 资源包补充
    loadFromMods(instancePath, out);
    loadFromResourcePacks(instancePath, out);
    return out;
}

} // namespace InstanceBlockNames
