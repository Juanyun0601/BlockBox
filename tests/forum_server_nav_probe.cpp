/**
 * @file   forum_server_nav_probe.cpp
 * @brief  服务器导航板块「来源」设置探针：真实页面 + 真实主题 QSS，截图核对排版
 *
 * 不启动 MainWindow、不联网：直接把论坛帖和 Modrinth 服务器注入列表，
 * 核对三件事：
 *   1. 地址抠取（IP / 域名 / 带端口 / 中文标签 / 纯链接不算）；
 *   2. 来源下拉只有全部源 / 方块盒子源 / Modrinth 源三项，切换后列表跟着变；
 *   3. 合并列表的小标题、地址行、复制按钮、分页在亮 / 暗主题下都排得下。
 *
 * 输出：.tmp_forum_nav_probe/probe_out/forum_nav_*.png
 * 退出码 0 = 全部断言通过。
 */
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "components/ForumKit.h"
#include "utils/ForumContentService.h"
#include "utils/StyleKit.h"
#include "utils/ThemeManager.h"
#include "utils/mod/ModData.h"

// 探针需要注入列表数据，这里临时放开访问权限（只影响本翻译单元）。
// 注意：先把页面用到的其它头文件全部包含完，再开宏，避免宏污染 Qt / 工程里的 private 段。
#define private public
#include "pages/ForumThreadListPage.h"
#undef private

