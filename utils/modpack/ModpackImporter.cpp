/**
 * @file   ModpackImporter.cpp
 * @brief  整合包导入器类实现
 * @author BlockBox Team
 * @date   2026-06-10
 */

#include "ModpackImporter.h"
#include "../SettingsManager.h"
#include "../VersionDownloader.h"
#include "../forge/ForgeInstaller.h"
#include "../fabric/FabricInstaller.h"
#include "../NeoForgeInstaller.h"

#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <zlib.h>

namespace modpack {

// ============================================================================
// Anonymous-namespace ZipReader - duplicated from ModpackDetector.cpp to
// avoid linker conflicts. Used for override extraction only.
// ============================================================================

namespace {

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

private:
  bool readEocd(qint64& centralDirOffset, quint16& totalEntries);
  bool readCentralDirectory(qint64 centralDirOffset, quint16 totalEntries);
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
  qint64 fileSize = m_file.size();
  if (fileSize < 22)
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
      ds.skipRawData(8);

      quint16 entryCount = 0;
      quint32 cdSize = 0;
      quint32 cdOffset = 0;
      ds >> entryCount >> cdSize >> cdOffset;

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

  const int cdEntryBaseSize = 46;
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

    m_file.read(extraFieldLength + fileCommentLength);

    Entry entry;
    entry.fileName = QString::fromUtf8(fileNameBytes);
    entry.compressionMethod = compressionMethod;
    entry.compressedSize = compressedSize;
    entry.uncompressedSize = uncompressedSize;
    entry.localHeaderOffset = localHeaderOffset;

    m_entries.append(entry);

    QString normalized = entry.fileName;
    if (normalized.endsWith('/'))
      normalized.chop(1);

    m_entryMap[entry.fileName] = entry;
    m_entryMap[normalized] = entry;
  }
  return true;
}

QByteArray ZipReader::extractFile(const QString& fileName)
{
  if (!m_entryMap.contains(fileName))
    return QByteArray();

  const Entry& entry = m_entryMap[fileName];

  if (!m_file.seek(entry.localHeaderOffset))
    return QByteArray();

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
  ds.skipRawData(8);
  ds.skipRawData(8);
  ds >> fileNameLength >> extraFieldLength;

  if (signature != 0x04034b50)
    return QByteArray();

  m_file.read(fileNameLength);
  m_file.read(extraFieldLength);

  QByteArray compressedData = m_file.read(entry.compressedSize);
  if (compressedData.size() < static_cast<int>(entry.compressedSize))
    return QByteArray();

  if (compressionMethod == 0)
    return compressedData;

  if (compressionMethod == 8)
    return decompressDeflate(compressedData, entry.uncompressedSize);

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
      return QByteArray();
    result.resize(strm.total_out);
  }
  else
  {
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

} // anonymous namespace

// ============================================================================
// ModpackImporter implementation
// ============================================================================

ModpackImporter::ModpackImporter(QObject* parent)
  : QObject(parent)
  , m_networkManager(new QNetworkAccessManager(this))
  , m_currentModReply(nullptr)
  , m_currentModIndex(0)
  , m_totalMods(0)
  , m_modsDownloaded(0)
  , m_modsFailed(0)
{
}

ModpackImporter::~ModpackImporter()
{
}

void ModpackImporter::startInstall()
{
  if (m_instanceName.isEmpty())
    m_instanceName = m_info.name;

  if (m_instancePath.isEmpty())
  {
    QString basePath = SettingsManager::instance()->getDefaultInstancePath();
    m_instancePath = basePath + "/" + m_instanceName;
  }

  QDir dir;
  if (!dir.mkpath(m_instancePath))
  {
    failInstall(tr("无法创建实例目录：%1").arg(m_instancePath));
    return;
  }

  emit stageChanged(tr("正在下载 Minecraft 版本..."));
  emit installProgressChanged(0, tr("准备下载 Minecraft %1").arg(m_info.gameVersion));

  installVersion();
}

