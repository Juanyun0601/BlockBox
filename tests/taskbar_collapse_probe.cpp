/**
 * @file   taskbar_collapse_probe.cpp
 * @brief  底部任务栏「收起 / 展开」探针
 *
 * 链接主程序全部目标文件，仅替换 main.cpp：
 *  - 真实 style.qss（经 ThemeManager 解析 token）
 *  - 真实 TaskBar / DownloadTaskCard
 *  - 通过 DownloadTaskManager 只登记任务（不触发网络下载）
 *  - 校验：展开几何 / 收起后只剩右下角小胶囊 / 点击按钮能收起和展开 /
 *          收起时新任务到达仍保持收起 / 任务清空后复位为展开
 *  - 输出：D:/BlockBox/_out/taskbar_collapse_*.png
 */
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QTimer>

#include <cstdio>

#include "components/DownloadTaskCard.h"
#include "components/TaskBar.h"
#include "utils/DownloadTaskManager.h"
#include "utils/ThemeManager.h"

namespace {

int g_parseWarnings = 0;
int g_fail = 0;

constexpr int kFloatMarginBottom = 24;
constexpr int kCollapsedVisibleHeight = 34;

void msgHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    if (msg.contains(QLatin1String("Could not parse"))
        || msg.contains(QLatin1String("unknown token")))
        ++g_parseWarnings;
    std::fprintf(stderr, "[qt ] %s\n", msg.toLocal8Bit().constData());
}

void expect(bool ok, const QString &what)
{
    std::fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", qUtf8Printable(what));
    if (!ok)
        ++g_fail;
}

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

bool saveShot(QWidget *w, const QString &path)
{
    const bool ok = w->grab().save(path);
    std::fprintf(stderr, "[%s] save %s\n", ok ? "ok" : "FAIL", qUtf8Printable(path));
    return ok;
}

/// 模拟壁纸底：托盘玻璃与卡片透明度需要有背景才看得出层次
class Backdrop : public QWidget
{
public:
    explicit Backdrop(QWidget *parent = nullptr) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QLinearGradient g(0, 0, width(), height());
        if (ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme) {
            g.setColorAt(0.0, QColor("#dbeafe"));
            g.setColorAt(0.55, QColor("#e0f2fe"));
            g.setColorAt(1.0, QColor("#bbf7d0"));
        } else {
            g.setColorAt(0.0, QColor("#0f172a"));
            g.setColorAt(0.55, QColor("#1e293b"));
            g.setColorAt(1.0, QColor("#14532d"));
        }
        p.fillRect(rect(), g);
    }
};

QString addTask(const QString &name, DownloadTaskStatus status, double progress, double speedMB,
                qint64 totalMB)
{
    DownloadTaskManager *mgr = DownloadTaskManager::instance();
    const QString id = mgr->addTask(name, QStringLiteral("D:/instances/") + name,
                                    QStringLiteral("1.21.1"), QStringList{QStringLiteral("Fabric")});
    mgr->updateTaskStatus(id, status);
    mgr->updateTaskProgressDouble(id, progress,
                                  static_cast<qint64>(totalMB * progress / 100.0) * 1024 * 1024,
                                  totalMB * 1024 * 1024,
                                  static_cast<qint64>(speedMB * 1024 * 1024));
    mgr->updateTaskCurrentFile(id, QStringLiteral("assets/minecraft/textures/block/deepslate.png"));
    mgr->updateTaskStep(id, QStringLiteral("下载资源文件"));
    return id;
}

void clearTasks()
{
    DownloadTaskManager *mgr = DownloadTaskManager::instance();
    const auto all = mgr->getAllTasks();
    for (const DownloadTask &t : all)
        mgr->removeTask(t.taskId);
    settle(120);
}

