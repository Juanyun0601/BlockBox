/**
 * 探针：验证投影方块编辑器对话框里的工具 → 左键动作映射，
 * 以及「贴面放置」默认勾选是否真的生效；并用真实鼠标事件走完整编辑链路。
 * 用法：dlg_probe.exe <litematic>
 */

#include "components/ProjectionBlockEditorDialog.h"
#include "components/VoxelViewWidget.h"
#include "utils/Schematic/SchematicDocument.h"

#include <QApplication>
#include <QComboBox>
#include <QMouseEvent>
#include <QPoint>
#include <QString>
#include <QToolButton>
#include <cstdio>
#include <cstdlib>

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

void click(VoxelViewWidget *v, const QPoint &p, Qt::MouseButton b)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(p), v->mapToGlobal(p), b, b,
                      Qt::NoModifier);
    QApplication::sendEvent(v, &press);
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(p), v->mapToGlobal(p), b,
                    Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(v, &rel);
}

QPoint findBlockPoint(VoxelViewWidget *v, int &hx, int &hy, int &hz,
                      bool interiorOnly = false)
{
    const SchematicDocument *doc = v->document();
    for (int y = 12; y < v->height(); y += 5)
    {
        for (int x = 12; x < v->width(); x += 5)
        {
            int ax = 0, ay = 0, az = 0;
            if (!v->rayPick(QPoint(x, y), ax, ay, az))
                continue;
            if (doc->block(ax, ay, az).isAir())
                continue;
            if (interiorOnly
                && (ax < 1 || ay < 1 || az < 1
                    || ax > doc->sizeX() - 2 || ay > doc->sizeY() - 2
                    || az > doc->sizeZ() - 2))
                continue;
            hx = ax;
            hy = ay;
            hz = az;
            return QPoint(x, y);
        }
    }
    return QPoint(-1, -1);
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const QString file = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QString();
    if (file.isEmpty())
    {
        std::printf("usage: dlg_probe <litematic>\n");
        return 2;
    }

    ProjectionBlockEditorDialog dlg;
    dlg.resize(1500, 1000);
    if (!dlg.loadProjection(file))
    {
        std::printf("[FAIL] loadProjection\n");
        return 2;
    }

    auto *view = dlg.findChild<VoxelViewWidget *>();
    auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("projToolCombo"));
    check(view != nullptr, "找到 3D 视图");
    check(combo != nullptr, "找到工具下拉");
    if (!view || !combo)
        return 2;
    // 不显示窗口（避免依赖 OpenGL 上下文），直接给视口一个尺寸供射线拾取使用
    view->resize(960, 680);

    // ---------- 工具 → 左键动作 ----------
    combo->setCurrentIndex(0);   // 查看
    check(view->leftAction() == VoxelViewWidget::LeftAction::Break, "查看工具：左键=破坏");
    combo->setCurrentIndex(1);   // 放置
    check(view->leftAction() == VoxelViewWidget::LeftAction::Place, "放置工具：左键=放置");
    combo->setCurrentIndex(2);   // 破坏
    check(view->leftAction() == VoxelViewWidget::LeftAction::Break, "破坏工具：左键=破坏");
    combo->setCurrentIndex(3);   // 填充
    check(view->leftAction() == VoxelViewWidget::LeftAction::Break, "填充工具：左键=破坏");
    combo->setCurrentIndex(4);   // 选取
    check(view->leftAction() == VoxelViewWidget::LeftAction::Select, "选取工具：左键=选取");

    // ---------- 贴面放置默认勾选同步 ----------
    QToolButton *adjacentBtn = nullptr;
    const QList<QToolButton *> toggles = dlg.findChildren<QToolButton *>();
    for (QToolButton *btn : toggles)
    {
        if (btn->text() == QStringLiteral("贴面放置"))
        {
            adjacentBtn = btn;
            break;
        }
    }
    check(adjacentBtn != nullptr, "找到「贴面放置」开关");
    if (adjacentBtn)
    {
        check(adjacentBtn->isChecked(), "「贴面放置」默认勾选");
        check(view->placeAdjacent(), "默认勾选已真正生效（无需手动切换）",
              QStringLiteral("view.placeAdjacent=%1").arg(view->placeAdjacent()));
        adjacentBtn->setChecked(false);
        check(!view->placeAdjacent(), "取消勾选后立即关闭贴面放置");
        adjacentBtn->setChecked(true);
        check(view->placeAdjacent(), "重新勾选后立即开启贴面放置");
    }

    // ---------- 真实点击：左键破坏 ----------
    combo->setCurrentIndex(0);   // 查看工具（默认）
    {
        int hx = 0, hy = 0, hz = 0;
        const QPoint pt = findBlockPoint(view, hx, hy, hz);
        check(pt.x() >= 0, "找到可命中的方块点", QStringLiteral("格(%1,%2,%3)").arg(hx).arg(hy).arg(hz));
        if (pt.x() >= 0)
        {
            click(view, pt, Qt::LeftButton);
            check(view->document()->block(hx, hy, hz).isAir(),
                  "对话框内左键单击 = 破坏方块",
                  QStringLiteral("格(%1,%2,%3)").arg(hx).arg(hy).arg(hz));
        }
    }

    // ---------- 真实点击：右键放置到相邻格 ----------
    {
        const QList<int> before = view->document()->indices();
        int hx = 0, hy = 0, hz = 0;
        const QPoint pt = findBlockPoint(view, hx, hy, hz, /*interiorOnly=*/true);
        check(pt.x() >= 0, "找到内部格命中点", QStringLiteral("格(%1,%2,%3)").arg(hx).arg(hy).arg(hz));
        if (pt.x() >= 0)
        {
            click(view, pt, Qt::RightButton);
            const QList<int> after = view->document()->indices();
            int diffs = 0, dx = -1, dy = -1, dz = -1;
            const int layerSize = view->document()->layerSize();
            const int sizeX = view->document()->sizeX();
            for (int i = 0; i < after.size(); ++i)
            {
                if (after.at(i) != before.at(i))
                {
                    ++diffs;
                    const int y = i / layerSize;
                    const int rem = i % layerSize;
                    dz = rem / sizeX;
                    dx = rem % sizeX;
                    dy = y;
                }
            }
            const int dist = qAbs(dx - hx) + qAbs(dy - hy) + qAbs(dz - hz);
            check(diffs == 1, "对话框内右键单击只改动 1 格",
                  QStringLiteral("diffs=%1").arg(diffs));
            check(diffs == 1 && dist == 1, "右键放置落在命中格的相邻面",
                  QStringLiteral("place=(%1,%2,%3) hit=(%4,%5,%6)")
                  .arg(dx).arg(dy).arg(dz).arg(hx).arg(hy).arg(hz));
            check(diffs == 1 && !view->document()->block(dx, dy, dz).isAir(),
                  "放置后该格有方块");
        }
    }

    std::printf("\n==== %s（失败 %d 项）====\n", g_failed == 0 ? "全部通过" : "有失败项", g_failed);
    std::fflush(stdout);
    std::_Exit(g_failed == 0 ? 0 : 1);
}
