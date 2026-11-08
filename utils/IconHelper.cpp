#include "IconHelper.h"

#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QScreen>
#include <QSvgRenderer>

namespace {

// 按当前屏幕缩放比渲染，避免高 DPI 下 1x 位图被拉大发虚
qreal currentDevicePixelRatio()
{
    if (QScreen *screen = QGuiApplication::primaryScreen())
        return screen->devicePixelRatio();
    return 1.0;
}

} // namespace

QIcon IconHelper::loadColoredIcon(const QString &svgPath, const QColor &color, int size)
{
    // 超采样渲染：低分数 DPR（移动端 0.6 全局缩放后 DPR 变为小数）下直接按
    // size*dpr 渲染，SVG 细描边会被缩到不足一个像素，图标发虚成一片淡斑。
    // 这里先按不低于 2 倍渲染出高分辨率位图，再平滑降采样回 size*dpr 的
    // 最终位图（DPR 契约与旧实现一致，绘制路径行为不变）：
    // 细线经过降采样后保持足够浓度，任何缩放比下都清晰可辨。
    const qreal dpr = currentDevicePixelRatio();
    const qreal renderScale = qMax(dpr, 2.0);
    QString cacheKey = QString("ih:%1:%2:%3:%4")
                           .arg(svgPath, color.name())
                           .arg(size)
                           .arg(dpr);

    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        return QIcon(cached);
    }

    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly))
        return QIcon(svgPath);

    QString svgData = QString::fromUtf8(file.readAll());
    file.close();

    svgData.replace("currentColor", color.name());

    QSvgRenderer renderer(svgData.toUtf8());
    const int renderSize = qMax(1, qRound(size * renderScale));
    QPixmap hiRes(renderSize, renderSize);
    hiRes.fill(Qt::transparent);
    QPainter hiPainter(&hiRes);
    hiPainter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&hiPainter, QRectF(0, 0, renderSize, renderSize));
    hiPainter.end();

    const int devSize = qMax(1, qRound(size * dpr));
    QImage downsampled = hiRes.toImage()
                             .scaled(devSize, devSize, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
    QPixmap pixmap = QPixmap::fromImage(downsampled);
    pixmap.setDevicePixelRatio(dpr);

    QPixmapCache::insert(cacheKey, pixmap);

    return QIcon(pixmap);
}

void IconHelper::clearCache()
{
    QPixmapCache::clear();
}
