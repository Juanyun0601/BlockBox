/**
 * @file   PluginZip.cpp
 * @brief  插件 ZIP 读写工具实现（基于 zlib）
 * @author BlockBox Team
 * @date   2026-08-05
 */
#include "PluginZip.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QtZlib/zlib.h>

namespace {

constexpr quint32 kLocalFileSig   = 0x04034b50;
constexpr quint32 kCentralDirSig  = 0x02014b50;
constexpr quint32 kEocdSig        = 0x06054b50;

// 读取小端无符号整数
quint16 readU16(const QByteArray &b, int off) {
    return static_cast<quint16>((quint8)b[off] | ((quint8)b[off + 1] << 8));
}
quint32 readU32(const QByteArray &b, int off) {
    return static_cast<quint32>((quint8)b[off] | ((quint8)b[off + 1] << 8)
            | ((quint8)b[off + 2] << 16) | ((quint8)b[off + 3] << 24));
}

struct CentralEntry {
    QString name;
    quint32 compSize = 0;
    quint32 uncompSize = 0;
    quint32 crc32 = 0;
    quint16 method = 0;
    quint16 flags = 0;
    quint32 localOffset = 0;
};

/**
 * @brief 解析 EOCD，返回中央目录偏移与条目数；失败返回 false
 */
bool parseEocd(const QByteArray &data, quint32 &cdOffset, quint16 &entryCount)
{
    // EOCD 位于文件末尾，从后往前搜索签名
    const int minSearch = qMax(0, data.size() - (65535 + 22));
    for (int i = data.size() - 22; i >= minSearch; --i) {
        if (readU32(data, i) == kEocdSig) {
            cdOffset = readU32(data, i + 16);
            entryCount = readU16(data, i + 10);
            return true;
        }
    }
    return false;
}

/**
 * @brief 读取文件全部内容到内存
 */
bool readFileAll(const QString &path, QByteArray &out, QString &error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = f.errorString();
        return false;
    }
    out = f.readAll();
    f.close();
    return true;
}

QByteArray inflateRaw(const QByteArray &compressed, quint32 expectedSize)
{
    if (compressed.isEmpty())
        return QByteArray();

    z_stream strm = {};
    int ret = inflateInit2(&strm, -MAX_WBITS);
    if (ret != Z_OK)
        return QByteArray();

    QByteArray result;
    result.reserve(expectedSize > 0 ? int(expectedSize) : compressed.size() * 4);

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    strm.avail_in = static_cast<uInt>(compressed.size());

    const int CHUNK = 16384;
    QByteArray buffer(CHUNK, '\0');
    do {
        strm.next_out = reinterpret_cast<Bytef *>(buffer.data());
        strm.avail_out = CHUNK;
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            return QByteArray();
        }
        result.append(buffer.data(), CHUNK - strm.avail_out);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return result;
}

QByteArray deflateRaw(const QByteArray &data)
{
    z_stream strm = {};
    int ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                           -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK)
        return QByteArray();

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    strm.avail_in = static_cast<uInt>(data.size());

    const int CHUNK = 16384;
    QByteArray result;
    QByteArray buffer(CHUNK, '\0');
    do {
        strm.next_out = reinterpret_cast<Bytef *>(buffer.data());
        strm.avail_out = CHUNK;
        ret = deflate(&strm, Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&strm);
            return QByteArray();
        }
        result.append(buffer.data(), CHUNK - strm.avail_out);
    } while (strm.avail_out == 0);

    deflateEnd(&strm);
    return result;
}

// CRC32（zlib 提供）
quint32 crc32Of(const QByteArray &data)
{
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef *>(data.constData()),
                static_cast<uInt>(data.size()));
    return static_cast<quint32>(crc);
}

} // namespace

