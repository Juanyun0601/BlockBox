/**
 * @file   JavaDownloadPage.h
 * @brief  Java 下载页面 — 多版本选择、多发行版、多镜像源
 * @author BlockBox Team
 * @date   2026-07-01
 */

#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QProgressBar>
#include <QListWidget>

#include "components/OutlinedLabel.h"
#include "utils/JavaDownloader.h"

class JavaDownloadPage : public QWidget
{
  Q_OBJECT

public:
  explicit JavaDownloadPage(QWidget *parent = nullptr);
  ~JavaDownloadPage() = default;

signals:
  void backToSettingsRequested();
  void javaInstalled(const QString &javaPath, const QString &versionName);

private slots:
  void onDistributionChanged(int index);
  void onMirrorChanged(int index);
  void onVersionChanged(int index);
  void onDownloadClicked();
  void onExtractPathBrowseClicked();
  void onCancelClicked();

  void onStatusChanged(JavaDownloadStatus status);
  void onVersionListFetched(const QVector<JavaBinaryInfo> &binaries);
  void onFetchError(const QString &error);
  void onDownloadProgress(const QString &fileName, int percent);
  void onDownloadBytesProgress(qint64 bytesReceived, qint64 bytesTotal);
  void onDownloadCompleted(const QString &filePath);
  void onDownloadFailed(const QString &error);
  void onExtractCompleted(const QString &javaPath);
  void onExtractFailed(const QString &error);

private:
  void initUI();
  void updateUIState();
  void updateBinaryInfo();
  QString formatFileSize(qint64 bytes) const;
  QString defaultJavaInstallPath() const;
  QString findJavaExecutable(const QString &dir) const;

  // 顶部区域
  OutlinedLabel *m_titleLabel;
  QLabel *m_subtitleLabel;

  // 发行版选择
  QComboBox *m_distributionCombo;
  QLabel *m_distributionDescLabel;

  // 镜像源选择
  QComboBox *m_mirrorCombo;

  // 版本选择
  QComboBox *m_versionCombo;
  QListWidget *m_binaryListWidget;

  // 二进制信息
  QLabel *m_binaryDetailLabel;

  // 安装路径
  QLabel *m_extractPathLabel;
  QPushButton *m_extractPathBrowseBtn;

  // 进度
  QProgressBar *m_progressBar;
  QLabel *m_progressDetailLabel;

  // 操作按钮
  QPushButton *m_downloadBtn;
  QPushButton *m_cancelBtn;
  QPushButton *m_backBtn;

  // 状态
  QLabel *m_statusLabel;

  // 数据
  QVector<JavaBinaryInfo> m_currentBinaries;
  JavaBinaryInfo m_selectedBinary;
  QString m_extractPath;
  bool m_isDownloading;
};