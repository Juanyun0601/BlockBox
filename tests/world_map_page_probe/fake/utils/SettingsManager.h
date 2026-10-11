/**
 * 探针用 SettingsManager 桩：只提供 WorldMapPage / SettingsGeneralPage
 * 链接时真正引用到的接口（地图结构图标风格），避免拉入 TranslationService
 * 等依赖链。持久化用进程内静态变量（探针不依赖真实 QSettings）。
 */
#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>

/** 存档地图结构标记图标风格（与真 SettingsManager 保持一致） */
enum class MapStructureIconStyle {
    Icon = 0,
    Overview = 1
};

class SettingsManager : public QObject
{
    Q_OBJECT
public:
    static SettingsManager *instance()
    {
        static SettingsManager m;
        return &m;
    }

    MapStructureIconStyle getMapStructureIconStyle() const
    {
        return m_mapIconStyle;
    }
    void setMapStructureIconStyle(MapStructureIconStyle style)
    {
        m_mapIconStyle = style;
    }

private:
    MapStructureIconStyle m_mapIconStyle = MapStructureIconStyle::Icon;
};

#endif // SETTINGSMANAGER_H
