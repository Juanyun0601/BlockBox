#ifndef CONTENTDATA_H
#define CONTENTDATA_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>

/**
 * @brief 内容类型枚举
 */
enum class ContentType
{
    Mod = 0,
    DataPack = 1,
    ResourcePack = 2,
    ShaderPack = 3,
    World = 4,
    Modpack = 5
};

/**
 * @brief 内容类型配置
 */
struct ContentTypeConfig
{
    ContentType type;
    QString displayName;       // 显示名称
    QString folderName;        // 实例下的文件夹名
    QStringList extensions;    // 支持的文件扩展名
    QString cfClassId;         // CurseForge classId
    QString mrProjectType;     // Modrinth project_type
    QString searchPlaceholder; // 搜索框占位符文本
    QString downloadLabel;     // 下载区域标签
    QString sectionTitle;      // 详情页标题
    QString emptyHint;         // 空列表提示

    static ContentTypeConfig getConfig(ContentType type)
    {
        switch (type)
        {
        case ContentType::Mod:
            return {
                ContentType::Mod,
                QStringLiteral("模组"),
                QStringLiteral("mods"),
                { "*.jar" },
                QStringLiteral("6"),
                QStringLiteral("mod"),
                QStringLiteral("搜索模组..."),
                QStringLiteral("模组下载"),
                QStringLiteral("模组"),
                QStringLiteral("暂无模组")
            };
        case ContentType::DataPack:
            return {
                ContentType::DataPack,
                QStringLiteral("数据包"),
                QStringLiteral("datapacks"),
                { "*.zip", "*.jar" },
                QStringLiteral("4546"),
                QStringLiteral("datapack"),
                QStringLiteral("搜索数据包..."),
                QStringLiteral("数据包下载"),
                QStringLiteral("数据包"),
                QStringLiteral("暂无数据包")
            };
        case ContentType::ResourcePack:
            return {
                ContentType::ResourcePack,
                QStringLiteral("资源包"),
                QStringLiteral("resourcepacks"),
                { "*.zip" },
                QStringLiteral("12"),
                QStringLiteral("resourcepack"),
                QStringLiteral("搜索资源包..."),
                QStringLiteral("资源包下载"),
                QStringLiteral("资源包"),
                QStringLiteral("暂无资源包")
            };
        case ContentType::ShaderPack:
            return {
                ContentType::ShaderPack,
                QStringLiteral("光影包"),
                QStringLiteral("shaderpacks"),
                { "*.zip" },
                QStringLiteral("6552"),
                QStringLiteral("shader"),
                QStringLiteral("搜索光影包..."),
                QStringLiteral("光影包下载"),
                QStringLiteral("光影包"),
                QStringLiteral("暂无光影包")
            };
        case ContentType::World:
            return {
                ContentType::World,
                QStringLiteral("世界"),
                QStringLiteral("saves"),
                { "*.zip" },
                QStringLiteral("17"),
                QStringLiteral("mod"),  // Modrinth has no world project type, use mod
                QStringLiteral("搜索世界存档..."),
                QStringLiteral("世界下载"),
                QStringLiteral("世界"),
                QStringLiteral("暂无世界存档")
            };
        case ContentType::Modpack:
            return {
                ContentType::Modpack,
                QStringLiteral("整合包"),
                QStringLiteral("modpacks"),
                { "*.zip", "*.mrpack" },
                QStringLiteral("4471"),
                QStringLiteral("modpack"),
                QStringLiteral("搜索整合包..."),
                QStringLiteral("整合包下载"),
                QStringLiteral("整合包"),
                QStringLiteral("暂无整合包")
            };
        }
        return {};
    }
};

// 复用 ModData.h 中的 ModInfo 作为内容信息结构体
// 在需要时添加 contentType 字段标识
#include "utils/mod/ModData.h"

/**
 * @brief 单条资源引用信息（用于 AI 助手引用功能）
 *
 * 仅保存元信息（类别/名称/路径/大小/附加说明），不读取文件内容。
 * 对于网络资源与网页引用，path 字段保存 URL，extra 保存来源/描述等。
 * 调用方据此拼装为文本描述附加到用户消息，AI 通过工具调用按需读取具体内容。
 * category="AI回答" 时表示引用历史 AI 回答文字，extra 保存完整引用文字。
 */
struct ResourceReference
{
    QString category;   ///< 类别：模组 / 资源包 / 光影包 / 投影 / 实例 / 文件 / 网络模组 / 网络资源包 / 网络光影包 / 网络数据包 / 网页 / AI回答
    QString name;       ///< 显示名（文件名、实例名或资源标题）
    QString path;       ///< 绝对路径或 URL（网络资源与网页引用时为 URL；AI回答时为空）
    qint64 size = 0;    ///< 文件大小（字节），网络/网页/AI回答引用可为 0
    QString extra;      ///< 附加信息（实例版本/加载器、资源来源、网页标题、AI回答完整文字等）

    bool operator==(const ResourceReference &other) const
    {
        return path == other.path && category == other.category && extra == other.extra;
    }
    bool operator!=(const ResourceReference &other) const
    {
        return !(*this == other);
    }
};

#endif // CONTENTDATA_H