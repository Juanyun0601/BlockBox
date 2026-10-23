/**
 * @file   RuntimeArchive.cpp
 * @brief  运行依赖包解压实现（zip / tar.gz，纯 zlib，不调用外部命令）
 * @author BlockBox Team
 * @date   2026-10-23
 */

#include "RuntimeArchive.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstring>

#include <zlib.h>

namespace {

constexpr int kBufferSize = 256 * 1024;
constexpr quint32 kZipLocalHeaderSig = 0x04034b50;
constexpr quint32 kZipCentralHeaderSig = 0x02014b50;
constexpr quint32 kZipEndOfCentralDirSig = 0x06054b50;

quint16 readLe16(const char *p)
{
    return static_cast<quint16>(static_cast<quint8>(p[0]))
        | static_cast<quint16>(static_cast<quint16>(static_cast<quint8>(p[1])) << 8);
}

quint32 readLe32(const char *p)
{
    return static_cast<quint32>(static_cast<quint8>(p[0]))
        | (static_cast<quint32>(static_cast<quint8>(p[1])) << 8)
        | (static_cast<quint32>(static_cast<quint8>(p[2])) << 16)
        | (static_cast<quint32>(static_cast<quint8>(p[3])) << 24);
}

/**
 * @brief 规范化压缩包内的条目路径，拒绝越权路径
 * @return 安全的相对路径；非法时返回空字符串
 */
QString sanitizeEntryPath(const QString &rawName)
{
    QString name = rawName;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QLatin1Char('/')))
        name.remove(0, 1);

    // Windows 盘符 / UNC 形式的绝对路径一律拒绝
    if (name.size() >= 2 && name.at(1) == QLatin1Char(':'))
        return QString();

    QStringList parts;
    const QStringList rawParts = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : rawParts) {
        if (part == QLatin1String("."))
            continue;
        if (part == QLatin1String(".."))
            return QString(); // 越权路径
        parts.append(part);
    }
    if (parts.isEmpty())
        return QString();
    return parts.join(QLatin1Char('/'));
}

QFileDevice::Permissions permissionsFromMode(quint32 mode)
{
    QFileDevice::Permissions perms;
    if (mode & 0400) perms |= QFileDevice::ReadOwner;
    if (mode & 0200) perms |= QFileDevice::WriteOwner;
    if (mode & 0100) perms |= QFileDevice::ExeOwner;
    if (mode & 0040) perms |= QFileDevice::ReadGroup;
    if (mode & 0020) perms |= QFileDevice::WriteGroup;
    if (mode & 0010) perms |= QFileDevice::ExeGroup;
    if (mode & 0004) perms |= QFileDevice::ReadOther;
    if (mode & 0002) perms |= QFileDevice::WriteOther;
    if (mode & 0001) perms |= QFileDevice::ExeOther;
    return perms;
}

void setErrorMessage(QString *errorMessage, const QString &text)
{
    if (errorMessage)
        *errorMessage = text;
}

void reportProgress(const RuntimeArchive::ProgressCallback &onProgress, int percent, const QString &stage)
{
    if (!onProgress)
        return;
    onProgress(qBound(0, percent, 100), stage);
}

/** 把压缩数据流式解压到输出文件 */
bool inflateToFile(QFile &input,
                   qint64 dataOffset,
                   qint64 compressedSize,
                   QFile &output,
                   int windowBits,
                   QString *errorMessage)
{
    if (!input.seek(dataOffset)) {
        setErrorMessage(errorMessage, QStringLiteral("无法读取压缩包数据"));
        return false;
    }

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, windowBits) != Z_OK) {
        setErrorMessage(errorMessage, QStringLiteral("解压组件初始化失败"));
        return false;
    }

    QByteArray inBuffer(kBufferSize, '\0');
    QByteArray outBuffer(kBufferSize, '\0');
    qint64 remaining = compressedSize;
    bool streamEnded = false;
    bool ok = true;

    while (!streamEnded) {
        const qint64 toRead = remaining > 0 ? qMin<qint64>(remaining, inBuffer.size()) : 0;
        if (toRead <= 0 && remaining <= 0) {
            // 没有更多输入：给解压器一次冲刷机会后结束
            stream.next_in = nullptr;
            stream.avail_in = 0;
        } else {
            const qint64 got = input.read(inBuffer.data(), toRead);
            if (got <= 0) {
                setErrorMessage(errorMessage, QStringLiteral("压缩包数据不完整"));
                ok = false;
                break;
            }
            remaining -= got;
            stream.next_in = reinterpret_cast<Bytef *>(inBuffer.data());
            stream.avail_in = static_cast<uInt>(got);
        }

        do {
            stream.next_out = reinterpret_cast<Bytef *>(outBuffer.data());
            stream.avail_out = static_cast<uInt>(outBuffer.size());
            const int ret = inflate(&stream, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
                setErrorMessage(errorMessage, QStringLiteral("压缩包内容已损坏"));
                ok = false;
                break;
            }
            const qint64 produced = outBuffer.size() - stream.avail_out;
            if (produced > 0 && output.write(outBuffer.constData(), produced) != produced) {
                setErrorMessage(errorMessage, QStringLiteral("写入文件失败：%1").arg(output.fileName()));
                ok = false;
                break;
            }
            if (ret == Z_STREAM_END) {
                streamEnded = true;
                break;
            }
            if (ret == Z_BUF_ERROR && stream.avail_in == 0 && remaining <= 0) {
                // 输入耗尽但流未结束：数据不完整
                setErrorMessage(errorMessage, QStringLiteral("压缩包数据不完整"));
                ok = false;
                streamEnded = true;
                break;
            }
        } while (stream.avail_in > 0);
        if (!ok)
            break;
    }

    inflateEnd(&stream);
    return ok;
}

