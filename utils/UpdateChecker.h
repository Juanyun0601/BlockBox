/**
 * @file   UpdateChecker.h
 * @brief  启动器更新检查器：从 GitHub Releases 获取最新版本并比较，支持应用内下载安装
 * @author BlockBox Team
 * @date   2026-10-05
 *
 * 升级通道（设置项 update_channel，默认抢先升级）：
 *   - 抢先升级（beta）：测试版（X.Y.Z-betaN）与正式版（X.Y.Z）都提示；
 *   - 保守升级（stable）：仅提示正式版，测试版一律忽略。
 *
 * 应用内更新（当前仅 Windows）：
 *   - 按平台/架构挑选 Release 资产：安装版选 setup.exe，绿色版选 portable.zip
 *     （以 exe 目录下是否存在 Inno 卸载器 unins000.exe 区分）；
 *   - startDownload() 后台下载并广播进度，完成后点击侧边栏圆形按钮
 *     会弹出确认并重启系统完成安装（安装版静默运行安装程序，绿色版解压覆盖）。
 *
 * 用法：
 *   - 启动时静默检查：checkForUpdates(parent, true)，仅发现新版本时弹窗；
 *     失败（无网络、接口异常、仓库暂无 Release）一律静默忽略，不打扰用户。
 *   - 设置页手动检查：checkForUpdates(parent, false)，无论结果如何都给出反馈。
 */
#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include "utils/AppVersion.h"

class QFile;
class QJsonArray;
class QNetworkAccessManager;
class QNetworkReply;
class QString;
class QWidget;

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    /** 按升级通道筛出的候选 Release */
    struct ReleaseCandidate {
        QString version;      // 解析出的版本号（tag 优先，Release 名称兜底）
        QString releaseName;  // Release 标题
        QString notes;        // 更新说明（body）
        QUrl pageUrl;         // Release 页面（html_url）
        bool isBeta = false;  // 是否测试版
        // 应用内更新资产（无匹配资产时为空，仅弹"前往下载"）
        QString assetUrl;     // 资产下载地址（github.com）
        QString assetName;    // 资产文件名，如 BlockBox-1.0.0-win64-setup.exe
        bool isValid() const { return !version.isEmpty(); }
    };

    /** 应用内更新包的下载状态 */
    enum DownloadState {
        DownloadIdle,      // 未开始
        DownloadRunning,   // 下载中
        DownloadCompleted, // 已下载完成，可安装
        DownloadFailed     // 上次下载失败
    };

    static UpdateChecker *instance();

    /**
     * @brief 检查更新
     * @param parent 弹窗父窗口
     * @param silent true=静默模式（仅发现新版本时弹窗，失败忽略）；
     *               false=手动模式（新版本/已是最新/检查失败均弹窗提示）
     */
    void checkForUpdates(QWidget *parent, bool silent);

    /** 是否正在检查中（用于按钮防抖） */
    bool isChecking() const { return m_checking; }

    /** 最近一次检查发现的候选版本（未检查过时无效） */
    const ReleaseCandidate &lastCandidate() const { return m_lastCandidate; }

    /** 开始下载应用内更新包（无可用资产或正在下载时忽略） */
    void startDownload();

    /** 弹确认框后重启流程完成安装（仅下载完成后有效） */
    void installDownloadedUpdate();

    DownloadState downloadState() const { return m_downloadState; }
    bool isDownloading() const { return m_downloadState == DownloadRunning; }
    /** 已下载完成的更新包完整路径（未完成时为空） */
    const QString &downloadedFilePath() const { return m_downloadPath; }

signals:
    /** 一次检查结束（无论成败）。hasUpdate 仅在成功且发现新版本时为 true */
    void checkFinished(bool hasUpdate, const QString &latestVersion);

    /** 应用内更新包开始下载 */
    void downloadStarted();
    /** 下载进度（total<=0 表示总大小未知） */
    void downloadProgress(qint64 received, qint64 total);
    /** 更新包下载完成 */
    void downloadFinished();
    /** 更新包下载失败 */
    void downloadFailed(const QString &error);

private:
    explicit UpdateChecker(QObject *parent = nullptr);

    void fetchLatest();
    void issueRequest();
    void handleReply(QNetworkReply *reply);

    /**
     * @brief 按当前升级通道从 Releases 列表中选出候选版本
     *
     * 列表按发布时间新→旧排列，返回第一个属于当前通道的 Release：
     * 抢先升级取最新的任意版本；保守升级跳过测试版取最新的正式版。
     * 同时从 assets 中挑选适配当前平台/架构的安装包。
     */
    ReleaseCandidate pickReleaseForChannel(const QJsonArray &releases) const;

    /** 从资产文件名中匹配当前平台应下载的安装包 */
    void pickAssetForPlatform(ReleaseCandidate &candidate,
                              const QJsonArray &assets) const;

    void issueDownloadRequest();
    void beginDownloadAttempt();
    void handleDownloadReply(QNetworkReply *reply);
    void resetDownloadState(DownloadState state);

    /** 鸿蒙：系统包管理器安装结果回调（失败时提供 hdc 命令兜底） */
    void onHarmonyInstallFinished(bool success, const QString &error);
    void showHdcInstallFallbackDialog(const QString &error, const QString &devicePath);

    void showUpdateDialog(const ReleaseCandidate &release);

    QNetworkAccessManager *m_nam;
    // 弹窗父窗口：检查是异步的，用 QPointer 防止窗口销毁后悬空
    QPointer<QWidget> m_parent;
    bool m_silent = true;
    bool m_checking = false;
    int m_attempt = 0;

    ReleaseCandidate m_lastCandidate;

    DownloadState m_downloadState = DownloadIdle;
    QString m_downloadPath;   // 更新包落盘路径
    int m_downloadAttempt = 0;
    QFile *m_downloadFile = nullptr;
    QElapsedTimer m_progressThrottle; // 限流进度信号，避免刷爆重绘
    qint64 m_received = 0;
};

#endif // UPDATECHECKER_H
