/**
 * @file   WorldConverter.cpp
 * @brief  Java 版 ⇄ 基岩版 存档互转核心实现（调用内置 chunker-cli）
 * @author BlockBox Team
 * @date   2026-10-17
 */
#include "WorldConverter.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include "utils/SettingsManager.h"
#include "utils/plugin/PluginZip.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace WorldConverter {

namespace {

const char kEngineVersion[] = "1.19.1";
// 与官方 release 资产 chunker-cli-1.19.1.jar 对应（HiveGamesOSS/Chunker tag 1.19.1）
const char kEngineSha256[] =
    "327662e8632acdb4571f60939206d605418ac0633741e1e5a58f5d6c6866dc74";
// 直连 GitHub 受限时的兜底镜像（与 UpdateChecker 相同前缀）
const char kGithubMirrorPrefix[] = "https://gh-proxy.com/";

/** 程序目录下所有可能放置引擎 jar 的目录（libs 在前） */
QStringList engineSearchDirs()
{
    QStringList dirs;
    dirs << WorldConverter::engineDir();
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty())
    {
        dirs << appDir + QStringLiteral("/libs")
             << appDir
             << appDir + QStringLiteral("/../libs");
    }
    const QString cwd = QDir::currentPath();
    if (!cwd.isEmpty() && cwd != appDir)
        dirs << cwd + QStringLiteral("/libs") << cwd;
    return dirs;
}

} // namespace

QString engineVersion()
{
    return QLatin1String(kEngineVersion);
}

QString engineDir()
{
    // 软件同级 BlockBox 文件夹（与插件目录同一约定）
    const QString appDir = QCoreApplication::applicationDirPath();
    QString dir;
    if (!appDir.isEmpty())
        dir = appDir + QStringLiteral("/BlockBox/world_convert");
    QDir d(dir);
    if (d.exists() || QDir().mkpath(dir))
        return dir;
    // 写入失败时回退到系统数据目录
    const QString fallback =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/world_convert");
    QDir().mkpath(fallback);
    return fallback;
}

QString engineJarPath()
{
    return engineDir() + QStringLiteral("/chunker-cli-%1.jar").arg(engineVersion());
}

QStringList engineDownloadUrls()
{
    const QString asset = QStringLiteral(
        "https://github.com/HiveGamesOSS/Chunker/releases/download/%1/chunker-cli-%1.jar")
        .arg(engineVersion());
    QStringList urls;
    urls << asset;
    urls << QLatin1String(kGithubMirrorPrefix) + asset;
    return urls;
}

QString engineSha256()
{
    return QLatin1String(kEngineSha256);
}

QStringList javaVersions()
{
    return QStringList()
        << QStringLiteral("1.8.8")
        << QStringLiteral("1.9") << QStringLiteral("1.9.1")
        << QStringLiteral("1.9.2") << QStringLiteral("1.9.3")
        << QStringLiteral("1.9.4")
        << QStringLiteral("1.10") << QStringLiteral("1.10.1")
        << QStringLiteral("1.10.2")
        << QStringLiteral("1.11") << QStringLiteral("1.11.1")
        << QStringLiteral("1.11.2")
        << QStringLiteral("1.12") << QStringLiteral("1.12.1")
        << QStringLiteral("1.12.2")
        << QStringLiteral("1.13") << QStringLiteral("1.13.1")
        << QStringLiteral("1.13.2")
        << QStringLiteral("1.14") << QStringLiteral("1.14.1")
        << QStringLiteral("1.14.2") << QStringLiteral("1.14.3")
        << QStringLiteral("1.14.4")
        << QStringLiteral("1.15") << QStringLiteral("1.15.1")
        << QStringLiteral("1.15.2")
        << QStringLiteral("1.16") << QStringLiteral("1.16.1")
        << QStringLiteral("1.16.2") << QStringLiteral("1.16.3")
        << QStringLiteral("1.16.4") << QStringLiteral("1.16.5")
        << QStringLiteral("1.17") << QStringLiteral("1.17.1")
        << QStringLiteral("1.18") << QStringLiteral("1.18.1")
        << QStringLiteral("1.18.2")
        << QStringLiteral("1.19") << QStringLiteral("1.19.1")
        << QStringLiteral("1.19.2") << QStringLiteral("1.19.3")
        << QStringLiteral("1.19.4")
        << QStringLiteral("1.20") << QStringLiteral("1.20.1")
        << QStringLiteral("1.20.2") << QStringLiteral("1.20.3")
        << QStringLiteral("1.20.4") << QStringLiteral("1.20.5")
        << QStringLiteral("1.20.6")
        << QStringLiteral("1.21") << QStringLiteral("1.21.1")
        << QStringLiteral("1.21.2") << QStringLiteral("1.21.3")
        << QStringLiteral("1.21.4") << QStringLiteral("1.21.5")
        << QStringLiteral("1.21.6") << QStringLiteral("1.21.7")
        << QStringLiteral("1.21.8") << QStringLiteral("1.21.9")
        << QStringLiteral("1.21.10") << QStringLiteral("1.21.11")
        << QStringLiteral("26.1") << QStringLiteral("26.1.1")
        << QStringLiteral("26.1.2") << QStringLiteral("26.2")
        << QStringLiteral("26.3");
}

