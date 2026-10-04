/**
 * @file   CommandDatabase.cpp
 * @brief  内置 JSON 指令数据库加载与查询实现
 * @author BlockBox Team
 * @date   2026-07-04
 *
 * 实现细节：
 *   1. 使用 QFile 打开 Qt 资源 :/resources/command_database.json
 *   2. 用 QJsonDocument::fromJson 解析，根对象包含 commands / items / entities / effects 四个数组
 *   3. 逐项填充成员结构体；params 中 default 字段可选，candidates 仅在 type=enum 时存在
 *   4. 查询接口统一使用大小写不敏感匹配，前缀用 startsWith，精确用 compare
 */

#include "CommandDatabase.h"

#include "utils/CommandAssistant/CommandPack.h" // mergePack 实现需要完整 CommandPack 定义

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

/**
 * @brief 解析单个 ParamInfo
 * @param obj JSON 对象
 * @return 解析得到的 ParamInfo
 */
ParamInfo parseParam(const QJsonObject &obj)
{
    ParamInfo param;
    param.name = obj.value(QStringLiteral("name")).toString();
    param.type = obj.value(QStringLiteral("type")).toString();
    if (obj.contains(QStringLiteral("default")))
    {
        const QJsonValue defaultValue = obj.value(QStringLiteral("default"));
        if (defaultValue.isString())
        {
            param.defaultValue = defaultValue.toString();
        }
        else
        {
            // 非字符串类型（int/float/bool）转字符串保留
            param.defaultValue = QJsonValue::fromVariant(defaultValue.toVariant()).toString();
        }
    }
    param.required = obj.value(QStringLiteral("required")).toBool(true);
    if (param.type == QStringLiteral("enum"))
    {
        const QJsonArray candidatesArray = obj.value(QStringLiteral("candidates")).toArray();
        for (const QJsonValue &candidate : candidatesArray)
        {
            param.candidates.append(candidate.toString());
        }
    }
    return param;
}

/**
 * @brief 解析单个 CommandInfo
 * @param obj JSON 对象
 * @return 解析得到的 CommandInfo
 */
CommandInfo parseCommand(const QJsonObject &obj)
{
    CommandInfo info;
    info.name = obj.value(QStringLiteral("name")).toString();
    info.minVersion = obj.value(QStringLiteral("minVersion")).toString();
    info.opLevel = obj.value(QStringLiteral("opLevel")).toInt(0);
    info.serverOnly = obj.value(QStringLiteral("serverOnly")).toBool(false);
    info.syntax = obj.value(QStringLiteral("syntax")).toString();
    info.description = obj.value(QStringLiteral("description")).toString();

    const QJsonArray chineseArray = obj.value(QStringLiteral("chinese")).toArray();
    for (const QJsonValue &alias : chineseArray)
    {
        info.chinese.append(alias.toString());
    }

    const QJsonArray paramsArray = obj.value(QStringLiteral("params")).toArray();
    for (const QJsonValue &paramValue : paramsArray)
    {
        info.params.append(parseParam(paramValue.toObject()));
    }

    return info;
}

/**
 * @brief 解析单个句子预设
 * @param obj JSON 对象
 * @return 解析得到的 CommandPreset
 */
CommandPreset parsePreset(const QJsonObject &obj)
{
    CommandPreset preset;
    preset.id = obj.value(QStringLiteral("id")).toString();
    preset.category = obj.value(QStringLiteral("category")).toString();
    preset.chinese = obj.value(QStringLiteral("chinese")).toString();
    preset.english = obj.value(QStringLiteral("english")).toString();
    preset.description = obj.value(QStringLiteral("description")).toString();
    return preset;
}

/**
 * @brief 通用：从 JSON 数组解析中英文映射条目
 * @tparam T  目标结构体类型（ItemInfo / EntityInfo / EffectInfo）
 * @param array JSON 数组
 * @return 解析得到的列表
 */
