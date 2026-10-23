/**
 * @file   RuntimeDependencyManager.cpp
 * @brief  移动端（安卓 / 鸿蒙）运行依赖（Java 运行环境等）检查与安装实现
 * @author BlockBox Team
 * @date   2026-10-23
 */
#include "utils/RuntimeDependencyManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include "components/AppMessageBox.h"
#include "utils/DownloadTaskManager.h"
#include "utils/GithubAccelerator.h"
#include "utils/RuntimeArchive.h"
#include "utils/Safety.h"
#include "utils/SettingsManager.h"

namespace {

constexpr int kRequestTimeoutMs = 8000;
constexpr int kDownloadTimeoutMs = 30000;
constexpr int kMaxAttempts = 3; // 直连、用户配置的加速节点、默认镜像
constexpr int kCheckTimeoutMs = 20000;
constexpr int kDownloadStageEnd = 70; // 下载阶段占总进度的百分比
constexpr const char *kMirrorPrefix = "https://gh-proxy.com/";
constexpr const char *kMarkerFileName = ".blockbox-dependency.json";
constexpr const char *kManifestSettingKey = "runtime_deps_manifest";

/**
 * @brief 默认依赖清单地址
 *
 * 安卓与鸿蒙的运行时不能混用（鸿蒙是 OpenHarmony + musl 系的运行时，
 * 安卓的 arm64-v8a JDK 无法在鸿蒙上执行），因此按平台分文件：
 *   - deps/android-runtime.json
 *   - deps/harmony-runtime.json
 * 维护者也可以只发布一份清单，用 "harmony-arm64-v8a" 这类 abi 键区分。
 */
QString defaultManifestUrl()
{
    static const QString kBase =
        QStringLiteral("https://raw.githubusercontent.com/Juanyun0601/BlockBox/main/deps/");
#if defined(Q_OS_HARMONY)
    const QString fileName = QStringLiteral("harmony-runtime.json");
#elif defined(Q_OS_ANDROID)
    const QString fileName = QStringLiteral("android-runtime.json");
#else
    const QString fileName = QStringLiteral("android-runtime.json");
#endif
    return kBase + fileName;
}

/** 按尝试次数拼接请求地址：直连 → 加速节点 → 兜底镜像 */
QString requestUrlFor(int attempt, const QString &url)
{
    if (attempt <= 0)
        return url;
    if (attempt == 1) {
        GithubAccelerator *accelerator = GithubAccelerator::instance();
        const QString accelerated = accelerator ? accelerator->accelerateUrl(url) : QString();
        if (!accelerated.isEmpty() && accelerated != url)
            return accelerated;
    }
    return QString::fromLatin1(kMirrorPrefix) + url;
}

/** 去掉 "sha256:" 前缀并转小写 */
QString normalizedSha256(const QString &digest)
{
    QString value = digest.trimmed().toLower();
    if (value.startsWith(QLatin1String("sha256:")))
        value = value.mid(7).trimmed();
    return value;
}

bool fileMatchesSha256(const QString &path, const QString &expected)
{
    const QString normalized = normalizedSha256(expected);
    if (normalized.isEmpty())
        return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return false;
    return QString::fromLatin1(hash.result().toHex()) == normalized;
}

RuntimeDependencyPackage packageFromJson(const QJsonObject &object, const QString &abi)
{
    RuntimeDependencyPackage package;
    package.abi = abi;
    package.url = object.value(QStringLiteral("url")).toString().trimmed();
    package.sizeBytes = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
    package.sha256 = object.value(QStringLiteral("sha256")).toString().trimmed();
    if (package.sha256.isEmpty())
        package.sha256 = object.value(QStringLiteral("digest")).toString().trimmed();
    return package;
}

/** 递归复制目录（跨分区无法直接改名时使用） */
bool copyDirectoryRecursively(const QString &sourceDir, const QString &destDir)
{
    QDir source(sourceDir);
    if (!source.exists())
        return false;
    if (!QDir().mkpath(destDir))
        return false;

    const QFileInfoList entries =
        source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &entry : entries) {
        const QString target = destDir + QLatin1Char('/') + entry.fileName();
        if (entry.isDir()) {
            if (!copyDirectoryRecursively(entry.absoluteFilePath(), target))
                return false;
        } else if (!QFile::copy(entry.absoluteFilePath(), target)) {
            return false;
        } else {
            QFile::setPermissions(target, entry.permissions());
        }
    }
    return true;
}

} // namespace