QStringList bedrockVersions()
{
    return QStringList()
        << QStringLiteral("1.12")
        << QStringLiteral("1.13")
        << QStringLiteral("1.14") << QStringLiteral("1.14.20")
        << QStringLiteral("1.14.30") << QStringLiteral("1.14.60")
        << QStringLiteral("1.16") << QStringLiteral("1.16.20")
        << QStringLiteral("1.16.40") << QStringLiteral("1.16.100")
        << QStringLiteral("1.16.200") << QStringLiteral("1.16.210")
        << QStringLiteral("1.16.220")
        << QStringLiteral("1.17") << QStringLiteral("1.17.10")
        << QStringLiteral("1.17.30") << QStringLiteral("1.17.40")
        << QStringLiteral("1.18") << QStringLiteral("1.18.10")
        << QStringLiteral("1.18.30")
        << QStringLiteral("1.19") << QStringLiteral("1.19.10")
        << QStringLiteral("1.19.20") << QStringLiteral("1.19.30")
        << QStringLiteral("1.19.40") << QStringLiteral("1.19.50")
        << QStringLiteral("1.19.60") << QStringLiteral("1.19.70")
        << QStringLiteral("1.19.80")
        << QStringLiteral("1.20") << QStringLiteral("1.20.10")
        << QStringLiteral("1.20.30") << QStringLiteral("1.20.40")
        << QStringLiteral("1.20.50") << QStringLiteral("1.20.60")
        << QStringLiteral("1.20.70") << QStringLiteral("1.20.80")
        << QStringLiteral("1.21") << QStringLiteral("1.21.20")
        << QStringLiteral("1.21.30") << QStringLiteral("1.21.40")
        << QStringLiteral("1.21.50") << QStringLiteral("1.21.60")
        << QStringLiteral("1.21.70") << QStringLiteral("1.21.80")
        << QStringLiteral("1.21.90") << QStringLiteral("1.21.93")
        << QStringLiteral("1.21.100") << QStringLiteral("1.21.110")
        << QStringLiteral("1.21.120") << QStringLiteral("1.21.130")
        << QStringLiteral("1.26") << QStringLiteral("1.26.10")
        << QStringLiteral("1.26.20") << QStringLiteral("1.26.30")
        << QStringLiteral("1.26.40");
}

QString formatIdForJava(const QString &version)
{
    QString v = version;
    v.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("JAVA_") + v;
}

QString formatIdForBedrock(const QString &version)
{
    QString v = version;
    if (v.startsWith(QLatin1String("1.")))
        v = v.mid(2);
    v.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("BEDROCK_R") + v;
}

