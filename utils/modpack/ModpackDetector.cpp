#include "ModpackDetector.h"

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

#include <zlib.h>

namespace modpack {

// ============================================================================
// Minimal ZipReader - reads central directory and extracts individual files
// Uses zlib for deflate decompression
// ============================================================================

class ZipReader
{
public:
  struct Entry
  {
    QString fileName;
    quint16 compressionMethod;
    quint32 compressedSize;
    quint32 uncompressedSize;
    quint32 localHeaderOffset;
  };

  bool open(const QString& filePath);
  QList<Entry> entries() const { return m_entries; }
  QByteArray extractFile(const QString& fileName);
  bool hasFile(const QString& fileName) const;
  QString findFile(const QString& pattern) const; // simple contains match

private:
  bool readEocd(qint64& centralDirOffset, quint16& totalEntries);
  bool readCentralDirectory(qint64 centralDirOffset, quint16 totalEntries);
  QByteArray readRaw(quint32 offset, quint32 size);
  QByteArray decompressDeflate(const QByteArray& compressed, quint32 expectedSize);

  QFile m_file;
  QList<Entry> m_entries;
  QMap<QString, Entry> m_entryMap;
};

bool ZipReader::open(const QString& filePath)
{
  m_file.setFileName(filePath);
  if (!m_file.open(QIODevice::ReadOnly))
    return false;

  qint64 centralDirOffset = 0;
  quint16 totalEntries = 0;
  if (!readEocd(centralDirOffset, totalEntries))
  {
    m_file.close();
    return false;
  }

  if (!readCentralDirectory(centralDirOffset, totalEntries))
  {
    m_file.close();
    return false;
  }

  return true;
}

bool ZipReader::readEocd(qint64& centralDirOffset, quint16& totalEntries)
{
  // EOCD signature: 0x06054b50
  // Search backward from end of file (max comment is 65535 bytes)
  qint64 fileSize = m_file.size();
  if (fileSize < 22) // minimum EOCD size
    return false;

  qint64 searchStart = qMax(qint64(0), fileSize - 65557);
  qint64 searchSize = fileSize - searchStart;
  if (searchSize > 65557)
    searchSize = 65557;

  m_file.seek(searchStart);
  QByteArray tail = m_file.read(searchSize);

  for (int i = tail.size() - 22; i >= 0; --i)
  {
    if (static_cast<quint8>(tail[i]) == 0x50
        && static_cast<quint8>(tail[i + 1]) == 0x4b
        && static_cast<quint8>(tail[i + 2]) == 0x05
        && static_cast<quint8>(tail[i + 3]) == 0x06)
    {
      QDataStream ds(tail.mid(i, 22));
      ds.setByteOrder(QDataStream::LittleEndian);
      ds.skipRawData(8); // skip disk numbers and entry counts on disk

      quint16 entryCount = 0;
      quint32 cdSize = 0;
      quint32 cdOffset = 0;
      ds >> entryCount >> cdSize >> cdOffset;

      // Also read the on-disk entry count for verification
      QDataStream ds2(tail.mid(i + 8, 2));
      ds2.setByteOrder(QDataStream::LittleEndian);
      quint16 onDiskEntryCount = 0;
      ds2 >> onDiskEntryCount;

      centralDirOffset = cdOffset;
      totalEntries = qMin(onDiskEntryCount, entryCount);
      return true;
    }
  }
  return false;
}

bool ZipReader::readCentralDirectory(qint64 centralDirOffset, quint16 totalEntries)
{
  m_entries.clear();
  m_entryMap.clear();

  if (!m_file.seek(centralDirOffset))
    return false;

  const int cdEntryBaseSize = 46; // fixed fields size
  for (quint16 i = 0; i < totalEntries; ++i)
  {
    QByteArray header = m_file.read(cdEntryBaseSize);
    if (header.size() < cdEntryBaseSize)
      return false;

    QDataStream ds(header);
    ds.setByteOrder(QDataStream::LittleEndian);

    quint32 signature = 0;
    quint16 versionMadeBy = 0;
    quint16 versionNeeded = 0;
    quint16 gpFlag = 0;
    quint16 compressionMethod = 0;
    quint32 crc32 = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;
    quint16 fileNameLength = 0;
    quint16 extraFieldLength = 0;
    quint16 fileCommentLength = 0;
    quint16 diskNumberStart = 0;
    quint16 internalAttrs = 0;
    quint32 externalAttrs = 0;
    quint32 localHeaderOffset = 0;

    ds >> signature >> versionMadeBy >> versionNeeded >> gpFlag
       >> compressionMethod
       >> crc32 >> compressedSize >> uncompressedSize
       >> fileNameLength >> extraFieldLength >> fileCommentLength
       >> diskNumberStart >> internalAttrs >> externalAttrs
       >> localHeaderOffset;

    if (signature != 0x02014b50)
      return false;

    QByteArray fileNameBytes = m_file.read(fileNameLength);
    if (fileNameBytes.size() < fileNameLength)
      return false;

    // Skip extra field and comment
    m_file.read(extraFieldLength + fileCommentLength);

    Entry entry;
    entry.fileName = QString::fromUtf8(fileNameBytes);
    entry.compressionMethod = compressionMethod;
    entry.compressedSize = compressedSize;
    entry.uncompressedSize = uncompressedSize;
    entry.localHeaderOffset = localHeaderOffset;

    m_entries.append(entry);

    // Store in map with both full path and just filename for lookups
    QString normalized = entry.fileName;
    if (normalized.endsWith('/'))
      normalized.chop(1);

    m_entryMap[entry.fileName] = entry;
    m_entryMap[normalized] = entry;

    // Also store just the filename part for convenience
    int lastSlash = entry.fileName.lastIndexOf('/');
    if (lastSlash >= 0)
    {
      QString baseName = entry.fileName.mid(lastSlash + 1);
      if (!m_entryMap.contains(baseName))
        m_entryMap[baseName] = entry;
    }
  }
  return true;
}

bool ZipReader::hasFile(const QString& fileName) const
{
  return m_entryMap.contains(fileName);
}

QString ZipReader::findFile(const QString& pattern) const
{
  for (const auto& entry : m_entries)
  {
    if (entry.fileName.contains(pattern, Qt::CaseInsensitive))
      return entry.fileName;
  }
  return QString();
}

QByteArray ZipReader::extractFile(const QString& fileName)
{
  if (!m_entryMap.contains(fileName))
    return QByteArray();

  const Entry& entry = m_entryMap[fileName];

  // Seek to local file header
  if (!m_file.seek(entry.localHeaderOffset))
    return QByteArray();

  // Read local file header (30 bytes fixed + variable filename + extra)
  QByteArray localHeader = m_file.read(30);
  if (localHeader.size() < 30)
    return QByteArray();

  QDataStream ds(localHeader);
  ds.setByteOrder(QDataStream::LittleEndian);

  quint32 signature = 0;
  quint16 versionNeeded = 0;
  quint16 gpFlag = 0;
  quint16 compressionMethod = 0;
  quint16 fileNameLength = 0;
  quint16 extraFieldLength = 0;

  ds >> signature >> versionNeeded >> gpFlag >> compressionMethod;
  ds.skipRawData(8); // skip time, date, crc32
  ds.skipRawData(8); // skip compressed/uncompressed size (may be 0 if data descriptor)
  ds >> fileNameLength >> extraFieldLength;

  if (signature != 0x04034b50)
    return QByteArray();

  // Skip filename and extra field to reach compressed data
  m_file.read(fileNameLength);
  m_file.read(extraFieldLength);

  QByteArray compressedData = m_file.read(entry.compressedSize);
  if (compressedData.size() < static_cast<int>(entry.compressedSize))
    return QByteArray();

  if (compressionMethod == 0)
  {
    // Stored (no compression)
    return compressedData;
  }
  else if (compressionMethod == 8)
  {
    // Deflated
    return decompressDeflate(compressedData, entry.uncompressedSize);
  }

  return QByteArray();
}

QByteArray ZipReader::decompressDeflate(const QByteArray& compressed, quint32 expectedSize)
{
  QByteArray result;
  if (expectedSize > 0)
    result.resize(expectedSize);

  z_stream strm = {};
  strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
  strm.avail_in = compressed.size();

  // -MAX_WBITS for raw deflate (no zlib/gzip header)
  int ret = inflateInit2(&strm, -MAX_WBITS);
  if (ret != Z_OK)
    return QByteArray();

  if (expectedSize > 0)
  {
    strm.next_out = reinterpret_cast<Bytef*>(result.data());
    strm.avail_out = expectedSize;
    ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END)
    {
      // Fall back to incremental decompression
      return QByteArray();
    }
    result.resize(strm.total_out);
  }
  else
  {
    // Size unknown, decompress incrementally
    inflateEnd(&strm);

    strm = {};
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    strm.avail_in = compressed.size();
    ret = inflateInit2(&strm, -MAX_WBITS);
    if (ret != Z_OK)
      return QByteArray();

    const int CHUNK = 16384;
    QByteArray buffer(CHUNK, '\0');
    do
    {
      strm.next_out = reinterpret_cast<Bytef*>(buffer.data());
      strm.avail_out = CHUNK;
      ret = inflate(&strm, Z_NO_FLUSH);
      if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
      {
        inflateEnd(&strm);
        return QByteArray();
      }
      result.append(buffer.data(), CHUNK - strm.avail_out);
    }
    while (strm.avail_out == 0);

    inflateEnd(&strm);
  }

