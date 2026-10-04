#include "ForgeInstaller.h"
#include "ForgeDownloader.h"
#include "ForgeNewInstallTask.h"
#include "ForgeProfile.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QUuid>
#include <QProcessEnvironment>
#include <QtConcurrent/QtConcurrent>

#include "../SettingsManager.h"
#include "../DownloadTaskManager.h"

ForgeInstaller *ForgeInstaller::m_instance = nullptr;

ForgeInstaller* ForgeInstaller::instance()
{
    if (!m_instance) {
        m_instance = new ForgeInstaller();
    }
    return m_instance;
}

ForgeInstaller::ForgeInstaller(QObject *parent)
    : QObject(parent)
    , m_downloader(new forge::ForgeDownloader(this))
    , m_installTask(nullptr)
    , m_isInstalling(false)
    , m_versionIsolationEnabled(false)
{
    ForgeDownloadSource source = SettingsManager::instance()->getForgeDownloadSource();
    m_downloader->setDownloadSource(
        source == ForgeDownloadSource::Official ? forge::DownloadSource::Official : forge::DownloadSource::BMCL);
}

ForgeInstaller::~ForgeInstaller()
{
    cancelInstall();

    if (!m_tempDir.isEmpty())
    {
        QDir tempDir(m_tempDir);
        if (tempDir.exists())
        {
            tempDir.removeRecursively();
        }
    }
}

void ForgeInstaller::setCurrentTaskId(const QString &taskId)
{
    m_taskId = taskId;
}

void ForgeInstaller::downloadForgeInstaller(const QString &mcVersion, const QString &forgeVersion,
                                              const QString &instancePath, const QString &instanceName)
{
    Q_UNUSED(instanceName);
    m_mcVersion = mcVersion;
    m_forgeVersion = forgeVersion;
    m_instancePath = instancePath;
    startInstall();
}

void ForgeInstaller::setInstancePath(const QString &path)
{
    m_instancePath = path;
}

void ForgeInstaller::setMcVersion(const QString &version)
{
    m_mcVersion = version;
}

void ForgeInstaller::setForgeVersion(const QString &version)
{
    m_forgeVersion = version;
}

void ForgeInstaller::setTaskId(const QString &taskId)
{
    m_taskId = taskId;
}

QString ForgeInstaller::taskId() const
{
    return m_taskId;
}

void ForgeInstaller::setVersionIsolationEnabled(bool enabled)
{
    m_versionIsolationEnabled = enabled;
}

void ForgeInstaller::startInstall()
{
    if (m_isInstalling)
    {
        emit installFailed(m_taskId, tr("安装器正忙"));
        return;
    }

    m_isInstalling = true;

    m_tempDir = QDir::tempPath() + "/BlockBox_Forge_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir tempDir(m_tempDir);
    if (!tempDir.exists())
    {
        tempDir.mkpath(".");
    }

    QStringList candidateUrls = m_downloader->getInstallerCandidateUrls(m_mcVersion, m_forgeVersion);
    m_installerJarPath = m_tempDir + "/forge-installer.jar";

    emit installStarted(m_taskId);
    emit installProgress(m_taskId, tr("正在下载Forge安装包"), 0);
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在下载Forge安装包"));
        // 参考 HMCL：不重置进度，Forge 阶段从 60% 起步
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 60);

    QMetaObject::Connection progressConn = connect(m_downloader, &forge::ForgeDownloader::downloadBytesProgress, this,
        [this](qint64 bytesReceived, qint64 bytesTotal)
        {
            if (!m_taskId.isEmpty())
            {
                DownloadTaskManager::instance()->updateTaskFileProgress(
                    m_taskId, "forge-installer.jar", bytesReceived, bytesTotal);
            }
        });

    QMetaObject::Connection percentConn = connect(m_downloader, &forge::ForgeDownloader::downloadProgress, this,
        [this](const QString &fileName, int percent)
        {
            Q_UNUSED(fileName);
            if (!m_taskId.isEmpty())
            {
                // 安装器 JAR 下载进度 → 映射到总体百分比 60-90%
                DownloadTaskManager::instance()->updateTaskProgressPercent(
                    m_taskId, 60 + percent * 30 / 100);
            }
        });

    // 下载超时看门狗（参考 HMCL 做法：2 分钟整体超时）
    QTimer *downloadTimeout = new QTimer(this);
    downloadTimeout->setSingleShot(true);
    connect(downloadTimeout, &QTimer::timeout, this, [this, downloadTimeout]()
    {
        qWarning() << "[ForgeInstaller] 安装器JAR下载超时（2分钟），取消下载";
        m_downloader->cancelAll();
        downloadTimeout->deleteLater();
        m_isInstalling = false;
        emit installFailed(m_taskId, tr("下载Forge安装包超时，请检查网络连接"));
    });
    downloadTimeout->start(2 * 60 * 1000);

    m_downloader->downloadFile(candidateUrls, m_installerJarPath,
                              [this, progressConn, percentConn, downloadTimeout](bool success)
                              {
                                  disconnect(progressConn);
                                  disconnect(percentConn);
                                  downloadTimeout->stop();
                                  downloadTimeout->deleteLater();
                                  onInstallerDownloaded(success);
                              });
}