QString formatIdToLabel(const QString &formatId)
{
    if (formatId.startsWith(QLatin1String("JAVA_")))
    {
        QString v = formatId.mid(5);
        v.replace(QLatin1Char('_'), QLatin1Char('.'));
        return QStringLiteral("Java版 ") + v;
    }
    if (formatId.startsWith(QLatin1String("BEDROCK_R")))
    {
        QString v = formatId.mid(9);
        v.replace(QLatin1Char('_'), QLatin1Char('.'));
        return QStringLiteral("基岩版 1.") + v;
    }
    return formatId;
}

QString findEngineJar()
{
    const QString want = QStringLiteral("chunker-cli-%1.jar").arg(engineVersion());
    const QString prefix = QStringLiteral("chunker-cli-");
    const QString suffix = QStringLiteral(".jar");

    for (const QString &dir : engineSearchDirs())
    {
        QDir d(dir);
        if (!d.exists())
            continue;
        // 优先精确版本
        const QString exact = d.absoluteFilePath(want);
        if (QFileInfo::exists(exact))
            return exact;
        // 兼容其他版本号的 chunker-cli-*.jar
        const QStringList jars = d.entryList({QStringLiteral("chunker-cli-*.jar")},
                                             QDir::Files, QDir::Name);
        for (const QString &jar : jars)
        {
            if (jar.startsWith(prefix) && jar.endsWith(suffix))
                return d.absoluteFilePath(jar);
        }
    }
    return QString();
}

namespace {

/** 校验 java 可用且主版本 ≥ 17 */
bool testJavaUsable(const QString &javaPath, QString *versionText = nullptr)
{
    if (javaPath.isEmpty() || !QFileInfo::exists(javaPath))
        return false;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(javaPath, {QStringLiteral("-version")});
    if (!proc.waitForStarted(5000))
        return false;
    if (!proc.waitForFinished(15000))
    {
        proc.kill();
        return false;
    }
    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    const QString firstLine = out.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (versionText)
        *versionText = firstLine;

    // 版本形如：openjdk version "17.0.2" / java version "1.8.0_301"
    static const QRegularExpression re(QStringLiteral("version\\s+\"(\\d+)"));
    const auto m = re.match(out);
    if (!m.hasMatch())
        return false;
    int major = m.captured(1).toInt();
    // 1.8 这类旧格式取第二段
    if (major == 1)
    {
        static const QRegularExpression re2(QStringLiteral("version\\s+\"1\\.(\\d+)"));
        const auto m2 = re2.match(out);
        if (m2.hasMatch())
            major = m2.captured(1).toInt();
    }
    return major >= 17;
}

/** 常见 Java 安装根目录 */
QStringList commonJavaRoots()
{
    return QStringList()
#ifdef Q_OS_WIN
        << QStringLiteral("C:/Program Files/Java")
        << QStringLiteral("C:/Program Files/Eclipse Adoptium")
        << QStringLiteral("C:/Program Files/Microsoft")
        << QStringLiteral("C:/Program Files/Zulu")
        << QStringLiteral("C:/Program Files/BellSoft")
        << QStringLiteral("C:/Program Files/Amazon Corretto")
        << QStringLiteral("C:/Program Files/AdoptOpenJDK")
        << QStringLiteral("C:/Program Files (x86)/Java")
        << QStringLiteral("C:/Program Files (x86)/Microsoft")
#else
        << QStringLiteral("/usr/lib/jvm")
        << QStringLiteral("/usr/java")
#endif
        ;
}

void scanJavaExe(const QString &dirPath, int depth, QString *found)
{
    if (!found || !found->isEmpty() || depth < 0)
        return;
    QDir dir(dirPath);
    if (!dir.exists())
        return;
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                          QDir::Name);
    for (const QFileInfo &fi : entries)
    {
        if (!found->isEmpty())
            return;
        if (fi.isDir())
        {
            scanJavaExe(fi.absoluteFilePath(), depth - 1, found);
        }
        else if (fi.fileName() == QLatin1String("java") ||
                 fi.fileName() == QLatin1String("java.exe"))
        {
            if (testJavaUsable(fi.absoluteFilePath()))
            {
                *found = fi.absoluteFilePath();
                return;
            }
        }
    }
}

} // namespace

