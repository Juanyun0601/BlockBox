/**
 * @file   DownloadEngine.cpp
 * @brief  统一下载引擎实现 — 每个下载任务自包含，无共享可变状态
 * @author BlockBox Team
 * @date   2026-06-06
 */

#include "DownloadEngine.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <QUrl>

DownloadEngine::DownloadEngine(QObject *parent)
  : QObject(parent)
  , m_networkManager(new QNetworkAccessManager(this))
  , m_maxConcurrency(6)
  , m_cancelled(false)
{
}

DownloadEngine::~DownloadEngine()
{
  cancelAll();
}

void DownloadEngine::setMaxConcurrency(int max)
{
  m_maxConcurrency = qMax(1, qMin(max, 128));
}

int DownloadEngine::maxConcurrency() const
{
  return m_maxConcurrency;
}

void DownloadEngine::downloadFile(const QStringList &candidateUrls, const QString &filePath,
                                   const std::function<void(bool success)> &callback,
                                   const QString &expectedSha1, int maxRetries)
{
  if (candidateUrls.isEmpty())
  {
    qWarning() << "[DownloadEngine] No candidate URLs provided";
    if (callback) callback(false);
    return;
  }

  tryNextUrl(candidateUrls, 0, 0, filePath, expectedSha1, maxRetries, callback);
}

void DownloadEngine::tryNextUrl(const QStringList &candidateUrls, int urlIndex, int retryCount,
                                 const QString &filePath, const QString &expectedSha1, int maxRetries,
                                 const std::function<void(bool)> &callback)
{
  if (m_cancelled)
  {
    if (callback) callback(false);
    return;
  }

  if (urlIndex >= candidateUrls.size())
  {
    qWarning() << "[DownloadEngine] All download URLs failed for:" << filePath;
    if (callback) callback(false);
    return;
  }

  QString currentUrl = candidateUrls[urlIndex];
  qDebug() << "[DownloadEngine] Trying URL:" << currentUrl
           << "retry:" << retryCount << "of" << maxRetries;

  QPointer<DownloadEngine> self(this);

  downloadSingleUrl(currentUrl, filePath,
    [self, candidateUrls, urlIndex, retryCount, filePath, expectedSha1, maxRetries, callback](bool success)
    {
      if (!self) return;

      if (success)
      {
        if (callback) callback(true);
        return;
      }

      if (self->m_cancelled)
      {
        if (callback) callback(false);
        return;
      }

      if (retryCount + 1 < maxRetries)
      {
        int delay = 500 * (retryCount + 1);
        QTimer::singleShot(delay, self, [self, candidateUrls, urlIndex, retryCount,
                                          filePath, expectedSha1, maxRetries, callback]()
        {
          if (!self) return;
          self->tryNextUrl(candidateUrls, urlIndex, retryCount + 1,
                     filePath, expectedSha1, maxRetries, callback);
        });
        return;
      }

      QTimer::singleShot(500, self, [self, candidateUrls, urlIndex, filePath,
                                      expectedSha1, maxRetries, callback]()
      {
        if (!self) return;
        self->tryNextUrl(candidateUrls, urlIndex + 1, 0,
                   filePath, expectedSha1, maxRetries, callback);
      });
    },
    expectedSha1);
}

