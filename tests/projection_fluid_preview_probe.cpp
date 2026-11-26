/**
 * @file   projection_fluid_preview_probe.cpp
 * @brief  投影三维预览「水 / 岩浆」观感回归探针
 *
 * 现造几份小投影，直接喂给 VoxelViewWidget 渲染，检查：
 *   1. 一整块水 / 岩浆的可见面数 = 96（4×4×4 立方体只画外表面）：
 *      内部面、层与层之间的接缝都合并掉了，不再是「一摞格子」；
 *   2. 水是半透明的：水后面放一面红方块墙，画面应当是「带红的蓝」，
 *      而不是几乎看不见底的死蓝（旧版 alpha≈0.92 时红几乎透不出来）；
 *   3. 岩浆自发光：侧面和顶面一样亮（旧版吃方向光，侧面暗 28%）；
 *   4. 有实例材质时，水 / 岩浆用的是 water_still / lava_still 贴图
 *      （按方块名 water / lava 是查不到的，旧版会退回纯色）。
 *
 * 同时把纯色 / 有材质两套截图存到 probe_out/，便于人工对比。
 * 退出码 0 = 全部断言通过。
 *
 * 用法（在 build-win-release 下）：
 *   python ..\tests\projection_fluid_fake_jar.py      （可选：现造一份只含贴图的最小客户端 jar）
 *   build_probe.bat projection_fluid_preview_probe ProjectionFluidPreviewProbe
 *   set BLOCKBOX_PROBE_INSTANCE=probe_out\fake_instance\fake   （可选，验证贴图路径）
 *   probe_out\ProjectionFluidPreviewProbe.exe
 */

#include "components/VoxelViewWidget.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/SchematicDocument.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QThread>
#include <cstdio>