QString findJavaRuntime(QString *versionText)
{
    // 1. 启动器设置里保存的 Java
    const QString configured = SettingsManager::instance()->getJavaPath();
    if (!configured.isEmpty() && configured != QLatin1String("auto"))
    {
        QString v;
        if (testJavaUsable(configured, &v))
        {
            if (versionText)
                *versionText = v;
            return configured;
        }
        const QString nested = configured + QStringLiteral("/bin/java");
#ifdef Q_OS_WIN
        const QString nestedExe = configured + QStringLiteral("/bin/java.exe");
        if (testJavaUsable(nestedExe, &v))
        {
            if (versionText)
                *versionText = v;
            return nestedExe;
        }
#endif
        if (testJavaUsable(nested, &v))
        {
            if (versionText)
                *versionText = v;
            return nested;
        }
    }

    // 2. 常见安装目录（浅层扫描，避免过慢）
    for (const QString &root : commonJavaRoots())
    {
        QString found;
        scanJavaExe(root, 2, &found);
        if (!found.isEmpty())
        {
            if (versionText)
                testJavaUsable(found, versionText);
            return found;
        }
    }

    // 3. JAVA_HOME
    const QByteArray javaHome = qgetenv("JAVA_HOME");
    if (!javaHome.isEmpty())
    {
#ifdef Q_OS_WIN
        const QString cand =
            QString::fromLocal8Bit(javaHome) + QStringLiteral("/bin/java.exe");
#else
        const QString cand =
            QString::fromLocal8Bit(javaHome) + QStringLiteral("/bin/java");
#endif
        QString v;
        if (testJavaUsable(cand, &v))
        {
            if (versionText)
                *versionText = v;
            return cand;
        }
    }

    // 4. PATH
    const QStringList pathDirs =
        QString::fromLocal8Bit(qgetenv("PATH")).split(QDir::listSeparator(),
                                                     Qt::SkipEmptyParts);
    for (const QString &dir : pathDirs)
    {
#ifdef Q_OS_WIN
        const QString cand = dir + QStringLiteral("/java.exe");
#else
        const QString cand = dir + QStringLiteral("/java");
#endif
        QString v;
        if (testJavaUsable(cand, &v))
        {
            if (versionText)
                *versionText = v;
            return cand;
        }
    }
    return QString();
}

WorldEdition detectWorldEdition(const QString &worldDir)
{
    QDir dir(worldDir);
    if (!dir.exists())
        return WorldEdition::Unknown;
    if (dir.exists(QLatin1String("db")))
        return WorldEdition::Bedrock;
    if (dir.exists(QLatin1String("region")) || dir.exists(QLatin1String("playerdata"))
        || dir.exists(QLatin1String("data")) || dir.exists(QLatin1String("poi")))
        return WorldEdition::Java;
    return WorldEdition::Unknown;
}

QString checkOutputDir(const QString &outputDir)
{
    if (outputDir.trimmed().isEmpty())
        return QStringLiteral("输出路径为空");
    const QFileInfo fi(outputDir);
    if (fi.exists())
    {
        if (!fi.isDir())
            return QStringLiteral("输出路径已存在同名文件，请选择新的文件夹");
        const QFileInfoList children = QDir(outputDir).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        if (!children.isEmpty())
            return QStringLiteral("输出文件夹非空，请选择空文件夹或新路径");
    }
    else
    {
        const QString parent = fi.absolutePath();
        if (!QDir(parent).exists())
            return QStringLiteral("输出路径的上级目录不存在");
    }
    return QString();
}

