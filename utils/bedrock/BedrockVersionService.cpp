/**
 * @file   BedrockVersionService.cpp
 * @brief  基岩版版本信息服务实现
 * @author BlockBox Team
 */

#include "BedrockVersionService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutex>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

#include <algorithm>

namespace {
// mcappx 源开发接口要求的 User-Agent（参考 XMCL 的 MCAPPX_DEVELOPER_USER_AGENT）
const QString kMcappxUserAgent = QStringLiteral("mcappx_developer");
const QString kMcappxVersionUrl = QStringLiteral("https://data.mcappx.com/v2/bedrock.json");

const QString kMcapksListUrl = QStringLiteral("https://mcapks.net/api/get-vslist.php");
const QString kMcapksLinksUrl = QStringLiteral("https://mcapks.net/api/get-download.php");

// bbk.endyun.ltd（MC版本库）— Android APK 版本库
const QString kBbkVersionUrl = QStringLiteral("https://bbk.endyun.ltd/api/get_version");
// 站点支持的主版本家族（28.x → 1.2.x），与前端版本页一致
const QStringList kBbkMajors = {
    QStringLiteral("28.x"), QStringLiteral("27.x"), QStringLiteral("26.x"),
    QStringLiteral("1.21.x"), QStringLiteral("1.20.x"), QStringLiteral("1.19.x"),
    QStringLiteral("1.18.x"), QStringLiteral("1.17.x"), QStringLiteral("1.16.x"),
    QStringLiteral("1.15.x"), QStringLiteral("1.14.x"), QStringLiteral("1.13.x"),
    QStringLiteral("1.12.x"), QStringLiteral("1.11.x"), QStringLiteral("1.10.x"),
    QStringLiteral("1.9.x"), QStringLiteral("1.8.x"), QStringLiteral("1.7.x"),
    QStringLiteral("1.6.x"), QStringLiteral("1.5.x"), QStringLiteral("1.4.x"),
    QStringLiteral("1.3.x"), QStringLiteral("1.2.x")
};

QNetworkRequest makeRequest(const QUrl &url, const QString &userAgent)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent.isEmpty()
                                                          ? QStringLiteral("BlockBox/1.0")
                                                          : userAgent);
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(30000);
    return request;
}
} // namespace

BedrockVersionService* BedrockVersionService::instance()
{
    static BedrockVersionService *s_instance = new BedrockVersionService();
    return s_instance;
}

BedrockVersionService::BedrockVersionService(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_mcappxReply(nullptr)
    , m_mcapksReply(nullptr)
    , m_linksReply(nullptr)
    , m_bbkPending(0)
    , m_fetchWatchdog(new QTimer(this))
    , m_listEmitted(false)
    , m_mcappxDone(false)
    , m_mcapksDone(false)
    , m_bbkDone(false)
    , m_mcappxOk(false)
    , m_mcapksOk(false)
    , m_bbkOk(false)
{
    m_fetchWatchdog->setSingleShot(true);
    m_fetchWatchdog->setInterval(45000);
    connect(m_fetchWatchdog, &QTimer::timeout, this, &BedrockVersionService::onFetchWatchdogTimeout);
}

BedrockVersionService::~BedrockVersionService()
{
}

QString BedrockVersionService::sourceDisplayName(const QString &source)
{
    if (source == QLatin1String("mcappx"))
        return QStringLiteral("mcappx.com（Windows 版）");
    if (source == QLatin1String("mcapks"))
        return QStringLiteral("mcapks.net（Android 版）");
    if (source == QLatin1String("bbk"))
        return QStringLiteral("bbk.endyun.ltd（Android 版）");
    return source;
}

// ────────────────────────────────────────────────────────────
// 版本列表拉取
// ────────────────────────────────────────────────────────────

void BedrockVersionService::fetchVersionList()
{
    if (m_mcappxReply || m_mcapksReply || !m_bbkReplies.isEmpty())
        return;

    m_mcappxEntries.clear();
    m_mcapksEntries.clear();
    m_bbkEntries.clear();
    m_mcappxDone = false;
    m_mcapksDone = false;
    m_bbkDone = false;
    m_mcappxOk = false;
    m_mcapksOk = false;
    m_bbkOk = false;
    m_mcappxError.clear();
    m_mcapksError.clear();
    m_bbkError.clear();
    m_listEmitted = false;

    // 看门狗：45s 内未完成则强制结束，避免界面一直停在"获取中"
    m_fetchWatchdog->start();

    fetchMcappxList();
    fetchMcapksList();
    fetchBbkList();
}

void BedrockVersionService::fetchMcappxList()
{
    QNetworkReply *reply = m_nam->get(makeRequest(QUrl(kMcappxVersionUrl), kMcappxUserAgent));
    m_mcappxReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_mcappxReply != reply)
            return;
        handleMcappxReply(reply);
    });
}

