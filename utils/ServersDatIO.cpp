/**
 * @file   ServersDatIO.cpp
 * @brief  Minecraft servers.dat 读写实现（gzip + NBT）
 * @author BlockBox Team
 * @date  2026-10-18
 */

#include "ServersDatIO.h"

#include <QDebug>
#include <QFile>

#ifdef Q_OS_WIN
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

namespace {

enum class NbtType : quint8
{
    End = 0,
    Byte = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Float = 5,
    Double = 6,
    ByteArray = 7,
    String = 8,
    List = 9,
    Compound = 10,
    IntArray = 11,
    LongArray = 12
};

struct NbtTag
{
    NbtType type = NbtType::End;
    QString name;
    qint64 intValue = 0;
    float floatValue = 0.0f;
    double doubleValue = 0.0;
    QString stringValue;
    QByteArray byteArray;
    QList<qint32> intArray;
    QList<qint64> longArray;
    NbtType listElementType = NbtType::End;
    QList<NbtTag> children;
};

class NbtReader
{
public:
    explicit NbtReader(const QByteArray &data)
        : data_(data)
        , pos_(0)
    {
    }

    bool readByte(quint8 &value)
    {
        if (pos_ + 1 > data_.size())
            return false;
        value = static_cast<quint8>(data_.at(pos_));
        pos_ += 1;
        return true;
    }

    bool readUShort(quint16 &value)
    {
        if (pos_ + 2 > data_.size())
            return false;
        value = static_cast<quint16>(
            (static_cast<quint8>(data_.at(pos_)) << 8) |
            static_cast<quint8>(data_.at(pos_ + 1)));
        pos_ += 2;
        return true;
    }

    bool readInt(qint32 &value)
    {
        if (pos_ + 4 > data_.size())
            return false;
        value = (static_cast<quint8>(data_.at(pos_)) << 24) |
                (static_cast<quint8>(data_.at(pos_ + 1)) << 16) |
                (static_cast<quint8>(data_.at(pos_ + 2)) << 8) |
                static_cast<quint8>(data_.at(pos_ + 3));
        pos_ += 4;
        return true;
    }

    bool readLong(qint64 &value)
    {
        if (pos_ + 8 > data_.size())
            return false;
        quint64 v = 0;
        for (int i = 0; i < 8; ++i)
            v = (v << 8) | static_cast<quint8>(data_.at(pos_ + i));
        value = static_cast<qint64>(v);
        pos_ += 8;
        return true;
    }

    bool readFloat(float &value)
    {
        qint32 raw = 0;
        if (!readInt(raw))
            return false;
        union { quint32 u; float f; } conv;
        conv.u = static_cast<quint32>(raw);
        value = conv.f;
        return true;
    }

    bool readDouble(double &value)
    {
        qint64 raw = 0;
        if (!readLong(raw))
            return false;
        union { quint64 u; double d; } conv;
        conv.u = static_cast<quint64>(raw);
        value = conv.d;
        return true;
    }

    bool readString(QString &value)
    {
        quint16 len = 0;
        if (!readUShort(len))
            return false;
        if (pos_ + len > data_.size())
            return false;
        value = QString::fromUtf8(data_.mid(pos_, len));
        pos_ += len;
        return true;
    }

