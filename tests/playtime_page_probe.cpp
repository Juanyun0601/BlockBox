/**
 * @file   playtime_page_probe.cpp
 * @brief  游玩统计页 / 年度报告页样式探针：真实页面 + 真实主题 QSS，截图核对排版
 *
 * 不启动 MainWindow，只把两个新页面放进空窗口，用一份写好的演示数据
 * （playtime/stats.json 同格式）喂给 PlaytimeStatsService，然后亮 / 暗主题各截一张图：
 *   1. 统计页：概览大数字 / 14 天柱状图 / 年度报告入口 / 实例排行 / 账号卡 / 最近会话；
 *   2. 年度报告页：年份胶囊 / 主视觉 / 8 张数据小卡 / 12 个月柱状图；
 *   3. 分享海报：AnnualReportCard::buildPoster 出图尺寸 900x1500。
 *
 * 输出：.tmp_playtime_probe/probe_out/playtime_*.png
 * 退出码 0 = 全部断言通过。
 */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QSet>
#include <QScrollArea>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QWidget>

#include <cstdio>

#include "components/AnnualReportCard.h"
#include "pages/AnnualReportPage.h"
#include "pages/ForumAccountPage.h"
#include "pages/PlaytimeStatsPage.h"
#include "utils/PlaytimeStats.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

namespace
{

int g_failures = 0;

void expect(const char *label, bool condition, const QString &detail = QString())
{
    fprintf(stderr, "[probe] %-52s %s%s\n", label, condition ? "OK" : "FAIL",
            condition || detail.isEmpty() ? "" : qPrintable(QStringLiteral("  <- ") + detail));
    if (!condition)
        ++g_failures;
}

bool savePng(QWidget *widget, const QString &path)
{
    if (!widget)
        return false;
    const QPixmap shot = widget->grab();
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(), shot.height(),
            ok ? "ok" : "FAILED");
    return ok;
}

/** 图里出现的不同颜色数：用来挡「整页空白 / 只有一个底色」的假成功 */
int distinctColors(const QImage &image)
{
    QSet<QRgb> colors;
    for (int y = 0; y < image.height(); y += 7)
    {
        for (int x = 0; x < image.width(); x += 7)
            colors.insert(image.pixel(x, y) | 0xff000000);
    }
    return colors.size();
}

/** 页面里不该残留未解析的样式令牌（残留会让 Qt 整条规则失效） */
bool hasUnresolvedToken(QWidget *root)
{
    if (!root)
        return true;
    const QStringList widgets = {QString()};
    Q_UNUSED(widgets);
    if (root->styleSheet().contains(QStringLiteral("@")))
        return true;
    for (QWidget *child : root->findChildren<QWidget *>())
    {
        if (child->styleSheet().contains(QStringLiteral("@")))
            return true;
    }
    return false;
}

