/**
 * @file   multiplayer_page_probe.cpp
 * @brief  实例助手「联机」页面样式探针：真实页面 + 真实令牌 QSS，逐页截图核对排版
 *
 * 不启动 MainWindow，只把一个 MultiplayerPage 放进空窗口，注入一组假状态后
 * 依次把页面栈里的 6 个页面（陶瓦首页 / 陶瓦房间 / 红石首页 / 红石隧道 /
 * 樱花首页 / frp 房间）渲染出来，亮色与暗色各来一轮，用来核对：
 *   1. 六个页面的实际排版、字号层级、按钮尺寸是否统一；
 *   2. 亮 / 暗主题下文字、边框、按钮是否都可读；
 *   3. 页面样式表里的 @TOKEN@ 是否全部解析完（残留占位符会让 Qt 丢掉规则）；
 *   4. 窄屏（手机比例）与宽屏（桌面实例助手）下都不溢出。
 *
 * 输出：.tmp_mp_probe/probe_out/mp_*.png
 * 退出码 0 = 全部断言通过。
 */
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>

#include <cstdio>

#include "utils/ThemeManager.h"

// 探针需要注入玩家资料并直接重绘列表 / 覆写状态文案，这里临时放开访问权限
// （只影响本翻译单元，与 lobby_page_style_probe 同一套做法）
#define private public
#include "pages/MultiplayerPage.h"
#include "utils/Terracotta/TerracottaClient.h"
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

/// 关掉页面里所有定时器，避免探针渲染时被 refreshPage 抢回页面栈
void stopTimers(QWidget *root)
{
    for (QTimer *timer : root->findChildren<QTimer *>())
        timer->stop();
}

/// 用一个假的玩家列表把「房间页」的玩家卡片渲染出来
void seedPlayers(MultiplayerPage &page)
{
    QList<TerracottaClient::Profile> profiles;

    TerracottaClient::Profile host;
    host.machineId = QStringLiteral("a");
    host.name = QStringLiteral("小明");
    host.vendor = QStringLiteral("Terracotta 2.6.0");
    host.kind = QStringLiteral("HOST");
    profiles << host;

    TerracottaClient::Profile local;
    local.machineId = QStringLiteral("b");
    local.name = QStringLiteral("BlockBox 玩家");
    local.vendor = QStringLiteral("BlockBox beta8");
    local.kind = QStringLiteral("LOCAL");
    profiles << local;

    TerracottaClient::Profile guest;
    guest.machineId = QStringLiteral("c");
    guest.name = QStringLiteral("一起来挖矿的邻居");
    guest.vendor = QStringLiteral("HMCL 3.5.9");
    guest.kind = QStringLiteral("GUEST");
    profiles << guest;

    TerracottaClient::instance()->m_profiles = profiles;
    page.refreshPlayerList();
}

/// 给红石首页的节点下拉框填几条假节点
void seedNodes(MultiplayerPage &page)
{
    if (!page.m_hongshiNodeCombo)
        return;
    page.m_hongshiNodeCombo->clear();
    page.m_hongshiNodeCombo->addItem(QStringLiteral("北京 · bj.hongshi.online"));
    page.m_hongshiNodeCombo->addItem(QStringLiteral("上海 · sh.hongshi.online"));
    page.m_hongshiNodeCombo->addItem(QStringLiteral("广州 · gz.hongshi.online"));
    if (page.m_hongshiNodeHint)
        page.m_hongshiNodeHint->setText(QStringLiteral("已从主站获取 3 个中转节点"));
}

