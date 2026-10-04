#include "utils/LauncherImporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QSettings>
#include <QStandardPaths>

namespace {

/** 递归收集 JSON 文档中的所有对象（用于宽容解析多种结构） */
void collectObjects(const QJsonValue &v, QList<QJsonObject> &out)
{
    if (v.isObject()) {
        const QJsonObject obj = v.toObject();
        out.append(obj);
        for (auto it = obj.begin(); it != obj.end(); ++it)
            collectObjects(it.value(), out);
    } else if (v.isArray()) {
        const QJsonArray arr = v.toArray();
        for (const QJsonValue &e : arr)
            collectObjects(e, out);
    }
}

/** 标准配置目录（Windows = %APPDATA%，即 Roaming）。
 *  支持 BLOCKBOX_APPDATA 环境变量覆盖（供自动化测试/诊断使用）。 */
QString appData()
{
    const QByteArray overrideEnv = qgetenv("BLOCKBOX_APPDATA");
    if (!overrideEnv.isEmpty())
        return QString::fromLocal8Bit(overrideEnv);
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

/** 判断某目录是否像 Minecraft 游戏目录（存在 versions 子目录） */
bool looksLikeMinecraftDir(const QString &dir)
{
    if (dir.isEmpty())
        return false;
    QDir d(dir);
    return d.exists() && d.cd(QStringLiteral("versions")) && d.exists();
}

} // namespace

QString LauncherImporter::appDataDir()
{
    return appData();
}

QList<LauncherDetect> LauncherImporter::detectAll()
{
    QList<LauncherDetect> result;
    const QStringList keys = { QStringLiteral("pcl"),
                               QStringLiteral("hmcl"),
                               QStringLiteral("multimc"),
                               QStringLiteral("baka"),
                               QStringLiteral("official") };
    for (const QString &k : keys)
        result.append(detectOne(k));
    return result;
}

LauncherDetect LauncherImporter::detectOne(const QString &key)
{
    LauncherDetect d;
    d.key = key;
    const QString base = appData();

    if (key == QStringLiteral("pcl")) {
        d.displayName = QStringLiteral("PCL");
        // PCL2 数据目录：%APPDATA%\PCL\PCL
        const QString dir = base + QStringLiteral("/PCL/PCL");
        d.dataDir = dir;
        d.detected = QDir(dir).exists();
        d.minecraftDir = findMinecraftDir(key);
        d.versionNames = versionsIn(d.minecraftDir);
        d.versionCount = d.versionNames.size();
        d.accountCount = importAccounts(key).size();
    } else if (key == QStringLiteral("hmcl")) {
        d.displayName = QStringLiteral("HMCL");
        // HMCL 数据目录：%APPDATA%\hmcl（accounts.json / hmcl.json）
        const QString dir = base + QStringLiteral("/hmcl");
        const QString alt = base + QStringLiteral("/HMCL");
        d.dataDir = QDir(dir).exists() ? dir : alt;
        d.detected = QDir(d.dataDir).exists()
                     || QFileInfo(base + QStringLiteral("/hmcl/accounts.json")).exists();
        d.minecraftDir = findMinecraftDir(key);
        d.versionNames = versionsIn(d.minecraftDir);
        d.versionCount = d.versionNames.size();
        d.accountCount = importAccounts(key).size();
        d.javaPath = hmclJavaPath(d.dataDir);
        {
            const QPair<int, int> mem = hmclJavaMemory(d.dataDir);
            d.javaMaxMemoryMb = mem.first;
            d.javaMinMemoryMb = mem.second;
        }
    } else if (key == QStringLiteral("multimc")) {
        // MultiMC / Prism Launcher（两者格式一致，共用处理逻辑）
        const QString mmcDir = base + QStringLiteral("/MultiMC");
        const QString prismDir = base + QStringLiteral("/PrismLauncher");
        d.displayName = QStringLiteral("MultiMC");
        if (QDir(prismDir).exists() && !QDir(mmcDir).exists())
            d.displayName = QStringLiteral("Prism");
        d.dataDir = QDir(mmcDir).exists() ? mmcDir : prismDir;
        d.detected = QDir(d.dataDir).exists()
                     && (QFileInfo(d.dataDir + QStringLiteral("/accounts.json")).exists()
                         || QDir(d.dataDir + QStringLiteral("/instances")).exists());
        d.versionNames = multiMcInstanceVersions(d.dataDir);
        d.versionCount = d.versionNames.size();
        d.accountCount = importAccounts(key).size();
        d.javaPath = multiMcJavaPath(d.dataDir);
        {
            const QPair<int, int> mem = multiMcJavaMemory(d.dataDir);
            d.javaMaxMemoryMb = mem.first;
            d.javaMinMemoryMb = mem.second;
        }
        d.minecraftDir = d.dataDir; // instances 根目录（导入时逐个注册）
    } else if (key == QStringLiteral("baka")) {
        d.displayName = QStringLiteral("BakaXL");
        // BakaXL 数据目录：%APPDATA%\BakaXL（Profiles 加密存储）
        const QString dir = base + QStringLiteral("/BakaXL");
        d.dataDir = dir;
        d.detected = QDir(dir).exists();
        d.minecraftDir = findMinecraftDir(key);
        d.versionNames = versionsIn(d.minecraftDir);
        d.versionCount = d.versionNames.size();
        d.accountCount = -1; // 加密，无法读取
    } else if (key == QStringLiteral("official")) {
        d.displayName = QStringLiteral("Official");
        const QString mc = base + QStringLiteral("/.minecraft");
        d.dataDir = mc;
        d.detected = QFileInfo(mc + QStringLiteral("/launcher_accounts.json")).exists()
                     || QFileInfo(mc + QStringLiteral("/launcher_profiles.json")).exists();
        d.minecraftDir = looksLikeMinecraftDir(mc) ? mc : QString();
        d.versionNames = versionsIn(d.minecraftDir);
        d.versionCount = d.versionNames.size();
        d.accountCount = importAccounts(key).size();
    }

    // 详情：账户数 + 版本数
    if (d.detected) {
        QStringList parts;
        if (d.accountCount >= 0)
            parts << QStringLiteral("%1 %2").arg(d.accountCount)
                      .arg(d.accountCount == 1 ? QStringLiteral("account") : QStringLiteral("accounts"));
        else
            parts << QStringLiteral("accounts encrypted");
        if (d.versionCount > 0)
            parts << QStringLiteral("%1 versions").arg(d.versionCount);
        if (!d.javaPath.isEmpty())
            parts << QStringLiteral("Java \u2713");
        d.detail = parts.join(QStringLiteral(" \u00B7 "));
    }
    return d;
}

QString LauncherImporter::findMinecraftDir(const QString &key)
{
    const QString base = appData();
    const QString def = base + QStringLiteral("/.minecraft");

    // MultiMC/Prism 无单一 .minecraft：每个 instance 一个游戏目录，由 importFrom 逐个注册
    if (key == QStringLiteral("multimc"))
        return QString();

    if (key == QStringLiteral("pcl")) {
        // PCL2 数据目录下可能携带自定义 .minecraft，依次探测
        const QStringList candidates = {
            base + QStringLiteral("/PCL/PCL/.minecraft"),
            base + QStringLiteral("/PCL/.minecraft"),
            def,
        };
        for (const QString &c : candidates)
            if (looksLikeMinecraftDir(c))
                return c;
        return QString();
    }

    if (key == QStringLiteral("hmcl")) {
        // HMCL 允许自定义游戏目录，尝试从 hmcl.json 的 configurations 中读取 gameDir/path
        const QString hmclJson = base + QStringLiteral("/hmcl/hmcl.json");
        QFile f(hmclJson);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            f.close();
            QList<QJsonObject> objs;
            collectObjects(doc.object(), objs);
            // 优先取含 versions 的 gameDir / path 字段
            QString candidate;
            for (const QJsonObject &o : objs) {
                const QString gd = o.value(QStringLiteral("gameDir")).toString();
                const QString p = o.value(QStringLiteral("path")).toString();
                if (looksLikeMinecraftDir(gd))
                    return gd;
                if (candidate.isEmpty() && !gd.isEmpty())
                    candidate = gd;
                if (candidate.isEmpty() && !p.isEmpty() && QDir(p).exists())
                    candidate = p;
            }
            if (!candidate.isEmpty())
                return candidate;
        }
    }

