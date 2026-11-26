/**
 * @file   AuthlibInjector.cpp
 * @brief  第三方登录（Yggdrasil / authlib-injector）游戏内注入支持实现
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 下载顺序：官方源 → BMCLAPI 镜像 → GitHub Release，任一个成功就停。
 * 网络请求用 QEventLoop + QTimer 做阻塞式超时（与 GameLauncher::downloadFile 同款），
 * 调用方只需要在启动前调用一次 ensureInstalled()，不必把整个启动流程改成异步。
 * jar 先写到 .part、校验 SHA-256 通过后再替换，失败不会留下损坏的 jar；
 * 失败后进入冷却期，避免每次启动都在同一个网络问题上干等。
 */
#include "utils/auth/AuthlibInjector.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <functional>

namespace {

// 单源超时：元数据很小给 8 秒；jar 三百多 KB，给 45 秒容忍慢速网络
const int kMetaTimeoutMs = 8 * 1000;
const int kJarTimeoutMs = 45 * 1000;

// 三个源加起来的总预算：首次用第三方账户启动时最多等这么久，
// 免得网络完全不通时一次启动卡上好几分钟（之后进入失败冷却，不会反复等）
const int kTotalBudgetMs = 100 * 1000;

// 小于 100KB 的文件不可能是完整的 authlib-injector.jar
const qint64 kMinJarBytes = 100 * 1024;

// 下载失败后的冷却时间
const int kFailureCooldownSeconds = 10 * 60;

QString g_lastError;         // 最近一次失败原因（面向用户的中文短句）
QDateTime g_cooldownUntil;   // 冷却截止时间，无效表示当前没有冷却

/** 一个下载源的定义 */
struct SourceDef
{
    QString label;    // 面向用户的源名，用于提示与报错
    QString metaUrl;  // latest.json 或 GitHub Release API 地址
    QString id;       // 写进版本标记的源标识
    bool github;      // 是否 GitHub Release API
};

/** 从源元数据里解析出来的下载信息 */
struct ArtifactInfo
{
    QString version;
    int buildNumber = 0;
    QString downloadUrl;
    QString sha256;
};

/** 进度回调可能是空的，这里统一收口 */
void reportProgress(const AuthlibInjector::ProgressCallback &progress, const QString &message)
{
    if (progress)
    {
        progress(message);
    }
}

/**
 * @brief 阻塞式 GET 请求
 *
 * 和 GameLauncher::downloadFile 一样用 QEventLoop + 单次 QTimer 实现超时，
 * 区别只是超时时长由调用方指定、并可附带下载进度回调。
 *
 * @param errorOut     失败原因（超时统一转成中文）
 * @param onProgress   可选进度回调（已接收字节、总字节；总字节未知时为 0）
 */
QByteArray httpGet(const QUrl &url, int timeoutMs, QString *errorOut,
                   const std::function<void(qint64, qint64)> &onProgress =
                       std::function<void(qint64, qint64)>())
{
    QNetworkAccessManager manager;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBox/1.0"));
    request.setRawHeader("Accept", "*/*");
    // GitHub Release 的资产会 302 到 objects.githubusercontent.com，必须跟随跳转
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    if (onProgress)
    {
        QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                         [onProgress](qint64 received, qint64 total) {
                             onProgress(received, total);
                         });
    }
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    QByteArray data;
    if (reply->error() == QNetworkReply::NoError)
    {
        data = reply->readAll();
    }
    else if (errorOut)
    {
        // 被定时器 abort 时 errorString() 是英文，换成用户看得懂的说法
        *errorOut = (reply->error() == QNetworkReply::OperationCanceledError)
                        ? QStringLiteral("连接超时")
                        : reply->errorString();
    }

    reply->deleteLater();
    return data;
}

/** 解析官方源 / BMCLAPI 的 latest.json（两者结构一致） */
bool parseArtifactMeta(const QByteArray &data, ArtifactInfo *info, QString *errorOut)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("下载信息格式错误");
        }
        return false;
    }

    const QJsonObject object = doc.object();
    const QJsonObject checksums = object.value(QStringLiteral("checksums")).toObject();

    info->version = object.value(QStringLiteral("version")).toString().trimmed();
    info->downloadUrl = object.value(QStringLiteral("download_url")).toString().trimmed();
    info->sha256 = checksums.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    info->buildNumber = object.value(QStringLiteral("build_number")).toInt();

    if (info->version.isEmpty() || info->downloadUrl.isEmpty())
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("下载信息缺少版本或地址");
        }
        return false;
    }
    return true;
}

