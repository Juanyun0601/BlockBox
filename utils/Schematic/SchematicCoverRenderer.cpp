/**
 * @file   SchematicCoverRenderer.cpp
 * @brief  投影封面等距 3D 渲染器实现（实例/资源包材质 + 两阶段出图）
 * @author BlockBox Team
 * @date 2026-10-16
 *
 * 遮挡剔除与 nonOccluding 列表来自 EngineHub/SchematicWebViewer（MIT）；
 * 等距体素预览思路参考 litematic-viewer / Deepslate；格式参考 litematica。
 */

#include "SchematicCoverRenderer.h"

#include "InstanceTextureLoader.h"
#include "LitematicEditor.h"
#include "SchematicDocument.h"
#include "utils/JarUtils.h"
#include "utils/WorldMap/WorldMapRenderer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTransform>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {

// ── 缓存与采样参数 ──────────────────────────────────────────────────
constexpr int kCacheFormatVersion = 4;
constexpr int kMaxSampledVoxels   = 64000;
constexpr int kMaxLinearDim        = 224;
constexpr int kMinStride           = 1;
constexpr int kMaxStride           = 16;
constexpr int kTextureDrawMinScale = 7;
// 无（贴图改由 InstanceTextureLoader 图集一次性加载，与投影编辑器同源）

// ── 方块颜色 ────────────────────────────────────────────────────────
QColor namedDyeColor(const QString &token)
{
    static const QHash<QString, QColor> kDye = {
        { QStringLiteral("white"),       QColor("#F0F0F0") },
        { QStringLiteral("orange"),      QColor("#F9801D") },
        { QStringLiteral("magenta"),     QColor("#C74EBD") },
        { QStringLiteral("light_blue"),  QColor("#3AB3DA") },
        { QStringLiteral("yellow"),       QColor("#FED83D") },
        { QStringLiteral("lime"),         QColor("#80C71F") },
        { QStringLiteral("pink"),         QColor("#F38BAA") },
        { QStringLiteral("gray"),         QColor("#474F52") },
        { QStringLiteral("light_gray"),   QColor("#9D9D97") },
        { QStringLiteral("cyan"),         QColor("#169C9C") },
        { QStringLiteral("purple"),       QColor("#8932B8") },
        { QStringLiteral("blue"),         QColor("#3C44AA") },
        { QStringLiteral("brown"),        QColor("#835432") },
        { QStringLiteral("green"),        QColor("#5E7C16") },
        { QStringLiteral("red"),          QColor("#B02E26") },
        { QStringLiteral("black"),        QColor("#1D1D21") },
    };
    return kDye.value(token, QColor());
}

QString normalizeBlockKey(const QString &blockState)
{
    QString n = blockState.trimmed();
    const int bracket = n.indexOf(QLatin1Char('['));
    if (bracket >= 0)
        n = n.left(bracket);
    const int colon = n.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        n = n.mid(colon + 1);
    return n.toLower().trimmed();
}

