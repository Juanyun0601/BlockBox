/**
 * @file   projection_entity_probe.cpp
 * @brief  投影生物模型探针：把实体模型表渲染成一张对照图（软件渲染，无需 GPU）
 *
 * 用途：验证 EntityModelLoader 解析出来的基岩版几何（骨骼旋转 / 盒子 UV /
 * 贴图映射 / 接地高度）是否正确。贴图从实例 jar 解压出来的实体贴图目录取。
 *
 * 用法：
 *   projection_entity_probe --tex <实体贴图目录> --out out.png
 *                            [--entities pig,cow,zombie] [--size 240]
 *
 * 仅开发期使用，不进正式构建。
 */

#include "utils/Schematic/EntityModelLoader.h"
#include "utils/Schematic/InstanceEntityTextures.h"
#include "utils/Schematic/SchematicDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace {

/** 实体的读 / 写往返自检：造一份带生物的投影、写盘、再读回来比对 */
bool runEntityRoundTrip()
{
    SchematicDocument doc;
    if (!doc.createEmpty(6, 6, 6))
    {
        qWarning("自检失败：造不出空投影");
        return false;
    }
    QList<SchematicEntity> entities;
    {
        SchematicEntity e;
        e.id = QStringLiteral("minecraft:pig");
        e.x = 1.5;
        e.y = 0.0;
        e.z = 2.25;
        e.yaw = 90.0f;
        entities.append(e);
    }
    {
        SchematicEntity e;
        e.id = QStringLiteral("minecraft:zombie");
        e.x = 3.0;
        e.y = 0.0;
        e.z = 4.0;
        e.yaw = 180.0f;
        entities.append(e);
    }
    doc.setEntities(entities);

    const QString tmpPath = QDir::tempPath() + QStringLiteral("/blockbox_entity_roundtrip.litematic");
    QString error;
    if (!doc.saveCopy(tmpPath, &error))
    {
        qWarning("自检失败：写盘失败 %s", qPrintable(error));
        return false;
    }

    SchematicDocument reloaded;
    if (!reloaded.load(tmpPath, &error))
    {
        qWarning("自检失败：读回失败 %s", qPrintable(error));
        return false;
    }
    QFile::remove(tmpPath);

    const QList<SchematicEntity> &back = reloaded.entities();
    qInfo("自检：写入 %d 个实体，读回 %d 个", int(entities.size()), int(back.size()));
    bool ok = back.size() == entities.size();
    for (int i = 0; ok && i < back.size(); ++i)
    {
        const SchematicEntity &a = entities.at(i);
        const SchematicEntity &b = back.at(i);
        qInfo("  %s -> %s  pos=(%.2f,%.2f,%.2f) yaw=%.0f", qPrintable(a.id), qPrintable(b.id),
              b.x, b.y, b.z, double(b.yaw));
        ok = b.id.compare(a.id, Qt::CaseInsensitive) == 0
             && qAbs(b.x - a.x) < 0.01 && qAbs(b.y - a.y) < 0.01 && qAbs(b.z - a.z) < 0.01
             && qAbs(b.yaw - a.yaw) < 0.01;
    }
    qInfo("自检结果：%s", ok ? "通过" : "不通过");
    return ok;
}

/** 一张贴上模型用的贴图（按 "entity/pig/pig" 这样的键索引） */
struct TexturePool
{
    QHash<QString, QImage> byKey;   // 完整键（去 .png）
    QHash<QString, QImage> byLeaf;  // 末段名

    bool find(const QString &keyIn, QImage &out) const
    {
        QString key = keyIn.trimmed().toLower();
        if (key.startsWith(QLatin1String("textures/")))
            key = key.mid(9);
        if (key.endsWith(QLatin1String(".png")))
            key.chop(4);
        auto it = byKey.constFind(key);
        if (it != byKey.constEnd())
        {
            out = it.value();
            return true;
        }
        const int slash = key.lastIndexOf(QLatin1Char('/'));
        const QString leaf = slash >= 0 ? key.mid(slash + 1) : key;
        it = byLeaf.constFind(leaf);
        if (it != byLeaf.constEnd())
        {
            out = it.value();
            return true;
        }
        return false;
    }
};

