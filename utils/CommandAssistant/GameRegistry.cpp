/**
 * @file   GameRegistry.cpp
 * @brief  游戏注册表数据自动提取器实现
 * @author BlockBox Team
 * @date   2026-08-06
 *
 * 实现细节：
 *   1. extractFromJar() 先列出 JAR 内 assets/ 下所有条目，收集每个命名空间的
 *      lang 翻译文件路径（zh_cn.json 优先，en_us.json 兜底）。
 *   2. applyLangObject() 遍历翻译键，按前缀分类：
 *      block./item./entity./effect./enchantment. 五类。
 *   3. 同一条目（英文注册名）在多个 lang 文件中出现时合并中文名（去重）。
 *   4. extractFromGameDir() 依次处理版本 JAR 与 mods/ 目录下全部 JAR，
 *      不区分 Forge / Fabric / NeoForge（资源格式一致）。
 */

#include "utils/CommandAssistant/GameRegistry.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include "utils/JarUtils.h"

namespace {

/**
 * @brief 将一条 lang 翻译键解析为「类别 + 注册名」并合并到对应容器
 *
 * 支持格式（Minecraft 1.13+）：
 *   block.minecraft.stone / item.<ns>.<id> / entity.<ns>.<id>
 *   effect.<ns>.<id> / enchantment.<ns>.<id>
 * 也兼容不带命名空间的旧式键（如 effect.speed / enchantment.sharpness），
 * 此时补全为 minecraft:<id>。
 *
 * @tparam T 目标结构体类型（含 english 与 chinese 成员）
 * @param list  目标容器
 * @param index 英文注册名（小写）→ 容器索引
 * @param fullId 完整注册名（如 "minecraft:stone"）
 * @param displayName 翻译显示名
 */
template<typename T>
void mergeLangEntry(QList<T> &list, QHash<QString, int> &index,
                    const QString &fullId, const QString &displayName)
{
    if (fullId.isEmpty() || displayName.isEmpty())
    {
        return;
    }

    const QString lowerId = fullId.toLower();
    const int idx = index.value(lowerId, -1);
    if (idx >= 0 && idx < list.size())
    {
        // 已存在：合并显示名（去重）
        T &entry = list[idx];
        for (const QString &name : entry.chinese)
        {
            if (name.compare(displayName, Qt::CaseInsensitive) == 0)
            {
                return;
            }
        }
        entry.chinese.append(displayName);
        return;
    }

    T entry;
    entry.english = fullId;
    entry.chinese.append(displayName);
    index.insert(lowerId, list.size());
    list.append(entry);
}

} // namespace

// ============================================================================
// 公开接口实现
// ============================================================================

int GameRegistry::extractFromJar(const QString &jarPath)
{
    const QStringList entries = JarUtils::listEntriesInJar(jarPath, QStringLiteral("assets/"));
    if (entries.isEmpty())
    {
        return 0;
    }

    // 1. 收集 lang 文件：namespace -> 条目路径（zh_cn 优先，en_us 兜底）
    QMap<QString, QString> zhLangPaths;
    QMap<QString, QString> enLangPaths;
    for (const QString &entry : entries)
    {
        const QStringList parts = entry.split(QLatin1Char('/'));
        if (parts.size() < 4 || parts[0] != QStringLiteral("assets")
            || parts[2] != QStringLiteral("lang"))
        {
            continue;
        }
        const QString &ns = parts[1];
        const QString &file = parts.last();
        if (file == QStringLiteral("zh_cn.json"))
        {
            zhLangPaths.insert(ns, entry);
        }
        else if (file == QStringLiteral("en_us.json"))
        {
            enLangPaths.insert(ns, entry);
        }
    }

    // 2. 解析语言文件并合并（先中文后英文，英文名作为无中文翻译时的兜底别名）
    int added = 0;
    auto applyLang = [&](const QMap<QString, QString> &langs) {
        for (auto it = langs.constBegin(); it != langs.constEnd(); ++it)
        {
            QByteArray data;
            if (!JarUtils::extractFromJarToMemory(jarPath, it.value(), data))
            {
                continue;
            }
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            {
                continue;
            }
            applyLangObject(doc.object(), added);
        }
    };
    applyLang(zhLangPaths);
    applyLang(enLangPaths);

    return added;
}

int GameRegistry::extractFromGameDir(const QString &gameDir, const QString &version)
{
    int totalCount = 0;

    // 1. 版本 JAR：{gameDir}/versions/{version}/{version}.jar
    const QString versionJar = gameDir
                             + QStringLiteral("/versions/")
                             + version
                             + QStringLiteral("/")
                             + version
                             + QStringLiteral(".jar");
    if (QFile::exists(versionJar))
    {
        totalCount += extractFromJar(versionJar);
    }

    // 2. mods 目录下的所有 JAR（Forge / Fabric / NeoForge 资源结构一致）
    const QString modsPath = gameDir + QStringLiteral("/mods");
    QDir modsDir(modsPath);
    if (modsDir.exists())
    {
        const QStringList modJars = modsDir.entryList({QStringLiteral("*.jar")}, QDir::Files);
        for (const QString &modJar : modJars)
        {
            totalCount += extractFromJar(modsDir.absoluteFilePath(modJar));
        }
    }

    return totalCount;
}

void GameRegistry::clear()
{
    m_blocks.clear();
    m_items.clear();
    m_entities.clear();
    m_effects.clear();
    m_enchantments.clear();
    m_blockIndex.clear();
    m_itemIndex.clear();
    m_entityIndex.clear();
    m_effectIndex.clear();
    m_enchantIndex.clear();
}

// ============================================================================
// 私有实现
// ============================================================================

void GameRegistry::applyLangObject(const QJsonObject &obj, int &added)
{
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
    {
        const QString key = it.key();
        const QString value = it.value().toString();
        if (value.isEmpty() || !key.contains(QLatin1Char('.')))
        {
            continue;
        }

        // 解析 key = "<category>.<ns>.<id>"（category 与 ns 之间固定两个点）
        const int firstDot = key.indexOf(QLatin1Char('.'));
        const int secondDot = key.indexOf(QLatin1Char('.'), firstDot + 1);
        if (firstDot <= 0 || secondDot <= firstDot + 1)
        {
            continue;
        }

        const QString category = key.left(firstDot);
        const QString ns = key.mid(firstDot + 1, secondDot - firstDot - 1);
        const QString id = key.mid(secondDot + 1);
        if (id.isEmpty())
        {
            continue;
        }

        // 旧版 lang 键可能不带命名空间（如 effect.speed），补全为 minecraft
        const QString effectiveNs = ns.isEmpty() ? QStringLiteral("minecraft") : ns;
        const QString fullId = effectiveNs + QLatin1Char(':') + id;

        if (category == QStringLiteral("block"))
        {
            mergeLangEntry(m_blocks, m_blockIndex, fullId, value);
            ++added;
        }
        else if (category == QStringLiteral("item"))
        {
            mergeLangEntry(m_items, m_itemIndex, fullId, value);
            ++added;
        }
        else if (category == QStringLiteral("entity"))
        {
            mergeLangEntry(m_entities, m_entityIndex, fullId, value);
            ++added;
        }
        else if (category == QStringLiteral("effect"))
        {
            mergeLangEntry(m_effects, m_effectIndex, fullId, value);
            ++added;
        }
        else if (category == QStringLiteral("enchantment"))
        {
            mergeLangEntry(m_enchantments, m_enchantIndex, fullId, value);
            ++added;
        }
        // 其他类别（potion./advancements. 等）忽略
    }
}
