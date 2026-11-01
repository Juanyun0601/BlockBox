#include "utils/LauncherImporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#include <algorithm>

namespace {

/** 单个启动器的探测结果（内部使用，不对外暴露） */
struct ProbeResult {
    QString key;
    QString displayName;
    bool found = false;          ///< 是否检测到安装痕迹
    QString dataDir;             ///< 主数据目录
    QStringList dataDirs;        ///< 全部命中的数据目录
    QStringList gameDirs;        ///< 有效游戏目录（含 versions）
    QList<AccountInfo> accounts; ///< 可导入账户（已过滤掉无令牌的微软账户）
    QString javaPath;            ///< 启动器里配置的 Java（首选）
    QStringList javaPaths;       ///< 启动器里登记的全部 Java
    QList<LauncherAuthServer> authServers; ///< 第三方登录服务器
    int downloadThreads = -1;    ///< 下载线程数
    int downloadSource = -1;     ///< 0 官方 / 1 BMCL 镜像 / -1 未知
    bool autoMemory = false;     ///< 是否自动分配内存
    bool autoMemoryKnown = false;///< 是否读到了分配方式
    int maxMemoryMb = -1;        ///< 最大内存（MB）
    int minMemoryMb = -1;        ///< 最小内存（MB）
    QStringList notes;           ///< 给用户看的补充说明（中文）
};

QString trImp(const char *text)
{
    return QCoreApplication::translate("LauncherImporter", text);
}

/** Roaming 数据目录（Windows = %APPDATA%）。
 *  支持 BLOCKBOX_APPDATA 覆盖，方便自动化测试指向假数据目录。 */
QString roamingAppData()
{
    const QByteArray overrideEnv = qgetenv("BLOCKBOX_APPDATA");
    if (!overrideEnv.isEmpty())
        return QString::fromLocal8Bit(overrideEnv);
#ifdef Q_OS_WIN
    const QString native = qEnvironmentVariable("APPDATA");
    if (!native.isEmpty())
        return native;
#endif
    const QString genericConfig = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!genericConfig.isEmpty())
        return genericConfig;
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

QString localAppData()
{
#ifdef Q_OS_WIN
    const QString native = qEnvironmentVariable("LOCALAPPDATA");
    if (!native.isEmpty())
        return native;
#endif
    return QString();
}

/** 安卓共享存储根（/storage/emulated/0）。
 *  BLOCKBOX_ANDROID_STORAGE 可覆盖：桌面版据此模拟安卓目录，方便探针测试导入逻辑。 */
QString androidExternalRoot()
{
    const QByteArray overrideEnv = qgetenv("BLOCKBOX_ANDROID_STORAGE");
    if (!overrideEnv.isEmpty())
        return QDir::cleanPath(QString::fromLocal8Bit(overrideEnv));
#ifdef Q_OS_ANDROID
    // 首选从 Qt 给的外部目录里截共享存储根（.../Android/data/<包名>/files → ...），
    // 截不出来时退回标准挂载点（与 JavaScanWorker 的移动端扫描根一致）
    QStringList candidates;
    const QString externalData =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const int androidIndex = externalData.indexOf(QStringLiteral("/Android/"));
    if (androidIndex > 0)
        candidates << externalData.left(androidIndex);
    candidates << QStringLiteral("/storage/emulated/0") << QStringLiteral("/sdcard");
    for (const QString &candidate : std::as_const(candidates)) {
        if (QDir(candidate).exists())
            return candidate;
    }
    return candidates.first();
#else
    return QString();
#endif
}

QString cleanDir(const QString &path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}

bool samePath(const QString &a, const QString &b)
{
    return cleanDir(a).compare(cleanDir(b), Qt::CaseInsensitive) == 0;
}

/** 目录是否像 Minecraft 游戏目录（存在 versions 子目录且里面有内容） */
bool looksLikeMinecraftDir(const QString &dir)
{
    if (dir.isEmpty())
        return false;
    QDir d(dir);
    if (!d.exists())
        return false;
    QDir v(dir + QStringLiteral("/versions"));
    return v.exists() && !v.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
}

/** 把候选目录加进列表：必须是有效游戏目录，且去重（Windows 不分大小写） */
void addGameDir(QStringList &dirs, const QString &candidate)
{
    if (candidate.isEmpty() || !looksLikeMinecraftDir(candidate))
        return;
    const QString normalized = QDir::toNativeSeparators(cleanDir(candidate));
    for (const QString &existing : dirs) {
        if (samePath(existing, normalized))
            return;
    }
    dirs.append(normalized);
}

void addDataDir(QStringList &dirs, const QString &candidate)
{
    if (candidate.isEmpty() || !QDir(candidate).exists())
        return;
    for (const QString &existing : dirs) {
        if (samePath(existing, candidate))
            return;
    }
    dirs.append(QDir::toNativeSeparators(cleanDir(candidate)));
}

QJsonDocument readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QJsonDocument();
    const QByteArray raw = f.readAll();
    f.close();
    return QJsonDocument::fromJson(raw);
}

/** PCL 的 setup.ini / config.v1.yml 是「键: 值」写法（不是标准 INI），单独读一下 */
QString readKeyValueFile(const QString &path, const QString &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int sep = line.indexOf(QLatin1Char(':'));
        if (sep <= 0)
            continue;
        if (line.left(sep).trimmed().compare(key, Qt::CaseInsensitive) != 0)
            continue;
        return line.mid(sep + 1).trimmed();
    }
    return QString();
}

/** 把 java.exe 登记进探测结果（存在才加；同一路径只记一次） */
void addJavaPath(ProbeResult &p, const QString &javaExe)
{
    if (javaExe.isEmpty() || !QFileInfo::exists(javaExe))
        return;
    const QString normalized = QDir::toNativeSeparators(cleanDir(javaExe));
    for (const QString &existing : std::as_const(p.javaPaths)) {
        if (samePath(existing, normalized))
            return;
    }
    p.javaPaths.append(normalized);
    if (p.javaPath.isEmpty())
        p.javaPath = normalized;
}

/** 启动器里记的是 Java 的 bin 目录时，补上 java.exe */
QString javaExeInFolder(const QString &folder)
{
    if (folder.isEmpty())
        return QString();
    QString path = QDir::fromNativeSeparators(folder);
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (path.endsWith(QStringLiteral("/java.exe"), Qt::CaseInsensitive)
        || path.endsWith(QStringLiteral("/javaw.exe"), Qt::CaseInsensitive))
        return QDir::toNativeSeparators(path);
    return QDir::toNativeSeparators(path + QStringLiteral("/java.exe"));
}

/** 登录服务器显示名：优先用启动器里记的服务器名，否则取域名 */
QString authServerDisplayName(const QString &url, const QString &preferred)
{
    if (!preferred.trimmed().isEmpty())
        return preferred.trimmed();
    const QUrl parsed(url);
    QString host = parsed.host();
    if (host.isEmpty()) {
        // 没有协议头时手工截一下
        QString s = url;
        s.remove(QRegularExpression(QStringLiteral("^[a-zA-Z]+://")));
        host = s.section(QLatin1Char('/'), 0, 0);
    }
    if (host.startsWith(QStringLiteral("www.")))
        host = host.mid(4);
    return host.isEmpty() ? url : host;
}

void addAuthServer(ProbeResult &p, const QString &url, const QString &preferredName)
{
    QString normalized = url.trimmed();
    if (normalized.isEmpty())
        return;
    for (const LauncherAuthServer &server : std::as_const(p.authServers)) {
        if (server.url.compare(normalized, Qt::CaseInsensitive) == 0)
            return;
    }
    p.authServers.append({ normalized, authServerDisplayName(normalized, preferredName) });
}

