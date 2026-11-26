/**
 * @file   AnnualReportCard.cpp
 * @brief  「我的 MC 年度报告」分享海报绘制实现
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 版式：宽 900 / 高 1500 的竖版海报，自上而下是
 *   品牌行 → 年份大字 → 总时长主视觉 → 8 个数据格（2 列 × 4 行）
 *   → 12 个月柱状图 → 页脚（方块盒子 · 昵称 / mcblockbox.top）。
 * 全部用 QPainter 手绘，字体取 QFont 默认族（跟随系统，中文不会出方框），
 * 只用 setBold 区分层级；品牌行用安装包里内嵌的官方 logo 资源（不联网、不读外部文件）。
 */

#include "AnnualReportCard.h"

#include <QDate>
#include <QDateTime>
#include <QFontMetricsF>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QStringList>

namespace
{

// ── 版式常量（整张图 900×1500，左右留白 64） ──
constexpr int kWidth = AnnualReportCard::kPosterWidth;
constexpr int kHeight = AnnualReportCard::kPosterHeight;
constexpr int kMargin = 64;
constexpr int kContentWidth = kWidth - kMargin * 2;

constexpr int kHeaderTop = 52;          ///< 品牌行
constexpr int kHeaderHeight = 46;
constexpr int kHeaderLineY = 136;       ///< 品牌行下的分隔线

constexpr int kYearTop = 152;           ///< 年份大字块
constexpr int kYearHeight = 180;

constexpr int kHeroCaptionTop = 348;    ///< 总时长主视觉
constexpr int kHeroValueTop = 374;
constexpr int kHeroValueHeight = 124;
constexpr int kHeroRangeTop = 500;
constexpr int kHeroRangeHeight = 34;

constexpr int kGridTop = 580;           ///< 8 个数据格：2 列 × 4 行
constexpr int kCellGap = 14;
constexpr int kCellWidth = (kContentWidth - kCellGap) / 2;
constexpr int kCellHeight = 106;
constexpr int kCellStep = kCellHeight + kCellGap;

constexpr int kChartTitleTop = 1086;    ///< 12 个月柱状图
constexpr int kChartTitleHeight = 34;
constexpr int kChartBaselineY = 1344;
constexpr int kChartMaxBarHeight = 198;
constexpr int kChartLabelTop = 1350;
constexpr int kChartLabelHeight = 26;

constexpr int kFooterLineY = 1408;      ///< 页脚
constexpr int kFooterNameTop = 1420;
constexpr int kFooterSubTop = 1456;

// ── 固定配色（海报不跟界面主题走，浅色 / 深色主题导出同一张图） ──
const QColor kAccent(0x48, 0xC9, 0x7E);      ///< 主题绿点缀
const QColor kInk(0xF2, 0xFA, 0xF5);         ///< 主文字
const QColor kInkSoft(0xE6, 0xF5, 0xEB, 178);
const QColor kInkMuted(0xE6, 0xF5, 0xEB, 118);
const QColor kCardFill(255, 255, 255, 16);   ///< 数据格底
const QColor kCardBorder(255, 255, 255, 26); ///< 数据格描边

QColor withAlpha(const QColor &color, int alpha)
{
    QColor out = color;
    out.setAlpha(alpha);
    return out;
}

/** 统一的字体工厂：默认族 + 像素字号 + 粗体开关，不加载任何外部字体 */
QFont pixelFont(int pixelSize, bool bold = false)
{
    QFont font = QFont();
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    return font;
}

/** 单行文字，超宽自动省略，避免长实例名把排版顶出去 */
void drawTextElided(QPainter &painter, const QRectF &rect, int flags, const QString &text,
                    const QColor &color)
{
    if (text.isEmpty())
        return;
    painter.setPen(color);
    const QFontMetricsF metrics(painter.font());
    painter.drawText(rect, flags, metrics.elidedText(text, Qt::ElideRight, rect.width()));
}

/** 背景：深绿渐变 + 右上角柔光 + 淡淡的方块网格，像一面竖起来的方块墙 */
void paintBackground(QPainter &painter, int height)
{
    QLinearGradient base(0, 0, kWidth * 0.3, height);
    base.setColorAt(0.0, QColor(0x0C, 0x3A, 0x2A));
    base.setColorAt(0.55, QColor(0x08, 0x28, 0x1D));
    base.setColorAt(1.0, QColor(0x04, 0x14, 0x0E));
    painter.fillRect(QRectF(0, 0, kWidth, height), base);

    QRadialGradient glow(QPointF(kWidth * 0.84, height * 0.03), kWidth * 0.94);
    glow.setColorAt(0.0, withAlpha(kAccent, 62));
    glow.setColorAt(0.55, withAlpha(kAccent, 18));
    glow.setColorAt(1.0, withAlpha(kAccent, 0));
    painter.fillRect(QRectF(0, 0, kWidth, height), glow);

    QRadialGradient cool(QPointF(kWidth * 0.08, height * 0.88), kWidth * 0.72);
    cool.setColorAt(0.0, QColor(0x1E, 0x5C, 0x46, 64));
    cool.setColorAt(1.0, QColor(0x1E, 0x5C, 0x46, 0));
    painter.fillRect(QRectF(0, 0, kWidth, height), cool);

    // 错行铺圆角方块当纹理：只加一点方块味，不能抢文字
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 7), 1));
    for (int y = 0; y < height; y += 72)
    {
        const int offset = ((y / 72) % 2 == 0) ? 0 : 36;
        for (int x = -72 + offset; x < kWidth; x += 72)
            painter.drawRoundedRect(QRectF(x + 4, y + 4, 64, 64), 8, 8);
    }
    painter.restore();
}

