/**
 * @file   LobbyContentFetcher.cpp
 * @brief  联机大厅「一键加入」内容补齐实现：按文件名去 Modrinth / CurseForge 找回并下载
 * @author BlockBox Team
 * @date   2026-11-14
 */

#include "LobbyContentFetcher.h"

#include "utils/mod/CurseForgeAPI.h"
#include "utils/mod/ModrinthAPI.h"
#include "utils/content/ContentDownloader.h"

#include <QDir>
#include <QFileInfo>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {

/** 单个网络请求最多等多久（毫秒）：下载源偶尔卡住时不让整个准备流程停住 */
constexpr int kStepTimeoutMs = 20000;

/** 下载阶段：多久没有任何进度就认为卡死（大文件正常下载时进度会不断重置它） */
constexpr int kDownloadIdleTimeoutMs = 120000;

/** 每个文件最多试几个搜索命中项目 */
constexpr int kMaxProjectTries = 3;

/** 搜索一次取多少条命中 */
constexpr int kSearchPageSize = 12;

/** 加载器 / 版本这类词不作为搜索词 */
bool isNoiseToken(const QString &token)
{
    static const QSet<QString> noise = {
        QStringLiteral("fabric"),  QStringLiteral("forge"),  QStringLiteral("neoforge"),
        QStringLiteral("quilt"),   QStringLiteral("liteloader"), QStringLiteral("mc"),
        QStringLiteral("minecraft"), QStringLiteral("client"), QStringLiteral("server"),
        QStringLiteral("universal"), QStringLiteral("release"), QStringLiteral("beta"),
        QStringLiteral("alpha"),   QStringLiteral("rc"),      QStringLiteral("snapshot"),
        QStringLiteral("shaders"), QStringLiteral("shader"),  QStringLiteral("resourcepack"),
        QStringLiteral("resourcepacks"), QStringLiteral("mod"), QStringLiteral("jar"),
        QStringLiteral("zip"),     QStringLiteral("win"),     QStringLiteral("windows"),
        QStringLiteral("linux"),   QStringLiteral("mac"),     QStringLiteral("x64"),
        QStringLiteral("x86"),     QStringLiteral("arm"),     QStringLiteral("all")
    };
    return noise.contains(token.toLower());
}

/** 1.20.1 / v2 / 15.2.0.27 / 1.20.1b 这类版本号（32x / 64x 这种分辨率不算版本） */
bool isVersionToken(const QString &token)
{
    static const QRegularExpression resolutionRe(
        QStringLiteral("^\\d+x$"), QRegularExpression::CaseInsensitiveOption);
    if (resolutionRe.match(token).hasMatch())
        return false;
    static const QRegularExpression re(
        QStringLiteral("^v?\\d+(\\.\\d+)*[a-z]?\\d*$"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(token).hasMatch();
}

/** 文件名拆成有意义的词：去掉扩展名、版本号、加载器、平台词 */
QStringList meaningfulTokens(const QString &fileName)
{
    QString base = QFileInfo(fileName).completeBaseName();
    if (base.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive))
        base.chop(9);
    if (base.isEmpty())
        base = fileName;

    QStringList raw;
    for (const QString &piece : base.split(QRegularExpression(QStringLiteral("[\\s\\-_.+()\\[\\]{}#,]+")),
                                           Qt::SkipEmptyParts))
    {
        const QString token = piece.trimmed();
        if (token.isEmpty() || isVersionToken(token) || isNoiseToken(token))
            continue;
        raw.append(token);
    }
    return raw;
}

/** 生成搜索词：先用文件名的前缀部分，再退化到所有保留的词 */
QStringList buildSearchQueries(const QString &fileName)
{
    const QStringList tokens = meaningfulTokens(fileName);
    if (tokens.isEmpty())
        return {QFileInfo(fileName).completeBaseName()};

    // 前缀：从第一个词开始，遇到版本号 / 加载器词就停（"sodium-fabric-0.5.11" → "sodium"）
    QString baseName = QFileInfo(fileName).completeBaseName();
    QStringList prefix;
    for (const QString &piece :
         baseName.split(QRegularExpression(QStringLiteral("[\\s\\-_.+()\\[\\]{}#,]+")),
                        Qt::SkipEmptyParts))
    {
        if (piece.isEmpty())
            continue;
        if (isVersionToken(piece) || isNoiseToken(piece))
            break;
        prefix.append(piece);
    }

    QStringList queries;
    const QString prefixQuery = prefix.join(QLatin1Char(' ')).trimmed();
    const QString allQuery = tokens.join(QLatin1Char(' ')).trimmed();
    if (!prefixQuery.isEmpty())
        queries.append(prefixQuery);
    if (!allQuery.isEmpty() && allQuery.compare(prefixQuery, Qt::CaseInsensitive) != 0)
        queries.append(allQuery);
    if (queries.isEmpty())
        queries.append(allQuery.isEmpty() ? QFileInfo(fileName).completeBaseName() : allQuery);
    return queries;
}

/** 名称归一化：只留字母数字和汉字，用来比相似度 */
QString normalizeName(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar &ch : text)
    {
        if (ch.isLetterOrNumber())
            out.append(ch.toLower());
    }
    return out;
}

