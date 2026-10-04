/**
 * @file   JavaScanWorker.cpp
 * @brief  Java 检测后台线程工作类（注册表 + 厂商目录 + Minecraft 运行时 + 关键词 BFS 全盘）
 * @author BlockBox Team
 * @date   2026-05-22
 */

#include "utils/JavaScanWorker.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQueue>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// ────────────────────────────────────────────────────────────
// 构造 / 析构
// ────────────────────────────────────────────────────────────

JavaScanWorker::JavaScanWorker(QObject* parent)
    : QObject(parent)
    , m_cancelled(0)
    , m_scanStartTime(0)
    , m_totalDriveMB(0)
    , m_lastPercent(0)
{
#ifdef Q_OS_ANDROID
    // Android: scan specific directories
    m_drives << "/system/bin" << "/data/data" << "/storage/emulated/0";
    m_excludeLower.clear();
#else
    m_drives << "C:/" << "D:/" << "E:/";
    m_excludeLower << "windows" << "programdata" << "$recycle.bin"
                   << "system volume information" << "recovery"
                   << "msocache" << "perflogs";
#endif

    // Java 相关关键词，用于 BFS 剪枝（目录名包含这些词才继续深入）
    // 综合 PCL-CE / ProjBobcat 的关键词集合
    m_keywords
        << "java" << "jdk" << "jre" << "jvm"
        << "oracle" << "openjdk" << "adoptium" << "adoptopenjdk"
        << "temurin" << "zulu" << "azul" << "bellsoft" << "liberica"
        << "microsoft" << "semeru" << "corretto" << "amazon"
        << "eclipse" << "kona" << "dragonwell" << "sapmachine"
        << "runtime" << "environment" << "env"
        << "x64" << "x86_64" << "amd64" << "arm64" << "aarch64"
        << "minecraft" << "mojang" << "mc" << "game"
        << "hmcl" << "pcl" << "baka" << "blockbox"
        << "jdks" << "javanw" << "javapath";
}

JavaScanWorker::~JavaScanWorker() {}

// ────────────────────────────────────────────────────────────
// 公共接口
// ────────────────────────────────────────────────────────────

void JavaScanWorker::cancel()
{
    m_cancelled.storeRelease(1);
}

bool JavaScanWorker::isCancelled() const
{
    return m_cancelled.loadAcquire() != 0;
}

// ────────────────────────────────────────────────────────────
// 通用：尝试加入一个 java.exe / javaw.exe
// ────────────────────────────────────────────────────────────

void JavaScanWorker::tryAddJava(const QString& dirPath, QList<QPair<QString, QString>>& outResults)
{
    if (isCancelled()) return;
    if (dirPath.isEmpty() || dirPath == "/" || dirPath == "\\") return;

#ifdef Q_OS_ANDROID
    QString javaPath = QDir(dirPath).filePath("java");
    QString exePath;
    if (QFile::exists(javaPath))
        exePath = QFileInfo(javaPath).absoluteFilePath();
    else
        return;
#else
    QString javaPath  = QDir(dirPath).filePath("java.exe");
    QString javawPath = QDir(dirPath).filePath("javaw.exe");

    // 优先 java.exe，其次 javaw.exe
    QString exePath;
    if (QFile::exists(javaPath))
        exePath = QDir::toNativeSeparators(QFileInfo(javaPath).absoluteFilePath());
    else if (QFile::exists(javawPath))
        exePath = QDir::toNativeSeparators(QFileInfo(javawPath).absoluteFilePath());
    else
        return;
#endif

    if (m_seenPaths.contains(exePath)) return;
    m_seenPaths.insert(exePath);

    QString version;
    if (validateJavaPath(exePath, version)) {
        outResults.append(qMakePair(exePath, version));
        qDebug() << "[JavaScanWorker] 发现 Java:" << version << "@" << exePath;
    }
}

// ────────────────────────────────────────────────────────────
// 带详细信息的尝试验证（包含架构、厂商等）
// ────────────────────────────────────────────────────────────

void JavaScanWorker::tryAddJavaDetailed(const QString& dirPath, QList<JavaInstallInfo>& outResults)
{
    if (isCancelled()) return;
    if (dirPath.isEmpty() || dirPath == "/" || dirPath == "\\") return;

#ifdef Q_OS_ANDROID
    QString javaPath = QDir(dirPath).filePath("java");
    QString exePath;
    if (QFile::exists(javaPath))
        exePath = QFileInfo(javaPath).absoluteFilePath();
    else
        return;
#else
    QString javaPath  = QDir(dirPath).filePath("java.exe");
    QString javawPath = QDir(dirPath).filePath("javaw.exe");

    // 优先 java.exe，其次 javaw.exe
    QString exePath;
    if (QFile::exists(javaPath))
        exePath = QDir::toNativeSeparators(QFileInfo(javaPath).absoluteFilePath());
    else if (QFile::exists(javawPath))
        exePath = QDir::toNativeSeparators(QFileInfo(javawPath).absoluteFilePath());
    else
        return;
#endif

    if (m_seenPaths.contains(exePath)) return;
    m_seenPaths.insert(exePath);

    JavaInstallInfo info;
    if (validateJavaPathDetailed(exePath, info)) {
        info.path = exePath;
        info.valid = true;
        outResults.append(info);
        qDebug() << "[JavaScanWorker] 发现 Java:" << info.version << info.arch << info.vendor << "@" << exePath;
    }
}

