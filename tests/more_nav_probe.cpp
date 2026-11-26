/**
 * @file   more_nav_probe.cpp
 * @brief  左侧导航「更多」分类集成探针：在真实 MainWindow 里点开更多分类并逐个跳转
 *
 * 链接主程序全部目标文件（仅替换 main.cpp 与被改动的翻译单元），验证：
 *   1. 侧边栏多了「更多」一级入口，点开后右侧弹出「社区与联机 / 我的与帮助」分类；
 *   2. 落地页（更多功能页）卡片布局与明暗主题；
 *   3. 子导航逐个点进去的页面：联机大厅 / 皮肤站 / 图书馆 / 年度报告 / 账户管理 / 新手引导；
 *   4. 落地页卡片点击也能跳转（点「图联机大厅」卡片）；
 *   5. 「反馈」调到最后一位后仍然点得开；首页能正常回去。
 *
 * 只读设置与本地文件，不做下载 / 安装 / 登录。
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QWidget>

#include "components/SideBar.h"
#include "components/SubNavPanel.h"
#include "components/TopBar.h"
#include "utils/SettingsManager.h"
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

/** 侧边栏一级按钮：按 parentIndex 属性找（按钮文字在子 QLabel 里，不能按 text 找） */
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

/** 子导航里的分类按钮：按 parentIndex + childIndex 找 */
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

/** 点一张「更多功能」卡片：按卡片里标题文字找（PublishKit::makeChoiceCard 的标题标签） */
bool clickCardByTitle(QWidget *page, const QString &title)
{
    if (!page) return false;
    const QList<QLabel *> labels = page->findChildren<QLabel *>(
        QStringLiteral("resourcesCardTitle"));
    for (QLabel *l : labels) {
        if (!l || l->text() != title) continue;
        QWidget *card = l->parentWidget() ? l->parentWidget()->parentWidget() : nullptr;
        if (!card) continue;
        const QPoint pos(card->width() / 2, card->height() / 2);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), card->mapToGlobal(pos),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(card, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), card->mapToGlobal(pos),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(card, &release);
        return true;
    }
    return false;
}

