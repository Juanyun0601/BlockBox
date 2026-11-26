/**
 * @file   PlaytimeStatsPage.h
 * @brief  游玩时长统计页：总览大数字、最近 14 天柱状图、年度报告入口、实例排行、账号绑定与最近会话
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 数据全部来自 PlaytimeStatsService（本机统计 + 账号云端），页面只负责展示：
 *   1. 概览卡：累计 / 今日 / 近 7 天 / 累计启动 / 游玩天数；
 *   2. 每日柱状图：最近 14 天，自绘（不引 QtCharts），悬停显示日期 + 时长；
 *   3. 年度报告入口卡：点进去看「我的 MC 年度报告」；
 *   4. 实例排行榜：按累计时长取前 5 名，进度条按第一名归一；
 *   5. 账号绑定卡：未登录引导绑定，已登录显示待同步 / 最近同步 / 云端累计；
 *   6. 最近会话：最近 8 条，没有数据时给空态提示。
 *
 * 页面大标题由顶栏负责，页内只用 sectionTitle / cardTitle；颜色一律走
 * StyleKit 与主题令牌，自绘部分取 ThemeManager 的当前颜色，跟随主题切换。
 */
#pragma once

#include <QVector>
#include <QWidget>

#include "utils/PlaytimeStats.h"

class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;

class PlaytimeStatsPage : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 统计页
     * @param embedded true = 嵌在别的页面里（账户管理页「游玩」页签）：不自带滚动区、
     *                 内边距更紧，滚动交给外层页面；false = 独立整页（侧边栏入口的形态）
     */
    explicit PlaytimeStatsPage(QWidget *parent = nullptr, bool embedded = false);

public slots:
    /** 重读本机统计并重绘（进页面 / 顶栏刷新时调用）；已登录且云端数据过期时顺手拉一次 */
    void refresh();

signals:
    /** 未登录时点了「登录并绑定」 */
    void loginRequested();
    /** 点了年度报告入口卡；year = 0 表示今年 */
    void annualReportRequested(int year);

private:
    void buildUi();

    /** 主题或主题色变化后重刷所有内联样式与自绘颜色 */
    void applyThemeStyles();

    /** 主题变化：先重刷固定控件样式，再重建动态行（不联网） */
    void onThemeChanged();

    void renderOverview();
    void renderRanking();
    void renderAccount();
    void renderSessions();

    /** 本机数据变化后整体重绘（只读内存里的统计，不联网） */
    void renderAll();

    // ── 概览卡 ──
    QFrame *m_overviewCard = nullptr;
    QLabel *m_overviewTitle = nullptr;
    QLabel *m_overviewSummary = nullptr;
    QVector<QLabel *> m_statValues;  ///< 累计 / 今日 / 近 7 天 / 累计启动 / 游玩天数
    QVector<QLabel *> m_statCaptions;

    // ── 每日柱状图 ──
    QFrame *m_chartCard = nullptr;
    QLabel *m_chartTitle = nullptr;
    QLabel *m_chartLegend = nullptr;
    QWidget *m_chart = nullptr;  ///< 内部自绘控件（DayBarChart，定义在 .cpp）

    // ── 年度报告入口卡 ──
    QFrame *m_reportCard = nullptr;
    QLabel *m_reportTitle = nullptr;
    QLabel *m_reportSubtitle = nullptr;
    QPushButton *m_reportButton = nullptr;

    // ── 实例排行榜 ──
    QFrame *m_rankCard = nullptr;
    QLabel *m_rankTitle = nullptr;
    QLabel *m_rankHint = nullptr;
    QVBoxLayout *m_rankLayout = nullptr;

    // ── 账号绑定卡（未登录 / 已登录两态整体重建） ──
    QFrame *m_accountCard = nullptr;
    QVBoxLayout *m_accountLayout = nullptr;

    // ── 最近会话 ──
    QFrame *m_sessionCard = nullptr;
    QLabel *m_sessionTitle = nullptr;
    QLabel *m_sessionHint = nullptr;
    QVBoxLayout *m_sessionLayout = nullptr;

    /** 嵌在账户管理页里时不自带滚动区（见构造函数注释） */
    bool m_embedded = false;

    /** 今年本机年度数据（概览副标题取「最常开玩的时段」） */
    PlaytimeAnnualReport m_yearReport;
};