bool extractZipEntry(QFile &archive,
                     qint64 localHeaderOffset,
                     quint16 method,
                     qint64 compressedSize,
                     const QString &destPath,
                     quint32 unixMode,
                     QString *errorMessage)
{
    char localHeader[30];
    if (!archive.seek(localHeaderOffset)
        || archive.read(localHeader, sizeof(localHeader)) != qint64(sizeof(localHeader))
        || readLe32(localHeader) != kZipLocalHeaderSig) {
        setErrorMessage(errorMessage, QStringLiteral("压缩包目录信息异常"));
        return false;
    }
    const quint16 nameLength = readLe16(localHeader + 26);
    const quint16 extraLength = readLe16(localHeader + 28);
    const qint64 dataOffset = localHeaderOffset + 30 + nameLength + extraLength;

    QDir().mkpath(QFileInfo(destPath).absolutePath());
    QFile output(destPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setErrorMessage(errorMessage, QStringLiteral("无法写入文件：%1").arg(destPath));
        return false;
    }

    bool ok = true;
    if (method == 0) {
        // Stored，无压缩
        if (!archive.seek(dataOffset)) {
            setErrorMessage(errorMessage, QStringLiteral("无法读取压缩包数据"));
            ok = false;
        } else {
            qint64 remaining = compressedSize;
            QByteArray buffer(kBufferSize, '\0');
            while (ok && remaining > 0) {
                const qint64 got = archive.read(buffer.data(), qMin<qint64>(remaining, buffer.size()));
                if (got <= 0) {
                    setErrorMessage(errorMessage, QStringLiteral("压缩包数据不完整"));
                    ok = false;
                    break;
                }
                remaining -= got;
                if (output.write(buffer.constData(), got) != got) {
                    setErrorMessage(errorMessage, QStringLiteral("写入文件失败：%1").arg(destPath));
                    ok = false;
                }
            }
        }
    } else if (method == 8) {
        ok = inflateToFile(archive, dataOffset, compressedSize, output, -MAX_WBITS, errorMessage);
    } else {
        setErrorMessage(errorMessage, QStringLiteral("不支持的压缩算法（method=%1）").arg(method));
        ok = false;
    }

    output.close();
    if (!ok) {
        output.remove();
        return false;
    }

    // 尽量还原可执行权限：bin/ 目录下的文件按可执行处理，方便解压后直接运行
    QFileDevice::Permissions perms = permissionsFromMode(unixMode);
    if (perms == QFileDevice::Permissions()) {
        const bool executable = destPath.contains(QLatin1String("/bin/"))
            || destPath.endsWith(QLatin1String("bin/java"))
            || destPath.contains(QLatin1String("/jre/bin/"));
        perms = QFileDevice::ReadOwner | QFileDevice::WriteOwner
            | (executable ? QFileDevice::ExeOwner : QFileDevice::Permissions());
    }
    output.setPermissions(perms);
    return true;
}

