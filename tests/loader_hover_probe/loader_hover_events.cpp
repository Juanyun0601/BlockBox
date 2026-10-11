/**
 * @file   loader_hover_events.cpp
 * @brief  事件追踪：真实光标停在加载器卡片上时，viewport / 卡片各自收到什么事件
 */
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QHoverEvent>
#include <QLabel>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGui/qevent.h>

#include <cstdio>

static const char *evName(int t)
{
    switch (t) {
    case QEvent::Enter: return "Enter";
    case QEvent::Leave: return "Leave";
    case QEvent::HoverEnter: return "HoverEnter";
    case QEvent::HoverLeave: return "HoverLeave";
    case QEvent::HoverMove: return "HoverMove";
    case QEvent::MouseMove: return "MouseMove";
    case QEvent::Paint: return "Paint";
    default: return nullptr;
    }
}

class Logger : public QObject
{
public:
    explicit Logger(QObject *t) : QObject(t) { t->installEventFilter(this); }
    QString label;
protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (const char *n = evName(e->type()))
            std::fprintf(stderr, "[%s] %s\n", qUtf8Printable(label), n);
        return false;
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("loader_hover_events"));

    QWidget win;
    win.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    win.resize(720, 560);
    // 与真实样式同族的 :hover 规则（触发 WA_Hover）
    win.setStyleSheet(QStringLiteral(
        "QListWidget#loaderListWidget { background: #ffffff; border: 1px solid #e8eaed; }"
        "QListWidget#loaderListWidget::item { padding: 10px 12px; }"
        "QListWidget#loaderListWidget::item:hover { background: #eaeaea; }"
        "QWidget#modCardListItem { background: #ffffff; border: 1px solid #e8eaed; border-radius: 12px; }"
        "QWidget#modCardListItem:hover { background: #eaeaea; }"));
    auto *vl = new QVBoxLayout(&win);
    vl->setContentsMargins(20, 20, 20, 20);

    auto *list = new QListWidget(&win);
    list->setObjectName(QStringLiteral("loaderListWidget"));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setUniformItemSizes(true);
    list->setMovement(QListView::Static);
    list->setSpacing(8);
    vl->addWidget(list, 1);

    for (const char *n : {"Forge", "Fabric", "Quilt"}) {
        auto *item = new QListWidgetItem(list);
        auto *card = new QWidget();
        card->setObjectName(QStringLiteral("modCardListItem"));
        card->setFixedHeight(76);
        auto *lay = new QHBoxLayout(card);
        lay->addWidget(new QLabel(QString::fromLatin1(n)));
        list->setItemWidget(item, card);
        item->setSizeHint(QSize(0, 76));
    }

    auto *lvLog = new Logger(list->viewport());
    lvLog->label = QStringLiteral("viewport");
    QWidget *card0 = list->itemWidget(list->item(0));
    auto *cLog = new Logger(card0);
    cLog->label = QStringLiteral("card0");

    win.show();
    win.raise();
    win.activateWindow();
    app.processEvents();

    const QRect ir0 = list->visualRect(list->model()->index(0, 0));
    const QPoint vp = list->viewport()->mapToGlobal(QPoint(0, 0));
    std::fprintf(stderr, "win=%d,%d vp=%d,%d ir0=(%d,%d %dx%d)\n",
                 win.x(), win.y(), vp.x(), vp.y(),
                 ir0.x(), ir0.y(), ir0.width(), ir0.height());

    auto *timer = new QTimer(&app);
    int tick = 0;
    QObject::connect(timer, &QTimer::timeout, [&]() {
        ++tick;
        if (tick == 3) {
            win.raise();
            win.activateWindow();
            std::fprintf(stderr, "--- move to card center (%d,%d) ---\n",
                         ir0.center().x(), ir0.center().y());
            QCursor::setPos(vp + ir0.center());
        } else if (tick == 6) {
            std::fprintf(stderr, "--- underMouse: win=%d viewport=%d card0=%d ---\n",
                         int(win.underMouse()), int(list->viewport()->underMouse()),
                         int(card0->underMouse()));
            std::fprintf(stderr, "--- move to card bottom+10 ---\n");
            QCursor::setPos(vp + QPoint(ir0.center().x(), ir0.bottom() + 10));
        } else if (tick == 9) {
            std::fprintf(stderr, "--- move away ---\n");
            QCursor::setPos(win.mapToGlobal(QPoint(5, 5)));
        } else if (tick == 12) {
            app.quit();
        }
    });
    timer->start(250);

    return app.exec();
}
