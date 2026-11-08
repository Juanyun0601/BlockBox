/**
 * @file   lobby_page_style_probe.cpp
 * @brief  联机大厅页面样式探针：真实页面 + 真实主题 QSS，截图核对排版
 *
 * 不启动 MainWindow，只把 MultiplayerLobbyPage 放进一个空窗口，注入几间
 * 假房间后截图，用来核对：
 *   1. 顶部横幅 / 筛选栏 / 房间卡 / 空状态 / 出错状态的实际排版；
 *   2. 亮色与暗色主题下文字、边框、按钮是否都可读（换主题后重算）；
 *   3. 页面样式表里的 @TOKEN@ 是否全部解析完（残留占位符会让 Qt 丢掉规则）。
 *
 * 输出：.tmp_lobby_probe/probe_out/lobby_*.png
 * 退出码 0 = 全部断言通过。
 */
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "utils/ForumLobbyService.h"
#include "utils/ThemeManager.h"

// 探针需要注入房间数据并直接重绘列表，这里临时放开访问权限（只影响本翻译单元）
#define private public
#include "pages/MultiplayerLobbyPage.h"
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

bool savePng(QWidget *widget, const QString &path)
{
    if (!widget)
        return false;
    const QPixmap shot = widget->grab();
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(), shot.height(),
            ok ? "ok" : "FAILED");
    return ok;
}

ForumLobbyRoom makeRoom(const QString &title, const QString &core, const QString &code,
                        const QString &version, const QString &loader, const QString &hostName,
                        const QString &status, int joinCount, bool mine = false)
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
    room.createdAt = QDateTime::currentSecsSinceEpoch() - 420;
    room.host.displayName = hostName;
    room.mine = mine;
    return room;
}

