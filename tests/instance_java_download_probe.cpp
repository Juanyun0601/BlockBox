/**
 * @file   instance_java_download_probe.cpp
 * @brief  实例助手 Java 下载页（窄窗口）离屏探针：亮/暗主题各一张
 * @author BlockBox Team
 * @date   2026-11-07
 *
 * 用法：
 *   pwsh .syntax_check/build_probe_win_release.ps1 -Name instance_java_download_probe `
 *        -Overrides build-win-release/probe_out/qrc_resources_data.cpp
 */

#include <QApplication>
#include <QColor>
#include <QEventLoop>
#include <QImage>
#include <QPalette>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "pages/InstanceJavaDownloadPage.h"
#include "utils/ThemeManager.h"

namespace {

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void shoot(ThemeManager::ThemeType theme, const QString &themeName)
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    ThemeManager::setTheme(app, theme);

    QWidget host;
    host.setAutoFillBackground(true);
    QPalette pal = host.palette();
    pal.setColor(QPalette::Window,
                 theme == ThemeManager::DarkTheme ? QColor(QStringLiteral("#1a1a1a"))
                                                  : QColor(QStringLiteral("#ffffff")));
    host.setPalette(pal);
    host.resize(450, 780);

    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *page = new InstanceJavaDownloadPage(&host);
    layout->addWidget(page);

    host.show();
    QApplication::processEvents();
    waitMs(2400);
    for (int i = 0; i < 30 && JavaDownloader::instance()->status() != JavaDownloadStatus::Idle; ++i)
        waitMs(200);
    QApplication::processEvents();

    const QString path = QStringLiteral("probe_out/instance_java_download_%1.png").arg(themeName);
    std::printf("[%s] %s\n", host.grab().save(path) ? "PASS" : "FAIL", qUtf8Printable(path));
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QApplication::setApplicationName(QStringLiteral("Settings"));

    shoot(ThemeManager::LightTheme, QStringLiteral("light"));
    shoot(ThemeManager::DarkTheme, QStringLiteral("dark"));
    return 0;
}
