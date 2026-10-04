/**
 * @file   InstanceHomePage.h
 * @brief  实例助手 - 首页：当前性能监控 + 游玩信息（当前时间/游玩时间/帧率）
 * @author BlockBox Team
 * @date   2026-07-21
 *
 * 用于 InstanceAssistantWindow 的「首页」标签页。窗口宽度较窄（450px），
 * 因此采用单列垂直卡片列表，分两个区块：
 *  - 游玩信息：当前时间、游玩时间（基于 javaw.exe 进程创建时间）、帧率
 *  - 性能监控：CPU / 内存 / GPU 实时占用率，复用 PerfMonitorCard + HardwareMonitor
 *
 * 帧率通过 DWM 合成计时信息估算：当游戏窗口处于活动状态时，桌面合成器
 * 每秒合成的帧数可作为窗口更新频率的近似参考。
 */
#ifndef INSTANCEHOMEPAGE_H
#define INSTANCEHOMEPAGE_H

#include <QLabel>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class HardwareMonitor;
class PerfMonitorCard;
class QVBoxLayout;

class InstanceHomePage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceHomePage(QWidget *parent = nullptr);
    ~InstanceHomePage();

    /** 设置当前实例路径，用于后续日志/资源定位 */
    void setInstancePath(const QString &path);

private slots:
    /** 每秒触发：刷新当前时间、游玩时间、帧率与游戏状态 */
    void onSecondTick();
    /** 硬件监控数据刷新（CPU/内存/GPU） */
    void onHardwareDataRefreshed();

private:
    void initUI();
    void applyThemeStyles();

    /** 检测 javaw.exe 进程，返回启动时间（ms since epoch，未检测到返回 0） */
    qint64 detectGameStartTime();
    /**
     * @brief 深度检测一次运行中的游戏并缓存 PID 与启动时间
     *
     * 会调用 GameDetector::detectRunningGame()（内部启动 PowerShell，耗时），
     * 只应在「无游戏 → 有游戏」状态跳变时调用，禁止在每秒 tick 中调用。
     */
    void refreshGameDetection();
    /** 查找运行中游戏窗口句柄（未找到返回 nullptr） */
#ifdef Q_OS_WIN
    HWND findGameWindow();
#endif
    /** 估算游戏窗口帧率（DWM 合成帧数增量） */
    int queryGameFps();

    // ---- 游玩信息控件 ----
    QLabel *m_currentTimeValue;    ///< 当前时间数值
    QLabel *m_playTimeValue;       ///< 游玩时间数值
    QLabel *m_fpsValue;            ///< 帧率数值

    // ---- 性能监控卡片 ----
    PerfMonitorCard *m_cpuCard;
    PerfMonitorCard *m_memoryCard;
    PerfMonitorCard *m_gpuCard;
    HardwareMonitor *m_hardwareMonitor;

    // ---- 定时器与状态 ----
    QTimer *m_tickTimer;           ///< 1 秒刷新定时器
    QString m_instancePath;        ///< 当前实例路径
    qint64 m_gameStartTimeMs;      ///< 游戏进程启动时间（毫秒，UTC epoch）
    bool m_gameRunning = false;    ///< 上一次 tick 检测到的游戏运行状态（用于状态跳变检测）
    qint64 m_cachedGamePid = 0;    ///< 缓存的游戏进程 PID（深度检测结果，避免每秒查 PowerShell）

#ifdef Q_OS_WIN
    HWND m_gameWnd;                ///< 上次缓存的游戏窗口句柄
    quint64 m_lastDwmFrame;        ///< 上次查询到的 DWM 帧计数
    qint64 m_lastDwmQueryMs;       ///< 上次查询 DWM 的时间戳（用于计算增量）
#endif
};

#endif // INSTANCEHOMEPAGE_H
