/**
 * @file   ForgeDownloader.cpp
 * @brief  Forge多源统一下载器实现 — 委托给 DownloadEngine
 * @author BlockBox Team
 * @date   2026-06-01
 */

#include "ForgeDownloader.h"

#include <QDebug>

namespace forge {

static const QString BMCL_BASE = QStringLiteral("https://bmclapi2.bangbang93.com");
static const QString FORGE_MAVEN = QStringLiteral("https://maven.minecraftforge.net");
static const QString MINECRAFT_LIBRARIES = QStringLiteral("https://libraries.minecraft.net");

ForgeDownloader::ForgeDownloader(QObject *parent)
  : QObject(parent)
  , m_engine(new DownloadEngine(this))
  , m_downloadSource(DownloadSource::Official)
{
  connect(m_engine, &DownloadEngine::downloadProgress,
          this, &ForgeDownloader::downloadProgress);
  connect(m_engine, &DownloadEngine::downloadBytesProgress,
          this, &ForgeDownloader::downloadBytesProgress);
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

void ForgeDownloader::setMaxConcurrency(int max)
{
  m_engine->setMaxConcurrency(max);
}

QStringList ForgeDownloader::getCandidateUrls(const QString &path)
{
  QStringList urls;
  QString primarySource = (m_downloadSource == DownloadSource::Official) ? FORGE_MAVEN : BMCL_BASE;

  if (primarySource == FORGE_MAVEN)
  {
    urls << FORGE_MAVEN + "/" + path;
    urls << MINECRAFT_LIBRARIES + "/" + path;
    urls << BMCL_BASE + "/maven/" + path;
  }
  else
  {
    urls << BMCL_BASE + "/maven/" + path;
    urls << FORGE_MAVEN + "/" + path;
    urls << MINECRAFT_LIBRARIES + "/" + path;
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

void ForgeDownloader::downloadFile(const QStringList &candidateUrls, const QString &filePath,
                                    const std::function<void(bool success)> &callback,
                                    const QString &expectedSha1, int maxRetries)
{
  m_engine->downloadFile(candidateUrls, filePath, callback, expectedSha1, maxRetries);
}

void ForgeDownloader::downloadFiles(const QVector<QPair<QStringList, QString>> &tasks,
                                     const std::function<void(int completed, int total)> &progressCallback,
                                     const std::function<void(bool allSuccess, const QStringList &failed)> &completionCallback)
{
  QVector<FileDownloadTask> engineTasks;
  engineTasks.reserve(tasks.size());

  for (const auto &task : tasks)
  {
    FileDownloadTask dt;
    dt.candidateUrls = task.first;
    dt.filePath = task.second;
    engineTasks.append(dt);
  }

  m_engine->downloadFiles(engineTasks, progressCallback, completionCallback);
}

void ForgeDownloader::cancelAll()
{
  m_engine->cancelAll();
}

} // namespace forge