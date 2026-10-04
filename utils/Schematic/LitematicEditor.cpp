/**
 * @file   LitematicEditor.cpp
 * @brief  Litematica 投影文件编辑器与格式转换器实现
 * @author BlockBox Team
 * @date   2026-08-02
 */

#include "LitematicEditor.h"

#include "NbtCodec.h"

#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QtZlib/zlib.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

using NbtType = ::NbtType;
using NbtTag = ::NbtTag;

// ============================================================================
// 中间模型：一个独立的"方块状态" = 方块 ID + 属性
// ============================================================================
struct BlockState
{
    QString name;                          ///< 方块 ID，如 "minecraft:stone"
    QMap<QString, QString> properties;     ///< 属性，如 "type" -> "top"

    QString toString() const
    {
        if (properties.isEmpty())
        {
            return name;
        }
        QStringList parts;
        for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
        {
            parts.append(it.key() + QStringLiteral("=") + it.value());
        }
        return name + QStringLiteral("[") + parts.join(QStringLiteral(",")) + QStringLiteral("]");
    }

    bool operator==(const BlockState &other) const
    {
        return name == other.name && properties == other.properties;
    }

    bool operator<(const BlockState &other) const
    {
        return toString() < other.toString();
    }
};

// ============================================================================
// 中间模型：投影（含区域与逐格方块索引）
// ============================================================================
struct SchematicData
{
    QString name;
    QString author;
    QString description;
    qint64 timeCreated = 0;
    qint64 timeModified = 0;
    int minecraftDataVersion = 0;
    int version = 6;
    bool hasMultipleRegions = false;

    // 扁平化后的单一区域：x/y/z 最小角（含）、尺寸（绝对正值）
    int originX = 0;
    int originY = 0;
    int originZ = 0;
    int sizeX = 0;
    int sizeY = 0;
    int sizeZ = 0;

    QList<BlockState> palette;   ///< 调色板：全局索引 -> 方块状态
    QList<int> blockIndices;     ///< 体积序（y*layerSize + z*sizeX + x）方块索引

    int volume() const
    {
        return sizeX * sizeY * sizeZ;
    }

    int paletteIndex(const BlockState &state)
    {
        for (int i = 0; i < palette.size(); ++i)
        {
            if (palette.at(i) == state)
            {
                return i;
            }
        }
        palette.append(state);
        return palette.size() - 1;
    }
};

// ============================================================================
// 数学辅助
// ============================================================================
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

// ============================================================================
// BlockStates 跨界位打包解码/编码
// 索引 i 的起始位 = i * bitsPerBlock，值可跨越两个 long（MSB → LSB 序）
// ============================================================================
int computeBits(int paletteSize)
{
    return std::max(2, ceilLog2(paletteSize));
}

int getBlockIndexAt(const QList<qint64> &longs, qint64 index, int bitsPerBlock)
{
    const qint64 bitOffset = index * bitsPerBlock;
    const int startArrIndex = static_cast<int>(bitOffset >> 6);
    const int startBitOffset = static_cast<int>(bitOffset & 0x3F);
    const quint64 mask = (1ULL << bitsPerBlock) - 1ULL;

    if (startArrIndex >= longs.size())
    {
        return 0;
    }

    if (startBitOffset + bitsPerBlock <= 64)
    {
        return static_cast<int>((static_cast<quint64>(longs.at(startArrIndex)) >> startBitOffset) & mask);
    }

    const int endOffset = 64 - startBitOffset;
    const quint64 low = static_cast<quint64>(longs.at(startArrIndex)) >> startBitOffset;
    quint64 high = 0;
    if (startArrIndex + 1 < longs.size())
    {
        high = static_cast<quint64>(longs.at(startArrIndex + 1)) << endOffset;
    }
    return static_cast<int>((low | high) & mask);
}

QList<qint64> encodeBlockStates(const QList<int> &indices, int bitsPerBlock)
{
    const int totalBits = static_cast<int>(indices.size()) * bitsPerBlock;
    const int longCount = (totalBits + 63) / 64;
    QList<qint64> longs(longCount, 0);
    const quint64 mask = (1ULL << bitsPerBlock) - 1ULL;

    for (int i = 0; i < indices.size(); ++i)
    {
        const qint64 bitOffset = static_cast<qint64>(i) * bitsPerBlock;
        const int startArrIndex = static_cast<int>(bitOffset >> 6);
        const int startBitOffset = static_cast<int>(bitOffset & 0x3F);
        const quint64 value = static_cast<quint64>(indices.at(i)) & mask;

        if (startArrIndex >= longs.size())
        {
            break;
        }

        if (startBitOffset + bitsPerBlock <= 64)
        {
            longs[startArrIndex] = static_cast<qint64>(
                static_cast<quint64>(longs.at(startArrIndex)) | (value << startBitOffset));
        }
        else
        {
            const int endOffset = 64 - startBitOffset;
            longs[startArrIndex] = static_cast<qint64>(
                static_cast<quint64>(longs.at(startArrIndex)) |
                ((value & ((1ULL << endOffset) - 1ULL)) << startBitOffset));
            if (startArrIndex + 1 < longs.size())
            {
                longs[startArrIndex + 1] = static_cast<qint64>(
                    static_cast<quint64>(longs.at(startArrIndex + 1)) | (value >> endOffset));
            }
        }
    }

    return longs;
}