void ModpackImporter::installVersion()
{
  auto* vd = VersionDownloader::instance();

  QMetaObject::Connection conn1 = connect(vd, &VersionDownloader::downloadCompleted,
    this, [this, vd](const QString& /*versionId*/, const QString& /*actualVersionPath*/)
    {
      disconnect(vd, &VersionDownloader::downloadCompleted, this, nullptr);
      disconnect(vd, &VersionDownloader::downloadFailed, this, nullptr);

      emit installProgressChanged(30, tr("Minecraft %1 下载完成").arg(m_info.gameVersion));

      if (!m_info.loaderType.isEmpty())
      {
        emit stageChanged(tr("正在安装加载器 %1...").arg(m_info.loaderType));
        installLoader();
      }
      else
      {
        emit stageChanged(tr("正在下载模组..."));
        installMods();
      }
    });

  connect(vd, &VersionDownloader::downloadFailed,
    this, [this, vd](const QString& error)
    {
      disconnect(vd, &VersionDownloader::downloadCompleted, this, nullptr);
      disconnect(vd, &VersionDownloader::downloadFailed, this, nullptr);
      failInstall(tr("Minecraft 版本下载失败：%1").arg(error));
    });

  vd->downloadVanilla(m_info.gameVersion, m_instancePath, m_instanceName);
}

void ModpackImporter::installLoader()
{
  QString loaderLower = m_info.loaderType.toLower();

  if (loaderLower == QStringLiteral("forge"))
  {
    auto* installer = ForgeInstaller::instance();

    connect(installer, &ForgeInstaller::installCompleted,
      this, [this, installer](const QString& /*taskId*/)
      {
        disconnect(installer, &ForgeInstaller::installCompleted, this, nullptr);
        disconnect(installer, &ForgeInstaller::installFailed, this, nullptr);

        emit installProgressChanged(60, tr("Forge %1 安装完成").arg(m_info.loaderVersion));
        emit stageChanged(tr("正在下载模组..."));
        installMods();
      });

    connect(installer, &ForgeInstaller::installFailed,
      this, [this, installer](const QString& /*taskId*/, const QString& error)
      {
        disconnect(installer, &ForgeInstaller::installCompleted, this, nullptr);
        disconnect(installer, &ForgeInstaller::installFailed, this, nullptr);
        failInstall(tr("Forge 安装失败：%1").arg(error));
      });

    installer->downloadForgeInstaller(m_info.gameVersion, m_info.loaderVersion,
                                      m_instancePath, m_instanceName);
  }
  else if (loaderLower == QStringLiteral("fabric"))
  {
    auto* installer = FabricInstaller::instance();

    connect(installer, &FabricInstaller::installCompleted,
      this, [this, installer](const QString& /*versionId*/)
      {
        disconnect(installer, &FabricInstaller::installCompleted, this, nullptr);
        disconnect(installer, &FabricInstaller::installFailed, this, nullptr);

        emit installProgressChanged(60, tr("Fabric %1 安装完成").arg(m_info.loaderVersion));
        emit stageChanged(tr("正在下载模组..."));
        installMods();
      });

    connect(installer, &FabricInstaller::installFailed,
      this, [this, installer](const QString& error)
      {
        disconnect(installer, &FabricInstaller::installCompleted, this, nullptr);
        disconnect(installer, &FabricInstaller::installFailed, this, nullptr);
        failInstall(tr("Fabric 安装失败：%1").arg(error));
      });

    installer->downloadFabricInstaller(m_info.gameVersion, m_info.loaderVersion,
                                       m_instancePath);
  }
  else if (loaderLower == QStringLiteral("neoforge"))
  {
    auto* installer = NeoForgeInstaller::instance();

    connect(installer, &NeoForgeInstaller::installCompleted,
      this, [this, installer](const QString& /*versionId*/)
      {
        disconnect(installer, &NeoForgeInstaller::installCompleted, this, nullptr);
        disconnect(installer, &NeoForgeInstaller::installFailed, this, nullptr);

        emit installProgressChanged(60, tr("NeoForge %1 安装完成").arg(m_info.loaderVersion));
        emit stageChanged(tr("正在下载模组..."));
        installMods();
      });

    connect(installer, &NeoForgeInstaller::installFailed,
      this, [this, installer](const QString& error)
      {
        disconnect(installer, &NeoForgeInstaller::installCompleted, this, nullptr);
        disconnect(installer, &NeoForgeInstaller::installFailed, this, nullptr);
        failInstall(tr("NeoForge 安装失败：%1").arg(error));
      });

    installer->downloadNeoForgeInstaller(m_info.gameVersion, m_info.loaderVersion,
                                         m_instancePath, m_instanceName);
  }
  else
  {
    // Unknown loader type - skip loader installation
    emit installProgressChanged(60, tr("跳过未知加载器：%1").arg(m_info.loaderType));
    emit stageChanged(tr("正在下载模组..."));
    installMods();
  }
}