    return looksLikeMinecraftDir(def) ? def : QString();
}

LauncherImportResult LauncherImporter::importFrom(const QString &key)
{
    LauncherImportResult r;
    const QString base = appData();

    QList<AccountInfo> accounts;
    QString mcDir;
    QStringList versionNames;

    if (key == QStringLiteral("hmcl")) {
        const QString accFile = base + QStringLiteral("/hmcl/accounts.json");
        if (QFileInfo::exists(accFile))
            accounts = parseHmclAccounts(accFile);
        mcDir = findMinecraftDir(key);
        versionNames = versionsIn(mcDir);
        r.javaPath = hmclJavaPath(base + QStringLiteral("/hmcl"));
        {
            const QPair<int, int> mem = hmclJavaMemory(base + QStringLiteral("/hmcl"));
            r.javaMaxMemoryMb = mem.first;
            r.javaMinMemoryMb = mem.second;
        }
    } else if (key == QStringLiteral("pcl") || key == QStringLiteral("official")) {
        mcDir = findMinecraftDir(key);
        const QString profiles = mcDir + QStringLiteral("/launcher_profiles.json");
        const QString accountsFile = mcDir + QStringLiteral("/launcher_accounts.json");
        if (QFileInfo::exists(profiles))
            accounts = parseOfficialAccounts(profiles);
        if (accounts.isEmpty() && QFileInfo::exists(accountsFile))
            accounts = parseOfficialAccounts(accountsFile);
        versionNames = versionsIn(mcDir);
    } else if (key == QStringLiteral("multimc")) {
        // MultiMC / Prism：账户 + 每个 instance 一个游戏目录
        const QString mmcDir = base + QStringLiteral("/MultiMC");
        const QString prismDir = base + QStringLiteral("/PrismLauncher");
        const QString dataDir = QDir(mmcDir).exists() ? mmcDir : prismDir;
        const QString accFile = dataDir + QStringLiteral("/accounts.json");
        if (QFileInfo::exists(accFile))
            accounts = parseMultiMCAccounts(accFile);
        // 遍历 instances：收集版本名 + 实例目录
        const QDir instancesDir(dataDir + QStringLiteral("/instances"));
        if (instancesDir.exists()) {
            const QStringList names = instancesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &name : names) {
                const QString instDir = instancesDir.absoluteFilePath(name);
                // 有效实例：含 mmc-pack.json 或 .minecraft
                if (!QFileInfo(instDir + QStringLiteral("/mmc-pack.json")).exists()
                    && !QDir(instDir + QStringLiteral("/.minecraft")).exists())
                    continue;
                const QString pack = instDir + QStringLiteral("/mmc-pack.json");
                QFile pf(pack);
                if (pf.open(QIODevice::ReadOnly)) {
                    const QJsonObject root = QJsonDocument::fromJson(pf.readAll()).object();
                    pf.close();
                    const QJsonArray comps = root.value(QStringLiteral("components")).toArray();
                    QString version;
                    QString loader;
                    for (const QJsonValue &cv : comps) {
                        const QJsonObject c = cv.toObject();
                        const QString uid = c.value(QStringLiteral("uid")).toString();
                        if (uid == QStringLiteral("net.minecraft"))
                            version = c.value(QStringLiteral("version")).toString();
                        else if (c.value(QStringLiteral("cachedName")).toString() == QStringLiteral("Fabric Loader")
                                 || c.value(QStringLiteral("cachedName")).toString() == QStringLiteral("Quilt Loader"))
                            loader = c.value(QStringLiteral("version")).toString();
                        else if (c.value(QStringLiteral("cachedName")).toString().startsWith(QStringLiteral("Forge"))
                                 || c.value(QStringLiteral("cachedName")).toString().startsWith(QStringLiteral("NeoForge")))
                            loader = c.value(QStringLiteral("version")).toString();
                    }
                    QString label = version;
                    if (!loader.isEmpty() && !label.isEmpty())
                        label += QStringLiteral(" + ") + loader;
                    if (!label.isEmpty())
                        versionNames.append(label);
                }
                // 实例的 .minecraft 可单独注册为实例文件夹
                const QString instMc = instDir + QStringLiteral("/.minecraft");
                if (looksLikeMinecraftDir(instMc))
                    r.instanceDirs.append(instMc);
            }
        }
        r.javaPath = multiMcJavaPath(dataDir);
        {
            const QPair<int, int> mem = multiMcJavaMemory(dataDir);
            r.javaMaxMemoryMb = mem.first;
            r.javaMinMemoryMb = mem.second;
        }
        r.minecraftDir = dataDir;
    } else if (key == QStringLiteral("baka")) {
        // BakaXL 账户加密存储，仅导入版本目录
        mcDir = findMinecraftDir(key);
        versionNames = versionsIn(mcDir);
    }

    r.minecraftDir = mcDir.isEmpty() ? r.minecraftDir : mcDir;
    r.versionNames = versionNames;
    r.versionCount = versionNames.size();
    r.accountCount = accounts.size();
    r.ok = !accounts.isEmpty() || r.versionCount > 0 || !r.instanceDirs.isEmpty();

    QStringList parts;
    if (!accounts.isEmpty())
        parts << QStringLiteral("%1 %2").arg(accounts.size())
                                            .arg(accounts.size() == 1 ? QStringLiteral("account") : QStringLiteral("accounts"));
    if (r.versionCount > 0)
        parts << QStringLiteral("%1 versions").arg(r.versionCount);
    if (!r.instanceDirs.isEmpty())
        parts << QStringLiteral("%1 instances").arg(r.instanceDirs.size());
    if (!r.javaPath.isEmpty())
        parts << QStringLiteral("Java \u2713");
    if (r.javaMaxMemoryMb > 0)
        parts << QStringLiteral("%1 MB RAM").arg(r.javaMaxMemoryMb);
    r.detail = parts.join(QStringLiteral(", "));
    if (r.detail.isEmpty())
        r.detail = QStringLiteral("nothing importable");
    return r;
}