    bool readTag(NbtTag &tag)
    {
        quint8 type = 0;
        if (!readByte(type))
            return false;
        tag.type = static_cast<NbtType>(type);
        if (tag.type == NbtType::End)
            return true;
        if (!readString(tag.name))
            return false;
        return readPayload(tag.type, tag);
    }

private:
    bool readPayload(NbtType type, NbtTag &tag)
    {
        tag.type = type;
        switch (type)
        {
        case NbtType::Byte:
        {
            quint8 v = 0;
            if (!readByte(v))
                return false;
            tag.intValue = static_cast<qint64>(static_cast<qint8>(v));
            return true;
        }
        case NbtType::Short:
        {
            quint16 v = 0;
            if (!readUShort(v))
                return false;
            tag.intValue = static_cast<qint64>(static_cast<qint16>(v));
            return true;
        }
        case NbtType::Int:
        {
            qint32 v = 0;
            if (!readInt(v))
                return false;
            tag.intValue = v;
            return true;
        }
        case NbtType::Long:
            return readLong(tag.intValue);
        case NbtType::Float:
            return readFloat(tag.floatValue);
        case NbtType::Double:
            return readDouble(tag.doubleValue);
        case NbtType::String:
            return readString(tag.stringValue);
        case NbtType::ByteArray:
        {
            qint32 len = 0;
            if (!readInt(len) || len < 0)
                return false;
            if (pos_ + len > data_.size())
                return false;
            tag.byteArray = data_.mid(pos_, len);
            pos_ += len;
            return true;
        }
        case NbtType::List:
        {
            quint8 elemType = 0;
            if (!readByte(elemType))
                return false;
            qint32 len = 0;
            if (!readInt(len) || len < 0)
                return false;
            tag.listElementType = static_cast<NbtType>(elemType);
            for (qint32 i = 0; i < len; ++i)
            {
                NbtTag elem;
                if (!readPayload(static_cast<NbtType>(elemType), elem))
                    return false;
                tag.children.append(std::move(elem));
            }
            return true;
        }
        case NbtType::Compound:
        {
            while (true)
            {
                NbtTag child;
                if (!readTag(child))
                    return false;
                if (child.type == NbtType::End)
                    break;
                tag.children.append(std::move(child));
            }
            return true;
        }
        case NbtType::IntArray:
        {
            qint32 len = 0;
            if (!readInt(len) || len < 0)
                return false;
            if (pos_ + 4LL * len > data_.size())
                return false;
            for (qint32 i = 0; i < len; ++i)
            {
                qint32 v = 0;
                if (!readInt(v))
                    return false;
                tag.intArray.append(v);
            }
            return true;
        }
        case NbtType::LongArray:
        {
            qint32 len = 0;
            if (!readInt(len) || len < 0)
                return false;
            if (pos_ + 8LL * len > data_.size())
                return false;
            for (qint32 i = 0; i < len; ++i)
            {
                qint64 v = 0;
                if (!readLong(v))
                    return false;
                tag.longArray.append(v);
            }
            return true;
        }
        case NbtType::End:
        default:
            return false;
        }
    }

    QByteArray data_;
    int pos_;
};

class NbtWriter
{
public:
    void writeByte(quint8 value)
    {
        out_.append(static_cast<char>(value));
    }

    void writeUShort(quint16 value)
    {
        out_.append(static_cast<char>((value >> 8) & 0xFF));
        out_.append(static_cast<char>(value & 0xFF));
    }

    void writeInt(qint32 value)
    {
        out_.append(static_cast<char>((value >> 24) & 0xFF));
        out_.append(static_cast<char>((value >> 16) & 0xFF));
        out_.append(static_cast<char>((value >> 8) & 0xFF));
        out_.append(static_cast<char>(value & 0xFF));
    }

    void writeLong(qint64 value)
    {
        for (int i = 7; i >= 0; --i)
            out_.append(static_cast<char>((value >> (i * 8)) & 0xFF));
    }

    void writeFloat(float value)
    {
        union { quint32 u; float f; } conv;
        conv.f = value;
        writeInt(static_cast<qint32>(conv.u));
    }

    void writeDouble(double value)
    {
        union { quint64 u; double d; } conv;
        conv.d = value;
        writeLong(static_cast<qint64>(conv.u));
    }

    void writeString(const QString &value)
    {
        QByteArray utf8 = value.toUtf8();
        writeUShort(static_cast<quint16>(utf8.size()));
        out_.append(utf8);
    }

    void writeTag(const NbtTag &tag)
    {
        writeByte(static_cast<quint8>(tag.type));
        if (tag.type == NbtType::End)
            return;
        writeString(tag.name);
        writePayload(tag.type, tag);
    }