/** 解析 GitHub Release API：tag_name 当版本号，assets 里挑 jar，body 里带构建号 */
bool parseGithubMeta(const QByteArray &data, ArtifactInfo *info, QString *errorOut)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("下载信息格式错误");
        }
        return false;
    }

    const QJsonObject object = doc.object();

    QString tag = object.value(QStringLiteral("tag_name")).toString().trimmed();
    if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
    {
        tag.remove(0, 1);
    }
    info->version = tag;

    const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets)
    {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        // 只认主 jar，跳过 sources / javadoc 之类的资产
        if (!name.endsWith(QStringLiteral(".jar"), Qt::CaseInsensitive))
        {
            continue;
        }
        if (!name.contains(QStringLiteral("authlib-injector"), Qt::CaseInsensitive))
        {
            continue;
        }

        info->downloadUrl = asset.value(QStringLiteral("browser_download_url")).toString().trimmed();
        // 新版 GitHub API 会带 digest（sha256:...），有就顺手用上；没有则只做 ZIP 头校验
        const QString digest = asset.value(QStringLiteral("digest")).toString().trimmed();
        if (digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive))
        {
            info->sha256 = digest.mid(7).trimmed().toLower();
        }
        break;
    }

    const QRegularExpression buildNumberPattern(QStringLiteral("Build number:\\s*(\\d+)"));
    const QRegularExpressionMatch buildNumberMatch =
        buildNumberPattern.match(object.value(QStringLiteral("body")).toString());
    if (buildNumberMatch.hasMatch())
    {
        info->buildNumber = buildNumberMatch.captured(1).toInt();
    }

    if (info->version.isEmpty() || info->downloadUrl.isEmpty())
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("下载信息缺少版本或地址");
        }
        return false;
    }
    return true;
}

/**
 * @brief 校验下载到的 jar 数据
 *
 * 有 SHA-256 就严格比对；没有（GitHub 旧接口）时至少确认文件是 ZIP 且大于 100KB，
 * 避免把错误页、半截文件当成 jar 装上。
 */
bool verifyJarData(const QByteArray &data, const QString &expectedSha256,
                   QString *actualSha256, QString *errorOut)
{
    if (data.size() <= kMinJarBytes)
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("下载文件不完整");
        }
        return false;
    }

    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    if (actualSha256)
    {
        *actualSha256 = QString::fromLatin1(hash);
    }

    if (!expectedSha256.isEmpty())
    {
        if (QString::fromLatin1(hash).compare(expectedSha256, Qt::CaseInsensitive) != 0)
        {
            if (errorOut)
            {
                *errorOut = QStringLiteral("文件校验失败");
            }
            return false;
        }
        return true;
    }

    // ZIP 本地文件头固定是 "PK\x03\x04"
    if (!data.startsWith(QByteArray("PK\x03\x04", 4)))
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("下载内容不是有效的 jar");
        }
        return false;
    }
    return true;
}

/** 写版本标记 .version.json，供 installedVersion() 和设置页读取 */
void writeVersionMark(const ArtifactInfo &info, const QString &sourceId, const QString &actualSha256)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), info.version);
    object.insert(QStringLiteral("build_number"), info.buildNumber);
    object.insert(QStringLiteral("sha256"), actualSha256);
    object.insert(QStringLiteral("source"), sourceId);
    object.insert(QStringLiteral("installed_at"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(AuthlibInjector::installDir() + QStringLiteral("/.version.json"));
    if (!file.open(QIODevice::WriteOnly))
    {
        return;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.commit();
}

} // namespace

QString AuthlibInjector::installDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
    {
        // 极少数环境拿不到通用数据目录，退回用户主目录，别拼出盘符根下的怪路径
        base = QDir::homePath() + QStringLiteral("/.local/share");
    }
    return base + QStringLiteral("/BlockBox/runtime/authlib-injector");
}

