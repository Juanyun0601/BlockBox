/**
 * @file   FabricInstaller.cpp
 * @brief  Fabric安装器类实现
 * @author BlockBox Team
 * @date   2026-05-28
 */

#include "FabricInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QThreadStorage>
#include <QTimer>

#include "utils/DownloadTaskManager.h"

namespace {

/**
 * @brief 每个线程复用一个网络管理器
 *
 * 加载器安装跑在后台线程里，而 QNetworkAccessManager 必须与调用线程同亲和。
 * 早先的实现每次请求都新建一个局部管理器：连接无法复用（每个库文件都要重新
 * 握手 TLS，装 Fabric 要建几十条连接），管理器析构时还会粗暴关闭 keep-alive
 * 的 SSL 连接，日志里因此出现 "QIODevice::read (QSslSocket): device not open"。
 */
QNetworkAccessManager *threadNetworkManager()
{
    static QThreadStorage<QNetworkAccessManager *> storage;
    if (!storage.hasLocalData() || !storage.localData()) {
        storage.setLocalData(new QNetworkAccessManager());
    }
    return storage.localData();
}

}  // namespace

FabricInstaller* FabricInstaller::m_instance = nullptr;
QMutex FabricInstaller::m_instanceMutex;

FabricInstaller::FabricInstaller()
    : m_networkManager(new QNetworkAccessManager(this))
    , m_downloadSource(FabricDownloadSource::BMCL)
    , m_currentReply(nullptr)
    , m_isInstalling(false)
    , m_isCancelled(false)
    , m_totalBytes(0)
    , m_downloadedBytes(0)
{
}

FabricInstaller::~FabricInstaller()
{
}

FabricInstaller* FabricInstaller::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new FabricInstaller();
        }
    }
    return m_instance;
}

void FabricInstaller::setDownloadSource(FabricDownloadSource source)
{
    m_downloadSource = source;
}

FabricDownloadSource FabricInstaller::downloadSource() const
{
    return m_downloadSource;
}

QString FabricInstaller::getBaseUrl() const
{
    switch (m_downloadSource) {
    case FabricDownloadSource::Official:
        return "https://meta.fabricmc.cn";
    case FabricDownloadSource::BMCL:
    default:
        // BMCLAPI 的 Fabric Meta 镜像位于 /fabric-meta 前缀下，
        // 直连根路径的 /v2/... 会返回 404
        return "https://bmclapi2.bangbang93.com/fabric-meta";
    }
}

QString FabricInstaller::getMavenBaseUrl() const
{
    switch (m_downloadSource) {
    case FabricDownloadSource::Official:
        return "https://maven.fabricmc.net/";
    case FabricDownloadSource::BMCL:
    default:
        // BMCLAPI 的 Maven 镜像（含 maven.fabricmc.net 代理）
        return "https://bmclapi2.bangbang93.com/maven/";
    }
}

void FabricInstaller::setCurrentTaskId(const QString &taskId)
{
    m_currentTaskId = taskId;
}

QString FabricInstaller::currentTaskId() const
{
    return m_currentTaskId;
}

void FabricInstaller::updateTaskProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    int progress = 0;
    if (bytesTotal > 0) {
        progress = static_cast<int>((bytesReceived * 100) / bytesTotal);
    }

    DownloadTaskManager::instance()->updateTaskProgress(m_currentTaskId, progress, bytesReceived, bytesTotal);
}

void FabricInstaller::updateTaskStatus(const QString &status)
{
    if (m_currentTaskId.isEmpty()) {
        return;
    }

    DownloadTaskManager::instance()->updateTaskStep(m_currentTaskId, status);
}

QList<FabricVersionInfo> FabricInstaller::getFabricVersions(const QString &mcVersion)
{
    emit statusChanged(tr("正在获取Fabric版本列表..."));
    updateTaskStatus(tr("正在获取Fabric版本列表"));

    // 首选当前配置的镜像源，失败时依次回退，保证列表可获取
    QStringList baseUrls;
    baseUrls << getBaseUrl();
    if (!baseUrls.contains("https://bmclapi2.bangbang93.com/fabric-meta"))
        baseUrls << "https://bmclapi2.bangbang93.com/fabric-meta";
    if (!baseUrls.contains("https://meta.fabricmc.net"))
        baseUrls << "https://meta.fabricmc.net";

    QList<FabricVersionInfo> versions;
    QString lastError;

    for (const QString &base : baseUrls) {
        const QString url = QString("%1/v2/versions/loader/%2").arg(base, mcVersion);
        bool ok = false;
        const QByteArray responseData = fetchUrl(url, &ok);

        if (ok) {
            versions = parseVersionList(QString::fromUtf8(responseData), mcVersion);
            if (!versions.isEmpty()) {
                emit versionListFetched(versions);
                return versions;
            }
            lastError = tr("未找到可用的Fabric版本");
        } else {
            lastError = tr("获取Fabric版本列表失败");
        }
    }

    emit versionListFetchFailed(lastError);
    return versions;
}

