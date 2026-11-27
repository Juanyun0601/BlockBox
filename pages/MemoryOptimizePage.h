/**
 * @file   MemoryOptimizePage.h
 * @brief  内存优化页 —— 左侧导航「更多 > 内存优化」的落地页
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * 页面只做两件事：
 *   1. 把系统内存 / 启动器占用的现成数字摆出来（进页面与点「刷新」时各采集一次）；
 *   2. 点「立即优化内存」把启动器闲置的工作集还给系统，并把本次释放量写在按钮下方。
 *
 * 这里不做任何危险的系统级操作（不逐个结束后台进程、不改系统设置），
 * 所以游戏运行中随手点一下也是安全的。
 */
#pragma once

#include <QWidget>

class CustomCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;

class MemoryOptimizePage : public QWidget
{
    Q_OBJECT

public:
    explicit MemoryOptimizePage(QWidget *parent = nullptr);

    /** 进入页面 / 点「刷新」时调用：重新采集内存数字并同步「启动前先优化」开关状态 */
    void refresh();

private:
    void buildUi();
    /** 重新读取系统与启动器内存数字，刷新概况区的文字和进度条 */
    void updateMemoryStats();
    /** 立即优化一次内存，刷新数字并把释放量写到结果行 */
    void runOptimize();

    QLabel *m_usageLabel = nullptr;       // 系统内存：已用 / 总量
    QProgressBar *m_usageBar = nullptr;   // 系统内存占用条
    QLabel *m_availableLabel = nullptr;   // 当前可用内存
    QLabel *m_launcherLabel = nullptr;    // 启动器自身占用
    QLabel *m_updatedLabel = nullptr;     // 数字采集时间
    QLabel *m_resultLabel = nullptr;      // 上一次优化结果
    QPushButton *m_optimizeBtn = nullptr; // 立即优化内存
    CustomCheckBox *m_preLaunchCheck = nullptr; // 启动游戏前先优化内存
};
