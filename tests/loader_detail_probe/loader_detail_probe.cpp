/**
 * @file   loader_detail_probe.cpp
 * @brief  加载器详情页视觉探针：渲染真实 LoaderDetailPage + 真实 style.qss
 *
 * 无需网络：LoaderDetailPage 的加载器卡片为静态内容。
 * 输出：
 *   D:/BlockBox/_out/loader_detail_default.png   （未选加载器）
 *   D:/BlockBox/_out/loader_detail_selected.png  （已选 Forge 47.3.0）
 */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <cstdio>

#include "pages/LoaderDetailPage.h"

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
    QCoreApplication::setApplicationName(QStringLiteral("loader_detail_probe"));

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
    const QStringList logoPaths = {
        QStringLiteral(":/Images/Loader/Forge.png"),
        QStringLiteral(":/Images/Loader/Fabric.png"),
        QStringLiteral(":/Images/Loader/NeoForge.png"),
    };
    for (const QString &p : logoPaths)
        std::fprintf(stderr, "[logo] %s %s\n", qUtf8Printable(p),
                     QPixmap(p).isNull() ? "MISSING" : "ok");

    QWidget win;
    win.setWindowTitle(QStringLiteral("loader detail probe"));
    win.resize(1240, 820);
    auto *layout = new QVBoxLayout(&win);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *page = new LoaderDetailPage(&win);
    layout->addWidget(page);
    page->setVersionType(QStringLiteral("正式版"));
    page->setVersionInfo(QStringLiteral("1.21.1"));

    win.show();
    app.processEvents();

    int cards = page->findChildren<QWidget *>(QStringLiteral("modCardListItem")).count();
    std::fprintf(stderr, "[cards] modCardListItem count=%d\n", cards);

    saveShot(win, "default", QStringLiteral("D:/BlockBox/_out/loader_detail_default.png"));

    // 模拟已选一个加载器（加依赖链展示已选卡片条）
    page->addSelectedLoader(QStringLiteral("Forge"), QStringLiteral("47.3.0"));
    app.processEvents();
    saveShot(win, "selected", QStringLiteral("D:/BlockBox/_out/loader_detail_selected.png"));

    QTimer::singleShot(200, &app, &QApplication::quit);
    return app.exec();
}
