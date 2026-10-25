/**
 * @file   ForgeDownloader.cpp
 * @brief  Forge多源统一下载器实现 — 基于 MultiMC net 层（NetJob/Download）
 * @author BlockBox Team
 * @date   2026-06-01
 */

#include "ForgeDownloader.h"

#include "../net/Download.h"
#include "../net/ChecksumValidator.h"
#include "../net/NetworkAccess.h"

#include <QDebug>
#include <QFileInfo>

namespace forge {

static const QString BMCL_BASE = QStringLiteral("https://bmclapi2.bangbang93.com");
static const QString FORGE_MAVEN = QStringLiteral("https://maven.minecraftforge.net");
static const QString NEOFORGE_MAVEN = QStringLiteral("https://maven.neoforged.net/releases");
static const QString MINECRAFT_LIBRARIES = QStringLiteral("https://libraries.minecraft.net");

// BMCLAPI 只代理固定几个上游仓库，把安装配置里写死的绝对地址换成镜像地址。
// 认不出来的主机返回空串，调用方会退回原地址
static QString bmclMirrorFor(const QString &url)
{
  if (url.startsWith(NEOFORGE_MAVEN + "/"))
    return BMCL_BASE + "/maven/" + url.mid(NEOFORGE_MAVEN.length() + 1);
  if (url.startsWith(FORGE_MAVEN + "/"))
    return BMCL_BASE + "/maven/" + url.mid(FORGE_MAVEN.length() + 1);
  if (url.startsWith(MINECRAFT_LIBRARIES + "/"))
    return BMCL_BASE + "/maven/" + url.mid(MINECRAFT_LIBRARIES.length() + 1);
  return QString();
}

ForgeDownloader::ForgeDownloader(QObject *parent)
  : QObject(parent)
  , m_downloadSource(DownloadSource::Official)
{
}

ForgeDownloader::~ForgeDownloader()
{
  cancelAll();
}

void ForgeDownloader::setDownloadSource(DownloadSource source)
{
  m_downloadSource = source;
}

DownloadSource ForgeDownloader::downloadSource() const
{
  return m_downloadSource;
}

void ForgeDownloader::setMavenFamily(InstallerFamily family)
{
  m_mavenFamily = family;
}

InstallerFamily ForgeDownloader::mavenFamily() const
{
  return m_mavenFamily;
}

void ForgeDownloader::setMaxConcurrency(int max)
{
  // MultiMC 的 NetJob 内部以 6 个并发 part 为上限，这里仅记录设置
  m_maxConcurrency = qBound(1, max, 128);
}

QStringList ForgeDownloader::getCandidateUrls(const QString &path)
{
  QStringList urls;
  const bool official = (m_downloadSource == DownloadSource::Official);
  const QString primaryMaven = (m_mavenFamily == InstallerFamily::NeoForge) ? NEOFORGE_MAVEN : FORGE_MAVEN;
  const QString secondaryMaven = (m_mavenFamily == InstallerFamily::NeoForge) ? FORGE_MAVEN : NEOFORGE_MAVEN;

  if (official)
  {
    urls << primaryMaven + "/" + path;
    urls << secondaryMaven + "/" + path;
    urls << MINECRAFT_LIBRARIES + "/" + path;
    urls << BMCL_BASE + "/maven/" + path;
  }
  else
  {
    urls << BMCL_BASE + "/maven/" + path;
    urls << primaryMaven + "/" + path;
    urls << secondaryMaven + "/" + path;
    urls << MINECRAFT_LIBRARIES + "/" + path;
  }

  return urls;
}

QStringList ForgeDownloader::getCandidateUrlsForRemote(const QString &absoluteUrl)
{
  if (absoluteUrl.isEmpty())
    return QStringList();

  QStringList urls;
  const QString mirrored = bmclMirrorFor(absoluteUrl);

  if (m_downloadSource == DownloadSource::Official)
  {
    urls << absoluteUrl;
    if (!mirrored.isEmpty() && mirrored != absoluteUrl)
      urls << mirrored;
  }
  else
  {
    if (!mirrored.isEmpty())
      urls << mirrored;
    urls << absoluteUrl;
  }

  return urls;
}

QStringList ForgeDownloader::getInstallerCandidateUrls(const QString &mcVersion, const QString &forgeVersion)
{
  QStringList urls;
  QString primarySource = (m_downloadSource == DownloadSource::Official) ? FORGE_MAVEN : BMCL_BASE;

  QString forgeVersionFull = mcVersion + "-" + forgeVersion;

  auto buildMavenUrl = [&](const QString &base) -> QString
  {
    if (base == BMCL_BASE)
    {
      return BMCL_BASE + "/forge/download?mcversion=" + mcVersion +
             "&version=" + forgeVersion + "&category=installer&format=jar";
    }
    QString path = "net/minecraftforge/forge/" + forgeVersionFull +
                   "/forge-" + forgeVersionFull + "-installer.jar";
    return FORGE_MAVEN + "/" + path;
  };

  if (primarySource == FORGE_MAVEN)
  {
    urls << buildMavenUrl(FORGE_MAVEN);
    urls << buildMavenUrl(BMCL_BASE);
  }
  else
  {
    urls << buildMavenUrl(BMCL_BASE);
    urls << buildMavenUrl(FORGE_MAVEN);
  }

  return urls;
}

QStringList ForgeDownloader::getNeoForgeInstallerCandidateUrls(const QString &neoForgeVersion)
{
  // NeoForge 安装包不走 forge/download 那套参数，官方地址与 BMCL 地址格式不同，
  // 两边都放进候选列表：首选源在前，另一个作回退
  const QString mavenPath = "net/neoforged/neoforge/" + neoForgeVersion +
                            "/neoforge-" + neoForgeVersion + "-installer.jar";
  const QString mavenUrl = NEOFORGE_MAVEN + "/" + mavenPath;
  const QString bmclUrl = BMCL_BASE + "/neoforge/version/" + neoForgeVersion + "/download/installer.jar";
  const QString bmclMavenUrl = BMCL_BASE + "/maven/" + mavenPath;

  QStringList urls;
  if (m_downloadSource == DownloadSource::Official)
  {
    urls << mavenUrl << bmclUrl << bmclMavenUrl;
  }
  else
  {
    urls << bmclUrl << bmclMavenUrl << mavenUrl;
  }
  return urls;
}
// 失败按顺序换源；expectedSha1 非空时挂 SHA1 校验
static Net::Download::Ptr makeDownload(const QStringList &candidateUrls, const QString &filePath,
                                       const QString &expectedSha1)
{
  auto dl = Net::Download::makeFile(QUrl(candidateUrls.first()), filePath);
  for (int i = 1; i < candidateUrls.size(); ++i)
    dl->addUrlFallback(QUrl(candidateUrls.at(i)));
  if (!expectedSha1.isEmpty())
  {
    dl->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1,
                                                QByteArray::fromHex(expectedSha1.toLatin1())));
  }
  return dl;
}

