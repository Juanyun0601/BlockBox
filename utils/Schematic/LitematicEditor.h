/**
 * @file   LitematicEditor.h
 * @brief  Litematica 投影文件编辑器与格式转换器
 * @author BlockBox Team
 * @date   2026-08-02
 *
 * 提供对 .litematic 投影文件的：
 *   - 元数据编辑：名称 / 作者 / 描述（写回 .litematic）
 *   - 格式转换：
 *       .litematic ↔ .schematic（Schematica 格式，Blocks/Data/AddBlocks）
 *       .litematic ↔ .schem（Sponge v2 格式，Palette/BlockData）
 *
 * 参考格式规范：
 *   Litematica：gzip + NBT，BlockStates 为跨界位打包 LongArray，
 *               bitsPerBlock = max(2, ceilLog2(paletteSize))
 *   Schematica：Blocks（低 8 位）+ AddBlocks（高 4 位，两方块一字节）+
 *               Data（元数据）+ SchematicaMapping（id → 方块名，0-4095）
 *   Sponge v2：Palette（方块状态字符串 → id）+ BlockData（varint 位打包）
 */

#pragma once

#include <QString>

/**
 * @brief Litematica 投影文件编辑器与格式转换器
 *
 * 全部为静态方法，无状态，可被共享调用。
 * 失败原因通过 errorOut（可选）输出，并通过 qWarning 记录。
 */
class LitematicEditor
{
public:
    /**
     * @brief 修改 .litematic 文件的元数据（名称/作者/描述）并写回
     * @param filePath    .litematic 文件绝对路径
     * @param name        新名称（传空表示不改）
     * @param author      新作者（传空表示不改）
     * @param description 新描述（传空表示不改）
     * @param errorOut    可选，失败时输出原因
     * @return 成功返回 true
     */
    static bool editMetadata(const QString &filePath,
                             const QString &name,
                             const QString &author,
                             const QString &description,
                             QString *errorOut = nullptr);

    /**
     * @brief 将 .litematic 转换为 .schematic（Schematica 格式）
     * @param inputPath  源 .litematic 文件
     * @param outputPath 输出 .schematic 文件路径
     * @param errorOut   可选，失败时输出原因
     * @return 成功返回 true
     */
    static bool convertToSchematica(const QString &inputPath,
                                    const QString &outputPath,
                                    QString *errorOut = nullptr);

    /**
     * @brief 将 .litematic 转换为 .schem（Sponge v2 格式）
     */
    static bool convertToSponge(const QString &inputPath,
                                const QString &outputPath,
                                QString *errorOut = nullptr);

    /**
     * @brief 将 .schematic（Schematica 格式）转换为 .litematic
     */
    static bool convertFromSchematica(const QString &inputPath,
                                      const QString &outputPath,
                                      QString *errorOut = nullptr);

    /**
     * @brief 将 .schem（Sponge v2 格式）转换为 .litematic
     */
    static bool convertFromSponge(const QString &inputPath,
                                  const QString &outputPath,
                                  QString *errorOut = nullptr);

    /**
     * @brief 转换 .litematic 的格式版本号（Version）
     * @param inputPath    源 .litematic 文件
     * @param outputPath   输出 .litematic 文件路径（可与输入相同即就地修改）
     * @param targetVersion 目标版本号（4/5/6/7）
     * @param targetDataVersion 目标 Minecraft 数据版本（可选，>0 时同步改写 MinecraftDataVersion）
     * @param errorOut     可选，失败时输出原因
     * @return 成功返回 true
     *
     * 版本对应关系（参考 litematica 官方）：
     *   Version 4: Minecraft 1.12 ~ 1.15
     *   Version 5: Minecraft 1.16
     *   Version 6: Minecraft 1.17 ~ 1.20.4
     *   Version 7: Minecraft 1.21+
     *
     * 仅改写元数据中的版本标签（Version / 可选 MinecraftDataVersion），
     * 不重编码方块数据；不同 MC 版本间的方块调色板差异需另行处理。
     */
    static bool convertVersion(const QString &inputPath,
                               const QString &outputPath,
                               int targetVersion,
                               int targetDataVersion = 0,
                               QString *errorOut = nullptr);

    /**
     * @brief 由 Litematica 版本号获取对应的 Minecraft 游戏版本范围描述
     * @param litematicaVersion Version 字段值（4/5/6/7）
     * @return 如 "1.17 ~ 1.20.4"；未知时返回空字符串
     */
    static QString minecraftVersionRange(int litematicaVersion);

    /**
     * @brief 读取 .litematic 当前的 Litematica Version 号
     * @return 解析成功返回版本号；无法解析返回 -1
     */
    static int currentVersion(const QString &filePath);
};