QList<AccountInfo> LauncherImporter::importAccounts(const QString &key)
{
    const QString base = appData();
    if (key == QStringLiteral("hmcl")) {
        const QString accFile = base + QStringLiteral("/hmcl/accounts.json");
        if (QFileInfo::exists(accFile))
            return parseHmclAccounts(accFile);
    } else if (key == QStringLiteral("pcl") || key == QStringLiteral("official")) {
        const QString mcDir = findMinecraftDir(key);
        if (mcDir.isEmpty())
            return {};
        const QString profiles = mcDir + QStringLiteral("/launcher_profiles.json");
        const QString accountsFile = mcDir + QStringLiteral("/launcher_accounts.json");
        if (QFileInfo::exists(profiles)) {
            const QList<AccountInfo> accs = parseOfficialAccounts(profiles);
            if (!accs.isEmpty())
                return accs;
        }
        if (QFileInfo::exists(accountsFile))
            return parseOfficialAccounts(accountsFile);
    } else if (key == QStringLiteral("multimc")) {
        const QString mmcDir = base + QStringLiteral("/MultiMC");
        const QString prismDir = base + QStringLiteral("/PrismLauncher");
        const QString dataDir = QDir(mmcDir).exists() ? mmcDir : prismDir;
        const QString accFile = dataDir + QStringLiteral("/accounts.json");
        if (QFileInfo::exists(accFile))
            return parseMultiMCAccounts(accFile);
    }
    // BakaXL 账户加密存储，无法解析
    return {};
}

