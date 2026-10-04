/**
 * @file   BlockBoxPlugin.h
 * @brief  BlockBox 原生插件 SDK 插件接口基类
 * @author BlockBox Team
 * @date   2026-09-08
 *
 * 本文件定义了原生插件必须实现的接口。插件开发者只需包含此头文件，
 * 继承 IBlockBoxPlugin 并实现所有纯虚函数即可。
 *
 * 插件动态库必须导出以下两个 C 函数：
 *   - createBlockBoxPlugin()  : 创建插件实例
 *   - destroyBlockBoxPlugin() : 销毁插件实例
 *
 * 示例：
 * @code
 *   class MyPlugin : public IBlockBoxPlugin {
 *   public:
 *       const char* name() const override { return "My Plugin"; }
 *       // ... 实现其他接口方法 ...
 *   };
 *
 *   extern "C" {
 *       BB_PLUGIN_EXPORT IBlockBoxPlugin* createBlockBoxPlugin() {
 *           return new MyPlugin();
 *       }
 *       BB_PLUGIN_EXPORT void destroyBlockBoxPlugin(IBlockBoxPlugin* p) {
 *           delete p;
 *       }
 *   }
 * @endcode
 */
#ifndef BLOCKBOXPLUGIN_H
#define BLOCKBOXPLUGIN_H

#include "BlockBoxTypes.h"
#include "BlockBoxHostAPI.h"

/**
 * @brief 原生插件接口基类
 *
 * 所有原生插件必须继承此类并实现所有纯虚函数。
 * 宿主通过 createBlockBoxPlugin() 导出函数获取插件实例，
 * 然后调用 initialize()、shutdown() 等方法管理生命周期。
 */
class IBlockBoxPlugin
{
public:
    virtual ~IBlockBoxPlugin() = default;

    // ==================== 元数据 ====================

    /** 插件唯一标识（英文/数字/下划线） */
    virtual const char* name() const = 0;

    /** 插件版本号（语义化版本） */
    virtual const char* version() const = 0;

    /** 插件作者 */
    virtual const char* author() const = 0;

    /** 插件描述 */
    virtual const char* description() const = 0;

    /** SDK API 版本（应返回 BB_PLUGIN_API_VERSION） */
    virtual int apiVersion() const { return BB_PLUGIN_API_VERSION; }

    // ==================== 生命周期 ====================

    /**
     * @brief 插件初始化
     *
     * 宿主在加载插件后调用此方法。插件应在此处完成初始化工作，
     * 如注册命令、读取设置、连接信号等。
     *
     * @param host 宿主 API 指针（插件应保存此指针以供后续使用）
     * @return true 初始化成功，false 初始化失败（宿主将卸载插件）
     */
    virtual bool initialize(IBlockBoxHostAPI* host) = 0;

    /**
     * @brief 插件关闭
     *
     * 宿主在卸载插件前调用此方法。插件应在此处释放资源、保存状态等。
     */
    virtual void shutdown() = 0;

    // ==================== 命令系统 ====================

    /**
     * @brief 获取插件提供的命令数量
     * @return 命令数量
     */
    virtual int commandCount() const = 0;

    /**
     * @brief 获取指定索引的命令信息
     * @param index 命令索引（0 到 commandCount()-1）
     * @return 命令信息结构体
     */
    virtual PluginCommandInfo command(int index) const = 0;

    /**
     * @brief 执行插件命令
     *
     * 当用户点击插件的命令按钮时，宿主调用此方法。
     *
     * @param cmdId    命令 ID（与 command() 返回的 id 匹配）
     * @param args     命令参数数组（键值对，如 ["key1", "value1", "key2", "value2"]）
     * @param argCount 参数数量（总是偶数）
     * @return true 执行成功，false 执行失败
     */
    virtual bool executeCommand(const char* cmdId, const char** args, int argCount) = 0;

    // ==================== 设置系统 ====================

    /**
     * @brief 获取插件提供的设置项数量
     * @return 设置项数量（返回 0 表示无设置项）
     */
    virtual int settingCount() const { return 0; }

    /**
     * @brief 获取指定索引的设置项信息
     * @param index 设置项索引
     * @return 设置项信息结构体
     */
    virtual PluginSettingInfo setting(int index) const
    {
        PluginSettingInfo empty = {};
        return empty;
    }
};

/**
 * @brief 插件动态库导出函数类型定义
 *
 * 每个原生插件 DLL 必须导出这两个函数。
 */
typedef IBlockBoxPlugin* (*CreatePluginFunc)();
typedef void (*DestroyPluginFunc)(IBlockBoxPlugin*);

#endif // BLOCKBOXPLUGIN_H
