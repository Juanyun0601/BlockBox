/**
 * @file   WorldMapModuleManager.cpp
 * @brief  存档地图模块（CDN 按需下载）实现
 * @author BlockBox Team
 * @date   2026-11-22
 */

#include "WorldMapModuleManager.h"

#include "modules/worldmap/WorldMapModuleApi.h"
#include "utils/AppVersion.h"
#include "utils/AssetUrls.h"
#include "utils/RuntimeArchive.h"
#include "platform.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrl>
#include <QWidget>

namespace {

/** 模块在官网仓库 / CDN 上的目录（清单与压缩包同级） */
const char *kRemoteDir = "/assets/modules/worldmap/";

/** 模块清单 schema（以后结构不兼容时 +1） */
constexpr int kManifestSchema = 1;

/** 当前平台架构标识；返回空表示运行时不支持下载模块 */
QString currentAbiTag()
{
#if defined(BLOCKBOX_WORLDMAP_REMOTE_MODULE)
#  if defined(Q_OS_WIN)
    const QString arch = QSysInfo::currentCpuArchitecture();
    if (arch == QLatin1String("arm64"))
        return QStringLiteral("windows-arm64");
    return QStringLiteral("windows-x64");
#  elif defined(Q_OS_MACOS)
    const QString arch = QSysInfo::currentCpuArchitecture();
    return arch == QLatin1String("arm64") ? QStringLiteral("macos-arm64")
                                          : QStringLiteral("macos-x64");
#  elif defined(Q_OS_LINUX)
    const QString arch = QSysInfo::currentCpuArchitecture();
    if (arch.contains(QLatin1String("arm64")) || arch.contains(QLatin1String("aarch64")))
        return QStringLiteral("linux-arm64");
    return QStringLiteral("linux-x64");
#  else
    return QString();
#  endif
#else
    return QString();
#endif
}

QString markerPath()
{
    return WorldMapModuleManager::installRoot() + QStringLiteral("/module.json");
}

QString manifestPathForVersion()
{
    return QString::fromLatin1(kRemoteDir) + AppVersion::current()
         + QStringLiteral("/manifest.json");
}

QString sanitizeVersion(const QString &version)
{
    QString v = version;
    v.replace(QLatin1Char('/'), QLatin1Char('_'));
    v.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return v.isEmpty() ? QStringLiteral("unknown") : v;
}

} // namespace

WorldMapModuleManager *WorldMapModuleManager::instance()
{
    static WorldMapModuleManager *s_instance = new WorldMapModuleManager();
    return s_instance;
}