void ForgeInstaller::onInstallerDownloaded(bool success)
{
    if (!success)
    {
        m_isInstalling = false;
        emit installFailed(m_taskId, tr("下载Forge安装包失败"));
        return;
    }

    DownloadTaskManager::instance()->updateTaskStageProgress(m_taskId, DownloadStage::LoaderInstall, 0);
    DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                      tr("正在安装Forge"));

    runInstallerDirectly();
}

void ForgeInstaller::onInstallerTypeDetected(int installerType)
{
    if (installerType == 0)
    {
        DownloadTaskManager::instance()->updateTaskStageProgress(m_taskId, DownloadStage::LoaderInstall, 0);
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在安装Forge"));

        runInstallerDirectly();
    }
    else if (installerType == 1)
    {
        m_isInstalling = false;
        emit installFailed(m_taskId, tr("不支持旧版Forge安装器，请使用旧版安装方式"));
    }
    else
    {
        m_isInstalling = false;
        emit installFailed(m_taskId, tr("无法识别Forge安装器类型"));
    }
}

void ForgeInstaller::runInstallerDirectly()
{
    // 安装器初始化 → 从 91% 开始
    DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 91);

    QString javaPath = getJavaPath();
    if (javaPath.isEmpty())
    {
        emit installFailed(m_taskId, tr("未找到Java运行环境"));
        m_isInstalling = false;
        return;
    }

    // Forge installer expects the Minecraft directory (containing versions/ and libraries/)
    QString minecraftDir;
    if (m_versionIsolationEnabled)
    {
        // 版本隔离开启时 m_instancePath = <root>/versions/<versionId>
        // 需要回到包含 versions/ 和 libraries/ 的版本目录
        minecraftDir = QDir::cleanPath(QFileInfo(m_instancePath).dir().absolutePath() + "/../..");
    }
    else
    {
        // 非隔离模式：m_instancePath = <root>/versions/<versionId>
        // 需要回到根目录以使用共享的 libraries/ 和 assets/
        minecraftDir = QDir::cleanPath(m_instancePath + "/../..");
    }

    // 验证 Minecraft 目录结构
    QDir versionsDir(minecraftDir + "/versions");
    if (!versionsDir.exists())
    {
        emit installFailed(m_taskId, tr("Minecraft目录结构不完整，请先下载原版"));
        m_isInstalling = false;
        return;
    }

    QProcess *process = new QProcess(this);
    process->setWorkingDirectory(minecraftDir);
    process->setProcessChannelMode(QProcess::MergedChannels);

    // 进度计时器：每 8 秒 +1%，从 91% 到 99%
    auto installProgressPercent = std::make_shared<int>(91);
    auto progressTimer = new QTimer(this);
    progressTimer->setInterval(8000);
    connect(progressTimer, &QTimer::timeout, this, [this, installProgressPercent]()
    {
        if (*installProgressPercent < 99)
        {
            (*installProgressPercent)++;
            DownloadTaskManager::instance()->updateTaskProgressPercent(
                m_taskId, *installProgressPercent);
        }
    });

    // 10 分钟超时保护
    auto timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, [this, process, progressTimer]()
    {
        progressTimer->stop();
        process->kill();
        process->deleteLater();
        emit installFailed(m_taskId, tr("Forge安装超时（超过10分钟），请检查网络连接"));
    });
    timeoutTimer->start(10 * 60 * 1000);

    // 共用进度解析逻辑（stdout 和 stderr 合并为 MergedChannels）
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process, installProgressPercent]()
    {
        QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        if (output.isEmpty()) return;

        const QStringList progressKeywords = {
            "Considering library", "Downloading library", "Extracting",
            "Processing", "Installing", "Maven", "MinecraftForge",
            "Installer", "library", "Downloaded"
        };

        bool matched = false;
        for (const QString &kw : progressKeywords)
        {
            if (output.contains(kw, Qt::CaseInsensitive))
            {
                matched = true;
                break;
            }
        }

        if (matched)
        {
            *installProgressPercent = qMin(99, *installProgressPercent + 2);
        }

        emit installProgress(m_taskId, output, *installProgressPercent);
        DownloadTaskManager::instance()->updateTaskProgressPercent(
            m_taskId, *installProgressPercent);
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, progressTimer, timeoutTimer](int exitCode, QProcess::ExitStatus exitStatus)
    {
        progressTimer->stop();
        timeoutTimer->stop();
        process->deleteLater();

        if (exitStatus == QProcess::NormalExit && exitCode == 0)
        {
            emit installProgress(m_taskId, tr("Forge安装完成"), 100);
            DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Completed,
                                                              tr("Forge安装完成"));
            DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 100);
            emit installCompleted(m_taskId);
        }
        else
        {
            QString errorOutput = QString::fromUtf8(process->readAllStandardOutput());
            QString errorMsg = tr("Forge安装失败 (退出码: %1)").arg(exitCode);
            if (!errorOutput.isEmpty())
            {
                errorMsg += "\n" + errorOutput.left(500);
            }
            emit installFailed(m_taskId, errorMsg);
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, progressTimer, timeoutTimer](QProcess::ProcessError error)
    {
        Q_UNUSED(error);
        progressTimer->stop();
        timeoutTimer->stop();
        process->deleteLater();
        emit installFailed(m_taskId, tr("无法启动Forge安装器"));
    });

    // JVM 参数参考 HMCL/PCL 标准做法
    QStringList args;
    args << "-Djava.net.preferIPv4Stack=true"
         << "-jar" << m_installerJarPath
         << "--installClient" << minecraftDir;

    qDebug() << "[ForgeInstaller]" << "Starting:" << javaPath << args;

    process->start(javaPath, args);
    progressTimer->start();
}

