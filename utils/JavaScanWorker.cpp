/**
 * @file   JavaScanWorker.cpp
 * @brief  Java 检测后台线程工作类（注册表 + 厂商目录 + 启动器运行时 + 黑名单剪枝全盘 BFS）
 * @author BlockBox Team
 * @date   2026-05-22
 */

#include "utils/JavaScanWorker.h"

#include <QCoreApplication>
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

// 全盘深扫的最大目录深度（防止病态嵌套拖垮扫描）
static const int kMaxScanDepth = 12;

// 顶层自由深入的深度：前 3 层不管目录名是什么都进入。
// 用户常把启动器/JDK 放在中文或自定义命名的目录下（如 "D:\系统迁移勿删\.hmcl"），
// 这些目录名不含任何 Java 关键词，关键词白名单剪枝会把它们整棵误剪掉；
// 顶层放开 + 深层再剪枝，能同时保住覆盖面与速度
static const int kFreeDescendDepth = 3;

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
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
    // 移动端：扫描系统与沙盒内的固定目录（鸿蒙同样是 musl/ohos 布局，
    // 可执行文件名是 java 而不是 java.exe）
    m_drives << "/system/bin" << "/data/data" << "/storage/emulated/0";
#else
    // 桌面端：动态枚举本机所有就绪的盘符（不再硬编码 C/D/E，
    // Java 装在 F 盘、G 盘等也能扫到；空光驱/未就绪的可移动盘会被跳过）
    const QFileInfoList rootDrives = QDir::drives();
    for (const QFileInfo& root : rootDrives) {
        const QString path = root.absoluteFilePath();  // 形如 "C:/"
        QStorageInfo si(path);
        if (!si.isValid() || !si.isReady() || si.bytesTotal() <= 0)
            continue;
        m_drives << path;
    }
    if (m_drives.isEmpty())
        m_drives << QStringLiteral("C:/");
#endif

    // 全盘深扫黑名单（按目录名精确匹配，命中即整棵跳过）：
    // 与"关键词白名单"相反的黑名单策略——除明确无关的大目录外全部深入，
    // 保证自定义命名目录（中文文件夹、任意层级）里的 Java 不被误剪
    m_excludedDirNames
        << "windows" << "windows.old" << "$recycle.bin" << "system volume information"
        << "msocache" << "perflogs" << "recovery" << "programdata"
        << "config.msi" << "windowsapps" << "steamapps"
        << "node_modules" << ".git" << ".svn" << ".hg"
        << "__pycache__" << "site-packages"
        << "temp" << "tmp" << "cache" << "caches" << ".cache"
        << "logs" << "crashdumps" << "vendor" << "venv" << ".venv"
        << "target";

    // Java 相关关键词，用于深扫时深层 BFS 剪枝（目录名包含这些词才继续深入）
    // 综合 PCL-CE / ProjBobcat 的关键词集合
    m_keywords
        << "java" << "jdk" << "jre" << "jvm"
        << "oracle" << "openjdk" << "adoptium" << "adoptopenjdk"
        << "temurin" << "zulu" << "azul" << "bellsoft" << "liberica"
        << "microsoft" << "semeru" << "corretto" << "amazon"
        << "eclipse" << "kona" << "dragonwell" << "sapmachine"
        << "graalvm" << "bisheng"
        << "runtime" << "environment" << "env"
        << "x64" << "x86_64" << "amd64" << "arm64" << "aarch64"
        << "minecraft" << "mojang" << "mc" << "game"
        << "hmcl" << "pcl" << "baka" << "blockbox"
        << "jdks" << "javanw" << "javapath"
        // 深度搜索需要逐层进入的系统/用户目录名（Java 常装在这些层级之下）
        << "program files" << "users" << "appdata" << "programs"
        << "scoop" << "gradle" << "jetbrains" << "toolbox" << "jbr"
        // JDK 的 bin 目录与其他工具的安装结构（bin/apps/current 层）
        << "bin" << "apps" << "current";
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
// 统一快速检测：环境变量 → 注册表 → 厂商目录 → 启动器运行时
// 参考 HMCL JavaRuntimeEnvironment / PrismLauncher MultiMC 的 Java 探测顺序，
// 覆盖绝大多数真实安装场景且不遍历磁盘，可同步调用
// ────────────────────────────────────────────────────────────