const RuntimeDependencyPackage *RuntimeDependency::packageForCurrentAbi() const
{
    const QStringList keys = RuntimeDependencyManager::abiKeyCandidates();
    const RuntimeDependencyPackage *fallback = nullptr;
    // 先按优先级匹配本机专属键，再退回通用包
    for (const QString &wanted : keys) {
        for (const RuntimeDependencyPackage &package : packages) {
            if (package.url.trimmed().isEmpty())
                continue;
            if (package.abi.trimmed().toLower() == wanted)
                return &package;
        }
    }
    for (const RuntimeDependencyPackage &package : packages) {
        if (package.url.trimmed().isEmpty())
            continue;
        const QString key = package.abi.trimmed().toLower();
        if (key.isEmpty() || key == QLatin1String("default") || key == QLatin1String("any"))
            fallback = &package;
    }
    return fallback;
}

QString RuntimeDependency::downloadSizeText() const
{
    const RuntimeDependencyPackage *package = packageForCurrentAbi();
    if (package && package->sizeBytes > 0) {
        const double megabytes = static_cast<double>(package->sizeBytes) / (1024.0 * 1024.0);
        const int precision = megabytes < 10.0 ? 1 : 0;
        return QObject::tr("约 %1 MB").arg(QString::number(megabytes, 'f', precision));
    }
    return sizeHint;
}

RuntimeDependencyManager *RuntimeDependencyManager::instance()
{
    static RuntimeDependencyManager *s_instance = new RuntimeDependencyManager();
    return s_instance;
}

RuntimeDependencyManager::RuntimeDependencyManager(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    refreshInstalledState();
}

/**
 * @brief 是否支持运行依赖
 *
 * 鸿蒙（Q_OS_HARMONY）与安卓一样没有可用的系统 Java，也要走依赖清单
 * 一键安装这条路。注意 qsystemdetection.h 里 __OHOS__ 会同时定义
 * Q_OS_LINUX，所以判断鸿蒙必须用 Q_OS_HARMONY 且放在 Q_OS_LINUX 之前。
 */
bool RuntimeDependencyManager::isSupportedPlatform()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_HARMONY)
    return true;
#else
    return false;
#endif
}

QString RuntimeDependencyManager::manifestUrl()
{
    const QString custom =
        SettingsManager::instance()->getProperty(QString::fromLatin1(kManifestSettingKey)).toString().trimmed();
    return custom.isEmpty() ? defaultManifestUrl() : custom;
}

QString RuntimeDependencyManager::installRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return base + QStringLiteral("/BlockBox");
}

QString RuntimeDependencyManager::installDirFor(const RuntimeDependency &dependency)
{
    const QString kind = dependency.kind.trimmed().toLower();
    const QString subDir = (kind == QLatin1String("runtime")) ? QStringLiteral("runtime") : QStringLiteral("java");
    QString id = dependency.id.trimmed();
    if (id.isEmpty())
        id = QStringLiteral("dependency");
    id = Safety::safeFileName(id, QStringLiteral("dependency"));
    return installRoot() + QLatin1Char('/') + subDir + QLatin1Char('/') + id;
}

QString RuntimeDependencyManager::currentAbiTag()
{
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    if (arch.contains(QLatin1String("arm64")) || arch == QLatin1String("aarch64"))
        return QStringLiteral("arm64-v8a");
    if (arch.startsWith(QLatin1String("armv7")) || arch == QLatin1String("arm"))
        return QStringLiteral("armeabi-v7a");
    if (arch.contains(QLatin1String("x86_64")) || arch == QLatin1String("x64"))
        return QStringLiteral("x86_64");
    if (arch.contains(QLatin1String("x86")) || arch == QLatin1String("i386"))
        return QStringLiteral("x86");
    return arch;
}

