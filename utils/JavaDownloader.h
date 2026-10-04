/**
 * @file   JavaDownloader.h
 * @brief  Java 下载器 — 多版本、多镜像源、多发行版
 * @author BlockBox Team
 * @date   2026-07-01
 */

#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>

#include <functional>

/**
 * @brief Java 发行版信息
 */
struct JavaDistribution
{
  QString name;        // 显示名称
  QString id;          // 内部标识
  QString description; // 描述
};

/**
 * @brief 镜像源信息
 */
struct JavaMirror
{
  QString name;  // 显示名称
  QString id;    // 内部标识
  QString baseUrl; // 基础 URL
};

/**
 * @brief Java 版本信息
 */
struct JavaVersionInfo
{
  int majorVersion;     // 主版本号 (8, 11, 17, 21, ...)
  QString versionName;  // 版本名称 (e.g. "Java 17")
  QString releaseName;  // 发行版全名
  bool isLts;           // 是否为 LTS 版本
};

/**
 * @brief 可下载的 Java 二进制包信息
 */
struct JavaBinaryInfo
{
  QString versionName;     // 版本名称
  int majorVersion;        // 主版本号
  QString distributionId;  // 发行版 ID
  QString distributionName;// 发行版显示名
  QString downloadUrl;     // 直接下载链接
  QString checksum;        // SHA256 校验和
  QString checksumType;    // 校验类型
  QString architecture;    // 架构 (x64, aarch64, ...)
  QString osName;          // 操作系统
  QString packageType;     // 包类型 (jdk, jre)
  qint64 fileSize;         // 文件大小 (bytes)
};

/**
 * @brief Java 下载任务状态
 */
enum class JavaDownloadStatus
{
  Idle,
  FetchingVersions,
  FetchingBinaries,
  Downloading,
  Extracting,
  Completed,
  Failed
};

class JavaDownloader : public QObject
{
  Q_OBJECT

public:
  static JavaDownloader* instance();

  // --- 发行版列表 ---
  QVector<JavaDistribution> availableDistributions() const;

  // --- 镜像源列表 ---
  QVector<JavaMirror> availableMirrors() const;

  // --- 支持的 Java 版本列表 ---
  QVector<JavaVersionInfo> supportedVersions() const;

  // --- 获取当前平台信息 ---
  QString currentOS() const;
  QString currentArchitecture() const;

  // --- 核心操作 ---
  void fetchAvailableBinaries(const QString &distributionId,
                              const QString &mirrorId,
                              int majorVersion);

  void downloadJava(const JavaBinaryInfo &binaryInfo,
                    const QString &savePath);

  void downloadAndExtract(const JavaBinaryInfo &binaryInfo,
                          const QString &extractDir);

  void cancel();

  JavaDownloadStatus status() const { return m_status; }

signals:
  void statusChanged(JavaDownloadStatus status);
  void versionListFetched(const QVector<JavaBinaryInfo> &binaries);
  void fetchError(const QString &error);
  void downloadProgress(const QString &fileName, int percent);
  void downloadBytesProgress(qint64 bytesReceived, qint64 bytesTotal);
  void downloadCompleted(const QString &filePath);
  void downloadFailed(const QString &error);
  void extractProgress(int percent);
  void extractCompleted(const QString &javaPath);
  void extractFailed(const QString &error);

private:
  explicit JavaDownloader(QObject *parent = nullptr);
  ~JavaDownloader() = default;

  // 构建下载 URL
  QString buildAdoptiumUrl(const QString &mirrorId, int majorVersion);
  QString buildCorrettoUrl(const QString &mirrorId, int majorVersion);
  QString buildMicrosoftUrl(const QString &mirrorId, int majorVersion);
  QString buildZuluUrl(int majorVersion);

  // 解析 API 响应
  void parseAdoptiumResponse(const QByteArray &data);
  void parseCorrettoResponse(const QByteArray &data, int majorVersion);
  void parseZuluResponse(const QByteArray &data, int majorVersion);

  // 下载与解压
  void doDownload(const QString &url, const QString &savePath);
  void doExtract(const QString &archivePath, const QString &extractDir);

  // 查找解压后的 Java Home 目录
  QString findJavaHome(const QString &extractDir) const;

  // 设置状态
  void setStatus(JavaDownloadStatus status);

  // 获取平台后缀
  QString platformExtension() const;

  QNetworkAccessManager *m_networkManager;
  QNetworkReply *m_activeReply;
  JavaDownloadStatus m_status;

  QString m_currentDistributionId;
  QString m_currentMirrorId;
  int m_currentMajorVersion;
  QString m_currentSavePath;
  QString m_currentExtractDir;
  QString m_currentArchivePath;
};