TexturePool loadTextures(const QString &dir)
{
    TexturePool pool;
    QDir root(dir);
    const QFileInfoList files = root.entryInfoList(QStringList() << "*.png", QDir::Files,
                                                   QDir::Name);
    for (const QFileInfo &fi : files)
    {
        QImage img(fi.absoluteFilePath());
        if (img.isNull())
            continue;
        const QString key = QStringLiteral("entity/") + fi.completeBaseName().toLower();
        pool.byKey.insert(key, img);
        pool.byLeaf.insert(fi.completeBaseName().toLower(), img);
    }
    // 子目录：entity/pig/pig.png -> "entity/pig/pig"
    const QFileInfoList subs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &sub : subs)
    {
        const QString subName = sub.fileName().toLower();
        const QFileInfoList inner = QDir(sub.absoluteFilePath())
                                        .entryInfoList(QStringList() << "*.png", QDir::Files,
                                                       QDir::Name);
        for (const QFileInfo &fi : inner)
        {
            QImage img(fi.absoluteFilePath());
            if (img.isNull())
                continue;
            const QString leaf = fi.completeBaseName().toLower();
            pool.byKey.insert(QStringLiteral("entity/") + subName + QLatin1Char('/') + leaf, img);
            if (!pool.byLeaf.contains(leaf))
                pool.byLeaf.insert(leaf, img);
        }
    }
    return pool;
}

/** 三点点对映射：把源三角形（贴图像素）贴到目标三角形（屏幕），仿射足够 */
QTransform triangleTransform(const QPointF src[3], const QPointF dst[3])
{
    auto basis = [](const QPointF p[3]) {
        const QPointF e1 = p[1] - p[0];
        const QPointF e2 = p[2] - p[0];
        return QTransform(e1.x(), e1.y(), e2.x(), e2.y(), p[0].x(), p[0].y());
    };
    const QTransform a = basis(src);
    bool ok = false;
    const QTransform aInv = a.inverted(&ok);
    if (!ok)
        return QTransform();
    // Qt 的 A * B 表示先应用 B 再应用 A：这里要「先把贴图坐标搬回单位三角形，再贴到屏幕上」
    return aInv * basis(dst);
}

/** 投影一个模型空间点（格）到屏幕（正交，绕 Y 转 yaw 再俯仰） */
QPointF project(const QVector3D &p, float yawDeg, float pitchDeg, const QPointF &center,
                float scale)
{
    const float yaw = qDegreesToRadians(yawDeg);
    const float pitch = qDegreesToRadians(pitchDeg);
    const float cy = qCos(yaw), sy = qSin(yaw);
    const float cp = qCos(pitch), sp = qSin(pitch);
    // 先绕 Y 转
    const float x1 = p.x() * cy + p.z() * sy;
    const float z1 = -p.x() * sy + p.z() * cy;
    const float y1 = p.y();
    // 再绕 X 俯仰
    const float y2 = y1 * cp - z1 * sp;
    const float z2 = y1 * sp + z1 * cp;
    Q_UNUSED(z2);
    return QPointF(center.x() + x1 * scale, center.y() - y2 * scale);
}

/** 深度：越大越远 */
float depthOf(const QVector3D &p, float yawDeg, float pitchDeg)
{
    const float yaw = qDegreesToRadians(yawDeg);
    const float pitch = qDegreesToRadians(pitchDeg);
    const float cy = qCos(yaw), sy = qSin(yaw);
    const float cp = qCos(pitch), sp = qSin(pitch);
    const float x1 = p.x() * cy + p.z() * sy;
    const float z1 = -p.x() * sy + p.z() * cy;
    const float y1 = p.y();
    const float z2 = y1 * sp + z1 * cp;
    Q_UNUSED(x1);
    return z2;
}

