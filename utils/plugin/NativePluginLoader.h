/**
 * @file   NativePluginLoader.h
 * @brief  原生插件动态库加载器
 * @author BlockBox Team
 * @date   2026-09-08
 *
 * 负责加载/卸载插件动态库（.dll / .so / .dylib），
 * 解析导出函数，创建插件实例，并通过宿主 API 调用 initialize()/shutdown()。
 */
#ifndef NATIVEPLUGINLOADER_H
#define NATIVEPLUGINLOADER_H

#include <QHash>
#include <QObject>
#include <QString>

#include "sdk/BlockBoxPlugin.h"

class QLibrary;

/**
 * @brief 已加载原生插件的运行时信息
 */
struct NativePluginInstance
{
    QString id;                         ///< 插件 ID
    QString libraryPath;                ///< 动态库绝对路径
    IBlockBoxPlugin* plugin = nullptr;  ///< 插件实例指针
    QLibrary* library = nullptr;        ///< 动态库句柄
    CreatePluginFunc createFunc = nullptr;   ///< 创建函数指针
    DestroyPluginFunc destroyFunc = nullptr; ///< 销毁函数指针
    bool initialized = false;                ///< initialize() 是否成功
    int state = 0;                      ///< PluginState 枚举
    QString errorMessage;               ///< 加载/初始化失败原因
};

/**
 * @brief 原生插件加载器
 *
 * 单例模式，负责管理所有原生插件动态库的生命周期。
 * 使用 QLibrary 跨平台加载 .dll（Windows）、.so（Linux）、.dylib（macOS）。
 */
class NativePluginLoader : public QObject
{
    Q_OBJECT

public:
    static NativePluginLoader* instance();

    /**
     * @brief 加载原生插件动态库
     * @param pluginId    插件 ID
     * @param libraryPath 动态库绝对路径
     * @param hostApi     宿主 API 表，可为 nullptr
     * @param error       输出参数：失败原因
     * @return true 成功
     */
    bool loadPlugin(const QString &pluginId, const QString &libraryPath,
                    IBlockBoxHostAPI* hostApi = nullptr, QString *error = nullptr);

    /**
     * @brief 卸载原生插件
     * @param pluginId 插件 ID
     */
    void unloadPlugin(const QString &pluginId);

    /**
     * @brief 卸载所有原生插件
     */
    void unloadAll();

    /**
     * @brief 获取已加载的插件实例
     * @param pluginId 插件 ID
     * @return 插件实例指针，未找到返回 nullptr
     */
    IBlockBoxPlugin* pluginInstance(const QString &pluginId) const;

    /**
     * @brief 获取已加载插件的完整信息
     * @param pluginId 插件 ID
     * @return 插件实例信息，未找到返回空结构体
     */
    NativePluginInstance pluginInfo(const QString &pluginId) const;

    /**
     * @brief 检查指定插件是否已加载
     * @param pluginId 插件 ID
     */
    bool isLoaded(const QString &pluginId) const;

    /**
     * @brief 获取所有已加载的插件 ID 列表
     */
    QStringList loadedPluginIds() const;

signals:
    /** 插件加载成功 */
    void pluginLoaded(const QString &pluginId);
    /** 插件卸载 */
    void pluginUnloaded(const QString &pluginId);
    /** 插件加载失败 */
    void pluginLoadFailed(const QString &pluginId, const QString &error);

private:
    explicit NativePluginLoader(QObject *parent = nullptr);
    ~NativePluginLoader() override;

    QHash<QString, NativePluginInstance> m_instances;
};

#endif // NATIVEPLUGINLOADER_H