QList<AccountInfo> LauncherImporter::parseHmclAccounts(const QString &path)
{
    QList<AccountInfo> result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return result;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    // HMCL 账户文件常见结构：
    //   1) [ {type, username, uuid, skin}, ... ]           （直接数组）
    //   2) { "accounts": [ ... ] }                          （包装数组）
    //   3) { "<type>:<uuid>": { type, username, ... } }     （map 结构）
    QList<QJsonObject> objs;
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const QJsonValue &e : arr)
            if (e.isObject())
                objs.append(e.toObject());
    } else if (doc.isObject()) {
        collectObjects(doc.object(), objs);
    }

    for (const QJsonObject &o : objs) {
        const QString type = o.value(QStringLiteral("type")).toString();
        QString username = o.value(QStringLiteral("username")).toString();
        if (username.isEmpty())
            username = o.value(QStringLiteral("name")).toString();

        AccountInfo acc;
        if (type == QStringLiteral("offline")) {
            acc.type = QStringLiteral("offline");
            acc.username = username;
        } else if (type == QStringLiteral("microsoft")) {
            acc.type = QStringLiteral("microsoft");
            acc.username = username;
            acc.accessToken = o.value(QStringLiteral("accessToken")).toString();
            acc.refreshToken = o.value(QStringLiteral("refreshToken")).toString();
            acc.uuid = o.value(QStringLiteral("uuid")).toString();
            // HMCL 微软账户的 profile 名可能嵌套在 selectedProfile / profiles
            if (acc.username.isEmpty())
                acc.username = o.value(QStringLiteral("selectedProfile"))
                                   .toObject().value(QStringLiteral("name")).toString();
        } else if (type == QStringLiteral("authlibInjector")) {
            // 第三方服务器账户：保留用户名，类型归离线（BlockBox 无第三方类型）
            acc.type = QStringLiteral("offline");
            acc.username = username;
            acc.serverUrl = o.value(QStringLiteral("apiRoot")).toString();
        } else {
            continue;
        }
        if (acc.username.isEmpty())
            continue;
        result.append(acc);
    }
    return result;
}

