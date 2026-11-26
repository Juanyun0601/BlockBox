/**
 * @file   CoverImageCache.cpp
 * @brief  个人主页封面图本机缓存实现
 * @author BlockBox Team
 * @date   2026-11-20
 */

#include "CoverImageCache.h"

#include <QCache>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace {

// 解码后的封面跟着内存走：账户页资料卡横幅和设置里的小预览共用同一份，
// 来回切页面不用重新解码（一张 1440×480 大约 2.8 MB）
constexpr int kMemoryCostLimit = 32 * 1024 * 1024;
// 每张封面正常两三百 KB，留 40 张足够应付反复换封面，再多就按最旧的删
constexpr int kMaxDiskFiles = 40;
// 缓存文件统一用这个后缀，读的时候按内容识别格式，不依赖扩展名
const char kCacheSuffix[] = "img";

QCache<QString, QImage> &memoryCache()
{
    static QCache<QString, QImage> s_cache(kMemoryCostLimit);
    return s_cache;
}

QString cacheKey(const QString &absoluteUrl)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(absoluteUrl.toUtf8(), QCryptographicHash::Sha1).toHex());
}

/** 缓存目录里文件太多时，按修改时间把最旧的一批删掉 */
void pruneDiskCache(const QString &directory)
{
    const QFileInfoList files = QDir(directory).entryInfoList(
        QStringList() << (QStringLiteral("*.") + QLatin1String(kCacheSuffix)), QDir::Files);
    if (files.size() <= kMaxDiskFiles)
        return;

    QVector<QFileInfo> sorted(files.begin(), files.end());
    std::sort(sorted.begin(), sorted.end(), [](const QFileInfo &lhs, const QFileInfo &rhs) {
        return lhs.lastModified() > rhs.lastModified();
    });
    for (int i = kMaxDiskFiles; i < sorted.size(); ++i)
        QFile::remove(sorted.at(i).absoluteFilePath());
}

} // namespace

QString CoverImageCache::cacheDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/forum_covers");
}

QString CoverImageCache::cacheFilePath(const QString &absoluteUrl)
{
    return cacheDirectory() + QLatin1Char('/') + cacheKey(absoluteUrl)
           + QLatin1Char('.') + QLatin1String(kCacheSuffix);
}

QImage CoverImageCache::load(const QString &absoluteUrl)
{
    if (absoluteUrl.isEmpty())
        return QImage();

    const QString key = cacheKey(absoluteUrl);
    if (QImage *cached = memoryCache().object(key))
        return *cached;

    const QString path = cacheFilePath(absoluteUrl);
    if (!QFileInfo::exists(path))
        return QImage();

    // 文件坏了（下载中断、磁盘写满）就当没有缓存，让外面重新去下载
    QImage image;
    if (!image.load(path) || image.isNull())
    {
        QFile::remove(path);
        return QImage();
    }

    memoryCache().insert(key, new QImage(image), image.sizeInBytes());
    return image;
}

void CoverImageCache::save(const QString &absoluteUrl, const QImage &image)
{
    if (absoluteUrl.isEmpty() || image.isNull())
        return;

    const QString key = cacheKey(absoluteUrl);
    memoryCache().insert(key, new QImage(image), image.sizeInBytes());

    const QString path = cacheFilePath(absoluteUrl);
    QDir().mkpath(cacheDirectory());

    // 带透明的封面存 PNG 不丢底色，照片类存 JPG 体积小（单张几百 KB）
    const bool lossless = image.hasAlphaChannel();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const bool written = lossless ? image.save(&file, "PNG")
                                  : image.save(&file, "JPG", 92);
    if (written && file.commit())
        pruneDiskCache(cacheDirectory());
}

void CoverImageCache::remove(const QString &absoluteUrl)
{
    if (absoluteUrl.isEmpty())
        return;

    memoryCache().remove(cacheKey(absoluteUrl));
    QFile::remove(cacheFilePath(absoluteUrl));
}

void CoverImageCache::clear()
{
    memoryCache().clear();

    const QString directory = cacheDirectory();
    const QFileInfoList files = QDir(directory).entryInfoList(
        QStringList() << (QStringLiteral("*.") + QLatin1String(kCacheSuffix)), QDir::Files);
    for (const QFileInfo &info : files)
        QFile::remove(info.absoluteFilePath());
}
