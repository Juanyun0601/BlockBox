/**
 * @file   JavaDownloader.cpp
 * @brief  Java 下载器实现 — 多版本、多镜像源、多发行版
 * @author BlockBox Team
 * @date   2026-07-01
 */

#include "JavaDownloader.h"

#include "Safety.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

// 目录列表 HTML 里挑选安装包：OpenJDK{主版本}U-{jdk|jre}_{架构}_{系统}_hotspot_版本号.后缀
// Java 8 的版本号段是字母数字混合（如 8u504b01），不能只匹配数字
QString mirrorFilePattern(int majorVersion, const QString &packageType,
                          const QString &arch, const QString &os,
                          const QString &ext)
{
  QString prefix = QString("OpenJDK%1U-%2_%3_%4_hotspot_")
                       .arg(majorVersion)
                       .arg(packageType)
                       .arg(arch)
                       .arg(os);
  QString extPattern = QRegularExpression::escape(ext);
  return QRegularExpression::escape(prefix) + QStringLiteral("[A-Za-z0-9._]+\\.") + extPattern;
}

// 把版本段拆成数字/字母串，逐段比较（数字按数值比），用于在多个候选文件中取最新
QStringList versionRuns(const QString &s)
{
  QStringList runs;
  QString current;
  bool currentIsDigit = false;
  for (const QChar &c : s)
  {
    bool isDigit = c.isDigit();
    if (!current.isEmpty() && isDigit != currentIsDigit)
    {
      runs.append(current);
      current.clear();
    }
    currentIsDigit = isDigit;
    current.append(c);
  }
  if (!current.isEmpty())
    runs.append(current);
  return runs;
}

int compareVersionRuns(const QStringList &a, const QStringList &b)
{
  int n = qMax(a.size(), b.size());
  for (int i = 0; i < n; ++i)
  {
    if (i >= a.size()) return -1;
    if (i >= b.size()) return 1;
    const QString &ra = a[i];
    const QString &rb = b[i];
    bool aNum = false, bNum = false;
    qlonglong av = ra.toLongLong(&aNum);
    qlonglong bv = rb.toLongLong(&bNum);
    if (aNum && bNum)
    {
      if (av != bv) return av < bv ? -1 : 1;
    }
    else if (ra != rb)
    {
      return ra < rb ? -1 : 1;
    }
  }
  return 0;
}

} // namespace

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
     "Microsoft 维护的 OpenJDK 发行版（仅提供 Java 11/17/21/25）"},
    {"Azul Zulu", "zulu",
     "Azul 维护的 OpenJDK 发行版，支持广泛平台"},
  };
}

QVector<JavaMirror> JavaDownloader::availableMirrors() const
{
  return {
    {"官方源", "official", ""},
    {"清华大学 TUNA", "tuna", "https://mirrors.tuna.tsinghua.edu.cn/Adoptium/"},
    {"中科大 USTC", "ustc", "https://mirrors.ustc.edu.cn/adoptium/releases/"},
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
    {25, "Java 25", "Java 25 (LTS)",     true},
  };
}

QVector<int> JavaDownloader::supportedMajorVersions(const QString &distributionId) const
{
  // Microsoft 只发布 11/17/21/25，其余版本直链是死链
  if (distributionId == "microsoft")
    return {11, 17, 21, 25};
  return {8, 11, 17, 21, 22, 23, 24, 25};
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
  return "zip";
#elif defined(Q_OS_ANDROID)
  // Android uses tar.gz for Java archives
  return "tar.gz";
#else
  return "tar.gz";
#endif
}

// --- 构建各发行版下载 URL ---

QString JavaDownloader::buildAdoptiumUrl(const QString &mirrorId, int majorVersion)
{
  Q_UNUSED(mirrorId);
  // Adoptium API v3: 获取指定版本的最新二进制包列表
  // 官方 API: https://api.adoptium.net/v3/assets/latest/{version}/hotspot
  return QString("https://api.adoptium.net/v3/assets/latest/%1/hotspot")
      .arg(majorVersion);
}

