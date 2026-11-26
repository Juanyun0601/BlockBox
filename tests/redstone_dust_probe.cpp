/**
 * @file   redstone_dust_probe.cpp
 * @brief  投影预览「红石线（红石粉）」渲染探针
 *
 * 现造一份带各种红石走线的小投影（直线 / 十字 / 孤立点 / 信号渐变 / 爬坡 /
 * 没有连接属性的旧状态），分别用「材质」和「纯色块」两种来源渲染，检查：
 *   1. 红石粉尘是红色的（以前材质模式下会因为 tintindex 被染成绿色）；
 *   2. 纯色模式下不会退化成整块红砖（用面数检查）；
 *   3. 截图落盘，便于人工确认连接形状和爬坡。
 *
 * 用法（在 build-win-release 下）：
 *   build_probe.bat redstone_dust_probe RedstoneDustProbe
 *   probe_out\RedstoneDustProbe.exe
 */

#include "components/VoxelViewWidget.h"
#include "components/ProjectionPreviewDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QThread>

#include "utils/Schematic/BlockShapeLoader.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/Schematic/SchematicDocument.h"

#include <cstdio>
#include <functional>

namespace {

int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-64s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

void pump(int rounds = 8, int ms = 25)
{
    for (int i = 0; i < rounds; ++i)
    {
        QThread::msleep(static_cast<unsigned long>(ms));
        QApplication::processEvents();
    }
}

SchematicBlockState wireState(int power, const QString &north, const QString &east,
                              const QString &south, const QString &west)
{
    SchematicBlockState st;
    st.name = QStringLiteral("minecraft:redstone_wire");
    st.properties.insert(QStringLiteral("power"), QString::number(power));
    st.properties.insert(QStringLiteral("north"), north);
    st.properties.insert(QStringLiteral("east"), east);
    st.properties.insert(QStringLiteral("south"), south);
    st.properties.insert(QStringLiteral("west"), west);
    return st;
}

/// 20×6×20 的红石样例：直线 / 十字 / 孤立点 / 功率渐变 / 爬坡 / 无属性旧状态
bool buildSample(SchematicDocument &doc)
{
    if (!doc.createEmpty(20, 6, 20))
        return false;

    SchematicBlockState floor;
    floor.name = QStringLiteral("minecraft:stone_bricks");
    for (int z = 0; z < 20; ++z)
        for (int x = 0; x < 20; ++x)
            doc.setBlock(x, 0, z, floor);

    // 1) 满信号直线（南北两侧不连）
    for (int x = 2; x <= 15; ++x)
    {
        const QString e = (x < 15) ? QStringLiteral("side") : QStringLiteral("none");
        const QString w = (x > 2) ? QStringLiteral("side") : QStringLiteral("none");
        doc.setBlock(x, 1, 3, wireState(15, QStringLiteral("none"), e,
                                        QStringLiteral("none"), w));
    }

    // 2) 十字交汇（两段垂直的线在 (7,1,12) 相交）
    for (int x = 4; x <= 10; ++x)
    {
        QString n = QStringLiteral("none"), s = QStringLiteral("none");
        if (x == 7)
        {
            n = QStringLiteral("side");
            s = QStringLiteral("side");
        }
        doc.setBlock(x, 1, 12,
                     wireState(8, n, (x < 10) ? QStringLiteral("side") : QStringLiteral("none"),
                               s, (x > 4) ? QStringLiteral("side") : QStringLiteral("none")));
    }
    for (int z = 9; z <= 15; ++z)
    {
        if (z == 12)
            continue;   // 交汇格已经写过
        doc.setBlock(7, 1, z,
                     wireState(8, (z > 9) ? QStringLiteral("side") : QStringLiteral("none"),
                               QStringLiteral("none"),
                               (z < 15) ? QStringLiteral("side") : QStringLiteral("none"),
                               QStringLiteral("none")));
    }

    // 3) 孤立点（暗红）
    doc.setBlock(2, 1, 8, wireState(3, QStringLiteral("none"), QStringLiteral("none"),
                                         QStringLiteral("none"), QStringLiteral("none")));

    // 4) 信号强度渐变：0 → 15
    for (int x = 2; x <= 17; ++x)
    {
        const QString e = (x < 17) ? QStringLiteral("side") : QStringLiteral("none");
        const QString w = (x > 2) ? QStringLiteral("side") : QStringLiteral("none");
        doc.setBlock(x, 1, 18, wireState(x - 2, QStringLiteral("none"), e,
                                         QStringLiteral("none"), w));
    }

    // 5) 爬坡：低位线遇到整块方块，方块顶上有另一段线
    SchematicBlockState stone;
    stone.name = QStringLiteral("minecraft:stone");
    doc.setBlock(16, 1, 16, stone);
    doc.setBlock(16, 2, 16, wireState(15, QStringLiteral("none"), QStringLiteral("none"),
                                           QStringLiteral("none"), QStringLiteral("none")));
    doc.setBlock(15, 1, 16, wireState(15, QStringLiteral("none"), QStringLiteral("up"),
                                           QStringLiteral("none"), QStringLiteral("none")));

    // 6) 旧格式转换过来的状态：只有方块名、没有连接属性，靠邻居推断
    SchematicBlockState bare;
    bare.name = QStringLiteral("minecraft:redstone_wire");
    for (int x = 4; x <= 6; ++x)
        doc.setBlock(x, 1, 16, bare);

    // 7) 四个方向的爬坡：方块两侧各放一段线，状态里写 up，用来检查竖条贴面朝向
    doc.setBlock(2, 1, 16, stone);
    doc.setBlock(2, 2, 16, wireState(15, QStringLiteral("none"), QStringLiteral("none"),
                                           QStringLiteral("none"), QStringLiteral("none")));
    doc.setBlock(2, 1, 15, wireState(15, QStringLiteral("none"), QStringLiteral("none"),
                                           QStringLiteral("up"), QStringLiteral("none")));
    doc.setBlock(2, 1, 17, wireState(15, QStringLiteral("up"), QStringLiteral("none"),
                                           QStringLiteral("none"), QStringLiteral("none")));

    return true;
}

/// 极简场景：一块石头 + 一格孤立红石线，用于面数检查（整块红砖会多出 5 个面）
bool buildTiny(SchematicDocument &doc)
{
    if (!doc.createEmpty(3, 2, 3))
        return false;
    SchematicBlockState stone;
    stone.name = QStringLiteral("minecraft:stone");
    doc.setBlock(1, 0, 1, stone);
    doc.setBlock(1, 1, 1, wireState(15, QStringLiteral("none"), QStringLiteral("none"),
                                         QStringLiteral("none"), QStringLiteral("none")));
    return true;
}

struct Analysis
{
    int red = 0;
    int green = 0;
    int total = 0;
    QRgb avgRed = 0;
};

Analysis analyze(const QImage &img)
{
    Analysis a;
    a.total = img.width() * img.height();
    qint64 r = 0, g = 0, b = 0;
    for (int y = 0; y < img.height(); ++y)
    {
        for (int x = 0; x < img.width(); ++x)
        {
            const QRgb c = img.pixel(x, y);
            const int cr = qRed(c), cg = qGreen(c), cb = qBlue(c);
            // 红石：红通道明显压过绿蓝（背景偏蓝、石头是灰的，不会误判）
            if (cr > 55 && cr > cg + 22 && cr > cb + 22)
            {
                ++a.red;
                r += cr;
                g += cg;
                b += cb;
            }
            else if (cg > 55 && cg > cr + 25 && cg > cb + 25)
            {
                ++a.green;
            }
        }
    }
    if (a.red > 0)
        a.avgRed = qRgb(int(r / a.red), int(g / a.red), int(b / a.red));
    return a;
}

QImage renderWidget(VoxelViewWidget &view, const QString &shotPath,
                    VoxelViewWidget::ViewPreset preset = VoxelViewWidget::ViewPreset::Top)
{
    view.show();
    pump(6);
    view.resetView();
    view.applyViewPreset(preset);
    view.focusAll();
    view.rebuild();
    pump(10);
    const QImage img = view.grabFramebuffer();
    if (!shotPath.isEmpty())
        img.save(shotPath);
    return img;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString samplePath = appDir + QStringLiteral("/redstone_probe_sample.litematic");
    const QString instancePath = QDir(appDir).absoluteFilePath(
        QStringLiteral("../../.tmp_redstone/versions/1.21.4"));

    SchematicDocument doc;
    expect("现造红石样例投影", buildSample(doc));
    QString saveError;
    expect("样例投影可保存", doc.saveCopy(samplePath, &saveError));
    fprintf(stderr, "[probe] 样例：%s\n", qPrintable(samplePath));

    // ---- 纯色块模式：没有模型/材质，红石线必须自己拼粉尘 ----
    {
        VoxelViewWidget view;
        view.resize(1000, 720);
        view.setDocument(&doc);
        view.setTextureLoader(nullptr);
        view.setShapeLoader(nullptr);
        const QImage img = renderWidget(view, appDir + QStringLiteral("/redstone_plain.png"));
        const Analysis a = analyze(img);
        fprintf(stderr, "[probe] 纯色块：红=%d 绿=%d 总像素=%d 平均红=(%d,%d,%d)\n",
                a.red, a.green, a.total, qRed(a.avgRed), qGreen(a.avgRed), qBlue(a.avgRed));
        expect("纯色块模式能画出红色粉尘", a.red > 120);
        expect("纯色块模式没有绿色红石线", a.green < 30);
        // 斜视角单独截一张：确认粉尘贴地、爬坡竖条朝向正确
        renderWidget(view, appDir + QStringLiteral("/redstone_plain_iso.png"),
                     VoxelViewWidget::ViewPreset::Iso);
        // 再拉近到爬坡处，确认竖条是贴在方块侧面、从下往上连通的
        {
            QSet<int> climb;
            const int layer = doc.layerSize();
            const int sx = doc.sizeX();
            climb.insert(1 * layer + 16 * sx + 15);
            climb.insert(1 * layer + 16 * sx + 16);
            climb.insert(2 * layer + 16 * sx + 16);
            view.focusOnCells(climb);
            pump(8);
            view.grabFramebuffer().save(appDir + QStringLiteral("/redstone_plain_climb.png"));
            // 南北两面的爬坡分别从正面 / 背面看，检查竖条没有因为背面剔除而消失
            climb.clear();
            climb.insert(1 * layer + 15 * sx + 2);
            climb.insert(1 * layer + 16 * sx + 2);
            climb.insert(1 * layer + 17 * sx + 2);
            view.focusOnCells(climb);
            view.applyViewPreset(VoxelViewWidget::ViewPreset::Front);
            pump(8);
            view.grabFramebuffer().save(
                appDir + QStringLiteral("/redstone_plain_climb_north.png"));
            view.applyViewPreset(VoxelViewWidget::ViewPreset::Back);
            pump(8);
            view.grabFramebuffer().save(
                appDir + QStringLiteral("/redstone_plain_climb_south.png"));
        }
    }

    // ---- 面数检查：孤立红石线不能是整块立方体 ----
    {
        SchematicDocument tiny;
        expect("现造单格红石场景", buildTiny(tiny));
        VoxelViewWidget view;
        view.resize(640, 480);
        view.setDocument(&tiny);
        view.setTextureLoader(nullptr);
        view.setShapeLoader(nullptr);
        renderWidget(view, appDir + QStringLiteral("/redstone_plain_tiny.png"));
        fprintf(stderr, "[probe] 单格场景面数：%d\n", view.quadCount());
        // 石头 6 面 + 粉尘 1 面 = 7；若红石线画成整块立方体则是 12
        expect("纯色模式下孤立红石线不是整块红砖", view.quadCount() <= 8);
    }

    // ---- 材质模式：有原版模型时走模型路径，但颜色必须乘信号强度 ----
    {
        InstanceTextureLoader *tex = InstanceTextureLoader::sharedForInstance(instancePath);
        BlockShapeLoader *shape = BlockShapeLoader::sharedForInstance(instancePath);
        fprintf(stderr, "[probe] 实例材质：贴图=%s 形状=%s\n",
                tex && tex->isValid() ? "OK" : "缺失", shape && shape->isValid() ? "OK" : "缺失");
        if (tex && tex->isValid() && shape && shape->isValid())
        {
            VoxelViewWidget view;
            view.resize(1000, 720);
            view.setTextureLoader(tex);
            view.setShapeLoader(shape);
            view.setDocument(&doc);
            const QImage img = renderWidget(view, appDir + QStringLiteral("/redstone_instance.png"));
            const Analysis a = analyze(img);
            fprintf(stderr, "[probe] 材质模式：红=%d 绿=%d 总像素=%d 平均红=(%d,%d,%d)\n",
                    a.red, a.green, a.total, qRed(a.avgRed), qGreen(a.avgRed), qBlue(a.avgRed));
            expect("材质模式红石线是红色（不是旧版的绿色）", a.red > 120);
            expect("材质模式没有绿色红石线", a.green < 30);
            renderWidget(view, appDir + QStringLiteral("/redstone_instance_iso.png"),
                         VoxelViewWidget::ViewPreset::Iso);
            {
                QSet<int> climb;
                const int layer = doc.layerSize();
                const int sx = doc.sizeX();
                climb.insert(1 * layer + 16 * sx + 15);
                climb.insert(1 * layer + 16 * sx + 16);
                climb.insert(2 * layer + 16 * sx + 16);
                view.focusOnCells(climb);
                pump(8);
                view.grabFramebuffer().save(
                    appDir + QStringLiteral("/redstone_instance_climb.png"));
            }
        }
        else
        {
            fprintf(stderr, "[probe] 跳过材质模式断言（本机没有可用的实例 jar）\n");
        }
    }

    // ---- 端到端：真正打开「投影预览」窗口，确认用户看到的画面也是红色的 ----
    {
        ProjectionPreviewDialog dialog(nullptr, samplePath, instancePath,
                                       QStringLiteral("红石测试"));
        dialog.resize(1000, 720);
        dialog.show();
        pump(6);
        auto *stats = dialog.findChild<QLabel *>(QStringLiteral("projectionPreviewStats"));
        auto *view = dialog.findChild<VoxelViewWidget *>(QStringLiteral("projectionPreviewView"));
        QElapsedTimer timer;
        timer.start();
        bool loaded = false;
        while (timer.elapsed() < 30000)
        {
            QThread::msleep(30);
            QApplication::processEvents();
            if (stats && !stats->text().isEmpty() && view && view->isVisible())
            {
                loaded = true;
                break;
            }
        }
        pump(20, 30);
        expect("投影预览窗口加载完成", loaded);
        if (view)
        {
            const QImage img = view->grabFramebuffer();
            img.save(appDir + QStringLiteral("/redstone_preview_dialog.png"));
            const Analysis a = analyze(img);
            fprintf(stderr, "[probe] 预览窗口：红=%d 绿=%d 平均红=(%d,%d,%d)\n",
                    a.red, a.green, qRed(a.avgRed), qGreen(a.avgRed), qBlue(a.avgRed));
            expect("投影预览窗口里的红石线是红色", a.red > 120);
            expect("投影预览窗口里没有绿色红石线", a.green < 30);

            // 切到「纯色块」再看一遍：没有实例材质时也必须是一条红粉尘，而不是红方块
            for (QComboBox *combo : dialog.findChildren<QComboBox *>(
                     QStringLiteral("projectionPreviewCombo")))
            {
                if (combo->count() == 2 && combo->itemText(1) == QStringLiteral("纯色块"))
                {
                    combo->setCurrentIndex(1);
                    pump(16, 30);
                    const QImage plainImg = view->grabFramebuffer();
                    plainImg.save(appDir + QStringLiteral("/redstone_preview_dialog_plain.png"));
                    const Analysis pa = analyze(plainImg);
                    fprintf(stderr, "[probe] 预览窗口纯色块：红=%d 绿=%d\n", pa.red, pa.green);
                    expect("预览窗口切纯色块后红石线仍是红色", pa.red > 120);
                    expect("预览窗口切纯色块后没有绿色红石线", pa.green < 30);
                    // 再检查一次切到纯色块后地面是不是按方块本色显示：换加载器时若没有
                    // 作废调色板缓存，旧条目还会带着「用贴图」的标记，拿白色兜底图集把
                    // 整份模型刷白，切到纯色块反而比材质模式还白
                    view->setTextureLoader(nullptr);
                    view->setShapeLoader(nullptr);
                    view->rebuild();
                    pump(10, 30);
                    const QImage again = view->grabFramebuffer();
                    again.save(appDir + QStringLiteral("/redstone_preview_dialog_plain2.png"));
                    const QColor floor = again.pixelColor(again.width() / 2,
                                                         again.height() * 72 / 100);
                    fprintf(stderr, "[probe] 纯色块地面色=(%d,%d,%d)\n",
                            floor.red(), floor.green(), floor.blue());
                    expect("纯色块地面按方块本色显示（不发白）",
                           floor.red() < 190 && qAbs(floor.red() - floor.green()) < 20);
                    break;
                }
            }
        }
        else
        {
            expect("投影预览窗口里有三维视图", false);
        }
    }

    // ---- 单独检查「材质 → 纯色块」切换后地面颜色（用于定位切换路径的问题）----
    {
        SchematicDocument loaded;
        loaded.load(samplePath);
        VoxelViewWidget view;
        view.resize(640, 480);
        view.setTextureLoader(InstanceTextureLoader::sharedForInstance(instancePath));
        view.setShapeLoader(BlockShapeLoader::sharedForInstance(instancePath));
        view.setDocument(&loaded);
        view.show();
        pump(8);
        view.resetView();
        view.focusAll();
        view.rebuild();
        pump(8);
        QImage before = view.grabFramebuffer();
        const int fx = before.width() / 2;
        const int fy = before.height() * 72 / 100;
        fprintf(stderr, "[probe] 独立切换前地面=(%d,%d,%d)\n",
                qRed(before.pixel(fx, fy)), qGreen(before.pixel(fx, fy)),
                qBlue(before.pixel(fx, fy)));
        view.setTextureLoader(nullptr);
        view.setShapeLoader(nullptr);
        view.rebuild();
        pump(8);
        QImage after = view.grabFramebuffer();
        after.save(appDir + QStringLiteral("/redstone_widget_switch_plain.png"));
        const QColor afterFloor = after.pixelColor(fx, fy);
        fprintf(stderr, "[probe] 独立切换后地面=(%d,%d,%d) 红=%d 绿=%d\n",
                afterFloor.red(), afterFloor.green(), afterFloor.blue(),
                analyze(after).red, analyze(after).green);
        expect("独立切换材质后地面不发白",
               afterFloor.red() < 190 && qAbs(afterFloor.red() - afterFloor.green()) < 20);
    }

    fprintf(stderr, "[probe] failures=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
