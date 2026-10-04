/**
 * @file   JavaDownloader.cpp
 * @brief  Java 下载器实现 — 多版本、多镜像源、多发行版
 * @author BlockBox Team
 * @date   2026-07-01
 */

#include "JavaDownloader.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

JavaDownloader* JavaDownloader::instance()
{
  static JavaDownloader inst;
  return &inst;
}

JavaDownloader::JavaDownloader(QObject *parent)
  : QObject(parent)
  , m_networkManager(new QNetworkAccessManager(this))
  , m_activeReply(nullptr)
  , m_status(JavaDownloadStatus::Idle)
  , m_currentMajorVersion(0)
{
}

QVector<JavaDistribution> JavaDownloader::availableDistributions() const
{
  return {
    {"Eclipse Adoptium (Temurin)", "adoptium",
     "Eclipse 基金会维护的 OpenJDK 发行版，最广泛使用"},
    {"Amazon Corretto", "corretto",
     "Amazon 维护的免费 OpenJDK 发行版，针对云环境优化"},
    {"Microsoft OpenJDK", "microsoft",
     "Microsoft 维护的 OpenJDK 发行版"},
    {"Azul Zulu", "zulu",
     "Azul 维护的 OpenJDK 发行版，支持广泛平台"},
  };
}

QVector<JavaMirror> JavaDownloader::availableMirrors() const
{
  return {
    {"官方源", "official", ""},
    {"清华大学 TUNA", "tuna", "https://mirrors.tuna.tsinghua.edu.cn/Adoptium/"},
    {"阿里云", "aliyun", "https://mirrors.aliyun.com/adoptium/"},
    {"中科大 USTC", "ustc", "https://mirrors.ustc.edu.cn/adoptium/"},
  };
}

QVector<JavaVersionInfo> JavaDownloader::supportedVersions() const
{
  return {
    {8,  "Java 8",  "Java 8 (LTS)",      true},
    {11, "Java 11", "Java 11 (LTS)",     true},
    {17, "Java 17", "Java 17 (LTS)",     true},
    {21, "Java 21", "Java 21 (LTS)",     true},
    {22, "Java 22", "Java 22",           false},
    {23, "Java 23", "Java 23",           false},
    {24, "Java 24", "Java 24",           false},
  };
}

QString JavaDownloader::currentOS() const
{
#ifdef Q_OS_WIN
  return "windows";
#elif defined(Q_OS_MACOS)
  return "mac";
#elif defined(Q_OS_ANDROID)
  return "android";
#else
  return "linux";
#endif
}

QString JavaDownloader::currentArchitecture() const
{
#ifdef Q_OS_WIN
  // On Windows, check if running under WOW64 (32-bit on 64-bit)
  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
      si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
    return "x64";
  return "x86";
#elif defined(Q_OS_ANDROID)
  // Android: detect architecture from Qt macros or system properties
#if defined(Q_PROCESSOR_ARM)
  return "arm";
#elif defined(Q_PROCESSOR_ARM_64)
  return "aarch64";
#elif defined(Q_PROCESSOR_X86)
  return "x86";
#elif defined(Q_PROCESSOR_X86_64)
  return "x64";
#else
  // Fallback: try to detect from QSysInfo
  QString arch = QSysInfo::currentCpuArchitecture();
  if (arch.contains("arm", Qt::CaseInsensitive))
    return arch.contains("64") ? "aarch64" : "arm";
  if (arch.contains("x86", Qt::CaseInsensitive))
    return arch.contains("64") ? "x64" : "x86";
  return "aarch64"; // Default to ARM64 for Android
#endif
#elif defined(Q_PROCESSOR_X86_64)
  return "x64";
#elif defined(Q_PROCESSOR_ARM64)
  return "aarch64";
#else
  return "x64";
#endif
}

QString JavaDownloader::platformExtension() const
{
#ifdef Q_OS_WIN
  return ".zip";
#elif defined(Q_OS_ANDROID)
  // Android uses tar.gz for Java archives
  return ".tar.gz";
#else
  return ".tar.gz";
#endif
}

