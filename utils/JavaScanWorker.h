/**
 * @file   JavaScanWorker.h
 * @brief  Java 检测后台线程工作类（注册表 + 厂商目录 + 关键词 BFS 全盘）
 * @author BlockBox Team
 * @date   2026-05-22
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QSet>
#include <QPair>
#include <QAtomicInt>

// Java 安装信息结构体（扩展版本，包含架构等详细信息）
struct JavaInstallInfo {
    QString path;           // java.exe / java 可执行文件路径
    QString version;        // 版本号（如 "17.0.1"）
    QString vendor;         // 厂商（如 "Eclipse Adoptium", "Oracle"）
    QString arch;           // 架构（如 "x64", "x86", "aarch64"）
    QString javaHome;       // JAVA_HOME 路径
    bool valid;             // 是否有效

    JavaInstallInfo() : valid(false) {}
};

class JavaScanWorker : public QObject
{
    Q_OBJECT

public:
    explicit JavaScanWorker(QObject* parent = nullptr);
    ~JavaScanWorker();

    void cancel();
    bool isCancelled() const;

public slots:
    void doScan();

signals:
    /// 扫描进度
    ///   currentPath: 当前文件路径或盘符提示
    ///   percent:     百分比 0-100（-1=indeterminate 滚动条）
    ///   etaMinutes:  预计剩余分钟数（-1=计算中）
    ///   scannedMB:   已扫描容量估算 MB
    ///   totalMB:     总容量 MB
    ///   foundCount:  已发现的 Java 数量
    void scanProgress(const QString& currentPath, int percent, int etaMinutes,
                      int scannedMB, int totalMB, int foundCount);

    void scanFinished(const QList<QPair<QString, QString>>& javaInstallations);

private:
    // 从环境变量中快速查找 Java（JAVA_HOME / PATH）
    void scanEnvironmentVariables(QList<QPair<QString, QString>>& outResults);

    // 扫描 Windows 注册表中的 Java 安装信息
    void scanRegistry(QList<QPair<QString, QString>>& outResults);

    // 从指定注册表键扫描 Java 安装（Windows 原生 API，支持 HKEY_CURRENT_USER 和 HKEY_LOCAL_MACHINE）
    void scanRegistryKey(void* hKeyBase, const QString& keyName, const QString& javaHomeValueName,
                         const QString& subkeySuffix, QList<QPair<QString, QString>>& outResults);

    // 扫描已知厂商安装目录（BellSoft / Zulu / AdoptOpenJDK / Microsoft / Eclipse / Semeru 等）
    void scanVendorDirectories(QList<QPair<QString, QString>>& outResults);

    // 扫描 Minecraft 启动器自带的运行时
    void scanMinecraftRuntime(QList<QPair<QString, QString>>& outResults);

    // 尝试验证并加入一个 java.exe / javaw.exe 路径
    void tryAddJava(const QString& dirPath, QList<QPair<QString, QString>>& outResults);

    // 带详细信息的尝试验证（包含架构、厂商等）
    void tryAddJavaDetailed(const QString& dirPath, QList<JavaInstallInfo>& outResults);

    // 扫描单个盘符，返回找到的 Java 数量
    int scanDrive(const QString& drive, qint64 driveBytes,
                  qint64& cumulativeBytes, int& driveBasePercent,
                  int drivePercentShare, int& totalFound,
                  QList<QPair<QString, QString>>& outResults);

    // Java 验证
    bool validateJavaPath(const QString& path, QString& outVersion);

    // Java 验证（详细版本，包含架构信息）
    bool validateJavaPathDetailed(const QString& path, JavaInstallInfo& outInfo);

    // 检测 Java 可执行文件的架构（通过文件名或路径推断）
    QString detectJavaArch(const QString& javaPath);

    // 路径是否命中排除目录
    bool isPathExcluded(const QString& filePath) const;

    // 目录名是否包含 Java 相关关键词（用于 BFS 剪枝）
    bool isJavaRelatedDirectory(const QString& dirName) const;

    // 目录名是否形如版本号（如 1.8.0_301 / 17.0.1）
    bool isVersionLikeDirectory(const QString& dirName) const;

    QAtomicInt m_cancelled;

    QStringList m_drives;
    QStringList m_excludeLower;          // 排除目录（小写）
    QList<QString> m_keywords;           // Java 相关关键词（小写）

    qint64 m_scanStartTime;
    qint64 m_totalDriveMB;               // 所有盘符总容量 MB

    QSet<QString>     m_seenPaths;
    int               m_lastPercent;
};
