/**
 * @file   ModpackImporter.cpp
 * @brief  整合包导入器类实现
 * @author BlockBox Team
 * @date   2026-06-10
 */

#include "ModpackImporter.h"
#include "../SettingsManager.h"
#include "../VersionDownloader.h"
#include "../download/DownloadMirrors.h"
#include "../net/Download.h"
#include "../net/ChecksumValidator.h"
#include "../net/NetworkAccess.h"
#include "../forge/ForgeInstaller.h"
#include "../fabric/FabricInstaller.h"
#include "../NeoForgeInstaller.h"
#include "../Safety.h"

#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QtConcurrent>

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
      // 必须跳过前 10 字节才到「总条目数」，跳 8 会把总条目数错并进 cdSize，
      // 导致 cdOffset 读成垃圾、整个 zip 识别失败
      ds.skipRawData(10);

      quint16 entryCount = 0;
      quint32 cdSize = 0;
      quint32 cdOffset = 0;
      ds >> entryCount >> cdSize >> cdOffset;

      centralDirOffset = cdOffset;
      totalEntries = entryCount;
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
    quint16 modTime = 0;
    quint16 modDate = 0;
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

    // 中央目录条目固定 46 字节，必须完整读完，否则后续字段全部错位
    ds >> signature >> versionMadeBy >> versionNeeded >> gpFlag
       >> compressionMethod
       >> modTime >> modDate
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
  : Task(parent)
  , m_networkManager(new QNetworkAccessManager(this))
  , m_currentModReply(nullptr)
  , m_totalMods(0)
  , m_modsDownloaded(0)
  , m_modsFailed(0)
{
}

ModpackImporter::~ModpackImporter()
{
}

void ModpackImporter::executeTask()
{
  if (m_instanceName.isEmpty())
    m_instanceName = m_info.name;

  // 实例名可能来自整合包元数据/用户输入，压成合法文件夹名，杜绝路径穿越
  m_instanceName = Safety::safeFileName(m_instanceName, QStringLiteral("instance"));

  // 实例列表只扫 <游戏根>/versions/* 一级目录，且版本 JSON 必须叫 <实例名>.json，
  // 否则导入完在启动器里根本看不到（对照 InstanceSelectPage / LoaderDetailPage 约定）
  m_gameRoot = SettingsManager::instance()->getDefaultInstancePath();
  if (m_instancePath.isEmpty())
    m_instancePath = m_gameRoot + "/versions/" + m_instanceName;

  if (m_info.gameVersion.trimmed().isEmpty())
  {
    failInstall(tr("整合包未提供游戏版本信息，请填写游戏版本后重试"));
    return;
  }

  // 目标实例已存在则拒绝，避免覆盖用户现有实例
  if (QFile::exists(m_instancePath + "/" + m_instanceName + ".json"))
  {
    failInstall(tr("实例已存在：%1，请更换实例名称").arg(m_instanceName));
    return;
  }

  // 只有主流加载器且带版本号才走加载器安装；其余（quilt/liteloader/缺版本号）
  // 降级为原版实例，内容照常解压，用户之后可在实例页手动装加载器
  const QString loaderLower = m_info.loaderType.toLower();
  m_useLoader = !m_info.loaderVersion.trimmed().isEmpty()
                && (loaderLower == QLatin1String("forge")
                    || loaderLower == QLatin1String("fabric")
                    || loaderLower == QLatin1String("neoforge"));

  emitStage(tr("正在下载 Minecraft 版本..."));
  emitProgress(0, tr("准备下载 Minecraft %1").arg(m_info.gameVersion));

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
      disconnect(vd, &VersionDownloader::downloadCancelled, this, nullptr);
      if (!isRunning())
        return;

      emitProgress(30, tr("Minecraft %1 下载完成").arg(m_info.gameVersion));

      if (m_useLoader)
      {
        emitStage(tr("正在安装加载器 %1...").arg(m_info.loaderType));
        installLoader();
      }
      else
      {
        if (!m_info.loaderType.isEmpty())
          emitStage(tr("跳过加载器 %1，按原版安装").arg(m_info.loaderType));
        emitStage(tr("正在下载模组..."));
        installMods();
      }
    });

  connect(vd, &VersionDownloader::downloadFailed,
    this, [this, vd](const QString& error)
    {
      disconnect(vd, &VersionDownloader::downloadCompleted, this, nullptr);
      disconnect(vd, &VersionDownloader::downloadFailed, this, nullptr);
      disconnect(vd, &VersionDownloader::downloadCancelled, this, nullptr);
      failInstall(tr("Minecraft 版本下载失败：%1").arg(error));
    });

  // 用户点「取消」时 VersionDownloader 只发 downloadCancelled，
  // 不补这条连接任务会永远停在 Running、进度框关不掉
  connect(vd, &VersionDownloader::downloadCancelled,
    this, [this, vd]()
    {
      disconnect(vd, &VersionDownloader::downloadCompleted, this, nullptr);
      disconnect(vd, &VersionDownloader::downloadFailed, this, nullptr);
      disconnect(vd, &VersionDownloader::downloadCancelled, this, nullptr);
      failInstall(tr("已取消"));
    });

  if (m_useLoader)
  {
    // 有加载器：原版基准必须落到 <游戏根>/versions/<mcVer>（加载器安装器在此基础上
    // 创建自己的版本目录，且加载器 JSON 的 inheritsFrom 也指向这里）。
    // 最终实例目录等加载器装完后定位（见 resolveLoaderVersionDir）。
    m_versionsBefore = QDir(m_gameRoot + "/versions")
                           .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    vd->downloadVanillaTo(m_info.gameVersion,
                          m_gameRoot + "/versions/" + m_info.gameVersion,
                          m_instanceName);
  }
  else
  {
    // 无加载器：原版直接落进实例目录，JSON 落成 <实例名>.json，实例列表可见可启动
    vd->downloadVanillaTo(m_info.gameVersion, m_instancePath, m_instanceName);
  }
}

