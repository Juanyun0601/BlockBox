/**
 * @file   projection_box_select_probe.cpp
 * @brief  投影方块编辑器「两点框选」回归探针
 *
 * 链接主程序全部目标文件（仅替换 main.cpp），直接构造 ProjectionBlockEditorDialog：
 *   1. 加载一份测试 .litematic；
 *   2. 把「选取」范围切到「框选（两点）」，此时应自动切到「选取」模式
 *      （否则在编辑模式下左键点角点会破坏方块）；
 *   3. 依次点两个非空气方块，检查：
 *        第一次点击 → 只出现一个金色角点标记，尚未有选中；
 *        第二次点击 → 选中数等于独立算出的「两点立方体内非空气方块数」；
 *        第三次点击 → 重新开始框选，旧选中被清掉、角点标记重新出现；
 *        取消 → 角点标记与选中一并清空。
 *   4. 截图保存到 probe_out/，便于人工确认绿色选中框与金色角点。
 *
 * 任一断言不符时打印 PROBE-FAIL 并以非 0 退出。
 */

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QElapsedTimer>
#include <QLabel>
#include <QMetaObject>
#include <QTimer>
#include <QWidget>

#include <cstdio>

#include "components/ProjectionBlockEditorDialog.h"
#include "components/VoxelViewWidget.h"
#include "utils/Schematic/SchematicDocument.h"

namespace {

int g_failures = 0;

void stderrMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx)
    const char *tag = "[info ]";
    if (type == QtWarningMsg) tag = "[warn ]";
    else if (type == QtCriticalMsg || type == QtFatalMsg) tag = "[crit ]";
    fprintf(stderr, "%s %s\n", tag, msg.toLocal8Bit().constData());
    fflush(stderr);
}

void check(bool ok, const QString &what, const QString &detail = QString())
{
    if (ok)
        qInfo().noquote() << QStringLiteral("[OK  ] %1 %2").arg(what, detail);
    else
    {
        ++g_failures;
        qInfo().noquote() << QStringLiteral("[FAIL] %1 %2").arg(what, detail);
    }
}

/** 编辑器侧栏所有提示标签的文字（选取提示就在里面） */
QStringList mutedTexts(QWidget *root)
{
    QStringList out;
    for (QLabel *label : root->findChildren<QLabel *>())
    {
        if (label->objectName() == QLatin1String("projMutedLabel") && !label->text().isEmpty())
            out << label->text();
    }
    return out;
}

/** 找出「选取范围」下拉框：三个选项、最后一项是框选 */
QComboBox *findScopeCombo(QWidget *root)
{
    for (QComboBox *combo : root->findChildren<QComboBox *>())
    {
        if (combo->count() == 3
            && combo->itemText(combo->count() - 1).contains(QStringLiteral("框选")))
            return combo;
    }
    return nullptr;
}

struct Pt { int x, y, z; };

bool nonAir(const SchematicDocument &doc, const Pt &p)
{
    return !doc.block(p.x, p.y, p.z).isAir();
}

/** 两点立方体（含边界）内非空气方块数量：与编辑器内部算法独立的一份实现 */
int countBoxBlocks(const SchematicDocument &doc, const Pt &a, const Pt &b)
{
    int n = 0;
    const int x0 = qMin(a.x, b.x), x1 = qMax(a.x, b.x);
    const int y0 = qMin(a.y, b.y), y1 = qMax(a.y, b.y);
    const int z0 = qMin(a.z, b.z), z1 = qMax(a.z, b.z);
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (!doc.block(x, y, z).isAir())
                    ++n;
    return n;
}

void click(ProjectionBlockEditorDialog *dlg, const Pt &p)
{
    QMetaObject::invokeMethod(dlg, "onSelect", Qt::DirectConnection,
                              Q_ARG(int, p.x), Q_ARG(int, p.y), Q_ARG(int, p.z));
}

} // namespace