QList<QPair<QString, QString>> JavaScanWorker::runQuickScan()
{
    m_cancelled.storeRelease(0);
    m_seenPaths.clear();

    QList<QPair<QString, QString>> results;
    scanEnvironmentVariables(results);
    if (isCancelled()) return results;
    scanRegistry(results);
    if (isCancelled()) return results;
    scanVendorDirectories(results);
    if (isCancelled()) return results;
    scanLauncherRuntimes(results);
    return results;
}

// ────────────────────────────────────────────────────────────
// 通用：尝试加入一个 java.exe / javaw.exe
// ────────────────────────────────────────────────────────────

void JavaScanWorker::tryAddJava(const QString& dirPath, QList<QPair<QString, QString>>& outResults)
{
    if (isCancelled()) return;
    if (dirPath.isEmpty() || dirPath == "/" || dirPath == "\\") return;

#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
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

#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
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
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
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
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
    // 移动端：没有注册表，直接跳过
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
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
    // 移动端：扫描常见 Java 路径（含启动器一键安装的运行依赖目录）
    const QStringList mobilePaths = {
        "/system/bin",
        "/data/data/com.termux/files/usr/bin",
        "/storage/emulated/0/Android/data",
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/BlockBox/java",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/java",
    };

    for (const QString& path : mobilePaths) {
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

    // Program Files 根目录：环境变量优先，缺失时回退硬编码；
    // 同时覆盖各固定盘的 Program Files（Java 常被装到非 C 盘）
    const QProcessEnvironment sysEnv = QProcessEnvironment::systemEnvironment();
    QStringList programFilesRoots;
    for (const QString& envKey : {QStringLiteral("ProgramFiles"),
                                  QStringLiteral("ProgramFiles(x86)")}) {
        const QString v = sysEnv.value(envKey).trimmed();
        if (!v.isEmpty()) programFilesRoots << v;
    }
    programFilesRoots << QStringLiteral("C:/Program Files")
                      << QStringLiteral("C:/Program Files (x86)");
    for (const QString& drive : m_drives) {
        programFilesRoots << (drive + QStringLiteral("Program Files"))
                          << (drive + QStringLiteral("Program Files (x86)"));
    }

    // 去重
    QSet<QString> seenRoots;
    QStringList uniqueRoots;
    for (const QString& r : programFilesRoots) {
        QString norm = QDir::fromNativeSeparators(r);
        while (norm.endsWith(QLatin1Char('/'))) norm.chop(1);
        if (norm.isEmpty() || seenRoots.contains(norm)) continue;
        seenRoots.insert(norm);
        uniqueRoots << norm;
    }

    for (const QString& root : uniqueRoots) {
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

    QString userHome = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!userHome.isEmpty()) {
        // IntelliJ IDEA 默认下载路径 ~/.jdks/<id>/bin
        QDir jdksDir(userHome + "/.jdks");
        if (jdksDir.exists()) {
            const QFileInfoList subdirs = jdksDir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }
        }

        // Scoop 包管理器 ~/scoop/apps/<app>/current/bin
        QDir scoopApps(userHome + "/scoop/apps");
        if (scoopApps.exists()) {
            const QFileInfoList subdirs = scoopApps.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/current/bin", outResults);
            }
        }

        // Gradle 工具链自动下载的 JDK %GRADLE_USER_HOME%/jdks/<id>/bin
        QString gradleUserHome = sysEnv.value("GRADLE_USER_HOME");
        if (gradleUserHome.isEmpty()) gradleUserHome = userHome + "/.gradle";
        QDir gradleJdksDir(gradleUserHome + "/jdks");
        if (gradleJdksDir.exists()) {
            const QFileInfoList subdirs = gradleJdksDir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }
        }
    }

    // JetBrains Toolbox 下载的 IDE 自带运行时 JBR
    QString localAppData = sysEnv.value("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        QDir toolboxApps(localAppData + "/JetBrains/Toolbox/apps");
        if (toolboxApps.exists()) {
            const QFileInfoList apps = toolboxApps.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& app : apps) {
                if (isCancelled()) return;
                // apps/<ide>/jbr/bin 或 apps/<ide>/<ver>/jbr/bin
                tryAddJava(app.absoluteFilePath() + "/jbr/bin", outResults);
                const QFileInfoList vers = QDir(app.absoluteFilePath()).entryInfoList(
                    QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QFileInfo& ver : vers) {
                    if (isCancelled()) return;
                    tryAddJava(ver.absoluteFilePath() + "/jbr/bin", outResults);
                }
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
            const QFileInfoList subdirs = jdksDir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs) {
                if (isCancelled()) return;
                tryAddJava(subdir.absoluteFilePath() + "/bin", outResults);
            }
        }

        // Gradle 工具链
        QString gradleUserHome = qEnvironmentVariable("GRADLE_USER_HOME", userHome + "/.gradle");
        QDir gradleJdksDir(gradleUserHome + "/jdks");
        if (gradleJdksDir.exists()) {
            const QFileInfoList subdirs = gradleJdksDir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
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
// 扫描各启动器自带的 Java 运行时
//   覆盖：官方启动器（商店版/桌面版）、HMCL、.minecraft（含各盘根与便携目录）
// ────────────────────────────────────────────────────────────

void JavaScanWorker::scanRuntimeRoot(const QString& runtimeRoot,
                                     QList<QPair<QString, QString>>& outResults)
{
    QDir rootDir(runtimeRoot);
    if (!rootDir.exists()) return;

    // 兼容三种目录层级（不同启动器下载的 Mojang 运行时布局不一）：
    //   1. runtime/<名称>/bin                    —— HMCL / PCL 等第三方启动器（平铺）
    //   2. runtime/<名称>/<平台>/bin
    //   3. runtime/<名称>/<平台>/<版本>/bin       —— 官方启动器
    const QFileInfoList runtimeNames = rootDir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& runtimeName : runtimeNames) {
        if (isCancelled()) return;

        tryAddJava(runtimeName.absoluteFilePath() + "/bin", outResults);

        const QFileInfoList platforms = QDir(runtimeName.absoluteFilePath()).entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& platform : platforms) {
            if (isCancelled()) return;

            tryAddJava(platform.absoluteFilePath() + "/bin", outResults);

            const QFileInfoList versions = QDir(platform.absoluteFilePath()).entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& version : versions) {
                if (isCancelled()) return;
                tryAddJava(version.absoluteFilePath() + "/bin", outResults);
            }
        }
    }
}