QString JavaDownloader::buildCorrettoUrl(int majorVersion)
{
  // Corretto 提供固定格式的“最新版”直链，302 到当前最新构建，全版本可用
  // https://corretto.aws/downloads/latest/amazon-corretto-{ver}-{arch}-{os}-jdk.{ext}
  QString os = currentOS();
  if (os == "mac")
    os = "macos";
  return QString("https://corretto.aws/downloads/latest/amazon-corretto-%1-%2-%3-jdk.%4")
      .arg(majorVersion)
      .arg(currentArchitecture())
      .arg(os)
      .arg(platformExtension());
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
  QString os = currentOS();
  QString arch = currentArchitecture();
  QString ext = platformExtension();

  return QString("https://api.azul.com/metadata/v1/zulu/packages/"
                 "?java_version=%1&os=%2&arch=%3&archive_type=%4"
                 "&java_package_type=jdk&latest=true&page_size=1")
      .arg(majorVersion)
      .arg(os)
      .arg(arch)
      .arg(ext);
}

QString JavaDownloader::mirrorDirectoryUrl(const QString &mirrorId, int majorVersion,
                                           const QString &os, const QString &arch,
                                           const QString &packageType) const
{
  if (mirrorId == "tuna")
  {
    // 清华 TUNA: /Adoptium/{主版本}/{jdk|jre}/{架构}/{系统}/
    return QString("https://mirrors.tuna.tsinghua.edu.cn/Adoptium/%1/%2/%3/%4/")
        .arg(majorVersion)
        .arg(packageType)
        .arg(arch)
        .arg(os);
  }
  if (mirrorId == "ustc")
  {
    // 中科大 USTC: /adoptium/releases/temurin{主版本}-binaries/LatestRelease/
    return QString("https://mirrors.ustc.edu.cn/adoptium/releases/temurin%1-binaries/LatestRelease/")
        .arg(majorVersion);
  }
  return QString();
}

// --- 获取可用二进制包 ---