/** 收集 .minecraft/versions 下的版本名（只算带 <名>.json 的目录） */
QStringList versionsIn(const QString &minecraftDir)
{
    QStringList result;
    if (minecraftDir.isEmpty())
        return result;
    const QDir vDir(minecraftDir + QStringLiteral("/versions"));
    if (!vDir.exists())
        return result;
    const QStringList names = vDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        if (QFileInfo::exists(vDir.absoluteFilePath(name + QLatin1Char('/') + name
                                                   + QStringLiteral(".json"))))
            result.append(name);
    }
    return result;
}

/** 系统目录：扫描游戏目录时跳过，避免浪费时间 */
bool isSystemDirName(const QString &name)
{
    static const QSet<QString> skip = {
        QStringLiteral("windows"),
        QStringLiteral("program files"),
        QStringLiteral("program files (x86)"),
        QStringLiteral("programdata"),
        QStringLiteral("$recycle.bin"),
        QStringLiteral("system volume information"),
        QStringLiteral("appdata"),
        QStringLiteral("application data"),
        QStringLiteral("users"),
        QStringLiteral("documents and settings"),
        QStringLiteral("$windows.~bt"),
        QStringLiteral("$windows.~ws"),
        QStringLiteral("config.msi"),
        QStringLiteral("perflogs"),
        QStringLiteral("recovery"),
        QStringLiteral("msocache"),
        QStringLiteral("node_modules"),
        QStringLiteral("onedrive"),
        QStringLiteral("intel"),
        QStringLiteral("amd"),
        QStringLiteral("nvidia"),
    };
    return skip.contains(name.toLower());
}

/** 扫描磁盘上常见的 .minecraft 目录（深度 2），供 PCL 便携版等没有配置指针的启动器使用。
 *  结果缓存 30 秒，避免引导页反复探测时重复遍历磁盘。 */
QStringList scannedMinecraftDirs()
{
    static QStringList cache;
    static QElapsedTimer stamp;

    QStringList dirs;
    addGameDir(dirs, roamingAppData() + QStringLiteral("/.minecraft"));
    addGameDir(dirs, QDir::homePath() + QStringLiteral("/.minecraft"));
    const QString local = localAppData();
    if (!local.isEmpty())
        addGameDir(dirs, local + QStringLiteral("/.minecraft"));
    // 便携启动器常见布局：<启动器目录>/.minecraft
    addGameDir(dirs, roamingAppData() + QStringLiteral("/PCL/.minecraft"));
    addGameDir(dirs, roamingAppData() + QStringLiteral("/.hmcl/.minecraft"));

    if (stamp.isValid() && stamp.elapsed() < 30000) {
        for (const QString &d : cache)
            addGameDir(dirs, d);
        return dirs;
    }

#ifdef Q_OS_WIN
    const QFileInfoList drives = QDir::drives();
    for (const QFileInfo &driveInfo : drives) {
        const QString root = driveInfo.absoluteFilePath();
        QDir rootDir(root);
        if (!rootDir.exists())
            continue;
        const QStringList firstLevel =
            rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
        int visitedFirst = 0;
        for (const QString &a : firstLevel) {
            if (++visitedFirst > 300)
                break;
            if (isSystemDirName(a))
                continue;
            const QString pa = root + a;
            addGameDir(dirs, pa + QStringLiteral("/.minecraft"));
            if (!QDir(pa).exists())
                continue;
            const QStringList secondLevel =
                QDir(pa).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
            int visitedSecond = 0;
            for (const QString &b : secondLevel) {
                if (++visitedSecond > 150)
                    break;
                if (isSystemDirName(b))
                    continue;
                addGameDir(dirs, pa + QLatin1Char('/') + b + QStringLiteral("/.minecraft"));
            }
        }
    }
#endif

    cache = dirs;
    stamp.start();
    return dirs;
}

/** 全部已知的游戏目录（标准位置 + 磁盘扫描） */
QStringList allMinecraftDirs()
{
    QStringList dirs;
    for (const QString &d : scannedMinecraftDirs())
        addGameDir(dirs, d);
    return dirs;
}

/* ---------------------------------------------------------------- 账户解析 */

/** 把「读得到但没法直接用」的微软账户降级为离线账户（否则导入后启动必失败） */
void sanitizeAccounts(ProbeResult &p)
{
    int degraded = 0;
    for (AccountInfo &acc : p.accounts) {
        if (acc.type == QStringLiteral("microsoft") && acc.accessToken.isEmpty()) {
            acc.type = QStringLiteral("offline");
            ++degraded;
        }
    }
    if (degraded > 0) {
        p.notes << trImp("%1 个微软账户无可用令牌，已按离线账户导入")
                       .arg(degraded);
    }
}

/** PCL2 / PCL CE：游戏目录下 accounts.json
 *  [{ "Name": "SeaYu", "TokenType": "offline", "IsOffline": true, "AuthServer": null }] */
QList<AccountInfo> parsePclAccounts(const QString &path)
{
    QList<AccountInfo> result;
    const QJsonDocument doc = readJson(path);
    QJsonArray arr;
    if (doc.isArray())
        arr = doc.array();
    else if (doc.isObject())
        arr = doc.object().value(QStringLiteral("accounts")).toArray();

    for (const QJsonValue &value : std::as_const(arr)) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("Name")).toString();
        if (name.isEmpty())
            continue;
        const QString tokenType = obj.value(QStringLiteral("TokenType")).toString();
        const bool offline = obj.value(QStringLiteral("IsOffline")).toBool(true)
                             || tokenType.compare(QStringLiteral("offline"), Qt::CaseInsensitive) == 0
                             || tokenType.isEmpty();

        AccountInfo acc;
        acc.username = name;
        acc.type = offline ? QStringLiteral("offline") : QStringLiteral("microsoft");
        acc.uuid = obj.value(QStringLiteral("Id")).toString();
        if (!offline) {
            acc.accessToken = obj.value(QStringLiteral("AccessToken")).toString();
            acc.refreshToken = obj.value(QStringLiteral("RefreshToken")).toString();
            // PCL 把令牌 DPAPI 加密成 "ENC:xxx"，方块盒子无法解密
            if (acc.accessToken.startsWith(QStringLiteral("ENC:"))) {
                acc.accessToken.clear();
                acc.refreshToken.clear();
            }
        }
        const QString server = obj.value(QStringLiteral("AuthServer")).toString();
        if (!server.isEmpty()) {
            acc.type = QStringLiteral("offline"); // 第三方登录服：按离线 + 服务器地址处理
            acc.serverUrl = server;
        }
        acc.isDefault = obj.value(QStringLiteral("IsActive")).toBool(false);
        result.append(acc);
    }
    return result;
}

/** HMCL 账户文件（新版 user-accounts.json / 旧版 accounts.json）
 *  数组：[{type, profileName|username, profileID|uuid, skin}]
 *  包装：{ "accounts": [ ... ] } */