  return result;
}

// ============================================================================
// ModpackInfo::typeName() implementation
// ============================================================================

QString ModpackInfo::typeName() const
{
  switch (type)
  {
  case ModpackType::CurseForge: return QStringLiteral("CurseForge");
  case ModpackType::Modrinth:   return QStringLiteral("Modrinth");
  case ModpackType::MultiMC:    return QStringLiteral("MultiMC");
  case ModpackType::MCBBS:      return QStringLiteral("MCBBS");
  case ModpackType::HMCL:       return QStringLiteral("HMCL");
  case ModpackType::Generic:    return QStringLiteral("通用整合包");
  default:                      return QStringLiteral("未知");
  }
}

// ============================================================================
// Helper: extract loader info from a loader ID string
// ============================================================================

static void extractLoaderInfo(const QString& loaderId, QString& loaderType, QString& loaderVersion)
{
  QString lower = loaderId.toLower();
  if (lower.startsWith("forge-"))
  {
    loaderType = QStringLiteral("forge");
    loaderVersion = loaderId.mid(6);
  }
  else if (lower.startsWith("fabric-"))
  {
    loaderType = QStringLiteral("fabric");
    loaderVersion = loaderId.mid(7);
  }
  else if (lower.startsWith("neoforge-") || lower.startsWith("neoforged-"))
  {
    loaderType = QStringLiteral("neoforge");
    int dashIdx = loaderId.indexOf('-');
    loaderVersion = loaderId.mid(dashIdx + 1);
  }
  else if (lower == "forge")
  {
    loaderType = QStringLiteral("forge");
  }
  else if (lower == "fabric")
  {
    loaderType = QStringLiteral("fabric");
  }
  else if (lower == "neoforge" || lower == "neoforged")
  {
    loaderType = QStringLiteral("neoforge");
  }
  else if (lower == "quilt")
  {
    loaderType = QStringLiteral("quilt");
  }
  else if (lower == "liteloader")
  {
    loaderType = QStringLiteral("liteloader");
  }
  else
  {
    loaderType = lower;
  }
}

