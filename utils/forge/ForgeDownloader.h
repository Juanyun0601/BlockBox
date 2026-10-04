/**
 * @file   ForgeDownloader.h
 * @brief  Forge多源统一下载器 — 委托给 DownloadEngine
 * @author BlockBox Team
 * @date   2026-06-01
 */

#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>
#include <QPair>

#include <functional>

#include "../download/DownloadEngine.h"

namespace forge {

enum class DownloadSource
{
  Official,
  BMCL,
  Auto
};

class ForgeDownloader : public QObject
{
  Q_OBJECT
public:
  explicit ForgeDownloader(QObject *parent = nullptr);
  ~ForgeDownloader();

  void setDownloadSource(DownloadSource source);
  DownloadSource downloadSource() const;
  void setMaxConcurrency(int max);

  QStringList getCandidateUrls(const QString &path);
  QStringList getInstallerCandidateUrls(const QString &mcVersion, const QString &forgeVersion);

  void downloadFile(const QStringList &candidateUrls, const QString &filePath,
                    const std::function<void(bool success)> &callback,
                    const QString &expectedSha1 = QString(), int maxRetries = 3);

  void downloadFiles(const QVector<QPair<QStringList, QString>> &tasks,
                     const std::function<void(int completed, int total)> &progressCallback,
                     const std::function<void(bool allSuccess, const QStringList &failed)> &completionCallback);

  void cancelAll();

signals:
  void downloadProgress(const QString &fileName, int percent);
  void downloadBytesProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
  DownloadEngine *m_engine;
  DownloadSource m_downloadSource;
};

} // namespace forge