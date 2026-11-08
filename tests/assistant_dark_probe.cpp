/**
 * @file   assistant_dark_probe.cpp
 * @brief  实例助手窗口底色探针：确认深色主题下页面区域的真实底色
 *
 * 只回答两件事：
 *   1. 窗外层原来用的 palette(window) 在深色主题下到底是什么颜色；
 *   2. 「联机」页放进真实窗口后，深色 / 亮色下底色与卡片是否协调。
 *
 * 输出：.tmp_mp_probe/probe_out/assistant_*.png（首页 + 联机页，亮/暗各一组）
 */
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QPixmap>
#include <QTimer>

#include <cstdio>

#include "utils/ThemeManager.h"

#define private public
#include "components/BedrockInstanceAssistantWindow.h"
#include "components/InstanceAssistantWindow.h"
#undef private

namespace
{
void pump()
{
    for (int i = 0; i < 8; ++i)
        QApplication::processEvents();
}

/// 等一截真实时间：切页有 250ms 淡入动画，只 processEvents 拍不到最终画面
void waitMs(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    for (const QString &path : {QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
                                QStringLiteral("C:/Windows/Fonts/msyhbd.ttc")})
        QFontDatabase::addApplicationFont(path);
    QFont probeFont(QStringLiteral("Microsoft YaHei"));
    probeFont.setPixelSize(13);
    app.setFont(probeFont);

    QDir().mkpath(QStringLiteral(".tmp_mp_probe/probe_out"));

    BedrockInstanceAssistantWindow *bedrockWindow = nullptr;
    auto *window = new InstanceAssistantWindow;
    window->resize(450, 760);
    window->show();
    pump();

    for (int pass = 0; pass < 2; ++pass)
    {
        const bool dark = pass == 1;
        ThemeManager::instance()->loadTheme(dark ? ThemeManager::DarkTheme
                                                 : ThemeManager::LightTheme);
        pump();

        // 首页
        window->switchToTab(0);
        waitMs(400);
        pump();
        QPixmap home = window->grab();
        home.save(QStringLiteral(".tmp_mp_probe/probe_out/assistant_%1_home.png")
                      .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));

        // 联机页
        window->switchToTab(3);
        waitMs(500);
        pump();
        QPixmap lobby = window->grab();
        lobby.save(QStringLiteral(".tmp_mp_probe/probe_out/assistant_%1_lobby.png")
                       .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));

        const QImage image = lobby.toImage();
        const QColor body = image.pixelColor(image.width() / 2, image.height() - 40);
        const QColor side = image.pixelColor(6, image.height() - 40);
        fprintf(stderr, "[probe] %s body=%s side=%s\n",
                dark ? "dark" : "light", qPrintable(body.name()), qPrintable(side.name()));

        // 基岩版实例助手：同一窗口结构，确认联机页 + 首页底色一致
        if (!bedrockWindow)
        {
            bedrockWindow = new BedrockInstanceAssistantWindow;
            bedrockWindow->resize(450, 760);
            bedrockWindow->show();
            pump();
        }
        bedrockWindow->switchToTab(0);
        waitMs(400);
        bedrockWindow->grab().save(
            QStringLiteral(".tmp_mp_probe/probe_out/assistant_bedrock_%1_home.png")
                .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
        bedrockWindow->switchToTab(3);
        waitMs(500);
        QPixmap bedrockLobby = bedrockWindow->grab();
        bedrockLobby.save(QStringLiteral(".tmp_mp_probe/probe_out/assistant_bedrock_%1_lobby.png")
                              .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
        const QImage bedrockImage = bedrockLobby.toImage();
        fprintf(stderr, "[probe] bedrock %s body=%s\n", dark ? "dark" : "light",
                qPrintable(bedrockImage.pixelColor(bedrockImage.width() / 2,
                                                   bedrockImage.height() - 40).name()));
    }

    return 0;
}