// ============================================================================
// Format-specific parsers
// ============================================================================

static ModpackInfo parseCurseForge(ZipReader& zip)
{
  ModpackInfo info;
  info.type = ModpackType::CurseForge;

  QByteArray data = zip.extractFile("manifest.json");
  if (data.isEmpty())
    return info;

  QJsonDocument doc = QJsonDocument::fromJson(data);
  QJsonObject root = doc.object();

  info.name = root.value("name").toString();
  info.version = root.value("version").toString();
  info.author = root.value("author").toString();

  // minecraft section
  QJsonObject mcObj = root.value("minecraft").toObject();
  info.gameVersion = mcObj.value("version").toString();

  // modLoaders
  QJsonArray loaders = mcObj.value("modLoaders").toArray();
  for (const auto& loaderVal : loaders)
  {
    QJsonObject loaderObj = loaderVal.toObject();
    QString loaderId = loaderObj.value("id").toString();
    bool primary = loaderObj.value("primary").toBool(false);

    if (primary)
      extractLoaderInfo(loaderId, info.loaderType, info.loaderVersion);
    else if (info.loaderType.isEmpty())
      extractLoaderInfo(loaderId, info.loaderType, info.loaderVersion);
  }

  // files
  QJsonArray files = root.value("files").toArray();
  for (const auto& fileVal : files)
  {
    QJsonObject fileObj = fileVal.toObject();
    ModInfo mod;
    mod.projectId = QString::number(fileObj.value("projectID").toInt());
    mod.fileId = QString::number(fileObj.value("fileID").toInt());
    mod.required = fileObj.value("required").toBool(true);
    info.mods.append(mod);
  }

  // overrides
  info.overridesPath = root.value("overrides").toString("overrides");

  return info;
}

