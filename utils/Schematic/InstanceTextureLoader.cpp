/**
 * @file   InstanceTextureLoader.cpp
 * @brief  从游戏实例的客户端 jar 加载方块材质（texture atlas）
 * @author BlockBox Team
 * @date   2026-08-18
 */

#include "InstanceTextureLoader.h"

#include "utils/JarUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QtMath>
#include <algorithm>

namespace {

const char kTexPrefix[] = "assets/minecraft/textures/block/";

// 常见派生方块后缀：优先尝试去掉这些后缀后找基础贴图
const char *kDerivedSuffixes[] = {
    "_wall_sign", "_sign", "_fence_gate", "_pressure_plate", "_button",
    "_trapdoor", "_door", "_wall", "_fence", "_stairs", "_slab",
    "_carpet", "_wool", "_leaves", "_torch", "_lantern",
    nullptr
};

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

    QString jarPath;
    if (!findClientJar(instancePath, jarPath))
    {
        if (errorOut)
            *errorOut = QStringLiteral("未在实例目录找到客户端 .jar：%1").arg(instancePath);
        return false;
    }

    // 列出方块贴图
    const QStringList entries = JarUtils::listEntriesInJar(jarPath, kTexPrefix);
    QMap<QString, QImage> textures;   // 文件名（不含扩展名/路径） -> 图
    for (const QString &entry : entries)
    {
        if (!entry.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
            continue;
        QByteArray data;
        if (!JarUtils::extractFromJarToMemory(jarPath, entry, data))
            continue;
        QImage img;
        if (!img.loadFromData(data) || img.isNull())
            continue;
        // 统一转成 RGBA
        if (img.format() != QImage::Format_RGBA8888)
            img = img.convertToFormat(QImage::Format_RGBA8888);
        if (img.width() <= 0 || img.height() <= 0)
            continue;
        const QString key = QFileInfo(entry).completeBaseName().toLower();
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
        QImage scaled = tex.scaled(tile, tile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // 缩放后可能是 ARGB，复制到 RGBA 图集
        QPainter p(&atlas);
        p.drawImage(col * tile, row * tile, scaled);
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