// ────────────────────────────────────────────────────────────
// 检测 Java 可执行文件的架构
// ────────────────────────────────────────────────────────────

QString JavaScanWorker::detectJavaArch(const QString& javaPath)
{
    QString lower = javaPath.toLower();

    // 从路径中推断架构
    if (lower.contains("x64") || lower.contains("x86_64") || lower.contains("amd64")) {
        return "x64";
    }
    if (lower.contains("x86") || lower.contains("i386") || lower.contains("i686")) {
        return "x86";
    }
    if (lower.contains("arm64") || lower.contains("aarch64")) {
        return "aarch64";
    }
    if (lower.contains("arm")) {
        return "arm";
    }

    // 尝试通过运行 Java 获取架构信息（较慢但准确）
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(javaPath, {"-XshowSettings:properties", "-version"});

    for (int i = 0; i < 10; i++) {
        if (isCancelled()) {
            process.kill();
            return "unknown";
        }
        if (process.waitForFinished(200))
            break;
    }

    if (process.state() != QProcess::NotRunning) {
        process.kill();
    }

    QByteArray output = process.readAll();
    QString outputStr = QString::fromLocal8Bit(output);

    // 解析 os.arch 属性
    static const QRegularExpression archRegex("os\\.arch[=:](\\S+)");
    QRegularExpressionMatch match = archRegex.match(outputStr);
    if (match.hasMatch()) {
        QString arch = match.captured(1).toLower();
        if (arch.contains("amd64") || arch.contains("x86_64")) return "x64";
        if (arch.contains("x86") || arch.contains("i386")) return "x86";
        if (arch.contains("aarch64") || arch.contains("arm64")) return "aarch64";
        if (arch.contains("arm")) return "arm";
    }

    return "unknown";
}

// ────────────────────────────────────────────────────────────
// 从环境变量快速查找 Java
// ────────────────────────────────────────────────────────────

void JavaScanWorker::scanEnvironmentVariables(QList<QPair<QString, QString>>& outResults)
{
    // ── 1. 检查 JAVA_HOME ──
    QString javaHome = QProcessEnvironment::systemEnvironment().value("JAVA_HOME");
    if (!javaHome.isEmpty()) {
        QDir jhDir(javaHome);
        if (jhDir.exists()) {
            // JAVA_HOME → bin/
            tryAddJava(jhDir.filePath("bin"), outResults);
            // 某些旧版 JDK 直接把 java.exe 放在 JAVA_HOME（jlink 产物）
            tryAddJava(javaHome, outResults);
        }
    }

    // ── 2. 检查 PATH ──
    QString pathEnv = QProcessEnvironment::systemEnvironment().value("PATH");
    if (!pathEnv.isEmpty()) {
#ifdef Q_OS_ANDROID
        QStringList paths = pathEnv.split(':', Qt::SkipEmptyParts);
#else
        QStringList paths = pathEnv.split(';', Qt::SkipEmptyParts);
#endif
        for (const QString& p : paths) {
            if (isCancelled()) break;
            QString trimmed = p.trimmed();
            if (trimmed.isEmpty()) continue;

            // 跳过明显不包含 java 的 PATH 条目
            QString lower = trimmed.toLower();
            if (lower.contains("python") || lower.contains("node") ||
                lower.contains("dotnet") || lower.contains("\\windows"))
                continue;

            tryAddJava(trimmed, outResults);
        }
    }

    int count = outResults.size();
    if (count > 0) {
        int dummyMB = (int)(m_totalDriveMB > INT_MAX ? INT_MAX : m_totalDriveMB);
        emit scanProgress(
            QStringLiteral("已从环境变量找到 %1 个 Java").arg(count),
            0, -1, 0, dummyMB, count
        );
    }
}

// ────────────────────────────────────────────────────────────
// 扫描 Windows 注册表中的 Java 安装信息
// ────────────────────────────────────────────────────────────

