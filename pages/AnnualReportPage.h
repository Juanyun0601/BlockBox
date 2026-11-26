/**
 * @file   AnnualReportPage.h
 * @brief  「我的 MC 年度报告」页：一整年的游玩数据 + 一键生成竖版分享海报
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 页面自上而下：年份胶囊 → 账号提示卡（未登录才显示）→ 主视觉
 * 「2026 年，你在方块里待了 X 小时」→ 8 张数据小卡 → 12 个月柱状图。
 *
 * 数据来源：没登录时读本机 annualReport(year)；登录后走 fetchAnnualReport(year)
 * 拿账号云端汇总（含其他电脑上的时长、含成就数），拉取失败退回本机数据并标注
 * 「本机数据」，不会让页面空着。
 *
 * 样式全部走 StyleKit + 主题令牌，颜色不硬编码；亮 / 暗主题切换后重刷。
 */
#pragma once

#include <QVector>
#include <QWidget>

#include "utils/PlaytimeStats.h"

class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;

/** 12 个月迷你柱状图（本页自绘，不依赖 QtCharts） */
class AnnualReportMonthChart;

class AnnualReportPage : public QWidget
{
    Q_OBJECT

public:
    explicit AnnualReportPage(QWidget *parent = nullptr);

    /**
     * @brief 进入页面时调用：选定年份并立刻刷新
     * @param year 年份，<= 0 表示今年
     */
    void openPage(int year = 0);

public slots:
    /** 重读数据：按当前选中的年份重新取（已登录优先云端） */
    void refresh();

signals:
    /** 未登录点「登录并绑定」：请求跳到登录页 */
    void loginRequested();

private:
    /** 一张数据小卡：小标题 + 大数字 / 名字 + 补充说明 */
    struct StatCard
    {
        QFrame *card = nullptr;
        QLabel *title = nullptr;
        QLabel *value = nullptr;
        QLabel *hint = nullptr;
    };

    static constexpr int kStatCardCount = 8;

    void buildUi();
    void applyStyle();
    /** 按 availableYears() + 今年重建年份胶囊（当前年份保持点亮） */
    void rebuildYearPills();
    /** 未登录显示提示卡，已登录隐藏 */
    void updateAccountCard();
    /** 把 m_report 画到页面上（主视觉 + 8 张小卡 + 柱状图） */
    void renderReport();
    void renderHero();
    void renderStatCards();
    void renderChart();

    void selectYear(int year);
    /** 取某年的数据：已登录走云端，失败 / 未登录用本机 */
    void loadReport(int year);
    void applyCloudReport(const PlaytimeAnnualReport &report, int requestedYear);
    void onCloudFailed(int requestedYear);
    void generatePoster();
    /** 海报上的数据来源小字 */
    QString posterSubtitle() const;
    /** 当前登录账号的昵称（没登录返回空） */
    static QString loggedInDisplayName();

    int m_year = 0;                  ///< 当前选中的年份（0 = 还没选，按今年）
    PlaytimeAnnualReport m_report;   ///< 当前展示的这一年的数据
    bool m_fromCloud = false;        ///< 当前数据来自账号云端
    bool m_cloudFailed = false;      ///< 云端拉取失败，正在显示本机兜底数据
    bool m_fetching = false;         ///< 正在拉云端年度报告

    QWidget *m_yearHost = nullptr;
    QHBoxLayout *m_yearLayout = nullptr;
    QVector<QPushButton *> m_yearButtons;

    QFrame *m_accountCard = nullptr;
    QLabel *m_accountTitle = nullptr;
    QLabel *m_accountHint = nullptr;
    QPushButton *m_accountButton = nullptr;

    QFrame *m_heroCard = nullptr;
    QLabel *m_heroTitle = nullptr;
    QLabel *m_heroValue = nullptr;
    QLabel *m_heroSub = nullptr;
    QLabel *m_sourceChip = nullptr;
    QPushButton *m_posterButton = nullptr;

    StatCard m_statCards[kStatCardCount];

    QFrame *m_chartCard = nullptr;
    QLabel *m_chartTitle = nullptr;
    QLabel *m_chartUnit = nullptr;
    AnnualReportMonthChart *m_chart = nullptr;
};
