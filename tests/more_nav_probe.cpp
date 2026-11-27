/**
 * @file   more_nav_probe.cpp
 * @brief  左侧导航「更多」分类集成探针：验证 7 个内置入口 + 插件入口（ui.moreEntries）
 *
 * 链接主程序全部目标文件（仅替换 main.cpp 与被改动的翻译单元），验证：
 *   1. 侧边栏一级项变成 首页/资源/设置/AI助手/社区/发布/更多/反馈（插件已移除）；
 *   2. 「更多」子导航与落地页卡片都是：方块图书馆 / 方块皮肤站 / 种子地图 /
 *      投影编辑器 / 文件传输 / 插件管理 / 新手引导；
 *   3. 临时装一个带 ui.moreEntries 的探针插件后，「更多」子导航变成 9 项、
 *      落地页多出「插件入口」卡片（标题/说明来自插件清单）；
 *   4. 点「种子地图」真的要能出图（等渲染完再截）；
 *   5. 点「文件传输」进独立整页；点「插件管理」进插件页；点「方块图书馆」进图书馆；
 *   6. 顶栏已经没有「文档」按钮（topBarDocsBtn）；
 *   7. 反馈仍能打开、首页能回去。
 *
 * 只读设置与本地文件，不做下载 / 安装 / 登录；探针插件跑完即删。
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QWidget>

#include "components/SideBar.h"
#include "components/SubNavPanel.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/plugin/PluginManager.h"

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
        if (b && b->property("parentIndex").toInt() == parentIndex &&
            b->property("childIndex").toInt() == childIndex)
            return b;
    }
    return nullptr;
}

bool clickCardByTitle(QWidget *page, const QString &title)
{
    if (!page) return false;
    const QList<QLabel *> labels = page->findChildren<QLabel *>(
        QStringLiteral("resourcesCardTitle"));
    for (QLabel *l : labels) {
        if (!l || l->text() != title) continue;
        QWidget *card = l->parentWidget() ? l->parentWidget()->parentWidget() : nullptr;
        if (!card) continue;
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), card->mapToGlobal(QPoint(10, 10)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(card, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(10, 10), card->mapToGlobal(QPoint(10, 10)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(card, &release);
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("BlockBox"));

    const QString outDir = QStringLiteral("D:/BlockBox/_out/more_nav_probe");
    QDir().mkpath(outDir);

    // ---- 准备一个带「更多」页入口的探针插件（模拟真实插件包，跑完删掉） ----
    PluginManager *pm = PluginManager::instance();
    // 先清掉上一次探针可能残留的包，保证每次都是干净的 6 + 2
    {
        const QDir pluginDir(pm->pluginsDir());
        const QStringList leftovers = pluginDir.entryList(
            QStringList() << QStringLiteral("probe_more_entry*.BlockBox"), QDir::Files);
        for (const QString &name : leftovers)
            QFile::remove(pluginDir.absoluteFilePath(name));
        pm->refresh();
    }
    const QString tplDir = QDir::tempPath()
        + QStringLiteral("/BlockBoxMoreEntryProbe/probe_more_entry");
    QDir().mkpath(tplDir);
    // 入口图标：用 currentColor 的 SVG，宿主会按卡片渐变色块重染
    QDir().mkpath(tplDir + QStringLiteral("/assets"));
    {
        QFile icon(tplDir + QStringLiteral("/assets/entry.svg"));
        if (icon.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            icon.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
                       "fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\">"
                       "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"4\"/>"
                       "<path d=\"M8 12h8M12 8v8\"/></svg>");
        }
    }
    {
        QFile manifest(tplDir + QStringLiteral("/plugin.json"));
        if (manifest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            manifest.write(R"json({
  "id": "probe_more_entry",
  "name": "更多页入口探针",
  "version": "1.0.0",
  "author": "Probe",
  "description": "验证插件在更多页添加入口",
  "permissions": ["ui:page"],
  "ui": {
    "moreEntries": [
      {
        "id": "card_url",
        "title": "探针入口",
        "description": "来自插件清单的入口卡片",
        "icon": "assets/entry.svg",
        "color": "#0EA5E9",
        "kind": "url",
        "target": "https://mcblockbox.top/"
      },
      {
        "id": "card_script",
        "title": "探针脚本入口",
        "description": "点开运行包内脚本",
        "kind": "script",
        "target": "scripts/noop.ps1"
      }
    ]
  }
})json");
        }
    }
    QString probePluginPath;
    QString packageError;
    const bool probePluginPacked = pm->packageToBlockBox(tplDir, probePluginPath, &packageError);
    fprintf(stderr, "[probe] 探针插件打包：%s %s\n",
            probePluginPacked ? "成功" : "失败",
            probePluginPacked ? probePluginPath.toLocal8Bit().constData()
                              : packageError.toLocal8Bit().constData());
    fflush(stderr);

    ThemeManager *tm = ThemeManager::instance();
    const ThemeManager::ThemeType prevTheme = tm->currentTheme();
    tm->setTheme(&app, ThemeManager::LightTheme);
    pump(nullptr, 100);

    MainWindow w;
    w.resize(1360, 880);
    w.show();
    pump(&w, 2200);
    grab(&w, outDir + QStringLiteral("/30_home.png"));

    int failures = 0;

    // ---- 侧边栏一级项 ----
    SideBar *bar = w.findChild<SideBar *>();
    SubNavPanel *panel = w.findChild<SubNavPanel *>();
    if (!bar) {
        qCritical() << "PROBE-FAIL: 没找到侧边栏";
        return 2;
    }
    fprintf(stderr, "[probe] 侧边栏一级项 %d 个\n",
            static_cast<int>(bar->parentItems().size()));
    for (int i = 0; i < bar->parentItems().size(); ++i) {
        fprintf(stderr, "[probe] 一级项 %d: %s (隐藏=%d)\n", i,
                bar->parentItems()[i].text.toLocal8Bit().constData(),
                bar->parentItems()[i].hideInSidebar ? 1 : 0);
    }
    fflush(stderr);

    if (!navButton(bar, 7)) {
        qWarning() << "PROBE-WARN: 侧边栏 index 7 不是「更多」";
        ++failures;
    }

    // ---- 顶栏不应该再有「文档」按钮 ----
    QWidget *docsBtn = w.findChild<QWidget *>(QStringLiteral("topBarDocsBtn"));
    fprintf(stderr, "[probe] 顶栏文档按钮存在？%s\n", docsBtn ? "是（应该已删除）" : "否");
    fflush(stderr);
    if (docsBtn)
        ++failures;

    // ---- 点「更多」----
    QPushButton *moreBtn = navButton(bar, 7);
    if (!moreBtn) {
        qCritical() << "PROBE-FAIL: 侧边栏没找到「更多」(index 7)";
        return 3;
    }
    moreBtn->click();
    pump(&w, 2200);
    // 侧边栏/子导航会因插件入口签名变化重建一次，旧按钮是 deleteLater 的，
    // processEvents 不处理 DeferredDelete，这里显式派发，避免数到两套按钮
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    pump(&w, 300);
    grab(&w, outDir + QStringLiteral("/31_more_page.png"));

    // 子导航条目
    const QList<QPushButton *> subButtons = panel
        ? panel->findChildren<QPushButton *>(QStringLiteral("subNavButton"))
        : QList<QPushButton *>();
    QStringList subTexts;
    for (QPushButton *b : subButtons) {
        if (b->property("parentIndex").toInt() != 7) continue;
        subTexts << b->text();
        fprintf(stderr, "[probe] 子导航 %d: %s\n", b->property("childIndex").toInt(),
                b->text().toLocal8Bit().constData());
    }
    fflush(stderr);
    if (subTexts.size() != 9) {
        qWarning() << "PROBE-WARN: 子导航条目数不是 9（7 内置 + 2 插件入口）:" << subTexts.size();
        ++failures;
    }
    if (!subTexts.contains(QString::fromUtf8("文件传输"))) {
        qWarning() << "PROBE-WARN: 左侧「更多」子导航没出现「文件传输」";
        ++failures;
    }
    if (probePluginPacked) {
        if (!subTexts.contains(QString::fromUtf8("探针入口"))
            || !subTexts.contains(QString::fromUtf8("探针脚本入口"))) {
            qWarning() << "PROBE-WARN: 左侧「更多」子导航没出现插件入口";
            ++failures;
        }
        // 落地页卡片：插件入口排在「插件入口」区块，标题来自清单
        QStringList cardTitles;
        const QList<QLabel *> titleLabels = w.findChildren<QLabel *>(
            QStringLiteral("resourcesCardTitle"));
        for (QLabel *l : titleLabels) {
            if (l) cardTitles << l->text();
        }
        fprintf(stderr, "[probe] 落地页卡片：%s\n",
                cardTitles.join(QStringLiteral(" / ")).toLocal8Bit().constData());
        fflush(stderr);
        if (!cardTitles.contains(QString::fromUtf8("探针入口"))
            || !cardTitles.contains(QString::fromUtf8("探针脚本入口"))) {
            qWarning() << "PROBE-WARN: 更多页没渲染出插件入口卡片";
            ++failures;
        }
        if (!w.findChild<QWidget *>(QStringLiteral("resourcesCard"))) {
            qWarning() << "PROBE-WARN: 更多页卡片容器缺失";
            ++failures;
        }
    }

    // ---- 深色主题下的更多页（含插件入口卡片） ----
    tm->setTheme(&app, ThemeManager::DarkTheme);
    pump(&w, 1200);
    grab(&w, outDir + QStringLiteral("/31b_more_page_dark.png"));
    tm->setTheme(&app, ThemeManager::LightTheme);
    pump(&w, 900);

    // ---- 种子地图（index 2）----
    if (QPushButton *child = subNavButton(panel, 7, 2)) {
        child->click();
        pump(&w, 9000); // 渲染 49 块 region，等它画完
        grab(&w, outDir + QStringLiteral("/32_seed_map.png"));
    } else {
        qWarning() << "PROBE-WARN: 没找到子导航「种子地图」";
        ++failures;
    }

    // ---- 种子地图：展开内容弹窗（结构 / 群系 / 矿物 + 坐标跳转）----
    if (QPushButton *expand = w.findChild<QPushButton *>(QStringLiteral("worldMapExpandBtn"))) {
        expand->click();
        pump(&w, 1000);
        grab(&w, outDir + QStringLiteral("/34_seed_popup.png"));

        // 群系列勾第一项 → 地图上应出现高亮叠加层
        const QList<QListWidget *> lists =
            w.findChildren<QListWidget *>(QStringLiteral("worldMapPopupList"));
        QListWidget *biomeList = lists.size() >= 2 ? lists.at(1) : nullptr;
        if (biomeList && biomeList->count() > 0) {
            biomeList->item(0)->setCheckState(Qt::Checked);
            pump(&w, 1200);
            // 再把群系列全选：整张图都该被叠加层染色，便于确认叠加层生效
            if (QWidget *popup = w.findChild<QWidget *>(QStringLiteral("worldMapContentPopup"))) {
                const QList<QPushButton *> allBtns = popup->findChildren<QPushButton *>();
                int seen = 0;
                for (QPushButton *b : allBtns) {
                    if (b->text() != QString::fromUtf8("全选")) continue;
                    if (++seen == 2) { b->click(); break; }
                }
            }
            pump(&w, 2000);
            grab(&w, outDir + QStringLiteral("/35a_seed_all_biomes.png"));
        } else {
            qWarning() << "PROBE-WARN: 群系列表为空";
            ++failures;
        }
        expand->click(); // 收起弹窗
        pump(&w, 900);
        grab(&w, outDir + QStringLiteral("/35_seed_biome_overlay.png"));
    } else {
        qWarning() << "PROBE-WARN: 没找到「展开」按钮";
        ++failures;
    }

    // ---- 深色主题下的种子地图 ----
    tm->setTheme(&app, ThemeManager::DarkTheme);
    pump(&w, 1200);
    grab(&w, outDir + QStringLiteral("/33_seed_map_dark.png"));
    tm->setTheme(&app, prevTheme);
    pump(&w, 600);

    // ---- 落地页卡片点「插件管理」----
    if (QPushButton *nb = navButton(bar, 7)) {
        nb->click();
        pump(&w, 1800);
    }
    if (clickCardByTitle(bar->window(), QString::fromUtf8("插件管理"))) {
        pump(&w, 1500);
        grab(&w, outDir + QStringLiteral("/34_plugin_manage.png"));
    } else {
        qWarning() << "PROBE-WARN: 落地页卡片「插件管理」没点到";
        ++failures;
    }

    // ---- 子导航进「方块图书馆」----
    if (QPushButton *nb = navButton(bar, 7)) {
        nb->click();
        pump(&w, 900);
    }
    if (QPushButton *child = subNavButton(panel, 7, 0)) {
        child->click();
        pump(&w, 1500);
        grab(&w, outDir + QStringLiteral("/35_library.png"));
    }

    // ---- 子导航进「方块皮肤站」----
    if (QPushButton *nb = navButton(bar, 7)) {
        nb->click();
        pump(&w, 900);
    }
    if (QPushButton *child = subNavButton(panel, 7, 1)) {
        child->click();
        pump(&w, 1500);
        grab(&w, outDir + QStringLiteral("/36_skin_station.png"));
    }

    // ---- 子导航进「文件传输」（独立整页，不依赖实例）----
    if (QPushButton *nb = navButton(bar, 7)) {
        nb->click();
        pump(&w, 900);
    }
    if (QPushButton *child = subNavButton(panel, 7, 4)) {
        child->click();
        pump(&w, 1800);
        grab(&w, outDir + QStringLiteral("/36a_lan_transfer.png"));
    } else {
        qWarning() << "PROBE-WARN: 没找到子导航「文件传输」";
        ++failures;
    }

    // ---- 反馈（index 8）仍可打开 ----
    if (QPushButton *fb = navButton(bar, 8)) {
        fb->click();
        pump(&w, 1200);
        grab(&w, outDir + QStringLiteral("/37_feedback.png"));
    } else {
        qWarning() << "PROBE-WARN: 没找到「反馈」(index 8)";
        ++failures;
    }

    // ---- 回首页 ----
    if (QPushButton *home = navButton(bar, 0)) {
        home->click();
        pump(&w, 1200);
        grab(&w, outDir + QStringLiteral("/38_back_home.png"));
    }

    tm->setTheme(&app, prevTheme);

    // ---- 清场：删掉探针插件，别留在探针目录里影响下一次 ----
    if (probePluginPacked) {
        const QDir pluginDir(pm->pluginsDir());
        const QStringList leftovers = pluginDir.entryList(
            QStringList() << QStringLiteral("probe_more_entry*.BlockBox"), QDir::Files);
        for (const QString &name : leftovers) {
            if (!QFile::remove(pluginDir.absoluteFilePath(name))) {
                fprintf(stderr, "[probe] 探针插件删除失败：%s\n",
                        name.toLocal8Bit().constData());
            }
        }
        pm->refresh();
        QDir(tplDir).removeRecursively();
    }

    qInfo() << "PROBE-DONE failures:" << failures;
    return failures == 0 ? 0 : 4;
}