WorldMapModuleManager::WorldMapModuleManager(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

WorldMapModuleManager::~WorldMapModuleManager()
{
    // QLibrary 用 PreventUnloadHint 加载：进程退出时统一回收，
    // 避免地图页还在用的时候把动态库卸掉。
}

bool WorldMapModuleManager::isDownloadSupported()
{
    return !currentAbiTag().isEmpty();
}

QString WorldMapModuleManager::abiTag()
{
    return currentAbiTag();
}

QString WorldMapModuleManager::installRoot()
{
    const QString overrideDir = qEnvironmentVariable("BLOCKBOX_WORLDMAP_DIR");
    if (!overrideDir.trimmed().isEmpty())
        return QDir::cleanPath(overrideDir.trimmed());
    return QDir::cleanPath(Platform::getDataDirectory()
                           + QStringLiteral("/modules/worldmap"));
}

bool WorldMapModuleManager::isBusy() const
{
    switch (m_state) {
    case State::Preparing:
    case State::Downloading:
    case State::Verifying:
    case State::Extracting:
    case State::Loading:
        return true;
    default:
        return false;
    }
}

void WorldMapModuleManager::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void WorldMapModuleManager::failWith(const QString &message)
{
    m_error = message;
    m_received = 0;
    m_total = 0;
    qWarning() << "[WorldMapModule]" << message;
    setState(State::Failed);
    emit failed(message);
}

void WorldMapModuleManager::ensureReady()
{
    if (m_state == State::Ready || isBusy())
        return;

    // 动态库已经加载过（例如建页失败后重试）：直接回到可用状态，
    // 不再重复 QLibrary::load 出一份新的模块实例。
    if (m_createFn && m_library) {
        m_error.clear();
        setState(State::Ready);
        emit ready();
        return;
    }

    m_error.clear();
    m_lastManifestStatus = 0;
    setState(State::Preparing);

    // 调试入口：直接指定本地动态库，跳过清单与下载
    const QString overrideLib = qEnvironmentVariable("BLOCKBOX_WORLDMAP_MODULE");
    if (!overrideLib.trimmed().isEmpty()) {
        if (loadLibraryFile(overrideLib.trimmed()))
            return;
        // 显式指定的库加载失败就不要再去联网，避免把开发者的错掩盖掉
        failWith(tr("指定的地图模块无法加载：%1").arg(m_error));
        return;
    }

    if (tryLoadLocal())
        return;

    startManifestRequest();
}

void WorldMapModuleManager::retry()
{
    if (isBusy())
        return;
    m_error.clear();
    if (m_state == State::Failed)
        setState(State::Idle);
    ensureReady();
}

bool WorldMapModuleManager::readMarker(QString *moduleVersion, QString *abi,
                                       QString *sha256, QString *libraryName) const
{
    QFile file(markerPath());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("appVersion")).toString() != AppVersion::current())
        return false;
    if (root.value(QStringLiteral("abi")).toString() != currentAbiTag())
        return false;
    if (moduleVersion)
        *moduleVersion = root.value(QStringLiteral("moduleVersion")).toString();
    if (abi)
        *abi = root.value(QStringLiteral("abi")).toString();
    if (sha256)
        *sha256 = root.value(QStringLiteral("sha256")).toString();
    if (libraryName)
        *libraryName = root.value(QStringLiteral("library")).toString();
    return true;
}

void WorldMapModuleManager::writeMarker(const QString &moduleVersion, const QString &sha256,
                                        const QString &libraryName) const
{
    QDir().mkpath(installRoot());
    QJsonObject root;
    root.insert(QStringLiteral("appVersion"), AppVersion::current());
    root.insert(QStringLiteral("abi"), currentAbiTag());
    root.insert(QStringLiteral("moduleVersion"), moduleVersion);
    root.insert(QStringLiteral("sha256"), sha256);
    root.insert(QStringLiteral("library"), libraryName);
    root.insert(QStringLiteral("installedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QFile file(markerPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[WorldMapModule] 写入模块标记失败：" << markerPath();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void WorldMapModuleManager::clearMarker() const
{
    QFile::remove(markerPath());
}

bool WorldMapModuleManager::tryLoadLocal()
{
    QString moduleVersion;
    QString abi;
    QString sha;
    QString library;
    if (!readMarker(&moduleVersion, &abi, &sha, &library))
        return false;

    const QString path = installRoot() + QLatin1Char('/') + library;
    if (library.isEmpty() || !QFile::exists(path)) {
        clearMarker();
        return false;
    }
    if (loadLibraryFile(path)) {
        m_moduleVersion = moduleVersion;
        return true;
    }

    // 本地库坏了或与当前启动器不匹配：清掉标记，走重新下载
    clearMarker();
    QFile::remove(path);
    return false;
}

bool WorldMapModuleManager::loadLibraryFile(const QString &path)
{
    setState(State::Loading);
    auto *lib = new QLibrary(path, this);
    lib->setLoadHints(QLibrary::PreventUnloadHint | QLibrary::ResolveAllSymbolsHint);
    if (!lib->load()) {
        m_error = tr("地图模块加载失败：%1").arg(lib->errorString());
        lib->deleteLater();
        return false;
    }

    auto versionFn = reinterpret_cast<int (*)()>(
        lib->resolve(BlockBoxWorldMapModule::apiVersionSymbol()));
    if (!versionFn) {
        m_error = tr("地图模块不完整（缺少版本号入口）");
        lib->unload();
        lib->deleteLater();
        return false;
    }
    const int moduleApi = versionFn();
    if (moduleApi != BlockBoxWorldMapModule::kApiVersion) {
        m_error = tr("地图模块版本与启动器不匹配，请更新启动器后重试");
        lib->unload();
        lib->deleteLater();
        return false;
    }

    auto createFn = reinterpret_cast<BlockBoxWorldMapModule::CreatePageFunc>(
        lib->resolve(BlockBoxWorldMapModule::createPageSymbol()));
    if (!createFn) {
        m_error = tr("地图模块不完整（缺少建页入口）");
        lib->unload();
        lib->deleteLater();
        return false;
    }

    m_library = lib;
    m_createFn = createFn;
    m_error.clear();
    setState(State::Ready);
    emit ready();
    return true;
}

void WorldMapModuleManager::startManifestRequest()
{
    const QString envManifest = qEnvironmentVariable("BLOCKBOX_WORLDMAP_MANIFEST");
    if (!envManifest.trimmed().isEmpty()) {
        m_manifestUrls = QStringList{envManifest.trimmed()};
    } else {
        m_manifestUrls = AssetUrls::baseCandidates(manifestPathForVersion());
    }
    m_manifestIndex = 0;
    requestNextManifest();
}

void WorldMapModuleManager::requestNextManifest()
{
    if (m_manifestIndex >= m_manifestUrls.size()) {
        // 404 通常不是网络问题，而是这一版启动器的模块还没上传好；
        // 两种情况给不同的提示，免得用户反复检查网络。
        if (m_lastManifestStatus == 404 || m_lastManifestStatus == 403) {
            failWith(tr("这一版的地图模块还没有发布，请稍后再试或更新启动器"));
        } else {
            failWith(tr("无法连接服务器下载地图模块，请检查网络后重试"));
        }
        return;
    }
    const QString url = m_manifestUrls.at(m_manifestIndex++);

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BlockBox/%1 (worldmap module)")
                          .arg(AppVersion::current()));

    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        if (!reply)
            return;
        reply->deleteLater();
        handleManifestReply(reply);
    });
}

