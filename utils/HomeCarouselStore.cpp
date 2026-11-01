/**
 * @file   HomeCarouselStore.cpp
 * @brief  首页轮播图自定义图片仓库实现
 */
#include "utils/HomeCarouselStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QStandardPaths>
#include <QDebug>

#include "utils/SettingsManager.h"

namespace {
const char *const kCarouselImagesKey = "home/carouselImages";
const char *const kCarouselImageDirName = "home_carousel";
constexpr int kCarouselImageMaxSide = 2560;
} // namespace

QString HomeCarouselStore::imageDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QLatin1Char('/') + QLatin1String(kCarouselImageDirName);
}

QString HomeCarouselStore::imageFileFilter()
{
    return tr("图片 (*.png *.jpg *.jpeg *.webp *.bmp)");
}

QStringList HomeCarouselStore::savedImages()
{
    return SettingsManager::instance()
        ->getProperty(QString::fromLatin1(kCarouselImagesKey)).toStringList();
}

void HomeCarouselStore::saveImages(const QStringList &paths)
{
    SettingsManager::instance()->setProperty(QString::fromLatin1(kCarouselImagesKey), paths);
}

QStringList HomeCarouselStore::existingImages(const QStringList &paths)
{
    QStringList result;
    for (const QString &path : paths)
    {
        const bool isResource = path.startsWith(QLatin1Char(':'));
        if (!isResource && !QFileInfo::exists(path))
            continue;
        // 只读文件头判断能否解码，不解码像素：轮播图为数 MB 的大图，
        // 启动时逐张完整解码要多花几百毫秒
        QImageReader reader(path);
        reader.setDecideFormatFromContent(true);
        if (!reader.canRead())
        {
            qWarning() << "[HomeCarouselStore] 轮播图片无法加载，已跳过:" << path;
            continue;
        }
        result.append(path);
    }
    return result;
}

QString HomeCarouselStore::importImage(const QString &srcPath)
{
    QFile srcFile(srcPath);
    if (!srcFile.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hasher(QCryptographicHash::Sha1);
    if (!hasher.addData(&srcFile))
        return QString();
    const QString key = QString::fromLatin1(hasher.result().toHex().left(16));
    srcFile.close();

    QImageReader reader(srcPath);
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead())
        return QString();

    QImage image;
    const QSize srcSize = reader.size();
    if (srcSize.isValid() && qMax(srcSize.width(), srcSize.height()) > kCarouselImageMaxSide)
    {
        reader.setScaledSize(srcSize.scaled(kCarouselImageMaxSide, kCarouselImageMaxSide,
                                            Qt::KeepAspectRatio));
        image = reader.read();
    }
    else
    {
        image = reader.read();
        if (!image.isNull() && qMax(image.width(), image.height()) > kCarouselImageMaxSide)
            image = image.scaled(kCarouselImageMaxSide, kCarouselImageMaxSide,
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (image.isNull())
        return QString();

    const QString dir = imageDir();
    if (!QDir().mkpath(dir))
        return QString();

    // 带透明通道的存 PNG，其余存 JPG（体积小、启动解码快）
    const bool alpha = image.hasAlphaChannel();
    const QString dest = dir + QStringLiteral("/carousel_%1.%2")
                                   .arg(key, alpha ? QStringLiteral("png") : QStringLiteral("jpg"));
    if (QFileInfo::exists(dest))
        return dest;   // 同一张图已经在轮播里了，直接复用

    QImageWriter writer(dest, alpha ? QByteArray("PNG") : QByteArray("JPEG"));
    if (!alpha)
        writer.setQuality(92);
    if (!writer.write(image))
        return QString();
    return dest;
}

bool HomeCarouselStore::isManagedFile(const QString &path)
{
    const QString dir = QDir::cleanPath(imageDir()) + QLatin1Char('/');
    const QString clean = QDir::cleanPath(path);
    return clean.startsWith(dir, Qt::CaseInsensitive);
}

void HomeCarouselStore::removeImageFile(const QString &path)
{
    if (!isManagedFile(path))
        return;
    if (QFileInfo::exists(path))
        QFile::remove(path);
}

void HomeCarouselStore::clearCustom()
{
    const QStringList saved = savedImages();
    for (const QString &path : saved)
        removeImageFile(path);
    saveImages(QStringList());
}
