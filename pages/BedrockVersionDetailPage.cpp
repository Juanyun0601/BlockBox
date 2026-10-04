/**
 * @file   BedrockVersionDetailPage.cpp
 * @brief  基岩版版本详情页实现
 * @author BlockBox Team
 */

#include "BedrockVersionDetailPage.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include "components/AppFileDialog.h"
#include "components/AppMessageBox.h"
#include "components/NotificationManager.h"
#include "components/OutlinedLabel.h"
#include "utils/DownloadTaskManager.h"
#include "utils/MultiThreadDownloader.h"
#include "utils/SettingsManager.h"

BedrockVersionDetailPage::BedrockVersionDetailPage(QWidget *parent)
    : QWidget(parent)
    , m_downloader(nullptr)
    , m_pendingOpenAfterFetch(false)
{
    initUI();

    BedrockVersionService *service = BedrockVersionService::instance();
    connect(service, &BedrockVersionService::downloadLinksFetched,
            this, &BedrockVersionDetailPage::onDownloadLinksFetched);
    connect(service, &BedrockVersionService::downloadLinksFailed,
            this, &BedrockVersionDetailPage::onDownloadLinksFailed);

    loadInstanceFolders();
}

BedrockVersionDetailPage::~BedrockVersionDetailPage()
{
}

void BedrockVersionDetailPage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(32, 24, 32, 24);
    mainLayout->setSpacing(14);

    // ── 标题区 ──
    OutlinedLabel *titleLabel = new OutlinedLabel(tr("基岩版版本详情"), this);
    titleLabel->setObjectName("sectionTitle");

    // ── 置顶：版本详情信息 ──
    QFrame *detailPanel = new QFrame(this);
    detailPanel->setObjectName("bedrockDetailPanel");
    QVBoxLayout *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(18, 14, 18, 14);
    detailLayout->setSpacing(10);

    OutlinedLabel *detailTitle = new OutlinedLabel(tr("版本详情"), detailPanel);
    detailTitle->setObjectName("bedrockDetailTitle");

    m_versionInfoLabel = new QLabel(detailPanel);
    m_versionInfoLabel->setObjectName("hintLabel");
    m_versionInfoLabel->setWordWrap(true);
    m_versionInfoLabel->setText(tr("暂无版本信息"));

    // 网盘链接（Android 版）
    m_linksList = new QListWidget(detailPanel);
    m_linksList->setObjectName("versionListWidget");
    m_linksList->setFrameShape(QFrame::NoFrame);
    m_linksList->setMaximumHeight(150);
    m_linksList->setVisible(false);
    connect(m_linksList, &QListWidget::itemClicked, this, &BedrockVersionDetailPage::onLinkClicked);

    m_copyPasswordBtn = new QPushButton(tr("复制提取码"), detailPanel);
    m_copyPasswordBtn->setCursor(Qt::PointingHandCursor);
    m_copyPasswordBtn->setVisible(false);
    connect(m_copyPasswordBtn, &QPushButton::clicked, this, &BedrockVersionDetailPage::onCopyPasswordClicked);

    detailLayout->addWidget(detailTitle);
    detailLayout->addWidget(m_versionInfoLabel);
    detailLayout->addWidget(m_linksList);
    detailLayout->addWidget(m_copyPasswordBtn, 0, Qt::AlignLeft);

    // ── 顶部填写区（与 Java 版一致，无加载器）──
    QWidget *topBar = new QWidget(this);
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);

    QLabel *nameLabel = new QLabel(tr("文件名:"), topBar);
    nameLabel->setObjectName("loaderSectionLabel");
    m_fileNameEdit = new QLineEdit(topBar);
    m_fileNameEdit->setObjectName("loaderInstanceNameEdit");
    m_fileNameEdit->setPlaceholderText(tr("请输入文件名"));
    m_downloadBtn = new QPushButton(tr("下载"), topBar);
    m_downloadBtn->setObjectName("installButton");
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    connect(m_downloadBtn, &QPushButton::clicked, this, &BedrockVersionDetailPage::onDownloadClicked);

    topLayout->addWidget(nameLabel);
    topLayout->addWidget(m_fileNameEdit, 1);
    topLayout->addWidget(m_downloadBtn);

    // ── 保存位置行 ──
    QWidget *pathWidget = new QWidget(this);
    QHBoxLayout *pathLayout = new QHBoxLayout(pathWidget);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(12);

    QLabel *pathTitle = new QLabel(tr("保存位置:"), pathWidget);
    pathTitle->setObjectName("loaderSectionLabel");
    m_savePathCombo = new QComboBox(pathWidget);
    m_savePathCombo->setObjectName("loaderPathCombo");
    m_savePathCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_addFolderBtn = new QPushButton(tr("浏览..."), pathWidget);
    m_addFolderBtn->setObjectName("loaderAddFolderBtn");
    m_addFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addFolderBtn, &QPushButton::clicked, this, &BedrockVersionDetailPage::onBrowseSavePathClicked);

    pathLayout->addWidget(pathTitle);
    pathLayout->addWidget(m_savePathCombo, 1);
    pathLayout->addWidget(m_addFolderBtn);

    // ── 进度 ──
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setVisible(false);

    m_progressLabel = new QLabel(this);
    m_progressLabel->setObjectName("hintLabel");
    m_progressLabel->setWordWrap(true);

    // ── 打开下载目录 ──
    m_openFolderBtn = new QPushButton(tr("打开下载目录"), this);
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    m_openFolderBtn->setVisible(false);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &BedrockVersionDetailPage::onOpenFolderClicked);

    QHBoxLayout *folderRow = new QHBoxLayout();
    folderRow->addStretch();
    folderRow->addWidget(m_openFolderBtn);

    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(detailPanel);
    mainLayout->addWidget(topBar);
    mainLayout->addWidget(pathWidget);
    mainLayout->addWidget(m_progressBar);
    mainLayout->addWidget(m_progressLabel);
    mainLayout->addLayout(folderRow);
    mainLayout->addStretch();
}

