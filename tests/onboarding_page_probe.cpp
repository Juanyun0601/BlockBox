/**
 * @file   onboarding_page_probe.cpp
 * @brief  新手引导「独立页面」集成探针：在真实 MainWindow 里进入引导页并走完
 *
 * 链接主程序全部目标文件（仅替换 main.cpp），构建真实 MainWindow：
 *   1. 首页截图（基线）；
 *   2. showOnboardingPage(true)：截图确认侧边栏隐藏、顶栏标题为「新手引导」；
 *   3. 点页面里的「跳过引导」→ 完成页截图（「跳过引导」应隐藏）；
 *   4. 点「开始使用方块盒子」→ 应回到首页并恢复侧边栏；
 *   5. showOnboardingPage(false)（模拟设置页入口）再次进入 → 第一步截图。
 *
 * 只读取设置，不改动账户 / Java / 实例文件夹（仅 onboarding/completed 会被置位）。
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPushButton>
#include <QWidget>

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

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("BlockBox"));

    const QString outDir = QStringLiteral("D:/BlockBox/_out/onboarding_page_probe");
    QDir().mkpath(outDir);

    ThemeManager::instance()->setTheme(&app, ThemeManager::LightTheme);
    pump(nullptr, 100);

    MainWindow w;
    w.resize(1280, 800);
    w.show();
    pump(&w, 1800);

    grab(&w, outDir + QStringLiteral("/20_home_before.png"));

    // ---- 进入引导页（首次启动路径）----
    w.showOnboardingPage(true);
    pump(&w, 1200);
    grab(&w, outDir + QStringLiteral("/21_onboarding_page.png"));
    // 等入场动画彻底结束再截一张，用于区分「动画半途」与「样式本身半透明」
    pump(&w, 2500);
    grab(&w, outDir + QStringLiteral("/21b_onboarding_settled.png"));

    QWidget *page = w.findChild<QWidget *>(QStringLiteral("OnboardingWizard"));
    if (!page) {
        qCritical() << "PROBE-FAIL: OnboardingWizard 页面未创建";
        return 2;
    }
    qInfo() << "page size:" << page->size() << " parent:" << page->parentWidget();

    QPushButton *skip = page->findChild<QPushButton *>(QStringLiteral("obwSkipBtn"));
    QPushButton *start = page->findChild<QPushButton *>(QStringLiteral("obwBtnPrimary"));
    QPushButton *doneBack = page->findChild<QPushButton *>(QStringLiteral("obwBtnSecondary"));
    qInfo() << "skipBtn visible:" << (skip && skip->isVisible())
            << " startBtn found:" << (start != nullptr)
            << " doneBack found:" << (doneBack != nullptr);

    // ---- 跳过引导 → 完成页 ----
    if (skip) skip->click();
    pump(&w, 600);
    grab(&w, outDir + QStringLiteral("/22_done_page.png"));
    qInfo() << "完成页：skipBtn 可见？" << (skip && skip->isVisible())
            << " doneBack 可见？" << (doneBack && doneBack->isVisible());

    // ---- 开始使用 → 回首页 ----
    if (start) start->click();
    pump(&w, 900);
    grab(&w, outDir + QStringLiteral("/23_after_finish.png"));

    // ---- 设置入口再次打开（应回到第一步）----
    w.showOnboardingPage(false);
    pump(&w, 1000);
    grab(&w, outDir + QStringLiteral("/24_reopen_from_settings.png"));
    qInfo() << "再次进入：skipBtn 可见？" << (skip && skip->isVisible());

    // ---- 完成页「返回设置」→ 应回到设置页并恢复侧边栏 ----
    if (skip) skip->click();
    pump(&w, 600);
    qInfo() << "本次完成页：doneBack 存在？" << (doneBack != nullptr)
            << " 可见？" << (doneBack && doneBack->isVisible());
    grab(&w, outDir + QStringLiteral("/25_done_from_settings.png"));
    if (doneBack) doneBack->click();
    pump(&w, 1200);
    grab(&w, outDir + QStringLiteral("/26_back_to_settings.png"));

    qInfo() << "PROBE-DONE";
    return 0;
}
