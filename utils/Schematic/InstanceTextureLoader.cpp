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
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QtMath>
#include <algorithm>
#include <memory>

namespace {

const char kTexPrefix[] = "assets/minecraft/textures/block/";
// 物品贴图目录：1.13+ 用 item/，1.6~1.12 用 items/（两个都列，缺失的一端自然为空）
const char *kItemPrefixes[] = {
    "assets/minecraft/textures/item/",
    "assets/minecraft/textures/items/",
    nullptr
};
const int  kCacheFormatVersion = 4;   // 4：动画贴图取第一帧（灯笼/水/岩浆等不再压成一条）

// 少数战利品 id 与贴图文件名不同名（如藏宝图在原版里就是 filled_map 贴图）
const char *kItemAliases[][2] = {
    { "buried_treasure_map", "filled_map" },
    { nullptr, nullptr }
};

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
    m_itemToTile.clear();
    m_tilesPerRow = 0;
    m_atlasSize = 0;
    m_textureCount = 0;
    m_itemCount = 0;
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
    int tilesPerRow = 0, atlasSize = 0, count = 0, itemCount = 0;
    QMap<QString, int> nameToTile, itemToTile;
    in >> atlas >> tilesPerRow >> atlasSize >> count >> itemCount
       >> nameToTile >> itemToTile;
    if (in.status() != QDataStream::Ok || atlas.isNull() || count <= 0
        || tilesPerRow <= 0 || atlasSize < tilesPerRow * 16
        || nameToTile.isEmpty() || count > tilesPerRow * tilesPerRow)
        return false;
    // 迟到文件截断/损坏时直接丢弃缓存，走 jar 重建
    if (atlas.width() != atlasSize || atlas.height() != atlasSize)
        return false;
    if (itemCount < 0 || itemCount > count || itemCount != itemToTile.size())
        return false;
    // 物品格下标必须落在有效范围内，否则图标会裁到空白处
    for (int tile : itemToTile)
        if (tile < 0 || tile >= count)
            return false;

    m_atlas = atlas;
    m_tilesPerRow = tilesPerRow;
    m_atlasSize = atlasSize;
    m_textureCount = count;
    m_itemCount = itemCount;
    m_nameToTile = nameToTile;
    m_itemToTile = itemToTile;
    m_cacheKey = jarPath;
    return true;
}

bool InstanceTextureLoader::saveDiskCache(const QString &jarPath) const
{
    if (m_atlas.isNull() || m_textureCount <= 0)
        return false;

    // QSaveFile 原子替换：避免写到一半崩溃/杀进程留下半截缓存导致下次打开闪退
    QSaveFile f(diskCachePath(jarPath));
    if (!f.open(QIODevice::WriteOnly))
        return false;

    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_5_12);
    out << quint32(0x42424154u) << quint32(kCacheFormatVersion);
    out << m_atlas << m_tilesPerRow << m_atlasSize << m_textureCount << m_itemCount;
    out << m_nameToTile << m_itemToTile;
    if (out.status() != QDataStream::Ok)
    {
        f.cancelWriting();
        return false;
    }
    return f.commit();
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

    // 物品贴图（contains 的物品图标用）跟方块贴图一起批量解压，避免多开一次 jar
    QSet<QString> itemEntries;
    for (int p = 0; kItemPrefixes[p]; ++p)
    {
        const QStringList items =
            JarUtils::listEntriesInJar(jarPath, QLatin1String(kItemPrefixes[p]));
        for (const QString &entry : items)
        {
            if (!entry.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
                continue;
            if (itemEntries.contains(entry))
                continue;
            itemEntries.insert(entry);
            pngs.append(entry);
        }
    }

    QMap<QString, QByteArray> raw;
    JarUtils::extractMultipleFromJarToMemory(jarPath, pngs, raw);

    QMap<QString, QImage> textures;       // 方块：文件名（不含扩展名/路径） -> 图
    QMap<QString, QImage> itemTextures;   // 物品：同上
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
        QMap<QString, QImage> &target = itemEntries.contains(it.key()) ? itemTextures
                                                                      : textures;
        // 仅保留第一张同名贴图
        if (!target.contains(key))
            target.insert(key, img);
    }

    if (textures.isEmpty())
    {
        if (errorOut)
            *errorOut = QStringLiteral("实例 jar 中没有方块贴图（%1）").arg(jarPath);
        return false;
    }

    buildAtlas(textures, itemTextures);
    m_cacheKey = jarPath;
    return true;
}