void BedrockVersionDetailPage::setVersionEntry(const BedrockVersionEntry &entry)
{
    m_entry = entry;
    m_pendingOpenAfterFetch = false;
    m_links.clear();
    m_linksList->clear();
    m_progressBar->setVisible(false);
    m_progressLabel->clear();
    m_openFolderBtn->setVisible(false);

    // 默认文件名
    const QString arch = entry.packageArch.isEmpty() ? QStringLiteral("x64") : entry.packageArch;
    m_fileNameEdit->setText(QStringLiteral("Minecraft_Bedrock_%1_%2.msixvc")
                                .arg(entry.version, arch));
    m_downloadBtn->setText(isMcappx() ? tr("下载并安装") : tr("打开网盘链接"));

    updateDetailInfo();

    // Android 版链接：bbk 内嵌；mcapks 需异步拉取
    if (entry.source == QLatin1String("bbk")) {
        m_links = entry.cloudLinks;
        populateLinks();
    } else if (entry.source == QLatin1String("mcapks")) {
        m_progressLabel->setText(tr("正在获取网盘下载链接..."));
        BedrockVersionService::instance()->fetchDownloadLinks(entry.version);
    }
}

// ────────────────────────────────────────────────────────────
// 信号处理
// ────────────────────────────────────────────────────────────

void BedrockVersionDetailPage::onDownloadLinksFetched(const QString &version,
                                                      const QVector<BedrockCloudLink> &links)
{
    if (m_entry.version != version)
        return;
    m_links = links;
    populateLinks();
}

void BedrockVersionDetailPage::onDownloadLinksFailed(const QString &version, const QString &error)
{
    if (m_entry.version != version)
        return;
    m_progressLabel->setText(tr("获取下载链接失败: %1").arg(error));
}