// --- 构建各发行版下载 URL ---

QString JavaDownloader::buildAdoptiumUrl(const QString &mirrorId, int majorVersion)
{
  // Adoptium API v3: 获取指定版本的最新二进制包列表
  // 官方 API: https://api.adoptium.net/v3/assets/latest/{version}/hotspot
  if (mirrorId == "official")
  {
    return QString("https://api.adoptium.net/v3/assets/latest/%1/hotspot")
        .arg(majorVersion);
  }
  // 镜像源：直接使用镜像站的预构建二进制
  // 注意：镜像源 URL 格式不同，需要先查 API 获取版本号，再拼接镜像下载地址
  // 这里先获取 API 数据，再在 parse 阶段替换为镜像 URL
  return QString("https://api.adoptium.net/v3/assets/latest/%1/hotspot")
      .arg(majorVersion);
}

QString JavaDownloader::buildCorrettoUrl(const QString &mirrorId, int majorVersion)
{
  Q_UNUSED(mirrorId);
  // Amazon Corretto: 使用 API 获取最新版本信息
  // https://corretto.github.io/corretto-downloads/latest_links/indexmap_with_checksum.html
  // 该页面返回所有平台的最新版本链接
  return "https://corretto.github.io/corretto-downloads/latest_links/indexmap_with_checksum.html";
}

QString JavaDownloader::buildMicrosoftUrl(const QString &mirrorId, int majorVersion)
{
  Q_UNUSED(mirrorId);
  // Microsoft OpenJDK: 固定格式的下载链接
  // https://aka.ms/download-jdk/microsoft-jdk-{version}-{platform}-{arch}.{ext}
  QString os = currentOS();
  QString arch = currentArchitecture();
  QString ext = (os == "windows") ? "zip" : "tar.gz";
  QString platform;
  if (os == "windows") platform = "windows";
  else if (os == "mac") platform = "macOS";
  else platform = "linux";

  return QString("https://aka.ms/download-jdk/microsoft-jdk-%1-%2-%3.%4")
      .arg(majorVersion)
      .arg(platform)
      .arg(arch)
      .arg(ext);
}

QString JavaDownloader::buildZuluUrl(int majorVersion)
{
  // Azul Zulu API: 获取最新版本信息
  // https://api.azul.com/metadata/v1/zulu/packages/?java_version={version}&os={os}&arch={arch}&archive_type={ext}&java_package_type=jdk&latest=true
  QString os = currentOS();
  QString arch = currentArchitecture();
  QString ext = (os == "windows") ? "zip" : "tar.gz";

  return QString("https://api.azul.com/metadata/v1/zulu/packages/"
                 "?java_version=%1&os=%2&arch=%3&archive_type=%4"
                 "&java_package_type=jdk&latest=true&page_size=1")
      .arg(majorVersion)
      .arg(os)
      .arg(arch)
      .arg(ext);
}

// --- 获取可用二进制包 ---

