/**
 * @file   dark_audit_probe.cpp
 * @brief  深色模式整窗巡检探针（不参与主程序构建，单独编译链接）
 *
 * 目的：在真实 MainWindow + 真实 style.qss（深色 token）下，把侧边栏各主页面
 * 与设置/资源子页逐页截图，用于人工比对深色下的发白、看不清、卡片没层次等问题。
 *
 * 用法（构建目录下）：参见 tests/build_dark_audit_probe.bat
 * 输出：build-win-release/probe_out/dark_audit/*.png
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QSettings>
#include <QWidget>

#include <cstdio>

#include "utils/ThemeManager.h"

namespace {

int g_fail = 0;

void msgHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (msg.contains(QLatin1String("Could not parse")))
        ++g_fail;
    const char *tag = (type == QtWarningMsg) ? "[warn ]" : "[info ]";
    std::fprintf(stderr, "%s %s\n", tag, msg.toLocal8Bit().constData());
    std::fflush(stderr);
}

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

QString outDir()
{
    const QString dir = QDir(qApp->applicationDirPath()).filePath(QStringLiteral("probe_out/dark_audit"));
    QDir().mkpath(dir);
    return dir;
}

void shot(QWidget *w, const QString &name)
{
    const QString path = outDir() + QLatin1Char('/') + name;
    const bool ok = w->grab().save(path);
    std::fprintf(stderr, "[%s] %s\n", ok ? "shot" : "FAIL", qUtf8Printable(path));
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(msgHandler);
    QApplication app(argc, argv);

    // 强制深色（只改内存态，不动用户设置）
    ThemeManager::setTheme(&app, ThemeManager::DarkTheme);
    std::fprintf(stderr, "[probe] theme = dark\n");

    MainWindow w;
    w.resize(1360, 860);
    w.show();
    settle(2500);
    shot(&w, QStringLiteral("00_startup.png"));

    // 侧边栏：0 首页 / 1 资源 / 2 设置 / 3 AI 助手 / 4 实例管理(隐藏) / 5 插件 / 6 社区 / 7 发布 / 8 反馈
    static const struct { int index; const char *name; } pages[] = {
        {0, "10_home"},
        {1, "11_resources"},
        {2, "12_settings"},
        {3, "13_ai"},
        {5, "15_plugins"},
        {6, "16_community"},
        {7, "17_publish"},
        {8, "18_feedback"},
    };

    for (const auto &p : pages) {
        QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Qt::QueuedConnection,
                                  Q_ARG(int, p.index));
        settle(2600);
        shot(&w, QString::fromLatin1(p.name) + QStringLiteral(".png"));
    }

    // 设置子页：逐项点到界面设置之后的所有页
    QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Qt::QueuedConnection, Q_ARG(int, 2));
    settle(2000);
    for (int i = 0; i < 10; ++i) {
        QMetaObject::invokeMethod(&w, "onSettingsNavItemClicked", Qt::QueuedConnection,
                                  Q_ARG(int, i));
        settle(1200);
        shot(&w, QStringLiteral("20_settings_%1.png").arg(i));
    }

    // 资源子页
    QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Qt::QueuedConnection, Q_ARG(int, 1));
    settle(2000);
    for (int i = 0; i < 8; ++i) {
        QMetaObject::invokeMethod(&w, "onResourcesNavItemClicked", Qt::QueuedConnection,
                                  Q_ARG(int, i));
        settle(1200);
        shot(&w, QStringLiteral("30_resources_%1.png").arg(i));
    }

    // 浅色主题回归：确认改动没影响亮色观感
    ThemeManager::setTheme(&app, ThemeManager::LightTheme);
    QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Qt::QueuedConnection, Q_ARG(int, 0));
    settle(2500);
    shot(&w, QStringLiteral("90_light_home.png"));
    QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Qt::QueuedConnection, Q_ARG(int, 2));
    settle(2200);
    shot(&w, QStringLiteral("91_light_settings.png"));
    QMetaObject::invokeMethod(&w, "onSideBarItemClicked", Qt::QueuedConnection, Q_ARG(int, 1));
    settle(2200);
    shot(&w, QStringLiteral("92_light_resources.png"));

    std::fprintf(stderr, "[probe] done, fail=%d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