void JavaDownloader::fetchAvailableBinaries(const QString &distributionId,
                                             const QString &mirrorId,
                                             int majorVersion)
{
  m_currentDistributionId = distributionId;
  m_currentMirrorId = mirrorId;
  m_currentMajorVersion = majorVersion;
  m_pendingMirrorBinaries.clear();
  m_resolvedMirrorBinaries.clear();

  setStatus(JavaDownloadStatus::FetchingBinaries);

  if (distributionId == "microsoft" || distributionId == "corretto")
  {
    // 这两家提供固定格式的直链，无需查询 API
    QVector<JavaBinaryInfo> binaries;
    JavaBinaryInfo info;
    info.majorVersion = majorVersion;
    info.versionName = QString("Java %1").arg(majorVersion);
    info.distributionId = distributionId;
    info.distributionName = (distributionId == "microsoft") ? "Microsoft OpenJDK"
                                                            : "Amazon Corretto";
    info.downloadUrl = (distributionId == "microsoft")
                           ? buildMicrosoftUrl(mirrorId, majorVersion)
                           : buildCorrettoUrl(majorVersion);
    info.architecture = currentArchitecture();
    info.osName = currentOS();
    info.packageType = "jdk";
    info.fileSize = 0;
    binaries.append(info);
    setStatus(JavaDownloadStatus::Idle);
    emit versionListFetched(binaries);
    return;
  }

  QString url;
  if (distributionId == "adoptium")
    url = buildAdoptiumUrl(mirrorId, majorVersion);
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
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
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

    if (m_currentMirrorId == "official")
    {
      info.downloadUrl = package["link"].toString();
      info.checksum = package["checksum"].toString();
      info.fileSize = package["size"].toVariant().toLongLong();
    }
    else
    {
      // 镜像站需要先从目录页解析出真实文件名（镜像同步的构建版本
      // 可能落后于 API 最新版，官方 API 的校验和对不上），
      // 这里先放目录页 URL，随后逐个解析
      info.downloadUrl = mirrorDirectoryUrl(m_currentMirrorId, m_currentMajorVersion,
                                            binaryOs, binaryArch, binaryType);
      info.checksum.clear();
      info.fileSize = 0;
    }
    info.checksumType = "sha256";

    binaries.append(info);
  }

  if (binaries.isEmpty())
  {
    setStatus(JavaDownloadStatus::Idle);
    emit fetchError(tr("未找到适用于当前平台的 Java %1 二进制包").arg(m_currentMajorVersion));
    return;
  }

  if (m_currentMirrorId == "official")
  {
    setStatus(JavaDownloadStatus::Idle);
    emit versionListFetched(binaries);
  }
  else
  {
    m_pendingMirrorBinaries = binaries;
    m_resolvedMirrorBinaries.clear();
    resolveNextMirrorBinary(); // 保持 FetchingBinaries 状态直到全部解析完成
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
    info.architecture = currentArchitecture();
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

// --- 镜像目录解析 ---

void JavaDownloader::resolveNextMirrorBinary()
{
  if (m_pendingMirrorBinaries.isEmpty())
  {
    finishMirrorResolution();
    return;
  }

  JavaBinaryInfo info = m_pendingMirrorBinaries.first();

  if (m_activeReply)
  {
    QNetworkReply *old = m_activeReply;
    m_activeReply = nullptr;
    old->abort();
    old->deleteLater();
  }

  QNetworkRequest request(info.downloadUrl); // 此处存的是目录页 URL
  request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setTransferTimeout(15000);

  QNetworkReply *reply = m_networkManager->get(request);
  m_activeReply = reply;

  connect(reply, &QNetworkReply::finished, this, [this, reply, info]()
  {
    if (!m_activeReply || m_activeReply != reply) return;

    QByteArray data = reply->readAll();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_activeReply = nullptr;
    reply->deleteLater();

    m_pendingMirrorBinaries.removeFirst();

    JavaBinaryInfo resolved = info;
    bool found = false;
    if (statusCode == 200)
    {
      QString html = QString::fromUtf8(data);
      QString ext = platformExtension();
      QString pattern = mirrorFilePattern(info.majorVersion, info.packageType,
                                          info.architecture, info.osName, ext);
      QRegularExpression re(pattern);
      QString best;
      QStringList bestRuns;
      QRegularExpressionMatchIterator it = re.globalMatch(html);
      while (it.hasNext())
      {
        QString name = it.next().captured(0);
        // 去掉可能混入的路径部分
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.mid(slash + 1);
        QString prefix = QString("OpenJDK%1U-%2_%3_%4_hotspot_")
                             .arg(info.majorVersion)
                             .arg(info.packageType)
                             .arg(info.architecture)
                             .arg(info.osName);
        QString verPart = name;
        verPart.remove(prefix);
        verPart.chop(ext.length() + 1);
        QStringList runs = versionRuns(verPart);
        if (best.isEmpty() || compareVersionRuns(runs, bestRuns) > 0)
        {
          best = name;
          bestRuns = runs;
        }
      }
      if (!best.isEmpty())
      {
        resolved.downloadUrl = QUrl(info.downloadUrl).resolved(QUrl(best)).toString();
        found = true;
      }
    }

    if (!found)
    {
      // 该条目在镜像上不可用（目录打不开或没有匹配文件），跳过继续
      resolveNextMirrorBinary();
      return;
    }

    m_resolvedMirrorBinaries.append(resolved);
    fetchMirrorChecksumSidecar(resolved);
  });
}

void JavaDownloader::fetchMirrorChecksumSidecar(const JavaBinaryInfo &info)
{
  if (m_activeReply)
  {
    QNetworkReply *old = m_activeReply;
    m_activeReply = nullptr;
    old->abort();
    old->deleteLater();
  }

  QNetworkRequest request(info.downloadUrl + ".sha256.txt");
  request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setTransferTimeout(10000);

  QNetworkReply *reply = m_networkManager->get(request);
  m_activeReply = reply;

  connect(reply, &QNetworkReply::finished, this, [this, reply, info]()
  {
    if (!m_activeReply || m_activeReply != reply) return;

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data = reply->readAll();
    m_activeReply = nullptr;
    reply->deleteLater();

    if (statusCode == 200)
    {
      QRegularExpression hexRe("([0-9a-fA-F]{64})");
      QRegularExpressionMatch m = hexRe.match(QString::fromUtf8(data));
      if (m.hasMatch())
      {
        for (JavaBinaryInfo &b : m_resolvedMirrorBinaries)
        {
          if (b.downloadUrl == info.downloadUrl)
          {
            b.checksum = m.captured(1);
            b.checksumType = "sha256";
            break;
          }
        }
      }
    }

    // 无论 sidecar 是否存在（TUNA 没有）都继续下一条
    resolveNextMirrorBinary();
  });
}

void JavaDownloader::finishMirrorResolution()
{
  setStatus(JavaDownloadStatus::Idle);

  if (m_resolvedMirrorBinaries.isEmpty())
  {
    emit fetchError(tr("镜像站上没有找到 Java %1 的安装包（非 LTS 版本可能未被镜像收录），"
                       "可切换回官方源或更换版本重试")
                        .arg(m_currentMajorVersion));
    setStatus(JavaDownloadStatus::Failed);
    return;
  }

  emit versionListFetched(m_resolvedMirrorBinaries);
}

// --- 下载操作 ---

void JavaDownloader::downloadJava(const JavaBinaryInfo &binaryInfo,
                                   const QString &savePath)
{
  m_currentSavePath = savePath;
  m_currentChecksum = binaryInfo.checksum;
  m_currentChecksumType = binaryInfo.checksumType;
  setStatus(JavaDownloadStatus::Downloading);
  doDownload(binaryInfo.downloadUrl, savePath);
}

void JavaDownloader::downloadAndExtract(const JavaBinaryInfo &binaryInfo,
                                         const QString &extractDir)
{
  m_currentExtractDir = extractDir;

  // 生成临时下载路径
  QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QString fileName = QString("java_%1_%2_%3.%4")
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

  // 流式写盘：JDK 安装包约 200MB，不能整个读进内存
  QFileInfo fileInfo(savePath);
  if (!fileInfo.absoluteDir().exists())
    QDir().mkpath(fileInfo.absolutePath());

  m_downloadFile = new QFile(savePath, this);
  if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    QString err = m_downloadFile->errorString();
    m_downloadFile->deleteLater();
    m_downloadFile = nullptr;
    emit downloadFailed(tr("写入文件失败: %1").arg(err));
    setStatus(JavaDownloadStatus::Failed);
    return;
  }

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setTransferTimeout(600000); // 10 分钟无数据超时

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

  // 数据到达即写盘
  connect(reply, &QNetworkReply::readyRead, this,
    [this, reply]()
    {
      if (!m_activeReply || m_activeReply != reply || !m_downloadFile) return;
      m_downloadFile->write(reply->readAll());
    });

  // 完成处理
  connect(reply, &QNetworkReply::finished, this, [this, reply, savePath]()
  {
    if (!m_activeReply || m_activeReply != reply) return;

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError error = reply->error();
    if (m_downloadFile)
      m_downloadFile->write(reply->readAll()); // 收尾的零散字节
    m_activeReply = nullptr;
    reply->deleteLater();

    if (m_downloadFile)
    {
      m_downloadFile->close();
      m_downloadFile->deleteLater();
      m_downloadFile = nullptr;
    }

    bool networkOk = (error == QNetworkReply::NoError) &&
                     (statusCode == 200 || statusCode == 206);
    if (!networkOk)
    {
      QFile::remove(savePath);
      QString msg = tr("下载失败 (HTTP %1): %2").arg(statusCode).arg(error);
      if (m_currentMirrorId == "official")
        msg += tr("\n官方源可能无法直接访问，可在“镜像源”中切换到清华 TUNA 等国内镜像后重试。");
      emit downloadFailed(msg);
      setStatus(JavaDownloadStatus::Failed);
      return;
    }

    // 校验 sha256（官方 API 或镜像 sidecar 提供；为空则跳过）
    if (!m_currentChecksum.isEmpty())
    {
      QCryptographicHash hash(QCryptographicHash::Sha256);
      QFile check(savePath);
      bool hashed = check.open(QIODevice::ReadOnly) && hash.addData(&check);
      if (!hashed)
      {
        QFile::remove(savePath);
        emit downloadFailed(tr("无法校验下载的 Java 运行时"));
        setStatus(JavaDownloadStatus::Failed);
        return;
      }
      if (hash.result().toHex().compare(m_currentChecksum.trimmed().toLatin1(),
                                        Qt::CaseInsensitive) != 0)
      {
        check.close();
        QFile::remove(savePath);
        emit downloadFailed(tr("Java 运行时校验失败，下载内容可能已被篡改"));
        setStatus(JavaDownloadStatus::Failed);
        return;
      }
    }

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
       << QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                .arg(Safety::psQuote(QString(archivePath).replace("/", "\\")),
                     Safety::psQuote(QString(extractDir).replace("/", "\\")));

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
  if (m_downloadFile)
  {
    QString partialPath = m_downloadFile->fileName();
    m_downloadFile->close();
    m_downloadFile->deleteLater();
    m_downloadFile = nullptr;
    QFile::remove(partialPath);
  }
  m_pendingMirrorBinaries.clear();
  m_resolvedMirrorBinaries.clear();
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