QList<FabricVersionInfo> FabricInstaller::parseVersionList(const QString &jsonResponse,
                                                          const QString &mcVersionFallback)
{
    QList<FabricVersionInfo> versions;

    QJsonDocument doc = QJsonDocument::fromJson(jsonResponse.toUtf8());
    if (!doc.isArray()) {
        return versions;
    }

    QJsonArray versionArray = doc.array();

    for (const QJsonValue &value : versionArray) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject obj = value.toObject();
        QJsonObject loader = obj.value("loader").toObject();
        QJsonObject launcherMeta = obj.value("launcherMeta").toObject();

        FabricVersionInfo info;
        info.fabricVersion = loader.value("version").toString();
        // 该接口不返回 game 字段，缺失时用请求 URL 里的版本号兜底
        info.minecraftVersion = obj.value("game").toString();
        if (info.minecraftVersion.isEmpty()) {
            info.minecraftVersion = mcVersionFallback;
        }
        info.hash = loader.value("hash").toString();
        info.build = loader.value("build").toString();
        info.intermediaryMaven = obj.value("intermediary").toObject().value("maven").toString();
        info.launcherMeta = launcherMeta;

        QJsonObject versionInfo = launcherMeta.value("version").toObject();
        info.launcherMetaVersion = versionInfo.value("version").toString();

        if (!info.fabricVersion.isEmpty() && !info.minecraftVersion.isEmpty()) {
            versions.append(info);
        }
    }

    return versions;
}

