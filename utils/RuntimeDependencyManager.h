/**
 * @file   RuntimeDependencyManager.h
 * @brief  移动端（安卓 / 鸿蒙）运行依赖（Java 运行环境等）的检查与一键安装
 * @author BlockBox Team
 * @date   2026-10-23
 *
 * 移动设备无法像桌面端那样直接扫描系统里的 Java，因此把「运行 Minecraft 所
 * 需要的运行环境」做成可远程更新的依赖清单：
 *   - 清单（JSON）托管在 GitHub 仓库/发布资源中，随时可以发布新版本；
 *   - 客户端检查更新时同时拉取清单，与启动器自身更新合并展示；
 *   - 用户点「安装」后自动下载、校验、解压到标准运行目录，
 *     启动器原有的 Java 扫描会自动发现这些运行时。
 *
 * 依赖包只支持 zip / tar.gz，解压由 RuntimeArchive 在没有外部命令的
 * 移动环境下完成。
 */

#ifndef RUNTIMEDEPENDENCYMANAGER_H
#define RUNTIMEDEPENDENCYMANAGER_H

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QFile;
class QNetworkReply;
class QTimer;
class QWidget;

/** 依赖包在某个 CPU 架构下的下载信息 */
struct RuntimeDependencyPackage {
    QString abi;        ///< arm64-v8a / x86_64 / x86 / armeabi-v7a，空表示通用
    QString url;        ///< 下载地址
    qint64 sizeBytes = 0; ///< 文件大小（0 表示不校验）
    QString sha256;     ///< SHA-256 校验值（支持 "sha256:" 前缀）
};

/** 一项运行依赖 */
struct RuntimeDependency {
    QString id;          ///< 唯一标识，如 jre21
    QString name;        ///< 显示名称，如 "Java 21 运行环境"
    QString description; ///< 一句话说明
    QString kind;        ///< "java"（安装到 Java 目录）/ "runtime"（安装到 runtime 目录）
    QString version;     ///< 清单中的最新版本
    QString installedVersion; ///< 已安装版本
    bool installed = false;   ///< 本地是否已安装
    bool required = true;     ///< 是否必需（必需项会在启动器更新窗口里提示）
    QString sizeHint;         ///< 体积提示，如 "≈ 45 MB"
    QList<RuntimeDependencyPackage> packages;

    /** 需要首次安装 */
    bool needsInstall() const { return !installed; }
    /** 已安装但版本与清单不一致 */
    bool needsUpdate() const
    {
        return installed && !installedVersion.isEmpty() && installedVersion != version;
    }
    /** 安装或更新都算待处理 */
    bool isPending() const { return needsInstall() || needsUpdate(); }
    /** 当前设备架构对应的下载包（没有则返回 nullptr） */
    const RuntimeDependencyPackage *packageForCurrentAbi() const;
    /** 当前架构下载包的体积提示文本 */
    QString downloadSizeText() const;
};

class RuntimeDependencyManager : public QObject
{
    Q_OBJECT

public:
    static RuntimeDependencyManager *instance();

    /** 当前平台是否支持运行依赖（安卓 / 鸿蒙） */
    static bool isSupportedPlatform();
    /** 依赖清单地址（可在设置中覆盖） */
    static QString manifestUrl();
    /** 运行目录根：<GenericDataLocation>/BlockBox */
    static QString installRoot();
    /** 依赖安装目录 */
    static QString installDirFor(const RuntimeDependency &dependency);
    /** 当前设备的移动端 ABI 标签 */
    static QString currentAbiTag();
    /**
     * @brief 当前设备可接受的下载包 abi 键（按优先级排列）
     *
     * 普通设备只有自身架构一个键；鸿蒙端额外接受
     * "harmony-<abi>" 与 "ohos-arm64"，方便发布方把鸿蒙专用的
     * 运行时和安卓包放在同一份清单里区分下发。
     */
    static QStringList abiKeyCandidates();

    bool isChecking() const { return m_checking; }
    bool isInstalling() const { return m_installing; }
    const QList<RuntimeDependency> &dependencies() const { return m_dependencies; }
    QList<RuntimeDependency> pendingDependencies() const;
    int pendingCount() const;
    RuntimeDependency dependencyById(const QString &id) const;

    /**
     * @brief 检查依赖清单是否有新版本
     * @param parent  弹窗父窗口（可为空）
     * @param silent  静默模式：不主动弹出提示
     */
    void checkForUpdates(QWidget *parent = nullptr, bool silent = true);

    /** 重新读取本地已安装状态（不联网） */
    void refreshInstalledState();

    /** 安装/更新单项依赖（异步，进度通过信号与任务栏展示） */
    void install(const QString &id);
    /** 安装/更新所有待处理依赖 */
    void installAll();
    /** 卸载某项依赖 */
    bool remove(const QString &id);
    /** 取消当前安装队列 */
    void cancelAll();

signals:
    void checkStarted();
    void checkFinished(bool success, const QString &error);
    void dependenciesChanged();
    void installProgress(const QString &id, int percent, const QString &stage);
    void installFinished(const QString &id, bool success, const QString &message);
    void allInstallsFinished(bool allSucceeded);

private:
    explicit RuntimeDependencyManager(QObject *parent = nullptr);
    ~RuntimeDependencyManager() override = default;

    void issueManifestRequest();
    void handleManifestReply(QNetworkReply *reply);
    void finishCheck(bool success, const QString &error);
    bool applyManifest(const QByteArray &payload, QString *errorMessage);

    void markInstalledState(RuntimeDependency &dependency) const;
    void writeMarker(const RuntimeDependency &dependency, const QString &dir) const;
    void applyJavaPermissions(const QString &dir) const;

    void startNextInstall();
    void beginInstall(const QString &id);
    void issueDownloadRequest(const RuntimeDependency &dependency,
                              const RuntimeDependencyPackage &package);
    void handleDownloadReply(const RuntimeDependency &dependency,
                             const RuntimeDependencyPackage &package);
    void finishInstall(const QString &id, bool success, const QString &message);
    void cleanupInstallArtifacts();
    void emitInstallProgress(const QString &id, int percent, const QString &stage);

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QWidget> m_parent;
    QTimer *m_checkTimer = nullptr;
    bool m_silent = true;
    bool m_checking = false;
    int m_attempt = 0;

    QList<RuntimeDependency> m_dependencies;

    bool m_installing = false;
    bool m_anyFailure = false;
    QString m_lastFailureMessage;
    QStringList m_queue;
    QString m_currentId;
    QNetworkReply *m_downloadReply = nullptr;
    QFile *m_downloadFile = nullptr;
    qint64 m_downloadReceived = 0;
    QString m_downloadPath;
    QString m_stagingDir;
    QString m_taskId;
};

#endif // RUNTIMEDEPENDENCYMANAGER_H
