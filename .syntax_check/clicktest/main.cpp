/**
 * 探针：验证 VoxelViewWidget 的新鼠标映射
 *   左键单击 = 破坏、右键单击 = 放置（贴面/覆盖）、拖动 = 只转向不编辑。
 * 用法：click_probe.exe <litematic>
 */

#include "components/VoxelViewWidget.h"
#include "utils/Schematic/SchematicDocument.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPoint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
int g_failed = 0;

void check(bool ok, const char *name, const QString &detail = QString())
{
    std::printf("%s %s%s%s\n", ok ? "[PASS]" : "[FAIL]", name,
                detail.isEmpty() ? "" : "  ", qPrintable(detail));
    if (!ok)
        ++g_failed;
}

struct Rec
{
    int breaks = 0, places = 0, selects = 0;
    int bx = -99, by = -99, bz = -99;
    int px = -99, py = -99, pz = -99;
    int sx = -99, sy = -99, sz = -99;
};

void sendPress(VoxelViewWidget *v, const QPoint &p, Qt::MouseButton b, Qt::KeyboardModifiers m)
{
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(p), v->mapToGlobal(p), b, b, m);
    QApplication::sendEvent(v, &ev);
}

void sendRelease(VoxelViewWidget *v, const QPoint &p, Qt::MouseButton b, Qt::KeyboardModifiers m)
{
    QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(p), v->mapToGlobal(p), b,
                   Qt::NoButton, m);
    QApplication::sendEvent(v, &ev);
}

void sendMove(VoxelViewWidget *v, const QPoint &p, Qt::MouseButton b, Qt::KeyboardModifiers m)
{
    QMouseEvent ev(QEvent::MouseMove, QPointF(p), v->mapToGlobal(p), Qt::NoButton, b, m);
    QApplication::sendEvent(v, &ev);
}

void click(VoxelViewWidget *v, const QPoint &p, Qt::MouseButton b, Qt::KeyboardModifiers m = Qt::NoModifier)
{
    sendPress(v, p, b, m);
    sendRelease(v, p, b, m);
}

/** 找一个射线能命中非空气方块的屏幕点 */
QPoint findBlockPoint(VoxelViewWidget *v, int &hx, int &hy, int &hz)
{
    for (int y = 12; y < v->height(); y += 5)
    {
        for (int x = 12; x < v->width(); x += 5)
        {
            int ax = 0, ay = 0, az = 0;
            if (!v->rayPick(QPoint(x, y), ax, ay, az))
                continue;
            if (v->document()->block(ax, ay, az).isAir())
                continue;
            hx = ax;
            hy = ay;
            hz = az;
            return QPoint(x, y);
        }
    }
    return QPoint(-1, -1);
}

