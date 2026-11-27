/**
 * @file   JavaDownloadPage.cpp
 * @brief  Java 下载页面实现
 * @author BlockBox Team
 * @date   2026-07-01
 */

#include "JavaDownloadPage.h"

#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include "components/AppMessageBox.h"
#include <QScrollArea>
#include <QStandardPaths>
#include <QStyle>

#include "components/NotificationManager.h"
#include "utils/RuntimeDependencyManager.h"
#include "utils/SettingsManager.h"

namespace {

/// 统一下载页卡片：步骤徽标 + 标题 + 内容区
QVBoxLayout *createCard(QFrame **cardOut, QWidget *parent,
                        const QString &step, const QString &title)
{
  auto *card = new QFrame(parent);
  card->setObjectName(QStringLiteral("javaDlCard"));
  card->setAttribute(Qt::WA_StyledBackground, true);

  auto *layout = new QVBoxLayout(card);
  layout->setContentsMargins(18, 14, 18, 16);
  layout->setSpacing(10);

  auto *head = new QHBoxLayout();
  head->setSpacing(8);

  auto *badge = new QLabel(step, card);
  badge->setObjectName(QStringLiteral("javaDlStep"));
  badge->setAlignment(Qt::AlignCenter);
  badge->setFixedSize(20, 20);

  auto *titleLabel = new QLabel(title, card);
  titleLabel->setObjectName(QStringLiteral("javaDlCardTitle"));

  head->addWidget(badge);
  head->addWidget(titleLabel);
  head->addStretch(1);
  layout->addLayout(head);

  *cardOut = card;
  return layout;
}

/// 卡片内分隔线
QFrame *createDivider(QWidget *parent)
{
  auto *divider = new QFrame(parent);
  divider->setObjectName(QStringLiteral("javaDlDivider"));
  divider->setAttribute(Qt::WA_StyledBackground, true);
  divider->setFixedHeight(1);
  return divider;
}

} // namespace

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
  connect(downloader, &JavaDownloader::extractProgress,
          this, &JavaDownloadPage::onExtractProgress);
  connect(downloader, &JavaDownloader::extractCompleted,
          this, &JavaDownloadPage::onExtractCompleted);
  connect(downloader, &JavaDownloader::extractFailed,
          this, &JavaDownloadPage::onExtractFailed);

#ifdef Q_OS_ANDROID
  // 安卓：下载动作改走运行依赖通道（RuntimeDependencyManager，见 onDownloadClicked），
  // 进度与完成映射到页面自身的进度条；装好后自动发现新 Java 并广播
  connect(RuntimeDependencyManager::instance(), &RuntimeDependencyManager::installProgress,
          this, [this](const QString &, int percent, const QString &stage) {
      m_progressBar->setValue(percent);
      m_progressDetailLabel->setText(stage);
  });
  connect(RuntimeDependencyManager::instance(), &RuntimeDependencyManager::installFinished,
          this, [this](const QString &, bool success, const QString &message) {
      if (!success)
          NotificationManager::showError(this, tr("运行环境安装失败：%1").arg(message));
  });
  connect(RuntimeDependencyManager::instance(), &RuntimeDependencyManager::allInstallsFinished,
          this, [this](bool allSucceeded) {
      m_isDownloading = false;
      m_progressBar->setValue(allSucceeded ? 100 : 0);
      m_progressDetailLabel->setText(allSucceeded ? tr("安装完成!") : QString());
      updateUIState();
      if (!allSucceeded)
          return;
      for (const RuntimeDependency &dep
           : RuntimeDependencyManager::instance()->dependencies()) {
          const QString dir = RuntimeDependencyManager::installDirFor(dep);
          const QString javaExe = findJavaExecutable(dir);
          if (javaExe.isEmpty())
              continue;
          m_pathHintLabel->setText(tr("Java 已安装至: %1\n可执行文件: %2").arg(dir, javaExe));
          m_pathHintLabel->setVisible(true);
          NotificationManager::showSuccess(this, tr("Java 安装成功!"));
          emit javaInstalled(javaExe, dep.name);
          return;
      }
  });