static ModpackInfo parseModrinth(ZipReader& zip)
{
  ModpackInfo info;
  info.type = ModpackType::Modrinth;

  QByteArray data = zip.extractFile("modrinth.index.json");
  if (data.isEmpty())
    return info;

  QJsonDocument doc = QJsonDocument::fromJson(data);
  QJsonObject root = doc.object();

  info.name = root.value("name").toString();
  info.version = root.value("versionId").toString();

  // dependencies
  QJsonObject deps = root.value("dependencies").toObject();
  info.gameVersion = deps.value("minecraft").toString();

  // Check loader dependencies
  static const char* loaderKeys[] = {"forge", "fabric-loader", "fabric", "neoforge", "quilt", nullptr};
  for (int i = 0; loaderKeys[i] != nullptr; ++i)
  {
    QString key = QString::fromLatin1(loaderKeys[i]);
    if (deps.contains(key))
    {
      QString ver = deps.value(key).toString();
      if (key == "fabric-loader")
        key = "fabric";

      if (info.loaderType.isEmpty())
      {
        info.loaderType = key;
        info.loaderVersion = ver;
      }
    }
  }

  // files
  QJsonArray files = root.value("files").toArray();
  for (const auto& fileVal : files)
  {
    QJsonObject fileObj = fileVal.toObject();
    ModInfo mod;
    mod.name = fileObj.value("path").toString();

    QJsonObject hashes = fileObj.value("hashes").toObject();
    mod.fileId = hashes.value("sha1").toString();
    if (mod.fileId.isEmpty())
      mod.fileId = hashes.value("sha512").toString();

    QJsonArray downloads = fileObj.value("downloads").toArray();
    if (!downloads.isEmpty())
      mod.downloadUrl = downloads.first().toString();

    mod.required = true;
    info.mods.append(mod);
  }

  info.overridesPath = QString();
  return info;
}

static ModpackInfo parseMultiMC(ZipReader& zip)
{
  ModpackInfo info;
  info.type = ModpackType::MultiMC;

  QByteArray data = zip.extractFile("mmc-pack.json");
  if (data.isEmpty())
    return info;

  QJsonDocument doc = QJsonDocument::fromJson(data);
  QJsonObject root = doc.object();

  info.name = root.value("name").toString();

  // components
  QJsonArray components = root.value("components").toArray();
  for (const auto& compVal : components)
  {
    QJsonObject compObj = compVal.toObject();
    QString uid = compObj.value("uid").toString();
    QString version = compObj.value("version").toString();

    if (uid == "net.minecraft")
    {
      info.gameVersion = version;
    }
    else if (uid == "net.minecraftforge")
    {
      info.loaderType = "forge";
      info.loaderVersion = version;
    }
    else if (uid == "net.fabricmc.fabric-loader")
    {
      info.loaderType = "fabric";
      info.loaderVersion = version;
    }
    else if (uid == "net.neoforged" || uid.contains("neoforge", Qt::CaseInsensitive))
    {
      info.loaderType = "neoforge";
      info.loaderVersion = version;
    }
    else if (uid == "org.quiltmc.quilt-loader")
    {
      info.loaderType = "quilt";
      info.loaderVersion = version;
    }
  }

  // For MultiMC, the overrides are in the minecraft/ subfolder
  QString mcFolder = zip.findFile("minecraft/");
  if (!mcFolder.isEmpty())
    info.overridesPath = "minecraft";

  return info;
}