void ForgeDownloader::downloadFile(const QStringList &candidateUrls, const QString &filePath,
                                    const std::function<void(bool success)> &callback,
                                    const QString &expectedSha1, int maxRetries)
{
  Q_UNUSED(maxRetries); // NetJob 对每个 part 自带 3 次重试

  auto job = NetJob::Ptr(new NetJob(QFileInfo(filePath).fileName(),
                                    Net::networkAccessManager()));
  auto dl = makeDownload(candidateUrls, filePath, expectedSha1);

  const QString fileName = QFileInfo(filePath).fileName();
  connect(dl.get(), &NetAction::netActionProgress, this,
          [this, fileName](int, qint64 received, qint64 total) {
    emit downloadProgress(fileName, total > 0 ? static_cast<int>(received * 100 / total) : 0);
    emit downloadBytesProgress(received, total);
  });

  job->addNetAction(dl);

  connect(job.get(), &Task::finished, this, [this, job, callback]() {
    const bool ok = job->wasSuccessful();
    m_activeJobs.removeAll(job);
    if (callback)
      callback(ok);
  });

  m_activeJobs.append(job);
  job->start();
}

void ForgeDownloader::downloadFiles(const QVector<QPair<QStringList, QString>> &tasks,
                                     const std::function<void(int completed, int total)> &progressCallback,
                                     const std::function<void(bool allSuccess, const QStringList &failed)> &completionCallback)
{
  QVector<ForgeFileDownload> converted;
  converted.reserve(tasks.size());
  for (const auto &task : tasks)
    converted.append({task.first, task.second, QString()});
  downloadFiles(converted, progressCallback, completionCallback);
}

void ForgeDownloader::downloadFiles(const QVector<ForgeFileDownload> &tasks,
                                     const std::function<void(int completed, int total)> &progressCallback,
                                     const std::function<void(bool allSuccess, const QStringList &failed)> &completionCallback)
{
  if (tasks.isEmpty())
  {
    if (completionCallback)
      completionCallback(true, {});
    return;
  }

  auto job = NetJob::Ptr(new NetJob(QStringLiteral("ForgeFiles"),
                                    Net::networkAccessManager()));

  const int total = tasks.size();
  auto finishedCount = std::make_shared<int>(0);
  auto failedFiles = std::make_shared<QStringList>();

  for (const auto &task : tasks)
  {
    auto dl = makeDownload(task.urls, task.filePath, task.expectedSha1);

    const QString fileName = QFileInfo(task.filePath).fileName();
    connect(dl.get(), &NetAction::netActionProgress, this,
            [this, fileName](int, qint64 received, qint64 totalBytes) {
      emit downloadProgress(fileName, totalBytes > 0
                                          ? static_cast<int>(received * 100 / totalBytes)
                                          : 0);
      emit downloadBytesProgress(received, totalBytes);
    });

    const QString filePath = task.filePath;
    connect(dl.get(), &NetAction::succeeded, this,
            [this, finishedCount, progressCallback, total](int) {
      ++(*finishedCount);
      if (progressCallback)
        progressCallback(*finishedCount, total);
    });
    connect(dl.get(), &NetAction::failed, this,
            [this, finishedCount, failedFiles, progressCallback, total, filePath](int) {
      ++(*finishedCount);
      failedFiles->append(filePath);
      if (progressCallback)
        progressCallback(*finishedCount, total);
    });

    job->addNetAction(dl);
  }

  connect(job.get(), &Task::finished, this, [this, job, completionCallback, failedFiles]() {
    const bool allSuccess = job->wasSuccessful();
    m_activeJobs.removeAll(job);
    if (completionCallback)
      completionCallback(allSuccess, *failedFiles);
  });

  m_activeJobs.append(job);
  job->start();
}

void ForgeDownloader::cancelAll()
{
  auto jobs = m_activeJobs;
  m_activeJobs.clear();
  for (const auto &job : jobs)
  {
    if (job)
      job->abort();
  }
}

} // namespace forge
