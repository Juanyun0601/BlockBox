/**
 * @file   CommandPack.h
 * @brief  自定义指令包数据结构与 JSON 读写
 * @author BlockBox Team
 * @date   2026-07-17
 *
 * 指令包（CommandPack）是 command_database.json 格式的扩展文件，
 * 在原 commands / items / entities / effects 四类数据基础上增加元信息字段
 * （name / description / author / version / mcVersion）。
 *
 * 用途：
 *   - 用户可加载外部 .json 指令包扩展内置指令数据库
 *   - 用户可通过 CommandPackEditor 自行制作并导出指令包
 *
 * 文件格式示例：
 * @code
 * {
 *   "name": "我的指令包",
 *   "description": "包含自定义指令与物品映射",
 *   "author": "玩家",
 *   "version": "1.0.0",
 *   "mcVersion": "1.20.4",
 *   "commands": [ { "name": "...", "chinese": [...], ... } ],
 *   "items":     [ { "english": "...", "chinese": [...] } ],
 *   "entities":  [ ... ],
 *   "effects":   [ ... ]
 * }
 * @endcode
 */

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include "utils/CommandAssistant/CommandDatabase.h" // CommandInfo / ItemInfo / EntityInfo / EffectInfo 完整定义

/**
 * @brief 指令包元信息（不含数据，用于列表显示）
 */
struct CommandPackInfo
{
    QString name;        ///< 指令包名称
    QString description; ///< 描述
    QString author;      ///< 作者
    QString version;     ///< 指令包版本
    QString mcVersion;   ///< 目标 Minecraft 版本
    QString filePath;    ///< 文件绝对路径
    bool enabled = true; ///< 是否启用（启用后才会合并到数据库）
    int commandCount = 0; ///< 指令条目数
    int itemCount = 0;    ///< 物品条目数
    int entityCount = 0;  ///< 实体条目数
    int effectCount = 0;  ///< 效果条目数

    bool operator==(const CommandPackInfo &other) const
    {
        return filePath == other.filePath;
    }
    bool operator!=(const CommandPackInfo &other) const
    {
        return !(*this == other);
    }
};

/**
 * @brief 自定义指令包（含完整数据）
 *
 * 持有元信息与四类数据列表，支持从 JSON 文件加载、保存到 JSON 文件。
 * 数据结构复用 CommandDatabase 中的 CommandInfo / ItemInfo / EntityInfo / EffectInfo。
 */
class CommandPack
{
public:
    CommandPack() = default;

    // ---- 文件 IO ----

    /**
     * @brief 从 JSON 文件加载指令包
     * @param filePath 文件绝对路径
     * @return 加载成功返回 true，失败返回 false（同时 qWarning 输出原因）
     */
    bool loadFromFile(const QString &filePath);

    /**
     * @brief 保存指令包到 JSON 文件
     * @param filePath 目标文件路径（已存在则覆盖）
     * @return 保存成功返回 true
     */
    bool saveToFile(const QString &filePath) const;

    /**
     * @brief 从 JSON 对象加载（不含 filePath）
     * @param obj JSON 根对象
     * @return 解析成功返回 true
     */
    bool loadFromJson(const QJsonObject &obj);

    /**
     * @brief 序列化为 JSON 对象
     * @return JSON 根对象
     */
    QJsonObject toJson() const;

    // ---- 元信息访问 ----

    const QString &name() const { return m_info.name; }
    const QString &description() const { return m_info.description; }
    const QString &author() const { return m_info.author; }
    const QString &version() const { return m_info.version; }
    const QString &mcVersion() const { return m_info.mcVersion; }
    const QString &filePath() const { return m_info.filePath; }
    const CommandPackInfo &info() const { return m_info; }

    // ---- 元信息编辑（编辑器使用） ----

    void setName(const QString &v) { m_info.name = v; }
    void setDescription(const QString &v) { m_info.description = v; }
    void setAuthor(const QString &v) { m_info.author = v; }
    void setVersion(const QString &v) { m_info.version = v; }
    void setMcVersion(const QString &v) { m_info.mcVersion = v; }
    void setFilePath(const QString &v) { m_info.filePath = v; }

    // ---- 数据访问（可编辑，供编辑器使用） ----

    QList<CommandInfo> &commands() { return m_commands; }
    QList<ItemInfo> &items() { return m_items; }
    QList<EntityInfo> &entities() { return m_entities; }
    QList<EffectInfo> &effects() { return m_effects; }

    const QList<CommandInfo> &commands() const { return m_commands; }
    const QList<ItemInfo> &items() const { return m_items; }
    const QList<EntityInfo> &entities() const { return m_entities; }
    const QList<EffectInfo> &effects() const { return m_effects; }

    /**
     * @brief 同步元信息中的条目计数字段
     *
     * 在编辑器修改数据后调用，保证 info() 中的 count 字段反映最新数据。
     */
    void refreshCounts();

    /**
     * @brief 是否已成功加载
     */
    bool isLoaded() const { return m_loaded; }

private:
    bool m_loaded = false;          ///< 是否已成功加载
    CommandPackInfo m_info;         ///< 元信息
    QList<CommandInfo> m_commands;   ///< 指令列表
    QList<ItemInfo> m_items;        ///< 物品列表
    QList<EntityInfo> m_entities;   ///< 实体列表
    QList<EffectInfo> m_effects;     ///< 效果列表
};