void BedrockVersionDetailPage::onDownloadClicked()
{
    if (m_downloader)
        return;
    if (m_entry.version.isEmpty())
        return;

    if (isMcappx()) {
        if (!m_entry.hasDirectPackage || m_entry.packageUrl.isEmpty()) {
            NotificationManager::showInfo(this, tr("该版本暂未提供可直接下载的安装包。"));
            return;
        }
        startDownload();
        return;
    }

    // Android 版：打开第一个网盘链接
    if (m_links.isEmpty()) {
        NotificationManager::showInfo(this, tr("暂无网盘下载链接，请稍候重试。"));
        return;
    }
    QDesktopServices::openUrl(QUrl(m_links.first().url));
}

void BedrockVersionDetailPage::startDownload()
{
    const QString dir = saveDir();
    QDir d(dir);
    if (!d.exists())
        d.mkpath(QStringLiteral("."));

    QString fileName = m_fileNameEdit->text().trimmed();
    if (fileName.isEmpty())
        fileName = QStringLiteral("Minecraft_Bedrock_%1.msixvc").arg(m_entry.version);
    fileName.replace(QChar(' '), QChar('_'));
    const QString savePath = dir + QLatin1Char('/') + fileName;

    if (QFile::exists(savePath)) {
        const auto ret = AppMessageBox::question(this, tr("文件已存在"),
            tr("下载文件已存在:\n%1\n\n是否直接安装该文件？").arg(savePath),
            AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
        if (ret == AppMessageBox::Yes)
            installPackage(savePath);
        return;
    }

    DownloadTaskManager *mgr = DownloadTaskManager::instance();
    m_activeTaskId = mgr->addTask(fileName, dir, m_entry.version,
                                   QStringList() << QStringLiteral("bedrock"));
    mgr->updateTaskStatus(m_activeTaskId, DownloadTaskStatus::Downloading, tr("连接中..."));

    m_downloader = new MultiThreadDownloader(this);
    connect(m_downloader, &MultiThreadDownloader::downloadProgress, this,
        [this](const QString &, qint64 received, qint64 total) {
            if (total > 0) {
                const int pct = static_cast<int>((received * 100) / total);
                m_progressBar->setVisible(true);
                m_progressBar->setValue(pct);
                m_progressLabel->setText(tr("正在下载... %1 / %2")
                                             .arg(formatFileSize(received), formatFileSize(total)));
                DownloadTaskManager::instance()->updateTaskProgressPercent(m_activeTaskId, pct);
                DownloadTaskManager::instance()->updateTaskFileProgress(m_activeTaskId,
                                                                        QString(), received, total);
            }
        });
    connect(m_downloader, &MultiThreadDownloader::downloadCompleted, this,
        [this](const QString &, const QString &path) {
            DownloadTaskManager *mgr = DownloadTaskManager::instance();
            mgr->updateTaskProgressPercent(m_activeTaskId, 100);
            mgr->updateTaskStatus(m_activeTaskId, DownloadTaskStatus::Completed, tr("下载完成"));
            m_activeTaskId.clear();

            m_progressBar->setVisible(true);
            m_progressBar->setValue(100);
            m_progressLabel->setText(tr("下载完成: %1").arg(path));
            NotificationManager::showSuccess(this, tr("基岩版安装包下载完成。"), 6000);

            m_downloader->deleteLater();
            m_downloader = nullptr;
            m_openFolderBtn->setVisible(true);
            m_downloadBtn->setEnabled(true);

            installPackage(path);
        });
    connect(m_downloader, &MultiThreadDownloader::downloadFailed, this,
        [this](const QString &, const QString &error) {
            DownloadTaskManager::instance()->updateTaskStatus(m_activeTaskId,
                                                              DownloadTaskStatus::Failed, error);
            m_activeTaskId.clear();

            m_progressBar->setVisible(true);
            m_progressBar->setValue(0);
            m_progressLabel->setText(tr("下载失败: %1").arg(error));
            NotificationManager::showError(this, tr("基岩版安装包下载失败: %1").arg(error));

            m_downloader->deleteLater();
            m_downloader = nullptr;
            m_downloadBtn->setEnabled(true);
        });

    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_progressLabel->setText(tr("准备下载..."));
    m_downloadBtn->setEnabled(false);

    const int threads = SettingsManager::instance()->getDownloadThreadCount();
    m_downloader->startDownload(m_entry.packageUrl, savePath, threads,
                                m_activeTaskId, QStringLiteral("BlockBox/1.0"));
}

void BedrockVersionDetailPage::installPackage(const QString &packagePath)
{
    const auto ret = AppMessageBox::question(this, tr("安装基岩版"),
        tr("是否立即将下载的基岩版安装到本机？\n\n%1\n\n（将通过 Add-AppxPackage 安装，"
           "安装完成后即可在「基岩版」模式下启动游戏。）").arg(packagePath),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::Yes);
    if (ret != AppMessageBox::Yes)
        return;

    m_progressLabel->setText(tr("正在安装，请稍候..."));
    m_downloadBtn->setEnabled(false);

    QProcess *process = new QProcess(this);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, process, packagePath](int exitCode, QProcess::ExitStatus) {
            const QString output = QString::fromUtf8(process->readAllStandardOutput()) +
                                   QString::fromUtf8(process->readAllStandardError());
            process->deleteLater();
            m_downloadBtn->setEnabled(true);

            if (exitCode == 0) {
                m_progressLabel->setText(tr("安装完成，可在「基岩版」模式下启动游戏。"));
                NotificationManager::showSuccess(this, tr("基岩版安装成功！"), 7000);
            } else {
                const QString detail = output.trimmed().right(400);
                m_progressLabel->setText(tr("安装失败（错误码 %1）。").arg(exitCode));
                NotificationManager::showError(this,
                    tr("基岩版安装失败:\n%1\n\n提示：若为预览版（Beta/Preview），"
                       "需要已登录微软商店账号并开启开发人员模式。").arg(detail), 10000);
            }
        });

    const QString script = QStringLiteral("Add-AppxPackage -Path '%1'").arg(
        QString(packagePath).replace(QLatin1Char('\''), QStringLiteral("''")));
    process->start(QStringLiteral("powershell.exe"),
                   QStringList() << QStringLiteral("-NoProfile")
                                 << QStringLiteral("-ExecutionPolicy")
                                 << QStringLiteral("Bypass")
                                 << QStringLiteral("-Command") << script);
}

