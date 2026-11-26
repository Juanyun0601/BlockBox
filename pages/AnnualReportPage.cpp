/**
 * @file   AnnualReportPage.cpp
 * @brief  「我的 MC 年度报告」页实现：年份切换、主视觉、8 张数据卡、月度柱状图、分享海报
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "AnnualReportPage.h"

#include <QDate>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QPushButton>
#include <QRectF>
#include <QScrollArea>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

#include "components/AnnualReportCard.h"
#include "components/MedalShareDialog.h"
#include "utils/ForumAccountService.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

namespace
{

/** 年份胶囊：选中态点亮主题色，未选中是中性描边 */
QString yearPillStyle()
{
    return StyleKit::resolve(QStringLiteral(
        "QPushButton {"
        "  background: transparent;"
        "  border: 1px solid @BORDER@;"
        "  border-radius: 14px;"
        "  color: @TEXT_SECONDARY@;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 0 14px;"
        "}"
        "QPushButton:hover {"
        "  border-color: @PRIMARY@;"
        "  color: @PRIMARY@;"
        "}"
        "QPushButton:checked {"
        "  background: @PRIMARY_BG_SOFT@;"
        "  border-color: @PRIMARY@;"
        "  color: @PRIMARY@;"
        "}"));
}

/**
 * 主视觉卡：深绿主色渐变（主色 → 主色加深），上面一律用白字。
 *
 * 以前是「主色浅调 → 卡片底」的浅色渐变 + 主题文字色，浅色主题下大数字几乎糊在
 * 背景里、深色主题下更像一块亮绿底上的黑字；改成品牌深绿后两种主题都稳定好看，
 * 也更容易和导出的分享海报对上。
 */
QString heroCardStyle()
{
    return StyleKit::resolve(QStringLiteral(
        "QFrame#annualHeroCard {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 @PRIMARY@, stop:1 @PRIMARY_DARK@);"
        "  border: 1px solid @PRIMARY_DARK@;"
        "  border-radius: 16px;"
        "}"));
}

/** 主视觉卡的白字：标题白、大数字白、说明白，只靠字号和字重分层 */
QString heroTextStyle(int pixelSize, int weight)
{
    return StyleKit::resolve(QStringLiteral(
        "QLabel { font-size: %1px; font-weight: %2; color: @TEXT_ON_PRIMARY@;"
        " border: none; background: transparent; }")
            .arg(pixelSize)
            .arg(weight));
}

/** 主视觉卡右下「生成分享图」：绿底上的白底按钮，比深绿按钮显眼 */
QString heroButtonStyle()
{
    return StyleKit::resolve(QStringLiteral(
        "QPushButton {"
        "  font-size: 13px; font-weight: 600;"
        "  color: @PRIMARY@;"
        "  background-color: @TEXT_ON_PRIMARY@;"
        "  border: none; border-radius: 8px; padding: 8px 18px;"
        "}"
        "QPushButton:hover { background-color: @PRIMARY_BG_SOFT@; }"
        "QPushButton:pressed { background-color: @PRIMARY_BG@; }"));
}

/** 数据小卡里的数字 / 名字 */
QString statValueStyle()
{
    return StyleKit::resolve(QStringLiteral(
        "QLabel { font-size: 17px; font-weight: 700; color: @TEXT_PRIMARY@;"
        " border: none; background: transparent; }"));
}

} // namespace

// ═══════════════════════════════════════════════════════════════════
// 12 个月迷你柱状图：自绘，跟随主题；悬停显示「月份 + 时长」
// 只在年月数据上做文章，不依赖 QtCharts，和统计页的每日柱状图同风格。
// ═══════════════════════════════════════════════════════════════════

class AnnualReportMonthChart : public QWidget
{
public:
    explicit AnnualReportMonthChart(QWidget *parent = nullptr);

    /** 塞入 12 个月的秒数（缺月按 0），highlightMonth 为最活跃月份（1~12，0 = 不高亮） */
    void setMonthly(const QVector<PlaytimeDayBucket> &monthly, int highlightMonth);