/** 名称相似度：完全相同 > 包含 > 词都对得上 */
int nameSimilarity(const QString &query, const QString &candidate)
{
    const QString q = normalizeName(query);
    const QString c = normalizeName(candidate);
    if (q.isEmpty() || c.isEmpty())
        return 0;
    if (q == c)
        return 1000;
    if (c.startsWith(q))
        return 700;
    if (c.contains(q) || q.contains(c))
        return 500;

    int hits = 0;
    for (const QString &token : query.split(QLatin1Char(' '), Qt::SkipEmptyParts))
    {
        const QString t = normalizeName(token);
        if (!t.isEmpty() && c.contains(t))
            ++hits;
    }
    return hits > 0 ? 100 * hits : 0;
}

/** 内容类型 → 存储目录（与 ContentDownloader 保持一致） */
QString folderFor(ContentType type)
{
    return ContentTypeConfig::getConfig(type).folderName;
}

/** 文件名里是否已经有这个名字（本地实例里） */
bool localHasFile(const QString &instancePath, ContentType type, const QString &fileName)
{
    const QString dir = instancePath + QLatin1Char('/') + folderFor(type);
    return QFileInfo::exists(dir + QLatin1Char('/') + fileName);
}

} // namespace

LobbyContentFetcher::LobbyContentFetcher(QObject *parent)
    : QObject(parent)
{
    m_downloader = new ContentDownloader(this);
    m_mr = new ModrinthAPI(this);
    m_cf = new CurseForgeAPI(this);
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(kStepTimeoutMs);

    connect(m_watchdog, &QTimer::timeout, this, [this]() {
        // 超时：这一项直接算失败，接着做下一项，别把整队卡死。
        // 先把阶段清成 Idle，迟到的搜索 / 下载回调会被阶段检查丢掉。
        if (!m_running)
            return;
        m_stage = Stage::Idle;
        m_busyDownload = false;
        m_expectedDetailId.clear();
        m_expectedCfDetailId.clear();
        finishItem(false, tr("下载源响应太慢，稍后可以手动补上"), QString(), QString());
    });

    connect(m_mr, &ModrinthAPI::searchCompleted, this, &LobbyContentFetcher::onMrSearchDone);
    connect(m_mr, &ModrinthAPI::searchFailed, this, &LobbyContentFetcher::onMrSearchFailed);
    connect(m_mr, &ModrinthAPI::modDetailReceived, this, &LobbyContentFetcher::onMrDetailDone);
    connect(m_mr, &ModrinthAPI::modDetailFailed, this, &LobbyContentFetcher::onMrDetailFailed);

    connect(m_cf, &CurseForgeAPI::searchCompleted, this, &LobbyContentFetcher::onCfSearchDone);
    connect(m_cf, &CurseForgeAPI::searchFailed, this, &LobbyContentFetcher::onCfSearchFailed);
    connect(m_cf, &CurseForgeAPI::modDetailReceived, this, &LobbyContentFetcher::onCfDetailDone);
    connect(m_cf, &CurseForgeAPI::modDetailFailed, this, &LobbyContentFetcher::onCfDetailFailed);

    connect(m_downloader, &ContentDownloader::downloadFinished, this,
            &LobbyContentFetcher::downloadFinished);
    connect(m_downloader, &ContentDownloader::downloadFailed, this,
            &LobbyContentFetcher::downloadFailed);
    connect(m_downloader, &ContentDownloader::downloadProgress, this,
            &LobbyContentFetcher::downloadProgress);
}

