/**
 * @file   ProjectionEditorSettings.h
 * @brief  投影方块编辑器内置设置读写
 * @author BlockBox Team
 * @date   2026-10-16
 *
 * 投影方块编辑器已从「原生插件」升级为启动器内置功能，设置随主程序存储。
 * 为兼容旧版本曾以插件方式配置的用户，读取时自动回退到旧的插件设置
 * （QSettings("BlockBox","Plugins") 的 settings/projection_editor/<key>）。
 */
#pragma once

#include <QString>

class ProjectionEditorSettings
{
public:
    /** 读取内置设置；未设置时回退旧版插件设置，仍无则返回 defaultValue */
    static QString value(const QString &key, const QString &defaultValue = QString());

    /** 写入内置设置并持久化 */
    static void setValue(const QString &key, const QString &value);

    /** 恢复全部默认值（清空内置设置，回退到默认） */
    static void resetToDefaults();

    // ==== 常用默认值 ====
    static QString defaultTool()         { return QStringLiteral("查看"); }
    static QString defaultShowAir()      { return QStringLiteral("false"); }
    static QString defaultUseTextures()  { return QStringLiteral("true"); }
    static QString defaultUseShapes()    { return QStringLiteral("true"); }
};