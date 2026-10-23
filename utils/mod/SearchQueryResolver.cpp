#include "SearchQueryResolver.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QDebug>

#include <climits>

namespace {

constexpr int kAcceptScore = 780;          // 候选名称的接受阈值（低于此分视为不相关）
constexpr int kRequestTimeoutMs = 6000;    // 在线查询超时
constexpr qint64 kCacheTtlSeconds = 7 * 24 * 3600;

/** 分类词：页面类型 → 该分类下的泛称（输入这些词时直接浏览全部） */
const QHash<QString, QStringList> &categoryWords()
{
    static const QHash<QString, QStringList> words = {
        {QStringLiteral("mod"),          {QStringLiteral("模组"), QStringLiteral("mod"), QStringLiteral("mods")}},
        {QStringLiteral("modpack"),      {QStringLiteral("整合包"), QStringLiteral("整合"), QStringLiteral("modpack"),
                                          QStringLiteral("整合包下载")}},
        {QStringLiteral("resourcepack"), {QStringLiteral("资源包"), QStringLiteral("材质包"), QStringLiteral("纹理包"),
                                          QStringLiteral("resourcepack"), QStringLiteral("resourcepacks")}},
        {QStringLiteral("shader"),       {QStringLiteral("光影"), QStringLiteral("光影包"), QStringLiteral("着色器"),
                                          QStringLiteral("shader"), QStringLiteral("shaders")}},
        {QStringLiteral("datapack"),     {QStringLiteral("数据包"), QStringLiteral("datapack"), QStringLiteral("datapacks")}},
        {QStringLiteral("world"),        {QStringLiteral("世界"), QStringLiteral("存档"), QStringLiteral("地图"),
                                          QStringLiteral("world"), QStringLiteral("map")}},
        {QStringLiteral("bedrock"),      {QStringLiteral("资源包"), QStringLiteral("行为包"), QStringLiteral("皮肤包"),
                                          QStringLiteral("附加包"), QStringLiteral("地图")}},
    };
    return words;
}

/** 翻译结果不能是这些过于宽泛的名字，否则会把搜索带到无关结果 */
bool isMeaninglessName(const QString &name)
{
    static const QSet<QString> blocked = {
        QStringLiteral("minecraft"), QStringLiteral("mod"), QStringLiteral("mods"),
        QStringLiteral("modpack"), QStringLiteral("resourcepack"), QStringLiteral("shader"),
        QStringLiteral("shaders"), QStringLiteral("datapack"), QStringLiteral("world"),
    };
    return blocked.contains(name.trimmed().toLower());
}

} // namespace

SearchQueryResolver::SearchQueryResolver(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_saveTimer(new QTimer(this))
    , m_baseUrl(QStringLiteral("https://mcmod-api.zkitefly.eu.org"))
{
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1500);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() {
        if (!m_dirty)
            return;
        m_dirty = false;
        saveDiskCache();
    });

    loadBundledNames();
    loadDiskCache();
}