QString AuthlibInjector::jarPath()
{
    return installDir() + QStringLiteral("/authlib-injector.jar");
}

bool AuthlibInjector::isInstalled()
{
    const QFileInfo info(jarPath());
    return info.exists() && info.isFile() && info.size() > kMinJarBytes;
}

QString AuthlibInjector::installedVersion()
{
    QFile file(installDir() + QStringLiteral("/.version.json"));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QString();
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
    {
        return QString();
    }
    return doc.object().value(QStringLiteral("version")).toString();
}

bool AuthlibInjector::ensureInstalled(QString *errorOut, const ProgressCallback &progress)
{
    // 已经装好就直接用，本次运行期间不再联网
    if (isInstalled())
    {
        g_lastError.clear();
        g_cooldownUntil = QDateTime();
        if (errorOut)
        {
            errorOut->clear();
        }
        return true;
    }

    // 冷却期内直接失败，不再发起网络请求
    if (g_cooldownUntil.isValid() && QDateTime::currentDateTime() < g_cooldownUntil)
    {
        if (errorOut)
        {
            *errorOut = g_lastError.isEmpty()
                            ? QStringLiteral("外置登录组件暂不可用，请稍后重试")
                            : g_lastError;
        }
        return false;
    }

    QDir().mkpath(installDir());

    const QList<SourceDef> sources = {
        {QStringLiteral("官方源"),
         QStringLiteral("https://authlib-injector.yushi.moe/artifact/latest.json"),
         QStringLiteral("official"), false},
        {QStringLiteral("镜像源"),
         QStringLiteral("https://bmclapi2.bangbang93.com/mirrors/authlib-injector/artifact/latest.json"),
         QStringLiteral("bmclapi"), false},
        {QStringLiteral("GitHub"),
         QStringLiteral("https://api.github.com/repos/yushijinhun/authlib-injector/releases/latest"),
         QStringLiteral("github"), true},
    };

    QStringList failures;
    QElapsedTimer budgetTimer;
    budgetTimer.start();
    for (const SourceDef &source : sources)
    {
        // 总预算用完后不再换源，直接失败（避免一次启动卡几十分钟）
        int remainingMs = kTotalBudgetMs - static_cast<int>(budgetTimer.elapsed());
        if (remainingMs < 3000)
        {
            failures << QStringLiteral("%1：总用时已超过上限").arg(source.label);
            break;
        }

        reportProgress(progress, QStringLiteral("正在获取外置登录组件信息（%1）…").arg(source.label));

        QString metaError;
        const QByteArray metaData = httpGet(QUrl(source.metaUrl), qMin(kMetaTimeoutMs, remainingMs), &metaError);
        if (metaData.isEmpty())
        {
            failures << QStringLiteral("%1：%2")
                            .arg(source.label,
                                 metaError.isEmpty() ? QStringLiteral("连接失败") : metaError);
            continue;
        }

        ArtifactInfo info;
        QString parseError;
        const bool metaOk = source.github ? parseGithubMeta(metaData, &info, &parseError)
                                          : parseArtifactMeta(metaData, &info, &parseError);
        if (!metaOk)
        {
            failures << QStringLiteral("%1：%2").arg(source.label, parseError);
            continue;
        }

        reportProgress(progress, QStringLiteral("正在下载外置登录组件（%1）…").arg(source.label));

        remainingMs = kTotalBudgetMs - static_cast<int>(budgetTimer.elapsed());
        if (remainingMs < 3000)
        {
            failures << QStringLiteral("%1：总用时已超过上限").arg(source.label);
            break;
        }

        int lastPercent = -1;
        QString downloadError;
        const QByteArray jarData = httpGet(
            QUrl(info.downloadUrl), qMin(kJarTimeoutMs, remainingMs), &downloadError,
            [&progress, &lastPercent](qint64 received, qint64 total) {
                // 只按十位变化提示，避免百分比数字飞快刷屏
                if (!progress || total <= 0 || received <= 0)
                {
                    return;
                }
                const int percent = static_cast<int>(received * 100 / total);
                if (percent == lastPercent)
                {
                    return;
                }
                lastPercent = percent;
                progress(QStringLiteral("正在下载外置登录组件… %1%").arg(percent));
            });
        if (jarData.isEmpty())
        {
            failures << QStringLiteral("%1：%2")
                            .arg(source.label,
                                 downloadError.isEmpty() ? QStringLiteral("下载失败")
                                                         : downloadError);
            continue;
        }

        QString actualSha256;
        QString verifyError;
        if (!verifyJarData(jarData, info.sha256, &actualSha256, &verifyError))
        {
            failures << QStringLiteral("%1：%2").arg(source.label, verifyError);
            continue;
        }

        // 先落到 .part，确认写全后再替换旧 jar
        const QString partPath = jarPath() + QStringLiteral(".part");
        QFile partFile(partPath);
        if (!partFile.open(QIODevice::WriteOnly))
        {
            failures << QStringLiteral("%1：无法创建本地文件").arg(source.label);
            continue;
        }
        const qint64 written = partFile.write(jarData);
        partFile.close();
        if (written != jarData.size())
        {
            QFile::remove(partPath);
            failures << QStringLiteral("%1：写入本地文件失败").arg(source.label);
            continue;
        }

        QFile::remove(jarPath());
        if (!QFile::rename(partPath, jarPath()))
        {
            QFile::remove(partPath);
            failures << QStringLiteral("%1：写入本地文件失败").arg(source.label);
            continue;
        }

        writeVersionMark(info, source.id, actualSha256);

        g_lastError.clear();
        g_cooldownUntil = QDateTime();
        if (errorOut)
        {
            errorOut->clear();
        }
        reportProgress(progress, QStringLiteral("外置登录组件已就绪（%1）").arg(info.version));
        return true;
    }

    g_lastError = QStringLiteral("外置登录组件下载失败，请检查网络后重试");
    if (!failures.isEmpty())
    {
        g_lastError += QStringLiteral("（%1）").arg(failures.last());
    }
    g_cooldownUntil = QDateTime::currentDateTime().addSecs(kFailureCooldownSeconds);

    if (errorOut)
    {
        *errorOut = g_lastError;
    }
    return false;
}