#endif

  // 默认安装路径
  m_extractPath = defaultJavaInstallPath();
  m_extractPathLabel->setText(m_extractPath);
  m_extractPathLabel->setToolTip(m_extractPath);

  // 默认镜像源：官方源（GitHub）在国内网络不稳定，默认走清华 TUNA 镜像
  int idxTuna = m_mirrorCombo->findData("tuna");
  if (idxTuna >= 0)
  {
    QSignalBlocker blocker(m_mirrorCombo);
    m_mirrorCombo->setCurrentIndex(idxTuna);
  }

  // 初始化版本列表（按发行版实际提供的版本过滤）
  repopulateVersionCombo("adoptium");

  updateUIState();
}

void JavaDownloadPage::initUI()
{
  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  // 窗口高度较小时可滚动，避免底部按钮被裁掉
  auto *scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("javaDlScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto *content = new QWidget(scroll);
  content->setObjectName(QStringLiteral("javaDlContent"));

  QVBoxLayout *mainLayout = new QVBoxLayout(content);
  mainLayout->setContentsMargins(32, 24, 32, 20);
  mainLayout->setSpacing(14);

  // ── 标题区域 ──
  auto *headerRow = new QHBoxLayout();
  headerRow->setSpacing(12);

  auto *titleBox = new QVBoxLayout();
  titleBox->setSpacing(4);

  m_titleLabel = new OutlinedLabel(tr("Java 下载管理"), content);
  m_titleLabel->setObjectName("sectionTitle");

  m_subtitleLabel = new QLabel(tr("选择 Java 发行版、版本和镜像源，下载并安装 Java 运行环境"), content);
  m_subtitleLabel->setObjectName("sectionSubtitle");
  m_subtitleLabel->setWordWrap(true);

  titleBox->addWidget(m_titleLabel);
  titleBox->addWidget(m_subtitleLabel);

  m_statusPill = new QLabel(content);
  m_statusPill->setObjectName(QStringLiteral("javaDlStatus"));
  m_statusPill->setProperty("state", QStringLiteral("idle"));
  m_statusPill->setAlignment(Qt::AlignCenter);
  m_statusPill->setVisible(false);

  headerRow->addLayout(titleBox, 1);
  headerRow->addWidget(m_statusPill, 0, Qt::AlignVCenter);
  mainLayout->addLayout(headerRow);

  // ── 第一行：发行版 + 版本 ──
  auto *topGrid = new QGridLayout();
  topGrid->setContentsMargins(0, 0, 0, 0);
  topGrid->setHorizontalSpacing(14);
  topGrid->setVerticalSpacing(14);
  topGrid->setColumnStretch(0, 1);
  topGrid->setColumnStretch(1, 1);

  // 发行版卡片（发行版 + 镜像源）
  QFrame *distCard = nullptr;
  QVBoxLayout *distLayout = createCard(&distCard, content, QStringLiteral("1"), tr("发行版"));

  m_distributionCombo = new QComboBox(distCard);
  m_distributionCombo->setObjectName(QStringLiteral("javaDlCombo"));
  QVector<JavaDistribution> dists = JavaDownloader::instance()->availableDistributions();
  for (const JavaDistribution &d : dists)
    m_distributionCombo->addItem(d.name, d.id);

  m_distributionDescLabel = new QLabel(distCard);
  m_distributionDescLabel->setObjectName(QStringLiteral("javaDlDesc"));
  m_distributionDescLabel->setWordWrap(true);
  m_distributionDescLabel->setText(dists.first().description);

  connect(m_distributionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &JavaDownloadPage::onDistributionChanged);

  auto *mirrorLabel = new QLabel(tr("镜像源"), distCard);
  mirrorLabel->setObjectName(QStringLiteral("javaDlFieldLabel"));

  m_mirrorCombo = new QComboBox(distCard);
  m_mirrorCombo->setObjectName(QStringLiteral("javaDlCombo"));
  QVector<JavaMirror> mirrors = JavaDownloader::instance()->availableMirrors();
  for (const JavaMirror &m : mirrors)
    m_mirrorCombo->addItem(m.name, m.id);

  connect(m_mirrorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &JavaDownloadPage::onMirrorChanged);

  distLayout->addWidget(m_distributionCombo);
  distLayout->addWidget(m_distributionDescLabel);
  distLayout->addWidget(createDivider(distCard));
  distLayout->addWidget(mirrorLabel);
  distLayout->addWidget(m_mirrorCombo);
  distLayout->addStretch(1);

  // 版本卡片（版本 + 安装包列表）
  QFrame *versionCard = nullptr;
  QVBoxLayout *verLayout = createCard(&versionCard, content, QStringLiteral("2"), tr("Java 版本"));

  m_versionCombo = new QComboBox(versionCard);
  m_versionCombo->setObjectName(QStringLiteral("javaDlCombo"));
  connect(m_versionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &JavaDownloadPage::onVersionChanged);

  m_binaryListWidget = new QListWidget(versionCard);
  m_binaryListWidget->setObjectName(QStringLiteral("javaDlBinaryList"));
  m_binaryListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_binaryListWidget->setVisible(false);
  connect(m_binaryListWidget, &QListWidget::currentRowChanged,
          this, [this](int) { updateBinaryInfo(); });

  m_binaryDetailLabel = new QLabel(versionCard);
  m_binaryDetailLabel->setObjectName(QStringLiteral("javaDlSummary"));
  m_binaryDetailLabel->setWordWrap(true);

  verLayout->addWidget(m_versionCombo);
  verLayout->addWidget(m_binaryListWidget);
  verLayout->addWidget(m_binaryDetailLabel);
  verLayout->addStretch(1);

  topGrid->addWidget(distCard, 0, 0);
  topGrid->addWidget(versionCard, 0, 1);
  mainLayout->addLayout(topGrid);

  // ── 安装路径卡片 ──
  QFrame *pathCard = nullptr;
  QVBoxLayout *pathLayout = createCard(&pathCard, content, QStringLiteral("3"), tr("安装路径"));

  auto *pathRow = new QHBoxLayout();
  pathRow->setSpacing(10);

  m_extractPathLabel = new QLabel(pathCard);
  m_extractPathLabel->setObjectName(QStringLiteral("javaDlPath"));
  m_extractPathLabel->setWordWrap(true);
  m_extractPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

  m_extractPathBrowseBtn = new QPushButton(tr("浏览..."), pathCard);
  m_extractPathBrowseBtn->setObjectName(QStringLiteral("javaDlBrowseBtn"));
  m_extractPathBrowseBtn->setCursor(Qt::PointingHandCursor);
  connect(m_extractPathBrowseBtn, &QPushButton::clicked,
          this, &JavaDownloadPage::onExtractPathBrowseClicked);

  m_pathHintLabel = new QLabel(pathCard);
  m_pathHintLabel->setObjectName(QStringLiteral("javaDlHint"));
  m_pathHintLabel->setWordWrap(true);
  m_pathHintLabel->setVisible(false);

  pathRow->addWidget(m_extractPathLabel, 1);
  pathRow->addWidget(m_extractPathBrowseBtn, 0, Qt::AlignVCenter);
  pathLayout->addLayout(pathRow);
  pathLayout->addWidget(m_pathHintLabel);

  mainLayout->addWidget(pathCard);

  // ── 下载进度卡片（开始下载后才出现，空闲时不占版面）──
  QFrame *progressCard = nullptr;
  QVBoxLayout *progressLayout = createCard(&progressCard, content, QStringLiteral("4"), tr("下载进度"));
  m_progressCard = progressCard;
  m_progressCard->setVisible(false);

  m_progressBar = new QProgressBar(progressCard);
  m_progressBar->setObjectName(QStringLiteral("javaDlProgress"));
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(false);

  m_progressDetailLabel = new QLabel(progressCard);
  m_progressDetailLabel->setObjectName(QStringLiteral("javaDlProgressText"));

  progressLayout->addWidget(m_progressBar);
  progressLayout->addWidget(m_progressDetailLabel);

  mainLayout->addWidget(progressCard);
  mainLayout->addStretch(1);

  // ── 底部按钮 ──
  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(10);

  m_backBtn = new QPushButton(QStringLiteral("‹  ") + tr("返回"), content);
  m_backBtn->setObjectName(QStringLiteral("javaDlBackBtn"));
  m_backBtn->setCursor(Qt::PointingHandCursor);
  connect(m_backBtn, &QPushButton::clicked, this, &JavaDownloadPage::backToSettingsRequested);

  m_cancelBtn = new QPushButton(tr("取消"), content);
  m_cancelBtn->setObjectName(QStringLiteral("javaDlCancelBtn"));
  m_cancelBtn->setCursor(Qt::PointingHandCursor);
  m_cancelBtn->setVisible(false);
  connect(m_cancelBtn, &QPushButton::clicked, this, &JavaDownloadPage::onCancelClicked);

  m_downloadBtn = new QPushButton(tr("下载并安装"), content);
  m_downloadBtn->setObjectName(QStringLiteral("javaDlPrimaryBtn"));
  m_downloadBtn->setCursor(Qt::PointingHandCursor);
  connect(m_downloadBtn, &QPushButton::clicked, this, &JavaDownloadPage::onDownloadClicked);

  buttonLayout->addWidget(m_backBtn);
  buttonLayout->addStretch(1);
  buttonLayout->addWidget(m_cancelBtn);
  buttonLayout->addWidget(m_downloadBtn);

  mainLayout->addLayout(buttonLayout);

  scroll->setWidget(content);
  rootLayout->addWidget(scroll);
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

  // 不同发行版提供的版本不同（如 Microsoft 没有 Java 8/22/23/24），重建版本下拉框
  repopulateVersionCombo(distId);
}

void JavaDownloadPage::onMirrorChanged(int index)
{
  Q_UNUSED(index);
  onVersionChanged(m_versionCombo->currentIndex());
}

void JavaDownloadPage::repopulateVersionCombo(const QString &distId)
{
  {
    QSignalBlocker blocker(m_versionCombo);
    m_versionCombo->clear();

    QVector<JavaVersionInfo> all = JavaDownloader::instance()->supportedVersions();
    QVector<int> majors = JavaDownloader::instance()->supportedMajorVersions(distId);
    for (const JavaVersionInfo &v : all)
    {
      if (!majors.contains(v.majorVersion))
        continue;
      QString label = v.versionName;
      if (v.isLts) label += " (LTS)";
      m_versionCombo->addItem(label, v.majorVersion);
    }

    // 默认选中 Java 21
    int idx21 = m_versionCombo->findData(21);
    if (idx21 >= 0)
      m_versionCombo->setCurrentIndex(idx21);
    else if (m_versionCombo->count() > 0)
      m_versionCombo->setCurrentIndex(0);
  }

  onVersionChanged(m_versionCombo->currentIndex());
}

void JavaDownloadPage::onVersionChanged(int index)
{
  Q_UNUSED(index);
  int majorVersion = m_versionCombo->currentData().toInt();
  QString distId = m_distributionCombo->currentData().toString();
  QString mirrorId = m_mirrorCombo->currentData().toString();

  setSummaryText(tr("正在获取可用版本..."), QStringLiteral("normal"));
  m_binaryListWidget->clear();
  m_binaryListWidget->setVisible(false);
  m_currentBinaries.clear();
  m_progressCard->setVisible(false);
  m_progressBar->setValue(0);
  m_progressDetailLabel->clear();

  JavaDownloader::instance()->fetchAvailableBinaries(distId, mirrorId, majorVersion);
}

void JavaDownloadPage::onDownloadClicked()
{
#ifdef Q_OS_ANDROID
  // 安卓：JavaDownloader 按 os=android 查各家厂商 API 恒为空，兜底还会下到
  // bionic 跑不了的桌面 Linux JDK。下载动作改走运行依赖通道：按设备芯片
  // 架构下载适配的 Java 运行环境，进度复用页面进度条展示
  if (m_isDownloading || RuntimeDependencyManager::instance()->isInstalling()
      || RuntimeDependencyManager::instance()->isChecking())
    return;
  m_isDownloading = true;
  m_progressCard->setVisible(true);
  m_progressBar->setValue(0);
  m_progressDetailLabel->setText(tr("正在获取适配安卓的 Java 运行环境…"));
  updateUIState();

  RuntimeDependencyManager *deps = RuntimeDependencyManager::instance();
  auto *checkConnection = new QMetaObject::Connection;
  *checkConnection = connect(deps, &RuntimeDependencyManager::checkFinished, deps,
                             [this, deps, checkConnection](bool ok, const QString &message) {
      disconnect(*checkConnection);
      delete checkConnection;
      if (!ok) {
        m_isDownloading = false;
        m_progressCard->setVisible(false);
        m_progressDetailLabel->clear();
        updateUIState();
        NotificationManager::showErrorWithNetworkCheck(this, tr("获取运行环境清单失败：\n%1").arg(message));
        return;
      }
      if (deps->pendingCount() <= 0) {
        m_isDownloading = false;
        m_progressCard->setVisible(false);
        m_progressDetailLabel->clear();
        updateUIState();
        const QList<RuntimeDependency> blocked = deps->unavailableDependencies();
        if (!blocked.isEmpty()) {
          // 清单里写了 available:false：官方包还没发布，别再提示「已是最新」糊弄过去
          QStringList names;
          for (const RuntimeDependency &dependency : blocked)
            names << dependency.name;
          NotificationManager::showInfo(
              this, tr("官方还没发布适配手机的 %1，暂时装不了；发布后这里会直接提示安装。")
                        .arg(names.join(tr("、"))));
        } else {
          NotificationManager::showInfo(this, tr("运行环境已是最新，无需再次安装。"));
        }
        return;
      }
      m_progressDetailLabel->setText(tr("开始下载安装 Java 运行环境…"));
      deps->installAll();
  });
  deps->checkForUpdates(this, true);
  return;
#endif
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
    NotificationManager::showErrorWithNetworkCheck(this, tr("下载链接不可用"));
    return;
  }

  if (m_extractPath.isEmpty())
  {
    NotificationManager::showError(this, tr("请选择安装路径"));
    return;
  }

  m_isDownloading = true;
  m_progressCard->setVisible(true);
  m_progressBar->setValue(0);
  m_progressDetailLabel->clear();
  updateUIState();

  JavaDownloader::instance()->downloadAndExtract(m_selectedBinary, m_extractPath);
}