void FabricInstaller::downloadFabricInstaller(const QString &mcVersion, const QString &fabricVersion,
                                              const QString &instancePath)
{
    if (m_isInstalling) {
        emit installFailed(tr("安装器正忙"));
        return;
    }

    m_isInstalling = true;
    m_isCancelled = false;
    m_currentMcVersion = mcVersion;
    m_currentFabricVersion = fabricVersion;
    m_currentInstancePath = instancePath;
    m_downloadedBytes = 0;
    m_totalBytes = 0;

    emit statusChanged(tr("正在获取Fabric版本信息..."));
    updateTaskStatus(tr("正在获取Fabric版本信息"));

    QList<FabricVersionInfo> versions = getFabricVersions(mcVersion);

    FabricVersionInfo targetVersion;
    bool found = false;

    for (const FabricVersionInfo &version : versions) {
        if (version.fabricVersion == fabricVersion) {
            targetVersion = version;
            found = true;
            break;
        }
    }

    if (!found) {
        // 区分「列表根本没拿到」和「列表里没有这个版本」，避免把网络问题误报成版本不存在
        if (versions.isEmpty()) {
            emit installFailed(tr("获取Fabric版本列表失败") + QString(" (Minecraft %1)").arg(mcVersion));
        } else {
            QStringList available;
            for (const FabricVersionInfo &version : versions) {
                if (available.size() >= 3) {
                    break;
                }
                if (!version.fabricVersion.isEmpty()) {
                    available << version.fabricVersion;
                }
            }
            emit installFailed(tr("未找到指定版本的Fabric: %1").arg(fabricVersion)
                               + (available.isEmpty()
                                      ? QString()
                                      : QString(" (Minecraft %1: %2)").arg(mcVersion, available.join(", "))));
        }
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    installFabric(targetVersion, instancePath);
}

void FabricInstaller::installFabric(const FabricVersionInfo &version, const QString &instancePath)
{
    // ── 单文件夹合并式安装（与 BakaXL 等整合式启动器一致）──
    // 一个实例只有一个版本目录 <mc>-Fabric <loader>：目录内是合并后的完整
    // 版本 JSON（原版全部字段 + Fabric 运行库 + Knot 主类，不含 inheritsFrom）
    // 和改名后的原版 client jar。不再额外生成 fabric-loader-xxx 继承目录。
    const QString versionsRoot = instancePath + "/versions";

    emit statusChanged(tr("正在准备实例目录..."));
    updateTaskStatus(tr("正在准备实例目录"));
    emit installProgressUpdated(8, tr("准备实例目录"));

    // ── 1. 原版基准 JSON：优先读本地已下载的原版目录，其次从官方/镜像源拉取 ──
    QJsonObject baseJson;
    bool hasLocalVanilla = false;
    const QStringList jsonCandidates = {
        versionsRoot + "/" + m_currentMcVersion + "/" + m_currentMcVersion + ".json",
        versionsRoot + "/" + m_currentMcVersion + "/version.json"
    };
    for (const QString &candidate : jsonCandidates) {
        QFile candidateFile(candidate);
        if (!candidateFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(candidateFile.readAll(), &parseError);
        candidateFile.close();
        if (parseError.error == QJsonParseError::NoError && doc.isObject()
                && doc.object().value("mainClass").isString()) {
            baseJson = doc.object();
            hasLocalVanilla = true;
            break;
        }
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    if (baseJson.isEmpty()) {
        emit statusChanged(tr("正在获取原版版本信息..."));
        updateTaskStatus(tr("正在获取原版版本信息"));
        baseJson = fetchVanillaVersionJson(m_currentMcVersion);
        if (baseJson.isEmpty()) {
            emit installFailed(tr("找不到原版 Minecraft %1，无法安装 Fabric").arg(m_currentMcVersion));
            m_isInstalling = false;
            return;
        }
    }

    // ── 2. 拉取 Fabric 官方 profile（权威的库列表与 Knot 主类）──
    emit statusChanged(tr("正在获取Fabric版本信息..."));
    updateTaskStatus(tr("正在获取Fabric版本信息"));
    emit installProgressUpdated(15, tr("获取Fabric配置"));

    QJsonObject fabricJson = versionJsonFromProfile(
        fetchLoaderProfile(m_currentMcVersion, m_currentFabricVersion),
        m_currentMcVersion, m_currentFabricVersion);
    if (fabricJson.isEmpty()) {
        fabricJson = generateVersionJson(version);
    }
    if (fabricJson.isEmpty()) {
        emit installFailed(tr("获取Fabric配置失败"));
        m_isInstalling = false;
        return;
    }

    // ── 3. 目标目录：<mc>-Fabric <loader>；重名时追加序号 ──
    QString dirName = QString("%1-Fabric %2").arg(m_currentMcVersion, m_currentFabricVersion);
    QString targetDir = versionsRoot + "/" + dirName;
    if (QDir(targetDir).exists()) {
        for (int i = 2; QDir(targetDir).exists(); ++i) {
            dirName = QString("%1-Fabric %2 (%3)").arg(m_currentMcVersion, m_currentFabricVersion).arg(i);
            targetDir = versionsRoot + "/" + dirName;
        }
    }

    // 本地原版目录存在时直接把它改名成目标目录（jar/assets 等内容随目录一起带走）；
    // 改名失败（文件被占用等）则原位合并，仍是只有一个文件夹
    if (hasLocalVanilla) {
        if (!QDir(versionsRoot).rename(m_currentMcVersion, dirName)) {
            dirName = m_currentMcVersion;
            targetDir = versionsRoot + "/" + dirName;
        }
    } else {
        QDir().mkpath(targetDir);
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    // ── 4. client jar 归位为 <目录名>.jar；缺失时按原版 downloads.client 补下 ──
    const QString jarPath = targetDir + "/" + dirName + ".jar";
    if (!QFile::exists(jarPath)) {
        const QString oldJarPath = targetDir + "/" + m_currentMcVersion + ".jar";
        if (QFile::exists(oldJarPath)) {
            QFile::rename(oldJarPath, jarPath);
        }
        if (!QFile::exists(jarPath)) {
            const QJsonObject client = baseJson.value("downloads").toObject().value("client").toObject();
            const QString url = client.value("url").toString();
            if (url.isEmpty()) {
                emit installFailed(tr("找不到游戏核心文件 (%1.jar)").arg(m_currentMcVersion));
                m_isInstalling = false;
                return;
            }
            emit statusChanged(tr("正在下载游戏核心文件..."));
            updateTaskStatus(tr("正在下载游戏核心文件"));
            emit installProgressUpdated(25, tr("下载游戏核心文件"));
            if (!downloadFile(url, jarPath, client.value("sha1").toString())) {
                emit installFailed(tr("下载游戏核心文件失败"));
                m_isInstalling = false;
                return;
            }
        }
    }

    // ── 5. 生成合并后的版本 JSON 并写入 ──
    emit statusChanged(tr("正在生成版本配置..."));
    updateTaskStatus(tr("正在生成版本配置"));
    emit installProgressUpdated(35, tr("生成版本配置"));

    const QJsonObject versionJson = mergeFabricIntoBase(baseJson, fabricJson, dirName, m_currentMcVersion);
    const QString jsonFilePath = targetDir + "/" + dirName + ".json";
    QFile jsonFile(jsonFilePath);
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit installFailed(tr("无法创建版本配置文件"));
        m_isInstalling = false;
        return;
    }
    jsonFile.write(QJsonDocument(versionJson).toJson(QJsonDocument::Indented));
    jsonFile.close();

    // 原版命名的旧 JSON 已被合并 JSON 取代，清掉避免实例目录里出现两份配置
    if (dirName != m_currentMcVersion) {
        QFile::remove(targetDir + "/" + m_currentMcVersion + ".json");
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    // ── 6. 下载 Fabric 运行库（ASM/mixin/intermediary/loader）。──
    // 首次启动会跳过通用文件补全流程（skipFileCompletion=true），
    // 因此这些库必须在安装阶段全部就位，否则启动 Knot 时必然崩溃。
    emit statusChanged(tr("正在下载Fabric运行库..."));
    updateTaskStatus(tr("正在下载Fabric运行库"));

    QStringList failed;
    if (!downloadLibraries(versionJson, getMinecraftLibrariesPath(instancePath), &failed)) {
        if (m_isCancelled) {
            m_isInstalling = false;
            return;
        }
        emit installFailed(tr("下载Fabric运行库失败: %1").arg(failed.join(", ")));
        m_isInstalling = false;
        return;
    }

    emit installProgressUpdated(100, tr("完成"));

    m_isInstalling = false;
    emit installCompleted(dirName);

    if (!m_currentTaskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Completed,
                                                          tr("安装完成"));
    }
}

QJsonObject FabricInstaller::fetchVanillaVersionJson(const QString &mcVersion)
{
    // 版本清单：Mojang 官方 → BMCLAPI 镜像，哪个先成功用哪个
    const QStringList manifests = {
        QStringLiteral("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"),
        QStringLiteral("https://launchermeta.mojang.com/mc/game/version_manifest_v2.json"),
        QStringLiteral("https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json")
    };

    QString versionUrl;
    for (const QString &manifestUrl : manifests) {
        bool ok = false;
        const QByteArray data = fetchUrl(manifestUrl, &ok);
        if (!ok) {
            continue;
        }
        const QJsonObject manifest = QJsonDocument::fromJson(data).object();
        for (const QJsonValue &val : manifest.value("versions").toArray()) {
            if (val.toObject().value("id").toString() == mcVersion) {
                versionUrl = val.toObject().value("url").toString();
                break;
            }
        }
        if (!versionUrl.isEmpty()) {
            break;
        }
    }

    if (versionUrl.isEmpty()) {
        return QJsonObject();
    }

    bool ok = false;
    const QByteArray data = fetchUrl(versionUrl, &ok);
    if (!ok) {
        return QJsonObject();
    }
    const QJsonObject json = QJsonDocument::fromJson(data).object();
    // 基本完整性：必须带得上 mainClass/libraries，才能作为合并基准
    if (json.isEmpty() || !json.value("mainClass").isString() || !json.contains("libraries")) {
        return QJsonObject();
    }
    return json;
}

QJsonObject FabricInstaller::mergeFabricIntoBase(const QJsonObject &baseJson, const QJsonObject &fabricJson,
                                                 const QString &dirName, const QString &mcVersion)
{
    // 保留原版全部字段（arguments/assetIndex/downloads/logging/javaVersion 等），
    // 只替换主类、追加 Fabric 运行库 —— 与主流整合式启动器的合并式实例同构
    QJsonObject merged = baseJson;
    merged["id"] = dirName;
    merged["clientVersion"] = mcVersion;
    merged["mainClass"] = fabricJson.value("mainClass").toString();
    if (merged.value("mainClass").toString().isEmpty()) {
        merged["mainClass"] = QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient");
    }
    merged.remove("inheritsFrom");

    QJsonArray libraries = baseJson.value("libraries").toArray();
    for (const QJsonValue &libVal : fabricJson.value("libraries").toArray()) {
        libraries.append(libVal);
    }
    merged["libraries"] = libraries;

    if (!merged.value("type").isString()) {
        merged["type"] = QStringLiteral("release");
    }
    if (merged.value("releaseTime").toString().isEmpty()) {
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        merged["releaseTime"] = now;
        merged["time"] = now;
    }

    return merged;
}

QString FabricInstaller::mavenArtifactPath(const QString &mavenName)
{
    // "net.fabricmc:fabric-loader:0.15.11" → "net/fabricmc/fabric-loader/0.15.11/fabric-loader-0.15.11.jar"
    const QStringList parts = mavenName.split(':');
    if (parts.size() < 3) {
        return QString();
    }

    QString group = parts[0];
    group.replace('.', '/');

    QString version = parts[2];
    if (parts.size() >= 4) {
        version += "-" + parts[3];  // classifier
    }

    return group + "/" + parts[1] + "/" + parts[2] + "/" + parts[1] + "-" + version + ".jar";
}

QJsonObject FabricInstaller::libraryToMojangFormat(const QJsonObject &fabricLib, const QString &mavenBaseUrl)
{
    const QString name = fabricLib.value("name").toString();
    const QString relPath = mavenArtifactPath(name);
    if (name.isEmpty() || relPath.isEmpty()) {
        return QJsonObject();
    }

    QJsonObject artifact;
    artifact["path"] = relPath;

    // 条目自带仓库地址时：Fabric 自己的库跟随当前下载源（镜像/官方可切换），
    // 其它仓库（如 Mojang 官方库）必须用条目地址 —— 镜像里没有这些文件
    QString base = mavenBaseUrl;
    const QString ownUrl = fabricLib.value("url").toString();
    if (!ownUrl.isEmpty() && !ownUrl.contains("fabricmc.net", Qt::CaseInsensitive)) {
        base = ownUrl;
    }
    if (base.isEmpty()) {
        base = QStringLiteral("https://maven.fabricmc.net/");
    }
    if (!base.endsWith('/')) {
        base += '/';
    }

    artifact["url"] = base + relPath;
    artifact["sha1"] = fabricLib.value("sha1").toString();
    if (fabricLib.contains("size")) {
        artifact["size"] = fabricLib.value("size");
    }

    QJsonObject downloads;
    downloads["artifact"] = artifact;

    QJsonObject libOut;
    libOut["name"] = name;
    libOut["downloads"] = downloads;
    return libOut;
}

QJsonObject FabricInstaller::fetchLoaderProfile(const QString &mcVersion, const QString &loaderVersion)
{
    // 与版本列表一致：先走当前下载源，再依次回退，避免单个镜像缺数据导致安装失败
    QStringList bases;
    bases << getBaseUrl();
    if (!bases.contains("https://bmclapi2.bangbang93.com/fabric-meta"))
        bases << "https://bmclapi2.bangbang93.com/fabric-meta";
    if (!bases.contains("https://meta.fabricmc.net"))
        bases << "https://meta.fabricmc.net";

    for (const QString &base : bases) {
        const QString url = QString("%1/v2/versions/loader/%2/%3/profile/json")
                                .arg(base, mcVersion, loaderVersion);
        bool ok = false;
        const QByteArray data = fetchUrl(url, &ok);
        if (!ok) {
            continue;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject() && doc.object().value("libraries").isArray()) {
            return doc.object();
        }
    }

    return QJsonObject();
}

QJsonObject FabricInstaller::versionJsonFromProfile(const QJsonObject &profile,
                                                    const QString &mcVersion,
                                                    const QString &loaderVersion)
{
    if (!profile.value("libraries").isArray()) {
        return QJsonObject();
    }

    const QString mavenBase = getMavenBaseUrl();

    QJsonObject versionJson;
    versionJson["id"] = QString("fabric-loader-%1-%2").arg(loaderVersion, mcVersion);
    versionJson["type"] = "release";
    versionJson["releaseTime"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    versionJson["time"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    versionJson["inheritsFrom"] = mcVersion;

    QString mainClass = profile.value("mainClass").toString();
    if (mainClass.isEmpty()) {
        mainClass = QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient");
    }
    versionJson["mainClass"] = mainClass;

    QJsonArray libraries;
    for (const QJsonValue &libVal : profile.value("libraries").toArray()) {
        QJsonObject lib = libraryToMojangFormat(libVal.toObject(), mavenBase);
        if (!lib.isEmpty()) {
            libraries.append(lib);
        }
    }
    if (libraries.isEmpty()) {
        return QJsonObject();
    }

    versionJson["libraries"] = libraries;
    return versionJson;
}

QJsonObject FabricInstaller::generateVersionJson(const FabricVersionInfo &version)
{
    QJsonObject versionJson;

    const QString mcVersion = !version.minecraftVersion.isEmpty()
                                  ? version.minecraftVersion : m_currentMcVersion;
    const QString versionId = QString("fabric-loader-%1-%2").arg(version.fabricVersion, mcVersion);

    versionJson["id"] = versionId;
    versionJson["type"] = "release";
    versionJson["releaseTime"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    versionJson["time"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    versionJson["inheritsFrom"] = mcVersion;

    // 与官方 /profile/json 一致: mainClass = launcherMeta.mainClass.client
    // （现代 Fabric Loader 0.11+ 的 Knot 主类）
    const QJsonObject mainClassObj = version.launcherMeta.value("mainClass").toObject();
    QString mainClass = mainClassObj.value("client").toString();
    if (mainClass.isEmpty()) {
        mainClass = QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient");
    }
    versionJson["mainClass"] = mainClass;

    // 注意: 不写 arguments/minecraftArguments。启动时 mergeInheritsFromJson 会
    // 继承原版完整的 jvm/game 参数（含 -cp、-Djava.library.path 占位符），
    // 这正是官方 inheritsFrom 版本的设计。

    const QString mavenBase = getMavenBaseUrl();
    QJsonArray libraries;

    if (version.launcherMeta.isEmpty()) {
        // 兜底: 老版 Meta 未返回 launcherMeta，至少补齐 loader 与 intermediary
        const QString loaderName = QString("net.fabricmc:fabric-loader:%1").arg(version.fabricVersion);
        QJsonObject loaderLib;
        loaderLib["name"] = loaderName;
        loaderLib = libraryToMojangFormat(loaderLib, mavenBase);
        if (!loaderLib.isEmpty()) libraries.append(loaderLib);

        const QString intermediaryName = !version.intermediaryMaven.isEmpty()
            ? version.intermediaryMaven
            : QString("net.fabricmc:intermediary:%1").arg(mcVersion);
        QJsonObject intermediaryLib;
        intermediaryLib["name"] = intermediaryName;
        intermediaryLib = libraryToMojangFormat(intermediaryLib, mavenBase);
        if (!intermediaryLib.isEmpty()) libraries.append(intermediaryLib);
    } else {
        // libraries = client + common + main（与官方 profile/json 顺序一致）
        const QJsonObject libsObj = version.launcherMeta.value("libraries").toObject();
        const QStringList sections = { "client", "common", "main" };
        for (const QString &section : sections) {
            const QJsonArray sectionLibs = libsObj.value(section).toArray();
            for (const QJsonValue &libVal : sectionLibs) {
                QJsonObject lib = libraryToMojangFormat(libVal.toObject(), mavenBase);
                if (!lib.isEmpty()) {
                    libraries.append(lib);
                }
            }
        }

        // 兜底: 个别 Meta 版本的 common 中缺 intermediary，从版本条目补齐
        bool hasIntermediary = false;
        for (const QJsonValue &libVal : libraries) {
            if (libVal.toObject().value("name").toString()
                    == QString("net.fabricmc:intermediary:%1").arg(mcVersion)) {
                hasIntermediary = true;
                break;
            }
        }
        if (!hasIntermediary) {
            const QString intermediaryName = !version.intermediaryMaven.isEmpty()
                ? version.intermediaryMaven
                : QString("net.fabricmc:intermediary:%1").arg(mcVersion);
            QJsonObject intermediaryLib;
            intermediaryLib["name"] = intermediaryName;
            intermediaryLib = libraryToMojangFormat(intermediaryLib, mavenBase);
            if (!intermediaryLib.isEmpty()) libraries.append(intermediaryLib);
        }
    }

    // 兜底: 新版权 Meta 的 launcherMeta.main 为空，loader 本体不会被列出，
    // 缺了它 classpath 里就没有 Knot 主类，启动必然失败
    bool hasLoaderLib = false;
    for (const QJsonValue &libVal : libraries) {
        if (libVal.toObject().value("name").toString().startsWith("net.fabricmc:fabric-loader:")) {
            hasLoaderLib = true;
            break;
        }
    }
    if (!hasLoaderLib) {
        QJsonObject loaderLib;
        loaderLib["name"] = QString("net.fabricmc:fabric-loader:%1").arg(version.fabricVersion);
        loaderLib = libraryToMojangFormat(loaderLib, mavenBase);
        if (!loaderLib.isEmpty()) {
            libraries.append(loaderLib);
        }
    }

    versionJson["libraries"] = libraries;

    return versionJson;
}

bool FabricInstaller::downloadLibraries(const QJsonObject &versionJson, const QString &librariesPath,
                                        QStringList *failed)
{
    const QJsonArray libs = versionJson.value("libraries").toArray();
    const int total = libs.size();
    int done = 0;

    for (const QJsonValue &libVal : libs) {
        if (m_isCancelled) {
            return false;
        }

        const QJsonObject lib = libVal.toObject();
        const QJsonObject artifact = lib.value("downloads").toObject().value("artifact").toObject();
        const QString relPath = artifact.value("path").toString();
        const QString url = artifact.value("url").toString();
        const QString expectedSha1 = artifact.value("sha1").toString();
        const QString libName = lib.value("name").toString();

        if (!relPath.isEmpty() && !url.isEmpty()) {
            const QString filePath = librariesPath + "/" + relPath;

            bool needDownload = true;
            if (QFile::exists(filePath)) {
                if (expectedSha1.isEmpty() || verifySha1(filePath, expectedSha1)) {
                    needDownload = false;
                } else {
                    QFile::remove(filePath);
                }
            }

            if (needDownload) {
                QDir().mkpath(QFileInfo(filePath).absolutePath());

                // 镜像站可能缺个别文件（例如某些 Fabric 库在镜像上返回 403），
                // 失败后换另一个 Maven 仓库再试一次，避免整个安装因单个库失败而中断
                QStringList candidates;
                candidates << url;
                if (url.endsWith(relPath)) {
                    if (!url.contains("maven.fabricmc.net")) {
                        candidates << QStringLiteral("https://maven.fabricmc.net/") + relPath;
                    }
                    if (!url.contains("bangbang93.com")) {
                        candidates << QStringLiteral("https://bmclapi2.bangbang93.com/maven/") + relPath;
                    }
                }

                bool downloaded = false;
                for (const QString &candidate : candidates) {
                    if (candidate.isEmpty()) {
                        continue;
                    }
                    if (downloadFile(candidate, filePath, expectedSha1)) {
                        downloaded = true;
                        break;
                    }
                }

                if (!downloaded && failed) {
                    *failed << (libName.isEmpty() ? relPath : libName);
                }
            }
        }

        done++;
        const int progress = 60 + (done * 35) / qMax(1, total);
        emit installProgressUpdated(progress, tr("下载Fabric运行库 (%1/%2)").arg(done).arg(total));
    }

    return !failed || failed->isEmpty();
}

bool FabricInstaller::repairIncompleteVersionJson(const QString &instancePath,
                                                  const std::function<void(const QString &)> &log)
{
    auto logMsg = [&log](const QString &msg) {
        if (log) {
            log(msg);
        }
    };

    // ── 读取版本 JSON（与 GameLauncher::readVersionJson 相同的候选命名）──
    const QString dirName = QFileInfo(instancePath).fileName();
    QStringList candidates;
    candidates << (instancePath + "/" + dirName + ".json")
               << (instancePath + "/version.json");

    QString jsonPath;
    QJsonObject versionJson;
    for (const QString &candidate : candidates) {
        QFile file(candidate);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject()) {
            jsonPath = candidate;
            versionJson = doc.object();
            break;
        }
    }

    if (versionJson.isEmpty() || !versionJson.contains("inheritsFrom")) {
        return true;    // 非 Fabric（无继承），不处理
    }

    // ── 仅处理 Fabric 版本（mainClass 或 libraries 中含 fabricmc）──
    bool isFabric = versionJson.value("mainClass").toString().contains("fabricmc", Qt::CaseInsensitive);
    if (!isFabric && versionJson.value("libraries").isArray()) {
        for (const QJsonValue &v : versionJson.value("libraries").toArray()) {
            if (v.toObject().value("name").toString().startsWith("net.fabricmc:fabric-loader")) {
                isFabric = true;
                break;
            }
        }
    }
    if (!isFabric) {
        return true;
    }

    const QString mcVersion = versionJson.value("inheritsFrom").toString();
    if (mcVersion.isEmpty()) {
        return true;
    }

    // ── 必需库检查: intermediary（旧版安装器生成 JSON 的标志性缺失项）──
    bool hasIntermediary = false;
    if (versionJson.value("libraries").isArray()) {
        for (const QJsonValue &v : versionJson.value("libraries").toArray()) {
            if (v.toObject().value("name").toString().startsWith("net.fabricmc:intermediary:")) {
                hasIntermediary = true;
                break;
            }
        }
    }
    if (hasIntermediary) {
        return true;    // 库列表完整，无需修复
    }

    logMsg(QObject::tr("检测到 Fabric 版本缺少必需库 (intermediary/ASM/mixin)，正在从 Fabric Meta 修复..."));

    // ── loader 版本: 优先取 libraries 中的 fabric-loader 坐标，回退从 id 解析 ──
    QString loaderVersion;
    if (versionJson.value("libraries").isArray()) {
        for (const QJsonValue &v : versionJson.value("libraries").toArray()) {
            const QString name = v.toObject().value("name").toString();
            if (name.startsWith("net.fabricmc:fabric-loader:")) {
                loaderVersion = name.section(':', 2, 2);
                break;
            }
        }
    }
    if (loaderVersion.isEmpty()) {
        static const QRegularExpression idRe("fabric-loader-([^-]+)-");
        loaderVersion = idRe.match(versionJson.value("id").toString()).captured(1);
    }
    if (loaderVersion.isEmpty()) {
        logMsg(QObject::tr("警告: 无法确定 Fabric Loader 版本，跳过修复"));
        return true;
    }

    // ── 从 Fabric Meta 拉取官方 profile/json（镜像源依次回退）──
    QStringList metaBases;
    switch (SettingsManager::instance()->getFabricDownloadSource()) {
    case FabricDownloadSource::Official:
        metaBases << "https://meta.fabricmc.cn"
                  << "https://meta.fabricmc.net"
                  << "https://bmclapi2.bangbang93.com/fabric-meta";
        break;
    case FabricDownloadSource::BMCL:
    default:
        metaBases << "https://bmclapi2.bangbang93.com/fabric-meta"
                  << "https://meta.fabricmc.net";
        break;
    }

    QJsonObject profile;
    for (const QString &base : metaBases) {
        const QString url = QString("%1/v2/versions/loader/%2/%3/profile/json")
                                .arg(base, mcVersion, loaderVersion);
        bool ok = false;

        // 静态方法没有单例的网络管理器，用当前线程的复用管理器
        QNetworkRequest request((QUrl(url)));
        request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
        request.setTransferTimeout(30000);

        QNetworkReply *reply = threadNetworkManager()->get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        ok = (reply->error() == QNetworkReply::NoError);
        const QByteArray data = reply->readAll();
        reply->disconnect();
        delete reply;

        if (ok) {
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject() && doc.object().value("libraries").isArray()) {
                profile = doc.object();
                break;
            }
        }
    }

    if (profile.isEmpty()) {
        logMsg(QObject::tr("警告: 无法从 Fabric Meta 获取官方版本数据，跳过修复"));
        return true;
    }

    // ── 重建 libraries: 保留非 Fabric 相关条目，追加官方条目 ──
    auto isFabricOwned = [](const QString &name) {
        // 旧版安装器写入的无效条目（fabric-loader 路径错误、fabric-api 并非库、
        // com.mojang:minecraft 不是 Maven 坐标），以及将被官方条目取代的 ASM/mixin
        return name.startsWith("net.fabricmc:")
            || name.startsWith("org.ow2.asm:")
            || name.startsWith("com.mojang:minecraft");
    };

    QJsonArray newLibs;
    if (versionJson.value("libraries").isArray()) {
        for (const QJsonValue &v : versionJson.value("libraries").toArray()) {
            const QString name = v.toObject().value("name").toString();
            if (!isFabricOwned(name)) {
                newLibs.append(v);
            }
        }
    }

    const QString mavenBase = [&]() {
        switch (SettingsManager::instance()->getFabricDownloadSource()) {
        case FabricDownloadSource::Official:
            return QStringLiteral("https://maven.fabricmc.net/");
        case FabricDownloadSource::BMCL:
        default:
            return QStringLiteral("https://bmclapi2.bangbang93.com/maven/");
        }
    }();

    for (const QJsonValue &v : profile.value("libraries").toArray()) {
        QJsonObject lib = libraryToMojangFormat(v.toObject(), mavenBase);
        if (!lib.isEmpty()) {
            newLibs.append(lib);
        }
    }

    if (newLibs.isEmpty()) {
        logMsg(QObject::tr("警告: Fabric Meta 返回的库列表为空，跳过修复"));
        return true;
    }
    versionJson["libraries"] = newLibs;

    // ── 写回版本 JSON ──
    if (jsonPath.isEmpty()) {
        jsonPath = instancePath + "/" + dirName + ".json";
    }
    QFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logMsg(QObject::tr("警告: 无法写入修复后的版本 JSON: %1").arg(jsonPath));
        return true;
    }
    jsonFile.write(QJsonDocument(versionJson).toJson(QJsonDocument::Indented));
    jsonFile.close();
    logMsg(QObject::tr("Fabric 版本 JSON 修复完成 (%1)").arg(QFileInfo(jsonPath).fileName()));

    // ── 同步下载缺失的 Fabric 库（首次启动会跳过通用文件补全，必须就位）──
    // 库目录与 buildClasspath 的全局回退目录一致（版本隔离时也可见）
    const QString librariesPath = QFileInfo(instancePath).dir().absolutePath() + "/../libraries";
    FabricInstaller downloader;   // 复用成员 downloadFile（含重定向处理与 SHA1 校验）
    for (const QJsonValue &libVal : newLibs) {
        const QJsonObject artifact = libVal.toObject().value("downloads").toObject().value("artifact").toObject();
        const QString relPath = artifact.value("path").toString();
        const QString url = artifact.value("url").toString();
        if (relPath.isEmpty() || url.isEmpty()) {
            continue;
        }

        const QString filePath = librariesPath + "/" + relPath;
        if (QFile::exists(filePath)) {
            continue;
        }

        QDir().mkpath(QFileInfo(filePath).absolutePath());

        if (downloader.downloadFile(url, filePath, artifact.value("sha1").toString())) {
            logMsg(QObject::tr("已补齐 Fabric 库: %1").arg(relPath.section('/', -1)));
        } else {
            logMsg(QObject::tr("警告: Fabric 库下载失败: %1（重试启动时将再次补全）").arg(relPath));
        }
    }

    return true;
}

bool FabricInstaller::downloadFile(const QString &url, const QString &filePath,
                                  const QString &expectedSha1, int redirectDepth)
{
    if (redirectDepth > 10) {
        return false;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
    // 60 秒无数据传输即超时，避免连接停滞时嵌套事件循环永不退出导致界面卡死
    request.setTransferTimeout(60000);

    // 本函数可能在后台线程执行（加载器安装已移出 GUI 线程）。
    // 使用与本线程同亲和的复用管理器：连接可以复用，不必每个库文件都重新握手 TLS
    QNetworkReply *reply = threadNetworkManager()->get(request);
    m_currentReply = reply;

    // 统一收尾：先断开信号再立即销毁，避免当前线程退出时把连接遗留成
    // 「device not open」一类的传输层告警
    auto releaseReply = [this, reply]() {
        reply->disconnect();
        m_currentReply = nullptr;
        delete reply;
    };

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        reply->abort();
        releaseReply();
        return false;
    }

    QEventLoop loop;
    // 进度上报挂在 this（GUI 线程）上跨线程排队，任务簿记统一回到主线程；
    // 数据落盘不挂接收者，保持直接连接，在执行线程内写文件
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 bytesReceived, qint64 bytesTotal) {
        Q_UNUSED(reply);
        this->m_totalBytes = bytesTotal;
        this->m_downloadedBytes = bytesReceived;
        emit downloadProgressUpdated(bytesReceived, bytesTotal);
        updateTaskProgress(bytesReceived, bytesTotal);
    });

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::readyRead, [reply, &file]() {
        file.write(reply->readAll());
    });

    loop.exec();

    // finished 之前可能有尾部数据没等 readyRead 取走，先落盘再关文件
    if (reply->bytesAvailable() > 0) {
        file.write(reply->readAll());
    }
    file.close();

    if (m_isCancelled) {
        releaseReply();
        return false;
    }

    QNetworkReply::NetworkError error = reply->error();

    if (error == QNetworkReply::NoError) {
        QVariant redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectUrl.isValid()) {
            // resolved() 同时兼容绝对与相对 Location
            const QUrl target = reply->url().resolved(redirectUrl.toUrl());
            releaseReply();
            return downloadFile(target.toString(), filePath, expectedSha1, redirectDepth + 1);
        }

        releaseReply();

        if (!expectedSha1.isEmpty()) {
            if (!verifySha1(filePath, expectedSha1)) {
                QFile::remove(filePath);
                return false;
            }
        }

        return true;
    } else if (error == QNetworkReply::OperationCanceledError) {
        releaseReply();
        return false;
    } else {
        QString errorString = reply->errorString();
        releaseReply();
        qWarning() << "Download failed:" << errorString;
        return false;
    }
}