QColor coverBlockColor(const QString &blockState)
{
    const QString n = normalizeBlockKey(blockState);

    static const QStringList kColored = {
        QStringLiteral("wool"),
        QStringLiteral("concrete"),
        QStringLiteral("concrete_powder"),
        QStringLiteral("terracotta"),
        QStringLiteral("stained_glass"),
        QStringLiteral("stained_glass_pane"),
        QStringLiteral("glazed_terracotta"),
        QStringLiteral("carpet"),
        QStringLiteral("shulker_box"),
    };
    for (const QString &suffix : kColored)
    {
        if (n.endsWith(QLatin1Char('_') + suffix) || n == suffix)
        {
            if (n == suffix)
                return coverBlockColor(QStringLiteral("minecraft:terracotta"));
            QString prefix = n;
            prefix.chop(suffix.size() + 1);
            const QColor dye = namedDyeColor(prefix);
            if (dye.isValid())
                return dye;
        }
    }

    static const QHash<QString, QColor> kExtra = {
        { QStringLiteral("oak_planks"),        QColor("#A08050") },
        { QStringLiteral("spruce_planks"),     QColor("#7A5A3A") },
        { QStringLiteral("birch_planks"),      QColor("#C0B080") },
        { QStringLiteral("jungle_planks"),     QColor("#9A7A4A") },
        { QStringLiteral("acacia_planks"),     QColor("#B06840") },
        { QStringLiteral("dark_oak_planks"),   QColor("#4A3220") },
        { QStringLiteral("mangrove_planks"),   QColor("#8A4A3A") },
        { QStringLiteral("cherry_planks"),     QColor("#E8B0A0") },
        { QStringLiteral("bamboo_planks"),     QColor("#B8A040") },
        { QStringLiteral("crimson_planks"),    QColor("#6A3030") },
        { QStringLiteral("warped_planks"),     QColor("#3A6A6A") },
        { QStringLiteral("oak_log"),           QColor("#6B5233") },
        { QStringLiteral("spruce_log"),        QColor("#3A2818") },
        { QStringLiteral("birch_log"),         QColor("#C0C0B0") },
        { QStringLiteral("oak_leaves"),        QColor("#4E7F32") },
        { QStringLiteral("spruce_leaves"),     QColor("#3E6B3E") },
        { QStringLiteral("birch_leaves"),      QColor("#6F9A45") },
        { QStringLiteral("white_concrete"),    QColor("#CFD5D6") },
        { QStringLiteral("glass_pane"),        QColor("#D5E8F0") },
        { QStringLiteral("bricks"),            QColor("#96554A") },
        { QStringLiteral("stone_bricks"),      QColor("#7F7F7F") },
        { QStringLiteral("smooth_stone"),      QColor("#A0A0A0") },
        { QStringLiteral("quartz_block"),      QColor("#E8E4DC") },
        { QStringLiteral("smooth_quartz"),     QColor("#E8E4DC") },
        { QStringLiteral("nether_bricks"),     QColor("#4C1E1E") },
        { QStringLiteral("prismarine"),        QColor("#6A9A9A") },
        { QStringLiteral("sea_lantern"),       QColor("#A8C8C0") },
        { QStringLiteral("glowstone"),         QColor("#F5C86A") },
        { QStringLiteral("shroomlight"),       QColor("#E08040") },
        { QStringLiteral("moss_block"),        QColor("#5B7C2E") },
        { QStringLiteral("azalea_leaves"),     QColor("#59833A") },
        { QStringLiteral("flowering_azalea_leaves"), QColor("#8A6A90") },
        { QStringLiteral("calcite"),           QColor("#DFDFDF") },
        { QStringLiteral("dripstone_block"),   QColor("#9A7A60") },
        { QStringLiteral("deepslate_bricks"),  QColor("#4A4A4C") },
        { QStringLiteral("polished_deepslate"),QColor("#555558") },
        { QStringLiteral("copper_block"),      QColor("#C07A40") },
        { QStringLiteral("exposed_copper"),    QColor("#8A9A60") },
        { QStringLiteral("oxidized_copper"),   QColor("#4A9A80") },
        { QStringLiteral("gold_block"),        QColor("#F0C830") },
        { QStringLiteral("iron_block"),        QColor("#D8D8D8") },
        { QStringLiteral("diamond_block"),     QColor("#5AC8C0") },
        { QStringLiteral("emerald_block"),     QColor("#3CB05A") },
        { QStringLiteral("lapis_block"),       QColor("#2A4AA0") },
        { QStringLiteral("redstone_block"),    QColor("#B03028") },
        { QStringLiteral("coal_block"),        QColor("#1A1A1A") },
        { QStringLiteral("amethyst_block"),    QColor("#8A6AC0") },
        { QStringLiteral("crying_obsidian"),   QColor("#3A2060") },
        { QStringLiteral("bookshelf"),         QColor("#A08050") },
        { QStringLiteral("crafting_table"),    QColor("#A08050") },
        { QStringLiteral("furnace"),           QColor("#7A7A7A") },
        { QStringLiteral("chest"),             QColor("#A08050") },
        { QStringLiteral("barrel"),            QColor("#A08050") },
        { QStringLiteral("lantern"),           QColor("#E0A040") },
        { QStringLiteral("torch"),             QColor("#E0C060") },
        { QStringLiteral("sea_pickle"),        QColor("#6A9A40") },
        { QStringLiteral("lily_pad"),          QColor("#4C8B3A") },
    };

    const auto it = kExtra.constFind(n);
    if (it != kExtra.constEnd())
        return *it;

    return WorldMapRenderer::blockColor(n);
}

// ── 等距投影 ─────────────────────────────────────────────────────────
struct IsoProjector
{
    double scale = 1.0;
    double originX = 0.0;
    double originY = 0.0;

    void project(double x, double y, double z, double &sx, double &sy) const
    {
        static const double kCos = 0.8660254037844386;
        static const double kSin = 0.5;
        sx = originX + (x - z) * kCos * scale;
        sy = originY + ((x + z) * kSin - y) * scale;
    }
};

struct Voxel
{
    int x = 0, y = 0, z = 0;
    QColor color;
    int blockKey = -1;   ///< blockKeys 下标，-1 无
};

inline int depthKey(const Voxel &v) { return v.x + v.y + v.z; }

/**
 * 不遮挡邻居的方块集合（叶子/玻璃/台阶/门等）。
 * 逻辑与列表数据来自 EngineHub/SchematicWebViewer（MIT）
 * https://github.com/EngineHub/SchematicWebViewer
 * —— 遮挡剔除：仅当 6 邻均为「实心且遮挡」方块时才跳过绘制。
 */
const QSet<QString> &nonOccludingBlocks()
{
    static QSet<QString> s_set = [] {
        QSet<QString> set;
        // 不可见方块（与 SchematicWebViewer INVISIBLE_BLOCKS 一致）
        set.insert(QStringLiteral("air"));
        set.insert(QStringLiteral("cave_air"));
        set.insert(QStringLiteral("void_air"));
        set.insert(QStringLiteral("structure_void"));
        set.insert(QStringLiteral("barrier"));
        set.insert(QStringLiteral("light"));
        QFile f(QStringLiteral(":/resources/nonOccluding.json"));
        if (f.open(QIODevice::ReadOnly))
        {
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isArray())
            {
                const QJsonArray arr = doc.array();
                set.reserve(set.size() + arr.size());
                for (const QJsonValue &v : arr)
                {
                    if (v.isString())
                        set.insert(v.toString());
                }
            }
        }
        return set;
    }();
    return s_set;
}

