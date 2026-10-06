/**
 * @file   JarUtils.cpp
 * @brief  JAR文件提取工具函数实现
 * @author BlockBox Team
 * @date   2026-06-05
 */

#include "JarUtils.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <QMutex>
#include <QMutexLocker>
#ifdef Q_OS_WIN
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

// zlib 已通过 .pro 的 -lz 静态链接，直接调用即可，无需运行时动态加载

static QString getJarToolPath()
{
    QString jarPath = QStandardPaths::findExecutable("jar");
    if (!jarPath.isEmpty())
    {
        return jarPath;
    }

    QStringList searchPaths;
#ifdef Q_OS_WIN
    searchPaths << "C:/Program Files/Java"
                << "C:/Program Files (x86)/Java"
                << QDir::homePath() + "/.jdks"
                << QDir::homePath() + "/AppData/Local/Programs/Eclipse Adoptium";

    for (const QString &searchPath : searchPaths)
    {
        QDir dir(searchPath);
        if (!dir.exists())
        {
            continue;
        }

        QStringList jdkDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &jdkDir : jdkDirs)
        {
            QString candidate = searchPath + "/" + jdkDir + "/bin/jar.exe";
            if (QFile::exists(candidate))
            {
                return candidate;
            }
        }
    }
#elif defined(Q_OS_ANDROID)
    searchPaths << "/system/bin"
                << "/data/data/com.termux/files/usr/bin"
                << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/BlockBox/java"
                << QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/java";

    for (const QString &searchPath : searchPaths)
    {
        QDir dir(searchPath);
        if (!dir.exists())
        {
            continue;
        }

        // Check if jar exists directly
        QString candidate = searchPath + "/jar";
        if (QFile::exists(candidate))
        {
            return candidate;
        }

        // Check subdirectories
        QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &subDir : subDirs)
        {
            candidate = searchPath + "/" + subDir + "/bin/jar";
            if (QFile::exists(candidate))
            {
                return candidate;
            }
        }
    }
#else
    searchPaths << "/usr/lib/jvm"
                << "/usr/local/lib/jvm"
                << "/Library/Java/JavaVirtualMachines";

    for (const QString &searchPath : searchPaths)
    {
        QDir dir(searchPath);
        if (!dir.exists())
        {
            continue;
        }

        QStringList jdkDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &jdkDir : jdkDirs)
        {
            QString candidate = searchPath + "/" + jdkDir + "/bin/jar";
            if (QFile::exists(candidate))
            {
                return candidate;
            }
        }
    }
#endif

    return QString();
}

namespace JarUtils {

bool extractFromJar(const QString &jarPath, const QString &entryPath, const QString &destPath)
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

    qWarning() << "[JarUtils]" << "All extraction methods failed for"
               << "jar:" << jarPath << "entry:" << entryPath;
    return false;
}

bool extractFromJarToString(const QString &jarPath, const QString &entryPath, QString &content)
{
    // 优先使用内存直取（参考 PrismLauncher/MultiMC 做法）
    QByteArray data;
    if (extractFromJarToMemory(jarPath, entryPath, data))
    {
        content = QString::fromUtf8(data);
        return true;
    }

    // 回退到临时文件方式
    QString tempFile = QDir::tempPath() + "/blockbox_extract_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".tmp";
    if (!extractFromJar(jarPath, entryPath, tempFile))
    {
        return false;
    }

    QFile file(tempFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QFile::remove(tempFile);
        return false;
    }

    content = QString::fromUtf8(file.readAll());
    file.close();
    QFile::remove(tempFile);
    return true;
}

