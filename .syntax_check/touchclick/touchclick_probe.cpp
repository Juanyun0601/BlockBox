// 探针 v3：验证 utils/TouchScroll.h 的 DragClickGuard（滚动不误点 + 轻点可点）
//
// 模拟手机端的双重事件路径：
//   触摸序列直接发给子控件（传播到视口被 QScroller 拿去滚动，桌面已验证同路径）；
//   系统合成出的鼠标按下/松手发到手指位置下的子控件（安卓端 Qt 的合成行为）。
//
//  阶段1（无保护基线）：只挂 QScroller → 拖动后松手在按钮内会误触发点击（复现问题）
//  阶段2（开启保护）  ：TouchScroll::detail::applyDragScroll →
//    A 轻点按钮             → 必须触发 clicked
//    B 小位移拖动后松手在按钮内 → 不得触发 clicked，内容已滚动
//    C 按住较久后再拖动       → 不得触发 clicked，内容已滚动，按钮不残留按下态
//    D 惯性滚动中轻点另一按钮  → 必须触发 clicked
//    E 纯鼠标点击（无触摸）    → 必须触发 clicked
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QSpacerItem>
#include <QThread>
#include <QTouchEvent>
#include <QVBoxLayout>

#include "utils/TouchScroll.h"

static QList<QPushButton *> g_buttons;
static int g_clicks[16] = {};
static QScrollArea *g_area = nullptr;

static void sendTouch(QWidget *target, const QPointingDevice *device, QEvent::Type type,
                      const QPoint &pos, ulong ts)
{
    const QPointF local(pos);
    const QPointF global(target->mapToGlobal(pos));
    const QEventPoint::State state = (type == QEvent::TouchBegin)   ? QEventPoint::Pressed
                                   : (type == QEvent::TouchEnd)     ? QEventPoint::Released
                                                                    : QEventPoint::Updated;
    QList<QEventPoint> points;
    points.append(QEventPoint(0, state, local, global));
    QTouchEvent event(type, device, Qt::NoModifier, points);
    event.setTimestamp(ts);
    QCoreApplication::sendEvent(target, &event);
}

// 模拟系统把触摸合成为的鼠标按下/松手
static void sendMouse(QWidget *target, QEvent::Type type, const QPoint &local)
{
    const QPointF localF(local);
    const QPointF globalF(target->mapToGlobal(local));
    const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease) ? Qt::NoButton
                                                                          : Qt::LeftButton;
    QMouseEvent event(type, localF, globalF, Qt::LeftButton, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &event);
}

static void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}

// 触摸拖动：从按钮中心偏下位置起手向上拖
static void touchDrag(QWidget *btn, const QPointingDevice *dev, ulong ts, int steps,
                      int dyPerStep, int msPerStep, int dwellMs = 0)
{
    const QPoint start(60, 35); // y=35：向上拖 30px 后松手仍在按钮内
    sendTouch(btn, dev, QEvent::TouchBegin, start, ts++);
    if (dwellMs > 0) {
        pump(dwellMs);
        ts += dwellMs;
    }
    for (int i = 1; i <= steps; ++i) {
        pump(msPerStep);
        ts += msPerStep;
        sendTouch(btn, dev, QEvent::TouchUpdate, QPoint(60, 35 - i * dyPerStep), ts++);
    }
    sendTouch(btn, dev, QEvent::TouchEnd, QPoint(60, 35 - steps * dyPerStep), ts++);
}