void JavaDownloader::fetchAvailableBinaries(const QString &distributionId,
                                             const QString &mirrorId,
                                             int majorVersion)
{
  m_currentDistributionId = distributionId;
  m_currentMirrorId = mirrorId;
  m_currentMajorVersion = majorVersion;

  setStatus(JavaDownloadStatus::FetchingBinaries);

  QString url;
  if (distributionId == "adoptium")
    url = buildAdoptiumUrl(mirrorId, majorVersion);
  else if (distributionId == "corretto")
    url = buildCorrettoUrl(mirrorId, majorVersion);
  else if (distributionId == "microsoft")
  {
    // Microsoft 直接构建下载 URL，无需 API 查询
    QVector<JavaBinaryInfo> binaries;
    JavaBinaryInfo info;
    info.majorVersion = majorVersion;
    info.versionName = QString("Java %1").arg(majorVersion);
    info.distributionId = "microsoft";
    info.distributionName = "Microsoft OpenJDK";
    info.downloadUrl = url;
    info.architecture = currentArchitecture();
    info.osName = currentOS();
    info.packageType = "jdk";
    info.fileSize = 0;
    binaries.append(info);
    setStatus(JavaDownloadStatus::Idle);
    emit versionListFetched(binaries);
    return;
  }
  else if (distributionId == "zulu")
    url = buildZuluUrl(majorVersion);
  else
  {
    emit fetchError(tr("未知的 Java 发行版: %1").arg(distributionId));
    setStatus(JavaDownloadStatus::Failed);
    return;
  }

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
  request.setRawHeader("Accept", "application/json");
  request.setTransferTimeout(15000);

  if (m_activeReply)
  {
    // abort() 会同步触发 finished 信号进入下方 lambda；必须先置空成员，
    // 否则 abort() 返回后继续 deleteLater 会空指针解引用闪退。
    QNetworkReply *old = m_activeReply;
    m_activeReply = nullptr;
    old->abort();
    old->deleteLater();
  }

  QNetworkReply *reply = m_networkManager->get(request);
  m_activeReply = reply;

  connect(reply, &QNetworkReply::finished, this, [this, reply, distributionId, majorVersion]()
  {
    if (!m_activeReply || m_activeReply != reply) return;

    QByteArray data = reply->readAll();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_activeReply = nullptr;
    reply->deleteLater();

    if (statusCode != 200)
    {
      emit fetchError(tr("API 请求失败 (HTTP %1)").arg(statusCode));
      setStatus(JavaDownloadStatus::Failed);
      return;
    }

    if (distributionId == "adoptium")
      parseAdoptiumResponse(data);
    else if (distributionId == "corretto")
      parseCorrettoResponse(data, majorVersion);
    else if (distributionId == "zulu")
      parseZuluResponse(data, majorVersion);
  });
}

// --- 解析各 API 响应 ---

void JavaDownloader::parseAdoptiumResponse(const QByteArray &data)
{
  QJsonDocument doc = QJsonDocument::fromJson(data);
  QJsonArray assets = doc.array();

  QVector<JavaBinaryInfo> binaries;
  QString os = currentOS();
  QString arch = currentArchitecture();

  for (const QJsonValue &val : assets)
  {
    QJsonObject asset = val.toObject();
    QJsonObject binary = asset["binary"].toObject();
    QJsonObject version = asset["version"].toObject();
    QJsonObject package = binary["package"].toObject();

    QString binaryOs = binary["os"].toString();
    QString binaryArch = binary["architecture"].toString();
    QString binaryType = binary["image_type"].toString();

    // 过滤：只匹配当前平台和架构
    if (binaryOs != os) continue;
    if (binaryArch != arch) continue;
    if (binaryType != "jdk" && binaryType != "jre") continue;

    JavaBinaryInfo info;
    info.versionName = QString("Java %1").arg(version["major"].toInt());
    info.majorVersion = version["major"].toInt();
    info.distributionId = "adoptium";
    info.distributionName = "Eclipse Adoptium (Temurin)";
    info.architecture = binaryArch;
    info.osName = binaryOs;
    info.packageType = binaryType;

    // 根据镜像源替换下载 URL
    QString originalUrl = package["link"].toString();
    QString checksum = package["checksum"].toString();

    if (m_currentMirrorId == "official")
    {
      info.downloadUrl = originalUrl;
    }
    else if (m_currentMirrorId == "tuna")
    {
      // 清华大学 TUNA: https://mirrors.tuna.tsinghua.edu.cn/Adoptium/{version}/ga/{os}/{arch}/jdk/hotspot/normal/eclipse
      // 从原始 URL 中提取路径部分
      // 原始格式: https://github.com/adoptium/temurin{version}-binaries/releases/download/...
      // TUNA 格式: https://mirrors.tuna.tsinghua.edu.cn/Adoptium/{major}/ga/{os}/{arch}/jdk/hotspot/normal/eclipse
      QString tunaUrl = QString("https://mirrors.tuna.tsinghua.edu.cn/Adoptium/%1/ga/%2/%3/jdk/hotspot/normal/eclipse")
                            .arg(m_currentMajorVersion)
                            .arg(os)
                            .arg(arch);
      info.downloadUrl = tunaUrl;
    }
    else if (m_currentMirrorId == "aliyun")
    {
      QString aliUrl = QString("https://mirrors.aliyun.com/adoptium/%1/ga/%2/%3/jdk/hotspot/normal/eclipse")
                           .arg(m_currentMajorVersion)
                           .arg(os)
                           .arg(arch);
      info.downloadUrl = aliUrl;
    }
    else if (m_currentMirrorId == "ustc")
    {
      QString ustcUrl = QString("https://mirrors.ustc.edu.cn/adoptium/%1/ga/%2/%3/jdk/hotspot/normal/eclipse")
                            .arg(m_currentMajorVersion)
                            .arg(os)
                            .arg(arch);
      info.downloadUrl = ustcUrl;
    }

    info.checksum = checksum;
    info.checksumType = "sha256";
    info.fileSize = package["size"].toInt();

    binaries.append(info);
  }

  setStatus(JavaDownloadStatus::Idle);

  if (binaries.isEmpty())
  {
    emit fetchError(tr("未找到适用于当前平台的 Java %1 二进制包").arg(m_currentMajorVersion));
  }
  else
  {
    emit versionListFetched(binaries);
  }
}

