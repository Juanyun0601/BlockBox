/**
 * @file   BedrockInstanceManager.cpp
 * @brief  基岩版实例管理器实现
 * @author BlockBox Team
 */

#include "BedrockInstanceManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>

#include "../BedrockLauncher.h"
#include "../../platform.h"

BedrockInstanceManager *BedrockInstanceManager::m_instance = nullptr;
QMutex BedrockInstanceManager::m_mutex;

BedrockInstanceManager *BedrockInstanceManager::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_mutex);
        if (!m_instance)
            m_instance = new BedrockInstanceManager();
    }
    return m_instance;
}

BedrockInstanceManager::BedrockInstanceManager(QObject *parent)
    : QObject(parent)
{
}

QString BedrockInstanceManager::instancesDir() const
{
    // 基岩版游戏数据独立存放，与 Java 版 .minecraft 分离
    return Platform::getBedrockDataDirectory() + QStringLiteral("/instances");
}

QString BedrockInstanceManager::instanceDataDir(const QString &id) const
{
    return instancesDir() + QLatin1Char('/') + id;
}

QString BedrockInstanceManager::gameDataRoot() const
{
#ifdef Q_OS_WIN
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty())
        return QString();

    // 优先使用 BedrockLauncher 已缓存的包系列名（无需再次调用 PowerShell）
    const BedrockLauncher::BedrockInfo info = BedrockLauncher::instance()->info();
    QStringList pfns;
    if (info.valid && !info.packageFamilyName.isEmpty())
        pfns << info.packageFamilyName;
    // 兜底：正式版 / 预览版的默认包系列名，通过 Packages 目录存在性快速判断是否已安装
    pfns << QStringLiteral("Microsoft.MinecraftUWP_8wekyb3d8bbwe")
         << QStringLiteral("Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe");

    for (const QString &pfn : pfns) {
        const QString pkgDir = QDir(localAppData).filePath(QStringLiteral("Packages/") + pfn);
        if (QDir(pkgDir).exists())
            return QDir(pkgDir).filePath(QStringLiteral("LocalState/games/com.mojang"));
    }
    return QString();
#else
    return QString();
#endif
}

void BedrockInstanceManager::ensureInitialized()
{
    if (m_initialized)
        return;
    m_initialized = true;

    loadFromDisk();
    migrateDataDirs();

    // 已有实例：补齐缺失的目录结构
    if (!m_instances.isEmpty()) {
        for (const BedrockInstance &inst : m_instances)
            QDir().mkpath(inst.dataDir + QStringLiteral("/com.mojang"));
        return;
    }

    const QString root = gameDataRoot();

    if (!root.isEmpty() && QDir(root).exists() && !isJunction(root)) {
        // 首次使用：把已有真实数据迁移进「默认数据」实例
        BedrockInstance inst;
        inst.id = generateId();
        inst.name = tr("默认数据");
        inst.dataDir = instancesDir() + QLatin1Char('/') + inst.id;
        inst.createdAt = QDateTime::currentDateTime();
        QDir().mkpath(inst.dataDir + QStringLiteral("/com.mojang"));

        QString err;
        if (!copyRecursively(root, inst.dataDir + QStringLiteral("/com.mojang"), &err))
            qWarning() << "[BedrockInstanceManager] 迁移基岩版数据失败:" << err;

        // 数据已复制进实例，将原目录改名备份后建立联接，成功后删除备份
        const QString backup = root + QStringLiteral("_bb_")
            + QString::number(QDateTime::currentMSecsSinceEpoch());
        if (QDir().rename(root, backup)) {
            QString linkErr;
            if (!createJunction(root, inst.dataDir + QStringLiteral("/com.mojang"), &linkErr)) {
                QDir().rename(backup, root);
                qWarning() << "[BedrockInstanceManager] 建立数据联接失败:" << linkErr;
            } else {
                QDir(backup).removeRecursively();
            }
        } else {
            qWarning() << "[BedrockInstanceManager] 无法移动已有游戏数据（游戏可能正在运行）";
        }

        m_instances.append(inst);
        setActiveInstance(inst.id);
        saveToDisk();
        emit instancesChanged();
    } else if (!root.isEmpty() && isJunction(root)) {
        // 已存在 junction（可能由其他工具管理）：创建指向其目标的默认实例
        BedrockInstance inst;
        inst.id = generateId();
        inst.name = tr("默认实例");
        const QString target = junctionTarget(root);
        inst.dataDir = target.endsWith(QStringLiteral("com.mojang"), Qt::CaseInsensitive)
            ? QFileInfo(target).absolutePath() : instancesDir() + QLatin1Char('/') + inst.id;
        inst.createdAt = QDateTime::currentDateTime();
        m_instances.append(inst);
        setActiveInstance(inst.id);
        saveToDisk();
        emit instancesChanged();
    } else {
        // 干净状态：创建一个空实例
        BedrockInstance inst = createInstance(tr("默认实例"));
        setActiveInstance(inst.id);
        saveToDisk();
    }
}

