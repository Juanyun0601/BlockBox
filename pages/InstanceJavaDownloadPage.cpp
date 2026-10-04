/**
 * @file   InstanceJavaDownloadPage.cpp
 * @brief  实例助手 - Java 下载页面实现
 * @author BlockBox Team
 * @date   2026-07-21
 */
#include "InstanceJavaDownloadPage.h"

#include <QComboBox>
#include <QDebug>
#include "components/AppFileDialog.h"
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include "components/AppMessageBox.h"
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "components/NotificationManager.h"
#include "utils/GameLauncher.h"
#include "utils/SettingsManager.h"

InstanceJavaDownloadPage::InstanceJavaDownloadPage(QWidget *parent)
    : QWidget(parent)
    , m_backBtn(nullptr)
    , m_titleLabel(nullptr)
    , m_subtitleLabel(nullptr)
    , m_distributionCombo(nullptr)
    , m_distributionDescLabel(nullptr)
    , m_mirrorCombo(nullptr)
    , m_versionCombo(nullptr)
    , m_binaryListWidget(nullptr)
    , m_binaryDetailLabel(nullptr)
    , m_pathLabel(nullptr)
    , m_browseBtn(nullptr)
    , m_progressBar(nullptr)
    , m_progressDetailLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_downloadBtn(nullptr)
    , m_cancelBtn(nullptr)
{
    initUI();

    JavaDownloader *downloader = JavaDownloader::instance();

    connect(downloader, &JavaDownloader::statusChanged,
            this, &InstanceJavaDownloadPage::onStatusChanged);
    connect(downloader, &JavaDownloader::versionListFetched,
            this, &InstanceJavaDownloadPage::onVersionListFetched);
    connect(downloader, &JavaDownloader::fetchError,
            this, &InstanceJavaDownloadPage::onFetchError);
    connect(downloader, &JavaDownloader::downloadProgress,
            this, &InstanceJavaDownloadPage::onDownloadProgress);
    connect(downloader, &JavaDownloader::downloadBytesProgress,
            this, &InstanceJavaDownloadPage::onDownloadBytesProgress);
    connect(downloader, &JavaDownloader::downloadCompleted,
            this, &InstanceJavaDownloadPage::onDownloadCompleted);
    connect(downloader, &JavaDownloader::downloadFailed,
            this, &InstanceJavaDownloadPage::onDownloadFailed);
    connect(downloader, &JavaDownloader::extractCompleted,
            this, &InstanceJavaDownloadPage::onExtractCompleted);
    connect(downloader, &JavaDownloader::extractFailed,
            this, &InstanceJavaDownloadPage::onExtractFailed);

    // 默认安装路径
    m_extractPath = defaultInstallPath();
    m_pathLabel->setText(m_extractPath);

    // 初始化版本列表
    QVector<JavaVersionInfo> versions = downloader->supportedVersions();
    for (const JavaVersionInfo &v : versions)
    {
        QString label = v.versionName;
        if (v.isLts)
            label += QStringLiteral(" (LTS)");
        m_versionCombo->addItem(label, v.majorVersion);
    }

    // 默认选中 Java 21
    int idx21 = m_versionCombo->findData(21);
    if (idx21 >= 0)
        m_versionCombo->setCurrentIndex(idx21);
    else if (m_versionCombo->count() > 0)
        m_versionCombo->setCurrentIndex(0);

    updateUIState();
}