    void writeRoot(const NbtTag &root)
    {
        writeTag(root);
    }

    QByteArray data() const { return out_; }

private:
    void writePayload(NbtType type, const NbtTag &tag)
    {
        switch (type)
        {
        case NbtType::Byte:
            writeByte(static_cast<quint8>(tag.intValue & 0xFF));
            break;
        case NbtType::Short:
            writeUShort(static_cast<quint16>(tag.intValue & 0xFFFF));
            break;
        case NbtType::Int:
            writeInt(static_cast<qint32>(tag.intValue));
            break;
        case NbtType::Long:
            writeLong(tag.intValue);
            break;
        case NbtType::Float:
            writeFloat(tag.floatValue);
            break;
        case NbtType::Double:
            writeDouble(tag.doubleValue);
            break;
        case NbtType::String:
            writeString(tag.stringValue);
            break;
        case NbtType::ByteArray:
            writeInt(static_cast<qint32>(tag.byteArray.size()));
            out_.append(tag.byteArray);
            break;
        case NbtType::List:
            writeByte(static_cast<quint8>(tag.listElementType));
            writeInt(static_cast<qint32>(tag.children.size()));
            for (const NbtTag &elem : tag.children)
                writePayload(tag.listElementType, elem);
            break;
        case NbtType::Compound:
            for (const NbtTag &child : tag.children)
                writeTag(child);
            writeByte(static_cast<quint8>(NbtType::End));
            break;
        case NbtType::IntArray:
            writeInt(static_cast<qint32>(tag.intArray.size()));
            for (qint32 v : tag.intArray)
                writeInt(v);
            break;
        case NbtType::LongArray:
            writeInt(static_cast<qint32>(tag.longArray.size()));
            for (qint64 v : tag.longArray)
                writeLong(v);
            break;
        case NbtType::End:
        default:
            break;
        }
    }

    QByteArray out_;
};

bool decompressGzip(const QByteArray &compressed, QByteArray &out)
{
    if (compressed.size() < 2 ||
        static_cast<quint8>(compressed.at(0)) != 0x1f ||
        static_cast<quint8>(compressed.at(1)) != 0x8b)
    {
        return false;
    }

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.data()));
    strm.avail_in = static_cast<uInt>(compressed.size());

    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK)
        return false;

    static constexpr int BUFFER_SIZE = 65536;
    char buffer[BUFFER_SIZE];
    int ret = Z_OK;
    do
    {
        strm.next_out = reinterpret_cast<Bytef *>(buffer);
        strm.avail_out = BUFFER_SIZE;
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END)
        {
            inflateEnd(&strm);
            return false;
        }
        out.append(buffer, BUFFER_SIZE - static_cast<int>(strm.avail_out));
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return true;
}

bool compressGzip(const QByteArray &raw, QByteArray &out)
{
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(raw.data()));
    strm.avail_in = static_cast<uInt>(raw.size());

    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;

    static constexpr int BUFFER_SIZE = 65536;
    char buffer[BUFFER_SIZE];
    int ret = Z_OK;
    do
    {
        strm.next_out = reinterpret_cast<Bytef *>(buffer);
        strm.avail_out = BUFFER_SIZE;
        ret = deflate(&strm, Z_FINISH);
        if (ret == Z_STREAM_ERROR)
        {
            deflateEnd(&strm);
            return false;
        }
        out.append(buffer, BUFFER_SIZE - static_cast<int>(strm.avail_out));
    } while (ret != Z_STREAM_END);

    deflateEnd(&strm);
    return true;
}

bool loadRoot(const QString &path, NbtTag &root)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray compressed = file.readAll();
    file.close();

    QByteArray raw;
    if (!decompressGzip(compressed, raw) || raw.isEmpty())
    {
        qWarning() << "[ServersDatIO] gzip 解压失败:" << path;
        return false;
    }

    NbtReader reader(raw);
    if (!reader.readTag(root) || root.type != NbtType::Compound)
    {
        qWarning() << "[ServersDatIO] NBT 解析失败:" << path;
        return false;
    }
    return true;
}