static ModpackInfo parseMCBBS(ZipReader& zip)
{
  ModpackInfo info;
  info.type = ModpackType::MCBBS;

  QByteArray data = zip.extractFile("mcbbs.pack.json");
  if (data.isEmpty())
    return info;

  QJsonDocument doc = QJsonDocument::fromJson(data);
  QJsonObject root = doc.object();

  info.name = root.value("name").toString();
  info.version = root.value("version").toString();
  info.author = root.value("author").toString();
  info.gameVersion = root.value("gameVersion").toString();
  info.description = root.value("description").toString();

  // loader
  QString loader = root.value("modLoader").toString();
  extractLoaderInfo(loader, info.loaderType, info.loaderVersion);

  // files
  QJsonArray files = root.value("files").toArray();
  for (const auto& fileVal : files)
  {
    QJsonObject fileObj = fileVal.toObject();
    ModInfo mod;
    mod.name = fileObj.value("name").toString();
    mod.downloadUrl = fileObj.value("url").toString();
    mod.required = fileObj.value("required").toBool(true);

    if (fileObj.contains("projectID"))
      mod.projectId = QString::number(fileObj.value("projectID").toInt());
    if (fileObj.contains("fileID"))
      mod.fileId = QString::number(fileObj.value("fileID").toInt());

    info.mods.append(mod);
  }

  info.overridesPath = root.value("overrides").toString();
  return info;
}

static ModpackInfo parseHMCL(ZipReader& zip)
{
  ModpackInfo info;
  info.type = ModpackType::HMCL;

  QByteArray data = zip.extractFile("modpack.json");
  if (data.isEmpty())
  {
    // HMCL also uses pack.json sometimes
    data = zip.extractFile("pack.json");
  }
  if (data.isEmpty())
    return info;

  QJsonDocument doc = QJsonDocument::fromJson(data);
  QJsonObject root = doc.object();

  info.name = root.value("name").toString();
  info.version = root.value("version").toString();
  info.author = root.value("author").toString();
  info.gameVersion = root.value("gameVersion").toString();
  info.description = root.value("description").toString();

  // modLoader - HMCL uses an array of loader objects
  QJsonValue loaderVal = root.value("modLoader");
  if (loaderVal.isArray())
  {
    QJsonArray loaderArray = loaderVal.toArray();
    for (const auto& lv : loaderArray)
    {
      QJsonObject loaderObj = lv.toObject();
      QString id = loaderObj.value("id").toString();
      QString ver = loaderObj.value("version").toString();
      if (!id.isEmpty() && info.loaderType.isEmpty())
      {
        info.loaderType = id.toLower();
        info.loaderVersion = ver;
      }
    }
  }
  else if (loaderVal.isObject())
  {
    QJsonObject loaderObj = loaderVal.toObject();
    info.loaderType = loaderObj.value("id").toString().toLower();
    info.loaderVersion = loaderObj.value("version").toString();
  }
  else if (loaderVal.isString())
  {
    extractLoaderInfo(loaderVal.toString(), info.loaderType, info.loaderVersion);
  }

  // files
  QJsonArray files = root.value("files").toArray();
  for (const auto& fileVal : files)
  {
    QJsonObject fileObj = fileVal.toObject();
    ModInfo mod;
    mod.name = fileObj.value("name").toString();
    mod.downloadUrl = fileObj.value("url").toString();
    mod.required = fileObj.value("required").toBool(true);

    if (fileObj.contains("projectID"))
      mod.projectId = QString::number(fileObj.value("projectID").toInt());
    if (fileObj.contains("fileID"))
      mod.fileId = QString::number(fileObj.value("fileID").toInt());

    info.mods.append(mod);
  }

  info.overridesPath = root.value("overrides").toString();
  return info;
}

