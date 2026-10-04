/**
 * @file   BlockBoxHostAPI.h
 * @brief  BlockBox 原生插件 SDK 宿主 API 接口
 * @author BlockBox Team
 * @date   2026-09-08
 *
 * 本文件定义了宿主（BlockBox 启动器）向插件提供的服务接口。
 * 插件在 initialize() 时接收 IBlockBoxHostAPI 指针，通过它访问宿主功能。
 *
 * 宿主 API 分为以下几类：
 *   - SettingsService:   读写插件设置
 *   - LogService:        日志输出
 *   - UIService:         UI 扩展（添加菜单项、页面等）
 *   - DownloadService:   文件下载
 *   - PluginService:     查询其他插件信息
 *   - CoreService:       核心功能（启动游戏、获取实例路径等）
 */
#ifndef BLOCKBOXHOSTAPI_H
#define BLOCKBOXHOSTAPI_H

#include "BlockBoxTypes.h"

/**
 * @brief 设置服务接口
 *
 * 提供插件设置的持久化读写能力。
 * 设置存储在 QSettings("BlockBox","Plugins") 的 "settings/<pluginId>/<key>" 下。
 */
struct ISettingsService
{
    /**
     * @brief 读取插件设置值
     * @param pluginId  插件 ID
     * @param key       设置键名
     * @param defaultValue 默认值（未设置时返回）
     * @return 设置值，未设置时返回 defaultValue
     */
    const char* (*getSetting)(const char* pluginId, const char* key, const char* defaultValue);

    /**
     * @brief 写入插件设置值
     * @param pluginId  插件 ID
     * @param key       设置键名
     * @param value     设置值
     */
    void (*setSetting)(const char* pluginId, const char* key, const char* value);
};

/**
 * @brief 日志服务接口
 *
 * 插件可通过此接口向宿主日志系统输出日志，
 * 日志会显示在启动器的日志页面中。
 */
struct ILogService
{
    /**
     * @brief 输出日志
     * @param pluginId  插件 ID（用于日志前缀）
     * @param level     日志级别
     * @param message   日志消息
     */
    void (*log)(const char* pluginId, LogLevel level, const char* message);
};

/**
 * @brief UI 服务接口
 *
 * 提供 UI 扩展能力，允许插件向宿主界面添加自定义内容。
 */
struct IUIService
{
    /**
     * @brief 向插件页面添加自定义操作按钮
     * @param pluginId  插件 ID
     * @param buttonId  按钮唯一标识
     * @param label     按钮文字
     * @param iconPath  图标路径（可为 nullptr）
     */
    void (*addActionButton)(const char* pluginId, const char* buttonId,
                            const char* label, const char* iconPath);

    /**
     * @brief 显示通知消息
     * @param title     通知标题
     * @param message   通知内容
     * @param level     通知级别（0=信息, 1=警告, 2=错误）
     */
    void (*showNotification)(const char* title, const char* message, int level);

    /**
     * @brief 在状态栏显示临时消息
     * @param message   状态消息
     * @param timeoutMs 超时时间（毫秒），0 表示持久显示
     */
    void (*showStatusMessage)(const char* message, int timeoutMs);
};

/**
 * @brief 下载服务接口
 *
 * 提供文件下载能力，复用宿主的下载引擎。
 */
struct IDownloadService
{
    /**
     * @brief 下载文件
     * @param url       下载地址
     * @param destPath  本地保存路径
     * @param callback  下载完成回调（可为 nullptr）
     * @param userData  回调用户数据
     * @return 任务 ID（用于取消下载），失败返回 -1
     */
    int (*download)(const char* url, const char* destPath,
                    void (*callback)(int taskId, bool success, const char* error, void* userData),
                    void* userData);

    /**
     * @brief 取消下载
     * @param taskId    任务 ID
     */
    void (*cancelDownload)(int taskId);
};

/**
 * @brief 插件服务接口
 *
 * 提供查询其他插件信息的能力。
 */
struct IPluginService
{
    /**
     * @brief 检查指定插件是否已安装且启用
     * @param pluginId  要检查的插件 ID
     * @return true 已安装且启用
     */
    bool (*isPluginEnabled)(const char* pluginId);

    /**
     * @brief 获取指定插件的版本号
     * @param pluginId  插件 ID
     * @return 版本号字符串，未找到返回 nullptr
     */
    const char* (*getPluginVersion)(const char* pluginId);
};

/**
 * @brief 核心服务接口
 *
 * 提供访问宿主核心功能的能力。
 */
struct ICoreService
{
    /**
     * @brief 获取插件目录路径（BlockBox/ 文件夹）
     * @return 插件目录绝对路径
     */
    const char* (*getPluginsDir)();

    /**
     * @brief 获取应用程序目录路径
     * @return 应用程序目录绝对路径
     */
    const char* (*getAppDir)();

    /**
     * @brief 启动 Minecraft 游戏
     * @param instancePath  实例路径
     * @return true 成功启动
     */
    bool (*launchGame)(const char* instancePath);

    /**
     * @brief 获取当前主题颜色（十六进制字符串，如 "#3B82F6"）
     * @return 主题颜色字符串
     */
    const char* (*getThemeColor)();

    /**
     * @brief 检查是否为浅色主题
     * @return true 浅色主题
     */
    bool (*isLightTheme)();
};

/**
 * @brief 宿主 API 主接口
 *
 * 插件通过此结构体访问所有宿主服务。
 * 宿主在调用插件 initialize() 时传入此结构体指针。
 *
 * 使用示例：
 * @code
 *   bool initialize(IBlockBoxHostAPI* host) {
 *       host->settings->getSetting(m_id, "key", "default");
 *       host->log->log(m_id, LogLevelInfo, "插件已初始化");
 *       return true;
 *   }
 * @endcode
 */
struct IBlockBoxHostAPI
{
    int apiVersion;              ///< API 版本（用于兼容性检查）
    ISettingsService* settings;  ///< 设置服务
    ILogService* log;            ///< 日志服务
    IUIService* ui;              ///< UI 服务
    IDownloadService* download;  ///< 下载服务
    IPluginService* plugin;      ///< 插件服务
    ICoreService* core;          ///< 核心服务
};

#endif // BLOCKBOXHOSTAPI_H