namespace
{
int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-56s %s\n", label, condition ? "OK" : "FAIL");
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

void pump()
{
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();
    // 列表重建走 deleteLater：探针要显式把延迟删除跑掉，否则旧行还在树里
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/** 页面上某个 objectName 的标签文字合集 */
QStringList labelTexts(QWidget *root, const QString &objectName)
{
    QStringList texts;
    const auto labels = root->findChildren<QLabel *>(objectName);
    for (const QLabel *label : labels)
        texts << label->text();
    return texts;
}

int buttonCount(QWidget *root, const QString &text)
{
    int count = 0;
    const auto buttons = root->findChildren<QPushButton *>();
    for (const QPushButton *button : buttons)
    {
        if (button->text() == text)
            ++count;
    }
    return count;
}

ForumThreadInfo makeNavThread(int id, const QString &title, const QString &address,
                              const QString &author, int comments, int views, qint64 minutesAgo)
{
    ForumThreadInfo info;
    info.id = id;
    info.title = title;
    info.boardSlug = QStringLiteral("server-nav");
    info.boardName = QStringLiteral("服务器导航");
    info.authorName = author;
    info.comments = comments;
    info.views = views;
    info.lastPostAt = QDateTime::currentSecsSinceEpoch() - minutesAgo * 60;
    info.navigation.kind = QStringLiteral("server");
    info.navigation.url = address;
    return info;
}

ModInfo makeServer(const QString &id, const QString &name, const QString &description,
                   const QStringList &versions, qint64 downloads)
{
    ModInfo info;
    info.id = id;
    info.name = name;
    info.description = description;
    info.gameVersions = versions;
    info.downloadCount = downloads;
    info.source = QStringLiteral("modrinth");
    info.contentKind = QStringLiteral("minecraft_java_server");
    info.pageUrl = QStringLiteral("https://modrinth.com/server/%1").arg(id);
    return info;
}
} // namespace

int main(int argc, char *argv[])
{
    setvbuf(stderr, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBoxServerNavProbe"));
    app.setOrganizationName(QStringLiteral("BlockBoxProbe"));
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);

    QDir().mkpath(QStringLiteral(".tmp_forum_nav_probe/probe_out"));

    // ── 1. 地址抠取（Modrinth 项目介绍里作者写法五花八门） ──
    using Page = ForumThreadListPage;
    expect("IP 标签 + 域名", Page::extractServerAddress(
                                QStringLiteral("One of the largest servers. Java 1.21.1, IP modrinth.mc-complex.com."))
                            == QStringLiteral("modrinth.mc-complex.com"));
    expect("Server IP + 端口", Page::extractServerAddress(
                                  QStringLiteral("Server IP: play.example.net:25565")) == QStringLiteral("play.example.net:25565"));
    expect("中文「服务器地址」", Page::extractServerAddress(
                                    QStringLiteral("服务器地址：mc.test.cn:25565 欢迎来玩")) == QStringLiteral("mc.test.cn:25565"));
    expect("IPv4 带端口", Page::extractServerAddress(
                              QStringLiteral("IP 42.192.1.9:25566 三线直连")) == QStringLiteral("42.192.1.9:25566"));
    expect("HTML 正文也能抠", Page::extractServerAddress(
                                  QStringLiteral("<p>Server IP: <strong>mc.hypixel.net</strong></p>"))
                              == QStringLiteral("mc.hypixel.net"));
    expect("只有外链不算地址", Page::extractServerAddress(
                                  QStringLiteral("加入我们 https://discord.gg/abcdef 一起玩")).isEmpty());
    expect("modrinth 链接不算地址", Page::extractServerAddress(
                                      QStringLiteral("见 https://modrinth.com/server/aero-server")).isEmpty());
    expect("play 子域兜底", Page::extractServerAddress(
                                QStringLiteral("Join us at play.some-server.com now")) == QStringLiteral("play.some-server.com"));

    // ── 2. 页面：服务器导航板块 ──
    auto *host = new QWidget;
    host->setObjectName(QStringLiteral("probeHost"));
    auto restyleHost = [host]() {
        host->setStyleSheet(StyleKit::resolve(
            QStringLiteral("QWidget#probeHost { background: @BG_BASE@; }")));
    };
    restyleHost();
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    auto *page = new ForumThreadListPage(host);
    hostLayout->addWidget(page);

    page->m_mode = ForumThreadListPage::BoardMode;
    page->m_boardSlug = QStringLiteral("server-nav");
    page->m_boardName = QStringLiteral("服务器导航");
    page->m_page = 1;
    page->m_serverSource = int(ForumThreadListPage::SourceAll);
    page->renderHeader();

    expect("来源下拉存在", page->m_sourceCombo != nullptr);
    expect("来源下拉正好三项", page->m_sourceCombo && page->m_sourceCombo->count() == 3);
    expect("第一项是全部源", page->m_sourceCombo
                                 && page->m_sourceCombo->itemText(0) == QStringLiteral("全部源"));
    expect("第二项是方块盒子源", page->m_sourceCombo
                                     && page->m_sourceCombo->itemText(1) == QStringLiteral("方块盒子源"));
    expect("第三项是 Modrinth 源", page->m_sourceCombo
                                       && page->m_sourceCombo->itemText(2) == QStringLiteral("Modrinth 源"));

    // 注入两个来源的数据（不联网）
    QVector<ForumThreadInfo> threads;
    threads << makeNavThread(101, QStringLiteral("周末生存服 · 一起盖房子"),
                             QStringLiteral("mc.example.cn:25565"), QStringLiteral("小明"), 3, 128, 40);
    threads << makeNavThread(102, QStringLiteral("创造建筑服 · 想要一起造城"),
                             QStringLiteral("build.example.cn"), QStringLiteral("阿星"), 8, 512, 600);
    page->m_forumThreads = threads;
    page->m_forumPages = 1;

    QVector<ModInfo> servers;
    servers << makeServer(QStringLiteral("aero-server"), QStringLiteral("Aero SMP"),
                          QStringLiteral("A no-nonsense server with just Create and Create Aeronautics."),
                          {QStringLiteral("1.21.1")}, 40714);
    servers << makeServer(QStringLiteral("complex-cobblemon"), QStringLiteral("Complex Cobblemon"),
                          QStringLiteral("One of the largest Cobblemon servers. Java 1.21.1, IP modrinth.mc-complex.com."),
                          {QStringLiteral("1.21.1"), QStringLiteral("1.21.4")}, 128000);
    page->m_modrinthServers = servers;
    page->m_modrinthAddresses.insert(QStringLiteral("aero-server"),
                                     QStringLiteral("play.aero.example.com:25565"));
    page->m_modrinthTotal = 2534;
    page->m_modrinthPages = 127;

    page->m_forumDone = true;
    page->m_modrinthDone = true;
    page->m_forumError.clear();
    page->m_modrinthError.clear();

    host->resize(1180, 940);
    host->show();
    pump();

    // ── 3. 全部源：两部分都在，小标题 + 地址行 + 复制按钮 ──
    page->m_serverSource = int(ForumThreadListPage::SourceAll);
    page->finishLoad();
    pump();

    const QStringList sections = labelTexts(page, QStringLiteral("forumSectionTitle"));
    expect("全部源有两个来源小标题", sections.size() == 2);
    expect("小标题写着方块盒子源", sections.contains(QStringLiteral("方块盒子源")));
    expect("小标题写着 Modrinth 源", sections.contains(QStringLiteral("Modrinth 源")));
    expect("论坛服务器地址显示在行里",
           labelTexts(page, QStringLiteral("forumMuted")).contains(QStringLiteral("mc.example.cn:25565")));
    expect("两个来源各有复制地址按钮（论坛 2 + Modrinth 2）",
           buttonCount(page, QStringLiteral("复制地址")) == 4);
    expect("Modrinth 行有「在 Modrinth 查看」", buttonCount(page, QStringLiteral("在 Modrinth 查看")) == 2);
    expect("分页取两个来源的较大页数", page->m_pager->totalPages() == 127);
    expect("状态行写着两个来源的数量",
           page->m_statusLabel && page->m_statusLabel->text().contains(QStringLiteral("Modrinth 源 2 台")));
    expect("地址来自批量正文（play.aero...）",
           labelTexts(page, QStringLiteral("forumMuted")).contains(QStringLiteral("play.aero.example.com:25565")));
    savePng(host, QStringLiteral(".tmp_forum_nav_probe/probe_out/forum_nav_all_light.png"));

    // 暗色主题：文字 / 边框不能糊
    ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    restyleHost();
    page->finishLoad();
    pump();
    savePng(host, QStringLiteral(".tmp_forum_nav_probe/probe_out/forum_nav_all_dark.png"));
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    restyleHost();

    // ── 4. 只看 Modrinth 源：论坛帖不该掺进来 ──
    page->m_serverSource = int(ForumThreadListPage::SourceModrinth);
    page->finishLoad();
    pump();
    expect("Modrinth 源不显示来源小标题",
           labelTexts(page, QStringLiteral("forumSectionTitle")).isEmpty());
    expect("Modrinth 源不显示论坛帖子",
           !labelTexts(page, QStringLiteral("forumRowTitle")).contains(QStringLiteral("周末生存服 · 一起盖房子")));
    expect("Modrinth 源仍有两台服务器",
           labelTexts(page, QStringLiteral("forumRowTitle")).contains(QStringLiteral("Aero SMP")));
    expect("Modrinth 源分页是 Modrinth 的页数", page->m_pager->totalPages() == 127);
    savePng(host, QStringLiteral(".tmp_forum_nav_probe/probe_out/forum_nav_modrinth.png"));

    // ── 5. 只看方块盒子源：Modrinth 卡片不该出现 ──
    page->m_serverSource = int(ForumThreadListPage::SourceBlockBox);
    page->finishLoad();
    pump();
    expect("方块盒子源不显示 Modrinth 卡片",
           !labelTexts(page, QStringLiteral("forumRowTitle")).contains(QStringLiteral("Aero SMP")));
    expect("方块盒子源有论坛帖", buttonCount(page, QStringLiteral("复制地址")) == 2);
    expect("方块盒子源没有再查看 Modrinth 的按钮",
           buttonCount(page, QStringLiteral("在 Modrinth 查看")) == 0);
    savePng(host, QStringLiteral(".tmp_forum_nav_probe/probe_out/forum_nav_forum.png"));

    // ── 6. 全空：给出「还没有人分享」的提示而不是白屏 ──
    page->m_serverSource = int(ForumThreadListPage::SourceAll);
    page->m_forumThreads.clear();
    page->m_modrinthServers.clear();
    page->finishLoad();
    pump();
    bool hasEmptyHint = false;
    for (const QString &text : labelTexts(page, QStringLiteral("forumMuted")))
    {
        if (text.contains(QStringLiteral("还没有人分享服务器")))
            hasEmptyHint = true;
    }
    expect("全空时有占位提示", hasEmptyHint);
    savePng(host, QStringLiteral(".tmp_forum_nav_probe/probe_out/forum_nav_empty.png"));

    // ── 7. 窄窗口：来源下拉 + 排序按钮别把表头挤爆 ──
    page->m_forumThreads = threads;
    page->m_modrinthServers = servers;
    page->finishLoad();
    pump();
    host->resize(760, 900);
    pump();
    savePng(host, QStringLiteral(".tmp_forum_nav_probe/probe_out/forum_nav_narrow.png"));
    host->resize(1180, 940);
    pump();

    // ── 8. 真实联网（可选）：PROBE_NETWORK=1 时确认 Modrinth 整条链路能用 ──
    if (qEnvironmentVariableIsSet("PROBE_NETWORK"))
    {
        page->m_serverSource = int(ForumThreadListPage::SourceModrinth);
        page->loadPage(1);
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 30000 && !page->m_modrinthDone)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            QThread::msleep(20);
        }
        pump();