QStringList listEntriesInJar(const QString &jarPath, const QString &prefix)
{
    QStringList entries;
    QFile jarFile(jarPath);
    if (!jarFile.open(QIODevice::ReadOnly))
    {
        return entries;
    }

    qint64 fileSize = jarFile.size();
    if (fileSize < 22)
    {
        jarFile.close();
        return entries;
    }

    // 读取 End of Central Directory 记录
    jarFile.seek(fileSize - 22);
    QByteArray eocdData = jarFile.read(22);
    if (eocdData.size() < 22)
    {
        jarFile.close();
        return entries;
    }

    quint32 eocdSignature = (quint8)eocdData[0] | ((quint8)eocdData[1] << 8)
                          | ((quint8)eocdData[2] << 16) | ((quint8)eocdData[3] << 24);
    if (eocdSignature != 0x06054b50)
    {
        // 搜索 EOCD 签名（可能存在 ZIP 注释）
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
            jarFile.close();
            return entries;
        }
        eocdData = tail.mid(foundPos, 22);
    }

    quint16 totalEntries = (quint8)eocdData[10] | ((quint8)eocdData[11] << 8);
    quint32 centralDirOffset = (quint8)eocdData[16] | ((quint8)eocdData[17] << 8)
                             | ((quint8)eocdData[18] << 16) | ((quint8)eocdData[19] << 24);

    jarFile.seek(centralDirOffset);

    for (int i = 0; i < totalEntries; ++i)
    {
        QByteArray header = jarFile.read(46);
        if (header.size() < 46)
        {
            break;
        }

        quint32 signature = (quint8)header[0] | ((quint8)header[1] << 8)
                          | ((quint8)header[2] << 16) | ((quint8)header[3] << 24);
        if (signature != 0x02014b50)
        {
            break;
        }

        quint16 fileNameLength = (quint8)header[28] | ((quint8)header[29] << 8);
        quint16 extraFieldLength = (quint8)header[30] | ((quint8)header[31] << 8);
        quint16 commentLength = (quint8)header[32] | ((quint8)header[33] << 8);

        QByteArray fileNameData = jarFile.read(fileNameLength);
        if (fileNameData.size() < fileNameLength)
        {
            break;
        }

        // 跳过 extra field 和 comment
        jarFile.read(extraFieldLength + commentLength);

        QString entryName = QString::fromUtf8(fileNameData);

        // 跳过目录条目（以 / 或 \ 结尾）
        if (entryName.endsWith(QStringLiteral("/")) || entryName.endsWith(QStringLiteral("\\")))
        {
            continue;
        }

        // 归一化路径分隔符：Windows 打包工具可能用反斜杠
        entryName.replace('\\', '/');

        if (prefix.isEmpty() || entryName.startsWith(prefix))
        {
            entries.append(entryName);
        }
    }

    jarFile.close();
    return entries;
}

bool extractJarDirectory(const QString &jarPath, const QString &dirPath, const QString &destDir)
{
    QString jarToolPath = getJarToolPath();
    if (!jarToolPath.isEmpty())
    {
        QProcess process;
        QStringList args;
        args << "-xf" << jarPath << dirPath;
        process.setWorkingDirectory(destDir);
        process.start(jarToolPath, args);
        if (process.waitForFinished(120000) && process.exitCode() == 0)
        {
            return true;
        }
    }

#ifdef Q_OS_WIN
    if (tryExtractJarDirectoryWithPowerShell(jarPath, dirPath, destDir))
    {
        return true;
    }
#else
    if (tryExtractJarDirectoryWithUnzip(jarPath, dirPath, destDir))
    {
        return true;
    }
#endif

    if (tryExtractJarDirectoryWithPython(jarPath, dirPath, destDir))
    {
        return true;
    }

    if (tryExtractJarDirectoryManual(jarPath, dirPath, destDir))
    {
        return true;
    }

    qWarning() << "[JarUtils]" << "All directory extraction methods failed";
    return false;
}

bool tryExtractWithJarTool(const QString &jarPath, const QString &entryPath, const QString &destPath)
{
    QString jarToolPath = getJarToolPath();
    if (jarToolPath.isEmpty())
    {
        return false;
    }

    QProcess process;
    QStringList args;
    args << "-xf" << jarPath << entryPath;
    process.setWorkingDirectory(QFileInfo(destPath).absolutePath());
    process.start(jarToolPath, args);

    if (!process.waitForFinished(30000))
    {
        return false;
    }

    if (process.exitCode() != 0)
    {
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

bool tryExtractWithPowerShell(const QString &jarPath, const QString &entryPath, const QString &destPath)
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
        return false;
    }

    if (process.exitCode() != 0)
    {
        return false;
    }

    return QFile::exists(destPath);
}

bool tryExtractWithPython(const QString &jarPath, const QString &entryPath, const QString &destPath)
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

        script.replace("\\", "\\\\");

        QProcess process;
        process.start(pythonCmd, QStringList() << "-c" << script);

        if (!process.waitForFinished(15000))
        {
            continue;
        }

        if (process.exitCode() != 0)
        {
            continue;
        }

        if (QFile::exists(destPath) && QFileInfo(destPath).size() > 0)
        {
            return true;
        }
    }

    return false;
}

