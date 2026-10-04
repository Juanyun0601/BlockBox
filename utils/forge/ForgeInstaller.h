#pragma once

#include <QObject>
#include <QString>

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

    static ForgeInstaller *m_instance;
};