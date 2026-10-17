/**
 * @file   item_hover_check.cpp
 * @brief  干净对照：光标先移出窗口截基线，再小步移入 item 截悬浮态
 */
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QRegularExpression>
#include <QTimer>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGui/qevent.h>

#include <cstdio>

static QString resolveTokens(QString style)
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
        {"@PRIMARY@", "#2E7D32"}, {"@PRIMARY_HOVER@", "#256428"},
        {"@PRIMARY_PRESSED@", "#1B5E20"}, {"@PRIMARY_DARK@", "#1B5E20"},
        {"@PRIMARY_RGBA_12@", "rgba(46, 125, 50, 0.12)"},
        {"@PRIMARY_RGBA_18@", "rgba(46, 125, 50, 0.18)"},
        {"@PRIMARY_RGBA_20@", "rgba(46, 125, 50, 0.2)"},
        {"@PRIMARY_RGBA_26@", "rgba(46, 125, 50, 0.26)"},
        {"@PRIMARY_RGBA_38@", "rgba(46, 125, 50, 0.38)"},
        {"@TEXT_ON_PRIMARY@", "#ffffff"},
        {"@SUCCESS@", "#10B981"}, {"@INFO@", "#2196F3"},
        {"@WARNING@", "#F57C00"}, {"@DANGER@", "#D32F2F"},
    };
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));
    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("item_hover_check"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    f.open(QIODevice::ReadOnly);
    qApp->setStyleSheet(resolveTokens(QString::fromUtf8(f.readAll())));
    f.close();
    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    QWidget win;
    win.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    win.resize(720, 560);
    auto *vl = new QVBoxLayout(&win);
    vl->setContentsMargins(20, 20, 20, 20);

    auto *list = new QListWidget(&win);
    list->setObjectName(QStringLiteral("loaderListWidget"));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setUniformItemSizes(true);
    list->setMovement(QListView::Static);
    list->setSpacing(8);
    vl->addWidget(list, 1);
    for (const char *n : {"Forge", "Fabric", "Quilt", "NeoForge"}) {
        auto *it = new QListWidgetItem(QString::fromLatin1(n), list);
        it->setSizeHint(QSize(0, 76));
    }

    win.show();
    win.raise();
    win.activateWindow();
    app.processEvents();

    // 先把光标移出窗口远处，确保基线无 hover
    QCursor::setPos(QPoint(10, 10));
    app.processEvents();

    const QRect ir0 = list->visualRect(list->model()->index(0, 0));
    const QPoint vp = list->viewport()->mapToGlobal(QPoint(0, 0));
    std::fprintf(stderr, "item0=(%d,%d %dx%d) vpGlobal=(%d,%d)\n",
                 ir0.x(), ir0.y(), ir0.width(), ir0.height(), vp.x(), vp.y());

    auto *timer = new QTimer(&app);
    int tick = 0;
    QObject::connect(timer, &QTimer::timeout, [&]() {
        ++tick;
        const QPoint target = vp + ir0.center();
        const QPoint start(10, 10);
        if (tick == 2) {
            win.grab().save(QStringLiteral("D:/BlockBox/_out/item_hover_base.png"));
            std::fprintf(stderr, "base saved, underMouse=%d\n", int(list->viewport()->underMouse()));
        } else if (tick >= 4 && tick <= 13) {
            // 10 步从 (10,10) 移到 item 中心
            const int k = tick - 3;
            QCursor::setPos(start + (target - start) * k / 10);
        } else if (tick == 16) {
            std::fprintf(stderr, "on: underMouse=%d cursor=%d,%d expect=%d,%d\n",
                         int(list->viewport()->underMouse()),
                         QCursor::pos().x(), QCursor::pos().y(), target.x(), target.y());
            win.grab().save(QStringLiteral("D:/BlockBox/_out/item_hover_on.png"));
            app.quit();
        }
    });
    timer->start(200);
    return app.exec();
}
