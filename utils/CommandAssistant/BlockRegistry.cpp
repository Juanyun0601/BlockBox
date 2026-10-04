/**
 * @file   BlockRegistry.cpp
 * @brief  Minecraft 方块注册表实现
 * @author BlockBox Team
 * @date   2026-07-05
 *
 * 实现细节：
 *   1. loadFromJar() 通过 JarUtils::listEntriesInJar 列出 assets/ 下所有条目，
 *      先扫描 assets/<ns>/blockstates/<id>.json 收集方块定义，
 *      再扫描 assets/<ns>/lang/zh_cn.json 加载中文翻译，
 *      最后为每个方块构造 BlockInfo 并去重入库。
 *   2. loadFromGameDir() 依次加载版本 JAR 与 mods/ 目录下的全部 JAR。
 *   3. findBlocksByPrefix() 按英文 ID 或中文名前缀做大小写不敏感匹配，
 *      结果上限 50 条，避免补全列表过长。
 */

#include "BlockRegistry.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "utils/JarUtils.h"

namespace {

/**
 * @brief 从 JAR 条目中扫描出的方块定义（命名空间 + 方块 ID）
 */
struct BlockEntry
{
  QString ns;       ///< 命名空间（如 "minecraft"）
  QString blockId;  ///< 方块 ID（如 "stone"）
};

} // namespace

// ============================================================================
// 公开接口实现
// ============================================================================

int BlockRegistry::loadFromJar(const QString &jarPath)
{
  // 1. 列出 assets/ 下所有条目
  QStringList entries = JarUtils::listEntriesInJar(jarPath, QStringLiteral("assets/"));
  if (entries.isEmpty())
  {
    return 0;
  }

  // 2. 第一遍扫描：收集 blockstates 方块定义与 zh_cn.json 语言文件路径
  QList<BlockEntry> blockEntries;
  QHash<QString, QString> langEntryPaths; // namespace -> assets/<ns>/lang/zh_cn.json

  for (const QString &entry : entries)
  {
    QStringList parts = entry.split(QLatin1Char('/'));
    if (parts.size() < 4)
    {
      continue;
    }
    if (parts[0] != QStringLiteral("assets"))
    {
      continue;
    }

    const QString &ns = parts[1];

    if (parts[2] == QStringLiteral("blockstates")
      && entry.endsWith(QStringLiteral(".json")))
    {
      QString fileName = parts.last();
      // 去掉 ".json" 后缀（5 个字符）
      QString blockId = fileName.left(fileName.length() - 5);
      blockEntries.append({ns, blockId});
    }
    else if (parts[2] == QStringLiteral("lang")
      && parts.last() == QStringLiteral("zh_cn.json"))
    {
      langEntryPaths.insert(ns, entry);
    }
  }

  if (blockEntries.isEmpty())
  {
    return 0;
  }

  // 3. 提取并解析各命名空间的 zh_cn.json，构建翻译表
  QHash<QString, QJsonObject> langObjects; // namespace -> zh_cn.json 解析结果
  for (auto it = langEntryPaths.constBegin(); it != langEntryPaths.constEnd(); ++it)
  {
    QByteArray data;
    if (!JarUtils::extractFromJarToMemory(jarPath, it.value(), data))
    {
      qWarning() << "[BlockRegistry]" << "Failed to extract lang file:" << it.value();
      continue;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
      qWarning() << "[BlockRegistry]" << "Lang JSON parse error:"
                 << parseError.errorString() << "in" << it.value();
      continue;
    }
    langObjects.insert(it.key(), doc.object());
  }

  // 4. 为每个方块构造 BlockInfo 并加入注册表（按英文 ID 小写去重）
  int addedCount = 0;
  for (const BlockEntry &entry : blockEntries)
  {
    QString fullId = entry.ns + QLatin1Char(':') + entry.blockId;
    QString lowerId = fullId.toLower();

    // 跳过已存在的方块
    if (m_englishIndex.contains(lowerId))
    {
      continue;
    }

    BlockInfo info;
    info.english = fullId;

    // 查找中文翻译，键格式为 "block.<namespace>.<block_id>"
    auto langIt = langObjects.constFind(entry.ns);
    if (langIt != langObjects.constEnd())
    {
      QString langKey = QStringLiteral("block.")
                      + entry.ns
                      + QLatin1Char('.')
                      + entry.blockId;
      QJsonValue langValue = langIt.value().value(langKey);
      if (langValue.isString())
      {
        info.chinese.append(langValue.toString());
      }
    }

    m_englishIndex.insert(lowerId, m_blocks.size());
    m_blocks.append(info);
    ++addedCount;
  }

  // 数据更新后重建前缀分桶索引
  rebuildIndexes();

  return addedCount;
}