inline bool isNonOccludingName(const QString &blockKey)
{
    return nonOccludingBlocks().contains(blockKey);
}

bool isSolidAt(const QVector<int> &grid, const QVector<bool> &occluding,
               int sx, int sy, int sz, int x, int y, int z)
{
    if (x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz)
        return false;
    const int layer = sx * sz;
    const int idx = grid.at(y * layer + z * sx + x);
    if (idx <= 0)
        return false;
    return idx < occluding.size() ? occluding.at(idx) : true;
}

bool isBuried(const QVector<int> &grid, const QVector<bool> &occluding,
              int sx, int sy, int sz, int x, int y, int z)
{
    return isSolidAt(grid, occluding, sx, sy, sz, x + 1, y, z)
        && isSolidAt(grid, occluding, sx, sy, sz, x - 1, y, z)
        && isSolidAt(grid, occluding, sx, sy, sz, x, y + 1, z)
        && isSolidAt(grid, occluding, sx, sy, sz, x, y - 1, z)
        && isSolidAt(grid, occluding, sx, sy, sz, x, y, z + 1)
        && isSolidAt(grid, occluding, sx, sy, sz, x, y, z - 1);
}

QString coverCacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                        + QStringLiteral("/schematic_covers");
    QDir().mkpath(dir);
    return dir;
}

int chooseStride(int sx, int sy, int sz)
{
    const int maxDim = qMax(sx, qMax(sy, sz));
    if (maxDim <= kMaxLinearDim)
        return kMinStride;
    int stride = kMinStride;
    while ((maxDim + stride - 1) / stride > kMaxLinearDim && stride < kMaxStride)
        ++stride;
    return stride;
}

// ── 材质源：与投影编辑器同源的 InstanceTextureLoader 图集 ──────────
// 只加载一次 jar 图集（串行，避免并发解压 jar 导致「只有一个投影有材质」），
// 之后按方块名从图集裁 16×16 面片；资源包可覆盖单个贴图。

struct FaceTex
{
    QImage side;
    QImage top;
    bool ok = false;
};

struct TextureBank
{
    InstanceTextureLoader *atlas = nullptr;  ///< 进程内共享图集（与编辑器同一份）
    QHash<QString, FaceTex> faces;        ///< 已裁好的面片缓存
    QSet<QString> missing;
    QStringList packRoots;                ///< 资源包根（zip/目录）
    bool atlasReady = false;
    bool packsScanned = false;
    bool loaded = false;                  ///< bank 已创建
};

QMutex g_bankMutex;
QHash<QString, QSharedPointer<TextureBank>> g_banks;

/** 解析客户端 jar 所在目录（InstanceTextureLoader 要求目录内含 jar） */
QString findClientJarDir(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return QString();

    auto hasJar = [](const QString &dirPath) {
        QDir d(dirPath);
        return !d.entryList(QStringList() << QStringLiteral("*.jar"), QDir::Files).isEmpty();
    };
    if (hasJar(instancePath))
        return QFileInfo(instancePath).absoluteFilePath();

    auto scanVersions = [&](const QString &rootPath) -> QString {
        QDir vroot(rootPath);
        if (!vroot.cd(QStringLiteral("versions")))
            return QString();
        const QFileInfoList vers = vroot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                       QDir::Name);
        // 优先与目录同名的 jar（标准客户端）
        for (const QFileInfo &vfi : vers)
        {
            QDir vd(vfi.absoluteFilePath());
            const QFileInfoList jars = vd.entryInfoList(QStringList() << QStringLiteral("*.jar"),
                                                        QDir::Files, QDir::Size);
            for (const QFileInfo &jfi : jars)
            {
                if (jfi.completeBaseName() == vfi.fileName())
                    return vfi.absoluteFilePath();
            }
        }
        // 否则取最大 jar 所在目录
        QString bestDir;
        qint64 bestSize = -1;
        for (const QFileInfo &vfi : vers)
        {
            QDir vd(vfi.absoluteFilePath());
            const QFileInfoList jars = vd.entryInfoList(QStringList() << QStringLiteral("*.jar"),
                                                        QDir::Files, QDir::Size);
            for (const QFileInfo &jfi : jars)
            {
                if (jfi.size() > bestSize)
                {
                    bestSize = jfi.size();
                    bestDir = vfi.absoluteFilePath();
                }
            }
        }
        return bestDir;
    };

    const QString a = scanVersions(instancePath);
    if (!a.isEmpty())
        return a;
    if (QDir(instancePath).cd(QStringLiteral(".minecraft")))
    {
        const QString b = scanVersions(instancePath + QStringLiteral("/.minecraft"));
        if (!b.isEmpty())
            return b;
    }
    return QString();
}

