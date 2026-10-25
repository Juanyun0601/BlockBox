#pragma once

#include <QObject>
#include <QString>

#include "../SettingsManager.h"

namespace forge {
class ForgeDownloader;
class ForgeNewInstallTask;
}

class ForgeInstaller : public QObject
{
    Q_OBJECT
public:
    static ForgeInstaller* instance();

    void setCurrentTaskId(const QString &taskId);
    void setDownloadSource(ForgeDownloadSource source);
    QString currentTaskId() const { return m_taskId; }
    void downloadForgeInstaller(const QString &mcVersion, const QString &forgeVersion,
                                const QString &instancePath, const QString &instanceName = QString());

    void setInstancePath(const QString &path);
    void setMcVersion(const QString &version);
    void setForgeVersion(const QString &version);
    void setTaskId(const QString &taskId);
    void setVersionIsolationEnabled(bool enabled);

    void startInstall();
    void cancelInstall();
    bool isInstalling() const;

    QString taskId() const;

signals:
    void installStarted(const QString &taskId);
    void installProgress(const QString &taskId, const QString &stage, int percent);
    void installCompleted(const QString &taskId);
    void installFailed(const QString &taskId, const QString &error);
    void installCancelled(const QString &taskId);

public:
    ForgeInstaller(QObject *parent = nullptr);
    ~ForgeInstaller();

    ForgeInstaller(const ForgeInstaller&) = delete;
    ForgeInstaller& operator=(const ForgeInstaller&) = delete;

    void onInstallerDownloaded(bool success);
    void onNewInstallCompleted(bool success, const QString &error);
    void onInstallerTypeDetected(int installerType);
    int detectInstallerType(const QString &jarPath);
    void detectInstallerTypeAsync(const QString &jarPath);
    /// MultiMC 式多源回退：一个源超时/失败自动换另一个源重试，最多两轮
    void attemptInstallerDownload();
    /// 单轮下载的结算入口（成功直通，失败先换源重试）
    void onInstallerDownloadAttemptDone(bool success);
    /// MultiMC 式安装（新版安装器）：解析 profile → 跑处理器 → 生成版本 JSON → 补齐库
    void startNewInstallTask();
    /// MultiMC Legacy 路径（MC ≤1.5.2）：下载 universal 包合并进原版 jar + 补 FML 依赖库
    void startLegacyInstallTask();
    void finishLegacyInstall(const QString &error = QString());
    void runInstallerDirectly();
    QString getJavaPath();
    QString getJarToolPath();

    bool extractEntryFromJar(const QString &jarPath, const QString &entryPath,
                             const QString &destPath);
    bool tryExtractWithJarTool(const QString &jarPath, const QString &entryPath,
                               const QString &destPath);
    bool tryExtractWithPowerShell(const QString &jarPath, const QString &entryPath,
                                  const QString &destPath);
    bool tryExtractWithUnzip(const QString &jarPath, const QString &entryPath,
                             const QString &destPath);
    bool tryExtractWithPython(const QString &jarPath, const QString &entryPath,
                              const QString &destPath);
    bool tryExtractManual(const QString &jarPath, const QString &entryPath,
                          const QString &destPath);
    QByteArray decompressRawDeflate(const QByteArray &compressedData, quint32 uncompressedSize);

    forge::ForgeDownloader *m_downloader;
    forge::ForgeNewInstallTask *m_installTask;
    QString m_instancePath;
    QString m_mcVersion;
    QString m_forgeVersion;
    QString m_installerJarPath;
    QString m_tempDir;
    QString m_taskId;
    bool m_isInstalling;
    bool m_versionIsolationEnabled;
    int m_installerAttempt = 0;   ///< 安装器下载已尝试轮数（0=首选源，1=换源重试）
    bool m_installerAttemptSettled = false; ///< 本轮下载已结算（看门狗与回调防双触发）
    bool m_legacyMode = false;     ///< MC ≤1.5.2：无可用安装器，走 MultiMC Legacy 合并路径
    bool m_legacySettled = false;  ///< Legacy 流程已结算（合并线程回调与取消防双触发）

    static ForgeInstaller *m_instance;
};