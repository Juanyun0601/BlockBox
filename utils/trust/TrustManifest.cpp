/**
 * @file   TrustManifest.cpp
 * @brief  可信分发公示清单的拉取 / 缓存 / 解析实现
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "utils/trust/TrustManifest.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSharedPointer>
#include <QUrl>

#include <memory>
#include <utility>

#include "platform.h"
#include "utils/AppVersion.h"
#include "utils/AssetUrls.h"

namespace {

constexpr int kTimeoutMs = 8000; // 公示清单很小，8 秒拿不到就换下一个源

/** 写缓存文件：先建目录，再用 QSaveFile 原子落盘 */
bool writeRawFile(const QString &path, const QByteArray &raw)
{
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(raw) != raw.size())
    {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

QByteArray readRawFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    const QByteArray raw = file.readAll();
    file.close();
    return raw;
}

/** 架构写法归一化：x86_64 / amd64 / x64 视为同一个 */
QString normalizeArch(const QString &arch)
{
    const QString value = arch.trimmed().toLower();
    if (value == QLatin1String("amd64") || value == QLatin1String("x86_64")
        || value == QLatin1String("x64"))
        return QStringLiteral("x64");
    if (value == QLatin1String("i386") || value == QLatin1String("i686")
        || value == QLatin1String("x86"))
        return QStringLiteral("x86");
    if (value == QLatin1String("aarch64"))
        return QStringLiteral("arm64");
    return value;
}

/** 从 packages 里挑出「主安装包」：Windows 优先 setup，其次第一个公示了哈希的条目 */
const TrustPackage *pickPrimaryPackage(const TrustBuild &build)
{
    const TrustPackage *firstHashed = nullptr;
    for (const TrustPackage &package : build.packages)
    {
        if (!package.hasSha256())
            continue;
        const QString name = package.name.toLower();
        if (name.contains(QLatin1String("setup")) || name.contains(QLatin1String("installer")))
            return &package;
        if (!firstHashed)
            firstHashed = &package;
    }
    return firstHashed;
}

} // namespace

TrustManifest *TrustManifest::instance()
{
    static TrustManifest s_instance;
    return &s_instance;
}

TrustManifest::TrustManifest(QObject *parent)
    : QObject(parent)
{
}

QString TrustManifest::trustPageUrl()
{
    return QStringLiteral("https://mcblockbox.top/trust/");
}

QString TrustManifest::cacheDir()
{
    const QString dataDir = Platform::getDataDirectory();
    if (dataDir.isEmpty())
        return QString();
    return dataDir + QStringLiteral("/trust");
}

