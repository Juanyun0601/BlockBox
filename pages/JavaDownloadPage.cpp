/**
 * @file   JavaDownloadPage.cpp
 * @brief  Java 下载页面实现
 * @author BlockBox Team
 * @date   2026-07-01
 */

#include "JavaDownloadPage.h"

#include <QComboBox>
#include <QDebug>
#include "components/AppFileDialog.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include "components/AppMessageBox.h"
#include <QStandardPaths>
#include <QStyle>

#include "components/NotificationManager.h"
#include "utils/SettingsManager.h"

JavaDownloadPage::JavaDownloadPage(QWidget *parent)
  : QWidget(parent)
  , m_isDownloading(false)
{
  initUI();

  JavaDownloader *downloader = JavaDownloader::instance();

  connect(downloader, &JavaDownloader::statusChanged,
          this, &JavaDownloadPage::onStatusChanged);
  connect(downloader, &JavaDownloader::versionListFetched,
          this, &JavaDownloadPage::onVersionListFetched);
  connect(downloader, &JavaDownloader::fetchError,
          this, &JavaDownloadPage::onFetchError);
  connect(downloader, &JavaDownloader::downloadProgress,
          this, &JavaDownloadPage::onDownloadProgress);
  connect(downloader, &JavaDownloader::downloadBytesProgress,
          this, &JavaDownloadPage::onDownloadBytesProgress);
  connect(downloader, &JavaDownloader::downloadCompleted,
          this, &JavaDownloadPage::onDownloadCompleted);
  connect(downloader, &JavaDownloader::downloadFailed,
          this, &JavaDownloadPage::onDownloadFailed);
  connect(downloader, &JavaDownloader::extractCompleted,
          this, &JavaDownloadPage::onExtractCompleted);
  connect(downloader, &JavaDownloader::extractFailed,
          this, &JavaDownloadPage::onExtractFailed);

  // 默认安装路径
  m_extractPath = defaultJavaInstallPath();
  m_extractPathLabel->setText(m_extractPath);

  // 初始化版本列表
  QVector<JavaVersionInfo> versions = downloader->supportedVersions();
  for (const JavaVersionInfo &v : versions)
  {
    QString label = v.versionName;
    if (v.isLts) label += " (LTS)";
    m_versionCombo->addItem(label, v.majorVersion);
  }

  // 默认选中 Java 21
  int idx21 = m_versionCombo->findData(21);
  if (idx21 >= 0)
    m_versionCombo->setCurrentIndex(idx21);
  else
    m_versionCombo->setCurrentIndex(0);

  updateUIState();
}

