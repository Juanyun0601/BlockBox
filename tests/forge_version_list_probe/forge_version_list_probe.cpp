/**
 * @file   forge_version_list_probe.cpp
 * @brief  加载器详情页（版本列表）视觉探针：渲染真实 ForgeVersionListPage + 真实 style.qss
 *
 * 流程：加载器设为 Forge → 拉取 BMCLAPI 版本列表 → 截图；
 *       再切 Fabric 重复一次。输出：
 *   D:/BlockBox/_out/forge_version_list_forge.png
 *   D:/BlockBox/_out/forge_version_list_fabric.png
 */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QRegularExpression>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <cstdio>

#include "pages/ForgeVersionListPage.h"

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
        {"@BG_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.42)"},
        {"@BORDER_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.55)"},
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
        {"@PRIMARY_RGBA_18@", "rgba(46, 125, 50, 0.18)"},
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

void saveShot(QWidget &w, const char *tag, const QString &path)
{
    const bool ok = w.grab().save(path);
    std::fprintf(stderr, "[%s] %s %s\n", ok ? "ok" : "FAIL", tag, qUtf8Printable(path));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("forge_version_list_probe"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    f.close();
    qApp->setStyleSheet(resolveTokens(raw));

    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    // 加载器 logo 图标是否随 qrc 打进二进制
    for (const QString &p : { QStringLiteral(":/Images/Loader/Forge.png"),
                              QStringLiteral(":/Images/Loader/Fabric.png") })
        std::fprintf(stderr, "[logo] %s %s\n", qUtf8Printable(p),
                     QPixmap(p).isNull() ? "MISSING" : "ok");

    QWidget win;
    win.setWindowTitle(QStringLiteral("forge version list probe"));
    win.resize(1240, 820);
    auto *layout = new QVBoxLayout(&win);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *page = new ForgeVersionListPage(&win);
    layout->addWidget(page);

    win.show();
    app.processEvents();

    auto *list = page->findChild<QListWidget *>(QStringLiteral("forgeVersionList"));

    // 状态机：Forge 列表就绪 → 截图 → 切 Fabric → 就绪 → 截图 → 退出
    auto *timer = new QTimer(&app);
    int tick = 0;
    int stage = 0;          // 0=等 Forge 1=Forge后等待 2=等 Fabric 3=完成
    int readyTick = -1;
    bool shotForge = false;
    bool shotFabric = false;
    page->setLoaderName(QStringLiteral("Forge"));
    page->setMinecraftVersion(QStringLiteral("1.21.1"));
    QObject::connect(timer, &QTimer::timeout, [&]() {
        ++tick;
        const int count = list ? list->count() : 0;

        if (stage == 0 && count > 0) {
            readyTick = tick;
            stage = 1;
            std::fprintf(stderr, "[forge] list count=%d\n", count);
        } else if (stage == 1 && readyTick > 0 && tick >= readyTick + 5 && !shotForge) {
            saveShot(win, "forge", QStringLiteral("D:/BlockBox/_out/forge_version_list_forge.png"));
            shotForge = true;
            stage = 2;
            readyTick = -1;
            page->setLoaderName(QStringLiteral("Fabric"));
            page->setMinecraftVersion(QStringLiteral("1.21.1"));
            std::fprintf(stderr, "[switch] fabric requested\n");
        } else if (stage == 2 && count > 0 && readyTick < 0) {
            readyTick = tick;
            std::fprintf(stderr, "[fabric] list count=%d\n", count);
        } else if (stage == 2 && readyTick > 0 && tick >= readyTick + 5 && !shotFabric) {
            saveShot(win, "fabric", QStringLiteral("D:/BlockBox/_out/forge_version_list_fabric.png"));
            shotFabric = true;
            stage = 3;
            app.quit();
            return;
        }

        if (tick >= 250) {   // 75s 兜底（网络异常时也留一张现场）
            std::fprintf(stderr, "[timeout] stage=%d count=%d\n", stage, count);
            if (!shotForge)
                saveShot(win, "timeout", QStringLiteral("D:/BlockBox/_out/forge_version_list_fabric.png"));
            app.quit();
        }
    });
    timer->start(300);

    return app.exec();
}