void hookSignals(VoxelViewWidget *v, Rec &rec)
{
    QObject::connect(v, &VoxelViewWidget::breakRequested, v, [v, &rec](int x, int y, int z) {
        ++rec.breaks;
        rec.bx = x; rec.by = y; rec.bz = z;
        QString err;
        v->document()->setBlock(x, y, z,
                                SchematicBlockState{ QStringLiteral("minecraft:air"), {} }, &err);
        v->setDocument(v->document());
    });
    QObject::connect(v, &VoxelViewWidget::placeRequested, v, [v, &rec](int x, int y, int z) {
        ++rec.places;
        rec.px = x; rec.py = y; rec.pz = z;
        QString err;
        v->document()->setBlock(x, y, z,
                                SchematicBlockState{ QStringLiteral("minecraft:stone"), {} }, &err);
        v->setDocument(v->document());
    });
    QObject::connect(v, &VoxelViewWidget::selectRequested, v, [&rec](int x, int y, int z) {
        ++rec.selects;
        rec.sx = x; rec.sy = y; rec.sz = z;
    });
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const QString file = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QString();
    if (file.isEmpty())
    {
        std::printf("usage: click_probe <litematic>\n");
        return 2;
    }

    SchematicDocument doc;
    if (!doc.load(file))
    {
        std::printf("[FAIL] 无法加载 %s\n", qPrintable(file));
        return 2;
    }
    std::printf("[info] 投影 %d×%d×%d origin=(%d,%d,%d)\n",
                doc.sizeX(), doc.sizeY(), doc.sizeZ(),
                doc.originX(), doc.originY(), doc.originZ());

    VoxelViewWidget *v = new VoxelViewWidget();
    v->resize(960, 680);
    v->setDocument(&doc);
    v->resetView();

    Rec rec;
    hookSignals(v, rec);

    // ---------- 1) 左键单击 = 破坏 ----------
    {
        // 每次测试前把文档恢复原样（重新加载）
        doc.load(file);
        v->setDocument(&doc);
        v->resetView();

        int hx = 0, hy = 0, hz = 0;
        const QPoint pt = findBlockPoint(v, hx, hy, hz);
        check(pt.x() >= 0, "找到可命中的方块点", QStringLiteral("(%1,%2) 格(%3,%4,%5)")
              .arg(pt.x()).arg(pt.y()).arg(hx).arg(hy).arg(hz));
        rec.breaks = rec.places = rec.selects = 0;
        click(v, pt, Qt::LeftButton);
        check(rec.breaks == 1 && rec.bx == hx && rec.by == hy && rec.bz == hz,
              "左键单击破坏命中格",
              QStringLiteral("breaks=%1 (%2,%3,%4) expect (%5,%6,%7)")
              .arg(rec.breaks).arg(rec.bx).arg(rec.by).arg(rec.bz).arg(hx).arg(hy).arg(hz));
        check(doc.block(hx, hy, hz).isAir(), "破坏后该格变为空气");
        check(rec.places == 0, "左键单击不会放置");
    }

    // ---------- 2) 右键单击 = 放置（覆盖模式）----------
    {
        doc.load(file);
        v->setDocument(&doc);
        v->resetView();
        v->setLeftAction(VoxelViewWidget::LeftAction::Break);
        v->setPlaceAdjacent(false);

        rec.breaks = rec.places = rec.selects = 0;
        int hx = 0, hy = 0, hz = 0;
        const QPoint pt = findBlockPoint(v, hx, hy, hz);
        click(v, pt, Qt::RightButton);
        check(rec.places == 1 && rec.px == hx && rec.py == hy && rec.pz == hz,
              "右键单击放置到命中格",
              QStringLiteral("places=%1 (%2,%3,%4) expect (%5,%6,%7)")
              .arg(rec.places).arg(rec.px).arg(rec.py).arg(rec.pz).arg(hx).arg(hy).arg(hz));
        check(!doc.block(rec.px, rec.py, rec.pz).isAir(), "放置后该格有方块");
        check(rec.breaks == 0, "右键单击不会破坏");
    }

    // ---------- 3) 右键 + 贴面放置 = 相邻格 ----------
    {
        doc.load(file);
        v->setDocument(&doc);
        v->resetView();
        v->setPlaceAdjacent(true);

        rec.breaks = rec.places = rec.selects = 0;
        // 只挑「内部格」：任一轴向 ±1 都还在投影范围内，避免贴面目标越界回退成覆盖
        int hx = -1, hy = -1, hz = -1;
        QPoint pt(-1, -1);
        for (int y = 12; y < v->height() && pt.x() < 0; y += 5)
        {
            for (int x = 12; x < v->width() && pt.x() < 0; x += 5)
            {
                int ax = 0, ay = 0, az = 0;
                if (!v->rayPick(QPoint(x, y), ax, ay, az))
                    continue;
                if (doc.block(ax, ay, az).isAir())
                    continue;
                if (ax < 1 || ay < 1 || az < 1
                    || ax > doc.sizeX() - 2 || ay > doc.sizeY() - 2 || az > doc.sizeZ() - 2)
                    continue;
                hx = ax; hy = ay; hz = az;
                pt = QPoint(x, y);
            }
        }
        check(pt.x() >= 0, "贴面放置：找到内部格命中点",
              QStringLiteral("(%1,%2) 格(%3,%4,%5)").arg(hx).arg(hy).arg(hz).arg(pt.x()).arg(pt.y()));
        if (pt.x() >= 0)
        {
            click(v, pt, Qt::RightButton);
            const bool same = (rec.px == hx && rec.py == hy && rec.pz == hz);
            const int manhattan = qAbs(rec.px - hx) + qAbs(rec.py - hy) + qAbs(rec.pz - hz);
            check(rec.places == 1, "贴面放置：右键仍触发一次放置");
            check(!same && manhattan == 1, "贴面放置：目标为相邻格",
                  QStringLiteral("place=(%1,%2,%3) hit=(%4,%5,%6)")
                  .arg(rec.px).arg(rec.py).arg(rec.pz).arg(hx).arg(hy).arg(hz));
            check(doc.contains(rec.px, rec.py, rec.pz), "贴面放置：相邻格在投影范围内");
        }
        v->setPlaceAdjacent(false);
    }

    // ---------- 4) 左键拖动 = 只转向，不破坏 ----------
    {
        doc.load(file);
        v->setDocument(&doc);
        v->resetView();

        rec.breaks = rec.places = rec.selects = 0;
        int hx = 0, hy = 0, hz = 0;
        const QPoint pt = findBlockPoint(v, hx, hy, hz);
        sendPress(v, pt, Qt::LeftButton, Qt::NoModifier);
        sendMove(v, pt + QPoint(60, 25), Qt::LeftButton, Qt::NoModifier);
        sendMove(v, pt + QPoint(120, 40), Qt::LeftButton, Qt::NoModifier);
        sendRelease(v, pt + QPoint(120, 40), Qt::LeftButton, Qt::NoModifier);
        check(rec.breaks == 0, "左键拖动只转向，不破坏方块");
        check(!doc.block(hx, hy, hz).isAir(), "拖动后命中格原样保留");
    }

    // ---------- 5) 右键拖动 = 只转向，不放置 ----------
    {
        doc.load(file);
        v->setDocument(&doc);
        v->resetView();

        rec.breaks = rec.places = rec.selects = 0;
        int hx = 0, hy = 0, hz = 0;
        const QPoint pt = findBlockPoint(v, hx, hy, hz);
        sendPress(v, pt, Qt::RightButton, Qt::NoModifier);
        sendMove(v, pt + QPoint(70, 30), Qt::RightButton, Qt::NoModifier);
        sendRelease(v, pt + QPoint(70, 30), Qt::RightButton, Qt::NoModifier);
        check(rec.places == 0, "右键拖动只转向，不放置方块");
    }

    // ---------- 6) 「放置」工具：左键放置 ----------
    {
        doc.load(file);
        v->setDocument(&doc);
        v->resetView();
        v->setLeftAction(VoxelViewWidget::LeftAction::Place);
        v->setPlaceAdjacent(false);

        rec.breaks = rec.places = rec.selects = 0;
        int hx = 0, hy = 0, hz = 0;
        const QPoint pt = findBlockPoint(v, hx, hy, hz);
        click(v, pt, Qt::LeftButton);
        check(rec.places == 1 && rec.px == hx && rec.py == hy && rec.pz == hz,
              "放置工具下左键放置", QStringLiteral("places=%1").arg(rec.places));
        check(rec.breaks == 0, "放置工具下左键不破坏");
    }

    // ---------- 7) 「选取」工具：左键选取 ----------
    {
        doc.load(file);
        v->setDocument(&doc);
        v->resetView();
        v->setLeftAction(VoxelViewWidget::LeftAction::Select);

        rec.breaks = rec.places = rec.selects = 0;
        int hx = 0, hy = 0, hz = 0;
        const QPoint pt = findBlockPoint(v, hx, hy, hz);
        click(v, pt, Qt::LeftButton);
        check(rec.selects == 1 && rec.sx == hx && rec.sy == hy && rec.sz == hz,
              "选取工具下左键选取", QStringLiteral("selects=%1").arg(rec.selects));
        check(rec.breaks == 0, "选取工具下左键不破坏");

        rec.places = 0;
        click(v, pt, Qt::RightButton);
        check(rec.places == 1, "选取工具下右键仍可放置");
        v->setLeftAction(VoxelViewWidget::LeftAction::Break);
    }

    // ---------- 8) Ctrl+左键保留为放置 ----------
    {
        doc.load(file);
        v->setDocument(&doc);
        v->resetView();
        v->setPlaceAdjacent(false);

        rec.breaks = rec.places = rec.selects = 0;
        int hx = 0, hy = 0, hz = 0;
        const QPoint pt = findBlockPoint(v, hx, hy, hz);
        sendPress(v, pt, Qt::LeftButton, Qt::ControlModifier);
        sendRelease(v, pt, Qt::LeftButton, Qt::ControlModifier);
        check(rec.places == 1 && rec.px == hx && rec.py == hy && rec.pz == hz,
              "Ctrl+左键仍可放置", QStringLiteral("places=%1").arg(rec.places));
        check(rec.breaks == 0, "Ctrl+左键不会破坏");
    }

    // ---------- 9) 点空白处：既不破坏也不放置 ----------
    {
        doc.load(file);
        v->setDocument(&doc);
        v->resetView();

        rec.breaks = rec.places = rec.selects = 0;
        bool foundMiss = false;
        QPoint miss;
        for (int y = 4; y < v->height() && !foundMiss; y += 9)
            for (int x = 4; x < v->width() && !foundMiss; x += 9)
            {
                int ax = 0, ay = 0, az = 0;
                if (!v->rayPick(QPoint(x, y), ax, ay, az))
                {
                    miss = QPoint(x, y);
                    foundMiss = true;
                }
            }
        if (foundMiss)
        {
            click(v, miss, Qt::LeftButton);
            click(v, miss, Qt::RightButton);
            check(rec.breaks == 0 && rec.places == 0, "点空白处不编辑任何方块",
                  QStringLiteral("breaks=%1 places=%2").arg(rec.breaks).arg(rec.places));
        }
        else
        {
            std::printf("[skip] 视口内没有空白像素\n");
        }
    }

    // ---------- 10) 显示空气时：空气格右键直接填入该格，左键不产生空破坏 ----------
    {
        doc.load(file);
        v->setShowAir(true);
        v->setDocument(&doc);
        v->resetView();

        rec.breaks = rec.places = rec.selects = 0;
        int ax0 = -1, ay0 = -1, az0 = -1;
        QPoint pt(-1, -1);
        for (int y = 12; y < v->height() && pt.x() < 0; y += 4)
        {
            for (int x = 12; x < v->width() && pt.x() < 0; x += 4)
            {
                int ax = 0, ay = 0, az = 0;
                if (!v->rayPick(QPoint(x, y), ax, ay, az))
                    continue;
                if (!doc.block(ax, ay, az).isAir())
                    continue;
                ax0 = ax; ay0 = ay; az0 = az;
                pt = QPoint(x, y);
            }
        }
        check(pt.x() >= 0, "显示空气：找到空气格命中点",
              QStringLiteral("格(%1,%2,%3)").arg(ax0).arg(ay0).arg(az0));
        if (pt.x() >= 0)
        {
            click(v, pt, Qt::LeftButton);
            check(rec.breaks == 0, "显示空气：左键点空气格不产生破坏记录",
                  QStringLiteral("breaks=%1").arg(rec.breaks));

            click(v, pt, Qt::RightButton);
            check(rec.places == 1 && rec.px == ax0 && rec.py == ay0 && rec.pz == az0,
                  "显示空气：右键把方块放进该空气格",
                  QStringLiteral("place=(%1,%2,%3) expect=(%4,%5,%6)")
                  .arg(rec.px).arg(rec.py).arg(rec.pz).arg(ax0).arg(ay0).arg(az0));
        }
        v->setShowAir(false);
        doc.load(file);
        v->setDocument(&doc);
    }

    std::printf("\n==== %s（失败 %d 项）====\n", g_failed == 0 ? "全部通过" : "有失败项", g_failed);
    std::fflush(stdout);
    std::_Exit(g_failed == 0 ? 0 : 1);
}
