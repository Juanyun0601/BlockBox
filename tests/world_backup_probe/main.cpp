/**
 * @file   main.cpp
 * @brief  存档备份功能独立验证程序
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 覆盖：备份创建与落盘布局、备份列表与备注、恢复（含恢复前快照）、
 *       保留策略、删除、启动前自动备份（改动检测 / 频率门控）、
 *       越界存档名防护、临时目录过滤。
 */
#include "utils/WorldBackupManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>

namespace {

int g_failures = 0;

void check(bool ok, const QString &what)
{
    if (ok) {
        qInfo().noquote() << "  ok  " << what;
    } else {
        ++g_failures;
        qCritical().noquote() << "  FAIL" << what;
    }
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(content);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

/** 同步等一次启动前自动备份批次（超时 20 秒算失败） */
void runAutoBackup(WorldBackupManager *manager, const QString &instancePath,
                   int *backed, int *failed)
{
    *backed = -1;
    *failed = -1;
    QEventLoop loop;
    bool done = false;
    QObject::connect(manager, &WorldBackupManager::autoBackupBatchFinished, &loop,
                     [backed, failed, &loop, &done](int okCount, int failCount) {
                         *backed = okCount;
                         *failed = failCount;
                         done = true;
                         loop.quit();
                     });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    manager->startAutoBackupBeforeLaunch(instancePath);
    // 未启用 / 频率未到 / 无可备份存档时信号是同步发出的，此时不必再进事件循环
    if (!done)
        loop.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        qCritical() << "无法创建临时目录";
        return 2;
    }
    qputenv("BLOCKBOX_PROBE_DATA_DIR", tempDir.path().toUtf8());

    const QString instance = tempDir.path() + "/instances/ProbeInstance";
    const QString savesDir = instance + "/saves";
    const QString world1 = savesDir + "/世界一";
    const QString world2 = savesDir + "/世界二";
    const QString world3 = savesDir + "/世界三";

    writeFile(world1 + "/level.dat", "LEVEL-DATA-V1");
    writeFile(world1 + "/region/r.0.0.mca", "REGION-BYTES");
    writeFile(world1 + "/data/raids.dat", "RAIDS");

    qInfo() << "== 存档发现 ==";
    // 恢复过程中的临时目录不能被当成真实存档
    writeFile(savesDir + "/世界一.old_123/level.dat", "TEMP");
    writeFile(savesDir + "/世界一.restoring_456/level.dat", "TEMP");
    const QStringList worlds = WorldBackupManager::listWorldNames(instance);
    check(worlds == QStringList{ QStringLiteral("世界一") },
          QStringLiteral("只列出真实存档，跳过 .old_/.restoring_ 临时目录（实际：%1）")
              .arg(worlds.join(QLatin1Char(','))));
    // 这两个目录只是用来验证过滤逻辑，清掉，避免干扰后面「恢复后无残留」的检查
    QDir(savesDir + "/世界一.old_123").removeRecursively();
    QDir(savesDir + "/世界一.restoring_456").removeRecursively();

    WorldBackupManager *manager = WorldBackupManager::instance();
    QString error;

    qInfo() << "== 创建备份 ==";
    WorldBackupInfo first;
    check(manager->createBackupSync(instance, QStringLiteral("世界一"),
                                    QStringLiteral("打末影龙之前"), false, &first, &error),
          QStringLiteral("创建备份成功（%1）").arg(error));
    check(!first.id.isEmpty() && QFileInfo::exists(first.archivePath),
          QStringLiteral("备份压缩包已落盘：%1").arg(first.archivePath));
    check(first.sourceSize == WorldBackupManager::directorySize(world1),
          QStringLiteral("记录的原始大小与世界目录一致"));
    check(first.note == QStringLiteral("打末影龙之前") && !first.automatic,
          QStringLiteral("备注与「手动」标记正确写入元数据"));

    QList<WorldBackupInfo> list = manager->listBackups(instance, QStringLiteral("世界一"));
    check(list.size() == 1 && list.first().id == first.id,
          QStringLiteral("备份列表能读回这份备份"));

    // 元数据文件丢失时，压缩包本身仍应能列出来（兜底）
    const QString metaPath = WorldBackupManager::worldBackupDir(instance, QStringLiteral("世界一"))
                             + "/世界一_" + first.id + ".json";
    QFile::remove(metaPath);
    list = manager->listBackups(instance, QStringLiteral("世界一"));
    check(list.size() == 1 && list.first().id == first.id && list.first().automatic == false,
          QStringLiteral("元数据丢失时按压缩包兜底列出"));
    // 重新写回元数据，后面的用例继续用
    check(manager->deleteBackupSync(instance, QStringLiteral("世界一"), first.id, &error),
          QStringLiteral("清理兜底测试用的备份"));

    WorldBackupInfo manual;
    check(manager->createBackupSync(instance, QStringLiteral("世界一"),
                                    QStringLiteral("第一份"), false, &manual, &error),
          QStringLiteral("重新创建一份备份（%1）").arg(error));

    qInfo() << "== 恢复到备份点 ==";
    writeFile(world1 + "/level.dat", "LEVEL-DATA-CHANGED");
    WorldBackupManager::setSnapshotBeforeRestore(true);
    check(manager->restoreBackupSync(instance, QStringLiteral("世界一"), manual.id, &error),
          QStringLiteral("恢复成功（%1）").arg(error));
    check(readFile(world1 + "/level.dat") == "LEVEL-DATA-V1",
          QStringLiteral("世界内容已回到备份点"));
    check(readFile(world1 + "/region/r.0.0.mca") == "REGION-BYTES",
          QStringLiteral("区域文件一并恢复"));

    list = manager->listBackups(instance, QStringLiteral("世界一"));
    const bool hasSnapshot = std::any_of(list.cbegin(), list.cend(),
                                         [](const WorldBackupInfo &b) { return b.automatic; });
    check(list.size() == 2 && hasSnapshot,
          QStringLiteral("恢复前自动生成了自动快照（共 %1 份）").arg(list.size()));

    const QStringList leftovers = QDir(savesDir).entryList(
        QStringList{ QStringLiteral("*restoring*"), QStringLiteral("*.old_*") },
        QDir::Dirs | QDir::NoDotAndDotDot);
    check(leftovers.isEmpty(), QStringLiteral("恢复后没有残留临时目录"));

    qInfo() << "== 保留策略 / 删除 ==";
    for (int i = 0; i < 3; ++i) {
        WorldBackupInfo extra;
        check(manager->createBackupSync(instance, QStringLiteral("世界一"),
                                        QStringLiteral("批量%1").arg(i), false, &extra, &error),
              QStringLiteral("再创建一份备份（%1）").arg(error));
    }
    check(manager->listBackups(instance, QStringLiteral("世界一")).size() == 5,
          QStringLiteral("此时共 5 份备份"));
    const int removed = manager->applyRetentionPolicy(instance, QStringLiteral("世界一"), 2);
    list = manager->listBackups(instance, QStringLiteral("世界一"));
    check(removed == 3 && list.size() == 2,
          QStringLiteral("保留 2 份时清理掉最旧的 3 份（实际清理 %1）").arg(removed));

    const QString toDelete = list.last().id;
    check(manager->deleteBackupSync(instance, QStringLiteral("世界一"), toDelete, &error),
          QStringLiteral("手动删除一份备份（%1）").arg(error));
    check(manager->listBackups(instance, QStringLiteral("世界一")).size() == 1,
          QStringLiteral("删除后只剩 1 份"));

    qInfo() << "== 启动前自动备份 ==";
    writeFile(world2 + "/level.dat", "WORLD-TWO");
    WorldBackupManager::setAutoBackupEnabled(true);
    WorldBackupManager::setAutoBackupFrequency(0); // 每次启动
    WorldBackupManager::setKeepCount(10);

    int backed = 0;
    int failed = 0;
    runAutoBackup(manager, instance, &backed, &failed);
    check(backed == 1 && failed == 0,
          QStringLiteral("首次自动备份只备份没有备份的存档（成功 %1 / 失败 %2）")
              .arg(backed).arg(failed));
    check(manager->listBackups(instance, QStringLiteral("世界二")).size() == 1,
          QStringLiteral("新存档已生成自动备份"));

    // 频率门控：改成每小时，立刻再启动不应该重复备份
    WorldBackupManager::setAutoBackupFrequency(1);
    runAutoBackup(manager, instance, &backed, &failed);
    check(backed == 0 && failed == 0,
          QStringLiteral("频率未到时不重复备份（成功 %1）").arg(backed));

    // 改回每次启动，存档有改动时应该再备份一份
    WorldBackupManager::setAutoBackupFrequency(0);
    writeFile(world2 + "/level.dat", "WORLD-TWO-CHANGED");
    {
        // 让 level.dat 的修改时间明确新于上次备份（文件系统时间粒度兜底）
        QFile file(world2 + "/level.dat");
        if (file.open(QIODevice::ReadWrite))
            file.setFileTime(QDateTime::currentDateTime().addSecs(60),
                             QFileDevice::FileModificationTime);
    }
    runAutoBackup(manager, instance, &backed, &failed);
    check(backed == 1 && failed == 0,
          QStringLiteral("存档有改动时会补一份新备份（成功 %1）").arg(backed));
    check(manager->listBackups(instance, QStringLiteral("世界二")).size() == 2,
          QStringLiteral("世界二的自动备份变为 2 份"));

    // 关闭开关后不应再备份
    WorldBackupManager::setAutoBackupEnabled(false);
    writeFile(world2 + "/level.dat", "WORLD-TWO-AGAIN");
    runAutoBackup(manager, instance, &backed, &failed);
    check(backed == 0,
          QStringLiteral("关闭自动备份后启动不备份（成功 %1）").arg(backed));

    qInfo() << "== 备份总览（找回已删除的存档） ==";
    writeFile(world3 + "/level.dat", "WORLD-THREE");
    WorldBackupInfo world3Backup;
    check(manager->createBackupSync(instance, QStringLiteral("世界三"), QStringLiteral("删除前"),
                                    false, &world3Backup, &error),
          QStringLiteral("为世界三创建备份（%1）").arg(error));
    QDir(world3).removeRecursively();
    check(!WorldBackupManager::listWorldNames(instance).contains(QStringLiteral("世界三")),
          QStringLiteral("世界三的存档目录已被删除"));
    const QStringList backupWorlds = manager->listBackupWorldNames(instance);
    check(backupWorlds.contains(QStringLiteral("世界三")),
          QStringLiteral("备份总览里仍能列出已删除的世界（%1）")
              .arg(backupWorlds.join(QLatin1Char(','))));
    check(manager->restoreBackupSync(instance, QStringLiteral("世界三"), world3Backup.id, &error),
          QStringLiteral("已删除的世界可以直接从备份恢复（%1）").arg(error));
    check(readFile(world3 + "/level.dat") == "WORLD-THREE",
          QStringLiteral("恢复后世界文件回来了"));

    qInfo() << "== 安全性 ==";
    WorldBackupInfo bad;
    check(!manager->createBackupSync(instance, QStringLiteral("../越界"), QString(), false,
                                     &bad, &error),
          QStringLiteral("拒绝带路径分隔符的存档名"));
    check(!manager->restoreBackupSync(instance, QStringLiteral("世界一"),
                                      QStringLiteral("../../x"), &error),
          QStringLiteral("拒绝越界的备份 ID"));

    qInfo().noquote() << (g_failures == 0
                              ? QStringLiteral("\n全部用例通过")
                              : QStringLiteral("\n有 %1 个用例失败").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
