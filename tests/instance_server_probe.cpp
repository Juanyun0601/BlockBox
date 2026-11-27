/**
 * @file   instance_server_probe.cpp
 * @brief  实例助手「开服」页探针：一键本地开服的准备、启动、停止全流程
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 不真跑 Minecraft 服务端（要下几十 MB + 生成世界），用 tests/fake_server_java.cpp
 * 编出来的假 java 顶替：它会按服务端的节奏打印启动日志与 Done(...)，读到 stop 正常退出。
 * 这样能覆盖：开服配置生成、mods 目录共享、进程启停、控制台输出、"就绪"识别、
 * 地址复制、写进 servers.dat、常见故障文案。
 *
 * 用法：
 *   pwsh -NoProfile -File .syntax_check/build_probe_from_release.ps1 -Name instance_server_probe
 */

#include <QtCore>
#include <QtGui>
#include <QtNetwork>
#include <QtWidgets>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <cstdio>

#include "utils/SettingsManager.h"
#include "utils/ServersDatIO.h"
#include "utils/ThemeManager.h"
#include "utils/server/ServerAdminStore.h"
#include "components/CustomCheckBox.h"
#include "components/ForumTagEditor.h"

// 探针要读页面的私有成员做断言（仓库里其它探针同一做法）
#define private public
#include "pages/ForumComposerPage.h"
#include "pages/InstanceServerPage.h"
#include "pages/MultiplayerPage.h"
#include "components/ServerDownloadDialog.h"
#undef private

namespace {

int g_pass = 0;
int g_fail = 0;
QStringList g_report;

void logLine(const QString &line)
{
    g_report << line;
    std::printf("%s\n", qUtf8Printable(line));
    std::fflush(stdout);
}

void check(const QString &name, bool ok, const QString &detail = QString())
{
    if (ok)
        ++g_pass;
    else
        ++g_fail;
    logLine(QStringLiteral("[%1] %2%3")
                .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"), name,
                     detail.isEmpty() ? QString() : QStringLiteral("  -> ") + detail));
}

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/// 等某个条件成立（用于等进程就绪 / 停止）
bool waitUntil(const std::function<bool()> &predicate, int timeoutMs)
{
    const int step = 60;
    for (int waited = 0; waited < timeoutMs; waited += step) {
        if (predicate())
            return true;
        waitMs(step);
    }
    return predicate();
}

QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(content);
}