bool tryExtractWithUnzip(const QString &jarPath, const QString &entryPath, const QString &destPath)
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

bool tryExtractManual(const QString &jarPath, const QString &entryPath, const QString &destPath)
{
    QFile jarFile(jarPath);
    if (!jarFile.open(QIODevice::ReadOnly))
    {
        return false;
    }

    qint64 fileSize = jarFile.size();
    if (fileSize < 22)
    {
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
            qWarning() << "[JarUtils]" << "Deflate decompression failed for" << entryPath;
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

bool tryExtractJarDirectoryWithPowerShell(const QString &jarPath, const QString &dirPath, const QString &destDir)
{
    QString normalizedDirPath = dirPath;
    if (!normalizedDirPath.endsWith("/"))
    {
        normalizedDirPath += "/";
    }

    QString script = QString(
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zip = [System.IO.Compression.ZipFile]::OpenRead('%1');"
        "$prefix = '%2';"
        "$dest = '%3';"
        "foreach ($entry in $zip.Entries) {"
        "    if ($entry.FullName.StartsWith($prefix) -and $entry.Name.Length -gt 0) {"
        "        $relPath = $entry.FullName.Substring($prefix.Length);"
        "        $targetPath = Join-Path $dest $relPath;"
        "        $targetDir = Split-Path $targetPath -Parent;"
        "        if (-not (Test-Path $targetDir)) { New-Item -ItemType Directory -Path $targetDir -Force | Out-Null; }"
        "        $fs = [System.IO.File]::Create($targetPath);"
        "        $entry.Open().CopyTo($fs);"
        "        $fs.Close();"
        "    }"
        "}"
        "$zip.Dispose();"
    ).arg(jarPath, normalizedDirPath, destDir);

    QProcess process;
    process.start("powershell", QStringList() << "-NoProfile" << "-Command" << script);

    if (!process.waitForFinished(120000))
    {
        return false;
    }

    return process.exitCode() == 0;
}

bool tryExtractJarDirectoryWithPython(const QString &jarPath, const QString &dirPath, const QString &destDir)
{
    QStringList pythonCommands = {"python3", "python"};

    for (const QString &pythonCmd : pythonCommands)
    {
        QString script = QString(
            "import zipfile, os, sys\n"
            "os.makedirs(r'%3', exist_ok=True)\n"
            "with zipfile.ZipFile(r'%1') as z:\n"
            "    for info in z.infolist():\n"
            "        if info.filename.startswith('%2') and not info.is_dir():\n"
            "            rel = info.filename[len('%2'):]\n"
            "            if rel.startswith('/'): rel = rel[1:]\n"
            "            target = os.path.join(r'%3', rel)\n"
            "            os.makedirs(os.path.dirname(target), exist_ok=True)\n"
            "            with z.open(info) as src, open(target, 'wb') as dst:\n"
            "                dst.write(src.read())\n"
        ).arg(jarPath, dirPath, destDir);

        QProcess process;
        process.start(pythonCmd, QStringList() << "-c" << script);

        if (!process.waitForFinished(30000))
        {
            continue;
        }

        if (process.exitCode() == 0)
        {
            return true;
        }
    }

    return false;
}

bool tryExtractJarDirectoryWithUnzip(const QString &jarPath, const QString &dirPath, const QString &destDir)
{
    QString pattern = dirPath + "/*";
    QProcess process;
    QStringList args;
    args << "-o" << jarPath << pattern << "-d" << destDir;
    process.start("unzip", args);

    if (!process.waitForFinished(120000))
    {
        return false;
    }

    return process.exitCode() == 0;
}

bool tryExtractJarDirectoryManual(const QString &jarPath, const QString &dirPath, const QString &destDir)
{
    QFile jarFile(jarPath);
    if (!jarFile.open(QIODevice::ReadOnly))
    {
        return false;
    }

    qint64 fileSize = jarFile.size();
    if (fileSize < 22)
    {
        jarFile.close();
        return false;
    }

    jarFile.seek(fileSize - 22);
    QByteArray eocdData = jarFile.read(22);

    quint32 eocdSignature = (quint8)eocdData[0] | ((quint8)eocdData[1] << 8)
                          | ((quint8)eocdData[2] << 16) | ((quint8)eocdData[3] << 24);
    if (eocdSignature != 0x06054b50)
    {
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
            jarFile.close();
            return false;
        }
        eocdData = tail.mid(foundPos, 22);
    }

    quint16 totalEntries = (quint8)eocdData[10] | ((quint8)eocdData[11] << 8);
    quint32 centralDirOffset = (quint8)eocdData[16] | ((quint8)eocdData[17] << 8)
                             | ((quint8)eocdData[18] << 16) | ((quint8)eocdData[19] << 24);

    jarFile.seek(centralDirOffset);

    QString normalizedDirPath = dirPath;
    if (!normalizedDirPath.endsWith("/"))
    {
        normalizedDirPath += "/";
    }

    int extractedCount = 0;

    for (int i = 0; i < totalEntries; ++i)
    {
        QByteArray header = jarFile.read(46);
        if (header.size() < 46) break;

        quint32 signature = (quint8)header[0] | ((quint8)header[1] << 8)
                          | ((quint8)header[2] << 16) | ((quint8)header[3] << 24);
        if (signature != 0x02014b50) break;

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
        QString entryName = QString::fromUtf8(fileNameData);
        jarFile.read(extraFieldLength + commentLength);

        if (!entryName.startsWith(normalizedDirPath) || entryName == normalizedDirPath)
        {
            continue;
        }

        qint64 savedPos = jarFile.pos();

        jarFile.seek(localHeaderOffset);
        QByteArray localHeader = jarFile.read(30);
        quint16 localFileNameLength = (quint8)localHeader[26] | ((quint8)localHeader[27] << 8);
        quint16 localExtraLength = (quint8)localHeader[28] | ((quint8)localHeader[29] << 8);
        jarFile.read(localFileNameLength + localExtraLength);

        QByteArray compressedData = jarFile.read(compressedSize);

        QByteArray outputData;
        if (compressionMethod == 0)
        {
            outputData = compressedData;
        }
        else if (compressionMethod == 8)
        {
            outputData = decompressRawDeflate(compressedData, uncompressedSize);
        }
        else
        {
            jarFile.seek(savedPos);
            continue;
        }

        if (outputData.isEmpty() && compressionMethod == 8)
        {
            jarFile.seek(savedPos);
            continue;
        }

        QString relPath = entryName.mid(normalizedDirPath.length());
        QString targetPath = destDir + "/" + relPath;

        QDir targetParentDir = QFileInfo(targetPath).absoluteDir();
        if (!targetParentDir.exists())
        {
            targetParentDir.mkpath(".");
        }

        QFile outFile(targetPath);
        if (outFile.open(QIODevice::WriteOnly))
        {
            outFile.write(outputData);
            outFile.close();
            extractedCount++;
        }

        jarFile.seek(savedPos);
    }

    jarFile.close();

    return extractedCount > 0;
}

// ========== 内存直取实现（参考 PrismLauncher/MultiMC 的 ZIP 读取方式） ==========

/**
 * @brief 解析 ZIP 中央目录并定位指定条目的本地文件头
 */
static bool locateZipEntry(QFile &jarFile, const QString &entryPath,
                           quint32 &localHeaderOffset,
                           quint16 &compressionMethod,
                           quint32 &compressedSize,
                           quint32 &uncompressedSize)
{
    qint64 fileSize = jarFile.size();
    if (fileSize < 22)
    {
        return false;
    }

    // 读取 EOCD
    jarFile.seek(fileSize - 22);
    QByteArray eocdData = jarFile.read(22);
    if (eocdData.size() < 22)
    {
        return false;
    }

    quint32 eocdSignature = (quint8)eocdData[0] | ((quint8)eocdData[1] << 8)
                          | ((quint8)eocdData[2] << 16) | ((quint8)eocdData[3] << 24);
    if (eocdSignature != 0x06054b50)
    {
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
            return false;
        }
        eocdData = tail.mid(foundPos, 22);
    }

    quint16 totalEntries = (quint8)eocdData[10] | ((quint8)eocdData[11] << 8);
    quint32 centralDirOffset = (quint8)eocdData[16] | ((quint8)eocdData[17] << 8)
                             | ((quint8)eocdData[18] << 16) | ((quint8)eocdData[19] << 24);

    jarFile.seek(centralDirOffset);

    for (int i = 0; i < totalEntries; ++i)
    {
        QByteArray header = jarFile.read(46);
        if (header.size() < 46)
        {
            return false;
        }

        quint32 signature = (quint8)header[0] | ((quint8)header[1] << 8)
                          | ((quint8)header[2] << 16) | ((quint8)header[3] << 24);
        if (signature != 0x02014b50)
        {
            break;
        }

        quint16 cm = (quint8)header[10] | ((quint8)header[11] << 8);
        quint32 cs = (quint8)header[20] | ((quint8)header[21] << 8)
                   | ((quint8)header[22] << 16) | ((quint8)header[23] << 24);
        quint32 us = (quint8)header[24] | ((quint8)header[25] << 8)
                   | ((quint8)header[26] << 16) | ((quint8)header[27] << 24);
        quint16 fnLen = (quint8)header[28] | ((quint8)header[29] << 8);
        quint16 exLen = (quint8)header[30] | ((quint8)header[31] << 8);
        quint16 cmLen = (quint8)header[32] | ((quint8)header[33] << 8);
        quint32 lho = (quint8)header[42] | ((quint8)header[43] << 8)
                    | ((quint8)header[44] << 16) | ((quint8)header[45] << 24);

        QByteArray fnData = jarFile.read(fnLen);
        if (fnData.size() < fnLen)
        {
            return false;
        }

        QString entryName = QString::fromUtf8(fnData);
        jarFile.read(exLen + cmLen);

        // 归一化路径分隔符：Windows 打包工具可能用反斜杠
        entryName.replace('\\', '/');

        if (entryName == entryPath)
        {
            localHeaderOffset = lho;
            compressionMethod = cm;
            compressedSize = cs;
            uncompressedSize = us;
            return true;
        }

        // 记录大小写不敏感的匹配，留待严格匹配失败后回退
        if (localHeaderOffset == 0
            && entryName.compare(entryPath, Qt::CaseInsensitive) == 0)
        {
            localHeaderOffset = lho;
            compressionMethod = cm;
            compressedSize = cs;
            uncompressedSize = us;
        }
    }

    // 未找到严格匹配，但有大小写不敏感的候选（localHeaderOffset 已被赋值）
    return localHeaderOffset != 0;
}

