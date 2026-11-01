/**
 * @file   world_map_3d_probe.cpp
 * @brief  存档地图立体视图（WorldMap3DView）探针
 *
 * 不依赖真实存档：现场合成一张带山脉 / 河谷 / 海岸线的地表图与高度图，
 * 直接把 WorldMap3DView 渲染出来存 PNG，肉眼验收立体感、光照与取景；
 * 同时断言网格真的建起来了（四边形数、耗时、高度数据）。
 *
 * 输出：D:/BlockBox/_out/wm3d_*.png
 */
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>
#include <cstdio>

#include "components/WorldMap3DView.h"
#include "utils/WorldMap/WorldMapRenderer.h"

static int g_fail = 0;

static void expect(bool ok, const char *what)
{
    std::printf("%s  %s\n", ok ? "[ ok ]" : "[FAIL]", what);
    if (!ok)
        ++g_fail;
}

/** 合成地形：起伏 + 山脉 + 河谷 + 海岸，颜色按高度分层 */
static void buildSyntheticMap(int size, QImage *color, QImage *height)
{
    *color = QImage(size, size, QImage::Format_ARGB32);
    color->fill(qRgba(0, 0, 0, 0));
    *height = QImage(size, size, QImage::Format_Grayscale16);
    height->fill(0x8000);

    const double sea = 63.0;
    for (int y = 0; y < size; ++y)
    {
        QRgb *c = reinterpret_cast<QRgb *>(color->scanLine(y));
        quint16 *h = reinterpret_cast<quint16 *>(height->scanLine(y));
        for (int x = 0; x < size; ++x)
        {
            double e = 66.0
                + 16.0 * std::sin(x / 78.0) * std::cos(y / 61.0)
                + 9.0 * std::sin((x + y) / 37.0)
                + 5.0 * std::sin((x * 2 - y) / 23.0);
            // 两座山：高斯隆起
            const double mx1 = 0.36 * size, my1 = 0.30 * size;
            const double mx2 = 0.70 * size, my2 = 0.62 * size;
            e += 86.0 * std::exp(-((x - mx1) * (x - mx1)
                                   + (y - my1) * (y - my1)) / (2 * 62.0 * 62.0));
            e += 64.0 * std::exp(-((x - mx2) * (x - mx2)
                                   + (y - my2) * (y - my2)) / (2 * 78.0 * 78.0));
            // 河谷：一条正弦河道压下去
            const double riverZ = 0.55 * size + 0.16 * size * std::sin(x / 90.0);
            const double dz = std::abs(y - riverZ);
            if (dz < 26.0)
                e -= (1.0 - dz / 26.0) * 26.0;

            int hh = int(std::lround(e));
            // 边缘渐低：做成一块浮空的地形岛
            const double edge = std::min(std::min(x, size - 1 - x),
                                         std::min(y, size - 1 - y));
            if (edge < 24.0)
                hh = int(std::lround(hh - (24.0 - edge) * 2.2));

            if (hh < int(sea))
            {
                const double depth = std::clamp((sea - hh) / 22.0, 0.0, 1.0);
                c[x] = qRgba(int(56 - 20 * depth), int(122 - 60 * depth),
                             int(196 - 70 * depth), 255);
                hh = int(sea); // 水面高度
            }
            else if (hh < int(sea) + 2)
                c[x] = qRgba(226, 214, 168, 255); // 沙滩
            else if (hh < 108)
                c[x] = qRgba(96, 156, 82, 255);   // 草地
            else if (hh < 134)
                c[x] = qRgba(130, 140, 108, 255); // 高地
            else if (hh < 152)
                c[x] = qRgba(146, 138, 128, 255); // 岩
            else
                c[x] = qRgba(242, 244, 248, 255); // 雪
            h[x] = quint16(qint16(hh));
        }
    }
}