/// 截任务栏区域并放大 3 倍，方便肉眼检查收起后的小胶囊
void saveBarZoom(QWidget *host, TaskBar *bar, const QString &path)
{
    const QPoint topLeft = bar->mapTo(host, QPoint(0, 0));
    QRect r(topLeft, bar->size());
    r = r.intersected(QRect(0, 0, host->width(), host->height()));
    if (r.isEmpty())
        return;
    const QPixmap shot = host->grab(r);
    QPixmap zoom(shot.size() * 3);
    zoom.setDevicePixelRatio(3.0);
    zoom.fill(Qt::transparent);
    {
        QPainter p(&zoom);
        p.drawPixmap(QPoint(0, 0), shot);
    }
    zoom.setDevicePixelRatio(1.0);
    zoom.save(path);
    std::fprintf(stderr, "[ok ] save %s\n", qUtf8Printable(path));
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(msgHandler);

    QApplication app(argc, argv);

    // 探针不修改用户主题设置
    QSettings themeSettings(QStringLiteral("BlockBox"), QStringLiteral("Settings"));
    const QString savedTheme =
        themeSettings.value(QStringLiteral("theme"), QStringLiteral("light")).toString();
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    settle(400);

    const auto restoreTheme = [&]() {
        if (savedTheme == QLatin1String("dark"))
            ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
        else if (savedTheme == QLatin1String("custom"))
            ThemeManager::instance()->loadTheme(ThemeManager::CustomTheme);
        else
            ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
        settle(300);
    };

    clearTasks();

    Backdrop w;
    w.resize(1240, 420);
    auto *bar = new TaskBar(&w);
    bar->updateFloatGeometry();

    QPushButton *toggle = bar->findChild<QPushButton *>(QStringLiteral("taskBarToggleBtn"));
    QWidget *tray = bar->findChild<QWidget *>(QStringLiteral("taskBarTray"));
    expect(toggle != nullptr, QStringLiteral("toggle button exists"));
    expect(tray != nullptr, QStringLiteral("tray exists"));
    if (!toggle || !tray) {
        std::fprintf(stderr, "\nFAILURES (probe setup)\n");
        return 1;
    }

    QStringList ids;
    ids << addTask(QStringLiteral("机械动力生存 1.21.1"), DownloadTaskStatus::Downloading, 38.0,
                   6.4, 520)
        << addTask(QStringLiteral("原版纯净 1.20.1"), DownloadTaskStatus::Paused, 12.0, 0.0, 310);

    settle(900);
    w.show();
    settle(600);

    // ── 1. 展开态 ────────────────────────────────────────────────
    expect(bar->isVisible(), QStringLiteral("expanded: bar visible"));
    expect(!bar->isCollapsed(), QStringLiteral("expanded: not collapsed"));
    expect(toggle->text() == QStringLiteral("收起"),
           QStringLiteral("expanded: button says 收起 (got '%1')").arg(toggle->text()));
    expect(!toggle->icon().isNull(), QStringLiteral("expanded: chevron_down icon loaded"));
    expect(tray->isVisible(), QStringLiteral("expanded: tray visible"));
    expect(bar->y() == w.height() - bar->height() - kFloatMarginBottom,
           QStringLiteral("expanded: bar bottom-aligned (y=%1 expect %2)")
               .arg(bar->y())
               .arg(w.height() - bar->height() - kFloatMarginBottom));
    expect(bar->width() > 1000, QStringLiteral("expanded: bar is full width"));
    {
        const QPoint expTopLeft = toggle->mapTo(&w, QPoint(0, 0));
        expect(qAbs(expTopLeft.x() + toggle->width() / 2 - w.width() / 2) <= 2,
               QStringLiteral("expanded: toggle centered at taskbar top (center=%1 expect %2)")
                   .arg(expTopLeft.x() + toggle->width() / 2)
                   .arg(w.width() / 2));
    }
    saveShot(&w, QStringLiteral("D:/BlockBox/_out/taskbar_collapse_expanded.png"));
    saveBarZoom(&w, bar, QStringLiteral("D:/BlockBox/_out/taskbar_collapse_expanded_zoom.png"));

    // ── 2. 点「收起」：整条滑到窗口下沿，只剩右下角小胶囊 ──────────
    toggle->click();
    settle(900);

    expect(bar->isCollapsed(), QStringLiteral("collapsed: state = collapsed"));
    expect(bar->isVisible(), QStringLiteral("collapsed: bar still visible"));
    expect(!tray->isVisible(), QStringLiteral("collapsed: tray hidden"));
    expect(bar->y() == w.height() - kCollapsedVisibleHeight,
           QStringLiteral("collapsed: only toggle strip visible (y=%1 expect %2)")
               .arg(bar->y())
               .arg(w.height() - kCollapsedVisibleHeight));
    expect(bar->width() < 240, QStringLiteral("collapsed: narrowed to a pill (w=%1)")
                                   .arg(bar->width()));
    expect(toggle->isVisible(), QStringLiteral("collapsed: expand button visible"));
    expect(toggle->text().startsWith(QStringLiteral("展开")),
           QStringLiteral("collapsed: button says 展开 (got '%1')").arg(toggle->text()));
    expect(toggle->text().contains(QStringLiteral("2")),
           QStringLiteral("collapsed: button carries task count (got '%1')").arg(toggle->text()));
    expect(!toggle->icon().isNull(), QStringLiteral("collapsed: chevron_up icon loaded"));
    // 收起后按钮必须落在窗口可见范围内，且水平居中
    const QPoint btnTopLeft = toggle->mapTo(&w, QPoint(0, 0));
    const QRect btnRect(btnTopLeft, toggle->size());
    expect(btnRect.bottom() < w.height() && btnRect.top() > w.height() - 60,
           QStringLiteral("collapsed: button sits at window bottom edge (rect=%1,%2 %3x%4)")
               .arg(btnRect.x())
               .arg(btnRect.y())
               .arg(btnRect.width())
               .arg(btnRect.height()));
    expect(qAbs(btnRect.center().x() - w.width() / 2) <= 2,
           QStringLiteral("collapsed: button horizontally centered (center=%1 expect %2)")
               .arg(btnRect.center().x())
               .arg(w.width() / 2));
    saveShot(&w, QStringLiteral("D:/BlockBox/_out/taskbar_collapse_collapsed.png"));
    saveBarZoom(&w, bar, QStringLiteral("D:/BlockBox/_out/taskbar_collapse_collapsed_zoom.png"));

    // ── 3. 收起状态下新增任务：保持收起，只更新按钮上的数量 ──────────
    ids << addTask(QStringLiteral("暮色森林冒险"), DownloadTaskStatus::Downloading, 4.0, 2.1, 800);
    settle(700);
    expect(bar->isCollapsed(), QStringLiteral("collapsed + new task: still collapsed"));
    expect(!tray->isVisible(), QStringLiteral("collapsed + new task: tray stays hidden"));
    expect(toggle->text().contains(QStringLiteral("3")),
           QStringLiteral("collapsed + new task: count updated (got '%1')").arg(toggle->text()));
    saveBarZoom(&w, bar, QStringLiteral("D:/BlockBox/_out/taskbar_collapse_collapsed_count_zoom.png"));

    // ── 4. 点「展开」：整条滑回原位，卡片重新出现 ───────────────
    toggle->click();
    settle(1000);
    expect(!bar->isCollapsed(), QStringLiteral("expanded again: state = expanded"));
    expect(tray->isVisible(), QStringLiteral("expanded again: tray visible"));
    expect(bar->y() == w.height() - bar->height() - kFloatMarginBottom,
           QStringLiteral("expanded again: back to bottom-aligned position (y=%1)").arg(bar->y()));
    expect(bar->width() > 1000, QStringLiteral("expanded again: full width restored"));
    expect(toggle->text() == QStringLiteral("收起"),
           QStringLiteral("expanded again: button says 收起"));
    saveShot(&w, QStringLiteral("D:/BlockBox/_out/taskbar_collapse_reexpanded.png"));

    // ── 5. 收起后任务全部结束：任务栏整体隐藏，并复位成展开态 ────────
    toggle->click();
    settle(500);
    expect(bar->isCollapsed(), QStringLiteral("hide: collapsed before clearing tasks"));
    clearTasks();
    settle(1400);
    expect(!bar->isVisible(), QStringLiteral("hide: bar hidden when no tasks left"));
    expect(!bar->isCollapsed(), QStringLiteral("hide: collapsed flag reset for next appearance"));

    // ── 6. 无任务时直接 setCollapsed 不应崩，且立刻落位 ──────────────
    bar->setCollapsed(true, false);
    settle(200);
    expect(bar->isCollapsed(), QStringLiteral("hidden + setCollapsed(true,false): state applied"));
    bar->setCollapsed(false, false);
    settle(200);
    expect(!bar->isCollapsed(), QStringLiteral("hidden + setCollapsed(false,false): state applied"));

    // ── 7. 启动任务卡片：任务栏隐藏时也能被叫出来，收起/展开都跟着走 ──────
    clearTasks();
    settle(1300);
    expect(!bar->isVisible(), QStringLiteral("launch: bar hidden with no tasks"));

    const QString launchA = QStringLiteral("D:/instances/taskbar-launch-a");
    bar->ensureLaunchCard(launchA, QStringLiteral("任务栏探针实例"));
    bar->showLaunchCard(launchA, true);
    bar->updateLaunchCardStatus(launchA, LaunchTaskCard::Launching);
    bar->updateLaunchCardProgress(launchA, 63);
    bar->updateLaunchCardMessage(launchA, QStringLiteral("检查并补全文件"));
    settle(1000);

    expect(bar->isVisible(), QStringLiteral("launch: bar slides in for a launch card"));
    expect(!bar->isCollapsed(), QStringLiteral("launch: appears expanded"));
    LaunchTaskCard *cardA = bar->launchCard(launchA);
    expect(cardA && cardA->isVisible(), QStringLiteral("launch: launch card visible"));

    toggle->click();
    settle(900);
    expect(bar->isCollapsed(), QStringLiteral("launch: collapses with a running launch card"));
    expect(!cardA->isVisible(), QStringLiteral("launch: card hidden by collapsing"));
    expect(toggle->text().contains(QStringLiteral("1")),
           QStringLiteral("launch: collapsed pill counts launch task (got '%1')")
               .arg(toggle->text()));

    // 收起状态下再来一个启动任务：保持收起，数量加一
    const QString launchB = QStringLiteral("D:/instances/taskbar-launch-b");
    bar->ensureLaunchCard(launchB, QStringLiteral("副启动实例"));
    bar->showLaunchCard(launchB, true);
    bar->updateLaunchCardStatus(launchB, LaunchTaskCard::Launching);
    settle(700);
    expect(bar->isCollapsed(), QStringLiteral("launch: stays collapsed when a new card arrives"));
    expect(toggle->text().contains(QStringLiteral("2")),
           QStringLiteral("launch: collapsed pill counts both (got '%1')").arg(toggle->text()));

    toggle->click();
    settle(1000);
    expect(!bar->isCollapsed(), QStringLiteral("launch: expands again"));
    expect(cardA->isVisible() && bar->launchCard(launchB)->isVisible(),
           QStringLiteral("launch: both cards visible after expanding"));

    bar->removeLaunchCard(launchA);
    bar->removeLaunchCard(launchB);
    settle(1300);
    expect(!bar->isVisible(), QStringLiteral("launch: bar hides after cards removed"));
    expect(!bar->isCollapsed(), QStringLiteral("launch: collapsed flag reset"));

    restoreTheme();

    expect(g_parseWarnings == 0,
           QStringLiteral("stylesheet parses cleanly (warnings=%1)").arg(g_parseWarnings));

    std::fprintf(stderr, "\n%s (%d failures)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