void InstanceJavaDownloadPage::initUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 16);
    layout->setSpacing(8);

    // ── 顶部：返回按钮 + 标题 ──
    auto *topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    m_backBtn = new QPushButton(QStringLiteral("‹"), this);
    m_backBtn->setObjectName("javaDownloadBackBtn");
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setFixedSize(28, 28);
    m_backBtn->setToolTip(tr("返回 Java 管理"));
    connect(m_backBtn, &QPushButton::clicked, this, &InstanceJavaDownloadPage::onBackClicked);

    m_titleLabel = new OutlinedLabel(tr("下载 Java"), this);
    m_titleLabel->setObjectName("sectionTitle");

    topBar->addWidget(m_backBtn);
    topBar->addWidget(m_titleLabel, 1);
    topBar->addStretch();

    layout->addLayout(topBar);

    m_subtitleLabel = new QLabel(
        tr("选择发行版、版本与镜像源，下载并自动解压 Java 运行环境。"),
        this);
    m_subtitleLabel->setObjectName("sectionSubtitle");
    m_subtitleLabel->setWordWrap(true);
    layout->addWidget(m_subtitleLabel);

    // ── 发行版 ──
    auto *distFrame = new QFrame(this);
    distFrame->setObjectName("javaDownloadSection");
    auto *distLayout = new QVBoxLayout(distFrame);
    distLayout->setContentsMargins(0, 0, 0, 0);
    distLayout->setSpacing(4);

    auto *distTitle = new QLabel(tr("发行版"), distFrame);
    distTitle->setObjectName("javaSectionLabel");

    m_distributionCombo = new QComboBox(distFrame);
    QVector<JavaDistribution> dists = JavaDownloader::instance()->availableDistributions();
    for (const JavaDistribution &d : dists)
        m_distributionCombo->addItem(d.name, d.id);

    m_distributionDescLabel = new QLabel(distFrame);
    m_distributionDescLabel->setObjectName("hintLabel");
    m_distributionDescLabel->setWordWrap(true);
    m_distributionDescLabel->setText(dists.first().description);

    connect(m_distributionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceJavaDownloadPage::onDistributionChanged);

    distLayout->addWidget(distTitle);
    distLayout->addWidget(m_distributionCombo);
    distLayout->addWidget(m_distributionDescLabel);

    layout->addWidget(distFrame);

    // ── 镜像源 ──
    auto *mirrorFrame = new QFrame(this);
    mirrorFrame->setObjectName("javaDownloadSection");
    auto *mirrorLayout = new QVBoxLayout(mirrorFrame);
    mirrorLayout->setContentsMargins(0, 0, 0, 0);
    mirrorLayout->setSpacing(4);

    auto *mirrorTitle = new QLabel(tr("镜像源"), mirrorFrame);
    mirrorTitle->setObjectName("javaSectionLabel");

    m_mirrorCombo = new QComboBox(mirrorFrame);
    QVector<JavaMirror> mirrors = JavaDownloader::instance()->availableMirrors();
    for (const JavaMirror &m : mirrors)
        m_mirrorCombo->addItem(m.name, m.id);

    connect(m_mirrorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceJavaDownloadPage::onMirrorChanged);

    mirrorLayout->addWidget(mirrorTitle);
    mirrorLayout->addWidget(m_mirrorCombo);

    layout->addWidget(mirrorFrame);

    // ── 版本 ──
    auto *versionFrame = new QFrame(this);
    versionFrame->setObjectName("javaDownloadSection");
    auto *versionLayout = new QVBoxLayout(versionFrame);
    versionLayout->setContentsMargins(0, 0, 0, 0);
    versionLayout->setSpacing(4);

    auto *versionTitle = new QLabel(tr("Java 版本"), versionFrame);
    versionTitle->setObjectName("javaSectionLabel");

    m_versionCombo = new QComboBox(versionFrame);
    connect(m_versionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InstanceJavaDownloadPage::onVersionChanged);

    m_binaryListWidget = new QListWidget(versionFrame);
    m_binaryListWidget->setObjectName("javaBinaryList");
    m_binaryListWidget->setMaximumHeight(96);
    m_binaryListWidget->setVisible(false);
    connect(m_binaryListWidget, &QListWidget::currentRowChanged,
            this, &InstanceJavaDownloadPage::onBinarySelectionChanged);

    m_binaryDetailLabel = new QLabel(versionFrame);
    m_binaryDetailLabel->setObjectName("hintLabel");
    m_binaryDetailLabel->setWordWrap(true);

    versionLayout->addWidget(versionTitle);
    versionLayout->addWidget(m_versionCombo);
    versionLayout->addWidget(m_binaryListWidget);
    versionLayout->addWidget(m_binaryDetailLabel);

    layout->addWidget(versionFrame);

    // ── 安装路径 ──
    auto *pathFrame = new QFrame(this);
    pathFrame->setObjectName("javaDownloadSection");
    auto *pathLayout = new QHBoxLayout(pathFrame);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(6);

    m_pathLabel = new QLabel(pathFrame);
    m_pathLabel->setObjectName("hintLabel");
    m_pathLabel->setWordWrap(true);
    m_pathLabel->setTextFormat(Qt::PlainText);

    m_browseBtn = new QPushButton(tr("浏览..."), pathFrame);
    m_browseBtn->setObjectName("javaBrowseBtn");
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_browseBtn, &QPushButton::clicked, this, &InstanceJavaDownloadPage::onBrowseClicked);

    pathLayout->addWidget(m_pathLabel, 1);
    pathLayout->addWidget(m_browseBtn);

    layout->addWidget(pathFrame);

    // ── 进度 ──
    auto *progressFrame = new QFrame(this);
    progressFrame->setObjectName("javaDownloadSection");
    auto *progressLayout = new QVBoxLayout(progressFrame);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(4);

    auto *progressTitle = new QLabel(tr("下载进度"), progressFrame);
    progressTitle->setObjectName("javaSectionLabel");

    m_progressBar = new QProgressBar(progressFrame);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);

    m_progressDetailLabel = new QLabel(progressFrame);
    m_progressDetailLabel->setObjectName("hintLabel");

    progressLayout->addWidget(progressTitle);
    progressLayout->addWidget(m_progressBar);
    progressLayout->addWidget(m_progressDetailLabel);

    layout->addWidget(progressFrame);

    // ── 状态标签 ──
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("hintLabel");
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    layout->addStretch(1);

    // ── 底部按钮 ──
    auto *buttonBar = new QHBoxLayout();
    buttonBar->setSpacing(8);

    m_cancelBtn = new QPushButton(tr("取消"), this);
    m_cancelBtn->setObjectName("javaCancelBtn");
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &InstanceJavaDownloadPage::onCancelClicked);

    m_downloadBtn = new QPushButton(tr("下载并安装"), this);
    m_downloadBtn->setObjectName("downloadJavaBtn");
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    connect(m_downloadBtn, &QPushButton::clicked, this, &InstanceJavaDownloadPage::onDownloadClicked);

    buttonBar->addStretch();
    buttonBar->addWidget(m_cancelBtn);
    buttonBar->addWidget(m_downloadBtn);

    layout->addLayout(buttonBar);
}

