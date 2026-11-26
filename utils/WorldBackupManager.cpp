/**
 * @file   WorldBackupManager.cpp
 * @brief  世界备份与恢复管理器实现
 * @author BlockBox Team
 * @date   2026-08-29
 */
#include "WorldBackupManager.h"

#include "../platform.h"
#include "SettingsManager.h"
#include "plugin/PluginZip.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QThreadPool>
#include <QtGlobal>

#include <algorithm>

namespace {

/** 世界目录候选：优先游戏实际读取的 saves 目录（版本隔离感知），兼容实例目录下的 saves */
QStringList worldDirCandidates(const QString &instancePath, const QString &worldName)
{
    QStringList dirs;
    dirs << WorldBackupManager::savesDirForInstance(instancePath) + "/" + worldName;
    dirs << instancePath + "/saves/" + worldName;
    dirs.removeDuplicates();
    return dirs;
}

/** 取第一个实际存在的世界目录，均不存在时返回首选路径 */
QString existingWorldDir(const QString &instancePath, const QString &worldName)
{
    const QStringList candidates = worldDirCandidates(instancePath, worldName);
    for (const QString &dir : candidates) {
        if (QFileInfo::exists(dir))
            return dir;
    }
    return candidates.first();
}

QString backupZipPath(const QString &instancePath, const QString &worldName, const QString &id)
{
    return WorldBackupManager::worldBackupDir(instancePath, worldName)
        + "/" + worldName + "_" + id + ".zip";
}

QString backupMetaPath(const QString &instancePath, const QString &worldName, const QString &id)
{
    return WorldBackupManager::worldBackupDir(instancePath, worldName)
        + "/" + worldName + "_" + id + ".json";
}

WorldBackupInfo infoFromJson(const QJsonObject &obj)
{
    WorldBackupInfo info;
    info.id = obj.value("id").toString();
    info.worldName = obj.value("worldName").toString();
    // ISODate 会丢弃毫秒，必须用 ISODateWithMs 才能精确往返
    info.createdAt = QDateTime::fromString(obj.value("createdAt").toString(),
                                           Qt::ISODateWithMs);
    info.sourceSize = static_cast<qint64>(obj.value("sourceSize").toDouble(0));
    info.archiveSize = static_cast<qint64>(obj.value("archiveSize").toDouble(0));
    info.automatic = obj.value("automatic").toBool();
    info.note = obj.value("note").toString();
    info.instanceName = obj.value("instanceName").toString();
    info.gameVersion = obj.value("gameVersion").toString();
    return info;
}

QJsonObject infoToJson(const WorldBackupInfo &info)
{
    QJsonObject obj;
    obj.insert("id", info.id);
    obj.insert("worldName", info.worldName);
    obj.insert("createdAt", info.createdAt.toString(Qt::ISODateWithMs));
    obj.insert("sourceSize", static_cast<double>(info.sourceSize));
    obj.insert("archiveSize", static_cast<double>(info.archiveSize));
    obj.insert("automatic", info.automatic);
    obj.insert("note", info.note);
    obj.insert("instanceName", info.instanceName);
    obj.insert("gameVersion", info.gameVersion);
    return obj;
}

/** 生成备份ID：时间戳 + 随机后缀，避免同一秒内重复 */
QString generateBackupId()
{
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    const quint32 rnd = QRandomGenerator::system()->generate() & 0xFFFF;
    return stamp + QStringLiteral("_") + QString::number(rnd, 16).rightJustified(4, QLatin1Char('0'));
}

/** 世界名 / 备份ID 都会拼进文件路径：拒绝分隔符与 .. ，防止越出备份目录 */
bool isSafePathComponent(const QString &name)
{
    return !name.isEmpty()
        && !name.contains(QLatin1Char('/'))
        && !name.contains(QLatin1Char('\\'))
        && name != QStringLiteral(".")
        && name != QStringLiteral("..");
}

/** 恢复/备份过程中的临时目录后缀（这些不是真正的世界，列表里要跳过） */
bool isTemporaryWorldName(const QString &name)
{
    return name.contains(QStringLiteral(".restoring_"))
        || name.contains(QStringLiteral(".old_"));
}

/** 某实例自动备份时间戳的 QSettings 键（按实例分开记） */
QString lastAutoBackupKey(const QString &instancePath)
{
    return QStringLiteral("worldBackup/lastAutoBackup/")
        + QFileInfo(WorldBackupManager::instanceBackupDir(instancePath)).fileName();
}

} // namespace

