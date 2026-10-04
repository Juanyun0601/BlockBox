/**
 * @file   ModpackFileAdviser.cpp
 * @brief  整合包文件建议器类实现
 * @author BlockBox Team
 * @date   2026-06-19
 */

#include "ModpackFileAdviser.h"

#include <QRegularExpression>
#include <QStringList>

namespace modpack {

// ── 隐藏黑名单：这些文件/目录在导出时不应出现在文件树中 ──
static const QStringList HIDDEN_LIST = {
  // 日志与崩溃报告
  "regex:.*\\.log$",
  "logs",
  "crash-reports",

  // 缓存与临时文件
  "cache",
  ".fabric",
  ".mixin.out",
  "asm",
  "backups",
  "webcache2",

  // 启动器配置文件
  "hmclversion.cfg",
  "launcher_profiles.json",
  "pack.json",
  "launcher_accounts.json",
  "launcher_cef_log.txt",
  "launcher_log.txt",
  "launcher_msa_credentials.bin",
  "launcher_settings.json",
  "launcher_ui_state.json",
  "realms_persistence.json",
  "treatment_tags.json",
  "clientId.txt",
  "PCL.ini",

  // 版本、资源与库文件
  "versions",
  "assets",
  "libraries",
  "natives",
  "native",
  "$native",
  "$natives",
  "server-resource-packs",

  // 用户缓存
  "usernamecache.json",
  "usercache.json",

  // 截图与命令历史
  "screenshots",
  "command_history.txt",

  // 下载与依赖
  "downloads",
  "essential",

  // 模组追踪与皮肤
  "TCNodeTracker",
  "CustomDISkins",
  "data",
  "CustomSkinLoader/caches",

  // 调试
  "debug",

  // 回放
  ".replay_cache",
  "replay_recordings",
  "replay_videos",

  // 特定文件
  "irisUpdateInfo.json",
  "modernfix",
  "modtranslations",
  "schematics",

  // 子目录
  "journeymap/data",
  "mods/.connector",

  // 整合包清单
  "manifest.json",
  "minecraftinstance.json",
  ".curseclient",
  "modrinth.index.json"
};

// ── 建议黑名单：这些文件/目录显示但不默认选中 ──
static const QStringList NORMAL_LIST = {
  "saves",
  "servers.dat",
  "options.txt",
  "optionsof.txt",
  "optionsshaders.txt",
  "journeymap",
  "blueprints",
  "fonts",
  "mods/VoxelMods"
};

bool ModpackFileAdviser::matchesPattern(const QString& fileName, const QString& pattern)
{
  // 正则表达式匹配：以 "regex:" 为前缀
  if (pattern.startsWith("regex:"))
  {
    QString regexStr = pattern.mid(6);
    QRegularExpression regex(regexStr);
    return regex.match(fileName).hasMatch();
  }

  // 目录前缀匹配：精确匹配或作为前缀目录匹配
  if (fileName == pattern)
  {
    return true;
  }
  if (fileName.startsWith(pattern + "/"))
  {
    return true;
  }

  return false;
}

FileSuggestion ModpackFileAdviser::suggest(const QString& fileName, bool isDirectory)
{
  Q_UNUSED(isDirectory)

  // 优先检查隐藏黑名单
  for (const auto& pattern : HIDDEN_LIST)
  {
    if (matchesPattern(fileName, pattern))
    {
      return FileSuggestion::HIDDEN;
    }
  }

  // 检查建议黑名单
  for (const auto& pattern : NORMAL_LIST)
  {
    if (matchesPattern(fileName, pattern))
    {
      return FileSuggestion::NORMAL;
    }
  }

  // 默认建议选中
  return FileSuggestion::SUGGESTED;
}

QList<ModuleInfo> ModpackFileAdviser::getModules()
{
  return {
    {"mods",          "模组",   {"mods/"},                               true},
    {"configs",       "配置",   {"config/"},                              true},
    {"saves",         "存档",   {"saves/"},                               false},
    {"resourcepacks", "资源包", {"resourcepacks/"},                       false},
    {"shaderpacks",   "光影",   {"shaderpacks/"},                         false},
    {"screenshots",   "截图",   {"screenshots/"},                         false},
    {"scripts",       "脚本",   {"scripts/"},                             true},
    {"options",       "选项文件", {"options.txt", "optionsof.txt", "servers.dat"}, true},
  };
}

QString ModpackFileAdviser::getModuleForFile(const QString& relativePath)
{
  const auto modules = getModules();
  for (const auto& module : modules)
  {
    for (const auto& pattern : module.patterns)
    {
      if (pattern.endsWith('/'))
      {
        // 目录前缀匹配："mods/" 匹配 "mods/foo.jar" 和 "mods/sub/bar.jar"
        if (relativePath.startsWith(pattern))
        {
          return module.id;
        }
      }
      else
      {
        // 精确文件名匹配："options.txt" 匹配 "options.txt"（仅根目录）
        if (relativePath == pattern)
        {
          return module.id;
        }
      }
    }
  }
  return {};
}

} // namespace modpack