template<typename T>
QList<T> parseTranslationEntries(const QJsonArray &array)
{
    QList<T> result;
    for (const QJsonValue &value : array)
    {
        const QJsonObject obj = value.toObject();
        T entry;
        entry.english = obj.value(QStringLiteral("english")).toString();
        const QJsonArray chineseArray = obj.value(QStringLiteral("chinese")).toArray();
        for (const QJsonValue &alias : chineseArray)
        {
            entry.chinese.append(alias.toString());
        }
        result.append(entry);
    }
    return result;
}

} // namespace

// ============================================================================
// 加载
// ============================================================================

bool CommandDatabase::load()
{
    QFile file(QStringLiteral(":/resources/command_database.json"));
    if (!file.exists())
    {
        qWarning() << "CommandDatabase: resource file not found:"
                   << ":/resources/command_database.json";
        return false;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "CommandDatabase: failed to open resource file:" << file.errorString();
        return false;
    }

    const QByteArray rawData = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(rawData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        qWarning() << "CommandDatabase: JSON parse failed:" << parseError.errorString();
        m_loaded = false;
        return false;
    }

    const QJsonObject root = doc.object();

    // commands
    m_commands.clear();
    const QJsonArray commandsArray = root.value(QStringLiteral("commands")).toArray();
    for (const QJsonValue &value : commandsArray)
    {
        m_commands.append(parseCommand(value.toObject()));
    }

    // items / entities / effects / enchantments
    m_items = parseTranslationEntries<ItemInfo>(root.value(QStringLiteral("items")).toArray());
    m_entities = parseTranslationEntries<EntityInfo>(root.value(QStringLiteral("entities")).toArray());
    m_effects = parseTranslationEntries<EffectInfo>(root.value(QStringLiteral("effects")).toArray());
    m_enchantments = parseTranslationEntries<EnchantmentInfo>(root.value(QStringLiteral("enchantments")).toArray());

    // presets（可选，兼容旧版数据库文件）
    m_presets.clear();
    const QJsonArray presetsArray = root.value(QStringLiteral("presets")).toArray();
    for (const QJsonValue &value : presetsArray)
    {
        m_presets.append(parsePreset(value.toObject()));
    }

    m_loaded = true;
    rebuildIndexes();
    return true;
}

bool CommandDatabase::loadFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists())
    {
        qWarning() << "CommandDatabase: file not found:" << filePath;
        return false;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "CommandDatabase: failed to open file:" << file.errorString();
        return false;
    }

    const QByteArray rawData = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(rawData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        qWarning() << "CommandDatabase: JSON parse failed:" << parseError.errorString();
        m_loaded = false;
        return false;
    }

    const QJsonObject root = doc.object();

    // commands
    m_commands.clear();
    const QJsonArray commandsArray = root.value(QStringLiteral("commands")).toArray();
    for (const QJsonValue &value : commandsArray)
    {
        m_commands.append(parseCommand(value.toObject()));
    }

    // items / entities / effects / enchantments
    m_items = parseTranslationEntries<ItemInfo>(root.value(QStringLiteral("items")).toArray());
    m_entities = parseTranslationEntries<EntityInfo>(root.value(QStringLiteral("entities")).toArray());
    m_effects = parseTranslationEntries<EffectInfo>(root.value(QStringLiteral("effects")).toArray());
    m_enchantments = parseTranslationEntries<EnchantmentInfo>(root.value(QStringLiteral("enchantments")).toArray());

    // presets
    m_presets.clear();
    const QJsonArray presetsArray = root.value(QStringLiteral("presets")).toArray();
    for (const QJsonValue &value : presetsArray)
    {
        m_presets.append(parsePreset(value.toObject()));
    }

    m_loaded = true;
    rebuildIndexes();
    return true;
}

bool CommandDatabase::isLoaded() const
{
    return m_loaded;
}

