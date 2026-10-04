/**
 * @file   BedrockVersionDetailPage.h
 * @brief  基岩版版本详情页 — 顶部与 Java 版一致的信息填写区（不含加载器），下方为详情信息
 * @author BlockBox Team
 *
 * 入口：基岩版下载页点击版本卡片进入。
 *
 * 顶部（参考 Java 版 LoaderDetailPage）：
 *   文件名 / 保存位置 / 下载按钮，无加载器选择。
 * 下方：
 *   版本详情信息、网盘链接（Android）、下载进度、返回/打开目录。
 */

#pragma once

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QVector>
#include <QWidget>

#include "utils/bedrock/BedrockVersionService.h"

class MultiThreadDownloader;

class BedrockVersionDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit BedrockVersionDetailPage(QWidget *parent = nullptr);
    ~BedrockVersionDetailPage() override;

    /// 设置当前版本条目并刷新页面内容
    void setVersionEntry(const BedrockVersionEntry &entry);

private slots:
    void onDownloadClicked();
    void onBrowseSavePathClicked();
    void onOpenFolderClicked();
    void onLinkClicked();
    void onCopyPasswordClicked();
    void onDownloadLinksFetched(const QString &version, const QVector<BedrockCloudLink> &links);
    void onDownloadLinksFailed(const QString &version, const QString &error);

private:
    void initUI();
    void loadInstanceFolders();
    void updateDetailInfo();
    void populateLinks();
    void startDownload();
    void installPackage(const QString &packagePath);
    QString saveDir() const;
    QString versionTypeName(BedrockVersionType type) const;
    QString formatFileSize(qint64 bytes) const;
    bool isMcappx() const;

    // 顶部（与 Java 版一致，不含加载器）
    QLineEdit *m_fileNameEdit;
    QComboBox *m_savePathCombo;
    QPushButton *m_addFolderBtn;
    QPushButton *m_downloadBtn;

    // 下方详情信息
    QLabel *m_versionInfoLabel;
    QListWidget *m_linksList;
    QPushButton *m_copyPasswordBtn;
    QProgressBar *m_progressBar;
    QLabel *m_progressLabel;
    QPushButton *m_openFolderBtn;

    // 数据
    BedrockVersionEntry m_entry;
    QVector<BedrockCloudLink> m_links;
    MultiThreadDownloader *m_downloader;
    QString m_activeTaskId;
    bool m_pendingOpenAfterFetch;
};