void JavaDownloadPage::onExtractPathBrowseClicked()
{
  QString dir = QFileDialog::getExistingDirectory(this, tr("选择 Java 安装目录"),
                                                    m_extractPath);
  if (!dir.isEmpty())
  {
    m_extractPath = dir;
    m_extractPathLabel->setText(dir);
    m_extractPathLabel->setToolTip(dir);
  }
}

void JavaDownloadPage::onCancelClicked()
{
  JavaDownloader::instance()->cancel();
  m_isDownloading = false;
  m_progressBar->setValue(0);
  m_progressDetailLabel->clear();
  m_progressCard->setVisible(false);
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
    QString label = QString("%1 · %2 · %3")
                        .arg(bin.packageType.toUpper())
                        .arg(bin.architecture)
                        .arg(bin.osName);
    if (bin.fileSize > 0)
      label += QString(" · %1").arg(formatFileSize(bin.fileSize));
    m_binaryListWidget->addItem(label);
  }

  if (!binaries.isEmpty())
  {
    // 列表高度跟随条目数（最多 3 条，其余滚动），避免只显示半行
    const int rows = qMin(binaries.size(), 3);
    const int rowHeight = qMax(30, m_binaryListWidget->sizeHintForRow(0));
    m_binaryListWidget->setFixedHeight(rows * rowHeight + 16);
    m_binaryListWidget->setCurrentRow(0);
    updateBinaryInfo();
  }
  else
  {
    m_binaryListWidget->setVisible(false);
    setSummaryText(tr("未找到可用的二进制包"), QStringLiteral("normal"));
  }

  updateUIState();
}

