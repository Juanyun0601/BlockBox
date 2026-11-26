/**
 * @file   PlaytimeStatsPage.cpp
 * @brief  游玩时长统计页实现：总览大数字、最近 14 天自绘柱状图、年度报告入口、实例排行、账号绑定、最近会话
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 页面只读 PlaytimeStatsService 的公开接口，不直接碰统计文件；
 * 颜色全部走 StyleKit 的 @TOKEN@ 与 ThemeManager 的当前颜色，主题切换后重刷；
 * 最近 14 天柱状图是页面内部的自绘控件（DayBarChart），不依赖 QtCharts。
 */

#include "PlaytimeStatsPage.h"

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include "components/ForumKit.h"
#include "utils/ForumAccountService.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

namespace
{
/** 把主题色转成带透明度的 rgba(...)：颜色本身仍来自主题，不写死色值 */
QString rgbaText(const QColor &color, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha);
}

/** 取主题颜色，异常时退回控件调色板，保证自绘永远画得出来 */
QColor themeColorOr(const QString &value, const QColor &fallback)
{
    const QColor color(value);
    return color.isValid() ? color : fallback;
}

/** 日期标签：今年只显示「11-22」，跨年补上年份 */
QString formatDayLabel(const QDate &day, const QDate &today)
{
    if (!day.isValid())
        return QStringLiteral("-");
    return day.toString(day.year() == today.year() ? QStringLiteral("MM-dd")
                                                   : QStringLiteral("yyyy-MM-dd"));
}

/** 实例副标题：「1.21.9 · Fabric · 3 次」，拿到哪几个拼哪几个 */
QString instanceMetaText(const QString &version, const QString &loader, int sessions)
{
    QStringList parts;
    if (!version.isEmpty())
        parts << version;
    if (!loader.isEmpty())
        parts << loader;
    if (sessions > 0)
        parts << QObject::tr("%1 次").arg(sessions);
    return parts.join(QStringLiteral(" · "));
}

/**
 * @brief 最近 N 天每日时长柱状图（自绘）
 *
 * 柱高按这 N 天里的最大值归一，今天那根用主题色实心高亮，其余柱子同色半透明；
 * 底部画日期刻度，鼠标悬停用 QToolTip 显示「日期 + 星期 · 时长 · 次数」。
 * 颜色每次 paintEvent 现取 ThemeManager，主题切换后只要 update() 就跟着变。
 */
class DayBarChart : public QWidget
{
public:
    explicit DayBarChart(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        // 底色交给外层卡片，自己只画柱子和刻度
        setAttribute(Qt::WA_StyledBackground, false);
        setMouseTracking(true);
        setMinimumHeight(150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    /** 换一批数据（末尾一个是今天） */
    void setBuckets(const QVector<PlaytimeDayBucket> &buckets)
    {
        m_buckets = buckets;
        m_hovered = -1;
        update();
    }

    QSize sizeHint() const override { return QSize(680, 156); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /** 柱子所在的绘图区（扣掉底部日期刻度的高度） */
    QRectF plotRect() const;
    qreal slotWidth() const;
    QRectF barRect(int index) const;
    int indexAt(const QPoint &pos) const;

    QVector<PlaytimeDayBucket> m_buckets;
    int m_hovered = -1;  ///< 当前悬停的柱子下标（-1 = 没悬停）
};

QRectF DayBarChart::plotRect() const
{
    static const qreal kLabelHeight = 19.0;  ///< 底部日期刻度占的高度
    return QRectF(rect()).adjusted(2.0, 6.0, -2.0, -kLabelHeight);
}

qreal DayBarChart::slotWidth() const
{
    if (m_buckets.isEmpty())
        return 0.0;
    return plotRect().width() / qreal(m_buckets.size());
}

QRectF DayBarChart::barRect(int index) const
{
    const QRectF plot = plotRect();
    const qreal slot = slotWidth();
    if (plot.width() <= 0.0 || slot <= 0.0)
        return QRectF();

    const qreal barWidth = qMin(slot * 0.52, 26.0);
    const qreal center = plot.left() + slot * (qreal(index) + 0.5);
    return QRectF(center - barWidth / 2.0, plot.top(), barWidth, plot.height());
}

int DayBarChart::indexAt(const QPoint &pos) const
{
    if (m_buckets.isEmpty())
        return -1;
    const QRectF plot = plotRect();
    if (pos.y() < plot.top() - 4.0 || pos.y() > height())
        return -1;
    const qreal slot = slotWidth();
    if (slot <= 0.0)
        return -1;

    const int index = int((qreal(pos.x()) - plot.left()) / slot);
    if (index < 0 || index >= m_buckets.size())
        return -1;
    return index;
}

void DayBarChart::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    ThemeManager *theme = ThemeManager::instance();
    const bool dark = !theme || theme->currentTheme() != ThemeManager::LightTheme;
    const QColor textColor = themeColorOr(theme ? theme->currentTextColor() : QString(),
                                          palette().color(QPalette::WindowText));
    const QColor borderColor = themeColorOr(theme ? theme->currentBorderColor() : QString(),
                                            palette().color(QPalette::Mid));
    const QColor primary = themeColorOr(theme ? theme->currentThemeColor() : QString(),
                                        palette().color(QPalette::Highlight));

    const QRectF plot = plotRect();
    if (plot.width() <= 0.0 || plot.height() <= 0.0)
        return;

    // 零基线：没有数据时也留一条线，视觉上不会空得莫名其妙
    QColor axisColor = borderColor;
    axisColor.setAlpha(dark ? 130 : 165);
    painter.setPen(QPen(axisColor, 1));
    painter.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));

