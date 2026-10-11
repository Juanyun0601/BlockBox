/**
 * @file   taskbar_visual_probe.cpp
 * @brief  底部任务栏（TaskBar 悬浮玻璃托盘 + 任务芯片）视觉探针
 *
 * 链接主程序全部目标文件，仅替换 main.cpp：
 *  - 真实 style.qss（经 ThemeManager 解析 token）
 *  - 真实 TaskBar / DownloadTaskCard / LaunchTaskCard
 *  - 通过 DownloadTaskManager 只登记任务（不触发网络下载）
 *  - 输出：D:/BlockBox/_out/taskbar_light.png / taskbar_light_overflow.png /
 *          taskbar_dark.png 以及 3x 放大细节图
 */
#include <QApplication>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEvent>
#include <QEventLoop>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdio>

#include "components/DownloadTaskCard.h"
#include "components/TaskBar.h"
#include "utils/DownloadTaskManager.h"
#include "utils/ThemeManager.h"

namespace {

int g_parseWarnings = 0;
int g_fail = 0;

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
    std::fprintf(stderr, "[%s] save %s\n", ok ? "ok" : "FAIL",
                 qUtf8Printable(path));
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
        if (ThemeManager::instance()->currentTheme() == ThemeManager::LightTheme)
        {
            g.setColorAt(0.0, QColor("#dbeafe"));
            g.setColorAt(0.55, QColor("#e0f2fe"));
            g.setColorAt(1.0, QColor("#bbf7d0"));
        }
        else
        {
            g.setColorAt(0.0, QColor("#0f172a"));
            g.setColorAt(0.55, QColor("#1e293b"));
            g.setColorAt(1.0, QColor("#14532d"));
        }
        p.fillRect(rect(), g);
    }
};