void JavaDownloader::parseCorrettoResponse(const QByteArray &data, int majorVersion)
{
  Q_UNUSED(majorVersion);
  // Amazon Corretto 的 latest_links 是一个 HTML 页面，包含 JSON 数据
  // 实际上返回的是纯文本链接列表，按平台分类
  QString content = QString::fromUtf8(data);
  QVector<JavaBinaryInfo> binaries;
  QString os = currentOS();
  QString arch = currentArchitecture();

  // 构建当前平台的匹配模式
  QString platformPrefix;
  if (os == "windows") platformPrefix = "windows-";
  else if (os == "mac") platformPrefix = "macos-";
  else platformPrefix = "linux-";

  QString archSuffix = (arch == "x64") ? "x64" : "aarch64";
  QString majorStr = QString::number(majorVersion);

  // 解析每行链接
  QStringList lines = content.split('\n', Qt::SkipEmptyParts);
  for (const QString &line : lines)
  {
    QString trimmed = line.trimmed();
    // 跳过 HTML 标签
    if (trimmed.startsWith("<") || trimmed.startsWith("<!DOCTYPE")) continue;

    // 查找匹配的 JDK 链接
    // 格式: https://corretto.aws/downloads/resources/.../amazon-corretto-{version}-{platform}-{arch}-jdk.{ext}
    if (trimmed.contains("amazon-corretto-" + majorStr) &&
        trimmed.contains(platformPrefix) &&
        trimmed.contains(archSuffix) &&
        trimmed.contains("jdk") &&
        !trimmed.contains("jre"))
    {
      JavaBinaryInfo info;
      info.versionName = QString("Java %1").arg(majorVersion);
      info.majorVersion = majorVersion;
      info.distributionId = "corretto";
      info.distributionName = "Amazon Corretto";
      info.downloadUrl = trimmed;
      info.architecture = arch;
      info.osName = os;
      info.packageType = "jdk";
      info.fileSize = 0;
      binaries.append(info);
      break;
    }
  }

  setStatus(JavaDownloadStatus::Idle);

  if (binaries.isEmpty())
  {
    emit fetchError(tr("未找到适用于当前平台的 Amazon Corretto Java %1").arg(majorVersion));
  }
  else
  {
    emit versionListFetched(binaries);
  }
}

void JavaDownloader::parseZuluResponse(const QByteArray &data, int majorVersion)
{
  QJsonDocument doc = QJsonDocument::fromJson(data);
  QJsonArray items = doc.array();

  QVector<JavaBinaryInfo> binaries;

  for (const QJsonValue &val : items)
  {
    QJsonObject item = val.toObject();

    JavaBinaryInfo info;
    info.versionName = QString("Java %1").arg(majorVersion);
    info.majorVersion = majorVersion;
    info.distributionId = "zulu";
    info.distributionName = "Azul Zulu";
    info.downloadUrl = item["download_url"].toString();
    info.checksum = item["sha256_hash"].toString();
    info.checksumType = "sha256";
    info.architecture = "x64";
    info.osName = currentOS();
    info.packageType = "jdk";
    info.fileSize = item["size"].toVariant().toLongLong();

    binaries.append(info);
    break; // 只需要最新版本
  }

  setStatus(JavaDownloadStatus::Idle);

  if (binaries.isEmpty())
  {
    emit fetchError(tr("未找到 Azul Zulu Java %1").arg(majorVersion));
  }
  else
  {
    emit versionListFetched(binaries);
  }
}