void ModpackImporter::installLoader()
{
  const QString loaderLower = m_info.loaderType.toLower();

  // 加载器完成/失败/取消后统一收尾（在 GUI 线程执行）
  auto onLoaded = [this](bool success, const QString& error) {
    disconnect(ForgeInstaller::instance(), nullptr, this, nullptr);
    disconnect(FabricInstaller::instance(), nullptr, this, nullptr);
    disconnect(NeoForgeInstaller::instance(), nullptr, this, nullptr);
    m_activeLoaderType.clear();
    if (!isRunning())
      return;
    if (m_abortRequested)
    {
      failInstall(tr("已取消"));
      return;
    }
    if (!success)
    {
      failInstall(error);
      return;
    }

    // 定位加载器版本目录 → 定为本整合包实例目录
    const QString loaderDir = resolveLoaderVersionDir();
    if (loaderDir.isEmpty())
    {
      failInstall(tr("未能定位加载器安装目录，请重试或手动安装加载器"));
      return;
    }
    const QString loaderDirName = QFileInfo(loaderDir).fileName();
    const QString targetDir = m_gameRoot + "/versions/" + m_instanceName;

    if (loaderDirName == m_instanceName)
    {
      // 实例名恰好等于加载器目录名，直接采用
      m_instancePath = loaderDir;
    }
    else if (!m_versionsBefore.contains(loaderDirName))
    {
      // 本次新装出的加载器目录：改名为用户实例名（目录连同内部 JSON 一起走）
      const QString targetJson = targetDir + "/" + m_instanceName + ".json";
      if (QFile::exists(targetJson) || QDir(targetDir).exists())
      {
        failInstall(tr("实例已存在：%1，请更换实例名称").arg(m_instanceName));
        return;
      }
      if (!QDir().rename(loaderDir, targetDir))
      {
        failInstall(tr("重命名加载器版本目录失败"));
        return;
      }
      // 目录改名后，内部版本 JSON 也要改成 <实例名>.json，启动器按目录名找它
      const QStringList jsons = QDir(targetDir).entryList({"*.json"}, QDir::Files);
      bool jsonRenamed = false;
      for (const QString& j : jsons)
      {
        if (QFile::rename(targetDir + "/" + j, targetJson))
        {
          jsonRenamed = true;
          break;
        }
      }
      if (!jsonRenamed)
      {
        failInstall(tr("重命名加载器版本信息失败"));
        return;
      }
      m_instancePath = targetDir;
    }
    else
    {
      // 同版本加载器目录已被其他实例占用：复制 JSON 到独立实例目录，互不干扰
      if (!QDir().mkpath(targetDir)
          || QFile::exists(targetDir + "/" + m_instanceName + ".json"))
      {
        failInstall(tr("无法创建实例目录：%1").arg(targetDir));
        return;
      }
      if (!QFile::copy(loaderDir + "/" + loaderDirName + ".json",
                       targetDir + "/" + m_instanceName + ".json"))
      {
        failInstall(tr("复制加载器版本信息失败"));
        return;
      }
      m_instancePath = targetDir;
    }

    emitProgress(60, tr("%1 %2 安装完成").arg(m_info.loaderType).arg(m_info.loaderVersion));
    emitStage(tr("正在下载模组..."));
    installMods();
  };

  if (loaderLower == QLatin1String("forge"))
  {
    auto* installer = ForgeInstaller::instance();
    m_activeLoaderType = QStringLiteral("forge");
    connect(installer, &ForgeInstaller::installCompleted, this,
            [onLoaded](const QString&) { onLoaded(true, QString()); });
    connect(installer, &ForgeInstaller::installFailed, this,
            [onLoaded](const QString&, const QString& error) {
              onLoaded(false, tr("Forge 安装失败：%1").arg(error));
            });
    connect(installer, &ForgeInstaller::installCancelled, this,
            [onLoaded](const QString&) { onLoaded(false, tr("已取消")); });
    installer->downloadForgeInstaller(m_info.gameVersion, m_info.loaderVersion,
                                      m_gameRoot, m_instanceName);
  }
  else if (loaderLower == QLatin1String("fabric") || loaderLower == QLatin1String("quilt"))
  {
    auto* installer = FabricInstaller::instance();
    m_activeLoaderType = QStringLiteral("fabric");
    connect(installer, &FabricInstaller::installCompleted, this,
            [onLoaded](const QString&) { onLoaded(true, QString()); });
    connect(installer, &FabricInstaller::installFailed, this,
            [onLoaded](const QString& error) {
              onLoaded(false, tr("Fabric 安装失败：%1").arg(error));
            });
    connect(installer, &FabricInstaller::installCancelled, this,
            [onLoaded]() { onLoaded(false, tr("已取消")); });
    // Fabric 安装器内部是阻塞实现，放后台线程执行，避免卡死 GUI
    const QString mc = m_info.gameVersion;
    const QString fv = m_info.loaderVersion;
    const QString root = m_gameRoot;
    (void)QtConcurrent::run([installer, mc, fv, root]() {
      installer->downloadFabricInstaller(mc, fv, root);
    });
  }
  else if (loaderLower == QLatin1String("neoforge"))
  {
    auto* installer = NeoForgeInstaller::instance();
    m_activeLoaderType = QStringLiteral("neoforge");
    connect(installer, &NeoForgeInstaller::installCompleted, this,
            [onLoaded](const QString&) { onLoaded(true, QString()); });
    connect(installer, &NeoForgeInstaller::installFailed, this,
            [onLoaded](const QString& error) {
              onLoaded(false, tr("NeoForge 安装失败：%1").arg(error));
            });
    connect(installer, &NeoForgeInstaller::installCancelled, this,
            [onLoaded]() { onLoaded(false, tr("已取消")); });
    // NeoForge 安装器同样是阻塞实现
    const QString mc = m_info.gameVersion;
    const QString fv = m_info.loaderVersion;
    const QString root = m_gameRoot;
    const QString name = m_instanceName;
    (void)QtConcurrent::run([installer, mc, fv, root, name]() {
      installer->downloadNeoForgeInstaller(mc, fv, root, name);
    });
  }
}