QList<AccountInfo> parseHmclAccounts(const QString &path)
{
    QList<AccountInfo> result;
    const QJsonDocument doc = readJson(path);
    QJsonArray arr;
    if (doc.isArray())
        arr = doc.array();
    else if (doc.isObject())
        arr = doc.object().value(QStringLiteral("accounts")).toArray();

    for (const QJsonValue &value : std::as_const(arr)) {
        const QJsonObject obj = value.toObject();
        QString name = obj.value(QStringLiteral("profileName")).toString();
        if (name.isEmpty())
            name = obj.value(QStringLiteral("username")).toString();
        if (name.isEmpty())
            name = obj.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;

        const QString type = obj.value(QStringLiteral("type")).toString();
        AccountInfo acc;
        acc.username = name;
        acc.uuid = obj.value(QStringLiteral("profileID")).toString();
        if (acc.uuid.isEmpty())
            acc.uuid = obj.value(QStringLiteral("uuid")).toString();

        if (type.compare(QStringLiteral("offline"), Qt::CaseInsensitive) == 0) {
            acc.type = QStringLiteral("offline");
        } else if (type.contains(QStringLiteral("authlib"), Qt::CaseInsensitive)
                   || type.compare(QStringLiteral("yggdrasil"), Qt::CaseInsensitive) == 0) {
            acc.type = QStringLiteral("offline");
            acc.serverUrl = obj.value(QStringLiteral("apiRoot")).toString();
            if (acc.serverUrl.isEmpty())
                acc.serverUrl = obj.value(QStringLiteral("server")).toString();
        } else {
            acc.type = QStringLiteral("microsoft");
            acc.accessToken = obj.value(QStringLiteral("accessToken")).toString();
            acc.refreshToken = obj.value(QStringLiteral("refreshToken")).toString();
        }
        result.append(acc);
    }
    return result;
}

/** MultiMC / Prism：{ "accounts": [ { "type": "MSA"|"Offline"|"Yggdrasil", "username", "uuid" } ] } */
QList<AccountInfo> parseMultiMcAccounts(const QString &path)
{
    QList<AccountInfo> result;
    const QJsonDocument doc = readJson(path);
    if (!doc.isObject())
        return result;
    const QJsonArray arr = doc.object().value(QStringLiteral("accounts")).toArray();
    for (const QJsonValue &value : std::as_const(arr)) {
        const QJsonObject obj = value.toObject();
        const QString type = obj.value(QStringLiteral("type")).toString();
        const QString name = obj.value(QStringLiteral("username")).toString();
        if (name.isEmpty())
            continue;
        AccountInfo acc;
        acc.username = name;
        acc.uuid = obj.value(QStringLiteral("uuid")).toString();
        if (type.compare(QStringLiteral("Offline"), Qt::CaseInsensitive) == 0) {
            acc.type = QStringLiteral("offline");
        } else {
            acc.type = QStringLiteral("microsoft");
            acc.accessToken = obj.value(QStringLiteral("accessToken")).toString();
            acc.refreshToken = obj.value(QStringLiteral("refreshToken")).toString();
        }
        result.append(acc);
    }
    return result;
}

/** 官方启动器：launcher_accounts.json（新版）/ launcher_profiles.json（旧版） */
QList<AccountInfo> parseOfficialAccounts(const QString &path)
{
    QList<AccountInfo> result;
    const QJsonDocument doc = readJson(path);
    if (!doc.isObject())
        return result;
    const QJsonObject root = doc.object();
    const bool isLauncherAccounts =
        QFileInfo(path).fileName().compare(QStringLiteral("launcher_accounts.json"), Qt::CaseInsensitive) == 0;

    if (isLauncherAccounts) {
        const QJsonObject accounts = root.value(QStringLiteral("accounts")).toObject();
        const QString activeId = root.value(QStringLiteral("activeAccountLocalId")).toString();
        for (auto it = accounts.constBegin(); it != accounts.constEnd(); ++it) {
            const QJsonObject obj = it.value().toObject();
            AccountInfo acc;
            acc.type = QStringLiteral("microsoft");
            acc.uuid = it.key();
            acc.username = obj.value(QStringLiteral("username")).toString();
            acc.accessToken = obj.value(QStringLiteral("accessToken")).toString();
            const QString profileName = obj.value(QStringLiteral("minecraftProfile"))
                                            .toObject()
                                            .value(QStringLiteral("name"))
                                            .toString();
            if (!profileName.isEmpty())
                acc.username = profileName;
            if (acc.username.isEmpty())
                continue;
            acc.isDefault = (it.key() == activeId);
            result.append(acc);
        }
        return result;
    }

    // launcher_profiles.json：只有带 authenticationDatabase 才说明存了账户
    const QJsonObject authDb = root.value(QStringLiteral("authenticationDatabase")).toObject();
    const QJsonObject selUser = root.value(QStringLiteral("selectedUser")).toObject();
    const QString selAccount = selUser.value(QStringLiteral("account")).toString();
    for (auto it = authDb.constBegin(); it != authDb.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        AccountInfo acc;
        acc.type = QStringLiteral("microsoft");
        acc.uuid = it.key();
        acc.username = obj.value(QStringLiteral("username")).toString();
        acc.accessToken = obj.value(QStringLiteral("accessToken")).toString();
        const QJsonObject profiles = obj.value(QStringLiteral("profiles")).toObject();
        if (!profiles.isEmpty()) {
            const QString profileName = profiles.constBegin().value().toObject()
                                            .value(QStringLiteral("name")).toString();
            if (!profileName.isEmpty())
                acc.username = profileName;
        }
        if (acc.username.isEmpty())
            continue;
        acc.isDefault = (it.key() == selAccount);
        result.append(acc);
    }
    return result;
}

/** 从游戏目录里再捞一遍账户：用户把电脑上的 .minecraft 整份拷过来、或手动搬过账户时，
 *  目录里会带着 PCL 的 accounts.json 或官方启动器的 launcher_accounts.json，
 *  读到就一起导入（同名只留一个）。安卓和桌面都适用。 */
