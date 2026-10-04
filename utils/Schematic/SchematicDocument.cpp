/**
 * @file   SchematicDocument.cpp
 * @brief  投影方块级文档模型实现（逐格读写 .litematic）
 * @author BlockBox Team
 * @date   2026-08-18
 */

#include "SchematicDocument.h"

#include "NbtCodec.h"

#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <cmath>
#include <optional>

namespace {

int ceilLog2(int x)
{
    if (x <= 1)
        return 0;
    int bits = 0;
    int v = x - 1;
    while (v > 0) { ++bits; v >>= 1; }
    return bits;
}

int computeBits(int paletteSize)
{
    return std::max(2, ceilLog2(paletteSize));
}

int getBlockIndexAt(const QList<qint64> &longs, qint64 index, int bits)
{
    const qint64 bitOffset = index * bits;
    const int arrIndex = static_cast<int>(bitOffset >> 6);
    const int bitIn  = static_cast<int>(bitOffset & 0x3F);
    const quint64 mask = (1ULL << bits) - 1ULL;
    if (arrIndex >= longs.size())
        return 0;
    if (bitIn + bits <= 64)
        return static_cast<int>((static_cast<quint64>(longs.at(arrIndex)) >> bitIn) & mask);
    const int endOffset = 64 - bitIn;
    const quint64 low = static_cast<quint64>(longs.at(arrIndex)) >> bitIn;
    quint64 high = 0;
    if (arrIndex + 1 < longs.size())
        high = static_cast<quint64>(longs.at(arrIndex + 1)) << endOffset;
    return static_cast<int>((low | high) & mask);
}

QList<qint64> encodeBlockStates(const QList<int> &indices, int bits)
{
    const qint64 totalBits = static_cast<qint64>(indices.size()) * bits;
    const int longCount = static_cast<int>((totalBits + 63) / 64);
    QList<qint64> longs(longCount, 0);
    const quint64 mask = (1ULL << bits) - 1ULL;
    for (int i = 0; i < indices.size(); ++i)
    {
        const qint64 bitOffset = static_cast<qint64>(i) * bits;
        const int arrIndex = static_cast<int>(bitOffset >> 6);
        const int bitIn = static_cast<int>(bitOffset & 0x3F);
        if (arrIndex >= longs.size())
            break;
        const quint64 value = static_cast<quint64>(indices.at(i)) & mask;
        if (bitIn + bits <= 64)
        {
            longs[arrIndex] = static_cast<qint64>(
                static_cast<quint64>(longs.at(arrIndex)) | (value << bitIn));
        }
        else
        {
            const int endOffset = 64 - bitIn;
            longs[arrIndex] = static_cast<qint64>(
                static_cast<quint64>(longs.at(arrIndex))
                | ((value & ((1ULL << endOffset) - 1ULL)) << bitIn));
            if (arrIndex + 1 < longs.size())
                longs[arrIndex + 1] = static_cast<qint64>(
                    static_cast<quint64>(longs.at(arrIndex + 1)) | (value >> endOffset));
        }
    }
    return longs;
}

SchematicBlockState stateFromLitematicPalette(const NbtTag &entry)
{
    SchematicBlockState state;
    state.name = NbtCodec::readStringValue(entry, QStringLiteral("Name"));
    if (state.name.isEmpty())
        state.name = QStringLiteral("minecraft:air");
    const NbtTag *props = NbtCodec::findChild(entry, QStringLiteral("Properties"));
    if (props != nullptr && props->type == NbtType::Compound)
    {
        for (const NbtTag &child : props->children)
        {
            if (child.type == NbtType::String)
                state.properties.insert(child.name, child.stringValue);
        }
    }
    return state;
}

NbtTag litematicPaletteEntryFromState(const SchematicBlockState &state)
{
    NbtTag entry = NbtCodec::makeCompound();
    entry.children.append(NbtCodec::makeString(QStringLiteral("Name"), state.name));
    if (!state.properties.isEmpty())
    {
        NbtTag props = NbtCodec::makeCompound(QStringLiteral("Properties"));
        for (auto it = state.properties.constBegin(); it != state.properties.constEnd(); ++it)
            props.children.append(NbtCodec::makeString(it.key(), it.value()));
        entry.children.append(std::move(props));
    }
    return entry;
}

} // namespace

