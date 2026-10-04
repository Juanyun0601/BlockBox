/**
 * @file   LevelDatReader.cpp
 * @brief  Minecraft level.dat 文件读取器实现
 * @author BlockBox Team
 * @date   2026-07-04
 *
 * 实现细节：
 *   1. gzip 解压：使用 zlib 的 inflate 函数（windowBits = 16 + MAX_WBITS），
 *      可自动识别并解压 gzip 头部。文件读取使用 QFile 以避免路径编码问题。
 *   2. NBT 解析：基于游标的二进制读取器，递归构建 NBT 标签树，
 *      仅完整解析 Compound / List / String / Byte / Short / Int / Long，
 *      对其它类型（Float/Double/ByteArray/IntArray/LongArray）仅跳过 payload。
 *   3. 字段定位：在根 Compound 下查找 Data 子标签，再从 Data 中提取
 *      allowCommands / GameType / Version.Name 三个字段。
 *
 * 手动测试方法（SubTask 2.5）：
 *   - 准备一个真实的 Minecraft 单机存档目录（如 .minecraft/saves/World1/）
 *   - 调用 LevelDatReader::readLevelDat(".../saves/World1/level.dat")
 *   - 验证返回的 LevelInfo 中：
 *       * allowCommands 与游戏内"局域网联机"中的"作弊"开关状态一致
 *       * gameType 与游戏内 F3 状态显示的模式一致（0=生存/1=创造/2=冒险/3=观察者）
 *       * versionName 与游戏主界面或 F3 显示的版本号一致
 *   - 异常用例：
 *       * 传入不存在的路径 -> 返回 std::nullopt，stderr 输出 "File not found"
 *       * 传入非 gzip 文件 -> 返回 std::nullopt，stderr 输出 "Not a gzip file"
 *       * 传入损坏的 level.dat -> 返回 std::nullopt，stderr 输出 "NBT parse failed"
 */

#include "LevelDatReader.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>

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
// NBT 标签节点（仅保留本模块所需字段）
// ============================================================================
struct NbtTag
{
    NbtType type = NbtType::End;
    QString name;
    qint64 intValue = 0;                     ///< Byte/Short/Int/Long 统一存为 64 位有符号
    QString stringValue;                     ///< String 类型的值
    QList<NbtTag> children;                  ///< Compound 或 List 的子元素
    NbtType listElementType = NbtType::End;  ///< List 元素类型
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

    /**
     * @brief 读取单字节
     */
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

    /**
     * @brief 读取 2 字节大端无符号整数（用于字符串/数组长度）
     */
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

    /**
     * @brief 读取 4 字节大端有符号整数
     */
    bool readInt(qint32 &value)
    {
        if (pos_ + 4 > data_.size())
        {
            return false;
        }
        value = (static_cast<quint8>(data_.at(pos_)) << 24) |
                (static_cast<quint8>(data_.at(pos_ + 1)) << 16) |
                (static_cast<quint8>(data_.at(pos_ + 2)) << 8) |
                static_cast<quint8>(data_.at(pos_ + 3));
        pos_ += 4;
        return true;
    }

    /**
     * @brief 读取 8 字节大端有符号整数
     */
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