bool saveRoot(const QString &path, const NbtTag &root)
{
    NbtWriter writer;
    writer.writeRoot(root);
    const QByteArray raw = writer.data();
    QByteArray gzipped;
    if (!compressGzip(raw, gzipped))
        return false;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 written = file.write(gzipped);
    file.close();
    return written == gzipped.size();
}

NbtTag *findChild(NbtTag &compound, const QString &name)
{
    for (NbtTag &child : compound.children)
    {
        if (child.name == name)
            return &child;
    }
    return nullptr;
}

const NbtTag *findChild(const NbtTag &compound, const QString &name)
{
    for (const NbtTag &child : compound.children)
    {
        if (child.name == name)
            return &child;
    }
    return nullptr;
}

NbtTag makeStringTag(const QString &name, const QString &value)
{
    NbtTag tag;
    tag.type = NbtType::String;
    tag.name = name;
    tag.stringValue = value;
    return tag;
}

NbtTag makeServerCompound(const QString &name, const QString &ip)
{
    NbtTag compound;
    compound.type = NbtType::Compound;
    compound.children.append(makeStringTag(QStringLiteral("name"), name));
    compound.children.append(makeStringTag(QStringLiteral("ip"), ip));
    return compound;
}

bool entryMatches(const NbtTag &server, const QString &host, quint16 port)
{
    const NbtTag *ipTag = findChild(server, QStringLiteral("ip"));
    if (!ipTag || ipTag->type != NbtType::String)
        return false;
    QString h;
    quint16 p = 25565;
    if (!ServersDatIO::parseAddress(ipTag->stringValue, h, p))
        return false;
    return ServersDatIO::addressKey(h, p) == ServersDatIO::addressKey(host, port);
}

} // namespace

QString ServersDatIO::formatAddress(const QString &host, quint16 port)
{
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('[')))
        return QStringLiteral("[%1]:%2").arg(host).arg(port);
    if (port == 25565)
        return host;
    return host + QLatin1Char(':') + QString::number(port);
}

QString ServersDatIO::addressKey(const QString &host, quint16 port)
{
    QString h = host.trimmed();
    if (h.startsWith(QLatin1Char('[')) && h.endsWith(QLatin1Char(']')))
        h = h.mid(1, h.size() - 2);
    if (port == 25565)
        return h.toLower();
    return h.toLower() + QLatin1Char(':') + QString::number(port);
}

bool ServersDatIO::parseAddress(const QString &ip, QString &hostOut, quint16 &portOut)
{
    const QString trimmed = ip.trimmed();
    if (trimmed.isEmpty())
        return false;

    if (trimmed.startsWith(QLatin1Char('[')))
    {
        const int close = trimmed.indexOf(QLatin1Char(']'));
        if (close <= 1)
            return false;
        hostOut = trimmed.mid(1, close - 1);
        if (close + 1 < trimmed.size() && trimmed.at(close + 1) == QLatin1Char(':'))
        {
            bool ok = false;
            const int p = trimmed.mid(close + 2).toInt(&ok);
            if (!ok || p <= 0 || p > 65535)
                return false;
            portOut = static_cast<quint16>(p);
        }
        else
        {
            portOut = 25565;
        }
        return !hostOut.isEmpty();
    }

    // 裸 IPv6（多个冒号且非 host:port）视为无端口
    const int colon = trimmed.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && trimmed.indexOf(QLatin1Char(':')) == colon)
    {
        const QString portStr = trimmed.mid(colon + 1);
        bool ok = false;
        const int p = portStr.toInt(&ok);
        if (ok && p > 0 && p <= 65535)
        {
            hostOut = trimmed.left(colon);
            portOut = static_cast<quint16>(p);
            return !hostOut.isEmpty();
        }
    }

    hostOut = trimmed;
    portOut = 25565;
    return true;
}

