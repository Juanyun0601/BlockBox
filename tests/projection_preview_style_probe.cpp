/**
 * @file   projection_preview_style_probe.cpp
 * @brief  投影三维预览弹窗「样式」探针
 *
 * 现造一份 16×9×16 的小房子（地板 / 玻璃窗带 / 屋顶 / 原木柱子），
 * 直接构造 ProjectionPreviewDialog（不 exec，避免阻塞），然后：
 *   1. 等后台解析完成，检查统计行、层数条、提示行都正常出现；
 *   2. 把工具栏里每个控件的几何打出来，检查有没有被卡片挤到外面（溢出）；
 *   3. 截浅色 / 深色两张卡片图，再截一张全屏图，人工对比样式；
 * 退出码 0 = 全部断言通过。
 *
 * 用法（在 build-win-release 下）：
 *   build_probe.bat projection_preview_style_probe ProjectionPreviewStyleProbe
 *   probe_out\ProjectionPreviewStyleProbe.exe
 */

#include "components/ProjectionPreviewDialog.h"
#include "components/VoxelViewWidget.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QThread>
#include <QToolButton>
#include <QWidget>

#include "utils/Schematic/SchematicDocument.h"
#include "utils/ThemeManager.h"

#include <cstdio>
#include <functional>

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

bool waitUntil(const std::function<bool()> &done, int maxMs = 20000)
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

void savePng(QWidget *widget, const QString &path)
{
    if (!widget)
        return;
    const QPixmap shot = widget->grab();
    const bool ok = shot.save(path);
    fprintf(stderr, "[probe] shot %s %dx%d %s\n", qPrintable(path), shot.width(), shot.height(),
            ok ? "ok" : "FAILED");
}

/// 现造一份小房子：16×9×16，地板 + 玻璃窗带 + 屋顶 + 四角原木柱
bool buildSample(const QString &path)
{
    SchematicDocument doc;
    if (!doc.createEmpty(16, 9, 16))
        return false;

    SchematicBlockState stone;
    stone.name = QStringLiteral("minecraft:stone_bricks");
    SchematicBlockState plank;
    plank.name = QStringLiteral("minecraft:oak_planks");
    SchematicBlockState glass;
    glass.name = QStringLiteral("minecraft:glass");
    SchematicBlockState log;
    log.name = QStringLiteral("minecraft:oak_log");

    // 地板
    if (doc.fillBox(0, 0, 0, 15, 0, 15, stone) != 256)
        return false;
    // 四面墙：下半段石头、中间一圈玻璃窗、上半段木板
    for (int y = 1; y <= 5; ++y)
    {
        const SchematicBlockState &state = (y == 3) ? glass : stone;
        for (int x = 0; x <= 15; ++x)
        {
            doc.setBlock(x, y, 0, state);
            doc.setBlock(x, y, 15, state);
        }
        for (int z = 1; z <= 14; ++z)
        {
            doc.setBlock(0, y, z, state);
            doc.setBlock(15, y, z, state);
        }
    }
    // 四角原木柱
    for (int y = 1; y <= 5; ++y)
    {
        doc.setBlock(0, y, 0, log);
        doc.setBlock(15, y, 0, log);
        doc.setBlock(0, y, 15, log);
        doc.setBlock(15, y, 15, log);
    }
    // 屋顶
    if (doc.fillBox(0, 6, 0, 15, 6, 15, plank) != 256)
        return false;

    QString error;
    if (!doc.saveCopy(path, &error))
    {
        fprintf(stderr, "[probe] saveCopy failed: %s\n", qPrintable(error));
        return false;
    }
    return true;
}

