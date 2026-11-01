/**
 * @file   rail_probe.cpp
 * @brief  投影方块编辑器「铁轨渲染」验证探针
 *
 * 用真实实例 jar 的 blockstate / model / 贴图渲染各种铁轨，输出：
 *   1) 各铁轨状态的形状解析结果（盒子范围、面、贴图 key、uv）
 *   2) 图集里铁轨贴图那几格（看取到的贴图对不对）
 *   3) 若干角度截图（看实际观感）
 *
 * 用法：rail_probe.exe <实例 versions/版本 目录> <输出目录> [uvtest|投影文件.litematic]
 *   uvtest：四个弯轨四周摆不同颜色的参照方块，俯视检查弯道到底连的是哪两条边
 *           （东=铁块 南=红石块 西=金块 北=钻石块）
 *   投影文件：加载真实投影，取景到其中第一段铁轨并截图
 */
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QDebug>
#include <cstdio>

#include "components/VoxelViewWidget.h"
#include "utils/Schematic/BlockShapeLoader.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/SchematicDocument.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QString instPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString outDir = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                    : QDir::currentPath();
    QDir().mkpath(outDir);

    InstanceTextureLoader *tex = InstanceTextureLoader::sharedForInstance(instPath);
    BlockShapeLoader *shapes = BlockShapeLoader::sharedForInstance(instPath);
    qInfo().noquote() << "instance:" << instPath;
    qInfo().noquote() << "textures:" << (tex && tex->isValid())
                      << "shapes:" << (shapes && shapes->isValid());

    // ---------------------------------------------------------------- 形状解析
    const QStringList railStates = {
        QStringLiteral("minecraft:rail[shape=north_south]"),
        QStringLiteral("minecraft:rail[shape=east_west]"),
        QStringLiteral("minecraft:rail[shape=north_east]"),
        QStringLiteral("minecraft:rail[shape=south_west]"),
        QStringLiteral("minecraft:rail[shape=ascending_north]"),
        QStringLiteral("minecraft:rail[shape=ascending_east]"),
        QStringLiteral("minecraft:powered_rail[shape=north_south,powered=false]"),
        QStringLiteral("minecraft:powered_rail[shape=north_south,powered=true]"),
        QStringLiteral("minecraft:detector_rail[shape=north_south,powered=false]"),
        QStringLiteral("minecraft:activator_rail[shape=north_south,powered=false]"),
    };
    for (const QString &state : railStates)
    {
        BlockShapes tmp;
        const bool ok = shapes && shapes->shapeFor(state, tmp);
        qInfo().noquote() << "----" << state << "parsed:" << ok << "boxes:" << tmp.boxes.size();
        for (const ShapeBox &bx : tmp.boxes)
        {
            QStringList dirs;
            QStringList texs;
            QStringList uvs;
            for (const ShapeFace &fc : bx.faces)
            {
                dirs.append(fc.dir);
                texs.append(fc.texture);
                uvs.append(QStringLiteral("[%1,%2,%3,%4]%5")
                               .arg(fc.uv[0]).arg(fc.uv[1]).arg(fc.uv[2]).arg(fc.uv[3])
                               .arg(fc.hasUv ? QString() : QStringLiteral("(default)")));
            }
            qInfo().noquote() << QStringLiteral("  box [%1,%2,%3]->[%4,%5,%6] yaw=%7 rot=%8/%9")
                                     .arg(bx.from[0] * 16, 0, 'f', 1).arg(bx.from[1] * 16, 0, 'f', 1)
                                     .arg(bx.from[2] * 16, 0, 'f', 1)
                                     .arg(bx.to[0] * 16, 0, 'f', 1).arg(bx.to[1] * 16, 0, 'f', 1)
                                     .arg(bx.to[2] * 16, 0, 'f', 1)
                                     .arg(bx.yaw).arg(bx.rotAngle).arg(bx.rotAxis);
            qInfo().noquote() << "    faces:" << dirs.join(QLatin1Char(','));
            qInfo().noquote() << "    tex  :" << texs.join(QLatin1Char(','));
            qInfo().noquote() << "    uv   :" << uvs.join(QLatin1Char(','));
        }
    }

    // ---------------------------------------------------------------- 图集检查
    if (tex && tex->isValid())
    {
        tex->atlasImage().save(outDir + QStringLiteral("/atlas.png"));
        const QStringList keys = { QStringLiteral("rail"), QStringLiteral("rail_corner"),
                                   QStringLiteral("powered_rail"),
                                   QStringLiteral("powered_rail_on"),
                                   QStringLiteral("detector_rail"),
                                   QStringLiteral("activator_rail") };
        for (const QString &key : keys)
        {
            int tile = -1;
            const bool ok = tex->tileForKey(QStringLiteral("block/") + key, tile);
            qInfo().noquote() << "tileForKey block/" << key << "->" << ok << tile;
            if (!ok)
                continue;
            float u0, v0, u1, v1;
            tex->tileUv(tile, u0, v0, u1, v1);
            const int ts = tex->atlasSize();
            const QImage img = tex->atlasImage().copy(int(u0 * ts + 0.5f), int(v0 * ts + 0.5f), 16, 16);
            if (!img.isNull())
            {
                img.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                    .save(outDir + QStringLiteral("/tile_%1.png").arg(key));
            }
        }
    }

    // ---------------------------------------------------------------- UV 朝向自检
    // 每个测试点：中间一条铁轨，四边各放一种颜色的参照方块——
    // 东=铁块(浅灰) 南=红石块(红) 西=金块(黄) 北=钻石块(青)。
    // shape=south_east 的弯轨必须连着南(红)与东(浅灰)，其余同理。
    if (argc > 3 && QString::fromLocal8Bit(argv[3]) == QLatin1String("uvtest"))
    {
        SchematicDocument uv;
        uv.createEmpty(13, 3, 13);
        auto put2 = [&uv](int x, int y, int z, const char *state) {
            uv.setBlock(x, y, z, SchematicBlockState::fromString(QString::fromLatin1(state)));
        };
        for (int z = 0; z < 13; ++z)
            for (int x = 0; x < 13; ++x)
                put2(x, 0, z, "minecraft:stone_bricks");

        struct Station { int cx, cz; const char *shape; };
        const Station stations[] = {
            { 3, 3, "south_east" },
            { 9, 3, "south_west" },
            { 3, 9, "north_east" },
            { 9, 9, "north_west" },
        };
        QSet<int> cells;
        for (const Station &st : stations)
        {
            char railState[96];
            std::snprintf(railState, sizeof(railState), "minecraft:rail[shape=%s]", st.shape);
            put2(st.cx, 1, st.cz, railState);
            put2(st.cx + 1, 1, st.cz, "minecraft:iron_block");      // 东
            put2(st.cx - 1, 1, st.cz, "minecraft:gold_block");      // 西
            put2(st.cx, 1, st.cz + 1, "minecraft:redstone_block");  // 南
            put2(st.cx, 1, st.cz - 1, "minecraft:diamond_block");   // 北
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    cells.insert(1 * uv.layerSize() + (st.cz + dz) * uv.sizeX() + (st.cx + dx));
            qInfo().noquote() << "uv test station" << st.cx << st.cz << st.shape;
        }

        VoxelViewWidget uvView;
        uvView.resize(900, 900);
        uvView.setTextureLoader(tex);
        uvView.setShapeLoader(shapes);
        uvView.setLayerMode(VoxelViewWidget::LayerMode::All);
        uvView.setDocument(&uv);
        uvView.rebuild();
        uvView.show();
        QTimer::singleShot(900, &app, [&]() {
            uvView.focusAll();
            uvView.applyViewPreset(VoxelViewWidget::ViewPreset::Top);
            QTimer::singleShot(600, &app, [&]() {
                QImage img = uvView.grabFramebuffer();
                if (!img.isNull())
                    img.save(outDir + QStringLiteral("/uvtest_top.png"));
                uvView.focusOnCells(cells);
                uvView.applyViewPreset(VoxelViewWidget::ViewPreset::Top);
                QTimer::singleShot(500, &app, [&]() {
                    QImage img2 = uvView.grabFramebuffer();
                    if (!img2.isNull())
                        img2.save(outDir + QStringLiteral("/uvtest_zoom.png"));
                    app.quit();
                });
            });
        });
        return app.exec();
    }

    // ---------------------------------------------------------------- 场景
    SchematicDocument doc;
    doc.createEmpty(14, 8, 14);
    auto put = [&doc](int x, int y, int z, const char *state) {
        doc.setBlock(x, y, z, SchematicBlockState::fromString(QString::fromLatin1(state)));
    };
    for (int z = 0; z < 14; ++z)
        for (int x = 0; x < 14; ++x)
            put(x, 0, z, "minecraft:stone_bricks");

    QSet<int> focus;
    auto rail = [&](int x, int y, int z, const char *state) {
        put(x, y, z, state);
        focus.insert(y * doc.layerSize() + z * doc.sizeX() + x);
    };

    // 南北直轨（贴图/朝向基准）
    for (int z = 1; z <= 5; ++z)
        rail(1, 1, z, "minecraft:rail[shape=north_south]");
    // 东西直轨
    for (int x = 3; x <= 7; ++x)
        rail(x, 1, 1, "minecraft:rail[shape=east_west]");
    // 弯轨四种朝向：拼成一个方框的四角
    rail(1, 1, 6, "minecraft:rail[shape=south_east]");
    rail(2, 1, 6, "minecraft:rail[shape=south_west]");
    rail(2, 1, 7, "minecraft:rail[shape=north_west]");
    rail(3, 1, 7, "minecraft:rail[shape=north_east]");
    // 上坡轨
    rail(5, 1, 4, "minecraft:rail[shape=ascending_north]");
    rail(5, 1, 5, "minecraft:rail[shape=ascending_east]");
    rail(5, 1, 6, "minecraft:rail[shape=ascending_south]");
    rail(5, 1, 7, "minecraft:rail[shape=ascending_west]");
    // 三种特殊轨（未通电 / 通电）
    rail(8, 1, 2, "minecraft:powered_rail[shape=north_south,powered=false]");
    rail(8, 1, 3, "minecraft:powered_rail[shape=north_south,powered=true]");
    rail(8, 1, 4, "minecraft:detector_rail[shape=north_south,powered=false]");
    rail(8, 1, 5, "minecraft:activator_rail[shape=north_south,powered=false]");
    // 铁轨下面还有一整块石砖（贴面贴合、看有没有共面闪烁）
    for (int z = 1; z <= 8; ++z)
        put(11, 1, z, "minecraft:stone_bricks");
    for (int z = 1; z <= 8; ++z)
        rail(11, 2, z, "minecraft:rail[shape=north_south]");
    // 参照物：同样的位置放普通方块，方便对比亮度和饱和度
    put(1, 1, 10, "minecraft:oak_planks");
    put(3, 1, 10, "minecraft:cobblestone");
    put(5, 1, 10, "minecraft:iron_block");

    VoxelViewWidget view;
    view.resize(1100, 820);
    view.setTextureLoader(tex);
    view.setShapeLoader(shapes);
    view.setLayerMode(VoxelViewWidget::LayerMode::All);
    view.setDocument(&doc);
    view.rebuild();
    qInfo().noquote() << "scene quads" << view.quadCount();

    // ---------------------------------------------------------------- 真实投影
    const QString projPath = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();
    SchematicDocument projDoc;
    QSet<int> projRailCells;
    if (!projPath.isEmpty())
    {
        QString err;
        if (!projDoc.load(projPath, &err))
            qInfo().noquote() << "load projection failed:" << err;
        else
        {
            qInfo().noquote() << "projection" << projDoc.name() << projDoc.sizeX() << "x"
                              << projDoc.sizeY() << "x" << projDoc.sizeZ();
            const QList<SchematicBlockState> pal = projDoc.palette();
            for (int p = 0; p < pal.size(); ++p)
            {
                if (!pal.at(p).name.contains(QLatin1String("rail")))
                    continue;
                qInfo().noquote() << "  rail palette:" << pal.at(p).toString();
                BlockShapes tmp;
                const bool ok = shapes && shapes->shapeFor(pal.at(p).toString(), tmp);
                qInfo().noquote() << "    shape parsed:" << ok << "boxes:" << tmp.boxes.size();
                for (const ShapeBox &bx : tmp.boxes)
                    qInfo().noquote() << QStringLiteral("      box [%1,%2,%3]->[%4,%5,%6] yaw=%7 faces=%8 tex=%9")
                                             .arg(bx.from[0] * 16, 0, 'f', 1).arg(bx.from[1] * 16, 0, 'f', 1)
                                             .arg(bx.from[2] * 16, 0, 'f', 1)
                                             .arg(bx.to[0] * 16, 0, 'f', 1).arg(bx.to[1] * 16, 0, 'f', 1)
                                             .arg(bx.to[2] * 16, 0, 'f', 1)
                                             .arg(bx.yaw).arg(bx.faces.size())
                                             .arg(bx.faces.isEmpty() ? QString() : bx.faces.first().texture);
            }
            const QVector<SchematicBlockState> all = projDoc.dumpAll();
            for (int i = 0; i < all.size(); ++i)
                if (all.at(i).name.contains(QLatin1String("rail")))
                    projRailCells.insert(i);
            qInfo().noquote() << "  rail cells:" << projRailCells.size();
            if (!projRailCells.isEmpty())
            {
                // 只围着第一段铁轨取景，免得整座城市把铁轨缩成一条线
                const int first = *projRailCells.constBegin();
                QSet<int> near;
                const int lx = first % projDoc.layerSize() % projDoc.sizeX();
                const int lz = first % projDoc.layerSize() / projDoc.sizeX();
                const int ly = first / projDoc.layerSize();
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dz = -5; dz <= 5; ++dz)
                        for (int dx = -5; dx <= 5; ++dx)
                        {
                            const int x = lx + dx, y = ly + dy, z = lz + dz;
                            if (projDoc.contains(x, y, z))
                                near.insert(y * projDoc.layerSize() + z * projDoc.sizeX() + x);
                        }
                view.setDocument(&projDoc);
                view.rebuild();
                view.show();
                QTimer::singleShot(800, &app, [&, near]() {
                    view.focusOnCells(near);
                    view.applyViewPreset(VoxelViewWidget::ViewPreset::Iso);
                    QTimer::singleShot(600, &app, [&]() {
                        QImage img = view.grabFramebuffer();
                        if (!img.isNull())
                            img.save(outDir + QStringLiteral("/proj_rail_iso.png"));
                        view.applyViewPreset(VoxelViewWidget::ViewPreset::Front);
                        QTimer::singleShot(500, &app, [&]() {
                            QImage f2 = view.grabFramebuffer();
                            if (!f2.isNull())
                                f2.save(outDir + QStringLiteral("/proj_rail_front.png"));
                            view.applyViewPreset(VoxelViewWidget::ViewPreset::Top);
                            QTimer::singleShot(500, &app, [&]() {
                                QImage t2 = view.grabFramebuffer();
                                if (!t2.isNull())
                                    t2.save(outDir + QStringLiteral("/proj_rail_top.png"));
                                app.quit();
                            });
                        });
                    });
                });
                return app.exec();
            }
        }
    }

    view.show();
    QTimer::singleShot(1200, &app, [&]() {
        view.focusAll();
        view.applyViewPreset(VoxelViewWidget::ViewPreset::Iso);
        QTimer::singleShot(600, &app, [&]() {
            QImage iso = view.grabFramebuffer();
            if (!iso.isNull())
                iso.save(outDir + QStringLiteral("/rail_iso.png"));

            view.focusOnCells(focus);
            view.applyViewPreset(VoxelViewWidget::ViewPreset::Top);
            QTimer::singleShot(600, &app, [&]() {
                QImage top = view.grabFramebuffer();
                if (!top.isNull())
                    top.save(outDir + QStringLiteral("/rail_top.png"));

                view.applyViewPreset(VoxelViewWidget::ViewPreset::Front);
                QTimer::singleShot(600, &app, [&]() {
                    QImage front = view.grabFramebuffer();
                    if (!front.isNull())
                        front.save(outDir + QStringLiteral("/rail_front.png"));

                    view.focusAll();
                    view.applyViewPreset(VoxelViewWidget::ViewPreset::Iso);
                    QTimer::singleShot(600, &app, [&]() {
                        QImage all = view.grabFramebuffer();
                        if (!all.isNull())
                            all.save(outDir + QStringLiteral("/rail_all.png"));
                        app.quit();
                    });
                });
            });
        });
    });

    return app.exec();
}