LobbyContentFetcher::~LobbyContentFetcher() = default;

void LobbyContentFetcher::start(const QString &instancePath, const QString &gameVersion,
                                const QString &loader, const QList<LobbyFetchRequest> &requests)
{
    if (m_running)
        cancel();

    m_instancePath = instancePath;
    m_gameVersion = gameVersion.trimmed();
    m_loader = loader.trimmed();
    m_requests = requests;
    m_results.clear();
    m_results.resize(requests.size());
    for (int i = 0; i < m_results.size(); ++i)
        m_results[i].request = requests.at(i);

    m_index = -1;
    m_okCount = 0;
    m_failCount = 0;
    m_cancelled = false;
    m_busyDownload = false;
    m_running = !requests.isEmpty();

    if (!m_running)
    {
        emit finished(0, 0);
        return;
    }
    processNext();
}

void LobbyContentFetcher::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    m_stage = Stage::Idle;
    stopWatchdog();
    m_busyDownload = false;
    finishAll();
}

void LobbyContentFetcher::processNext()
{
    if (m_cancelled)
    {
        finishAll();
        return;
    }

    // 下标前进一位；跳过本机已经有同名文件的项（别的房间刚装过）
    while (true)
    {
        ++m_index;
        if (m_index >= m_requests.size())
        {
            finishAll();
            return;
        }

        const LobbyFetchRequest &request = m_requests.at(m_index);
        if (request.fileName.trimmed().isEmpty())
        {
            LobbyFetchResult result;
            result.request = request;
            result.attempted = true;
            result.message = tr("房间里的文件名为空，跳过");
            m_results[m_index] = result;
            ++m_failCount;
            emit itemFinished(m_index, result);
            continue;
        }
        if (localHasFile(m_instancePath, request.type, request.fileName))
        {
            LobbyFetchResult result;
            result.request = request;
            result.attempted = true;
            result.ok = true;
            result.savedName = request.fileName;
            result.message = tr("本机已有同名文件");
            m_results[m_index] = result;
            ++m_okCount;
            emit itemFinished(m_index, result);
            continue;
        }
        break;
    }

    m_queries = buildSearchQueries(m_requests.at(m_index).fileName);
    m_queryIndex = 0;
    m_mrCandidates.clear();
    m_mrCandidateIndex = 0;
    m_cfCandidates.clear();
    m_cfCandidateIndex = 0;

    emit progressChanged(m_okCount + m_failCount, m_requests.size(),
                         tr("正在准备 %1/%2：%3")
                             .arg(m_okCount + m_failCount + 1)
                             .arg(m_requests.size())
                             .arg(m_requests.at(m_index).fileName));
    beginModrinth();
}

void LobbyContentFetcher::beginModrinth()
{
    const LobbyFetchRequest &request = m_requests.at(m_index);
    const ContentTypeConfig config = ContentTypeConfig::getConfig(request.type);
    if (config.mrProjectType.isEmpty())
    {
        beginCurseForge();
        return;
    }

    m_platform = QStringLiteral("Modrinth");
    m_stage = Stage::Searching;
    m_mr->setProjectType(config.mrProjectType);
    armWatchdog();
    m_mr->searchMods(currentQuery(), m_gameVersion, QString(), 0, kSearchPageSize);
}

void LobbyContentFetcher::beginCurseForge()
{
    const LobbyFetchRequest &request = m_requests.at(m_index);
    const ContentTypeConfig config = ContentTypeConfig::getConfig(request.type);
    m_platform = QStringLiteral("CurseForge");
    m_stage = Stage::Searching;
    m_cf->setClassId(config.cfClassId);
    armWatchdog();
    m_cf->searchMods(currentQuery(), m_gameVersion, QString(), 0, kSearchPageSize);
}