void JavaScanWorker::scanJavaLibraryDir(const QString& javaDir,
                                        QList<QPair<QString, QString>>& outResults)
{
    QDir dir(javaDir);
    if (!dir.exists()) return;

    // 兼容两种布局：
    //   <根>/<id>/bin                 —— 方块盒子 / HMCL 旧版
    //   <根>/<平台>/<运行时>/bin      —— HMCL（windows-x86_64/mojang-java-runtime-*）
    const QFileInfoList subs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& sub : subs) {
        if (isCancelled()) return;

        tryAddJava(sub.absoluteFilePath() + "/bin", outResults);

        const QFileInfoList subs2 = QDir(sub.absoluteFilePath()).entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& sub2 : subs2) {
            if (isCancelled()) return;
            tryAddJava(sub2.absoluteFilePath() + "/bin", outResults);
        }
    }
}

void JavaScanWorker::scanLauncherRuntimes(QList<QPair<QString, QString>>& outResults)
{
    const QProcessEnvironment sysEnv = QProcessEnvironment::systemEnvironment();
    const QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString userHome = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString appDir = QCoreApplication::applicationDirPath();

    // %APPDATA%（Roaming）：AppDataLocation 形如 .../Roaming/<应用名>，上退一级
    QString roaming;
    const QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataLoc.isEmpty()) {
        QDir adDir(appDataLoc);
        if (adDir.cdUp())
            roaming = QDir(adDir.absolutePath()).absolutePath();
    }

    // ── 1. .minecraft/runtime（官方与第三方启动器都会在此放运行时）──
    QStringList runtimeRoots;
    // 官方启动器 商店版：LocalCache/Local/runtime
    if (!localAppData.isEmpty())
        runtimeRoots << localAppData + "/Packages/Microsoft.4297127D64EC6_8wekyb3d8bbwe/LocalCache/Local/runtime";
    // 官方启动器 桌面版
    const QString pfX86 = sysEnv.value("ProgramFiles(x86)");
    if (!pfX86.isEmpty())
        runtimeRoots << pfX86 + "/Minecraft Launcher/runtime";
    // .minecraft 的常见位置：Roaming、用户主目录、各盘根目录、启动器程序目录（便携模式）
    if (!roaming.isEmpty())
        runtimeRoots << roaming + "/.minecraft/runtime";
    if (!userHome.isEmpty())
        runtimeRoots << userHome + "/.minecraft/runtime";
    if (!appDir.isEmpty())
        runtimeRoots << appDir + "/.minecraft/runtime";
    for (const QString& drive : m_drives)
        runtimeRoots << drive + ".minecraft/runtime";

    for (const QString& root : runtimeRoots) {
        if (isCancelled()) return;
        scanRuntimeRoot(root, outResults);
    }

    // ── 2. HMCL 的 Java 目录：.hmcl/java/<平台>/<运行时>/bin ──
    // HMCL 数据目录可能在 %APPDATA%、用户主目录、各盘根或程序目录（便携模式）
    QStringList hmclRoots;
    if (!roaming.isEmpty()) hmclRoots << roaming + "/.hmcl";
    if (!userHome.isEmpty()) hmclRoots << userHome + "/.hmcl";
    if (!appDir.isEmpty()) hmclRoots << appDir + "/.hmcl";
    for (const QString& drive : m_drives)
        hmclRoots << drive + ".hmcl";

    for (const QString& root : hmclRoots) {
        if (isCancelled()) return;
        scanJavaLibraryDir(root + "/java", outResults);
    }

    // ── 3. 方块盒子自己下载的 Java（默认位置）：AppDataLocation/java/<id>/bin ──
    if (!appDataLoc.isEmpty())
        scanJavaLibraryDir(appDataLoc + "/java", outResults);

    qDebug() << "[JavaScanWorker] 启动器运行时扫描完成，累计发现" << outResults.size() << "个 Java";
}