// ─────────────────────────────────────────────────────────────

bool WorldBackupInfo::exists() const
{
    return !archivePath.isEmpty() && QFileInfo::exists(archivePath);
}

WorldBackupManager *WorldBackupManager::instance()
{
    static WorldBackupManager s_instance;
    return &s_instance;
}

WorldBackupManager::WorldBackupManager(QObject *parent)
    : QObject(parent)
{
}

// ── 路径 ──

QString WorldBackupManager::backupsRoot()
{
    return Platform::getDataDirectory() + "/WorldBackups/Worlds";
}

QString WorldBackupManager::instanceBackupDir(const QString &instancePath)
{
    // 实例名 + 路径哈希：同名实例在不同实例文件夹中互不混淆
    const QString name = QFileInfo(instancePath).fileName();
    const QByteArray hash =
        QCryptographicHash::hash(QDir(instancePath).absolutePath().toUtf8(),
                                 QCryptographicHash::Md5).toHex().left(8);
    return backupsRoot() + "/" + name + "_" + QString::fromLatin1(hash);
}

QString WorldBackupManager::worldBackupDir(const QString &instancePath, const QString &worldName)
{
    return instanceBackupDir(instancePath) + "/" + worldName;
}

QString WorldBackupManager::savesDirForInstance(const QString &instancePath)
{
    // 与 GameLauncher 的 --gameDir 计算保持一致：
    // 版本隔离 → 存档在版本目录内；非隔离 → 存档在实例根目录的 saves/
    if (SettingsManager::instance()->isVersionIsolationEnabled())
        return instancePath + "/saves";
    return QDir(QFileInfo(instancePath).dir().absolutePath() + "/..").absolutePath() + "/saves";
}

QStringList WorldBackupManager::listWorldNames(const QString &instancePath)
{
    QStringList names;
    for (const QString &dir : worldDirCandidates(instancePath, QString())) {
        QDir savesDir(dir);
        if (!savesDir.exists())
            continue;
        const QFileInfoList entries =
            savesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &fi : entries) {
            // 恢复过程中的临时目录（xxx.restoring_* / xxx.old_*）不是真正的世界
            if (isTemporaryWorldName(fi.fileName()))
                continue;
            // 视为世界目录：包含 level.dat 或 region/ 子目录
            if (QFileInfo::exists(fi.absoluteFilePath() + "/level.dat")
                    || QDir(fi.absoluteFilePath() + "/region").exists()) {
                if (!names.contains(fi.fileName()))
                    names << fi.fileName();
            }
        }
    }
    return names;
}