void ModpackImporter::installMods()
{
  m_totalMods = m_info.mods.size();
  m_currentModIndex = 0;
  m_modsDownloaded = 0;
  m_modsFailed = 0;

  if (m_totalMods == 0)
  {
    emit installProgressChanged(75, tr("无模组需要下载"));
    emit stageChanged(tr("正在解压覆盖文件..."));
    extractOverrides();
    return;
  }

  QString modsDir = m_instancePath + "/mods";
  QDir dir;
  dir.mkpath(modsDir);

  emit installProgressChanged(62, tr("正在下载模组 (0/%1)...").arg(m_totalMods));
  downloadNextMod();
}

void ModpackImporter::downloadNextMod()
{
  if (m_currentModIndex >= m_totalMods)
  {
    int progress = 75;
    emit installProgressChanged(progress,
      tr("模组下载完成 (%1 成功, %2 失败)").arg(m_modsDownloaded).arg(m_modsFailed));
    emit stageChanged(tr("正在解压覆盖文件..."));
    extractOverrides();
    return;
  }

  const ModInfo& mod = m_info.mods.at(m_currentModIndex);
  QString url;

  if (!mod.downloadUrl.isEmpty())
  {
    url = mod.downloadUrl;
  }
  else if (!mod.projectId.isEmpty() && !mod.fileId.isEmpty())
  {
    url = getCurseForgeDownloadUrl(mod.projectId, mod.fileId);
  }

  if (url.isEmpty())
  {
    ++m_modsFailed;
    ++m_currentModIndex;
    downloadNextMod();
    return;
  }

  QNetworkRequest request{QUrl(url)};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);

  m_currentModReply = m_networkManager->get(request);

  connect(m_currentModReply, &QNetworkReply::finished, this, [this]()
  {
    if (!m_currentModReply)
      return;

    QString modsDir = m_instancePath + "/mods";

    if (m_currentModReply->error() == QNetworkReply::NoError)
    {
      QByteArray data = m_currentModReply->readAll();

      // Determine filename from Content-Disposition or URL
      QString fileName;
      QString contentDisposition =
        m_currentModReply->rawHeader("Content-Disposition");
      if (!contentDisposition.isEmpty())
      {
        int idx = contentDisposition.indexOf("filename=");
        if (idx >= 0)
        {
          fileName = contentDisposition.mid(idx + 9).trimmed();
          if (fileName.startsWith('"') && fileName.endsWith('"'))
            fileName = fileName.mid(1, fileName.length() - 2);
        }
      }

      if (fileName.isEmpty())
      {
        // Fall back to mod name or last segment of URL
        const ModInfo& mod = m_info.mods.at(m_currentModIndex);
        if (!mod.name.isEmpty())
          fileName = mod.name;
        else
        {
          QString urlPath = m_currentModReply->url().path();
          int lastSlash = urlPath.lastIndexOf('/');
          fileName = (lastSlash >= 0) ? urlPath.mid(lastSlash + 1) : "mod.jar";
        }

        // Ensure .jar extension
        if (!fileName.endsWith(".jar", Qt::CaseInsensitive))
          fileName += ".jar";
      }

      QString filePath = modsDir + "/" + fileName;
      QFile file(filePath);
      if (file.open(QIODevice::WriteOnly))
      {
        file.write(data);
        file.close();
        ++m_modsDownloaded;
      }
      else
      {
        ++m_modsFailed;
      }
    }
    else
    {
      ++m_modsFailed;
    }

    m_currentModReply->deleteLater();
    m_currentModReply = nullptr;

    ++m_currentModIndex;

    int pctBase = 62;
    int pctRange = 13;
    int progress = pctBase + (m_currentModIndex * pctRange / m_totalMods);
    emit installProgressChanged(progress,
      tr("正在下载模组 (%1/%2)...").arg(m_currentModIndex).arg(m_totalMods));

    downloadNextMod();
  });
}