// --- 下载操作 ---

void JavaDownloader::downloadJava(const JavaBinaryInfo &binaryInfo,
                                   const QString &savePath)
{
  m_currentSavePath = savePath;
  setStatus(JavaDownloadStatus::Downloading);
  doDownload(binaryInfo.downloadUrl, savePath);
}

void JavaDownloader::downloadAndExtract(const JavaBinaryInfo &binaryInfo,
                                         const QString &extractDir)
{
  m_currentExtractDir = extractDir;

  // 生成临时下载路径
  QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QString fileName = QString("java_%1_%2_%3%4")
                         .arg(binaryInfo.distributionId)
                         .arg(binaryInfo.majorVersion)
                         .arg(QUuid::createUuid().toString(QUuid::Id128).left(8))
                         .arg(platformExtension());
  m_currentArchivePath = QDir(tempDir).absoluteFilePath(fileName);
  m_currentSavePath = m_currentArchivePath;

  downloadJava(binaryInfo, m_currentArchivePath);
}

void JavaDownloader::doDownload(const QString &url, const QString &savePath)
{
  if (m_activeReply)
  {
    // abort() 会同步触发 finished 信号进入下方 lambda；必须先置空成员，
    // 否则 abort() 返回后继续 deleteLater 会空指针解引用闪退。
    QNetworkReply *old = m_activeReply;
    m_activeReply = nullptr;
    old->abort();
    old->deleteLater();
  }

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
  request.setTransferTimeout(600000); // 10 分钟超时

  QNetworkReply *reply = m_networkManager->get(request);
  m_activeReply = reply;

  // 进度信号
  connect(reply, &QNetworkReply::downloadProgress, this,
    [this, reply](qint64 bytesReceived, qint64 bytesTotal)
    {
      if (!m_activeReply || m_activeReply != reply) return;

      // 限制进度更新频率至 200ms
      qint64 now = QDateTime::currentMSecsSinceEpoch();
      QVariant lastEmitVar = reply->property("lastProgressEmit");
      if (lastEmitVar.isValid())
      {
        qint64 lastEmit = lastEmitVar.toLongLong();
        if (now - lastEmit < 200) return;
      }
      reply->setProperty("lastProgressEmit", now);

      int percent = 0;
      if (bytesTotal > 0)
        percent = static_cast<int>((bytesReceived * 100) / bytesTotal);

      emit downloadProgress(QFileInfo(m_currentSavePath).fileName(), percent);
      emit downloadBytesProgress(bytesReceived, bytesTotal);
    });

  // 完成处理
  connect(reply, &QNetworkReply::finished, this, [this, reply, savePath]()
  {
    if (!m_activeReply || m_activeReply != reply) return;

    QByteArray data = reply->readAll();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError error = reply->error();
    m_activeReply = nullptr;
    reply->deleteLater();

    if (error != QNetworkReply::NoError && statusCode != 200)
    {
      emit downloadFailed(tr("下载失败 (HTTP %1): %2").arg(statusCode).arg(error));
      setStatus(JavaDownloadStatus::Failed);
      return;
    }

    // 写入文件
    QFile file(savePath);
    QFileInfo fileInfo(savePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists())
      dir.mkpath(".");

    if (file.open(QIODevice::WriteOnly))
    {
      file.write(data);
      file.close();
      qDebug() << "[JavaDownloader] Downloaded:" << savePath;

      emit downloadCompleted(savePath);

      // 如果需要解压
      if (!m_currentExtractDir.isEmpty())
      {
        setStatus(JavaDownloadStatus::Extracting);
        doExtract(savePath, m_currentExtractDir);
      }
      else
      {
        setStatus(JavaDownloadStatus::Completed);
      }
    }
    else
    {
      emit downloadFailed(tr("写入文件失败: %1").arg(savePath));
      setStatus(JavaDownloadStatus::Failed);
    }
  });
}

