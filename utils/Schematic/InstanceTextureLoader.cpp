/**
 * @file   InstanceTextureLoader.cpp
 * @brief  从游戏实例的客户端 jar 加载方块材质（texture atlas）
 * @author BlockBox Team
 * @date   2026-08-18
 */

#include "InstanceTextureLoader.h"

#include "utils/JarUtils.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QStandardPaths>
#include <QHash>
#include <QtMath>
#include <algorithm>
#include <memory>

namespace {

const char kTexPrefix[] = "assets/minecraft/textures/block/";
const int  kCacheFormatVersion = 1;

// 常见派生方块后缀：优先尝试去掉这些后缀后找基础贴图
const char *kDerivedSuffixes[] = {
    "_wall_sign", "_sign", "_fence_gate", "_pressure_plate", "_button",
    "_trapdoor", "_door", "_wall", "_fence", "_stairs", "_slab",
    "_carpet", "_wool", "_leaves", "_torch", "_lantern",
    nullptr
};

QMutex g_sharedMutex;
QHash<QString, std::shared_ptr<InstanceTextureLoader>> g_shared;

QString diskCacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                        + QStringLiteral("/projection_editor/atlas");
    QDir().mkpath(dir);
    return dir;
}

QString diskCachePath(const QString &jarPath)
{
    const QFileInfo fi(jarPath);
    QByteArray id;
    id.append(fi.absoluteFilePath().toUtf8());
    id.append(QByteArray::number(fi.size()));
    id.append(QByteArray::number(fi.lastModified().toMSecsSinceEpoch()));
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(id, QCryptographicHash::Md5).toHex());
    return diskCacheDir() + QLatin1Char('/') + hash + QStringLiteral(".atlas");
}

} // namespace

QString InstanceTextureLoader::normalizeName(const QString &blockName)
{
    QString n = blockName.trimmed();
    const int bracket = n.indexOf(QLatin1Char('['));
    if (bracket >= 0)
        n = n.left(bracket);
    const int slash = n.indexOf(QLatin1Char(':'));
    if (slash >= 0)
        n = n.mid(slash + 1);
    return n.toLower().trimmed();
}

bool InstanceTextureLoader::findClientJar(const QString &instancePath, QString &jarPath) const
{
    QDir dir(instancePath);
    if (!dir.exists())
        return false;

    // 优先找与目录名同名的 jar（标准客户端 jar），否则取最大 jar
    const QString base = QFileInfo(instancePath).fileName();
    QString bestJar;
    qint64 bestSize = -1;
    const QFileInfoList jars = dir.entryInfoList(QStringList() << "*.jar",
                                                 QDir::Files, QDir::Name);
    for (const QFileInfo &fi : jars)
    {
        if (fi.completeBaseName() == base)
        {
            jarPath = fi.absoluteFilePath();
            return true;
        }
        if (fi.size() > bestSize)
        {
            bestSize = fi.size();
            bestJar = fi.absoluteFilePath();
        }
    }
    if (!bestJar.isEmpty())
    {
        jarPath = bestJar;
        return true;
    }
    return false;
}

bool InstanceTextureLoader::load(const QString &instancePath, QString *errorOut)
{
    m_atlas = QImage();
    m_nameToTile.clear();
    m_tilesPerRow = 0;
    m_atlasSize = 0;
    m_textureCount = 0;
    m_cacheKey.clear();

    QString jarPath;
    if (!findClientJar(instancePath, jarPath))
    {
        if (errorOut)
            *errorOut = QStringLiteral("未在实例目录找到客户端 .jar：%1").arg(instancePath);
        return false;
    }

    // 磁盘缓存优先（同一 jar 二次打开走毫秒级路径）
    if (tryLoadDiskCache(jarPath))
        return true;

    if (!loadFromJar(jarPath, errorOut))
        return false;

    saveDiskCache(jarPath);
    return true;
}

