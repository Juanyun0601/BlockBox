/**
 * @file   CreationCenterPage.h
 * @brief  创作中心：集中管理自己发布的作品
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 从账户管理页资料卡进入（和账户页同一个顶层页面，返回键回到账户总览）。
 * 「作品」= 自己在论坛发布的主题：普通帖与挂了插件包 / 投影文件的作品帖都算。
 * 进入时把作品一次拉齐（上限内），之后翻页 / 类型筛选 / 标题标签搜索都在本地完成，
 * 汇总卡的数字就是全部作品的真实统计。每行带数据与编辑 / 详情 / 删除操作，
 * 也可以点开作品详情做版本管理。
 */
#pragma once

#include <QWidget>

#include "components/ForumKit.h"
#include "pages/CreationWorkDetailPage.h"
#include "utils/ForumContentService.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

class CreationCenterPage : public QWidget
{
    Q_OBJECT

public:
    explicit CreationCenterPage(QWidget *parent = nullptr);

    /** 进入创作中心：回到第一页重新拉取（数据以当前登录账号为准） */
    void open();

    /** 列表点开一个作品：切到创作中心内部的作品详情子页 */
    void openWork(const ForumThreadInfo &thread);

    /**
     * @brief 顶栏返回键：作品详情子页先退回作品列表
     * @return 是否在作品详情里（true 表示这次返回被本页消化）
     */
    bool returnToWorkList();

signals:
    /** 点击某个作品 / 「详情」：在启动器内打开作品详情 */
    void threadOpenRequested(int threadId);

    /** 还没有作品时点「去发布」：打开发布中心 */
    void publishRequested();

private:
    /** 作品类型筛选（投影 = 挂了投影文件的作品帖） */
    enum class WorkFilter
    {
        All = 0,
        Plugin = 1,
        Schematic = 2,
        Plain = 3,
    };

    void buildUi();
    void showWorkList();
    void loadAllWorks();
    void fetchPage(int userId, int page);
    void finishLoad();
    void setFilter(WorkFilter filter);
    void applyFilter();
    void renderCurrentPage();
    void editWork(const ForumThreadInfo &thread);
    void deleteWork(const ForumThreadInfo &thread);
    void setStatus(const QString &text, bool error = false);
    void updateSummary();
    void updateFilterChips();
    static bool matchesFilter(const ForumThreadInfo &thread, WorkFilter filter);

    QVector<ForumThreadInfo> m_allWorks;   ///< 进入时拉齐的全部作品（按发布时间从新到旧）
    QVector<ForumThreadInfo> m_filtered;   ///< 类型筛选 + 关键词过滤后要展示的作品
    WorkFilter m_filter = WorkFilter::All;
    QString m_keyword;
    int m_page = 1;
    int m_totalPages = 1;
    bool m_loading = false;
    bool m_partial = false;   ///< 作品超出自动拉取上限 / 中途有页失败，只显示已加载的部分

    /** 本地每页条数（和服务端分页大小一致）与进入时最多自动连拉的页数 */
    static constexpr int kPageSize = 20;
    static constexpr int kMaxAutoPages = 10;

    QScrollArea *m_scroll = nullptr;
    /** 创作中心内部两层：作品列表 / 作品详情（详情不是独立页面，返回键由本页消化） */
    QStackedWidget *m_viewStack = nullptr;
    QWidget *m_listPage = nullptr;
    CreationWorkDetailPage *m_workDetail = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QLabel *m_statWorks = nullptr;
    QLabel *m_statViews = nullptr;
    QLabel *m_statReplies = nullptr;
    QLabel *m_statDownloads = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QWidget *m_filterRow = nullptr;          ///< 类型筛选胶囊行（未登录时隐藏）
    QPushButton *m_filterChips[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *m_countLabel = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    ForumKit::ForumPager *m_pager = nullptr;
};