#ifdef Q_OS_WIN
void JavaScanWorker::scanRegistryKey(void* hKeyBase, const QString& keyName,
                                     const QString& javaHomeValueName,
                                     const QString& subkeySuffix,
                                     QList<QPair<QString, QString>>& outResults)
{
    HKEY hKeyBase_ = static_cast<HKEY>(hKeyBase);
    HKEY hKey;

    // 打开注册表键（支持枚举子键）
    if (RegOpenKeyExW(hKeyBase_, keyName.toStdWString().c_str(), 0,
                      KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hKey) != ERROR_SUCCESS) {
        return;
    }

    // 获取子键数量
    DWORD numSubKeys = 0;
    RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &numSubKeys,
                     nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    // 遍历所有子键（按版本号命名的子键）
    if (numSubKeys > 0) {
        WCHAR subKeyName[255];
        for (DWORD i = 0; i < numSubKeys; i++) {
            if (isCancelled()) break;

            DWORD subKeyNameSize = 255;
            DWORD retCode = RegEnumKeyExW(hKey, i, subKeyName, &subKeyNameSize,
                                          nullptr, nullptr, nullptr, nullptr);
            if (retCode != ERROR_SUCCESS) continue;

            QString newKeyName = keyName + "\\" + QString::fromWCharArray(subKeyName) + subkeySuffix;

            HKEY hNewKey;
            if (RegOpenKeyExW(hKeyBase_, newKeyName.toStdWString().c_str(), 0,
                              KEY_READ, &hNewKey) == ERROR_SUCCESS) {
                // 读取 JavaHome 或 Path 值
                DWORD valueSz = 0;
                if (RegQueryValueExW(hNewKey, javaHomeValueName.toStdWString().c_str(),
                                     nullptr, nullptr, nullptr, &valueSz) == ERROR_SUCCESS) {
                    WCHAR* value = new WCHAR[valueSz / sizeof(WCHAR)];
                    if (RegQueryValueExW(hNewKey, javaHomeValueName.toStdWString().c_str(),
                                         nullptr, nullptr, reinterpret_cast<BYTE*>(value),
                                         &valueSz) == ERROR_SUCCESS) {
                        QString javaHome = QString::fromWCharArray(value);
                        tryAddJava(javaHome + "/bin", outResults);
                    }
                    delete[] value;
                }
                RegCloseKey(hNewKey);
            }
        }
    }

    RegCloseKey(hKey);
}
#endif

void JavaScanWorker::scanRegistry(QList<QPair<QString, QString>>& outResults)
{
#ifdef Q_OS_ANDROID
    // Android: no registry support, skip
    Q_UNUSED(outResults);
    return;
#else
#ifdef Q_OS_WIN
    // 参考 PrismLauncher/MultiMC，使用原生 Windows API 扫描注册表
    // 支持 HKEY_LOCAL_MACHINE 和 HKEY_CURRENT_USER

    struct RegistryScanEntry {
        void* hKeyBase;
        QString keyName;
        QString javaHomeValueName;
        QString subkeySuffix;
    };

    // 所有需要扫描的注册表路径
    // 格式: {根键, 路径, JavaHome值名, 子键后缀}
    const QList<RegistryScanEntry> registryEntries = {
        // Oracle Java (旧版)
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\JavaSoft\\Java Runtime Environment", "JavaHome", ""},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\JavaSoft\\Java Development Kit", "JavaHome", ""},
        // Oracle Java 9+ (新版)
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\JavaSoft\\JRE", "JavaHome", ""},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\JavaSoft\\JDK", "JavaHome", ""},
        // WOW6432Node (32位)
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\JavaSoft\\Java Runtime Environment", "JavaHome", ""},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\JavaSoft\\Java Development Kit", "JavaHome", ""},
        // AdoptOpenJDK
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\AdoptOpenJDK\\JRE", "Path", "\\hotspot\\MSI"},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\AdoptOpenJDK\\JDK", "Path", "\\hotspot\\MSI"},
        // Eclipse Foundation
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Eclipse Foundation\\JDK", "Path", "\\hotspot\\MSI"},
        // Eclipse Adoptium (Temurin)
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Eclipse Adoptium\\JRE", "Path", "\\hotspot\\MSI"},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Eclipse Adoptium\\JDK", "Path", "\\hotspot\\MSI"},
        // IBM Semeru
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Semeru\\JRE", "Path", "\\openj9\\MSI"},
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Semeru\\JDK", "Path", "\\openj9\\MSI"},
        // Microsoft OpenJDK
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\JDK", "Path", "\\hotspot\\MSI"},
        // Azul Zulu
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Azul Systems\\Zulu", "InstallationPath", ""},
        // BellSoft Liberica
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\BellSoft\\Liberica", "InstallationPath", ""},
        // Amazon Corretto
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Amazon.com\\Amazon Corretto", "InstallationPath", ""},
        // Dragonwell
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Alibaba\\Dragonwell", "InstallationPath", ""},
        // SAP Machine
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\SAP\\SAP Machine JDK", "InstallationPath", ""},
        // GraalVM
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\GraalVM", "InstallationPath", ""},
        // HUAWEI JDK (毕昇)
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Huawei\\JDK", "InstallationPath", ""},
        // Tencent Kona
        {HKEY_LOCAL_MACHINE, "SOFTWARE\\Tencent\\Kona", "InstallationPath", ""},
    };

    // 同时扫描 HKEY_CURRENT_USER
    QList<RegistryScanEntry> currentUserEntries;
    for (const auto& entry : registryEntries) {
        currentUserEntries.append({HKEY_CURRENT_USER, entry.keyName, entry.javaHomeValueName, entry.subkeySuffix});
    }

    // 执行扫描
    for (const auto& entry : registryEntries + currentUserEntries) {
        if (isCancelled()) return;
        scanRegistryKey(entry.hKeyBase, entry.keyName, entry.javaHomeValueName,
                        entry.subkeySuffix, outResults);
    }

    int count = outResults.size();
    qDebug() << "[JavaScanWorker] 注册表扫描完成，累计发现" << count << "个 Java";
