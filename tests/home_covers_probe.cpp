/**
 * @file   home_covers_probe.cpp
 * @brief  首页封面图空态探针：确认「下载预设图 / 导入风景图」引导卡的真实排版
 *
 * 链接主程序全部目标文件（仅替换 main.cpp），不起完整的启动流程（不弹协议窗、不进引导），
 * 直接创建 MainWindow 截取首页，用于人工确认：
 *   1. 一张封面图都没有时，轮播区显示引导卡而不是空白；
 *   2. 卡片里「下载预设图」「导入风景图」两个按钮都在，并且排版正常；
 *   3. 皮肤面板与公告栏不受影响（轮播区只换自己那一块）。
 *
 * 产物：home_covers_probe_window.png（整窗）、home_covers_probe_empty.png（引导卡特写）
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWidget>

#include "utils/ThemeManager.h"

namespace {

void describe(QWidget *root)
{
    if (!root) {
        qInfo().noquote() << "PROBE-FAIL: 没找到轮播空态卡 carouselEmpty";
        return;
    }

    qInfo().noquote() << QString("carouselEmpty geometry=%1x%2 visible=%3")
                             .arg(root->width()).arg(root->height())
                             .arg(root->isVisible() ? "yes" : "no");

    for (QPushButton *btn : root->findChildren<QPushButton *>()) {
        qInfo().noquote() << QString("  button objectName=%1 text=%2 enabled=%3 size=%4x%5")
                                 .arg(btn->objectName(), btn->text())
                                 .arg(btn->isEnabled() ? "yes" : "no")
                                 .arg(btn->width()).arg(btn->height());
    }
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->text().isEmpty())
            continue;
        qInfo().noquote() << QString("  label objectName=%1 text=%2")
                                 .arg(label->objectName(), label->text());
    }
}

} // namespace

int runProbe(int argc, char *argv[])
{    // 与正式启动一致：3D 皮肤预览需要 Core Profile 的默认表面格式
    QSurfaceFormat glFormat;
    glFormat.setVersion(3, 3);
    glFormat.setProfile(QSurfaceFormat::CoreProfile);
    glFormat.setDepthBufferSize(24);
    glFormat.setStencilBufferSize(0);
    glFormat.setSamples(0);
    QSurfaceFormat::setDefaultFormat(glFormat);

    QApplication app(argc, argv);
    ThemeManager::setTheme(&app, ThemeManager::LightTheme);

    MainWindow w;
    w.resize(1440, 940);
    w.show();
    QApplication::processEvents();

    // 等首页布局、公告栏与最近游玩扫描落定
    QTimer::singleShot(6000, &app, [&app, &w]() {
        const bool okWindow = w.grab().save(QStringLiteral("home_covers_probe_window.png"));
        qInfo().noquote() << "[shot] home_covers_probe_window.png" << (okWindow ? "ok" : "FAILED");

        QWidget *empty = w.findChild<QWidget *>(QStringLiteral("carouselEmpty"));
        describe(empty);
        if (empty) {
            const bool okEmpty = empty->grab().save(QStringLiteral("home_covers_probe_empty.png"));
            qInfo().noquote() << "[shot] home_covers_probe_empty.png" << (okEmpty ? "ok" : "FAILED");
        }

        // 深色主题再来一张，确认描边与文字在深色下也清楚
        ThemeManager::setTheme(&app, ThemeManager::DarkTheme);
        QApplication::processEvents();
        QTimer::singleShot(800, &app, [&app, &w]() {
            QWidget *emptyDark = w.findChild<QWidget *>(QStringLiteral("carouselEmpty"));
            if (emptyDark) {
                const bool ok = emptyDark->grab().save(QStringLiteral("home_covers_probe_empty_dark.png"));
                qInfo().noquote() << "[shot] home_covers_probe_empty_dark.png" << (ok ? "ok" : "FAILED");
            }
            qInfo().noquote() << "PROBE-DONE";
            app.quit();
        });
    });

    return app.exec();
}

int main(int argc, char *argv[])
{
    return runProbe(argc, argv);
}

#if defined(_WIN32)
#include <windows.h>
// 主程序的目标文件里带着 GUI 子系统的链接指令，这里补一个 WinMain
// 兜底：无论链接器最终选的是控制台入口还是窗口入口，都跑同一套探针逻辑。
extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return runProbe(__argc, __argv);
}
#endif