// ============================================================================
// Minecraft varint（Sponge BlockData 使用，7 bit/字节，最多 5 字节）
// ============================================================================
bool readVarInt(const QByteArray &data, int &pos, int &value)
{
    int result = 0;
    int shift = 0;
    while (true)
    {
        if (pos >= data.size() || shift > 31)
        {
            return false;
        }
        const quint8 b = static_cast<quint8>(data.at(pos));
        ++pos;
        result |= static_cast<int>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
        {
            break;
        }
        shift += 7;
    }
    value = result;
    return true;
}

void writeVarInt(QByteArray &out, int value)
{
    while (true)
    {
        if ((value & ~0x7F) == 0)
        {
            out.append(static_cast<char>(value));
            return;
        }
        out.append(static_cast<char>((value & 0x7F) | 0x80));
        value = static_cast<quint32>(value) >> 7;
    }
}

// ============================================================================
// 方块状态字符串 <-> BlockState
// ============================================================================
BlockState stateFromString(const QString &str)
{
    BlockState state;
    const int index = str.indexOf(QStringLiteral("["));
    if (index < 0)
    {
        state.name = str;
        return state;
    }
    state.name = str.left(index);
    const QString propStr = str.mid(index + 1);
    if (propStr.endsWith(QStringLiteral("]")))
    {
        const QStringList pairs = propStr.left(propStr.size() - 1).split(QStringLiteral(","));
        for (const QString &pair : pairs)
        {
            const int eq = pair.indexOf(QStringLiteral("="));
            if (eq > 0)
            {
                state.properties.insert(pair.left(eq).trimmed(), pair.mid(eq + 1).trimmed());
            }
        }
    }
    return state;
}

// ============================================================================
// litematic 调色板项 <-> BlockState
// ============================================================================
std::optional<BlockState> stateFromLitematicPalette(const NbtTag &entry)
{
    if (entry.type != NbtType::Compound)
    {
        return std::nullopt;
    }
    const QString name = NbtCodec::readStringValue(entry, QStringLiteral("Name"));
    if (name.isEmpty())
    {
        return std::nullopt;
    }
    BlockState state;
    state.name = name;

    const NbtTag *props = NbtCodec::findChild(entry, QStringLiteral("Properties"));
    if (props != nullptr && props->type == NbtType::Compound)
    {
        for (const NbtTag &child : props->children)
        {
            if (child.type == NbtType::String)
            {
                state.properties.insert(child.name, child.stringValue);
            }
        }
    }
    return state;
}

NbtTag litematicPaletteEntryFromState(const BlockState &state)
{
    NbtTag entry = NbtCodec::makeCompound();
    entry.children.append(NbtCodec::makeString(QStringLiteral("Name"), state.name));
    if (!state.properties.isEmpty())
    {
        NbtTag props = NbtCodec::makeCompound(QStringLiteral("Properties"));
        for (auto it = state.properties.constBegin(); it != state.properties.constEnd(); ++it)
        {
            props.children.append(NbtCodec::makeString(it.key(), it.value()));
        }
        entry.children.append(std::move(props));
    }
    return entry;
}

// ============================================================================
// 区域坐标辅助：Size 可能为负，计算最小角与跨度
// ============================================================================
void regionBounds(int posX, int posY, int posZ, int sizeX, int sizeY, int sizeZ,
                  int &minX, int &minY, int &minZ, int &spanX, int &spanY, int &spanZ)
{
    minX = std::min(posX, posX + sizeX);
    minY = std::min(posY, posY + sizeY);
    minZ = std::min(posZ, posZ + sizeZ);
    spanX = std::abs(sizeX);
    spanY = std::abs(sizeY);
    spanZ = std::abs(sizeZ);
}