QString packageWorldDir(const QString &dir, Direction direction, QString *errorOut)
{
    const QString ext =
        (direction == Direction::JavaToBedrock) ? QStringLiteral(".mcworld")
                                                : QStringLiteral(".zip");
    QString target = dir;
    if (!target.endsWith(ext, Qt::CaseInsensitive))
        target += ext;

    if (QFileInfo::exists(target))
        QFile::remove(target);

    bool ok = false;
    const int count = PluginZip::zipDirectory(dir, target, &ok);
    if (!ok || count < 0)
    {
        if (errorOut)
            *errorOut = QStringLiteral("打包压缩包失败");
        return QString();
    }
    return target;
}

} // namespace WorldConverter

// ============================================================
// WorldConvertTask
// ============================================================

using WorldConverter::WorldConvertTask;
using WorldConverter::WorldEngineDownloader;

WorldConvertTask::WorldConvertTask(QObject *parent)
    : QObject(parent)
{
}

WorldConvertTask::~WorldConvertTask()
{
    cancel();
    if (!m_settingsFile.isEmpty() && QFileInfo::exists(m_settingsFile))
        QFile::remove(m_settingsFile);
    if (!m_tempDir.isEmpty())
        QDir(m_tempDir).removeRecursively();
}

bool WorldConvertTask::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

QString WorldConvertTask::writeSettingsFile()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("discardEmptyChunks"), m_request.options.discardEmpty);
    obj.insert(QStringLiteral("enableCompact"), m_request.options.enableCompact);
    obj.insert(QStringLiteral("mapConversion"), m_request.options.keepContent);
    obj.insert(QStringLiteral("itemConversion"), m_request.options.keepContent);
    obj.insert(QStringLiteral("blockConnections"), m_request.options.keepContent);
    obj.insert(QStringLiteral("lootTableConversion"), m_request.options.keepContent);
    obj.insert(QStringLiteral("customIdentifiers"), true);
    if (m_request.options.preventYBlend)
        obj.insert(QStringLiteral("preventYBiomeBlending"), true);

    m_tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/BlockBoxWorldConvert");
    QDir().mkpath(m_tempDir);
    const QString file = m_tempDir
        + QStringLiteral("/converter_settings_%1.json")
              .arg(QCoreApplication::applicationPid());

    QSaveFile sf(file);
    if (!sf.open(QIODevice::WriteOnly))
        return QString();
    sf.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    if (!sf.commit())
        return QString();
    return file;
}

void WorldConvertTask::start(const Request &request)
{
    if (isRunning())
        return;

    m_request = request;
    m_doneMarkerSeen = false;
    m_finishing = false;

    m_settingsFile = writeSettingsFile();

    QStringList args;
    args << QStringLiteral("-Dfile.encoding=UTF-8");

    // 按物理内存选择堆大小，降低大存档 OOM（退出码 12）概率
    QString heap = QStringLiteral("2g");
#ifdef Q_OS_WIN
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
    {
        const quint64 totalGb = ms.ullTotalPhys / (1024ull * 1024ull * 1024ull);
        if (totalGb >= 16)
            heap = QStringLiteral("6g");
        else if (totalGb >= 8)
            heap = QStringLiteral("4g");
    }
#endif
    args << QStringLiteral("-Xmx") + heap;
    args << QStringLiteral("-jar") << m_request.jarPath;
    args << QStringLiteral("-i") << m_request.inputWorldDir;
    args << QStringLiteral("-f") << m_request.formatId;
    args << QStringLiteral("-o") << m_request.outputDir;
    if (!m_settingsFile.isEmpty())
    {
        args << QStringLiteral("-c") << m_settingsFile;
    }

    QDir().mkpath(m_request.outputDir);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &WorldConvertTask::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &WorldConvertTask::onReadyReadStandardError);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &WorldConvertTask::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &WorldConvertTask::onProcessError);

    emit statusChanged(tr("正在启动转换引擎…"));
    m_process->start(m_request.javaExe, args);
}