#endif
#endif
}

// ────────────────────────────────────────────────────────────
// 扫描已知厂商安装目录
// ────────────────────────────────────────────────────────────

void JavaScanWorker::scanVendorDirectories(QList<QPair<QString, QString>>& outResults)
{
#ifdef Q_OS_ANDROID
    // Android: scan common Java paths
    const QStringList androidPaths = {
        "/system/bin",
        "/data/data/com.termux/files/usr/bin",
        "/storage/emulated/0/Android/data",
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/BlockBox/java",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/java",
    };

    for (const QString& path : androidPaths) {
        if (isCancelled()) return;
        QDir dir(path);
        if (!dir.exists()) continue;

        // Check if java exists directly
        tryAddJava(path, outResults);

        // Check subdirectories
        const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& subdir : subdirs) {
            if (isCancelled()) return;
            tryAddJava(subdir.absoluteFilePath(), outResults);
            tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
        }
    }
#elif defined(Q_OS_WIN)
    // Windows: 已知 JDK 厂商在 Program Files 下的安装目录名
    // 参考 PrismLauncher/MultiMC/HMCL 的已知厂商目录列表
    const QStringList vendorDirs = {
        "Java", "BellSoft", "AdoptOpenJDK", "Eclipse Foundation",
        "Eclipse Adoptium", "Zulu", "Microsoft", "Semeru",
        "Amazon Corretto", "Azul", "JavaFX",
        "GraalVM", "Huawei", "Tencent", "SAP",
        "Dragonwell", "Kona", "Bisheng"
    };

    const QStringList programFilesRoots = {
        "C:/Program Files",
        "C:/Program Files (x86)"
    };

    for (const QString& root : programFilesRoots) {
        if (isCancelled()) return;
        QDir rootDir(root);
        if (!rootDir.exists()) continue;

        for (const QString& vendor : vendorDirs) {
            if (isCancelled()) return;
            QDir vendorDir(root + "/" + vendor);
            if (!vendorDir.exists()) continue;

            // 厂商目录下通常是版本号命名的子目录，每个子目录里有 bin/java.exe
            const QFileInfoList subdirs = vendorDir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }

            // 某些厂商直接把 bin/ 放在 vendor/ 下
            tryAddJava(vendorDir.absolutePath() + "/bin", outResults);
        }
    }

    // 用户目录下的 .jdks （IntelliJ IDEA 默认路径）
    QString userHome = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!userHome.isEmpty()) {
        QDir jdksDir(userHome + "/.jdks");
        if (jdksDir.exists()) {
            const QFileInfoList subdirs = jdksDir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }
        }
    }
#elif defined(Q_OS_MACOS)
    // macOS: 参考 PrismLauncher/MultiMC 的 macOS Java 检测路径
    const QStringList macJvmPaths = {
        "/Library/Java/JavaVirtualMachines",
        "/System/Library/Java/JavaVirtualMachines",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/Library/Java/JavaVirtualMachines",
        // Xcode 附带的 Java
        "/Applications/Xcode.app/Contents/Applications/Application Loader.app/Contents/MacOS/itms/java/bin",
        // 旧版 Java 插件
        "/Library/Internet Plug-Ins/JavaAppletPlugin.plugin/Contents/Home/bin",
        // JavaVM.framework
        "/System/Library/Frameworks/JavaVM.framework/Versions/Current/Commands",
    };

    for (const QString& jvmPath : macJvmPaths) {
        if (isCancelled()) return;
        QDir dir(jvmPath);
        if (!dir.exists()) continue;

        const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& subdir : subdirs) {
            if (isCancelled()) return;
            // macOS JVM 结构: <name>.jdk/Contents/Home/bin/java
            tryAddJava(subdir.absoluteFilePath() + "/Contents/Home/bin", outResults);
            // JRE 变体
            tryAddJava(subdir.absoluteFilePath() + "/Contents/Home/jre/bin", outResults);
            // Commands 目录
            tryAddJava(subdir.absoluteFilePath() + "/Contents/Commands", outResults);
        }
    }

    // SDKMAN (macOS/Linux)
    QString userHome = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!userHome.isEmpty()) {
        QString sdkmanDir = qEnvironmentVariable("SDKMAN_DIR", userHome + "/.sdkman");
        QDir sdkmanJavaDir(sdkmanDir + "/candidates/java");
        if (sdkmanJavaDir.exists()) {
            const QFileInfoList subdirs = sdkmanJavaDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }
        }

        // asdf 版本管理器
        QString asdfDataDir = qEnvironmentVariable("ASDF_DATA_DIR", userHome + "/.asdf");
        QDir asdfJavaDir(asdfDataDir + "/installs/java");
        if (asdfJavaDir.exists()) {
            const QFileInfoList subdirs = asdfJavaDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }
        }
    }