SearchQueryResolver::~SearchQueryResolver()
{
    if (m_dirty)
        saveDiskCache();

    const QList<QNetworkReply *> replies = m_pending.keys();
    m_pending.clear();
    for (QNetworkReply *reply : replies) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void SearchQueryResolver::setBaseUrl(const QString &url)
{
    if (!url.trimmed().isEmpty())
        m_baseUrl = url.trimmed();
}

bool SearchQueryResolver::containsChinese(const QString &text)
{
    for (const QChar &ch : text) {
        const ushort code = ch.unicode();
        if ((code >= 0x4E00 && code <= 0x9FFF)     // 基本汉字
            || (code >= 0x3400 && code <= 0x4DBF)  // 扩展 A
            || (code >= 0xF900 && code <= 0xFAFF)) // 兼容汉字
            return true;
    }
    return false;
}

void SearchQueryResolver::resolve(quint64 requestId, const QString &query, const QString &category)
{
    const QString original = query.trimmed();
    if (original.isEmpty()) {
        emit resolved(requestId, original, QString(), QString());
        return;
    }

    // 1. 纯英文（含其它非中文）直接搜索
    if (!containsChinese(original)) {
        emit resolved(requestId, original, original, QString());
        return;
    }

    const QString normalized = normalize(original);

    // 2. 分类词：直接浏览该分类全部内容
    if (isCategoryWord(normalized, category)) {
        emit resolved(requestId, original, QString(), original);
        return;
    }

    // 3. 中英混输：优先取其中的英文部分（如「Create 机械动力」→「Create」）
    const QString ascii = asciiPart(original);
    if (!ascii.isEmpty() && ascii.size() < original.size()) {
        emit resolved(requestId, original, ascii, original);
        return;
    }

    // 4. 缓存
    bool cached = false;
    const QString cachedValue = cachedKeyword(normalized, &cached);
    if (cached) {
        emit resolved(requestId, original, cachedValue.isEmpty() ? original : cachedValue,
                      cachedValue.isEmpty() ? QString() : original);
        return;
    }

    // 5. 内置词表 / 运行时缓存
    QString matchedName;
    const QString local = translateLocally(normalized, &matchedName);
    if (!local.isEmpty() && !isMeaninglessName(local)) {
        CacheEntry entry;
        entry.keyword = local;
        entry.ok = true;
        entry.savedAt = QDateTime::currentSecsSinceEpoch();
        m_cache.insert(normalized, entry);
        m_dirty = true;
        scheduleSave();
        emit resolved(requestId, original, local, matchedName.isEmpty() ? original : matchedName);
        return;
    }

    // 6. 在线查询 MC 百科
    QUrl url(m_baseUrl + QStringLiteral("/s/key=")
             + QString::fromUtf8(QUrl::toPercentEncoding(original)));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBox/1.0"));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network->get(request);

    Pending pending;
    pending.requestId = requestId;
    pending.original = original;
    pending.normalized = normalized;
    pending.category = category;
    m_pending.insert(reply, pending);

    connect(reply, &QNetworkReply::finished, this, &SearchQueryResolver::onReplyFinished);

    QTimer::singleShot(kRequestTimeoutMs, this, [guard = QPointer<QNetworkReply>(reply)]() {
        if (guard && guard->isRunning())
            guard->abort();
    });
}

void SearchQueryResolver::onReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    const auto it = m_pending.find(reply);
    if (it == m_pending.end()) {
        reply->deleteLater();
        return;
    }
    const Pending pending = it.value();
    m_pending.erase(it);
    reply->deleteLater();

    QString keyword;
    QString matchedName;

    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isArray()) {
            const QJsonArray results = doc.array();
            int bestScore = 0;
            int bestLengthDiff = INT_MAX;
            QString bestEnglish;
            QString bestChinese;

            for (const QJsonValue &value : results) {
                if (!value.isObject())
                    continue;
                const QJsonObject item = value.toObject();
                const QJsonObject data = item.value(QStringLiteral("data")).toObject();

                QString chinese = data.value(QStringLiteral("chinese_name")).toString().trimmed();
                if (chinese.isEmpty()) {
                    // 兜底：从「[IC2] 工业时代2 (Industrial Craft 2)」这类标题里取中文段
                    const QString title = item.value(QStringLiteral("title")).toString().trimmed();
                    const QRegularExpression re(QStringLiteral("^\\s*(?:\\[[^\\]]*\\]\\s*)?(.+?)\\s*(?:\\([^\\)]*\\))?\\s*$"));
                    const QRegularExpressionMatch match = re.match(title);
                    if (match.hasMatch())
                        chinese = match.captured(1).trimmed();
                }
                if (chinese.isEmpty())
                    continue;

                const QString english = englishNameOf(item);
                if (english.isEmpty())
                    continue;

                const int score = scoreMatch(pending.normalized, normalize(chinese));
                // 同分时优先与查询长度最接近的中文名（避免选中「Yu的机械动力附属」这类长尾条目）
                const int lengthDiff = qAbs(chinese.size() - pending.original.size());
                if (score > bestScore || (score == bestScore && lengthDiff < bestLengthDiff)) {
                    bestScore = score;
                    bestLengthDiff = lengthDiff;
                    bestEnglish = english;
                    bestChinese = chinese;
                }
            }

            if (bestScore >= kAcceptScore && !isMeaninglessName(bestEnglish)) {
                keyword = bestEnglish;
                matchedName = bestChinese;
            }
        }
    }

    CacheEntry entry;
    entry.savedAt = QDateTime::currentSecsSinceEpoch();
    if (!keyword.isEmpty()) {
        entry.keyword = keyword;
        entry.ok = true;
        m_cache.insert(pending.normalized, entry);
        m_dirty = true;
        scheduleSave();
        emit resolved(pending.requestId, pending.original, keyword,
                      matchedName.isEmpty() ? pending.original : matchedName);
        return;
    }

    // 未命中：短期记住失败，避免每次输入都重复联网
    entry.ok = false;
    m_cache.insert(pending.normalized, entry);
    m_dirty = true;
    scheduleSave();
    emit resolved(pending.requestId, pending.original, pending.original, QString());
}