QList<ServersDatIO::Entry> ServersDatIO::read(const QString &path)
{
    QList<Entry> result;
    if (path.isEmpty() || !QFile::exists(path))
        return result;

    NbtTag root;
    if (!loadRoot(path, root))
        return result;

    const NbtTag *servers = findChild(root, QStringLiteral("servers"));
    if (!servers || servers->type != NbtType::List)
        return result;

    for (const NbtTag &item : servers->children)
    {
        if (item.type != NbtType::Compound)
            continue;
        const NbtTag *nameTag = findChild(item, QStringLiteral("name"));
        const NbtTag *ipTag = findChild(item, QStringLiteral("ip"));
        if (!ipTag || ipTag->type != NbtType::String)
            continue;

        Entry entry;
        entry.name = (nameTag && nameTag->type == NbtType::String)
                         ? nameTag->stringValue
                         : QString();
        if (!parseAddress(ipTag->stringValue, entry.host, entry.port))
            continue;
        if (entry.name.isEmpty())
            entry.name = entry.host;
        result.append(entry);
    }
    return result;
}

bool ServersDatIO::upsert(const QString &path, const Entry &entry)
{
    if (path.isEmpty() || entry.host.trimmed().isEmpty())
        return false;

    const QString ip = formatAddress(entry.host, entry.port);
    NbtTag root;
    bool hasRoot = false;

    if (QFile::exists(path))
    {
        if (!loadRoot(path, root))
            return false;
        hasRoot = true;
    }

    if (!hasRoot || root.type != NbtType::Compound)
    {
        NbtTag newRoot;
        newRoot.type = NbtType::Compound;
        NbtTag servers;
        servers.type = NbtType::List;
        servers.name = QStringLiteral("servers");
        servers.listElementType = NbtType::Compound;
        servers.children.append(makeServerCompound(entry.name, ip));
        newRoot.children.append(servers);
        return saveRoot(path, newRoot);
    }

    NbtTag *servers = findChild(root, QStringLiteral("servers"));
    if (!servers)
    {
        NbtTag list;
        list.type = NbtType::List;
        list.name = QStringLiteral("servers");
        list.listElementType = NbtType::Compound;
        list.children.append(makeServerCompound(entry.name, ip));
        root.children.append(list);
        return saveRoot(path, root);
    }

    if (servers->type != NbtType::List)
        return false;

    for (NbtTag &item : servers->children)
    {
        if (item.type != NbtType::Compound)
            continue;
        if (!entryMatches(item, entry.host, entry.port))
            continue;

        // 更新已有条目的 name / ip，保留 icon 等字段
        if (NbtTag *nameTag = findChild(item, QStringLiteral("name")))
        {
            if (nameTag->type == NbtType::String)
                nameTag->stringValue = entry.name;
        }
        else
        {
            item.children.append(makeStringTag(QStringLiteral("name"), entry.name));
        }
        if (NbtTag *ipTag = findChild(item, QStringLiteral("ip")))
        {
            if (ipTag->type == NbtType::String)
                ipTag->stringValue = ip;
        }
        else
        {
            item.children.append(makeStringTag(QStringLiteral("ip"), ip));
        }
        return saveRoot(path, root);
    }

    servers->children.append(makeServerCompound(entry.name, ip));
    return saveRoot(path, root);
}

bool ServersDatIO::remove(const QString &path, const QString &host, quint16 port)
{
    if (path.isEmpty() || !QFile::exists(path))
        return true;

    NbtTag root;
    if (!loadRoot(path, root))
        return false;

    NbtTag *servers = findChild(root, QStringLiteral("servers"));
    if (!servers || servers->type != NbtType::List)
        return true;

    bool changed = false;
    for (int i = servers->children.size() - 1; i >= 0; --i)
    {
        if (servers->children.at(i).type != NbtType::Compound)
            continue;
        if (entryMatches(servers->children.at(i), host, port))
        {
            servers->children.removeAt(i);
            changed = true;
        }
    }

    if (!changed)
        return true;
    return saveRoot(path, root);
}
