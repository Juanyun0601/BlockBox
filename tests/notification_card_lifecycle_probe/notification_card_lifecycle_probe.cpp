/**
 * @file   notification_card_lifecycle_probe.cpp
 * @brief  左下角提示卡生命周期探针
 *
 * 用户反馈：“提示信息有时不会倒计时结束后消失”。
 * 覆盖两条真实会漏掉销毁的路径：
 *   1) 倒计时归零时正在播退场动画，此时新提示到达 / 窗口缩放触发重排，
 *      重排会 stopPosAnimations() 打断退场动画，finished 不再发出，
 *      卡片就永远停在屏幕上（倒计时显示 0s）。
 *   2) 鼠标悬停暂停倒计时，但触屏、卡片被重排移走等情况下收不到 leave，
 *      倒计时永久停住，卡片同样不会消失。
 *
 * 用法：qmake notification_card_lifecycle_probe.pro && mingw32-make
 *       ./release/notification_card_lifecycle_probe.exe
 */
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QEnterEvent>
#include <QEvent>
#include <QEventLoop>
#include <QMainWindow>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <cstdio>

#include "components/NotificationCard.h"
#include "components/NotificationManager.h"

namespace {

int failures = 0;

void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

NotificationCard *findCard(QWidget *win, const QString &text)
{
    const QList<NotificationCard *> cards = win->findChildren<NotificationCard *>();
    for (NotificationCard *card : cards) {
        if (card->messageText() == text)
            return card;
    }
    return nullptr;
}

bool cardAlive(QWidget *win, const QString &text)
{
    NotificationCard *card = findCard(win, text);
    return card && card->isVisible();
}

/// 轮询等待卡片消失（真机上计时器可能被系统拖慢几十毫秒，固定等待容易误报）
bool waitGone(QWidget *win, const QString &text, int timeoutMs)
{
    for (int waited = 0; waited < timeoutMs; waited += 20) {
        if (!cardAlive(win, text))
            return true;
        settle(20);
    }
    return !cardAlive(win, text);
}

/// 等到卡片倒计时归零（退场动画已经开始），最多等 timeoutMs
bool waitCountdownEnd(NotificationCard *card, int timeoutMs)
{
    if (!card)
        return false;
    for (int waited = 0; waited < timeoutMs; waited += 5) {
        if (card->remainingSeconds() <= 0)
            return true;
        settle(5);
    }
    return false;
}

/// 模拟真实悬停：直接给卡片发 Enter 事件（这是 enterEvent 的正常入口）
void sendEnter(NotificationCard *card)
{
    const QPointF pos(2, 2);
    QEnterEvent enterEvent(pos, pos, card->mapToGlobal(QPoint(2, 2)));
    QCoreApplication::sendEvent(card, &enterEvent);
}

void check(bool ok, const char *what, const char *detail = "")
{
    fprintf(stderr, "[%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
            ok || !*detail ? "" : "：", detail);
    if (!ok)
        ++failures;
}

/// 打印悬停相关状态，排查偶发失败（真实鼠标在跑测试时可能带来额外的 Enter/Leave）
void dumpState(NotificationCard *card, const char *tag)
{
    if (!card) {
        fprintf(stderr, "[DUMP] %s card=null\n", tag);
        return;
    }
    const QRect gr(card->mapToGlobal(QPoint(0, 0)), card->size());
    const QPoint cursor = QCursor::pos();
    fprintf(stderr,
            "[DUMP] %s visible=%d remaining=%d cardGlobal=(%d,%d %dx%d) cursor=(%d,%d) "
            "inside=%d active=%d\n",
            tag, card->isVisible(), card->remainingSeconds(), gr.x(), gr.y(), gr.width(),
            gr.height(), cursor.x(), cursor.y(),
            card->rect().contains(card->mapFromGlobal(cursor)),
            card->window()->isActiveWindow());
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("BlockBox"));

    QMainWindow win;
    win.setWindowFlags(Qt::FramelessWindowHint);
    win.resize(1000, 700);
    win.move(100, 100);
    auto *page = new QWidget(&win);
    win.setCentralWidget(page);
    win.show();
    win.activateWindow();
    win.raise();
    settle(200);

    QDir().mkpath(QStringLiteral("probe_out"));

    // ── 1) 正常倒计时：到点必须消失 ──
    NotificationManager::showInfo(page, QStringLiteral("lifecycle-normal"), 1200);
    check(waitGone(&win, QStringLiteral("lifecycle-normal"), 3000),
          "普通提示倒计时结束后消失");

    // ── 2) 退场途中来了新提示：旧卡不能被重排拽回来 ──
    NotificationManager::showInfo(page, QStringLiteral("lifecycle-race"), 1200);
    NotificationCard *raceCard = findCard(&win, QStringLiteral("lifecycle-race"));
    const bool raceStarted = waitCountdownEnd(raceCard, 3000);
    // 退场动画约 250ms，紧接着插入新提示，触发 repositionAll
    NotificationManager::showInfo(page, QStringLiteral("lifecycle-race-next"), 4000);
    check(raceStarted, "退场途中插入新提示的前置条件成立");
    check(waitGone(&win, QStringLiteral("lifecycle-race"), 2000),
          "退场途中来新提示时旧提示照样消失");
    check(cardAlive(&win, QStringLiteral("lifecycle-race-next")), "新提示正常显示");

    // ── 3) 退场途中窗口缩放：旧卡同样不能留下 ──
    NotificationManager::showInfo(page, QStringLiteral("lifecycle-resize"), 1200);
    NotificationCard *resizeCard = findCard(&win, QStringLiteral("lifecycle-resize"));
    const bool resizeStarted = waitCountdownEnd(resizeCard, 3000);
    win.resize(900, 640); // 宿主窗口缩放 → repositionAll(false)
    check(resizeStarted, "退场途中缩放窗口的前置条件成立");
    check(waitGone(&win, QStringLiteral("lifecycle-resize"), 2000),
          "退场途中缩放窗口时旧提示照样消失");

