/**
 * @file   loader_hover_probe.cpp
 * @brief  版本详情页加载器卡片悬浮探针：真实 style.qss + 与 LoaderDetailPage 同构的列表
 *
 * 目的：复现「加载器卡片下面有一层多余的鼠标悬浮效果」，并量化成因——
 *   打印每个列表项 item rect 与卡片 widget geometry，确认卡片是否完全盖住 item；
 *   光标分别停在卡片中心 / 卡片下缘外 2px / 两卡之间，各截一张图对比。
 *
 * 输出：D:/BlockBox/_out/loader_hover_*.png + stderr 几何数据
 */
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QtGui/qevent.h>
#include <QLabel>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPixmap>
#include <QRegularExpression>
#include <QTimer>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

namespace {

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

// 与 LoaderDetailPage::createLoaderListCard 同构的最小卡片
QWidget *makeCard(QListWidget *list, const QString &name)
{
    auto *item = new QListWidgetItem(list);
    item->setData(Qt::UserRole, name);
    auto *card = new QWidget();
    card->setObjectName(QStringLiteral("modCardListItem"));
    card->setProperty("cardRole", "container");
    card->setFixedHeight(76);
    card->setCursor(Qt::PointingHandCursor);
    auto *lay = new QHBoxLayout(card);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(14);
    auto *icon = new QLabel(QStringLiteral("I"));
    icon->setObjectName(QStringLiteral("modCardIcon"));
    icon->setProperty("cardRole", "icon");
    icon->setFixedSize(48, 48);
    icon->setAlignment(Qt::AlignCenter);
    lay->addWidget(icon);
    auto *nameLab = new QLabel(name);
    nameLab->setObjectName(QStringLiteral("modCardName"));
    nameLab->setProperty("cardRole", "name");
    lay->addWidget(nameLab, 1);
    list->setItemWidget(item, card);
    item->setSizeHint(QSize(0, 76));
    return card;
}

void saveShot(QWidget &w, const char *tag)
{
    const QString path = QStringLiteral("D:/BlockBox/_out/loader_hover_%1.png").arg(QLatin1String(tag));
    const bool ok = w.grab().save(path);
    std::fprintf(stderr, "[%s] %s\n", ok ? "ok" : "FAIL", qUtf8Printable(path));
}

// 追踪 viewport / 卡片收到的 hover 事件
class EvLogger : public QObject
{
public:
    explicit EvLogger(QObject *t, const char *n) : QObject(t), m_name(n)
    { t->installEventFilter(this); }
protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::HoverEnter:
            std::fprintf(stderr, "[%s] HoverEnter\n", m_name); break;
        case QEvent::HoverMove: {
            auto *he = static_cast<QHoverEvent *>(e);
            std::fprintf(stderr, "[%s] HoverMove pos=(%.1f,%.1f)\n", m_name,
                         he->position().x(), he->position().y());
            break;
        }
        case QEvent::HoverLeave: std::fprintf(stderr, "[%s] HoverLeave\n", m_name); break;
        default: break;
        }
        return false;
    }
private:
    const char *m_name;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("loader_hover_probe"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    f.close();
    qApp->setStyleSheet(resolveTokens(raw));

    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    QWidget win;
    win.setWindowTitle(QStringLiteral("loader hover probe"));
    win.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    win.resize(720, 560);
    auto *vl = new QVBoxLayout(&win);
    vl->setContentsMargins(20, 20, 20, 20);

    // 与 LoaderDetailPage::initUI 同构
    auto *list = new QListWidget(&win);
    list->setObjectName(QStringLiteral("loaderListWidget"));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setAlternatingRowColors(false);
    list->setUniformItemSizes(true);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setMovement(QListView::Static);
    list->setSpacing(8);
    vl->addWidget(list, 1);

    const char *names[] = {"Forge", "Fabric", "Quilt", "NeoForge", "OptiFine"};
    for (const char *n : names)
        makeCard(list, QString::fromLatin1(n));

    EvLogger vpLog(list->viewport(), "viewport");
    EvLogger cardLog(list->itemWidget(list->item(0)), "card0");

    win.show();
    app.processEvents();

    // 几何诊断：item rect vs 卡片 widget geometry（viewport / 窗口两套坐标）
    std::fprintf(stderr, "== geometry ==\n");
    std::fprintf(stderr, "viewport win-pos=(%d,%d)\n",
                 list->viewport()->mapTo(&win, QPoint(0, 0)).x(),
                 list->viewport()->mapTo(&win, QPoint(0, 0)).y());
    for (int i = 0; i < list->count(); ++i) {
        const QRect ir = list->visualRect(list->model()->index(i, 0));
        QWidget *w = list->itemWidget(list->item(i));
        const QRect wr = w ? QRect(w->mapTo(list->viewport(), QPoint(0, 0)), w->size()) : QRect();
        std::fprintf(stderr,
                     "item[%d] vp=(%d,%d %dx%d) widget_vp=(%d,%d %dx%d) %s\n",
                     i, ir.x(), ir.y(), ir.width(), ir.height(),
                     wr.x(), wr.y(), wr.width(), wr.height(),
                     (wr.contains(ir) ? "FULL" : "GAP"));
        if (i == 0) {
            const QRect iw = QRect(ir.topLeft() + list->viewport()->mapTo(&win, QPoint(0, 0)), ir.size());
            const QRect ww = QRect(wr.topLeft() + list->viewport()->mapTo(&win, QPoint(0, 0)), wr.size());
            std::fprintf(stderr, "  win: item=(%d,%d %dx%d) card=(%d,%d %dx%d)\n",
                         iw.x(), iw.y(), iw.width(), iw.height(),
                         ww.x(), ww.y(), ww.width(), ww.height());
        }
    }

    // 真实光标悬浮（窗口置顶后可触发真实 hover）
    win.raise();
    win.activateWindow();
    app.processEvents();

    const QPoint start(10, 10);
    QCursor::setPos(start);
    app.processEvents();

    // 状态机：先截无 hover 基线，再小步把光标移到卡片中心，截悬浮态
    auto *timer = new QTimer(&app);
    int tick = 0;
    QObject::connect(timer, &QTimer::timeout, [&]() {
        ++tick;
        const QRect ir0 = list->visualRect(list->model()->index(0, 0));
        const QPoint vp = list->viewport()->mapToGlobal(QPoint(0, 0));
        const QPoint target = vp + ir0.center();

        if (tick == 2) {
            std::fprintf(stderr, "--- shot base (cursor away) underMouse=%d ---\n",
                         int(list->viewport()->underMouse()));
            saveShot(win, "base");
        } else if (tick >= 4 && tick <= 13) {
            const int k = tick - 3;
            QCursor::setPos(start + (target - start) * k / 10);
        } else if (tick == 16) {
            std::fprintf(stderr, "--- shot oncard cursor=(%d,%d) target=(%d,%d) underMouse=%d cardUnder=%d ---\n",
                         QCursor::pos().x(), QCursor::pos().y(), target.x(), target.y(),
                         int(list->viewport()->underMouse()),
                         int(list->itemWidget(list->item(0))->underMouse()));
            saveShot(win, "oncard");
            QCursor::setPos(start);
        } else if (tick == 19) {
            app.quit();
        }
    });
    timer->start(250);

    return app.exec();
}
