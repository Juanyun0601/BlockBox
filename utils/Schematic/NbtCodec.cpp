/**
 * @file   NbtCodec.cpp
 * @brief  通用 NBT（Named Binary Tag）编解码模块实现
 * @author BlockBox Team
 * @date   2026-08-02
 */

#include "NbtCodec.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QtZlib/zlib.h>

#include <cstring>

namespace {

// ============================================================================
// NBT 二进制读取器（大端序）
// ============================================================================
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
        {
            return false;
        }
        value = static_cast<quint8>(data_.at(pos_));
        pos_ += 1;
        return true;
    }

    bool readUShort(quint16 &value)
    {
        if (pos_ + 2 > data_.size())
        {
            return false;
        }
        value = static_cast<quint16>(
            (static_cast<quint8>(data_.at(pos_)) << 8) |
            static_cast<quint8>(data_.at(pos_ + 1)));
        pos_ += 2;
        return true;
    }

    bool readInt(qint32 &value)
    {
        if (pos_ + 4 > data_.size())
        {
            return false;
        }
        value = (static_cast<qint32>(static_cast<quint8>(data_.at(pos_))) << 24) |
                (static_cast<qint32>(static_cast<quint8>(data_.at(pos_ + 1))) << 16) |
                (static_cast<qint32>(static_cast<quint8>(data_.at(pos_ + 2))) << 8) |
                static_cast<qint32>(static_cast<quint8>(data_.at(pos_ + 3)));
        pos_ += 4;
        return true;
    }

    bool readLong(qint64 &value)
    {
        if (pos_ + 8 > data_.size())
        {
            return false;
        }
        quint64 v = 0;
        for (int i = 0; i < 8; ++i)
        {
            v = (v << 8) | static_cast<quint8>(data_.at(pos_ + i));
        }
        value = static_cast<qint64>(v);
        pos_ += 8;
        return true;
    }

    bool readFloat(float &value)
    {
        qint32 raw = 0;
        if (!readInt(raw))
        {
            return false;
        }
        union { quint32 u; float f; } conv;
        conv.u = static_cast<quint32>(raw);
        value = conv.f;
        return true;
    }

    bool readDouble(double &value)
    {
        qint64 raw = 0;
        if (!readLong(raw))
        {
            return false;
        }
        union { quint64 u; double d; } conv;
        conv.u = static_cast<quint64>(raw);
        value = conv.d;
        return true;
    }

    bool readString(QString &value)
    {
        quint16 len = 0;
        if (!readUShort(len))
        {
            return false;
        }
        if (pos_ + len > data_.size())
        {
            return false;
        }
        value = QString::fromUtf8(data_.constData() + pos_, len);
        pos_ += len;
        return true;
    }

    bool readTag(NbtTag &tag)
    {
        quint8 typeByte = 0;
        if (!readByte(typeByte))
        {
            return false;
        }
        tag.type = static_cast<NbtType>(typeByte);
        if (tag.type == NbtType::End)
        {
            return true;
        }
        if (!readString(tag.name))
        {
            return false;
        }
        return readPayload(tag);
    }