// ============================================================================
// 读取 .litematic 到中间模型
// ============================================================================
bool readLitematic(const QString &path, SchematicData &data, QString *errorOut)
{
    NbtTag root;
    if (!NbtCodec::readCompressedFile(path, root))
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("无法读取或解析 .litematic 文件（gzip/NBT 损坏）");
        }
        return false;
    }

    data.minecraftDataVersion = static_cast<int>(
        NbtCodec::readIntValue(root, QStringLiteral("MinecraftDataVersion"), 0));
    data.version = static_cast<int>(NbtCodec::readIntValue(root, QStringLiteral("Version"), 6));

    const NbtTag *metadata = NbtCodec::findChild(root, QStringLiteral("Metadata"));
    if (metadata != nullptr && metadata->type == NbtType::Compound)
    {
        data.name = NbtCodec::readStringValue(*metadata, QStringLiteral("Name"));
        data.author = NbtCodec::readStringValue(*metadata, QStringLiteral("Author"));
        data.description = NbtCodec::readStringValue(*metadata, QStringLiteral("Description"));
        data.timeCreated = NbtCodec::readIntValue(*metadata, QStringLiteral("TimeCreated"), 0);
        data.timeModified = NbtCodec::readIntValue(*metadata, QStringLiteral("TimeModified"), 0);
    }

    const NbtTag *regions = NbtCodec::findChild(root, QStringLiteral("Regions"));
    if (regions == nullptr || regions->type != NbtType::Compound)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral(".litematic 缺少 Regions 复合标签");
        }
        return false;
    }

    // 收集所有区域，计算总体包围盒
    struct RegionRaw
    {
        int minX, minY, minZ;
        int spanX, spanY, spanZ;
        QList<int> indices;          ///< 体积序方块索引（相对于该区域最小角）
        QList<BlockState> palette;
    };
    QList<RegionRaw> regionList;

    bool globalInit = false;
    int gMinX = 0, gMinY = 0, gMinZ = 0;
    int gMaxX = 0, gMaxY = 0, gMaxZ = 0;

    for (const NbtTag &regionEntry : regions->children)
    {
        if (regionEntry.type != NbtType::Compound)
        {
            continue;
        }

        const NbtTag *posTag = NbtCodec::findChild(regionEntry, QStringLiteral("Position"));
        const NbtTag *sizeTag = NbtCodec::findChild(regionEntry, QStringLiteral("Size"));
        int posX = 0, posY = 0, posZ = 0;
        int sizeX = 0, sizeY = 0, sizeZ = 0;
        if (posTag != nullptr && posTag->type == NbtType::Compound)
        {
            posX = static_cast<int>(NbtCodec::readIntValue(*posTag, QStringLiteral("x"), 0));
            posY = static_cast<int>(NbtCodec::readIntValue(*posTag, QStringLiteral("y"), 0));
            posZ = static_cast<int>(NbtCodec::readIntValue(*posTag, QStringLiteral("z"), 0));
        }
        if (sizeTag != nullptr && sizeTag->type == NbtType::Compound)
        {
            sizeX = static_cast<int>(NbtCodec::readIntValue(*sizeTag, QStringLiteral("x"), 0));
            sizeY = static_cast<int>(NbtCodec::readIntValue(*sizeTag, QStringLiteral("y"), 0));
            sizeZ = static_cast<int>(NbtCodec::readIntValue(*sizeTag, QStringLiteral("z"), 0));
        }

        int minX, minY, minZ, spanX, spanY, spanZ;
        regionBounds(posX, posY, posZ, sizeX, sizeY, sizeZ,
                     minX, minY, minZ, spanX, spanY, spanZ);
        if (spanX <= 0 || spanY <= 0 || spanZ <= 0)
        {
            continue;
        }

        // 调色板
        RegionRaw raw;
        raw.minX = minX;
        raw.minY = minY;
        raw.minZ = minZ;
        raw.spanX = spanX;
        raw.spanY = spanY;
        raw.spanZ = spanZ;

        const NbtTag *paletteTag = NbtCodec::findChild(regionEntry, QStringLiteral("BlockStatePalette"));
        if (paletteTag != nullptr && paletteTag->type == NbtType::List)
        {
            for (const NbtTag &entry : paletteTag->children)
            {
                auto state = stateFromLitematicPalette(entry);
                raw.palette.append(state.value_or(BlockState{ QStringLiteral("minecraft:air"), {} }));
            }
        }
        if (raw.palette.isEmpty())
        {
            raw.palette.append(BlockState{ QStringLiteral("minecraft:air"), {} });
        }

        const int bits = computeBits(raw.palette.size());
        const int volume = spanX * spanY * spanZ;
        raw.indices.reserve(volume);

        const NbtTag *blockStates = NbtCodec::findChild(regionEntry, QStringLiteral("BlockStates"));
        QList<qint64> longs;
        if (blockStates != nullptr && blockStates->type == NbtType::LongArray)
        {
            longs = blockStates->longArray;
        }
        for (int i = 0; i < volume; ++i)
        {
            int idx = 0;
            if (!longs.isEmpty())
            {
                idx = getBlockIndexAt(longs, i, bits);
                if (idx < 0 || idx >= raw.palette.size())
                {
                    idx = 0;
                }
            }
            raw.indices.append(idx);
        }

        regionList.append(std::move(raw));

        // 更新全局包围盒
        const int maxX = minX + spanX - 1;
        const int maxY = minY + spanY - 1;
        const int maxZ = minZ + spanZ - 1;
        if (!globalInit)
        {
            gMinX = minX; gMinY = minY; gMinZ = minZ;
            gMaxX = maxX; gMaxY = maxY; gMaxZ = maxZ;
            globalInit = true;
        }
        else
        {
            gMinX = std::min(gMinX, minX);
            gMinY = std::min(gMinY, minY);
            gMinZ = std::min(gMinZ, minZ);
            gMaxX = std::max(gMaxX, maxX);
            gMaxY = std::max(gMaxY, maxY);
            gMaxZ = std::max(gMaxZ, maxZ);
        }
    }

    if (regionList.isEmpty())
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral(".litematic 中没有有效的区域数据");
        }
        return false;
    }

    data.hasMultipleRegions = regionList.size() > 1;

    // 扁平化为单一区域
    data.originX = gMinX;
    data.originY = gMinY;
    data.originZ = gMinZ;
    data.sizeX = gMaxX - gMinX + 1;
    data.sizeY = gMaxY - gMinY + 1;
    data.sizeZ = gMaxZ - gMinZ + 1;

    const int layerSize = data.sizeX * data.sizeZ;
    const int totalVolume = data.volume();
    data.blockIndices.fill(0, totalVolume);
    data.palette.clear();
    data.palette.append(BlockState{ QStringLiteral("minecraft:air"), {} });
    // 预建调色板索引映射：air=0

    auto remap = [&data, layerSize](int gx, int gy, int gz) {
        // 全局坐标 -> 扁平数组索引
        const int dx = gx - data.originX;
        const int dy = gy - data.originY;
        const int dz = gz - data.originZ;
        return dy * layerSize + dz * data.sizeX + dx;
    };

    for (const RegionRaw &raw : regionList)
    {
        const int rLayer = raw.spanX * raw.spanZ;
        for (int y = 0; y < raw.spanY; ++y)
        {
            for (int z = 0; z < raw.spanZ; ++z)
            {
                for (int x = 0; x < raw.spanX; ++x)
                {
                    const int localIdx = y * rLayer + z * raw.spanX + x;
                    const int paletteIdx = raw.indices.at(localIdx);
                    const BlockState state = raw.palette.at(paletteIdx);
                    const int globalIdx = remap(raw.minX + x, raw.minY + y, raw.minZ + z);
                    data.blockIndices[globalIdx] = data.paletteIndex(state);
                }
            }
        }
    }

    return true;
}

