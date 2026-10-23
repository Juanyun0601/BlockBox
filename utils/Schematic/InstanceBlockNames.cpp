/**
 * @file   InstanceBlockNames.cpp
 * @brief  方块中文名（实例语言文件 + 内置兜底）与中文输入解析实现
 * @author BlockBox Team
 * @date   2026-10-17
 */

#include "InstanceBlockNames.h"

#include "SchematicDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

#include "utils/JarUtils.h"

namespace InstanceBlockNames {

namespace {

/**
 * @brief 内置常见方块中文名兜底表
 *
 * 实例没有语言文件（未绑定实例、旧版本、精简整合包）时使用；
 * 只收录编辑器常用方块，键为小写 "ns:id"。
 */
const QHash<QString, QString> &builtinTable()
{
    static const QHash<QString, QString> table = {
        { QStringLiteral("minecraft:air"), QStringLiteral("空气") },
        { QStringLiteral("minecraft:stone"), QStringLiteral("石头") },
        { QStringLiteral("minecraft:cobblestone"), QStringLiteral("圆石") },
        { QStringLiteral("minecraft:dirt"), QStringLiteral("泥土") },
        { QStringLiteral("minecraft:grass_block"), QStringLiteral("草方块") },
        { QStringLiteral("minecraft:sand"), QStringLiteral("沙子") },
        { QStringLiteral("minecraft:gravel"), QStringLiteral("沙砾") },
        { QStringLiteral("minecraft:glass"), QStringLiteral("玻璃") },
        { QStringLiteral("minecraft:bricks"), QStringLiteral("砖块") },
        { QStringLiteral("minecraft:water"), QStringLiteral("水") },
        { QStringLiteral("minecraft:lava"), QStringLiteral("熔岩") },
        { QStringLiteral("minecraft:oak_log"), QStringLiteral("橡木原木") },
        { QStringLiteral("minecraft:oak_planks"), QStringLiteral("橡木木板") },
        { QStringLiteral("minecraft:oak_leaves"), QStringLiteral("橡木树叶") },
        { QStringLiteral("minecraft:oak_stairs"), QStringLiteral("橡木楼梯") },
        { QStringLiteral("minecraft:stone_stairs"), QStringLiteral("石楼梯") },
        { QStringLiteral("minecraft:oak_slab"), QStringLiteral("橡木台阶") },
        { QStringLiteral("minecraft:stone_slab"), QStringLiteral("石台阶") },
        { QStringLiteral("minecraft:oak_fence"), QStringLiteral("橡木栅栏") },
        { QStringLiteral("minecraft:oak_door"), QStringLiteral("橡木门") },
        { QStringLiteral("minecraft:oak_trapdoor"), QStringLiteral("橡木活板门") },
        { QStringLiteral("minecraft:iron_trapdoor"), QStringLiteral("铁活板门") },
        { QStringLiteral("minecraft:dark_oak_trapdoor"), QStringLiteral("深色橡木活板门") },
        { QStringLiteral("minecraft:iron_block"), QStringLiteral("铁块") },
        { QStringLiteral("minecraft:gold_block"), QStringLiteral("金块") },
        { QStringLiteral("minecraft:diamond_block"), QStringLiteral("钻石块") },
        { QStringLiteral("minecraft:redstone_block"), QStringLiteral("红石块") },
        { QStringLiteral("minecraft:redstone_wire"), QStringLiteral("红石线") },
        { QStringLiteral("minecraft:sea_lantern"), QStringLiteral("海晶灯") },
        { QStringLiteral("minecraft:glowstone"), QStringLiteral("荧石") },
        { QStringLiteral("minecraft:torch"), QStringLiteral("火把") },
        { QStringLiteral("minecraft:wall_torch"), QStringLiteral("墙上的火把") },
        { QStringLiteral("minecraft:lever"), QStringLiteral("拉杆") },
        { QStringLiteral("minecraft:chest"), QStringLiteral("箱子") },
        { QStringLiteral("minecraft:sign"), QStringLiteral("橡木告示牌") },
        { QStringLiteral("minecraft:oak_sign"), QStringLiteral("橡木告示牌") },
        { QStringLiteral("minecraft:oak_wall_sign"), QStringLiteral("橡木告示牌") },
        { QStringLiteral("minecraft:white_wool"), QStringLiteral("白色羊毛") },
        { QStringLiteral("minecraft:black_wool"), QStringLiteral("黑色羊毛") },
        { QStringLiteral("minecraft:red_wool"), QStringLiteral("红色羊毛") },
        { QStringLiteral("minecraft:blue_wool"), QStringLiteral("蓝色羊毛") },
        { QStringLiteral("minecraft:oak_fence_gate"), QStringLiteral("橡木栅栏门") },
        { QStringLiteral("minecraft:cobblestone_wall"), QStringLiteral("圆石墙") },
        { QStringLiteral("minecraft:ladder"), QStringLiteral("梯子") },
        { QStringLiteral("minecraft:barrel"), QStringLiteral("木桶") },
        { QStringLiteral("minecraft:furnace"), QStringLiteral("熔炉") },
        { QStringLiteral("minecraft:crafting_table"), QStringLiteral("工作台") },
        { QStringLiteral("minecraft:bookshelf"), QStringLiteral("书架") },
        { QStringLiteral("minecraft:obsidian"), QStringLiteral("黑曜石") },
        { QStringLiteral("minecraft:bedrock"), QStringLiteral("基岩") },
        { QStringLiteral("minecraft:netherrack"), QStringLiteral("下界岩") },
        { QStringLiteral("minecraft:end_stone"), QStringLiteral("末地石") },
    };
    return table;
}

/** 去掉命名空间：minecraft:oak_log -> oak_log */
QString shortId(const QString &blockId)
{
    const QString n = baseBlockId(blockId);
    const int slash = n.indexOf(QLatin1Char(':'));
    return slash >= 0 ? n.mid(slash + 1) : n;
}

QString facingCn(const QString &value)
{
    if (value == QLatin1String("north")) return QStringLiteral("北面");
    if (value == QLatin1String("south")) return QStringLiteral("南面");
    if (value == QLatin1String("east"))  return QStringLiteral("东面");
    if (value == QLatin1String("west"))  return QStringLiteral("西面");
    if (value == QLatin1String("up"))    return QStringLiteral("顶面");
    if (value == QLatin1String("down"))  return QStringLiteral("底面");
    return QString();
}

/** true/false -> 自定义中文；其它值返回空 */
QString boolCn(const QString &value, const QString &on, const QString &off)
{
    if (value == QLatin1String("true"))  return on;
    if (value == QLatin1String("false")) return off;
    return QString();
}

/** 单个方块属性的中文描述；未收录返回空（调用方按 key=value 原样显示） */
QString cnPropertyValue(const QString &key, const QString &value)
{
    if (key == QLatin1String("facing"))
        return facingCn(value);
    if (key == QLatin1String("half"))
    {
        if (value == QLatin1String("bottom") || value == QLatin1String("lower"))
            return QStringLiteral("下半");
        if (value == QLatin1String("top") || value == QLatin1String("upper"))
            return QStringLiteral("上半");
        return QString();
    }
    if (key == QLatin1String("open"))
        return boolCn(value, QStringLiteral("开启"), QStringLiteral("关闭"));
    if (key == QLatin1String("powered"))
        return boolCn(value, QStringLiteral("已激活"), QStringLiteral("未激活"));
    if (key == QLatin1String("waterlogged"))
        return boolCn(value, QStringLiteral("含水"), QStringLiteral("无水"));
    if (key == QLatin1String("lit"))
        return boolCn(value, QStringLiteral("点燃"), QStringLiteral("熄灭"));
    if (key == QLatin1String("conditional"))
        return boolCn(value, QStringLiteral("有条件"), QStringLiteral("无条件"));
    if (key == QLatin1String("hinge"))
    {
        if (value == QLatin1String("left"))  return QStringLiteral("合页左");
        if (value == QLatin1String("right")) return QStringLiteral("合页右");
        return QString();
    }
    if (key == QLatin1String("axis"))
    {
        if (value == QLatin1String("x")) return QStringLiteral("X轴");
        if (value == QLatin1String("y")) return QStringLiteral("Y轴");
        if (value == QLatin1String("z")) return QStringLiteral("Z轴");
        return QString();
    }
    if (key == QLatin1String("face"))
    {
        if (value == QLatin1String("wall"))    return QStringLiteral("墙面");
        if (value == QLatin1String("floor"))   return QStringLiteral("地面");
        if (value == QLatin1String("ceiling")) return QStringLiteral("天花板");
        return QString();
    }
    if (key == QLatin1String("type"))
    {
        if (value == QLatin1String("top"))    return QStringLiteral("上半");
        if (value == QLatin1String("bottom")) return QStringLiteral("下半");
        if (value == QLatin1String("double")) return QStringLiteral("双层");
        if (value == QLatin1String("single")) return QStringLiteral("单个");
        if (value == QLatin1String("left"))   return QStringLiteral("左");
        if (value == QLatin1String("right"))  return QStringLiteral("右");
        return QString();
    }
    if (key == QLatin1String("shape"))
    {
        if (value == QLatin1String("straight"))    return QStringLiteral("直行");
        if (value == QLatin1String("inner_left"))  return QStringLiteral("内左");
        if (value == QLatin1String("inner_right")) return QStringLiteral("内右");
        if (value == QLatin1String("outer_left"))  return QStringLiteral("外左");
        if (value == QLatin1String("outer_right")) return QStringLiteral("外右");
        return QString();
    }
    // 栅栏/墙的四面连接：east=true 等
    if (key == QLatin1String("north") || key == QLatin1String("south")
        || key == QLatin1String("east") || key == QLatin1String("west")
        || key == QLatin1String("up") || key == QLatin1String("down"))
    {
        QString dir;
        if (key == QLatin1String("north")) dir = QStringLiteral("北");
        else if (key == QLatin1String("south")) dir = QStringLiteral("南");
        else if (key == QLatin1String("east")) dir = QStringLiteral("东");
        else if (key == QLatin1String("west")) dir = QStringLiteral("西");
        else if (key == QLatin1String("up")) dir = QStringLiteral("上");
        else dir = QStringLiteral("下");
        const QString state = boolCn(value, QStringLiteral("连"), QStringLiteral("断"));
        return state.isEmpty() ? QString() : dir + state;
    }
    return QString();
}

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
    const QString key = baseBlockId(blockId).toLower();
    if (key.isEmpty())
        return QString();