QStringList RuntimeDependencyManager::abiKeyCandidates()
{
    QStringList keys;
#if defined(Q_OS_HARMONY)
    // 鸿蒙端只认带 harmony / ohos 前缀的包，避免误装安卓或桌面 Linux 的运行时。
    // 如果维护者确实提供了两端通用的包，请在清单里用 "default" 键标出。
    const QString tag = currentAbiTag();
    keys << QStringLiteral("harmony-%1").arg(tag);
    if (tag == QLatin1String("arm64-v8a"))
        keys << QStringLiteral("ohos-arm64") << QStringLiteral("ohos-arm64-v8a");
#else
    keys << currentAbiTag();
#endif
    return keys;
}

QList<RuntimeDependency> RuntimeDependencyManager::pendingDependencies() const
{
    QList<RuntimeDependency> pending;
    for (const RuntimeDependency &dependency : m_dependencies) {
        if (dependency.isPending())
            pending.append(dependency);
    }
    return pending;
}

int RuntimeDependencyManager::pendingCount() const
{
    int count = 0;
    for (const RuntimeDependency &dependency : m_dependencies) {
        if (dependency.isPending())
            ++count;
    }
    return count;
}

RuntimeDependency RuntimeDependencyManager::dependencyById(const QString &id) const
{
    for (const RuntimeDependency &dependency : m_dependencies) {
        if (dependency.id == id)
            return dependency;
    }
    return RuntimeDependency();
}

void RuntimeDependencyManager::checkForUpdates(QWidget *parent, bool silent)
{
    if (!isSupportedPlatform()) {
        emit checkFinished(false, tr("当前平台无需安装运行依赖"));
        return;
    }
    if (m_checking)
        return;

    m_parent = parent;
    m_silent = silent;
    m_checking = true;
    m_attempt = 0;

    if (!m_checkTimer) {
        m_checkTimer = new QTimer(this);
        m_checkTimer->setSingleShot(true);
        connect(m_checkTimer, &QTimer::timeout, this, [this]() {
            if (!m_checking)
                return;
            finishCheck(false, tr("检查运行依赖超时，请稍后重试"));
        });
    }
    m_checkTimer->start(kCheckTimeoutMs);

    emit checkStarted();
    issueManifestRequest();
}

void RuntimeDependencyManager::issueManifestRequest()
{
    const QString manifest = manifestUrl();
    if (manifest.isEmpty()) {
        finishCheck(false, tr("未配置运行依赖清单地址"));
        return;
    }

    QNetworkRequest request{QUrl(requestUrlFor(m_attempt, manifest))};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBox"));
    request.setTransferTimeout(kRequestTimeoutMs);

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        handleManifestReply(reply);
    });
}

void RuntimeDependencyManager::handleManifestReply(QNetworkReply *reply)
{
    if (!m_checking)
        return; // 已超时或已完成

    if (reply->error() == QNetworkReply::NoError) {
        QString error;
        if (applyManifest(reply->readAll(), &error)) {
            finishCheck(true, QString());
            return;
        }
        if (m_attempt + 1 < kMaxAttempts) {
            ++m_attempt;
            issueManifestRequest();
            return;
        }
        finishCheck(false, error.isEmpty() ? tr("运行依赖清单解析失败") : error);
        return;
    }

    if (m_attempt + 1 < kMaxAttempts) {
        ++m_attempt;
        issueManifestRequest();
        return;
    }
    finishCheck(false, reply->errorString());
}

void RuntimeDependencyManager::finishCheck(bool success, const QString &error)
{
    m_checking = false;
    if (m_checkTimer)
        m_checkTimer->stop();

    emit checkFinished(success, error);

    if (!success && !m_silent && m_parent)
        AppMessageBox::warning(m_parent, tr("运行依赖"), tr("检查运行依赖失败：%1").arg(error));
}

