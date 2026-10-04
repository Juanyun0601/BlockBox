/**
 * @file   LitematicReader.cpp
 * @brief  Minecraft Litematica 投影文件读取器实现
 * @author BlockBox Team
 * @date   2026-07-17
 *
 * 实现细节：
 *   1. gzip 解压：使用 zlib 的 inflate（windowBits = 16 + MAX_WBITS）自动识别 gzip 头部。
 *   2. NBT 解析：基于游标的二进制读取器，递归构建 NBT 标签树，
 *      完整解析 Byte/Short/Int/Long/String/List/Compound，
 *      以及 ByteArray/IntArray/LongArray 的载荷值（用于 BlockStates LongArray）。
 *   3. 字段定位：
 *      - 根 Compound 下读取 Metadata / Regions / MinecraftDataVersion / Version
 *      - Metadata.Name / Author / Description / TimeCreated / TimeModified /
 *        TotalBlocks / TotalVolume / RegionCount / EnclosingSize
 *      - Regions.<name>.Position / Size / BlockStatePalette / BlockStates
 *   4. 材料清单计算：
 *      - bitsPerBlock = max(2, ceilLog2(paletteSize))
 *      - blocksPerLong = 64 / bitsPerBlock（整除，余数位未使用）
 *      - 每个 long 内的索引按 MSB → LSB 排列，不跨 long 边界
 *      - 遍历所有 long，提取每个 block 索引，调色板索引 0 通常为 minecraft:air（剔除）
 *
 * 测试方法：
 *   - 准备一个由 Litematica 模组生成的 .litematic 文件
 *   - 调用 LitematicReader::readFile("xxx.litematic")
 *   - 验证 name / enclosingSize / totalBlocks / materialList 与 Litematica 模组内显示一致
 */

#include "LitematicReader.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QtZlib/zlib.h>

#include <algorithm>

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
// NBT 标签节点（完整保留所有类型载荷，含 LongArray）
// ============================================================================
struct NbtTag
{
    NbtType type = NbtType::End;
    QString name;
    qint64 intValue = 0;                     ///< Byte/Short/Int/Long 统一存为 64 位有符号
    QString stringValue;                     ///< String 类型的值
    QList<NbtTag> children;                  ///< Compound 或 List 的子元素
    NbtType listElementType = NbtType::End;  ///< List 元素类型
    QList<qint64> longArrayValue;            ///< LongArray 的完整值（用于 BlockStates）
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
     * @brief 解析指定类型的 payload（递归），完整保留 LongArray 值
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
            case NbtType::LongArray:
            {
                qint32 len = 0;
                if (!readInt(len) || len < 0)
                {
                    return false;
                }
                tag.longArrayValue.reserve(len);
                for (qint32 i = 0; i < len; ++i)
                {
                    qint64 v = 0;
                    if (!readLong(v))
                    {
                        return false;
                    }
                    tag.longArrayValue.append(v);
                }
                return true;
            }
            // 本模块不关心 Float/Double/ByteArray/IntArray 的具体值，
            // 但仍需跳过 payload 以便解析后续兄弟标签。
            case NbtType::Float:
            case NbtType::Double:
            case NbtType::ByteArray:
            case NbtType::IntArray:
                return skipPayload(type);
            case NbtType::End:
            default:
                return false;
        }
    }

    /**
     * @brief 解析整个 NBT 文档的根标签
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

/**
 * @brief 读取 Compound 子标签的 Int 值（兼容 Byte/Short/Int/Long）
 */
bool readIntChild(const NbtTag &compound, const QString &name, int &out)
{
    const NbtTag *tag = findChild(compound, name);
    if (tag == nullptr)
    {
        return false;
    }
    if (tag->type == NbtType::Byte || tag->type == NbtType::Short ||
        tag->type == NbtType::Int || tag->type == NbtType::Long)
    {
        out = static_cast<int>(tag->intValue);
        return true;
    }
    return false;
}

/**
 * @brief 读取 Compound 子标签的 Long 值
 */
