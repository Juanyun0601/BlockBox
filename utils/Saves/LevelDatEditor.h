/**
 * @file   LevelDatEditor.h
 * @brief  Minecraft 单机存档 level.dat 读写编辑器
 * @author BlockBox Team
 * @date   2026-08-01
 *
 * 提供对 Minecraft 单机存档 level.dat（NBT + gzip）的完整读取与修改能力。
 * 功能参考开源启动器 PCL-CE 的 SaveManager / SaveInfo：
 *   - 读取：版本、存档名、种子、最后游玩时间、出生点、游戏模式、
 *           游玩时长、难度、是否锁定难度、是否允许作弊
 *   - 修改：允许作弊（Data.allowCommands）、难度（Data.Difficulty /
 *           difficulty_settings.difficulty）、锁定难度（Data.DifficultyLocked）
 *
 * 写入采用原子替换：先写临时文件，再把旧文件备份为 level.dat_old，
 * 最后重命名完成替换，避免中途断电损坏存档。
 */

#pragma once

#include <QString>
#include <optional>

/**
 * @brief 从 level.dat 解析得到的完整存档信息
 */
struct SaveInfo
{
    QString levelName;              ///< 存档名称（Data.LevelName）
    QString versionName;            ///< 游戏版本名（Data.Version.Name）

    qint64 lastPlayed = 0;          ///< 最后游玩时间（毫秒时间戳，Data.LastPlayed）
    qint64 playTimeTicks = 0;       ///< 累计游戏时长（游戏刻，Data.Time）

    bool hasSeed = false;           ///< 是否成功读取到种子
    qint64 seed = 0;                ///< 世界种子（WorldGenSettings.seed / RandomSeed）

    bool hasSpawn = false;          ///< 是否读取到出生点
    int spawnX = 0;                 ///< 出生点 X（Data.SpawnX）
    int spawnY = 0;                 ///< 出生点 Y（Data.SpawnY）
    int spawnZ = 0;                 ///< 出生点 Z（Data.SpawnZ）

    int gameType = 0;               ///< 游戏模式（0=生存，1=创造，2=冒险，3=观察者）
    bool hasGameType = false;       ///< 是否读取到游戏模式
    bool hardcore = false;          ///< 是否为极限模式

    bool hasDayTime = false;        ///< 是否读取到游戏内时间
    qint64 dayTime = 0;             ///< 游戏内时间（DayTime，游戏刻，模 24000）

    bool allowCommands = false;     ///< 是否允许作弊命令（Data.allowCommands）
    bool hasDifficulty = false;     ///< 是否读取到难度
    int difficulty = 1;             ///< 难度（0=和平，1=简单，2=普通，3=困难）
    bool difficultyLocked = false;  ///< 难度是否已锁定

    QString folderPath;             ///< 存档文件夹绝对路径
    QString levelDatPath;           ///< 实际读取/写入的 level.dat 路径
};

/**
 * @brief 用户希望对存档应用的修改。仅 std::optional 有值的字段会被写入。
 */
struct SaveChanges
{
    std::optional<bool> allowCommands;      ///< 允许作弊
    std::optional<int> difficulty;          ///< 难度（0~3）
    std::optional<bool> difficultyLocked;   ///< 锁定难度
    std::optional<int> gameType;            ///< 游戏模式（0~3）
    std::optional<bool> hardcore;           ///< 极限模式
    std::optional<QString> levelName;       ///< 世界名称（Data.LevelName）
    std::optional<qint64> dayTime;          ///< 游戏内时间（DayTime，游戏刻）
    std::optional<int> spawnX;              ///< 出生点 X
    std::optional<int> spawnY;              ///< 出生点 Y
    std::optional<int> spawnZ;              ///< 出生点 Z

    bool isEmpty() const
    {
        return !allowCommands.has_value()
            && !difficulty.has_value()
            && !difficultyLocked.has_value()
            && !gameType.has_value()
            && !hardcore.has_value()
            && !levelName.has_value()
            && !dayTime.has_value()
            && !spawnX.has_value()
            && !spawnY.has_value()
            && !spawnZ.has_value();
    }
};

/**
 * @brief Minecraft 单机存档 level.dat 读写编辑器
 *
 * 负责 gzip 解压/压缩、NBT 二进制解析/序列化，
 * 以及字段的定位与原子写入。模块无状态，线程安全，可被共享。
 */
class LevelDatEditor
{
public:
    /**
     * @brief 读取并解析指定存档文件夹的 level.dat
     * @param folderPath 存档文件夹绝对路径（内含 level.dat）
     * @return 解析成功返回 SaveInfo；失败返回 std::nullopt
     */
    static std::optional<SaveInfo> readSave(const QString &folderPath);

    /**
     * @brief 将修改应用到指定存档的 level.dat（原子写入）
     * @param folderPath 存档文件夹绝对路径
     * @param changes    待应用的修改（为空则直接返回 false）
     * @param errorOut   可选，失败时输出原因
     * @return 至少有一项修改成功写入时返回 true
     */
    static bool applyChanges(const QString &folderPath,
                             const SaveChanges &changes,
                             QString *errorOut = nullptr);

    /**
     * @brief 重命名世界：重命名存档文件夹并同步更新 level.dat 中的世界名
     * @param folderPath 当前存档文件夹绝对路径
     * @param newFolderName 新文件夹名（不含路径）
     * @param newPathOut 可选，成功时输出重命名后的文件夹绝对路径
     * @param errorOut 可选，失败时输出原因
     * @return 成功返回 true
     */
    static bool renameWorld(const QString &folderPath,
                            const QString &newFolderName,
                            QString *newPathOut = nullptr,
                            QString *errorOut = nullptr);
};