        expect("联网：Modrinth 源拿到服务器", !page->m_modrinthServers.isEmpty());
        expect("联网：项目页地址挂在 /server/",
               !page->m_modrinthServers.isEmpty()
                   && page->m_modrinthServers.first().pageUrl.contains(QStringLiteral("/server/")));
        expect("联网：卡片图标地址非空",
               !page->m_modrinthServers.isEmpty()
               && !page->m_modrinthServers.first().iconUrl.isEmpty());
        int withAddress = 0;
        for (const ModInfo &server : page->m_modrinthServers)
        {
            const QString address = page->m_modrinthAddresses.value(server.id);
            if (!address.isEmpty())
                ++withAddress;
            if (qEnvironmentVariableIsSet("PROBE_NETWORK_VERBOSE"))
                fprintf(stderr, "[probe]   %-28s -> [%s]\n", qPrintable(server.name),
                        qPrintable(address));
        }
        fprintf(stderr, "[probe] 联网：本页 %d 台，其中 %d 台从介绍里认出了地址\n",
                int(page->m_modrinthServers.size()), withAddress);
        expect("联网：至少一台能认出服务器地址", withAddress > 0);
        expect("联网：状态行写明了总数",
               page->m_statusLabel && page->m_statusLabel->text().contains(QStringLiteral("Modrinth")));