// ────────────────────────────────────────────────────────────
// 主入口：快速检测 → 全盘深扫（总是执行并合并，保证自定义路径也能找到）
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
        if (si.isValid() && si.isReady() && si.bytesTotal() > 0) {
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

    // ── 阶段 1：快速检测（环境变量、注册表、厂商目录、启动器运行时）──
    // 几秒内完成，先把绝大多数用户的 Java 找出来
    emit scanProgress(
        QStringLiteral("正在检测环境变量、注册表与常见安装目录..."),
        0, -1, 0, totalMB, 0
    );

    scanEnvironmentVariables(allResults);
    if (!isCancelled()) scanRegistry(allResults);
    if (!isCancelled()) scanVendorDirectories(allResults);
    if (!isCancelled()) scanLauncherRuntimes(allResults);

    int totalFound = allResults.size();

    if (isCancelled()) {
        emit scanProgress(
            QStringLiteral("已取消检测，共发现 %1 个 Java").arg(totalFound),
            100, 0, totalMB, totalMB, totalFound
        );
        emit scanFinished(allResults);
        return;
    }

    // ── 阶段 2：全盘深扫（黑名单剪枝 BFS）──
    // 不再"快速检测找到就跳过深扫"：Java 分散装在多个自定义路径（其他启动器目录、
    // 中文文件夹等）时，快速检测只能找到其中一部分，必须深扫补全
    emit scanProgress(
        QStringLiteral("快速检测发现 %1 个 Java，正在全盘深度搜索其余位置...").arg(totalFound),
        5, -1, 0, totalMB, totalFound
    );

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
        // 注意：totalFound 由 scanDrive 内部通过引用同步累加（供进度显示），
        // 这里不能再 += found，否则"已发现"计数会双倍虚增
        scanDrive(info.drive, info.bytes,
                  cumulativeBytes, driveBasePercent,
                  drivePercentShare, totalFound, driveResults);
        allResults.append(driveResults);

        cumulativeBytes += info.bytes;

        if (isCancelled()) break;

        // 当前盘完成 → 发射进度到该盘段的 100%
        int endPercent = qMin(99, 5 + (int)(cumulativeBytes * 95 / totalBytes));
        scannedMB = (int)(cumulativeBytes / (1024 * 1024));
        emit scanProgress(
            QStringLiteral("%1 盘搜索完成 (累计发现 %2 个)").arg(info.drive).arg(totalFound),
            endPercent, -1, scannedMB, totalMB, totalFound
        );
    }

    // ── 完成 ──
    if (!isCancelled()) {
        emit scanProgress(QStringLiteral("检测完成，共发现 %1 个 Java").arg(totalFound),
                          100, 0, totalMB, totalMB, totalFound);
    }
    emit scanFinished(allResults);
}

// ────────────────────────────────────────────────────────────
// 单盘扫描：黑名单剪枝的全盘 BFS
// ────────────────────────────────────────────────────────────

