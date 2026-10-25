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

        // 离屏环境下信号直连投递不可复现，改按「任务进度变更 → 重开详情页」
        // 的确定性路径验证阶段映射（与用户重新打开详情页的行为一致）
        // Forge 实际汇报的是全局百分比：安装器下载 60→90、安装 90→100
        auto chipAt = [&](int percent) {
            tm->updateTaskProgressPercent(taskId, percent);
            page.setTaskId(taskId);
            app.processEvents();
            return stageChip ? stageChip->text() : QString();
        };

        const QString chip89 = chipAt(89);
        check(chip89 == QObject::tr("阶段 1 / 3 · 下载安装器"),
              QStringLiteral("加载器 89% → 下载安装器 实际<%1>").arg(chip89));
        const QString chip91 = chipAt(91);
        check(chip91 == QObject::tr("阶段 2 / 3 · 安装组件"),
              QStringLiteral("加载器 91% → 安装组件 实际<%1>").arg(chip91));
        const QString chip100 = chipAt(100);
        check(chip100 == QObject::tr("阶段 3 / 3 · 安装完成"),
              QStringLiteral("加载器 100% → 安装完成 实际<%1>").arg(chip100));

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
        app.processEvents();

        QLabel *stageChip = page.findChild<QLabel *>(QStringLiteral("tdStageChip"));
        check(stageChip && stageChip->text() == QObject::tr("阶段 2 / 2 · 完成"),
              QStringLiteral("内容下载完成 → 完成 实际<%1>")
                  .arg(stageChip ? stageChip->text() : QString()));

        tm->removeTask(taskId);
    }

    // 进度钳位：上游算出 >100 的百分比必须被压回 100（防"101.70%"这类显示）
    {
        const QString taskId = tm->addTaskWithType("钳位测试", "D:/tmp-inst", QString(),
                                                   QStringList(), TaskType::ContentDownload);
        tm->updateTaskProgressPercent(taskId, 150);
        check(tm->getTaskCopy(taskId).progressDouble <= 100.0,
              QStringLiteral("150%% 进度被钳位 实际<%1>")
                  .arg(tm->getTaskCopy(taskId).progressDouble));
        tm->updateTaskProgress(taskId, 102, 102, 100);
        check(tm->getTaskCopy(taskId).progressDouble <= 100.0,
              QStringLiteral("102%% 进度被钳位 实际<%1>")
                  .arg(tm->getTaskCopy(taskId).progressDouble));
        tm->removeTask(taskId);
    }

    qWarning() << (g_failures == 0 ? "SMOKE: ALL PASS" : "SMOKE: FAILURES PRESENT");
    return g_failures == 0 ? 0 : 1;
}