QList<AccountInfo> LauncherImporter::parseOfficialAccounts(const QString &path)
{
    QList<AccountInfo> result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return result;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return result;
    const QJsonObject root = doc.object();

    const QFileInfo fi(path);
    const bool isLauncherAccounts = fi.fileName() == QStringLiteral("launcher_accounts.json");

    if (isLauncherAccounts) {
        // 新版官方：{ "accounts": { "<uuid>": { accessToken, username, minecraftProfile:{name,id}, type } }, "activeAccountLocalId": "..." }
        const QJsonObject accounts = root.value(QStringLiteral("accounts")).toObject();
        QString activeId = root.value(QStringLiteral("activeAccountLocalId")).toString();
        for (auto it = accounts.begin(); it != accounts.end(); ++it) {
            const QJsonObject o = it.value().toObject();
            AccountInfo acc;
            acc.type = QStringLiteral("microsoft");
            acc.uuid = it.key();
            acc.username = o.value(QStringLiteral("username")).toString();
            acc.accessToken = o.value(QStringLiteral("accessToken")).toString();
            const QString name = o.value(QStringLiteral("minecraftProfile")).toObject()
                                     .value(QStringLiteral("name")).toString();
            if (!name.isEmpty())
                acc.username = name;
            if (acc.username.isEmpty())
                continue;
            acc.isDefault = (it.key() == activeId);
            result.append(acc);
        }
    } else {
        // 旧版官方 / PCL2：launcher_profiles.json
        // { "authenticationDatabase": { "<uuid>": { username, accessToken, profiles:{ "<id>": {name,...} } } },
        //   "selectedUser": { "account": "<uuid>", "profile": "<id>" } }
        const QJsonObject authDb = root.value(QStringLiteral("authenticationDatabase")).toObject();
        const QJsonObject selUser = root.value(QStringLiteral("selectedUser")).toObject();
        const QString selAccount = selUser.value(QStringLiteral("account")).toString();
        for (auto it = authDb.begin(); it != authDb.end(); ++it) {
            const QJsonObject o = it.value().toObject();
            AccountInfo acc;
            acc.type = QStringLiteral("microsoft");
            acc.uuid = it.key();
            acc.username = o.value(QStringLiteral("username")).toString();
            acc.accessToken = o.value(QStringLiteral("accessToken")).toString();
            // profiles 里的名字优先（游戏角色名）
            const QJsonObject profiles = o.value(QStringLiteral("profiles")).toObject();
            if (!profiles.isEmpty()) {
                const QJsonObject first = profiles.begin().value().toObject();
                const QString name = first.value(QStringLiteral("name")).toString();
                if (!name.isEmpty())
                    acc.username = name;
            }
            if (acc.username.isEmpty())
                continue;
            acc.isDefault = (it.key() == selAccount);
            result.append(acc);
        }
    }
    return result;
}