QByteArray FabricInstaller::fetchUrl(const QString &url, bool *ok)
{
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
    request.setTransferTimeout(30000);

    // 可能运行在后台线程，使用与本线程同亲和的复用管理器
    QNetworkReply *reply = threadNetworkManager()->get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool success = (reply->error() == QNetworkReply::NoError);
    // 读完缓冲再销毁（失败时也读一遍），避免残留数据在对象析构时被丢弃
    QByteArray data = reply->readAll();

    if (!success) {
        qWarning() << "Fetch failed:" << url << reply->errorString();
    }

    reply->disconnect();
    delete reply;
    if (ok) *ok = success;
    return data;
}

QString FabricInstaller::calculateSha1(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    file.close();

    return hash.result().toHex();
}

bool FabricInstaller::verifySha1(const QString &filePath, const QString &expectedSha1)
{
    QString actualSha1 = calculateSha1(filePath);
    return actualSha1.toLower() == expectedSha1.toLower();
}

void FabricInstaller::cancelInstall()
{
    if (!m_isInstalling) {
        return;
    }

    m_isCancelled = true;
    m_isInstalling = false;

    // reply 可能存活在执行安装的后台线程，跨线程直接 abort 不安全；
    // 通过排队调用投递到 reply 所属线程执行，未及中止的请求也会在
    // 传输超时（60 秒）内自行结束
    if (m_currentReply) {
        QMetaObject::invokeMethod(m_currentReply, &QNetworkReply::abort, Qt::QueuedConnection);
        m_currentReply = nullptr;
    }

    if (!m_currentTaskId.isEmpty()) {
        DownloadTaskManager::instance()->cancelTask(m_currentTaskId);
    }

    emit installCancelled();
}

bool FabricInstaller::isInstalling() const
{
    return m_isInstalling;
}

QString FabricInstaller::getJavaPath() const
{
    QString javaPath = SettingsManager::instance()->getJavaPath();

    if (javaPath.isEmpty()) {
        javaPath = qEnvironmentVariable("JAVA_HOME");
        if (!javaPath.isEmpty()) {
#ifdef Q_OS_WIN
            javaPath += "/bin/java.exe";
#else
            javaPath += "/bin/java";
#endif
        } else {
#ifdef Q_OS_WIN
            javaPath = "java.exe";
#else
            javaPath = "java";
#endif
        }
    }

    return javaPath;
}

QString FabricInstaller::getMinecraftLibrariesPath(const QString &instancePath) const
{
    return instancePath + "/libraries";
}