    // 清掉前面留下的卡片（等它们自己走完）
    settle(4200);

    // 把窗口挪到鼠标碰不到的一角：真机上不能为了跑测试去动用户的鼠标
    const QRect avail = win.screen()->availableGeometry();
    auto moveAwayFromCursor = [&]() {
        const QPoint cursor = QCursor::pos();
        QPoint topLeft;
        topLeft.setX(cursor.x() < avail.center().x() ? avail.right() - win.width() : avail.left());
        topLeft.setY(cursor.y() < avail.center().y() ? avail.bottom() - win.height() : avail.top());
        win.move(topLeft);
        settle(120);
    };
    moveAwayFromCursor();

    // ── 4) 悬停暂停但漏收 leave：看门狗必须恢复倒计时 ──
    // 指针在卡片外，人工发 Enter 模拟“悬停中”，之后不再发任何鼠标事件
    NotificationManager::showInfo(page, QStringLiteral("lifecycle-hover"), 1200);
    NotificationCard *hoverCard = findCard(&win, QStringLiteral("lifecycle-hover"));
    settle(250);
    if (hoverCard) {
        dumpState(hoverCard, "hover-enter");
        if (hoverCard->rect().contains(hoverCard->mapFromGlobal(QCursor::pos()))) {
            fprintf(stderr, "[SKIP] 漏收 leave 用例（用户鼠标正好压在卡片上，暂停是正确行为）\n");
            settle(1800);
        } else {
            sendEnter(hoverCard);
            const int pausedAt = hoverCard->remainingSeconds();
            settle(300);
            check(hoverCard->remainingSeconds() == pausedAt, "悬停时倒计时暂停");
            // 之后不再发任何鼠标事件，模拟“leave 丢了”
            check(waitGone(&win, QStringLiteral("lifecycle-hover"), 3000),
                  "漏收 leave 时看门狗恢复倒计时并让提示消失");
        }
    } else {
        check(false, "悬停探针拿到了提示卡");
    }

    // ── 5) 鼠标真的压在提示上：暂停；卡片离开鼠标（窗口挪走）后恢复 ──
    // 全程不动用户的鼠标，只把窗口移过去 / 移开
    {
        NotificationManager::showInfo(page, QStringLiteral("lifecycle-hover-real"), 3000);
        NotificationCard *realCard = findCard(&win, QStringLiteral("lifecycle-hover-real"));
        settle(450); // 等滑入动画（350ms）结束，卡片才真正停到左下角
        win.activateWindow();
        settle(120);
        if (!realCard || !win.isActiveWindow()) {
            fprintf(stderr, "[SKIP] 指针悬停用例（窗口拿不到焦点）\n");
            settle(3400);
        } else {
            const QPoint centerInWindow = realCard->geometry().center();
            // 真机上用户随时可能动鼠标：每次挪窗口前现读一次指针位置，最多试 3 次
            bool underMouse = false;
            for (int attempt = 0; attempt < 3 && !underMouse; ++attempt) {
                win.move(QCursor::pos() - centerInWindow); // 让卡片正好落在鼠标下面
                settle(150);
                underMouse = realCard->rect().contains(realCard->mapFromGlobal(QCursor::pos()));
            }
            dumpState(realCard, "real-under-cursor");
            if (!underMouse) {
                fprintf(stderr, "[SKIP] 指针悬停用例（窗口没能挪到鼠标下）\n");
                settle(3400);
            } else {
                sendEnter(realCard);
                const int before = realCard->remainingSeconds();
                settle(700);
                dumpState(realCard, "real-after-pause");
                // 真机上用户随时可能把鼠标移开（那就该恢复倒计时），只验证“指针还在卡片上”的这一段
                const bool stillUnderMouse =
                    realCard->rect().contains(realCard->mapFromGlobal(QCursor::pos()));
                if (!stillUnderMouse) {
                    fprintf(stderr, "[SKIP] 指针悬停用例（测试过程中用户移开了鼠标）\n");
                } else {
                    check(cardAlive(&win, QStringLiteral("lifecycle-hover-real"))
                              && realCard->remainingSeconds() == before,
                          "鼠标压在提示上时倒计时保持暂停");
                }

                win.move(avail.topLeft()); // 先随便挪开，下面再按鼠标位置挪到空角
                moveAwayFromCursor();      // 卡片随窗口离开鼠标（不会收到 leave）
                settle(400); // 看门狗 400ms 内发现指针已移开
                dumpState(realCard, "real-after-move-away");
                const int resumed = realCard->remainingSeconds();
                check(cardAlive(&win, QStringLiteral("lifecycle-hover-real"))
                          && resumed > 0 && resumed <= before,
                      "鼠标离开后倒计时恢复");
                check(waitGone(&win, QStringLiteral("lifecycle-hover-real"), 4000),
                      "鼠标离开后提示正常消失");
            }
        }
    }

    // ── 6) 收尾：所有提示都应该自己走干净，不能有残影 ──
    settle(6000);
    {
        const QList<NotificationCard *> left = win.findChildren<NotificationCard *>();
        int visible = 0;
        for (NotificationCard *card : left) {
            if (card->isVisible()) {
                ++visible;
                fprintf(stderr, "[INFO] 残留卡片：%s remaining=%ds\n",
                        qPrintable(card->messageText()), card->remainingSeconds());
            }
        }
        check(visible == 0, "所有提示最终都消失了（无残影）");
    }

    fprintf(stderr, "[probe] %s（failures=%d）\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