// ============================================================================
// 加载
// ============================================================================
bool SchematicDocument::load(const QString &filePath, QString *error)
{
    clear();
    m_filePath = filePath;

    NbtTag root;
    if (!NbtCodec::readCompressedFile(filePath, root))
    {
        if (error) *error = QStringLiteral("无法读取或解析 .litematic 文件（gzip/NBT 损坏）");
        return false;
    }

    m_minecraftDataVersion = static_cast<int>(
        NbtCodec::readIntValue(root, QStringLiteral("MinecraftDataVersion"), 0));
    m_version = static_cast<int>(NbtCodec::readIntValue(root, QStringLiteral("Version"), 6));

    const NbtTag *metadata = NbtCodec::findChild(root, QStringLiteral("Metadata"));
    if (metadata != nullptr && metadata->type == NbtType::Compound)
    {
        m_name = NbtCodec::readStringValue(*metadata, QStringLiteral("Name"));
        m_author = NbtCodec::readStringValue(*metadata, QStringLiteral("Author"));
        m_description = NbtCodec::readStringValue(*metadata, QStringLiteral("Description"));
    }

    const NbtTag *regions = NbtCodec::findChild(root, QStringLiteral("Regions"));
    if (regions == nullptr || regions->type != NbtType::Compound)
    {
        if (error) *error = QStringLiteral(".litematic 缺少 Regions 复合标签");
        return false;
    }

    // 收集所有区域并计算全局包围盒
    struct RegionRaw
    {
        int minX, minY, minZ;
        int spanX, spanY, spanZ;
        QList<int> indices;
        QList<SchematicBlockState> palette;
    };
    QList<RegionRaw> regionList;
    bool globalInit = false;
    int gMinX = 0, gMinY = 0, gMinZ = 0, gMaxX = 0, gMaxY = 0, gMaxZ = 0;

    for (const NbtTag &regionEntry : regions->children)
    {
        if (regionEntry.type != NbtType::Compound)
            continue;

        const NbtTag *posTag = NbtCodec::findChild(regionEntry, QStringLiteral("Position"));
        const NbtTag *sizeTag = NbtCodec::findChild(regionEntry, QStringLiteral("Size"));
        int posX = 0, posY = 0, posZ = 0, sizeX = 0, sizeY = 0, sizeZ = 0;
        if (posTag && posTag->type == NbtType::Compound)
        {
            posX = (int)NbtCodec::readIntValue(*posTag, QStringLiteral("x"), 0);
            posY = (int)NbtCodec::readIntValue(*posTag, QStringLiteral("y"), 0);
            posZ = (int)NbtCodec::readIntValue(*posTag, QStringLiteral("z"), 0);
        }
        if (sizeTag && sizeTag->type == NbtType::Compound)
        {
            sizeX = (int)NbtCodec::readIntValue(*sizeTag, QStringLiteral("x"), 0);
            sizeY = (int)NbtCodec::readIntValue(*sizeTag, QStringLiteral("y"), 0);
            sizeZ = (int)NbtCodec::readIntValue(*sizeTag, QStringLiteral("z"), 0);
        }

        const int minX = std::min(posX, posX + sizeX);
        const int minY = std::min(posY, posY + sizeY);
        const int minZ = std::min(posZ, posZ + sizeZ);
        const int spanX = std::abs(sizeX);
        const int spanY = std::abs(sizeY);
        const int spanZ = std::abs(sizeZ);
        if (spanX <= 0 || spanY <= 0 || spanZ <= 0)
            continue;

        RegionRaw raw;
        raw.minX = minX; raw.minY = minY; raw.minZ = minZ;
        raw.spanX = spanX; raw.spanY = spanY; raw.spanZ = spanZ;

        const NbtTag *paletteTag = NbtCodec::findChild(regionEntry, QStringLiteral("BlockStatePalette"));
        if (paletteTag && paletteTag->type == NbtType::List)
        {
            for (const NbtTag &entry : paletteTag->children)
                raw.palette.append(stateFromLitematicPalette(entry));
        }
        if (raw.palette.isEmpty())
            raw.palette.append(SchematicBlockState{ QStringLiteral("minecraft:air"), {} });

        const int bits = computeBits(raw.palette.size());
        const int volume = spanX * spanY * spanZ;
        raw.indices.reserve(volume);

        const NbtTag *blockStates = NbtCodec::findChild(regionEntry, QStringLiteral("BlockStates"));
        QList<qint64> longs;
        if (blockStates && blockStates->type == NbtType::LongArray)
            longs = blockStates->longArray;
        for (int i = 0; i < volume; ++i)
        {
            int idx = 0;
            if (!longs.isEmpty())
            {
                idx = getBlockIndexAt(longs, i, bits);
                if (idx < 0 || idx >= raw.palette.size())
                    idx = 0;
            }
            raw.indices.append(idx);
        }
        regionList.append(std::move(raw));

        const int maxX = minX + spanX - 1, maxY = minY + spanY - 1, maxZ = minZ + spanZ - 1;
        if (!globalInit)
        {
            gMinX = minX; gMinY = minY; gMinZ = minZ;
            gMaxX = maxX; gMaxY = maxY; gMaxZ = maxZ;
            globalInit = true;
        }
        else
        {
            gMinX = std::min(gMinX, minX); gMinY = std::min(gMinY, minY); gMinZ = std::min(gMinZ, minZ);
            gMaxX = std::max(gMaxX, maxX); gMaxY = std::max(gMaxY, maxY); gMaxZ = std::max(gMaxZ, maxZ);
        }
    }

    if (regionList.isEmpty())
    {
        if (error) *error = QStringLiteral(".litematic 中没有有效的区域数据");
        return false;
    }

    m_originX = gMinX; m_originY = gMinY; m_originZ = gMinZ;
    m_sizeX = gMaxX - gMinX + 1;
    m_sizeY = gMaxY - gMinY + 1;
    m_sizeZ = gMaxZ - gMinZ + 1;

    const int layer = layerSize();
    m_indices.fill(0, volume());
    m_palette.clear();
    m_palette.append(SchematicBlockState{ QStringLiteral("minecraft:air"), {} });

    auto remap = [&](int gx, int gy, int gz) {
        const int dx = gx - m_originX, dy = gy - m_originY, dz = gz - m_originZ;
        return dy * layer + dz * m_sizeX + dx;
    };

    for (const RegionRaw &raw : regionList)
    {
        const int rLayer = raw.spanX * raw.spanZ;
        for (int y = 0; y < raw.spanY; ++y)
            for (int z = 0; z < raw.spanZ; ++z)
                for (int x = 0; x < raw.spanX; ++x)
                {
                    const int localIdx = y * rLayer + z * raw.spanX + x;
                    const int p = raw.indices.at(localIdx);
                    if (p < 0 || p >= raw.palette.size())
                        continue;
                    const int globalIdx = remap(raw.minX + x, raw.minY + y, raw.minZ + z);
                    m_indices[globalIdx] = paletteIndexOf(raw.palette.at(p));
                }
    }

    m_loaded = true;
    return true;
}