    if (m_buckets.isEmpty())
        return;

    qint64 maxSeconds = 0;
    for (const PlaytimeDayBucket &bucket : m_buckets)
        maxSeconds = qMax(maxSeconds, bucket.seconds);

    if (maxSeconds <= 0)
    {
        QColor emptyColor = textColor;
        emptyColor.setAlpha(150);
        painter.setPen(emptyColor);
        painter.drawText(plot, Qt::AlignCenter,
                         QObject::tr("还没有游玩记录，启动一次游戏后这里就会画出来"));
        return;
    }

    const qreal slot = slotWidth();
    if (slot <= 0.0)
        return;

    // dailyBuckets 约定末尾就是今天；对不上时按日期再找一次
    int todayIndex = -1;
    for (int i = 0; i < m_buckets.size(); ++i)
    {
        if (m_buckets.at(i).day == QDate::currentDate())
        {
            todayIndex = i;
            break;
        }
    }

    const qreal usable = qMax(1.0, plot.height() - 6.0);
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < m_buckets.size(); ++i)
    {
        const QRectF bar = barRect(i);
        if (bar.width() <= 0.0)
            continue;

        // 柱子底下的浅色轨道：空着的那天也能看出位置
        QColor trackColor = borderColor;
        trackColor.setAlpha(dark ? 46 : 30);
        painter.setBrush(trackColor);
        painter.drawRoundedRect(QRectF(bar.left(), plot.top(), bar.width(), plot.height()),
                                bar.width() / 2.0, bar.width() / 2.0);

        const qint64 seconds = m_buckets.at(i).seconds;
        if (seconds <= 0)
            continue;

        const qreal barHeight = qMax(usable * qreal(seconds) / qreal(maxSeconds), 3.0);
        const QRectF fill(bar.left(), plot.bottom() - barHeight, bar.width(), barHeight);

        QColor barColor = primary;
        if (i == todayIndex)
            barColor.setAlpha(255);  // 今天实心高亮
        else if (i == m_hovered)
            barColor.setAlpha(215);
        else
            barColor.setAlpha(dark ? 160 : 125);
        painter.setBrush(barColor);
        painter.drawRoundedRect(fill, qMin(bar.width() / 2.0, 5.0), qMin(bar.width() / 2.0, 5.0));
    }

    // 底部日期刻度：格子太窄时隔一天画一个，避免文字叠在一起
    QFont labelFont = font();
    labelFont.setPointSizeF(qMax(7.5, labelFont.pointSizeF() - 2.0));
    const QFontMetrics metrics(labelFont);
    const int labelStep =
        (slot < qreal(metrics.horizontalAdvance(QStringLiteral("00-00")) + 10)) ? 2 : 1;
    const QRectF labelArea(plot.left(), plot.bottom() + 3.0, plot.width(),
                           qMax(0.0, height() - plot.bottom() - 3.0));

    for (int i = 0; i < m_buckets.size(); ++i)
    {
        const bool isToday = (i == todayIndex);
        if (i % labelStep != 0 && !isToday)
            continue;

        QFont tickFont = labelFont;
        tickFont.setBold(isToday);
        painter.setFont(tickFont);

        QColor labelColor = isToday ? primary : textColor;
        labelColor.setAlpha(isToday ? 255 : 150);
        painter.setPen(labelColor);

        painter.drawText(QRectF(plot.left() + slot * qreal(i), labelArea.top(), slot,
                                labelArea.height()),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         m_buckets.at(i).day.toString(QStringLiteral("MM-dd")));
    }
}

void DayBarChart::mouseMoveEvent(QMouseEvent *event)
{
    const int index = indexAt(event->position().toPoint());
    if (index != m_hovered)
    {
        m_hovered = index;
        update();
    }

    if (index < 0 || index >= m_buckets.size())
    {
        QToolTip::hideText();
        return;
    }

    const PlaytimeDayBucket &bucket = m_buckets.at(index);
    const QString weekday = Playtime::weekdayName(bucket.day.dayOfWeek());
    QString title = bucket.day.toString(QStringLiteral("yyyy-MM-dd"));
    if (!weekday.isEmpty())
        title = QStringLiteral("%1 %2").arg(title, weekday);

    QString detail = (bucket.seconds > 0) ? Playtime::formatDuration(bucket.seconds)
                                          : QObject::tr("没有游玩");
    if (bucket.sessions > 0)
        detail = QStringLiteral("%1 · %2").arg(detail, QObject::tr("%1 次").arg(bucket.sessions));

    QToolTip::showText(event->globalPosition().toPoint(),
                       QStringLiteral("%1 · %2").arg(title, detail), this);
}

