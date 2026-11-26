/**
 * @file   notification_card_probe.cpp
 * @brief  左下角提示卡定位探针
 *
 * 模拟主窗口的真实结构（顶栏 + 内容区 + 左侧导航 + 页面），
 * 验证提示卡始终贴在窗口内部左下角，不会被顶栏偏移推到窗口外，
 * 也不会在窄窗口 / 长文案 / 多卡叠加时溢出窗口。
 */
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "components/NotificationCard.h"
#include "components/NotificationManager.h"

namespace {

void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/// 建一个和主窗口同构的窗口：顶栏 52 + 分割线 1 + 内容区（左导航 76 + 页面）
QWidget *makePage(QMainWindow *win)
{
    auto *central = new QWidget(win);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *topBar = new QWidget(central);
    topBar->setFixedHeight(52);
    topBar->setStyleSheet(QStringLiteral("background: #ffffff;"));
    mainLayout->addWidget(topBar);

    auto *sep = new QFrame(central);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QStringLiteral("background: #dddddd;"));
    mainLayout->addWidget(sep);

    auto *content = new QWidget(central);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto *sideBar = new QWidget(content);
    sideBar->setFixedWidth(76);
    sideBar->setStyleSheet(QStringLiteral("background: #f2f2f2;"));
    contentLayout->addWidget(sideBar);

    auto *page = new QWidget(content);
    page->setObjectName(QStringLiteral("page"));
    page->setStyleSheet(QStringLiteral("background: #fafafa;"));
    auto *pageLayout = new QVBoxLayout(page);
    auto *label = new QLabel(QStringLiteral("页面内容区（提示卡应当贴在整个窗口的左下角）"), page);
    pageLayout->addWidget(label);
    pageLayout->addStretch();
    contentLayout->addWidget(page, 1);

    mainLayout->addWidget(content, 1);
    win->setCentralWidget(central);
    return page;
}

int failures = 0;

void check(QWidget *win, const char *tag)
{
    const QList<NotificationCard *> cards = win->findChildren<NotificationCard *>();
    fprintf(stderr, "[INFO] %s：当前卡片数 %lld\n", tag, static_cast<long long>(cards.size()));
    const QRect winRect = win->rect();
    NotificationCard *lowest = nullptr;
    for (NotificationCard *card : cards) {
        const QRect r = card->geometry();
        const bool inside = winRect.contains(r);
        const bool atLeft = qAbs(r.x() - 16) <= 1;
        // 卡片父控件必须是窗口：页面坐标带顶栏 / 侧栏偏移，会算错落点
        const bool parentIsWindow = (card->parentWidget() == win);
        fprintf(stderr,
                "[CHECK] %s card parent=%s pos=(%d,%d) size=%dx%d inside=%s left=%s\n",
                tag,
                card->parentWidget() ? card->parentWidget()->metaObject()->className() : "null",
                r.x(), r.y(), r.width(), r.height(), inside ? "yes" : "NO",
                atLeft ? "yes" : "NO");
        if (!inside || !atLeft || !parentIsWindow) {
            fprintf(stderr, "[FAIL] %s：卡片跑出窗口或挂错了父控件\n", tag);
            ++failures;
        }
        if (!lowest || r.y() > lowest->y())
            lowest = card;
    }
    // 最底下那张必须贴住窗口底部（上方的是堆叠卡片，允许逐渐上移）
    if (lowest) {
        const int gap = (lowest->y() + lowest->height()) - (win->height() - 16);
        if (qAbs(gap) > 1) {
            fprintf(stderr, "[FAIL] %s：最底卡片底部与窗口底边间距 %dpx（期望 16px）\n",
                    tag, win->height() - (lowest->y() + lowest->height()));
            ++failures;
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("BlockBox"));

    QMainWindow win;
    win.setWindowFlags(Qt::FramelessWindowHint);
    win.resize(1000, 700);
    QWidget *page = makePage(&win);
    win.show();
    settle(120);

    QDir().mkpath(QStringLiteral("probe_out"));

    // ── 1) 普通提示：必须贴在窗口左下角 ──
    NotificationManager::showInfo(page, QStringLiteral("已复制链接到剪贴板"));
    settle(500);
    check(&win, "normal");
    win.grab().save(QStringLiteral("probe_out/notification_card_normal.png"));

    // ── 2) 三张卡片堆叠：全部在窗口内，越旧越靠上 ──
    NotificationManager::showSuccess(page, QStringLiteral("文件已保存到 D:/BlockBox/mods"));
    NotificationManager::showError(page, QStringLiteral("写入失败：文件夹不可写"));
    settle(500);
    check(&win, "stack3");
    win.grab().save(QStringLiteral("probe_out/notification_card_stack3.png"));

    // ── 3) 窄窗口 + 长文案：卡片宽度收进窗口，文字不被裁掉 ──
    win.resize(430, 320);
    settle(200);
    NotificationManager::showInfo(
        page, QStringLiteral("安装失败：无法写入 D:/BlockBox/instances/示例实例/mods/example-fabric-1.0.0.jar，请检查文件夹权限后重试"));
    settle(500);
    check(&win, "narrow-long");
    win.grab().save(QStringLiteral("probe_out/notification_card_narrow.png"));

    // ── 4) 窗口从大改小后，卡片要跟着重新贴身（不能留在旧坐标）──
    win.resize(1400, 900);
    settle(200);
    check(&win, "resize-bigger");
    win.grab().save(QStringLiteral("probe_out/notification_card_resize.png"));

    fprintf(stderr, "[probe] %s（failures=%d）\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