QString ModpackImporter::resolveLoaderVersionDir() const
{
  const QDir vdir(m_gameRoot + "/versions");
  const QStringList after = vdir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

  // 1) 本次新装出来的目录（排除原版基准目录）
  for (const QString& name : after)
  {
    if (m_versionsBefore.contains(name) || name == m_info.gameVersion)
      continue;
    if (!QFile::exists(vdir.absoluteFilePath(name + "/" + name + ".json")))
      continue;
    return vdir.absoluteFilePath(name);
  }

  // 2) 同版本加载器目录已存在（被其他实例占用）：按 JSON id 含加载器版本号匹配
  if (!m_info.loaderVersion.isEmpty())
  {
    for (const QString& name : after)
    {
      QFile f(vdir.absoluteFilePath(name + "/" + name + ".json"));
      if (!f.open(QIODevice::ReadOnly))
        continue;
      const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
      f.close();
      if (obj.value("id").toString().contains(m_info.loaderVersion))
        return vdir.absoluteFilePath(name);
    }
  }
  return QString();
}

void ModpackImporter::installMods()
{
  m_totalMods = m_info.mods.size();
  m_modsDownloaded = 0;
  m_modsFailed = 0;
  m_serialModIndices.clear();
  m_serialPos = 0;

  if (m_totalMods == 0)
  {
    emitProgress(75, tr("无模组需要下载"));
    emitStage(tr("正在解压覆盖文件..."));
    extractOverrides();
    return;
  }

  QString modsDir = m_instancePath + "/mods";
  QDir dir;
  dir.mkpath(modsDir);

  emitProgress(62, tr("正在下载模组 (0/%1)...").arg(m_totalMods));

  // MultiMC net 层：带下载地址的文件（Modrinth 清单）全部放进一个 NetJob，
  // 并发下载；只有项目/文件 ID 的（CurseForge 清单）进串行队列逐个下。
  // 注意：NetJob 各 part 的回调只计数，串行推进只由 NetJob finished 后驱动，
  // 否则并发完成会把串行索引提前烧完、任务在下载未结束时就宣告成功。
  m_modJob = NetJob::Ptr(new NetJob(tr("整合包模组"), Net::networkAccessManager()));

  for (int i = 0; i < m_info.mods.size(); ++i)
  {
    const ModInfo& mod = m_info.mods.at(i);
    const QStringList candidates = buildModCandidates(mod);
    if (candidates.isEmpty())
    {
      // 无直接地址：CurseForge 旧路径（有项目/文件 ID），或两者皆无（记失败）
      m_serialModIndices.append(i);
      continue;
    }

    // mrpack 的 path 可能是 resourcepacks/xxx.zip 这类非 mods/ 路径，保留目录结构
    QString relTarget;
    const QString modPath = mod.name.trimmed();
    if (modPath.contains(QLatin1Char('/')) && !modPath.startsWith(QLatin1Char('/')))
      relTarget = modPath;
    else
      relTarget = QStringLiteral("mods/") + modFileName(mod, candidates);

    const QString filePath = QDir::cleanPath(m_instancePath + "/" + relTarget);
    if (!Safety::isPathInside(m_instancePath, filePath))
    {
      ++m_modsFailed;
      continue;
    }
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    auto dl = Net::Download::makeFile(QUrl(candidates.first()), filePath);
    for (int j = 1; j < candidates.size(); ++j)
      dl->addUrlFallback(QUrl(candidates.at(j)));

    // 清单校验和：sha1 优先，其次 sha512
    if (!mod.sha1.isEmpty())
    {
      dl->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1,
                                                  QByteArray::fromHex(mod.sha1.toLatin1())));
    }
    else if (!mod.sha512.isEmpty())
    {
      dl->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha512,
                                                  QByteArray::fromHex(mod.sha512.toLatin1())));
    }

    // 并发回调只更新计数与进度，不驱动串行状态机
    connect(dl.get(), &NetAction::succeeded, this, [this](int) {
      ++m_modsDownloaded;
      reportModProgress();
    });
    connect(dl.get(), &NetAction::failed, this, [this](int) {
      ++m_modsFailed;
      reportModProgress();
    });

    m_modJob->addNetAction(dl);
  }

  const bool hasDirectDownloads = m_modJob->size() > 0;

  connect(m_modJob.get(), &Task::finished, this, [this]() {
    m_modJob.reset();
    if (m_abortRequested)
    {
      failInstall(tr("已取消"));
      return;
    }
    // 直接下载全部结束后才轮到串行（CurseForge ID）队列，队列空则收尾
    downloadNextMod();
  });

  if (hasDirectDownloads)
  {
    m_modJob->start();
  }
  else
  {
    m_modJob.reset();
    downloadNextMod();
  }
}