/**
 * @brief 读取并解压指定 ZIP 条目到 QByteArray
 */
static bool readZipEntryToMemory(QFile &jarFile,
                                 quint32 localHeaderOffset,
                                 quint16 compressionMethod,
                                 quint32 compressedSize,
                                 quint32 uncompressedSize,
                                 QByteArray &data)
{
    jarFile.seek(localHeaderOffset);
    QByteArray localHeader = jarFile.read(30);
    if (localHeader.size() < 30)
    {
        return false;
    }

    quint16 localFNLen = (quint8)localHeader[26] | ((quint8)localHeader[27] << 8);
    quint16 localExLen = (quint8)localHeader[28] | ((quint8)localHeader[29] << 8);
    jarFile.read(localFNLen + localExLen);

    QByteArray compressedData = jarFile.read(compressedSize);
    if (compressedData.size() < (int)compressedSize)
    {
        return false;
    }

    if (compressionMethod == 0)
    {
        data = compressedData;
        return true;
    }
    else if (compressionMethod == 8)
    {
        data = decompressRawDeflate(compressedData, uncompressedSize);
        return !data.isEmpty();
    }

    return false;
}

bool extractFromJarToMemory(const QString &jarPath, const QString &entryPath, QByteArray &data)
{
    QFile jarFile(jarPath);
    if (!jarFile.open(QIODevice::ReadOnly))
    {
        return false;
    }

    quint32 localHeaderOffset = 0;
    quint16 compressionMethod = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;

    if (!locateZipEntry(jarFile, entryPath, localHeaderOffset, compressionMethod,
                        compressedSize, uncompressedSize))
    {
        jarFile.close();
        return false;
    }

    bool ok = readZipEntryToMemory(jarFile, localHeaderOffset, compressionMethod,
                                   compressedSize, uncompressedSize, data);
    jarFile.close();
    return ok;
}

