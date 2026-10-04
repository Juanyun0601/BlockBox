/**
 * @file   CommandDatabase.h
 * @brief  内置 JSON 指令数据库加载与查询
 * @author BlockBox Team
 * @date   2026-07-04
 *
 * 提供从 Qt 资源 :/resources/command_database.json 加载内置指令数据，
 * 并支持按中文别名 / 英文名进行前缀与精确匹配查询。
 * 数据库包含四类条目：指令（commands）、物品（items）、实体（entities）、效果（effects）。
 */

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

// 前置声明以避免循环包含：CommandPack.h 会包含本文件
class CommandPack;

/**
 * @brief 单个指令参数描述
 */
struct ParamInfo
{
    QString name;                            ///< 参数名（中文，如 "玩家"）
    QString type;                            ///< 参数类型（selector/item/entity/effect/int/float/string/nbt/enum/pos/block）
    std::optional<QString> defaultValue;     ///< 默认值（JSON 中的 "default" 字段，可能不存在）
    bool required = true;                    ///< 是否必填
    QStringList candidates;                  ///< 候选值列表（仅 type=enum 时填充）
};

/**
 * @brief 单条指令信息
 */
struct CommandInfo
{
    QString name;          ///< 英文指令名（如 "give"）
    QStringList chinese;   ///< 中文别名数组
    QString minVersion;    ///< 最低支持版本（如 "1.13"）
    int opLevel = 0;       ///< 所需 OP 等级（0-4）
    bool serverOnly = false; ///< 是否仅服务端可用
    QString syntax;        ///< 语法模板
    QList<ParamInfo> params; ///< 参数列表
    QString description;   ///< 指令描述
};

/**
 * @brief 物品中英文映射
 */
struct ItemInfo
{
    QString english;       ///< 英文 ID（如 "diamond_sword"）
    QStringList chinese;   ///< 中文名数组（如 ["钻石剑"]）
};

/**
 * @brief 实体中英文映射
 */
struct EntityInfo
{
    QString english;       ///< 英文 ID（如 "zombie"）
    QStringList chinese;   ///< 中文名数组（如 ["僵尸"]）
};

/**
 * @brief 药水效果中英文映射
 */
struct EffectInfo
{
    QString english;       ///< 英文 ID（如 "speed"）
    QStringList chinese;   ///< 中文名数组（如 ["速度", "迅捷"]）
};

/**
 * @brief 附魔中英文映射
 *
 * english 为注册名（如 "minecraft:sharpness" 或 "sharpness"），
 * chinese 为中文名数组（如 ["锋利"]）。
 */
struct EnchantmentInfo
{
    QString english;       ///< 英文 ID（如 "minecraft:sharpness"）
    QStringList chinese;   ///< 中文名数组（如 ["锋利"]）
};

/**
 * @brief 指令句子预设（常用完整指令模板）
 *
 * 内置一批经过验证的完整指令句子，用户可通过「预设」菜单一键填入。
 * chinese 为中文描述（填入中文输入框便于用户理解/微调），
 * english 为可直接执行的英文指令（直接写入英文输出框）。
 */
struct CommandPreset
{
    QString id;          ///< 唯一标识
    QString category;    ///< 分类（如 "物品" / "传送" / "药水效果"）
    QString chinese;     ///< 中文句子
    QString english;     ///< 英文指令（含 /）
    QString description; ///< 说明
};

/**
 * @brief 内置指令数据库
 *
 * 从 Qt 资源 :/resources/command_database.json 加载指令 / 物品 / 实体 / 效果
 * 四类数据，提供中英文前缀 / 精确匹配查询接口。
 *
 * 使用示例：
 * @code
 *   CommandDatabase db;
 *   if (db.load())
 *   {
 *       auto cmd = db.findCommandByChinese(QStringLiteral("给予"));
 *       if (cmd)
 *       {
 *           qDebug() << "matched:" << cmd->name << cmd->syntax;
 *       }
 *   }
 * @endcode
 */
class CommandDatabase
{
public:
    /**
     * @brief 从 Qt 资源 :/resources/command_database.json 加载数据库
     * @return 加载成功返回 true，失败返回 false（同时 qWarning 输出原因）
     */
    bool load();

    /**
     * @brief 从指定文件路径加载数据库（用于基岩版等不同指令集）
     * @param filePath JSON 文件的绝对路径
     * @return 加载成功返回 true，失败返回 false（同时 qWarning 输出原因）
     */
    bool loadFromFile(const QString &filePath);

    /**
     * @brief 是否已成功加载
     * @return 已加载返回 true
     */
    bool isLoaded() const;

    /**
     * @brief 清空所有数据（内置 + 自定义）
     *
     * 用于指令包管理器重新构建数据集合时先清空再 load() + mergePack()。
     */
    void clearAll();

