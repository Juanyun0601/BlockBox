/**
 * @file   LevelDatReader.h
 * @brief  Minecraft level.dat 文件读取器
 * @author BlockBox Team
 * @date   2026-07-04
 *
 * 提供从 Minecraft 单机存档的 level.dat 文件中读取上下文信息的能力。
 * level.dat 为 NBT（Named Binary Tag）格式，外层使用 gzip 压缩。
 * 本模块提取以下字段：
 *   - Data.allowCommands（TAG_Byte）—— 是否开启作弊
 *   - Data.GameType（TAG_Int）—— 游戏模式
 *   - Data.Version.Name（TAG_String）—— 游戏版本名
 *   - Data.LevelName（TAG_String）—— 存档名称
 *   - Data.LastPlayed（TAG_Long）—— 最后游玩时间（毫秒时间戳）
 */

#pragma once

#include <QString>
#include <optional>

/**
 * @brief 从 level.dat 解析得到的存档上下文信息
 */
struct LevelInfo
{
    bool allowCommands = false;  ///< 是否开启作弊（0=关闭，1=开启）
    int gameType = 0;            ///< 游戏模式（0=生存，1=创造，2=冒险，3=观察者）
    QString versionName;         ///< 游戏版本名（如 "Java版 1.20.4" 或 "1.20.4"）
    QString levelName;           ///< 存档名称（Data.LevelName）
    qint64 lastPlayed = 0;       ///< 最后游玩时间（毫秒时间戳，Data.LastPlayed）
};

/**
 * @brief Minecraft 单机存档 level.dat 读取器
 *
 * 负责 gzip 解压与 NBT 二进制解析，仅提取快捷指令助手所需的
 * Data.allowCommands / Data.GameType / Data.Version.Name 三个字段。
 *
 * 使用示例：
 * @code
 *   auto info = LevelDatReader::readLevelDat(QStringLiteral("D:/saves/World1/level.dat"));
 *   if (info)
 *   {
 *       qDebug() << "allowCommands:" << info->allowCommands
 *                << "gameType:" << info->gameType
 *                << "version:" << info->versionName;
 *   }
 * @endcode
 */
class LevelDatReader
{
public:
    /**
     * @brief 读取并解析 level.dat
     * @param path level.dat 文件绝对路径
     * @return 解析成功返回 LevelInfo，失败返回 std::nullopt（同时 qWarning 输出原因）
     *
     * 失败原因包括：文件不存在、gzip 解压失败、NBT 格式损坏、缺少必要字段。
     */
    static std::optional<LevelInfo> readLevelDat(const QString &path);
};
