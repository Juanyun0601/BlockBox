/**
 * @file   NativePluginLoader.cpp
 * @brief  原生插件动态库加载器实现
 * @author BlockBox Team
 * @date   2026-09-08
 */
#include "NativePluginLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QDebug>

#include "sdk/BlockBoxPlugin.h"

NativePluginLoader *NativePluginLoader::instance()
{
    static NativePluginLoader s_instance;
    return &s_instance;
}

NativePluginLoader::NativePluginLoader(QObject *parent)
    : QObject(parent)
{
}

NativePluginLoader::~NativePluginLoader()
{
    unloadAll();
}

bool NativePluginLoader::loadPlugin(const QString &pluginId, const QString &libraryPath,
                                     IBlockBoxHostAPI* hostApi, QString *error)
{
    if (pluginId.isEmpty()) {
        if (error) *error = QStringLiteral("插件 ID 为空");
        return false;
    }
    if (libraryPath.isEmpty()) {
        if (error) *error = QStringLiteral("动态库路径为空");
        return false;
    }
    if (m_instances.contains(pluginId))
        unloadPlugin(pluginId);
    if (!QFileInfo::exists(libraryPath)) {
        const QString errMsg = QStringLiteral("动态库文件不存在: %1").arg(libraryPath);
        if (error) *error = errMsg;
        emit pluginLoadFailed(pluginId, errMsg);
        return false;
    }

    NativePluginInstance inst;
    inst.id = pluginId;
    inst.libraryPath = libraryPath;
    inst.state = 1;

    inst.library = new QLibrary(libraryPath, this);
    inst.library->setLoadHints(QLibrary::PreventUnloadHint | QLibrary::ResolveAllSymbolsHint);
    if (!inst.library->load()) {
        const QString errMsg = QStringLiteral("加载动态库失败: %1\n错误: %2")
                                   .arg(libraryPath, inst.library->errorString());
        inst.state = 3;
        inst.errorMessage = errMsg;
        if (error) *error = errMsg;
        delete inst.library;
        inst.library = nullptr;
        emit pluginLoadFailed(pluginId, errMsg);
        return false;
    }

    inst.createFunc = reinterpret_cast<CreatePluginFunc>(
        inst.library->resolve("createBlockBoxPlugin"));
    inst.destroyFunc = reinterpret_cast<DestroyPluginFunc>(
        inst.library->resolve("destroyBlockBoxPlugin"));
    if (!inst.createFunc || !inst.destroyFunc) {
        const QString errMsg = QStringLiteral("动态库缺少导出函数（createBlockBoxPlugin / destroyBlockBoxPlugin）");
        inst.state = 3;
        inst.errorMessage = errMsg;
        if (error) *error = errMsg;
        inst.library->unload();
        delete inst.library;
        inst.library = nullptr;
        emit pluginLoadFailed(pluginId, errMsg);
        return false;
    }

    auto destroyPlugin = [&inst]() {
        if (!inst.plugin || !inst.destroyFunc)
            return;
        try {
            inst.destroyFunc(inst.plugin);
        } catch (...) {
        }
        inst.plugin = nullptr;
    };
    auto unloadLibrary = [&inst]() {
        if (!inst.library)
            return;
        inst.library->unload();
        delete inst.library;
        inst.library = nullptr;
    };

    try {
        inst.plugin = inst.createFunc();
    } catch (...) {
        const QString errMsg = QStringLiteral("createBlockBoxPlugin() 抛出异常");
        inst.state = 3;
        inst.errorMessage = errMsg;
        if (error) *error = errMsg;
        unloadLibrary();
        emit pluginLoadFailed(pluginId, errMsg);
        return false;
    }
    if (!inst.plugin) {
        const QString errMsg = QStringLiteral("createBlockBoxPlugin() 返回 nullptr");
        inst.state = 3;
        inst.errorMessage = errMsg;
        if (error) *error = errMsg;
        unloadLibrary();
        emit pluginLoadFailed(pluginId, errMsg);
        return false;
    }

    int pluginApiVersion = 0;
    try {
        pluginApiVersion = inst.plugin->apiVersion();
    } catch (...) {
        pluginApiVersion = 0;
    }
    if (pluginApiVersion != BB_PLUGIN_API_VERSION) {
        const QString errMsg = QStringLiteral("插件 SDK API 版本不匹配：插件=%1，宿主=%2")
                                   .arg(pluginApiVersion).arg(BB_PLUGIN_API_VERSION);
        inst.state = 3;
        inst.errorMessage = errMsg;
        if (error) *error = errMsg;
        destroyPlugin();
        unloadLibrary();
        emit pluginLoadFailed(pluginId, errMsg);
        return false;
    }

    bool initialized = false;
    try {
        initialized = inst.plugin->initialize(hostApi);
    } catch (...) {
        initialized = false;
    }
    if (!initialized) {
        const QString errMsg = QStringLiteral("插件 initialize() 初始化失败");
        inst.state = 3;
        inst.errorMessage = errMsg;
        if (error) *error = errMsg;
        try {
            inst.plugin->shutdown();
        } catch (...) {
        }
        destroyPlugin();
        unloadLibrary();
        emit pluginLoadFailed(pluginId, errMsg);
        return false;
    }

    inst.initialized = true;
    inst.state = 2;
    m_instances.insert(pluginId, inst);
    emit pluginLoaded(pluginId);
    return true;
}

void NativePluginLoader::unloadPlugin(const QString &pluginId)
{
    if (!m_instances.contains(pluginId))
        return;

    NativePluginInstance inst = m_instances.take(pluginId);
    if (inst.plugin && inst.initialized) {
        try {
            inst.plugin->shutdown();
        } catch (...) {
        }
    }
    if (inst.plugin && inst.destroyFunc) {
        try {
            inst.destroyFunc(inst.plugin);
        } catch (...) {
        }
    }
    if (inst.library) {
        inst.library->unload();
        delete inst.library;
    }
    emit pluginUnloaded(pluginId);
}

void NativePluginLoader::unloadAll()
{
    const QStringList ids = m_instances.keys();
    for (const QString &id : ids)
        unloadPlugin(id);
}

IBlockBoxPlugin *NativePluginLoader::pluginInstance(const QString &pluginId) const
{
    const auto it = m_instances.constFind(pluginId);
    if (it == m_instances.constEnd())
        return nullptr;
    return it.value().plugin;
}

NativePluginInstance NativePluginLoader::pluginInfo(const QString &pluginId) const
{
    const auto it = m_instances.constFind(pluginId);
    if (it == m_instances.constEnd())
        return NativePluginInstance();
    return it.value();
}

bool NativePluginLoader::isLoaded(const QString &pluginId) const
{
    return m_instances.contains(pluginId);
}

QStringList NativePluginLoader::loadedPluginIds() const
{
    return m_instances.keys();
}
