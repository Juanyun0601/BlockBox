/**
 * @file   memory_page_probe.cpp
 * @brief  内存优化页视觉探针：渲染真实 MemoryOptimizePage + 真实 style.qss
 *
 * 只把 ThemeManager / SettingsManager / SystemTools 换成桩（真实实现会拉入
 * BackgroundManager、PluginManager、MultiThreadDownloader 等重依赖），
 * 页面代码、QSS、图标资源都用真的。
 *
 * 用法：qmake memory_page_probe.pro && mingw32-make && release/memory_page_probe.exe
 * 截图输出：D:/BlockBox/_out/memory_page_*.png
 */

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QVBoxLayout>

#include <cstdio>

#include "components/CustomCheckBox.h"
#include "pages/MemoryOptimizePage.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

namespace {

int g_parseWarnings = 0;
int g_fail = 0;

void msgHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    if (msg.contains(QLatin1String("Could not parse")))
        ++g_parseWarnings;
    std::fprintf(stderr, "[qt ] %s\n", msg.toLocal8Bit().constData());
}

void expect(bool ok, const char *what)
{
    std::fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++g_fail;
}

/** 与 ThemeManager::buildTokenTable 同族的 token 表（探针精简版） */
QString resolveTokens(QString style, bool dark)
{
    struct KV {
        const char *k;
        const char *v;
    };
    const QVector<KV> light = {
        {"@BG_BASE@", "#f5f5f5"}, {"@BG_CARD@", "#ffffff"},
        {"@BG_CONTENT@", "#fafafa"}, {"@BG_MUTED@", "#f8f9fa"},
        {"@BG_HOVER@", "#eaeaea"}, {"@BG_PRESSED@", "#e0e0e0"},
        {"@BG_TRACK@", "#e8e8e8"}, {"@BG_TAB@", "#f0f0f0"},
        {"@BG_DISABLED@", "#d5d7db"}, {"@BG_MSG@", "#edf0f4"},
        {"@BG_GLASS@", "rgba(255, 255, 255, 0.72)"},
        {"@BG_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.42)"},
        {"@BORDER_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.55)"},
        {"@BORDER@", "#e8eaed"}, {"@BORDER_STRONG@", "#dadce0"},
        {"@BORDER_HOVER@", "#bdbdbd"}, {"@BORDER_DASHED@", "#c4c7c5"},
        {"@BORDER_DISABLED@", "#b0b0b0"}, {"@BORDER_LIGHT@", "#f0f1f3"},
        {"@TEXT_SECONDARY@", "#666666"}, {"@TEXT_TERTIARY@", "#888888"},
        {"@TEXT_DISABLED@", "#bbbbbb"}, {"@TEXT_PRIMARY@", "#333333"},
        {"@TEXT_BORDER@", "#e8eaed"}, {"@TEXT_ACCENT@", "#1b5e20"},
        {"@PRIMARY_BG@", "#e8f5e9"},
        {"@BLACK_RGBA_0.06@", "rgba(0, 0, 0, 0.06)"},
        {"@BLACK_RGBA_0.08@", "rgba(0, 0, 0, 0.08)"},
        {"@BLACK_RGBA_0.10@", "rgba(0, 0, 0, 0.10)"},
        {"@BLACK_RGBA_0.13@", "rgba(0, 0, 0, 0.13)"},
        {"@BLACK_RGBA_0.25@", "rgba(0, 0, 0, 0.25)"},
        {"@PRIMARY@", "#2E7D32"},
        {"@PRIMARY_HOVER@", "#256428"},
        {"@PRIMARY_PRESSED@", "#1B5E20"},
        {"@PRIMARY_DARK@", "#1B5E20"},
        {"@PRIMARY_RGBA_12@", "rgba(46, 125, 50, 0.12)"},
        {"@PRIMARY_RGBA_20@", "rgba(46, 125, 50, 0.2)"},
        {"@PRIMARY_RGBA_38@", "rgba(46, 125, 50, 0.38)"},
        {"@TEXT_ON_PRIMARY@", "#ffffff"},
        {"@SUCCESS@", "#059669"}, {"@INFO@", "#2196F3"},
        {"@WARNING@", "#F57C00"}, {"@DANGER@", "#D32F2F"},
        {"@WARNING_TEXT_DARK@", "#856404"},
        {"@WARNING_RGBA_26@", "rgba(255, 152, 0, 0.26)"},
    };
    const QVector<KV> darkOverride = {
        {"@BG_BASE@", "#1a1a1a"}, {"@BG_CARD@", "#2d2d2d"},
        {"@BG_CONTENT@", "#1f1f1f"}, {"@BG_MUTED@", "#2d2d2d"},
        {"@BG_HOVER@", "#3d3d3d"}, {"@BG_PRESSED@", "#4a4a4a"},
        {"@BG_TRACK@", "#4a4a4a"}, {"@BG_TAB@", "#252525"},
        {"@BG_DISABLED@", "#3d3d3d"}, {"@BG_MSG@", "#1a1a1a"},
        {"@BORDER@", "#3a3a3a"}, {"@BORDER_STRONG@", "#3a3a3a"},
        {"@BORDER_HOVER@", "#444444"}, {"@BORDER_DASHED@", "#555555"},
        {"@BORDER_DISABLED@", "#444444"}, {"@BORDER_LIGHT@", "#444444"},
        {"@TEXT_SECONDARY@", "#b0b0b0"}, {"@TEXT_TERTIARY@", "#808080"},
        {"@TEXT_DISABLED@", "#808080"}, {"@TEXT_PRIMARY@", "#e8e8e8"},
        {"@TEXT_BORDER@", "#3a3a3a"}, {"@TEXT_ACCENT@", "#a5d6a7"},
        {"@PRIMARY_BG@", "#263238"},
        {"@SUCCESS@", "#10B981"},
    };

    // 深色：先替换深色覆盖表（同一个 token 在 QSS 里只出现一次，先替换者生效），
    // 再补一遍浅色表兜住没覆盖到的 token
    if (dark) {
        for (const auto &kv : darkOverride)
            style.replace(QLatin1String(kv.k), QLatin1String(kv.v));
    }
    for (const auto &kv : light)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    static const QRegularExpression re(QStringLiteral("@([A-Za-z0-9_.\\-]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

void applyStyle(bool dark, QWidget *host = nullptr)
{
    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    qApp->setStyleSheet(resolveTokens(raw, dark));

    ThemeManager::instance()->setCurrentTheme(dark ? ThemeManager::DarkTheme
                                                   : ThemeManager::LightTheme);

    // 真实程序里页面底色来自主窗口容器，探针的宿主窗口自己填一层
    if (host) {
        QPalette palette = host->palette();
        palette.setColor(QPalette::Window, QColor(dark ? QStringLiteral("#1a1a1a")
                                                      : QStringLiteral("#f5f5f5")));
        host->setPalette(palette);
        host->setAutoFillBackground(true);
        host->repaint();
    }
}

void pump(int rounds = 12)
{
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
        QThread::msleep(10);
    }
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // offscreen 平台默认找不到字体目录：指定系统字体目录，否则截图里全是方框
    qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));
    qInstallMessageHandler(msgHandler);

    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    applyStyle(false);

    auto *host = new QWidget();
    host->setObjectName(QStringLiteral("probeHost"));
    host->resize(1000, 780);
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    auto *page = new MemoryOptimizePage(host);
    hostLayout->addWidget(page);
    host->show();
    applyStyle(false, host);
    pump();

    host->grab().save(QStringLiteral("D:/BlockBox/_out/memory_page_light.png"));

    // ── 概况行是否都填上了数字 ──
    const QList<QLabel *> valueLabels = page->findChildren<QLabel *>(
        QStringLiteral("settingValueLabel"));
    expect(valueLabels.size() >= 4, "four value labels rendered (usage/available/launcher/time)");
    for (QLabel *l : valueLabels)
        std::fprintf(stderr, "        value: %s\n", l->text().toLocal8Bit().constData());

    auto *bar = page->findChild<QProgressBar *>(QStringLiteral("largeProgressBar"));
    expect(bar != nullptr, "memory usage progress bar exists");
    if (bar) {
        std::fprintf(stderr, "        bar: %d / %d\n", bar->value(), bar->maximum());
        expect(bar->maximum() > 0 && bar->value() > 0, "usage bar has non-zero range/value");
    }

    // ── 点「立即优化内存」：结果行要立刻报出释放量 ──
    auto *btn = page->findChild<QPushButton *>(QStringLiteral("memoryOptimizeBtn"));
    expect(btn != nullptr, "optimize button exists");
    QLabel *result = nullptr;
    if (btn) {
        btn->click();
        pump(4);
        result = page->findChild<QLabel *>(QStringLiteral("memoryOptimizeResult"));
        expect(result != nullptr, "result label exists");
        if (result) {
            std::fprintf(stderr, "        result: %s\n",
                         result->text().toLocal8Bit().constData());
            expect(result->text().contains(QStringLiteral("释放")), "result reports freed memory");
        }
    }
    host->grab().save(QStringLiteral("D:/BlockBox/_out/memory_page_optimized.png"));

    // ── 「启动游戏前先优化内存」开关写回设置 ──
    const QList<CustomCheckBox *> boxes = page->findChildren<CustomCheckBox *>();
    expect(boxes.size() == 1, "exactly one pre-launch switch");
    if (boxes.size() == 1) {
        CustomCheckBox *box = boxes.first();
        expect(!box->isChecked(), "switch starts off (settings default)");
        box->setChecked(true);
        pump(3);
        expect(SettingsManager::instance()->isOptimizeMemoryBeforeLaunchEnabled(),
               "switch writes memory/optimizeBeforeLaunch");
        // 从设置侧打开后再进页面：refresh 应把开关回填成开启
        SettingsManager::instance()->setOptimizeMemoryBeforeLaunchEnabled(true);
        page->refresh();
        pump(3);
        expect(box->isChecked(), "refresh mirrors setting value");
    }
    host->grab().save(QStringLiteral("D:/BlockBox/_out/memory_page_switch_on.png"));

    if (boxes.size() == 1) {
        // 在设置页改这个开关时，内存页的开关应当立刻跟着变（走 SettingsManager 信号）
        CustomCheckBox *box = boxes.first();
        SettingsManager::instance()->setOptimizeMemoryBeforeLaunchEnabled(false);
        pump(3);
        expect(!box->isChecked(), "switch follows setting changed elsewhere");
    }

    // ── 深色主题 ──
    ThemeManager::instance()->setCurrentTheme(ThemeManager::DarkTheme);
    applyStyle(true, host);
    pump();
    host->grab().save(QStringLiteral("D:/BlockBox/_out/memory_page_dark.png"));

    expect(g_parseWarnings == 0, "no QSS parse warnings");

    // ── 窄窗口（模拟手机竖屏 / 分屏）下的排版 ──
    applyStyle(false);
    host->resize(430, 780);
    pump(8);
    host->grab().save(QStringLiteral("D:/BlockBox/_out/memory_page_narrow.png"));

    std::fprintf(stderr, "\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "SOME FAILED",
                 g_fail, g_fail == 1 ? "" : "s");

    delete host;
    return g_fail == 0 ? 0 : 1;
}
