/**
 * 探针用 GameDetector 桩：不做 Windows 进程枚举，恒返回 false。
 */
#ifndef GAMEDETECTOR_H
#define GAMEDETECTOR_H

class GameDetector
{
public:
    static bool isJavaGameRunning() { return false; }
};

#endif // GAMEDETECTOR_H