void pump(int rounds = 12)
{
    for (int i = 0; i < rounds; ++i)
    {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

QJsonArray makeHours(int peakHour, qint64 peakSeconds)
{
    QJsonArray hours;
    for (int hour = 0; hour < 24; ++hour)
    {
        qint64 value = 0;
        const int distance = qAbs(hour - peakHour);
        if (distance == 0)
            value = peakSeconds;
        else if (distance == 1)
            value = peakSeconds / 2;
        else if (distance == 2)
            value = peakSeconds / 5;
        hours.append(static_cast<double>(value));
    }
    return hours;
}

QJsonArray makeWeekdays(const QVector<qint64> &values)
{
    QJsonArray weekdays;
    for (qint64 value : values)
        weekdays.append(static_cast<double>(value));
    return weekdays;
}

QJsonObject makeDayMap(const QVector<QPair<QString, qint64>> &entries)
{
    QJsonObject days;
    for (const auto &entry : entries)
        days.insert(entry.first, static_cast<double>(entry.second));
    return days;
}

QJsonObject makeYear(int year, const QString &mainInstance, qint64 mainSeconds,
                     const QVector<QPair<QString, qint64>> &days, int peakHour)
{
    qint64 total = 0;
    QJsonObject daySessions;
    for (const auto &entry : days)
    {
        total += entry.second;
        daySessions.insert(entry.first, entry.second > 3600 ? 2 : 1);
    }
    QJsonObject instanceSeconds;
    instanceSeconds.insert(mainInstance, static_cast<double>(mainSeconds));
    instanceSeconds.insert(QStringLiteral("1.20.1-Forge"), static_cast<double>(total - mainSeconds));
    QJsonObject instanceSessions;
    instanceSessions.insert(mainInstance, static_cast<double>(qMax(1, int(days.size()) - 1)));
    instanceSessions.insert(QStringLiteral("1.20.1-Forge"), 1);

    QJsonObject longest;
    longest.insert(QStringLiteral("seconds"), static_cast<double>(qMin<qint64>(total, 12600)));
    longest.insert(QStringLiteral("day"), days.isEmpty() ? QString() : days.last().first);
    longest.insert(QStringLiteral("instance"), mainInstance);

    QJsonObject object;
    object.insert(QStringLiteral("seconds"), static_cast<double>(total));
    object.insert(QStringLiteral("sessions"), days.size());
    object.insert(QStringLiteral("launches"), days.size());
    object.insert(QStringLiteral("days"), makeDayMap(days));
    object.insert(QStringLiteral("day_sessions"), daySessions);
    object.insert(QStringLiteral("instances"), instanceSeconds);
    object.insert(QStringLiteral("instance_sessions"), instanceSessions);
    object.insert(QStringLiteral("hours"), makeHours(peakHour, total / 3));
    object.insert(QStringLiteral("weekdays"), makeWeekdays({total / 10, total / 8, total / 7,
                                                            total / 6, total / 5, total / 4,
                                                            total / 9}));
    object.insert(QStringLiteral("longest"), longest);
    object.insert(QStringLiteral("first"), days.isEmpty() ? QString() : days.first().first);
    object.insert(QStringLiteral("last"), days.isEmpty() ? QString() : days.last().first);
    Q_UNUSED(year);
    return object;
}

/** 按 PlaytimeStatsService 的本地存储格式写一份演示数据 */
void writeDemoData(const QString &path)
{
    QJsonObject instances;
    QJsonObject fabric;
    fabric.insert(QStringLiteral("name"), QStringLiteral("1.21.9-Fabric"));
    fabric.insert(QStringLiteral("version"), QStringLiteral("1.21.9"));
    fabric.insert(QStringLiteral("loader"), QStringLiteral("Fabric"));
    fabric.insert(QStringLiteral("last"), 1795000000.0);
    instances.insert(QStringLiteral("D:/Games/.minecraft/versions/1.21.9-Fabric"), fabric);
    QJsonObject forge;
    forge.insert(QStringLiteral("name"), QStringLiteral("1.20.1-Forge"));
    forge.insert(QStringLiteral("version"), QStringLiteral("1.20.1"));
    forge.insert(QStringLiteral("loader"), QStringLiteral("Forge"));
    forge.insert(QStringLiteral("last"), 1794000000.0);
    instances.insert(QStringLiteral("D:/Games/.minecraft/versions/1.20.1-Forge"), forge);

    // 2026：今天之前的一串日子（含今天，曲线才不会是空的）
    QVector<QPair<QString, qint64>> days2026;
    const QDate today = QDate::currentDate();
    const qint64 pattern[] = {5400, 0, 7200, 3600, 0, 10800, 4500, 0, 6300, 9000, 0, 3000, 8100, 6600};
    for (int i = 13; i >= 0; --i)
    {
        const QDate day = today.addDays(-i);
        const qint64 seconds = pattern[(i + 1) % 14];
        if (seconds > 0)
            days2026.append({day.toString(QStringLiteral("yyyy-MM-dd")), seconds});
    }

    QJsonObject years;
    years.insert(QStringLiteral("2026"),
                 makeYear(2026, QStringLiteral("1.21.9-Fabric"), 62000, days2026, 20));

    QVector<QPair<QString, qint64>> days2025;
    for (int month = 1; month <= 12; ++month)
    {
        days2025.append({QStringLiteral("2025-%1-12").arg(month, 2, 10, QLatin1Char('0')),
                         3600LL * (month % 5 + 1)});
    }
    years.insert(QStringLiteral("2025"),
                 makeYear(2025, QStringLiteral("1.20.1-Forge"), 42000, days2025, 21));

    // 最近会话列表
    QJsonArray sessions;
    const struct
    {
        int daysAgo;
        int seconds;
        const char *instance;
        const char *save;
        int room;
    } recent[] = {
        {0, 7200, "1.21.9-Fabric", "海边的橡木小屋", 0},
        {1, 5400, "1.21.9-Fabric", "生电基地", 0},
        {3, 3600, "1.20.1-Forge", "", 12882},
        {4, 10800, "1.20.1-Forge", "我的世界生存", 0},
        {6, 2700, "1.21.9-Fabric", "创造飞行试验场", 0},
        {9, 9000, "1.21.9-Fabric", "空岛挑战", 0},
    };
    for (const auto &item : recent)
    {
        const QDateTime start = QDateTime(QDate::currentDate().addDays(-item.daysAgo),
                                          QTime(20, 10));
        QJsonObject session;
        session.insert(QStringLiteral("id"),
                       QStringLiteral("demo-%1-%2").arg(item.daysAgo).arg(item.seconds));
        session.insert(QStringLiteral("start"), double(start.toSecsSinceEpoch()));
        session.insert(QStringLiteral("end"), double(start.toSecsSinceEpoch() + item.seconds));
        session.insert(QStringLiteral("seconds"), item.seconds);
        session.insert(QStringLiteral("instance_path"),
                       QStringLiteral("D:/Games/.minecraft/versions/") +
                           QString::fromLatin1(item.instance));
        session.insert(QStringLiteral("instance_name"), QString::fromLatin1(item.instance));
        session.insert(QStringLiteral("version"),
                       QString::fromLatin1(item.instance).startsWith(QStringLiteral("1.21"))
                           ? QStringLiteral("1.21.9")
                           : QStringLiteral("1.20.1"));
        session.insert(QStringLiteral("loader"),
                       QString::fromLatin1(item.instance).startsWith(QStringLiteral("1.21"))
                           ? QStringLiteral("Fabric")
                           : QStringLiteral("Forge"));
        session.insert(QStringLiteral("save"), QString::fromLatin1(item.save));
        session.insert(QStringLiteral("room_id"), item.room);
        session.insert(QStringLiteral("synced"), false);
        sessions.append(session);
    }

    QJsonObject meta;
    meta.insert(QStringLiteral("first_day"), QStringLiteral("2024-03-02"));
    meta.insert(QStringLiteral("last_day"), today.toString(QStringLiteral("yyyy-MM-dd")));
    meta.insert(QStringLiteral("launches"),
                days2026.size() + days2025.size());
    meta.insert(QStringLiteral("sessions"),
                days2026.size() + days2025.size());
    meta.insert(QStringLiteral("last_sync_at"), 0.0);

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("meta"), meta);
    root.insert(QStringLiteral("instances"), instances);
    root.insert(QStringLiteral("years"), years);
    root.insert(QStringLiteral("sessions"), sessions);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        fprintf(stderr, "[probe] 演示数据写入失败：%s\n", qPrintable(path));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    const QString workDir = QStringLiteral(".tmp_playtime_probe");
    const QString outDir = workDir + QStringLiteral("/probe_out");
    QDir().mkpath(outDir);

    const QString dataPath = workDir + QStringLiteral("/playtime_demo.json");
    writeDemoData(dataPath);
    PlaytimeStatsService::setDataFileOverrideForTesting(dataPath);
    PlaytimeStatsService::instance()->reloadFromDisk();

    // 注意：换主题后要重建页面再截图。页面里的内联样式在创建时按当时主题解析，
    // 只切 QSS 不重建的话，截图里的文字颜色还是上一个主题的（会误判成对比度问题）。
    auto makeHost = [](const QString &objectName) {
        auto *host = new QWidget;
        host->resize(1280, 900);
        host->setObjectName(objectName);
        host->setStyleSheet(
            StyleKit::resolve(QStringLiteral("QWidget#%1 { background: @BG_BASE@; }")
                                  .arg(objectName)));
        return host;
    };

    const QStringList themeNames = {QStringLiteral("light"), QStringLiteral("dark")};
    const QVector<ThemeManager::ThemeType> themeTypes = {ThemeManager::LightTheme,
                                                         ThemeManager::DarkTheme};
    for (int i = 0; i < themeNames.size(); ++i)
    {
        ThemeManager::instance()->loadTheme(themeTypes.at(i));
        pump();

        // ── 统计页 ──
        QWidget *statsHost = makeHost(QStringLiteral("statsProbeHost"));
        auto *statsPage = new PlaytimeStatsPage(statsHost);
        statsPage->setGeometry(0, 0, 1280, 900);
        statsPage->refresh();
        pump();
        expect("统计页：已经渲染出内容", distinctColors(statsPage->grab().toImage()) > 24,
               QStringLiteral("diff colors=%1").arg(distinctColors(statsPage->grab().toImage())));
        expect("统计页：样式令牌全部解析", !hasUnresolvedToken(statsPage));
        savePng(statsPage, outDir + QStringLiteral("/stats_") + themeNames.at(i) +
                               QStringLiteral(".png"));
        delete statsHost;
        pump();

        // ── 年度报告页 ──
        QWidget *reportHost = makeHost(QStringLiteral("reportProbeHost"));
        auto *reportPage = new AnnualReportPage(reportHost);
        reportPage->setGeometry(0, 0, 1280, 900);
        reportPage->openPage(QDate::currentDate().year());
        pump(20);
        expect("年度报告页：已经渲染出内容", distinctColors(reportPage->grab().toImage()) > 24);
        expect("年度报告页：样式令牌全部解析", !hasUnresolvedToken(reportPage));
        savePng(reportPage, outDir + QStringLiteral("/report_") + themeNames.at(i) +
                                QStringLiteral(".png"));
        delete reportHost;
        pump();
    }

    // 嵌入模式：账户管理页「游玩」页签里不自带滚动区（滚动由外层页面负责）
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    pump();
    QWidget *embedHost = makeHost(QStringLiteral("embedProbeHost"));
    auto *embedPage = new PlaytimeStatsPage(embedHost, /*embedded=*/true);
    embedPage->setGeometry(0, 0, 1160, 1500);
    embedPage->refresh();
    pump(16);
    expect("嵌入模式：页面里没有自己的滚动区",
           embedPage->findChild<QScrollArea *>() == nullptr);
    expect("嵌入模式：已经渲染出内容", distinctColors(embedPage->grab().toImage()) > 24);
    expect("嵌入模式：样式令牌全部解析", !hasUnresolvedToken(embedPage));
    savePng(embedPage, outDir + QStringLiteral("/stats_embedded_light.png"));
    delete embedHost;
    pump();
    // 账户管理页：确认「游玩」页签确实挂上了统计页（整页不显示窗口，避免触发首次引导）
    {
        QWidget *accountHost = makeHost(QStringLiteral("accountProbeHost"));
        accountHost->resize(1280, 1400);
        auto *account = new ForumAccountPage(accountHost);
        account->setGeometry(0, 0, 1280, 1400);
        QPushButton *playtimeTab = nullptr;
        for (QPushButton *button : account->findChildren<QPushButton *>())
        {
            if (button->text() == QStringLiteral("游玩"))
            {
                playtimeTab = button;
                break;
            }
        }
        expect("账户管理页：有「游玩」页签", playtimeTab != nullptr);
        if (playtimeTab)
        {
            playtimeTab->click();
            pump(24);
            bool hasTitle = false;
            for (QLabel *label : account->findChildren<QLabel *>())
            {
                if (label->text().contains(QStringLiteral("本机游玩时长")))
                {
                    hasTitle = true;
                    break;
                }
            }
            expect("账户管理页：游玩页签里能看到本机统计", hasTitle);
            expect("账户管理页：游玩页签样式令牌全部解析", !hasUnresolvedToken(account));
            savePng(account, outDir + QStringLiteral("/account_playtime_tab.png"));
        }
        delete accountHost;
        pump();
    }
    // 分享海报：直接出图并校验尺寸
    const PlaytimeAnnualReport report =
        PlaytimeStatsService::instance()->annualReport(QDate::currentDate().year());
    const QImage poster = AnnualReportCard::buildPoster(report, QStringLiteral("方块盒子玩家"),
                                                        QStringLiteral("本机数据"));
    expect("海报：尺寸 900x1500", poster.width() == AnnualReportCard::kPosterWidth &&
                                     poster.height() == AnnualReportCard::kPosterHeight,
           QStringLiteral("%1x%2").arg(poster.width()).arg(poster.height()));
    expect("海报：不是纯色图", distinctColors(poster) > 24);
    const QString posterPath = outDir + QStringLiteral("/poster.png");
    expect("海报：保存成功", poster.save(posterPath));
    fprintf(stderr, "[probe] shot %s %dx%d\n", qPrintable(posterPath), poster.width(),
            poster.height());

    // 时长文案：0 秒不该说「不到 1 分钟」
    expect("文案：0 秒显示 0 分钟",
           Playtime::formatDuration(0) == QStringLiteral("0 分钟"),
           Playtime::formatDuration(0));

    fprintf(stderr, "[probe] %s（失败 %d 项）\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED",
            g_failures);
    return g_failures == 0 ? 0 : 1;
}