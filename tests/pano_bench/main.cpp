/**
 * @file   main.cpp
 * @brief  旋转背景渲染开销基准：量出「CPU 出图」和「缩放铺满」各占多少毫秒
 *
 * 目的：用户反馈改完 CPU 方案后整个界面变卡，需要数据判断是出图贵、
 * 还是把出好的小图放大铺到窗口贵，以及降分辨率能省多少。
 */

#include <QGuiApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QTransform>
#include <QDebug>
#include <QSize>

#include "components/PanoramaSkyRenderer.h"

namespace {

double benchRender(PanoramaSkyRenderer &renderer, const QSize &size, int rounds)
{
    // 预热一帧，避免把首次分配算进去
    renderer.render(size);

    double totalMs = 0.0;
    int counted = 0;
    for (int i = 0; i < rounds; ++i) {
        renderer.advance(33);
        QElapsedTimer timer;
        timer.start();
        const QImage &frame = renderer.render(size);
        totalMs += double(timer.nsecsElapsed()) / 1e6;
        if (!frame.isNull())
            ++counted;
    }
    return totalMs / double(qMax(1, counted));
}

double benchBlit(const QImage &source, const QSize &target, bool smooth, int rounds)
{
    QImage canvas(target, QImage::Format_RGB32);
    canvas.fill(Qt::black);

    double totalMs = 0.0;
    for (int i = 0; i < rounds; ++i) {
        QElapsedTimer timer;
        timer.start();
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
        painter.drawImage(QRect(QPoint(0, 0), target), source);
        painter.end();
        totalMs += double(timer.nsecsElapsed()) / 1e6;
    }
    return totalMs / double(qMax(1, rounds));
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    PanoramaSkyRenderer renderer;
    renderer.setScene(QStringLiteral("menu_classic"));
    renderer.setRotationSpeedScale(0.37);

    struct Case { const char *name; QSize size; };
    const Case cases[] = {
        { "渲染 1200x800（默认限幅，1200x800 窗口）", QSize(1200, 800) },
        { "渲染 1920x1080（最大化窗口）",             QSize(1920, 1080) },
        { "渲染 1600x900",                            QSize(1600, 900) },
        { "渲染 1280x720",                            QSize(1280, 720) },
        { "渲染 960x540",                             QSize(960, 540) },
        { "渲染 800x450",                             QSize(800, 450) },
        { "渲染 640x360",                             QSize(640, 360) },
    };

    qInfo().noquote() << "=== 出图耗时（每帧） ===";
    QVector<QImage> frames;
    for (const Case &c : cases) {
        const double ms = benchRender(renderer, c.size, 12);
        const QImage frame = renderer.render(c.size);
        frames.append(frame);
        qInfo().noquote() << QStringLiteral("%1: %2 ms  实际出图 %3x%4")
                                 .arg(QString::fromUtf8(c.name))
                                 .arg(ms, 0, 'f', 2)
                                 .arg(frame.width())
                                 .arg(frame.height());
    }

    qInfo().noquote() << "";
    qInfo().noquote() << "=== 放大铺满耗时（1920x1080 目标） ===";
    const QSize full(1920, 1080);
    for (int i = 0; i < frames.size(); ++i) {
        const QImage &frame = frames.at(i);
        if (frame.isNull())
            continue;
        const double smooth = benchBlit(frame, full, true, 12);
        const double fast = benchBlit(frame, full, false, 12);
        qInfo().noquote() << QStringLiteral("源 %1x%2 → 1920x1080：平滑 %3 ms / 最近邻 %4 ms")
                                 .arg(frame.width())
                                 .arg(frame.height())
                                 .arg(smooth, 0, 'f', 2)
                                 .arg(fast, 0, 'f', 2);
    }

    return 0;
}
