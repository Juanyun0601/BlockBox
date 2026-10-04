/**
 * @file   BlockBoxTypes.h
 * @brief  BlockBox 原生插件 SDK 公共类型定义
 * @author BlockBox Team
 * @date   2026-09-08
 *
 * 本文件定义了插件与宿主之间通信所需的所有公共数据结构。
 * 插件开发者和宿主内部均使用此头文件，确保二进制兼容。
 */
#ifndef BLOCKBOXTYPES_H
#define BLOCKBOXTYPES_H

#ifdef _WIN32
    #define BB_PLUGIN_EXPORT __declspec(dllexport)
    #define BB_PLUGIN_IMPORT __declspec(dllimport)
#else
    #define BB_PLUGIN_EXPORT __attribute__((visibility("default")))
    #define BB_PLUGIN_IMPORT __attribute__((visibility("default")))
#endif

#define BB_PLUGIN_API_VERSION 1

/**
 * @brief 插件命令信息（用于插件向宿主注册命令）
 */
struct PluginCommandInfo
{
    const char* id;          ///< 命令唯一标识
    const char* label;       ///< 显示名称
    const char* description; ///< 命令描述（可为 nullptr）
    const char* riskLevel;   ///< 风险等级："low" / "medium" / "high"
    const char* riskNote;    ///< 风险说明（可为 nullptr）
};

/**
 * @brief 插件设置项信息（用于插件向宿主注册设置）
 */
struct PluginSettingInfo
{
    const char* key;          ///< 设置键名
    const char* label;        ///< 显示名称
    const char* type;         ///< 类型："text" / "bool" / "number" / "select" / "color"
    const char* defaultValue; ///< 默认值
    const char* options;      ///< select 类型的候选项，以 '|' 分隔（可为 nullptr）
};

/**
 * @brief 插件元数据信息（宿主查询插件信息时使用）
 */
struct PluginMetadata
{
    const char* id;           ///< 插件唯一标识
    const char* name;         ///< 插件显示名称
    const char* version;      ///< 版本号
    const char* author;       ///< 作者
    const char* description;  ///< 描述
    int apiVersion;           ///< SDK API 版本
};

/**
 * @brief 插件状态枚举
 */
enum PluginState
{
    PluginStateUnloaded = 0,  ///< 未加载
    PluginStateLoaded   = 1,  ///< 已加载但未初始化
    PluginStateRunning  = 2,  ///< 运行中（已初始化）
    PluginStateError    = 3   ///< 加载/初始化失败
};

/**
 * @brief 日志级别
 */
enum LogLevel
{
    LogLevelDebug   = 0,
    LogLevelInfo    = 1,
    LogLevelWarning = 2,
    LogLevelError   = 3,
    LogLevelFatal   = 4
};

#endif // BLOCKBOXTYPES_H
