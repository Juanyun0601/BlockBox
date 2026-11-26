/**
 * @file   projection_assistant_tab_probe.cpp
 * @brief  实例助手「资源管理 → 投影」页探针
 *
 * 用真实 InstanceResourcesPage 打开一个临时实例目录，断言：
 *   - Tab 栏出现「投影」；
 *   - 能扫描到 schematics 下的 .litematic 并生成卡片；
 *   - 投影卡片上有「学习搭建」按钮（点开的就是统一的学习搭建窗口）。
 * 退出码 0 = 全部断言通过。
 */

#include "pages/InstanceResourcesPage.h"
#include "components/ProjectionBuildGuideDialog.h"
#include "utils/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QThread>
#include <QWidget>

#include <functional>

#include <cstdio>

namespace {
int g_failures = 0;

void expect(const char *label, bool condition)
{
    fprintf(stderr, "[probe] %-56s %s\n", label, condition ? "OK" : "FAIL");
    if (!condition)
        ++g_failures;
}

void pump(int rounds = 8, int ms = 20)
{
    for (int i = 0; i < rounds; ++i)
    {
        QThread::msleep(static_cast<unsigned long>(ms));
        QApplication::processEvents();
    }
}

bool waitUntil(const std::function<bool()> &done, int maxMs = 8000)
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
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BlockBox"));

    if (ThemeManager::instance())
        ThemeManager::instance()->loadTheme(ThemeManager::LightTheme);
    QApplication::processEvents();

    // 找一份能用的投影：优先用学习搭建探针现造的样例
    QString sample = QStringLiteral("probe_out/build_guide_sample.litematic");
    if (!QFileInfo::exists(sample))
        sample = QStringLiteral("../.syntax_check/test_proj.litematic");
    const QString sampleAbs = QFileInfo(sample).absoluteFilePath();
    expect("找到可用的投影样例", QFileInfo::exists(sampleAbs));

    const QString instanceRoot = QDir::currentPath() + QStringLiteral("/probe_instance");
    QDir(instanceRoot).removeRecursively();
    const QString versionDir = instanceRoot + QStringLiteral("/versions/1.20.4");
    const QString schematicsDir = versionDir + QStringLiteral("/schematics");
    QDir().mkpath(schematicsDir);
    const QString target = schematicsDir + QStringLiteral("/probe_build.litematic");
    if (QFileInfo::exists(sampleAbs))
        QFile::copy(sampleAbs, target);
    expect("临时实例里放好投影文件", QFileInfo::exists(target));

    InstanceResourcesPage page;
    page.resize(460, 760);
    page.setInstancePath(versionDir);
    page.show();
    pump(10);

    QPushButton *projectionTab = nullptr;
    for (QPushButton *btn : page.findChildren<QPushButton *>())
    {
        if (btn && btn->text() == QStringLiteral("投影"))
        {
            projectionTab = btn;
            break;
        }
    }
    expect("资源管理 Tab 栏出现「投影」", projectionTab != nullptr);
    if (projectionTab)
    {
        projectionTab->click();
        pump(10);
    }

    QWidget *projectionCard = nullptr;
    waitUntil([&]() {
        for (QWidget *w : page.findChildren<QWidget *>())
        {
            const QString path = w->property("filePath").toString();
            if (path.endsWith(QStringLiteral("probe_build.litematic")))
            {
                projectionCard = w;
                return true;
            }
        }
        return false;
    });
    expect("扫描到投影并生成卡片", projectionCard != nullptr);

    QPushButton *buildBtn = nullptr;
    if (projectionCard)
    {
        for (QPushButton *btn : projectionCard->findChildren<QPushButton *>())
        {
            if (btn && btn->toolTip().contains(QStringLiteral("学习搭建")))
            {
                buildBtn = btn;
                break;
            }
        }
    }
    expect("投影卡片带「学习搭建」按钮", buildBtn != nullptr);

    if (buildBtn)
    {
        buildBtn->click();
        pump(12);
        ProjectionBuildGuideDialog *guide = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets())
        {
            if (auto *dialog = qobject_cast<ProjectionBuildGuideDialog *>(w))
            {
                if (dialog->isVisible())
                    guide = dialog;
            }
        }
        expect("点按钮能打开学习搭建窗口", guide != nullptr);
        if (guide)
            guide->close();
        pump(4);
    }

    const QPixmap shot = page.grab();
    const bool saved = shot.save(QStringLiteral("probe_out/assistant_projection_tab.png"));
    fprintf(stderr, "[probe] shot probe_out/assistant_projection_tab.png %dx%d %s\n",
            shot.width(), shot.height(), saved ? "ok" : "FAILED");

    fprintf(stderr, "[probe] failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