void setText(QLabel *label, const QString &text)
{
    if (label)
        label->setText(text);
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    // offscreen 平台不会自己去找系统字体，手动挂上中文黑体，否则截图全是方框
    for (const QString &path : {QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
                                QStringLiteral("C:/Windows/Fonts/msyhbd.ttc"),
                                QStringLiteral("C:/Windows/Fonts/segoeui.ttf")})
    {
        if (QFontDatabase::addApplicationFont(path) >= 0)
            fprintf(stderr, "[probe] font %s ok\n", qPrintable(path));
    }
    QFont probeFont(QStringLiteral("Microsoft YaHei"));
    probeFont.setPixelSize(13);
    app.setFont(probeFont);

    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);

    QDir().mkpath(QStringLiteral(".tmp_mp_probe/probe_out"));

    MultiplayerPage page;
    stopTimers(&page);

    auto *host = new QWidget;
    host->setObjectName(QStringLiteral("probeHost"));
    host->resize(430, 760);

    page.setParent(host);
    page.setGeometry(0, 0, 430, 760);
    host->show();
    pump();

    QStackedWidget *stack = page.findChild<QStackedWidget *>(QStringLiteral("multiplayerStack"));
    expect("页面栈存在且为 6 页", stack && stack->count() == 6);
    if (!stack)
        return 1;

    expect("核心下拉框存在", page.m_coreCombo != nullptr);

    // 注入一组看得懂的假状态（真实流程要连核心进程，探针不启动）
    seedPlayers(page);
    seedNodes(page);

    // 探针默认把状态点设成「正常」，并让主操作按钮可用 —— 真实环境里装好核心后
    // 就是这副样子；逐个页面截图核对按钮/圆点的成型样式
    const auto setTone = [](QLabel *dot, const char *tone) {
        if (!dot)
            return;
        dot->setProperty("tone", QString::fromLatin1(tone));
        dot->style()->unpolish(dot);
        dot->style()->polish(dot);
    };
    for (QLabel *dot : {page.m_statusDot, page.m_latencyDot, page.m_hongshiStatusDot,
                        page.m_hongshiLatencyDot, page.m_frpStatusDot, page.m_frpLatencyDot})
        setTone(dot, "ok");
    for (QPushButton *button : page.findChildren<QPushButton *>())
        button->setEnabled(true);
    page.m_latencyLabel->setProperty("status", QStringLiteral("good"));
    page.m_hongshiLatencyLabel->setProperty("status", QStringLiteral("good"));
    page.m_frpLatencyLabel->setProperty("status", QStringLiteral("good"));

    // 0 陶瓦首页
    setText(page.m_statusLabel, QStringLiteral("联机核心已就绪（v2.6.0）"));
    // 1 陶瓦房间页
    setText(page.m_latencyLabel, QStringLiteral("房主就绪 | 联机时间 12:34"));
    setText(page.m_roomCodeLabel, QStringLiteral("U/8F2K-91QX-7B3D-4M2N"));
    // 2 红石首页
    setText(page.m_hongshiStatusLabel, QStringLiteral("红石联机内核已就绪（v1.4.0）"));
    // 3 红石隧道页
    setText(page.m_hongshiLatencyLabel, QStringLiteral("隧道已建立 | 联机时间 05:12"));
    setText(page.m_tunnelAddrLabel, QStringLiteral("mc-frp.example.com:25565"));
    // 4 樱花首页
    setText(page.m_frpStatusLabel, QStringLiteral("frpc 客户端已就绪（v0.61.0）"));
    if (page.m_frpKeyEdit)
        page.m_frpKeyEdit->setText(QStringLiteral("sakura-access-key-8f2k91qx"));
    if (page.m_frpIdEdit)
        page.m_frpIdEdit->setText(QStringLiteral("10245"));
    setText(page.m_frpHintLabel,
            QStringLiteral("没有账号？点「打开面板」注册并创建 TCP 隧道，再把密钥和 ID 填在上面。"));
    // 5 frp 房间页
    setText(page.m_frpLatencyLabel, QStringLiteral("隧道已建立 | 联机时间 03:41"));
    setText(page.m_frpAddrLabel, QStringLiteral("frp-abc.top:25565"));
    setText(page.m_frpRoomHint,
            QStringLiteral("在 Minecraft 多人游戏中连接上方地址即可联机，请将地址分享给好友。"));

    const char *names[] = {"home", "room", "hongshi_home", "hongshi_tunnel", "frp_home", "frp_room"};
    const int widths[] = {430, 900};

    for (int themePass = 0; themePass < 2; ++themePass)
    {
        const bool dark = themePass == 1;
        ThemeManager::instance()->loadTheme(dark ? ThemeManager::DarkTheme
                                                 : ThemeManager::LightTheme);
        page.applyThemeStyles();
        seedPlayers(page);
        pump();

        // 样式表里的 @TOKEN@ 必须全部解析完
        const QString sheet = page.styleSheet();
        expect("页面样式表非空", !sheet.isEmpty());
        expect("样式表里没有残留的 @TOKEN@", !sheet.contains(QLatin1Char('@')));

        for (int index = 0; index < 6; ++index)
        {
            QWidget *sub = stack->widget(index);
            if (!sub)
                continue;
            for (int w = 0; w < 2; ++w)
            {
                const int width = widths[w];
                host->resize(width, 760);
                page.setGeometry(0, 0, width, 760);
                stack->setCurrentIndex(index);
                pump();
                const QString path = QStringLiteral(".tmp_mp_probe/probe_out/mp_%1_%2_%3.png")
                                         .arg(QLatin1String(names[index]))
                                         .arg(width)
                                         .arg(dark ? QStringLiteral("dark")
                                                   : QStringLiteral("light"));
                if (!savePng(host, path))
                    ++g_failures;
            }
        }
    }

    // 字体层级抽查：按钮高度、字号是否统一
    if (page.m_joinBtn && page.m_createBtn)
    {
        fprintf(stderr, "[probe] join=%dx%d create=%dx%d\n", page.m_joinBtn->width(),
                page.m_joinBtn->height(), page.m_createBtn->width(), page.m_createBtn->height());
        expect("加入 / 创建按钮高度一致",
               page.m_joinBtn->height() == page.m_createBtn->height());
    }

    // ---- 额外状态：已发布到大厅的按钮 + 空玩家列表 + 警告色状态点 ----
    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    page.applyThemeStyles();
    TerracottaClient::instance()->m_profiles.clear();
    page.refreshPlayerList();
    for (QPushButton *button : {page.m_pubTerracottaBtn, page.m_pubHongshiBtn, page.m_pubFrpBtn})
    {
        if (!button)
            continue;
        button->setProperty("published", true);
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
    setTone(page.m_statusDot, "warn");
    setTone(page.m_latencyDot, "err");
    stack->setCurrentIndex(1);
    pump();
    expect("空玩家列表有占位提示",
           !page.findChildren<QLabel *>(QStringLiteral("mpEmptyPlayers")).isEmpty());
    savePng(host, QStringLiteral(".tmp_mp_probe/probe_out/mp_room_states.png"));

    ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    fprintf(stderr, "[probe] %s (failures=%d)\n",
            g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