/**
 * 官方 logo：立方体 +「方块盒子 / BlockBox」白色字样，背景透明，直接压在深绿海报上。
 * 用安装包里已经内嵌的 Qt 资源读一次后缓存，不联网、不依赖外部文件。
 */
const QImage &brandLogo()
{
    static QImage logo;
    static bool loaded = false;
    if (!loaded)
    {
        loaded = true;
        logo = QImage(QStringLiteral(":/Images/blockbox_icon_dark.png"));
    }
    return logo;
}

/** 后备标志：读不到 logo 资源时手绘的绿色圆角方块 + 2×2 小方块，保证海报永远能出图 */
void paintFallbackMark(QPainter &painter, const QRectF &mark)
{
    QLinearGradient markFill(mark.topLeft(), mark.bottomRight());
    markFill.setColorAt(0.0, kAccent);
    markFill.setColorAt(1.0, kAccent.darker(138));
    painter.setPen(Qt::NoPen);
    painter.setBrush(markFill);
    painter.drawRoundedRect(mark, 12, 12);

    painter.setBrush(QColor(255, 255, 255, 236));
    for (int row = 0; row < 2; ++row)
    {
        for (int col = 0; col < 2; ++col)
        {
            painter.drawRoundedRect(
                QRectF(mark.left() + 11 + col * 13, mark.top() + 11 + row * 13, 10, 10), 3, 3);
        }
    }
}