#elif defined(Q_OS_LINUX)
    // Linux: 参考 PrismLauncher/MultiMC 的 Linux Java 检测路径
    const QStringList linuxJvmPaths = {
        "/usr/java",                     // Oracle RPMs
        "/usr/lib/jvm",                  // 通用发行版路径
        "/usr/lib64/jvm",               // 64位
        "/usr/lib32/jvm",               // 32位
        "/opt/jdk",                      // 手动安装
        "/opt/jdks",
        "/opt/ibm",                      // IBM Semeru
        "/app/jdk",                      // Flatpak
    };

    for (const QString& jvmPath : linuxJvmPaths) {
        if (isCancelled()) return;
        QDir dir(jvmPath);
        if (!dir.exists()) continue;

        const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& subdir : subdirs) {
            if (isCancelled()) return;
            // Linux 通常直接有 bin/ 目录
            tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            // JRE 变体
            tryAddJava(subdir.absoluteFilePath() + "/jre/bin", outResults);
        }
    }

    // SDKMAN (macOS/Linux)
    QString userHome = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!userHome.isEmpty()) {
        QString sdkmanDir = qEnvironmentVariable("SDKMAN_DIR", userHome + "/.sdkman");
        QDir sdkmanJavaDir(sdkmanDir + "/candidates/java");
        if (sdkmanJavaDir.exists()) {
            const QFileInfoList subdirs = sdkmanJavaDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }
        }

        // asdf 版本管理器
        QString asdfDataDir = qEnvironmentVariable("ASDF_DATA_DIR", userHome + "/.asdf");
        QDir asdfJavaDir(asdfDataDir + "/installs/java");
        if (asdfJavaDir.exists()) {
            const QFileInfoList subdirs = asdfJavaDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }
        }

        // IntelliJ IDEA 下载的 JDK
        QDir jdksDir(userHome + "/.jdks");
        if (jdksDir.exists()) {
            const QFileInfoList subdirs = jdksDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }
        }

        // Gradle 工具链
        QString gradleUserHome = qEnvironmentVariable("GRADLE_USER_HOME", userHome + "/.gradle");
        QDir gradleJdksDir(gradleUserHome + "/jdks");
        if (gradleJdksDir.exists()) {
            const QFileInfoList subdirs = gradleJdksDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }
        }
    }
#endif

    qDebug() << "[JavaScanWorker] 厂商目录扫描完成，累计发现" << outResults.size() << "个 Java";
}

// ────────────────────────────────────────────────────────────
// 扫描 Minecraft 启动器自带的 Java 运行时
// ────────────────────────────────────────────────────────────

void JavaScanWorker::scanMinecraftRuntime(QList<QPair<QString, QString>>& outResults)
{
    // Minecraft 官方启动器在以下位置预装 Java 运行时:
    //   %LOCALAPPDATA%/Packages/.../LocalCache/Local/runtime
    //   %ProgramFiles(x86)%/Minecraft Launcher/runtime
    //   %APPDATA%/.minecraft/runtime
    QStringList runtimeRoots;

    QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!localAppData.isEmpty()) {
        runtimeRoots << localAppData + "/Packages/Microsoft.4297127D64EC6_8wekyb3d8bbwe/LocalCache/Local/runtime";
    }

    QString programFilesX86 = QProcessEnvironment::systemEnvironment().value("ProgramFiles(x86)");
    if (!programFilesX86.isEmpty()) {
        runtimeRoots << programFilesX86 + "/Minecraft Launcher/runtime";
    }

    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        // .minecraft 通常在 %APPDATA%\.minecraft
        QDir adDir(appData);
        adDir.cdUp();
        runtimeRoots << adDir.absolutePath() + "/.minecraft/runtime";
    }

    for (const QString& root : runtimeRoots) {
        if (isCancelled()) return;
        QDir rootDir(root);
        if (!rootDir.exists()) continue;

        // runtime/<runtime-name>/<platform>/<version>/bin/java.exe
        const QFileInfoList runtimeNames = rootDir.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& runtimeName : runtimeNames) {
            if (isCancelled()) return;
            QDir runtimeDir(runtimeName.absoluteFilePath());

            // 可能直接是 platform 子目录，也可能多一层 version
            const QFileInfoList platforms = runtimeDir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& platform : platforms) {
                if (isCancelled()) return;
                QDir platformDir(platform.absoluteFilePath());

                // platform/<version>/bin/java.exe
                const QFileInfoList versions = platformDir.entryInfoList(
                    QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QFileInfo& version : versions) {
                    if (isCancelled()) return;
                    tryAddJava(version.absoluteFilePath() + "/bin", outResults);
                }

                // 兜底：platform/bin/java.exe
                tryAddJava(platform.absoluteFilePath() + "/bin", outResults);
            }
        }
    }

    qDebug() << "[JavaScanWorker] Minecraft 运行时扫描完成，累计发现" << outResults.size() << "个 Java";
}

// ────────────────────────────────────────────────────────────
// 主入口
// ────────────────────────────────────────────────────────────

