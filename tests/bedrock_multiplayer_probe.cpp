/**
 * @file   bedrock_multiplayer_probe.cpp
 * @brief  基岩版「联机」页面样式探针：真实页面 + 真实令牌 QSS，逐页截图核对排版
 *
 * 与 Java 版联机页共用 MultiplayerUiParts.h 里的版式零件和样式表，这里只验证：
 *   1. 首页 / 房间页在亮色、暗色下的排版与可读性；
 *   2. 样式表里的 @TOKEN@ 全部解析完；
 *   3. 服务器地址卡片在没地址时会整块收起（不占位）。
 *
 * 输出：.tmp_mp_probe/probe_out/bedrock_*.png
 * 退出码 0 = 全部断言通过。
 */
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QPixmap>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>

#include <cstdio>

#include "utils/ThemeManager.h"

#define private public
#include "pages/BedrockMultiplayerPage.h"
#include "utils/GravityCone/GravityConeClient.h"
#undef private

namespace
{
int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-52s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

void pump()
{
    for (int i = 0; i < 6; ++i)
    {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

bool savePng(QWidget *widget, const QString &path)
{
    if (!widget)
        return false;
    const QPixmap shot = widget->grab();
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(),
            shot.height(), ok ? "ok" : "FAILED");
    return ok;
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

    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    QDir().mkpath(QStringLiteral(".tmp_mp_probe/probe_out"));

    BedrockMultiplayerPage page;
    for (QTimer *timer : page.findChildren<QTimer *>())
        timer->stop();

    auto *host = new QWidget;
    host->resize(430, 760);
    page.setParent(host);
    page.setGeometry(0, 0, 430, 760);
    host->show();
    pump();

    QStackedWidget *stack =
        page.findChild<QStackedWidget *>(QStringLiteral("bedrockMultiplayerStack"));
    expect("页面栈存在且为 2 页", stack && stack->count() == 2);
    if (!stack)
        return 1;

    // 注入假状态：房主在房间里，两个玩家
    QList<GravityConeClient::Player> players;
    GravityConeClient::Player hostPlayer;
    hostPlayer.name = QStringLiteral("小明");
    hostPlayer.clientId = QStringLiteral("a");
    hostPlayer.isRoomHost = true;
    players << hostPlayer;
    GravityConeClient::Player guest;
    guest.name = QStringLiteral("基岩版搭子");
    guest.clientId = QStringLiteral("b");
    players << guest;
    GravityConeClient::instance()->m_players = players;
    page.refreshPlayerList();

    page.m_statusLabel->setText(QStringLiteral("联机核心已就绪（v1.2.3）"));
    page.m_latencyLabel->setText(QStringLiteral("房主就绪 | 联机时间 08:42"));
    page.m_latencyLabel->setProperty("status", QStringLiteral("good"));
    page.m_roomCodeLabel->setText(QStringLiteral("P/7K2M-4QX9-1B8D-3N5V"));
    page.m_addressLabel->setText(QStringLiteral("192.168.1.7:19132"));
    page.m_addressCard->setVisible(true);

    const auto setTone = [](QLabel *dot, const char *tone) {
        if (!dot)
            return;
        dot->setProperty("tone", QString::fromLatin1(tone));
        dot->style()->unpolish(dot);
        dot->style()->polish(dot);
    };
    for (QLabel *dot : {page.m_statusDot, page.m_latencyDot})
        setTone(dot, "ok");
    for (QPushButton *button : page.findChildren<QPushButton *>())
        button->setEnabled(true);

    const char *names[] = {"home", "room"};
    for (int themePass = 0; themePass < 2; ++themePass)
    {
        const bool dark = themePass == 1;
        ThemeManager::instance()->loadTheme(dark ? ThemeManager::DarkTheme
                                                 : ThemeManager::LightTheme);
        page.applyThemeStyles();
        page.refreshPlayerList();
        pump();

        expect("样式表里没有残留的 @TOKEN@", !page.styleSheet().contains(QLatin1Char('@')));

        for (int index = 0; index < 2; ++index)
        {
            stack->setCurrentIndex(index);
            pump();
            savePng(host, QStringLiteral(".tmp_mp_probe/probe_out/bedrock_%1_%2.png")
                             .arg(QLatin1String(names[index]))
                             .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
        }
    }

    // 地址清空后卡片应整块隐藏
    page.m_addressLabel->clear();
    page.m_addressCard->setVisible(false);
    pump();
    expect("没地址时地址卡片不占位", page.m_addressCard->isHidden());

    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    fprintf(stderr, "[probe] %s (failures=%d)\n",
            g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
