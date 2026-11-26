/**
 * @file   projection_build_guide_probe.cpp
 * @brief  投影「学习搭建」窗口探针
 *
 * 现造一份 3 层、6 种步骤的投影，直接构造 ProjectionBuildGuideDialog：
 *   - 打开后先显示最下面一层，第一步是这一层数量最多的材料；
 *   - 「下一步」按材料走完本层后自动进入上一层；
 *   - 全部步骤走完后显示完成状态；
 *   - 平面图四边刻度、琥珀色高亮能在窗口里正常画出来（截图）。
 * 退出码 0 = 全部断言通过。
 */

#include "components/ProjectionBuildGuideDialog.h"
#include "components/LayerBuildPlanWidget.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <functional>

#include "utils/Schematic/SchematicDocument.h"
#include "utils/Schematic/InstanceTextureLoader.h"
#include "utils/ThemeManager.h"

#include <cstdio>

namespace {

int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-58s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
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

bool waitUntil(const std::function<bool()> &done, int maxMs = 12000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < maxMs)
    {
        QThread::msleep(20);
        QApplication::processEvents();
        if (done())
            return true;
    }
    return false;
}

bool buildSample(const QString &path)
{
    SchematicDocument doc;
    if (!doc.createEmpty(12, 3, 10))
        return false;

    SchematicBlockState stone;
    stone.name = QStringLiteral("minecraft:stone");
    SchematicBlockState oak;
    oak.name = QStringLiteral("minecraft:oak_planks");
    SchematicBlockState glass;
    glass.name = QStringLiteral("minecraft:glass");
    SchematicBlockState dirt;
    dirt.name = QStringLiteral("minecraft:dirt");

    // 第 0 层：石头 60、木板 20、玻璃 10（石头最多，第一步）
    if (doc.fillBox(0, 0, 0, 5, 0, 9, stone) != 60)
        return false;
    if (doc.fillBox(6, 0, 0, 7, 0, 9, oak) != 20)
        return false;
    if (doc.fillBox(8, 0, 0, 9, 0, 4, glass) != 10)
        return false;

    // 第 1 层：泥土 24、石头 8
    if (doc.fillBox(0, 1, 0, 2, 1, 7, dirt) != 24)
        return false;
    if (doc.fillBox(3, 1, 0, 4, 1, 3, stone) != 8)
        return false;

    // 第 2 层：木板 12
    if (doc.fillBox(0, 2, 0, 2, 2, 3, oak) != 12)
        return false;

    QString error;
    if (!doc.saveCopy(path, &error))
    {
        fprintf(stderr, "[probe] saveCopy failed: %s\n", qPrintable(error));
        return false;
    }
    return true;
}

QTreeWidgetItem *firstChildOfLayer(QTreeWidget *tree, int layerRow)
{
    if (!tree || layerRow < 0 || layerRow >= tree->topLevelItemCount())
        return nullptr;
    QTreeWidgetItem *layer = tree->topLevelItem(layerRow);
    return layer && layer->childCount() > 0 ? layer->child(0) : nullptr;
}

} // namespace