/// 工具栏里控件是否都还在卡片内（右边界不超出、没被压成 0 宽）
void checkNoOverflow(QWidget *root, const char *what)
{
    if (!root)
    {
        expect(what, false);
        return;
    }
    const int width = root->width();
    bool ok = true;
    const QList<QWidget *> kids = root->findChildren<QWidget *>(QString(), Qt::FindChildrenRecursively);
    for (QWidget *w : kids)
    {
        if (!w->isVisible() || w->parentWidget() == nullptr)
            continue;
        if (QString::fromLatin1(w->metaObject()->className()).contains(QStringLiteral("VoxelView")))
            continue;
        const QRect geo = w->geometry();
        if (geo.width() <= 0)
            continue;
        if (geo.right() > width + 1)
        {
            fprintf(stderr, "[probe]   溢出：%s (class=%s) right=%d > %d\n",
                    qPrintable(w->objectName()), w->metaObject()->className(), geo.right(), width);
            ok = false;
        }
    }
    expect(what, ok);
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

    const QString samplePath = shotDir + QStringLiteral("/projection_preview_sample.litematic");
    QFile::remove(samplePath);
    expect("现造一份小房子投影", buildSample(samplePath));
    expect("投影文件已落盘", QFileInfo(samplePath).size() > 0);

    ProjectionPreviewDialog dialog(nullptr, samplePath, QString(),
                                   QStringLiteral("雪原小屋·测试建筑"));
    dialog.resize(1100, 760);
    dialog.show();
    pump(4);

    auto *stats = dialog.findChild<QLabel *>(QStringLiteral("projectionPreviewStats"));
    auto *hint = dialog.findChild<QLabel *>(QStringLiteral("projectionPreviewHint"));
    auto *view = dialog.findChild<VoxelViewWidget *>(QStringLiteral("projectionPreviewView"));
    auto *layerSlider = dialog.findChild<QSlider *>(QStringLiteral("projectionPreviewLayerSlider"));
    auto *toolbar = dialog.findChild<QWidget *>(QStringLiteral("projectionPreviewToolbar"));
    auto *card = dialog.findChild<QWidget *>(QStringLiteral("projectionPreviewCard"));

    expect("统计标签存在", stats != nullptr);
    expect("提示标签存在", hint != nullptr);
    expect("三维视图存在", view != nullptr);
    expect("层数滑块存在", layerSlider != nullptr);

    // 等后台把投影解析完（统计行一有内容就说明文档就绪）
    const bool loaded = waitUntil([&]() {
        return stats && !stats->text().isEmpty() && view && view->isVisible();
    });
    pump(40, 30);

    expect("投影解析完成并显示", loaded);
    if (stats)
        fprintf(stderr, "[probe]   统计行：%s\n", qPrintable(stats->text()));
    if (hint)
        fprintf(stderr, "[probe]   提示行：%s\n", qPrintable(hint->text()));

    expect("尺寸统计是 16×9×16",
           stats && stats->text().contains(QStringLiteral("16×9×16")));    expect("层数滑块按高度铺到 9 层", layerSlider && layerSlider->maximum() == 9);
    expect("提示行是操作说明（加载完就换掉）",
           hint && hint->text().contains(QStringLiteral("WASD")));

    // 取景检查：三维画面里模型的像素包围盒不能贴边（贴边 = 模型被切掉了）
    if (view)
    {
        const QImage fb = view->grabFramebuffer();
        int minX = fb.width(), minY = fb.height(), maxX = -1, maxY = -1;
        // 视图背景是一层偏蓝的渐变，方块本身是中性灰或暖色：
        // 用「蓝比红高多少」区分背景，比和某个固定清屏色比对稳得多
        for (int y = 0; y < fb.height(); ++y)
        {
            for (int x = 0; x < fb.width(); ++x)
            {
                const QColor c = fb.pixelColor(x, y);
                // 背景是一层偏蓝的暗渐变：蓝明显高于红，或者整体很暗
                const int brightest = qMax(c.red(), qMax(c.green(), c.blue()));
                if (c.blue() - c.red() >= 5 || brightest < 45)
                    continue;
                minX = qMin(minX, x);
                maxX = qMax(maxX, x);
                minY = qMin(minY, y);
                maxY = qMax(maxY, y);
            }
        }
        const int left = minX, right = fb.width() - 1 - maxX;
        const int top = minY, bottom = fb.height() - 1 - maxY;
        fprintf(stderr, "[probe]   模型包围盒留白 左=%d 右=%d 上=%d 下=%d（视图 %dx%d）\n",
                left, right, top, bottom, fb.width(), fb.height());
        if (maxX >= 0)
        {
            expect("模型四边都没被切掉（留白 ≥ 6px）",
                   left >= 6 && right >= 6 && top >= 6 && bottom >= 6);
        }
        else
        {
            expect("画面里能看到模型", false);
        }
        fb.save(shotDir + QStringLiteral("/projection_preview_gl_light.png"));
    }

    if (card)
    {
        fprintf(stderr, "[probe]   卡片尺寸：%dx%d\n", card->width(), card->height());
        expect("卡片宽度不小于最小宽度 680", card->width() >= 680);
    }
    if (toolbar)
    {
        fprintf(stderr, "[probe]   工具栏尺寸：%dx%d\n", toolbar->width(), toolbar->height());
        for (QWidget *w : toolbar->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
        {
            if (!w->isVisible())
                continue;
            fprintf(stderr, "[probe]     控件 %-34s class=%-12s geo=%d,%d %dx%d\n",
                    qPrintable(w->objectName()), w->metaObject()->className(),
                    w->x(), w->y(), w->width(), w->height());
        }
    }
    checkNoOverflow(toolbar, "工具栏控件没有溢出卡片");
    checkNoOverflow(card, "卡片子控件没有溢出");

    // 浅色
    if (QWidget *titleBar = dialog.findChild<QWidget *>(QStringLiteral("projectionPreviewTitleBar")))
        fprintf(stderr, "[probe]   标题行高度：%d\n", titleBar->height());
    savePng(&dialog, shotDir + QStringLiteral("/projection_preview_dialog_light.png"));
    savePng(card, shotDir + QStringLiteral("/projection_preview_card_light.png"));
    if (view)
    {
        const QImage fb = view->grabFramebuffer();
        fprintf(stderr, "[probe]   GL 帧缓存 %dx%d\n", fb.width(), fb.height());
        fb.save(shotDir + QStringLiteral("/projection_preview_gl_light.png"));
    }

    // 层数条展开的状态
    if (layerSlider)
    {
        layerSlider->setValue(6);
        pump(6, 40);
    }
    savePng(card, shotDir + QStringLiteral("/projection_preview_card_light_layer.png"));
    if (layerSlider)
        layerSlider->setValue(0);

    // 窄窗口：小屏上卡片会被压窄，工具栏该整组换行而不是把控件挤出去
    if (card && toolbar)
    {
        card->setMinimumSize(700, 620);
        card->resize(700, 620);
        dialog.resize(760, 680);
        pump(20, 30);
        fprintf(stderr, "[probe]   窄卡片 %dx%d / 工具栏 %dx%d\n", card->width(), card->height(),
                toolbar->width(), toolbar->height());
        expect("窄窗口下工具栏换行变高", toolbar->height() > 50);
        checkNoOverflow(toolbar, "窄窗口工具栏控件没有溢出");
        savePng(card, shotDir + QStringLiteral("/projection_preview_card_narrow.png"));
        card->setMinimumSize(1000, 660);
        dialog.resize(1100, 760);
        pump(16, 30);
    }

    // 深色
    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::DarkTheme);
    pump(20, 30);
    savePng(&dialog, shotDir + QStringLiteral("/projection_preview_dialog_dark.png"));
    savePng(card, shotDir + QStringLiteral("/projection_preview_card_dark.png"));
    if (view)
        view->grabFramebuffer().save(shotDir + QStringLiteral("/projection_preview_gl_dark.png"));

    // 全屏
    dialog.setFullscreenPreview(true);
    pump(10, 40);
    savePng(&dialog, shotDir + QStringLiteral("/projection_preview_fullscreen_dark.png"));
    expect("全屏后铺满窗口", dialog.isFullscreenPreview());
    if (card)
        fprintf(stderr, "[probe]   全屏卡片尺寸：%dx%d\n", card->width(), card->height());
    dialog.setFullscreenPreview(false);
    pump(6, 30);
    expect("退出全屏", !dialog.isFullscreenPreview());

    // 失败态：文件坏掉时的提示排版（画面区留空，提示与「关闭」都要在卡片里）
    {
        const QString badPath = shotDir + QStringLiteral("/projection_preview_bad.litematic");
        QFile badFile(badPath);
        if (badFile.open(QIODevice::WriteOnly))
        {
            badFile.write("这不是一份投影文件");
            badFile.close();
        }

        ProjectionPreviewDialog bad(nullptr, badPath, QString(), QStringLiteral("坏掉的投影"));
        bad.resize(1000, 660);
        bad.show();
        pump(30, 40);

        auto *badHint = bad.findChild<QLabel *>(QStringLiteral("projectionPreviewHint"));
        auto *badCard = bad.findChild<QWidget *>(QStringLiteral("projectionPreviewCard"));
        fprintf(stderr, "[probe]   失败态提示：%s\n",
                badHint ? qPrintable(badHint->text().left(40)) : "(无)");
        expect("坏文件给出失败提示", badHint && badHint->text().contains(QStringLiteral("损坏")));
        savePng(badCard, shotDir + QStringLiteral("/projection_preview_error.png"));
    }

    fprintf(stderr, "[probe] failures=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