    /**
     * @brief 读取 UTF-8 字符串（前 2 字节大端长度）
     *
     * NBT 使用 Java modified UTF-8，对 ASCII 字符与标准 UTF-8 一致；
     * QString::fromUtf8 在大多数情况下能正确处理（包括多字节中文）。
     */
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
        value = QString::fromUtf8(data_.mid(pos_, len));
        pos_ += len;
        return true;
    }

    /**
     * @brief 跳过指定类型的 payload（用于忽略不需要的字段）
     */
    bool skipPayload(NbtType type)
    {
        switch (type)
        {
            case NbtType::Byte:
                pos_ += 1;
                return pos_ <= data_.size();
            case NbtType::Short:
                pos_ += 2;
                return pos_ <= data_.size();
            case NbtType::Int:
                pos_ += 4;
                return pos_ <= data_.size();
            case NbtType::Long:
                pos_ += 8;
                return pos_ <= data_.size();
            case NbtType::Float:
                pos_ += 4;
                return pos_ <= data_.size();
            case NbtType::Double:
                pos_ += 8;
                return pos_ <= data_.size();
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
                pos_ += len;
                return true;
            }
            case NbtType::String:
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
                pos_ += len;
                return true;
            }
            case NbtType::List:
            {
                quint8 elemType = 0;
                if (!readByte(elemType))
                {
                    return false;
                }
                qint32 len = 0;
                if (!readInt(len) || len < 0)
                {
                    return false;
                }
                for (qint32 i = 0; i < len; ++i)
                {
                    if (!skipPayload(static_cast<NbtType>(elemType)))
                    {
                        return false;
                    }
                }
                return true;
            }
            case NbtType::Compound:
            {
                while (true)
                {
                    quint8 childType = 0;
                    if (!readByte(childType))
                    {
                        return false;
                    }
                    if (childType == static_cast<quint8>(NbtType::End))
                    {
                        break;
                    }
                    QString childName;
                    if (!readString(childName))
                    {
                        return false;
                    }
                    if (!skipPayload(static_cast<NbtType>(childType)))
                    {
                        return false;
                    }
                }
                return true;
            }
            case NbtType::IntArray:
            {
                qint32 len = 0;
                if (!readInt(len) || len < 0)
                {
                    return false;
                }
                if (pos_ + 4LL * len > data_.size())
                {
                    return false;
                }
                pos_ += 4LL * len;
                return true;
            }
            case NbtType::LongArray:
            {
                qint32 len = 0;
                if (!readInt(len) || len < 0)
                {
                    return false;
                }
                if (pos_ + 8LL * len > data_.size())
                {
                    return false;
                }
                pos_ += 8LL * len;
                return true;
            }
            case NbtType::End:
            default:
                return false;
        }
    }

    /**
     * @brief 解析指定类型的 payload（递归）
     *
     * 调用者需先读取 type 与 name，本函数仅解析 payload 并填充到 tag 中。
     */
    bool parsePayload(NbtType type, NbtTag &tag)
    {
        tag.type = type;
        switch (type)
        {
            case NbtType::Byte:
            {
                quint8 v = 0;
                if (!readByte(v))
                {
                    return false;
                }
                // TAG_Byte 是有符号的，需做符号扩展
                tag.intValue = static_cast<qint64>(static_cast<qint8>(v));
                return true;
            }
            case NbtType::Short:
            {
                quint16 v = 0;
                if (!readUShort(v))
                {
                    return false;
                }
                tag.intValue = static_cast<qint64>(static_cast<qint16>(v));
                return true;
            }
            case NbtType::Int:
            {
                qint32 v = 0;
                if (!readInt(v))
                {
                    return false;
                }
                tag.intValue = static_cast<qint64>(v);
                return true;
            }
            case NbtType::Long:
            {
                qint64 v = 0;
                if (!readLong(v))
                {
                    return false;
                }
                tag.intValue = v;
                return true;
            }
            case NbtType::String:
            {
                return readString(tag.stringValue);
            }
            case NbtType::Compound:
            {
                while (true)
                {
                    quint8 childType = 0;
                    if (!readByte(childType))
                    {
                        return false;
                    }
                    if (childType == static_cast<quint8>(NbtType::End))
                    {
                        break;
                    }
                    NbtTag child;
                    child.type = static_cast<NbtType>(childType);
                    if (!readString(child.name))
                    {
                        return false;
                    }
                    if (!parsePayload(static_cast<NbtType>(childType), child))
                    {
                        return false;
                    }
                    tag.children.append(std::move(child));
                }
                return true;
            }
            case NbtType::List:
            {
                quint8 elemType = 0;
                if (!readByte(elemType))
                {
                    return false;
                }
                qint32 len = 0;
                if (!readInt(len) || len < 0)
                {
                    return false;
                }
                tag.listElementType = static_cast<NbtType>(elemType);
                for (qint32 i = 0; i < len; ++i)
                {
                    NbtTag elem;
                    if (!parsePayload(static_cast<NbtType>(elemType), elem))
                    {
                        return false;
                    }
                    tag.children.append(std::move(elem));
                }
                return true;
            }
            // 本模块不关心 Float/Double/ByteArray/IntArray/LongArray 的具体值，
            // 但仍需跳过它们的 payload 以便解析后续兄弟标签。
            case NbtType::Float:
            case NbtType::Double:
            case NbtType::ByteArray:
            case NbtType::IntArray:
            case NbtType::LongArray:
                return skipPayload(type);
            case NbtType::End:
            default:
                return false;
        }
    }

    /**
     * @brief 解析整个 NBT 文档的根标签
     *
     * 顶层标签必须是 TAG_Compound，名称通常为空字符串。
     */
    bool parseRoot(NbtTag &root)
    {
        quint8 rootType = 0;
        if (!readByte(rootType))
        {
            return false;
        }
        if (rootType != static_cast<quint8>(NbtType::Compound))
        {
            return false;
        }
        if (!readString(root.name))
        {
            return false;
        }
        return parsePayload(NbtType::Compound, root);
    }

private:
    QByteArray data_;
    int pos_;
};

// ============================================================================
// 辅助查找：在 Compound 子标签中按名称查找
// ============================================================================
const NbtTag *findChild(const NbtTag &compound, const QString &name)
{
    if (compound.type != NbtType::Compound)
    {
        return nullptr;
    }
    for (const auto &child : compound.children)
    {
        if (child.name == name)
        {
            return &child;
        }
    }
    return nullptr;
}