void SchematicDocument::clear()
{
    m_loaded = false;
    m_filePath.clear();
    m_name.clear();
    m_author.clear();
    m_description.clear();
    m_version = 6;
    m_minecraftDataVersion = 0;
    m_originX = m_originY = m_originZ = 0;
    m_sizeX = m_sizeY = m_sizeZ = 0;
    m_palette.clear();
    m_indices.clear();
}

// ============================================================================
// 几何查询
// ============================================================================
bool SchematicDocument::contains(int x, int y, int z) const
{
    return x >= m_originX && x < m_originX + m_sizeX
        && y >= m_originY && y < m_originY + m_sizeY
        && z >= m_originZ && z < m_originZ + m_sizeZ;
}

int SchematicDocument::indexOf(int x, int y, int z) const
{
    if (!contains(x, y, z))
        return -1;
    const int dx = x - m_originX, dy = y - m_originY, dz = z - m_originZ;
    return dy * layerSize() + dz * m_sizeX + dx;
}

// ============================================================================
// 方块读写
// ============================================================================
SchematicBlockState SchematicDocument::blockAt(int i) const
{
    if (i < 0 || i >= m_indices.size())
        return SchematicBlockState{ QStringLiteral("minecraft:air"), {} };
    const int p = m_indices.at(i);
    if (p < 0 || p >= m_palette.size())
        return SchematicBlockState{ QStringLiteral("minecraft:air"), {} };
    return m_palette.at(p);
}

SchematicBlockState SchematicDocument::block(int x, int y, int z) const
{
    return blockAt(indexOf(x, y, z));
}

