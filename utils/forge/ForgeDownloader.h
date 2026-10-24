/**
 * @file   ForgeDownloader.h
 * @brief  Forge多源统一下载器 — 基于 MultiMC net 层（NetJob/Download）
 * @author BlockBox Team
 * @date   2026-06-01
 */

#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>
#include <QPair>

#include <functional>

#include "../net/NetJob.h"

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
  /// MultiMC 的 NetJob 内部固定 6 路并发；此设置保留接口供上层调用
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
  DownloadSource m_downloadSource;
  int m_maxConcurrency = 6;
  QList<NetJob::Ptr> m_activeJobs;
};

} // namespace forge