bool readLongChild(const NbtTag &compound, const QString &name, qint64 &out)
{
    const NbtTag *tag = findChild(compound, name);
    if (tag == nullptr)
    {
        return false;
    }
    if (tag->type == NbtType::Byte || tag->type == NbtType::Short ||
        tag->type == NbtType::Int || tag->type == NbtType::Long)
    {
        out = tag->intValue;
        return true;
    }
    return false;
}

/**
 * @brief 读取 Compound 子标签的 String 值
 */
bool readStringChild(const NbtTag &compound, const QString &name, QString &out)
{
    const NbtTag *tag = findChild(compound, name);
    if (tag == nullptr || tag->type != NbtType::String)
    {
        return false;
    }
    out = tag->stringValue;
    return true;
}

// ============================================================================
// gzip 解压：使用 QFile 读取文件 + zlib inflate 自动识别 gzip 头
// ============================================================================
bool decompressGzip(const QString &path, QByteArray &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "[LitematicReader] Cannot open file:" << path
                   << "error:" << file.errorString();
        return false;
    }
    QByteArray compressed = file.readAll();
    file.close();

    if (compressed.size() < 2)
    {
        qWarning() << "[LitematicReader] File too small:" << path;
        return false;
    }

    // 验证 gzip 魔数（0x1f 0x8b）
    if (static_cast<quint8>(compressed.at(0)) != 0x1f ||
        static_cast<quint8>(compressed.at(1)) != 0x8b)
    {
        qWarning() << "[LitematicReader] Not a gzip file:" << path;
        return false;
    }

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = reinterpret_cast<Bytef *>(compressed.data());
    strm.avail_in = static_cast<uInt>(compressed.size());

    int ret = inflateInit2(&strm, 16 + MAX_WBITS);
    if (ret != Z_OK)
    {
        qWarning() << "[LitematicReader] inflateInit2 failed:" << ret;
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
            qWarning() << "[LitematicReader] inflate failed:" << ret
                       << "msg:" << strm.msg;
            return false;
        }
        out.append(buffer, BUFFER_SIZE - static_cast<int>(strm.avail_out));
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return true;
}

// ============================================================================
// 数学辅助：向上取整的 log2
// ============================================================================
/**
 * @brief ceil(log2(x))，x >= 1
 *   ceilLog2(1) = 0
 *   ceilLog2(2) = 1
 *   ceilLog2(3) = 2
 *   ceilLog2(4) = 2
 *   ceilLog2(5) = 3
 */
int ceilLog2(int x)
{
    if (x <= 1)
    {
        return 0;
    }
    int bits = 0;
    int v = x - 1;
    while (v > 0)
    {
        ++bits;
        v >>= 1;
    }
    return bits;
}

/**
 * @brief 从 BlockStatePalette + BlockStates 计算材料清单
 * @param palette 调色板 Compound List（每项含 Name 字段）
 * @param blockStates BlockStates LongArray
 * @param totalEntries 该区域方块总数（Size.x * Size.y * Size.z 绝对值）
 * @param blockCountOut 输出：实际非空气方块数
 * @param countsOut 输出：方块 ID -> 数量（含空气）
 * @return 是否成功
 *
 * 位打包规则（Litematica 特有，与 Minecraft 区块存储一致）：
 *   - bitsPerBlock = max(2, ceilLog2(paletteSize))
 *   - 索引按全局位偏移连续排列，值可跨 long 边界（MSB → LSB 序）
 *   - 索引 i 的起始位 = i * bitsPerBlock；起始位所在 long 及结束位所在 long
 *     可能不同，此时高 64 - startBitOffset 位取前一个 long，其余取后一个 long
 */