void LobbyContentFetcher::armWatchdog(int timeoutMs)
{
    stopWatchdog();
    m_watchdog->start(timeoutMs > 0 ? timeoutMs : kStepTimeoutMs);
}

void LobbyContentFetcher::stopWatchdog()
{
    if (m_watchdog)
        m_watchdog->stop();
}

QString LobbyContentFetcher::currentQuery() const
{
    if (m_queries.isEmpty())
        return QString();
    return m_queries.at(qBound(0, m_queryIndex, m_queries.size() - 1));
}

bool LobbyContentFetcher::advanceQuery()
{
    if (m_queryIndex + 1 < m_queries.size())
    {
        ++m_queryIndex;
        return true;
    }
    return false;
}

QList<ModInfo> LobbyContentFetcher::rankCandidates(const QList<ModInfo> &hits,
                                                   const QString &query) const
{
    QList<QPair<int, int>> ranked; // 相似度, 原始下标
    ranked.reserve(hits.size());
    for (int i = 0; i < hits.size(); ++i)
        ranked.append({nameSimilarity(query, hits.at(i).name), i});

    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const QPair<int, int> &a, const QPair<int, int> &b) {
                         return a.first > b.first;
                     });

    QList<ModInfo> ordered;
    ordered.reserve(hits.size());
    for (const auto &pair : ranked)
    {
        // 相似度为 0 的结果离题太远，直接丢掉（搜索排序第一条通常也没意义）
        if (pair.first <= 0)
            continue;
        ordered.append(hits.at(pair.second));
    }
    return ordered;
}

bool LobbyContentFetcher::pickVersionFile(const ModInfo &detail, ModVersionFile *fileOut) const
{
    const LobbyFetchRequest &request = m_requests.at(m_index);
    const QString wantedVersion = m_gameVersion.trimmed();
    const bool needLoader = request.type == ContentType::Mod && !m_loader.trimmed().isEmpty();
    const QString wantedLoader = m_loader.trimmed().toLower();

    // 1. 文件名完全相同：房主用的就是这个文件，装上后最容易对上
    for (const ModVersionFile &file : detail.versionFiles)
    {
        if (!file.fileName.isEmpty()
            && file.fileName.compare(request.fileName, Qt::CaseInsensitive) == 0
            && !file.downloadUrl.isEmpty())
        {
            if (fileOut)
                *fileOut = file;
            return true;
        }
    }

    // 2. 版本 / 加载器兼容里最新的那个
    const ModVersionFile *best = nullptr;
    for (const ModVersionFile &file : detail.versionFiles)
    {
        if (file.downloadUrl.isEmpty())
            continue;
        if (!wantedVersion.isEmpty() && !file.gameVersions.contains(wantedVersion))
            continue;
        if (needLoader && !file.loaders.isEmpty()
            && !file.loaders.contains(wantedLoader, Qt::CaseInsensitive))
            continue;
        if (!best || file.datePublished > best->datePublished)
            best = &file;
    }
    if (best)
    {
        if (fileOut)
            *fileOut = *best;
        return true;
    }

    // 3. 放宽加载器：版本对得上就先用（加载器不对顶多进不去，提示里会说）
    for (const ModVersionFile &file : detail.versionFiles)
    {
        if (file.downloadUrl.isEmpty())
            continue;
        if (!wantedVersion.isEmpty() && !file.gameVersions.contains(wantedVersion))
            continue;
        if (!best || file.datePublished > best->datePublished)
            best = &file;
    }
    if (best)
    {
        if (fileOut)
            *fileOut = *best;
        return true;
    }
    return false;
}

bool LobbyContentFetcher::tryNextMrCandidate()
{
    while (++m_mrCandidateIndex < m_mrCandidates.size()
           && m_mrCandidateIndex < kMaxProjectTries)
    {
        const ModInfo &project = m_mrCandidates.at(m_mrCandidateIndex);
        if (project.id.isEmpty())
            continue;
        m_stage = Stage::Detailing;
        m_expectedDetailId = project.id;
        armWatchdog();
        m_mr->fetchModDetail(project.id);
        return true;
    }
    return false;
}