void DownloadEngine::downloadSingleUrl(const QString &url, const QString &filePath,
                                        const std::function<void(bool)> &callback,
                                        const QString &expectedSha1, int redirectDepth)
{
  if (redirectDepth > 10)
  {
    qWarning() << "[DownloadEngine] Too many redirects for:" << url;
    if (callback) callback(false);
    return;
  }

  if (m_cancelled)
  {
    if (callback) callback(false);
    return;
  }

  QPointer<DownloadEngine> self(this);

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
  request.setRawHeader("Accept", "*/*");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(30000);

  QNetworkReply *reply = m_networkManager->get(request);
  m_activeReplies.insert(reply);

  connect(reply, &QNetworkReply::finished, this,
    [self, reply, url, filePath, callback, expectedSha1, redirectDepth]()
    {
      if (!self) return;

      if (self->m_activeReplies.contains(reply))
      {
        self->m_activeReplies.remove(reply);
      }

      if (self->m_cancelled)
      {
        reply->deleteLater();
        if (callback) callback(false);
        return;
      }

      int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

      if (statusCode == 301 || statusCode == 302 || statusCode == 307 || statusCode == 308)
      {
        QString redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toString();
        QUrl baseUrl(url);
        QUrl resolvedUrl = baseUrl.resolved(redirectUrl);
        reply->deleteLater();
        self->downloadSingleUrl(resolvedUrl.toString(), filePath, callback, expectedSha1, redirectDepth + 1);
        return;
      }

      if (statusCode == 404)
      {
        reply->deleteLater();
        qWarning() << "[DownloadEngine] 404 Not Found:" << url;
        if (callback) callback(false);
        return;
      }

      if (reply->error() != QNetworkReply::NoError)
      {
        QString errorStr = reply->errorString();
        reply->deleteLater();
        qWarning() << "[DownloadEngine] Download error:" << errorStr << "for URL:" << url;
        if (callback) callback(false);
        return;
      }

      QByteArray data = reply->readAll();
      reply->deleteLater();

      if (!expectedSha1.isEmpty())
      {
        QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
        sha1Hash.addData(data);
        QString actualSha1 = sha1Hash.result().toHex();
        if (actualSha1 != expectedSha1)
        {
          qWarning() << "[DownloadEngine] SHA1 mismatch for:" << url
                     << "expected:" << expectedSha1 << "actual:" << actualSha1;
          if (callback) callback(false);
          return;
        }
      }

      QFile file(filePath);
      QFileInfo fileInfo(filePath);
      QDir dir = fileInfo.absoluteDir();
      if (!dir.exists())
      {
        dir.mkpath(".");
      }

      if (file.open(QIODevice::WriteOnly))
      {
        file.write(data);
        file.close();
        qDebug() << "[DownloadEngine] Downloaded:" << filePath << "from:" << url;
        if (callback) callback(true);
      }
      else
      {
        qWarning() << "[DownloadEngine] Failed to write file:" << filePath;
        if (callback) callback(false);
      }
    });

  connect(reply, &QNetworkReply::downloadProgress, this,
    [self, reply, filePath](qint64 bytesReceived, qint64 bytesTotal)
    {
      if (!self) return;

      // 限制进度更新频率至 200ms（参考 ProjBobcat 做法）
      qint64 now = QDateTime::currentMSecsSinceEpoch();
      QVariant lastEmitVar = reply->property("lastProgressEmit");
      if (lastEmitVar.isValid()) {
          qint64 lastEmit = lastEmitVar.toLongLong();
          if (now - lastEmit < 200) {
              return;
          }
      }
      reply->setProperty("lastProgressEmit", now);

      int percent = 0;
      if (bytesTotal > 0)
      {
        percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
      }
      emit self->downloadProgress(filePath, percent);
      emit self->downloadBytesProgress(bytesReceived, bytesTotal);
    });
}

void DownloadEngine::downloadFiles(const QVector<FileDownloadTask> &tasks,
                                    const std::function<void(int completed, int total)> &progressCallback,
                                    const std::function<void(bool allSuccess, const QStringList &failed)> &completionCallback)
{
  if (tasks.isEmpty())
  {
    if (completionCallback) completionCallback(true, {});
    return;
  }

  m_cancelled = false;

  QPointer<DownloadEngine> self(this);

  auto completed = std::make_shared<int>(0);
  auto failed = std::make_shared<int>(0);
  auto failedFiles = std::make_shared<QStringList>();
  auto activeCount = std::make_shared<int>(0);
  auto taskIndex = std::make_shared<int>(0);
  int total = tasks.size();

  auto launchNext = std::make_shared<std::function<void()>>();

  *launchNext = [self, tasks, completed, failed, failedFiles, activeCount,
                  taskIndex, total, progressCallback, completionCallback, launchNext]()
  {
    while (self && *activeCount < self->m_maxConcurrency && *taskIndex < total && !self->m_cancelled)
    {
      int idx = (*taskIndex)++;
      const FileDownloadTask &task = tasks[idx];

      (*activeCount)++;

      self->downloadFile(task.candidateUrls, task.filePath,
        [self, completed, failed, failedFiles, activeCount, taskIndex, total,
         progressCallback, completionCallback, launchNext, task](bool success)
        {
          if (success)
          {
            (*completed)++;
          }
          else
          {
            (*failed)++;
            failedFiles->append(task.filePath);
          }

          if (progressCallback)
          {
            progressCallback(*completed + *failed, total);
          }

          (*activeCount)--;

          if (!self)
          {
            return;
          }

          if (*activeCount == 0 && *taskIndex >= total)
          {
            bool allSuccess = (*failed == 0);
            if (completionCallback)
            {
              completionCallback(allSuccess, *failedFiles);
            }
          }
          else if (*activeCount < self->m_maxConcurrency && *taskIndex < total && !self->m_cancelled)
          {
            (*launchNext)();
          }
        },
        task.expectedSha1, task.maxRetries);
    }
  };

  (*launchNext)();
}

void DownloadEngine::cancelAll()
{
  m_cancelled = true;

  QSet<QNetworkReply*> replies = m_activeReplies;
  m_activeReplies.clear();

  for (QNetworkReply *reply : replies)
  {
    reply->abort();
    reply->deleteLater();
  }
}