void SearchQueryResolver::loadBundledNames()
{
    QFile file(QStringLiteral(":/utils/mod/mcmod_names.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "SearchQueryResolver: 无法加载内置中文名词表";
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return;

    const QJsonObject root = doc.object();
    m_entries.reserve(root.size());
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString chinese = it.value().toString().trimmed();
        const QString english = it.key().trimmed();
        if (chinese.isEmpty() || english.isEmpty())
            continue;
        Entry entry;
        entry.chinese = chinese;
        entry.english = english;
        m_entries.append(entry);
    }
    qDebug() << "SearchQueryResolver: 内置词表条目数" << m_entries.size();
}

QString SearchQueryResolver::translateLocally(const QString &normalized, QString *matchedName) const
{
    if (normalized.isEmpty() || m_entries.isEmpty())
        return QString();

    int bestScore = 0;
    const Entry *best = nullptr;
    for (const Entry &entry : m_entries) {
        const int score = scoreMatch(normalized, normalize(entry.chinese));
        if (score > bestScore) {
            bestScore = score;
            best = &entry;
        }
    }

    if (!best || bestScore < kAcceptScore || best->english.isEmpty())
        return QString();

    if (matchedName)
        *matchedName = best->chinese;
    return best->english;
}

bool SearchQueryResolver::isCategoryWord(const QString &normalized, const QString &category)
{
    if (normalized.isEmpty() || category.isEmpty())
        return false;

    const QStringList words = categoryWords().value(category.trimmed().toLower());
    for (const QString &word : words) {
        if (normalize(word) == normalized)
            return true;
    }
    return false;
}

QString SearchQueryResolver::asciiPart(const QString &text)
{
    QString stripped;
    stripped.reserve(text.size());
    for (const QChar &ch : text) {
        const ushort code = ch.unicode();
        const bool cjk = (code >= 0x4E00 && code <= 0x9FFF)
                         || (code >= 0x3400 && code <= 0x4DBF)
                         || (code >= 0xF900 && code <= 0xFAFF)
                         || (code >= 0x3000 && code <= 0x303F)
                         || (code >= 0xFF00 && code <= 0xFFEF);
        stripped.append(cjk ? QLatin1Char(' ') : ch);
    }

    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9 '&.!_\\-+]*$"));
    static const QRegularExpression hasLetters(QStringLiteral("[A-Za-z]{2,}"));

    stripped = stripped.simplified();
    if (stripped.isEmpty() || !allowed.match(stripped).hasMatch())
        return QString();
    if (!hasLetters.match(stripped).hasMatch())
        return QString();
    return stripped;
}

QString SearchQueryResolver::normalize(const QString &text)
{
    QString normalized = text.normalized(QString::NormalizationForm_KC).toLower();

    QString compact;
    compact.reserve(normalized.size());
    for (const QChar &ch : normalized) {
        if (ch.isLetterOrNumber())
            compact.append(ch);
    }
    return compact;
}

int SearchQueryResolver::scoreMatch(const QString &query, const QString &chinese)
{
    if (query.isEmpty() || chinese.isEmpty())
        return 0;
    if (query == chinese)
        return 1000;

    if (query.startsWith(chinese))
        return 900 - qMin(query.size() - chinese.size(), 100);
    if (chinese.startsWith(query))
        return 880 - qMin(chinese.size() - query.size(), 100);
    if (chinese.contains(query))
        return 820;
    if (query.contains(chinese))
        return 800;
    return 0;
}

QString SearchQueryResolver::englishNameOf(const QJsonObject &entry)
{
    const QJsonObject data = entry.value(QStringLiteral("data")).toObject();

    // 1. sub_name 就是 MC 百科记录的英文名
    const QString subName = data.value(QStringLiteral("sub_name")).toString().trimmed();
    if (isUsableEnglishName(subName))
        return subName;

    // 2. 标题末尾括号内的英文，如「[IC2] 工业时代2 (Industrial Craft 2)」
    const QString title = entry.value(QStringLiteral("title")).toString().trimmed();
    if (!title.isEmpty()) {
        static const QRegularExpression tail(QStringLiteral("[\\(（]([^()（）]+)[\\)）]\\s*$"));
        const QRegularExpressionMatch match = tail.match(title);
        if (match.hasMatch()) {
            const QString candidate = match.captured(1).trimmed();
            if (isUsableEnglishName(candidate))
                return candidate;
        }
    }

    // 3. 光影、资源包等条目的英文名常常直接写在 chinese_name 里
    const QString chineseName = data.value(QStringLiteral("chinese_name")).toString().trimmed();
    if (isUsableEnglishName(chineseName))
        return chineseName;

    return QString();
}

bool SearchQueryResolver::isUsableEnglishName(const QString &name)
{
    if (name.isEmpty() || containsChinese(name))
        return false;

    int letters = 0;
    for (const QChar &ch : name) {
        if (ch.isLetter() && ch.unicode() < 0x250)
            ++letters;
    }
    return letters >= 2;
}

QString SearchQueryResolver::cachedKeyword(const QString &normalized, bool *found) const
{
    *found = false;
    const auto it = m_cache.constFind(normalized);
    if (it == m_cache.constEnd())
        return QString();

    if (it.value().savedAt > 0
        && QDateTime::currentSecsSinceEpoch() - it.value().savedAt > kCacheTtlSeconds) {
        return QString();
    }

    *found = true;
    return it.value().ok ? it.value().keyword : QString();
}

void SearchQueryResolver::scheduleSave()
{
    if (m_saveTimer && !m_saveTimer->isActive())
        m_saveTimer->start();
}

namespace {
QString diskCachePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/cache");
    return dir + QStringLiteral("/search_query_alias.json");
}
} // namespace

