/**
 * @file   lantern_probe.cpp
 * @brief  投影方块编辑器「灯笼渲染」验证探针
 *
 * 用真实实例 jar 的 blockstate / model / 贴图渲染灯笼，输出：
 *   1) 单方块面数统计（看模型缺面补面有没有白画面）
 *   2) 图集里 lantern 贴图那一格（看动画贴图有没有被压扁）
 *   3) 若干角度截图（看实际观感）
 *
 * 用法：lantern_probe.exe <实例 versions/版本 目录> <输出目录>
 */
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QDebug>

#include "components/VoxelViewWidget.h"
#include "utils/Schematic/BlockShapeLoader.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/SchematicDocument.h"

namespace {

/** 单个方块状态在 3×3×3 空文档里的可见面数（不含空气、不含邻居遮挡） */
struct SingleStat
{
    QString state;
    int     quads = 0;
    int     boxes = 0;
};

} // namespace

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

    VoxelViewWidget view;
    view.resize(1000, 760);
    view.setTextureLoader(tex);
    view.setShapeLoader(shapes);
    view.setLayerMode(VoxelViewWidget::LayerMode::All);

    // ---------------------------------------------------------------- 单方块面数
    const QStringList singleStates = {
        QStringLiteral("minecraft:lantern[hanging=false]"),
        QStringLiteral("minecraft:lantern[hanging=true]"),
        QStringLiteral("minecraft:soul_lantern[hanging=false]"),
        QStringLiteral("minecraft:soul_lantern[hanging=true]"),
        QStringLiteral("minecraft:sea_lantern"),
        QStringLiteral("minecraft:glowstone"),
        QStringLiteral("minecraft:jack_o_lantern[facing=south]"),
        QStringLiteral("minecraft:torch"),
        QStringLiteral("minecraft:stone"),
        QStringLiteral("minecraft:oak_stairs[facing=east,half=bottom,shape=straight]"),
        // 以下几种常见形状用来确认「只画模型声明的面」没有把方块画没
        QStringLiteral("minecraft:oak_slab[type=bottom]"),
        QStringLiteral("minecraft:oak_fence[north=true,south=true,east=true,west=true]"),
        QStringLiteral("minecraft:cobblestone_wall[east=tall,north=tall,south=tall,west=tall,up=true,waterlogged=false]"),
        QStringLiteral("minecraft:glass_pane[north=true,south=true]"),
        QStringLiteral("minecraft:oak_door[facing=east,half=lower,hinge=left,open=false]"),
        QStringLiteral("minecraft:oak_trapdoor[facing=north,half=bottom,open=false]"),
        QStringLiteral("minecraft:chain[axis=y]"),
        QStringLiteral("minecraft:flower_pot"),
        QStringLiteral("minecraft:grass_block[snowy=false]"),
        QStringLiteral("minecraft:vine[east=true]"),
        QStringLiteral("minecraft:ladder[facing=north]"),
        QStringLiteral("minecraft:chest[facing=north,type=single,waterlogged=false]"),
        QStringLiteral("minecraft:campfire[lit=true,facing=north]"),
        QStringLiteral("minecraft:anvil[facing=north]"),
        QStringLiteral("minecraft:candle[candles=3,lit=true,waterlogged=false]"),
        QStringLiteral("minecraft:hopper[facing=down]"),
        QStringLiteral("minecraft:oak_leaves[persistent=true]"),
        QStringLiteral("minecraft:water"),
    };

    SchematicDocument single;
    QVector<SingleStat> stats;
    for (const QString &state : singleStates)
    {
        single.clear();
        single.createEmpty(3, 3, 3);
        single.setBlock(1, 1, 1, SchematicBlockState::fromString(state));
        // 每个状态用一个全新视图，排除上一份文档残留的缓存干扰
        VoxelViewWidget fresh;
        fresh.setTextureLoader(tex);
        fresh.setShapeLoader(shapes);
        fresh.setLayerMode(VoxelViewWidget::LayerMode::All);
        fresh.setDocument(&single);
        fresh.rebuild();
        QStringList pal;
        for (const SchematicBlockState &st2 : single.palette())
            pal.append(st2.toString());
        SingleStat st;
        st.state = state;
        st.quads = fresh.quadCount();
        qInfo().noquote() << "  palette:" << pal.join(QStringLiteral(" | "))
                          << "cells" << single.countByState();
        if (shapes && shapes->isValid())
        {
            BlockShapes tmp;
            if (shapes->shapeFor(state, tmp))
            {
                st.boxes = tmp.boxes.size();
                // 只有灯笼类才逐盒打印（看清模型声明了哪些面、哪些面被跳过）
                if (state.contains(QLatin1String("lantern")))
                {
                    for (const ShapeBox &bx : tmp.boxes)
                    {
                        QStringList dirs;
                        for (const ShapeFace &fc : bx.faces)
                            dirs.append(fc.dir);
                        qInfo().noquote() << QStringLiteral("    box [%1,%2,%3]->[%4,%5,%6] %7 faces: %8")
                                                 .arg(bx.from[0] * 16).arg(bx.from[1] * 16).arg(bx.from[2] * 16)
                                                 .arg(bx.to[0] * 16).arg(bx.to[1] * 16).arg(bx.to[2] * 16)
                                                 .arg(bx.faces.size())
                                                 .arg(dirs.join(QLatin1Char(',')));
                    }
                }
            }
        }
        stats.append(st);
    }
    for (const SingleStat &st : stats)
        qInfo().noquote() << QStringLiteral("quads %1  boxes %2  %3")
                                 .arg(st.quads, 3).arg(st.boxes, 2).arg(st.state);

    // ---------------------------------------------------------------- 图集检查
    if (tex && tex->isValid())
    {
        tex->atlasImage().save(outDir + QStringLiteral("/atlas.png"));
        int tile = -1;
        if (tex->textureFor(QStringLiteral("minecraft:lantern"), tile))
        {
            float u0, v0, u1, v1;
            tex->tileUv(tile, u0, v0, u1, v1);
            const int ts = tex->atlasSize();
            const QImage tileImg = tex->atlasImage().copy(int(u0 * ts), int(v0 * ts),
                                                          qMax(1, int((u1 - u0) * ts)),
                                                          qMax(1, int((v1 - v0) * ts)));
            tileImg.save(outDir + QStringLiteral("/atlas_lantern_tile.png"));
            qInfo().noquote() << "lantern tile" << tile << "atlas" << ts;
        }
        int soulTile = -1;
        if (tex->textureFor(QStringLiteral("minecraft:soul_lantern"), soulTile))
        {
            float u0, v0, u1, v1;
            tex->tileUv(soulTile, u0, v0, u1, v1);
            const int ts = tex->atlasSize();
            tex->atlasImage().copy(int(u0 * ts), int(v0 * ts),
                                   qMax(1, int((u1 - u0) * ts)),
                                   qMax(1, int((v1 - v0) * ts)))
                .save(outDir + QStringLiteral("/atlas_soul_lantern_tile.png"));
        }
    }

    // ---------------------------------------------------------------- 场景
    SchematicDocument doc;
    doc.createEmpty(8, 6, 8);
    auto put = [&doc](int x, int y, int z, const char *state) {
        doc.setBlock(x, y, z, SchematicBlockState::fromString(QString::fromLatin1(state)));
    };
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x)
            put(x, 0, z, "minecraft:stone_bricks");
    for (int y = 1; y <= 5; ++y)
        for (int x = 0; x < 8; ++x)
            put(x, y, 7, "minecraft:stone_bricks");

    QSet<int> lit;
    auto lantern = [&](int x, int y, int z, const char *state) {
        put(x, y, z, state);
        lit.insert(y * doc.layerSize() + z * doc.sizeX() + x);
    };
    lantern(1, 1, 2, "minecraft:lantern[hanging=false]");
    lantern(2, 1, 2, "minecraft:soul_lantern[hanging=false]");
    lantern(3, 1, 2, "minecraft:sea_lantern");
    lantern(4, 1, 2, "minecraft:jack_o_lantern[facing=south]");
    lantern(5, 1, 2, "minecraft:glowstone");
    lantern(2, 3, 5, "minecraft:lantern[hanging=true]");
    lantern(4, 3, 5, "minecraft:soul_lantern[hanging=true]");
    // 其它动画贴图方块：水/岩浆/营火/命令方块/岩浆块，确认取第一帧后观感正常
    put(1, 1, 4, "minecraft:water");
    put(2, 1, 4, "minecraft:lava");
    put(3, 1, 4, "minecraft:campfire[lit=true,facing=north]");
    put(4, 1, 4, "minecraft:command_block[facing=north]");
    put(5, 1, 4, "minecraft:magma_block");
    // 光源旁边的普通方块，方便看发光效果对比
    put(2, 1, 6, "minecraft:oak_planks");
    put(4, 1, 6, "minecraft:oak_planks");

    view.setDocument(&doc);
    view.rebuild();
    qInfo().noquote() << "scene quads" << view.quadCount();

    view.show();
    QTimer::singleShot(1200, &app, [&]() {
        view.focusOnCells(lit);
        view.applyViewPreset(VoxelViewWidget::ViewPreset::Iso);
        QTimer::singleShot(500, &app, [&]() {
            const QImage iso = view.grabFramebuffer();
            if (!iso.isNull())
                iso.save(outDir + QStringLiteral("/lantern_iso.png"));

            // 平视，看灯笼正面贴图
            view.applyViewPreset(VoxelViewWidget::ViewPreset::Front);
            QTimer::singleShot(400, &app, [&]() {
                const QImage front = view.grabFramebuffer();
                if (!front.isNull())
                    front.save(outDir + QStringLiteral("/lantern_front.png"));

                // 俯视一点，看顶部灯笼盖
                view.applyViewPreset(VoxelViewWidget::ViewPreset::Top);
                QTimer::singleShot(400, &app, [&]() {
                    const QImage top = view.grabFramebuffer();
                    if (!top.isNull())
                        top.save(outDir + QStringLiteral("/lantern_top.png"));

                    view.focusAll();
                    view.applyViewPreset(VoxelViewWidget::ViewPreset::Iso);
                    QTimer::singleShot(400, &app, [&]() {
                        const QImage all = view.grabFramebuffer();
                        if (!all.isNull())
                            all.save(outDir + QStringLiteral("/scene_all.png"));
                        app.quit();
                    });
                });
            });
        });
    });

    return app.exec();
}