private:
    bool readPayload(NbtTag &tag)
    {
        switch (tag.type)
        {
            case NbtType::Byte:
            {
                quint8 v = 0;
                if (!readByte(v))
                {
                    return false;
                }
                tag.intValue = static_cast<qint8>(v);
                break;
            }
            case NbtType::Short:
            {
                qint32 v = 0;
                if (!readInt16(v))
                {
                    return false;
                }
                tag.intValue = v;
                break;
            }
            case NbtType::Int:
            {
                qint32 v = 0;
                if (!readInt(v))
                {
                    return false;
                }
                tag.intValue = v;
                break;
            }
            case NbtType::Long:
            {
                qint64 v = 0;
                if (!readLong(v))
                {
                    return false;
                }
                tag.intValue = v;
                break;
            }
            case NbtType::Float:
            {
                float v = 0.0f;
                if (!readFloat(v))
                {
                    return false;
                }
                tag.doubleValue = static_cast<double>(v);
                break;
            }
            case NbtType::Double:
            {
                double v = 0.0;
                if (!readDouble(v))
                {
                    return false;
                }
                tag.doubleValue = v;
                break;
            }
            case NbtType::ByteArray:
            {
                qint32 len = 0;
                if (!readInt(len) || len < 0)
                {
                    return false;
                }
                if (pos_ + len > data_.size())
                {
                    return false;
                }
                tag.byteArray = data_.mid(pos_, len);
                pos_ += len;
                break;
            }
            case NbtType::String:
            {
                QString v;
                if (!readString(v))
                {
                    return false;
                }
                tag.stringValue = v;
                break;
            }
            case NbtType::List:
            {
                quint8 elementTypeByte = 0;
                if (!readByte(elementTypeByte))
                {
                    return false;
                }
                tag.listElementType = static_cast<NbtType>(elementTypeByte);
                qint32 len = 0;
                if (!readInt(len) || len < 0)
                {
                    return false;
                }
                if (tag.listElementType == NbtType::End && len > 0)
                {
                    return false;
                }
                for (qint32 i = 0; i < len; ++i)
                {
                    NbtTag child;
                    child.type = tag.listElementType;
                    if (!readPayload(child))
                    {
                        return false;
                    }
                    tag.children.append(child);
                }
                break;
            }
            case NbtType::Compound:
            {
                while (true)
                {
                    NbtTag child;
                    if (!readTag(child))
                    {
                        return false;
                    }
                    if (child.type == NbtType::End)
                    {
                        break;
                    }
                    tag.children.append(child);
                }
                break;
            }
            case NbtType::IntArray:
            {
                qint32 len = 0;
                if (!readInt(len) || len < 0)
                {
                    return false;
                }
                for (qint32 i = 0; i < len; ++i)
                {
                    qint32 v = 0;
                    if (!readInt(v))
                    {
                        return false;
                    }
                    tag.intArray.append(v);
                }
                break;
            }
            case NbtType::LongArray:
            {
                qint32 len = 0;
                if (!readInt(len) || len < 0)
                {
                    return false;
                }
                for (qint32 i = 0; i < len; ++i)
                {
                    qint64 v = 0;
                    if (!readLong(v))
                    {
                        return false;
                    }
                    tag.longArray.append(v);
                }
                break;
            }
            case NbtType::End:
            default:
                break;
        }
        return true;
    }

    bool readInt16(qint32 &value)
    {
        if (pos_ + 2 > data_.size())
        {
            return false;
        }
        value = static_cast<qint16>(
            (static_cast<quint8>(data_.at(pos_)) << 8) |
            static_cast<quint8>(data_.at(pos_ + 1)));
        pos_ += 2;
        return true;
    }

    const QByteArray &data_;
    int pos_;
};

// ============================================================================
// NBT 二进制写入器（大端序）
// ============================================================================
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
        {
            out_.append(static_cast<char>((value >> (i * 8)) & 0xFF));
        }
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
        const QByteArray utf8 = value.toUtf8();
        writeUShort(static_cast<quint16>(utf8.size()));
        out_.append(utf8);
    }

    void writeTag(const NbtTag &tag)
    {
        writeByte(static_cast<quint8>(tag.type));
        if (tag.type == NbtType::End)
        {
            return;
        }
        writeString(tag.name);
        writePayload(tag);
    }

    void writeRoot(const NbtTag &root)
    {
        writeTag(root);
    }

    QByteArray data() const
    {
        return out_;
    }

private:
    void writePayload(const NbtTag &tag)
    {
        switch (tag.type)
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
                writeFloat(static_cast<float>(tag.doubleValue));
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
                for (const NbtTag &child : tag.children)
                {
                    writePayload(child);
                }
                break;
            case NbtType::Compound:
                for (const NbtTag &child : tag.children)
                {
                    writeTag(child);
                }
                writeByte(static_cast<quint8>(NbtType::End));
                break;
            case NbtType::IntArray:
                writeInt(static_cast<qint32>(tag.intArray.size()));
                for (qint32 v : tag.intArray)
                {
                    writeInt(v);
                }
                break;
            case NbtType::LongArray:
                writeInt(static_cast<qint32>(tag.longArray.size()));
                for (qint64 v : tag.longArray)
                {
                    writeLong(v);
                }
                break;
            case NbtType::End:
            default:
                break;
        }
    }

    QByteArray out_;
};