void DayBarChart::leaveEvent(QEvent *event)
{
    if (m_hovered != -1)
    {
        m_hovered = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

} // namespace

PlaytimeStatsPage::PlaytimeStatsPage(QWidget *parent, bool embedded)
    : QWidget(parent), m_embedded(embedded)
{
    buildUi();
    applyThemeStyles();

    // 主题 / 主题色 / 文字色 / 边框色变化都要重刷内联样式与自绘颜色
    ThemeManager *theme = ThemeManager::instance();
    connect(theme, &ThemeManager::themeChanged, this,
            [this](ThemeManager::ThemeType) { onThemeChanged(); });
    connect(theme, &ThemeManager::themeColorChanged, this,
            [this](const QString &) { onThemeChanged(); });
    connect(theme, &ThemeManager::textColorChanged, this,
            [this](const QString &) { onThemeChanged(); });
    connect(theme, &ThemeManager::borderColorChanged, this,
            [this](const QString &) { onThemeChanged(); });

    // 数据层：会话结束 / 心跳落盘后重绘本机数字，同步状态变化后重绘账号卡
    PlaytimeStatsService *service = PlaytimeStatsService::instance();
    connect(service, &PlaytimeStatsService::statsChanged, this, [this]() { renderAll(); });
    connect(service, &PlaytimeStatsService::syncStateChanged, this,
            [this]() { renderAccount(); });
    // 登录 / 退出登录后账号卡要立刻换态（数据层自己会补传未同步会话）
    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            [this](const ForumUser &) { renderAccount(); });

    renderAll();
}

void PlaytimeStatsPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 内容直接挂在页面上：独立整页时外面再套滚动区，嵌在账户管理页里时由外层页面滚动
    auto *content = new QWidget(this);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    // 嵌进账户管理页时内边距收窄，和那边列表页的留白对齐
    layout->setContentsMargins(m_embedded ? 14 : 28, m_embedded ? 6 : 18,
                              m_embedded ? 14 : 28, m_embedded ? 10 : 28);
    layout->setSpacing(m_embedded ? 12 : 14);

    // ── 1. 概览卡：一句副标题 + 五个大数字 ──
    m_overviewCard = new QFrame(content);
    m_overviewCard->setObjectName(QStringLiteral("playtimeOverviewCard"));
    auto *overviewLayout = new QVBoxLayout(m_overviewCard);
    overviewLayout->setContentsMargins(20, 16, 20, 16);
    overviewLayout->setSpacing(8);

    m_overviewTitle = new QLabel(tr("本机游玩时长"), m_overviewCard);
    overviewLayout->addWidget(m_overviewTitle);

    m_overviewSummary = new QLabel(m_overviewCard);
    m_overviewSummary->setWordWrap(true);
    overviewLayout->addWidget(m_overviewSummary);

    auto *statRow = new QHBoxLayout();
    statRow->setContentsMargins(0, 6, 0, 0);
    statRow->setSpacing(18);
    const auto addStat = [this, statRow](const QString &caption) {
        auto *cell = new QWidget(m_overviewCard);
        cell->setAttribute(Qt::WA_StyledBackground, false);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(2);

        auto *value = new QLabel(QStringLiteral("--"), cell);
        auto *captionLabel = new QLabel(caption, cell);
        cellLayout->addWidget(value);
        cellLayout->addWidget(captionLabel);

        m_statValues.append(value);
        m_statCaptions.append(captionLabel);
        statRow->addWidget(cell, 1);
    };
    addStat(tr("累计"));
    addStat(tr("今日"));
    addStat(tr("近 7 天"));
    addStat(tr("累计启动（次）"));
    addStat(tr("游玩天数（天）"));
    overviewLayout->addLayout(statRow);
    layout->addWidget(m_overviewCard);

    // ── 2. 最近 14 天柱状图 ──
    m_chartCard = new QFrame(content);
    m_chartCard->setObjectName(QStringLiteral("playtimeChartCard"));
    auto *chartLayout = new QVBoxLayout(m_chartCard);
    chartLayout->setContentsMargins(20, 16, 20, 14);
    chartLayout->setSpacing(8);

    auto *chartHead = new QHBoxLayout();
    chartHead->setSpacing(10);
    m_chartTitle = new QLabel(tr("最近 14 天"), m_chartCard);
    chartHead->addWidget(m_chartTitle);
    chartHead->addStretch(1);
    m_chartLegend = new QLabel(tr("单位：小时"), m_chartCard);
    chartHead->addWidget(m_chartLegend);
    chartLayout->addLayout(chartHead);

    auto *chart = new DayBarChart(m_chartCard);
    m_chart = chart;
    chartLayout->addWidget(chart);
    layout->addWidget(m_chartCard);

    // ── 3. 年度报告入口卡（主题色渐变底） ──
    m_reportCard = new QFrame(content);
    m_reportCard->setObjectName(QStringLiteral("playtimeReportCard"));
    auto *reportLayout = new QHBoxLayout(m_reportCard);
    reportLayout->setContentsMargins(22, 18, 22, 18);
    reportLayout->setSpacing(16);

    auto *reportTextLayout = new QVBoxLayout();
    reportTextLayout->setSpacing(4);
    m_reportTitle = new QLabel(m_reportCard);
    m_reportSubtitle = new QLabel(
        tr("今年一共玩了多久、最爱哪个实例、最常在什么时候开玩，都能一眼看完，还能存成图片分享。"),
        m_reportCard);
    m_reportSubtitle->setWordWrap(true);
    reportTextLayout->addWidget(m_reportTitle);
    reportTextLayout->addWidget(m_reportSubtitle);
    reportLayout->addLayout(reportTextLayout, 1);

    m_reportButton = new QPushButton(tr("查看报告"), m_reportCard);
    m_reportButton->setCursor(Qt::PointingHandCursor);
    m_reportButton->setFixedHeight(34);
    m_reportButton->setMinimumWidth(100);
    // year = 0 表示今年，由年度报告页自己按当前年份取
    connect(m_reportButton, &QPushButton::clicked, this,
            [this]() { emit annualReportRequested(0); });
    reportLayout->addWidget(m_reportButton, 0, Qt::AlignVCenter);
    layout->addWidget(m_reportCard);

    // ── 4. 实例排行榜 ──
    m_rankCard = new QFrame(content);
    m_rankCard->setObjectName(QStringLiteral("playtimeRankCard"));
    auto *rankCardLayout = new QVBoxLayout(m_rankCard);
    rankCardLayout->setContentsMargins(20, 16, 20, 14);
    rankCardLayout->setSpacing(6);

    auto *rankHead = new QHBoxLayout();
    rankHead->setSpacing(10);
    m_rankTitle = new QLabel(tr("实例排行榜"), m_rankCard);
    rankHead->addWidget(m_rankTitle);
    rankHead->addStretch(1);
    m_rankHint = new QLabel(tr("按累计游玩时长取前 5 名"), m_rankCard);
    rankHead->addWidget(m_rankHint);
    rankCardLayout->addLayout(rankHead);

    m_rankLayout = new QVBoxLayout();
    m_rankLayout->setContentsMargins(0, 0, 0, 0);
    m_rankLayout->setSpacing(2);
    rankCardLayout->addLayout(m_rankLayout);
    layout->addWidget(m_rankCard);

    // ── 5. 账号绑定卡（未登录 / 已登录两态整体重建） ──
    m_accountCard = new QFrame(content);
    m_accountCard->setObjectName(QStringLiteral("playtimeAccountCard"));
    m_accountLayout = new QVBoxLayout(m_accountCard);
    m_accountLayout->setContentsMargins(20, 16, 20, 16);
    m_accountLayout->setSpacing(8);
    layout->addWidget(m_accountCard);

    // ── 6. 最近会话 ──
    m_sessionCard = new QFrame(content);
    m_sessionCard->setObjectName(QStringLiteral("playtimeSessionCard"));
    auto *sessionCardLayout = new QVBoxLayout(m_sessionCard);
    sessionCardLayout->setContentsMargins(20, 16, 20, 14);
    sessionCardLayout->setSpacing(6);

    auto *sessionHead = new QHBoxLayout();
    sessionHead->setSpacing(10);
    m_sessionTitle = new QLabel(tr("最近会话"), m_sessionCard);
    sessionHead->addWidget(m_sessionTitle);
    sessionHead->addStretch(1);
    m_sessionHint = new QLabel(tr("只统计游戏真正跑起来的时间"), m_sessionCard);
    sessionHead->addWidget(m_sessionHint);
    sessionCardLayout->addLayout(sessionHead);

    m_sessionLayout = new QVBoxLayout();
    m_sessionLayout->setContentsMargins(0, 0, 0, 0);
    m_sessionLayout->setSpacing(2);
    sessionCardLayout->addLayout(m_sessionLayout);
    layout->addWidget(m_sessionCard);

    layout->addStretch(1);

    if (m_embedded)
    {
        // 账户管理页「游玩」页签：自带滚动区会和外层页面抢滚轮，直接摊平交给外层
        outer->addWidget(content);
    }
    else
    {
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
        scroll->setWidget(content);
        outer->addWidget(scroll);
    }
}