void ForgeInstaller::onNewInstallCompleted(bool success, const QString &error)
{
    m_isInstalling = false;

    if (!m_tempDir.isEmpty())
    {
        QDir tempDir(m_tempDir);
        if (tempDir.exists())
        {
            tempDir.removeRecursively();
        }
    }

    if (success)
    {
        emit installProgress(m_taskId, tr("Forge安装完成"), 100);
            DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Completed,
                                                              tr("Forge安装完成"));
            DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 100);
            emit installCompleted(m_taskId);
    }
    else
    {
        emit installFailed(m_taskId, error);
    }
}

QString ForgeInstaller::getJavaPath()
{
    QString javaPath = "java";

    if (QFile::exists(javaPath))
    {
        return javaPath;
    }

    QString javaHome = qEnvironmentVariable("JAVA_HOME");
    if (!javaHome.isEmpty())
    {
        QString javaExe = javaHome + "/bin/java";
#ifdef Q_OS_WIN
        javaExe += ".exe";
#endif
        if (QFile::exists(javaExe))
        {
            return javaExe;
        }
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = env.value("PATH");
    const QStringList paths = path.split(QDir::listSeparator());

    for (const QString &p : paths)
    {
        QString javaExe = p + "/java";
#ifdef Q_OS_WIN
        javaExe += ".exe";
#endif
        if (QFile::exists(javaExe))
        {
            return javaExe;
        }
    }

    return QString();
}

QString ForgeInstaller::getJarToolPath()
{
    QString javaPath = SettingsManager::instance()->getJavaPath();

    if (javaPath.isEmpty())
    {
        javaPath = qEnvironmentVariable("JAVA_HOME");
        if (!javaPath.isEmpty())
        {
#ifdef Q_OS_WIN
            javaPath += "/bin/java.exe";
#else
            javaPath += "/bin/java";
#endif
        }
        else
        {
#ifdef Q_OS_WIN
            javaPath = "java.exe";
#else
            javaPath = "java";
#endif
        }
    }

#ifdef Q_OS_WIN
    javaPath.replace("java.exe", "jar.exe");
    javaPath.replace("javaw.exe", "jar.exe");
#else
    javaPath.replace("java", "jar");
#endif

    return javaPath;
}

QByteArray ForgeInstaller::decompressRawDeflate(const QByteArray &compressedData, quint32 uncompressedSize)
{
    // Qt qUncompress() requires the data to be in zlib format with
    // a 4-byte big-endian uncompressed size header at the beginning.
    // The input is raw deflate data, so we need to prepend the header.
    //
    // Reference: https://doc.qt.io/qt-6/qbytearray.html#qUncompress
    QByteArray input;
    input.append(static_cast<char>((uncompressedSize >> 24) & 0xFF));
    input.append(static_cast<char>((uncompressedSize >> 16) & 0xFF));
    input.append(static_cast<char>((uncompressedSize >> 8) & 0xFF));
    input.append(static_cast<char>(uncompressedSize & 0xFF));
    input.append(compressedData);

    return qUncompress(input);
}

bool ForgeInstaller::extractEntryFromJar(const QString &jarPath, const QString &entryPath,
                                         const QString &destPath)
{
    if (tryExtractWithJarTool(jarPath, entryPath, destPath))
    {
        return true;
    }

#ifdef Q_OS_WIN
    if (tryExtractWithPowerShell(jarPath, entryPath, destPath))
    {
        return true;
    }
#else
    if (tryExtractWithUnzip(jarPath, entryPath, destPath))
    {
        return true;
    }
#endif

    if (tryExtractWithPython(jarPath, entryPath, destPath))
    {
        return true;
    }

    if (tryExtractManual(jarPath, entryPath, destPath))
    {
        return true;
    }

    qWarning() << "[ForgeInstaller]" << "All extraction methods failed for"
               << "jar:" << jarPath << "entry:" << entryPath;
    return false;
}

bool ForgeInstaller::tryExtractWithJarTool(const QString &jarPath, const QString &entryPath,
                                           const QString &destPath)
{
    QString jarToolPath = getJarToolPath();
    if (jarToolPath.isEmpty())
    {
        qWarning() << "[ForgeInstaller] jar.exe not found (JDK required)";
        return false;
    }

    QProcess process;
    QStringList args;
    args << "-xf" << jarPath << entryPath;
    process.setWorkingDirectory(QFileInfo(destPath).absolutePath());
    process.start(jarToolPath, args);

    if (!process.waitForFinished(30000))
    {
        qWarning() << "[ForgeInstaller] jar.exe timed out";
        return false;
    }

    if (process.exitCode() != 0)
    {
        qWarning() << "[ForgeInstaller] jar.exe failed, exit code:" << process.exitCode();
        return false;
    }

    QString extractedPath = QFileInfo(destPath).absolutePath() + "/" + entryPath;
    if (!QFile::exists(extractedPath))
    {
        return false;
    }

    if (extractedPath != destPath)
    {
        QFile::remove(destPath);
        QFile::rename(extractedPath, destPath);
    }

    return true;
}

bool ForgeInstaller::tryExtractWithPowerShell(const QString &jarPath, const QString &entryPath,
                                              const QString &destPath)
{
    QString script = QString(
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zip = [System.IO.Compression.ZipFile]::OpenRead('%1');"
        "$entry = $zip.GetEntry('%2');"
        "if ($entry -eq $null) { exit 1; }"
        "$stream = $entry.Open();"
        "$destDir = Split-Path '%3' -Parent;"
        "if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null; }"
        "$fs = [System.IO.File]::Create('%3');"
        "$stream.CopyTo($fs);"
        "$fs.Close();"
        "$stream.Close();"
        "$zip.Dispose();"
    ).arg(jarPath, entryPath, destPath);

    QProcess process;
    process.start("powershell", QStringList() << "-NoProfile" << "-Command" << script);

    if (!process.waitForFinished(30000))
    {
        qWarning() << "[ForgeInstaller] PowerShell timed out";
        return false;
    }

    if (process.exitCode() != 0)
    {
        qWarning() << "[ForgeInstaller] PowerShell failed, exit code:" << process.exitCode();
        return false;
    }

    if (!QFile::exists(destPath))
    {
        qWarning() << "[ForgeInstaller] PowerShell extraction succeeded but output file not found";
        return false;
    }

    return true;
}

bool ForgeInstaller::tryExtractWithPython(const QString &jarPath, const QString &entryPath,
                                          const QString &destPath)
{
    QStringList pythonCommands = {"python3", "python"};

    for (const QString &pythonCmd : pythonCommands)
    {
        QString script = QString(
            "import zipfile, sys, os\n"
            "os.makedirs(os.path.dirname(r'%3'), exist_ok=True)\n"
            "with zipfile.ZipFile(r'%1') as z:\n"
            "    z.extract(r'%2', os.path.dirname(r'%3'))\n"
            "src = os.path.join(os.path.dirname(r'%3'), r'%2')\n"
            "if src != r'%3':\n"
            "    os.replace(src, r'%3')\n"
        ).arg(jarPath, entryPath, destPath);

        QProcess process;
        process.start(pythonCmd, QStringList() << "-c" << script);

        if (!process.waitForFinished(15000))
        {
            qWarning() << "[ForgeInstaller] Python timed out:" << pythonCmd;
            continue;
        }

        if (process.exitCode() != 0)
        {
            qWarning() << "[ForgeInstaller] Python failed:" << pythonCmd
                       << "exit code:" << process.exitCode();
            continue;
        }

        if (QFile::exists(destPath) && QFileInfo(destPath).size() > 0)
        {
            return true;
        }
    }

    qWarning() << "[ForgeInstaller] Python not available or extraction failed";
    return false;
}

bool ForgeInstaller::tryExtractWithUnzip(const QString &jarPath, const QString &entryPath,
                                         const QString &destPath)
{
    QProcess process;
    QStringList args;
    args << "-p" << jarPath << entryPath;
    process.setStandardOutputFile(destPath);
    process.start("unzip", args);

    if (!process.waitForFinished(30000))
    {
        return false;
    }

    if (process.exitCode() != 0)
    {
        return false;
    }

    return QFile::exists(destPath) && QFileInfo(destPath).size() > 0;
}

bool ForgeInstaller::tryExtractManual(const QString &jarPath, const QString &entryPath,
                                      const QString &destPath)
{
    QFile jarFile(jarPath);
    if (!jarFile.open(QIODevice::ReadOnly))
    {
        qWarning() << "[ForgeInstaller] Manual extraction: cannot open JAR file:" << jarPath;
        return false;
    }

    qint64 fileSize = jarFile.size();
    if (fileSize < 22)
    {
        qWarning() << "[ForgeInstaller] Manual extraction: JAR file too small:" << fileSize;
        jarFile.close();
        return false;
    }

    jarFile.seek(fileSize - 22);
    QByteArray eocdData = jarFile.read(22);
    if (eocdData.size() < 22)
    {
        jarFile.close();
        return false;
    }

    quint32 eocdSignature = (quint8)eocdData[0] | ((quint8)eocdData[1] << 8)
                          | ((quint8)eocdData[2] << 16) | ((quint8)eocdData[3] << 24);
    if (eocdSignature != 0x06054b50)
    {
        eocdData.clear();
        qint64 searchStart = qMax((qint64)0, fileSize - 65557);
        jarFile.seek(searchStart);
        QByteArray tail = jarFile.read(fileSize - searchStart);
        int foundPos = -1;
        for (int i = tail.size() - 22; i >= 0; --i)
        {
            if ((quint8)tail[i] == 0x50 && (quint8)tail[i + 1] == 0x4b
                && (quint8)tail[i + 2] == 0x05 && (quint8)tail[i + 3] == 0x06)
            {
                foundPos = i;
                break;
            }
        }
        if (foundPos < 0)
        {
            qWarning() << "[ForgeInstaller] Manual extraction: ZIP EOCD not found";
            jarFile.close();
            return false;
        }
        eocdData = tail.mid(foundPos, 22);
    }

    quint16 totalEntries = (quint8)eocdData[10] | ((quint8)eocdData[11] << 8);
    quint32 centralDirOffset = (quint8)eocdData[16] | ((quint8)eocdData[17] << 8)
                             | ((quint8)eocdData[18] << 16) | ((quint8)eocdData[19] << 24);

    jarFile.seek(centralDirOffset);

    quint32 targetLocalHeaderOffset = 0;
    quint16 targetCompressionMethod = 0;
    quint32 targetCompressedSize = 0;
    quint32 targetUncompressedSize = 0;
    bool found = false;

    for (int i = 0; i < totalEntries; ++i)
    {
        QByteArray header = jarFile.read(46);
        if (header.size() < 46)
        {
            jarFile.close();
            return false;
        }

        quint32 signature = (quint8)header[0] | ((quint8)header[1] << 8)
                          | ((quint8)header[2] << 16) | ((quint8)header[3] << 24);
        if (signature != 0x02014b50)
        {
            break;
        }

        quint16 compressionMethod = (quint8)header[10] | ((quint8)header[11] << 8);
        quint32 compressedSize = (quint8)header[20] | ((quint8)header[21] << 8)
                               | ((quint8)header[22] << 16) | ((quint8)header[23] << 24);
        quint32 uncompressedSize = (quint8)header[24] | ((quint8)header[25] << 8)
                                 | ((quint8)header[26] << 16) | ((quint8)header[27] << 24);
        quint16 fileNameLength = (quint8)header[28] | ((quint8)header[29] << 8);
        quint16 extraFieldLength = (quint8)header[30] | ((quint8)header[31] << 8);
        quint16 commentLength = (quint8)header[32] | ((quint8)header[33] << 8);
        quint32 localHeaderOffset = (quint8)header[42] | ((quint8)header[43] << 8)
                                  | ((quint8)header[44] << 16) | ((quint8)header[45] << 24);

        QByteArray fileNameData = jarFile.read(fileNameLength);
        if (fileNameData.size() < fileNameLength)
        {
            jarFile.close();
            return false;
        }

        QString entryName = QString::fromUtf8(fileNameData);
        jarFile.read(extraFieldLength + commentLength);

        if (entryName == entryPath)
        {
            targetLocalHeaderOffset = localHeaderOffset;
            targetCompressionMethod = compressionMethod;
            targetCompressedSize = compressedSize;
            targetUncompressedSize = uncompressedSize;
            found = true;
            break;
        }
    }

    if (!found)
    {
        qWarning() << "[ForgeInstaller] Manual extraction: entry not found in JAR:" << entryPath;
        jarFile.close();
        return false;
    }

    jarFile.seek(targetLocalHeaderOffset);
    QByteArray localHeader = jarFile.read(30);
    if (localHeader.size() < 30)
    {
        jarFile.close();
        return false;
    }

    quint16 localFileNameLength = (quint8)localHeader[26] | ((quint8)localHeader[27] << 8);
    quint16 localExtraLength = (quint8)localHeader[28] | ((quint8)localHeader[29] << 8);

    jarFile.read(localFileNameLength + localExtraLength);

    QByteArray compressedData = jarFile.read(targetCompressedSize);
    if (compressedData.size() < (int)targetCompressedSize)
    {
        jarFile.close();
        return false;
    }

    jarFile.close();

    QByteArray outputData;

    if (targetCompressionMethod == 0)
    {
        outputData = compressedData;
    }
    else if (targetCompressionMethod == 8)
    {
        outputData = decompressRawDeflate(compressedData, targetUncompressedSize);
        if (outputData.isEmpty())
        {
            qWarning() << "[ForgeInstaller]" << "Deflate decompression failed for" << entryPath;
            return false;
        }
    }
    else
    {
        return false;
    }

    QDir destDir = QFileInfo(destPath).absoluteDir();
    if (!destDir.exists())
    {
        destDir.mkpath(".");
    }

    QFile outFile(destPath);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        return false;
    }

    outFile.write(outputData);
    outFile.close();

    return true;
}