// ============================================================================
// gzip 解压：使用 zlib inflate 自动识别 gzip 头
// ============================================================================
bool gzipDecompress(const QByteArray &compressed, QByteArray &out)
{
    if (compressed.size() < 2)
    {
        return false;
    }
    if (static_cast<quint8>(compressed.at(0)) != 0x1f ||
        static_cast<quint8>(compressed.at(1)) != 0x8b)
    {
        return false;
    }

    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.data()));
    strm.avail_in = static_cast<uInt>(compressed.size());

    int ret = inflateInit2(&strm, 16 + MAX_WBITS);
    if (ret != Z_OK)
    {
        return false;
    }

    static constexpr int BUFFER_SIZE = 65536;
    char buffer[BUFFER_SIZE];

    do
    {
        strm.next_out = reinterpret_cast<Bytef *>(buffer);
        strm.avail_out = BUFFER_SIZE;
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT ||
            ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
        {
            inflateEnd(&strm);
            return false;
        }
        out.append(buffer, BUFFER_SIZE - static_cast<int>(strm.avail_out));
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return true;
}

// ============================================================================
// gzip 压缩：使用 zlib deflate 生成标准 gzip 流
// ============================================================================
bool gzipCompress(const QByteArray &raw, QByteArray &out)
{
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));

    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        return false;
    }

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(raw.data()));
    strm.avail_in = static_cast<uInt>(raw.size());

    static constexpr int BUFFER_SIZE = 65536;
    char buffer[BUFFER_SIZE];

    int ret;
    do
    {
        strm.next_out = reinterpret_cast<Bytef *>(buffer);
        strm.avail_out = BUFFER_SIZE;
        ret = deflate(&strm, Z_FINISH);
        if (ret == Z_STREAM_ERROR || ret == Z_MEM_ERROR)
        {
            deflateEnd(&strm);
            return false;
        }
        out.append(buffer, BUFFER_SIZE - static_cast<int>(strm.avail_out));
    } while (ret != Z_STREAM_END);

    deflateEnd(&strm);
    return true;
}

} // namespace

namespace NbtCodec {

std::optional<NbtTag> parse(const QByteArray &data)
{
    if (data.isEmpty())
    {
        return std::nullopt;
    }
    NbtReader reader(data);
    NbtTag root;
    if (!reader.readTag(root) || root.type != NbtType::Compound)
    {
        return std::nullopt;
    }
    return root;
}

QByteArray serialize(const NbtTag &root)
{
    if (root.type != NbtType::Compound)
    {
        return QByteArray();
    }
    NbtWriter writer;
    writer.writeRoot(root);
    return writer.data();
}

QByteArray gzipCompress(const QByteArray &raw, bool *ok)
{
    QByteArray out;
    const bool success = ::gzipCompress(raw, out);
    if (ok != nullptr)
    {
        *ok = success;
    }
    return success ? out : QByteArray();
}

QByteArray gzipDecompress(const QByteArray &compressed, bool *ok)
{
    QByteArray out;
    const bool success = ::gzipDecompress(compressed, out);
    if (ok != nullptr)
    {
        *ok = success;
    }
    return success ? out : QByteArray();
}

bool readCompressedFile(const QString &path, NbtTag &root)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }
    const QByteArray compressed = file.readAll();
    file.close();

    QByteArray raw;
    if (!::gzipDecompress(compressed, raw))
    {
        return false;
    }

    auto parsed = parse(raw);
    if (!parsed)
    {
        return false;
    }
    root = std::move(*parsed);
    return true;
}

