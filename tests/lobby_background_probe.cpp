/**
 * @file   lobby_background_probe.cpp
 * @brief  联机大厅背景回归探针：自定义背景必须能从大厅页透出来
 *
 * 复刻主窗口的关键层次（BackgroundWidget → #contentWrapper → QStackedWidget →
 * CommunityForumPage → 联机大厅浮层），背景层涂纯品红 #FF20E0，然后：
 *   1. 打开联机大厅（真实 CommunityForumPage 的浮层 + 真实大厅页）；
 *   2. 在大厅页的空白边距上取色：透明链路生效 ⇒ 是品红，
 *      若被浮层 / 页面底色挡住 ⇒ 变成主题基色（浅色即接近白色）；
 *   3. 顶部横幅仍要正常渐变，确认不是整页被抠空；
 *   4. 退回社区页后论坛正文重新显示，大厅浮层隐藏。
 *
 * 输出：.tmp_lobby_bg/probe_out/lobby_bg_*.png
 * 退出码 0 = 全部断言通过。
 */
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFrame>
#include <QImage>
#include <QMainWindow>
#include <QPixmap>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

// 探针要直接调 showLobbyPage / hideLobbyPage 并读取私有成员，只影响本翻译单元
#define private public
#include "pages/CommunityForumPage.h"
#include "pages/MultiplayerLobbyDetailPage.h"
#include "pages/MultiplayerLobbyPage.h"
#undef private

#include "components/BackgroundWidget.h"
#include "utils/BackgroundManager.h"
#include "utils/ThemeManager.h"