QStringList collectResourcePackRoots(const QString &instancePath)
{
    QStringList roots;
    if (instancePath.isEmpty())
        return roots;

    const QStringList packDirs = {
        instancePath + QStringLiteral("/resourcepacks"),
        instancePath + QStringLiteral("/.minecraft/resourcepacks"),
    };
    for (const QString &pd : packDirs)
    {
        QDir dir(pd);
        if (!dir.exists())
            continue;
        const QFileInfoList zips = dir.entryInfoList(QStringList() << QStringLiteral("*.zip"),
                                                     QDir::Files, QDir::Time);
        for (auto it = zips.crbegin(); it != zips.crend(); ++it)
            roots.append(it->absoluteFilePath());
        const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                     QDir::Time);
        for (int i = dirs.size() - 1; i >= 0; --i)
            roots.append(dirs.at(i).absoluteFilePath());
    }
    return roots;
}

bool loadFaceFromZip(const QString &zipPath, const QString &block, FaceTex &out)
{
    auto read = [&](const QString &name) -> QImage {
        const QString rel = QStringLiteral("assets/minecraft/textures/block/%1.png").arg(name);
        QByteArray data;
        if (!JarUtils::extractFromJarToMemory(zipPath, rel, data) || data.isEmpty())
            return QImage();
        QImage img;
        if (!img.loadFromData(data))
            return QImage();
        if (img.width() != 16 || img.height() != 16)
            img = img.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        return img;
    };
    out.side = read(block);
    if (out.side.isNull())
        return false;
    out.top = read(block + QStringLiteral("_top"));
    if (out.top.isNull())
        out.top = out.side;
    out.ok = true;
    return true;
}

bool loadFaceFromDir(const QString &root, const QString &block, FaceTex &out)
{
    const QString base = root + QStringLiteral("/assets/minecraft/textures/block/");
    auto read = [&](const QString &name) -> QImage {
        QImage img(base + name + QStringLiteral(".png"));
        if (img.isNull())
            return QImage();
        if (img.width() != 16 || img.height() != 16)
            img = img.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        return img;
    };
    out.side = read(block);
    if (out.side.isNull())
        return false;
    out.top = read(block + QStringLiteral("_top"));
    if (out.top.isNull())
        out.top = out.side;
    out.ok = true;
    return true;
}

/** 从 InstanceTextureLoader 图集裁一块 16×16 贴图（与编辑器同源） */
QImage cropAtlasTile(const InstanceTextureLoader &loader, const QString &blockName)
{
    if (!loader.isValid())
        return QImage();
    int tile = -1;
    if (!loader.textureFor(blockName, tile))
        return QImage();
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    loader.tileUv(tile, u0, v0, u1, v1);
    const QImage atlas = loader.atlasImage();
    if (atlas.isNull() || atlas.width() <= 0 || atlas.height() <= 0)
        return QImage();
    const int x0 = qBound(0, int(u0 * atlas.width()), atlas.width() - 1);
    const int y0 = qBound(0, int(v0 * atlas.height()), atlas.height() - 1);
    const int x1 = qBound(x0 + 1, int(u1 * atlas.width() + 0.5f), atlas.width());
    const int y1 = qBound(y0 + 1, int(v1 * atlas.height() + 0.5f), atlas.height());
    QImage tileImg = atlas.copy(x0, y0, x1 - x0, y1 - y0);
    if (tileImg.isNull())
        return QImage();
    if (tileImg.width() != 16 || tileImg.height() != 16)
        tileImg = tileImg.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    return tileImg;
}

QSharedPointer<TextureBank> bankForInstance(const QString &instancePath)
{
    const QString key = instancePath.isEmpty()
        ? QStringLiteral("__color_only__")
        : QFileInfo(instancePath).absoluteFilePath();

    QMutexLocker lock(&g_bankMutex);
    auto it = g_banks.constFind(key);
    if (it != g_banks.constEnd() && !it->isNull() && (*it)->loaded)
        return *it;

    auto bank = QSharedPointer<TextureBank>::create();
    bank->loaded = true;
    g_banks.insert(key, bank);
    return bank;
}

/**
 * 确保 bank 里的贴图就绪。
 * 关键：图集只加载一次，jar 解压全程持锁串行 —— 多个封面并发渲染共享同一图集，
 * 不会再出现「只有一个投影有材质」。
 */