QString addTask(const QString &name, const QString &loaders, DownloadTaskStatus status,
                double progress, double speedMB, qint64 totalMB)
{
    DownloadTaskManager *mgr = DownloadTaskManager::instance();
    const QString id = mgr->addTask(name, QStringLiteral("D:/instances/") + name,
                                    QStringLiteral("1.21.1"), loaders.split(QLatin1Char(' '),
                                                                           Qt::SkipEmptyParts));
    mgr->updateTaskStatus(id, status);
    mgr->updateTaskProgressDouble(id, progress, static_cast<qint64>(totalMB * progress / 100.0)
                                                   * 1024 * 1024,
                                  totalMB * 1024 * 1024,
                                  static_cast<qint64>(speedMB * 1024 * 1024));
    mgr->updateTaskCurrentFile(id, QStringLiteral("assets/minecraft/textures/block/deepslate.png"));
    mgr->updateTaskStep(id, status == DownloadTaskStatus::Paused
                                ? QStringLiteral("已暂停")
                                : QStringLiteral("下载资源文件"));
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

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(msgHandler);

    QApplication app(argc, argv);

    // 探针不修改用户主题设置
    QSettings themeSettings(QStringLiteral("BlockBox"), QStringLiteral("Settings"));
    const QString savedTheme = themeSettings.value(QStringLiteral("theme"),
                                                   QStringLiteral("light")).toString();
    const auto restoreTheme = [&]() {
        if (savedTheme == QLatin1String("dark"))
            ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
        else if (savedTheme == QLatin1String("custom"))
            ThemeManager::instance()->loadTheme(ThemeManager::CustomTheme);
        else
            ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
        settle(300);
    };

    const auto run = [&](ThemeManager::ThemeType theme, const QString &tag, int taskCount,
                         bool withLaunch) {
        ThemeManager::instance()->loadTheme(theme);
        settle(350);
        clearTasks();

        Backdrop w;
        w.resize(1240, 360);
        auto *lay = new QVBoxLayout(&w);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        lay->addStretch(1); // 任务栏压在窗口底部，四周留白透出背景
        auto *bar = new TaskBar(&w);
        lay->addWidget(bar);

        if (withLaunch)
        {
            bar->showLaunchTaskCard(true);
            bar->updateLaunchTaskStatus(LaunchTaskCard::Launching);
            bar->updateLaunchTaskProgress(63);
            bar->updateLaunchTaskMessage(QStringLiteral("检查并补全文件 core-1.21.1.jar"));
        }

        const QStringList names = {
            QStringLiteral("机械动力生存 1.21.1"),
            QStringLiteral("Fabulously Optimized 光影整合包"),
            QStringLiteral("Create-A-Long-Instance-Name-That-Should-Be-Ellipsized"),
            QStringLiteral("原版纯净 1.20.1"),
            QStringLiteral("暮色森林冒险"),
            QStringLiteral("空岛科技 2"),
        };
        const QStringList loaders = {
            QStringLiteral("Fabric"), QStringLiteral("Fabric Iris"), QStringLiteral(""),
            QStringLiteral("Vanilla"), QStringLiteral("Forge"), QStringLiteral("NeoForge"),
        };
        QList<QString> ids;
        for (int i = 0; i < taskCount; ++i)
        {
            const DownloadTaskStatus st = (i % 3 == 2) ? DownloadTaskStatus::Paused
                                                       : DownloadTaskStatus::Downloading;
            ids << addTask(names[i % names.size()], loaders[i % loaders.size()], st,
                           12.0 + i * 17.5, 3.2 + i * 2.1, 520 + i * 90);
            settle(60);
        }

        settle(900); // 等进出动画结束

        w.show();
        settle(500);
        w.raise();

        const QString base = QStringLiteral("D:/BlockBox/_out/taskbar_%1").arg(tag);
        saveShot(&w, base + QStringLiteral(".png"));

        // 3x 放大细节图：截任务栏区域（按控件在窗口内的实际位置换算）
        const QPoint barTopLeft = bar->mapTo(&w, QPoint(0, 0));
        const QRect barRect(QPoint(qMax(0, barTopLeft.x()),
                                   qMax(0, barTopLeft.y() - 6)),
                            QSize(w.width(), bar->height() + 12));
        const QPixmap shot = w.grab(barRect);
        QPixmap zoom(shot.size() * 3);
        zoom.setDevicePixelRatio(3.0);
        zoom.fill(Qt::transparent);
        {
            QPainter p(&zoom);
            p.drawPixmap(QPoint(0, 0), shot);
        }
        zoom.setDevicePixelRatio(1.0);
        zoom.save(base + QStringLiteral("_zoom.png"));
        std::fprintf(stderr, "[ok ] save %s\n", qUtf8Printable(base + QStringLiteral("_zoom.png")));

        // hover 态：手工投递 Enter 事件，检查卡片描边是否点亮主题色
        if (taskCount > 0 && !withLaunch)
        {
            const auto cards = bar->findChildren<DownloadTaskCard *>();
            if (!cards.isEmpty())
            {
                DownloadTaskCard *card = cards.first();
                QEnterEvent enter(QPointF(8, 8), QPointF(8, 8), QPointF(8, 8));
                QApplication::sendEvent(card, &enter);
                settle(250);
                saveShot(&w, base + QStringLiteral("_hover.png"));
                QEvent leave(QEvent::Leave);
                QApplication::sendEvent(card, &leave);
                settle(150);
            }
        }

        expect(bar->isVisible(), tag + QStringLiteral(": task bar visible"));
        expect(!withLaunch || bar->launchTaskCard()->isVisible(),
               tag + QStringLiteral(": launch card visible"));

        clearTasks();
        w.close();
    };

    run(ThemeManager::LightTheme, QStringLiteral("light"), 2, true);
    run(ThemeManager::LightTheme, QStringLiteral("light_overflow"), 6, false);
    run(ThemeManager::DarkTheme, QStringLiteral("dark"), 2, true);

    restoreTheme();

    expect(g_parseWarnings == 0,
           QStringLiteral("stylesheet parses cleanly (warnings=%1)").arg(g_parseWarnings));

    std::fprintf(stderr, "\n%s (%d failures)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