/** 品牌行：官方 logo（含「方块盒子 / BlockBox」字样）+「年度报告」胶囊，右边是导出日期 */
void paintHeader(QPainter &painter)
{
    // logo 自带品牌字样，所以整行以品牌行（kHeaderTop ~ 分隔线）的高度居中
    const qreal centerY = (kHeaderTop + kHeaderLineY) / 2.0;
    const qreal logoHeight = 72.0;
    qreal brandRight = kMargin;

    const QImage &logo = brandLogo();
    if (!logo.isNull())
    {
        const qreal logoWidth = logoHeight * logo.width() / logo.height();
        const QRectF logoRect(kMargin, centerY - logoHeight / 2.0, logoWidth, logoHeight);
        painter.drawImage(logoRect, logo);
        brandRight = logoRect.right();
    }
    else
    {
        // 资源缺失时的后备：绿色方块标 + 文字，版式和以前一致
        const QRectF mark(kMargin, centerY - kHeaderHeight / 2.0, kHeaderHeight, kHeaderHeight);
        paintFallbackMark(painter, mark);

        QFont brandFont = pixelFont(26, true);
        painter.setFont(brandFont);
        painter.setPen(kInk);
        const QFontMetricsF brandMetrics(brandFont);
        const QString brandText = QStringLiteral("方块盒子");
        const qreal brandWidth = brandMetrics.horizontalAdvance(brandText);
        painter.drawText(QRectF(mark.right() + 16, centerY - 18, brandWidth + 4, 36),
                         Qt::AlignLeft | Qt::AlignVCenter, brandText);
        brandRight = mark.right() + 16 + brandWidth;
    }

    // 「年度报告」胶囊
    const QString chipText = QStringLiteral("年度报告");
    QFont chipFont = pixelFont(20, true);
    painter.setFont(chipFont);
    const QFontMetricsF chipMetrics(chipFont);
    const qreal chipWidth = chipMetrics.horizontalAdvance(chipText) + 28;
    const QRectF chipRect(brandRight + 18, centerY - 17, chipWidth, 34);
    painter.setPen(QPen(withAlpha(kAccent, 112), 1));
    painter.setBrush(withAlpha(kAccent, 38));
    painter.drawRoundedRect(chipRect, 17, 17);
    painter.setPen(kAccent);
    painter.drawText(chipRect, Qt::AlignCenter, chipText);

    painter.setFont(pixelFont(18));
    painter.setPen(kInkMuted);
    painter.drawText(QRectF(kWidth - kMargin - 260, centerY - 16, 260, 32),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));

    painter.fillRect(QRectF(kMargin, kHeaderLineY, kContentWidth, 1), QColor(255, 255, 255, 24));
}