void ensureTexturesFor(const QString &instancePath, const QStringList &blockNames)
{
    if (instancePath.isEmpty() || blockNames.isEmpty())
        return;

    auto bank = bankForInstance(instancePath);

    QMutexLocker lock(&g_bankMutex);

    // 1) 首次：优先复用编辑器同一份共享图集（磁盘缓存/内存缓存），失败再按目录兜底
    if (!bank->atlasReady)
    {
        bank->atlas = InstanceTextureLoader::sharedForInstance(instancePath);
        if (!bank->atlas)
        {
            const QString jarDir = findClientJarDir(instancePath);
            if (!jarDir.isEmpty())
            {
                bank->atlas = InstanceTextureLoader::sharedForInstance(jarDir);
                if (!bank->atlas)
                {
                    // 兼容：jarDir 就是实例根但 jar 在子目录时上面已扫过；再试直接当 versions 目录
                    bank->atlas = InstanceTextureLoader::sharedForInstance(instancePath);
                }
            }
        }
        bank->atlasReady = true;
    }

    // 2) 资源包列表（只扫一次）
    if (!bank->packsScanned)
    {
        bank->packRoots = collectResourcePackRoots(instancePath);
        bank->packsScanned = true;
    }

    // 3) 按需补面片：资源包覆盖 → 图集
    for (const QString &block : blockNames)
    {
        if (bank->faces.contains(block) || bank->missing.contains(block))
            continue;
        FaceTex ft;
        bool ok = false;
        for (const QString &root : bank->packRoots)
        {
            if (root.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
                ok = loadFaceFromZip(root, block, ft);
            else
                ok = loadFaceFromDir(root, block, ft);
            if (ok)
                break;
        }
        if (!ok && bank->atlas && bank->atlas->isValid())
        {
            ft.side = cropAtlasTile(*bank->atlas, block);
            if (!ft.side.isNull())
            {
                ft.top = cropAtlasTile(*bank->atlas, block + QStringLiteral("_top"));
                if (ft.top.isNull())
                    ft.top = ft.side;
                ft.ok = true;
                ok = true;
            }
        }
        if (ok)
            bank->faces.insert(block, ft);
        else
            bank->missing.insert(block);
    }
}

void drawIsoCubeColor(QPainter &p, double sx, double sy, double scale,
                      const QColor &base)
{
    static const double kCos = 0.8660254037844386;
    static const double kSin = 0.5;
    const double hw = kCos * scale;
    const double hh = kSin * scale;
    const double vd = scale;

    const QPointF top(sx, sy);
    const QPointF right(sx + hw, sy + hh);
    const QPointF bottom(sx, sy + 2 * hh);
    const QPointF left(sx - hw, sy + hh);

    QPainterPath topPath;
    topPath.moveTo(top);
    topPath.lineTo(right);
    topPath.lineTo(bottom);
    topPath.lineTo(left);
    topPath.closeSubpath();
    p.fillPath(topPath, base.lighter(118));

    QPainterPath leftPath;
    leftPath.moveTo(left);
    leftPath.lineTo(bottom);
    leftPath.lineTo(bottom.x(), bottom.y() + vd);
    leftPath.lineTo(left.x(), left.y() + vd);
    leftPath.closeSubpath();
    p.fillPath(leftPath, base.darker(125));

    QPainterPath rightPath;
    rightPath.moveTo(bottom);
    rightPath.lineTo(right);
    rightPath.lineTo(right.x(), right.y() + vd);
    rightPath.lineTo(bottom.x(), bottom.y() + vd);
    rightPath.closeSubpath();
    p.fillPath(rightPath, base.darker(155));
}

/** 将 16×16 贴图仿射映射到平行四边形面（单位方 → 屏幕） */
void drawFaceTextured(QPainter &p, const QPointF &o, const QPointF &ex, const QPointF &ey,
                      const QImage &tex, qreal bright)
{
    if (tex.isNull())
        return;

    QImage src = tex;
    if (!qFuzzyCompare(bright, qreal(1.0)))
    {
        src = tex.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < src.height(); ++y)
        {
            auto *line = reinterpret_cast<QRgb *>(src.scanLine(y));
            for (int x = 0; x < src.width(); ++x)
            {
                const QRgb px = line[x];
                const int r = qBound(0, int(qRed(px) * bright), 255);
                const int g = qBound(0, int(qGreen(px) * bright), 255);
                const int b = qBound(0, int(qBlue(px) * bright), 255);
                line[x] = qRgba(r, g, b, qAlpha(px));
            }
        }
    }

    const double ax = ex.x() - o.x(), ay = ex.y() - o.y();
    const double bx = ey.x() - o.x(), by = ey.y() - o.y();
    QTransform map;
    map.setMatrix(ax, ay, 0,
                  bx, by, 0,
                  o.x(), o.y(), 1);
    p.save();
    p.setWorldTransform(map, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(QRectF(0, 0, 1, 1), src);
    p.restore();
}

void drawIsoCubeTextured(QPainter &p, double sx, double sy, double scale,
                         const FaceTex &ft, const QColor &fallback)
{
    static const double kCos = 0.8660254037844386;
    static const double kSin = 0.5;
    const double hw = kCos * scale;
    const double hh = kSin * scale;
    const double vd = scale;

    const QPointF top(sx, sy);
    const QPointF right(sx + hw, sy + hh);
    const QPointF bottom(sx, sy + 2 * hh);
    const QPointF left(sx - hw, sy + hh);
    const QPointF leftDown(left.x(), left.y() + vd);
    const QPointF bottomDown(bottom.x(), bottom.y() + vd);
    const QPointF rightDown(right.x(), right.y() + vd);

    // 顶面
    if (!ft.top.isNull())
        drawFaceTextured(p, top, right, left, ft.top, 1.12);
    else
    {
        QPainterPath path;
        path.moveTo(top); path.lineTo(right); path.lineTo(bottom); path.lineTo(left);
        path.closeSubpath();
        p.fillPath(path, fallback.lighter(118));
    }

    // 左面
    if (!ft.side.isNull())
        drawFaceTextured(p, left, bottom, leftDown, ft.side, 0.80);
    else
    {
        QPainterPath path;
        path.moveTo(left); path.lineTo(bottom); path.lineTo(bottomDown); path.lineTo(leftDown);
        path.closeSubpath();
        p.fillPath(path, fallback.darker(125));
    }

    // 右面
    if (!ft.side.isNull())
        drawFaceTextured(p, bottom, right, bottomDown, ft.side, 0.62);
    else
    {
        QPainterPath path;
        path.moveTo(bottom); path.lineTo(right); path.lineTo(rightDown); path.lineTo(bottomDown);
        path.closeSubpath();
        p.fillPath(path, fallback.darker(155));
    }
}

QString ensureLitematic(const QString &path)
{
    const QString lowered = path.toLower();
    if (lowered.endsWith(QLatin1String(".litematic")) || lowered.endsWith(QLatin1String(".nbt")))
        return path;

    static QAtomicInt s_tmpSeq{0};
    const QString tmpDir = QDir::tempPath() + QStringLiteral("/blockbox_cover");
    QDir().mkpath(tmpDir);
    const QString tmp = QString("%1/%2_%3.litematic")
                            .arg(tmpDir)
                            .arg(QFileInfo(path).completeBaseName())
                            .arg(s_tmpSeq.fetchAndAddRelaxed(1));

    QString err;
    bool ok = false;
    if (lowered.endsWith(QLatin1String(".schematic")))
        ok = LitematicEditor::convertFromSchematica(path, tmp, &err);
    else if (lowered.endsWith(QLatin1String(".schem")))
        ok = LitematicEditor::convertFromSponge(path, tmp, &err);
    else
        return path;

    if (!ok)
    {
        QFile::remove(tmp);
        return QString();
    }
    return tmp;
}

} // namespace

