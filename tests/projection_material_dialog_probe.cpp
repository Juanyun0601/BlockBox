/**
 * @file   projection_material_dialog_probe.cpp
 * @brief  投影帖详情页「材料列表」弹窗探针
 *
 * 先用 SchematicDocument 现造一份 3 种方块的 .litematic，再直接构造
 * ProjectionMaterialDialog（不 exec，避免阻塞），断言：
 *   - 后台统计出 3 种方块、数量与排序正确（石头 100 / 橡木原木 5 / 玻璃 4）
 *   - 清单里带中文名（「石头」而不是光秃秃的 minecraft:stone）
 *   - 按英文 ID 筛选能只剩一行，清空筛选后恢复
 *   - 「复制清单」把整份清单写进剪贴板
 * 并截图：
 *   probe_out/projection_material_light.png
 *   probe_out/projection_material_dark.png
 * 退出码 0 = 全部断言通过。
 */

#include "components/ProjectionMaterialDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>
#include <QTreeWidget>

#include "utils/Schematic/SchematicDocument.h"
#include "utils/ThemeManager.h"

#include <cstdio>

namespace {

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

void pump(int rounds = 8, int ms = 20)
{
    for (int i = 0; i < rounds; ++i)
    {
        QThread::msleep(static_cast<unsigned long>(ms));
        QApplication::processEvents();
    }
}

/// 等后台统计落表：树里出现行，或提示已经是失败文案
void waitForResult(QTreeWidget *tree, QLabel *hint, int maxMs = 8000)
{
    for (int waited = 0; waited < maxMs; waited += 20)
    {
        QThread::msleep(20);
        QApplication::processEvents();
        if (tree && tree->topLevelItemCount() > 0)
            return;
        if (hint && (hint->text().contains(QStringLiteral("无法"))
                     || hint->text().contains(QStringLiteral("找不到"))))
            return;
    }
}

int visibleRows(QTreeWidget *tree)
{
    if (!tree)
        return 0;
    int visible = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
    {
        if (tree->topLevelItem(i) && !tree->topLevelItem(i)->isHidden())
            ++visible;
    }
    return visible;
}

/// 现造一份投影：10×6×10 的地板 + 一根柱子 + 一小块玻璃，正好 3 种方块
bool buildSampleProjection(const QString &path)
{
    SchematicDocument doc;
    if (!doc.createEmpty(10, 6, 10))
        return false;

    SchematicBlockState stone;
    stone.name = QStringLiteral("minecraft:stone");
    SchematicBlockState oak;
    oak.name = QStringLiteral("minecraft:oak_log");
    SchematicBlockState glass;
    glass.name = QStringLiteral("minecraft:glass");

    if (doc.fillBox(0, 0, 0, 9, 0, 9, stone) != 100)
        return false;
    if (doc.fillBox(0, 1, 0, 0, 5, 0, oak) != 5)
        return false;
    if (doc.fillBox(2, 1, 2, 3, 1, 3, glass) != 4)
        return false;

    QString error;
    if (!doc.saveCopy(path, &error))
    {
        fprintf(stderr, "[probe] saveCopy failed: %s\n", qPrintable(error));
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    const QString shotDir = QStringLiteral("probe_out");
    QDir().mkpath(shotDir);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    // 真实启动时主题由启动流程套用；探针里补上，否则弹窗走系统默认样式
    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    QApplication::processEvents();

    const QString samplePath = shotDir + QStringLiteral("/projection_material_sample.litematic");
    QFile::remove(samplePath);
    expect("现造一份 3 种方块的投影文件", buildSampleProjection(samplePath));
    expect("投影文件已落盘", QFileInfo(samplePath).size() > 0);

    ProjectionMaterialDialog dialog(nullptr, samplePath, QString(),
                                    QStringLiteral("测试建筑"));
    dialog.show();
    pump(4);

    auto *tree = dialog.findChild<QTreeWidget *>(QStringLiteral("projectionMaterialTree"));
    auto *hint = dialog.findChild<QLabel *>(QStringLiteral("projectionMaterialHint"));
    auto *summary = dialog.findChild<QLabel *>(QStringLiteral("projectionMaterialSummary"));
    auto *title = dialog.findChild<QLabel *>(QStringLiteral("projectionMaterialTitle"));
    auto *filter = dialog.findChild<QLineEdit *>(QStringLiteral("projectionMaterialFilter"));
    auto *copy = dialog.findChild<QPushButton *>(QStringLiteral("projectionMaterialCopyBtn"));

    expect("清单树存在", tree != nullptr);
    expect("汇总标签存在", summary != nullptr);
    expect("筛选框存在", filter != nullptr);
    expect("复制按钮存在", copy != nullptr);

    waitForResult(tree, hint);
    pump(4);

    expect("标题带投影名", title && title->text().contains(QStringLiteral("测试建筑")));
    expect("统计出 3 种方块", tree && tree->topLevelItemCount() == 3);
    expect("汇总写着 3 种方块", summary && summary->text().contains(QStringLiteral("3 种方块")));
    expect("汇总写着合计 109 个", summary && summary->text().contains(QStringLiteral("109")));

    if (tree && tree->topLevelItemCount() == 3)
    {
        QTreeWidgetItem *first = tree->topLevelItem(0);
        expect("第一行是石头（数量最多）", first->text(0).contains(QStringLiteral("石头")));
        expect("石头数量 100", first->text(1) == QStringLiteral("100"));
        expect("第一行带英文 ID 提示", first->toolTip(0) == QStringLiteral("minecraft:stone"));
        expect("第二行是橡木原木", tree->topLevelItem(1)->text(0).contains(QStringLiteral("橡木原木")));
        expect("第三行是玻璃", tree->topLevelItem(2)->text(0).contains(QStringLiteral("玻璃")));
    }

    // 筛选：按英文 ID 只留一根柱子
    if (filter && tree)
    {
        filter->setText(QStringLiteral("oak"));
        pump(2);
        expect("筛选 oak 只剩 1 行", visibleRows(tree) == 1);
        expect("筛选提示说明匹配数", hint && hint->text().contains(QStringLiteral("匹配 1 种")));
        filter->clear();
        pump(2);
        expect("清空筛选后恢复 3 行", visibleRows(tree) == 3);
        filter->setText(QStringLiteral("不存在的方块"));
        pump(2);
        expect("筛不到时提示没有匹配", hint && hint->text().contains(QStringLiteral("没有匹配")));
        filter->clear();
        pump(2);
    }

    if (copy)
    {
        copy->click();
        pump(2);
        const QString clip = QApplication::clipboard()->text();
        expect("剪贴板第一行是材料清单标题",
               clip.startsWith(QStringLiteral("材料清单：测试建筑")));
        expect("剪贴板里有石头 100 那一行",
               clip.contains(QStringLiteral("石头 (stone) x 100")));
        expect("剪贴板里 3 种方块都在",
               clip.contains(QStringLiteral("橡木原木")) && clip.contains(QStringLiteral("玻璃")));
    }

    savePng(&dialog, shotDir + QStringLiteral("/projection_material_light.png"));

    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    pump(6);
    savePng(&dialog, shotDir + QStringLiteral("/projection_material_dark.png"));

    fprintf(stderr, "[probe] failures=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