qint64 WorldBackupManager::directorySize(const QString &dirPath)
{
    qint64 total = 0;
    QDirIterator it(dirPath, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

// ── 设置项 ──

bool WorldBackupManager::autoBackupEnabled()
{
    return SettingsManager::instance()
        ->getProperty(QStringLiteral("worldBackup/autoEnabled"), false).toBool();
}

void WorldBackupManager::setAutoBackupEnabled(bool enabled)
{
    SettingsManager::instance()->setProperty(QStringLiteral("worldBackup/autoEnabled"), enabled);
}

int WorldBackupManager::autoBackupFrequency()
{
    return SettingsManager::instance()
        ->getProperty(QStringLiteral("worldBackup/frequency"), 0).toInt();
}

void WorldBackupManager::setAutoBackupFrequency(int frequency)
{
    SettingsManager::instance()->setProperty(QStringLiteral("worldBackup/frequency"), frequency);
}

int WorldBackupManager::keepCount()
{
    return qBound(1, SettingsManager::instance()
                         ->getProperty(QStringLiteral("worldBackup/keepCount"), 10).toInt(), 100);
}

void WorldBackupManager::setKeepCount(int count)
{
    SettingsManager::instance()->setProperty(QStringLiteral("worldBackup/keepCount"),
                                             qBound(1, count, 100));
}

bool WorldBackupManager::snapshotBeforeRestore()
{
    return SettingsManager::instance()
        ->getProperty(QStringLiteral("worldBackup/snapshotBeforeRestore"), true).toBool();
}

void WorldBackupManager::setSnapshotBeforeRestore(bool enabled)
{
    SettingsManager::instance()->setProperty(QStringLiteral("worldBackup/snapshotBeforeRestore"),
                                             enabled);
}

// ── 备份列表 ──

QList<WorldBackupInfo> WorldBackupManager::listBackups(const QString &instancePath,
                                                       const QString &worldName) const
{
    QList<WorldBackupInfo> result;
    const QString dir = worldBackupDir(instancePath, worldName);
    QDir backupDir(dir);
    if (!backupDir.exists())
        return result;

    // 不用通配符过滤（世界名可能含 [] * ? 等被当作通配符的字符），改为按前缀手动筛选
    const QString metaPrefix = worldName + QStringLiteral("_");
    QSet<QString> seenIds;
    // 主来源：元数据 JSON
    const QFileInfoList allFiles = backupDir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &fi : allFiles) {
        const QString fileName = fi.fileName();
        if (!fileName.startsWith(metaPrefix) || !fileName.endsWith(QStringLiteral(".json")))
            continue;
        QFile file(fi.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        WorldBackupInfo info = infoFromJson(obj);
        if (info.id.isEmpty()) {
            // 文件名主干兜底：worldName_id.json
            info.id = fi.completeBaseName();
            info.id.remove(0, worldName.size() + 1);
        }
        info.archivePath = backupZipPath(instancePath, worldName, info.id);
        if (!info.exists())
            continue; // 压缩包已被手动删除，跳过孤儿元数据
        info.worldName = worldName;
        seenIds.insert(info.id);
        result.append(info);
    }

    // 兜底：元数据丢失但压缩包仍在的备份（手动复制等情形）
    for (const QFileInfo &fi : allFiles) {
        const QString fileName = fi.fileName();
        if (!fileName.startsWith(metaPrefix) || !fileName.endsWith(QStringLiteral(".zip")))
            continue;
        QString id = fi.completeBaseName();
        id.remove(0, worldName.size() + 1);
        if (seenIds.contains(id))
            continue;
        WorldBackupInfo info;
        info.id = id;
        info.worldName = worldName;
        info.createdAt = fi.lastModified();
        info.archiveSize = fi.size();
        info.archivePath = fi.absoluteFilePath();
        result.append(info);
    }

    std::sort(result.begin(), result.end(),
              [](const WorldBackupInfo &a, const WorldBackupInfo &b) {
                  return a.createdAt > b.createdAt;
              });
    return result;
}

QStringList WorldBackupManager::listBackupWorldNames(const QString &instancePath) const
{
    QStringList names;
    QDir root(instanceBackupDir(instancePath));
    if (!root.exists())
        return names;

    QList<QPair<QDateTime, QString>> latestByName;
    const QFileInfoList dirs =
        root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &dirInfo : dirs) {
        const QString worldName = dirInfo.fileName();
        const QList<WorldBackupInfo> backups = listBackups(instancePath, worldName);
        if (backups.isEmpty())
            continue; // 只有空目录 / 孤儿元数据，没有可用的备份
        latestByName.append(qMakePair(backups.first().createdAt, worldName));
    }
    std::sort(latestByName.begin(), latestByName.end(),
              [](const QPair<QDateTime, QString> &a, const QPair<QDateTime, QString> &b) {
                  return a.first > b.first;
              });
    for (const auto &item : latestByName)
        names << item.second;
    return names;
}

// ── 同步核心 ──

bool WorldBackupManager::createBackupSync(const QString &instancePath, const QString &worldName,
                                          const QString &note, bool automatic,
                                          WorldBackupInfo *outInfo, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return false;
    };

    if (!isSafePathComponent(worldName))
        return fail(QCoreApplication::translate("WorldBackupManager", "世界名不合法: %1").arg(worldName));

    const QString worldPath = existingWorldDir(instancePath, worldName);
    if (!QDir(worldPath).exists())
        return fail(QCoreApplication::translate("WorldBackupManager", "世界目录不存在: %1").arg(worldPath));
    const qint64 sourceBytes = directorySize(worldPath);
    if (sourceBytes <= 0)
        return fail(QCoreApplication::translate("WorldBackupManager", "世界目录为空，无法备份"));

    const QString dir = worldBackupDir(instancePath, worldName);
    if (!QDir().mkpath(dir))
        return fail(QCoreApplication::translate("WorldBackupManager", "无法创建备份目录: %1").arg(dir));

    const QString id = generateBackupId();
    const QString zipPath = backupZipPath(instancePath, worldName, id);
    const QString metaPath = backupMetaPath(instancePath, worldName, id);

    // 压缩世界目录（条目路径相对于世界目录，恢复时解压到临时目录再整体替换）
    bool ok = false;
    const int entries = PluginZip::zipDirectory(worldPath, zipPath, &ok);
    if (!ok || entries <= 0) {
        QFile::remove(zipPath);
        return fail(QCoreApplication::translate("WorldBackupManager", "压缩世界文件失败"));
    }

    WorldBackupInfo info;
    info.id = id;
    info.worldName = worldName;
    info.createdAt = QDateTime::currentDateTime();
    info.sourceSize = sourceBytes;
    info.archiveSize = QFileInfo(zipPath).size();
    info.automatic = automatic;
    info.note = note;
    info.instanceName = QFileInfo(instancePath).fileName();
    info.gameVersion = readGameVersion(instancePath);
    info.archivePath = zipPath;

    // QSaveFile：元数据要么完整写入，要么完全不落盘，避免断电留下半个 JSON
    QSaveFile metaFile(metaPath);
    if (!metaFile.open(QIODevice::WriteOnly)) {
        QFile::remove(zipPath);
        return fail(QCoreApplication::translate("WorldBackupManager", "写入备份元数据失败"));
    }
    metaFile.write(QJsonDocument(infoToJson(info)).toJson(QJsonDocument::Compact));
    if (!metaFile.commit()) {
        QFile::remove(zipPath);
        return fail(QCoreApplication::translate("WorldBackupManager", "写入备份元数据失败"));
    }

    if (outInfo) *outInfo = info;
    return true;
}