void shoot(QWidget *host, const QString &name)
{
    QApplication::processEvents();
    const QString path = QStringLiteral(".tmp_server_probe/out/%1.png").arg(name);
    QDir().mkpath(QStringLiteral(".tmp_server_probe/out"));
    const bool ok = host->grab().save(path);
    check(QStringLiteral("截图 %1").arg(name), ok, path);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QApplication::setApplicationName(QStringLiteral("Settings"));

    const QString root = QDir::currentPath();
    const QString tmpRoot = root + QStringLiteral("/.tmp_server_probe");
    const QString instPath = tmpRoot + QStringLiteral("/instance");

    // ── 假实例：mods、一个假服务端 jar、一个存档 ──
    QDir(instPath).removeRecursively();
    QDir().mkpath(instPath + QStringLiteral("/mods"));
    writeFile(instPath + QStringLiteral("/mods/demo-mod.jar"), QByteArray(2048, 'x'));
    QByteArray fakeJar("PK\x03\x04", 4);
    fakeJar.append(QByteArray(8000, '\0'));
    writeFile(instPath + QStringLiteral("/server/fabric-server.jar"), fakeJar);
    // 玩家已经改过的配置项（验证只更新必要项、不覆盖玩家设置）
    writeFile(instPath + QStringLiteral("/server/server.properties"),
              QByteArray("difficulty=hard\nview-distance=4\n"));
    // 一个存档（给「用存档开服」用）：世界目录里放几个文件 + 客户端锁文件
    const QString saveDir = instPath + QStringLiteral("/saves/我的世界");
    QDir().mkpath(saveDir + QStringLiteral("/region"));
    writeFile(saveDir + QStringLiteral("/level.dat"), QByteArray(4096, 'L'));
    writeFile(saveDir + QStringLiteral("/region/r.0.0.mca"), QByteArray(20000, 'R'));
    writeFile(saveDir + QStringLiteral("/session.lock"), QByteArray("lock"));
    writeFile(saveDir + QStringLiteral("/playerdata/player.dat"), QByteArray(128, 'P'));

    const QString fakeJava = tmpRoot + QStringLiteral("/fake_server_java.exe");
    check(QStringLiteral("假 java 存在"), QFileInfo::exists(fakeJava), fakeJava);

    // ── 记住并临时改设置，最后还原 ──
    auto *settings = SettingsManager::instance();
    const QVariant oldPort = settings->getProperty(QStringLiteral("local_server_port"));
    const QVariant oldPlayers = settings->getProperty(QStringLiteral("local_server_players"));
    const QVariant oldMemory = settings->getProperty(QStringLiteral("local_server_memory"));
    const QVariant oldOnline = settings->getProperty(QStringLiteral("local_server_online_mode"));
    const QVariant oldCore = settings->getProperty(QStringLiteral("local_server_core"));
    const QVariant oldMotd = settings->getProperty(QStringLiteral("local_server_motd"));
    const QVariant oldWorld = settings->getProperty(QStringLiteral("local_server_world"));

    ThemeManager::setTheme(&app, ThemeManager::DarkTheme);

    QWidget host;
    host.resize(450, 820);
    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *page = new InstanceServerPage(&host);
    layout->addWidget(page);
    host.show();
    waitMs(300);

    // ── 1. 实例上下文 ──
    page->setInstanceContext(instPath, QStringLiteral("1.20.4"), QStringLiteral("Fabric"));
    waitMs(200);
    check(QStringLiteral("实例说明带版本与加载器"),
          page->m_instanceLabel->text().contains(QStringLiteral("1.20.4"))
              && page->m_instanceLabel->text().contains(QStringLiteral("Fabric")),
          page->m_instanceLabel->text());
    check(QStringLiteral("默认核心按加载器选 Fabric"),
          page->m_coreCombo->currentData().toInt()
              == static_cast<int>(LocalServerManager::Core::Fabric),
          page->m_coreCombo->currentText());
    check(QStringLiteral("初始状态是未开服"), page->m_statusText->text() == QStringLiteral("未开服"),
          page->m_statusText->text());
    check(QStringLiteral("开服按钮可用且是「一键开服」"),
          page->m_primaryBtn->isEnabled() && page->m_primaryBtn->text() == QStringLiteral("一键开服"),
          page->m_primaryBtn->text());
    check(QStringLiteral("地址行显示本机端口"),
          page->m_addressLabel->text() == QStringLiteral("127.0.0.1:25565"),
          page->m_addressLabel->text());
    shoot(&host, QStringLiteral("01_idle_dark"));

    // ── 2. 设置联动与持久化 ──
    page->m_portSpin->setValue(25599);
    page->m_playersSpin->setValue(7);
    page->m_memoryCombo->setCurrentIndex(page->m_memoryCombo->findData(1024));
    page->onSettingsChanged();
    waitMs(120);
    check(QStringLiteral("端口改动同步到管理器"), page->m_manager->config().port == 25599,
          QString::number(page->m_manager->config().port));
    check(QStringLiteral("地址行跟着端口变"),
          page->m_addressLabel->text() == QStringLiteral("127.0.0.1:25599"),
          page->m_addressLabel->text());
    check(QStringLiteral("端口写入设置"),
          settings->getProperty(QStringLiteral("local_server_port")).toInt() == 25599,
          settings->getProperty(QStringLiteral("local_server_port")).toString());

    // 未开服时不能发指令：不该往控制台里塞东西
    page->m_console->clear();
    page->m_commandEdit->setText(QStringLiteral("say 还没开服"));
    page->onCommandEntered();
    waitMs(120);
    check(QStringLiteral("未开服时发指令被挡住"),
          !page->m_console->toPlainText().contains(QStringLiteral("> say")),
          page->m_console->toPlainText().left(80));

    // ── 3. 开服配置生成 ──
    page->m_motdEdit->setText(QStringLiteral("测试服 ● 朋友来玩"));
    waitMs(80);
    check(QStringLiteral("服务器名称写入设置"),
          settings->getProperty(QStringLiteral("local_server_motd")).toString()
              == QStringLiteral("测试服 ● 朋友来玩"),
          settings->getProperty(QStringLiteral("local_server_motd")).toString());

    page->m_manager->setConfig(page->configFromUi());
    QString cfgError;
    const bool cfgOk = page->m_manager->writeConfigs(&cfgError);
    check(QStringLiteral("写开服配置成功"), cfgOk, cfgError);

    const QString eula = readAll(instPath + QStringLiteral("/server/eula.txt"));
    check(QStringLiteral("eula.txt 已同意"), eula.contains(QStringLiteral("eula=true")));

    const QString props = readAll(instPath + QStringLiteral("/server/server.properties"));
    check(QStringLiteral("server.properties 写入端口"),
          props.contains(QStringLiteral("server-port=25599")));
    check(QStringLiteral("server.properties 写入人数"),
          props.contains(QStringLiteral("max-players=7")));
    check(QStringLiteral("离线模式关正版验证"),
          props.contains(QStringLiteral("online-mode=false"))
              && props.contains(QStringLiteral("enforce-secure-profile=false")));
    check(QStringLiteral("中文 MOTD 转义成 \\u"),
          props.contains(QStringLiteral("\\u65b9")) || props.contains(QStringLiteral("\\u6d4b")),
          props.section(QStringLiteral("motd="), 1).section(QLatin1Char('\n'), 0, 0));
    check(QStringLiteral("玩家改过的配置项保留"),
          props.contains(QStringLiteral("difficulty=hard"))
              && props.contains(QStringLiteral("view-distance=4")));

    // ── 4. 管理卡：白名单 / 管理员 / 基础设置 ──
    check(QStringLiteral("管理卡有三个页签"),
          page->m_adminTabBtns.size() == 3 && page->m_whitelistPage && page->m_opPage
              && page->m_rulesPage,
          QString::number(page->m_adminTabBtns.size()));
    check(QStringLiteral("默认停在白名单页"),
          page->m_whitelistPage->isVisible() && !page->m_opPage->isVisible());
    check(QStringLiteral("白名单开关默认关闭"), !page->m_whitelistToggle->isChecked());
    check(QStringLiteral("基础设置读到玩家改过的难度"),
          page->m_difficultyCombo->currentData().toString() == QStringLiteral("hard"),
          page->m_difficultyCombo->currentData().toString());
    check(QStringLiteral("基础设置读到玩家改过的视距"), page->m_viewDistanceSpin->value() == 4,
          QString::number(page->m_viewDistanceSpin->value()));

    // 停服加白名单：按离线 UUID 写进 whitelist.json
    page->m_whitelistInput->setText(QStringLiteral("Steve"));
    page->onAddWhitelistClicked();
    waitMs(180);
    const QString whitelistJson = readAll(instPath + QStringLiteral("/server/whitelist.json"));
    check(QStringLiteral("白名单写入玩家"), whitelistJson.contains(QStringLiteral("\"Steve\"")),
          whitelistJson.left(100));
    check(QStringLiteral("白名单 UUID 用离线算法"),
          whitelistJson.contains(ServerAdminStore::offlineUuid(QStringLiteral("Steve"))),
          ServerAdminStore::offlineUuid(QStringLiteral("Steve")));
    check(QStringLiteral("白名单列表显示这个玩家"),
          page->m_whitelistEntries.size() == 1
              && page->m_whitelistEntries.first().name == QStringLiteral("Steve"),
          QString::number(page->m_whitelistEntries.size()));

    // 白名单开关写进 server.properties，别的项不受影响
    page->m_whitelistToggle->setChecked(true);
    waitMs(150);
    QString propsNow = readAll(instPath + QStringLiteral("/server/server.properties"));
    check(QStringLiteral("开启白名单写进配置"), propsNow.contains(QStringLiteral("white-list=true")),
          propsNow.left(140));
    check(QStringLiteral("改白名单不冲掉玩家的其它配置"),
          propsNow.contains(QStringLiteral("difficulty=hard")));

    // 管理员：2 级
    page->m_opInput->setText(QStringLiteral("Alex"));
    page->m_opLevelCombo->setCurrentIndex(page->m_opLevelCombo->findData(2));
    page->onAddOpClicked();
    waitMs(180);
    const QString opsJson = readAll(instPath + QStringLiteral("/server/ops.json"));
    check(QStringLiteral("管理员写进 ops.json"),
          opsJson.contains(QStringLiteral("\"Alex\""))
              && opsJson.contains(QStringLiteral("\"level\": 2")),
          opsJson.left(140));
    check(QStringLiteral("管理员列表显示等级"),
          page->m_opEntries.size() == 1 && page->m_opEntries.first().level == 2,
          QString::number(page->m_opEntries.size()));

    // 基础设置：PVP / 视距
    page->m_pvpCheck->setChecked(false);
    page->m_viewDistanceSpin->setValue(6);
    waitMs(180);
    propsNow = readAll(instPath + QStringLiteral("/server/server.properties"));
    check(QStringLiteral("PVP 开关写进配置"), propsNow.contains(QStringLiteral("pvp=false")));
    check(QStringLiteral("视距写进配置"), propsNow.contains(QStringLiteral("view-distance=6")));

    // 页签切换与截图（先把页面滚到管理卡，截图才看得到）
    if (auto *outerScroll = page->findChild<QScrollArea *>(QStringLiteral("serverScroll"))) {
        if (QWidget *adminCard = page->m_whitelistPage->parentWidget()->parentWidget()) {
            const int top = adminCard->mapTo(outerScroll->widget(), QPoint(0, 0)).y();
            outerScroll->verticalScrollBar()->setValue(qMax(0, top - 8));
            waitMs(150);
        }
    }
    page->m_adminTabBtns.at(1)->click();
    waitMs(120);
    check(QStringLiteral("点「管理员」切到管理员页"),
          page->m_opPage->isVisible() && !page->m_whitelistPage->isVisible());
    shoot(&host, QStringLiteral("03_admin_op"));
    page->m_adminTabBtns.at(2)->click();
    waitMs(120);
    shoot(&host, QStringLiteral("04_admin_rules"));
    page->m_adminTabBtns.at(0)->click();
    waitMs(120);
    shoot(&host, QStringLiteral("05_admin_whitelist"));

    // 移出白名单
    if (!page->m_whitelistEntries.isEmpty())
        page->removeWhitelistEntry(page->m_whitelistEntries.first());
    waitMs(180);
    check(QStringLiteral("移出白名单后文件里没有这个人"),
          !readAll(instPath + QStringLiteral("/server/whitelist.json"))
               .contains(QStringLiteral("\"Steve\"")));

    // ── 4. Fabric 的 mods 目录共享 ──
    page->m_manager->prepareModsDir();
    waitMs(150);
    check(QStringLiteral("服务端 mods 目录已共享"),
          QFileInfo(instPath + QStringLiteral("/server/mods")).isDir()
              && QFileInfo::exists(instPath + QStringLiteral("/server/mods/demo-mod.jar")));

    // ── 5. 进程生命周期：启动 → 就绪 → 停止 ──
    page->m_console->clear();
    page->m_manager->m_javaPath = fakeJava;
    page->m_manager->setState(LocalServerManager::State::Starting);
    page->m_manager->launchProcess(fakeJava);

    const bool ready = waitUntil(
        [&]() { return page->m_manager->state() == LocalServerManager::State::Running; }, 15000);
    check(QStringLiteral("服务端就绪（识别到 Done）"), ready, page->m_statusText->text());
    waitMs(200);
    check(QStringLiteral("状态文字变成运行中"),
          page->m_statusText->text() == QStringLiteral("服务器运行中"), page->m_statusText->text());
    check(QStringLiteral("控制台有服务端日志"),
          page->m_console->toPlainText().contains(QStringLiteral("Done (1.234s)")),
          page->m_console->toPlainText().left(120));
    check(QStringLiteral("按钮变成停止服务器"),
          page->m_primaryBtn->text() == QStringLiteral("停止服务器")
              && page->m_primaryBtn->property("danger").toBool(),
          page->m_primaryBtn->text());
    check(QStringLiteral("运行中禁止改设置"), !page->m_portSpin->isEnabled());
    check(QStringLiteral("运行中可以复制地址"), page->m_copyBtn->isEnabled(),
          page->m_addressLabel->text());
    check(QStringLiteral("运行中出现「进游戏 / 远程加入」按钮"),
          page->m_runningActions->isVisible() && page->m_joinGameBtn->isVisible());
    check(QStringLiteral("运行中指令框可用"), page->m_commandEdit->isEnabled());
    shoot(&host, QStringLiteral("02_running_dark"));

    // 开服后自动写进 servers.dat（不需要点按钮）
    const QString serversDat = ServersDatIO::existingPath(ServersDatIO::candidatePaths(instPath));
    bool autoAdded = false;
    QString autoName;
    if (!serversDat.isEmpty()) {
        for (const auto &entry : ServersDatIO::read(serversDat)) {
            if (entry.host == QStringLiteral("127.0.0.1") && entry.port == 25599) {
                autoAdded = true;
                autoName = entry.name;
            }
        }
    }
    check(QStringLiteral("开服后自动写进 servers.dat"), autoAdded,
          page->m_console->toPlainText().right(160).simplified());
    check(QStringLiteral("服务器列表条目用自定义名称"),
          autoName == QStringLiteral("测试服 ● 朋友来玩"), autoName);

    // 复制地址（放在复制日志之前，避免剪贴板被覆盖）
    page->onCopyAddressClicked();
    waitMs(120);
    check(QStringLiteral("复制地址写入剪贴板"),
          QApplication::clipboard()->text() == page->m_manager->address(),
          QApplication::clipboard()->text());

    // ── 指令发送：走服务端 stdin，假 java 会回显 ──
    page->m_console->clear();
    page->m_commandEdit->setText(QStringLiteral("op Steve"));
    page->onCommandEntered();
    page->m_commandEdit->setText(QStringLiteral("say 你好"));
    page->onCommandEntered();
    waitMs(400);
    const QString consoleText = page->m_console->toPlainText();
    check(QStringLiteral("指令写进控制台回显"),
          consoleText.contains(QStringLiteral("> op Steve"))
              && consoleText.contains(QStringLiteral("> say 你好")),
          consoleText.left(120));
    check(QStringLiteral("服务端收到了指令"),
          consoleText.contains(QStringLiteral("<Player> op Steve"))
              && consoleText.contains(QStringLiteral("<Player> say 你好")),
          consoleText.right(160).simplified());
    check(QStringLiteral("指令历史记住了两条"), page->m_commandHistory.size() == 2,
          QString::number(page->m_commandHistory.size()));

    // 指令历史：按一次上键回填最后一条
    {
        page->m_commandEdit->clear();
        QKeyEvent upEvent(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        QApplication::sendEvent(page->m_commandEdit, &upEvent);
        waitMs(80);
        check(QStringLiteral("上键翻出上一条指令"),
              page->m_commandEdit->text() == QStringLiteral("say 你好"),
              page->m_commandEdit->text());
    }

    // ── 进游戏 / 远程加入：只发信号，具体动作由窗口和主窗口做 ──
    bool launchRequested = false;
    QString launchAddress;
    int launchPort = 0;
    bool tunnelRequested = false;
    QObject::connect(page, &InstanceServerPage::launchGameRequested, page,
            [&](const QString &address, int port) {
                launchRequested = true;
                launchAddress = address;
                launchPort = port;
            });
    QObject::connect(page, &InstanceServerPage::multiplayerTabRequested, page,
            [&]() { tunnelRequested = true; });

    page->m_joinGameBtn->click();
    page->m_tunnelBtn->click();
    waitMs(120);
    check(QStringLiteral("「进游戏」发出启动请求"),
          launchRequested && launchAddress == QStringLiteral("127.0.0.1") && launchPort == 25599,
          QStringLiteral("%1:%2").arg(launchAddress).arg(launchPort));
    check(QStringLiteral("「远程加入」请求切到联机页"), tunnelRequested);

    // ── 发布到社区服务器导航：标题/正文/标签/地址按服务器信息生成 ──
    bool publishRequested = false;
    QString publishTitle;
    QString publishContent;
    QStringList publishTags;
    QString publishAddress;
    QObject::connect(page, &InstanceServerPage::publishToServerNavigationRequested, page,
                     [&](const QString &title, const QString &content, const QStringList &tags,
                         const QString &address) {
                         publishRequested = true;
                         publishTitle = title;
                         publishContent = content;
                         publishTags = tags;
                         publishAddress = address;
                     });
    check(QStringLiteral("运行中可以发布到服务器导航"), page->m_publishNavBtn->isEnabled());
    page->m_publishNavBtn->click();
    waitMs(150);
    check(QStringLiteral("「发布到服务器导航」发出请求"), publishRequested, publishTitle);
    check(QStringLiteral("发布标题带服务器名称与版本"),
          publishTitle.contains(QStringLiteral("测试服"))
              && publishTitle.contains(QStringLiteral("1.20.4")),
          publishTitle);
    check(QStringLiteral("发布正文带版本/人数/进服说明"),
          publishContent.contains(QStringLiteral("- 版本：1.20.4"))
              && publishContent.contains(QStringLiteral("容纳人数：7"))
              && publishContent.contains(QStringLiteral("直接连接")),
          publishContent.left(80).simplified());
    check(QStringLiteral("发布标签带版本与服务器"),
          publishTags.contains(QStringLiteral("服务器"))
              && publishTags.contains(QStringLiteral("1.20.4")),
          publishTags.join(QStringLiteral(" / ")));
    check(QStringLiteral("发布默认地址是本机地址"),
          publishAddress == QStringLiteral("127.0.0.1:25599"), publishAddress);

    // ── 在线人数探测：假服不是 MC 协议，只要求不崩、有说明 ──
    page->refreshOnlineInfo();
    waitUntil([&]() { return !page->m_onlineText.isEmpty(); }, 5000);
    check(QStringLiteral("在线人数探测有结论"),
          !page->m_onlineText.isEmpty() && page->m_playersLabel->isVisible(),
          page->m_onlineText.isEmpty() ? QStringLiteral("(空)") : page->m_onlineText);

    // ── 复制日志 ──
    page->onCopyConsoleClicked();
    waitMs(120);
    check(QStringLiteral("复制日志到剪贴板"),
          QApplication::clipboard()->text().contains(QStringLiteral("<Player> say 你好")),
          QString::number(QApplication::clipboard()->text().size()));

    // ── 运行中管理名单：交给服务端指令，由它自己写文件 ──
    page->m_console->clear();
    page->m_whitelistInput->setText(QStringLiteral("Notch"));
    page->onAddWhitelistClicked();
    page->m_adminTabBtns.at(1)->click();
    page->m_opInput->setText(QStringLiteral("Herobrine"));
    page->m_opLevelCombo->setCurrentIndex(page->m_opLevelCombo->findData(4));
    page->onAddOpClicked();
    waitMs(400);
    const QString liveConsole = page->m_console->toPlainText();
    check(QStringLiteral("运行中加白名单走 whitelist add"),
          liveConsole.contains(QStringLiteral("> whitelist add Notch")), liveConsole.right(160));
    check(QStringLiteral("运行中加管理员走 op"),
          liveConsole.contains(QStringLiteral("> op Herobrine")), liveConsole.right(160));
    // 服务端没写回名单时（假 java 不会写），界面要如实说没加上，而不是假装成功
    const bool verifyDone = waitUntil([&]() {
        return page->m_pendingWhitelistAdds.isEmpty() && page->m_pendingOpAdds.isEmpty();
    }, 3000);
    check(QStringLiteral("指令发完会核对名单里到底有没有"), verifyDone,
          page->m_whitelistNotice->text());
    check(QStringLiteral("没加上时提示名字对不上"),
          !page->m_whitelistNotice->text().isEmpty()
              && !page->m_whitelistNotice->text().contains(QStringLiteral("稍等")),
          page->m_whitelistNotice->text());

    // 滚到底部截一张：看控制台指令行和日志排布
    if (auto *scroll = page->findChild<QScrollArea *>(QStringLiteral("serverScroll"))) {
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        shoot(&host, QStringLiteral("06_console_dark"));
        scroll->verticalScrollBar()->setValue(0);
    }

    // 停止服务器
    page->m_primaryBtn->click();
    const bool stopped = waitUntil(
        [&]() { return page->m_manager->state() == LocalServerManager::State::Idle; }, 15000);
    check(QStringLiteral("点停止后回到未开服"), stopped, page->m_statusText->text());
    check(QStringLiteral("未开服时发布按钮不可点"), !page->m_publishNavBtn->isEnabled());
    check(QStringLiteral("停止后控制台有存盘日志"),
          page->m_console->toPlainText().contains(QStringLiteral("Stopping the server"))
              || page->m_console->toPlainText().contains(QStringLiteral("All dimensions are saved")));
    waitMs(300);
    shoot(&host, QStringLiteral("03_stopped_dark"));

    // ── 5b. 运行中被强行结束：给出异常退出提示，界面回到未开服 ──
    {
        bool crashed = false;
        QString crashReason;
        QObject::connect(page->m_manager, &LocalServerManager::crashed, page,
                [&](const QString &reason) {
                    crashed = true;
                    crashReason = reason;
                });

        page->m_console->clear();
        page->m_manager->setState(LocalServerManager::State::Starting);
        page->m_manager->launchProcess(fakeJava);
        const bool readyAgain = waitUntil(
            [&]() { return page->m_manager->state() == LocalServerManager::State::Running; }, 15000);
        if (page->m_manager->m_process)
            page->m_manager->m_process->kill();
        const bool backToIdle = waitUntil(
            [&]() { return page->m_manager->state() == LocalServerManager::State::Idle; }, 15000);
        waitMs(250);
        check(QStringLiteral("服务端被强行结束时给出提示"), readyAgain && crashed, crashReason);
        check(QStringLiteral("异常退出后界面回到未开服"),
              backToIdle && !page->m_joinGameBtn->isVisible(), page->m_statusText->text());
    }

    // ── 6. 常见故障文案 ──
    const QString portErr = page->m_manager->friendlyLaunchError(
        QStringLiteral("**** FAILED TO BIND TO PORT!"));
    check(QStringLiteral("端口占用有中文提示"),
          portErr.contains(QStringLiteral("端口")), portErr);
    const QString javaErr = page->m_manager->friendlyLaunchError(
        QStringLiteral("java.lang.UnsupportedClassVersionError: compiled by a more recent version"));
    check(QStringLiteral("Java 版本过低有中文提示"),
          javaErr.contains(QStringLiteral("Java")), javaErr);
    const QString jarErr =
        page->m_manager->friendlyLaunchError(QStringLiteral("Error: Unable to access jarfile x.jar"));
    check(QStringLiteral("jar 缺失有中文提示"),
          jarErr.contains(QStringLiteral("server")), jarErr);
    check(QStringLiteral("正常日志不误报"),
          page->m_manager->friendlyLaunchError(QStringLiteral("Preparing spawn area: 42%")).isEmpty());

    // ── 7. Forge 实例：现在也能一键开服（走官方安装器） ──
    page->setInstanceContext(instPath, QStringLiteral("1.20.1"), QStringLiteral("Forge"));
    waitMs(150);
    check(QStringLiteral("Forge 实例也能一键开服"),
          page->m_primaryBtn->isEnabled()
              && page->m_statusText->text() == QStringLiteral("未开服"),
          page->m_statusText->text());
    check(QStringLiteral("Forge 实例默认选 Forge 核心"),
          static_cast<LocalServerManager::Core>(page->m_coreCombo->currentData().toInt())
              == LocalServerManager::Core::Forge,
          page->m_coreCombo->currentText());
    check(QStringLiteral("Forge 核心提示会跑官方安装器"),
          page->m_coreHintLabel->text().contains(QStringLiteral("安装器")),
          page->m_coreHintLabel->text());
    shoot(&host, QStringLiteral("04_forge_dark"));

    // 老版本 Forge（1.12.2 之前没有 --installServer）不给选 Forge 核心
    page->setInstanceContext(instPath, QStringLiteral("1.12.2"), QStringLiteral("Forge"));
    waitMs(150);
    check(QStringLiteral("1.12.2 的 Forge 核心被挡掉"),
          !page->m_primaryBtn->isEnabled()
              && page->m_statusText->text().contains(QStringLiteral("不支持当前版本")),
          page->m_statusText->text());

    // ── 8. 浅色主题 ──
    page->setInstanceContext(instPath, QStringLiteral("1.20.4"), QStringLiteral("Fabric"));
    waitMs(200);
    ThemeManager::setTheme(&app, ThemeManager::LightTheme);
    waitMs(400);
    shoot(&host, QStringLiteral("05_idle_light"));

    // ── 9. 真实下载：删掉假 jar，让管理器自己把服务端下来并跑起来 ──
    // 这一步依赖网络；连不上时只记 WARN，不算失败。
    auto downloadCase = [&](const QString &title, LocalServerManager::Core core,
                            const QString &jarName, int versionMajor) {
        QFile::remove(instPath + QStringLiteral("/server/") + jarName);
        LocalServerManager::Config cfg = page->configFromUi();
        cfg.core = core;
        page->m_manager->setConfig(cfg);
        page->m_manager->m_javaPath = fakeJava;
        page->m_manager->setState(LocalServerManager::State::Preparing);
        page->m_console->clear();
        page->m_manager->ensureJar();
        // 走一遍准备阶段看门狗（真机上由 start() 拉起）：卡住的 CDN 要能自己收尾
        page->m_manager->m_prepareTimer->setInterval(60000);
        page->m_manager->m_prepareTimer->start();

        const bool ok = waitUntil([&]() {
            const auto st = page->m_manager->state();
            return st == LocalServerManager::State::Running || st == LocalServerManager::State::Idle;
        }, 120000);

        const QFileInfo jarInfo(instPath + QStringLiteral("/server/") + jarName);
        const bool gotJar = jarInfo.isFile() && jarInfo.size() > 100 * 1024;
        const bool running = page->m_manager->state() == LocalServerManager::State::Running;
        const QString detail = QStringLiteral("%1（%2 MB，state=%3）")
                                   .arg(jarName)
                                   .arg(jarInfo.size() / 1024 / 1024)
                                   .arg(static_cast<int>(page->m_manager->state()));
        if (gotJar && running) {
            check(QStringLiteral("真实下载 %1 并启动").arg(title), ok && gotJar && running, detail);
            page->m_manager->stop();
            waitUntil([&]() {
                return page->m_manager->state() == LocalServerManager::State::Idle;
            }, 15000);
        } else if (page->m_manager->state() == LocalServerManager::State::Idle
                   && page->m_manager->lastError().contains(QStringLiteral("超时"))) {
            // 这条 CDN 在本机网络下连不上：确认看门狗按预期收尾，而不是一直转圈
            check(QStringLiteral("真实下载 %1 卡住时按超时收尾").arg(title), true,
                  page->m_manager->lastError());
        } else {
            logLine(QStringLiteral("[WARN] 真实下载 %1 未完成（网络原因跳过）：%2 | %3")
                        .arg(title, detail,
                             page->m_console->toPlainText().right(200).simplified()));
        }
        Q_UNUSED(versionMajor);
    };

    downloadCase(QStringLiteral("Fabric 服务端"), LocalServerManager::Core::Fabric,
                 QStringLiteral("fabric-server.jar"), 17);
    downloadCase(QStringLiteral("原版服务端"), LocalServerManager::Core::Vanilla,
                 QStringLiteral("server.jar"), 17);
    downloadCase(QStringLiteral("Paper 服务端"), LocalServerManager::Core::Paper,
                 QStringLiteral("paper-server.jar"), 17);

    // ── 10. 发帖页预填（「发布到服务器导航」落到发帖页的那一半） ──
    {
        auto *composer = new ForumComposerPage(&host);
        composer->resize(620, 720);
        composer->startNewNavigation(QStringLiteral("server"),
                                     QStringLiteral("play.example.com:25565"),
                                     QStringLiteral("测试服（Minecraft 1.20.4）"),
                                     QStringLiteral("## 服务器信息\n- 版本：1.20.4"),
                                     {QStringLiteral("服务器"), QStringLiteral("1.20.4")});
        waitMs(250);
        check(QStringLiteral("发帖页切到「服务器」导航类型"),
              composer->m_navKind == QStringLiteral("server"), composer->m_navKind);
        check(QStringLiteral("发帖页预填地址"),
              composer->m_navUrlEdit->text() == QStringLiteral("play.example.com:25565"),
              composer->m_navUrlEdit->text());
        check(QStringLiteral("发帖页预填标题与正文"),
              composer->m_titleEdit->text() == QStringLiteral("测试服（Minecraft 1.20.4）")
                  && composer->m_contentEdit->toPlainText().contains(
                      QStringLiteral("- 版本：1.20.4")),
              composer->m_titleEdit->text());
        check(QStringLiteral("发帖页预填标签"),
              composer->m_tagsEditor->tags().contains(QStringLiteral("服务器"))
                  && composer->m_tagsEditor->tags().contains(QStringLiteral("1.20.4")),
              composer->m_tagsEditor->tags().join(QStringLiteral(" / ")));
        composer->show();
        waitMs(400);
        shoot(composer, QStringLiteral("07_composer_nav"));
        composer->hide();
        delete composer;
    }

    // ── 11. 隧道地址提取：发布时优先用联机页已经建好的地址 ──
    {
        auto *multiplayer = new MultiplayerPage(&host);
        check(QStringLiteral("没有隧道时不给地址"), multiplayer->currentTunnelAddress().isEmpty(),
              multiplayer->currentTunnelAddress());

        multiplayer->m_frpAddrLabel->setText(QStringLiteral("abc.natfrp.cloud:23456"));
        check(QStringLiteral("取到 frp 隧道地址"),
              multiplayer->currentTunnelAddress() == QStringLiteral("abc.natfrp.cloud:23456"),
              multiplayer->currentTunnelAddress());

        // frp 还在连接时红石的地址不该被顶掉，说明文字也不能当地址
        multiplayer->m_frpAddrLabel->setText(QStringLiteral("正在连接节点..."));
        multiplayer->m_tunnelAddrLabel->setText(QStringLiteral("1.2.3.4:25565"));
        check(QStringLiteral("取到红石隧道地址"),
              multiplayer->currentTunnelAddress() == QStringLiteral("1.2.3.4:25565"),
              multiplayer->currentTunnelAddress());

        multiplayer->m_tunnelAddrLabel->setText(QStringLiteral("正在建立隧道..."));
        check(QStringLiteral("状态说明不当地址用"),
              multiplayer->currentTunnelAddress().isEmpty(),
              multiplayer->currentTunnelAddress());
        delete multiplayer;
    }

    // ── 12. 用实例存档开服：世界复制 ──
    {
        const QString worldTarget = tmpRoot + QStringLiteral("/world_copy");
        QDir(worldTarget).removeRecursively();
        qint64 progressDone = 0;
        qint64 progressTotal = 0;
        int progressCalls = 0;
        QString copyError;
        const bool copied = LocalServerManager::copyWorldTree(
            saveDir, worldTarget,
            [&](qint64 done, qint64 total) {
                progressDone = done;
                progressTotal = total;
                ++progressCalls;
            },
            nullptr, &copyError);
        check(QStringLiteral("存档能复制成服务端世界"), copied, copyError);
        check(QStringLiteral("level.dat 与区块文件都复制了"),
              QFileInfo::exists(worldTarget + QStringLiteral("/level.dat"))
                  && QFileInfo::exists(worldTarget + QStringLiteral("/region/r.0.0.mca"))
                  && QFileInfo::exists(worldTarget + QStringLiteral("/playerdata/player.dat")));
        check(QStringLiteral("客户端 session.lock 没带过去"),
              !QFileInfo::exists(worldTarget + QStringLiteral("/session.lock")));
        check(QStringLiteral("复制过程报了进度"),
              progressCalls > 0 && progressTotal > 0 && progressDone == progressTotal,
              QStringLiteral("%1/%2，回调 %3 次")
                  .arg(progressDone)
                  .arg(progressTotal)
                  .arg(progressCalls));

        // 界面：实例里有存档时，「世界」下拉里能选到
        page->setInstanceContext(instPath, QStringLiteral("1.20.4"), QStringLiteral("Fabric"));
        waitMs(200);
        check(QStringLiteral("世界下拉列出新建世界与存档"),
              page->m_worldCombo->count() == 2
                  && page->m_worldCombo->itemData(0).toString().isEmpty()
                  && page->m_worldCombo->itemData(1).toString().contains(QStringLiteral("saves")),
              QStringLiteral("%1 项：%2")
                  .arg(page->m_worldCombo->count())
                  .arg(page->m_worldCombo->currentText()));
        page->m_worldCombo->setCurrentIndex(1);
        page->onWorldChanged(1);
        waitMs(120);
        check(QStringLiteral("选存档后写进开服参数"),
              QDir::cleanPath(page->m_manager->config().worldSource)
                  == QDir::cleanPath(saveDir),
              page->m_manager->config().worldSource);
        check(QStringLiteral("存档选择被记住"),
              QDir::cleanPath(settings->getProperty(QStringLiteral("local_server_world")).toString())
                  == QDir::cleanPath(saveDir),
              settings->getProperty(QStringLiteral("local_server_world")).toString());
        page->m_worldCombo->setCurrentIndex(0);
        page->onWorldChanged(0);
        waitMs(80);
    }

    // ── 12b. 完整链路：选存档开服（复制世界 → 启动服务端） ──
    {
        const QString chainInst = tmpRoot + QStringLiteral("/instance_world");
        QDir(chainInst).removeRecursively();
        QDir().mkpath(chainInst + QStringLiteral("/server"));
        QDir().mkpath(chainInst + QStringLiteral("/saves/我的世界/region"));
        writeFile(chainInst + QStringLiteral("/saves/我的世界/level.dat"), QByteArray(4096, 'L'));
        writeFile(chainInst + QStringLiteral("/saves/我的世界/region/r.0.0.mca"),
                  QByteArray(30000, 'R'));
        writeFile(chainInst + QStringLiteral("/saves/我的世界/session.lock"), QByteArray("lock"));
        writeFile(chainInst + QStringLiteral("/server/fabric-server.jar"), fakeJar);

        // 这一块用独立宿主窗口：主 host 里放的是另一个页面，抓图会抓错对象
        QWidget worldHost;
        worldHost.resize(450, 820);
        auto *worldLayout = new QVBoxLayout(&worldHost);
        worldLayout->setContentsMargins(0, 0, 0, 0);
        auto *worldPage = new InstanceServerPage(&worldHost);
        worldLayout->addWidget(worldPage);
        worldHost.show();
        waitMs(300);

        worldPage->setInstanceContext(chainInst, QStringLiteral("1.20.4"),
                                      QStringLiteral("Fabric"));
        waitMs(200);
        check(QStringLiteral("链路上世界下拉能选到存档"),
              worldPage->m_worldCombo->count() == 2,
              QString::number(worldPage->m_worldCombo->count()));
        {
            QSignalBlocker blocker(worldPage->m_worldCombo);
            worldPage->m_worldCombo->setCurrentIndex(1);
        }
        worldPage->onWorldChanged(1);
        waitMs(120);
        // 滚到设置卡，截一张「世界」选了存档的样子
        if (auto *scroll = worldPage->findChild<QScrollArea *>(QStringLiteral("serverScroll"))) {
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            shoot(&worldHost, QStringLiteral("08_world_dark"));
            scroll->verticalScrollBar()->setValue(0);
        }

        worldPage->m_manager->m_javaPath = fakeJava;
        worldPage->m_manager->setConfig(worldPage->configFromUi());
        worldPage->m_manager->setState(LocalServerManager::State::Preparing);
        worldPage->m_manager->beginWorldCopy();

        const bool chainRunning =
            waitUntil([&]() { return worldPage->m_manager->isRunning(); }, 30000);
        check(QStringLiteral("用存档开服一路跑到就绪"), chainRunning,
              worldPage->m_manager->lastError());
        check(QStringLiteral("服务端世界已经生成"),
              QFileInfo::exists(chainInst + QStringLiteral("/server/world/level.dat"))
                  && QFileInfo::exists(chainInst
                                       + QStringLiteral("/server/world/region/r.0.0.mca")));
        check(QStringLiteral("记下了世界是从哪个存档来的"),
              worldPage->m_manager->serverWorldSource().contains(QStringLiteral("saves")),
              worldPage->m_manager->serverWorldSource());

        worldPage->m_manager->stop();
        waitUntil([&]() { return worldPage->m_manager->state() == LocalServerManager::State::Idle; },
                  15000);
        worldHost.hide();
        delete worldPage;
    }

    // ── 13. Forge / NeoForge：版本解析与可用性 ──
    {
        const QByteArray forgeMeta =
            QByteArray("<metadata><versioning><versions>"
                       "<version>1.20.1-47.1.0</version>"
                       "<version>1.20.1-47.1.3</version>"
                       "<version>1.20.1-47.2.0</version>"
                       "<version>1.20.1-47.3.0-beta</version>"
                       "<version>1.20.4-49.0.30</version>"
                       "</versions></versioning></metadata>");
        QString forgeVersion;
        const bool forgeOk =
            LocalServerManager::pickMavenVersion(forgeMeta, QStringLiteral("1.20.1-"),
                                                 &forgeVersion);
        check(QStringLiteral("Forge 版本挑出最高的稳定版"),
              forgeOk && forgeVersion == QStringLiteral("1.20.1-47.2.0"), forgeVersion);

        QString missingVersion;
        check(QStringLiteral("没有对应版本时不乱挑"),
              !LocalServerManager::pickMavenVersion(forgeMeta, QStringLiteral("1.19.2-"),
                                                    &missingVersion),
              missingVersion);

        const QByteArray neoMeta =
            QByteArray("<metadata><versioning><versions>"
                       "<version>21.1.9</version>"
                       "<version>21.1.77</version>"
                       "<version>21.4.0-beta</version>"
                       "<version>20.4.237</version>"
                       "</versions></versioning></metadata>");
        QString neoVersion;
        const bool neoOk = LocalServerManager::pickMavenVersion(neoMeta, QStringLiteral("21.1."),
                                                                &neoVersion);
        check(QStringLiteral("NeoForge 版本挑对分段"),
              neoOk && neoVersion == QStringLiteral("21.1.77"), neoVersion);

        check(QStringLiteral("1.20.1 能用 Forge 不能用 NeoForge"),
              LocalServerManager::coreAvailableForVersion(LocalServerManager::Core::Forge,
                                                          QStringLiteral("1.20.1"))
                  && !LocalServerManager::coreAvailableForVersion(
                      LocalServerManager::Core::NeoForge, QStringLiteral("1.20.1")));
        check(QStringLiteral("1.21.1 两种模组服都能用"),
              LocalServerManager::coreAvailableForVersion(LocalServerManager::Core::Forge,
                                                          QStringLiteral("1.21.1"))
                  && LocalServerManager::coreAvailableForVersion(
                      LocalServerManager::Core::NeoForge, QStringLiteral("1.21.1")));
        check(QStringLiteral("1.12.2 老 Forge 不给选"),
              !LocalServerManager::coreAvailableForVersion(LocalServerManager::Core::Forge,
                                                           QStringLiteral("1.12.2")));
    }

    // ── 14. Forge 安装器流程（用假安装器跑一遍） ──
    {
        const QString forgeInst = tmpRoot + QStringLiteral("/instance_forge");
        QDir(forgeInst).removeRecursively();
        QDir().mkpath(forgeInst);

        auto *forgePage = new InstanceServerPage(&host);
        forgePage->setInstanceContext(forgeInst, QStringLiteral("1.20.1"),
                                      QStringLiteral("Forge"));
        waitMs(200);
        check(QStringLiteral("Forge 实例默认选 Forge 核心"),
              static_cast<LocalServerManager::Core>(
                  forgePage->m_coreCombo->currentData().toInt())
                  == LocalServerManager::Core::Forge,
              forgePage->m_coreCombo->currentText());
        check(QStringLiteral("1.12.2 实例的 Forge 核心置灰不可选"), [&]() {
            auto *oldPage = new InstanceServerPage(&host);
            oldPage->setInstanceContext(forgeInst, QStringLiteral("1.12.2"),
                                        QStringLiteral("Forge"));
            waitMs(120);
            const int forgeIndex = oldPage->m_coreCombo->findData(
                static_cast<int>(LocalServerManager::Core::Forge));
            if (forgeIndex < 0) {
                delete oldPage;
                return false;   // 列表里干脆没有这一项也算「不给选」，但我们要的是置灰
            }
            auto *model = qobject_cast<QStandardItemModel *>(oldPage->m_coreCombo->model());
            const bool disabled = model && model->item(forgeIndex)
                                  && !model->item(forgeIndex)->isEnabled();
            const bool blocked = !oldPage->m_primaryBtn->isEnabled();
            delete oldPage;
            return disabled && blocked;
        }());

        // 假安装器：跑 runInstaller，它会建出 libraries/.../win_args.txt
        qputenv("FAKE_FORGE_MC", "1.20.1");
        qputenv("FAKE_FORGE_VERSION", "47.2.0");
        forgePage->m_manager->m_installerVersion = QStringLiteral("47.2.0");
        forgePage->m_manager->m_javaPath = fakeJava;
        forgePage->m_manager->setState(LocalServerManager::State::Preparing);
        forgePage->m_console->clear();
        forgePage->m_manager->runInstaller(forgePage->m_manager->jarPath(),
                                          QStringLiteral("47.2.0"));

        const bool started = waitUntil(
            [&]() { return forgePage->m_manager->isRunning(); }, 20000);
        check(QStringLiteral("Forge 装完能直接启动"), started,
              forgePage->m_manager->lastError());
        check(QStringLiteral("装好后能找到 @args 启动配置"), [&]() {
            QString argsFile;
            QString launcherJar;
            const bool found = forgePage->m_manager->locateInstallerLauncher(&argsFile,
                                                                            &launcherJar);
            return found && !argsFile.isEmpty() && argsFile.endsWith(QStringLiteral("win_args.txt"));
        }());
        const QStringList forgeArgs = forgePage->m_manager->buildLaunchArgs();
        check(QStringLiteral("启动参数用 @args 文件"),
              forgeArgs.contains(QStringLiteral("nogui"))
                  && !forgeArgs.filter(QStringLiteral("@libraries/")).isEmpty(),
              forgeArgs.join(QLatin1Char(' ')));
        check(QStringLiteral("装好后 jarReady 认为是就绪的"),
              forgePage->m_manager->jarReady(),
              forgePage->m_manager->readyMarkerPath());
        check(QStringLiteral("Forge 服务端能识别到 Done"),
              forgePage->m_console->toPlainText().contains(QStringLiteral("Done (1.234s)")),
              forgePage->m_console->toPlainText().right(120).simplified());
        forgePage->m_manager->stop();
        waitUntil([&]() { return forgePage->m_manager->state() == LocalServerManager::State::Idle; },
                  15000);
        delete forgePage;
        qunsetenv("FAKE_FORGE_MC");
        qunsetenv("FAKE_FORGE_VERSION");
    }

    // ── 15. 「下载服务端」：只下载模式 + 弹窗端到端 ──
    {
        // 15a. 管理器只下载模式：Fabric 服务端只有一百多 KB，适合跑真下载
        const QString downloadDir = tmpRoot + QStringLiteral("/download_only");
        QDir(downloadDir).removeRecursively();
        LocalServerManager downloader;
        LocalServerManager::Config cfg;
        cfg.version = QStringLiteral("1.20.4");
        cfg.core = LocalServerManager::Core::Fabric;
        cfg.serverDirOverride = downloadDir;
        cfg.downloadOnly = true;
        downloader.setConfig(cfg);

        QString finishedPath;
        bool needsInstall = true;
        QObject::connect(&downloader, &LocalServerManager::downloadFinished, &downloader,
                         [&](const QString &path, bool needsInstallFlag) {
                             finishedPath = path;
                             needsInstall = needsInstallFlag;
                         });
        downloader.start();
        const bool downloaded =
            waitUntil([&]() { return !finishedPath.isEmpty(); }, 120000);
        if (downloaded) {
            check(QStringLiteral("只下载模式能下到服务端 jar"),
                  !needsInstall && QFileInfo(finishedPath).size() > 100 * 1024, finishedPath);
            check(QStringLiteral("只下载模式不写 eula / server.properties"),
                  !QFileInfo::exists(downloadDir + QStringLiteral("/eula.txt"))
                      && !QFileInfo::exists(downloadDir + QStringLiteral("/server.properties")));
            check(QStringLiteral("只下载模式不需要 Java 也不启动服务"),
                  downloader.state() == LocalServerManager::State::Idle
                      && !downloader.isRunning());
        } else {
            logLine(QStringLiteral("[WARN] 只下载模式没跑成（网络原因跳过）：%1")
                        .arg(downloader.lastError()));
        }

        // 15b. 弹窗端到端：打开 → 切 Fabric → 换目录 → 点下载 → 成功截图
        const QString dialogDir = tmpRoot + QStringLiteral("/download_dialog");
        QDir(dialogDir).removeRecursively();
        QDir().mkpath(dialogDir);

        bool dialogFound = false;
        QString dialogDefaultCore;
        QString dialogStatus;
        QString dialogFile;
        bool dialogHasOpenButton = false;

        QTimer::singleShot(700, [&]() {
            QWidget *modal = QApplication::activeModalWidget();
            if (!modal)
                return;
            auto *combo = modal->findChild<QComboBox *>(QStringLiteral("serverDownloadCombo"));
            auto *dirEdit = modal->findChild<QLineEdit *>(QStringLiteral("serverDownloadDirEdit"));
            auto *primary = modal->findChild<QPushButton *>(QStringLiteral("serverDownloadPrimaryBtn"));
            auto *status = modal->findChild<QLabel *>(QStringLiteral("serverDownloadStatus"));
            if (!combo || !dirEdit || !primary || !status)
                return;

            dialogFound = true;
            dialogDefaultCore = combo->currentText();
            // 选 Fabric（文件小）+ 换成本次测试目录
            const int fabricIndex =
                combo->findData(static_cast<int>(LocalServerManager::Core::Fabric));
            if (fabricIndex >= 0)
                combo->setCurrentIndex(fabricIndex);
            dirEdit->setText(dialogDir);
            modal->grab().save(QStringLiteral(".tmp_server_probe/out/09_server_download.png"));

            primary->click();   // 开始下载

            for (int waited = 0; waited < 90000; waited += 150) {
                QEventLoop loop;
                QTimer::singleShot(150, &loop, &QEventLoop::quit);
                loop.exec();
                dialogStatus = status->text();
                if (dialogStatus.contains(QStringLiteral("已下载")))
                    break;
            }
            const QFileInfo jar(dialogDir + QStringLiteral("/fabric-server.jar"));
            if (jar.exists())
                dialogFile = jar.absoluteFilePath();
            for (auto *btn : modal->findChildren<QPushButton *>()) {
                if (btn->text() == QObject::tr("打开目录") && btn->isEnabled())
                    dialogHasOpenButton = true;
            }
            // 等布局把「打开目录 / 复制路径 / 重新下载」摆好再抓图
            {
                QEventLoop settle;
                QTimer::singleShot(400, &settle, &QEventLoop::quit);
                settle.exec();
            }
            modal->grab().save(
                QStringLiteral(".tmp_server_probe/out/10_server_download_done.png"));
            if (auto *card = modal->findChild<QWidget *>(QStringLiteral("serverDownloadCard"))) {
                auto *startBtn =
                    modal->findChild<QPushButton *>(QStringLiteral("serverDownloadPrimaryBtn"));
                // 弹窗内容由 AppDialogBase 套了滚动区，最容易踩高度的坑：按钮不能被裁掉
                check(QStringLiteral("「重新下载」按钮完整落在卡片里"),
                      startBtn && startBtn->isVisible()
                          && startBtn->geometry().bottom() <= card->height(),
                      QStringLiteral("按钮底 %1 / 卡片高 %2")
                          .arg(startBtn ? startBtn->geometry().bottom() : -1)
                          .arg(card->height()));
            }
            modal->close();
        });

        ServerDownloadDialog::openFor(&host, QStringLiteral("1.20.4"));

        check(QStringLiteral("下载服务端弹窗能打开且控件齐全"), dialogFound, dialogDefaultCore);
        check(QStringLiteral("弹窗默认核心是原版"),
              dialogDefaultCore.contains(QStringLiteral("原版")), dialogDefaultCore);
        if (!dialogFile.isEmpty()) {
            check(QStringLiteral("弹窗里点下载能把文件下到指定目录"),
                  QFileInfo(dialogFile).size() > 100 * 1024, dialogFile);
            check(QStringLiteral("下载完成后出现「打开目录」按钮"), dialogHasOpenButton, dialogStatus);
        } else {
            logLine(QStringLiteral("[WARN] 弹窗下载没完成（网络原因跳过）：%1").arg(dialogStatus));
        }
    }

    // ── 还原用户设置 ──
    if (oldPort.isValid()) settings->setProperty(QStringLiteral("local_server_port"), oldPort);
    else settings->removeProperty(QStringLiteral("local_server_port"));
    if (oldPlayers.isValid())
        settings->setProperty(QStringLiteral("local_server_players"), oldPlayers);
    else settings->removeProperty(QStringLiteral("local_server_players"));
    if (oldMemory.isValid()) settings->setProperty(QStringLiteral("local_server_memory"), oldMemory);
    else settings->removeProperty(QStringLiteral("local_server_memory"));
    if (oldOnline.isValid())
        settings->setProperty(QStringLiteral("local_server_online_mode"), oldOnline);
    else settings->removeProperty(QStringLiteral("local_server_online_mode"));
    if (oldCore.isValid()) settings->setProperty(QStringLiteral("local_server_core"), oldCore);
    else settings->removeProperty(QStringLiteral("local_server_core"));
    if (oldMotd.isValid()) settings->setProperty(QStringLiteral("local_server_motd"), oldMotd);
    else settings->removeProperty(QStringLiteral("local_server_motd"));
    if (oldWorld.isValid()) settings->setProperty(QStringLiteral("local_server_world"), oldWorld);
    else settings->removeProperty(QStringLiteral("local_server_world"));

    logLine(QStringLiteral("=== 汇总：PASS %1 / FAIL %2 ===").arg(g_pass).arg(g_fail));
    QFile report(tmpRoot + QStringLiteral("/report.txt"));
    if (report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        report.write(g_report.join(QLatin1Char('\n')).toUtf8());

    return g_fail == 0 ? 0 : 1;
}