bool computeMaterialList(const NbtTag &palette, const NbtTag &blockStates,
                        int totalEntries, int &blockCountOut,
                        QHash<QString, int> &countsOut)
{
    if (palette.type != NbtType::List || palette.listElementType != NbtType::Compound)
    {
        return false;
    }

    const int paletteSize = palette.children.size();
    if (paletteSize <= 0)
    {
        return false;
    }

    // 提取调色板：每个 Compound 的 Name 字段
    QStringList paletteNames;
    paletteNames.reserve(paletteSize);
    for (const auto &entry : palette.children)
    {
        QString blockName;
        if (readStringChild(entry, QStringLiteral("Name"), blockName))
        {
            paletteNames.append(blockName);
        }
        else
        {
            paletteNames.append(QStringLiteral("minecraft:unknown"));
        }
    }

    // 无 BlockStates（空区域）
    if (blockStates.type != NbtType::LongArray || blockStates.longArrayValue.isEmpty())
    {
        blockCountOut = 0;
        return true;
    }

    const int bitsPerBlock = std::max(2, ceilLog2(paletteSize));
    if (bitsPerBlock > 32)
    {
        return false;
    }
    const quint64 mask = (1ULL << bitsPerBlock) - 1ULL;

    const QList<qint64> &longs = blockStates.longArrayValue;
    const int longCount = longs.size();
    if (longCount <= 0)
    {
        blockCountOut = 0;
        return true;
    }

    // 预分配计数容器
    for (const auto &name : paletteNames)
    {
        countsOut.insert(name, 0);
    }

    int nonAirCount = 0;
    const QString airId = QStringLiteral("minecraft:air");

    // 跨界位打包：索引 i 的起始位 = i * bitsPerBlock，值可跨越两个 long
    for (int i = 0; i < totalEntries; ++i)
    {
        const qint64 bitOffset = static_cast<qint64>(i) * bitsPerBlock;
        const int startArrIndex = static_cast<int>(bitOffset >> 6);
        const int startBitOffset = static_cast<int>(bitOffset & 0x3F);

        if (startArrIndex >= longCount)
        {
            break;
        }

        quint64 value = 0;
        if (startBitOffset + bitsPerBlock <= 64)
        {
            // 完全落在单个 long 内
            value = (static_cast<quint64>(longs.at(startArrIndex)) >> startBitOffset) & mask;
        }
        else
        {
            // 跨 long：低 64 - startBitOffset 位来自当前 long，其余来自下一个 long
            const int endOffset = 64 - startBitOffset;
            const quint64 low = static_cast<quint64>(longs.at(startArrIndex)) >> startBitOffset;
            quint64 high = 0;
            if (startArrIndex + 1 < longCount)
            {
                high = static_cast<quint64>(longs.at(startArrIndex + 1)) << endOffset;
            }
            value = (low | high) & mask;
        }

        const int paletteId = static_cast<int>(value);
        if (paletteId < 0 || paletteId >= paletteSize)
        {
            // 越界索引（可能是填充位），跳过
            continue;
        }

        const QString &blockName = paletteNames.at(paletteId);
        countsOut[blockName] = countsOut.value(blockName) + 1;

        if (blockName != airId)
        {
            ++nonAirCount;
        }
    }

    blockCountOut = nonAirCount;
    return true;
}

} // namespace

// ============================================================================
// LitematicReader 公开接口实现
// ============================================================================