bool LobbyContentFetcher::tryNextCfCandidate()
{
    while (++m_cfCandidateIndex < m_cfCandidates.size()
           && m_cfCandidateIndex < kMaxProjectTries)
    {
        const ModInfo &project = m_cfCandidates.at(m_cfCandidateIndex);
        if (project.id.isEmpty())
            continue;
        m_stage = Stage::Detailing;
        m_expectedCfDetailId = project.id;
        armWatchdog();
        m_cf->fetchModDetail(project.id);
        return true;
    }
    return false;
}

void LobbyContentFetcher::onMrSearchDone(const ModSearchResult &result)
{
    if (!m_running || m_stage != Stage::Searching)
        return;
    stopWatchdog();

    const QString query = currentQuery();
    m_mrCandidates = rankCandidates(result.mods, query);
    m_mrCandidateIndex = -1;

    if (!m_mrCandidates.isEmpty() && tryNextMrCandidate())
        return;

    // 这一轮搜索没命中：换下一个搜索词再试，全部用完再退 CurseForge
    if (advanceQuery())
    {
        beginModrinth();
        return;
    }
    beginCurseForge();
}

void LobbyContentFetcher::onMrSearchFailed(const QString &error)
{
    if (!m_running || m_stage != Stage::Searching)
        return;
    stopWatchdog();
    Q_UNUSED(error);

    if (advanceQuery())
    {
        beginModrinth();
        return;
    }
    beginCurseForge();
}

void LobbyContentFetcher::onMrDetailDone(const ModInfo &detail)
{
    if (!m_running || m_stage != Stage::Detailing)
        return;
    // 超时后旧请求才回来：项目 id 对不上就丢掉
    if (!m_expectedDetailId.isEmpty() && !detail.id.isEmpty()
        && detail.id != m_expectedDetailId)
        return;
    stopWatchdog();
    m_expectedDetailId.clear();

    ModVersionFile file;
    if (pickVersionFile(detail, &file))
    {
        m_platform = QStringLiteral("Modrinth");
        requestDownload(file.downloadUrl,
                        file.fileName.isEmpty() ? m_requests.at(m_index).fileName : file.fileName);
        return;
    }

    if (tryNextMrCandidate())
        return;
    beginCurseForge();
}

void LobbyContentFetcher::onMrDetailFailed(const QString &error)
{
    if (!m_running || m_stage != Stage::Detailing)
        return;
    stopWatchdog();
    m_expectedDetailId.clear();
    Q_UNUSED(error);

    if (tryNextMrCandidate())
        return;
    if (advanceQuery())
    {
        beginModrinth();
        return;
    }
    beginCurseForge();
}

void LobbyContentFetcher::onCfSearchDone(const ModSearchResult &result)
{
    if (!m_running || m_stage != Stage::Searching)
        return;
    stopWatchdog();

    const QString query = currentQuery();
    m_cfCandidates = rankCandidates(result.mods, query);
    m_cfCandidateIndex = -1;

    if (!m_cfCandidates.isEmpty() && tryNextCfCandidate())
        return;

    if (advanceQuery())
    {
        // 搜索词还能再退一步：先回 Modrinth 再试，实在不行再来这里
        beginModrinth();
        return;
    }
    finishItem(false, tr("下载源里没找到这个文件，可以在「资源」里手动搜一下"), QString(),
               QString());
}

void LobbyContentFetcher::onCfSearchFailed(const QString &error)
{
    if (!m_running || m_stage != Stage::Searching)
        return;
    stopWatchdog();

    if (advanceQuery())
    {
        beginModrinth();
        return;
    }
    finishItem(false,
               error.trimmed().isEmpty()
                   ? tr("下载源里没找到这个文件，可以在「资源」里手动搜一下")
                   : error.trimmed(),
               QString(), QString());
}