bool WorldBackupManager::restoreBackupSync(const QString &instancePath, const QString &worldName,
                                           const QString &backupId, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return false;
    };

    if (!isSafePathComponent(worldName) || !isSafePathComponent(backupId))
        return fail(QCoreApplication::translate("WorldBackupManager", "备份标识不合法"));

    const QString zipPath = backupZipPath(instancePath, worldName, backupId);
    if (!QFileInfo::exists(zipPath))
        return fail(QCoreApplication::translate("WorldBackupManager", "备份文件不存在或已被删除"));

    const QString savesDir = savesDirForInstance(instancePath);
    const QString worldPath = savesDir + "/" + worldName;
    if (!QDir().mkpath(savesDir))
        return fail(QCoreApplication::translate("WorldBackupManager", "无法创建存档目录: %1").arg(savesDir));

    // 0) 恢复前先为当前世界创建快照，保证恢复操作可回退
    if (QDir(worldPath).exists() && snapshotBeforeRestore()) {
        WorldBackupInfo snapshot;
        QString snapshotError;
        if (!createBackupSync(instancePath, worldName,
                              QCoreApplication::translate("WorldBackupManager", "恢复前自动快照"),
                              true, &snapshot, &snapshotError)) {
            // 快照失败意味着当前进度可能丢失，中止恢复
            return fail(QCoreApplication::translate("WorldBackupManager",
                       "恢复前创建快照失败，已中止恢复: %1").arg(snapshotError));
        }
    }

    const QString uid = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString extractDir = savesDir + "/" + worldName + ".restoring_" + uid;
    const QString oldDir = savesDir + "/" + worldName + ".old_" + uid;

    // 1) 解压备份到临时目录
    bool ok = false;
    PluginZip::extractAllToDir(zipPath, extractDir, &ok);
    if (!ok || !QDir(extractDir).exists()) {
        QDir(extractDir).removeRecursively();
        return fail(QCoreApplication::translate("WorldBackupManager", "解压备份文件失败"));
    }

    // 2) 将当前世界目录整体移出（重命名是快速操作，保证任意失败都能回滚）
    bool hasOld = false;
    if (QDir(worldPath).exists()) {
        if (!QDir().rename(worldPath, oldDir)) {
            QDir(extractDir).removeRecursively();
            return fail(QCoreApplication::translate("WorldBackupManager",
                       "无法移除当前世界目录（世界可能正被占用）"));
        }
        hasOld = true;
    }

    // 3) 将解压结果重命名为世界目录
    if (!QDir().rename(extractDir, worldPath)) {
        QDir(extractDir).removeRecursively();
        if (hasOld)
            QDir().rename(oldDir, worldPath);
        return fail(QCoreApplication::translate("WorldBackupManager", "恢复世界失败：无法完成目录替换"));
    }

    // 4) 清理旧目录
    if (hasOld)
        QDir(oldDir).removeRecursively();
    return true;
}