void JavaDownloadPage::initUI()
{
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(32, 24, 32, 24);
  mainLayout->setSpacing(16);

  // --- 标题区域 ---
  m_titleLabel = new OutlinedLabel(tr("Java 下载管理"), this);
  m_titleLabel->setObjectName("sectionTitle");

  m_subtitleLabel = new QLabel(tr("选择 Java 发行版、版本和镜像源，下载并安装 Java 运行环境"), this);
  m_subtitleLabel->setObjectName("sectionSubtitle");
  m_subtitleLabel->setWordWrap(true);

  // --- 发行版选择 ---
  QGroupBox *distGroup = new QGroupBox(tr("发行版"), this);
  QVBoxLayout *distLayout = new QVBoxLayout(distGroup);

  m_distributionCombo = new QComboBox(distGroup);
  QVector<JavaDistribution> dists = JavaDownloader::instance()->availableDistributions();
  for (const JavaDistribution &d : dists)
    m_distributionCombo->addItem(d.name, d.id);

  m_distributionDescLabel = new QLabel(distGroup);
  m_distributionDescLabel->setObjectName("hintLabel");
  m_distributionDescLabel->setWordWrap(true);
  m_distributionDescLabel->setText(dists.first().description);

  connect(m_distributionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &JavaDownloadPage::onDistributionChanged);

  distLayout->addWidget(m_distributionCombo);
  distLayout->addWidget(m_distributionDescLabel);

  // --- 镜像源选择 ---
  QGroupBox *mirrorGroup = new QGroupBox(tr("镜像源"), this);
  QVBoxLayout *mirrorLayout = new QVBoxLayout(mirrorGroup);

  m_mirrorCombo = new QComboBox(mirrorGroup);
  QVector<JavaMirror> mirrors = JavaDownloader::instance()->availableMirrors();
  for (const JavaMirror &m : mirrors)
    m_mirrorCombo->addItem(m.name, m.id);

  connect(m_mirrorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &JavaDownloadPage::onMirrorChanged);

  mirrorLayout->addWidget(m_mirrorCombo);

  // --- 版本选择 ---
  QGroupBox *versionGroup = new QGroupBox(tr("Java 版本"), this);
  QVBoxLayout *versionLayout = new QVBoxLayout(versionGroup);

  m_versionCombo = new QComboBox(versionGroup);
  connect(m_versionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &JavaDownloadPage::onVersionChanged);

  m_binaryListWidget = new QListWidget(versionGroup);
  m_binaryListWidget->setMaximumHeight(80);
  m_binaryListWidget->setVisible(false);

  m_binaryDetailLabel = new QLabel(versionGroup);
  m_binaryDetailLabel->setObjectName("hintLabel");
  m_binaryDetailLabel->setWordWrap(true);

  versionLayout->addWidget(m_versionCombo);
  versionLayout->addWidget(m_binaryListWidget);
  versionLayout->addWidget(m_binaryDetailLabel);

  // --- 安装路径 ---
  QGroupBox *pathGroup = new QGroupBox(tr("安装路径"), this);
  QHBoxLayout *pathLayout = new QHBoxLayout(pathGroup);

  m_extractPathLabel = new QLabel(pathGroup);
  m_extractPathLabel->setObjectName("hintLabel");
  m_extractPathLabel->setWordWrap(true);

  m_extractPathBrowseBtn = new QPushButton(tr("浏览..."), pathGroup);
  connect(m_extractPathBrowseBtn, &QPushButton::clicked,
          this, &JavaDownloadPage::onExtractPathBrowseClicked);

  pathLayout->addWidget(m_extractPathLabel, 1);
  pathLayout->addWidget(m_extractPathBrowseBtn);

  // --- 进度 ---
  QGroupBox *progressGroup = new QGroupBox(tr("下载进度"), this);
  QVBoxLayout *progressLayout = new QVBoxLayout(progressGroup);

  m_progressBar = new QProgressBar(progressGroup);
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(true);

  m_progressDetailLabel = new QLabel(progressGroup);
  m_progressDetailLabel->setObjectName("hintLabel");

  progressLayout->addWidget(m_progressBar);
  progressLayout->addWidget(m_progressDetailLabel);

  // --- 状态 ---
  m_statusLabel = new QLabel(this);
  m_statusLabel->setObjectName("statusLabel");

  // --- 按钮区域 ---
  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(12);

  m_backBtn = new QPushButton(tr("返回"), this);
  connect(m_backBtn, &QPushButton::clicked, this, &JavaDownloadPage::backToSettingsRequested);

  m_cancelBtn = new QPushButton(tr("取消"), this);
  m_cancelBtn->setVisible(false);
  connect(m_cancelBtn, &QPushButton::clicked, this, &JavaDownloadPage::onCancelClicked);

  m_downloadBtn = new QPushButton(tr("下载并安装"), this);
  m_downloadBtn->setObjectName("primaryButton");
  connect(m_downloadBtn, &QPushButton::clicked, this, &JavaDownloadPage::onDownloadClicked);

  buttonLayout->addWidget(m_backBtn);
  buttonLayout->addStretch();
  buttonLayout->addWidget(m_cancelBtn);
  buttonLayout->addWidget(m_downloadBtn);

  // --- 组装 ---
  mainLayout->addWidget(m_titleLabel);
  mainLayout->addWidget(m_subtitleLabel);
  mainLayout->addWidget(distGroup);
  mainLayout->addWidget(mirrorGroup);
  mainLayout->addWidget(versionGroup);
  mainLayout->addWidget(pathGroup);
  mainLayout->addWidget(progressGroup);
  mainLayout->addWidget(m_statusLabel);
  mainLayout->addStretch();
  mainLayout->addLayout(buttonLayout);
}

// --- Slots ---

void JavaDownloadPage::onDistributionChanged(int index)
{
  QVector<JavaDistribution> dists = JavaDownloader::instance()->availableDistributions();
  if (index >= 0 && index < dists.size())
    m_distributionDescLabel->setText(dists[index].description);

  // 非 Adoptium 发行版不支持镜像源
  QString distId = m_distributionCombo->currentData().toString();
  m_mirrorCombo->setEnabled(distId == "adoptium");

  onVersionChanged(m_versionCombo->currentIndex());
}