        // 图标是异步下载的：等第一张图落地再截图，否则看着像空占位
        QElapsedTimer iconTimer;
        iconTimer.start();
        while (iconTimer.elapsed() < 10000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            QThread::msleep(30);
            const auto iconLabels = host->findChildren<QLabel *>(QStringLiteral("forumAvatar"));
            bool anyPixmap = false;
            for (const QLabel *label : iconLabels)
            {
                const QPixmap pixmap = label->pixmap();
                if (!pixmap.isNull())
                {
                    anyPixmap = true;
                    break;
                }
            }
            if (anyPixmap)
                break;
        }
        savePng(host, QStringLiteral(".tmp_forum_nav_probe/probe_out/forum_nav_online.png"));
    }

    // ── 9. 普通搜索模式的列表：不受来源设置影响，行为保持原样 ──
    page->m_mode = ForumThreadListPage::SearchMode;
    page->m_boardSlug.clear();
    page->m_query = QStringLiteral("生存");
    page->renderHeader();
    page->m_forumThreads = threads;
    page->m_modrinthServers.clear();
    page->m_forumDone = true;
    page->m_modrinthDone = true;
    page->m_totalPages = 3;
    page->m_page = 1;
    page->finishLoad();
    pump();
    expect("搜索模式没有来源下拉", page->m_sourceCombo == nullptr);
    expect("搜索模式照常显示帖子", buttonCount(page, QStringLiteral("复制地址")) == 2);
    expect("搜索模式没有来源小标题",
           labelTexts(page, QStringLiteral("forumSectionTitle")).isEmpty());
    expect("搜索模式分页用论坛的页数", page->m_pager->totalPages() == 3);

    host->close();
    delete host;

    if (g_failures > 0)
    {
        fprintf(stderr, "[probe] %d 项不通过\n", g_failures);
        return 1;
    }
    fprintf(stderr, "[probe] 全部通过\n");
    return 0;
}
