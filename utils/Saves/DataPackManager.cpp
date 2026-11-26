/**
 * @file   DataPackManager.cpp
 * @brief  本地数据包扫描、启停、排序、预览与兼容性检测实现
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 数据包（data pack）按世界生效：文件放在世界目录的 datapacks/ 下，
 * 启停状态与加载顺序写在 level.dat 的 Data.DataPacks.Enabled / Disabled。
 * 本文件只依赖 Qt Core、JarUtils（进程内 ZIP 解析）与 LevelDatEditor，
 * 不涉及任何界面代码，方便桌面端 / 安卓 / 鸿蒙共用。
 */
#include "DataPackManager.h"

#include "LevelDatEditor.h"

#include "../JarUtils.h"
#include "../Safety.h"
#include "../compat/LocalCompatibilityChecker.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <climits>

namespace {

// ============================================================================
// 数据包格式 → 兼容的 MC 版本区间
//
// 数据来源：minecraft.wiki「Pack format」的数据包格式表（正式版）。
// 1.21.9 起格式号变为「主版本.次版本」（如 88.0、94.1、101.1），
// 表内 minor 为 0 的项是主版本升级，非 0 的项是数值型小改动。
// 表外的版本（快照、26w14a 这类开发版）一律按「无法判断」处理。
// ============================================================================
struct DataPackFormatEntry
{
    int major;
    int minor;
    const char *minVersion;
    const char *maxVersion;
};

const QList<DataPackFormatEntry> k_dataPackFormats = {
    {4,   0, "1.13",    "1.14.4"},
    {5,   0, "1.15",    "1.16.1"},
    {6,   0, "1.16.2",  "1.16.5"},
    {7,   0, "1.17",    "1.17.1"},
    {8,   0, "1.18",    "1.18.1"},
    {9,   0, "1.18.2",  "1.18.2"},
    {10,  0, "1.19",    "1.19.3"},
    {12,  0, "1.19.4",  "1.19.4"},
    {15,  0, "1.20",    "1.20.1"},
    {18,  0, "1.20.2",  "1.20.2"},
    {26,  0, "1.20.3",  "1.20.4"},
    {41,  0, "1.20.5",  "1.20.6"},
    {48,  0, "1.21",    "1.21.1"},
    {57,  0, "1.21.2",  "1.21.3"},
    {61,  0, "1.21.4",  "1.21.4"},
    {71,  0, "1.21.5",  "1.21.5"},
    {80,  0, "1.21.6",  "1.21.6"},
    {81,  0, "1.21.7",  "1.21.8"},
    {88,  0, "1.21.9",  "1.21.10"},
    {94,  1, "1.21.11", "1.21.11"},
    {101, 1, "26.1",    "26.1.2"},
    {107, 1, "26.2",    "26.2"},
    {121, 0, "26.3",    "26.3"},
};

// 实验性内容目录：这些目录里出现文件时，游戏打开世界会提示「实验性设置」
const QSet<QString> k_experimentalKeys = {
    "banner_pattern", "block_transformer", "cat_variant", "chat_type",
    "chicken_variant", "cow_variant", "damage_type", "decorated_pot_pattern",
    "dialog", "dimension", "dimension_type", "enchantment", "enchantment_provider",
    "frog_variant", "instrument", "jukebox_song", "painting_variant",
    "pig_variant", "sulfur_cube_archetype", "test_environment", "test_instance",
    "timeline", "trade_set", "trial_spawner", "trim_material", "trim_pattern",
    "villager_trade", "wolf_sound_variant", "wolf_variant", "world_clock",
    "worldgen", "zombie_nautilus_variant"
};

// 旧版本目录名 → 新版本目录名（1.21 起改成了单数）
const QHash<QString, QString> k_legacyTypeNames = {
    {"functions", "function"},
    {"advancements", "advancement"},
    {"recipes", "recipe"},
    {"loot_tables", "loot_table"},
    {"predicates", "predicate"},
    {"item_modifiers", "item_modifier"},
    {"structures", "structure"},
};

/** 文件型数据包在 level.dat 中的条目名 */
QString fileEntryId(const QString &fileName)
{
    return QStringLiteral("file/") + fileName;
}

/** 去掉 level.dat 条目可能的 "file/" 前缀，得到文件名 */
QString stripFilePrefix(const QString &entry)
{
    if (entry.startsWith(QStringLiteral("file/"), Qt::CaseInsensitive))
        return entry.mid(5);
    return entry;
}

/** 数据包条目是否为文件型（file/xxx） */
bool looksLikeFileEntry(const QString &entry)
{
    return entry.startsWith(QStringLiteral("file/"), Qt::CaseInsensitive);
}

/** 格式号比较：先主版本、再次版本 */
bool formatLess(int majorA, int minorA, int majorB, int minorB)
{
    if (majorA != majorB)
        return majorA < majorB;
    return minorA < minorB;
}

bool formatEqual(int majorA, int minorA, int majorB, int minorB)
{
    return majorA == majorB && minorA == minorB;
}

/** 解析 JSON 里的格式值：数字 / "88.0" 字符串 / [88, 0] 数组 */
bool parseFormatValue(const QJsonValue &value, int &major, int &minor)
{
    major = -1;
    minor = 0;
    if (value.isDouble())
    {
        major = value.toInt(-1);
        return major >= 0;
    }
    if (value.isString())
    {
        const QString text = value.toString().trimmed();
        if (text.isEmpty())
            return false;
        const QStringList parts = text.split('.'); 
        bool ok = false;
        major = parts.at(0).toInt(&ok);
        if (!ok || major < 0)
            return false;
        if (parts.size() > 1)
        {
            bool okMinor = false;
            minor = parts.at(1).toInt(&okMinor);
            if (!okMinor)
                minor = 0;
        }
        return true;
    }
    if (value.isArray())
    {
        const QJsonArray arr = value.toArray();
        if (arr.isEmpty())
            return false;
        if (!parseFormatValue(arr.at(0), major, minor))
            return false;
        if (arr.size() >= 2)
        {
            // 约定为 [主版本, 次版本]
            int secondMajor = -1;
            int secondMinor = 0;
            if (parseFormatValue(arr.at(1), secondMajor, secondMinor))
                minor = secondMajor;
        }
        return true;
    }
    return false;
}

/** 把 JSON 文本组件（字符串 / {text} / {translate,fallback} / 数组）压成纯文本 */
QString jsonTextToString(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isArray())
    {
        QStringList parts;
        for (const QJsonValue &item : value.toArray())
            parts.append(jsonTextToString(item));
        return parts.join(QString());
    }
    if (value.isObject())
    {
        const QJsonObject obj = value.toObject();
        if (obj.contains(QStringLiteral("text")))
            return obj.value(QStringLiteral("text")).toString();
        if (obj.contains(QStringLiteral("fallback")))
            return obj.value(QStringLiteral("fallback")).toString();
        if (obj.contains(QStringLiteral("translate")))
            return obj.value(QStringLiteral("translate")).toString();
        QStringList parts;
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            parts.append(jsonTextToString(it.value()));
        return parts.join(QString());
    }
    return QString();
}