void BedrockVersionService::fetchMcapksList()
{
    QNetworkReply *reply = m_nam->get(makeRequest(QUrl(kMcapksListUrl), QString()));
    m_mcapksReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_mcapksReply != reply)
            return;
        handleMcapksListReply(reply);
    });
}

void BedrockVersionService::handleMcappxReply(QNetworkReply *reply)
{
    m_mcappxReply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || (status >= 400 && status != 0)) {
        m_mcappxOk = false;
        m_mcappxError = reply->errorString();
        m_mcappxDone = true;
        emitCombined();
        return;
    }

    m_mcappxEntries = parseMcappx(reply->readAll());
    m_mcappxOk = true;
    m_mcappxDone = true;
    emitCombined();
}

void BedrockVersionService::handleMcapksListReply(QNetworkReply *reply)
{
    m_mcapksReply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || (status >= 400 && status != 0)) {
        m_mcapksOk = false;
        m_mcapksError = reply->errorString();
        m_mcapksDone = true;
        emitCombined();
        return;
    }

    m_mcapksEntries = parseMcapks(reply->readAll());
    m_mcapksOk = true;
    m_mcapksDone = true;
    emitCombined();
}

void BedrockVersionService::fetchBbkList()
{
    // 并行拉取各主版本家族（v=主版本家族 & b=2 表示全部类型）
    m_bbkPending = 0;
    m_bbkReplies.clear();
    for (const QString &major : kBbkMajors) {
        QNetworkRequest request = QNetworkRequest(QUrl(kBbkVersionUrl));
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBox/1.0"));
        request.setTransferTimeout(30000);

        const QByteArray body = QUrlQuery({ { QStringLiteral("v"), major },
                                            { QStringLiteral("b"), QStringLiteral("2") } })
                                    .toString(QUrl::FullyEncoded)
                                    .toUtf8();
        QNetworkReply *reply = m_nam->post(request, body);
        m_bbkReplies.append(reply);
        ++m_bbkPending;
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (!m_bbkReplies.contains(reply))
                return;
            handleBbkReply(reply);
        });
    }
    // 全部请求均已同步挂起，若无请求则视为失败
    if (m_bbkPending == 0) {
        m_bbkOk = false;
        m_bbkError = tr("版本库未配置主版本");
        m_bbkDone = true;
    }
}

void BedrockVersionService::handleBbkReply(QNetworkReply *reply)
{
    m_bbkReplies.removeAll(reply);
    --m_bbkPending;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && (status == 0 || status < 400)) {
        m_bbkEntries += parseBbk(reply->readAll());
    } else {
        m_bbkError = reply->errorString();
    }

    if (m_bbkPending > 0)
        return;
    m_bbkOk = m_bbkError.isEmpty();
    m_bbkDone = true;
    emitCombined();
}

void BedrockVersionService::emitCombined()
{
    if (!m_mcappxDone || !m_mcapksDone || !m_bbkDone)
        return;
    if (m_listEmitted)
        return;
    m_listEmitted = true;
    m_fetchWatchdog->stop();

    if (!m_mcappxOk)
        emit fetchError(sourceDisplayName(QStringLiteral("mcappx")), m_mcappxError);
    if (!m_mcapksOk)
        emit fetchError(sourceDisplayName(QStringLiteral("mcapks")), m_mcapksError);
    if (!m_bbkOk)
        emit fetchError(sourceDisplayName(QStringLiteral("bbk")), m_bbkError);

    QVector<BedrockVersionEntry> all;
    all.reserve(m_mcappxEntries.size() + m_mcapksEntries.size() + m_bbkEntries.size());

    // mcappx 优先（Windows 直链），同名版本以 mcappx 为准
    QSet<QString> seen;
    for (const BedrockVersionEntry &e : m_mcappxEntries) {
        seen.insert(e.version);
        all.append(e);
    }
    for (const BedrockVersionEntry &e : m_mcapksEntries) {
        if (seen.contains(e.version))
            continue;
        seen.insert(e.version);
        all.append(e);
    }
    for (const BedrockVersionEntry &e : m_bbkEntries) {
        if (seen.contains(e.version))
            continue;
        seen.insert(e.version);
        all.append(e);
    }

    // 按版本号降序（新版本在前）
    std::sort(all.begin(), all.end(), [](const BedrockVersionEntry &a, const BedrockVersionEntry &b) {
        return versionLess(b.version, a.version);
    });

    emit versionListFetched(all);
}