bool SchematicDocument::setBlock(int x, int y, int z, const SchematicBlockState &state, QString *error)
{
    const int i = indexOf(x, y, z);
    if (i < 0)
    {
        // 越界：忽略（与投影编辑语义一致，允许扩展到盒外时忽略）
        if (error) *error = QStringLiteral("目标坐标 (%1,%2,%3) 超出投影包围盒").arg(x).arg(y).arg(z);
        return false;
    }
    m_indices[i] = paletteIndexOf(state);
    return true;
}

bool SchematicDocument::clearBlock(int x, int y, int z, QString *error)
{
    return setBlock(x, y, z, SchematicBlockState{ QStringLiteral("minecraft:air"), {} }, error);
}

int SchematicDocument::paletteIndexOf(const SchematicBlockState &state)
{
    for (int i = 0; i < m_palette.size(); ++i)
    {
        if (m_palette.at(i) == state)
            return i;
    }
    m_palette.append(state);
    return m_palette.size() - 1;
}

// ============================================================================
// 批量操作
// ============================================================================
int SchematicDocument::replaceAll(const SchematicBlockState &from, const SchematicBlockState &to,
                                  QString *error)
{
    Q_UNUSED(error);
    const int fromIdx = -1;
    int match = -1;
    for (int i = 0; i < m_palette.size(); ++i)
        if (m_palette.at(i) == from) { match = i; break; }
    Q_UNUSED(fromIdx);

    if (match < 0)
        return 0; // 源方块不存在，无需替换

    const int toIdx = paletteIndexOf(to);
    int count = 0;
    for (int i = 0; i < m_indices.size(); ++i)
    {
        if (m_indices.at(i) == match)
        {
            m_indices[i] = toIdx;
            ++count;
        }
    }
    return count;
}

int SchematicDocument::fillAll(const SchematicBlockState &state, QString *error)
{
    Q_UNUSED(error);
    const int idx = paletteIndexOf(state);
    const int count = m_indices.size();
    for (int i = 0; i < m_indices.size(); ++i)
        m_indices[i] = idx;
    return count;
}

int SchematicDocument::fillBox(int x0, int y0, int z0, int x1, int y1, int z1,
                              const SchematicBlockState &state, QString *error)
{
    const int minX = qMax(m_originX, std::min(x0, x1));
    const int maxX = qMin(m_originX + m_sizeX - 1, std::max(x0, x1));
    const int minY = qMax(m_originY, std::min(y0, y1));
    const int maxY = qMin(m_originY + m_sizeY - 1, std::max(y0, y1));
    const int minZ = qMax(m_originZ, std::min(z0, z1));
    const int maxZ = qMin(m_originZ + m_sizeZ - 1, std::max(z0, z1));

    if (maxX < minX || maxY < minY || maxZ < minZ)
    {
        if (error) *error = QStringLiteral("填充范围与投影无交集");
        return 0;
    }

    const int idx = paletteIndexOf(state);
    int count = 0;
    for (int y = minY; y <= maxY; ++y)
        for (int z = minZ; z <= maxZ; ++z)
            for (int x = minX; x <= maxX; ++x)
            {
                const int i = indexOf(x, y, z);
                m_indices[i] = idx;
                ++count;
            }
    return count;
}

QList<SchematicBlockState> SchematicDocument::palette(bool includeAir) const
{
    QList<SchematicBlockState> out;
    for (const auto &state : m_palette)
    {
        if (!includeAir && state.isAir())
            continue;
        out.append(state);
    }
    return out;
}

QMap<QString, int> SchematicDocument::countByState() const
{
    QMap<QString, int> counts;
    for (int i = 0; i < m_indices.size(); ++i)
    {
        const int p = m_indices.at(i);
        if (p < 0 || p >= m_palette.size())
            continue;
        counts[m_palette.at(p).toString()] += 1;
    }
    return counts;
}

QVector<SchematicBlockState> SchematicDocument::dumpAll() const
{
    QVector<SchematicBlockState> out;
    out.reserve(m_indices.size());
    for (int i = 0; i < m_indices.size(); ++i)
        out.append(blockAt(i));
    return out;
}