namespace {

int g_failures = 0;

void expect(const QString &label, bool ok, const QString &detail = QString())
{
    fprintf(stderr, "[probe] %-48s %s%s%s\n", qPrintable(label), ok ? "OK" : "FAIL",
            detail.isEmpty() ? "" : "  ", qPrintable(detail));
    fflush(stderr);
    if (!ok)
        ++g_failures;
}

void pump(int rounds = 6, int ms = 20)
{
    for (int i = 0; i < rounds; ++i)
    {
        QThread::msleep(static_cast<unsigned long>(ms));
        QApplication::processEvents();
    }
}

/** 渲染一帧并取回帧缓存（先 update 让 paintGL 在事件循环里跑一次） */
QImage frame(VoxelViewWidget &view, int rounds = 5)
{
    view.update();
    pump(rounds, 16);
    return view.grabFramebuffer();
}

struct Mean
{
    double r = 0.0, g = 0.0, b = 0.0;
    double maxChannel() const { return qMax(r, qMax(g, b)); }
};

/** 画面正中一块区域的平均通道值（四周留白不进统计） */
Mean meanCenter(const QImage &img, double frac = 0.5)
{
    Mean m;
    if (img.isNull())
        return m;
    const int w = qMax(1, int(img.width() * frac));
    const int h = qMax(1, int(img.height() * frac));
    const int x0 = (img.width() - w) / 2;
    const int y0 = (img.height() - h) / 2;
    int n = 0;
    for (int y = y0; y < y0 + h; ++y)
    {
        for (int x = x0; x < x0 + w; ++x)
        {
            const QColor c = img.pixelColor(x, y);
            m.r += c.red();
            m.g += c.green();
            m.b += c.blue();
            ++n;
        }
    }
    if (n > 0)
    {
        m.r /= n;
        m.g /= n;
        m.b /= n;
    }
    return m;
}

SchematicBlockState block(const char *name)
{
    SchematicBlockState s;
    s.name = QString::fromLatin1(name);
    return s;
}

/** 液体状态（level 为空时按水源画） */
SchematicBlockState fluid(const char *name, int level = -1)
{
    SchematicBlockState s = block(name);
    if (level >= 0)
        s.properties.insert(QStringLiteral("level"), QString::number(level));
    return s;
}

/**
 * 一整块 n×n×n 液体：只该画出外表面
 * 顶 n² + 底 n² + 四侧 4n² = 6n² 个面（n=4 → 96）
 */
int expectedFluidQuads(int n)
{
    return 6 * n * n;
}

/** 水面 / 岩浆面观感样张：石头池底 + 水池 + 岩浆池 + 水下结构 + 台阶式流水 + 水柱 */
void buildMixedScene(SchematicDocument &doc)
{
    if (!doc.createEmpty(16, 9, 12))
        return;

    // 池底
    doc.fillBox(0, 0, 0, 15, 0, 11, block("minecraft:stone_bricks"));

    // 水池：水面 14/16 高，水里放玻璃柱 / 木板 / 红石作参照物
    doc.fillBox(1, 1, 1, 6, 4, 9, fluid("minecraft:water", 0));
    for (int y = 1; y <= 3; ++y)
    {
        doc.setBlock(2, y, 3, block("minecraft:glass"));
        doc.setBlock(2, y, 7, block("minecraft:oak_planks"));
    }
    doc.setBlock(3, 1, 5, block("minecraft:redstone_block"));

    // 岩浆池：更浅，池边摆一圈下界岩
    doc.fillBox(8, 1, 1, 12, 3, 9, block("minecraft:lava"));
    doc.fillBox(7, 1, 0, 13, 1, 10, block("minecraft:netherrack"));

    // 台阶式流水（液面逐级下降：level 0 / 4 / 7）+ 一根向下流的水柱
    doc.setBlock(2, 1, 10, fluid("minecraft:water", 0));
    doc.setBlock(3, 1, 10, fluid("minecraft:water", 4));
    doc.setBlock(4, 1, 10, fluid("minecraft:water", 7));
    for (int y = 1; y <= 4; ++y)
        doc.setBlock(5, y, 10, fluid("minecraft:water", 8));
    doc.setBlock(5, 5, 10, block("minecraft:stone_bricks"));
}

/** 把同一份场景按等距 / 正视 / 俯视各截一张 */
void shootMixed(SchematicDocument &doc, InstanceTextureLoader *loader, const QString &suffix)
{
    VoxelViewWidget view;
    view.resize(960, 640);
    view.setDocument(&doc);
    if (loader)
        view.setTextureLoader(loader);
    view.show();
    pump(8, 20);

    struct Preset { VoxelViewWidget::ViewPreset preset; const char *name; };
    const Preset presets[] = {
        { VoxelViewWidget::ViewPreset::Iso,   "iso" },
        { VoxelViewWidget::ViewPreset::Front, "front" },
        { VoxelViewWidget::ViewPreset::Top,   "top" },
    };
    for (const Preset &p : presets)
    {
        view.applyViewPreset(p.preset);
        view.focusAll();
        const QImage img = frame(view, 6);
        const QString path = QStringLiteral("probe_out/projection_fluid_%1_%2.png")
                                 .arg(QLatin1String(p.name), suffix);
        const bool ok = img.save(path);
        fprintf(stderr, "[probe] shot %-52s %dx%d %s (quads=%d)\n", qPrintable(path),
                img.width(), img.height(), ok ? "ok" : "FAILED", view.quadCount());
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));
    QDir().mkpath(QStringLiteral("probe_out"));

    // ---- 1. 一整块水的可见面数：内部面与层间接缝都该合并 ----
    {
        SchematicDocument doc;
        expect(QStringLiteral("现造 6×6×6 空投影"), doc.createEmpty(6, 6, 6));
        doc.fillBox(1, 1, 1, 4, 4, 4, fluid("minecraft:water", 0));
        VoxelViewWidget view;
        view.resize(640, 560);
        view.setDocument(&doc);
        view.show();
        pump(6, 20);
        const int expected = expectedFluidQuads(4);
        expect(QStringLiteral("水块只画外表面（4×4×4 → 96 面）"),
               view.quadCount() == expected,
               QStringLiteral("实际 %1").arg(view.quadCount()));

        view.applyViewPreset(VoxelViewWidget::ViewPreset::Iso);
        view.focusAll();
        const QImage img = frame(view);
        expect(QStringLiteral("水面渲染出画面"), !img.isNull());
        img.save(QStringLiteral("probe_out/projection_fluid_plain_water_cube.png"));
    }