// 模拟系统把触摸合成的鼠标序列：按下 → 若干移动 → 松手（位置随手指移动）
static void synthesizedMouseDrag(QWidget *btn, const QPoint &from, const QPoint &to)
{
    sendMouse(btn, QEvent::MouseButtonPress, from);
    const int steps = 6;
    for (int i = 1; i <= steps; ++i) {
        const QPoint p = from + (to - from) * i / steps;
        sendMouse(btn, QEvent::MouseMove, p);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    sendMouse(btn, QEvent::MouseButtonRelease, to);
}

static void resetState()
{
    g_area->verticalScrollBar()->setValue(0);
    pump(400); // 等惯性彻底停下
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QWidget host;
    auto *mainLay = new QVBoxLayout(&host);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    g_area = new QScrollArea(&host);
    g_area->setWidgetResizable(true);
    g_area->setFrameShape(QFrame::NoFrame);
    g_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    g_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget(g_area);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(0, 8, 0, 8);
    lay->setSpacing(6);
    for (int i = 0; i < 11; ++i) {
        auto *b = new QPushButton(QStringLiteral("B%1").arg(i), content);
        b->setFixedSize(120, 44);
        lay->addWidget(b, 0, Qt::AlignHCenter);
        g_buttons.append(b);
        QObject::connect(b, &QPushButton::clicked, [i] { ++g_clicks[i]; });
    }
    lay->addItem(new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));
    g_area->setWidget(content);
    mainLay->addWidget(g_area);
    host.resize(200, 320);
    host.show();
    pump(200);

    const QPointingDevice *device = new QPointingDevice(
        QStringLiteral("probe-touch"), 1000, QInputDevice::DeviceType::TouchScreen,
        QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 10, 3);
    QScrollBar *vbar = g_area->verticalScrollBar();
    int failures = 0;
    auto expect = [&failures](bool ok, const char *what) {
        qDebug() << (ok ? "  PASS " : "  FAIL ") << what;
        if (!ok)
            ++failures;
    };

    // ================= 阶段1：无保护基线（只挂 QScroller，复现误点问题）=================
    QScroller::grabGesture(g_area->viewport(), QScroller::TouchGesture);
    qDebug() << "[phase1] no guard";

    // 拖动 30px 后松手仍在 B1 内 → 系统合成松手 → 误触发点击（这就是要修的问题）
    touchDrag(g_buttons.at(1), device, 1000, 3, 10, 30);
    synthesizedMouseDrag(g_buttons.at(1), QPoint(60, 35), QPoint(60, 5));
    pump(400);
    expect(g_clicks[1] == 1, "baseline: 拖动后松手在按钮内 → 误触发点击（问题复现）");

    resetState();

    // ================= 阶段2：开启 TouchScroll 保护 =================
    TouchScroll::detail::applyDragScroll(g_area);
    qDebug() << "[phase2] guard on";

    // A. 轻点 B2：触摸 + 合成鼠标按下/松手 → 必须点击
    sendTouch(g_buttons.at(2), device, QEvent::TouchBegin, QPoint(60, 22), 2000);
    pump(60);
    sendTouch(g_buttons.at(2), device, QEvent::TouchEnd, QPoint(60, 22), 2060);
    synthesizedMouseDrag(g_buttons.at(2), QPoint(60, 22), QPoint(60, 22));
    pump(300);
    expect(g_clicks[2] == 1, "A 轻点按钮 → 触发 clicked");

    // B. 小位移拖动 30px，松手仍在 B3 内 → 不得点击，内容已滚动
    touchDrag(g_buttons.at(3), device, 3000, 3, 10, 30);
    synthesizedMouseDrag(g_buttons.at(3), QPoint(60, 35), QPoint(60, 5));
    pump(400);
    expect(g_clicks[3] == 0, "B 拖动后松手在按钮内 → 不触发 clicked");
    expect(vbar->value() > 0, "B 拖动 → 内容已滚动");
    resetState();

    // C. 按住 500ms 再拖动 → 不得点击，按钮不残留按下态
    touchDrag(g_buttons.at(4), device, 4000, 6, 10, 40, 500);
    synthesizedMouseDrag(g_buttons.at(4), QPoint(60, 35), QPoint(60, 35 - 60));
    pump(400);
    expect(g_clicks[4] == 0, "C 按住后拖动 → 不触发 clicked");
    expect(!g_buttons.at(4)->isDown(), "C 按住后拖动 → 按钮不残留按下态");
    expect(vbar->value() > 0, "C 按住后拖动 → 内容已滚动");
    resetState();

    // D. 大幅甩动触发惯性 → 惯性中轻点 B5 → 必须点击
    touchDrag(g_buttons.at(5), device, 5000, 8, 22, 25);
    pump(30);
    qDebug() << "  inertia state =" << QScroller::scroller(g_area->viewport())->state();
    sendTouch(g_buttons.at(6), device, QEvent::TouchBegin, QPoint(60, 22), 5600);
    pump(60);
    sendTouch(g_buttons.at(6), device, QEvent::TouchEnd, QPoint(60, 22), 5660);
    synthesizedMouseDrag(g_buttons.at(6), QPoint(60, 22), QPoint(60, 22));
    pump(400);
    expect(g_clicks[6] == 1, "D 惯性中轻点按钮 → 触发 clicked");

    // E. 纯鼠标点击 B7（无触摸参与）→ 必须点击
    synthesizedMouseDrag(g_buttons.at(7), QPoint(60, 22), QPoint(60, 22));
    pump(200);
    expect(g_clicks[7] == 1, "E 纯鼠标点击 → 触发 clicked");

    qDebug() << "clicks:" << [&] {
        QString s;
        for (int i = 0; i < 11; ++i)
            s += QString::number(g_clicks[i]);
        return s;
    }() << " failures =" << failures;
    return failures == 0 ? 0 : 2;
}