// ============================================================================
// SchematicCoverRenderer
// ============================================================================

SchematicCoverRenderer::SchematicCoverRenderer(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SchematicCoverRenderer::CoverResult>("SchematicCoverRenderer::CoverResult");
    connect(this, &SchematicCoverRenderer::coverReady,
            this, &SchematicCoverRenderer::onCoverReady,
            Qt::QueuedConnection);
}

SchematicCoverRenderer::~SchematicCoverRenderer() = default;

SchematicCoverRenderer *SchematicCoverRenderer::instance()
{
    static SchematicCoverRenderer *s_inst = nullptr;
    if (!s_inst)
        s_inst = new SchematicCoverRenderer(QCoreApplication::instance());
    return s_inst;
}

void SchematicCoverRenderer::setTextureInstance(const QString &instancePath)
{
    const QString norm = instancePath.isEmpty()
        ? QString()
        : QFileInfo(instancePath).absoluteFilePath();
    if (m_textureInstance == norm)
        return;
    m_textureInstance = norm;
    m_jobSeq.ref();
}

QString SchematicCoverRenderer::cacheFilePath(const QString &filePath, int width, int height,
                                              const QString &instancePath)
{
    const QFileInfo fi(filePath);
    if (!fi.exists())
        return QString();

    const QString instKey = instancePath.isEmpty()
        ? QString()
        : QFileInfo(instancePath).absoluteFilePath();
    const QString seed = QStringLiteral("%1|%2|%3|%4x%5|%6|%7")
                             .arg(fi.absoluteFilePath())
                             .arg(fi.size())
                             .arg(fi.lastModified().toMSecsSinceEpoch())
                             .arg(width).arg(height)
                             .arg(kCacheFormatVersion)
                             .arg(instKey);
    const QByteArray hash = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha1)
                                .toHex()
                                .left(16);
    return coverCacheDir() + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".png");
}

void SchematicCoverRenderer::request(const QString &filePath, int width, int height,
                                     SchematicCoverCallback callback)
{
    if (filePath.isEmpty() || width <= 0 || height <= 0)
        return;

    const QString inst = m_textureInstance;
    const QString cachePath = cacheFilePath(filePath, width, height, inst);
    if (!cachePath.isEmpty() && QFile::exists(cachePath))
    {
        QImage img(cachePath);
        if (!img.isNull())
        {
            if (callback)
                callback(filePath, img, true);
            return;
        }
    }

    Pending pend;
    pend.callback = std::move(callback);
    pend.width = width;
    pend.height = height;
    m_pending[filePath].append(pend);

    if (m_inflight.contains(filePath))
        return;
    m_inflight.insert(filePath);

    const int seq = m_jobSeq.loadAcquire();
    (void)QtConcurrent::run(QThreadPool::globalInstance(),
                            [this, filePath, width, height, inst, seq]() {
        if (seq != m_jobSeq.loadAcquire())
        {
            CoverResult cancel;
            cancel.filePath = filePath;
            cancel.done = true;
            emit coverReady(cancel);
            return;
        }

        // 阶段 1：色块轮廓（快）
        const QImage colorImg = renderInWorker(filePath, width, height, inst, false);
        CoverResult r1;
        r1.filePath = filePath;
        r1.image = colorImg;
        r1.textured = false;
        // 有材质阶段时色块版是中间态；无实例则本次结束
        r1.done = inst.isEmpty();
        if (seq == m_jobSeq.loadAcquire())
            emit coverReady(r1);

        // 阶段 2：材质细化
        if (!inst.isEmpty())
        {
            if (seq != m_jobSeq.loadAcquire())
            {
                CoverResult cancel;
                cancel.filePath = filePath;
                cancel.done = true;
                emit coverReady(cancel);
                return;
            }
            const QImage texImg = renderInWorker(filePath, width, height, inst, true);
            CoverResult r2;
            r2.filePath = filePath;
            r2.image = texImg;
            r2.textured = true;
            r2.done = true;
            if (seq == m_jobSeq.loadAcquire())
                emit coverReady(r2);
        }
    });
}