void WorldMapModuleManager::handleManifestReply(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        m_lastManifestStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        requestNextManifest();
        return;
    }

    const QByteArray payload = reply->readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // CDN 返回的可能是错误页；换下一个基址再试
        m_lastManifestStatus = 0;
        requestNextManifest();
        return;
    }

    const QJsonObject root = doc.object();
    const int schema = root.value(QStringLiteral("schema")).toInt(0);
    if (schema != kManifestSchema) {
        failWith(tr("地图模块清单格式过旧，请更新启动器"));
        return;
    }
    const int apiVersion = root.value(QStringLiteral("apiVersion")).toInt(0);
    if (apiVersion != BlockBoxWorldMapModule::kApiVersion) {
        failWith(tr("地图模块版本与启动器不匹配，请更新启动器后重试"));
        return;
    }

    const QString abi = currentAbiTag();
    QJsonObject chosen;
    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &value : files) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("abi")).toString() == abi) {
            chosen = item;
            break;
        }
    }
    if (chosen.isEmpty()) {
        failWith(tr("当前系统暂时没有可用的地图模块，请稍后再试"));
        return;
    }

    const QString moduleVersion =
        root.value(QStringLiteral("moduleVersion")).toString(QStringLiteral("1.0.0"));
    QString zipUrl = chosen.value(QStringLiteral("url")).toString();
    if (zipUrl.isEmpty()) {
        failWith(tr("地图模块清单缺少下载地址"));
        return;
    }
    zipUrl = reply->url().resolved(QUrl(zipUrl)).toString();

    const QString sha256 = chosen.value(QStringLiteral("sha256")).toString();
    const qint64 size = qint64(chosen.value(QStringLiteral("size")).toDouble(0));
    QString library = chosen.value(QStringLiteral("library")).toString();
    if (library.isEmpty())
        library = abiLibraryName();

    beginDownload(moduleVersion, zipUrl, sha256, size, library);
}