void JavaDownloadPage::onMirrorChanged(int index)
{
  Q_UNUSED(index);
  onVersionChanged(m_versionCombo->currentIndex());
}

void JavaDownloadPage::onVersionChanged(int index)
{
  Q_UNUSED(index);
  int majorVersion = m_versionCombo->currentData().toInt();
  QString distId = m_distributionCombo->currentData().toString();
  QString mirrorId = m_mirrorCombo->currentData().toString();

  m_binaryDetailLabel->setText(tr("正在获取可用版本..."));
  m_binaryListWidget->clear();
  m_binaryListWidget->setVisible(false);
  m_currentBinaries.clear();

  JavaDownloader::instance()->fetchAvailableBinaries(distId, mirrorId, majorVersion);
}

void JavaDownloadPage::onDownloadClicked()
{
  if (m_currentBinaries.isEmpty())
  {
    NotificationManager::showError(this, tr("请先选择一个可用的 Java 版本"));
    return;
  }

  // 获取选中的二进制包
  int selectedRow = m_binaryListWidget->currentRow();
  if (selectedRow < 0 || selectedRow >= m_currentBinaries.size())
    selectedRow = 0;

  m_selectedBinary = m_currentBinaries[selectedRow];

  if (m_selectedBinary.downloadUrl.isEmpty())
  {
    NotificationManager::showError(this, tr("下载链接不可用"));
    return;
  }

  if (m_extractPath.isEmpty())
  {
    NotificationManager::showError(this, tr("请选择安装路径"));
    return;
  }

  m_isDownloading = true;
  updateUIState();

  JavaDownloader::instance()->downloadAndExtract(m_selectedBinary, m_extractPath);
}

void JavaDownloadPage::onExtractPathBrowseClicked()
{
  QString dir = AppFileDialog::getExistingDirectory(this, tr("选择 Java 安装目录"),
                                                    m_extractPath);
  if (!dir.isEmpty())
  {
    m_extractPath = dir;
    m_extractPathLabel->setText(dir);
  }
}

void JavaDownloadPage::onCancelClicked()
{
  JavaDownloader::instance()->cancel();
  m_isDownloading = false;
  m_progressBar->setValue(0);
  m_progressDetailLabel->clear();
  updateUIState();
}

void JavaDownloadPage::onStatusChanged(JavaDownloadStatus status)
{
  Q_UNUSED(status);
  updateUIState();
}

void JavaDownloadPage::onVersionListFetched(const QVector<JavaBinaryInfo> &binaries)
{
  m_currentBinaries = binaries;
  m_binaryListWidget->clear();
  m_binaryListWidget->setVisible(true);

  for (const JavaBinaryInfo &bin : binaries)
  {
    QString label = QString("%1 | %2 | %3")
                        .arg(bin.packageType.toUpper())
                        .arg(bin.architecture)
                        .arg(bin.osName);
    if (bin.fileSize > 0)
      label += QString(" | %1").arg(formatFileSize(bin.fileSize));
    m_binaryListWidget->addItem(label);
  }

  if (!binaries.isEmpty())
  {
    m_binaryListWidget->setCurrentRow(0);
    updateBinaryInfo();
  }
  else
  {
    m_binaryDetailLabel->setText(tr("未找到可用的二进制包"));
  }
}

void JavaDownloadPage::onFetchError(const QString &error)
{
  m_binaryDetailLabel->setText(tr("获取失败: %1").arg(error));
  m_binaryListWidget->setVisible(false);
}

void JavaDownloadPage::onDownloadProgress(const QString &fileName, int percent)
{
  Q_UNUSED(fileName);
  m_progressBar->setValue(percent);
  m_progressDetailLabel->setText(tr("正在下载... %1%").arg(percent));
}

void JavaDownloadPage::onDownloadBytesProgress(qint64 bytesReceived, qint64 bytesTotal)
{
  if (bytesTotal > 0)
  {
    m_progressDetailLabel->setText(tr("正在下载... %1 / %2")
                                       .arg(formatFileSize(bytesReceived))
                                       .arg(formatFileSize(bytesTotal)));
  }
}

