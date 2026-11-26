/**
 * @file   InstanceEntityTextures.cpp
 * @brief  实例实体贴图图集实现
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "InstanceEntityTextures.h"

#include "InstanceTextureLoader.h"
#include "utils/JarUtils.h"

#include <QFileInfo>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QStringList>
#include <QVector>
#include <QtMath>

#include <algorithm>
#include <memory>

namespace {

/** jar 里实体贴图目录 */
const char kEntityPrefix[] = "assets/minecraft/textures/entity/";

/** 单张实体贴图最大边长：原版最大是末影龙/凋灵的 256，留余量给资源包 */
const int kMaxTextureSide = 512;

/** 图集上限边长 */
const int kMaxAtlasSide = 4096;

/**
 * 分层的生物贴图：村民 / 僵尸村民在 jar 里是「底图 + 生物群系外观 + 职业外观」
 * 三层，后两层只画衣服和帽子的那一小块，其余像素全透明。单拿职业那层当整张
 * 贴图用，画出来就是个透空的人（看上去像「村民渲染不出来」），所以这里先把
 * 它们按游戏里的叠法合成一张。
 */
struct LayeredSkin
{
    const char *dir;        ///< 贴图子目录，如 "villager"
    const char *baseLeaf;   ///< 底图文件名，如 "villager"
    const char *typeLeaf;   ///< 生物群系外观（缺了就只用底图），如 "type/plains"
    const char *layerDir;   ///< 外观叠层目录，如 "profession"
};

const LayeredSkin kLayeredSkins[] = {
    { "villager", "villager", "type/plains", "profession" },
    { "zombie_villager", "zombie_villager", "type/plains", "profession" },
    { nullptr, nullptr, nullptr, nullptr }
};

/** 把若干张同尺寸的透明叠层压到一张底图上 */
QImage compositeLayers(const QImage &base, const QVector<QImage> &layers)
{
    QImage out = base.convertToFormat(QImage::Format_RGBA8888);
    if (out.isNull())
        return out;
    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (const QImage &layer : layers)
    {
        if (layer.isNull() || layer.size() != out.size())
            continue;
        painter.drawImage(0, 0, layer);
    }
    painter.end();
    return out;
}

QMutex g_sharedMutex;
QHash<QString, std::shared_ptr<InstanceEntityTextures>> g_shared;

} // namespace

QString InstanceEntityTextures::normalizeKey(const QString &key)
{
    QString k = key.trimmed().toLower();
    if (k.startsWith(QLatin1String("assets/minecraft/textures/")))
        k = k.mid(24);
    if (k.startsWith(QLatin1String("textures/")))
        k = k.mid(9);
    if (k.endsWith(QLatin1String(".png")))
        k.chop(4);
    return k;
}

InstanceEntityTextures *InstanceEntityTextures::sharedForInstance(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return nullptr;

    QString jarPath;
    if (!InstanceTextureLoader::clientJarPath(instancePath, jarPath))
        return nullptr;

    QMutexLocker lock(&g_sharedMutex);
    auto it = g_shared.constFind(jarPath);
    if (it != g_shared.constEnd() && it.value() && it.value()->isValid())
        return it.value().get();

    std::shared_ptr<InstanceEntityTextures> loader(new InstanceEntityTextures());
    if (!loader->load(jarPath) || !loader->isValid())
        return nullptr;
    g_shared.insert(jarPath, loader);
    return loader.get();
}

bool InstanceEntityTextures::rectForKey(const QString &key, QRect &rect, QSize *sourceSize) const
{
    const QString norm = normalizeKey(key);
    if (norm.isEmpty())
        return false;

    auto hit = [&](const QString &entryKey) {
        auto it = m_entries.constFind(entryKey);
        if (it == m_entries.constEnd())
            return false;
        rect = it.value().rect;
        if (sourceSize != nullptr)
            *sourceSize = it.value().size;
        return true;
    };

    if (hit(norm))
        return true;

    // 图集里的键是相对 textures/entity/ 存的（"pig/pig"），模型里写的是
    // "entity/pig/pig"，少了这一步就只能靠末段名兜底 —— 村民和僵尸村民的
    // profession 同名，兜底会串成对方的外观
    const QString entryKey = norm.startsWith(QLatin1String("entity/")) ? norm.mid(7) : norm;
    if (entryKey != norm && hit(entryKey))
        return true;

    // 老版本 jar（1.12 及以前）没有职业 / 群系分层，村民只有一张整皮肤的
    // villager.png：拿「上一级目录名」当文件名再试（villager/profession/butcher
    // -> villager/villager），这样老投影里的村民也画得出来
    const QStringList parts = entryKey.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() >= 3)
    {
        const QString stem = parts.at(parts.size() - 3);
        if (hit(stem + QLatin1Char('/') + stem))
            return true;
    }

    // 末段名兜底：模型里写 entity/pig/pig，老版本 jar 里就是 entity/pig.png
    const int slash = norm.lastIndexOf(QLatin1Char('/'));
    const QString leaf = (slash >= 0) ? norm.mid(slash + 1) : norm;
    if (leaf.isEmpty())
        return false;
    const QString leafKey = m_leafToKey.value(leaf);
    if (!leafKey.isEmpty() && hit(leafKey))
        return true;

    // 再试 entity/<leaf>（模型写 entity/horse/horse_white，jar 里是 entity/horse_white.png）
    return hit(QLatin1String("entity/") + leaf);
}