void pump()
{
    for (int i = 0; i < 6; ++i)
    {
        QApplication::processEvents();
        // deleteLater 的对象要等 DeferredDelete 事件，processEvents 默认不处理
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);

    QDir().mkpath(QStringLiteral(".tmp_lobby_probe/probe_out"));

    auto *host = new QWidget;
    host->setObjectName(QStringLiteral("probeHost"));
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    auto *page = new MultiplayerLobbyPage(host);
    hostLayout->addWidget(page);

    // 先挂上 m_loading，避免 showEvent 真的去请求大厅（离线跑探针）
    page->m_loading = true;
    if (page->m_refreshTimer)
        page->m_refreshTimer->stop();

    host->resize(1200, 820);
    host->show();
    pump();

    // ---- 1. 样式表里的占位符必须全部解析完 ----
    const QString sheet = page->styleSheet();
    expect("页面样式表非空", !sheet.isEmpty());
    expect("样式表里没有残留的 @TOKEN@", !sheet.contains(QLatin1Char('@')));
    expect("搜索框有放大镜图标", page->m_searchIcon != nullptr);

    // ---- 2. 有房间：亮色主题 ----
    QVector<ForumLobbyRoom> rooms;
    ForumLobbyRoom terracotta =
        makeRoom(QStringLiteral("周末生存服 · 一起盖房子"), QStringLiteral("terracotta"),
                 QStringLiteral("TC-8F2K-91QX"), QStringLiteral("1.20.1"),
                 QStringLiteral("Forge"), QStringLiteral("小明"), QStringLiteral("waiting"), 3, true);
    terracotta.description = QStringLiteral("纯生存，装了光影和优化模组，晚上开黑挖矿。");
    terracotta.modpackMode = QStringLiteral("online");
    terracotta.modpackName = QStringLiteral("整合包 · 光影生存");
    rooms << terracotta;

    ForumLobbyRoom redstone =
        makeRoom(QStringLiteral("红石服务器 · 建筑比赛"), QStringLiteral("hongshi"),
                 QStringLiteral("mc.example.com:25565"), QStringLiteral("1.21.1"),
                 QStringLiteral("Fabric"), QStringLiteral("阿星"), QStringLiteral("playing"), 6);
    redstone.description = QStringLiteral("本周主题：中世纪城堡，评分最高的人拿称号。");
    rooms << redstone;

    ForumLobbyRoom frp =
        makeRoom(QStringLiteral("内网穿透测试房"), QStringLiteral("frp"),
                 QStringLiteral("frp-abc.top:25565"), QStringLiteral("1.20.4"),
                 QStringLiteral("NeoForge"), QStringLiteral("路人甲"), QStringLiteral("full"), 8);
    rooms << frp;

    ForumLobbyRoom empty = makeRoom(QStringLiteral("刚开的小房间"), QStringLiteral("terracotta"),
                                    QString(), QString(), QString(), QString(),
                                    QStringLiteral("waiting"), 0);
    empty.expiresIn = 0;   // 已下架
    rooms << empty;

    page->m_rooms = rooms;
    page->m_total = rooms.size();
    page->m_errorText.clear();
    page->m_loadedOnce = true;
    page->m_loading = false;
    page->renderRooms();
    pump();

    expect("房间卡数量与房间数一致",
           page->findChildren<QLabel *>(QStringLiteral("mlCardTitle")).size() == rooms.size());
    expect("有「我的房间」徽记", !page->findChildren<QLabel *>(QStringLiteral("mlChipMine")).isEmpty());
    expect("有前置整合包胶囊", page->findChildren<QLabel *>(QStringLiteral("mlChip")).size() >= 2);
    expect("封面缩略图已生成", !page->findChildren<QWidget *>(QStringLiteral("mlCodeBox")).isEmpty());

    const QList<QPushButton *> joinButtons =
        page->findChildren<QPushButton *>(QStringLiteral("mlJoinBtn"));
    expect("每间房都有一个加入按钮", joinButtons.size() == rooms.size());
    int disabled = 0;
    for (QPushButton *button : joinButtons)
    {
        if (!button->isEnabled())
            ++disabled;
    }
    expect("已下架的房间加入按钮置灰", disabled == 1);

    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_light.png"));

    // ---- 3. 暗色主题：同一份内容重新上色 ----
    ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    pump();
    page->renderRooms();
    pump();
    expect("暗色主题下样式表仍无残留占位符", !page->styleSheet().contains(QLatin1Char('@')));
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_dark.png"));

    // ---- 4. 空状态（暗色） ----
    page->m_rooms.clear();
    page->m_total = 0;
    page->m_errorText.clear();
    page->renderRooms();
    pump();
    expect("空状态出现占位卡", !page->findChildren<QFrame *>(QStringLiteral("mlStateCard")).isEmpty());
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_empty_dark.png"));

    // ---- 5. 连不上（亮色） ----
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    page->m_errorText = QStringLiteral("连接超时");
    page->renderRooms();
    pump();
    expect("出错状态也有占位卡", !page->findChildren<QFrame *>(QStringLiteral("mlStateCard")).isEmpty());
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_error_light.png"));

    // ---- 6. 窄窗口（手机比例）下不溢出 ----
    page->m_errorText.clear();
    page->m_rooms = rooms;
    page->renderRooms();
    host->resize(420, 760);
    pump();
    fprintf(stderr, "[probe] narrow: host=%dx%d page=%dx%d compact=%d dir=%d\n",
            host->width(), host->height(), page->width(), page->height(),
            int(page->m_compactLayout),
            page->m_heroLayout ? int(page->m_heroLayout->direction()) : -1);
    pump();
    fprintf(stderr, "[probe] page minHint=%dx%d\n", page->minimumSizeHint().width(), page->minimumSizeHint().height());
    for (QWidget *w : page->findChildren<QWidget *>()) {
        if (w->parentWidget() != page) continue;
        fprintf(stderr, "[probe]   child %s min=%d\n", qPrintable(w->objectName()), w->minimumSizeHint().width());
    }
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_narrow_light.png"));

    fprintf(stderr, "[probe] %s (failures=%d)\n", g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL",
            g_failures);
    return g_failures == 0 ? 0 : 1;
}