// --- 解压操作 ---

void JavaDownloader::doExtract(const QString &archivePath, const QString &extractDir)
{
  QDir dir(extractDir);
  if (!dir.exists())
    dir.mkpath(".");

  emit extractProgress(0);

  // 使用 7z 或系统命令解压
  // 在 Windows 上，使用 PowerShell 解压 zip；在 Android/Linux/macOS 上使用 tar
#ifdef Q_OS_WIN
  // Windows: 使用 PowerShell Expand-Archive
  QProcess *process = new QProcess(this);
  QStringList args;
  args << "-NoProfile" << "-Command"
       << QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                .arg(QString(archivePath).replace("/", "\\"))
                .arg(QString(extractDir).replace("/", "\\"));

  connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, [this, process, archivePath, extractDir](int exitCode, QProcess::ExitStatus)
  {
    process->deleteLater();
    if (exitCode == 0)
    {
      emit extractProgress(100);
      // 清理临时文件
      QFile::remove(archivePath);
      // 查找解压后的 Java 路径
      QString javaHome = findJavaHome(extractDir);
      emit extractCompleted(javaHome);
      setStatus(JavaDownloadStatus::Completed);
    }
    else
    {
      QString error = process->readAllStandardError();
      emit extractFailed(tr("解压失败: %1").arg(error));
      setStatus(JavaDownloadStatus::Failed);
    }
  });

  connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error)
  {
    Q_UNUSED(error);
    process->deleteLater();
    emit extractFailed(tr("无法启动解压进程"));
    setStatus(JavaDownloadStatus::Failed);
  });

  process->start("powershell", args);
#else
  // Android/Linux/macOS: 使用 tar
  QProcess *process = new QProcess(this);
  QStringList args;
  args << "-xzf" << archivePath << "-C" << extractDir;

  connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, [this, process, archivePath, extractDir](int exitCode, QProcess::ExitStatus)
  {
    process->deleteLater();
    if (exitCode == 0)
    {
      emit extractProgress(100);
      QFile::remove(archivePath);
      QString javaHome = findJavaHome(extractDir);
      emit extractCompleted(javaHome);
      setStatus(JavaDownloadStatus::Completed);
    }
    else
    {
      QString error = process->readAllStandardError();
      emit extractFailed(tr("解压失败: %1").arg(error));
      setStatus(JavaDownloadStatus::Failed);
    }
  });

  process->start("tar", args);
#endif
}

QString JavaDownloader::findJavaHome(const QString &extractDir) const
{
  // 递归查找包含 bin/java 或 bin/java.exe 的目录
  QDir dir(extractDir);
  QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

  for (const QFileInfo &entry : entries)
  {
    QString javaExe = entry.absoluteFilePath() +
#ifdef Q_OS_WIN
                      "/bin/java.exe";
#else
                      "/bin/java";
#endif
    if (QFile::exists(javaExe))
      return entry.absoluteFilePath();

    // 递归查找（最多两层）
    QString subResult = findJavaHome(entry.absoluteFilePath());
    if (!subResult.isEmpty())
      return subResult;
  }

  return extractDir;
}

void JavaDownloader::cancel()
{
  if (m_activeReply)
  {
    // abort() 会同步触发 finished 信号进入上方 lambda；必须先置空成员，
    // 否则 abort() 返回后继续 deleteLater 会空指针解引用闪退。
    QNetworkReply *old = m_activeReply;
    m_activeReply = nullptr;
    old->abort();
    old->deleteLater();
  }
  m_currentExtractDir.clear();
  setStatus(JavaDownloadStatus::Idle);
}

void JavaDownloader::setStatus(JavaDownloadStatus status)
{
  if (m_status != status)
  {
    m_status = status;
    emit statusChanged(status);
  }
}