void WorldConvertTask::cancel()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
    {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

void WorldConvertTask::onReadyReadStandardOutput()
{
    if (!m_process)
        return;
    while (m_process->canReadLine())
        handleLine(QString::fromUtf8(m_process->readLine()).trimmed());
}

void WorldConvertTask::onReadyReadStandardError()
{
    if (!m_process)
        return;
    while (m_process->canReadLine())
        handleLine(QString::fromLocal8Bit(m_process->readLine()).trimmed());
}

void WorldConvertTask::handleLine(const QString &line)
{
    if (line.isEmpty())
        return;

    // 纯百分比行：如 "42.0%"
    static const QRegularExpression pctRe(
        QStringLiteral("^\\d+(\\.\\d+)?%$"));
    if (pctRe.match(line).hasMatch())
    {
        const double v = line.chopped(1).toDouble();
        emit progress(qBound(0, static_cast<int>(v), 100));
        return;
    }

    if (line.startsWith(QLatin1String("Converting from")))
    {
        emit statusChanged(line);
        emit logLine(line);
        return;
    }
    if (line.contains(QLatin1String("Compacting world")))
    {
        emit statusChanged(tr("正在压缩输出世界，请稍候…"));
        emit progress(98);
        return;
    }
    if (line.contains(QLatin1String("Finished compacting world")))
    {
        emit statusChanged(tr("输出压缩完成"));
        return;
    }
    if (line.contains(QLatin1String("Conversion complete")))
    {
        m_doneMarkerSeen = true;
        emit statusChanged(tr("转换完成"));
        emit logLine(line);
        return;
    }
    emit logLine(line);
}

void WorldConvertTask::onProcessFinished(int exitCode, QProcess::ExitStatus)
{
    // 收尾读取残留输出
    if (m_process)
    {
        const QByteArray restOut = m_process->readAllStandardOutput();
        const QStringList outLines =
            QString::fromUtf8(restOut).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &l : outLines)
            handleLine(l.trimmed());
        const QByteArray restErr = m_process->readAllStandardError();
        const QStringList errLines =
            QString::fromLocal8Bit(restErr).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &l : errLines)
            handleLine(l.trimmed());
    }

    if (exitCode == 12)
    {
        finishInternal(false, exitCode,
                       tr("转换内存不足（OOM），请关闭部分程序后重试；超大存档建议拆分转换。"));
        return;
    }
    if (exitCode != 0)
    {
        finishInternal(false, exitCode,
                       tr("转换失败（退出码 %1），请查看日志。").arg(exitCode));
        return;
    }
    if (!m_doneMarkerSeen)
    {
        finishInternal(false, exitCode,
                       tr("进程正常退出但未检测到完成信息，输出可能不完整。"));
        return;
    }
    finishInternal(true, exitCode, tr("转换完成"));
}

void WorldConvertTask::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart)
    {
        finishInternal(false, -1,
                       tr("无法启动转换引擎，请确认 Java 17+ 可用且引擎文件完整。"));
    }
    // Crashed 由 finished 信号统一处理
}

void WorldConvertTask::finishInternal(bool ok, int exitCode, const QString &message)
{
    if (m_finishing)
        return;
    m_finishing = true;

    QString outputPath = m_request.outputDir;
    if (ok)
    {
        // 打包为 .mcworld / .zip（可选）
        if (m_request.options.packageOutput)
        {
            emit statusChanged(tr("正在打包…"));
            QString packErr;
            const QString archive =
                WorldConverter::packageWorldDir(m_request.outputDir,
                                                m_request.direction, &packErr);
            if (!archive.isEmpty())
                emit logLine(tr("已打包：%1").arg(archive));
            else if (!packErr.isEmpty())
                emit logLine(tr("[警告] %1").arg(packErr));
        }
        emit progress(100);
        outputPath = m_request.outputDir;
    }

    // 清理临时设置文件
    if (!m_settingsFile.isEmpty())
    {
        QFile::remove(m_settingsFile);
        m_settingsFile.clear();
    }

    emit finished(ok, exitCode, outputPath, message);
}