    // 没有命名空间时，也按原版方块试一次（oak_trapdoor -> minecraft:oak_trapdoor）
    QStringList keys;
    keys << key;
    if (!key.contains(QLatin1Char(':')))
        keys << QStringLiteral("minecraft:") + key;

    // 1) 实例语言文件
    for (const QString &k : keys)
    {
        const auto it = cnMap.constFind(k);
        if (it != cnMap.constEnd() && !it.value().isEmpty())
            return it.value();
    }
    // 2) 内置常见方块兜底
    for (const QString &k : keys)
    {
        const auto it = builtinTable().constFind(k);
        if (it != builtinTable().constEnd() && !it.value().isEmpty())
            return it.value();
    }
    return QString();
}

QString displayName(const QHash<QString, QString> &cnMap, const QString &blockId)
{
    const QString trimmed = blockId.trimmed();
    if (trimmed.isEmpty())
        return QString();

    QString base = lookup(cnMap, trimmed);
    if (base.isEmpty())
        base = shortId(trimmed);          // 没有中文名时回退英文短 ID
    if (base.isEmpty())
        base = trimmed;

    const SchematicBlockState st = SchematicBlockState::fromString(trimmed);
    if (st.properties.isEmpty())
        return base;

    // 状态摘要：已收录的属性译成中文，未收录的原样列出
    QStringList parts;
    for (auto it = st.properties.constBegin(); it != st.properties.constEnd(); ++it)
    {
        QString part = cnPropertyValue(it.key(), it.value());
        if (part.isEmpty())
            part = it.key() + QLatin1Char('=') + it.value();
        parts << part;
    }
    return QStringLiteral("%1（%2）").arg(base, parts.join(QStringLiteral("·")));
}

QString resolveId(const QHash<QString, QString> &cnMap, const QString &text)
{
    const QString s = text.trimmed();
    if (s.isEmpty())
        return QString();

    // 是否只含方块 ID 允许的字符（小写字母、数字、_ . / - :）
    auto isIdText = [](const QString &v) {
        if (v.isEmpty())
            return false;
        for (const QChar &ch : v)
        {
            const ushort u = ch.unicode();
            const bool ok = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')
                            || u == '_' || u == '.' || u == '/' || u == '-' || u == ':';
            if (!ok)
                return false;
        }
        return true;
    };

    // 没有命名空间时补上原版命名空间
    auto withNamespace = [](const QString &v) {
        return v.contains(QLatin1Char(':')) ? v : QStringLiteral("minecraft:") + v;
    };

    // 1) 「中文名 (english_id)」：括号里是 ID 就用它
    const int open = s.lastIndexOf(QStringLiteral(" ("));
    if (open > 0 && s.endsWith(QLatin1Char(')')))
    {
        const QString inner = s.mid(open + 2, s.length() - open - 3).trimmed();
        if (isIdText(baseBlockId(inner)))
            return withNamespace(inner);
    }

    // 2) 带命名空间的完整 ID（可能带状态）
    if (s.contains(QLatin1Char(':')))
        return s;

    // 3) 英文短 ID（可能带状态）：补 minecraft: 前缀
    const QString idPart = baseBlockId(s);
    if (isIdText(idPart))
        return withNamespace(idPart) + s.mid(idPart.length());

    // 4) 中文名（可能带「（状态摘要）」后缀）：反查实例译名，再查内置表
    QString name = s;
    const int cnOpen = name.indexOf(QStringLiteral("（"));
    if (cnOpen > 0)
        name = name.left(cnOpen).trimmed();

    for (auto it = cnMap.constBegin(); it != cnMap.constEnd(); ++it)
    {
        if (it.value() == name)
            return it.key();
    }
    for (auto it = builtinTable().constBegin(); it != builtinTable().constEnd(); ++it)
    {
        if (it.value() == name && it.key().startsWith(QLatin1String("minecraft:")))
            return it.key();
    }

    // 5) 无法识别：原样返回
    return s;
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
