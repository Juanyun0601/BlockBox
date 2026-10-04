/**
 * @file   ModpackFileAdviser.h
 * @brief  整合包文件建议器类声明，用于导出整合包时判断文件是否应被选中
 * @author BlockBox Team
 * @date   2026-06-19
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace modpack {

/**
 * @brief 文件建议类型枚举
 */
enum class FileSuggestion
{
  SUGGESTED, ///< 建议选中（默认选中）
  NORMAL,    ///< 正常显示但默认不选中
  HIDDEN     ///< 隐藏（不出现在文件树中）
};

/**
 * @brief 模块信息结构体
 *
 * 描述整合包中的一个模块（如 mods、configs 等），
 * 包含模块 ID、名称、匹配模式及默认选中状态。
 */
struct ModuleInfo
{
  QString id;             ///< 模块唯一标识，如 "mods"、"configs"
  QString name;           ///< 模块显示名称，如 "模组"、"配置"
  QStringList patterns;   ///< 路径匹配模式列表
  bool defaultSelected;   ///< 是否默认选中
};

/**
 * @brief 整合包文件建议器
 *
 * 提供文件建议逻辑，用于整合包导出时对文件进行分类：
 * - SUGGESTED: 建议默认选中（mods、config、scripts、resourcepacks 等）
 * - NORMAL: 显示但不默认选中（saves、servers.dat、options.txt 等）
 * - HIDDEN: 隐藏不显示（日志、缓存、启动器配置等）
 *
 * 支持两种匹配模式：
 * - 目录前缀匹配：如 "logs" 匹配 logs/ 目录及其下所有文件
 * - 正则表达式匹配：以 "regex:" 为前缀，如 "regex:.*\\.log$" 匹配所有 .log 文件
 */
class ModpackFileAdviser
{
public:
  ModpackFileAdviser() = delete;

  /**
   * @brief 判断文件或目录是否应被建议选中
   * @param fileName 文件或目录的相对路径
   * @param isDirectory 是否为目录
   * @return 文件建议类型
   */
  static FileSuggestion suggest(const QString& fileName, bool isDirectory);

  /**
   * @brief 获取所有模块信息列表
   * @return 模块信息列表
   */
  static QList<ModuleInfo> getModules();

  /**
   * @brief 根据文件相对路径获取所属模块 ID
   * @param relativePath 文件相对路径
   * @return 所属模块 ID，若不匹配则返回空字符串
   */
  static QString getModuleForFile(const QString& relativePath);

private:
  static bool matchesPattern(const QString& fileName, const QString& pattern);
};

} // namespace modpack