void InstanceTextureLoader::buildAtlas(const QMap<QString, QImage> &blockTextures,
                                       const QMap<QString, QImage> &itemTextures)
{
    const int tile = 16; // 每格 16×16
    const int count = blockTextures.size() + itemTextures.size();
    // 正方形图集：每行格数与行数一致；上限 64（4096 格）足够容纳新版方块 + 物品贴图
    m_tilesPerRow = qMax(1, qMin(64, (int)qCeil(qSqrt((double)count))));
    const int atlasSide = m_tilesPerRow * tile;

    QImage atlas(atlasSide, atlasSide, QImage::Format_RGBA8888);
    atlas.fill(Qt::transparent);

    int idx = 0;
    // 方块先入图集：既有的方块格下标保持不变，投影/预览取图不受影响
    auto placeAll = [&](const QMap<QString, QImage> &src, QMap<QString, int> &dst) {
        for (auto it = src.constBegin(); it != src.constEnd(); ++it)
        {
            if (idx >= m_tilesPerRow * m_tilesPerRow)
                break;
            QImage tex = it.value();
            if (tex.isNull())
                continue;
            if (tex.width() != tile || tex.height() != tile)
            {
                // 动画贴图是「一竖排正方形帧」：灯笼 16×48、静态水 16×512、水流 32×1024、
                // 时钟/指南针 16×64…… 整条缩进一格的话，几帧会被压成一条，而模型是按
                // 单帧的 0..16 像素 uv 取样的，于是取样落到了相邻帧上（灯笼渲染成一圈
                // 横条纹就是这么来的）。这里统一只取第一帧，再缩放到图集格。
                const bool vFrames = tex.height() > tex.width() && tex.height() % tex.width() == 0;
                const bool hFrames = tex.width() > tex.height() && tex.width() % tex.height() == 0;
                if (vFrames || hFrames)
                {
                    const int side = vFrames ? tex.width() : tex.height();
                    tex = tex.copy(0, 0, side, side);
                }
                tex = tex.scaled(tile, tile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            // 缩放/格式转换失败时跳过，避免对空图 memcpy 崩溃
            if (tex.isNull())
                continue;
            if (tex.format() != QImage::Format_RGBA8888)
                tex = tex.convertToFormat(QImage::Format_RGBA8888);
            if (tex.isNull() || tex.width() < tile || tex.height() < tile)
                continue;
            // 16×16 直接 memcpy 进图集，比逐次 QPainter 快
            const int dstX = (idx % m_tilesPerRow) * tile;
            const int dstY = (idx / m_tilesPerRow) * tile;
            bool copied = true;
            for (int y = 0; y < tile; ++y)
            {
                uchar *dstLine = atlas.scanLine(dstY + y) + dstX * 4;
                const uchar *srcLine = tex.constScanLine(y);
                if (!srcLine)
                {
                    copied = false;
                    break;
                }
                memcpy(dstLine, srcLine, size_t(tile) * 4);
            }
            if (!copied)
                continue;
            dst.insert(it.key(), idx);
            ++idx;
        }
    };

    placeAll(blockTextures, m_nameToTile);
    const int blockEnd = idx;
    placeAll(itemTextures, m_itemToTile);

    m_atlas = atlas;
    m_atlasSize = atlasSide;
    m_textureCount = idx;
    m_itemCount = idx - blockEnd;
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

QIcon InstanceTextureLoader::iconFromTile(int tile, int size) const
{
    if (m_atlas.isNull() || m_tilesPerRow <= 0 || tile < 0 || tile >= m_textureCount)
        return QIcon();
    // 图集中每格固定 16×16（buildAtlas 统一缩放到 tile=16）
    const int x = (tile % m_tilesPerRow) * 16;
    const int y = (tile / m_tilesPerRow) * 16;
    if (x + 16 > m_atlas.width() || y + 16 > m_atlas.height())
        return QIcon();
    QImage img = m_atlas.copy(x, y, 16, 16);
    if (img.isNull())
        return QIcon();
    if (size > 0 && size != 16)
        img = img.scaled(size, size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (img.isNull())
        return QIcon();
    return QIcon(QPixmap::fromImage(img));
}

QIcon InstanceTextureLoader::blockIcon(const QString &blockName, int size) const
{
    int tile = -1;
    if (!textureFor(blockName, tile))
        return QIcon();
    return iconFromTile(tile, size);
}

bool InstanceTextureLoader::itemTextureFor(const QString &itemId, int &tile) const
{
    if (m_itemToTile.isEmpty())
        return false;

    const QString base = normalizeName(itemId);
    if (base.isEmpty())
        return false;

    auto it = m_itemToTile.constFind(base);
    if (it != m_itemToTile.constEnd())
    {
        tile = it.value();
        return true;
    }

    // 少量战利品 id 与贴图文件名不同名
    for (int i = 0; kItemAliases[i][0]; ++i)
    {
        if (base != QLatin1String(kItemAliases[i][0]))
            continue;
        auto ai = m_itemToTile.constFind(QLatin1String(kItemAliases[i][1]));
        if (ai != m_itemToTile.constEnd())
        {
            tile = ai.value();
            return true;
        }
        break;
    }

    // 部分实例把动画贴图预先拆成了 *_00 / *_standby 之类的帧图，取第一帧顶上
    const QString frameCandidates[] = { base + QStringLiteral("_00"),
                                        base + QStringLiteral("_standby"),
                                        base + QStringLiteral("_standing") };
    for (const QString &cand : frameCandidates)
    {
        auto ci = m_itemToTile.constFind(cand);
        if (ci != m_itemToTile.constEnd())
        {
            tile = ci.value();
            return true;
        }
    }
    return false;
}

QIcon InstanceTextureLoader::itemIcon(const QString &itemId, int size) const
{
    int tile = -1;
    if (!itemTextureFor(itemId, tile))
        return QIcon();
    return iconFromTile(tile, size);
}

QIcon InstanceTextureLoader::swatchIcon(const QColor &color, int size, qreal dpr)
{
    const QPixmap pm = swatchPixmap(color, size, dpr);
    if (pm.isNull())
        return QIcon();
    return QIcon(pm);
}

QPixmap InstanceTextureLoader::swatchPixmap(const QColor &color, int size, qreal dpr)
{
    if (size <= 0)
        return QPixmap();
    if (dpr <= 0.0)
        dpr = 1.0;

    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    const qreal s = size;
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(1.0, 1.0, s - 2.0, s - 2.0);
    p.setPen(QPen(color.darker(150), 1.0));
    p.setBrush(color);
    p.drawRoundedRect(r, s * 0.22, s * 0.22);

    QColor hi = color.lighter(155);
    hi.setAlpha(150);
    p.setPen(Qt::NoPen);
    p.setBrush(hi);
    QPainterPath path;
    path.moveTo(r.left() + r.width() * 0.18, r.top() + r.height() * 0.16);
    path.lineTo(r.right() - r.width() * 0.18, r.top() + r.height() * 0.16);
    path.lineTo(r.right() - r.width() * 0.30, r.top() + r.height() * 0.40);
    path.lineTo(r.left() + r.width() * 0.30, r.top() + r.height() * 0.40);
    path.closeSubpath();
    p.drawPath(path);
    p.end();
    return pm;
}