    QSize sizeHint() const override { return QSize(640, 200); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /** 鼠标横坐标落在第几个月（0~11），不在柱状区返回 -1 */
    int monthAt(qreal x) const;

    QVector<qint64> m_seconds;   ///< 12 个月的秒数
    int m_highlightMonth = 0;    ///< 最活跃月份（1~12，0 = 不高亮）
    int m_hoveredMonth = -1;     ///< 鼠标悬停的月份（0~11，-1 = 没悬停）
    QRectF m_plotRect;           ///< 柱状区（鼠标命中判定用）
};

AnnualReportMonthChart::AnnualReportMonthChart(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMinimumHeight(170);
    setMouseTracking(true);
    m_seconds = QVector<qint64>(12, 0);
}

void AnnualReportMonthChart::setMonthly(const QVector<PlaytimeDayBucket> &monthly,
                                        int highlightMonth)
{
    m_seconds = QVector<qint64>(12, 0);
    for (int month = 0; month < 12 && month < monthly.size(); ++month)
        m_seconds[month] = qMax<qint64>(0, monthly.at(month).seconds);
    m_highlightMonth = (highlightMonth >= 1 && highlightMonth <= 12) ? highlightMonth : 0;
    update();
}

int AnnualReportMonthChart::monthAt(qreal x) const
{
    if (m_plotRect.width() <= 0)
        return -1;
    if (x < m_plotRect.left() || x > m_plotRect.right())
        return -1;
    const qreal slot = m_plotRect.width() / 12.0;
    return qBound(0, int((x - m_plotRect.left()) / slot), 11);
}

void AnnualReportMonthChart::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    ThemeManager *theme = ThemeManager::instance();
    const QColor accent(theme->currentThemeColor());
    const QColor text(theme->currentTextColor());
    const QColor border(theme->currentBorderColor());
    const bool dark = theme->currentTheme() != ThemeManager::LightTheme;

    const qreal labelHeight = 20.0;
    const QRectF plot(0.0, 8.0, qMax(1, width()), qMax(1, height()) - labelHeight - 8.0);
    m_plotRect = plot;

    qint64 maxSeconds = 0;
    for (qint64 seconds : m_seconds)
        maxSeconds = qMax(maxSeconds, seconds);

    // 三条淡虚线当刻度，基线用实线
    QColor gridColor = border;
    gridColor.setAlpha(110);
    painter.setPen(QPen(gridColor, 1, Qt::DashLine));
    for (int line = 1; line <= 3; ++line)
    {
        const qreal y = plot.bottom() - plot.height() * line / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    painter.setPen(QPen(border, 1));
    painter.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));

    const qreal slot = plot.width() / 12.0;
    const qreal barWidth = qMin(30.0, slot * 0.56);

    QFont labelFont = font();
    labelFont.setPixelSize(10);

    for (int month = 0; month < 12; ++month)
    {
        const qint64 seconds = m_seconds.at(month);
        const qreal centerX = plot.left() + slot * month + slot / 2.0;

        qreal barHeight = 2.0;
        if (maxSeconds > 0 && seconds > 0)
            barHeight = qMax(4.0, plot.height() * qreal(seconds) / qreal(maxSeconds));

        const bool highlight = m_highlightMonth == month + 1 && seconds > 0;
        const bool hovered = m_hoveredMonth == month;

        painter.setPen(Qt::NoPen);
        if (highlight)
        {
            QLinearGradient gradient(0, plot.bottom() - barHeight, 0, plot.bottom());
            gradient.setColorAt(0.0, accent.lighter(118));
            gradient.setColorAt(1.0, accent.darker(112));
            painter.setBrush(gradient);
        }
        else if (seconds > 0)
        {
            QColor fill = accent;
            fill.setAlpha(hovered ? 215 : (dark ? 100 : 135));
            painter.setBrush(fill);
        }
        else
        {
            QColor empty = border;
            empty.setAlpha(90);
            painter.setBrush(empty);
        }
        painter.drawRoundedRect(
            QRectF(centerX - barWidth / 2.0, plot.bottom() - barHeight, barWidth, barHeight), 4, 4);

        labelFont.setBold(highlight || hovered);
        painter.setFont(labelFont);
        QColor labelColor = (highlight || hovered) ? accent : text;
        if (!highlight && !hovered)
            labelColor.setAlpha(150);
        painter.setPen(labelColor);
        painter.drawText(QRectF(centerX - slot / 2.0, plot.bottom() + 2.0, slot, labelHeight),
                         Qt::AlignHCenter | Qt::AlignTop, QString::number(month + 1));
    }

    if (maxSeconds <= 0)
    {
        QFont emptyFont = font();
        emptyFont.setPixelSize(12);
        painter.setFont(emptyFont);
        QColor emptyColor = text;
        emptyColor.setAlpha(150);
        painter.setPen(emptyColor);
        painter.drawText(plot, Qt::AlignCenter, tr("还没有按月统计的游玩数据"));
    }
}