// ============================================================================
// 写入 .litematic（Version 6）
// ============================================================================
bool writeLitematic(const QString &path, const SchematicData &data, QString *errorOut)
{
    NbtTag root = NbtCodec::makeCompound();

    root.children.append(NbtCodec::makeInt(QStringLiteral("MinecraftDataVersion"),
                                           data.minecraftDataVersion > 0 ? data.minecraftDataVersion : 3955));
    root.children.append(NbtCodec::makeInt(QStringLiteral("Version"), 6));
    root.children.append(NbtCodec::makeInt(QStringLiteral("SubVersion"), 4));

    NbtTag metadata = NbtCodec::makeCompound(QStringLiteral("Metadata"));
    metadata.children.append(NbtCodec::makeString(QStringLiteral("Name"), data.name));
    metadata.children.append(NbtCodec::makeString(QStringLiteral("Author"), data.author));
    metadata.children.append(NbtCodec::makeString(QStringLiteral("Description"), data.description));
    metadata.children.append(NbtCodec::makeLong(QStringLiteral("TimeCreated"), data.timeCreated));
    metadata.children.append(NbtCodec::makeLong(QStringLiteral("TimeModified"),
                                                data.timeModified > 0 ? data.timeModified
                                                                       : QDateTime::currentMSecsSinceEpoch()));

    NbtTag enclosing = NbtCodec::makeCompound(QStringLiteral("EnclosingSize"));
    enclosing.children.append(NbtCodec::makeInt(QStringLiteral("x"), data.sizeX));
    enclosing.children.append(NbtCodec::makeInt(QStringLiteral("y"), data.sizeY));
    enclosing.children.append(NbtCodec::makeInt(QStringLiteral("z"), data.sizeZ));
    metadata.children.append(std::move(enclosing));

    int totalBlocks = 0;
    for (int idx : data.blockIndices)
    {
        if (data.palette.at(idx).name != QStringLiteral("minecraft:air"))
        {
            ++totalBlocks;
        }
    }
    metadata.children.append(NbtCodec::makeInt(QStringLiteral("TotalBlocks"), totalBlocks));
    metadata.children.append(NbtCodec::makeInt(QStringLiteral("TotalVolume"), data.volume()));
    metadata.children.append(NbtCodec::makeInt(QStringLiteral("RegionCount"), 1));
    root.children.append(std::move(metadata));

    // 单一区域
    NbtTag regions = NbtCodec::makeCompound(QStringLiteral("Regions"));
    NbtTag region = NbtCodec::makeCompound(QStringLiteral("Region"));

    NbtTag posTag = NbtCodec::makeCompound(QStringLiteral("Position"));
    posTag.children.append(NbtCodec::makeInt(QStringLiteral("x"), 0));
    posTag.children.append(NbtCodec::makeInt(QStringLiteral("y"), 0));
    posTag.children.append(NbtCodec::makeInt(QStringLiteral("z"), 0));
    region.children.append(std::move(posTag));

    NbtTag sizeTag = NbtCodec::makeCompound(QStringLiteral("Size"));
    sizeTag.children.append(NbtCodec::makeInt(QStringLiteral("x"), data.sizeX));
    sizeTag.children.append(NbtCodec::makeInt(QStringLiteral("y"), data.sizeY));
    sizeTag.children.append(NbtCodec::makeInt(QStringLiteral("z"), data.sizeZ));
    region.children.append(std::move(sizeTag));

    NbtTag palette = NbtCodec::makeList(QStringLiteral("BlockStatePalette"), NbtType::Compound, {});
    for (const BlockState &state : data.palette)
    {
        palette.children.append(litematicPaletteEntryFromState(state));
    }
    region.children.append(std::move(palette));

    const int bits = computeBits(data.palette.size());
    const QList<qint64> blockStates = encodeBlockStates(data.blockIndices, bits);
    region.children.append(NbtCodec::makeLongArray(QStringLiteral("BlockStates"), blockStates));

    region.children.append(NbtCodec::makeList(QStringLiteral("TileEntities"), NbtType::Compound, {}));
    region.children.append(NbtCodec::makeList(QStringLiteral("Entities"), NbtType::Compound, {}));

    regions.children.append(std::move(region));
    root.children.append(std::move(regions));

    if (!NbtCodec::writeCompressedFile(path, root))
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("写入 .litematic 文件失败");
        }
        return false;
    }
    return true;
}