void mergeAccountsFromGameDirs(ProbeResult &p)
{
    for (const QString &gameDir : std::as_const(p.gameDirs)) {
        if (p.accounts.size() >= 32)
            break;
        const struct {
            const char *file;
            bool official;
        } candidates[] = {
            { "/accounts.json", false },
            { "/launcher_accounts.json", true },
        };
        for (const auto &candidate : candidates) {
            const QString path = gameDir + QLatin1String(candidate.file);
            if (!QFileInfo::exists(path))
                continue;
            const QList<AccountInfo> parsed =
                candidate.official ? parseOfficialAccounts(path) : parsePclAccounts(path);
            for (const AccountInfo &acc : parsed) {
                if (acc.username.isEmpty())
                    continue;
                bool duplicate = false;
                for (const AccountInfo &existing : std::as_const(p.accounts)) {
                    if (existing.username.compare(acc.username, Qt::CaseInsensitive) == 0) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                    p.accounts.append(acc);
            }
        }
    }
}

/* ---------------------------------------------------------------- 各启动器探测 */

/** HMCL：数据目录 -> 游戏目录 / 账户 / Java / 内存（兼容新版 config 子目录与旧版 hmcl.json） */
void probeHmclDataDir(ProbeResult &p, const QString &dataDir)
{
    // 便携版 .hmcl 里的相对路径以启动器目录（.hmcl 的父目录）为基准
    QDir baseDir(dataDir);
    if (baseDir.dirName().startsWith(QLatin1Char('.')) && baseDir.cdUp())
        ;
    else
        baseDir = QDir(dataDir);

    // 游戏目录：新版 user-game-directories.json / game-directories.json
    const QStringList dirFiles = {
        QStringLiteral("config/user-game-directories.json"),
        QStringLiteral("config/game-directories.json"),
        QStringLiteral("game-directories.json"),
    };
    for (const QString &rel : dirFiles) {
        const QJsonDocument doc = readJson(dataDir + QLatin1Char('/') + rel);
        if (!doc.isObject())
            continue;
        const QJsonArray arr = doc.object().value(QStringLiteral("directories")).toArray();
        for (const QJsonValue &value : std::as_const(arr)) {
            QString path = value.toObject().value(QStringLiteral("path")).toString();
            if (path.isEmpty())
                continue;
            path = QDir::fromNativeSeparators(path);
            if (!QDir::isAbsolutePath(path))
                path = baseDir.absoluteFilePath(path);
            addGameDir(p.gameDirs, path);
        }
    }

    // 游戏目录：新版 launcher-settings.json 的 commonDirectory
    const QJsonObject launcherSettings =
        readJson(dataDir + QStringLiteral("/config/launcher-settings.json")).object();
    const QString commonDir = launcherSettings.value(QStringLiteral("commonDirectory")).toString();
    if (!commonDir.isEmpty())
        addGameDir(p.gameDirs, commonDir);

    // 游戏目录 / Java / 内存 / 下载设置 / 第三方登录服：旧版 hmcl.json
    const QJsonObject legacy = readJson(dataDir + QStringLiteral("/hmcl.json")).object();
    if (!legacy.isEmpty()) {
        const QString commonPath = legacy.value(QStringLiteral("commonpath")).toString();
        if (!commonPath.isEmpty())
            addGameDir(p.gameDirs, commonPath);
        const QJsonObject configurations = legacy.value(QStringLiteral("configurations")).toObject();
        for (auto it = configurations.constBegin(); it != configurations.constEnd(); ++it) {
            QString gameDir = it.value().toObject().value(QStringLiteral("gameDir")).toString();
            if (gameDir.isEmpty())
                continue;
            if (!QDir::isAbsolutePath(gameDir))
                gameDir = baseDir.absoluteFilePath(gameDir);
            addGameDir(p.gameDirs, gameDir);
        }
        for (const char *key : { "javaPath", "java" })
            addJavaPath(p, legacy.value(QLatin1String(key)).toString());
        if (p.maxMemoryMb <= 0) {
            const int maxMemory = legacy.value(QStringLiteral("maxMemory")).toInt(-1);
            if (maxMemory > 0)
                p.maxMemoryMb = maxMemory;
        }
        if (p.downloadThreads <= 0) {
            const int threads = legacy.value(QStringLiteral("downloadThreads")).toInt(-1);
            if (threads > 0)
                p.downloadThreads = threads;
        }
        if (p.downloadSource < 0) {
            const QString type = legacy.value(QStringLiteral("downloadType")).toString().toLower();
            if (type.contains(QStringLiteral("bmcl")))
                p.downloadSource = 1;
            else if (type.contains(QStringLiteral("official")) || type.contains(QStringLiteral("mojang")))
                p.downloadSource = 0;
        }
        // 内存分配方式（自动 / 手动）
        const QJsonObject global = configurations.value(QStringLiteral("Default"))
                                       .toObject()
                                       .value(QStringLiteral("global"))
                                       .toObject();
        if (global.contains(QStringLiteral("autoMemory"))) {
            p.autoMemoryKnown = true;
            p.autoMemory = global.value(QStringLiteral("autoMemory")).toBool();
        }
        // 第三方登录服务器（metadataResponse 里带服务器名）
        const QJsonArray authlib = legacy.value(QStringLiteral("authlibInjectorServers")).toArray();
        for (const QJsonValue &value : authlib) {
            const QJsonObject entry = value.toObject();
            QString name;
            const QString metadata = entry.value(QStringLiteral("metadataResponse")).toString();
            if (!metadata.isEmpty()) {
                name = QJsonDocument::fromJson(metadata.toUtf8())
                           .object()
                           .value(QStringLiteral("meta"))
                           .toObject()
                           .value(QStringLiteral("serverName"))
                           .toString();
            }
            addAuthServer(p, entry.value(QStringLiteral("url")).toString(), name);
        }
    }

    // Java：新版 user-settings.json 的 userJava 列表（全部导入，不只是第一个）
    const QJsonArray userJava = readJson(dataDir + QStringLiteral("/config/user-settings.json"))
                                    .object()
                                    .value(QStringLiteral("userJava"))
                                    .toArray();
    for (const QJsonValue &value : std::as_const(userJava))
        addJavaPath(p, value.toString());

    // 内存 / 单独 Java：新版 game-settings.json 的 presets
    const QJsonArray presets = readJson(dataDir + QStringLiteral("/config/game-settings.json"))
                                   .object()
                                   .value(QStringLiteral("presets"))
                                   .toArray();
    for (const QJsonValue &value : presets) {
        const QJsonObject preset = value.toObject();
        if (p.maxMemoryMb <= 0) {
            const int maxMemory = preset.value(QStringLiteral("maxMemory")).toInt(-1);
            if (maxMemory > 0)
                p.maxMemoryMb = maxMemory;
        }
        addJavaPath(p, preset.value(QStringLiteral("customJavaPath")).toString());
    }

    // 下载线程 / 下载源：新版 launcher-settings.json
    if (p.downloadThreads <= 0) {
        const int threads = launcherSettings.value(QStringLiteral("downloadThreads")).toInt(-1);
        if (threads > 0)
            p.downloadThreads = threads;
    }
    if (p.downloadSource < 0) {
        const QString source = launcherSettings.value(QStringLiteral("fileDownloadSource")).toString().toLower();
        if (source.contains(QStringLiteral("bmcl")))
            p.downloadSource = 1;
        else if (source.contains(QStringLiteral("official")) || source.contains(QStringLiteral("mojang")))
            p.downloadSource = 0;
    }

    // 第三方登录服务器：新版 authlib-injector-servers.json
    const QStringList authFiles = {
        QStringLiteral("config/authlib-injector-servers.json"),
        QStringLiteral("authlib-injector-servers.json"),
    };
    for (const QString &rel : authFiles) {
        const QJsonArray servers =
            readJson(dataDir + QLatin1Char('/') + rel).object().value(QStringLiteral("servers")).toArray();
        for (const QJsonValue &value : std::as_const(servers)) {
            const QJsonObject entry = value.toObject();
            QString name = entry.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                name = entry.value(QStringLiteral("alias")).toString();
            addAuthServer(p, entry.value(QStringLiteral("url")).toString(), name);
        }
    }

    // 账户：新版 user-accounts.json / 便携版 accounts.json / 旧版 accounts.json
    const QStringList accountFiles = {
        QStringLiteral("config/user-accounts.json"),
        QStringLiteral("config/accounts.json"),
        QStringLiteral("accounts.json"),
    };
    for (const QString &rel : accountFiles) {
        if (!QFileInfo::exists(dataDir + QLatin1Char('/') + rel))
            continue;
        const QList<AccountInfo> accounts = parseHmclAccounts(dataDir + QLatin1Char('/') + rel);
        if (!accounts.isEmpty()) {
            p.accounts = accounts;
            break;
        }
    }
}

ProbeResult probeHmcl()
{
    ProbeResult p;
    p.key = QStringLiteral("hmcl");
    p.displayName = QStringLiteral("HMCL");

    const QString roaming = roamingAppData();
    QStringList candidates = {
        roaming + QStringLiteral("/.hmcl"),
        roaming + QStringLiteral("/hmcl"),
        roaming + QStringLiteral("/HMCL"),
        QDir::homePath() + QStringLiteral("/.hmcl"),
    };
    // 便携版：游戏目录同级的 .hmcl（例如 D:\Minecraft\.hmcl）
    for (const QString &gameDir : allMinecraftDirs()) {
        QDir parent(gameDir);
        if (parent.cdUp())
            candidates << parent.filePath(QStringLiteral(".hmcl"));
    }

    for (const QString &candidate : std::as_const(candidates))
        addDataDir(p.dataDirs, candidate);
    p.dataDir = p.dataDirs.value(0);

    for (const QString &dataDir : std::as_const(p.dataDirs))
        probeHmclDataDir(p, dataDir);

    // 兜底：HMCL 默认游戏目录就是官方 .minecraft
    if (p.gameDirs.isEmpty()) {
        addGameDir(p.gameDirs, roaming + QStringLiteral("/.minecraft"));
        addGameDir(p.gameDirs, QDir::homePath() + QStringLiteral("/.minecraft"));
    }

    p.found = !p.dataDirs.isEmpty() || !p.gameDirs.isEmpty();
    mergeAccountsFromGameDirs(p); // 顺带带走游戏目录里 PCL / 官方格式的账户文件
    sanitizeAccounts(p);
    return p;
}

ProbeResult probePcl()
{
    ProbeResult p;
    p.key = QStringLiteral("pcl");
    p.displayName = QStringLiteral("PCL");

    const QString roaming = roamingAppData();
    const QStringList dataCandidates = {
        roaming + QStringLiteral("/PCL"),
        roaming + QStringLiteral("/PCL/PCL"),
    };
    for (const QString &candidate : dataCandidates)
        addDataDir(p.dataDirs, candidate);
    p.dataDir = p.dataDirs.value(0);

    // Java：config.json 的 JavaList（Folder 指向 bin 目录，全部导入，主版本最高的排最前）
    QList<QPair<int, QString>> pclJavas; // (主版本, java.exe)
    for (const QString &dataDir : std::as_const(p.dataDirs)) {
        const QJsonArray javaList = readJson(dataDir + QStringLiteral("/config.json"))
                                        .object()
                                        .value(QStringLiteral("JavaList"))
                                        .toArray();
        for (const QJsonValue &value : javaList) {
            const QJsonObject entry = value.toObject();
            const QString exe = javaExeInFolder(entry.value(QStringLiteral("Folder")).toString());
            if (exe.isEmpty() || !QFileInfo::exists(exe))
                continue;
            const int major = entry.value(QStringLiteral("Version"))
                                  .toObject()
                                  .value(QStringLiteral("Major"))
                                  .toInt(0);
            pclJavas.append(qMakePair(major, exe));
        }
        if (!pclJavas.isEmpty())
            break;
    }
    std::sort(pclJavas.begin(), pclJavas.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) { return a.first > b.first; });
    for (const QPair<int, QString> &java : std::as_const(pclJavas))
        addJavaPath(p, java.second);

    // 游戏目录：带 PCL.ini 或 PCL 账户文件的就是 PCL 用过的目录
    for (const QString &gameDir : allMinecraftDirs()) {
        if (QFileInfo::exists(gameDir + QStringLiteral("/PCL.ini"))
            || QFileInfo::exists(gameDir + QStringLiteral("/accounts.json")))
            addGameDir(p.gameDirs, gameDir);
    }
    addGameDir(p.gameDirs, roaming + QStringLiteral("/.minecraft"));
    addGameDir(p.gameDirs, roaming + QStringLiteral("/PCL/.minecraft"));

    // 账户：PCL2 存的离线账户在游戏目录 accounts.json
    for (const QString &gameDir : std::as_const(p.gameDirs)) {
        const QList<AccountInfo> accounts = parsePclAccounts(gameDir + QStringLiteral("/accounts.json"));
        if (!accounts.isEmpty()) {
            p.accounts = accounts;
            break;
        }
    }
    if (p.accounts.isEmpty())
        p.accounts = parsePclAccounts(roaming + QStringLiteral("/PCL/accounts.json"));

    // 内存分配方式：PCL 便携版目录里的 config.v1.yml / setup.ini（LaunchRamType 0 = 自动分配）
    for (const QString &gameDir : std::as_const(p.gameDirs)) {
        QDir parent(gameDir);
        if (!parent.cdUp())
            continue;
        const QString pclDir = parent.absoluteFilePath(QStringLiteral("PCL"));
        for (const char *fileName : { "config.v1.yml", "setup.ini" }) {
            const QString path = pclDir + QLatin1Char('/') + QLatin1String(fileName);
            const QString ramType = readKeyValueFile(path, QStringLiteral("LaunchRamType"));
            if (ramType.isEmpty())
                continue;
            p.autoMemoryKnown = true;
            p.autoMemory = (ramType == QStringLiteral("0"));
            break;
        }
        if (p.autoMemoryKnown)
            break;
    }

    p.found = !p.dataDirs.isEmpty() || !p.gameDirs.isEmpty();
    mergeAccountsFromGameDirs(p); // 顺带带走游戏目录里 PCL / 官方格式的账户文件
    sanitizeAccounts(p);
    return p;
}