void InstanceJavaDownloadPage::onDistributionChanged(int index)
{
    QVector<JavaDistribution> dists = JavaDownloader::instance()->availableDistributions();
    if (index >= 0 && index < dists.size())
        m_distributionDescLabel->setText(dists[index].description);

    // 非 Adoptium 发行版不支持镜像源
    QString distId = m_distributionCombo->currentData().toString();
    m_mirrorCombo->setEnabled(distId == "adoptium");

    onVersionChanged(m_versionCombo->currentIndex());
}

void InstanceJavaDownloadPage::onMirrorChanged(int index)
{
    Q_UNUSED(index);
    onVersionChanged(m_versionCombo->currentIndex());
}

void InstanceJavaDownloadPage::onVersionChanged(int index)
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

void InstanceJavaDownloadPage::onBinarySelectionChanged(int row)
{
    if (row < 0 || row >= m_currentBinaries.size())
        return;
    updateBinaryDetail();
}

void InstanceJavaDownloadPage::onDownloadClicked()
{
    if (m_currentBinaries.isEmpty())
    {
        NotificationManager::showError(this, tr("请先选择一个可用的 Java 版本"));
        return;
    }

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

    updateUIState();
    JavaDownloader::instance()->downloadAndExtract(m_selectedBinary, m_extractPath);
}

void InstanceJavaDownloadPage::onBrowseClicked()
{
    QString dir = AppFileDialog::getExistingDirectory(this, tr("选择 Java 安装目录"),
                                                     m_extractPath);
    if (!dir.isEmpty())
    {
        m_extractPath = dir;
        m_pathLabel->setText(dir);
    }
}

void InstanceJavaDownloadPage::onCancelClicked()
{
    JavaDownloader::instance()->cancel();
    m_progressBar->setValue(0);
    m_progressDetailLabel->clear();
    updateUIState();
}

void InstanceJavaDownloadPage::onBackClicked()
{
    // 若正在下载则不允许直接返回
    JavaDownloadStatus status = JavaDownloader::instance()->status();
    if (status == JavaDownloadStatus::Downloading ||
        status == JavaDownloadStatus::Extracting ||
        status == JavaDownloadStatus::FetchingBinaries)
    {
        auto reply = AppMessageBox::question(this, tr("确认返回"),
            tr("Java 下载正在进行中，确定要返回吗？"),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
        if (reply != AppMessageBox::Yes)
            return;
        JavaDownloader::instance()->cancel();
    }

    emit backRequested();
}

void InstanceJavaDownloadPage::onStatusChanged(JavaDownloadStatus status)
{
    Q_UNUSED(status);
    updateUIState();
}

void InstanceJavaDownloadPage::onVersionListFetched(const QVector<JavaBinaryInfo> &binaries)
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
        updateBinaryDetail();
    }
    else
    {
        m_binaryDetailLabel->setText(tr("未找到可用的二进制包"));
    }
}

