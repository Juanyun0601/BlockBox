/**
 * @file   InstanceJavaDownloadPage.h
 * @brief  实例助手 - Java 下载页面，多发行版 / 多镜像源 / 多版本
 * @author BlockBox Team
 * @date   2026-07-21
 *
 * 用于 InstanceAssistantWindow 的「Java管理」标签内的下载子页。
 * 与主程序 JavaDownloadPage 共享同一个 JavaDownloader 单例，但 UI 适配窄窗口（450px）：
 *  - 顶部：返回按钮 + 标题
 *  - 发行版下拉 + 描述
 *  - 镜像源下拉（仅 Adoptium 启用）
 *  - 版本下拉 + 二进制列表
 *  - 安装路径 + 浏览
 *  - 进度条 + 详情
 *  - 下载 / 取消按钮
 *
 * 安装完成后会发出 javaInstalled 信号，由 InstanceJavaPage 监听并刷新列表。
 */
#ifndef INSTANCEJAVADOWNLOADPAGE_H
#define INSTANCEJAVADOWNLOADPAGE_H

#include <QString>
#include <QVector>
#include <QWidget>

#include "components/OutlinedLabel.h"
#include "utils/JavaDownloader.h"

class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

class InstanceJavaDownloadPage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceJavaDownloadPage(QWidget *parent = nullptr);
    ~InstanceJavaDownloadPage() = default;

signals:
    /** 用户点击返回按钮，请求切换回 Java 管理页 */
    void backRequested();
    /** Java 安装成功完成 */
    void javaInstalled(const QString &javaPath, const QString &versionName);

private slots:
    void onDistributionChanged(int index);
    void onMirrorChanged(int index);
    void onVersionChanged(int index);
    void onBinarySelectionChanged(int row);
    void onDownloadClicked();
    void onBrowseClicked();
    void onCancelClicked();
    void onBackClicked();

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
    void updateBinaryDetail();
    QString formatFileSize(qint64 bytes) const;
    QString defaultInstallPath() const;
    QString findJavaExecutable(const QString &dir) const;

    // ---- UI 控件 ----
    QPushButton *m_backBtn;
    OutlinedLabel *m_titleLabel;
    QLabel *m_subtitleLabel;

    QComboBox *m_distributionCombo;
    QLabel *m_distributionDescLabel;

    QComboBox *m_mirrorCombo;

    QComboBox *m_versionCombo;
    QListWidget *m_binaryListWidget;
    QLabel *m_binaryDetailLabel;

    QLabel *m_pathLabel;
    QPushButton *m_browseBtn;

    QProgressBar *m_progressBar;
    QLabel *m_progressDetailLabel;

    QLabel *m_statusLabel;

    QPushButton *m_downloadBtn;
    QPushButton *m_cancelBtn;

    // ---- 数据 ----
    QVector<JavaBinaryInfo> m_currentBinaries;
    JavaBinaryInfo m_selectedBinary;
    QString m_extractPath;
};

#endif // INSTANCEJAVADOWNLOADPAGE_H