bool RuntimeDependencyManager::applyManifest(const QByteArray &payload, QString *errorMessage)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (document.isNull() || !document.isObject()) {
        if (errorMessage)
            *errorMessage = tr("运行依赖清单格式不正确");
        return false;
    }

    const QJsonObject root = document.object();
    QJsonArray items;
    bool hasDependencyList = false;
    if (root.value(QStringLiteral("dependencies")).isArray()) {
        items = root.value(QStringLiteral("dependencies")).toArray();
        hasDependencyList = true;
    } else if (root.value(QStringLiteral("packages")).isArray()) {
        items = root.value(QStringLiteral("packages")).toArray();
        hasDependencyList = true;
    }

    if (!hasDependencyList) {
        if (errorMessage)
            *errorMessage = tr("运行依赖清单格式不正确（缺少依赖列表）");
        return false;
    }

    QList<RuntimeDependency> parsed;
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();

        RuntimeDependency dependency;
        dependency.id = object.value(QStringLiteral("id")).toString().trimmed();
        if (dependency.id.isEmpty())
            continue;
        dependency.name = object.value(QStringLiteral("name")).toString().trimmed();
        if (dependency.name.isEmpty())
            dependency.name = dependency.id;
        dependency.description = object.value(QStringLiteral("description")).toString().trimmed();
        dependency.kind = object.value(QStringLiteral("kind")).toString().trimmed().toLower();
        if (dependency.kind.isEmpty())
            dependency.kind = QStringLiteral("java");
        dependency.version = object.value(QStringLiteral("version")).toString().trimmed();
        dependency.required = object.value(QStringLiteral("required")).toBool(true);
        dependency.sizeHint = object.value(QStringLiteral("size")).toString().trimmed();

        const QJsonValue packagesValue = object.value(QStringLiteral("packages"));
        if (packagesValue.isObject()) {
            const QJsonObject packageMap = packagesValue.toObject();
            for (auto it = packageMap.begin(); it != packageMap.end(); ++it) {
                if (!it.value().isObject())
                    continue;
                const QString key = it.key().trimmed().toLower();
                const QString abi =
                    (key == QLatin1String("default") || key == QLatin1String("any")) ? QString() : key;
                const RuntimeDependencyPackage package = packageFromJson(it.value().toObject(), abi);
                if (!package.url.isEmpty())
                    dependency.packages.append(package);
            }
        } else if (packagesValue.isArray()) {
            const QJsonArray packageArray = packagesValue.toArray();
            for (const QJsonValue &packageValue : packageArray) {
                if (!packageValue.isObject())
                    continue;
                const QJsonObject packageObject = packageValue.toObject();
                const QString abi = packageObject.value(QStringLiteral("abi")).toString().trimmed().toLower();
                const RuntimeDependencyPackage package =
                    packageFromJson(packageObject, (abi == QLatin1String("default")) ? QString() : abi);
                if (!package.url.isEmpty())
                    dependency.packages.append(package);
            }
        }

        if (dependency.packages.isEmpty()) {
            const QString directUrl = object.value(QStringLiteral("url")).toString().trimmed();
            if (!directUrl.isEmpty()) {
                RuntimeDependencyPackage package;
                package.url = directUrl;
                package.sizeBytes = static_cast<qint64>(object.value(QStringLiteral("downloadSize")).toDouble());
                package.sha256 = object.value(QStringLiteral("sha256")).toString().trimmed();
                dependency.packages.append(package);
            }
        }

        if (dependency.packages.isEmpty())
            continue;

        markInstalledState(dependency);
        parsed.append(dependency);
    }

    if (parsed.isEmpty() && !items.isEmpty()) {
        if (errorMessage)
            *errorMessage = tr("运行依赖清单中没有可用的依赖项");
        return false;
    }

    m_dependencies = parsed;
    emit dependenciesChanged();
    return true;
}