int JavaScanWorker::scanDrive(const QString& drive, qint64 driveBytes,
                               qint64& cumulativeBytes, int& driveBasePercent,
                               int drivePercentShare, int& totalFound,
                               QList<QPair<QString, QString>>& outResults)
{
    qint64 driveStart = QDateTime::currentMSecsSinceEpoch();

    int totalMB  = (int)(m_totalDriveMB > INT_MAX ? INT_MAX : m_totalDriveMB);
    int dirCount = 0;
    int found    = 0;
    int lastEmit = 0;
    qint64 lastEmitTime = driveStart;

    // —— 基于盘大小的粗略时间估算 (用于进度条动画) ——
    qint64 estimatedDriveMs = qMax((qint64)2000, driveBytes / (2000LL * 1024));

    // 黑名单剪枝的全盘 BFS（HMCL/PCL 全盘搜索的通行做法）：
    //   1) 只跳过明确无关的目录（windows / $recycle.bin / 缓存等），
    //      其余目录全部有机会进入——用户自定义的中文目录（如 "系统迁移勿删"）
    //      不含 Java 关键词，按旧的白名单剪枝会被整棵误剪掉
    //   2) 顶层 4 层自由深入，更深层按关键词/版本号剪枝，兼顾覆盖面与速度
    //   3) 符号链接/联接点不进入，避免目录环造成死循环
    // 队列元素：pair<目录路径, 当前深度>
    QQueue<QPair<QString, int>> dirQueue;
    dirQueue.enqueue(qMakePair(drive, 0));

    while (!dirQueue.isEmpty()) {
        if (isCancelled()) break;

        auto head = dirQueue.dequeue();
        const QString dirPath = head.first;
        const int depth = head.second;

#ifndef Q_OS_WIN
        QDir dir(dirPath);
        if (!dir.exists()) continue;
#endif

        dirCount++;

        // 进度发射（每 30 个目录或每 500ms）
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (dirCount - lastEmit >= 30 || now - lastEmitTime >= 500) {
            lastEmit = dirCount;
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

            emit scanProgress(dirPath, overallPercent, eta, scannedMB, totalMB, totalFound);
        }

        if (isCancelled()) break;

        // 单次枚举同时拿子目录与 java 可执行文件
        QStringList subdirs;         // 子目录名（待筛选入队）
        QStringList javaCandidates;  // 本目录下的 java 可执行文件名

#ifdef Q_OS_WIN
        // Windows：原生 FindFirstFileW 单次遍历（比 QDir::entryInfoList 少一层
        // 对象分配，百万级目录下差距明显）；重解析点（符号链接/联接点）直接跳过，
        // 用户目录下大量 "Application Data" 类联接环不会拖慢或绕死扫描
        WIN32_FIND_DATAW fd;
        const QString pattern = dirPath.endsWith(QLatin1Char('/'))
                ? dirPath + QLatin1Char('*')
                : dirPath + QStringLiteral("/*");
        HANDLE hFind = FindFirstFileW(
            reinterpret_cast<const wchar_t*>(pattern.utf16()), &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            continue;
        do {
            const wchar_t* fn = fd.cFileName;
            if (fn[0] == L'.' && (fn[1] == L'\0' || (fn[1] == L'.' && fn[2] == L'\0')))
                continue;  // "." / ".."
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;  // 符号链接/联接点，避免目录环
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                subdirs << QString::fromWCharArray(fn);
            } else if (wcscmp(fn, L"java.exe") == 0 || wcscmp(fn, L"javaw.exe") == 0) {
                javaCandidates << QString::fromWCharArray(fn);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
#else
        const QFileInfoList entries = dir.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo& entry : entries) {
            if (entry.isDir()) {
                if (entry.isSymLink()) continue;  // 符号链接不进入，避免目录环
                subdirs << entry.fileName();
            } else {
                const QString fn = entry.fileName();
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
                if (fn.compare(QLatin1String("java"), Qt::CaseInsensitive) == 0)
                    javaCandidates << fn;
#else
                if (fn.compare(QLatin1String("java"), Qt::CaseInsensitive) == 0
                        || fn.compare(QLatin1String("java.exe"), Qt::CaseInsensitive) == 0
                        || fn.compare(QLatin1String("javaw.exe"), Qt::CaseInsensitive) == 0)
                    javaCandidates << fn;
#endif
            }
        }
#endif

        if (isCancelled()) break;

        // 同一目录下的 java.exe / javaw.exe（移动端 java）是同一个安装：
        // 只保留一个，优先 java.exe（与 tryAddJava 的偏好一致），避免重复列出
        if (javaCandidates.size() > 1) {
            bool hasJavaExe = false, hasPlainJava = false;
            for (const QString& n : javaCandidates) {
                if (n.compare(QLatin1String("java.exe"), Qt::CaseInsensitive) == 0)
                    hasJavaExe = true;
                else if (n.compare(QLatin1String("java"), Qt::CaseInsensitive) == 0)
                    hasPlainJava = true;
            }
            const bool preferJavaExe = hasJavaExe || (!hasPlainJava);
            QString chosen;
            for (const QString& n : javaCandidates) {
                const bool isExe = n.compare(QLatin1String("java.exe"), Qt::CaseInsensitive) == 0;
                const bool isPlain = n.compare(QLatin1String("java"), Qt::CaseInsensitive) == 0;
                if (preferJavaExe == isExe || (!preferJavaExe && isPlain)) {
                    chosen = n;
                    break;
                }
            }
            if (!chosen.isEmpty()) {
                javaCandidates.clear();
                javaCandidates << chosen;
            }
        }

        // ── 子目录：黑名单 + 深度策略决定是否入队 ──
        for (const QString& name : subdirs) {
            if (isExcludedDirName(name)) continue;

            const int childDepth = depth + 1;
            if (childDepth > kMaxScanDepth) continue;

            // 顶层自由深入；更深层按关键词/版本号剪枝
            if (childDepth <= kFreeDescendDepth
                    || isJavaRelatedDirectory(name) || isVersionLikeDirectory(name)) {
                const QString childPath = dirPath.endsWith(QLatin1Char('/'))
                        ? dirPath + name
                        : dirPath + QLatin1Char('/') + name;
                dirQueue.enqueue(qMakePair(childPath, childDepth));
            }
        }

        // ── 文件：验证并加入 java.exe / javaw.exe（移动端为 java）──
        for (const QString& name : javaCandidates) {
            if (isCancelled()) break;

            // 去重
            const QString absPath = QDir::toNativeSeparators(
                dirPath.endsWith(QLatin1Char('/'))
                        ? dirPath + name
                        : dirPath + QLatin1Char('/') + name);
            if (m_seenPaths.contains(absPath)) continue;
            m_seenPaths.insert(absPath);

            QString version;
            if (validateJavaPath(absPath, version)) {
                outResults.append(qMakePair(absPath, version));
                found++;
                totalFound++;
                qDebug() << "[JavaScanWorker] 发现 Java:" << version << "@" << absPath;
            }
        }
    }

    return found;
}

// ────────────────────────────────────────────────────────────
// 目录名黑名单（全盘深扫剪枝）
// ────────────────────────────────────────────────────────────

bool JavaScanWorker::isExcludedDirName(const QString& dirName) const
{
    return m_excludedDirNames.contains(dirName.toLower());
}

// ────────────────────────────────────────────────────────────
// 关键词剪枝（深层 BFS）
// ────────────────────────────────────────────────────────────

bool JavaScanWorker::isJavaRelatedDirectory(const QString& dirName) const
{
    if (dirName.isEmpty()) return false;
    QString lower = dirName.toLower();

    // 排除常见无关目录名（黑名单在入队前已拦截，这里兜底排除同名变体）
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

    // 每 100ms 轮询一次，让取消信号可以及时中断；
    // 总超时 5s：杀软实时扫描下 JVM 启动可能超过 2s（与 GameLauncher::validateJavaPath 对齐），
    // 超时过短会把有效的 Java 误判为无效
    for (int i = 0; i < 50; i++) {
        if (isCancelled()) {
            process.kill();
            return false;
        }
        if (process.waitForFinished(100))
            break;
    }

    if (process.state() != QProcess::NotRunning)
    {
        // 超时未退出（如失效的 javapath 存根）：结束进程，避免残留挂起的 java.exe
        process.kill();
        process.waitForFinished(1000);
        return false;
    }

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

    // 每 100ms 轮询一次，让取消信号可以及时中断；总超时 5s（与 validateJavaPath 对齐）
    for (int i = 0; i < 50; i++) {
        if (isCancelled()) {
            process.kill();
            return false;
        }
        if (process.waitForFinished(100))
            break;
    }

    if (process.state() != QProcess::NotRunning)
    {
        // 超时未退出：结束进程，避免残留挂起的 java.exe
        process.kill();
        process.waitForFinished(1000);
        return false;
    }

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
