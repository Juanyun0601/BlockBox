/**
 * @file   McpToolCatalog.cpp
 * @brief  本地 MCP 服务器对外开放的工具目录实现
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "utils/mcp/McpToolCatalog.h"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

namespace {

struct CatalogEntry
{
    const char *name;
    bool readOnly;
    bool destructive;
};

// 对外开放的工具清单：顺序即 tools/list 的展示顺序。
// 只收录「不依赖 AI 聊天界面」的本地能力；ask_user / update_task_list /
// 联网搜索 / 论坛这些与聊天页强绑定的工具不对外。
const CatalogEntry kEntries[] = {
    {"list_instances", true, false},
    {"list_mods", true, false},
    {"read_game_log", true, false},
    {"read_crash_report", true, false},
    {"analyze_crash_log", true, false},
    {"check_mod_updates", true, false},
    {"list_saves", true, false},
    {"read_save_info", true, false},
    {"list_servers", true, false},
    {"ping_server", true, false},
    {"find_structure", true, false},
    {"read_litematic", true, false},
    {"list_download_tasks", true, false},
    {"get_settings", true, false},
    {"detect_java", true, false},
    {"check_loader_compatibility", true, false},
    {"get_game_versions", true, false},
    {"manage_mods", false, true},
    {"manage_world_backup", false, true},
    {"launch_game", false, false},
    {"download_instance", false, false},
    {"modify_instance", false, false},
    {"download_resource", false, false},
};

const QHash<QString, QJsonObject> &baseTools()
{
    static const QHash<QString, QJsonObject> cache = []() {
        QHash<QString, QJsonObject> map;
        QFile file(QStringLiteral(":/resources/ai_agent_tools.json"));
        if (!file.open(QIODevice::ReadOnly))
        {
            return map;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            return map;
        }
        const QJsonArray tools = doc.object().value(QStringLiteral("tools")).toArray();
        for (const QJsonValue &value : tools)
        {
            const QJsonObject obj = value.toObject();
            const QString name = obj.value(QStringLiteral("name")).toString();
            if (!name.isEmpty())
            {
                map.insert(name, obj);
            }
        }
        return map;
    }();
    return cache;
}

const CatalogEntry *findEntry(const QString &name)
{
    for (const CatalogEntry &entry : kEntries)
    {
        if (name == QLatin1String(entry.name))
        {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

namespace McpToolCatalog {

bool isKnown(const QString &name)
{
    return findEntry(name) != nullptr;
}

bool isWrite(const QString &name)
{
    const CatalogEntry *entry = findEntry(name);
    return entry != nullptr && !entry->readOnly;
}

bool isDestructive(const QString &name)
{
    const CatalogEntry *entry = findEntry(name);
    return entry != nullptr && entry->destructive;
}

QJsonArray tools(bool allowWrite)
{
    QJsonArray result;
    for (const CatalogEntry &entry : kEntries)
    {
        if (!entry.readOnly && !allowWrite)
        {
            continue;
        }

        const QString name = QString::fromLatin1(entry.name);
        const QJsonObject base = baseTools().value(name);

        QJsonObject schema = base.value(QStringLiteral("parameters")).toObject();
        if (schema.isEmpty())
        {
            schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject()}};
        }

        QString description = base.value(QStringLiteral("description")).toString();
        if (description.isEmpty())
        {
            description = name;
        }

        if (!entry.readOnly)
        {
            QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
            properties.insert(QStringLiteral("confirm"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("boolean")},
                {QStringLiteral("description"),
                 QStringLiteral("写操作确认开关。必须显式传 true 才会真正执行；不传或传 false 时"
                                "只返回确认提示，不会改动任何文件。")},
            });
            schema.insert(QStringLiteral("properties"), properties);
            description += QStringLiteral("\n【写操作】需要先在方块盒子设置里开启「允许写操作」，"
                                          "并在参数中传 confirm=true 才会执行。");
        }

        QJsonObject tool;
        tool.insert(QStringLiteral("name"), name);
        tool.insert(QStringLiteral("description"), description);
        tool.insert(QStringLiteral("inputSchema"), schema);

        QJsonObject annotations;
        annotations.insert(QStringLiteral("readOnlyHint"), entry.readOnly);
        annotations.insert(QStringLiteral("destructiveHint"), entry.destructive);
        annotations.insert(QStringLiteral("idempotentHint"), entry.readOnly);
        tool.insert(QStringLiteral("annotations"), annotations);

        result.append(tool);
    }
    return result;
}

QString summary(bool allowWrite)
{
    int total = 0;
    int writeCount = 0;
    for (const CatalogEntry &entry : kEntries)
    {
        if (!entry.readOnly && !allowWrite)
        {
            continue;
        }
        ++total;
        if (!entry.readOnly)
        {
            ++writeCount;
        }
    }
    if (writeCount > 0)
    {
        return QStringLiteral("方块盒子本地 MCP 服务器：%1 个工具（只读 %2 个，写操作 %3 个）")
            .arg(total)
            .arg(total - writeCount)
            .arg(writeCount);
    }
    return QStringLiteral("方块盒子本地 MCP 服务器：%1 个只读工具（写操作未开启）").arg(total);
}

} // namespace McpToolCatalog