ProbeResult probeMultiMc()
{
    ProbeResult p;
    p.key = QStringLiteral("multimc");
    p.displayName = QStringLiteral("MultiMC");

    const QString roaming = roamingAppData();
    const QString local = localAppData();
    QStringList candidates = {
        roaming + QStringLiteral("/MultiMC"),
        roaming + QStringLiteral("/PrismLauncher"),
    };
    if (!local.isEmpty())
        candidates << local + QStringLiteral("/Programs/PrismLauncher");

    QStringList existing;
    for (const QString &candidate : std::as_const(candidates)) {
        if (!QDir(candidate).exists())
            continue;
        addDataDir(existing, candidate);
        if (QDir(candidate + QStringLiteral("/instances")).exists())
            addDataDir(p.dataDirs, candidate);
    }
    if (p.dataDirs.size() > 1)
        p.displayName = QStringLiteral("MultiMC / Prism");
    p.dataDir = p.dataDirs.value(0);

    for (const QString &dataDir : std::as_const(p.dataDirs)) {
        const QDir instancesDir(dataDir + QStringLiteral("/instances"));
        if (instancesDir.exists()) {
            const QStringList names =
                instancesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString &name : names) {
                const QString instDir = instancesDir.absoluteFilePath(name);
                addGameDir(p.gameDirs, instDir + QStringLiteral("/.minecraft"));
                if (QFileInfo::exists(instDir + QStringLiteral("/.minecraft/versions")))
                    addGameDir(p.gameDirs, instDir);
            }
        }
        // Java / 内存：prism.cfg / mmc.cfg，其次各实例的 instance.cfg
        if (p.maxMemoryMb <= 0) {
            const QStringList cfgFiles = {
                dataDir + QStringLiteral("/prism.cfg"),
                dataDir + QStringLiteral("/mmc.cfg"),
            };
            for (const QString &cfg : cfgFiles) {
                QSettings settings(cfg, QSettings::IniFormat);
                addJavaPath(p, settings.value(QStringLiteral("JavaPath")).toString());
                if (p.maxMemoryMb <= 0) {
                    const int maxMemory = settings.value(QStringLiteral("MaxMemAlloc"), -1).toInt();
                    if (maxMemory > 0)
                        p.maxMemoryMb = maxMemory;
                }
            }
        }
        const QDir instanceCfgRoot(dataDir + QStringLiteral("/instances"));
        if (instanceCfgRoot.exists()) {
            const QStringList names =
                instanceCfgRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString &name : names) {
                QSettings settings(instanceCfgRoot.absoluteFilePath(name + QStringLiteral("/instance.cfg")),
                                   QSettings::IniFormat);
                addJavaPath(p, settings.value(QStringLiteral("JavaPath")).toString());
            }
        }
        // 账户
        if (p.accounts.isEmpty()) {
            const QString accountsFile = dataDir + QStringLiteral("/accounts.json");
            if (QFileInfo::exists(accountsFile))
                p.accounts = parseMultiMcAccounts(accountsFile);
        }
    }

    p.found = !p.dataDirs.isEmpty();
    mergeAccountsFromGameDirs(p); // 顺带带走实例里 PCL / 官方格式的账户文件
    sanitizeAccounts(p);
    return p;
}

