/**
 * @file   ModScanner.cpp
 * @brief  本地模组扫描器实现
 * @author BlockBox Team
 * @date   2026-06-05
 *
 * 性能优化说明：
 * - 使用 JarUtils::extractMultipleFromJarToMemory() 批量提取元数据，一次 ZIP 遍历获取所有条目
 * - 使用 QtConcurrent::mapped() 多线程并行扫描 JAR 文件
 * - 基于文件修改时间的缓存，避免重复扫描未变更的模组
 */

#include "ModScanner.h"
#include "MurmurHash2.h"

#include "../JarUtils.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QtConcurrent>

// 需要从 JAR 中提取的元数据条目列表（按优先级排列）
static const QStringList METADATA_ENTRIES = {
  "fabric.mod.json",
  "META-INF/mods.toml",
  "mcmod.info",
  "META-INF/MANIFEST.MF"
};

ModScanner::ModScanner(QObject *parent)
  : QObject(parent)
{
  // 预加载 zlib，避免扫描时首次加载
  JarUtils::ensureZlibLoaded();
}

ModScanner::~ModScanner()
{
}

void ModScanner::scanMods(const QString &instancePath)
{
  QtConcurrent::run([this, instancePath]()
  {
    LocalModList result;
    scanModsSync(instancePath, result);
    emit scanCompleted(result);
  });
}

bool ModScanner::scanModsSync(const QString &instancePath, LocalModList &result)
{
  result.mods.clear();
  result.enabledCount = 0;
  result.disabledCount = 0;
  result.totalCount = 0;

  const QString modsDir = instancePath + "/mods";
  QDir dir(modsDir);
  if (!dir.exists())
  {
    qDebug() << "ModScanner: mods directory not found:" << modsDir;
    return true;
  }

  // ========== 第一步：收集所有 JAR 文件信息 ==========
  struct JarEntry
  {
    QString filePath;
    QString fileName;
    bool enabled;
  };

  QVector<JarEntry> jarEntries;

  QStringList nameFilters;
  nameFilters << "*.jar" << "*.jar.disabled";

  QDirIterator it(modsDir, nameFilters, QDir::Files, QDirIterator::NoIteratorFlags);
  while (it.hasNext())
  {
    it.next();
    QFileInfo fileInfo = it.fileInfo();

    JarEntry entry;
    entry.filePath = fileInfo.absoluteFilePath();
    entry.fileName = fileInfo.fileName();
    entry.enabled = !entry.fileName.endsWith(".jar.disabled");

    jarEntries.append(entry);
  }

  if (jarEntries.isEmpty())
  {
    return true;
  }

  const int totalCount = jarEntries.size();

  // ========== 第二步：使用 QtConcurrent::mapped 并行扫描 ==========
  // 使用 blockingMapped 确保线程安全地收集结果

  QVector<ModInfo> modInfos = QtConcurrent::blockingMapped<QVector<ModInfo>>(
    jarEntries,
    [this](const JarEntry &entry) -> ModInfo
    {
      return scanSingleModFast(entry.filePath, entry.fileName);
    }
  );

  // ========== 第三步：汇总结果 ==========
  result.mods.reserve(modInfos.size());

  for (int i = 0; i < modInfos.size() && i < jarEntries.size(); ++i)
  {
    ModInfo mod = modInfos[i];
    mod.enabled = jarEntries[i].enabled;

    // 如果未解析出名称，使用文件名
    if (mod.name.isEmpty())
    {
      mod.name = jarEntries[i].fileName;
    }

    result.mods.append(mod);
  }

  // 按名称字母排序（不区分大小写）
  std::sort(result.mods.begin(), result.mods.end(),
            [](const ModInfo &a, const ModInfo &b)
            {
              return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
            });

  // 统计启用/禁用/总数
  for (const auto &mod : qAsConst(result.mods))
  {
    if (mod.enabled)
    {
      ++result.enabledCount;
    }
    else
    {
      ++result.disabledCount;
    }
  }
  result.totalCount = result.mods.size();

  emit scanProgress(result.totalCount, totalCount);

  return true;
}

void ModScanner::clearCache()
{
  QMutexLocker locker(&m_cacheMutex);
  m_cache.clear();
}