void SchematicCoverRenderer::onCoverReady(const CoverResult &result)
{
    const QList<Pending> pending = m_pending.value(result.filePath);
    for (const Pending &p : pending)
    {
        if (p.callback)
            p.callback(result.filePath, result.image, result.textured);
    }

    if (result.done)
    {
        m_pending.remove(result.filePath);
        m_inflight.remove(result.filePath);
    }
}

QImage SchematicCoverRenderer::renderSync(const QString &filePath, int width, int height,
                                          const QString &instancePath, bool textured)
{
    if (textured)
    {
        const QString cachePath = cacheFilePath(filePath, width, height, instancePath);
        if (!cachePath.isEmpty() && QFile::exists(cachePath))
        {
            QImage img(cachePath);
            if (!img.isNull())
                return img;
        }
    }
    return renderInWorker(filePath, width, height, instancePath, textured);
}

QImage SchematicCoverRenderer::renderInWorker(const QString &filePath, int width, int height,
                                              const QString &instancePath, bool textured)
{
    try
    {
        QImage img = renderFromDocument(filePath, width, height, instancePath, textured);
        if (!img.isNull() && textured)
        {
            const QString cachePath = cacheFilePath(filePath, width, height, instancePath);
            if (!cachePath.isEmpty())
                img.save(cachePath, "PNG");
        }
        return img;
    }
    catch (...)
    {
        qWarning() << "SchematicCoverRenderer: unexpected exception for" << filePath;
        return QImage();
    }
}

