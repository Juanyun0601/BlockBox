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
    const qreal dpr = currentDevicePixelRatio();
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
    const int devSize = qMax(1, qRound(size * dpr));
    QPixmap pixmap(devSize, devSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, devSize, devSize));
    painter.end();

    QPixmapCache::insert(cacheKey, pixmap);

    return QIcon(pixmap);
}

void IconHelper::clearCache()
{
    QPixmapCache::clear();
}
