/**
 * @file   test_taskdetail.cpp
 * @brief  任务详情页按类型渲染冒烟测试（离屏运行）
 *
 * 用真实构建产物链接，把每种任务类型灌进 TaskDetailPage，
 * 断言标题动词、阶段步条步数与阶段标签按类型区分。
 */

#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QTimer>

#include "pages/TaskDetailPage.h"
#include "utils/DownloadTaskManager.h"

static int g_failures = 0;

static void check(bool ok, const QString &what)
{
    qWarning() << (ok ? "[PASS]" : "[FAIL]") << what;
    if (!ok)
        ++g_failures;
}

static QLabel *findChip(QLabel *root)
{
    return root;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    DownloadTaskManager *tm = DownloadTaskManager::instance();

    struct Case
    {
        TaskType type;
        QString name;
        QString mcVersion;
        QStringList loaders;
        int expectStepCount;
        QString expectTitle;
        QString expectFirstStage;
    };

    const QList<Case> cases = {
        { TaskType::InstanceInstall, "测试实例", "1.21.1", { "Forge" }, 7,
          QObject::tr("安装 测试实例"), QObject::tr("获取版本清单") },
        { TaskType::ContentDownload, "测试实例", QString(), {}, 2,
          QObject::tr("下载 测试实例"), QObject::tr("下载文件") },
        { TaskType::LoaderInstall, "测试实例", "1.21.1", { "NeoForge" }, 3,
          QObject::tr("安装 测试实例"), QObject::tr("下载安装器") },
        { TaskType::LanTransfer, "文件传输", QString(), {}, 2,
          QObject::tr("传输 文件传输"), QObject::tr("传输中") },
        { TaskType::AiModel, "Ollama 模型", QString(), {}, 2,
          QObject::tr("下载 Ollama 模型"), QObject::tr("下载中") },
    };

    TaskDetailPage page;

    for (const Case &c : cases)
    {
        const QString taskId = tm->addTaskWithType(c.name, "D:/tmp-inst", c.mcVersion,
                                                   c.loaders, c.type);
        check(!taskId.isEmpty(),
              QStringLiteral("创建任务 type=%1").arg(int(c.type)));

        page.setTaskId(taskId);
        app.processEvents();

        // 标题
        QLabel *title = page.findChild<QLabel *>(QStringLiteral("tdTitle"));
        check(title && title->text() == c.expectTitle,
              QStringLiteral("标题 [%1] 期望<%2> 实际<%3>")
                  .arg(int(c.type)).arg(c.expectTitle, title ? title->text() : QString()));

        // 阶段 chip：阶段 1 / N · 首阶段名
        QLabel *stageChip = page.findChild<QLabel *>(QStringLiteral("tdStageChip"));
        const QString expectChip = QObject::tr("阶段 %1 / %2 · %3")
                                       .arg(1).arg(c.expectStepCount).arg(c.expectFirstStage);
        check(stageChip && stageChip->text() == expectChip,
              QStringLiteral("阶段chip type=%1 期望<%2> 实际<%3>")
                  .arg(int(c.type)).arg(expectChip, stageChip ? stageChip->text() : QString()));

        // 信息卡首行按类型改名
        QLabel *instanceKey = nullptr;
        const QList<QLabel *> keyLabels = page.findChildren<QLabel *>(
            QStringLiteral("tdInfoKey"));
        for (QLabel *l : keyLabels)
        {
            if (l->text().contains(QObject::tr("实例"))
                || l->text().contains(QObject::tr("任务名称")))
            {
                instanceKey = l;
                break;
            }
        }
        const bool instanceLike = (c.type == TaskType::InstanceInstall
                                   || c.type == TaskType::LoaderInstall
                                   || c.type == TaskType::ContentDownload);
        const QString expectKey = instanceLike ? QObject::tr("目标实例")
                                               : QObject::tr("任务名称");
        check(instanceKey && instanceKey->text() == expectKey,
              QStringLiteral("信息卡首行 type=%1 期望<%2> 实际<%3>")
                  .arg(int(c.type)).arg(expectKey,
                                        instanceKey ? instanceKey->text() : QString()));

        tm->removeTask(taskId);
    }

    // 动态映射：进度推进时阶段步条应跟随（加载器安装的 60/90/100 预算）
    {
        const QString taskId = tm->addTaskWithType("测试实例", "D:/tmp-inst", "1.21.1",
                                                   QStringList{"NeoForge"},
                                                   TaskType::LoaderInstall);
        page.setTaskId(taskId);
        QLabel *stageChip = page.findChild<QLabel *>(QStringLiteral("tdStageChip"));

        auto chipAt = [&](int percent) {
            tm->updateTaskProgressPercent(taskId, percent);
            tm->updateTaskStageProgress(taskId, DownloadStage::LoaderInstall, percent);
            page.onTaskStageChanged(taskId, DownloadStage::LoaderInstall, percent);
            app.processEvents();
            return stageChip ? stageChip->text() : QString();
        };

        check(chipAt(60) == QObject::tr("阶段 1 / 3 · 下载安装器"),
              QStringLiteral("加载器 60%% → 下载安装器 实际<%1>").arg(chipAt(60)));
        check(chipAt(91) == QObject::tr("阶段 2 / 3 · 安装组件"),
              QStringLiteral("加载器 91%% → 安装组件 实际<%1>").arg(chipAt(91)));
        check(chipAt(100) == QObject::tr("阶段 3 / 3 · 安装完成"),
              QStringLiteral("加载器 100%% → 安装完成 实际<%1>").arg(chipAt(100)));

        tm->removeTask(taskId);
    }

    // 内容下载完成态：进入「完成」步
    {
        const QString taskId = tm->addTaskWithType("测试实例", "D:/tmp-inst", QString(),
                                                   QStringList(), TaskType::ContentDownload);
        page.setTaskId(taskId);
        tm->updateTaskProgressPercent(taskId, 100);
        tm->updateTaskStage(taskId, DownloadStage::Completed, 100);
        tm->updateTaskStatus(taskId, DownloadTaskStatus::Completed, QObject::tr("下载完成"));
        page.onTaskStatusChanged(taskId, DownloadTaskStatus::Completed);
        app.processEvents();

        QLabel *stageChip = page.findChild<QLabel *>(QStringLiteral("tdStageChip"));
        check(stageChip && stageChip->text() == QObject::tr("阶段 2 / 2 · 完成"),
              QStringLiteral("内容下载完成 → 完成 实际<%1>")
                  .arg(stageChip ? stageChip->text() : QString()));

        tm->removeTask(taskId);
    }

    qWarning() << (g_failures == 0 ? "SMOKE: ALL PASS" : "SMOKE: FAILURES PRESENT");
    return g_failures == 0 ? 0 : 1;
}