static ModpackInfo parseGeneric(ZipReader& zip, const QString& zipFilePath)
{
  ModpackInfo info;
  info.type = ModpackType::Generic;

  // Use zip filename as modpack name
  QFileInfo fi(zipFilePath);
  info.name = fi.completeBaseName();

  // Scan for known Minecraft directory structures
  QStringList foundDirs;
  if (!zip.findFile("mods/").isEmpty())
    foundDirs << "mods";
  if (!zip.findFile("config/").isEmpty())
    foundDirs << "config";
  if (!zip.findFile("scripts/").isEmpty())
    foundDirs << "scripts";
  if (!zip.findFile("resourcepacks/").isEmpty())
    foundDirs << "resourcepacks";
  if (!zip.findFile("shaderpacks/").isEmpty())
    foundDirs << "shaderpacks";

  if (!foundDirs.isEmpty())
  {
    info.description = QString("包含: %1").arg(foundDirs.join(", "));
  }

  info.overridesPath = QString(); // root of zip
  return info;
}

// ============================================================================
// ModpackDetector implementation
// ============================================================================

ModpackType ModpackDetector::detect(const QString& zipFilePath)
{
  ZipReader zip;
  if (!zip.open(zipFilePath))
    return ModpackType::Unknown;

  // Check in priority order - most specific format markers first

  if (zip.hasFile("modrinth.index.json"))
    return ModpackType::Modrinth;

  if (zip.hasFile("mmc-pack.json") || zip.hasFile("instance.cfg"))
    return ModpackType::MultiMC;

  if (zip.hasFile("mcbbs.pack.json"))
    return ModpackType::MCBBS;

  if (zip.hasFile("modpack.json") || zip.hasFile("pack.json"))
  {
    // Verify it's HMCL format (not a random modpack.json)
    QByteArray data = zip.extractFile("modpack.json");
    if (data.isEmpty())
      data = zip.extractFile("pack.json");

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();
    // HMCL has gameVersion field at top level, or a modLoader field
    if (obj.contains("gameVersion") || obj.contains("modLoader"))
      return ModpackType::HMCL;
  }

  if (zip.hasFile("manifest.json"))
  {
    // Verify it's CurseForge format
    QByteArray data = zip.extractFile("manifest.json");
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();
    if (obj.contains("minecraft") && obj.contains("manifestType"))
      return ModpackType::CurseForge;
  }

  // Check for generic modpack structure (has mods/ directory)
  QString modsDir = zip.findFile("mods/");
  if (!modsDir.isEmpty())
    return ModpackType::Generic;

  // Valid zip but no known format - treat as generic
  return ModpackType::Generic;
}

ModpackInfo ModpackDetector::parse(const QString& zipFilePath)
{
  ModpackType type = detect(zipFilePath);

  if (type == ModpackType::Unknown)
  {
    ModpackInfo info;
    info.type = ModpackType::Unknown;
    QFileInfo fi(zipFilePath);
    info.name = fi.completeBaseName();
    return info;
  }

  ZipReader zip;
  if (!zip.open(zipFilePath))
  {
    ModpackInfo info;
    info.type = ModpackType::Unknown;
    QFileInfo fi(zipFilePath);
    info.name = fi.completeBaseName();
    return info;
  }

  switch (type)
  {
  case ModpackType::CurseForge:
    return parseCurseForge(zip);
  case ModpackType::Modrinth:
    return parseModrinth(zip);
  case ModpackType::MultiMC:
    return parseMultiMC(zip);
  case ModpackType::MCBBS:
    return parseMCBBS(zip);
  case ModpackType::HMCL:
    return parseHMCL(zip);
  case ModpackType::Generic:
    return parseGeneric(zip, zipFilePath);
  default:
    break;
  }

  ModpackInfo info;
  info.type = ModpackType::Unknown;
  return info;
}

} // namespace modpack