int BlockRegistry::loadFromGameDir(const QString &gameDir, const QString &version)
{
  int totalCount = 0;

  // 1. 版本 JAR：{gameDir}/versions/{version}/{version}.jar
  QString versionJar = gameDir
                     + QStringLiteral("/versions/")
                     + version
                     + QStringLiteral("/")
                     + version
                     + QStringLiteral(".jar");
  if (QFile::exists(versionJar))
  {
    totalCount += loadFromJar(versionJar);
  }

  // 2. mods 目录下的所有 JAR
  QString modsPath = gameDir + QStringLiteral("/mods");
  QDir modsDir(modsPath);
  if (modsDir.exists())
  {
    QStringList jarFilters;
    jarFilters << QStringLiteral("*.jar");
    QStringList modJars = modsDir.entryList(jarFilters, QDir::Files);
    for (const QString &modJar : modJars)
    {
      QString modJarPath = modsDir.absoluteFilePath(modJar);
      totalCount += loadFromJar(modJarPath);
    }
  }

  return totalCount;
}

void BlockRegistry::clear()
{
  m_blocks.clear();
  m_englishIndex.clear();
  m_prefixBucket.clear();
}

bool BlockRegistry::addBlock(const BlockInfo &block)
{
  if (block.english.isEmpty())
  {
    return false;
  }
  const QString lowerId = block.english.toLower();
  const int idx = m_englishIndex.value(lowerId, -1);
  if (idx >= 0 && idx < m_blocks.size())
  {
    // 已存在：合并中文名（去重）
    BlockInfo &dst = m_blocks[idx];
    for (const QString &cn : block.chinese)
    {
      bool exists = false;
      for (const QString &existing : dst.chinese)
      {
        if (existing.compare(cn, Qt::CaseInsensitive) == 0)
        {
          exists = true;
          break;
        }
      }
      if (!exists)
      {
        dst.chinese.append(cn);
      }
    }
    return false;
  }

  m_englishIndex.insert(lowerId, m_blocks.size());
  m_blocks.append(block);
  return true;
}

void BlockRegistry::rebuildIndexes()
{
  m_prefixBucket.clear();
  for (int i = 0; i < m_blocks.size(); ++i)
  {
    const BlockInfo &block = m_blocks.at(i);
    // 英文 ID 首字符（如 "minecraft:stone" → 'm'）
    if (!block.english.isEmpty())
    {
      m_prefixBucket[block.english.at(0).toLower()].append(i);
    }
    // 每个中文别名首字符各进一次桶
    for (const QString &cn : block.chinese)
    {
      if (!cn.isEmpty())
      {
        m_prefixBucket[cn.at(0).toLower()].append(i);
      }
    }
  }
}

QList<BlockInfo> BlockRegistry::findBlocksByPrefix(const QString &keyword) const
{
  QList<BlockInfo> results;
  const int kMaxResults = 50;

  if (keyword.isEmpty())
  {
    // 关键词为空时返回前 50 个方块，避免补全列表过长
    int limit = qMin(kMaxResults, static_cast<int>(m_blocks.size()));
    for (int i = 0; i < limit; ++i)
    {
      results.append(m_blocks[i]);
    }
    return results;
  }

  // 首字符桶（大小写不敏感）：只需扫描同首字符条目，避免全量线性扫描
  const auto bucketIt = m_prefixBucket.constFind(keyword.at(0).toLower());
  if (bucketIt == m_prefixBucket.constEnd())
  {
    return results;
  }

  for (int idx : bucketIt.value())
  {
    const BlockInfo &block = m_blocks.at(idx);

    bool matched = block.english.startsWith(keyword, Qt::CaseInsensitive);
    if (!matched)
    {
      for (const QString &cn : block.chinese)
      {
        if (cn.startsWith(keyword, Qt::CaseInsensitive))
        {
          matched = true;
          break;
        }
      }
    }

    if (matched)
    {
      results.append(block);
      if (results.size() >= kMaxResults)
      {
        break;
      }
    }
  }

  return results;
}

const QList<BlockInfo> &BlockRegistry::allBlocks() const
{
  return m_blocks;
}
