/**
 * @file   BlockRegistry.h
 * @brief  Minecraft 方块注册表
 * @author BlockBox Team
 * @date   2026-07-05
 */

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * @brief 方块中英文映射
 */
struct BlockInfo
{
  QString english;      ///< 英文 ID（如 "minecraft:stone" 或 "stone"）
  QStringList chinese;  ///< 中文名数组（如 ["石头"]）
};

/**
 * @brief 方块注册表
 *
 * 从 Minecraft 版本 JAR 和模组 JAR 中提取方块 ID 及其中文翻译，
 * 供指令补全器在遇到 block 类型参数时提供候选词。
 *
 * 提取原理：
 *   1. JAR 内 assets/<namespace>/blockstates/<block_id>.json 的文件名即方块 ID
 *   2. JAR 内 assets/<namespace>/lang/zh_cn.json 包含中文翻译，
 *      键格式为 "block.<namespace>.<block_id>"
 */
class BlockRegistry
{
public:
  /**
   * @brief 从单个 JAR 文件加载方块定义
   * @param jarPath JAR 文件路径
   * @return 成功加载的方块数量
   */
  int loadFromJar(const QString &jarPath);

  /**
   * @brief 从游戏目录加载（版本 JAR + mods 目录）
   * @param gameDir  .minecraft 根目录
   * @param version  游戏版本（如 "1.20.4"）
   * @return 成功加载的方块数量
   */
  int loadFromGameDir(const QString &gameDir, const QString &version);

  /**
   * @brief 清空已加载的方块列表
   */
  void clear();

  /**
   * @brief 按英文 ID（大小写不敏感）合并一个方块，已存在则合并中文名
   *
   * 用于将 GameRegistry 从 lang 文件提取的方块数据合并进来，
   * 与 blockstates 方式提取的数据互补。
   *
   * @param block 待合并方块
   * @return 新增返回 true；已存在（仅合并别名）返回 false
   */
  bool addBlock(const BlockInfo &block);

  /**
   * @brief 重建前缀分桶索引（由 loadFromJar / clear 自动维护，通常无需手动调用）
   */
  void rebuildIndexes();

  /**
   * @brief 按前缀匹配方块（支持英文 ID 和中文名前缀）
   * @param keyword 用户输入的关键词
   * @return 匹配的方块列表
   */
  QList<BlockInfo> findBlocksByPrefix(const QString &keyword) const;

  /**
   * @brief 获取所有已加载的方块
   * @return 方块列表的 const 引用
   */
  const QList<BlockInfo> &allBlocks() const;

private:
  QList<BlockInfo> m_blocks;
  QHash<QString, int> m_englishIndex;        ///< 英文 ID 小写 -> m_blocks 索引
  QHash<QChar, QList<int>> m_prefixBucket;   ///< 首字符（小写）-> 条目索引（英文 ID + 中文名），前缀查询只扫首字符桶
};