void PlaytimeStatsPage::applyThemeStyles()
{
    // 卡片：统一走 StyleKit::card()
    const QString cardStyle = StyleKit::card();
    m_overviewCard->setStyleSheet(cardStyle);
    m_chartCard->setStyleSheet(cardStyle);
    m_rankCard->setStyleSheet(cardStyle);
    m_accountCard->setStyleSheet(cardStyle);
    m_sessionCard->setStyleSheet(cardStyle);

    // 区块标题 / 说明
    m_overviewTitle->setStyleSheet(StyleKit::sectionTitle());
    m_overviewSummary->setStyleSheet(StyleKit::mutedLabel());
    m_chartTitle->setStyleSheet(StyleKit::sectionTitle());
    m_chartLegend->setStyleSheet(StyleKit::mutedLabel());
    m_rankTitle->setStyleSheet(StyleKit::sectionTitle());
    m_rankHint->setStyleSheet(StyleKit::mutedLabel());
    m_sessionTitle->setStyleSheet(StyleKit::sectionTitle());
    m_sessionHint->setStyleSheet(StyleKit::mutedLabel());

    // 大数字：累计用主题色，其余用主文字色；说明统一弱化
    const QString accentValue = StyleKit::resolve(QStringLiteral(
        "QLabel{font-size:20px;font-weight:700;color:@PRIMARY@;"
        "border:none;background:transparent;}"));
    const QString plainValue = StyleKit::resolve(QStringLiteral(
        "QLabel{font-size:20px;font-weight:700;color:@TEXT_PRIMARY@;"
        "border:none;background:transparent;}"));
    for (int i = 0; i < m_statValues.size(); ++i)
        m_statValues.at(i)->setStyleSheet(i == 0 ? accentValue : plainValue);
    for (QLabel *captionLabel : m_statCaptions)
        captionLabel->setStyleSheet(StyleKit::mutedLabel());

    // 年度报告卡：主题色渐变底 + 反白文字；按钮用「主色底上的白」做胶囊
    const QColor primary(ThemeManager::instance()->currentThemeColor());
    if (primary.isValid())
    {
        m_reportCard->setStyleSheet(QStringLiteral(
            "QFrame#playtimeReportCard{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "stop:0 %1,stop:1 %2);border:none;border-radius:12px;}")
            .arg(primary.lighter(112).name(), primary.darker(118).name()));
    }
    else
    {
        // 主题色异常时退回平卡，不写死颜色
        m_reportCard->setStyleSheet(StyleKit::flatCard());
    }

    const QColor onPrimary = themeColorOr(StyleKit::resolve(QStringLiteral("@TEXT_ON_PRIMARY@")),
                                          palette().color(QPalette::HighlightedText));
    m_reportTitle->setStyleSheet(QStringLiteral(
        "QLabel{font-size:16px;font-weight:700;color:%1;border:none;background:transparent;}")
        .arg(onPrimary.name()));
    m_reportSubtitle->setStyleSheet(QStringLiteral(
        "QLabel{font-size:12px;color:%1;border:none;background:transparent;}")
        .arg(rgbaText(onPrimary, 205)));
    m_reportButton->setStyleSheet(QStringLiteral(
        "QPushButton{font-size:13px;font-weight:600;color:%1;background-color:%2;"
        "border:none;border-radius:8px;padding:0 18px;}"
        "QPushButton:hover{background-color:%3;}"
        "QPushButton:pressed{background-color:%4;}")
        .arg(primary.isValid() ? primary.darker(110).name() : onPrimary.name(),
             onPrimary.name(), rgbaText(onPrimary, 205), rgbaText(onPrimary, 170)));

    // 自绘柱子每次重画都现取主题色，这里只要触发一次重绘
    if (DayBarChart *chart = static_cast<DayBarChart *>(m_chart))
        chart->update();
}

void PlaytimeStatsPage::onThemeChanged()
{
    applyThemeStyles();
    renderAll();
}

void PlaytimeStatsPage::refresh()
{
    PlaytimeStatsService *service = PlaytimeStatsService::instance();
    renderAll();

    // 已登录且云端累计还没拉到 / 放旧了，就顺手拉一次（失败数据层静默处理）
    if (!ForumAccountService::instance()->isLoggedIn())
        return;
    const PlaytimeCloudSummary &cloud = service->cloudSummary();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!cloud.valid || (cloud.fetchedAt > 0 && now - cloud.fetchedAt > 600))
        service->fetchCloudSummary();
}

void PlaytimeStatsPage::renderAll()
{
    renderOverview();

    // 最近 14 天柱状图：末尾一个是今天
    if (DayBarChart *chart = static_cast<DayBarChart *>(m_chart))
        chart->setBuckets(PlaytimeStatsService::instance()->dailyBuckets(14));

    renderRanking();
    renderAccount();
    renderSessions();
}