// ============================================================================
// 写入 .schematic（Schematica 格式）
// Blocks 低 8 位 + AddBlocks 高 4 位（两方块一字节）+ Data 元数据 + SchematicaMapping
// ============================================================================
bool writeSchematica(const QString &path, const SchematicData &data, QString *errorOut)
{
    const int numBlocks = data.volume();
    if (numBlocks <= 0)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("投影为空，无法转换");
        }
        return false;
    }
    if (data.palette.size() > 4096)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("方块状态种类超过 4096，无法写入 Schematica 格式");
        }
        return false;
    }

    NbtTag root = NbtCodec::makeCompound();
    root.children.append(NbtCodec::makeShort(QStringLiteral("Width"), static_cast<qint16>(data.sizeX)));
    root.children.append(NbtCodec::makeShort(QStringLiteral("Height"), static_cast<qint16>(data.sizeY)));
    root.children.append(NbtCodec::makeShort(QStringLiteral("Length"), static_cast<qint16>(data.sizeZ)));
    root.children.append(NbtCodec::makeString(QStringLiteral("Materials"), QStringLiteral("Alpha")));

    QByteArray blocksArr(numBlocks, static_cast<char>(0));
    QByteArray dataArr(numBlocks, static_cast<char>(0));
    const int addSize = (numBlocks + 1) / 2;
    QByteArray addArr(addSize, static_cast<char>(0));
    int numAdd = 0;

    NbtTag mapping = NbtCodec::makeCompound(QStringLiteral("SchematicaMapping"));
    // 保存 调色板索引 -> 方块状态字符串
    for (int i = 0; i < data.palette.size(); ++i)
    {
        mapping.children.append(
            NbtCodec::makeString(data.palette.at(i).toString(), QString::number(i)));
    }

    for (int bi = 0; bi < numBlocks; ++bi)
    {
        const int paletteIdx = data.blockIndices.at(bi);
        const int id = paletteIdx;   // 我们直接把调色板索引用作 Schematica id（0-4095）
        blocksArr[bi] = static_cast<char>(id & 0xFF);

        if (bi % 2 == 0)
        {
            const int high = (id >> 8) & 0x0F;
            addArr[bi / 2] = static_cast<char>((addArr.at(bi / 2) & 0x0F) | (high << 4));
            if (high != 0)
            {
                ++numAdd;
            }
        }
        else
        {
            const int high = (id >> 8) & 0x0F;
            addArr[bi / 2] = static_cast<char>((addArr.at(bi / 2) & 0xF0) | high);
            if (high != 0)
            {
                ++numAdd;
            }
        }
        dataArr[bi] = static_cast<char>(0);
    }

    root.children.append(NbtCodec::makeByteArray(QStringLiteral("Blocks"), blocksArr));
    root.children.append(NbtCodec::makeByteArray(QStringLiteral("Data"), dataArr));
    if (numAdd > 0)
    {
        root.children.append(NbtCodec::makeByteArray(QStringLiteral("AddBlocks"), addArr));
    }
    root.children.append(std::move(mapping));

    root.children.append(NbtCodec::makeList(QStringLiteral("TileEntities"), NbtType::Compound, {}));
    root.children.append(NbtCodec::makeList(QStringLiteral("Entities"), NbtType::Compound, {}));

    if (!NbtCodec::writeCompressedFile(path, root))
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("写入 .schematic 文件失败");
        }
        return false;
    }
    return true;
}

