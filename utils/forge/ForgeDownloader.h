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
#include "ForgeProfile.h"

namespace forge {

enum class DownloadSource
{
  Official,
  BMCL,
  Auto
};

/// 单个待下载文件：候选地址 + 落盘路径 + 可选 SHA1 校验
struct ForgeFileDownload
{
  QStringList urls;
  QString filePath;
  QString expectedSha1;
};

class ForgeDownloader : public QObject
{
  Q_OBJECT
public:
  explicit ForgeDownloader(QObject *parent = nullptr);
  ~ForgeDownloader();

  void setDownloadSource(DownloadSource source);
  DownloadSource downloadSource() const;
  /// 选择主 Maven 仓库：Forge 走 maven.minecraftforge.net，NeoForge 走 maven.neoforged.net
  void setMavenFamily(InstallerFamily family);
  InstallerFamily mavenFamily() const;
  /// MultiMC 的 NetJob 内部固定 6 路并发；此设置保留接口供上层调用
  void setMaxConcurrency(int max);

  QStringList getCandidateUrls(const QString &path);
  QStringList getInstallerCandidateUrls(const QString &mcVersion, const QString &forgeVersion);
  QStringList getNeoForgeInstallerCandidateUrls(const QString &neoForgeVersion);
  /// 安装配置里写死了绝对下载地址时，按「镜像优先/官方优先」生成候选列表
  QStringList getCandidateUrlsForRemote(const QString &absoluteUrl);

  void downloadFile(const QStringList &candidateUrls, const QString &filePath,
                    const std::function<void(bool success)> &callback,
                    const QString &expectedSha1 = QString(), int maxRetries = 3);

  void downloadFiles(const QVector<QPair<QStringList, QString>> &tasks,
                     const std::function<void(int completed, int total)> &progressCallback,
                     const std::function<void(bool allSuccess, const QStringList &failed)> &completionCallback);

  /// MultiMC 式批量下载：一个 NetJob 内 6 路并发，逐文件可挂 SHA1 校验
  void downloadFiles(const QVector<ForgeFileDownload> &tasks,
                     const std::function<void(int completed, int total)> &progressCallback,
                     const std::function<void(bool allSuccess, const QStringList &failed)> &completionCallback);

  void cancelAll();

signals:
  void downloadProgress(const QString &fileName, int percent);
  void downloadBytesProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
  DownloadSource m_downloadSource;
  InstallerFamily m_mavenFamily = InstallerFamily::Forge;
  int m_maxConcurrency = 6;
  QList<NetJob::Ptr> m_activeJobs;
};

} // namespace forge
