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
  quint32 m_cdOffset = 0;
};

bool ZipWriter::open(const QString& filePath)
{
  m_file.setFileName(filePath);
  if (!m_file.open(QIODevice::WriteOnly))
    return false;

  m_entries.clear();
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
  QByteArray localHeader;
  {
    QDataStream ds(&localHeader, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << LOCAL_FILE_HEADER_SIG;
    ds << static_cast<quint16>(20);  // version needed (2.0)
    ds << static_cast<quint16>(0);   // general purpose flag
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

  QByteArray fileNameBytes = dirPath.toUtf8();

  quint32 headerOffset = static_cast<quint32>(m_file.pos());

  // Write local file header for directory
  QByteArray localHeader;
  {
    QDataStream ds(&localHeader, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << LOCAL_FILE_HEADER_SIG;
    ds << static_cast<quint16>(20);
    ds << static_cast<quint16>(0);
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
      ds << static_cast<quint16>(0);   // flag
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

// CRC32 helper (not available in QCryptographicHash)
static quint32 crc32Block(const QByteArray& data)
{
  static const quint32 table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6B20C, 0x74B1B29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB30A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
  };
  quint32 crc = 0xFFFFFFFF;
  for (const char c : data)
    crc = table[(crc ^ static_cast<quint8>(c)) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFF;
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

      // Ensure parent directories exist in zip
      QString parentDir = QFileInfo(zipPath).path();
      if (!parentDir.isEmpty() && parentDir != QStringLiteral("."))
      {
        QStringList dirParts = parentDir.split('/');
        QString accumulated;
        for (const QString& part : dirParts)
        {
          if (part.isEmpty())
            continue;
          accumulated += part + "/";
          zip.addDirectory(QStringLiteral("overrides/") + accumulated);
        }
      }

      zip.addFile(zipPath, fileData);

      ++processed;
      int percent = 10 + (processed * 80 / total);
      emit exportProgressChanged(percent, tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  zip.close();
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

      // Ensure parent directories exist in zip
      QString parentDir = QFileInfo(zipPath).path();
      if (!parentDir.isEmpty() && parentDir != QStringLiteral("."))
      {
        QStringList dirParts = parentDir.split('/');
        QString accumulated;
        for (const QString& part : dirParts)
        {
          if (part.isEmpty())
            continue;
          accumulated += part + "/";
          zip.addDirectory(QStringLiteral(".minecraft/") + accumulated);
        }
      }

      zip.addFile(zipPath, fileData);

      ++processed;
      int percent = 10 + (processed * 80 / total);
      emit exportProgressChanged(percent, tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  zip.close();
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

      // Ensure parent directories exist in zip
      QString parentDir = QFileInfo(zipPath).path();
      if (!parentDir.isEmpty() && parentDir != QStringLiteral("."))
      {
        QStringList dirParts = parentDir.split('/');
        QString accumulated;
        for (const QString& part : dirParts)
        {
          if (part.isEmpty())
            continue;
          accumulated += part + "/";
          zip.addDirectory(QStringLiteral("overrides/") + accumulated);
        }
      }

      zip.addFile(zipPath, fileData);

      ++processed;
      int percent = 30 + (processed * 60 / total);
      emit exportProgressChanged(percent, tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  zip.close();
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

  // Separate mod files from non-mod files
  QStringList modFiles;
  QStringList overrideFiles;

  for (const QString& relPath : m_selectedFiles)
  {
    if (relPath.startsWith(QStringLiteral("mods/"), Qt::CaseInsensitive))
      modFiles.append(relPath);
    else
      overrideFiles.append(relPath);
  }

  // Build modrinth.index.json
  QJsonObject index;
  index["formatVersion"] = 1;
  index["game"] = QStringLiteral("minecraft");
  index["versionId"] = m_exportInfo.version;
  index["name"] = m_exportInfo.name;
  if (!m_exportInfo.description.isEmpty())
    index["summary"] = m_exportInfo.description;

  // Files array (mods only - not packed into client-overrides)
  QJsonArray filesArray;
  int totalMods = modFiles.size();
  int processedMods = 0;

  for (const QString& relPath : modFiles)
  {
    QString fullPath = m_instancePath + "/" + relPath;
    QFileInfo fi(fullPath);

    QByteArray sha1 = calculateSha1(fullPath);
    QByteArray sha512 = calculateSha512(fullPath);
    if (sha1.isEmpty() || sha512.isEmpty())
    {
      ++processedMods;
      continue;
    }

    QJsonObject fileObj;
    fileObj["path"] = relPath;

    QJsonObject hashes;
    hashes["sha1"] = QString::fromLatin1(sha1.toHex());
    hashes["sha512"] = QString::fromLatin1(sha512.toHex());
    fileObj["hashes"] = hashes;

    QJsonObject env;
    env["client"] = QStringLiteral("required");
    fileObj["env"] = env;

    fileObj["downloads"] = QJsonArray();
    fileObj["fileSize"] = static_cast<qint64>(fi.size());

    filesArray.append(fileObj);

    ++processedMods;
    int percent = processedMods * 30 / qMax(totalMods, 1);
    emit exportProgressChanged(percent, tr("正在计算模组哈希 (%1/%2)...").arg(processedMods).arg(totalMods));
  }

  index["files"] = filesArray;

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

  emit exportProgressChanged(30, tr("正在创建整合包文件..."));

  ZipWriter zip;
  if (!zip.open(m_outputPath))
  {
    emit exportFinished(false, QString(), tr("无法创建输出文件：%1").arg(m_outputPath));
    return;
  }

  zip.addFile(QStringLiteral("modrinth.index.json"), indexData, false);

  // Add non-mod files under "client-overrides/"
  if (!overrideFiles.isEmpty())
  {
    int total = overrideFiles.size();
    int processed = 0;

    zip.addDirectory(QStringLiteral("client-overrides"));

    for (const QString& relPath : overrideFiles)
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

      // Ensure parent directories exist in zip
      QString parentDir = QFileInfo(zipPath).path();
      if (!parentDir.isEmpty() && parentDir != QStringLiteral("."))
      {
        QStringList dirParts = parentDir.split('/');
        QString accumulated;
        for (const QString& part : dirParts)
        {
          if (part.isEmpty())
            continue;
          accumulated += part + "/";
          zip.addDirectory(QStringLiteral("client-overrides/") + accumulated);
        }
      }

      zip.addFile(zipPath, fileData);

      ++processed;
      int percent = 30 + (processed * 60 / total);
      emit exportProgressChanged(percent, tr("正在打包覆盖文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  zip.close();
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
      fileObj["name"] = true;
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

      // Ensure parent directories exist in zip
      QString parentDir = QFileInfo(zipPath).path();
      if (!parentDir.isEmpty() && parentDir != QStringLiteral("."))
      {
        QStringList dirParts = parentDir.split('/');
        QString accumulated;
        for (const QString& part : dirParts)
        {
          if (part.isEmpty())
            continue;
          accumulated += part + "/";
          zip.addDirectory(QStringLiteral("overrides/") + accumulated);
        }
      }

      zip.addFile(zipPath, fileData);

      ++processed;
      int percent = 80 + (processed * 15 / total);
      emit exportProgressChanged(percent, tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
    }
  }

  zip.close();
  emit exportProgressChanged(100, tr("导出完成"));
  emit exportFinished(true, m_outputPath, QString());
}

// ---------------------------------------------------------------------------
// BlockBox export
// ---------------------------------------------------------------------------

void ModpackExporter::exportAsBlockBox()
{
  // =========================================================================
  // Phase 1 - Scan (0-10%)
  // =========================================================================
  emit exportProgressChanged(0, tr("正在扫描文件..."));

  struct FileEntry
  {
    QString path;
    qint64 size = 0;
    QByteArray sha1;
  };
  QList<FileEntry> entries;

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

      QByteArray sha1 = calculateSha1(fullPath);
      if (sha1.isEmpty())
      {
        ++processed;
        continue;
      }

      FileEntry entry;
      entry.path = relPath;
      entry.size = fi.size();
      entry.sha1 = sha1;
      entries.append(entry);

      ++processed;
      int percent = processed * 10 / total;
      emit exportProgressChanged(percent, tr("正在扫描文件..."));
    }
  }

  if (entries.isEmpty())
  {
    emit exportFinished(false, QString(), tr("没有找到文件"));
    return;
  }

  // =========================================================================
  // Phase 2 - Metadata (10-30%)
  // =========================================================================
  emit exportProgressChanged(10, tr("正在生成元数据..."));

  QString gameVersion = detectGameVersion();
  QMap<QString, QString> allLoaders = detectAllLoaders();

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

  // Build JSON metadata
  QJsonObject meta;
  meta["blockboxVersion"] = 1;
  meta["name"] = m_exportInfo.name;
  meta["version"] = m_exportInfo.version;
  meta["author"] = m_exportInfo.author;
  meta["description"] = m_exportInfo.description;
  meta["gameVersion"] = gameVersion;

  // Loaders
  QJsonArray loadersArray;
  for (auto it = allLoaders.constBegin(); it != allLoaders.constEnd(); ++it)
  {
    QJsonObject loaderObj;
    loaderObj["id"] = it.key();
    loaderObj["version"] = it.value();
    loadersArray.append(loaderObj);
  }
  meta["loaders"] = loadersArray;

  // Modules
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

  // Files - compute offsets later during packing
  QJsonArray filesArray;
  for (const FileEntry& entry : entries)
  {
    QJsonObject fileObj;
    fileObj["path"] = entry.path;
    fileObj["offset"] = 0;  // placeholder
    fileObj["size"] = entry.size;
    fileObj["sha1"] = QString::fromLatin1(entry.sha1.toHex());
    fileObj["compressed"] = false;  // placeholder
    filesArray.append(fileObj);
  }
  meta["files"] = filesArray;

  QJsonDocument metaDoc(meta);
  QByteArray metaBytes = metaDoc.toJson(QJsonDocument::Compact);

  emit exportProgressChanged(30, tr("正在生成元数据..."));

  // =========================================================================
  // Phase 3 - Pack (30-90%)
  // =========================================================================
  emit exportProgressChanged(30, tr("正在打包文件..."));
  emit exportProgressChanged(30, tr("正在打包文件 (%1/%2)...").arg(0).arg(entries.size()));

  QFile outFile(m_outputPath);
  if (!outFile.open(QIODevice::WriteOnly))
  {
    emit exportFinished(false, QString(), tr("无法创建输出文件：%1").arg(m_outputPath));
    return;
  }

  // Write header: magic "BBOX" + version + metadata_len (placeholder)
  {
    QDataStream ds(&outFile);
    ds.setByteOrder(QDataStream::LittleEndian);
    const quint8 magic[4] = { 'B', 'B', 'O', 'X' };
    ds.writeRawData(reinterpret_cast<const char*>(magic), 4);
    ds << static_cast<quint32>(1);         // version
    ds << static_cast<quint32>(0);         // metadata_len placeholder
  }

  // Write metadata JSON
  qint64 metaStartPos = outFile.pos();
  outFile.write(metaBytes);
  qint64 metaEndPos = outFile.pos();

  // Go back and fill metadata_len
  {
    quint32 metaLen = static_cast<quint32>(metaEndPos - metaStartPos);
    outFile.seek(8);  // after magic(4) + version(4)
    QDataStream ds(&outFile);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << metaLen;
    outFile.seek(metaEndPos);
  }

  // Track offsets for JSON update
  QList<qint64> fileOffsets;
  QList<bool> fileCompressed;
  // 原实现将全部文件数据累积到 allDataBlocks 用于最终 CRC 计算，大整合包会耗尽内存。
  // 改为循环内增量计算 CRC32（zlib 支持流式 crc32(crc, data, len)）。
  quint32 runningCrc = 0;

  // Write file data blocks
  int total = entries.size();
  int processed = 0;

  for (const FileEntry& entry : entries)
  {
    QString fullPath = m_instancePath + "/" + entry.path;
    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly))
    {
      ++processed;
      continue;
    }

    QByteArray fileData = file.readAll();
    file.close();

    QString ext = QFileInfo(entry.path).suffix();
    bool isCompressed = isAlreadyCompressed(ext);

    QByteArray writeData;
    bool compressed = false;

    if (isCompressed)
    {
      // Already compressed, store as-is
      writeData = fileData;
      compressed = false;
    }
    else
    {
      // Try deflate compression for text/config files
      static const QStringList textExts = {
        "json", "cfg", "txt", "properties", "toml", "lang", "info",
        "mcmeta", "nbt", "dat", "xml", "yml", "yaml", "html", "css", "js"
      };

      if (textExts.contains(ext.toLower()))
      {
        QByteArray deflated = compressDeflate(fileData);
        if (!deflated.isEmpty() && deflated.size() < fileData.size())
        {
          writeData = deflated;
          compressed = true;
        }
        else
        {
          writeData = fileData;
          compressed = false;
        }
      }
      else
      {
        writeData = fileData;
        compressed = false;
      }
    }

    qint64 offset = outFile.pos();
    fileOffsets.append(offset);
    fileCompressed.append(compressed);

    outFile.write(writeData);
    runningCrc = crc32(runningCrc,
                       reinterpret_cast<const Bytef*>(writeData.constData()),
                       static_cast<uInt>(writeData.size()));

    ++processed;
    int percent = 30 + (processed * 60 / total);
    emit exportProgressChanged(percent, tr("正在打包文件 (%1/%2)...").arg(processed).arg(total));
  }

  // Update file offsets and compressed flags in the JSON metadata
  // We need to rebuild the metadata with correct offsets
  {
    QJsonArray updatedFilesArray;
    for (int i = 0; i < entries.size(); ++i)
    {
      QJsonObject fileObj;
      fileObj["path"] = entries[i].path;
      fileObj["offset"] = fileOffsets.value(i, 0);
      fileObj["size"] = entries[i].size;
      fileObj["sha1"] = QString::fromLatin1(entries[i].sha1.toHex());
      fileObj["compressed"] = fileCompressed.value(i, false);
      updatedFilesArray.append(fileObj);
    }
    meta["files"] = updatedFilesArray;

    QJsonDocument updatedMetaDoc(meta);
    QByteArray updatedMetaBytes = updatedMetaDoc.toJson(QJsonDocument::Compact);

    // Rewrite: seek to beginning, write header, write updated metadata
    outFile.seek(0);
    {
      QDataStream ds(&outFile);
      ds.setByteOrder(QDataStream::LittleEndian);
      const quint8 magic[4] = { 'B', 'B', 'O', 'X' };
      ds.writeRawData(reinterpret_cast<const char*>(magic), 4);
      ds << static_cast<quint32>(1);  // version
      ds << static_cast<quint32>(updatedMetaBytes.size());
    }
    outFile.write(updatedMetaBytes);

    // Seek to end for footer
    outFile.seek(outFile.size());
  }

  // =========================================================================
  // Phase 4 - Verify (90-95%)
  // =========================================================================
  emit exportProgressChanged(90, tr("正在校验..."));

  quint32 crc = runningCrc;

  // Write footer: magic "BBOX" + CRC32
  {
    QDataStream ds(&outFile);
    ds.setByteOrder(QDataStream::LittleEndian);
    const quint8 magic[4] = { 'B', 'B', 'O', 'X' };
    ds.writeRawData(reinterpret_cast<const char*>(magic), 4);
    ds << crc;
  }

  emit exportProgressChanged(95, tr("正在校验..."));

  // =========================================================================
  // Phase 5 - Done (95-100%)
  // =========================================================================
  outFile.close();
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

QString ModpackExporter::detectGameVersion() const
{
  // Scan versions directory for Minecraft version JSON files
  QString versionsDir = m_instancePath + "/versions";
  QDir dir(versionsDir);
  if (!dir.exists())
    return QString();

  QStringList jsonFiles = dir.entryList(QStringList() << "*.json", QDir::Files);
  if (jsonFiles.isEmpty())
    return QString();

  // Look for the vanilla Minecraft version (not a loader version)
  for (const QString& fileName : jsonFiles)
  {
    QString filePath = versionsDir + "/" + fileName;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
      continue;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject())
      continue;

    QJsonObject obj = doc.object();

    // Skip loader-specific versions
    QString versionId = obj["id"].toString();
    QString inheritsFrom = obj["inheritsFrom"].toString();
    QString type = obj["type"].toString();

    // If it's a vanilla release/snapshot, use this version id
    if (type == QStringLiteral("release") || type == QStringLiteral("snapshot"))
    {
      if (inheritsFrom.isEmpty())
        return versionId;
    }

    // If it inherits from vanilla, use the inherited version
    if (!inheritsFrom.isEmpty())
    {
      // Check if the inherited version is a vanilla version
      // For simplicity, return inheritsFrom directly
      return inheritsFrom;
    }
  }

  // Fallback: return the first version id found
  for (const QString& fileName : jsonFiles)
  {
    QString filePath = versionsDir + "/" + fileName;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
      continue;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isObject())
    {
      QString versionId = doc.object()["id"].toString();
      if (!versionId.isEmpty())
        return versionId;
    }
  }

  return QString();
}

QString ModpackExporter::detectLoaderType() const
{
  QString versionsDir = m_instancePath + "/versions";
  QDir dir(versionsDir);
  if (!dir.exists())
    return QString();

  QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& entry : entries)
  {
    QString lower = entry.toLower();
    if (lower.startsWith(QStringLiteral("forge-")) && !lower.contains(QStringLiteral("neoforge")))
      return QStringLiteral("Forge");
    if (lower.startsWith(QStringLiteral("fabric-loader-")))
      return QStringLiteral("Fabric");
    if (lower.startsWith(QStringLiteral("neoforge-")))
      return QStringLiteral("NeoForge");
    if (lower.startsWith(QStringLiteral("liteloader-")))
      return QStringLiteral("LiteLoader");
    if (lower.startsWith(QStringLiteral("optifine-")))
      return QStringLiteral("OptiFine");
    if (lower.startsWith(QStringLiteral("quilt-loader-")))
      return QStringLiteral("Quilt");
  }

  // Also check JSON files for loader info
  QStringList jsonFiles = dir.entryList(QStringList() << "*.json", QDir::Files);
  for (const QString& fileName : jsonFiles)
  {
    QString lower = fileName.toLower();
    if (lower.startsWith(QStringLiteral("forge-")) && !lower.contains(QStringLiteral("neoforge")))
      return QStringLiteral("Forge");
    if (lower.startsWith(QStringLiteral("fabric-loader-")))
      return QStringLiteral("Fabric");
    if (lower.startsWith(QStringLiteral("neoforge-")))
      return QStringLiteral("NeoForge");
    if (lower.startsWith(QStringLiteral("liteloader-")))
      return QStringLiteral("LiteLoader");
    if (lower.startsWith(QStringLiteral("optifine-")))
      return QStringLiteral("OptiFine");
    if (lower.startsWith(QStringLiteral("quilt-loader-")))
      return QStringLiteral("Quilt");
  }

  // Also check JSON content for loader detection
  for (const QString& fileName : jsonFiles)
  {
    QString filePath = versionsDir + "/" + fileName;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
      continue;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject())
      continue;

    QJsonObject obj = doc.object();
    QString id = obj["id"].toString().toLower();

    if (id.contains(QStringLiteral("liteloader")))
      return QStringLiteral("LiteLoader");
    if (id.contains(QStringLiteral("optifine")))
      return QStringLiteral("OptiFine");
    if (id.contains(QStringLiteral("quilt-loader")))
      return QStringLiteral("Quilt");
  }

  return QString();
}

QString ModpackExporter::detectLoaderVersion() const
{
  QString versionsDir = m_instancePath + "/versions";
  QDir dir(versionsDir);
  if (!dir.exists())
    return QString();

  QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& entry : entries)
  {
    QString lower = entry.toLower();
    if (lower.startsWith(QStringLiteral("forge-")) && !lower.contains(QStringLiteral("neoforge")))
      return entry.mid(6);  // "forge-".length() == 6
    if (lower.startsWith(QStringLiteral("fabric-loader-")))
      return entry.mid(14); // "fabric-loader-".length() == 14
    if (lower.startsWith(QStringLiteral("neoforge-")))
      return entry.mid(9);  // "neoforge-".length() == 9
    if (lower.startsWith(QStringLiteral("liteloader-")))
      return entry.mid(11); // "liteloader-".length() == 11
    if (lower.startsWith(QStringLiteral("optifine-")))
      return entry.mid(9);  // "optifine-".length() == 9
    if (lower.startsWith(QStringLiteral("quilt-loader-")))
      return entry.mid(13); // "quilt-loader-".length() == 13
  }

  // Also check JSON files
  QStringList jsonFiles = dir.entryList(QStringList() << "*.json", QDir::Files);
  for (const QString& fileName : jsonFiles)
  {
    QString lower = fileName.toLower();
    QString baseName = fileName;
    if (baseName.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
      baseName.chop(5);

    QString lowerBase = baseName.toLower();
    if (lowerBase.startsWith(QStringLiteral("forge-")) && !lowerBase.contains(QStringLiteral("neoforge")))
      return baseName.mid(6);
    if (lowerBase.startsWith(QStringLiteral("fabric-loader-")))
      return baseName.mid(14);
    if (lowerBase.startsWith(QStringLiteral("neoforge-")))
      return baseName.mid(9);
    if (lowerBase.startsWith(QStringLiteral("liteloader-")))
      return baseName.mid(11);
    if (lowerBase.startsWith(QStringLiteral("optifine-")))
      return baseName.mid(9);
    if (lowerBase.startsWith(QStringLiteral("quilt-loader-")))
      return baseName.mid(13);
  }

  return QString();
}

QMap<QString, QString> ModpackExporter::detectAllLoaders() const
{
  QMap<QString, QString> loaders;
  QString versionsDir = m_instancePath + "/versions";
  QDir dir(versionsDir);
  if (!dir.exists())
    return loaders;

  // Scan directory names for loader versions
  QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& entry : entries)
  {
    QString lower = entry.toLower();
    if (lower.startsWith(QStringLiteral("forge-")) && !lower.contains(QStringLiteral("neoforge")))
      loaders[QStringLiteral("forge")] = entry.mid(6);
    else if (lower.startsWith(QStringLiteral("fabric-loader-")))
      loaders[QStringLiteral("fabric")] = entry.mid(14);
    else if (lower.startsWith(QStringLiteral("neoforge-")))
      loaders[QStringLiteral("neoforge")] = entry.mid(9);
    else if (lower.startsWith(QStringLiteral("liteloader-")))
      loaders[QStringLiteral("liteloader")] = entry.mid(11);
    else if (lower.startsWith(QStringLiteral("optifine-")))
      loaders[QStringLiteral("optifine")] = entry.mid(9);
    else if (lower.startsWith(QStringLiteral("quilt-loader-")))
      loaders[QStringLiteral("quilt")] = entry.mid(13);
  }

  // Also scan JSON file names for loader versions
  QStringList jsonFiles = dir.entryList(QStringList() << "*.json", QDir::Files);
  for (const QString& fileName : jsonFiles)
  {
    QString lower = fileName.toLower();
    QString baseName = fileName;
    if (baseName.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
      baseName.chop(5);

    QString lowerBase = baseName.toLower();
    if (lowerBase.startsWith(QStringLiteral("forge-")) && !lowerBase.contains(QStringLiteral("neoforge"))
        && !loaders.contains(QStringLiteral("forge")))
      loaders[QStringLiteral("forge")] = baseName.mid(6);
    else if (lowerBase.startsWith(QStringLiteral("fabric-loader-"))
             && !loaders.contains(QStringLiteral("fabric")))
      loaders[QStringLiteral("fabric")] = baseName.mid(14);
    else if (lowerBase.startsWith(QStringLiteral("neoforge-"))
             && !loaders.contains(QStringLiteral("neoforge")))
      loaders[QStringLiteral("neoforge")] = baseName.mid(9);
    else if (lowerBase.startsWith(QStringLiteral("liteloader-"))
             && !loaders.contains(QStringLiteral("liteloader")))
      loaders[QStringLiteral("liteloader")] = baseName.mid(11);
    else if (lowerBase.startsWith(QStringLiteral("optifine-"))
             && !loaders.contains(QStringLiteral("optifine")))
      loaders[QStringLiteral("optifine")] = baseName.mid(9);
    else if (lowerBase.startsWith(QStringLiteral("quilt-loader-"))
             && !loaders.contains(QStringLiteral("quilt")))
      loaders[QStringLiteral("quilt")] = baseName.mid(13);
  }

  // Also check JSON content for loaders not yet detected
  for (const QString& fileName : jsonFiles)
  {
    QString filePath = versionsDir + "/" + fileName;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
      continue;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject())
      continue;

    QJsonObject obj = doc.object();
    QString id = obj["id"].toString().toLower();
    QString version = obj["version"].toString();
    if (version.isEmpty())
      version = obj["name"].toString();

    if (id.contains(QStringLiteral("liteloader")) && !loaders.contains(QStringLiteral("liteloader")))
      loaders[QStringLiteral("liteloader")] = version;
    if (id.contains(QStringLiteral("optifine")) && !loaders.contains(QStringLiteral("optifine")))
      loaders[QStringLiteral("optifine")] = version;
    if (id.contains(QStringLiteral("quilt-loader")) && !loaders.contains(QStringLiteral("quilt")))
      loaders[QStringLiteral("quilt")] = version;
  }

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

bool ModpackExporter::isAlreadyCompressed(const QString& ext) const
{
  static const QStringList compressed = {
    "jar", "zip", "png", "jpg", "jpeg", "gif", "webp", "ogg", "mp3", "mp4",
    "webm", "gz", "bz2", "xz", "lz4", "lzma", "7z"
  };
  return compressed.contains(ext.toLower());
}

} // namespace modpack