void AnnualReportMonthChart::mouseMoveEvent(QMouseEvent *event)
{
    const int month = monthAt(event->position().x());
    if (month != m_hoveredMonth)
    {
        m_hoveredMonth = month;
        update();
    }

    if (month >= 0 && month < m_seconds.size())
    {
        QToolTip::showText(event->globalPosition().toPoint(),
                           tr("%1 月 · %2").arg(month + 1).arg(
                               Playtime::formatDuration(m_seconds.at(month))),
                           this);
    }
    else
    {
        QToolTip::hideText();
    }
    QWidget::mouseMoveEvent(event);
}

void AnnualReportMonthChart::leaveEvent(QEvent *event)
{
    m_hoveredMonth = -1;
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}

// ═══════════════════════════════════════════════════════════════════
// 年度报告页
// ═══════════════════════════════════════════════════════════════════

AnnualReportPage::AnnualReportPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    applyStyle();

    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](ThemeManager::ThemeType) { applyStyle(); });
    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this,
            [this](const QString &) { applyStyle(); });

    PlaytimeStatsService *service = PlaytimeStatsService::instance();
    // 本机汇总变了：云端数据优先，本机数据在显时才跟着重刷
    connect(service, &PlaytimeStatsService::statsChanged, this, [this]() {
        if (m_fromCloud || m_year <= 0)
            return;
        m_report = PlaytimeStatsService::instance()->annualReport(m_year);
        renderReport();
    });
    connect(service, &PlaytimeStatsService::syncStateChanged, this,
            [this]() { updateAccountCard(); });

    // 刚登录 / 退登：账号数据能不能看变了，整页重来一次
    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            [this](const ForumUser &) { refresh(); });

    // 进页面之前先摆一个空态，免得主视觉什么都没有
    m_heroTitle->setText(tr("年度报告"));
    m_heroValue->setText(tr("—"));
    m_heroSub->setText(tr("正在读取游玩数据…"));
    m_sourceChip->setText(tr("本机数据"));
    updateAccountCard();
}

void AnnualReportPage::openPage(int year)
{
    if (year > 0)
        m_year = year;
    refresh();
}

void AnnualReportPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

    auto *content = new QWidget(scroll);
    content->setAttribute(Qt::WA_StyledBackground, false);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 18, 28, 28);
    layout->setSpacing(14);

    // ── 年份胶囊：哪年有数据给哪年，另外永远给一个「今年」 ──
    auto *yearRow = new QHBoxLayout();
    yearRow->setSpacing(10);
    m_yearHost = new QWidget(content);
    m_yearHost->setAttribute(Qt::WA_StyledBackground, false);
    m_yearLayout = new QHBoxLayout(m_yearHost);
    m_yearLayout->setContentsMargins(0, 0, 0, 0);
    m_yearLayout->setSpacing(8);
    yearRow->addWidget(m_yearHost);
    yearRow->addStretch(1);
    layout->addLayout(yearRow);

    // ── 账号提示卡（未登录才显示） ──
    m_accountCard = new QFrame(content);
    m_accountCard->setObjectName(QStringLiteral("annualAccountCard"));
    auto *accountLayout = new QHBoxLayout(m_accountCard);
    accountLayout->setContentsMargins(16, 12, 16, 12);
    accountLayout->setSpacing(14);

    auto *accountText = new QVBoxLayout();
    accountText->setContentsMargins(0, 0, 0, 0);
    accountText->setSpacing(3);
    m_accountTitle = new QLabel(m_accountCard);
    m_accountTitle->setWordWrap(true);
    m_accountHint = new QLabel(m_accountCard);
    m_accountHint->setWordWrap(true);
    accountText->addWidget(m_accountTitle);
    accountText->addWidget(m_accountHint);
    accountLayout->addLayout(accountText, 1);

    m_accountButton = new QPushButton(tr("登录并绑定"), m_accountCard);
    m_accountButton->setCursor(Qt::PointingHandCursor);
    m_accountButton->setMinimumHeight(32);
    connect(m_accountButton, &QPushButton::clicked, this, [this]() { emit loginRequested(); });
    accountLayout->addWidget(m_accountButton, 0, Qt::AlignVCenter);
    layout->addWidget(m_accountCard);

    // ── 主视觉：一年待了多久 + 数据来源 + 生成分享图 ──
    m_heroCard = new QFrame(content);
    m_heroCard->setObjectName(QStringLiteral("annualHeroCard"));
    m_heroCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *heroLayout = new QVBoxLayout(m_heroCard);
    heroLayout->setContentsMargins(22, 18, 22, 18);
    heroLayout->setSpacing(6);

    m_heroTitle = new QLabel(m_heroCard);
    m_heroValue = new QLabel(m_heroCard);
    m_heroSub = new QLabel(m_heroCard);
    m_heroSub->setWordWrap(true);
    heroLayout->addWidget(m_heroTitle);
    heroLayout->addWidget(m_heroValue);
    heroLayout->addWidget(m_heroSub);
    heroLayout->addSpacing(4);

    auto *heroBottom = new QHBoxLayout();
    heroBottom->setSpacing(10);
    m_sourceChip = new QLabel(m_heroCard);
    m_sourceChip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    heroBottom->addWidget(m_sourceChip, 0, Qt::AlignVCenter);
    heroBottom->addStretch(1);
    m_posterButton = new QPushButton(tr("生成分享图"), m_heroCard);
    m_posterButton->setCursor(Qt::PointingHandCursor);
    m_posterButton->setMinimumHeight(34);
    connect(m_posterButton, &QPushButton::clicked, this, &AnnualReportPage::generatePoster);
    heroBottom->addWidget(m_posterButton, 0, Qt::AlignVCenter);
    heroLayout->addLayout(heroBottom);
    layout->addWidget(m_heroCard);

    // ── 8 张数据小卡：4 列 × 2 行 ──
    const QStringList titles = {
        tr("启动次数"), tr("活跃天数"), tr("最长一次"),   tr("最爱实例"),
        tr("最常玩的时段"), tr("最活跃月份"), tr("最常玩的星期"), tr("点亮成就数"),
    };
    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);
    for (int index = 0; index < kStatCardCount; ++index)
    {
        StatCard &stat = m_statCards[index];
        stat.card = new QFrame(content);
        stat.card->setObjectName(QStringLiteral("annualStatCard"));
        auto *cardLayout = new QVBoxLayout(stat.card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->setSpacing(4);

        stat.title = new QLabel(titles.value(index), stat.card);
        stat.value = new QLabel(tr("—"), stat.card);
        stat.value->setWordWrap(true);
        stat.hint = new QLabel(stat.card);
        stat.hint->setWordWrap(true);
        cardLayout->addWidget(stat.title);
        cardLayout->addWidget(stat.value);
        cardLayout->addWidget(stat.hint);
        cardLayout->addStretch(1);

        grid->addWidget(stat.card, index / 4, index % 4);
    }
    layout->addLayout(grid);

    // ── 12 个月柱状图 ──
    m_chartCard = new QFrame(content);
    m_chartCard->setObjectName(QStringLiteral("annualChartCard"));
    auto *chartLayout = new QVBoxLayout(m_chartCard);
    chartLayout->setContentsMargins(18, 14, 18, 14);
    chartLayout->setSpacing(8);

    auto *chartHead = new QHBoxLayout();
    chartHead->setSpacing(10);
    m_chartTitle = new QLabel(tr("每月游玩时长"), m_chartCard);
    m_chartUnit = new QLabel(tr("单位：小时"), m_chartCard);
    chartHead->addWidget(m_chartTitle);
    chartHead->addStretch(1);
    chartHead->addWidget(m_chartUnit);
    chartLayout->addLayout(chartHead);

    m_chart = new AnnualReportMonthChart(m_chartCard);
    chartLayout->addWidget(m_chart);
    layout->addWidget(m_chartCard);

    layout->addStretch(1);

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void AnnualReportPage::applyStyle()
{
    // 年份胶囊：样式里带 :checked / :hover 伪状态，不用每次点击重设
    const QString pill = yearPillStyle();
    for (QPushButton *button : m_yearButtons)
        button->setStyleSheet(pill);

    if (m_accountCard)
    {
        m_accountCard->setStyleSheet(StyleKit::card());
        m_accountTitle->setStyleSheet(StyleKit::sectionTitle());
        m_accountHint->setStyleSheet(StyleKit::mutedLabel());
        m_accountButton->setStyleSheet(StyleKit::smallPrimaryButton());
    }

    if (m_heroCard)
    {
        m_heroCard->setStyleSheet(heroCardStyle());
        m_heroTitle->setStyleSheet(heroTextStyle(13, 600));
        m_heroValue->setStyleSheet(heroTextStyle(34, 800));
        m_heroSub->setStyleSheet(heroTextStyle(12, 400));
        m_posterButton->setStyleSheet(heroButtonStyle());
        m_sourceChip->setStyleSheet(StyleKit::chip(QStringLiteral("@TEXT_ON_PRIMARY@"),
                                                   QStringLiteral("@PRIMARY@")));
    }

    for (const StatCard &stat : m_statCards)
    {
        if (!stat.card)
            continue;
        stat.card->setStyleSheet(StyleKit::card());
        stat.title->setStyleSheet(StyleKit::mutedLabel());
        stat.value->setStyleSheet(statValueStyle());
        stat.hint->setStyleSheet(StyleKit::captionLabel());
    }

    if (m_chartCard)
    {
        m_chartCard->setStyleSheet(StyleKit::card());
        m_chartTitle->setStyleSheet(StyleKit::sectionTitle());
        m_chartUnit->setStyleSheet(StyleKit::captionLabel());
    }

    if (m_chart)
        m_chart->update();
}

void AnnualReportPage::rebuildYearPills()
{
    if (!m_yearLayout || !m_yearHost)
        return;

    for (QPushButton *button : m_yearButtons)
    {
        m_yearLayout->removeWidget(button);
        button->deleteLater();
    }
    m_yearButtons.clear();

    // 有数据的年份 + 今年，倒序排（最近的年份在最左边）
    QVector<int> years = PlaytimeStatsService::instance()->availableYears();
    const int thisYear = QDate::currentDate().year();
    if (!years.contains(thisYear))
        years.append(thisYear);
    if (m_year > 0 && !years.contains(m_year))
        years.append(m_year);
    std::sort(years.begin(), years.end(), std::greater<int>());

    const QString pill = yearPillStyle();
    for (int year : years)
    {
        auto *button = new QPushButton(QString::number(year), m_yearHost);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(28);
        button->setChecked(year == m_year);
        button->setStyleSheet(pill);
        connect(button, &QPushButton::clicked, this, [this, year]() { selectYear(year); });
        m_yearLayout->addWidget(button);
        m_yearButtons.append(button);
    }
}

void AnnualReportPage::updateAccountCard()
{
    if (!m_accountCard)
        return;

    const bool bound = PlaytimeStatsService::instance()->accountBound();
    // 已登录就汇总到账号上了，顶部不用再提醒；未登录才摆提示卡
    m_accountCard->setVisible(!bound);
    if (bound)
        return;

    const int pending = PlaytimeStatsService::instance()->pendingSyncCount();
    m_accountTitle->setText(tr("绑定方块盒子账户后，年度报告会汇总你所有电脑上的时长"));
    m_accountHint->setText(
        pending > 0
            ? tr("现在显示的是这台电脑的本地记录，还有 %1 条记录等着上传。").arg(pending)
            : tr("现在显示的是这台电脑的本地记录，换台电脑就看不到了。"));
    m_accountButton->setText(tr("登录并绑定"));
}

void AnnualReportPage::refresh()
{
    if (m_year <= 0)
        m_year = QDate::currentDate().year();
    rebuildYearPills();
    updateAccountCard();
    loadReport(m_year);
}

void AnnualReportPage::selectYear(int year)
{
    if (year <= 0 || year == m_year)
        return;
    m_year = year;
    loadReport(year);
}

void AnnualReportPage::loadReport(int year)
{
    if (year <= 0)
        year = QDate::currentDate().year();
    m_year = year;
    m_cloudFailed = false;

    PlaytimeStatsService *service = PlaytimeStatsService::instance();
    if (!service->accountBound())
    {
        // 没登录：本机数据，不标来源（本机是默认态）
        m_fetching = false;
        m_fromCloud = false;
        m_report = service->annualReport(year);
        renderReport();
        return;
    }

    // 登录了：先把本机数据摆出来，云端数据回来再替换，进页面不会先空一下
    m_fetching = true;
    m_fromCloud = false;
    m_report = service->annualReport(year);
    renderReport();

    QPointer<AnnualReportPage> guard(this);
    service->fetchAnnualReport(
        year,
        [guard, year](const PlaytimeAnnualReport &report) {
            if (!guard)
                return;
            guard->applyCloudReport(report, year);
        },
        [guard, year]() {
            if (!guard)
                return;
            guard->onCloudFailed(year);
        });
}

void AnnualReportPage::applyCloudReport(const PlaytimeAnnualReport &report, int requestedYear)
{
    if (requestedYear != m_year)
        return; // 用户已经切到别的年份了，丢掉这次结果
    m_fetching = false;
    m_cloudFailed = false;
    m_fromCloud = true;
    m_report = report;
    if (m_report.year <= 0)
        m_report.year = requestedYear;
    renderReport();
}

void AnnualReportPage::onCloudFailed(int requestedYear)
{
    if (requestedYear != m_year)
        return;
    m_fetching = false;
    m_fromCloud = false;
    m_cloudFailed = PlaytimeStatsService::instance()->accountBound();
    m_report = PlaytimeStatsService::instance()->annualReport(requestedYear);
    renderReport();
}

void AnnualReportPage::renderReport()
{
    renderHero();
    renderStatCards();
    renderChart();
}

void AnnualReportPage::renderHero()
{
    const int year = m_year > 0 ? m_year : QDate::currentDate().year();
    m_heroTitle->setText(tr("%1 年，你在方块里待了").arg(year));

    if (!m_report.hasData())
    {
        m_heroValue->setText(tr("0 小时"));
        m_heroSub->setText(tr("%1 年还没有游玩记录，进游戏玩一次就会出现在这里。").arg(year));
    }
    else
    {
        m_heroValue->setText(Playtime::formatHours(m_report.totalSeconds));
        if (m_report.firstPlayedOn.isValid() && m_report.lastPlayedOn.isValid())
        {
            m_heroSub->setText(tr("%1 ~ %2 · 共 %3 次启动 · %4 天有游玩")
                                   .arg(m_report.firstPlayedOn.toString(
                                            QStringLiteral("yyyy-MM-dd")),
                                        m_report.lastPlayedOn.toString(
                                            QStringLiteral("yyyy-MM-dd")))
                                   .arg(qMax(0, m_report.launchCount))
                                   .arg(qMax(0, m_report.activeDays)));
        }
        else
        {
            m_heroSub->setText(tr("共 %1 次启动 · %2 天有游玩")
                                   .arg(qMax(0, m_report.launchCount))
                                   .arg(qMax(0, m_report.activeDays)));
        }
    }

    if (m_fetching)
        m_sourceChip->setText(tr("正在汇总账号数据…"));
    else if (m_fromCloud)
        m_sourceChip->setText(tr("账号云端"));
    else if (m_cloudFailed)
        m_sourceChip->setText(tr("本机数据 · 云端拉取失败"));
    else
        m_sourceChip->setText(tr("本机数据"));
}

void AnnualReportPage::renderStatCards()
{
    auto setCard = [this](int index, const QString &value, const QString &hint) {
        if (index < 0 || index >= kStatCardCount)
            return;
        m_statCards[index].value->setText(value);
        m_statCards[index].hint->setText(hint);
    };

    // 1. 启动次数
    const int launches = qMax(0, m_report.launchCount);
    // 平均每月：不足 1 次时别写成「0 次」，看着像没玩过
    const double monthlyAverage = launches / 12.0;
    const QString launchHint =
        launches <= 0
            ? tr("这一年还没启动过游戏")
            : (monthlyAverage >= 1.0
                   ? tr("平均每月 %1 次").arg(QString::number(monthlyAverage, 'f', 1))
                   : tr("平均每月不到 1 次"));
    setCard(0, tr("%1 次").arg(launches), launchHint);

    // 2. 活跃天数
    const int days = qMax(0, m_report.activeDays);
    setCard(1, tr("%1 天").arg(days),
            days > 0 ? tr("平均每天 %1").arg(
                           Playtime::formatDuration(m_report.totalSeconds / days))
                     : tr("有游玩记录的天数"));

    // 3. 最长一次
    setCard(2,
            m_report.longestSessionSeconds > 0
                ? Playtime::formatDuration(m_report.longestSessionSeconds)
                : tr("—"),
            m_report.longestSessionDay.isValid()
                ? m_report.longestSessionDay.toString(QStringLiteral("M 月 d 日"))
                : tr("单次连续游玩的最长时长"));

    // 4. 最爱实例
    const QString favorite = m_report.favoriteInstance.trimmed();
    if (favorite.isEmpty())
    {
        setCard(3, tr("—"), tr("玩得最久的实例会出现在这里"));
    }
    else
    {
        const QString version = m_report.favoriteVersion.trimmed();
        setCard(3, favorite,
                version.isEmpty()
                    ? Playtime::formatDuration(m_report.favoriteInstanceSeconds)
                    : tr("%1 · 累计 %2").arg(version, Playtime::formatDuration(
                                                          m_report.favoriteInstanceSeconds)));
    }

    // 5. 最常玩的时段
    const QString hourText = Playtime::hourRangeText(m_report.favoriteHour);
    setCard(4, hourText.isEmpty() ? tr("数据不足") : hourText, tr("按每次会话的开始时间统计"));

    // 6. 最活跃月份
    if (m_report.busiestMonth >= 1 && m_report.busiestMonth <= 12)
    {
        setCard(5, tr("%1 月").arg(m_report.busiestMonth),
                tr("当月累计 %1").arg(Playtime::formatDuration(m_report.busiestMonthSeconds)));
    }
    else
    {
        setCard(5, tr("数据不足"), tr("玩得最多的那一个月"));
    }

    // 7. 最常玩的星期
    const QString weekday = Playtime::weekdayName(m_report.favoriteWeekday);
    setCard(6, weekday.isEmpty() ? tr("数据不足") : weekday, tr("一周里最常上线的一天"));

    // 8. 点亮成就数（登录后才拿得到）
    if (m_report.achievementCount >= 0)
    {
        setCard(7, tr("%1 个").arg(m_report.achievementCount),
                m_report.medalCount >= 0 ? tr("同时拿到 %1 枚勋章").arg(m_report.medalCount)
                                         : tr("这一年点亮的游戏成就"));
    }
    else if (PlaytimeStatsService::instance()->accountBound())
    {
        setCard(7, tr("暂无数据"), tr("账号云端还没返回成就数，稍后刷新试试"));
    }
    else
    {
        setCard(7, tr("绑定账户后可见"), tr("成就数从方块盒子账户读取"));
    }
}

void AnnualReportPage::renderChart()
{
    if (!m_chart)
        return;
    m_chart->setMonthly(m_report.monthly, m_report.busiestMonth);
}

QString AnnualReportPage::loggedInDisplayName()
{
    ForumAccountService *account = ForumAccountService::instance();
    if (!account || !account->isLoggedIn())
        return QString();
    const ForumUser user = account->user();
    const QString display = user.displayName.trimmed();
    return display.isEmpty() ? user.username.trimmed() : display;
}

QString AnnualReportPage::posterSubtitle() const
{
    if (m_fromCloud)
        return tr("账号云端 · 含所有设备");
    if (m_cloudFailed)
        return tr("本机数据 · 云端拉取失败");
    return tr("本机数据");
}

void AnnualReportPage::generatePoster()
{
    const int year = m_year > 0 ? m_year : QDate::currentDate().year();

    PlaytimeAnnualReport report = m_report;
    report.year = year;

    QString displayName = loggedInDisplayName();
    if (displayName.isEmpty())
        displayName = tr("方块盒子玩家");

    const QImage poster = AnnualReportCard::buildPoster(report, displayName, posterSubtitle());
    MedalShareDialog::show(this, poster, AnnualReportCard::suggestedFileName(year));
}