void JavaDownloadPage::onDownloadCompleted(const QString &filePath)
{
  Q_UNUSED(filePath);
  m_progressBar->setValue(100);
  m_progressDetailLabel->setText(tr("下载完成，正在解压..."));
}

void JavaDownloadPage::onDownloadFailed(const QString &error)
{
  m_isDownloading = false;
  m_progressBar->setValue(0);
  m_progressDetailLabel->setText(tr("下载失败: %1").arg(error));
  NotificationManager::showError(this, tr("Java 下载失败: %1").arg(error));
  updateUIState();
}

void JavaDownloadPage::onExtractCompleted(const QString &javaPath)
{
  m_isDownloading = false;
  m_progressBar->setValue(100);
  m_progressDetailLabel->setText(tr("安装完成!"));

  QString javaExe = findJavaExecutable(javaPath);
  m_statusLabel->setText(tr("Java 已安装至: %1\n可执行文件: %2").arg(javaPath).arg(javaExe));

  NotificationManager::showSuccess(this, tr("Java 安装成功!"));

  emit javaInstalled(javaPath, m_selectedBinary.versionName);
  updateUIState();
}

void JavaDownloadPage::onExtractFailed(const QString &error)
{
  m_isDownloading = false;
  m_progressBar->setValue(0);
  m_progressDetailLabel->setText(tr("解压失败: %1").arg(error));
  NotificationManager::showError(this, tr("Java 解压失败: %1").arg(error));
  updateUIState();
}

// --- Helpers ---

void JavaDownloadPage::updateUIState()
{
  JavaDownloadStatus status = JavaDownloader::instance()->status();
  bool isBusy = (status == JavaDownloadStatus::FetchingBinaries ||
                 status == JavaDownloadStatus::Downloading ||
                 status == JavaDownloadStatus::Extracting);

  m_distributionCombo->setEnabled(!isBusy);
  m_versionCombo->setEnabled(!isBusy);
  m_extractPathBrowseBtn->setEnabled(!isBusy);
  m_downloadBtn->setEnabled(!isBusy && !m_currentBinaries.isEmpty());
  m_cancelBtn->setVisible(isBusy);
  m_backBtn->setEnabled(!isBusy);

  if (status == JavaDownloadStatus::FetchingBinaries)
    m_statusLabel->setText(tr("正在获取可用版本列表..."));
  else if (status == JavaDownloadStatus::Downloading)
    m_statusLabel->setText(tr("正在下载 Java..."));
  else if (status == JavaDownloadStatus::Extracting)
    m_statusLabel->setText(tr("正在解压安装..."));
  else if (status == JavaDownloadStatus::Failed)
    m_statusLabel->setText(tr("操作失败，请重试"));
  else
    m_statusLabel->clear();
}

void JavaDownloadPage::updateBinaryInfo()
{
  int row = m_binaryListWidget->currentRow();
  if (row < 0 || row >= m_currentBinaries.size())
    return;

  const JavaBinaryInfo &bin = m_currentBinaries[row];
  QString info = QString("发行版: %1\n版本: %2\n架构: %3 | 系统: %4")
                     .arg(bin.distributionName)
                     .arg(bin.versionName)
                     .arg(bin.architecture)
                     .arg(bin.osName);
  if (bin.fileSize > 0)
    info += QString("\n大小: %1").arg(formatFileSize(bin.fileSize));
  m_binaryDetailLabel->setText(info);
}

QString JavaDownloadPage::formatFileSize(qint64 bytes) const
{
  if (bytes < 1024)
    return QString("%1 B").arg(bytes);
  else if (bytes < 1024 * 1024)
    return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
  else if (bytes < 1024LL * 1024 * 1024)
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
  else
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString JavaDownloadPage::defaultJavaInstallPath() const
{
  QString base = SettingsManager::instance()->getDefaultInstancePath();
  if (base.isEmpty())
    base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return base + "/java";
}

QString JavaDownloadPage::findJavaExecutable(const QString &dir) const
{
#ifdef Q_OS_WIN
  QString javaExe = dir + "/bin/java.exe";
#else
  QString javaExe = dir + "/bin/java";
#endif
  if (QFile::exists(javaExe))
    return javaExe;

  // 递归查找一层
  QDir d(dir);
  QFileInfoList entries = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo &entry : entries)
  {
    QString subPath = findJavaExecutable(entry.absoluteFilePath());
    if (!subPath.isEmpty())
      return subPath;
  }

  return QString();
}