void CommandDatabase::clearAll()
{
    m_commands.clear();
    m_items.clear();
    m_entities.clear();
    m_effects.clear();
    m_enchantments.clear();
    m_presets.clear();
    m_loaded = false;
    rebuildIndexes();
}

bool CommandDatabase::mergePack(const CommandPack &pack)
{
    // 合并指令：按英文名匹配，已存在则覆盖；不存在则追加
    for (const CommandInfo &cmd : pack.commands())
    {
        bool replaced = false;
        for (int i = 0; i < m_commands.size(); ++i)
        {
            if (m_commands[i].name.compare(cmd.name, Qt::CaseInsensitive) == 0)
            {
                m_commands[i] = cmd;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            m_commands.append(cmd);
        }
    }

    // 合并物品：按英文名匹配覆盖
    for (const ItemInfo &entry : pack.items())
    {
        bool replaced = false;
        for (int i = 0; i < m_items.size(); ++i)
        {
            if (m_items[i].english.compare(entry.english, Qt::CaseInsensitive) == 0)
            {
                m_items[i] = entry;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            m_items.append(entry);
        }
    }

    // 合并实体：按英文名匹配覆盖
    for (const EntityInfo &entry : pack.entities())
    {
        bool replaced = false;
        for (int i = 0; i < m_entities.size(); ++i)
        {
            if (m_entities[i].english.compare(entry.english, Qt::CaseInsensitive) == 0)
            {
                m_entities[i] = entry;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            m_entities.append(entry);
        }
    }

    // 合并效果：按英文名匹配覆盖
    for (const EffectInfo &entry : pack.effects())
    {
        bool replaced = false;
        for (int i = 0; i < m_effects.size(); ++i)
        {
            if (m_effects[i].english.compare(entry.english, Qt::CaseInsensitive) == 0)
            {
                m_effects[i] = entry;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            m_effects.append(entry);
        }
    }

    return !pack.commands().isEmpty() || !pack.items().isEmpty()
           || !pack.entities().isEmpty() || !pack.effects().isEmpty();
}

void CommandDatabase::mergeDynamicData(const QList<ItemInfo> &items,
                                       const QList<EntityInfo> &entities,
                                       const QList<EffectInfo> &effects,
                                       const QList<EnchantmentInfo> &enchantments)
{
    // 按英文名（大小写不敏感）覆盖合并：模组/游戏运行时数据优先于内置静态数据
    auto mergeList = [](auto &dst, const auto &src) {
        for (const auto &entry : src)
        {
            bool replaced = false;
            for (int i = 0; i < dst.size(); ++i)
            {
                if (dst[i].english.compare(entry.english, Qt::CaseInsensitive) == 0)
                {
                    dst[i] = entry;
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
            {
                dst.append(entry);
            }
        }
    };

    mergeList(m_items, items);
    mergeList(m_entities, entities);
    mergeList(m_effects, effects);
    mergeList(m_enchantments, enchantments);

    rebuildIndexes();
}

// ============================================================================
// 查询索引维护
// ============================================================================

void CommandDatabase::rebuildIndexes()
{
    // ---- 指令 ----
    m_cmdEnglishIdx.clear();
    m_cmdChineseExactIdx.clear();
    m_cmdPrefixBucket.clear();
    for (int i = 0; i < m_commands.size(); ++i)
    {
        const CommandInfo &cmd = m_commands.at(i);
        m_cmdEnglishIdx.insert(cmd.name.toLower(), i);
        for (const QString &alias : cmd.chinese)
        {
            if (alias.isEmpty())
            {
                continue;
            }
            m_cmdChineseExactIdx.insert(alias.toLower(), i);
            m_cmdPrefixBucket[alias.at(0).toLower()].append(i);
        }
    }

    // ---- 物品 ----
    m_itemEnglishIdx.clear();
    m_itemChineseExactIdx.clear();
    m_itemPrefixBucket.clear();
    for (int i = 0; i < m_items.size(); ++i)
    {
        const ItemInfo &item = m_items.at(i);
        m_itemEnglishIdx.insert(item.english.toLower(), i);
        for (const QString &alias : item.chinese)
        {
            if (alias.isEmpty())
            {
                continue;
            }
            m_itemChineseExactIdx.insert(alias.toLower(), i);
            m_itemPrefixBucket[alias.at(0).toLower()].append(i);
        }
    }

    // ---- 实体 ----
    m_entityEnglishIdx.clear();
    m_entityChineseExactIdx.clear();
    m_entityPrefixBucket.clear();
    for (int i = 0; i < m_entities.size(); ++i)
    {
        const EntityInfo &entity = m_entities.at(i);
        m_entityEnglishIdx.insert(entity.english.toLower(), i);
        for (const QString &alias : entity.chinese)
        {
            if (alias.isEmpty())
            {
                continue;
            }
            m_entityChineseExactIdx.insert(alias.toLower(), i);
            m_entityPrefixBucket[alias.at(0).toLower()].append(i);
        }
    }

    // ---- 效果 ----
    m_effectEnglishIdx.clear();
    m_effectChineseExactIdx.clear();
    m_effectPrefixBucket.clear();
    for (int i = 0; i < m_effects.size(); ++i)
    {
        const EffectInfo &effect = m_effects.at(i);
        m_effectEnglishIdx.insert(effect.english.toLower(), i);
        for (const QString &alias : effect.chinese)
        {
            if (alias.isEmpty())
            {
                continue;
            }
            m_effectChineseExactIdx.insert(alias.toLower(), i);
            m_effectPrefixBucket[alias.at(0).toLower()].append(i);
        }
    }

    // ---- 附魔 ----
    m_enchantEnglishIdx.clear();
    m_enchantChineseExactIdx.clear();
    m_enchantPrefixBucket.clear();
    for (int i = 0; i < m_enchantments.size(); ++i)
    {
        const EnchantmentInfo &enchant = m_enchantments.at(i);
        m_enchantEnglishIdx.insert(enchant.english.toLower(), i);
        for (const QString &alias : enchant.chinese)
        {
            if (alias.isEmpty())
            {
                continue;
            }
            m_enchantChineseExactIdx.insert(alias.toLower(), i);
            m_enchantPrefixBucket[alias.at(0).toLower()].append(i);
        }
    }
}

// ============================================================================
// 指令查询
// ============================================================================

QList<CommandInfo> CommandDatabase::findCommandsByChinesePrefix(const QString &keyword) const
{
    QList<CommandInfo> result;
    if (keyword.isEmpty())
    {
        // 与原线性实现行为一致：空关键词返回全部
        return m_commands;
    }
    // 首字符桶：只需扫描同首字符条目，避免全量线性扫描
    const auto bucketIt = m_cmdPrefixBucket.constFind(keyword.at(0).toLower());
    if (bucketIt == m_cmdPrefixBucket.constEnd())
    {
        return result;
    }
    for (int idx : bucketIt.value())
    {
        const CommandInfo &cmd = m_commands.at(idx);
        for (const QString &alias : cmd.chinese)
        {
            if (alias.startsWith(keyword, Qt::CaseInsensitive))
            {
                result.append(cmd);
                break; // 单条目只加入一次
            }
        }
    }
    return result;
}

std::optional<CommandInfo> CommandDatabase::findCommandByChinese(const QString &keyword) const
{
    const int idx = m_cmdChineseExactIdx.value(keyword.toLower(), -1);
    if (idx < 0 || idx >= m_commands.size())
    {
        return std::nullopt;
    }
    return m_commands.at(idx);
}

std::optional<CommandInfo> CommandDatabase::findCommandByEnglish(const QString &name) const
{
    const int idx = m_cmdEnglishIdx.value(name.toLower(), -1);
    if (idx < 0 || idx >= m_commands.size())
    {
        return std::nullopt;
    }
    return m_commands.at(idx);
}

// ============================================================================
// 物品查询
// ============================================================================

QList<ItemInfo> CommandDatabase::findItemsByChinesePrefix(const QString &keyword) const
{
    QList<ItemInfo> result;
    if (keyword.isEmpty())
    {
        return m_items;
    }
    const auto bucketIt = m_itemPrefixBucket.constFind(keyword.at(0).toLower());
    if (bucketIt == m_itemPrefixBucket.constEnd())
    {
        return result;
    }
    for (int idx : bucketIt.value())
    {
        const ItemInfo &item = m_items.at(idx);
        for (const QString &alias : item.chinese)
        {
            if (alias.startsWith(keyword, Qt::CaseInsensitive))
            {
                result.append(item);
                break;
            }
        }
    }
    return result;
}

std::optional<ItemInfo> CommandDatabase::findItemByChinese(const QString &keyword) const
{
    const int idx = m_itemChineseExactIdx.value(keyword.toLower(), -1);
    if (idx < 0 || idx >= m_items.size())
    {
        return std::nullopt;
    }
    return m_items.at(idx);
}

std::optional<ItemInfo> CommandDatabase::findItemByEnglish(const QString &english) const
{
    const int idx = m_itemEnglishIdx.value(english.toLower(), -1);
    if (idx < 0 || idx >= m_items.size())
    {
        return std::nullopt;
    }
    return m_items.at(idx);
}

// ============================================================================
// 实体查询
// ============================================================================

QList<EntityInfo> CommandDatabase::findEntitiesByChinesePrefix(const QString &keyword) const
{
    QList<EntityInfo> result;
    if (keyword.isEmpty())
    {
        return m_entities;
    }
    const auto bucketIt = m_entityPrefixBucket.constFind(keyword.at(0).toLower());
    if (bucketIt == m_entityPrefixBucket.constEnd())
    {
        return result;
    }
    for (int idx : bucketIt.value())
    {
        const EntityInfo &entity = m_entities.at(idx);
        for (const QString &alias : entity.chinese)
        {
            if (alias.startsWith(keyword, Qt::CaseInsensitive))
            {
                result.append(entity);
                break;
            }
        }
    }
    return result;
}

std::optional<EntityInfo> CommandDatabase::findEntityByChinese(const QString &keyword) const
{
    const int idx = m_entityChineseExactIdx.value(keyword.toLower(), -1);
    if (idx < 0 || idx >= m_entities.size())
    {
        return std::nullopt;
    }
    return m_entities.at(idx);
}

std::optional<EntityInfo> CommandDatabase::findEntityByEnglish(const QString &english) const
{
    const int idx = m_entityEnglishIdx.value(english.toLower(), -1);
    if (idx < 0 || idx >= m_entities.size())
    {
        return std::nullopt;
    }
    return m_entities.at(idx);
}

// ============================================================================
// 效果查询
// ============================================================================

QList<EffectInfo> CommandDatabase::findEffectsByChinesePrefix(const QString &keyword) const
{
    QList<EffectInfo> result;
    if (keyword.isEmpty())
    {
        return m_effects;
    }
    const auto bucketIt = m_effectPrefixBucket.constFind(keyword.at(0).toLower());
    if (bucketIt == m_effectPrefixBucket.constEnd())
    {
        return result;
    }
    for (int idx : bucketIt.value())
    {
        const EffectInfo &effect = m_effects.at(idx);
        for (const QString &alias : effect.chinese)
        {
            if (alias.startsWith(keyword, Qt::CaseInsensitive))
            {
                result.append(effect);
                break;
            }
        }
    }
    return result;
}

std::optional<EffectInfo> CommandDatabase::findEffectByChinese(const QString &keyword) const
{
    const int idx = m_effectChineseExactIdx.value(keyword.toLower(), -1);
    if (idx < 0 || idx >= m_effects.size())
    {
        return std::nullopt;
    }
    return m_effects.at(idx);
}

std::optional<EffectInfo> CommandDatabase::findEffectByEnglish(const QString &english) const
{
    const int idx = m_effectEnglishIdx.value(english.toLower(), -1);
    if (idx < 0 || idx >= m_effects.size())
    {
        return std::nullopt;
    }
    return m_effects.at(idx);
}

// ============================================================================
// 附魔查询
// ============================================================================

QList<EnchantmentInfo> CommandDatabase::findEnchantmentsByChinesePrefix(const QString &keyword) const
{
    QList<EnchantmentInfo> result;
    if (keyword.isEmpty())
    {
        return m_enchantments;
    }
    const auto bucketIt = m_enchantPrefixBucket.constFind(keyword.at(0).toLower());
    if (bucketIt == m_enchantPrefixBucket.constEnd())
    {
        return result;
    }
    for (int idx : bucketIt.value())
    {
        const EnchantmentInfo &enchant = m_enchantments.at(idx);
        for (const QString &alias : enchant.chinese)
        {
            if (alias.startsWith(keyword, Qt::CaseInsensitive))
            {
                result.append(enchant);
                break;
            }
        }
    }
    return result;
}

std::optional<EnchantmentInfo> CommandDatabase::findEnchantmentByChinese(const QString &keyword) const
{
    const int idx = m_enchantChineseExactIdx.value(keyword.toLower(), -1);
    if (idx < 0 || idx >= m_enchantments.size())
    {
        return std::nullopt;
    }
    return m_enchantments.at(idx);
}

std::optional<EnchantmentInfo> CommandDatabase::findEnchantmentByEnglish(const QString &english) const
{
    const int idx = m_enchantEnglishIdx.value(english.toLower(), -1);
    if (idx < 0 || idx >= m_enchantments.size())
    {
        return std::nullopt;
    }
    return m_enchantments.at(idx);
}

// ============================================================================
// 全量访问
// ============================================================================

const QList<CommandInfo> &CommandDatabase::allCommands() const
{
    return m_commands;
}

const QList<ItemInfo> &CommandDatabase::allItems() const
{
    return m_items;
}

const QList<EntityInfo> &CommandDatabase::allEntities() const
{
    return m_entities;
}

const QList<EffectInfo> &CommandDatabase::allEffects() const
{
    return m_effects;
}

const QList<EnchantmentInfo> &CommandDatabase::allEnchantments() const
{
    return m_enchantments;
}

// ============================================================================
// 句子预设查询
// ============================================================================

const QList<CommandPreset> &CommandDatabase::allPresets() const
{
    return m_presets;
}

QList<CommandPreset> CommandDatabase::findPresetsByCategory(const QString &category) const
{
    if (category.isEmpty())
    {
        return m_presets;
    }
    QList<CommandPreset> result;
    for (const CommandPreset &preset : m_presets)
    {
        if (preset.category.compare(category, Qt::CaseInsensitive) == 0)
        {
            result.append(preset);
        }
    }
    return result;
}

QList<CommandPreset> CommandDatabase::findPresetsByPrefix(const QString &keyword) const
{
    QList<CommandPreset> result;
    if (keyword.isEmpty())
    {
        return m_presets;
    }
    for (const CommandPreset &preset : m_presets)
    {
        if (preset.chinese.startsWith(keyword, Qt::CaseInsensitive)
            || preset.english.startsWith(keyword, Qt::CaseInsensitive))
        {
            result.append(preset);
        }
    }
    return result;
}

QStringList CommandDatabase::presetCategories() const
{
    QStringList result;
    for (const CommandPreset &preset : m_presets)
    {
        if (!result.contains(preset.category))
        {
            result.append(preset.category);
        }
    }
    return result;
}