void PlaytimeStatsPage::renderOverview()
{
    PlaytimeStatsService *service = PlaytimeStatsService::instance();
    const qint64 total = service->totalSeconds();
    const int year = QDate::currentDate().year();
    m_yearReport = service->annualReport(year);

    // 年度报告入口标题跟着年份走（每年都能点进去看，不要求今年有数据）
    m_reportTitle->setText(tr("我的 MC %1 年度报告").arg(year));

    if (m_statValues.size() >= 5)
    {
        m_statValues.at(0)->setText(Playtime::formatHours(total));
        m_statValues.at(1)->setText(Playtime::formatDuration(service->todaySeconds()));
        m_statValues.at(2)->setText(Playtime::formatDuration(service->weekSeconds()));
        m_statValues.at(3)->setText(QString::number(service->totalLaunchCount()));
        m_statValues.at(4)->setText(QString::number(service->activeDayCount()));
    }

    QStringList parts;
    const QDate firstDay = service->firstPlayedDay();
    if (firstDay.isValid())
        parts << tr("从 %1 开始记录").arg(firstDay.toString(QStringLiteral("yyyy-MM-dd")));

    if (total <= 0)
    {
        parts << tr("还没有统计数据，启动一次游戏后这里就会有记录");
    }
    else
    {
        const QString hourRange = Playtime::hourRangeText(m_yearReport.favoriteHour);
        if (!hourRange.isEmpty())
            parts << tr("今年最常 %1 开玩").arg(hourRange);
        if (!m_yearReport.favoriteInstance.isEmpty())
            parts << tr("玩得最多的是 %1").arg(m_yearReport.favoriteInstance);
    }

    m_overviewSummary->setText(parts.join(QStringLiteral("；")) + QStringLiteral("。"));
}

void PlaytimeStatsPage::renderRanking()
{
    if (!m_rankLayout)
        return;
    ForumKit::clearLayout(m_rankLayout);

    const QVector<PlaytimeInstanceBucket> ranking =
        PlaytimeStatsService::instance()->instanceRanking(5);
    if (ranking.isEmpty())
    {
        auto *empty = new QLabel(tr("还没有玩过的实例，启动一次游戏后这里会按时长排名。"), m_rankCard);
        empty->setStyleSheet(StyleKit::mutedLabel());
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        m_rankLayout->addSpacing(6);
        m_rankLayout->addWidget(empty);
        m_rankLayout->addSpacing(6);
        return;
    }

    const qint64 topSeconds = qMax<qint64>(ranking.first().seconds, 1);
    const QString barStyle = StyleKit::resolve(QStringLiteral(
        "QProgressBar{background-color:@BG_TRACK@;border:none;border-radius:3px;}"
        "QProgressBar::chunk{background-color:@PRIMARY@;border-radius:3px;}"));
    const QString nameStyle = StyleKit::resolve(QStringLiteral(
        "QLabel{font-size:13px;font-weight:600;color:@TEXT_PRIMARY@;"
        "border:none;background:transparent;}"));
    const QString rankStyle = StyleKit::resolve(QStringLiteral(
        "QLabel{font-size:12px;font-weight:700;color:@PRIMARY@;"
        "border:none;background:transparent;}"));
    const QString durationStyle = StyleKit::resolve(QStringLiteral(
        "QLabel{font-size:13px;font-weight:600;color:@PRIMARY@;"
        "border:none;background:transparent;}"));

    for (int i = 0; i < ranking.size(); ++i)
    {
        const PlaytimeInstanceBucket &item = ranking.at(i);

        auto *row = new QWidget(m_rankCard);
        row->setAttribute(Qt::WA_StyledBackground, false);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 5, 0, 5);
        rowLayout->setSpacing(5);

        auto *head = new QHBoxLayout();
        head->setSpacing(8);

        auto *rankLabel = new QLabel(QString::number(i + 1), row);
        rankLabel->setFixedWidth(16);
        rankLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rankLabel->setStyleSheet(rankStyle);
        head->addWidget(rankLabel);

        auto *nameLabel = new ForumKit::ElidedLabel(row);
        nameLabel->setFullText(item.name.isEmpty() ? tr("未知实例") : item.name);
        nameLabel->setStyleSheet(nameStyle);
        head->addWidget(nameLabel, 1);

        const QString meta = instanceMetaText(item.version, item.loader, item.sessions);
        if (!meta.isEmpty())
        {
            auto *metaLabel = new QLabel(meta, row);
            metaLabel->setStyleSheet(StyleKit::mutedLabel());
            head->addWidget(metaLabel, 0);
        }

        auto *durationLabel = new QLabel(Playtime::formatDuration(item.seconds), row);
        durationLabel->setStyleSheet(durationStyle);
        durationLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        head->addWidget(durationLabel, 0);
        rowLayout->addLayout(head);

        auto *bar = new QProgressBar(row);
        bar->setRange(0, 1000);
        const qint64 ratio = item.seconds * 1000 / topSeconds;
        bar->setValue(int(qBound<qint64>(qint64(0), ratio, qint64(1000))));
        bar->setTextVisible(false);
        bar->setFixedHeight(6);
        bar->setStyleSheet(barStyle);
        rowLayout->addWidget(bar);

        m_rankLayout->addWidget(row);
    }
}