void BedrockVersionService::onFetchWatchdogTimeout()
{
    // 兜底：强制结束仍未完成的源，避免界面一直停留在"获取版本列表..."
    if (!m_mcappxDone) {
        m_mcappxDone = true;
        m_mcappxOk = false;
        if (m_mcappxError.isEmpty())
            m_mcappxError = tr("请求超时");
    }
    if (!m_mcapksDone) {
        m_mcapksDone = true;
        m_mcapksOk = false;
        if (m_mcapksError.isEmpty())
            m_mcapksError = tr("请求超时");
    }
    if (!m_bbkDone) {
        m_bbkDone = true;
        m_bbkOk = false;
        if (m_bbkError.isEmpty())
            m_bbkError = tr("请求超时");
        // 中断残留的 bbk 请求，避免继续占用连接
        for (QNetworkReply *r : qAsConst(m_bbkReplies)) {
            r->abort();
            r->deleteLater();
        }
        m_bbkReplies.clear();
        m_bbkPending = 0;
    }
    emitCombined();
}

// ────────────────────────────────────────────────────────────
// mcapks 网盘链接拉取
// ────────────────────────────────────────────────────────────

void BedrockVersionService::fetchDownloadLinks(const QString &version)
{
    if (m_linksReply)
        return;

    QUrl url(kMcapksLinksUrl);
    url.setQuery(QStringLiteral("version=%1&type=v8a").arg(QString(QUrl::toPercentEncoding(version))));
    QNetworkReply *reply = m_nam->get(makeRequest(url, QString()));
    m_linksReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, version]() {
        if (m_linksReply != reply)
            return;
        handleLinksReply(reply, version);
    });
}

void BedrockVersionService::handleLinksReply(QNetworkReply *reply, const QString &version)
{
    m_linksReply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || (status >= 400 && status != 0)) {
        emit downloadLinksFailed(version, reply->errorString());
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        emit downloadLinksFailed(version, tr("响应格式错误"));
        return;
    }

    const QJsonObject root = doc.object();
    if (!root.value("success").toBool(false)) {
        const QJsonObject err = root.value("error").toObject();
        const QString msg = err.value("message").toString();
        emit downloadLinksFailed(version, msg.isEmpty() ? tr("获取失败") : msg);
        return;
    }

    QVector<BedrockCloudLink> links;
    const QJsonArray downloads = root.value("data").toObject().value("downloads").toArray();
    for (const QJsonValue &v : downloads) {
        const QJsonObject o = v.toObject();
        BedrockCloudLink link;
        link.name = o.value("name").toString();
        link.url = o.value("url").toString();
        link.password = o.value("password").toString();
        if (link.name.isEmpty())
            link.name = link.url;
        if (!link.url.isEmpty())
            links.append(link);
    }

    emit downloadLinksFetched(version, links);
}

// ────────────────────────────────────────────────────────────
// mcappx JSON 解析
// ────────────────────────────────────────────────────────────

QVector<BedrockVersionEntry> BedrockVersionService::parseMcappx(const QByteArray &data)
{
    QVector<BedrockVersionEntry> result;

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return result;

    const QJsonObject root = doc.object();

    // 版本数据库位于 "From_mcappx.com" / "From_mcappx_com" 字段下
    // （兼容数据格式变动，回退到第一个非 CreationTime 的对象字段）
    QJsonObject versionsObject;
    for (const char *key : {"From_mcappx.com", "From_mcappx_com"}) {
        const QJsonValue v = root.value(QString::fromLatin1(key));
        if (v.isObject()) {
            versionsObject = v.toObject();
            break;
        }
    }
    if (versionsObject.isEmpty()) {
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            if (it.key() == QLatin1String("CreationTime"))
                continue;
            if (it.value().isObject()) {
                versionsObject = it.value().toObject();
                break;
            }
        }
    }

    for (auto it = versionsObject.constBegin(); it != versionsObject.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject build = it.value().toObject();

        BedrockVersionEntry entry;
        entry.source = QStringLiteral("mcappx");

        const QString id = build.value("ID").toString();
        entry.version = !id.isEmpty() ? id : it.key();

        const QString typeStr = build.value("Type").toString().toLower();
        if (typeStr == QLatin1String("beta"))
            entry.type = BedrockVersionType::Beta;
        else if (typeStr == QLatin1String("preview"))
            entry.type = BedrockVersionType::Preview;
        else
            entry.type = BedrockVersionType::Release;

        entry.date = build.value("Date").toString();

        // 从 Variations 中挑选 x64 / neutral 的直链（参考 XMCL：preferred 架构优先）
        const QJsonArray variations = build.value("Variations").toArray();
        QJsonArray ordered;
        for (const char *preferred : {"x64", "neutral"}) {
            const QString arch = QString::fromLatin1(preferred);
            for (const QJsonValue &v : variations) {
                const QJsonObject o = v.toObject();
                if (o.value("Arch").toString().toLower() == arch)
                    ordered.append(o);
            }
        }
        for (const QJsonValue &v : variations)
            ordered.append(v.toObject());

        for (const QJsonValue &v : ordered) {
            if (!v.isObject())
                continue;
            const QJsonObject o = v.toObject();
            const QJsonArray meta = o.value("MetaData").toArray();
            // 取该架构下最后一条非空元数据作为直链
            for (int i = meta.size() - 1; i >= 0; --i) {
                const QString text = meta.at(i).toString().trimmed();
                if (!text.isEmpty()) {
                    entry.hasDirectPackage = true;
                    entry.packageUrl = text;
                    entry.packageArch = o.value("Arch").toString();
                    break;
                }
            }
            if (entry.hasDirectPackage)
                break;
        }

        result.append(entry);
    }

    return result;
}