QImage renderEntity(const EntityModel &model, const QImage &texture, int size,
                    float yawDeg, float pitchDeg, const QString &caption)
{
    QImage tile(size, size, QImage::Format_ARGB32_Premultiplied);
    tile.fill(QColor(0x20, 0x24, 0x2c));
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QVector<EntityMeshTriangle> mesh = buildEntityMesh(model);
    // 自动缩放：按模型包围盒高度取景
    QVector3D mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
    for (const EntityMeshTriangle &t : mesh)
    {
        for (const QVector3D &p : t.pos)
        {
            mn.setX(qMin(mn.x(), p.x()));
            mn.setY(qMin(mn.y(), p.y()));
            mn.setZ(qMin(mn.z(), p.z()));
            mx.setX(qMax(mx.x(), p.x()));
            mx.setY(qMax(mx.y(), p.y()));
            mx.setZ(qMax(mx.z(), p.z()));
        }
    }
    const float height = qMax(0.4f, mx.y() - mn.y());
    const float span = qMax(height, qMax(mx.x() - mn.x(), mx.z() - mn.z()));
    const float scale = float(size) * 0.78f / qMax(0.6f, span);
    const QPointF center(size * 0.5f, size * 0.92f - mn.y() * scale);

    // 1 格参考方块（白框），方便判断生物大小与接地
    painter.setPen(QPen(QColor(255, 255, 255, 60), 1.0));
    const QPointF c000 = project(QVector3D(0, 0, 0), yawDeg, pitchDeg, center, scale);
    const QPointF c100 = project(QVector3D(1, 0, 0), yawDeg, pitchDeg, center, scale);
    const QPointF c110 = project(QVector3D(1, 1, 0), yawDeg, pitchDeg, center, scale);
    const QPointF c010 = project(QVector3D(0, 1, 0), yawDeg, pitchDeg, center, scale);
    painter.drawPolygon(QPolygonF() << c000 << c100 << c110 << c010);
    // 地面网格
    painter.setPen(QPen(QColor(255, 255, 255, 26), 1.0));
    for (int i = -2; i <= 2; ++i)
    {
        painter.drawLine(project(QVector3D(float(i), 0, -2), yawDeg, pitchDeg, center, scale),
                         project(QVector3D(float(i), 0, 2), yawDeg, pitchDeg, center, scale));
        painter.drawLine(project(QVector3D(-2, 0, float(i)), yawDeg, pitchDeg, center, scale),
                         project(QVector3D(2, 0, float(i)), yawDeg, pitchDeg, center, scale));
    }

    // 画家算法：远 -> 近
    QVector<int> order(mesh.size());
    for (int i = 0; i < order.size(); ++i)
        order[i] = i;
    QVector<float> depths(mesh.size());
    for (int i = 0; i < mesh.size(); ++i)
    {
        const EntityMeshTriangle &t = mesh.at(i);
        depths[i] = depthOf((t.pos[0] + t.pos[1] + t.pos[2]) / 3.0f, yawDeg, pitchDeg);
    }
    std::sort(order.begin(), order.end(), [&depths](int a, int b) {
        return depths.at(a) < depths.at(b);   // 远的先画（z 越大越远）
    });

    for (int idx : order)
    {
        const EntityMeshTriangle &t = mesh.at(idx);
        QPointF dst[3];
        QPointF src[3];
        bool degenerate = false;
        for (int k = 0; k < 3; ++k)
        {
            dst[k] = project(t.pos[k], yawDeg, pitchDeg, center, scale);
            src[k] = QPointF(t.uv[k].x(), t.uv[k].y());   // uv 已是贴图像素坐标
        }
        const double area = (dst[1].x() - dst[0].x()) * (dst[2].y() - dst[0].y())
                            - (dst[2].x() - dst[0].x()) * (dst[1].y() - dst[0].y());
        if (qAbs(area) < 0.6)
            continue;

        // 面光（与正式渲染同一套系数）
        const QVector3D lightDir = QVector3D(0.32f, 0.86f, 0.38f).normalized();
        const float nd = qAbs(QVector3D::dotProduct(t.normal, lightDir));
        const float light = 0.56f + 0.44f * qBound(0.0f, nd, 1.0f);

        QPainterPath clipPath;
        clipPath.addPolygon(QPolygonF() << dst[0] << dst[1] << dst[2]);
        clipPath.closeSubpath();

        painter.save();
        painter.setClipPath(clipPath, Qt::IntersectClip);
        painter.setTransform(triangleTransform(src, dst), true);
        painter.drawImage(0, 0, texture);
        painter.restore();

        // 探针只看几何与贴图对不对，不做压暗（正式渲染的面光在着色器里做）
        Q_UNUSED(light);
        Q_UNUSED(degenerate);
    }

    painter.setClipping(false);
    painter.setPen(QColor(230, 235, 245));
    QFont font = painter.font();
    font.setPixelSize(qMax(11, size / 16));
    painter.setFont(font);
    QString title = caption;
    if (!model.chineseName.isEmpty())
        title += QStringLiteral("（%1）").arg(model.chineseName);
    painter.drawText(QRectF(6, 4, size - 12, 20), Qt::AlignLeft | Qt::AlignTop, title);
    painter.setPen(QColor(150, 160, 175));
    painter.drawText(QRectF(6, size - 22, size - 12, 18), Qt::AlignLeft | Qt::AlignBottom,
                     QStringLiteral("%1 面 · %2×%3")
                         .arg(mesh.size())
                         .arg(model.texWidth)
                         .arg(model.texHeight));
    painter.end();
    return tile;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QString texDir;
    QString outPath = QStringLiteral("entity_sheet.png");
    QString ids;
    int size = 220;
    QString instancePath;
    bool selftest = false;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i)
    {
        const QString a = args.at(i);
        if (a == QLatin1String("--tex") && i + 1 < args.size())
            texDir = args.at(++i);
        else if (a == QLatin1String("--out") && i + 1 < args.size())
            outPath = args.at(++i);
        else if (a == QLatin1String("--entities") && i + 1 < args.size())
            ids = args.at(++i);
        else if (a == QLatin1String("--instance") && i + 1 < args.size())
            instancePath = args.at(++i);
        else if (a == QLatin1String("--selftest"))
            selftest = true;
        else if (a == QLatin1String("--size") && i + 1 < args.size())
            size = args.at(++i).toInt();
    }

    if (selftest)
        return runEntityRoundTrip() ? 0 : 1;

    if (ids.isEmpty())
        ids = QStringLiteral("pig,cow,sheep,chicken,zombie,skeleton,villager,iron_golem,"
                             "horse,wolf,player,armor_stand,boat,minecart,bee,fox");

    const TexturePool pool = texDir.isEmpty() ? TexturePool() : loadTextures(texDir);
    QImage instanceAtlas;
    if (!instancePath.isEmpty())
    {
        InstanceEntityTextures *tex = InstanceEntityTextures::sharedForInstance(instancePath);
        if (tex != nullptr)
            instanceAtlas = tex->atlasImage();
        qInfo("实例图集: %s  %d 张贴图 %dx%d", instanceAtlas.isNull() ? "失败" : "OK",
              tex ? tex->textureCount() : 0, instanceAtlas.width(), instanceAtlas.height());
        if (!instanceAtlas.isNull())
            instanceAtlas.save(outPath + QStringLiteral("_atlas.png"));
    }
    const QStringList idList = ids.split(QLatin1Char(','), Qt::SkipEmptyParts);

    const int cols = 4;
    const int rows = (idList.size() + cols - 1) / cols;
    QImage sheet(cols * size, rows * size, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor(0x12, 0x15, 0x1c));
    QPainter sheetPainter(&sheet);

    int placed = 0;
    for (const QString &id : idList)
    {
        EntityModel model;
        if (!EntityModelLoader::instance()->modelFor(id, model))
        {
            qWarning("no model: %s", qPrintable(id));
            continue;
        }
        QImage texture;
        if (!instanceAtlas.isNull())
        {
            // 实机路径：从实例 jar 建出的实体图集里取这张贴图（验证 rectForKey + uv 换算）
            QRect rect;
            QSize sourceSize;
            if (InstanceEntityTextures::sharedForInstance(instancePath)
                    ->rectForKey(model.textureKey, rect, &sourceSize)
                && !rect.isEmpty())
            {
                if (model.uvLimitU > float(sourceSize.width()) + 2.0f
                    || model.uvLimitV > float(sourceSize.height()) + 2.0f)
                {
                    qWarning("skip %s: 模型 uv %.0fx%.0f 超出实例贴图 %dx%d", qPrintable(id),
                             double(model.uvLimitU), double(model.uvLimitV),
                             sourceSize.width(), sourceSize.height());
                    continue;
                }
                texture = instanceAtlas.copy(rect);
            }
            else
            {
                qWarning("no atlas texture for %s (%s)", qPrintable(id),
                         qPrintable(model.textureKey));
            }
        }
        else if (!pool.find(model.textureKey, texture))
        {
            qWarning("no texture for %s (%s)", qPrintable(id), qPrintable(model.textureKey));
        }

        const QImage tile = renderEntity(model, texture, size, 28.0f, 18.0f, id);
        sheetPainter.drawImage(QPoint((placed % cols) * size, (placed / cols) * size), tile);
        ++placed;

        // 诊断输出：三角面数与包围盒（模型/贴图对不上时一眼能看出来）
        const QVector<EntityMeshTriangle> dbgMesh = buildEntityMesh(model);
        if (qEnvironmentVariableIsSet("PROBE_DEBUG_UV"))
        {
            for (const EntityMeshTriangle &t : dbgMesh)
            {
                const QVector2D uv = (t.uv[0] + t.uv[1] + t.uv[2]) / 3.0f;
                const int tx = qBound(0, int(uv.x()), qMax(0, texture.width() - 1));
                const int ty = qBound(0, int(uv.y()), qMax(0, texture.height() - 1));
                const QRgb px = texture.isNull() ? 0 : texture.pixel(tx, ty);
                qInfo("  tri uv=(%.3f,%.3f) texel=(%d,%d) rgba=%d,%d,%d,%d",
                      double(uv.x()), double(uv.y()), tx, ty,
                      qRed(px), qGreen(px), qBlue(px), qAlpha(px));
            }
        }
        QVector3D mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
        for (const EntityMeshTriangle &t : dbgMesh)
        {
            for (const QVector3D &p : t.pos)
            {
                mn.setX(qMin(mn.x(), p.x()));
                mn.setY(qMin(mn.y(), p.y()));
                mn.setZ(qMin(mn.z(), p.z()));
                mx.setX(qMax(mx.x(), p.x()));
                mx.setY(qMax(mx.y(), p.y()));
                mx.setZ(qMax(mx.z(), p.z()));
            }
        }
        qInfo("%-14s tex=%-34s tris=%-5d bbox=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f)",
              qPrintable(id), qPrintable(model.textureKey), dbgMesh.size(),
              double(mn.x()), double(mn.y()), double(mn.z()),
              double(mx.x()), double(mx.y()), double(mx.z()));
    }
    sheetPainter.end();
    sheet.save(outPath);
    qInfo("wrote %s (%d entities, models=%d)", qPrintable(outPath), placed,
          EntityModelLoader::instance()->modelCount());
    return 0;
}
