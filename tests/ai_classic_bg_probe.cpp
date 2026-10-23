/**
 * @file   ai_classic_bg_probe.cpp
 * @brief  AI 助手页主栏背景一致性探针
 *
 * 用法：强制浅色主题 + 经典背景模式，打开 AI 助手页，采样
 *   - 消息主栏（#aiChatMessageArea）区域的主色
 *   - 输入区（#aiChatInputPanel，透明 → 应露出页面底色 @BG_BASE@）上边距的主色
 * 两者应一致（经典模式下主栏不应带自己的底色）。
 *
 * 环境变量：
 *   PROBE_TAG   截图文件名后缀（默认 before）
 *   PROBE_NOBACK（保留兼容，默认就强制经典）
 */

#include "mainwindow.h"

#include "pages/AiChatPage.h"
#include "utils/BackgroundManager.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QScrollArea>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <cstdio>

namespace {

void stderrMessageHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    std::fprintf(stderr, "%s\n", qPrintable(msg));
    std::fflush(stderr);
}

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
}

/// 区域内出现次数最多的颜色（背景色通常占比最高）
QRgb dominantColor(const QImage &img, const QRect &rect)
{
    QHash<QRgb, int> counts;
    QRgb best = 0;
    int bestN = 0;
    const QRect r = rect.intersected(img.rect());
    for (int y = r.top(); y <= r.bottom(); y += 2) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = r.left(); x <= r.right(); x += 2) {
            const QRgb c = line[x];
            const int n = counts[c] + 1;
            counts[c] = n;
            if (n > bestN) {
                bestN = n;
                best = c;
            }
        }
    }
    return best;
}

QString colorName(QRgb c)
{
    return QString("#%1%2%3")
        .arg(qRed(c), 2, 16, QLatin1Char('0'))
        .arg(qGreen(c), 2, 16, QLatin1Char('0'))
        .arg(qBlue(c), 2, 16, QLatin1Char('0'));
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);

    QSettings settings("BlockBox", "Settings");
    const QString savedTheme = settings.value("theme", "light").toString();
    const int savedMode = settings.value("background/mode", 0).toInt();
    const QString savedBingPath = settings.value("background/bingPath", "").toString();
    const QString savedImagePath = settings.value("background/imagePath", "").toString();

    settings.setValue("theme", QStringLiteral("light"));
    ThemeManager::setTheme(&app, QStringLiteral("light"));

    const QString tag = qEnvironmentVariable("PROBE_TAG", "classic_before");
    const QString outDir = QApplication::applicationDirPath();
    QDir().mkpath(outDir);

    MainWindow w;
    w.resize(1280, 800);
    BackgroundManager::instance()->setMode(BackgroundManager::Classic);
    w.show();
    settle(1500);

    int failures = 0;
    auto expect = [&](const char *what, bool ok, const QString &detail) {
        qInfo().noquote() << QString("  [%1] %2  %3")
            .arg(ok ? "PASS" : "FAIL", QString::fromLatin1(what), detail);
        if (!ok)
            ++failures;
    };

    // ── 打开 AI 助手页 ──
    QMetaObject::invokeMethod(&w, "onParentNavClicked", Q_ARG(int, 3));
    settle(2500);
    auto *aiPage = w.findChild<AiChatPage *>();
    expect("ai chat page opened", aiPage != nullptr, QString());
    if (!aiPage) {
        qInfo() << "PROBE-FAIL";
        return 1;
    }

    const QImage full = w.grab().toImage();
    const QString fullPath = outDir + QString("/ai_classic_bg_%1.png").arg(tag);
    full.save(fullPath);
    qInfo().noquote() << QString("  screenshot: %1  (%2x%3, window %4x%5)")
                             .arg(fullPath)
                             .arg(full.width()).arg(full.height())
                             .arg(w.width()).arg(w.height());

    const qreal kx = full.width() > 0 ? qreal(full.width()) / w.width() : 1.0;
    const qreal ky = full.height() > 0 ? qreal(full.height()) / w.height() : 1.0;
    auto toImageRect = [&](QWidget *widget, const QRect &local) {
        const QPoint origin = widget->mapTo(&w, local.topLeft());
        return QRect(int(origin.x() * kx), int(origin.y() * ky),
                     int(local.width() * kx), int(local.height() * ky));
    };

    auto *msgArea = aiPage->findChild<QScrollArea *>("aiChatMessageArea");
    auto *inputPanel = aiPage->findChild<QWidget *>("aiChatInputPanel");
    expect("message area found", msgArea != nullptr, QStringLiteral("#aiChatMessageArea"));
    expect("input panel found", inputPanel != nullptr, QStringLiteral("#aiChatInputPanel"));

    QRect msgRect = msgArea ? toImageRect(msgArea, msgArea->rect()) : QRect();
    // 去掉边框/滚动条附近的少量像素
    const QRect msgInner = msgRect.adjusted(10, 10, -14, -10);
    const QRgb msgColor = dominantColor(full, msgInner);

    // 输入区上边距（10px）是页面底色，框体（玻璃白）从下面开始
    QRect pageRect;
    if (inputPanel) {
        const QRect panelRect = toImageRect(inputPanel, inputPanel->rect());
        pageRect = QRect(panelRect.left() + 10, panelRect.top() + 1,
                         qMax(10, panelRect.width() - 20), 7);
    }
    const QRgb pageColor = dominantColor(full, pageRect);

    // 额外对照：消息主栏正下方、输入区上边的窗口左侧留白（若有）——不取，避免误判
    qInfo().noquote() << QString("  msg area rect   = %1,%2 %3x%4")
                             .arg(msgRect.left()).arg(msgRect.top())
                             .arg(msgRect.width()).arg(msgRect.height());
    qInfo().noquote() << QString("  page ref rect   = %1,%2 %3x%4")
                             .arg(pageRect.left()).arg(pageRect.top())
                             .arg(pageRect.width()).arg(pageRect.height());
    qInfo().noquote() << QString("  main column bg  = %1").arg(colorName(msgColor));
    qInfo().noquote() << QString("  page bg (input) = %1").arg(colorName(pageColor));

    const int diff = qAbs(qRed(msgColor) - qRed(pageColor))
                     + qAbs(qGreen(msgColor) - qGreen(pageColor))
                     + qAbs(qBlue(msgColor) - qBlue(pageColor));
    expect("main column matches page background", diff == 0,
           QString("delta=%1").arg(diff));

    // 恢复设置
    BackgroundManager::instance()->setMode(
        static_cast<BackgroundManager::BackgroundMode>(savedMode));
    settings.setValue("background/mode", savedMode);
    settings.setValue("background/bingPath", savedBingPath);
    settings.setValue("background/imagePath", savedImagePath);
    settings.setValue("theme", savedTheme);

    qInfo() << ((failures == 0) ? "PROBE-PASS" : "PROBE-FAIL");
    return failures == 0 ? 0 : 1;
}
