/**
 * 探针用 GameLauncher 桩：WorldMapPage 的玩家标记轮询只引用
 * isGameRunning()，探针环境没有真实启动器，恒返回 false
 * （玩家标记外观与朝向走 debugShowPlayerMarker 钩子验证）。
 */
#ifndef GAMELAUNCHER_H
#define GAMELAUNCHER_H

#include <QString>

class GameLauncher
{
public:
    static GameLauncher *instance()
    {
        static GameLauncher m;
        return &m;
    }

    bool isGameRunning() { return false; }
    QString currentInstancePath() const { return QString(); }
};

#endif // GAMELAUNCHER_H
