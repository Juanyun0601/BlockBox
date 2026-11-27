/**
 * @file   MemoryOptimizer.h
 * @brief  运行内存优化引擎：把闲置的工作集与系统待机缓存还给系统
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 实现思路参考 PCL CE（Plain Craft Launcher 社区版）的内存优化：
 *   - 优化动作放在独立线程/独立进程里跑，界面不卡；
 *   - 不只收启动器自己的内存，也收后台程序闲置的那部分（PCL 是整机扫一遍）；
 *   - 顺带清理系统待机缓存（Windows 上这一步「可用内存」涨得最多），需要管理员；
 *   - 优化量按「系统可用物理内存」的前后差值统计，和 PCL 的统计口径一致。
 *
 * 与 PCL 的差异（有意为之）：
 *   - 不结束任何进程，只收工作集；
 *   - 跳过 java.exe / javaw.exe（正在跑的游戏）与前台窗口，游戏开着时点也安全；
 *   - 不需要管理员也能用，只是少了「清理系统缓存」那一步。
 */

#pragma once

#include <QString>
#include <QtGlobal>

/**
 * @brief 一次内存优化的结果
 *
 * 数字统一用字节；读不到时留 -1，展示层据此决定是否显示。
 */
struct MemoryOptimizeReport
{
    bool supported = false;            // 当前平台是否支持（非 Windows 恒为 false）
    bool elevated = false;             // 是否以管理员权限运行
    bool systemCachePurged = false;    // 系统待机缓存是否真的清理成功
    int processesTrimmed = 0;          // 成功回收闲置工作集的进程数
    int processesSkipped = 0;          // 因权限 / 体积 / 是游戏进程而跳过的进程数
    qint64 availableBeforeBytes = -1;  // 优化前系统可用物理内存
    qint64 availableAfterBytes = -1;   // 优化后系统可用物理内存
    qint64 launcherBeforeBytes = -1;   // 优化前启动器自身工作集
    qint64 launcherAfterBytes = -1;    // 优化后启动器自身工作集

    /** 启动器自身收回去的内存（工作集差值，至少 0） */
    qint64 launcherFreedBytes() const
    {
        if (launcherBeforeBytes <= 0 || launcherAfterBytes < 0)
            return 0;
        const qint64 delta = launcherBeforeBytes - launcherAfterBytes;
        return delta > 0 ? delta : 0;
    }

    /** 系统可用物理内存的增量（PCL 用它做统计口径，至少 0） */
    qint64 systemFreedBytes() const
    {
        if (availableBeforeBytes <= 0 || availableAfterBytes <= 0)
            return 0;
        const qint64 delta = availableAfterBytes - availableBeforeBytes;
        return delta > 0 ? delta : 0;
    }

    /** 展示用的「本次释放」：优先用系统口径，没有再退回启动器自己的收量 */
    qint64 headlineFreedBytes() const
    {
        const qint64 system = systemFreedBytes();
        return system > 0 ? system : launcherFreedBytes();
    }

    /** 这一次优化是不是白跑了（既没清缓存，也没收到内存） */
    bool isNoOp() const
    {
        return headlineFreedBytes() <= 0 && !systemCachePurged && processesTrimmed <= 0;
    }
};

/**
 * @brief 运行内存优化引擎
 *
 * 所有接口都只做内存回收，不结束进程、不改系统设置。
 * optimize() 会阻塞几百毫秒，页面与启动流程都应放到后台线程里调。
 */
class MemoryOptimizer
{
public:
    /** 当前进程是否以管理员身份运行（非 Windows 恒为 false） */
    static bool isElevated();

    /**
     * @brief 执行一次内存优化
     *
     * @param trimOtherProcesses 是否顺带回收后台进程的闲置工作集
     * @param purgeSystemCache   是否尝试清理系统待机缓存（需要管理员，失败自动跳过）
     * @return 本次优化的明细结果
     */
    static MemoryOptimizeReport optimize(bool trimOtherProcesses = true,
                                         bool purgeSystemCache = true);
};