// ============================================================================
// 读取 .schematic（Schematica 格式）
// ============================================================================
bool readSchematica(const QString &path, SchematicData &data, QString *errorOut)
{
    NbtTag root;
    if (!NbtCodec::readCompressedFile(path, root))
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("无法读取或解析 .schematic 文件（gzip/NBT 损坏）");
        }
        return false;
    }

    const int sizeX = static_cast<int>(NbtCodec::readIntValue(root, QStringLiteral("Width"), 0));
    const int sizeY = static_cast<int>(NbtCodec::readIntValue(root, QStringLiteral("Height"), 0));
    const int sizeZ = static_cast<int>(NbtCodec::readIntValue(root, QStringLiteral("Length"), 0));
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral(".schematic 尺寸无效");
        }
        return false;
    }

    const NbtTag *blocksTag = NbtCodec::findChild(root, QStringLiteral("Blocks"));
    const NbtTag *dataTag = NbtCodec::findChild(root, QStringLiteral("Data"));
    if (blocksTag == nullptr || blocksTag->type != NbtType::ByteArray ||
        dataTag == nullptr || dataTag->type != NbtType::ByteArray)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral(".schematic 缺少 Blocks/Data 数组");
        }
        return false;
    }

    const QByteArray blocks = blocksTag->byteArray;
    const QByteArray meta = dataTag->byteArray;
    const int numBlocks = sizeX * sizeY * sizeZ;
    if (blocks.size() != numBlocks || meta.size() != numBlocks)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral(".schematic 方块数组长度与尺寸不匹配");
        }
        return false;
    }

    // AddBlocks
    const NbtTag *addTag = NbtCodec::findChild(root, QStringLiteral("AddBlocks"));
    QByteArray add;
    if (addTag != nullptr && addTag->type == NbtType::ByteArray)
    {
        add = addTag->byteArray;
    }

    // SchematicaMapping：id -> 方块状态字符串
    QMap<int, BlockState> mapping;
    const NbtTag *mappingTag = NbtCodec::findChild(root, QStringLiteral("SchematicaMapping"));
    if (mappingTag != nullptr && mappingTag->type == NbtType::Compound)
    {
        for (const NbtTag &child : mappingTag->children)
        {
            if (child.type != NbtType::String)
            {
                continue;
            }
            bool ok = false;
            const int id = child.stringValue.toInt(&ok);
            if (ok)
            {
                mapping.insert(id, stateFromString(child.name));
            }
        }
    }

    data.sizeX = sizeX;
    data.sizeY = sizeY;
    data.sizeZ = sizeZ;
    data.originX = 0;
    data.originY = 0;
    data.originZ = 0;
    data.hasMultipleRegions = false;
    data.palette.clear();
    data.palette.append(BlockState{ QStringLiteral("minecraft:air"), {} });
    data.blockIndices.fill(0, numBlocks);

    for (int bi = 0; bi < numBlocks; ++bi)
    {
        const int low = static_cast<quint8>(blocks.at(bi));
        int high = 0;
        const int addValue = (!add.isEmpty()) ? static_cast<quint8>(add.at(bi / 2)) : 0;
        if (bi % 2 == 0)
        {
            high = (addValue >> 4) & 0x0F;
        }
        else
        {
            high = addValue & 0x0F;
        }
        const int id = (high << 8) | low;

        BlockState state;
        const auto it = mapping.constFind(id);
        if (it != mapping.constEnd())
        {
            state = it.value();
        }
        else
        {
            // 无映射：尝试用 vanilla 数字 id 近似（直接存 minecraft:air 兜底）
            state = BlockState{ QStringLiteral("minecraft:air"), {} };
        }
        data.blockIndices[bi] = data.paletteIndex(state);
    }

    return true;
}

// ============================================================================
// 写入 .schem（Sponge v2）
// Palette：方块状态字符串 -> id；BlockData：varint 位打包
// ============================================================================
bool writeSponge(const QString &path, const SchematicData &data, QString *errorOut)
{
    const int numBlocks = data.volume();
    if (numBlocks <= 0)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("投影为空，无法转换");
        }
        return false;
    }

    NbtTag root = NbtCodec::makeCompound();
    root.children.append(NbtCodec::makeInt(QStringLiteral("Version"), 2));
    root.children.append(NbtCodec::makeInt(QStringLiteral("DataVersion"),
                                           data.minecraftDataVersion > 0 ? data.minecraftDataVersion : 3955));
    root.children.append(NbtCodec::makeInt(QStringLiteral("Width"), data.sizeX));
    root.children.append(NbtCodec::makeInt(QStringLiteral("Height"), data.sizeY));
    root.children.append(NbtCodec::makeInt(QStringLiteral("Length"), data.sizeZ));

    NbtTag palette = NbtCodec::makeCompound(QStringLiteral("Palette"));
    for (int i = 0; i < data.palette.size(); ++i)
    {
        palette.children.append(NbtCodec::makeInt(data.palette.at(i).toString(), i));
    }
    root.children.append(std::move(palette));

    QByteArray blockData;
    for (int idx : data.blockIndices)
    {
        writeVarInt(blockData, idx);
    }
    root.children.append(NbtCodec::makeByteArray(QStringLiteral("BlockData"), blockData));

    root.children.append(NbtCodec::makeIntArray(QStringLiteral("Offset"),
                                                QList<qint32>{ 0, 0, 0 }));

    NbtTag tileEntities = NbtCodec::makeList(QStringLiteral("TileEntities"), NbtType::Compound, {});
    const NbtTag *source = nullptr;
    Q_UNUSED(source);
    root.children.append(std::move(tileEntities));
    root.children.append(NbtCodec::makeList(QStringLiteral("Entities"), NbtType::Compound, {}));

    if (!NbtCodec::writeCompressedFile(path, root))
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("写入 .schem 文件失败");
        }
        return false;
    }
    return true;
}