/** 读取文件夹型数据包里的普通文件 */
QByteArray readFolderFile(const QString &dirPath, const QString &relativeName)
{
    QFile file(dirPath + QLatin1Char('/') + relativeName);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

/** 读取 zip 数据包内的文件（大小写不敏感地按文件名查找） */
QByteArray readZipFile(const QString &zipPath, const QString &entryName,
                       const QStringList *knownEntries)
{
    if (knownEntries && !knownEntries->isEmpty())
    {
        for (const QString &entry : *knownEntries)
        {
            if (entry.compare(entryName, Qt::CaseInsensitive) == 0)
            {
                QByteArray data;
                if (JarUtils::extractFromJarToMemory(zipPath, entry, data))
                    return data;
            }
        }
        // 条目清单不可靠（例如被截断）时再直接按原始名字尝试一次
    }
    QByteArray data;
    if (JarUtils::extractFromJarToMemory(zipPath, entryName, data))
        return data;
    return QByteArray();
}

/** 列出数据包内的全部文件（相对路径，统一用 /） */
QStringList listPackFiles(const DataPackInfo &info)
{
    QStringList files;
    if (!info.exists)
        return files;

    const QFileInfo fileInfo(info.filePath);
    if (fileInfo.isDir())
    {
        QDirIterator it(info.filePath, QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        const QString base = QDir::cleanPath(info.filePath);
        while (it.hasNext())
        {
            const QString abs = it.next();
            QString rel = QDir(base).relativeFilePath(abs);
            rel.replace('\\', '/');
            files.append(rel);
            if (files.size() >= DataPackManager::kMaxZipEntries)
                break;
        }
        return files;
    }

    if (!fileInfo.isFile())
        return files;

    files = JarUtils::listEntriesInJar(info.filePath);
    if (files.size() > DataPackManager::kMaxZipEntries)
        files = files.mid(0, DataPackManager::kMaxZipEntries);
    for (QString &name : files)
        name.replace('\\', '/');
    return files;
}

/** 读取并解析 pack.mcmeta，填充描述与格式字段 */
void readPackMeta(DataPackInfo &info, const QStringList &zipEntries)
{
    QByteArray metaBytes;
    const QFileInfo fileInfo(info.filePath);
    if (info.exists)
    {
        if (fileInfo.isDir())
            metaBytes = readFolderFile(info.filePath, QStringLiteral("pack.mcmeta"));
        else
            metaBytes = readZipFile(info.filePath, QStringLiteral("pack.mcmeta"), &zipEntries);
    }

    if (metaBytes.isEmpty())
    {
        info.metaOk = false;
        info.invalidReason = QStringLiteral("缺少 pack.mcmeta，游戏无法识别这个数据包");
        return;
    }

    // 去掉 UTF-8 BOM，避免 QJsonDocument 解析失败
    if (metaBytes.startsWith("\xEF\xBB\xBF"))
        metaBytes.remove(0, 3);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(metaBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        info.metaOk = false;
        info.invalidReason = QStringLiteral("pack.mcmeta 不是有效的 JSON，游戏将忽略这个数据包");
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonValue packValue = root.value(QStringLiteral("pack"));
    const QJsonObject packObj = packValue.toObject();
    if (!packObj.isEmpty())
    {
        if (packObj.contains(QStringLiteral("description")))
            info.description = jsonTextToString(packObj.value(QStringLiteral("description")));
    }
    else if (root.contains(QStringLiteral("description")))
    {
        // 极老的数据包直接把描述写在根上
        info.description = jsonTextToString(root.value(QStringLiteral("description")));
    }

    if (info.description.trimmed().isEmpty())
        info.description.clear();

    // 1) 传统 pack_format
    if (packObj.contains(QStringLiteral("pack_format")))
    {
        int major = -1;
        int minor = 0;
        if (parseFormatValue(packObj.value(QStringLiteral("pack_format")), major, minor))
        {
            info.hasPackFormat = true;
            info.packFormat = major;
            info.packFormatMinor = minor;
        }
    }

    // 2) supported_formats（1.20.2+）：{min_inclusive,max_inclusive} 或 [min,max]
    if (packObj.contains(QStringLiteral("supported_formats")))
    {
        const QJsonValue sf = packObj.value(QStringLiteral("supported_formats"));
        int minMajor = -1, minMinor = 0, maxMajor = -1, maxMinor = 0;
        bool hasMin = false;
        bool hasMax = false;
        if (sf.isObject())
        {
            const QJsonObject obj = sf.toObject();
            if (obj.contains(QStringLiteral("min_inclusive")))
                hasMin = parseFormatValue(obj.value(QStringLiteral("min_inclusive")), minMajor, minMinor);
            if (obj.contains(QStringLiteral("max_inclusive")))
                hasMax = parseFormatValue(obj.value(QStringLiteral("max_inclusive")), maxMajor, maxMinor);
        }
        else if (sf.isArray())
        {
            const QJsonArray arr = sf.toArray();
            if (!arr.isEmpty())
                hasMin = parseFormatValue(arr.at(0), minMajor, minMinor);
            if (arr.size() > 1)
                hasMax = parseFormatValue(arr.at(1), maxMajor, maxMinor);
            else
            {
                hasMax = hasMin;
                maxMajor = minMajor;
                maxMinor = minMinor;
            }
        }
        else if (sf.isDouble() || sf.isString())
        {
            hasMin = parseFormatValue(sf, minMajor, minMinor);
            hasMax = hasMin;
            maxMajor = minMajor;
            maxMinor = minMinor;
        }

        if (hasMin || hasMax)
        {
            info.hasFormatRange = true;
            info.minFormat = hasMin ? minMajor : 0;
            info.minFormatMinor = hasMin ? minMinor : 0;
            info.maxFormat = hasMax ? maxMajor : minMajor;
            info.maxFormatMinor = hasMax ? maxMinor : minMinor;
        }
    }

    // 3) 1.21.9+ 的 min_format / max_format（可单独出现，也可与 supported_formats 并存）
    {
        int minMajor = -1, minMinor = 0, maxMajor = -1, maxMinor = 0;
        const bool hasMin = packObj.contains(QStringLiteral("min_format"))
            && parseFormatValue(packObj.value(QStringLiteral("min_format")), minMajor, minMinor);
        const bool hasMax = packObj.contains(QStringLiteral("max_format"))
            && parseFormatValue(packObj.value(QStringLiteral("max_format")), maxMajor, maxMinor);
        if (hasMin || hasMax)
        {
            info.hasFormatRange = true;
            if (hasMin)
            {
                info.minFormat = minMajor;
                info.minFormatMinor = minMinor;
            }
            else
            {
                info.minFormat = 0;
                info.minFormatMinor = 0;
            }
            if (hasMax)
            {
                info.maxFormat = maxMajor;
                info.maxFormatMinor = maxMinor;
            }
            else
            {
                info.maxFormat = hasMin ? minMajor : info.maxFormat;
                info.maxFormatMinor = hasMin ? minMinor : info.maxFormatMinor;
            }
            if (!info.hasPackFormat && hasMin)
            {
                info.hasPackFormat = true;
                info.packFormat = minMajor;
                info.packFormatMinor = minMinor;
            }
        }
    }

    info.metaOk = true;
}

/** 从数据文件路径里提取「分类键 / 命名空间 / 名称」 */
void parseDataFilePath(const QString &rawPath, QString &typeKey, QString &displayName,
                       QString &normalizedPath)
{
    QString path = rawPath;
    path.replace('\\', '/');

    // 兼容 overlays/<目录>/data/... 的覆盖包结构：先剥掉 overlays/<目录>/
    if (path.startsWith(QStringLiteral("overlays/"), Qt::CaseInsensitive))
    {
        const int dataPos = path.indexOf(QStringLiteral("/data/"), Qt::CaseInsensitive);
        if (dataPos > 0)
            path = path.mid(dataPos + 1);
        else
            return;
    }

    const QString dataPrefix = QStringLiteral("data/");
    if (!path.startsWith(dataPrefix, Qt::CaseInsensitive))
        return;

    const QString rest = path.mid(dataPrefix.size());
    const QStringList parts = rest.split('/', Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return;

    const QString ns = parts.at(0);
    typeKey = parts.at(1).toLower();
    const QString legacyNew = k_legacyTypeNames.value(typeKey);
    if (!legacyNew.isEmpty())
        typeKey = legacyNew;

    QString name = parts.mid(2).join('/');
    // 去掉常见的数据文件扩展名，展示更干净
    const QStringList suffixes = {".json", ".mcfunction", ".nbt", ".txt", ".yml", ".yaml"};
    for (const QString &suffix : suffixes)
    {
        if (name.endsWith(suffix, Qt::CaseInsensitive))
        {
            name.chop(suffix.size());
            break;
        }
    }

    // 标签的真实类型在 tags/<注册名>/... 里，展示成 "命名空间:注册名/路径"
    if (typeKey == QLatin1String("tags") && parts.size() >= 4)
    {
        const QString registry = parts.at(2);
        QString tagName = parts.mid(3).join('/');
        for (const QString &suffix : suffixes)
        {
            if (tagName.endsWith(suffix, Qt::CaseInsensitive))
            {
                tagName.chop(suffix.size());
                break;
            }
        }
        displayName = ns + QLatin1Char(':') + registry + QLatin1Char('/') + tagName;
        normalizedPath = QStringLiteral("data/") + ns + QStringLiteral("/tags/")
            + registry + QLatin1Char('/') + tagName;
        return;
    }

    displayName = ns + QLatin1Char(':') + name;
    normalizedPath = QStringLiteral("data/") + ns + QLatin1Char('/') + typeKey + QLatin1Char('/') + name;
}

/** 检查数据包是否包含实验性目录 */
bool checkExperimental(const QString &typeKey)
{
    if (k_experimentalKeys.contains(typeKey))
        return true;
    // *_variant 全部属于动态注册表（实验性）
    return typeKey.endsWith(QStringLiteral("_variant"));
}

/** 生成兼容性结论与说明 */
void evaluateCompatibility(DataPackInfo &info, const QString &worldVersion,
                           int expectedMajor, int expectedMinor)
{
    if (!info.metaOk)
    {
        info.compat = DataPackCompat::Invalid;
        info.compatDetail = info.invalidReason;
        return;
    }

    if (expectedMajor < 0)
    {
        info.compat = DataPackCompat::Unknown;
        if (worldVersion.isEmpty())
            info.compatDetail = QStringLiteral("没有找到世界版本信息，无法判断这个数据包是否兼容。");
        else
            info.compatDetail = QStringLiteral("无法识别当前世界的游戏版本「%1」，无法判断数据包兼容性。")
                                    .arg(worldVersion);
        return;
    }

    const QString expectedFormat = DataPackManager::formatDisplayName(expectedMajor, expectedMinor);
    const QString currentText = QStringLiteral("当前世界 %1（数据包格式 %2）")
                                    .arg(worldVersion.isEmpty() ? QStringLiteral("版本未知") : worldVersion,
                                         expectedFormat);

    if (info.hasFormatRange)
    {
        const bool belowMin = formatLess(expectedMajor, expectedMinor,
                                         info.minFormat, info.minFormatMinor);
        const bool aboveMax = formatLess(info.maxFormat, info.maxFormatMinor,
                                         expectedMajor, expectedMinor);
        const QString minText = DataPackManager::formatDisplayName(info.minFormat, info.minFormatMinor);
        const QString maxText = DataPackManager::formatDisplayName(info.maxFormat, info.maxFormatMinor);
        const QString declared = (minText == maxText)
            ? QStringLiteral("数据包格式 %1").arg(minText)
            : QStringLiteral("数据包格式 %1–%2").arg(minText, maxText);

        if (!belowMin && !aboveMax)
        {
            info.compat = DataPackCompat::Compatible;
            info.compatDetail = QStringLiteral("%1，%2，可以正常加载。").arg(declared, currentText);
        }
        else
        {
            info.compat = DataPackCompat::VersionMismatch;
            // 期望格式低于数据包声明的最低格式 → 数据包比游戏新；反之比游戏旧
            const QString direction = belowMin ? QStringLiteral("数据包偏新") : QStringLiteral("数据包偏旧");
            info.compatDetail = QStringLiteral("%1，%2，%3，可能加载失败或部分内容失效。")
                                    .arg(declared, currentText, direction);
        }
        return;
    }

    if (!info.hasPackFormat)
    {
        info.compat = DataPackCompat::Unknown;
        info.compatDetail = QStringLiteral("数据包没有声明 pack_format，无法判断兼容性。%1。")
                                .arg(currentText);
        return;
    }

    const QString packFormatText = DataPackManager::formatDisplayName(info.packFormat, info.packFormatMinor);
    QString packRange = DataPackManager::versionRangeForFormat(info.packFormat, info.packFormatMinor);
    const QString declared = packRange.isEmpty()
        ? QStringLiteral("数据包格式 %1").arg(packFormatText)
        : QStringLiteral("数据包格式 %1（%2）").arg(packFormatText, packRange);

    if (formatEqual(info.packFormat, info.packFormatMinor, expectedMajor, expectedMinor))
    {
        info.compat = DataPackCompat::Compatible;
        info.compatDetail = QStringLiteral("%1，%2，格式匹配。").arg(declared, currentText);
        return;
    }

    info.compat = DataPackCompat::VersionMismatch;
    const bool older = formatLess(info.packFormat, info.packFormatMinor, expectedMajor, expectedMinor);
    info.compatDetail = QStringLiteral("%1，%2，%3，可能加载失败或部分内容失效。")
                            .arg(declared, currentText,
                                 older ? QStringLiteral("数据包偏旧") : QStringLiteral("数据包偏新"));
}

/** 读取单个数据包的完整信息（元数据 + 内容清单） */
void fillPackInfo(DataPackInfo &info, const QStringList &files, QSet<QString> &dataPaths)
{
    QSet<QString> seenPaths;

    for (const QString &entry : files)
    {
        QString typeKey;
        QString displayName;
        QString normalized;
        parseDataFilePath(entry, typeKey, displayName, normalized);
        if (typeKey.isEmpty())
            continue; // pack.mcmeta / pack.png / README 等

        ++info.contentFileCount;
        if (checkExperimental(typeKey))
            info.hasExperimental = true;

        const QString category = DataPackManager::categoryDisplayName(typeKey);
        QStringList &items = info.contents[category];
        if (items.size() < DataPackManager::kMaxPreviewPerCategory)
            items.append(displayName);
        seenPaths.insert(normalized);
    }

    dataPaths = seenPaths;

    // 分类内按名称排序，方便查找
    for (auto it = info.contents.begin(); it != info.contents.end(); ++it)
        std::sort(it.value().begin(), it.value().end());
}

} // namespace

// ============================================================================
// 对外接口
// ============================================================================

DataPackWorld DataPackManager::scanWorld(const QString &worldDir,
                                         const QString &versionHint)
{
    DataPackWorld result;
    result.worldDir = QDir::cleanPath(worldDir);
    if (result.worldDir.isEmpty())
    {
        result.error = QStringLiteral("世界路径为空");
        return result;
    }

    if (!QDir(result.worldDir).exists())
    {
        result.error = QStringLiteral("找不到世界文件夹：%1").arg(result.worldDir);
        return result;
    }

    // 世界版本：优先 level.dat 记录的版本，缺失时用实例版本兜底
    QString worldVersion;
    if (const auto save = LevelDatEditor::readSave(result.worldDir))
        worldVersion = save->versionName.trimmed();
    if (worldVersion.isEmpty())
        worldVersion = versionHint.trimmed();
    result.worldVersion = worldVersion;

    DataPackLists lists;
    if (const auto read = LevelDatEditor::readDataPacks(result.worldDir))
    {
        lists = *read;
        result.ok = true;
    }
    else
    {
        result.ok = false;
        result.error = QStringLiteral("无法读取 level.dat 中的数据包列表，可能文件损坏或正被游戏占用");
    }
    result.rawEnabled = lists.enabled;
    result.rawDisabled = lists.disabled;

    // 建立 level.dat 条目 → 位置 的索引，用于还原启用状态与顺序
    auto buildIndex = [](const QStringList &entries) {
        QHash<QString, int> index;
        for (int i = 0; i < entries.size(); ++i)
        {
            const QString key = stripFilePrefix(entries.at(i)).trimmed().toLower();
            if (!index.contains(key))
                index.insert(key, i);
        }
        return index;
    };
    const QHash<QString, int> enabledIndex = buildIndex(lists.enabled);
    const QHash<QString, int> disabledIndex = buildIndex(lists.disabled);

    const QString dataPackDir = result.worldDir + QStringLiteral("/datapacks");
    const QFileInfo dirInfo(dataPackDir);
    QStringList fileNames;
    if (dirInfo.exists() && dirInfo.isDir())
    {
        const QFileInfoList entries = QDir(dataPackDir).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries)
        {
            const QString name = entry.fileName();
            if (entry.isFile())
            {
                const QString suffix = entry.suffix().toLower();
                if (suffix != QLatin1String("zip") && suffix != QLatin1String("jar"))
                    continue;
            }
            fileNames.append(name);
        }
    }

    QSet<QString> knownFileNames;
    for (const QString &name : fileNames)
        knownFileNames.insert(name.toLower());

    // 已启用的文件包按 Enabled 顺序，未登记（游戏会自动启用）的排在后面
    QList<DataPackInfo> enabledPacks;
    QList<DataPackInfo> disabledPacks;

    for (const QString &name : fileNames)
    {
        DataPackInfo info;
        info.isFile = true;
        info.fileName = name;
        info.id = fileEntryId(name);
        info.filePath = dataPackDir + QLatin1Char('/') + name;
        const QFileInfo fileInfo(info.filePath);
        info.exists = fileInfo.exists();
        info.fileSize = fileInfo.isFile() ? fileInfo.size() : 0;
        info.lastModified = fileInfo.lastModified();

        const QString key = name.toLower();
        if (enabledIndex.contains(key))
        {
            info.enabled = true;
            enabledPacks.append(info);
        }
        else if (disabledIndex.contains(key))
        {
            info.enabled = false;
            disabledPacks.append(info);
        }
        else
        {
            // 文件在目录里但 level.dat 没有记录：游戏加载世界时会自动启用
            info.enabled = true;
            enabledPacks.append(info);
        }
    }

    auto sortByListOrder = [](QList<DataPackInfo> &packs, const QHash<QString, int> &index) {
        std::sort(packs.begin(), packs.end(), [&index](const DataPackInfo &a, const DataPackInfo &b) {
            const QString keyA = a.fileName.toLower();
            const QString keyB = b.fileName.toLower();
            const int posA = index.value(keyA, INT_MAX);
            const int posB = index.value(keyB, INT_MAX);
            if (posA != posB)
                return posA < posB;
            return QString::localeAwareCompare(a.fileName, b.fileName) < 0;
        });
    };
    sortByListOrder(enabledPacks, enabledIndex);
    sortByListOrder(disabledPacks, disabledIndex);

    // level.dat 里残留但文件已不存在的 file/ 条目：列为「文件缺失」，允许清理
    QSet<QString> knownLower = knownFileNames;
    auto appendMissing = [&](const QStringList &entries, bool enabled, QList<DataPackInfo> &target) {
        for (const QString &entry : entries)
        {
            if (!looksLikeFileEntry(entry))
                continue;
            const QString name = stripFilePrefix(entry);
            if (knownLower.contains(name.toLower()))
                continue;
            if (target.size() + enabledPacks.size() + disabledPacks.size() >= 500)
                break;
            DataPackInfo info;
            info.isFile = true;
            info.exists = false;
            info.enabled = enabled;
            info.id = entry;
            info.fileName = name;
            info.invalidReason = QStringLiteral("文件已不存在，可以删除这条记录");
            info.compat = DataPackCompat::Invalid;
            info.compatDetail = info.invalidReason;
            target.append(info);
            knownLower.insert(name.toLower());
        }
    };
    appendMissing(lists.enabled, true, enabledPacks);
    appendMissing(lists.disabled, false, disabledPacks);

    // 内置/模组提供的数据包：不参与文件管理，也不在列表里展示
    QSet<QString> builtinKeys;
    for (const QString &entry : lists.enabled + lists.disabled)
    {
        if (entry.trimmed().isEmpty() || looksLikeFileEntry(entry))
            continue;
        if (knownFileNames.contains(stripFilePrefix(entry).toLower()))
            continue;
        builtinKeys.insert(entry);
    }
    result.builtinCount = builtinKeys.size();

    // 解析每个文件包
    const QString expectedFormat = packFormatForVersion(worldVersion);
    int expectedMajor = -1;
    int expectedMinor = 0;
    if (!expectedFormat.isEmpty())
    {
        const QStringList parts = expectedFormat.split('.');
        expectedMajor = parts.value(0).toInt();
        expectedMinor = parts.value(1).toInt();
    }

    QHash<QString, QSet<QString>> packPaths; // id → 规范化数据文件路径
    for (DataPackInfo &info : enabledPacks)
    {
        if (!info.exists)
            continue;
        const QStringList files = listPackFiles(info);
        readPackMeta(info, files);
        QSet<QString> paths;
        fillPackInfo(info, files, paths);
        packPaths.insert(info.id, paths);
        evaluateCompatibility(info, worldVersion, expectedMajor, expectedMinor);
    }
    for (DataPackInfo &info : disabledPacks)
    {
        if (!info.exists)
            continue;
        const QStringList files = listPackFiles(info);
        readPackMeta(info, files);
        QSet<QString> paths;
        fillPackInfo(info, files, paths);
        evaluateCompatibility(info, worldVersion, expectedMajor, expectedMinor);
    }

    // 冲突检测：已启用的数据包之间，同一个数据文件出现多次
    {
        QHash<QString, QList<DataPackInfo *>> pathOwners;
        for (DataPackInfo &info : enabledPacks)
        {
            if (!info.exists || !info.metaOk)
                continue;
            const QSet<QString> &paths = packPaths.value(info.id);
            for (const QString &path : paths)
                pathOwners[path].append(&info);
        }
        QHash<DataPackInfo *, QSet<QString>> conflicts;
        for (auto it = pathOwners.constBegin(); it != pathOwners.constEnd(); ++it)
        {
            if (it.value().size() < 2)
                continue;
            for (DataPackInfo *owner : it.value())
            {
                QSet<QString> &set = conflicts[owner];
                if (set.size() < 200)
                    set.insert(it.key());
            }
        }
        for (DataPackInfo &info : enabledPacks)
        {
            const QSet<QString> set = conflicts.value(&info);
            info.conflictPaths = QStringList(set.begin(), set.end());
            std::sort(info.conflictPaths.begin(), info.conflictPaths.end());
        }
    }

    result.packs = enabledPacks + disabledPacks;
    return result;
}

bool DataPackManager::applyStates(const QString &worldDir, const QList<DataPackInfo> &packs,
                                  QString *errorOut)
{
    auto fail = [errorOut](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    const QString cleanWorld = QDir::cleanPath(worldDir);
    if (cleanWorld.isEmpty() || !QDir(cleanWorld).exists())
        return fail(QStringLiteral("世界文件夹不存在"));

    const auto current = LevelDatEditor::readDataPacks(cleanWorld);
    if (!current.has_value())
        return fail(QStringLiteral("无法读取 level.dat 中的数据包列表"));

    // 文件型数据包的识别：file/ 前缀，或文件名与界面上的某个包一致
    QSet<QString> fileKeys;
    for (const DataPackInfo &info : packs)
    {
        if (!info.isFile || info.fileName.trimmed().isEmpty())
            continue;
        fileKeys.insert(info.fileName.trimmed().toLower());
    }

    auto isManagedFileEntry = [&fileKeys](const QString &entry) {
        if (looksLikeFileEntry(entry))
            return true;
        // 极少数旧数据可能只写文件名：只把明确的压缩包名当作文件条目，
        // 避免误删 "vanilla" / "fabric" 这类不带 file/ 前缀的内置条目
        const QString name = entry.trimmed().toLower();
        return (name.endsWith(QLatin1String(".zip")) || name.endsWith(QLatin1String(".jar")))
            && fileKeys.contains(name);
    };

    QStringList keptEnabled;
    QStringList keptDisabled;
    QSet<QString> seenKeys;
    for (const QString &entry : current->enabled)
    {
        const QString key = entry.trimmed().toLower();
        if (entry.trimmed().isEmpty() || isManagedFileEntry(entry) || seenKeys.contains(key))
            continue;
        seenKeys.insert(key);
        keptEnabled.append(entry);
    }
    seenKeys.clear();
    for (const QString &entry : current->disabled)
    {
        const QString key = entry.trimmed().toLower();
        if (entry.trimmed().isEmpty() || isManagedFileEntry(entry) || seenKeys.contains(key))
            continue;
        seenKeys.insert(key);
        keptDisabled.append(entry);
    }

    // 新世界可能还没有 vanilla 条目，显式补上，避免游戏把它当成“未启用”
    if (!current->hasDataPacks && keptEnabled.isEmpty())
        keptEnabled.append(QStringLiteral("vanilla"));

    QStringList enabledIds;
    QStringList disabledIds;
    QSet<QString> addedKeys;
    for (const DataPackInfo &info : packs)
    {
        if (!info.isFile || info.fileName.trimmed().isEmpty())
            continue;
        const QString id = info.id.isEmpty() ? fileEntryId(info.fileName) : info.id;
        const QString key = stripFilePrefix(id).trimmed().toLower();
        if (addedKeys.contains(key))
            continue;
        addedKeys.insert(key);
        if (info.enabled)
            enabledIds.append(id);
        else
            disabledIds.append(id);
    }

    QStringList newEnabled = keptEnabled + enabledIds;
    QStringList newDisabled = keptDisabled + disabledIds;

    QString error;
    if (!LevelDatEditor::writeDataPacks(cleanWorld, newEnabled, newDisabled, &error))
        return fail(error.isEmpty() ? QStringLiteral("写入 level.dat 失败") : error);
    return true;
}

bool DataPackManager::importPack(const QString &worldDir, const QString &sourcePath,
                                 QString *destNameOut, QString *errorOut)
{
    auto fail = [errorOut](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    const QString cleanWorld = QDir::cleanPath(worldDir);
    if (cleanWorld.isEmpty() || !QDir(cleanWorld).exists())
        return fail(QStringLiteral("世界文件夹不存在"));

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
        return fail(QStringLiteral("找不到要导入的数据包文件"));

    const QString suffix = sourceInfo.suffix().toLower();
    if (suffix != QLatin1String("zip") && suffix != QLatin1String("jar"))
        return fail(QStringLiteral("只支持导入 .zip 或 .jar 数据包"));

    const QString targetDir = cleanWorld + QStringLiteral("/datapacks");
    if (!QDir().mkpath(targetDir))
        return fail(QStringLiteral("无法创建 datapacks 文件夹"));

    const QString baseName = Safety::safeFileName(sourceInfo.fileName(), QStringLiteral("datapack.zip"));
    QString destName = baseName;
    int counter = 2;
    while (QFile::exists(targetDir + QLatin1Char('/') + destName))
    {
        const QString stem = baseName.left(baseName.size() - suffix.size() - 1);
        const QString candidate = QStringLiteral("%1 (%2).%3").arg(stem).arg(counter).arg(suffix);
        destName = candidate;
        ++counter;
        if (counter > 999)
            return fail(QStringLiteral("同名数据包太多，无法生成新文件名"));
    }

    const QString destPath = targetDir + QLatin1Char('/') + destName;
    if (!QFile::copy(sourcePath, destPath))
        return fail(QStringLiteral("复制数据包失败，请检查磁盘空间或文件权限"));

    if (destNameOut)
        *destNameOut = destName;
    return true;
}

bool DataPackManager::removePack(const QString &worldDir, const DataPackInfo &info,
                                 QString *errorOut)
{
    auto fail = [errorOut](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    if (!info.isFile || info.filePath.isEmpty())
        return fail(QStringLiteral("只能删除世界数据包目录里的文件"));

    const QString dataPackDir = QDir::cleanPath(worldDir) + QStringLiteral("/datapacks");
    const QString cleanPath = QDir::cleanPath(info.filePath);
    // 安全兜底：只允许删除 datapacks/ 内的内容
    if (!Safety::isPathInside(dataPackDir, cleanPath) || cleanPath == dataPackDir)
        return fail(QStringLiteral("拒绝删除数据包目录之外的文件"));

    const QFileInfo fileInfo(cleanPath);
    if (!fileInfo.exists())
        return fail(QStringLiteral("文件已经不存在"));

    if (fileInfo.isDir())
    {
        QDir dir(cleanPath);
        if (!dir.removeRecursively())
            return fail(QStringLiteral("删除数据包文件夹失败，请检查文件是否被占用"));
        return true;
    }

    if (!QFile::remove(cleanPath))
        return fail(QStringLiteral("删除数据包失败，请检查文件是否被占用"));
    return true;
}

QStringList DataPackManager::listInstanceLibrary(const QString &instancePath)
{
    QStringList result;
    if (instancePath.trimmed().isEmpty())
        return result;

    QDir dir(instancePath + QStringLiteral("/datapacks"));
    if (!dir.exists())
        return result;

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo &entry : entries)
    {
        const QString suffix = entry.suffix().toLower();
        if (suffix != QLatin1String("zip") && suffix != QLatin1String("jar"))
            continue;
        if (entry.fileName().endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
            continue;
        result.append(entry.absoluteFilePath());
    }
    return result;
}

QString DataPackManager::packFormatForVersion(const QString &gameVersion)
{
    const QString version = gameVersion.trimmed();
    if (version.isEmpty())
        return QString();

    // 快照（26w14a）、预发布（1.21.9-pre1 / 1.21.9-rc1）一律不猜
    static const QRegularExpression snapshotPattern(QStringLiteral("^\\d{2}w\\d{2}[a-z]$"));
    if (snapshotPattern.match(version.toLower()).hasMatch())
        return QString();
    if (version.contains(QLatin1Char('-'), Qt::CaseInsensitive))
        return QString();
    if (version.contains(QRegularExpression(QStringLiteral("[A-Za-z]"))))
        return QString();

    for (const DataPackFormatEntry &entry : k_dataPackFormats)
    {
        if (LocalCompatibilityChecker::compareVersions(version, QString::fromLatin1(entry.minVersion)) >= 0
            && LocalCompatibilityChecker::compareVersions(version, QString::fromLatin1(entry.maxVersion)) <= 0)
        {
            return formatDisplayName(entry.major, entry.minor);
        }
    }
    return QString();
}

QString DataPackManager::versionRangeForFormat(int major, int minor)
{
    if (major < 0)
        return QString();

    for (const DataPackFormatEntry &entry : k_dataPackFormats)
    {
        if (entry.major != major)
            continue;
        if (entry.minor != minor && minor != 0)
            continue;
        const QString minVersion = QString::fromLatin1(entry.minVersion);
        const QString maxVersion = QString::fromLatin1(entry.maxVersion);
        if (minVersion == maxVersion)
            return minVersion;
        return QStringLiteral("%1–%2").arg(minVersion, maxVersion);
    }
    return QString();
}

QString DataPackManager::formatDisplayName(int major, int minor)
{
    if (major < 0)
        return QString();
    // 1.21.9 起官方使用「主版本.次版本」写法（88.0、94.1），保持一致的展示习惯
    if (minor > 0 || major >= 88)
        return QStringLiteral("%1.%2").arg(major).arg(minor);
    return QString::number(major);
}

QByteArray DataPackManager::readPackIcon(const DataPackInfo &info)
{
    if (!info.exists || info.filePath.isEmpty())
        return QByteArray();

    const QFileInfo fileInfo(info.filePath);
    if (fileInfo.isDir())
    {
        const QByteArray data = readFolderFile(info.filePath, QStringLiteral("pack.png"));
        return data.size() <= 4 * 1024 * 1024 ? data : QByteArray();
    }

    QByteArray data;
    if (JarUtils::extractFromJarToMemory(info.filePath, QStringLiteral("pack.png"), data))
        return data.size() <= 4 * 1024 * 1024 ? data : QByteArray();
    return QByteArray();
}

QStringList DataPackManager::categoryOrder()
{
    return {
        QStringLiteral("函数"),
        QStringLiteral("进度"),
        QStringLiteral("配方"),
        QStringLiteral("战利品表"),
        QStringLiteral("物品修饰器"),
        QStringLiteral("谓词"),
        QStringLiteral("标签"),
        QStringLiteral("结构"),
        QStringLiteral("世界生成"),
        QStringLiteral("维度"),
        QStringLiteral("维度类型"),
        QStringLiteral("附魔"),
        QStringLiteral("附魔提供器"),
        QStringLiteral("伤害类型"),
        QStringLiteral("画作变种"),
        QStringLiteral("唱片"),
        QStringLiteral("盔甲纹饰材料"),
        QStringLiteral("盔甲纹饰图案"),
        QStringLiteral("旗帜图案"),
        QStringLiteral("聊天类型"),
        QStringLiteral("饰纹陶罐图案"),
        QStringLiteral("乐器"),
        QStringLiteral("生物变种"),
        QStringLiteral("对话框"),
        QStringLiteral("时间轴"),
        QStringLiteral("世界时钟"),
        QStringLiteral("游戏测试"),
        QStringLiteral("交易"),
        QStringLiteral("村民交易"),
        QStringLiteral("试炼刷怪笼"),
        QStringLiteral("物品槽来源"),
        QStringLiteral("数值提供器"),
        QStringLiteral("方块转换"),
        QStringLiteral("硫磺立方体原型"),
        QStringLiteral("其他")
    };
}

QString DataPackManager::categoryDisplayName(const QString &typeKey)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("function"), QStringLiteral("函数")},
        {QStringLiteral("advancement"), QStringLiteral("进度")},
        {QStringLiteral("recipe"), QStringLiteral("配方")},
        {QStringLiteral("loot_table"), QStringLiteral("战利品表")},
        {QStringLiteral("predicate"), QStringLiteral("谓词")},
        {QStringLiteral("item_modifier"), QStringLiteral("物品修饰器")},
        {QStringLiteral("tags"), QStringLiteral("标签")},
        {QStringLiteral("structure"), QStringLiteral("结构")},
        {QStringLiteral("worldgen"), QStringLiteral("世界生成")},
        {QStringLiteral("dimension"), QStringLiteral("维度")},
        {QStringLiteral("dimension_type"), QStringLiteral("维度类型")},
        {QStringLiteral("enchantment"), QStringLiteral("附魔")},
        {QStringLiteral("enchantment_provider"), QStringLiteral("附魔提供器")},
        {QStringLiteral("damage_type"), QStringLiteral("伤害类型")},
        {QStringLiteral("painting_variant"), QStringLiteral("画作变种")},
        {QStringLiteral("jukebox_song"), QStringLiteral("唱片")},
        {QStringLiteral("trim_material"), QStringLiteral("盔甲纹饰材料")},
        {QStringLiteral("trim_pattern"), QStringLiteral("盔甲纹饰图案")},
        {QStringLiteral("banner_pattern"), QStringLiteral("旗帜图案")},
        {QStringLiteral("instrument"), QStringLiteral("乐器")},
        {QStringLiteral("dialog"), QStringLiteral("对话框")},
        {QStringLiteral("timeline"), QStringLiteral("时间轴")},
        {QStringLiteral("world_clock"), QStringLiteral("世界时钟")},
        {QStringLiteral("test_environment"), QStringLiteral("游戏测试")},
        {QStringLiteral("test_instance"), QStringLiteral("游戏测试")},
        {QStringLiteral("trade_set"), QStringLiteral("交易")},
        {QStringLiteral("villager_trade"), QStringLiteral("村民交易")},
        {QStringLiteral("trial_spawner"), QStringLiteral("试炼刷怪笼")},
        {QStringLiteral("slot_source"), QStringLiteral("物品槽来源")},
        {QStringLiteral("context_float_provider"), QStringLiteral("数值提供器")},
        {QStringLiteral("context_int_provider"), QStringLiteral("数值提供器")},
        {QStringLiteral("block_transformer"), QStringLiteral("方块转换")},
        {QStringLiteral("sulfur_cube_archetype"), QStringLiteral("硫磺立方体原型")},
        {QStringLiteral("chat_type"), QStringLiteral("聊天类型")},
        {QStringLiteral("decorated_pot_pattern"), QStringLiteral("饰纹陶罐图案")},
        {QStringLiteral("cat_variant"), QStringLiteral("生物变种")},
        {QStringLiteral("chicken_variant"), QStringLiteral("生物变种")},
        {QStringLiteral("cow_variant"), QStringLiteral("生物变种")},
        {QStringLiteral("frog_variant"), QStringLiteral("生物变种")},
        {QStringLiteral("pig_variant"), QStringLiteral("生物变种")},
        {QStringLiteral("wolf_variant"), QStringLiteral("生物变种")},
        {QStringLiteral("wolf_sound_variant"), QStringLiteral("生物变种")},
        {QStringLiteral("zombie_nautilus_variant"), QStringLiteral("生物变种")},
        {QStringLiteral("loot_table_modifier"), QStringLiteral("战利品表")},
    };
    return names.value(typeKey, QStringLiteral("其他"));
}
