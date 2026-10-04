/**
 * @file   LevelDatEditor.cpp
 * @brief  Minecraft 单机存档 level.dat 读写编辑器实现
 * @author BlockBox Team
 * @date   2026-08-01
 *
 * 实现细节：
 *   1. gzip 解压/压缩：使用 zlib 的 inflate/deflate（windowBits = 16 + MAX_WBITS），
 *      可自动识别/生成 gzip 头部。文件读写使用 QFile 以避免路径编码问题。
 *   2. NBT 解析与序列化：基于游标的二进制读写器，递归构建完整标签树，
 *      覆盖全部 13 种标签类型（End/Byte/Short/Int/Long/Float/Double/
 *      ByteArray/String/List/Compound/IntArray/LongArray），保证修改后
 *      重写 level.dat 时其它字段原样保留。
 *   3. 字段定位：在根 Compound 下查找 Data 子标签，从中提取各字段。
 *   4. 修改写入：更新内存树后整体重写。难度字段同时兼容旧格式
 *      （Data.Difficulty / DifficultyLocked）与新格式（difficulty_settings
 *      复合标签），保证不同版本存档均能生效。
 *   5. 原子写入：临时文件 → level.dat → level.dat_old 备份 → 重命名替换。
 */

#include "LevelDatEditor.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>

#include <QtZlib/zlib.h>

namespace {

// ============================================================================
// NBT 标签类型 ID（详见 NBT 官方规范）
// ============================================================================
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

// ============================================================================
// NBT 标签节点（完整保留所有类型，保证可无损重写）
// ============================================================================
struct NbtTag
{
    NbtType type = NbtType::End;
    QString name;
    qint64 intValue = 0;                     ///< Byte/Short/Int/Long
    float floatValue = 0.0f;                 ///< Float
    double doubleValue = 0.0;                ///< Double
    QString stringValue;                     ///< String
    QByteArray byteArray;                    ///< ByteArray
    QList<qint32> intArray;                  ///< IntArray
    QList<qint64> longArray;                 ///< LongArray
    NbtType listElementType = NbtType::End;  ///< List 元素类型
    QList<NbtTag> children;                  ///< Compound 子标签或 List 元素
};

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
        // 大端 4 字节 → float
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
        // NBT 文件根标签为复合标签，其 payload 末尾自带 TAG_End，无需额外写入
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
            {
                writeByte(static_cast<quint8>(tag.listElementType));
                writeInt(static_cast<qint32>(tag.children.size()));
                for (const NbtTag &child : tag.children)
                    writePayload(tag.listElementType, child);
                break;
            }
            case NbtType::Compound:
                for (const NbtTag &child : tag.children)
                    writeTag(child);
                writeByte(static_cast<quint8>(NbtType::End));
                break;
            case NbtType::IntArray:
            {
                writeInt(static_cast<qint32>(tag.intArray.size()));
                for (qint32 v : tag.intArray)
                    writeInt(v);
                break;
            }
            case NbtType::LongArray:
            {
                writeInt(static_cast<qint32>(tag.longArray.size()));
                for (qint64 v : tag.longArray)
                    writeLong(v);
                break;
            }
            case NbtType::End:
            default:
                break;
        }
    }

    QByteArray out_;
};

// ============================================================================
// 辅助查找：在 Compound 子标签中按名称查找（返回可写引用）
// ============================================================================
int findChildIndex(const NbtTag &compound, const QString &name)
{
    if (compound.type != NbtType::Compound)
        return -1;
    for (int i = 0; i < compound.children.size(); ++i)
    {
        if (compound.children.at(i).name == name)
            return i;
    }
    return -1;
}

NbtTag *findChild(NbtTag &compound, const QString &name)
{
    const int idx = findChildIndex(compound, name);
    return (idx >= 0) ? &compound.children[idx] : nullptr;
}

const NbtTag *findChild(const NbtTag &compound, const QString &name)
{
    const int idx = findChildIndex(compound, name);
    return (idx >= 0) ? &compound.children.at(idx) : nullptr;
}

