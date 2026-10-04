/**
 * @file   GameDetector.h
 * @brief  运行中的 Minecraft 游戏进程检测器
 * @author BlockBox Team
 * @date   2026-07-05
 */

#pragma once

#include <QString>
#include <optional>

/**
 * @brief 运行中的 Minecraft 游戏进程信息
 */
struct GameProcessInfo
{
  QString version;  ///< 游戏版本（如 "1.20.4"）
  QString gameDir;  ///< 游戏目录（.minecraft 根目录）
  QString jarPath;  ///< 版本 JAR 文件路径
  qint64 pid = 0;   ///< 进程 ID
};

/**
 * @brief 运行中的游戏进程检测器
 *
 * 通过 Windows 进程枚举查找 javaw.exe，提取命令行参数中的
 * Minecraft 版本号与游戏目录，用于自动识别当前正在游玩的游戏。
 */
class GameDetector
{
public:
  /**
   * @brief 轻量检测：是否存在运行中的 Minecraft 进程（仅枚举进程快照，无 PowerShell 调用）
   * @return 存在 javaw.exe 进程返回 true
   *
   * 开销约 1ms，可安全地高频调用（如首页每秒 tick）。
   * detectRunningGame() 会启动 PowerShell 查询命令行，只应在状态跳变时调用。
   */
  static bool isJavaGameRunning();

  /**
   * @brief 检测当前正在运行的 Minecraft 进程
   * @return 检测成功返回进程信息，未检测到返回 std::nullopt
   *
   * 注意：此方法会为每个 javaw.exe 进程同步启动 PowerShell 查询命令行，
   * 最坏情况阻塞 5 秒/进程，禁止在 UI 线程高频调用（应放在后台线程或低频调用）。
   */
  static std::optional<GameProcessInfo> detectRunningGame();

private:
  /**
   * @brief 从命令行参数中解析 Minecraft 版本与游戏目录
   * @param commandLine javaw.exe 的完整命令行
   * @return 解析得到的 GameProcessInfo（version/gameDir 可能为空）
   */
  static GameProcessInfo parseCommandLine(const QString &commandLine);
};
