#include "utils/MigrationService.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>

#include <utility>

#include "utils/LauncherImporter.h"
#include "utils/SettingsManager.h"

namespace {

QString trMig(const char *text)
{
    return QCoreApplication::translate("MigrationService", text);
}

/** Java 显示名：优先取父目录名，父目录是 bin 时再往上一层 */
QString javaDisplayName(const QString &javaExe)
{
    QDir javaDir = QFileInfo(javaExe).absoluteDir();
    if (javaDir.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0
        || javaDir.dirName().isEmpty()) {
        javaDir.cdUp();
    }
    const QString name = javaDir.dirName();
    return name.isEmpty() ? trMig("Java") : name;
}

/** 字节数的简短显示（磁盘空间提示用） */
QString formatBytes(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return trMig("%1 GB").arg(QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1));
    if (bytes >= 1024LL * 1024)
        return trMig("%1 MB").arg(QString::number(bytes / (1024.0 * 1024), 'f', 1));
    return trMig("%1 KB").arg(QString::number(qMax<qint64>(1, bytes / 1024), 'f', 0));
}

/** 复制目标目录名里不能出现的字符统一换成下划线 */
QString sanitizeDirName(const QString &name)
{
    QString clean = name;
    clean.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    clean = clean.trimmed();
    while (clean.endsWith(QLatin1Char('.')) || clean.endsWith(QLatin1Char(' ')))
        clean.chop(1);
    if (clean.isEmpty())
        clean = QStringLiteral("Migrated");
    return clean;
}

} // namespace

/* ---------------------------------------------------------------- 展示与路径 */

QString MigrationService::displayNameFor(const QString &key)
{
    if (key == QStringLiteral("pcl"))
        return QStringLiteral("PCL");
    if (key == QStringLiteral("hmcl"))
        return QStringLiteral("HMCL");
    if (key == QStringLiteral("multimc"))
        return QStringLiteral("MultiMC");
    if (key == QStringLiteral("baka"))
        return QStringLiteral("BakaXL");
    if (key == QStringLiteral("official"))
        return trMig("官方启动器");
    if (key == QStringLiteral("fcl"))
        return QStringLiteral("FCL");
    if (key == QStringLiteral("zl2"))
        return QStringLiteral("ZL2");
    if (key == QStringLiteral("pojav"))
        return QStringLiteral("PojavLauncher");
    if (key == QStringLiteral("other"))
        return trMig("其他目录");
    if (key == QStringLiteral("custom"))
        return trMig("手动添加");
    return key;
}

QString MigrationService::folderNameFor(const QString &key, const QString &gameDir)
{
    QDir d(gameDir);
    QString leaf = d.dirName();
    if (leaf.startsWith(QLatin1Char('.')) || leaf.isEmpty()) {
        QDir parent(d);
        if (parent.cdUp())
            leaf = parent.dirName();
    }
    if (leaf.isEmpty())
        leaf = displayNameFor(key);
    return trMig("%1 导入 · %2").arg(displayNameFor(key), leaf);
}

QString MigrationService::defaultCopyRoot()
{
    return QDir(SettingsManager::defaultInstanceFolderPath())
        .filePath(QStringLiteral("Migrated"));
}

QString MigrationService::uniqueCopyDestination(const QString &root, const QString &name)
{
    const QString base = sanitizeDirName(name);
    QString candidate = QDir(root).filePath(base);
    int suffix = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = QDir(root).filePath(QStringLiteral("%1 (%2)").arg(base).arg(suffix));
        ++suffix;
    }
    return candidate;
}

int MigrationService::countVersions(const QString &gameDir)
{
    if (gameDir.isEmpty())
        return 0;
    const QDir versionsDir(QDir(gameDir).filePath(QStringLiteral("versions")));
    if (!versionsDir.exists())
        return 0;
    int count = 0;
    const QStringList names =
        versionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        if (QFileInfo::exists(versionsDir.absoluteFilePath(
                name + QLatin1Char('/') + name + QStringLiteral(".json")))) {
            ++count;
        }
    }
    return count;
}

