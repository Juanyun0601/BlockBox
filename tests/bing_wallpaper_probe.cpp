/**
 * @file   bing_wallpaper_probe.cpp
 * @brief  必应壁纸功能端到端探针：切换背景模式 → 等待联网拉取 → 验证生效并截图
 *
 * 链接主程序全部目标文件（仅替换 main.cpp）。流程：
 *   1. 启动真实 MainWindow（1280x800）；
 *   2. 将背景模式切到 Bing，触发必应壁纸管理器联网拉取列表并下载今日壁纸；
 *   3. 轮询等待 bingImagePath 落盘（最长 45 秒）；
 *   4. 校验缓存文件存在、壁纸元数据齐全，抓取整窗截图存档；
 *   5. 恢复用户原有背景模式，输出 PASS/FAIL 结论。
 */

#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QTimer>

#include "utils/BackgroundManager.h"
#include "utils/BingWallpaperManager.h"

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

void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);

    // 保存用户原有的背景设置，探针结束后恢复
    QSettings settings("BlockBox", "Settings");
    const int savedMode = settings.value("background/mode", 0).toInt();
    const QString savedBingPath = settings.value("background/bingPath", "").toString();

    MainWindow w;
    w.resize(1280, 800);
    w.show();
    settle(1500);

    int failures = 0;
    auto expect = [&](const char *what, bool ok, const QString &detail) {
        qInfo().noquote() << QString("  [%1] %2  %3")
            .arg(ok ? "PASS" : "FAIL", QString::fromLatin1(what), detail);
        if (!ok) ++failures;
    };

    // ── 切换到必应壁纸模式 ──
    qInfo() << "== A. switch background mode to Bing ==";
    BackgroundManager *bg = BackgroundManager::instance();
    bg->setMode(BackgroundManager::Bing);
    settle(500);
    expect("mode applied", bg->currentMode() == BackgroundManager::Bing,
           QString("mode=%1").arg(static_cast<int>(bg->currentMode())));

    // ── 等待壁纸联网下载（最长 45 秒）──
    qInfo() << "== B. wait for wallpaper download ==";
    QElapsedTimer timer;
    timer.start();
    QString imagePath;
    while (timer.elapsed() < 45000) {
        settle(500);
        imagePath = bg->bingImagePath();
        if (!imagePath.isEmpty() && QFile::exists(imagePath))
            break;
        QApplication::processEvents();
    }
    expect("wallpaper cached locally", !imagePath.isEmpty() && QFile::exists(imagePath),
           QString("path=%1 waited=%2ms").arg(imagePath).arg(timer.elapsed()));
    expect("backgroundChanged propagated",
           bg->currentMode() == BackgroundManager::Bing && !imagePath.isEmpty(),
           QString("bingPath=%1").arg(imagePath));

    // ── 壁纸元数据 ──
    BingWallpaperManager *mgr = BingWallpaperManager::instance();
    const QList<BingWallpaperManager::WallpaperInfo> list = mgr->wallpapers();
    expect("wallpaper list fetched", !list.isEmpty(),
           QString("count=%1").arg(list.size()));
    if (!list.isEmpty() && mgr->currentIndex() >= 0
        && mgr->currentIndex() < list.size()) {
        const auto &info = list.at(mgr->currentIndex());
        expect("metadata present", !info.title.isEmpty() && !info.copyright.isEmpty(),
               QString("title=%1 date=%2").arg(info.title, info.date));
    }

    // ── 截图存档 ──
    const QImage shot = w.grab().toImage();
    const QString shotPath = QApplication::applicationDirPath() + "/bing_probe_shot.png";
    shot.save(shotPath);
    qInfo().noquote() << QString("  screenshot saved: %1 (%2x%3)")
        .arg(shotPath).arg(shot.width()).arg(shot.height());

    // ── 恢复用户设置 ──
    bg->setMode(static_cast<BackgroundManager::BackgroundMode>(savedMode));
    if (savedBingPath.isEmpty()) {
        settings.setValue("background/bingPath", savedBingPath);
    }
    settings.setValue("background/mode", savedMode);
    qInfo().noquote() << QString("  restored background/mode = %1").arg(savedMode);

    qInfo() << (failures == 0 ? "PROBE-PASS" : "PROBE-FAIL");
    return failures == 0 ? 0 : 1;
}
