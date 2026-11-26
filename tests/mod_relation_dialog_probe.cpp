/**
 * @file   mod_relation_dialog_probe.cpp
 * @brief  「模组所属关系」弹窗探针：左侧列表、依赖树、关系图双模式与联动
 *
 * 直接构造 ModRelationDialog（不 exec，避免阻塞），用一组带前置依赖的假模组
 * 断言两种模式的内容与节点点击联动，并截图：
 *   probe_out/mod_relation_tree_fabric.png（依赖树）
 *   probe_out/mod_relation_graph.png     （关系图）
 *   probe_out/mod_relation_tree_xaero.png（缺失前置的依赖树）
 * 退出码 0 = 全部断言通过。
 */
#include "components/ModRelationDialog.h"

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QMetaObject>
#include <QDir>
#include <QTest>

#include "utils/ThemeManager.h"

#include <cstdio>

namespace
{

int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-52s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

void savePng(QWidget *widget, const QString &path)
{
    if (!widget)
        return;
    const QPixmap shot = widget->grab();
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(), shot.height(),
            ok ? "ok" : "FAILED");
}

/// 底部统计行由「概览 + 名称 + 若干数量胶囊」拼成，断言时合并成一段文字
QString summaryText(QWidget *root)
{
    QStringList parts;
    const QList<QLabel *> labels = root->findChildren<QLabel *>();
    for (QLabel *label : labels)
    {
        const QString name = label->objectName();
        if (name == QStringLiteral("relationSummaryLabel")
            || name == QStringLiteral("relationCurrentLabel")
            || name.startsWith(QStringLiteral("relationStatChip")))
        {
            if (!label->text().isEmpty())
                parts << label->text();
        }
    }
    return parts.join(QStringLiteral("  "));
}

bool summaryChipVisible(QWidget *root, const QString &objectName)
{
    auto *chip = root->findChild<QLabel *>(objectName);
    return chip && chip->isVisible() && !chip->text().isEmpty();
}

ModInfo makeMod(const QString &name, const QString &id, const QString &version)
{
    ModInfo info;
    info.name = name;
    info.id = id;
    info.fileName = id + QStringLiteral(".jar");
    info.latestVersion = version;
    info.enabled = true;
    return info;
}

void addDep(ModInfo &mod, const QString &depId, bool required)
{
    ModDependency dep;
    dep.name = depId;
    dep.version = QStringLiteral("*");
    dep.isRequired = required;
    mod.dependencies.append(dep);
}

} // namespace