int LauncherImporter::countVersions(const QString &minecraftDir)
{
    if (minecraftDir.isEmpty())
        return 0;
    const QDir v(minecraftDir + QStringLiteral("/versions"));
    if (!v.exists())
        return 0;
    return v.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
}

QList<AccountInfo> LauncherImporter::parseMultiMCAccounts(const QString &path)
{
    QList<AccountInfo> result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return result;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    // Prism/MultiMC 账户格式：
    // { "accounts": [ { "type": "MSA"|"Offline"|"Yggdrasil", "username": "...", "uuid": "...", ... } ],
    //   "formatVersion": 3 }
    if (!doc.isObject())
        return result;
    const QJsonArray accounts = doc.object().value(QStringLiteral("accounts")).toArray();
    for (const QJsonValue &av : accounts) {
        const QJsonObject o = av.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        AccountInfo acc;
        if (type == QStringLiteral("Offline") || type == QStringLiteral("offline")) {
            acc.type = QStringLiteral("offline");
        } else if (type == QStringLiteral("MSA") || type == QStringLiteral("microsoft")
                   || type == QStringLiteral("Yggdrasil") || type == QStringLiteral("mojang")) {
            acc.type = QStringLiteral("microsoft");
        } else {
            continue; // 未知类型跳过
        }
        acc.username = o.value(QStringLiteral("username")).toString();
        acc.uuid = o.value(QStringLiteral("uuid")).toString();
        if (acc.username.isEmpty())
            continue;
        result.append(acc);
    }
    return result;
}

QStringList LauncherImporter::versionsIn(const QString &minecraftDir)
{
    QStringList result;
    if (minecraftDir.isEmpty())
        return result;
    const QDir v(minecraftDir + QStringLiteral("/versions"));
    if (!v.exists())
        return result;
    const QStringList names = v.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        // 仅统计含 <name>.json 的版本目录（避免把非版本目录计入）
        if (QFileInfo::exists(v.absoluteFilePath(name + QStringLiteral("/") + name + QStringLiteral(".json"))))
            result.append(name);
    }
    return result;
}

QStringList LauncherImporter::multiMcInstanceVersions(const QString &dataDir)
{
    QStringList result;
    const QDir instancesDir(dataDir + QStringLiteral("/instances"));
    if (!instancesDir.exists())
        return result;
    const QStringList names = instancesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        const QString pack = instancesDir.absoluteFilePath(name + QStringLiteral("/mmc-pack.json"));
        QFile pf(pack);
        if (!pf.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject root = QJsonDocument::fromJson(pf.readAll()).object();
        pf.close();
        const QJsonArray comps = root.value(QStringLiteral("components")).toArray();
        QString version;
        QString loader;
        for (const QJsonValue &cv : comps) {
            const QJsonObject c = cv.toObject();
            const QString uid = c.value(QStringLiteral("uid")).toString();
            const QString cached = c.value(QStringLiteral("cachedName")).toString();
            if (uid == QStringLiteral("net.minecraft")) {
                version = c.value(QStringLiteral("version")).toString();
            } else if (cached == QStringLiteral("Fabric Loader")
                       || cached == QStringLiteral("Quilt Loader")
                       || cached.startsWith(QStringLiteral("Forge"))
                       || cached.startsWith(QStringLiteral("NeoForge"))
                       || cached.startsWith(QStringLiteral("OptiFine"))) {
                loader = c.value(QStringLiteral("version")).toString();
            }
        }
        QString label = version;
        if (!loader.isEmpty() && !label.isEmpty())
            label += QStringLiteral(" + ") + loader;
        else if (!loader.isEmpty())
            label = loader;
        if (!label.isEmpty())
            result.append(label);
    }
    return result;
}