void PlaytimeStatsPage::renderAccount()
{
    if (!m_accountLayout)
        return;
    ForumKit::clearLayout(m_accountLayout);

    PlaytimeStatsService *service = PlaytimeStatsService::instance();
    // 账号两态直接看论坛账号登录状态（数据层 accountBound() 与之等价）
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        auto *title = new QLabel(tr("绑定方块盒子账户"), m_accountCard);
        title->setStyleSheet(StyleKit::cardTitle());
        m_accountLayout->addWidget(title);

        auto *desc = new QLabel(
            tr("绑定后，游玩时长会跟着账号跨设备累计，换电脑也能看到同一份数据，"
               "还能生成「我的 MC 年度报告」。"),
            m_accountCard);
        desc->setStyleSheet(StyleKit::mutedLabel());
        desc->setWordWrap(true);
        m_accountLayout->addWidget(desc);

        auto *buttonRow = new QHBoxLayout();
        buttonRow->setContentsMargins(0, 4, 0, 0);
        buttonRow->addStretch(1);
        auto *loginButton = new QPushButton(tr("登录并绑定"), m_accountCard);
        loginButton->setStyleSheet(StyleKit::primaryButton());
        loginButton->setCursor(Qt::PointingHandCursor);
        loginButton->setFixedHeight(34);
        connect(loginButton, &QPushButton::clicked, this, &PlaytimeStatsPage::loginRequested);
        buttonRow->addWidget(loginButton);
        m_accountLayout->addLayout(buttonRow);
        return;
    }

    const ForumUser user = ForumAccountService::instance()->user();
    const QString displayName = user.displayName.isEmpty() ? user.username : user.displayName;
    const int pending = service->pendingSyncCount();
    const PlaytimeCloudSummary &cloud = service->cloudSummary();

    // 标题行：已绑定 @昵称 + 同步状态胶囊
    auto *head = new QHBoxLayout();
    head->setSpacing(8);
    auto *title = new QLabel(tr("已绑定 @%1").arg(
                                 displayName.isEmpty() ? tr("方块盒子账户") : displayName),
                             m_accountCard);
    title->setStyleSheet(StyleKit::cardTitle());
    head->addWidget(title);

    auto *stateChip = new QLabel(m_accountCard);
    if (pending > 0)
    {
        stateChip->setText(tr("%1 条待同步").arg(pending));
        stateChip->setStyleSheet(
            StyleKit::chip(StyleKit::resolve(QStringLiteral("@WARNING_BG@")),
                           StyleKit::resolve(QStringLiteral("@WARNING_TEXT@"))));
    }
    else
    {
        stateChip->setText(tr("已全部同步"));
        stateChip->setStyleSheet(
            StyleKit::chip(StyleKit::resolve(QStringLiteral("@PRIMARY_BG@")),
                           StyleKit::resolve(QStringLiteral("@TEXT_ACCENT@"))));
    }
    head->addWidget(stateChip);
    head->addStretch(1);
    m_accountLayout->addLayout(head);

    // 说明行：待同步条数 + 最近同步时间
    const qint64 lastSyncAt = service->lastSyncAt();
    const QString lastSyncText =
        lastSyncAt > 0
            ? QDateTime::fromSecsSinceEpoch(lastSyncAt).toString(QStringLiteral("yyyy-MM-dd HH:mm"))
            : tr("还没同步过");
    auto *info = new QLabel(tr("待同步 %1 条 · 最近同步 %2").arg(pending).arg(lastSyncText),
                            m_accountCard);
    info->setStyleSheet(StyleKit::mutedLabel());
    info->setWordWrap(true);
    m_accountLayout->addWidget(info);

    // 云端累计 + 立即同步 / 刷新云端
    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 2, 0, 0);
    actionRow->setSpacing(8);

    auto *cloudLabel = new QLabel(m_accountCard);
    if (cloud.valid)
    {
        QString cloudText = tr("云端累计 %1").arg(Playtime::formatDuration(cloud.lifetimeSeconds));
        if (cloud.sessionCount > 0)
            cloudText += tr(" · %1 次会话").arg(cloud.sessionCount);
        cloudLabel->setText(cloudText);
        cloudLabel->setStyleSheet(StyleKit::resolve(QStringLiteral(
            "QLabel{font-size:13px;font-weight:600;color:@TEXT_PRIMARY@;"
            "border:none;background:transparent;}")));
    }
    else
    {
        cloudLabel->setText(tr("云端累计：点击刷新"));
        cloudLabel->setStyleSheet(StyleKit::mutedLabel());
    }
    actionRow->addWidget(cloudLabel);
    actionRow->addStretch(1);

    auto *refreshButton = new QPushButton(tr("刷新云端"), m_accountCard);
    refreshButton->setStyleSheet(StyleKit::ghostButton());
    refreshButton->setCursor(Qt::PointingHandCursor);
    refreshButton->setFixedHeight(28);
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        PlaytimeStatsService::instance()->fetchCloudSummary();
        renderAccount();
    });
    actionRow->addWidget(refreshButton);

    auto *syncButton = new QPushButton(tr("立即同步"), m_accountCard);
    syncButton->setStyleSheet(StyleKit::outlineButton());
    syncButton->setCursor(Qt::PointingHandCursor);
    syncButton->setFixedHeight(30);
    syncButton->setEnabled(pending > 0);
    connect(syncButton, &QPushButton::clicked, this, [this, syncButton]() {
        syncButton->setEnabled(false);
        syncButton->setText(tr("同步中…"));
        PlaytimeStatsService::instance()->syncNow(false);
        // 网络失败时数据层不发信号，兜一下，别让按钮一直卡在「同步中…」
        QTimer::singleShot(4000, this, [this]() { renderAccount(); });
    });
    actionRow->addWidget(syncButton);
    m_accountLayout->addLayout(actionRow);
}