void JavaScanWorker::doScan()
{
    m_cancelled.storeRelease(0);
    m_seenPaths.clear();
    m_lastPercent = 0;
    m_totalDriveMB = 0;
    m_scanStartTime = QDateTime::currentMSecsSinceEpoch();

    // ── 统计盘符 + 总容量 ──
    struct DriveInfo {
        QString drive;
        qint64  bytes;
    };
    QList<DriveInfo> drives;
    qint64 totalBytes = 0;

    for (const QString& d : m_drives) {
        QStorageInfo si(d);
        if (si.isValid() && si.bytesTotal() > 0) {
            drives.append({d, si.bytesTotal()});
            totalBytes += si.bytesTotal();
        }
    }

    if (drives.isEmpty()) {
        emit scanFinished({});
        return;
    }

    m_totalDriveMB = totalBytes / (1024 * 1024);
    if (m_totalDriveMB <= 0) m_totalDriveMB = 1;

    int totalMB = (int)(m_totalDriveMB > INT_MAX ? INT_MAX : m_totalDriveMB);
    QList<QPair<QString, QString>> allResults;

    // ── 阶段 1：快速路径扫描（注册表、环境变量、厂商目录、Minecraft 运行时）──
    // 这些路径几乎覆盖 95% 的用户场景，可在数秒内完成
    emit scanProgress(
        QStringLiteral("正在扫描注册表与环境变量..."),
        0, -1, 0, totalMB, 0
    );

    scanEnvironmentVariables(allResults);
    scanRegistry(allResults);
    scanVendorDirectories(allResults);
    scanMinecraftRuntime(allResults);

    int totalFound = allResults.size();
    emit scanProgress(
        QStringLiteral("快速扫描完成，已发现 %1 个 Java，正在全盘深度搜索...").arg(totalFound),
        5, -1, 0, totalMB, totalFound
    );

    // ── 阶段 2：全盘 BFS（关键词剪枝）—— 作为兜底，捕获用户放在自定义路径的 Java ──
    qint64 cumulativeBytes = 0;
    int    totalDrives = drives.size();

    for (int di = 0; di < totalDrives; di++) {
        if (isCancelled()) break;

        const DriveInfo& info = drives[di];

        // 当前盘占进度条的份额（从 5% 开始，前 5% 留给快速扫描）
        int driveBasePercent = 5 + (int)(cumulativeBytes * 95 / totalBytes);
        int drivePercentShare = (int)(info.bytes * 95 / totalBytes);

        // 发射"扫描某盘"提示
        int scannedMB = (int)(cumulativeBytes / (1024 * 1024));
        emit scanProgress(
            QStringLiteral("正在搜索 %1 盘...").arg(info.drive),
            driveBasePercent, -1, scannedMB, totalMB, totalFound
        );

        QList<QPair<QString, QString>> driveResults;
        int found = scanDrive(info.drive, info.bytes,
                              cumulativeBytes, driveBasePercent,
                              drivePercentShare, totalFound, driveResults);
        totalFound += found;
        allResults.append(driveResults);

        cumulativeBytes += info.bytes;

        if (isCancelled()) break;

        // 当前盘完成 → 发射进度到该盘段的 100%
        int endPercent = qMin(99, 5 + (int)(cumulativeBytes * 95 / totalBytes));
        scannedMB = (int)(cumulativeBytes / (1024 * 1024));
        emit scanProgress(
            QStringLiteral("%1 盘搜索完成 (发现 %2 个)").arg(info.drive).arg(found),
            endPercent, -1, scannedMB, totalMB, totalFound
        );
    }

    // ── 完成 ──
    if (!isCancelled()) {
        emit scanProgress(QStringLiteral("扫描完成"), 100, 0, totalMB, totalMB, totalFound);
    }
    emit scanFinished(allResults);
}

// ────────────────────────────────────────────────────────────
// 单盘扫描
// ────────────────────────────────────────────────────────────