QString TrustManifest::platformKey()
{
#if defined(Q_OS_ANDROID) || defined(ANDROID_OS)
    return QStringLiteral("android");
#elif defined(HARMONY_OS) || defined(Q_OS_HARMONY)
    return QStringLiteral("harmony");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QString TrustManifest::archKey()
{
#if defined(Q_PROCESSOR_ARM_64) || defined(__aarch64__) || defined(_M_ARM64)
    // 安卓按 ABI 写法，桌面按 x64 / arm64 写法
    return platformKey() == QLatin1String("android") ? QStringLiteral("arm64-v8a")
                                                     : QStringLiteral("arm64");
#elif defined(Q_PROCESSOR_ARM)
    return QStringLiteral("arm");
#elif defined(Q_PROCESSOR_X86_64) || defined(__x86_64__)
    return QStringLiteral("x64");
#else
    return QStringLiteral("x86");
#endif
}

QStringList TrustManifest::manifestUrlCandidates()
{
    return AssetUrls::baseCandidates(QStringLiteral("/trust/manifest.json"));
}

QString TrustManifest::normalizePath(const QString &path)
{
    QString value = path.trimmed();
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (value.startsWith(QLatin1String("./")))
        value.remove(0, 2);
    while (value.startsWith(QLatin1Char('/')))
        value.remove(0, 1);
    return value;
}

bool TrustManifest::parseManifest(const QByteArray &raw, QVector<TrustBuild> *builds,
                                  QString *updatedAt, QString *error)
{
    if (builds)
        builds->clear();
    if (updatedAt)
        updatedAt->clear();
    if (error)
        error->clear();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (error)
            *error = tr("公示清单不是合法的 JSON");
        return false;
    }
    if (!doc.isObject())
    {
        if (error)
            *error = tr("公示清单格式不正确");
        return false;
    }

    const QJsonObject root = doc.object();
    if (updatedAt)
        *updatedAt = root.value(QStringLiteral("updated")).toString().trimmed();

    QVector<TrustBuild> parsed;
    const QJsonArray buildArray = root.value(QStringLiteral("builds")).toArray();
    for (const QJsonValue &value : buildArray)
    {
        const QJsonObject object = value.toObject();
        TrustBuild build;
        build.version = object.value(QStringLiteral("version")).toString().trimmed();
        if (build.version.isEmpty())
            continue; // 没有版本号的条目没法用，直接跳过，不让界面出现空行
        build.channel = object.value(QStringLiteral("channel")).toString().trimmed().toLower();
        build.platform = object.value(QStringLiteral("platform")).toString().trimmed().toLower();
        build.arch = object.value(QStringLiteral("arch")).toString().trimmed().toLower();
        build.official = object.value(QStringLiteral("official")).toBool();
        build.publishedAt = object.value(QStringLiteral("publishedAt")).toString().trimmed();
        build.filesManifestUrl =
            object.value(QStringLiteral("filesManifest")).toString().trimmed();

        const QJsonArray packageArray = object.value(QStringLiteral("packages")).toArray();
        for (const QJsonValue &packageValue : packageArray)
        {
            const QJsonObject packageObject = packageValue.toObject();
            TrustPackage package;
            package.name = packageObject.value(QStringLiteral("name")).toString().trimmed();
            package.size =
                static_cast<qint64>(packageObject.value(QStringLiteral("size")).toDouble());
            package.sha256 =
                packageObject.value(QStringLiteral("sha256")).toString().trimmed().toLower();
            package.url = packageObject.value(QStringLiteral("url")).toString().trimmed();

            const QJsonValue signatureValue = packageObject.value(QStringLiteral("signature"));
            if (signatureValue.isObject())
            {
                const QJsonObject signature = signatureValue.toObject();
                package.hasSignature = true;
                package.signatureType =
                    signature.value(QStringLiteral("type")).toString().trimmed();
                package.signatureStatus =
                    signature.value(QStringLiteral("status")).toString().trimmed();
                package.signatureSubject =
                    signature.value(QStringLiteral("subject")).toString().trimmed();
                package.signatureIssuer =
                    signature.value(QStringLiteral("issuer")).toString().trimmed();
                package.signatureThumbprint =
                    signature.value(QStringLiteral("thumbprint")).toString().trimmed();
            }

            if (package.name.isEmpty() && package.sha256.isEmpty())
                continue;
            build.packages.append(package);
        }

        parsed.append(build);
    }

    if (builds)
        *builds = parsed;
    return true;
}

bool TrustManifest::parseFilesManifest(const QByteArray &raw, TrustFileManifest *manifest,
                                       QString *error)
{
    if (manifest)
        *manifest = TrustFileManifest();
    if (error)
        error->clear();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error)
            *error = tr("文件清单不是合法的 JSON");
        return false;
    }

    const QJsonObject root = doc.object();
    TrustFileManifest parsed;
    parsed.version = root.value(QStringLiteral("version")).toString().trimmed();
    parsed.platform = root.value(QStringLiteral("platform")).toString().trimmed().toLower();
    parsed.arch = root.value(QStringLiteral("arch")).toString().trimmed().toLower();
    parsed.generatedAt = root.value(QStringLiteral("generatedAt")).toString().trimmed();

    const QJsonArray fileArray = root.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &value : fileArray)
    {
        const QJsonObject object = value.toObject();
        TrustFileEntry entry;
        entry.path = normalizePath(object.value(QStringLiteral("path")).toString());
        if (entry.path.isEmpty())
            continue;
        entry.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
        entry.sha256 = object.value(QStringLiteral("sha256")).toString().trimmed().toLower();
        parsed.files.append(entry);
    }

    if (parsed.files.isEmpty())
    {
        if (error)
            *error = tr("文件清单里没有任何文件，可能还没生成完整");
        return false;
    }

    if (manifest)
        *manifest = parsed;
    return true;
}

TrustBuild TrustManifest::currentBuild() const
{
    const QString version = AppVersion::current();
    const QString platform = platformKey();
    const QString wantedArch = normalizeArch(archKey());

    const TrustBuild *archMatched = nullptr;
    const TrustBuild *platformMatched = nullptr;
    for (const TrustBuild &build : m_builds)
    {
        if (build.version.compare(version, Qt::CaseInsensitive) != 0)
            continue;
        if (build.platform.compare(platform, Qt::CaseInsensitive) != 0)
            continue;
        if (normalizeArch(build.arch) == wantedArch)
        {
            // 同一版本同一架构理论上只有一条；真有多条时优先官方构建
            if (!archMatched || (!archMatched->official && build.official))
                archMatched = &build;
            continue;
        }
        if (!platformMatched)
            platformMatched = &build;
    }
    if (archMatched)
        return *archMatched;
    if (platformMatched)
        return *platformMatched; // 架构写法对不上：同一版本先给一条，别让用户看到空的
    return TrustBuild();
}