std::optional<LitematicInfo> LitematicReader::readFile(const QString &path)
{
    if (!QFileInfo::exists(path))
    {
        qWarning() << "[LitematicReader] File not found:" << path;
        return std::nullopt;
    }

    // 1. gzip 解压
    QByteArray decompressed;
    if (!decompressGzip(path, decompressed) || decompressed.isEmpty())
    {
        return std::nullopt;
    }

    // 2. NBT 解析
    NbtReader reader(decompressed);
    NbtTag root;
    if (!reader.parseRoot(root))
    {
        qWarning() << "[LitematicReader] NBT parse failed:" << path;
        return std::nullopt;
    }

    // 3. 定位 Metadata
    const NbtTag *metadata = findChild(root, QStringLiteral("Metadata"));
    if (metadata == nullptr)
    {
        qWarning() << "[LitematicReader] Missing 'Metadata' tag:" << path;
        return std::nullopt;
    }

    LitematicInfo info;

    // 4. 元信息字段
    readStringChild(*metadata, QStringLiteral("Name"), info.name);
    readStringChild(*metadata, QStringLiteral("Author"), info.author);
    readStringChild(*metadata, QStringLiteral("Description"), info.description);
    readLongChild(*metadata, QStringLiteral("TimeCreated"), info.timeCreated);
    readLongChild(*metadata, QStringLiteral("TimeModified"), info.timeModified);
    readIntChild(*metadata, QStringLiteral("TotalBlocks"), info.totalBlocks);
    readIntChild(*metadata, QStringLiteral("TotalVolume"), info.totalVolume);
    readIntChild(*metadata, QStringLiteral("RegionCount"), info.regionCount);

    // 5. 包围盒尺寸
    const NbtTag *enclosingSize = findChild(*metadata, QStringLiteral("EnclosingSize"));
    if (enclosingSize != nullptr)
    {
        readIntChild(*enclosingSize, QStringLiteral("x"), info.enclosingSizeX);
        readIntChild(*enclosingSize, QStringLiteral("y"), info.enclosingSizeY);
        readIntChild(*enclosingSize, QStringLiteral("z"), info.enclosingSizeZ);
    }

    // 6. 根级版本字段
    readIntChild(root, QStringLiteral("MinecraftDataVersion"), info.minecraftDataVersion);
    readIntChild(root, QStringLiteral("Version"), info.version);

    // 7. 解析各区域 + 聚合材料清单
    const NbtTag *regions = findChild(root, QStringLiteral("Regions"));
    QHash<QString, int> aggregatedCounts;
    int aggregatedNonAir = 0;

    if (regions != nullptr && regions->type == NbtType::Compound)
    {
        for (const auto &regionEntry : regions->children)
        {
            LitematicRegionInfo r;
            r.name = regionEntry.name;

            // Position
            const NbtTag *posTag = findChild(regionEntry, QStringLiteral("Position"));
            if (posTag != nullptr)
            {
                readIntChild(*posTag, QStringLiteral("x"), r.posX);
                readIntChild(*posTag, QStringLiteral("y"), r.posY);
                readIntChild(*posTag, QStringLiteral("z"), r.posZ);
            }

            // Size（可能为负）
            const NbtTag *sizeTag = findChild(regionEntry, QStringLiteral("Size"));
            if (sizeTag != nullptr)
            {
                readIntChild(*sizeTag, QStringLiteral("x"), r.sizeX);
                readIntChild(*sizeTag, QStringLiteral("y"), r.sizeY);
                readIntChild(*sizeTag, QStringLiteral("z"), r.sizeZ);
            }
            r.volume = std::abs(r.sizeX) * std::abs(r.sizeY) * std::abs(r.sizeZ);

            // BlockStatePalette
            const NbtTag *paletteTag = findChild(regionEntry, QStringLiteral("BlockStatePalette"));
            if (paletteTag != nullptr && paletteTag->type == NbtType::List)
            {
                r.paletteSize = paletteTag->children.size();
            }

            // BlockStates + 计数
            const NbtTag *blockStatesTag = findChild(regionEntry, QStringLiteral("BlockStates"));
            if (paletteTag != nullptr && blockStatesTag != nullptr)
            {
                int nonAir = 0;
                QHash<QString, int> regionCounts;
                if (computeMaterialList(*paletteTag, *blockStatesTag, r.volume, nonAir, regionCounts))
                {
                    r.blockCount = nonAir;
                    aggregatedNonAir += nonAir;
                    for (auto it = regionCounts.begin(); it != regionCounts.end(); ++it)
                    {
                        aggregatedCounts[it.key()] += it.value();
                    }
                }
            }

            info.regions.append(std::move(r));
        }
    }

    // 8. 构造材料清单（剔除空气，按数量降序）
    const QString airId = QStringLiteral("minecraft:air");
    for (auto it = aggregatedCounts.begin(); it != aggregatedCounts.end(); ++it)
    {
        if (it.key() == airId || it.value() == 0)
        {
            continue;
        }
        LitematicBlockCount item;
        item.blockId = it.key();
        item.count = it.value();
        info.materialList.append(std::move(item));
    }
    std::sort(info.materialList.begin(), info.materialList.end(),
              [](const LitematicBlockCount &a, const LitematicBlockCount &b) {
                  if (a.count != b.count)
                  {
                      return a.count > b.count;
                  }
                  return a.blockId < b.blockId;
              });

    // 若 Metadata.TotalBlocks 缺失或为 0，用聚合的非空气方块数补全
    if (info.totalBlocks <= 0 && aggregatedNonAir > 0)
    {
        info.totalBlocks = aggregatedNonAir;
    }

    return info;
}