    /**
     * @brief 合并一个指令包的数据到数据库
     *
     * 将指令包中的 commands / items / entities / effects 追加到当前数据集合。
     * 同名条目（按英文名匹配）会被指令包版本覆盖。
     *
     * @param pack 已加载完成的指令包
     * @return 至少合并了一条数据返回 true
     */
    bool mergePack(const CommandPack &pack);

    /**
     * @brief 合并游戏运行时提取的动态数据（模组物品/实体/效果/附魔）
     *
     * 由 GameRegistry 从游戏 JAR / 模组 JAR 提取后调用，与内置数据按英文名
     * 覆盖合并（模组数据优先于内置静态数据）。合并后自动重建查询索引。
     *
     * @param items       模组/游戏物品（含中英文名）
     * @param entities    模组/游戏实体
     * @param effects     模组/游戏状态效果
     * @param enchantments 模组/游戏附魔
     */
    void mergeDynamicData(const QList<ItemInfo> &items,
                          const QList<EntityInfo> &entities,
                          const QList<EffectInfo> &effects,
                          const QList<EnchantmentInfo> &enchantments);

    // ---- 指令查询 ----

    /**
     * @brief 按中文别名前缀匹配指令
     * @param keyword 用户输入的关键词
     * @return 所有 chinese 数组中任一别名以 keyword 开头的指令（大小写不敏感）
     */
    QList<CommandInfo> findCommandsByChinesePrefix(const QString &keyword) const;

    /**
     * @brief 按中文别名精确匹配指令
     * @param keyword 用户输入的关键词
     * @return chinese 数组中任一别名 == keyword 的指令（大小写不敏感），未匹配返回 std::nullopt
     */
    std::optional<CommandInfo> findCommandByChinese(const QString &keyword) const;

    /**
     * @brief 按英文名精确匹配指令
     * @param name 英文指令名
     * @return 匹配的指令（大小写不敏感），未匹配返回 std::nullopt
     */
    std::optional<CommandInfo> findCommandByEnglish(const QString &name) const;

    // ---- 物品查询 ----

    /**
     * @brief 按中文前缀匹配物品
     * @param keyword 用户输入的关键词
     * @return 所有 chinese 数组中任一别名以 keyword 开头的物品
     */
    QList<ItemInfo> findItemsByChinesePrefix(const QString &keyword) const;

    /**
     * @brief 按中文精确匹配物品
     * @param keyword 用户输入的关键词
     * @return 匹配的物品，未匹配返回 std::nullopt
     */
    std::optional<ItemInfo> findItemByChinese(const QString &keyword) const;

    /**
     * @brief 按英文精确匹配物品
     * @param english 英文 ID
     * @return 匹配的物品（大小写不敏感），未匹配返回 std::nullopt
     */
    std::optional<ItemInfo> findItemByEnglish(const QString &english) const;

    // ---- 实体查询 ----

    /**
     * @brief 按中文前缀匹配实体
     * @param keyword 用户输入的关键词
     * @return 所有 chinese 数组中任一别名以 keyword 开头的实体
     */
    QList<EntityInfo> findEntitiesByChinesePrefix(const QString &keyword) const;

    /**
     * @brief 按中文精确匹配实体
     * @param keyword 用户输入的关键词
     * @return 匹配的实体，未匹配返回 std::nullopt
     */
    std::optional<EntityInfo> findEntityByChinese(const QString &keyword) const;

    /**
     * @brief 按英文精确匹配实体
     * @param english 英文 ID
     * @return 匹配的实体（大小写不敏感），未匹配返回 std::nullopt
     */
    std::optional<EntityInfo> findEntityByEnglish(const QString &english) const;

    // ---- 效果查询 ----

    /**
     * @brief 按中文前缀匹配效果
     * @param keyword 用户输入的关键词
     * @return 所有 chinese 数组中任一别名以 keyword 开头的效果
     */
    QList<EffectInfo> findEffectsByChinesePrefix(const QString &keyword) const;

    /**
     * @brief 按中文精确匹配效果
     * @param keyword 用户输入的关键词
     * @return 匹配的效果，未匹配返回 std::nullopt
     */
    std::optional<EffectInfo> findEffectByChinese(const QString &keyword) const;

    /**
     * @brief 按英文精确匹配效果
     * @param english 英文 ID
     * @return 匹配的效果（大小写不敏感），未匹配返回 std::nullopt
     */
    std::optional<EffectInfo> findEffectByEnglish(const QString &english) const;

    // ---- 附魔查询 ----

    /**
     * @brief 按中文前缀匹配附魔
     * @param keyword 用户输入的关键词
     * @return 所有 chinese 数组中任一别名以 keyword 开头的附魔
     */
    QList<EnchantmentInfo> findEnchantmentsByChinesePrefix(const QString &keyword) const;