void LobbyContentFetcher::onCfDetailDone(const ModInfo &detail)
{
    if (!m_running || m_stage != Stage::Detailing)
        return;
    if (!m_expectedCfDetailId.isEmpty() && !detail.id.isEmpty()
        && detail.id != m_expectedCfDetailId)
        return;
    stopWatchdog();
    m_expectedCfDetailId.clear();

    ModVersionFile file;
    if (pickVersionFile(detail, &file))
    {
        m_platform = QStringLiteral("CurseForge");
        requestDownload(file.downloadUrl,
                        file.fileName.isEmpty() ? m_requests.at(m_index).fileName : file.fileName);
        return;
    }
    if (tryNextCfCandidate())
        return;
    finishItem(false, tr("下载源里没找到对应版本 / 加载器的文件"), QString(), QString());
}

void LobbyContentFetcher::onCfDetailFailed(const QString &error)
{
    if (!m_running || m_stage != Stage::Detailing)
        return;
    stopWatchdog();
    m_expectedCfDetailId.clear();
    Q_UNUSED(error);

    if (tryNextCfCandidate())
        return;
    finishItem(false, tr("下载源里没找到这个文件，可以在「资源」里手动搜一下"), QString(),
               QString());
}

void LobbyContentFetcher::requestDownload(const QString &url, const QString &fileName)
{
    if (url.trimmed().isEmpty())
    {
        finishItem(false, tr("下载地址为空"), QString(), QString());
        return;
    }

    m_stage = Stage::Downloading;
    m_busyDownload = true;
    armWatchdog(kDownloadIdleTimeoutMs);
    emit progressChanged(m_okCount + m_failCount, m_requests.size(),
                         tr("正在下载 %1/%2：%3")
                             .arg(m_okCount + m_failCount + 1)
                             .arg(m_requests.size())
                             .arg(fileName));
    m_downloader->downloadToInstance(url, fileName, m_instancePath,
                                     m_requests.at(m_index).type);
}

void LobbyContentFetcher::downloadFinished(const QString &filePath)
{
    if (!m_running || m_stage != Stage::Downloading)
        return;
    stopWatchdog();
    m_busyDownload = false;

    const QString saved = QFileInfo(filePath).fileName();
    finishItem(true, QString(), saved, m_platform);
}

void LobbyContentFetcher::downloadFailed(const QString &error)
{
    if (!m_running || m_stage != Stage::Downloading)
        return;
    stopWatchdog();
    m_busyDownload = false;
    finishItem(false, error, QString(), m_platform);
}

void LobbyContentFetcher::downloadProgress(qint64 received, qint64 total)
{
    if (!m_running || m_stage != Stage::Downloading)
        return;
    // 有进度就重置静默超时，并按百分比刷一句提示（大文件也能看出在动）
    armWatchdog(kDownloadIdleTimeoutMs);
    if (total > 0)
    {
        const int percent = int((received * 100) / total);
        emit progressChanged(m_okCount + m_failCount, m_requests.size(),
                             tr("正在下载 %1/%2：%3（%4%）")
                                 .arg(m_okCount + m_failCount + 1)
                                 .arg(m_requests.size())
                                 .arg(m_requests.at(m_index).fileName)
                                 .arg(percent));
    }
}

void LobbyContentFetcher::finishItem(bool ok, const QString &message, const QString &savedName,
                                     const QString &source)
{
    stopWatchdog();
    m_busyDownload = false;
    m_stage = Stage::Idle;
    m_expectedDetailId.clear();
    m_expectedCfDetailId.clear();

    LobbyFetchResult result;
    result.request = m_requests.at(m_index);
    result.attempted = true;
    result.ok = ok;
    result.savedName = savedName;
    result.source = source;
    result.message = message;
    m_results[m_index] = result;

    if (ok)
        ++m_okCount;
    else
        ++m_failCount;

    emit itemFinished(m_index, result);
    processNext();
}

void LobbyContentFetcher::finishAll()
{
    if (!m_running)
        return;
    m_running = false;
    stopWatchdog();
    m_busyDownload = false;

    // 取消后没轮到的项：明确标成未处理，界面上不显示为失败
    for (int i = 0; i < m_results.size(); ++i)
    {
        if (!m_results.at(i).attempted)
            m_results[i].request = m_requests.at(i);
    }

    emit finished(m_okCount, m_failCount);
}
