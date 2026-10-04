/**
 * @file   main.cpp
 * @brief  Application entry point
 * @author BlockBox Team
 * @date   2026-05-09
 */

#include "mainwindow.h"

#include <atomic>

#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QDir>
#include <QFuture>
#include <QFutureWatcher>
#include <QProcess>
#include <QResource>
#include <QSettings>
#include <QSurfaceFormat>
#include <QtConcurrent>
#include <QTimer>
#include <QTranslator>

#include "platform.h"
#include "pages/OnboardingWizard.h"
#include "utils/AsyncInitializer.h"
#include "utils/GameLauncher.h"
#include "utils/LanguageManager.h"
#include "utils/LowConfigMode.h"
#include "utils/PerformanceMonitor.h"
#include "utils/ResourceManager.h"
#include "utils/StartupProgressWidget.h"
#include "utils/ThemeManager.h"

int main(int argc, char *argv[])
{
    // 设置 OpenGL 3.3 Core Profile 表面格式（必须在 QApplication 之前）
    QSurfaceFormat glFormat;
    glFormat.setVersion(3, 3);
    glFormat.setProfile(QSurfaceFormat::CoreProfile);
    glFormat.setDepthBufferSize(24);
    glFormat.setStencilBufferSize(0);
    glFormat.setSamples(0);
    QSurfaceFormat::setDefaultFormat(glFormat);

    QApplication a(argc, argv);

    // 设置应用程序图标
    a.setWindowIcon(QIcon(":/Images/icon.ico"));

    // 初始化低配置模式检测
    LowConfigMode* lowConfigMode = LowConfigMode::instance();

    // 显示启动进度窗口
    StartupProgressWidget* startupWidget = StartupProgressWidget::instance();

    // 根据低配置模式设置动画启用状态
    startupWidget->setAnimationsEnabled(lowConfigMode->areAnimationsEnabled());

    startupWidget->showStartup();

    // 开始测量应用启动时间
    PerformanceMonitor::instance()->startMeasurement("Application Startup");

    // 首次运行判定：以持久化的 onboarding/completed 标记为准（而非数据文件夹是否存在）。
    // 数据文件夹由 getDataDirectory() 在启动早期创建，不能作为首次判据——
    // 否则引导一旦被中断（语言切换重启 / 用户关闭 / 崩溃），下次启动文件夹已存在、引导就再也不出现。
    // 只有用户走完引导到达完成页（OnboardingWizard::goDone，含跳过）才会置该标记为 true；
    // 语言切换会重启且不置标记，故重启后引导会再次弹出以继续。
    const bool isFirstRun = !QSettings("BlockBox", "BlockBox")
                                .value("onboarding/completed", false).toBool();

    // 初始化应用数据目录，确保BlockBox和.minecraft文件夹被创建
    startupWidget->updateProgress(10, QCoreApplication::translate("MainWindow", "正在初始化数据目录..."));
    PerformanceMonitor::instance()->startMeasurement("Data Directory Initialization");
    Platform::getDataDirectory();
    PerformanceMonitor::instance()->endMeasurement("Data Directory Initialization");

    // 同步加载语言设置（在创建任何 Widget 之前，确保 tr() 使用正确的翻译）
    startupWidget->updateProgress(15, QCoreApplication::translate("MainWindow", "正在加载语言设置..."));
    LanguageManager::instance()->loadLanguageSync();
    qDebug() << "[App]" << "LanguageManager initialized synchronously";

    // 初始化主题管理器，在创建任何Widget之前应用QSS样式表
    startupWidget->updateProgress(18, QCoreApplication::translate("MainWindow", "正在加载主题样式..."));
    ThemeManager::instance();
    qDebug() << "[App]" << "ThemeManager initialized and stylesheet applied";

    // 检查资源文件是否正确加载
    qDebug() << "[App]" << "Resource paths:" << QDir::searchPaths(":");
    QDir resourceDir(":/translations");
    qDebug() << "[App]" << "Translation files in resource:" << resourceDir.entryList();
    
    // 创建主窗口（但不显示，等待异步初始化完成）
    startupWidget->updateProgress(20, QCoreApplication::translate("MainWindow", "正在创建主窗口..."));
    PerformanceMonitor::instance()->startMeasurement("Main Window Initialization");
    MainWindow w;
    PerformanceMonitor::instance()->endMeasurement("Main Window Initialization");

    // 初始化Java缓存（首次启动全盘扫描，后续从缓存加载）
    startupWidget->updateProgress(25, QCoreApplication::translate("MainWindow", "正在检测Java..."));
    PerformanceMonitor::instance()->startMeasurement("Java Cache Initialization");
    GameLauncher::instance()->initJavaCache();
    PerformanceMonitor::instance()->endMeasurement("Java Cache Initialization");
    qDebug() << "[App]" << "Java cache initialization completed";



    // 创建异步初始化器
    AsyncInitializer initializer;

    // 连接异步初始化完成信号
    QObject::connect(&initializer, &AsyncInitializer::allInitializationFinished, [&]() {
        PerformanceMonitor::instance()->endMeasurement("Async Initialization");
        qDebug() << "[App]" << "Showing main window after async initialization";
        
        // 启动完成
        startupWidget->updateProgress(100, QCoreApplication::translate("MainWindow", "启动完成"));
        
        // 短暂延迟后隐藏启动窗口并显示主窗口
        QTimer::singleShot(300, [&w, startupWidget, isFirstRun]() {
            startupWidget->hideStartup();
            w.show();

            // 首次运行（未检测到 BlockBox 数据文件夹）：弹出新手引导
            if (isFirstRun) {
                QTimer::singleShot(400, [&w]() {
                    OnboardingWizard wizard(&w);
                    wizard.exec();
                    // 语言切换会请求重启：启动新进程并退出当前进程，
                    // 新进程启动时 loadLanguageSync() 干净加载所选语言，且因 onboarding 未完成会再次弹出引导。
                    if (wizard.restartRequested()) {
                        w.hide();
                        QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList());
                        QCoreApplication::quit();
                    }
                });
            }
        });

        // 结束测量应用启动时间
        PerformanceMonitor::instance()->endMeasurement("Application Startup");
    });

    // 启动异步初始化
    PerformanceMonitor::instance()->startMeasurement("Async Initialization");
    initializer.startAsyncInitialization();

    return a.exec();
}