void JavaDownloadPage::onFetchError(const QString &error)
{
#ifdef Q_OS_ANDROID
  // 安卓上各家 Java 厂商 API 没有 android 平台条目，版本列表必然拉不到；
  // 直接给出「一键安装运行环境」的正确路径，而不是让用户反复重试
  Q_UNUSED(error);
  setSummaryText(tr("安卓版请使用「一键安装运行环境」获取适配手机的 Java（会自动按芯片架构下载）。"),
                 QStringLiteral("error"));
  m_binaryListWidget->setVisible(false);
  updateUIState();
  return;
#endif
  setSummaryText(tr("获取失败: %1").arg(error), QStringLiteral("error"));
  m_binaryListWidget->setVisible(false);
  updateUIState();
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
    m_progressBar->setValue(static_cast<int>(bytesReceived * 100 / bytesTotal));
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
  NotificationManager::showErrorWithNetworkCheck(this, tr("Java 下载失败: %1").arg(error));
  updateUIState();
}

void JavaDownloadPage::onExtractProgress(int percent)
{
  m_progressBar->setValue(percent);
  m_progressDetailLabel->setText(tr("正在解压安装..."));
}

void JavaDownloadPage::onExtractCompleted(const QString &javaPath)
{
  m_isDownloading = false;
  m_progressBar->setValue(100);
  m_progressDetailLabel->setText(tr("安装完成!"));

  QString javaExe = findJavaExecutable(javaPath);
  m_pathHintLabel->setText(tr("Java 已安装至: %1\n可执行文件: %2").arg(javaPath).arg(javaExe));
  m_pathHintLabel->setVisible(true);

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
  bool isBusy = (status == JavaDownloadStatus::FetchingVersions ||
                 status == JavaDownloadStatus::FetchingBinaries ||
                 status == JavaDownloadStatus::Downloading ||
                 status == JavaDownloadStatus::Extracting);

  m_distributionCombo->setEnabled(!isBusy);
  m_versionCombo->setEnabled(!isBusy);
  m_mirrorCombo->setEnabled(!isBusy &&
                            m_distributionCombo->currentData().toString() == "adoptium");
  m_extractPathBrowseBtn->setEnabled(!isBusy);
  m_downloadBtn->setEnabled(!isBusy && !m_currentBinaries.isEmpty());
  m_cancelBtn->setVisible(isBusy);
  m_backBtn->setEnabled(!isBusy);

  if (status == JavaDownloadStatus::FetchingVersions ||
      status == JavaDownloadStatus::FetchingBinaries)
    setStatusPill(QStringLiteral("busy"), tr("正在获取可用版本列表..."));
  else if (status == JavaDownloadStatus::Downloading)
  {
    m_progressCard->setVisible(true);
    setStatusPill(QStringLiteral("busy"), tr("正在下载 Java..."));
  }
  else if (status == JavaDownloadStatus::Extracting)
  {
    m_progressCard->setVisible(true);
    setStatusPill(QStringLiteral("busy"), tr("正在解压安装..."));
  }
  else if (status == JavaDownloadStatus::Completed)
    setStatusPill(QStringLiteral("success"), tr("安装完成!"));
  else if (status == JavaDownloadStatus::Failed)
    setStatusPill(QStringLiteral("error"), tr("操作失败，请重试"));
  else
    setStatusPill(QStringLiteral("idle"), QString());
}

