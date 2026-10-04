/**
 * @file   LitematicReader.h
 * @brief  Minecraft Litematica 投影文件读取器
 * @author BlockBox Team
 * @date   2026-07-17
 *
 * 提供从 .litematic 投影文件中读取元信息与材料列表的能力。
 * litematic 文件为 NBT（Named Binary Tag）格式，外层使用 gzip 压缩。
 *
 * 本模块提取以下信息：
 *   - 名称（Metadata.Name）/ 作者（Metadata.Author）/ 描述（Metadata.Description）
 *   - 创建/修改时间（Metadata.TimeCreated / TimeModified，毫秒时间戳）
 *   - 包围盒尺寸（Metadata.EnclosingSize.x/y/z）
 *   - 总方块数（Metadata.TotalBlocks）/ 总体积（Metadata.TotalVolume）/ 区域数（Metadata.RegionCount）
 *   - Minecraft 数据版本（MinecraftDataVersion）/ Litematica 格式版本（Version）
 *   - 各区域信息（Regions.<name>.Position / Size / 调色板大小 / 实际方块数）
 *   - 材料清单（聚合各区域 BlockStatePalette + BlockStates 计数，按数量降序）
 *
 * Litematica 文件格式参考：
 *   https://github.com/maruohon/litematica/blob/master/src/main/java/fi/dy/masa/litematica/schematic/
 *
 * BlockStates 位打包规则（Litematica 特有，与 1.13+ vanilla chunk 存储一致）：
 *   - bitsPerBlock = max(2, ceilLog2(paletteSize))
 *   - 索引按全局位偏移连续排列，值可跨 long 边界
 *   - 索引 i 的起始位 = i * bitsPerBlock，跨 long 时低 64 - startBitOffset 位
 *     取前一个 long，其余取后一个 long
 */

#pragma once

#include <QList>
#include <QString>
#include <optional>

/**
 * @brief 材料清单中的单项（方块 ID + 数量）
 */
struct LitematicBlockCount
{
    QString blockId;  ///< 方块 ID，如 "minecraft:stone"
    int count = 0;    ///< 该方块在整个投影中的总数量
};

/**
 * @brief 单个区域（Region）的统计信息
 */
struct LitematicRegionInfo
{
    QString name;        ///< 区域名（Regions 下的 key）
    int posX = 0;        ///< 区域原点 X 坐标（Position.x）
    int posY = 0;        ///< 区域原点 Y 坐标（Position.y）
    int posZ = 0;        ///< 区域原点 Z 坐标（Position.z）
    int sizeX = 0;       ///< 区域尺寸 X（Size.x，可能为负，表示方向）
    int sizeY = 0;       ///< 区域尺寸 Y（Size.y，可能为负）
    int sizeZ = 0;       ///< 区域尺寸 Z（Size.z，可能为负）
    int volume = 0;      ///< 区域体积 = abs(sizeX) * abs(sizeY) * abs(sizeZ)
    int blockCount = 0;  ///< 区域内非空气方块数（遍历 BlockStates 计数）
    int paletteSize = 0; ///< 调色板大小（BlockStatePalette 长度）
};

/**
 * @brief 从 .litematic 解析得到的投影信息
 */
struct LitematicInfo
{
    // --- 元信息 ---
    QString name;                    ///< 投影名称（Metadata.Name）
    QString author;                  ///< 作者（Metadata.Author，可能为空）
    QString description;             ///< 描述（Metadata.Description，可能为空）
    qint64 timeCreated = 0;          ///< 创建时间（毫秒时间戳）
    qint64 timeModified = 0;         ///< 修改时间（毫秒时间戳）

    // --- 统计 ---
    int totalBlocks = 0;             ///< 总方块数（Metadata.TotalBlocks）
    int totalVolume = 0;             ///< 总体积（Metadata.TotalVolume）
    int regionCount = 0;             ///< 区域数（Metadata.RegionCount）

    // --- 包围盒尺寸 ---
    int enclosingSizeX = 0;          ///< 包围盒 X（Metadata.EnclosingSize.x）
    int enclosingSizeY = 0;          ///< 包围盒 Y（Metadata.EnclosingSize.y）
    int enclosingSizeZ = 0;          ///< 包围盒 Z（Metadata.EnclosingSize.z）

    // --- 版本 ---
    int minecraftDataVersion = 0;    ///< Minecraft 数据版本（MinecraftDataVersion）
    int version = 0;                 ///< Litematica 格式版本（Version）

    // --- 详细数据 ---
    QList<LitematicRegionInfo> regions;          ///< 各区域信息
    QList<LitematicBlockCount> materialList;     ///< 材料清单（聚合各区域，按数量降序，已剔除空气）
};

/**
 * @brief Litematica 投影文件读取器
 *
 * 负责 gzip 解压、NBT 二进制解析与 Litematica 特有的位打包解码，
 * 仅提供 readFile() 静态方法，无状态。
 *
 * 使用示例：
 * @code
 *   auto info = LitematicReader::readFile(QStringLiteral("D:/schematics/house.litematic"));
 *   if (info)
 *   {
 *       qDebug() << "name:" << info->name
 *                << "size:" << info->enclosingSizeX << "x" << info->enclosingSizeY << "x" << info->enclosingSizeZ
 *                << "materials:" << info->materialList.size();
 *   }
 * @endcode
 */
class LitematicReader
{
public:
    /**
     * @brief 读取并解析 .litematic 文件
     * @param path .litematic 文件绝对路径
     * @return 解析成功返回 LitematicInfo，失败返回 std::nullopt（同时 qWarning 输出原因）
     *
     * 失败原因包括：文件不存在、gzip 解压失败、NBT 格式损坏、缺少 Metadata 字段。
     * 材料清单计算失败（如 BlockStates 缺失）不会导致整体失败，仅 materialList 为空。
     */
    static std::optional<LitematicInfo> readFile(const QString &path);
};
