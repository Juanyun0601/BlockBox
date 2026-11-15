/**
 * @file   lobby_page_style_probe.cpp
 * @brief  联机大厅页面样式探针：真实页面 + 真实主题 QSS，截图核对排版
 *
 * 不启动 MainWindow，只把 MultiplayerLobbyPage 放进一个空窗口，注入几间
 * 假房间后截图，用来核对：
 *   1. 顶部横幅 / 筛选栏 / 房间卡 / 空状态 / 出错状态的实际排版；
 *   2. 房间卡在列表与瀑布流两种视图下都与模组卡同款；
 *   3. 单击卡片进入的房间详情页（宽 / 窄 / 暗色）版式；
 *   4. 亮色与暗色主题下文字、边框、按钮是否都可读（换主题后重算）；
 *   5. 页面样式表里的 @TOKEN@ 是否全部解析完（残留占位符会让 Qt 丢掉规则）。
 *
 * 输出：.tmp_lobby_probe/probe_out/lobby_*.png
 * 退出码 0 = 全部断言通过。
 */
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "components/ContentViewSwitch.h"
#include "utils/ForumLobbyService.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"

// 探针需要注入房间数据并直接重绘列表，这里临时放开访问权限（只影响本翻译单元）
#define private public
#include "pages/MultiplayerLobbyPage.h"
#include "pages/MultiplayerLobbyDetailPage.h"
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
    for (int i = 0; i < 8; ++i)
    {
        QApplication::processEvents();
        // deleteLater 的对象要等 DeferredDelete 事件，processEvents 默认不处理
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

int countChildren(QWidget *root, const QString &name)
{
    return root->findChildren<QWidget *>(name).size();
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
    // 探针宿主铺当前主题底色，截图才和真实主窗口一致（页面本身是透明的）
    auto restyleHost = [host]() {
        host->setStyleSheet(StyleKit::resolve(
            QStringLiteral("QWidget#probeHost { background: @BG_BASE@; }")));
    };
    restyleHost();
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
    expect("有列表 / 瀑布流切换控件", page->m_viewSwitch != nullptr);

    // ---- 2. 有房间：亮色主题 + 列表视图 ----
    QVector<ForumLobbyRoom> rooms;
    ForumLobbyRoom terracotta =
        makeRoom(QStringLiteral("周末生存服 · 一起盖房子"), QStringLiteral("terracotta"),
                 QStringLiteral("TC-8F2K-91QX"), QStringLiteral("1.20.1"),
                 QStringLiteral("Forge"), QStringLiteral("小明"), QStringLiteral("waiting"), 3, true);
    terracotta.description = QStringLiteral("纯生存，装了光影和优化模组，晚上开黑挖矿。");
    terracotta.md = QStringLiteral(
        "## 房间说明\n\n"
        "- **版本**：1.20.1 Forge，装了 Create 和农夫乐事\n"
        "- 晚上八点开黑，白天随意\n\n"
        "> 进房先看告示牌，不要拆别人盖的房子。");
    terracotta.mods << QStringLiteral("create-1.20.1-0.5.1.f.jar")
                    << QStringLiteral("farmers-delight-1.20.1-1.2.4.jar")
                    << QStringLiteral("jei-1.20.1-forge-15.2.0.27.jar");
    terracotta.modpackMode = QStringLiteral("online");
    terracotta.modpackName = QStringLiteral("整合包 · 光影生存");
    terracotta.modpackUrl = QStringLiteral("https://forum.mcblockbox.top/packs/1");
    terracotta.modpackMeta = QStringLiteral(
        "{\"platform\":\"CurseForge\",\"author\":\"小明\",\"version\":\"1.4.2\"}");
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

    page->m_viewMode = static_cast<int>(ContentViewSwitch::List);
    page->renderRooms();
    pump();

    expect("列表视图：房间卡数量与房间数一致",
           countChildren(page, QStringLiteral("modCardListItem")) == rooms.size());
    expect("列表视图：有「我的房间」徽记", countChildren(page, QStringLiteral("mlChipMine")) >= 1);
    expect("列表视图：每间房都有邀请码文字",
           countChildren(page, QStringLiteral("mlCardCode")) == rooms.size());
    expect("列表视图：卡片里有模组风格的信息胶囊",
           countChildren(page, QStringLiteral("modChip")) >= rooms.size() * 2);

    const QList<QPushButton *> listJoinButtons =
        page->findChildren<QPushButton *>(QStringLiteral("mlJoinBtn"));
    expect("列表视图：每间房都有一个加入按钮", listJoinButtons.size() == rooms.size());
    int disabled = 0;
    for (QPushButton *button : listJoinButtons)
    {
        if (!button->isEnabled())
            ++disabled;
    }
    expect("已下架的房间加入按钮置灰", disabled == 1);
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_list_light.png"));

    // ---- 2.5 单击卡片：进入详情页（点击标题区域，不点按钮） ----
    if (QWidget *firstCard = page->findChild<QWidget *>(QStringLiteral("modCardListItem")))
    {
        const QPoint pos(180, 20);
        QMouseEvent press(QEvent::MouseButtonPress, pos, firstCard->mapToGlobal(pos),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, pos, firstCard->mapToGlobal(pos),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(firstCard, &press);
        QApplication::sendEvent(firstCard, &release);
        pump();
    }
    expect("单击房间卡进入详情页", page->isDetailVisible());
    page->hideRoomDetail();
    pump();

    // 卡片里的「加入」按钮不能顺带把详情页也打开
    if (listJoinButtons.size() > 1)
    {
        listJoinButtons.at(1)->click();
        pump();
    }
    expect("点卡片里的按钮不会误开详情页", !page->isDetailVisible());

    // ---- 3. 瀑布流视图：卡片与模组瀑布流同款 ----
    page->applyRoomViewMode(static_cast<int>(ContentViewSwitch::Masonry));
    pump();
    expect("瀑布流视图：contentCard 数量与房间数一致",
           countChildren(page, QStringLiteral("contentCard")) == rooms.size());
    expect("瀑布流视图：有封面 banner",
           countChildren(page, QStringLiteral("contentCardBanner")) == rooms.size());
    expect("瀑布流视图：列表卡片已全部移除",
           countChildren(page, QStringLiteral("modCardListItem")) == 0);
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_masonry_light.png"));

    // ---- 4. 暗色主题：同一份内容重新上色 ----
    ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    restyleHost();
    pump();
    page->renderRooms();
    pump();
    expect("暗色主题下样式表仍无残留占位符", !page->styleSheet().contains(QLatin1Char('@')));
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_masonry_dark.png"));

    page->m_viewMode = static_cast<int>(ContentViewSwitch::List);
    page->renderRooms();
    pump();
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_list_dark.png"));
    page->m_viewMode = static_cast<int>(ContentViewSwitch::Masonry);
    page->renderRooms();
    pump();

    // ---- 4.5 房间很多时瀑布流能正常撑高 / 滚动 ----
    {
        QVector<ForumLobbyRoom> many;
        for (int i = 0; i < 30; ++i)
        {
            ForumLobbyRoom room = makeRoom(
                QStringLiteral("测试房间 %1 · 周末开黑").arg(i + 1),
                (i % 3 == 0) ? QStringLiteral("terracotta")
                             : (i % 3 == 1 ? QStringLiteral("hongshi") : QStringLiteral("frp")),
                QStringLiteral("room-%1.example.com:25565").arg(i + 1),
                QStringLiteral("1.20.1"), QStringLiteral("Forge"),
                QStringLiteral("房主%1").arg(i + 1), QStringLiteral("waiting"), i);
            room.description = QStringLiteral("这是第 %1 个房间的简介，用来把瀑布流卡片撑高。").arg(i + 1);
            many << room;
        }
        page->m_rooms = many;
        page->m_total = many.size();
        page->m_viewMode = static_cast<int>(ContentViewSwitch::Masonry);
        page->renderRooms();
        pump();
        expect("30 个房间的瀑布流卡片都建出来了",
               countChildren(page, QStringLiteral("contentCard")) == many.size());
        const int contentH = page->m_container->height();
        const int viewH = page->m_scroll->viewport()->height();
        fprintf(stderr, "[probe] many rooms: content=%d viewport=%d\n", contentH, viewH);
        expect("房间多时瀑布流内容高于视口（可滚动）", contentH > viewH);
        savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_masonry_many_dark.png"));
    }

    // ---- 5. 空状态（暗色） ----
    page->m_rooms.clear();
    page->m_total = 0;
    page->m_errorText.clear();
    page->renderRooms();
    pump();
    expect("空状态出现占位卡", countChildren(page, QStringLiteral("mlStateCard")) == 1);
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_empty_dark.png"));

    // ---- 6. 连不上（亮色） ----
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    restyleHost();
    page->m_errorText = QStringLiteral("连接超时");
    page->renderRooms();
    pump();
    expect("出错状态也有占位卡", countChildren(page, QStringLiteral("mlStateCard")) == 1);
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_error_light.png"));

    // ---- 7. 中等宽度（平板 / 半窗）不挤出按钮 ----
    page->m_errorText.clear();
    page->m_rooms = rooms;
    page->m_total = rooms.size();
    page->m_viewMode = static_cast<int>(ContentViewSwitch::List);
    page->renderRooms();
    host->resize(660, 760);
    pump();
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_medium_list_light.png"));

    // ---- 7.5 极窄窗口（手机比例）下不溢出 ----
    page->m_errorText.clear();
    page->m_rooms = rooms;
    page->m_viewMode = static_cast<int>(ContentViewSwitch::List);
    page->renderRooms();
    host->resize(420, 760);
    pump();
    fprintf(stderr, "[probe] narrow: host=%dx%d page=%dx%d compact=%d dir=%d\n",
            host->width(), host->height(), page->width(), page->height(),
            int(page->m_compactLayout),
            page->m_heroLayout ? int(page->m_heroLayout->direction()) : -1);
    fprintf(stderr, "[probe] page minHint=%dx%d\n", page->minimumSizeHint().width(),
            page->minimumSizeHint().height());
    for (QWidget *w : page->findChildren<QWidget *>()) {
        if (w->parentWidget() != page) continue;
        fprintf(stderr, "[probe]   child %s min=%d\n", qPrintable(w->objectName()),
                w->minimumSizeHint().width());
    }
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_narrow_light.png"));

    // ---- 8. 房间详情页：宽屏 / 窄屏 / 暗色 ----
    host->resize(1200, 820);
    page->m_rooms = rooms;
    page->m_total = rooms.size();
    page->renderRooms();
    pump();

    page->openRoomDetail(terracotta);
    pump();
    expect("详情页已打开", page->isDetailVisible());
    expect("详情页标题是房间名",
           page->m_detailPage->room().title == terracotta.title);
    expect("详情页有 Markdown 介绍浏览器",
           countChildren(page->m_detailPage, QStringLiteral("mlDetailIntroBrowser")) == 1);
    expect("详情页有房间介绍 / 加入 / 信息三张卡以上",
           countChildren(page->m_detailPage, QStringLiteral("mlDetailCard")) >= 5);
    expect("详情页有大号邀请码",
           countChildren(page->m_detailPage, QStringLiteral("mlDetailCode")) == 1);
    expect("详情页有一键加入按钮",
           countChildren(page->m_detailPage, QStringLiteral("mlDetailPrimary")) == 1);
    expect("详情页有分享按钮",
           countChildren(page->m_detailPage, QStringLiteral("mlDetailShare")) == 1);
    {
        const QString shareText = page->m_detailPage->shareText();
        expect("分享文案带房间名和邀请码",
               shareText.contains(terracotta.title) && shareText.contains(terracotta.inviteCode));
        expect("分享私信正文不重复房间名标题",
               !page->m_detailPage->shareText(false).startsWith(QLatin1String("【方块盒子")));
    }
    expect("详情页列出了房间模组胶囊",
           countChildren(page->m_detailPage, QStringLiteral("mlDetailModChip")) ==
               terracotta.mods.size());
    expect("详情页样式表里没有残留的 @TOKEN@",
           !page->m_detailPage->styleSheet().contains(QLatin1Char('@')));
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_detail_light.png"));

    // 滚到详情页底部，确认前置整合包 / 房间信息卡也排好了
    if (auto *bar = page->m_detailPage->m_scroll->verticalScrollBar())
    {
        bar->setValue(bar->maximum());
        pump();
        savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_detail_bottom_light.png"));
        bar->setValue(0);
        pump();
    }

    // 返回按钮把详情关掉、回到列表
    expect("closeDetailIfVisible 关掉详情", page->closeDetailIfVisible());
    pump();
    expect("详情关闭后回到大厅列表", !page->isDetailVisible());

    // 已下架房间的详情页：加入按钮置灰
    page->openRoomDetail(empty);
    pump();
    QPushButton *detailJoin =
        page->m_detailPage->findChild<QPushButton *>(QStringLiteral("mlDetailPrimary"));
    expect("已下架房间的详情页加入按钮置灰", detailJoin && !detailJoin->isEnabled());
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_detail_offline_light.png"));
    page->hideRoomDetail();
    pump();

    // 窄屏详情页
    host->resize(480, 820);
    page->openRoomDetail(redstone);
    pump();
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_detail_narrow_light.png"));
    fprintf(stderr, "[probe] detail narrow: page=%dx%d compact=%d\n",
            page->m_detailPage->width(), page->m_detailPage->height(),
            int(page->m_detailPage->m_compact));
    page->hideRoomDetail();

    // 暗色详情页
    ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    restyleHost();
    host->resize(1200, 820);
    page->openRoomDetail(terracotta);
    pump();
    expect("暗色详情页样式表仍无残留占位符",
           !page->m_detailPage->styleSheet().contains(QLatin1Char('@')));
    savePng(host, QStringLiteral(".tmp_lobby_probe/probe_out/lobby_detail_dark.png"));

    fprintf(stderr, "[probe] %s (failures=%d)\n", g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL",
            g_failures);
    return g_failures == 0 ? 0 : 1;
}