namespace PluginZip {

QStringList listEntries(const QString &zipPath, bool *ok)
{
    QStringList result;
    if (ok) *ok = false;

    QByteArray data;
    QString err;
    if (!readFileAll(zipPath, data, err))
        return result;

    quint32 cdOffset = 0;
    quint16 entryCount = 0;
    if (!parseEocd(data, cdOffset, entryCount))
        return result;

    int pos = static_cast<int>(cdOffset);
    for (int i = 0; i < entryCount; ++i) {
        if (pos + 46 > data.size() || readU32(data, pos) != kCentralDirSig)
            break;
        quint16 nameLen = readU16(data, pos + 28);
        quint16 extraLen = readU16(data, pos + 30);
        quint16 commentLen = readU16(data, pos + 32);
        if (pos + 46 + nameLen > data.size())
            break;
        result.append(QString::fromUtf8(data.mid(pos + 46, nameLen)));
        pos += 46 + nameLen + extraLen + commentLen;
    }

    if (ok) *ok = true;
    return result;
}

bool extractEntryToMemory(const QString &zipPath, const QString &entryPath, QByteArray &data)
{
    data.clear();
    QByteArray fileData;
    QString err;
    if (!readFileAll(zipPath, fileData, err))
        return false;

    quint32 cdOffset = 0;
    quint16 entryCount = 0;
    if (!parseEocd(fileData, cdOffset, entryCount))
        return false;

    // 遍历中央目录找到目标条目
    int pos = static_cast<int>(cdOffset);
    for (int i = 0; i < entryCount; ++i) {
        if (pos + 46 > fileData.size() || readU32(fileData, pos) != kCentralDirSig)
            return false;
        quint16 nameLen = readU16(fileData, pos + 28);
        quint16 extraLen = readU16(fileData, pos + 30);
        quint16 commentLen = readU16(fileData, pos + 32);
        if (pos + 46 + nameLen > fileData.size())
            return false;
        const QByteArray nameBytes = fileData.mid(pos + 46, nameLen);
        const QString name = QString::fromUtf8(nameBytes);
        // 统一目录分隔符比较：兼容 Windows 下 Compress-Archive 生成的反斜杠条目路径
        const QString normName = QString(name).replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString normEntry = QString(entryPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (normName == normEntry) {
            quint32 localOffset = readU32(fileData, pos + 42);
            quint32 compSize = readU32(fileData, pos + 20);
            quint32 uncompSize = readU32(fileData, pos + 24);
            quint16 method = readU16(fileData, pos + 10);
            quint16 localExtraLen = 0;
            if (localOffset + 30 <= static_cast<quint32>(fileData.size())
                    && readU32(fileData, static_cast<int>(localOffset)) == kLocalFileSig) {
                localExtraLen = readU16(fileData, static_cast<int>(localOffset) + 28);
            }
            int dataStart = static_cast<int>(localOffset) + 30 + nameLen + localExtraLen;
            if (dataStart + static_cast<int>(compSize) > fileData.size())
                return false;
            const QByteArray raw = fileData.mid(dataStart, static_cast<int>(compSize));
            if (method == 0) { // STORE
                data = raw;
                return true;
            } else if (method == 8) { // DEFLATE
                data = inflateRaw(raw, uncompSize);
                return !data.isEmpty() || uncompSize == 0;
            }
            return false;
        }
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return false;
}

bool extractEntryToString(const QString &zipPath, const QString &entryPath, QString &content)
{
    QByteArray data;
    if (!extractEntryToMemory(zipPath, entryPath, data))
        return false;
    content = QString::fromUtf8(data);
    return true;
}

int extractAllToDir(const QString &zipPath, const QString &destDir, bool *ok)
{
    if (ok) *ok = false;

    QByteArray data;
    QString err;
    if (!readFileAll(zipPath, data, err))
        return -1;

    quint32 cdOffset = 0;
    quint16 entryCount = 0;
    if (!parseEocd(data, cdOffset, entryCount))
        return -1;

    if (!QDir().mkpath(destDir))
        return -1;

    int extracted = 0;
    int pos = static_cast<int>(cdOffset);
    for (int i = 0; i < entryCount; ++i) {
        if (pos + 46 > data.size() || readU32(data, pos) != kCentralDirSig)
            break;
        quint16 nameLen = readU16(data, pos + 28);
        quint16 extraLen = readU16(data, pos + 30);
        quint16 commentLen = readU16(data, pos + 32);
        if (pos + 46 + nameLen > data.size())
            break;

        const QByteArray nameBytes = data.mid(pos + 46, nameLen);
        const QString name = QString::fromUtf8(nameBytes);
        const quint32 compSize = readU32(data, pos + 20);
        const quint32 uncompSize = readU32(data, pos + 24);
        const quint16 method = readU16(data, pos + 10);
        const quint32 localOffset = readU32(data, pos + 42);

        // 跳过目录条目（以 / 结尾）
        if (!name.isEmpty() && !name.endsWith(QLatin1Char('/'))) {
            quint16 localExtraLen = 0;
            if (localOffset + 30 <= static_cast<quint32>(data.size())
                    && readU32(data, static_cast<int>(localOffset)) == kLocalFileSig) {
                localExtraLen = readU16(data, static_cast<int>(localOffset) + 28);
            }
            int dataStart = static_cast<int>(localOffset) + 30 + nameLen + localExtraLen;
            if (dataStart + static_cast<int>(compSize) <= data.size()) {
                const QByteArray raw = data.mid(dataStart, static_cast<int>(compSize));
                QByteArray content;
                if (method == 0) {
                    content = raw;
                } else if (method == 8) {
                    content = inflateRaw(raw, uncompSize);
                }
                if (!content.isEmpty() || uncompSize == 0) {
                    // 防路径穿越：仅保留合法相对路径
                    const QString rel = QDir::cleanPath(name).replace('\\', '/');
                    if (!rel.isEmpty() && !rel.startsWith(QLatin1Char('/'))
                            && !rel.contains(QStringLiteral(".."))) {
                        const QString destFile = QDir::cleanPath(destDir + QStringLiteral("/") + rel);
                        if (destFile.startsWith(QDir::cleanPath(destDir))) {
                            QDir().mkpath(QFileInfo(destFile).absolutePath());
                            QFile out(destFile);
                            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                                out.write(content);
                                out.close();
                                ++extracted;
                            }
                        }
                    }
                }
            }
        }
        pos += 46 + nameLen + extraLen + commentLen;
    }

    if (ok) *ok = (extracted > 0);
    return extracted;
}

int zipDirectory(const QString &srcDir, const QString &zipPath, bool *ok)
{
    if (ok) *ok = false;

    QDir rootDir(srcDir);
    if (!rootDir.exists())
        return -1;

    // 收集相对路径 -> 绝对路径
    QList<QPair<QString, QString> > files; // (相对路径, 绝对路径)
    QDirIterator it(srcDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString absPath = it.filePath();
        const QString relPath = QDir::cleanPath(
            QDir(srcDir).relativeFilePath(absPath)).replace('\\', '/');
        if (!relPath.isEmpty())
            files.append(qMakePair(relPath, absPath));
    }
    if (files.isEmpty())
        return -1;

    QFile out(zipPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return -1;

    // 条目元数据（用于中央目录）
    struct EntryMeta {
        QString name;
        quint32 crc;
        quint32 compSize;
        quint32 uncompSize;
        quint16 method;
        quint32 localOffset;
    };
    QList<EntryMeta> metas;

    for (int i = 0; i < files.size(); ++i) {
        const QString &rel = files[i].first;
        const QString &abs = files[i].second;

        QFile src(abs);
        if (!src.open(QIODevice::ReadOnly))
            continue;
        const QByteArray content = src.readAll();
        src.close();

        const quint32 crc = crc32Of(content);
        const quint32 uncompSize = static_cast<quint32>(content.size());
        const QByteArray nameBytes = rel.toUtf8();
        quint16 method = 8;
        QByteArray stored = deflateRaw(content);
        if (stored.isEmpty() || static_cast<quint32>(stored.size()) >= uncompSize) {
            // 压缩失败或压缩后更大，使用 STORE
            method = 0;
            stored = content;
        }
        const quint32 compSize = static_cast<quint32>(stored.size());

        const quint32 localOffset = static_cast<quint32>(out.pos());

        // 本地文件头
        QByteArray header;
        QDataStream ds(&header, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds << kLocalFileSig;
        ds << static_cast<quint16>(20);            // version needed
        ds << static_cast<quint16>(0);             // flags
        ds << method;
        ds << static_cast<quint16>(0);             // mod time
        ds << static_cast<quint16>(0x21);          // mod date (1980-01-01)
        ds << crc;
        ds << compSize;
        ds << uncompSize;
        ds << static_cast<quint16>(nameBytes.size());
        ds << static_cast<quint16>(0);             // extra length
        out.write(header);
        out.write(nameBytes);
        out.write(stored);

        EntryMeta meta;
        meta.name = rel;
        meta.crc = crc;
        meta.compSize = compSize;
        meta.uncompSize = uncompSize;
        meta.method = method;
        meta.localOffset = localOffset;
        metas.append(meta);
    }

    if (metas.isEmpty()) {
        out.close();
        return -1;
    }

    const quint32 cdStart = static_cast<quint32>(out.pos());

    // 中央目录
    for (int i = 0; i < metas.size(); ++i) {
        const EntryMeta &m = metas[i];
        const QByteArray nameBytes = m.name.toUtf8();
        QByteArray cd;
        QDataStream ds(&cd, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds << kCentralDirSig;
        ds << static_cast<quint16>(20);            // version made by
        ds << static_cast<quint16>(20);            // version needed
        ds << static_cast<quint16>(0);             // flags
        ds << m.method;
        ds << static_cast<quint16>(0);             // mod time
        ds << static_cast<quint16>(0x21);          // mod date
        ds << m.crc;
        ds << m.compSize;
        ds << m.uncompSize;
        ds << static_cast<quint16>(nameBytes.size());
        ds << static_cast<quint16>(0);             // extra length
        ds << static_cast<quint16>(0);             // comment length
        ds << static_cast<quint16>(0);             // disk number start
        ds << static_cast<quint16>(0);             // internal attrs
        ds << static_cast<quint32>(0);             // external attrs
        ds << m.localOffset;
        out.write(cd);
        out.write(nameBytes);
    }

    const quint32 cdSize = static_cast<quint32>(out.pos()) - cdStart;

    // EOCD
    QByteArray eocd;
    QDataStream ds(&eocd, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << kEocdSig;
    ds << static_cast<quint16>(0);
    ds << static_cast<quint16>(0);
    ds << static_cast<quint16>(metas.size());
    ds << static_cast<quint16>(metas.size());
    ds << cdSize;
    ds << cdStart;
    ds << static_cast<quint16>(0);
    out.write(eocd);

    out.close();

    if (ok) *ok = true;
    return metas.size();
}

} // namespace PluginZip
