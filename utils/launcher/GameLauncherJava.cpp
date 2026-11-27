/**
 * @file   GameLauncherJava.cpp
 * @brief  游戏启动器 Java 检测模块
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "utils/GameLauncher.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>

#include "platform.h"
#include "utils/JavaDownloader.h"
#include "utils/JavaScanWorker.h"
#include "utils/SettingsManager.h"

namespace {

/// java.exe 同目录下的 server/jvm.dll
/// 真实安装（JDK/JRE）都有这个文件；Oracle 的 javapath / java8path 转发存根目录里没有，
/// 靠这一点可以把「存根」排到真实安装后面（存根可能指向 32 位 JVM）。
QString serverJvmDllOf(const QString &javaExe)
{
    if (javaExe.isEmpty())
    {
        return QString();
    }

    const QFileInfo exeInfo(javaExe);
    // detectJava() 可能返回 PATH 简写 "java"，此时无从判断
    if (!exeInfo.isAbsolute())
    {
        return QString();
    }

    return exeInfo.absolutePath() + QStringLiteral("/server/jvm.dll");
}

/// 读取 PE 头里的 Machine 字段：0x8664=x64，0x014c=x86，读不到返回 0
int readPeMachine(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return 0;
    }

    const QByteArray dosHeader = file.read(0x40);
    if (dosHeader.size() < 0x40)
    {
        return 0;
    }

    const auto u16At = [](const QByteArray &data, int offset) -> quint16 {
        return quint16(quint8(data[offset])) | (quint16(quint8(data[offset + 1])) << 8);
    };
    const auto u32At = [](const QByteArray &data, int offset) -> quint32 {
        return quint32(quint8(data[offset]))
             | (quint32(quint8(data[offset + 1])) << 8)
             | (quint32(quint8(data[offset + 2])) << 16)
             | (quint32(quint8(data[offset + 3])) << 24);
    };

    // e_lfanew(0x3C) 指向 PE 签名
    const quint32 peOffset = u32At(dosHeader, 0x3C);
    if (peOffset < 0x40 || peOffset > 0x10000 || !file.seek(peOffset))
    {
        return 0;
    }

    const QByteArray peHeader = file.read(6);
    if (peHeader.size() < 6
        || peHeader.at(0) != 'P' || peHeader.at(1) != 'E'
        || peHeader.at(2) != '\0' || peHeader.at(3) != '\0')
    {
        return 0;
    }

    return u16At(peHeader, 4);
}

} // namespace

int GameLauncher::javaBitness(const QString &javaExe)
{
    const int machine = readPeMachine(serverJvmDllOf(javaExe));
    if (machine == 0x8664)
    {
        return 64;
    }
    if (machine == 0x014c)
    {
        return 32;
    }
    return 0;
}

int GameLauncher::javaMajorOf(const QString& version)
{
    // "1.8.0_301"→8，"17.0.1"→17，"21"→21；兼容 Java 8 的 "1.x" 老写法
    static const QRegularExpression re(QStringLiteral("(?:1\\.)?([0-9]+)"));
    const QRegularExpressionMatch m = re.match(version);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

bool GameLauncher::wasJavaRescueAttempted() const
{
    return m_javaRescueAttempted;
}

GameLauncher::JavaInfo GameLauncher::detectJava()
{
    JavaInfo info;

    // ── 快速路径：PATH 上的 java（一次 -version 即可返回）──
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("java", {"-version"});

    // PATH 上的 java 可能是失效的 Oracle javapath 存根（快速退出），
    // 也可能因杀软扫描/系统负载而启动缓慢，超时需留足余量
    if (!process.waitForFinished(5000))
    {
        // 超时：结束残留进程（失效的 javapath 存根会一直挂着不退出）
        process.kill();
        process.waitForFinished(1000);
    }
    else
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

    // ── 兜底：按开源启动器（HMCL/PrismLauncher/MultiMC）通行做法做统一快速检测──
    // 依次查环境变量（JAVA_HOME/PATH）→ 注册表（全厂商键）→ 厂商安装目录 →
    // Minecraft 启动器自带运行时；具体实现见 JavaScanWorker::runQuickScan
    JavaScanWorker worker;
    const QList<QPair<QString, QString>> found = worker.runQuickScan();
    if (!found.isEmpty())
    {
        info.path = found.first().first;
        info.version = found.first().second;
        info.valid = true;
        return info;
    }

    return info;
}

QList<GameLauncher::JavaInfo> GameLauncher::cachedJavaInstallations() const
{
    QList<JavaInfo> javaList;
    QSet<QString> seenPaths;

    const QList<QPair<QString, QString>> cachedInstallations =
        SettingsManager::instance()->getJavaInstallations();
    for (const QPair<QString, QString>& inst : cachedInstallations)
    {
        if (!QFile::exists(inst.first))
        {
            continue;
        }
        const QString absPath = QDir::toNativeSeparators(inst.first);
        if (seenPaths.contains(absPath))
        {
            continue;
        }
        seenPaths.insert(absPath);

        JavaInfo info;
        info.path = inst.first;
        info.version = inst.second;
        info.valid = true;
        javaList.append(info);
    }

    return javaList;
}

void GameLauncher::scanJavaInBackground(QObject* context,
                                        std::function<void(const QList<JavaInfo>&)> onFinished)
{
    // 缓存读取放在主线程；扫描阶段只做目录遍历与 java -version 进程探测，
    // 不触碰设置存储，因此可以在工作线程安全执行
    const QList<QPair<QString, QString>> cachedInstallations =
        SettingsManager::instance()->getJavaInstallations();

    auto* watcher = new QFutureWatcher<QList<JavaInfo>>(context);
    QObject::connect(watcher, &QFutureWatcher<QList<JavaInfo>>::finished, context,
                     [watcher, onFinished]()
    {
        const QList<JavaInfo> result = watcher->result();
        watcher->deleteLater();

        // 回到主线程后写回缓存（下次启动可直接复用），再通知调用方填充界面
        if (!result.isEmpty())
        {
            QList<QPair<QString, QString>> installations;
            installations.reserve(result.size());
            for (const JavaInfo& info : result)
            {
                installations.append(qMakePair(info.path, info.version));
            }
            SettingsManager::instance()->setJavaInstallations(installations);
        }

        if (onFinished)
            onFinished(result);
    });

    watcher->setFuture(QtConcurrent::run([cachedInstallations]()
    {
        return instance()->findAllJavaInstallations(cachedInstallations, false);
    }));
}

QList<GameLauncher::JavaInfo> GameLauncher::findAllJavaInstallations()
{
    return findAllJavaInstallations(SettingsManager::instance()->getJavaInstallations(), true);
}

QList<GameLauncher::JavaInfo> GameLauncher::findAllJavaInstallations(
    const QList<QPair<QString, QString>>& cachedInstallations, bool persistToCache)
{
    QList<JavaInfo> javaList;
    QSet<QString> seenPaths;

    // ── 0) 已缓存的 Java 优先（设置页下拉框的主要数据源）──
    // PATH 上的 java 可能是失效的 Oracle javapath 存根、常见目录也可能没有 Java，
    // 此时目录扫描与 detectJava 都会扑空，但 config.ini 缓存里的 Java 仍然有效。
    // 与 findBestJavaVersion 保持一致：缓存条目验证存在性 + 可执行性后纳入。
    for (const QPair<QString, QString>& inst : cachedInstallations)
    {
        if (!QFile::exists(inst.first))
        {
            continue;
        }
        QString absPath = QDir::toNativeSeparators(inst.first);
        if (seenPaths.contains(absPath))
        {
            continue;
        }
        JavaInfo info;
        if (validateJavaPath(inst.first, info))
        {
            seenPaths.insert(absPath);
            javaList.append(info);
        }
    }
    QList<QString> commonPaths;
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
    commonPaths << "/system/bin";
    commonPaths << "/data/data/com.termux/files/usr/bin";
#if defined(Q_OS_ANDROID)
    // 一键安装运行环境的落点与全局数据目录同根（外部存储不可写时退内部目录）；
    // 旧版通用数据路径仍保留扫描，兼容老安装不被"失踪"
    commonPaths << Platform::getDataDirectory() + "/java";
#endif
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
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
                QString javaExePath = subdir.absoluteFilePath() + "/bin/java";
#else
                QDir binDir(subdir.absolutePath() + "/bin");
                QString javaExePath = binDir.absoluteFilePath("java.exe");
#endif
                if (QFileInfo::exists(javaExePath))
                {
                    QString absPath = QDir::toNativeSeparators(javaExePath);
                    if (seenPaths.contains(absPath))
                    {
                        continue;
                    }
                    JavaInfo info;
                    if (validateJavaPath(javaExePath, info))
                    {
                        seenPaths.insert(absPath);
                        javaList.append(info);
                    }
                }
            }
        }
    }

    JavaInfo pathJava = detectJava();
    if (pathJava.valid)
    {
        QString absPath = QDir::toNativeSeparators(pathJava.path);
        if (!seenPaths.contains(absPath))
        {
            seenPaths.insert(absPath);
            javaList.append(pathJava);
        }
    }

    // 扫描结果写回缓存：下次启动/打开设置页直接复用，不必再做整轮全量扫描
    // （后台线程调用方会自行在主线程写回，此处跳过以避免跨线程访问 QSettings）
    if (persistToCache && !javaList.isEmpty())
    {
        QList<QPair<QString, QString>> installations;
        installations.reserve(javaList.size());
        for (const JavaInfo& info : javaList)
        {
            installations.append(qMakePair(info.path, info.version));
        }
        SettingsManager::instance()->setJavaInstallations(installations);
    }

    return javaList;
}

QList<GameLauncher::JavaInfo> GameLauncher::findJavaOnSystem()
{
    QList<JavaInfo> javaList;
    QSet<QString> seenPaths;

#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
    // 移动端：扫描固定目录（含一键安装的运行依赖目录）
    QStringList androidPaths;
    androidPaths << "/system/bin";
    androidPaths << "/data/data/com.termux/files/usr/bin";
#if defined(Q_OS_ANDROID)
    // 一键安装落点（与全局数据目录同根）+ 旧版通用数据路径（兼容老安装）
    androidPaths << Platform::getDataDirectory() + "/java";
#endif
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
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
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
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
            // 移动端：跳过明显不是 Java 的系统目录
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

    // 正常情况 java -version 在百毫秒级完成，但杀软实时扫描/系统负载下
    // JVM 启动可能超过 2 秒（实测 2.1s+），超时过短会把有效的 Java 误判为无效
    if (!process.waitForFinished(5000))
    {
        // 超时：结束残留进程，避免每次探测都留下一个挂起的 java.exe
        process.kill();
        process.waitForFinished(1000);
        return false;
    }

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

    return false;
}

GameLauncher::JavaRequirement GameLauncher::javaRequirementFor(const QString& gameVersion, int jsonMajorHint)
{
    JavaRequirement req;

    int mcMajor = 0;
    int mcMinor = 0;
    int mcPatch = 0;

    // 旧规则 1.x（必须从开头锚定：新规则 "26.1.1" 里也含 "1.1"，
    // 不锚定会被误判成 MC 1.1，从而错误地把上限压到 Java 8）
    static const QRegularExpression legacyRegex(QStringLiteral(R"(^1\.([0-9]+)(?:\.([0-9]+))?)"));
    // 新规则（2026 年起）：26.1 / 26.1.2 / 26.4-snapshot-5 / 26.3-rc-1
    static const QRegularExpression yearRegex(QStringLiteral(R"(^([0-9]{2,})(?:\.([0-9]+))?(?:\.([0-9]+))?)"));
    // 旧快照 23w14a（没有版本号，只能按年份粗略推断）
    static const QRegularExpression weekSnapshotRegex(
        QStringLiteral(R"(^([0-9]{2})w([0-9]{2})[a-z])"), QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch m = legacyRegex.match(gameVersion);
    if (m.hasMatch())
    {
        mcMajor = 1;
        mcMinor = m.captured(1).toInt();
        mcPatch = m.captured(2).toInt();
    }
    else
    {
        m = yearRegex.match(gameVersion);
        if (m.hasMatch())
        {
            mcMajor = m.captured(1).toInt();
            mcMinor = m.captured(2).toInt();
            mcPatch = m.captured(3).toInt();
        }
    }

    if (mcMajor == 1)
    {
        if (mcMinor <= 5)
        {
            // MC <= 1.5.2：旧 lwjgl 不兼容新 Java，上限 Java 8
            req.minJava = 0;
            req.maxJava = 8;
        }
        else if (mcMinor <= 12)
        {
            // MC 1.6 - 1.12：LaunchWrapper 假设 SystemClassLoader 是 URLClassLoader
            // Java 9+ 会崩溃，强制上限 Java 8（HMCL LAUNCH_WRAPPER 约束）
            req.minJava = 8;
            req.maxJava = 8;
        }
        else if (mcMinor <= 16)
        {
            // MC 1.13 - 1.16：最低 Java 8，原版无严格上限
            req.minJava = 8;
            req.maxJava = 999;
        }
        else if (mcMinor == 17)
        {
            // MC 1.17：Mojang 官方要求 Java 16
            req.minJava = 16;
            req.maxJava = 999;
        }
        else if (mcMinor >= 18 && mcMinor <= 20)
        {
            if (mcMinor == 20 && mcPatch >= 5)
            {
                // MC 1.20.5+：最低 Java 21
                req.minJava = 21;
            }
            else
            {
                // MC 1.18 - 1.20.4：最低 Java 17
                req.minJava = 17;
            }
            req.maxJava = 999;
        }
        else
        {
            // MC 1.21+：最低 Java 21
            req.minJava = 21;
            req.maxJava = 999;
        }
    }
    else if (mcMajor >= 26)
    {
        // 2026 年起的新版本号规则：26.1 / 26.2 / 26.3 … 官方要求 Java 25
        req.minJava = 25;
        req.maxJava = 999;
    }
    else
    {
        QRegularExpressionMatch wm = weekSnapshotRegex.match(gameVersion);
        if (wm.hasMatch())
        {
            // 旧快照：按年份推断当时的主版本要求
            const int year = 2000 + wm.captured(1).toInt();
            if (year >= 2024)      req.minJava = 21;
            else if (year >= 2022) req.minJava = 17;
            else if (year >= 2021) req.minJava = 16;
            else                   req.minJava = 8;
        }
        else
        {
            // 无法识别的版本号（自定义命名/整合包改名等）：按通用规则最低 Java 8
            req.minJava = 8;
        }
        req.maxJava = 999;
    }

    // 版本 JSON 里的 javaVersion.majorVersion 是官方声明的最低要求，优先级最高
    // （新规则、快照、模组版本都靠它兜底，避免按版本号硬编码判断失误）
    if (jsonMajorHint > 0)
        req.minJava = jsonMajorHint;

    return req;
}

GameLauncher::JavaInfo GameLauncher::findBestJavaVersion(const QString& gameVersion, int jsonMajorHint)
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
    // 以及 PCL-CE ModLaunch.cs McLaunchJava 的版本阈值；
    // 版本号规则（旧 1.x / 新 26.x / 快照）与官方 javaVersion 提示都在里面统一处理
    const JavaRequirement req = javaRequirementFor(gameVersion, jsonMajorHint);
    const int minJavaVersion = req.minJava;
    const int maxJavaVersion = req.maxJava;
    const QString rangeDesc = req.rangeDesc();
    emit launchDetailAdded(QString("游戏版本 %1 需要 %2").arg(gameVersion, rangeDesc));

    // ── 提取 Java 主版本号的工具 lambda（修复原 split('.').first() 对 Java 8 取到 1 的 bug）──
    auto javaMajorOf = [](const QString& version) -> int {
        static const QRegularExpression re("(?:1\\.)?([0-9]+)");
        QRegularExpressionMatch m = re.match(version);
        return m.hasMatch() ? m.captured(1).toInt() : 0;
    };

    // ── 筛选满足约束的候选，优先选最低兼容版本（最贴近 minJavaVersion）──
    // 同一主版本内再有侧重：真实安装 > Oracle 转发存根；64 位 JVM > 32 位。
    // 32 位 JVM 的堆上限只有 1.2~1.5GB，自动内存分配很容易超限导致秒退，
    // 而 Oracle 的 java8path（(x86) 目录下）往往是扫描时最先命中的那个。
    int bestMajor = -1;
    int bestTier = -1;
    for (const JavaInfo& javaInfo : javaList)
    {
        int javaMajor = javaMajorOf(javaInfo.version);
        if (javaMajor <= 0) continue;

        if (javaMajor < minJavaVersion || javaMajor > maxJavaVersion)
        {
            continue;
        }

        const bool hasRealJvm = QFile::exists(serverJvmDllOf(javaInfo.path));
        const int bitness = hasRealJvm ? javaBitness(javaInfo.path) : 0;
        int tier = 0;
        if (hasRealJvm)      tier += 2;
        if (bitness == 64)   tier += 1;

        // 选最低兼容版本；主版本相同的才用 tier 决胜
        if (bestMajor < 0 || javaMajor < bestMajor
            || (javaMajor == bestMajor && tier > bestTier))
        {
            bestJavaInfo = javaInfo;
            bestMajor = javaMajor;
            bestTier = tier;
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
            qDebug() << "[GameLauncher]" << "未找到满足约束的 Java，最高版本兜底:"
                     << bestJavaInfo.version << bestJavaInfo.path;
            emit launchDetailAdded(QString("警告: 未找到满足约束 (%1) 的 Java，使用最高版本兜底: %2 (%3)")
                .arg(rangeDesc).arg(bestJavaInfo.version).arg(bestJavaInfo.path));
            // 版本太低（例如新版游戏需要 Java 25，而机器上最高只有 Java 21）时明确提示
            if (highestMajor < minJavaVersion)
            {
                emit launchDetailAdded(QString("警告: 当前最高 Java %1 低于所需 Java %2，"
                                               "请安装 Java %2 或更高版本后重试")
                    .arg(highestMajor).arg(minJavaVersion));
            }
        }
    }
    else
    {
        const int bitness = javaBitness(bestJavaInfo.path);
        const QString bitnessText = (bitness == 64) ? QStringLiteral("64 位")
                                  : (bitness == 32) ? QStringLiteral("32 位")
                                                    : QStringLiteral("位数未知");
        // 写进 launcher.log：用户反馈「Java 没选对」时，日志里能直接看到实际用了哪个 java.exe
        qDebug() << "[GameLauncher]" << "自动选择 Java:" << bestJavaInfo.version
                 << bestJavaInfo.path << bitnessText;
        emit launchDetailAdded(QString("选择了兼容的 Java 版本: %1 (%2, %3)")
            .arg(bestJavaInfo.version, bestJavaInfo.path, bitnessText));
    }

    return bestJavaInfo;
}

// ────────────────────────────────────────────────────────────
// Java 缺失自救：启动时没有合适的 Java，先全盘查找本机已装的 Java，
// 仍没有合适的就按游戏版本需求自动安装一个，装好后继续启动
// ────────────────────────────────────────────────────────────

void GameLauncher::beginJavaRescue(const QString& gameVersion, int jsonJavaMajor)
{
    m_javaRescueAttempted = true;
    m_rescueGeneration++;
    m_rescueConfig = m_currentConfig;
    m_rescueGameVersion = gameVersion;
    m_rescueJsonJavaMajor = jsonJavaMajor;
    m_rescueInstallAttempted = false;
    m_rescueOfficialMirrorRetried = false;
    m_rescueSelectedMajor = 0;
    m_rescueLastProgressBucket = -1;
    m_rescueStage = RescueStage::Scanning;

    emit launchDetailAdded(QStringLiteral("未找到合适的 Java，正在全盘查找本机已安装的 Java..."));
    emit launchProgressChanged(20, QStringLiteral("正在查找Java..."));

    // 全盘 Java 查找：复用设置页「Java检测」的 JavaScanWorker 后台线程模式，
    // 扫描结果回来后仍会合并进缓存供以后启动使用
    m_rescueScanWorker = new JavaScanWorker;
    QThread* scanThread = new QThread;
    m_rescueScanWorker->moveToThread(scanThread);

    connect(scanThread, &QThread::started, m_rescueScanWorker, &JavaScanWorker::doScan);
    connect(m_rescueScanWorker, &JavaScanWorker::scanProgress,
            this, &GameLauncher::onRescueScanProgress, Qt::QueuedConnection);
    connect(m_rescueScanWorker, &JavaScanWorker::scanFinished,
            this, &GameLauncher::onRescueScanFinished, Qt::QueuedConnection);
    connect(m_rescueScanWorker, &JavaScanWorker::scanFinished, scanThread, &QThread::quit);
    connect(scanThread, &QThread::finished, m_rescueScanWorker, &QObject::deleteLater);
    connect(scanThread, &QThread::finished, scanThread, &QObject::deleteLater);

    // 全盘深扫限时 120 秒：超时取消（取消后仍带回部分结果）并转入自动安装；
    // generation 守卫防止旧一轮的定时器误杀下一次启动触发的扫描
    const int generation = m_rescueGeneration;
    QTimer::singleShot(120000, this, [this, generation]() {
        if (m_rescueGeneration == generation && m_rescueStage == RescueStage::Scanning
            && m_rescueScanWorker)
        {
            emit launchDetailAdded(QStringLiteral("全盘查找超时，转为自动安装合适的 Java..."));
            m_rescueScanWorker->cancel();
        }
    });

    scanThread->start();
}

void GameLauncher::onRescueScanProgress(const QString& currentPath, int percent, int etaMinutes,
                                        int scannedMB, int totalMB, int foundCount)
{
    Q_UNUSED(currentPath);
    Q_UNUSED(etaMinutes);
    Q_UNUSED(scannedMB);
    Q_UNUSED(totalMB);
    if (m_rescueStage != RescueStage::Scanning) return;

    // 进度按 10% 粒度回显，避免刷屏
    const int bucket = (percent < 0) ? -1 : percent / 10;
    if (bucket == m_rescueLastProgressBucket) return;
    m_rescueLastProgressBucket = bucket;

    emit launchDetailAdded(percent >= 0
        ? QStringLiteral("正在查找Java: %1%（已发现 %2 个）").arg(percent).arg(foundCount)
        : QStringLiteral("正在查找Java...（已发现 %1 个）").arg(foundCount));
    emit launchProgressChanged(20 + qBound(0, percent, 100) / 12,
                               QStringLiteral("正在查找Java..."));
}

void GameLauncher::onRescueScanFinished(const QList<QPair<QString, QString>>& installations)
{
    m_rescueScanWorker = nullptr;
    if (m_rescueStage != RescueStage::Scanning) return;
    m_rescueStage = RescueStage::None;

    // 合并去重后写回 Java 缓存（与设置页「Java检测」的合并逻辑一致）
    QList<QPair<QString, QString>> existing = SettingsManager::instance()->getJavaInstallations();
    QSet<QString> seenPaths;
    for (const auto& e : existing) seenPaths.insert(e.first);
    int newCount = 0;
    for (const auto& inst : installations) {
        if (!seenPaths.contains(inst.first)) {
            existing.append(inst);
            seenPaths.insert(inst.first);
            newCount++;
        }
    }
    SettingsManager::instance()->setJavaInstallations(existing);
    emit launchDetailAdded(QString("Java 查找完成: 共发现 %1 个，新增 %2 个")
        .arg(installations.size()).arg(newCount));

    // 在合并后的缓存里按需求区间严格挑选
    //（不能用 findBestJavaVersion：它有"最高版本兜底"，不兼容的版本不能拿来用）
    const QString javaPath = selectRescueJava(cachedJavaInstallations());
    if (!javaPath.isEmpty())
    {
        emit launchDetailAdded(QString("已找到合适的 Java: %1，继续启动游戏").arg(javaPath));
        finishJavaRescue(javaPath);
        return;
    }

    startJavaAutoInstall();
}

QString GameLauncher::selectRescueJava(const QList<JavaInfo>& candidates)
{
    const JavaRequirement req = javaRequirementFor(m_rescueGameVersion, m_rescueJsonJavaMajor);
    emit launchDetailAdded(QString("正在筛选满足 %1 的 Java...").arg(req.rangeDesc()));

    // 与 findBestJavaVersion 相同的取舍：最低兼容主版本优先，
    // 同主版本再比 tier（真实 JVM > 转发存根、64 位 > 32 位）；
    // 缓存里的路径可能早已失效，每个候选都实际跑一次 java -version 验证
    QString bestPath;
    int bestMajor = -1;
    int bestTier = -1;
    for (const JavaInfo& info : candidates)
    {
        const int major = javaMajorOf(info.version);
        if (major < req.minJava || major > req.maxJava) continue;

        JavaInfo validated;
        if (!validateJavaPath(info.path, validated)) continue;

        const bool hasRealJvm = QFile::exists(serverJvmDllOf(info.path));
        const int bitness = javaBitness(info.path);
        int tier = 0;
        if (hasRealJvm) tier += 2;
        if (bitness == 64) tier += 1;

        if (bestMajor < 0 || major < bestMajor || (major == bestMajor && tier > bestTier))
        {
            bestPath = info.path;
            bestMajor = major;
            bestTier = tier;
        }
    }
    return bestPath;
}

void GameLauncher::startJavaAutoInstall()
{
    // 自动安装只尝试一次，避免查找/安装来回打转
    if (m_rescueInstallAttempted)
    {
        failJavaRescue(QStringLiteral("自动安装 Java 失败：无可用的安装方案"));
        return;
    }
    m_rescueInstallAttempted = true;

    // 用户正在手动下载 Java（下载页/设置页）时不能抢用同一个下载器。
    // 注意只能拦「真正在忙」的状态：Completed / Failed 是上一次下载的收尾状态，
    // 不会自己回到 Idle，按「不是 Idle 就拒绝」会把上次已结束的下载当成进行中，
    // 导致以后再也不会自动装 Java。
    const JavaDownloadStatus downloadStatus = JavaDownloader::instance()->status();
    if (downloadStatus == JavaDownloadStatus::FetchingVersions
        || downloadStatus == JavaDownloadStatus::FetchingBinaries
        || downloadStatus == JavaDownloadStatus::Downloading
        || downloadStatus == JavaDownloadStatus::Extracting)
    {
        failJavaRescue(QStringLiteral("检测到正在进行的 Java 下载任务，请等待其完成后再启动游戏"));
        return;
    }

    // ── 确定要安装的 Java 主版本 ──
    // 取发行版实际提供的主版本与需求区间的交集的最小值：
    // 例如 MC 1.17 需要 Java 16+，而各家都不再提供 16，就改装满足 16+ 的 17
    const JavaRequirement req = javaRequirementFor(m_rescueGameVersion, m_rescueJsonJavaMajor);
    const QVector<int> provided =
        JavaDownloader::instance()->supportedMajorVersions(QStringLiteral("adoptium"));
    int installMajor = 0;
    for (int major : provided)
    {
        if (major >= req.minJava && major <= req.maxJava)
        {
            if (installMajor == 0 || major < installMajor) installMajor = major;
        }
    }
    if (installMajor == 0)
    {
        failJavaRescue(QStringLiteral("没有适合游戏版本 %1 的可下载 Java（需要 %2）")
            .arg(m_rescueGameVersion, req.rangeDesc()));
        return;
    }
    m_rescueSelectedMajor = installMajor;

    m_rescueStage = RescueStage::Installing;
    emit launchDetailAdded(QString("未找到合适的 Java，正在自动安装 Java %1（Eclipse Adoptium），"
                                   "安装完成后将自动继续启动").arg(installMajor));
    emit launchProgressChanged(28, QString("正在下载 Java %1...").arg(installMajor));

    // 与「设置 → Java下载页」的默认选择一致：Adoptium Temurin + 清华 TUNA 镜像
    connectJavaDownloaderRescue();
    JavaDownloader::instance()->fetchAvailableBinaries(QStringLiteral("adoptium"),
                                                       QStringLiteral("tuna"),
                                                       installMajor);
}

void GameLauncher::connectJavaDownloaderRescue()
{
    JavaDownloader* dl = JavaDownloader::instance();
    m_rescueDownloaderConnections.clear();
    m_rescueDownloaderConnections
        << connect(dl, &JavaDownloader::versionListFetched, this,
                   [this](const QVector<JavaBinaryInfo>& binaries) {
                       onRescueVersionListFetched(binaries);
                   })
        << connect(dl, &JavaDownloader::fetchError,         this, &GameLauncher::onRescueFetchError)
        << connect(dl, &JavaDownloader::downloadProgress,   this, &GameLauncher::onRescueDownloadProgress)
        << connect(dl, &JavaDownloader::downloadBytesProgress, this, &GameLauncher::onRescueDownloadBytesProgress)
        << connect(dl, &JavaDownloader::downloadCompleted,  this, &GameLauncher::onRescueDownloadCompleted)
        << connect(dl, &JavaDownloader::downloadFailed,     this, &GameLauncher::onRescueDownloadFailed)
        << connect(dl, &JavaDownloader::extractProgress,    this, &GameLauncher::onRescueExtractProgress)
        << connect(dl, &JavaDownloader::extractCompleted,   this, &GameLauncher::onRescueExtractCompleted)
        << connect(dl, &JavaDownloader::extractFailed,      this, &GameLauncher::onRescueExtractFailed);
}

void GameLauncher::disconnectJavaDownloaderRescue()
{
    for (const QMetaObject::Connection& c : m_rescueDownloaderConnections)
        disconnect(c);
    m_rescueDownloaderConnections.clear();
}

void GameLauncher::retryOrFailRescueDownload(const QString& reason)
{
    if (!m_rescueOfficialMirrorRetried)
    {
        // TUNA 镜像拉取/下载失败 → 换官方源再试一次
        m_rescueOfficialMirrorRetried = true;
        emit launchDetailAdded(QString("清华镜像不可用（%1），改用官方源重试...").arg(reason));
        JavaDownloader::instance()->fetchAvailableBinaries(QStringLiteral("adoptium"),
                                                           QStringLiteral("official"),
                                                           m_rescueSelectedMajor);
        return;
    }
    failJavaRescue(QString("自动安装 Java 失败: %1").arg(reason));
}

void GameLauncher::onRescueVersionListFetched(const QVector<JavaBinaryInfo>& binaries)
{
    if (m_rescueStage != RescueStage::Installing) return;

    // 优先挑 JRE（体积小、够用），再挑与当前架构匹配的；都没有就取第一个
    const QString arch = JavaDownloader::instance()->currentArchitecture();
    const JavaBinaryInfo* chosen = nullptr;
    for (const JavaBinaryInfo& bin : binaries)
    {
        if (bin.packageType == QLatin1String("jre") && bin.architecture == arch) { chosen = &bin; break; }
    }
    if (!chosen)
    {
        for (const JavaBinaryInfo& bin : binaries)
        {
            if (bin.packageType == QLatin1String("jre")) { chosen = &bin; break; }
        }
    }
    if (!chosen && !binaries.isEmpty()) chosen = &binaries.first();

    if (!chosen || chosen->downloadUrl.isEmpty())
    {
        retryOrFailRescueDownload(QString("未找到可下载的 Java %1 安装包").arg(m_rescueSelectedMajor));
        return;
    }

    // 安装目录与「设置 → Java下载页」的默认安装路径一致，
    // 装在这里的 Java 会被启动器的 Java 扫描自动发现
    QString installDir = SettingsManager::instance()->getDefaultInstancePath();
    if (installDir.isEmpty())
        installDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    installDir += QStringLiteral("/java");

    emit launchDetailAdded(QString("开始下载 Java %1: %2").arg(m_rescueSelectedMajor)
                                                         .arg(chosen->versionName));
    JavaDownloader::instance()->downloadAndExtract(*chosen, installDir);
}

void GameLauncher::onRescueFetchError(const QString& error)
{
    if (m_rescueStage != RescueStage::Installing) return;
    retryOrFailRescueDownload(error);
}

void GameLauncher::onRescueDownloadProgress(const QString& fileName, int percent)
{
    Q_UNUSED(fileName);
    if (m_rescueStage != RescueStage::Installing) return;
    emit launchProgressChanged(28 + percent * 15 / 100,
        QString("正在下载 Java %1... %2%").arg(m_rescueSelectedMajor).arg(percent));
}

void GameLauncher::onRescueDownloadBytesProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (m_rescueStage != RescueStage::Installing) return;
    if (bytesTotal <= 0) return;
    const int percent = static_cast<int>(bytesReceived * 100 / bytesTotal);
    emit launchProgressChanged(28 + percent * 15 / 100,
        QString("正在下载 Java %1... %2%").arg(m_rescueSelectedMajor).arg(percent));
}

void GameLauncher::onRescueDownloadCompleted(const QString& filePath)
{
    Q_UNUSED(filePath);
    if (m_rescueStage != RescueStage::Installing) return;
    emit launchProgressChanged(43, QString("下载完成，正在解压安装 Java %1...").arg(m_rescueSelectedMajor));
    emit launchDetailAdded(QStringLiteral("下载完成，正在解压安装..."));
}

void GameLauncher::onRescueDownloadFailed(const QString& error)
{
    if (m_rescueStage != RescueStage::Installing) return;
    retryOrFailRescueDownload(error);
}

void GameLauncher::onRescueExtractProgress(int percent)
{
    if (m_rescueStage != RescueStage::Installing) return;
    emit launchProgressChanged(43 + percent * 7 / 100,
        QString("正在解压安装 Java %1...").arg(m_rescueSelectedMajor));
}

void GameLauncher::onRescueExtractCompleted(const QString& javaPath)
{
    if (m_rescueStage != RescueStage::Installing) return;
    disconnectJavaDownloaderRescue();
    m_rescueStage = RescueStage::None;

    // JavaDownloader::extractCompleted 发的是 Java Home 目录，可执行文件在其 bin/ 下
    QString javaExe = javaPath;
#ifdef Q_OS_WIN
    javaExe += QStringLiteral("/bin/java.exe");
#else
    javaExe += QStringLiteral("/bin/java");
#endif
    JavaInfo info;
    if (!validateJavaPath(javaExe, info))
    {
        failJavaRescue(QString("自动安装的 Java 验证失败: %1").arg(javaExe));
        return;
    }

    // 写入 Java 安装缓存，之后的启动就能自动选到这个 Java
    QList<QPair<QString, QString>> installations = SettingsManager::instance()->getJavaInstallations();
    bool exists = false;
    for (const auto& inst : installations)
    {
        if (inst.first == javaExe) { exists = true; break; }
    }
    if (!exists)
    {
        installations.append(qMakePair(javaExe, info.version));
        SettingsManager::instance()->setJavaInstallations(installations);
    }

    emit launchDetailAdded(QString("Java %1 安装成功: %2，继续启动游戏")
        .arg(info.version, javaExe));
    finishJavaRescue(javaExe);
}

void GameLauncher::onRescueExtractFailed(const QString& error)
{
    if (m_rescueStage != RescueStage::Installing) return;
    failJavaRescue(QString("自动安装 Java 失败（解压）: %1").arg(error));
}

void GameLauncher::finishJavaRescue(const QString& javaPath)
{
    m_rescueStage = RescueStage::None;
    proceedWithLaunch(m_rescueConfig, javaPath);
}

void GameLauncher::failJavaRescue(const QString& reason)
{
    disconnectJavaDownloaderRescue();
    m_rescueStage = RescueStage::None;
    m_errorMessage = reason;
    m_status = Failed;
    emit launchStatusChanged(m_status);
    emit launchDetailAdded(QString("错误: %1").arg(reason));
    // 与"Java 路径无效"失败路径保持一致的信号模式；
    // MainWindow 会因 wasJavaRescueAttempted 跳过文件补全重启，只把启动卡片标为失败
    emit gameCrashed(reason);
}