/**
 * @file   GameRegistry.h
 * @brief  游戏注册表数据自动提取器（方块/物品/实体/效果/附魔）
 * @author BlockBox Team
 * @date   2026-08-06
 *
 * 通过解析游戏版本 JAR 与模组 JAR 的资源翻译文件
 *   assets/<namespace>/lang/zh_cn.json（en_us.json 兜底）
 * 反向构建「英文注册名 ↔ 中文显示名」映射，覆盖当前游戏可用的
 * 方块 / 物品 / 实体 / 效果 / 附魔 五类数据。
 *
 * 由于 Forge / Fabric / NeoForge 模组 JAR 均使用相同的 assets 资源结构，
 * 本提取器无需区分加载器，天然支持全部主流模组。
 *
 * 提取规则（Minecraft 1.13+ lang 键格式）：
 *   - "block.minecraft.stone"          → 方块 minecraft:stone（石头）
 *   - "item.minecraft.diamond_sword"   → 物品 minecraft:diamond_sword（钻石剑）
 *   - "entity.minecraft.zombie"        → 实体 minecraft:zombie（僵尸，含生物）
 *   - "effect.minecraft.speed"         → 效果 minecraft:speed（速度）
 *   - "enchantment.minecraft.sharpness"→ 附魔 minecraft:sharpness（锋利）
 *
 * 使用方式：仅作为「提取器 + 中转容器」，提取完成后由调用方将数据
 * 合并到 CommandDatabase / BlockRegistry（见 InstanceAssistantWindow
 * 的 runContextDetectionInBackground）。
 */

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "utils/CommandAssistant/BlockRegistry.h"
#include "utils/CommandAssistant/CommandDatabase.h"

/**
 * @brief 游戏注册表数据提取器
 */
class GameRegistry
{
public:
    /**
     * @brief 从单个 JAR 提取注册表数据（解析 lang 翻译文件）
     * @param jarPath 版本 JAR 或模组 JAR 路径
     * @return 提取到的条目总数（含合并计数）
     */
    int extractFromJar(const QString &jarPath);

    /**
     * @brief 从游戏目录提取：版本 JAR + mods/ 目录下全部 JAR
     * @param gameDir .minecraft 根目录
     * @param version 游戏版本（如 "1.20.4"）
     * @return 提取到的条目总数
     */
    int extractFromGameDir(const QString &gameDir, const QString &version);

    /**
     * @brief 清空全部已提取数据
     */
    void clear();

    // ---- 提取结果访问（合并到 CommandDatabase / BlockRegistry 使用） ----

    const QList<BlockInfo> &blocks() const { return m_blocks; }
    const QList<ItemInfo> &items() const { return m_items; }
    const QList<EntityInfo> &entities() const { return m_entities; }
    const QList<EffectInfo> &effects() const { return m_effects; }
    const QList<EnchantmentInfo> &enchantments() const { return m_enchantments; }

private:
    /**
     * @brief 解析单个 lang JSON 对象并合并到对应容器
     * @param obj      lang JSON 对象
     * @param added    输出：本次新增/合并条目数
     */
    void applyLangObject(const QJsonObject &obj, int &added);

    QList<BlockInfo> m_blocks;                    ///< 方块（english=minecraft:stone）
    QList<ItemInfo> m_items;                      ///< 物品
    QList<EntityInfo> m_entities;                 ///< 实体（含生物）
    QList<EffectInfo> m_effects;                  ///< 状态效果
    QList<EnchantmentInfo> m_enchantments;        ///< 附魔

    QHash<QString, int> m_blockIndex;             ///< 方块：小写 ID -> 索引
    QHash<QString, int> m_itemIndex;              ///< 物品：小写 ID -> 索引
    QHash<QString, int> m_entityIndex;            ///< 实体：小写 ID -> 索引
    QHash<QString, int> m_effectIndex;            ///< 效果：小写 ID -> 索引
    QHash<QString, int> m_enchantIndex;           ///< 附魔：小写 ID -> 索引
};
