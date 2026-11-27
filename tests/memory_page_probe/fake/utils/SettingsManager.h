/**
 * 探针用 SettingsManager 桩：只提供 MemoryOptimizePage 用到的
 * 「启动游戏前先优化内存」开关，持久化用进程内静态变量。
 */
#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>

class SettingsManager : public QObject
{
    Q_OBJECT
public:
    static SettingsManager *instance()
    {
        static SettingsManager m;
        return &m;
    }

    bool isOptimizeMemoryBeforeLaunchEnabled() const { return m_optimizeBeforeLaunch; }
    void setOptimizeMemoryBeforeLaunchEnabled(bool enabled)
    {
        if (m_optimizeBeforeLaunch == enabled)
            return;
        m_optimizeBeforeLaunch = enabled;
        emit optimizeMemoryBeforeLaunchChanged(enabled);
    }

signals:
    void optimizeMemoryBeforeLaunchChanged(bool enabled);

private:
    bool m_optimizeBeforeLaunch = false;
};

#endif // SETTINGSMANAGER_H