void InstanceJavaDownloadPage::onFetchError(const QString &error)
{
    m_binaryDetailLabel->setText(tr("获取失败: %1").arg(error));
    m_binaryListWidget->setVisible(false);
}

void InstanceJavaDownloadPage::onDownloadProgress(const QString &fileName, int percent)
{
    Q_UNUSED(fileName);
    m_progressBar->setValue(percent);
    m_progressDetailLabel->setText(tr("正在下载... %1%").arg(percent));
}

void InstanceJavaDownloadPage::onDownloadBytesProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal > 0)
    {
        m_progressDetailLabel->setText(tr("正在下载... %1 / %2")
                                           .arg(formatFileSize(bytesReceived))
                                           .arg(formatFileSize(bytesTotal)));
    }
}

void InstanceJavaDownloadPage::onDownloadCompleted(const QString &filePath)
{
    Q_UNUSED(filePath);
    m_progressBar->setValue(100);
    m_progressDetailLabel->setText(tr("下载完成，正在解压..."));
}

void InstanceJavaDownloadPage::onDownloadFailed(const QString &error)
{
    m_progressBar->setValue(0);
    m_progressDetailLabel->setText(tr("下载失败: %1").arg(error));
    NotificationManager::showError(this, tr("Java 下载失败: %1").arg(error));
    updateUIState();
}

void InstanceJavaDownloadPage::onExtractCompleted(const QString &javaPath)
{
    m_progressBar->setValue(100);
    m_progressDetailLabel->setText(tr("安装完成!"));

    QString javaExe = findJavaExecutable(javaPath);
    m_statusLabel->setText(tr("Java 已安装至: %1\n可执行文件: %2").arg(javaPath).arg(javaExe));

    NotificationManager::showSuccess(this, tr("Java 安装成功!"));

    // 自动将安装好的 Java 加入缓存列表（去重）
    if (!javaExe.isEmpty())
    {
        GameLauncher::JavaInfo info;
        if (GameLauncher::instance()->validateJavaPath(javaExe, info))
        {
            QList<QPair<QString, QString>> installations =
                SettingsManager::instance()->getJavaInstallations();
            bool exists = false;
            for (const auto &inst : installations)
            {
                if (inst.first == javaExe)
                {
                    exists = true;
                    break;
                }
            }
            if (!exists)
            {
                installations.append(qMakePair(javaExe, info.version));
                SettingsManager::instance()->setJavaInstallations(installations);
            }
        }
    }

    emit javaInstalled(javaPath, m_selectedBinary.versionName);
    updateUIState();
}

void InstanceJavaDownloadPage::onExtractFailed(const QString &error)
{
    m_progressBar->setValue(0);
    m_progressDetailLabel->setText(tr("解压失败: %1").arg(error));
    NotificationManager::showError(this, tr("Java 解压失败: %1").arg(error));
    updateUIState();
}

void InstanceJavaDownloadPage::updateUIState()
{
    JavaDownloadStatus status = JavaDownloader::instance()->status();
    bool isBusy = (status == JavaDownloadStatus::FetchingBinaries ||
                   status == JavaDownloadStatus::Downloading ||
                   status == JavaDownloadStatus::Extracting);

    m_distributionCombo->setEnabled(!isBusy);
    m_versionCombo->setEnabled(!isBusy);
    m_browseBtn->setEnabled(!isBusy);
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
    else if (status == JavaDownloadStatus::Completed)
        ; // 状态已由 onExtractCompleted 设置
    else
        m_statusLabel->clear();
}

void InstanceJavaDownloadPage::updateBinaryDetail()
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

QString InstanceJavaDownloadPage::formatFileSize(qint64 bytes) const
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

QString InstanceJavaDownloadPage::defaultInstallPath() const
{
    QString base = SettingsManager::instance()->getDefaultInstancePath();
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/java");
}

QString InstanceJavaDownloadPage::findJavaExecutable(const QString &dir) const
{
#ifdef Q_OS_WIN
    QString javaExe = dir + QStringLiteral("/bin/java.exe");
#else
    QString javaExe = dir + QStringLiteral("/bin/java");
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
