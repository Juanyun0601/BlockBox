/**
 * @file   ProjectionEditorSettings.cpp
 * @brief  投影方块编辑器内置设置读写实现
 * @author BlockBox Team
 * @date   2026-10-16
 */
#include "ProjectionEditorSettings.h"

#include <QSettings>

namespace {

const QString kNewOrg   = QStringLiteral("BlockBox");
const QString kNewApp   = QStringLiteral("BlockBox");
const QString kNewRoot  = QStringLiteral("projectionEditor");

// 旧版插件设置的存储位置（PluginManager：QSettings("BlockBox","Plugins")，
// 键为 settings/projection_editor/<key>）。仅用于兼容迁移读取。
const QString kLegacyOrg    = QStringLiteral("BlockBox");
const QString kLegacyApp    = QStringLiteral("Plugins");
const QString kLegacyRoot   = QStringLiteral("settings/projection_editor");

} // namespace

QString ProjectionEditorSettings::value(const QString &key, const QString &defaultValue)
{
    QSettings current(kNewOrg, kNewApp);
    const QString newPath = kNewRoot + QLatin1Char('/') + key;
    if (current.contains(newPath))
        return current.value(newPath).toString();

    // 旧版曾以插件方式配置过：沿用旧值，保证升级不丢配置
    QSettings legacy(kLegacyOrg, kLegacyApp);
    const QString legacyPath = kLegacyRoot + QLatin1Char('/') + key;
    if (legacy.contains(legacyPath))
        return legacy.value(legacyPath).toString();

    return defaultValue;
}

void ProjectionEditorSettings::setValue(const QString &key, const QString &value)
{
    QSettings settings(kNewOrg, kNewApp);
    settings.setValue(kNewRoot + QLatin1Char('/') + key, value);
    settings.sync();
}

void ProjectionEditorSettings::resetToDefaults()
{
    QSettings current(kNewOrg, kNewApp);
    current.remove(kNewRoot);
    current.sync();

    // 同步清掉旧版插件设置，避免回退时读到残留旧值
    QSettings legacy(kLegacyOrg, kLegacyApp);
    legacy.remove(kLegacyRoot);
    legacy.sync();
}