// ============================================================================
// 读取 .schem（Sponge v2 / v3）
// ============================================================================
bool readSponge(const QString &path, SchematicData &data, QString *errorOut)
{
    NbtTag root;
    if (!NbtCodec::readCompressedFile(path, root))
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("无法读取或解析 .schem 文件（gzip/NBT 损坏）");
        }
        return false;
    }

    NbtTag *container = &root;
    NbtTag localContainer;

    // v3：所有内容放在 Schematic 复合中
    NbtTag *schematicTag = NbtCodec::findChild(root, QStringLiteral("Schematic"));
    if (schematicTag != nullptr && schematicTag->type == NbtType::Compound)
    {
        container = schematicTag;
    }

    const int spongeVersion = static_cast<int>(
        NbtCodec::readIntValue(*container, QStringLiteral("Version"), 2));
    const int sizeX = static_cast<int>(NbtCodec::readIntValue(*container, QStringLiteral("Width"), 0));
    const int sizeY = static_cast<int>(NbtCodec::readIntValue(*container, QStringLiteral("Height"), 0));
    const int sizeZ = static_cast<int>(NbtCodec::readIntValue(*container, QStringLiteral("Length"), 0));
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral(".schem 尺寸无效");
        }
        return false;
    }
    data.minecraftDataVersion = static_cast<int>(
        NbtCodec::readIntValue(*container, QStringLiteral("DataVersion"), 0));

    // v3 时 BlockData/Palette 在 Blocks 复合中
    NbtTag *paletteTag = nullptr;
    NbtTag *blockDataTag = nullptr;
    if (spongeVersion >= 3)
    {
        NbtTag *blocks = NbtCodec::findChild(*container, QStringLiteral("Blocks"));
        if (blocks != nullptr && blocks->type == NbtType::Compound)
        {
            paletteTag = NbtCodec::findChild(*blocks, QStringLiteral("Palette"));
            blockDataTag = NbtCodec::findChild(*blocks, QStringLiteral("Data"));
        }
    }
    else
    {
        paletteTag = NbtCodec::findChild(*container, QStringLiteral("Palette"));
        blockDataTag = NbtCodec::findChild(*container, QStringLiteral("BlockData"));
    }

    if (paletteTag == nullptr || paletteTag->type != NbtType::Compound ||
        blockDataTag == nullptr || blockDataTag->type != NbtType::ByteArray)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral(".schem 缺少 Palette/BlockData 数据");
        }
        return false;
    }

    // 调色板：字符串 -> id
    QMap<int, BlockState> palette;
    int maxId = -1;
    for (const NbtTag &child : paletteTag->children)
    {
        if (child.type != NbtType::Int)
        {
            continue;
        }
        const int id = static_cast<int>(child.intValue);
        palette.insert(id, stateFromString(child.name));
        maxId = std::max(maxId, id);
    }

    data.sizeX = sizeX;
    data.sizeY = sizeY;
    data.sizeZ = sizeZ;
    data.originX = 0;
    data.originY = 0;
    data.originZ = 0;
    data.hasMultipleRegions = false;
    data.palette.clear();
    data.palette.append(BlockState{ QStringLiteral("minecraft:air"), {} });

    const QByteArray blockData = blockDataTag->byteArray;
    const int numBlocks = sizeX * sizeY * sizeZ;
    data.blockIndices.fill(0, numBlocks);

    int pos = 0;
    for (int i = 0; i < numBlocks && pos < blockData.size(); ++i)
    {
        int id = 0;
        if (!readVarInt(blockData, pos, id))
        {
            break;
        }
        BlockState state;
        const auto it = palette.constFind(id);
        state = (it != palette.constEnd()) ? it.value()
                                           : BlockState{ QStringLiteral("minecraft:air"), {} };
        data.blockIndices[i] = data.paletteIndex(state);
    }

    Q_UNUSED(maxId);
    return true;
}

} // namespace

// ============================================================================
// LitematicEditor 公开接口实现
// ============================================================================