// ============================================================
// WorldEngineDownloader
// ============================================================

WorldEngineDownloader::WorldEngineDownloader(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

WorldEngineDownloader::~WorldEngineDownloader()
{
    cancel();
}

bool WorldEngineDownloader::isRunning() const
{
    return m_reply != nullptr;
}

void WorldEngineDownloader::start()
{
    if (isRunning() || m_finishing)
        return;

    // 已存在且校验通过：直接成功
    const QString target = WorldConverter::engineJarPath();
    if (QFileInfo::exists(target))
    {
        QFile f(target);
        if (f.open(QIODevice::ReadOnly))
        {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (hash.addData(&f) && QString::fromLatin1(hash.result().toHex())
                    == WorldConverter::engineSha256())
            {
                finishWith(true, tr("转换引擎已就绪。"), target);
                return;
            }
        }
        QFile::remove(target);
    }

    m_urls = WorldConverter::engineDownloadUrls();
    m_urlIndex = -1;
    m_cancelled = false;
    m_finishing = false;
    m_tempFile = WorldConverter::engineDir()
        + QStringLiteral("/chunker-cli-%1.jar.download").arg(
            WorldConverter::engineVersion());
    tryNextUrl();
}

void WorldEngineDownloader::tryNextUrl()
{
    if (m_cancelled)
    {
        finishWith(false, tr("已取消下载。"));
        return;
    }
    ++m_urlIndex;
    if (m_urlIndex >= m_urls.size())
    {
        finishWith(false, tr("转换引擎下载失败，请检查网络后重试（可更换网络或使用代理）。"));
        return;
    }

    const QUrl url(m_urls.at(m_urlIndex));
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("BlockBox/%1").arg(
                      QCoreApplication::applicationVersion()));

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            &WorldEngineDownloader::progress);
    connect(m_reply, &QNetworkReply::finished,
            this, &WorldEngineDownloader::onReplyFinished);
}

void WorldEngineDownloader::onReplyFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    if (m_cancelled)
    {
        if (!m_tempFile.isEmpty())
            QFile::remove(m_tempFile);
        finishWith(false, tr("已取消下载。"));
        return;
    }

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool netOk = (reply->error() == QNetworkReply::NoError);
    const QByteArray body = netOk ? reply->readAll() : QByteArray();

    if (!netOk || httpStatus >= 400 || body.size() < 1024 * 1024)
    {
        // 失败：换下一个镜像
        tryNextUrl();
        return;
    }

    // 写临时文件并校验 SHA256
    {
        QFile out(m_tempFile);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || out.write(body) != body.size())
        {
            finishWith(false, tr("写入引擎文件失败，请检查磁盘空间。"));
            return;
        }
        out.close();
    }

    {
        QFile in(m_tempFile);
        if (!in.open(QIODevice::ReadOnly))
        {
            finishWith(false, tr("读取引擎文件失败。"));
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&in)
            || QString::fromLatin1(hash.result().toHex())
                   != WorldConverter::engineSha256())
        {
            QFile::remove(m_tempFile);
            tryNextUrl();
            return;
        }
    }

    const QString target = WorldConverter::engineJarPath();
    if (QFileInfo::exists(target))
        QFile::remove(target);
    if (!QFile::rename(m_tempFile, target))
    {
        // rename 跨设备可能失败，退回复制
        if (QFile::copy(m_tempFile, target))
            QFile::remove(m_tempFile);
        else
        {
            finishWith(false, tr("无法保存引擎文件到：%1").arg(target));
            return;
        }
    }
    finishWith(true, tr("转换引擎下载完成。"), target);
}

void WorldEngineDownloader::cancel()
{
    m_cancelled = true;
    if (m_reply)
    {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void WorldEngineDownloader::finishWith(bool ok, const QString &message,
                                       const QString &jarPath)
{
    if (m_finishing)
        return;
    m_finishing = true;
    emit finished(ok, message, jarPath);
    m_finishing = false;
}