int main(int argc, char *argv[])
{
    qInstallMessageHandler(stderrMessageHandler);
    QApplication app(argc, argv);

    QString file = QStringLiteral("D:/BlockBox/C-BlockBox/.syntax_check/test_proj.litematic");
    if (argc > 1)
        file = QString::fromLocal8Bit(argv[1]);

    // 独立加载一份文档：用来挑两个非空气角点，并算出期望的选中数量
    SchematicDocument doc;
    QString err;
    if (!doc.load(file, &err))
    {
        qCritical().noquote() << QStringLiteral("PROBE-FAIL: 无法加载测试投影 %1 (%2)").arg(file, err);
        return 2;
    }
    qInfo().noquote() << QStringLiteral("投影 %1 尺寸 %2x%3x%4 原点 (%5,%6,%7) 体积 %8")
                             .arg(file).arg(doc.sizeX()).arg(doc.sizeY()).arg(doc.sizeZ())
                             .arg(doc.originX()).arg(doc.originY()).arg(doc.originZ())
                             .arg(doc.volume());

    // 挑角点：第一个非空气方块，以及与它曼哈顿距离最远的非空气方块
    bool found = false;
    Pt first{0, 0, 0};
    for (int y = 0; y < doc.sizeY() && !found; ++y)
        for (int z = 0; z < doc.sizeZ() && !found; ++z)
            for (int x = 0; x < doc.sizeX() && !found; ++x)
            {
                Pt p{ x + doc.originX(), y + doc.originY(), z + doc.originZ() };
                if (nonAir(doc, p)) { first = p; found = true; }
            }
    if (!found)
    {
        qCritical() << "PROBE-FAIL: 测试投影里没有非空气方块";
        return 2;
    }

    Pt far = first;
    int farDist = -1;
    for (int y = 0; y < doc.sizeY(); ++y)
        for (int z = 0; z < doc.sizeZ(); ++z)
            for (int x = 0; x < doc.sizeX(); ++x)
            {
                Pt p{ x + doc.originX(), y + doc.originY(), z + doc.originZ() };
                if (!nonAir(doc, p))
                    continue;
                const int d = qAbs(p.x - first.x) + qAbs(p.y - first.y) + qAbs(p.z - first.z);
                if (d > farDist) { farDist = d; far = p; }
            }

    const int expected = countBoxBlocks(doc, first, far);
    qInfo().noquote() << QStringLiteral("角点 A (%1,%2,%3) → 角点 B (%4,%5,%6)，期望选中 %7 个方块")
                             .arg(first.x).arg(first.y).arg(first.z)
                             .arg(far.x).arg(far.y).arg(far.z).arg(expected);

    auto *dlg = new ProjectionBlockEditorDialog();
    dlg->resize(1360, 860);
    dlg->show();

    QTimer::singleShot(1200, &app, [&, dlg]() {
        if (!dlg->loadProjection(file))
        {
            qCritical() << "PROBE-FAIL: 编辑器加载投影失败";
            app.exit(3);
            return;
        }
        QApplication::processEvents();

        VoxelViewWidget *view = dlg->findChild<VoxelViewWidget *>();
        QComboBox *scope = findScopeCombo(dlg);
        check(view != nullptr, "三维视图存在");
        check(scope != nullptr, "找到「选取范围」下拉框");
        if (!view || !scope)
        {
            qCritical() << "PROBE-FAIL: 控件缺失";
            app.exit(3);
            return;
        }

        // 1) 切到框选：应自动切到「选取」模式（左键 = 选取，不会破坏方块）
        scope->setCurrentIndex(2);
        QApplication::processEvents();
        check(view->leftAction() == VoxelViewWidget::LeftAction::Select,
              "切到框选后左键动作自动变为「选取」");
        check(view->selectedCells().isEmpty(), "进入框选时选中被清空");
        qInfo().noquote() << "  提示:" << mutedTexts(dlg).join(QStringLiteral(" | "));

        // 2) 第一次点击：只标记第一个角点
        click(dlg, first);
        QApplication::processEvents();
        check(view->highlightCells().size() == 1,
              "第一次点击出现单个金色角点标记",
              QStringLiteral("(highlight=%1)").arg(view->highlightCells().size()));
        check(view->selectedCells().isEmpty(), "第一次点击还没有选中任何方块");
        const QStringList afterFirst = mutedTexts(dlg);
        check(afterFirst.filter(QStringLiteral("第一个角点")).size() == 1,
              "提示语切换为「已标记第一个角点」");
        qInfo().noquote() << "  提示:" << afterFirst.join(QStringLiteral(" | "));
        // 待定角点截图：人工确认金色标记够醒目
        view->grabFramebuffer().save(QStringLiteral("probe_out/proj_box_pivot_view.png"));

        // 3) 第二次点击：整块选中（顺带量一下大范围框选耗时）
        QElapsedTimer timer;
        timer.start();
        click(dlg, far);
        QApplication::processEvents();
        const qint64 boxMs = timer.elapsed();
        check(view->selectedCells().size() == expected,
              "第二次点击选中整块区域",
              QStringLiteral("(实际 %1 / 期望 %2，耗时 %3 ms)")
                  .arg(view->selectedCells().size()).arg(expected).arg(boxMs));
        check(view->highlightCells().isEmpty(), "完成框选后角点标记收起");
        const QStringList afterSecond = mutedTexts(dlg);
        check(afterSecond.filter(QStringLiteral("已框选")).size() == 1,
              "提示语显示框选范围与方块数");
        qInfo().noquote() << "  提示:" << afterSecond.join(QStringLiteral(" | "));

        // 截图：整窗 + 三维视图本身
        const bool shot1 = dlg->grab().save(QStringLiteral("probe_out/proj_box_select_dialog.png"));
        const bool shot2 = view->grabFramebuffer()
                               .save(QStringLiteral("probe_out/proj_box_select_view.png"));
        qInfo().noquote() << QStringLiteral("[shot] 窗口=%1 视图=%2")
                                 .arg(shot1 ? "ok" : "FAILED", shot2 ? "ok" : "FAILED");

        // 3.5) 小块框选（最典型的用法）：从 first 往 +X/+Y/+Z 找一个近距离非空气方块
        Pt small{ first.x + 3, first.y + 2, first.z + 3 };
        bool smallOk = nonAir(doc, small);
        for (int r = 1; !smallOk && r <= 4; ++r)
        {
            for (int dy = -r; dy <= r && !smallOk; ++dy)
                for (int dz = -r; dz <= r && !smallOk; ++dz)
                    for (int dx = -r; dx <= r && !smallOk; ++dx)
                    {
                        Pt p{ first.x + 3 + dx, first.y + 2 + dy, first.z + 3 + dz };
                        if (nonAir(doc, p)) { small = p; smallOk = true; }
                    }
        }
        if (smallOk)
        {
            click(dlg, first);
            QApplication::processEvents();
            timer.restart();
            click(dlg, small);
            QApplication::processEvents();
            const int smallExpected = countBoxBlocks(doc, first, small);
            check(view->selectedCells().size() == smallExpected,
                  "小范围框选数量正确",
                  QStringLiteral("(%1×%2×%3 内 %4 个，耗时 %5 ms)")
                      .arg(qAbs(small.x - first.x) + 1)
                      .arg(qAbs(small.y - first.y) + 1)
                      .arg(qAbs(small.z - first.z) + 1)
                      .arg(view->selectedCells().size())
                      .arg(timer.elapsed()));
        }
        else
        {
            qInfo() << "[skip] 附近没有第二个非空气方块，跳过小范围框选";
        }

        // 4) 再点一次：应从新框选的第一个角点开始，旧选中被清掉
        click(dlg, far);
        QApplication::processEvents();
        check(view->selectedCells().isEmpty(), "重新点第一个角点时清掉上一次的选中");
        check(view->highlightCells().size() == 1, "重新开始框选时角点标记重新出现");

        // 5) 取消：角点标记与选中一起清空
        QMetaObject::invokeMethod(dlg, "onClearSelection", Qt::DirectConnection);
        QApplication::processEvents();
        check(view->highlightCells().isEmpty() && view->selectedCells().isEmpty(),
              "取消后角点标记与选中全部清空");

        // 6) 切回单方块：应恢复「左键单击选中一个」
        scope->setCurrentIndex(0);
        QApplication::processEvents();
        click(dlg, first);
        QApplication::processEvents();
        check(view->selectedCells().size() == 1, "切回单方块后只选中一个方块",
              QStringLiteral("(实际 %1)").arg(view->selectedCells().size()));

        // 7) 点空白处（视图给 INT_MIN）：选中被清空
        click(dlg, Pt{ INT_MIN, INT_MIN, INT_MIN });
        QApplication::processEvents();
        check(view->selectedCells().isEmpty(), "点到空白处清空选中");

        // 8) 框选模式下点空白处：只收起待定角点
        scope->setCurrentIndex(2);
        QApplication::processEvents();
        click(dlg, first);
        QApplication::processEvents();
        check(view->highlightCells().size() == 1, "再次进入框选并点下第一个角点");
        click(dlg, Pt{ INT_MIN, INT_MIN, INT_MIN });
        QApplication::processEvents();
        check(view->highlightCells().isEmpty() && view->selectedCells().isEmpty(),
              "框选待定时点空白处只收起角点标记");

        qInfo().noquote() << (g_failures == 0 ? QStringLiteral("PROBE-DONE")
                                              : QStringLiteral("PROBE-FAIL: %1 项不符").arg(g_failures));
        app.exit(g_failures == 0 ? 0 : 1);
    });

    return app.exec();
}
