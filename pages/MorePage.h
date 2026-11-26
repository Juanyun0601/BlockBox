/**
 * @file   MorePage.h
 * @brief  更多功能页 —— 左侧导航「更多」分类的落地页
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * 侧边栏一级入口放得下的功能有限，剩下这些好用但不好塞进一列的入口收在这里，
 * 和子导航里的分类一一对应：
 *   社区与联机：联机大厅 / 皮肤站 / 方块图书馆
 *   我的与帮助：任务中心 / 年度报告 / 账户管理 / 新手引导
 */
#pragma once

#include <QWidget>

class MorePage : public QWidget
{
    Q_OBJECT

public:
    explicit MorePage(QWidget *parent = nullptr);

signals:
    /** 点了「联机大厅」卡片 */
    void lobbyRequested();
    /** 点了「皮肤站」卡片 */
    void skinStationRequested();
    /** 点了「方块图书馆」卡片 */
    void libraryRequested();
    /** 点了「任务中心」卡片 */
    void taskCenterRequested();
    /** 点了「年度报告」卡片 */
    void annualReportRequested();
    /** 点了「账户管理」卡片 */
    void accountRequested();
    /** 点了「新手引导」卡片 */
    void onboardingRequested();

private:
    void buildUi();
};
