/**
 * @file   MorePage.h
 * @brief  更多功能页 —— 左侧导航「更多」分类的落地页
 * @author BlockBox Team
 * @date   2026-11-26
 *
 * 侧边栏一级入口放得下的功能有限，剩下这些好用但不好塞进一列的入口收在这里，
 * 和子导航里的分类一一对应（顺序一致）：
 *   方块图书馆 / 方块皮肤站 / 种子地图 / 投影编辑器 / 文件传输 / 多线程下载 / 内存优化 / 迁移向导 / 新手引导
 *
 * 插件也能往这里挂入口（manifest.ui.moreEntries，需声明 ui:page 权限）：
 * 宿主把插件入口排在上面九个内置入口之后，卡片区单独成一块「插件入口」，
 * 与左侧「更多」子导航里追加的项保持同一顺序。
 */
#pragma once

#include <QList>
#include <QWidget>

#include "utils/plugin/PluginInfo.h"

class FlowLayout;

class MorePage : public QWidget
{
    Q_OBJECT

public:
    explicit MorePage(QWidget *parent = nullptr);

    /** 内置入口数量（图书馆 / 皮肤站 / 种子地图 / 投影编辑器 / 文件传输 / 多线程下载 / 内存优化 / 迁移向导 / 新手引导）；
     *  插件入口索引从这里往后排，与左侧「更多」子导航一致 */
    static int builtinEntryCount() { return 9; }

    /**
     * @brief 刷新「插件入口」区
     *
     * 传空列表时整块隐藏（没有插件提供入口 / 插件全部停用）。
     * 插件启用状态或插件列表变化后由宿主动态调用。
     */
    void setPluginEntries(const QList<PluginPageEntry> &entries);

signals:
    /** 点了「方块图书馆」卡片 */
    void libraryRequested();
    /** 点了「方块皮肤站」卡片 */
    void skinStationRequested();
    /** 点了「种子地图」卡片 */
    void seedMapRequested();
    /** 点了「投影编辑器」卡片 */
    void projectionEditorRequested();
    /** 点了「文件传输」卡片 */
    void transferRequested();
    /** 点了「多线程下载」卡片 */
    void multiThreadDownloadRequested();
    /** 点了「内存优化」卡片 */
    void memoryOptimizeRequested();
    /** 迁移向导已关闭（openInstances=true 表示用户点了「查看实例列表」） */
    void migrationFinished(bool openInstances);
    /** 点了「新手引导」卡片 */
    void onboardingRequested();
    /** 点了插件提供的入口（pluginId 为插件 ID，entryId 为入口 ID） */
    void pluginEntryRequested(const QString &pluginId, const QString &entryId);

private:
    void buildUi();
    /** 清空并重建插件入口卡片（按当前条目列表） */
    void rebuildPluginCards();

    QWidget *m_pluginSection = nullptr;  // 「插件入口」标题 + 卡片流的外壳
    QWidget *m_pluginCardsHost = nullptr; // 卡片流的宿主容器
    FlowLayout *m_pluginFlow = nullptr;   // 插件入口卡片流
    QList<PluginPageEntry> m_pluginEntries;
};