void ModpackImporter::downloadNextMod()
{
  if (m_abortRequested)
  {
    failInstall(tr("已取消"));
    return;
  }

  // 串行队列：只处理无直接下载地址的 mod（下标来自 m_serialModIndices）
  while (m_serialPos < m_serialModIndices.size())
  {
    const int idx = m_serialModIndices.at(m_serialPos);
    const ModInfo& mod = m_info.mods.at(idx);

    QString url;
    if (!mod.projectId.isEmpty() && !mod.fileId.isEmpty())
      url = getCurseForgeDownloadUrl(mod.projectId, mod.fileId);

    if (url.isEmpty())
    {
      // 既无下载地址也无 CurseForge ID：内容应在 overrides 里，计失败继续
      ++m_modsFailed;
      ++m_serialPos;
      reportModProgress();
      continue;
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
          const int modIdx = m_serialPos < m_serialModIndices.size()
                                 ? m_serialModIndices.at(m_serialPos)
                                 : -1;
          const ModInfo& mod = modIdx >= 0 ? m_info.mods.at(modIdx) : ModInfo{};
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

        // 文件名来自服务器响应/整合包清单，先压成安全的单层文件名
        fileName = Safety::safeFileName(fileName, QStringLiteral("mod.jar"));
        if (!fileName.endsWith(".jar", Qt::CaseInsensitive))
          fileName += ".jar";

        QDir().mkpath(modsDir);
        QString filePath = QDir::cleanPath(modsDir + "/" + fileName);
        QFile file(filePath);
        if (Safety::isPathInside(modsDir, filePath) && file.open(QIODevice::WriteOnly))
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

      ++m_serialPos;
      reportModProgress();
      downloadNextMod();
    });

    return; // 异步下载已发起，等 finished 回调再推进
  }

  // 串行队列耗收尾
  emitProgress(75,
    tr("模组下载完成 (%1 成功, %2 失败)").arg(m_modsDownloaded).arg(m_modsFailed));
  emitStage(tr("正在解压覆盖文件..."));
  extractOverrides();
}