// ============================================================================
// gzip 解压：使用 zlib inflate 自动识别 gzip 头
// ============================================================================
bool decompressGzip(const QByteArray &compressed, QByteArray &out)
{
    if (compressed.size() < 2)
        return false;
    // 验证 gzip 魔数（0x1f 0x8b）
    if (static_cast<quint8>(compressed.at(0)) != 0x1f ||
        static_cast<quint8>(compressed.at(1)) != 0x8b)
        return false;

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.data()));
    strm.avail_in = static_cast<uInt>(compressed.size());

    // 16 + MAX_WBITS 告诉 inflate 自动处理 gzip 头部
    int ret = inflateInit2(&strm, 16 + MAX_WBITS);
    if (ret != Z_OK)
        return false;

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
// gzip 压缩：使用 zlib deflate 生成 gzip 文件
// ============================================================================
bool compressGzip(const QByteArray &raw, QByteArray &out)
{
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(raw.data()));
    strm.avail_in = static_cast<uInt>(raw.size());

    // 16 + MAX_WBITS 告诉 deflate 生成带 gzip 头的流
    int ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                           16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK)
        return false;

    static constexpr int BUFFER_SIZE = 65536;
    char buffer[BUFFER_SIZE];

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

// ============================================================================
// 辅助：读取 Byte/Int/Long/String 值（带类型校验）
// ============================================================================
bool readByteValue(const NbtTag &tag, qint64 &value)
{
    if (tag.type != NbtType::Byte)
        return false;
    value = tag.intValue;
    return true;
}

bool readIntValue(const NbtTag &tag, qint64 &value)
{
    if (tag.type != NbtType::Int)
        return false;
    value = tag.intValue;
    return true;
}

bool readLongValue(const NbtTag &tag, qint64 &value)
{
    if (tag.type != NbtType::Long)
        return false;
    value = tag.intValue;
    return true;
}

// ============================================================================
// 辅助：设置/创建指定名称的标签（保留已有标签位置）
// ============================================================================
template <typename TSetter>
void upsertTag(NbtTag &compound, const QString &name, TSetter &&setter)
{
    int idx = findChildIndex(compound, name);
    if (idx >= 0)
    {
        setter(compound.children[idx]);
    }
    else
    {
        NbtTag tag;
        tag.name = name;
        setter(tag);
        compound.children.append(std::move(tag));
    }
}

// ============================================================================
// level.dat 字段读取
// ============================================================================
void readBasicInfo(const NbtTag &data, SaveInfo &info)
{
    // LevelName（TAG_String）
    if (const NbtTag *t = findChild(data, QStringLiteral("LevelName"));
        t && t->type == NbtType::String)
        info.levelName = t->stringValue;

    // Version.Name（TAG_String）
    if (const NbtTag *v = findChild(data, QStringLiteral("Version")))
    {
        if (const NbtTag *n = findChild(*v, QStringLiteral("Name"));
            n && n->type == NbtType::String)
            info.versionName = n->stringValue;
    }

    // LastPlayed（TAG_Long）
    if (const NbtTag *t = findChild(data, QStringLiteral("LastPlayed")))
        readLongValue(*t, info.lastPlayed);

    // Time（TAG_Long）—— 累计游戏时长（游戏刻）
    if (const NbtTag *t = findChild(data, QStringLiteral("Time")))
        readLongValue(*t, info.playTimeTicks);

    // DayTime（TAG_Long）—— 游戏内时间（游戏刻，模 24000）
    if (const NbtTag *t = findChild(data, QStringLiteral("DayTime")))
    {
        qint64 v = 0;
        if (readLongValue(*t, v))
        {
            info.hasDayTime = true;
            info.dayTime = v;
        }
    }

    // GameType（TAG_Int）
    if (const NbtTag *t = findChild(data, QStringLiteral("GameType")))
    {
        qint64 v = 0;
        if (readIntValue(*t, v))
        {
            info.gameType = static_cast<int>(v);
            info.hasGameType = true;
        }
    }

    // allowCommands（TAG_Byte）
    if (const NbtTag *t = findChild(data, QStringLiteral("allowCommands")))
    {
        qint64 v = 0;
        if (readByteValue(*t, v))
            info.allowCommands = (v != 0);
    }

    // hardcore（TAG_Byte）
    if (const NbtTag *t = findChild(data, QStringLiteral("hardcore")))
    {
        qint64 v = 0;
        if (readByteValue(*t, v))
            info.hardcore = (v != 0);
    }
}