ProbeResult probeBaka()
{
    ProbeResult p;
    p.key = QStringLiteral("baka");
    p.displayName = QStringLiteral("BakaXL");

    const QString roaming = roamingAppData();
    const QString dataDir = roaming + QStringLiteral("/BakaXL");
    addDataDir(p.dataDirs, dataDir);
    p.dataDir = p.dataDirs.value(0);

    // 游戏目录：CoreDirectory.json 的 directories 键就是 .minecraft 路径（明文）
    const QJsonObject root = readJson(dataDir + QStringLiteral("/CoreDirectory.json")).object();
    const QJsonObject directories = root.value(QStringLiteral("directories")).toObject();
    for (auto it = directories.constBegin(); it != directories.constEnd(); ++it)
        addGameDir(p.gameDirs, it.key());
    const QString merged = root.value(QStringLiteral("MergerGameAssetsDirectiory")).toString();
    if (!merged.isEmpty())
        addGameDir(p.gameDirs, merged);

    // 账户：BakaAccount.xml 是设备端加密的，读不了
    p.notes << trImp("BakaXL 账户为加密存储，无法导入账户");

    p.found = !p.dataDirs.isEmpty() || !p.gameDirs.isEmpty();
    return p;
}

ProbeResult probeOfficial()
{
    ProbeResult p;
    p.key = QStringLiteral("official");
    p.displayName = QStringLiteral("Official");

    const QString roaming = roamingAppData();
    const QStringList candidates = {
        roaming + QStringLiteral("/.minecraft"),
        QDir::homePath() + QStringLiteral("/.minecraft"),
    };
    QString mcDir;
    for (const QString &candidate : candidates) {
        if (looksLikeMinecraftDir(candidate)) {
            mcDir = QDir::toNativeSeparators(cleanDir(candidate));
            break;
        }
    }
    if (mcDir.isEmpty()) {
        for (const QString &candidate : candidates) {
            if (QFileInfo::exists(candidate + QStringLiteral("/launcher_accounts.json"))
                || QFileInfo::exists(candidate + QStringLiteral("/launcher_profiles.json"))) {
                mcDir = QDir::toNativeSeparators(cleanDir(candidate));
                break;
            }
        }
    }
    p.dataDir = mcDir;
    addGameDir(p.gameDirs, mcDir);

    const QString accountsFile = mcDir + QStringLiteral("/launcher_accounts.json");
    if (QFileInfo::exists(accountsFile))
        p.accounts = parseOfficialAccounts(accountsFile);
    if (p.accounts.isEmpty()) {
        const QString profilesFile = mcDir + QStringLiteral("/launcher_profiles.json");
        if (QFileInfo::exists(profilesFile))
            p.accounts = parseOfficialAccounts(profilesFile);
    }

    p.found = !p.dataDirs.isEmpty() || !p.gameDirs.isEmpty() || !mcDir.isEmpty();
    mergeAccountsFromGameDirs(p); // 官方游戏目录里也可能还留着 launcher_profiles.json 的账户
    sanitizeAccounts(p);
    return p;
}

/* ---------------------------------------------------------------- 安卓启动器探测 */

/** 安卓卡片专用的 key（账户 / Java 都在应用私有目录，导入只能带游戏目录与版本） */
bool isAndroidLauncherKey(const QString &key)
{
    return key == QStringLiteral("fcl") || key == QStringLiteral("zl2")
           || key == QStringLiteral("pojav") || key == QStringLiteral("other");
}

/** 安卓共享存储里，名字带某个关键词的游戏目录候选（用户把游戏目录挪出来时最常见）：
 *  共享存储根与常见的 games 子目录各扫一层 */
QStringList androidSharedGameHomes(const QString &root, const QString &keyword)
{
    QStringList homes;
    const QStringList bases = {
        root,
        root + QStringLiteral("/games"),
        root + QStringLiteral("/Games"),
    };
    for (const QString &base : bases) {
        const QFileInfoList entries =
            QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            if (!entry.fileName().contains(keyword, Qt::CaseInsensitive))
                continue;
            homes << entry.absoluteFilePath();
        }
    }
    return homes;
}

/** 安卓：该游戏目录是不是已经归到 FCL / ZL2 / Pojav 卡片里了（"其他目录"卡不重复认领） */
bool androidDirClaimedByLauncher(const QString &dir)
{
    const QString lower = QDir::fromNativeSeparators(dir).toLower();
    static const QStringList markers = {
        QStringLiteral("/fcl/"),
        QStringLiteral("zalith"),
        QStringLiteral("pojavlauncher"),
        QStringLiteral("/android/data/com.tungsten.fcl"),
        QStringLiteral("/android/data/com.movtery.zalithlauncher"),
        QStringLiteral("/android/data/org.zalithlauncher"),
        QStringLiteral("/android/data/net.kdt.pojavlaunch"),
    };
    for (const QString &marker : markers) {
        if (lower.contains(marker))
            return true;
    }
    return false;
}

/** 安卓：方块盒子自己用的目录（不要出现在"其他目录"卡里） */
bool androidDirIsOwn(const QString &dir)
{
    const QString lower = QDir::fromNativeSeparators(dir).toLower();
    return lower.contains(QStringLiteral("/android/data/com.blockbox.launcher"))
           || lower.contains(QStringLiteral("/blockbox/.minecraft"));
}

/** 安卓探测的收尾：写 dataDir / found / 账户说明 */
void finishAndroidProbe(ProbeResult &p, const QString &note)
{
    p.dataDir = p.dataDirs.value(0);
    if (p.dataDir.isEmpty())
        p.dataDir = p.gameDirs.value(0);
    mergeAccountsFromGameDirs(p);
    sanitizeAccounts(p);
    p.found = !p.dataDirs.isEmpty() || !p.gameDirs.isEmpty();
    if (!p.gameDirs.isEmpty() && p.accounts.isEmpty() && !note.isEmpty())
        p.notes << note;
}

/** FCL：数据目录 <根>/FCL，默认游戏目录 <FCL>/.minecraft。
 *  账户（accounts.json）与 Java 运行时都在应用私有目录里，安卓 11 起读不到，只能带版本与目录。 */
ProbeResult probeFcl()
{
    ProbeResult p;
    p.key = QStringLiteral("fcl");
    p.displayName = QStringLiteral("FCL");

    const QString root = androidExternalRoot();
    if (root.isEmpty())
        return p; // 非安卓目标：卡片不会用到，保持未检测

    const QString fclHome = root + QStringLiteral("/FCL");
    addDataDir(p.dataDirs, fclHome);

    // FCL 的共享游戏目录（FCLPath.SHARED_COMMON_DIR）
    addGameDir(p.gameDirs, fclHome + QStringLiteral("/.minecraft"));
    // 自己把游戏目录挪到共享存储、名字里带 FCL 的情况
    for (const QString &home : androidSharedGameHomes(root, QStringLiteral("fcl"))) {
        addGameDir(p.gameDirs, home + QStringLiteral("/.minecraft"));
        addGameDir(p.gameDirs, home);
    }
    // FCL 的外部私有游戏目录（安卓 10 及以下、或系统刚好放开时能读到）
    addGameDir(p.gameDirs, root + QStringLiteral("/Android/data/com.tungsten.fcl/files/.minecraft"));

    finishAndroidProbe(p, trImp("FCL 的账户与 Java 存在应用私有目录，安卓系统不允许其他应用读取，"
                                "导入后请在「设置 → 账户管理」里重新登录"));
    return p;
}