qint64 MigrationService::directorySize(const QString &gameDir)
{
    if (gameDir.isEmpty() || !QDir(gameDir).exists())
        return 0;
    qint64 total = 0;
    QDirIterator it(gameDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

bool MigrationService::isWritableDir(const QString &dir)
{
    if (dir.isEmpty())
        return false;
    if (!QDir().mkpath(dir))
        return false;
    const QString probe =
        QDir(dir).filePath(QStringLiteral(".blockbox_write_test_%1")
                               .arg(QCoreApplication::applicationPid()));
    QFile file(probe);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.close();
    QFile::remove(probe);
    return true;
}

bool MigrationService::canCopyTo(const QString &sourceDir, const QString &destination,
                                 QString *reason)
{
    if (reason)
        reason->clear();

    if (sourceDir.trimmed().isEmpty()) {
        if (reason)
            *reason = trMig("原游戏目录不存在");
        return false;
    }
    if (destination.trimmed().isEmpty()) {
        if (reason)
            *reason = trMig("还没有选择复制位置");
        return false;
    }

    const QString source = QDir(sourceDir).absolutePath();
    const QString dest = QDir(destination).absolutePath();
    if (!QDir(source).exists()) {
        if (reason)
            *reason = trMig("原游戏目录不存在");
        return false;
    }
    if (dest.isEmpty()) {
        if (reason)
            *reason = trMig("还没有选择复制位置");
        return false;
    }

    const QString sourceSlash = source + QLatin1Char('/');
    const QString destSlash = dest + QLatin1Char('/');
    if (dest.compare(source, Qt::CaseInsensitive) == 0
        || dest.startsWith(sourceSlash, Qt::CaseInsensitive)) {
        if (reason)
            *reason = trMig("复制位置不能放在原游戏目录里面");
        return false;
    }
    if (source.startsWith(destSlash, Qt::CaseInsensitive)) {
        if (reason)
            *reason = trMig("复制位置不能是原游戏目录的上级目录");
        return false;
    }

    const QString parent = QFileInfo(dest).absolutePath();
    if (!isWritableDir(parent)) {
        if (reason)
            *reason = trMig("复制位置不可写，请换一个目录");
        return false;
    }

    const QStorageInfo storage(parent);
    if (storage.isValid() && storage.isReady()) {
        const qint64 needed = directorySize(source);
        // 留 64 MB 给版本 JSON / 日志等运行期文件，避免刚好写满
        const qint64 available = storage.bytesAvailable();
        if (needed > 0 && available < needed + 64LL * 1024 * 1024) {
            if (reason) {
                *reason = trMig("磁盘空间不够：需要约 %1，可用 %2")
                              .arg(formatBytes(needed), formatBytes(available));
            }
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- 数据导入 */

void MigrationService::applySettingsAndAccounts(const QString &key,
                                                const MigrationSelection &selection,
                                                MigrationApplyResult &result)
{
    if (key.isEmpty() || key == QStringLiteral("custom"))
        return;

    const LauncherImportResult imported = LauncherImporter::importFrom(key);
    SettingsManager *sm = SettingsManager::instance();

    // ── 账户：同名自动去重，第一个账户在列表为空时设为默认 ──
    if (selection.accounts) {
        const int before = sm->getAccounts().size();
        bool first = (before == 0);
        for (const AccountInfo &acc : imported.accounts) {
            if (acc.username.isEmpty())
                continue;
            if (acc.type.compare(QStringLiteral("microsoft"), Qt::CaseInsensitive) == 0
                && !acc.accessToken.isEmpty()) {
                sm->addMicrosoftAccount(acc.username, acc.accessToken, acc.refreshToken,
                                        acc.uuid, acc.skinUrl, first);
            } else {
                const QString type = acc.type.isEmpty() ? QStringLiteral("offline") : acc.type;
                sm->addAccount(acc.username, type, acc.serverUrl, first);
            }
            first = false;
        }
        result.addedAccounts += qMax(0, sm->getAccounts().size() - before);
    } else if (key == QStringLiteral("baka")) {
        result.notes << trMig("BakaXL 的账户是设备加密的，无法读取；需要的话请在方块盒子账户页重新登录");
    }

    if (key == QStringLiteral("baka") && selection.accounts && imported.accounts.isEmpty()) {
        result.notes << trMig("没有读到 BakaXL 的账户（加密存储），已跳过账户导入");
    }
    const bool androidLauncher = key == QStringLiteral("fcl") || key == QStringLiteral("zl2")
                                 || key == QStringLiteral("pojav")
                                 || key == QStringLiteral("other");
    if (androidLauncher && selection.accounts && imported.accounts.isEmpty()
        && !imported.instanceDirs.isEmpty()) {
        result.notes << trMig("%1 的账户在应用私有目录里，读不到；游戏目录和版本已照常导入")
                            .arg(displayNameFor(key));
    }

    // ── Java：合并去重，保留启动器里登记的全部运行时 ──
    if (selection.java) {
        QList<QPair<QString, QString>> javas = sm->getJavaInstallations();
        const auto hasJava = [&javas](const QString &path) {
            for (const auto &entry : std::as_const(javas)) {
                if (entry.first.compare(path, Qt::CaseInsensitive) == 0)
                    return true;
            }
            return false;
        };
        for (const QString &javaExe : imported.javaPaths) {
            if (javaExe.isEmpty() || !QFileInfo::exists(javaExe) || hasJava(javaExe))
                continue;
            javas.append(qMakePair(javaExe, javaDisplayName(javaExe)));
            ++result.addedJavas;
        }
        if (result.addedJavas > 0)
            sm->setJavaInstallations(javas);
    }

    // ── 第三方登录服务器：按地址去重 ──
    if (selection.authServers) {
        QStringList known;
        for (const AuthServerInfo &server : sm->getAuthServers())
            known << server.url.toLower();
        for (const LauncherAuthServer &server : imported.authServers) {
            if (server.url.isEmpty() || known.contains(server.url.toLower()))
                continue;
            sm->addAuthServer(server.url, server.name);
            known << server.url.toLower();
            ++result.addedAuthServers;
        }
    }

    // ── 下载源 / 下载线程 ──
    if (selection.downloadSettings) {
        if (imported.downloadSource == 0 || imported.downloadSource == 1) {
            const bool bmcl = (imported.downloadSource == 1);
            const DownloadSource source = bmcl ? DownloadSource::BMCL : DownloadSource::Official;
            sm->setDownloadSource(source);
            sm->setForgeDownloadSource(bmcl ? ForgeDownloadSource::BMCL
                                            : ForgeDownloadSource::Official);
            sm->setFabricDownloadSource(bmcl ? FabricDownloadSource::BMCL
                                             : FabricDownloadSource::Official);
            sm->setOptiFineDownloadSource(bmcl ? OptiFineDownloadSource::BMCL
                                               : OptiFineDownloadSource::Official);
            sm->setNeoForgeDownloadSource(bmcl ? NeoForgeDownloadSource::BMCL
                                               : NeoForgeDownloadSource::Official);
            result.downloadApplied = true;
        }
        if (imported.downloadThreads > 0) {
            sm->setDownloadThreadCount(imported.downloadThreads);
            result.downloadApplied = true;
        }
    }

    // ── 内存 ──
    if (selection.memory) {
        if (imported.javaMaxMemoryMb > 0) {
            sm->setMaxMemory(imported.javaMaxMemoryMb);
            result.memoryApplied = true;
        }
        if (imported.javaMinMemoryMb > 0) {
            sm->setMinMemory(imported.javaMinMemoryMb);
            result.memoryApplied = true;
        }
        if (imported.autoMemoryKnown) {
            sm->setAutoMemoryEnabled(imported.autoMemory);
            result.memoryApplied = true;
        }
    }
}

bool MigrationService::registerGameDir(const QString &key, const QString &dir,
                                       const QString &nameSourceDir,
                                       MigrationApplyResult &result)
{
    if (dir.isEmpty())
        return false;
    const QString absDir = QDir(dir).absolutePath();
    if (!QDir(absDir).exists())
        return false;

    SettingsManager *sm = SettingsManager::instance();
    for (const InstanceFolderInfo &folder : sm->getInstanceFolders()) {
        if (QDir(folder.path).absolutePath().compare(absDir, Qt::CaseInsensitive) == 0)
            return false; // 已经登记过
    }

    const QString nameDir = nameSourceDir.isEmpty() ? absDir : nameSourceDir;
    sm->addInstanceFolder(folderNameFor(key, nameDir),
                          QDir::toNativeSeparators(absDir), false);
    ++result.addedFolders;
    result.importedDirs << absDir;
    result.versionCount += countVersions(absDir);
    return true;
}

QStringList MigrationService::verify(const MigrationApplyResult &result,
                                     const QStringList &javaPaths)
{
    QStringList warnings;
    for (const QString &dir : result.importedDirs) {
        if (!QDir(dir).exists()) {
            warnings << trMig("实例目录不见了：%1").arg(QDir::toNativeSeparators(dir));
            continue;
        }
        const int versions = countVersions(dir);
        if (versions <= 0) {
            warnings << trMig("这个目录里没找到可启动的版本：%1")
                            .arg(QDir::toNativeSeparators(dir));
        }
    }
    for (const QString &javaExe : javaPaths) {
        if (!javaExe.isEmpty() && !QFileInfo::exists(javaExe))
            warnings << trMig("Java 文件不存在：%1").arg(QDir::toNativeSeparators(javaExe));
    }
    return warnings;
}

/* ---------------------------------------------------------------- 复制线程 */

GameDirCopyTask::GameDirCopyTask(const QString &sourceDir, const QString &destinationDir,
                                 QObject *parent)
    : QThread(parent)
    , m_sourceDir(sourceDir)
    , m_destinationDir(destinationDir)
    , m_cancel(false)
{
}

GameDirCopyTask::~GameDirCopyTask()
{
    m_cancel.store(true);
    wait();
}

void GameDirCopyTask::cancel()
{
    m_cancel.store(true);
}

void GameDirCopyTask::run()
{
    const QString source = QDir(m_sourceDir).absolutePath();
    const QString destination = QDir(m_destinationDir).absolutePath();

    if (!QDir(source).exists()) {
        emit finishedCopy(destination, trMig("原游戏目录不存在"));
        return;
    }
    if (!QDir().mkpath(destination)) {
        emit finishedCopy(destination, trMig("无法创建复制目录：%1")
                                           .arg(QDir::toNativeSeparators(destination)));
        return;
    }

    // 先枚举一遍：既用于进度总数，也保留空目录（否则复制完会少几个空文件夹）
    QStringList directories;
    QStringList files;
    qint64 totalBytes = 0;
    QDirIterator it(source, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (m_cancel.load())
            break;
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink())
            continue;
        if (info.isDir())
            directories << info.absoluteFilePath();
        else if (info.isFile()) {
            files << info.absoluteFilePath();
            totalBytes += info.size();
        }
    }

    if (m_cancel.load()) {
        QDir(destination).removeRecursively();
        emit finishedCopy(destination, trMig("已取消"));
        return;
    }

    for (const QString &dir : std::as_const(directories)) {
        if (m_cancel.load())
            break;
        const QString rel = QDir(source).relativeFilePath(dir);
        if (!rel.isEmpty())
            QDir().mkpath(QDir(destination).filePath(rel));
    }

    qint64 copiedBytes = 0;
    QString error;
    QElapsedTimer emitTimer;
    emitTimer.start();
    for (int i = 0; i < files.size(); ++i) {
        if (m_cancel.load()) {
            error = trMig("已取消");
            break;
        }
        const QString file = files.at(i);
        const QString rel = QDir(source).relativeFilePath(file);
        const QString target = QDir(destination).filePath(rel);
        QDir().mkpath(QFileInfo(target).absolutePath());

        bool copied = QFile::copy(file, target);
        if (!copied && QFileInfo::exists(target)) {
            // 上次中断留下的半截文件：删掉再试一次
            QFile::remove(target);
            copied = QFile::copy(file, target);
        }
        if (!copied) {
            error = trMig("复制失败：%1").arg(QDir::toNativeSeparators(rel));
            break;
        }
        copiedBytes += QFileInfo(file).size();
        if (emitTimer.elapsed() > 120 || i == files.size() - 1) {
            emit progress(copiedBytes, totalBytes, rel);
            emitTimer.restart();
        }
    }

    if (!error.isEmpty()) {
        // 只删我们刚创建的这份副本；目标目录由调用方用 uniqueCopyDestination 保证是新建的
        QDir(destination).removeRecursively();
        emit finishedCopy(destination, error);
        return;
    }

    emit progress(totalBytes, totalBytes, QString());
    emit finishedCopy(destination, QString());
}
