/**
 * @file   overview_probe.cpp
 * @brief  实例管理-概览页视觉探针
 *
 * 直接实例化真实的 InstanceOverviewPage，加载真实 style.qss（token 解析后），
 * 渲染并截图到 D:/BlockBox/_out/overview_probe.png，用于核对统计卡片边框、
 * 启动按钮圆角等视觉问题。
 */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <cstdio>

#include "pages/InstanceOverviewPage.h"
#include "utils/UiMetrics.h"

namespace {

// 与 ThemeManager::buildTokenTable 同族的浅色 token 表（探针用精简版）
QString resolveTokens(QString style)
{
    struct KV { const char *k; const char *v; };
    const QVector<KV> t = {
        {"@BG_BASE@", "#f5f5f5"}, {"@BG_CARD@", "#ffffff"},
        {"@BG_CONTENT@", "#fafafa"}, {"@BG_MUTED@", "#f8f9fa"},
        {"@BG_HOVER@", "#eaeaea"}, {"@BG_PRESSED@", "#e0e0e0"},
        {"@BG_TRACK@", "#e8e8e8"}, {"@BG_TAB@", "#f0f0f0"},
        {"@BG_DISABLED@", "#d5d7db"}, {"@BG_MSG@", "#edf0f4"},
        {"@BG_GLASS@", "rgba(255, 255, 255, 0.72)"},
        {"@BORDER@", "#e8eaed"}, {"@BORDER_STRONG@", "#dadce0"},
        {"@BORDER_HOVER@", "#bdbdbd"}, {"@BORDER_DASHED@", "#c4c7c5"},
        {"@BORDER_DISABLED@", "#b0b0b0"}, {"@BORDER_LIGHT@", "#f0f1f3"},
        {"@TEXT_SECONDARY@", "#666666"}, {"@TEXT_TERTIARY@", "#888888"},
        {"@TEXT_DISABLED@", "#bbbbbb"}, {"@TEXT_PRIMARY@", "#333333"},
        {"@TEXT_BORDER@", "#e8e8e8"},
        {"@PRIMARY_BG@", "#e8f5e9"}, {"@TEXT_ACCENT@", "#1b5e20"},
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
        {"@SUCCESS@", "#10B981"}, {"@INFO@", "#2196F3"},
        {"@WARNING@", "#F57C00"}, {"@DANGER@", "#D32F2F"},
    };
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    // 兜底：剩余 token 置灰，避免 QSS 解析告警
    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

void buildFakeInstance()
{
    const QString root = QStringLiteral("D:/BlockBox/_out/probe_instance");
    QDir rootDir(root);
    rootDir.removeRecursively();
    const QStringList dirs = {"mods", "resourcepacks", "shaderpacks", "saves"};
    for (const QString &d : dirs)
        QDir().mkpath(root + "/" + d);

    auto touch = [&](const QString &rel, int bytes) {
        QFile f(root + "/" + rel);
        if (f.open(QIODevice::WriteOnly))
            f.write(QByteArray(bytes, 'x'));
    };
    touch("mods/sodium.jar", 2048);
    touch("mods/lithium.jar", 1024);
    touch("mods/create.jar", 4096);
    touch("resourcepacks/default.zip", 512);
    touch("resourcepacks/fancy.zip", 256);
    touch("shaderpacks/bsl.zip", 768);
    QDir().mkpath(root + "/saves/world1");
    QDir().mkpath(root + "/saves/world2");
    touch("saves/world1/level.dat", 1024);
    touch("versions/1.20.1.json", 40960);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // 统计卡片固定宽度是否装得下极端尺寸文本（font-size 24px Consolas）
    {
        QFont f(QStringLiteral("Consolas"));
        f.setPixelSize(24);
        QFontMetrics fm(f);
        const QStringList samples = {"9.5 KB", "2.31 GB", "999.9 MB", "1023.9 MB"};
        const int contentWidth = UiMetrics::kStatCardWidth - 2 /*border*/ - 28 /*layout margins*/;
        for (const QString &s : samples)
            std::fprintf(stderr, "[fit] %-10s %dpx  (content %dpx) %s\n",
                         qUtf8Printable(s), fm.horizontalAdvance(s), contentWidth,
                         fm.horizontalAdvance(s) <= contentWidth ? "OK" : "CLIPPED");
    }

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    f.close();
    qApp->setStyleSheet(resolveTokens(raw));

    buildFakeInstance();

    QWidget win;
    win.setWindowTitle(QStringLiteral("overview probe"));
    win.resize(1180, 780);
    auto *layout = new QVBoxLayout(&win);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *page = new InstanceOverviewPage(&win);
    layout->addWidget(page);
    page->setGameVersion(QStringLiteral("1.20.1"));
    page->setLoaderInfo(QStringLiteral("Fabric 0.15.11"));
    page->setInstanceName(QStringLiteral("生存测试实例"));
    page->setInstancePath(QStringLiteral("D:/BlockBox/_out/probe_instance"));

    win.show();
    app.processEvents();

    // 圆角失效原因定位：border-radius 数值扫描（整窗截图后由外部脚本判角）
    {
        QWidget diag;
        diag.setWindowTitle(QStringLiteral("radius sweep"));
        auto *v = new QVBoxLayout(&diag);
        v->setContentsMargins(4, 4, 4, 4);
        v->setSpacing(4);
        const QString grad = QStringLiteral(
            "qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2E7D32, stop:1 #1B5E20)");
        static const int radii[] = {22, 23, 24, 25, 26, 28, 30, 31, 32, 40, 64, 999};
        for (int r : radii) {
            auto *b = new QPushButton(QStringLiteral("r=%1").arg(r));
            b->setFixedSize(130, 44);
            b->setStyleSheet(QStringLiteral(
                "QPushButton{background-color:%1;border:none;border-radius:%2px;"
                "padding:10px 32px;color:#fff;}").arg(grad).arg(r));
            v->addWidget(b);
        }
        diag.resize(160, 4 + (44 + 4) * int(sizeof(radii) / sizeof(radii[0])) + 4);
        diag.show();
        app.processEvents();
        diag.grab().save(QStringLiteral("D:/BlockBox/_out/radius_sweep.png"));
        diag.close();
        std::fprintf(stderr, "[sweep] saved %d radii\n",
                     int(sizeof(radii) / sizeof(radii[0])));

        // 对照：同样 999px，但把按钮加高到 120px（44 高度下 r>h/2 是否被钳制）
        QWidget tall;
        auto *tv = new QVBoxLayout(&tall);
        tv->setContentsMargins(4, 4, 4, 4);
        auto *tb = new QPushButton(QStringLiteral("tall 999"));
        tb->setFixedSize(130, 120);
        tb->setStyleSheet(QStringLiteral(
            "QPushButton{background-color:%1;border:none;border-radius:999px;"
            "color:#fff;}").arg(grad));
        tv->addWidget(tb);
        tall.resize(160, 128);
        tall.show();
        app.processEvents();
        tall.grab().save(QStringLiteral("D:/BlockBox/_out/radius_tall.png"));
        tall.close();

        // 百分比半径对照（Qt 是否支持 border-radius: 50% 作为胶囊）
        QWidget pct;
        auto *pv = new QVBoxLayout(&pct);
        pv->setContentsMargins(4, 4, 4, 4);
        const QStringList pctRules = {
            QStringLiteral("50%"), QStringLiteral("999px"), QStringLiteral("22px")};
        for (const QString &r : pctRules) {
            auto *pb = new QPushButton(QStringLiteral("r=%1").arg(r));
            pb->setFixedSize(130, 44);
            pb->setStyleSheet(QStringLiteral(
                "QPushButton{background-color:%1;border:none;border-radius:%2;"
                "padding:10px 32px;color:#fff;}").arg(grad, r));
            pv->addWidget(pb);
        }
        pct.resize(160, 4 + (44 + 4) * pctRules.size() + 4);
        pct.show();
        app.processEvents();
        pct.grab().save(QStringLiteral("D:/BlockBox/_out/radius_pct.png"));
        pct.close();
    }

    // 等待 QtConcurrent 的体积统计回填
    QTimer::singleShot(800, [&]() {
        app.processEvents();
        const QString out = QStringLiteral("D:/BlockBox/_out/overview_probe.png");
        const bool ok = win.grab().save(out);
        std::fprintf(stderr, "[%s] %s\n", ok ? "ok" : "FAIL", qUtf8Printable(out));

        // 对照组：追加同选择器覆盖规则，验证 border-radius 是否可生效
        qApp->setStyleSheet(qApp->styleSheet() +
            QStringLiteral("\nQPushButton#overviewLaunchBtn {"
                           "  border: 2px solid #D32F2F;"
                           "  border-radius: 22px;"
                           "}\n"));
        win.grab().save(QStringLiteral("D:/BlockBox/_out/overview_probe_override.png"));
        app.quit();
    });
    return app.exec();
}