ModInfo ModScanner::scanSingleModFast(const QString &filePath, const QString &fileName)
{
  ModInfo info;
  info.fileName = fileName;
  info.filePath = filePath;

  // ========== 缓存检查：基于文件修改时间 ==========
  QFileInfo fileInfo(filePath);
  QDateTime currentModified = fileInfo.lastModified();
  qint64 currentSize = fileInfo.size();

  {
    QMutexLocker locker(&m_cacheMutex);
    auto cacheIt = m_cache.find(filePath);
    if (cacheIt != m_cache.end()
        && cacheIt->lastModified == currentModified
        && cacheIt->modInfo.fileSize == currentSize)
    {
      // 缓存命中，仅更新可变字段
      ModInfo cached = cacheIt->modInfo;
      cached.fileName = fileName;
      cached.filePath = filePath;
      return cached;
    }
  }

  // ========== 批量提取所有元数据条目（一次 ZIP 遍历） ==========
  QMap<QString, QByteArray> extracted;
  int extractedCount = JarUtils::extractMultipleFromJarToMemory(
    filePath, METADATA_ENTRIES, extracted);

  if (extractedCount > 0)
  {
    parseFromExtractedData(extracted, info);
  }

  // 兜底：如果什么都没解析出来，使用文件名
  if (info.name.isEmpty())
  {
    info.name = fileName;
  }

  info.fileSize = currentSize;

  // ========== 计算文件哈希（用于 CurseForge/Modrinth 指纹匹配） ==========
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly))
  {
    QByteArray fileData = file.readAll();
    file.close();

    // SHA-1: Modrinth 指纹匹配用
    info.sha1Hash = QString::fromLatin1(QCryptographicHash::hash(fileData, QCryptographicHash::Sha1).toHex());

    // MurmurHash2: CurseForge 指纹匹配用
    info.curseforgeHash = MurmurHash2::curseforgeFingerprint(fileData);
  }

  // ========== 写入缓存 ==========
  CachedModInfo cached;
  cached.lastModified = currentModified;
  cached.modInfo = info;
  {
    QMutexLocker locker(&m_cacheMutex);
    m_cache[filePath] = cached;
  }

  return info;
}

bool ModScanner::parseFromExtractedData(const QMap<QString, QByteArray> &extracted,
                                        ModInfo &info)
{
  // 按优先级尝试每种元数据格式
  for (const QString &entryPath : METADATA_ENTRIES)
  {
    auto it = extracted.find(entryPath);
    if (it == extracted.end())
    {
      continue;
    }

    QString content = QString::fromUtf8(it.value());
    if (content.isEmpty())
    {
      continue;
    }

    bool parsed = false;

    if (entryPath == "fabric.mod.json")
    {
      parsed = parseFabricModJson(content, info);
    }
    else if (entryPath == "META-INF/mods.toml")
    {
      parsed = parseModsToml(content, info);
    }
    else if (entryPath == "mcmod.info")
    {
      parsed = parseMcmodInfo(content, info);
    }
    else if (entryPath == "META-INF/MANIFEST.MF")
    {
      parsed = parseManifest(content, info);
    }

    if (parsed)
    {
      return true;
    }
  }

  return false;
}

// ========== 以下解析函数保持不变 ==========

bool ModScanner::parseFabricModJson(const QString &content, ModInfo &info)
{
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError)
  {
    qDebug() << "ModScanner: fabric.mod.json parse error:" << parseError.errorString();
    return false;
  }

  QJsonObject root = doc.object();
  if (root.isEmpty())
  {
    return false;
  }

  // id
  if (root.contains("id"))
  {
    info.id = root.value("id").toString();
  }

  // name
  if (root.contains("name"))
  {
    info.name = root.value("name").toString();
  }

  // version
  if (root.contains("version"))
  {
    info.latestVersion = root.value("version").toString();
  }

  // description
  if (root.contains("description"))
  {
    info.description = root.value("description").toString();
  }

  // authors - 可能是字符串或数组
  if (root.contains("authors"))
  {
    QJsonValue authorsVal = root.value("authors");
    if (authorsVal.isString())
    {
      info.author = authorsVal.toString();
    }
    else if (authorsVal.isArray())
    {
      QJsonArray authorsArr = authorsVal.toArray();
      QStringList authorList;
      for (const auto &a : authorsArr)
      {
        if (a.isObject())
        {
          QString name = a.toObject().value("name").toString();
          if (!name.isEmpty())
          {
            authorList.append(name);
          }
        }
        else if (a.isString())
        {
          authorList.append(a.toString());
        }
      }
      info.author = authorList.join(", ");
    }
  }

  // icon 路径
  if (root.contains("icon"))
  {
    info.iconUrl = root.value("icon").toString();
  }

  // contact 对象（homepage, issues, sources）
  if (root.contains("contact") && root.value("contact").isObject())
  {
    QJsonObject contact = root.value("contact").toObject();
    if (contact.contains("homepage"))
    {
      info.homepageUrl = contact.value("homepage").toString();
    }
    if (contact.contains("issues"))
    {
      info.issuesUrl = contact.value("issues").toString();
    }
    else if (contact.contains("sources"))
    {
      info.issuesUrl = contact.value("sources").toString();
    }
  }

  info.loaderType = "fabric";

  // 如果 name 为空，使用 id
  if (info.name.isEmpty() && !info.id.isEmpty())
  {
    info.name = info.id;
  }

  return !info.id.isEmpty() || !info.name.isEmpty();
}

