/**
 * @file   ai_chat_bottom_probe.cpp
 * @brief  AI 助手页底部输入区端到端探针（链接主程序全部目标文件，仅替换 main.cpp）
 *
 * 流程：
 *   1. 启动真实 MainWindow（1280x800），切换背景为必应壁纸缓存（无缓存则生成
 *      一张高对比渐变测试图，走 Image 模式，保证不依赖联网）；
 *   2. 切到 AI 助手页 → 切到"工作"模式（与需求截图一致）；
 *   3. 抓整窗截图 + 底部输入条裁剪，存 probe_out/ai_bottom_before/after_*.png；
 *   4. 恢复用户原有背景设置。
 */

#include "mainwindow.h"
#include "pages/AiChatPage.h"
#include "utils/BackgroundManager.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QLinearGradient>
#include <QMetaObject>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QTimer>

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

// 生成一张高对比渐变 + 斜条纹测试图（模拟壁纸，透明度效果一目了然）
QString makeSimWallpaper(const QString &path)
{
    QImage img(1600, 900, QImage::Format_RGB32);
    QLinearGradient grad(0, 0, 1600, 900);
    grad.setColorAt(0.0, QColor(230, 126, 34));
    grad.setColorAt(0.35, QColor(46, 204, 113));
    grad.setColorAt(0.7, QColor(52, 152, 219));
    grad.setColorAt(1.0, QColor(155, 89, 182));
    QPainter p(&img);
    p.fillRect(img.rect(), grad);
    p.setPen(Qt::NoPen);
    for (int i = -900; i < 1600; i += 120) {
        QPolygon tri;
        tri << QPoint(i, 900) << QPoint(i + 60, 900) << QPoint(i + 300, 0) << QPoint(i + 240, 0);
        p.setBrush(QColor(20, 20, 20, 60));
        p.drawPolygon(tri);
    }
    p.end();
    img.save(path);
    return path;
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);

    // PROBE_DARK=1 时把主题设置临时切到 dark（ThemeManager 启动时从设置读取）
    QSettings settings("BlockBox", "Settings");
    const bool dark = qEnvironmentVariableIntValue("PROBE_DARK") != 0;
    QString savedThemeName;
    if (dark) {
        savedThemeName = settings.value("theme", "light").toString();
        settings.setValue("theme", "dark");
        qInfo().noquote() << QString("  theme forced to dark (saved=%1)").arg(savedThemeName);
    }

    const QString tag = qEnvironmentVariable("PROBE_TAG", "before");
    const QString outDir = QApplication::applicationDirPath();
    QDir().mkpath(outDir);

    // 保存用户原有背景设置，探针结束后恢复
    const int savedMode = settings.value("background/mode", 0).toInt();
    const QString savedBingPath = settings.value("background/bingPath", "").toString();
    const QString savedImagePath = settings.value("background/imagePath", "").toString();

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

    // ── 背景切到壁纸（优先必应缓存，否则用生成的测试图；PROBE_NOBACK=1 则强制经典无壁纸） ──
    BackgroundManager *bg = BackgroundManager::instance();
    const bool noBack = qEnvironmentVariableIntValue("PROBE_NOBACK") != 0;
    QString bing = bg->bingImagePath();
    if (noBack) {
        bg->setMode(BackgroundManager::Classic);
        expect("wallpaper source", true, "forced classic (no wallpaper)");
    } else if (!bing.isEmpty() && QFile::exists(bing)) {
        bg->setMode(BackgroundManager::Bing);
        expect("wallpaper source", true, "bing cache: " + bing);
    } else {
        const QString sim = makeSimWallpaper(outDir + "/sim_wallpaper.png");
        bg->setImagePath(sim);
        bg->setMode(BackgroundManager::Image);
        expect("wallpaper source", true, "simulated: " + sim);
    }
    settle(800);

    // ── 切到 AI 助手页（onParentNavClicked 为私有槽，经元对象系统调用） ──
    QMetaObject::invokeMethod(&w, "onParentNavClicked", Q_ARG(int, 3));
    settle(2500);
    auto *aiPage = w.findChild<AiChatPage *>();
    expect("ai chat page opened", aiPage != nullptr, QString());
    // 诊断：确认应用内 AiChatPage / 欢迎区实例数量（排查"重影"是否为双实例重叠）
    qInfo().noquote() << QString("  diag: AiChatPage count=%1, welcomeWidget count=%2")
        .arg(w.findChildren<AiChatPage *>().size())
        .arg(w.findChildren<QWidget *>("aiChatWelcomeWidget").size());

    // ── 切到工作模式（与需求截图一致：让 AI 助手帮你做事）；PROBE_NOMODE=1 跳过 ──
    if (qEnvironmentVariableIntValue("PROBE_NOMODE") == 0 && aiPage) {
        const QList<QPushButton *> modeBtns = aiPage->findChildren<QPushButton *>("aiChatModeBtn");
        if (modeBtns.size() >= 2) {
            modeBtns.at(1)->click();
        }
        if (auto *edit = aiPage->findChild<QTextEdit *>("aiChatInputEdit")) {
            edit->setPlainText("帮我搭配一个整合包");
        }
    }

    // ── 欢迎区多帧抓拍（诊断文字渲染时序）：工作模式切换后 0/300/800ms ──
    // 对照组：一个无任何 QSS 定位的原生 QLabel（判断红蓝重影是否 QSS 触发）
    auto *plain = new QLabel(QStringLiteral("对照：原生标签 no-style 123"), &w);
    plain->move(380, 210);
    plain->show();
    plain->raise();

    const QRect welcomeCrop(380, 250, 570, 300);
    const int frameDelays[] = {0, 800, 2500, 5000};
    for (int d : frameDelays) {
        if (d > 0) settle(d);
        w.grab().toImage().copy(welcomeCrop)
            .save(outDir + QString("/ai_welcome_%1_f%2.png").arg(tag).arg(d));
        if (d == 0 && aiPage) {
            // 诊断：列出消息区容器当前全部子控件（找"白卡"重影来源）
            if (auto *msgArea = aiPage->findChild<QScrollArea *>("aiChatMessageArea")) {
                if (auto *container = msgArea->widget()) {
                    qInfo().noquote() << QString("  msgContainer: %1 geo=(%2,%3 %4x%5) vis=%6")
                        .arg(container->metaObject()->className())
                        .arg(container->geometry().x()).arg(container->geometry().y())
                        .arg(container->geometry().width()).arg(container->geometry().height())
                        .arg(container->isVisible());
                    for (QWidget *cc : container->findChildren<QWidget *>(QString(),
                                                                          Qt::FindDirectChildrenOnly)) {
                        qInfo().noquote() << QString("  msgChild: %1 %2 geo=(%3,%4 %5x%6) vis=%7")
                            .arg(cc->metaObject()->className(), cc->objectName())
                            .arg(cc->geometry().x()).arg(cc->geometry().y())
                            .arg(cc->geometry().width()).arg(cc->geometry().height())
                            .arg(cc->isVisible());
                    }
                }
            }
            if (auto *wel = aiPage->findChild<QWidget *>("aiChatWelcomeWidget")) {
                if (auto *quick = wel->findChild<QWidget *>("aiChatWelcomeQuickBtns")) {
                    const QList<QPushButton *> btns = quick->findChildren<QPushButton *>();
                    qInfo().noquote() << QString("  quick buttons: %1").arg(btns.size());
                    for (QPushButton *b : btns) {
                        qInfo().noquote() << QString("    btn '%1' geo=(%2,%3 %4x%5) vis=%6 enabled=%7 font=%8/%9 textBrush=%10")
                            .arg(b->text(), QString::number(b->geometry().x()),
                                 QString::number(b->geometry().y()))
                            .arg(b->geometry().width()).arg(b->geometry().height())
                            .arg(b->isVisible()).arg(b->isEnabled())
                            .arg(b->font().family(), QString::number(b->font().pointSizeF()))
                            .arg(b->palette().brush(QPalette::ButtonText).color().name());
                        b->grab().save(outDir + QString("/btn_%1.png")
                                       .arg(b->text().left(4).trimmed()));
                    }
                }
            }
        }
    }
    settle(1200);

    // ── 截图：整窗 + 底部输入条裁剪 ──
    const QImage full = w.grab().toImage();
    const QString fullPath = outDir + QString("/ai_bottom_%1_full.png").arg(tag);
    const QString cropPath = outDir + QString("/ai_bottom_%1_crop.png").arg(tag);
    full.save(fullPath);
    const int cropH = qMin(260, full.height());
    full.copy(0, full.height() - cropH, full.width(), cropH).save(cropPath);
    qInfo().noquote() << QString("  screenshots saved: %1 / %2").arg(fullPath, cropPath);

    // ── 恢复用户设置 ──
    bg->setMode(static_cast<BackgroundManager::BackgroundMode>(savedMode));
    settings.setValue("background/mode", savedMode);
    settings.setValue("background/bingPath", savedBingPath);
    settings.setValue("background/imagePath", savedImagePath);
    if (dark) {
        settings.setValue("theme", savedThemeName.isEmpty() ? QStringLiteral("light")
                                                            : savedThemeName);
    }
    qInfo().noquote() << QString("  restored background/mode = %1").arg(savedMode);

    qInfo() << (failures == 0 ? "PROBE-PASS" : "PROBE-FAIL");
    return failures == 0 ? 0 : 1;
}