void dumpSubNav(SubNavPanel *panel)
{
    if (!panel) return;
    const QList<QLabel *> sections = panel->findChildren<QLabel *>(
        QStringLiteral("subNavSectionLabel"));
    for (QLabel *l : sections)
        fprintf(stderr, "[probe] 分类: %s\n", l->text().toLocal8Bit().constData());
    const QList<QPushButton *> buttons = panel->findChildren<QPushButton *>(
        QStringLiteral("subNavButton"));
    for (QPushButton *b : buttons)
        fprintf(stderr, "[probe] 条目 %d/%d: %s\n", b->property("parentIndex").toInt(),
                b->property("childIndex").toInt(), b->text().toLocal8Bit().constData());
    fflush(stderr);
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

    ThemeManager::instance()->setTheme(&app, ThemeManager::LightTheme);
    pump(nullptr, 100);

    MainWindow w;
    w.resize(1280, 860);
    w.show();
    pump(&w, 2200);
    grab(&w, outDir + QStringLiteral("/10_home.png"));

    SideBar *bar = w.findChild<SideBar *>();
    SubNavPanel *panel = w.findChild<SubNavPanel *>();
    if (!bar) {
        qCritical() << "PROBE-FAIL: 没找到侧边栏";
        return 2;
    }
    fprintf(stderr, "[probe] 侧边栏一级项 %d 个\n",
            static_cast<int>(bar->parentItems().size()));
    for (int i = 0; i < bar->parentItems().size(); ++i) {
        fprintf(stderr, "[probe] 一级项 %d: %s (隐藏侧边栏=%d)\n", i,
                bar->parentItems()[i].text.toLocal8Bit().constData(),
                bar->parentItems()[i].hideInSidebar ? 1 : 0);
    }
    fflush(stderr);

    // ---- 点「更多」----
    QPushButton *moreBtn = navButton(bar, 8);
    if (!moreBtn) {
        qCritical() << "PROBE-FAIL: 侧边栏没找到「更多」(index 8)";
        return 3;
    }
    moreBtn->click();
    pump(&w, 900);
    grab(&w, outDir + QStringLiteral("/11_more_page.png"));
    pump(&w, 1400);
    grab(&w, outDir + QStringLiteral("/11b_more_page_settled.png"));
    dumpSubNav(panel);

    // ---- 落地页卡片点「联机大厅」----
    if (clickCardByTitle(bar->window(), QString::fromUtf8("联机大厅"))) {
        pump(&w, 1200);
        grab(&w, outDir + QStringLiteral("/12_lobby_from_card.png"));
    } else {
        qWarning() << "PROBE-WARN: 落地页卡片「联机大厅」没点到";
    }

    // ---- 子导航逐个进 ----
    struct Step { int child; const char *file; };
    const Step steps[] = {
        { 0, "/13_lobby_from_subnav.png" },
        { 1, "/14_skin_station.png" },
        { 2, "/15_library.png" },
        { 3, "/16_task_center.png" },
        { 4, "/17_annual_report.png" },
        { 5, "/18_account.png" },
        { 6, "/19_onboarding.png" },
    };
    int failures = 0;
    for (const Step &step : steps) {
        QPushButton *nb = navButton(bar, 8);
        if (!nb) { ++failures; break; }
        nb->click();
        pump(&w, 700);
        QPushButton *child = subNavButton(panel, 8, step.child);
        if (!child) {
            qWarning() << "PROBE-WARN: 子导航索引没找到:" << step.child;
            ++failures;
            continue;
        }
        child->click();
        pump(&w, 1200);
        grab(&w, outDir + QString::fromUtf8(step.file));
    }

    // ---- 反馈仍可打开（索引已后移一位）----
    QPushButton *fbBtn = navButton(bar, 9);
    if (fbBtn) {
        fbBtn->click();
        pump(&w, 900);
        grab(&w, outDir + QStringLiteral("/20_feedback.png"));
    } else {
        qWarning() << "PROBE-WARN: 侧边栏没找到「反馈」(index 9)";
        ++failures;
    }

    // ---- 回首页 ----
    QPushButton *homeBtn = navButton(bar, 0);
    if (homeBtn) {
        homeBtn->click();
        pump(&w, 900);
        grab(&w, outDir + QStringLiteral("/21_back_home.png"));
    }

    // ---- 深色主题下的「更多」落地页 ----
    ThemeManager *tm = ThemeManager::instance();
    const ThemeManager::ThemeType prevTheme = tm->currentTheme();
    tm->setTheme(&app, ThemeManager::DarkTheme);
    pump(&w, 600);
    if (QPushButton *nb = navButton(bar, 8)) {
        nb->click();
        pump(&w, 900);
        grab(&w, outDir + QStringLiteral("/22_more_dark.png"));
    }
    tm->setTheme(&app, prevTheme);
    pump(&w, 400);

    // ---- 隐藏「首页」后索引仍稳定（更多 / 反馈 不应前移）----
    SettingsManager *sm = SettingsManager::instance();
    const bool origHomeVisible = sm->isHomeButtonVisible();
    sm->setHomeButtonVisible(false);
    bar->refreshNavStructure();
    pump(&w, 500);

    const QList<QPushButton *> visibleNav = bar->findChildren<QPushButton *>(
        QStringLiteral("parentNavButton"));
    for (QPushButton *b : visibleNav) {
        QLabel *textLabel = b->findChild<QLabel *>(QStringLiteral("parentNavTextLabel"));
        fprintf(stderr, "[probe] 隐藏首页后可见项 index=%d text=%s\n",
                b->property("parentIndex").toInt(),
                textLabel ? textLabel->text().toLocal8Bit().constData() : "?");
        if (b->property("parentIndex").toInt() == 8 && textLabel &&
            textLabel->text() == QString::fromUtf8("更多")) {
            b->click();
            pump(&w, 900);
            grab(&w, outDir + QStringLiteral("/23_more_with_home_hidden.png"));
        }
    }
    fflush(stderr);

    sm->setHomeButtonVisible(origHomeVisible);
    bar->refreshNavStructure();
    pump(&w, 400);

    qInfo() << "PROBE-DONE failures:" << failures;
    return failures == 0 ? 0 : 4;
}