    // ---- 2. 岩浆同样只画外表面 ----
    {
        SchematicDocument doc;
        doc.createEmpty(6, 6, 6);
        doc.fillBox(1, 1, 1, 4, 4, 4, block("minecraft:lava"));
        VoxelViewWidget view;
        view.resize(640, 560);
        view.setDocument(&doc);
        view.show();
        pump(6, 20);
        const int expected = expectedFluidQuads(4);
        expect(QStringLiteral("岩浆块只画外表面（4×4×4 → 96 面）"),
               view.quadCount() == expected,
               QStringLiteral("实际 %1").arg(view.quadCount()));
        view.applyViewPreset(VoxelViewWidget::ViewPreset::Iso);
        view.focusAll();
        frame(view).save(QStringLiteral("probe_out/projection_fluid_plain_lava_cube.png"));
    }

    // ---- 3. 水要半透明：水后面一面红墙，画面应该「红得出来」 ----
    {
        SchematicDocument doc;
        doc.createEmpty(8, 8, 2);
        doc.fillBox(0, 0, 0, 7, 7, 0, fluid("minecraft:water", 0));
        doc.fillBox(0, 0, 1, 7, 7, 1, block("minecraft:red_concrete"));
        VoxelViewWidget view;
        view.resize(640, 640);
        view.setDocument(&doc);
        view.show();
        pump(6, 20);
        view.applyViewPreset(VoxelViewWidget::ViewPreset::Front);
        view.focusAll();
        const QImage img = frame(view);
        const Mean m = meanCenter(img, 0.4);
        fprintf(stderr, "[probe]   水面后透出的红墙：R=%.0f G=%.0f B=%.0f\n", m.r, m.g, m.b);
        // 旧版水几乎不透明（alpha≈0.92），红色只能透出几个百分点，R/B 大约 0.25；
        // 半透明之后红色明显透出来，R/B 在 0.6 以上
        expect(QStringLiteral("水是半透明的（水下红墙能透出来）"),
               m.r > m.b * 0.5,
               QStringLiteral("R/B=%1").arg(m.b > 1.0 ? m.r / m.b : 0.0, 0, 'f', 2));
        img.save(QStringLiteral("probe_out/projection_fluid_plain_water_alpha.png"));
    }

    // ---- 4. 岩浆自发光：立方体的侧面和顶面一样亮 ----
    {
        SchematicDocument doc;
        doc.createEmpty(7, 7, 7);
        doc.fillBox(1, 1, 1, 5, 5, 5, block("minecraft:lava"));
        VoxelViewWidget view;
        view.resize(640, 640);
        view.setDocument(&doc);
        view.show();
        pump(6, 20);

        view.applyViewPreset(VoxelViewWidget::ViewPreset::Front);
        view.focusAll();
        const QImage frontShot = frame(view);
        const Mean side = meanCenter(frontShot, 0.35);
        frontShot.save(QStringLiteral("probe_out/projection_fluid_plain_lava_front.png"));

        view.applyViewPreset(VoxelViewWidget::ViewPreset::Top);
        view.focusAll();
        const QImage topShot = frame(view);
        const Mean top = meanCenter(topShot, 0.35);
        topShot.save(QStringLiteral("probe_out/projection_fluid_plain_lava_top.png"));

        fprintf(stderr, "[probe]   岩浆亮度：侧面 %.0f / 顶面 %.0f\n",
                side.maxChannel(), top.maxChannel());
        // 旧版按方向光画：侧面 0.72、顶面 1.0，差 28%；自发光后两面都是满亮
        expect(QStringLiteral("岩浆自发光（侧面和顶面一样亮）"),
               top.maxChannel() > 1.0 && side.maxChannel() > top.maxChannel() * 0.9,
               QStringLiteral("比值 %1").arg(top.maxChannel() > 1.0
                                               ? side.maxChannel() / top.maxChannel()
                                               : 0.0, 0, 'f', 2));
    }

    // ---- 5. 水池 + 岩浆池观感样张（纯色）----
    SchematicDocument mixed;
    buildMixedScene(mixed);
    expect(QStringLiteral("水池 + 岩浆池场景生成"), mixed.isLoaded());
    shootMixed(mixed, nullptr, QStringLiteral("plain"));