QString TrustManifest::currentPackageSha256() const
{
    const TrustBuild build = currentBuild();
    if (!build.isValid())
        return QString();
    const TrustPackage *package = pickPrimaryPackage(build);
    return package ? package->sha256 : QString();
}

QString TrustManifest::currentPackageName() const
{
    const TrustBuild build = currentBuild();
    if (!build.isValid())
        return QString();
    const TrustPackage *package = pickPrimaryPackage(build);
    return package ? package->name : QString();
}

void TrustManifest::loadCache()
{
    if (m_cacheLoaded)
        return;
    m_cacheLoaded = true;

    const QString path = cacheDir() + QStringLiteral("/manifest.json");
    const QByteArray raw = readRawFile(path);
    if (raw.isEmpty())
        return;

    QVector<TrustBuild> builds;
    QString updatedAt;
    QString error;
    if (!parseManifest(raw, &builds, &updatedAt, &error))
    {
        qWarning() << "[TrustManifest]"
                   << "缓存的公示清单无法解析：" << error;
        return;
    }
    if (builds.isEmpty())
        return;
    m_builds = builds;
    m_updatedAt = updatedAt;
    m_sourceUrl = path;
}

void TrustManifest::refresh(bool forceNetwork)
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    // 先把缓存顶上：断网 / 首次打开设置页都能立刻显示「当前版本公示了什么」
    loadCache();
    if (!m_builds.isEmpty())
        emit manifestUpdated();

    if (m_loading)
        return;
    if (m_networkFetched && !forceNetwork)
        return;

    m_loading = true;
    m_lastError.clear();
    fetchFromNetwork(0);
}

void TrustManifest::fetchFromNetwork(int candidateIndex)
{
    const QStringList candidates = manifestUrlCandidates();
    if (candidateIndex >= candidates.size())
    {
        m_loading = false;
        if (m_lastError.isEmpty())
            m_lastError = tr("获取公示清单失败，请稍后再试");
        emit manifestFetchFailed(m_lastError);
        return;
    }

    const QUrl url(candidates.at(candidateIndex));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "BlockBox");
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(kTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, candidateIndex]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400)
        {
            if (status == 404 || status == 403)
                m_lastError = tr("公示清单尚未发布");
            else if (status > 0)
                m_lastError = tr("下载公示清单失败（HTTP %1）").arg(status);
            else
                m_lastError = tr("网络连接失败");
            fetchFromNetwork(candidateIndex + 1);
            return;
        }

        const QByteArray raw = reply->readAll();
        QVector<TrustBuild> builds;
        QString updatedAt;
        QString error;
        if (!parseManifest(raw, &builds, &updatedAt, &error))
        {
            m_lastError = error;
            fetchFromNetwork(candidateIndex + 1);
            return;
        }

        m_builds = builds;
        m_updatedAt = updatedAt;
        m_sourceUrl = reply->url().toString();
        m_networkFetched = true;
        m_loading = false;

        // 缓存原始字节：下次启动即使断网，也能按上次公示的内容做校验
        const QString dir = cacheDir();
        if (!dir.isEmpty() && !writeRawFile(dir + QStringLiteral("/manifest.json"), raw))
            qWarning() << "[TrustManifest]"
                       << "公示清单缓存写入失败：" << dir;
        emit manifestUpdated();
    });
}

QString TrustManifest::filesCachePath(const TrustBuild &build)
{
    const QString dir = cacheDir();
    if (dir.isEmpty() || build.version.isEmpty())
        return QString();
    const QString name =
        QStringLiteral("%1-%2-%3.json")
            .arg(build.version,
                 build.platform.isEmpty() ? platformKey() : build.platform,
                 build.arch.isEmpty() ? archKey() : build.arch);
    return dir + QStringLiteral("/files/") + name;
}

QStringList TrustManifest::filesUrlCandidates(const TrustBuild &build)
{
    QStringList urls;
    auto append = [&urls](const QString &url) {
        const QString trimmed = url.trimmed();
        if (!trimmed.isEmpty() && !urls.contains(trimmed))
            urls.append(trimmed);
    };

    // 清单里明确给了地址就以它为主，同时把 CDN / 官网互换再补一份（换域名后老清单也能用）
    const QString declared = build.filesManifestUrl.trimmed();
    append(declared);
    if (declared.startsWith(AssetUrls::originBase()))
        append(QString(declared).replace(AssetUrls::originBase(), AssetUrls::cdnBase()));
    else if (declared.startsWith(AssetUrls::cdnBase()))
        append(QString(declared).replace(AssetUrls::cdnBase(), AssetUrls::originBase()));

    // 兜底：按契约约定的固定路径拼
    const QString path =
        QStringLiteral("/trust/files/%1-%2-%3.json")
            .arg(build.version,
                 build.platform.isEmpty() ? platformKey() : build.platform,
                 build.arch.isEmpty() ? archKey() : build.arch);
    for (const QString &base : AssetUrls::baseCandidates(path))
        append(base);
    return urls;
}