void readSpawn(const NbtTag &data, SaveInfo &info)
{
    qint64 x = 0, y = 0, z = 0;
    bool ok = true;
    if (const NbtTag *t = findChild(data, QStringLiteral("SpawnX")))
        ok = readIntValue(*t, x) && ok;
    else
        ok = false;
    if (const NbtTag *t = findChild(data, QStringLiteral("SpawnY")))
        ok = readIntValue(*t, y) && ok;
    else
        ok = false;
    if (const NbtTag *t = findChild(data, QStringLiteral("SpawnZ")))
        ok = readIntValue(*t, z) && ok;
    else
        ok = false;

    if (ok)
    {
        info.hasSpawn = true;
        info.spawnX = static_cast<int>(x);
        info.spawnY = static_cast<int>(y);
        info.spawnZ = static_cast<int>(z);
        return;
    }

    // 新格式：spawn.pos int[3]
    if (const NbtTag *spawn = findChild(data, QStringLiteral("spawn")))
    {
        if (const NbtTag *pos = findChild(*spawn, QStringLiteral("pos"));
            pos && pos->type == NbtType::IntArray && pos->intArray.size() >= 3)
        {
            info.hasSpawn = true;
            info.spawnX = pos->intArray.at(0);
            info.spawnY = pos->intArray.at(1);
            info.spawnZ = pos->intArray.at(2);
        }
    }
}

void readSeed(const NbtTag &data, SaveInfo &info)
{
    // 1.16+：WorldGenSettings.seed（TAG_Long）
    if (const NbtTag *wgs = findChild(data, QStringLiteral("WorldGenSettings")))
    {
        if (const NbtTag *seed = findChild(*wgs, QStringLiteral("seed")))
        {
            qint64 v = 0;
            if (readLongValue(*seed, v))
            {
                info.hasSeed = true;
                info.seed = v;
                return;
            }
        }
    }
    // 旧版本：RandomSeed（TAG_Long）
    if (const NbtTag *seed = findChild(data, QStringLiteral("RandomSeed")))
    {
        qint64 v = 0;
        if (readLongValue(*seed, v))
        {
            info.hasSeed = true;
            info.seed = v;
        }
    }
}

void readDifficulty(const NbtTag &data, SaveInfo &info)
{
    // 新格式：difficulty_settings.difficulty（TAG_String）
    if (const NbtTag *ds = findChild(data, QStringLiteral("difficulty_settings")))
    {
        const NbtTag *diff = findChild(*ds, QStringLiteral("difficulty"));
        if (diff && diff->type == NbtType::String)
        {
            const QString &s = diff->stringValue;
            if (s == QStringLiteral("peaceful"))
                info.difficulty = 0;
            else if (s == QStringLiteral("easy"))
                info.difficulty = 1;
            else if (s == QStringLiteral("normal"))
                info.difficulty = 2;
            else if (s == QStringLiteral("hard"))
                info.difficulty = 3;
            info.hasDifficulty = true;
        }
        if (const NbtTag *hc = findChild(*ds, QStringLiteral("hardcore"));
            hc && hc->type == NbtType::Byte)
            info.hardcore = (hc->intValue != 0);
        if (const NbtTag *lk = findChild(*ds, QStringLiteral("locked"));
            lk && lk->type == NbtType::Byte)
            info.difficultyLocked = (lk->intValue != 0);
    }

    // 旧格式：Difficulty（TAG_Byte）
    if (const NbtTag *diff = findChild(data, QStringLiteral("Difficulty"));
        diff && diff->type == NbtType::Byte)
    {
        info.hasDifficulty = true;
        info.difficulty = static_cast<int>(diff->intValue);
    }

    // DifficultyLocked（TAG_Byte）
    if (const NbtTag *lk = findChild(data, QStringLiteral("DifficultyLocked"));
        lk && lk->type == NbtType::Byte)
        info.difficultyLocked = (lk->intValue != 0);
}