void JavaDownloadPage::updateBinaryInfo()
{
  int row = m_binaryListWidget->currentRow();
  if (row < 0 || row >= m_currentBinaries.size())
  {
    m_binaryDetailLabel->clear();
    return;
  }

  const JavaBinaryInfo &bin = m_currentBinaries[row];
  setSummaryText(QString("%1 · %2").arg(bin.distributionName).arg(bin.versionName),
                 QStringLiteral("normal"));
}

void JavaDownloadPage::setSummaryText(const QString &text, const QString &state)
{
  if (!m_binaryDetailLabel)
    return;
  if (m_binaryDetailLabel->property("state").toString() != state)
  {
    m_binaryDetailLabel->setProperty("state", state);
    m_binaryDetailLabel->style()->unpolish(m_binaryDetailLabel);
    m_binaryDetailLabel->style()->polish(m_binaryDetailLabel);
  }
  m_binaryDetailLabel->setText(text);
}

void JavaDownloadPage::setStatusPill(const QString &state, const QString &text)
{
  if (!m_statusPill)
    return;

  m_statusPill->setText(text);
  m_statusPill->setVisible(!text.isEmpty());

  if (m_statusPill->property("state").toString() != state)
  {
    m_statusPill->setProperty("state", state);
    m_statusPill->style()->unpolish(m_statusPill);
    m_statusPill->style()->polish(m_statusPill);
  }
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
