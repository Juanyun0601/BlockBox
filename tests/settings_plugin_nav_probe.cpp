/**
 * @file   settings_plugin_nav_probe.cpp
 * @brief  插件管理挪进「设置」后的导航探针（集成验证）
 *
 * 链接主程序全部目标文件（只重编被改动的翻译单元），验证：
 *   1. 「更多」子导航里已经没有「插件管理」，落地页也没有这张卡片；
 *   2. 「设置」子导航最后一项是「插件管理」，且序号等于 SettingsPage 的
 *      pluginManageTabIndex()（设置内容页数量）；
 *   3. 点「插件管理」进入插件整页：侧边栏仍选中「设置」、子导航保持展开并高亮该项、
 *      顶栏标题是「方块盒子>设置>插件管理」；
 *   4. 从插件页点子导航里的「常规设置」能切回设置内容页（顶栏标题同步）；
 *   5. 回到「更多」子导航不再出现「插件管理」。
 *
 * 只读本地设置与插件目录，不做下载 / 安装 / 登录。
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "components/SideBar.h"
#include "components/SubNavPanel.h"
#include "components/TopBar.h"
#include "pages/PluginPage.h"
#include "pages/SettingsPage.h"
#include "utils/ThemeManager.h"

#include <cstdio>

namespace {

void stderrMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx)
    const char *tag = "[info ]";
    if (type == QtWarningMsg) tag = "[warn ]";
    else if (type == QtCriticalMsg || type == QtFatalMsg) tag = "[crit ]";
    fprintf(stderr, "%s %s\n", tag, msg.toLocal8Bit().constData());
    fflush(stderr);
}

void pump(QWidget *w, int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (w) w->repaint();
    }
}

bool grab(QWidget *w, const QString &path)
{
    if (!w) return false;
    const bool ok = w->grab().save(path);
    fprintf(stderr, "[%s] %s\n", ok ? "ok" : "FAIL", path.toLocal8Bit().constData());
    fflush(stderr);
    return ok;
}

QPushButton *navButton(SideBar *bar, int parentIndex)
{
    if (!bar) return nullptr;
    const QList<QPushButton *> buttons = bar->findChildren<QPushButton *>(
        QStringLiteral("parentNavButton"));
    for (QPushButton *b : buttons) {
        if (b && b->property("parentIndex").toInt() == parentIndex)
            return b;
    }
    return nullptr;
}

QPushButton *subNavButton(SubNavPanel *panel, int parentIndex, int childIndex)
{
    if (!panel) return nullptr;
    const QList<QPushButton *> buttons = panel->findChildren<QPushButton *>(
        QStringLiteral("subNavButton"));
    for (QPushButton *b : buttons) {
        if (!b) continue;
        if (b->property("parentIndex").toInt() == parentIndex &&
            b->property("childIndex").toInt() == childIndex)
            return b;
    }
    return nullptr;
}

QStringList subNavTexts(SubNavPanel *panel, int parentIndex)
{
    QStringList out;
    if (!panel) return out;
    const QList<QPushButton *> buttons = panel->findChildren<QPushButton *>(
        QStringLiteral("subNavButton"));
    for (QPushButton *b : buttons) {
        if (b && b->property("parentIndex").toInt() == parentIndex)
            out << b->text();
    }
    return out;
}

QString titleText(QWidget *window)
{
    TopBar *bar = window ? window->findChild<TopBar *>() : nullptr;
    if (!bar) return QString();
    QLabel *label = bar->findChild<QLabel *>(QStringLiteral("titleLabel"));
    return label ? label->text() : QString();
}

QStringList cardTitles(QWidget *window)
{
    QStringList out;
    if (!window) return out;
    const QList<QLabel *> labels = window->findChildren<QLabel *>(
        QStringLiteral("resourcesCardTitle"));
    for (QLabel *l : labels) {
        if (l && l->isVisible()) out << l->text();
    }
    return out;
}

int fail(const char *what)
{
    fprintf(stderr, "PROBE-FAIL: %s\n", what);
    fflush(stderr);
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("BlockBox"));

    const QString outDir = QStringLiteral("D:/BlockBox/_out/settings_plugin_nav_probe");
    QDir().mkpath(outDir);

    ThemeManager *tm = ThemeManager::instance();
    const ThemeManager::ThemeType prevTheme = tm->currentTheme();
    tm->setTheme(&app, ThemeManager::LightTheme);
    pump(nullptr, 100);

    MainWindow w;
    w.resize(1360, 880);
    w.show();
    pump(&w, 2600);

    int failures = 0;
    SideBar *bar = w.findChild<SideBar *>();
    SubNavPanel *panel = w.findChild<SubNavPanel *>();
    SettingsPage *settingsPage = w.findChild<SettingsPage *>();
    PluginPage *pluginPage = w.findChild<PluginPage *>();
    if (!bar || !panel) {
        qCritical() << "PROBE-FAIL: 没找到侧边栏 / 子导航面板";
        return 2;
    }
    grab(&w, outDir + QStringLiteral("/40_home.png"));

    // ---- 1. 进入「设置」，看子导航最后一项 ----
    QPushButton *settingsBtn = navButton(bar, 2);
    if (!settingsBtn) { failures += fail("侧边栏没找到「设置」(index 2)"); return 3; }
    settingsBtn->click();
    pump(&w, 1500);
    grab(&w, outDir + QStringLiteral("/41_settings.png"));

    const QStringList settingsItems = subNavTexts(panel, 2);
    fprintf(stderr, "[probe] 设置子导航 %d 项：%s\n", static_cast<int>(settingsItems.size()),
            settingsItems.join(QStringLiteral(" / ")).toLocal8Bit().constData());
    fflush(stderr);
    if (settingsItems.isEmpty()) {
        failures += fail("设置子导航没有条目");
    } else if (settingsItems.last() != QString::fromUtf8("插件管理")) {
        failures += fail("设置子导航最后一项不是「插件管理」");
    }

    // 子导航序号必须和设置页给出的插件页序号一致（等于设置内容页数量）
    int pluginChildIndex = -1;
    for (int i = 0; i < settingsItems.size(); ++i) {
        if (settingsItems.at(i) == QString::fromUtf8("插件管理")) {
            pluginChildIndex = i;
            break;
        }
    }
    if (pluginChildIndex < 0) {
        failures += fail("设置子导航里找不到「插件管理」");
    } else {
        settingsPage = w.findChild<SettingsPage *>();
        const int expected = settingsPage ? settingsPage->pluginManageTabIndex() : -1;
        fprintf(stderr, "[probe] 插件管理子项序号=%d 设置内容页数=%d\n",
                pluginChildIndex, expected);
        fflush(stderr);
        if (!settingsPage) {
            failures += fail("没找到 SettingsPage");
        } else if (!settingsPage->isPluginManageTab(pluginChildIndex) || pluginChildIndex != expected) {
            failures += fail("插件管理子项序号与 SettingsPage 记录不一致");
        }
    }

    // ---- 2. 点「插件管理」→ 插件整页，复用「设置」导航 ----
    if (pluginChildIndex >= 0) {
        if (QPushButton *item = subNavButton(panel, 2, pluginChildIndex)) {
            item->click();
            pump(&w, 1800);
            grab(&w, outDir + QStringLiteral("/42_plugin_manage.png"));

            pluginPage = w.findChild<PluginPage *>();
            if (!pluginPage || !pluginPage->isVisible())
                failures += fail("点「插件管理」后插件页没有显示");
            if (panel->currentParentIndex() != 2)
                failures += fail("进入插件页后设置子导航被切走了");
            if (!panel->isVisible())
                failures += fail("进入插件页后设置子导航收起了");
            if (QPushButton *nowItem = subNavButton(panel, 2, pluginChildIndex)) {
                if (!nowItem->isChecked())
                    failures += fail("进入插件页后「插件管理」没有高亮");
            } else {
                failures += fail("重新展开设置子导航后找不到「插件管理」按钮");
            }
            const QString title = titleText(&w);
            fprintf(stderr, "[probe] 插件页顶栏标题：%s\n",
                    title.toLocal8Bit().constData());
            fflush(stderr);
            if (!title.contains(QString::fromUtf8("插件管理")))
                failures += fail("插件页顶栏标题里没有「插件管理」");
        } else {
            failures += fail("设置子导航里找不到「插件管理」按钮");
        }
    }

    // ---- 3. 从插件页 / 设置子导航切回「常规设置」 ----
    if (QPushButton *general = subNavButton(panel, 2, 0)) {
        general->click();
        pump(&w, 1400);
        grab(&w, outDir + QStringLiteral("/43_back_to_general.png"));
        pluginPage = w.findChild<PluginPage *>();
        settingsPage = w.findChild<SettingsPage *>();
        if (pluginPage && pluginPage->isVisible())
            failures += fail("切回「常规设置」后插件页还在显示");
        if (!settingsPage || !settingsPage->isVisible())
            failures += fail("切回「常规设置」后设置页没有显示");
        const QString title = titleText(&w);
        if (!title.contains(QString::fromUtf8("常规设置")))
            failures += fail("切回「常规设置」后顶栏标题没更新");
    } else {
        failures += fail("设置子导航里找不到「常规设置」按钮");
    }

    // ---- 4. 「更多」里不再有插件管理（子导航与落地页卡片）----
    if (QPushButton *moreBtn = navButton(bar, 7)) {
        moreBtn->click();
        pump(&w, 1800);
        grab(&w, outDir + QStringLiteral("/44_more_page.png"));
        const QStringList moreItems = subNavTexts(panel, 7);
        fprintf(stderr, "[probe] 更多子导航 %d 项：%s\n", static_cast<int>(moreItems.size()),
                moreItems.join(QStringLiteral(" / ")).toLocal8Bit().constData());
        fflush(stderr);
        if (moreItems.contains(QString::fromUtf8("插件管理")))
            failures += fail("「更多」子导航里还有「插件管理」");
        const QStringList cards = cardTitles(&w);
        if (cards.contains(QString::fromUtf8("插件管理")))
            failures += fail("「更多」落地页里还有「插件管理」卡片");
    } else {
        failures += fail("侧边栏没找到「更多」(index 7)");
    }

    // ---- 5. 深色主题下再看一眼插件页 ----
    tm->setTheme(&app, ThemeManager::DarkTheme);
    pump(&w, 800);
    settingsBtn = navButton(bar, 2);
    if (settingsBtn) {
        settingsBtn->click();
        pump(&w, 1200);
        if (pluginChildIndex >= 0) {
            if (QPushButton *item = subNavButton(panel, 2, pluginChildIndex)) {
                item->click();
                pump(&w, 1500);
                grab(&w, outDir + QStringLiteral("/45_plugin_manage_dark.png"));
            }
        }
    }
    tm->setTheme(&app, prevTheme);
    pump(&w, 400);

    qInfo() << "PROBE-DONE failures:" << failures;
    return failures == 0 ? 0 : 4;
}