void RuntimeDependencyManager::markInstalledState(RuntimeDependency &dependency) const
{
    dependency.installed = false;
    dependency.installedVersion.clear();

    const QString dir = installDirFor(dependency);
    if (!QDir(dir).exists())
        return;

    const bool wantsJava = dependency.kind.trimmed().compare(QLatin1String("java"), Qt::CaseInsensitive) == 0;
    const bool javaBinaryExists = QFileInfo::exists(dir + QStringLiteral("/bin/java"));

    QFile marker(dir + QLatin1Char('/') + QString::fromLatin1(kMarkerFileName));
    if (marker.open(QIODevice::ReadOnly)) {
        const QJsonObject object = QJsonDocument::fromJson(marker.readAll()).object();
        marker.close();
        if (wantsJava && !javaBinaryExists)
            return; // 标记在但运行时被清理过，视为未安装
        dependency.installed = true;
        dependency.installedVersion = object.value(QStringLiteral("version")).toString().trimmed();
        return;
    }

    // 没有标记文件：手动导入的运行时也认，只是版本未知
    const QFileInfoList entries =
        QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    if (entries.isEmpty())
        return;
    if (wantsJava && !javaBinaryExists)
        return;
    dependency.installed = true;
}

void RuntimeDependencyManager::refreshInstalledState()
{
    for (RuntimeDependency &dependency : m_dependencies)
        markInstalledState(dependency);
    emit dependenciesChanged();
}

void RuntimeDependencyManager::writeMarker(const RuntimeDependency &dependency, const QString &dir) const
{
    const RuntimeDependencyPackage *package = dependency.packageForCurrentAbi();

    QJsonObject object;
    object.insert(QStringLiteral("id"), dependency.id);
    object.insert(QStringLiteral("name"), dependency.name);
    object.insert(QStringLiteral("kind"), dependency.kind);
    object.insert(QStringLiteral("version"), dependency.version);
    object.insert(QStringLiteral("abi"), package ? package->abi : currentAbiTag());
    object.insert(QStringLiteral("installedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QFile marker(dir + QLatin1Char('/') + QString::fromLatin1(kMarkerFileName));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    marker.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    marker.close();
}

void RuntimeDependencyManager::applyJavaPermissions(const QString &dir) const
{
#ifdef Q_OS_UNIX
    const QDir binDir(dir + QStringLiteral("/bin"));
    if (!binDir.exists())
        return;
    const QFileInfoList files = binDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &file : files) {
        QFile::setPermissions(file.absoluteFilePath(),
                              QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup
                                  | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    }
#else
    Q_UNUSED(dir)
#endif
}

void RuntimeDependencyManager::install(const QString &id)
{
    if (id.isEmpty())
        return;

    const RuntimeDependency dependency = dependencyById(id);
    if (!dependency.id.isEmpty() && dependency.installed && !dependency.needsUpdate())
        return; // 已是最新版本

    if (!m_queue.contains(id))
        m_queue.append(id);
    if (!m_installing)
        startNextInstall();
}

void RuntimeDependencyManager::installAll()
{
    for (const RuntimeDependency &dependency : m_dependencies) {
        if (!dependency.isPending())
            continue;
        if (!m_queue.contains(dependency.id))
            m_queue.append(dependency.id);
    }
    m_anyFailure = false;
    m_lastFailureMessage.clear();
    if (!m_installing)
        startNextInstall();
}

bool RuntimeDependencyManager::remove(const QString &id)
{
    const RuntimeDependency dependency = dependencyById(id);
    if (dependency.id.isEmpty())
        return false;

    m_queue.removeAll(id);
    const QString dir = installDirFor(dependency);
    if (!QDir(dir).exists())
        return false;

    const bool ok = QDir(dir).removeRecursively();
    refreshInstalledState();
    return ok;
}

void RuntimeDependencyManager::cancelAll()
{
    m_queue.clear();
    if (m_downloadReply)
        m_downloadReply->abort();
}

void RuntimeDependencyManager::startNextInstall()
{
    if (m_installing)
        return;
    if (m_queue.isEmpty())
        return;

    const QString id = m_queue.takeFirst();
    beginInstall(id);
}

void RuntimeDependencyManager::beginInstall(const QString &id)
{
    const RuntimeDependency dependency = dependencyById(id);
    if (dependency.id.isEmpty()) {
        emit installFinished(id, false, tr("依赖项信息已失效，请重新检查更新"));
        startNextInstall();
        return;
    }

    const RuntimeDependencyPackage *package = dependency.packageForCurrentAbi();
    if (!package || package->url.trimmed().isEmpty()) {
        m_anyFailure = true;
        emit installFinished(id, false, tr("暂时没有适用于当前设备的 %1 安装包").arg(dependency.name));
        startNextInstall();
        return;
    }

    m_installing = true;
    m_currentId = id;

    const QString downloadDir = installRoot() + QStringLiteral("/update/deps");
    QDir().mkpath(downloadDir);

    // 按下载地址的扩展名决定落盘文件名（解压时再按文件头确认格式）
    const QString lowerFileName = QUrl(package->url).fileName().toLower();
    QString archiveSuffix = QStringLiteral(".zip");
    if (lowerFileName.endsWith(QLatin1String(".tar.gz")) || lowerFileName.endsWith(QLatin1String(".tgz")))
        archiveSuffix = QStringLiteral(".tar.gz");

    const QString archiveName =
        Safety::safeFileName(id, QStringLiteral("dependency")) + archiveSuffix;

    m_downloadPath = downloadDir + QLatin1Char('/') + archiveName;
    QFile::remove(m_downloadPath);

    m_stagingDir = installRoot() + QStringLiteral("/update/staging/")
        + Safety::safeFileName(id, QStringLiteral("dependency"));
    QDir(m_stagingDir).removeRecursively();
    QDir().mkpath(m_stagingDir);

    m_taskId = DownloadTaskManager::instance()->addTask(dependency.name, installDirFor(dependency),
                                                        dependency.version, QStringList{tr("运行依赖")});
    DownloadTaskManager::instance()->updateTaskDisplayStatus(m_taskId, tr("准备下载"));
    DownloadTaskManager::instance()->updateTaskStep(m_taskId, tr("下载 %1").arg(dependency.name));

    emitInstallProgress(id, 0, tr("开始下载"));
    issueDownloadRequest(dependency, *package);
}

void RuntimeDependencyManager::issueDownloadRequest(const RuntimeDependency &dependency,
                                                    const RuntimeDependencyPackage &package)
{
    QNetworkRequest request{QUrl(requestUrlFor(0, package.url))};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBox"));
    request.setTransferTimeout(kDownloadTimeoutMs);

    m_downloadFile = new QFile(m_downloadPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete m_downloadFile;
        m_downloadFile = nullptr;
        finishInstall(dependency.id, false, tr("无法写入下载目录，请检查存储空间"));
        return;
    }
    m_downloadReceived = 0;

    m_downloadReply = m_nam->get(request);

    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadFile && m_downloadReply)
            m_downloadFile->write(m_downloadReply->readAll());
    });

    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                m_downloadReceived = received;
                int percent = 0;
                if (total > 0)
                    percent = static_cast<int>(received * 100 / total);
                const int overall = percent * kDownloadStageEnd / 100;
                emitInstallProgress(m_currentId, overall, tr("下载中 %1%").arg(percent));
                if (!m_taskId.isEmpty()) {
                    DownloadTaskManager::instance()->updateTaskProgress(m_taskId, overall, received, total);
                    DownloadTaskManager::instance()->updateTaskDisplayStatus(m_taskId, tr("传输中"));
                }
            });

    connect(m_downloadReply, &QNetworkReply::finished, this, [this, dependency, package]() {
        handleDownloadReply(dependency, package);
    });
}