InstanceTextureLoader *InstanceTextureLoader::sharedForInstance(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return nullptr;

    // 先定位 jar 作为共享 key，避免同一 jar 被不同实例路径重复加载
    QString jarPath;
    {
        InstanceTextureLoader probe;
        if (!probe.findClientJar(instancePath, jarPath))
            return nullptr;
    }

    QMutexLocker lock(&g_sharedMutex);
    auto it = g_shared.constFind(jarPath);
    if (it != g_shared.constEnd() && it.value() && it.value()->isValid())
        return it.value().get();

    auto loader = std::make_shared<InstanceTextureLoader>();
    if (!loader->load(instancePath, nullptr) || !loader->isValid())
        return nullptr;
    g_shared.insert(jarPath, loader);
    return loader.get();
}

bool InstanceTextureLoader::tryLoadDiskCache(const QString &jarPath)
{
    const QString path = diskCachePath(jarPath);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_5_12);
    quint32 magic = 0, ver = 0;
    in >> magic >> ver;
    if (magic != 0x42424154u /* 'BBAT' */ || ver != kCacheFormatVersion)
        return false;

    QImage atlas;
    int tilesPerRow = 0, atlasSize = 0, count = 0;
    in >> atlas >> tilesPerRow >> atlasSize >> count;
    QMap<QString, int> nameToTile;
    in >> nameToTile;
    if (in.status() != QDataStream::Ok || atlas.isNull() || count <= 0
        || tilesPerRow <= 0 || nameToTile.isEmpty())
        return false;

    m_atlas = atlas;
    m_tilesPerRow = tilesPerRow;
    m_atlasSize = atlasSize;
    m_textureCount = count;
    m_nameToTile = nameToTile;
    m_cacheKey = jarPath;
    return true;
}

bool InstanceTextureLoader::saveDiskCache(const QString &jarPath) const
{
    if (m_atlas.isNull() || m_textureCount <= 0)
        return false;

    const QString path = diskCachePath(jarPath);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;

    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_5_12);
    out << quint32(0x42424154u) << quint32(kCacheFormatVersion);
    out << m_atlas << m_tilesPerRow << m_atlasSize << m_textureCount;
    out << m_nameToTile;
    return out.status() == QDataStream::Ok;
}

bool InstanceTextureLoader::loadFromJar(const QString &jarPath, QString *errorOut)
{
    // 列出方块贴图后整包一次批量解压（原先逐条 open+扫中心目录，是打开慢的主因）
    QStringList entries = JarUtils::listEntriesInJar(jarPath, kTexPrefix);
    QStringList pngs;
    pngs.reserve(entries.size());
    for (const QString &entry : entries)
    {
        if (entry.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
            pngs.append(entry);
    }
    if (pngs.isEmpty())
    {
        if (errorOut)
            *errorOut = QStringLiteral("实例 jar 中没有方块贴图（%1）").arg(jarPath);
        return false;
    }

    QMap<QString, QByteArray> raw;
    JarUtils::extractMultipleFromJarToMemory(jarPath, pngs, raw);

    QMap<QString, QImage> textures;   // 文件名（不含扩展名/路径） -> 图
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it)
    {
        QImage img;
        if (!img.loadFromData(it.value()) || img.isNull())
            continue;
        // 统一转成 RGBA
        if (img.format() != QImage::Format_RGBA8888)
            img = img.convertToFormat(QImage::Format_RGBA8888);
        if (img.width() <= 0 || img.height() <= 0)
            continue;
        const QString key = QFileInfo(it.key()).completeBaseName().toLower();
        // 仅保留第一张同名贴图
        if (!textures.contains(key))
            textures.insert(key, img);
    }

    if (textures.isEmpty())
    {
        if (errorOut)
            *errorOut = QStringLiteral("实例 jar 中没有方块贴图（%1）").arg(jarPath);
        return false;
    }

    buildAtlas(textures);
    m_cacheKey = jarPath;
    return true;
}