QVector<BedrockInstance> BedrockInstanceManager::instances() const
{
    return m_instances;
}

BedrockInstance BedrockInstanceManager::instanceById(const QString &id) const
{
    for (const BedrockInstance &inst : m_instances) {
        if (inst.id == id)
            return inst;
    }
    return BedrockInstance();
}

BedrockInstance BedrockInstanceManager::createInstance(const QString &name, const QString &importDir)
{
    ensureInitialized();

    BedrockInstance inst;
    inst.id = generateId();
    inst.name = name.isEmpty() ? tr("新实例") : name;
    inst.dataDir = instancesDir() + QLatin1Char('/') + inst.id;
    inst.createdAt = QDateTime::currentDateTime();
    QDir().mkpath(inst.dataDir + QStringLiteral("/com.mojang"));

    if (!importDir.isEmpty()) {
        QString source = importDir;
        if (QFileInfo(source).fileName().compare(QStringLiteral("com.mojang"), Qt::CaseInsensitive) != 0
            && QDir(source).exists(QStringLiteral("com.mojang"))) {
            source += QStringLiteral("/com.mojang");
        }
        QString err;
        if (!copyRecursively(source, inst.dataDir + QStringLiteral("/com.mojang"), &err))
            qWarning() << "[BedrockInstanceManager] 导入基岩版数据失败:" << err;
    }

    m_instances.append(inst);
    saveToDisk();
    emit instancesChanged();
    return inst;
}

bool BedrockInstanceManager::renameInstance(const QString &id, const QString &newName)
{
    const int idx = indexOf(id);
    if (idx < 0)
        return false;
    m_instances[idx].name = newName.isEmpty() ? tr("未命名实例") : newName;
    saveToDisk();
    emit instancesChanged();
    return true;
}

bool BedrockInstanceManager::removeInstance(const QString &id)
{
    const int idx = indexOf(id);
    if (idx < 0)
        return false;

    // 不允许删除当前激活的实例（其数据目录正被游戏使用）
    if (m_activeInstanceId == id)
        return false;

    const QString dir = m_instances[idx].dataDir;
    m_instances.removeAt(idx);
    saveToDisk();

    if (QDir(dir).exists() && !isJunction(dir))
        QDir(dir).removeRecursively();
    emit instancesChanged();
    return true;
}

QString BedrockInstanceManager::activeInstanceId() const
{
    return m_activeInstanceId;
}

BedrockInstance BedrockInstanceManager::activeInstance() const
{
    return instanceById(m_activeInstanceId);
}

void BedrockInstanceManager::refreshVersionInfo(const QString &id, const QString &version)
{
    const int idx = indexOf(id);
    if (idx < 0)
        return;
    if (m_instances[idx].version == version)
        return;
    m_instances[idx].version = version;
    m_instances[idx].lastPlayed = QDateTime::currentDateTime();
    saveToDisk();
    emit instancesChanged();
}

bool BedrockInstanceManager::activateInstance(const QString &id, QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();

    const int idx = indexOf(id);
    if (idx < 0) {
        if (errorMessage)
            *errorMessage = tr("实例不存在");
        return false;
    }

    const QString root = gameDataRoot();
    const QString target = m_instances[idx].dataDir + QStringLiteral("/com.mojang");

    // 未安装基岩版：仅记录当前实例，安装后数据隔离自动生效
    if (root.isEmpty()) {
        setActiveInstance(id);
        return true;
    }

    // 已是目标联接：直接激活
    if (isJunction(root) && QDir::cleanPath(junctionTarget(root)) == QDir::cleanPath(target)) {
        setActiveInstance(id);
        return true;
    }

    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QDir().mkpath(target)) {
        if (errorMessage)
            *errorMessage = tr("无法创建实例数据目录：%1").arg(target);
        return false;
    }

    if (QFileInfo::exists(root)) {
        if (isJunction(root)) {
            if (!removeJunction(root)) {
                if (errorMessage)
                    *errorMessage = tr("无法移除旧的数据联接，请先完全退出基岩版");
                return false;
            }
        } else {
            // 真实目录：先复制进目标实例（数据安全），再改名备份原目录，
            // 建立联接成功后才删除备份，失败则还原
            QString copyErr;
            if (!copyRecursively(root, target, &copyErr)) {
                if (errorMessage)
                    *errorMessage = tr("迁移已有游戏数据失败：%1").arg(copyErr);
                return false;
            }
            const QString backup = root + QStringLiteral("_bb_")
                + QString::number(QDateTime::currentMSecsSinceEpoch());
            if (!QDir().rename(root, backup)) {
                if (errorMessage)
                    *errorMessage = tr("无法移动现有游戏数据，请先完全退出基岩版");
                return false;
            }
            if (!createJunction(root, target, errorMessage)) {
                QDir().rename(backup, root);
                return false;
            }
            QDir(backup).removeRecursively();
            setActiveInstance(id);
            return true;
        }
    }

    if (!createJunction(root, target, errorMessage))
        return false;

    setActiveInstance(id);
    return true;
}

