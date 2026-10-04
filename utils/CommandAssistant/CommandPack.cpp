/**
 * @file   CommandPack.cpp
 * @brief  自定义指令包 JSON 读写实现
 * @author BlockBox Team
 * @date   2026-07-17
 *
 * 实现细节：
 *   1. 复用 CommandDatabase.cpp 中的 parseParam / parseCommand 内部逻辑（此处为独立实现，
 *      避免暴露匿名命名空间中的内部函数）
 *   2. saveToFile 输出格式化的 JSON（缩进 4 空格），便于用户阅读与版本管理
 *   3. loadFromFile 兼容 command_database.json（无元信息字段时使用文件名作为包名）
 */

#include "CommandPack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace {

/**
 * @brief 解析单个 ParamInfo
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
 * @brief 通用：从 JSON 数组解析中英文映射条目
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

/**
 * @brief 序列化 ParamInfo 为 JSON 对象
 */
QJsonObject paramToJson(const ParamInfo &param)
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = param.name;
    obj[QStringLiteral("type")] = param.type;
    if (param.defaultValue.has_value())
    {
        obj[QStringLiteral("default")] = param.defaultValue.value();
    }
    obj[QStringLiteral("required")] = param.required;
    if (param.type == QStringLiteral("enum") && !param.candidates.isEmpty())
    {
        QJsonArray arr;
        for (const QString &c : param.candidates)
        {
            arr.append(c);
        }
        obj[QStringLiteral("candidates")] = arr;
    }
    return obj;
}

/**
 * @brief 序列化 CommandInfo 为 JSON 对象
 */
QJsonObject commandToJson(const CommandInfo &cmd)
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = cmd.name;
    QJsonArray chineseArr;
    for (const QString &alias : cmd.chinese)
    {
        chineseArr.append(alias);
    }
    obj[QStringLiteral("chinese")] = chineseArr;
    obj[QStringLiteral("minVersion")] = cmd.minVersion;
    obj[QStringLiteral("opLevel")] = cmd.opLevel;
    obj[QStringLiteral("serverOnly")] = cmd.serverOnly;
    obj[QStringLiteral("syntax")] = cmd.syntax;
    obj[QStringLiteral("description")] = cmd.description;
    QJsonArray paramsArr;
    for (const ParamInfo &p : cmd.params)
    {
        paramsArr.append(paramToJson(p));
    }
    obj[QStringLiteral("params")] = paramsArr;
    return obj;
}

/**
 * @brief 通用：序列化中英文映射条目
 */
template<typename T>
QJsonObject translationEntryToJson(const T &entry)
{
    QJsonObject obj;
    obj[QStringLiteral("english")] = entry.english;
    QJsonArray arr;
    for (const QString &alias : entry.chinese)
    {
        arr.append(alias);
    }
    obj[QStringLiteral("chinese")] = arr;
    return obj;
}

} // namespace

// ============================================================================
// 加载
// ============================================================================

bool CommandPack::loadFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists())
    {
        qWarning() << "CommandPack: file not found:" << filePath;
        return false;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "CommandPack: failed to open file:" << file.errorString();
        return false;
    }

    const QByteArray rawData = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(rawData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        qWarning() << "CommandPack: JSON parse failed:" << parseError.errorString();
        return false;
    }

    if (!loadFromJson(doc.object()))
    {
        return false;
    }

    m_info.filePath = QDir(filePath).absolutePath();
    // 元信息缺少 name 字段时使用文件名作为包名
    if (m_info.name.isEmpty())
    {
        m_info.name = QFileInfo(filePath).completeBaseName();
    }
    m_loaded = true;
    return true;
}

bool CommandPack::loadFromJson(const QJsonObject &obj)
{
    m_info.name = obj.value(QStringLiteral("name")).toString();
    m_info.description = obj.value(QStringLiteral("description")).toString();
    m_info.author = obj.value(QStringLiteral("author")).toString();
    m_info.version = obj.value(QStringLiteral("version")).toString();
    m_info.mcVersion = obj.value(QStringLiteral("mcVersion")).toString();

    m_commands.clear();
    const QJsonArray commandsArray = obj.value(QStringLiteral("commands")).toArray();
    for (const QJsonValue &value : commandsArray)
    {
        m_commands.append(parseCommand(value.toObject()));
    }

    m_items = parseTranslationEntries<ItemInfo>(obj.value(QStringLiteral("items")).toArray());
    m_entities = parseTranslationEntries<EntityInfo>(obj.value(QStringLiteral("entities")).toArray());
    m_effects = parseTranslationEntries<EffectInfo>(obj.value(QStringLiteral("effects")).toArray());

    refreshCounts();
    m_loaded = true;
    return true;
}

bool CommandPack::saveToFile(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qWarning() << "CommandPack: failed to open file for write:" << file.errorString();
        return false;
    }

    const QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QJsonObject CommandPack::toJson() const
{
    QJsonObject root;
    root[QStringLiteral("name")] = m_info.name;
    root[QStringLiteral("description")] = m_info.description;
    root[QStringLiteral("author")] = m_info.author;
    root[QStringLiteral("version")] = m_info.version;
    root[QStringLiteral("mcVersion")] = m_info.mcVersion;

    QJsonArray commandsArray;
    for (const CommandInfo &cmd : m_commands)
    {
        commandsArray.append(commandToJson(cmd));
    }
    root[QStringLiteral("commands")] = commandsArray;

    QJsonArray itemsArray;
    for (const ItemInfo &item : m_items)
    {
        itemsArray.append(translationEntryToJson(item));
    }
    root[QStringLiteral("items")] = itemsArray;

    QJsonArray entitiesArray;
    for (const EntityInfo &entity : m_entities)
    {
        entitiesArray.append(translationEntryToJson(entity));
    }
    root[QStringLiteral("entities")] = entitiesArray;

    QJsonArray effectsArray;
    for (const EffectInfo &effect : m_effects)
    {
        effectsArray.append(translationEntryToJson(effect));
    }
    root[QStringLiteral("effects")] = effectsArray;

    return root;
}

void CommandPack::refreshCounts()
{
    m_info.commandCount = m_commands.size();
    m_info.itemCount = m_items.size();
    m_info.entityCount = m_entities.size();
    m_info.effectCount = m_effects.size();
}