bool writeCompressedFile(const QString &path, const NbtTag &root)
{
    const QByteArray raw = serialize(root);
    if (raw.isEmpty())
    {
        return false;
    }

    QByteArray gzipped;
    if (!::gzipCompress(raw, gzipped))
    {
        return false;
    }

    // 原子写入：先写临时文件，再替换
    const QString tmpPath = path + QStringLiteral(".tmp");
    {
        QFile tmp(tmpPath);
        if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return false;
        }
        if (tmp.write(gzipped) != gzipped.size())
        {
            tmp.close();
            QFile::remove(tmpPath);
            return false;
        }
        tmp.close();
    }

    QFile::remove(path);
    if (!QFile::rename(tmpPath, path))
    {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

NbtTag *findChild(NbtTag &compound, const QString &name)
{
    if (compound.type != NbtType::Compound)
    {
        return nullptr;
    }
    for (NbtTag &child : compound.children)
    {
        if (child.name == name)
        {
            return &child;
        }
    }
    return nullptr;
}

const NbtTag *findChild(const NbtTag &compound, const QString &name)
{
    if (compound.type != NbtType::Compound)
    {
        return nullptr;
    }
    for (const NbtTag &child : compound.children)
    {
        if (child.name == name)
        {
            return &child;
        }
    }
    return nullptr;
}

qint64 readIntValue(const NbtTag &compound, const QString &name, qint64 defaultValue)
{
    const NbtTag *child = findChild(compound, name);
    if (child == nullptr)
    {
        return defaultValue;
    }
    switch (child->type)
    {
        case NbtType::Byte:
        case NbtType::Short:
        case NbtType::Int:
        case NbtType::Long:
            return child->intValue;
        default:
            return defaultValue;
    }
}

QString readStringValue(const NbtTag &compound, const QString &name,
                        const QString &defaultValue)
{
    const NbtTag *child = findChild(compound, name);
    if (child == nullptr || child->type != NbtType::String)
    {
        return defaultValue;
    }
    return child->stringValue;
}

NbtTag makeCompound(const QString &name)
{
    NbtTag tag;
    tag.type = NbtType::Compound;
    tag.name = name;
    return tag;
}

NbtTag makeString(const QString &name, const QString &value)
{
    NbtTag tag;
    tag.type = NbtType::String;
    tag.name = name;
    tag.stringValue = value;
    return tag;
}

NbtTag makeInt(const QString &name, qint32 value)
{
    NbtTag tag;
    tag.type = NbtType::Int;
    tag.name = name;
    tag.intValue = value;
    return tag;
}

NbtTag makeShort(const QString &name, qint16 value)
{
    NbtTag tag;
    tag.type = NbtType::Short;
    tag.name = name;
    tag.intValue = value;
    return tag;
}

NbtTag makeLong(const QString &name, qint64 value)
{
    NbtTag tag;
    tag.type = NbtType::Long;
    tag.name = name;
    tag.intValue = value;
    return tag;
}

NbtTag makeByte(const QString &name, qint8 value)
{
    NbtTag tag;
    tag.type = NbtType::Byte;
    tag.name = name;
    tag.intValue = value;
    return tag;
}

NbtTag makeByteArray(const QString &name, const QByteArray &value)
{
    NbtTag tag;
    tag.type = NbtType::ByteArray;
    tag.name = name;
    tag.byteArray = value;
    return tag;
}

NbtTag makeIntArray(const QString &name, const QList<qint32> &value)
{
    NbtTag tag;
    tag.type = NbtType::IntArray;
    tag.name = name;
    tag.intArray = value;
    return tag;
}

NbtTag makeLongArray(const QString &name, const QList<qint64> &value)
{
    NbtTag tag;
    tag.type = NbtType::LongArray;
    tag.name = name;
    tag.longArray = value;
    return tag;
}

NbtTag makeList(const QString &name, NbtType elementType, const QList<NbtTag> &items)
{
    NbtTag tag;
    tag.type = NbtType::List;
    tag.name = name;
    tag.listElementType = elementType;
    tag.children = items;
    return tag;
}

} // namespace NbtCodec