void BedrockInstanceManager::setActiveInstance(const QString &id)
{
    if (m_activeInstanceId == id)
        return;
    m_activeInstanceId = id;
    saveToDisk();
    emit instancesChanged();
}

int BedrockInstanceManager::indexOf(const QString &id) const
{
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances[i].id == id)
            return i;
    }
    return -1;
}

void BedrockInstanceManager::migrateDataDirs()
{
    // 旧位置（应用数据目录内）→ 独立基岩数据目录
    const QString oldRoot = QDir::cleanPath(Platform::getDataDirectory()
                                            + QStringLiteral("/bedrock/instances"));
    const QString newRoot = QDir::cleanPath(instancesDir());
    if (oldRoot == newRoot)
        return;

    const QString prefix = oldRoot + QLatin1Char('/');
    bool changed = false;
    for (BedrockInstance &inst : m_instances) {
        const QString dir = QDir::cleanPath(inst.dataDir);
        if (!dir.startsWith(prefix))
            continue;
        const QString rel = dir.mid(prefix.length());
        const QString newDir = newRoot + QLatin1Char('/') + rel;
        if (QDir(dir).exists() && !isJunction(dir)) {
            QDir().mkpath(QFileInfo(newDir).absolutePath());
            if (QDir().rename(dir, newDir)) {
                inst.dataDir = newDir;
                changed = true;
            }
        } else if (!QDir(dir).exists()) {
            inst.dataDir = newDir;
            changed = true;
        }
    }
    if (changed)
        saveToDisk();

    // 清理已空的旧目录
    QDir oldRootDir(oldRoot);
    if (oldRootDir.exists() && oldRootDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
        oldRootDir.rmdir(".");
}

QString BedrockInstanceManager::generateId() const
{
    return QStringLiteral("inst_") + QString::number(QDateTime::currentMSecsSinceEpoch())
        + QStringLiteral("_") + QString::number(QRandomGenerator::global()->bounded(1000, 9999));
}

bool BedrockInstanceManager::loadFromDisk()
{
    m_instances.clear();
    m_activeInstanceId.clear();

    const QString filePath = Platform::getDataDirectory() + QStringLiteral("/bedrock/instances.json");
    if (!QFile::exists(filePath)) {
        QDir().mkpath(Platform::getDataDirectory() + QStringLiteral("/bedrock"));
        saveToDisk();
        return true;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    const QJsonObject root = doc.object();
    m_activeInstanceId = root.value(QStringLiteral("activeInstanceId")).toString();

    const QJsonArray arr = root.value(QStringLiteral("instances")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        BedrockInstance inst;
        inst.id = o.value(QStringLiteral("id")).toString();
        if (inst.id.isEmpty())
            continue;
        inst.name = o.value(QStringLiteral("name")).toString();
        inst.dataDir = o.value(QStringLiteral("dataDir")).toString();
        inst.version = o.value(QStringLiteral("version")).toString();
        inst.createdAt = QDateTime::fromString(o.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
        inst.lastPlayed = QDateTime::fromString(o.value(QStringLiteral("lastPlayed")).toString(), Qt::ISODate);
        m_instances.append(inst);
    }

    // 活动实例不存在时清空
    if (!m_activeInstanceId.isEmpty() && instanceById(m_activeInstanceId).id.isEmpty())
        m_activeInstanceId.clear();
    return true;
}

bool BedrockInstanceManager::saveToDisk()
{
    const QString dir = Platform::getDataDirectory() + QStringLiteral("/bedrock");
    if (!QDir().mkpath(dir))
        return false;

    QJsonObject root;
    root.insert(QStringLiteral("activeInstanceId"), m_activeInstanceId);

    QJsonArray arr;
    for (const BedrockInstance &inst : m_instances) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), inst.id);
        o.insert(QStringLiteral("name"), inst.name);
        o.insert(QStringLiteral("dataDir"), inst.dataDir);
        o.insert(QStringLiteral("version"), inst.version);
        o.insert(QStringLiteral("createdAt"), inst.createdAt.toString(Qt::ISODate));
        o.insert(QStringLiteral("lastPlayed"), inst.lastPlayed.toString(Qt::ISODate));
        arr.append(o);
    }
    root.insert(QStringLiteral("instances"), arr);

    QFile file(dir + QStringLiteral("/instances.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

// ---------- junction 工具 ----------

bool BedrockInstanceManager::createJunction(const QString &linkPath, const QString &targetPath, QString *err)
{
    if (QFileInfo::exists(linkPath)) {
        if (isJunction(linkPath)) {
            if (!removeJunction(linkPath)) {
                if (err)
                    *err = tr("无法移除已存在的数据联接：%1").arg(linkPath);
                return false;
            }
        } else {
            // 绝不静默删除真实数据目录，交由调用方处理迁移
            if (err)
                *err = tr("目标路径已存在且不是数据联接，请先退出基岩版：%1").arg(linkPath);
            return false;
        }
    }
    QDir().mkpath(QFileInfo(linkPath).absolutePath());

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral("cmd.exe"),
               QStringList() << QStringLiteral("/c") << QStringLiteral("mklink")
                             << QStringLiteral("/J") << QDir::toNativeSeparators(linkPath)
                             << QDir::toNativeSeparators(targetPath));
    if (!proc.waitForFinished(15000)) {
        if (err)
            *err = tr("创建数据联接超时");
        return false;
    }
    const QString output = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    if (proc.exitCode() != 0 || !isJunction(linkPath)) {
        if (err)
            *err = output.isEmpty() ? tr("创建数据联接失败") : output;
        return false;
    }
    return true;
}

bool BedrockInstanceManager::isJunction(const QString &path)
{
    if (!QFileInfo::exists(path))
        return false;
    const QFileInfo fi(path);
    if (fi.isSymLink())
        return true;
    // 兜底：解析 dir /AL 输出
    QProcess proc;
    proc.start(QStringLiteral("cmd.exe"),
               QStringList() << QStringLiteral("/c") << QStringLiteral("dir") << QStringLiteral("/AL")
                             << QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
    if (proc.waitForFinished(8000)) {
        const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
        const QString name = QFileInfo(path).fileName();
        for (const QString &line : out.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("<JUNCTION>")) && line.contains(name))
                return true;
        }
    }
    return false;
}

QString BedrockInstanceManager::junctionTarget(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isSymLink()) {
        const QString t = fi.symLinkTarget();
        if (!t.isEmpty())
            return QDir::cleanPath(t);
    }
    // 兜底：解析 dir /AL 输出中的 [目标]
    QProcess proc;
    proc.start(QStringLiteral("cmd.exe"),
               QStringList() << QStringLiteral("/c") << QStringLiteral("dir") << QStringLiteral("/AL")
                             << QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
    if (proc.waitForFinished(8000)) {
        const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
        const QString name = QFileInfo(path).fileName();
        for (const QString &line : out.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("<JUNCTION>")) && line.contains(name)) {
                const int idx = line.indexOf(QLatin1Char('['));
                if (idx >= 0) {
                    QString t = line.mid(idx + 1);
                    if (t.endsWith(QLatin1Char(']')))
                        t.chop(1);
                    return QDir::cleanPath(t);
                }
            }
        }
    }
    return QString();
}