static void shoot(WorldMap3DView &view, const QString &path)
{
    QApplication::processEvents();
    const QImage shot = view.grab().toImage();
    shot.save(path);
    std::printf("  -> %s  (%dx%d, quads=%d, %d ms)\n",
                qPrintable(QFileInfo(path).fileName()), shot.width(),
                shot.height(), view.lastQuadCount(), view.lastFrameMs());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    QImage color, height;
    buildSyntheticMap(560, &color, &height);

    // 戳两块"未生成区域"（高度图打上无地表标记、颜色挖成透明）：
    // 西边一个贴地图边界的豁口、中部一个孤立洞 —— 验证裙边墙与深色补面
    auto carve = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y < y1; ++y)
        {
            quint16 *h = reinterpret_cast<quint16 *>(height.scanLine(y));
            QRgb *c = reinterpret_cast<QRgb *>(color.scanLine(y));
            for (int x = x0; x < x1; ++x)
            {
                h[x] = WorldMapRenderer::kNoHeightEncoded;
                c[x] = qRgba(0, 0, 0, 0);
            }
        }
    };
    carve(0, 200, 44, 380);
    carve(430, 110, 474, 154);

    WorldMap3DView view;
    view.resize(1080, 720);
    view.show();

    view.setSurface(&color, &height, 0, 0, 1);
    view.fitToWorld();

    expect(view.hasSurface(), "surface bound");
    expect(view.hasHeightData(), "height data bound");

    shoot(view, QStringLiteral("D:/BlockBox/_out/wm3d_default.png"));
    expect(view.lastQuadCount() > 1000, "mesh has quads");
    expect(view.lastFrameMs() < 120, "first frame under 120ms");

    // 正俯视附近：验证俯角上限与取景
    view.orbitBy(0.0, 40.0);
    shoot(view, QStringLiteral("D:/BlockBox/_out/wm3d_top.png"));

    // 低角度：山脊轮廓
    view.fitToWorld();
    view.orbitBy(0.0, -30.0);
    shoot(view, QStringLiteral("D:/BlockBox/_out/wm3d_low.png"));

    // 水面近景：俯角压低斜望河道，天光反射 / 岸线泡沫 / 波光最明显
    view.fitToWorld();
    view.orbitBy(-15.0, -18.0);
    view.centerOnBlock(int(0.42 * 560), int(0.63 * 560));
    view.zoomBy(0.45);
    shoot(view, QStringLiteral("D:/BlockBox/_out/wm3d_water.png"));

    // 拉近 + 高起伏
    view.fitToWorld();
    view.setReliefScale(2.6);
    view.zoomBy(0.42);
    shoot(view, QStringLiteral("D:/BlockBox/_out/wm3d_close_relief.png"));

    // 转半圈：背面光照
    view.orbitBy(180.0, 0.0);
    shoot(view, QStringLiteral("D:/BlockBox/_out/wm3d_back.png"));

    // 无高度图：只能按平地画，也不该崩
    WorldMap3DView flat;
    flat.resize(600, 400);
    flat.show();
    flat.setSurface(&color, nullptr, 0, 0, 1);
    QApplication::processEvents();
    flat.grab();
    expect(flat.lastQuadCount() > 500, "flat fallback still draws");
    expect(!flat.hasHeightData(), "flat fallback reports no height");

    // 性能：模拟真实拖拽（走低精度快速路径）转 12 帧，报平均耗时
    view.fitToWorld();
    QApplication::processEvents();
    QElapsedTimer t;
    t.start();
    int frames = 0;
    for (int i = 0; i < 12; ++i)
    {
        const QPoint from = view.rect().center();
        const QPoint to = from + QPoint(14, 0);
        QMouseEvent press(QEvent::MouseButtonPress, from, view.mapToGlobal(from),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &press);
        QMouseEvent move(QEvent::MouseMove, to, view.mapToGlobal(to),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &move);
        QApplication::processEvents(); // 按真实节奏让事件循环走一拍
        view.grab();
        QMouseEvent release(QEvent::MouseButtonRelease, to,
                            view.mapToGlobal(to), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(&view, &release);
        ++frames;
    }
    const double avg = double(t.elapsed()) / std::max(1, frames);
    std::printf("  avg frame: %.1f ms (%d quads)\n", avg, view.lastQuadCount());
    expect(avg < 60.0, "interactive frame under 60ms");

    std::printf(g_fail ? "\n%d FAILED\n" : "\nall checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