    /**
     * @brief 按中文精确匹配附魔
     * @param keyword 用户输入的关键词
     * @return 匹配的附魔，未匹配返回 std::nullopt
     */
    std::optional<EnchantmentInfo> findEnchantmentByChinese(const QString &keyword) const;

    /**
     * @brief 按英文精确匹配附魔
     * @param english 英文 ID
     * @return 匹配的附魔（大小写不敏感），未匹配返回 std::nullopt
     */
    std::optional<EnchantmentInfo> findEnchantmentByEnglish(const QString &english) const;

    // ---- 全量访问 ----

    /**
     * @brief 获取所有指令
     * @return 指令列表的 const 引用
     */
    const QList<CommandInfo> &allCommands() const;

    /**
     * @brief 获取所有物品
     * @return 物品列表的 const 引用
     */
    const QList<ItemInfo> &allItems() const;

    /**
     * @brief 获取所有实体
     * @return 实体列表的 const 引用
     */
    const QList<EntityInfo> &allEntities() const;

    /**
     * @brief 获取所有效果
     * @return 效果列表的 const 引用
     */
    const QList<EffectInfo> &allEffects() const;

    /**
     * @brief 获取所有附魔
     * @return 附魔列表的 const 引用
     */
    const QList<EnchantmentInfo> &allEnchantments() const;

    // ---- 句子预设查询 ----

    /**
     * @brief 获取全部句子预设
     * @return 预设列表的 const 引用
     */
    const QList<CommandPreset> &allPresets() const;

    /**
     * @brief 按分类获取句子预设
     * @param category 分类名（如 "物品"），空字符串返回全部
     * @return 该分类下的预设列表（保持 JSON 顺序）
     */
    QList<CommandPreset> findPresetsByCategory(const QString &category) const;

    /**
     * @brief 按中文/英文句子前缀匹配预设
     * @param keyword 用户输入的关键词
     * @return 任一文本以 keyword 开头的预设（大小写不敏感）
     */
    QList<CommandPreset> findPresetsByPrefix(const QString &keyword) const;

    /**
     * @brief 获取所有预设分类（去重、保持出现顺序）
     * @return 分类名列表
     */
    QStringList presetCategories() const;

private:
    /**
     * @brief 重建查询索引（load / mergePack / clearAll 后必须调用）
     *
     * 为四类数据构建：
     *   - 英文精确索引：小写英文名 → 列表索引（O(1) 精确匹配）
     *   - 中文精确索引：中文别名 → 列表索引（O(1) 精确匹配）
     *   - 首字符前缀桶：首字符 → 条目索引列表（前缀匹配只需扫首字符桶，
     *     避免每次击键全量线性扫描全部条目 × 全部别名）
     */
    void rebuildIndexes();

    bool m_loaded = false;            ///< 是否已成功加载
    QList<CommandInfo> m_commands;    ///< 指令列表
    QList<ItemInfo> m_items;          ///< 物品列表
    QList<EntityInfo> m_entities;     ///< 实体列表
    QList<EffectInfo> m_effects;      ///< 效果列表
    QList<EnchantmentInfo> m_enchantments; ///< 附魔列表
    QList<CommandPreset> m_presets;   ///< 句子预设列表

    // ---- 查询索引（由 rebuildIndexes() 维护，保持与 *_Index / *_Bucket 同步） ----
    QHash<QString, int> m_cmdEnglishIdx;             ///< 指令：小写英文名 → 索引
    QHash<QString, int> m_cmdChineseExactIdx;        ///< 指令：中文别名 → 索引
    QHash<QChar, QList<int>> m_cmdPrefixBucket;      ///< 指令：首字符 → 条目索引
    QHash<QString, int> m_itemEnglishIdx;            ///< 物品：小写英文名 → 索引
    QHash<QString, int> m_itemChineseExactIdx;       ///< 物品：中文别名 → 索引
    QHash<QChar, QList<int>> m_itemPrefixBucket;     ///< 物品：首字符 → 条目索引
    QHash<QString, int> m_entityEnglishIdx;          ///< 实体：小写英文名 → 索引
    QHash<QString, int> m_entityChineseExactIdx;     ///< 实体：中文别名 → 索引
    QHash<QChar, QList<int>> m_entityPrefixBucket;   ///< 实体：首字符 → 条目索引
    QHash<QString, int> m_effectEnglishIdx;          ///< 效果：小写英文名 → 索引
    QHash<QString, int> m_effectChineseExactIdx;     ///< 效果：中文别名 → 索引
    QHash<QChar, QList<int>> m_effectPrefixBucket;   ///< 效果：首字符 → 条目索引
    QHash<QString, int> m_enchantEnglishIdx;         ///< 附魔：小写英文名 → 索引
    QHash<QString, int> m_enchantChineseExactIdx;    ///< 附魔：中文别名 → 索引
    QHash<QChar, QList<int>> m_enchantPrefixBucket;  ///< 附魔：首字符 → 条目索引
};
