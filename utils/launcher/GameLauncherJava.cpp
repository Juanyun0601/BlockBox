/**
 * @file   GameLauncherJava.cpp
 * @brief  游戏启动器 Java 检测模块
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "utils/GameLauncher.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>

#include "utils/SettingsManager.h"

GameLauncher::JavaInfo GameLauncher::detectJava()
{
    JavaInfo info;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("java", {"-version"});

    if (process.waitForFinished(2000))
    {
        QByteArray output = process.readAll();
        QString outputStr = QString::fromLocal8Bit(output);

        static const QRegularExpression versionRegex("version \"([0-9]+(\\.[0-9]+)*(_[0-9]+)?)");
        QRegularExpressionMatch match = versionRegex.match(outputStr);
        if (match.hasMatch())
        {
            info.version = match.captured(1);
            info.path = "java";
            info.valid = true;
            return info;
        }
    }

    QList<QString> commonPaths;

#ifdef Q_OS_ANDROID
    // Android: scan common Java paths
    commonPaths << "/system/bin";
    commonPaths << "/data/data/com.termux/files/usr/bin";
    commonPaths << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/BlockBox/java";
    commonPaths << QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/java";
#else
    commonPaths << "C:/Program Files/Java";
    commonPaths << "C:/Program Files (x86)/Java";

    QSettings settings("HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Runtime Environment", QSettings::NativeFormat);
    QString currentVersion = settings.value("CurrentVersion").toString();
    if (!currentVersion.isEmpty())
    {
        QString javaHome = settings.value(currentVersion + "/JavaHome").toString();
        if (!javaHome.isEmpty())
        {
            commonPaths << javaHome + "/bin";
        }
    }
#endif

    for (const QString& basePath : commonPaths)
    {
        QDir dir(basePath);
        if (dir.exists())
        {
#ifdef Q_OS_ANDROID
            QString javaExePath = dir.absoluteFilePath("java");
#else
            QDir binDir(dir.absolutePath() + "/bin");
            QString javaExePath = binDir.absoluteFilePath("java.exe");
#endif
            if (QFileInfo::exists(javaExePath))
            {
                JavaInfo tempInfo;
                if (validateJavaPath(javaExePath, tempInfo))
                {
                    return tempInfo;
                }
            }

            QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs)
            {
#ifdef Q_OS_ANDROID
                QString javaExePath = subdir.absoluteFilePath() + "/bin/java";
#else
                QDir binDir(subdir.absolutePath() + "/bin");
                QString javaExePath = binDir.absoluteFilePath("java.exe");
#endif
                if (QFileInfo::exists(javaExePath))
                {
                    JavaInfo tempInfo;
                    if (validateJavaPath(javaExePath, tempInfo))
                    {
                        return tempInfo;
                    }
                }
            }
        }
    }

    return info;
}

QList<GameLauncher::JavaInfo> GameLauncher::findAllJavaInstallations()
{
    QList<JavaInfo> javaList;

    QList<QString> commonPaths;
#ifdef Q_OS_ANDROID
    commonPaths << "/system/bin";
    commonPaths << "/data/data/com.termux/files/usr/bin";
    commonPaths << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/BlockBox/java";
    commonPaths << QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/java";
#else
    commonPaths << "C:/Program Files/Java";
    commonPaths << "C:/Program Files (x86)/Java";
#endif

    for (const QString& basePath : commonPaths)
    {
        QDir dir(basePath);
        if (dir.exists())
        {
            QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& subdir : subdirs)
            {
#ifdef Q_OS_ANDROID
                QString javaExePath = subdir.absoluteFilePath() + "/bin/java";
#else
                QDir binDir(subdir.absolutePath() + "/bin");
                QString javaExePath = binDir.absoluteFilePath("java.exe");
#endif
                if (QFileInfo::exists(javaExePath))
                {
                    JavaInfo info;
                    if (validateJavaPath(javaExePath, info))
                    {
                        javaList.append(info);
                    }
                }
            }
        }
    }

    JavaInfo pathJava = detectJava();
    if (pathJava.valid)
    {
        bool alreadyExists = false;
        for (const JavaInfo& info : javaList)
        {
            if (info.path == pathJava.path)
            {
                alreadyExists = true;
                break;
            }
        }
        if (!alreadyExists)
        {
            javaList.append(pathJava);
        }
    }

    return javaList;
}

QList<GameLauncher::JavaInfo> GameLauncher::findJavaOnSystem()
{
    QList<JavaInfo> javaList;
    QSet<QString> seenPaths;

#ifdef Q_OS_ANDROID
    // Android: scan specific directories
    QStringList androidPaths;
    androidPaths << "/system/bin";
    androidPaths << "/data/data/com.termux/files/usr/bin";
    androidPaths << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/BlockBox/java";
    androidPaths << QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/java";

    for (const QString& path : androidPaths)
    {
        QDir dir(path);
        if (!dir.exists())
            continue;

        QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries)
        {
            searchJavaInDirectory(entry.absoluteFilePath(), javaList, seenPaths, 0, 3);
        }
    }
#else
    QStringList drives;
    drives << "C:/" << "D:/" << "E:/";

    QStringList excludeDirs;
    excludeDirs << "Windows" << "ProgramData" << "$Recycle.Bin" << "System Volume Information"
                << "Recovery" << "MSOCache" << "PerfLogs";

    for (const QString& drive : drives)
    {
        QDir driveDir(drive);
        if (!driveDir.exists())
        {
            continue;
        }

        QFileInfoList entries = driveDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries)
        {
            if (excludeDirs.contains(entry.fileName()))
            {
                continue;
            }

            searchJavaInDirectory(entry.absoluteFilePath(), javaList, seenPaths, 0, 5);
        }
    }
#endif

    return javaList;
}

void GameLauncher::searchJavaInDirectory(const QString& dirPath, QList<JavaInfo>& javaList,
                                          QSet<QString>& seenPaths, int depth, int maxDepth)
{
    if (depth > maxDepth)
    {
        return;
    }

    QDir dir(dirPath);
    if (!dir.exists())
    {
        return;
    }

    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries)
    {
        if (entry.isFile())
        {
            QString fileName = entry.fileName().toLower();
#ifdef Q_OS_ANDROID
            if (fileName == "java")
#else
            if (fileName == "java.exe" || fileName == "javaw.exe")
#endif
            {
                QString absPath = QDir::toNativeSeparators(entry.absoluteFilePath());
                if (!seenPaths.contains(absPath))
                {
                    seenPaths.insert(absPath);
                    JavaInfo info;
                    if (validateJavaPath(absPath, info))
                    {
                        javaList.append(info);
                    }
                }
            }
        }
        else if (entry.isDir())
        {
            QString dirName = entry.fileName().toLower();
#ifdef Q_OS_ANDROID
            // Android: skip common non-Java directories
            if (dirName == "proc" || dirName == "sys" || dirName == "dev")
            {
                continue;
            }
#else
            if (dirName == "windows" || dirName == "programdata" || dirName == "$recycle.bin"
                || dirName == "system volume information" || dirName == "recovery"
                || dirName == "msocache" || dirName == "perflogs")
            {
                continue;
            }
#endif
            searchJavaInDirectory(entry.absoluteFilePath(), javaList, seenPaths, depth + 1, maxDepth);
        }
    }
}

void GameLauncher::initJavaCache()
{
    SettingsManager* settings = SettingsManager::instance();
    QList<QPair<QString, QString>> cachedInstallations = settings->getJavaInstallations();

    if (!cachedInstallations.isEmpty())
    {
        emit launchDetailAdded(QString("已从缓存加载 %1 个Java").arg(cachedInstallations.size()));
        return;
    }

    // 缓存为空：不再自动弹窗询问/全盘扫描（已由新手引导第 3 步「Java 管理」与
    // 设置页「扫描 Java」承担），启动流程静默跳过，避免打断首次启动体验。
    emit launchDetailAdded("未找到已保存的Java信息，可在新手引导或设置中配置Java路径");
}

bool GameLauncher::validateJavaPath(const QString& path, JavaInfo& info)
{
    info.path = path;
    info.valid = false;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(path, {"-version"});

    if (process.waitForFinished(2000))
    {
        QByteArray output = process.readAll();
        QString outputStr = QString::fromLocal8Bit(output);

        static const QRegularExpression versionRegex("version \"([0-9]+(\\.[0-9]+)*(_[0-9]+)?)");
        QRegularExpressionMatch match = versionRegex.match(outputStr);
        if (match.hasMatch())
        {
            info.version = match.captured(1);
            info.valid = true;
            return true;
        }
    }

    return false;
}

GameLauncher::JavaInfo GameLauncher::findBestJavaVersion(const QString& gameVersion)
{
    JavaInfo bestJavaInfo;

    // ── 收集候选 Java（优先用缓存，缓存为空才现场扫描）──
    QList<JavaInfo> javaList;

    QList<QPair<QString, QString>> cachedInstallations =
        SettingsManager::instance()->getJavaInstallations();
    QSet<QString> seenPaths;
    if (!cachedInstallations.isEmpty())
    {
        for (const auto& inst : cachedInstallations)
        {
            if (seenPaths.contains(inst.first)) continue;
            seenPaths.insert(inst.first);

            // 验证缓存路径仍存在（Java 可能已被外部卸载）
            if (!QFile::exists(inst.first))
            {
                emit launchDetailAdded(QString("跳过无效的 Java 路径: %1").arg(inst.first));
                continue;
            }

            JavaInfo info;
            info.path = inst.first;
            info.version = inst.second;
            info.valid = true;
            javaList.append(info);
        }
    }

    if (javaList.isEmpty())
    {
        emit launchDetailAdded("Java 缓存为空，正在扫描系统...");
        javaList = findAllJavaInstallations();
    }

    if (javaList.isEmpty())
    {
        return bestJavaInfo;
    }

    // ── 计算该游戏版本的 Java 版本约束 [minJava, maxJava] ──
    // 参考 HMCL GameJavaVersion.getMinimumJavaVersion + JavaVersionConstraint
    // 以及 PCL-CE ModLaunch.cs McLaunchJava 的版本阈值
    int minJavaVersion = 0;     // 0 表示无最低限制
    int maxJavaVersion = 999;   // 999 表示无上限

    int mcMajor = 0;
    int mcMinor = 0;
    int mcPatch = 0;

    static const QRegularExpression prefixedVersionRegex("1\\.([0-9]+)(?:\\.([0-9]+))?");
    QRegularExpressionMatch prefixedMatch = prefixedVersionRegex.match(gameVersion);
    if (prefixedMatch.hasMatch())
    {
        mcMajor = 1;
        mcMinor = prefixedMatch.captured(1).toInt();
        QString patchStr = prefixedMatch.captured(2);
        mcPatch = patchStr.isEmpty() ? 0 : patchStr.toInt();
    }
    else
    {
        static const QRegularExpression plainVersionRegex("([0-9]+)(?:\\.([0-9]+))?");
        QRegularExpressionMatch plainMatch = plainVersionRegex.match(gameVersion);
        if (plainMatch.hasMatch())
        {
            mcMajor = plainMatch.captured(1).toInt();
            QString minorStr = plainMatch.captured(2);
            mcMinor = minorStr.isEmpty() ? 0 : minorStr.toInt();
        }
    }

    if (mcMajor == 1)
    {
        if (mcMinor <= 5)
        {
            // MC <= 1.5.2：旧 lwjgl 不兼容新 Java，上限 Java 8
            minJavaVersion = 0;
            maxJavaVersion = 8;
        }
        else if (mcMinor <= 12)
        {
            // MC 1.6 - 1.12：LaunchWrapper 假设 SystemClassLoader 是 URLClassLoader
            // Java 9+ 会崩溃，强制上限 Java 8（HMCL LAUNCH_WRAPPER 约束）
            minJavaVersion = 8;
            maxJavaVersion = 8;
        }
        else if (mcMinor <= 16)
        {
            // MC 1.13 - 1.16：最低 Java 8，原版无严格上限
            minJavaVersion = 8;
            maxJavaVersion = 999;
        }
        else if (mcMinor == 17)
        {
            // MC 1.17：Mojang 官方要求 Java 16（原代码误设为 17）
            minJavaVersion = 16;
            maxJavaVersion = 999;
        }
        else if (mcMinor >= 18 && mcMinor <= 20)
        {
            if (mcMinor == 20 && mcPatch >= 5)
            {
                // MC 1.20.5+：最低 Java 21
                minJavaVersion = 21;
            }
            else
            {
                // MC 1.18 - 1.20.4：最低 Java 17
                minJavaVersion = 17;
            }
            maxJavaVersion = 999;
        }
        else
        {
            // MC 1.21+：最低 Java 21
            minJavaVersion = 21;
            maxJavaVersion = 999;
        }
    }
    else if (mcMajor >= 26)
    {
        if (mcMajor > 26 || mcMinor >= 1)
        {
            // MC 26.1+：最低 Java 25
            minJavaVersion = 25;
        }
        else
        {
            minJavaVersion = 21;
        }
        maxJavaVersion = 999;
    }
    else
    {
        // 无法识别的版本号（快照如 23w14a 等），按通用规则：最低 Java 8
        minJavaVersion = 8;
        maxJavaVersion = 999;
    }

    QString rangeDesc = (maxJavaVersion == 999)
        ? QStringLiteral("Java %1+").arg(minJavaVersion)
        : QStringLiteral("Java %1-%2").arg(minJavaVersion).arg(maxJavaVersion);
    emit launchDetailAdded(QString("游戏版本 %1 需要 %2").arg(gameVersion).arg(rangeDesc));

    // ── 提取 Java 主版本号的工具 lambda（修复原 split('.').first() 对 Java 8 取到 1 的 bug）──
    auto javaMajorOf = [](const QString& version) -> int {
        static const QRegularExpression re("(?:1\\.)?([0-9]+)");
        QRegularExpressionMatch m = re.match(version);
        return m.hasMatch() ? m.captured(1).toInt() : 0;
    };

    // ── 筛选满足约束的候选，优先选最低兼容版本（最贴近 minJavaVersion）──
    int bestMajor = -1;
    for (const JavaInfo& javaInfo : javaList)
    {
        int javaMajor = javaMajorOf(javaInfo.version);
        if (javaMajor <= 0) continue;

        if (javaMajor < minJavaVersion || javaMajor > maxJavaVersion)
        {
            continue;
        }

        // 选最低兼容版本（参考 PCL SelectSuitableJavaAsync：按主版本号升序优先）
        if (bestMajor < 0 || javaMajor < bestMajor)
        {
            bestJavaInfo = javaInfo;
            bestMajor = javaMajor;
        }
    }

    // ── 兜底：若无满足约束的版本，使用系统最高版本（并给出警告）──
    if (bestMajor < 0)
    {
        JavaInfo highestJava;
        int highestMajor = -1;
        for (const JavaInfo& javaInfo : javaList)
        {
            int javaMajor = javaMajorOf(javaInfo.version);
            if (javaMajor > highestMajor)
            {
                highestJava = javaInfo;
                highestMajor = javaMajor;
            }
        }

        if (highestJava.valid)
        {
            bestJavaInfo = highestJava;
            emit launchDetailAdded(QString("警告: 未找到满足约束 (%1) 的 Java，使用最高版本兜底: %2 (%3)")
                .arg(rangeDesc).arg(bestJavaInfo.version).arg(bestJavaInfo.path));
        }
    }
    else
    {
        emit launchDetailAdded(QString("选择了兼容的 Java 版本: %1 (%2)").arg(bestJavaInfo.version).arg(bestJavaInfo.path));
    }

    return bestJavaInfo;
}