int JavaScanWorker::scanDrive(const QString& drive, qint64 driveBytes,
                               qint64& cumulativeBytes, int& driveBasePercent,
                               int drivePercentShare, int& totalFound,
                               QList<QPair<QString, QString>>& outResults)
{
    qint64 driveStart = QDateTime::currentMSecsSinceEpoch();

    int totalMB   = (int)(m_totalDriveMB > INT_MAX ? INT_MAX : m_totalDriveMB);
    int fileCount = 0;
    int found     = 0;
    int lastEmit  = 0;
    qint64 lastEmitTime = driveStart;

    // —— 基于盘大小的粗略时间估算 (用于进度条动画) ——
    // 假设 2000 MB/s 的扫描速率
    qint64 estimatedDriveMs = qMax((qint64)2000, driveBytes / (2000LL * 1024));

    // BFS 目录队列，避免 QDirIterator::hasNext() 在巨量文件下阻塞取消
    // 队列元素：pair<目录路径, 当前深度>
    QQueue<QPair<QString, int>> dirQueue;
    dirQueue.enqueue(qMakePair(drive, 0));

    while (!dirQueue.isEmpty()) {
        if (isCancelled()) break;

        auto head = dirQueue.dequeue();
        QString dirPath = head.first;
        int depth = head.second;
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        // 目录级排除（加 / 让 isPathExcluded 能匹配到尾部）
        if (isPathExcluded(dirPath + QStringLiteral("/"))) continue;

        // 列出当前目录下的 java.exe / javaw.exe（纯文件名匹配，无 stat）
        const QStringList javaFiles = dir.entryList(
            QStringList() << QStringLiteral("java.exe") << QStringLiteral("javaw.exe"),
            QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);

        for (const QString& fileName : javaFiles) {
            fileCount++;
            QString filePath = dir.absoluteFilePath(fileName);

            // 进度发射（每 3 个文件或每 800ms）
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (fileCount - lastEmit >= 3 || now - lastEmitTime >= 800) {
                lastEmit = fileCount;
                lastEmitTime = now;

                qint64 elapsed = now - driveStart;
                int drivePercent = qBound(0, (int)(elapsed * 100 / estimatedDriveMs), 99);
                int overallPercent = driveBasePercent + drivePercentShare * drivePercent / 100;
                overallPercent = qBound(driveBasePercent, overallPercent, 99);
                if (overallPercent < m_lastPercent) overallPercent = m_lastPercent;
                m_lastPercent = overallPercent;

                qint64 cumBytes = cumulativeBytes + driveBytes * drivePercent / 100;
                int scannedMB = (int)(cumBytes / (1024 * 1024));

                int eta = -1;
                if (overallPercent > 0) {
                    qint64 totalElapsed = now - m_scanStartTime;
                    qint64 totalEst = totalElapsed * 100 / overallPercent;
                    eta = qMax(0, (int)((totalEst - totalElapsed) / 60000));
                }

                emit scanProgress(filePath, overallPercent, eta, scannedMB, totalMB, totalFound);
            }

            // 去重
            QString absPath = QDir::toNativeSeparators(filePath);
            if (m_seenPaths.contains(absPath)) continue;
            m_seenPaths.insert(absPath);

            if (isCancelled()) break;

            QString version;
            if (validateJavaPath(absPath, version)) {
                outResults.append(qMakePair(absPath, version));
                found++;
                totalFound++;
                qDebug() << "[JavaScanWorker] 发现 Java:" << version << "@" << absPath;
            }
        }

        if (isCancelled()) break;

        // 最大深度 8 层，避免无限递归
        if (depth >= 8) continue;

        // 列出子目录，按关键词剪枝（参考 PCL-CE/ProjBobcat）
        // 仅当目录名包含 Java 相关关键词或形如版本号时才入队
        // 这样可以跳过 99% 不相关的目录，大幅加速扫描
        const QStringList subdirs = dir.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot);

        for (const QString& name : subdirs) {
            if (isJavaRelatedDirectory(name) || isVersionLikeDirectory(name)) {
                dirQueue.enqueue(qMakePair(dir.absoluteFilePath(name), depth + 1));
            }
        }
    }

    return found;
}

// ────────────────────────────────────────────────────────────
// 关键词剪枝
// ────────────────────────────────────────────────────────────

bool JavaScanWorker::isJavaRelatedDirectory(const QString& dirName) const
{
    if (dirName.isEmpty()) return false;
    QString lower = dirName.toLower();

    // 排除常见无关目录名
    if (lower == "temp" || lower == "tmp" || lower == "cache" ||
        lower == "logs" || lower == "crashdumps" || lower == "node_modules" ||
        lower == "__pycache__" || lower == ".git" || lower == ".svn" ||
        lower == "site-packages" || lower == "vendor" || lower == "venv") {
        return false;
    }

    for (const QString& kw : m_keywords) {
        if (lower.contains(kw)) {
            return true;
        }
    }
    return false;
}

bool JavaScanWorker::isVersionLikeDirectory(const QString& dirName) const
{
    if (dirName.isEmpty()) return false;

    // 形如 1.8.0_301 / 17.0.1 / 21 / jdk-17 等
    // 至少包含一个数字，并且以数字开头或包含 .数字
    static const QRegularExpression versionLike(
        QStringLiteral("^[0-9]+(\\.[0-9]+)*(_[0-9]+)?$"));
    if (versionLike.match(dirName).hasMatch()) {
        return true;
    }

    // jdk-17 / jre-1.8 / jdk11 这种
    static const QRegularExpression jdkPrefix(
        QStringLiteral("^(jdk|jre)[-_]?[0-9].*"),
        QRegularExpression::CaseInsensitiveOption);
    if (jdkPrefix.match(dirName).hasMatch()) {
        return true;
    }

    return false;
}

// ────────────────────────────────────────────────────────────
// Java 验证
// ────────────────────────────────────────────────────────────

bool JavaScanWorker::validateJavaPath(const QString& path, QString& outVersion)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(path, {"-version"});

    // 每 100ms 轮询一次，让取消信号可以及时中断
    for (int i = 0; i < 20; i++) {
        if (isCancelled()) {
            process.kill();
            return false;
        }
        if (process.waitForFinished(100))
            break;
    }

    if (process.state() != QProcess::NotRunning)
        return false;

    {
        QByteArray output = process.readAll();
        QString outputStr = QString::fromLocal8Bit(output);

        static const QRegularExpression re("version \"([0-9]+(\\.[0-9]+)*(_[0-9]+)?)");
        QRegularExpressionMatch m = re.match(outputStr);
        if (m.hasMatch()) {
            outVersion = m.captured(1);
            return true;
        }
    }
    return false;
}

