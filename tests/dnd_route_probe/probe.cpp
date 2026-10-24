// 拖放事件路由探针：确认「拖文件进主窗口」时事件最终落到哪个控件上。
//
// 实测结论（沿 QWidgetWindow 真实派发路径发送 DragEnter）：
//   - 拖到普通标签 / 空白容器 / QLineEdit 上：直接交给 setAcceptDrops(true) 的主窗口；
//   - 拖到自己也注册了拖放位的子页面上：先给子页面，子页面 ignore 后仍会冒泡到主窗口。
// 也就是说整窗拖入更新包不会因为「鼠标下面是某个页面」而失效，问题不在事件路由，
// 而在安装流程本身（见 tests/update_install_probe）。
//
// 用法：probe.exe <1..4>，分别对应上面四种落点。
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <cstdio>

static void note(const QString &text)
{
    std::fprintf(stderr, "%s\n", text.toUtf8().constData());
    std::fflush(stderr);
}

class FakeMainWindow : public QWidget
{
public:
    FakeMainWindow()
    {
        setAcceptDrops(true);
        resize(900, 700);
    }
    int dragEnterCount = 0;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        ++dragEnterCount;
        note(QStringLiteral("[FakeMainWindow] dragEnter"));
        event->acceptProposedAction();
    }
};

class FakeDropPage : public QScrollArea
{
public:
    explicit FakeDropPage(QWidget *parent = nullptr) : QScrollArea(parent)
    {
        setAcceptDrops(true);
    }
    int dragEnterCount = 0;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        ++dragEnterCount;
        note(QStringLiteral("[FakeDropPage] dragEnter -> ignore"));
        event->ignore();
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const int scenario = argc > 1 ? QString::fromLatin1(argv[1]).toInt() : 1;

    auto *win = new FakeMainWindow;
    auto *lay = new QVBoxLayout(win);
    auto *label = new QLabel(QStringLiteral("label"), win);
    auto *line = new QLineEdit(win);
    auto *page = new FakeDropPage(win);
    lay->addWidget(label);
    lay->addWidget(line);
    lay->addWidget(page);
    win->show();
    app.processEvents();
    note(QStringLiteral("window shown, handle=%1")
             .arg(win->windowHandle() ? QStringLiteral("ok") : QStringLiteral("null")));

    QWidget *target = label;
    if (scenario == 2)
        target = line;
    else if (scenario == 3)
        target = page;
    else if (scenario == 4)
        target = win;

    QMimeData *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(QStringLiteral("C:/tmp/BlockBox-beta6-win64-setup.exe"))});
    const QPoint windowPos = target->mapTo(win, QPoint(5, 5));
    note(QStringLiteral("scenario %1: target=%2 windowPos=(%3,%4)")
             .arg(scenario)
             .arg(target->metaObject()->className())
             .arg(windowPos.x())
             .arg(windowPos.y()));

    QDragEnterEvent enter(windowPos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    note(QStringLiteral("sending DragEnter to window"));
    QApplication::sendEvent(win->windowHandle(), &enter);
    note(QStringLiteral("sent, accepted=%1").arg(enter.isAccepted()));

    note(QStringLiteral("RESULT: mainDragEnter=%1 pageDragEnter=%2")
             .arg(win->dragEnterCount)
             .arg(page->dragEnterCount));
    return 0;
}