int ForgeInstaller::detectInstallerType(const QString &jarPath)
{
    QString extractedPath = m_tempDir + "/install_profile.json";
    if (!extractEntryFromJar(jarPath, "install_profile.json", extractedPath))
    {
        qWarning() << "[ForgeInstaller]" << "Failed to extract install_profile.json from installer jar";
        return -1;
    }

    QFile file(extractedPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "[ForgeInstaller]" << "Failed to open install_profile.json";
        return -1;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        qWarning() << "[ForgeInstaller]" << "Failed to parse install_profile.json:" << parseError.errorString();
        return -1;
    }

    QJsonObject root = doc.object();

    if (root.contains("spec"))
    {
        qDebug() << "[ForgeInstaller]" << "Detected new version Forge installer (has 'spec' field)";
        return 0;
    }

    if (root.contains("install") && root.contains("versionInfo"))
    {
        qDebug() << "[ForgeInstaller]" << "Detected old version Forge installer (has 'install' and 'versionInfo' fields)";
        return 1;
    }

    qWarning() << "[ForgeInstaller]" << "Unknown installer type";
    return -1;
}

void ForgeInstaller::detectInstallerTypeAsync(const QString &jarPath)
{
    // Run extraction and type detection in a worker thread to avoid
    // blocking the UI thread with QProcess::waitForFinished() calls.
    auto future = QtConcurrent::run([this, jarPath]()
    {
        int result = detectInstallerType(jarPath);

        // Post result back to main thread
        QMetaObject::invokeMethod(this, [this, result]()
        {
            onInstallerTypeDetected(result);
        }, Qt::QueuedConnection);
    });
    Q_UNUSED(future);
}

void ForgeInstaller::cancelInstall()
{
    if (!m_isInstalling)
    {
        return;
    }

    m_downloader->cancelAll();

    if (m_installTask)
    {
        m_installTask->cancel();
        m_installTask->deleteLater();
        m_installTask = nullptr;
    }

    m_isInstalling = false;

    if (!m_tempDir.isEmpty())
    {
        QDir tempDir(m_tempDir);
        if (tempDir.exists())
        {
            tempDir.removeRecursively();
        }
    }

    emit installCancelled(m_taskId);
}

bool ForgeInstaller::isInstalling() const
{
    return m_isInstalling;
}