int main(int argc, char *argv[])
{
    const QString shotDir = QStringLiteral("probe_out");
    QDir().mkpath(shotDir);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    QApplication::processEvents();

    const QString samplePath = shotDir + QStringLiteral("/build_guide_sample.litematic");
    // 可选：传一个版本目录（versions/<版本>）来验证真实材质
    const QString instancePath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    QFile::remove(samplePath);
    expect("现造一份 3 层投影", buildSample(samplePath));
    expect("投影文件已落盘", QFileInfo(samplePath).size() > 0);

    ProjectionBuildGuideDialog dialog(nullptr, samplePath, instancePath,
                                      QStringLiteral("探针建筑"));
    dialog.resize(1240, 820);
    dialog.show();

    auto *tree = dialog.findChild<QTreeWidget *>(QStringLiteral("buildGuideTree"));
    auto *plan = dialog.findChild<LayerBuildPlanWidget *>(QStringLiteral("buildPlanView"));
    auto *material = dialog.findChild<QLabel *>(QStringLiteral("buildGuideMaterial"));
    auto *stepLabel = dialog.findChild<QLabel *>(QStringLiteral("buildGuideStepLabel"));
    auto *progress = dialog.findChild<QProgressBar *>(QStringLiteral("buildGuideProgress"));
    auto *nextBtn = dialog.findChild<QPushButton *>(QStringLiteral("buildGuidePrimaryBtn"));

    expect("搭建顺序树存在", tree != nullptr);
    expect("平面图存在", plan != nullptr);
    expect("材料标签存在", material != nullptr);
    expect("步骤标签存在", stepLabel != nullptr);
    expect("进度条存在", progress != nullptr);
    expect("下一步按钮存在", nextBtn != nullptr);

    const bool loaded = waitUntil([&]() {
        return tree && tree->topLevelItemCount() > 0;
    });
    pump(8);
    expect("后台解析完成并出现层列表", loaded);

    if (loaded && tree)
    {
        expect("识别出 3 层", tree->topLevelItemCount() == 3);
        QTreeWidgetItem *first = firstChildOfLayer(tree, 0);
        expect("最下面一层第一项存在", first != nullptr);
        expect("第一步是这一层数量最多的材料（石头）",
               first && first->text(0).contains(QStringLiteral("石头")));
        expect("第一步数量 60", first && first->text(1) == QStringLiteral("60"));
        expect("打开后停在第 1 层第 1 步",
               stepLabel && stepLabel->text().contains(QStringLiteral("第 1 层"))
               && stepLabel->text().contains(QStringLiteral("第 1 / 3 步")));
        expect("材料标签显示石头 × 60",
               material && material->text().contains(QStringLiteral("石头"))
               && material->text().contains(QStringLiteral("60")));
        expect("进度 0 / 6", progress && progress->maximum() == 6 && progress->value() == 0);

        // 平面图确实画出了东西（不是全空白）
        const QImage shot = plan->grab().toImage();
        bool colored = false;
        for (int y = 0; y < shot.height() && !colored; y += 4)
        {
            for (int x = 0; x < shot.width(); x += 4)
            {
                const QColor c = shot.pixelColor(x, y);
                if (c.red() > 190 && c.green() > 120 && c.blue() < 140)
                {
                    colored = true;
                    break;
                }
            }
        }
        expect("平面图画出琥珀色高亮/刻度", colored);

        nextBtn->click();
        pump(4);
        expect("下一步进入本层第 2 种材料（木板）",
               material && material->text().contains(QStringLiteral("木板")));
        expect("进度 1 / 6", progress && progress->value() == 1);
        expect("仍在第 1 层", stepLabel && stepLabel->text().contains(QStringLiteral("第 1 层")));

        // 把剩下 5 步走完（2、3 步在第 1 层，4、5 步在第 2 层，6 步在第 3 层）
        for (int i = 0; i < 5 && !material->text().contains(QStringLiteral("全部搭建完成")); ++i)
        {
            nextBtn->click();
            pump(3);
        }
        expect("走完最后一步后显示全部完成",
               material && material->text().contains(QStringLiteral("全部搭建完成")));
        expect("完成时进度拉满", progress && progress->value() == progress->maximum());
        expect("完成后下一步按钮禁用", nextBtn && !nextBtn->isEnabled());

        const QPixmap shot2 = dialog.grab();
        const bool saved = shot2.save(shotDir + QStringLiteral("/build_guide_complete.png"));
        fprintf(stderr, "[probe] shot %s %dx%d %s\n",
                qPrintable(shotDir + QStringLiteral("/build_guide_complete.png")),
                shot2.width(), shot2.height(), saved ? "ok" : "FAILED");
    }

    // 再抓一张刚打开第一步的样子（重新开一个窗口）
    {
        ProjectionBuildGuideDialog lightDialog(nullptr, samplePath, instancePath,
                                               QStringLiteral("探针建筑"));
        lightDialog.resize(1240, 820);
        lightDialog.show();
        waitUntil([&]() {
            auto *t = lightDialog.findChild<QTreeWidget *>(QStringLiteral("buildGuideTree"));
            return t && t->topLevelItemCount() > 0;
        });
        pump(10);
        const QPixmap shot = lightDialog.grab();
        const QString path = shotDir + QStringLiteral("/build_guide_light.png");
        const bool saved = shot.save(path);
        fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path),
                shot.width(), shot.height(), saved ? "ok" : "FAILED");

        if (!instancePath.isEmpty())
        {
            auto *p = lightDialog.findChild<LayerBuildPlanWidget *>(QStringLiteral("buildPlanView"));
            expect("平面图挂上了实例材质",
                   p && p->textureLoader() && p->textureLoader()->isValid());
            const QPixmap textured = lightDialog.grab();
            const QString texturedPath =
                shotDir + QStringLiteral("/build_guide_textured.png");
            const bool texturedSaved = textured.save(texturedPath);
            fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(texturedPath),
                    textured.width(), textured.height(), texturedSaved ? "ok" : "FAILED");
        }
    }

    // 深色主题再抓一张
    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    QApplication::processEvents();
    {
        ProjectionBuildGuideDialog darkDialog(nullptr, samplePath, instancePath,
                                              QStringLiteral("探针建筑"));
        darkDialog.resize(1240, 820);
        darkDialog.show();
        waitUntil([&]() {
            auto *t = darkDialog.findChild<QTreeWidget *>(QStringLiteral("buildGuideTree"));
            return t && t->topLevelItemCount() > 0;
        });
        pump(10);
        const QPixmap shot = darkDialog.grab();
        const QString path = shotDir + QStringLiteral("/build_guide_dark.png");
        const bool saved = shot.save(path);
        fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path),
                shot.width(), shot.height(), saved ? "ok" : "FAILED");
    }

    fprintf(stderr, "[probe] failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