bool extractZip(QFile &archive,
                const QString &destDir,
                QString *errorMessage,
                const RuntimeArchive::ProgressCallback &onProgress)
{
    const qint64 fileSize = archive.size();
    if (fileSize < 22) {
        setErrorMessage(errorMessage, QStringLiteral("压缩包内容为空或已损坏"));
        return false;
    }

    // 从文件尾向前搜索中央目录结束记录（最多 64KB + 22 字节注释）
    const qint64 scanSize = qMin<qint64>(fileSize, 64 * 1024 + 22);
    QByteArray tail(static_cast<int>(scanSize), '\0');
    if (!archive.seek(fileSize - scanSize)
        || archive.read(tail.data(), scanSize) != scanSize) {
        setErrorMessage(errorMessage, QStringLiteral("无法读取压缩包"));
        return false;
    }

    int eocdIndex = -1;
    for (int i = tail.size() - 22; i >= 0; --i) {
        if (readLe32(tail.constData() + i) == kZipEndOfCentralDirSig) {
            eocdIndex = i;
            break;
        }
    }
    if (eocdIndex < 0) {
        setErrorMessage(errorMessage, QStringLiteral("不是有效的 ZIP 压缩包"));
        return false;
    }

    const quint16 totalEntries = readLe16(tail.constData() + eocdIndex + 10);
    const quint32 centralDirSize = readLe32(tail.constData() + eocdIndex + 12);
    const quint32 centralDirOffset = readLe32(tail.constData() + eocdIndex + 16);
    if (totalEntries == 0) {
        setErrorMessage(errorMessage, QStringLiteral("压缩包内没有文件"));
        return false;
    }
    if (centralDirOffset == 0
        || qint64(centralDirOffset) + qint64(centralDirSize) > fileSize) {
        setErrorMessage(errorMessage, QStringLiteral("压缩包目录信息异常"));
        return false;
    }

    if (!archive.seek(centralDirOffset)) {
        setErrorMessage(errorMessage, QStringLiteral("无法读取压缩包目录"));
        return false;
    }
    QByteArray centralDir = archive.read(centralDirSize);
    if (centralDir.size() != int(centralDirSize)) {
        setErrorMessage(errorMessage, QStringLiteral("无法读取压缩包目录"));
        return false;
    }

    int processed = 0;
    int cursor = 0;
    while (cursor + 46 <= centralDir.size() && processed < totalEntries) {
        if (readLe32(centralDir.constData() + cursor) != kZipCentralHeaderSig)
            break;

        const quint16 method = readLe16(centralDir.constData() + cursor + 10);
        const quint32 compressedSize = readLe32(centralDir.constData() + cursor + 20);
        const quint16 nameLength = readLe16(centralDir.constData() + cursor + 28);
        const quint16 extraLength = readLe16(centralDir.constData() + cursor + 30);
        const quint16 commentLength = readLe16(centralDir.constData() + cursor + 32);
        const quint32 externalAttr = readLe32(centralDir.constData() + cursor + 38);
        const quint32 localHeaderOffset = readLe32(centralDir.constData() + cursor + 42);
        if (cursor + 46 + nameLength > centralDir.size())
            break;

        const QString rawName = QString::fromUtf8(centralDir.constData() + cursor + 46, nameLength);
        const quint32 unixMode = (externalAttr >> 16) & 0xffff;
        cursor += 46 + nameLength + extraLength + commentLength;
        ++processed;

        if (rawName.endsWith(QLatin1Char('/'))) {
            const QString dirPath = sanitizeEntryPath(rawName);
            if (!dirPath.isEmpty())
                QDir().mkpath(QDir(destDir).absoluteFilePath(dirPath));
            continue;
        }

        const QString relativePath = sanitizeEntryPath(rawName);
        if (relativePath.isEmpty())
            continue; // 跳过越权/非法路径条目
        const QString destPath = QDir(destDir).absoluteFilePath(relativePath);
        if (!extractZipEntry(archive, localHeaderOffset, method, compressedSize,
                             destPath, unixMode, errorMessage)) {
            return false;
        }
        if (onProgress && (processed % 8 == 0 || processed == totalEntries))
            reportProgress(onProgress, processed * 100 / qMax<int>(1, totalEntries),
                           QStringLiteral("正在解压（%1/%2）").arg(processed).arg(totalEntries));
    }

    reportProgress(onProgress, 100, QStringLiteral("解压完成"));
    return true;
}

