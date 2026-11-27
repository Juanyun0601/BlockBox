/**
 * @file   MemoryOptimizePage.h
 * @brief  内存优化页 —— 左侧导航「更多 > 内存优化」的落地页
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * 页面只做两件事：
 *   1. 把系统内存 / 启动器占用的现成数字摆出来（进页面与点「刷新」时各采集一次）；
 *   2. 点「立即优化内存」把闲置内存还给系统，并把本次释放量写在按钮下方。
 *
 * 优化动作放到后台线程里跑（参考 PCL CE：优化整机内存要几百毫秒到几秒，
 * 不能把界面卡住），这里只负责按钮状态与结果文案。
 *
 * 这里不做任何危险的系统级操作（不逐个结束后台进程、不改系统设置），
 * 所以游戏运行中随手点一下也是安全的。
 */
#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include "utils/MemoryOptimizer.h"

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
    /** 立即优化一次内存（后台线程跑，结果回来再刷新界面） */
    void runOptimize();
    /** 后台优化结束：刷新数字、把释放量与明细写到结果区 */
    void onOptimizeFinished();

    QLabel *m_usageLabel = nullptr;       // 系统内存：已用 / 总量
    QProgressBar *m_usageBar = nullptr;   // 系统内存占用条
    QLabel *m_availableLabel = nullptr;   // 当前可用内存
    QLabel *m_launcherLabel = nullptr;    // 启动器自身占用
    QLabel *m_updatedLabel = nullptr;     // 数字采集时间
    QLabel *m_resultLabel = nullptr;      // 上一次优化结果
    QLabel *m_detailLabel = nullptr;      // 上一次优化的明细（收了多少进程 / 系统缓存）
    QPushButton *m_optimizeBtn = nullptr; // 立即优化内存
    CustomCheckBox *m_preLaunchCheck = nullptr; // 启动游戏前先优化内存
    QFutureWatcher<MemoryOptimizeReport> *m_watcher = nullptr; // 后台优化任务
    bool m_optimizing = false;            // 正在优化，防止重复点击
};