void TrustManifest::requestCurrentFilesManifest(FilesHandler handler)
{
    const TrustBuild build = currentBuild();
    if (!build.isValid())
    {
        if (handler)
            handler(false, TrustFileManifest(), tr("当前版本暂未公示文件清单"));
        return;
    }

    const QString key =
        build.version + QLatin1Char('|') + build.platform + QLatin1Char('|') + build.arch;
    const auto cachedManifest = m_filesCache.constFind(key);
    if (cachedManifest != m_filesCache.constEnd() && cachedManifest->isValid())
    {
        const TrustFileManifest manifest = *cachedManifest;
        if (handler)
            handler(true, manifest, QString());
        return;
    }

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    // 一次请求可能要走多个候选地址（CDN → 官网 → 清单里给的地址），
    // 状态与「取下一条」的函数放在同一个共享结构里跟着异步回调走。
    struct FilesRequestState
    {
        QStringList urls;
        int index = 0;
        QString lastError;
        FilesHandler handler;
        std::function<void()> next; // 指向下一候选；回调结束时清空，避免自引用循环
    };
    auto state = QSharedPointer<FilesRequestState>::create();
    state->urls = filesUrlCandidates(build);
    state->handler = std::move(handler);

    // 统一出口：回调只触发一次，顺带打断 state → next → state 的引用环
    auto deliver = [state](bool ok, const TrustFileManifest &manifest, const QString &error) {
        state->next = nullptr;
        if (!state->handler)
            return;
        const FilesHandler done = state->handler;
        state->handler = FilesHandler();
        done(ok, manifest, error);
    };

    auto finishOk = [this, key, build, deliver](const TrustFileManifest &manifest,
                                                const QByteArray &raw) {
        m_filesCache.insert(key, manifest);
        const QString path = filesCachePath(build);
        if (!path.isEmpty() && !writeRawFile(path, raw))
            qWarning() << "[TrustManifest]"
                       << "文件清单缓存写入失败：" << path;
        deliver(true, manifest, QString());
    };

    auto finishWithCache = [this, key, build, state, deliver]() {
        // 网络不通：退回上次缓存的文件清单，至少还能看出本机文件有没有被改动
        TrustFileManifest cached;
        const QByteArray raw = readRawFile(filesCachePath(build));
        QString error;
        if (!raw.isEmpty() && parseFilesManifest(raw, &cached, &error) && cached.isValid())
        {
            m_filesCache.insert(key, cached);
            deliver(true, cached, tr("网络不可用，使用的是上次缓存的文件清单"));
            return;
        }
        deliver(false, TrustFileManifest(),
                state->lastError.isEmpty() ? tr("获取文件清单失败，请稍后再试")
                                           : state->lastError);
    };

    state->next = [this, state, finishOk, finishWithCache]() {
        if (state->index >= state->urls.size())
        {
            finishWithCache();
            return;
        }
        const QUrl url(state->urls.at(state->index++));
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "BlockBox");
        request.setRawHeader("Accept", "application/json");
        request.setTransferTimeout(kTimeoutMs);

        QNetworkReply *reply = m_network->get(request);
        connect(reply, &QNetworkReply::finished, this,
                [reply, state, finishOk]() {
                    reply->deleteLater();
                    const int status =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    if (reply->error() != QNetworkReply::NoError || status >= 400)
                    {
                        if (status == 404 || status == 403)
                            state->lastError = tr("当前版本暂未公示文件清单");
                        else if (status > 0)
                            state->lastError = tr("下载文件清单失败（HTTP %1）").arg(status);
                        else
                            state->lastError = tr("网络连接失败");
                        if (state->next)
                            state->next();
                        return;
                    }

                    const QByteArray raw = reply->readAll();
                    TrustFileManifest manifest;
                    QString error;
                    if (!parseFilesManifest(raw, &manifest, &error) || !manifest.isValid())
                    {
                        state->lastError =
                            error.isEmpty() ? tr("文件清单格式不正确") : error;
                        if (state->next)
                            state->next();
                        return;
                    }
                    finishOk(manifest, raw);
                });
    };
    if (state->next)
        state->next();
}
