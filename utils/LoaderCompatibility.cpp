/**
 * @file   LoaderCompatibility.cpp
 * @brief  加载器兼容性检查实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "LoaderCompatibility.h"

LoaderCompatibility* LoaderCompatibility::m_instance = nullptr;

LoaderCompatibility::LoaderCompatibility()
{
    initCompatibilityRules();
    initDependencyRules();
}

LoaderCompatibility* LoaderCompatibility::instance()
{
    if (!m_instance) {
        m_instance = new LoaderCompatibility();
    }
    return m_instance;
}

void LoaderCompatibility::initCompatibilityRules()
{
    m_incompatibleMap["Forge"] = {"Fabric", "Quilt", "NeoForge", "Cleanroom", "LiteLoader"};
    m_incompatibleMap["Fabric"] = {"Forge", "NeoForge", "Cleanroom", "LiteLoader", "OptiFine"};
    m_incompatibleMap["Quilt"] = {"Forge", "NeoForge", "Cleanroom", "LiteLoader", "OptiFine"};
    m_incompatibleMap["NeoForge"] = {"Forge", "Fabric", "Quilt", "Cleanroom", "LiteLoader", "OptiFine"};
    m_incompatibleMap["Cleanroom"] = {"Forge", "Fabric", "Quilt", "NeoForge", "LiteLoader", "OptiFine"};
    m_incompatibleMap["LiteLoader"] = {"Fabric", "Quilt", "NeoForge", "Cleanroom"};
    m_incompatibleMap["OptiFine"] = {"Fabric", "Quilt", "NeoForge", "Cleanroom"};
    m_incompatibleMap["OptiFabric"] = {"Forge", "NeoForge", "Cleanroom", "LiteLoader"};
    m_incompatibleMap["LegacyFabric"] = {"Forge", "NeoForge", "Cleanroom", "LiteLoader"};
}

void LoaderCompatibility::initDependencyRules()
{
    m_dependencyMap["OptiFabric"] = {"Fabric"};
    m_dependencyMap["Fabric API"] = {"Fabric"};
    m_dependencyMap["Quilt API"] = {"Quilt"};
}

LoaderCompatibilityResult LoaderCompatibility::checkCompatibility(const QString& loaderName, const QStringList& installedLoaders) const
{
    LoaderCompatibilityResult result;
    result.canInstall = true;
    result.hasConflict = false;
    result.needsDependency = false;

    QSet<QString> incompatibleLoaders = m_incompatibleMap.value(loaderName);
    for (const QString& installed : installedLoaders) {
        if (incompatibleLoaders.contains(installed)) {
            result.canInstall = false;
            result.hasConflict = true;
            result.conflictLoaderName = installed;
            result.message = QString("%1 与已安装的 %2 不兼容，无法同时安装").arg(loaderName).arg(installed);
            return result;
        }
    }

    QStringList requiredDeps = m_dependencyMap.value(loaderName);
    for (const QString& dep : requiredDeps) {
        if (!installedLoaders.contains(dep)) {
            result.needsDependency = true;
            result.dependencyLoaderName = dep;
            result.message = QString("%1 需要先安装 %2").arg(loaderName).arg(dep);
            return result;
        }
    }

    result.message = QString("可以安装 %1").arg(loaderName);
    return result;
}

QStringList LoaderCompatibility::getIncompatibleLoaders(const QString& loaderName) const
{
    QStringList list;
    QSet<QString> set = m_incompatibleMap.value(loaderName);
    for (const QString& item : set) {
        list.append(item);
    }
    return list;
}

QStringList LoaderCompatibility::getRequiredDependencies(const QString& loaderName) const
{
    return m_dependencyMap.value(loaderName);
}

bool LoaderCompatibility::hasConflict(const QString& loader1, const QString& loader2) const
{
    return m_incompatibleMap.value(loader1).contains(loader2) ||
           m_incompatibleMap.value(loader2).contains(loader1);
}