    // ---- 6. 有实例材质时：水 / 岩浆该用 water_still / lava_still 贴图 ----
    const QString instanceDir = qEnvironmentVariable("BLOCKBOX_PROBE_INSTANCE");
    if (!instanceDir.isEmpty())
    {
        InstanceTextureLoader loader;
        QString error;
        const bool loaded = loader.load(instanceDir, &error);
        expect(QStringLiteral("实例材质加载成功"), loaded, error);
        if (loaded)
        {
            int waterTile = -1, lavaTile = -1;
            const bool waterOk = loader.tileForKey(QStringLiteral("block/water_still"), waterTile);
            const bool lavaOk = loader.tileForKey(QStringLiteral("block/lava_still"), lavaTile);
            expect(QStringLiteral("图集里有 water_still / lava_still"), waterOk && lavaOk,
                   QStringLiteral("tile %1 / %2").arg(waterTile).arg(lavaTile));
            shootMixed(mixed, &loader, QStringLiteral("textured"));

            // 有贴图时水依然是蓝的（贴图乘群系色），不是惨白 / 纯色板
            SchematicDocument doc;
            doc.createEmpty(8, 8, 2);
            doc.fillBox(0, 0, 0, 7, 7, 0, fluid("minecraft:water", 0));
            doc.fillBox(0, 0, 1, 7, 7, 1, block("minecraft:red_concrete"));
            VoxelViewWidget view;
            view.resize(640, 640);
            view.setDocument(&doc);
            view.setTextureLoader(&loader);
            view.show();
            pump(8, 20);
            view.applyViewPreset(VoxelViewWidget::ViewPreset::Front);
            view.focusAll();
            const QImage img = frame(view);
            img.save(QStringLiteral("probe_out/projection_fluid_textured_water_alpha.png"));
            const Mean m = meanCenter(img, 0.4);
            fprintf(stderr, "[probe]   有材质的水：R=%.0f G=%.0f B=%.0f\n", m.r, m.g, m.b);
            expect(QStringLiteral("有材质的水仍是蓝色（贴图 × 群系色）"),
                   m.b > m.r * 1.15,
                   QStringLiteral("B/R=%1").arg(m.r > 1.0 ? m.b / m.r : 0.0, 0, 'f', 2));
        }
    }
    else
    {
        fprintf(stderr, "[probe] 未设置 BLOCKBOX_PROBE_INSTANCE，跳过材质路径\n");
    }

    // ---- 7. 流动液面样张：level 0 / 4 / 7 三级液面应当一级比一级低 ----
    {
        SchematicDocument doc;
        doc.createEmpty(13, 4, 9);
        doc.fillBox(0, 0, 0, 12, 0, 8, block("minecraft:stone_bricks"));
        doc.fillBox(1, 1, 1, 11, 1, 7, fluid("minecraft:water", 0));
        // 三格水面：满水 / 半高 / 很低，紧挨着放，肉眼一看就该是阶梯
        doc.setBlock(5, 2, 1, fluid("minecraft:water", 4));
        doc.setBlock(5, 2, 2, fluid("minecraft:water", 4));
        doc.setBlock(5, 2, 3, fluid("minecraft:water", 4));
        doc.setBlock(8, 2, 1, fluid("minecraft:water", 7));
        doc.setBlock(8, 2, 2, fluid("minecraft:water", 7));
        doc.setBlock(8, 2, 3, fluid("minecraft:water", 7));

        VoxelViewWidget view;
        view.resize(900, 520);
        view.setDocument(&doc);
        view.show();
        pump(8, 20);
        view.applyViewPreset(VoxelViewWidget::ViewPreset::Iso);
        view.focusAll();
        const QImage img = frame(view, 6);
        img.save(QStringLiteral("probe_out/projection_fluid_levels.png"));
        fprintf(stderr, "[probe] shot probe_out/projection_fluid_levels.png  quads=%d\n",
                view.quadCount());
        expect(QStringLiteral("流动液面场景渲染出画面"), !img.isNull());
    }

    fprintf(stderr, "[probe] failures=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