QImage SchematicCoverRenderer::renderFromDocument(const QString &filePath, int width, int height,
                                                  const QString &instancePath, bool textured)
{
    QString loadPath = filePath;
    QString tmpConverted;
    const QString lowered = filePath.toLower();
    if (lowered.endsWith(QLatin1String(".schematic")) || lowered.endsWith(QLatin1String(".schem")))
    {
        tmpConverted = ensureLitematic(filePath);
        if (tmpConverted.isEmpty())
            return QImage();
        loadPath = tmpConverted;
    }

    SchematicDocument doc;
    QString err;
    if (!doc.load(loadPath, &err) || !doc.isLoaded())
    {
        if (!tmpConverted.isEmpty())
            QFile::remove(tmpConverted);
        return QImage();
    }

    const int sx = doc.sizeX();
    const int sy = doc.sizeY();
    const int sz = doc.sizeZ();
    if (sx <= 0 || sy <= 0 || sz <= 0)
    {
        if (!tmpConverted.isEmpty())
            QFile::remove(tmpConverted);
        return QImage();
    }

    const int volume = sx * sy * sz;
    if (volume <= 0 || volume > 16 * 1000 * 1000)
    {
        if (!tmpConverted.isEmpty())
            QFile::remove(tmpConverted);
        return QImage();
    }

    QVector<int> grid(volume, 0);
    QVector<QColor> paletteColors;
    paletteColors.append(QColor());
    // 遮挡标记：index 0 = 空气不遮挡
    QVector<bool> paletteOccluding;
    paletteOccluding.append(false);
    QStringList blockKeys;
    QHash<QString, int> nameToIdx;

    int nonAir = 0;
    int minX = sx, minY = sy, minZ = sz;
    int maxX = -1, maxY = -1, maxZ = -1;
    const int layer = sx * sz;

    for (int y = 0; y < sy; ++y)
    {
        for (int z = 0; z < sz; ++z)
        {
            for (int x = 0; x < sx; ++x)
            {
                const int gi = y * layer + z * sx + x;
                const SchematicBlockState st = doc.blockAt(gi);
                if (st.isAir())
                    continue;
                const QString key = normalizeBlockKey(st.name);
                // 完全不可见方块不画（逻辑对齐 SchematicWebViewer INVISIBLE_BLOCKS）
                if (key == QLatin1String("barrier") || key == QLatin1String("light")
                    || key == QLatin1String("structure_void")
                    || key == QLatin1String("cave_air") || key == QLatin1String("void_air"))
                    continue;

                int idx = nameToIdx.value(st.name, 0);
                if (idx <= 0)
                {
                    paletteColors.append(coverBlockColor(st.name));
                    idx = paletteColors.size() - 1;
                    nameToIdx.insert(st.name, idx);
                    blockKeys.append(key);
                    // 非遮挡方块（叶子/玻璃/台阶等）不挡住邻居 —— SchematicWebViewer 遮挡逻辑
                    paletteOccluding.append(!isNonOccludingName(key));
                }
                grid[gi] = idx;
                ++nonAir;
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (z < minZ) minZ = z;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
                if (z > maxZ) maxZ = z;
            }
        }
    }

    if (!tmpConverted.isEmpty())
        QFile::remove(tmpConverted);

    if (nonAir == 0 || maxX < 0)
        return QImage();

    // 材质按需加载
    QSharedPointer<TextureBank> bank;
    if (textured && !instancePath.isEmpty())
    {
        ensureTexturesFor(instancePath, blockKeys);
        bank = bankForInstance(instancePath);
    }

    // 采样：步长自适应，保证整图包围盒覆盖 min/max 结构角
    int stride = chooseStride(sx, sy, sz);
    auto collect = [&](int step, QList<Voxel> &out) {
        out.clear();
        out.reserve(4096);
        const int st = qMax(1, step);
        for (int y = minY; y <= maxY; y += st)
            for (int z = minZ; z <= maxZ; z += st)
                for (int x = minX; x <= maxX; x += st)
                {
                    const int idx = grid.at(y * layer + z * sx + x);
                    if (idx <= 0)
                        continue;
                    // 遮挡：6 邻均为「实心且遮挡」才跳过（叶子/玻璃等不藏邻居）
                    if (st == 1 && isBuried(grid, paletteOccluding, sx, sy, sz, x, y, z))
                        continue;
                    Voxel v;
                    v.x = x;
                    v.y = y;
                    v.z = z;
                    v.color = idx < paletteColors.size() ? paletteColors.at(idx)
                                                         : QColor("#9C7F4E");
                    v.blockKey = idx - 1; // blockKeys 与 paletteColors[1..] 对齐
                    out.append(v);
                    if (out.size() >= kMaxSampledVoxels)
                        return;
                }
    };

    QList<Voxel> voxels;
    collect(stride, voxels);
    while (voxels.size() >= kMaxSampledVoxels && stride < kMaxStride)
    {
        ++stride;
        collect(stride, voxels);
    }

    if (voxels.isEmpty())
    {
        for (int y = minY; y <= maxY && voxels.size() < kMaxSampledVoxels; y += stride)
            for (int z = minZ; z <= maxZ && voxels.size() < kMaxSampledVoxels; z += stride)
                for (int x = minX; x <= maxX && voxels.size() < kMaxSampledVoxels; x += stride)
                {
                    const int idx = grid.at(y * layer + z * sx + x);
                    if (idx <= 0)
                        continue;
                    Voxel v;
                    v.x = x; v.y = y; v.z = z;
                    v.color = idx < paletteColors.size() ? paletteColors.at(idx)
                                                         : QColor("#9C7F4E");
                    v.blockKey = idx - 1;
                    voxels.append(v);
                }
        if (voxels.isEmpty())
            return QImage();
    }

    std::sort(voxels.begin(), voxels.end(),
              [](const Voxel &a, const Voxel &b) { return depthKey(a) < depthKey(b); });

    // 完整投影：结构 8 角点 + 立方体几何外扩
    static const double kCos = 0.8660254037844386;
    static const double kSin = 0.5;
    IsoProjector probe;
    double minXs = 1e300, maxXs = -1e300, minYs = 1e300, maxYs = -1e300;
    const int corners[8][3] = {
        {minX, minY, minZ}, {maxX + 1, minY, minZ},
        {minX, minY, maxZ + 1}, {maxX + 1, minY, maxZ + 1},
        {minX, maxY + 1, minZ}, {maxX + 1, maxY + 1, minZ},
        {minX, maxY + 1, maxZ + 1}, {maxX + 1, maxY + 1, maxZ + 1},
    };
    for (const auto &c : corners)
    {
        double px, py;
        probe.project(c[0], c[1], c[2], px, py);
        minXs = qMin(minXs, px - kCos);
        maxXs = qMax(maxXs, px + kCos);
        minYs = qMin(minYs, py - kSin);
        maxYs = qMax(maxYs, py + kSin + 1.0);
    }
    const double boxW = qMax(1.0, maxXs - minXs);
    const double boxH = qMax(1.0, maxYs - minYs);

    const int pad = 4;
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return QImage();
    image.fill(Qt::transparent);

    IsoProjector proj;
    proj.scale = qMin((width - pad * 2) / boxW, (height - pad * 2) / boxH);
    proj.originX = width * 0.5 - ((minXs + maxXs) * 0.5) * proj.scale;
    proj.originY = height * 0.5 - ((minYs + maxYs) * 0.5) * proj.scale;

    QPainter p;
    if (!p.begin(&image))
        return QImage();
    p.setRenderHint(QPainter::Antialiasing, false);

    const bool useTex = textured && bank && proj.scale >= kTextureDrawMinScale;
    QHash<QString, FaceTex> localFaces;
    if (useTex)
    {
        QMutexLocker lock(&g_bankMutex);
        localFaces = bank->faces;
    }

    for (const Voxel &v : voxels)
    {
        double sxp, syp;
        proj.project(v.x, v.y, v.z, sxp, syp);

        bool drewTex = false;
        if (useTex && v.blockKey >= 0 && v.blockKey < blockKeys.size())
        {
            const FaceTex ft = localFaces.value(blockKeys.at(v.blockKey));
            if (ft.ok)
            {
                drawIsoCubeTextured(p, sxp, syp, proj.scale, ft, v.color);
                drewTex = true;
            }
        }
        if (!drewTex)
            drawIsoCubeColor(p, sxp, syp, proj.scale, v.color);
    }
    p.end();
    return image;
}