bool WorldBackupManager::deleteBackupSync(const QString &instancePath, const QString &worldName,
                                          const QString &backupId, QString *error)
{
    if (!isSafePathComponent(worldName) || !isSafePathComponent(backupId)) {
        if (error) *error = QCoreApplication::translate("WorldBackupManager", "备份标识不合法");
        return false;
    }
    const QString zipPath = backupZipPath(instancePath, worldName, backupId);
    const QString metaPath = backupMetaPath(instancePath, worldName, backupId);
    const bool existed = QFileInfo::exists(zipPath);
    if (existed && !QFile::remove(zipPath)) {
        if (error) *error = QCoreApplication::translate("WorldBackupManager", "删除备份失败（文件可能被占用）");
        return false;
    }
    QFile::remove(metaPath);
    return true;
}

// ── 异步封装 ──

bool WorldBackupManager::tryAcquireBusy()
{
    return m_busy.testAndSetOrdered(0, 1);
}

void WorldBackupManager::releaseBusy()
{
    m_busy.storeRelease(0);
}

void WorldBackupManager::createBackupAsync(const QString &instancePath, const QString &worldName,
                                           const QString &note, bool automatic)
{
    if (!tryAcquireBusy()) {
        emit backupFinished(instancePath, worldName, false,
                            QCoreApplication::translate("WorldBackupManager", "已有备份任务正在进行"),
                            automatic);
        return;
    }
    const QPointer<WorldBackupManager> self(this);
    const int keep = keepCount();
    QThreadPool::globalInstance()->start([self, instancePath, worldName, note, automatic, keep]() {
        if (!self)
            return;
        WorldBackupInfo info;
        QString error;
        const bool ok = self->createBackupSync(instancePath, worldName, note,
                                               automatic, &info, &error);
        if (ok)
            self->applyRetentionPolicy(instancePath, worldName, keep);
        // 必须有且只有一处释放：否则第一次备份之后忙碌标志永远为 1，
        // 本会话内所有备份/恢复请求都会被"已有备份任务正在进行"拒绝
        self->releaseBusy();
        emit self->backupFinished(instancePath, worldName, ok,
                                  ok ? QString() : error, automatic);
    });
}

void WorldBackupManager::restoreBackupAsync(const QString &instancePath, const QString &worldName,
                                            const QString &backupId)
{
    if (!tryAcquireBusy()) {
        emit restoreFinished(instancePath, worldName, false,
                             QCoreApplication::translate("WorldBackupManager", "已有备份任务正在进行"));
        return;
    }
    const QPointer<WorldBackupManager> self(this);
    QThreadPool::globalInstance()->start([self, instancePath, worldName, backupId]() {
        if (!self)
            return;
        QString error;
        const bool ok = self->restoreBackupSync(instancePath, worldName, backupId, &error);
        self->releaseBusy(); // 同 createBackupAsync：异步完成后必须复位
        emit self->restoreFinished(instancePath, worldName, ok, ok ? QString() : error);
    });
}

// ── 保留策略 ──

int WorldBackupManager::applyRetentionPolicy(const QString &instancePath, const QString &worldName,
                                             int keep)
{
    if (keep <= 0)
        return 0;
    const QList<WorldBackupInfo> backups = listBackups(instancePath, worldName);
    int removed = 0;
    QString error;
    for (int i = keep; i < backups.size(); ++i) {
        if (deleteBackupSync(instancePath, worldName, backups[i].id, &error))
            ++removed;
    }
    return removed;
}

// ── 启动前自动备份 ──