// ============================================================================
// gzip 解压：使用 QFile 读取文件 + zlib inflate 自动识别 gzip 头
// ============================================================================
bool decompressGzip(const QString &path, QByteArray &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "[LevelDatReader] Cannot open file:" << path
                   << "error:" << file.errorString();
        return false;
    }
    QByteArray compressed = file.readAll();
    file.close();

    if (compressed.size() < 2)
    {
        qWarning() << "[LevelDatReader] File too small:" << path;
        return false;
    }

    // 验证 gzip 魔数（0x1f 0x8b）
    if (static_cast<quint8>(compressed.at(0)) != 0x1f ||
        static_cast<quint8>(compressed.at(1)) != 0x8b)
    {
        qWarning() << "[LevelDatReader] Not a gzip file:" << path;
        return false;
    }

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = reinterpret_cast<Bytef *>(compressed.data());
    strm.avail_in = static_cast<uInt>(compressed.size());

    // 16 + MAX_WBITS 告诉 inflate 自动处理 gzip 头部
    int ret = inflateInit2(&strm, 16 + MAX_WBITS);
    if (ret != Z_OK)
    {
        qWarning() << "[LevelDatReader] inflateInit2 failed:" << ret;
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
            qWarning() << "[LevelDatReader] inflate failed:" << ret
                       << "msg:" << strm.msg;
            return false;
        }
        out.append(buffer, BUFFER_SIZE - static_cast<int>(strm.avail_out));
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return true;
}

} // namespace

// ============================================================================
// LevelDatReader 公开接口实现
// ============================================================================

std::optional<LevelInfo> LevelDatReader::readLevelDat(const QString &path)
{
    if (!QFileInfo::exists(path))
    {
        qWarning() << "[LevelDatReader] File not found:" << path;
        return std::nullopt;
    }

    // 1. gzip 解压
    QByteArray decompressed;
    if (!decompressGzip(path, decompressed) || decompressed.isEmpty())
    {
        // decompressGzip 内部已输出具体失败原因
        return std::nullopt;
    }

    // 2. NBT 解析
    NbtReader reader(decompressed);
    NbtTag root;
    if (!reader.parseRoot(root))
    {
        qWarning() << "[LevelDatReader] NBT parse failed:" << path;
        return std::nullopt;
    }

    // 3. 定位 Data 字段
    const NbtTag *dataTag = findChild(root, QStringLiteral("Data"));
    if (dataTag == nullptr)
    {
        qWarning() << "[LevelDatReader] Missing 'Data' tag:" << path;
        return std::nullopt;
    }

    LevelInfo info;
    bool allowCommandsFound = false;
    bool gameTypeFound = false;
    bool versionNameFound = false;

    // 4. allowCommands（TAG_Byte）
    const NbtTag *allowCmdTag = findChild(*dataTag, QStringLiteral("allowCommands"));
    if (allowCmdTag != nullptr && allowCmdTag->type == NbtType::Byte)
    {
        info.allowCommands = (allowCmdTag->intValue != 0);
        allowCommandsFound = true;
    }

    // 5. GameType（TAG_Int）
    const NbtTag *gameTypeTag = findChild(*dataTag, QStringLiteral("GameType"));
    if (gameTypeTag != nullptr && gameTypeTag->type == NbtType::Int)
    {
        info.gameType = static_cast<int>(gameTypeTag->intValue);
        gameTypeFound = true;
    }

    // 6. Version.Name（TAG_String）
    const NbtTag *versionTag = findChild(*dataTag, QStringLiteral("Version"));
    if (versionTag != nullptr)
    {
        const NbtTag *nameTag = findChild(*versionTag, QStringLiteral("Name"));
        if (nameTag != nullptr && nameTag->type == NbtType::String)
        {
            info.versionName = nameTag->stringValue;
            versionNameFound = true;
        }
    }

    // 7. LevelName（TAG_String）—— 存档名称
    const NbtTag *levelNameTag = findChild(*dataTag, QStringLiteral("LevelName"));
    if (levelNameTag != nullptr && levelNameTag->type == NbtType::String)
    {
        info.levelName = levelNameTag->stringValue;
    }

    // 8. LastPlayed（TAG_Long）—— 最后游玩时间（毫秒时间戳）
    const NbtTag *lastPlayedTag = findChild(*dataTag, QStringLiteral("LastPlayed"));
    if (lastPlayedTag != nullptr && lastPlayedTag->type == NbtType::Long)
    {
        info.lastPlayed = lastPlayedTag->intValue;
    }

    if (!allowCommandsFound || !gameTypeFound || !versionNameFound)
    {
        qWarning() << "[LevelDatReader] Missing required fields:"
                   << "allowCommands=" << allowCommandsFound
                   << "GameType=" << gameTypeFound
                   << "Version.Name=" << versionNameFound
                   << "path=" << path;
        return std::nullopt;
    }

    return info;
}