// ────────────────────────────────────────────────────────────
// mcapks JSON 解析
// ────────────────────────────────────────────────────────────

QVector<BedrockVersionEntry> BedrockVersionService::parseMcapks(const QByteArray &data)
{
    QVector<BedrockVersionEntry> result;

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return result;

    const QJsonObject root = doc.object();
    if (!root.value("success").toBool(false))
        return result;

    const QJsonArray versions = root.value("data").toObject().value("versions").toArray();
    for (const QJsonValue &v : versions) {
        const QJsonObject o = v.toObject();
        BedrockVersionEntry entry;
        entry.source = QStringLiteral("mcapks");
        entry.version = o.value("version").toString();
        entry.type = o.value("beta").toBool(false) ? BedrockVersionType::Beta
                                                   : BedrockVersionType::Release;
        entry.date = o.value("date").toString();
        entry.size = o.value("size").toString().trimmed();
        if (!entry.version.isEmpty())
            result.append(entry);
    }

    return result;
}

// ────────────────────────────────────────────────────────────
// bbk JSON 解析
// ────────────────────────────────────────────────────────────

QVector<BedrockVersionEntry> BedrockVersionService::parseBbk(const QByteArray &data)
{
    QVector<BedrockVersionEntry> result;

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return result;

    const QJsonObject root = doc.object();
    if (root.value("status").toInt(200) >= 400)
        return result;

    const QJsonArray versions = root.value("message").toArray();
    for (const QJsonValue &v : versions) {
        const QJsonObject o = v.toObject();

        BedrockVersionEntry entry;
        entry.source = QStringLiteral("bbk");
        entry.version = o.value("version_all").toString().trimmed();
        if (entry.version.isEmpty())
            entry.version = o.value("version").toString().trimmed();
        if (entry.version.isEmpty())
            continue;

        entry.type = o.value("is_beta").toInt(0) == 1 ? BedrockVersionType::Beta
                                                      : BedrockVersionType::Release;
        entry.date = o.value("update_time").toString().left(10);
        entry.size = o.value("file_size").toString().trimmed();

        // 网盘链接：每个网盘名 → {ARMv7, ARMv8}，优先 ARMv8（64 位）
        const QJsonObject linkObj = o.value("link").toObject();
        for (auto it = linkObj.constBegin(); it != linkObj.constEnd(); ++it) {
            const QJsonValue linkVal = it.value();
            QString url;
            if (linkVal.isObject()) {
                const QJsonObject archObj = linkVal.toObject();
                if (!archObj.value("ARMv8").toString().trimmed().isEmpty())
                    url = archObj.value("ARMv8").toString().trimmed();
                else
                    url = archObj.value("ARMv7").toString().trimmed();
            } else if (linkVal.isString()) {
                url = linkVal.toString().trimmed();
            }
            if (url.isEmpty())
                continue;

            BedrockCloudLink link;
            link.name = it.key();
            link.url = url;
            // 去重：避免同名网盘（如"123盘"与"123盘(原OneDrive_365)"）
            bool dup = false;
            for (const BedrockCloudLink &existing : entry.cloudLinks) {
                if (existing.name == link.name) {
                    dup = true;
                    break;
                }
            }
            if (!dup)
                entry.cloudLinks.append(link);
        }

        result.append(entry);
    }

    return result;
}

// ────────────────────────────────────────────────────────────
// 版本号比较（按数字段比较，如 26.50.25 > 1.21.101.1）
// ────────────────────────────────────────────────────────────

bool BedrockVersionService::versionLess(const QString &a, const QString &b)
{
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const qulonglong na = i < pa.size() ? pa.at(i).toULongLong() : 0;
        const qulonglong nb = i < pb.size() ? pb.at(i).toULongLong() : 0;
        if (na != nb)
            return na < nb;
    }
    return false;
}