void WorldMapModuleManager::beginDownload(const QString &moduleVersion,
                                          const QString &zipUrl,
                                          const QString &sha256,
                                          qint64 expectedSize,
                                          const QString &libraryName)
{
    m_moduleVersion = moduleVersion;
    m_zipUrl = zipUrl;
    m_pendingSha = sha256;
    m_pendingLibrary = libraryName;
    m_expectedSize = expectedSize;
    m_received = 0;
    m_total = expectedSize > 0 ? expectedSize : 0;

    QDir().mkpath(installRoot());
    m_zipPath = installRoot() + QStringLiteral("/download-")
              + sanitizeVersion(moduleVersion) + QStringLiteral(".zip.tmp");
    QFile::remove(m_zipPath);

    QNetworkRequest request{QUrl(zipUrl)};
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BlockBox/%1 (worldmap module)")
                          .arg(AppVersion::current()));

    setState(State::Downloading);
    emit progressChanged(0, m_total);

    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        m_received = received;
        m_total = total;
        emit progressChanged(m_received, m_total);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        // reply 在 handleDownloadFinished 里统一读取与释放
        handleDownloadFinished();
    });
}

void WorldMapModuleManager::handleDownloadFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QFile::remove(m_zipPath);
        failWith(tr("地图模块下载失败：%1").arg(reply->errorString()));
        return;
    }

    const QByteArray payload = reply->readAll();
    if (payload.isEmpty()) {
        QFile::remove(m_zipPath);
        failWith(tr("地图模块下载失败：服务器返回了空文件"));
        return;
    }
    QFile zipFile(m_zipPath);
    if (!zipFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failWith(tr("地图模块保存失败：无法写入 %1").arg(m_zipPath));
        return;
    }
    zipFile.write(payload);
    zipFile.close();

    if (m_expectedSize > 0 && payload.size() != m_expectedSize) {
        QFile::remove(m_zipPath);
        failWith(tr("地图模块下载不完整，请重试"));
        return;
    }

    setState(State::Verifying);
    if (!m_pendingSha.isEmpty()) {
        QFile hashFile(m_zipPath);
        if (!hashFile.open(QIODevice::ReadOnly)) {
            failWith(tr("地图模块校验失败：无法读取下载文件"));
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&hashFile);
        const QString actual = QString::fromLatin1(hash.result().toHex());
        if (actual.compare(m_pendingSha, Qt::CaseInsensitive) != 0) {
            QFile::remove(m_zipPath);
            failWith(tr("地图模块校验失败（文件已损坏），请重试"));
            return;
        }
    }

    setState(State::Extracting);
    const QString extractDir = installRoot();
    QDir().mkpath(extractDir);
    QString error;
    const bool ok = RuntimeArchive::extract(m_zipPath, extractDir, &error,
                                            [this](int percent, const QString &stage) {
        Q_UNUSED(stage);
        emit progressChanged(percent, 100);
    });
    if (!ok) {
        QFile::remove(m_zipPath);
        failWith(tr("地图模块解压失败：%1").arg(error));
        return;
    }
    QFile::remove(m_zipPath);

    const QString libraryPath = extractDir + QLatin1Char('/') + m_pendingLibrary;
    if (!QFile::exists(libraryPath)) {
        failWith(tr("地图模块解压后缺少主文件：%1").arg(m_pendingLibrary));
        return;
    }

    finishWithLibrary(libraryPath, m_moduleVersion, m_pendingSha, m_pendingLibrary);
}

void WorldMapModuleManager::finishWithLibrary(const QString &libraryPath,
                                              const QString &moduleVersion,
                                              const QString &sha256,
                                              const QString &libraryName)
{
    if (!loadLibraryFile(libraryPath)) {
        const QString reason = m_error;
        QFile::remove(libraryPath);
        failWith(reason);
        return;
    }
    m_moduleVersion = moduleVersion;
    writeMarker(moduleVersion, sha256, libraryName);
    m_received = 0;
    m_total = 0;
    emit progressChanged(100, 100);
}

QString WorldMapModuleManager::abiLibraryName() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("blockbox_worldmap.dll");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("libblockbox_worldmap.dylib");
#else
    return QStringLiteral("libblockbox_worldmap.so");
#endif
}

QWidget *WorldMapModuleManager::createPage(QWidget *parent)
{
    if (!m_createFn || m_state != State::Ready)
        return nullptr;
    QWidget *page = m_createFn(parent);
    if (!page) {
        failWith(tr("地图模块初始化失败，请重试"));
        return nullptr;
    }
    return page;
}