bool BedrockInstanceManager::removeJunction(const QString &path)
{
    // 仅移除联接本身，不触碰目标目录内容
    if (QDir().rmdir(path))
        return true;

    QProcess proc;
    proc.start(QStringLiteral("cmd.exe"),
               QStringList() << QStringLiteral("/c") << QStringLiteral("rmdir")
                             << QDir::toNativeSeparators(path));
    if (!proc.waitForFinished(10000))
        return false;
    return proc.exitCode() == 0 || !QFileInfo::exists(path);
}

bool BedrockInstanceManager::copyRecursively(const QString &srcDir, const QString &dstDir, QString *err)
{
    QDir src(srcDir);
    if (!src.exists())
        return true;
    if (!QDir().mkpath(dstDir)) {
        if (err)
            *err = tr("无法创建目标目录");
        return false;
    }

    // 先建立目录结构
    QDirIterator dirIt(srcDir, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                       QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        dirIt.next();
        const QString rel = src.relativeFilePath(dirIt.filePath());
        if (!QDir().mkpath(dstDir + QLatin1Char('/') + rel)) {
            if (err)
                *err = tr("无法创建目录 %1").arg(rel);
            return false;
        }
    }

    // 再复制文件
    QDirIterator fileIt(srcDir, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
    while (fileIt.hasNext()) {
        fileIt.next();
        const QString rel = src.relativeFilePath(fileIt.filePath());
        if (!QFile::copy(fileIt.filePath(), dstDir + QLatin1Char('/') + rel)) {
            if (err)
                *err = rel;
            return false;
        }
    }
    return true;
}