QStringList ModpackImporter::buildModCandidates(const ModInfo& mod) const
{
  QStringList candidates;
  for (const QString& u : mod.downloadUrls)
  {
    // 每个地址展开为「镜像在前、官方兜底」，整体仍保持清单里的先后顺序
    const QStringList expanded = DownloadMirrors::expandFileCandidates(u);
    for (const QString& c : expanded)
      if (!candidates.contains(c))
        candidates.append(c);
  }
  if (candidates.isEmpty() && !mod.downloadUrl.isEmpty())
    candidates = DownloadMirrors::expandFileCandidates(mod.downloadUrl);
  return candidates;
}

QString ModpackImporter::modFileName(const ModInfo& mod, const QStringList& candidates) const
{
  QString fileName;

  // mrpack 的 path 形如 "mods/xxx.jar"，取末段作为文件名
  if (!mod.name.isEmpty())
  {
    const QString trimmed = mod.name.trimmed();
    const int lastSlash = qMax(trimmed.lastIndexOf('/'), trimmed.lastIndexOf('\\'));
    fileName = (lastSlash >= 0) ? trimmed.mid(lastSlash + 1) : trimmed;
  }

  if (fileName.isEmpty() && !candidates.isEmpty())
  {
    const QString path = QUrl(candidates.first()).path();
    const int lastSlash = path.lastIndexOf('/');
    fileName = (lastSlash >= 0) ? path.mid(lastSlash + 1) : QString();
  }

  fileName = Safety::safeFileName(fileName, QStringLiteral("mod.jar"));
  if (!fileName.endsWith(".jar", Qt::CaseInsensitive))
    fileName += ".jar";
  return fileName;
}

// 模组下载进度：并发（NetJob）与串行（CurseForge ID）两条路径共用，
// 只按"已完成 + 已失败"计数推进，避免并发完成把进度烧到 100%
void ModpackImporter::reportModProgress()
{
  const int total = qMax(1, m_totalMods);
  const int done = m_modsDownloaded + m_modsFailed;
  const int percent = 62 + qMin(13, done * 13 / total);
  emitProgress(percent, tr("正在下载模组 (%1/%2)...").arg(done).arg(m_totalMods));
}