void SearchQueryResolver::loadDiskCache()
{
    QFile file(diskCachePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return;

    const QJsonObject entries = doc.object().value(QStringLiteral("entries")).toObject();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QJsonObject item = it.value().toObject();
        CacheEntry entry;
        entry.keyword = item.value(QStringLiteral("keyword")).toString();
        entry.ok = item.value(QStringLiteral("ok")).toBool();
        entry.savedAt = static_cast<qint64>(item.value(QStringLiteral("time")).toDouble());
        if (entry.savedAt > 0 && now - entry.savedAt > kCacheTtlSeconds)
            continue;
        m_cache.insert(it.key(), entry);
    }
}

void SearchQueryResolver::saveDiskCache()
{
    const QString path = diskCachePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject entries;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
        if (it.value().savedAt > 0 && now - it.value().savedAt > kCacheTtlSeconds)
            continue;
        QJsonObject item;
        item.insert(QStringLiteral("keyword"), it.value().ok ? it.value().keyword : QString());
        item.insert(QStringLiteral("ok"), it.value().ok);
        item.insert(QStringLiteral("time"), static_cast<double>(it.value().savedAt));
        entries.insert(it.key(), item);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), entries);
    root.insert(QStringLiteral("updated"), static_cast<double>(now));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
}