// 外部种子文件（26.x 新版本体系：data/minecraft/world_gen_settings.dat）
void readExternalSeed(const QString &folderPath, SaveInfo &info)
{
    if (info.hasSeed)
        return;
    const QString externalPath = folderPath + QStringLiteral("/data/minecraft/world_gen_settings.dat");
    if (!QFileInfo::exists(externalPath))
        return;

    QFile file(externalPath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QByteArray compressed = file.readAll();
    file.close();

    QByteArray raw;
    if (!decompressGzip(compressed, raw))
        return;

    NbtReader reader(raw);
    NbtTag root;
    if (!reader.readTag(root) || root.type != NbtType::Compound)
        return;

    const NbtTag *dataTag = findChild(root, QStringLiteral("data"));
    if (!dataTag)
        return;
    const NbtTag *seed = findChild(*dataTag, QStringLiteral("seed"));
    qint64 v = 0;
    if (seed && readLongValue(*seed, v))
    {
        info.hasSeed = true;
        info.seed = v;
    }
}

// ============================================================================
// level.dat 字段写入
// ============================================================================
QString difficultyString(int difficulty)
{
    switch (difficulty)
    {
    case 0: return QStringLiteral("peaceful");
    case 1: return QStringLiteral("easy");
    case 2: return QStringLiteral("normal");
    case 3: return QStringLiteral("hard");
    default: return QStringLiteral("normal");
    }
}

void applyChangesToData(NbtTag &data, const SaveChanges &changes)
{
    // 允许作弊：Data.allowCommands（TAG_Byte）
    if (changes.allowCommands.has_value())
    {
        upsertTag(data, QStringLiteral("allowCommands"), [&](NbtTag &tag) {
            tag.type = NbtType::Byte;
            tag.intValue = changes.allowCommands.value() ? 1 : 0;
        });
    }

    // 游戏模式：Data.GameType（TAG_Int）
    if (changes.gameType.has_value())
    {
        upsertTag(data, QStringLiteral("GameType"), [&](NbtTag &tag) {
            tag.type = NbtType::Int;
            tag.intValue = changes.gameType.value();
        });
    }

    // 极限模式：Data.hardcore（TAG_Byte）；开启时强制难度为困难并锁定
    if (changes.hardcore.has_value())
    {
        const bool hc = changes.hardcore.value();
        upsertTag(data, QStringLiteral("hardcore"), [&](NbtTag &tag) {
            tag.type = NbtType::Byte;
            tag.intValue = hc ? 1 : 0;
        });
        // 新格式：difficulty_settings.hardcore（TAG_Byte）
        if (NbtTag *ds = findChild(data, QStringLiteral("difficulty_settings")))
        {
            upsertTag(*ds, QStringLiteral("hardcore"), [&](NbtTag &tag) {
                tag.type = NbtType::Byte;
                tag.intValue = hc ? 1 : 0;
            });
        }
        // 极限模式强制难度=困难（3）并锁定
        if (hc)
        {
            upsertTag(data, QStringLiteral("Difficulty"), [&](NbtTag &tag) {
                tag.type = NbtType::Byte;
                tag.intValue = 3;
            });
            upsertTag(data, QStringLiteral("DifficultyLocked"), [&](NbtTag &tag) {
                tag.type = NbtType::Byte;
                tag.intValue = 1;
            });
            if (NbtTag *ds = findChild(data, QStringLiteral("difficulty_settings")))
            {
                upsertTag(*ds, QStringLiteral("difficulty"), [&](NbtTag &tag) {
                    tag.type = NbtType::String;
                    tag.stringValue = QStringLiteral("hard");
                });
                upsertTag(*ds, QStringLiteral("locked"), [&](NbtTag &tag) {
                    tag.type = NbtType::Byte;
                    tag.intValue = 1;
                });
            }
        }
    }

    // 世界名称：Data.LevelName（TAG_String）
    if (changes.levelName.has_value())
    {
        upsertTag(data, QStringLiteral("LevelName"), [&](NbtTag &tag) {
            tag.type = NbtType::String;
            tag.stringValue = changes.levelName.value();
        });
    }

    // 游戏内时间：Data.DayTime（TAG_Long）
    if (changes.dayTime.has_value())
    {
        upsertTag(data, QStringLiteral("DayTime"), [&](NbtTag &tag) {
            tag.type = NbtType::Long;
            tag.intValue = changes.dayTime.value();
        });
    }

    // 难度
    if (changes.difficulty.has_value())
    {
        const int diff = changes.difficulty.value();
        // 旧格式：Data.Difficulty（TAG_Byte）
        upsertTag(data, QStringLiteral("Difficulty"), [&](NbtTag &tag) {
            tag.type = NbtType::Byte;
            tag.intValue = diff;
        });
        // 新格式：difficulty_settings.difficulty（TAG_String）
        if (NbtTag *ds = findChild(data, QStringLiteral("difficulty_settings")))
        {
            upsertTag(*ds, QStringLiteral("difficulty"), [&](NbtTag &tag) {
                tag.type = NbtType::String;
                tag.stringValue = difficultyString(diff);
            });
        }
    }

    // 锁定难度
    if (changes.difficultyLocked.has_value())
    {
        const int locked = changes.difficultyLocked.value() ? 1 : 0;
        // 旧格式：Data.DifficultyLocked（TAG_Byte）
        upsertTag(data, QStringLiteral("DifficultyLocked"), [&](NbtTag &tag) {
            tag.type = NbtType::Byte;
            tag.intValue = locked;
        });
        // 新格式：difficulty_settings.locked（TAG_Byte）
        if (NbtTag *ds = findChild(data, QStringLiteral("difficulty_settings")))
        {
            upsertTag(*ds, QStringLiteral("locked"), [&](NbtTag &tag) {
                tag.type = NbtType::Byte;
                tag.intValue = locked;
            });
        }
    }

    // 出生点：旧格式 SpawnX/SpawnY/SpawnZ（TAG_Int）+ 新格式 spawn.pos int[3]
    const bool hasSpawnX = changes.spawnX.has_value();
    const bool hasSpawnY = changes.spawnY.has_value();
    const bool hasSpawnZ = changes.spawnZ.has_value();
    if (hasSpawnX || hasSpawnY || hasSpawnZ)
    {
        auto setSpawnInt = [&](const QString &name, const std::optional<int> &val) {
            if (!val.has_value())
                return;
            upsertTag(data, name, [&](NbtTag &tag) {
                tag.type = NbtType::Int;
                tag.intValue = val.value();
            });
        };
        setSpawnInt(QStringLiteral("SpawnX"), changes.spawnX);
        setSpawnInt(QStringLiteral("SpawnY"), changes.spawnY);
        setSpawnInt(QStringLiteral("SpawnZ"), changes.spawnZ);

        // 新格式：spawn.pos int[3]（三个都具备时整体替换）
        if (NbtTag *spawn = findChild(data, QStringLiteral("spawn")))
        {
            if (NbtTag *pos = findChild(*spawn, QStringLiteral("pos"));
                pos && pos->type == NbtType::IntArray && pos->intArray.size() >= 3)
            {
                if (hasSpawnX) pos->intArray[0] = changes.spawnX.value();
                if (hasSpawnY) pos->intArray[1] = changes.spawnY.value();
                if (hasSpawnZ) pos->intArray[2] = changes.spawnZ.value();
            }
        }
    }
}

// ============================================================================
// 原子写入：临时文件 → 备份 level.dat_old → 重命名替换
// ============================================================================
bool writeLevelDatAtomically(const QString &levelDatPath, const QByteArray &rawNbt)
{
    QByteArray gzipped;
    if (!compressGzip(rawNbt, gzipped))
        return false;

    QDir dir = QFileInfo(levelDatPath).absoluteDir();
    const QString tempPath = dir.absoluteFilePath(QStringLiteral("level.tmp.blockbox.dat"));
    const QString backupPath = dir.absoluteFilePath(QStringLiteral("level.dat_old"));

    // 1. 写临时文件
    QFile temp(tempPath);
    if (!temp.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    temp.write(gzipped);
    temp.flush();
    temp.close();

    // 2. 备份现有 level.dat → level.dat_old
    if (QFile::exists(levelDatPath))
    {
        if (QFile::exists(backupPath))
            QFile::remove(backupPath);
        if (!QFile::copy(levelDatPath, backupPath))
        {
            QFile::remove(tempPath);
            return false;
        }
    }

    // 3. 替换
    if (!QFile::exists(levelDatPath))
    {
        if (!QFile::rename(tempPath, levelDatPath))
        {
            QFile::remove(tempPath);
            return false;
        }
    }
    else
    {
        if (!QFile::remove(levelDatPath))
        {
            QFile::remove(tempPath);
            return false;
        }
        if (!QFile::rename(tempPath, levelDatPath))
        {
            QFile::remove(tempPath);
            return false;
        }
    }
    return true;
}

} // namespace

// ============================================================================
// LevelDatEditor 公开接口实现
// ============================================================================

std::optional<SaveInfo> LevelDatEditor::readSave(const QString &folderPath)
{
    SaveInfo info;
    info.folderPath = folderPath;

    QString levelDatPath = folderPath + QStringLiteral("/level.dat");
    if (!QFileInfo::exists(levelDatPath))
    {
        // 回退到 level.dat_old
        const QString backup = folderPath + QStringLiteral("/level.dat_old");
        if (!QFileInfo::exists(backup))
        {
            qWarning() << "[LevelDatEditor] level.dat not found:" << folderPath;
            return std::nullopt;
        }
        levelDatPath = backup;
    }
    info.levelDatPath = levelDatPath;

    QFile file(levelDatPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "[LevelDatEditor] Cannot open:" << levelDatPath
                   << file.errorString();
        return std::nullopt;
    }
    QByteArray compressed = file.readAll();
    file.close();

    QByteArray raw;
    if (!decompressGzip(compressed, raw) || raw.isEmpty())
    {
        qWarning() << "[LevelDatEditor] Decompress failed:" << levelDatPath;
        return std::nullopt;
    }

    NbtReader reader(raw);
    NbtTag root;
    if (!reader.readTag(root) || root.type != NbtType::Compound)
    {
        qWarning() << "[LevelDatEditor] NBT parse failed:" << levelDatPath;
        return std::nullopt;
    }

    const NbtTag *dataTag = findChild(root, QStringLiteral("Data"));
    if (!dataTag)
    {
        qWarning() << "[LevelDatEditor] Missing Data tag:" << levelDatPath;
        return std::nullopt;
    }

    readBasicInfo(*dataTag, info);
    readSpawn(*dataTag, info);
    readSeed(*dataTag, info);
    readDifficulty(*dataTag, info);
    readExternalSeed(folderPath, info);

    return info;
}

bool LevelDatEditor::applyChanges(const QString &folderPath,
                                  const SaveChanges &changes,
                                  QString *errorOut)
{
    if (changes.isEmpty())
        return false;

    auto fail = [errorOut](const QString &msg) {
        qWarning() << "[LevelDatEditor]" << msg;
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    QString levelDatPath = folderPath + QStringLiteral("/level.dat");
    if (!QFileInfo::exists(levelDatPath))
    {
        const QString backup = folderPath + QStringLiteral("/level.dat_old");
        if (!QFileInfo::exists(backup))
            return fail(QStringLiteral("存档缺少 level.dat 文件"));
        levelDatPath = backup;
    }

    QFile file(levelDatPath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("无法打开 level.dat：") + file.errorString());

    QByteArray compressed = file.readAll();
    file.close();

    QByteArray raw;
    if (!decompressGzip(compressed, raw) || raw.isEmpty())
        return fail(QStringLiteral("level.dat 解压失败"));

    NbtReader reader(raw);
    NbtTag root;
    if (!reader.readTag(root) || root.type != NbtType::Compound)
        return fail(QStringLiteral("level.dat NBT 解析失败"));

    NbtTag *dataTag = findChild(root, QStringLiteral("Data"));
    if (!dataTag)
        return fail(QStringLiteral("level.dat 缺少 Data 标签"));

    applyChangesToData(*dataTag, changes);

    // 重新序列化并原子写入
    NbtWriter writer;
    writer.writeRoot(root);
    if (!writeLevelDatAtomically(levelDatPath, writer.data()))
        return fail(QStringLiteral("level.dat 写入失败"));

    return true;
}

bool LevelDatEditor::renameWorld(const QString &folderPath,
                                 const QString &newFolderName,
                                 QString *newPathOut,
                                 QString *errorOut)
{
    auto fail = [errorOut](const QString &msg) {
        qWarning() << "[LevelDatEditor]" << msg;
        if (errorOut)
            *errorOut = msg;
        return false;
    };

    QString newName = newFolderName.trimmed();
    if (newName.isEmpty())
        return fail(QStringLiteral("新的世界名称不能为空"));

    // 不允许包含路径分隔符等非法字符
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    for (const QChar &c : newName)
    {
        if (forbidden.contains(c))
            return fail(QStringLiteral("世界名称不能包含字符：/ \\ : * ? \" < > |"));
    }

    QDir oldDir(folderPath);
    if (!oldDir.exists())
        return fail(QStringLiteral("存档文件夹不存在"));

    const QString oldFolderName = oldDir.dirName();
    if (oldFolderName == newName)
        return fail(QStringLiteral("世界名称未改变"));

    QDir parentDir = oldDir;
    parentDir.cdUp();
    const QString newPath = parentDir.absoluteFilePath(newName);

    if (QDir(newPath).exists())
        return fail(QStringLiteral("已存在同名文件夹：%1").arg(newName));

    // 先重命名文件夹
    if (!oldDir.rename(folderPath, newPath))
        return fail(QStringLiteral("文件夹重命名失败"));

    // 再更新 level.dat 中的世界名
    QString error;
    SaveChanges changes;
    changes.levelName = newName;
    if (!applyChanges(newPath, changes, &error))
    {
        // 更新失败时尽量还原文件夹名，避免用户找不到存档
        if (!QDir(newPath).rename(newPath, folderPath))
            qWarning() << "[LevelDatEditor] 回滚文件夹名失败：" << folderPath;
        return fail(QStringLiteral("更新世界名失败：") + error);
    }

    if (newPathOut)
        *newPathOut = newPath;
    return true;
}
