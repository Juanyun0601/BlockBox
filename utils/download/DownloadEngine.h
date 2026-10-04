/**
 * @file   DownloadEngine.h
 * @brief  统一下载引擎 — 自包含任务、真正并发、多源回退
 * @author BlockBox Team
 * @date   2026-06-06
 */

#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

struct FileDownloadTask
{
  QStringList candidateUrls;
  QString filePath;
  QString expectedSha1;
  int maxRetries = 3;
};

class DownloadEngine : public QObject
{
  Q_OBJECT

public:
  explicit DownloadEngine(QObject *parent = nullptr);
  ~DownloadEngine();

  void setMaxConcurrency(int max);
  int maxConcurrency() const;

  void downloadFile(const QStringList &candidateUrls, const QString &filePath,
                    const std::function<void(bool success)> &callback,
                    const QString &expectedSha1 = QString(), int maxRetries = 3);

  void downloadFiles(const QVector<FileDownloadTask> &tasks,
                     const std::function<void(int completed, int total)> &progressCallback,
                     const std::function<void(bool allSuccess, const QStringList &failed)> &completionCallback);

  void cancelAll();

signals:
  void downloadProgress(const QString &fileName, int percent);
  void downloadBytesProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
  void tryNextUrl(const QStringList &candidateUrls, int urlIndex, int retryCount,
                  const QString &filePath, const QString &expectedSha1, int maxRetries,
                  const std::function<void(bool)> &callback);
  void downloadSingleUrl(const QString &url, const QString &filePath,
                         const std::function<void(bool)> &callback,
                         const QString &expectedSha1, int redirectDepth = 0);

  QNetworkAccessManager *m_networkManager;
  QSet<QNetworkReply*> m_activeReplies;
  int m_maxConcurrency;
  bool m_cancelled;
};