void WorldBackupManager::startAutoBackupBeforeLaunch(const QString &instancePath)
{
    if (!autoBackupEnabled()) {
        emit autoBackupBatchFinished(0, 0);
        return;
    }

    // 频率门控：0=每次启动 1=每小时 2=每天（时间戳按实例分开记）
    const int frequency = autoBackupFrequency();
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime last = lastAutoBackupTime(instancePath);
    qint64 intervalMs = 0;
    if (frequency == 1) intervalMs = 3600LL * 1000;
    else if (frequency == 2) intervalMs = 24LL * 3600 * 1000;
    if (intervalMs > 0 && last.isValid() && last.msecsTo(now) < intervalMs) {
        emit autoBackupBatchFinished(0, 0);
        return;
    }

    // 仅备份自上次备份后有改动的世界
    QStringList changedWorlds;
    const QStringList worlds = listWorldNames(instancePath);
    for (const QString &name : worlds) {
        const QString worldPath = existingWorldDir(instancePath, name);
        const QList<WorldBackupInfo> backups = listBackups(instancePath, name);
        const QDateTime lastBackupTime = backups.isEmpty() ? QDateTime()
                                                           : backups.first().createdAt;
        if (lastBackupTime.isNull() || worldModifiedTime(worldPath) > lastBackupTime)
            changedWorlds << name;
    }
    if (changedWorlds.isEmpty()) {
        emit autoBackupBatchFinished(0, 0);
        return;
    }

    // 已有备份任务（如用户正在手动备份）时直接跳过，不阻塞启动
    if (!tryAcquireBusy()) {
        emit autoBackupBatchFinished(0, 0);
        return;
    }

    const QPointer<WorldBackupManager> self(this);
    const int keep = keepCount();
    QThreadPool::globalInstance()->start([self, instancePath, changedWorlds, now, keep]() {
        if (!self)
            return;
        const QPair<int, int> result = self->runAutoBackupBatch(instancePath, changedWorlds, keep);
        if (result.first > 0) {
            // 自动备份时间戳写回 QSettings，交给 UI 线程做（QSettings 不在工作线程里用）
            QMetaObject::invokeMethod(self, [self, instancePath, now]() {
                if (self)
                    self->setLastAutoBackupTime(instancePath, now);
            }, Qt::QueuedConnection);
        }
        self->releaseBusy();
        emit self->autoBackupBatchFinished(result.first, result.second);
    });
}

QPair<int, int> WorldBackupManager::runAutoBackupBatch(const QString &instancePath,
                                                      const QStringList &worlds, int keep)
{
    int backed = 0;
    int failed = 0;
    for (int i = 0; i < worlds.size(); ++i) {
        const QString &name = worlds[i];
        emit backupProgress(i * 100 / worlds.size(),
                            QCoreApplication::translate("WorldBackupManager", "正在备份世界 %1...").arg(name));
        WorldBackupInfo info;
        QString error;
        if (createBackupSync(instancePath, name, QString(), true, &info, &error)) {
            applyRetentionPolicy(instancePath, name, keep);
            ++backed;
            emit backupFinished(instancePath, name, true, QString(), true);
        } else {
            ++failed;
            qWarning() << "[WorldBackupManager]" << "自动备份失败：" << name << error;
            emit backupFinished(instancePath, name, false, error, true);
        }
    }
    emit backupProgress(100, QCoreApplication::translate("WorldBackupManager", "自动备份完成"));
    if (failed > 0)
        qWarning() << "[WorldBackupManager]" << "自动备份批量完成，失败" << failed << "次";
    return qMakePair(backed, failed);
}

// ── 辅助 ──

QString WorldBackupManager::readGameVersion(const QString &instancePath)
{
    const QString name = QFileInfo(instancePath).fileName();
    QFile file(instancePath + "/" + name + ".json");
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    if (obj.contains("inheritsFrom"))
        return obj.value("inheritsFrom").toString();
    if (obj.contains("clientVersion"))
        return obj.value("clientVersion").toString();
    return obj.value("id").toString();
}

QDateTime WorldBackupManager::worldModifiedTime(const QString &worldPath)
{
    QDateTime latest = QFileInfo(worldPath).lastModified();
    const QDateTime levelDat = QFileInfo(worldPath + "/level.dat").lastModified();
    if (levelDat.isValid() && levelDat > latest)
        latest = levelDat;
    return latest;
}

QDateTime WorldBackupManager::lastAutoBackupTime(const QString &instancePath) const
{
    const QString raw = SettingsManager::instance()
                            ->getProperty(lastAutoBackupKey(instancePath)).toString();
    return QDateTime::fromString(raw, Qt::ISODateWithMs);
}

void WorldBackupManager::setLastAutoBackupTime(const QString &instancePath, const QDateTime &time)
{
    SettingsManager::instance()->setProperty(lastAutoBackupKey(instancePath),
                                             time.toString(Qt::ISODateWithMs));
}