/** Zalith Launcher 2（ZL2）：默认游戏目录在自身外部私有目录里（安卓 11 起读不到），
 *  用户在 ZL2 里把游戏目录挪到共享存储后，按目录名里的 zalith 认领。 */
ProbeResult probeZl2()
{
    ProbeResult p;
    p.key = QStringLiteral("zl2");
    p.displayName = QStringLiteral("ZL2");

    const QString root = androidExternalRoot();
    if (root.isEmpty())
        return p;

    // 启动器自己的外部私有目录（默认游戏目录在它下面）
    const QStringList dataHomes = {
        root + QStringLiteral("/Android/data/com.movtery.zalithlauncher/files"),
        root + QStringLiteral("/Android/data/org.zalithlauncher/files"), // Zalith Launcher 1 / 旧包名
    };
    for (const QString &home : dataHomes) {
        addDataDir(p.dataDirs, home);
        addGameDir(p.gameDirs, home + QStringLiteral("/.minecraft"));
    }

    // ZL2 里常见的共享存储目录
    const QStringList fixedHomes = {
        root + QStringLiteral("/ZalithLauncher"),
        root + QStringLiteral("/ZalithLauncher2"),
        root + QStringLiteral("/games/ZalithLauncher"),
    };
    for (const QString &home : fixedHomes)
        addGameDir(p.gameDirs, home + QStringLiteral("/.minecraft"));
    // 其余名字带 zalith 的一级目录
    for (const QString &home : androidSharedGameHomes(root, QStringLiteral("zalith"))) {
        addGameDir(p.gameDirs, home + QStringLiteral("/.minecraft"));
        addGameDir(p.gameDirs, home);
    }

    finishAndroidProbe(p, trImp("ZL2 的账户存在应用数据库里无法读取；默认游戏目录位于 Android/data 内，"
                                "安卓 11 及以上系统不允许其他应用读取"));
    return p;
}

/** PojavLauncher：默认游戏目录 <根>/games/PojavLauncher，游戏文件在它下面的 .minecraft
 *  （老版本直接放在游戏目录里，两种布局都认）。 */
ProbeResult probePojav()
{
    ProbeResult p;
    p.key = QStringLiteral("pojav");
    p.displayName = QStringLiteral("PojavLauncher");

    const QString root = androidExternalRoot();
    if (root.isEmpty())
        return p;

    const QStringList homes = {
        root + QStringLiteral("/games/PojavLauncher"),
        root + QStringLiteral("/PojavLauncher"),
        root + QStringLiteral("/Android/data/net.kdt.pojavlaunch/files"),
    };
    for (const QString &home : homes) {
        addDataDir(p.dataDirs, home);
        addGameDir(p.gameDirs, home + QStringLiteral("/.minecraft"));
        addGameDir(p.gameDirs, home);
    }
    for (const QString &home : androidSharedGameHomes(root, QStringLiteral("pojav"))) {
        addGameDir(p.gameDirs, home + QStringLiteral("/.minecraft"));
        addGameDir(p.gameDirs, home);
    }

    finishAndroidProbe(p, trImp("PojavLauncher 的账户存在应用私有目录，无法读取，"
                                "导入后请在「设置 → 账户管理」里重新登录"));
    return p;
}

/** 安卓：共享存储里那些"一看就不是游戏目录"的大目录，翻找时直接跳过，
 *  免得在手机上遍历相册、聊天记录这类成千上万文件的地方 */
bool androidNoiseDirName(const QString &name)
{
    static const QSet<QString> skip = {
        QStringLiteral("android"),
        QStringLiteral("dcim"),
        QStringLiteral("pictures"),
        QStringLiteral("movies"),
        QStringLiteral("music"),
        QStringLiteral("alarms"),
        QStringLiteral("notifications"),
        QStringLiteral("podcasts"),
        QStringLiteral("ringtones"),
        QStringLiteral("recordings"),
        QStringLiteral("screenshots"),
        QStringLiteral("bluetooth"),
        QStringLiteral("fonts"),
    };
    return skip.contains(name.toLower());
}

/** 本机其他游戏目录：共享存储里任何像 .minecraft 的目录（手动拷进来的、旧启动器留下的、
 *  第三方工具解压的），只要不归前面几张卡，就当作可导入来源。 */
ProbeResult probeAndroidOther()
{
    ProbeResult p;
    p.key = QStringLiteral("other");
    p.displayName = QStringLiteral("其他目录");

    const QString root = androidExternalRoot();
    if (root.isEmpty())
        return p;

    QStringList candidates;
    candidates << root + QStringLiteral("/.minecraft");
    const QStringList bases = {
        root,
        root + QStringLiteral("/games"),
        root + QStringLiteral("/Games"),
    };
    for (const QString &base : bases) {
        const QFileInfoList entries =
            QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QString dir = entry.absoluteFilePath();
            candidates << dir + QStringLiteral("/.minecraft");
            candidates << dir; // 目录本身可能就是游戏目录（versions 直接放在里面）
            if (base != root || androidNoiseDirName(entry.fileName()))
                continue;
            // 再往下翻一层：整合包常被解压到 Download/某某整合包/.minecraft 这种位置
            int visited = 0;
            const QFileInfoList subEntries = QDir(dir).entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
            for (const QFileInfo &sub : subEntries) {
                if (++visited > 80)
                    break;
                candidates << sub.absoluteFilePath() + QStringLiteral("/.minecraft");
                candidates << sub.absoluteFilePath();
            }
        }
    }

    const int maxDirs = 8;  // 卡片上放不下太多，按发现顺序取前几个
    int budget = 400;       // 最多检查这么多个候选，避免在手机上把主线程拖住
    for (const QString &candidate : std::as_const(candidates)) {
        if (p.gameDirs.size() >= maxDirs || --budget < 0)
            break;
        const QString normalized = QDir::toNativeSeparators(cleanDir(candidate));
        if (androidDirClaimedByLauncher(normalized) || androidDirIsOwn(normalized))
            continue;
        addGameDir(p.gameDirs, normalized);
    }

    finishAndroidProbe(p, trImp("账户存在各启动器的应用私有目录里，读不到就只带游戏目录与版本"));
    return p;
}

ProbeResult probeLauncher(const QString &key)
{
    if (key == QStringLiteral("pcl"))
        return probePcl();
    if (key == QStringLiteral("hmcl"))
        return probeHmcl();
    if (key == QStringLiteral("multimc"))
        return probeMultiMc();
    if (key == QStringLiteral("baka"))
        return probeBaka();
    if (key == QStringLiteral("official"))
        return probeOfficial();
    if (key == QStringLiteral("fcl"))
        return probeFcl();
    if (key == QStringLiteral("zl2"))
        return probeZl2();
    if (key == QStringLiteral("pojav"))
        return probePojav();
    if (key == QStringLiteral("other"))
        return probeAndroidOther();
    return ProbeResult();
}

/** 游戏目录的简短展示名：取最后两级，例如 "Minecraft\.minecraft" */
QString shortGameDir(const QString &path)
{
    QFileInfo info(path);
    const QString leaf = info.fileName();
    const QString parent = info.dir().dirName();
    if (parent.isEmpty())
        return leaf;
#ifdef Q_OS_WIN
    return parent + QLatin1Char('\\') + leaf;
#else
    // 安卓 / Linux 的路径分隔符是 /，别再拼成反斜杠
    return parent + QLatin1Char('/') + leaf;
#endif
}