// ============================================================================
// 保存
// ============================================================================
bool SchematicDocument::writeToNbt(NbtTag &root) const
{
    NbtTag rootTag = NbtCodec::makeCompound();

    rootTag.children.append(NbtCodec::makeInt(QStringLiteral("MinecraftDataVersion"),
                                              m_minecraftDataVersion > 0 ? m_minecraftDataVersion : 3955));
    rootTag.children.append(NbtCodec::makeInt(QStringLiteral("Version"), 6));
    rootTag.children.append(NbtCodec::makeInt(QStringLiteral("SubVersion"), 4));

    NbtTag metadata = NbtCodec::makeCompound(QStringLiteral("Metadata"));
    metadata.children.append(NbtCodec::makeString(QStringLiteral("Name"), m_name));
    metadata.children.append(NbtCodec::makeString(QStringLiteral("Author"), m_author));
    metadata.children.append(NbtCodec::makeString(QStringLiteral("Description"), m_description));
    metadata.children.append(NbtCodec::makeLong(QStringLiteral("TimeCreated"),
                                                QDateTime::currentMSecsSinceEpoch()));
    metadata.children.append(NbtCodec::makeLong(QStringLiteral("TimeModified"),
                                                QDateTime::currentMSecsSinceEpoch()));

    NbtTag enclosing = NbtCodec::makeCompound(QStringLiteral("EnclosingSize"));
    enclosing.children.append(NbtCodec::makeInt(QStringLiteral("x"), m_sizeX));
    enclosing.children.append(NbtCodec::makeInt(QStringLiteral("y"), m_sizeY));
    enclosing.children.append(NbtCodec::makeInt(QStringLiteral("z"), m_sizeZ));
    metadata.children.append(std::move(enclosing));

    int totalBlocks = 0;
    for (int i = 0; i < m_indices.size(); ++i)
    {
        const int p = m_indices.at(i);
        if (p >= 0 && p < m_palette.size() && !m_palette.at(p).isAir())
            ++totalBlocks;
    }
    metadata.children.append(NbtCodec::makeInt(QStringLiteral("TotalBlocks"), totalBlocks));
    metadata.children.append(NbtCodec::makeInt(QStringLiteral("TotalVolume"), volume()));
    metadata.children.append(NbtCodec::makeInt(QStringLiteral("RegionCount"), 1));
    rootTag.children.append(std::move(metadata));

    NbtTag regions = NbtCodec::makeCompound(QStringLiteral("Regions"));
    NbtTag region = NbtCodec::makeCompound(QStringLiteral("Region"));

    NbtTag posTag = NbtCodec::makeCompound(QStringLiteral("Position"));
    posTag.children.append(NbtCodec::makeInt(QStringLiteral("x"), 0));
    posTag.children.append(NbtCodec::makeInt(QStringLiteral("y"), 0));
    posTag.children.append(NbtCodec::makeInt(QStringLiteral("z"), 0));
    region.children.append(std::move(posTag));

    NbtTag sizeTag = NbtCodec::makeCompound(QStringLiteral("Size"));
    sizeTag.children.append(NbtCodec::makeInt(QStringLiteral("x"), m_sizeX));
    sizeTag.children.append(NbtCodec::makeInt(QStringLiteral("y"), m_sizeY));
    sizeTag.children.append(NbtCodec::makeInt(QStringLiteral("z"), m_sizeZ));
    region.children.append(std::move(sizeTag));

    NbtTag palette = NbtCodec::makeList(QStringLiteral("BlockStatePalette"), NbtType::Compound, {});
    for (const auto &state : m_palette)
        palette.children.append(litematicPaletteEntryFromState(state));
    region.children.append(std::move(palette));

    const int bits = computeBits(m_palette.size());
    const QList<qint64> blockStates = encodeBlockStates(m_indices, bits);
    region.children.append(NbtCodec::makeLongArray(QStringLiteral("BlockStates"), blockStates));

    region.children.append(NbtCodec::makeList(QStringLiteral("TileEntities"), NbtType::Compound, {}));
    region.children.append(NbtCodec::makeList(QStringLiteral("Entities"), NbtType::Compound, {}));

    regions.children.append(std::move(region));
    rootTag.children.append(std::move(regions));

    root = rootTag;
    return true;
}

bool SchematicDocument::saveCopy(const QString &outputPath, QString *error)
{
    if (!m_loaded)
    {
        if (error) *error = QStringLiteral("文档尚未加载");
        return false;
    }
    NbtTag root;
    if (!writeToNbt(root))
    {
        if (error) *error = QStringLiteral("构建 .litematic 内容失败");
        return false;
    }
    if (!NbtCodec::writeCompressedFile(outputPath, root))
    {
        if (error) *error = QStringLiteral("写入 .litematic 文件失败");
        return false;
    }
    return true;
}