QString AuthlibInjector::lastError()
{
    return g_lastError;
}

void AuthlibInjector::resetFailureCooldown()
{
    g_cooldownUntil = QDateTime();
    g_lastError.clear();
}

bool AuthlibInjector::supportsServer(const QString &serverUrl)
{
    const QString trimmed = serverUrl.trimmed();
    if (trimmed.isEmpty())
    {
        return false;
    }

    const QUrl url(trimmed);
    if (!url.isValid() || url.host().isEmpty())
    {
        return false;
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
    {
        return false;
    }

    // Mojang 官方接口不能交给 authlib-injector 接管
    static const QStringList mojangHosts = {
        QStringLiteral("minecraft.net"),
        QStringLiteral("mojang.com"),
        QStringLiteral("api.minecraftservices.com"),
    };
    const QString host = url.host().toLower();
    for (const QString &blocked : mojangHosts)
    {
        if (host == blocked || host.endsWith(QStringLiteral(".") + blocked))
        {
            return false;
        }
    }

    return true;
}

QStringList AuthlibInjector::jvmArgs(const QString &serverUrl)
{
    if (!isInstalled() || !supportsServer(serverUrl))
    {
        return QStringList();
    }

    // 统一成「根地址 + 结尾斜杠」，下面拼接口路径才不会缺分隔符
    QString root = serverUrl.trimmed();
    while (root.endsWith(QLatin1Char('/')))
    {
        root.chop(1);
    }
    root += QLatin1Char('/');

    QStringList args;
    args << QStringLiteral("-javaagent:%1=%2").arg(jarPath(), root);
    args << QStringLiteral("-Dminecraft.api.env=custom");
    args << QStringLiteral("-Dminecraft.api.auth.host=%1authserver").arg(root);
    args << QStringLiteral("-Dminecraft.api.account.host=%1sessionserver").arg(root);
    args << QStringLiteral("-Dminecraft.api.session.host=%1sessionserver").arg(root);
    args << QStringLiteral("-Dminecraft.api.services.host=%1minecraftservices").arg(root);
    return args;
}