void InstanceTextureLoader::buildAtlas(const QMap<QString, QImage> &textures)
{
    const int tile = 16; // 每格 16×16
    const int count = textures.size();
    // 正方形图集：每行格数与行数一致，避免非方形时 v 向 UV 计算错误
    m_tilesPerRow = qMax(1, qMin(32, (int)qCeil(qSqrt((double)count))));
    const int rows = m_tilesPerRow;
    const int atlasSide = m_tilesPerRow * tile;

    QImage atlas(atlasSide, atlasSide, QImage::Format_RGBA8888);
    atlas.fill(Qt::transparent);

    int idx = 0;
    for (auto it = textures.constBegin(); it != textures.constEnd(); ++it)
    {
        const int col = idx % m_tilesPerRow;
        const int row = idx / m_tilesPerRow;
        QImage tex = it.value();
        if (tex.width() != tile || tex.height() != tile)
            tex = tex.scaled(tile, tile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // 16×16 直接 memcpy 进图集，比逐次 QPainter 快
        if (tex.format() != QImage::Format_RGBA8888)
            tex = tex.convertToFormat(QImage::Format_RGBA8888);
        const int dstX = col * tile;
        const int dstY = row * tile;
        for (int y = 0; y < tile; ++y)
        {
            uchar *dst = atlas.scanLine(dstY + y) + dstX * 4;
            const uchar *src = tex.constScanLine(y);
            memcpy(dst, src, size_t(tile) * 4);
        }
        m_nameToTile.insert(it.key(), idx);
        ++idx;
    }

    m_atlas = atlas;
    m_atlasSize = atlasSide;
    m_textureCount = count;
}

bool InstanceTextureLoader::resolveByName(const QString &base, int &tile) const
{
    if (m_nameToTile.isEmpty() || base.isEmpty())
        return false;

    auto it = m_nameToTile.constFind(base);
    if (it != m_nameToTile.constEnd())
    {
        tile = it.value();
        return true;
    }

    // 基础名自身尝试常见变体：如 grass_block -> grass_block_top，
    // oak_log -> oak_log_top（某些贴图仅提供 _top/_side 变体）
    for (const QString &cand : QStringList()
             << base + QStringLiteral("_top")
             << base + QStringLiteral("_side")
             << base + QStringLiteral("_planks"))
    {
        auto vi = m_nameToTile.constFind(cand);
        if (vi != m_nameToTile.constEnd())
        {
            tile = vi.value();
            return true;
        }
    }

    // 去掉常见派生后缀后尝试，并尝试 _top/_side/_planks 常见变体
    for (int s = 0; kDerivedSuffixes[s]; ++s)
    {
        const QString suffix = QLatin1String(kDerivedSuffixes[s]);
        if (!base.endsWith(suffix))
            continue;
        const QString core = base.left(base.size() - suffix.size());
        for (const QString &cand : QStringList()
                 << core
                 << core + QStringLiteral("_top")
                 << core + QStringLiteral("_side")
                 << core + QStringLiteral("_planks"))
        {
            auto ci = m_nameToTile.constFind(cand);
            if (ci != m_nameToTile.constEnd())
            {
                tile = ci.value();
                return true;
            }
        }
        break; // 只尝试第一个匹配的后缀
    }

    return false;
}

bool InstanceTextureLoader::textureFor(const QString &blockName, int &tile) const
{
    const QString base = normalizeName(blockName);
    return resolveByName(base, tile);
}

bool InstanceTextureLoader::tileForKey(const QString &modelKey, int &tile) const
{
    QString key = modelKey.trimmed();
    // 去 "minecraft:" 前缀
    const int colon = key.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        key = key.mid(colon + 1);
    // 去 "block/" 及更上层路径
    const int slash = key.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0)
        key = key.mid(slash + 1);
    return resolveByName(key.toLower(), tile);
}

void InstanceTextureLoader::tileUv(int tile, float &u0, float &v0, float &u1, float &v1) const
{
    if (m_atlasSize <= 0 || tile < 0 || tile >= m_textureCount)
    {
        u0 = v0 = 0.0f; u1 = v1 = 1.0f;
        return;
    }
    const float per = 16.0f / m_atlasSize; // 每格在图集中的尺寸比例
    const int col = tile % m_tilesPerRow;
    const int row = tile / m_tilesPerRow;
    u0 = col * per;
    v0 = row * per;
    u1 = u0 + per;
    v1 = v0 + per;
}
