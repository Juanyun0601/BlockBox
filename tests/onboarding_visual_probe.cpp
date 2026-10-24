/**
 * @file   onboarding_visual_probe.cpp
 * @brief  新手引导窗轻量视觉探针：不依赖 MainWindow，直接渲染 OnboardingWizard
 */
#include "pages/OnboardingWizard.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMetaObject>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include <cstdio>

namespace {

void stderrMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx)
    if (msg.contains(QStringLiteral("Could not parse")))
        fprintf(stderr, "[qss-err] %s\n", msg.toLocal8Bit().constData());
    const char *tag = "[info ]";
    if (type == QtWarningMsg) tag = "[warn ]";
    else if (type == QtCriticalMsg || type == QtFatalMsg) tag = "[crit ]";
    fprintf(stderr, "%s %s\n", tag, msg.toLocal8Bit().constData());
    fflush(stderr);
}

bool grab(QWidget *w, const QString &path)
{
    if (!w) return false;
    const bool ok = w->grab().save(path);
    fprintf(stderr, "[%s] %s\n", ok ? "ok" : "FAIL", path.toLocal8Bit().constData());
    fflush(stderr);
    return ok;
}

void settle(QWidget *w, int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (w) w->repaint();
    }
}

void invoke(OnboardingWizard *wiz, const char *slot)
{
    QMetaObject::invokeMethod(wiz, slot, Qt::DirectConnection);
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);

    const QString outDir = QStringLiteral("D:/BlockBox/_out/onboarding_probe");
    QDir().mkpath(outDir);

    // 真实加载主程序样式（ThemeManager 内会解析 style.qss + token）
    ThemeManager::instance()->setTheme(&app, ThemeManager::LightTheme);
    ThemeManager::instance()->setThemeColor(QStringLiteral("#2E7D32"));
    settle(nullptr, 80);

    QWidget host;
    host.setWindowTitle(QStringLiteral("onboarding probe host"));
    host.resize(1280, 800);
    host.show();
    settle(&host, 200);

    // 浅色 · 默认绿
    {
        OnboardingWizard *wiz = new OnboardingWizard(&host);
        wiz->show();
        settle(wiz, 250);
        grab(wiz, outDir + QStringLiteral("/01_lang_light.png"));

        invoke(wiz, "onNextClicked"); settle(wiz, 160);
        grab(wiz, outDir + QStringLiteral("/02_method_light.png"));
        invoke(wiz, "onNextClicked"); settle(wiz, 160);
        grab(wiz, outDir + QStringLiteral("/03_account_light.png"));
        invoke(wiz, "onNextClicked"); settle(wiz, 160);
        grab(wiz, outDir + QStringLiteral("/04_java_light.png"));
        invoke(wiz, "onNextClicked"); settle(wiz, 160);
        grab(wiz, outDir + QStringLiteral("/05_folder_light.png"));
        invoke(wiz, "onNextClicked"); settle(wiz, 160);
        grab(wiz, outDir + QStringLiteral("/06_style_light.png"));
        invoke(wiz, "onNextClicked"); settle(wiz, 220);
        grab(wiz, outDir + QStringLiteral("/07_done_light.png"));

        wiz->close();
        wiz->deleteLater();
        settle(&host, 80);
    }

    // 深色 · 默认绿
    ThemeManager::instance()->setTheme(&app, ThemeManager::DarkTheme);
    settle(&host, 200);
    {
        OnboardingWizard *wiz = new OnboardingWizard(&host);
        wiz->show();
        settle(wiz, 250);
        grab(wiz, outDir + QStringLiteral("/10_lang_dark.png"));
        invoke(wiz, "onNextClicked"); settle(wiz, 120);
        invoke(wiz, "onNextClicked"); settle(wiz, 120);
        invoke(wiz, "onNextClicked"); settle(wiz, 120);
        invoke(wiz, "onNextClicked"); settle(wiz, 120);
        invoke(wiz, "onNextClicked"); settle(wiz, 120);
        invoke(wiz, "onNextClicked"); settle(wiz, 220);
        grab(wiz, outDir + QStringLiteral("/11_done_dark.png"));
        // 样式页（上一步）
        invoke(wiz, "onPrevClicked"); settle(wiz, 200);
        grab(wiz, outDir + QStringLiteral("/14_style_dark.png"));
        wiz->close();
        wiz->deleteLater();
        settle(&host, 80);
    }

    ThemeManager::instance()->setTheme(&app, ThemeManager::LightTheme);
    fprintf(stderr, "PROBE-DONE\n");
    fflush(stderr);
    app.quit();
    return 0;
}