bool LitematicEditor::editMetadata(const QString &filePath,
                                   const QString &name,
                                   const QString &author,
                                   const QString &description,
                                   QString *errorOut)
{
    NbtTag root;
    if (!NbtCodec::readCompressedFile(filePath, root))
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("无法读取或解析 .litematic 文件");
        }
        return false;
    }

    NbtTag *metadata = NbtCodec::findChild(root, QStringLiteral("Metadata"));
    if (metadata == nullptr || metadata->type != NbtType::Compound)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral(".litematic 缺少 Metadata 复合标签");
        }
        return false;
    }

    auto setOrAppendString = [](NbtTag &compound, const QString &key, const QString &value) {
        NbtTag *existing = NbtCodec::findChild(compound, key);
        if (existing != nullptr)
        {
            existing->type = NbtType::String;
            existing->stringValue = value;
        }
        else
        {
            compound.children.append(NbtCodec::makeString(key, value));
        }
    };

    if (!name.isEmpty())
    {
        setOrAppendString(*metadata, QStringLiteral("Name"), name);
    }
    if (!author.isEmpty())
    {
        setOrAppendString(*metadata, QStringLiteral("Author"), author);
    }
    if (!description.isEmpty())
    {
        setOrAppendString(*metadata, QStringLiteral("Description"), description);
    }

    // 更新修改时间
    NbtTag *timeModified = NbtCodec::findChild(*metadata, QStringLiteral("TimeModified"));
    if (timeModified != nullptr)
    {
        timeModified->type = NbtType::Long;
        timeModified->intValue = QDateTime::currentMSecsSinceEpoch();
    }
    else
    {
        metadata->children.append(
            NbtCodec::makeLong(QStringLiteral("TimeModified"), QDateTime::currentMSecsSinceEpoch()));
    }

    if (!NbtCodec::writeCompressedFile(filePath, root))
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("写回 .litematic 文件失败");
        }
        return false;
    }
    return true;
}

bool LitematicEditor::convertToSchematica(const QString &inputPath,
                                          const QString &outputPath,
                                          QString *errorOut)
{
    SchematicData data;
    if (!readLitematic(inputPath, data, errorOut))
    {
        return false;
    }
    return writeSchematica(outputPath, data, errorOut);
}

bool LitematicEditor::convertToSponge(const QString &inputPath,
                                      const QString &outputPath,
                                      QString *errorOut)
{
    SchematicData data;
    if (!readLitematic(inputPath, data, errorOut))
    {
        return false;
    }
    return writeSponge(outputPath, data, errorOut);
}

bool LitematicEditor::convertFromSchematica(const QString &inputPath,
                                            const QString &outputPath,
                                            QString *errorOut)
{
    SchematicData data;
    if (!readSchematica(inputPath, data, errorOut))
    {
        return false;
    }
    return writeLitematic(outputPath, data, errorOut);
}

bool LitematicEditor::convertFromSponge(const QString &inputPath,
                                        const QString &outputPath,
                                        QString *errorOut)
{
    SchematicData data;
    if (!readSponge(inputPath, data, errorOut))
    {
        return false;
    }
    return writeLitematic(outputPath, data, errorOut);
}

bool LitematicEditor::convertVersion(const QString &inputPath,
                                     const QString &outputPath,
                                     int targetVersion,
                                     int targetDataVersion,
                                     QString *errorOut)
{
    if (targetVersion < 1 || targetVersion > 7)
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("无效的目标版本号：%1").arg(targetVersion);
        }
        return false;
    }

    NbtTag root;
    if (!NbtCodec::readCompressedFile(inputPath, root))
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("无法读取或解析 .litematic 文件");
        }
        return false;
    }

    // Version（根级）
    NbtTag *version = NbtCodec::findChild(root, QStringLiteral("Version"));
    if (version != nullptr)
    {
        version->type = NbtType::Int;
        version->intValue = targetVersion;
    }
    else
    {
        // 找不到 Version 字段则按合理顺序插入（保持 NBT 语义无效但可解析）
        root.children.append(NbtCodec::makeInt(QStringLiteral("Version"), targetVersion));
    }

    // 可选：同步改写 MinecraftDataVersion
    if (targetDataVersion > 0)
    {
        NbtTag *dataVersion = NbtCodec::findChild(root, QStringLiteral("MinecraftDataVersion"));
        if (dataVersion != nullptr)
        {
            dataVersion->type = NbtType::Int;
            dataVersion->intValue = targetDataVersion;
        }
        else
        {
            root.children.append(
                NbtCodec::makeInt(QStringLiteral("MinecraftDataVersion"), targetDataVersion));
        }
    }

    // 重写文件（写入临时文件后原子替换，避免损坏原文件）
    if (!NbtCodec::writeCompressedFile(outputPath, root))
    {
        if (errorOut != nullptr)
        {
            *errorOut = QStringLiteral("写入转换后的 .litematic 文件失败");
        }
        return false;
    }
    return true;
}

QString LitematicEditor::minecraftVersionRange(int litematicaVersion)
{
    switch (litematicaVersion)
    {
    case 4: return QStringLiteral("1.12 ~ 1.15");
    case 5: return QStringLiteral("1.16");
    case 6: return QStringLiteral("1.17 ~ 1.20.4");
    case 7: return QStringLiteral("1.21+");
    default: return QString();
    }
}

int LitematicEditor::currentVersion(const QString &filePath)
{
    NbtTag root;
    if (!NbtCodec::readCompressedFile(filePath, root))
    {
        return -1;
    }
    const NbtTag *version = NbtCodec::findChild(root, QStringLiteral("Version"));
    if (version == nullptr || version->type != NbtType::Int)
    {
        return -1;
    }
    return static_cast<int>(version->intValue);
}