bool InstanceEntityTextures::load(const QString &jarPath)
{
    m_atlas = QImage();
    m_entries.clear();
    m_leafToKey.clear();
    m_atlasSize = 0;
    m_textureCount = 0;

    // 列出实体贴图并整包一次批量解压（逐条扫中心目录太慢）
    QStringList entries = JarUtils::listEntriesInJar(jarPath, QLatin1String(kEntityPrefix));
    QStringList pngs;
    pngs.reserve(entries.size());
    for (const QString &entry : entries)
    {
        if (entry.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
            pngs.append(entry);
    }
    if (pngs.isEmpty())
        return false;

    QMap<QString, QByteArray> raw;
    JarUtils::extractMultipleFromJarToMemory(jarPath, pngs, raw);
    if (raw.isEmpty())
        return false;

    // 读取 + 归一化尺寸（大贴图等比缩小，避免图集被一张图吃掉）
    struct Entry
    {
        QString key;
        QString leaf;
        QImage image;
    };
    std::vector<Entry> items;
    items.reserve(size_t(raw.size()));
    qint64 areaSum = 0;
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it)
    {
        QImage img;
        if (!img.loadFromData(it.value()) || img.isNull())
            continue;
        if (img.format() != QImage::Format_RGBA8888)
            img = img.convertToFormat(QImage::Format_RGBA8888);
        if (img.isNull() || img.width() <= 0 || img.height() <= 0)
            continue;

        if (img.width() > kMaxTextureSide || img.height() > kMaxTextureSide)
        {
            img = img.scaled(kMaxTextureSide, kMaxTextureSide, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
            if (img.isNull())
                continue;
        }

        Entry entry;
        // "assets/minecraft/textures/entity/pig/pig.png" -> "pig/pig"
        entry.key = it.key().mid(int(qstrlen(kEntityPrefix)));
        if (entry.key.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
            entry.key.chop(4);
        entry.key = entry.key.toLower();
        entry.leaf = QFileInfo(entry.key).fileName();
        entry.image = img;
        areaSum += qint64(img.width()) * img.height();
        items.push_back(std::move(entry));
    }
    if (items.empty())
        return false;

    // 分层贴图（村民这类）：把透明的职业 / 群系叠层合成到实心底图上，
    // 合成后仍然挂在职业那一层的键上，渲染端照旧按 /profession/xxx 取图
    {
        QHash<QString, int> keyToIndex;
        keyToIndex.reserve(int(items.size()));
        for (int i = 0; i < int(items.size()); ++i)
            keyToIndex.insert(items[size_t(i)].key, i);

        for (int s = 0; kLayeredSkins[s].dir; ++s)
        {
            const QString dir = QString::fromLatin1(kLayeredSkins[s].dir) + QLatin1Char('/');
            const int baseIndex =
                keyToIndex.value(dir + QLatin1String(kLayeredSkins[s].baseLeaf), -1);
            if (baseIndex < 0)
                continue;

            const QImage baseImage = items[size_t(baseIndex)].image;
            const int typeIndex =
                keyToIndex.value(dir + QLatin1String(kLayeredSkins[s].typeLeaf), -1);
            const QString layerPrefix =
                dir + QLatin1String(kLayeredSkins[s].layerDir) + QLatin1Char('/');

            for (Entry &entry : items)
            {
                if (!entry.key.startsWith(layerPrefix))
                    continue;
                QVector<QImage> layers;
                if (typeIndex >= 0)
                    layers.append(items[size_t(typeIndex)].image);
                layers.append(entry.image);
                const QImage composed = compositeLayers(baseImage, layers);
                if (!composed.isNull())
                    entry.image = composed;
            }
        }
    }

    // 图集边长：按总面积留 35% 余量取 2 的幂，夹在 256..4096 之间
    int side = 256;
    const qint64 needed = qint64(double(areaSum) * 1.35) + 1;
    while (side < kMaxAtlasSide && qint64(side) * side < needed)
        side *= 2;
    m_atlasSize = side;

    m_atlas = QImage(side, side, QImage::Format_RGBA8888);
    if (m_atlas.isNull())
        return false;
    m_atlas.fill(Qt::transparent);

    // 货架式打包：高的先放，逐行往右排
    std::sort(items.begin(), items.end(), [](const Entry &a, const Entry &b) {
        if (a.image.height() != b.image.height())
            return a.image.height() > b.image.height();
        return a.image.width() > b.image.width();
    });

    int penX = 0, penY = 0, shelfHeight = 0;
    for (const Entry &entry : items)
    {
        const int w = entry.image.width();
        const int h = entry.image.height();
        if (w > side || h > side)
            continue;
        if (penX + w > side)
        {
            penX = 0;
            penY += shelfHeight;
            shelfHeight = 0;
        }
        if (penY + h > side)
            continue;   // 放不下就跳过（图集已满，剩下的实体回退为纯色）

        // 逐行拷进图集（不透明/半透明像素都原样保留，渲染时靠 alpha 判断）
        for (int y = 0; y < h; ++y)
        {
            const uchar *src = entry.image.constScanLine(y);
            uchar *dst = m_atlas.scanLine(penY + y) + penX * 4;
            if (!src || !dst)
                continue;
            memcpy(dst, src, size_t(w) * 4);
        }

        const QRect rect(penX, penY, w, h);
        InstanceEntityTextures::TexEntry texEntry;
        texEntry.rect = rect;
        texEntry.size = QSize(w, h);
        m_entries.insert(entry.key, texEntry);
        if (!m_leafToKey.contains(entry.leaf))
            m_leafToKey.insert(entry.leaf, entry.key);

        penX += w;
        shelfHeight = qMax(shelfHeight, h);
        ++m_textureCount;
    }

    return m_textureCount > 0;
}