namespace
{
int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-56s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

void pump()
{
    for (int i = 0; i < 8; ++i)
    {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

bool isMagenta(const QColor &color)
{
    // 背景层固定 #FF20E0：允许一点抗锯齿/缩放的偏差
    return color.red() > 225 && color.green() < 70 && color.blue() > 190;
}

void reportPixel(const char *label, const QColor &color)
{
    fprintf(stderr, "[probe] %-30s #%02X%02X%02X\n", label, color.red(), color.green(),
            color.blue());
}

ForumLobbyRoom makeRoom(const QString &title, const QString &core, const QString &code,
                        const QString &version, const QString &loader, const QString &hostName,
                        const QString &status, int joinCount)
{
    ForumLobbyRoom room;
    room.id = title.size();
    room.title = title;
    room.core = core;
    room.coreLabel = ForumLobbyService::coreDisplayName(core);
    if (core == QLatin1String("terracotta"))
        room.inviteCode = code;
    else
        room.address = code;
    room.gameVersion = version;
    room.loader = loader;
    room.status = status;
    room.joinCount = joinCount;
    room.maxPlayers = 8;
    room.expiresIn = 600;
    room.createdAt = QDateTime::currentSecsSinceEpoch() - 300;
    room.host.displayName = hostName;
    return room;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    // 结束时要还原用户真实的背景设置；先切到探针自己的品红纯色，
    // 免得用户设置的壁纸路径（可能已经不存在）干扰这次的取色
    BackgroundManager *bgm = BackgroundManager::instance();
    const BackgroundManager::BackgroundMode oldMode = bgm->currentMode();
    const QString oldSolid = bgm->solidColor();
    bgm->setMode(BackgroundManager::SolidColor);
    bgm->setSolidColor(QStringLiteral("#FF20E0"));

    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    fprintf(stderr, "[probe] checkpoint theme\n");

    QDir().mkpath(QStringLiteral(".tmp_lobby_bg/probe_out"));

    QMainWindow window;
    window.resize(1200, 820);

    auto *central = new QWidget(&window);
    central->setObjectName(QStringLiteral("probeCentral"));
    window.setCentralWidget(central);

    auto *bgLayer = new BackgroundWidget(central);
    fprintf(stderr, "[probe] checkpoint bgwidget\n");
    bgLayer->setSolidColor(QColor(QStringLiteral("#FF20E0")));
    bgLayer->setMode(BackgroundWidget::SolidColor);
    bgLayer->lower();

    auto *contentWrapper = new QWidget(central);
    contentWrapper->setObjectName(QStringLiteral("contentWrapper"));
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(contentWrapper);

    auto *stack = new QStackedWidget(contentWrapper);
    auto *stackLayout = new QVBoxLayout(contentWrapper);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    stackLayout->setSpacing(0);
    stackLayout->addWidget(stack);

    auto *communityPage = new CommunityForumPage(stack);
    fprintf(stderr, "[probe] checkpoint community page\n");
    stack->addWidget(communityPage);
    stack->setCurrentWidget(communityPage);

    // 让主程序把「自定义背景」的透明规则也下发下来
    bgm->setMode(BackgroundManager::SolidColor);
    bgm->setSolidColor(QStringLiteral("#FF20E0"));
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    fprintf(stderr, "[probe] checkpoint restyle\n");

    window.show();
    pump();
    fprintf(stderr, "[probe] checkpoint shown\n");
    bgLayer->setGeometry(central->rect());
    pump();
    fprintf(stderr, "[probe] checkpoint geometry\n");

    // 打开联机大厅，并塞两间假房间，别让探针真的去请求论坛
    communityPage->showLobbyPage();
    fprintf(stderr, "[probe] checkpoint lobby opened\n");
    pump();
    fprintf(stderr, "[probe] checkpoint lobby pumped\n");

    MultiplayerLobbyPage *lobbyPage = communityPage->m_lobbyPage;
    expect("大厅页已创建", lobbyPage != nullptr);
    expect("论坛正文已收起", communityPage->m_contentScroll
                                && !communityPage->m_contentScroll->isVisible());
    if (!lobbyPage)
        return 1;

    lobbyPage->m_loading = true;
    if (lobbyPage->m_refreshTimer)
        lobbyPage->m_refreshTimer->stop();
    lobbyPage->m_rooms.clear();
    lobbyPage->m_rooms << makeRoom(QStringLiteral("周末生存服 · 一起盖房子"),
                                   QStringLiteral("terracotta"), QStringLiteral("TC-8F2K-91QX"),
                                   QStringLiteral("1.20.1"), QStringLiteral("Forge"),
                                   QStringLiteral("小明"), QStringLiteral("waiting"), 3);
    lobbyPage->m_rooms << makeRoom(QStringLiteral("红石服务器 · 建筑比赛"),
                                   QStringLiteral("hongshi"), QStringLiteral("mc.example.com:25565"),
                                   QStringLiteral("1.21.1"), QStringLiteral("Fabric"),
                                   QStringLiteral("阿星"), QStringLiteral("playing"), 6);
    lobbyPage->m_total = lobbyPage->m_rooms.size();
    lobbyPage->m_errorText.clear();
    lobbyPage->m_loadedOnce = true;
    lobbyPage->m_loading = false;
    lobbyPage->renderRooms();
    pump();

    const QImage shot = window.grab().toImage();
    const bool saved =
        shot.save(QStringLiteral(".tmp_lobby_bg/probe_out/lobby_bg_open.png"));
    expect("大厅截图已保存", saved);

    // ── 空白边距取色：页面四周留白处应当直接是背景层 ──
    const QPoint topGap = lobbyPage->mapTo(&window, QPoint(6, 6));
    const QPoint bottomGap = lobbyPage->mapTo(&window, QPoint(6, lobbyPage->height() - 6));
    const QColor topColor = shot.pixelColor(topGap);
    const QColor bottomColor = shot.pixelColor(bottomGap);
    reportPixel("顶部留白", topColor);
    reportPixel("底部留白", bottomColor);
    expect("大厅页顶部留白透出背景", isMagenta(topColor));
    expect("大厅页底部留白透出背景", isMagenta(bottomColor));

    // ── 横幅仍然正常绘制（不是整页被抠空） ──
    QFrame *hero = lobbyPage->findChild<QFrame *>(QStringLiteral("mlHero"));
    expect("大厅横幅存在", hero != nullptr);
    if (hero)
    {
        const QPoint heroPoint = hero->mapTo(&window, QPoint(14, hero->height() / 2));
        const QColor heroColor = shot.pixelColor(heroPoint);
        reportPixel("横幅内部", heroColor);
        expect("横幅不是背景色（主题渐变还在）", !isMagenta(heroColor));
    }

    // ── 房间详情页：同样要能透出背景，且底下的列表必须收起来 ──
    lobbyPage->openRoomDetail(lobbyPage->m_rooms.first());
    pump();
    expect("详情页已打开", lobbyPage->m_detailPage != nullptr && lobbyPage->isDetailVisible());
    const QImage detailShot = window.grab().toImage();
    detailShot.save(QStringLiteral(".tmp_lobby_bg/probe_out/lobby_bg_detail.png"));
    if (lobbyPage->m_detailPage)
    {
        const QPoint detailGap = lobbyPage->m_detailPage->mapTo(&window, QPoint(5, 5));
        const QColor detailColor = detailShot.pixelColor(detailGap);
        reportPixel("详情页留白", detailColor);
        expect("详情页留白透出背景", isMagenta(detailColor));
    }
    lobbyPage->hideRoomDetail();
    pump();
    expect("详情页关闭后列表回来", lobbyPage->m_scroll && lobbyPage->m_scroll->isVisible());

    // ── 退回到社区页：论坛正文回来、浮层消失 ──
    communityPage->hideLobbyPage();
    pump();
    expect("退回后论坛正文恢复", communityPage->m_contentScroll->isVisible());
    expect("退回后大厅浮层隐藏", !communityPage->m_lobbyOverlay->isVisible());
    const QImage back = window.grab().toImage();
    back.save(QStringLiteral(".tmp_lobby_bg/probe_out/lobby_bg_closed.png"));

    // ── 经典背景 + 深色主题：不铺自定义背景时，大厅要由主内容区垫深色底 ──
    bgLayer->setMode(BackgroundWidget::Classic);
    bgm->setMode(BackgroundManager::Classic);
    ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    pump();
    const QImage darkShot = window.grab().toImage();
    darkShot.save(QStringLiteral(".tmp_lobby_bg/probe_out/lobby_bg_dark_classic.png"));
    const QPoint darkGap = lobbyPage->mapTo(&window, QPoint(6, 6));
    const QColor darkColor = darkShot.pixelColor(darkGap);
    reportPixel("经典背景深色留白", darkColor);
    expect("经典背景深色主题下不是背景色", !isMagenta(darkColor));
    expect("经典背景深色主题下留白为深色", darkColor.red() < 110 && darkColor.green() < 110
                                               && darkColor.blue() < 110);
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    pump();

    // 还原设置
    bgm->setSolidColor(oldSolid);
    bgm->setMode(oldMode);
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);

    fprintf(stderr, "[probe] %s (failures=%d)\n",
            g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