bool ModScanner::parseModsToml(const QString &content, ModInfo &info)
{
  // 简单 TOML 解析：查找 [[mods]] 表格并提取键值对
  const QStringList lines = content.split('\n');

  int modsSectionStart = -1;
  for (int i = 0; i < lines.size(); ++i)
  {
    QString trimmed = lines[i].trimmed();
    if (trimmed == "[[mods]]")
    {
      modsSectionStart = i + 1;
      break;
    }
  }

  if (modsSectionStart < 0)
  {
    return false;
  }

  for (int i = modsSectionStart; i < lines.size(); ++i)
  {
    QString line = lines[i].trimmed();

    // 遇到下一个 section 则停止
    if (line.startsWith('['))
    {
      break;
    }

    // 跳过空行和注释
    if (line.isEmpty() || line.startsWith('#'))
    {
      continue;
    }

    // 解析 key = value
    int eqIdx = line.indexOf('=');
    if (eqIdx < 0)
    {
      continue;
    }

    QString key = line.left(eqIdx).trimmed();
    QString value = line.mid(eqIdx + 1).trimmed();

    // 去除引号
    if (value.startsWith('"') && value.endsWith('"'))
    {
      value = value.mid(1, value.length() - 2);
    }

    if (key == "modId")
    {
      info.id = value;
    }
    else if (key == "displayName")
    {
      info.name = value;
    }
    else if (key == "version")
    {
      info.latestVersion = value;
    }
    else if (key == "description")
    {
      info.description = value;
    }
    else if (key == "authors")
    {
      info.author = value;
    }
    else if (key == "author")
    {
      info.author = value;
    }
    else if (key == "displayURL")
    {
      info.homepageUrl = value;
    }
    else if (key == "issueTrackerURL")
    {
      info.issuesUrl = value;
    }
  }

  info.loaderType = "neoforge";

  // 如果 name 为空，使用 id 或 displayName
  if (info.name.isEmpty() && !info.id.isEmpty())
  {
    info.name = info.id;
  }

  return !info.id.isEmpty() || !info.name.isEmpty();
}

bool ModScanner::parseMcmodInfo(const QString &content, ModInfo &info)
{
  // mcmod.info 是 JSON 数组
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError)
  {
    qDebug() << "ModScanner: mcmod.info parse error:" << parseError.errorString();
    return false;
  }

  QJsonArray rootArray;
  if (doc.isArray())
  {
    rootArray = doc.array();
  }
  else if (doc.isObject())
  {
    // 某些情况下可能是对象而非数组
    rootArray.append(doc.object());
  }
  else
  {
    return false;
  }

  if (rootArray.isEmpty())
  {
    return false;
  }

  QJsonObject modObj = rootArray.first().toObject();

  if (modObj.contains("modid"))
  {
    info.id = modObj.value("modid").toString();
  }

  if (modObj.contains("name"))
  {
    info.name = modObj.value("name").toString();
  }

  if (modObj.contains("version"))
  {
    info.latestVersion = modObj.value("version").toString();
  }

  if (modObj.contains("description"))
  {
    info.description = modObj.value("description").toString();
  }

  // author 可能是字符串或数组
  if (modObj.contains("authors"))
  {
    QJsonValue authorsVal = modObj.value("authors");
    if (authorsVal.isString())
    {
      info.author = authorsVal.toString();
    }
    else if (authorsVal.isArray())
    {
      QStringList authorList;
      for (const auto &a : authorsVal.toArray())
      {
        authorList.append(a.toString());
      }
      info.author = authorList.join(", ");
    }
  }
  else if (modObj.contains("authorList"))
  {
    QJsonValue authorsVal = modObj.value("authorList");
    if (authorsVal.isArray())
    {
      QStringList authorList;
      for (const auto &a : authorsVal.toArray())
      {
        authorList.append(a.toString());
      }
      info.author = authorList.join(", ");
    }
  }
  else if (modObj.contains("author"))
  {
    info.author = modObj.value("author").toString();
  }

  if (modObj.contains("logoFile"))
  {
    info.iconUrl = modObj.value("logoFile").toString();
  }

  if (modObj.contains("url"))
  {
    info.homepageUrl = modObj.value("url").toString();
  }

  info.loaderType = "forge";

  if (info.name.isEmpty() && !info.id.isEmpty())
  {
    info.name = info.id;
  }

  return !info.id.isEmpty() || !info.name.isEmpty();
}

bool ModScanner::parseManifest(const QString &content, ModInfo &info)
{
  const QStringList lines = content.split('\n');
  for (const QString &line : lines)
  {
    if (line.startsWith("Implementation-Title:"))
    {
      QString title = line.mid(QString("Implementation-Title:").length()).trimmed();
      if (!title.isEmpty())
      {
        info.name = title;
      }
    }
    else if (line.startsWith("Implementation-Version:"))
    {
      QString version = line.mid(QString("Implementation-Version:").length()).trimmed();
      if (!version.isEmpty())
      {
        info.latestVersion = version;
      }
    }
  }

  // 如果标题为空，使用文件名
  if (info.name.isEmpty())
  {
    info.name = info.fileName;
  }

  // loaderType 留空（无法确定）
  info.loaderType.clear();

  return true;
}