// ────────────────────────────────────────────────────────────
// 其它槽
// ────────────────────────────────────────────────────────────

void BedrockVersionDetailPage::onBrowseSavePathClicked()
{
    const QString dir = AppFileDialog::getExistingDirectory(this, tr("选择下载保存目录"), saveDir());
    if (dir.isEmpty())
        return;
    m_savePathCombo->setCurrentText(dir);
    const QString path = m_savePathCombo->currentData().toString();
    if (path != dir) {
        // 用浏览目录直接作为保存位置（优先已有项，否则追加）
        const int idx = m_savePathCombo->findData(dir);
        if (idx >= 0)
            m_savePathCombo->setCurrentIndex(idx);
        else
            m_savePathCombo->addItem(QFileInfo(dir).fileName(), dir);
    }
}

void BedrockVersionDetailPage::onOpenFolderClicked()
{
    const QString dir = saveDir();
    QDir d(dir);
    if (!d.exists())
        d.mkpath(QStringLiteral("."));
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void BedrockVersionDetailPage::onLinkClicked()
{
    const int row = m_linksList->currentRow();
    if (row < 0 || row >= m_links.size())
        return;
    QDesktopServices::openUrl(QUrl(m_links[row].url));
}

void BedrockVersionDetailPage::onCopyPasswordClicked()
{
    for (const BedrockCloudLink &link : m_links) {
        if (!link.password.isEmpty()) {
            QGuiApplication::clipboard()->setText(link.password);
            NotificationManager::showSuccess(this, tr("提取码已复制: %1").arg(link.password));
            return;
        }
    }
    NotificationManager::showInfo(this, tr("该版本网盘链接无提取码。"));
}

// ────────────────────────────────────────────────────────────
// 辅助
// ────────────────────────────────────────────────────────────

void BedrockVersionDetailPage::loadInstanceFolders()
{
    m_savePathCombo->blockSignals(true);
    m_savePathCombo->clear();

    const QList<InstanceFolderInfo> folders = SettingsManager::instance()->getInstanceFolders();
    int defaultIndex = 0;
    for (int i = 0; i < folders.size(); ++i) {
        const InstanceFolderInfo &folder = folders[i];
        m_savePathCombo->addItem(folder.name, folder.path);
        if (folder.isDefault)
            defaultIndex = i;
    }
    if (folders.isEmpty()) {
        const QString appDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        m_savePathCombo->addItem(tr("默认目录"), appDir);
    }
    m_savePathCombo->setCurrentIndex(defaultIndex);
    m_savePathCombo->blockSignals(false);
}

void BedrockVersionDetailPage::updateDetailInfo()
{
    if (m_entry.version.isEmpty()) {
        m_versionInfoLabel->setText(tr("暂无版本信息"));
        return;
    }

    QString info = tr("版本: %1\n类型: %2 · 来源: %3")
                       .arg(m_entry.version, versionTypeName(m_entry.type),
                            BedrockVersionService::sourceDisplayName(m_entry.source));
    if (!m_entry.date.isEmpty())
        info += QStringLiteral("\n日期: %1").arg(m_entry.date);
    if (!m_entry.size.isEmpty())
        info += QStringLiteral("\n大小: %1").arg(m_entry.size);
    if (isMcappx()) {
        if (m_entry.hasDirectPackage)
            info += QStringLiteral("\n架构: %1").arg(m_entry.packageArch);
        else
            info += QStringLiteral("\n该版本暂未提供可直接下载的安装包。");
    }
    m_versionInfoLabel->setText(info);
}

void BedrockVersionDetailPage::populateLinks()
{
    m_linksList->clear();
    for (const BedrockCloudLink &link : m_links) {
        QString text = link.name;
        if (!link.password.isEmpty())
            text += QStringLiteral("  ·  提取码: %1").arg(link.password);
        m_linksList->addItem(text);
    }
    m_linksList->setVisible(!m_links.isEmpty());
    m_copyPasswordBtn->setVisible(!m_links.isEmpty());

    if (m_links.isEmpty())
        m_progressLabel->setText(tr("该版本暂无可用的网盘下载链接。"));
    else
        m_progressLabel->clear();

    if (m_pendingOpenAfterFetch && !m_links.isEmpty()) {
        m_pendingOpenAfterFetch = false;
        QDesktopServices::openUrl(QUrl(m_links.first().url));
    }
}

QString BedrockVersionDetailPage::saveDir() const
{
    const QString data = m_savePathCombo->currentData().toString();
    if (!data.isEmpty())
        return data;
    return m_savePathCombo->currentText();
}

QString BedrockVersionDetailPage::versionTypeName(BedrockVersionType type) const
{
    switch (type) {
    case BedrockVersionType::Release: return tr("正式版");
    case BedrockVersionType::Beta:    return tr("测试版");
    case BedrockVersionType::Preview: return tr("预览版");
    }
    return tr("未知");
}

QString BedrockVersionDetailPage::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024LL * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

bool BedrockVersionDetailPage::isMcappx() const
{
    return m_entry.source == QLatin1String("mcappx");
}
