/**
 * @file   ForumPluginListPage.h
 * @brief  资源页「插件」子页：论坛插件帖聚合成的可安装插件列表
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 数据来自论坛 /api/forum-plugins（启动器专用接口，网页端不提供），
 * 每行就是一个插件帖打包出来的插件卡：图标、名称、版本、下载次数等，
 * 卡片外观与「下载模组」「插件管理」等页的列表卡片一致。
 * 点「安装」走与帖子详情页一致的一键流程（人机验证门禁 → 下载 →
 * 信任确认 → 导入），点「看原帖」跳到原帖看介绍与讨论。
 */
#pragma once

#include <QHash>
#include <QWidget>

#include "utils/ForumContentService.h"

class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QPushButton;
class QComboBox;

class ForumPluginListPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForumPluginListPage(QWidget *parent = nullptr);

    /** 打开页面并从第 1 页重新拉取 */
    void openPage();

signals:
    /** 点了某张插件卡：跳到该插件的原帖 */
    void threadOpenRequested(int threadId);

    /** 从某个插件成功安装：侧边栏插件分组刷新 */
    void pluginInstalled();

    /** 网络失败需要登录/人机验证时提示跳账户页 */
    void loginRequested();

private:
    void buildUi();
    void loadPage(int page);
    void clearList();
    void renderLoaded();
    void installPlugin(const ForumPluginInfo &plugin);
    /** 筛选按钮：按安装状态筛选当前页的插件 */
    void onFilterClicked();
    /** 根据当前生效的筛选刷新按钮文字 / 高亮 */
    void updateFilterButton();
    /** 刷新底部分页栏（本页筛空时也保留，方便翻页继续找） */
    void updatePager();
    /** 插件是否已经装在本地（按清单 id 判断） */
    bool isPluginInstalled(const ForumPluginInfo &plugin) const;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QPushButton *m_filterBtn = nullptr;
    QWidget *m_paginationBar = nullptr;
    QPushButton *m_firstButton = nullptr;
    QPushButton *m_prevButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_lastButton = nullptr;
    QLabel *m_pageLabel = nullptr;

    int m_page = 1;
    int m_totalPages = 1;
    QString m_keyword;   ///< 客户端过滤关键字（本地已拉取的列表里搜）
    QString m_sort = QStringLiteral("latest");   ///< 服务端排序：latest / hot / downloads
    QString m_status;    ///< 客户端安装状态筛选：空 / installed / uninstalled
    bool m_loaded = false;   ///< 当前页数据是否已经拉回来（决定搜索是否本地重渲染）
    QVector<ForumPluginInfo> m_plugins;   ///< 当前页原始数据
    QHash<int, int> m_threadIdByPlugin;   ///< plugin 行 id -> 所属主题 id
};