/** 解 gzip 数据到临时文件，返回临时文件路径 */
bool gunzipToFile(QFile &input, const QString &targetPath, QString *errorMessage,
                  const RuntimeArchive::ProgressCallback &onProgress)
{
    input.seek(0);
    QFile output(targetPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setErrorMessage(errorMessage, QStringLiteral("无法创建临时文件"));
        return false;
    }

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        setErrorMessage(errorMessage, QStringLiteral("解压组件初始化失败"));
        return false;
    }

    QByteArray inBuffer(kBufferSize, '\0');
    QByteArray outBuffer(kBufferSize, '\0');
    const qint64 total = qMax<qint64>(1, input.size());
    qint64 consumed = 0;
    bool ok = true;
    bool streamEnded = false;

    while (!streamEnded) {
        const qint64 got = input.read(inBuffer.data(), inBuffer.size());
        if (got <= 0)
            break;
        consumed += got;
        stream.next_in = reinterpret_cast<Bytef *>(inBuffer.data());
        stream.avail_in = static_cast<uInt>(got);
        do {
            stream.next_out = reinterpret_cast<Bytef *>(outBuffer.data());
            stream.avail_out = static_cast<uInt>(outBuffer.size());
            const int ret = inflate(&stream, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                setErrorMessage(errorMessage, QStringLiteral("gzip 数据已损坏"));
                ok = false;
                break;
            }
            const qint64 produced = outBuffer.size() - stream.avail_out;
            if (produced > 0 && output.write(outBuffer.constData(), produced) != produced) {
                setErrorMessage(errorMessage, QStringLiteral("写入临时文件失败"));
                ok = false;
                break;
            }
            if (ret == Z_STREAM_END) {
                streamEnded = true;
                break;
            }
        } while (stream.avail_in > 0);
        if (!ok)
            break;
        if (onProgress)
            reportProgress(onProgress, int(consumed * 40 / total), QStringLiteral("正在解压 gzip"));
    }

    inflateEnd(&stream);
    output.close();
    if (!ok)
        output.remove();
    return ok;
}

quint64 tarNumber(const char *field, int length)
{
    // GNU 基础 256 进制（最高位为 1 表示二进制编码）
    if (static_cast<quint8>(field[0]) & 0x80) {
        quint64 value = static_cast<quint8>(field[0]) & 0x7f;
        for (int i = 1; i < length; ++i)
            value = (value << 8) | static_cast<quint8>(field[i]);
        return value;
    }
    QString text = QString::fromLatin1(field, length).trimmed();
    text.remove(QLatin1Char('\0'));
    bool ok = false;
    const quint64 value = text.toULongLong(&ok, 8);
    return ok ? value : 0;
}

QString tarString(const char *field, int length)
{
    int end = 0;
    while (end < length && field[end] != '\0')
        ++end;
    while (end > 0 && (field[end - 1] == ' '))
        --end;
    return QString::fromUtf8(field, end);
}

bool extractTar(QFile &tarFile,
                const QString &destDir,
                QString *errorMessage,
                const RuntimeArchive::ProgressCallback &onProgress)
{
    const qint64 totalSize = qMax<qint64>(1, tarFile.size());
    QString pendingLongName;
    qint64 offset = 0;
    char header[512];
    bool sawEntry = false;

    while (tarFile.seek(offset) && tarFile.read(header, sizeof(header)) == qint64(sizeof(header))) {
        bool allZero = true;
        for (int i = 0; i < 512; ++i) {
            if (header[i] != '\0') {
                allZero = false;
                break;
            }
        }
        if (allZero)
            break; // tar 结束块

        QString name = tarString(header, 100);
        const quint64 size = tarNumber(header + 124, 12);
        const char typeFlag = header[156];
        const QString prefix = tarString(header + 345, 155);
        if (!prefix.isEmpty() && tarString(header + 257, 6).startsWith(QLatin1String("ustar")))
            name = prefix + QLatin1Char('/') + name;

        const qint64 dataOffset = offset + 512;
        const qint64 paddedSize = qint64((size + 511) / 512) * 512;

        if (typeFlag == 'L') {
            // GNU 长文件名：内容就是下一个条目的完整名字
            QByteArray data(static_cast<int>(size), '\0');
            if (tarFile.seek(dataOffset) && tarFile.read(data.data(), size) == qint64(size))
                pendingLongName = tarString(data.constData(), data.size());
            offset = dataOffset + paddedSize;
            continue;
        }
        if (typeFlag == 'x' || typeFlag == 'g') {
            // pax 扩展头：仅取 path 字段
            QByteArray data(static_cast<int>(size), '\0');
            if (tarFile.seek(dataOffset) && tarFile.read(data.data(), size) == qint64(size)) {
                const QList<QByteArray> lines = data.split('\n');
                for (const QByteArray &line : lines) {
                    if (line.startsWith("path="))
                        pendingLongName = QString::fromUtf8(line.mid(5));
                }
            }
            offset = dataOffset + paddedSize;
            continue;
        }

        if (!pendingLongName.isEmpty()) {
            name = pendingLongName;
            pendingLongName.clear();
        }

        const QString relativePath = sanitizeEntryPath(name);
        const bool isDir = (typeFlag == '5') || name.endsWith(QLatin1Char('/'));
        if (!relativePath.isEmpty()) {
            const QString destPath = QDir(destDir).absoluteFilePath(relativePath);
            if (isDir) {
                QDir().mkpath(destPath);
            } else if (typeFlag == '0' || typeFlag == '\0' || typeFlag == '7') {
                QDir().mkpath(QFileInfo(destPath).absolutePath());
                QFile output(destPath);
                if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    setErrorMessage(errorMessage, QStringLiteral("无法写入文件：%1").arg(destPath));
                    return false;
                }
                qint64 remaining = qint64(size);
                QByteArray buffer(kBufferSize, '\0');
                if (tarFile.seek(dataOffset)) {
                    while (remaining > 0) {
                        const qint64 got = tarFile.read(buffer.data(), qMin<qint64>(remaining, buffer.size()));
                        if (got <= 0)
                            break;
                        remaining -= got;
                        if (output.write(buffer.constData(), got) != got) {
                            setErrorMessage(errorMessage, QStringLiteral("写入文件失败：%1").arg(destPath));
                            return false;
                        }
                    }
                }
                output.close();
                quint32 mode = quint32(tarNumber(header + 100, 8)) & 0xffff;
                QFileDevice::Permissions perms = permissionsFromMode(mode);
                if (perms == QFileDevice::Permissions())
                    perms = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
                output.setPermissions(perms);
            }
            sawEntry = true;
        }

        offset = dataOffset + paddedSize;
        if (onProgress)
            reportProgress(onProgress, 40 + int(offset * 60 / totalSize), QStringLiteral("正在解包"));
    }

    if (!sawEntry) {
        setErrorMessage(errorMessage, QStringLiteral("压缩包内没有可用文件"));
        return false;
    }
    reportProgress(onProgress, 100, QStringLiteral("解压完成"));
    return true;
}