QString LauncherImporter::multiMcJavaPath(const QString &dataDir)
{
    // 便携/安装版：prism.cfg / mmc.cfg 的 JavaPath（INI 格式）
    const QStringList cfgFiles = {
        dataDir + QStringLiteral("/prism.cfg"),
        dataDir + QStringLiteral("/mmc.cfg"),
    };
    for (const QString &cfg : cfgFiles) {
        QSettings s(cfg, QSettings::IniFormat);
        const QString java = s.value(QStringLiteral("JavaPath")).toString();
        if (!java.isEmpty() && QFileInfo::exists(java))
            return java;
    }
    // 回退：首个 instance.cfg 的 JavaPath
    const QDir instancesDir(dataDir + QStringLiteral("/instances"));
    if (instancesDir.exists()) {
        const QStringList names = instancesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : names) {
            QSettings s(instancesDir.absoluteFilePath(name + QStringLiteral("/instance.cfg")),
                        QSettings::IniFormat);
            const QString java = s.value(QStringLiteral("JavaPath")).toString();
            if (!java.isEmpty() && QFileInfo::exists(java))
                return java;
        }
    }
    return QString();
}

QString LauncherImporter::hmclJavaPath(const QString &dataDir)
{
    const QString hmclJson = dataDir + QStringLiteral("/hmcl.json");
    QFile f(hmclJson);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QString java = root.value(QStringLiteral("java")).toString();
    if (!java.isEmpty() && QFileInfo::exists(java))
        return java;
    return QString();
}

QPair<int, int> LauncherImporter::hmclJavaMemory(const QString &dataDir)
{
    // HMCL hmcl.json：maxMemory / minMemory（单位 MB）
    const QString hmclJson = dataDir + QStringLiteral("/hmcl.json");
    QFile f(hmclJson);
    if (!f.open(QIODevice::ReadOnly))
        return {-1, -1};
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const int maxMem = root.value(QStringLiteral("maxMemory")).toInt(-1);
    const int minMem = root.value(QStringLiteral("minMemory")).toInt(-1);
    return {maxMem, minMem};
}

QPair<int, int> LauncherImporter::multiMcJavaMemory(const QString &dataDir)
{
    // MultiMC/Prism：prism.cfg / mmc.cfg / 首个 instance.cfg 的 MaxMemAlloc / MinMemAlloc（单位 MB）
    const QStringList cfgFiles = {
        dataDir + QStringLiteral("/prism.cfg"),
        dataDir + QStringLiteral("/mmc.cfg"),
    };
    for (const QString &cfg : cfgFiles) {
        QSettings s(cfg, QSettings::IniFormat);
        const int maxMem = s.value(QStringLiteral("MaxMemAlloc"), -1).toInt();
        const int minMem = s.value(QStringLiteral("MinMemAlloc"), -1).toInt();
        if (maxMem > 0 || minMem > 0)
            return {maxMem > 0 ? maxMem : -1, minMem > 0 ? minMem : -1};
    }
    // 回退：遍历 instance.cfg 取首个有内存配置的
    const QDir instancesDir(dataDir + QStringLiteral("/instances"));
    if (instancesDir.exists()) {
        const QStringList names = instancesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : names) {
            QSettings s(instancesDir.absoluteFilePath(name + QStringLiteral("/instance.cfg")),
                        QSettings::IniFormat);
            const int maxMem = s.value(QStringLiteral("MaxMemAlloc"), -1).toInt();
            const int minMem = s.value(QStringLiteral("MinMemAlloc"), -1).toInt();
            if (maxMem > 0 || minMem > 0)
                return {maxMem > 0 ? maxMem : -1, minMem > 0 ? minMem : -1};
        }
    }
    return {-1, -1};
}