int extractMultipleFromJarToMemory(const QString &jarPath,
                                   const QStringList &entryPaths,
                                   QMap<QString, QByteArray> &results)
{
    if (entryPaths.isEmpty())
    {
        return 0;
    }

    QFile jarFile(jarPath);
    if (!jarFile.open(QIODevice::ReadOnly))
    {
        return 0;
    }

    // 先用 QSet 加速查找
    QSet<QString> targetSet(entryPaths.begin(), entryPaths.end());

    qint64 fileSize = jarFile.size();
    if (fileSize < 22)
    {
        jarFile.close();
        return 0;
    }

    // 读取 EOCD
    jarFile.seek(fileSize - 22);
    QByteArray eocdData = jarFile.read(22);
    if (eocdData.size() < 22)
    {
        jarFile.close();
        return 0;
    }

    quint32 eocdSignature = (quint8)eocdData[0] | ((quint8)eocdData[1] << 8)
                          | ((quint8)eocdData[2] << 16) | ((quint8)eocdData[3] << 24);
    if (eocdSignature != 0x06054b50)
    {
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
            jarFile.close();
            return 0;
        }
        eocdData = tail.mid(foundPos, 22);
    }

    quint16 totalEntries = (quint8)eocdData[10] | ((quint8)eocdData[11] << 8);
    quint32 centralDirOffset = (quint8)eocdData[16] | ((quint8)eocdData[17] << 8)
                             | ((quint8)eocdData[18] << 16) | ((quint8)eocdData[19] << 24);

    jarFile.seek(centralDirOffset);

    int extractedCount = 0;

    for (int i = 0; i < totalEntries && !targetSet.isEmpty(); ++i)
    {
        QByteArray header = jarFile.read(46);
        if (header.size() < 46)
        {
            break;
        }

        quint32 signature = (quint8)header[0] | ((quint8)header[1] << 8)
                          | ((quint8)header[2] << 16) | ((quint8)header[3] << 24);
        if (signature != 0x02014b50)
        {
            break;
        }

        quint16 cm = (quint8)header[10] | ((quint8)header[11] << 8);
        quint32 cs = (quint8)header[20] | ((quint8)header[21] << 8)
                   | ((quint8)header[22] << 16) | ((quint8)header[23] << 24);
        quint32 us = (quint8)header[24] | ((quint8)header[25] << 8)
                   | ((quint8)header[26] << 16) | ((quint8)header[27] << 24);
        quint16 fnLen = (quint8)header[28] | ((quint8)header[29] << 8);
        quint16 exLen = (quint8)header[30] | ((quint8)header[31] << 8);
        quint16 cmLen = (quint8)header[32] | ((quint8)header[33] << 8);
        quint32 lho = (quint8)header[42] | ((quint8)header[43] << 8)
                    | ((quint8)header[44] << 16) | ((quint8)header[45] << 24);

        QByteArray fnData = jarFile.read(fnLen);
        if (fnData.size() < fnLen)
        {
            break;
        }

        QString entryName = QString::fromUtf8(fnData);
        jarFile.read(exLen + cmLen);

        // 记录下一个中央目录条目的偏移，readZipEntryToMemory 会 seek 到 local header，
        // 读完需回到这里继续遍历。
        qint64 nextHeaderPos = jarFile.pos();

        if (targetSet.contains(entryName))
        {
            QByteArray data;
            if (readZipEntryToMemory(jarFile, lho, cm, cs, us, data))
            {
                results[entryName] = data;
                targetSet.remove(entryName);
                extractedCount++;
            }
            jarFile.seek(nextHeaderPos);
        }
    }

    jarFile.close();
    return extractedCount;
}

void ensureZlibLoaded()
{
    // zlib 已静态链接，无需预加载
}

QByteArray decompressRawDeflate(const QByteArray &compressedData, quint32 uncompressedSize)
{
    QByteArray result;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.next_in = (Bytef*)compressedData.constData();
    strm.avail_in = compressedData.size();

    // 直接调用静态链接的 zlib 函数（-lz）
    int ret = inflateInit2(&strm, -15);
    if (ret != Z_OK)
    {
        qWarning() << "[JarUtils]" << "inflateInit2 failed:" << ret;
        return result;
    }

    // 预留空间，若 uncompressedSize 为 0 则动态增长
    if (uncompressedSize > 0)
    {
        result.resize(uncompressedSize);
    }
    else
    {
        result.resize(compressedData.size() * 4 + 1024);
    }
    strm.next_out = (Bytef*)result.data();
    strm.avail_out = result.size();

    ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END)
    {
        qWarning() << "[JarUtils]" << "inflate failed:" << ret;
        return QByteArray();
    }

    result.resize(strm.total_out);
    return result;
}

} // namespace JarUtils