/**
 * @file   ModpackExporter.cpp
 * @brief  整合包导出器类实现
 * @author BlockBox Team
 * @date   2026-06-19
 */

#include "ModpackExporter.h"

#include <QDebug>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QSet>

#include <zlib.h>

namespace modpack {

// ============================================================================
// Anonymous-namespace ZipWriter - minimal ZIP file creator using zlib
// ============================================================================

namespace {

// Forward declaration
QByteArray compressDeflate(const QByteArray& data);

// Local file header signature
static constexpr quint32 LOCAL_FILE_HEADER_SIG = 0x04034b50;
// Central directory entry signature
static constexpr quint32 CENTRAL_DIR_ENTRY_SIG = 0x02014b50;
// End of central directory signature
static constexpr quint32 EOCD_SIG = 0x06054b50;

// DOS date/time packing helpers
static quint32 dosDateTime()
{
  QDateTime now = QDateTime::currentDateTime();
  QDate d = now.date();
  QTime t = now.time();
  quint16 dosDate = static_cast<quint16>(
    ((d.year() - 1980) << 9) | (d.month() << 5) | d.day());
  quint16 dosTime = static_cast<quint16>(
    (t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
  return (static_cast<quint32>(dosTime) << 16) | dosDate;
}

struct CentralDirEntry
{
  QString fileName;
  quint32 localHeaderOffset = 0;
  quint32 crc32 = 0;
  quint32 compressedSize = 0;
  quint32 uncompressedSize = 0;
  quint16 compressionMethod = 0;
};

class ZipWriter
{
public:
  bool open(const QString& filePath);
  bool addFile(const QString& zipPath, const QByteArray& data, bool compress = true);
  bool addDirectory(const QString& zipPath);
  bool close();

private:
  QFile m_file;
  QList<CentralDirEntry> m_entries;
  QSet<QString> m_addedDirs;
  quint32 m_cdOffset = 0;
};

bool ZipWriter::open(const QString& filePath)
{
  m_file.setFileName(filePath);
  if (!m_file.open(QIODevice::WriteOnly))
    return false;

  m_entries.clear();
  m_addedDirs.clear();
  m_cdOffset = 0;
  return true;
}

bool ZipWriter::addFile(const QString& zipPath, const QByteArray& data, bool compress)
{
  if (!m_file.isOpen())
    return false;

  // ZIP 条目大小字段为 32 位，超 4GB 会被截断导致归档损坏。
  // 当前实现不支持 ZIP64，故拒绝超大单文件，避免静默生成损坏的压缩包。
  if (data.size() > 0xFFFFFFFFLL) {
    qWarning() << "[ModpackExporter] 文件超出 ZIP 4GB 上限，无法打包:" << zipPath
               << data.size() << "bytes";
    return false;
  }

  QByteArray fileNameBytes = zipPath.toUtf8();
  QByteArray compressed;
  quint16 method = 0;
  quint32 crc = 0;
  quint32 uncompressedSize = static_cast<quint32>(data.size());
  quint32 compressedSize = 0;

  crc = crc32(0, reinterpret_cast<const Bytef*>(data.constData()),
              static_cast<uInt>(data.size()));

  if (compress)
  {
    compressed = compressDeflate(data);
    if (compressed.size() < data.size())
    {
      method = 8;
      compressedSize = static_cast<quint32>(compressed.size());
    }
    else
    {
      // Compression didn't help, store uncompressed
      compressed = data;
      method = 0;
      compressedSize = uncompressedSize;
    }
  }
  else
  {
    compressed = data;
    method = 0;
    compressedSize = uncompressedSize;
  }

  quint32 headerOffset = static_cast<quint32>(m_file.pos());

  // Write local file header
  // GP bit 11 (0x800)：文件名是 UTF-8 编码，否则中文名在资源管理器里乱码
  QByteArray localHeader;
  {
    QDataStream ds(&localHeader, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << LOCAL_FILE_HEADER_SIG;
    ds << static_cast<quint16>(20);  // version needed (2.0)
    ds << static_cast<quint16>(0x800); // UTF-8 filename flag
    ds << method;
    ds << dosDateTime();
    ds << crc;
    ds << compressedSize;
    ds << uncompressedSize;
    ds << static_cast<quint16>(fileNameBytes.size());
    ds << static_cast<quint16>(0);   // extra field length
  }
  m_file.write(localHeader);
  m_file.write(fileNameBytes);

  // Write file data
  m_file.write(compressed);

  // Record central directory entry
  CentralDirEntry entry;
  entry.fileName = zipPath;
  entry.localHeaderOffset = headerOffset;
  entry.crc32 = crc;
  entry.compressedSize = compressedSize;
  entry.uncompressedSize = uncompressedSize;
  entry.compressionMethod = method;
  m_entries.append(entry);

  return true;
}

bool ZipWriter::addDirectory(const QString& zipPath)
{
  QString dirPath = zipPath;
  if (!dirPath.endsWith('/'))
    dirPath += '/';

  // 同名目录只写一次，避免每个文件都重复添加父目录导致条目膨胀
  if (m_addedDirs.contains(dirPath))
    return true;
  m_addedDirs.insert(dirPath);

  QByteArray fileNameBytes = dirPath.toUtf8();

  quint32 headerOffset = static_cast<quint32>(m_file.pos());

  // Write local file header for directory
  QByteArray localHeader;
  {
    QDataStream ds(&localHeader, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << LOCAL_FILE_HEADER_SIG;
    ds << static_cast<quint16>(20);
    ds << static_cast<quint16>(0x800);
    ds << static_cast<quint16>(0);   // stored
    ds << dosDateTime();
    ds << static_cast<quint32>(0);   // crc32
    ds << static_cast<quint32>(0);   // compressed size
    ds << static_cast<quint32>(0);   // uncompressed size
    ds << static_cast<quint16>(fileNameBytes.size());
    ds << static_cast<quint16>(0);
  }
  m_file.write(localHeader);
  m_file.write(fileNameBytes);

  CentralDirEntry entry;
  entry.fileName = dirPath;
  entry.localHeaderOffset = headerOffset;
  entry.crc32 = 0;
  entry.compressedSize = 0;
  entry.uncompressedSize = 0;
  entry.compressionMethod = 0;
  m_entries.append(entry);

  return true;
}

bool ZipWriter::close()
{
  if (!m_file.isOpen())
    return false;

  quint32 cdStartOffset = static_cast<quint32>(m_file.pos());

  // Write central directory entries
  for (const auto& entry : m_entries)
  {
    QByteArray fileNameBytes = entry.fileName.toUtf8();

    QByteArray cdEntry;
    {
      QDataStream ds(&cdEntry, QIODevice::WriteOnly);
      ds.setByteOrder(QDataStream::LittleEndian);
      ds << CENTRAL_DIR_ENTRY_SIG;
      ds << static_cast<quint16>(20);  // version made by
      ds << static_cast<quint16>(20);  // version needed
      ds << static_cast<quint16>(0x800); // UTF-8 filename flag
      ds << entry.compressionMethod;
      ds << dosDateTime();
      ds << entry.crc32;
      ds << entry.compressedSize;
      ds << entry.uncompressedSize;
      ds << static_cast<quint16>(fileNameBytes.size());
      ds << static_cast<quint16>(0);   // extra field length
      ds << static_cast<quint16>(0);   // file comment length
      ds << static_cast<quint16>(0);   // disk number start
      ds << static_cast<quint16>(0);   // internal attrs
      ds << static_cast<quint32>(0);   // external attrs
      ds << entry.localHeaderOffset;
    }
    m_file.write(cdEntry);
    m_file.write(fileNameBytes);
  }

  quint32 cdSize = static_cast<quint32>(m_file.pos()) - cdStartOffset;

  // Write EOCD
  QByteArray eocd;
  {
    QDataStream ds(&eocd, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << EOCD_SIG;
    ds << static_cast<quint16>(0);    // disk number
    ds << static_cast<quint16>(0);    // disk with CD
    ds << static_cast<quint16>(m_entries.size());  // entries on disk
    ds << static_cast<quint16>(m_entries.size());  // total entries
    ds << cdSize;
    ds << cdStartOffset;
    ds << static_cast<quint16>(0);    // comment length
  }
  m_file.write(eocd);

  m_file.close();
  return true;
}

QByteArray compressDeflate(const QByteArray& data)
{
  z_stream strm = {};
  int ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                         -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
  if (ret != Z_OK)
    return QByteArray();

  strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
  strm.avail_in = static_cast<uInt>(data.size());

  const int CHUNK = 16384;
  QByteArray result;
  QByteArray buffer(CHUNK, '\0');

  do
  {
    strm.next_out = reinterpret_cast<Bytef*>(buffer.data());
    strm.avail_out = CHUNK;
    ret = deflate(&strm, Z_FINISH);
    if (ret == Z_STREAM_ERROR)
    {
      deflateEnd(&strm);
      return QByteArray();
    }
    result.append(buffer.data(), CHUNK - strm.avail_out);
  }
  while (strm.avail_out == 0);

  deflateEnd(&strm);
  return result;
}

} // anonymous namespace

// ============================================================================
// ModpackExporter implementation
// ============================================================================

ModpackExporter::ModpackExporter(const QString& instancePath,
                                 ExportFormat exportFormat,
                                 const ModpackExportInfo& exportInfo,
                                 const QStringList& selectedFiles,
                                 const QString& outputPath,
                                 QObject* parent)
  : QObject(parent)
  , m_instancePath(instancePath)
  , m_exportFormat(exportFormat)
  , m_exportInfo(exportInfo)
  , m_selectedFiles(selectedFiles)
  , m_outputPath(outputPath)
{
}

ModpackExporter::~ModpackExporter()
{
}

void ModpackExporter::startExport()
{
  switch (m_exportFormat)
  {
  case ExportFormat::CurseForge:
    exportAsCurseForge();
    break;
  case ExportFormat::MultiMC:
    exportAsMultiMC();
    break;
  case ExportFormat::Server:
    exportAsServer();
    break;
  case ExportFormat::Modrinth:
    exportAsModrinth();
    break;
  case ExportFormat::Mcbbs:
    exportAsMcbbs();
    break;
  case ExportFormat::BlockBox:
    exportAsBlockBox();
    break;
  case ExportFormat::Packwiz:
    exportAsPackwiz();
    break;
  }
}

// ---------------------------------------------------------------------------
// CurseForge export
// ---------------------------------------------------------------------------

void ModpackExporter::exportAsCurseForge()
{
  emit exportProgressChanged(0, tr("正在准备导出 (CurseForge)..."));

  QString gameVersion = detectGameVersion();
  QString loaderType = detectLoaderType();
  QString loaderVersion = detectLoaderVersion();

  // Build manifest.json
  QJsonObject manifest;
  manifest["manifestType"] = QStringLiteral("minecraftModpack");
  manifest["manifestVersion"] = 1;
  manifest["name"] = m_exportInfo.name;
  manifest["version"] = m_exportInfo.version;
  manifest["author"] = m_exportInfo.author;
  manifest["overrides"] = QStringLiteral("overrides");

  // Minecraft section
  QJsonObject minecraft;
  minecraft["version"] = gameVersion;

  QJsonArray modLoaders;
  if (!loaderType.isEmpty())
  {
    QJsonObject loaderObj;
    QString loaderId;
    if (loaderType.toLower() == QStringLiteral("forge"))
      loaderId = QStringLiteral("forge-%1").arg(loaderVersion);
    else if (loaderType.toLower() == QStringLiteral("fabric"))
      loaderId = QStringLiteral("fabric-%1").arg(loaderVersion);
    else if (loaderType.toLower() == QStringLiteral("neoforge"))
      loaderId = QStringLiteral("neoforge-%1").arg(loaderVersion);
    else
      loaderId = QStringLiteral("%1-%2").arg(loaderType, loaderVersion);

    loaderObj["id"] = loaderId;
    loaderObj["primary"] = true;
    modLoaders.append(loaderObj);
  }
  minecraft["modLoaders"] = modLoaders;
  manifest["minecraft"] = minecraft;

  // Files array (empty - no mod hosting URLs)
  manifest["files"] = QJsonArray();

  QJsonDocument manifestDoc(manifest);
  QByteArray manifestData = manifestDoc.toJson(QJsonDocument::Indented);

  emit exportProgressChanged(10, tr("正在创建整合包文件..."));

  // Create zip
  ZipWriter zip;
  if (!zip.open(m_outputPath))
  {
    emit exportFinished(false, QString(), tr("无法创建输出文件：%1").arg(m_outputPath));
    return;
  }

  zip.addFile(QStringLiteral("manifest.json"), manifestData, false);

  // Add selected files under "overrides/"
  if (!m_selectedFiles.isEmpty())
  {
    int total = m_selectedFiles.size();
    int processed = 0;

    // Add overrides directory
    zip.addDirectory(QStringLiteral("overrides"));

    for (const QString& relPath : m_selectedFiles)
    {
      QString fullPath = m_instancePath + "/" + relPath;
      QFile file(fullPath);
      if (!file.open(QIODevice::ReadOnly))
      {
        ++processed;
        continue;
      }

      QByteArray fileData = file.readAll();
      file.close();

      QString zipPath = QStringLiteral("overrides/") + relPath;

      // Ensure parent directories exist in zip（parentDir 已含前缀，勿再拼一次）
      const QString parentDir = QFileInfo(zipPath).path();
      if (!parentDir.isEmpty() && parentDir != QStringLiteral("."))
        zip.addDirectory(parentDir);

      zip.addFile(zipPath, fileData);

      ++processed;
      int percent = 10 + (processed * 80 / total);
      emit exportProgressChanged(percent, tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  if (!zip.close())
  {
    QFile::remove(m_outputPath);
    emit exportFinished(false, QString(), tr("写入压缩包失败，磁盘可能已满"));
    return;
  }
  emit exportProgressChanged(100, tr("导出完成"));
  emit exportFinished(true, m_outputPath, QString());
}

// ---------------------------------------------------------------------------
// MultiMC export
// ---------------------------------------------------------------------------

void ModpackExporter::exportAsMultiMC()
{
  emit exportProgressChanged(0, tr("正在准备导出 (MultiMC)..."));

  QString gameVersion = detectGameVersion();
  QString loaderType = detectLoaderType();
  QString loaderVersion = detectLoaderVersion();

  // Build mmc-pack.json
  QJsonObject mmcPack;
  mmcPack["formatVersion"] = 1;

  QJsonArray components;

  // Minecraft component
  QJsonObject mcComponent;
  mcComponent["cachedName"] = QStringLiteral("Minecraft");
  mcComponent["cachedVersion"] = gameVersion;
  mcComponent["uid"] = QStringLiteral("net.minecraft");
  mcComponent["version"] = gameVersion;
  components.append(mcComponent);

  // Loader component
  if (!loaderType.isEmpty())
  {
    QJsonObject loaderComponent;
    QString loaderUid;
    QString loaderName;

    if (loaderType.toLower() == QStringLiteral("forge"))
    {
      loaderUid = QStringLiteral("net.minecraftforge");
      loaderName = QStringLiteral("Forge");
    }
    else if (loaderType.toLower() == QStringLiteral("fabric"))
    {
      loaderUid = QStringLiteral("net.fabricmc.fabric-loader");
      loaderName = QStringLiteral("Fabric");
    }
    else if (loaderType.toLower() == QStringLiteral("neoforge"))
    {
      loaderUid = QStringLiteral("net.neoforged");
      loaderName = QStringLiteral("NeoForge");
    }
    else
    {
      loaderUid = loaderType;
      loaderName = loaderType;
    }

    loaderComponent["cachedName"] = loaderName;
    loaderComponent["cachedVersion"] = loaderVersion;
    loaderComponent["uid"] = loaderUid;
    loaderComponent["version"] = loaderVersion;
    components.append(loaderComponent);
  }

  mmcPack["components"] = components;

  QJsonDocument mmcDoc(mmcPack);
  QByteArray mmcPackData = mmcDoc.toJson(QJsonDocument::Indented);

  // Build instance.cfg
  QString instanceCfg;
  instanceCfg += QStringLiteral("name=%1\n").arg(m_exportInfo.name);
  instanceCfg += QStringLiteral("iconKey=default\n");
  instanceCfg += QStringLiteral("OverrideMemory=true\n");
  if (m_exportInfo.minMemory > 0)
    instanceCfg += QStringLiteral("MinMemAlloc=%1\n").arg(m_exportInfo.minMemory);
  if (!m_exportInfo.javaArgs.isEmpty())
    instanceCfg += QStringLiteral("JavaArgs=%1\n").arg(m_exportInfo.javaArgs);

  emit exportProgressChanged(10, tr("正在创建整合包文件..."));

  ZipWriter zip;
  if (!zip.open(m_outputPath))
  {
    emit exportFinished(false, QString(), tr("无法创建输出文件：%1").arg(m_outputPath));
    return;
  }

  zip.addFile(QStringLiteral("mmc-pack.json"), mmcPackData, false);
  zip.addFile(QStringLiteral("instance.cfg"), instanceCfg.toUtf8(), false);

  // Add selected files under ".minecraft/"
  if (!m_selectedFiles.isEmpty())
  {
    int total = m_selectedFiles.size();
    int processed = 0;

    zip.addDirectory(QStringLiteral(".minecraft"));

    for (const QString& relPath : m_selectedFiles)
    {
      QString fullPath = m_instancePath + "/" + relPath;
      QFile file(fullPath);
      if (!file.open(QIODevice::ReadOnly))
      {
        ++processed;
        continue;
      }

      QByteArray fileData = file.readAll();
      file.close();

      QString zipPath = QStringLiteral(".minecraft/") + relPath;

      // Ensure parent directories exist in zip（parentDir 已含前缀，勿再拼一次）
      const QString parentDir = QFileInfo(zipPath).path();
      if (!parentDir.isEmpty() && parentDir != QStringLiteral("."))
        zip.addDirectory(parentDir);

      zip.addFile(zipPath, fileData);

      ++processed;
      int percent = 10 + (processed * 80 / total);
      emit exportProgressChanged(percent, tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  if (!zip.close())
  {
    QFile::remove(m_outputPath);
    emit exportFinished(false, QString(), tr("写入压缩包失败，磁盘可能已满"));
    return;
  }
  emit exportProgressChanged(100, tr("导出完成"));
  emit exportFinished(true, m_outputPath, QString());
}

// ---------------------------------------------------------------------------
// Server export
// ---------------------------------------------------------------------------

void ModpackExporter::exportAsServer()
{
  emit exportProgressChanged(0, tr("正在准备导出 (Server)..."));

  // Build server-manifest.json
  QJsonObject serverManifest;
  serverManifest["name"] = m_exportInfo.name;
  serverManifest["author"] = m_exportInfo.author;
  serverManifest["version"] = m_exportInfo.version;
  serverManifest["description"] = m_exportInfo.description;
  serverManifest["fileApi"] = m_exportInfo.fileApi;

  // Calculate SHA-1 for each file
  QJsonArray filesArray;
  if (!m_selectedFiles.isEmpty())
  {
    int total = m_selectedFiles.size();
    int processed = 0;

    for (const QString& relPath : m_selectedFiles)
    {
      QString fullPath = m_instancePath + "/" + relPath;
      QByteArray sha1 = calculateSha1(fullPath);
      if (sha1.isEmpty())
      {
        ++processed;
        continue;
      }

      QJsonObject fileObj;
      fileObj["path"] = relPath;
      fileObj["hash"] = QString::fromLatin1(sha1.toHex());
      filesArray.append(fileObj);

      ++processed;
      int percent = processed * 30 / total;
      emit exportProgressChanged(percent, tr("正在计算文件哈希 (%1/%2)...").arg(processed).arg(total));
    }
  }
  serverManifest["files"] = filesArray;

  // Addons array (empty - no specific addons)
  serverManifest["addons"] = QJsonArray();

  QJsonDocument manifestDoc(serverManifest);
  QByteArray manifestData = manifestDoc.toJson(QJsonDocument::Indented);

  emit exportProgressChanged(30, tr("正在创建整合包文件..."));

  ZipWriter zip;
  if (!zip.open(m_outputPath))
  {
    emit exportFinished(false, QString(), tr("无法创建输出文件：%1").arg(m_outputPath));
    return;
  }

  zip.addFile(QStringLiteral("server-manifest.json"), manifestData, false);

  // Add selected files under "overrides/"
  if (!m_selectedFiles.isEmpty())
  {
    int total = m_selectedFiles.size();
    int processed = 0;

    zip.addDirectory(QStringLiteral("overrides"));

    for (const QString& relPath : m_selectedFiles)
    {
      QString fullPath = m_instancePath + "/" + relPath;
      QFile file(fullPath);
      if (!file.open(QIODevice::ReadOnly))
      {
        ++processed;
        continue;
      }

      QByteArray fileData = file.readAll();
      file.close();

      QString zipPath = QStringLiteral("overrides/") + relPath;

      // Ensure parent directories exist in zip（parentDir 已含前缀，勿再拼一次）
      const QString parentDir = QFileInfo(zipPath).path();
      if (!parentDir.isEmpty() && parentDir != QStringLiteral("."))
        zip.addDirectory(parentDir);

      zip.addFile(zipPath, fileData);

      ++processed;
      int percent = 30 + (processed * 60 / total);
      emit exportProgressChanged(percent, tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  if (!zip.close())
  {
    QFile::remove(m_outputPath);
    emit exportFinished(false, QString(), tr("写入压缩包失败，磁盘可能已满"));
    return;
  }
  emit exportProgressChanged(100, tr("导出完成"));
  emit exportFinished(true, m_outputPath, QString());
}

// ---------------------------------------------------------------------------
// Modrinth export
// ---------------------------------------------------------------------------

void ModpackExporter::exportAsModrinth()
{
  emit exportProgressChanged(0, tr("正在准备导出 (Modrinth)..."));

  QString gameVersion = detectGameVersion();
  QString loaderType = detectLoaderType();
  QString loaderVersion = detectLoaderVersion();

  // 离线导出没有可供下载的模组 URL：把全部选中文件（含 mods/）打进
  // client-overrides/，files 清单留空。导入端解压时剥掉该前缀即可完整还原
  QJsonObject index;
  index["formatVersion"] = 1;
  index["game"] = QStringLiteral("minecraft");
  index["versionId"] = m_exportInfo.version;
  index["name"] = m_exportInfo.name;
  if (!m_exportInfo.description.isEmpty())
    index["summary"] = m_exportInfo.description;
  index["files"] = QJsonArray();

  // Dependencies
  QJsonObject dependencies;
  dependencies["minecraft"] = gameVersion;
  if (!loaderType.isEmpty())
  {
    QString loaderLower = loaderType.toLower();
    if (loaderLower == QStringLiteral("forge"))
      dependencies["forge"] = loaderVersion;
    else if (loaderLower == QStringLiteral("fabric"))
      dependencies["fabric-loader"] = loaderVersion;
    else if (loaderLower == QStringLiteral("neoforge"))
      dependencies["neoforge"] = loaderVersion;
    else if (loaderLower == QStringLiteral("quilt"))
      dependencies["quilt-loader"] = loaderVersion;
  }
  index["dependencies"] = dependencies;

  QJsonDocument indexDoc(index);
  QByteArray indexData = indexDoc.toJson(QJsonDocument::Indented);

  emit exportProgressChanged(10, tr("正在创建整合包文件..."));

  ZipWriter zip;
  if (!zip.open(m_outputPath))
  {
    emit exportFinished(false, QString(), tr("无法创建输出文件：%1").arg(m_outputPath));
    return;
  }

  if (!zip.addFile(QStringLiteral("modrinth.index.json"), indexData, false))
  {
    zip.close();
    QFile::remove(m_outputPath);
    emit exportFinished(false, QString(), tr("写入清单文件失败"));
    return;
  }

  // Add all selected files under "client-overrides/"
  if (!m_selectedFiles.isEmpty())
  {
    int total = m_selectedFiles.size();
    int processed = 0;

    zip.addDirectory(QStringLiteral("client-overrides"));

    for (const QString& relPath : m_selectedFiles)
    {
      QString fullPath = m_instancePath + "/" + relPath;
      QFile file(fullPath);
      if (!file.open(QIODevice::ReadOnly))
      {
        ++processed;
        continue;
      }

      QByteArray fileData = file.readAll();
      file.close();

      QString zipPath = QStringLiteral("client-overrides/") + relPath;

      // Ensure parent directories exist in zip（parentDir 已含前缀，勿再拼一次）
      const QString parentDir = QFileInfo(zipPath).path();
      if (!parentDir.isEmpty() && parentDir != QStringLiteral("."))
        zip.addDirectory(parentDir);

      zip.addFile(zipPath, fileData);

      ++processed;
      int percent = 10 + (processed * 80 / total);
      emit exportProgressChanged(percent, tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  if (!zip.close())
  {
    QFile::remove(m_outputPath);
    emit exportFinished(false, QString(), tr("写入压缩包失败，磁盘可能已满"));
    return;
  }
  emit exportProgressChanged(100, tr("导出完成"));
  emit exportFinished(true, m_outputPath, QString());
}

// ---------------------------------------------------------------------------
// MCBBS export
// ---------------------------------------------------------------------------

void ModpackExporter::exportAsMcbbs()
{
  emit exportProgressChanged(0, tr("正在准备导出 (MCBBS)..."));
  emit exportProgressChanged(0, tr("正在计算文件哈希..."));

  QString gameVersion = detectGameVersion();
  QMap<QString, QString> allLoaders = detectAllLoaders();

  // Calculate SHA-1 for each file
  QJsonArray filesArray;
  if (!m_selectedFiles.isEmpty())
  {
    int total = m_selectedFiles.size();
    int processed = 0;

    for (const QString& relPath : m_selectedFiles)
    {
      QString fullPath = m_instancePath + "/" + relPath;
      QByteArray sha1 = calculateSha1(fullPath);
      if (sha1.isEmpty())
      {
        ++processed;
        continue;
      }

      QJsonObject fileObj;
      fileObj["name"] = QFileInfo(relPath).fileName();
      fileObj["path"] = relPath;
      fileObj["hash"] = QString::fromLatin1(sha1.toHex());
      filesArray.append(fileObj);

      ++processed;
      int percent = processed * 30 / total;
      emit exportProgressChanged(percent, tr("正在计算文件哈希 (%1/%2)...").arg(processed).arg(total));
    }
  }

  emit exportProgressChanged(30, tr("正在创建整合包文件..."));

  // Open zip
  ZipWriter zip;
  if (!zip.open(m_outputPath))
  {
    emit exportFinished(false, QString(), tr("无法创建输出文件：%1").arg(m_outputPath));
    return;
  }

  emit exportProgressChanged(50, tr("正在生成清单..."));

  // Build mcbbs.packmeta
  QJsonObject packmeta;
  packmeta["manifestType"] = QStringLiteral("minecraftModpack");
  packmeta["manifestVersion"] = 2;
  packmeta["name"] = m_exportInfo.name;
  packmeta["version"] = m_exportInfo.version;
  packmeta["author"] = m_exportInfo.author;
  packmeta["description"] = m_exportInfo.description;
  packmeta["fileApi"] = m_exportInfo.fileApi;
  packmeta["url"] = m_exportInfo.url;
  packmeta["forceUpdate"] = m_exportInfo.forceUpdate;

  // Origins
  QJsonArray origins;
  if (!m_exportInfo.fileApi.isEmpty())
  {
    QJsonObject originObj;
    originObj["type"] = QStringLiteral("self");
    originObj["source"] = m_exportInfo.fileApi;
    origins.append(originObj);
  }
  packmeta["origins"] = origins;

  // Addons - detect all loaders
  QJsonArray addons;

  // game
  QJsonObject gameAddon;
  gameAddon["id"] = QStringLiteral("game");
  gameAddon["version"] = gameVersion;
  addons.append(gameAddon);

  // Other loaders
  for (auto it = allLoaders.constBegin(); it != allLoaders.constEnd(); ++it)
  {
    QJsonObject addonObj;
    addonObj["id"] = it.key();
    addonObj["version"] = it.value();
    addons.append(addonObj);
  }
  packmeta["addons"] = addons;

  // Libraries
  packmeta["libraries"] = QJsonArray();

  // Files
  packmeta["files"] = filesArray;

  // Settings
  packmeta["settings"] = QJsonObject();

  // LaunchInfo
  QJsonObject launchInfo;
  launchInfo["minMemory"] = m_exportInfo.minMemory;

  QJsonArray javaArgsArray;
  if (!m_exportInfo.javaArgs.isEmpty())
  {
    QStringList javaArgsList = m_exportInfo.javaArgs.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (const QString& arg : javaArgsList)
      javaArgsArray.append(arg);
  }
  launchInfo["javaArguments"] = javaArgsArray;

  QJsonArray launchArgsArray;
  if (!m_exportInfo.launchArgs.isEmpty())
  {
    QStringList launchArgsList = m_exportInfo.launchArgs.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (const QString& arg : launchArgsList)
      launchArgsArray.append(arg);
  }
  launchInfo["launchArguments"] = launchArgsArray;

  packmeta["launchInfo"] = launchInfo;

  QJsonDocument packmetaDoc(packmeta);
  QByteArray packmetaData = packmetaDoc.toJson(QJsonDocument::Indented);
  zip.addFile(QStringLiteral("mcbbs.packmeta"), packmetaData, false);

  // Build manifest.json (CurseForge compatible, same logic as exportAsCurseForge)
  QJsonObject manifest;
  manifest["manifestType"] = QStringLiteral("minecraftModpack");
  manifest["manifestVersion"] = 1;
  manifest["name"] = m_exportInfo.name;
  manifest["version"] = m_exportInfo.version;
  manifest["author"] = m_exportInfo.author;
  manifest["overrides"] = QStringLiteral("overrides");

  QJsonObject minecraft;
  minecraft["version"] = gameVersion;

  QJsonArray modLoaders;
  if (!allLoaders.isEmpty())
  {
    // Use the first (primary) loader for the manifest
    QString primaryLoader = allLoaders.firstKey();
    QString primaryVersion = allLoaders.first();

    QJsonObject loaderObj;
    QString loaderId;
    if (primaryLoader == QStringLiteral("forge"))
      loaderId = QStringLiteral("forge-%1").arg(primaryVersion);
    else if (primaryLoader == QStringLiteral("fabric"))
      loaderId = QStringLiteral("fabric-%1").arg(primaryVersion);
    else if (primaryLoader == QStringLiteral("neoforge"))
      loaderId = QStringLiteral("neoforge-%1").arg(primaryVersion);
    else
      loaderId = QStringLiteral("%1-%2").arg(primaryLoader, primaryVersion);

    loaderObj["id"] = loaderId;
    loaderObj["primary"] = true;
    modLoaders.append(loaderObj);
  }
  minecraft["modLoaders"] = modLoaders;
  manifest["minecraft"] = minecraft;
  manifest["files"] = QJsonArray();

  QJsonDocument manifestDoc(manifest);
  QByteArray manifestData = manifestDoc.toJson(QJsonDocument::Indented);
  zip.addFile(QStringLiteral("manifest.json"), manifestData, false);

  emit exportProgressChanged(80, tr("正在打包文件..."));

  // Add selected files under "overrides/"
  if (!m_selectedFiles.isEmpty())
  {
    int total = m_selectedFiles.size();
    int processed = 0;

    zip.addDirectory(QStringLiteral("overrides"));

    for (const QString& relPath : m_selectedFiles)
    {
      QString fullPath = m_instancePath + "/" + relPath;
      QFile file(fullPath);
      if (!file.open(QIODevice::ReadOnly))
      {
        ++processed;
        continue;
      }

      QByteArray fileData = file.readAll();
      file.close();

      QString zipPath = QStringLiteral("overrides/") + relPath;

      // Ensure parent directories exist in zip（parentDir 已含前缀，勿再拼一次）
      const QString parentDir = QFileInfo(zipPath).path();
      if (!parentDir.isEmpty() && parentDir != QStringLiteral("."))
        zip.addDirectory(parentDir);

      zip.addFile(zipPath, fileData);

      ++processed;
      int percent = 80 + (processed * 15 / total);
      emit exportProgressChanged(percent, tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  if (!zip.close())
  {
    QFile::remove(m_outputPath);
    emit exportFinished(false, QString(), tr("写入压缩包失败，磁盘可能已满"));
    return;
  }
  emit exportProgressChanged(100, tr("导出完成"));
  emit exportFinished(true, m_outputPath, QString());
}

// ---------------------------------------------------------------------------
// BlockBox export
// ---------------------------------------------------------------------------

void ModpackExporter::exportAsBlockBox()
{
  // =========================================================================
  // 方块盒子原生格式：ZIP 容器 + blockbox.pack.json 清单 + 原始相对路径文件。
  // 旧实现是自定义 BBOX 二进制（元数据重写后偏移全错、产物必然损坏），
  // 且导入端完全无法识别；改为 zip 后复用现有 ZipWriter/ZipReader，
  // 导入端按 blockbox.pack.json 识别并整包解压即可往返。
  // =========================================================================
  emit exportProgressChanged(0, tr("正在扫描文件..."));

  struct FileEntry
  {
    QString path;
    qint64 size = 0;
    QByteArray sha1;
  };
  QList<FileEntry> entries;

  for (const QString& relPath : m_selectedFiles)
  {
    const QString fullPath = m_instancePath + "/" + relPath;
    const QFileInfo fi(fullPath);
    if (!fi.exists() || !fi.isFile())
      continue;

    const QByteArray sha1 = calculateSha1(fullPath);
    if (sha1.isEmpty())
      continue;

    FileEntry entry;
    entry.path = relPath;
    entry.size = fi.size();
    entry.sha1 = sha1;
    entries.append(entry);
  }

  if (entries.isEmpty())
  {
    emit exportFinished(false, QString(), tr("没有找到文件"));
    return;
  }

  emit exportProgressChanged(10, tr("正在生成元数据..."));

  const QString gameVersion = detectGameVersion();
  const QMap<QString, QString> allLoaders = detectAllLoaders();

  // Group files by top-level directory for module classification
  struct ModuleInfo
  {
    QString name;
    QString label;
    int fileCount = 0;
    qint64 totalSize = 0;
  };
  QMap<QString, ModuleInfo> moduleMap;

  for (const FileEntry& entry : entries)
  {
    QString topDir = entry.path.section('/', 0, 0);
    if (topDir.isEmpty() || topDir == entry.path)
      topDir = QStringLiteral("root");

    ModuleInfo& mod = moduleMap[topDir];
    if (mod.name.isEmpty())
    {
      mod.name = topDir;
      mod.label = topDir;
    }
    mod.fileCount++;
    mod.totalSize += entry.size;
  }

  QJsonObject meta;
  meta["blockboxVersion"] = 1;
  meta["name"] = m_exportInfo.name;
  meta["version"] = m_exportInfo.version;
  meta["author"] = m_exportInfo.author;
  meta["description"] = m_exportInfo.description;
  meta["gameVersion"] = gameVersion;

  QJsonArray loadersArray;
  for (auto it = allLoaders.constBegin(); it != allLoaders.constEnd(); ++it)
  {
    QJsonObject loaderObj;
    loaderObj["id"] = it.key();
    loaderObj["version"] = it.value();
    loadersArray.append(loaderObj);
  }
  meta["loaders"] = loadersArray;

  QJsonArray modulesArray;
  for (const ModuleInfo& mod : moduleMap)
  {
    QJsonObject modObj;
    modObj["name"] = mod.name;
    modObj["label"] = mod.label;
    modObj["fileCount"] = mod.fileCount;
    modObj["totalSize"] = mod.totalSize;
    modulesArray.append(modObj);
  }
  meta["modules"] = modulesArray;

  QJsonArray filesArray;
  for (const FileEntry& entry : entries)
  {
    QJsonObject fileObj;
    fileObj["path"] = entry.path;
    fileObj["size"] = entry.size;
    fileObj["sha1"] = QString::fromLatin1(entry.sha1.toHex());
    filesArray.append(fileObj);
  }
  meta["files"] = filesArray;

  const QByteArray metaBytes =
    QJsonDocument(meta).toJson(QJsonDocument::Compact);

  emit exportProgressChanged(20, tr("正在创建整合包文件..."));

  ZipWriter zip;
  if (!zip.open(m_outputPath))
  {
    emit exportFinished(false, QString(), tr("无法创建输出文件：%1").arg(m_outputPath));
    return;
  }

  if (!zip.addFile(QStringLiteral("blockbox.pack.json"), metaBytes, false))
  {
    zip.close();
    QFile::remove(m_outputPath);
    emit exportFinished(false, QString(), tr("写入清单文件失败"));
    return;
  }

  const int total = entries.size();
  int processed = 0;
  for (const FileEntry& entry : entries)
  {
    QFile file(m_instancePath + "/" + entry.path);
    if (!file.open(QIODevice::ReadOnly))
    {
      zip.close();
      QFile::remove(m_outputPath);
      emit exportFinished(false, QString(),
                          tr("读取文件失败：%1").arg(entry.path));
      return;
    }
    const QByteArray fileData = file.readAll();
    file.close();

    if (!zip.addFile(entry.path, fileData))
    {
      zip.close();
      QFile::remove(m_outputPath);
      emit exportFinished(false, QString(),
                          tr("打包文件失败：%1").arg(entry.path));
      return;
    }

    ++processed;
    emit exportProgressChanged(20 + processed * 75 / total,
                               tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
  }

  if (!zip.close())
  {
    QFile::remove(m_outputPath);
    emit exportFinished(false, QString(), tr("写入压缩包失败，磁盘可能已满"));
    return;
  }

  emit exportProgressChanged(100, tr("导出完成"));
  emit exportFinished(true, m_outputPath, QString());
}

// ---------------------------------------------------------------------------
// Packwiz export
// ---------------------------------------------------------------------------

void ModpackExporter::exportAsPackwiz()
{
  // =========================================================================
  // Phase 1 - Prepare (0-20%)
  // =========================================================================
  emit exportProgressChanged(0, tr("正在准备导出 (Packwiz)..."));

  // Create output directory and mods/ subdirectory
  QDir outputDir(m_outputPath);
  if (!outputDir.exists())
  {
    if (!outputDir.mkpath("."))
    {
      emit exportFinished(false, QString(), tr("无法创建输出目录：%1").arg(m_outputPath));
      return;
    }
  }

  QString modsDir = m_outputPath + "/mods";
  QDir modsDirectory(modsDir);
  if (!modsDirectory.exists())
  {
    if (!modsDirectory.mkpath("."))
    {
      emit exportFinished(false, QString(), tr("无法创建 mods 目录"));
      return;
    }
  }

  emit exportProgressChanged(20, tr("正在准备导出 (Packwiz)..."));

  // =========================================================================
  // Phase 2 - Generate pack.toml (20-40%)
  // =========================================================================
  emit exportProgressChanged(20, tr("正在生成 pack.toml..."));

  QString gameVersion = detectGameVersion();
  QMap<QString, QString> allLoaders = detectAllLoaders();

  // Build pack.toml content
  QString packToml;
  packToml += QStringLiteral("name = \"%1\"\n").arg(m_exportInfo.name);
  packToml += QStringLiteral("version = \"%2\"\n").arg(m_exportInfo.version);
  packToml += QStringLiteral("author = \"%3\"\n").arg(m_exportInfo.author);
  packToml += QStringLiteral("pack-format = \"packwiz:1.1.0\"\n\n");
  packToml += QStringLiteral("[index]\n");
  packToml += QStringLiteral("file = \"index.toml\"\n");
  packToml += QStringLiteral("hash-format = \"sha256\"\n");
  packToml += QStringLiteral("hash = \"\"\n\n");
  packToml += QStringLiteral("[versions]\n");
  packToml += QStringLiteral("minecraft = \"%1\"\n").arg(gameVersion);

  // Add loader-specific lines
  for (auto it = allLoaders.constBegin(); it != allLoaders.constEnd(); ++it)
  {
    packToml += QStringLiteral("%1 = \"%2\"\n").arg(it.key(), it.value());
  }

  // Write pack.toml
  QString packTomlPath = m_outputPath + "/pack.toml";
  QFile packTomlFile(packTomlPath);
  if (!packTomlFile.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    emit exportFinished(false, QString(), tr("无法写入 pack.toml"));
    return;
  }
  packTomlFile.write(packToml.toUtf8());
  packTomlFile.close();

  emit exportProgressChanged(40, tr("正在生成 pack.toml..."));

  // =========================================================================
  // Phase 3 - Copy files + generate .pw.toml (40-80%)
  // =========================================================================

  struct IndexEntry
  {
    QString file;
    QString hash;
    bool metafile = false;
  };
  QList<IndexEntry> indexEntries;

  if (!m_selectedFiles.isEmpty())
  {
    int total = m_selectedFiles.size();
    int processed = 0;

    for (const QString& relPath : m_selectedFiles)
    {
      QString fullPath = m_instancePath + "/" + relPath;
      QFileInfo fi(fullPath);
      if (!fi.exists() || !fi.isFile())
      {
        ++processed;
        continue;
      }

      bool isMod = relPath.startsWith(QStringLiteral("mods/"), Qt::CaseInsensitive);

      if (isMod)
      {
        // Copy mod file to mods/ directory
        QString destPath = modsDir + "/" + fi.fileName();
        if (!QFile::copy(fullPath, destPath))
        {
          ++processed;
          continue;
        }

        // Calculate SHA-256 hash of the mod file
        QByteArray sha256 = calculateSha256(destPath);
        if (sha256.isEmpty())
        {
          ++processed;
          continue;
        }
        QString sha256Hex = QString::fromLatin1(sha256);

        // Generate .pw.toml file
        QString modName = fi.completeBaseName();
        if (modName.isEmpty())
          modName = fi.fileName();

        QString pwToml;
        pwToml += QStringLiteral("name = \"%1\"\n").arg(modName);
        pwToml += QStringLiteral("filename = \"%1\"\n").arg(fi.fileName());
        pwToml += QStringLiteral("side = \"both\"\n\n");
        pwToml += QStringLiteral("[download]\n");
        pwToml += QStringLiteral("hash-format = \"sha256\"\n");
        pwToml += QStringLiteral("hash = \"%1\"\n").arg(sha256Hex);

        QString pwTomlPath = destPath + ".pw.toml";
        QFile pwTomlFile(pwTomlPath);
        if (!pwTomlFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
          ++processed;
          continue;
        }
        pwTomlFile.write(pwToml.toUtf8());
        pwTomlFile.close();

        // Add to index with metafile = true
        IndexEntry entry;
        entry.file = QStringLiteral("mods/") + fi.fileName();
        entry.hash = sha256Hex;
        entry.metafile = true;
        indexEntries.append(entry);
      }
      else
      {
        // Copy non-mod file maintaining relative path
        QString destPath = m_outputPath + "/" + relPath;

        // Ensure parent directories exist
        QDir destDir = QFileInfo(destPath).absoluteDir();
        if (!destDir.exists())
          destDir.mkpath(".");

        if (!QFile::copy(fullPath, destPath))
        {
          ++processed;
          continue;
        }

        // Calculate SHA-256 hash
        QByteArray sha256 = calculateSha256(destPath);
        if (sha256.isEmpty())
        {
          ++processed;
          continue;
        }

        IndexEntry entry;
        entry.file = relPath;
        entry.hash = QString::fromLatin1(sha256);
        entry.metafile = false;
        indexEntries.append(entry);
      }

      ++processed;
      int percent = 40 + (processed * 40 / total);
      emit exportProgressChanged(percent, tr("正在导出文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  // =========================================================================
  // Phase 4 - Generate index.toml (80-95%)
  // =========================================================================
  emit exportProgressChanged(80, tr("正在生成 index.toml..."));

  QString indexToml;
  for (const IndexEntry& entry : indexEntries)
  {
    indexToml += QStringLiteral("[[files]]\n");
    indexToml += QStringLiteral("file = \"%1\"\n").arg(entry.file);
    indexToml += QStringLiteral("hash = \"%1\"\n").arg(entry.hash);
    indexToml += QStringLiteral("metafile = %1\n\n").arg(entry.metafile ? QStringLiteral("true") : QStringLiteral("false"));
  }

  // Write index.toml
  QString indexTomlPath = m_outputPath + "/index.toml";
  QFile indexTomlFile(indexTomlPath);
  if (!indexTomlFile.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    emit exportFinished(false, QString(), tr("无法写入 index.toml"));
    return;
  }
  indexTomlFile.write(indexToml.toUtf8());
  indexTomlFile.close();

  // Calculate hash of index.toml and update pack.toml
  QByteArray indexHash = calculateSha256(indexTomlPath);
  QString indexHashHex = QString::fromLatin1(indexHash);

  if (!indexHash.isEmpty())
  {
    // Re-read pack.toml, replace empty hash with actual hash
    if (packTomlFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      QString updatedPackToml = QString::fromUtf8(packTomlFile.readAll());
      packTomlFile.close();

      updatedPackToml.replace(QStringLiteral("hash = \"\""),
                              QStringLiteral("hash = \"%1\"").arg(indexHashHex));

      if (packTomlFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
      {
        packTomlFile.write(updatedPackToml.toUtf8());
        packTomlFile.close();
      }
    }
  }

  emit exportProgressChanged(95, tr("正在生成 index.toml..."));

  // =========================================================================
  // Phase 5 - Done (95-100%)
  // =========================================================================
  emit exportProgressChanged(100, tr("导出完成"));
  emit exportFinished(true, m_outputPath, QString());
}

// ---------------------------------------------------------------------------
// Helper methods
// ---------------------------------------------------------------------------

namespace {

/// 读取实例目录里的版本 JSON（约定文件名 <目录名>.json，兜底任意 *.json）
QJsonObject readInstanceVersionJson(const QString& instancePath)
{
  const QString dirName = QFileInfo(instancePath).fileName();
  QStringList candidates{instancePath + "/" + dirName + ".json"};
  const QStringList jsons =
    QDir(instancePath).entryList(QStringList() << "*.json", QDir::Files);
  for (const QString& j : jsons)
  {
    const QString p = instancePath + "/" + j;
    if (!candidates.contains(p))
      candidates.append(p);
  }
  for (const QString& p : candidates)
  {
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly))
      continue;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    if (!obj.isEmpty())
      return obj;
  }
  return QJsonObject();
}

/// 从版本 JSON 的 id / inheritsFrom 推导游戏版本号
QString gameVersionFromJson(const QJsonObject& obj)
{
  // 加载器 JSON：inheritsFrom 指向原版版本
  const QString inherits = obj.value(QStringLiteral("inheritsFrom")).toString();
  if (!inherits.isEmpty())
    return inherits;
  // 纯原版 JSON：id 就是版本号；带后缀的（1.20.1-forge-47.2.0）截取前段
  const QString id = obj.value(QStringLiteral("id")).toString();
  if (id.isEmpty())
    return QString();
  static const QRegularExpression pureVer(
    QStringLiteral("^(1\\.\\d+(\\.\\d+)?|[a-z]+\\d+[a-z]?)$"),
    QRegularExpression::CaseInsensitiveOption);
  if (pureVer.match(id).hasMatch())
    return id;
  static const QRegularExpression extract(QStringLiteral("1\\.\\d+(\\.\\d+)?"));
  const QRegularExpressionMatch m = extract.match(id);
  return m.hasMatch() ? m.captured() : QString();
}

/// 从版本 JSON 的 id 解析加载器类型与版本号
void loaderFromJson(const QJsonObject& obj, QString& type, QString& version)
{
  const QString id = obj.value(QStringLiteral("id")).toString().toLower();
  if (id.isEmpty())
    return;
  // fabric-loader-0.15.11-1.20.1 → fabric / 0.15.11
  static const QRegularExpression fabricRe(
    QStringLiteral("fabric-loader-([0-9][0-9a-zA-Z\\.\\+]*)-"));
  QRegularExpressionMatch m = fabricRe.match(id);
  if (m.hasMatch())
  {
    type = QStringLiteral("fabric");
    version = m.captured(1);
    return;
  }
  // quilt-loader-x.y-1.20.1 → quilt
  static const QRegularExpression quiltRe(
    QStringLiteral("quilt-loader-([0-9][0-9a-zA-Z\\.\\+]*)-"));
  m = quiltRe.match(id);
  if (m.hasMatch())
  {
    type = QStringLiteral("quilt");
    version = m.captured(1);
    return;
  }
  // neoforge-20.4.237 或 1.20.1-neoforge-20.4.237 → neoforge
  static const QRegularExpression neoforgeRe(QStringLiteral("neoforge-([0-9][0-9a-zA-Z\\.]*)"));
  m = neoforgeRe.match(id);
  if (m.hasMatch())
  {
    type = QStringLiteral("neoforge");
    version = m.captured(1);
    return;
  }
  // forge-1.20.1-47.2.0 或 1.20.1-forge-47.2.0 → forge（取 forge- 之后的部分）
  static const QRegularExpression forgeRe(QStringLiteral("forge-(.+)$"));
  m = forgeRe.match(id);
  if (m.hasMatch())
  {
    type = QStringLiteral("forge");
    QString ver = m.captured(1);
    // 若形如 1.20.1-47.2.0（forge- 后又带了游戏版本），去掉游戏版本前缀
    static const QRegularExpression stripMc(QStringLiteral("^1\\.\\d+(\\.\\d+)?-"));
    ver.remove(stripMc);
    version = ver;
    return;
  }

  // 合并式版本 JSON（无 inheritsFrom，id 形如 "1.21.4-Fabric 0.17.1"）:
  // 从 mainClass 与库坐标识别加载器及其版本号
  const QString mainClass = obj.value(QStringLiteral("mainClass")).toString();
  if (mainClass.contains(QStringLiteral("fabricmc"), Qt::CaseInsensitive))
    type = QStringLiteral("fabric");
  else if (mainClass.contains(QStringLiteral("quiltmc"), Qt::CaseInsensitive))
    type = QStringLiteral("quilt");
  for (const QJsonValue& libVal : obj.value(QStringLiteral("libraries")).toArray())
  {
    const QStringList coords = libVal.toObject().value(QStringLiteral("name")).toString().split(QChar(':'));
    if (coords.size() < 3)
      continue;
    if (coords[0] == QLatin1String("net.fabricmc") && coords[1] == QLatin1String("fabric-loader"))
    {
      type = QStringLiteral("fabric");
      version = coords[2];
      break;
    }
    if (coords[0] == QLatin1String("org.quiltmc") && coords[1] == QLatin1String("quilt-loader"))
    {
      type = QStringLiteral("quilt");
      version = coords[2];
      break;
    }
    if (coords[0] == QLatin1String("net.neoforged") && coords[1] == QLatin1String("neoforge"))
    {
      type = QStringLiteral("neoforge");
      version = coords[2];
      break;
    }
  }
}

} // namespace

QString ModpackExporter::detectGameVersion() const
{
  // 实例目录自身就带版本 JSON（<实例名>.json），直接读它，
  // 不要再去扫 <实例>/versions —— 那是旧「游戏根」布局，真实实例没有这层
  return gameVersionFromJson(readInstanceVersionJson(m_instancePath));
}

QString ModpackExporter::detectLoaderType() const
{
  QString type, version;
  loaderFromJson(readInstanceVersionJson(m_instancePath), type, version);
  if (type.isEmpty())
    return QString();
  // 保持旧接口的展示大小写
  if (type == QLatin1String("forge"))
    return QStringLiteral("Forge");
  if (type == QLatin1String("fabric"))
    return QStringLiteral("Fabric");
  if (type == QLatin1String("neoforge"))
    return QStringLiteral("NeoForge");
  if (type == QLatin1String("quilt"))
    return QStringLiteral("Quilt");
  return type;
}

QString ModpackExporter::detectLoaderVersion() const
{
  QString type, version;
  loaderFromJson(readInstanceVersionJson(m_instancePath), type, version);
  return version;
}

QMap<QString, QString> ModpackExporter::detectAllLoaders() const
{
  QMap<QString, QString> loaders;
  QString type, version;
  loaderFromJson(readInstanceVersionJson(m_instancePath), type, version);
  if (!type.isEmpty() && !version.isEmpty())
    loaders[type] = version;
  return loaders;
}

QByteArray ModpackExporter::calculateSha1(const QString& filePath) const
{
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly))
    return QByteArray();

  QCryptographicHash hash(QCryptographicHash::Sha1);
  hash.addData(&file);
  file.close();
  return hash.result();
}

QByteArray ModpackExporter::calculateSha512(const QString& filePath) const
{
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly))
    return QByteArray();

  QCryptographicHash hash(QCryptographicHash::Sha512);
  hash.addData(&file);
  file.close();
  return hash.result();
}

QByteArray ModpackExporter::calculateSha256(const QString& filePath) const
{
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly))
    return QByteArray();

  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(&file);
  file.close();
  return hash.result().toHex();
}

} // namespace modpack