QString composeDetectDetail(const ProbeResult &p, int versionCount)
{
    QStringList parts;
    if (p.key == QStringLiteral("baka")) {
        parts << trImp("账户已加密");
    } else if (!p.accounts.isEmpty()) {
        parts << trImp("%1 个账户").arg(p.accounts.size());
    } else if (isAndroidLauncherKey(p.key) && (versionCount > 0 || !p.gameDirs.isEmpty())) {
        // 安卓启动器的账户都在应用私有目录 / 数据库里，读不到，卡片上直接说明
        parts << trImp("账户需重新登录");
    }
    if (versionCount > 0)
        parts << trImp("%1 个版本").arg(versionCount);
    if (!p.javaPaths.isEmpty())
        parts << trImp("%1 个 Java").arg(p.javaPaths.size());
    if (!p.authServers.isEmpty())
        parts << trImp("%1 个登录服").arg(p.authServers.size());
    if (p.downloadSource == 1)
        parts << trImp("镜像下载");
    else if (p.downloadSource == 0)
        parts << trImp("官方下载");
    if (p.maxMemoryMb > 0)
        parts << trImp("内存 %1 MB").arg(p.maxMemoryMb);
    if (!p.gameDirs.isEmpty())
        parts << shortGameDir(p.gameDirs.first());
    return parts.join(QStringLiteral(" \u00B7 "));
}

} // namespace

/* ---------------------------------------------------------------- 对外接口 */

QString LauncherImporter::appDataDir()
{
    return roamingAppData();
}

QString LauncherImporter::androidStorageRoot()
{
    return androidExternalRoot();
}

bool LauncherImporter::isAndroidTarget()
{
    return !androidExternalRoot().isEmpty();
}

QStringList LauncherImporter::supportedKeys()
{
    if (isAndroidTarget()) {
        // 安卓：三家常见启动器 + 本机其他 .minecraft 兜底
        return { QStringLiteral("fcl"),
                 QStringLiteral("zl2"),
                 QStringLiteral("pojav"),
                 QStringLiteral("other") };
    }
    return { QStringLiteral("pcl"),
             QStringLiteral("hmcl"),
             QStringLiteral("multimc"),
             QStringLiteral("baka") };
}

QList<LauncherDetect> LauncherImporter::detectAll()
{
    QList<LauncherDetect> result;
    QStringList keys = supportedKeys();
    if (!isAndroidTarget())
        keys << QStringLiteral("official");
    for (const QString &key : keys)
        result.append(detectOne(key));
    return result;
}

LauncherDetect LauncherImporter::detectOne(const QString &key)
{
    const ProbeResult probe = probeLauncher(key);

    LauncherDetect d;
    d.key = key;
    d.displayName = probe.displayName;
    d.detected = probe.found;
    d.dataDir = probe.dataDir;
    d.minecraftDir = probe.gameDirs.value(0);
    d.gameDirs = probe.gameDirs;
    d.accountCount = probe.accounts.size();
    d.javaPath = probe.javaPath;
    d.javaPaths = probe.javaPaths;
    d.authServers = probe.authServers;
    d.downloadThreads = probe.downloadThreads;
    d.downloadSource = probe.downloadSource;
    d.autoMemory = probe.autoMemory;
    d.autoMemoryKnown = probe.autoMemoryKnown;
    d.javaMaxMemoryMb = probe.maxMemoryMb;
    d.javaMinMemoryMb = probe.minMemoryMb;

    QStringList versions;
    for (const QString &gameDir : probe.gameDirs) {
        for (const QString &name : versionsIn(gameDir)) {
            if (!versions.contains(name))
                versions.append(name);
        }
    }
    d.versionNames = versions;
    d.versionCount = versions.size();
    d.detail = composeDetectDetail(probe, d.versionCount);
    return d;
}

QString LauncherImporter::findMinecraftDir(const QString &key)
{
    return probeLauncher(key).gameDirs.value(0);
}

QStringList LauncherImporter::findMinecraftDirs(const QString &key)
{
    return probeLauncher(key).gameDirs;
}

LauncherImportResult LauncherImporter::importFrom(const QString &key)
{
    const ProbeResult probe = probeLauncher(key);

    LauncherImportResult r;
    r.instanceDirs = probe.gameDirs;
    r.minecraftDir = probe.gameDirs.value(0);
    r.accountCount = probe.accounts.size();
    r.javaPath = probe.javaPath;
    r.javaPaths = probe.javaPaths;
    r.authServers = probe.authServers;
    r.downloadThreads = probe.downloadThreads;
    r.downloadSource = probe.downloadSource;
    r.autoMemory = probe.autoMemory;
    r.autoMemoryKnown = probe.autoMemoryKnown;
    if (probe.maxMemoryMb > 0)
        r.javaMaxMemoryMb = qBound(512, probe.maxMemoryMb, 16384);
    if (probe.minMemoryMb > 0)
        r.javaMinMemoryMb = qBound(512, probe.minMemoryMb, 16384);

    int versionCount = 0;
    QStringList versionNames;
    for (const QString &gameDir : probe.gameDirs) {
        for (const QString &name : versionsIn(gameDir)) {
            ++versionCount;
            if (versionNames.size() < 8 && !versionNames.contains(name))
                versionNames.append(name);
        }
    }
    r.versionCount = versionCount;
    r.versionNames = versionNames;

    QStringList parts;
    if (probe.key == QStringLiteral("baka")) {
        // BakaXL 账户加密：只说明版本导入情况
    } else if (probe.key == QStringLiteral("fcl") || probe.key == QStringLiteral("zl2")
               || probe.key == QStringLiteral("pojav") || probe.key == QStringLiteral("other")) {
        // 安卓启动器账户在应用私有目录，读不到时只能带游戏目录与版本
        if (r.accountCount > 0)
            parts << trImp("%1 个账户").arg(r.accountCount);
        else
            parts << trImp("账户需在方块盒子内重新登录");
    } else if (r.accountCount > 0) {
        parts << trImp("%1 个账户").arg(r.accountCount);
    }
    if (r.versionCount > 0)
        parts << trImp("%1 个版本").arg(r.versionCount);
    if (!r.instanceDirs.isEmpty())
        parts << trImp("游戏目录已加入实例列表");
    if (!r.javaPaths.isEmpty())
        parts << trImp("%1 个 Java").arg(r.javaPaths.size());
    if (!r.authServers.isEmpty())
        parts << trImp("%1 个第三方登录服").arg(r.authServers.size());
    if (r.downloadSource == 1)
        parts << trImp("下载源已切到镜像");
    else if (r.downloadSource == 0)
        parts << trImp("下载源已切到官方");
    if (r.downloadThreads > 0)
        parts << trImp("下载线程 %1").arg(r.downloadThreads);
    if (r.javaMaxMemoryMb > 0)
        parts << trImp("内存 %1 MB").arg(r.javaMaxMemoryMb);
    if (r.autoMemoryKnown)
        parts << (r.autoMemory ? trImp("自动分配内存") : trImp("手动分配内存"));
    parts.append(probe.notes);
    r.detail = parts.isEmpty() ? trImp("没有发现可导入的内容")
                               : parts.join(QStringLiteral(" \u00B7 "));

    r.ok = !probe.accounts.isEmpty() || !probe.gameDirs.isEmpty() || !probe.javaPaths.isEmpty()
           || !probe.authServers.isEmpty() || probe.downloadSource >= 0;
    return r;
}

QList<AccountInfo> LauncherImporter::importAccounts(const QString &key)
{
    return probeLauncher(key).accounts;
}