// ────────────────────────────────────────────────────────────
// Java 验证（详细版本，包含架构、厂商等信息）
// 参考 PrismLauncher/MultiMC 的 JavaInstall 结构
// ────────────────────────────────────────────────────────────

bool JavaScanWorker::validateJavaPathDetailed(const QString& path, JavaInstallInfo& outInfo)
{
    outInfo.path = path;
    outInfo.valid = false;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(path, {"-version"});

    // 每 100ms 轮询一次，让取消信号可以及时中断
    for (int i = 0; i < 20; i++) {
        if (isCancelled()) {
            process.kill();
            return false;
        }
        if (process.waitForFinished(100))
            break;
    }

    if (process.state() != QProcess::NotRunning)
        return false;

    QByteArray output = process.readAll();
    QString outputStr = QString::fromLocal8Bit(output);

    // 解析版本号
    // 支持格式: "1.8.0_301", "17.0.1", "21.0.2"
    static const QRegularExpression versionRegex("version \"([0-9]+(\\.[0-9]+)*(_[0-9]+)?)\"");
    QRegularExpressionMatch versionMatch = versionRegex.match(outputStr);
    if (!versionMatch.hasMatch()) {
        return false;
    }
    outInfo.version = versionMatch.captured(1);

    // 解析厂商信息
    // OpenJDK 格式: "OpenJDK Runtime Environment (build 17.0.1+12-39)"
    // Oracle 格式: "Java(TM) SE Runtime Environment (build 17.0.1+12-LTS-39)"
    static const QRegularExpression vendorRegex("(OpenJDK|Java\\(TM\\)|Oracle|Eclipse Adoptium|Azul|BellSoft|Amazon|Microsoft|IBM|SAP|GraalVM|Huawei|Tencent)");
    QRegularExpressionMatch vendorMatch = vendorRegex.match(outputStr);
    if (vendorMatch.hasMatch()) {
        QString vendorRaw = vendorMatch.captured(1);
        if (vendorRaw == "OpenJDK") {
            // 进一步区分 OpenJDK 的具体发行版
            if (outputStr.contains("Eclipse Adoptium") || outputStr.contains("Temurin")) {
                outInfo.vendor = "Eclipse Adoptium";
            } else if (outputStr.contains("Amazon")) {
                outInfo.vendor = "Amazon Corretto";
            } else if (outputStr.contains("Microsoft")) {
                outInfo.vendor = "Microsoft";
            } else if (outputStr.contains("Azul") || outputStr.contains("Zulu")) {
                outInfo.vendor = "Azul Zulu";
            } else if (outputStr.contains("BellSoft") || outputStr.contains("Liberica")) {
                outInfo.vendor = "BellSoft Liberica";
            } else if (outputStr.contains("SAP") || outputStr.contains("SAP Machine")) {
                outInfo.vendor = "SAP Machine";
            } else if (outputStr.contains("GraalVM")) {
                outInfo.vendor = "GraalVM";
            } else if (outputStr.contains("Huawei") || outputStr.contains("Bisheng")) {
                outInfo.vendor = "Huawei Bisheng";
            } else if (outputStr.contains("Tencent") || outputStr.contains("Kona")) {
                outInfo.vendor = "Tencent Kona";
            } else if (outputStr.contains("Alibaba") || outputStr.contains("Dragonwell")) {
                outInfo.vendor = "Alibaba Dragonwell";
            } else {
                outInfo.vendor = "OpenJDK";
            }
        } else if (vendorRaw == "Java(TM)") {
            outInfo.vendor = "Oracle";
        } else {
            outInfo.vendor = vendorRaw;
        }
    } else {
        outInfo.vendor = "Unknown";
    }

    // 检测架构
    outInfo.arch = detectJavaArch(path);

    // 从路径推断 JAVA_HOME
    QString pathLower = path.toLower();
    if (pathLower.contains("/bin/java")) {
        outInfo.javaHome = QFileInfo(path).absolutePath();  // 去掉 /bin
        outInfo.javaHome = QFileInfo(outInfo.javaHome).absolutePath();  // 再去掉一层可能的 /jre
    } else if (pathLower.contains("\\bin\\java")) {
        outInfo.javaHome = QFileInfo(path).absolutePath();
        outInfo.javaHome = QFileInfo(outInfo.javaHome).absolutePath();
    }

    outInfo.valid = true;
    return true;
}

// ────────────────────────────────────────────────────────────
// 路径排除
// ────────────────────────────────────────────────────────────

bool JavaScanWorker::isPathExcluded(const QString& filePath) const
{
    QString lower = filePath.toLower();
    for (const QString& excl : m_excludeLower) {
        if (lower.contains("/" + excl + "/") || lower.contains("\\" + excl + "\\"))
            return true;
    }
    return false;
}