bool extractTarGz(QFile &archive,
                  const QString &destDir,
                  QString *errorMessage,
                  const RuntimeArchive::ProgressCallback &onProgress)
{
    QDir().mkpath(destDir);
    QTemporaryDir tempDir(QDir(destDir).absoluteFilePath(QStringLiteral(".blockbox_unpack_XXXXXX")));
    if (!tempDir.isValid()) {
        setErrorMessage(errorMessage, QStringLiteral("无法创建临时目录"));
        return false;
    }
    const QString tarPath = tempDir.filePath(QStringLiteral("payload.tar"));
    if (!gunzipToFile(archive, tarPath, errorMessage, onProgress))
        return false;

    QFile tarFile(tarPath);
    if (!tarFile.open(QIODevice::ReadOnly)) {
        setErrorMessage(errorMessage, QStringLiteral("无法读取解压后的临时文件"));
        return false;
    }
    return extractTar(tarFile, destDir, errorMessage, onProgress);
}

} // namespace

namespace RuntimeArchive {

bool isSupportedArchive(const QString &archivePath)
{
    QFile file(archivePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray magic = file.read(6);
    if (magic.size() >= 4 && static_cast<quint8>(magic.at(0)) == 0x50
        && static_cast<quint8>(magic.at(1)) == 0x4b)
        return true;
    if (magic.size() >= 2 && static_cast<quint8>(magic.at(0)) == 0x1f
        && static_cast<quint8>(magic.at(1)) == 0x8b)
        return true;
    return false;
}

bool extract(const QString &archivePath,
             const QString &destDir,
             QString *errorMessage,
             const ProgressCallback &onProgress)
{
    QFile archive(archivePath);
    if (!archive.open(QIODevice::ReadOnly)) {
        setErrorMessage(errorMessage, QStringLiteral("无法打开依赖包：%1").arg(archivePath));
        return false;
    }
    if (!QDir().mkpath(destDir)) {
        setErrorMessage(errorMessage, QStringLiteral("无法创建安装目录：%1").arg(destDir));
        return false;
    }

    const QByteArray magic = archive.peek(4);
    if (magic.size() >= 2 && static_cast<quint8>(magic.at(0)) == 0x50
        && static_cast<quint8>(magic.at(1)) == 0x4b) {
        reportProgress(onProgress, 0, QStringLiteral("正在解压"));
        return extractZip(archive, destDir, errorMessage, onProgress);
    }
    if (magic.size() >= 2 && static_cast<quint8>(magic.at(0)) == 0x1f
        && static_cast<quint8>(magic.at(1)) == 0x8b) {
        reportProgress(onProgress, 0, QStringLiteral("正在解压"));
        return extractTarGz(archive, destDir, errorMessage, onProgress);
    }

    setErrorMessage(errorMessage,
                    QStringLiteral("不支持的依赖包格式（仅支持 zip 或 tar.gz）"));
    return false;
}

} // namespace RuntimeArchive