/** 年份大字 + 右侧「年度报告」胶囊和数据来源小字 */
void paintYearBlock(QPainter &painter, int year, const QString &subtitle)
{
    painter.setFont(pixelFont(128, true));
    painter.setPen(kInk);
    painter.drawText(QRectF(kMargin, kYearTop, 520, kYearHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, QString::number(year));

    const qreal rightEdge = kWidth - kMargin;

    const QString chipText = QStringLiteral("年度报告");
    QFont chipFont = pixelFont(22, true);
    painter.setFont(chipFont);
    const QFontMetricsF chipMetrics(chipFont);
    const qreal chipWidth = chipMetrics.horizontalAdvance(chipText) + 34;
    const QRectF chipRect(rightEdge - chipWidth, kYearTop + 46, chipWidth, 44);
    painter.setPen(QPen(withAlpha(kAccent, 128), 1));
    painter.setBrush(withAlpha(kAccent, 40));
    painter.drawRoundedRect(chipRect, 22, 22);
    painter.setPen(kAccent);
    painter.drawText(chipRect, Qt::AlignCenter, chipText);

    if (!subtitle.trimmed().isEmpty())
    {
        painter.setFont(pixelFont(19));
        drawTextElided(painter, QRectF(rightEdge - 330, kYearTop + 104, 330, 30),
                       Qt::AlignRight | Qt::AlignVCenter, subtitle.trimmed(), kInkSoft);
    }
}

/** 总时长主视觉：大号时长 + 一行日期区间 / 启动次数 */
void paintHero(QPainter &painter, const PlaytimeAnnualReport &report)
{
    painter.setFont(pixelFont(21, true));
    painter.setPen(kAccent);
    painter.drawText(QRectF(kMargin, kHeroCaptionTop, 420, 28),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("总游玩时长"));

    // 右侧留出 28px 给那根亮绿装饰条，超长的时长也不会顶上去
    painter.setFont(pixelFont(84, true));
    drawTextElided(painter,
                   QRectF(kMargin, kHeroValueTop, kContentWidth - 28, kHeroValueHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, Playtime::formatHours(report.totalSeconds),
                   kInk);

    QString rangeText;
    if (report.firstPlayedOn.isValid() && report.lastPlayedOn.isValid())
    {
        rangeText = QStringLiteral("%1 ~ %2 · 共 %3 次启动")
                        .arg(report.firstPlayedOn.toString(QStringLiteral("yyyy-MM-dd")),
                             report.lastPlayedOn.toString(QStringLiteral("yyyy-MM-dd")))
                        .arg(qMax(0, report.launchCount));
    }
    else
    {
        rangeText = QStringLiteral("这一年还没有游玩记录，进游戏玩一次就会出现在这里");
    }

    painter.setFont(pixelFont(20));
    drawTextElided(painter, QRectF(kMargin, kHeroRangeTop, kContentWidth, kHeroRangeHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, rangeText, kInkMuted);

    // 主视觉左侧一小段竖着的亮绿装饰，截图发出去有个「证书」的边角感
    painter.setPen(Qt::NoPen);
    painter.setBrush(withAlpha(kAccent, 200));
    painter.drawRoundedRect(QRectF(kWidth - kMargin - 4, kHeroValueTop + 8, 4,
                                   kHeroValueHeight - 16),
                            2, 2);
}

/** 一个数据格：小标题 + 大数字 / 名字 + 一行补充说明 */
void paintStatCell(QPainter &painter, const QRectF &cell, const QString &title,
                   const QString &value, const QString &hint)
{
    painter.setPen(QPen(kCardBorder, 1));
    painter.setBrush(kCardFill);
    painter.drawRoundedRect(cell, 18, 18);

    const qreal innerLeft = cell.left() + 20;
    const qreal innerWidth = cell.width() - 40;

    painter.setFont(pixelFont(19));
    drawTextElided(painter, QRectF(innerLeft, cell.top() + 13, innerWidth, 24),
                   Qt::AlignLeft | Qt::AlignVCenter, title, kInkMuted);

    painter.setFont(pixelFont(27, true));
    drawTextElided(painter, QRectF(innerLeft, cell.top() + 39, innerWidth, 38),
                   Qt::AlignLeft | Qt::AlignVCenter, value, kInk);

    if (!hint.isEmpty())
    {
        painter.setFont(pixelFont(16));
        drawTextElided(painter, QRectF(innerLeft, cell.top() + 78, innerWidth, 22),
                       Qt::AlignLeft | Qt::AlignVCenter, hint, kInkMuted);
    }
}

/** 8 个数据格：2 列 × 4 行，顺序与年度报告页一致 */
void paintStatGrid(QPainter &painter, const PlaytimeAnnualReport &report)
{
    struct Cell
    {
        QString title;
        QString value;
        QString hint;
    };

    QVector<Cell> cells;
    cells.reserve(8);

    // 1. 启动次数
    {
        const int launches = qMax(0, report.launchCount);
        // 不足 1 次时写「不到 1 次」，写成「0 次」看着像没玩过
        const double monthlyAverage = launches / 12.0;
        const QString launchHint =
            launches <= 0
                ? QStringLiteral("这一年还没启动过游戏")
                : (monthlyAverage >= 1.0
                       ? QStringLiteral("平均每月 %1 次")
                             .arg(QString::number(monthlyAverage, 'f', 1))
                       : QStringLiteral("平均每月不到 1 次"));
        cells.append({QStringLiteral("启动次数"), QStringLiteral("%1 次").arg(launches),
                      launchHint});
    }
    // 2. 活跃天数
    {
        const int days = qMax(0, report.activeDays);
        const QString hint = days > 0
                                 ? QStringLiteral("平均每天 %1")
                                       .arg(Playtime::formatDuration(report.totalSeconds / days))
                                 : QStringLiteral("这一年还没开始玩");
        cells.append({QStringLiteral("活跃天数"), QStringLiteral("%1 天").arg(days), hint});
    }
    // 3. 最长一次
    {
        const QString hint = report.longestSessionDay.isValid()
                                 ? report.longestSessionDay.toString(QStringLiteral("M 月 d 日"))
                                 : QStringLiteral("—");
        cells.append({QStringLiteral("最长一次"),
                      report.longestSessionSeconds > 0
                          ? Playtime::formatDuration(report.longestSessionSeconds)
                          : QStringLiteral("—"),
                      hint});
    }
    // 4. 最爱实例
    {
        const QString name = report.favoriteInstance.trimmed();
        const QString hint = !report.favoriteVersion.trimmed().isEmpty()
                                 ? report.favoriteVersion.trimmed()
                                 : Playtime::formatDuration(report.favoriteInstanceSeconds);
        cells.append({QStringLiteral("最爱实例"), name.isEmpty() ? QStringLiteral("—") : name,
                      name.isEmpty() ? QStringLiteral("还没有记录") : hint});
    }
    // 5. 最常玩的时段
    {
        const QString text = Playtime::hourRangeText(report.favoriteHour);
        cells.append({QStringLiteral("最常玩的时段"),
                      text.isEmpty() ? QStringLiteral("—") : text,
                      QStringLiteral("按开始时间统计")});
    }
    // 6. 最活跃月份
    {
        const QString text = report.busiestMonth >= 1 && report.busiestMonth <= 12
                                 ? QStringLiteral("%1 月").arg(report.busiestMonth)
                                 : QStringLiteral("—");
        cells.append({QStringLiteral("最活跃月份"), text,
                      report.busiestMonthSeconds > 0
                          ? Playtime::formatDuration(report.busiestMonthSeconds)
                          : QStringLiteral("数据不足")});
    }
    // 7. 最常玩的星期
    {
        const QString text = Playtime::weekdayName(report.favoriteWeekday);
        cells.append({QStringLiteral("最常玩的星期"),
                      text.isEmpty() ? QStringLiteral("—") : text,
                      QStringLiteral("每周最常上线的一天")});
    }
    // 8. 点亮成就
    {
        const QString value = report.achievementCount >= 0
                                  ? QStringLiteral("%1 个").arg(report.achievementCount)
                                  : QStringLiteral("—");
        const QString hint = report.achievementCount >= 0
                                 ? QStringLiteral("这一年点亮的游戏成就")
                                 : QStringLiteral("绑定方块盒子账户后可见");
        cells.append({QStringLiteral("点亮成就"), value, hint});
    }

    for (int index = 0; index < cells.size(); ++index)
    {
        const int row = index / 2;
        const int col = index % 2;
        const QRectF cell(kMargin + col * (kCellWidth + kCellGap), kGridTop + row * kCellStep,
                          kCellWidth, kCellHeight);
        paintStatCell(painter, cell, cells.at(index).title, cells.at(index).value,
                      cells.at(index).hint);
    }
}

/** 12 个月柱状图：最活跃的月份用主题绿点亮，其余是半透明白 */
void paintMonthChart(QPainter &painter, const PlaytimeAnnualReport &report)
{
    painter.setFont(pixelFont(25, true));
    painter.setPen(kInk);
    painter.drawText(QRectF(kMargin, kChartTitleTop, 420, kChartTitleHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("每月游玩时长"));

    painter.setFont(pixelFont(18));
    painter.setPen(kInkMuted);
    painter.drawText(QRectF(kWidth - kMargin - 260, kChartTitleTop + 4, 260,
                            kChartTitleHeight - 8),
                     Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("单位：小时"));

    // 缺月按 0 处理：服务端没给 monthly 时也能画出一整排
    qint64 monthSeconds[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    qint64 maxSeconds = 0;
    for (int month = 0; month < 12; ++month)
    {
        if (month < report.monthly.size())
            monthSeconds[month] = qMax<qint64>(0, report.monthly.at(month).seconds);
        maxSeconds = qMax(maxSeconds, monthSeconds[month]);
    }

    const qreal slotWidth = qreal(kContentWidth) / 12.0;
    const qreal barWidth = 30.0;

    // 三条淡横线当刻度，只有基线是实的
    painter.setPen(QPen(QColor(255, 255, 255, 13), 1));
    for (int line = 1; line <= 3; ++line)
    {
        const qreal y = kChartBaselineY - kChartMaxBarHeight * line / 3.0;
        painter.drawLine(QPointF(kMargin, y), QPointF(kMargin + kContentWidth, y));
    }
    painter.setPen(QPen(QColor(255, 255, 255, 44), 1));
    painter.drawLine(QPointF(kMargin, kChartBaselineY),
                     QPointF(kMargin + kContentWidth, kChartBaselineY));

    for (int month = 0; month < 12; ++month)
    {
        const qreal centerX = kMargin + slotWidth * month + slotWidth / 2.0;
        const qreal barLeft = centerX - barWidth / 2.0;

        qreal barHeight = 2.0;
        if (maxSeconds > 0 && monthSeconds[month] > 0)
            barHeight = qMax(4.0, kChartMaxBarHeight * qreal(monthSeconds[month]) / maxSeconds);

        const bool highlight = report.busiestMonth >= 1 && report.busiestMonth <= 12
                               && month + 1 == report.busiestMonth && monthSeconds[month] > 0;

        if (highlight)
        {
            QLinearGradient fill(0, kChartBaselineY - barHeight, 0, kChartBaselineY);
            fill.setColorAt(0.0, kAccent.lighter(112));
            fill.setColorAt(1.0, kAccent.darker(118));
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
        }
        else
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(monthSeconds[month] > 0 ? QColor(255, 255, 255, 58)
                                                     : QColor(255, 255, 255, 24));
        }
        painter.drawRoundedRect(
            QRectF(barLeft, kChartBaselineY - barHeight, barWidth, barHeight), 5, 5);

        painter.setFont(pixelFont(15, highlight));
        painter.setPen(highlight ? kAccent : kInkMuted);
        painter.drawText(QRectF(centerX - slotWidth / 2.0, kChartLabelTop, slotWidth,
                                kChartLabelHeight),
                         Qt::AlignCenter, QString::number(month + 1));
    }

    if (maxSeconds <= 0)
    {
        painter.setFont(pixelFont(19));
        painter.setPen(kInkMuted);
        painter.drawText(QRectF(kMargin, kChartBaselineY - kChartMaxBarHeight / 2 - 14,
                                kContentWidth, 30),
                         Qt::AlignCenter, QStringLiteral("还没有按月统计的游玩数据"));
    }
}

/** 页脚：左边「方块盒子 · 昵称」+ 一行副信息，右下角 mcblockbox.top */
void paintFooter(QPainter &painter, const PlaytimeAnnualReport &report, const QString &displayName)
{
    painter.fillRect(QRectF(kMargin, kFooterLineY, kContentWidth, 1), QColor(255, 255, 255, 22));

    const QString name = displayName.trimmed().isEmpty() ? QStringLiteral("方块盒子玩家")
                                                         : displayName.trimmed();
    painter.setFont(pixelFont(24, true));
    drawTextElided(painter, QRectF(kMargin, kFooterNameTop, 600, 32),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("方块盒子 · %1").arg(name), kInk);

    painter.setFont(pixelFont(16));
    drawTextElided(painter, QRectF(kMargin, kFooterSubTop, 620, 26),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("MC 年度报告 · %1 · 共 %2 次启动")
                       .arg(report.year > 0 ? report.year : QDate::currentDate().year())
                       .arg(qMax(0, report.launchCount)),
                   kInkMuted);

    painter.setFont(pixelFont(17));
    painter.setPen(kInkSoft);
    painter.drawText(QRectF(kWidth - kMargin - 280, kFooterSubTop, 280, 26),
                     Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("mcblockbox.top"));
}

} // namespace

namespace AnnualReportCard
{

QImage buildPoster(const PlaytimeAnnualReport &report, const QString &displayName,
                   const QString &subtitle)
{
    QImage image(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(0x04, 0x14, 0x0E));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const int year = report.year > 0 ? report.year : QDate::currentDate().year();

    paintBackground(painter, kHeight);
    painter.fillRect(QRectF(0, 0, kWidth, 5), withAlpha(kAccent, 210));

    paintHeader(painter);
    paintYearBlock(painter, year, subtitle);
    paintHero(painter, report);
    paintStatGrid(painter, report);
    paintMonthChart(painter, report);
    paintFooter(painter, report, displayName);

    painter.end();
    return image;
}

QString suggestedFileName(int year)
{
    const int shownYear = year > 0 ? year : QDate::currentDate().year();
    return QStringLiteral("方块盒子-MC年度报告-%1-%2.png")
        .arg(shownYear)
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")));
}

} // namespace AnnualReportCard