void RuntimeDependencyManager::handleDownloadReply(const RuntimeDependency &dependency,
                                                   const RuntimeDependencyPackage &package)
{
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;

    QString error;
    bool downloaded = false;

    if (!reply) {
        error = tr("下载连接已丢失");
    } else if (reply->error() == QNetworkReply::NoError) {
        if (m_downloadFile)
            m_downloadFile->write(reply->readAll());
        downloaded = true;
    } else {
        if (m_downloadFile)
            m_downloadFile->write(reply->readAll());
        error = (reply->error() == QNetworkReply::OperationCanceledError) ? tr("已取消安装")
                                                                          : reply->errorString();
    }

    if (m_downloadFile) {
        m_downloadFile->flush();
        m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
    if (reply)
        reply->deleteLater();

    if (!downloaded) {
        finishInstall(dependency.id, false, error.isEmpty() ? tr("下载失败") : error);
        return;
    }

    const QFileInfo info(m_downloadPath);
    if (!info.exists() || info.size() <= 0) {
        finishInstall(dependency.id, false, tr("下载的安装包为空"));
        return;
    }
    if (package.sizeBytes > 0 && info.size() != package.sizeBytes) {
        finishInstall(dependency.id, false, tr("安装包大小校验失败，请重试"));
        return;
    }
    if (!package.sha256.trimmed().isEmpty() && !fileMatchesSha256(m_downloadPath, package.sha256)) {
        finishInstall(dependency.id, false, tr("安装包完整性校验失败，请重试"));
        return;
    }

    emitInstallProgress(dependency.id, kDownloadStageEnd, tr("正在解压"));

    QString extractError;
    const QString id = dependency.id;
    const bool extracted = RuntimeArchive::extract(
        m_downloadPath, m_stagingDir, &extractError, [this, id](int percent, const QString &stage) {
            const int overall = kDownloadStageEnd + percent * (100 - kDownloadStageEnd) / 100;
            emitInstallProgress(id, overall, stage);
            if (!m_taskId.isEmpty()) {
                DownloadTaskManager::instance()->updateTaskProgress(m_taskId, overall, 0, 0);
                DownloadTaskManager::instance()->updateTaskStep(m_taskId, stage);
            }
            // 解压是同步的，定期让界面刷新进度
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        });

    if (!extracted) {
        finishInstall(dependency.id, false,
                      extractError.isEmpty() ? tr("安装包解压失败") : extractError);
        return;
    }

    // 压缩包通常多套一层目录，单顶层目录时下沉
    QString contentRoot = m_stagingDir;
    const QFileInfoList entries =
        QDir(m_stagingDir).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    if (entries.size() == 1 && entries.first().isDir())
        contentRoot = entries.first().absoluteFilePath();

    const QString target = installDirFor(dependency);
    QDir(target).removeRecursively();
    QDir().mkpath(QFileInfo(target).absolutePath());

    bool placed = QDir().rename(contentRoot, target);
    if (!placed)
        placed = copyDirectoryRecursively(contentRoot, target);
    if (!placed) {
        finishInstall(dependency.id, false, tr("无法写入运行目录，请检查存储空间"));
        return;
    }

    writeMarker(dependency, target);
    applyJavaPermissions(target);

    refreshInstalledState();
    emitInstallProgress(dependency.id, 100, tr("安装完成"));
    finishInstall(dependency.id, true, tr("%1 已安装完成").arg(dependency.name));
}

void RuntimeDependencyManager::finishInstall(const QString &id, bool success, const QString &message)
{
    cleanupInstallArtifacts();

    if (!m_taskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(
            m_taskId, success ? DownloadTaskStatus::Completed : DownloadTaskStatus::Failed, message);
        DownloadTaskManager::instance()->updateTaskDisplayStatus(
            m_taskId, success ? tr("已完成") : tr("失败"));
        m_taskId.clear();
    }

    if (!success) {
        m_anyFailure = true;
        m_lastFailureMessage = message;
    }

    m_currentId.clear();
    m_installing = false;

    emit installFinished(id, success, message);

    if (m_queue.isEmpty()) {
        const bool allSucceeded = !m_anyFailure;
        const QString failureText = m_lastFailureMessage;
        m_anyFailure = false;
        m_lastFailureMessage.clear();
        emit allInstallsFinished(allSucceeded);
        if (!m_silent && m_parent && !allSucceeded && !failureText.isEmpty())
            AppMessageBox::warning(m_parent, tr("运行依赖"), failureText);
        return;
    }

    startNextInstall();
}

void RuntimeDependencyManager::cleanupInstallArtifacts()
{
    if (!m_downloadPath.isEmpty()) {
        QFile::remove(m_downloadPath);
        m_downloadPath.clear();
    }
    if (!m_stagingDir.isEmpty()) {
        QDir(m_stagingDir).removeRecursively();
        m_stagingDir.clear();
    }
}

void RuntimeDependencyManager::emitInstallProgress(const QString &id, int percent, const QString &stage)
{
    emit installProgress(id, qBound(0, percent, 100), stage);
}
