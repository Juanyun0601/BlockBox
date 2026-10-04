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
#include <QTimer>

#include "utils/DownloadTaskManager.h"

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
            versions = parseVersionList(QString::fromUtf8(responseData));
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

QList<FabricVersionInfo> FabricInstaller::parseVersionList(const QString &jsonResponse)
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
        info.minecraftVersion = obj.value("game").toString();
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
        emit installFailed(tr("未找到指定版本的Fabric: %1").arg(fabricVersion));
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
    emit statusChanged(tr("正在创建版本目录..."));
    updateTaskStatus(tr("正在创建版本目录"));
    emit installProgressUpdated(10, tr("创建版本目录"));

    QString versionPath = createVersionDirectory(instancePath, m_currentMcVersion, m_currentFabricVersion);
    if (versionPath.isEmpty()) {
        emit installFailed(tr("创建版本目录失败"));
        m_isInstalling = false;
        return;
    }

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    emit statusChanged(tr("正在生成版本配置..."));
    updateTaskStatus(tr("正在生成版本配置"));
    emit installProgressUpdated(30, tr("生成版本配置"));

    QJsonObject versionJson = generateVersionJson(version);

    QString dirName = QFileInfo(versionPath).fileName();
    QString jsonFilePath = versionPath + "/" + dirName + ".json";
    QFile jsonFile(jsonFilePath);

    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit installFailed(tr("无法创建版本配置文件"));
        m_isInstalling = false;
        return;
    }

    QJsonDocument jsonDoc(versionJson);
    jsonFile.write(jsonDoc.toJson(QJsonDocument::Indented));
    jsonFile.close();

    if (m_isCancelled) {
        m_isInstalling = false;
        return;
    }

    // 下载 Fabric 运行库（ASM/mixin/intermediary/loader）。
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
    emit installCompleted(QString("fabric-loader-%1-%2").arg(m_currentFabricVersion, m_currentMcVersion));

    if (!m_currentTaskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_currentTaskId, DownloadTaskStatus::Completed,
                                                          tr("安装完成"));
    }
}

QString FabricInstaller::createVersionDirectory(const QString &instancePath,
                                              const QString &mcVersion,
                                              const QString &fabricVersion)
{
    QString versionId = QString("fabric-loader-%1-%2").arg(fabricVersion, mcVersion);
    QString versionPath = instancePath + "/versions/" + versionId;

    QDir versionDir(versionPath);
    if (!versionDir.exists()) {
        if (!versionDir.mkpath(".")) {
            return QString();
        }
    }

    return versionPath;
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
    artifact["url"] = mavenBaseUrl + relPath;
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
                if (!downloadFile(url, filePath, expectedSha1)) {
                    if (failed) {
                        *failed << (libName.isEmpty() ? relPath : libName);
                    }
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

        // 静态方法没有单例的网络管理器，使用局部 NAM（启动流程运行在主线程）
        QNetworkAccessManager nam;
        QNetworkRequest request((QUrl(url)));
        request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
        request.setTransferTimeout(30000);

        QNetworkReply *reply = nam.get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        ok = (reply->error() == QNetworkReply::NoError);
        const QByteArray data = ok ? reply->readAll() : QByteArray();
        reply->deleteLater();

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

    QNetworkReply *reply = m_networkManager->get(request);
    m_currentReply = reply;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        reply->abort();
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    QEventLoop loop;
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 bytesReceived, qint64 bytesTotal) {
        Q_UNUSED(reply);
        this->m_totalBytes = bytesTotal;
        this->m_downloadedBytes = bytesReceived;
        emit downloadProgressUpdated(bytesReceived, bytesTotal);
        updateTaskProgress(bytesReceived, bytesTotal);
    });

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::readyRead, this, [reply, &file]() {
        file.write(reply->readAll());
    });

    loop.exec();

    file.close();

    if (m_isCancelled) {
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    }

    QNetworkReply::NetworkError error = reply->error();

    if (error == QNetworkReply::NoError) {
        QVariant redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectUrl.isValid()) {
            // resolved() 同时兼容绝对与相对 Location
            const QUrl target = reply->url().resolved(redirectUrl.toUrl());
            reply->deleteLater();
            m_currentReply = nullptr;
            return downloadFile(target.toString(), filePath, expectedSha1, redirectDepth + 1);
        }

        reply->deleteLater();
        m_currentReply = nullptr;

        if (!expectedSha1.isEmpty()) {
            if (!verifySha1(filePath, expectedSha1)) {
                QFile::remove(filePath);
                return false;
            }
        }

        return true;
    } else if (error == QNetworkReply::OperationCanceledError) {
        reply->deleteLater();
        m_currentReply = nullptr;
        return false;
    } else {
        QString errorString = reply->errorString();
        reply->deleteLater();
        m_currentReply = nullptr;
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

    QNetworkReply *reply = m_networkManager->get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool success = (reply->error() == QNetworkReply::NoError);
    QByteArray data = success ? reply->readAll() : QByteArray();

    if (!success) {
        qWarning() << "Fetch failed:" << url << reply->errorString();
    }

    reply->deleteLater();
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

    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
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