void PlaytimeStatsPage::renderSessions()
{
    if (!m_sessionLayout)
        return;
    ForumKit::clearLayout(m_sessionLayout);

    const QVector<PlaytimeSessionRecord> sessions =
        PlaytimeStatsService::instance()->recentSessions(8);
    if (sessions.isEmpty())
    {
        auto *empty = new QLabel(tr("还没有游玩记录，启动一次游戏后这里就会有数据。"), m_sessionCard);
        empty->setStyleSheet(StyleKit::mutedLabel());
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        m_sessionLayout->addSpacing(6);
        m_sessionLayout->addWidget(empty);
        m_sessionLayout->addSpacing(6);
        return;
    }

    const QDate today = QDate::currentDate();
    const QString dateStyle = StyleKit::mutedLabel();
    const QString nameStyle = StyleKit::resolve(QStringLiteral(
        "QLabel{font-size:13px;color:@TEXT_PRIMARY@;border:none;background:transparent;}"));
    const QString durationStyle = StyleKit::resolve(QStringLiteral(
        "QLabel{font-size:13px;font-weight:600;color:@PRIMARY@;"
        "border:none;background:transparent;}"));
    const QString separatorStyle = StyleKit::resolve(
        QStringLiteral("QFrame{background-color:@BORDER@;border:none;}"));

    for (int i = 0; i < sessions.size(); ++i)
    {
        const PlaytimeSessionRecord &item = sessions.at(i);
        if (i > 0)
        {
            auto *separator = new QFrame(m_sessionCard);
            separator->setFixedHeight(1);
            separator->setStyleSheet(separatorStyle);
            m_sessionLayout->addWidget(separator);
        }

        const QDate day = QDateTime::fromSecsSinceEpoch(item.endAt > 0 ? item.endAt : item.startAt)
                              .date();
        QString dateText = formatDayLabel(day, today);
        const QString weekday = Playtime::weekdayName(day.dayOfWeek());
        if (!weekday.isEmpty())
            dateText = QStringLiteral("%1 %2").arg(dateText, weekday);

        auto *row = new QWidget(m_sessionCard);
        row->setAttribute(Qt::WA_StyledBackground, false);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 6, 0, 6);
        rowLayout->setSpacing(10);

        auto *dateLabel = new QLabel(dateText, row);
        dateLabel->setFixedWidth(92);
        dateLabel->setStyleSheet(dateStyle);
        rowLayout->addWidget(dateLabel);

        auto *nameLabel = new ForumKit::ElidedLabel(row);
        nameLabel->setFullText(item.instanceName.isEmpty() ? tr("未知实例") : item.instanceName);
        nameLabel->setStyleSheet(nameStyle);
        rowLayout->addWidget(nameLabel, 1);

        auto *saveLabel = new ForumKit::ElidedLabel(row);
        QString saveText;
        if (!item.saveName.isEmpty())
            saveText = tr("存档 %1").arg(item.saveName);
        else if (item.roomId > 0)
            saveText = tr("联机房间 %1").arg(item.roomId);
        else
            saveText = tr("未识别存档");
        saveLabel->setFullText(saveText);
        saveLabel->setStyleSheet(dateStyle);
        saveLabel->setMaximumWidth(200);
        rowLayout->addWidget(saveLabel, 0);

        auto *durationLabel = new QLabel(Playtime::formatDuration(item.seconds), row);
        durationLabel->setStyleSheet(durationStyle);
        durationLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(durationLabel, 0);

        m_sessionLayout->addWidget(row);
    }
}