void ModpackImporter::extractOverrides()
{
  if (m_zipPath.isEmpty() || !QFile::exists(m_zipPath))
  {
    emit installProgressChanged(90, tr("无覆盖文件需要解压"));
    createInstance();
    return;
  }

  ZipReader zip;
  if (!zip.open(m_zipPath))
  {
    emit installProgressChanged(90, tr("无法打开整合包文件，跳过覆盖文件解压"));
    createInstance();
    return;
  }

  QString prefixToExtract;
  QString prefixToStrip;

  switch (m_info.type)
  {
  case ModpackType::CurseForge:
    prefixToExtract = "overrides/";
    prefixToStrip = "overrides/";
    break;

  case ModpackType::MultiMC:
    prefixToExtract = "minecraft/";
    prefixToStrip = "minecraft/";
    break;

  case ModpackType::Modrinth:
    // Extract everything except the index json
    // No prefix filter, strip nothing
    prefixToExtract = QString();
    prefixToStrip = QString();
    break;

  case ModpackType::MCBBS:
    if (!m_info.overridesPath.isEmpty())
    {
      prefixToExtract = m_info.overridesPath;
      if (!prefixToExtract.endsWith('/'))
        prefixToExtract += '/';
      prefixToStrip = prefixToExtract;
    }
    break;

  case ModpackType::HMCL:
    if (!m_info.overridesPath.isEmpty())
    {
      prefixToExtract = m_info.overridesPath;
      if (!prefixToExtract.endsWith('/'))
        prefixToExtract += '/';
      prefixToStrip = prefixToExtract;
    }
    break;

  default:
    break;
  }

  int extractedCount = 0;
  const QList<ZipReader::Entry>& entries = zip.entries();

  for (const auto& entry : entries)
  {
    // Skip directories
    if (entry.fileName.endsWith('/'))
      continue;

    // Skip metadata files for Modrinth
    if (m_info.type == ModpackType::Modrinth)
    {
      if (entry.fileName == "modrinth.index.json")
        continue;
      if (entry.fileName == "pack.mrpack")
        continue;
    }

    // Filter by prefix if specified
    if (!prefixToExtract.isEmpty() && !entry.fileName.startsWith(prefixToExtract))
      continue;

    // Determine output path
    QString relativePath = entry.fileName;
    if (!prefixToStrip.isEmpty() && relativePath.startsWith(prefixToStrip))
      relativePath = relativePath.mid(prefixToStrip.length());

    if (relativePath.isEmpty())
      continue;

    QString destPath = m_instancePath + "/" + relativePath;

    // Create parent directory
    QFileInfo fi(destPath);
    QDir().mkpath(fi.absolutePath());

    // Extract file
    QByteArray data = zip.extractFile(entry.fileName);
    if (!data.isEmpty())
    {
      QFile outFile(destPath);
      if (outFile.open(QIODevice::WriteOnly))
      {
        outFile.write(data);
        outFile.close();
        ++extractedCount;
      }
    }
  }

  emit installProgressChanged(92,
    tr("已解压 %1 个覆盖文件").arg(extractedCount));
  createInstance();
}

void ModpackImporter::createInstance()
{
  QJsonObject instanceJson;
  instanceJson["name"] = m_instanceName;
  instanceJson["gameVersion"] = m_info.gameVersion;
  instanceJson["modpackType"] = m_info.typeName();
  instanceJson["loaderType"] = m_info.loaderType;
  instanceJson["loaderVersion"] = m_info.loaderVersion;
  instanceJson["modpackName"] = m_info.name;
  instanceJson["modpackVersion"] = m_info.version;

  QJsonDocument doc(instanceJson);
  QString jsonPath = m_instancePath + "/instance.json";
  QFile file(jsonPath);
  if (file.open(QIODevice::WriteOnly))
  {
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
  }

  emit installProgressChanged(100, tr("安装完成"));
  emit installFinished(true, m_instancePath);
}

void ModpackImporter::failInstall(const QString& error)
{
  emit installFinished(false, error);
}

QString ModpackImporter::getCurseForgeDownloadUrl(const QString& projectId,
                                                   const QString& fileId) const
{
  // CurseForge API download endpoint
  return QString("https://www.curseforge.com/api/v1/mods/%1/files/%2/download")
    .arg(projectId, fileId);
}

} // namespace modpack