int main(int argc, char *argv[])
{
    const QString shotDir = QStringLiteral("probe_out");
    QDir().mkpath(shotDir);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    // 真实启动时主题由 StartupProgressWidget::applyTheme 套用；探针里补上，否则弹窗走系统默认样式
    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    QApplication::processEvents();

    // 假模组：fabric-api 被 4 个模组依赖；xaerominimap 依赖一个未安装的 xaeroworldmap
    QList<ModInfo> mods;
    mods.append(makeMod(QStringLiteral("Fabric API"), QStringLiteral("fabric-api"), QStringLiteral("0.100.0")));
    mods.append(makeMod(QStringLiteral("Cloth Config API"), QStringLiteral("cloth-config"), QStringLiteral("15.0.0")));
    mods.append(makeMod(QStringLiteral("JEI"), QStringLiteral("jei"), QStringLiteral("19.0.0")));
    mods.append(makeMod(QStringLiteral("Sodium"), QStringLiteral("sodium"), QStringLiteral("0.6.0")));
    mods.append(makeMod(QStringLiteral("Reese's Sodium Options"), QStringLiteral("reeses-sodium-options"), QStringLiteral("1.7.0")));
    mods.append(makeMod(QStringLiteral("Xaero's Minimap"), QStringLiteral("xaerominimap"), QStringLiteral("24.0.0")));
    addDep(mods[1], QStringLiteral("fabric-api"), true);
    addDep(mods[2], QStringLiteral("fabric-api"), true);
    addDep(mods[2], QStringLiteral("cloth-config"), true);
    addDep(mods[3], QStringLiteral("fabric-api"), true);
    addDep(mods[3], QStringLiteral("reeses-sodium-options"), false);
    addDep(mods[4], QStringLiteral("sodium"), true);
    addDep(mods[5], QStringLiteral("xaeroworldmap"), true);
    addDep(mods[5], QStringLiteral("fabric-api"), false);

    ModRelationDialog dialog(mods);
    dialog.show();
    for (int k = 0; k < 10; ++k)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }

    auto *list = dialog.findChild<QListWidget *>();
    auto *tree = dialog.findChild<QTreeWidget *>();
    auto *stack = dialog.findChild<QStackedWidget *>();
    auto *summary = dialog.findChild<QLabel *>(QStringLiteral("relationSummaryLabel"));
    expect("左侧模组列表存在且载入 6 个模组", list && list->count() == 6);
    expect("右侧树/图堆叠容器存在", stack && stack->count() == 2);
    expect("底部汇总标签存在", summary != nullptr);

    // 默认选中第一个模组（Fabric API）：无前置、被 4 个模组依赖
    expect("依赖树有两棵根树", tree && tree->topLevelItemCount() == 2);
    if (tree && tree->topLevelItemCount() == 2)
    {
        expect("Fabric API 无前置", tree->topLevelItem(0)->text(0).contains(QStringLiteral("（0）")));
        expect("Fabric API 被 4 个模组依赖", tree->topLevelItem(1)->text(0).contains(QStringLiteral("（4）")));
    }
    expect("汇总行包含前置/被依赖统计",
           summaryText(&dialog).contains(QStringLiteral("被 4 个模组依赖")));
    expect("列表行显示名称与版本两行信息",
           list && list->item(0)->data(Qt::UserRole + 201).toString() == QStringLiteral("Fabric API")
               && list->item(0)->data(Qt::UserRole + 202).toString() == QStringLiteral("0.100.0"));
    savePng(&dialog, shotDir + QStringLiteral("/mod_relation_tree_fabric.png"));

    // 切到关系图模式
    QPushButton *graphBtn = nullptr;
    for (auto *b : dialog.findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("关系图"))
            graphBtn = b;
    expect("模式切换里有「关系图」按钮", graphBtn != nullptr);
    if (graphBtn)
        graphBtn->click();
    for (int k = 0; k < 10; ++k)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }
    expect("切换后右侧显示关系图页", stack && stack->currentIndex() == 1);
    savePng(&dialog, shotDir + QStringLiteral("/mod_relation_graph.png"));

    // 关系图节点点击联动：通过信号直达（等价于点击一个已安装节点）切到 Sodium
    auto *graph = dialog.findChild<ModRelationGraphWidget *>();
    expect("关系图自绘控件存在", graph != nullptr);
    if (graph)
        QMetaObject::invokeMethod(graph, "nodeClicked", Q_ARG(int, 3));
    for (int k = 0; k < 10; ++k)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }
    expect("图内点击后汇总切到 Sodium", summaryText(&dialog).contains(QStringLiteral("Sodium")));
    expect("Sodium 有 2 个前置", tree && tree->topLevelItem(0)->text(0).contains(QStringLiteral("（2）")));
    if (tree && tree->topLevelItem(0)->childCount() == 2)
    {
        // reeses-sodium-options 是建议依赖且已安装（状态带 ● 圆点前缀）
        expect("建议依赖标为「已安装 · 建议」",
               tree->topLevelItem(0)->child(1)->text(2).contains(QStringLiteral("已安装 · 建议")));
    }

    // 真实鼠标点击关系图右侧第一个「被依赖」节点：Sodium 的被依赖是 Reese's Sodium Options
    auto *graph2 = dialog.findChild<ModRelationGraphWidget *>();
    expect("关系图提供节点矩形用于定位", graph2 && graph2->nodeRect(2, 0).isValid());
    if (graph2)
    {
        const QRectF target = graph2->nodeRect(2, 0);
        QTest::mouseClick(graph2, Qt::LeftButton, Qt::NoModifier, target.center().toPoint());
        for (int k = 0; k < 10; ++k)
        {
            QThread::msleep(20);
            QApplication::processEvents();
        }
        expect("真实点击被依赖节点切到 Reese's Sodium Options",
               summaryText(&dialog).contains(QStringLiteral("Reese's Sodium Options")));
        expect("点击后汇总显示 1 个前置",
               summaryText(&dialog).contains(QStringLiteral("前置 1 个")));
    }

    // 左侧列表点选 Xaero's Minimap：包含未安装前置
    if (list)
    {
        for (int r = 0; r < list->count(); ++r)
        {
            if (list->item(r)->text().contains(QStringLiteral("Xaero's Minimap")))
            {
                list->setCurrentRow(r);
                break;
            }
        }
    }
    for (int k = 0; k < 10; ++k)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }
    if (tree && tree->topLevelItem(0)->childCount() == 2)
    {
        expect("缺失前置标为「未安装」",
               tree->topLevelItem(0)->child(0)->text(2).contains(QStringLiteral("未安装")));
    }
    else
    {
        expect("Xaero 有 2 个前置条目", false);
    }
    expect("缺失数量以红色胶囊显示",
           summaryChipVisible(&dialog, QStringLiteral("relationStatChipDanger"))
               && dialog.findChild<QLabel *>(QStringLiteral("relationStatChipDanger"))
                      ->text()
                      .contains(QStringLiteral("缺失 1 个")));

    // 切回依赖树模式截图（带缺失前置的场景）
    QPushButton *treeBtn = nullptr;
    for (auto *b : dialog.findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("依赖树"))
            treeBtn = b;
    if (treeBtn)
        treeBtn->click();
    for (int k = 0; k < 10; ++k)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }
    expect("切回依赖树模式", stack && stack->currentIndex() == 0);
    savePng(&dialog, shotDir + QStringLiteral("/mod_relation_tree_xaero.png"));

    // 分组标题左侧的自绘箭头可以收起 / 展开（关系树已改为自绘分支装饰）
    if (tree && tree->topLevelItemCount() > 0)
    {
        QTreeWidgetItem *section = tree->topLevelItem(0);
        const QRect r = tree->visualItemRect(section);
        const QPoint chevron(r.left() - tree->indentation() / 2 - 1, r.center().y());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, chevron);
        for (int k = 0; k < 6; ++k)
        {
            QThread::msleep(20);
            QApplication::processEvents();
        }
        expect("点击分组箭头可收起前置依赖", !section->isExpanded());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, chevron);
        for (int k = 0; k < 6; ++k)
        {
            QThread::msleep(20);
            QApplication::processEvents();
        }
        expect("再次点击可展开前置依赖", section->isExpanded());
    }

    // 选中一条依赖：真实点击一行，验证整行选中底色（不会像以前那样被拆成一段段小胶囊）
    if (tree && tree->topLevelItem(0)->childCount() > 1)
    {
        const QRect rowRect = tree->visualItemRect(tree->topLevelItem(0)->child(1));
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          rowRect.center());
        for (int k = 0; k < 6; ++k)
        {
            QThread::msleep(20);
            QApplication::processEvents();
        }
        expect("点击依赖条目后整行选中",
               tree->currentItem() == tree->topLevelItem(0)->child(1)
                   && tree->currentItem()->isSelected());
        savePng(&dialog, shotDir + QStringLiteral("/mod_relation_tree_selected.png"));
    }

    // 关系图模式再截一张（带缺失前置与空列占位）
    if (graphBtn)
        graphBtn->click();
    for (int k = 0; k < 10; ++k)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }
    expect("关系图展示缺失前置节点",
           graph && graph->nodeRect(0, 0).isValid());
    savePng(&dialog, shotDir + QStringLiteral("/mod_relation_graph_xaero.png"));

    // 深色主题：验证关系图与树的配色适配（令牌色应随主题翻转）
    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    for (int k = 0; k < 10; ++k)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }
    savePng(&dialog, shotDir + QStringLiteral("/mod_relation_graph_xaero_dark.png"));
    if (treeBtn)
        treeBtn->click();
    for (int k = 0; k < 10; ++k)
    {
        QThread::msleep(20);
        QApplication::processEvents();
    }
    savePng(&dialog, shotDir + QStringLiteral("/mod_relation_tree_xaero_dark.png"));

    fprintf(stderr, "[probe] %s (failures=%d)\n", g_failures == 0 ? "PROBE-PASS" : "PROBE-FAIL",
            g_failures);
    return g_failures == 0 ? 0 : 1;
}
