/**
 * @file   WorldMapSignalBridge.cpp
 * @brief  存档地图模块的跨模块信号转发实现
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "WorldMapSignalBridge.h"

#include "utils/SettingsManager.h"

namespace WorldMapSignalBridge {

QMetaObject::Connection connectMap3dChanged(QObject *context,
                                            std::function<void(bool)> handler)
{
    if (!context || !handler)
        return QMetaObject::Connection();
    return QObject::connect(SettingsManager::instance(),
                            &SettingsManager::experimentalMap3dChanged,
                            context,
                            [handler](bool enabled) { handler(enabled); });
}

} // namespace WorldMapSignalBridge