void ModpackImporter::extractOverrides()
{
  if (m_zipPath.isEmpty() || !QFile::exists(m_zipPath))
  {
    emitProgress(90, tr("无覆盖文件需要解压"));
    createInstance();
    return;
  }

  ZipReader zip;
  if (!zip.open(m_zipPath))
  {
    emitProgress(90, tr("无法打开整合包文件，跳过覆盖文件解压"));
    createInstance();
    return;
  }

  QString prefixToExtract;
  QString prefixToStrip;
  // Modrinth：覆盖文件可能在 overrides/ 或 client-overrides/ 下，统一剥前缀
  bool modrinthStripOverrides = false;
  // MultiMC：本家导出用 .minecraft/，外部包常见 minecraft/，两种都认
  bool multimcStripMinecraft = false;

  switch (m_info.type)
  {
  case ModpackType::CurseForge:
    prefixToExtract = "overrides/";
    prefixToStrip = "overrides/";
    break;

  case ModpackType::MultiMC:
    multimcStripMinecraft = true;
    break;

  case ModpackType::Modrinth:
    modrinthStripOverrides = true;
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

  // BlockBox / Generic：文件按原始相对路径打在 zip 根，整包即覆盖内容
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

    // Skip metadata / index files
    if (entry.fileName == "modrinth.index.json" || entry.fileName == "pack.mrpack"
        || entry.fileName == "blockbox.pack.json" || entry.fileName == "mcbbs.packmeta"
        || entry.fileName == "instance.json")
      continue;

    QString relativePath = entry.fileName;

    if (multimcStripMinecraft)
    {
      if (relativePath.startsWith(".minecraft/"))
        relativePath = relativePath.mid(11);
      else if (relativePath.startsWith("minecraft/"))
        relativePath = relativePath.mid(10);
      else
        continue; // mmc-pack.json / instance.cfg 等根文件不进实例目录
    }
    else if (modrinthStripOverrides)
    {
      if (relativePath.startsWith("client-overrides/"))
        relativePath = relativePath.mid(17);
      else if (relativePath.startsWith("overrides/"))
        relativePath = relativePath.mid(10);
      else
        continue; // 只解压覆盖目录内容
    }
    else
    {
      // Filter by prefix if specified
      if (!prefixToExtract.isEmpty() && !entry.fileName.startsWith(prefixToExtract))
        continue;
      if (!prefixToStrip.isEmpty() && relativePath.startsWith(prefixToStrip))
        relativePath = relativePath.mid(prefixToStrip.length());
    }

    if (relativePath.isEmpty())
      continue;

    // 条目名可能带 "../" 想写到实例目录之外，一律拒绝
    if (!Safety::isSafeEntryName(relativePath))
      continue;

    QString destPath = QDir::cleanPath(m_instancePath + "/" + relativePath);
    if (!Safety::isPathInside(m_instancePath, destPath))
      continue;

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

  emitProgress(92,
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

  emitProgress(100, tr("安装完成"));
  emit installFinished(true, m_instancePath);
  emitSucceeded();
}

void ModpackImporter::failInstall(const QString& error)
{
  emit installFinished(false, error);
  if (m_abortRequested)
    emitAborted();
  else
    emitFailed(error);
}

bool ModpackImporter::canAbort() const
{
  return true;
}

bool ModpackImporter::abort()
{
  m_abortRequested = true;

  // 模组下载阶段：取消在途 NetJob / 请求
  if (m_modJob)
    m_modJob->abort();
  if (m_currentModReply)
    m_currentModReply->abort();

  // 版本下载阶段：该下载由本次导入发起，随之取消
  VersionDownloader* vd = VersionDownloader::instance();
  if (vd->isDownloading())
    vd->cancelDownload();

  // 加载器安装阶段：取消当前活跃的安装器
  if (m_activeLoaderType == QLatin1String("forge"))
    ForgeInstaller::instance()->cancelInstall();
  else if (m_activeLoaderType == QLatin1String("fabric"))
    FabricInstaller::instance()->cancelInstall();
  else if (m_activeLoaderType == QLatin1String("neoforge"))
    NeoForgeInstaller::instance()->cancelInstall();

  return true;
}

void ModpackImporter::emitStage(const QString& stage)
{
  emit stageChanged(stage);
  setStatus(stage);
}

void ModpackImporter::emitProgress(int percent, const QString& message)
{
  emit installProgressChanged(percent, message);
  setProgress(percent, 100);
}

QString ModpackImporter::getCurseForgeDownloadUrl(const QString& projectId,
                                                   const QString& fileId) const
{
  // CurseForge API download endpoint
  return QString("https://www.curseforge.com/api/v1/mods/%1/files/%2/download")
    .arg(projectId, fileId);
}

} // namespace modpack